// Actual worker/GM ownership transitions. Native enumeration remains an explicit fixture boundary.
  {
   seed(row,"winrt",false);candidate::fixtureResetCount=0;
   const auto defaultMeta=candidate::g_sensorMeta;
   const auto modelRecipe=candidate::inputCaptureWindowsSelectionEvidence(json::object(),
       candidate::inputCaptureMotionCapabilityReceipt(defaultMeta,false),candidate::inputCaptureCandidateMatrixReceipt(defaultMeta),candidate::fixtureTick).model;
   auto choice=std::make_shared<candidate::MockBindingSink>();
   auto aCapture=std::make_shared<candidate::MockBindingSink>(),bCapture=std::make_shared<candidate::MockBindingSink>();
   auto aAlive=std::make_shared<std::atomic<bool>>(true),bAlive=std::make_shared<std::atomic<bool>>(true);
   const auto rawDefault=ymcc::windows_motion::retain(owner,73);
   const auto make=[&](bool privateSource,const std::string& suffix,const std::shared_ptr<candidate::MockBindingSink>& capture,
                      const std::shared_ptr<std::atomic<bool>>& alive){
    candidate::WindowsSensorView view;view.coherent=true;view.privateSource=privateSource;view.metadata=defaultMeta;
    if(privateSource){view.metadata.gyroDeviceId="ACPI#"+suffix+"#1#{GYRO}";view.metadata.accelDeviceId="ACPI#"+suffix+"#1#{ACCEL}";}
    view.capture=privateSource?ymcc::windows_motion::retain(capture,73):rawDefault;
    view.submission=privateSource?ymcc::windows_motion::guarded(view.capture,ymcc::windows_motion::retain(choice,73),alive):view.capture;
    if(privateSource){view.profile=ymcc::sensors::selection::fingerprint(modelRecipe);
      view.calibrationKey=ymcc::sensors::selection::calibrationKey(modelRecipe,{view.metadata.gyroDeviceId,view.metadata.accelDeviceId},ymcc::sensors::selection::TransformPolicy::ExplicitCurrentModel);view.captureRequestId=suffix=="PRIVATE-A"?101:102;view.choiceRequestId=view.captureRequestId+100;}
    for(unsigned k=0;k<2;++k){auto& part=view.parts[k];part.usable=true;part.axes=k?std::array<double,3>{0,0,1}:std::array<double,3>{1,2,3};part.generation=73;part.sequence=++sequence;part.receipt=candidate::fixtureTick;part.timestamp100ns=candidate::fixtureTick*10000;}
    return view;
   };
   auto defaultView=make(false,"",owner,{});
   const auto defaultKey=candidate::inputCaptureCalibrationStorageKey(defaultMeta.gyroDeviceId);
   candidate::inputCaptureStoreCalibration(defaultMeta.gyroDeviceId,1,2,3,10,1);
   {
    ymcc::sensors::view::ProcessingScope<candidate::InputCaptureSensorMeta> scope(defaultView,candidate::t_windowsProcessingMeta,candidate::t_windowsProcessingStamp,&candidate::t_windowsProcessingView);
    txCheck(candidate::inputCaptureWindowsProcessingAdopt(defaultView),"worker adopts the actual default model: "+model);
    txCheck(candidate::fixtureResetCount==0,"first default ownership does not reset working native state: "+model);
    candidate::inputCaptureWindowsProcessingNoteFrame(defaultView,true);
    {const auto missing=candidate::inputCaptureWindowsProcessingReceiptSnapshot();
     txCheck(!missing.value("currentSamplingAndProcessingLease",false),"a boolean cannot manufacture an actual processed receipt: "+model);
#if YMCC_FIXTURE_PUBLICATION_FENCE
     txCheck(missing.contains("sourcePublicationAllowed")&&missing["sourcePublicationAllowed"].is_null(),"no actual processed receipt keeps publication permission unknown: "+model);
#endif
    }
    txCheck(candidate::inputCaptureWindowsProcessingProcess(defaultView,1,2,3,0,0,1,.008f),"actual wrapper invokes the pinned DLL: "+model);
    candidate::inputCaptureWindowsProcessingNoteFrame(defaultView,true);
    txCheck(candidate::fixtureProcessCount==1&&candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("currentSamplingAndProcessingLease",false),"only actual ProcessMotion creates the first receipt: "+model);
   }
   const auto firstReceipt=candidate::inputCaptureWindowsProcessingReceiptSnapshot();candidate::fixtureTick+=8;
   {candidate::FixtureProcessingScope scope(defaultView);candidate::inputCaptureWindowsProcessingNoteFrame(defaultView,true);}
   txCheck(candidate::inputCaptureWindowsProcessingReceiptSnapshot()==firstReceipt,"repeated NoteFrame cannot advance proof without another actual ProcessMotion: "+model);
   const auto defaultCpu=defaultView.submission;const auto defaultEpoch=candidate::g_windowsProcessingOwner.identity.epoch;
   auto repeated=make(false,"",owner,{});
   {candidate::FixtureProcessingScope scope(repeated);txCheck(candidate::inputCaptureWindowsProcessingAdopt(repeated)&&candidate::g_windowsProcessingOwner.identity.epoch==defaultEpoch&&ymcc::windows_motion::sameOwner(repeated.submission.processingLease,defaultCpu.processingLease)&&candidate::fixtureResetCount==0,"stable default frames reuse one CPU owner without reset: "+model);}
   candidate::g_gyroCalibRequested=true;candidate::fixtureWindowsFrame(owner,73,73,true);candidate::fixtureWindowsFrame(owner,73,73,true);
   txCheck(candidate::g_reviewedCalibrationContext.transaction.active,"default calibration is active before handoff: "+model);
   auto privateA=make(true,"PRIVATE-A",aCapture,aAlive);auto privateB=make(true,"PRIVATE-B",bCapture,bAlive);
   // Seed each record through the actual storage path with an explicitly controlled owner, before real adoption.
   for(auto* view:{&privateA,&privateB}){auto storageView=*view;storageView.processingOwnerAccepted=true;
    storageView.submission=ymcc::windows_motion::forProcessing(storageView.submission,std::make_shared<std::atomic<bool>>(true));
    candidate::FixtureProcessingScope scope(storageView);candidate::inputCaptureStoreCalibration(view->metadata.gyroDeviceId,view==&privateA?11:-7,view==&privateA?12:-8,view==&privateA?13:-9,10,1);}
   candidate::g_ymMadgQ[0]=.75f;candidate::g_autoCalibAttempts=2;candidate::g_gyroCalibProcessedTicks=55;
   const auto finishedCount=[&](){return std::count_if(candidate::fixtureEvents.begin(),candidate::fixtureEvents.end(),[](const json& e){return e["event"]=="gyro-calibration-result";});};
   const auto oldResults=finishedCount();
   {
    candidate::FixtureProcessingScope scope(privateA);
    txCheck(candidate::inputCaptureWindowsProcessingAdopt(privateA),"worker independently accepts private A from raw provenance: "+model);
    txCheck(!ymcc::windows_motion::current(defaultCpu)&&ymcc::windows_motion::current(rawDefault),"default CPU revoked while default native capture remains alive: "+model);
    txCheck(candidate::fixtureResetCount==1&&candidate::g_ymMadgQ[0]==0&&candidate::g_ymMadgQ[2]==-1&&candidate::g_autoCalibAttempts==0&&candidate::g_gyroCalibProcessedTicks==0,"handoff resets the real native state, overlay and source counters: "+model);
    txCheck(!candidate::g_reviewedCalibrationContext.transaction.active&&finishedCount()==oldResults+1,"handoff interrupts the real prior calibration exactly once: "+model);
    candidate::inputCaptureGyroCalibrationRestoreOnce();
    txCheck(offset()==std::array<float,3>{11,12,13},"A restores its own persisted offset before its first ProcessMotion: "+model);
   }
   const auto epochA=candidate::g_windowsProcessingOwner.identity.epoch;const auto cpuA=privateA.submission;
   auto rawA=make(true,"PRIVATE-A",aCapture,aAlive);
   auto noPad=candidate::fixtureWindowsFrame(owner,73,73,false,true,true,{1,2,3,0,0,1},0,&rawA);
   txCheck(noPad.processed&&!noPad.physical&&!noPad.output&&!noPad.outputStamp,"complete private input processes with no pad while output remains gated: "+model);
   txCheck(candidate::fixtureOffsetAtProcess==std::array<float,3>{11,12,13}&&candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("privateSource",false),"actual pinned ProcessMotion consumes A offset and publishes private evidence: "+model);
   txCheck(candidate::g_windowsProcessingOwner.identity.epoch==epochA&&candidate::fixtureResetCount==1&&finishedCount()==oldResults+1,"stable private sampling neither resets nor repeats calibration interruption: "+model);
   auto cacheA=std::atomic_load(&candidate::g_windowsProcessingReceipt);
   txCheck(cacheA&&cacheA->read(candidate::fixtureTick,candidate::currentPowerGeneration()).has_value(),"STA reader can obtain an immutable receipt without a native handle: "+model);
#if YMCC_FIXTURE_PUBLICATION_FENCE
   {const auto actualReceipt=cacheA?cacheA->read(candidate::fixtureTick,candidate::currentPowerGeneration()):std::nullopt;
    txCheck(actualReceipt.has_value()&&!ymcc::windows_motion::currentForOutput(actualReceipt->identity.source),"actual private processing alone cannot authorize publication: "+model);
    if(actualReceipt){const auto grant=ymcc::windows_motion::PublicationFence::committed(actualReceipt->identity.source);
     auto permitted=rawA;permitted.submission=ymcc::windows_motion::forPublication(permitted.submission,grant);
     const auto allowed=candidate::fixtureWindowsFrame(owner,73,73,true,true,true,{1,2,3,0,0,1},0,&permitted);
     txCheck(allowed.processed&&allowed.physical&&allowed.output&&allowed.outputStamp,"actual DLL receipt can authorize motion only on a fresh permitted frame: "+model);
     txCheck(candidate::g_windowsProcessingOwner.identity.epoch==epochA&&candidate::fixtureResetCount==1,"publication commitment does not reset actual GM or calibration ownership: "+model);
     txCheck(candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("sourcePublicationAllowed",false),"read-only actual processing evidence reports the current permitted frame: "+model);
     grant->revoke();txCheck(!candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("sourcePublicationAllowed",true)&&candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("currentSamplingAndProcessingLease",false),"read-only evidence observes output revocation immediately without revoking sampling: "+model);
     const auto withheld=candidate::fixtureWindowsFrame(owner,73,73,true,true,true,{1,2,3,0,0,1},0,&permitted);
     txCheck(withheld.processed&&withheld.physical&&!withheld.output&&!withheld.outputStamp,"revoking publication keeps actual sampling/GM and physical pad admission alive: "+model);
    }
   }
#endif
   candidate::g_gyroCalibRequested=true;candidate::fixtureWindowsFrame(owner,73,73,false,true,true,{1,2,3,0,0,1},0,&rawA);candidate::fixtureWindowsFrame(owner,73,73,false,true,true,{1,2,3,0,0,1},0,&rawA);
   txCheck(candidate::g_reviewedCalibrationContext.transaction.active,"private A starts an actual calibration transaction: "+model);
   const auto aResults=finishedCount();
   {candidate::FixtureProcessingScope scope(privateB);txCheck(candidate::inputCaptureWindowsProcessingAdopt(privateB),"worker accepts B as a separate private pair: "+model);candidate::inputCaptureGyroCalibrationRestoreOnce();
    txCheck(offset()==std::array<float,3>{-7,-8,-9}&&candidate::fixtureResetCount==2,"B restores only its own offset after the real native reset: "+model);
    txCheck(!ymcc::windows_motion::current(cpuA)&&(!cacheA||!cacheA->read(candidate::fixtureTick,candidate::currentPowerGeneration()))&&aAlive->load(),"A retained submissions and cached evidence are revoked without closing its native lease: "+model);
    txCheck(finishedCount()==aResults+1&&!candidate::g_reviewedCalibrationContext.transaction.active,"A to B interrupts the actual A transaction once: "+model);}
   const auto cpuB=privateB.submission;
   auto returned=make(false,"",owner,{});
   {candidate::FixtureProcessingScope scope(returned);txCheck(candidate::inputCaptureWindowsProcessingAdopt(returned),"worker can return to the default capture: "+model);candidate::inputCaptureGyroCalibrationRestoreOnce();
    txCheck(offset()==std::array<float,3>{1,2,3}&&candidate::fixtureResetCount==3&&!ymcc::windows_motion::current(cpuB)&&bAlive->load(),"default return restores its offset and retires only B CPU ownership: "+model);}
   // Re-adopt a current raw private frame and execute real processing for each failure boundary.
   for(const std::string failure:{"sequence","native-close","power","session-reset"}){
    auto trial=make(true,"PRIVATE-A",aCapture,aAlive);aAlive->store(true);
    candidate::FixtureProcessingScope scope(trial);txCheck(candidate::inputCaptureWindowsProcessingAdopt(trial),"trial adoption for "+failure+": "+model);candidate::inputCaptureGyroCalibrationRestoreOnce();
    txCheck(candidate::inputCaptureWindowsProcessingProcess(trial,1,2,3,0,0,1,.008f),"actual pinned processing before "+failure+": "+model);
    if(failure=="sequence")++trial.parts[0].sequence;
    if(failure=="native-close")aAlive->store(false);
    if(failure=="power")++candidate::mockCapabilityPower;
    if(failure=="session-reset"){candidate::g_gmSessionResetRequested=true;candidate::inputCaptureGyroCalibrationTick(&trial.submission,&trial.metadata);}
    candidate::inputCaptureWindowsProcessingNoteFrame(trial,true);
    txCheck(!candidate::inputCaptureWindowsProcessingReceiptSnapshot().value("currentSamplingAndProcessingLease",false),"no processed receipt survives "+failure+": "+model);
    candidate::inputCaptureWindowsProcessingRetire(false);aAlive->store(true);candidate::mockCapabilityPower=7;
   }
   ++scenarios;
  }
