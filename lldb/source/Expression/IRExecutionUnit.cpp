//===-- IRExecutionUnit.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"

using namespace lldb_private;

IRExecutionUnit::IRExecutionUnit (std::unique_ptr<llvm::LLVMContext> &context_ap,
                                  std::unique_ptr<llvm::Module> &module_ap,
                                  ConstString &name,
                                  const lldb::TargetSP &target_sp,
                                  std::vector<std::string> &cpu_features) :
    IRMemoryMap(target_sp),
    m_context_ap(context_ap.release()),
    m_module_ap(module_ap.release()),
    m_module(m_module_ap.get()),
    m_cpu_features(cpu_features),
    m_name(name),
    m_did_jit(false),
    m_function_load_addr(LLDB_INVALID_ADDRESS),
    m_function_end_load_addr(LLDB_INVALID_ADDRESS)
{
}

lldb::addr_t
IRExecutionUnit::WriteNow (const uint8_t *bytes,
                           size_t size,
                           Error &error)
{
    lldb::addr_t allocation_process_addr = Malloc (size,
                                                   8,
                                                   lldb::ePermissionsWritable | lldb::ePermissionsReadable,
                                                   eAllocationPolicyMirror,
                                                   error);

    if (!error.Success())
        return LLDB_INVALID_ADDRESS;

    WriteMemory(allocation_process_addr, bytes, size, error);

    if (!error.Success())
    {
        Error err;
        Free (allocation_process_addr, err);

        return LLDB_INVALID_ADDRESS;
    }

    if (Log *log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS))
    {
        DataBufferHeap my_buffer(size, 0);
        Error err;
        ReadMemory(my_buffer.GetBytes(), allocation_process_addr, size, err);

        if (err.Success())
        {
            DataExtractor my_extractor(my_buffer.GetBytes(), my_buffer.GetByteSize(), lldb::eByteOrderBig, 8);
            my_extractor.PutToLog(log, 0, my_buffer.GetByteSize(), allocation_process_addr, 16, DataExtractor::TypeUInt8);
        }
    }

    return allocation_process_addr;
}

void
IRExecutionUnit::FreeNow (lldb::addr_t allocation)
{
    if (allocation == LLDB_INVALID_ADDRESS)
        return;

    Error err;

    Free(allocation, err);
}

