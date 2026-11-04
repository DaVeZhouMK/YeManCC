// Included after the exact production OneX owner, plan, codec, bind, parser and producer functions.
static BOOL CloseHandle(HANDLE handle){
 require(handle&&handle!=INVALID_HANDLE_VALUE&&!handle->closed,"double close/invalid close");
 if(handle==&fixtureProbe){handle->closed=true;++fixtureProbeCloses;return TRUE;}
 require(!g_onexBack.readPending&&!g_onexBack.writeInFlight,"premature resource release before both I/O terminal");
 handle->closed=true;++fixtureCloses;return 1;
}
static void reset(bool reading=false,bool writing=false){
 g_onexBack=OnexBackSession{};fixtureHandles={};fixtureTick=10000;
 fixtureError=0;fixtureCancels=fixtureWaits=fixtureResults=fixtureCloses=0;
 fixtureParses=fixtureOpened=fixtureBindAttempts=fixtureInit=fixtureStartRead=0;
 fixtureProbeCaps={};fixtureOpenCaps={};fixtureProbe={};fixtureSet={};fixturePath=L"\\\\?\\hid#vid_1a86&pid_fe00&mi_02#fixture";
 fixtureInterfaceCount=1;fixtureEnumerate=fixtureOpenOk=true;fixtureProbeCloses=fixtureAttrQueries=fixtureCapsQueries=fixtureEventCreates=0;
 fixtureReadMode=FixtureIo::Pending;fixtureWriteMode=FixtureIo::Sync;fixtureWrites.clear();fixtureReadBytes.clear();
 fixtureLogs.clear();fixtureSiblingCloses={};g_oemProducerStates={};g_oemProducerEdgeHead=g_oemProducerEdgeCount=0;
 for(auto& value:g_oemProducerGenerations)value.store(0);for(auto& value:g_oemProducerAuthorities)value.store(0);
 fixtureProducerGeneration=41;fixtureProducerMask=0xf;g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].nextGeneration=41;
 g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].physicalMask=0xf;
 g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].authorityMask=0xf;
 g_oemProducerGenerations[static_cast<size_t>(OemProducerId::OneX)].store(41);
 g_oemProducerAuthorities[static_cast<size_t>(OemProducerId::OneX)].store(0xf);
 fixtureProducerRevokes=0;g_onexBack.actionGeneration=41;g_onexBack.readActionGeneration=41;
 g_onexBack.device=&fixtureHandles[0];g_onexBack.ovEvent=&fixtureHandles[1];g_onexBack.writeEvent=&fixtureHandles[2];
 g_onexBack.ov.hEvent=g_onexBack.ovEvent;g_onexBack.writeOv.hEvent=g_onexBack.writeEvent;
 g_onexBack.enabled=g_onexBack.bound=true;g_onexBack.state=3;g_onexBack.auxKeys=0xf;
 g_onexBack.desc=onexCapDescriptorFor("HandheldCompanion.Devices.OneXPlayerX1");
 g_onexBack.inputReportBytes=g_onexBack.outputReportBytes=64;
 g_onexBack.buf.assign(64,0);g_onexBack.buf[0]=0xb2;g_onexBack.buf[1]=g_onexBack.buf[62]=0x3f;g_onexBack.buf[6]=0x23;g_onexBack.buf[12]=1;g_onexBack.writeBuf.assign(64,0xb4);
 g_onexBack.bindGeneration=9;g_onexBack.readInFlightGen=9;g_onexBack.writeInFlightGen=9;
 g_onexBack.writeInFlightToken=7;g_onexBack.completedOpToken=6;g_onexBack.writeDeadlineMs=10050;
 g_onexBack.readPending=reading;g_onexBack.writeInFlight=writing;g_onexBack.initAwaiting=writing;
 g_onexBack.initAwaitingToken=7;g_onexBack.initDone=true;g_onexBack.initWrites=2;
 g_onexBack.initStep=2;g_onexBack.feLatched=true;
 g_legionBack.enabled=g_legionSBack.enabled=g_gpdBack.enabled=g_rogOemKey.enabled=true;
 g_gpdBackEnabledSnap.store(true);
}
static void complete(size_t event,bool ok=true,DWORD bytes=64,DWORD error=0){
 auto& h=fixtureHandles[event];h.wait=WAIT_OBJECT_0;h.ok=ok;h.bytes=bytes;h.error=error;
}
static size_t logs(const char* event){return static_cast<size_t>(std::count_if(fixtureLogs.begin(),fixtureLogs.end(),[&](const json& r){return r.at("event")==event;}));}
static unsigned checks=0;
static std::string section;
static json results=json::array();
static void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(section+": "+what);}
template<class Run>static void test(const char* name,Run run){section=name;unsigned begin=checks;run();results.push_back({{"name",name},{"checks",checks-begin}});}
static void advanceToRetirement(){fixtureTick+=kOnexBackReadDrainLimitMs+kOnexBackWriteDrainLimitMs+10;onexBackPump();}
static std::vector<BYTE> inputReport(size_t bytes,BYTE command=0xb2,BYTE id=0x23,bool pressed=true,bool prefix=false,BYTE reportId=0){
 const size_t offset=(prefix||bytes!=64)?1u:0u;
 require(bytes>=offset+14,"input fixture logical frame has at least14 bytes");
 std::vector<BYTE> result(bytes,0);if(offset)result[0]=reportId;
 result[offset]=command;result[offset+1]=0x3f;result[offset+6]=id;result[offset+12]=pressed?1:0;
 // The source's trailing marker belongs to logical[size-2]. At raw15 its
 // logical index12 overlaps the button value, so minimum-size controls use B8.
 result[bytes-2]=0x3f;result[bytes-1]=0x7e;return result;
}
static std::vector<BYTE> readControlReport(size_t bytes){
 auto result=inputReport(bytes,bytes==15?0xb8:0xb2,0x23,true,false,0xa7);
 if(bytes==15)result[6]=0x20; // prefixed X1 status: logical[5], no button oracle
 return result;
}
static std::vector<BYTE> outputReport(const json& step,size_t bytes){
 std::vector<BYTE> result(bytes,0);const size_t offset=bytes==65?1:0;
 result[offset]=step.at("cmd").get<BYTE>();result[offset+1]=0x3f;result[offset+2]=1;
 const auto payload=step.at("payload").get<std::vector<BYTE>>();
 std::copy(payload.begin(),payload.end(),result.begin()+static_cast<std::ptrdiff_t>(offset+3));
 result[offset+62]=0x3f;result[offset+63]=result[offset];return result;
}
static const char* profileName(OnexInitProfile p){return p==OnexInitProfile::Apex?"Apex":p==OnexInitProfile::X1Mini?"X1Mini":"X1";}
static void fresh(OnexInitProfile profile=OnexInitProfile::X1,USHORT input=64,USHORT output=64,ULONGLONG now=10000){
 reset();g_onexBack=OnexBackSession{};g_onexBack.enabled=true;
 g_onexBack.desc=onexCapDescriptorFor(std::string("HandheldCompanion.Devices.OneXPlayer")+profileName(profile));
 fixtureProbeCaps.input=fixtureOpenCaps.input=input;fixtureProbeCaps.output=fixtureOpenCaps.output=output;fixtureTick=now;
}
static void finishInit(){
 for(unsigned i=0;i<24&&!g_onexBack.initDone;++i){
  check(!g_onexBack.writeInFlight,"finish helper requires terminal writes");
  fixtureTick=(std::max)(fixtureTick,g_onexBack.initStepDue);onexBackInitPump();
 }
 check(g_onexBack.initDone,"initialization completes within fixed bound");
}
static void status(OnexInitProfile profile,bool requested){
 auto report=inputReport(g_onexBack.inputReportBytes,0xb8);const size_t o=report.size()==64?0:1;
 report[o+(profile==OnexInitProfile::X1Mini?3:5)]=requested?(profile==OnexInitProfile::X1Mini?0xfe:0x20):0;
 onexBackParse(report.data(),report.size(),g_onexBack.actionGeneration);
}
static void fixedPlanAndCodec(){
 for(const auto profile:{OnexInitProfile::X1,OnexInitProfile::X1Mini,OnexInitProfile::Apex}){
  const auto& plan=fixtureOracle.at(profileName(profile));
  for(int i=0;i<static_cast<int>(plan.size());++i){
   BYTE cmd=0;const BYTE* payload=nullptr;size_t n=0;ULONGLONG delay=0;
   check(onexInitStepAt(profile,i,cmd,payload,n,delay),"fixed source step exists");
   check(cmd==plan[i].at("cmd").get<BYTE>()&&n==plan[i].at("payload").size(),"fixed source command and exact step payload length");
   check(std::vector<BYTE>(payload,payload+n)==plan[i].at("payload").get<std::vector<BYTE>>(),"fixed source complete payload bytes/prefix");
   check(delay==plan[i].at("delay").get<ULONGLONG>(),"fixed source exact delay");
   for(size_t bytes:{64u,65u}){std::vector<BYTE> encoded(bytes,0xaa);check(contract::encodeOutput(cmd,payload,n,bytes,encoded.data(),encoded.size()),"codec accepts observed report size");check(encoded==outputReport(plan[i],bytes),"independent full output frame/report-ID bytes");}
  }
  for(int i:{-1,static_cast<int>(plan.size()),100}){BYTE c=0;const BYTE* p=nullptr;size_t n=0;ULONGLONG d=0;check(!onexInitStepAt(profile,i,c,p,n,d),"no negative or extra fixed steps");}
 }
 for(auto profile:{OnexInitProfile::None,static_cast<OnexInitProfile>(77)}){BYTE c=0;const BYTE* p=nullptr;size_t n=0;ULONGLONG d=0;check(!onexInitStepAt(profile,0,c,p,n,d),"unknown profile has zero writes");}
 std::array<BYTE,66> encoded{};std::array<BYTE,60> payload{};
 for(size_t bytes:{0u,63u,66u})check(!contract::encodeOutput(0xb4,payload.data(),1,bytes,encoded.data(),bytes),"codec rejects unknown caps");
 check(!contract::encodeOutput(0xb4,payload.data(),60,64,encoded.data(),64),"oversize payload cannot truncate");
 check(!contract::encodeOutput(0xb4,nullptr,1,64,encoded.data(),64),"nonnull payload required");
 check(!contract::encodeOutput(0xb4,payload.data(),1,64,encoded.data(),63),"codec capacity mismatch rejected");
 check(!contract::encodeOutput(0xb4,payload.data(),1,64,nullptr,64),"null output rejected");
 check(!contract::encodeOutput(0x11,payload.data(),1,64,encoded.data(),64),"unknown output command rejected");
}
static void collectionAndInitialRead(){
 for(USHORT input:{15,16,33,64,65,66,129,512})for(USHORT output:{64,65}){
  fresh(OnexInitProfile::X1,input,output);const auto start=fixtureTick;
  check(onexBackBind(),"unique observed collection binds");
  check(fixtureAttrQueries==2&&fixtureCapsQueries==2&&fixtureProbeCloses==1,"probe and reopened handle both checked");
  check(g_onexBack.buf.size()==input&&g_onexBack.writeBuf.size()==output,"independent input/output report lengths retained");
  onexBackPump();check(fixtureWrites.empty()&&g_onexBack.readPending&&fixtureStartRead==1,"single read continues during initial delay");
  onexBackPump();check(fixtureStartRead==1,"pending read occupies exactly one slot");
  fixtureTick=start+3999;onexBackPump();check(fixtureWrites.empty(),"all admitted models wait full initial4000");
  fixtureTick=start+4000;onexBackPump();check(fixtureWrites.size()==1&&fixtureWrites[0].at==start+4000,"first actual write at initial4000 boundary");
  check(fixtureWrites[0].bytes==outputReport(fixtureOracle.at("X1")[0],output),"actual submit uses independent report output size");
 }
 for(const auto* model:{"X1","X1AMD","X1Intel","X1Mini","X1Pro","Apex"}){
  fresh();g_onexBack.desc=onexCapDescriptorFor(std::string("HandheldCompanion.Devices.OneXPlayer")+model);const auto start=fixtureTick;
  check(onexBackBind()&&g_onexBack.initStepDue==start+4000,"all six original descriptors have initial4000");
  fixtureTick=start+3999;onexBackPump();check(fixtureWrites.empty(),"no model bypasses initial wait");
  fixtureTick=start+4000;finishInit();const auto& plan=fixtureOracle.at(profileName(g_onexBack.desc.initProfile));
  check(fixtureWrites.size()==plan.size(),"actual initialization fixed step count");
  ULONGLONG due=start+4000;
  for(size_t i=0;i<plan.size();++i){check(fixtureWrites[i].at==due&&fixtureWrites[i].bytes==outputReport(plan[i],64),"actual initialization independent bytes and timing");due+=plan[i].at("delay").get<ULONGLONG>();}
 }
 for(bool reopen:{false,true})for(int field=0;field<10;++field){
  fresh();auto& caps=reopen?fixtureOpenCaps:fixtureProbeCaps;
  switch(field){case 0:caps.vid=0x17ef;break;case 1:caps.pid=0xfe01;break;case 2:caps.page=0xff01;break;case 3:caps.usage=2;break;case 4:caps.input=14;break;case 5:caps.output=66;break;case 6:caps.attrsOk=false;break;case 7:caps.prepOk=false;break;case 8:caps.capsOk=false;break;case 9:caps.input=513;break;}
  check(!onexBackBind()&&!g_onexBack.bound&&fixtureEventCreates==0,"reject wrong collection/attrs/caps at probe and reopen");
 }
 for(bool inputDrift:{false,true}){fresh();if(inputDrift)fixtureOpenCaps.input=65;else fixtureOpenCaps.output=65;check(!onexBackBind()&&fixtureEventCreates==0,"valid but changed reopened length rejects drift");}
 fresh();fixturePath=L"fixture#mi_01";check(!onexBackBind()&&fixtureOpened==0,"wrong MI rejected before open");
 fresh();fixtureInterfaceCount=2;check(!onexBackBind()&&fixtureOpened==0,"two matching collections are ambiguous");
}
static void parserAndReadBoundaries(){
 // Independently apply the fixed ReadLoopAsync logical shape; no output field
 // or report-ID value selects an input offset.
 const auto logical=[](const BYTE* frame,size_t n){return n>=14&&(frame[0]==0xb2||frame[0]==0xb8)&&frame[1]==0x3f&&frame[n-2]==0x3f;};
 for(size_t bytes:{15u,16u,33u,64u,65u,66u,129u,512u})for(BYTE id:{0,0x17,0x3f,0xb2,0xb8}){
  const BYTE command=bytes==15?0xb8:0xb2;
  auto report=inputReport(bytes,command,0x23,true,true,id);
  if(bytes==15)report[6]=0x20;
  const bool prefixed=logical(report.data()+1,bytes-1),raw=bytes==64&&logical(report.data(),bytes);
  check(prefixed&&!raw,"prefixed command+dynamic markers select exactly one logical layout for any report ID");
  check(contract::inputFrame(report.data(),bytes)==report.data()+1,"input offset comes from unique frame shape including nonzero and command-valued IDs");
  reset();g_onexBack.feLatched=false;g_onexBack.state=0;g_onexBack.auxKeys=0;
  onexBackParse(report.data(),bytes,41);
  check(bytes==15?g_onexBack.reinitRequested:(fixtureParses==1&&g_onexBack.state==1),"raw15 status / longer raw button frames consume their actual logical fields");
  const auto count=fixtureParses;report[2]=0;check(!contract::inputFrame(report.data(),bytes),"invalid leading marker has no input layout");onexBackParse(report.data(),bytes,41);check(fixtureParses==count,"bad leading marker rejected");
  report=inputReport(bytes,command,0x23,true,true,id);report[bytes-2]=0;
  check(!contract::inputFrame(report.data(),bytes),"dynamic trailing marker required at every input length");
  onexBackParse(report.data(),bytes,41);check(fixtureParses==count,"bad trailing marker cannot publish");
  report=inputReport(bytes,0xb4,0x23,true,true,id);check(!contract::inputFrame(report.data(),bytes),"unknown logical command cannot choose a layout");
 }
 // raw64 remains the original layout; it does not acquire a prefix from output.
 for(BYTE command:{0xb2,0xb8}){
  auto raw=inputReport(64,command);check(logical(raw.data(),raw.size())&&!logical(raw.data()+1,raw.size()-1),"legacy raw64 shape is uniquely unprefixed");
  check(contract::inputFrame(raw.data(),raw.size())==raw.data(),"raw64 original pointer retained");
  auto report=inputReport(64,command,0x23,true,true,0xb2);check(contract::inputFrame(report.data(),report.size())==report.data()+1,"prefixed64 command-valued ID never selects legacy offset");
 }
 for(size_t bad:{0u,14u,513u}){std::vector<BYTE> report(bad,0);check(!contract::inputFrame(report.data(),bad),"raw input outside15..512 rejected");reset();onexBackParse(report.data(),bad,41);check(fixtureParses==0,"out-of-bounds input cannot publish");}
 check(!contract::inputFrame(nullptr,64),"null input rejected");
 auto raw=inputReport(64);raw[0]=0;check(!contract::inputFrame(raw.data(),raw.size()),"ID0 does not excuse an invalid raw64 command");
 auto misplaced=inputReport(64);misplaced.push_back(0);check(!contract::inputFrame(misplaced.data(),misplaced.size()),"unprefixed65 cannot be guessed from output framing");
 for(size_t bytes:{66u,129u,512u}){auto report=inputReport(bytes);report[bytes-2]=0;report[63]=0x3f;check(!contract::inputFrame(report.data(),bytes),"fixed64 trailing offset cannot validate a variable logical frame");}
 for(USHORT bytes:{15,16,33,64,65,66,129,512}){
  if(bytes>=16){
   reset();g_onexBack.inputReportBytes=bytes;g_onexBack.buf=inputReport(bytes);g_onexBack.state=0;g_onexBack.auxKeys=0;
   auto data=inputReport(bytes);onexBackParse(data.data(),data.size(),41);
   check(fixtureParses==1&&g_onexBack.state==1,"real parser accepts independent full button-report length");
   data=inputReport(bytes,0xb2,0x23,false);onexBackParse(data.data(),data.size(),41);check(g_onexBack.state==0,"real parser releases correct input bit");
   const auto count=fixtureParses;data=inputReport(bytes);onexBackParse(data.data(),data.size(),40);check(fixtureParses==count,"stale producer cannot parse");
  }
  for(auto mode:{FixtureIo::Short,FixtureIo::Failure}){
   reset();g_onexBack.inputReportBytes=bytes;g_onexBack.buf=readControlReport(bytes);fixtureReadMode=mode;fixtureReadBytes=readControlReport(bytes);
   check(!onexBackStartRead()&&fixtureProducerGeneration==0&&fixtureProducerMask==0&&fixtureParses==0&&fixtureCloses==3,"sync error/short read withdraws owner and returns terminal resources");
  }
  reset();g_onexBack.inputReportBytes=bytes;g_onexBack.buf=readControlReport(bytes);g_onexBack.state=0;g_onexBack.auxKeys=0;g_onexBack.feLatched=false;fixtureReadMode=FixtureIo::Sync;fixtureReadBytes=readControlReport(bytes);
  check(onexBackStartRead()&&(bytes==15?g_onexBack.reinitRequested:(fixtureParses==1&&g_onexBack.state==1))&&!g_onexBack.readPending&&fixtureCloses==0,"sync full bounded input consumes actual parser and retains current owner");
  reset(true,false);g_onexBack.inputReportBytes=bytes;g_onexBack.buf=readControlReport(bytes);g_onexBack.state=0;g_onexBack.auxKeys=0;g_onexBack.feLatched=false;complete(1,true,bytes);onexBackServiceIo();
  check((bytes==15?g_onexBack.reinitRequested:(fixtureParses==1&&g_onexBack.state==1))&&!g_onexBack.readPending&&fixtureCloses==0,"async full bounded input consumes actual parser and retains current owner");
  reset(true,false);g_onexBack.inputReportBytes=bytes;g_onexBack.buf=readControlReport(bytes);complete(1,true,bytes-1);onexBackServiceIo();
  check(fixtureProducerGeneration==0&&fixtureCloses==3&&fixtureParses==0,"async short read withdraws and returns terminal resources");
 }
 for(auto profile:{OnexInitProfile::X1,OnexInitProfile::X1Mini,OnexInitProfile::Apex}){
  for(BYTE id:{0x21,0x22,0x23,0x24}){
   fresh(profile,65,64);check(onexBackBind(),"actual role profile bound");
   const uint64_t expected=id==0x21?(profile==OnexInitProfile::Apex?kOemBitOrange:kOemBitSpecial):id==0x22?kOemBitR4:id==0x23?kOemBitL4:kOemBitKeyboard;
   auto report=inputReport(65,0xb2,id);onexBackParse(report.data(),report.size(),g_onexBack.actionGeneration);
   check(fixtureProducerMask==expected&&(g_oemProducerAuthorities[static_cast<size_t>(OemProducerId::OneX)].load()&expected)==expected,"real parser publishes exact model role and owned authority");
   report=inputReport(65,0xb2,id,false);onexBackParse(report.data(),report.size(),g_onexBack.actionGeneration);check(fixtureProducerMask==0,"release clears only current event key");
  }
  fresh(profile);check(onexBackBind(),"status profile bound");status(profile,true);check(g_onexBack.reinitRequested,"fixed profile status offset requests reinit");
  const auto due=g_onexBack.reinitDue;for(int i=0;i<32;++i)status(profile,true);
  check(g_onexBack.reinitDue==due&&g_onexBack.reinitCount==0,"repeated status boundedly coalesces without extending due");
  fresh(profile);check(onexBackBind(),"negative status profile bound");auto wrong=inputReport(64,0xb8);wrong[profile==OnexInitProfile::X1Mini?5:3]=profile==OnexInitProfile::X1Mini?0x20:0xfe;
  onexBackParse(wrong.data(),wrong.size(),g_onexBack.actionGeneration);check(!g_onexBack.reinitRequested,"wrong model status field cannot trigger");
 }
}
static void queuedInitAndRateLimit(){
 fresh(OnexInitProfile::X1Mini,64,65,50);check(onexBackBind(),"queued Mini bound at small uptime");
 status(OnexInitProfile::X1Mini,true);check(g_onexBack.reinitRequested&&g_onexBack.reinitDue==250&&g_onexBack.initStepDue==4050,"initial-wait request queued with200ms and no uptime2000 assumption");
 fixtureTick=4050;fixtureWriteMode=FixtureIo::Pending;onexBackPump();
 check(g_onexBack.writeInFlight&&g_onexBack.initAwaiting&&g_onexBack.initStep==0,"actual final Mini step pending");
 const auto token=g_onexBack.initAwaitingToken;const auto buffer=g_onexBack.writeBuf;
 status(OnexInitProfile::X1Mini,false);status(OnexInitProfile::X1Mini,true);
 onexBackPump();check(g_onexBack.reinitRequested&&g_onexBack.initAwaitingToken==token&&g_onexBack.writeBuf==buffer&&fixtureWrites.size()==1,"status during final pending step retained without restart or buffer reuse");
 complete(2,true,65);onexBackServiceWrite();fixtureWriteMode=FixtureIo::Sync;finishInit();
 check(g_onexBack.reinitRequested&&g_onexBack.reinitCount==0,"request survives final terminal accounting");
 onexBackInitPump();finishInit();check(g_onexBack.reinitCount==1&&fixtureWrites.size()==2,"one queued reinit consumed after original initialization");
 for(int count=2;count<=4;++count){
  status(OnexInitProfile::X1Mini,false);status(OnexInitProfile::X1Mini,true);const auto due=g_onexBack.reinitDue;const auto writes=fixtureWrites.size();
  check(due>=fixtureTick+200&&due>=g_onexBack.lastReinitAt+2000,"source200 and generation2000 limits remain distinct");
  fixtureTick=due-1;onexBackInitPump();check(fixtureWrites.size()==writes&&g_onexBack.reinitCount==count-1,"no write before reinit due");
  fixtureTick=due;onexBackInitPump();finishInit();check(g_onexBack.reinitCount==count&&fixtureWrites.size()==writes+1,"exactly one bounded request consumed at due");
 }
 status(OnexInitProfile::X1Mini,false);status(OnexInitProfile::X1Mini,true);const auto writes=fixtureWrites.size();fixtureTick+=10000;onexBackInitPump();
 check(!g_onexBack.reinitRequested&&g_onexBack.reinitCount==4&&fixtureWrites.size()==writes,"four per-generation budget cannot reset from status storm");
 // A refused/stale request cannot consume later generations or survive stopping.
 fresh(OnexInitProfile::X1Mini);check(onexBackBind(),"stale queue setup");status(OnexInitProfile::X1Mini,true);++g_onexBack.bindGeneration;finishInit();
 check(!g_onexBack.reinitRequested&&g_onexBack.reinitCount==0,"stale queued generation discarded");
 status(OnexInitProfile::X1Mini,false);status(OnexInitProfile::X1Mini,true);onexBackClose();
 check(!g_onexBack.reinitRequested&&!g_onexBack.reinitRequestGen&&!g_onexBack.reinitDue,"close clears pending request");
 fresh(OnexInitProfile::X1Mini);check(onexBackBind(),"identity queue setup");status(OnexInitProfile::X1Mini,true);onexBackApplyDescriptor("HandheldCompanion.Devices.OneXPlayerApex");
 check(!g_onexBack.reinitRequested&&!g_onexBack.bound&&g_onexBack.desc.role21Orange,"identity change cancels old request and role");
 // A later step is still in flight when status arrives, on a multi-step profile.
 fresh(OnexInitProfile::Apex);check(onexBackBind(),"Apex final-step setup");fixtureTick=g_onexBack.initStepDue;
 for(int i=0;i<3;++i){onexBackInitPump();fixtureTick=g_onexBack.initStepDue;}
 fixtureWriteMode=FixtureIo::Pending;onexBackInitPump();check(g_onexBack.initStep==3&&g_onexBack.writeInFlight,"actual fourth Apex step pending");
 status(OnexInitProfile::Apex,true);const auto step=g_onexBack.initStep;const auto pendingToken=g_onexBack.initAwaitingToken;
 onexBackInitPump();check(g_onexBack.reinitRequested&&g_onexBack.initStep==step&&g_onexBack.initAwaitingToken==pendingToken,"late Apex request does not overwrite final step");
 complete(2,true,64);onexBackServiceWrite();fixtureWriteMode=FixtureIo::Sync;finishInit();fixtureTick=g_onexBack.reinitDue;onexBackInitPump();finishInit();
 check(g_onexBack.reinitCount==1&&fixtureWrites.size()==8,"late multi-step request executes exactly one complete next plan");
 fresh(OnexInitProfile::X1Mini);check(onexBackBind(),"completed init with independent write setup");finishInit();
 const BYTE motor[]={1,5,5};uint64_t externalToken=0;fixtureWriteMode=FixtureIo::Pending;
 check(onexBackSubmitWrite(0xb3,motor,sizeof(motor),externalToken)==OnexWriteSubmit::Pending,"independent write occupies actual slot");
 status(OnexInitProfile::X1Mini,true);fixtureTick=g_onexBack.reinitDue;onexBackInitPump();
 check(g_onexBack.reinitRequested&&g_onexBack.reinitCount==0&&g_onexBack.writeInFlightToken==externalToken,"request remains queued behind a non-init write");
 complete(2,true,64);onexBackServiceWrite();fixtureWriteMode=FixtureIo::Sync;onexBackInitPump();finishInit();
 check(g_onexBack.reinitCount==1&&fixtureWrites.size()==3,"queued reinit starts after independent write terminal");
 for(bool writing:{false,true}){
  reset(!writing,writing);g_onexBack.reinitRequested=true;g_onexBack.reinitRequestGen=g_onexBack.bindGeneration;g_onexBack.reinitDue=fixtureTick+200;
  if(writing)onexBackRetireStuck();else onexBackRetireReadStuck("fixture-observation-failure");
  check(!g_onexBack.reinitRequested&&g_onexBack.draining&&fixtureCloses==0,"read/write retirement withdraws queued request while retaining unresolved slot");
  check(!onexBackBind(),"retired queued owner cannot rebind");complete(writing?2:1,false,0,ERROR_OPERATION_ABORTED);onexBackServiceIo();
  check(fixtureCloses==3&&!g_onexBack.draining,"retired queued slot still returns only on terminal");
 }
}
static void actualWriteBoundaries(){
 const BYTE payload[]={1,5,5};
 for(USHORT bytes:{64,65})for(auto mode:{FixtureIo::Sync,FixtureIo::Short,FixtureIo::Failure,FixtureIo::Pending}){
  reset();g_onexBack.outputReportBytes=bytes;g_onexBack.writeBuf.assign(bytes,0);fixtureWriteMode=mode;uint64_t token=0;
  const auto result=onexBackSubmitWrite(0xb3,payload,sizeof(payload),token);
  check(fixtureWrites.size()==1&&fixtureWrites[0].bytes==outputReport(fixtureOracle.at("X1Mini")[0],bytes),"actual WriteFile full bytes match fixed oracle");
  if(mode==FixtureIo::Pending){
   check(result==OnexWriteSubmit::Pending&&token&&g_onexBack.writeInFlight,"actual async submit owns slot");const auto saved=g_onexBack.writeBuf;uint64_t next=0;
   check(onexBackSubmitWrite(0xb3,payload,sizeof(payload),next)==OnexWriteSubmit::Failed&&fixtureWrites.size()==1&&g_onexBack.writeBuf==saved,"pending write cannot be overwritten");
   complete(2,true,bytes-1);onexBackServiceWrite();check(!g_onexBack.lastOpSuccess&&!g_onexBack.writeInFlight,"async short write fails terminal validation");
  }else check(!g_onexBack.writeInFlight&&g_onexBack.lastOpSuccess==(mode==FixtureIo::Sync),"sync/error/short write terminal status");
 }
 reset();std::array<BYTE,60> large{};uint64_t token=0;
 check(onexBackSubmitWrite(0xb4,large.data(),large.size(),token)==OnexWriteSubmit::Failed&&fixtureWrites.empty(),"actual submit rejects oversize without IO");
 reset();g_onexBack.outputReportBytes=66;g_onexBack.writeBuf.assign(66,0);
 check(onexBackSubmitWrite(0xb3,payload,sizeof(payload),token)==OnexWriteSubmit::Failed&&fixtureWrites.empty(),"actual submit rejects unknown report caps");
 for(auto mode:{FixtureIo::Short,FixtureIo::Failure}){
  fresh(OnexInitProfile::Apex);check(onexBackBind(),"init aggregate failure setup");fixtureTick=g_onexBack.initStepDue;fixtureWriteMode=mode;onexBackInitPump();fixtureWriteMode=FixtureIo::Sync;finishInit();
  check(g_onexBack.initFailures==1&&!g_onexBack.initAllWritesOk&&fixtureWrites.size()==4,"earlier failure survives later successful writes with no replay");
 }
}

