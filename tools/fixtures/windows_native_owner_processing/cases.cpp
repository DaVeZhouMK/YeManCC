static void txCheck(bool ok,const std::string& text){check(ok,"native-owner-real-processing",text);}
int main(int argc,char**argv){json traces=json::array();std::set<std::string> coveredModels,coveredProfiles;unsigned aliases=0,historicalRawReplays=0;std::set<std::string> historicalFamilies;HMODULE module=nullptr;
try{
 if(argc!=5)return 2;json oracle;std::ifstream(argv[1])>>oracle;const std::string control=argv[4];
 module=LoadLibraryExW(std::filesystem::path(argv[2]).wstring().c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("Pinned DLL load failed");
 const auto sym=[&](const char* name){const auto value=GetProcAddress(module,name);if(!value)throw std::runtime_error(name);return value;};
 const auto create=reinterpret_cast<void*(__cdecl *)()>(sym("CreateGamepadMotion"));const auto destroy=reinterpret_cast<void(__cdecl *)(void*)>(sym("DeleteGamepadMotion"));
 candidate::g_gmSetOffset=reinterpret_cast<decltype(candidate::g_gmSetOffset)>(sym("SetCalibrationOffset"));candidate::g_gmSetMode=reinterpret_cast<decltype(candidate::g_gmSetMode)>(sym("SetCalibrationMode"));
 candidate::g_gmResetCal=reinterpret_cast<decltype(candidate::g_gmResetCal)>(sym("ResetContinuousCalibration"));candidate::g_gmGetOffset=reinterpret_cast<decltype(candidate::g_gmGetOffset)>(sym("GetCalibrationOffset"));
 candidate::fixtureRealReset=reinterpret_cast<decltype(candidate::g_gmResetMotionApi)>(sym("ResetGamepadMotion"));candidate::g_gmResetMotionApi=&candidate::fixtureRecordingReset;
 candidate::g_gmGetCal=reinterpret_cast<decltype(candidate::g_gmGetCal)>(sym("GetCalibratedGyro"));candidate::fixtureRealProcess=reinterpret_cast<decltype(candidate::g_gmProcess)>(sym("ProcessMotion"));candidate::g_gmProcess=&candidate::fixtureRecordingProcess;
 candidate::g_gmGetConf=reinterpret_cast<decltype(candidate::g_gmGetConf)>(sym("GetAutoCalibrationConfidence"));candidate::g_gmIsSteady=reinterpret_cast<decltype(candidate::g_gmIsSteady)>(sym("GetAutoCalibrationIsSteady"));
 const auto file=std::filesystem::absolute(candidate::inputCaptureCalibrationJsonPath()).lexically_normal();
 const auto expectedFile=std::filesystem::absolute(std::filesystem::path(argv[3]).parent_path()/"isolated-appdata"/"YeManCC"/"calibration.json").lexically_normal();
 if(file!=expectedFile)throw std::runtime_error("Not an isolated calibration file");std::filesystem::create_directories(file.parent_path());
 const auto write=[&](const json& value){std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<value.dump(2);};
 std::array<double,6> independentInput{1,2,3,0,0,1};
 // @@INDEPENDENT_ALIAS_AND_AXES_ORACLE@@
 unsigned sequence=0;
 for(const auto&[caseId,row]:rows){
  const auto cls=row["expectedFullClass"].get<std::string>();
  if(!control.empty()&&cls!="HandheldCompanion.Devices.ClawCG3EM"&&cls!="HandheldCompanion.Devices.GPDWin5"&&cls.find("XboxROGAlly")==std::string::npos)continue;
  const auto beforeFailures=failures.size();candidate::closeNative();candidate::inputCaptureWindowsProcessingRetire(false);
  if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);
  candidate::setup(row);candidate::inputCaptureResolveHcMotionSelector();candidate::g_gm=create();candidate::g_gmSessionStart=std::chrono::steady_clock::time_point(std::chrono::nanoseconds(++sequence));
  candidate::mockCapabilityPower=7;candidate::fixtureTick=1000;candidate::fixturePhysicalLive=true;candidate::fixtureHostBusy=false;
  candidate::g_sensorMeta.gyroPresent=candidate::g_sensorMeta.accelPresent=true;candidate::g_sensorMeta.gyroDeviceId="ACPI#BASE#1#{GYRO}";candidate::g_sensorMeta.accelDeviceId="ACPI#BASE#1#{ACCEL}";
  const bool msiDefault=cls.find("Claw")!=std::string::npos;candidate::g_sensorMeta.gyroFamily=candidate::g_sensorMeta.accelFamily=msiDefault?"sensorapi":"winrt";candidate::g_sensorMeta.gyroHcLegacyFields=candidate::g_sensorMeta.accelHcLegacyFields=msiDefault;
  candidate::g_reviewedCalibrationContext={};candidate::g_calibRestoredDeviceId.clear();candidate::g_gyroCalibStageAtomic=candidate::GyroCalibStage::None;candidate::g_gyroCalibRequested=false;candidate::g_calibSourceAuto=false;
  candidate::g_realStickTest.enabled=true;candidate::g_realStickTest.autoCalibrate=false;candidate::g_realStickTest.virtualTargetEnabled=true;
  candidate::g_autoCalibTrusted=false;candidate::g_autoCalibFinished=false;candidate::g_autoCalibAttempts=0;candidate::g_gmProcessedThisTick=false;
  candidate::fixtureProcessCount=candidate::fixtureResetCount=0;candidate::fixtureEvents.clear();candidate::fixtureBeforeLoad={};candidate::fixtureAfterLoad={};candidate::fixtureBeforeFinish={};candidate::fixtureAfterProcess={};
  candidate::mockSink.gyroBindingGeneration=candidate::mockSink.accelBindingGeneration=73;candidate::mockSink.gyroOk=candidate::mockSink.accelOk=true;
  candidate::g_gmSetMode(candidate::g_gm,0);candidate::g_gmSetOffset(candidate::g_gm,0,0,0,0);write({{"keep",{{"unrelated",true}}}});
  fake_sdk::rawAxes={1,2,3,0,0,1};fake_sdk::intervalValue=0;fake_sdk::failOpenKind=-1;fake_sdk::nullMask=fake_sdk::badNumericMask=fake_sdk::badTimeMask=fake_sdk::frozenMask=0;fake_sdk::frameValue=0;
  candidate::g_sensorThreadMailbox.open();candidate::inputCaptureWindowsSelectionSetStaOpen(true);candidate::g_powerLifecycle=candidate::PowerLifecycle::Ready;
  std::atomic_store(&candidate::g_windowsSelectionWorkerContext,std::shared_ptr<const candidate::WindowsSelectionWorkerContext>{});
  candidate::nativeBusTicks=candidate::pendingFrames=candidate::committedFrames=0;candidate::nativeProcessWitness=true;fake_sdk::onPump=[](){candidate::nativeBusPump();};candidate::nativeBusPump();
  txCheck(candidate::g_machineIdentity.deviceClass==cls,"actual identity matches source oracle: "+caseId);
  const auto context=std::atomic_load(&candidate::g_windowsSelectionWorkerContext);txCheck(candidate::inputCaptureWindowsSelectionContextCurrent(context),"actual BUS publishes a usable native request context: "+caseId);
  if(!candidate::inputCaptureWindowsSelectionContextCurrent(context)){traces.push_back({{"case",caseId},{"phase","context-unavailable"}});continue;}
  coveredModels.insert(cls);++aliases;const auto normalKey=context->defaultKey;
  const auto defaultStamp=candidate::lastNativeBus.view.submission;
  const auto processedBefore=candidate::fixtureProcessCount;candidate::fixturePhysicalLive=false;
  const auto a=candidate::selectNative("PRIVATE-A");
  txCheck(a.value("inputProcessingCommitted",false),"actual native source commits only after actual BUS/DLL processing: "+caseId);
  txCheck(candidate::nativeProcessWitness&&candidate::fixtureProcessCount>processedBefore&&candidate::pendingFrames>0,"actual DLL runs with pending publication closed: "+caseId);
  txCheck(defaultStamp.processingLease&&!defaultStamp.processingLease->load(),"prior default CPU permission is revoked separately from input choice: "+caseId);
  txCheck(!ymcc::windows_motion::current(defaultStamp),"old default submission retires at native choice commit: "+caseId);
  if(!a.value("inputProcessingCommitted",false)){traces.push_back({{"case",caseId},{"phase","select-A"},{"status",a},{"processor",candidate::inputCaptureWindowsProcessingReceiptSnapshot()}});continue;}
  candidate::nativeBusPump();const auto processed=candidate::inputCaptureWindowsProcessingReceiptSnapshot();
  txCheck(processed.value("currentSamplingAndProcessingLease",false)&&processed.value("sourcePublicationAllowed",false),"fresh actual DLL receipt has committed source permission: "+caseId);
  txCheck(candidate::lastNativeBus.processed&&!candidate::lastNativeBus.output,"source-only actual processing does not authorize physical-controller output: "+caseId);
  const auto wanted=expectedAxes(row,cls);for(unsigned i=0;i<6;++i)txCheck(std::fabs(candidate::fixtureProcessedAxes[i]-wanted[i])<1e-5,"raw native source reaches pinned DLL through correct model axis "+std::to_string(i)+": "+caseId);
  coveredProfiles.insert(ymcc::sensors::selection::fingerprint(context->model));
  const auto aStamp=candidate::lastNativeBus.view.submission;
  {
   const auto& view=candidate::lastNativeBus.view;ymcc::sensors::view::ProcessingScope<candidate::InputCaptureSensorMeta> scope(view,candidate::t_windowsProcessingMeta,candidate::t_windowsProcessingStamp,&candidate::t_windowsProcessingView);
   candidate::inputCaptureStoreCalibration(view.metadata.gyroDeviceId,11,12,13,10,1);
  }
  const auto b=candidate::selectNative("PRIVATE-B");txCheck(b.value("inputProcessingCommitted",false),"second private source commits through real producer: "+caseId);
  txCheck(aStamp.processingLease&&!aStamp.processingLease->load(),"prior private CPU permission is independently revoked: "+caseId);
  txCheck(!ymcc::windows_motion::current(aStamp),"prior actual processed frame is retired after second selection: "+caseId);
  const auto returnA=candidate::selectNative("PRIVATE-A");txCheck(returnA.value("inputProcessingCommitted",false),"returning private source commits under new native capture: "+caseId);
  txCheck(candidate::fixtureOffsetAtProcess==std::array<float,3>{11,12,13},"actual stored private offset is restored before first ProcessMotion: "+caseId);
  candidate::fixturePhysicalLive=true;const auto d=candidate::returnNativeDefault();txCheck(d.value("inputProcessingCommitted",false)&&!d["selected"].value("privateSource",true),"actual default return observes its own real DLL receipt: "+caseId);
  txCheck(d["selected"].value("calibrationKey",std::string{})==normalKey,"default calibration key survives composed native route roundtrip: "+caseId);
  txCheck(candidate::g_windowsSelectionLeaseCount==0&&fake_sdk::opened==fake_sdk::closed&&fake_sdk::handlers.empty(),"default commitment releases all native leases and callbacks: "+caseId);
  json stored;std::ifstream(file)>>stored;txCheck(stored["keep"]["unrelated"]==true,"real source-specific storage preserves unrelated calibration JSON: "+caseId);
  if(control.empty()||control=="--host-readiness"||control=="--charge-host-wait"||control=="--revoke-rollback"){
   const auto readyStart=candidate::fixtureTick;candidate::fixtureHostBusy=true;candidate::fixturePhysicalLive=false;
   candidate::beforeNativeBus=[readyStart]{if(candidate::fixtureTick>=readyStart+5000)candidate::fixtureHostBusy=false;};
   const auto readiness=candidate::selectNative("HOST-READINESS");
   txCheck(readiness.value("inputProcessingCommitted",false)&&readiness.value("phase",std::string{})=="committed-input-observed"&&readiness["selected"].value("gyroDeviceId",std::string{})=="ACPI#HOST-READINESS#1#{GYRO}","real BUS host-lifecycle gate can outlast 2s without quarantining the source selection: "+caseId);
   if(!readiness.value("inputProcessingCommitted",false))traces.push_back({{"case",caseId},{"phase","host-busy-before-prepare"},{"status",readiness},{"elapsed",candidate::fixtureTick-readyStart}});
   candidate::fixtureHostBusy=false;candidate::beforeNativeBus={};candidate::fixturePhysicalLive=true;candidate::nativeBusPump();candidate::returnNativeDefault();
   std::uint64_t pendingBusyUntil=0;
   candidate::fixturePhysicalLive=false;
   candidate::beforeNativeBus=[&]{const auto projection=std::atomic_load(&candidate::g_windowsPrivateConsumerProjection);
    if(projection&&projection->selected.pair.gyroId=="ACPI#HOST-PENDING#1#{GYRO}"&&!pendingBusyUntil)pendingBusyUntil=candidate::fixtureTick+5000;
    candidate::fixtureHostBusy=pendingBusyUntil&&candidate::fixtureTick<pendingBusyUntil;};
   const auto pendingReady=candidate::selectNative("HOST-PENDING");
   txCheck(pendingReady.value("inputProcessingCommitted",false)&&pendingReady.value("phase",std::string{})=="committed-input-observed"&&pendingReady["selected"].value("gyroDeviceId",std::string{})=="ACPI#HOST-PENDING#1#{GYRO}"&&pendingReady.value("processingOwnerWaitMs",std::uint64_t{0})>=5000,"already pending candidate waits for actual BUS readiness without spending its processing budget: "+caseId);
   candidate::fixtureHostBusy=false;candidate::beforeNativeBus={};candidate::nativeBusPump();
   bool sawUnavailable=false;std::uint64_t recoveryBusyUntil=0;
   candidate::beforeNativeBus=[&]{const auto projection=std::atomic_load(&candidate::g_windowsPrivateConsumerProjection);
    const bool unavailable=projection&&projection->selected.pair.gyroId=="ACPI#NO-RECEIPT#1#{GYRO}";
    candidate::g_gmProcess=unavailable?nullptr:&candidate::fixtureRecordingProcess;
    if(unavailable)sawUnavailable=true;
    else if(sawUnavailable&&projection&&projection->selected.pair.gyroId=="ACPI#HOST-PENDING#1#{GYRO}"&&!recoveryBusyUntil)recoveryBusyUntil=candidate::fixtureTick+5000;
    candidate::fixtureHostBusy=recoveryBusyUntil&&candidate::fixtureTick<recoveryBusyUntil;};
   const auto recovery=candidate::selectNative("NO-RECEIPT");
   txCheck(recovery.value("phase",std::string{})=="rolled-back-input-observed"&&recovery.value("inputProcessingCommitted",false),"failed real producer restores its retained source after upstream readiness returns: "+caseId);
   candidate::fixtureHostBusy=false;candidate::beforeNativeBus={};candidate::g_gmProcess=&candidate::fixtureRecordingProcess;candidate::nativeBusPump();
   const auto recoveredStatus=candidate::inputCaptureWindowsSourceSelectionStatus();
   txCheck((recoveredStatus.contains("sourcePublicationAllowed")&&recoveredStatus["sourcePublicationAllowed"]==true),"freshly processed rollback source retains committed publication permission: "+caseId);
   if(!(recoveredStatus.contains("sourcePublicationAllowed")&&recoveredStatus["sourcePublicationAllowed"]==true))traces.push_back({{"case",caseId},{"phase","rollback-publication"},{"status",recoveredStatus}});
   candidate::fixturePhysicalLive=true;candidate::returnNativeDefault();
   const auto timeoutStamp=candidate::lastNativeBus.view.submission;const auto timeoutTick=candidate::fixtureTick;
   const auto opensAtWait=fake_sdk::opened;candidate::fixtureHostBusy=true;
   const auto readinessTimeout=candidate::selectNative("HOST-NOT-READY");
   txCheck(!readinessTimeout.value("inputProcessingCommitted",true)&&readinessTimeout.value("reason",std::string{})=="processing-owner-busy-timeout"&&candidate::fixtureTick-timeoutTick<=30000,"upstream readiness has one bounded per-request budget: "+caseId);
   txCheck(fake_sdk::opened==opensAtWait&&ymcc::windows_motion::current(timeoutStamp),"pre-adoption readiness timeout preserves existing source and avoids native candidate opens: "+caseId);
   candidate::fixtureHostBusy=false;candidate::nativeBusPump();
   const auto cancelledRequest=candidate::inputCaptureWindowsSourceSelectionRequest(candidate::selectionArgs("CANCEL-READINESS"));
   candidate::fixtureHostBusy=true;const auto cancelledAt=candidate::fixtureTick;
   candidate::beforeNativeBus=[&]{if(candidate::fixtureTick-cancelledAt>=64)candidate::inputCaptureWindowsSourceSelectionCancel({{"requestId",cancelledRequest["requestId"]}});};
   candidate::inputCaptureRunWindowsSelection();const auto cancelledReady=candidate::inputCaptureWindowsSourceSelectionStatus();
   txCheck(!cancelledReady.value("inputProcessingCommitted",true)&&candidate::fixtureTick-cancelledAt<=80&&candidate::g_windowsSelectionLeaseCount==0,"readiness wait observes explicit cancellation promptly and closes resources: "+caseId);
   candidate::fixtureHostBusy=false;candidate::beforeNativeBus={};candidate::nativeBusPump();candidate::returnNativeDefault();
  }
  if(control.empty()&&!historicalFamilies.contains(cls))for(const auto& archive:oracle["nativeOwnerLogReplay"])if(archive["deviceClass"]==cls){
   historicalFamilies.insert(cls);candidate::fixturePhysicalLive=false;unsigned sampleIndex=0;
   for(const auto& sample:archive["samples"]){independentInput=sample["raw"].get<std::array<double,6>>();fake_sdk::rawAxes=independentInput;
    const auto replay=candidate::selectNative("LOG-REPLAY-"+std::to_string(sampleIndex++));
    txCheck(replay.value("inputProcessingCommitted",false),"historical numeric stream completes actual native owner/DLL commit: "+caseId);
    const auto wantedReplay=expectedAxes(row,cls);for(unsigned i=0;i<6;++i)txCheck(std::fabs(candidate::fixtureProcessedAxes[i]-wantedReplay[i])<1e-5,"historical raw numeric axis reaches declared native recipe: "+caseId+"/"+std::to_string(i));
    txCheck(!candidate::lastNativeBus.output,"historical numeric stream does not invent a physical pad: "+caseId);++historicalRawReplays;
   }
   independentInput={1,2,3,0,0,1};fake_sdk::rawAxes=independentInput;candidate::fixturePhysicalLive=true;const auto back=candidate::returnNativeDefault();txCheck(back.value("inputProcessingCommitted",false),"historical numeric replay returns through actual default processing: "+caseId);
  }
  if(failures.size()!=beforeFailures)traces.push_back({{"case",caseId},{"phase","roundtrip"},{"status",candidate::inputCaptureWindowsSourceSelectionStatus()},{"actualProcessCalls",candidate::fixtureProcessCount}});
 }
 candidate::closeNative();candidate::inputCaptureWindowsProcessingRetire(false);if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle){destroy(candidate::g_gm);candidate::g_gm=nullptr;}
 txCheck(fake_sdk::opened==fake_sdk::closed&&fake_sdk::handlers.empty(),"all composed native resource lifetimes balanced");
 txCheck(!control.empty()?coveredModels.size()>=3:coveredModels.size()==67,"full declared Windows model set is composed with the real DLL");
 if(control.empty())txCheck(historicalRawReplays==6&&historicalFamilies.size()==3,"all three fixed source archives supply two re-read raw samples");
 if(control.empty())txCheck(aliases==229&&coveredProfiles.size()==71,"all positive source aliases and explicit profiles are exercised");
}catch(const std::exception& e){failures.push_back({{"group","native-owner-real-processing"},{"label",std::string("fixture exception: ")+e.what()}});}
const json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"traces",traces},{"models",coveredModels},{"aliases",aliases},{"profiles",coveredProfiles.size()},{"historicalRawReplays",historicalRawReplays},{"historicalFamilies",historicalFamilies},
 {"nativeOwnerAndActualBusMotionProducerComposed",true},{"realPinnedGamepadMotionExecuted",fixtureTotalPinnedCalls>0},{"actualPinnedProcessCalls",fixtureTotalPinnedCalls},{"receiptProducerControlled",false},{"sdkAcquisitionControlled",true},{"wholeInputCaptureOnGamepadExecuted",false},{"hardwareIo",false},{"newProductionDlls",json::array()}};
if(argc>=4)std::ofstream(argv[3])<<report.dump(2);std::cout<<report.dump()<<std::endl;if(module)FreeLibrary(module);return failures.empty()?0:1;}
