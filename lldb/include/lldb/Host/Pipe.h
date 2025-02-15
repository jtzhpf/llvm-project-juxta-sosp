//===-- Pipe.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_Pipe_h_
#define liblldb_Host_Pipe_h_

#if defined(_WIN32)
#include "lldb/Host/windows/PipeWindows.h"
#else
#include "lldb/Host/posix/PipePosix.h"
#endif

#endif // liblldb_Host_Pipe_h_
