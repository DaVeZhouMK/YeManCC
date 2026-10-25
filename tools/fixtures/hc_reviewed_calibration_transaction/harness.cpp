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
// Retirement body is actual; no worker adoption or native enumeration occurs in this transaction fixture.
// @@PROCESSOR_GLOBALS@@
// @@CALIBRATION_DECLARATIONS@@
static ymcc::reviewed_calibration::Context g_reviewedCalibrationContext;
static std::vector<json> fixtureEvents,fixtureUiEvents;
static int fixtureStatus=-1,fixtureResubscribe=0;
static bool fixtureFailFlush=false,fixtureRetireOnFlush=false;
static std::function<void()> fixtureAfterMove;
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
static void fixtureBusCalibration(const std::shared_ptr<MockBindingSink>& sampleOwner,std::uint64_t sampleGeneration,const InputCaptureSensorMeta& meta,bool admitted){
    const bool controllerMotionRoute=false,hcMatrixAdmitted=admitted,calibrationPairProven=false;
    const auto windowsMotionSensorOwner=sampleOwner;const auto gyroBindingGeneration=sampleGeneration;const auto sensorMeta=meta;
    // Snapshot selection is controlled here; the real stamp handoff below is extracted verbatim.
    struct FixtureView {ymcc::windows_motion::Stamp submission;};
    const FixtureView windowsSensorView{ymcc::windows_motion::retain(sampleOwner,sampleGeneration)};
    // @@BUS_CALL@@
}
}
static void txCheck(bool ok,const std::string& label){check(ok,"reviewed-calibration-transaction",label);}
int main(int argc,char**argv){try{
 if(argc!=4)return 2;json oracle;std::ifstream(argv[1])>>oracle;
 const auto module=LoadLibraryExW(std::filesystem::path(argv[2]).wstring().c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("Pinned DLL load failed");
 const auto sym=[&](const char* name){auto p=GetProcAddress(module,name);if(!p)throw std::runtime_error(name);return p;};
 const auto create=reinterpret_cast<void*(__cdecl *)()>(sym("CreateGamepadMotion"));const auto destroy=reinterpret_cast<void(__cdecl *)(void*)>(sym("DeleteGamepadMotion"));
 const auto set=reinterpret_cast<void(__cdecl *)(void*,float,float,float,int)>(sym("SetCalibrationOffset"));
 candidate::g_gmSetOffset=set;candidate::g_gmSetMode=reinterpret_cast<decltype(candidate::g_gmSetMode)>(sym("SetCalibrationMode"));
 candidate::g_gmResetCal=reinterpret_cast<decltype(candidate::g_gmResetCal)>(sym("ResetContinuousCalibration"));candidate::g_gmGetOffset=reinterpret_cast<decltype(candidate::g_gmGetOffset)>(sym("GetCalibrationOffset"));
 candidate::g_gmResetMotionApi=reinterpret_cast<decltype(candidate::g_gmResetMotionApi)>(sym("ResetGamepadMotion"));
 candidate::g_gmGetCal=reinterpret_cast<decltype(candidate::g_gmGetCal)>(sym("GetCalibratedGyro"));candidate::g_gmProcess=reinterpret_cast<decltype(candidate::g_gmProcess)>(sym("ProcessMotion"));
 const auto file=std::filesystem::path(candidate::inputCaptureCalibrationJsonPath()),expectedFile=std::filesystem::path(argv[3]).parent_path()/"isolated-appdata"/"YeManCC"/"calibration.json";
 if(std::filesystem::absolute(file).lexically_normal()!=std::filesystem::absolute(expectedFile).lexically_normal())throw std::runtime_error("Fixture cannot access real calibration path");
 std::filesystem::create_directories(file.parent_path());
 const auto read=[&](){json d;std::ifstream in(file);in>>d;return d;};const auto bytes=[&](){std::ifstream in(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
 const auto noOwnedTemporary=[&](){for(const auto& item:std::filesystem::directory_iterator(file.parent_path())){const auto name=item.path().filename().string();if(name.starts_with("calibration.json.reviewed."))return false;}return true;};
 const auto writeText=[&](const std::string& text){std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<text;if(!out)throw std::runtime_error("Fixture file write failed");};
 const auto offset=[&](){std::array<float,3> r{};candidate::g_gmGetOffset(candidate::g_gm,&r[0],&r[1],&r[2]);return r;};
 const auto same=[&](std::array<float,3> a,std::array<float,3> b){for(int i=0;i<3;i++)if(std::abs(a[i]-b[i])>1e-5f)return false;return true;};
 std::shared_ptr<candidate::MockBindingSink> owner(&candidate::mockSink,[](auto*){});
 std::uint64_t sessionSequence=0,scenarios=0;json models=json::array();
 const auto seed=[&](const json& row){
  if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);
  candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();candidate::g_gm=create();if(!candidate::g_gm)throw std::runtime_error("Create failed");
  candidate::g_gmSessionStart=std::chrono::steady_clock::time_point(std::chrono::nanoseconds(++sessionSequence));candidate::mockCapabilityPower=7;
  candidate::g_sensorMeta.gyroPresent=candidate::g_sensorMeta.accelPresent=true;candidate::g_sensorMeta.gyroDeviceId="fixture-sensor-gyro";candidate::g_sensorMeta.accelDeviceId="fixture-sensor-accel";
  candidate::g_sensorMeta.gyroFamily=candidate::g_sensorMeta.accelFamily="fixture-winrt";
  candidate::g_realStickTest.autoCalibrate=false;candidate::g_reviewedCalibrationContext={};candidate::g_gyroCalibStageAtomic=candidate::GyroCalibStage::None;candidate::g_gyroCalibRequested=false;
  candidate::g_autoCalibTrusted=false;candidate::g_autoCalibFinished=false;candidate::g_autoCalibAttempts=0;candidate::g_autoCalibWindowStart=0;candidate::g_autoStillSince=0;candidate::g_autoStillLastSteadyTick=0;
  candidate::g_calibSourceAuto=false;candidate::g_gmProcessedThisTick=false;candidate::g_gmSessionResetRequested=false;candidate::g_gmGetConf=&candidate::fixtureGetConfidence;
  candidate::fixtureConfidence=1;candidate::fixtureTick=1000;candidate::fixtureFailFlush=false;candidate::fixtureRetireOnFlush=false;candidate::fixtureAfterMove={};candidate::fixtureResubscribe=0;
  const auto key=candidate::inputCaptureCalibrationStorageKey(candidate::g_sensorMeta.gyroDeviceId);const auto* recipe=candidate::inputCaptureReviewedCalibrationRecipe();if(!recipe)throw std::runtime_error("Fixture requires reviewed model");
  writeText(json{{"keep",{{"unrelated",true}}},{key,{{"xOffset",1.0},{"yOffset",2.0},{"zOffset",3.0},{"weight",10},{"confidence",1.0},{"thresholdG",2000.0},{"thresholdA",4.0},{"matrixId",recipe->gyroMatrixId},{"matrixSourceSha256",recipe->sourceSha256},{"deviceClass",recipe->deviceClass}}}}.dump(2));
  candidate::inputCaptureGyroCalibrationRestoreOnce();candidate::fixtureEvents.clear();candidate::fixtureUiEvents.clear();return key;
 };
 const auto frame=[&](bool injectCandidate=false,std::uint64_t generation=73,bool processed=true){
  candidate::fixtureTick+=8;candidate::g_gmProcessedThisTick=false;
  if(processed){const auto result=candidate::probeActive(true,true,true,"same-provider","same-provider",generation,generation);if(!result.admitted)throw std::runtime_error("Fixture real ProcessMotion not admitted");}
  if(injectCandidate)set(candidate::g_gm,0.25f,-0.5f,0.75f,10);
  const auto meta=candidate::g_sensorMeta;candidate::fixtureBusCalibration(owner,generation,meta,processed);
 };
 const auto toApply=[&](){candidate::g_gyroCalibRequested=true;frame();txCheck(candidate::fixtureStatus==2,"validated start reports calibrating");frame();frame(true);txCheck(candidate::g_gyroCalibStageAtomic.load()==candidate::GyroCalibStage::ApplyOffset,"actual state machine reaches ApplyOffset");};
 const auto lastResult=[&](){for(auto it=candidate::fixtureEvents.rbegin();it!=candidate::fixtureEvents.rend();++it)if((*it)["event"]=="gyro-calibration-result")return (*it)["body"];throw std::runtime_error("No result event");};
 for(const auto& recipe:oracle["legacy"]["recipes"]){if(!recipe.contains("selectedRecipe"))continue;const auto it=std::find_if(oracle["legacy"]["cases"].begin(),oracle["legacy"]["cases"].end(),[&](const json& r){return r["expectedRecipe"]==recipe["selectorId"];});if(it==oracle["legacy"]["cases"].end())throw std::runtime_error("Identity missing");const auto& row=*it;
  // Successful commit and current record reload through actual DLL and file calls.
  auto key=seed(row);const auto old=read();toApply();frame();auto event=lastResult();
  txCheck(event["ok"]==true&&event["saved"]==true&&event["applied"]==true&&event["storageReadbackVerified"]==true,"saved and applied reflect separate successful receipts");
  txCheck(same(offset(),{0.25f,-0.5f,0.75f})&&candidate::g_autoCalibTrusted&&candidate::g_autoCalibFinished,"actual DLL applies committed offset");txCheck(read()["keep"]==old["keep"]&&read()[key]["xOffset"]==0.25f,"other records retained and target verified");frame();txCheck(candidate::fixtureResubscribe==1,"current successful exit resubscribes once");++scenarios;
  // Actual sharing violation after complete temp write preserves the target and rolls back live bias.
  key=seed(row);toApply();const auto original=bytes();HANDLE held=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(held==INVALID_HANDLE_VALUE)throw std::runtime_error("Target lock failed");frame();CloseHandle(held);event=lastResult();
  txCheck(event["saved"]==false&&event["applied"]==false&&event["rolledBack"]==true&&event["storageReason"]=="replace-failed","sharing error is not reported saved");txCheck(bytes()==original&&same(offset(),{1,2,3}),"sharing failure keeps previous file and real bias");++scenarios;
  key=seed(row);toApply();const auto beforeFlush=bytes();candidate::fixtureFailFlush=true;frame();candidate::fixtureFailFlush=false;event=lastResult();txCheck(event["saved"]==false&&event["storageReason"]=="temporary-flush-failed"&&bytes()==beforeFlush&&same(offset(),{1,2,3}),"flush failure cannot replace original or apply bias");txCheck(noOwnedTemporary(),"failed flush closes handle before deleting owned temporary and lock");++scenarios;
  key=seed(row);toApply();writeText("{ incomplete");frame();event=lastResult();txCheck(event["saved"]==false&&event["storageReason"]=="existing-json-invalid"&&bytes()=="{ incomplete"&&same(offset(),{1,2,3}),"invalid existing file not overwritten");++scenarios;
  key=seed(row);toApply();const auto lockedBefore=bytes();const auto lockPath=file.wstring()+L".reviewed.lock";
  HANDLE sidecar=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);if(sidecar==INVALID_HANDLE_VALUE)throw std::runtime_error("Fixture sidecar lock failed");
  frame();CloseHandle(sidecar);event=lastResult();txCheck(event["saved"]==false&&event["storageReason"]=="writer-busy-or-lock-failed"&&bytes()==lockedBefore&&same(offset(),{1,2,3})&&noOwnedTemporary(),"exclusive sidecar rejects second writer without file or bias loss");++scenarios;
  // Source retirement inside real FlushFileBuffers must be noticed before the move.
  key=seed(row);toApply();const auto beforeRetire=bytes();candidate::fixtureRetireOnFlush=true;frame();event=lastResult();txCheck(event["saved"]==false&&event["sourceCurrent"]==false&&bytes()==beforeRetire&&same(offset(),{1,2,3}),"source retirement during flush rejects final replacement");++scenarios;
  // A committed file is reported truthfully even if source retirement prevents application.
  key=seed(row);toApply();candidate::fixtureAfterMove=[](){candidate::mockSink.activeBindingGeneration.store(74);};frame();event=lastResult();txCheck(event["saved"]==true&&event["applied"]==false&&event["sourceCurrent"]==false&&event["ok"]==false,"save and apply split when source retires after move");txCheck(read()[key]["xOffset"]==0.25f&&same(offset(),{1,2,3}),"qualified saved record retained while old live bias restored");++scenarios;
  // Low confidence must restore the actual prior state after ResetContinuousCalibration.
  key=seed(row);candidate::fixtureConfidence=0;candidate::g_gyroCalibRequested=true;frame();frame();set(candidate::g_gm,9,9,9,10);candidate::fixtureTick+=6000;const auto lowFile=bytes();frame();event=lastResult();txCheck(event["saved"]==false&&event["reason"]=="confidence-not-trusted"&&bytes()==lowFile&&same(offset(),{1,2,3}),"timeout rolls back partial training rather than leaving its bias active");++scenarios;
  for(const std::string failure:{"power","selector","processed","handle-session","device","counter-aba"}){
   key=seed(row);toApply();const auto unchanged=bytes();
   if(failure=="power")candidate::mockCapabilityPower=8;
   if(failure=="selector")candidate::g_hcMotionSelector.matrixSourceSha256="forged";
   if(failure=="handle-session"){candidate::g_gmSessionStart+=std::chrono::nanoseconds(1);candidate::inputCaptureGyroCalibrationRestoreOnce();set(candidate::g_gm,6,7,8,10);}
   if(failure=="device"){candidate::g_sensorMeta.gyroDeviceId="new-sensor";candidate::inputCaptureGyroCalibrationRestoreOnce();set(candidate::g_gm,6,7,8,10);}
   candidate::MockBindingSink other;other.activeBindingGeneration.store(73);if(failure=="counter-aba")candidate::g_inputCaptureSink=&other;
   // Avoid pretending to run a newly unbound producer: the actual call receives its prior admitted sample.
   candidate::g_gmProcessedThisTick=failure!="processed";const auto meta=candidate::g_sensorMeta;candidate::fixtureBusCalibration(owner,73,meta,true);event=lastResult();
   txCheck(event["saved"]==false&&event["applied"]==false&&bytes()==unchanged,"interrupted source cannot commit: "+failure);
   if(failure=="device"||failure=="handle-session")txCheck(same(offset(),{6,7,8}),"abort cannot overwrite a new source/processing owner");
   candidate::g_inputCaptureSink=&candidate::mockSink;++scenarios;
  }
  // A family change cannot send the reviewed in-flight stage into the legacy machine.
  key=seed(row);toApply();const auto beforeFamilyChange=bytes();candidate::g_machineIdentity.family=candidate::YmccFamily::Unknown;
  candidate::g_gmProcessedThisTick=true;{const auto meta=candidate::g_sensorMeta;candidate::fixtureBusCalibration(owner,73,meta,true);}event=lastResult();
  txCheck(event["saved"]==false&&event["reason"]=="source-retired"&&candidate::g_gyroCalibStageAtomic==candidate::GyroCalibStage::None&&bytes()==beforeFamilyChange&&same(offset(),{1,2,3}),"reviewed transaction abort remains reviewed when family becomes unreviewed");++scenarios;
  key=seed(row);toApply();frame();const auto resubscribeBefore=candidate::fixtureResubscribe;candidate::g_machineIdentity.family=candidate::YmccFamily::Unknown;
  candidate::g_gmProcessedThisTick=true;{const auto meta=candidate::g_sensorMeta;candidate::fixtureBusCalibration(owner,73,meta,true);}
  txCheck(candidate::fixtureResubscribe==resubscribeBefore&&!candidate::g_reviewedCalibrationContext.transaction.active,"completed source cannot resubscribe replacement family");++scenarios;
  key=seed(row);candidate::g_gyroCalibRequested=true;frame();candidate::g_calibSourceAuto=true;candidate::g_gyroCalibRequested=true;frame();frame(true);frame();event=lastResult();
  txCheck(event["ok"]==true&&event["source"]=="manual","duplicate request cannot change immutable origin or restart transaction");++scenarios;
  for(float invalidConfidence:{std::numeric_limits<float>::quiet_NaN(),-0.5f,1.5f}){
   key=seed(row);candidate::g_gyroCalibRequested=true;frame();frame();set(candidate::g_gm,8,8,8,10);candidate::fixtureConfidence=invalidConfidence;const auto beforeInvalid=bytes();frame();event=lastResult();
   txCheck(event["ok"]==false&&event["reason"]=="invalid-confidence"&&bytes()==beforeInvalid&&same(offset(),{1,2,3}),"invalid confidence cannot save, cast weight or leave learned bias active");++scenarios;
  }
  key=seed(row);candidate::g_realStickTest.autoCalibrate=true;candidate::g_autoCalibTrusted=false;candidate::g_autoCalibFinished=false;set(candidate::g_gm,0,0,0,0);
  for(int i=0;i<6;i++){candidate::g_gmProcessedThisTick=true;candidate::fixtureTick+=1000;const auto meta=candidate::g_sensorMeta;candidate::fixtureBusCalibration(owner,0,meta,true);}
  txCheck(candidate::g_autoCalibAttempts==0&&!candidate::g_reviewedCalibrationContext.transaction.active,"invalid source cannot accumulate stillness or consume automatic attempts");++scenarios;
  // Retired sample cannot acquire the newly active generation at the production call site.
  key=seed(row);candidate::g_gyroCalibRequested=true;candidate::g_gmProcessedThisTick=true;candidate::mockSink.activeBindingGeneration.store(74);const auto staleMeta=candidate::g_sensorMeta;candidate::fixtureBusCalibration(owner,73,staleMeta,true);
  txCheck(candidate::g_gyroCalibStageAtomic==candidate::GyroCalibStage::None&&!candidate::g_reviewedCalibrationContext.transaction.active&&same(offset(),{1,2,3}),"actual BUS caller uses sample generation, not latest counter");++scenarios;
  models.push_back(recipe["deviceClass"]);
 }
 txCheck(models.size()==18,"exact reviewed-only model count");
 for(auto protectedFamily:{candidate::YmccFamily::MsiClaw,candidate::YmccFamily::AsusRogAllyX,candidate::YmccFamily::AsusRogAlly,candidate::YmccFamily::AsusRogAllyClassic,candidate::YmccFamily::AsusRogAllyXClassic,candidate::YmccFamily::LenovoLegionGo,candidate::YmccFamily::Gpd,candidate::YmccFamily::Unknown}){
  candidate::g_machineIdentity.family=protectedFamily;
  txCheck(candidate::inputCaptureReviewedCalibrationRecipe()==nullptr&&candidate::inputCaptureCalibrationStorageKey("protected-device")=="PROTECTED-DEVICE","protected families retain legacy calibration key and cannot enter reviewed transaction");
 }
 destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);
 const json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"groups",groups},{"models",models},{"scenarios",scenarios},{"actualTick",true},{"actualBusCall",true},{"actualFileApis",true},{"actualGamepadMotion",true},{"confidence","controlled; calibration convergence not tested"},{"physicalAcceptance",false},{"hardwareIo",false},{"normalAppStarted",false},{"realCalibrationFileTouched",false}};
 std::ofstream(argv[3])<<report.dump(2)<<'\n';std::cout<<json({{"status",report["status"]},{"checks",checks},{"failures",failures.size()},{"scenarios",scenarios}}).dump()<<'\n';return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
