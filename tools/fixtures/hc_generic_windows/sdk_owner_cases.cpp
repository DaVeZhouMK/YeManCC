namespace candidate {
static bool g_gyroSensor=false,g_accelSensor=false;
static UINT64 g_inputCaptureSensorGeneration=73;
static constexpr UINT32 kInputCaptureRequestedSensorIntervalMs=8;
static std::atomic<bool> g_inputCaptureSawRealGyro{false};
static void appendDomainLog(const char*,const char*,const json&){}
struct RealSdkProvider {
 ymcc::msi_sensor::Provider real;
 std::shared_ptr<Shared> transport;
 ymcc::msi_sensor::Options last;
 bool running()const{return real.running();}
 bool stop(){return real.stop(100);}
 bool start(ymcc::msi_sensor::Options options){
  last=options;const auto state=transport;const auto receiver=options.sample;
  options.managerFactory=[state](ISensorManager** result){*result=new Manager(state);return S_OK;};
  options.sample=[state,receiver](const ymcc::msi_sensor::Sample& sample){
   receiver(sample);bool accepted=false;
   {std::lock_guard<std::mutex> lock(mockSink.mx);const bool gyro=sample.kind==Kind::Gyro;
    accepted=(gyro?mockSink.gyroOk:mockSink.accelOk)&&(gyro?mockSink.gyroTimestamp100ns:mockSink.accelTimestamp100ns)==sample.timestamp100ns&&(gyro?mockSink.gyroBindingGeneration:mockSink.accelBindingGeneration)==sample.generation;}
   {std::lock_guard<std::mutex> lock(state->mx);state->received.push_back(sample);if(accepted)state->accepted.push_back(sample);}
  };
  return real.start(std::move(options));
 }
};
static RealSdkProvider g_hcWindowsSensorApiProvider;
static std::atomic<UINT64> g_hcWindowsSensorApiGeneration{0};
static void inputCaptureStopWinRtSensorsImpl();
// @@ACTUAL_FALLBACK@@
// Typed WinRT unsubscribe is controlled. Current fallback owns the retirement decision.
static void inputCaptureStopWinRtSensorsImpl(){mockSink.SetBindingGeneration(0);inputCaptureStopHcWindowsSensorApi();++g_inputCaptureSensorGeneration;g_gyroSensor=g_accelSensor=false;std::lock_guard<std::mutex> lock(g_sensorMetaMx);g_sensorMeta={};}
}
int main(int argc,char**argv){HMODULE module=nullptr;json traces=json::array(),covered=json::array();
try{
 if(argc!=4)return 2;json oracle;std::ifstream(argv[1])>>oracle;
 module=LoadLibraryExW(std::filesystem::path(argv[2]).wstring().c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("Pinned DLL load failed");
 const auto sym=[&](const char* name){const auto value=GetProcAddress(module,name);if(!value)throw std::runtime_error(name);return value;};
 const auto create=reinterpret_cast<void*(__cdecl *)()>(sym("CreateGamepadMotion"));const auto destroy=reinterpret_cast<void(__cdecl *)(void*)>(sym("DeleteGamepadMotion"));
 candidate::g_gmSetOffset=reinterpret_cast<decltype(candidate::g_gmSetOffset)>(sym("SetCalibrationOffset"));candidate::g_gmSetMode=reinterpret_cast<decltype(candidate::g_gmSetMode)>(sym("SetCalibrationMode"));
 candidate::g_gmResetCal=reinterpret_cast<decltype(candidate::g_gmResetCal)>(sym("ResetContinuousCalibration"));candidate::g_gmGetOffset=reinterpret_cast<decltype(candidate::g_gmGetOffset)>(sym("GetCalibrationOffset"));
 candidate::fixtureRealReset=reinterpret_cast<decltype(candidate::g_gmResetMotionApi)>(sym("ResetGamepadMotion"));candidate::g_gmResetMotionApi=&candidate::fixtureRecordingReset;
 candidate::g_gmGetCal=reinterpret_cast<decltype(candidate::g_gmGetCal)>(sym("GetCalibratedGyro"));candidate::fixtureRealProcess=reinterpret_cast<decltype(candidate::g_gmProcess)>(sym("ProcessMotion"));candidate::g_gmProcess=&candidate::fixtureRecordingProcess;
 candidate::g_gmGetConf=reinterpret_cast<decltype(candidate::g_gmGetConf)>(sym("GetAutoCalibrationConfidence"));candidate::g_gmIsSteady=reinterpret_cast<decltype(candidate::g_gmIsSteady)>(sym("GetAutoCalibrationIsSteady"));
 const auto file=std::filesystem::absolute(candidate::inputCaptureCalibrationJsonPath()).lexically_normal(),expectedFile=std::filesystem::absolute(std::filesystem::path(argv[3]).parent_path()/"isolated-appdata"/"YeManCC"/"calibration.json").lexically_normal();
 if(file!=expectedFile)throw std::runtime_error("Not an isolated calibration file");std::filesystem::create_directories(file.parent_path());
 unsigned generic=0;std::uint64_t session=0;
 for(const auto& item:oracle["cases"]){const auto model=item["model"].get<std::string>();const bool isGeneric=item["generic"].get<bool>();
  candidate::inputCaptureStopHcWindowsSensorApi();candidate::closeNative();candidate::inputCaptureWindowsProcessingRetire(false);
  if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);
  candidate::setup(item["dmi"]);candidate::inputCaptureResolveHcMotionSelector();candidate::g_gm=create();if(!candidate::g_gm)throw std::runtime_error("GM create failed");
  candidate::g_gmSessionStart=std::chrono::steady_clock::time_point(std::chrono::nanoseconds(++session));candidate::fixtureTick=::GetTickCount64();candidate::mockCapabilityPower=7;
  candidate::g_reviewedCalibrationContext={};candidate::g_calibRestoredDeviceId.clear();candidate::g_gyroCalibStageAtomic=candidate::GyroCalibStage::None;candidate::g_gyroCalibRequested=false;candidate::g_calibSourceAuto=false;
  candidate::g_realStickTest.enabled=true;candidate::g_realStickTest.autoCalibrate=false;candidate::g_realStickTest.virtualTargetEnabled=true;
  candidate::g_autoCalibTrusted=false;candidate::g_autoCalibFinished=false;candidate::g_autoCalibAttempts=0;candidate::g_gmProcessedThisTick=false;
  candidate::fixtureProcessCount=candidate::fixtureResetCount=0;candidate::fixtureEvents.clear();candidate::fixtureAfterProcess={};candidate::fixtureBeforeLoad={};candidate::fixtureAfterLoad={};candidate::fixtureBeforeFinish={};
  candidate::g_gmSetMode(candidate::g_gm,0);candidate::g_gmSetOffset(candidate::g_gm,0,0,0,0);{std::ofstream out(file);out<<"{}";}
  candidate::g_sensorThreadMailbox.open();candidate::g_powerLifecycle=candidate::PowerLifecycle::Ready;candidate::g_inputCaptureReady=true;candidate::fixtureHostBusy=false;candidate::fixturePhysicalLive=true;
  candidate::inputCaptureWindowsSelectionSetStaOpen(true);fake_sdk::onPump={};std::atomic_store(&candidate::g_windowsSelectionWorkerContext,std::shared_ptr<const candidate::WindowsSelectionWorkerContext>{});
  candidate::mockSink.ResetSamples();candidate::mockSink.SetBindingGeneration(73);candidate::g_inputCaptureSensorGeneration=73;candidate::g_gyroSensor=candidate::g_accelSensor=false;
  if(isGeneric)++generic;
  check(candidate::inputCaptureMotionDeviceClass()==model,"sdk-owner-identity",model+": effective recipe agrees with independent HC class");
  if(isGeneric)check(!candidate::g_machineIdentity.resolved&&candidate::g_machineIdentity.deviceClass.empty()&&candidate::g_machineIdentity.inputHidKey.empty(),"sdk-owner-identity",model+": unknown machine identity remains untouched");
  candidate::WindowsSensorView firstView;
  for(unsigned pass=0;pass<2;++pass){
   const auto generation=73+pass;
   if(pass){candidate::inputCaptureWindowsProcessingRetire();check(firstView.submission.processingLease&&!firstView.submission.processingLease->load(),"sdk-owner-retirement",model+": old CPU lease retired");candidate::mockSink.SetBindingGeneration(generation);candidate::g_inputCaptureSensorGeneration=generation;
    const auto empty=candidate::nativeBusStep();check(!empty.processed&&!empty.output,"sdk-owner-retirement",model+": rebinding without SDK samples cannot process or submit");
    check(!ymcc::windows_motion::current(firstView.submission),"sdk-owner-retirement",model+": old input epoch cannot reappear");}
   auto state=std::make_shared<Shared>();candidate::g_hcWindowsSensorApiProvider.transport=state;
   check(candidate::inputCaptureStartHcWindowsSensorApi(generation),"sdk-owner-ingress",model+": current fallback starts real standard SDK reader");
   const bool ready=await([&]{std::lock_guard<std::mutex> lock(state->mx);unsigned gy=0,ac=0;for(const auto& r:state->accepted)(r.kind==Kind::Gyro?gy:ac)++;return gy>=2&&ac>=2;});
   check(ready,"sdk-owner-ingress",model+": two original timestamped frames of each kind arrive");
   const auto oldCallback=candidate::g_hcWindowsSensorApiProvider.last.sample;
   check(candidate::inputCaptureStopHcWindowsSensorApi(),"sdk-owner-ingress",model+": reader drains before deterministic BUS step");
   std::array<Sample,2> last;std::array<unsigned,2> counts{};
   {std::lock_guard<std::mutex> lock(state->mx);for(const auto& sample:state->accepted){const unsigned k=sample.kind==Kind::Gyro?0:1;last[k]=sample;++counts[k];}}
   candidate::fixtureTick=::GetTickCount64();
   const auto before=::fixtureTotalPinnedCalls;
   const auto frame=candidate::nativeBusStep();
   check(frame.processed&&::fixtureTotalPinnedCalls==before+1,"sdk-owner-process",model+": actual SDK capture enters actual current processing owner and pinned DLL");
   check(frame.output,"sdk-owner-output",model+": actual physical gate permits the freshly processed frame");
   const auto receipt=candidate::inputCaptureWindowsProcessingReceiptSnapshot();
   check(receipt.value("currentSamplingAndProcessingLease",false),"sdk-owner-process",model+": actual processing receipt is current");
   for(unsigned k=0;k<2;++k){const auto& part=frame.view.parts[k];
    check(part.axes==last[k].axes&&part.timestamp100ns==last[k].timestamp100ns&&part.receipt==last[k].receiptTick&&part.generation==last[k].generation,"sdk-owner-provenance",model+": original SDK values, timestamp, receipt and generation retained "+std::to_string(k));
    check(part.sequence==counts[k]+(pass?firstView.parts[k].sequence:0)&&counts[k]>=2,"sdk-owner-sequence",model+": actual raw sink increments for every decoded frame "+std::to_string(k));
    constexpr std::uint64_t day=86400ull*10000000ull;
    check(part.timeOfDayMs==double((last[k].timestamp100ns%day)/10000),"sdk-owner-provenance",model+": source time of day uses actual sink conversion "+std::to_string(k));
   }
   const auto mapped=item["mapped"].get<std::array<double,6>>();for(unsigned i=0;i<6;++i)check(std::abs(candidate::fixtureProcessedAxes[i]-mapped[i])<1e-6,"sdk-owner-axis",model+": original raw sample maps through independent HC axis "+std::to_string(i));
   const std::array<float,3> offsets=pass?std::array<float,3>{.125f,.25f,.5f}:std::array<float,3>{};
   check(candidate::fixtureOffsetAtProcess==offsets,"sdk-owner-calibration",model+": source-specific offsets restored before actual ProcessMotion");
   std::array<float,3> calibrated{};candidate::g_gmGetCal(candidate::g_gm,&calibrated[0],&calibrated[1],&calibrated[2]);for(unsigned k=0;k<3;++k)check(std::abs(calibrated[k]-(mapped[k]-offsets[k]))<1e-5,"sdk-owner-calibration",model+": real DLL calibrated gyro matches original sample "+std::to_string(k));
   candidate::fixturePhysicalLive=false;const auto disabled=candidate::nativeBusStep();check(!disabled.output,"sdk-owner-output",model+": removing physical permission blocks output separately");candidate::fixturePhysicalLive=true;
   {std::lock_guard<std::mutex> lock(candidate::mockSink.mx);const auto gy=candidate::mockSink.gyroSequence,ac=candidate::mockSink.accelSequence;oldCallback(last[0]);oldCallback(last[1]);check(candidate::mockSink.gyroSequence==gy&&candidate::mockSink.accelSequence==ac,"sdk-owner-retirement",model+": drained SDK callback cannot mutate the capture");}
   check(state->writes==0&&state->permissions==0&&state->managers==0&&state->sensors==0&&state->reports==0,"sdk-owner-ingress",model+": no device writes, permission calls or COM leaks");
   if(!pass){firstView=frame.view;ymcc::sensors::view::ProcessingScope<candidate::InputCaptureSensorMeta> scope(frame.view,candidate::t_windowsProcessingMeta,candidate::t_windowsProcessingStamp,&candidate::t_windowsProcessingView);candidate::inputCaptureStoreCalibration(frame.view.metadata.gyroDeviceId,.125f,.25f,.5f,10,1);}
   else if(isGeneric){candidate::fixtureTick=std::max(last[0].receiptTick,last[1].receiptTick)+351;const auto stale=candidate::nativeBusStep();check(!stale.processed&&!stale.output,"sdk-owner-freshness",model+": original SDK receipts expire at generic admission");}
   traces.push_back({{"model",model},{"pass",pass},{"originalGyroTimestamp",last[0].timestamp100ns},{"originalAccelTimestamp",last[1].timestamp100ns},{"originalGyroReceipt",last[0].receiptTick},{"originalAccelReceipt",last[1].receiptTick},{"gyroDecoded",counts[0]},{"accelDecoded",counts[1]},{"currentProcessing",receipt.value("currentSamplingAndProcessingLease",false)}});
  }
  covered.push_back(model);
 }
 check(generic==2&&covered.size()==56,"sdk-owner-coverage","both generic classes and all standard SDK fallback models covered");
 candidate::inputCaptureStopHcWindowsSensorApi();candidate::closeNative();candidate::inputCaptureWindowsProcessingRetire(false);destroy(candidate::g_gm);candidate::g_gm=nullptr;
}catch(const std::exception& error){failures.push_back({{"group","sdk-owner-fixture"},{"label",std::string("fixture exception: ")+error.what()}});}
const json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"traces",traces},{"models",covered},{"actualPinnedProcessCalls",fixtureTotalPinnedCalls},{"actualRawSink",true},{"actualSdkDecoder",true},{"actualFallbackCallbacks",true},{"actualFullProcessingOwner",true},{"receiptProducerControlled",false},{"sdkTransport","controlled standard SDK COM reports"},{"wholeInputCaptureOnGamepadExecuted",false},{"wholeBusExecuted",false},{"hostSessionStarted",false},{"physicalAcceptance",false},{"hardwareIo",false},{"newProductionDlls",json::array()}};
std::ofstream(argv[3])<<report.dump(2);std::cout<<report["status"]<<" "<<checks<<" checks / "<<failures.size()<<" failures; "<<covered.size()<<" models\n";
if(module)FreeLibrary(module);return failures.empty()?0:1;
}