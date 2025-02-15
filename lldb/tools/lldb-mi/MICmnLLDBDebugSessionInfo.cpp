//===-- MICmnLLDBDebugSessionInfo.cpp ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//++
// File:        MICmnLLDBDebugSessionInfo.cpp
//
// Overview:    CMICmnLLDBDebugSessionInfo implementation.
//
// Environment: Compilers:  Visual C++ 12.
//                          gcc (Ubuntu/Linaro 4.8.1-10ubuntu9) 4.8.1
//              Libraries:  See MIReadmetxt.
//
// Copyright:   None.
//--

// Third party headers:
#include <lldb/API/SBThread.h>
#ifdef _WIN32
#include <io.h> // For the ::_access()
#else
#include <unistd.h> // For the ::access()
#endif              // _WIN32
#include <lldb/API/SBBreakpointLocation.h>

// In-house headers:
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnResources.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnMIValueList.h"
#include "MICmnMIValueTuple.h"
#include "MICmdData.h"
#include "MICmnLLDBUtilSBValue.h"

//++ ------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfo constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfo::CMICmnLLDBDebugSessionInfo(void)
    : m_rLldbDebugger(CMICmnLLDBDebugger::Instance().GetTheDebugger())
    , m_rLlldbListener(CMICmnLLDBDebugger::Instance().GetTheListener())
    , m_nBrkPointCntMax(INT32_MAX)
    , m_currentSelectedThread(LLDB_INVALID_THREAD_ID)
    , m_constStrSharedDataKeyWkDir("Working Directory")
    , m_constStrSharedDataSolibPath("Solib Path")
{
}

//++ ------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebugSessionInfo destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebugSessionInfo::~CMICmnLLDBDebugSessionInfo(void)
{
    Shutdown();
}

//++ ------------------------------------------------------------------------------------
// Details: Initialize resources for *this object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::Initialize(void)
{
    m_clientUsageRefCnt++;

    if (m_bInitialized)
        return MIstatus::success;

    m_currentSelectedThread = LLDB_INVALID_THREAD_ID;
    CMICmnLLDBDebugSessionInfoVarObj::VarObjIdResetToZero();

    m_bInitialized = MIstatus::success;

    return m_bInitialized;
}

