//===-- Platform.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Platform_h_
#define liblldb_Platform_h_

// C Includes
// C++ Includes
#include <map>
#include <string>
#include <vector>

// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-public.h"
#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/ConstString.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Host/Mutex.h"

// TODO pull NativeDelegate class out of NativeProcessProtocol so we
// can just forward ref the NativeDelegate rather than include it here.
#include "../../../source/Host/common/NativeProcessProtocol.h"

namespace lldb_private {

    //----------------------------------------------------------------------
    /// @class Platform Platform.h "lldb/Target/Platform.h"
    /// @brief A plug-in interface definition class for debug platform that
    /// includes many platform abilities such as:
    ///     @li getting platform information such as supported architectures,
    ///         supported binary file formats and more
    ///     @li launching new processes
    ///     @li attaching to existing processes
    ///     @li download/upload files
    ///     @li execute shell commands
    ///     @li listing and getting info for existing processes
    ///     @li attaching and possibly debugging the platform's kernel
    //----------------------------------------------------------------------
    class Platform :
        public PluginInterface
    {
    public:

        //------------------------------------------------------------------
        /// Get the native host platform plug-in. 
        ///
        /// There should only be one of these for each host that LLDB runs
        /// upon that should be statically compiled in and registered using
        /// preprocessor macros or other similar build mechanisms in a 
        /// PlatformSubclass::Initialize() function.
        ///
        /// This platform will be used as the default platform when launching
        /// or attaching to processes unless another platform is specified.
        //------------------------------------------------------------------
        static lldb::PlatformSP
        GetHostPlatform ();

        static lldb::PlatformSP
        GetPlatformForArchitecture (const ArchSpec &arch,
                                    ArchSpec *platform_arch_ptr);

        static const char *
        GetHostPlatformName ();

        static void
        SetHostPlatform (const lldb::PlatformSP &platform_sp);

        // Find an existing platform plug-in by name
        static lldb::PlatformSP
        Find (const ConstString &name);

        static lldb::PlatformSP
        Create (const ConstString &name, Error &error);

        static lldb::PlatformSP
        Create (const ArchSpec &arch, ArchSpec *platform_arch_ptr, Error &error);
        
        static uint32_t
        GetNumConnectedRemotePlatforms ();
        
        static lldb::PlatformSP
        GetConnectedRemotePlatformAtIndex (uint32_t idx);

        //------------------------------------------------------------------
        /// Default Constructor
        //------------------------------------------------------------------
        Platform (bool is_host_platform);

        //------------------------------------------------------------------
        /// Destructor.
        ///
        /// The destructor is virtual since this class is designed to be
        /// inherited from by the plug-in instance.
        //------------------------------------------------------------------
        virtual
        ~Platform();

        //------------------------------------------------------------------
        /// Find a platform plugin for a given process.
        ///
        /// Scans the installed Platform plug-ins and tries to find
        /// an instance that can be used for \a process
        ///
        /// @param[in] process
        ///     The process for which to try and locate a platform
        ///     plug-in instance.
        ///
        /// @param[in] plugin_name
        ///     An optional name of a specific platform plug-in that
        ///     should be used. If NULL, pick the best plug-in.
        //------------------------------------------------------------------
//        static lldb::PlatformSP
//        FindPlugin (Process *process, const ConstString &plugin_name);

        //------------------------------------------------------------------
        /// Set the target's executable based off of the existing 
        /// architecture information in \a target given a path to an 
        /// executable \a exe_file.
        ///
        /// Each platform knows the architectures that it supports and can
        /// select the correct architecture slice within \a exe_file by 
        /// inspecting the architecture in \a target. If the target had an
        /// architecture specified, then in can try and obey that request
        /// and optionally fail if the architecture doesn't match up.
        /// If no architecture is specified, the platform should select the
        /// default architecture from \a exe_file. Any application bundles
        /// or executable wrappers can also be inspected for the actual
        /// application binary within the bundle that should be used.
        ///
        /// @return
        ///     Returns \b true if this Platform plug-in was able to find
        ///     a suitable executable, \b false otherwise.
        //------------------------------------------------------------------
        virtual Error
        ResolveExecutable (const ModuleSpec &module_spec,
                           lldb::ModuleSP &module_sp,
                           const FileSpecList *module_search_paths_ptr);

        
        //------------------------------------------------------------------
        /// Find a symbol file given a symbol file module specification.
        ///
        /// Each platform might have tricks to find symbol files for an
        /// executable given information in a symbol file ModuleSpec. Some
        /// platforms might also support symbol files that are bundles and
        /// know how to extract the right symbol file given a bundle.
        ///
        /// @param[in] target
        ///     The target in which we are trying to resolve the symbol file.
        ///     The target has a list of modules that we might be able to
        ///     use in order to help find the right symbol file. If the
        ///     "m_file" or "m_platform_file" entries in the \a sym_spec
        ///     are filled in, then we might be able to locate a module in
        ///     the target, extract its UUID and locate a symbol file.
        ///     If just the "m_uuid" is specified, then we might be able
        ///     to find the module in the target that matches that UUID
        ///     and pair the symbol file along with it. If just "m_symbol_file"
        ///     is specified, we can use a variety of tricks to locate the
        ///     symbols in an SDK, PDK, or other development kit location.
        ///
        /// @param[in] sym_spec
        ///     A module spec that describes some information about the
        ///     symbol file we are trying to resolve. The ModuleSpec might
        ///     contain the following:
        ///     m_file - A full or partial path to an executable from the
        ///              target (might be empty).
        ///     m_platform_file - Another executable hint that contains
        ///                       the path to the file as known on the
        ///                       local/remote platform.
        ///     m_symbol_file - A full or partial path to a symbol file
        ///                     or symbol bundle that should be used when
        ///                     trying to resolve the symbol file.
        ///     m_arch - The architecture we are looking for when resolving
        ///              the symbol file.
        ///     m_uuid - The UUID of the executable and symbol file. This
        ///              can often be used to match up an executable with
        ///              a symbol file, or resolve an symbol file in a
        ///              symbol file bundle.
        ///
        /// @param[out] sym_file
        ///     The resolved symbol file spec if the returned error
        ///     indicates success.
        ///
        /// @return
        ///     Returns an error that describes success or failure.
        //------------------------------------------------------------------
        virtual Error
        ResolveSymbolFile (Target &target,
                           const ModuleSpec &sym_spec,
                           FileSpec &sym_file);

        //------------------------------------------------------------------
        /// Resolves the FileSpec to a (possibly) remote path. Remote
        /// platforms must override this to resolve to a path on the remote
        /// side.
        //------------------------------------------------------------------
        virtual bool
        ResolveRemotePath (const FileSpec &platform_path,
                           FileSpec &resolved_platform_path);

        bool
        GetOSVersion (uint32_t &major, 
                      uint32_t &minor, 
                      uint32_t &update);
           
        bool
        SetOSVersion (uint32_t major, 
                      uint32_t minor, 
                      uint32_t update);

        bool
        GetOSBuildString (std::string &s);
        
        bool
        GetOSKernelDescription (std::string &s);

        // Returns the name of the platform
        ConstString
        GetName ();

        virtual const char *
        GetHostname ();

        virtual const char *
        GetDescription () = 0;

        //------------------------------------------------------------------
        /// Report the current status for this platform. 
        ///
        /// The returned string usually involves returning the OS version
        /// (if available), and any SDK directory that might be being used
        /// for local file caching, and if connected a quick blurb about
        /// what this platform is connected to.
        //------------------------------------------------------------------        
        virtual void
        GetStatus (Stream &strm);

        //------------------------------------------------------------------
        // Subclasses must be able to fetch the current OS version
        //
        // Remote classes must be connected for this to succeed. Local 
        // subclasses don't need to override this function as it will just
        // call the HostInfo::GetOSVersion().
        //------------------------------------------------------------------
        virtual bool
        GetRemoteOSVersion ()
        {
            return false;
        }

        virtual bool
        GetRemoteOSBuildString (std::string &s)
        {
            s.clear();
            return false;
        }
        
        virtual bool
        GetRemoteOSKernelDescription (std::string &s)
        {
            s.clear();
            return false;
        }

        // Remote Platform subclasses need to override this function
        virtual ArchSpec
        GetRemoteSystemArchitecture ()
        {
            return ArchSpec(); // Return an invalid architecture
        }
        
        virtual ConstString
        GetRemoteWorkingDirectory()
        {
            return m_working_dir;
        }
        
        virtual bool
        SetRemoteWorkingDirectory(const ConstString &path);

        virtual const char *
        GetUserName (uint32_t uid);

        virtual const char *
        GetGroupName (uint32_t gid);

        //------------------------------------------------------------------
        /// Locate a file for a platform.
        ///
        /// The default implementation of this function will return the same
        /// file patch in \a local_file as was in \a platform_file.
        ///
        /// @param[in] platform_file
        ///     The platform file path to locate and cache locally.
        ///
        /// @param[in] uuid_ptr
        ///     If we know the exact UUID of the file we are looking for, it
        ///     can be specified. If it is not specified, we might now know
        ///     the exact file. The UUID is usually some sort of MD5 checksum
        ///     for the file and is sometimes known by dynamic linkers/loaders.
        ///     If the UUID is known, it is best to supply it to platform
        ///     file queries to ensure we are finding the correct file, not
        ///     just a file at the correct path.
        ///
        /// @param[out] local_file
        ///     A locally cached version of the platform file. For platforms
        ///     that describe the current host computer, this will just be
        ///     the same file. For remote platforms, this file might come from
        ///     and SDK directory, or might need to be sync'ed over to the
        ///     current machine for efficient debugging access.
        ///
        /// @return
        ///     An error object.
        //------------------------------------------------------------------
        virtual Error
        GetFileWithUUID (const FileSpec &platform_file, 
                         const UUID *uuid_ptr,
                         FileSpec &local_file);

        //----------------------------------------------------------------------
        // Locate the scripting resource given a module specification.
        //
        // Locating the file should happen only on the local computer or using
        // the current computers global settings.
        //----------------------------------------------------------------------
        virtual FileSpecList
        LocateExecutableScriptingResources (Target *target,
                                            Module &module,
                                            Stream* feedback_stream);
        
        virtual Error
        GetSharedModule (const ModuleSpec &module_spec, 
                         lldb::ModuleSP &module_sp,
                         const FileSpecList *module_search_paths_ptr,
                         lldb::ModuleSP *old_module_sp_ptr,
                         bool *did_create_ptr);

        virtual Error
        ConnectRemote (Args& args);

        virtual Error
        DisconnectRemote ();

        //------------------------------------------------------------------
        /// Get the platform's supported architectures in the order in which
        /// they should be searched.
        ///
        /// @param[in] idx
        ///     A zero based architecture index
        ///
        /// @param[out] arch
        ///     A copy of the architecture at index if the return value is
        ///     \b true.
        ///
        /// @return
        ///     \b true if \a arch was filled in and is valid, \b false 
        ///     otherwise.
        //------------------------------------------------------------------
        virtual bool
        GetSupportedArchitectureAtIndex (uint32_t idx, ArchSpec &arch) = 0;

        virtual size_t
        GetSoftwareBreakpointTrapOpcode (Target &target,
                                         BreakpointSite *bp_site) = 0;

        //------------------------------------------------------------------
        /// Launch a new process on a platform, not necessarily for 
        /// debugging, it could be just for running the process.
        //------------------------------------------------------------------
        virtual Error
        LaunchProcess (ProcessLaunchInfo &launch_info);

        //------------------------------------------------------------------
        /// Lets a platform answer if it is compatible with a given
        /// architecture and the target triple contained within.
        //------------------------------------------------------------------
        virtual bool
        IsCompatibleArchitecture (const ArchSpec &arch,
                                  bool exact_arch_match,
                                  ArchSpec *compatible_arch_ptr);

        //------------------------------------------------------------------
        /// Not all platforms will support debugging a process by spawning
        /// somehow halted for a debugger (specified using the 
        /// "eLaunchFlagDebug" launch flag) and then attaching. If your 
        /// platform doesn't support this, override this function and return
        /// false.
        //------------------------------------------------------------------
        virtual bool
        CanDebugProcess ()
        {
            return true; 
        }

        //------------------------------------------------------------------
        /// Subclasses do not need to implement this function as it uses
        /// the Platform::LaunchProcess() followed by Platform::Attach ().
        /// Remote platforms will want to subclass this function in order
        /// to be able to intercept STDIO and possibly launch a separate
        /// process that will debug the debuggee.
        //------------------------------------------------------------------
        virtual lldb::ProcessSP
        DebugProcess (ProcessLaunchInfo &launch_info,
                      Debugger &debugger,
                      Target *target,       // Can be NULL, if NULL create a new target, else use existing one
                      Error &error);

        //------------------------------------------------------------------
        /// Attach to an existing process using a process ID.
        ///
        /// Each platform subclass needs to implement this function and 
        /// attempt to attach to the process with the process ID of \a pid.
        /// The platform subclass should return an appropriate ProcessSP 
        /// subclass that is attached to the process, or an empty shared 
        /// pointer with an appropriate error.
        ///
        /// @param[in] pid
        ///     The process ID that we should attempt to attach to.
        ///
        /// @return
        ///     An appropriate ProcessSP containing a valid shared pointer
        ///     to the default Process subclass for the platform that is 
        ///     attached to the process, or an empty shared pointer with an
        ///     appropriate error fill into the \a error object.
        //------------------------------------------------------------------
        virtual lldb::ProcessSP
        Attach (ProcessAttachInfo &attach_info,
                Debugger &debugger,
                Target *target,       // Can be NULL, if NULL create a new target, else use existing one
                Error &error) = 0;

        //------------------------------------------------------------------
        /// Attach to an existing process by process name.
        ///
        /// This function is not meant to be overridden by Process
        /// subclasses. It will first call
        /// Process::WillAttach (const char *) and if that returns \b
        /// true, Process::DoAttach (const char *) will be called to
        /// actually do the attach. If DoAttach returns \b true, then
        /// Process::DidAttach() will be called.
        ///
        /// @param[in] process_name
        ///     A process name to match against the current process list.
        ///
        /// @return
        ///     Returns \a pid if attaching was successful, or
        ///     LLDB_INVALID_PROCESS_ID if attaching fails.
        //------------------------------------------------------------------
//        virtual lldb::ProcessSP
//        Attach (const char *process_name, 
//                bool wait_for_launch, 
//                Error &error) = 0;
        
        //------------------------------------------------------------------
        // The base class Platform will take care of the host platform.
        // Subclasses will need to fill in the remote case.
        //------------------------------------------------------------------
        virtual uint32_t
        FindProcesses (const ProcessInstanceInfoMatch &match_info,
                       ProcessInstanceInfoList &proc_infos);

        virtual bool
        GetProcessInfo (lldb::pid_t pid, ProcessInstanceInfo &proc_info);
        
        //------------------------------------------------------------------
        // Set a breakpoint on all functions that can end up creating a thread
        // for this platform. This is needed when running expressions and
        // also for process control.
        //------------------------------------------------------------------
        virtual lldb::BreakpointSP
        SetThreadCreationBreakpoint (Target &target);
        
        //------------------------------------------------------------------
        // Given a target, find the local SDK directory if one exists on the
        // current host.
        //------------------------------------------------------------------
        virtual lldb_private::ConstString
        GetSDKDirectory (lldb_private::Target &target)
        {
            return lldb_private::ConstString();
        }

        const std::string &
        GetRemoteURL () const
        {
            return m_remote_url;
        }

        bool
        IsHost () const
        {
            return m_is_host;    // Is this the default host platform?
        }

        bool
        IsRemote () const
        {
            return !m_is_host;
        }
        
        virtual bool
        IsConnected () const
        {
            // Remote subclasses should override this function
            return IsHost();
        }
        
        const ArchSpec &
        GetSystemArchitecture();

        void
        SetSystemArchitecture (const ArchSpec &arch)
        {
            m_system_arch = arch;
            if (IsHost())
                m_os_version_set_while_connected = m_system_arch.IsValid();
        }

        // Used for column widths
        size_t
        GetMaxUserIDNameLength() const
        {
            return m_max_uid_name_len;
        }
        // Used for column widths
        size_t
        GetMaxGroupIDNameLength() const
        {
            return m_max_gid_name_len;
        }
        
        const ConstString &
        GetSDKRootDirectory () const
        {
            return m_sdk_sysroot;
        }

        void
        SetSDKRootDirectory (const ConstString &dir)
        {
            m_sdk_sysroot = dir;
        }

        const ConstString &
        GetSDKBuild () const
        {
            return m_sdk_build;
        }
        
        void
        SetSDKBuild (const ConstString &sdk_build)
        {
            m_sdk_build = sdk_build;
        }    

        ConstString
        GetWorkingDirectory ();
        
        bool
        SetWorkingDirectory (const ConstString &path);
        
        // There may be modules that we don't want to find by default for operations like "setting breakpoint by name".
        // The platform will return "true" from this call if the passed in module happens to be one of these.
        
        virtual bool
        ModuleIsExcludedForNonModuleSpecificSearches (Target &target, const lldb::ModuleSP &module_sp)
        {
            return false;
        }
        
        virtual Error
        MakeDirectory (const char *path, uint32_t permissions);
        
        virtual Error
        GetFilePermissions (const char *path, uint32_t &file_permissions);

        virtual Error
        SetFilePermissions (const char *path, uint32_t file_permissions);

        virtual lldb::user_id_t
        OpenFile (const FileSpec& file_spec,
                  uint32_t flags,
                  uint32_t mode,
                  Error &error)
        {
            return UINT64_MAX;
        }
        
        virtual bool
        CloseFile (lldb::user_id_t fd,
                   Error &error)
        {
            return false;
        }
        
        virtual lldb::user_id_t
        GetFileSize (const FileSpec& file_spec)
        {
            return UINT64_MAX;
        }

        virtual uint64_t
        ReadFile (lldb::user_id_t fd,
                  uint64_t offset,
                  void *dst,
                  uint64_t dst_len,
                  Error &error)
        {
            error.SetErrorStringWithFormat ("Platform::ReadFile() is not supported in the %s platform", GetName().GetCString());
            return -1;
        }
        
        virtual uint64_t
        WriteFile (lldb::user_id_t fd,
                   uint64_t offset,
                   const void* src,
                   uint64_t src_len,
                   Error &error)
        {
            error.SetErrorStringWithFormat ("Platform::ReadFile() is not supported in the %s platform", GetName().GetCString());
            return -1;
        }
        
        virtual Error
        GetFile (const FileSpec& source,
                 const FileSpec& destination);
        
        virtual Error
        PutFile (const FileSpec& source,
                 const FileSpec& destination,
                 uint32_t uid = UINT32_MAX,
                 uint32_t gid = UINT32_MAX);

        virtual Error
        CreateSymlink (const char *src, // The name of the link is in src
                       const char *dst);// The symlink points to dst

        //----------------------------------------------------------------------
        /// Install a file or directory to the remote system.
        ///
        /// Install is similar to Platform::PutFile(), but it differs in that if
        /// an application/framework/shared library is installed on a remote
        /// platform and the remote platform requires something to be done to
        /// register the application/framework/shared library, then this extra
        /// registration can be done.
        ///
        /// @param[in] src
        ///     The source file/directory to install on the remote system.
        ///
        /// @param[in] dst
        ///     The destination file/directory where \a src will be installed.
        ///     If \a dst has no filename specified, then its filename will
        ///     be set from \a src. It \a dst has no directory specified, it
        ///     will use the platform working directory. If \a dst has a
        ///     directory specified, but the directory path is relative, the
        ///     platform working directory will be prepended to the relative
        ///     directory.
        ///
        /// @return
        ///     An error object that describes anything that went wrong.
        //----------------------------------------------------------------------
        virtual Error
        Install (const FileSpec& src, const FileSpec& dst);

        virtual size_t
        GetEnvironment (StringList &environment);
        
        virtual bool
        GetFileExists (const lldb_private::FileSpec& file_spec);
        
        virtual Error
        Unlink (const char *path);

        virtual bool
        GetSupportsRSync ()
        {
            return m_supports_rsync;
        }
        
        virtual void
        SetSupportsRSync(bool flag)
        {
            m_supports_rsync = flag;
        }
        
        virtual const char*
        GetRSyncOpts ()
        {
            return m_rsync_opts.c_str();
        }
        
        virtual void
        SetRSyncOpts (const char* opts)
        {
            m_rsync_opts.assign(opts);
        }
        
        virtual const char*
        GetRSyncPrefix ()
        {
            return m_rsync_prefix.c_str();
        }
        
        virtual void
        SetRSyncPrefix (const char* prefix)
        {
            m_rsync_prefix.assign(prefix);
        }
        
        virtual bool
        GetSupportsSSH ()
        {
            return m_supports_ssh;
        }
        
        virtual void
        SetSupportsSSH(bool flag)
        {
            m_supports_ssh = flag;
        }
        
        virtual const char*
        GetSSHOpts ()
        {
            return m_ssh_opts.c_str();
        }
        
        virtual void
        SetSSHOpts (const char* opts)
        {
            m_ssh_opts.assign(opts);
        }
        
        virtual bool
        GetIgnoresRemoteHostname ()
        {
            return m_ignores_remote_hostname;
        }
        
        virtual void
        SetIgnoresRemoteHostname(bool flag)
        {
            m_ignores_remote_hostname = flag;
        }
        
        virtual lldb_private::OptionGroupOptions *
        GetConnectionOptions (CommandInterpreter& interpreter)
        {
            return NULL;
        }
        
        virtual lldb_private::Error
        RunShellCommand (const char *command,           // Shouldn't be NULL
                         const char *working_dir,       // Pass NULL to use the current working directory
                         int *status_ptr,               // Pass NULL if you don't want the process exit status
                         int *signo_ptr,                // Pass NULL if you don't want the signal that caused the process to exit
                         std::string *command_output,   // Pass NULL if you don't want the command output
                         uint32_t timeout_sec);         // Timeout in seconds to wait for shell program to finish
        
        virtual void
        SetLocalCacheDirectory (const char* local);
        
        virtual const char*
        GetLocalCacheDirectory ();
        
        virtual std::string
        GetPlatformSpecificConnectionInformation()
        {
            return "";
        }
        
        virtual bool
        CalculateMD5 (const FileSpec& file_spec,
                      uint64_t &low,
                      uint64_t &high);
        
        virtual int32_t
        GetResumeCountForLaunchInfo (ProcessLaunchInfo &launch_info)
        {
            return 1;
        }

        //------------------------------------------------------------------
        /// Locate a queue name given a thread's qaddr
        ///
        /// On a system using libdispatch ("Grand Central Dispatch") style
        /// queues, a thread may be associated with a GCD queue or not,
        /// and a queue may be associated with multiple threads.
        /// The process/thread must provide a way to find the "dispatch_qaddr" 
        /// for each thread, and from that dispatch_qaddr this Platform method
        /// will locate the queue name and provide that.
        ///
        /// @param[in] process
        ///     A process is required for reading memory.
        ///
        /// @param[in] dispatch_qaddr
        ///     The dispatch_qaddr for this thread.
        ///
        /// @return
        ///     The name of the queue, if there is one.  An empty string
        ///     means that this thread is not associated with a dispatch 
        ///     queue.
        //------------------------------------------------------------------
        virtual std::string
        GetQueueNameForThreadQAddress (Process *process, lldb::addr_t dispatch_qaddr)
        {
            return "";
        }

        //------------------------------------------------------------------
        /// Locate a queue ID given a thread's qaddr
        ///
        /// On a system using libdispatch ("Grand Central Dispatch") style
        /// queues, a thread may be associated with a GCD queue or not,
        /// and a queue may be associated with multiple threads.
        /// The process/thread must provide a way to find the "dispatch_qaddr" 
        /// for each thread, and from that dispatch_qaddr this Platform method
        /// will locate the queue ID and provide that.
        ///
        /// @param[in] process
        ///     A process is required for reading memory.
        ///
        /// @param[in] dispatch_qaddr
        ///     The dispatch_qaddr for this thread.
        ///
        /// @return
        ///     The queue_id for this thread, if this thread is associated
        ///     with a dispatch queue.  Else LLDB_INVALID_QUEUE_ID is returned.
        //------------------------------------------------------------------
        virtual lldb::queue_id_t
        GetQueueIDForThreadQAddress (Process *process, lldb::addr_t dispatch_qaddr)
        {
            return LLDB_INVALID_QUEUE_ID;
        }

        //------------------------------------------------------------------
        /// Provide a list of trap handler function names for this platform
        ///
        /// The unwinder needs to treat trap handlers specially -- the stack
        /// frame may not be aligned correctly for a trap handler (the kernel
        /// often won't perturb the stack pointer, or won't re-align it properly,
        /// in the process of calling the handler) and the frame above the handler
        /// needs to be treated by the unwinder's "frame 0" rules instead of its
        /// "middle of the stack frame" rules.
        /// 
        /// In a user process debugging scenario, the list of trap handlers is
        /// typically just "_sigtramp".
        ///
        /// The Platform base class provides the m_trap_handlers ivar but it does
        /// not populate it.  Subclasses should add the names of the asynchronous
        /// signal handler routines as needed.  For most Unix platforms, add _sigtramp.
        ///
        /// @return
        ///     A list of symbol names.  The list may be empty.
        //------------------------------------------------------------------
        virtual const std::vector<ConstString> &
        GetTrapHandlerSymbolNames ();

        //------------------------------------------------------------------
        /// Launch a process for debugging.
        ///
        /// This differs from Launch in that it returns a NativeProcessProtocol.
        /// Currently used by lldb-gdbserver.
        ///
        /// @param[in] launch_info
        ///     Information required to launch the process.
        ///
        /// @param[in] native_delegate
        ///     The delegate that will receive messages regarding the
        ///     inferior.  Must outlive the NativeProcessProtocol
        ///     instance.
        ///
        /// @param[out] process_sp
        ///     On successful return from the method, this parameter
        ///     contains the shared pointer to the
        ///     NativeProcessProtocol that can be used to manipulate
        ///     the native process.
        ///
        /// @return
        ///     An error object indicating if the operation succeeded,
        ///     and if not, what error occurred.
        //------------------------------------------------------------------
        virtual Error
        LaunchNativeProcess (
            ProcessLaunchInfo &launch_info,
            lldb_private::NativeProcessProtocol::NativeDelegate &native_delegate,
            NativeProcessProtocolSP &process_sp);

        //------------------------------------------------------------------
        /// Attach to an existing process on the given platform.
        ///
        /// This method differs from Attach() in that it returns a
        /// NativeProcessProtocol.  Currently this is used by lldb-gdbserver.
        ///
        /// @param[in] pid
        ///     pid of the process locatable by the platform.
        ///
        /// @param[in] native_delegate
        ///     The delegate that will receive messages regarding the
        ///     inferior.  Must outlive the NativeProcessProtocol
        ///     instance.
        ///
        /// @param[out] process_sp
        ///     On successful return from the method, this parameter
        ///     contains the shared pointer to the
        ///     NativeProcessProtocol that can be used to manipulate
        ///     the native process.
        ///
        /// @return
        ///     An error object indicating if the operation succeeded,
        ///     and if not, what error occurred.
        //------------------------------------------------------------------
        virtual Error
        AttachNativeProcess (lldb::pid_t pid,
                             lldb_private::NativeProcessProtocol::NativeDelegate &native_delegate,
                             NativeProcessProtocolSP &process_sp);

    protected:
        bool m_is_host;
        // Set to true when we are able to actually set the OS version while 
        // being connected. For remote platforms, we might set the version ahead
        // of time before we actually connect and this version might change when
        // we actually connect to a remote platform. For the host platform this
        // will be set to the once we call HostInfo::GetOSVersion().
        bool m_os_version_set_while_connected;
        bool m_system_arch_set_while_connected;
        ConstString m_sdk_sysroot; // the root location of where the SDK files are all located
        ConstString m_sdk_build;
        ConstString m_working_dir; // The working directory which is used when installing modules that have no install path set
        std::string m_remote_url;
        std::string m_name;
        uint32_t m_major_os_version;
        uint32_t m_minor_os_version;
        uint32_t m_update_os_version;
        ArchSpec m_system_arch; // The architecture of the kernel or the remote platform
        typedef std::map<uint32_t, ConstString> IDToNameMap;
        Mutex m_uid_map_mutex;
        Mutex m_gid_map_mutex;
        IDToNameMap m_uid_map;
        IDToNameMap m_gid_map;
        size_t m_max_uid_name_len;
        size_t m_max_gid_name_len;
        bool m_supports_rsync;
        std::string m_rsync_opts;
        std::string m_rsync_prefix;
        bool m_supports_ssh;
        std::string m_ssh_opts;
        bool m_ignores_remote_hostname;
        std::string m_local_cache_directory;
        std::vector<ConstString> m_trap_handlers;
        bool m_calculated_trap_handlers;
        Mutex m_trap_handler_mutex;

        //------------------------------------------------------------------
        /// Ask the Platform subclass to fill in the list of trap handler names
        ///
        /// For most Unix user process environments, this will be a single
        /// function name, _sigtramp.  More specialized environments may have
        /// additional handler names.  The unwinder code needs to know when a
        /// trap handler is on the stack because the unwind rules for the frame
        /// that caused the trap are different.
        ///
        /// The base class Platform ivar m_trap_handlers should be updated by
        /// the Platform subclass when this method is called.  If there are no
        /// predefined trap handlers, this method may be a no-op.
        //------------------------------------------------------------------
        virtual void
        CalculateTrapHandlerSymbolNames () = 0;

        const char *
        GetCachedUserName (uint32_t uid)
        {
            Mutex::Locker locker (m_uid_map_mutex);
            IDToNameMap::iterator pos = m_uid_map.find (uid);
            if (pos != m_uid_map.end())
            {
                // return the empty string if our string is NULL
                // so we can tell when things were in the negative
                // cached (didn't find a valid user name, don't keep
                // trying)
                return pos->second.AsCString("");
            }
            return NULL;
        }

        const char *
        SetCachedUserName (uint32_t uid, const char *name, size_t name_len)
        {
            Mutex::Locker locker (m_uid_map_mutex);
            ConstString const_name (name);
            m_uid_map[uid] = const_name;
            if (m_max_uid_name_len < name_len)
                m_max_uid_name_len = name_len;
            // Const strings lives forever in our const string pool, so we can return the const char *
            return const_name.GetCString(); 
        }

        void
        SetUserNameNotFound (uint32_t uid)
        {
            Mutex::Locker locker (m_uid_map_mutex);
            m_uid_map[uid] = ConstString();
        }
        

        void
        ClearCachedUserNames ()
        {
            Mutex::Locker locker (m_uid_map_mutex);
            m_uid_map.clear();
        }
    
        const char *
        GetCachedGroupName (uint32_t gid)
        {
            Mutex::Locker locker (m_gid_map_mutex);
            IDToNameMap::iterator pos = m_gid_map.find (gid);
            if (pos != m_gid_map.end())
            {
                // return the empty string if our string is NULL
                // so we can tell when things were in the negative
                // cached (didn't find a valid group name, don't keep
                // trying)
                return pos->second.AsCString("");
            }
            return NULL;
        }

        const char *
        SetCachedGroupName (uint32_t gid, const char *name, size_t name_len)
        {
            Mutex::Locker locker (m_gid_map_mutex);
            ConstString const_name (name);
            m_gid_map[gid] = const_name;
            if (m_max_gid_name_len < name_len)
                m_max_gid_name_len = name_len;
            // Const strings lives forever in our const string pool, so we can return the const char *
            return const_name.GetCString(); 
        }

        void
        SetGroupNameNotFound (uint32_t gid)
        {
            Mutex::Locker locker (m_gid_map_mutex);
            m_gid_map[gid] = ConstString();
        }

        void
        ClearCachedGroupNames ()
        {
            Mutex::Locker locker (m_gid_map_mutex);
            m_gid_map.clear();
        }

    private:
        DISALLOW_COPY_AND_ASSIGN (Platform);
    };

    
    class PlatformList
    {
    public:
        PlatformList() :
            m_mutex (Mutex::eMutexTypeRecursive),
            m_platforms (),
            m_selected_platform_sp()
        {
        }
        
