// Actual native request owner + real transaction callbacks. The SDK and CPU
// receipt producer are controlled boundaries; no physical devices are opened.
#include "sensor_thread_mailbox.h"
#include "windows_motion_processing.h"
static bool inputHostLifecycleBusy(){return false;}
static std::mutex g_sensorThreadMx;
static std::atomic<bool> g_windowsSourceProbeBusy{false};
static void* g_sensorThreadWake=reinterpret_cast<void*>(1);
static bool SetEvent(void*){return true;}
static ymcc::sensors::ThreadMailbox g_sensorThreadMailbox;
static bool inputCaptureSensorStopRequested(){return g_sensorThreadMailbox.stopRequested();}
enum class PowerLifecycle{Ready,Suspended};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::shared_ptr<const ymcc::sensors::processing::ReceiptCache> g_windowsProcessingReceipt;
#include "windows_source_owner_native.inc"
static std::string ownerFault;
static std::shared_ptr<std::atomic<bool>> ownerCpu=std::make_shared<std::atomic<bool>>(true);
static ymcc::windows_motion::Stamp ownerRaw;
static bool observedPendingClosed=false,observedDefaultWhilePrivate=false;
static std::function<void()> ownerPumpHook;
static void ownerProcess(){
 auto raw=inputCaptureWindowsSensorViewSnapshot();
 auto defaultRaw=inputCaptureWindowsDefaultViewSnapshot(false);
 if(raw.privateSource&&defaultRaw.parts[0].usable&&defaultRaw.parts[1].usable)observedDefaultWhilePrivate=true;
 if(!raw.coherent||!raw.parts[0].usable||!raw.parts[1].usable||!ymcc::windows_motion::current(raw.submission))return;
 if(!ymcc::windows_motion::sameInput(ownerRaw,raw.submission)){ownerCpu->store(false);ownerCpu=std::make_shared<std::atomic<bool>>(true);ownerRaw=raw.submission;}
 auto processed=ymcc::windows_motion::forProcessing(raw.submission,ownerCpu);
 if(raw.submission.publication&&!ymcc::windows_motion::currentForOutput(processed))observedPendingClosed=true;
 if(ownerFault=="no-processing"||(ownerFault=="default-no-processing"&&!raw.privateSource))return;
 const auto context=std::atomic_load(&g_windowsSelectionWorkerContext);if(!context)return;
 ymcc::sensors::processing::Identity id;id.source=processed;id.privateSource=raw.privateSource;id.power=currentPower;
 id.epoch=1;id.captureRequest=raw.captureRequestId;id.choiceRequest=raw.choiceRequestId;
 id.recipe={context->deviceClass,context->selector,context->gyro,context->accel,context->digest,true};
 id.metadata={raw.metadata.gyroDeviceId,raw.metadata.accelDeviceId,raw.metadata.gyroFamily,raw.metadata.accelFamily,
     raw.metadata.gyroHcLegacyFields,raw.metadata.accelHcLegacyFields};
 id.profile=raw.privateSource?raw.profile:context->defaultProfile;id.key=raw.privateSource?raw.calibrationKey:context->defaultKey;
 if(ownerFault=="wrong-choice")++id.choiceRequest;if(ownerFault=="wrong-key")id.key="foreign-key";
 if(ownerFault=="wrong-legacy")id.metadata.gyroLegacy=!id.metadata.gyroLegacy;
 if(ownerFault=="wrong-model")id.recipe.selector="foreign-selector";
 if(ownerFault=="wrong-source")id.metadata.gyroId="foreign-source";
 auto cache=std::make_shared<ymcc::sensors::processing::ReceiptCache>(id);
 const auto& gyro=raw.parts[0];const auto& accel=raw.parts[1];
 cache->publish({fakeNow,gyro.sequence,accel.sequence,gyro.receipt,accel.receipt,gyro.timestamp100ns,accel.timestamp100ns,
     ownerFault=="wrong-fence"?std::shared_ptr<const ymcc::windows_motion::PublicationFence>{}:raw.submission.publication});
 std::atomic_store(&g_windowsProcessingReceipt,std::shared_ptr<const ymcc::sensors::processing::ReceiptCache>(cache));
}
static void ownerRefreshContext(){
 auto context=std::make_shared<WindowsSelectionWorkerContext>();context->capture={ymcc::windows_motion::retain(g_inputCaptureSink,7),currentPower};
 context->model=model();context->metadata=g_sensorMeta;context->lifetime=g_windowsSourceModuleLifetime.load();context->revision=1;
 context->deviceClass=model().deviceClass;context->selector="controlled-selector";context->gyro=model().gyroMatrixId;context->accel=model().accelMatrixId;
 context->digest=std::string(64,'c');context->defaultProfile="retained-default-profile";context->defaultKey="retained-default-key";
 std::atomic_store(&g_windowsSelectionWorkerContext,std::shared_ptr<const WindowsSelectionWorkerContext>(context));
}
static void ownerReset(){
 inputCaptureWindowsSelectionLifecycle();g_sensorThreadMailbox.close();g_sensorThreadMailbox.open();resetSdk();resetView();
 std::atomic_store(&g_windowsDefaultSelectedRoute,std::shared_ptr<const ss::ActiveSource>{});
 std::atomic_store(&g_windowsSelectionWorkerContext,std::shared_ptr<const WindowsSelectionWorkerContext>{});
 std::atomic_store(&g_windowsProcessingReceipt,std::shared_ptr<const ymcc::sensors::processing::ReceiptCache>{});
 ownerCpu->store(false);ownerCpu=std::make_shared<std::atomic<bool>>(true);ownerRaw={};ownerFault.clear();ownerPumpHook={};
 observedPendingClosed=observedDefaultWhilePrivate=false;g_powerLifecycle=PowerLifecycle::Ready;inputCaptureWindowsSelectionSetStaOpen(true);ownerRefreshContext();
 onPump=[]{auto& sink=*g_inputCaptureSink;sink.gyroReceiptTick=sink.accelReceiptTick=fakeNow;
   sink.gyroTimestamp100ns=sink.accelTimestamp100ns=fakeNow*100;++sink.gyroSequence;++sink.accelSequence;
   ownerProcess();if(ownerPumpHook)ownerPumpHook();};
}
static json ownerArgs(const std::string& name="B"){return{{"gyroDeviceId",pair(name).gyroId},{"accelDeviceId",pair(name).accelId},
 {"transformPolicy","explicit-current-model"},{"recipeFingerprint",ss::fingerprint(model())}};}
