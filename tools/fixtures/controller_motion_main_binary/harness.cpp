#include "harness_base.inc"
struct CarrierProbe { bool issue=false,request=false,scoped=false,controller=false,windows=false;json response;InputFrameSubmission frame; };
static CarrierProbe carrierProbe(bool physicalSourceAdmitted,bool semanticNeutral,const std::function<void(unsigned)>& interleave={}){
#include "submit_carrier_prefix.generated.inl"
    CarrierProbe result;result.scoped=t_inputFrameSubmission==&frameSubmission;result.controller=frameSubmission.controllerMotion==t_legionMotionSubmission;result.windows=frameSubmission.windowsMotion==t_windowsMotionSubmission;
    if(interleave)interleave(0);result.issue=inputHostFrameSubmissionMayIssue(&frameSubmission);
    if(interleave)interleave(1);result.request=inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&result.response,"private-main-binary-probe");result.frame=frameSubmission;return result;
}
static InputCaptureGamepadSnapshot commonSnapshot(){InputCaptureGamepadSnapshot in;in.live=true;in.connectedMask=1;in.inputSource="xinput";in.readAttemptSequence=1;in.readPowerGeneration=frameCurrent.power;in.readTopologyGeneration=frameCurrent.topology;in.readIdentityGeneration=frameCurrent.identity;in.readStartedTick=in.readCompletedTick=clockMs;in.readContextStable=true;return in;}
static std::vector<BYTE> rawBytes(const std::string& hex){CHECK(hex.size()%2==0);std::vector<BYTE> out;for(std::size_t i=0;i<hex.size();i+=2)out.push_back(static_cast<BYTE>(std::stoul(hex.substr(i,2),nullptr,16)));return out;}
static unsigned serialCase=0;
static void setupModel(const json& model,unsigned side,bool automatic=false){
    reset(++serialCase);t_commonFrameSource=nullptr;t_windowsMotionSubmission=nullptr;g_inputHostVirtualIntentRevision.store(frameCurrent.intent);
    g_machineIdentity.deviceClass=model.at("name").get<std::string>();auto selected=physical("USB\\MAIN-BINARY-PRIVATE");selected.vendorId=model.at("vendorId").get<USHORT>();selected.productId=model.at("productIds").at(0).get<USHORT>();selectPhysical(selected);selectSensor(side);g_realStickTest.autoCalibrate=automatic;
    json root={{"WINDOWS-UNRELATED",record(900,901,902,10)}};root["controllerMotionCalibration"][identity()]["0"]=record(0.25f,-0.5f,0.125f,8);root["controllerMotionCalibration"][identity()]["1"]=record(-0.75f,0.5f,-0.25f,8);writeCalibration(root);
}
static MotionSample decodeAtCurrentQueueTime(bool withZeroPrefix=true){
    const bool tablet=g_machineIdentity.deviceClass.find("Tablet")!=std::string::npos;const auto& oracle=familyTopology.at("rawHelperOracles").at(tablet?"Tablet":"GoS");auto raw=rawBytes(oracle.at("logicalHex").get<std::string>());if(withZeroPrefix)raw.insert(raw.begin(),0);const auto actual=raw.size();raw.resize(actual+16,0x5A);
    const auto side=legionMotionSelectedSensorIndex();const auto decoded=legionMotionDecodeReport(g_hidHidePhysicalIdentity,raw.data(),raw.size(),actual,side);CHECK(decoded.has_value());
    queuedMessageTime32=static_cast<std::uint32_t>(clockMs);const auto received=legionMotionMessageReceiptTick();CHECK(received.has_value()&&*received==clockMs);
    auto in=sample(received.value_or(0));if(decoded){in.gyro={decoded->gx,decoded->gy,decoded->gz};in.accel={decoded->ax,decoded->ay,decoded->az};const float values[]{decoded->gx,decoded->gy,decoded->gz,decoded->ax,decoded->ay,decoded->az};const auto expected=oracle.at("expectedNumerators").at(side);for(unsigned i=0;i<6;++i)CHECK(near(values[i],expected.at(i).get<double>()/32767.0));}return in;
}
static std::optional<cm::MotionResult> actualConsume(const MotionSample& in){CHECK(publish(in));const auto snap=legionMotionSnapshot(clockMs);CHECK(snap.has_value());if(!snap)return {};return inputCaptureLegionMotion(snap,axes(*snap),static_cast<double>(clockMs)/1000.0);}
static void* actualHandle(){CHECK(g_legionMotionSessionKey.has_value());return g_legionMotionSessionKey?sessionHandleFor(*g_legionMotionSessionKey):nullptr;}
static std::array<float,3> actualOffset(void* h){std::array<float,3> result{};bridge_get_offset(h,&result[0],&result[1],&result[2]);for(float f:result)CHECK(std::isfinite(f));return result;}
static void finiteResult(const std::optional<cm::MotionResult>& result){CHECK(result.has_value());if(!result)return;CHECK(std::isfinite(result->calibrated.x)&&std::isfinite(result->calibrated.y)&&std::isfinite(result->calibrated.z));CHECK(std::isfinite(result->gravity.x)&&std::isfinite(result->gravity.y)&&std::isfinite(result->gravity.z));CHECK(result->orientationFinite);}
static void nextFresh(){++clockMs;CHECK(clockMs>g_legionMotionEpochStartedAt.load());}
static void sameTickRejected(){CHECK(clockMs==g_legionMotionEpochStartedAt.load());const auto input=decodeAtCurrentQueueTime();CHECK(!publish(input));CHECK(!legionMotionSnapshot(clockMs));CHECK(!legionMotionSubmissionCurrent(input,clockMs));}
static void familyPipeline(){
 for(const auto& model:familyTopology.at("models"))for(unsigned side=0;side<(model.at("rightAllowed").get<bool>()?2u:1u);++side){
    setupModel(model,side);sameTickRejected();nextFresh();const auto input=decodeAtCurrentQueueTime();const auto result=actualConsume(input);finiteResult(result);void* h=actualHandle();CHECK(h&&bridge_mode_read(h)==0);const auto offset=actualOffset(h);const auto raw=axes(input);if(result){CHECK(near(result->calibrated.x,raw.gx-offset[0]));CHECK(near(result->calibrated.y,raw.gy-offset[1]));CHECK(near(result->calibrated.z,raw.gz-offset[2]));}
    auto common=commonSnapshot();{CommonFrameSourceScope cs(&common);LegionMotionSubmissionScope ls(&input);const auto probe=carrierProbe(true,false);CHECK(probe.scoped&&probe.controller&&probe.windows&&probe.issue&&probe.request);CHECK(probe.frame.sample.window.readAttemptSequence==common.readAttemptSequence);}
    CHECK(!t_commonFrameSource&&!t_legionMotionSubmission&&!t_inputFrameSubmission);effects.push_back({{"event","family-route"},{"model",model.at("name")},{"side",side},{"actualMode",bridge_mode_read(h)},{"timestampTransfer","fixture optional->Sample after real clock wrapper"}});
    const auto creates=createCalls;const auto pointer=h;nextFresh();g_realStickTest.motionInput="player-space";auto second=actualConsume(decodeAtCurrentQueueTime(false));finiteResult(second);CHECK(second&&second->player&&!second->world);CHECK(createCalls==creates&&actualHandle()==pointer);nextFresh();g_realStickTest.motionInput="world-space";auto third=actualConsume(decodeAtCurrentQueueTime());finiteResult(third);CHECK(third&&!third->player&&third->world);
 }
}
static void staleContextAndNewSession(){
 for(unsigned cause=0;cause<5;++cause){setupModel(familyTopology.at("models").at(0),0);nextFresh();const auto old=decodeAtCurrentQueueTime();finiteResult(actualConsume(old));inputCaptureLegionMotionSessionRevoke();CHECK(liveHandles()==0);
    if(cause==0){++g_powerGeneration;frameCurrent.power=currentPowerGeneration();legionMotionRevoke();}
    if(cause==1){++g_hidResolveCacheGeneration;legionMotionRevoke();}
    if(cause==2)legionMotionRevoke();
    if(cause==3)selectSensor(1);
    if(cause==4){auto next=g_hidHidePhysicalIdentity;next.baseContainerDeviceInstanceId="USB\\NEW-IDENTITY";selectPhysical(next);json root;root["controllerMotionCalibration"][identity()]["0"]=record(0.25f,-0.5f,0.125f,8);writeCalibration(root);}
    const auto creates=createCalls,processes=processCalls;CHECK(!publish(old));CHECK(!inputCaptureLegionMotion(old,axes(old),1));CHECK(createCalls==creates);CHECK(processCalls==processes);CHECK(!g_legionMotionSessionKey);
    sameTickRejected();nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));CHECK(createCalls==creates+1);CHECK(liveHandles()==1);effects.push_back({{"event","context-retired-and-rebuilt"},{"cause",cause},{"crossSessionAuthority","actual main legionMotionSubmissionCurrent, not global token history"}});
 }
}
static void steadyModeAndBorrow(){
 setupModel(familyTopology.at("models").at(0),0,false);nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));void* h=actualHandle();CHECK(bridge_mode_read(h)==0);
 g_realStickTest.autoCalibrate=true;nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));CHECK(bridge_mode_read(h)==3);g_realStickTest.autoCalibrate=false;nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));CHECK(bridge_mode_read(h)==0);
 g_realStickTest.autoCalibrate=true;nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));CHECK(bridge_mode_read(h)==3);
 const std::array<float,3> live{0.75f,-0.25f,0.125f};bridge_offset(h,live[0],live[1],live[2],8);++clockMs;
 const auto request=inputCaptureControllerCalibrationRequest();CHECK(request.value("ok",false));const auto duplicate=inputCaptureControllerCalibrationRequest();CHECK(!duplicate.value("ok",true)&&duplicate.value("reason","")=="in-progress");
 const auto preRequest=legionMotionSnapshot(clockMs);CHECK(preRequest&&preRequest->capturedAt<clockMs);const auto calls=processCalls;CHECK(!inputCaptureLegionMotion(preRequest,preRequest?axes(*preRequest):cm::RawSixAxes{},static_cast<double>(clockMs)/1000));CHECK(processCalls==calls);CHECK(g_controllerCalibrationActiveRequest.has_value());
 inputCaptureControllerCalibrationAbort();CHECK(!g_controllerCalibrationHasRequest.load()&&!g_controllerCalibrationActiveRequest);CHECK(bridge_mode_read(h)==0);const auto restored=actualOffset(h);for(unsigned i=0;i<3;++i)CHECK(near(restored[i],live[i]));
 nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));CHECK(bridge_mode_read(h)==3);const float confidence=inputCaptureControllerCalibrationOps().getConfidence(h);CHECK(std::isfinite(confidence));effects.push_back({{"event","actual-borrow-abort-live-baseline"},{"liveOffset",live},{"confidenceObserved",confidence},{"convergenceGate",false},{"requestBeforeTraining",true}});
}
static void freshTrainingAndCancel(){
 setupModel(familyTopology.at("models").at(0),0,true);nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));
 void* h=actualHandle();CHECK(bridge_mode_read(h)==3);const auto creates=createCalls,deletes=deleteCalls;
 // This is a live native baseline, deliberately different from the startup file.
 const std::array<float,3> live{1.25f,-0.375f,0.625f};bridge_offset(h,live[0],live[1],live[2],8);
 const auto liveBefore=actualOffset(h);for(unsigned i=0;i<3;++i)CHECK(near(liveBefore[i],live[i]));
 json storedBefore;{std::ifstream f(calibrationPath);f>>storedBefore;}
 ++clockMs;const auto request=inputCaptureControllerCalibrationRequest();CHECK(request.value("ok",false));
 CHECK(g_controllerCalibrationRequest.has_value());if(!g_controllerCalibrationRequest)return;
 const auto requestedAt=g_controllerCalibrationRequest->requestedAt;CHECK(requestedAt==clockMs);
 const auto nativeResetsBefore=bridge_reset_calls(),nativeProcessesBefore=bridge_process_calls();
 nextFresh();const auto training=decodeAtCurrentQueueTime();CHECK(training.capturedAt>requestedAt);
 CHECK(legionMotionSubmissionCurrent(training,clockMs));CHECK(!actualConsume(training));
 const auto nativeResetsAfter=bridge_reset_calls(),nativeProcessesAfter=bridge_process_calls();
 CHECK(nativeResetsAfter==nativeResetsBefore+1);CHECK(nativeProcessesAfter==nativeProcessesBefore+1);
 CHECK(actualHandle()==h&&createCalls==creates&&deleteCalls==deletes);
 const int trainingMode=bridge_mode_read(h);const auto trainingOffset=actualOffset(h);
 std::array<float,3> calibrated{};bridge_cal(h,&calibrated[0],&calibrated[1],&calibrated[2]);for(float f:calibrated)CHECK(std::isfinite(f));
 const float confidence=bridge_confidence(h);CHECK(std::isfinite(confidence));
 const bool nonterminal=g_controllerCalibrationTransaction&&g_controllerCalibrationTransaction->active();
 json observation={{"type","training-path-observation"},{"case",activeCase},{"requestedAt",requestedAt},{"sampleCapturedAt",training.capturedAt},
     {"strictlyPostRequest",training.capturedAt>requestedAt},{"nativeResetsBefore",nativeResetsBefore},{"nativeResetsAfter",nativeResetsAfter},
     {"nativeProcessesBefore",nativeProcessesBefore},{"nativeProcessesAfter",nativeProcessesAfter},{"trainingMode",trainingMode},
     {"liveBaseline",liveBefore},{"trainingOffset",trainingOffset},{"calibratedAfterProcess",calibrated},{"actualConfidence",confidence},
     {"nonterminalTrainingObserved",nonterminal},{"convergenceGate",false},{"receiptClock","controlled host queue ms, not firmware time"}};
 if(nonterminal){
    CHECK(g_controllerCalibrationActiveRequest&&g_controllerCalibrationHasRequest.load());CHECK(trainingMode==3);
    CHECK(g_controllerCalibrationTransaction->outcome()==cm::CalibrationOutcome::None);
    CHECK(!g_controllerCalibrationTransaction->candidate());
    // Real main cancellation rolls back the borrowed live handle before retirement.
    inputCaptureControllerCalibrationAbort();CHECK(!g_controllerCalibrationHasRequest.load()&&!g_controllerCalibrationActiveRequest&&!g_controllerCalibrationTransaction);
    const int rolledBackMode=bridge_mode_read(h);CHECK(rolledBackMode==0);const auto restored=actualOffset(h);
    for(unsigned i=0;i<3;++i)CHECK(near(restored[i],live[i]));
    json storedAfter;{std::ifstream f(calibrationPath);f>>storedAfter;}CHECK(storedAfter==storedBefore);
    CHECK(actualHandle()==h&&createCalls==creates&&deleteCalls==deletes&&liveHandles()==1);
    bool abortedResult=false;for(const auto& event:calibrationEvents)if(event.at("event")=="gyro.calibrate.result"){
        const auto& value=event.at("value");if(value.value("requestId",std::uint64_t{0})==request.at("requestId")){
            CHECK(!value.value("saved",true)&&!value.value("persisted",true));CHECK(value.value("reason","")=="source-retired");abortedResult=true;
        }
    }CHECK(abortedResult);
    observation["branch"]="nonterminal-training-main-Abort-live-baseline-rollback";
    observation["rollbackMode"]=rolledBackMode;observation["rollbackOffset"]=restored;observation["privateCalibrationUnchanged"]=storedAfter==storedBefore;
 }else{
    // Do not manufacture a pending state if the real native algorithm completes early.
    bool succeeded=false;for(const auto& event:calibrationEvents)if(event.at("event")=="gyro.calibrate.result"){
        const auto& value=event.at("value");if(value.value("requestId",std::uint64_t{0})==request.at("requestId")){
            succeeded=value.value("saved",false)&&value.value("persisted",false)&&value.value("reason","")=="succeeded";observation["earlyTerminal"]=value;
        }
    }CHECK(succeeded);CHECK(!g_controllerCalibrationHasRequest.load()&&!g_controllerCalibrationActiveRequest);CHECK(trainingMode==0);
    observation["branch"]="actual-early-success-no-cancel-rollback-claim";
 }
 const auto idleProcessesBefore=bridge_process_calls();nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));
 const int idleMode=bridge_mode_read(h);CHECK(idleMode==3);CHECK(bridge_process_calls()==idleProcessesBefore+1);
 CHECK(actualHandle()==h&&createCalls==creates&&deleteCalls==deletes);CHECK(bridge_reset_calls()==nativeResetsAfter);
 observation["nextIdleMode"]=idleMode;observation["nextIdleActualProcess"]=bridge_process_calls()==idleProcessesBefore+1;
 observation["currentSoftwareAutoPreference"]=g_controllerAutoCalibrationRequested.load();observation["sameLiveHandleThroughIdle"]=actualHandle()==h;
 effects.push_back(observation);std::cout<<observation.dump()<<'\n';
}
static void stopSuspendRetirement(){
 for(unsigned stopped=0;stopped<2;++stopped){setupModel(familyTopology.at("models").at(0),0,true);nextFresh();const auto old=decodeAtCurrentQueueTime();finiteResult(actualConsume(old));const auto deleted=deleteCalls;
    if(stopped)g_inputCaptureStop.store(true);else g_powerLifecycle.store(PowerLifecycle::Suspended);CHECK(!legionMotionSnapshot(clockMs));CHECK(!inputCaptureLegionMotion(legionMotionSnapshot(clockMs),axes(old),1));CHECK(deleteCalls==deleted+1&&liveHandles()==0);
    g_inputCaptureStop.store(false);g_powerLifecycle.store(PowerLifecycle::Ready);const auto creates=createCalls;CHECK(!inputCaptureLegionMotion(old,axes(old),2));CHECK(createCalls==creates);sameTickRejected();nextFresh();finiteResult(actualConsume(decodeAtCurrentQueueTime()));CHECK(createCalls==creates+1);CHECK(bridge_mode_read(actualHandle())==3);
 }
}
static CarrierProbe scopedProbe(const MotionSample* controller,const ymcc::windows_motion::Stamp* windows,bool neutral,const std::function<void(unsigned)>& hook={}){
 auto common=commonSnapshot();CommonFrameSourceScope cs(&common);LegionMotionSubmissionScope ls(controller);WindowsMotionSubmissionScope ws(windows);auto result=carrierProbe(true,neutral,hook);CHECK(result.scoped&&result.controller&&result.windows);return result;
}
static void carrierIssueRequestFences(){
 setupModel(familyTopology.at("models").at(0),0);nextFresh();const auto input=decodeAtCurrentQueueTime();finiteResult(actualConsume(input));
 auto valid=scopedProbe(&input,nullptr,false);CHECK(valid.issue&&valid.request);
 auto beforeIssue=scopedProbe(&input,nullptr,false,[](unsigned phase){if(phase==0)++g_legionMotionCaptureEpoch;});CHECK(!beforeIssue.issue);CHECK(!beforeIssue.request&&beforeIssue.response.value("deviceWriteAttempted",true)==false);
 auto release=scopedProbe(&input,nullptr,true);CHECK(release.issue&&release.request);
 // A newly valid report and source context precedes the second barrier interleave.
 nextFresh();const auto fresh=decodeAtCurrentQueueTime();finiteResult(actualConsume(fresh));
 auto beforeRequest=scopedProbe(&fresh,nullptr,false,[](unsigned phase){if(phase==1)++g_hidResolveCacheGeneration;});CHECK(beforeRequest.issue&&!beforeRequest.request);CHECK(beforeRequest.response.value("deviceWriteAttempted",true)==false);
 auto neutral=scopedProbe(&fresh,nullptr,true);CHECK(neutral.issue&&neutral.request);
 auto targetChanged=scopedProbe(&fresh,nullptr,true,[](unsigned phase){if(phase==0)++frameCurrent.host;});CHECK(!targetChanged.issue&&!targetChanged.request);
}
static void windowsStampRealGuard(){
 setupModel(familyTopology.at("models").at(0),0);g_machineIdentity.family=YmccFamily::RogAlly;
 struct WindowsOwner{std::atomic<std::uint64_t> activeBindingGeneration{77};};auto owner=std::make_shared<WindowsOwner>();const auto stamp=ymcc::windows_motion::retain(owner,77);
 CHECK(ymcc::windows_motion::current(stamp));auto valid=scopedProbe(nullptr,&stamp,false);CHECK(valid.issue&&valid.request);
 auto stale=scopedProbe(nullptr,&stamp,false,[&](unsigned phase){if(phase==0)owner->activeBindingGeneration.fetch_add(1);});CHECK(!stale.issue&&!stale.request);CHECK(stale.response.is_object()&&stale.response.value("detail","")=="windows-motion-source-retired");
 auto neutral=scopedProbe(nullptr,&stamp,true);CHECK(neutral.issue&&neutral.request);CHECK(!ymcc::windows_motion::current(stamp));
 effects.push_back({{"event","windows-stamp-real-header"},{"constantTrueBypass",false},{"WindowsNumericalCallbacks",false}});
}
struct Scenario {const char* name;void(*run)();};
static const Scenario scenarios[]={{"four-family-decode-main-dll-carrier",familyPipeline},{"current-context-new-Session",staleContextAndNewSession},{"mode-and-pending-borrow-cancel",steadyModeAndBorrow},{"fresh-request-training-reset-cancel",freshTrainingAndCancel},{"stop-suspend-like-retirement",stopSuspendRetirement},{"carrier-issue-request-fences",carrierIssueRequestFences},{"windows-stamp-real-guard",windowsStampRealGuard}};
int main(int argc,char** argv){
 if(argc!=5)return 3;std::cout<<std::unitbuf;calibrationPath=std::filesystem::path(argv[1])/"initial.json";{std::ifstream f(argv[2]);f>>familyTopology;}if(familyTopology.at("schema")!=1)return 3;
 if(!bridge_open(argv[3],argv[4])){std::cerr<<"ABI gap: "<<bridge_error()<<'\n';return 4;}
 unsigned failedCases=0;
 try{for(const auto& scenario:scenarios){activeCase=scenario.name;const auto firstChecks=checks,firstFailures=failures;scenario.run();inputCaptureLegionMotionSessionRevoke();CHECK(liveHandles()==0&&!g_controllerCalibrationHasRequest.load());if(failures!=firstFailures)++failedCases;std::cout<<"CASE "<<activeCase<<" checks="<<checks-firstChecks<<" failures="<<failures-firstFailures<<'\n';}}
 catch(const std::exception& e){++failures;std::cerr<<"RUNTIME GAP "<<e.what()<<'\n';inputCaptureLegionMotionSessionRevoke();}
 inputCaptureLegionMotionSessionRevoke();g_legionMotionSession.reset();g_controllerCalibrationTransaction.reset();CHECK(liveHandles()==0);const bool closed=bridge_close()!=0;CHECK(closed);libraryLoaded=false;
 {std::ofstream f(std::filesystem::path(argv[1])/"effects.json");f<<json(effects).dump(2);} {std::ofstream f(std::filesystem::path(argv[1])/"production-trace.json");f<<json(trace).dump(2);}
 std::cout<<"SUMMARY cases="<<std::size(scenarios)<<" checks="<<checks<<" failures="<<failures<<" failedCases="<<failedCases<<'\n';
 std::cout<<json({{"type","consumer-result"},{"status",failures?"fail":"pass"},{"checks",checks},{"failures",failures},{"cases",std::size(scenarios)},{"lastCaseCreated",createCalls},{"cleanupComplete",closed&&liveHandles()==0},{"actualPbt",false},{"physicalTargetAcceptance",false}}).dump()<<'\n';return failures?1:0;
}
