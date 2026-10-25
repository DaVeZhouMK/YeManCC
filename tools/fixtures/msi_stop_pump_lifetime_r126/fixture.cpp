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
int wmain(){
 run("complete-pump-normal-zero-read-is-current",[]{const auto begin=GetTickCount64();msiDinputPump();const auto end=GetTickCount64();const auto p=proof();require(device.polls==1&&device.reads==1,"one actual Poll and GetDeviceState");require(p.first&&p.second.startedTick>=begin&&p.second.completedTick<=end&&p.second.completedTick>=p.second.startedTick,"actual call is enclosed by producer window");const auto s=consumerCopy();require(s.live&&s.pad.wButtons==0&&issue(s),"valid all-zero read reaches final admission");});
 run("complete-pump-same-payload-is-new-real-read",[]{msiDinputPump();const auto a=proof();msiDinputPump();const auto b=proof();require(device.reads==2&&b.second.sequence>a.second.sequence,"identical payload still advances real publish ordinal");require(issue(consumerCopy()),"unchanged fresh input remains admissible");});
 run("complete-pump-inputlost-first-valid-recovery",[]{msiDinputPump();const auto old=proof();device.stateResult=DIERR_INPUTLOST;msiDinputPump();require(g_msiDinput.pad==&device&&g_msiDinput.inputLostRecovery.active&&!g_msiDinputSourceValid&&!proof().first,"loss keeps same object and revokes old payload");const auto revoked=g_msiMainSourceRevision.load();device.stateResult=DI_OK;msiDinputPump();const auto fresh=proof();require(device.acquires==1&&fresh.first&&!g_msiDinput.inputLostRecovery.active,"Acquire plus first valid state recovers");require(fresh.second.sourceRevision>revoked&&fresh.second.sourceRevision==g_msiMainSourceRevision.load(),"proof uses actual republished identity revision");require(fresh.second.sequence>old.second.sequence&&issue(consumerCopy()),"first valid recovered read is not self-retired");});
 run("complete-pump-acquire-alone-does-not-reopen-source",[]{device.stateResult=DIERR_INPUTLOST;msiDinputPump();device.stateResult=DIERR_NOTACQUIRED;msiDinputPump();require(device.acquires==1&&!proof().first&&!g_msiDinputSourceValid,"successful Acquire alone does not establish state");require(!issue(consumerCopy()),"no canonical sample means no live publication");device.stateResult=DI_OK;g_msiDinput.inputLostRecovery.nextDue=0;msiDinputPump();require(proof().first&&issue(consumerCopy()),"later actual valid state resumes");});
 run("complete-pump-recovery-throttle-preserves-revocation",[]{device.stateResult=DIERR_INPUTLOST;msiDinputPump();g_msiDinput.inputLostRecovery.nextDue=GetTickCount64()+5000;const auto reads=device.reads,acquires=device.acquires;for(unsigned i=0;i<50;++i)msiDinputPump();require(device.reads==reads&&device.acquires==acquires,"early recovery calls do not poll the OS or reacquire");require(!proof().first&&!issue(consumerCopy()),"no retained old proof during throttle");});
 run("complete-pump-recovery-exhaustion-releases-bound-object",[]{device.stateResult=DIERR_INPUTLOST;msiDinputPump();device.acquireResult=DIERR_INPUTLOST;for(unsigned i=0;i<ymcc::msi::AcquireRecovery::budget;++i){g_msiDinput.inputLostRecovery.nextDue=0;msiDinputPump();}require(g_msiDinput.inputLostRecovery.exhausted(),"finite existing acquire budget reached");msiDinputPump();require(!g_msiDinput.pad&&device.releases==1&&!proof().first&&!issue(consumerCopy()),"exhaustion releases fake object and keeps source revoked");});
 run("complete-pump-poll-inputlost-never-calls-getstate",[]{device.pollResult=DIERR_INPUTLOST;msiDinputPump();require(device.polls==1&&device.reads==0,"failed Poll does not issue state API");require(!proof().first&&!issue(consumerCopy()),"failed Poll cannot publish old payload");});
 run("complete-pump-generic-state-failure-clears-source",[]{msiDinputPump();device.stateResult=E_FAIL;msiDinputPump();require(!g_msiDinput.pad&&device.releases==1&&!proof().first&&!issue(consumerCopy()),"generic state failure clears and releases");});
 run("complete-pump-invalid-axis-range-clears-source",[]{msiDinputPump();g_msiDinput.axisRangeVerdict=MsiAxisRangeVerdict::UnverifiedDegraded;msiDinputPump();require(!proof().first&&!g_msiDinputSourceValid&&!issue(consumerCopy()),"API success cannot bypass axis verification");});
 run("complete-pump-corrupt-frame-clears-source",[]{msiDinputPump();device.state.lRx=device.state.lRy=device.state.lRz=32767;msiDinputPump();require(!proof().first&&!g_msiDinputSourceValid&&!issue(consumerCopy()),"corrupt raw state never gains producer proof");});
 for(const auto what:{0,1,2})run((std::string("complete-pump-generation-changes-in-read-")+std::to_string(what)).c_str(),[=]{device.stateHook=[=]{if(what==0)++g_powerGeneration;else if(what==1)++g_xinputTopologyEpoch;else ++g_msiFeedbackModeGeneration;};msiDinputPump();require(!proof().first&&!g_msiDinputSourceValid&&!issue(consumerCopy()),"power/topology/mode edge in API read revokes publication");});
 run("complete-pump-late-mode-edge-rechecked-at-issue",[]{msiDinputPump();const auto s=consumerCopy();require(issue(s),"initial current sample is admissible");++g_msiFeedbackModeGeneration;require(!issue(s),"saved copy cannot cross later mode change");require(issue(s,true),"same-target full neutral remains admissible");msiDinputPump();require(issue(consumerCopy()),"new actual mode read resumes without target replacement");});
 run("complete-pump-long-poll-window-is-not-restamped",[]{device.pollHook=[](){Sleep(1600);};msiDinputPump();const auto p=proof();require(p.first&&p.second.completedTick-p.second.startedTick>1500,"actual Poll wait is inside producer window");require(!issue(consumerCopy()),"wrapper cannot hide a long real API interval");device.pollHook={};msiDinputPump();require(issue(consumerCopy()),"quick subsequent actual read restores admission");});
 run("lifecycle-stop-keeps-lock-through-clear",[]{
   std::promise<void> entered,allow;auto enteredFuture=entered.get_future();auto allowFuture=allow.get_future().share();
   device.stateHook=[&]{entered.set_value();allowFuture.wait();};
   std::thread pumpThread([]{msiDinputPump();});enteredFuture.wait();
   std::promise<void> stopStarted,stopDone;auto startedFuture=stopStarted.get_future();auto doneFuture=stopDone.get_future();
   std::thread stopThread([&]{stopStarted.set_value();msiDinputStopForLifecycle("test-stop",true);stopDone.set_value();});startedFuture.wait();
   Sleep(25);require(device.releases==0,"complete stop remains blocked during active API read");
   allow.set_value();pumpThread.join();doneFuture.wait();stopThread.join();
   require(device.releases==1&&g_msiDinput.pad==nullptr&&!g_msiDinputSourceValid.load(),"stop clears source while still serialized");
 });
 run("lifecycle-release-waits-for-active-pump",[]{
   std::promise<void> entered,allow;auto enteredFuture=entered.get_future();auto allowFuture=allow.get_future().share();
   device.stateHook=[&]{entered.set_value();allowFuture.wait();};
   std::thread pumpThread([]{msiDinputPump();});enteredFuture.wait();
   std::promise<void> releaseStarted,releaseDone;auto startedFuture=releaseStarted.get_future();auto doneFuture=releaseDone.get_future();
   std::thread releaseThread([&]{releaseStarted.set_value();msiDinputReleasePadForLifecycle("test-stop");releaseDone.set_value();});startedFuture.wait();
   Sleep(25);require(device.releases==0,"lifecycle release is blocked while actual pump owns the MSI lock");
   allow.set_value();pumpThread.join();doneFuture.wait();releaseThread.join();
   require(device.releases==1&&g_msiDinput.pad==nullptr,"release completes after Poll/GetDeviceState and clears the object");
 });
 run("lifecycle-disable-waits-for-active-pump",[]{
   std::promise<void> entered,allow;auto enteredFuture=entered.get_future();auto allowFuture=allow.get_future().share();
   device.stateHook=[&]{entered.set_value();allowFuture.wait();};
   std::thread pumpThread([]{msiDinputPump();});enteredFuture.wait();
   std::promise<void> started;auto startedFuture=started.get_future();
   std::thread disableThread([&]{started.set_value();msiDinputSetEnabled(false);});startedFuture.wait();
   Sleep(25);require(device.releases==0,"actual disable does not release an in-flight read");
   allow.set_value();pumpThread.join();disableThread.join();
   require(device.releases==1&&!g_msiDinput.pad&&!g_msiDinput.enabled.load(),"actual disable completes only after the read retires");
 });
 run("lifecycle-delayed-pump-after-disable-completes",[]{delayedPumpAcrossDisable(false);});
 run("lifecycle-delayed-pump-during-disable-di-release",[]{delayedPumpAcrossDisable(true);});
 run("lifecycle-disable-reuses-preheld-persona-lock",[]{
   g_msiDinputRouteDinputArmed=false;g_msiDinputReturnOwed=false;g_msiDinput.di=&directInput;
   g_msiPersonaPumpHold=std::unique_lock<std::mutex>(g_msiPersonaPumpMx);
   bool tailHeld=false;directInput.releaseHook=[&]{tailHeld=g_msiPersonaPumpHold.owns_lock();};
   msiDinputSetEnabled(false);
   require(tailHeld&&g_msiPersonaPumpHold.owns_lock(),"disable reuses the lifecycle owner's existing persona hold without recursive locking or unlocking it");
   g_msiPersonaPumpHold.unlock();require(device.releases==1&&directInput.releases==1&&!g_msiDinput.pad&&!g_msiDinput.di,"preheld path releases both objects once");
 });
 run("lifecycle-disable-repeat-and-explicit-enable",[]{
   g_msiDinputRouteDinputArmed=false;g_msiDinputReturnOwed=false;g_msiDinput.di=&directInput;
   msiDinputSetEnabled(false);msiDinputSetEnabled(false);
   require(device.releases==1&&directInput.releases==1,"repeated disable is idempotent for owned objects");
   g_msiDinput.exhausted=true;g_msiDinput.episodeOpens=9;g_msiDinput.m1Armed=g_msiDinput.m2Armed=true;
   msiDinputSetEnabled(true);
   require(g_msiDinput.enabled.load()&&!g_msiDinput.exhausted&&g_msiDinput.episodeOpens==0&&!g_msiDinput.m1Armed&&!g_msiDinput.m2Armed,"explicit reenable preserves episode reset and release-before-arm semantics");
   require(!g_msiSampleGate.closing,"explicit reenable reopens the existing sample permit gate");
 });
 run("complete-pump-persona-mutex-busy-does-not-resign",[]{msiDinputPump();const auto before=proof();const auto polls=device.polls;std::promise<void> ready,release;auto done=release.get_future();std::thread holder([&]{std::lock_guard<std::mutex> lock(g_msiPersonaPumpMx);ready.set_value();done.wait();});ready.get_future().wait();msiDinputPump();release.set_value();holder.join();const auto after=proof();require(device.polls==polls&&before.second.sequence==after.second.sequence&&before.second.startedTick==after.second.startedTick,"busy pump returns without OS read or renewed provenance");});
 run("complete-pump-resume-copy-retains-sleep-generation",[]{msiDinputPump();const auto old=proof();++g_powerGeneration;const auto stale=consumerCopy();require(stale.readPowerGeneration==old.second.power&&!issue(stale),"new wrapper cannot relabel pre-sleep proof");msiDinputPump();require(issue(consumerCopy()),"actual post-resume read is admitted");});
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
