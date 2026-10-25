// Independent scheduled inputs and explicit expected effects; all behavior is production code.
static unsigned acCount(const char* event){unsigned n=0;for(const auto& e:effects)if(e.value("case",std::string{})==activeCase&&e.value("event",std::string{})==event)++n;return n;}
static unsigned acModeEvents(){unsigned n=0;for(const auto& row:trace)if(row.value("kind",std::string{})=="controller-motion-calibration-mode"){++n;CHECK(row.at("modeCallbackCompleted")==true);CHECK(row.at("nativeModeReadback")==false);}return n;}
static json acLastResult(){for(auto i=calibrationEvents.rbegin();i!=calibrationEvents.rend();++i)if(i->value("event",std::string{})=="gyro.calibrate.result")return i->at("value");return json::object();}
static bool acRetireDuringMode=false;
static void acMode(void* h,int mode){setMode(h,mode);if(acRetireDuringMode&&mode==3){acRetireDuringMode=false;g_powerGeneration.fetch_add(1);}}
static void acGravity(void* h,float* x,float* y,float* z){effects.push_back({{"case",activeCase},{"event","alternate-gravity"}});getGravity(h,x,y,z);}
static void acReset(void* h){effects.push_back({{"case",activeCase},{"event","alternate-reset"}});controllerResetCalibration(h);}
static void acRestoreCallbacks(){g_controllerGmCreate=&createController;g_controllerGmDelete=&deleteController;g_gmSetMode=&setMode;
 g_gmSetOffset=&setOffset;g_gmProcess=&processController;g_gmGetCal=&getCal;g_gmGetGravity=&getGravity;
 g_gmPlayer=&getPlayer;g_gmWorld=&getWorld;g_gmResetCal=&controllerResetCalibration;g_gmGetConf=&controllerGetConfidence;g_gmGetOffset=&controllerGetOffset;acRetireDuringMode=false;}
static MotionSample acReady(uint64_t tick){clockMs=tick;const auto s=sample(tick);CHECK(publish(s));return s;}
static std::optional<cm::MotionResult> acTick(uint64_t tick){const auto s=acReady(tick);return consume(s,static_cast<double>(tick)/1000.0);}
static uint64_t acNext(){return std::max(clockMs+1,g_legionMotionEpochStartedAt.load()+1);}
static void acModeIs(int expected){const auto* h=currentHandle();CHECK(h!=nullptr);if(h)CHECK(h->mode==expected);}
static void acCacheIs(std::optional<int> expected){CHECK(g_legionMotionSession&&g_legionMotionSessionKey);if(g_legionMotionSession&&g_legionMotionSessionKey)CHECK(g_legionMotionSession->calibrationModeFor(*g_legionMotionSessionKey)==expected);}
static json acRead(){std::ifstream input(calibrationPath,std::ios::binary);json v;input>>v;return v;}
static json acBegin(bool automatic=true){g_realStickTest.autoCalibrate=automatic;CHECK(acTick(acNext()));
 const auto t=acNext();acReady(t);const auto ack=inputCaptureControllerCalibrationRequest();CHECK(ack.value("ok",false));
 controllerConfidence=0.5f;CHECK(!acTick(t));CHECK(g_controllerCalibrationTransaction&&g_controllerCalibrationActiveRequest);
 acModeIs(3);acCacheIs(std::nullopt);return ack;}
static void acIdleRestores(bool automatic){g_realStickTest.autoCalibrate=automatic;const auto reads=calibrationPathCalls,writes=acCount("file-create"),resets=acCount("calibration-reset"),offsets=acCount("offset"),modes=acCount("mode");
 CHECK(acTick(acNext()));acModeIs(automatic?3:0);acCacheIs(automatic?3:0);
 CHECK(acCount("mode")==modes+1);CHECK(calibrationPathCalls==reads&&acCount("file-create")==writes);
 CHECK(acCount("calibration-reset")==resets&&acCount("offset")==offsets);}
