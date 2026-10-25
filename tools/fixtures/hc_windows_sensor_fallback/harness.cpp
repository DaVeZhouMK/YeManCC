namespace candidate {
static void(__cdecl *fixtureRealProcess)(void*,float,float,float,float,float,float,float)=nullptr;
static void fixtureProcessAndRecord(void* handle,float gx,float gy,float gz,float ax,float ay,float az,float dt){fixtureRealProcess(handle,gx,gy,gz,ax,ay,az,dt);recordMotionEffect(handle,gx,gy,gz,ax,ay,az,dt);}

static bool g_gyroSensor=false,g_accelSensor=false,mockStopRequested=false;
static UINT64 g_inputCaptureSensorGeneration=73;
static constexpr UINT32 kInputCaptureRequestedSensorIntervalMs=8;
static std::atomic<bool> g_inputCaptureSawRealGyro{false};
static bool inputCaptureSensorStopRequested(){return mockStopRequested;}
static std::string W2U(const std::wstring& value){const auto n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);return out;}
static std::mutex fixtureLogMx;static std::vector<json> fixtureLogs;
static void appendDomainLog(const char*,const char* event,const json& detail){std::lock_guard<std::mutex> lock(fixtureLogMx);fixtureLogs.push_back({{"event",event},{"detail",detail}});}
static void appendNativeLifecycleLog(const char* event,const json& detail){appendDomainLog("gyro",event,detail);}
struct RealProviderWithSdkTransport {
    ymcc::msi_sensor::Provider real;
    std::shared_ptr<Shared> transport;
    ymcc::msi_sensor::Options last;
    unsigned starts=0;
    bool running()const{return real.running();}
    bool stop(){return real.stop(100);}
    bool start(ymcc::msi_sensor::Options options){++starts;last=options;const auto state=transport;options.managerFactory=[state](ISensorManager** out){if(!out)return E_POINTER;*out=new Manager(state);return S_OK;};return real.start(std::move(options));}
};
static RealProviderWithSdkTransport g_hcWindowsSensorApiProvider;
static std::atomic<UINT64> g_hcWindowsSensorApiGeneration{0};
static void inputCaptureStopWinRtSensorsImpl();
// @@SOURCE_FUNCTIONS@@
static unsigned fixturePartialRetirements=0;
static void inputCaptureStopWinRtSensorsImpl(){++fixturePartialRetirements;g_inputCaptureSink->SetBindingGeneration(0);inputCaptureStopHcWindowsSensorApi();++g_inputCaptureSensorGeneration;g_gyroSensor=g_accelSensor=false;std::lock_guard<std::mutex> lock(g_sensorMetaMx);g_sensorMeta={};}
}
int main(int argc,char** argv){try{
 if(argc!=4)return 2;json oracle;std::ifstream(argv[1])>>oracle;
 const auto module=LoadLibraryExW(std::filesystem::path(argv[2]).wstring().c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("Pinned GM load failed");
 const auto sym=[&](const char* name){auto p=GetProcAddress(module,name);if(!p)throw std::runtime_error(name);return p;};
 const auto create=reinterpret_cast<void*(__cdecl *)()>(sym("CreateGamepadMotion"));const auto destroy=reinterpret_cast<void(__cdecl *)(void*)>(sym("DeleteGamepadMotion"));const auto manual=reinterpret_cast<void(__cdecl *)(void*,int)>(sym("SetCalibrationMode"));const auto calibrated=reinterpret_cast<void(__cdecl *)(void*,float*,float*,float*)>(sym("GetCalibratedGyro"));
 candidate::fixtureRealProcess=reinterpret_cast<decltype(candidate::fixtureRealProcess)>(sym("ProcessMotion"));candidate::g_gmProcess=&candidate::fixtureProcessAndRecord;
json rows=json::array();for(const auto& r:oracle["identities"])rows.push_back(r);for(const auto& r:oracle["legacy"]["cases"])rows.push_back(r);
 const auto defaults=oracle["reviewedDefaultModelClasses"].get<std::set<std::string>>();std::set<std::string> allowed={"AYANEO","OneXPlayer","AOKZOE","Ayn","Zotac","Minisforum","SuiPlay"};std::map<std::string,json> models;json coverage=json::array();unsigned scenarios=0;
 for(const auto& row:rows){candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();const auto& id=candidate::g_machineIdentity;const auto declared=candidate::inputCaptureMotionCapabilityReceipt(candidate::g_sensorMeta,false);
  const std::string family=declared.value("deviceClass","").empty()?"":row.value("family","");
  // Independent source-class family map is supplied with the fixed HC audit.
  const auto classFamily=oracle["sourceFamilies"].value(id.deviceClass,"");const bool expected=(allowed.contains(classFamily)||defaults.contains(id.deviceClass))&&id.resolved&&candidate::inputCaptureMotionSelectorBound();
  const bool actual=candidate::inputCaptureHcWindowsLegacyNeeded(id,false,false);check(actual==expected,"windows-fallback-scope",id.deviceClass+": actual classifier, exact catalog and independent family; actual="+std::to_string(actual)+" expected="+std::to_string(expected)+" bound="+std::to_string(candidate::inputCaptureMotionSelectorBound()));
  check(!candidate::inputCaptureHcWindowsLegacyNeeded(id,true,true),"windows-fallback-scope",id.deviceClass+": full WinRT source retained");
  if(actual){models.emplace(id.deviceClass,row);check(candidate::inputCaptureHcWindowsLegacyNeeded(id,true,false)&&candidate::inputCaptureHcWindowsLegacyNeeded(id,false,true),"windows-fallback-scope",id.deviceClass+": incomplete WinRT pair eligible");}
 }
 auto sink=&candidate::mockSink;
 const auto reset=[&](const json& row){candidate::inputCaptureStopHcWindowsSensorApi();if(candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();candidate::g_gm=create();if(!candidate::g_gm)throw std::runtime_error("GM create failed");manual(candidate::g_gm,0);candidate::g_gyroSensor=candidate::g_accelSensor=false;candidate::mockStopRequested=false;candidate::g_inputCaptureSensorGeneration=73;sink->SetBindingGeneration(73);sink->rawCount=0;candidate::g_hcWindowsSensorApiProvider.transport=std::make_shared<Shared>();candidate::g_inputCaptureSawRealGyro=false;candidate::fixtureLogs.clear();};
 for(const auto& [model,row]:models){
  reset(row);auto state=candidate::g_hcWindowsSensorApiProvider.transport;
  check(candidate::inputCaptureStartHcWindowsSensorApi(73),"windows-fallback-source",model+": actual main start with SDK mock transport");
  check(await([&]{const auto m=candidate::inputCaptureSensorMetaSnapshot();return m.gyroPresent&&m.accelPresent;}),"windows-fallback-source",model+": both actual timestamped standard triples observed");
  const auto callback=candidate::g_hcWindowsSensorApiProvider.last.sample;const auto gen=candidate::g_hcWindowsSensorApiGeneration.load();
  check(candidate::inputCaptureStopHcWindowsSensorApi(),"windows-fallback-source",model+": actual stop helper drains reader");
  const auto meta=candidate::inputCaptureSensorMetaSnapshot();check(meta.gyroFamily=="sensorapi"&&meta.accelFamily=="sensorapi"&&!meta.gyroHcLegacyFields&&!meta.accelHcLegacyFields,"windows-fallback-source",model+": no Claw layout or extra matrix flag");
  {std::lock_guard<std::mutex> lock(sink->rawMx);check(sink->rawAxes==std::array<double,6>{1,2,3,0.25,-0.5,1},"windows-fallback-source",model+": physical units and original axis order preserved at ingress");}
  {
   std::array<double,6> raw;{std::lock_guard<std::mutex> lock(sink->rawMx);raw=sink->rawAxes;}
   const auto processed=candidate::probeActive(true,meta.gyroPresent,meta.accelPresent,meta.gyroFamily,meta.accelFamily,73,73,raw);
   check(processed.admitted&&processed.processCalls==1,"windows-fallback-process",model+": actual SDK ingress enters extracted native matrix and real GM");
   const auto gy=oracle["expectedSdkProcess"].at(candidate::g_hcMotionSelector.gyroMatrixId),ac=oracle["expectedSdkProcess"].at(candidate::g_hcMotionSelector.accelMatrixId);bool correct=true;
   for(int i=0;i<6;i++)correct&=std::abs(processed.processAxes[i]-(i<3?gy[i].get<float>():ac[i-3].get<float>()))<1e-6f;
   std::array<float,3> cal{};calibrated(candidate::g_gm,&cal[0],&cal[1],&cal[2]);for(int i=0;i<3;i++)correct&=std::isfinite(cal[i])&&std::abs(cal[i]-gy[i].get<float>())<1e-6f;
   check(correct,"windows-fallback-process",model+": independent HC numeric recipe and real calibrated gyro agree");
  }
  check(candidate::g_hcWindowsSensorApiProvider.last.accelPollFloorMs==8&&Options{}.accelPollFloorMs==100&&!candidate::g_hcWindowsSensorApiProvider.last.clawHidCompatible,"windows-fallback-source",model+": standard source cadence separate from preserved MSI defaults");
  Sample late;late.generation=gen;late.timestamp100ns=134361240000000000ull;late.kind=Kind::Gyro;late.layout="sdk-motion-gyro-dps";late.axes={99,99,99};const auto count=sink->rawCount.load();callback(late);check(sink->rawCount==count,"windows-fallback-retirement",model+": callback retained after stop cannot publish");
  check(state->managers==0&&state->sensors==0&&state->reports==0&&state->writes==0&&state->permissions==0,"windows-fallback-source",model+": no COM leaks, device writes or permission calls");
  coverage.push_back(model);++scenarios;
 }
 if(models.empty())throw std::runtime_error("No eligible source model");const auto row=models.begin()->second;
 for(unsigned partial=0;partial<2;++partial){reset(row);candidate::g_gyroSensor=partial==0;candidate::g_accelSensor=partial==1;const auto previous=candidate::fixturePartialRetirements;check(candidate::inputCaptureStartHcWindowsSensorApi(73),"windows-fallback-partial","incomplete WinRT pair starts complete replacement");check(await([&]{auto m=candidate::inputCaptureSensorMetaSnapshot();return m.gyroPresent&&m.accelPresent;}),"windows-fallback-partial","new provider forms pair");candidate::inputCaptureStopHcWindowsSensorApi();check(candidate::fixturePartialRetirements==previous+1&&candidate::g_hcWindowsSensorApiProvider.last.generation==74&&sink->activeBindingGeneration==74&&!candidate::g_gyroSensor&&!candidate::g_accelSensor,"windows-fallback-partial","partial source retired before new generation and no mixed WinRT source");++scenarios;}
 reset(row);candidate::g_gyroSensor=candidate::g_accelSensor=true;const auto starts=candidate::g_hcWindowsSensorApiProvider.starts;check(!candidate::inputCaptureStartHcWindowsSensorApi(73)&&candidate::g_hcWindowsSensorApiProvider.starts==starts,"windows-fallback-partial","successful WinRT pair causes no fallback start");
 for(const std::string failure:{"ambiguous","missing-axis","no-timestamp","denied","no-sensors"}){reset(row);auto state=candidate::g_hcWindowsSensorApiProvider.transport;state->ambiguous=failure=="ambiguous";state->missingAxis=failure=="missing-axis";state->noTimestamp=failure=="no-timestamp";state->denied=failure=="denied";state->noSensors=failure=="no-sensors";candidate::inputCaptureStartHcWindowsSensorApi(73);if(failure=="no-sensors")await([&]{return !candidate::g_hcWindowsSensorApiProvider.running();});else await([&]{return state->gyroReads>=2||state->accelReads>=2;});candidate::inputCaptureStopHcWindowsSensorApi();const auto meta=candidate::inputCaptureSensorMetaSnapshot();check(!(meta.gyroPresent&&meta.accelPresent),"windows-fallback-rejection",failure+": cannot claim complete physical pair");check(state->writes==0&&state->permissions==0&&state->managers==0&&state->sensors==0&&state->reports==0,"windows-fallback-rejection",failure+": clean read-only failure");++scenarios;}
 reset(row);auto state=candidate::g_hcWindowsSensorApiProvider.transport;state->block=true;candidate::inputCaptureStartHcWindowsSensorApi(73);check(WaitForSingleObject(state->readEntered,1500)==WAIT_OBJECT_0,"windows-fallback-quarantine","blocked SDK read entered");check(!candidate::inputCaptureStopHcWindowsSensorApi(),"windows-fallback-quarantine","blocked read is quarantined");const auto startsBefore=candidate::g_hcWindowsSensorApiProvider.starts;check(!candidate::inputCaptureStartHcWindowsSensorApi(73)&&candidate::g_hcWindowsSensorApiProvider.starts==startsBefore,"windows-fallback-quarantine","no second reader while old one remains live");SetEvent(state->release);await([&]{return !candidate::g_hcWindowsSensorApiProvider.running();});check(candidate::inputCaptureStopHcWindowsSensorApi()&&sink->rawCount==0,"windows-fallback-quarantine","late blocked completion cannot publish after retirement");++scenarios;
 destroy(candidate::g_gm);candidate::g_gm=nullptr;FreeLibrary(module);
 const json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"models",coverage},{"scenarios",scenarios},{"actualMainFunctions",true},{"actualSdkReader",true},{"actualGamepadMotion",true},{"physicalCalibrationConvergence",false},{"sdkTransport","mock COM interfaces only"},{"hardwareIo",false},{"physicalAcceptance",false},{"newProductDlls",json::array()}};std::ofstream(argv[3])<<report.dump(2);std::cout<<report["status"]<<" "<<checks<<" checks / "<<failures.size()<<" failures; "<<models.size()<<" model classes\n";return failures.empty()?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
