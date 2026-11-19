// Actual calibration source/file functions plus actual pinned GamepadMotion ABI.
// The surrounding type/source setup is the previously compiled data-only consumer.
namespace candidate {
static auto g_gmSetOffset=static_cast<void(__cdecl *)(void*,float,float,float,int)>(nullptr);
static auto g_gmSetMode=static_cast<void(__cdecl *)(void*,int)>(nullptr);
static std::chrono::steady_clock::time_point g_gmSessionStart{};
static std::atomic<bool> g_autoCalibTrusted{false},g_autoCalibFinished{false};
static std::string g_calibRestoredDeviceId;
static int fixtureCalibStatus=-1;
static std::vector<json> fixtureCalibEvents;
static void inputCapturePostCalibStatus(int status){fixtureCalibStatus=status;}
static void appendNativeLifecycleLog(const char* name,const json& body){fixtureCalibEvents.push_back({{"name",name},{"body",body}});}
// @@CALIBRATION_CODE@@
}
static void recipeCalCheck(bool ok,const std::string& group,const std::string& label){check(ok,"recipe-calibration/"+group,label);}
int main(int argc,char** argv){
 try {
  if(argc!=4)return 2;
  json oracle;std::ifstream(argv[1])>>oracle;
  const auto module=LoadLibraryExW(std::filesystem::path(argv[2]).wstring().c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!module)throw std::runtime_error("Pinned DLL load failed");
  const auto symbol=[&](const char* name){auto p=GetProcAddress(module,name);if(!p)throw std::runtime_error(name);return p;};
  const auto create=reinterpret_cast<void*(__cdecl *)()>(symbol("CreateGamepadMotion"));
  const auto destroy=reinterpret_cast<void(__cdecl *)(void*)>(symbol("DeleteGamepadMotion"));
  const auto getOffset=reinterpret_cast<void(__cdecl *)(void*,float*,float*,float*)>(symbol("GetCalibrationOffset"));
  const auto getCal=reinterpret_cast<void(__cdecl *)(void*,float*,float*,float*)>(symbol("GetCalibratedGyro"));
  const auto process=reinterpret_cast<void(__cdecl *)(void*,float,float,float,float,float,float,float)>(symbol("ProcessMotion"));
  candidate::g_gmSetOffset=reinterpret_cast<decltype(candidate::g_gmSetOffset)>(symbol("SetCalibrationOffset"));
  candidate::g_gmSetMode=reinterpret_cast<decltype(candidate::g_gmSetMode)>(symbol("SetCalibrationMode"));
  const auto resetHandle=[&](){if(candidate::g_gm && candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);candidate::g_gm=create();if(!candidate::g_gm)throw std::runtime_error("Create failed");candidate::g_gmSessionStart+=std::chrono::nanoseconds(1);};
  const auto offset=[&](){std::array<float,3> out{};getOffset(candidate::g_gm,&out[0],&out[1],&out[2]);return out;};
  const auto same=[&](const std::array<float,3>& a,const std::array<float,3>& b){for(int i=0;i<3;i++)if(std::abs(a[i]-b[i])>1e-6f)return false;return true;};
  const std::string deviceId="fixture-sensor-path";
  const auto setupRow=[&](const json& row){candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();candidate::g_sensorMeta.gyroPresent=candidate::g_sensorMeta.accelPresent=true;candidate::g_sensorMeta.gyroDeviceId=deviceId;candidate::g_sensorMeta.accelDeviceId=deviceId+"-accel";candidate::g_sensorMeta.gyroFamily=candidate::g_sensorMeta.accelFamily="fixture-winrt";resetHandle();};
  const auto file=std::filesystem::path(candidate::inputCaptureCalibrationJsonPath());
  const auto expectedFile=std::filesystem::path(argv[3]).parent_path()/"isolated-appdata"/"YeManCC"/"calibration.json";
  if(std::filesystem::absolute(file).lexically_normal()!=std::filesystem::absolute(expectedFile).lexically_normal())throw std::runtime_error("Calibration fixture must stay inside its evidence directory");
  std::filesystem::create_directories(file.parent_path());
  const auto write=[&](const json& d){std::ofstream out(file);out<<d.dump(2);if(!out)throw std::runtime_error("Fixture file write failed");};
  const auto read=[&](){json d;std::ifstream in(file);in>>d;return d;};
  const auto load=[&](){float x=0,y=0,z=0,confidence=0;int weight=0;return candidate::inputCaptureLoadCalibration(deviceId,x,y,z,weight,confidence);};
  const json legacyRecord={{"xOffset",11.0},{"yOffset",13.0},{"zOffset",17.0},{"weight",10},{"confidence",1.0},{"thresholdG",2000.0},{"thresholdA",4.0}};
  std::set<std::string> expectedReviewedClasses,reviewedClasses,scopedKeys;json models=json::array();unsigned reviewed=0,preserved=0,expectedReviewed=0;
  for(const auto& recipe:oracle["legacy"]["recipes"])if(recipe.contains("selectedRecipe")){
   ++expectedReviewed;expectedReviewedClasses.insert(recipe["deviceClass"].get<std::string>());
  }
  recipeCalCheck(expectedReviewed>0&&expectedReviewedClasses.size()==expectedReviewed,"scope","oracle declares unique reviewed calibration models");
  recipeCalCheck(expectedReviewedClasses.contains("HandheldCompanion.Devices.GPDWin5"),"scope","oracle includes the reviewed Win5 calibration case");
  for(const auto& recipe:oracle["legacy"]["recipes"]){
   if(!recipe.contains("selectedRecipe"))continue;
   const auto it=std::find_if(oracle["legacy"]["cases"].begin(),oracle["legacy"]["cases"].end(),[&](const json& r){return r["expectedRecipe"]==recipe["selectorId"];});
   if(it==oracle["legacy"]["cases"].end())throw std::runtime_error("Reviewed identity missing");
   const std::string label=recipe["deviceClass"].get<std::string>();setupRow(*it);const auto selected=candidate::g_hcMotionSelector;reviewedClasses.insert(label);
   const bool win5=label=="HandheldCompanion.Devices.GPDWin5";
   if(win5){
    recipeCalCheck(selected.selectorId=="hc-fdf0bf66:reviewed:GPDWin5"&&selected.gyroMatrixId=="hc-fdf0bf66:gpd-win5:gyro:y-neg-z-neg-x"&&
     selected.accelMatrixId=="hc-06c0b954:gpd-win5:accel:neg-x-neg-z-y"&&selected.matrixSourceSha256=="d0524d599683d07b67fb191b77a9f1efef6ee7068849d2dd618ba6cb650e109f","source",label+": reviewed historical callback identity");
    recipeCalCheck(candidate::inputCaptureReviewedCalibrationRecipe()==ymcc::hc_model_motion::find(label)&&candidate::inputCaptureNewHcMotionSelectorBound(),"source",label+": default calibration owns the bound reviewed recipe");
   }
   recipeCalCheck(candidate::inputCaptureMotionSelectorBound(),"source",label);
   const auto oldKey=candidate::inputCaptureCalibrationKeyUpper(deviceId),key=candidate::inputCaptureCalibrationStorageKey(deviceId);scopedKeys.insert(key);
   recipeCalCheck(key!=oldKey&&!key.empty(),"key",label+": new source-qualified key");
   const json oldRoot={{oldKey,legacyRecord}};write(oldRoot);
   recipeCalCheck(!load(),"legacy-record",label+": unqualified record rejected");
   candidate::g_gmSetOffset(candidate::g_gm,31,-17,9,10);candidate::g_autoCalibTrusted.store(true);candidate::g_autoCalibFinished.store(true);
   candidate::inputCaptureGyroCalibrationRestoreOnce();
   recipeCalCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted.load()&&!candidate::g_autoCalibFinished.load(),"cold-restore",label+": wrong-frame offsets cleared in actual DLL");
   recipeCalCheck(read()==oldRoot,"legacy-record",label+": old record untouched");
   if(win5)recipeCalCheck(candidate::fixtureCalibStatus==0&&!read().contains(key),"key",label+": bare old bias stays uncalibrated without an inferred migration");
   candidate::inputCaptureStoreCalibration(deviceId,1.25f,-2.5f,3.75f,10,1.0f);const auto saved=read();
   recipeCalCheck(saved.size()==2&&saved[oldKey]==legacyRecord&&saved.contains(key),"save",label+": qualified save preserves legacy record");
   const auto& record=saved[key];const auto& expected=recipe["selectedRecipe"];
   recipeCalCheck(record["matrixId"]==expected["gyroMatrixId"]&&record["matrixSourceSha256"]==expected["sourceSha256"]&&record["deviceClass"]==expected["deviceClass"],"save",label+": stored source metadata");
   recipeCalCheck(load(),"load",label+": source-matched record accepted");
   if(win5){
    // Exercise the existing explicit-source key/load/store/restore bodies in
    // this same calibration consumer; the native capture remains controlled.
    const auto* reviewedRecipe=candidate::inputCaptureReviewedCalibrationRecipe();
    candidate::WindowsSensorView privateView;privateView.coherent=privateView.privateSource=privateView.processingOwnerAccepted=true;
    privateView.metadata=candidate::g_sensorMeta;
    privateView.metadata.gyroDeviceId="ACPI#WIN5-CALIBRATION#1#{GYRO}";privateView.metadata.accelDeviceId="ACPI#WIN5-CALIBRATION#1#{ACCEL}";
    privateView.metadata.gyroFamily=privateView.metadata.accelFamily="winrt";
    auto capture=std::make_shared<candidate::MockBindingSink>(),choice=std::make_shared<candidate::MockBindingSink>();
    auto alive=std::make_shared<std::atomic<bool>>(true),processing=std::make_shared<std::atomic<bool>>(true);
    privateView.submission=ymcc::windows_motion::forProcessing(ymcc::windows_motion::guarded(
     ymcc::windows_motion::retain(capture,73),ymcc::windows_motion::retain(choice,73),alive),processing);
    const ymcc::sensors::selection::ModelRecipe privateModel{label,selected.gyroMatrixId,selected.accelMatrixId,selected.matrixSourceSha256,selected.matrixSourceSha256,true,true};
    privateView.profile=ymcc::sensors::selection::fingerprint(privateModel);
    privateView.calibrationKey=ymcc::sensors::selection::calibrationKey(privateModel,{privateView.metadata.gyroDeviceId,privateView.metadata.accelDeviceId},ymcc::sensors::selection::TransformPolicy::ExplicitCurrentModel);
    const auto& privateId=privateView.metadata.gyroDeviceId;
    const auto privateLoad=[&](){float x=0,y=0,z=0,c=0;int w=0;return candidate::inputCaptureLoadCalibration(privateId,x,y,z,w,c);};
    {
     const ymcc::sensors::view::ProcessingScope<candidate::InputCaptureSensorMeta> scope(privateView,candidate::t_windowsProcessingMeta,candidate::t_windowsProcessingStamp,&candidate::t_windowsProcessingView);
     recipeCalCheck(candidate::inputCaptureSourceQualifiedCalibrationRecipe()==reviewedRecipe&&candidate::inputCaptureNewHcMotionSelectorBound()&&candidate::inputCaptureSourceQualifiedCalibrationBound(),"source",label+": explicit source shares the bound reviewed recipe");
     const auto privateKey=candidate::inputCaptureCalibrationStorageKey(privateId),bareKey=candidate::inputCaptureCalibrationKeyUpper(privateId);
     recipeCalCheck(!privateKey.empty()&&privateKey==privateView.calibrationKey&&privateKey!=key&&privateKey!=bareKey,"key",label+": explicit and default keys remain separate");
     auto bareRoot=saved;bareRoot[bareKey]=legacyRecord;write(bareRoot);
     recipeCalCheck(!privateLoad(),"legacy-record",label+": explicit source rejects unqualified old bias");
     candidate::g_gmSetOffset(candidate::g_gm,31,-17,9,10);candidate::g_autoCalibTrusted=true;candidate::g_autoCalibFinished=true;
     candidate::inputCaptureGyroCalibrationRestoreOnce();
     recipeCalCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted.load()&&!candidate::g_autoCalibFinished.load()&&candidate::fixtureCalibStatus==0,"cold-restore",label+": explicit old bias cannot remain active or trusted");
     recipeCalCheck(read()==bareRoot,"legacy-record",label+": explicit restore neither migrates nor rewrites bare bias");
     recipeCalCheck(candidate::inputCaptureStoreCalibration(privateId,-4,5,-6,10,1),"save",label+": explicit current recipe calibration saves");
     const auto privateSaved=read();
     recipeCalCheck(privateSaved[key]==saved[key]&&privateSaved[bareKey]==legacyRecord&&privateSaved[privateKey]["matrixId"]==selected.gyroMatrixId&&
      privateSaved[privateKey]["sourceIdentity"]["profile"]==privateView.profile,"save",label+": explicit save retains default and bare records with current provenance");
     resetHandle();candidate::g_gmSetOffset(candidate::g_gm,99,99,99,10);candidate::inputCaptureGyroCalibrationRestoreOnce();
     recipeCalCheck(privateLoad()&&same(offset(),{-4,5,-6})&&candidate::g_autoCalibTrusted.load()&&candidate::g_autoCalibFinished.load(),"restore",label+": explicit reviewed offset restores in a new actual DLL session");
     auto wrongAxes=privateSaved;wrongAxes[privateKey]["matrixId"]="hc-06c0b954:gpd-win5:gyro:z-neg-x-neg-y";write(wrongAxes);
     recipeCalCheck(!privateLoad(),"tampered-source",label+": explicit key rejects the superseded matrix identity");write(privateSaved);
     for(const std::string why:{"profile","key","processing-lease","capture-lease"}){
      auto bad=privateView;if(why=="profile")bad.profile+="-old";if(why=="key")bad.calibrationKey=key;
      if(why=="processing-lease")processing->store(false);if(why=="capture-lease")alive->store(false);
      const ymcc::sensors::view::ProcessingScope<candidate::InputCaptureSensorMeta> badScope(bad,candidate::t_windowsProcessingMeta,candidate::t_windowsProcessingStamp,&candidate::t_windowsProcessingView);
      recipeCalCheck(candidate::inputCaptureCalibrationStorageKey(privateId).empty()&&!privateLoad(),"unbound",label+": explicit source rejects invalid "+why);
      candidate::inputCaptureStoreCalibration(privateId,99,99,99,10,1);recipeCalCheck(read()==privateSaved,"unbound",label+": invalid "+why+" cannot overwrite calibration");
      processing->store(true);alive->store(true);
     }
     candidate::g_hcMotionSelector.matrixSourceSha256="wrong";
     recipeCalCheck(!candidate::inputCaptureNewHcMotionSelectorBound()&&candidate::inputCaptureCalibrationStorageKey(privateId).empty()&&!privateLoad(),"unbound",label+": explicit source cannot use an unbound reviewed digest");
     candidate::inputCaptureStoreCalibration(privateId,99,99,99,10,1);recipeCalCheck(read()==privateSaved,"unbound",label+": unbound reviewed digest cannot write");candidate::g_hcMotionSelector=selected;
    }
    candidate::inputCaptureGyroCalibrationRestoreOnce();
    recipeCalCheck(load()&&same(offset(),{1.25f,-2.5f,3.75f}),"restore",label+": returning from explicit source restores only the default reviewed offset");
    write(saved);
   }
   resetHandle();candidate::g_gmSetOffset(candidate::g_gm,99,99,99,10);candidate::inputCaptureGyroCalibrationRestoreOnce();
   recipeCalCheck(same(offset(),{1.25f,-2.5f,3.75f})&&candidate::g_autoCalibTrusted.load()&&candidate::g_autoCalibFinished.load(),"restore",label+": actual new DLL handle restored");
   process(candidate::g_gm,1.25f,-2.5f,3.75f,0,0,1,0.008f);std::array<float,3> calibrated{};getCal(candidate::g_gm,&calibrated[0],&calibrated[1],&calibrated[2]);
   recipeCalCheck(same(calibrated,{0,0,0}),"actual-process",label+": restored current-frame bias consumed");
   const auto eventCount=candidate::fixtureCalibEvents.size();candidate::g_gmSetOffset(candidate::g_gm,3,4,5,10);candidate::inputCaptureGyroCalibrationRestoreOnce();
   recipeCalCheck(same(offset(),{3,4,5})&&candidate::fixtureCalibEvents.size()==eventCount,"once",label+": no per-tick reapply");
   for(const std::string field:{"matrixId","matrixSourceSha256","deviceClass"}){auto bad=saved;bad[key][field]="wrong";write(bad);recipeCalCheck(!load(),"tampered-source",label+"/"+field);}
   for(const std::string field:{"xOffset","yOffset","zOffset","confidence","weight"}){auto bad=saved;bad[key][field]="malformed";write(bad);recipeCalCheck(!load(),"malformed-record",label+"/"+field);}
   for(const auto& value:std::array<json,3>{json(-1),json(std::uint64_t{2147483648}),json(std::numeric_limits<std::uint64_t>::max())}){auto bad=saved;bad[key]["weight"]=value;write(bad);recipeCalCheck(!load(),"malformed-weight",label);}
   write(saved);recipeCalCheck(candidate::inputCaptureCalibrationStorageKey("").empty(),"no-device",label);candidate::inputCaptureStoreCalibration("",9,9,9,10,1);recipeCalCheck(read()==saved,"no-device",label+": no anonymous record");
   write(saved);candidate::g_hcMotionSelector.matrixSourceSha256="wrong";
   recipeCalCheck(!load(),"unbound",label+": load denied");candidate::inputCaptureStoreCalibration(deviceId,9,9,9,10,1.0f);
   recipeCalCheck(read()==saved,"unbound",label+": no write when source unbound");candidate::g_hcMotionSelector=selected;
   candidate::g_sensorMeta.gyroDeviceId=deviceId+"-new";candidate::inputCaptureGyroCalibrationRestoreOnce();
   recipeCalCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted.load(),"device-change",label+": no old-handle bias for new device");
   models.push_back({{"deviceClass",label},{"key",key},{"physicalAcceptance",false}});++reviewed;
  }
  recipeCalCheck(reviewed==expectedReviewed&&reviewedClasses==expectedReviewedClasses&&scopedKeys.size()==expectedReviewed,"scope","exact oracle model set has independently keyed reviewed calibrations");
  for(const auto& row:oracle["legacy"]["cases"]){
   setupRow(row);if(candidate::inputCaptureReviewedCalibrationRecipe())continue;
   if(candidate::inputCaptureExtendedCalibrationRecipe()){
    const auto oldKey=candidate::inputCaptureCalibrationKeyUpper(deviceId),key=candidate::inputCaptureCalibrationStorageKey(deviceId);write({{oldKey,legacyRecord}});
    recipeCalCheck(key.starts_with("YMCC-HC-WINDOWS-SOURCE-V1|")&&!load(),"extended","device-only legacy record not promoted to a source-qualified calibration");
    candidate::inputCaptureStoreCalibration(deviceId,1,2,3,10,1);const auto saved=read();
    recipeCalCheck(saved.contains(oldKey)&&saved.contains(key)&&saved[key].contains("sourceIdentity")&&load(),"extended","new record source identity persisted and unrelated legacy record preserved");
    continue;
   }
   const auto key=candidate::inputCaptureCalibrationKeyUpper(deviceId);write({{key,legacyRecord}});
   recipeCalCheck(candidate::inputCaptureCalibrationStorageKey(deviceId)==key&&load(),"preserved","unaffected storage/load");
   candidate::inputCaptureStoreCalibration(deviceId,1,2,3,10,1);const auto saved=read();
   recipeCalCheck(saved.size()==1&&saved[key].size()==7&&!saved[key].contains("matrixId"),"preserved","unaffected record schema");++preserved;
  }
  destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);
  json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"groups",groups},{"reviewedModels",reviewed},{"expectedReviewedModels",expectedReviewed},{"preservedIdentityCases",preserved},{"models",models},{"actualCalibrationFunctions",true},{"actualGamepadMotionDll",true},{"fileScope","child-only LOCALAPPDATA in evidence directory"},{"realCalibrationFileTouched",false},{"hardwareIo",false},{"deployed",false}};
  std::ofstream(argv[3])<<report.dump(2)<<'\n';std::cout<<json({{"status",report["status"]},{"checks",checks},{"failures",failures.size()}}).dump()<<'\n';return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