//++ ------------------------------------------------------------------------------------
// Details: Release resources for *this object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::Shutdown(void)
{
    if (--m_clientUsageRefCnt > 0)
        return MIstatus::success;

    if (!m_bInitialized)
        return MIstatus::success;

    bool bOk = MIstatus::success;
    CMIUtilString errMsg;

    // Tidy up
    bOk = SharedDataDestroy();
    if (!bOk)
    {
        errMsg = CMIUtilString::Format(MIRSRC(IDS_DBGSESSION_ERR_SHARED_DATA_RELEASE));
        errMsg += "\n";
    }
    m_vecActiveThreadId.clear();
    CMICmnLLDBDebugSessionInfoVarObj::VarObjClear();

    m_bInitialized = false;

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Command instances can create and share data between other instances of commands.
//          Data can also be assigned by a command and retrieved by LLDB event handler.
//          This function takes down those resources build up over the use of the commands.
//          This function should be called when the creation and running of command has
//          stopped i.e. application shutdown.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::SharedDataDestroy(void)
{
    m_mapIdToSessionData.Clear();
    m_vecVarObj.clear();
    m_mapBrkPtIdToBrkPtInfo.clear();

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Record information about a LLDB break point so that is can be recalled in other
//          commands or LLDB event handling functions.
// Type:    Method.
// Args:    vBrkPtId        - (R) LLDB break point ID.
//          vrBrkPtInfo     - (R) Break point information object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::RecordBrkPtInfo(const MIuint vnBrkPtId, const SBrkPtInfo &vrBrkPtInfo)
{
    MapPairBrkPtIdToBrkPtInfo_t pr(vnBrkPtId, vrBrkPtInfo);
    m_mapBrkPtIdToBrkPtInfo.insert(pr);

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Retrieve information about a LLDB break point previous recorded either by
//          commands or LLDB event handling functions.
// Type:    Method.
// Args:    vBrkPtId        - (R) LLDB break point ID.
//          vrwBrkPtInfo    - (W) Break point information object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::RecordBrkPtInfoGet(const MIuint vnBrkPtId, SBrkPtInfo &vrwBrkPtInfo) const
{
    const MapBrkPtIdToBrkPtInfo_t::const_iterator it = m_mapBrkPtIdToBrkPtInfo.find(vnBrkPtId);
    if (it != m_mapBrkPtIdToBrkPtInfo.end())
    {
        vrwBrkPtInfo = (*it).second;
        return MIstatus::success;
    }

    return MIstatus::failure;
}

//++ ------------------------------------------------------------------------------------
// Details: Delete information about a specific LLDB break point object. This function
//          should be called when a LLDB break point is deleted.
// Type:    Method.
// Args:    vBrkPtId        - (R) LLDB break point ID.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::RecordBrkPtInfoDelete(const MIuint vnBrkPtId)
{
    const MapBrkPtIdToBrkPtInfo_t::const_iterator it = m_mapBrkPtIdToBrkPtInfo.find(vnBrkPtId);
    if (it != m_mapBrkPtIdToBrkPtInfo.end())
    {
        m_mapBrkPtIdToBrkPtInfo.erase(it);
        return MIstatus::success;
    }

    return MIstatus::failure;
}

//++ ------------------------------------------------------------------------------------
// Details: Retrieve the specified thread's frame information.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vThreadIdx      - (R) Thread index.
//          vwrThreadFrames - (W) Frame data.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::GetThreadFrames(const SMICmdData &vCmdData, const MIuint vThreadIdx, CMIUtilString &vwrThreadFrames)
{
    lldb::SBThread thread = m_lldbProcess.GetThreadByIndexID(vThreadIdx);
    const uint32_t nFrames = thread.GetNumFrames();
    if (nFrames == 0)
    {
        // MI print "frame={}"
        CMICmnMIValueTuple miValueTuple;
        CMICmnMIValueResult miValueResult("frame", miValueTuple);
        vwrThreadFrames = miValueResult.GetString();
        return MIstatus::success;
    }

    // MI print
    // "frame={level=\"%d\",addr=\"0x%08llx\",func=\"%s\",args=[%s],file=\"%s\",fullname=\"%s\",line=\"%d\"},frame={level=\"%d\",addr=\"0x%08llx\",func=\"%s\",args=[%s],file=\"%s\",fullname=\"%s\",line=\"%d\"},
    // ..."
    CMIUtilString strListCommaSeperated;
    for (MIuint nLevel = 0; nLevel < nFrames; nLevel++)
    {
        lldb::SBFrame frame = thread.GetFrameAtIndex(nLevel);
        lldb::addr_t pc = 0;
        CMIUtilString fnName;
        CMIUtilString fileName;
        CMIUtilString path;
        MIuint nLine = 0;
        if (!GetFrameInfo(frame, pc, fnName, fileName, path, nLine))
            return MIstatus::failure;

        // Function args
        CMICmnMIValueList miValueList(true);
        const MIuint maskVarTypes = 0x1000;
        if (!MIResponseFormVariableInfo(frame, maskVarTypes, miValueList))
            return MIstatus::failure;

        const MIchar *pUnknown = "??";
        if (fnName != pUnknown)
        {
            std::replace(fnName.begin(), fnName.end(), ')', ' ');
            std::replace(fnName.begin(), fnName.end(), '(', ' ');
            std::replace(fnName.begin(), fnName.end(), '\'', ' ');
        }

        CMICmnMIValueTuple miValueTuple;
        const CMIUtilString strLevel(CMIUtilString::Format("%d", nLevel));
        const CMICmnMIValueConst miValueConst(strLevel);
        const CMICmnMIValueResult miValueResult("level", miValueConst);
        miValueTuple.Add(miValueResult);
        if (!MIResponseFormFrameInfo2(pc, miValueList.GetString(), fnName, fileName, path, nLine, miValueTuple))
            return MIstatus::failure;

        const CMICmnMIValueResult miValueResult2("frame", miValueTuple);
        if (nLevel != 0)
            strListCommaSeperated += ",";
        strListCommaSeperated += miValueResult2.GetString();
    }

    vwrThreadFrames = strListCommaSeperated;

    return MIstatus::success;
}

// Todo: Refactor maybe to so only one function with this name, but not just yet
//++ ------------------------------------------------------------------------------------
// Details: Retrieve the specified thread's frame information.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vThreadIdx      - (R) Thread index.
//          vwrThreadFrames - (W) Frame data.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::GetThreadFrames2(const SMICmdData &vCmdData, const MIuint vThreadIdx, CMIUtilString &vwrThreadFrames)
{
    lldb::SBThread thread = m_lldbProcess.GetThreadByIndexID(vThreadIdx);
    const uint32_t nFrames = thread.GetNumFrames();
    if (nFrames == 0)
    {
        // MI print "frame={}"
        CMICmnMIValueTuple miValueTuple;
        CMICmnMIValueResult miValueResult("frame", miValueTuple);
        vwrThreadFrames = miValueResult.GetString();
        return MIstatus::success;
    }

    // MI print
    // "frame={level=\"%d\",addr=\"0x%08llx\",func=\"%s\",args=[%s],file=\"%s\",fullname=\"%s\",line=\"%d\"},frame={level=\"%d\",addr=\"0x%08llx\",func=\"%s\",args=[%s],file=\"%s\",fullname=\"%s\",line=\"%d\"},
    // ..."
    CMIUtilString strListCommaSeperated;
    for (MIuint nLevel = 0; nLevel < nFrames; nLevel++)
    {
        lldb::SBFrame frame = thread.GetFrameAtIndex(nLevel);
        lldb::addr_t pc = 0;
        CMIUtilString fnName;
        CMIUtilString fileName;
        CMIUtilString path;
        MIuint nLine = 0;
        if (!GetFrameInfo(frame, pc, fnName, fileName, path, nLine))
            return MIstatus::failure;

        // Function args
        CMICmnMIValueList miValueList(true);
        const MIuint maskVarTypes = 0x1000;
        if (!MIResponseFormVariableInfo2(frame, maskVarTypes, miValueList))
            return MIstatus::failure;

        const MIchar *pUnknown = "??";
        if (fnName != pUnknown)
        {
            std::replace(fnName.begin(), fnName.end(), ')', ' ');
            std::replace(fnName.begin(), fnName.end(), '(', ' ');
            std::replace(fnName.begin(), fnName.end(), '\'', ' ');
        }

        CMICmnMIValueTuple miValueTuple;
        const CMIUtilString strLevel(CMIUtilString::Format("%d", nLevel));
        const CMICmnMIValueConst miValueConst(strLevel);
        const CMICmnMIValueResult miValueResult("level", miValueConst);
        miValueTuple.Add(miValueResult);
        if (!MIResponseFormFrameInfo2(pc, miValueList.GetString(), fnName, fileName, path, nLine, miValueTuple))
            return MIstatus::failure;

        const CMICmnMIValueResult miValueResult2("frame", miValueTuple);
        if (nLevel != 0)
            strListCommaSeperated += ",";
        strListCommaSeperated += miValueResult2.GetString();
    }

    vwrThreadFrames = strListCommaSeperated;

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Return the resolved file's path for the given file.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vPath           - (R) Original path.
//          vwrResolvedPath - (W) Resolved path.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::ResolvePath(const SMICmdData &vCmdData, const CMIUtilString &vPath, CMIUtilString &vwrResolvedPath)
{
    // ToDo: Verify this code as it does not work as vPath is always empty

    CMIUtilString strResolvedPath;
    if (!SharedDataRetrieve<CMIUtilString>(m_constStrSharedDataKeyWkDir, strResolvedPath))
    {
        vwrResolvedPath = "";
        SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_SHARED_DATA_NOT_FOUND), vCmdData.strMiCmd.c_str(),
                                                  m_constStrSharedDataKeyWkDir.c_str()));
        return MIstatus::failure;
    }

    vwrResolvedPath = vPath;

    return ResolvePath(strResolvedPath, vwrResolvedPath);
}

//++ ------------------------------------------------------------------------------------
// Details: Return the resolved file's path for the given file.
// Type:    Method.
// Args:    vstrUnknown     - (R)   String assigned to path when resolved path is empty.
//          vwrResolvedPath - (RW)  The original path overwritten with resolved path.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::ResolvePath(const CMIUtilString &vstrUnknown, CMIUtilString &vwrResolvedPath)
{
    if (vwrResolvedPath.size() < 1)
    {
        vwrResolvedPath = vstrUnknown;
        return MIstatus::success;
    }

    bool bOk = MIstatus::success;

    CMIUtilString::VecString_t vecPathFolders;
    const MIuint nSplits = vwrResolvedPath.Split("/", vecPathFolders);
    MIunused(nSplits);
    MIuint nFoldersBack = 1; // 1 is just the file (last element of vector)
    while (bOk && (vecPathFolders.size() >= nFoldersBack))
    {
        CMIUtilString strTestPath;
        MIuint nFoldersToAdd = nFoldersBack;
        while (nFoldersToAdd > 0)
        {
            strTestPath += "/";
            strTestPath += vecPathFolders[vecPathFolders.size() - nFoldersToAdd];
            nFoldersToAdd--;
        }
        bool bYesAccessible = false;
        bOk = AccessPath(strTestPath, bYesAccessible);
        if (bYesAccessible)
        {
            vwrResolvedPath = strTestPath;
            return MIstatus::success;
        }
        else
            nFoldersBack++;
    }

    // No files exist in the union of working directory and debuginfo path
    // Simply use the debuginfo path and let the IDE handle it.

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Determine the given file path exists or not.
// Type:    Method.
// Args:    vPath               - (R) File name path.
//          vwbYesAccessible    - (W) True - file exists, false = does not exist.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::AccessPath(const CMIUtilString &vPath, bool &vwbYesAccessible)
{
#ifdef _WIN32
    vwbYesAccessible = (::_access(vPath.c_str(), 0) == 0);
#else
    vwbYesAccessible = (::access(vPath.c_str(), 0) == 0);
#endif // _WIN32

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vrThread        - (R) LLDB thread object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormThreadInfo(const SMICmdData &vCmdData, const lldb::SBThread &vrThread,
                                                     CMICmnMIValueTuple &vwrMIValueTuple)
{
    lldb::SBThread &rThread = const_cast<lldb::SBThread &>(vrThread);

    CMIUtilString strFrames;
    if (!GetThreadFrames(vCmdData, rThread.GetIndexID(), strFrames))
        return MIstatus::failure;

    const bool bSuspended = rThread.IsSuspended();
    const lldb::StopReason eReason = rThread.GetStopReason();
    const bool bValidReason = !((eReason == lldb::eStopReasonNone) || (eReason == lldb::eStopReasonInvalid));
    const CMIUtilString strState((bSuspended || bValidReason) ? "stopped" : "running");

    // Add "id"
    const CMIUtilString strId(CMIUtilString::Format("%d", rThread.GetIndexID()));
    const CMICmnMIValueConst miValueConst1(strId);
    const CMICmnMIValueResult miValueResult1("id", miValueConst1);
    if (!vwrMIValueTuple.Add(miValueResult1))
        return MIstatus::failure;

    // Add "target-id"
    const MIchar *pThreadName = rThread.GetName();
    const MIuint len = (pThreadName != nullptr) ? CMIUtilString(pThreadName).length() : 0;
    const bool bHaveName = ((pThreadName != nullptr) && (len > 0) && (len < 32) &&
                            CMIUtilString::IsAllValidAlphaAndNumeric(*pThreadName)); // 32 is arbitary number
    const MIchar *pThrdFmt = bHaveName ? "%s" : "Thread %d";
    CMIUtilString strThread;
    if (bHaveName)
        strThread = CMIUtilString::Format(pThrdFmt, pThreadName);
    else
        strThread = CMIUtilString::Format(pThrdFmt, rThread.GetIndexID());
    const CMICmnMIValueConst miValueConst2(strThread);
    const CMICmnMIValueResult miValueResult2("target-id", miValueConst2);
    if (!vwrMIValueTuple.Add(miValueResult2))
        return MIstatus::failure;

    // Add "frame"
    const CMICmnMIValueConst miValueConst3(strFrames, true);
    if (!vwrMIValueTuple.Add(miValueConst3, false))
        return MIstatus::failure;

    // Add "state"
    const CMICmnMIValueConst miValueConst4(strState);
    const CMICmnMIValueResult miValueResult4("state", miValueConst4);
    if (!vwrMIValueTuple.Add(miValueResult4))
        return MIstatus::failure;

    return MIstatus::success;
}

// Todo: Refactor maybe to so only one function with this name, but not just yet
//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vrThread        - (R) LLDB thread object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormThreadInfo3(const SMICmdData &vCmdData, const lldb::SBThread &vrThread,
                                                      CMICmnMIValueTuple &vwrMIValueTuple)
{
    lldb::SBThread &rThread = const_cast<lldb::SBThread &>(vrThread);

    CMIUtilString strFrames;
    if (!GetThreadFrames2(vCmdData, rThread.GetIndexID(), strFrames))
        return MIstatus::failure;

    const bool bSuspended = rThread.IsSuspended();
    const lldb::StopReason eReason = rThread.GetStopReason();
    const bool bValidReason = !((eReason == lldb::eStopReasonNone) || (eReason == lldb::eStopReasonInvalid));
    const CMIUtilString strState((bSuspended || bValidReason) ? "stopped" : "running");

    // Add "id"
    const CMIUtilString strId(CMIUtilString::Format("%d", rThread.GetIndexID()));
    const CMICmnMIValueConst miValueConst1(strId);
    const CMICmnMIValueResult miValueResult1("id", miValueConst1);
    if (!vwrMIValueTuple.Add(miValueResult1))
        return MIstatus::failure;

    // Add "target-id"
    const MIchar *pThreadName = rThread.GetName();
    const MIuint len = (pThreadName != nullptr) ? CMIUtilString(pThreadName).length() : 0;
    const bool bHaveName = ((pThreadName != nullptr) && (len > 0) && (len < 32) &&
                            CMIUtilString::IsAllValidAlphaAndNumeric(*pThreadName)); // 32 is arbitary number
    const MIchar *pThrdFmt = bHaveName ? "%s" : "Thread %d";
    CMIUtilString strThread;
    if (bHaveName)
        strThread = CMIUtilString::Format(pThrdFmt, pThreadName);
    else
        strThread = CMIUtilString::Format(pThrdFmt, rThread.GetIndexID());
    const CMICmnMIValueConst miValueConst2(strThread);
    const CMICmnMIValueResult miValueResult2("target-id", miValueConst2);
    if (!vwrMIValueTuple.Add(miValueResult2))
        return MIstatus::failure;

    // Add "frame"
    const CMICmnMIValueConst miValueConst3(strFrames, true);
    if (!vwrMIValueTuple.Add(miValueConst3, false))
        return MIstatus::failure;

    // Add "state"
    const CMICmnMIValueConst miValueConst4(strState);
    const CMICmnMIValueResult miValueResult4("state", miValueConst4);
    if (!vwrMIValueTuple.Add(miValueResult4))
        return MIstatus::failure;

    return MIstatus::success;
}

// Todo: Refactor maybe to so only one function with this name, but not just yet
//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vCmdData        - (R) A command's information.
//          vrThread        - (R) LLDB thread object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormThreadInfo2(const SMICmdData &vCmdData, const lldb::SBThread &vrThread,
                                                      CMICmnMIValueTuple &vwrMIValueTuple)
{
    lldb::SBThread &rThread = const_cast<lldb::SBThread &>(vrThread);

    const bool bSuspended = rThread.IsSuspended();
    const lldb::StopReason eReason = rThread.GetStopReason();
    const bool bValidReason = !((eReason == lldb::eStopReasonNone) || (eReason == lldb::eStopReasonInvalid));
    const CMIUtilString strState((bSuspended || bValidReason) ? "stopped" : "running");

    // Add "id"
    const CMIUtilString strId(CMIUtilString::Format("%d", rThread.GetIndexID()));
    const CMICmnMIValueConst miValueConst1(strId);
    const CMICmnMIValueResult miValueResult1("id", miValueConst1);
    if (!vwrMIValueTuple.Add(miValueResult1))
        return MIstatus::failure;

    // Add "target-id"
    const MIchar *pThreadName = rThread.GetName();
    const MIuint len = (pThreadName != nullptr) ? CMIUtilString(pThreadName).length() : 0;
    const bool bHaveName = ((pThreadName != nullptr) && (len > 0) && (len < 32) &&
                            CMIUtilString::IsAllValidAlphaAndNumeric(*pThreadName)); // 32 is arbitary number
    const MIchar *pThrdFmt = bHaveName ? "%s" : "Thread %d";
    CMIUtilString strThread;
    if (bHaveName)
        strThread = CMIUtilString::Format(pThrdFmt, pThreadName);
    else
        strThread = CMIUtilString::Format(pThrdFmt, rThread.GetIndexID());
    const CMICmnMIValueConst miValueConst2(strThread);
    const CMICmnMIValueResult miValueResult2("target-id", miValueConst2);
    if (!vwrMIValueTuple.Add(miValueResult2))
        return MIstatus::failure;

    // Add "state"
    const CMICmnMIValueConst miValueConst4(strState);
    const CMICmnMIValueResult miValueResult4("state", miValueConst4);
    if (!vwrMIValueTuple.Add(miValueResult4))
        return MIstatus::failure;

    return MIstatus::success;
}

// Todo: Refactor maybe to so only one function with this name, but not just yet
//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vrFrame         - (R)   LLDB thread object.
//          vMaskVarTypes   - (R)   0x1000 = arguments,
//                                  0x0100 = locals,
//                                  0x0010 = statics,
//                                  0x0001 = in scope only.
//          vwrMIValueList  - (W)   MI value list object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormVariableInfo2(const lldb::SBFrame &vrFrame, const MIuint vMaskVarTypes,
                                                        CMICmnMIValueList &vwrMiValueList)
{
    bool bOk = MIstatus::success;
    lldb::SBFrame &rFrame = const_cast<lldb::SBFrame &>(vrFrame);

    const bool bArg = (vMaskVarTypes & 0x1000);
    const bool bLocals = (vMaskVarTypes & 0x0100);
    const bool bStatics = (vMaskVarTypes & 0x0010);
    const bool bInScopeOnly = (vMaskVarTypes & 0x0001);
    lldb::SBValueList listArg = rFrame.GetVariables(bArg, bLocals, bStatics, bInScopeOnly);
    const MIuint nArgs = listArg.GetSize();
    for (MIuint i = 0; bOk && (i < nArgs); i++)
    {
        lldb::SBValue value = listArg.GetValueAtIndex(i);
        const CMICmnLLDBUtilSBValue utilValue(value);
        const CMICmnMIValueConst miValueConst(utilValue.GetName());
        const CMICmnMIValueResult miValueResult("name", miValueConst);
        CMICmnMIValueTuple miValueTuple(miValueResult);
        const CMICmnMIValueConst miValueConst2(utilValue.GetValue());
        const CMICmnMIValueResult miValueResult2("value", miValueConst2);
        miValueTuple.Add(miValueResult2);
        bOk = vwrMiValueList.Add(miValueTuple);
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vrFrame         - (R)   LLDB thread object.
//          vMaskVarTypes   - (R)   0x1000 = arguments,
//                                  0x0100 = locals,
//                                  0x0010 = statics,
//                                  0x0001 = in scope only.
//          vwrMIValueList  - (W)   MI value list object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormVariableInfo(const lldb::SBFrame &vrFrame, const MIuint vMaskVarTypes,
                                                       CMICmnMIValueList &vwrMiValueList)
{
    bool bOk = MIstatus::success;
    lldb::SBFrame &rFrame = const_cast<lldb::SBFrame &>(vrFrame);

    const bool bArg = (vMaskVarTypes & 0x1000);
    const bool bLocals = (vMaskVarTypes & 0x0100);
    const bool bStatics = (vMaskVarTypes & 0x0010);
    const bool bInScopeOnly = (vMaskVarTypes & 0x0001);
    const MIuint nMaxRecusiveDepth = 10;
    MIuint nCurrentRecursiveDepth = 0;
    lldb::SBValueList listArg = rFrame.GetVariables(bArg, bLocals, bStatics, bInScopeOnly);
    const MIuint nArgs = listArg.GetSize();
    for (MIuint i = 0; bOk && (i < nArgs); i++)
    {
        lldb::SBValue value = listArg.GetValueAtIndex(i);
        bOk = GetVariableInfo(nMaxRecusiveDepth, value, false, vwrMiValueList, nCurrentRecursiveDepth);
    }

    return bOk;
}

// *** Do not refactor this function to be one function with same name as it can break more than
// *** than one stack type command
//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vrFrame         - (R)   LLDB thread object.
//          vMaskVarTypes   - (R)   0x1000 = arguments,
//                                  0x0100 = locals,
//                                  0x0010 = statics,
//                                  0x0001 = in scope only.
//          vwrMIValueList  - (W)   MI value list object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormVariableInfo3(const lldb::SBFrame &vrFrame, const MIuint vMaskVarTypes,
                                                        CMICmnMIValueList &vwrMiValueList)
{
    bool bOk = MIstatus::success;
    lldb::SBFrame &rFrame = const_cast<lldb::SBFrame &>(vrFrame);

    const bool bArg = (vMaskVarTypes & 0x1000);
    const bool bLocals = (vMaskVarTypes & 0x0100);
    const bool bStatics = (vMaskVarTypes & 0x0010);
    const bool bInScopeOnly = (vMaskVarTypes & 0x0001);
    const MIuint nMaxRecusiveDepth = 10;
    MIuint nCurrentRecursiveDepth = 0;
    lldb::SBValueList listArg = rFrame.GetVariables(bArg, bLocals, bStatics, bInScopeOnly);
    const MIuint nArgs = listArg.GetSize();
    for (MIuint i = 0; bOk && (i < nArgs); i++)
    {
        lldb::SBValue value = listArg.GetValueAtIndex(i);
        bOk = GetVariableInfo2(nMaxRecusiveDepth, value, false, vwrMiValueList, nCurrentRecursiveDepth);
    }

    return bOk;
}