static void acFamilies(){unsigned i=0;for(const auto& model:familyTopology.at("models")){
 for(unsigned side=0;side<(model.at("rightAllowed").get<bool>()?2u:1u);++side){
  inputCaptureLegionMotionSessionRevoke();clockMs+=10;g_machineIdentity.deviceClass=model.at("name").get<std::string>();
  auto p=physical("USB\\AUTO-MODEL-"+std::to_string(++i));p.vendorId=model.at("vendorId").get<USHORT>();p.productId=model.at("productIds").at(0).get<USHORT>();selectPhysical(p);selectSensor(side);
  for(const bool automatic:{false,true}){g_realStickTest.autoCalibrate=automatic;const auto writes=acCount("file-create");CHECK(acTick(acNext()));
   CHECK(legionMotionSelectedSensorIndex()==side);acModeIs(automatic?3:0);acCacheIs(automatic?3:0);CHECK(acCount("file-create")==writes);}
  CHECK(acCount("calibration-reset")==0);
 }
}CHECK(i==6);CHECK(g_gm==windowsSentinel);}
static void acSessionCache(){
 cm::Session session;const cm::Key key{identity(),currentPowerGeneration(),g_hidResolveCacheGeneration.load(),epoch(),0};
 const cm::Ops ops{g_controllerGmCreate,g_controllerGmDelete,g_gmSetMode,g_gmSetOffset,g_gmProcess};
 const cm::ReadOps reads{g_gmGetCal,g_gmGetGravity,g_gmPlayer,g_gmWorld};
 CHECK(session.select(key,ops,reads,{},0.1));CHECK(session.calibrationModeFor(key)==0);const auto modes=acCount("mode");
 CHECK(session.reconcileCalibrationMode(key,false));CHECK(acCount("mode")==modes);
 CHECK(session.reconcileCalibrationMode(key,true));CHECK(session.calibrationModeFor(key)==3);CHECK(acCount("mode")==modes+1);
 CHECK(session.select(key,ops,reads,{},0.2));CHECK(session.calibrationModeFor(key)==3);
 CHECK(session.reconcileCalibrationMode(key,true));CHECK(acCount("mode")==modes+1);
 session.invalidateCalibrationMode();CHECK(!session.calibrationModeFor(key));CHECK(acCount("mode")==modes+1);
 CHECK(session.reconcileCalibrationMode(key,true));CHECK(acCount("mode")==modes+2);
 auto stale=key;++stale.captureEpoch;CHECK(!session.calibrationModeFor(stale));CHECK(!session.reconcileCalibrationMode(stale,false));CHECK(acCount("mode")==modes+2);
 const cm::ReadOps replacement{g_gmGetCal,&acGravity,g_gmPlayer,g_gmWorld};CHECK(session.select(key,ops,replacement,{},0.3));CHECK(session.calibrationModeFor(key)==0);
 session.revoke();const auto after=acCount("mode");CHECK(!session.calibrationModeFor(key));CHECK(!session.reconcileCalibrationMode(key,true));CHECK(acCount("mode")==after);
}
static void acSteady(){g_realStickTest.autoCalibrate=true;const auto s=acReady(100);CHECK(consume(s,0.1));acModeIs(3);acCacheIs(3);
 const auto reads=calibrationPathCalls,creates=createCalls,modes=acCount("mode"),offsets=acCount("offset"),resets=acCount("calibration-reset");const auto modeEvents=acModeEvents();
 for(unsigned i=1;i<=5;++i){clockMs=100+i;CHECK(consume(s,0.1+i*0.001));acModeIs(3);}
 CHECK(acCount("mode")==modes&&calibrationPathCalls==reads&&createCalls==creates);CHECK(acModeEvents()==modeEvents);CHECK(acCount("offset")==offsets&&acCount("calibration-reset")==resets&&acCount("file-create")==0);
 g_realStickTest.autoCalibrate=false;CHECK(acTick(acNext()));acModeIs(0);acCacheIs(0);CHECK(acCount("mode")==modes+1);
 CHECK(acTick(acNext()));CHECK(acCount("mode")==modes+1);g_realStickTest.autoCalibrate=true;CHECK(acTick(acNext()));acModeIs(3);CHECK(acCount("mode")==modes+2);
 CHECK(acCount("calibration-reset")==resets&&acCount("offset")==offsets&&calibrationPathCalls==reads&&createCalls==creates&&acCount("file-create")==0);CHECK(acModeEvents()==modeEvents+2);
}
static void acSuccess(){const json initial={{"gyroDeviceId","Windows-unmodified"},{"xOffset",72},{"unrelated",{1,2,3}}};writeCalibration(initial);
 const auto ack=acBegin(true);const auto baselineReads=acCount("calibration-offset");CHECK(baselineReads==1);const auto resets=acCount("calibration-reset"),trained=processCalls;g_realStickTest.autoCalibrate=false;
 // Same receipt cannot train twice, and changing Auto while busy cannot force Manual.
 CHECK(!acTick(clockMs));CHECK(processCalls==trained);acModeIs(3);CHECK(acCount("calibration-reset")==resets);
 controllerConfidence=0.99f;CHECK(!acTick(acNext()));CHECK(g_controllerCalibrationTransaction);CHECK(acCount("calibration-offset")==baselineReads&&acCount("file-create")==0);acModeIs(3);
 controllerConfidence=0.999f;CHECK(!acTick(acNext()));CHECK(!g_controllerCalibrationTransaction&&!g_controllerCalibrationActiveRequest);acModeIs(0);acCacheIs(std::nullopt);
 const auto result=acLastResult();CHECK(result.value("saved",false)&&result.value("applied",false));CHECK(result.value("requestId",uint64_t{})==ack.value("requestId",uint64_t{}));
 const auto saved=acRead();CHECK(saved.at("gyroDeviceId")==initial.at("gyroDeviceId")&&saved.at("xOffset")==initial.at("xOffset")&&saved.at("unrelated")==initial.at("unrelated"));
 const auto& record=saved.at("controllerMotionCalibration").at(identity()).at("0");CHECK(near(record.at("confidence").get<float>(),0.999));CHECK(record.at("weight")==9);
 CHECK(acCount("calibration-offset")==baselineReads+1&&acCount("file-create")==1);acIdleRestores(false);CHECK(acTick(acNext()));
 g_realStickTest.autoCalibrate=true;CHECK(acTick(acNext()));acModeIs(3);CHECK(acCount("calibration-reset")==resets);
}
static void acPersistFailure(){const json initial={{"gyroDeviceId","Windows"},{"xOffset",91}};writeCalibration(initial);acBegin(false);
 g_realStickTest.autoCalibrate=true;fileFault=6;controllerConfidence=1;CHECK(!acTick(acNext()));CHECK(!acLastResult().value("saved",true));CHECK(acLastResult().value("reason","")=="persistence-failed");
 CHECK(acRead()==initial);acModeIs(0);CHECK(currentHandle()->offset.weight==0);acCacheIs(std::nullopt);fileFault=0;acIdleRestores(true);}
