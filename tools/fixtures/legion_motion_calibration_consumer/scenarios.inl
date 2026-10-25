// Scenario orchestration only; request/advance/store/abort are extracted from main.
static json readPrivateCalibration(){std::ifstream f(calibrationPath,std::ios::binary);json j;f>>j;return j;}
static MotionSample readyAt(uint64_t tick){clockMs=tick;auto s=sample(tick);CHECK(publish(s));return s;}
static std::optional<cm::MotionResult> tickAt(uint64_t tick){const auto s=readyAt(tick);return consume(s,static_cast<double>(tick)/1000.0);}
static unsigned effectCount(const char* event){unsigned n=0;for(const auto& e:effects)if(e.value("case",std::string{})==activeCase&&e.value("event",std::string{})==event)++n;return n;}
static json lastResult(){for(auto i=calibrationEvents.rbegin();i!=calibrationEvents.rend();++i)
    if(i->value("event",std::string{})=="gyro.calibrate.result")return i->at("value");return json::object();}
static void resultIsFailure(){const auto r=lastResult();CHECK(r.is_object()&&r.contains("saved")&&r.value("saved",true)==false);
    CHECK(r.contains("ok")&&!r.value("ok",true));CHECK(r.contains("applied")&&!r.value("applied",true));}
static void missingAndStale(){CHECK(!inputCaptureControllerCalibrationRequest().value("ok",true));CHECK(!g_controllerCalibrationHasRequest.load());
    CHECK(inputCaptureControllerCalibrationStatus().value("status","")=="uncalibrated");
    const auto s=readyAt(100);clockMs=100+ymcc::kVirtualOutputSourceWindowMaxAgeMs+1;
    CHECK(!inputCaptureControllerCalibrationRequest().value("ok",true));CHECK(!g_controllerCalibrationRequest);
    clockMs=100;g_powerGeneration.fetch_add(1);CHECK(!inputCaptureControllerCalibrationRequest().value("ok",true));
    CHECK(createCalls==0&&processCalls==0&&effectCount("file-create")==0);(void)s;}
static void routeAndBusy(){for(const auto& name:familyTopology.at("rejectedModels")){g_machineIdentity.deviceClass=name.get<std::string>();
    CHECK(!inputCaptureControllerCalibrationRequest().value("ok",true));}
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet";readyAt(100);
    auto ack=inputCaptureControllerCalibrationRequest();CHECK(ack.value("ok",false));CHECK(ack.value("pollWindowMs",0)==5000);
    CHECK(ack.value("provider","")=="controller");const auto id=ack.value("requestId",uint64_t{0});CHECK(id!=0);
    for(unsigned i=0;i<3;++i){clockMs=101+i;const auto duplicate=inputCaptureControllerCalibrationRequest();
        CHECK(!duplicate.value("ok",true)&&duplicate.value("reason","")=="in-progress");
        CHECK(g_controllerCalibrationRequest&&g_controllerCalibrationRequest->id==id&&g_controllerCalibrationRequest->requestedAt==100);}
    CHECK(inputCaptureControllerCalibrationStatus().value("status","")=="calibrating");CHECK(createCalls==0&&processCalls==0);
    inputCaptureControllerCalibrationAbort();resultIsFailure();CHECK(!g_controllerCalibrationHasRequest.load());}
static void requestBeforeHandle(){readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));CHECK(createCalls==0);
    CHECK(!tickAt(100));CHECK(createCalls==1&&processCalls==1);auto r=lastResult();CHECK(r.value("saved",false)&&r.value("persisted",false));
    CHECK(!g_controllerCalibrationRequest&&!g_controllerCalibrationTransaction);CHECK(tickAt(101).has_value());
    CHECK(inputCaptureControllerCalibrationStatus().value("trusted",false));}
static void preRequestSamples(){clockMs=100;const auto old=sample(99);CHECK(publish(old));CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));
    CHECK(!inputCaptureLegionMotion(old,axes(old),0.1));CHECK(createCalls==1&&processCalls==0&&effectCount("calibration-reset")==0);
    CHECK(g_controllerCalibrationTransaction&&g_controllerCalibrationTransaction->active());
    CHECK(!inputCaptureLegionMotion(old,axes(old),0.101));CHECK(processCalls==0);
    CHECK(!tickAt(100));CHECK(processCalls==1&&lastResult().value("saved",false));}