Error
IRExecutionUnit::DisassembleFunction (Stream &stream,
                                      lldb::ProcessSP &process_wp)
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

    ExecutionContext exe_ctx(process_wp);

    Error ret;

    ret.Clear();

    lldb::addr_t func_local_addr = LLDB_INVALID_ADDRESS;
    lldb::addr_t func_remote_addr = LLDB_INVALID_ADDRESS;

    for (JittedFunction &function : m_jitted_functions)
    {
        if (strstr(function.m_name.c_str(), m_name.AsCString()))
        {
            func_local_addr = function.m_local_addr;
            func_remote_addr = function.m_remote_addr;
        }
    }

    if (func_local_addr == LLDB_INVALID_ADDRESS)
    {
        ret.SetErrorToGenericError();
        ret.SetErrorStringWithFormat("Couldn't find function %s for disassembly", m_name.AsCString());
        return ret;
    }

    if (log)
        log->Printf("Found function, has local address 0x%" PRIx64 " and remote address 0x%" PRIx64, (uint64_t)func_local_addr, (uint64_t)func_remote_addr);

    std::pair <lldb::addr_t, lldb::addr_t> func_range;

    func_range = GetRemoteRangeForLocal(func_local_addr);

    if (func_range.first == 0 && func_range.second == 0)
    {
        ret.SetErrorToGenericError();
        ret.SetErrorStringWithFormat("Couldn't find code range for function %s", m_name.AsCString());
        return ret;
    }

    if (log)
        log->Printf("Function's code range is [0x%" PRIx64 "+0x%" PRIx64 "]", func_range.first, func_range.second);

    Target *target = exe_ctx.GetTargetPtr();
    if (!target)
    {
        ret.SetErrorToGenericError();
        ret.SetErrorString("Couldn't find the target");
        return ret;
    }

    lldb::DataBufferSP buffer_sp(new DataBufferHeap(func_range.second, 0));

    Process *process = exe_ctx.GetProcessPtr();
    Error err;
    process->ReadMemory(func_remote_addr, buffer_sp->GetBytes(), buffer_sp->GetByteSize(), err);

    if (!err.Success())
    {
        ret.SetErrorToGenericError();
        ret.SetErrorStringWithFormat("Couldn't read from process: %s", err.AsCString("unknown error"));
        return ret;
    }

    ArchSpec arch(target->GetArchitecture());

    const char *plugin_name = NULL;
    const char *flavor_string = NULL;
    lldb::DisassemblerSP disassembler_sp = Disassembler::FindPlugin(arch, flavor_string, plugin_name);

    if (!disassembler_sp)
    {
        ret.SetErrorToGenericError();
        ret.SetErrorStringWithFormat("Unable to find disassembler plug-in for %s architecture.", arch.GetArchitectureName());
        return ret;
    }

    if (!process)
    {
        ret.SetErrorToGenericError();
        ret.SetErrorString("Couldn't find the process");
        return ret;
    }

    DataExtractor extractor(buffer_sp,
                            process->GetByteOrder(),
                            target->GetArchitecture().GetAddressByteSize());

    if (log)
    {
        log->Printf("Function data has contents:");
        extractor.PutToLog (log,
                            0,
                            extractor.GetByteSize(),
                            func_remote_addr,
                            16,
                            DataExtractor::TypeUInt8);
    }

    disassembler_sp->DecodeInstructions (Address (func_remote_addr), extractor, 0, UINT32_MAX, false, false);

    InstructionList &instruction_list = disassembler_sp->GetInstructionList();
    const uint32_t max_opcode_byte_size = instruction_list.GetMaxOpcocdeByteSize();
    const char *disassemble_format = "${addr-file-or-load}: ";
    if (exe_ctx.HasTargetScope())
    {
        disassemble_format = exe_ctx.GetTargetRef().GetDebugger().GetDisassemblyFormat();
    }

    for (size_t instruction_index = 0, num_instructions = instruction_list.GetSize();
         instruction_index < num_instructions;
         ++instruction_index)
    {
        Instruction *instruction = instruction_list.GetInstructionAtIndex(instruction_index).get();
        instruction->Dump (&stream,
                           max_opcode_byte_size,
                           true,
                           true,
                           &exe_ctx,
                           NULL,
                           NULL,
                           disassemble_format);
        stream.PutChar('\n');
    }
    // FIXME: The DisassemblerLLVMC has a reference cycle and won't go away if it has any active instructions.
    // I'll fix that but for now, just clear the list and it will go away nicely.
    disassembler_sp->GetInstructionList().Clear();
    return ret;
}

static void ReportInlineAsmError(const llvm::SMDiagnostic &diagnostic, void *Context, unsigned LocCookie)
{
    Error *err = static_cast<Error*>(Context);

    if (err && err->Success())
    {
        err->SetErrorToGenericError();
        err->SetErrorStringWithFormat("Inline assembly error: %s", diagnostic.getMessage().str().c_str());
    }
}

