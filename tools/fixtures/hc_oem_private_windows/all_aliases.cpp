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


 std::set<std::string> expectedClasses;
 for(const auto& item:oracle["legacy"]["recipes"])if(!item["controllerPreferred"].get<bool>())expectedClasses.insert(item["deviceClass"].get<std::string>());
 for(const auto& item:oracle["models"])if(item.value("admitted",false))expectedClasses.insert(item["fullName"].get<std::string>());
 for(const auto& item:oracle["motionCapabilityPolicy"]["existingWindowsClasses"])expectedClasses.insert(item.get<std::string>());
 const std::set<std::string> defaults={"HandheldCompanion.Devices.GPDWin2","HandheldCompanion.Devices.GPDWin3","HandheldCompanion.Devices.SteamMachine"};expectedClasses.insert(defaults.begin(),defaults.end());
 txCheck(expectedClasses.size()==67,"independent full library declares 67 Windows source model classes");
 std::map<std::string,json> rows;
 const auto add=[&](json row,const std::string& expectedClass){if(!expectedClasses.contains(expectedClass))return;row["expectedFullClass"]=expectedClass;json dmi=json::object();for(const auto* field:{"manufacturer","product","systemProduct","processor","systemVersion"})dmi[field]=row.value(field,std::string{});auto [it,inserted]=rows.emplace(expectedClass+"|"+dmi.dump(),row);if(row.contains("expectedRecipe")&&!row["expectedRecipe"].is_null())it->second["expectedRecipe"]=row["expectedRecipe"];};
 // legacyIdentities contains compatibility probes, not promises that each target label resolves.
 // Positive HC cases carry actual source-reader identities; use each recipe canonical class.
 for(const auto& row:oracle["identities"])add(row,"HandheldCompanion.Devices."+row["deviceClass"].get<std::string>());
 for(const auto& row:oracle["legacy"]["cases"]){
  if(!row["expectedRecipe"].is_null()){const auto it=std::find_if(oracle["legacy"]["recipes"].begin(),oracle["legacy"]["recipes"].end(),[&](const json& item){return item["selectorId"]==row["expectedRecipe"];});if(it==oracle["legacy"]["recipes"].end())throw std::runtime_error("independent canonical recipe missing");add(row,(*it)["deviceClass"].get<std::string>());}
  else if(row.contains("hcClass"))add(row,row["hcClass"].get<std::string>());
 }
 const auto axesFrom=[&](const json& matrices){std::array<double,6> out{};for(unsigned k=0;k<2;++k){const auto sensor=k?"accel":"gyro";const auto found=std::find_if(matrices.begin(),matrices.end(),[&](const json& m){return m["sensor"]==sensor;});if(found==matrices.end())throw std::runtime_error("independent matrix missing");
   const std::array<double,3> raw=k?std::array<double,3>{0,0,1}:std::array<double,3>{1,2,3};std::array<double,3> axis{1,1,1};std::array<unsigned,3> swap{0,1,2};
   if(found->contains("axis")){axis=(*found)["axis"].get<std::array<double,3>>();swap=(*found)["swap"].get<std::array<unsigned,3>>();}
   else{const std::array<std::string,3> labels{"X","Y","Z"};for(unsigned i=0;i<3;++i){axis[i]=(*found)["matrix"]["Axis"][labels[i]].get<double>();const auto target=(*found)["matrix"]["AxisSwap"][labels[i]].get<std::string>();swap[i]=unsigned(std::find(labels.begin(),labels.end(),target)-labels.begin());if(swap[i]>2)throw std::runtime_error("independent axis destination");}}
   std::array<double,3> mapped{};for(unsigned i=0;i<3;++i)mapped[swap[i]]=raw[i];for(unsigned i=0;i<3;++i)out[k*3+i]=mapped[i]*axis[i];}return out;};
 const auto expectedAxes=[&](const json& row,const std::string& cls){
  if(oracle["oemPrivateRecipeOracle"]["models"].contains(cls))return oracle["oemPrivateRecipeOracle"]["models"][cls]["sample"].get<std::array<double,6>>();
  if(defaults.contains(cls))return std::array<double,6>{1,2,3,0,0,1};
  for(const auto& item:oracle["models"])if(item.value("admitted",false)&&item["fullName"]==cls)return axesFrom(item["matrices"]);
  for(const auto& item:oracle["reviewed"])if(item["deviceClass"]==cls)return axesFrom(item["matrices"]);
  unsigned classRecipes=0;const json* single=nullptr;for(const auto& item:oracle["legacy"]["recipes"])if(item["deviceClass"]==cls){++classRecipes;single=&item;}
  if(classRecipes==1)return axesFrom((*single)["matrices"]);
  if(row.contains("expectedRecipe"))for(const auto& item:oracle["legacy"]["recipes"])if(item["selectorId"]==row["expectedRecipe"])return axesFrom(item["matrices"]);
  before::setup(row);before::inputCaptureResolveHcMotionSelector();
  for(const auto& item:oracle["reviewed"])if(item["deviceClass"]==cls&&item["identityGyroMatrixId"]==before::g_machineIdentity.gyroMatrixId&&item["identityAccelMatrixId"]==before::g_machineIdentity.accelMatrixId)return axesFrom(item["matrices"]);
  for(const auto& item:oracle["legacy"]["recipes"])if(item["deviceClass"]==cls&&item["gyroMatrixId"]==before::g_machineIdentity.gyroMatrixId&&item["accelMatrixId"]==before::g_machineIdentity.accelMatrixId)return axesFrom(item["matrices"]);
  throw std::runtime_error("no independent matrix for alias "+cls+" "+row.dump());
 };
 std::set<std::string> recipes;json aliases=json::array();
 for(const auto& [caseId,row]:rows){const auto cls=row["expectedFullClass"].get<std::string>();models.insert(cls);const auto wanted=expectedAxes(row,cls);seed(row,"winrt",false);
  txCheck(candidate::g_machineIdentity.deviceClass==cls,"independent alias resolves to "+caseId);if(candidate::g_machineIdentity.deviceClass!=cls)continue;
  const auto meta=candidate::g_sensorMeta;const auto normalKey=candidate::inputCaptureCalibrationStorageKey(meta.gyroDeviceId);
  const auto capability=candidate::inputCaptureMotionCapabilityReceipt(meta,false),effective=candidate::inputCaptureCandidateMatrixReceipt(meta);
  txCheck(capability.value("sourceRoute",std::string{})=="windows-sensors"&&capability.value("modelSelectorBound",false),"full library alias has a bound Windows route: "+caseId);
  const auto profile=candidate::inputCaptureWindowsSelectionEvidence(json::object(),capability,effective,candidate::fixtureTick).model;
  txCheck(profile.valid(),"actual full library producer supplies a complete recipe: "+caseId);
  auto capture=std::make_shared<candidate::MockBindingSink>(),choice=std::make_shared<candidate::MockBindingSink>();auto alive=std::make_shared<std::atomic<bool>>(true);
  candidate::WindowsSensorView view;view.coherent=view.privateSource=true;view.metadata=meta;view.capture=ymcc::windows_motion::retain(capture,73);view.submission=ymcc::windows_motion::guarded(view.capture,ymcc::windows_motion::retain(choice,73),alive);
  view.captureRequestId=++sequence;view.choiceRequestId=view.captureRequestId+100;view.profile=ymcc::sensors::selection::fingerprint(profile);
  view.calibrationKey=ymcc::sensors::selection::calibrationKey(profile,{meta.gyroDeviceId,meta.accelDeviceId},ymcc::sensors::selection::TransformPolicy::ExplicitCurrentModel);
  for(unsigned k=0;k<2;++k){auto& part=view.parts[k];part.usable=true;part.axes=k?std::array<double,3>{0,0,1}:std::array<double,3>{1,2,3};part.generation=73;part.sequence=++sequence;part.receipt=candidate::fixtureTick;part.timestamp100ns=candidate::fixtureTick*10000;}
  bool adopted=false;{candidate::FixtureProcessingScope scope(view);adopted=candidate::inputCaptureWindowsProcessingAdopt(view);txCheck(adopted,"full library explicit source is adopted: "+caseId);
   if(adopted){txCheck(candidate::inputCaptureCalibrationStorageKey(meta.gyroDeviceId)==view.calibrationKey&&view.calibrationKey!=normalKey,"full library private/default keys stay separate: "+caseId);candidate::inputCaptureStoreCalibration(meta.gyroDeviceId,11,12,13,10,1);candidate::inputCaptureGyroCalibrationRestoreOnce();txCheck(offset()==std::array<float,3>{11,12,13},"full library private offset restores in actual DLL: "+caseId);}}
  if(adopted){
   auto badProfile=view;badProfile.profile+="-different-profile";
   {candidate::FixtureProcessingScope invalid(badProfile);
    txCheck(candidate::inputCapturePrivateCalibrationRequestedKey(badProfile,meta.gyroDeviceId).empty(),"explicit requested calibration rejects a different active profile: "+caseId);
    txCheck(candidate::inputCaptureCalibrationStorageKey(meta.gyroDeviceId).empty(),"actual storage key refuses a different active profile: "+caseId);
   }
   auto badKey=view;badKey.calibrationKey+="-different-transaction-key";
   {candidate::FixtureProcessingScope invalid(badKey);
    txCheck(candidate::inputCapturePrivateCalibrationRequestedKey(badKey,meta.gyroDeviceId).empty(),"explicit requested calibration rejects another transaction key: "+caseId);
   }
  }
  aliases.push_back({{"case",caseId},{"class",cls},{"adopted",adopted},{"gyroRecipe",profile.gyroMatrixId},{"accelRecipe",profile.accelMatrixId}});
  if(!adopted)continue;recipes.insert(view.profile);
  const auto frame=candidate::fixtureWindowsFrame(owner,73,73,false,true,true,{1,2,3,0,0,1},0,&view);bool same=true;for(unsigned i=0;i<6;++i)same=same&&std::abs(candidate::fixtureProcessedAxes[i]-wanted[i])<1e-5;
  txCheck(frame.processed&&same,"full library private processing consumes independently derived axes: "+caseId);
  txCheck(!frame.physical&&!frame.output&&!frame.outputStamp,"private full library sample does not impersonate physical pad: "+caseId);
  txCheck(candidate::fixtureOffsetAtProcess==std::array<float,3>{11,12,13}&&candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("currentSamplingAndProcessingLease",false),"full library actual processing produces the matching current receipt: "+caseId);
  const auto stamp=view.submission;auto cache=std::atomic_load(&candidate::g_windowsProcessingReceipt);
#if YMCC_FIXTURE_PUBLICATION_FENCE
  {const auto actualReceipt=cache?cache->read(candidate::fixtureTick,candidate::currentPowerGeneration()):std::nullopt;
   txCheck(actualReceipt.has_value()&&!ymcc::windows_motion::currentForOutput(actualReceipt->identity.source),"full library real processing receipt is separate from publication: "+caseId);
   if(actualReceipt){const auto grant=ymcc::windows_motion::PublicationFence::committed(actualReceipt->identity.source);auto fresh=view;
    fresh.submission=ymcc::windows_motion::forPublication(ymcc::windows_motion::guarded(view.capture,ymcc::windows_motion::retain(choice,73),alive),grant);
    const auto epoch=candidate::g_windowsProcessingOwner.identity.epoch;
    const auto allowed=candidate::fixtureWindowsFrame(owner,73,73,true,true,true,{1,2,3,0,0,1},0,&fresh);
    txCheck(allowed.processed&&allowed.physical&&allowed.output&&allowed.outputStamp&&candidate::g_windowsProcessingOwner.identity.epoch==epoch,"full library actual producer permits fresh committed motion without reinitializing GM: "+caseId);
    txCheck(candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("sourcePublicationAllowed",false),"full library receipt identifies current permitted processed input: "+caseId);
    grant->revoke();txCheck(!candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("sourcePublicationAllowed",true)&&candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("currentSamplingAndProcessingLease",false),"full library read-only receipt separates output revocation from source liveness: "+caseId);
    const auto withheld=candidate::fixtureWindowsFrame(owner,73,73,true,true,true,{1,2,3,0,0,1},0,&fresh);
    txCheck(withheld.processed&&withheld.physical&&!withheld.output&&!withheld.outputStamp,"full library withdrawal blocks contribution while actual processing continues: "+caseId);
   }
  }
#endif
  candidate::fixtureWindowsFrame(owner,73,73,true);
  txCheck(!ymcc::windows_motion::current(stamp)&&alive->load()&&cache&&!cache->read(candidate::fixtureTick,candidate::currentPowerGeneration()),"full library default return retires CPU receipts while native capture remains alive: "+caseId);
  txCheck(candidate::inputCaptureCalibrationStorageKey(meta.gyroDeviceId)==normalKey,"full library alias keeps its original default calibration key: "+caseId);++scenarios;
 }
 txCheck(models==expectedClasses,"every independently declared Windows class has actual alias coverage");
 candidate::inputCaptureWindowsProcessingRetire();if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);
 json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"groups",groups},{"models",models},{"scenarios",scenarios},{"aliases",aliases},{"distinctRecipeProfiles",recipes.size()},{"expectedWindowsClasses",expectedClasses},{"actualGamepadMotion",true},{"hardwareIo",false},{"normalAppStarted",false},{"physicalAcceptance",false},{"newProductDlls",json::array()}};
 std::ofstream out(argv[3]);out<<report.dump(2);return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