static void repeatedReceiptAndTimeout(){controllerConfidence=0.5f;readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));
    CHECK(!tickAt(100));CHECK(processCalls==1);CHECK(!tickAt(100));CHECK(processCalls==1);
    CHECK(!tickAt(101));CHECK(processCalls==2);CHECK(!tickAt(5099));CHECK(g_controllerCalibrationTransaction);
    CHECK(!tickAt(5100));CHECK(!g_controllerCalibrationTransaction);resultIsFailure();CHECK(lastResult().value("reason","")=="timeout");
    CHECK(effectCount("file-replace-attempt")==0&&currentHandle()->mode==0);}
static void persistenceRecords(){json original={{"gyroDeviceId","WINDOWS-only"},{"xOffset",991},{"yOffset",992},{"zOffset",993},
    {"weight",88},{"confidence",0.75},{"unrelated",{{"keep",true}}}};
    original["controllerMotionCalibration"]["other-physical"]["1"]=record(8,7,6,5);writeCalibration(original);
    const auto physicalId=identity();CHECK(inputCaptureControllerCalibrationStore({physicalId,0},{1,2,3,10},1));
    CHECK(inputCaptureControllerCalibrationStore({physicalId,1},{11,12,13,10},1));const auto stored=readPrivateCalibration();
    for(const auto* key:{"gyroDeviceId","xOffset","yOffset","zOffset","weight","confidence","unrelated"})CHECK(stored.at(key)==original.at(key));
    CHECK(stored.at("controllerMotionCalibration").at("other-physical")==original.at("controllerMotionCalibration").at("other-physical"));
    for(unsigned side=0;side<2;++side){const auto loaded=inputCaptureControllerCalibration({physicalId,side});CHECK(loaded&&loaded->x==(side?11.f:1.f));}
    CHECK(stored.at("controllerMotionCalibration").size()==2&&stored.at("controllerMotionCalibration").at(physicalId).size()==2);
    CHECK(g_gm==windowsSentinel&&createCalls==0);
    // Positive right-side transaction uses the actual consumer, not Store alone.
    CHECK(legionMotionSelectSensorIndex(1,g_legionMotionSelectionRevision+1));CHECK(!publish(sample(100)));readyAt(101);
    CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));CHECK(!tickAt(101));
    CHECK(lastResult().value("saved",false)&&lastResult().value("sensorIndex",99u)==1);
    const auto finalRecords=readPrivateCalibration();
    CHECK(finalRecords.at("controllerMotionCalibration").at(physicalId).at("0")==stored.at("controllerMotionCalibration").at(physicalId).at("0"));
    CHECK(finalRecords.at("controllerMotionCalibration").at(physicalId).contains("1")&&finalRecords.at("controllerMotionCalibration").at(physicalId).at("1").at("xOffset")==controllerCandidate.x);
    CHECK(finalRecords.at("gyroDeviceId")==original.at("gyroDeviceId")&&g_gm==windowsSentinel);
}
static void atomicFailures(){const json initial={{"gyroDeviceId","Windows-preserve"},{"xOffset",81},{"keep",{1,2,3}}};
    for(unsigned fault=1;fault<=6;++fault){writeCalibration(initial);fileFault=fault;
        CHECK(!inputCaptureControllerCalibrationStore({identity(),0},{1,2,3,10},1));CHECK(readPrivateCalibration()==initial);
        CHECK(calibrationFiles.empty());for(const auto& e:std::filesystem::directory_iterator(calibrationPath.parent_path()))CHECK(e.path().extension()!=L".tmp");}
    fileFault=0;CHECK(inputCaptureControllerCalibrationStore({identity(),0},{1,2,3,10},1));CHECK(readPrivateCalibration().at("keep")==initial.at("keep"));
    const auto calls=effectCount("file-create");CHECK(!inputCaptureControllerCalibrationWriteAtomic(calibrationPath.wstring(),std::string((1u<<20)+1,'x')));
    CHECK(effectCount("file-create")==calls);}
