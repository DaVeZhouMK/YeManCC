// Complete actual msiDinputPump and canonical source helpers execute here.
// Only explicitly listed external OS/output/discovery/log boundaries are inert.
#define YMCC_MSI_FOREGROUND_ISOLATION_TEST 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <functional>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <stdexcept>
#include "json.hpp"
static unsigned unexpectedOpen=0,unexpectedMode=0,usbQueries=0,feedbackStops=0;
static std::vector<nlohmann::json> fixtureEvents;
// Snapshot-only scheduling seam; the production pump's executable statements are preserved.
static std::function<void()> fixtureBeforePumpLock;
static std::atomic<unsigned> fixturePumpBodyEntries{0};
static bool allowLifecycleOpenProbe=false;
static unsigned lifecycleOpenProbes=0;
static bool allowTimeoutIntent=false;static unsigned timeoutIntents=0;
#include "main.inert.cpp"

// Output-selftest only. A COM-shaped, in-memory DirectInput device lets the
// actual production pump handle INPUTLOST without enumerating/touching hardware.
class PumpDevice final : public IDirectInputDevice8W {
public:
    HRESULT acquireResult=DI_OK,stateResult=DI_OK,pollResult=DI_NOEFFECT;
    unsigned acquires=0,reads=0,unacquires=0,releases=0,polls=0;std::function<void()> pollHook,stateHook;DIJOYSTATE2 state{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID* value) override {if(value)*value=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return 1;}
    ULONG STDMETHODCALLTYPE Release() override {++releases;return 1;}
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,LPDIPROPHEADER) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID,LPCDIPROPHEADER) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Acquire() override {++acquires;return acquireResult;}
    HRESULT STDMETHODCALLTYPE Unacquire() override {++unacquires;return DI_OK;}
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD bytes, LPVOID value) override {++reads;if(stateHook)stateHook();if(FAILED(stateResult))return stateResult;if(bytes!=sizeof(state)||!value)return E_INVALIDARG;memcpy(value,&state,sizeof(state));return DI_OK;}
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW,DWORD,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID,LPCDIEFFECT,LPDIRECTINPUTEFFECT *,LPUNKNOWN) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW,REFGUID) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Poll() override {++polls;if(pollHook)pollHook();return pollResult;}
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD,LPCDIDEVICEOBJECTDATA,LPDWORD,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR,LPDIENUMEFFECTSINFILECALLBACK,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR,DWORD,LPDIFILEEFFECT,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW) override {return E_NOTIMPL;}
};