static json ownerSelect(const std::string& name="B"){
 auto admitted=inputCaptureWindowsSourceSelectionRequest(ownerArgs(name));check(admitted.value("accepted",false),"native owner admits explicit pair to asynchronous queue");
 inputCaptureRunWindowsSelection();return inputCaptureWindowsSourceSelectionStatus();
}
static bool ownerOutput(){auto raw=inputCaptureWindowsSensorViewSnapshot();return raw.coherent&&
 ymcc::windows_motion::currentForOutput(ymcc::windows_motion::forProcessing(raw.submission,ownerCpu));}
static void ownerTests(){
 ownerReset();g_windowsSourceProbeBusy=true;check(!inputCaptureWindowsSourceSelectionRequest(ownerArgs()).value("ok",true)&&opened==0,"an active metadata sampling probe cannot overlap native source selection");g_windowsSourceProbeBusy=false;
 ownerReset();const auto beforeOpens=opened;auto invalid=ownerArgs();invalid["transformPolicy"]="automatic";
 check(!inputCaptureWindowsSourceSelectionRequest(invalid).value("ok",true)&&opened==beforeOpens,"policy rejection does not touch native sensors");
 auto queued=inputCaptureWindowsSourceSelectionRequest(ownerArgs());check(queued.value("accepted",false)&&opened==beforeOpens,"request returns before any typed sensor is opened");
 check(!inputCaptureWindowsSourceSelectionRequest(ownerArgs("C")).value("ok",true),"one pending request owns the source writer");
 check(inputCaptureWindowsSourceSelectionCancel({{"requestId",queued["requestId"]}}).value("cancellationRequested",false),"exact queued request can be cancelled");
 inputCaptureRunWindowsSelection();check(opened==beforeOpens&&!inputCaptureWindowsSourceSelectionStatus().value("inputProcessingCommitted",true),"pre-start cancellation never opens or commits a source");
 ownerReset();auto state=ownerSelect();check(state.value("phase",std::string{})=="committed-input-observed"&&state.value("inputProcessingCommitted",false),"actual native callbacks complete source selection after CPU receipt");
 check(observedPendingClosed&&observedDefaultWhilePrivate,"candidate publication remains closed while default source stays readable");
 check(g_windowsSelectionLeaseCount==1&&opened-closed==2,"STA registry retains exactly one active gyro/accel lease");
 onPump();check(ownerOutput(),"new snapshots carry committed publication permission");
 auto privateBefore=inputCaptureWindowsSensorViewSnapshot();const auto priorCpu=ownerCpu;
 state=ownerSelect("C");check(state.value("inputProcessingCommitted",false)&&g_windowsSelectionLeaseCount==1&&opened-closed==2,"second private pair commits and closes inactive native capture");
 check(!ymcc::windows_motion::current(privateBefore.submission)&&!priorCpu->load(),"second selection retires prior choice and CPU proof");
 auto restore=inputCaptureWindowsSourceSelectionRequest(json::object(),true);check(restore.value("accepted",false),"default restoration has explicit queue operation");
 inputCaptureRunWindowsSelection();state=inputCaptureWindowsSourceSelectionStatus();
 check(state.value("inputProcessingCommitted",false)&&!state["selected"].value("privateSource",true),"formal default transaction commits actual default processing proof");
 check(g_windowsSelectionLeaseCount==0&&opened==closed&&handlers.empty(),"default commitment releases all private native handles on STA");
 check(inputCaptureWindowsSensorViewSnapshot().calibrationKey=="retained-default-key","default restoration preserves its calibration owner");
 check(g_inputCaptureSink->activeBindingGeneration==7&&intervalWrites==0&&frameWrites==0,"transactions do not rebind default capture or write native sensor settings");
 auto oldDefault=inputCaptureWindowsSensorViewSnapshot();state=ownerSelect();check(state.value("inputProcessingCommitted",false),"private selection works after managed default restoration");
 auto oldStamp=oldDefault.submission;oldStamp=ymcc::windows_motion::forProcessing(oldStamp,ownerCpu);check(!ymcc::windows_motion::currentForOutput(oldStamp),"returned-default output cannot revive after later selection");
 for(const std::string fault:{"no-processing","wrong-choice","wrong-key","wrong-model","wrong-source","wrong-fence","wrong-legacy"}){
   ownerReset();ownerFault=fault;state=ownerSelect();
   check(!state.value("inputProcessingCommitted",true),"raw reports cannot substitute matching actual processing: "+fault);
   check(!ownerOutput(),"failed processing proof cannot authorize output: "+fault);
 }
 ownerReset();state=ownerSelect();ownerFault="default-no-processing";
 auto returnRequest=inputCaptureWindowsSourceSelectionRequest(json::object(),true);inputCaptureRunWindowsSelection();state=inputCaptureWindowsSourceSelectionStatus();
 check(returnRequest.value("accepted",false)&&state.value("phase",std::string{})=="rolled-back-input-observed"&&state["selected"].value("privateSource",false),"failed default processing rolls back to retained private capture with fresh proof");
 check(g_windowsSelectionLeaseCount==1,"default rollback retains active private native lease");
 ownerReset();state=ownerSelect();auto retiredContext=std::atomic_load(&g_windowsSelectionWorkerContext);retiredContext->active->store(false);ownerRefreshContext();
 inputCaptureRunWindowsSelection();check(!ownerOutput(),"same-fingerprint replacement context retires old native capture and publication");
 auto replacedReturn=inputCaptureWindowsSourceSelectionRequest(json::object(),true);inputCaptureRunWindowsSelection();state=inputCaptureWindowsSourceSelectionStatus();
 check(replacedReturn.value("accepted",false)&&state.value("inputProcessingCommitted",false)&&!state["selected"].value("privateSource",true),"new valid worker context can explicitly restore default under fresh proof");
 ownerReset();failOpenKind=1;state=ownerSelect();check(!state.value("inputProcessingCommitted",true)&&state.value("hresult",std::int64_t{0})==-85&&
   state.value("failurePhase",std::string{})=="accel-from-id","actual async-open HRESULT and operation phase survive preparation cleanup");
 check(state["preparationEvidence"]["parts"][1].value("openHresult",std::int64_t{0})==-85&&
   state["preparationEvidence"]["parts"][1]["distinctReports"].is_null(),"failure export keeps actual native error and unobserved report count");
 check(opened==closed&&handlers.empty(),"failed second native open cleans first native subscription");
 ownerReset();g_sensorMeta.gyroFamily=g_sensorMeta.accelFamily="winrt";g_sensorMeta.gyroHcLegacyFields=g_sensorMeta.accelHcLegacyFields=false;ownerRefreshContext();
 state=ownerSelect();check(state.value("inputProcessingCommitted",false),"existing WinRT default can enter explicit private source route");
 inputCaptureWindowsSourceSelectionRequest(json::object(),true);inputCaptureRunWindowsSelection();state=inputCaptureWindowsSourceSelectionStatus();
 check(state.value("inputProcessingCommitted",false)&&!state["selected"].value("privateSource",true),"existing WinRT default restores under its original transform policy without a fabricated native-frame observation");
 ownerReset();intervalValue=0;state=ownerSelect();check(state.value("inputProcessingCommitted",false)&&subscribed==0,"zero-report-interval native source commits via polling without writes");
 const auto readsBefore=currentReads;fakeNow+=16;inputCaptureRunWindowsSelection();check(currentReads>readsBefore&&g_windowsSelectionLeaseCount==1,"adopted zero-interval source keeps polling after preparation");
 const auto retainedView=std::atomic_load(&g_windowsPrivateConsumerProjection);const auto oldReports=retainedView->source->evidence(fakeNow,retainedView->parent).sources[0].observations.distinct;
 onOpen=[](unsigned kind){if(kind==0)fakeNow+=16;};failOpenKind=1;state=ownerSelect("C");
 check(!state.value("inputProcessingCommitted",true)&&g_windowsSelectionLeaseCount==1&&ownerOutput(),"failed candidate preparation preserves the already committed private output");
 check(retainedView->source->evidence(fakeNow,retainedView->parent).sources[0].observations.distinct>oldReports,"existing zero-interval source keeps receiving polled raw reports during a later native preparation");
 ownerReset();auto cancelRequest=inputCaptureWindowsSourceSelectionRequest(ownerArgs());
 ownerPumpHook=[id=cancelRequest["requestId"]]{inputCaptureWindowsSourceSelectionCancel({{"requestId",id}});};
 inputCaptureRunWindowsSelection();check(!inputCaptureWindowsSourceSelectionStatus().value("inputProcessingCommitted",true)&&opened==closed,"cancellation during preparation closes typed native resources");
 ownerReset();auto cancelAfterAdopt=inputCaptureWindowsSourceSelectionRequest(ownerArgs());bool didCancel=false;
 ownerPumpHook=[&]{if(!didCancel&&std::atomic_load(&g_windowsPrivateConsumerProjection)){didCancel=true;inputCaptureWindowsSourceSelectionCancel({{"requestId",cancelAfterAdopt["requestId"]}});}};
 inputCaptureRunWindowsSelection();state=inputCaptureWindowsSourceSelectionStatus();
 check(didCancel&&!state.value("inputProcessingCommitted",true)&&g_windowsSelectionLeaseCount==0&&opened==closed,"cancellation after tentative adoption retires native candidate without an idle polling leak");
 ownerPumpHook={};auto afterCancelReturn=inputCaptureWindowsSourceSelectionRequest(json::object(),true);inputCaptureRunWindowsSelection();state=inputCaptureWindowsSourceSelectionStatus();
 check(afterCancelReturn.value("accepted",false)&&state.value("inputProcessingCommitted",false)&&!state["selected"].value("privateSource",true),"explicit default restoration recovers a cancelled retired private route with fresh CPU proof");
 ownerReset();ownerPumpHook=[]{g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Suspended);};state=ownerSelect();
 check(!state.value("inputProcessingCommitted",true),"pending suspend cancels actual native source transaction");inputCaptureWindowsSelectionLifecycle();
 check(opened==closed&&handlers.empty()&&g_windowsSelectionLeaseCount==0,"lifecycle drains all STA resources and callbacks");
 check(!inputCaptureWindowsSourceSelectionRequest(ownerArgs()).value("ok",true),"lifecycle and closed model context reject late admission");
 ownerReset();state=ownerSelect();const auto beforeLifecycle=inputCaptureWindowsSensorViewSnapshot();onPump();
 inputCaptureWindowsSelectionSetStaOpen(false);inputCaptureWindowsSelectionLifecycle();
 check(!ymcc::windows_motion::current(beforeLifecycle.submission)&&opened==closed&&handlers.empty(),"shutdown retires source tokens before private native resources are released");
 check(!inputCaptureWindowsSourceSelectionRequest(ownerArgs()).value("ok",true),"closed admission rejects requests after apartment cleanup");
 std::atomic_store(&g_windowsSelectionWorkerContext,std::shared_ptr<const WindowsSelectionWorkerContext>{});
 std::atomic_store(&g_windowsDefaultSelectedRoute,std::shared_ptr<const ss::ActiveSource>{});g_sensorThreadMailbox.close();
 onPump={};ownerPumpHook={};resetView();
}