// *** Do not refactor this function to be one function with same name as it can break more than
// *** than one stack type command
//++ ------------------------------------------------------------------------------------
// Details: Extract the value's name and value or recurse into child value object.
// Type:    Method.
// Args:    vnMaxDepth      - (R)  The max recursive depth for this function.
//          vrValue         - (R)  LLDB value object.
//          vbIsChildValue  - (R)  True = Value object is a child of a higher Value object,
//                          -      False =  Value object not a child.
//          vwrMIValueList  - (W)  MI value list object.
//          vnDepth         - (RW) The current recursive depth of this function.
//          // Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::GetVariableInfo(const MIuint vnMaxDepth, const lldb::SBValue &vrValue, const bool vbIsChildValue,
                                            CMICmnMIValueList &vwrMiValueList, MIuint &vrwnDepth)
{
    // *** Update GetVariableInfo2() with any code changes here ***

    // Check recursive depth
    if (vrwnDepth >= vnMaxDepth)
        return MIstatus::success;

    bool bOk = MIstatus::success;
    lldb::SBValue &rValue = const_cast<lldb::SBValue &>(vrValue);
    const CMICmnLLDBUtilSBValue utilValue(vrValue, true);
    CMICmnMIValueTuple miValueTuple;
    const MIchar *pName = rValue.GetName();
    MIunused(pName);
    const bool bIsPointerType = rValue.GetType().IsPointerType();
    const MIuint nChildren = rValue.GetNumChildren();
    if (nChildren == 0)
    {
        if (vbIsChildValue)
        {
            if (utilValue.IsCharType())
            {
                // For char types and try to form text string
                const CMICmnMIValueConst miValueConst(utilValue.GetValue().c_str(), true);
                miValueTuple.Add(miValueConst, true);
            }
            else
            {
                // For composite types
                const CMICmnMIValueConst miValueConst(
                    CMIUtilString::Format("%s = %s", utilValue.GetName().c_str(), utilValue.GetValue().c_str()), true);
                miValueTuple.Add(miValueConst, true);
            }
            return vwrMiValueList.Add(CMICmnMIValueConst(miValueTuple.ExtractContentNoBrackets(), true));
        }
        else
        {
            // Basic types
            const CMICmnMIValueConst miValueConst(utilValue.GetName());
            const CMICmnMIValueResult miValueResult("name", miValueConst);
            miValueTuple.Add(miValueResult);
            const CMICmnMIValueConst miValueConst2(utilValue.GetValue());
            const CMICmnMIValueResult miValueResult2("value", miValueConst2);
            miValueTuple.Add(miValueResult2);
            return vwrMiValueList.Add(miValueTuple);
        }
    }
    else if (bIsPointerType && utilValue.IsChildCharType())
    {
        // Append string text to the parent value information
        const CMICmnMIValueConst miValueConst(utilValue.GetName());
        const CMICmnMIValueResult miValueResult("name", miValueConst);
        miValueTuple.Add(miValueResult);

        const CMIUtilString &rText(utilValue.GetChildValueCString());
        if (rText.empty())
        {
            const CMICmnMIValueConst miValueConst(utilValue.GetValue());
            const CMICmnMIValueResult miValueResult("value", miValueConst);
            miValueTuple.Add(miValueResult);
        }
        else
        {
            if (utilValue.IsValueUnknown())
            {
                const CMICmnMIValueConst miValueConst(rText);
                const CMICmnMIValueResult miValueResult("value", miValueConst);
                miValueTuple.Add(miValueResult);
            }
            else
            {
                // Note code that has const in will not show the text suffix to the string pointer
                // i.e. const char * pMyStr = "blah"; ==> "0x00007000"" <-- Eclipse shows this
                // but        char * pMyStr = "blah"; ==> "0x00007000" "blah"" <-- Eclipse shows this
                const CMICmnMIValueConst miValueConst(CMIUtilString::Format("%s %s", utilValue.GetValue().c_str(), rText.c_str()));
                const CMICmnMIValueResult miValueResult("value", miValueConst);
                miValueTuple.Add(miValueResult);
            }
        }
        return vwrMiValueList.Add(miValueTuple);
    }
    else if (bIsPointerType)
    {
        if (vbIsChildValue)
        {
            // For composite types
            const CMICmnMIValueConst miValueConst(
                CMIUtilString::Format("%s = %s", utilValue.GetName().c_str(), utilValue.GetValue().c_str()), true);
            miValueTuple.Add(miValueConst, true);
            return vwrMiValueList.Add(CMICmnMIValueConst(miValueTuple.ExtractContentNoBrackets(), true));
        }
        else
        {
            // Basic types
            const CMICmnMIValueConst miValueConst(utilValue.GetName());
            const CMICmnMIValueResult miValueResult("name", miValueConst);
            miValueTuple.Add(miValueResult);
            const CMICmnMIValueConst miValueConst2(utilValue.GetValue());
            const CMICmnMIValueResult miValueResult2("value", miValueConst2);
            miValueTuple.Add(miValueResult2);
            return vwrMiValueList.Add(miValueTuple);
        }
    }
    else
    {
        // Build parent child composite types
        CMICmnMIValueList miValueList(true);
        for (MIuint i = 0; bOk && (i < nChildren); i++)
        {
            lldb::SBValue member = rValue.GetChildAtIndex(i);
            bOk = GetVariableInfo(vnMaxDepth, member, true, miValueList, ++vrwnDepth);
        }
        const CMICmnMIValueConst miValueConst(utilValue.GetName());
        const CMICmnMIValueResult miValueResult("name", miValueConst);
        miValueTuple.Add(miValueResult);
        const CMICmnMIValueConst miValueConst2(CMIUtilString::Format("{%s}", miValueList.ExtractContentNoBrackets().c_str()));
        const CMICmnMIValueResult miValueResult2("value", miValueConst2);
        miValueTuple.Add(miValueResult2);
        return vwrMiValueList.Add(miValueTuple);
    }
}

