//===-- GDBRemoteCommunication.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "GDBRemoteCommunication.h"

// C Includes
#include <limits.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>

// C++ Includes
// Other libraries and framework includes
#include "lldb/Core/Log.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/StreamString.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSpec.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/Socket.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/TimeValue.h"
#include "lldb/Target/Process.h"

// Project includes
#include "ProcessGDBRemoteLog.h"

#if defined(__APPLE__)
# define DEBUGSERVER_BASENAME    "debugserver"
#else
# define DEBUGSERVER_BASENAME    "lldb-gdbserver"
#endif

using namespace lldb;
using namespace lldb_private;

namespace
{

Error
ReadPortFromPipe (const char *const named_pipe_path, uint16_t& port, const int timeout_secs)
{
    File name_pipe_file;
    auto error = name_pipe_file.Open (named_pipe_path, File::eOpenOptionRead | File::eOpenOptionNonBlocking);
    if (error.Fail ())
        return error;

    struct timeval tv = {timeout_secs, 0};
    const auto pipe_handle = name_pipe_file.GetWaitableHandle ();
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(pipe_handle, &rfds);

    const auto retval = ::select (pipe_handle + 1, &rfds, NULL, NULL, &tv);
    if (retval == -1)
    {
        error.SetErrorToErrno ();
        return error;
    }
    if (retval == 0)
    {
        error.SetErrorString ("timeout exceeded");
        return error;
    }

    char port_cstr[256];
    port_cstr[0] = '\0';
    size_t num_bytes = sizeof(port_cstr);
    error = name_pipe_file.Read (port_cstr, num_bytes);

    if (error.Success ())
    {
        assert (num_bytes > 0 && port_cstr[num_bytes-1] == '\0');
        port = Args::StringToUInt32 (port_cstr, 0);
    }
    return error;
}

}

GDBRemoteCommunication::History::History (uint32_t size) :
    m_packets(),
    m_curr_idx (0),
    m_total_packet_count (0),
    m_dumped_to_log (false)
{
    m_packets.resize(size);
}

GDBRemoteCommunication::History::~History ()
{
}

void
GDBRemoteCommunication::History::AddPacket (char packet_char,
                                            PacketType type,
                                            uint32_t bytes_transmitted)
{
    const size_t size = m_packets.size();
    if (size > 0)
    {
        const uint32_t idx = GetNextIndex();
        m_packets[idx].packet.assign (1, packet_char);
        m_packets[idx].type = type;
        m_packets[idx].bytes_transmitted = bytes_transmitted;
        m_packets[idx].packet_idx = m_total_packet_count;
        m_packets[idx].tid = Host::GetCurrentThreadID();
    }
}

void
GDBRemoteCommunication::History::AddPacket (const std::string &src,
                                            uint32_t src_len,
                                            PacketType type,
                                            uint32_t bytes_transmitted)
{
    const size_t size = m_packets.size();
    if (size > 0)
    {
        const uint32_t idx = GetNextIndex();
        m_packets[idx].packet.assign (src, 0, src_len);
        m_packets[idx].type = type;
        m_packets[idx].bytes_transmitted = bytes_transmitted;
        m_packets[idx].packet_idx = m_total_packet_count;
        m_packets[idx].tid = Host::GetCurrentThreadID();
    }
}

void
GDBRemoteCommunication::History::Dump (lldb_private::Stream &strm) const
{
    const uint32_t size = GetNumPacketsInHistory ();
    const uint32_t first_idx = GetFirstSavedPacketIndex ();
    const uint32_t stop_idx = m_curr_idx + size;
    for (uint32_t i = first_idx;  i < stop_idx; ++i)
    {
        const uint32_t idx = NormalizeIndex (i);
        const Entry &entry = m_packets[idx];
        if (entry.type == ePacketTypeInvalid || entry.packet.empty())
            break;
        strm.Printf ("history[%u] tid=0x%4.4" PRIx64 " <%4u> %s packet: %s\n",
                     entry.packet_idx,
                     entry.tid,
                     entry.bytes_transmitted,
                     (entry.type == ePacketTypeSend) ? "send" : "read",
                     entry.packet.c_str());
    }
}

