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
  candidate::fixtureConfidence=1;candidate::fixtureTick=1000;candidate::fixtureFailFlush=false;candidate::fixtureRetireOnFlush=false;candidate::fixtureAfterMove={};candidate::fixtureResubscribe=0;candidate::fixtureBeforeLoad={};candidate::fixtureAfterLoad={};candidate::fixtureBeforeFinish={};
  const auto key=candidate::inputCaptureCalibrationStorageKey(candidate::g_sensorMeta.gyroDeviceId);const auto* recipe=candidate::inputCaptureSourceQualifiedCalibrationRecipe();if(!recipe)throw std::runtime_error("Fixture requires reviewed model");
  writeText(json{{"keep",{{"unrelated",true}}},{key,{{"xOffset",1.0},{"yOffset",2.0},{"zOffset",3.0},{"weight",10},{"confidence",1.0},{"thresholdG",2000.0},{"thresholdA",4.0},{"matrixId",recipe->gyroMatrixId},{"matrixSourceSha256",recipe->sourceSha256},{"deviceClass",recipe->deviceClass}}}}.dump(2));
  if(candidate::inputCaptureExtendedCalibrationRecipe()){auto data=read();data[key]["sourceIdentity"]=candidate::inputCaptureExtendedCalibrationSourceIdentity(candidate::g_sensorMeta.gyroDeviceId);writeText(data.dump(2));}
  candidate::inputCaptureGyroCalibrationRestoreOnce();candidate::fixtureEvents.clear();candidate::fixtureUiEvents.clear();return key;
 };
 const auto frame=[&](bool injectCandidate=false,std::uint64_t generation=73,bool processed=true){
  candidate::fixtureTick+=8;candidate::g_gmProcessedThisTick=false;
  if(processed){const auto result=candidate::probeActive(true,true,true,"same-provider","same-provider",generation,generation);if(!result.admitted)throw std::runtime_error("Fixture real ProcessMotion not admitted");}
  if(injectCandidate)set(candidate::g_gm,0.25f,-0.5f,0.75f,10);
  const auto meta=candidate::g_sensorMeta;candidate::fixtureBusCalibration(owner,generation,meta,processed);
 };
 const auto stillFrame=[&](){candidate::fixtureTick+=8;candidate::g_gmProcessedThisTick=false;const auto meta=candidate::g_sensorMeta;const auto result=candidate::probeActive(true,true,true,meta.gyroFamily,meta.accelFamily,73,73,{0,0,0,0,0,1});if(!result.admitted)throw std::runtime_error("Still source not admitted");candidate::fixtureBusCalibration(owner,73,meta,true);};
 const auto startAutomatic=[&](){candidate::g_realStickTest.autoCalibrate=true;candidate::g_autoCalibTrusted=false;candidate::g_autoCalibFinished=false;set(candidate::g_gm,0,0,0,0);candidate::inputCaptureReviewedCalibrationNoteApplied(candidate::g_sensorMeta.gyroDeviceId,0);stillFrame();candidate::fixtureTick+=candidate::kAutoCalibStillMs+1;stillFrame();txCheck(candidate::g_gyroCalibStageAtomic==candidate::GyroCalibStage::EnterFusion&&candidate::g_autoCalibAttempts==1,"actual automatic scheduler starts once after the unchanged dwell");};
 const auto toApply=[&](){candidate::g_gyroCalibRequested=true;frame();txCheck(candidate::fixtureStatus==2,"validated start reports calibrating");frame();frame(true);txCheck(candidate::g_gyroCalibStageAtomic.load()==candidate::GyroCalibStage::ApplyOffset,"actual state machine reaches ApplyOffset");};
 const auto lastResult=[&](){for(auto it=candidate::fixtureEvents.rbegin();it!=candidate::fixtureEvents.rend();++it)if((*it)["event"]=="gyro-calibration-result")return (*it)["body"];return json::object(); /* Missing completion is an assertion failure, including in negative controls. */};
 std::map<std::string,json> targetRows;std::set<std::string> expectedModels=oracle["expectedSourceQualifiedModels"].get<std::set<std::string>>();
 for(const std::string group:{"identities","legacy-cases"}){const auto& cases=group=="identities"?oracle["identities"]:oracle["legacy"]["cases"];for(const auto& row:cases){candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();if(expectedModels.contains(candidate::g_machineIdentity.deviceClass)&&candidate::inputCaptureSourceQualifiedCalibrationRecipe()&&candidate::inputCaptureSourceQualifiedCalibrationBound())targetRows.emplace(candidate::g_machineIdentity.deviceClass,row);}}
 txCheck(targetRows.size()==expectedModels.size(),"all independent expected source-qualified classes found");
 unsigned extendedModels=0;
 for(const auto& [model,row]:targetRows){const json recipe={{"deviceClass",model}};
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
  key=seed(row);startAutomatic();stillFrame();set(candidate::g_gm,0.25f,-0.5f,0.75f,10);stillFrame();stillFrame();event=lastResult();
  txCheck(event["ok"]==true&&event["source"]=="auto"&&event["saved"]==true&&read()[key]["xOffset"]==0.25f,"automatic calibration commits through actual source and storage receipts");++scenarios;
  key=seed(row);startAutomatic();stillFrame();candidate::g_sensorMeta.accelDeviceId="auto-replacement-accel";candidate::inputCaptureGyroCalibrationRestoreOnce();stillFrame();event=lastResult();
  txCheck(event["ok"]==false&&event["source"]=="auto"&&event["reason"]=="source-retired"&&same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted,"automatic source interruption cannot retain bias/trust or overwrite replacement owner");++scenarios;
  if(candidate::inputCaptureExtendedCalibrationRecipe()){
   ++extendedModels;
   key=seed(row);const auto saved=read();const auto originalMeta=candidate::g_sensorMeta;
   const auto restore=[&](){candidate::inputCaptureGyroCalibrationRestoreOnce();};
   const auto loadCurrent=[&](){float x=0,y=0,z=0,c=0;int w=0;return candidate::inputCaptureLoadCalibration(candidate::g_sensorMeta.gyroDeviceId,x,y,z,w,c);};
   txCheck(key.starts_with("YMCC-HC-WINDOWS-SOURCE-V1|")&&saved[key].contains("sourceIdentity"),"extended record and key explicitly bind recipe, both sensors and provider");
   set(candidate::g_gm,4,5,6,10);candidate::g_autoCalibTrusted=true;candidate::g_autoCalibFinished=true;candidate::g_sensorMeta.gyroDeviceId="new-sdk-gyro";restore();
   txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted&&!candidate::g_autoCalibFinished&&bytes()==saved.dump(2),"reproduced 33-class new gyro source now clears old live bias and trust without touching file");
   candidate::g_sensorMeta=originalMeta;restore();txCheck(same(offset(),{1,2,3})&&candidate::g_autoCalibTrusted,"original full source restores its exact matching trusted record");
   candidate::g_sensorMeta.accelDeviceId="replacement-accel-only";restore();txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted,"same gyro with replacement accel cannot reuse previous source trust");
   candidate::g_sensorMeta=originalMeta;restore();txCheck(same(offset(),{1,2,3})&&candidate::g_autoCalibTrusted,"original pair can reacquire its saved calibration");
   candidate::g_sensorMeta.gyroFamily=candidate::g_sensorMeta.accelFamily="sensorapi";restore();txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted,"same IDs with another provider require their own record");
   candidate::inputCaptureStoreCalibration(candidate::g_sensorMeta.gyroDeviceId,0.5f,0.75f,1.25f,10,1);const auto sdkKey=candidate::inputCaptureCalibrationStorageKey(candidate::g_sensorMeta.gyroDeviceId);candidate::g_gmSessionStart+=std::chrono::nanoseconds(1);restore();txCheck(same(offset(),{0.5f,0.75f,1.25f})&&candidate::g_autoCalibTrusted&&read().contains(key)&&read().contains(sdkKey),"SDK and original-provider records coexist and restore separately");
   candidate::g_sensorMeta=originalMeta;restore();txCheck(same(offset(),{1,2,3})&&candidate::g_autoCalibTrusted,"return from SDK to original provider restores original record rather than SDK bias");
   auto changed=read();changed[key]["sourceIdentity"]["accelDeviceId"]="forged";writeText(changed.dump(2));txCheck(!loadCurrent(),"record-body source identity mismatch is rejected even at the correct key");
   changed=saved;changed[key].erase("sourceIdentity");writeText(changed.dump(2));txCheck(!loadCurrent(),"source-qualified records require explicit full source identity");
   for(float invalid:{-0.1f,1.1f}){changed=saved;changed[key]["confidence"]=invalid;writeText(changed.dump(2));txCheck(!loadCurrent(),"extended restore refuses out-of-range stored confidence");}
   writeText(saved.dump(2));candidate::g_sensorMeta.accelFamily="different-provider";txCheck(candidate::inputCaptureCalibrationStorageKey(originalMeta.gyroDeviceId).empty()&&!loadCurrent(),"mixed provider metadata cannot acquire calibration key");candidate::g_sensorMeta=originalMeta;
   writeText(saved.dump(2));candidate::g_sensorMeta=originalMeta;candidate::g_gmSessionStart+=std::chrono::nanoseconds(1);restore();candidate::g_sensorMeta.accelPresent=false;restore();
   txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted&&!candidate::g_autoCalibFinished,"incomplete pair retires live bias and old trusted flags before the missing half appears");
   candidate::g_sensorMeta=originalMeta;restore();txCheck(same(offset(),{1,2,3})&&candidate::g_autoCalibTrusted,"complete original source recovers trusted saved record after incomplete interval");
   candidate::g_sensorMeta.gyroPresent=false;restore();txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted,"missing gyro retires previous live calibration");candidate::g_sensorMeta=originalMeta;restore();
   candidate::g_sensorMeta.accelFamily="mixed-provider";restore();txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted,"mixed provider cannot leave old calibration marked current");candidate::g_sensorMeta=originalMeta;restore();
   const auto selector=candidate::g_hcMotionSelector;candidate::g_hcMotionSelector.matrixSourceSha256="invalid-source";restore();txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted,"unbound selector retires calibration trust");candidate::g_hcMotionSelector=selector;restore();
   key=seed(row);const auto stableMeta=candidate::g_sensorMeta;auto otherMeta=stableMeta;otherMeta.accelDeviceId="interleaved-source-accel";
   candidate::g_sensorMeta=otherMeta;candidate::inputCaptureStoreCalibration(otherMeta.gyroDeviceId,7,8,9,10,1);const auto otherKey=candidate::inputCaptureCalibrationStorageKey(otherMeta.gyroDeviceId);candidate::g_sensorMeta=stableMeta;
   candidate::g_gmSessionStart+=std::chrono::nanoseconds(1);candidate::fixtureBeforeLoad=[&](){candidate::g_sensorMeta=otherMeta;candidate::mockSink.activeBindingGeneration=74;};candidate::fixtureAfterLoad=[&](){candidate::g_sensorMeta=stableMeta;candidate::mockSink.activeBindingGeneration=75;};restore();
   txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted,"restore ABA during actual file load cannot apply a record from another source");restore();txCheck(same(offset(),{1,2,3})&&candidate::g_autoCalibTrusted,"fresh retry after restore ABA loads the original exact source record");
   candidate::g_gmSessionStart+=std::chrono::nanoseconds(1);candidate::fixtureAfterLoad=[&](){candidate::g_sensorMeta=otherMeta;candidate::mockSink.activeBindingGeneration=76;};restore();
   txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted,"retirement after actual file read cannot tag previous bias as the replacement source");restore();txCheck(same(offset(),{7,8,9})&&candidate::g_autoCalibTrusted,"replacement source retry restores its own record rather than a mislabelled cache");
   key=seed(row);const auto finishMeta=candidate::g_sensorMeta;auto finishOther=finishMeta;finishOther.accelDeviceId="finish-new-owner-accel";candidate::g_sensorMeta=finishOther;candidate::inputCaptureStoreCalibration(finishOther.gyroDeviceId,7,8,9,10,1);const auto finishOtherKey=candidate::inputCaptureCalibrationStorageKey(finishOther.gyroDeviceId);candidate::g_sensorMeta=finishMeta;
   candidate::fixtureConfidence=0.995f;toApply();candidate::fixtureBeforeFinish=[&](){candidate::g_sensorMeta=finishOther;candidate::mockSink.activeBindingGeneration=74;candidate::inputCaptureGyroCalibrationRestoreOnce();};frame();event=lastResult();
   txCheck(event["saved"]==true&&event["ok"]==false&&event["sourceCurrent"]==false,"source retirement between caller check and finish cannot be reported accepted");
   txCheck(same(offset(),{7,8,9})&&candidate::g_reviewedCalibrationContext.owner.storageKey==finishOtherKey&&candidate::g_reviewedCalibrationContext.owner.weight==10,"finish cannot overwrite a replacement owner's key, offset or known weight");
   candidate::g_sensorMeta=originalMeta;candidate::mockSink.activeBindingGeneration=73;writeText(saved.dump(2));
   auto oldOnly=json{{candidate::inputCaptureCalibrationKeyUpper(originalMeta.gyroDeviceId),{{"xOffset",7},{"yOffset",8},{"zOffset",9},{"weight",10},{"confidence",1}}}};writeText(oldOnly.dump(2));candidate::g_gmSessionStart+=std::chrono::nanoseconds(1);set(candidate::g_gm,4,5,6,10);candidate::g_autoCalibTrusted=true;restore();txCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted&&read()==oldOnly,"device-only historical record preserved and never promoted into a new source frame");
  }
  models.push_back(recipe["deviceClass"]);
 }
 txCheck(models.size()==51&&extendedModels==33,"exact independent cohort: reviewed18 and extended33");
 for(auto protectedFamily:{candidate::YmccFamily::MsiClaw,candidate::YmccFamily::AsusRogAllyX,candidate::YmccFamily::AsusRogAlly,candidate::YmccFamily::AsusRogAllyClassic,candidate::YmccFamily::AsusRogAllyXClassic,candidate::YmccFamily::LenovoLegionGo,candidate::YmccFamily::Gpd,candidate::YmccFamily::Unknown}){
  candidate::g_machineIdentity.family=protectedFamily;
  txCheck(candidate::inputCaptureReviewedCalibrationRecipe()==nullptr&&candidate::inputCaptureCalibrationStorageKey("protected-device")=="PROTECTED-DEVICE","protected families retain legacy calibration key and cannot enter reviewed transaction");
 }
 destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);
 const json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"groups",groups},{"models",models},{"extendedModels",extendedModels},{"scenarios",scenarios},{"actualTick",true},{"actualBusCall",true},{"actualFileApis",true},{"actualGamepadMotion",true},{"confidence","controlled; calibration convergence not tested"},{"physicalAcceptance",false},{"hardwareIo",false},{"normalAppStarted",false},{"realCalibrationFileTouched",false}};
 std::ofstream(argv[3])<<report.dump(2)<<'\n';std::cout<<json({{"status",report["status"]},{"checks",checks},{"failures",failures.size()},{"scenarios",scenarios}}).dump()<<'\n';return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