void
IRExecutionUnit::GetRunnableInfo(Error &error,
                                 lldb::addr_t &func_addr,
                                 lldb::addr_t &func_end)
{
    lldb::ProcessSP process_sp(GetProcessWP().lock());

    static Mutex s_runnable_info_mutex(Mutex::Type::eMutexTypeRecursive);

    func_addr = LLDB_INVALID_ADDRESS;
    func_end = LLDB_INVALID_ADDRESS;

    if (!process_sp)
    {
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't write the JIT compiled code into the process because the process is invalid");
        return;
    }

    if (m_did_jit)
    {
        func_addr = m_function_load_addr;
        func_end = m_function_end_load_addr;

        return;
    };

    Mutex::Locker runnable_info_mutex_locker(s_runnable_info_mutex);

    m_did_jit = true;

    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

    std::string error_string;

    if (log)
    {
        std::string s;
        llvm::raw_string_ostream oss(s);

        m_module->print(oss, NULL);

        oss.flush();

        log->Printf ("Module being sent to JIT: \n%s", s.c_str());
    }

    llvm::Triple triple(m_module->getTargetTriple());
    llvm::Function *function = m_module->getFunction (m_name.AsCString());
    llvm::Reloc::Model relocModel;
    llvm::CodeModel::Model codeModel;

    if (triple.isOSBinFormatELF())
    {
        relocModel = llvm::Reloc::Static;
        // This will be small for 32-bit and large for 64-bit.
        codeModel = llvm::CodeModel::JITDefault;
    }
    else
    {
        relocModel = llvm::Reloc::PIC_;
        codeModel = llvm::CodeModel::Small;
    }

    m_module_ap->getContext().setInlineAsmDiagnosticHandler(ReportInlineAsmError, &error);

    llvm::EngineBuilder builder(std::move(m_module_ap));

    builder.setEngineKind(llvm::EngineKind::JIT)
    .setErrorStr(&error_string)
    .setRelocationModel(relocModel)
    .setMCJITMemoryManager(new MemoryManager(*this))
    .setCodeModel(codeModel)
    .setOptLevel(llvm::CodeGenOpt::Less);

    llvm::StringRef mArch;
    llvm::StringRef mCPU;
    llvm::SmallVector<std::string, 0> mAttrs;

    for (std::string &feature : m_cpu_features)
        mAttrs.push_back(feature);

    llvm::TargetMachine *target_machine = builder.selectTarget(triple,
                                                               mArch,
                                                               mCPU,
                                                               mAttrs);

    m_execution_engine_ap.reset(builder.create(target_machine));

    if (!m_execution_engine_ap.get())
    {
        error.SetErrorToGenericError();
        error.SetErrorStringWithFormat("Couldn't JIT the function: %s", error_string.c_str());
        return;
    }

    // Make sure we see all sections, including ones that don't have relocations...
    m_execution_engine_ap->setProcessAllSections(true);

    m_execution_engine_ap->DisableLazyCompilation();

    // We don't actually need the function pointer here, this just forces it to get resolved.

    void *fun_ptr = m_execution_engine_ap->getPointerToFunction(function);

    if (!error.Success())
    {
        // We got an error through our callback!
        return;
    }

    if (!function)
    {
        error.SetErrorToGenericError();
        error.SetErrorStringWithFormat("Couldn't find '%s' in the JITted module", m_name.AsCString());
        return;
    }

    if (!fun_ptr)
    {
        error.SetErrorToGenericError();
        error.SetErrorStringWithFormat("'%s' was in the JITted module but wasn't lowered", m_name.AsCString());
        return;
    }

    m_jitted_functions.push_back (JittedFunction(m_name.AsCString(), (lldb::addr_t)fun_ptr));

    CommitAllocations(process_sp);
    ReportAllocations(*m_execution_engine_ap);
    WriteData(process_sp);

    for (JittedFunction &jitted_function : m_jitted_functions)
    {
        jitted_function.m_remote_addr = GetRemoteAddressForLocal (jitted_function.m_local_addr);

        if (!jitted_function.m_name.compare(m_name.AsCString()))
        {
            AddrRange func_range = GetRemoteRangeForLocal(jitted_function.m_local_addr);
            m_function_end_load_addr = func_range.first + func_range.second;
            m_function_load_addr = jitted_function.m_remote_addr;
        }
    }

    if (log)
    {
        log->Printf("Code can be run in the target.");

        StreamString disassembly_stream;

        Error err = DisassembleFunction(disassembly_stream, process_sp);

        if (!err.Success())
        {
            log->Printf("Couldn't disassemble function : %s", err.AsCString("unknown error"));
        }
        else
        {
            log->Printf("Function disassembly:\n%s", disassembly_stream.GetData());
        }

        log->Printf("Sections: ");
        for (AllocationRecord &record : m_records)
        {
            if (record.m_process_address != LLDB_INVALID_ADDRESS)
            {
                record.dump(log);

                DataBufferHeap my_buffer(record.m_size, 0);
                Error err;
                ReadMemory(my_buffer.GetBytes(), record.m_process_address, record.m_size, err);

                if (err.Success())
                {
                    DataExtractor my_extractor(my_buffer.GetBytes(), my_buffer.GetByteSize(), lldb::eByteOrderBig, 8);
                    my_extractor.PutToLog(log, 0, my_buffer.GetByteSize(), record.m_process_address, 16, DataExtractor::TypeUInt8);
                }
            }
        }
    }

    func_addr = m_function_load_addr;
    func_end = m_function_end_load_addr;

    return;
}

IRExecutionUnit::~IRExecutionUnit ()
{
    m_module_ap.reset();
    m_execution_engine_ap.reset();
    m_context_ap.reset();
}

IRExecutionUnit::MemoryManager::MemoryManager (IRExecutionUnit &parent) :
    m_default_mm_ap (new llvm::SectionMemoryManager()),
    m_parent (parent)
{
}

IRExecutionUnit::MemoryManager::~MemoryManager ()
{
}