// Memory-only DirectInput owner: Release is the real disable tail's synchronization point.
class PumpDirectInput final : public IDirectInput8W {
public:
    unsigned releases=0;std::function<void()> releaseHook;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,LPVOID* p) override {if(p)*p=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return 1;}
    ULONG STDMETHODCALLTYPE Release() override {++releases;if(releaseHook)releaseHook();return 1;}
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID,LPDIRECTINPUTDEVICE8W*,LPUNKNOWN) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD,LPDIENUMDEVICESCALLBACKW,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID,LPCWSTR,LPGUID) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCWSTR,LPDIACTIONFORMATW,LPDIENUMDEVICESBYSEMANTICSCBW,LPVOID,DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,LPDICONFIGUREDEVICESPARAMSW,DWORD,LPVOID) override {return E_NOTIMPL;}
};
static PumpDirectInput directInput;
static unsigned assertions=0;static std::vector<std::string> cases,failures;static json details=json::array();
static void require(bool good,const char* message){++assertions;if(!good)failures.push_back(cases.back()+": "+message);}
static PumpDevice device;
static void seed(){
 g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;g_msiDinput.pad=nullptr;g_msiDinput.di=nullptr;
 fixtureBeforePumpLock={};fixturePumpBodyEntries=0;allowLifecycleOpenProbe=false;directInput.releases=0;directInput.releaseHook={};
 // Clear the exact pump-owned state; no copy assignment of atomic/mutex state.
 allowTimeoutIntent=false;timeoutIntents=0;
 g_msiDinput.inputAdmission.reset();
 g_msiDinput.inputLostRecovery.reset();g_msiDinput.openTried=false;g_msiDinput.lastFailAt=0;g_msiDinput.exhausted=false;
 g_msiDinput.routeWindowSeen=false;g_msiDinput.routeStartedAt=0;g_msiDinput.routeResent=g_msiDinput.routeReverted=false;
 g_msiDinput.notAcquiredRetries=0;g_msiDinput.notAcquiredNextAt=0;g_msiDinput.notAcquiredLogged=false;
 g_msiDinput.readableLogged=false;g_msiDinput.corruptLogged=false;g_msiDinput.axisUnverifiedLogged=false;
 g_msiDinput.backkeyFirstSampleLogged=g_msiDinput.backkeySourceReleaseLogged=g_msiDinput.backkeyCapChangedLogged=false;
 g_msiDinput.m1Armed=g_msiDinput.m2Armed=false;g_msiDinput.actionGeneration=0;
 g_msiDinput.enabled=true;g_msiDinput.productId=kMsiClawPidDInput;g_msiDinput.mainInterfaceMatched=true;g_msiDinput.bindCount=2;
 g_msiDinput.interfacePath="offline-msi-game-interface";g_msiDinput.baseContainer="offline-container";
 g_msiDinput.axisRangeVerdict=MsiAxisRangeVerdict::VerifiedDeclared;g_msiDinput.axisRangeNormalize=false;
 g_msiDinput.dwButtons=17;g_msiDinput.hasBtn15=g_msiDinput.hasBtn16=true;
 g_msiDinputRouteDinputArmed=true;g_msiDinputRouteConfirmed=g_msiDinputSourceValid=false;msiDinputInvalidateCanonicalFrame();
 g_msiDinput.pad=&device;device.acquireResult=device.stateResult=DI_OK;device.pollResult=DI_NOEFFECT;device.polls=device.reads=device.acquires=device.releases=device.unacquires=0;device.pollHook={};device.stateHook={};
 device.state={};device.state.lX=device.state.lY=device.state.lZ=device.state.lRz=32768;device.state.lRx=device.state.lRy=0;device.state.rgdwPOV[0]=0xffffffffu;
 g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawCG3EM";g_machineIdentity.resolved=g_machineIdentity.attempted=true;
 g_powerGeneration=7;g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;
 g_inputHostRequestedPersonaMatchesBound=true;
 g_inputHostEpoch=11;g_inputHostEpochMirror=11;g_inputHostVirtualIntentRevision=1;g_inputHostVirtualIntentWriters=0;
 g_inputHostProcess=reinterpret_cast<HANDLE>(uintptr_t(1));g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;g_inputHostFaultHoldsStart=false;g_inputHostExitFence=false;g_exitRequested=false;
 g_xinputTopologyEpoch=10;g_xinputSlotIdentityEpoch=20;g_xinputPrimarySlot=0;g_msiFeedbackModeGeneration=3;
 g_msiPersonaReadPause=false;g_msiPersonaModeHandoff={};g_msiOwnedReturnPending=false;g_msiTerminalReturnPending=false;
 g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
 g_msiSampleGate.closing=false;g_msiSampleGate.activeReads=g_msiSampleGate.quarantinedReads=0;++g_msiSampleGate.gen;
 g_msiForegroundTestIo.locations=[](const auto&){return std::vector<std::wstring>{L"OFFLINE-PORT"};};
 g_msiForegroundTestIo.ui=[](){return false;};g_msiForegroundTestIo.nativeCandidates=[](){return std::vector<HidHidePhysicalIdentity>{};};
 g_msiForegroundTestIo.queue=[](const auto&){throw std::logic_error("Unexpected physical foreground queue");};
 g_msiForegroundIsolation=MsiForegroundIsolationState{};g_msiForegroundUiOwned=false;g_msiForegroundUiEdge=0;
 {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);g_msiMainSource=HidHideMsiMainSourceRecord{};g_msiFeedbackSourceRecord.reset();g_msiFeedbackSourceGeneration=0;}
 const auto revisionBeforeBind=g_msiMainSourceRevision.load();
 msiDinputPublishMainSourceIdentity(g_msiDinput.interfacePath,g_msiDinput.baseContainer,kMsiClawVendorId,kMsiClawPidDInput,g_msiDinput.bindCount);
 {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);if(!g_msiMainSource.valid||g_msiMainSource.generation!=2||g_msiMainSource.usbInstance!="USB\\VID_0DB0&PID_1902\\OFFLINE-PUMP"||g_msiMainSourceRevision.load()!=revisionBeforeBind+1)throw std::logic_error("Fixture binding was not independently established");}
 fixtureEvents.clear();feedbackStops=0;g_inputCaptureGamepadSnapshot={};
}
static InputCaptureGamepadSnapshot consumerCopy(){
@@READ_BEGIN@@
@@READ_LOCALS@@
 unsigned admittedMask=0;bool externalPriority=false;
@@FALLBACK@@
@@READ_SEQUENCE@@
@@READ_ASSIGNMENT@@
 return g_inputCaptureGamepadSnapshot;
}
static bool issue(const InputCaptureGamepadSnapshot& sample,bool neutral=false){
 InputFrameSubmission f{};
 f.ticket={g_inputHostEpoch,currentPowerGeneration(),g_inputHostVirtualIntentRevision.load(),neutral};
 f.sample=inputHostFrameSubmissionSample(&sample);f.physicalSourceAdmitted=true;
 const auto ok=inputHostFrameSubmissionMayIssue(&f);details.push_back({{"case",cases.back()},{"allowed",ok},{"reason",ymcc::input::frameSubmission::name(f.reason)},{"readStart",sample.readStartedTick},{"readEnd",sample.readCompletedTick},{"samplePower",sample.readPowerGeneration},{"sampleMode",sample.readMsiModeGeneration},{"sourceRevision",sample.readMsiSourceRevision},{"currentSourceRevision",g_msiMainSourceRevision.load()}});return ok;
}
static std::pair<bool,MsiCanonicalReadProof> proof(){XINPUT_GAMEPAD pad{};MsiCanonicalReadProof p;const bool valid=msiDinputCanonicalSnapshot(pad
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 ,nullptr
#endif
 ,&p);return {valid,p};}