static void acTimeout(){acBegin(true);const auto begin=clockMs;const auto trained=processCalls,resets=acCount("calibration-reset");g_realStickTest.autoCalibrate=false;
 CHECK(!acTick(begin+5000));CHECK(processCalls==trained);CHECK(acLastResult().value("reason","")=="timeout");acModeIs(0);acCacheIs(std::nullopt);
 CHECK(acCount("file-create")==0&&acCount("calibration-reset")==resets);acIdleRestores(false);}
static void acCancel(){
 acBegin(true);const auto oldHandle=sessionHandleFor(*g_legionMotionSessionKey);const auto resets=acCount("calibration-reset");
 {std::lock_guard<std::mutex> lock(g_controllerCalibrationMx);g_controllerCalibrationRequest.reset();g_controllerCalibrationHasRequest=false;}
 CHECK(acTick(acNext()));CHECK(!g_controllerCalibrationTransaction&&!g_controllerCalibrationActiveRequest);acModeIs(3);acCacheIs(3);
 CHECK(sessionHandleFor(*g_legionMotionSessionKey)==oldHandle&&acCount("calibration-reset")==resets);
 acBegin(true);g_realStickTest.autoCalibrate=true;inputCaptureControllerCalibrationAbort();CHECK(!g_controllerCalibrationTransaction);acModeIs(0);acCacheIs(std::nullopt);
 acIdleRestores(true);CHECK(acCount("file-create")==0);
 // Pending-only abort must invalidate the known steady cache too.
 const auto next=acNext();acReady(next);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));acCacheIs(3);
 inputCaptureControllerCalibrationAbort();acCacheIs(std::nullopt);acModeIs(3);acIdleRestores(true);
}
static void acReplacement(){const auto first=acBegin(true);const auto t=clockMs+6500;g_realStickTest.autoCalibrate=false;acReady(t);
 const auto second=inputCaptureControllerCalibrationRequest();CHECK(second.value("ok",false));CHECK(first.value("requestId",uint64_t{})!=second.value("requestId",uint64_t{}));
 const auto modes=acCount("mode");CHECK(!acTick(t));CHECK(g_controllerCalibrationActiveRequest&&g_controllerCalibrationActiveRequest->id==second.value("requestId",uint64_t{}));
 // Old rollback Manual followed by new transaction Fusion, with no idle reconcile between them.
 CHECK(acCount("mode")==modes+2);acModeIs(3);acCacheIs(std::nullopt);
 controllerConfidence=1;CHECK(!acTick(acNext()));CHECK(acLastResult().value("saved",false));CHECK(acLastResult().value("requestId",uint64_t{})==second.value("requestId",uint64_t{}));acModeIs(0);acIdleRestores(false);
}
static void acSourceRetirement(){for(unsigned cause=0;cause<5;++cause){
 acBegin(cause%2==0);const auto key=*g_legionMotionSessionKey;void* old=sessionHandleFor(key);auto oldSample=sample(clockMs);
 if(cause==0)g_powerGeneration.fetch_add(1);else if(cause==1)g_hidResolveCacheGeneration.fetch_add(1);else if(cause==2)legionMotionRevoke();else if(cause==3)selectSensor(legionMotionSelectedSensorIndex()?0:1);else selectPhysical(physical("USB\\AUTO-REPLACEMENT"));
 const auto processes=processCalls;CHECK(!inputCaptureLegionMotion(oldSample,axes(oldSample),static_cast<double>(clockMs)/1000));CHECK(processCalls==processes);CHECK(!handles.at(old).alive);
 g_realStickTest.autoCalibrate=(cause%2!=0);CHECK(acTick(acNext()));acModeIs(g_realStickTest.autoCalibrate?3:0);CHECK(sessionHandleFor(*g_legionMotionSessionKey)!=old);
 const auto modes=acCount("mode");CHECK(!g_legionMotionSession->reconcileCalibrationMode(key,true));CHECK(!g_legionMotionSession->calibrationModeFor(key));CHECK(acCount("mode")==modes);
 CHECK(acCount("file-create")==0);
}}
static void acCallbackRetirement(){for(unsigned cause=0;cause<3;++cause){acRestoreCallbacks();acBegin(true);
 void* old=sessionHandleFor(*g_legionMotionSessionKey);const auto resets=acCount("calibration-reset"),modes=acCount("mode");
 if(cause==0)g_gmSetMode=&acMode;else if(cause==1)g_gmGetGravity=&acGravity;else g_gmResetCal=&acReset;
 g_realStickTest.autoCalibrate=true;CHECK(!acTick(acNext()));CHECK(!g_controllerCalibrationTransaction);CHECK(!acLastResult().value("saved",true));CHECK(acCount("calibration-reset")==resets);
 if(cause<2){CHECK(!handles.at(old).alive);CHECK(sessionHandleFor(*g_legionMotionSessionKey)!=old);acModeIs(0);CHECK(acCount("mode")==modes+1);}else CHECK(sessionHandleFor(*g_legionMotionSessionKey)==old);
 acCacheIs(std::nullopt);acIdleRestores(true);CHECK(acCount("file-create")==0);
}}
static void acMissingCallbacks(){
 for(unsigned cause=0;cause<3;++cause){acRestoreCallbacks();CHECK(acTick(acNext()));void* old=sessionHandleFor(*g_legionMotionSessionKey);const auto processes=processCalls;
  if(cause==0)g_gmSetMode=nullptr;else if(cause==1)g_gmProcess=nullptr;else g_gmGetGravity=nullptr;
  CHECK(!acTick(acNext()));CHECK(processCalls==processes&&!handles.at(old).alive);acRestoreCallbacks();
  CHECK(!acTick(acNext()));clockMs=acNext();legionMotionRevoke();g_realStickTest.autoCalibrate=true;CHECK(acTick(acNext()));acModeIs(3);
 }
 for(unsigned cause=0;cause<2;++cause){acRestoreCallbacks();CHECK(acTick(acNext()));const auto t=acNext();acReady(t);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));
  const auto processes=processCalls;if(cause==0)g_gmResetCal=nullptr;else g_gmGetConf=nullptr;
  CHECK(!acTick(t));CHECK(!g_controllerCalibrationTransaction&&processCalls==processes);CHECK(!acLastResult().value("saved",true));CHECK(acCount("file-create")==0);
  acRestoreCallbacks();acIdleRestores(true);
 }
}
static void acFreshness(){g_realStickTest.autoCalibrate=true;CHECK(acTick(100));const auto key=*g_legionMotionSessionKey;
 auto stale=sample(100);clockMs=1601;g_realStickTest.autoCalibrate=false;auto modes=acCount("mode"),processes=processCalls;
 CHECK(!inputCaptureControllerCalibrationAdvance(stale,key,axes(stale),1.601));CHECK(acCount("mode")==modes&&processCalls==processes);
 auto future=sample(1602);CHECK(!inputCaptureControllerCalibrationAdvance(future,key,axes(future),1.601));CHECK(acCount("mode")==modes&&processCalls==processes);
 // A deterministic nonreentrant callback models retirement between the two source checks.
 g_gmSetMode=&acMode;g_realStickTest.autoCalibrate=true;acRetireDuringMode=true;CHECK(!acTick(1602));CHECK(processCalls==processes);
 acRestoreCallbacks();CHECK(acTick(acNext()));acModeIs(3);CHECK(acCount("calibration-reset")==0&&acCount("file-create")==0);
}