lldb::SectionType
IRExecutionUnit::GetSectionTypeFromSectionName (const llvm::StringRef &name, IRExecutionUnit::AllocationKind alloc_kind)
{
    lldb::SectionType sect_type = lldb::eSectionTypeCode;
    switch (alloc_kind)
    {
        case AllocationKind::Stub:  sect_type = lldb::eSectionTypeCode; break;
        case AllocationKind::Code:  sect_type = lldb::eSectionTypeCode; break;
        case AllocationKind::Data:  sect_type = lldb::eSectionTypeData; break;
        case AllocationKind::Global:sect_type = lldb::eSectionTypeData; break;
        case AllocationKind::Bytes: sect_type = lldb::eSectionTypeOther; break;
    }

    if (!name.empty())
    {
        if (name.equals("__text") || name.equals(".text"))
            sect_type = lldb::eSectionTypeCode;
        else if (name.equals("__data") || name.equals(".data"))
            sect_type = lldb::eSectionTypeCode;
        else if (name.startswith("__debug_") || name.startswith(".debug_"))
        {
            const uint32_t name_idx = name[0] == '_' ? 8 : 7;
            llvm::StringRef dwarf_name(name.substr(name_idx));
            switch (dwarf_name[0])
            {
                case 'a':
                    if (dwarf_name.equals("abbrev"))
                        sect_type = lldb::eSectionTypeDWARFDebugAbbrev;
                    else if (dwarf_name.equals("aranges"))
                        sect_type = lldb::eSectionTypeDWARFDebugAranges;
                    break;

                case 'f':
                    if (dwarf_name.equals("frame"))
                        sect_type = lldb::eSectionTypeDWARFDebugFrame;
                    break;

                case 'i':
                    if (dwarf_name.equals("info"))
                        sect_type = lldb::eSectionTypeDWARFDebugInfo;
                    break;

                case 'l':
                    if (dwarf_name.equals("line"))
                        sect_type = lldb::eSectionTypeDWARFDebugLine;
                    else if (dwarf_name.equals("loc"))
                        sect_type = lldb::eSectionTypeDWARFDebugLoc;
                    break;

                case 'm':
                    if (dwarf_name.equals("macinfo"))
                        sect_type = lldb::eSectionTypeDWARFDebugMacInfo;
                    break;

                case 'p':
                    if (dwarf_name.equals("pubnames"))
                        sect_type = lldb::eSectionTypeDWARFDebugPubNames;
                    else if (dwarf_name.equals("pubtypes"))
                        sect_type = lldb::eSectionTypeDWARFDebugPubTypes;
                    break;

                case 's':
                    if (dwarf_name.equals("str"))
                        sect_type = lldb::eSectionTypeDWARFDebugStr;
                    break;

                case 'r':
                    if (dwarf_name.equals("ranges"))
                        sect_type = lldb::eSectionTypeDWARFDebugRanges;
                    break;

                default:
                    break;
            }
        }
        else if (name.startswith("__apple_") || name.startswith(".apple_"))
        {
#if 0
            const uint32_t name_idx = name[0] == '_' ? 8 : 7;
            llvm::StringRef apple_name(name.substr(name_idx));
            switch (apple_name[0])
            {
                case 'n':
                    if (apple_name.equals("names"))
                        sect_type = lldb::eSectionTypeDWARFAppleNames;
                    else if (apple_name.equals("namespac") || apple_name.equals("namespaces"))
                        sect_type = lldb::eSectionTypeDWARFAppleNamespaces;
                    break;
                case 't':
                    if (apple_name.equals("types"))
                        sect_type = lldb::eSectionTypeDWARFAppleTypes;
                    break;
                case 'o':
                    if (apple_name.equals("objc"))
                        sect_type = lldb::eSectionTypeDWARFAppleObjC;
                    break;
                default:
                    break;
            }
#else
            sect_type = lldb::eSectionTypeInvalid;
#endif
        }
        else if (name.equals("__objc_imageinfo"))
            sect_type = lldb::eSectionTypeOther;
    }
    return sect_type;
}

uint8_t *
IRExecutionUnit::MemoryManager::allocateCodeSection(uintptr_t Size,
                                                    unsigned Alignment,
                                                    unsigned SectionID,
                                                    llvm::StringRef SectionName)
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

    uint8_t *return_value = m_default_mm_ap->allocateCodeSection(Size, Alignment, SectionID, SectionName);

    m_parent.m_records.push_back(AllocationRecord((uintptr_t)return_value,
                                                  lldb::ePermissionsReadable | lldb::ePermissionsExecutable,
                                                  GetSectionTypeFromSectionName (SectionName, AllocationKind::Code),
                                                  Size,
                                                  Alignment,
                                                  SectionID,
                                                  SectionName.str().c_str()));

    if (log)
    {
        log->Printf("IRExecutionUnit::allocateCodeSection(Size=0x%" PRIx64 ", Alignment=%u, SectionID=%u) = %p",
                    (uint64_t)Size, Alignment, SectionID, return_value);
    }

    return return_value;
}