// *** Do not refactor this function to be one function with same name as it can break more than
// *** than one stack type command
//++ ------------------------------------------------------------------------------------
// Details: Extract the value's name and value or recurse into child value object.
// Type:    Method.
// Args:    vnMaxDepth      - (R)  The max recursive depth for this function.
//          vrValue         - (R)  LLDB value object.
//          vbIsChildValue  - (R)  True = Value object is a child of a higher Value object,
//                          -      False =  Value object not a child.
//          vwrMIValueList  - (W)  MI value list object.
//          vnDepth         - (RW) The current recursive depth of this function.
//          // Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::GetVariableInfo2(const MIuint vnMaxDepth, const lldb::SBValue &vrValue, const bool vbIsChildValue,
                                             CMICmnMIValueList &vwrMiValueList, MIuint &vrwnDepth)
{
    // *** Update GetVariableInfo() with any code changes here ***

    // Check recursive depth
    if (vrwnDepth >= vnMaxDepth)
        return MIstatus::success;

    bool bOk = MIstatus::success;
    lldb::SBValue &rValue = const_cast<lldb::SBValue &>(vrValue);
    const CMICmnLLDBUtilSBValue utilValue(vrValue, true);
    CMICmnMIValueTuple miValueTuple;
    const MIchar *pName = rValue.GetName();
    MIunused(pName);
    const MIuint nChildren = rValue.GetNumChildren();
    if (nChildren == 0)
    {
        if (vbIsChildValue && utilValue.IsCharType())
        {
            // For char types and try to form text string
            const CMICmnMIValueConst miValueConst(utilValue.GetValue().c_str(), true);
            miValueTuple.Add(miValueConst, true);
            return vwrMiValueList.Add(CMICmnMIValueConst(miValueTuple.ExtractContentNoBrackets(), true));
        }
        else
        {
            // Basic types
            const CMICmnMIValueConst miValueConst(utilValue.GetName());
            const CMICmnMIValueResult miValueResult("name", miValueConst);
            miValueTuple.Add(miValueResult);
            const CMICmnMIValueConst miValueConst2(utilValue.GetValue());
            const CMICmnMIValueResult miValueResult2("value", miValueConst2);
            miValueTuple.Add(miValueResult2);
            return vwrMiValueList.Add(miValueTuple);
        }
    }
    else if (utilValue.IsChildCharType())
    {
        // Append string text to the parent value information
        const CMICmnMIValueConst miValueConst(utilValue.GetName());
        const CMICmnMIValueResult miValueResult("name", miValueConst);
        miValueTuple.Add(miValueResult);

        const CMIUtilString &rText(utilValue.GetChildValueCString());
        if (rText.empty())
        {
            const CMICmnMIValueConst miValueConst(utilValue.GetValue());
            const CMICmnMIValueResult miValueResult("value", miValueConst);
            miValueTuple.Add(miValueResult);
        }
        else
        {
            if (utilValue.IsValueUnknown())
            {
                const CMICmnMIValueConst miValueConst(rText);
                const CMICmnMIValueResult miValueResult("value", miValueConst);
                miValueTuple.Add(miValueResult);
            }
            else
            {
                // Note code that has const in will not show the text suffix to the string pointer
                // i.e. const char * pMyStr = "blah"; ==> "0x00007000"" <-- Eclipse shows this
                // but        char * pMyStr = "blah"; ==> "0x00007000" "blah"" <-- Eclipse shows this
                const CMICmnMIValueConst miValueConst(CMIUtilString::Format("%s %s", utilValue.GetValue().c_str(), rText.c_str()));
                const CMICmnMIValueResult miValueResult("value", miValueConst);
                miValueTuple.Add(miValueResult);
            }
        }
        return vwrMiValueList.Add(miValueTuple);
    }
    else
    {
        // Build parent child composite types
        CMICmnMIValueList miValueList(true);
        for (MIuint i = 0; bOk && (i < nChildren); i++)
        {
            lldb::SBValue member = rValue.GetChildAtIndex(i);
            bOk = GetVariableInfo(vnMaxDepth, member, true, miValueList, ++vrwnDepth);
        }
        const CMICmnMIValueConst miValueConst(utilValue.GetName());
        const CMICmnMIValueResult miValueResult("name", miValueConst);
        miValueTuple.Add(miValueResult);
        const CMICmnMIValueConst miValueConst2(CMIUtilString::Format("{%s}", miValueList.ExtractContentNoBrackets().c_str()));
        const CMICmnMIValueResult miValueResult2("value", miValueConst2);
        miValueTuple.Add(miValueResult2);
        return vwrMiValueList.Add(miValueTuple);
    }
}

