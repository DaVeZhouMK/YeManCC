namespace candidate {
static std::uint64_t fixtureTick=1000;
static ULONGLONG GetTickCount64(){return fixtureTick;}
static float fixtureConfidence=1.0f;
static float fixtureGetConfidence(void*){return fixtureConfidence;}
static void(__cdecl *g_gmSetOffset)(void*,float,float,float,int)=nullptr;
static void(__cdecl *g_gmSetMode)(void*,int)=nullptr;
static void(__cdecl *g_gmResetCal)(void*)=nullptr;
static void(__cdecl *g_gmGetOffset)(void*,float*,float*,float*)=nullptr;
static void(__cdecl *g_gmResetMotionApi)(void*)=nullptr;
static auto g_gmGetConf=&fixtureGetConfidence;
static bool(__cdecl *g_gmIsSteady)(void*)=nullptr;
static std::chrono::steady_clock::time_point g_gmSessionStart{};
static std::atomic<bool> g_gmSessionResetRequested{false};
static std::atomic<const char*> g_gmSessionResetReason{"fixture"};
static std::string g_calibRestoredDeviceId;
// @@PROCESSOR_GLOBALS@@
// @@CALIBRATION_DECLARATIONS@@
static ymcc::reviewed_calibration::Context g_reviewedCalibrationContext;
static std::vector<json> fixtureEvents,fixtureUiEvents;
static int fixtureStatus=-1,fixtureResubscribe=0;
static bool fixtureFailFlush=false,fixtureRetireOnFlush=false;
static std::function<void()> fixtureAfterMove;
static std::function<void()> fixtureBeforeLoad,fixtureAfterLoad,fixtureBeforeFinish;
static void inputCapturePostCalibStatus(int status){fixtureStatus=status;}
static void appendNativeLifecycleLog(const char* name,const json& body){fixtureEvents.push_back({{"event",name},{"body",body}});}
static void gamepadSerialPostEvent(const char* name,const json& body){fixtureUiEvents.push_back({{"event",name},{"body",body}});}
static void ymccMadgwickResetOrientation(){}
enum class SensorThreadRequest{Resubscribe};
static void inputCaptureSensorThreadPost(SensorThreadRequest){++fixtureResubscribe;}
static BOOL FlushFileBuffers(HANDLE handle){
    if(fixtureFailFlush){SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    const auto result=::FlushFileBuffers(handle);
    if(result&&fixtureRetireOnFlush){fixtureRetireOnFlush=false;mockSink.activeBindingGeneration.fetch_add(1);}
    return result;
}
static BOOL MoveFileExW(LPCWSTR from,LPCWSTR to,DWORD flags){
    const auto result=::MoveFileExW(from,to,flags);if(result&&fixtureAfterMove){auto action=std::move(fixtureAfterMove);fixtureAfterMove={};action();}return result;
}
// @@CALIBRATION_FUNCTIONS@@
// @@PROCESSOR_FUNCTIONS@@
#if YMCC_FIXTURE_PROCESSING_CONTEXT
#include "private_profile_scope.inc"
#endif
static unsigned fixtureProcessCount=0,fixtureResetCount=0;
static decltype(g_gmResetMotionApi) fixtureRealReset=nullptr;
static void fixtureRecordingReset(void* handle){++fixtureResetCount;fixtureRealReset(handle);}
static std::array<float,3> fixtureOffsetAtProcess{};
static std::array<float,6> fixtureProcessedAxes{};
static decltype(g_gmProcess) fixtureRealProcess=nullptr;
static std::function<void()> fixtureAfterProcess;
static void fixtureRecordingProcess(void* h,float gx,float gy,float gz,float ax,float ay,float az,float dt){
    if(g_gmGetOffset)g_gmGetOffset(h,&fixtureOffsetAtProcess[0],&fixtureOffsetAtProcess[1],&fixtureOffsetAtProcess[2]);
    ++fixtureProcessCount;fixtureProcessedAxes={gx,gy,gz,ax,ay,az};fixtureRealProcess(h,gx,gy,gz,ax,ay,az,dt);
    if(fixtureAfterProcess){auto action=std::move(fixtureAfterProcess);fixtureAfterProcess={};action();}
}
static bool legionMotionSubmissionCurrent(const MockControllerMotionObservation&,ULONGLONG){return false;}
struct FixtureFrame {bool physical=false,calibration=false,processed=false,output=false,outputStamp=false,publication=false;};
static FixtureFrame fixtureWindowsFrame(const std::shared_ptr<MockBindingSink>& windowsMotionSensorOwner,
    std::uint64_t gyroBindingGeneration=73,std::uint64_t accelBindingGeneration=73,bool live=false,
    bool gyro=true,bool accel=true,std::array<double,6> raw={0,0,0,0,0,1},std::uint64_t accelAge=0,const WindowsSensorView* selected=nullptr){
    fixtureTick+=8;
    // Native snapshot is controlled; CPU adoption and the processing wrapper are production bodies.
    WindowsSensorView windowsSensorView;windowsSensorView.metadata=inputCaptureSensorMetaSnapshot();
    windowsSensorView.capture=ymcc::windows_motion::retain(windowsMotionSensorOwner,gyroBindingGeneration);
    windowsSensorView.submission=windowsSensorView.capture;windowsSensorView.coherent=true;
    const auto now=fixtureTick;const auto gyroReceiptTick=now,accelReceiptTick=now-accelAge;
    const UINT64 gyroSequence=10,accelSequence=10;
    for(unsigned k=0;k<2;++k){auto& part=windowsSensorView.parts[k];part.axes={raw[k*3],raw[k*3+1],raw[k*3+2]};part.usable=k?accel:gyro;
        part.generation=k?accelBindingGeneration:gyroBindingGeneration;part.sequence=10;part.timestamp100ns=now*10000;part.receipt=k?accelReceiptTick:gyroReceiptTick;}
    if(selected)windowsSensorView=*selected;
    const ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> scope(windowsSensorView,t_windowsProcessingMeta,t_windowsProcessingStamp,&t_windowsProcessingView);
    const bool windowsProcessorOwnerReady=inputCaptureWindowsProcessingAdopt(windowsSensorView);
    const bool windowsSensorViewCurrent=ymcc::sensors::view::current(windowsSensorView);
    const auto sensorMeta=inputCaptureSensorMetaSnapshot();
    const auto gyroProvider=inputCaptureSensorProviderKey(sensorMeta.gyroDeviceId),accelProvider=inputCaptureSensorProviderKey(sensorMeta.accelDeviceId);
    const bool sameProvider=!gyroProvider.empty()&&gyroProvider==accelProvider;
    const bool sameBindingGeneration=gyroBindingGeneration!=0&&gyroBindingGeneration==accelBindingGeneration;
    const auto activeBindingGeneration=g_inputCaptureSink?g_inputCaptureSink->activeBindingGeneration.load():0;
    const bool currentBindingGeneration=sameBindingGeneration&&gyroBindingGeneration==activeBindingGeneration;
    const bool selectorBound=inputCaptureMotionSelectorBound();
    const bool controllerMotionRoute=false,controllerMotionPair=false,wakeGyroHold=false;
    const std::optional<MockControllerMotionObservation> controllerMotion;
    const double gx=raw[0],gy=raw[1],gz=raw[2],ax=raw[3],ay=raw[4],az=raw[5];
    double mgx=gx,mgy=gy,mgz=gz,maxa=ax,may=ay,maz=az;
    double hcCandidateGx=gx,hcCandidateGy=gy,hcCandidateGz=gz,hcCandidateAx=ax,hcCandidateAy=ay,hcCandidateAz=az;
    inputCaptureApplyHcMatrixCandidate(gx,gy,gz,ax,ay,az,hcCandidateGx,hcCandidateGy,hcCandidateGz,hcCandidateAx,hcCandidateAy,hcCandidateAz);
    std::string matrixIdentity;const float motionDeltaSeconds=0.008f;
    g_gmProcessedThisTick=false;
    // @@ACTUAL_PIPELINE@@
    // @@BUS_CALL@@
    inputCaptureWindowsProcessingNoteFrame(windowsSensorView,g_gmProcessedThisTick);
    const bool motionProcessorReady=g_gmProcessedThisTick&&windowsProcessorOwnerReady&&inputCaptureWindowsProcessingFrameCurrent(windowsSensorView);
    // @@OUTPUT_GATES@@
    return {hcMatrixAdmitted,calibrationPairProven,g_gmProcessedThisTick,gyroSubmitAllowed,ymcc::windows_motion::current(windowsMotionSource),publicationAllowed};
}
}
static void txCheck(bool ok,const std::string& label){check(ok,"windows-source-only-calibration",label);}
int main(int argc,char**argv){try{
 if(argc!=4)return 2;json oracle;std::ifstream(argv[1])>>oracle;
 const auto module=LoadLibraryExW(std::filesystem::path(argv[2]).wstring().c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("Pinned DLL load failed");
 const auto sym=[&](const char* name){auto p=GetProcAddress(module,name);if(!p)throw std::runtime_error(name);return p;};
 const auto create=reinterpret_cast<void*(__cdecl *)()>(sym("CreateGamepadMotion"));const auto destroy=reinterpret_cast<void(__cdecl *)(void*)>(sym("DeleteGamepadMotion"));
 const auto set=reinterpret_cast<void(__cdecl *)(void*,float,float,float,int)>(sym("SetCalibrationOffset"));
 candidate::g_gmSetOffset=set;candidate::g_gmSetMode=reinterpret_cast<decltype(candidate::g_gmSetMode)>(sym("SetCalibrationMode"));
 candidate::g_gmResetCal=reinterpret_cast<decltype(candidate::g_gmResetCal)>(sym("ResetContinuousCalibration"));candidate::g_gmGetOffset=reinterpret_cast<decltype(candidate::g_gmGetOffset)>(sym("GetCalibrationOffset"));
 candidate::fixtureRealReset=reinterpret_cast<decltype(candidate::g_gmResetMotionApi)>(sym("ResetGamepadMotion"));candidate::g_gmResetMotionApi=&candidate::fixtureRecordingReset;
 candidate::g_gmGetCal=reinterpret_cast<decltype(candidate::g_gmGetCal)>(sym("GetCalibratedGyro"));candidate::fixtureRealProcess=reinterpret_cast<decltype(candidate::g_gmProcess)>(sym("ProcessMotion"));candidate::g_gmProcess=&candidate::fixtureRecordingProcess;
 const auto file=std::filesystem::path(candidate::inputCaptureCalibrationJsonPath()),expectedFile=std::filesystem::path(argv[3]).parent_path()/"isolated-appdata"/"YeManCC"/"calibration.json";
 if(std::filesystem::absolute(file).lexically_normal()!=std::filesystem::absolute(expectedFile).lexically_normal())throw std::runtime_error("Fixture cannot access real calibration path");std::filesystem::create_directories(file.parent_path());
 const auto write=[&](const json& d){std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<d.dump(2);};
 const auto read=[&](){json d;std::ifstream in(file);in>>d;return d;};
 const auto offset=[&](){std::array<float,3> out{};candidate::g_gmGetOffset(candidate::g_gm,&out[0],&out[1],&out[2]);return out;};
 const auto result=[&](){for(auto it=candidate::fixtureEvents.rbegin();it!=candidate::fixtureEvents.rend();++it)if((*it)["event"]=="gyro-calibration-result")return (*it)["body"];return json::object();};
 std::shared_ptr<candidate::MockBindingSink> owner(&candidate::mockSink,[](auto*){});std::uint64_t sequence=0,scenarios=0;std::set<std::string> models;
 const auto seed=[&](const json& row,const std::string& provider,bool target){
    candidate::inputCaptureWindowsProcessingRetire(false);candidate::g_windowsProcessingOwner={};candidate::g_gmSessionResetRequested=false;
    if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);
    candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();candidate::g_gm=create();candidate::g_gmSessionStart=std::chrono::steady_clock::time_point(std::chrono::nanoseconds(++sequence));candidate::mockCapabilityPower=7;
    candidate::g_sensorMeta.gyroPresent=candidate::g_sensorMeta.accelPresent=true;candidate::g_sensorMeta.gyroDeviceId="ACPI#FIXTURE0001#1#{GYRO}";candidate::g_sensorMeta.accelDeviceId="ACPI#FIXTURE0001#1#{ACCEL}";
    candidate::g_sensorMeta.gyroFamily=candidate::g_sensorMeta.accelFamily=provider;
    candidate::g_reviewedCalibrationContext={};candidate::g_calibRestoredDeviceId.clear();candidate::g_gyroCalibStageAtomic=candidate::GyroCalibStage::None;candidate::g_gyroCalibRequested=false;candidate::g_calibSourceAuto=false;
    candidate::g_autoCalibTrusted=false;candidate::g_autoCalibFinished=false;candidate::g_autoCalibAttempts=0;candidate::g_autoCalibWindowStart=0;candidate::g_autoStillSince=0;candidate::g_autoStillLastSteadyTick=0;candidate::g_gmProcessedThisTick=false;
    candidate::g_realStickTest.autoCalibrate=false;candidate::g_realStickTest.virtualTargetEnabled=target;candidate::fixtureConfidence=1;candidate::fixtureBeforeLoad={};candidate::fixtureAfterLoad={};candidate::fixtureBeforeFinish={};candidate::fixtureAfterProcess={};candidate::fixtureProcessCount=0;
    candidate::g_gmSetMode(candidate::g_gm,0);set(candidate::g_gm,0,0,0,0);write({{"keep",{{"unrelated",true}}}});candidate::inputCaptureGyroCalibrationRestoreOnce();candidate::fixtureEvents.clear();candidate::fixtureUiEvents.clear();
    return candidate::inputCaptureCalibrationStorageKey(candidate::g_sensorMeta.gyroDeviceId);
 };

 std::map<std::string,json> rows;const std::set<std::string> expected=oracle["expectedSourceQualifiedModels"].get<std::set<std::string>>();
 for(const auto& row:oracle["legacyIdentities"]){candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();if(expected.contains(candidate::g_machineIdentity.deviceClass))rows.emplace(candidate::g_machineIdentity.deviceClass+"|"+candidate::g_machineIdentity.product,row);}
 txCheck(rows.size()==7,"all six independent OEM models and both 1T42/1T52 aliases are present");
 for(const auto& [caseId,row]:rows){const auto model=caseId.substr(0,caseId.find('|'));models.insert(model);seed(row,"winrt",false);candidate::fixtureResetCount=0;
  const auto defaultMeta=candidate::g_sensorMeta;const auto defaultKey=candidate::inputCaptureCalibrationStorageKey(defaultMeta.gyroDeviceId);
  const auto defaultMatrix=candidate::inputCaptureCandidateMatrixReceipt(defaultMeta);const auto defaultAxes=candidate::mapped({1,2,3,0,0,1});
  candidate::inputCaptureStoreCalibration(defaultMeta.gyroDeviceId,1,2,3,10,1);
  txCheck(!defaultKey.empty()&&candidate::inputCaptureSourceQualifiedCalibrationRecipe()==nullptr,"default OEM route keeps its original calibration owner: "+model);
  const auto modelRecipe=candidate::inputCaptureWindowsSelectionEvidence(json::object(),candidate::inputCaptureMotionCapabilityReceipt(defaultMeta,false),defaultMatrix,candidate::fixtureTick).model;
  txCheck(modelRecipe.valid(),"actual OEM capability and effective matrix produce a valid selection profile: "+model);
  auto capture=std::make_shared<candidate::MockBindingSink>(),choice=std::make_shared<candidate::MockBindingSink>();auto alive=std::make_shared<std::atomic<bool>>(true);
  const auto make=[&](const std::string& suffix){candidate::WindowsSensorView view;view.coherent=view.privateSource=true;view.metadata=defaultMeta;
    view.metadata.gyroDeviceId="ACPI#"+suffix+"#1#{GYRO}";view.metadata.accelDeviceId="ACPI#"+suffix+"#1#{ACCEL}";
    view.capture=ymcc::windows_motion::retain(capture,73);view.submission=ymcc::windows_motion::guarded(view.capture,ymcc::windows_motion::retain(choice,73),alive);
    view.captureRequestId=++sequence;view.choiceRequestId=view.captureRequestId+100;view.profile=ymcc::sensors::selection::fingerprint(modelRecipe);
    view.calibrationKey=ymcc::sensors::selection::calibrationKey(modelRecipe,{view.metadata.gyroDeviceId,view.metadata.accelDeviceId},ymcc::sensors::selection::TransformPolicy::ExplicitCurrentModel);
    for(unsigned k=0;k<2;++k){auto& part=view.parts[k];part.usable=true;part.axes=k?std::array<double,3>{0,0,1}:std::array<double,3>{1,2,3};part.generation=73;part.sequence=++sequence;part.receipt=candidate::fixtureTick;part.timestamp100ns=candidate::fixtureTick*10000;}
    return view;};
  auto a=make("OEM-A"),b=make("OEM-B");
  bool adopted=false;{
   candidate::FixtureProcessingScope scope(a);const auto requested=candidate::inputCapturePrivateCalibrationRequestedKey(a,a.metadata.gyroDeviceId);
   txCheck(!requested.empty()&&requested==a.calibrationKey,"complete explicit OEM source receives its own validated calibration key: "+model);
   adopted=candidate::inputCaptureWindowsProcessingAdopt(a);txCheck(adopted,"actual CPU owner adopts complete OEM WinRT source: "+model);
   if(adopted){const auto* recipe=candidate::inputCaptureSourceQualifiedCalibrationRecipe();txCheck(recipe&&recipe->deviceClass==model&&recipe->gyroMatrixId==modelRecipe.gyroMatrixId&&recipe->accelMatrixId==modelRecipe.accelMatrixId&&recipe->sourceSha256==modelRecipe.gyroSha256,"calibration uses actual effective WinRT axes and evidence digest: "+model);
    txCheck(candidate::inputCaptureCalibrationStorageKey(a.metadata.gyroDeviceId)==a.calibrationKey&&a.calibrationKey!=defaultKey,"private key cannot collide with default OEM record: "+model);
    candidate::inputCaptureStoreCalibration(a.metadata.gyroDeviceId,11,12,13,10,1);candidate::inputCaptureGyroCalibrationRestoreOnce();
    txCheck(offset()==std::array<float,3>{11,12,13}&&read().contains(defaultKey)&&read()[defaultKey]["xOffset"]==1,"actual existing DLL restores private offset and preserves default record: "+model);
   }
  }
  if(!adopted){++scenarios;continue;}
  before::setup(row);before::inputCaptureResolveHcMotionSelector();before::g_sensorMeta.gyroFamily=before::g_sensorMeta.accelFamily="winrt";
  const auto frozenAxes=before::mapped({1,2,3,0,0,1});
  const auto declaredAxes=oracle["oemPrivateRecipeOracle"]["models"][model]["sample"].get<std::array<double,6>>();txCheck(frozenAxes==declaredAxes,"frozen native route agrees with independently parsed HC C# inheritance: "+caseId);
  // The already owned view is intentionally supplied; the real worker verifies the same decoration.
  const auto frame=candidate::fixtureWindowsFrame(owner,73,73,false,true,true,{1,2,3,0,0,1},0,&a);
  bool axesMatch=true;for(unsigned i=0;i<6;++i)axesMatch=axesMatch&&std::abs(candidate::fixtureProcessedAxes[i]-declaredAxes[i])<1e-5;
  txCheck(frame.processed&&!frame.output&&!frame.outputStamp&&!frame.physical,"OEM private data processes without a pad and cannot unlock virtual output: "+model);
  txCheck(axesMatch&&candidate::fixtureOffsetAtProcess==std::array<float,3>{11,12,13},"actual pinned processing preserves established OEM WinRT matrix and restores before processing: "+model);
  const auto receipt=candidate::inputCaptureWindowsProcessingReceiptSnapshot();txCheck(receipt.value("currentSamplingAndProcessingLease",false)&&receipt.value("privateSource",false)&&receipt.value("calibrationKey",std::string{})==a.calibrationKey,"only actual native processing produces OEM source evidence: "+model);
  const auto aCpu=a.submission;candidate::g_gyroCalibRequested=true;candidate::fixtureWindowsFrame(owner,73,73,false,true,true,{1,2,3,0,0,1},0,&a);candidate::fixtureWindowsFrame(owner,73,73,false,true,true,{1,2,3,0,0,1},0,&a);
  txCheck(candidate::g_reviewedCalibrationContext.transaction.active,"explicit OEM source has an actual calibration transaction: "+model);
  {candidate::FixtureProcessingScope scope(b);txCheck(candidate::inputCaptureWindowsProcessingAdopt(b),"actual OEM handoff can adopt source B: "+model);
   txCheck(!candidate::g_reviewedCalibrationContext.transaction.active&&!ymcc::windows_motion::current(aCpu)&&alive->load(),"OEM handoff cancels old calibration and CPU lease while retaining native capture: "+model);
   candidate::inputCaptureStoreCalibration(b.metadata.gyroDeviceId,-7,-8,-9,10,1);candidate::inputCaptureGyroCalibrationRestoreOnce();txCheck(offset()==std::array<float,3>{-7,-8,-9},"source B restores an independent OEM calibration record: "+model);
  }
  const auto bCpu=b.submission;candidate::fixtureWindowsFrame(owner,73,73,true);txCheck(!ymcc::windows_motion::current(bCpu)&&candidate::inputCaptureCalibrationStorageKey(defaultMeta.gyroDeviceId)==defaultKey&&offset()==std::array<float,3>{1,2,3},"return to default restores the established OEM offset and original key: "+model);
  txCheck(candidate::inputCaptureSourceQualifiedCalibrationRecipe()==nullptr&&candidate::inputCaptureCandidateMatrixReceipt(defaultMeta)==defaultMatrix&&candidate::mapped({1,2,3,0,0,1})==defaultAxes,"ordinary OEM recipe, transform and owner remain unchanged: "+model);
  for(const std::string why:{"profile","digest","sensorapi","controller-route","unknown-model","cpu-retired","manufacturer","board","family","selector","gyro-matrix","accel-matrix","coherent-wrong-axes","selector-class"}){auto bad=a;bad.submission=ymcc::windows_motion::guarded(bad.capture,ymcc::windows_motion::retain(choice,73),alive);bad.processingOwnerAccepted=false;
   const auto identity=candidate::g_machineIdentity;const auto selector=candidate::g_hcMotionSelector;
   if(why=="profile")bad.profile+="wrong";if(why=="digest")candidate::g_hcMotionSelector.matrixSourceSha256="wrong";
   if(why=="sensorapi")bad.metadata.gyroFamily=bad.metadata.accelFamily="sensorapi";
   if(why=="controller-route")candidate::g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet";
   if(why=="unknown-model")candidate::g_machineIdentity.deviceClass="HandheldCompanion.Devices.UNKNOWN";
   if(why=="manufacturer")candidate::g_machineIdentity.manufacturer="WRONG OEM";
   if(why=="board")candidate::g_machineIdentity.product="UNKNOWN BOARD";
   if(why=="family")candidate::g_machineIdentity.family=candidate::YmccFamily::Unknown;
   if(why=="selector")candidate::g_hcMotionSelector.selectorId="wrong-selector";
   if(why=="selector-class")candidate::g_hcMotionSelector.deviceClass="wrong-class";
   if(why=="gyro-matrix")candidate::g_machineIdentity.gyroMatrixId="wrong-gyro";
   if(why=="accel-matrix")candidate::g_machineIdentity.accelMatrixId="wrong-accel";
   if(why=="coherent-wrong-axes"){candidate::g_machineIdentity.gyroMatrixId="hc-06c0b954:msi-claw-wrong:gyro:x-y-z";candidate::g_hcMotionSelector.gyroMatrixId=candidate::g_machineIdentity.gyroMatrixId;}
   if(why=="cpu-retired")bad.submission.processingLease=std::make_shared<std::atomic<bool>>(false);
   {candidate::FixtureProcessingScope scope(bad);const auto previous=read();if(why!="profile")txCheck(candidate::inputCaptureSourceQualifiedCalibrationRecipe()==nullptr,"invalid OEM recipe rejected before calibration: "+why+":"+caseId);txCheck(!candidate::inputCaptureWindowsProcessingAdopt(bad),"invalid OEM source rejected: "+why+":"+model);candidate::inputCaptureStoreCalibration(bad.metadata.gyroDeviceId,99,99,99,10,1);txCheck(read()==previous,"invalid explicit OEM source cannot overwrite defaults or private calibration: "+why+":"+model);}
   candidate::g_machineIdentity=identity;candidate::g_hcMotionSelector=selector;
  }
  ++scenarios;
 }
 candidate::inputCaptureWindowsProcessingRetire();if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);
 json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"groups",groups},{"models",models},{"scenarios",scenarios},{"actualAdmissionAndProcessStatements",true},{"actualBusCall",true},{"actualGamepadMotion",true},{"hardwareIo",false},{"normalAppStarted",false},{"physicalAcceptance",false},{"newProductDlls",json::array()}};
 std::ofstream out(argv[3]);out<<report.dump(2);return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