uint8_t *
IRExecutionUnit::MemoryManager::allocateDataSection(uintptr_t Size,
                                                    unsigned Alignment,
                                                    unsigned SectionID,
                                                    llvm::StringRef SectionName,
                                                    bool IsReadOnly)
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

    uint8_t *return_value = m_default_mm_ap->allocateDataSection(Size, Alignment, SectionID, SectionName, IsReadOnly);

    m_parent.m_records.push_back(AllocationRecord((uintptr_t)return_value,
                                                  lldb::ePermissionsReadable | (IsReadOnly ? 0 : lldb::ePermissionsWritable),
                                                  GetSectionTypeFromSectionName (SectionName, AllocationKind::Data),
                                                  Size,
                                                  Alignment,
                                                  SectionID,
                                                  SectionName.str().c_str()));
    if (log)
    {
        log->Printf("IRExecutionUnit::allocateDataSection(Size=0x%" PRIx64 ", Alignment=%u, SectionID=%u) = %p",
                    (uint64_t)Size, Alignment, SectionID, return_value);
    }

    return return_value;
}

lldb::addr_t
IRExecutionUnit::GetRemoteAddressForLocal (lldb::addr_t local_address)
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

    for (AllocationRecord &record : m_records)
    {
        if (local_address >= record.m_host_address &&
            local_address < record.m_host_address + record.m_size)
        {
            if (record.m_process_address == LLDB_INVALID_ADDRESS)
                return LLDB_INVALID_ADDRESS;

            lldb::addr_t ret = record.m_process_address + (local_address - record.m_host_address);

            if (log)
            {
                log->Printf("IRExecutionUnit::GetRemoteAddressForLocal() found 0x%" PRIx64 " in [0x%" PRIx64 "..0x%" PRIx64 "], and returned 0x%" PRIx64 " from [0x%" PRIx64 "..0x%" PRIx64 "].",
                            local_address,
                            (uint64_t)record.m_host_address,
                            (uint64_t)record.m_host_address + (uint64_t)record.m_size,
                            ret,
                            record.m_process_address,
                            record.m_process_address + record.m_size);
            }

            return ret;
        }
    }

    return LLDB_INVALID_ADDRESS;
}

IRExecutionUnit::AddrRange
IRExecutionUnit::GetRemoteRangeForLocal (lldb::addr_t local_address)
{
    for (AllocationRecord &record : m_records)
    {
        if (local_address >= record.m_host_address &&
            local_address < record.m_host_address + record.m_size)
        {
            if (record.m_process_address == LLDB_INVALID_ADDRESS)
                return AddrRange(0, 0);

            return AddrRange(record.m_process_address, record.m_size);
        }
    }

    return AddrRange (0, 0);
}

bool
IRExecutionUnit::CommitAllocations (lldb::ProcessSP &process_sp)
{
    bool ret = true;

    lldb_private::Error err;

    for (AllocationRecord &record : m_records)
    {
        if (record.m_process_address != LLDB_INVALID_ADDRESS)
            continue;

        switch (record.m_sect_type)
        {
        case lldb::eSectionTypeInvalid:
        case lldb::eSectionTypeDWARFDebugAbbrev:
        case lldb::eSectionTypeDWARFDebugAranges:
        case lldb::eSectionTypeDWARFDebugFrame:
        case lldb::eSectionTypeDWARFDebugInfo:
        case lldb::eSectionTypeDWARFDebugLine:
        case lldb::eSectionTypeDWARFDebugLoc:
        case lldb::eSectionTypeDWARFDebugMacInfo:
        case lldb::eSectionTypeDWARFDebugPubNames:
        case lldb::eSectionTypeDWARFDebugPubTypes:
        case lldb::eSectionTypeDWARFDebugRanges:
        case lldb::eSectionTypeDWARFDebugStr:
        case lldb::eSectionTypeDWARFAppleNames:
        case lldb::eSectionTypeDWARFAppleTypes:
        case lldb::eSectionTypeDWARFAppleNamespaces:
        case lldb::eSectionTypeDWARFAppleObjC:
            err.Clear();
            break;
        default:
            record.m_process_address = Malloc (record.m_size,
                                               record.m_alignment,
                                               record.m_permissions,
                                               eAllocationPolicyProcessOnly,
                                               err);
            break;
        }

        if (!err.Success())
        {
            ret = false;
            break;
        }
    }

    if (!ret)
    {
        for (AllocationRecord &record : m_records)
        {
            if (record.m_process_address != LLDB_INVALID_ADDRESS)
            {
                Free(record.m_process_address, err);
                record.m_process_address = LLDB_INVALID_ADDRESS;
            }
        }
    }

    return ret;
}