static void invalidPersistence(){const auto k=cm::CalibrationKey{identity(),0};const auto nan=std::numeric_limits<float>::quiet_NaN();
    for(const auto confidence:{0.99f,0.f,-1.f,1.1f,nan})CHECK(!inputCaptureControllerCalibrationStore(k,{1,2,3,10},confidence));
    CHECK(!inputCaptureControllerCalibrationStore(k,{nan,2,3,10},1));CHECK(!inputCaptureControllerCalibrationStore(k,{1,2,3,9},1));
    CHECK(!inputCaptureControllerCalibrationStore({"",0},{1,2,3,10},1));CHECK(!inputCaptureControllerCalibrationStore({identity(),2},{1,2,3,10},1));
    writeCalibration(json::array({1,2}));CHECK(!inputCaptureControllerCalibrationStore(k,{1,2,3,10},1));CHECK(readPrivateCalibration().is_array());
    writeCalibration({{"controllerMotionCalibration","malformed"}});CHECK(!inputCaptureControllerCalibrationStore(k,{1,2,3,10},1));
    CHECK(effectCount("file-create")==0);}
static void successOrdering(){readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));CHECK(!tickAt(100));
    const auto r=lastResult();CHECK(r.value("saved",false)&&r.value("persisted",false)&&r.value("applied",false));
    CHECK(r.value("provider","")=="controller"&&r.value("source","")=="manual");CHECK(r.at("offset").at("x")==controllerCandidate.x);
    CHECK(currentHandle()->offset.x==controllerCandidate.x&&currentHandle()->mode==0);
    std::size_t saved=effects.size(),applied=effects.size();for(std::size_t i=0;i<effects.size();++i){const auto& e=effects[i];
        if(e.value("case",std::string{})!=activeCase)continue;if(e.value("event",std::string{})=="file-replaced")saved=i;
        if(e.value("event",std::string{})=="offset"&&e.value("x",0.f)==controllerCandidate.x)applied=i;}
    CHECK(saved<applied&&applied<effects.size());CHECK(readPrivateCalibration()["controllerMotionCalibration"][identity()]["0"]["xOffset"]==controllerCandidate.x);}
static void failedSaveRollback(){const json initial={{"xOffset",78},{"gyroDeviceId","Windows"}};writeCalibration(initial);fileFault=6;
    readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));CHECK(!tickAt(100));resultIsFailure();
    CHECK(lastResult().value("reason","")=="persistence-failed");CHECK(!lastResult().value("persisted",true));
    CHECK(readPrivateCalibration()==initial);CHECK(currentHandle()->offset.x==0&&currentHandle()->offset.weight==0&&currentHandle()->mode==0);
    CHECK(g_legionMotionCalibration&&g_legionMotionCalibration->weight==0);}
static void lowAndInvalidConfidence(){controllerConfidence=0.99f;readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));
    CHECK(!tickAt(100));CHECK(g_controllerCalibrationTransaction&&effectCount("file-create")==0);
    controllerConfidence=std::numeric_limits<float>::quiet_NaN();CHECK(!tickAt(101));resultIsFailure();CHECK(lastResult().value("reason","")=="invalid-confidence");}
static void invalidOffset(){controllerCandidate.x=std::numeric_limits<float>::quiet_NaN();readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));
    CHECK(!tickAt(100));resultIsFailure();CHECK(lastResult().value("reason","")=="invalid-offset");CHECK(effectCount("file-create")==0);}
static void retireDimension(unsigned cause){if(cause==0)g_powerGeneration.fetch_add(1);else if(cause==1)g_hidResolveCacheGeneration.fetch_add(1);
    else if(cause==2)legionMotionRevoke();else if(cause==3)CHECK(legionMotionSelectSensorIndex(1-g_legionMotionSensorIndex.load(),g_legionMotionSelectionRevision+1));
    else if(cause==4)selectPhysical(physical("USB\\NEW-PHYSICAL"));else g_inputCaptureStop.store(true);}