static void acLiveBaseline(){for(unsigned terminal=0;terminal<4;++terminal){
 inputCaptureLegionMotionSessionRevoke();clockMs=acNext();legionMotionRevoke();
 const json initial={{"gyroDeviceId","Windows-live-baseline"},{"unrelated",23}};writeCalibration(initial);g_realStickTest.autoCalibrate=true;
 CHECK(acTick(acNext()));CHECK(g_legionMotionCalibration&&g_legionMotionCalibration->weight==0);auto* h=currentHandle();
 h->offset={8.5f,-4.25f,2.125f,99}; // Simulate native Auto learning; this must not create a saved record.
 const auto live=h->offset;const auto managedWeight=g_legionMotionCalibration->weight;const auto writes=acCount("file-create"),reads=acCount("calibration-offset");
 const auto requested=acNext();acReady(requested);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));controllerConfidence=0.5f;
 CHECK(!acTick(requested));CHECK(acCount("calibration-offset")==reads+1&&acCount("file-create")==writes);
 CHECK(!near(h->offset.x,live.x)); // Polling mock trained native offsets after begin captured its baseline.
 if(terminal==0){controllerConfidence=1;CHECK(!acTick(acNext()));CHECK(acLastResult().value("saved",false));CHECK(near(h->offset.x,controllerCandidate.x)&&h->offset.weight==10);}
 else {
  if(terminal==1){fileFault=6;controllerConfidence=1;CHECK(!acTick(acNext()));fileFault=0;}
  else if(terminal==2)CHECK(!acTick(requested+5000));
  else inputCaptureControllerCalibrationAbort();
  CHECK(!acLastResult().value("saved",true));CHECK(acRead()==initial);
  CHECK(near(h->offset.x,live.x)&&near(h->offset.y,live.y)&&near(h->offset.z,live.z));CHECK(h->offset.weight==managedWeight);
 }
 const auto final=h->offset;acIdleRestores(true);CHECK(near(h->offset.x,final.x)&&near(h->offset.y,final.y)&&near(h->offset.z,final.z));CHECK(h->offset.weight==final.weight);
}}
static void acBaselineRetiringRead(void* h,float* x,float* y,float* z){controllerGetOffset(h,x,y,z);g_powerGeneration.fetch_add(1);}
static void acBaselineAdmission(){for(unsigned cause=0;cause<3;++cause){
 acRestoreCallbacks();inputCaptureLegionMotionSessionRevoke();clockMs=acNext();legionMotionRevoke();g_realStickTest.autoCalibrate=true;CHECK(acTick(acNext()));
 const auto t=acNext();acReady(t);CHECK(inputCaptureControllerCalibrationRequest().value("ok",false));
 const auto processed=processCalls,resets=acCount("calibration-reset"),writes=acCount("file-create");
 if(cause==0)currentHandle()->offset.x=std::numeric_limits<float>::quiet_NaN();else if(cause==1)g_gmGetOffset=nullptr;else g_gmGetOffset=&acBaselineRetiringRead;
 CHECK(!acTick(t));CHECK(!g_controllerCalibrationTransaction);CHECK(processCalls==processed&&acCount("calibration-reset")==resets&&acCount("file-create")==writes);CHECK(!acLastResult().value("saved",true));
 acRestoreCallbacks();
}}

