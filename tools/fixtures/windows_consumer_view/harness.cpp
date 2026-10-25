// @@BASE@@
#include "windows_source_view.h"
#include "dsu_frame.h"
using UINT32=std::uint32_t;using UINT64=std::uint64_t;using ULONGLONG=std::uint64_t;
// @@METADATA_DECL@@
// @@LEGACY_DECL@@
static std::mutex g_sensorMetaMx;
static InputCaptureSensorMeta g_sensorMeta;
static thread_local const InputCaptureSensorMeta* t_windowsProcessingMeta=nullptr;
static thread_local const ymcc::windows_motion::Stamp* t_windowsProcessingStamp=nullptr;
using WindowsSensorView=ymcc::sensors::view::Snapshot<InputCaptureSensorMeta>;
static thread_local const WindowsSensorView* t_windowsProcessingView=nullptr;
struct InputCaptureSensorSink {
 std::mutex mx;std::atomic<std::uint64_t> activeBindingGeneration{7};
 bool gyroOk=true,accelOk=true;double gx=1,gy=2,gz=3,ax=4,ay=5,az=6;
 std::uint64_t gyroBindingGeneration=7,accelBindingGeneration=7,gyroTimestamp100ns=100000,accelTimestamp100ns=100000;
 std::uint64_t gyroReceiptTick=1000,accelReceiptTick=1000,gyroSequence=5,accelSequence=6,gyroRejectedSamples=0,accelRejectedSamples=0;
 double gyroTimeOfDayMs=10,accelTimeOfDayMs=10;
};
static std::shared_ptr<InputCaptureSensorSink> g_inputCaptureSink;
static std::uint64_t currentPower=3;
static std::uint64_t currentPowerGeneration(){return currentPower;}
static bool fixtureModelBound=true;
static json inputCaptureCandidateMatrixReceipt(const InputCaptureSensorMeta&){const auto value=model();return{{"gyro",{{"id",value.gyroMatrixId},{"sourceSha256",value.gyroSha256}}},{"accel",{{"id",value.accelMatrixId},{"sourceSha256",value.accelSha256}}}};}
static json inputCaptureMotionCapabilityReceipt(const InputCaptureSensorMeta&,bool){return{{"deviceClass",model().deviceClass},{"sourceRoute","windows-sensors"},{"modelSelectorBound",fixtureModelBound}};}
static double hcSinkTimeOfDayMs(std::uint64_t timestamp)noexcept{return double(timestamp/10000);}
// @@EVIDENCE@@
// @@VIEW@@
// @@META_GETTER@@
// @@LEGACY_GETTER@@
static void ymccResolveMachineIdentityOnce(){}static void inputCaptureResolveHcMotionSelector(){}
// Controlled processor boundary for this raw-view test; real-DLL ownership has a separate suite.
static std::shared_ptr<std::atomic<bool>> fixtureProcessingLease=std::make_shared<std::atomic<bool>>(true);
static bool inputCaptureLegionControllerMotionRoute(){return false;}
static bool inputCaptureWindowsProcessingAdopt(WindowsSensorView& view){
 if(view.privateSource&&!view.processingOwnerAccepted)return false;
 view.submission=ymcc::windows_motion::forProcessing(view.submission,fixtureProcessingLease);
 return ymcc::windows_motion::current(view.submission);
}
static void inputCaptureWindowsProcessingRetire(){fixtureProcessingLease->store(false);}

