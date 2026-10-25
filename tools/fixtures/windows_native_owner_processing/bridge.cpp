// The typed SDK is controlled; source owner, source snapshots, BUS motion
// branches, calibration persistence and ProcessMotion/receipt code are live.
namespace candidate {
using winrt=void; // @@SDK_ALIAS@@
using MSG=fake_sdk::MSG;
using fake_sdk::PeekMessageW;using fake_sdk::TranslateMessage;using fake_sdk::DispatchMessageW;
using fake_sdk::U2W;using fake_sdk::W2U;using fake_sdk::MsgWaitForMultipleObjectsEx;
using fake_sdk::ymcc::sensors::getStaResults;
static bool g_inputCaptureReady=true;
static bool fixtureHostBusy=false;
static bool inputHostLifecycleBusy(){return fixtureHostBusy;}
enum class PowerLifecycle{Ready,Suspended};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::mutex g_sensorThreadMx;static HANDLE g_sensorThreadWake=reinterpret_cast<HANDLE>(1);
static BOOL SetEvent(HANDLE){return TRUE;}
static ymcc::sensors::ThreadMailbox g_sensorThreadMailbox;
static std::atomic<bool> g_windowsSourceProbeBusy{false};
static bool inputCaptureSensorStopRequested(){return g_sensorThreadMailbox.stopRequested();}
static bool inputCaptureLegionControllerMotionRoute(){return false;}
static void ymccResolveMachineIdentityOnce(){} // Machine identity is seeded from fixed HC aliases.
static double hcSinkTimeOfDayMs(std::uint64_t ts){return double(ts/10000);}
// @@NATIVE_TRANSPORT@@
// @@NATIVE_SOURCE_VIEW@@
// @@ACTUAL_CONTEXT_PUBLISHER@@
#include "windows_source_owner_native.inc"
struct TickCosts{std::uint64_t metaUs=0;};static TickCosts g_tickPreHostSub;static constexpr int kTickCostPreHostMeta=0;
struct TickPreHostSubScope{TickPreHostSubScope(int,std::uint64_t*){}};
struct NativeBusEvidence{WindowsSensorView view;bool processed=false,output=false;std::uint64_t calls=0;};
static bool fixturePhysicalLive=true;
static NativeBusEvidence nativeBusStep(){
 const auto now=GetTickCount64();
 double gx=0,gy=0,gz=0,ax=0,ay=0,az=0;bool gyro=false,accel=false;
 std::uint64_t gyroTimestamp100ns=0,accelTimestamp100ns=0,gyroReceiptTick=0,accelReceiptTick=0,gyroSequence=0,accelSequence=0,gyroRejectedSamples=0,accelRejectedSamples=0,gyroBindingGeneration=0,accelBindingGeneration=0;
 double gyroTimeOfDayMs=0,accelTimeOfDayMs=0;
 // @@BUS_SOURCE_PREFIX@@
 // @@BUS_METADATA@@
 const bool sameProvider=!gyroProvider.empty()&&gyroProvider==accelProvider;
 const bool sameBindingGeneration=gyroBindingGeneration!=0&&gyroBindingGeneration==accelBindingGeneration;
 const auto activeBindingGeneration=windowsSensorView.capture.activeBinding?windowsSensorView.capture.activeBinding->load():0;
 const bool currentBindingGeneration=sameBindingGeneration&&gyroBindingGeneration==activeBindingGeneration;
 const bool selectorBound=inputCaptureMotionSelectorBound(),controllerMotionRoute=false,controllerMotionPair=false,wakeGyroHold=false;
 const bool live=fixturePhysicalLive;
 const std::optional<MockControllerMotionObservation> controllerMotion;
 double mgx=gx,mgy=gy,mgz=gz,maxa=ax,may=ay,maz=az;
 double hcCandidateGx=gx,hcCandidateGy=gy,hcCandidateGz=gz,hcCandidateAx=ax,hcCandidateAy=ay,hcCandidateAz=az;
 inputCaptureApplyHcMatrixCandidate(gx,gy,gz,ax,ay,az,hcCandidateGx,hcCandidateGy,hcCandidateGz,hcCandidateAx,hcCandidateAy,hcCandidateAz);
 std::string matrixIdentity;const float motionDeltaSeconds=.008f;
 // @@BUS_ADMISSION_AND_PROCESS@@
 // @@BUS_CALIBRATION_TICK@@
 inputCaptureWindowsProcessingNoteFrame(windowsSensorView,g_gmProcessedThisTick);
 const bool motionProcessorReady=g_gmProcessedThisTick&&windowsProcessorOwnerReady&&inputCaptureWindowsProcessingFrameCurrent(windowsSensorView);
 // @@BUS_OUTPUT_GATES@@
 return {windowsSensorView,g_gmProcessedThisTick,gyroSubmitAllowed,fixtureProcessCount};
}
static NativeBusEvidence lastNativeBus;
static unsigned nativeBusTicks=0,pendingFrames=0,committedFrames=0;
static bool nativeProcessWitness=true;
static std::function<void()> beforeNativeBus,afterNativeBus;
static void nativeBusPump(){
 if(beforeNativeBus)beforeNativeBus();
 auto& sink=mockSink;sink.gyroReceiptTick=sink.accelReceiptTick=fixtureTick;
 sink.gyroTimestamp100ns=sink.accelTimestamp100ns=fixtureTick*100;
 ++sink.gyroSequence;++sink.accelSequence;
 sink.gx=fake_sdk::rawAxes[0];sink.gy=fake_sdk::rawAxes[1];sink.gz=fake_sdk::rawAxes[2];
 sink.ax=fake_sdk::rawAxes[3];sink.ay=fake_sdk::rawAxes[4];sink.az=fake_sdk::rawAxes[5];
 const auto calls=fixtureProcessCount;
 // @@ACTUAL_BUS_DISPATCH_CONDITION@@
 {lastNativeBus=nativeBusStep();}else{lastNativeBus.processed=false;lastNativeBus.output=false;}
 ++nativeBusTicks;
 if(lastNativeBus.processed&&lastNativeBus.calls<=calls)nativeProcessWitness=false;
 if(lastNativeBus.processed&&ymcc::windows_motion::current(lastNativeBus.view.submission)&&lastNativeBus.view.submission.publication&&!ymcc::windows_motion::currentForOutput(lastNativeBus.view.submission))++pendingFrames;
 if(lastNativeBus.output)++committedFrames;
 if(afterNativeBus)afterNativeBus();
}
static json selectionArgs(const std::string& suffix){
 const auto context=std::atomic_load(&g_windowsSelectionWorkerContext);
 return {{"gyroDeviceId","ACPI#"+suffix+"#1#{GYRO}"},{"accelDeviceId","ACPI#"+suffix+"#1#{ACCEL}"},
     {"transformPolicy","explicit-current-model"},{"recipeFingerprint",ymcc::sensors::selection::fingerprint(context->model)}};
}
static json selectNative(const std::string& suffix){const auto admitted=inputCaptureWindowsSourceSelectionRequest(selectionArgs(suffix));
 if(!admitted.value("accepted",false))return admitted;inputCaptureRunWindowsSelection();return inputCaptureWindowsSourceSelectionStatus();}
static json returnNativeDefault(){const auto admitted=inputCaptureWindowsSourceSelectionRequest(json::object(),true);
 if(!admitted.value("accepted",false))return admitted;inputCaptureRunWindowsSelection();return inputCaptureWindowsSourceSelectionStatus();}
static void closeNative(){inputCaptureWindowsSelectionSetStaOpen(false);inputCaptureWindowsSelectionLifecycle();g_sensorThreadMailbox.close();fake_sdk::onPump={};beforeNativeBus={};afterNativeBus={};}
}