        ~PlatformList()
        {
        }
        
        void
        Append (const lldb::PlatformSP &platform_sp, bool set_selected)
        {
            Mutex::Locker locker (m_mutex);
            m_platforms.push_back (platform_sp);
            if (set_selected)
                m_selected_platform_sp = m_platforms.back();
        }

        size_t
        GetSize()
        {
            Mutex::Locker locker (m_mutex);
            return m_platforms.size();
        }

        lldb::PlatformSP
        GetAtIndex (uint32_t idx)
        {
            lldb::PlatformSP platform_sp;
            {
                Mutex::Locker locker (m_mutex);
                if (idx < m_platforms.size())
                    platform_sp = m_platforms[idx];
            }
            return platform_sp;
        }

        //------------------------------------------------------------------
        /// Select the active platform.
        ///
        /// In order to debug remotely, other platform's can be remotely
        /// connected to and set as the selected platform for any subsequent
        /// debugging. This allows connection to remote targets and allows
        /// the ability to discover process info, launch and attach to remote
        /// processes.
        //------------------------------------------------------------------
        lldb::PlatformSP
        GetSelectedPlatform ()
        {
            Mutex::Locker locker (m_mutex);
            if (!m_selected_platform_sp && !m_platforms.empty())
                m_selected_platform_sp = m_platforms.front();
            
            return m_selected_platform_sp;
        }