void
GDBRemoteCommunication::History::Dump (lldb_private::Log *log) const
{
    if (log && !m_dumped_to_log)
    {
        m_dumped_to_log = true;
        const uint32_t size = GetNumPacketsInHistory ();
        const uint32_t first_idx = GetFirstSavedPacketIndex ();
        const uint32_t stop_idx = m_curr_idx + size;
        for (uint32_t i = first_idx;  i < stop_idx; ++i)
        {
            const uint32_t idx = NormalizeIndex (i);
            const Entry &entry = m_packets[idx];
            if (entry.type == ePacketTypeInvalid || entry.packet.empty())
                break;
            log->Printf ("history[%u] tid=0x%4.4" PRIx64 " <%4u> %s packet: %s",
                         entry.packet_idx,
                         entry.tid,
                         entry.bytes_transmitted,
                         (entry.type == ePacketTypeSend) ? "send" : "read",
                         entry.packet.c_str());
        }
    }
}

//----------------------------------------------------------------------
// GDBRemoteCommunication constructor
//----------------------------------------------------------------------
GDBRemoteCommunication::GDBRemoteCommunication(const char *comm_name, 
                                               const char *listener_name, 
                                               bool is_platform) :
    Communication(comm_name),
#ifdef LLDB_CONFIGURATION_DEBUG
    m_packet_timeout (1000),
#else
    m_packet_timeout (1),
#endif
    m_sequence_mutex (Mutex::eMutexTypeRecursive),
    m_public_is_running (false),
    m_private_is_running (false),
    m_history (512),
    m_send_acks (true),
    m_is_platform (is_platform),
    m_listen_url ()
{
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
GDBRemoteCommunication::~GDBRemoteCommunication()
{
    if (IsConnected())
    {
        Disconnect();
    }
}

char
GDBRemoteCommunication::CalculcateChecksum (const char *payload, size_t payload_length)
{
    int checksum = 0;

    for (size_t i = 0; i < payload_length; ++i)
        checksum += payload[i];

    return checksum & 255;
}

size_t
GDBRemoteCommunication::SendAck ()
{
    Log *log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PACKETS));
    ConnectionStatus status = eConnectionStatusSuccess;
    char ch = '+';
    const size_t bytes_written = Write (&ch, 1, status, NULL);
    if (log)
        log->Printf ("<%4" PRIu64 "> send packet: %c", (uint64_t)bytes_written, ch);
    m_history.AddPacket (ch, History::ePacketTypeSend, bytes_written);
    return bytes_written;
}

size_t
GDBRemoteCommunication::SendNack ()
{
    Log *log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PACKETS));
    ConnectionStatus status = eConnectionStatusSuccess;
    char ch = '-';
    const size_t bytes_written = Write (&ch, 1, status, NULL);
    if (log)
        log->Printf("<%4" PRIu64 "> send packet: %c", (uint64_t)bytes_written, ch);
    m_history.AddPacket (ch, History::ePacketTypeSend, bytes_written);
    return bytes_written;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::SendPacket (const char *payload, size_t payload_length)
{
    Mutex::Locker locker(m_sequence_mutex);
    return SendPacketNoLock (payload, payload_length);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::SendPacketNoLock (const char *payload, size_t payload_length)
{
    if (IsConnected())
    {
        StreamString packet(0, 4, eByteOrderBig);

        packet.PutChar('$');
        packet.Write (payload, payload_length);
        packet.PutChar('#');
        packet.PutHex8(CalculcateChecksum (payload, payload_length));

        Log *log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PACKETS));
        ConnectionStatus status = eConnectionStatusSuccess;
        const char *packet_data = packet.GetData();
        const size_t packet_length = packet.GetSize();
        size_t bytes_written = Write (packet_data, packet_length, status, NULL);
        if (log)
        {
            size_t binary_start_offset = 0;
            if (strncmp(packet_data, "$vFile:pwrite:", strlen("$vFile:pwrite:")) == 0)
            {
                const char *first_comma = strchr(packet_data, ',');
                if (first_comma)
                {
                    const char *second_comma = strchr(first_comma + 1, ',');
                    if (second_comma)
                        binary_start_offset = second_comma - packet_data + 1;
                }
            }

            // If logging was just enabled and we have history, then dump out what
            // we have to the log so we get the historical context. The Dump() call that
            // logs all of the packet will set a boolean so that we don't dump this more
            // than once
            if (!m_history.DidDumpToLog ())
                m_history.Dump (log);

            if (binary_start_offset)
            {
                StreamString strm;
                // Print non binary data header
                strm.Printf("<%4" PRIu64 "> send packet: %.*s", (uint64_t)bytes_written, (int)binary_start_offset, packet_data);
                const uint8_t *p;
                // Print binary data exactly as sent
                for (p = (uint8_t*)packet_data + binary_start_offset; *p != '#'; ++p)
                    strm.Printf("\\x%2.2x", *p);
                // Print the checksum
                strm.Printf("%*s", (int)3, p);
                log->PutCString(strm.GetString().c_str());
            }
            else
                log->Printf("<%4" PRIu64 "> send packet: %.*s", (uint64_t)bytes_written, (int)packet_length, packet_data);
        }

        m_history.AddPacket (packet.GetString(), packet_length, History::ePacketTypeSend, bytes_written);


        if (bytes_written == packet_length)
        {
            if (GetSendAcks ())
                return GetAck ();
            else
                return PacketResult::Success;
        }
        else
        {
            if (log)
                log->Printf ("error: failed to send packet: %.*s", (int)packet_length, packet_data);
        }
    }
    return PacketResult::ErrorSendFailed;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::GetAck ()
{
    StringExtractorGDBRemote packet;
    PacketResult result = WaitForPacketWithTimeoutMicroSecondsNoLock (packet, GetPacketTimeoutInMicroSeconds ());
    if (result == PacketResult::Success)
    {
        if (packet.GetResponseType() == StringExtractorGDBRemote::ResponseType::eAck)
            return PacketResult::Success;
        else
            return PacketResult::ErrorSendAck;
    }
    return result;
}

