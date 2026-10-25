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
  std::set<std::string> scopedKeys;json models=json::array();unsigned reviewed=0,preserved=0;
  for(const auto& recipe:oracle["legacy"]["recipes"]){
   if(!recipe.contains("selectedRecipe"))continue;
   const auto it=std::find_if(oracle["legacy"]["cases"].begin(),oracle["legacy"]["cases"].end(),[&](const json& r){return r["expectedRecipe"]==recipe["selectorId"];});
   if(it==oracle["legacy"]["cases"].end())throw std::runtime_error("Reviewed identity missing");
   const std::string label=recipe["deviceClass"].get<std::string>();setupRow(*it);const auto selected=candidate::g_hcMotionSelector;
   recipeCalCheck(candidate::inputCaptureMotionSelectorBound(),"source",label);
   const auto oldKey=candidate::inputCaptureCalibrationKeyUpper(deviceId),key=candidate::inputCaptureCalibrationStorageKey(deviceId);scopedKeys.insert(key);
   recipeCalCheck(key!=oldKey&&!key.empty(),"key",label+": new source-qualified key");
   const json oldRoot={{oldKey,legacyRecord}};write(oldRoot);
   recipeCalCheck(!load(),"legacy-record",label+": unqualified record rejected");
   candidate::g_gmSetOffset(candidate::g_gm,31,-17,9,10);candidate::g_autoCalibTrusted.store(true);candidate::g_autoCalibFinished.store(true);
   candidate::inputCaptureGyroCalibrationRestoreOnce();
   recipeCalCheck(same(offset(),{0,0,0})&&!candidate::g_autoCalibTrusted.load()&&!candidate::g_autoCalibFinished.load(),"cold-restore",label+": wrong-frame offsets cleared in actual DLL");
   recipeCalCheck(read()==oldRoot,"legacy-record",label+": old record untouched");
   candidate::inputCaptureStoreCalibration(deviceId,1.25f,-2.5f,3.75f,10,1.0f);const auto saved=read();
   recipeCalCheck(saved.size()==2&&saved[oldKey]==legacyRecord&&saved.contains(key),"save",label+": qualified save preserves legacy record");
   const auto& record=saved[key];const auto& expected=recipe["selectedRecipe"];
   recipeCalCheck(record["matrixId"]==expected["gyroMatrixId"]&&record["matrixSourceSha256"]==expected["sourceSha256"]&&record["deviceClass"]==expected["deviceClass"],"save",label+": stored source metadata");
   recipeCalCheck(load(),"load",label+": source-matched record accepted");
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
  recipeCalCheck(reviewed==18&&scopedKeys.size()==18,"scope","exactly 18 independently keyed models");
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
  json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"groups",groups},{"reviewedModels",reviewed},{"preservedIdentityCases",preserved},{"models",models},{"actualCalibrationFunctions",true},{"actualGamepadMotionDll",true},{"fileScope","child-only LOCALAPPDATA in evidence directory"},{"realCalibrationFileTouched",false},{"hardwareIo",false},{"deployed",false}};
  std::ofstream(argv[3])<<report.dump(2)<<'\n';std::cout<<json({{"status",report["status"]},{"checks",checks},{"failures",failures.size()}}).dump()<<'\n';return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