//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vrThread        - (R) LLDB thread object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormFrameInfo(const lldb::SBThread &vrThread, const MIuint vnLevel,
                                                    CMICmnMIValueTuple &vwrMiValueTuple)
{
    lldb::SBThread &rThread = const_cast<lldb::SBThread &>(vrThread);

    lldb::SBFrame frame = rThread.GetFrameAtIndex(vnLevel);
    lldb::addr_t pc = 0;
    CMIUtilString fnName;
    CMIUtilString fileName;
    CMIUtilString path;
    MIuint nLine = 0;
    if (!GetFrameInfo(frame, pc, fnName, fileName, path, nLine))
        return MIstatus::failure;

    // MI print "{level=\"0\",addr=\"0x%08llx\",func=\"%s\",file=\"%s\",fullname=\"%s\",line=\"%d\"}"
    const CMIUtilString strLevel(CMIUtilString::Format("%d", vnLevel));
    const CMICmnMIValueConst miValueConst(strLevel);
    const CMICmnMIValueResult miValueResult("level", miValueConst);
    CMICmnMIValueTuple miValueTuple(miValueResult);
    if (!MIResponseFormFrameInfo(pc, fnName, fileName, path, nLine, miValueTuple))
        return MIstatus::failure;

    vwrMiValueTuple = miValueTuple;

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Retrieve the frame information from LLDB frame object.
// Type:    Method.
// Args:    vrFrame         - (R) LLDB thread object.
//          vPc             - (W) Address number.
//          vFnName         - (W) Function name.
//          vFileName       - (W) File name text.
//          vPath           - (W) Full file name and path text.
//          vnLine          - (W) File line number.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::GetFrameInfo(const lldb::SBFrame &vrFrame, lldb::addr_t &vwPc, CMIUtilString &vwFnName,
                                         CMIUtilString &vwFileName, CMIUtilString &vwPath, MIuint &vwnLine)
{
    lldb::SBFrame &rFrame = const_cast<lldb::SBFrame &>(vrFrame);

    static char pBuffer[MAX_PATH];
    const MIuint nBytes = rFrame.GetLineEntry().GetFileSpec().GetPath(&pBuffer[0], sizeof(pBuffer));
    MIunused(nBytes);
    CMIUtilString strResolvedPath(&pBuffer[0]);
    const MIchar *pUnkwn = "??";
    if (!ResolvePath(pUnkwn, strResolvedPath))
        return MIstatus::failure;
    vwPath = strResolvedPath;

    vwPc = rFrame.GetPC();

    const MIchar *pFnName = rFrame.GetFunctionName();
    vwFnName = (pFnName != nullptr) ? pFnName : pUnkwn;

    const MIchar *pFileName = rFrame.GetLineEntry().GetFileSpec().GetFilename();
    vwFileName = (pFileName != nullptr) ? pFileName : pUnkwn;

    vwnLine = rFrame.GetLineEntry().GetLine();

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vPc             - (R) Address number.
//          vFnName         - (R) Function name.
//          vFileName       - (R) File name text.
//          vPath           - (R) Full file name and path text.
//          vnLine          - (R) File line number.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormFrameInfo(const lldb::addr_t vPc, const CMIUtilString &vFnName, const CMIUtilString &vFileName,
                                                    const CMIUtilString &vPath, const MIuint vnLine, CMICmnMIValueTuple &vwrMiValueTuple)
{
    const CMIUtilString strAddr(CMIUtilString::Format("0x%08llx", vPc));
    const CMICmnMIValueConst miValueConst2(strAddr);
    const CMICmnMIValueResult miValueResult2("addr", miValueConst2);
    if (!vwrMiValueTuple.Add(miValueResult2))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst3(vFnName);
    const CMICmnMIValueResult miValueResult3("func", miValueConst3);
    if (!vwrMiValueTuple.Add(miValueResult3))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst5(vFileName);
    const CMICmnMIValueResult miValueResult5("file", miValueConst5);
    if (!vwrMiValueTuple.Add(miValueResult5))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst6(vPath);
    const CMICmnMIValueResult miValueResult6("fullname", miValueConst6);
    if (!vwrMiValueTuple.Add(miValueResult6))
        return MIstatus::failure;
    const CMIUtilString strLine(CMIUtilString::Format("%d", vnLine));
    const CMICmnMIValueConst miValueConst7(strLine);
    const CMICmnMIValueResult miValueResult7("line", miValueConst7);
    if (!vwrMiValueTuple.Add(miValueResult7))
        return MIstatus::failure;

    return MIstatus::success;
}

