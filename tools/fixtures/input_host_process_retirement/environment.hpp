// Only private child processes/events/threads touch the kernel. Protocol, target,
// visibility, MSI and PS are named memory ports, never a Host/SDK implementation.
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "json.hpp"
using json=nlohmann::json;
static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Ports {
    DWORD waitError=0,closeError=0,waitOverride=WAIT_TIMEOUT;
    bool overrideWait=false,pipeRetired=true,pipeTransfer=true;
    bool ack=true,shutdownExits=true,endpointAbsent=false,restore=true,ps=false,msi=false,targetHistorySettled=true;
    unsigned waits=0,closes=0,restores=0,kills=0,creates=0,requests=0,queued=0,targetExited=0;
    HANDLE childRelease=nullptr;
    json logs=json::array();
} ports;
static DWORD osWait(HANDLE h,DWORD ms){++ports.waits;if(ports.waitError){SetLastError(ports.waitError);return WAIT_FAILED;}if(ports.overrideWait)return ports.waitOverride;return ::WaitForSingleObject(h,ms);}
static BOOL osClose(HANDLE h){++ports.closes;if(ports.closeError){SetLastError(ports.closeError);return FALSE;}return ::CloseHandle(h);}
static BOOL noKill(HANDLE,UINT){++ports.kills;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
#define WaitForSingleObject osWait
#define CloseHandle osClose
#define TerminateProcess noKill
#include "input_host_process_retirement.h"
#include "virtual_controller_runtime_contract.h"
using InputHostMutex=std::mutex;
static InputHostMutex g_inputHostMx;
// Exact production ownership/global declarations are source-bound by runner.
// Primary is an explicit no-operation port: original scenes never adopt it.
#include "actual_host_handle_globals.hpp"
static bool g_inputHostPrepared=false,g_inputHostPrepareMayHaveReachedHost=false,g_inputHostNeutralized=false,g_inputHostActive=false,g_inputHostTransportFault=false,g_inputHostHello=false,g_inputHostPipeRetired=false,g_inputHostPipeConnected=false,g_inputHostMsiStageRequested=false,g_inputHostHidHideStateQuery=false,g_inputHostReleaseQuarantined=false,g_inputHostStartBlocked=false;
static std::string g_inputHostStartupNonce,g_inputHostHostInstanceId,g_inputHostBoundPersona,g_inputHostLifecycle,g_inputHostReleaseDisposition;
static std::wstring g_inputHostPipeName;
static ULONGLONG g_inputHostBoundPowerGeneration=0,g_inputHostSequence=0,g_inputHostSampleSequence=0,g_inputHostLastKeepAliveTick=0,g_inputHostSwitchRecoveryStartDeadline=0;
static std::atomic<ULONGLONG> g_inputHostHealthySinceTick{0},g_inputHostEpochMirror{0},g_inputHostLastKeepAliveRequestTick{0},g_powerGeneration{7};
static std::atomic<bool> g_wakeHoldPhysicalUnhide{false},g_wakeRescanRequested{false},g_msiVirtualReleaseRearmEligible{false},g_keyboardModifierEventGate{false};
static bool g_inputHostFreshNeutralAdmitted=false,g_inputHostAnalogAnchorInitialized=false,g_inputHostSourceCandidateLogged=false;
static int g_inputHostFreshNeutralFrames=0,g_inputHostFreshNeutralMask=0,g_inputHostAnalogAnchor=0,g_inputHostAnalogStableFrames=0;
static ULONGLONG g_inputHostLastPhysicalIdentityCheckTick=0,g_inputHostLastQuarantineProbeTick=0;
static bool g_xinputPhysicalSourceLocked=false,g_xinputReconfirmRejectLogged=false;
static unsigned g_xinputPhysicalSourceMask=0,g_xinputPreTargetSlots=0,g_xinputAdmittedMask=0,g_xinputLoggedAdmittedMask=0,g_xinputReconfirmMsiGeneration=0,g_xinputConnectedMask=1;
static std::atomic<unsigned> g_xinputSourceReconfirmMask{0};static std::atomic<bool> g_xinputPreTargetBoundaryScanned{false};static std::string g_xinputReconfirmIdentity;
static int g_inputHostRecreatedWakePowerProof=0;
struct RealStick{bool virtualTargetEnabled=false;}g_realStickTest;
struct Identity{std::string deviceInstanceId="physical",baseContainerDeviceInstanceId="physical-root";};
struct Journal{bool active=false;Identity physical;};static Journal g_hidHideVisibilityJournal;
struct Fence{bool debt=false;void clear(){debt=false;}bool required()const{return debt;}bool currentPinned(const std::string&,ULONGLONG)const{return false;}bool acceptRelease(const json&){return ports.endpointAbsent;}bool verified(const std::string&,ULONGLONG)const{return ports.endpointAbsent;}std::string run()const{return g_inputHostRunId;}ULONGLONG epoch()const{return g_inputHostEpoch;}json ownedTargets()const{return json::array();}std::string reason()const{return "explicit-memory-endpoint-port";}};
static Fence g_msiSteamDeckReleaseFence,g_msiSteamDeckReleaseDebt;
struct PS{bool debt=false;bool capable()const{return ports.ps;}void invalidate(const char*){}void captureRetirement(bool target){if(ports.ps&&target)debt=true;}bool retirementRequired()const{return debt;}bool confirmRetirement(const std::string&,ULONGLONG){if(!ports.endpointAbsent)return false;debt=false;return true;}int retirement()const{return 1;}void clear(){debt=false;}}g_psEndpointOwnership;
struct Inbox{void revoke(){}}g_inputHostGameMotorInbox;
static std::atomic<unsigned> g_inputHostGameMotorBindingGeneration{0};
static std::atomic<bool> g_msiDinputReturnOwed{false},g_msiDinputRouteDinputArmed{false},g_msiDinputRouteConfirmed{false},g_msiDinputAdmissionLogged{false};
static std::string g_exitSessionId="private-fixture";
namespace ymcc::screenpads{inline void observeNativeTransport(ULONGLONG,bool){}}
namespace ymcc::keyboardEvidence{inline int read(){return 0;}inline json toJson(int){return nullptr;}}
namespace ymcc::msi::rearm{inline constexpr bool buildEnabled=false;}
static ymcc::VirtualStartProtection g_virtualStartProtection;
static ULONGLONG inputHostStartProtectionNow(){return GetTickCount64();}
static std::string inputHostConfiguredPersona(){return "disabled";}
static const char* inputHostPersona(){return "steamdeck";}
static void inputHostStartProtectionFinish(std::uint64_t,const char*,const char*){}
static void appendNativeLifecycleLog(const char* event,const json& data){ports.logs.push_back({{"event",event},{"data",data}});}
static void writeLog(const char*,const std::string&){}
static void inputHostSwitchVerificationForgetAcceptedLocked(){}
static void gamepadSerialRevokeGameFeedback(){}
static void hidHideInvalidateRawInputIdentityCache(){}
static void inputHostClearBoundPersona(){g_inputHostBoundPersona.clear();}
static std::string inputHostBoundPersonaValue(){return g_inputHostBoundPersona;}
static void msiQuarantineDiagnosticBeginStop(){}
static void msiQuarantineDiagnosticReceipt(){}
static bool msiControllerTraceEnabled(){return false;}
static void msiControllerTraceLog(const char*,const json&,bool){}
static bool ymStartSeamActive(){return false;}
static void ymStartSeamTrace(const std::string&){}
static bool msiClawKnownBoardAuthority(){return ports.msi;}
static const char* inputHostIntentOriginForReason(const char*){return "memory-port";}
static unsigned inputStopSignalBeginLocked(const char*,const std::string&,unsigned long long){return 1;}
static void inputStopSignalPhaseLocked(unsigned,const char*,const char* = nullptr,int = -1,bool = false){}
static void msiPersonaModeCapture(const char*,bool,const std::string&){}
static bool msiPersonaModeRetain(const char*,bool,bool){return false;}
static void msiPersonaModeStopDone(bool){}
static void msiDinputStopForLifecycle(const char*,bool){g_msiDinputReturnOwed=false;}
static void msiPersonaModeNote(const char*,const char*){}
static bool inputHostTargetReturnHistorySettledLocked(){return ports.targetHistorySettled&&!g_msiSteamDeckReleaseDebt.required()&&!g_psEndpointOwnership.retirementRequired();}
static void inputHostTargetReturnTargetExitedLocked(const std::string&,ULONGLONG){++ports.targetExited;}
static bool inputHostTargetReturnAfterPsQuarantineLocked(int,bool absent){return absent;}
static bool psRetiredReturnProvenLocked(){return ports.endpointAbsent;}
static bool msiSteamDeckRecheckOwnedRemovalDebt(){return ports.endpointAbsent;}
static bool msiExplicitRearmRestoredLocked(){return false;}
static bool inputHostSwitchExecutionReturnedLocked(ULONGLONG,const std::string&){return false;}
static bool inputHostSwitchExecutionPreserveMsiIntentLocked(std::uint64_t,ULONGLONG,const std::string&,bool){return false;}
static bool inputHostSwitchRecoveryReturnedLocked(ULONGLONG,const std::string&){return false;}
static bool inputHostWakeReturnedLocked(ULONGLONG,const std::string&){return false;}
static void msiExplicitRearmRelease(const char*,bool,bool,bool,bool,ULONGLONG){}
static void inputHostSwitchAuditDeviceRemoved(const char*,const std::string&,ULONGLONG,const std::string&,bool){}
static void inputHostSwitchAuditToggleResult(const char*,bool,ULONGLONG,bool,bool,unsigned long long,unsigned long long){}
static void inputHostSwitchAuditRejected(const char*){}
static void inputHostSwitchAuditSkipped(const char*){}
static void ymStartEnsureRoute(const char*){}
static void inputHostPipeCloseOwned(HANDLE){/* named memory transport leaf */}
static DWORD ymStartHostExitWait(HANDLE process){return osWait(process,ports.shutdownExits?1000:0);}
static bool inputHostRequestLocked(const char* command,const json&,const char*,DWORD,json* receipt=nullptr){
    ++ports.requests;if(receipt)*receipt=json::object();
    if(std::strcmp(command,"SHUTDOWN")==0&&ports.ack&&ports.shutdownExits)need(SetEvent(ports.childRelease)!=FALSE,"release child");
    return ports.ack;
}
static void inputHostRestoreVisibilityViaPipeLocked(){/* memory only, no external proof */}
struct HidHideVisibilitySnapshot{std::vector<std::string> hidden;bool cloaking=true;};
static bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot&){return true;}
static bool hidHideContainsPath(const std::vector<std::string>&,const std::string&){return false;}
static bool hidHideRestorePhysicalTransaction(const char*){++ports.restores;if(ports.restore)g_hidHideVisibilityJournal.active=false;return ports.restore;}
static bool hidHideHoldPhysicalTransaction(const char*){g_hidHideVisibilityJournal.active=false;return true;}
static bool inputHostQueueLifecycleWork(const char*){++ports.queued;return true;}

enum class InputHostPublicationStage{Unknown};
static void inputHostNotePublicationStage(InputHostPublicationStage){}
static std::atomic<const char*> g_gmSessionResetReason{nullptr};
static std::atomic<bool> g_gmSessionResetRequested{false};
static constexpr std::uint64_t requestTicket=0;
static bool inputHostSwitchExecutionBeginReturnLocked(std::uint64_t){return false;}
static void inputHostSwitchExecutionReturnStoppedLocked(){}

// Explicit memory observation of the separately owned real transport runtime.
static bool inputHostPipeRetirementProvenLocked(){return ports.pipeRetired;}
