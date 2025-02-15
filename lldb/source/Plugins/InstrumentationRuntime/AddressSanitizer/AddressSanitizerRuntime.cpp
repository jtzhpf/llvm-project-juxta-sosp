//===-- AddressSanitizerRuntime.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AddressSanitizerRuntime.h"

#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Core/Module.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Target/InstrumentationRuntimeStopInfo.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Stream.h"
#include "lldb/Core/StreamFile.h"

using namespace lldb;
using namespace lldb_private;

lldb::InstrumentationRuntimeSP
AddressSanitizerRuntime::CreateInstance (const lldb::ProcessSP &process_sp)
{
    return InstrumentationRuntimeSP(new AddressSanitizerRuntime(process_sp));
}

void
AddressSanitizerRuntime::Initialize()
{
    PluginManager::RegisterPlugin (GetPluginNameStatic(),
                                   "AddressSanitizer instrumentation runtime plugin.",
                                   CreateInstance,
                                   GetTypeStatic);
}

void
AddressSanitizerRuntime::Terminate()
{
    PluginManager::UnregisterPlugin (CreateInstance);
}

lldb_private::ConstString
AddressSanitizerRuntime::GetPluginNameStatic()
{
    return ConstString("AddressSanitizer");
}

lldb::InstrumentationRuntimeType
AddressSanitizerRuntime::GetTypeStatic()
{
    return eInstrumentationRuntimeTypeAddressSanitizer;
}

AddressSanitizerRuntime::AddressSanitizerRuntime(const ProcessSP &process_sp) :
    m_is_active(false),
    m_runtime_module(),
    m_process(process_sp),
    m_breakpoint_id(0)
{
}

AddressSanitizerRuntime::~AddressSanitizerRuntime()
{
    Deactivate();
}

bool ModuleContainsASanRuntime(Module * module)
{
    SymbolContextList sc_list;
    const bool include_symbols = true;
    const bool append = true;
    const bool include_inlines = true;
    
    size_t num_matches = module->FindFunctions(ConstString("__asan_get_alloc_stack"), NULL, eFunctionNameTypeAuto, include_symbols, include_inlines, append, sc_list);
    
    return num_matches > 0;
}

void
AddressSanitizerRuntime::ModulesDidLoad(lldb_private::ModuleList &module_list)
{
    if (IsActive())
        return;
    
    if (m_runtime_module) {
        Activate();
        return;
    }
    
    Mutex::Locker modules_locker(module_list.GetMutex());
    const size_t num_modules = module_list.GetSize();
    for (size_t i = 0; i < num_modules; ++i)
    {
        Module *module_pointer = module_list.GetModulePointerAtIndexUnlocked(i);
        const FileSpec & file_spec = module_pointer->GetFileSpec();
        if (! file_spec)
            continue;
        
        static RegularExpression g_asan_runtime_regex("libclang_rt.asan_(.*)_dynamic\\.dylib");
        if (g_asan_runtime_regex.Execute (file_spec.GetFilename().GetCString()) || module_pointer->IsExecutable())
        {
            if (ModuleContainsASanRuntime(module_pointer))
            {
                m_runtime_module = module_pointer->shared_from_this();
                Activate();
                return;
            }
        }
    }
}

bool
AddressSanitizerRuntime::IsActive()
{
    return m_is_active;
}

#define RETRIEVE_REPORT_DATA_FUNCTION_TIMEOUT_USEC 2*1000*1000

const char *
address_sanitizer_retrieve_report_data_command = R"(
    struct {
        int present;
        void *pc, *bp, *sp, *address;
        int access_type;
        size_t access_size;
        const char *description;
    } t;

    t.present = ((int (*) ())__asan_report_present)();
    t.pc = ((void * (*) ())__asan_get_report_pc)();
    /* commented out because rdar://problem/18533301
    t.bp = ((void * (*) ())__asan_get_report_bp)();
    t.sp = ((void * (*) ())__asan_get_report_sp)();
    */
    t.address = ((void * (*) ())__asan_get_report_address)();
    t.description = ((const char * (*) ())__asan_get_report_description)();
    t.access_type = ((int (*) ())__asan_get_report_access_type)();
    t.access_size = ((size_t (*) ())__asan_get_report_access_size)();

    t;
)";