template<class F>static void run(const char* name,F fn){cases.push_back(name);try{seed();fn();}catch(const std::exception& e){failures.push_back(std::string(name)+": "+e.what());}g_inputHostProcess=nullptr;}
// All cross-thread ordering uses promises. The short timeout is only a failure bound.
static void delayedPumpAcrossDisable(bool duringDiRelease){
 using namespace std::chrono_literals;
 g_msiDinputRouteDinputArmed=false;g_msiDinputReturnOwed=false;
 g_msiDinput.di=&directInput;allowLifecycleOpenProbe=true;
 const auto openBefore=lifecycleOpenProbes;
 std::promise<void> passedCheck,resumePump,diEntered,resumeDisable;
 auto passed=passedCheck.get_future();auto pumpResume=resumePump.get_future().share();
 auto diReady=diEntered.get_future();auto disableResume=resumeDisable.get_future().share();
 fixtureBeforePumpLock=[&]{passedCheck.set_value();pumpResume.wait();};
 std::exception_ptr pumpError,disableError;
 std::thread pump([&]{try{msiDinputPump();}catch(...){pumpError=std::current_exception();}});
 const bool pumpPaused=passed.wait_for(2s)==std::future_status::ready;
 require(pumpPaused,"controlled pump has passed the actual enabled=true fast check");
 bool tailUnlocked=false,tailReached=false;
 std::thread disable;
 if(duringDiRelease){
   directInput.releaseHook=[&]{diEntered.set_value();disableResume.wait();};
   disable=std::thread([&]{try{msiDinputSetEnabled(false);}catch(...){disableError=std::current_exception();}});
   tailReached=diReady.wait_for(2s)==std::future_status::ready;
   require(tailReached,"actual disable has released pad and entered DI Release before clearOwnedState");
   if(tailReached){std::unique_lock<std::mutex> probe(g_msiPersonaPumpMx,std::try_to_lock);tailUnlocked=probe.owns_lock();}
   require(!tailUnlocked,"disable owns the pump mutex through DI Release and remaining state cleanup");
 }else msiDinputSetEnabled(false);
 resumePump.set_value();pump.join();
 const auto bodyEntries=fixturePumpBodyEntries.load();const auto openAttempts=lifecycleOpenProbes-openBefore;
 if(duringDiRelease){resumeDisable.set_value();disable.join();}
 require(!pumpError&&!disableError,"actual lifecycle calls complete without an exception");
 require(bodyEntries==0,"delayed pump never enters mutable state after disable has set enabled=false");
 require(openAttempts==0,"delayed pump never attempts DirectInput reopen after disable");
 require(!g_msiDinput.enabled.load()&&!g_msiDinput.pad&&!g_msiDinput.di,"disable leaves enabled=false and both owned pointers null");
 require(device.releases==1&&directInput.releases==1,"each fake owned COM object is released exactly once");
 require(!proof().first&&!g_msiDinputSourceValid.load()&&!g_msiDinput.mainInterfaceMatched.load(),"disable leaves canonical sample and source identity revoked");
 require(g_msiDinput.openTried==false,"delayed pump cannot overwrite disable's final retry state");
 details.push_back({{"case",cases.back()},{"pumpPassedEnabledBeforeDisable",pumpPaused},{"disableTailReached",tailReached},{"pumpMutexAvailableDuringDisableTail",tailUnlocked},{"mutablePumpBodyEntries",bodyEntries},{"openBoundaryCallsAfterDisable",openAttempts},{"hardwareAccess",false}});
 fixtureBeforePumpLock={};directInput.releaseHook={};allowLifecycleOpenProbe=false;
}
// GetTickCount64 can have ~15.6ms clock granularity. Ensure distinct completed ticks.
static void readNeutral(unsigned count=3){for(unsigned i=0;i<count;++i){Sleep(17);msiDinputPump();}}
static void half(){device.state.lRx=device.state.lRy=32768;}
static void release(){device.state.lRx=device.state.lRy=0;}
int wmain(){
 run("new-binding-half-pulse-is-not-consumable",[]{half();msiDinputPump();require(!proof().first&&!g_msiDinputSourceValid&&!g_msiDinputRouteConfirmed,"ambiguous startup half cannot publish or confirm route");release();readNeutral(2);require(!proof().first,"two neutral reads do not establish source");readNeutral(1);require(proof().first&&issue(consumerCopy()),"third neutral read establishes fresh source");half();Sleep(17);msiDinputPump();const auto view=consumerCopy();require(view.live&&view.pad.bLeftTrigger==127&&view.pad.bRightTrigger==127&&issue(view),"real half press after admission is unmodified");});
 run("continuous-held-input-does-not-recreate-or-open",[]{half();for(unsigned i=0;i<200;++i)msiDinputPump();require(!proof().first&&device.reads==200&&device.releases==0&&device.acquires==0,"pending does not release or reacquire binding");release();readNeutral();require(issue(consumerCopy()),"release recovers without new target");});
 run("inputlost-reacquire-needs-release-before-recovery",[]{readNeutral();device.stateResult=DIERR_INPUTLOST;msiDinputPump();require(!proof().first,"lost immediately revokes source");device.stateResult=DI_OK;g_msiDinput.inputLostRecovery.nextDue=0;half();Sleep(17);msiDinputPump();require(!proof().first&&g_msiDinput.inputLostRecovery.active,"Acquire and held sample alone do not restore source");release();for(unsigned i=0;i<3;++i){g_msiDinput.inputLostRecovery.nextDue=0;Sleep(17);msiDinputPump();}require(proof().first&&!g_msiDinput.inputLostRecovery.active&&issue(consumerCopy()),"same-binding recovery completes after neutral reads");});
 run("power-mode-binding-each-require-new-release",[]{readNeutral();const auto old=consumerCopy();++g_powerGeneration;half();Sleep(17);msiDinputPump();require(!proof().first&&!issue(old),"power edge revokes old proof and blocks held input");release();readNeutral();require(issue(consumerCopy()),"post-power release admits source");++g_msiFeedbackModeGeneration;half();Sleep(17);msiDinputPump();require(proof().first&&issue(consumerCopy()),"software mode marker alone does not reset an established binding");release();readNeutral();require(issue(consumerCopy()),"mode release recovers");++g_msiDinput.bindCount;half();Sleep(17);msiDinputPump();require(!proof().first,"new binding cannot inherit readiness");});
 run("corrupt-and-range-invalid-reads-do-not-count-as-neutral",[]{release();readNeutral(2);device.state.lRx=device.state.lRy=device.state.lRz=32767;msiDinputPump();require(!proof().first,"corrupt frame remains invalid");device.state.lRz=32768;release();readNeutral(2);require(!proof().first,"corrupt sample resets pending count");readNeutral(1);require(issue(consumerCopy()),"valid source recovers");g_msiDinput.axisRangeVerdict=MsiAxisRangeVerdict::UnverifiedDegraded;msiDinputPump();require(!proof().first,"axis unverified revokes source");g_msiDinput.axisRangeVerdict=MsiAxisRangeVerdict::VerifiedDeclared;readNeutral(2);require(!proof().first,"unverified interval cannot preserve admission");readNeutral(1);require(issue(consumerCopy()),"verified range and release recovers");});
 run("rear-and-stick-held-require-release",[]{device.state.rgbButtons[15]=0x80;readNeutral(4);require(!proof().first,"rear source cannot sneak through canonical zero buttons");device.state.rgbButtons[15]=0;device.state.lX=60000;readNeutral(4);require(!proof().first,"off-center stick is not neutral");device.state.lX=32768;readNeutral();require(issue(consumerCopy()),"all controls released admits source");});
 run("pending-still-allows-lifecycle-stop",[]{half();msiDinputPump();g_msiDinputRouteDinputArmed=false;g_msiDinputReturnOwed=false;msiDinputStopForLifecycle("test-stop",true);require(!g_msiDinput.pad&&!proof().first&&!g_msiDinputSourceValid&&device.releases==1,"stop releases pending input with no wait for user's release");require(!g_msiDinput.inputAdmission.ready(),"stop resets readiness");});
 run("pending-does-not-extend-route-deadline",[]{half();msiDinputPump();allowTimeoutIntent=true;g_msiDinput.routeStartedAt=GetTickCount64()-kMsiDinputRouteTotalWindowMs-1;msiDinputPump();require(g_msiDinput.routeReverted&&!g_msiDinputRouteDinputArmed&&!proof().first&&timeoutIntents==1,"existing bounded route timeout still ends failed acquisition");});
 require(unexpectedOpen==0&&unexpectedMode==0,"no OS enumeration, mode interface or mode writer invoked");
 g_msiDinput.pad=nullptr;g_msiDinput.enabled=false;g_inputHostProcess=nullptr;g_msiForegroundTestIo={};
 const json result={{"lifecycleOpenBoundaryProbes",lifecycleOpenProbes},{"schedulingHooksInSnapshotOnly",true},{"cases",cases},{"assertions",assertions},{"failures",failures},{"details",details},{"completeActualPumpExecuted",true},{"wholeBusExecuted",false},{"usbQueryBoundaryCalls",usbQueries},{"unexpectedOpen",unexpectedOpen},{"unexpectedMode",unexpectedMode},{"realHardwareAccess",false},{"hostExecuted",false},{"systemSleepExecuted",false},{"diagnosticBuild",
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 true
#else
 false
#endif
 }};std::cout<<result.dump(2)<<std::endl;return failures.empty()?0:1;
}