// Todo: Refactor maybe to so only one function with this name, but not just yet
//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vPc             - (R) Address number.
//          vArgInfo        - (R) Args information in MI response form.
//          vFnName         - (R) Function name.
//          vFileName       - (R) File name text.
//          vPath           - (R) Full file name and path text.
//          vnLine          - (R) File line number.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormFrameInfo2(const lldb::addr_t vPc, const CMIUtilString &vArgInfo, const CMIUtilString &vFnName,
                                                     const CMIUtilString &vFileName, const CMIUtilString &vPath, const MIuint vnLine,
                                                     CMICmnMIValueTuple &vwrMiValueTuple)
{
    const CMIUtilString strAddr(CMIUtilString::Format("0x%08llx", vPc));
    const CMICmnMIValueConst miValueConst2(strAddr);
    const CMICmnMIValueResult miValueResult2("addr", miValueConst2);
    if (!vwrMiValueTuple.Add(miValueResult2))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst3(vFnName);
    const CMICmnMIValueResult miValueResult3("func", miValueConst3);
    if (!vwrMiValueTuple.Add(miValueResult3))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst4(vArgInfo, true);
    const CMICmnMIValueResult miValueResult4("args", miValueConst4);
    if (!vwrMiValueTuple.Add(miValueResult4))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst5(vFileName);
    const CMICmnMIValueResult miValueResult5("file", miValueConst5);
    if (!vwrMiValueTuple.Add(miValueResult5))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst6(vPath);
    const CMICmnMIValueResult miValueResult6("fullname", miValueConst6);
    if (!vwrMiValueTuple.Add(miValueResult6))
        return MIstatus::failure;
    const CMIUtilString strLine(CMIUtilString::Format("%d", vnLine));
    const CMICmnMIValueConst miValueConst7(strLine);
    const CMICmnMIValueResult miValueResult7("line", miValueConst7);
    if (!vwrMiValueTuple.Add(miValueResult7))
        return MIstatus::failure;

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vrBrkPtInfo     - (R) Break point information object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormBrkPtFrameInfo(const SBrkPtInfo &vrBrkPtInfo, CMICmnMIValueTuple &vwrMiValueTuple)
{
    const CMIUtilString strAddr(CMIUtilString::Format("0x%08llx", vrBrkPtInfo.m_pc));
    const CMICmnMIValueConst miValueConst2(strAddr);
    const CMICmnMIValueResult miValueResult2("addr", miValueConst2);
    if (!vwrMiValueTuple.Add(miValueResult2))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst3(vrBrkPtInfo.m_fnName);
    const CMICmnMIValueResult miValueResult3("func", miValueConst3);
    if (!vwrMiValueTuple.Add(miValueResult3))
        return MIstatus::failure;
    const CMICmnMIValueConst miValueConst5(vrBrkPtInfo.m_fileName);
    const CMICmnMIValueResult miValueResult5("file", miValueConst5);
    if (!vwrMiValueTuple.Add(miValueResult5))
        return MIstatus::failure;
    const CMIUtilString strN5 = CMIUtilString::Format("%s/%s", vrBrkPtInfo.m_path.c_str(), vrBrkPtInfo.m_fileName.c_str());
    const CMICmnMIValueConst miValueConst6(strN5);
    const CMICmnMIValueResult miValueResult6("fullname", miValueConst6);
    if (!vwrMiValueTuple.Add(miValueResult6))
        return MIstatus::failure;
    const CMIUtilString strLine(CMIUtilString::Format("%d", vrBrkPtInfo.m_nLine));
    const CMICmnMIValueConst miValueConst7(strLine);
    const CMICmnMIValueResult miValueResult7("line", miValueConst7);
    if (!vwrMiValueTuple.Add(miValueResult7))
        return MIstatus::failure;

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Form MI partial response by appending more MI value type objects to the
//          tuple type object past in.
// Type:    Method.
// Args:    vrBrkPtInfo     - (R) Break point information object.
//          vwrMIValueTuple - (W) MI value tuple object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::MIResponseFormBrkPtInfo(const SBrkPtInfo &vrBrkPtInfo, CMICmnMIValueTuple &vwrMiValueTuple)
{
    // MI print "=breakpoint-modified,bkpt={number=\"%d\",type=\"breakpoint\",disp=\"%s\",enabled=\"%c\",addr=\"0x%08x\",
    // func=\"%s\",file=\"%s\",fullname=\"%s/%s\",line=\"%d\",times=\"%d\",original-location=\"%s\"}"

    // "number="
    const CMICmnMIValueConst miValueConst(CMIUtilString::Format("%d", vrBrkPtInfo.m_id));
    const CMICmnMIValueResult miValueResult("number", miValueConst);
    CMICmnMIValueTuple miValueTuple(miValueResult);
    // "type="
    const CMICmnMIValueConst miValueConst2(vrBrkPtInfo.m_strType);
    const CMICmnMIValueResult miValueResult2("type", miValueConst2);
    bool bOk = miValueTuple.Add(miValueResult2);
    // "disp="
    const CMICmnMIValueConst miValueConst3(vrBrkPtInfo.m_bDisp ? "del" : "keep");
    const CMICmnMIValueResult miValueResult3("disp", miValueConst3);
    bOk = bOk && miValueTuple.Add(miValueResult3);
    // "enabled="
    const CMICmnMIValueConst miValueConst4(vrBrkPtInfo.m_bEnabled ? "y" : "n");
    const CMICmnMIValueResult miValueResult4("enabled", miValueConst4);
    bOk = bOk && miValueTuple.Add(miValueResult4);
    // "addr="
    // "func="
    // "file="
    // "fullname="
    // "line="
    bOk = bOk && MIResponseFormBrkPtFrameInfo(vrBrkPtInfo, miValueTuple);
    // "pending="
    if (vrBrkPtInfo.m_bPending)
    {
        const CMICmnMIValueConst miValueConst(vrBrkPtInfo.m_strOrigLoc);
        const CMICmnMIValueList miValueList(miValueConst);
        const CMICmnMIValueResult miValueResult("pending", miValueList);
        bOk = bOk && miValueTuple.Add(miValueResult);
    }
    if (vrBrkPtInfo.m_bHaveArgOptionThreadGrp)
    {
        const CMICmnMIValueConst miValueConst(vrBrkPtInfo.m_strOptThrdGrp);
        const CMICmnMIValueList miValueList(miValueConst);
        const CMICmnMIValueResult miValueResult("thread-groups", miValueList);
        bOk = bOk && miValueTuple.Add(miValueResult);
    }
    // "times="
    const CMICmnMIValueConst miValueConstB(CMIUtilString::Format("%d", vrBrkPtInfo.m_nTimes));
    const CMICmnMIValueResult miValueResultB("times", miValueConstB);
    bOk = bOk && miValueTuple.Add(miValueResultB);
    // "thread="
    if (vrBrkPtInfo.m_bBrkPtThreadId)
    {
        const CMICmnMIValueConst miValueConst(CMIUtilString::Format("%d", vrBrkPtInfo.m_nBrkPtThreadId));
        const CMICmnMIValueResult miValueResult("thread", miValueConst);
        bOk = bOk && miValueTuple.Add(miValueResult);
    }
    // "cond="
    if (vrBrkPtInfo.m_bCondition)
    {
        const CMICmnMIValueConst miValueConst(vrBrkPtInfo.m_strCondition);
        const CMICmnMIValueResult miValueResult("cond", miValueConst);
        bOk = bOk && miValueTuple.Add(miValueResult);
    }
    // "ignore="
    if (vrBrkPtInfo.m_nIgnore != 0)
    {
        const CMICmnMIValueConst miValueConst(CMIUtilString::Format("%d", vrBrkPtInfo.m_nIgnore));
        const CMICmnMIValueResult miValueResult("ignore", miValueConst);
        bOk = bOk && miValueTuple.Add(miValueResult);
    }
    // "original-location="
    const CMICmnMIValueConst miValueConstC(vrBrkPtInfo.m_strOrigLoc);
    const CMICmnMIValueResult miValueResultC("original-location", miValueConstC);
    bOk = bOk && miValueTuple.Add(miValueResultC);

    vwrMiValueTuple = miValueTuple;

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Retrieve breakpoint information and write into the given breakpoint information
//          object. Note not all possible information is retrieved and so the information
//          object may need to be filled in with more information after calling this
//          function. Mainly breakpoint location information of information that is
//          unlikely to change.
// Type:    Method.
// Args:    vBrkPt      - (R) LLDB break point object.
//          vrBrkPtInfo - (W) Break point information object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebugSessionInfo::GetBrkPtInfo(const lldb::SBBreakpoint &vBrkPt, SBrkPtInfo &vrwBrkPtInfo) const
{
    lldb::SBBreakpoint &rBrkPt = const_cast<lldb::SBBreakpoint &>(vBrkPt);
    lldb::SBBreakpointLocation brkPtLoc = rBrkPt.GetLocationAtIndex(0);
    lldb::SBAddress brkPtAddr = brkPtLoc.GetAddress();
    lldb::SBSymbolContext symbolCntxt = brkPtAddr.GetSymbolContext(lldb::eSymbolContextEverything);
    const MIchar *pUnkwn = "??";
    lldb::SBModule rModule = symbolCntxt.GetModule();
    const MIchar *pModule = rModule.IsValid() ? rModule.GetFileSpec().GetFilename() : pUnkwn;
    MIunused(pModule);
    const MIchar *pFile = pUnkwn;
    const MIchar *pFn = pUnkwn;
    const MIchar *pFilePath = pUnkwn;
    size_t nLine = 0;
    const size_t nAddr = brkPtAddr.GetLoadAddress(m_lldbTarget);

    lldb::SBCompileUnit rCmplUnit = symbolCntxt.GetCompileUnit();
    if (rCmplUnit.IsValid())
    {
        lldb::SBFileSpec rFileSpec = rCmplUnit.GetFileSpec();
        pFile = rFileSpec.GetFilename();
        pFilePath = rFileSpec.GetDirectory();
        lldb::SBFunction rFn = symbolCntxt.GetFunction();
        if (rFn.IsValid())
            pFn = rFn.GetName();
        lldb::SBLineEntry rLnEntry = symbolCntxt.GetLineEntry();
        if (rLnEntry.GetLine() > 0)
            nLine = rLnEntry.GetLine();
    }

    vrwBrkPtInfo.m_id = vBrkPt.GetID();
    vrwBrkPtInfo.m_strType = "breakpoint";
    vrwBrkPtInfo.m_pc = nAddr;
    vrwBrkPtInfo.m_fnName = pFn;
    vrwBrkPtInfo.m_fileName = pFile;
    vrwBrkPtInfo.m_path = pFilePath;
    vrwBrkPtInfo.m_nLine = nLine;
    vrwBrkPtInfo.m_nTimes = vBrkPt.GetHitCount();

    return MIstatus::success;
}
