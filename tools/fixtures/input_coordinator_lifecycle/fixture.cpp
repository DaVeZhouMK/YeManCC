// Full production translation unit. Only listed external I/O/log ends are inert.
#define YMCC_FAULT_INJECT 1
#define YMCC_MSI_FOREGROUND_ISOLATION_TEST 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <powrprof.h>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <exception>
#include <cstdlib>
#include <crtdbg.h>
#include <functional>
#include <future>
#include <thread>
#include <atomic>
#include <chrono>
#ifdef YMCC_FIXTURE_OEM_AFTER_OFF
#include <mutex>
static std::recursive_mutex fixtureOemLogMx;
static std::atomic<unsigned> fixtureOemKeyboardActions{0},fixtureOemDesktopActions{0},fixtureOemActionTid{0};
static unsigned fixtureOemHookInstalls=0,fixtureOemHookRemoves=0;
static std::atomic<unsigned> fixtureOemJournalMutexRequests{0};
static HHOOK fixtureOemSetHook(int kind,HOOKPROC,HINSTANCE,DWORD){++fixtureOemHookInstalls;return reinterpret_cast<HHOOK>(kind==WH_KEYBOARD_LL?0x701:0x702);}
static BOOL fixtureOemUnhook(HHOOK){++fixtureOemHookRemoves;return TRUE;}
static LRESULT fixtureOemCallNextHook(HHOOK,int,WPARAM,LPARAM){return 0;}
#endif
#ifdef YMCC_GPD_PERSONA_MUTEX_PROBES
#include <mutex>
#include <condition_variable>
// POD TLS only. The generated TU wraps configured-persona reads solely around
// the wake header include; its real getter and both helper bodies stay intact.
static thread_local void (*fixtureGpdPersonaMutexObserver)(bool,void*)=nullptr;
static thread_local void* fixtureGpdPersonaMutexObserverContext=nullptr;
#endif
#include "json.hpp"
#include "virtual_target_return_record.h"
static bool fixtureSerialFailCreation=false;
static unsigned fixtureSerialCreationAttempts=0;
template<class F>static std::thread fixtureSerialCreateThread(F&& body){
    ++fixtureSerialCreationAttempts;
    if(fixtureSerialFailCreation){fixtureSerialFailCreation=false;throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));}
    return std::thread(std::forward<F>(body));
}
static void fixtureSerialAfterClosingRead();
static std::atomic<bool> fixtureSerialClaimRaceArmed{false};
static void fixtureSerialEntryInstallTerminateObserver() noexcept;
static std::atomic<unsigned> fixtureSerialKillCommands{0};
static unsigned forbiddenIo=0,inertXinputReads=0;
static unsigned fixtureExcludedSteamWallpaperNotifications=0;
static void fixtureObserveWallpaperNotification() noexcept;
static void fixtureCountForbiddenIo(const char* reason) noexcept;
// Explicit read-only OS capability/monotonic-clock leaves. Defaults preserve
// every existing case; PC wake cases advance real time without freezing IO.
static bool fixturePcPowerCapabilities=false;
static bool fixtureModernStandbyCapabilities=false;
static ULONGLONG fixturePowerClockOffset=0;
// Controlled preemption at the existing read-only OS clock boundary. Disabled
// for every other case; production producer/worker bodies remain unchanged.
static std::function<void()> fixtureMonotonicInterleave;
// Scoped observer for the explicit kept-source case only. The generated
// adapter retains the actual source projection and its original snapshot lock.
static std::function<void()> fixtureWakeAfterSourceQuery;
// Exact helper return observation: preserve its value and borrowed locks.
static std::function<void(bool)> fixtureSourceReconfirmAfterProof;
static std::function<void()> fixtureSourceReconfirmAfterReconfirmedLog;
static bool fixtureSourceReconfirmObserveProof(bool value){
 if(fixtureSourceReconfirmAfterProof){auto observer=std::move(fixtureSourceReconfirmAfterProof);fixtureSourceReconfirmAfterProof={};observer(value);}
 return value;
}
static thread_local bool fixtureInsideMonotonicInterleave=false;
static ULONGLONG fixtureOneXTick();
static ULONGLONG fixtureMonotonicTick(){
 const auto oneXTick=fixtureOneXTick();if(oneXTick)return oneXTick;
 if(fixtureMonotonicInterleave&&!fixtureInsideMonotonicInterleave){
  const auto observer=fixtureMonotonicInterleave;fixtureInsideMonotonicInterleave=true;
  try{observer();}catch(...){fixtureInsideMonotonicInterleave=false;throw;}
  fixtureInsideMonotonicInterleave=false;
 }
 return ::GetTickCount64()+fixturePowerClockOffset;
}
static BOOLEAN fixtureGetPwrCapabilities(PSYSTEM_POWER_CAPABILITIES caps){
 if(fixtureModernStandbyCapabilities){if(!caps)return FALSE;*caps={};caps->AoAc=TRUE;caps->SystemS3=FALSE;return TRUE;}
 if(fixturePcPowerCapabilities){if(!caps)return FALSE;*caps={};caps->SystemS3=TRUE;caps->AoAc=FALSE;return TRUE;}
 return ::GetPwrCapabilities(caps);
}
static const auto fixtureOneXRealSleep=&::Sleep;
static void WINAPI fixtureOneXSleep(DWORD);
#define Sleep fixtureOneXSleep
#define GetTickCount64 fixtureMonotonicTick
#define GetPwrCapabilities fixtureGetPwrCapabilities
static thread_local bool fixtureHealthInventoryRead=false;
static bool fixtureXinputSourceLive=false;
static unsigned fixtureXinputLivePolls[4]={0,0,0,0};
static std::uint64_t startProtectionClock=100000;
static const char* throwLogEvent=nullptr;
static std::vector<nlohmann::json> fixtureEvents;
static bool fixtureFullPipe=false;
#ifdef YMCC_PREPARE_REJECTION_COLD_CASE
static bool fixturePrepareRejectionCapture=false;
static nlohmann::json fixturePrepareRejectionWire=nlohmann::json::array();
#endif
static bool fixturePrepareRejectionSelector(const std::wstring& selectedCase){
#ifdef YMCC_PREPARE_REJECTION_COLD_CASE
 return selectedCase==L"case:prepare-rejection-cold";
#else
 (void)selectedCase;
 return false;
#endif
}
static bool fixtureMsiOwnedMetadata=false;
static std::function<void()> fixtureMsiRuntimeProofEdges;
static bool fixtureMsiExactUsbRoot(const wchar_t* path,std::wstring& out){
 if(!fixtureMsiOwnedMetadata){++forbiddenIo;throw std::logic_error("unadmitted-MSI-USB-identity-leaf");}
 const std::wstring value=path?path:L"";
 if(value==L"\\\\?\\HID#VID_0DB0&PID_1902#OFFLINE_MSI_COMMAND"){out=L"USB\\VID_0DB0&PID_1902\\7&OWNED&0&3";return true;}
 if(value==L"\\\\?\\HID#VID_0DB0&PID_1901#OFFLINE_MSI_RETURN"){out=L"USB\\VID_0DB0&PID_1901\\7&OWNED&0&3";return true;}
 out.clear();return false;
}


// OneX directed helper scope only. These seven explicit OS/dispatch leaves do
// not replace Current, MayIssue, the helper, source admission or final outcomes.
static bool fixtureOneXEnabled=false;
static std::string fixtureOneXShape;
static unsigned fixtureOneXLocationsReads=0,fixtureOneXDispatchCalls=0,fixtureOneXIssues=0;
static bool fixtureOneXReplayAdmitted=false;
static bool fixtureOneXInitialForeign=false;
static unsigned fixtureOneXForeignSlot=1;
static unsigned fixtureOneXMayIssueCalls=0;
static bool fixtureOneXStaleClockObserving=false;
static std::size_t fixtureOneXStaleTraceBegin=0;
static std::uint64_t fixtureOneXStaleSequence=0;
static std::uint64_t fixtureOneXDeadline=0;
// Parameters of existing managed Start/deadline cases; OS events and waits only.
static std::string fixtureOneXBoundaryVariant;
// Existing Apex/elite parameters. Inventory is observation-only in this scope.
static std::string fixtureOneXSelectedVariant,fixtureOneXSelectedInventoryVariant;
static bool fixtureOneXSelectedHideObserved=false;
static bool fixtureOneXBoundaryObserving=false,fixtureOneXLateArrivalApplied=false;
static unsigned fixtureOneXBoundaryDeferrals=0;
static std::size_t fixtureOneXReadmissionTraceBegin=0;
static nlohmann::json fixtureOneXIo=nlohmann::json::array();
struct HidHidePhysicalIdentity;
static bool fixtureOneXInitialPending=false;
static unsigned fixtureOneXInitialOwnerReads=0;
static bool fixtureOneXReturned=false,fixtureOneXOwnerReading=false,fixtureOneXSelectingRehide=false;
static unsigned fixtureOneXOwnerReads=0,fixtureOneXVisibilityReads=0,fixtureOneXRehideSelections=0;
static bool fixtureOneXReturnedGuardProven=false,fixtureOneXExternalSourceRejected=false,fixtureOneXExternalTargetRejected=false;
static bool fixtureOneXHideSeam(bool,const std::string&,bool);
static bool fixtureOneXReadPhysical(std::vector<HidHidePhysicalIdentity>&);
static void fixtureOneXPipeStage(const char*,bool,DWORD);
static bool fixtureOneXFreshMode(){return fixtureOneXShape=="flow"||fixtureOneXShape=="returned-stale"||fixtureOneXShape=="fresh-positive"||fixtureOneXShape=="external"||fixtureOneXShape=="unknown-port"||fixtureOneXShape=="owned-prehidden"||fixtureOneXShape=="initial-pending"||fixtureOneXShape=="rehide-switch-source"||fixtureOneXShape=="rehide-context-changed";}
struct HidHidePhysicalIdentity;
struct HidHideVisibilitySnapshot;
static std::vector<std::wstring> fixtureOneXLocations(const std::string&);
static int fixtureOneXPortKind(const std::string&);
static bool fixtureOneXSourceSlot(const HidHidePhysicalIdentity&,unsigned&);
static bool fixtureOneXRootStarted(const std::string&);
static bool fixtureOneXResolveSources(std::vector<HidHidePhysicalIdentity>&,bool*);
static bool fixtureOneXResolveSelectedChildren(std::vector<HidHidePhysicalIdentity>&,bool*);
static bool fixtureOneXVisibility(HidHideVisibilitySnapshot&);
static bool inputHostOneXVisibility(HidHideVisibilitySnapshot&);
namespace ymcc::targetReturn::physicalPath {struct Result;}
static ymcc::targetReturn::physicalPath::Result fixtureOneXVerifyPhysicalPath(const std::string&,const std::string&);
static bool fixtureOneXCycle(const std::wstring&,const char*,DWORD);

// Test-only existing log endpoint: producer arrives after empty token0 decision.
static std::function<void()> fixtureLateHideAfterEmptyDecision;
// Deterministic BUS preemption at an existing log I/O endpoint, never a product hook.
static std::function<void()> fixtureLateHideDuringDecision;
// Opt-in ROG131128 ports only. The product worker/helper/guards remain real.
// A successful existing hide seam may establish a fake, exactly-owned journal;
// the cycle leaf records the request/ACK and never touches a physical device.
static bool fixtureRogInitialIdentityEnabled=false,fixtureRogInitialDiscoveryComplete=true;
static unsigned fixtureRogInitialDiscoveryReads=0;
static std::function<void()> fixtureRogInitialDiscoveryCallback;
static std::string fixtureRogInitialShape="valid";
struct HidHidePhysicalIdentity;
static bool fixtureRogInitialDiscovery(std::vector<HidHidePhysicalIdentity>&,bool*);
static bool fixtureRogDeferredJournalSeed=false,fixtureRogDeferredCycleEnabled=false;
static bool fixtureRogPartialEnabled=false;
static std::string fixtureRogPartialShape="valid";
static unsigned fixtureRogPartialStage=0,fixtureRogPartialSnapshotReads=0;
static std::function<void()> fixtureRogPartialAfterFirst,fixtureRogPartialDuringRetry;
static std::function<void(unsigned)> fixtureRogPartialDuringSnapshot;
struct HidHideVisibilitySnapshot;
static bool fixtureRogPartialSnapshot(HidHideVisibilitySnapshot&);

// Switch-recovery only: actual Start supplies the transaction to this hide I/O leaf.
static bool fixtureSwitchOwnedHideSeed=false;
static std::function<void()> fixtureSwitchSourceAtEnsure;
static std::function<void()> fixtureGpdTransferSourceAtEnsure;
static std::string fixtureRogDeferredCycleResult="success";
static nlohmann::json fixtureRogDeferredHideRecords=nlohmann::json::array();
static nlohmann::json fixtureRogDeferredCycles=nlohmann::json::array();
static std::function<void()> fixtureRogDeferredAfterHide,fixtureRogDeferredCycleCallback;
// At the existing inert cycle I/O leaf, immediately before actual issue admission.
static std::function<void()> fixtureRogDeferredBeforeCycleIssue;
static nlohmann::json fixtureRogDeferredIssueGuards=nlohmann::json::array();
static nlohmann::json fixtureRogDeferredSnapshots=nlohmann::json::array();
static bool fixtureRogDeferredHideSeam(bool,const std::string&,bool);
static bool fixtureRogDeferredCycle(const std::wstring&,const char*,DWORD);
static int fixtureRogDeferredPortKind=0;
static nlohmann::json fixtureRogDeferredPortQueries=nlohmann::json::array();
static std::function<void()> fixtureRogDeferredPortCallback;
static int fixtureRogDeferredUsbPortKind(const std::string& root){
 if(!fixtureRogDeferredCycleEnabled){fixtureCountForbiddenIo("unexpected-USB-topology-query");throw std::logic_error("unexpected-USB-topology-query");}
 fixtureRogDeferredPortQueries.push_back({{"root",root},{"portKind",fixtureRogDeferredPortKind}});
 auto callback=std::move(fixtureRogDeferredPortCallback);fixtureRogDeferredPortCallback={};if(callback)callback();
 return fixtureRogDeferredPortKind;
}


// Opt-in archived cold-start regression. Only private files and controlled OS
// observations; real ColdEnvironment, Session/store, owner queue and Host pipe.
static bool fixtureColdArchive=false;
static bool fixtureColdMsiArchive=false;
namespace ymcc::targetReturn::physicalPath { struct Result; }
namespace ymcc::targetReturn::pnpAbsence { struct Result; }
static bool fixtureColdMsiHidden(std::vector<std::string>&);
static bool fixtureColdMsiCli(const std::wstring&,std::string*,DWORD*);
static ymcc::targetReturn::physicalPath::Result fixtureColdMsiPath(const std::string&,const std::string&);
static ymcc::targetReturn::pnpAbsence::Result fixtureColdMsiAbsence(const std::vector<std::string>&);
static std::string fixtureColdMachine;
namespace ymcc::targetReturn::process { struct Probe; }
namespace ymcc::targetReturn::pnpAbsence { struct Result; }
static ymcc::targetReturn::process::Probe fixtureColdWriter(const ymcc::targetReturn::ProcessIdentity&);
static ymcc::targetReturn::pnpAbsence::Result fixtureColdAbsence(const std::vector<std::string>&);
static ymcc::targetReturn::pnpAbsence::Result fixturePsProfileAbsence();
static void fixtureColdBeforeCreate();
// Opt-in runtime incident OS leaves. A retained observer is the exact native
// fixture Host event, never the Native parent or an archived/cold writer.
static bool fixtureRuntimeRecoveryOsEnabled=false;
static ULONGLONG fixtureClassifiedReturnReleasedEpoch=0;
static bool fixtureRuntimeRecoveryFaultEnabled=false,fixtureRuntimeRecoveryFaultConsumed=false;
static bool fixtureRuntimeRecoveryFirstHelloOnly=false;
static HANDLE fixtureRuntimeRecoveryOldHost=nullptr;
static std::optional<ymcc::targetReturn::ProcessIdentity> fixtureRuntimeRecoveryOldOwner;
static std::string fixtureRuntimeRecoveryCensus="unknown";
static unsigned fixtureRuntimeRecoveryFaultCalls=0,fixtureRuntimeRecoveryWriterCalls=0,fixtureRuntimeRecoveryCensusCalls=0;
static nlohmann::json fixtureRuntimeRecoveryOsObservations=nlohmann::json::array();
static std::function<void()> fixtureRuntimeRecoveryWriterPathCallback;
static unsigned fixtureRuntimeRecoveryWriterPathCallbackCalls=0;
static void fixtureRuntimeRecoveryWriterPathBoundary();
static ymcc::targetReturn::process::Probe fixtureRuntimeRecoveryWriter(const ymcc::targetReturn::ProcessIdentity&);
static ymcc::targetReturn::pnpAbsence::Result fixtureRuntimeRecoveryAbsence(const std::vector<std::string>&);
// Session paths are private per independent case; never reset inside recovery.
static bool fixtureOrdinarySwitchCliScope=false;
static std::wstring fixtureTargetReturnPath;
static std::uint64_t fixtureTargetReturnCaseSerial=0;
static std::function<void(const nlohmann::json&)> fixtureTargetReturnAfterReservation;
static nlohmann::json fixtureTargetReturnObservations=nlohmann::json::array();
static std::wstring fixtureTargetReturnRecordPath();
static std::wstring fixtureFullPipeCliRecordPath();
static ymcc::targetReturn::process::Probe fixtureFullPipeOwnedWriter(const ymcc::targetReturn::ProcessIdentity&);
static ymcc::targetReturn::pnpAbsence::Result fixtureFullPipeTargetAbsence(const std::vector<std::string>&);
static std::optional<ymcc::targetReturn::ProcessIdentity> fixtureTargetReturnCaptureOwner(HANDLE);
static void fixtureTargetReturnWireObserve(void*,DWORD);
static void fixtureTargetReturnAckLostObserve(const nlohmann::json&);
static void fixtureTargetReturnLifecycleObserve(const char*,const nlohmann::json&);
static void fixtureTargetReturnNativeCases();
static void fixtureTargetReturnFullPipeCases();
static DWORD fixtureStartupTransportError=ERROR_SUCCESS;
static bool fixtureFrameFirstAck=false;
// Actual owned pipe IO still runs. Observe its arguments and supply elapsed
// monotonic time at one completed external port; never replace parser/owner.
static bool fixturePipeBudgetEnabled=false,fixturePipeBudgetActive=false,fixturePipeBudgetAdvanced=false;
static std::uint64_t fixturePipeBudgetDeadline=0,fixturePipeBudgetAdvanceMs=0;
static std::string fixturePipeBudgetAdvancePhase,fixturePipeBudgetCommand="RECREATE_TARGET",fixturePipeBudgetCurrentCommand;
static std::uint64_t fixtureActiveExecutionDeadline();
static std::vector<nlohmann::json> fixturePipeBudgetStages;
static void fixturePipeBudgetPort(const char* stage,bool ok,DWORD timeout,void* data=nullptr,DWORD size=0){
 if(!fixturePipeBudgetEnabled)return;
 if(std::strcmp(stage,"write")==0){const auto request=nlohmann::json::parse(std::string(static_cast<char*>(data),size),nullptr,false);fixturePipeBudgetCurrentCommand=request.is_object()?request.value("command",""):"";
  fixturePipeBudgetActive=fixturePipeBudgetCurrentCommand==fixturePipeBudgetCommand||
   (fixturePipeBudgetCommand=="initial-start"&&(fixturePipeBudgetCurrentCommand=="HELLO"||fixturePipeBudgetCurrentCommand=="PREPARE_TARGET"||fixturePipeBudgetCurrentCommand=="SUBMIT_NEUTRAL"))||
   (fixtureOneXEnabled&&(fixtureOneXShape=="deadline-read-stage"||fixtureOneXShape=="returned-stale")&&fixturePipeBudgetCurrentCommand=="HIDHIDE_CYCLE");}
 if(!fixturePipeBudgetActive)return;
 fixturePipeBudgetStages.push_back({{"stage",stage},{"command",fixturePipeBudgetCurrentCommand},{"activeExecutionDeadline",fixtureActiveExecutionDeadline()},{"ok",ok},{"timeoutMs",timeout},{"clock",startProtectionClock},{"originalDeadline",fixturePipeBudgetDeadline},{"remainingMs",fixturePipeBudgetDeadline>startProtectionClock?fixturePipeBudgetDeadline-startProtectionClock:0}});
 fixtureOneXPipeStage(stage,ok,timeout);
 if(ok&&!fixturePipeBudgetAdvanced&&(fixturePipeBudgetAdvancePhase==stage||fixturePipeBudgetAdvancePhase==fixturePipeBudgetCurrentCommand+":"+stage)){startProtectionClock+=fixturePipeBudgetAdvanceMs;fixturePowerClockOffset+=fixturePipeBudgetAdvanceMs;fixturePipeBudgetAdvanced=true;}
}
static bool fixtureExitSweep=false;
static unsigned fixtureExitSweepLists=0,fixtureExitSweepUnhides=0,fixtureExitSweepCloakOffs=0;
static bool fixturePsReturnManualTopology=false;
static bool fixturePsAutoReturnTopology();
static bool fixturePrepareAckLoss=false,fixturePrepareAckLossConsumed=false;
static nlohmann::json fixturePrepareAckOriginal;
static bool fixtureWireCapture=false,fixtureWireFaultConsumed=false;
static std::string fixtureWirePrevious,fixtureWireFault;
static nlohmann::json fixtureWireOriginal,fixtureWireInjected;
static bool fixtureWakeScenario=false,fixtureWakeHideResult=true;
static unsigned fixtureWakeHideCalls=0,fixtureWakeVisibilityExclusions=0;
static std::function<void()> fixtureWakeBeforeHideReturn;
static unsigned fixtureWakeBlockingSleeps=0;
static void fixtureOsSleep(DWORD ms){if(fixtureWakeScenario){++fixtureWakeBlockingSleeps;fixturePowerClockOffset+=ms;return;}::Sleep(ms);}
#define Sleep fixtureOsSleep
static std::wstring fixtureManagedDll;
static BOOL fixtureCreateManagedProcess(STARTUPINFOW&,PROCESS_INFORMATION&);
static HDEVINFO fixturePsGetClassDevs(const GUID*,PCWSTR,HWND,DWORD);
static BOOL fixturePsEnum(HDEVINFO,PSP_DEVINFO_DATA,const GUID*,DWORD,PSP_DEVICE_INTERFACE_DATA);
static BOOL fixturePsDetail(HDEVINFO,PSP_DEVICE_INTERFACE_DATA,PSP_DEVICE_INTERFACE_DETAIL_DATA_W,DWORD,PDWORD,PSP_DEVINFO_DATA);
static BOOL fixturePsDestroy(HDEVINFO);
static CONFIGRET fixturePsDeviceId(DEVINST,PWSTR,ULONG,ULONG);
static CONFIGRET fixturePsLocate(PDEVINST,PWSTR,ULONG);
static CONFIGRET fixturePsParent(PDEVINST,DEVINST,ULONG);
static unsigned fullPipeRecreateCommands();
struct _XINPUT_STATE;
static DWORD fixtureControlledXinputState(DWORD,_XINPUT_STATE*);
static std::wstring fixtureControlledXinputPath(DWORD);
static BYTE fixtureControlledXinputIndex(const std::wstring&);
static HDEVINFO fixtureXboxCreate(const GUID*,HWND);
static BOOL fixtureXboxOpen(HDEVINFO,PCWSTR,DWORD,PSP_DEVICE_INTERFACE_DATA);
static BOOL fixtureXboxDetail(HDEVINFO,PSP_DEVICE_INTERFACE_DATA,PSP_DEVICE_INTERFACE_DETAIL_DATA_W,DWORD,PDWORD,PSP_DEVINFO_DATA);
static BOOL fixtureXboxDestroy(HDEVINFO);
static CONFIGRET fixtureXboxDeviceId(DEVINST,PWSTR,ULONG,ULONG);
static CONFIGRET fixtureXboxLocate(PDEVINST,DEVINSTID_W,ULONG);
static CONFIGRET fixtureXboxStatus(PULONG,PULONG,DEVINST,ULONG);
// Native-only startup owns unsignaled events in place of Host processes.
// Windows can report successful GetExitCodeProcess(event) with exit code 0;
// model that one process-query leaf without changing production liveness.
static BOOL fixtureOwnedHostExitCode(HANDLE,LPDWORD);
static void ymStartSeamTrace(const std::string&);
static bool fixtureGpdPostHideFullStart=false;
static std::atomic<bool> fixtureGpdFullCycleObserving{false};
static std::mutex fixtureGpdFullCycleObservationMx;
static nlohmann::json fixtureGpdFullCycleSourceReads=nlohmann::json::array();
static bool fixtureGpdPostHideApplied=false;
struct HidHideVisibilitySnapshot;
struct HidHidePhysicalIdentity;
static bool fixtureGpdPostHideApply(const std::string&,bool);
static HidHidePhysicalIdentity fixtureGpdPostHideSource();
static bool fixtureGpdPostHideSnapshot(HidHideVisibilitySnapshot&);
static bool fixtureGpdPostHidePhysicalIdentities(std::vector<HidHidePhysicalIdentity>&);
#define GetExitCodeProcess fixtureOwnedHostExitCode
#define YMCC_FOCUS_RECOVERY_PORTS
#include "focus_recovery_cases.inc"
#undef YMCC_FOCUS_RECOVERY_PORTS
static bool fixtureOwnedCliBoundary=false;
static std::function<void()> fixtureOwnedCliAfterDevList;
static std::vector<std::wstring> fixtureOwnedCliCommands,fixtureOwnedCliHidden;
static unsigned fixtureOwnedCliCallbacks=0,fixtureOwnedCliPhysicalQueries=0,fixtureOwnedCliFullQueries=0,fixtureOwnedCliWrites=0;
static std::vector<HidHidePhysicalIdentity> fixtureOwnedCliPhysical;
static bool fixtureOwnedCliRead(const std::wstring&,std::string*,DWORD*);

#if defined(YMCC_FIXTURE_SD_OFF_SOURCE_LOST)
static bool fixtureSdOffSourceLostActive();
static bool fixtureSdOffSourceLostCli(const std::wstring&,std::string*,DWORD*);
static bool fixtureSdOffSourceLostSnapshot(HidHideVisibilitySnapshot&);
static ymcc::targetReturn::physicalPath::Result fixtureSdOffSourceLostPath(const std::string&,const std::string&);
static void fixtureSdOffSourceLostObserve(const char*,const nlohmann::json&);
static void fixtureSdOffSharedPowerEdge(const std::string&);
static std::vector<std::string> fixtureSdOffSourceLostVariantNames();
static bool fixtureSdOffSourceLostSelector(const std::wstring& value){
 if(value==L"case:sd-off-source-lost")return true;
 const std::wstring prefix=L"case:sd-off-source-lost:";
 if(value.rfind(prefix,0)!=0||value.size()==prefix.size())return false;
 std::string selected;for(const auto c:value.substr(prefix.size())){if(c>127)return false;selected.push_back(static_cast<char>(c));}
 const auto variants=fixtureSdOffSourceLostVariantNames();
 return std::find(variants.begin(),variants.end(),selected)!=variants.end();
}
#else
static bool fixtureSdOffSourceLostSelector(const std::wstring&){return false;}
#endif

#if defined(YMCC_FIXTURE_GPD_PENDING_BEFORE_HIDE)
static void fixturePreHidePipe(const char*);
static void fixturePreHideGetterReturn();
static bool fixturePreHideActive();
static bool fixturePreHideSleep(DWORD);
static bool fixturePreHideApply(const std::string&,bool);
#endif

#include "main.inert.cpp"

#if defined(YMCC_FIXTURE_SD_OFF_SOURCE_LOST)
static void fixtureSdOffSharedPowerEdge(const std::string& edge){
 static ymcc::sleeppower::Dispatch::Receipt pending;
 if(edge=="ready"){
  if(pending){g_sleepPowerDispatch.complete(pending,GetTickCount64());pending={};}
  g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;
  ymcc::sleeppower::SharedPowerV1 power;
  if(!captureSharedPowerLifecycle(power)||!sharedPowerLifecycleCurrent(power))throw std::logic_error("sd-off-public-power-not-ready");
  return;
 }
 ymcc::sleeppower::SharedPowerV1 before;
 if(!captureSharedPowerLifecycle(before)||!sharedPowerLifecycleCurrent(before))throw std::logic_error("sd-off-edge-requires-current-public-capture");
 if(edge=="notification"){
  if(pending)throw std::logic_error("sd-off-notification-already-pending");
  pending=g_sleepPowerDispatch.window(PBT_APMSUSPEND,GetTickCount64());
  if(!pending||sharedPowerLifecycleCurrent(before))throw std::logic_error("sd-off-notification-did-not-retire-public-capture");
 }else if(edge=="generation"){
  const auto value=g_powerGeneration.load(std::memory_order_acquire);
  if(value==~decltype(value){0})throw std::logic_error("sd-off-generation-overflow");
  g_powerGeneration.store(value+1,std::memory_order_release);
  if(sharedPowerLifecycleCurrent(before))throw std::logic_error("sd-off-generation-did-not-retire-public-capture");
 }else if(edge=="publication"){
  g_sharedPowerPublicationRevision.fetch_add(1,std::memory_order_acq_rel);
  if(!sharedPowerLifecycleCurrent(before))throw std::logic_error("sd-off-publication-cancelled-healthy-power");
 }else throw std::invalid_argument("unknown-sd-off-shared-power-edge");
}
#endif
static bool fixtureOwnedCliRead(const std::wstring& command,std::string* output,DWORD* code){
 fixtureOwnedCliCommands.push_back(command);if(output)output->clear();if(code)*code=ERROR_SUCCESS;
 if(command==L"--dev-list"){
  if(output)for(const auto& hidden:fixtureOwnedCliHidden)*output+="--dev-hide \""+W2U(hidden)+"\"\n";
  auto callback=std::move(fixtureOwnedCliAfterDevList);fixtureOwnedCliAfterDevList={};
  if(callback){++fixtureOwnedCliCallbacks;callback();}
 }else if(command==L"--cloak-state"){if(output)*output="--cloak-on\n";}
 else if(command.rfind(L"--dev-hide \"",0)==0){
  ++fixtureOwnedCliWrites;
  const auto start=std::wstring(L"--dev-hide \"").size();
  const auto target=command.substr(start,command.size()-start-1);
  fixtureOwnedCliHidden.push_back(target);
 }else{++forbiddenIo;throw std::logic_error("unexpected-owned-cli-query:"+W2U(command));}
 return true;
}

#undef GetExitCodeProcess

// Read-only copy of the sample actually published by the synchronous owner below.
// The OS clock must not acquire source/sample mutexes recursively inside a guard.
// Boundary parameters inject only the same dirty event as a late OS arrival;
// all scan completion, admission and freshness decisions remain production code.
static InputCaptureGamepadSnapshot fixtureOneXStaleOwnerSample;
static ULONGLONG fixtureOneXTick(){
 // OS time leaf only: never rewrite a sample or the production freshness verdict.
 // The first freshSlot assignment follows the helper's real ymStartAdmitSource.
 const auto* e=t_inputHostOneXStartWrite;
 if(fixtureOneXEnabled&&!fixtureOneXBoundaryVariant.empty()&&fixtureOneXBoundaryVariant!="original-fresh"&&
    !fixtureOneXLateArrivalApplied&&!fixtureOneXOwnerReading&&!fixtureOneXBoundaryObserving&&
    e&&e->issueReserved&&e->returnedTick&&e->freshSlot<4){
  struct Observing {Observing(){fixtureOneXBoundaryObserving=true;}~Observing(){fixtureOneXBoundaryObserving=false;}} observing;
  const bool readmitted=fixtureOneXReadmissionTraceBegin<=g_startEntrySeam.trace.size()&&g_startEntrySeam.admitResult&&
   std::find(g_startEntrySeam.trace.begin()+fixtureOneXReadmissionTraceBegin,g_startEntrySeam.trace.end(),"admit:true")!=g_startEntrySeam.trace.end();
  const auto& sample=fixtureOneXStaleOwnerSample;
  const bool fresh=inputHostOneXFreshSample(*e,sample,e->freshSlot);
  if(readmitted&&fresh&&inputHostOneXStartCurrent()&&inputHostOneXSameReturnedSource(*e,g_hidHidePhysicalIdentity)&&
     fixtureXinputSourceLive&&g_xinputPreTargetSlots&&!g_xinputSourceReconfirmMask&&
     !g_xinputSlotFullScanOnce.load()&&!g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost){
   const auto boundary=g_xinputBoundaryScanEpoch.load(),topology=g_xinputTopologyEpoch.load();
   fixtureOneXLateArrivalApplied=true;
   xinputSlotsMarkDiscoveryDirty(); // OS arrival: no sample/scan epoch, admission or request flag is written here.
   fixtureOneXIo.push_back({{"leaf","late-topology-arrival"},{"variant",fixtureOneXBoundaryVariant},
    {"requestId",e->ticket},{"deadline",e->deadline},{"clock",startProtectionClock},{"runId",e->runId},{"hostEpoch",e->epoch},
    {"readmitted",readmitted},{"freshSampleBeforeArrival",fresh},
    {"oldBoundaryEpoch",boundary},{"topologyBefore",topology},{"currentEpoch",g_xinputTopologyEpoch.load()},
    {"boundaryAfterArrival",g_xinputBoundaryScanEpoch.load()},{"fullScanPending",g_xinputSlotFullScanOnce.load()},
    {"sourceLiveAtOsLeaf",fixtureXinputSourceLive},{"sourceLocked",static_cast<bool>(g_xinputPhysicalSourceLocked)},
    {"preTargetSlots",static_cast<unsigned>(g_xinputPreTargetSlots)},{"sourceMask",static_cast<unsigned>(g_xinputPhysicalSourceMask)},
    {"connectedMask",static_cast<unsigned>(xinputNonVirtualConnectedMask())},{"reconfirmMask",static_cast<unsigned>(g_xinputSourceReconfirmMask.load())},
    {"sampleSequence",sample.readAttemptSequence},{"sampleTopology",sample.readTopologyGeneration},
    {"prepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost}});
  }
 }
 if(fixtureOneXEnabled&&fixtureOneXShape=="returned-stale"&&!fixtureOneXOwnerReading&&
    !fixtureOneXStaleClockObserving&&e&&e->issueReserved&&e->returnedTick&&e->freshSlot<4){
  const auto& sample=fixtureOneXStaleOwnerSample;
  if(sample.readAttemptSequence>e->baselineSequence&&sample.readCompletedTick>=e->returnedTick&&
      sample.readAttemptSequence!=fixtureOneXStaleSequence){
    struct Observing {Observing(){fixtureOneXStaleClockObserving=true;}~Observing(){fixtureOneXStaleClockObserving=false;}} observing;
    const auto before=startProtectionClock;const auto aged=sample.readCompletedTick+201;
    if(startProtectionClock<aged){const auto delta=aged-startProtectionClock;startProtectionClock+=delta;fixturePowerClockOffset+=delta;}
    const bool readmitted=fixtureOneXStaleTraceBegin<=g_startEntrySeam.trace.size()&&g_startEntrySeam.admitResult&&
     std::find(g_startEntrySeam.trace.begin()+fixtureOneXStaleTraceBegin,g_startEntrySeam.trace.end(),"admit:true")!=g_startEntrySeam.trace.end();
    const auto slot=e->freshSlot;const bool nonAgeProof=slot<4&&g_xinputPhysicalSourceLocked&&g_xinputPrimarySlot==slot&&
     g_xinputPhysicalSourceMask==(1u<<slot)&&xinputNonVirtualConnectedMask()==(1u<<slot)&&!g_xinputSlotUnknownMask&&
     g_xinputPreTargetBoundaryScanned&&g_xinputBoundaryScanEpoch.load()==g_xinputTopologyEpoch.load()&&
     g_xinputTopologyEpoch.load()==e->freshTopology&&g_xinputSlotIdentityEpoch.load()==e->freshIdentity&&
     sample.live&&sample.readContextStable&&sample.primarySlot==slot&&sample.readContinuityGeneration&&
     sample.readContinuityGeneration!=(std::numeric_limits<uint64_t>::max)()&&sample.readStartedTick>=e->returnedTick&&
     sample.readCompletedTick>=sample.readStartedTick&&sample.readPowerGeneration==e->power&&
     sample.readTopologyGeneration==e->freshTopology&&sample.readIdentityGeneration==e->freshIdentity&&
     inputCaptureBindStandardInput(&sample).known();
    fixtureOneXIo.push_back({{"leaf","returned-sample-age"},{"clockBefore",before},{"clock",startProtectionClock},
     {"deadline",e->deadline},{"requestId",e->ticket},{"originalRoot",e->root},{"originalHid",e->sourceInstance},
     {"returnedRoot",e->freshSource.baseContainerDeviceInstanceId},{"returnedHid",e->freshSource.deviceInstanceId},
     {"returnedIdentity",e->freshSource.physicalIdentity},{"sampleFromActualCanonicalOwner",true},
     {"readmitted",readmitted},{"admitTraceBegin",fixtureOneXStaleTraceBegin},{"admitTraceEnd",g_startEntrySeam.trace.size()},
     {"sampleSequence",sample.readAttemptSequence},{"baselineSequence",e->baselineSequence},{"sampleStarted",sample.readStartedTick},
     {"sampleCompleted",sample.readCompletedTick},{"returnedTick",e->returnedTick},{"sampleAgeMs",startProtectionClock-sample.readCompletedTick},
     {"nonAgeProof",nonAgeProof},{"prepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost}});
    fixtureOneXStaleSequence=sample.readAttemptSequence;
  }
 }
 return fixtureOneXEnabled?startProtectionClock:0;
}
// Yield to the real canonical reader after the helper released its Host mutex
// and suspended scoped write permissions. Discovery itself never samples.
static void WINAPI fixtureOneXSleep(DWORD millis){
#if defined(YMCC_FIXTURE_GPD_PENDING_BEFORE_HIDE)
 if(fixturePreHideSleep(millis))return;
#endif
 if(fixtureOneXEnabled && fixtureOneXShape=="initial-pending" && !fixtureOneXReturned && g_inputHostProcess && g_inputHostHello &&
    !g_inputHostPrepared && !g_inputHostPrepareMayHaveReachedHost && !g_inputHostActive &&
    !g_inputHostMx.ownedByCurrentThread() && !g_hidHideVisibilityJournal.active && !t_inputHostOneXStartWrite){
  startProtectionClock+=millis;fixturePowerClockOffset+=millis;
  fixtureXinputSourceLive=true;fixtureOneXInitialPending=false;
  gamepadReadState(false);gamepadReadState(false);fixtureOneXInitialOwnerReads+=2;
  fixtureOneXIo.push_back({{"leaf","yield-pre-hide-to-canonical-owner"},{"sourceBecameLiveAtOsLeaf",true},
   {"hostLockOwned",false},{"prepared",g_inputHostPrepared},{"admissionNotForged",true}});return;
 }

 if(fixtureOneXEnabled&&!fixtureOneXSelectedVariant.empty()&&!fixtureOneXReturned&&fixtureOneXDispatchCalls&&
    g_inputHostProcess&&g_inputHostHello&&!g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost&&
    !g_inputHostMx.ownedByCurrentThread()&&!t_inputHostOneXStartWrite){
  const auto before=startProtectionClock;
  const auto delta=fixtureOneXDeadline>before?(std::min)(static_cast<std::uint64_t>(millis),fixtureOneXDeadline-before):0;
  startProtectionClock+=delta;fixturePowerClockOffset+=delta;
  if(fixtureOneXDispatchCalls>=3&&startProtectionClock<fixtureOneXDeadline){
   const auto remaining=fixtureOneXDeadline-startProtectionClock;startProtectionClock+=remaining;fixturePowerClockOffset+=remaining;
  }
  fixtureOneXIo.push_back({{"leaf","pre-cycle-unissued-owner-wait"},{"clockBefore",before},{"clock",startProtectionClock},
   {"deadline",fixtureOneXDeadline},{"cycleLeafCalls",fixtureOneXDispatchCalls},{"issuedRefreshes",fixtureOneXIssues},
   {"prepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost}});return;
 }
 if(fixtureOneXEnabled && fixtureOneXFreshMode() && fixtureOneXReturned && !fixtureOneXOwnerReading &&
    g_inputHostProcess && g_inputHostHello && !g_inputHostPrepared && !g_inputHostPrepareMayHaveReachedHost &&
    !g_inputHostActive && g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled &&
    !g_inputHostMx.ownedByCurrentThread() && !t_inputHostOneXStartWrite){
  if(fixtureOneXShape=="returned-stale"&&fixtureOneXDeadline>startProtectionClock&&
     fixtureOneXDeadline-startProtectionClock<=static_cast<std::uint64_t>(millis)+201){
   // Do not publish a fresh sample in the final <201ms interval. The last real
   // sample remains stale while this OS wait reaches the original deadline.
   const auto before=startProtectionClock;const auto delta=fixtureOneXDeadline-before;
   startProtectionClock+=delta;fixturePowerClockOffset+=delta;
   fixtureOneXIo.push_back({{"leaf","returned-stale-deadline-wait"},{"clockBefore",before},
    {"clock",startProtectionClock},{"deadline",fixtureOneXDeadline}});return;
  }
  const bool pending=g_xinputSlotFullScanOnce.load();
  const auto requestedEpoch=g_xinputSlotFullScanEpochStart.load(),boundaryBefore=g_xinputBoundaryScanEpoch.load();
  if(fixtureOneXLateArrivalApplied&&pending&&
     (fixtureOneXBoundaryVariant=="boundary-scan-not-consumed"||fixtureOneXBoundaryDeferrals<2)){
   // A controlled OS scheduling delay leaves the real pending request untouched.
   // Re-enter the original same-epoch loop before letting its owner consume it.
   ++fixtureOneXBoundaryDeferrals;startProtectionClock+=millis;fixturePowerClockOffset+=millis;
   if(fixtureOneXBoundaryVariant=="boundary-scan-not-consumed"&&fixtureOneXBoundaryDeferrals>=3&&startProtectionClock<fixtureOneXDeadline){
    const auto delta=fixtureOneXDeadline-startProtectionClock;startProtectionClock+=delta;fixturePowerClockOffset+=delta;
   }
   fixtureOneXIo.push_back({{"leaf","boundary-owner-deferred"},{"clock",startProtectionClock},{"deadline",fixtureOneXDeadline},
    {"pending",g_xinputSlotFullScanOnce.load()},{"requestedEpoch",g_xinputSlotFullScanEpochStart.load()},
    {"boundaryEpoch",g_xinputBoundaryScanEpoch.load()},{"topologyEpoch",g_xinputTopologyEpoch.load()},
    {"hostEpoch",g_inputHostEpoch},{"runId",g_inputHostRunId},{"sequence",g_inputHostSequence},
    {"cycles",fixtureOneXIssues},{"preTargetSlots",static_cast<unsigned>(g_xinputPreTargetSlots)},
    {"sourceLocked",static_cast<bool>(g_xinputPhysicalSourceLocked)},{"reconfirmMask",static_cast<unsigned>(g_xinputSourceReconfirmMask.load())},
    {"hostLockOwned",g_inputHostMx.ownedByCurrentThread()},{"scopedWriteInherited",t_inputHostOneXStartWrite!=nullptr},
    {"prepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost}});return;
  }
  struct Reading {Reading(){fixtureOneXOwnerReading=true;}~Reading(){fixtureOneXOwnerReading=false;}} reading;
  startProtectionClock+=millis;fixturePowerClockOffset+=millis;
  unsigned polls[4];for(unsigned slot=0;slot<4;++slot)polls[slot]=fixtureXinputLivePolls[slot];
  gamepadReadState(false);++fixtureOneXOwnerReads;
  const bool pendingAfterRead=g_xinputSlotFullScanOnce.load();const auto boundaryAfterRead=g_xinputBoundaryScanEpoch.load();
  nlohmann::json actualPolls=nlohmann::json::array();for(unsigned slot=0;slot<4;++slot)actualPolls.push_back(fixtureXinputLivePolls[slot]-polls[slot]);
  gamepadReadState(false);++fixtureOneXOwnerReads;
  if(fixtureOneXShape=="returned-stale"||!fixtureOneXBoundaryVariant.empty()){
   std::lock_guard<std::mutex> snapshot(g_inputCaptureGamepadSnapshotMx);fixtureOneXStaleOwnerSample=g_inputCaptureGamepadSnapshot;
  }
  if(fixtureOneXLateArrivalApplied&&pending){
   const auto& sample=fixtureOneXStaleOwnerSample;
   fixtureOneXIo.push_back({{"leaf","boundary-consumed-by-canonical-owner"},{"clock",startProtectionClock},{"deadline",fixtureOneXDeadline},
    {"requestedEpoch",requestedEpoch},{"boundaryBeforeRead",boundaryBefore},{"boundaryAfterFirstRead",boundaryAfterRead},
    {"pendingAfterFirstRead",pendingAfterRead},{"topologyEpoch",g_xinputTopologyEpoch.load()},{"actualSlotPolls",actualPolls},
    {"sampleSequence",sample.readAttemptSequence},{"sampleTopology",sample.readTopologyGeneration},
    {"sampleLive",sample.live},{"sampleContextStable",sample.readContextStable},{"sampleKnown",inputCaptureBindStandardInput(&sample).known()},
    {"sampleCompleted",sample.readCompletedTick},{"hostLockOwned",false},{"scopedWriteInherited",false},
    {"prepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost}});
  }
  fixtureOneXIo.push_back({{"leaf","yield-to-canonical-owner"},{"hostLockOwned",false},
   {"scopedWriteInherited",false},{"ownerReads",fixtureOneXOwnerReads}});
  if(fixtureOneXOwnerReads>8&&fixtureOneXShape!="returned-stale"){const auto delta=fixtureOneXDeadline>startProtectionClock?fixtureOneXDeadline-startProtectionClock:0;
   startProtectionClock+=delta;fixturePowerClockOffset+=delta;}
  return;
 }
 fixtureOneXRealSleep(millis);
}
static const std::string& fixtureOneXRoot(){
 static const std::string value="USB\\VID_045E&PID_028E\\ONEX-INTERNAL";
 static const std::string ownedB="USB\\VID_045E&PID_028E\\ONEX-OWNED-B";
 return fixtureOneXInitialForeign?ownedB:value;
}
static HidHidePhysicalIdentity fixtureOneXSource(){
 HidHidePhysicalIdentity source;source.resolved=true;source.vendorId=0x045e;source.productId=0x028e;
 source.usagePage=1;source.usage=5;source.baseContainerDeviceInstanceId=fixtureOneXRoot();
 source.deviceInstanceId=fixtureOneXInitialForeign?"HID\\VID_045E&PID_028E&IG_00\\ONEX-OWNED-B":"HID\\VID_045E&PID_028E&IG_00\\ONEX-GAME";
 if(fixtureFullPipe&&fixtureOneXReturned)source.deviceInstanceId+="-RETURNED";
 source.sourcePath=source.deviceInstanceId;source.physicalIdentity=source.deviceInstanceId+"|"+source.baseContainerDeviceInstanceId;
 return source;
}
static HidHidePhysicalIdentity fixtureOneXStaleSelectedChild(const HidHidePhysicalIdentity& source){
 auto selected=source;const auto collection=selected.deviceInstanceId.find("&IG_00");
 if(collection==std::string::npos)throw std::logic_error("OneX selected-child fixture requires its original IG_00 descriptor");
 selected.deviceInstanceId.replace(collection,6,"&IG_01");selected.sourcePath=selected.deviceInstanceId;
 selected.physicalIdentity=selected.deviceInstanceId+"|"+selected.baseContainerDeviceInstanceId;
 return selected;
}
static json fixtureOneXIdentityObservation(const HidHidePhysicalIdentity& source){
 return {{"resolved",source.resolved},{"root",source.baseContainerDeviceInstanceId},{"hid",source.deviceInstanceId},
  {"identity",source.physicalIdentity},{"sourcePath",source.sourcePath},{"interface",source.baseContainerDevicePath},
  {"vendorId",source.vendorId},{"productId",source.productId},
  {"usagePage",source.usagePage},{"usage",source.usage}};
}
static ymcc::targetReturn::physicalPath::Result fixtureOneXVerifyPhysicalPath(const std::string& root,const std::string& target){
 // Exact private CM node inventory only. Execute the original ancestry verifier;
 // no fabricated ownership/return/source verdict and no access to real devices.
 struct Reader {
  std::wstring root,child;
  CONFIGRET locate(PDEVINST node,DEVINSTID_W id,ULONG){
   if(!id){*node=3;return CR_SUCCESS;}
   if(root==id){*node=1;return CR_SUCCESS;}if(child==id){*node=2;return CR_SUCCESS;}
   return CR_NO_SUCH_DEVNODE;
  }
  CONFIGRET deviceId(DEVINST node,PWCHAR id,ULONG count,ULONG){
   const std::wstring tree=L"HTREE\\ROOT\\ONEX-FIXTURE";
   const auto* text=node==1?&root:node==2?&child:node==3?&tree:nullptr;
   if(!text)return CR_NO_SUCH_DEVNODE;if(count<=text->size())return CR_BUFFER_SMALL;
   std::copy(text->begin(),text->end(),id);id[text->size()]=0;return CR_SUCCESS;
  }
  CONFIGRET parent(PDEVINST out,DEVINST node,ULONG){if(node==2){*out=1;return CR_SUCCESS;}
   if(node==1){*out=3;return CR_SUCCESS;}return CR_NO_SUCH_DEVNODE;}
 } reader{U2W(fixtureOneXRoot()),U2W(fixtureOneXSource().deviceInstanceId)};
 const auto proof=ymcc::targetReturn::physicalPath::verify(root,target,reader);
 fixtureOneXIo.push_back({{"leaf","exact-private-CM-ancestry"},{"root",root},{"target",target},
  {"state",ymcc::targetReturn::physicalPath::stateName(proof.state)}});
 return proof;
}
static std::vector<std::wstring> fixtureOneXLocations(const std::string& root){
 ++fixtureOneXLocationsReads;
 if(root!=fixtureOneXRoot() || fixtureOneXShape=="unknown-root")return {};
 const bool changed=fixtureOneXShape=="locations-changed"&&fixtureOneXLocationsReads>=3;
 return {changed?L"PCIROOT(0)#USBROOT(0)#USB(9)":L"PCIROOT(0)#USBROOT(0)#USB(4)"};
}
static int fixtureOneXPortKind(const std::string& root){
 if(root!=fixtureOneXRoot() || fixtureOneXShape=="unknown-port")return -1;
 return fixtureOneXShape=="external"?1:0;
}
static bool fixtureOneXSourceSlot(const HidHidePhysicalIdentity& source,unsigned& slot){
 slot=4;const bool bound=!(fixtureOneXShape=="unbound-slot"&&!fixtureOneXInitialForeign) &&
  source.baseContainerDeviceInstanceId==fixtureOneXRoot() && source.physicalIdentity==fixtureOneXSource().physicalIdentity;
 if(bound)slot=fixtureOneXInitialForeign?fixtureOneXForeignSlot:0;
 if(fixtureOneXShape=="unbound-slot")fixtureOneXIo.push_back({{"leaf","reverse-binding"},
  {"stage",t_inputHostOneXStartWrite&&t_inputHostOneXStartWrite->slot<4?"pre-dispatch-root-slot-proof":"initial-root-slot-proof"},
  {"root",source.baseContainerDeviceInstanceId},{"hid",source.deviceInstanceId},{"physicalIdentity",source.physicalIdentity},
  {"bound",bound},{"slot",slot}});
 return bound;
}
static bool fixtureOneXRootStarted(const std::string& root){
 if(fixtureOneXFreshMode()&&root==fixtureOneXRoot())return true;
 fixtureOneXIo.push_back({{"leaf","root-started"},{"root",root},{"started",false},{"deadline",fixtureOneXDeadline}});
 if(startProtectionClock<fixtureOneXDeadline){const auto delta=fixtureOneXDeadline-startProtectionClock;
  startProtectionClock+=delta;fixturePowerClockOffset+=delta;}
 return false;
}
// This separate OS leaf models the NEW pre-collapse current-child inventory.
// It must not route through fixtureOneXResolveSources or publish raw selection.
static bool fixtureOneXResolveSelectedChildren(std::vector<HidHidePhysicalIdentity>& out,bool* complete){
 const auto source=fixtureOneXSource();out={source};
 if(fixtureOneXSelectedInventoryVariant=="same-root-two-live-children")out.push_back(fixtureOneXStaleSelectedChild(source));
 const bool inventoryComplete=fixtureOneXSelectedInventoryVariant!="same-root-incomplete-inventory";
 if(complete)*complete=inventoryComplete;
 json children=json::array();for(const auto& child:out)children.push_back(fixtureOneXIdentityObservation(child));
 std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);
 fixtureOneXIo.push_back({{"leaf","selected-child-inventory"},{"wrapper","inputHostOneXResolveSelectedChildren"},
  {"complete",inventoryComplete},{"count",out.size()},{"children",children},
  {"selectedAtLeafReturn",fixtureOneXIdentityObservation(g_hidHidePhysicalIdentity)},{"leafPublishesSelected",false},
  {"topologyEpoch",g_xinputTopologyEpoch.load()},{"identityEpoch",g_xinputSlotIdentityEpoch.load()}});
 return true;
}
static bool fixtureOneXResolveSources(std::vector<HidHidePhysicalIdentity>& out,bool* complete){
 out.clear();if(complete)*complete=false;
 if(!fixtureOneXFreshMode()){fixtureOneXIo.push_back({{"leaf","resolve-sources"},{"complete",false}});return false;}
 const auto source=fixtureOneXSource();
 if(fixtureOneXSelectingRehide){
  ++fixtureOneXRehideSelections;
  fixtureOneXReturnedGuardProven=inputHostOneXReturnedSourceCurrent();
  out={source};
  if(fixtureOneXShape=="rehide-switch-source"){
   out.front().baseContainerDeviceInstanceId="USB\\VID_045E&PID_028E\\ONEX-EXTERNAL-B";
   out.front().deviceInstanceId="HID\\VID_045E&PID_028E&IG_00\\EXTERNAL-B";
   out.front().physicalIdentity=out.front().deviceInstanceId+"|"+out.front().baseContainerDeviceInstanceId;
   fixtureOneXExternalSourceRejected=!inputHostOneXRefreshSourcesCurrent(out);
   const auto sequence=g_inputHostSequence;
   fixtureOneXExternalTargetRejected=!inputHostHidHideApplyViaPipeLocked(L"hide",U2W(out.front().deviceInstanceId))&&g_inputHostSequence==sequence;
  }
  fixtureOneXIo.push_back({{"leaf","rehide-source-selection"},{"root",out.front().baseContainerDeviceInstanceId},
   {"returnedSourceCurrentBeforeSelection",fixtureOneXReturnedGuardProven},
   {"externalSourceRejected",fixtureOneXExternalSourceRejected},{"externalTargetRejectedBeforeCommand",fixtureOneXExternalTargetRejected}});
 }else{
  // Selected-alignment parameters must expose the production stale selection:
  // an inventory query returns the current child, never publishes that child.
  if(!fixtureOneXReturned&&fixtureOneXSelectedVariant.empty()&&fixtureOneXSelectedInventoryVariant.empty()){
   std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity=source;
  }
  out={source};
 }
 if(complete)*complete=true;return true;
}
static bool fixtureOneXReadPhysical(std::vector<HidHidePhysicalIdentity>& out){
 struct Selection {bool previous=fixtureOneXSelectingRehide;Selection(){fixtureOneXSelectingRehide=t_inputHostOneXStartWrite&&t_inputHostOneXStartWrite->refreshing;}~Selection(){fixtureOneXSelectingRehide=previous;}} selection;
 bool complete=false;return inputHostOneXResolveSources(out,&complete)&&complete;
}
static bool fixtureOneXVisibility(HidHideVisibilitySnapshot& snapshot){
 ++fixtureOneXVisibilityReads;snapshot={};snapshot.valid=true;snapshot.cloaking=fixtureOneXShape!="cloak-off";
 snapshot.inverseRaw=fixtureOneXShape=="inverse-unknown"?"":"--inv-off";
 if(fixtureOneXShape!="app-missing")snapshot.applications={hidHideCurrentModulePath()};
 const auto& journal=g_hidHideVisibilityJournal;
 snapshot.hidden=journal.hiddenByUs;
 if(snapshot.hidden.empty())snapshot.hidden={U2W(journal.physical.deviceInstanceId),U2W(journal.physical.baseContainerDeviceInstanceId)};
 if(fixtureOneXShape=="root-not-hidden")snapshot.hidden={U2W(journal.physical.deviceInstanceId)};
 if(fixtureOneXShape=="rehide-context-changed"&&fixtureOneXVisibilityReads==3){
  fixtureOneXReturnedGuardProven=inputHostOneXReturnedSourceCurrent();g_virtualStartProtection.cancel(startProtectionClock);
 }
 return fixtureOneXShape!="configuration-unknown";
}
static bool fixtureOneXHideSeam(bool success,const std::string& transactionId,bool){
 if(!success)return false;
 const auto source=fixtureOneXSource();auto& journal=g_hidHideVisibilityJournal;journal=HidHideVisibilityJournal{};
 journal.active=true;journal.transactionId=transactionId;journal.physical=source;journal.physicalSources={source};
 journal.before.valid=true;journal.before.cloaking=false;journal.before.inverseRaw="--inv-off";
 journal.hiddenByUs={U2W(source.deviceInstanceId),U2W(source.baseContainerDeviceInstanceId)};
 {
  std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);
  if(fixtureOneXSelectedVariant.empty())g_hidHidePhysicalIdentity=source;
  else if(!fixtureOneXSelectedHideObserved){
   // Controlled first-hide OS input only. Preserve the already-live canonical
   // slot/mask; the owned journal is IG_00 while RawInput still names IG_01.
   auto selected=source;
   if(fixtureOneXSelectedVariant=="same-root-current-child-stale-selected"){
    selected=fixtureOneXStaleSelectedChild(source);
   }
   g_hidHidePhysicalIdentity=selected;fixtureOneXSelectedHideObserved=true;
   const auto view=g_virtualStartProtection.view(startProtectionClock);
   fixtureOneXIo.push_back({{"leaf","first-hide-selected-child-input"},{"variant",fixtureOneXSelectedVariant},
    {"selected",fixtureOneXIdentityObservation(selected)},{"journalPhysical",fixtureOneXIdentityObservation(journal.physical)},
    {"requestId",view.requestId},{"originalDeadline",startProtectionClock+view.remainingMs},{"clock",startProtectionClock},
    {"hostEpoch",g_inputHostEpoch},{"runId",g_inputHostRunId},{"power",currentPowerGeneration()},
    {"topologyEpoch",g_xinputTopologyEpoch.load()},{"identityEpoch",g_xinputSlotIdentityEpoch.load()},
    {"sourceLiveAtOsLeaf",fixtureXinputSourceLive},{"sourceLocked",static_cast<bool>(g_xinputPhysicalSourceLocked)},
    {"primarySlot",static_cast<unsigned>(g_xinputPrimarySlot)},{"sourceMask",static_cast<unsigned>(g_xinputPhysicalSourceMask)},
    {"connectedMask",static_cast<unsigned>(xinputNonVirtualConnectedMask())},{"preTargetSlots",static_cast<unsigned>(g_xinputPreTargetSlots)},
    {"prepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost}});
  }
 }
 return true;
}
static void fixtureOneXPipeStage(const char* stage,bool ok,DWORD timeout){
 if(!fixtureOneXEnabled||(fixtureOneXShape!="deadline-read-stage"&&fixtureOneXShape!="returned-stale")||!ok)return;
 const bool cycle=t_inputHostOneXStartWrite!=nullptr;
 const bool hello=!cycle&&(std::strcmp(stage,"connect")==0||fixturePipeBudgetCurrentCommand=="HELLO");
 if(!cycle&&!hello)return;
 const unsigned delta=fixtureOneXShape=="returned-stale"?0:cycle?(std::strcmp(stage,"write")==0?500:std::strcmp(stage,"read")==0?1000:0):
  std::strcmp(stage,"connect")==0?500:250;
 fixtureOneXIo.push_back({{"leaf","actual-pipe-stage"},{"stage",stage},{"command",cycle?"HIDHIDE_CYCLE":"HELLO"},
  {"timeoutMs",timeout},{"clockBefore",startProtectionClock},{"deadline",fixtureOneXDeadline},{"elapsedMs",delta}});
 startProtectionClock+=delta;fixturePowerClockOffset+=delta;
}
static bool fixtureOneXCycle(const std::wstring& root,const char* phase,DWORD budget){
 ++fixtureOneXDispatchCalls;
 if(!fixtureOneXEnabled || !t_inputHostOneXStartWrite || root!=U2W(fixtureOneXRoot()) ||
  !phase || std::strcmp(phase,"onex-start-post-hide")!=0 || !budget || budget>5000)
  throw std::logic_error("unadmitted-OneX-cycle-leaf");
 const auto ticket=t_inputHostOneXStartWrite->ticket;
 if(!fixtureOneXDeadline)fixtureOneXDeadline=inputHostOneXStartDeadline();
 if(fixtureFullPipe){
  // Owned actual managed pipe and original final transport guard. The existing
  // managed fixture's fake HidHide endpoint supplies its SDK-only response.
  if(!fixtureOneXSelectedVariant.empty()){
   const auto* episode=t_inputHostOneXStartWrite;
   std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);
   fixtureOneXIo.push_back({{"leaf","selected-child-before-cycle-transport"},{"variant",fixtureOneXSelectedVariant},
    {"selected",fixtureOneXIdentityObservation(g_hidHidePhysicalIdentity)},
    {"journalPhysical",fixtureOneXIdentityObservation(g_hidHideVisibilityJournal.physical)},
    {"originalSelectedExact",inputHostOneXOriginalSelectedSource(*episode,g_hidHidePhysicalIdentity)},
    {"requestId",ticket},{"episodeDeadline",inputHostOneXStartDeadline()},{"clock",startProtectionClock},
    {"hostEpoch",g_inputHostEpoch},{"runId",g_inputHostRunId},{"power",currentPowerGeneration()},
    {"topologyEpoch",g_xinputTopologyEpoch.load()},{"proofTopology",episode->topology},
    {"identityEpoch",g_xinputSlotIdentityEpoch.load()},{"proofIdentity",episode->identity},
    {"candidateSlot",episode->slot},{"primarySlot",static_cast<unsigned>(g_xinputPrimarySlot)},
    {"sourceLocked",static_cast<bool>(g_xinputPhysicalSourceLocked)},{"sourceMask",static_cast<unsigned>(g_xinputPhysicalSourceMask)},
    {"connectedMask",static_cast<unsigned>(xinputNonVirtualConnectedMask())},
    {"prepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost}});
  }
  json reply;const bool ok=inputHostRequestLocked("HIDHIDE_CYCLE",{{"path",W2U(root)}},"hidhide-cycle",budget,&reply);
  const bool issued=t_inputHostOneXStartWrite->issueReserved;
  if(issued)++fixtureOneXIssues;
  const auto detail=reply.value("detail",std::string{});
  fixtureOneXReturned=ok&&(detail=="true"||detail.rfind("true;",0)==0);
  if(fixtureOneXShape=="returned-stale"&&fixtureOneXReturned)fixtureOneXStaleTraceBegin=g_startEntrySeam.trace.size();
  if(!fixtureOneXBoundaryVariant.empty()&&fixtureOneXReturned)fixtureOneXReadmissionTraceBegin=g_startEntrySeam.trace.size();
  fixtureOneXIo.push_back({{"leaf","cycle-dispatch"},{"requestId",ticket},{"episodeDeadline",inputHostOneXStartDeadline()},
   {"issueReserved",issued},{"managedCommandPath",true},{"ack",reply}});
  return fixtureOneXReturned;
 }
 if(fixtureOneXShape=="ticket-at-write"){
  g_virtualStartProtection.cancel(startProtectionClock);
  const auto cooling=g_virtualStartProtection.view(startProtectionClock).remainingMs;
  startProtectionClock+=cooling;fixturePowerClockOffset+=cooling;
  const auto newer=g_virtualStartProtection.admit("steamdeck",startProtectionClock);
  if(!newer.accepted || newer.requestId==ticket)throw std::logic_error("fixture-new-ticket-not-admitted-after-settle");
 }else if(fixtureOneXShape=="off-at-write"){
  g_realStickTest.virtualTargetEnabled=false;g_realStickTest.virtualTargetPersona="disabled";
 }else if(fixtureOneXShape=="cancel-at-write")g_virtualStartProtection.cancel(startProtectionClock);
 else if(fixtureOneXShape=="power-at-write")++g_powerGeneration;
 else if(fixtureOneXShape=="root-at-write")g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId+="-OTHER";
 else if(fixtureOneXShape=="mask-at-write")g_xinputPhysicalSourceMask=2;
 else if(fixtureOneXShape=="deadline-at-write"){
  const auto delta=fixtureOneXDeadline-startProtectionClock;startProtectionClock+=delta;fixturePowerClockOffset+=delta;
 }
 const bool currentBeforeIssue=inputHostOneXStartCurrent();
 ++fixtureOneXMayIssueCalls;
 const bool admitted=inputHostOneXStartMayIssue();
 if(admitted){++fixtureOneXIssues;++fixtureOneXMayIssueCalls;fixtureOneXReplayAdmitted=inputHostOneXStartMayIssue();}
 fixtureOneXIo.push_back({{"leaf","cycle-dispatch"},{"root",W2U(root)},{"phase",phase},{"budgetMs",budget},
  {"requestId",ticket},{"episodeDeadline",inputHostOneXStartDeadline()},{"admitted",admitted},
  {"issueReserved",t_inputHostOneXStartWrite->issueReserved},{"secondIssueAdmitted",fixtureOneXReplayAdmitted},{"managedCommandPath",false},
  {"stage","final-StartMayIssue-before-dispatch"},{"startCurrentBeforeMayIssue",currentBeforeIssue},
  {"candidateSlot",t_inputHostOneXStartWrite->slot},{"primarySlot",static_cast<unsigned>(g_xinputPrimarySlot)}});
 // Retire this simulated observation only after the real final guard refused.
 // A wrongly admitted foreign candidate still spends the issue and fails below.
 if(!admitted){if((fixtureOneXShape=="mask-at-write"||fixtureOneXInitialForeign)&&startProtectionClock<fixtureOneXDeadline){
   const auto delta=fixtureOneXDeadline-startProtectionClock;startProtectionClock+=delta;fixturePowerClockOffset+=delta;
  }return false;}
 if(fixtureOneXShape=="off-after-issue")g_realStickTest.virtualTargetEnabled=false;
 if(fixtureOneXShape=="deadline-after-issue"){
  const auto delta=fixtureOneXDeadline-startProtectionClock;startProtectionClock+=delta;fixturePowerClockOffset+=delta;
 }
 fixtureOneXReturned=fixtureOneXShape=="source-missing"||fixtureOneXFreshMode();return fixtureOneXReturned;
}

static json fixtureRogDeferredDebtState(){
 std::lock_guard<std::mutex> guard(g_inputHostDeferredPhysicalHideMx);
 const auto& debt=g_inputHostDeferredPhysicalHide;
 return {{"runId",debt.runId},{"hostGen",debt.hostGen},{"powerGen",debt.powerGen},
  {"requestId",debt.requestId},{"deadline",debt.deadline},{"configurationApplied",debt.configurationApplied},
  {"configurationReady",debt.configurationReady},{"configurationPhysicalIdentity",debt.configurationPhysicalIdentity},
  {"configurationRoot",debt.configurationRoot},{"configurationTargets",debt.configurationTargets},
  {"configurationTargetCount",debt.configurationTargets.size()},{"continuationRetired",debt.continuationRetired},
  {"refreshAttempted",debt.refreshAttempted},{"sourcePending",g_inputHostDeferredPhysicalHideSourcePending.load()}};
}
static const std::string& fixtureRogDeferredRoot(){
 static const std::string root="USB\\VID_0B05&PID_1B4C\\FIXTURE_ROG131128";return root;
}
// Fixture state for the existing hide I/O port (or an explicitly retained
// journal setup), not a replacement for the product refresh helper.
static void fixtureRogDeferredSeedJournal(const std::string& transactionId){
 HidHidePhysicalIdentity source;source.resolved=true;source.vendorId=0x0b05;source.productId=0x1b4c;
 source.usagePage=1;source.usage=5;
 source.deviceInstanceId="HID\\VID_0B05&PID_1B4C&MI_02\\FIXTURE_ROG131128_GAMEPAD";
 source.sourcePath=source.deviceInstanceId;source.baseContainerDeviceInstanceId=fixtureRogDeferredRoot();
 source.physicalIdentity=source.deviceInstanceId+"|"+source.baseContainerDeviceInstanceId;
 auto& journal=g_hidHideVisibilityJournal;journal=HidHideVisibilityJournal{};
 journal.active=true;journal.transactionId=transactionId;journal.physical=source;
 journal.physicalSources={source};journal.hiddenByUs={U2W(source.baseContainerDeviceInstanceId),U2W(source.deviceInstanceId)};
 journal.before.valid=true;journal.before.cloaking=false;
}
static bool fixtureRogDeferredHideSeam(bool success,const std::string& transactionId,bool powerCycle){
 if(fixtureRogPartialEnabled && t_inputHostDeferredPhysicalHideWrite){
  fixtureRogDeferredHideRecords.push_back({{"success",fixtureRogPartialStage>0},{"powerCycle",powerCycle},{"transactionId",transactionId},{"clock",startProtectionClock}});
  if(!fixtureRogPartialStage){
   fixtureRogDeferredSeedJournal(transactionId);auto& j=g_hidHideVisibilityJournal;
   j.physical.physicalIdentity=j.physical.deviceInstanceId+"|"+j.physical.baseContainerDeviceInstanceId;j.physicalSources={j.physical};
   fixtureRogPartialStage=1;
   auto callback=std::move(fixtureRogPartialAfterFirst);fixtureRogPartialAfterFirst={};if(callback)callback();
   return false; // explicit half-completed hide OS result, never an ordinary failed-hide journal
  }
  ++fixtureRogPartialStage;
  auto callback=std::move(fixtureRogPartialDuringRetry);fixtureRogPartialDuringRetry={};if(callback)callback();
  return fixtureRogPartialShape!="retry-failed";
 }

 if(fixtureSwitchOwnedHideSeed&&success){fixtureRogDeferredSeedJournal(transactionId);
  // Native-only fixture owns an event, not a real managed process. Supply
  // this explicit environment identity at the successful HELLO/hide boundary;
  // the production capture/publish/epoch/run validators remain unmodified.
  g_inputHostHostInstanceId="fixture-native-event-"+std::to_string(g_inputHostEpoch)+"-"+g_inputHostRunId;
  auto& journal=g_hidHideVisibilityJournal;
  journal.physicalSources={journal.physical};}
 if(!fixtureRogDeferredJournalSeed || !t_inputHostDeferredPhysicalHideWrite)return success;
 fixtureRogDeferredHideRecords.push_back({{"success",success},{"powerCycle",powerCycle},
  {"transactionId",transactionId},{"clock",startProtectionClock}});
 // Failed hides must not invent a journal or an owned root.
 if(success){fixtureRogDeferredSeedJournal(transactionId);
  auto callback=std::move(fixtureRogDeferredAfterHide);fixtureRogDeferredAfterHide={};if(callback)callback();}
 return success;
}
static bool fixtureRogPartialSnapshot(HidHideVisibilitySnapshot& value){
 ++fixtureRogPartialSnapshotReads;
 // Ordinary successful ROG hide uses a complete list too. Partial-hide scenarios
 // keep their original first/second read shape and opt-in fault parameters.
 const bool after=!fixtureRogPartialEnabled || fixtureRogPartialStage>1;
 value={};value.valid=true;value.cloaking=true;value.inverseRaw="--inv-off";
 value.applications={hidHideCurrentModulePath()};
 const auto& j=g_hidHideVisibilityJournal;
 value.hidden={U2W(j.physical.deviceInstanceId)};
 if((after&&fixtureRogPartialShape!="retry-failed") || fixtureRogPartialShape=="already-complete")value.hidden.push_back(U2W(j.physical.baseContainerDeviceInstanceId));
 if(fixtureRogPartialShape=="none-hidden")value.hidden.clear();
 if(fixtureRogPartialShape=="child-only")value.hidden={U2W(j.physical.deviceInstanceId)};
 if(fixtureRogPartialShape=="root-only"&&(!fixtureRogPartialEnabled || !after))value.hidden={U2W(j.physical.baseContainerDeviceInstanceId)};
 if(fixtureRogPartialShape=="cloak-off" || (after&&fixtureRogPartialShape=="after-cloak-off"))value.cloaking=false;
 if(fixtureRogPartialShape=="app-missing" || (after&&fixtureRogPartialShape=="after-app-missing"))value.applications.clear();
 if(fixtureRogPartialShape=="inverse-unknown")value.inverseRaw.clear();
 if(fixtureRogPartialShape=="inverse-on")value.inverseRaw="--inv-on";
 if(after&&fixtureRogPartialShape=="after-still-missing")value.hidden={U2W(j.physical.deviceInstanceId)};
 if(fixtureRogPartialShape=="invalid")value.valid=false;
 const bool known=fixtureRogPartialShape!="unknown" && !(after&&fixtureRogPartialShape=="after-unknown");
 std::vector<std::string> hidden,applications;
 for(const auto& path:value.hidden)hidden.push_back(W2U(path));
 for(const auto& path:value.applications)applications.push_back(W2U(path));
 const auto before=fixtureRogDeferredDebtState();
 if(fixtureRogPartialDuringSnapshot)fixtureRogPartialDuringSnapshot(fixtureRogPartialSnapshotReads);
 fixtureRogDeferredSnapshots.push_back({{"read",fixtureRogPartialSnapshotReads},{"shape",fixtureRogPartialShape},
  {"known",known},{"valid",value.valid},{"cloaking",value.cloaking},{"inverseRaw",value.inverseRaw},
  {"hidden",hidden},{"applications",applications},{"debtBefore",before},{"debtAfter",fixtureRogDeferredDebtState()}});
 return known;
}
static bool fixtureRogDeferredCycle(const std::wstring& root,const char* phase,DWORD budget){
 if(!fixtureRogDeferredCycleEnabled){fixtureCountForbiddenIo("unexpected-physical-cycle");throw std::logic_error("unexpected-physical-cycle");}
 bool admitted=true;
 if(t_inputHostDeferredPhysicalHideRefreshIssueActive){
  const auto before=fixtureRogDeferredDebtState();
  auto beforeIssue=std::move(fixtureRogDeferredBeforeCycleIssue);fixtureRogDeferredBeforeCycleIssue={};
  if(beforeIssue)beforeIssue();
  // Simulate the production final WriteFile guard before actual dispatch.
  // Inactive refresh scopes retain the original cycle I/O leaf behavior.
  admitted=inputHostDeferredPhysicalHideReserveRefreshIssue();
  fixtureRogDeferredIssueGuards.push_back({{"root",W2U(root)},{"phase",phase?phase:""},
   {"admitted",admitted},{"issueActive",t_inputHostDeferredPhysicalHideRefreshIssueActive},
   {"debtBefore",before},{"debtAfter",fixtureRogDeferredDebtState()},
   {"contextReason",t_inputHostDeferredPhysicalHideFirstRefusal?t_inputHostDeferredPhysicalHideFirstRefusal:""}});
  if(!admitted)return false;
 }
 ymStartSeamTrace(std::string("hidhide-cycle:")+(phase?phase:""));
 const auto* debt=t_inputHostDeferredPhysicalHideWrite;
 fixtureRogDeferredCycles.push_back({{"root",W2U(root)},{"phase",phase?phase:""},{"budgetMs",budget},
  {"clockBefore",startProtectionClock},{"result",fixtureRogDeferredCycleResult},
  {"tlsDebt",debt!=nullptr},{"deadline",debt?debt->deadline:0},{"requestId",debt?debt->requestId:0},
  {"issueGuardAdmitted",admitted},{"issueActive",t_inputHostDeferredPhysicalHideRefreshIssueActive},
  {"executionDeadline",g_inputHostSwitchRecoveryStartDeadline},
  {"executionRequestId",g_inputHostSwitchRecoveryStartRequestId},{"executionPower",g_inputHostSwitchRecoveryStartPower}});
 auto callback=std::move(fixtureRogDeferredCycleCallback);fixtureRogDeferredCycleCallback={};if(callback)callback();
 fixtureRogDeferredCycles.back()["clockAfter"]=startProtectionClock;
 fixtureRogDeferredCycles.back()["sourceLockedAfter"]=g_xinputPhysicalSourceLocked;
 fixtureRogDeferredCycles.back()["sourceMaskAfter"]=static_cast<unsigned>(g_xinputConnectedMask & g_xinputPhysicalSourceMask);
 return fixtureRogDeferredCycleResult=="success";
}

static bool fixtureRogInitialDiscovery(std::vector<HidHidePhysicalIdentity>& out,bool* complete){
 ++fixtureRogInitialDiscoveryReads;HidHidePhysicalIdentity source;
 source.resolved=true;source.vendorId=0x0b05;source.productId=0x1b4c;source.usagePage=1;source.usage=5;
 source.deviceInstanceId="HID\\VID_0B05&PID_1B4C&MI_05&IG_00\\INITIAL_GAME";
 source.baseContainerDeviceInstanceId="USB\\VID_0B05&PID_1B4C\\INITIAL_ROOT";
 source.sourcePath=source.deviceInstanceId;source.physicalIdentity="initial-ROG-physical-identity";
 if(fixtureRogInitialShape=="other-PID")source.productId=0xffff;
 if(fixtureRogInitialShape=="empty-identity")source.physicalIdentity.clear();
 if(fixtureRogInitialShape=="virtual")source.deviceInstanceId="ROOT\\VIGEMBUS\\0000";
 out={source};if(fixtureRogInitialShape=="multiple")out.push_back(source);
 if(fixtureRogInitialShape=="empty")out.clear();
 if(complete)*complete=fixtureRogInitialDiscoveryComplete;
 auto callback=std::move(fixtureRogInitialDiscoveryCallback);fixtureRogInitialDiscoveryCallback={};if(callback)callback();
 return !out.empty();
}

static const std::string& fixtureGpdPostHideRoot(){
 static const std::string value="USB\\VID_045E&PID_028E\\GPD-WIN5-ROOT";return value;
}
static const std::string& fixtureGpdPostHideGame(){
 static const std::string value="HID\\VID_045E&PID_028E&IG_00\\GPD-WIN5-GAME";return value;
}
static const std::wstring& fixtureGpdPostHideLocation(){
 static const std::wstring value=L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)";return value;
}
static HidHidePhysicalIdentity fixtureGpdPostHideSource(){
 HidHidePhysicalIdentity value;value.resolved=true;value.vendorId=0x045e;value.productId=0x028e;
 value.usagePage=1;value.usage=5;value.deviceInstanceId=fixtureGpdPostHideGame();
 value.baseContainerDeviceInstanceId=fixtureGpdPostHideRoot();
 value.sourcePath=fixtureGpdPostHideGame();return value;
}
static bool fixtureGpdPostHideSnapshot(HidHideVisibilitySnapshot& snapshot){
 if(!fixtureGpdPostHideApplied)return false;
 snapshot={};snapshot.valid=true;snapshot.cloaking=true;snapshot.inverseRaw="off";
 snapshot.hidden={U2W(fixtureGpdPostHideRoot()),U2W(fixtureGpdPostHideGame())};
 const auto module=hidHideCurrentModulePath();if(!module.empty())snapshot.applications={module};
 return true;
}
static bool fixtureGpdPostHidePhysicalIdentities(std::vector<HidHidePhysicalIdentity>& out){
 if(!fixtureGpdPostHideApplied)return false;out={fixtureGpdPostHideSource()};return true;
}
static bool fixtureGpdPostHideApply(const std::string& transactionId,bool powerCycle){
 if(!fixtureFullPipe || g_machineIdentity.family!=YmccFamily::Gpd)return false;
 const auto source=fixtureGpdPostHideSource();fixtureGpdPostHideApplied=true;
 auto& journal=g_hidHideVisibilityJournal;journal=HidHideVisibilityJournal{};journal.active=true;
 journal.transactionId=transactionId.empty()?"fixture-gpd-post-hide":transactionId;
 journal.physical=source;journal.physicalSources={source};journal.before.valid=true;
 journal.before.cloaking=true;journal.before.inverseRaw="off";
 const auto module=hidHideCurrentModulePath();if(!module.empty())journal.before.applications={module};
 journal.hiddenByUs={U2W(fixtureGpdPostHideRoot()),U2W(fixtureGpdPostHideGame())};
 g_msiForegroundTestIo.locations=[](const std::string& root){
   return root==fixtureGpdPostHideRoot()?std::vector<std::wstring>{fixtureGpdPostHideLocation()}:std::vector<std::wstring>{};
 };
 g_msiNativeForegroundTestIo.snapshot=[](HidHideVisibilitySnapshot& value){return fixtureGpdPostHideSnapshot(value);};
 g_msiNativeForegroundTestIo.candidates=[](){return std::vector<HidHidePhysicalIdentity>{fixtureGpdPostHideSource()};};
 g_msiNativeForegroundTestIo.inventoryComplete=[](){return true;};
 g_msiNativeForegroundTestIo.xusbInventory=[](const HidHidePhysicalIdentity& source){
   MsiNativeForegroundXusbInventory result;result.complete=true;result.reason="fixture-gpd-exact-xusb";
   ymcc::msi::NativeForegroundXusbInterface item;item.activeInterface=true;item.ancestorMatched=true;item.virtualDevice=false;
   item.instanceId=fixtureGpdPostHideRoot();item.ancestorRoot=source.baseContainerDeviceInstanceId;
   result.interfaces={item};return result;
 };
 static unsigned refreshCalls=0,sourceSequence=0;refreshCalls=sourceSequence=0;
 g_msiNativeForegroundTestIo.xusbRefresh=[&](const HidHidePhysicalIdentity& target,const std::wstring& location){
   ++refreshCalls;return target.baseContainerDeviceInstanceId==fixtureGpdPostHideRoot()&&location==fixtureGpdPostHideLocation()?1:2;
 };
 g_msiNativeForegroundTestIo.poll=[&](){return refreshCalls?0:-1;};
 g_msiNativeForegroundTestIo.postHideRootStarted=[](const std::string& root){return root==fixtureGpdPostHideRoot();};
 g_msiNativeForegroundTestIo.postHideSource=[](){
   using namespace ymcc::input::recoveryHealth;Source value;value.sequence=++sourceSequence;
   value.started=value.completed=GetTickCount64();value.power=currentPowerGeneration();
   value.topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
   value.identity=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);
   value.live=true;value.stable=true;value.route=Route::XInput;value.incarnation=1;value.continuity=1;
   if(fixtureGpdFullCycleObserving.load(std::memory_order_acquire)){std::lock_guard<std::mutex> observation(fixtureGpdFullCycleObservationMx);
    fixtureGpdFullCycleSourceReads.push_back({{"sequence",value.sequence},{"started",value.started},{"completed",value.completed},
     {"power",value.power},{"topology",value.topology},{"identity",value.identity},{"live",value.live},{"stable",value.stable},
     {"hello",g_inputHostHello},{"prepared",g_inputHostPrepared}});}
   return value;
 };
 // The real readmit guards/mask-reset path is exercised in the dedicated
 // native regression; this full lifecycle consumer uses only its final
 // standard-admission leaf, while retaining the same post-cycle await.
 g_msiNativeForegroundTestIo.postHideReadmit=[](){return 0;};
 g_msiNativeForegroundTestIo.postHideAdmitSelectedSource=[](){return true;};
 (void)powerCycle;return true;
}
static std::uint64_t fixtureActiveExecutionDeadline(){return g_inputHostSwitchRecoveryStartDeadline;}
static BOOL fixtureOwnedHostExitCode(HANDLE process,LPDWORD exitCode){
 if(!fixtureFullPipe&&ymStartSeamActive()&&process&&process==g_inputHostProcess&&exitCode&&
       WaitForSingleObject(process,0)==WAIT_TIMEOUT){
  *exitCode=STILL_ACTIVE;return TRUE;
 }
 return ::GetExitCodeProcess(process,exitCode);
}
static ymcc::targetReturn::process::Probe fixtureRuntimeRecoveryWriter(const ymcc::targetReturn::ProcessIdentity& owner){
 using namespace ymcc::targetReturn;
 if(!fixtureRuntimeRecoveryOsEnabled || !fixtureRuntimeRecoveryOldHost || !fixtureRuntimeRecoveryOldOwner ||
    !detail::sameProcess(owner,*fixtureRuntimeRecoveryOldOwner)){
  ++forbiddenIo;throw std::logic_error("runtime-return-probe-not-exact-owned-Host");
 }
 ++fixtureRuntimeRecoveryWriterCalls;
 const auto wait=WaitForSingleObject(fixtureRuntimeRecoveryOldHost,0);
 fixtureRuntimeRecoveryOsObservations.push_back({{"leaf","exact-old-Host-exit"},{"pid",owner.pid},
  {"birth",owner.creationTime},{"wait",wait},{"nativeParentDeclaredDead",false}});
 if(wait==WAIT_OBJECT_0)return {process::State::Dead,process::Reason::MatchingExited};
 if(wait==WAIT_TIMEOUT)return {process::State::Alive,process::Reason::MatchingRunning};
 return {process::State::Unknown,process::Reason::WaitFailed,GetLastError()};
}
static ymcc::targetReturn::pnpAbsence::Result fixtureRuntimeRecoveryAbsence(const std::vector<std::string>& ids){
 namespace proof=ymcc::targetReturn::pnpAbsence;
 if(!fixtureRuntimeRecoveryOsEnabled){++forbiddenIo;throw std::logic_error("runtime-PnP-leaf-not-enabled");}
 // The original complete-census parser decides absent/present/unknown. Only
 // SetupAPI/CM results are controlled; no Session/proven/ledger field is changed.
 struct Reader {
  DWORD lastError=ERROR_SUCCESS;unsigned enumerations=0,properties=0,closes=0;
  bool physicalXbox=false;
  HDEVINFO getClassDevs(const GUID*,PCWSTR,HWND,DWORD flags){
   if(flags!=(DIGCF_PRESENT|DIGCF_ALLCLASSES))throw std::logic_error("runtime-PnP-census-flags");
   return reinterpret_cast<HDEVINFO>(std::uintptr_t(0x31));
  }
  BOOL enumerate(HDEVINFO,DWORD index,PSP_DEVINFO_DATA data){
   ++enumerations;
   if(fixtureRuntimeRecoveryCensus=="unknown" && index==1){lastError=ERROR_ACCESS_DENIED;return FALSE;}
   if(index>=2){lastError=ERROR_NO_MORE_ITEMS;return FALSE;}
   data->DevInst=index+1;return TRUE;
  }
  BOOL destroy(HDEVINFO){++closes;return TRUE;}
  CONFIGRET deviceId(DEVINST node,PWCHAR out,ULONG chars,ULONG){
   const auto id=physicalXbox&&node==1?std::wstring(L"USB\\VID_045E&PID_028E\\FIXTURE-SD-OFF-111700"):L"ROOT\\FIXTURE_RUNTIME_NODE\\"+std::to_wstring(node);
   if(id.size()+1>chars)return CR_BUFFER_SMALL;
   std::copy(id.begin(),id.end(),out);out[id.size()]=0;return CR_SUCCESS;
  }
  CONFIGRET hardwareIds(DEVINST node,const DEVPROPKEY*,DEVPROPTYPE* type,PBYTE out,PULONG bytes,ULONG){
   ++properties;*type=DEVPROP_TYPE_STRING_LIST;
   std::wstring value=physicalXbox&&node==1?L"USB\\VID_045E&PID_028E":node==2 && fixtureRuntimeRecoveryCensus=="present"?L"ROOT\\HIDMaestroXUSB":L"PCI\\UNRELATED";
   value.push_back(0);value.push_back(0);const auto size=static_cast<ULONG>(value.size()*sizeof(wchar_t));
   if(*bytes<size){*bytes=size;return CR_BUFFER_SMALL;}
   std::memcpy(out,value.data(),size);*bytes=size;return CR_SUCCESS;
  }
  DWORD error(){return lastError;}
 } reader;
#if defined(YMCC_FIXTURE_SD_OFF_SOURCE_LOST)
 reader.physicalXbox=fixtureSdOffSourceLostActive();
#endif
 ++fixtureRuntimeRecoveryCensusCalls;const auto result=proof::verify(ids,reader);
 fixtureRuntimeRecoveryOsObservations.push_back({{"leaf","complete-runtime-target-census"},
  {"input",fixtureRuntimeRecoveryCensus},{"state",proof::stateName(result.state)},
  {"reason",proof::reasonName(result.reason)},{"enumerations",reader.enumerations},
  {"properties",reader.properties},{"closes",reader.closes},{"physicalXboxNodeIncluded",reader.physicalXbox},{"ledgerMutatedByFixture",false}});
 if(reader.closes!=1)throw std::logic_error("runtime-PnP-owned-set-not-closed");
 return result;
}
static void fixtureRuntimeRecoveryWriterPathBoundary(){
 if(!fixtureRuntimeRecoveryOsEnabled || !fixtureRuntimeRecoveryWriterPathCallback ||
    !fixtureRuntimeRecoveryCensusCalls)return;
 const auto& incident=ymcc::inputHost::runtimeRecovery::state;
 const auto& ledger=ymcc::inputHost::targetReturnRuntime::state;
 if(!incident.pending || !incident.stopInvoked || !ledger.session || !ledger.session->ready())return;
 const auto* record=ledger.session->record();if(!record)return;
 const auto old=[&](const auto& origin){return origin.runId==incident.runId && origin.epoch==incident.hostEpoch;};
 if(std::any_of(record->obligations.begin(),record->obligations.end(),[&](const auto& item){return old(item.origin);}) ||
    std::any_of(ledger.targets.begin(),ledger.targets.end(),[&](const auto& item){return old(item.origin);}) ||
    std::any_of(ledger.proven.begin(),ledger.proven.end(),old))return;
 // Only the last real current()->physicalWritersQuiescent()->writer open
 // boundary after the real Session commit is eligible. Do not fake its result.
 auto callback=std::move(fixtureRuntimeRecoveryWriterPathCallback);fixtureRuntimeRecoveryWriterPathCallback={};
 ++fixtureRuntimeRecoveryWriterPathCallbackCalls;
 const auto power=currentPowerGeneration(),intent=g_inputHostVirtualIntentRevision.load();
 callback();
 fixtureRuntimeRecoveryOsObservations.push_back({{"leaf","runtime-writer-path-after-target-commit"},
  {"calls",fixtureRuntimeRecoveryWriterPathCallbackCalls},{"censusCalls",fixtureRuntimeRecoveryCensusCalls},
  {"oldRunId",incident.runId},{"oldEpoch",incident.hostEpoch},{"oldTargetObligations",0},
  {"powerBefore",power},{"powerAfter",currentPowerGeneration()},
  {"intentBefore",intent},{"intentAfter",g_inputHostVirtualIntentRevision.load()},
  {"quiescentReadAndDecisionReplaced",false}});
}
static unsigned checks=0;static json failures=json::array(),cases=json::array();
// Evidence-only, explicit bounded OFF mode. Each row is flushed before the
// next operation so abnormal private-process exit cannot erase prior checks.
static bool fixturePersistOffCheckpoints=false;
static void fixtureOffCheckpoint(const char* stage,const char* assertion=nullptr,int passed=-1){
 if(!fixturePersistOffCheckpoints)return;
 const auto owner=cases.empty()?std::string("bootstrap"):cases.back().value("name","");
 const json row={{"stage",stage},{"case",owner},{"checkIndex",checks},{"failureCount",failures.size()},{"forbiddenIo",forbiddenIo},{"excludedSteamWallpaperNotifications",fixtureExcludedSteamWallpaperNotifications},
  {"assertion",assertion?json(assertion):json(nullptr)},{"passed",passed<0?json(nullptr):json(passed!=0)},
  {"requestId",g_virtualStartProtection.view(startProtectionClock).requestId},
  {"nativeClock",startProtectionClock},{"hostPresent",g_inputHostProcess!=nullptr},
  {"sourceScope","private-fixture-checkpoint;not-runtime-acceptance"}};
 std::ofstream checkpoint(fspath::path(g_yemanccDataDir)/L"off-checkpoints.jsonl",std::ios::app|std::ios::binary);
 if(!checkpoint)throw std::runtime_error("private OFF checkpoint open failed");
 checkpoint<<row.dump()<<'\n';checkpoint.flush();
 if(!checkpoint)throw std::runtime_error("private OFF checkpoint write failed");
}
// Diagnostics only: preserve exactly one counter increment and the caller's
// original exception/return. A swallowed OS-leaf exception still leaves a reason.
static void fixtureCountForbiddenIo(const char* reason) noexcept {
 ++forbiddenIo;
 try {fixtureOffCheckpoint("forbidden-io",reason,0);}catch(...){}
 try {std::cerr<<"fixture-forbidden-io: "<<(reason?reason:"unknown")<<" count="<<forbiddenIo<<'\n';}catch(...){}
}
static void fixtureObserveWallpaperNotification() noexcept {
 ++fixtureExcludedSteamWallpaperNotifications;
 try {fixtureOffCheckpoint("excluded-steam-wallpaper-postcommit");}catch(...){}
}
static json assertionTrace=json::array();
static void fixtureOffFatalCheckpoint(const char* kind,const char* what) noexcept {
 if(!fixturePersistOffCheckpoints)return;
 try {void* frames[24]{};const auto count=CaptureStackBackTrace(0,24,frames,nullptr);json stack=json::array();
  for(USHORT i=0;i<count;++i)stack.push_back(reinterpret_cast<std::uintptr_t>(frames[i]));
  const json row={{"stage",kind},{"what",what?what:""},{"checkIndex",checks},{"failureCount",failures.size()},
   {"exeBase",reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))},{"frames",stack}};
  std::ofstream out(fspath::path(g_yemanccDataDir)/L"off-fatal-checkpoint.json",std::ios::binary);out<<row.dump(2);out.flush();
 }catch(...){}
}
static void fixtureOffTerminate() noexcept {
 const char* what="no-current-exception";
 try {if(const auto error=std::current_exception())std::rethrow_exception(error);}
 catch(const std::exception& error){fixtureOffFatalCheckpoint("terminate",error.what());std::abort();}
 catch(...){what="non-std-current-exception";}
 fixtureOffFatalCheckpoint("terminate",what);std::abort();
}
static void fixtureOffInvalidParameter(const wchar_t*,const wchar_t*,const wchar_t*,unsigned int,uintptr_t) {
 fixtureOffFatalCheckpoint("invalid-CRT-parameter","invalid parameter reached CRT");
}
static void check(bool good,const char* name){++checks;const auto owner=cases.empty()?"bootstrap":cases.back().value("name","");assertionTrace.push_back({{"case",owner},{"assertion",name},{"passed",good},{"checkIndex",checks}});if(!good)failures.push_back({{"case",owner},{"assertion",name}});fixtureOffCheckpoint("assertion",name,good?1:0);}
static json state(){return {{"hostHandle",g_inputHostProcess!=nullptr},{"prepared",g_inputHostPrepared},{"creationPossible",g_inputHostPrepareMayHaveReachedHost},{"neutralized",g_inputHostNeutralized},{"active",g_inputHostActive},{"transportFault",g_inputHostTransportFault},{"quarantined",g_inputHostReleaseQuarantined},{"startBlocked",g_inputHostStartBlocked},{"journalActive",g_hidHideVisibilityJournal.active},{"routeArmed",g_msiDinputRouteDinputArmed.load()},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},{"personaHandoffActive",g_msiPersonaModeHandoff.active},{"readPaused",g_msiPersonaReadPause.load()}};}
static void fixtureTargetReturnIndependentCase(){
 ymcc::inputHost::targetReturnRuntime::state=ymcc::inputHost::targetReturnRuntime::State{};
 const auto directory=fspath::path(g_yemanccDataDir)/L"target-return-private"/(L"case-"+std::to_wstring(++fixtureTargetReturnCaseSerial));
 fspath::create_directories(directory);fixtureTargetReturnPath=(directory/L"record.json").wstring();
}
static std::wstring fixtureTargetReturnRecordPath(){
 if(fixtureTargetReturnPath.empty())fixtureTargetReturnIndependentCase();
 return fixtureTargetReturnPath;
}
static std::wstring fixtureFullPipeCliRecordPath(){
 if(!fixtureFullPipe && !fixtureOrdinarySwitchCliScope)throw std::logic_error("ordinary-full-pipe-cli-scope-required");
 if(fixtureOrdinarySwitchCliScope && (fixtureFullPipe || fixtureColdArchive || fixtureRuntimeRecoveryOsEnabled))
  throw std::logic_error("ordinary-native-switch-cli-role-conflict");
 const auto record=fspath::path(fixtureTargetReturnRecordPath());
 const auto root=fspath::path(g_yemanccDataDir)/L"target-return-private";
 const auto exact=root/(L"case-"+std::to_wstring(fixtureTargetReturnCaseSerial));
 const auto attrs=GetFileAttributesW(exact.c_str());
 if(!record.is_absolute()||attrs==INVALID_FILE_ATTRIBUTES||!(attrs&FILE_ATTRIBUTE_DIRECTORY)||
    (attrs&FILE_ATTRIBUTE_REPARSE_POINT)||
    normalizeWindowsPathForCompare(record.parent_path().wstring())!=normalizeWindowsPathForCompare(exact.wstring())||
    record.filename().wstring()!=L"record.json")
  throw std::logic_error("ordinary-full-pipe-cli-private-record-identity");
 return record.wstring()+(fixtureOrdinarySwitchCliScope?L".native-switch-cli":L".full-pipe-cli");
}
static std::optional<ymcc::targetReturn::ProcessIdentity> fixtureTargetReturnCaptureOwner(HANDLE process){
 if(fixtureFullPipe||process==GetCurrentProcess())return ymcc::targetReturn::process::capture(process);
 // Native-only existing fixture uses an event as Host. This is explicitly a
 // synthetic process identity, never proof of an OS process or a cold restart.
 if(process&&ymStartSeamActive())return ymcc::targetReturn::ProcessIdentity{400000u+static_cast<DWORD>(fixtureTargetReturnCaseSerial),"132000000000000001"};
 return std::nullopt;
}
// Each seed models a fresh process. Reset audit-only state before resetting the
// synthetic monotonic offset; a preceding simulated wake may be 60s ahead.
// Within-case history, production clocks, work queues and budgets are unchanged.
static void fixtureResetSwitchAudit(){
 std::lock_guard<std::mutex> lock(g_switchAudit.mx);
 auto& audit=g_switchAudit;
 audit.requestedFrom.clear();audit.requestedTo.clear();audit.requestedBound.clear();
 audit.requestedTick=0;audit.requestedEpoch=0;
 audit.enqueuedTick=0;audit.enqueuedCount=0;
 audit.dequeuedTick=0;audit.deferredCount=0;
 audit.dispatchedTick=0;audit.dispatchedPersona.clear();audit.dispatchedProfile.clear();
 audit.requestedAtDequeue.clear();audit.cycleAwaitingOutcome=false;
 audit.throttledCount=0;audit.settleLoggedReason.clear();audit.throttleLoggedWindow=0;
}
static void seed(){
 fixtureRuntimeRecoveryOsEnabled=false;fixtureRuntimeRecoveryFaultEnabled=false;fixtureRuntimeRecoveryFaultConsumed=false;
 fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryCensus="unknown";
 fixtureRuntimeRecoveryFaultCalls=fixtureRuntimeRecoveryWriterCalls=fixtureRuntimeRecoveryCensusCalls=0;
 fixtureRuntimeRecoveryOsObservations=nlohmann::json::array();
 fixtureRuntimeRecoveryWriterPathCallback={};fixtureRuntimeRecoveryWriterPathCallbackCalls=0;
 ymcc::inputHost::runtimeRecovery::state={};g_inputHostRuntimeRecoveryPending=false;
 fixtureOneXEnabled=false;fixtureOneXShape.clear();fixtureOneXLocationsReads=fixtureOneXDispatchCalls=fixtureOneXIssues=0;
 fixtureOneXReplayAdmitted=false;fixtureOneXInitialForeign=false;fixtureOneXForeignSlot=1;fixtureOneXMayIssueCalls=0;
 fixtureOneXStaleClockObserving=false;fixtureOneXStaleTraceBegin=0;fixtureOneXStaleSequence=0;fixtureOneXStaleOwnerSample={};
 fixtureOneXDeadline=0;fixtureOneXIo=nlohmann::json::array();
 fixtureOneXBoundaryVariant.clear();fixtureOneXBoundaryObserving=fixtureOneXLateArrivalApplied=false;
 fixtureOneXSelectedVariant.clear();fixtureOneXSelectedInventoryVariant.clear();fixtureOneXSelectedHideObserved=false;
 fixtureOneXBoundaryDeferrals=0;fixtureOneXReadmissionTraceBegin=0;
 fixtureOneXInitialPending=false;fixtureOneXInitialOwnerReads=0;
 fixtureOneXReturned=fixtureOneXOwnerReading=fixtureOneXSelectingRehide=false;
 fixtureOneXOwnerReads=fixtureOneXVisibilityReads=fixtureOneXRehideSelections=0;
 fixtureOneXReturnedGuardProven=fixtureOneXExternalSourceRejected=fixtureOneXExternalTargetRejected=false;
 inputHostDeferredPhysicalHideClear(); // fresh fixture process; never used as evidence of physical return
 fixtureRogInitialIdentityEnabled=false;fixtureRogInitialDiscoveryComplete=true;fixtureRogInitialDiscoveryReads=0;fixtureRogInitialDiscoveryCallback={};fixtureRogInitialShape="valid";
 fixtureSwitchOwnedHideSeed=false;fixtureSwitchSourceAtEnsure={};fixtureGpdTransferSourceAtEnsure={};
 fixtureRogDeferredJournalSeed=fixtureRogDeferredCycleEnabled=false;fixtureRogDeferredCycleResult="success";
 fixtureRogPartialEnabled=false;fixtureRogPartialShape="valid";fixtureRogPartialStage=fixtureRogPartialSnapshotReads=0;fixtureRogPartialAfterFirst={};fixtureRogPartialDuringRetry={};fixtureRogPartialDuringSnapshot={};
 fixtureRogDeferredHideRecords=json::array();fixtureRogDeferredCycles=json::array();
 fixtureRogDeferredAfterHide={};fixtureRogDeferredCycleCallback={};fixtureRogDeferredBeforeCycleIssue={};
 fixtureRogDeferredIssueGuards=json::array();fixtureRogDeferredSnapshots=json::array();
 fixtureRogDeferredPortKind=0;fixtureRogDeferredPortQueries=json::array();fixtureRogDeferredPortCallback={};
 g_hidHideCycleInFlight=false;g_hidHideCycleBusySinceTick=0;

 fixtureTargetReturnIndependentCase();
 fixtureResetSwitchAudit();
 fixtureExitSweep=false;fixtureExitSweepLists=fixtureExitSweepUnhides=fixtureExitSweepCloakOffs=0;
 fixturePsReturnManualTopology=false;
 fixturePrepareAckLoss=fixturePrepareAckLossConsumed=false;fixturePrepareAckOriginal=nullptr;
 fixturePipeBudgetEnabled=fixturePipeBudgetActive=fixturePipeBudgetAdvanced=false;fixturePipeBudgetStages.clear();fixturePipeBudgetDeadline=fixturePipeBudgetAdvanceMs=0;fixturePipeBudgetAdvancePhase.clear();fixturePipeBudgetCommand="RECREATE_TARGET";fixturePipeBudgetCurrentCommand.clear();
 fixtureHealthInventoryRead=false;fixtureWakeBeforeHideReturn={};fixtureWakeAfterSourceQuery={};fixturePcPowerCapabilities=false;fixtureModernStandbyCapabilities=false;fixturePowerClockOffset=0;fixtureWakeBlockingSleeps=0;
 {std::lock_guard<std::mutex> lock(ymcc::inputHost::wakeExecutionRuntime::mx);ymcc::inputHost::wakeExecutionRuntime::episode={};ymcc::inputHost::wakeExecutionRuntime::returnWait={};ymcc::inputHost::wakeExecutionRuntime::sourceWait={};}
 g_wakeRescanRequested=false;g_wakeRescanGeneration=0;g_wakeRescanArmTick=0;g_wakeHotReplaceRetryAfterTick=0;g_wakeKeptTargetGeneration=0;g_inputHostRecreatedWakePowerProof={};g_msiVirtualWakeGeneration=0;g_msiVirtualWakeArmTick=0;g_msiVirtualWakeStartAttempts=0;g_msiVirtualWakeRearmedGeneration=0;
 g_psEndpointOwnership.clear();g_inputHostPsEndpointFault={};g_inputHostTupleFault={};g_inputHostDeferredTuple={};g_inputHostTupleAccepted={};g_psWakeProtectedRequestId=0;g_psWakeRequestHostEpoch=0;g_psWakeRequestCycleId=0;fixtureWireCapture=false;fixtureWireFaultConsumed=false;fixtureWirePrevious.clear();fixtureWireFault.clear();fixtureWireOriginal=nullptr;fixtureWireInjected=nullptr;
 // Each case is process-equivalent; preserve budgets inside, not across cases.
 g_inputHostRecoveryEpisodeAttempts=0;g_inputHostRecoveryLastStartTick=0;g_inputHostRecoveryDeferredUntil=0;g_inputHostRecoveryPending=false;g_inputHostRecoveryTerminalLogged=false;g_inputHostRecoveryRolling[0]=0;g_inputHostRecoveryRolling[1]=0;g_inputHostRecoveryRollingNext=0;g_inputHostRecoveryIntentReset=false;g_inputHostHealthySinceTick=0;g_inputHostRecoveryHealth.clear();g_inputHostRecoveryHealthWasTracking=false;g_inputHostRecoveryFallbackState=0;g_inputHostTupleHealPending=false;
 ymcc::inputHost::switchExecutionReturnRuntime::state={};g_inputHostSwitchExecutionReturnPending=false;
 g_virtualStartProtection.~VirtualStartProtection();new (&g_virtualStartProtection) ymcc::VirtualStartProtection();startProtectionClock=100000;g_inputHostStartProtectionBoundTicket=0;g_inputHostSwitchVerificationPending=false;ymcc::inputHost::switchVerificationRuntime::lastFinalRequestId=0;ymcc::inputHost::switchVerificationRuntime::observation={};
 // A seed is a new process-equivalent case. Production intentionally retains
 // ROG return lineage across Stop/wake; independent cases must not inherit it.
 // Never call this reset from a within-case return or recovery helper.
 ymcc::inputHost::switchVerificationRuntime::acceptedSource={};
 ymcc::inputHost::switchVerificationRuntime::rogReturnBaseline={};
 ymcc::inputHost::switchVerificationRuntime::rogOffSuccessReceipt={};
 ymcc::inputHost::switchVerificationRuntime::rogOffSignal={};
g_inputHostSwitchRecovery={};g_inputHostSwitchRecoveryPending=false;g_inputHostSwitchRecoveryStartDeadline=0;g_inputHostSwitchRecoveryStartRequestId=0;g_inputHostSwitchRecoveryStartPower=0;g_inputHostSwitchRecoveryTransportError=ERROR_SUCCESS;fixtureStartupTransportError=ERROR_SUCCESS;fixtureFrameFirstAck=false;
 g_machineIdentity={};g_machineIdentity.attempted=true;g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
 g_startEntrySeam=YmccStartEntrySeam{};g_startEntrySeam.active=true;g_startEntrySeam.modeWriteOk=true;g_startEntrySeam.confirmRouteAfterEnsure=true;
 g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
 // Independent process-equivalent initial state, never used as recovery proof.
 g_inputHostFreshNeutralAdmitted=false;g_inputHostFreshNeutralFrames=0;g_inputHostFreshNeutralMask=0;
 g_psWakeRearmRequested=false;g_psWakeRearmGeneration=0;g_psWakeRearmArmTick=0;g_psWakeRearmRetryAfterTick=0;g_psWakeRearmAttempts=0;g_psWakeRearmAttemptGeneration=0;g_psWakeRebuiltGeneration=0;g_psWakeExhaustedGeneration=0;
 g_msiM12RemapDone=true;g_hidHideLastHealTick=GetTickCount64();g_inputHostExitFence=false;g_exitRequested=false;g_inputHostReleaseQuarantined=false;g_inputHostStartBlocked=false;g_inputHostFaultHoldsStart=false;g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=g_inputHostHello=g_inputHostActive=g_inputHostTransportFault=g_inputHostPipeRetired=false;g_inputHostPrepareMayHaveReachedHost=false;
 g_hidHideVisibilityJournal.active=false;g_wakeHoldPhysicalUnhide=false;g_inputHostBoundPersona.clear();g_inputHostBuildPersona.clear();g_msiSteamDeckReleaseFence.clear();
 g_xinputConnectedMask=g_xinputVirtualEndpointMask=g_xinputSlotUnknownMask=g_xinputPhysicalSourceMask=0;g_xinputPhysicalSourceLocked=false;g_msiDinputRouteDinputArmed=false;g_msiDinputRouteConfirmed=false;g_msiDinputAdmissionLogged=false;
 g_msiDinput.enabled=true;g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=kMsiClawCapability.pidDInput;g_msiDinputSourceValid=true;
 g_msiControllerTraceInventoryForTest=[](){return json{{"known",false},{"scope","offline"}};};g_msiControllerOfficialStackForTest=[](){return json{{"scope","offline"},{"modeWriterProven",false}};};
 throwLogEvent=nullptr;fixtureEvents.clear();
}
static bool traced(const std::string& value){return std::find(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),value)!=g_startEntrySeam.trace.end();}
static json cleanup(){g_startEntrySeam.pipeThrowCommand.clear();g_startEntrySeam.pipeFailCommand.clear();inputHostStop("offline-owned-cleanup");const auto result=state();
 // An unresolved fake endpoint is retained, then the fixture supplies its
 // successful return observation and retries the real stop for its own objects.
 if(g_inputHostReleaseQuarantined){g_startEntrySeam.msiSdReleaseAbsent=true;g_startEntrySeam.msiSdProofSucceeded=true;g_startEntrySeam.hostExitWait=WAIT_OBJECT_0;inputHostStop("offline-owned-cleanup-retry");}
 return result;}
static void rollbackCases(){
 for(const char* command:{"HELLO","PREPARE_TARGET","SUBMIT_NEUTRAL"}){
  seed();cases.push_back({{"name",std::string("exception-at-")+command}});const auto f=failures.size();g_startEntrySeam.pipeThrowCommand=command;bool threw=false;try{(void)inputHostStart();}catch(const std::exception&){threw=true;}
  const auto after=state();check(threw,"configured exception reaches real start path");
  cases.back()["stateAfterException"]=after;cases.back()["trace"]=g_startEntrySeam.trace;
  const auto final=cleanup();check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostNeutralized,"real stop clears owned local start state after exception");check(!g_msiPersonaModeHandoff.active&&!g_msiPersonaReadPause.load(),"exception does not leave MSI handoff pause latched");cases.back()["cleanupFirstResult"]=final;cases.back()["passed"]=failures.size()==f;
 }
 seed();cases.push_back({{"name","normal-start-stop-and-repeat"}});{const auto f=failures.size();for(int i=0;i<3;++i){check(inputHostStart(),"real complete start returns success");check(g_inputHostPrepared&&g_inputHostNeutralized,"real start publishes prepared and neutralized");inputHostStop("virtual-target-disabled");check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostNeutralized,"real OFF stop completes local release");}cases.back()["trace"]=g_startEntrySeam.trace;cases.back()["passed"]=failures.size()==f;}
}
// Observe the existing queue item's reason without copying its ownership payload.
// The same fixture can also be paired with a frozen pre-struct string queue.
static std::string_view fixtureLifecycleWorkReason(const auto& work) noexcept {return std::string_view{work};}
static void queuedExceptionCase(bool common=false){
 seed();if(common){g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="ROG Ally X";g_msiDinput.enabled=false;g_msiDinput.mainInterfaceMatched=false;g_msiDinput.productId=0;g_msiDinputSourceValid=false;}g_startupInputState=ymcc::StartupInputState::Ready;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=false;}
 cases.push_back({{"name",common?"common-ready-partial-start-exception":"msi-ready-partial-start-exception"}});const auto before=failures.size();g_startEntrySeam.pipeThrowCommand="HELLO";
 check(inputHostQueueLifecycleWork("persona-transition"),"real user transition request queued");inputHostRunQueuedLifecycleWork();
 cases.back()["afterFailure"]=state();cases.back()["startupStateAfterFailure"]=static_cast<int>(g_startupInputState.load());cases.back()["traceAfterFailure"]=g_startEntrySeam.trace;
 bool cleanupQueued=false;{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);cases.back()["queueAfterFailure"]=g_inputHostLifecycleWorkQueue;for(const auto& work:g_inputHostLifecycleWorkQueue)if(fixtureLifecycleWorkReason(work).rfind("fault-stop:",0)==0)cleanupQueued=true;}
 check(cleanupQueued||(!g_inputHostProcess&&!g_inputHostPrepared&&!g_hidHideVisibilityJournal.active&&!g_msiDinputRouteDinputArmed.load()),"partially-owned start exception retains or completes cleanup outside startup Waiting state");check(!g_inputHostLifecycleWorkInFlight,"failed whole dispatcher releases serial work claim");
 g_startEntrySeam.pipeThrowCommand.clear();inputHostRunQueuedLifecycleWork();cases.back()["afterNextOwnerPass"]=state();cases.back()["traceAfterNextOwnerPass"]=g_startEntrySeam.trace;
 check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostNeutralized,"next real owner pass completes partial-start cleanup");
 cases.back()["passed"]=failures.size()==before;cases.back()["manualFixtureTeardown"]=cleanup();
}
static void healthyDiagnosticExceptionCase(){
 seed();g_startupInputState=ymcc::StartupInputState::Ready;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=false;}
 cases.push_back({{"name","completed-target-survives-final-diagnostic-exception"}});const auto before=failures.size();throwLogEvent="input-host-started";check(inputHostQueueLifecycleWork("persona-transition"),"real healthy-start request queued");inputHostRunQueuedLifecycleWork();
 cases.back()["afterFailure"]=state();bool faultStop=false;{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);cases.back()["queueAfterFailure"]=g_inputHostLifecycleWorkQueue;for(const auto& work:g_inputHostLifecycleWorkQueue)if(fixtureLifecycleWorkReason(work).rfind("fault-stop:",0)==0)faultStop=true;}
 check(g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&!g_inputHostReleaseQuarantined,"accepted prepared and neutralized target retained after optional final log failure");check(!faultStop,"healthy target does not receive exception cleanup request");check(!g_inputHostLifecycleWorkInFlight,"diagnostic exception releases owner claim");cases.back()["trace"]=g_startEntrySeam.trace;cases.back()["passed"]=failures.size()==before;cases.back()["manualFixtureTeardown"]=cleanup();
}
static json targetDocument(const std::string& persona,int revision){return json{{"schemaVersion",1},{"input",{{"revision",revision},{"outputTarget",{{"persona",persona},{"buttonMappingEnabled",persona!="disabled"},{"gyroEnabled",false}}}}}};}
static json writeTarget(const json& before,const json& after){return g_cmds.at("settings.write")({{"path",W2U(YM_SETTINGS_FILE)},{"content",after.dump()},{"baseline",before},{"expectedInputRevision",before["input"]["revision"]}});}
static void startProtectionCases(){
 seed();g_realStickTest.enabled=false;g_inputShortcutRuntime.clear();reg_fs();
 cases.push_back({{"name","actual-settings-IPC-admission-and-burst"}});
 check(nativeLifecycleEventIsBase("virtual-start-protection"),"request protection survives detailed log gate OFF");check(nativeLifecycleEventIsBase("virtual-start-protection-state"),"protection terminal/unlock survives detailed log gate OFF");
 auto base=targetDocument("disabled",0);check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump()),"private fixture settings initialized");
 auto on=targetDocument("elite",1);auto accepted=writeTarget(base,on);check(accepted.value("ok",false),"first actual settings IPC request accepted");
 const auto id=g_virtualStartProtection.view(startProtectionClock).requestId;check(id&&g_virtualStartProtection.view(startProtectionClock).locked,"actual durable request locks before consumer runs");
 const auto saved=ymSettingsReadRaw(YM_SETTINGS_FILE),backup=ymSettingsReadRaw(YM_SETTINGS_BACKUP_FILE);const auto queueSize=g_inputHostLifecycleWorkQueue.size();
 bool allRejected=true;for(int i=0;i<1000;++i){const auto next=targetDocument(i%3==0?"disabled":i%3==1?"steamdeck":"dualsense-edge",2);const auto result=writeTarget(on,next);allRejected=allRejected&&!result.value("ok",true)&&result.value("reason","")=="virtual-start-protection-active";}
 check(allRejected,"1000 actual mixed OFF ON persona settings requests rejected with explicit busy receipt");
 check(saved==ymSettingsReadRaw(YM_SETTINGS_FILE)&&backup==ymSettingsReadRaw(YM_SETTINGS_BACKUP_FILE),"rejected burst changes neither settings nor backup");check(queueSize==g_inputHostLifecycleWorkQueue.size(),"rejected burst adds no lifecycle jobs");
 auto unrelated=on;unrelated["input"]["revision"]=2;unrelated["input"]["diagnostics"]={{"inputLoggingEnabled",true}};check(writeTarget(on,unrelated).value("ok",false),"unrelated settings write remains accepted during startup protection");check(g_virtualStartProtection.view(startProtectionClock).requestId==id,"unrelated write does not reset protection timer");
 auto off=targetDocument("disabled",3);check(!writeTarget(unrelated,off).value("ok",true),"OFF is refused while the current player intent is pending");check(g_virtualStartProtection.view(startProtectionClock).phase=="pending","refused OFF does not cancel or settle the original intent");
 g_virtualStartProtection.finish(id,"success",startProtectionClock);startProtectionClock+=9999;check(!writeTarget(unrelated,off).value("ok",true),"actual OFF remains closed at terminal plus9999");++startProtectionClock;
 check(writeTarget(unrelated,off).value("ok",false),"actual OFF accepted only after terminal plus10000");const auto closing=g_virtualStartProtection.view(startProtectionClock).requestId;
 check(closing!=id&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","OFF owns a distinct pending transaction until actual stop finishes");
 auto next=targetDocument("steamdeck",4);check(!writeTarget(off,next).value("ok",true),"ON cannot bypass a pending OFF transaction");
 g_realStickTest.enabled=true;inputCaptureRefreshRealStickSettings(true);check(!g_realStickTest.virtualTargetEnabled&&inputHostConfiguredPersona()=="disabled","actual OFF consumer applies the admitted physical target");
 check(g_inputHostLifecycleWorkRequested.load(),"OFF with no Host still requests serialized cleanup for a real terminal receipt");inputHostStop("virtual-target-disabled");
 check(g_virtualStartProtection.view(startProtectionClock).phase=="verifying"&&g_virtualStartProtection.view(startProtectionClock).candidateOutcome=="success","actual no-Host stop reports only a candidate before required verification");
 {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;}
 startProtectionClock+=9999;check(!writeTarget(off,next).value("ok",true),"player reopen remains closed at OFF completion plus9999");++startProtectionClock;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(writeTarget(off,next).value("ok",false),"player reopen accepted at OFF completion plus10000");
 const auto newer=g_virtualStartProtection.view(startProtectionClock).requestId;inputHostStartProtectionFinish(id,"success","offline-old-receipt");check(g_virtualStartProtection.view(startProtectionClock).requestId==newer&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","old completion cannot settle newer actual request");
 startProtectionClock+=29999;auto timeoutNext=targetDocument("elite",5);check(!writeTarget(next,timeoutNext).value("ok",true),"actual IPC rejects at admission plus29999");++startProtectionClock;check(writeTarget(next,timeoutNext).value("ok",false),"actual IPC permits new request at maximum30000 without completing old one");
 g_realStickTest.enabled=false;
 cases.back()["events"]=fixtureEvents;
 // A pending request may not be bypassed by an actual runtime shortcut or clear.
 cases.push_back({{"name","actual-shortcut-and-clear-admission"}});g_inputShortcutRuntime.clear();auto disabled=targetDocument("disabled",6);check(sgWriteFileAtomic(YM_SETTINGS_FILE,disabled.dump()),"private shortcut base seeded");nativeInputShortcutToggle("input.virtualGamepadToggle","elite",nullptr);check(!g_inputShortcutRuntime.active(),"runtime shortcut cannot enable while another request locked");
 const auto input=disabled["input"];auto temp=g_inputShortcutRuntime.toggle(input,gameInputOwnerSession(),"input.virtualGamepadToggle","elite");check(temp.ok,"temporary shortcut fixture seeded");nativeInputShortcutToggle("input.virtualGamepadToggle","elite",nullptr);check(inputHostTargetKey(g_inputShortcutRuntime.effective(input,gameInputOwnerSession()))=="elite","runtime shortcut OFF is refused while locked");
 auto enabledBase=targetDocument("elite",7);check(sgWriteFileAtomic(YM_SETTINGS_FILE,enabledBase.dump()),"private clear base seeded");g_inputShortcutRuntime.clear();temp=g_inputShortcutRuntime.toggle(enabledBase["input"],gameInputOwnerSession(),"input.virtualGamepadToggle","elite");check(temp.ok&&!temp.enabled,"OFF overlay seeded");check(!clearInputShortcutRuntime(),"clear rejects a hidden ON transition during protection");check(g_inputShortcutRuntime.active(),"rejected clear preserves its runtime overlay");g_inputShortcutRuntime.clear();
 // Execute the actual registered endpoint; the gamepad feed is not involved.
 cases.push_back({{"name","actual-clear-ACK-protection-observation"}});seed();g_realStickTest.enabled=false;reg_shell_app();
 auto ackBase=targetDocument("elite",1);check(sgWriteFileAtomic(YM_SETTINGS_FILE,ackBase.dump()),"private clear ACK base seeded");
 const auto ackTicket=g_virtualStartProtection.admit("elite",startProtectionClock).requestId;
 auto wallMs=[](){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();};
 const auto wallBefore=wallMs();const auto producer=inputHostStartProtectionSnapshot();const auto wallAfter=wallMs();
 const auto expiry=producer.value("expiresAtUnixMs",0ULL);
 check(producer.value("locked",false)&&producer.value("requestId",0ULL)==ackTicket,"actual snapshot identifies the active admission");
 check(expiry>=wallBefore+30000&&expiry<=wallAfter+30000,"actual producer exports exact thirty-second absolute expiry");
 for(const auto& legacyArgs:std::vector<json>{json::object(),nullptr,"legacy",{{"observeProtection",false}},{{"observeProtection","true"}}}){
  const auto legacy=g_cmds.at("input.shortcutRuntime.clear")(legacyArgs);
  check(legacy.is_boolean()&&legacy.get<bool>(),"legacy or malformed opt-in retains successful bool clear ACK");
 }
 const auto observed=g_cmds.at("input.shortcutRuntime.clear")({{"observeProtection",true}});
 check(observed.is_object()&&observed.value("ok",false)&&observed["startProtection"].value("locked",false),"successful opted-in clear ACK carries protection without state feed");
 check(observed["startProtection"].value("requestId",0ULL)==ackTicket&&g_virtualStartProtection.view(startProtectionClock).requestId==ackTicket,"successful same-target clear ACK neither renews nor replaces admission");
 startProtectionClock+=28000;g_virtualStartProtection.finish(ackTicket,"success",startProtectionClock);
 const auto terminalBefore=wallMs();const auto terminal=g_cmds.at("input.shortcutRuntime.clear")({{"observeProtection",true}});const auto terminalAfter=wallMs();
 const auto terminalExpiry=terminal["startProtection"].value("expiresAtUnixMs",0ULL);
 check(terminal["startProtection"].value("phase","")=="success"&&terminalExpiry>=terminalBefore+2000&&terminalExpiry<=terminalAfter+2000,"terminal ACK expires at original thirty-second cap before ten-second verification");
 startProtectionClock+=2000;const auto unlocked=g_cmds.at("input.shortcutRuntime.clear")({{"observeProtection",true}});
 check(unlocked.value("ok",false)&&!unlocked["startProtection"].value("locked",true)&&unlocked["startProtection"].value("expiresAtUnixMs",1ULL)==0,"released clear ACK has stable zero expiry");
 seed();g_realStickTest.enabled=false;auto refusedBase=targetDocument("elite",2);check(sgWriteFileAtomic(YM_SETTINGS_FILE,refusedBase.dump()),"private refused clear base seeded");
 const auto refusedTicket=g_virtualStartProtection.admit("elite",startProtectionClock).requestId;const auto overlay=g_inputShortcutRuntime.toggle(refusedBase["input"],gameInputOwnerSession(),"input.virtualGamepadToggle","elite");check(overlay.ok&&!overlay.enabled,"refused clear ACK OFF overlay seeded");
 const auto refused=g_cmds.at("input.shortcutRuntime.clear")({{"observeProtection",true}});
 check(!refused.value("ok",true)&&refused.value("reason","")=="virtual-start-protection-active"&&refused["startProtection"].value("requestId",0ULL)==refusedTicket&&refused["startProtection"].value("expiresAtUnixMs",0ULL)>0,"refusal retains explicit reason and original active protection");
 check(g_inputShortcutRuntime.active(),"refused observed clear never changes runtime overlay");g_inputShortcutRuntime.clear();
 cases.push_back({{"name","actual-partial-start-failure-settles-protection"}});seed();const auto ticket=g_virtualStartProtection.admit("steamdeck",startProtectionClock);g_startEntrySeam.pipeThrowCommand="HELLO";bool threw=false;try{(void)inputHostStart();}catch(...){threw=true;}check(threw,"real partial start exception executed");check(g_virtualStartProtection.view(startProtectionClock).phase=="verifying"&&g_virtualStartProtection.view(startProtectionClock).candidateOutcome=="failure","real partial start exception creates failure candidate for required verification");startProtectionClock+=9999;check(g_virtualStartProtection.view(startProtectionClock).locked,"real failure remains locked for9999");++startProtectionClock;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}check(!g_virtualStartProtection.view(startProtectionClock).locked,"real failure unlocks only after actual verification at10000");cases.back()["cleanup"]=cleanup();
}

static void recoveryProtectionCases(){
 for(const int family:{0,1,2})for(const auto delay:{0ULL,3100ULL,10100ULL,31000ULL})for(const bool terminal:{false,true}){
  seed();g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_realStickTest.virtualTargetEnabled=false;
  if(family){g_machineIdentity.family=family==1?YmccFamily::AsusRogAllyX:YmccFamily::Gpd;g_machineIdentity.manufacturer=family==1?"ASUSTEK COMPUTER INC.":"GPD";g_machineIdentity.product=family==1?"ROG Ally X":"G1617";g_msiDinput.enabled=false;g_msiDinput.mainInterfaceMatched=false;g_msiDinput.productId=0;g_msiDinputSourceValid=false;}
  cases.push_back({{"name",terminal?"same-intent-external-return-during-success-settle":"same-intent-external-return-during-pending-start"}});
  if(family||delay)cases.back()["name"]=(family==1?"ROG/":family==2?"GPD/":"MSI/")+std::to_string(delay)+"/"+cases.back()["name"].get<std::string>();
  auto doc=targetDocument("steamdeck",0);doc["input"]["gyroMotion"]={{"enabled",false}};check(sgWriteFileAtomic(YM_SETTINGS_FILE,doc.dump()),"private runtime settings seeded");
  inputCaptureRefreshRealStickSettings(true);check(g_realStickTest.virtualTargetEnabled,"initial real effective consumer enables requested target");
  auto before=g_virtualStartProtection.view(startProtectionClock);check(before.requestId&&before.locked,"initial runtime intent has one protection episode");
  if(terminal)inputHostStartProtectionFinish(before.requestId,"success","offline-current-frame-receipt");
  g_externalPriorityVirtualSuspended=true;inputCaptureRefreshRealStickSettings(true);check(!g_realStickTest.virtualTargetEnabled,"external priority pauses operational output");
  const auto paused=g_virtualStartProtection.view(startProtectionClock);cases.back()["pausedProtection"]={{"phase",paused.phase},{"requestId",paused.requestId}};
  check(paused.phase==(terminal?"verifying":"pending"),"policy pause preserves the admitted logical intent instead of cancelling it");
  startProtectionClock+=delay;const auto beforeReturn=g_virtualStartProtection.view(startProtectionClock);
  g_externalPriorityVirtualSuspended=false;inputCaptureRefreshRealStickSettings(true);
  check(g_virtualStartProtection.view(startProtectionClock).locked==beforeReturn.locked,"policy return never resets or prematurely releases user protection");
  check(g_realStickTest.virtualTargetEnabled&&g_sleepRecoveryOutputWanted,"single real return refresh restores requested operational target");
  const auto restored=g_virtualStartProtection.view(startProtectionClock);check(restored.requestId==before.requestId,"policy restore does not create a new30second protection episode");
  if(g_realStickTest.virtualTargetEnabled){const bool started=inputHostStart();cases.back()["startEvents"]=fixtureEvents;cases.back()["startTrace"]=g_startEntrySeam.trace;check(started,"same-intent recovery can execute its serialized real start during user cooldown");check(g_virtualStartProtection.view(startProtectionClock).requestId==before.requestId,"recovery start inherits request ownership without renewing timer");cases.back()["cleanup"]=cleanup();}
 }
 seed();g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_realStickTest.virtualTargetEnabled=false;reg_fs();cases.push_back({{"name","explicit-OFF-during-policy-pause-cannot-auto-resurrect"}});
 auto base=targetDocument("disabled",0);check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump()),"explicit cancellation private base seeded");auto on=targetDocument("steamdeck",1);check(writeTarget(base,on).value("ok",false),"user ON accepted before policy pause");const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
 g_externalPriorityVirtualSuspended=true;inputCaptureRefreshRealStickSettings(true);auto off=targetDocument("disabled",2);check(!writeTarget(on,off).value("ok",true),"player OFF is refused during protected policy pause");check(g_virtualStartProtection.view(startProtectionClock).phase=="pending","rejected OFF cannot cancel the original recoverable intent");
 startProtectionClock+=30000;check(writeTarget(on,off).value("ok",false),"player OFF is admitted after hard thirty-second budget");
 g_externalPriorityVirtualSuspended=false;inputCaptureRefreshRealStickSettings(true);check(!g_realStickTest.virtualTargetEnabled&&!inputHostStart(),"return after admitted user OFF cannot recreate old output");check(!g_virtualStartProtection.existingTicket("steamdeck",startProtectionClock),"old target ownership cannot be borrowed after a new OFF intent");
 auto reopen=targetDocument("steamdeck",3);check(!writeTarget(off,reopen).value("ok",true),"reentry is blocked until actual OFF completion");inputHostStop("virtual-target-disabled");{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;}startProtectionClock+=10000;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(writeTarget(off,reopen).value("ok",false)&&g_virtualStartProtection.view(startProtectionClock).requestId!=ticket,"reentry after actual OFF verification gets a fresh request");g_realStickTest.enabled=false;

}

static void recordReadCases(){
 seed();g_startEntrySeam.active=false;g_realStickTest.virtualTargetEnabled=true;g_powerLifecycle=PowerLifecycle::Ready;g_machineIdentity.systemProduct="MS-1T91";g_machineIdentity.deviceClass="offline-MSI-G3E";
 cases.push_back({{"name","real-private-owned-file-reader-and-retry-loader"}});
 const auto root=fspath::path(g_yemanccDataDir);const auto ordinary=(root/L"ordinary.json").wstring(),missing=(root/L"missing.json").wstring(),large=(root/L"large.json").wstring();
 std::string actual="sentinel";check(msiCrashReadOwnedRecord(missing,actual)==ERROR_FILE_NOT_FOUND&&actual.empty(),"real missing private file retains typed missing error and publishes no bytes");
 check(sgWriteFileAtomic(ordinary,"private-owned-file"),"real private file written by actual atomic writer");check(msiCrashReadOwnedRecord(ordinary,actual)==ERROR_SUCCESS&&actual=="private-owned-file","real bounded reader returns exact ordinary-file bytes");
 HANDLE held=CreateFileW(ordinary.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"fixture owns exclusive private-file handle");const auto blocked=msiCrashReadOwnedRecord(ordinary,actual);check(blocked==ERROR_SHARING_VIOLATION&&actual.empty()&&msiCrashRetryableRecordReadError(blocked),"real Windows sharing violation remains retryable without parsed authority");if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
 check(msiCrashReadOwnedRecord(ordinary,actual)==ERROR_SUCCESS&&actual=="private-owned-file","same actual reader succeeds after private exclusive handle closes");
 check(sgWriteFileAtomic(large,std::string(65537,'x')),"oversize private file written");check(msiCrashReadOwnedRecord(large,actual)==ERROR_INVALID_DATA&&actual.empty()&&!msiCrashRetryableRecordReadError(ERROR_INVALID_DATA),"real reader rejects over64KiB record without exposing partial bytes");
 const auto dir=(root/L"ordinary-directory");fspath::create_directories(dir);const auto directoryError=msiCrashReadOwnedRecord(dir.wstring(),actual);check(directoryError!=ERROR_SUCCESS&&!msiCrashRetryableRecordReadError(directoryError)&&actual.empty(),"real directory path cannot establish record bytes or retry authority");
 const auto link=(root/L"private-link.json").wstring();const bool symlink=CreateSymbolicLinkW(link.c_str(),ordinary.c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=FALSE;cases.back()["physicalSymlinkFixtureAvailable"]=symlink;
 if(symlink){check(msiCrashReadOwnedRecord(link,actual)==ERROR_INVALID_DATA&&actual.empty(),"actual reparse-point reader is rejected instead of following its target");DeleteFileW(link.c_str());}
 else cases.back()["physicalSymlinkFixtureUnavailableError"]=GetLastError();
 msiRecovery::Record record;record.boardKey=msiCrashBoardKey();record.ownerPid=30000;record.ownerCreationTime="133800000000000001";record.transactionId="offline-private-read";record.rootInstance="USB\\VID_0DB0&PID_1902\\OFFLINE";record.modeReturnPending=true;
 check(msiRecovery::validRecord(record),"private actual-loader record obeys original strict ownership schema");const auto bytes=msiRecovery::encodeRecord(record);const auto owned=msiCrashRecoveryPath();check(sgWriteFileAtomic(owned,bytes),"actual startup-owned filename written only inside private appdata");
 g_msiCrashLoaded=false;g_msiCrashStoreInvalid=false;g_msiCrashRecord={};g_msiCrashInheritedPending=false;g_msiCrashReadRetryPending=false;g_msiCrashReadAttempts=0;g_msiCrashReadRetryAt=g_msiCrashReadStartedAt=0;
 held=CreateFileW(owned.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"private initializer-sharing handle owned");msiCrashRecoveryInitialize();check(!g_msiCrashLoaded&&g_msiCrashStoreInvalid&&g_msiCrashReadRetryPending&&g_msiCrashReadAttempts==1&&!g_msiCrashInheritedPending,"actual Initialize with real sharing failure retains unknown responsibility and retry");
 for(int i=0;i<100;++i)msiCrashRecoveryInitialize();check(g_msiCrashReadAttempts==1,"actual repeated initializer does not spin open inside backoff");if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
 // Due scheduling is an explicit fixture input here; precise time/backoff is
 // independently executed by the integrated cold-loader clock scenarios.
 g_msiCrashReadRetryAt=0;msiCrashRecoveryMaintenance(GetTickCount64());
 check(g_msiCrashLoaded&&!g_msiCrashStoreInvalid&&!g_msiCrashReadRetryPending&&g_msiCrashReadAttempts==2&&g_msiCrashInheritedPending,"actual maintenance reloads same real private file and publishes proven duty");
 check(g_msiCrashRecord.boardKey==record.boardKey&&g_msiCrashRecord.ownerPid==record.ownerPid&&g_msiCrashRecord.transactionId==record.transactionId,"real read and strict parser preserve exact original owner and transaction");
 check(ymSettingsReadRaw(owned)==bytes&&forbiddenIo==0,"actual read recovery preserves original bytes and invokes no device boundary");
 g_msiCrashLoaded=false;g_msiCrashStoreInvalid=false;g_msiCrashInheritedPending=false;g_msiCrashReadRetryPending=false;g_msiCrashReadAttempts=0;g_msiCrashReadRetryAt=g_msiCrashReadStartedAt=0;check(sgWriteFileAtomic(owned,"{bad-json"),"malformed private file seeded");msiCrashRecoveryInitialize();check(g_msiCrashLoaded&&g_msiCrashStoreInvalid&&!g_msiCrashReadRetryPending&&!g_msiCrashInheritedPending,"real malformed bytes remain terminal and cannot publish inherited authority");
}


#include "ps_wake_cases.inc"
#include "pipe_cases.inc"
#include "focus_recovery_cases.inc"
#include "tuple_recovery_cases.inc"
#include "target_return_cases.inc"
#include "persona_lifecycle_cases.inc"
#ifdef YMCC_PREPARE_REJECTION_COLD_CASE
#include "prepare_rejection_on_edge.inc"
#include "prepare_rejection_cases.inc"
#endif
#include "cold_archive_cases.inc"
#include "cold_msi_archive_cases.inc"
#define YMCC_LATE_HIDE_PENDING_FULL_TU 1
#include "msi_late_hide_pending_regression.h"
#include "msi_late_hide_publication_regression.h"
#include "msi_late_hide_inflight_regression.h"
#include "recovery_cost_cases.inc"
#include "worker_cost_cases.inc"

static void switchRecoveryCases(int cliBoundaryOnly=0){
 struct OrdinarySwitchCliScope {
  bool previous=fixtureOrdinarySwitchCliScope;
  OrdinarySwitchCliScope(){if(fixtureFullPipe || fixtureColdArchive || fixtureRuntimeRecoveryOsEnabled)throw std::logic_error("ordinary-switch-cli-scope-role");fixtureOrdinarySwitchCliScope=true;}
  ~OrdinarySwitchCliScope(){fixtureOrdinarySwitchCliScope=previous;}
 } ordinarySwitchCliScope;
 const auto prepare=[&](const std::string& persona="steamdeck"){
  seed();g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="ROG Ally X";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;
  g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_externalPriorityVirtualSuspended=false;g_shortcutRecordingActive=false;
  g_inputHostRecoveryEpisodeAttempts=0;g_inputHostRecoveryLastStartTick=0;g_inputHostRecoveryRolling[0]=0;g_inputHostRecoveryRolling[1]=0;g_inputHostRecoveryRollingNext=0;g_inputHostRecoveryTerminalLogged=false;g_inputHostRecoveryPending=false;g_inputHostRecoveryIntentReset=false;g_inputHostTupleHealPending=false;g_inputHostRecoveryFallbackState=0;g_inputCaptureStop=false;g_inputHostRecoveryDeferredUntil=0;g_inputHostRecoveryHealth.clear();g_inputHostHealthySinceTick=0;
  {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;}
  g_hidHideVisibilityJournal={};
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);g_inputHostOwnedVisibilityReadback={};
   ymcc::inputHost::switchVerificationRuntime::acceptedSource={};
   ymcc::inputHost::switchVerificationRuntime::rogReturnBaseline={};
   ymcc::inputHost::switchVerificationRuntime::rogOffSignal={};
   ymcc::inputHost::switchVerificationRuntime::rogOffSuccessReceipt={};}
  g_realStickTest.virtualTargetPersona=persona;
  auto base=targetDocument(persona,1);check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump()),"recovery private logical SteamDeck target seeded");
  cases.back()["lifecycleVersion"]=ymcc::VirtualStartProtection::lifecycleVersion;
  cases.back()["fallbackMaxAttempts"]=ymcc::VirtualStartProtection::fallbackMaxAttempts;
  g_virtualStartProtection.admit(persona,startProtectionClock);fixtureXinputSourceLive=true;g_xinputAdmittedMask=1;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
  auto sample=fullPipeReadNow(1);{std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot=sample;}
 };
 const auto readVerifiedCurrent=[&](int family=1){auto sample=fullPipeReadNow(family);std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
  const auto previous=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);
  sample.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(previous,inputHostRecoveryHealthSource(&sample,true),GetTickCount64());
  g_inputCaptureGamepadSnapshot=sample;return sample;};
 const auto stopCount=[](){return static_cast<unsigned>(std::count_if(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),[](const auto& v){return v.rfind("stop:",0)==0;}));};
 const auto createCount=[](){return static_cast<unsigned>(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"create-process"));};
 const auto queuedRecoveryCount=[](){std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
  return static_cast<unsigned>(std::count_if(g_inputHostLifecycleWorkQueue.begin(),g_inputHostLifecycleWorkQueue.end(),[](const auto& work){return fixtureLifecycleWorkReason(work).rfind("switch-recovery:",0)==0;}));};
 const auto timeline=[&](const char* stage){const auto view=g_virtualStartProtection.view(startProtectionClock);const auto& request=g_inputHostSwitchRecovery;
  cases.back()["timeline"].push_back({{"stage",stage},{"tick",startProtectionClock},{"requestId",view.requestId},{"phase",view.phase},
   {"elapsedMs",view.elapsedMs},{"remainingMs",view.remainingMs},{"verificationRemainingMs",view.verificationRemainingMs},
   {"fallbackAttempts",view.fallbackAttempts},{"episodeAttempts",g_inputHostRecoveryEpisodeAttempts.load()},
   {"stopCount",stopCount()},{"createCount",createCount()},{"spent",request.spent},{"executionCleanup",request.executionCleanup},{"awaitingCleanup",request.awaitingCleanup}});};
 const auto noRecovery=[&](const char* assertion){const auto view=g_virtualStartProtection.view(startProtectionClock);
  check(view.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&!g_inputHostSwitchRecoveryPending.load()&&
   !g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==0&&stopCount()==0&&createCount()==1,assertion);};
 const auto injectFrameFault=[&](int family=1){
  fixtureFrameFirstAck=false;g_startEntrySeam.pipeFailCommand="SUBMIT_FRAME";fixtureStartupTransportError=ERROR_BROKEN_PIPE;
  auto sample=readVerifiedCurrent(family);sample.pad.wButtons=XINPUT_GAMEPAD_A;const auto result=fullPipeSubmitSource(sample,true,false);
  check(g_inputHostTransportFault&&result.value("hostSubmission",std::string{})!="frame-accepted","actual SUBMIT_FRAME timeout produces the scoped cleanup token");
  g_startEntrySeam.pipeFailCommand.clear();fixtureStartupTransportError=ERROR_SUCCESS;fixtureFrameFirstAck=true;
 };
 // Controlled physical read/cache endpoints only. Admission, fault predicates,
 // verification, offers, maintenance, Stop, return cleanup and Start stay real.
 const auto selectLocalPhysicalSource=[&](){
  std::scoped_lock lock(g_gamepadReadMx,g_hidHidePhysicalIdentityMx);
  HidHidePhysicalIdentity source;source.resolved=true;source.vendorId=0x0b05;source.productId=0x1b4c;source.usagePage=1;source.usage=5;
  source.deviceInstanceId="HID\\VID_0B05&PID_1B4C&MI_02\\FIXTURE_ROG131128_GAMEPAD";
  source.sourcePath=source.deviceInstanceId;source.baseContainerDeviceInstanceId=fixtureRogDeferredRoot();
  source.physicalIdentity=source.deviceInstanceId+"|"+source.baseContainerDeviceInstanceId;
  g_hidHidePhysicalIdentity=source;g_xinputPrimarySlot=0;g_xinputSemanticSlot=0;
  g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=g_xinputAdmittedMask=g_xinputConnectedMask=g_xinputPreTargetSlots=1;
  g_xinputSlotUnknownMask=g_xinputVirtualEndpointMask=0;g_xinputSourceReconfirmMask=0;g_xinputSlotReprobeMask=0;
  g_xinputSlotIdentityResolvedEpoch[0]=g_xinputSlotIdentityEpoch.load();
  g_xinputPreTargetBoundaryScanned=true;g_xinputBoundaryScanEpoch=g_xinputTopologyEpoch.load();fixtureXinputSourceLive=true;
 };
 const auto acceptLocalFrames=[&](){json ack;
  for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();if(n==kInputHostFreshNeutralFramesRequired)sample.pad.wButtons=XINPUT_GAMEPAD_A;ack=fullPipeSubmitSource(sample,true,false);}
  check(ack.value("hostSubmission",std::string{})=="frame-accepted"&&!ack.value("focusIsolationSemanticNeutral",true)&&g_inputHostActive,
   "local visibility scenario consumes an actual current physical source frame ACK");
 };
 const auto startLocalOwned=[&](const std::string& persona="steamdeck",bool acceptFrames=true,bool rootStage=false){
  prepare(persona);if(rootStage)g_machineIdentity.product="ROG Ally X RC72LA";fixtureSwitchOwnedHideSeed=true;fixtureRogDeferredCycleEnabled=true;fixtureFrameFirstAck=acceptFrames;fixtureSwitchSourceAtEnsure=selectLocalPhysicalSource;selectLocalPhysicalSource();
  check(inputHostStart(),"local visibility scenario starts its actual owned prepared target");
  check(traced("hidhide-apply")&&g_inputHostProcess&&g_inputHostHello&&g_inputHostPrepared&&g_inputHostNeutralized&&
   g_inputHostBoundPersona==persona&&g_inputHostBoundPowerGeneration==currentPowerGeneration()&&g_hidHideVisibilityJournal.active&&
   g_hidHideVisibilityJournal.transactionId==g_inputHostRunId&&!g_hidHideVisibilityJournal.physical.deviceInstanceId.empty()&&
   !g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId.empty()&&!g_hidHideVisibilityJournal.hiddenByUs.empty(),
   "actual Start hide leaf binds nonempty physical identity root owned paths and its exact current run transaction");
  if(acceptFrames)acceptLocalFrames();
  return g_virtualStartProtection.view(startProtectionClock);
 };
 const auto captureLocalReadback=[&](){std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  const auto stamp=inputHostCaptureOwnedVisibilityReadbackLocked();
  check(stamp.requestId==g_virtualStartProtection.view(startProtectionClock).requestId&&stamp.hostEpoch==g_inputHostEpoch&&
   stamp.power==currentPowerGeneration()&&stamp.runId==g_inputHostRunId&&stamp.persona==g_inputHostBoundPersona&&
   stamp.configRevision==g_inputHostConfigRevision&&stamp.intentRevision==g_inputHostVirtualIntentRevision.load()&&
   stamp.transactionId==g_hidHideVisibilityJournal.transactionId&&!stamp.deviceInstanceId.empty()&&!stamp.physicalRoot.empty()&&
   stamp.deviceInstanceId==g_hidHideVisibilityJournal.physical.deviceInstanceId&&stamp.physicalRoot==g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId&&
   !stamp.targets.empty()&&stamp.targets==g_hidHideVisibilityJournal.hiddenByUs&&stamp.startedAt,
   "actual readback capture stamps the complete current Host request configuration journal identity and owned target set");
  return stamp;
 };
 const auto publishLocalReadback=[&](bool missing=true,bool readback=true,bool cloakOn=true){
  auto stamp=captureLocalReadback();auto hidden=stamp.targets;if(missing&&!hidden.empty())hidden.erase(hidden.begin());
  const bool published=inputHostPublishOwnedVisibilityReadback(stamp,readback,hidden,cloakOn,GetTickCount64());
  check(published,"current stamped owned visibility readback publishes outside the Host lock");
  cases.back()["readbacks"].push_back({{"requestId",stamp.requestId},{"hostEpoch",stamp.hostEpoch},{"power",stamp.power},
   {"runId",stamp.runId},{"persona",stamp.persona},{"transactionId",stamp.transactionId},{"physicalId",stamp.deviceInstanceId},
   {"physicalRoot",stamp.physicalRoot},{"ownedTargets",stamp.targets},{"startedAt",stamp.startedAt},{"readback",readback},{"missing",missing},{"cloakOn",cloakOn}});
 };
 const auto offerLocal=[&](std::uint64_t ticket,InputHostSwitchLocalFault kind){std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  const auto commands=g_inputHostSequence;const auto io=forbiddenIo;const auto reads=inertXinputReads;
  check(inputHostSwitchLocalFaultCurrentLocked(ticket)==kind,"actual current local fault predicate classifies exact owned obligation");
  check(g_inputHostSequence==commands&&forbiddenIo==io&&inertXinputReads==reads,"local fault classification performs no command or device read");
  inputHostSwitchRecoveryOfferLocked(ticket,currentPowerGeneration(),ERROR_SUCCESS,false,kind);
  check(g_inputHostSwitchRecovery.pending&&g_inputHostSwitchRecovery.localFault==kind&&g_inputHostSwitchRecovery.error==ERROR_SUCCESS&&
   !g_inputHostSwitchRecovery.tupleFault&&!g_inputHostTransportFault&&!g_inputHostTupleFault.requestId,
   "local obligation offers the original recovery owner without fabricated tuple or transport failure");
 };
 const auto finishLocalCase=[&](){cases.back()["events"]=fixtureEvents;cases.back()["trace"]=g_startEntrySeam.trace;cases.back()["cleanup"]=cleanup();
  cases.back()["controlledIoObservation"]={{"forbiddenIoThroughCase",forbiddenIo},{"scope","cumulative fixture rejection counter; not device IO or hardware proof"}};fixtureFrameFirstAck=false;};
if(cliBoundaryOnly && cliBoundaryOnly!=4){
 for(const std::string variant:(cliBoundaryOnly==2?std::vector<std::string>{"current-owner"}:cliBoundaryOnly==3?std::vector<std::string>{"host-journal"}:std::vector<std::string>{"host-journal","power"})){
  const auto failuresBefore=failures.size();
  cases.push_back({{"name",std::string(variant=="current-owner"?"owned-readback-cli-current/":"owned-readback-cli-boundary/")+variant}});
  const auto request=startLocalOwned();const auto old=captureLocalReadback();
  const auto oldJournal=g_hidHideVisibilityJournal;const auto oldBoundPower=g_inputHostBoundPowerGeneration;
  const auto oldCapability=g_inputHostHidHideStateQuery;const auto oldFault=g_inputHostTransportFault;
  const auto oldSeam=g_startEntrySeam.active;const auto commands=g_inputHostSequence;
  fixtureOwnedCliBoundary=true;fixtureOwnedCliCommands.clear();fixtureOwnedCliHidden=variant=="current-owner"?old.targets:std::vector<std::wstring>{};fixtureOwnedCliCallbacks=0;
  fixtureOwnedCliPhysical={oldJournal.physical};fixtureOwnedCliPhysicalQueries=fixtureOwnedCliFullQueries=fixtureOwnedCliWrites=0;
  g_inputHostHidHideStateQuery=false;g_inputHostTransportFault=true;g_startEntrySeam.active=false;
  g_hidHideVisibilityJournal.admittedTick=0;g_inputHostLastPhysicalIdentityCheckTick=0;
  g_inputHostReapplyHideEpisodeKey="successor-repair-budget-sentinel";
  g_inputHostReapplyHideRepairs=2;g_inputHostReapplyHideExhausted=false;g_inputHostReapplyHideLastTick=0;
  g_hidHideOwnedGameRepairProgress={};g_hidHideOwnedGameRepairProgress.key="successor-discovery-sentinel";
  const auto budgetKey=g_inputHostReapplyHideEpisodeKey;const auto repairs=g_inputHostReapplyHideRepairs;
  const auto repairProgressKey=g_hidHideOwnedGameRepairProgress.key;
  InputHostOwnedVisibilityReadback newer;
  fixtureOwnedCliAfterDevList=[&](){
   {
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    if(variant=="host-journal"){
     ++g_inputHostEpoch;g_inputHostRunId+="-replacement";g_inputHostHostInstanceId+="-replacement";
     g_hidHideVisibilityJournal.transactionId=g_inputHostRunId;
     g_hidHideVisibilityJournal.hiddenByUs.push_back(L"HID\\CLI_BOUNDARY_NEW_OWNER");
    }else if(variant=="power"){g_powerGeneration=old.power+1;g_inputHostBoundPowerGeneration=old.power+1;}
    newer=inputHostCaptureOwnedVisibilityReadbackLocked();
   }
   check(newer.requestId&&inputHostPublishOwnedVisibilityReadback(newer,true,newer.targets,true,GetTickCount64()),
    "successor current contract publishes positive authority during actual CLI read");
  };
  inputHostReconcilePhysicalSourceOnLifecycleThread();
  g_startEntrySeam.active=oldSeam;fixtureOwnedCliBoundary=false;
  check(fixtureOwnedCliCallbacks==1&&fixtureOwnedCliCommands.size()>=2&&fixtureOwnedCliCommands[0]==L"--dev-list"&&fixtureOwnedCliCommands[1]==L"--cloak-state",
   "actual callback interleaves exactly two unlocked CLI queries");
  check(fixtureOwnedCliCommands==std::vector<std::wstring>{L"--dev-list",L"--cloak-state"},
   "actual reconcile old CLI read sends only the original two query commands");
  check(fixtureOwnedCliWrites==0,"actual reconcile old CLI read cannot issue hide writes for successor owner");
  check(fixtureOwnedCliPhysicalQueries==0&&fixtureOwnedCliFullQueries==0,
   "actual reconcile refuses stale observation before repair discovery or full readback");
  check(variant=="current-owner" ?
   (g_inputHostReapplyHideRepairs==0&&!g_inputHostReapplyHideExhausted&&g_hidHideOwnedGameRepairProgress.key.empty()) :
   (g_inputHostReapplyHideEpisodeKey==budgetKey&&g_inputHostReapplyHideRepairs==repairs&&g_inputHostReapplyHideLastTick==0&&g_hidHideOwnedGameRepairProgress.key==repairProgressKey),
   variant=="current-owner"?"actual current owner two-query healthy reconcile clears repair episode from two to zero":
   "actual reconcile old CLI read cannot reset or charge successor repair budget");
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   check(g_inputHostOwnedVisibilityReadback.hostEpoch==newer.hostEpoch&&g_inputHostOwnedVisibilityReadback.power==newer.power&&
    g_inputHostOwnedVisibilityReadback.transactionId==newer.transactionId&&!g_inputHostOwnedVisibilityReadback.targetsMissing,
    "old query cannot overwrite successor authority");
   check(inputHostSwitchLocalFaultCurrentLocked(request.requestId)==InputHostSwitchLocalFault::None,"old missing-target query supplies no current recovery fault");
   inputHostSwitchRecoveryOfferLocked(request.requestId,currentPowerGeneration(),ERROR_SUCCESS,false,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);
   inputHostSwitchRecoveryMaintenanceLocked();}
  check(g_inputHostSequence==commands,"old CLI consumption sends no Host command");
  noRecovery("old CLI observation causes no queue Stop Start reservation or charge");
  std::vector<std::string> actualCommands;for(const auto& command:fixtureOwnedCliCommands)actualCommands.push_back(W2U(command));
  cases.back()["cliBoundary"]={{"variant",variant},{"callbackCount",fixtureOwnedCliCallbacks},{"firstCommands",{"--dev-list","--cloak-state"}},
   {"commands",actualCommands},{"actualReconcile",true},{"physicalQueries",fixtureOwnedCliPhysicalQueries},{"fullQueries",fixtureOwnedCliFullQueries},{"writes",fixtureOwnedCliWrites},
   {"repairKeyBefore",budgetKey},{"repairKeyAfter",g_inputHostReapplyHideEpisodeKey},{"repairAttemptsBefore",repairs},{"repairAttemptsAfter",g_inputHostReapplyHideRepairs},
   {"commandSequenceBefore",commands},{"commandSequenceAfter",g_inputHostSequence},
   {"oldEpoch",old.hostEpoch},{"newEpoch",newer.hostEpoch},{"oldPower",old.power},{"newPower",newer.power},
   {"oldTransaction",old.transactionId},{"newTransaction",newer.transactionId}};
  g_inputHostEpoch=old.hostEpoch;g_inputHostRunId=old.runId;g_inputHostHostInstanceId=old.hostInstanceId;
  g_powerGeneration=old.power;g_inputHostBoundPowerGeneration=oldBoundPower;g_hidHideVisibilityJournal=oldJournal;
  g_inputHostHidHideStateQuery=oldCapability;g_inputHostTransportFault=oldFault;
  cases.back()["passed"]=failures.size()==failuresBefore;finishLocalCase();
 }
 return;
}

 struct LocalDeferredWriteScope {
  const InputHostDeferredPhysicalHide* previous=t_inputHostDeferredPhysicalHideWrite;
  bool refreshIssue=t_inputHostDeferredPhysicalHideRefreshIssueActive;
  explicit LocalDeferredWriteScope(const InputHostDeferredPhysicalHide& stage){t_inputHostDeferredPhysicalHideWrite=&stage;}
  ~LocalDeferredWriteScope(){t_inputHostDeferredPhysicalHideWrite=previous;t_inputHostDeferredPhysicalHideRefreshIssueActive=refreshIssue;}
 };
 const auto captureRootStage=[](){std::lock_guard<std::mutex> lock(g_inputHostDeferredPhysicalHideMx);return g_inputHostDeferredPhysicalHide;};
 const auto startRootRefreshStage=[&](){
  const auto candidate=startLocalOwned("steamdeck",true,true);const auto deadline=startProtectionClock+candidate.remainingMs;
  // Controlled deferred-obligation input after actual Start. Run the REAL normal
  // configuration helper through the existing snapshot I/O leaf; this does not
  // claim that the normal worker or a physical USB refresh ran.
  inputHostDeferredPhysicalHideArm(true,g_inputHostRunId,g_inputHostEpoch,currentPowerGeneration(),"steamdeck",deadline,candidate.requestId);
  fixtureRogDeferredJournalSeed=true;const auto beforeReads=fixtureRogPartialSnapshotReads;
  const auto initial=captureRootStage();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);LocalDeferredWriteScope write(initial);
   const bool configured=inputHostDeferredPhysicalHideConfigurationLocked(true);
   check(configured,"real normal configuration helper accepts the exact journal through controlled snapshot I/O");
   if(!configured)throw std::runtime_error("root stage fixture configuration preparation failed; retain checkpoints without unsafe target mutation");}
  publishLocalReadback(false);
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);InputHostDeferredPhysicalHide stage;
   const bool captured=inputHostSwitchRootRefreshSnapshotLocked(candidate.requestId,stage);
   check(captured&&stage.configurationApplied&&stage.configurationReady&&
    !stage.refreshAttempted&&!stage.continuationRetired&&stage.deadline==deadline&&stage.configurationTargets.size()==2&&
    g_hidHideVisibilityJournal.physicalSources.size()==1&&fixtureRogPartialSnapshotReads>beforeReads&&fixtureRogDeferredCycles.empty(),
    "shared root snapshot consumes actual normal configuration progress and fresh positive bound readback without a refresh");
   if(!captured || stage.configurationTargets.size()!=2)throw std::runtime_error("root stage fixture proof unavailable; refuse downstream target mutation");}
  cases.back()["stageInput"]="controlled deferred obligation; real normal configuration helper; controlled snapshot I/O; actual bound readback publication";
  cases.back()["normalConfigurationSnapshots"]=fixtureRogDeferredSnapshots;
  return candidate;
 };
 if(cliBoundaryOnly!=4){
 {
 cases.push_back({{"name","central-switch-lifecycle-version-and-single-fallback-clock"}});
 using Cycle=ymcc::VirtualStartProtection;Cycle cycle;const Cycle::Tick admittedAt=100000,candidateAt=admittedAt+2500;
 check(std::string(Cycle::lifecycleVersion)=="VC-LIFECYCLE-20261020-R3"&&Cycle::fallbackMaxAttempts==1&&Cycle::settleMs==10000&&Cycle::maxMs==30000,"central lifecycle version fixes one fallback and the original ten-second and thirty-second limits");
 const auto admission=cycle.admit("steamdeck",admittedAt);
 check(cycle.beginVerification(admission.requestId,"failure",candidateAt),"central failure candidate begins execution inspection after admission");
 check(!cycle.executionInspectionComplete(candidateAt+Cycle::settleMs-1)&&!cycle.fallbackAllowed(admission.requestId,candidateAt+Cycle::settleMs-1)&&!cycle.reserveFallback(admission.requestId,candidateAt+Cycle::settleMs-1)&&cycle.view(candidateAt+Cycle::settleMs-1).fallbackAttempts==0,"central fallback cannot reserve at9999 after the first candidate");
 check(!cycle.admit("disabled",candidateAt+Cycle::settleMs-1).accepted&&!cycle.beginVerification(admission.requestId,"failure",candidateAt+Cycle::settleMs-1),"player request and repeated failure cannot restart candidate inspection");
 const auto ready=cycle.view(candidateAt+Cycle::settleMs);
 check(cycle.executionInspectionComplete(candidateAt+Cycle::settleMs)&&ready.requestId==admission.requestId&&ready.verificationRemainingMs==0&&ready.fallbackAttempts==0&&ready.elapsedMs==12500&&ready.remainingMs==17500&&cycle.fallbackAllowed(admission.requestId,candidateAt+Cycle::settleMs),"central fallback becomes available at exactly10000 with its original admission remainder");
 check(!cycle.reserveFallback(admission.requestId+1,candidateAt+Cycle::settleMs)&&cycle.view(candidateAt+Cycle::settleMs).fallbackAttempts==0,"foreign request cannot reserve the current fallback");
 check(cycle.reserveFallback(admission.requestId,candidateAt+Cycle::settleMs)&&cycle.view(candidateAt+Cycle::settleMs).fallbackAttempts==1,"central fallback reservation charges the same request exactly once");
 check(!cycle.admit("steamdeck",candidateAt+Cycle::settleMs).accepted&&!cycle.admit("disabled",candidateAt+Cycle::settleMs).accepted&&!cycle.fallbackAllowed(admission.requestId,candidateAt+Cycle::settleMs)&&!cycle.reserveFallback(admission.requestId,candidateAt+Cycle::settleMs),"players and repeated dispatch cannot reserve a second fallback or renew the request");
 check(!cycle.beginVerification(admission.requestId,"failure",admittedAt+Cycle::maxMs-1)&&!cycle.reserveFallback(admission.requestId,admittedAt+Cycle::maxMs-1)&&cycle.view(admittedAt+Cycle::maxMs-1).remainingMs==1&&cycle.view(admittedAt+Cycle::maxMs-1).fallbackAttempts==1,"repeated late failure retains one reservation and one millisecond of the original deadline");
 check(cycle.view(admittedAt+Cycle::maxMs).phase=="expired"&&!cycle.reserveFallback(admission.requestId,admittedAt+Cycle::maxMs)&&cycle.view(admittedAt+Cycle::maxMs).fallbackAttempts==1,"thirty-second expiry cannot renew or reserve a charged request");
 Cycle late;const auto lateAdmission=late.admit("steamdeck",admittedAt);late.beginVerification(lateAdmission.requestId,"failure",admittedAt);
 check(late.reserveFallback(lateAdmission.requestId,admittedAt+Cycle::maxMs-1)&&late.view(admittedAt+Cycle::maxMs-1).remainingMs==1,"an inspected fallback remains eligible strictly before the original thirty-second cap");
 Cycle capped;const auto cappedAdmission=capped.admit("steamdeck",admittedAt);capped.beginVerification(cappedAdmission.requestId,"failure",admittedAt);
 check(!capped.reserveFallback(cappedAdmission.requestId,admittedAt+Cycle::maxMs)&&capped.view(admittedAt+Cycle::maxMs).fallbackAttempts==0,"an unspent fallback cannot first reserve at the thirty-second cap");
 Cycle pending;const auto pendingAdmission=pending.admit("steamdeck",admittedAt);
 check(!pending.reserveFallback(pendingAdmission.requestId,admittedAt+Cycle::settleMs-1)&&pending.view(admittedAt+Cycle::settleMs-1).fallbackAttempts==0&&!pending.reserveFallback(pendingAdmission.requestId,admittedAt+Cycle::settleMs)&&pending.view(admittedAt+Cycle::settleMs).fallbackAttempts==0&&!pending.executionInspectionComplete(admittedAt+Cycle::settleMs),"admission time without an execution receipt cannot authorize fallback or invent completed inspection");
 cases.back()["lifecycleVersion"]=Cycle::lifecycleVersion;cases.back()["fallbackMaxAttempts"]=Cycle::fallbackMaxAttempts;
 cases.back()["timeline"]={{{"stage","inspection-complete-before-reserve"},{"elapsedMs",ready.elapsedMs},{"remainingMs",ready.remainingMs},{"fallbackAttempts",ready.fallbackAttempts}},{{"stage","original-cap"},{"elapsedMs",Cycle::maxMs},{"fallbackAttempts",cycle.view(admittedAt+Cycle::maxMs).fallbackAttempts}}};
 }
 cases.push_back({{"name","actual-source-lineage-change-requires-current-accepted-frame"}});prepare();fixtureFrameFirstAck=true;
 const auto lineageCommandCount=[](){return g_inputHostSampleSequence;}; // Serializer sequence is real even when the explicit receipt seam bypasses envelopes.
 check(inputHostStart(),"source lineage proof establishes an actual native target");
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();fullPipeSubmitSource(sample,true,false);}
 const auto lineageCandidate=g_virtualStartProtection.view(startProtectionClock);const auto lineageEpoch=g_inputHostEpoch;const auto lineageCreates=createCount();const auto oldAcceptedSequence=lineageCommandCount();
 check(lineageCandidate.verificationPending&&lineageCandidate.candidateOutcome=="success"&&g_inputHostActive,"actual first accepted source enters candidate verification");
 ymcc::input::recoveryHealth::Source oldLineage;InputCaptureGamepadSnapshot absentSource;
 {std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);oldLineage=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);g_inputCaptureGamepadSnapshot.live=false;absentSource=g_inputCaptureGamepadSnapshot;}
 startProtectionClock+=100;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).locked&&g_inputHostRecoveryEpisodeAttempts.load()==0,"missing source is unknown and never creates a repair attempt");
 auto returningSource=readVerifiedCurrent();
 check(returningSource.readContinuityGeneration!=oldLineage.continuity&&returningSource.live&&returningSource.readContextStable,"real continuity producer distinguishes reappeared source from old accepted lineage");
 startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 const auto beforeFreshAck=g_virtualStartProtection.view(startProtectionClock);const auto beforeFreshAckSequence=lineageCommandCount();
 check(beforeFreshAck.locked&&beforeFreshAck.phase=="verifying","a new fresh source cannot borrow an old lineage accepted frame to publish final success");
 check(beforeFreshAckSequence==oldAcceptedSequence,"lineage replacement and actual verification inspect without any new Host command or ACK");
 // Submit the previously absent read after the fresh replacement was published.
 // This is a real cleanup ACK for the same Host, never a report from the new
 // live lineage. The fixture does not overwrite the returning cached source.
 const auto absentAck=fullPipeSubmitSource(absentSource,false,false);
 check(absentAck.value("hostSubmission",std::string{})=="frame-accepted"&&absentAck.value("focusIsolationSemanticNeutral",false)&&lineageCommandCount()==oldAcceptedSequence+1,"actual source-absent submit reaches only the owned cleanup report and ACK");
 const auto afterAbsentSequence=lineageCommandCount();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 const auto afterAbsentAck=g_virtualStartProtection.view(startProtectionClock);
 check(afterAbsentAck.requestId==lineageCandidate.requestId&&afterAbsentAck.phase=="verifying"&&afterAbsentAck.locked&&afterAbsentAck.verificationRemainingMs==0&&lineageCommandCount()==afterAbsentSequence,"actual source-absent cleanup ACK cannot prove the newly live physical lineage");
 check(g_inputHostEpoch==lineageEpoch&&createCount()==lineageCreates&&stopCount()==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"unproven new lineage holds verification without rebuilding a healthy owned target");
 returningSource.pad.wButtons=XINPUT_GAMEPAD_A;const auto newAck=fullPipeSubmitSource(returningSource,true,false);
 check(newAck.value("hostSubmission",std::string{})=="frame-accepted"&&!newAck.value("focusIsolationSemanticNeutral",true)&&lineageCommandCount()==afterAbsentSequence+1,"current reappeared source forms an actual new accepted report");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&!g_virtualStartProtection.view(startProtectionClock).locked,"actual current-lineage report closes verification without renewing its ten-second or thirty-second clock");
 noRecovery("normal source lineage verification uses zero recovery queues Stops reservations or episode attempts");
 cases.back()["candidate"]=lineageCandidate.candidateOutcome;cases.back()["beforeFreshAck"]={{"phase",beforeFreshAck.phase},{"locked",beforeFreshAck.locked}};cases.back()["oldContinuity"]=oldLineage.continuity;cases.back()["newContinuity"]=returningSource.readContinuityGeneration;cases.back()["oldAcceptedSequence"]=oldAcceptedSequence;cases.back()["beforeFreshAckSequence"]=beforeFreshAckSequence;cases.back()["absentAck"]=absentAck;cases.back()["afterAbsentAck"]={{"phase",afterAbsentAck.phase},{"locked",afterAbsentAck.locked},{"commandSequence",afterAbsentSequence}};cases.back()["newAck"]=newAck;cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 {
 cases.push_back({{"name","actual-msi-mode-change-requires-current-mode-accepted-frame"}});prepare();fixtureFrameFirstAck=true;
 g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
 g_msiDinput.enabled=true;g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=kMsiClawCapability.pidDInput;g_msiDinputSourceValid=true;
 g_msiMainSourceRevision=1;fixtureXinputSourceLive=false;g_xinputAdmittedMask=0;g_xinputConnectedMask=0;g_xinputPhysicalSourceMask=0;
 // Actual Start/Ensure arms and confirms the MSI route through the existing
 // mode-write/first-valid-read leaves; never replace the eligibility predicate.
 check(inputHostStart(),"MSI mode proof establishes an actual owned native target");
 check(traced("ensure:main")&&msiControllerRepairScope()&&msiDinputMainSourceEligible(),"actual Start route consumer establishes eligible MSI DInput without XInput fallback");
 InputCaptureGamepadSnapshot acceptedRead;json initialAck;
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){acceptedRead=readVerifiedCurrent(0);initialAck=fullPipeSubmitSource(acceptedRead,true,false);}
 const auto candidate=g_virtualStartProtection.view(startProtectionClock);const auto host=g_inputHostProcess;const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;
 const auto power=currentPowerGeneration();const auto sourceRevision=g_msiMainSourceRevision.load();const auto oldMode=g_msiFeedbackModeGeneration.load();const auto intent=g_inputHostVirtualIntentRevision.load();const auto targetRevision=g_psEndpointOwnership.revision();
 const auto source=inputHostRecoveryHealthSource(&acceptedRead,true);const auto commands=g_inputHostSequence;const auto samples=g_inputHostSampleSequence;const auto creates=createCount();const auto stops=stopCount();
 check(initialAck.value("hostSubmission",std::string{})=="frame-accepted"&&!initialAck.value("focusIsolationSemanticNeutral",true)&&g_inputHostActive&&candidate.phase=="verifying"&&candidate.candidateOutcome=="success"&&candidate.verificationPending&&candidate.verificationRemainingMs==10000&&source.live&&source.stable&&source.route==ymcc::input::recoveryHealth::Route::MsiDinput&&source.incarnation==sourceRevision&&source.continuity,"actual accepted MSI source frame supplies the original-mode verification candidate");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(ymcc::inputHost::switchVerificationRuntime::currentOutputProvenLocked(candidate),"initial MSI accepted frame satisfies the actual current-output verification predicate");}
 // This real mode-generation producer retires feedback authority only. A
 // continuous current-mode reader need not revoke/rebind its physical source.
 msiNoteDisruptiveTx("fixture-current-source-mode-change");
 startProtectionClock+=candidate.verificationRemainingMs;auto currentRead=readVerifiedCurrent(0);const auto currentSource=inputHostRecoveryHealthSource(&currentRead,true);
 check(g_msiFeedbackModeGeneration.load()==oldMode+1&&currentRead.readMsiModeGeneration==oldMode+1&&currentRead.readMsiSourceRevision==sourceRevision&&g_msiMainSourceRevision.load()==sourceRevision&&currentSource.live&&currentSource.stable&&ymcc::input::recoveryHealth::sameSource(source,currentSource)&&currentSource.continuity!=source.continuity&&currentSource.sequence>source.sequence&&currentSource.identity==source.identity&&currentSource.topology==source.topology&&msiDinputMainSourceEligible(),"real MSI mode edge retires old accepted continuity while retaining eligible physical source revision");
 check(g_inputHostProcess==host&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&currentPowerGeneration()==power&&g_inputHostVirtualIntentRevision.load()==intent&&g_psEndpointOwnership.revision()==targetRevision&&g_virtualStartProtection.view(startProtectionClock).requestId==candidate.requestId&&g_inputHostSequence==commands&&g_inputHostSampleSequence==samples,"MSI mode change preserves Host power request intent and target without a new command or ACK");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 const auto beforeAck=g_virtualStartProtection.view(startProtectionClock);
 check(beforeAck.requestId==candidate.requestId&&beforeAck.phase=="verifying"&&beforeAck.locked&&beforeAck.verificationRemainingMs==0&&beforeAck.remainingMs==candidate.remainingMs-candidate.verificationRemainingMs,"fresh MSI mode cannot borrow the old-mode accepted frame to publish final success");
 check(g_inputHostSequence==commands&&g_inputHostSampleSequence==samples&&createCount()==creates&&stopCount()==stops&&g_inputHostRecoveryEpisodeAttempts.load()==0,"missing current-mode ACK holds verification without Host commands or a recovery attempt");
 currentRead.pad.wButtons=XINPUT_GAMEPAD_A;const auto currentAck=fullPipeSubmitSource(currentRead,true,false);
 check(currentAck.value("hostSubmission",std::string{})=="frame-accepted"&&!currentAck.value("focusIsolationSemanticNeutral",true)&&g_inputHostSampleSequence==samples+1,"actual current-mode MSI source report reaches a new accepted Host frame");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 const auto final=g_virtualStartProtection.view(startProtectionClock);
 check(final.requestId==candidate.requestId&&final.phase=="success"&&!final.locked&&g_inputHostProcess==host&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&currentPowerGeneration()==power&&g_msiMainSourceRevision.load()==sourceRevision&&g_msiFeedbackModeGeneration.load()==oldMode+1&&g_inputHostVirtualIntentRevision.load()==intent&&g_psEndpointOwnership.revision()==targetRevision&&createCount()==creates&&stopCount()==stops&&g_inputHostRecoveryEpisodeAttempts.load()==0,"actual current-mode MSI ACK permits final verification on the same request Host power and source");
 noRecovery("normal MSI source and mode lineage verification uses zero recovery queues Stops reservations or episode attempts");
 cases.back()["initialAck"]=initialAck;cases.back()["oldMode"]=oldMode;cases.back()["currentMode"]=currentRead.readMsiModeGeneration;cases.back()["sourceRevision"]=sourceRevision;cases.back()["oldContinuity"]=source.continuity;cases.back()["currentContinuity"]=currentSource.continuity;cases.back()["beforeCurrentModeAck"]={{"phase",beforeAck.phase},{"locked",beforeAck.locked},{"sampleSequence",samples}};cases.back()["currentAck"]=currentAck;cases.back()["final"]={{"phase",final.phase},{"locked",final.locked}};cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 }
 {
 cases.push_back({{"name","actual-msi-mode-change-cannot-credit-old-recovery-health"}});prepare();fixtureFrameFirstAck=true;
 g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
 g_msiDinput.enabled=true;g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=kMsiClawCapability.pidDInput;g_msiDinputSourceValid=true;g_msiMainSourceRevision=1;
 fixtureXinputSourceLive=false;g_xinputAdmittedMask=g_xinputConnectedMask=g_xinputPhysicalSourceMask=0;
 check(inputHostStart()&&msiDinputMainSourceEligible(),"MSI recovery health case establishes actual owned target and eligible source");
 g_inputHostRecoveryEpisodeAttempts=1;const auto rolling=GetTickCount64();g_inputHostRecoveryRolling[0]=rolling;g_inputHostRecoveryRolling[1]=0;
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent(0);fullPipeSubmitSource(sample,true,false);}
 // Real elapsed physical-read/ACK coverage, not a forged health timestamp.
 for(int n=0;n<9;++n){Sleep(250);auto sample=readVerifiedCurrent(0);fullPipeSubmitSource(sample,true,false);}
 const auto oldMode=g_msiFeedbackModeGeneration.load();const auto oldThrough=g_inputHostRecoveryHealth.through();const auto oldContinuity=g_inputHostRecoveryHealth.continuity();
 check(g_inputHostRecoveryHealth.complete(kRecoveryHealthyCompleteMs)&&g_inputHostRecoveryEpisodeAttempts.load()==1,"old MSI mode owns an actual accepted two-second healthy window before maintenance");
 msiNoteDisruptiveTx("fixture-recovery-health-mode-change");const auto freshMode=readVerifiedCurrent(0);const auto beforeSamples=g_inputHostSampleSequence;
 inputHostRecoveryMaintenanceTick();
 check(g_msiFeedbackModeGeneration.load()==oldMode+1&&freshMode.readMsiModeGeneration==oldMode+1&&g_inputHostSampleSequence==beforeSamples,"mode edge and fresh read precede any current-mode report or ACK");
 check(g_inputHostRecoveryEpisodeAttempts.load()==1&&!g_inputHostRecoveryHealth.complete(kRecoveryHealthyCompleteMs),"new MSI mode cannot reset recovery budget using an old-mode healthy ACK window");
 check(g_inputHostRecoveryRolling[0].load()==rolling,"MSI mode maintenance cannot erase the existing rolling recovery budget");
 for(int n=0;n<9;++n){Sleep(250);auto sample=readVerifiedCurrent(0);fullPipeSubmitSource(sample,true,false);}
 check(g_inputHostRecoveryHealth.complete(kRecoveryHealthyCompleteMs),"actual current-mode reports establish their own two-second accepted coverage");
 inputHostRecoveryMaintenanceTick();
 check(g_inputHostRecoveryEpisodeAttempts.load()==0&&g_inputHostRecoveryRolling[0].load()==rolling,"only actual current-mode healthy coverage may settle episode budget while retaining rolling limits");
 cases.back()["oldMode"]=oldMode;cases.back()["newMode"]=g_msiFeedbackModeGeneration.load();cases.back()["oldThrough"]=oldThrough;cases.back()["oldContinuity"]=oldContinuity;cases.back()["newContinuity"]=freshMode.readContinuityGeneration;cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 }
 cases.push_back({{"name","actual-focus-neutral-current-source-verifies-without-health-credit"}});prepare();fixtureFrameFirstAck=true;
 {
  // Establish the actual Host first, then acquire the existing cached isolation
  // owner bound to that Host epoch/source/power. No HWND/UI or input operations.
  check(inputHostStart(),"foreground-neutral scenario establishes its actual owned Host");
  struct CachedRadialOwnerScope{CachedRadialOwnerScope(){radialInputIsolationOwnership(true);}~CachedRadialOwnerScope(){radialInputIsolationInvalidate();}} radialOwner;
  check(radialHasInputOwner()&&gamepadUiInputEligible(),"cached radial ownership supplies actual foreground-neutral eligibility without window operations");
  json neutralFrames=json::array();
  for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();if(n==kInputHostFreshNeutralFramesRequired)sample.pad.wButtons=XINPUT_GAMEPAD_A;neutralFrames.push_back(fullPipeSubmitSource(sample,true,true));}
  const auto focusCandidate=g_virtualStartProtection.view(startProtectionClock);const auto focusSequence=g_inputHostSequence;const auto focusEpoch=g_inputHostEpoch;
  check(neutralFrames.back().value("hostSubmission",std::string{})=="frame-accepted"&&neutralFrames.back().value("focusIsolationSemanticNeutral",false)&&g_inputHostActive&&focusCandidate.phase=="verifying"&&focusCandidate.verificationPending&&focusCandidate.candidateOutcome=="success"&&focusCandidate.verificationRemainingMs==10000,"actual accepted current-source foreground neutral supplies a success candidate");
  check(g_inputHostRecoveryHealth.since()==0&&g_inputHostRecoveryHealth.through()==0&&g_inputHostHealthySinceTick.load()==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"eligible foreground-neutral verification does not credit shared non-neutral recovery health or budget");
  startProtectionClock+=focusCandidate.verificationRemainingMs-1;auto focusRead=readVerifiedCurrent();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
  check(g_virtualStartProtection.view(startProtectionClock).locked&&g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs==1&&g_inputHostSequence==focusSequence,"foreground-neutral candidate remains locked at9999 with continuous reads and no extra ACK");
  ++startProtectionClock;const auto focusNext=readVerifiedCurrent();
  check(focusNext.readContinuityGeneration==focusRead.readContinuityGeneration,"healthy foreground-neutral reads retain the accepted source lineage");
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
  const auto focusFinal=g_virtualStartProtection.view(startProtectionClock);
  check(focusFinal.requestId==focusCandidate.requestId&&focusFinal.phase=="success"&&!focusFinal.locked&&g_inputHostSequence==focusSequence&&g_inputHostEpoch==focusEpoch&&createCount()==1&&stopCount()==0,"actual current-lineage foreground-neutral ACK verifies at10000 without extra submits or recovery");
  check(g_inputHostRecoveryHealth.since()==0&&g_inputHostRecoveryHealth.through()==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"foreground-neutral final leaves shared health evidence and recovery budget untouched");
  noRecovery("normal focus-neutral verification uses zero recovery queues Stops reservations or episode attempts");
  cases.back()["neutralFrames"]=neutralFrames;cases.back()["acceptedCommandSequence"]=focusSequence;cases.back()["finalCommandSequence"]=g_inputHostSequence;cases.back()["candidate"]={{"phase",focusCandidate.phase},{"outcome",focusCandidate.candidateOutcome}};cases.back()["final"]={{"phase",focusFinal.phase},{"locked",focusFinal.locked}};cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();
 }
 fixtureFrameFirstAck=false;
 cases.push_back({{"name","actual-classified-failed-start-off-on-recovery"}});prepare();const auto original=g_virtualStartProtection.view(startProtectionClock);const auto power=g_powerGeneration.load();
 g_startEntrySeam.pipeFailCommand="HELLO";fixtureStartupTransportError=ERROR_BROKEN_PIPE;
 check(!inputHostStart(),"actual failed HELLO reaches ordinary opening-failed stop");
 const auto failed=g_virtualStartProtection.view(startProtectionClock);const auto initialStops=stopCount(),initialCreates=createCount();const auto saved=ymSettingsReadRaw(YM_SETTINGS_FILE);
 check(failed.phase=="verifying"&&failed.verificationPending&&failed.candidateOutcome=="failure"&&failed.verificationRemainingMs==10000&&failed.fallbackAttempts==0&&g_inputHostSwitchRecovery.pending&&g_inputHostSwitchRecovery.requestId==original.requestId,"classified failed startup enters execution inspection even with a pending fallback offer");timeline("failed-start-candidate");
 g_startEntrySeam.pipeFailCommand.clear();fixtureStartupTransportError=ERROR_SUCCESS;
 g_hidHideLastHealTick=0; // Due-health remains inside the existing read-only fixture boundaries.
 for(const auto elapsed:{0ULL,9999ULL}){
  startProtectionClock=100000+elapsed;const auto sample=readVerifiedCurrent();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(!ymcc::inputHost::switchRecoveryRuntime::executionInspectionCompleteLocked(startProtectionClock),"failed-start execution inspection is incomplete before10000");inputHostSwitchRecoveryMaintenanceLocked();}
  inputHostPerformSwitchRecovery(original.requestId);
  const auto view=g_virtualStartProtection.view(startProtectionClock);
  check(!g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==0&&stopCount()==initialStops&&createCount()==initialCreates&&!g_inputHostProcess&&g_inputHostRecoveryEpisodeAttempts.load()==0&&view.fallbackAttempts==0&&!g_inputHostSwitchRecovery.spent&&!g_inputHostSwitchRecovery.stopInvoked,"failed startup performs no fallback queue Stop create or budget charge before10000");
  check(sample.live&&sample.readContextStable&&g_inputReady.load()&&!g_inputCaptureStop.load()&&fixtureXinputSourceLive&&g_xinputAdmittedMask==0&&!g_xinputPhysicalSourceLocked&&view.locked&&view.requestId==original.requestId&&view.remainingMs==original.remainingMs-elapsed,"controlled physical read remains live while released source admission and player clock stay independent");
 }
 timeline("failed-start-at9999");++startProtectionClock;readVerifiedCurrent();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(ymcc::inputHost::switchRecoveryRuntime::executionInspectionCompleteLocked(startProtectionClock),"failed-start execution inspection completes exactly at10000");inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"failed-start retry queues exactly at10000 before central reservation or budget charge");timeline("failed-start-at10000-before-reserve");
 inputHostPerformSwitchRecovery(original.requestId);
 check(g_inputHostSwitchRecovery.spent&&g_inputHostSwitchRecovery.stopInvoked&&!g_inputHostSwitchRecovery.executionCleanup&&!g_inputHostSwitchRecoveryPending.load()&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1,"actual controlled stop then start consumes exactly one shared attempt and central reservation");
 check(stopCount()==initialStops+1&&traced("stop:switch-recovery")&&createCount()==initialCreates+1,"startup failure without execution Stop retains the ordered stop start fallback");
 check(traced("health:service-query")&&traced("health:readonly-dev-all")&&!fixtureHealthInventoryRead,"forced due-health body executes its exact read-only service query and dev-all inventory leaves");
 check(g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&inputHostConfiguredPersona()=="steamdeck"&&g_realStickTest.virtualTargetEnabled,"recovery restores actual prepared target while preserving logical SteamDeck intent");
 const auto recovered=g_virtualStartProtection.view(startProtectionClock);
 check(recovered.phase=="verifying"&&recovered.candidateOutcome=="failure"&&recovered.verificationRemainingMs==0,"restored target retains its original failed candidate and completed inspection until actual healthy proof");
 check(ymSettingsReadRaw(YM_SETTINGS_FILE)==saved&&g_powerGeneration.load()==power&&recovered.requestId==original.requestId&&recovered.remainingMs==20000&&g_inputHostSwitchRecovery.deadline==100000+ymcc::VirtualStartProtection::maxMs,"recovery preserves settings power request and the original thirty-second deadline");timeline("failed-start-fallback-spent");
 const auto epoch=g_inputHostEpoch;const auto stops=stopCount(),creates=createCount();
 check(!g_virtualStartProtection.admit("disabled",startProtectionClock).accepted&&!g_virtualStartProtection.reserveFallback(original.requestId,startProtectionClock),"player transition and repeated reserve cannot renew an already charged recovery");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryOfferLocked(original.requestId,power,ERROR_BROKEN_PIPE);inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(original.requestId);fullPipeDrainQueuedLifecycle();
 check(g_inputHostEpoch==epoch&&stopCount()==stops&&createCount()==creates&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&g_virtualStartProtection.view(startProtectionClock).remainingMs==recovered.remainingMs,"duplicate failure offer and dispatch cannot Stop Start reserve or extend a charged recovery");
 fixtureFrameFirstAck=true;for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();fullPipeSubmitSource(sample,true,false);}
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&!g_virtualStartProtection.view(startProtectionClock).locked&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1,"actual recovered healthy proof permits the unique final success after an initial failure candidate");timeline("failed-start-healthy-final");
 unsigned initialFinals=0,initialFailures=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-switch-verification"&&event["detail"].value("stage","")=="final"&&event["detail"].value("requestId",0ULL)==original.requestId){++initialFinals;initialFailures+=event["detail"].value("outcome","")=="failure";}
 check(initialFinals==1&&initialFailures==0,"initial failed candidate and recovered healthy proof publish exactly one successful final");
 cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 for(const bool naturalOrder:{false,true}){
 cases.push_back({{"name",naturalOrder?"actual-frame-fault-natural-worker-order-recovery":"actual-verification-window-detects-transport-fault-and-reopens"}});prepare();fixtureFrameFirstAck=true;

 check(inputHostStart(),"verification fault scenario creates its first actual native target");
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();fullPipeSubmitSource(sample,true,false);}
 const auto candidate=g_virtualStartProtection.view(startProtectionClock);check(candidate.phase=="verifying"&&candidate.candidateOutcome=="success"&&candidate.locked,"actual current-frame candidate starts the inspection window without a final");
 for(int n=0;n<4;++n){startProtectionClock+=100;auto sample=readVerifiedCurrent();{std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot=sample;}{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}}
 check(ymcc::inputHost::switchVerificationRuntime::observation.checks==4&&ymcc::inputHost::switchVerificationRuntime::observation.healthyChecks==4,"the first ten seconds performs real current-chain inspections instead of waiting idle");
 g_shortcutRecordingActive=true;startProtectionClock+=100;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(ymcc::inputHost::switchVerificationRuntime::observation.excludedChecks==1&&g_inputHostRecoveryEpisodeAttempts.load()==0&&!g_inputHostSwitchRecoveryPending.load(),"known recording suppression is inspected and excluded without a stop or recovery charge");g_shortcutRecordingActive=false;
 const auto firstEpoch=g_inputHostEpoch;const auto firstRun=g_inputHostRunId;
 injectFrameFault();
 check(g_inputHostTransportFault&&ymcc::inputHost::switchRecoveryRuntime::retryable(g_inputHostSwitchRecoveryTransportError),"real bounded frame transport timeout supplies a classified fault during verification");
 const auto faultToken="fault-stop:frame-receipt-fault:"+std::to_string(firstEpoch);
 if(naturalOrder){
  // Let the actual timed-out source age; cleanup cannot require a fresh source.
  ymcc::input::recoveryHealth::Source cached;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);cached=inputHostRecoveryHealthLatestSourceLocked();}const auto now=GetTickCount64();
  if(now>=cached.started&&now-cached.started<=ymcc::kVirtualOutputSourceWindowMaxAgeMs)
   Sleep(static_cast<DWORD>(ymcc::kVirtualOutputSourceWindowMaxAgeMs+16-(now-cached.started)));
  ymcc::input::recoveryHealth::Source stale;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);stale=inputHostRecoveryHealthLatestSourceLocked();}
  check(!ymcc::input::recoveryHealth::fresh(stale,GetTickCount64(),currentPowerGeneration(),g_xinputTopologyEpoch.load()),"actual timed-out frame leaves cached source stale before natural cleanup dispatch");
  {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);check(!g_inputHostLifecycleWorkQueue.empty()&&g_inputHostLifecycleWorkQueue.front()==faultToken,"actual frame-fault cleanup stays ahead of maintenance recovery work");}
 }else{
  readVerifiedCurrent();{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();inputHostSwitchRecoveryMaintenanceLocked();}
  check(!g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==0&&stopCount()==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"known fault inside inspection cannot queue or charge fallback before its safety token runs");
  check(inputHostConsumeSwitchFaultCleanup(firstEpoch+1)&&stopCount()==0&&g_inputHostEpoch==firstEpoch&&g_inputHostRunId==firstRun,"foreign epoch cleanup token cannot claim or Stop the current Host");
  check(inputHostConsumeSwitchFaultCleanup(firstEpoch),"exact current frame fault is claimed by execution cleanup");
 }
 inputHostRunQueuedLifecycleWork();fullPipeDrainQueuedLifecycle();
 check(stopCount()==1&&traced("stop:switch-execution-fault-cleanup")&&!traced("stop:switch-recovery")&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"one current frame fault has exactly one serialized Stop owner");
 check(g_inputHostSwitchRecovery.requestId==candidate.requestId&&g_inputHostSwitchRecovery.hostEpoch==firstEpoch&&g_inputHostSwitchRecovery.stoppedRunId==firstRun&&g_inputHostSwitchRecovery.stopInvoked&&g_inputHostSwitchRecovery.executionCleanup&&!g_inputHostSwitchRecovery.spent&&g_inputHostSwitchRecovery.awaitingCleanup&&g_inputHostSwitchRecoveryPending.load()&&g_inputHostReleaseQuarantined&&!g_inputHostProcess,"execution Stop owns the exact epoch and run while unproven cleanup retains an unspent fallback");timeline("execution-fault-stop-unspent");
 const auto originalDeadline=g_inputHostSwitchRecovery.deadline;
 startProtectionClock=100000+ymcc::VirtualStartProtection::settleMs-1;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(!ymcc::inputHost::switchRecoveryRuntime::executionInspectionCompleteLocked(startProtectionClock),"frame-fault execution inspection remains incomplete at9999");inputHostSwitchVerificationMaintenanceLocked();inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(candidate.requestId);
 check(!g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==0&&stopCount()==1&&createCount()==1&&!g_inputHostSwitchRecovery.spent&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&fixtureXinputSourceLive&&g_inputReady.load()&&!g_inputCaptureStop.load(),"at9999 execution cleanup permits physical input but no fallback queue duplicate Stop create or budget charge");timeline("execution-cleanup-at9999");
 ++startProtectionClock;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(ymcc::inputHost::switchRecoveryRuntime::executionInspectionCompleteLocked(startProtectionClock),"frame-fault execution inspection completes exactly at10000");inputHostSwitchVerificationMaintenanceLocked();inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(candidate.requestId);
 check(g_virtualStartProtection.view(startProtectionClock).locked&&g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs==0&&!g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==0&&!g_inputHostSwitchRecovery.spent&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&createCount()==1&&stopCount()==1&&g_inputHostReleaseQuarantined,"ten seconds with unfinished fault return cannot reserve charge queue or create a fallback");timeline("inspection-complete-return-unproven");
 if(!naturalOrder){std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot.live=false;}
 // The actual quarantine owner supplies return proof; never clear its state here.
 g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_wakeHoldPhysicalUnhide=false;fullPipeQuarantineProbe();
 check(!g_inputHostReleaseQuarantined&&!g_inputHostProcess&&!g_inputHostPrepared&&g_inputHostEpoch==0,"actual return consumer retires the old owned target before recovery can continue");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 fullPipeDrainQueuedLifecycle();
 check(!g_inputHostProcess&&createCount()==1&&stopCount()==1&&!g_inputHostSwitchRecovery.queued&&!g_inputHostSwitchRecovery.spent&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostSwitchRecovery.deadline==originalDeadline,"proven return with missing or stale source cannot Start reserve or charge fallback");
 readVerifiedCurrent();{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1&&!g_inputHostSwitchRecovery.spent&&g_inputHostSwitchRecovery.executionCleanup&&g_inputHostSwitchRecovery.awaitingCleanup&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"proven execution cleanup and fresh source queue the first unspent fallback only after inspection");timeline("execution-return-before-first-reserve");
 if(naturalOrder)inputHostRunQueuedLifecycleWork();else inputHostPerformSwitchRecovery(candidate.requestId);
 fullPipeDrainQueuedLifecycle();
 check(g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&g_inputHostSwitchRecovery.spent&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&createCount()==2&&stopCount()==1&&!traced("stop:switch-recovery")&&g_inputHostSwitchRecovery.deadline==originalDeadline,"fallback after execution Stop performs only one Start and one central and episode charge");timeline("execution-return-first-fallback-spent");
 const auto stop=std::find(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"stop:switch-execution-fault-cleanup");
 check(stop!=g_startEntrySeam.trace.end()&&std::find(stop+1,g_startEntrySeam.trace.end(),"create-process")!=g_startEntrySeam.trace.end(),"replacement Start occurs strictly after the execution Stop and actual return");
 const auto successorEpoch=g_inputHostEpoch;const auto successorRun=g_inputHostRunId;const auto successorHandle=g_inputHostProcess;const auto commands=g_inputHostSequence;
 check(!g_virtualStartProtection.admit("disabled",startProtectionClock).accepted&&!g_virtualStartProtection.reserveFallback(candidate.requestId,startProtectionClock),"player and repeated dispatch cannot reserve a second repair on the same verifying request");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryOfferLocked(candidate.requestId,currentPowerGeneration(),ERROR_BROKEN_PIPE);inputHostSwitchRecoveryMaintenanceLocked();}
 check(successorEpoch!=firstEpoch&&inputHostQueueLifecycleWork(faultToken.c_str()),"old scoped fault token is replayed only after an actual successor target exists");
 fullPipeDrainQueuedLifecycle();inputHostPerformSwitchRecovery(candidate.requestId);
 check(g_inputHostEpoch==successorEpoch&&g_inputHostRunId==successorRun&&g_inputHostProcess==successorHandle&&g_inputHostSequence==commands&&g_inputHostPrepared&&!g_inputHostTransportFault&&createCount()==2&&stopCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&g_inputHostSwitchRecovery.deadline==originalDeadline,"old fault token and repeated offer cannot Stop mutate reserve or renew the successor Host");
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();fullPipeSubmitSource(sample,true,false);}
 const auto repaired=g_virtualStartProtection.view(startProtectionClock);
 check(repaired.requestId==candidate.requestId&&repaired.phase=="verifying"&&repaired.verificationRemainingMs==0&&repaired.remainingMs==20000&&repaired.fallbackAttempts==1,"repaired candidate neither publishes a contradictory final nor renews inspection or admission time");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&!g_virtualStartProtection.view(startProtectionClock).locked,"only post-repair actual verification emits the one successful final and releases players");timeline("execution-repaired-healthy-final");
 unsigned successes=0,failuresForTicket=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-switch-verification"&&event["detail"].value("stage","")=="final"&&event["detail"].value("requestId",0ULL)==candidate.requestId){successes+=event["detail"].value("outcome","")=="success";failuresForTicket+=event["detail"].value("outcome","")=="failure";}
 check(successes==1&&failuresForTicket==0,"attempt fault followed by recovery never reports both final failure and success");cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 }
 {
 cases.push_back({{"name","actual-post-inspection-fault-spent-return-continuation"}});prepare();fixtureFrameFirstAck=true;
 check(inputHostStart(),"post-inspection fault establishes an actual owned target");
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();fullPipeSubmitSource(sample,true,false);}
 const auto candidate=g_virtualStartProtection.view(startProtectionClock);const auto oldEpoch=g_inputHostEpoch;const auto oldRun=g_inputHostRunId;
 // The first fault arrives AFTER inspection, before the verifier publishes a
 // final. Its actual fault owner therefore enters the charged Stop/Start path.
 startProtectionClock+=candidate.verificationRemainingMs;injectFrameFault();inputHostRunQueuedLifecycleWork();fullPipeDrainQueuedLifecycle();
 check(g_inputHostSwitchRecovery.spent&&!g_inputHostSwitchRecovery.executionCleanup&&g_inputHostSwitchRecovery.stopInvoked&&g_inputHostSwitchRecovery.awaitingCleanup&&g_inputHostSwitchRecovery.pending&&g_inputHostReleaseQuarantined&&!g_inputHostProcess&&g_inputHostSwitchRecovery.hostEpoch==oldEpoch&&g_inputHostSwitchRecovery.stoppedRunId==oldRun&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1,"post-inspection classified fault reserves once and retains the actual charged Stop while return is unproven");
 check(stopCount()==1&&traced("stop:switch-recovery")&&createCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==1,"one current frame fault has exactly one serialized Stop owner");timeline("post-inspection-spent-awaiting-return");
 const auto deadline=g_inputHostSwitchRecovery.deadline;const auto rolling=g_inputHostRecoveryRolling[0].load();const auto lastStart=g_inputHostRecoveryLastStartTick.load();
 startProtectionClock+=5000;readVerifiedCurrent();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(candidate.requestId);
 check(g_inputHostReleaseQuarantined&&!g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==0&&stopCount()==1&&createCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&g_inputHostSwitchRecovery.deadline==deadline,"charged fallback waits for actual late return without another Stop Start reserve charge or deadline");
 g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_wakeHoldPhysicalUnhide=false;fullPipeQuarantineProbe();readVerifiedCurrent();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1&&g_inputHostSwitchRecovery.spent&&g_inputHostSwitchRecovery.awaitingCleanup&&!g_inputHostSwitchRecovery.executionCleanup&&!g_inputHostReleaseQuarantined&&!g_inputHostStartBlocked&&g_inputHostEpoch==0&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&g_inputHostSwitchRecovery.deadline==deadline,"verified late cleanup queues only the continuation of the already charged recovery");timeline("spent-return-proven-continuation-queued");
 inputHostRunQueuedLifecycleWork();fullPipeDrainQueuedLifecycle();
 check(g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&stopCount()==1&&createCount()==2&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&g_inputHostRecoveryRolling[0].load()==rolling&&g_inputHostRecoveryLastStartTick.load()==lastStart&&g_inputHostSwitchRecovery.deadline==deadline&&g_virtualStartProtection.view(startProtectionClock).remainingMs==15000,"late charged continuation performs only its remaining Start with the original reservation cooldown rolling budget and deadline");
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();fullPipeSubmitSource(sample,true,false);}
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&!g_virtualStartProtection.view(startProtectionClock).locked,"actual current output after late charged return permits the unique successful final");
 cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 }
 for(const auto* condition:{"recording","source-missing","power-paused","external-priority","budget-exhausted","stale-request","stale-power","stale-epoch","deadline-expired","unproven-release","nonretryable-error"}){
  cases.push_back({{"name",std::string("actual-switch-recovery-guard/")+condition}});prepare();const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;const auto pwr=g_powerGeneration.load();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryOfferLocked(ticket,pwr,std::strcmp(condition,"nonretryable-error")==0?ERROR_ACCESS_DENIED:ERROR_BROKEN_PIPE);}
  // Exercise each exclusion AFTER the real inspection gate opens, so a
  // guard cannot pass merely because the old fixture never reached retry time.
  startProtectionClock+=ymcc::VirtualStartProtection::settleMs-1;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(!ymcc::inputHost::switchRecoveryRuntime::executionInspectionCompleteLocked(startProtectionClock),"classified execution outcome cannot finish inspection at9999");inputHostSwitchRecoveryMaintenanceLocked();}
  check(queuedRecoveryCount()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"pending offer before inspection queues and reserves no fallback");
  ++startProtectionClock;readVerifiedCurrent();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(ymcc::inputHost::switchRecoveryRuntime::executionInspectionCompleteLocked(startProtectionClock)==(std::strcmp(condition,"nonretryable-error")!=0),"only classified execution receipt completes inspection at10000");}
  if(std::strcmp(condition,"recording")==0)g_shortcutRecordingActive=true;
  if(std::strcmp(condition,"source-missing")==0){std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot={};}
  if(std::strcmp(condition,"power-paused")==0)g_powerLifecycle=PowerLifecycle::Suspended;
  if(std::strcmp(condition,"external-priority")==0)g_externalPriorityVirtualSuspended=true;
  if(std::strcmp(condition,"budget-exhausted")==0)g_inputHostRecoveryEpisodeAttempts=kRecoveryEpisodeMax;
  if(std::strcmp(condition,"stale-request")==0){startProtectionClock+=30000;g_virtualStartProtection.admit("steamdeck",startProtectionClock);}
  if(std::strcmp(condition,"stale-power")==0)g_powerGeneration=pwr+1;
  if(std::strcmp(condition,"stale-epoch")==0)++g_inputHostEpoch;
  if(std::strcmp(condition,"deadline-expired")==0)startProtectionClock+=30000;
  if(std::strcmp(condition,"unproven-release")==0)g_inputHostReleaseQuarantined=true;
  const auto requests=g_inputHostSequence;const auto before=g_virtualStartProtection.view(startProtectionClock);const auto budget=g_inputHostRecoveryEpisodeAttempts.load();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
  inputHostPerformSwitchRecovery(ticket);
  check(!g_inputHostProcess&&g_inputHostSequence==requests,"excluded condition cannot create Host or send target commands");
  check(g_inputHostRecoveryEpisodeAttempts.load()==budget&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&stopCount()==0&&createCount()==0&&queuedRecoveryCount()==0,"excluded condition spends no recovery attempt reservation Stop or create after inspection");
  check(g_virtualStartProtection.view(startProtectionClock).requestId==before.requestId&&g_virtualStartProtection.view(startProtectionClock).remainingMs==before.remainingMs,"excluded condition cannot renew or replace player protection");
  cases.back()["events"]=fixtureEvents;g_shortcutRecordingActive=false;g_externalPriorityVirtualSuspended=false;g_powerLifecycle=PowerLifecycle::Ready;
 }
 // Real owned Stop/return supplies the guard context. Only negative owner
 // metadata and controlled power/clock leaves are varied at the exact hook.
 cases.push_back({{"name","actual-switch-return-hook-ownership-guards"}});prepare();fixtureFrameFirstAck=true;
 check(inputHostStart(),"return-hook guards establish an actual owned Host");
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();fullPipeSubmitSource(sample,true,false);}
 const auto oldEpoch=g_inputHostEpoch;const auto oldRun=g_inputHostRunId;const auto oldPower=currentPowerGeneration();
 injectFrameFault();readVerifiedCurrent();inputHostRunQueuedLifecycleWork();
 check(g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecovery.spent&&g_inputHostSwitchRecovery.executionCleanup&&g_inputHostSwitchRecovery.stopInvoked&&g_inputHostSwitchRecovery.awaitingCleanup&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&!g_inputHostProcess,"return-hook guards retain actual unspent execution cleanup and quarantined return ownership");
 const auto proofState=[](){const auto& r=g_inputHostSwitchRecovery;return json{{"state",state()},{"requestId",r.requestId},{"power",r.power},{"hostEpoch",r.hostEpoch},{"deadline",r.deadline},{"stoppedRunId",r.stoppedRunId},{"pending",r.pending},{"queued",r.queued},{"spent",r.spent},{"executionCleanup",r.executionCleanup},{"stopInvoked",r.stopInvoked},{"fallbackAttempts",g_virtualStartProtection.view(startProtectionClock).fallbackAttempts},{"awaitingCleanup",r.awaitingCleanup},{"atomicPending",g_inputHostSwitchRecoveryPending.load()},{"attempts",g_inputHostRecoveryEpisodeAttempts.load()},{"debtRequired",g_msiSteamDeckReleaseDebt.required()},{"debtPinned",g_msiSteamDeckReleaseDebt.pinned()},{"debtRun",g_msiSteamDeckReleaseDebt.run()},{"debtEpoch",g_msiSteamDeckReleaseDebt.epoch()},{"debtTargets",g_msiSteamDeckReleaseDebt.ownedTargets()},{"debtReason",g_msiSteamDeckReleaseDebt.reason()}};};
 const auto rejectReturn=[&](std::uint64_t epoch,const std::string& run,const char* assertion){std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto before=proofState();const bool accepted=inputHostSwitchRecoveryReturnedLocked(epoch,run);check(!accepted&&g_inputHostStartBlocked&&proofState()==before,assertion);};
 rejectReturn(oldEpoch,oldRun,"matching ownership cannot release StartBlocked while actual quarantine remains unresolved");
 // Mismatched controlled power makes the real return consumer refuse rearm
 // after completing restore/Clear. It supplies a clean but still blocked state,
 // without the fixture clearing quarantine, debt, epoch or StartBlocked.
 g_powerGeneration=oldPower+1;g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_wakeHoldPhysicalUnhide=false;
 inputHostReconcileQuarantineOnLifecycleThread();g_powerGeneration=oldPower;
 check(!g_inputHostReleaseQuarantined&&!g_inputHostProcess&&!g_inputHostPrepared&&!g_hidHideVisibilityJournal.active&&g_inputHostEpoch==0&&g_inputHostStartBlocked&&g_inputHostSwitchRecovery.pending&&g_inputHostSwitchRecovery.hostEpoch==oldEpoch,"actual restore and Clear leave a power-mismatched rearm blocked for isolated proof guards");
 rejectReturn(oldEpoch,oldRun+"-foreign","return hook refuses wrong old runId without clearing StartBlocked or debt");
 rejectReturn(oldEpoch+1,oldRun,"return hook refuses wrong old epoch without clearing StartBlocked or debt");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);++g_inputHostSwitchRecovery.requestId;}
 rejectReturn(oldEpoch,oldRun,"return hook refuses wrong request without clearing StartBlocked or debt");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);--g_inputHostSwitchRecovery.requestId;}
 g_powerGeneration=oldPower+1;rejectReturn(oldEpoch,oldRun,"return hook refuses wrong power without clearing StartBlocked or debt");g_powerGeneration=oldPower;
 startProtectionClock=g_inputHostSwitchRecovery.deadline;rejectReturn(oldEpoch,oldRun,"return hook refuses expired deadline without clearing StartBlocked or debt");
 cases.back()["events"]=fixtureEvents;cases.back()["guardState"]=proofState();cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 cases.push_back({{"name","actual-switch-return-hook-retains-owned-endpoint-debt"}});prepare();fixtureFrameFirstAck=true;
 g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
 g_msiDinput.enabled=true;g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=kMsiClawCapability.pidDInput;g_msiDinputSourceValid=true;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_xinputAdmittedMask=0;g_xinputConnectedMask=0;g_xinputPhysicalSourceMask=0;
 check(inputHostStart(),"MSI debt guard prepares its actual opted-in owned target");
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent(0);fullPipeSubmitSource(sample,true,false);}
 const auto debtEpoch=g_inputHostEpoch;const auto debtRun=g_inputHostRunId;injectFrameFault(0);readVerifiedCurrent(0);inputHostRunQueuedLifecycleWork();
 check(!g_inputHostSwitchRecovery.spent&&g_inputHostSwitchRecovery.executionCleanup&&g_inputHostSwitchRecovery.stopInvoked&&g_inputHostSwitchRecovery.awaitingCleanup&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_msiSteamDeckReleaseDebt.required()&&g_msiSteamDeckReleaseDebt.pinned()&&!g_msiSteamDeckReleaseDebt.ownedTargets().empty(),"actual lost MSI release retains nonempty owned-endpoint debt without charging fallback for return-hook rejection");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto before=proofState();const bool accepted=inputHostSwitchRecoveryReturnedLocked(debtEpoch,debtRun);
  check(!accepted&&g_inputHostStartBlocked&&proofState()==before,"return hook cannot clear StartBlocked or nonempty actual owned-endpoint debt");}
 g_msiSdOwnedPnpSnapshotForTest=[](){return std::pair{true,std::set<std::string>{}};};
 inputHostReconcileQuarantineOnLifecycleThread();g_msiSdOwnedPnpSnapshotForTest={};
 check(!g_msiSteamDeckReleaseDebt.required()&&!g_inputHostReleaseQuarantined&&!g_inputHostProcess&&!g_inputHostPrepared,"only the actual owned-absence and physical-return consumer clears MSI debt");
 cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 for(const auto* held:{"recording","budget-exhausted"}){
  cases.push_back({{"name",std::string("actual-frame-fault-safety-cleanup/")+held}});prepare();fixtureFrameFirstAck=true;
  check(inputHostStart(),"held recovery safety case establishes an actual owned target");
  for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent();fullPipeSubmitSource(sample,true,false);}
  injectFrameFault();if(std::strcmp(held,"recording")==0)g_shortcutRecordingActive=true;else g_inputHostRecoveryEpisodeAttempts=kRecoveryEpisodeMax;
  const auto charge=g_inputHostRecoveryEpisodeAttempts.load();const auto request=g_virtualStartProtection.view(startProtectionClock);const auto faultEpoch=g_inputHostEpoch;
  inputHostRunQueuedLifecycleWork();fullPipeDrainQueuedLifecycle();
  check(stopCount()==1&&traced("stop:switch-execution-fault-cleanup")&&!traced("stop:switch-recovery")&&g_inputHostSwitchRecovery.hostEpoch==faultEpoch&&g_inputHostSwitchRecovery.stopInvoked&&g_inputHostSwitchRecovery.executionCleanup,"withheld recovery leaves exactly one serialized execution safety Stop");
  check(createCount()==1&&!g_inputHostProcess&&g_inputHostFaultHoldsStart.load()&&g_inputHostRecoveryEpisodeAttempts.load()==charge&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&!g_inputHostSwitchRecovery.spent,"expected pause or exhausted budget cannot suppress execution cleanup or charge a replacement Start");
  startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
  check(createCount()==1&&stopCount()==1&&queuedRecoveryCount()==0&&g_inputHostRecoveryEpisodeAttempts.load()==charge&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"held fault return after inspection still cannot queue reserve charge or create a fallback");
  g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_wakeHoldPhysicalUnhide=false;fullPipeQuarantineProbe();readVerifiedCurrent();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
  inputHostPerformSwitchRecovery(request.requestId);fullPipeDrainQueuedLifecycle();
  check(!g_inputHostReleaseQuarantined&&!g_inputHostProcess&&createCount()==1&&stopCount()==1&&queuedRecoveryCount()==0&&g_inputHostRecoveryEpisodeAttempts.load()==charge&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"known guard after proven return and fresh source still withholds fallback while preserving its safety Stop");
  check(g_virtualStartProtection.view(startProtectionClock).requestId==request.requestId&&g_virtualStartProtection.view(startProtectionClock).remainingMs==request.remainingMs-ymcc::VirtualStartProtection::settleMs,"held recovery safety cleanup cannot renew the player deadline");timeline("guarded-execution-cleanup-after-inspection");
  cases.back()["events"]=fixtureEvents;g_shortcutRecordingActive=false;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;
 }
 }
 for(const int family:{1,2,0})for(const std::string condition:{"resume","return-hook-guards","stale-request-at-return","stale-power-at-return","deadline-at-return","recording","external-priority","power-not-ready","input-not-ready","queued-recording","queued-external-priority","queued-power-not-ready","queued-stale-request","queued-stale-power","queued-deadline","pause-to-deadline"}){
 if(cliBoundaryOnly==4 && (family!=0 || condition!="resume"))continue;
 if(family!=1&&condition!="resume")continue;
 cases.push_back({{"name",family==1?"ROG/normal-persona-transition-retains-timed-out-old-host":family==2?"GPD/normal-persona-transition-retains-timed-out-old-host":"MSI/normal-persona-transition-retains-timed-out-old-host"}});
 if(condition!="resume")cases.back()["name"]=cases.back()["name"].get<std::string>()+"/"+condition;
 prepare();fixtureFrameFirstAck=true;
 if(family==0){
  g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
  g_msiDinput.enabled=true;g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=kMsiClawCapability.pidDInput;g_msiDinputSourceValid=true;
  g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_xinputAdmittedMask=0;g_xinputConnectedMask=0;g_xinputPhysicalSourceMask=0;
 }
 if(family==2){g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.manufacturer="GPD";g_machineIdentity.product="G1617";}
 check(inputHostStart(),"normal transition establishes its original actual target");
 for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent(family);fullPipeSubmitSource(sample,true,false);}
 const auto candidate=g_virtualStartProtection.view(startProtectionClock);startProtectionClock+=candidate.verificationRemainingMs;readVerifiedCurrent(family);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&!g_virtualStartProtection.view(startProtectionClock).locked,"normal transition begins only after previous actual input and final verification");
 reg_fs();const auto before=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));auto next=targetDocument("dualsense-edge",2);
 const auto committed=writeTarget(before,next);check(committed.value("ok",false),"normal persona transition enters through actual player settings admission");inputCaptureRefreshRealStickSettings(true);
 const auto request=g_virtualStartProtection.view(startProtectionClock);const auto oldHandle=g_inputHostProcess;const auto oldEpoch=g_inputHostEpoch;const auto oldRun=g_inputHostRunId;
 check(request.persona=="dualsense-edge"&&request.phase=="pending"&&inputHostConfiguredPersona()=="dualsense-edge"&&g_inputHostBoundPersona=="steamdeck","new persona intent preserves old target until serialized execution");
 const auto requestAt=startProtectionClock,oldPower=currentPowerGeneration();
 const auto hardDeadline=requestAt+ymcc::VirtualStartProtection::maxMs;
 const auto queuedExecutionCount=[&](){std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
  return static_cast<unsigned>(std::count_if(g_inputHostLifecycleWorkQueue.begin(),g_inputHostLifecycleWorkQueue.end(),[&](const auto& reason){return reason=="switch-execution-return:"+std::to_string(request.requestId);}));};
 const auto pipeCount=[](){return static_cast<unsigned>(std::count_if(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),[](const auto& entry){return entry.rfind("pipe:",0)==0;}));};
 const auto returnProofState=[&](){const auto& r=ymcc::inputHost::switchExecutionReturnRuntime::state;
  return json{{"state",state()},{"requestId",r.requestId},{"power",r.power},{"oldEpoch",r.oldEpoch},{"oldRunId",r.oldRunId},{"persona",r.persona},
   {"pending",r.pending},{"returned",r.returned},{"queued",r.queued},{"atomicPending",g_inputHostSwitchExecutionReturnPending.load()},
   {"faultHold",g_inputHostFaultHoldsStart.load()},{"debtRequired",g_msiSteamDeckReleaseDebt.required()},{"debtTargets",g_msiSteamDeckReleaseDebt.ownedTargets()},
   {"fallbackAttempts",g_virtualStartProtection.view(startProtectionClock).fallbackAttempts},{"episodeAttempts",g_inputHostRecoveryEpisodeAttempts.load()}};};
 // Keep a real owned MSI endpoint debt even if the old Host exits later.
 if(family==0)g_startEntrySeam.msiSdReleaseAbsent=false;
 g_startEntrySeam.hostExitWait=WAIT_TIMEOUT;const bool changed=inputHostStart();
 const auto observed=state();const auto creates=createCount();const auto stops=stopCount();
 check(!changed&&creates==1&&stops==1&&g_inputHostProcess==oldHandle&&g_inputHostEpoch==oldEpoch&&g_inputHostRunId==oldRun,"normal persona transition cannot overwrite an old Host whose exit is unproven");
 check(!g_inputHostActive&&g_inputHostPipeRetired&&g_virtualStartProtection.view(startProtectionClock).locked&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"normal delayed return keeps old ownership nonpublishing and preserves player request without fallback reservation");
 const auto waiting=g_virtualStartProtection.view(startProtectionClock);const auto waitingStops=stopCount();
 for(int n=0;n<8;++n)check(!inputHostStart(),"duplicate internal Start cannot replace an owned normal return wait");
 check(stopCount()==waitingStops&&createCount()==1&&g_virtualStartProtection.view(startProtectionClock).requestId==waiting.requestId&&g_virtualStartProtection.view(startProtectionClock).remainingMs==waiting.remainingMs,"duplicate normal return attempts neither repeat Stop nor renew the player cycle");
 cases.back()["transitionEvidence"]={{"changed",changed},{"createCount",creates},{"stopCount",stops},{"oldEpoch",oldEpoch},{"observedEpoch",g_inputHostEpoch},{"oldRunId",oldRun},{"sameOldHandle",g_inputHostProcess==oldHandle},{"observed",observed},{"requestId",request.requestId}};
 {
  std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto& r=ymcc::inputHost::switchExecutionReturnRuntime::state;
  check(r.pending&&!r.returned&&!r.queued&&g_inputHostSwitchExecutionReturnPending.load()&&r.requestId==request.requestId&&r.power==oldPower&&r.oldEpoch==oldEpoch&&r.oldRunId==oldRun&&r.persona==request.persona,
   "normal delayed return captures only the exact original request power epoch runId and persona");
  check(g_virtualStartProtection.canContinueExecution(request.requestId,request.persona,startProtectionClock)&&!g_virtualStartProtection.canContinueExecution(request.requestId+1,request.persona,startProtectionClock)&&!g_virtualStartProtection.canContinueExecution(request.requestId,"steamdeck",startProtectionClock),
   "public continuation gate refuses a foreign ticket or persona without replacing the admitted request");
  const auto before=returnProofState();
  check(!inputHostSwitchExecutionReturnedLocked(oldEpoch,oldRun)&&returnProofState()==before,
   "even the exact normal old token cannot authorize Start while the actual owned return is unresolved");
 }
 startProtectionClock+=2000;
 const bool rejectAtReturn=condition=="stale-request-at-return"||condition=="stale-power-at-return"||condition=="deadline-at-return";
 const auto retireRequest=[&](){startProtectionClock=hardDeadline;const auto replacement=g_virtualStartProtection.admit(request.persona,startProtectionClock);
  check(replacement.accepted&&replacement.created&&replacement.requestId!=request.requestId&&g_virtualStartProtection.canContinueExecution(replacement.requestId,request.persona,startProtectionClock)&&!g_virtualStartProtection.canContinueExecution(request.requestId,request.persona,startProtectionClock),
   "fresh same-persona admission cannot lend its live thirty-second budget to the retired normal request");};
 const auto expireRequest=[&](){startProtectionClock=hardDeadline-1;
  check(g_virtualStartProtection.canContinueExecution(request.requestId,request.persona,startProtectionClock)&&g_virtualStartProtection.view(startProtectionClock).remainingMs==1,
   "normal continuation retains exactly one millisecond strictly before its original thirty-second cap");
  ++startProtectionClock;
  check(!g_virtualStartProtection.canContinueExecution(request.requestId,request.persona,startProtectionClock)&&g_virtualStartProtection.view(startProtectionClock).phase=="expired"&&g_virtualStartProtection.view(startProtectionClock).remainingMs==0,
   "normal continuation loses authority at exactly thirty seconds without renewing admission");};
 if(condition=="stale-request-at-return")retireRequest();
 if(condition=="stale-power-at-return")g_powerGeneration=oldPower+1;
 if(condition=="return-hook-guards"||family==0)g_shortcutRecordingActive=true;
 if(condition=="deadline-at-return")expireRequest();
 g_startEntrySeam.hostExitWait=WAIT_OBJECT_0;
 if(g_inputHostProcess==oldHandle){
  check(SetEvent(oldHandle)!=FALSE,"fixture old Host exit is supplied only by signaling its owned process event");
  g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_wakeHoldPhysicalUnhide=false;
  if(family==0){
   const auto targets=g_msiSteamDeckReleaseDebt.ownedTargets();const auto restores=fullPipeRestoreCount();
   check(g_msiSteamDeckReleaseDebt.required()&&g_msiSteamDeckReleaseDebt.pinned()&&!targets.empty()&&g_msiSteamDeckReleaseDebt.epoch()==oldEpoch&&g_msiSteamDeckReleaseDebt.run()==oldRun,
    "normal MSI SteamDeck retirement pins nonempty actual old-tuple owned endpoint debt");
   g_msiSdOwnedPnpSnapshotForTest=[](){return std::pair{false,std::set<std::string>{}};};fullPipeQuarantineProbe();
   check(!g_inputHostProcess&&g_inputHostReleaseQuarantined&&g_msiSteamDeckReleaseDebt.required()&&g_msiSteamDeckReleaseDebt.ownedTargets()==targets&&fullPipeRestoreCount()==restores&&!ymcc::inputHost::switchExecutionReturnRuntime::state.returned&&createCount()==1,
    "normal MSI old Host exit plus incomplete empty PnP cannot prove owned return or restore physical visibility");
   g_msiSdOwnedPnpSnapshotForTest=[targets](){return std::pair{true,targets};};fullPipeQuarantineProbe();
   check(g_inputHostReleaseQuarantined&&g_msiSteamDeckReleaseDebt.required()&&g_msiSteamDeckReleaseDebt.ownedTargets()==targets&&fullPipeRestoreCount()==restores&&!ymcc::inputHost::switchExecutionReturnRuntime::state.returned,
    "normal MSI complete PnP retaining the pinned SD endpoint cannot authorize the new persona");
   g_msiSdOwnedPnpSnapshotForTest=[](){return std::pair{true,std::set<std::string>{}};};g_startEntrySeam.hidHideRestoreResult=false;fullPipeQuarantineProbe();
   check(g_inputHostReleaseQuarantined&&g_msiSteamDeckReleaseDebt.required()&&g_msiSteamDeckReleaseDebt.ownedTargets()==targets&&fullPipeRestoreCount()==restores+1&&!ymcc::inputHost::switchExecutionReturnRuntime::state.returned,
    "normal MSI proven owned absence still cannot bypass a failed actual physical restore");
   g_startEntrySeam.hidHideRestoreResult=true;fullPipeQuarantineProbe();g_msiSdOwnedPnpSnapshotForTest={};
   check(!g_msiSteamDeckReleaseDebt.required()&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&fullPipeRestoreCount()==restores+2,
    "normal MSI only complete owned PnP absence and successful production physical restore retire the debt");
  }else fullPipeQuarantineProbe();
  readVerifiedCurrent(family);
 }
 if(condition=="return-hook-guards"){
  // The actual return owner completed under a recording pause. Wrong tokens
  // must neither alter that proof nor authorize queued IO during the pause.
  readVerifiedCurrent(family);
  check(!g_inputHostProcess&&!g_inputHostEpoch&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&!g_msiSteamDeckReleaseDebt.required()&&(g_inputHostStartBlocked||family==0&&g_inputHostFaultHoldsStart.load())&&ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&ymcc::inputHost::switchExecutionReturnRuntime::state.returned,
   "actual paused cleanup retains exact normal return proof without prematurely claiming Start");
  const auto rejectToken=[&](std::uint64_t epoch,const std::string& run,const char* assertion){
   std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto before=returnProofState();
   check(!inputHostSwitchExecutionReturnedLocked(epoch,run)&&returnProofState()==before,assertion);
   inputHostSwitchExecutionReturnMaintenanceLocked();
   check(ymcc::inputHost::switchExecutionReturnRuntime::state.returned&&!ymcc::inputHost::switchExecutionReturnRuntime::state.queued&&queuedExecutionCount()==0&&stopCount()==1&&createCount()==1,
    "rejected normal old token cannot queue a continuation or repeat Stop or Start");};
  rejectToken(0,oldRun,"normal return refuses a zero old epoch even after actual ownership cleanup");
  rejectToken(oldEpoch+1,oldRun,"normal return refuses a foreign old epoch even after actual ownership cleanup");
  rejectToken(oldEpoch,oldRun+"-foreign","normal return refuses a foreign old runId even after actual ownership cleanup");
  rejectToken(oldEpoch,"","normal return refuses an empty old runId even after actual ownership cleanup");
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   check(inputHostSwitchExecutionReturnedLocked(oldEpoch,oldRun),"only the exact captured normal old tuple preserves idempotent proof after actual cleanup");}
  g_shortcutRecordingActive=false;
 }
 if(family==0)g_shortcutRecordingActive=false;
 const bool queuedRetirement=condition=="queued-stale-request"||condition=="queued-stale-power"||condition=="queued-deadline";
 const bool queuedPause=condition=="queued-recording"||condition=="queued-external-priority"||condition=="queued-power-not-ready";
 const auto queueOnce=[&](){for(int n=0;n<3;++n){std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
  check(queuedExecutionCount()==1&&ymcc::inputHost::switchExecutionReturnRuntime::state.queued&&stopCount()==1&&createCount()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,
   "repeated normal return maintenance queues exactly one remaining Start without Stop or fallback");};
 if(queuedRetirement||queuedPause)queueOnce();
 if(condition=="queued-stale-request")retireRequest();
 if(condition=="queued-stale-power")g_powerGeneration=oldPower+1;
 if(condition=="queued-deadline")expireRequest();
 if(rejectAtReturn||queuedRetirement){
  const auto before=g_virtualStartProtection.view(startProtectionClock);const auto commands=pipeCount();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   check(!inputHostSwitchExecutionReturnedLocked(oldEpoch,oldRun),"exact old normal tuple cannot revive a retired request power or thirty-second budget");
   // A queued stale item must be retired by its actual dispatch consumer.
   if(!queuedRetirement)inputHostSwitchExecutionReturnMaintenanceLocked();}
  fullPipeDrainQueuedLifecycle();inputHostPerformSwitchExecutionReturn(request.requestId);
  const auto after=g_virtualStartProtection.view(startProtectionClock);
  check(!g_inputHostProcess&&!g_inputHostPrepared&&g_inputHostStartBlocked&&!g_inputHostSwitchExecutionReturnPending.load()&&!ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&!ymcc::inputHost::switchExecutionReturnRuntime::state.queued&&queuedExecutionCount()==0&&stopCount()==1&&createCount()==1&&pipeCount()==commands,
   "retired normal continuation cannot create or command a target through either maintenance or delayed dispatch");
  check(after.requestId==before.requestId&&after.remainingMs==before.remainingMs&&after.elapsedMs==before.elapsedMs&&after.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&queuedRecoveryCount()==0,
   "retired normal old token cannot borrow a fresh request clock or spend fallback");
  g_powerGeneration=oldPower;inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  check(createCount()==1&&stopCount()==1&&!g_inputHostSwitchExecutionReturnPending.load(),"restoring old power cannot resurrect an already retired normal continuation");
  cases.back()["events"]=fixtureEvents;cases.back()["returnProof"]=returnProofState();cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;continue;
 }
 check(!g_inputHostProcess&&!g_inputHostEpoch&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&!g_msiSteamDeckReleaseDebt.required()&&ymcc::inputHost::switchExecutionReturnRuntime::state.returned&&g_inputHostSwitchExecutionReturnPending.load(),
  "actual old return preserves a blocked normal continuation until its sole remaining Start is claimed");
 const bool paused=condition=="recording"||condition=="external-priority"||condition=="power-not-ready"||condition=="input-not-ready"||condition=="pause-to-deadline"||queuedPause;
 if(paused){
  const auto settings=ymSettingsReadRaw(YM_SETTINGS_FILE);const auto commands=pipeCount();const auto sequence=g_inputHostSequence;
  g_shortcutRecordingActive=condition=="recording"||condition=="queued-recording"||condition=="pause-to-deadline";
  g_externalPriorityVirtualSuspended=condition=="external-priority"||condition=="queued-external-priority";
  if(condition=="power-not-ready"||condition=="queued-power-not-ready")g_powerLifecycle=PowerLifecycle::Suspended;
  if(condition=="input-not-ready")g_inputReady=false;
  // Suppressing a queued item must release its queued claim; otherwise the
  // actual dispatcher strands normal continuation after external priority ends.
  if(queuedPause){fullPipeDrainQueuedLifecycle();
   check(ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&!ymcc::inputHost::switchExecutionReturnRuntime::state.queued&&queuedExecutionCount()==0,
    "normal continuation paused after queue dispatch remains available for the same-request resume");}
  for(int n=0;n<2;++n){startProtectionClock+=1500;auto sample=readVerifiedCurrent(family);const auto submitted=fullPipeSubmitSource(sample,true,false);
   inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();const auto view=g_virtualStartProtection.view(startProtectionClock);
   check(submitted.value("hostSubmission",std::string{})!="frame-accepted"&&!g_inputHostProcess&&g_inputHostStartBlocked&&g_inputHostSequence==sequence&&pipeCount()==commands&&createCount()==1&&stopCount()==1&&queuedExecutionCount()==0&&queuedRecoveryCount()==0,
    "recording external priority or non-Ready pause after real return sends no target commands and repeats no Stop or Start");
   check(view.requestId==request.requestId&&view.elapsedMs==startProtectionClock-requestAt&&view.remainingMs==hardDeadline-startProtectionClock&&view.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&!g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecoveryPending.load()&&!g_inputHostSwitchRecovery.spent&&ymSettingsReadRaw(YM_SETTINGS_FILE)==settings,
    "normal return pause preserves private intent and its original shrinking clock without fallback writes or charges");}
  g_shortcutRecordingActive=false;g_externalPriorityVirtualSuspended=false;g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;
  if(condition=="pause-to-deadline"){
   expireRequest();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();inputHostPerformSwitchExecutionReturn(request.requestId);
   check(createCount()==1&&stopCount()==1&&!g_inputHostProcess&&!g_inputHostSwitchExecutionReturnPending.load()&&g_virtualStartProtection.view(startProtectionClock).requestId==request.requestId&&g_virtualStartProtection.view(startProtectionClock).remainingMs==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,
    "resuming a normal return pause at the original thirty-second cap cannot renew time or create a target");
   cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();fixtureFrameFirstAck=false;continue;
  }
 }
 queueOnce();
 inputHostPerformSwitchExecutionReturn(request.requestId+1);
 check(createCount()==1&&stopCount()==1&&queuedExecutionCount()==1&&ymcc::inputHost::switchExecutionReturnRuntime::state.pending,
  "a foreign queued ticket cannot claim or consume the exact normal request's remaining Start");
 inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
 // Delayed duplicate dispatch and maintenance may observe the completed claim.
 if(createCount()==2&&g_inputHostPrepared)for(int n=0;n<3;++n){inputHostPerformSwitchExecutionReturn(request.requestId);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();}
 check(createCount()==2&&stopCount()==1&&!g_inputHostSwitchExecutionReturnPending.load()&&!ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&queuedExecutionCount()==0&&g_virtualStartProtection.view(startProtectionClock).elapsedMs==startProtectionClock-requestAt&&g_virtualStartProtection.view(startProtectionClock).remainingMs==hardDeadline-startProtectionClock,
  "same-request normal resume claims only one Start with no duplicate Stop create or renewed clock");
 check(createCount()==2&&g_inputHostProcess&&g_inputHostEpoch!=oldEpoch&&g_inputHostBoundPersona=="dualsense-edge"&&g_inputHostPrepared&&g_inputHostNeutralized&&g_virtualStartProtection.view(startProtectionClock).requestId==request.requestId&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"normal delayed persona execution resumes same player request after proven old return without fallback");
 cases.back()["afterOldExit"]={{"state",state()},{"createCount",createCount()},{"requestId",g_virtualStartProtection.view(startProtectionClock).requestId},{"fallbackAttempts",g_virtualStartProtection.view(startProtectionClock).fallbackAttempts},{"episodeAttempts",g_inputHostRecoveryEpisodeAttempts.load()}};
 if(createCount()==2&&g_inputHostPrepared){
  for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=readVerifiedCurrent(family);fullPipeSubmitSource(sample,true,false);}
  const auto ready=g_virtualStartProtection.view(startProtectionClock);
  check(ready.requestId==request.requestId&&ready.phase=="verifying"&&ready.candidateOutcome=="success"&&ready.fallbackAttempts==0,"normal remaining Start waits for its actual current-source accepted execution receipt");
  startProtectionClock+=ready.verificationRemainingMs;readVerifiedCurrent(family);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  const auto final=g_virtualStartProtection.view(startProtectionClock);
  check(final.requestId==request.requestId&&final.phase=="success"&&!final.locked&&final.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"normal delayed return completes through the unique verified final with zero fallback");
  unsigned finals=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-switch-verification"&&event["detail"].value("stage","")=="final"&&event["detail"].value("requestId",0ULL)==request.requestId)++finals;
  check(finals==1,"normal delayed return publishes exactly one final for its original player request");
  cases.back()["finalExecution"]={{"requestId",final.requestId},{"phase",final.phase},{"locked",final.locked},{"fallbackAttempts",final.fallbackAttempts},{"elapsedMs",final.elapsedMs},{"stopCount",stopCount()},{"createCount",createCount()}};
 }
 cases.back()["events"]=fixtureEvents;cases.back()["cleanup"]=cleanup();
 // Only release the fixture's own old event if the defective path lost it.
 // This occurs after observations; it does not supply any return proof.
 if(changed&&oldHandle&&oldHandle!=g_inputHostProcess)CloseHandle(oldHandle);
 fixtureFrameFirstAck=false;
 }
 // Bounded common-self-heal cases extend this original mode only.
 if(cliBoundaryOnly!=4){
 {
 cases.push_back({{"name","actual-local-owned-hidden-missing-single-fallback"}});
 const auto candidate=startLocalOwned();const auto ticket=candidate.requestId;const auto power=currentPowerGeneration();
 const auto deadline=startProtectionClock+candidate.remainingMs;const auto settings=ymSettingsReadRaw(YM_SETTINGS_FILE);
 publishLocalReadback();offerLocal(ticket,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);
 check(g_inputHostSwitchRecovery.deadline==deadline,"owned visibility offer preserves the original thirty-second deadline");
 startProtectionClock+=9999;readVerifiedCurrent();publishLocalReadback();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);
 check(stopCount()==0&&createCount()==1&&queuedRecoveryCount()==0&&!g_inputHostSwitchRecovery.spent&&
  g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&
  g_virtualStartProtection.view(startProtectionClock).locked,
  "owned hidden loss at9999 holds the original inspection with no queue Stop Start or budget charge");
 ++startProtectionClock;readVerifiedCurrent();publishLocalReadback();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==0,
  "owned hidden loss queues exactly one original recovery token at10000 before charging");
 inputHostPerformSwitchRecovery(ticket);
 if(g_inputHostSwitchRecovery.awaitingCleanup)fullPipeQuarantineProbe();
 fullPipeDrainQueuedLifecycle();
 check(stopCount()==1&&createCount()==2&&traced("stop:switch-recovery")&&g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&
  g_inputHostSwitchRecovery.spent&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1,
  "actual owned hidden loss performs one serialized Stop cleanup and Start with one shared and central charge");
 const auto stopped=std::find(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"stop:switch-recovery");
 check(stopped!=g_startEntrySeam.trace.end()&&std::find(stopped,g_startEntrySeam.trace.end(),"create-process")!=g_startEntrySeam.trace.end(),
  "owned hidden fallback Stops its old target before creating the replacement Host");
 check(currentPowerGeneration()==power&&ymSettingsReadRaw(YM_SETTINGS_FILE)==settings&&
  g_virtualStartProtection.view(startProtectionClock).requestId==ticket&&g_inputHostSwitchRecovery.deadline==deadline&&
  g_virtualStartProtection.view(startProtectionClock).remainingMs==20000,
  "owned hidden recovery keeps player intent power ticket settings and original remaining budget");
 publishLocalReadback();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryOfferLocked(ticket,power,ERROR_SUCCESS,false,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(stopCount()==1&&createCount()==2&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1,
  "spent owned hidden recovery cannot repeat after a second current missing-target readback");
 publishLocalReadback(false);acceptLocalFrames();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&!g_virtualStartProtection.view(startProtectionClock).locked,
  "actual replacement accepted frame and healthy stamped readback finish the original verification");
 timeline("owned-hidden-single-fallback-final");finishLocalCase();
 }
 for(const std::string condition:{"unknown-readback","cloak-off-only","journal-selected-identity-mismatch"}){
 cases.push_back({{"name",std::string("actual-local-owned-hidden-guard/")+condition}});
 const auto candidate=startLocalOwned();const auto ticket=candidate.requestId;
 publishLocalReadback(condition!="cloak-off-only",condition!="unknown-readback",condition!="cloak-off-only");
 if(condition=="journal-selected-identity-mismatch"){
  std::lock_guard<std::mutex> lock(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity.physicalIdentity+="-selected-other";
 }
 const auto commands=g_inputHostSequence;const auto io=forbiddenIo;const auto reads=inertXinputReads;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultCurrentLocked(ticket)==InputHostSwitchLocalFault::None,
   "unknown readback cloak-off alone or a different selected physical identity cannot diagnose an owned hidden fault");
  inputHostSwitchRecoveryOfferLocked(ticket,currentPowerGeneration(),ERROR_SUCCESS,false,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);
  inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);
 check(g_inputHostSequence==commands&&forbiddenIo==io&&inertXinputReads==reads,
  "rejected local visibility evidence performs no command or physical observation");
 noRecovery("rejected local visibility evidence produces zero queues Stops Starts reservations or attempts");
 finishLocalCase();
 }
 {
 cases.push_back({{"name","actual-local-offer-unknown-holds-healthy-resolves-sticky"}});
 const auto candidate=startLocalOwned();const auto ticket=candidate.requestId;
 const auto deadline=startProtectionClock+candidate.remainingMs;
 publishLocalReadback();offerLocal(ticket,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);
 startProtectionClock+=candidate.verificationRemainingMs;readVerifiedCurrent();publishLocalReadback();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1,"current local fault queues its original fallback after inspection");
 // A failed read replaces authority with UNKNOWN; it cannot mean repaired.
 publishLocalReadback(true,false,true);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultCurrentLocked(ticket)==InputHostSwitchLocalFault::None&&
   !inputHostSwitchLocalFaultResolvedLocked(ticket,InputHostSwitchLocalFault::OwnedHiddenTargetMissing),
   "failed owned readback is neither a current fault nor positive resolution");}
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(g_inputHostSwitchRecovery.requestId==ticket&&g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecovery.resolved&&
  !g_inputHostSwitchRecovery.spent&&stopCount()==0&&createCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==0&&
  g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,
  "queued UNKNOWN evidence holds the exact unspent offer with no Stop or charge");
 // Ordinary readback observes all owned paths before the next queued dispatch.
 publishLocalReadback();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1,"still-current missing targets requeue only the retained offer");
 publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultResolvedLocked(ticket,InputHostSwitchLocalFault::OwnedHiddenTargetMissing),
   "fresh actual owned-path readback proves ordinary repair before dispatch");}
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(g_inputHostSwitchRecovery.requestId==ticket&&g_inputHostSwitchRecovery.resolved&&!g_inputHostSwitchRecovery.pending&&
  !g_inputHostSwitchRecovery.queued&&!g_inputHostSwitchRecovery.spent&&!g_inputHostSwitchRecoveryPending.load()&&
  g_inputHostSwitchRecovery.localFault==InputHostSwitchLocalFault::OwnedHiddenTargetMissing&&g_inputHostSwitchRecovery.deadline==deadline,
  "ordinary repair cancels queued IO while retaining resolved ticket fault metadata and original deadline");
 // Keep verification locked: the second fault must be genuinely current when
 // Offer refuses renewal, rather than being rejected only by a final request.
 publishLocalReadback();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultCurrentLocked(ticket)==InputHostSwitchLocalFault::OwnedHiddenTargetMissing,
   "same-ticket duplicate test has genuine newly missing owned paths");
  inputHostSwitchRecoveryOfferLocked(ticket,currentPowerGeneration(),ERROR_SUCCESS,false,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);
  inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(g_inputHostSwitchRecovery.requestId==ticket&&g_inputHostSwitchRecovery.resolved&&!g_inputHostSwitchRecovery.pending&&
  queuedRecoveryCount()==0&&stopCount()==0&&createCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==0&&
  g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,
  "resolved original ticket cannot renew a local offer or consume Stop Start or budget");
 finishLocalCase();
 }
 for(const std::string condition:{"same-transaction-target-mutation","stale-request","stale-power"}){
 cases.push_back({{"name",std::string("actual-owned-readback-publication-rejected/")+condition}});
 const auto candidate=startLocalOwned();const auto originalHost=g_inputHostProcess;const auto originalEpoch=g_inputHostEpoch;
 const auto originalRun=g_inputHostRunId;const auto originalPower=currentPowerGeneration();const auto transaction=g_hidHideVisibilityJournal.transactionId;
 const auto stamp=captureLocalReadback();const auto commands=g_inputHostSequence;
 if(condition=="same-transaction-target-mutation"){
  std::lock_guard<InputHostMutex> lock(g_inputHostMx);g_hidHideVisibilityJournal.hiddenByUs.push_back(L"USB\\VID_0B05&PID_1B4C\\CHANGED_OWNED_TARGET");
 }
 if(condition=="stale-request"){
  startProtectionClock+=ymcc::VirtualStartProtection::maxMs;
  check(g_virtualStartProtection.admit("steamdeck",startProtectionClock).accepted,"stale publication test admits a genuinely newer player request");
 }
 if(condition=="stale-power")g_powerGeneration=originalPower+1;
 const bool published=inputHostPublishOwnedVisibilityReadback(stamp,true,{},true,GetTickCount64());
 check(!published,"actual publisher rejects changed target set request or power after the query captured its stamp");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
  check(!g_inputHostOwnedVisibilityReadback.requestId&&inputHostSwitchLocalFaultCurrentLocked(ticket)==InputHostSwitchLocalFault::None,
   "rejected old publication cannot set authoritative current missing-target evidence");
  inputHostSwitchRecoveryOfferLocked(ticket,currentPowerGeneration(),ERROR_SUCCESS,false,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);
  inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostProcess==originalHost&&g_inputHostEpoch==originalEpoch&&g_inputHostRunId==originalRun&&
  g_hidHideVisibilityJournal.transactionId==transaction&&g_inputHostSequence==commands,
  "publication rejection preserves the same actual Host run epoch transaction and command count");
 noRecovery("stale or mutated readback causes zero recovery queue Stop Start reservation or episode attempt");
 g_powerGeneration=originalPower;finishLocalCase();
 }
 {
 cases.push_back({{"name","actual-owned-readback-older-query-and-start-freshness"}});
 const auto candidate=startLocalOwned();const auto older=captureLocalReadback();
 ++fixturePowerClockOffset;const auto newer=captureLocalReadback();auto hidden=newer.targets;
 if(hidden.empty())throw std::runtime_error("owned-readback-newer-query-targets-missing");
 hidden.erase(hidden.begin());
 check(inputHostPublishOwnedVisibilityReadback(newer,true,hidden,true,GetTickCount64()),"newer actual query publishes missing owned targets");
 check(!inputHostPublishOwnedVisibilityReadback(older,true,older.targets,true,GetTickCount64()),"late healthy old query cannot overwrite newer missing-target authority");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultCurrentLocked(candidate.requestId)==InputHostSwitchLocalFault::OwnedHiddenTargetMissing&&
   !inputHostSwitchLocalFaultResolvedLocked(candidate.requestId,InputHostSwitchLocalFault::OwnedHiddenTargetMissing),
   "older healthy query cannot clear a newer current owned fault");}
 ++fixturePowerClockOffset;const auto healthy=captureLocalReadback();
 check(inputHostPublishOwnedVisibilityReadback(healthy,true,healthy.targets,true,GetTickCount64()),"fresh newer query publishes positively healthy owned paths");
 check(!inputHostPublishOwnedVisibilityReadback(newer,true,hidden,true,GetTickCount64()),"late old missing query cannot replace newer healthy authority");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultCurrentLocked(candidate.requestId)==InputHostSwitchLocalFault::None&&
   inputHostSwitchLocalFaultResolvedLocked(candidate.requestId,InputHostSwitchLocalFault::OwnedHiddenTargetMissing),
   "older missing query cannot create a current fault after newer positive readback");}
 const auto slow=captureLocalReadback();fixturePowerClockOffset+=1001;readVerifiedCurrent();
 check(inputHostPublishOwnedVisibilityReadback(slow,true,hidden,true,GetTickCount64()),"same-contract slow query may publish its original start and actual completion");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultCurrentLocked(candidate.requestId)==InputHostSwitchLocalFault::None&&
   !inputHostSwitchLocalFaultResolvedLocked(candidate.requestId,InputHostSwitchLocalFault::OwnedHiddenTargetMissing),
   "fresh completion cannot rescue over1000ms query-start evidence or count as healthy resolution");}
 noRecovery("old-query and stale-start publication tests perform zero recovery work or charge");finishLocalCase();
 }
 {
 cases.push_back({{"name","actual-deferred-hide-current-known-source-and-unknown-guards"}});prepare();fixtureFrameFirstAck=true;
 g_machineIdentity.product="RC73XA";check(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Rog,"deferred scenario uses an actual registered ROG board identity");
 fixtureXinputSourceLive=false;
 {std::scoped_lock lock(g_gamepadReadMx,g_hidHidePhysicalIdentityMx);
  g_hidHidePhysicalIdentity={};g_xinputPhysicalSourceLocked=false;
  g_xinputAdmittedMask=g_xinputConnectedMask=g_xinputPhysicalSourceMask=g_xinputPreTargetSlots=0;
  g_xinputSourceReconfirmMask=0;g_xinputSlotReprobeMask=0;}
 {std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot={};}
 check(inputHostStart()&&g_inputHostHello&&g_inputHostPrepared&&g_inputHostNeutralized&&!g_hidHideVisibilityJournal.active,
  "actual ROG source-pending Start prepares its target without manufacturing an owned hide journal");
 const auto pending=g_virtualStartProtection.view(startProtectionClock);const auto ticket=pending.requestId;
 check(inputHostDeferredPhysicalHideCurrent(g_inputHostEpoch,g_inputHostRunId,currentPowerGeneration(),"steamdeck",startProtectionClock),
  "actual Start arms the current exact run epoch power persona and deadline hide obligation");
 selectLocalPhysicalSource();acceptLocalFrames();const auto candidate=g_virtualStartProtection.view(startProtectionClock);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);const char* missing=nullptr;
  check(!ymcc::inputHost::switchVerificationRuntime::currentOutputProvenLocked(candidate,&missing)&&
   missing&&std::string(missing)=="current-deferred-physical-hide-unfinished",
   "current deferred obligation blocks local success despite an actual fresh accepted current-source report");}
 offerLocal(ticket,InputHostSwitchLocalFault::DeferredPhysicalHide);
 // The controlled read endpoint now reports another incarnation while the
 // actual selected/admitted physical slot remains slot0.
 auto mismatch=readVerifiedCurrent();mismatch.primarySlot=1;
 {std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot=mismatch;}
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultCurrentLocked(ticket)==InputHostSwitchLocalFault::None&&
   !inputHostSwitchLocalFaultResolvedLocked(ticket,InputHostSwitchLocalFault::DeferredPhysicalHide),
   "fresh read from a different selected-slot incarnation cannot diagnose or resolve deferred hide");}
 {std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot={};}
 startProtectionClock+=candidate.verificationRemainingMs;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);const char* missing=nullptr;
  check(inputHostSwitchLocalFaultCurrentLocked(ticket)==InputHostSwitchLocalFault::None&&
   !inputHostSwitchLocalFaultResolvedLocked(ticket,InputHostSwitchLocalFault::DeferredPhysicalHide)&&
   !ymcc::inputHost::switchVerificationRuntime::currentOutputProvenLocked(g_virtualStartProtection.view(startProtectionClock),&missing)&&
   missing&&std::string(missing)=="current-deferred-physical-hide-unfinished",
   "no fresh source leaves pending debt unresolved and success blocked without inferring a fault");
  inputHostSwitchVerificationMaintenanceLocked();inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);
 check(g_virtualStartProtection.view(startProtectionClock).locked&&g_virtualStartProtection.view(startProtectionClock).phase=="verifying"&&
  g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecovery.queued&&!g_inputHostSwitchRecovery.spent&&queuedRecoveryCount()==0&&
  stopCount()==0&&createCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,
  "post-inspection UNKNOWN deferred source holds original verification and offer with no queue Stop Start or budget");
 finishLocalCase();
 }
 {
 cases.push_back({{"name","actual-queued-local-fallback-original-deadline-no-io"}});
 const auto candidate=startLocalOwned();const auto ticket=candidate.requestId;const auto deadline=startProtectionClock+candidate.remainingMs;
 publishLocalReadback();offerLocal(ticket,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);
 startProtectionClock+=candidate.verificationRemainingMs;readVerifiedCurrent();publishLocalReadback();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1,"deadline case has actual queued local fallback before expiry");
 const auto commands=g_inputHostSequence;startProtectionClock=deadline;
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 const auto expired=g_virtualStartProtection.view(startProtectionClock);
 check(expired.requestId==ticket&&expired.remainingMs==0&&expired.phase=="expired"&&expired.fallbackAttempts==0&&
  g_inputHostRecoveryEpisodeAttempts.load()==0&&g_inputHostSequence==commands&&stopCount()==0&&createCount()==1&&
  !g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecoveryPending.load()&&queuedRecoveryCount()==0&&g_inputHostSwitchRecovery.deadline==deadline,
  "queued local fallback at original thirty-second deadline cannot Stop send commands charge or renew request time");
 finishLocalCase();
 }
 {
 cases.push_back({{"name","actual-owned-local-fault-before-first-frame-starts-original-inspection"}});
 const auto initial=startLocalOwned("steamdeck",false);const auto deadline=startProtectionClock+initial.remainingMs;
 check(initial.phase=="pending"&&!initial.verificationPending&&g_inputHostPrepared&&g_inputHostNeutralized&&!g_inputHostActive,
  "actual completed target without first frame has not invented an execution success receipt");
 publishLocalReadback();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 const auto candidate=g_virtualStartProtection.view(startProtectionClock);
 check(candidate.requestId==initial.requestId&&candidate.phase=="verifying"&&candidate.candidateOutcome=="failure"&&
  candidate.verificationRemainingMs==10000&&g_inputHostSwitchRecovery.pending&&
  g_inputHostSwitchRecovery.localFault==InputHostSwitchLocalFault::OwnedHiddenTargetMissing&&g_inputHostSwitchRecovery.deadline==deadline,
  "actual known visibility failure supplies Tc before any accepted frame while retaining the original deadline");
 check(stopCount()==0&&createCount()==1&&g_inputHostRecoveryEpisodeAttempts.load()==0&&candidate.fallbackAttempts==0,
  "fault observation before the first frame performs no Stop Start or charge inside execution inspection");
 finishLocalCase();
 }
 {
 cases.push_back({{"name","actual-owned-root-refresh-original-clock-single-fallback"}});
 const auto candidate=startRootRefreshStage();const auto ticket=candidate.requestId;const auto power=currentPowerGeneration();
 const auto deadline=startProtectionClock+candidate.remainingMs;const auto settings=ymSettingsReadRaw(YM_SETTINGS_FILE);
 const auto stage=captureRootStage();offerLocal(ticket,InputHostSwitchLocalFault::OwnedRootRefreshIncomplete);
 check(g_inputHostSwitchRecovery.deadline==deadline&&g_inputHostSwitchRecovery.persona=="steamdeck"&&
  g_inputHostSwitchRecovery.localRoot==stage.configurationRoot&&g_inputHostSwitchRecovery.localTargets==stage.configurationTargets&&
  g_inputHostSwitchRecovery.localConfigurationRevision==g_inputHostConfigRevision&&
  g_inputHostSwitchRecovery.localIntentRevision==g_inputHostVirtualIntentRevision.load(),
  "root offer pins original deadline persona exact normal root targets and configuration intent revisions");
 startProtectionClock+=9999;readVerifiedCurrent();publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);
 check(stopCount()==0&&createCount()==1&&queuedRecoveryCount()==0&&!g_inputHostSwitchRecovery.spent&&
  g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&
  !captureRootStage().continuationRetired,"root refresh at9999 neither retires normal continuation nor queues Stops or spends fallback");
 ++startProtectionClock;readVerifiedCurrent();publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1&&!g_inputHostSwitchRecovery.spent,
  "root refresh queues the original unspent offer at exactly10000");
 fullPipeDrainQueuedLifecycle();if(g_inputHostSwitchRecovery.awaitingCleanup)fullPipeQuarantineProbe();fullPipeDrainQueuedLifecycle();
 const auto stopped=std::find(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"stop:switch-recovery");
 check(stopCount()==1&&createCount()==2&&stopped!=g_startEntrySeam.trace.end()&&
  std::find(stopped,g_startEntrySeam.trace.end(),"create-process")!=g_startEntrySeam.trace.end()&&
  g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&g_inputHostBoundPersona=="steamdeck"&&
  g_inputHostSwitchRecovery.spent&&g_inputHostRecoveryEpisodeAttempts.load()==1&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1,
  "actual lifecycle dispatcher takes over then Stops cleans up and Starts the same SD persona with one shared fallback");
 check(currentPowerGeneration()==power&&ymSettingsReadRaw(YM_SETTINGS_FILE)==settings&&g_virtualStartProtection.view(startProtectionClock).requestId==ticket&&
  g_inputHostSwitchRecovery.deadline==deadline&&g_virtualStartProtection.view(startProtectionClock).remainingMs==20000,
  "root takeover preserves the original player ticket power settings and thirty-second absolute deadline");
 inputHostDeferredPhysicalHideArm(true,g_inputHostRunId,g_inputHostEpoch,power,"steamdeck",deadline,ticket);
 const auto replacement=captureRootStage();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);LocalDeferredWriteScope write(replacement);
  check(inputHostDeferredPhysicalHideConfigurationLocked(true),"replacement normal stage is established by its actual configuration helper");}
 readVerifiedCurrent();publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);InputHostDeferredPhysicalHide proof;
  check(inputHostSwitchRootRefreshSnapshotLocked(ticket,proof),"spent replay probe has a genuine fresh replacement root stage");
  inputHostSwitchRecoveryOfferLocked(ticket,power,ERROR_SUCCESS,false,InputHostSwitchLocalFault::OwnedRootRefreshIncomplete);inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(stopCount()==1&&createCount()==2&&g_inputHostSwitchRecovery.spent&&g_inputHostRecoveryEpisodeAttempts.load()==1&&
  g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&!captureRootStage().continuationRetired,
  "spent original ticket cannot replay takeover or retire a fresh replacement normal stage");
 timeline("owned-root-single-fallback-final");finishLocalCase();
 }
 }
 {
 cases.push_back({{"name","actual-owned-root-refresh-ordinary-repair-revokes-unspent-offer"}});
 const auto candidate=startRootRefreshStage();const auto ticket=candidate.requestId;const auto deadline=startProtectionClock+candidate.remainingMs;
 offerLocal(ticket,InputHostSwitchLocalFault::OwnedRootRefreshIncomplete);const auto stage=captureRootStage();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);LocalDeferredWriteScope write(stage);const char* outcome=nullptr;
  check(inputHostDeferredPhysicalHideRefreshLocked(&outcome)&&outcome&&std::string(outcome)=="owned-root-refreshed",
   "actual normal refresh helper completes through the controlled cycle I/O leaf before fallback");}
 check(captureRootStage().refreshAttempted&&fixtureRogDeferredCycles.size()==1,
  "ordinary completion records a possibly issued refresh before clearing its original debt");
 inputHostDeferredPhysicalHideClear(); // Actual normal completion helper, not physical-return evidence.
 acceptLocalFrames();publishLocalReadback(false);startProtectionClock+=candidate.verificationRemainingMs;readVerifiedCurrent();publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(inputHostSwitchLocalFaultResolvedLocked(ticket,InputHostSwitchLocalFault::OwnedRootRefreshIncomplete),
   "actual accepted output positive readback and completed normal debt prove repair before fallback");
  inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(g_inputHostSwitchRecovery.resolved&&!g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecovery.spent&&
  g_inputHostSwitchRecovery.requestId==ticket&&g_inputHostSwitchRecovery.deadline==deadline,
  "ordinary repair revokes the unspent offer while retaining original ticket deadline and resolved state");
 inputHostDeferredPhysicalHideArm(true,g_inputHostRunId,g_inputHostEpoch,currentPowerGeneration(),"steamdeck",deadline,ticket);
 const auto again=captureRootStage();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);LocalDeferredWriteScope write(again);
  check(inputHostDeferredPhysicalHideConfigurationLocked(true),"resolved replay probe has a genuine current normal configuration stage");}
 publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);InputHostDeferredPhysicalHide proof;
  check(inputHostSwitchRootRefreshSnapshotLocked(ticket,proof),"resolved replay is rejected with positive current root evidence");
  inputHostSwitchRecoveryOfferLocked(ticket,currentPowerGeneration(),ERROR_SUCCESS,false,InputHostSwitchLocalFault::OwnedRootRefreshIncomplete);inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(g_inputHostSwitchRecovery.resolved&&!g_inputHostSwitchRecovery.spent,"resolved root offer cannot be renewed by later same-ticket configuration progress");
 noRecovery("ordinary repair and resolved replay use zero fallback queues Stops Starts and recovery budget");finishLocalCase();
 }
 if(cliBoundaryOnly!=4){
 for(const std::string condition:{"refresh-issued","refresh-result-unknown","configuration-unapplied","configuration-not-ready","continuation-retired",
  "refresh-not-required","unknown-readback","cloak-off","stale-readback","foreign-readback","stage-targets-changed","stage-root-changed","stage-identity-changed",
  "journal-targets-changed","journal-root-changed","journal-owner-changed","journal-source-count","journal-source-mismatch",
  "old-host","old-run","old-request","old-power","old-persona","expired-stage","source-mask-absent","source-mask-extra","source-not-admitted","pretarget-absent"}){
 cases.push_back({{"name",std::string("actual-owned-root-refresh-guard/")+condition}});
 const auto candidate=startRootRefreshStage();const auto ticket=candidate.requestId;
 {std::lock_guard<std::mutex> lock(g_inputHostDeferredPhysicalHideMx);auto& stage=g_inputHostDeferredPhysicalHide;
  if(condition=="refresh-issued"||condition=="refresh-result-unknown")stage.refreshAttempted=true;
  if(condition=="configuration-unapplied")stage.configurationApplied=false;
  if(condition=="configuration-not-ready")stage.configurationReady=false;
  if(condition=="continuation-retired")stage.continuationRetired=true;
  if(condition=="refresh-not-required")stage.refreshRequired=false;
  if(condition=="stage-targets-changed")stage.configurationTargets.back()+="-OTHER";
  if(condition=="stage-root-changed")stage.configurationRoot+="-OTHER";
  if(condition=="stage-identity-changed")stage.configurationPhysicalIdentity+="-OTHER";
  if(condition=="old-host")++stage.hostGen;
  if(condition=="old-run")stage.runId+="-OTHER";
  if(condition=="old-request")++stage.requestId;
  if(condition=="old-power")++stage.powerGen;
  if(condition=="old-persona")stage.persona="elite";
  if(condition=="expired-stage")stage.deadline=startProtectionClock;
 }
 if(condition=="unknown-readback")publishLocalReadback(false,false,true);
 if(condition=="cloak-off")publishLocalReadback(false,true,false);
 if(condition=="stale-readback")fixturePowerClockOffset+=1001;
 if(condition=="foreign-readback"){std::lock_guard<InputHostMutex> lock(g_inputHostMx);++g_inputHostOwnedVisibilityReadback.hostEpoch;}
 if(condition=="journal-targets-changed")g_hidHideVisibilityJournal.hiddenByUs.back()+=L"-OTHER";
 if(condition=="journal-root-changed")g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId+="-OTHER";
 if(condition=="journal-owner-changed")g_hidHideVisibilityJournal.transactionId+="-OTHER";
 if(condition=="journal-source-count")g_hidHideVisibilityJournal.physicalSources.push_back(g_hidHideVisibilityJournal.physical);
 if(condition=="journal-source-mismatch")g_hidHideVisibilityJournal.physicalSources.front().physicalIdentity+="-OTHER";
 {std::lock_guard<std::mutex> lock(g_gamepadReadMx);
  if(condition=="source-mask-absent")g_xinputPhysicalSourceMask=0;
  if(condition=="source-mask-extra")g_xinputPhysicalSourceMask=3;
  if(condition=="source-not-admitted")g_xinputAdmittedMask=0;
  if(condition=="pretarget-absent")g_xinputPreTargetSlots=0;
 }
 const auto commands=g_inputHostSequence;const auto io=forbiddenIo;const auto reads=inertXinputReads;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);InputHostDeferredPhysicalHide stage;
  check(!inputHostSwitchRootRefreshSnapshotLocked(ticket,stage)&&inputHostSwitchLocalFaultCurrentLocked(ticket)==InputHostSwitchLocalFault::None,
   "incomplete issued retired unknown stale mismatched owner journal or source cannot diagnose root refresh");
  inputHostSwitchRecoveryOfferLocked(ticket,currentPowerGeneration(),ERROR_SUCCESS,false,InputHostSwitchLocalFault::OwnedRootRefreshIncomplete);}
 startProtectionClock+=candidate.verificationRemainingMs;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(g_inputHostSequence==commands&&forbiddenIo==io&&inertXinputReads==reads&&fixtureRogDeferredCycles.empty(),
  "rejected root stage performs no command source read or physical refresh");
 noRecovery("rejected root stage cannot offer queue Stop Start retire continuation or consume fallback");finishLocalCase();
 }
 for(const std::string condition:{"refresh-issued","configuration-not-ready","continuation-retired","stage-targets-changed","stage-root-changed",
  "stage-owner-changed","configuration-revision","intent-revision","unknown-readback","replacement-root-and-targets"}){
 cases.push_back({{"name",std::string("actual-owned-root-refresh-offered-stage-change/")+condition}});
 const auto candidate=startRootRefreshStage();const auto ticket=candidate.requestId;const auto deadline=startProtectionClock+candidate.remainingMs;
 offerLocal(ticket,InputHostSwitchLocalFault::OwnedRootRefreshIncomplete);
 startProtectionClock+=candidate.verificationRemainingMs;readVerifiedCurrent();publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1,"stage-change probe begins with one real queued unspent root fallback");
 {std::lock_guard<std::mutex> lock(g_inputHostDeferredPhysicalHideMx);auto& stage=g_inputHostDeferredPhysicalHide;
  if(condition=="refresh-issued")stage.refreshAttempted=true;
  if(condition=="configuration-not-ready")stage.configurationReady=false;
  if(condition=="continuation-retired")stage.continuationRetired=true;
  if(condition=="stage-targets-changed")stage.configurationTargets.back()+="-OTHER";
  if(condition=="stage-root-changed")stage.configurationRoot+="-OTHER";
  if(condition=="stage-owner-changed")stage.runId+="-OTHER";
 }
 if(condition=="configuration-revision")++g_inputHostConfigRevision;
 if(condition=="intent-revision")g_inputHostVirtualIntentRevision.fetch_add(1);
 if(condition=="configuration-revision"||condition=="intent-revision")publishLocalReadback(false);
 if(condition=="unknown-readback")publishLocalReadback(false,false,true);
 if(condition=="replacement-root-and-targets"){
  const auto oldRoot=g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId;
  const auto oldChild=g_hidHideVisibilityJournal.physical.deviceInstanceId;
  const auto root=oldRoot+"-REPLACED",child=oldChild+"-REPLACED";
  auto& journal=g_hidHideVisibilityJournal;journal.physical.baseContainerDeviceInstanceId=root;
  journal.physical.deviceInstanceId=journal.physical.physicalIdentity=child;journal.physicalSources={journal.physical};
  journal.hiddenByUs={U2W(root),U2W(child)};
  {std::lock_guard<std::mutex> lock(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity=journal.physical;}
  // A replacement controlled normal stage must still pass its REAL helper.
  {std::lock_guard<std::mutex> lock(g_inputHostDeferredPhysicalHideMx);auto& stage=g_inputHostDeferredPhysicalHide;
   stage.configurationApplied=stage.configurationReady=false;stage.configurationPhysicalIdentity.clear();stage.configurationRoot.clear();stage.configurationTargets.clear();}
  const auto replacement=captureRootStage();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);LocalDeferredWriteScope write(replacement);
   check(inputHostDeferredPhysicalHideConfigurationLocked(true),"replacement exact root and target set obtains actual normal configuration proof");}
  publishLocalReadback(false);
 }
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);InputHostDeferredPhysicalHide stage;
  if(condition=="replacement-root-and-targets"||condition=="configuration-revision"||condition=="intent-revision")
   check(inputHostSwitchRootRefreshSnapshotLocked(ticket,stage),"changed offer binding is tested with a genuinely current positive root snapshot");
  check(!inputHostSwitchRootRefreshOfferCurrentLocked(g_inputHostSwitchRecovery),"offered exact stage or revision cannot borrow subsequent normal progress");}
 const auto commands=g_inputHostSequence;fullPipeDrainQueuedLifecycle();inputHostPerformSwitchRecovery(ticket);
 check(stopCount()==0&&createCount()==1&&g_inputHostSequence==commands&&!g_inputHostSwitchRecovery.spent&&
  g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&
  g_inputHostSwitchRecovery.requestId==ticket&&g_inputHostSwitchRecovery.deadline==deadline,
  "stage mutation after queue dispatch refuses Stop and budget while preserving the original offer ticket and deadline");
 finishLocalCase();
 }
 {
 cases.push_back({{"name","actual-owned-hidden-offer-advances-root-after-source-wait"}});
 const auto candidate=startLocalOwned("steamdeck",true,true);const auto ticket=candidate.requestId;const auto deadline=startProtectionClock+candidate.remainingMs;
 const auto inspectionDeadline=startProtectionClock+candidate.verificationRemainingMs;
 publishLocalReadback();offerLocal(ticket,InputHostSwitchLocalFault::OwnedHiddenTargetMissing);
 inputHostDeferredPhysicalHideArm(true,g_inputHostRunId,g_inputHostEpoch,currentPowerGeneration(),"steamdeck",deadline,ticket);
 startProtectionClock+=2500;fixtureRogDeferredJournalSeed=true;const auto original=captureRootStage();
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);LocalDeferredWriteScope write(original);
  check(hidHideApplyPhysicalTransaction(g_inputHostRunId,false)&&inputHostDeferredPhysicalHideConfigurationLocked(true),
   "same normal owner completes configuration through controlled hide and actual configuration helper");}
 readVerifiedCurrent();publishLocalReadback(false);
 {std::lock_guard<std::mutex> lock(g_gamepadReadMx);g_xinputPhysicalSourceMask=0;}
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(!ymcc::inputHost::switchRecoveryRuntime::localFaultReadyLocked(g_inputHostSwitchRecovery)&&
   !inputHostSwitchLocalFaultResolvedLocked(ticket,InputHostSwitchLocalFault::OwnedHiddenTargetMissing)&&
   g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecovery.resolved&&!g_inputHostSwitchRecovery.spent&&
   g_inputHostSwitchRecovery.localFault==InputHostSwitchLocalFault::OwnedHiddenTargetMissing,
   "positive hidden readback with outstanding root debt and unknown source cannot resolve or strand the old hidden offer");}
 check(stopCount()==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,
  "source-paused promotion retains the original pending offer without consuming fallback");
 selectLocalPhysicalSource();readVerifiedCurrent();publishLocalReadback(false);
 startProtectionClock=inspectionDeadline-1;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(!g_inputHostSwitchRecovery.queued&&stopCount()==0,"normal configuration progress does not shorten the original ten-second inspection");
 ++startProtectionClock;readVerifiedCurrent();publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();
  check(g_inputHostSwitchRecovery.localFault==InputHostSwitchLocalFault::OwnedRootRefreshIncomplete&&
   g_inputHostSwitchRecovery.requestId==ticket&&g_inputHostSwitchRecovery.deadline==deadline&&
   g_inputHostSwitchRecovery.queued&&!g_inputHostSwitchRecovery.spent&&
   g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs==0&&
   inputHostSwitchRootRefreshOfferCurrentLocked(g_inputHostSwitchRecovery),
   "source recovery advances the SAME unspent hidden offer to exact root proof at its original inspection deadline");}
 fullPipeDrainQueuedLifecycle();if(g_inputHostSwitchRecovery.awaitingCleanup)fullPipeQuarantineProbe();fullPipeDrainQueuedLifecycle();
 check(stopCount()==1&&createCount()==2&&g_inputHostSwitchRecovery.spent&&g_inputHostRecoveryEpisodeAttempts.load()==1&&
  g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&g_virtualStartProtection.view(startProtectionClock).requestId==ticket&&
  g_inputHostSwitchRecovery.deadline==deadline&&g_inputHostBoundPersona=="steamdeck",
  "same-offer root promotion performs exactly one fallback without renewing request persona deadline or inspection");
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  check(!ymcc::inputHost::switchRecoveryRuntime::pinRootStageLocked(g_inputHostSwitchRecovery),
   "spent continuation cannot use normal stage promotion to rearm the offer");}
 finishLocalCase();
 }
 for(const std::string condition:{"queued-original-deadline","stop-reaches-original-deadline"}){
 cases.push_back({{"name",std::string("actual-owned-root-refresh-deadline/")+condition}});
 const auto candidate=startRootRefreshStage();const auto ticket=candidate.requestId;const auto deadline=startProtectionClock+candidate.remainingMs;
 offerLocal(ticket,InputHostSwitchLocalFault::OwnedRootRefreshIncomplete);
 startProtectionClock+=candidate.verificationRemainingMs;readVerifiedCurrent();publishLocalReadback(false);
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchRecoveryMaintenanceLocked();}
 check(g_inputHostSwitchRecovery.queued&&queuedRecoveryCount()==1,"deadline probe begins with the exact root offer queued before the cap");
 bool advancedDuringStop=false;std::uint64_t stopExecutionDeadline=0;
 const auto previousObserver=std::move(fixtureMonotonicInterleave);
 if(condition=="queued-original-deadline")startProtectionClock=deadline;
 else fixtureMonotonicInterleave=[&](){if(!advancedDuringStop&&stopCount()==1){
  stopExecutionDeadline=fixtureActiveExecutionDeadline();startProtectionClock=deadline;advancedDuringStop=true;}};
 fullPipeDrainQueuedLifecycle();fixtureMonotonicInterleave=previousObserver;
 const auto expired=g_virtualStartProtection.view(startProtectionClock);
 if(condition=="queued-original-deadline")
  check(stopCount()==0&&expired.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,
   "old queued root offer at the original thirty-second cap cannot Stop or spend fallback");
 else check(advancedDuringStop&&stopExecutionDeadline==deadline&&stopCount()==1&&g_inputHostSwitchRecovery.spent&&
  expired.fallbackAttempts==1&&g_inputHostRecoveryEpisodeAttempts.load()==1,
  "existing monotonic I/O hook advances actual Stop to the same absolute deadline without refunding its reservation");
 check(expired.phase=="expired"&&expired.remainingMs==0&&createCount()==1&&g_inputHostSwitchRecovery.deadline==deadline&&
  fixtureActiveExecutionDeadline()==0,"expiry before dispatch or inside Stop never creates a replacement Host and restores the borrowed scope");
 const auto next=g_virtualStartProtection.admit("steamdeck",startProtectionClock);
 check(next.accepted&&next.requestId!=ticket,"deadline replay probe has an independently admitted newer request");
 inputHostPerformSwitchRecovery(ticket);fullPipeDrainQueuedLifecycle();
 check(createCount()==1&&g_virtualStartProtection.view(startProtectionClock).requestId==next.requestId&&
  g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"old queued recovery cannot borrow a newer ticket after its original period");
 finishLocalCase();
 }
 }
 cases.push_back({{"name","actual-unique-OFF-verification-exit"}});prepare();
 startProtectionClock+=30000;g_virtualStartProtection.admit("disabled",startProtectionClock);g_realStickTest.virtualTargetEnabled=false;g_realStickTest.virtualTargetPersona="disabled";
 inputHostStop("virtual-target-disabled");const auto closing=g_virtualStartProtection.view(startProtectionClock);
 check(closing.phase=="verifying"&&closing.candidateOutcome=="success"&&closing.locked,"actual cleanup only creates a candidate and cannot release player buttons");
 startProtectionClock+=9999;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).locked,"actual common exit waits full ten-second verification window");
 ++startProtectionClock;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 const auto final=g_virtualStartProtection.view(startProtectionClock);check(!final.locked&&final.phase=="success","actual common verification is the unique normal release exit");
 inputHostStartProtectionFinish(final.requestId,"failure","offline-late-contradictory-receipt");{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
 check(g_virtualStartProtection.view(startProtectionClock).phase=="success","late opposite callback cannot create both success and failure");
 unsigned finals=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-switch-verification"&&event["detail"].value("stage","")=="final")++finals;
 check(finals==1,"same request has exactly one final result observation");cases.back()["events"]=fixtureEvents;
 fixtureXinputSourceLive=false;g_inputHostSwitchRecovery={};g_inputHostSwitchRecoveryPending=false;
}

// Explicit parameters of the existing queued-exception consumer. All Start,
// exception compensation, queue dispatch and Stop bodies remain production.

// Parameters of the existing native preflight consumer, all I/O leaves inert.
// A controlled native lease is not proof of a field scheduler interleaving.

// Waiting-state parameters of the original queued exception consumer.
// All Start/exception/Stop bodies are production; alternate successor ordering
// is controlled and is not a claim that this ordering occurred in field logs.



static void fixtureSerialAfterClosingRead(){if(fixtureSerialClaimRaceArmed.exchange(false)){g_exitRequested.store(true,std::memory_order_release);}}

static constexpr const char* serialPowerActionVariants[]={"legacy-kill","ymcc-factory","scope-unwind","nested-retired"};
static unsigned long long fixtureCurrentActionPower(){
#ifdef YMCC_FIXTURE_CURRENT_POWER_ACTION
 return g_gamepadSerialActionPower;
#else
 return 0;
#endif
}
static bool fixtureSubmitCurrentPower(std::function<void()> job){
#ifdef YMCC_FIXTURE_CURRENT_POWER_ACTION
 return gamepadSerialSubmitCurrentPower(std::move(job));
#else
 return gamepadSerialSubmit(std::move(job)); // exact old generic action route, not a substitute guarded helper
#endif
}
static void fixtureQueueLegacyKill(){/*ACTUAL_LEGACY_KILL_QUEUE*/}
static void serialPowerActionCases(){
 for(const char* name:serialPowerActionVariants){
  seed();const auto before=failures.size();const std::string variant=name;cases.push_back({{"name",std::string("serial-power-action/")+name}});
  check(!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted,"previous actual worker returned before isolated action scenario");
  g_exitRequested=false;g_gamepadSerialStopping=false;g_powerGeneration=7000;g_shortcutRecordingActive=false;fixtureSerialKillCommands=0;
  std::atomic<unsigned> oldWrites{0},freshWrites{0},returnWrites{0},scopeMismatches{0};
  std::promise<DWORD> complete;auto ready=complete.get_future();
  HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),go=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(entered&&go,"private action latch handles created");
  check(gamepadSerialSubmit([entered,go]{SetEvent(entered);WaitForSingleObject(go,5000);}),"real serial worker accepts controlled predecessor");
  check(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0,"real predecessor entered private latch");
  if(variant=="legacy-kill")fixtureQueueLegacyKill();
  else if(variant=="ymcc-factory")check(nativeYmccShortcutSubmit([&]{++oldWrites;}),"actual native shortcut factory accepts pre-bound user action");
  else if(variant=="scope-unwind")check(fixtureSubmitCurrentPower([&]{if(fixtureCurrentActionPower()!=7000)++scopeMismatches;throw std::runtime_error("controlled-action-exception");}),"guarded action accepts exception-unwind scenario");
  else check(fixtureSubmitCurrentPower([&]{
   if(fixtureCurrentActionPower()!=7000)++scopeMismatches;
   g_powerGeneration=7001;
   fixtureSubmitCurrentPower([&]{++oldWrites;});
   gamepadSerialSubmit([&]{complete.set_value(GetCurrentThreadId());});
  }),"guarded action accepts controlled in-flight nested scenario");
  if(variant=="legacy-kill"||variant=="ymcc-factory")g_powerGeneration=7001;
  check(gamepadSerialSubmit([&]{++returnWrites;if(fixtureCurrentActionPower()!=0)++scopeMismatches;}),"original generic return/observation job stays admitted");
  if(variant!="nested-retired")check(fixtureSubmitCurrentPower([&]{++freshWrites;if(fixtureCurrentActionPower()!=currentPowerGeneration())++scopeMismatches;}),"new current-power action accepted");
  if(variant!="nested-retired")check(gamepadSerialSubmit([&]{complete.set_value(GetCurrentThreadId());}),"original serial completion barrier accepted");
  SetEvent(go);const bool ran=ready.wait_for(std::chrono::seconds(2))==std::future_status::ready;check(ran,"actual serial worker reaches original completion barrier");
  DWORD worker=ran?ready.get():0;const bool joined=gamepadSerialStop(2000);
  check(joined&&!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted&&worker&&worker!=GetCurrentThreadId(),"original Stop joins the actual separate worker");
  check(CloseHandle(go)&&CloseHandle(entered),"private action latch handles close after actual worker return");
  if(variant=="legacy-kill")check(fixtureSerialKillCommands==0,"actual legacy queued kill cannot run after a newer power generation");
  if(variant=="ymcc-factory"||variant=="nested-retired")check(oldWrites==0,"retired user action cannot borrow a newer power generation");
  check(returnWrites==1,"physical return and observation work is not cancelled by user-action fencing");
  if(variant!="nested-retired")check(freshWrites==1,"fresh action after the synthetic resume executes exactly once");
#ifdef YMCC_FIXTURE_CURRENT_POWER_ACTION
  check(scopeMismatches==0,"actual action scope stamps and restores original power even across throw/nested queue");
#endif
  cases.back()["actualWorkerThreadId"]=worker;cases.back()["fakeShellCalls"]=fixtureSerialKillCommands.load();cases.back()["returnJobs"]=returnWrites.load();
  cases.back()["syntheticPowerOnly"]=true;cases.back()["actualWindowsSleep"]=false;cases.back()["passed"]=failures.size()==before;
 }
}


static constexpr const char* serialConfigActionVariants[]={"legacy-kill","default-rule","write-pending","parse-gap","nested-retired","failed-write","source-epoch"};
#ifdef YMCC_FIXTURE_ACTION_CONFIGURATION
using FixtureActionConfigurationWrite=GamepadSerialEvent::ConfigurationWrite;
static void fixtureConfigurationReset(){GamepadSerialEvent::ConfigurationRevision=1;GamepadSerialEvent::ConfigurationWriters=0;GamepadSerialEvent::RulesRequired=0;GamepadSerialEvent::RulesApplied=0;GamepadSerialEvent::BoundRevision=0;}
static void fixtureConfigurationPublished(){GamepadSerialEvent::rulesPublished();}
static void fixtureConfigurationParsed(){GamepadSerialEvent::RulesApplied.store(GamepadSerialEvent::RulesRequired.load());}
static bool fixtureConfigurationBound(){return GamepadSerialEvent::BoundRevision!=0;}
static unsigned fixtureConfigurationWriters(){return GamepadSerialEvent::ConfigurationWriters.load();}
#else
// Before source has no configuration fence. These external facts do not add a
// replacement guard to the original helper/worker, so actual stale writes remain.
struct FixtureActionConfigurationWrite{explicit FixtureActionConfigurationWrite(bool){}};
static void fixtureConfigurationReset(){}
static void fixtureConfigurationPublished(){}
static void fixtureConfigurationParsed(){}
static bool fixtureConfigurationBound(){return false;}
static unsigned fixtureConfigurationWriters(){return 0;}
#endif
static void serialConfigActionCases(){
 for(const char* name:serialConfigActionVariants){
  seed();const auto failedBefore=failures.size();const std::string variant=name;cases.push_back({{"name",std::string("serial-config-action/")+name}});
  check(!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted,"previous actual worker returned before configuration case");
  fixtureConfigurationReset();g_exitRequested=false;g_gamepadSerialStopping=false;g_powerGeneration=9000;g_oemShortcutEdgeEpoch=1;g_shortcutRecordingActive=false;fixtureSerialKillCommands=0;
  std::atomic<unsigned> oldWrites{0},freshWrites{0},returnWrites{0},boundReturns{0};
  HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  check(entered&&release,"owned configuration latch handles created");
  check(gamepadSerialSubmit([&]{SetEvent(entered);WaitForSingleObject(release,4000);}),"real predecessor admitted");
  check(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0,"real predecessor holds queue before cancellation");
  std::promise<bool> finished;auto ready=finished.get_future();
  if(variant=="legacy-kill")fixtureQueueLegacyKill();
  else if(variant=="nested-retired"){
   check(gamepadSerialSubmitCurrentPower([&]{
    {FixtureActionConfigurationWrite changed(true);}
    (void)gamepadSerialSubmitCurrentPower([&]{++oldWrites;});
    // Parent queues its own barrier AFTER its possible child. Stop cannot erase
    // an unobserved stale child and turn this assertion green.
    gamepadSerialSubmit([&]{finished.set_value(true);});
   }),"actual helper accepts parent before configuration cancellation");
  }else check(nativeYmccShortcutSubmit([&]{++oldWrites;}),"actual shortcut factory accepts original action");
  std::optional<FixtureActionConfigurationWrite> pending;
  if(variant=="write-pending"){
   pending.emplace(true);
   check(!gamepadSerialSubmitCurrentPower([&]{++oldWrites;}),"configuration write cannot stamp a new admitted action");
  }else if(variant=="parse-gap"){
   fixtureConfigurationPublished();
   check(!gamepadSerialSubmitCurrentPower([&]{++oldWrites;}),"committed rules without parsed ownership reject newly stamped actions");
  }else if(variant=="failed-write"){
   try{FixtureActionConfigurationWrite changed(true);throw std::runtime_error("controlled-failed-setting-write");}catch(...){}
  }else if(variant=="source-epoch")++g_oemShortcutEdgeEpoch;
  else if(variant!="nested-retired"){
   {FixtureActionConfigurationWrite off(true);}
   {FixtureActionConfigurationWrite on(true);} // Same final enabled value, distinct withdrawal history.
  }
  check(gamepadSerialSubmit([&]{++returnWrites;if(fixtureConfigurationBound())++boundReturns;}),"generic return remains admitted across configuration mutation");
  if(variant!="nested-retired")check(gamepadSerialSubmit([&]{finished.set_value(true);}),"observation barrier queued after old action");
  SetEvent(release);const bool observed=ready.wait_for(std::chrono::seconds(3))==std::future_status::ready;
  check(observed,"actual worker reaches post-action observation before Stop");
  check(oldWrites==0&&fixtureSerialKillCommands==0,"same-power withdrawn action produces no old side effect");
  check(returnWrites==1&&boundReturns==0,"generic return executes once outside action TLS");
  pending.reset();if(variant=="parse-gap")fixtureConfigurationParsed();
  std::promise<bool> freshFinished;auto freshReady=freshFinished.get_future();
  check(nativeYmccShortcutSubmit([&]{++freshWrites;}),"new current action admitted after actual ownership is ready");
  check(gamepadSerialSubmit([&]{freshFinished.set_value(true);}),"fresh action barrier admitted");
  check(freshReady.wait_for(std::chrono::seconds(2))==std::future_status::ready&&freshWrites==1,"new action actually executes after withdrawal");
  check(gamepadSerialStop(2000)&&!g_gamepadSerialThread.joinable(),"original Stop joins actual serial worker");
  check(!fixtureConfigurationWriters(),"configuration mutation leaves no writer responsibility");
  check(g_powerGeneration.load()==9000,"case never changes actual power generation or performs Windows sleep");
  if(entered)CloseHandle(entered);if(release)CloseHandle(release);
  cases.back()["passed"]=failures.size()==failedBefore;cases.back()["actualSerialWorker"]=true;
  cases.back()["realSettingsIo"]=false;cases.back()["syntheticConfigurationFacts"]=true;
  cases.back()["returnJobs"]=returnWrites.load();cases.back()["oldWrites"]=oldWrites.load()+fixtureSerialKillCommands.load();
 }
}

static constexpr const char* serialExitOwnerVariants[]={"actual-worker-return"};
#ifndef YMCC_FIXTURE_HAS_SERIAL_EXIT_OWNER
static bool gamepadSerialExitOwnershipReleased(){return false;} // old source has no proof; never substitutes a candidate body
#endif
template<class F>static bool fixtureSerialStopResult(F&& call){if constexpr(std::is_same_v<std::invoke_result_t<F>,bool>)return call();else{call();return false;}}
static void serialExitOwnerCases(){
 seed();const auto before=failures.size();cases.push_back({{"name","serial-exit-owner/actual-worker-return"}});
 const auto begin=GetTickCount64();unsigned actualWorkers=0;
 auto reset=[](){check(!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted,"prior actual worker returned before isolated serial reset");g_exitRequested=false;g_gamepadSerialStopping=false;};
 auto duplicate=[](){HANDLE h=nullptr;check(DuplicateHandle(GetCurrentProcess(),reinterpret_cast<HANDLE>(g_gamepadSerialThread.native_handle()),GetCurrentProcess(),&h,0,FALSE,DUPLICATE_SAME_ACCESS)!=0,"observer duplicates the actual owned worker handle");return h;};
 auto close=[](HANDLE h){check(h&&CloseHandle(h),"private observer handle closes after actual thread observation");};
 reset();HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::promise<void> entered;auto ready=entered.get_future();std::atomic<unsigned> late{0};
 check(gamepadSerialSubmit([&]{entered.set_value();WaitForSingleObject(release,3000);}),"blocked actual job accepted");++actualWorkers;
 check(ready.wait_for(std::chrono::seconds(2))==std::future_status::ready,"actual worker entered private latch");HANDLE observer=duplicate();
 check(gamepadSerialSubmit([&]{++late;}),"second job queued before terminal stop");
 const bool stopped=fixtureSerialStopResult([](){return gamepadSerialStop(15);});
 check(!stopped&&WaitForSingleObject(observer,0)==WAIT_TIMEOUT,"finite Stop cannot report completion while actual worker is blocked");
 check(g_gamepadSerialThread.joinable()&&g_gamepadSerialStarted,"finite timeout retains the exact actual std thread owner");
 const auto proofBegin=GetTickCount64();check(!gamepadSerialExitOwnershipReleased(),"retained worker vetoes UI completion");
 check(GetTickCount64()-proofBegin<100,"UI completion predicate returns without waiting for the blocked job");
 check(!gamepadSerialSubmit([]{}),"terminal Stop seals later player jobs");
 SetEvent(release);check(WaitForSingleObject(observer,2000)==WAIT_OBJECT_0,"observer sees actual kernel thread return after original job release");
 gamepadSerialStop(2000);check(!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted,"original product thread joins after delayed return");
 check(gamepadSerialExitOwnershipReleased()&&late==0,"only actual joined owner permits completion and canceled queued work never runs");
 close(observer);close(release);
 reset();release=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::promise<void> entered2;auto ready2=entered2.get_future();late=0;
 check(gamepadSerialSubmit([&]{entered2.set_value();WaitForSingleObject(release,3000);}),"terminal-fence scenario owns a new actual worker");++actualWorkers;
 check(ready2.wait_for(std::chrono::seconds(2))==std::future_status::ready,"terminal-fence worker entered latch");observer=duplicate();
 check(gamepadSerialSubmit([&]{++late;}),"pre-fence queue contains an unclaimed job");g_exitRequested=true;SetEvent(release);
 const auto terminalWait=WaitForSingleObject(observer,100);gamepadSerialStop(2000);
 check(late==0,"exit intent prevents an already queued unclaimed generic job from running before backend Stop");
#ifdef YMCC_FIXTURE_SERIAL_EXIT_PROOF
 check(terminalWait==WAIT_OBJECT_0,"worker honors terminal exit even before backend Stop reaches it");
 reset();std::promise<bool> self;auto selfResult=self.get_future();
 check(gamepadSerialSubmit([&]{self.set_value(gamepadSerialStop(0));}),"self-stop scenario owns actual worker");++actualWorkers;
 check(selfResult.wait_for(std::chrono::seconds(2))==std::future_status::ready&&!selfResult.get(),"worker self-stop refuses self wait and retains original owner");
 gamepadSerialStop(2000);check(gamepadSerialExitOwnershipReleased(),"external owner joins the self-stopped worker");
 reset();HANDLE release3=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::promise<void> entered3;auto ready3=entered3.get_future();
 check(gamepadSerialSubmit([&]{entered3.set_value();WaitForSingleObject(release3,3000);}),"concurrent-stop scenario owns actual worker");++actualWorkers;
 check(ready3.wait_for(std::chrono::seconds(2))==std::future_status::ready,"concurrent-stop worker entered latch");
 std::atomic<bool> joined{false};std::thread owner([&]{joined=gamepadSerialStop(2000);});
 const auto until=GetTickCount64()+1000;bool stopping=false;while(GetTickCount64()<until){{std::lock_guard<std::mutex> lock(g_gamepadSerialMx);stopping=g_gamepadSerialStopping;}if(stopping)break;Sleep(1);}
 check(stopping,"first real retirement owner entered Stop");const auto otherBegin=GetTickCount64();
 check(!gamepadSerialStop(0)&&!gamepadSerialExitOwnershipReleased(),"competing Stop and UI probe retain rather than double join current owner");
 check(GetTickCount64()-otherBegin<100,"competing Stop and UI probe do not block on retirement mutex");
 SetEvent(release3);owner.join();check(joined&&gamepadSerialExitOwnershipReleased(),"one retirement owner joins the actual worker and publishes final completion");close(release3);
#endif
 close(observer);close(release);
#ifdef YMCC_FIXTURE_SERIAL_EXIT_PROOF
 reset();HANDLE release4=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::promise<void> entered4;auto ready4=entered4.get_future();std::atomic<unsigned> racedJob{0};
 check(gamepadSerialSubmit([&]{entered4.set_value();WaitForSingleObject(release4,3000);}),"claim-race scenario owns actual worker");++actualWorkers;
 check(ready4.wait_for(std::chrono::seconds(2))==std::future_status::ready,"claim-race worker entered latch");HANDLE observer4=duplicate();
 check(gamepadSerialSubmit([&]{++racedJob;}),"claim-race has one unclaimed queued job");fixtureSerialClaimRaceArmed=true;SetEvent(release4);
 check(WaitForSingleObject(observer4,2000)==WAIT_OBJECT_0,"exit published after cached closing read still terminates actual worker");
 gamepadSerialStop(2000);check(!fixtureSerialClaimRaceArmed&&g_exitRequested&&racedJob==0&&gamepadSerialExitOwnershipReleased(),"actual claim CAS rejects exit published after cached closing read");close(observer4);close(release4);
#endif
 g_exitRequested=true;bool revived=false;{std::lock_guard<std::mutex> lock(g_gamepadSerialMx);revived=gamepadSerialStartLocked();check(!revived,"eager late start cannot revive terminal serial lane");}
 if(revived)gamepadSerialStop(2000); // a deliberately broken old eager guard still needs fixture cleanup

 cases.back()["actualWorkers"]=actualWorkers;cases.back()["elapsedMs"]=GetTickCount64()-begin;cases.back()["passed"]=failures.size()==before;
}

static constexpr const char* serialEntryDiagnosticVariants[]={"role-log-throw"};
static std::wstring fixtureSerialEntryTerminatePath;
static DWORD fixtureSerialEntryMainThread=0;
static void fixtureSerialEntryTerminate() noexcept {
 try {
  std::string detail="unknown";const auto exception=std::current_exception();
  if(exception)try{std::rethrow_exception(exception);}catch(const std::exception& e){detail=e.what();}catch(...){detail="non-standard";}
  const auto thread=GetCurrentThreadId();
  const json marker={{"schema","actual-serial-entry-terminate.v1"},{"event","actual-std-thread-uncaught-optional-role-log"},
   {"processId",GetCurrentProcessId()},{"mainThreadId",fixtureSerialEntryMainThread},{"terminateThreadId",thread},
   {"onWorkerThread",thread!=fixtureSerialEntryMainThread},{"exceptionDetail",detail},{"handlerExitCode",78},
   {"normalFixtureCompleted",false},{"physicalDeviceIo",false}};
  std::ofstream file{fspath::path(fixtureSerialEntryTerminatePath)};file<<marker.dump(2);file.flush();
 }catch(...){}
 std::_Exit(78); // this private native test process only; avoid a WER/dialog prompt
}
static void fixtureSerialEntryInstallTerminateObserver() noexcept { std::set_terminate(fixtureSerialEntryTerminate); }
static void serialEntryDiagnosticCases(){
 seed();cases.push_back({{"name","serial-entry-diagnostic/role-log-throw"}});const auto beforeFailures=failures.size();
 check(!g_gamepadSerialStarted&&!g_gamepadSerialThread.joinable()&&g_gamepadSerialQ.empty(),"entry diagnostic scenario begins without a prior worker");
 fixtureSerialEntryTerminatePath=(fspath::path(g_yemanccDataDir)/L"serial-entry-terminate.json").wstring();fixtureSerialEntryMainThread=GetCurrentThreadId();
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);const auto previous=std::set_terminate(fixtureSerialEntryTerminate);
 g_exitRequested=false;g_gamepadSerialStopping=false;throwLogEvent="thread-role";
 std::promise<DWORD> complete;auto ready=complete.get_future();unsigned jobs=0;
 const bool accepted=gamepadSerialSubmit([&]{++jobs;complete.set_value(GetCurrentThreadId());});
 check(accepted,"real serial queue accepts the current job after thread construction");
 const bool ran=ready.wait_for(std::chrono::seconds(2))==std::future_status::ready;
 check(ran,"optional worker role log exception cannot prevent the current job from running");
 DWORD worker=ran?ready.get():0;gamepadSerialStop();
 check(worker&&worker!=fixtureSerialEntryMainThread&&jobs==1,"current job executes exactly once on an actual separate std thread");
 check(throwLogEvent==nullptr,"actual diagnostic failure endpoint was consumed by the worker");
 check(!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted,"actual Stop joins the worker after the isolated optional exception");
 std::set_terminate(previous);cases.back()["actualWorker"]={{"mainThreadId",fixtureSerialEntryMainThread},{"jobThreadId",worker},{"jobs",jobs},{"joined",!g_gamepadSerialThread.joinable()}};
 cases.back()["passed"]=failures.size()==beforeFailures;
}

static constexpr const char* serialThreadStartVariants[]={"eager","generic","deadline","frame","haptic"};
template<class F>static bool fixtureSerialInvokeStart(F&& call){if constexpr(std::is_same_v<std::invoke_result_t<F>,bool>)return call();else {call();return true;}}
static void serialThreadStartCases(){
 for(const char* label:serialThreadStartVariants){
  seed();const std::string variant=label;const auto beforeFailures=failures.size();cases.push_back({{"name",std::string("serial-thread-start/")+variant}});
  {std::lock_guard<std::mutex> lock(g_gamepadSerialMx);check(!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted&&g_gamepadSerialQ.empty(),"serial scenario starts with no owned worker or queued job");g_gamepadSerialStopping=false;}
  g_exitRequested=false;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_hardwareWriteGate=true;
  g_msiFeedbackSourceGeneration=0;g_xinputPrimarySlot=0;g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
  g_xinputVirtualEndpointMask=0;g_xinputSlotUnknownMask=0;g_xinputSlotReprobeMask=0;g_feedbackSlotGeneration[0]=1;
  const auto identity=g_xinputSlotIdentityEpoch.load();const auto power=currentPowerGeneration();g_xinputSlotIdentityResolvedEpoch[0]=identity;
  (void)g_xinputFeedbackIdentity[0].observe({true,true,false,false,L"YMCC#SERIAL-OFFLINE-PHYSICAL",ymcc::input::identityRefresh::Kind::Physical,identity,identity,power,power});
  fixtureSerialCreationAttempts=0;fixtureSerialFailCreation=true;unsigned rejectedJobRuns=0,currentJobRuns=0;bool accepted=true,threw=false;
  InputFrameSubmission frame;
  if(variant=="frame"){
   g_inputHostEpoch=17;g_inputHostEpochMirror=17;g_inputHostGameMotorBindingGeneration=1;
   auto source=fullPipeReadNow(1);frame.sample=inputHostFrameSubmissionSample(&source);frame.ticket={g_inputHostEpoch,currentPowerGeneration(),g_inputHostVirtualIntentRevision.load(),false};
   frame.sourcePower=source.sharedPower;
   {std::lock_guard<InputHostMutex> powerLock(g_inputHostMx);
    frame.powerScope=g_inputHostInputPowerCache.acquire(&readSharedSystemPower,g_sharedPowerRevocations,sgShouldKeepInputDuringStandby());}
   frame.physicalSourceAdmitted=true;frame.feedbackSource={ymcc::touchhaptic::Backend::XInput,1,identity,power,0,g_xinputTopologyEpoch.load()};
   frame.gameMotorReply=ymcc::game_feedback::fromWords({1,1,0x12,0xA5,1,1},0x3f);
   check(nativeGameFeedbackFrameCurrent({frame.ticket,frame.sample,frame.feedbackSource,frame.gameMotorReply,true}),"serial motor case reaches actual current-frame admission before thread failure");
  }
  if(variant=="haptic")check(nativePhysicalFeedbackReady().ready(),"touch upstream has an admitted inert physical source before request");
  try{
   if(variant=="eager"){std::lock_guard<std::mutex> lock(g_gamepadSerialMx);accepted=fixtureSerialInvokeStart([](){return gamepadSerialStartLocked();});}
   else if(variant=="generic")accepted=gamepadSerialSubmit([&]{++rejectedJobRuns;});
   else if(variant=="deadline")accepted=gamepadSerialScheduleFeedbackStop(1,10);
   else if(variant=="frame")nativeSubmitGameFeedbackFrame(frame,true);
   else nativeScreenTouchpadHapticPulse(1,true,ymcc::touchhaptic::kStrengthPercent);
  }catch(...){threw=true;}
  check(fixtureSerialCreationAttempts==1&&!g_gamepadSerialThread.joinable(),"actual first serial constructor boundary failed without creating an OS thread");
  check(!threw,"serial constructor failure is isolated at worker creation instead of escaping its caller");
  check(!g_gamepadSerialStarted&&g_gamepadSerialQ.empty()&&!g_gamepadSerialHaptic&&!g_gamepadSerialFeedbackStop,"failed constructor publishes no worker or admitted serial mailbox ownership");
  if(variant=="eager"||variant=="generic"||variant=="deadline")check(!accepted,"actual serial caller rejects unstarted execution");
  if(variant=="frame"){
   check(!g_gamepadSerialGameFeedback.snapshot().desired&&!g_gamepadSerialGameInterested,"failed optional motor submission leaves no orphan serial demand");
   gamepadSerialRevokeGameFeedback(); // actual return of old-source control state before the isolated recovery job
  }
  if(variant=="haptic"){
   const auto next=g_touchHapticDispatch.request(1,true,GetTickCount64(),currentPowerGeneration(),ymcc::touchhaptic::kStrengthPercent);
   check(next.has_value(),"real touch caller returns failed ticket so next pulse is not blocked by pending");if(next)g_touchHapticDispatch.finish(*next);
  }
  cases.back()["afterConstructorFailure"]={{"escaped",threw},{"accepted",accepted},{"started",g_gamepadSerialStarted},{"constructorAttempts",fixtureSerialCreationAttempts}};
  std::promise<void> complete;auto ready=complete.get_future();
  const bool nextAccepted=gamepadSerialSubmit([&]{++currentJobRuns;complete.set_value();});
  const bool ran=ready.wait_for(std::chrono::seconds(2))==std::future_status::ready;
  check(nextAccepted&&ran&&fixtureSerialCreationAttempts==2,"new current job creates one real worker and runs after construction pressure is removed");
  gamepadSerialStop();
  check(!g_gamepadSerialThread.joinable()&&!g_gamepadSerialStarted&&g_gamepadSerialQ.empty()&&currentJobRuns==1&&rejectedJobRuns==0,"actual Stop joins the owned worker and rejected job is never replayed");
  cases.back()["actualThreadRecovery"]={{"currentJobRuns",currentJobRuns},{"rejectedJobRuns",rejectedJobRuns},{"constructorAttempts",fixtureSerialCreationAttempts},{"threadJoinableAfterStop",g_gamepadSerialThread.joinable()}};
  cases.back()["passed"]=failures.size()==beforeFailures;
 }
}

static constexpr const char* startupExceptionOwnerVariants[]={"healthy-waiting","incomplete-current","incomplete-retired"};
static void startupExceptionOwnerCases(){
 for(const char* raw:startupExceptionOwnerVariants){
  const std::string variant=raw;seed();fixtureFullPipe=false;
  g_startupInputState=ymcc::StartupInputState::Waiting;g_startupInputAttempted=false;g_startupInputStartSucceeded=false;
  g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
  g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="PC";g_machineIdentity.product="unregistered-desktop";
  g_msiDinput.enabled=false;g_msiDinput.mainInterfaceMatched=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=1;fixturePcPowerCapabilities=true;reg_fs();
  cases.push_back({{"name",std::string("startup-exception-owner/")+variant}});const auto beforeFailures=failures.size();
  const auto base=targetDocument("disabled",0),on=targetDocument("steamdeck",1);
  check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump())&&writeTarget(base,on).value("ok",false),"startup exception uses actual private player setting admission");inputCaptureRefreshRealStickSettings(true);
  if(variant=="healthy-waiting")throwLogEvent="input-host-started";
  else {g_startEntrySeam.pipeThrowCommand="HELLO";g_startEntrySeam.hostExitWait=WAIT_TIMEOUT;}
  check(inputHostQueueLifecycleWork("persona-transition"),"Waiting-state request is consumed through actual dispatcher");inputHostRunQueuedLifecycleWork();
  std::vector<std::string> cleanupReasons;{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);for(const auto& work:g_inputHostLifecycleWorkQueue){const auto reason=fixtureLifecycleWorkReason(work);if(reason.rfind("fault-stop:",0)==0)cleanupReasons.emplace_back(reason);}}
  cases.back()["afterException"]=state();cases.back()["startupAttempted"]=g_startupInputAttempted;cases.back()["startupSucceeded"]=g_startupInputStartSucceeded;cases.back()["cleanupReasons"]=cleanupReasons;cases.back()["failureTrace"]=g_startEntrySeam.trace;
  throwLogEvent=nullptr;g_startEntrySeam.pipeThrowCommand.clear();g_startEntrySeam.hostExitWait=WAIT_OBJECT_0;
  if(variant=="healthy-waiting"){
   check(g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&!g_inputHostTransportFault&&!g_inputHostReleaseQuarantined,"completed target remains ready after its optional final diagnostic throws");
   check(g_startupInputAttempted&&g_startupInputStartSucceeded&&!g_inputHostFaultHoldsStart,"startup receipt uses accepted target state instead of the diagnostic exception");
   check(cleanupReasons.empty(),"Waiting-state healthy target receives no unrelated startup cleanup");
   fullPipeDrainQueuedLifecycle();check(g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized,"later worker pass cannot Stop an accepted target for optional startup diagnostics");
   cleanup();
  }else{
   check(g_inputHostProcess&&!g_inputHostPrepared&&g_inputHostTransportFault,"incomplete Waiting-state Start retains the actual old Host when exit is unproven");
   const auto expected=ymcc::inputHost::partialStartCleanup::bind(g_inputHostEpoch,g_inputHostRunId);
   check(cleanupReasons==std::vector<std::string>{expected},"startup fallback preserves one original typed cleanup instead of adding a bare second Stop");
   if(variant=="incomplete-current"){
    fullPipeDrainQueuedLifecycle();check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined,"current Waiting-state partial Start still returns its original owner");
   }else{
    const auto oldEpoch=g_inputHostEpoch;const auto oldRun=g_inputHostRunId;
    cleanup();check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined,"controlled alternate return settles old Host before replacement");
    startProtectionClock+=ymcc::VirtualStartProtection::maxMs;fixturePowerClockOffset+=ymcc::VirtualStartProtection::maxMs;
    const auto next=targetDocument("dualsense-edge",2);
    check(writeTarget(on,next).value("ok",false),"successor uses actual separate player admission");inputCaptureRefreshRealStickSettings(true);
    check(inputHostStart()&&g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized,"successor is created through actual Start after old owned return");
    const auto process=g_inputHostProcess;const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto sequence=g_inputHostSequence;const auto protection=fullPipeProtectionState();
    check(epoch!=oldEpoch||run!=oldRun,"successor has a different actual Host tuple");
    fullPipeDrainQueuedLifecycle();
    check(g_inputHostProcess==process&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&g_inputHostPrepared&&g_inputHostNeutralized&&g_inputHostSequence==sequence,"Waiting-state old exception cannot send Stop commands to successor Host");
    check(fullPipeProtectionState()==protection,"old startup exception cannot settle successor player request");
    cases.back()["afterOldDispatch"]=state();cleanup();
   }
  }
  fullPipeDrainQueuedLifecycle();check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined,"Waiting-state case returns every controlled Host using actual Stop");
  cases.back()["nativeEvents"]=fixtureEvents;cases.back()["trace"]=g_startEntrySeam.trace;cases.back()["passed"]=failures.size()==beforeFailures;
 }
}

static constexpr const char* partialStartPreflightVariants[]={"current-gpd","bare-gpd","retired-epoch-gpd","retired-run-gpd","malformed-gpd","retired-msi","retired-rog"};
static void partialStartCleanupPreflightCases(){
 for(const char* raw:partialStartPreflightVariants){
  seed();const std::string variant=raw;const auto beforeFailures=failures.size();
  cases.push_back({{"name",std::string("partial-start-cleanup-preflight/")+variant}});
  g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration=8;
  g_inputHostEpoch=88;g_inputHostRunId="current-preflight-run";
  g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
  g_machineIdentity.resolved=true;g_machineIdentity.family=variant=="retired-msi"?YmccFamily::MsiClaw:variant=="retired-rog"?YmccFamily::AsusRogAllyX:YmccFamily::Gpd;
  g_machineIdentity.manufacturer=variant=="retired-msi"?"MICRO-STAR INTERNATIONAL CO., LTD.":variant=="retired-rog"?"ASUSTeK COMPUTER INC.":"GPD";
  g_machineIdentity.product=variant=="retired-msi"?"MS-1T91":variant=="retired-rog"?"RC72LA":"G1618-05";
  const auto profile=nativeForegroundProfile();
  const std::wstring own=L"HID\\PREFLIGHT-CURRENT-OWN-HIDE",foreign=L"HID\\PREFLIGHT-FOREIGN-HIDE";
  std::vector<std::wstring> hidden={own,foreign};unsigned reads=0,writes=0,refreshes=0,polls=0,queues=0,cloaks=0;
  const auto oldIo=g_msiNativeForegroundTestIo;g_msiNativeForegroundTestIo={};
  g_msiNativeForegroundTestIo.snapshot=[&](HidHideVisibilitySnapshot& value){++reads;value={};value.valid=true;value.hidden=hidden;return true;};
  g_msiNativeForegroundTestIo.visibility=[&](const std::wstring& path,bool hide){++writes;if(hide)hidden.push_back(path);else hidden.erase(std::remove(hidden.begin(),hidden.end(),path),hidden.end());return true;};
  g_msiNativeForegroundTestIo.cloak=[&](bool){++cloaks;return true;};
  g_msiNativeForegroundTestIo.durableReturn=[](const auto&){};
  g_msiNativeForegroundTestIo.poll=[&](){++polls;return -1;};
  g_msiNativeForegroundTestIo.cycle=[&](const auto&,const auto&,bool){++refreshes;return 2;};
  g_msiNativeForegroundTestIo.gameRefresh=[&](const auto&,const auto&){++refreshes;return 2;};
  g_msiNativeForegroundTestIo.xusbRefresh=[&](const auto&,const auto&){++refreshes;return 2;};
  g_msiNativeForegroundTestIo.queue=[&](){++queues;};g_msiNativeForegroundTestIo.virtualQueue=[&](const auto&){++queues;};
  g_msiNativeForegroundLease=MsiNativeForegroundLease{};
  auto& lease=g_msiNativeForegroundLease;lease.profile=profile;lease.active=true;lease.owned={own};lease.before.hidden={foreign};
  lease.root="USB\\PREFLIGHT-EXACT-ROOT";lease.power=8;lease.edge=7;lease.virtualHandoffHeld=true;
  lease.virtualHandoffRequest=99;lease.deferredVirtualWork={"persona-transition","settings-changed"};
  lease.syntheticCycle=true;lease.cycleIssued=true;lease.helperDiagnostic={{"owner","current-lease"}};
  g_msiNativeForegroundCyclePending=true;g_msiNativeForegroundSourceRetryNeeded=true;
  std::string reason=ymcc::inputHost::partialStartCleanup::bind(87,"retired-preflight-run");
  if(variant=="current-gpd")reason=ymcc::inputHost::partialStartCleanup::bind(88,g_inputHostRunId);
  else if(variant=="bare-gpd")reason="fault-stop:partial-start-exception";
  else if(variant=="retired-run-gpd")reason=ymcc::inputHost::partialStartCleanup::bind(88,"retired-preflight-run");
  else if(variant=="malformed-gpd")reason="fault-stop:partial-start-exception:bad:tuple";
  const bool current=variant=="current-gpd"||variant=="bare-gpd";
  check(profile!=ymcc::nativeForeground::Profile::Unsupported,"preflight case has a real enabled family consumer");
  const bool allowed=msiNativeForegroundBeforeVirtualWork(reason);
  cases.back()["reason"]=reason;cases.back()["observed"]={{"allowed",allowed},{"reads",reads},{"writes",writes},{"refreshes",refreshes},{"polls",polls},{"queues",queues},{"cloaks",cloaks},{"active",lease.active},{"configurationReturned",lease.configurationReturned},{"virtualHandoffHeld",lease.virtualHandoffHeld},{"virtualContinuationEnded",lease.virtualContinuationEnded},{"pending",lease.deferredVirtualWork}};
  if(current){
   check(!allowed&&lease.active&&lease.configurationReturned&&hidden==std::vector<std::wstring>{foreign}&&reads==3&&writes==1,"current and synchronous fault keep exact hide return while live helper remains owned");
   check(lease.virtualContinuationEnded&&!lease.virtualHandoffHeld&&lease.virtualHandoffRequest==0&&std::none_of(lease.deferredVirtualWork.begin(),lease.deferredVirtualWork.end(),[](const auto& value){return value=="persona-transition"||value=="settings-changed";}),"current fault still cancels its own GPD handoff without replaying parked Start");
  }else{
   check(!allowed&&reads+writes+refreshes+polls+queues+cloaks==0&&hidden==std::vector<std::wstring>({own,foreign}),"retired partial cleanup cannot touch successor native visibility or helper I/O");
   check(lease.active&&!lease.configurationReturned&&!lease.restorePending&&lease.virtualHandoffHeld&&!lease.virtualContinuationEnded&&lease.virtualHandoffRequest==99&&lease.deferredVirtualWork==std::vector<std::string>({"persona-transition","settings-changed"})&&g_msiNativeForegroundSourceRetryNeeded,"retired cleanup preserves successor handoff and native-only pending state");
  }
  check(g_msiNativeForegroundCyclePending&&lease.syntheticCycle&&lease.owned==std::vector<std::wstring>{own}&&lease.helperDiagnostic==json{{"owner","current-lease"}}&&refreshes+polls+queues+cloaks==0,"preflight never replaces unresolved helper or borrows another duty");
  g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundCyclePending=false;g_msiNativeForegroundSourceRetryNeeded=false;g_msiNativeForegroundTestIo=oldIo;
  g_inputHostEpoch=0;g_inputHostRunId.clear();cases.back()["passed"]=failures.size()==beforeFailures;
 }
}

static constexpr const char* partialStartOwnerVariants[]={"current-owner","retired-owner"};
static void partialStartCleanupOwnerCases(){
 for(const char* variant:partialStartOwnerVariants){
  seed();fixtureFullPipe=false;g_startupInputState=ymcc::StartupInputState::Ready;
  g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
  g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="PC";g_machineIdentity.product="unregistered-desktop";
  g_msiDinput.enabled=false;g_msiDinput.mainInterfaceMatched=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=1;fixturePcPowerCapabilities=true;reg_fs();
  cases.push_back({{"name",std::string("partial-start-cleanup-owner/")+variant}});const auto beforeFailure=failures.size();
  const auto base=targetDocument("disabled",0),on=targetDocument("steamdeck",1);
  check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump())&&writeTarget(base,on).value("ok",false),"partial exception test uses original private settings admission");inputCaptureRefreshRealStickSettings(true);
  g_startEntrySeam.pipeThrowCommand="HELLO";g_startEntrySeam.hostExitWait=WAIT_TIMEOUT;
  check(inputHostQueueLifecycleWork("persona-transition"),"actual partial Start is queued before exception");inputHostRunQueuedLifecycleWork();
  std::string cleanupWork;{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);for(const auto& work:g_inputHostLifecycleWorkQueue){const auto reason=fixtureLifecycleWorkReason(work);if(reason.rfind("fault-stop:partial-start-exception",0)==0)cleanupWork=reason;}}
  cases.back()["actualQueuedCleanup"]=cleanupWork;cases.back()["afterFailure"]=state();cases.back()["failureTrace"]=g_startEntrySeam.trace;
  check(!cleanupWork.empty()&&g_inputHostProcess&&!g_inputHostPrepared&&g_inputHostTransportFault,"actual exception producer retains its original Host and queues cleanup when exit is unproven");
  if(cleanupWork.empty()||!g_inputHostProcess){cases.back()["passed"]=false;cleanup();continue;}
  const auto oldEpoch=g_inputHostEpoch;const auto oldRun=g_inputHostRunId;
  g_startEntrySeam.pipeThrowCommand.clear();g_startEntrySeam.hostExitWait=WAIT_OBJECT_0;
  if(std::string_view(variant)=="current-owner"){
   fullPipeDrainQueuedLifecycle();
   check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined,"same-owner deferred partial cleanup still completes actual owned return");
  }else{
   // Controlled alternate return/Start ordering. This calls the existing
   // cleanup and actual new player admission; it does not prove a field scheduler.
   cleanup();check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined,"alternate old return actually releases original ownership before replacement");
   startProtectionClock+=ymcc::VirtualStartProtection::maxMs;fixturePowerClockOffset+=ymcc::VirtualStartProtection::maxMs;
   const auto next=targetDocument("dualsense-edge",2);
   check(writeTarget(on,next).value("ok",false),"replacement comes from a separately admitted real player setting");inputCaptureRefreshRealStickSettings(true);
   check(inputHostStart()&&g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized,"replacement is created through actual Start with controlled OS and SDK leaves");
   const auto process=g_inputHostProcess;const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto sequence=g_inputHostSequence;
   const auto protection=fullPipeProtectionState();
   check(epoch!=oldEpoch||run!=oldRun,"replacement differs from the original partial Start tuple");
   inputHostRunQueuedLifecycleWork();
   check(g_inputHostProcess==process&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&g_inputHostPrepared&&g_inputHostNeutralized&&g_inputHostSequence==sequence,
    "retired partial-Start cleanup cannot Stop or send target commands to successor Host");
   check(fullPipeProtectionState()==protection,"retired partial cleanup cannot settle or renew successor request");
   cases.back()["successor"]={{"oldEpoch",oldEpoch},{"oldRun",oldRun},{"newEpoch",epoch},{"newRun",run},{"afterDispatch",state()},{"requestBefore",protection},{"requestAfter",fullPipeProtectionState()}};
   cleanup();
  }
  fullPipeDrainQueuedLifecycle();
  check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined,"partial cleanup case returns every controlled Host through actual Stop");
  cases.back()["nativeEvents"]=fixtureEvents;cases.back()["trace"]=g_startEntrySeam.trace;cases.back()["passed"]=failures.size()==beforeFailure;
 }
}


// Explicit parameter of the existing native-only coordinator fixture. Actual
// wake/queue/Start/Session execute; OS/Host ports remain the original inert ends.
#ifdef YMCC_GPD_WAKE_R4_BOUNDARY_PROBES
// These component probes run only at the existing actual ensure:main seam.
// They seed no physical source and launch no helper/managed Host. A temporarily
// controlled lease proves Context versus Execution, never a complete Cycle.
struct GpdWakeBorrowedHostUnlock {
 GpdWakeBorrowedHostUnlock(){
  if(!g_inputHostMx.ownedByCurrentThread())throw std::logic_error("GPD probe requires actual Start Host ownership");
  g_inputHostMx.unlock();
 }
 ~GpdWakeBorrowedHostUnlock(){g_inputHostMx.lock();}
 GpdWakeBorrowedHostUnlock(const GpdWakeBorrowedHostUnlock&)=delete;
 GpdWakeBorrowedHostUnlock& operator=(const GpdWakeBorrowedHostUnlock&)=delete;
};
struct GpdWakeJoinedObserver {
 std::thread thread;
 explicit GpdWakeJoinedObserver(std::function<void()> body):thread(std::move(body)){}
 ~GpdWakeJoinedObserver(){if(thread.joinable())thread.join();}
 GpdWakeJoinedObserver(const GpdWakeJoinedObserver&)=delete;
 GpdWakeJoinedObserver& operator=(const GpdWakeJoinedObserver&)=delete;
};
struct GpdWakeLeaseRestore {
 MsiNativeForegroundLease previous;
 GpdWakeLeaseRestore(){
  std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
  previous=std::move(g_msiNativeForegroundLease);
  g_msiNativeForegroundLease=MsiNativeForegroundLease{};
 }
 ~GpdWakeLeaseRestore(){
  std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
  g_msiNativeForegroundLease=std::move(previous);
 }
 GpdWakeLeaseRestore(const GpdWakeLeaseRestore&)=delete;
 GpdWakeLeaseRestore& operator=(const GpdWakeLeaseRestore&)=delete;
};
struct GpdWakeClockInterleaveRestore {
 std::function<void()> previous=std::move(fixtureMonotonicInterleave);
 ~GpdWakeClockInterleaveRestore(){fixtureMonotonicInterleave=std::move(previous);}
};
static void gpdFirstOnWakeFakeTlsProbe(std::uint64_t ticket,const ymcc::inputHost::wakeExecutionRuntime::Work& armed){
 namespace wake=ymcc::inputHost::wakeExecutionRuntime;
 std::lock_guard<InputHostMutex> hostLock(g_inputHostMx);
 check(!wake::active,"GPD forged TLS control starts outside any actual wake Scope");
 auto unbound=armed;unbound.gpdStartBound=false;
 // Real Scope with an explicitly unbound Work models unrelated TLS deadlines.
 // It cannot turn an expired player view into normal-wake authority.
 std::uint64_t serial=999,remaining=0;
 {wake::Scope unrelated(unbound);remaining=inputHostGpdStartRemaining(ticket,"elite",inputHostStartProtectionNow(),&serial);}
 check(!remaining&&!serial&&!wake::active,"GPD unbound TLS deadline cannot authorize the expired first ON or publish a wake cookie");
 cases.back()["fakeTlsProbe"]={{"remainingMs",remaining},{"wakeCookie",serial},{"scopeRestored",wake::active==nullptr}};
}
static void gpdFirstOnWakeHelperProbe(bool expiredOrigin,std::uint64_t ticket){
 namespace wake=ymcc::inputHost::wakeExecutionRuntime;
 const auto* actualScope=wake::active;
 check(g_inputHostMx.ownedByCurrentThread()&&actualScope&&actualScope->gpdStartBound,
   "GPD helper probe is entered by actual Start with its real bound wake Scope and Host lock");
 if(!g_inputHostMx.ownedByCurrentThread()||!actualScope||!actualScope->gpdStartBound)return;
 const auto originalWork=*actualScope;
 const auto originalStartDeadline=g_inputHostSwitchRecoveryStartDeadline;
 const auto originalStartAttempts=wake::episode.startAttempts,originalRecreateAttempts=wake::episode.recreateAttempts;
 const auto originalPlayer=g_virtualStartProtection.view(inputHostStartProtectionNow());
 std::uint64_t cookie=0;
 const auto remaining=inputHostGpdStartRemaining(ticket,"elite",inputHostStartProtectionNow(),&cookie);
 check(remaining&&cookie==originalWork.serial&&remaining<=originalWork.maxAgeMs&&
   inputHostStartProtectionNow()+remaining<=originalStartDeadline,
   "GPD actual Scope helper returns the captured wake cookie within the original execution deadline");
 auto& proof=cases.back()["helperProbe"];
 proof={{"scope","actual Start ensure:main; controlled lease component only"},{"wakeCookie",cookie},
   {"remainingMs",remaining},{"originalStartDeadline",originalStartDeadline},
   {"originalWakeArmedAt",originalWork.armedAt},{"originalWakeMaxAgeMs",originalWork.maxAgeMs},
   {"physicalSourceSeeded",false},{"completeCycleProven",false}};
 if(!remaining||!cookie)return;
 if(!expiredOrigin){
  // The first real monotonic read is inside retiredLocked, after the initial
  // intent/persona reads in gpdRemainingLocked. Revoke same-persona intent at
  // that existing read-only clock leaf; do not imitate a persona-lock wait.
  unsigned crossings=0;std::uint64_t rejectedCookie=999,rejected=0;
  {GpdWakeClockInterleaveRestore restore;
   fixtureMonotonicInterleave=[&](){if(++crossings==1){InputHostVirtualIntentMutation changed;}};
   rejected=inputHostGpdStartRemaining(ticket,"elite",inputHostStartProtectionNow(),&rejectedCookie);
  }
  check(crossings&& !rejected&&!rejectedCookie&&inputHostConfiguredPersona()=="elite"&&
    g_inputHostVirtualIntentRevision.load()!=originalWork.gpdIntentRevision,
    "GPD helper rechecks same-persona intent revoked after its initial reads without issuing a cookie");
  proof["intentDuringHelper"]={{"clockCrossings",crossings},{"remainingMs",rejected},
    {"cookie",rejectedCookie},{"actualPersonaLockContention",false}};
 }else{
  GpdWakeLeaseRestore restoreLease;
  {
   std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
   auto& lease=g_msiNativeForegroundLease;
   lease.profile=ymcc::nativeForeground::Profile::Gpd;lease.power=originalWork.generation;
   lease.postHideCycle=true;lease.postHideTicket=ticket;lease.postHidePersona="elite";
   lease.postHideWakeSerial=cookie;lease.postHideDeadline=originalStartDeadline;
   lease.postHideEpoch=g_inputHostEpoch;lease.postHideRunId=g_inputHostRunId;lease.postHideHostProcess=g_inputHostProcess;
   check(msiPostHideContextCurrentLocked()&&msiPostHideExecutionCurrentLocked(),
     "GPD real Scope permits both observation and execution for its exact controlled post-hide lease");
   lease.postHideWakeSerial=cookie+1;
   check(!msiPostHideContextCurrentLocked()&&!msiPostHideExecutionCurrentLocked()&&
     !inputHostGpdPostHideWakeRemaining(ticket,"elite",inputHostStartProtectionNow(),cookie+1),
     "GPD wrong wake cookie cannot observe or execute an existing post-hide lease");
   lease.postHideWakeSerial=cookie;
  }
  bool noLockDenied=false,observerContext=false,observerExecution=true,observerTls=true;
  std::uint64_t observerRemaining=0,foreignStart=0,foreignCookie=999;
  std::exception_ptr observerError;
  {
   // Release both lease and borrowed Host ownership BEFORE joining. The
   // observer joins before this RAII scope restores the original Host owner.
   GpdWakeBorrowedHostUnlock releaseHost;
   GpdWakeJoinedObserver observer([&](){try{
    std::uint64_t deniedCookie=999;
    noLockDenied=!inputHostGpdStartRemaining(ticket,"elite",inputHostStartProtectionNow(),&deniedCookie)&&!deniedCookie&&
      !inputHostGpdPostHideWakeRemaining(ticket,"elite",inputHostStartProtectionNow(),cookie);
    std::lock_guard<InputHostMutex> hostLock(g_inputHostMx);
    std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
    observerTls=wake::active!=nullptr;
    observerRemaining=inputHostGpdPostHideWakeRemaining(ticket,"elite",inputHostStartProtectionNow(),cookie);
    observerContext=msiPostHideContextCurrentLocked();observerExecution=msiPostHideExecutionCurrentLocked();
    foreignStart=inputHostGpdStartRemaining(ticket,"elite",inputHostStartProtectionNow(),&foreignCookie);
   }catch(...){observerError=std::current_exception();}});
  }
  if(observerError)std::rethrow_exception(observerError);
  check(noLockDenied,"GPD Start and observer helpers both reject a thread without current Host ownership");
  check(!observerTls&&observerRemaining&&observerContext&&!observerExecution&&!foreignStart&&!foreignCookie,
    "GPD foreign thread may observe the exact retained cookie but cannot borrow Scope to initiate execution");
  check(g_inputHostMx.ownedByCurrentThread()&&wake::active==actualScope,
    "GPD cross-thread probe restores actual Start Host ownership and leaves its TLS Scope unchanged");
  proof["crossThread"]={{"noHostLockDenied",noLockDenied},{"activeScope",observerTls},
    {"observerRemainingMs",observerRemaining},{"context",observerContext},{"execution",observerExecution},
    {"foreignStartRemainingMs",foreignStart},{"foreignCookie",foreignCookie}};
  {
   std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
   auto& lease=g_msiNativeForegroundLease;
   check(msiPostHideContextCurrentLocked()&&msiPostHideExecutionCurrentLocked(),
     "GPD actual Scope still owns controlled lease execution after observer return");
   // Expire only the shorter saved outer lease deadline while the actual
   // observer helper runs. Its wake remainder remains positive.
   const auto savedDeadline=lease.postHideDeadline;lease.postHideDeadline=inputHostStartProtectionNow()+1;
   unsigned crossings=0;bool accepted=true;
   {GpdWakeClockInterleaveRestore restore;
    fixtureMonotonicInterleave=[&](){if(++crossings==1)personaLifecycle140::advance(2);};
    accepted=msiPostHideContextCurrentLocked();
   }
   const auto stillRemaining=inputHostGpdPostHideWakeRemaining(ticket,"elite",inputHostStartProtectionNow(),cookie);
   check(crossings&&!accepted&&stillRemaining&&inputHostStartProtectionNow()>=lease.postHideDeadline,
     "GPD post-hide Context uses fresh time after observer helper crosses its shorter outer deadline");
   proof["outerDeadline"]={{"clockCrossings",crossings},{"context",accepted},{"wakeRemainingMs",stillRemaining}};
   lease.postHideDeadline=savedDeadline; // restore the original value, never extend its budget
  }
  // Now cross the original wake limit at a real clock read during the helper.
  const auto before=GetTickCount64();const auto age=before>=originalWork.armedAt?before-originalWork.armedAt:originalWork.maxAgeMs;
  const auto advanceBy=age<originalWork.maxAgeMs?originalWork.maxAgeMs-age:0;
  unsigned crossings=0;std::uint64_t rejectedCookie=999,rejected=0;
  {GpdWakeClockInterleaveRestore restore;
   fixtureMonotonicInterleave=[&](){if(++crossings==1)personaLifecycle140::advance(advanceBy);};
   rejected=inputHostGpdStartRemaining(ticket,"elite",inputHostStartProtectionNow(),&rejectedCookie);
  }
  {
   std::lock_guard<std::mutex> leaseLock(g_msiNativeForegroundMx);
   check(crossings&&advanceBy&&!rejected&&!rejectedCookie&&!msiPostHideContextCurrentLocked()&&!msiPostHideExecutionCurrentLocked(),
     "GPD helper and retained lease reject expiry of the original wake budget without minting another period");
  }
  proof["wakeBudgetDuringHelper"]={{"clockCrossings",crossings},{"advanceMs",advanceBy},
    {"remainingMs",rejected},{"cookie",rejectedCookie}};
 }
 const auto finalPlayer=g_virtualStartProtection.view(inputHostStartProtectionNow());
 check(finalPlayer.requestId==originalPlayer.requestId&&finalPlayer.fallbackAttempts==originalPlayer.fallbackAttempts&&
   wake::episode.serial==originalWork.serial&&wake::episode.armedAt==originalWork.armedAt&&wake::episode.maxAgeMs==originalWork.maxAgeMs&&
   wake::episode.startAttempts==originalStartAttempts&&wake::episode.recreateAttempts==originalRecreateAttempts&&
   g_inputHostSwitchRecoveryStartDeadline==originalStartDeadline,
   "GPD helper probes preserve the original request wake clocks attempt quota and Start deadline");
}
#endif

static constexpr const char* gpdFirstOnExpiredWakeVariants[]={"pending-control","expired-first-on","off-before-start","new-power-before-start","new-request-before-start","unknown-writer","intent-before-start","wake-budget-before-start"};
static void gpdFirstOnExpiredWakeCases(){
 namespace wake=ymcc::inputHost::wakeExecutionRuntime;
 const personaLifecycle140::ControlledDmi dmi={"GPD","G1618-05","HandheldCompanion.Devices.GPDWin5","/physicalModels/0"};
 for(const std::string variant:gpdFirstOnExpiredWakeVariants){
  const bool expiredOrigin=variant!="pending-control";
  const bool cancelled=variant=="off-before-start"||variant=="new-power-before-start"||variant=="new-request-before-start"||variant=="intent-before-start"||variant=="wake-budget-before-start";
  const bool unknownWriter=variant=="unknown-writer";
  const auto beforeFailures=failures.size();
  personaLifecycle140::prepare(2,"gpd-first-on-expired-wake/"+variant,&dmi);
  fixtureFullPipe=false;
  cases.back()["realManagedPipe"]=false;
  cases.back()["selectedParameter"]="gpd-first-on-expired-wake";
  cases.back()["acceptanceScope"]="native writer/helper/controlled lease components; no complete Cycle or managed Host";
  g_realStickTest.virtualTargetPersona="elite";
  const auto on=targetDocument("elite",0);
  check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"GPD wake boundary writes only its private original ON input");
  const auto origin=g_virtualStartProtection.admit("elite",startProtectionClock);
  const auto originalDeadline=startProtectionClock+g_virtualStartProtection.view(startProtectionClock).remainingMs;
  check(origin.accepted&&origin.requestId&&!g_inputHostProcess&&g_inputHostEpoch==0&&
    !g_inputHostPrepared&&!g_inputHostHello&&fullPipeChildObservers.empty(),
    "GPD wake boundary starts from an admitted first ON that has never owned a Host");
  if(expiredOrigin)personaLifecycle140::advance(ymcc::VirtualStartProtection::maxMs+1);
  const auto beforeWake=g_virtualStartProtection.view(startProtectionClock);
  check(beforeWake.requestId==origin.requestId&&(expiredOrigin?
    beforeWake.phase=="expired"&&!beforeWake.locked&&beforeWake.remainingMs==0:
    beforeWake.phase=="pending"&&beforeWake.locked&&beforeWake.remainingMs>0),
    "GPD wake boundary records the actual original protection phase before resume");
  const std::string unknownBytes="{\"schemaVersion\":999,\"kind\":\"private-invalid-writer-observation\"}";
  const auto writerPath=fspath::path(nativeRefreshWriterRecordPath());
  if(unknownWriter){
   const auto dataRoot=fspath::absolute(fspath::path(yemancc_data_dir())).lexically_normal();
   const auto relative=fspath::absolute(writerPath).lexically_normal().lexically_relative(dataRoot);
   check(!relative.empty()&&!relative.is_absolute()&&*relative.begin()!=L"..",
     "GPD unknown writer record stays inside the fixture private data root");
   fspath::create_directories(writerPath.parent_path());
   std::ofstream output(writerPath,std::ios::binary|std::ios::trunc);output<<unknownBytes;output.flush();
   check(output.good(),"GPD unknown writer boundary publishes only its owned malformed input record");
  }
  const auto generation=currentPowerGeneration()+1;g_powerGeneration=generation;
  g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;
  inputHostRequestWakeRescan(generation);
  const auto armed=wake::episode;
  cases.back()["beforeWake"]={{"requestId",beforeWake.requestId},{"phase",beforeWake.phase},
    {"remainingMs",beforeWake.remainingMs},{"originalDeadline",originalDeadline},
    {"nativeHostEpoch",g_inputHostEpoch},{"wakeSerial",armed.serial},{"wakePower",armed.generation},
    {"protectedPlayer",armed.protectedPlayer},{"playerDeadline",armed.playerDeadline},{"maxAgeMs",armed.maxAgeMs}};
  check(armed.serial&&armed.generation==generation&&armed.requestId==origin.requestId,
    "GPD wake boundary arms the real new-power episode with the original ticket identity");
  check(!expiredOrigin||(!armed.protectedPlayer&&armed.playerDeadline==0),
    "GPD expired first ON resumes as normal wake without reopening the old player period");
  personaLifecycle140::advance(kWakeHandshakeWaitMs+1);
  inputHostConsumeWakeRescan();
  check(g_inputHostLifecycleWorkRequested.load(),"GPD no-Host wake reaches the original lifecycle queue");
  if(variant=="off-before-start")g_realStickTest.virtualTargetEnabled=false;
  if(variant=="new-power-before-start")g_powerGeneration=generation+1;
  if(variant=="new-request-before-start"){
   const auto next=g_virtualStartProtection.admit("elite",startProtectionClock);
   check(next.accepted&&next.requestId!=origin.requestId,"GPD old wake is tested against a real newer admitted player request");
   cases.back()["newRequestId"]=next.requestId;
  }
  if(variant=="intent-before-start"){
   {InputHostVirtualIntentMutation changed;}
   check(inputHostConfiguredPersona()=="elite","GPD intent replacement keeps the same persona without a new player ticket");
  }
  if(variant=="wake-budget-before-start"){
   const auto now=GetTickCount64();const auto age=now>=armed.armedAt?now-armed.armedAt:armed.maxAgeMs;
   if(age<armed.maxAgeMs)personaLifecycle140::advance(armed.maxAgeMs-age);
  }
#ifdef YMCC_GPD_WAKE_R4_BOUNDARY_PROBES
  cases.back()["r4BoundaryProbesCompiled"]=true;
  if(variant=="expired-first-on")gpdFirstOnWakeFakeTlsProbe(origin.requestId,armed);
  bool actualHelperSeam=false;
  if(variant=="pending-control"||variant=="expired-first-on")fixtureGpdTransferSourceAtEnsure=[&](){
   actualHelperSeam=true;gpdFirstOnWakeHelperProbe(expiredOrigin,origin.requestId);
  };
#else
  cases.back()["r4BoundaryProbesCompiled"]=false;
#endif
  {FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
#ifdef YMCC_GPD_WAKE_R4_BOUNDARY_PROBES
  if(variant=="pending-control"||variant=="expired-first-on")check(actualHelperSeam,
    "GPD selected helper probes actually execute at the real Start ensure seam");
#endif
  bool held=false,writerWait=false;unsigned attempts=0;
  for(const auto& event:fixtureEvents){
   if(event.value("event","")=="input-host-native-writer-fence"){
    held=held||event["detail"].value("phase","")=="held";
    writerWait=writerWait||event["detail"].value("phase","")=="wait";
   }
   if(event.value("event","")=="input-host-wake-execution"&&event["detail"].value("stage","")=="start-attempt")++attempts;
  }
  cases.back()["actualWriterFenceHeld"]=held;
  cases.back()["actualWriterSessionRefusalObserved"]=writerWait;
  cases.back()["wakeAttempts"]=wake::episode.startAttempts;
  cases.back()["observedStartAttemptEvents"]=attempts;
  cases.back()["nativeEvents"]=fixtureEvents;
  cases.back()["trace"]=g_startEntrySeam.trace;
  const auto after=g_virtualStartProtection.view(startProtectionClock);
  check(wake::episode.startAttempts==(variant=="intent-before-start"?1u:cancelled?0u:1u),
    "GPD queue retirement forbids Start and same-persona intent revocation is refused by its sole actual Start attempt");
  if(cancelled){
   check(!held&&!g_inputHostProcess&&!g_inputHostHello&&g_startEntrySeam.trace.empty(),
     "GPD retired wake cannot acquire a writer or borrow OFF newer power or newer player ownership");
  }else if(unknownWriter){
   std::ifstream input(writerPath,std::ios::binary);std::ostringstream observed;observed<<input.rdbuf();
   check(writerWait&&!held&&!g_inputHostProcess&&!g_inputHostHello,
     "GPD normal wake reaches the actual writer Session refusal without creating a Host for an unknown record");
   check(observed.str()==unknownBytes,"GPD rejected unknown writer record retains every original byte");
  }else{
   check(held,expiredOrigin?
     "GPD normal wake after expired first ON reaches the actual writer fence without renewing player protection":
     "GPD pending control reaches the actual writer fence through the same wake and Start consumers");
  }
  check(!expiredOrigin||cancelled||(after.requestId==origin.requestId&&after.phase=="expired"&&after.remainingMs==0&&after.fallbackAttempts==0),
    "GPD normal wake does not mint a replacement player ticket or extend its expired thirty-second period");
  check(forbiddenIo==0&&fullPipeChildObservers.empty(),"GPD wake boundary performs no physical IO or real managed Host RPC");
  cases.back()["cleanup"]=cleanup();
  check(!g_inputHostProcess&&!g_inputHostPrepared&&fullPipeChildObservers.empty(),
    "GPD directed wake boundary returns all fixture-owned Host handles before finishing");
  cases.back()["passed"]=failures.size()==beforeFailures;
 }
}

#ifdef YMCC_GPD_PERSONA_MUTEX_PROBES
// Explicit-only successor: the real Start Scope calls the real helper while a
// writer owns the real persona mutex. Observation is an adapted read-only leaf,
// not an unchanged call graph, a fake getter, or an OS WaitChain measurement.
static constexpr const char* gpdPersonaMutexVariants[]={"unchanged-intent","same-persona-replaced-intent"};
struct GpdPersonaMutexProbe {
 std::mutex mx;std::condition_variable cv;
 const std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 bool held=false,holding=false,beforeObserved=false,afterObserved=false,readReturnedWhileHeld=false;
 bool cancelled=false,timedOut=false,writerDone=false,writerException=false,holdWindowCompleted=false;
 unsigned beforeCalls=0,afterCalls=0;
 unsigned long long writerRevisionBefore=0,writerRevisionAfter=0;
 std::string personaWhileHeld;
};
static void gpdPersonaMutexObserve(bool after,void* context){
 auto& probe=*static_cast<GpdPersonaMutexProbe*>(context);
 std::lock_guard<std::mutex> lock(probe.mx);
 if(after){++probe.afterCalls;probe.afterObserved=true;if(probe.holding)probe.readReturnedWhileHeld=true;}
 else{++probe.beforeCalls;probe.beforeObserved=true;}
 probe.cv.notify_all();
}
struct GpdPersonaMutexObservationScope {
 void (*previous)(bool,void*)=fixtureGpdPersonaMutexObserver;
 void* previousContext=fixtureGpdPersonaMutexObserverContext;
 explicit GpdPersonaMutexObservationScope(GpdPersonaMutexProbe& probe){
  fixtureGpdPersonaMutexObserverContext=&probe;fixtureGpdPersonaMutexObserver=&gpdPersonaMutexObserve;
 }
 ~GpdPersonaMutexObservationScope(){
  fixtureGpdPersonaMutexObserver=previous;fixtureGpdPersonaMutexObserverContext=previousContext;
 }
 GpdPersonaMutexObservationScope(const GpdPersonaMutexObservationScope&)=delete;
 GpdPersonaMutexObservationScope& operator=(const GpdPersonaMutexObservationScope&)=delete;
};
struct GpdPersonaMutexWriter {
 GpdPersonaMutexProbe& probe;bool replaceIntent;std::thread thread;
 GpdPersonaMutexWriter(GpdPersonaMutexProbe& value,bool replace):probe(value),replaceIntent(replace),thread([this](){run();}){}
 void run() noexcept {
  try{
   // No Host/runtime lock is taken here. The helper thread retains its actual
   // Host -> wake-metadata -> persona ordering throughout the wait.
   std::unique_lock<std::mutex> personaLock(g_inputHostPersonaMx);
   std::unique_lock<std::mutex> lock(probe.mx);
   probe.held=probe.holding=true;probe.personaWhileHeld=g_realStickTest.virtualTargetPersona;
   probe.writerRevisionBefore=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
   probe.cv.notify_all();
   if(!probe.cv.wait_until(lock,probe.deadline,[&](){return probe.beforeObserved||probe.cancelled;}))probe.timedOut=true;
   if(probe.beforeObserved&&!probe.cancelled&&!probe.timedOut){
    const auto holdUntil=(std::min)(probe.deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(20));
    const bool signalled=probe.cv.wait_until(lock,holdUntil,[&](){return probe.afterObserved||probe.cancelled;});
    probe.readReturnedWhileHeld=probe.readReturnedWhileHeld||probe.afterObserved;
    probe.holdWindowCompleted=!signalled&&std::chrono::steady_clock::now()>=holdUntil;
    if(std::chrono::steady_clock::now()>=probe.deadline)probe.timedOut=true;
    if(!probe.cancelled&&!probe.timedOut&&replaceIntent){InputHostVirtualIntentMutation changed;}
   }
   probe.writerRevisionAfter=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
   personaLock.unlock();probe.holding=false;probe.writerDone=true;probe.cv.notify_all();
  }catch(...){
   std::lock_guard<std::mutex> lock(probe.mx);probe.holding=false;probe.writerException=true;probe.writerDone=true;probe.cv.notify_all();
  }
 }
 void cancelAndJoin(){
  {std::lock_guard<std::mutex> lock(probe.mx);probe.cancelled=true;probe.cv.notify_all();}
  if(thread.joinable())thread.join();
 }
 ~GpdPersonaMutexWriter(){cancelAndJoin();}
 GpdPersonaMutexWriter(const GpdPersonaMutexWriter&)=delete;
 GpdPersonaMutexWriter& operator=(const GpdPersonaMutexWriter&)=delete;
};
static void gpdPersonaMutexHelperProbe(bool replaceIntent,std::uint64_t ticket){
 namespace wake=ymcc::inputHost::wakeExecutionRuntime;
 auto& proof=cases.back()["mutexProbe"];
 proof={{"acceptanceScope","real GPD Start helper plus real persona mutex component only"},
   {"observationPort","read-only configured-persona wrapper; actual getter unchanged; call graph adapted"},
   {"osWaitChainObserved",false},{"completeCycleProven",false},{"managedHostProven",false},{"writerThreads",0}};
 const auto* actualScope=wake::active;
 check(g_inputHostMx.ownedByCurrentThread()&&actualScope&&actualScope->gpdStartBound,
   "GPD persona mutex probe enters the actual Start Scope with current Host ownership");
 if(!g_inputHostMx.ownedByCurrentThread()||!actualScope||!actualScope->gpdStartBound)return;
 const auto originalWork=*actualScope;
 const auto originalDeadline=g_inputHostSwitchRecoveryStartDeadline;
 const auto originalRequest=g_inputHostSwitchRecoveryStartRequestId,originalPower=g_inputHostSwitchRecoveryStartPower;
 const auto originalAttempts=wake::episode.startAttempts,originalRecreates=wake::episode.recreateAttempts;
 const auto originalPlayer=g_virtualStartProtection.view(inputHostStartProtectionNow());
 const auto oldObserver=fixtureGpdPersonaMutexObserver;const auto oldContext=fixtureGpdPersonaMutexObserverContext;
 std::uint64_t initialCookie=0;
 const auto initialRemaining=inputHostGpdStartRemaining(ticket,"elite",inputHostStartProtectionNow(),&initialCookie);
 check(!oldObserver&&initialRemaining&&initialCookie==originalWork.serial&&
   originalPlayer.phase=="expired"&&!originalPlayer.locked&&originalPlayer.remainingMs==0,
   "GPD persona mutex control first confirms real normal-wake authority without renewing expired ON");
 proof["initialRemaining"]=initialRemaining;proof["initialCookie"]=initialCookie;
 proof["originalWork"]={{"serial",originalWork.serial},{"requestId",originalWork.requestId},
   {"generation",originalWork.generation},{"armedAt",originalWork.armedAt},{"maxAgeMs",originalWork.maxAgeMs},
   {"playerDeadline",originalWork.playerDeadline},{"protectedPlayer",originalWork.protectedPlayer},
   {"gpdIntentRevision",originalWork.gpdIntentRevision},{"startDeadline",originalDeadline},
   {"startAttempts",originalAttempts},{"recreateAttempts",originalRecreates}};
 if(oldObserver||!initialRemaining||initialCookie!=originalWork.serial)return;
 GpdPersonaMutexProbe probe;std::uint64_t remaining=0,cookie=999;bool held=false,helperException=false;
 {
  GpdPersonaMutexWriter writer(probe,replaceIntent);proof["writerThreads"]=1;
  {
   std::unique_lock<std::mutex> lock(probe.mx);
   held=probe.cv.wait_until(lock,probe.deadline,[&](){return probe.held||probe.writerDone;})&&probe.held;
  }
  if(held){
   GpdPersonaMutexObservationScope observation(probe);
   try{remaining=inputHostGpdStartRemaining(ticket,"elite",inputHostStartProtectionNow(),&cookie);}
   catch(...){helperException=true;}
  }
  // Cancellation is only private handshaking/exception cleanup. It cannot
  // mutate work, player protection, execution clocks or product intent.
  writer.cancelAndJoin();
 }
 const auto player=g_virtualStartProtection.view(inputHostStartProtectionNow());
 const auto persona=inputHostConfiguredPersona();
 proof["beforeObserved"]=probe.beforeObserved;proof["afterObserved"]=probe.afterObserved;
 proof["readReturnedWhileHeld"]=probe.readReturnedWhileHeld;proof["holdWindowMs"]=20;
 proof["holdWindowCompleted"]=probe.holdWindowCompleted;proof["sharedWaitLimitMs"]=5000;
 proof["beforeCalls"]=probe.beforeCalls;proof["afterCalls"]=probe.afterCalls;
 proof["writerHeld"]=held;proof["writerRevisionBefore"]=probe.writerRevisionBefore;
 proof["writerRevisionAfter"]=probe.writerRevisionAfter;proof["personaWhileHeld"]=probe.personaWhileHeld;
 proof["personaAfter"]=persona;proof["remaining"]=remaining;proof["cookie"]=cookie;
 proof["writerTimedOut"]=probe.timedOut;proof["writerException"]=probe.writerException;
 proof["helperException"]=helperException;proof["writerDone"]=probe.writerDone;proof["writerJoined"]=true;
 proof["observerRestored"]=fixtureGpdPersonaMutexObserver==oldObserver&&fixtureGpdPersonaMutexObserverContext==oldContext;
 proof["afterWork"]={{"serial",wake::episode.serial},{"requestId",wake::episode.requestId},
   {"armedAt",wake::episode.armedAt},{"maxAgeMs",wake::episode.maxAgeMs},
   {"startDeadline",g_inputHostSwitchRecoveryStartDeadline},{"startAttempts",wake::episode.startAttempts},
   {"recreateAttempts",wake::episode.recreateAttempts}};
 check(held&&probe.beforeObserved&&probe.afterObserved&&probe.holdWindowCompleted&&!probe.readReturnedWhileHeld&&
   !probe.timedOut&&!probe.writerException&&!helperException&&probe.writerDone&&!probe.holding,
   "GPD real persona read is observed before waiting and only returns after the writer releases its mutex");
 check(probe.personaWhileHeld=="elite"&&persona=="elite"&&
   probe.writerRevisionBefore==originalWork.gpdIntentRevision&&
   (replaceIntent?probe.writerRevisionAfter!=probe.writerRevisionBefore:probe.writerRevisionAfter==probe.writerRevisionBefore)&&
   !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire),
   "GPD persona mutex writer preserves elite and settles the real same-persona intent mutation before unlocking");
 check(replaceIntent?(!remaining&&!cookie):(remaining&&cookie==originalWork.serial&&remaining<=initialRemaining),
   replaceIntent?"GPD real helper rejects same-persona intent replaced during the actual persona mutex wait":
     "GPD real helper retains original cookie for unchanged intent after the actual persona mutex wait");
 check(wake::active==actualScope&&g_inputHostMx.ownedByCurrentThread()&&
   fixtureGpdPersonaMutexObserver==oldObserver&&fixtureGpdPersonaMutexObserverContext==oldContext,
   "GPD persona mutex probe restores observation and never borrows or reverses the actual Host lock");
 check(player.requestId==originalPlayer.requestId&&player.phase=="expired"&&!player.locked&&!player.remainingMs&&
   player.fallbackAttempts==originalPlayer.fallbackAttempts&&wake::episode.serial==originalWork.serial&&
   wake::episode.requestId==originalWork.requestId&&wake::episode.generation==originalWork.generation&&
   wake::episode.armedAt==originalWork.armedAt&&wake::episode.maxAgeMs==originalWork.maxAgeMs&&
   wake::episode.playerDeadline==originalWork.playerDeadline&&wake::episode.protectedPlayer==originalWork.protectedPlayer&&
   wake::episode.startAttempts==originalAttempts&&wake::episode.recreateAttempts==originalRecreates&&
   g_inputHostSwitchRecoveryStartDeadline==originalDeadline&&g_inputHostSwitchRecoveryStartRequestId==originalRequest&&
   g_inputHostSwitchRecoveryStartPower==originalPower,
   "GPD real persona mutex control preserves original ticket wake ARM deadline and attempt quotas");
}
static void gpdPersonaMutexRaceCases(){
 namespace wake=ymcc::inputHost::wakeExecutionRuntime;
 const personaLifecycle140::ControlledDmi dmi={"GPD","G1618-05","HandheldCompanion.Devices.GPDWin5","/physicalModels/0"};
 for(const std::string variant:gpdPersonaMutexVariants){
  const auto beforeFailures=failures.size();
  personaLifecycle140::prepare(2,"gpd-persona-mutex-race/"+variant,&dmi);fixtureFullPipe=false;
  const auto managedSpawnsBefore=fullPipeSpawnCount;const auto managedReceiptsBefore=fullPipeReceipts.size();
  cases.back()["selectedParameter"]="gpd-persona-mutex-race";cases.back()["realManagedPipe"]=false;
  cases.back()["acceptanceScope"]="real GPD Start helper plus real persona mutex component only; no full Cycle/Host";
  g_realStickTest.virtualTargetPersona="elite";
  const auto on=targetDocument("elite",0);
  check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"GPD persona mutex case writes only its private original ON document");
  const auto origin=g_virtualStartProtection.admit("elite",startProtectionClock);
  check(origin.accepted&&origin.requestId&&!g_inputHostProcess&&!g_inputHostEpoch&&!g_inputHostPrepared&&
    !g_inputHostHello&&fullPipeChildObservers.empty(),"GPD persona mutex case begins at real admitted first ON without a Host");
  personaLifecycle140::advance(ymcc::VirtualStartProtection::maxMs+1);
  const auto beforeWake=g_virtualStartProtection.view(startProtectionClock);
  check(beforeWake.requestId==origin.requestId&&beforeWake.phase=="expired"&&!beforeWake.locked&&!beforeWake.remainingMs,
    "GPD persona mutex case expires the original thirty-second player period before ordinary wake");
  const auto generation=currentPowerGeneration()+1;g_powerGeneration=generation;
  g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;
  inputHostRequestWakeRescan(generation);const auto armed=wake::episode;
  check(armed.serial&&armed.generation==generation&&armed.requestId==origin.requestId&&
    !armed.protectedPlayer&&!armed.playerDeadline,
    "GPD persona mutex case arms the actual ordinary wake with its original expired ticket");
  personaLifecycle140::advance(kWakeHandshakeWaitMs+1);inputHostConsumeWakeRescan();
  check(g_inputHostLifecycleWorkRequested.load(),"GPD persona mutex case reaches the actual lifecycle queue before Start");
  bool actualHelperSeam=false;
  fixtureGpdTransferSourceAtEnsure=[&](){
   actualHelperSeam=true;gpdPersonaMutexHelperProbe(variant=="same-persona-replaced-intent",origin.requestId);
  };
  {FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
  check(actualHelperSeam,"GPD persona mutex case executes its two-thread control at the actual Start ensure seam");
  bool held=false;unsigned attempts=0;
  for(const auto& event:fixtureEvents){
   if(event.value("event","")=="input-host-native-writer-fence"&&event["detail"].value("phase","")=="held")held=true;
   if(event.value("event","")=="input-host-wake-execution"&&event["detail"].value("stage","")=="start-attempt")++attempts;
  }
  cases.back()["actualWriterFenceHeld"]=held;cases.back()["observedStartAttemptEvents"]=attempts;
  cases.back()["ownedManagedHostSpawns"]=fullPipeSpawnCount-managedSpawnsBefore;
  cases.back()["managedPipeReceiptsAdded"]=fullPipeReceipts.size()-managedReceiptsBefore;
  cases.back()["nativeEvents"]=fixtureEvents;cases.back()["trace"]=g_startEntrySeam.trace;
  check(held&&attempts==1&&wake::episode.startAttempts==1,
    "GPD persona mutex controls share one actual native writer admission and one original Start attempt");
  check(forbiddenIo==0&&fullPipeChildObservers.empty()&&!fixtureFullPipe&&
    fullPipeSpawnCount==managedSpawnsBefore&&fullPipeReceipts.size()==managedReceiptsBefore,
    "GPD persona mutex controls issue no physical IO or real managed Host RPC");
  cases.back()["cleanup"]=cleanup();
  check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostHello&&fullPipeChildObservers.empty()&&!fixtureGpdTransferSourceAtEnsure&&
    !fixtureGpdPersonaMutexObserver&&!fixtureGpdPersonaMutexObserverContext,
    "GPD persona mutex controls join owned threads restore POD observation and return fixture Host handles");
  cases.back()["passed"]=failures.size()==beforeFailures;
 }
}
#endif

#if defined(YMCC_FIXTURE_GPD_PENDING_BEFORE_HIDE)
#include "pre_hide_cases.inc"
#endif

int wmain(int argc,wchar_t**argv){try{
#ifdef YMCC_FIXTURE_GPD_NATIVE_WRITER_RETURN
 if(argc==2&&std::wstring_view(argv[1])==L"--gpd-terminal-inert-leaf")return 6;
#endif
 if(argc!=3&&argc!=4&&argc!=5)return 2;const std::wstring mode=argv[2];std::wstring selectedCase;
 if(argc>=4&&std::wstring_view(argv[3])==L"case:onex-parameterized"){
  const auto* family=std::getenv("YMCC_FIXTURE_FAMILY");
  if(argc!=5||mode!=L"family-lifecycle"||!family||std::strcmp(family,"onex")!=0||!*argv[4])return 2;
  std::error_code managedError;if(!fspath::is_regular_file(fspath::path(argv[4]),managedError)||managedError)return 2;
 }
#ifndef YMCC_FIXTURE_OEM_AFTER_OFF
 if(argc==5&&mode==L"persona-lifecycle"&&std::wstring_view(argv[3])==L"case:oem-after-off")return 2;
#endif
 if(argc==5){selectedCase=argv[3];if(!((mode==L"off-lifecycle"&&fixtureSdOffSourceLostSelector(selectedCase))||(mode==L"full-pipe-core"&&((selectedCase==L"case:wake-kept-source"||(selectedCase.rfind(L"case:wake-kept-source:",0)==0&&fixtureWakeSourceSelectionKnown(W2U(selectedCase.substr(std::wstring(L"case:wake-kept-source:").size())))))||selectedCase==L"case:wake-kept-visibility"||(selectedCase==L"case:gpd-expired-first-on-full-cycle"||selectedCase==L"case:gpd-expired-first-on-full-cycle:native-writer")))||(mode==L"persona-lifecycle"&&(selectedCase==L"case:gpd-live-sd-transfer"||selectedCase==L"case:oem-after-off"))||(mode==L"family-lifecycle"&&(selectedCase==L"case:onex-parameterized"||selectedCase==L"case:classified-fault-cleanup-elite"||selectedCase==L"case:classified-fault-cleanup-ps"||selectedCase.rfind(L"case:runtime-after-final:",0)==0))||(mode==L"target-return"&&(fixturePrepareRejectionSelector(selectedCase)||selectedCase==L"case:ps-return-root"||selectedCase==L"case:ps-return-proof"||selectedCase==L"case:ps-nopin-proof"||selectedCase==L"case:ps-return-all"))))return 2;fixtureManagedDll=argv[4];}
 else if(argc==4&&std::wstring_view(argv[3]).rfind(L"case:",0)==0){selectedCase=argv[3];
#if defined(YMCC_FIXTURE_GPD_PENDING_BEFORE_HIDE)
 if(selectedCase.rfind(L"case:gpd-pending-before-hide:",0)==0){if(mode!=L"family-lifecycle")return 2;}else
#endif
#ifdef YMCC_GPD_PERSONA_MUTEX_PROBES
 if(selectedCase==L"case:gpd-persona-mutex-race"){if(mode!=L"switch-recovery")return 2;}else
#endif
 if(mode!=L"switch-recovery"||(selectedCase!=L"case:source-reconfirm-collection"&&selectedCase.rfind(L"case:source-reconfirm-collection:",0)!=0&&selectedCase!=L"case:gpd-first-on-expired-wake"&&selectedCase!=L"case:owned-readback-cli-current"&&selectedCase!=L"case:owned-readback-cli-boundary:host-journal"&&selectedCase!=L"case:owned-readback-cli-boundary"&&selectedCase!=L"case:partial-start-cleanup-owner"&&selectedCase!=L"case:partial-start-cleanup-preflight"&&selectedCase!=L"case:startup-exception-owner"&&selectedCase!=L"case:serial-thread-start"&&selectedCase!=L"case:serial-entry-diagnostic"&&selectedCase!=L"case:serial-exit-owner"&&selectedCase!=L"case:serial-power-action"&&selectedCase!=L"case:serial-config-action"&&selectedCase!=L"case:switch-return-boundaries"))return 2;}else if(argc==4)fixtureManagedDll=argv[3];g_yemanccDataDir=argv[1];g_appDataDir=argv[1];fspath::create_directories(g_yemanccDataDir);fixturePersistOffCheckpoints=mode==L"off-lifecycle"||mode==L"switch-recovery";if(fixturePersistOffCheckpoints){std::set_terminate(fixtureOffTerminate);_set_invalid_parameter_handler(fixtureOffInvalidParameter);}fixtureOffCheckpoint("consumer-entry");fixtureTargetReturnIndependentCase();g_machineIdentity.attempted=true;g_machineIdentity.resolved=true;
 g_msiControllerTraceInventoryForTest=[](){return json{{"known",false},{"scope","offline"}};};g_msiControllerOfficialStackForTest=[](){return json{{"scope","offline"},{"modeWriterProven",false}};};
 bool expectedFailure=false,ok=false;
 if(mode==L"entry"||mode==L"mutation1"||mode==L"mutation2"){const int mutation=mode==L"entry"?0:mode==L"mutation1"?1:2;expectedFailure=mutation!=0;ok=msiStartOrderEntrySelfTest(mutation);}
 else if(mode==L"rollback"){rollbackCases();fixtureTargetReturnNativeCases();ok=failures.empty();}
 else if(mode==L"queued-exception"){queuedExceptionCase(false);ok=failures.empty();}
 else if(mode==L"queued-exception-common"){queuedExceptionCase(true);ok=failures.empty();}
 else if(mode==L"healthy-log-exception"){healthyDiagnosticExceptionCase();ok=failures.empty();}
 else if(mode==L"start-protection"){startProtectionCases();ok=failures.empty();}
 else if(mode==L"recovery-protection"){recoveryProtectionCases();ok=failures.empty();}
 else if(mode==L"record-read"){recordReadCases();ok=failures.empty();}
 else if(mode==L"switch-recovery"){
#ifdef YMCC_GPD_PERSONA_MUTEX_PROBES
 if(selectedCase==L"case:gpd-persona-mutex-race")gpdPersonaMutexRaceCases();else
#endif
 if(selectedCase==L"case:switch-return-boundaries")switchRecoveryCases(4);else if(selectedCase==L"case:source-reconfirm-collection")sourceReconfirmCollectionCases();else if(selectedCase.rfind(L"case:source-reconfirm-collection:",0)==0)sourceReconfirmCollectionCases(W2U(selectedCase.substr(std::wstring(L"case:source-reconfirm-collection:").size())));else if(selectedCase==L"case:gpd-first-on-expired-wake")gpdFirstOnExpiredWakeCases();else if(selectedCase==L"case:owned-readback-cli-current")switchRecoveryCases(2);else if(selectedCase==L"case:owned-readback-cli-boundary:host-journal")switchRecoveryCases(3);else if(selectedCase==L"case:owned-readback-cli-boundary")switchRecoveryCases(1);else if(selectedCase==L"case:partial-start-cleanup-owner")partialStartCleanupOwnerCases();else if(selectedCase==L"case:partial-start-cleanup-preflight")partialStartCleanupPreflightCases();else if(selectedCase==L"case:startup-exception-owner")startupExceptionOwnerCases();else if(selectedCase==L"case:serial-thread-start")serialThreadStartCases();else if(selectedCase==L"case:serial-entry-diagnostic")serialEntryDiagnosticCases();else if(selectedCase==L"case:serial-exit-owner")serialExitOwnerCases();else if(selectedCase==L"case:serial-power-action")serialPowerActionCases();else if(selectedCase==L"case:serial-config-action")serialConfigActionCases();else switchRecoveryCases();ok=failures.empty();}
 else if(mode==L"tuple-protection"){protectedTupleRecoveryCases();ok=failures.empty();}
 else if(mode==L"persona-lifecycle"){
#ifdef YMCC_FIXTURE_OEM_AFTER_OFF
 if(selectedCase==L"case:oem-after-off")personaLifecycle140::oemAfterOffCases();else
#endif
 if(selectedCase==L"case:gpd-live-sd-transfer")personaLifecycle140::gpdLiveSdTransferCases();else personaLifecycleCases140();ok=failures.empty();}
 else if(mode==L"family-lifecycle"){
#if defined(YMCC_FIXTURE_GPD_PENDING_BEFORE_HIDE)
 if(selectedCase.rfind(L"case:gpd-pending-before-hide:",0)==0)preHide183409::run(W2U(selectedCase.substr(std::wstring(L"case:gpd-pending-before-hide:").size())));else
#endif
 if(selectedCase.rfind(L"case:runtime-after-final:",0)==0){
 const auto value=W2U(selectedCase.substr(25));const auto separator=value.find(':');
 if(separator==std::string::npos)return 2;const auto edge=value.find(':',separator+1);
 if(edge!=std::string::npos&&value.find(':',edge+1)!=std::string::npos)return 2;
 runtimeFamilyTransportCase140(value.substr(0,separator),value.substr(separator+1,edge==std::string::npos?std::string::npos:edge-separator-1),edge==std::string::npos?std::string{}:value.substr(edge+1));
 }else if(selectedCase==L"case:classified-fault-cleanup-elite")classifiedFaultCleanupCases140("elite");else if(selectedCase==L"case:classified-fault-cleanup-ps")classifiedFaultCleanupCases140("dualsense-edge");else familyLifecycleCases140(selectedCase==L"case:onex-parameterized");ok=failures.empty();}
 else if(mode==L"worker-cost"){workerMaintenanceCostCases();ok=failures.empty();}
 else if(mode==L"recovery-cost"){recoveryMaintenanceCostCases();ok=failures.empty();}
 else if(mode==L"late-hide-pending"){expectedFailure=!ymcc::msi::latehide::buildEnabled;msiLateHidePendingConsumerCases();ok=failures.empty();}
 else if(mode==L"late-hide-publication"){msiLateHidePublicationConsumerCases();ok=failures.empty();}
 else if(mode==L"late-hide-inflight"){msiLateHideInflightConsumerCases();ok=failures.empty();}
 else if(mode==L"late-hide-inflight-edges"){msiLateHideInflightEdgeCases();ok=failures.empty();}
 else if(mode==L"late-hide-orphaned-inflight"){msiLateHideOrphanedInflightCase();ok=failures.empty();}
 else if(mode==L"off-lifecycle"){
#if defined(YMCC_FIXTURE_SD_OFF_SOURCE_LOST)
 if(fixtureSdOffSourceLostSelector(selectedCase)){
  const auto prefix=std::wstring(L"case:sd-off-source-lost:");
  if(selectedCase==L"case:sd-off-source-lost")fullPipeSdOffSourceLostCases("absent");
  else{const auto variant=W2U(selectedCase.substr(prefix.size()));if(variant.empty())throw std::invalid_argument("empty-sd-off-source-lost-variant");fullPipeSdOffSourceLostCases(variant);}
 }else
#endif
 {coldOffCarryCases();fixtureOffCheckpoint("cold-cases-returned");fullPipeOffLifecycleCases();fixtureOffCheckpoint("original-off-cases-returned");}ok=failures.empty();}
 else if(mode==L"cold-archive"){coldArchiveCases();ok=failures.empty();}
 else if(mode==L"cold-msi-archive"){coldMsiArchiveCases();ok=failures.empty();}
 else if(mode==L"focus-recovery"){focusRecoveryCases();ok=failures.empty();}
 else if(mode==L"input-application"){fullPipeInputApplicationCases();ok=failures.empty();}
 else if(mode==L"full-pipe-core"){if(selectedCase==L"case:gpd-expired-first-on-full-cycle"||selectedCase==L"case:gpd-expired-first-on-full-cycle:native-writer")fullPipeGpdExpiredFirstOnSelectedCases(selectedCase==L"case:gpd-expired-first-on-full-cycle:native-writer");else if(selectedCase==L"case:wake-kept-source")fullPipeWakeKeptSourceSelectedCases();else if(selectedCase.rfind(L"case:wake-kept-source:",0)==0)fullPipeWakeKeptSourceSelectedCases(W2U(selectedCase.substr(std::wstring(L"case:wake-kept-source:").size())));else if(selectedCase==L"case:wake-kept-visibility")fullPipeWakeKeptSelectedCases();else fullPipeCases();ok=failures.empty();}
#ifdef YMCC_PREPARE_REJECTION_COLD_CASE
 else if(mode==L"target-return"&&fixturePrepareRejectionSelector(selectedCase)){fullPipePrepareRejectionColdCase();ok=failures.empty();}
#endif
 else if(mode==L"target-return"){if(selectedCase==L"case:ps-nopin-proof")fullPipePrepareAckLostCase("PC",false,true);else if(selectedCase==L"case:ps-return-all")fullPipePsReturnEvidenceCases();else if(selectedCase==L"case:ps-return-root"||selectedCase==L"case:ps-return-proof")fullPipePsReturnSelected(selectedCase==L"case:ps-return-root");else fixtureTargetReturnFullPipeCases();ok=failures.empty();}
 else if(mode==L"target-return-family"){familyLifecycleCases140();fixtureTargetReturnFullPipeCases();ok=failures.empty();}
 else if(mode==L"full-pipe"){fullPipeCases(true);fixtureTargetReturnFullPipeCases();fullPipeOffLifecycleCase("MSI-return-unproven");ok=failures.empty();}
 else return 2;
 const auto report=json{{"status",ok?"PASS":"FAIL"},{"checks",checks},{"assertionTrace",assertionTrace},{"cases",cases},{"failures",failures},{"expectedFailure",expectedFailure},{"forbiddenIo",forbiddenIo},{"inertXinputReads",inertXinputReads},{"excludedSteamWallpaperNotifications",fixtureExcludedSteamWallpaperNotifications},{"targetReturnObservations",fixtureTargetReturnObservations},{"targetReturnCaptureModel","full-pipe: real107 parent+managed-child; native-only: event Host has explicit controlled PID/birth"},{"fullTranslationUnit",true},{"normalProductMainExecuted",false},{"realDeviceIo",false}};
 std::ofstream(fspath::path(g_yemanccDataDir)/L"fixture-result.json")<<report.dump(2);if(mode==L"full-pipe-core"||mode==L"family-lifecycle"||mode==L"target-return"||mode==L"target-return-family"||mode==L"full-pipe"||mode==L"late-hide-pending"||mode==L"switch-recovery")std::cout<<json{{"status",ok?"PASS":"FAIL"},{"checks",checks},{"failures",failures}}.dump()<<'\n';else std::cout<<report.dump(2)<<'\n';return ok&&forbiddenIo==0?0:1;
 }catch(const std::exception& e){try{const auto report=json{{"status","INCOMPLETE"},{"exception",e.what()},{"checks",checks},{"assertionTrace",assertionTrace},{"cases",cases},{"failures",failures},{"nativeEvents",fixtureEvents},{"state",state()},{"forbiddenIo",forbiddenIo},{"normalProductMainExecuted",false},{"realDeviceIo",false}};std::ofstream(fspath::path(g_yemanccDataDir)/L"fixture-exception.json")<<report.dump(2);}catch(...){}std::cerr<<e.what()<<'\n';return 3;}}