        void
        SetSelectedPlatform (const lldb::PlatformSP &platform_sp)
        {
            if (platform_sp)
            {
                Mutex::Locker locker (m_mutex);
                const size_t num_platforms = m_platforms.size();
                for (size_t idx=0; idx<num_platforms; ++idx)
                {
                    if (m_platforms[idx].get() == platform_sp.get())
                    {
                        m_selected_platform_sp = m_platforms[idx];
                        return;
                    }
                }
                m_platforms.push_back (platform_sp);
                m_selected_platform_sp = m_platforms.back();
            }
        }

    protected:
        typedef std::vector<lldb::PlatformSP> collection;
        mutable Mutex m_mutex;
        collection m_platforms;
        lldb::PlatformSP m_selected_platform_sp;

    private:
        DISALLOW_COPY_AND_ASSIGN (PlatformList);
    };
    
    class OptionGroupPlatformRSync : public lldb_private::OptionGroup
    {
    public:
        OptionGroupPlatformRSync ();
        
        virtual
        ~OptionGroupPlatformRSync ();
        
        virtual lldb_private::Error
        SetOptionValue (CommandInterpreter &interpreter,
                        uint32_t option_idx,
                        const char *option_value);
        
        void
        OptionParsingStarting (CommandInterpreter &interpreter);
        
