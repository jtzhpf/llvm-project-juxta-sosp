//===-- TargetList.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-python.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/Broadcaster.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Event.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/State.h"
#include "lldb/Core/Timer.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionGroupPlatform.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/TargetList.h"

#include "llvm/ADT/SmallString.h"

using namespace lldb;
using namespace lldb_private;

ConstString &
TargetList::GetStaticBroadcasterClass ()
{
    static ConstString class_name ("lldb.targetList");
    return class_name;
}

//----------------------------------------------------------------------
// TargetList constructor
//----------------------------------------------------------------------
TargetList::TargetList(Debugger &debugger) :
    Broadcaster(&debugger, TargetList::GetStaticBroadcasterClass().AsCString()),
    m_target_list(),
    m_target_list_mutex (Mutex::eMutexTypeRecursive),
    m_selected_target_idx (0)
{
    CheckInWithManager();
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
TargetList::~TargetList()
{
    Mutex::Locker locker(m_target_list_mutex);
    m_target_list.clear();
}

Error
TargetList::CreateTarget (Debugger &debugger,
                          const char *user_exe_path,
                          const char *triple_cstr,
                          bool get_dependent_files,
                          const OptionGroupPlatform *platform_options,
                          TargetSP &target_sp)
{
    return CreateTargetInternal (debugger,
                                 user_exe_path,
                                 triple_cstr,
                                 get_dependent_files,
                                 platform_options,
                                 target_sp,
                                 false);
}

Error
TargetList::CreateTarget (Debugger &debugger,
                          const char *user_exe_path,
                          const ArchSpec& specified_arch,
                          bool get_dependent_files,
                          PlatformSP &platform_sp,
                          TargetSP &target_sp)
{
    return CreateTargetInternal (debugger,
                                 user_exe_path,
                                 specified_arch,
                                 get_dependent_files,
                                 platform_sp,
                                 target_sp,
                                 false);
}

Error
TargetList::CreateTargetInternal (Debugger &debugger,
                          const char *user_exe_path,
                          const char *triple_cstr,
                          bool get_dependent_files,
                          const OptionGroupPlatform *platform_options,
                          TargetSP &target_sp,
                          bool is_dummy_target)
{
    Error error;
    PlatformSP platform_sp;
    
    // This is purposely left empty unless it is specified by triple_cstr.
    // If not initialized via triple_cstr, then the currently selected platform
    // will set the architecture correctly.
    const ArchSpec arch(triple_cstr);
    if (triple_cstr && triple_cstr[0])
    {
        if (!arch.IsValid())
        {
            error.SetErrorStringWithFormat("invalid triple '%s'", triple_cstr);
            return error;
        }
    }
    
    ArchSpec platform_arch(arch);

    bool prefer_platform_arch = false;
    
    CommandInterpreter &interpreter = debugger.GetCommandInterpreter();
    if (platform_options && platform_options->PlatformWasSpecified ())
    {
        const bool select_platform = true;
        platform_sp = platform_options->CreatePlatformWithOptions (interpreter,
                                                                   arch,
                                                                   select_platform,
                                                                   error,
                                                                   platform_arch);
        if (!platform_sp)
            return error;
    }
    
    if (user_exe_path && user_exe_path[0])
    {
        ModuleSpecList module_specs;
        ModuleSpec module_spec;
        module_spec.GetFileSpec().SetFile(user_exe_path, true);
        
        // Resolve the executable in case we are given a path to a application bundle
        // like a .app bundle on MacOSX
        Host::ResolveExecutableInBundle (module_spec.GetFileSpec());

        lldb::offset_t file_offset = 0;
        lldb::offset_t file_size = 0;
        const size_t num_specs = ObjectFile::GetModuleSpecifications (module_spec.GetFileSpec(), file_offset, file_size, module_specs);
        if (num_specs > 0)
        {
            ModuleSpec matching_module_spec;

            if (num_specs == 1)
            {
                if (module_specs.GetModuleSpecAtIndex(0, matching_module_spec))
                {
                    if (platform_arch.IsValid())
                    {
                        if (platform_arch.IsCompatibleMatch(matching_module_spec.GetArchitecture()))
                        {
                            // If the OS or vendor weren't specified, then adopt the module's
                            // architecture so that the platform matching can be more accurate
                            if (!platform_arch.TripleOSWasSpecified() || !platform_arch.TripleVendorWasSpecified())
                            {
                                prefer_platform_arch = true;
                                platform_arch = matching_module_spec.GetArchitecture();
                            }
                        }
                        else
                        {
                            error.SetErrorStringWithFormat("the specified architecture '%s' is not compatible with '%s' in '%s'",
                                                           platform_arch.GetTriple().str().c_str(),
                                                           matching_module_spec.GetArchitecture().GetTriple().str().c_str(),
                                                           module_spec.GetFileSpec().GetPath().c_str());
                            return error;
                        }
                    }
                    else
                    {
                        // Only one arch and none was specified
                        prefer_platform_arch = true;
                        platform_arch = matching_module_spec.GetArchitecture();
                    }
                }
            }
            else
            {
                if (arch.IsValid())
                {
                    module_spec.GetArchitecture() = arch;
                    if (module_specs.FindMatchingModuleSpec(module_spec, matching_module_spec))
                    {
                        prefer_platform_arch = true;
                        platform_arch = matching_module_spec.GetArchitecture();
                    }
                }
                else
                {
                    // No architecture specified, check if there is only one platform for
                    // all of the architectures.
                    
                    typedef std::vector<PlatformSP> PlatformList;
                    PlatformList platforms;
                    PlatformSP host_platform_sp = Platform::GetHostPlatform();
                    for (size_t i=0; i<num_specs; ++i)
                    {
                        ModuleSpec module_spec;
                        if (module_specs.GetModuleSpecAtIndex(i, module_spec))
                        {
                            // See if there was a selected platform and check that first
                            // since the user may have specified it.
                            if (platform_sp)
                            {
                                if (platform_sp->IsCompatibleArchitecture(module_spec.GetArchitecture(), false, NULL))
                                {
                                    platforms.push_back(platform_sp);
                                    continue;
                                }
                            }
                            
                            // Next check the host platform it if wasn't already checked above
                            if (host_platform_sp && (!platform_sp || host_platform_sp->GetName() != platform_sp->GetName()))
                            {
                                if (host_platform_sp->IsCompatibleArchitecture(module_spec.GetArchitecture(), false, NULL))
                                {
                                    platforms.push_back(host_platform_sp);
                                    continue;
                                }
                            }
                            
                            // Just find a platform that matches the architecture in the executable file
                            platforms.push_back(Platform::GetPlatformForArchitecture(module_spec.GetArchitecture(), nullptr));
                        }
                    }
                    
                    Platform *platform_ptr = NULL;
                    for (const auto &the_platform_sp : platforms)
                    {
                        if (platform_ptr)
                        {
                            if (platform_ptr->GetName() != the_platform_sp->GetName())
                            {
                                platform_ptr = NULL;
                                break;
                            }
                        }
                        else
                        {
                            platform_ptr = the_platform_sp.get();
                        }
                    }
                    
                    if (platform_ptr)
                    {
                        // All platforms for all modules in the exectuable match, so we can select this platform
                        platform_sp = platforms.front();
                    }
                    else
                    {
                        // More than one platform claims to support this file, so the --platform option must be specified
                        StreamString error_strm;
                        std::set<Platform *> platform_set;
                        error_strm.Printf ("more than one platform supports this executable (");
                        for (const auto &the_platform_sp : platforms)
                        {
                            if (platform_set.find(the_platform_sp.get()) == platform_set.end())
                            {
                                if (!platform_set.empty())
                                    error_strm.PutCString(", ");
                                error_strm.PutCString(the_platform_sp->GetName().GetCString());
                                platform_set.insert(the_platform_sp.get());
                            }
                        }
                        error_strm.Printf("), use the --platform option to specify a platform");
                        error.SetErrorString(error_strm.GetString().c_str());
                        return error;
                    }
                }
            }
        }
    }

    if (!platform_sp)
    {
        // Get the current platform and make sure it is compatible with the
        // current architecture if we have a valid architecture.
        platform_sp = debugger.GetPlatformList().GetSelectedPlatform ();
        
        if (!prefer_platform_arch && arch.IsValid())
        {
            if (!platform_sp->IsCompatibleArchitecture(arch, false, &platform_arch))
            {
                platform_sp = Platform::GetPlatformForArchitecture(arch, &platform_arch);
                if (platform_sp)
                    debugger.GetPlatformList().SetSelectedPlatform(platform_sp);
            }
        }
        else if (platform_arch.IsValid())
        {
            // if "arch" isn't valid, yet "platform_arch" is, it means we have an executable file with
            // a single architecture which should be used
            ArchSpec fixed_platform_arch;
            if (!platform_sp->IsCompatibleArchitecture(platform_arch, false, &fixed_platform_arch))
            {
                platform_sp = Platform::GetPlatformForArchitecture(platform_arch, &fixed_platform_arch);
                if (platform_sp)
                    debugger.GetPlatformList().SetSelectedPlatform(platform_sp);
            }
        }
    }
    
    if (!platform_arch.IsValid())
        platform_arch = arch;

    error = TargetList::CreateTargetInternal (debugger,
                                              user_exe_path,
                                              platform_arch,
                                              get_dependent_files,
                                              platform_sp,
                                              target_sp,
                                              is_dummy_target);
    return error;
}

lldb::TargetSP
TargetList::GetDummyTarget (lldb_private::Debugger &debugger)
{
    // FIXME: Maybe the dummy target should be per-Debugger
    if (!m_dummy_target_sp || !m_dummy_target_sp->IsValid())
    {
        ArchSpec arch(Target::GetDefaultArchitecture());
        if (!arch.IsValid())
            arch = HostInfo::GetArchitecture();
        Error err = CreateDummyTarget(debugger,
                                      arch.GetTriple().getTriple().c_str(),
                                      m_dummy_target_sp);
    }

    return m_dummy_target_sp;
}

Error
TargetList::CreateDummyTarget (Debugger &debugger,
                               const char *specified_arch_name,
                               lldb::TargetSP &target_sp)
{
    PlatformSP host_platform_sp(Platform::GetHostPlatform());
    return CreateTargetInternal (debugger,
                                 (const char *) nullptr,
                                 specified_arch_name,
                                 false,
                                 (const OptionGroupPlatform *) nullptr,
                                 target_sp,
                                 true);
}

Error
TargetList::CreateTargetInternal (Debugger &debugger,
                      const char *user_exe_path,
                      const ArchSpec& specified_arch,
                      bool get_dependent_files,
                      lldb::PlatformSP &platform_sp,
                      lldb::TargetSP &target_sp,
                      bool is_dummy_target)
{

    Timer scoped_timer (__PRETTY_FUNCTION__,
                        "TargetList::CreateTarget (file = '%s', arch = '%s')",
                        user_exe_path,
                        specified_arch.GetArchitectureName());
    Error error;

    ArchSpec arch(specified_arch);

    if (arch.IsValid())
    {
        if (!platform_sp || !platform_sp->IsCompatibleArchitecture(arch, false, NULL))
            platform_sp = Platform::GetPlatformForArchitecture(specified_arch, &arch);
    }
    
    if (!platform_sp)
        platform_sp = debugger.GetPlatformList().GetSelectedPlatform();

    if (!arch.IsValid())
        arch = specified_arch;

    FileSpec file (user_exe_path, false);
    if (!file.Exists() && user_exe_path && user_exe_path[0] == '~')
    {
        // we want to expand the tilde but we don't want to resolve any symbolic links
        // so we can't use the FileSpec constructor's resolve flag
        llvm::SmallString<64> unglobbed_path(user_exe_path);
        FileSpec::ResolveUsername(unglobbed_path);

        if (unglobbed_path.empty())
            file = FileSpec(user_exe_path, false);
        else
            file = FileSpec(unglobbed_path.c_str(), false);
    }

    bool user_exe_path_is_bundle = false;
    char resolved_bundle_exe_path[PATH_MAX];
    resolved_bundle_exe_path[0] = '\0';
    if (file)
    {
        if (file.GetFileType() == FileSpec::eFileTypeDirectory)
            user_exe_path_is_bundle = true;

        if (file.IsRelativeToCurrentWorkingDirectory() && user_exe_path)
        {
            // Ignore paths that start with "./" and "../"
            if (!((user_exe_path[0] == '.' && user_exe_path[1] == '/') ||
                  (user_exe_path[0] == '.' && user_exe_path[1] == '.' && user_exe_path[2] == '/')))
            {
                char cwd[PATH_MAX];
                if (getcwd (cwd, sizeof(cwd)))
                {
                    std::string cwd_user_exe_path (cwd);
                    cwd_user_exe_path += '/';
                    cwd_user_exe_path += user_exe_path;
                    FileSpec cwd_file (cwd_user_exe_path.c_str(), false);
                    if (cwd_file.Exists())
                        file = cwd_file;
                }
            }
        }

        ModuleSP exe_module_sp;
        if (platform_sp)
        {
            FileSpecList executable_search_paths (Target::GetDefaultExecutableSearchPaths());
            ModuleSpec module_spec(file, arch);
            error = platform_sp->ResolveExecutable (module_spec,
                                                    exe_module_sp, 
                                                    executable_search_paths.GetSize() ? &executable_search_paths : NULL);
        }

        if (error.Success() && exe_module_sp)
        {
            if (exe_module_sp->GetObjectFile() == NULL)
            {
                if (arch.IsValid())
                {
                    error.SetErrorStringWithFormat("\"%s\" doesn't contain architecture %s",
                                                   file.GetPath().c_str(),
                                                   arch.GetArchitectureName());
                }
                else
                {
                    error.SetErrorStringWithFormat("unsupported file type \"%s\"",
                                                   file.GetPath().c_str());
                }
                return error;
            }
            target_sp.reset(new Target(debugger, arch, platform_sp, is_dummy_target));
            target_sp->SetExecutableModule (exe_module_sp, get_dependent_files);
            if (user_exe_path_is_bundle)
                exe_module_sp->GetFileSpec().GetPath(resolved_bundle_exe_path, sizeof(resolved_bundle_exe_path));
        }
    }
    else
    {
        // No file was specified, just create an empty target with any arch
        // if a valid arch was specified
        target_sp.reset(new Target(debugger, arch, platform_sp, is_dummy_target));
    }

    if (target_sp)
    {
        // Set argv0 with what the user typed, unless the user specified a
        // directory. If the user specified a directory, then it is probably a
        // bundle that was resolved and we need to use the resolved bundle path
        if (user_exe_path)
        {
            // Use exactly what the user typed as the first argument when we exec or posix_spawn
            if (user_exe_path_is_bundle && resolved_bundle_exe_path[0])
            {
                target_sp->SetArg0 (resolved_bundle_exe_path);
            }
            else
            {
                // Use resolved path
                target_sp->SetArg0 (file.GetPath().c_str());
            }
        }
        if (file.GetDirectory())
        {
            FileSpec file_dir;
            file_dir.GetDirectory() = file.GetDirectory();
            target_sp->GetExecutableSearchPaths ().Append (file_dir);
        }

        // Don't put the dummy target in the target list, it's held separately.
        if (!is_dummy_target)
        {
            Mutex::Locker locker(m_target_list_mutex);
            m_selected_target_idx = m_target_list.size();
            m_target_list.push_back(target_sp);
        }
        else
        {
            m_dummy_target_sp = target_sp;
        }
    }

    return error;
}

bool
TargetList::DeleteTarget (TargetSP &target_sp)
{
    Mutex::Locker locker(m_target_list_mutex);
    collection::iterator pos, end = m_target_list.end();

    for (pos = m_target_list.begin(); pos != end; ++pos)
    {
        if (pos->get() == target_sp.get())
        {
            m_target_list.erase(pos);
            return true;
        }
    }
    return false;
}


TargetSP
TargetList::FindTargetWithExecutableAndArchitecture
(
    const FileSpec &exe_file_spec,
    const ArchSpec *exe_arch_ptr
) const
{
    Mutex::Locker locker (m_target_list_mutex);
    TargetSP target_sp;
    bool full_match = (bool)exe_file_spec.GetDirectory();

    collection::const_iterator pos, end = m_target_list.end();
    for (pos = m_target_list.begin(); pos != end; ++pos)
    {
        Module *exe_module = (*pos)->GetExecutableModulePointer();

        if (exe_module)
        {
            if (FileSpec::Equal (exe_file_spec, exe_module->GetFileSpec(), full_match))
            {
                if (exe_arch_ptr)
                {
                    if (!exe_arch_ptr->IsCompatibleMatch(exe_module->GetArchitecture()))
                        continue;
                }
                target_sp = *pos;
                break;
            }
        }
    }
    return target_sp;
}

TargetSP
TargetList::FindTargetWithProcessID (lldb::pid_t pid) const
{
    Mutex::Locker locker(m_target_list_mutex);
    TargetSP target_sp;
    collection::const_iterator pos, end = m_target_list.end();
    for (pos = m_target_list.begin(); pos != end; ++pos)
    {
        Process* process = (*pos)->GetProcessSP().get();
        if (process && process->GetID() == pid)
        {
            target_sp = *pos;
            break;
        }
    }
    return target_sp;
}


TargetSP
TargetList::FindTargetWithProcess (Process *process) const
{
    TargetSP target_sp;
    if (process)
    {
        Mutex::Locker locker(m_target_list_mutex);
        collection::const_iterator pos, end = m_target_list.end();
        for (pos = m_target_list.begin(); pos != end; ++pos)
        {
            if (process == (*pos)->GetProcessSP().get())
            {
                target_sp = *pos;
                break;
            }
        }
    }
    return target_sp;
}

TargetSP
TargetList::GetTargetSP (Target *target) const
{
    TargetSP target_sp;
    if (target)
    {
        Mutex::Locker locker(m_target_list_mutex);
        collection::const_iterator pos, end = m_target_list.end();
        for (pos = m_target_list.begin(); pos != end; ++pos)
        {
            if (target == (*pos).get())
            {
                target_sp = *pos;
                break;
            }
        }
    }
    return target_sp;
}

uint32_t
TargetList::SendAsyncInterrupt (lldb::pid_t pid)
{
    uint32_t num_async_interrupts_sent = 0;

    if (pid != LLDB_INVALID_PROCESS_ID)
    {
        TargetSP target_sp(FindTargetWithProcessID (pid));
        if (target_sp.get())
        {
            Process* process = target_sp->GetProcessSP().get();
            if (process)
            {
                process->SendAsyncInterrupt();
                ++num_async_interrupts_sent;
            }
        }
    }
    else
    {
        // We don't have a valid pid to broadcast to, so broadcast to the target
        // list's async broadcaster...
        BroadcastEvent (Process::eBroadcastBitInterrupt, NULL);
    }

    return num_async_interrupts_sent;
}

uint32_t
TargetList::SignalIfRunning (lldb::pid_t pid, int signo)
{
    uint32_t num_signals_sent = 0;
    Process *process = NULL;
    if (pid == LLDB_INVALID_PROCESS_ID)
    {
        // Signal all processes with signal
        Mutex::Locker locker(m_target_list_mutex);
        collection::iterator pos, end = m_target_list.end();
        for (pos = m_target_list.begin(); pos != end; ++pos)
        {
            process = (*pos)->GetProcessSP().get();
            if (process)
            {
                if (process->IsAlive())
                {
                    ++num_signals_sent;
                    process->Signal (signo);
                }
            }
        }
    }
    else
    {
        // Signal a specific process with signal
        TargetSP target_sp(FindTargetWithProcessID (pid));
        if (target_sp.get())
        {
            process = target_sp->GetProcessSP().get();
            if (process)
            {
                if (process->IsAlive())
                {
                    ++num_signals_sent;
                    process->Signal (signo);
                }
            }
        }
    }
    return num_signals_sent;
}

int
TargetList::GetNumTargets () const
{
    Mutex::Locker locker (m_target_list_mutex);
    return m_target_list.size();
}

lldb::TargetSP
TargetList::GetTargetAtIndex (uint32_t idx) const
{
    TargetSP target_sp;
    Mutex::Locker locker (m_target_list_mutex);
    if (idx < m_target_list.size())
        target_sp = m_target_list[idx];
    return target_sp;
}

uint32_t
TargetList::GetIndexOfTarget (lldb::TargetSP target_sp) const
{
    Mutex::Locker locker (m_target_list_mutex);
    size_t num_targets = m_target_list.size();
    for (size_t idx = 0; idx < num_targets; idx++)
    {
        if (target_sp == m_target_list[idx])
            return idx;
    }
    return UINT32_MAX;
}

uint32_t
TargetList::SetSelectedTarget (Target* target)
{
    Mutex::Locker locker (m_target_list_mutex);
    collection::const_iterator pos,
        begin = m_target_list.begin(),
        end = m_target_list.end();
    for (pos = begin; pos != end; ++pos)
    {
        if (pos->get() == target)
        {
            m_selected_target_idx = std::distance (begin, pos);
            return m_selected_target_idx;
        }
    }
    m_selected_target_idx = 0;
    return m_selected_target_idx;
}

lldb::TargetSP
TargetList::GetSelectedTarget ()
{
    Mutex::Locker locker (m_target_list_mutex);
    if (m_selected_target_idx >= m_target_list.size())
        m_selected_target_idx = 0;
    return GetTargetAtIndex (m_selected_target_idx);
}