static void sourceRetirement(bool active){controllerConfidence=0.5f;
    for(unsigned cause=0;cause<6;++cause){const uint64_t now=100+cause*100;const auto old=readyAt(now);
        CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));if(active)CHECK(!consume(old,static_cast<double>(now)/1000));
        const auto oldKey=inputCaptureControllerCalibrationSourceKey(old);void* oldHandle=sessionHandleFor(oldKey);
        retireDimension(cause);CHECK(!inputCaptureLegionMotion(old,axes(old),static_cast<double>(now+1)/1000));
        if(cause==5)CHECK(!g_controllerCalibrationRequest);
        // Missing owner + pending cannot be processed under a successor key either.
        g_inputCaptureStop.store(false);auto next=readyAt(now+1);CHECK(!inputCaptureLegionMotion(next,axes(next),static_cast<double>(now+1)/1000)||!g_controllerCalibrationRequest);
        CHECK(!g_controllerCalibrationRequest&&!g_controllerCalibrationTransaction);CHECK(!lastResult().value("saved",false));
        if(oldHandle){CHECK(!handles.at(oldHandle).alive);CHECK(sessionHandleFor(oldKey)==nullptr);}
        else if(!(inputCaptureControllerCalibrationSourceKey(next)==oldKey))CHECK(sessionHandleFor(oldKey)==nullptr);CHECK(effectCount("file-create")==0);
        inputCaptureLegionMotionSessionRevoke();legionMotionRevoke();}
}
static void pendingSourceRetirement(){sourceRetirement(false);}
static void activeSourceRetirement(){sourceRetirement(true);}
static void requestReplacement(){controllerConfidence=0.5f;readyAt(100);auto first=inputCaptureControllerCalibrationRequest();CHECK(first.value("ok",false));
    CHECK(!tickAt(100));const auto firstId=first.value("requestId",uint64_t{0});readyAt(6600);auto second=inputCaptureControllerCalibrationRequest();
    CHECK(second.value("ok",false));const auto secondId=second.value("requestId",uint64_t{0});CHECK(firstId!=secondId);
    const auto eventsBefore=calibrationEvents.size();CHECK(!tickAt(6600));CHECK(g_controllerCalibrationRequest&&g_controllerCalibrationRequest->id==secondId);
    CHECK(g_controllerCalibrationHasRequest.load()&&g_controllerCalibrationActiveRequest&&g_controllerCalibrationActiveRequest->id==secondId);
    bool evidence=false;for(const auto& entry:lifecycle)if(entry.value("event",std::string{})=="controller-gyro-calibration-result"){
        const auto& r=entry.at("value");if(r.value("requestId",uint64_t{0})==firstId&&r.value("reason","")=="request-replaced")evidence=true;}
    CHECK(evidence);for(std::size_t i=eventsBefore;i<calibrationEvents.size();++i)CHECK(calibrationEvents[i].at("value").value("requestId",uint64_t{0})!=firstId);
    controllerConfidence=1;CHECK(!tickAt(6601));CHECK(lastResult().value("saved",false)&&lastResult().value("requestId",uint64_t{0})==secondId);}
static void loggingFailureStillDestroys(){controllerConfidence=0.5f;readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));CHECK(!tickAt(100));
    CHECK(g_controllerCalibrationTransaction&&liveHandles()==1);const auto deleted=deleteCalls;throwCalibrationLog=true;
    inputCaptureLegionMotionSessionRevoke();throwCalibrationLog=false;CHECK(deleteCalls==deleted+1&&liveHandles()==0);
    CHECK(!g_controllerCalibrationTransaction&&!g_controllerCalibrationActiveRequest&&!g_legionMotionSession);CHECK(effectCount("file-create")==0);}
static void createFailureCancels(){readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));failCreate=true;
    CHECK(!tickAt(100));resultIsFailure();CHECK(!g_controllerCalibrationHasRequest.load()&&!g_controllerCalibrationRequest);CHECK(liveHandles()==0&&effectCount("file-create")==0);}
static void missingReadCancels(){readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));const auto reads=g_gmGetGravity;g_gmGetGravity=nullptr;
    CHECK(!tickAt(100));g_gmGetGravity=reads;resultIsFailure();CHECK(!g_controllerCalibrationRequest&&createCalls==0&&effectCount("file-create")==0);}