bool
GDBRemoteCommunication::GetSequenceMutex (Mutex::Locker& locker, const char *failure_message)
{
    if (IsRunning())
        return locker.TryLock (m_sequence_mutex, failure_message);

    locker.Lock (m_sequence_mutex);
    return true;
}


bool
GDBRemoteCommunication::WaitForNotRunningPrivate (const TimeValue *timeout_ptr)
{
    return m_private_is_running.WaitForValueEqualTo (false, timeout_ptr, NULL);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::WaitForPacketWithTimeoutMicroSecondsNoLock (StringExtractorGDBRemote &packet, uint32_t timeout_usec)
{
    uint8_t buffer[8192];
    Error error;

    Log *log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PACKETS | GDBR_LOG_VERBOSE));

    // Check for a packet from our cache first without trying any reading...
    if (CheckForPacket (NULL, 0, packet))
        return PacketResult::Success;

    bool timed_out = false;
    bool disconnected = false;
    while (IsConnected() && !timed_out)
    {
        lldb::ConnectionStatus status = eConnectionStatusNoConnection;
        size_t bytes_read = Read (buffer, sizeof(buffer), timeout_usec, status, &error);
        
        if (log)
            log->Printf ("%s: Read (buffer, (sizeof(buffer), timeout_usec = 0x%x, status = %s, error = %s) => bytes_read = %" PRIu64,
                         __PRETTY_FUNCTION__,
                         timeout_usec, 
                         Communication::ConnectionStatusAsCString (status),
                         error.AsCString(), 
                         (uint64_t)bytes_read);

        if (bytes_read > 0)
        {
            if (CheckForPacket (buffer, bytes_read, packet))
                return PacketResult::Success;
        }
        else
        {
            switch (status)
            {
            case eConnectionStatusTimedOut:
            case eConnectionStatusInterrupted:
                timed_out = true;
                break;
            case eConnectionStatusSuccess:
                //printf ("status = success but error = %s\n", error.AsCString("<invalid>"));
                break;
                
            case eConnectionStatusEndOfFile:
            case eConnectionStatusNoConnection:
            case eConnectionStatusLostConnection:
            case eConnectionStatusError:
                disconnected = true;
                Disconnect();
                break;
            }
        }
    }
    packet.Clear ();
    if (disconnected)
        return PacketResult::ErrorDisconnected;
    if (timed_out)
        return PacketResult::ErrorReplyTimeout;
    else
        return PacketResult::ErrorReplyFailed;
}