        const lldb_private::OptionDefinition*
        GetDefinitions ();
        
        virtual uint32_t
        GetNumDefinitions ();
        
        // Options table: Required for subclasses of Options.
        
        static lldb_private::OptionDefinition g_option_table[];
        
        // Instance variables to hold the values for command options.
        
        bool m_rsync;
        std::string m_rsync_opts;
        std::string m_rsync_prefix;
        bool m_ignores_remote_hostname;
    private:
        DISALLOW_COPY_AND_ASSIGN(OptionGroupPlatformRSync);
    };
    
    class OptionGroupPlatformSSH : public lldb_private::OptionGroup
    {
    public:
        OptionGroupPlatformSSH ();
        
        virtual
        ~OptionGroupPlatformSSH ();
        
        virtual lldb_private::Error
        SetOptionValue (CommandInterpreter &interpreter,
                        uint32_t option_idx,
                        const char *option_value);
        
        void
        OptionParsingStarting (CommandInterpreter &interpreter);
        
        virtual uint32_t
        GetNumDefinitions ();
        
        const lldb_private::OptionDefinition*
        GetDefinitions ();
        
        // Options table: Required for subclasses of Options.
        
        static lldb_private::OptionDefinition g_option_table[];
        
        // Instance variables to hold the values for command options.
        
        bool m_ssh;
        std::string m_ssh_opts;

    private:

        DISALLOW_COPY_AND_ASSIGN(OptionGroupPlatformSSH);
    };
    
    class OptionGroupPlatformCaching : public lldb_private::OptionGroup
    {
    public:
        OptionGroupPlatformCaching ();
        
        virtual
        ~OptionGroupPlatformCaching ();
        
        virtual lldb_private::Error
        SetOptionValue (CommandInterpreter &interpreter,
                        uint32_t option_idx,
                        const char *option_value);
        
        void
        OptionParsingStarting (CommandInterpreter &interpreter);
        
        virtual uint32_t
        GetNumDefinitions ();
        
        const lldb_private::OptionDefinition*
        GetDefinitions ();
        
        // Options table: Required for subclasses of Options.
        
        static lldb_private::OptionDefinition g_option_table[];
        
        // Instance variables to hold the values for command options.
        
        std::string m_cache_dir;
    private:
        DISALLOW_COPY_AND_ASSIGN(OptionGroupPlatformCaching);
    };
    
} // namespace lldb_private

#endif  // liblldb_Platform_h_
