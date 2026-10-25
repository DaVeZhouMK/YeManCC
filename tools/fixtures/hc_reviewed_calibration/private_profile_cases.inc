  {
   seed(row,"winrt",false);candidate::g_gmProcessedThisTick=true;
   const auto defaultMeta=candidate::g_sensorMeta;const auto defaultKey=candidate::inputCaptureCalibrationStorageKey(defaultMeta.gyroDeviceId);
   candidate::inputCaptureStoreCalibration(defaultMeta.gyroDeviceId,1,2,3,10,1);
   const auto effectiveRecipe=candidate::inputCaptureCandidateMatrixReceipt(defaultMeta);
   const auto capability=candidate::inputCaptureMotionCapabilityReceipt(defaultMeta,false);
   const auto expectedModel=candidate::inputCaptureWindowsSelectionEvidence(json::object(),capability,effectiveRecipe,candidate::fixtureTick).model;
   if(!expectedModel.valid())std::ofstream("profile-model-gap-"+model+".json")<<json{{"capability",capability},{"effectiveRecipe",effectiveRecipe},{"modelValid",false}}.dump(2);
   txCheck(expectedModel.valid(),"actual capability and effective-recipe producer supplies the private profile: "+model);
   auto capture=std::make_shared<candidate::MockBindingSink>(),choice=std::make_shared<candidate::MockBindingSink>();auto alive=std::make_shared<std::atomic<bool>>(true);
   // Storage-only cases explicitly control CPU acceptance; the worker suite acquires its real owner.
   auto processing=std::make_shared<std::atomic<bool>>(true);
   const auto stamp=ymcc::windows_motion::forProcessing(ymcc::windows_motion::guarded(ymcc::windows_motion::retain(capture,73),ymcc::windows_motion::retain(choice,73),alive),processing);
   const auto makeView=[&](candidate::InputCaptureSensorMeta meta){candidate::WindowsSensorView v;v.metadata=std::move(meta);v.privateSource=v.processingOwnerAccepted=v.coherent=true;v.submission=stamp;v.profile=ymcc::sensors::selection::fingerprint(expectedModel);v.calibrationKey=ymcc::sensors::selection::calibrationKey(expectedModel,{v.metadata.gyroDeviceId,v.metadata.accelDeviceId},ymcc::sensors::selection::TransformPolicy::ExplicitCurrentModel);return v;};
   auto viewA=makeView(defaultMeta);candidate::FixtureProcessingScope scopeA(viewA);
   const auto keyA=candidate::inputCaptureCalibrationStorageKey(defaultMeta.gyroDeviceId);
   txCheck(!keyA.empty()&&keyA!=defaultKey,"explicit policy cannot reuse the same-pair default storage key: "+model);
   candidate::inputCaptureStoreCalibration(defaultMeta.gyroDeviceId,11,12,13,10,1);auto saved=read();
   txCheck(saved.contains(defaultKey)&&saved[defaultKey]["xOffset"]==1&&saved.contains(keyA)&&saved[keyA]["xOffset"]==11,"actual store keeps default and explicit records separate: "+model);
   candidate::inputCaptureGyroCalibrationRestoreOnce();txCheck(offset()==std::array<float,3>{11,12,13},"actual DLL restores the current explicit profile: "+model);
   auto sourceA=candidate::inputCaptureReviewedCalibrationSource(&stamp,&viewA.metadata);
   txCheck(sourceA.calibrationKey==keyA&&candidate::inputCaptureReviewedCalibrationSourceMatches(sourceA,&stamp,&viewA.metadata,viewA.metadata),"actual reviewed source carries the explicit profile key: "+model);
   sourceA.calibrationKey=defaultKey;txCheck(!candidate::inputCaptureReviewedCalibrationSourceMatches(sourceA,&stamp,&viewA.metadata,viewA.metadata),"equal sample and metadata cannot conceal a different calibration profile key: "+model);
   auto metaB=defaultMeta;metaB.accelDeviceId="ACPI#OTHER-ACCEL#1#{ACCEL}";auto viewB=makeView(metaB);
   {candidate::FixtureProcessingScope scopeB(viewB);const auto keyB=candidate::inputCaptureCalibrationStorageKey(metaB.gyroDeviceId);
    txCheck(!keyB.empty()&&keyB!=keyA,"same gyro with another accel gets another key: "+model);
    candidate::inputCaptureGyroCalibrationRestoreOnce();txCheck(offset()==std::array<float,3>{0,0,0}&&!candidate::g_autoCalibTrusted.load(),"missing second-pair record cannot borrow the first-pair offset: "+model);
    candidate::inputCaptureStoreCalibration(metaB.gyroDeviceId,-7,-8,-9,10,1);
    txCheck(read().contains(keyB)&&read()[keyB]["xOffset"]==-7,"second pair persists its own record: "+model);
   }
   candidate::inputCaptureGyroCalibrationRestoreOnce();txCheck(offset()==std::array<float,3>{11,12,13},"returning to the original explicit pair restores its own offset: "+model);
   {candidate::FixtureProcessingScope scopeB(viewB);candidate::inputCaptureGyroCalibrationRestoreOnce();txCheck(offset()==std::array<float,3>{-7,-8,-9},"actual second-pair offset restores after a real profile-key transition: "+model);}
   candidate::inputCaptureGyroCalibrationRestoreOnce();
   {auto lower=viewA;for(auto* id:{&lower.metadata.gyroDeviceId,&lower.metadata.accelDeviceId})for(auto& c:*id)c=(char)std::tolower((unsigned char)c);candidate::FixtureProcessingScope scopedLower(lower);candidate::inputCaptureGyroCalibrationRestoreOnce();txCheck(candidate::inputCaptureCalibrationStorageKey(lower.metadata.gyroDeviceId)==keyA&&offset()==std::array<float,3>{11,12,13},"Windows ID casing keeps the key and identity compatible: "+model);}
   for(const std::string why:{"profile","transaction-key","not-owned","api"}){auto bad=viewA;if(why=="profile")bad.profile+="other";if(why=="transaction-key")bad.calibrationKey=defaultKey;if(why=="not-owned")bad.processingOwnerAccepted=false;if(why=="api")bad.metadata.gyroFamily=bad.metadata.accelFamily="sensorapi";candidate::FixtureProcessingScope badScope(bad);const auto prior=read();
    txCheck(candidate::inputCaptureCalibrationStorageKey(bad.metadata.gyroDeviceId).empty()&&candidate::inputCaptureExtendedCalibrationSourceIdentity(bad.metadata.gyroDeviceId).is_null(),"invalid private profile never falls back to default identity: "+why+":"+model);
    candidate::inputCaptureStoreCalibration(bad.metadata.gyroDeviceId,99,99,99,10,1);txCheck(read()==prior,"invalid private profile cannot overwrite any record: "+why+":"+model);
   }
   alive->store(false);const auto prior=read();candidate::inputCaptureStoreCalibration(defaultMeta.gyroDeviceId,99,99,99,10,1);candidate::inputCaptureGyroCalibrationRestoreOnce();
   txCheck(candidate::inputCaptureCalibrationStorageKey(defaultMeta.gyroDeviceId).empty()&&read()==prior&&offset()==std::array<float,3>{0,0,0},"retired explicit profile cannot read or overwrite a default record: "+model);
  }