bool
GDBRemoteCommunication::CheckForPacket (const uint8_t *src, size_t src_len, StringExtractorGDBRemote &packet)
{
    // Put the packet data into the buffer in a thread safe fashion
    Mutex::Locker locker(m_bytes_mutex);
    
    Log *log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PACKETS));

    if (src && src_len > 0)
    {
        if (log && log->GetVerbose())
        {
            StreamString s;
            log->Printf ("GDBRemoteCommunication::%s adding %u bytes: %.*s",
                         __FUNCTION__, 
                         (uint32_t)src_len, 
                         (uint32_t)src_len, 
                         src);
        }
        m_bytes.append ((const char *)src, src_len);
    }

    // Parse up the packets into gdb remote packets
    if (!m_bytes.empty())
    {
        // end_idx must be one past the last valid packet byte. Start
        // it off with an invalid value that is the same as the current
        // index.
        size_t content_start = 0;
        size_t content_length = 0;
        size_t total_length = 0;
        size_t checksum_idx = std::string::npos;

        switch (m_bytes[0])
        {
            case '+':       // Look for ack
            case '-':       // Look for cancel
            case '\x03':    // ^C to halt target
                content_length = total_length = 1;  // The command is one byte long...
                break;

            case '$':
                // Look for a standard gdb packet?
                {
                    size_t hash_pos = m_bytes.find('#');
                    if (hash_pos != std::string::npos)
                    {
                        if (hash_pos + 2 < m_bytes.size())
                        {
                            checksum_idx = hash_pos + 1;
                            // Skip the dollar sign
                            content_start = 1; 
                            // Don't include the # in the content or the $ in the content length
                            content_length = hash_pos - 1;  
                            
                            total_length = hash_pos + 3; // Skip the # and the two hex checksum bytes
                        }
                        else
                        {
                            // Checksum bytes aren't all here yet
                            content_length = std::string::npos;
                        }
                    }
                }
                break;

            default:
                {
                    // We have an unexpected byte and we need to flush all bad 
                    // data that is in m_bytes, so we need to find the first
                    // byte that is a '+' (ACK), '-' (NACK), \x03 (CTRL+C interrupt),
                    // or '$' character (start of packet header) or of course,
                    // the end of the data in m_bytes...
                    const size_t bytes_len = m_bytes.size();
                    bool done = false;
                    uint32_t idx;
                    for (idx = 1; !done && idx < bytes_len; ++idx)
                    {
                        switch (m_bytes[idx])
                        {
                        case '+':
                        case '-':
                        case '\x03':
                        case '$':
                            done = true;
                            break;
                                
                        default:
                            break;
                        }
                    }
                    if (log)
                        log->Printf ("GDBRemoteCommunication::%s tossing %u junk bytes: '%.*s'",
                                     __FUNCTION__, idx - 1, idx - 1, m_bytes.c_str());
                    m_bytes.erase(0, idx - 1);
                }
                break;
        }

        if (content_length == std::string::npos)
        {
            packet.Clear();
            return false;
        }
        else if (total_length > 0)
        {

            // We have a valid packet...
            assert (content_length <= m_bytes.size());
            assert (total_length <= m_bytes.size());
            assert (content_length <= total_length);
            const size_t content_end = content_start + content_length;

            bool success = true;
            std::string &packet_str = packet.GetStringRef();
            
            
            if (log)
            {
                // If logging was just enabled and we have history, then dump out what
                // we have to the log so we get the historical context. The Dump() call that
                // logs all of the packet will set a boolean so that we don't dump this more
                // than once
                if (!m_history.DidDumpToLog ())
                    m_history.Dump (log);
                
                bool binary = false;
                // Only detect binary for packets that start with a '$' and have a '#CC' checksum
                if (m_bytes[0] == '$' && total_length > 4)
                {
                    for (size_t i=0; !binary && i<total_length; ++i)
                    {
                        if (isprint(m_bytes[i]) == 0)
                            binary = true;
                    }
                }
                if (binary)
                {
                    StreamString strm;
                    // Packet header...
                    strm.Printf("<%4" PRIu64 "> read packet: %c", (uint64_t)total_length, m_bytes[0]);
                    for (size_t i=content_start; i<content_end; ++i)
                    {
                        // Remove binary escaped bytes when displaying the packet...
                        const char ch = m_bytes[i];
                        if (ch == 0x7d)
                        {
                            // 0x7d is the escape character.  The next character is to
                            // be XOR'd with 0x20.
                            const char escapee = m_bytes[++i] ^ 0x20;
                            strm.Printf("%2.2x", escapee);
                        }
                        else
                        {
                            strm.Printf("%2.2x", (uint8_t)ch);
                        }
                    }
                    // Packet footer...
                    strm.Printf("%c%c%c", m_bytes[total_length-3], m_bytes[total_length-2], m_bytes[total_length-1]);
                    log->PutCString(strm.GetString().c_str());
                }
                else
                {
                    log->Printf("<%4" PRIu64 "> read packet: %.*s", (uint64_t)total_length, (int)(total_length), m_bytes.c_str());
                }
            }

            m_history.AddPacket (m_bytes.c_str(), total_length, History::ePacketTypeRecv, total_length);

            // Clear packet_str in case there is some existing data in it.
            packet_str.clear();
            // Copy the packet from m_bytes to packet_str expanding the
            // run-length encoding in the process.
            // Reserve enough byte for the most common case (no RLE used)
            packet_str.reserve(m_bytes.length());
            for (std::string::const_iterator c = m_bytes.begin() + content_start; c != m_bytes.begin() + content_end; ++c)
            {
                if (*c == '*')
                {
                    // '*' indicates RLE. Next character will give us the
                    // repeat count and previous character is what is to be
                    // repeated.
                    char char_to_repeat = packet_str.back();
                    // Number of time the previous character is repeated
                    int repeat_count = *++c + 3 - ' ';
                    // We have the char_to_repeat and repeat_count. Now push
                    // it in the packet.
                    for (int i = 0; i < repeat_count; ++i)
                        packet_str.push_back(char_to_repeat);
                }
                else if (*c == 0x7d)
                {
                    // 0x7d is the escape character.  The next character is to
                    // be XOR'd with 0x20.
                    char escapee = *++c ^ 0x20;
                    packet_str.push_back(escapee);
                }
                else
                {
                    packet_str.push_back(*c);
                }
            }

            if (m_bytes[0] == '$')
            {
                assert (checksum_idx < m_bytes.size());
                if (::isxdigit (m_bytes[checksum_idx+0]) || 
                    ::isxdigit (m_bytes[checksum_idx+1]))
                {
                    if (GetSendAcks ())
                    {
                        const char *packet_checksum_cstr = &m_bytes[checksum_idx];
                        char packet_checksum = strtol (packet_checksum_cstr, NULL, 16);
                        char actual_checksum = CalculcateChecksum (packet_str.c_str(), packet_str.size());
                        success = packet_checksum == actual_checksum;
                        if (!success)
                        {
                            if (log)
                                log->Printf ("error: checksum mismatch: %.*s expected 0x%2.2x, got 0x%2.2x", 
                                             (int)(total_length), 
                                             m_bytes.c_str(),
                                             (uint8_t)packet_checksum,
                                             (uint8_t)actual_checksum);
                        }
                        // Send the ack or nack if needed
                        if (!success)
                            SendNack();
                        else
                            SendAck();
                    }
                }
                else
                {
                    success = false;
                    if (log)
                        log->Printf ("error: invalid checksum in packet: '%s'\n", m_bytes.c_str());
                }
            }
            
            m_bytes.erase(0, total_length);
            packet.SetFilePos(0);
            return success;
        }
    }
    packet.Clear();
    return false;
}