static void missingCalibrationCallback(){readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));const auto cb=g_gmResetCal;g_gmResetCal=nullptr;
    CHECK(!tickAt(100));g_gmResetCal=cb;resultIsFailure();CHECK(!g_controllerCalibrationRequest&&processCalls==0&&effectCount("file-create")==0);}
static void retirementDuringReplace(){readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));
    afterReplaceHook=[](){g_hidResolveCacheGeneration.fetch_add(1);};CHECK(!tickAt(100));afterReplaceHook={};resultIsFailure();
    CHECK(lastResult().value("persisted",false));CHECK(lastResult().value("reason","")=="source-retired");CHECK(currentHandle()->offset.weight==0);
    CHECK(g_legionMotionCalibration&&g_legionMotionCalibration->weight==0);}
static void abortAndStop(){controllerConfidence=0.5f;readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));CHECK(!tickAt(100));
    g_inputCaptureStop.store(true);CHECK(!inputCaptureLegionMotion(std::nullopt,{},0.2));resultIsFailure();
    CHECK(!g_controllerCalibrationTransaction&&liveHandles()==0);CHECK(effectCount("file-create")==0);}

static void savedTrustRestoredAcrossEpoch(){readyAt(100);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));CHECK(!tickAt(100));
    CHECK(lastResult().value("saved",false));const auto oldKey=*g_legionMotionSessionKey;void* oldHandle=sessionHandleFor(oldKey);
    inputCaptureLegionMotionSessionRevoke();g_controllerCalibrationTrustedKey.reset();clockMs=101;legionMotionRevoke();
    CHECK(!publish(sample(101))); // An equal queue tick cannot prove post-cutover membership.
    const auto before=calibrationPathCalls;CHECK(tickAt(102).has_value());CHECK(calibrationPathCalls==before+1);
    CHECK(currentHandle()->offset.x==controllerCandidate.x&&currentHandle()->offset.weight==10);
    CHECK(g_legionMotionSessionKey&&!(*g_legionMotionSessionKey==oldKey)&&sessionHandleFor(oldKey)==nullptr&&!handles.at(oldHandle).alive);
    const auto status=inputCaptureControllerCalibrationStatus();CHECK(status.value("trusted",false)&&status.value("status","")=="already-calibrated");
    const auto eventCount=calibrationEvents.size();const auto reads=calibrationPathCalls;CHECK(tickAt(103));CHECK(calibrationPathCalls==reads&&calibrationEvents.size()==eventCount);}
static void legacyAndMismatchedTrust(){json root;root["controllerMotionCalibration"][identity()]["0"]=record(9,8,7,10);writeCalibration(root);
    CHECK(tickAt(100));CHECK(currentHandle()->offset.x==9);CHECK(!inputCaptureControllerCalibrationStatus().value("trusted",true));
    for(unsigned stage=0;stage<3;++stage){root["controllerMotionCalibration"][identity()]["0"]["confidence"]=stage==0?json(0.999f):stage==1?json(0.99f):json("1.0");
        writeCalibration(root);inputCaptureLegionMotionSessionRevoke();clockMs=101+stage*2;legionMotionRevoke();
        CHECK(!publish(sample(clockMs)));CHECK(tickAt(clockMs+1));
        CHECK(currentHandle()->offset.x==9&&!inputCaptureControllerCalibrationStatus().value("trusted",true));}
}
static void fourModelsCalibrate(){unsigned step=0;for(const auto& model:familyTopology.at("models")){
    const auto boundary=100+step*10;clockMs=boundary;
    g_machineIdentity.deviceClass=model.at("name").get<std::string>();auto physicalId=physical("USB\\MODEL-"+std::to_string(step));
    physicalId.vendorId=model.at("vendorId").get<USHORT>();physicalId.productId=model.at("productIds").at(0).get<USHORT>();selectPhysical(physicalId);
    ++step;CHECK(!publish(sample(boundary)));const auto now=boundary+1;readyAt(now);
    CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));CHECK(!tickAt(now));
    CHECK(lastResult().value("saved",false)&&lastResult().value("sensorIndex",99u)==0);inputCaptureLegionMotionSessionRevoke();clockMs=now+1;legionMotionRevoke();}
}