struct TickCosts{std::uint64_t metaUs=0;};static TickCosts g_tickPreHostSub;static constexpr int kTickCostPreHostMeta=0;
struct TickPreHostSubScope{TickPreHostSubScope(int,std::uint64_t*){}};
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1};
static std::atomic<std::uint64_t> g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
static bool inputHostPhysicalPublicationSourceAdmitted(bool live,bool){return live;}
static bool legionMotionSubmissionCurrent(const ymcc::legion_motion::Sample&,std::uint64_t){return false;}
struct InputFrameSubmission{ymcc::input::frameSubmission::Ticket ticket;std::optional<ymcc::windows_motion::Stamp> windowsMotion;bool rejected=false;ymcc::input::frameSubmission::Verdict reason=ymcc::input::frameSubmission::Verdict::Allowed;};
// @@HOST_GATE@@
// @@DSU_GATE@@
struct ReadView{std::array<double,6> raw{};bool gyro=false,accel=false;InputCaptureSensorMeta meta;InputCaptureLegacySensorFields fields;ymcc::windows_motion::Stamp stamp,calibration,reviewed,dsu,host;std::uint64_t gyroSequence=0,accelSequence=0;};
static ReadView readConsumer(const std::function<void()>& between={}){
 double gx=0,gy=0,gz=0,ax=0,ay=0,az=0;bool gyro=false,accel=false;
 std::uint64_t gyroTimestamp100ns=0,accelTimestamp100ns=0,gyroReceiptTick=0,accelReceiptTick=0,gyroSequence=0,accelSequence=0,gyroRejectedSamples=0,accelRejectedSamples=0,gyroBindingGeneration=0,accelBindingGeneration=0;
 double gyroTimeOfDayMs=0,accelTimeOfDayMs=0;
// @@CONSUMER_PREFIX@@
 if(between)between();
// @@CONSUMER_META@@
 ReadView result;result.raw={gx,gy,gz,ax,ay,az};result.gyro=gyro;result.accel=accel;result.meta=sensorMeta;result.fields=inputCaptureLegacySensorFieldsSnapshot();result.stamp=windowsSensorView.submission;result.gyroSequence=gyroSequence;result.accelSequence=accelSequence;
// @@CALIBRATION_STAMP@@
 result.calibration=calibrationSensorFrame;
 const bool reviewed=true,hcMatrixAdmitted=true,calibrationPairProven=false;
// @@REVIEWED_STAMP@@
 result.reviewed=reviewedFrame;
 ymcc::dsu::Frame dsuFrame;
// @@DSU_STAMP@@
 result.dsu=dsuFrame.source.windows;
 const bool controllerMotionRoute=false,gyroSubmitAllowed=true;
// @@HOST_STAMP@@
 result.host=windowsMotionSource;
 return result;
}
static void resetView(){
#if YMCC_FIXTURE_SELECTION_OWNER
 std::atomic_store(&g_windowsSelectionWorkerContext,std::shared_ptr<const WindowsSelectionWorkerContext>{});
 std::atomic_store(&g_windowsDefaultSelectedRoute,std::shared_ptr<const ss::ActiveSource>{});
#endif
#if YMCC_FIXTURE_PUBLICATION_FENCE
 std::atomic_store(&g_windowsDefaultPublicationFence,std::shared_ptr<const ymcc::windows_motion::PublicationFence>{});
#endif
fixtureProcessingLease=std::make_shared<std::atomic<bool>>(true);g_inputCaptureSink=std::make_shared<InputCaptureSensorSink>();g_sensorMeta={};g_sensorMeta.gyroDeviceId="HID#VID_8087&PID_0AC2#1#{GYRO}";g_sensorMeta.accelDeviceId="HID#VID_8087&PID_0AC2#1#{ACCEL}";g_sensorMeta.gyroFamily=g_sensorMeta.accelFamily="sensorapi";g_sensorMeta.gyroHcLegacyFields=g_sensorMeta.accelHcLegacyFields=true;g_sensorMeta.gyroPresent=g_sensorMeta.accelPresent=true;g_windowsChoiceCounter->activeBindingGeneration=20;g_windowsSourceModuleLifetime=1;std::atomic_store(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>{});currentPower=3;fixtureModelBound=true;t_windowsProcessingMeta=nullptr;t_windowsProcessingStamp=nullptr;g_xinputTopologyEpoch=1;g_xinputSlotIdentityEpoch=1;}
static ymcc::dsu::Frame dsuFor(const ymcc::windows_motion::Stamp& stamp){ymcc::dsu::Frame value;value.live=value.motionReady=true;value.source.power=3;value.source.windows=stamp;auto& physical=value.source.physical;physical.window={1,fakeNow,fakeNow,3,1,true};physical.identity=1;physical.live=physical.routeKnown=true;value.validate=inputCaptureDsuFrameCurrent;value.raw={1,2,3,4,5,6};value.transformed=value.raw;return value;}
static bool hostFor(const ymcc::windows_motion::Stamp& stamp,bool neutral=false){InputFrameSubmission frame;frame.ticket.neutral=neutral;frame.windowsMotion=stamp;return inputHostWindowsMotionSubmissionCurrent(frame);}
static void attachPrivate(Backend& backend,bool accepted,bool committed=true){
#if YMCC_FIXTURE_SELECTION_OWNER
 inputCaptureWindowsSelectionPublishContext();auto context=std::atomic_load(&g_windowsSelectionWorkerContext);
 if(!context){auto created=std::make_shared<WindowsSelectionWorkerContext>();created->capture=backend.parent().scope;created->lifetime=backend.parent().lifetime;created->model=model();context=created;
  std::atomic_store(&g_windowsSelectionWorkerContext,context);}
#endif
 auto value=std::make_shared<WindowsPrivateConsumerProjection>();
#if YMCC_FIXTURE_SELECTION_OWNER
 value->context=context;
#endif
 value->source=backend.retained;value->selected=Backend::current(&backend);value->parent=backend.parent();value->processingOwnerAccepted=accepted;
#if YMCC_FIXTURE_PUBLICATION_FENCE
 const auto frame=value->source->frame(value->selected,value->parent,fakeNow);if(!frame||!frame->captureScope)throw std::runtime_error("fixture private frame unavailable");
 auto stamp=ymcc::windows_motion::guarded(frame->captureScope->binding,frame->scope.binding,value->selected.captureLease);
 if(committed)stamp=ymcc::windows_motion::forProcessing(stamp,fixtureProcessingLease);
 value->publication=committed?ymcc::windows_motion::PublicationFence::committed(stamp):ymcc::windows_motion::PublicationFence::pending(stamp);
#endif
 std::atomic_store(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>(std::move(value)));
}
int main(int argc,char** argv){try{
 resetSdk();resetView();auto first=readConsumer();check(first.gyro&&first.accel&&first.raw==std::array<double,6>{1,2,3,4,5,6},"actual default processor prefix consumes exact coherent raw fields");check(first.meta.gyroFamily=="sensorapi"&&first.fields.gyro&&first.fields.accel,"actual metadata and MSI field helpers read pinned source");for(const auto& stamp:{first.calibration,first.reviewed,first.dsu,first.host})check(ymcc::windows_motion::same(stamp,first.stamp),"actual four consumer assignments share same composite stamp");check(hostFor(first.host)&&ymcc::dsu::admit(dsuFor(first.dsu),fakeNow).motion,"actual Host/DSU gates accept current source");++g_windowsChoiceCounter->activeBindingGeneration;check(!hostFor(first.host)&&!ymcc::dsu::admit(dsuFor(first.dsu),fakeNow).motion,"default frames before first choice retire while default capture remains alive");check(g_inputCaptureSink->activeBindingGeneration==7,"choice cutover never resets default capture counter");
 resetView();auto pinned=readConsumer([]{std::lock_guard<std::mutex> lock(g_sensorMetaMx);g_sensorMeta.gyroDeviceId="changed";g_sensorMeta.gyroFamily="winrt";g_sensorMeta.gyroHcLegacyFields=false;});check(pinned.meta.gyroDeviceId!="changed"&&pinned.fields.gyro,"metadata and legacy layout cannot change midway through actual processor prefix");check(!t_windowsProcessingMeta&&!t_windowsProcessingStamp,"processing scope restores thread-local view");
 resetView();auto outer=inputCaptureWindowsSensorViewSnapshot();{ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> scope(outer,t_windowsProcessingMeta,t_windowsProcessingStamp);try{auto inner=outer;inner.metadata.gyroDeviceId="inner";ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> nested(inner,t_windowsProcessingMeta,t_windowsProcessingStamp);check(inputCaptureSensorMetaSnapshot().gyroDeviceId=="inner","nested scope pins its own metadata");throw std::runtime_error("unwind");}catch(...){check(inputCaptureSensorMetaSnapshot().gyroDeviceId==outer.metadata.gyroDeviceId,"exception restores outer processing metadata");}}check(!t_windowsProcessingMeta&&!t_windowsProcessingStamp,"exception leaves no stale processing pointer");
 resetView();{auto outer=inputCaptureWindowsSensorViewSnapshot();outer.profile="profile-a";outer.calibrationKey="key-a";
 {ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> scope(outer,t_windowsProcessingMeta,t_windowsProcessingStamp,&t_windowsProcessingView);check(t_windowsProcessingView==&outer&&t_windowsProcessingView->calibrationKey=="key-a","actual processing scope pins the complete profile and key");
 try{auto inner=outer;inner.profile="profile-b";inner.calibrationKey="key-b";ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> nested(inner,t_windowsProcessingMeta,t_windowsProcessingStamp,&t_windowsProcessingView);check(t_windowsProcessingView==&inner&&t_windowsProcessingView->profile=="profile-b","nested scope pins its own complete profile");throw std::runtime_error("unwind-profile");}catch(...){check(t_windowsProcessingView==&outer&&t_windowsProcessingView->calibrationKey=="key-a","exception restores the complete outer profile");}}
 check(!t_windowsProcessingView&&!t_windowsProcessingMeta&&!t_windowsProcessingStamp,"complete processing scope leaves no stale thread-local source");}
 resetView();auto cpuFrame=readConsumer();auto rawCapture=cpuFrame.stamp;rawCapture.processingLease.reset();
 check(bool(cpuFrame.stamp.processingLease)&&hostFor(cpuFrame.host)&&ymcc::dsu::admit(dsuFor(cpuFrame.dsu),fakeNow).motion,"controlled CPU owner reaches actual Host and DSU gates");
 fixtureProcessingLease->store(false);
 check(ymcc::windows_motion::current(rawCapture),"retiring processor keeps the native capture and choice alive");
 check(!hostFor(cpuFrame.host)&&!ymcc::dsu::admit(dsuFor(cpuFrame.dsu),fakeNow).motion,"retired CPU lease rejects already-built Host and DSU frames");
 check(!ymcc::windows_motion::current(cpuFrame.calibration)&&!ymcc::windows_motion::current(cpuFrame.reviewed),"calibration and reviewed stamps share the CPU revocation");
 auto replacement=std::make_shared<std::atomic<bool>>(true);auto redecorated=ymcc::windows_motion::forProcessing(cpuFrame.stamp,replacement);
 check(!ymcc::windows_motion::current(redecorated),"an expired CPU decoration cannot be replaced to revive the frame");
 resetView();auto beforeStop=readConsumer();g_inputCaptureSink->activeBindingGeneration=0;check(!hostFor(beforeStop.host)&&!ymcc::dsu::admit(dsuFor(beforeStop.dsu),fakeNow).motion,"default physical capture retirement reaches actual final gates");check(hostFor(beforeStop.host,true),"neutral source release remains permitted by the actual source gate");auto neutral=dsuFor(beforeStop.dsu);neutral.semanticNeutral=true;auto release=ymcc::dsu::admit(neutral,fakeNow);check(release.pad&&!release.motion,"DSU neutral cannot revive motion");
 resetView();auto malformed=readConsumer().stamp;malformed.choiceBinding.reset();check(!ymcc::windows_motion::current(malformed),"missing choice pointer cannot retain nonzero choice generation");auto guardedAgain=ymcc::windows_motion::guarded(readConsumer().stamp,ymcc::windows_motion::retain(g_windowsChoiceCounter,20));check(!ymcc::windows_motion::current(guardedAgain),"existing guard cannot be overwritten to revive a stamp");
 resetSdk();resetView();{Backend backend;backend.choice=g_windowsChoiceCounter;backend.active.scope.binding=ymcc::windows_motion::retain(backend.choice,20);backend.defaultFrame();const auto original=readConsumer();ss::SelectionTransaction txn;check(txn.apply(request(1),backend.ops()).outcome==ss::Outcome::Committed,"actual typed private preparation supplies selected consumer cache");attachPrivate(backend,false);auto pending=readConsumer();check(!pending.gyro&&!pending.accel,"private data is not processed before processor ownership acceptance");attachPrivate(backend,true);auto selected=readConsumer();check(selected.gyro&&selected.accel&&selected.raw==std::array<double,6>{1,2,3,104,105,106},"actual private cache frame reaches processor prefix");check(selected.meta.gyroFamily=="winrt"&&!selected.fields.gyro&&!selected.fields.accel,"private WinRT source never borrows default MSI field layout");const auto fresh=backend.retained->evidence(fakeNow,backend.parent());check(selected.gyroSequence==fresh.sources[0].observations.distinct&&selected.accelSequence==fresh.sources[1].observations.distinct,"raw values and sequence metadata come from one cache frame");check(!hostFor(original.host)&&hostFor(selected.host),"first private choice retires original output while preserving original capture");check(ymcc::dsu::admit(dsuFor(selected.dsu),fakeNow).motion,"actual DSU gate admits current private snapshot");backend.leases.at(backend.active.resource)->close();check(!hostFor(selected.host)&&!ymcc::dsu::admit(dsuFor(selected.dsu),fakeNow).motion,"native close revokes already-built private final submissions");std::atomic_store(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>{});backend.cleanup();}
 resetSdk();resetView();{Backend backend;backend.choice=g_windowsChoiceCounter;backend.active.scope.binding=ymcc::windows_motion::retain(backend.choice,20);backend.defaultFrame();ss::SelectionTransaction txn;
 check(txn.apply(request(1),backend.ops()).outcome==ss::Outcome::Committed,"private capture supplies actual candidate publication test");attachPrivate(backend,true,false);auto candidate=readConsumer();
 check(candidate.gyro&&candidate.accel&&ymcc::windows_motion::current(candidate.stamp),"candidate input and processing remain current before output commitment");
 check(!hostFor(candidate.host)&&!ymcc::dsu::admit(dsuFor(candidate.dsu),fakeNow).motion,"uncommitted candidate cannot pass actual Host or DSU final gates");
#if YMCC_FIXTURE_PUBLICATION_FENCE
 check(hostFor(candidate.host,true),"neutral release remains available while candidate publication is withheld");
 InputFrameSubmission pendingSubmission;pendingSubmission.windowsMotion=candidate.host;
 check(!inputHostWindowsMotionSubmissionCurrent(pendingSubmission)&&pendingSubmission.reason==ymcc::input::frameSubmission::Verdict::SourcePublicationPending&&std::string(ymcc::input::frameSubmission::name(pendingSubmission.reason))=="windows-source-publication-not-committed","uncommitted output has a distinct diagnostic reason from a retired input");
 const auto padOnly=ymcc::dsu::admit(dsuFor(candidate.dsu),fakeNow);check(padOnly.pad&&!padOnly.motion,"pending motion keeps physical DSU pad admission independent");

 auto held=std::atomic_load(&g_windowsPrivateConsumerProjection);const auto pendingFence=held->publication;
 attachPrivate(backend,true,true);auto committed=readConsumer();
 check(hostFor(committed.host)&&ymcc::dsu::admit(dsuFor(committed.dsu),fakeNow).motion,"fresh committed snapshot passes actual final gates");
 check(!hostFor(candidate.host)&&!ymcc::dsu::admit(dsuFor(candidate.dsu),fakeNow).motion,"committing fresh snapshot never reopens an older pending frame");
 check(ymcc::windows_motion::sameInput(candidate.stamp,committed.stamp)&&ymcc::windows_motion::same(candidate.stamp,committed.stamp),"publication change preserves actual input and calibration provenance");
 auto promoted=ymcc::windows_motion::forPublication(candidate.stamp,std::atomic_load(&g_windowsPrivateConsumerProjection)->publication);
 check(!ymcc::windows_motion::current(promoted),"already-built candidate stamp cannot be redecorated for publication");
 auto changedCpu=committed.stamp;changedCpu.processingLease=std::make_shared<std::atomic<bool>>(true);
 check(!hostFor(changedCpu)&&!ymcc::dsu::admit(dsuFor(changedCpu),fakeNow).motion,"commit grant cannot be borrowed by another live CPU processing lease");
 const auto granted=std::atomic_load(&g_windowsPrivateConsumerProjection)->publication;granted->revoke();
 check(ymcc::windows_motion::current(committed.stamp)&&!hostFor(committed.host)&&!ymcc::dsu::admit(dsuFor(committed.dsu),fakeNow).motion,"publication revocation keeps sampling and CPU alive but rejects retained output");
 auto noPermission=committed.stamp;noPermission.publication.reset();check(!hostFor(noPermission),"private native lease fails output closed when publication fence is omitted");
 auto other=committed.stamp;other.captureLease=std::make_shared<std::atomic<bool>>(true);
 check(!ymcc::windows_motion::currentForOutput(other),"permission cannot be borrowed by a different native capture lease");
 check(!ymcc::windows_motion::PublicationFence::committed(ymcc::windows_motion::guarded(held->selected.frame->captureScope->binding,held->selected.scope.binding,held->selected.captureLease)),"unprocessed source cannot mint a committed publication fence");
#endif
 std::atomic_store(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>{});backend.cleanup();}
#if YMCC_FIXTURE_PUBLICATION_FENCE
 resetView();{const auto raw=inputCaptureWindowsSensorViewSnapshot();const auto withheld=ymcc::windows_motion::PublicationFence::pending(raw.submission);std::atomic_store(&g_windowsDefaultPublicationFence,withheld);const auto pendingDefault=readConsumer();
 check(pendingDefault.gyro&&pendingDefault.fields.gyro&&ymcc::windows_motion::current(pendingDefault.stamp)&&!hostFor(pendingDefault.host),"default return can process original MSI SensorAPI data while its publication is withheld");
 const auto restored=ymcc::windows_motion::PublicationFence::committed(pendingDefault.stamp);std::atomic_store(&g_windowsDefaultPublicationFence,restored);const auto resumed=readConsumer();
 check(hostFor(resumed.host)&&!hostFor(pendingDefault.host),"default commitment admits only fresh committed snapshots");
 restored->revoke();check(!hostFor(resumed.host)&&ymcc::windows_motion::current(resumed.stamp),"default publication revoke preserves original capture and processor");
 std::atomic_store(&g_windowsDefaultPublicationFence,std::shared_ptr<const ymcc::windows_motion::PublicationFence>{});
 check(hostFor(readConsumer().host)&&!hostFor(resumed.host),"ending explicit control restores the original default policy without reviving its revoked frames");}
#endif
 resetSdk();resetView();{Backend backend;backend.choice=g_windowsChoiceCounter;backend.active.scope.binding=ymcc::windows_motion::retain(backend.choice,20);backend.defaultFrame();ss::SelectionTransaction txn;check(txn.apply(request(1),backend.ops()).outcome==ss::Outcome::Committed,"private source for model-binding guard");attachPrivate(backend,true);fixtureModelBound=false;auto missing=readConsumer();check(!missing.gyro&&!missing.accel,"unbound model cannot enter private processor snapshot");std::atomic_store(&g_windowsPrivateConsumerProjection,std::shared_ptr<const WindowsPrivateConsumerProjection>{});backend.cleanup();}
 }catch(const std::exception& error){failures.push_back(std::string("fixture exception: ")+error.what());}
 json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"actualSnapshotGettersProcessorPrefixFourStampsAndFinalGatesExecuted",true},{"sdkModelAndProcessorAcceptanceControlled",true},{"wholeBusExecuted",false},{"realGamepadMotionExecuted",false},{"hardwareIo",false},{"newProductDlls",json::array()}};if(argc>1)std::ofstream(argv[1])<<report.dump(2);std::cout<<report.dump()<<std::endl;return failures.empty()?0:1;}