Error
GDBRemoteCommunication::StartListenThread (const char *hostname, uint16_t port)
{
    Error error;
    if (m_listen_thread.IsJoinable())
    {
        error.SetErrorString("listen thread already running");
    }
    else
    {
        char listen_url[512];
        if (hostname && hostname[0])
            snprintf(listen_url, sizeof(listen_url), "listen://%s:%i", hostname, port);
        else
            snprintf(listen_url, sizeof(listen_url), "listen://%i", port);
        m_listen_url = listen_url;
        SetConnection(new ConnectionFileDescriptor());
        m_listen_thread = ThreadLauncher::LaunchThread(listen_url, GDBRemoteCommunication::ListenThread, this, &error);
    }
    return error;
}

bool
GDBRemoteCommunication::JoinListenThread ()
{
    if (m_listen_thread.IsJoinable())
        m_listen_thread.Join(nullptr);
    return true;
}

lldb::thread_result_t
GDBRemoteCommunication::ListenThread (lldb::thread_arg_t arg)
{
    GDBRemoteCommunication *comm = (GDBRemoteCommunication *)arg;
    Error error;
    ConnectionFileDescriptor *connection = (ConnectionFileDescriptor *)comm->GetConnection ();
    
    if (connection)
    {
        // Do the listen on another thread so we can continue on...
        if (connection->Connect(comm->m_listen_url.c_str(), &error) != eConnectionStatusSuccess)
            comm->SetConnection(NULL);
    }
    return NULL;
}