int main(){try{
 test("actual-source-ledger-generation-and-queue",[]{
  reset();auto& other=g_oemProducerStates[static_cast<size_t>(OemProducerId::Gpd)];other.generation=70;other.mask=123;
  g_oemProducerGenerations[static_cast<size_t>(OemProducerId::Gpd)].store(70);
  g_oemProducerEdgeHead=kOemProducerEdgeCapacity-1;g_oemProducerEdgeCount=3;
  g_oemProducerEdges[kOemProducerEdgeCapacity-1]={};g_oemProducerEdges[kOemProducerEdgeCapacity-1].source=OemProducerId::OneX;g_oemProducerEdges[kOemProducerEdgeCapacity-1].generation=41;
  g_oemProducerEdges[0]={};g_oemProducerEdges[0].source=OemProducerId::Gpd;g_oemProducerEdges[0].generation=70;g_oemProducerEdges[0].sequence=101;
  g_oemProducerEdges[1]={};g_oemProducerEdges[1].source=OemProducerId::OneX;g_oemProducerEdges[1].generation=40;g_oemProducerEdges[1].sequence=102;
  check(!oemProducerCancel(OemProducerId::OneX,40)&&g_oemProducerEdgeCount==3&&fixtureProducerGeneration==41,"stale cancel cannot alter current ledger");
  oemVendorHidShutdown();
  const auto& own=g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)];
  check(own.generation==0&&own.mask==0&&own.physicalMask==0&&own.authorityMask==0&&g_oemProducerAuthorities[static_cast<size_t>(OemProducerId::OneX)].load()==0,"actual production cancel clears action/physical/authority state");
  check(g_oemProducerEdgeCount==2&&g_oemProducerEdges[g_oemProducerEdgeHead].sequence==101&&g_oemProducerEdges[(g_oemProducerEdgeHead+1)%kOemProducerEdgeCapacity].sequence==102,"actual cancellation compacts only matching-generation receipts in FIFO order");
  check(other.generation==70&&other.mask==123,"OneX close cannot withdraw another producer ledger");
  fixedPlanAndCodec();
 });
 test("shutdown-no-pending",[]{
  reset();oemVendorHidShutdown();
  check(!g_onexBack.enabled&&!g_onexBack.bound,"capture teardown disables OneX");
  check(g_onexBack.actionGeneration==0&&fixtureProducerGeneration==0&&fixtureProducerMask==0&&fixtureProducerRevokes==1,"source ownership withdrawn");
  check(g_onexBack.state==0&&g_onexBack.auxKeys==0,"owned key publication cleared");
  check(g_onexBack.bindGeneration==10&&!g_onexBack.initAwaiting&&!g_onexBack.initDone&&g_onexBack.initStep==0&&!g_onexBack.feLatched,"old initialization/generation retired");
  check(fixtureCloses==3&&g_onexBack.device==INVALID_HANDLE_VALUE&&g_onexBack.ovEvent==nullptr&&g_onexBack.writeEvent==nullptr,"quiescent resources closed once");
  check(g_onexBack.buf.empty()&&g_onexBack.writeBuf.empty(),"quiescent buffers released");
  check(fixtureSiblingCloses==std::array<int,4>{1,1,1,1},"existing sibling close calls retained exactly once");
  check(!g_legionBack.enabled&&!g_legionSBack.enabled&&!g_gpdBack.enabled&&!g_gpdBackEnabledSnap.load()&&!g_rogOemKey.enabled,"sibling shutdown flags preserved");
  oemVendorHidShutdown();check(fixtureCloses==3&&fixtureProducerRevokes==1,"second close idempotent for physical ownership");
 });
 test("dual-pending-shutdown-and-restart",[]{
  reset(true,true);const auto* readBuf=g_onexBack.buf.data();const auto* writeBuf=g_onexBack.writeBuf.data();
  oemVendorHidShutdown();
  check(!g_onexBack.enabled&&g_onexBack.draining&&g_onexBack.readPending&&g_onexBack.writeInFlight,"stop retains both pending operations");
  check(fixtureProducerMask==0&&fixtureProducerGeneration==0&&g_onexBack.state==0&&g_onexBack.auxKeys==0,"stop revokes keys before cancellation proof");
  check(fixtureCancels==2&&fixtureCloses==0,"cancel requested without pretending terminal");
  check(readBuf==g_onexBack.buf.data()&&writeBuf==g_onexBack.writeBuf.data(),"buffers retain stable storage");
  advanceToRetirement();check(g_onexBack.readStuck&&g_onexBack.writeStuck,"bounded retirement observed");
  onexBackSetEnabled(true);check(fixtureOpened==0&&g_onexBack.draining,"actual bind guard refuses retained operations");
  complete(1,false,0,ERROR_OPERATION_ABORTED);onexBackPump();
  onexBackFinalizeClose(); // Finalizer must also refuse the still-pending write.
  check(!g_onexBack.readPending&&g_onexBack.writeInFlight&&fixtureCloses==0,"read terminal alone cannot close shared resources");
  check(fixtureParses==0&&fixtureOpened==0,"old read not published and no premature rebind");
  complete(2);onexBackPump();
  check(!g_onexBack.readPending&&!g_onexBack.writeInFlight&&!g_onexBack.draining,"late terminal write completes draining");
  check(fixtureCloses==3&&fixtureOpened==1,"restart admission opens only after terminal retirement");
  check(g_onexBack.completedOpToken==6,"old-generation success does not become current init token");
  check(fixtureParses==0&&logs("onex-back-write-late")==1,"old write completion retained diagnostically");
  queuedInitAndRateLimit();
 });
 test("retired-read-late-success",[]{
  reset(true,false);oemVendorHidShutdown();advanceToRetirement();
  check(g_onexBack.readStuck&&g_onexBack.readPending&&fixtureCloses==0,"unresolved read storage retained");
  complete(1,true,64);onexBackPump();
  check(!g_onexBack.readPending&&fixtureCloses==3&&!g_onexBack.readStuck,"retired read still observes real terminal");
  check(fixtureParses==0&&logs("onex-back-read-late")==1,"draining success is discarded");
  check(fixtureOpened==0&&fixtureStartRead==0,"disabled drain does not open/start another read");
 });
 test("retired-write-late-abort",[]{
  reset(false,true);oemVendorHidShutdown();advanceToRetirement();
  check(g_onexBack.writeStuck&&g_onexBack.writeInFlight&&fixtureCloses==0,"unresolved write storage retained");
  complete(2,false,0,ERROR_OPERATION_ABORTED);onexBackPump();
  check(!g_onexBack.writeInFlight&&fixtureCloses==3&&!g_onexBack.writeStuck,"retired write still observes abort terminal");
  check(!g_onexBack.lastOpSuccess&&g_onexBack.completedOpToken==6,"failed old write cannot validate initialization");
 });
 test("event-signaled-but-operation-incomplete",[]{
  reset(true,true);oemVendorHidShutdown();advanceToRetirement();
  complete(1,false,0,ERROR_IO_INCOMPLETE);complete(2,false,0,ERROR_IO_INCOMPLETE);onexBackPump();
  check(g_onexBack.readPending&&g_onexBack.writeInFlight&&fixtureCloses==0,"signaled event alone is not terminal proof");
  check(fixtureParses==0&&fixtureOpened==0,"incomplete operations never publish/open");
  complete(2);onexBackPump();check(g_onexBack.readPending&&!g_onexBack.writeInFlight&&fixtureCloses==0,"write terminal cannot release pending read");
  complete(1,false,0,ERROR_OPERATION_ABORTED);onexBackPump();check(fixtureCloses==3,"both actual terminals release once");
  actualWriteBoundaries();
 });
 test("lost-event-observation-can-later-terminate",[]{
  reset(true,true);fixtureHandles[1].wait=WAIT_FAILED;fixtureHandles[2].wait=WAIT_FAILED;
  oemVendorHidShutdown();onexBackPump();check(g_onexBack.readStuck&&g_onexBack.writeStuck&&fixtureCloses==0,"unknown event outcome retires instead of releasing");
  for(int i=0;i<4;++i)onexBackPump();check(fixtureCloses==0&&fixtureOpened==0&&fixtureCancels==2,"retirement never recancels/reopens in a loop");
  complete(1,false,0,ERROR_OPERATION_ABORTED);complete(2,false,0,ERROR_OPERATION_ABORTED);onexBackPump();
  check(fixtureCloses==3&&!g_onexBack.draining,"subsequent known terminals resolve retirement");
 });
 test("active-current-read-control",[]{
  reset(true,false);complete(1,true,64);onexBackServiceIo();
  check(fixtureParses==1&&!g_onexBack.readPending&&fixtureCloses==0,"current live read still reaches parser");
  check(fixtureProducerGeneration==41&&g_onexBack.bound,"healthy current owner retained");
  parserAndReadBoundaries();
 });
 test("active-read-fault-withdraws-owner",[]{
  reset(true,false);complete(1,false,0,5);onexBackServiceIo();
  check(fixtureParses==0&&fixtureProducerGeneration==0&&fixtureProducerMask==0,"read failure cannot preserve old key authority");
  check(fixtureCloses==3&&!g_onexBack.bound,"terminal read fault closes quiescent resources");
 });
 test("shutdown-completion-order-matrix",[]{
  for(const bool readFirst:{false,true})for(const bool readSuccess:{false,true})for(const bool writeSuccess:{false,true}){
   reset(true,true);oemVendorHidShutdown();advanceToRetirement();
   const size_t first=readFirst?1:2,second=readFirst?2:1;
   const bool okFirst=readFirst?readSuccess:writeSuccess,okSecond=readFirst?writeSuccess:readSuccess;
   complete(first,okFirst,okFirst?64:0,okFirst?0:ERROR_OPERATION_ABORTED);onexBackPump();
   check(fixtureCloses==0,"first terminal cannot release remaining operation");
   complete(second,okSecond,okSecond?64:0,okSecond?0:ERROR_OPERATION_ABORTED);onexBackPump();
   check(fixtureCloses==3&&!g_onexBack.draining&&fixtureParses==0,"all terminal order/result combinations settle without stale parse");
  }
  collectionAndInitialRead();
 });
 std::cout<<json{{"status","pass"},{"checks",checks},{"cases",results},{"scope","Exact production OneX plan/codec/bind/parser/read/write/init/shutdown/producer code; deterministic in-memory OS/UI boundaries; no actual Windows IO, binding, device, power callback, Host or Steam"}}.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cout<<json{{"status","fail"},{"section",section},{"checks",checks},{"error",e.what()}}.dump(2)<<'\n';return 1;}}
