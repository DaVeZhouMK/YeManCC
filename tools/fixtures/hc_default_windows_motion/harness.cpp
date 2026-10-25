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
#if YMCC_FIXTURE_PROFILE_CONTEXT
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
 for(const std::string group:{"identities","legacy-cases"}){const auto& cases=group=="identities"?oracle["identities"]:oracle["legacy"]["cases"];for(const auto& row:cases){candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();if(expected.contains(candidate::g_machineIdentity.deviceClass))rows.emplace(candidate::g_machineIdentity.deviceClass,row);}}
 txCheck(rows.size()==3,"all3 fixed-source default models found");
 for(const auto&[model,row]:rows){models.insert(model);
#if YMCC_FIXTURE_PROFILE_CONTEXT
#include "private_profile_cases.inc"
#include "windows_processing_cases.inc"
#endif
  seed(row,"winrt",false);
#if YMCC_FIXTURE_DEFAULT_WINDOWS_HELPER
  txCheck(candidate::inputCaptureDefaultHcMotionIdentity(candidate::g_machineIdentity)&&candidate::inputCaptureDefaultHcMotionSelectorBound(),"exact fixed-source default identity binds: "+model);
  txCheck(candidate::inputCaptureHcWindowsLegacyNeeded(candidate::g_machineIdentity,false,false)&&candidate::inputCaptureHcWindowsLegacyNeeded(candidate::g_machineIdentity,true,false)&&!candidate::inputCaptureHcWindowsLegacyNeeded(candidate::g_machineIdentity,true,true),"default model reuses standard SDK fallback only for incomplete WinRT pair");
  for(const std::string bad:{"manufacturer","product-suffix","class","family","canonical-matrix","unresolved"}){
    auto changed=candidate::g_machineIdentity;if(bad=="manufacturer")changed.manufacturer="OTHER";if(bad=="product-suffix")changed.product+="-OTHER";if(bad=="class")changed.deviceClass="HandheldCompanion.Devices.DefaultDevice";if(bad=="family")changed.family=candidate::YmccFamily::Unknown;if(bad=="canonical-matrix")changed.gyroMatrixId="forged";if(bad=="unresolved")changed.resolved=false;
    candidate::HcMotionSelectorReceipt receipt;txCheck(!candidate::inputCaptureDefaultHcMotionIdentity(changed)&&!candidate::inputCaptureResolveDefaultHcMotionSelector(changed,receipt)&&!receipt.resolved,"default identity must not broaden: "+bad);
  }
  for(const std::string changed:{"manufacturer","product","system-product","processor","system-version","family","class","selector","gyro-matrix","accel-matrix","source-sha"}){
    seed(row,"sensorapi",false);auto& r=candidate::g_hcMotionSelector;
    if(changed=="manufacturer")r.dmiManufacturer+="changed";if(changed=="product")r.dmiProduct+="changed";if(changed=="system-product")r.dmiSystemProduct+="changed";if(changed=="processor")r.dmiProcessor+="changed";if(changed=="system-version")r.dmiSystemVersion+="changed";if(changed=="family")r.nativeFamily=-1;if(changed=="class")r.deviceClass="unknown";if(changed=="selector")r.selectorId="changed";if(changed=="gyro-matrix")r.gyroMatrixId="changed";if(changed=="accel-matrix")r.accelMatrixId="changed";if(changed=="source-sha")r.matrixSourceSha256="changed";
    txCheck(!candidate::inputCaptureDefaultHcMotionSelectorBound()&&!candidate::inputCaptureMotionSelectorBound(),"default binding rejects stale receipt: "+changed);candidate::g_gyroCalibRequested=true;const auto f=candidate::fixtureWindowsFrame(owner);txCheck(!f.calibration&&!f.processed&&!result().value("saved",false),"stale default receipt cannot reach training or file");
  }
#else
  txCheck(false,"exact default selector and standard-source route missing: "+model);
#endif
  seed(row,"winrt",false);txCheck(candidate::g_machineIdentity.gyroMatrixId.empty()&&candidate::g_machineIdentity.accelMatrixId.empty(),"default adapter does not rewrite canonical machine matrix identity");
  candidate::g_sensorMeta.gyroPresent=candidate::g_sensorMeta.accelPresent=false;const auto noSensors=candidate::inputCaptureMotionCapabilityReceipt(candidate::g_sensorMeta,false);txCheck(noSensors["modelSelectorImplemented"]==true&&noSensors["observedWindowsGyro"]==false&&noSensors["observedWindowsAccel"]==false&&noSensors["virtualOutputProven"]==false,"default transform declaration never proves a sensor exists");
  txCheck(noSensors.value("matrixProvenance",std::string{})=="hc-default-unit-axes-no-device-resource"&&noSensors["modelSpecificAxesVerified"]==false,"capability receipt preserves default source and physical-axis uncertainty");
  seed(row,"winrt",true);candidate::g_gyroCalibRequested=true;const std::array<double,6> independent{2,-3,5,11,13,-17};candidate::fixtureWindowsFrame(owner,73,73,false,true,true,independent);bool exact=true;for(int i=0;i<6;++i)exact=exact&&std::abs(candidate::fixtureProcessedAxes[i]-independent[i])<1e-5;txCheck(exact&&candidate::fixtureProcessCount==1,"actual fixed HC default numeric callbacks preserve each signed axis independently");
  for(const std::string provider:{"winrt","sensorapi"})for(bool target:{false,true}){
   const auto key=seed(row,provider,target);candidate::g_gyroCalibRequested=true;candidate::FixtureFrame frame;
   for(int i=0;i<4;++i){frame=candidate::fixtureWindowsFrame(owner);txCheck(frame.processed&&frame.calibration,"no-live manual frame must reach real ProcessMotion: "+model);txCheck(!frame.physical&&!frame.output&&!frame.outputStamp,"calibration cannot admit physical or virtual motion: "+model);txCheck(frame.publication==target,"virtual publication intent remains independent");}
   const auto done=result();txCheck(done.value("saved",false)&&done.value("applied",false)&&done.value("sourceCurrent",false),"no-live manual transaction committed: "+model);
   txCheck(read().contains(key)&&read()["keep"]["unrelated"]==true,"qualified record persisted without losing unrelated data");txCheck(candidate::fixtureProcessCount==4,"only actual frame path produced training ticks");++scenarios;
   seed(row,provider,target);candidate::g_realStickTest.autoCalibrate=true;candidate::fixtureWindowsFrame(owner);candidate::fixtureTick+=candidate::kAutoCalibStillMs+1;candidate::fixtureWindowsFrame(owner);
   txCheck(candidate::g_autoCalibAttempts==1&&candidate::g_gyroCalibStageAtomic==candidate::GyroCalibStage::EnterFusion,"no-live automatic scheduler starts after unchanged dwell: "+model);
   for(int i=0;i<3;++i){const auto f=candidate::fixtureWindowsFrame(owner);txCheck(!f.physical&&!f.output&&!f.outputStamp,"auto-only frames cannot admit output");}
   txCheck(result().value("saved",false)&&result().value("applied",false),"no-live automatic transaction saved/applied: "+model);++scenarios;
  }
  seed(row,"winrt",false);const auto idle=candidate::fixtureWindowsFrame(owner);txCheck(!idle.processed&&!idle.calibration&&!idle.physical&&!idle.output,"no calibration requested cannot fabricate a no-live processing source");++scenarios;
  seed(row,"winrt",false);const auto physical=candidate::fixtureWindowsFrame(owner,73,73,true);txCheck(physical.processed&&physical.physical&&physical.output&&physical.outputStamp&&!physical.publication,"physical source still processes with virtual-off");++scenarios;
  seed(row,"winrt",true);candidate::g_gyroCalibRequested=true;const std::array<double,6> raw{2,-3,5,11,13,-17};const auto mapped=candidate::mapped(raw);const auto axes=candidate::fixtureWindowsFrame(owner,73,73,false,true,true,raw);bool same=true;for(int i=0;i<6;++i)same=same&&std::abs(candidate::fixtureProcessedAxes[i]-mapped[i])<1e-5;txCheck(axes.processed&&same,"no-live path feeds selected matrix axes into real library");++scenarios;
  seed(row,"winrt",false);candidate::g_gyroCalibRequested=true;const auto slowAccel=candidate::fixtureWindowsFrame(owner,73,73,false,true,true,{0,0,0,0,0,1},1000);txCheck(slowAccel.processed,"same-current-pair is not rejected by a new universal callback age cutoff");++scenarios;
  for(const std::string why:{"zero-generation","mixed-generation","retired-generation","foreign-provider","mixed-family","unknown-family","missing-gyro","missing-accel","invalid-number","unbound-selector","runtime-off","owner-aba"}){
   seed(row,"winrt",false);candidate::g_gyroCalibRequested=true;std::uint64_t gg=73,ag=73;bool gyro=true,accel=true;std::array<double,6> raw{0,0,0,0,0,1};auto sampled=owner;
   if(why=="zero-generation")gg=ag=0;if(why=="mixed-generation")ag=72;if(why=="retired-generation")candidate::mockSink.activeBindingGeneration=74;
   if(why=="foreign-provider")candidate::g_sensorMeta.accelDeviceId="ACPI#OTHER#1#{ACCEL}";
   if(why=="mixed-family")candidate::g_sensorMeta.accelFamily="sensorapi";if(why=="unknown-family")candidate::g_sensorMeta.gyroFamily=candidate::g_sensorMeta.accelFamily="arbitrary";
   if(why=="missing-gyro")gyro=candidate::g_sensorMeta.gyroPresent=false;if(why=="missing-accel")accel=candidate::g_sensorMeta.accelPresent=false;
   if(why=="invalid-number")raw[0]=std::numeric_limits<double>::quiet_NaN();if(why=="unbound-selector")candidate::g_hcMotionSelector.matrixSourceSha256="changed";if(why=="runtime-off")candidate::g_realStickTest.enabled=false;
   if(why=="owner-aba")sampled=std::make_shared<candidate::MockBindingSink>();const auto f=candidate::fixtureWindowsFrame(sampled,gg,ag,false,gyro,accel,raw);
   txCheck(!f.calibration&&!f.processed&&!f.physical&&!f.output&&!f.outputStamp,"unproven sensor-only source blocked: "+why+" "+model);txCheck(!result().value("saved",false),"invalid source cannot persist calibration");++scenarios;
  }
  for(const std::string why:{"binding","owner-aba","power","identity"}){
   seed(row,"sensorapi",false);candidate::g_gyroCalibRequested=true;candidate::fixtureWindowsFrame(owner);candidate::fixtureWindowsFrame(owner);const auto previous=read();candidate::MockBindingSink replacement;
   candidate::fixtureAfterProcess=[&](){if(why=="binding")candidate::mockSink.activeBindingGeneration=74;if(why=="owner-aba")candidate::g_inputCaptureSink=&replacement;if(why=="power")candidate::mockCapabilityPower=8;if(why=="identity")candidate::g_sensorMeta.gyroDeviceId="changed-source";};candidate::fixtureWindowsFrame(owner);
   txCheck(!result().value("saved",false)&&!result().value("applied",false)&&read()==previous,"sample retires after processing before BUS commit: "+why);candidate::g_inputCaptureSink=&candidate::mockSink;++scenarios;
  }
 }
 std::set<std::string> excluded;
 for(const std::string group:{"identities","legacy-cases"}){const auto& cases=group=="identities"?oracle["identities"]:oracle["legacy"]["cases"];for(const auto& row:cases){candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();const auto name=candidate::g_machineIdentity.deviceClass;if(name.empty()||expected.contains(name)||!excluded.insert(name).second)continue;
#if YMCC_FIXTURE_DEFAULT_WINDOWS_HELPER
  candidate::HcMotionSelectorReceipt receipt;txCheck(!candidate::inputCaptureDefaultHcMotionIdentity(candidate::g_machineIdentity)&&!candidate::inputCaptureResolveDefaultHcMotionSelector(candidate::g_machineIdentity,receipt),"default source route excludes existing canonical model: "+name);
#endif
 }}

 if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);
 json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"groups",groups},{"models",models},{"scenarios",scenarios},{"excludedCatalogClasses",excluded},{"actualAdmissionAndProcessStatements",true},{"actualBusCall",true},{"actualTick",true},{"actualGamepadMotion",true},{"actualFileApis",true},{"confidence","controlled; calibration convergence not tested"},{"hardwareIo",false},{"normalAppStarted",false},{"realCalibrationFileTouched",false},{"HostExecuted",false},{"physicalAcceptance",false}};
 std::ofstream out(argv[3]);out<<report.dump(2);return failures.empty()?0:1;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}}