Error
GDBRemoteCommunication::StartDebugserverProcess (const char *hostname,
                                                 uint16_t in_port,
                                                 lldb_private::ProcessLaunchInfo &launch_info,
                                                 uint16_t &out_port)
{
    Log *log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("GDBRemoteCommunication::%s(hostname=%s, in_port=%" PRIu16 ", out_port=%" PRIu16, __FUNCTION__, hostname ? hostname : "<empty>", in_port, out_port);

    out_port = in_port;
    Error error;
    // If we locate debugserver, keep that located version around
    static FileSpec g_debugserver_file_spec;
    
    char debugserver_path[PATH_MAX];
    FileSpec &debugserver_file_spec = launch_info.GetExecutableFile();
    
    // Always check to see if we have an environment override for the path
    // to the debugserver to use and use it if we do.
    const char *env_debugserver_path = getenv("LLDB_DEBUGSERVER_PATH");
    if (env_debugserver_path)
    {
        debugserver_file_spec.SetFile (env_debugserver_path, false);
        if (log)
            log->Printf ("GDBRemoteCommunication::%s() gdb-remote stub exe path set from environment variable: %s", __FUNCTION__, env_debugserver_path);
    }
    else
        debugserver_file_spec = g_debugserver_file_spec;
    bool debugserver_exists = debugserver_file_spec.Exists();
    if (!debugserver_exists)
    {
        // The debugserver binary is in the LLDB.framework/Resources
        // directory.
        if (HostInfo::GetLLDBPath(ePathTypeSupportExecutableDir, debugserver_file_spec))
        {
            debugserver_file_spec.AppendPathComponent (DEBUGSERVER_BASENAME);
            debugserver_exists = debugserver_file_spec.Exists();
            if (debugserver_exists)
            {
                if (log)
                    log->Printf ("GDBRemoteCommunication::%s() found gdb-remote stub exe '%s'", __FUNCTION__, debugserver_file_spec.GetPath ().c_str ());

                g_debugserver_file_spec = debugserver_file_spec;
            }
            else
            {
                if (log)
                    log->Printf ("GDBRemoteCommunication::%s() could not find gdb-remote stub exe '%s'", __FUNCTION__, debugserver_file_spec.GetPath ().c_str ());

                g_debugserver_file_spec.Clear();
                debugserver_file_spec.Clear();
            }
        }
    }
    
    if (debugserver_exists)
    {
        debugserver_file_spec.GetPath (debugserver_path, sizeof(debugserver_path));

        Args &debugserver_args = launch_info.GetArguments();
        debugserver_args.Clear();
        char arg_cstr[PATH_MAX];
        
        // Start args with "debugserver /file/path -r --"
        debugserver_args.AppendArgument(debugserver_path);

        // If a host and port is supplied then use it
        char host_and_port[128];
        if (hostname)
        {
            snprintf (host_and_port, sizeof(host_and_port), "%s:%u", hostname, in_port);
            debugserver_args.AppendArgument(host_and_port);
        }
        else
        {
            host_and_port[0] = '\0';
        }

        // use native registers, not the GDB registers
        debugserver_args.AppendArgument("--native-regs");   
        // make debugserver run in its own session so signals generated by 
        // special terminal key sequences (^C) don't affect debugserver
        debugserver_args.AppendArgument("--setsid");

        char named_pipe_path[PATH_MAX];
        named_pipe_path[0] = '\0';

        bool listen = false;
        if (host_and_port[0])
        {
            // Create a temporary file to get the stdout/stderr and redirect the
            // output of the command into this file. We will later read this file
            // if all goes well and fill the data into "command_output_ptr"

            if (in_port == 0)
            {
                // Binding to port zero, we need to figure out what port it ends up
                // using using a named pipe...
                FileSpec tmpdir_file_spec;
                if (HostInfo::GetLLDBPath(ePathTypeLLDBTempSystemDir, tmpdir_file_spec))
                {
                    tmpdir_file_spec.AppendPathComponent("debugserver-named-pipe.XXXXXX");
                    strncpy(named_pipe_path, tmpdir_file_spec.GetPath().c_str(), sizeof(named_pipe_path));
                }
                else
                {
                    strncpy(named_pipe_path, "/tmp/debugserver-named-pipe.XXXXXX", sizeof(named_pipe_path));
                }

                if (::mktemp (named_pipe_path))
                {
#if defined(_WIN32)
                    if ( false )
#else
                    if (::mkfifo(named_pipe_path, 0600) == 0)
#endif
                    {
                        debugserver_args.AppendArgument("--named-pipe");
                        debugserver_args.AppendArgument(named_pipe_path);
                    }
                }
            }
            else
            {
                listen = true;
            }
        }
        else
        {
            // No host and port given, so lets listen on our end and make the debugserver
            // connect to us..
            error = StartListenThread ("127.0.0.1", 0);
            if (error.Fail())
                return error;

            ConnectionFileDescriptor *connection = (ConnectionFileDescriptor *)GetConnection ();
            // Wait for 10 seconds to resolve the bound port
            out_port = connection->GetListeningPort(10);
            if (out_port > 0)
            {
                char port_cstr[32];
                snprintf(port_cstr, sizeof(port_cstr), "127.0.0.1:%i", out_port);
                // Send the host and port down that debugserver and specify an option
                // so that it connects back to the port we are listening to in this process
                debugserver_args.AppendArgument("--reverse-connect");
                debugserver_args.AppendArgument(port_cstr);
            }
            else
            {
                error.SetErrorString ("failed to bind to port 0 on 127.0.0.1");
                return error;
            }
        }
        
        const char *env_debugserver_log_file = getenv("LLDB_DEBUGSERVER_LOG_FILE");
        if (env_debugserver_log_file)
        {
            ::snprintf (arg_cstr, sizeof(arg_cstr), "--log-file=%s", env_debugserver_log_file);
            debugserver_args.AppendArgument(arg_cstr);
        }
        
        const char *env_debugserver_log_flags = getenv("LLDB_DEBUGSERVER_LOG_FLAGS");
        if (env_debugserver_log_flags)
        {
            ::snprintf (arg_cstr, sizeof(arg_cstr), "--log-flags=%s", env_debugserver_log_flags);
            debugserver_args.AppendArgument(arg_cstr);
        }

        // Add additional args, starting with LLDB_DEBUGSERVER_EXTRA_ARG_1 until an env var doesn't come back.
        uint32_t env_var_index = 1;
        bool has_env_var;
        do
        {
            char env_var_name[64];
            snprintf (env_var_name, sizeof (env_var_name), "LLDB_DEBUGSERVER_EXTRA_ARG_%" PRIu32, env_var_index++);
            const char *extra_arg = getenv(env_var_name);
            has_env_var = extra_arg != nullptr;

            if (has_env_var)
            {
                debugserver_args.AppendArgument (extra_arg);
                if (log)
                    log->Printf ("GDBRemoteCommunication::%s adding env var %s contents to stub command line (%s)", __FUNCTION__, env_var_name, extra_arg);
            }
        } while (has_env_var);

        // Close STDIN, STDOUT and STDERR.
        launch_info.AppendCloseFileAction (STDIN_FILENO);
        launch_info.AppendCloseFileAction (STDOUT_FILENO);
        launch_info.AppendCloseFileAction (STDERR_FILENO);

        // Redirect STDIN, STDOUT and STDERR to "/dev/null".
        launch_info.AppendSuppressFileAction (STDIN_FILENO, true, false);
        launch_info.AppendSuppressFileAction (STDOUT_FILENO, false, true);
        launch_info.AppendSuppressFileAction (STDERR_FILENO, false, true);

        error = Host::LaunchProcess(launch_info);

        if (error.Success() && launch_info.GetProcessID() != LLDB_INVALID_PROCESS_ID)
        {
            if (named_pipe_path[0])
            {
                error = ReadPortFromPipe(named_pipe_path, out_port, 10);
                if (error.Success())
                {
                    if (log)
                        log->Printf("GDBRemoteCommunication::%s() debugserver listens %u port", __FUNCTION__, out_port);
                }
                else
                {
                    if (log)
                        log->Printf("GDBRemoteCommunication::%s() failed to read a port value from named pipe %s: %s", __FUNCTION__, named_pipe_path, error.AsCString());
                }
                FileSystem::Unlink(named_pipe_path);
            }
            else if (listen)
            {
                
            }
            else
            {
                // Make sure we actually connect with the debugserver...
                JoinListenThread();
            }
        }
    }
    else
    {
        error.SetErrorStringWithFormat ("unable to locate " DEBUGSERVER_BASENAME );
    }
    return error;
}

void
GDBRemoteCommunication::DumpHistory(Stream &strm)
{
    m_history.Dump (strm);
}
