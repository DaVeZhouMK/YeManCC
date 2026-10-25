  // Generic fallback tests use fixed upstream classes/resources as expectations.
  // Machine identity remains independently unresolved; no OEM or HID route is changed.
  unsigned genericSources=0,genericCommitted=0;
  for(const auto& generic:genericOracle){
   ++genericSources;candidate::closeNative();candidate::inputCaptureWindowsProcessingRetire(false);
   if(candidate::g_gm&&candidate::g_gm!=&candidate::mockGamepadHandle)destroy(candidate::g_gm);
   candidate::setup(generic["dmi"]);candidate::inputCaptureResolveHcMotionSelector();candidate::g_gm=create();candidate::g_gmSessionStart=std::chrono::steady_clock::time_point(std::chrono::nanoseconds(++sequence));
   candidate::mockCapabilityPower=7;candidate::fixtureTick=1000;candidate::fixturePhysicalLive=true;candidate::fixtureHostBusy=false;
   const auto originalIdentity=candidate::identityJson(candidate::g_machineIdentity);
   txCheck(candidate::g_machineIdentity.deviceClass.empty()&&candidate::g_machineIdentity.inputHidKey.empty(),"generic source does not fabricate an exact model/HID route");
   candidate::g_sensorMeta.gyroPresent=candidate::g_sensorMeta.accelPresent=true;candidate::g_sensorMeta.gyroDeviceId="ACPI#GENERIC#1#{GYRO}";candidate::g_sensorMeta.accelDeviceId="ACPI#GENERIC#1#{ACCEL}";
   const auto defaultApi=generic.value("defaultApi",std::string{"winrt"});candidate::g_sensorMeta.gyroFamily=candidate::g_sensorMeta.accelFamily=defaultApi;candidate::g_sensorMeta.gyroHcLegacyFields=candidate::g_sensorMeta.accelHcLegacyFields=false;
   candidate::g_reviewedCalibrationContext={};candidate::g_calibRestoredDeviceId.clear();candidate::g_gyroCalibStageAtomic=candidate::GyroCalibStage::None;candidate::g_gyroCalibRequested=false;candidate::g_calibSourceAuto=false;
   candidate::g_realStickTest.enabled=true;candidate::g_realStickTest.autoCalibrate=false;candidate::g_realStickTest.virtualTargetEnabled=true;
   candidate::g_autoCalibTrusted=false;candidate::g_autoCalibFinished=false;candidate::g_autoCalibAttempts=0;candidate::g_gmProcessedThisTick=false;
   candidate::fixtureProcessCount=candidate::fixtureResetCount=0;candidate::fixtureEvents.clear();candidate::fixtureBeforeLoad={};candidate::fixtureAfterLoad={};candidate::fixtureBeforeFinish={};candidate::fixtureAfterProcess={};
   candidate::mockSink.gyroBindingGeneration=candidate::mockSink.accelBindingGeneration=73;candidate::mockSink.gyroOk=candidate::mockSink.accelOk=true;
   candidate::g_gmSetMode(candidate::g_gm,0);candidate::g_gmSetOffset(candidate::g_gm,0,0,0,0);write({{"keep",{{"unrelated",true}}}});
   fake_sdk::opened=fake_sdk::closed=0;fake_sdk::handlers.clear();fake_sdk::badNumericMask=fake_sdk::badTimeMask=fake_sdk::frozenMask=0;fake_sdk::frameValue=0;
   candidate::g_sensorThreadMailbox.open();candidate::inputCaptureWindowsSelectionSetStaOpen(true);candidate::g_powerLifecycle=candidate::PowerLifecycle::Ready;
   std::atomic_store(&candidate::g_windowsSelectionWorkerContext,std::shared_ptr<const candidate::WindowsSelectionWorkerContext>{});
   candidate::nativeBusTicks=candidate::pendingFrames=candidate::committedFrames=0;candidate::nativeProcessWitness=true;fake_sdk::onPump=[](){candidate::nativeBusPump();};
   independentInput={1,2,3,.1,.2,.9};fake_sdk::rawAxes=independentInput;candidate::nativeBusPump();
   const auto capability=candidate::inputCaptureMotionCapabilityReceipt(candidate::g_sensorMeta,false);
   const auto expectedClass=generic["expectedClass"].get<std::string>();
   txCheck(capability.value("deviceClass",std::string{})==expectedClass&&capability.value("modelSelectorBound",false),"actual generic fallback recipe binds without exact device identity: "+expectedClass);
   const auto context=std::atomic_load(&candidate::g_windowsSelectionWorkerContext);const bool current=candidate::inputCaptureWindowsSelectionContextCurrent(context);
   txCheck(current,"actual generic source publishes usable processing context: "+expectedClass);
   if(!current){traces.push_back({{"phase","generic-context-unavailable"},{"expectedClass",expectedClass},{"capability",capability},{"identity",originalIdentity}});continue;}
   json observation={{"recorded",true},{"contextCurrent",true},{"observationSerial",1},{"lastObservation",{{"deviceClass",candidate::g_machineIdentity.deviceClass},{"pairs",json::array()}}}};
   const auto recipe=candidate::inputCaptureCandidateMatrixReceipt(candidate::g_sensorMeta);
   const auto qualified=candidate::inputCaptureWindowsSelectionEvidence(observation,capability,recipe,candidate::fixtureTick);
   txCheck(qualified.model.valid()&&qualified.model.deviceClass==expectedClass&&qualified.contextCurrent,"generic read-only evidence preserves distinct machine and motion recipe classes");
   observation["lastObservation"]["deviceClass"]="ANOTHER-MACHINE";txCheck(!candidate::inputCaptureWindowsSelectionEvidence(observation,capability,recipe,candidate::fixtureTick).contextCurrent,"generic query does not borrow another machine observation");
   const auto expected=generic["expectedAxes"].get<std::array<double,6>>();
   for(unsigned i=0;i<6;++i)txCheck(std::fabs(candidate::fixtureProcessedAxes[i]-expected[i])<1e-5,"generic default axes reach actual DLL: "+expectedClass+"/"+std::to_string(i));
   txCheck(candidate::inputCaptureHcWindowsLegacyNeeded(candidate::g_machineIdentity,false,true)&&!candidate::inputCaptureHcWindowsLegacyNeeded(candidate::g_machineIdentity,true,true),"generic SensorAPI fallback only when default pair is incomplete");
   const auto capturedIdentity=candidate::g_machineIdentity;const auto capturedReceipt=candidate::g_hcMotionSelector;
   for(unsigned fault=0;fault<7;++fault){candidate::g_hcMotionSelector=capturedReceipt;
    if(fault==0)candidate::g_hcMotionSelector.deviceClass="HandheldCompanion.Devices.ROGAlly";
    if(fault==1)candidate::g_hcMotionSelector.matrixSourceSha256=std::string(64,'0');
    if(fault==2)candidate::g_hcMotionSelector.dmiProduct+="-STALE";
    if(fault==3)candidate::g_hcMotionSelector.gyroMatrixId=candidate::g_hcMotionSelector.accelMatrixId;
    if(fault==4)candidate::g_hcMotionSelector.nativeFamily=9;
    if(fault==5)candidate::g_hcMotionSelector.dmiProcessor+="-STALE";
    if(fault==6)candidate::g_hcMotionSelector.dmiSystemVersion+="-STALE";
    txCheck(!candidate::inputCaptureMotionSelectorBound(),"generic source rejects forged/stale recipe receipt");}
   candidate::g_hcMotionSelector=capturedReceipt;
   for(unsigned fault=0;fault<6;++fault){auto changed=capturedIdentity;
    if(fault==0)changed.attempted=false;if(fault==1)changed.resolved=true;
    if(fault==2)changed.deviceClass="HandheldCompanion.Devices.DefaultDevice";
    if(fault==3)changed.gyroMatrixId="untrusted";if(fault==4)changed.inputHidKey="untrusted";
    if(fault==5)changed.family=candidate::YmccFamily::Gpd;
    candidate::HcMotionSelectorReceipt none;txCheck(!candidate::inputCaptureResolveGenericHcMotionSelector(changed,none),"generic source rejects copied exact-model or partial identities");}
   for(unsigned fault=0;fault<8;++fault){candidate::nativeBusPump();const auto meta=candidate::g_sensorMeta;
    if(fault==0)candidate::mockSink.gyroReceiptTick=1;
    if(fault==1)candidate::mockSink.accelReceiptTick=candidate::fixtureTick+1;
    if(fault==2)candidate::mockSink.accelSequence=0;
    if(fault==3)candidate::mockSink.gyroTimestamp100ns=0;
    if(fault==4)candidate::mockSink.accelOk=false;
    if(fault==5)candidate::g_sensorMeta.accelDeviceId="ACPI#OTHER#9#{ACCEL}";
    if(fault==6)candidate::g_sensorMeta.accelFamily=defaultApi=="sensorapi"?"winrt":"sensorapi";
    if(fault==7)candidate::mockSink.accelBindingGeneration=72;
    const auto rejected=candidate::nativeBusStep();txCheck(!rejected.processed&&!rejected.output,"generic incomplete/stale/mixed source cannot reach processing or output");candidate::g_sensorMeta=meta;
    candidate::mockSink.gyroBindingGeneration=candidate::mockSink.accelBindingGeneration=73;candidate::mockSink.gyroOk=candidate::mockSink.accelOk=true;}
   candidate::nativeBusPump();
   const auto key=context->defaultKey;txCheck(!key.empty()&&key.find("YMCC-HC-WINDOWS-SOURCE-V1|")==0,"generic default owns a source-qualified calibration key");
   candidate::fixturePhysicalLive=false;const auto selected=candidate::selectNative("GENERIC-PRIVATE");txCheck(selected.value("inputProcessingCommitted",false),"generic private selection forms an actual processing commit: "+expectedClass);
   candidate::nativeBusPump();txCheck(candidate::lastNativeBus.processed&&!candidate::lastNativeBus.output,"generic source-only processing keeps physical output closed");
   for(unsigned i=0;i<6;++i)txCheck(std::fabs(candidate::fixtureProcessedAxes[i]-expected[i])<1e-5,"generic private axes reach actual DLL: "+expectedClass+"/"+std::to_string(i));
   candidate::fixturePhysicalLive=true;const auto restored=candidate::returnNativeDefault();txCheck(restored.value("inputProcessingCommitted",false)&&!restored["selected"].value("privateSource",true),"generic return-default observes current real processing");
   txCheck(restored["selected"].value("calibrationKey",std::string{})==key,"generic original default key restored");
   txCheck(candidate::identityJson(candidate::g_machineIdentity)==originalIdentity,"generic processing leaves machine family/identity unresolved and unchanged");
   txCheck(candidate::g_windowsSelectionLeaseCount==0&&fake_sdk::opened==fake_sdk::closed,"generic native captures balanced on default return");
   {const auto& view=candidate::lastNativeBus.view;ymcc::sensors::view::ProcessingScope<candidate::InputCaptureSensorMeta> scope(view,candidate::t_windowsProcessingMeta,candidate::t_windowsProcessingStamp,&candidate::t_windowsProcessingView);
    candidate::inputCaptureStoreCalibration(view.metadata.gyroDeviceId,7,8,9,10,1);}
   const auto again=candidate::selectNative("GENERIC-RELOAD");txCheck(again.value("inputProcessingCommitted",false),"generic source can adopt another private capture");
   candidate::fixturePhysicalLive=true;const auto reload=candidate::returnNativeDefault();txCheck(reload.value("inputProcessingCommitted",false)&&candidate::fixtureOffsetAtProcess==std::array<float,3>{7,8,9},"generic source calibration restored before first actual default processing");
   json stored;std::ifstream(file)>>stored;txCheck(stored["keep"]["unrelated"]==true,"generic source calibration preserves unrelated storage");
   if(selected.value("inputProcessingCommitted",false)&&restored.value("inputProcessingCommitted",false))++genericCommitted;
  }
  txCheck(genericSources==genericOracle.size()&&genericCommitted==genericOracle.size(),"both actual HC generic fallback classes complete source/processing roundtrip");