void
IRExecutionUnit::ReportAllocations (llvm::ExecutionEngine &engine)
{
    for (AllocationRecord &record : m_records)
    {
        if (record.m_process_address == LLDB_INVALID_ADDRESS)
            continue;

        if (record.m_section_id == eSectionIDInvalid)
            continue;

        engine.mapSectionAddress((void*)record.m_host_address, record.m_process_address);
    }

    // Trigger re-application of relocations.
    engine.finalizeObject();
}

bool
IRExecutionUnit::WriteData (lldb::ProcessSP &process_sp)
{
    bool wrote_something = false;
    for (AllocationRecord &record : m_records)
    {
        if (record.m_process_address != LLDB_INVALID_ADDRESS)
        {
            lldb_private::Error err;
            WriteMemory (record.m_process_address, (uint8_t*)record.m_host_address, record.m_size, err);
            if (err.Success())
                wrote_something = true;
        }
    }
    return wrote_something;
}

void
IRExecutionUnit::AllocationRecord::dump (Log *log)
{
    if (!log)
        return;

    log->Printf("[0x%llx+0x%llx]->0x%llx (alignment %d, section ID %d)",
                (unsigned long long)m_host_address,
                (unsigned long long)m_size,
                (unsigned long long)m_process_address,
                (unsigned)m_alignment,
                (unsigned)m_section_id);
}


lldb::ByteOrder
IRExecutionUnit::GetByteOrder () const
{
    ExecutionContext exe_ctx (GetBestExecutionContextScope());
    return exe_ctx.GetByteOrder();
}

uint32_t
IRExecutionUnit::GetAddressByteSize () const
{
    ExecutionContext exe_ctx (GetBestExecutionContextScope());
    return exe_ctx.GetAddressByteSize();
}

void
IRExecutionUnit::PopulateSymtab (lldb_private::ObjectFile *obj_file,
                                 lldb_private::Symtab &symtab)
{
    // No symbols yet...
}


void
IRExecutionUnit::PopulateSectionList (lldb_private::ObjectFile *obj_file,
                                      lldb_private::SectionList &section_list)
{
    for (AllocationRecord &record : m_records)
    {
        if (record.m_size > 0)
        {
            lldb::SectionSP section_sp (new lldb_private::Section (obj_file->GetModule(),
                                                                   obj_file,
                                                                   record.m_section_id,
                                                                   ConstString(record.m_name),
                                                                   record.m_sect_type,
                                                                   record.m_process_address,
                                                                   record.m_size,
                                                                   record.m_host_address,   // file_offset (which is the host address for the data)
                                                                   record.m_size,           // file_size
                                                                   0,
                                                                   record.m_permissions));  // flags
            section_list.AddSection (section_sp);
        }
    }
}

bool
IRExecutionUnit::GetArchitecture (lldb_private::ArchSpec &arch)
{
    ExecutionContext exe_ctx (GetBestExecutionContextScope());
    Target *target = exe_ctx.GetTargetPtr();
    if (target)
        arch = target->GetArchitecture();
    else
        arch.Clear();
    return arch.IsValid();
}

lldb::ModuleSP
IRExecutionUnit::GetJITModule ()
{
    ExecutionContext exe_ctx (GetBestExecutionContextScope());
    Target *target = exe_ctx.GetTargetPtr();
    if (target)
    {
        lldb::ModuleSP jit_module_sp = lldb_private::Module::CreateJITModule (std::static_pointer_cast<lldb_private::ObjectFileJITDelegate>(shared_from_this()));
        if (jit_module_sp)
        {
            bool changed = false;
            jit_module_sp->SetLoadAddress(*target, 0, true, changed);
        }
        return jit_module_sp;
    }
    return lldb::ModuleSP();
}