// Controlled observed state of a legal cancellation interleaving, not a thread stress test.
static void acLaggingRequestFlag(){for(const bool automatic:{true,false}){
 g_controllerCalibrationHasRequest.store(false,std::memory_order_release);g_realStickTest.autoCalibrate=automatic;
 const auto receipt=acReady(acNext());CHECK(consume(receipt,static_cast<double>(clockMs)/1000.0));acModeIs(automatic?3:0);acCacheIs(automatic?3:0);
 CHECK(!g_controllerCalibrationActiveRequest&&!g_controllerCalibrationTransaction);
 {std::lock_guard<std::mutex> lock(g_controllerCalibrationMx);CHECK(!g_controllerCalibrationRequest);}
 const auto modes=acCount("mode"),offsets=acCount("offset"),resets=acCount("calibration-reset"),offsetReads=acCount("calibration-offset"),confidenceReads=acCount("calibration-confidence");
 const auto reads=calibrationPathCalls,writes=acCount("file-create"),replaces=acCount("file-replace-attempt"),creates=createCalls,processed=processCalls,modeEvents=acModeEvents();
 g_controllerCalibrationHasRequest.store(true,std::memory_order_release);
 for(unsigned tick=0;tick<4;++tick){
  const auto next=acNext();if(tick<2){clockMs=next;CHECK(consume(receipt,static_cast<double>(clockMs)/1000.0));}else CHECK(acTick(next));
  acModeIs(automatic?3:0);acCacheIs(automatic?3:0);CHECK(g_controllerCalibrationHasRequest.load(std::memory_order_acquire));
  CHECK(!g_controllerCalibrationActiveRequest&&!g_controllerCalibrationTransaction);
  {std::lock_guard<std::mutex> lock(g_controllerCalibrationMx);CHECK(!g_controllerCalibrationRequest);}
  CHECK(acCount("mode")==modes&&acModeEvents()==modeEvents);CHECK(acCount("offset")==offsets&&acCount("calibration-reset")==resets);
  CHECK(acCount("calibration-offset")==offsetReads&&acCount("calibration-confidence")==confidenceReads);
  CHECK(calibrationPathCalls==reads&&acCount("file-create")==writes&&acCount("file-replace-attempt")==replaces&&createCalls==creates);
 }
 CHECK(processCalls==processed+4);g_controllerCalibrationHasRequest.store(false,std::memory_order_release);
}}