StructuredData::ObjectSP
AddressSanitizerRuntime::RetrieveReportData()
{
    ThreadSP thread_sp = m_process->GetThreadList().GetSelectedThread();
    StackFrameSP frame_sp = thread_sp->GetSelectedFrame();
    
    if (!frame_sp)
        return StructuredData::ObjectSP();
    
    EvaluateExpressionOptions options;
    options.SetUnwindOnError(true);
    options.SetTryAllThreads(true);
    options.SetStopOthers(true);
    options.SetIgnoreBreakpoints(true);
    options.SetTimeoutUsec(RETRIEVE_REPORT_DATA_FUNCTION_TIMEOUT_USEC);
    
    ValueObjectSP return_value_sp;
    if (m_process->GetTarget().EvaluateExpression(address_sanitizer_retrieve_report_data_command, frame_sp.get(), return_value_sp, options) != eExpressionCompleted)
        return StructuredData::ObjectSP();
    
    int present = return_value_sp->GetValueForExpressionPath(".present")->GetValueAsUnsigned(0);
    if (present != 1)
        return StructuredData::ObjectSP();
        
    addr_t pc = return_value_sp->GetValueForExpressionPath(".pc")->GetValueAsUnsigned(0);
    addr_t bp = return_value_sp->GetValueForExpressionPath(".bp")->GetValueAsUnsigned(0);
    addr_t sp = return_value_sp->GetValueForExpressionPath(".sp")->GetValueAsUnsigned(0);
    addr_t address = return_value_sp->GetValueForExpressionPath(".address")->GetValueAsUnsigned(0);
    addr_t access_type = return_value_sp->GetValueForExpressionPath(".access_type")->GetValueAsUnsigned(0);
    addr_t access_size = return_value_sp->GetValueForExpressionPath(".access_size")->GetValueAsUnsigned(0);
    addr_t description_ptr = return_value_sp->GetValueForExpressionPath(".description")->GetValueAsUnsigned(0);
    std::string description;
    Error error;
    m_process->ReadCStringFromMemory(description_ptr, description, error);
    
    StructuredData::Dictionary *dict = new StructuredData::Dictionary();
    dict->AddStringItem("instrumentation_class", "AddressSanitizer");
    dict->AddStringItem("stop_type", "fatal_error");
    dict->AddIntegerItem("pc", pc);
    dict->AddIntegerItem("bp", bp);
    dict->AddIntegerItem("sp", sp);
    dict->AddIntegerItem("address", address);
    dict->AddIntegerItem("access_type", access_type);
    dict->AddIntegerItem("access_size", access_size);
    dict->AddStringItem("description", description);

    return StructuredData::ObjectSP(dict);
}

std::string
AddressSanitizerRuntime::FormatDescription(StructuredData::ObjectSP report)
{
    std::string description = report->GetAsDictionary()->GetValueForKey("description")->GetAsString()->GetValue();
    if (description == "heap-use-after-free") {
        return "Use of deallocated memory detected";
    } else if (description == "heap-buffer-overflow") {
        return "Heap buffer overflow detected";
    } else if (description == "stack-buffer-underflow") {
        return "Stack buffer underflow detected";
    } else if (description == "initialization-order-fiasco") {
        return "Initialization order problem detected";
    } else if (description == "stack-buffer-overflow") {
        return "Stack buffer overflow detected";
    } else if (description == "stack-use-after-return") {
        return "Use of returned stack memory detected";
    } else if (description == "use-after-poison") {
        return "Use of poisoned memory detected";
    } else if (description == "container-overflow") {
        return "Container overflow detected";
    } else if (description == "stack-use-after-scope") {
        return "Use of out-of-scope stack memory detected";
    } else if (description == "global-buffer-overflow") {
        return "Global buffer overflow detected";
    } else if (description == "unknown-crash") {
        return "Invalid memory access detected";
    }
    
    // for unknown report codes just show the code
    return description;
}

bool
AddressSanitizerRuntime::NotifyBreakpointHit(void *baton, StoppointCallbackContext *context, user_id_t break_id, user_id_t break_loc_id)
{
    assert (baton && "null baton");
    if (!baton)
        return false;
    
    AddressSanitizerRuntime *const instance = static_cast<AddressSanitizerRuntime*>(baton);
    
    StructuredData::ObjectSP report = instance->RetrieveReportData();
    std::string description;
    if (report) {
        description = instance->FormatDescription(report);
    }
    ThreadSP thread = context->exe_ctx_ref.GetThreadSP();
    thread->SetStopInfo(InstrumentationRuntimeStopInfo::CreateStopReasonWithInstrumentationData(*thread, description.c_str(), report));

    instance->m_runtime_module->ReportWarning("AddressSanitizer report breakpoint hit. Use 'thread info -s' to get extended information about the report.\n");
    
    // Return true to stop the target, false to just let the target run.
    return true;
}

void
AddressSanitizerRuntime::Activate()
{
    if (m_is_active)
        return;

    ConstString symbol_name ("__asan::AsanDie()");
    const Symbol *symbol = m_runtime_module->FindFirstSymbolWithNameAndType (symbol_name, eSymbolTypeCode);
    
    if (symbol == NULL)
        return;
    
    if (!symbol->GetAddress().IsValid())
        return;
    
    Target &target = m_process->GetTarget();
    addr_t symbol_address = symbol->GetAddress().GetOpcodeLoadAddress(&target);
    
    if (symbol_address == LLDB_INVALID_ADDRESS)
        return;
    
    bool internal = true;
    bool hardware = false;
    Breakpoint *breakpoint = m_process->GetTarget().CreateBreakpoint(symbol_address, internal, hardware).get();
    breakpoint->SetCallback (AddressSanitizerRuntime::NotifyBreakpointHit, this, true);
    breakpoint->SetBreakpointKind ("address-sanitizer-report");
    m_breakpoint_id = breakpoint->GetID();
    
    m_runtime_module->ReportWarning("AddressSanitizer debugger support is active. Memory error breakpoint has been installed and you can now use the 'memory history' command.\n");
    
    m_is_active = true;
}

void
AddressSanitizerRuntime::Deactivate()
{
    if (m_breakpoint_id != LLDB_INVALID_BREAK_ID)
    {
        m_process->GetTarget().RemoveBreakpointByID(m_breakpoint_id);
        m_breakpoint_id = LLDB_INVALID_BREAK_ID;
    }
    m_is_active = false;
}
