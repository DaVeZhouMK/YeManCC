// Included after the exact production session/functions and binding admission prefix.
static BOOL CloseHandle(HANDLE handle){
 require(handle&&handle!=INVALID_HANDLE_VALUE&&!handle->closed,"double close/invalid close");
 require(!g_onexBack.readPending&&!g_onexBack.writeInFlight,"premature resource release before both I/O terminal");
 handle->closed=true;++fixtureCloses;return 1;
}
static bool onexBackBind(){
 ++fixtureBindAttempts;
 if(!extractedBindAdmission())return false;
 if(g_onexBack.bound)return true;
 ++fixtureOpened;g_onexBack.bound=true;g_onexBack.actionGeneration=++fixtureProducerGeneration;g_oemProducerGenerations[static_cast<size_t>(OemProducerId::OneX)].store(fixtureProducerGeneration);
 return true;
}
static void onexBackInitPump(){++fixtureInit;}
static bool onexBackStartRead(){++fixtureStartRead;return true;}
static void reset(bool reading=false,bool writing=false){
 g_onexBack=OnexBackSession{};fixtureHandles={};fixtureTick=10000;
 fixtureError=0;fixtureCancels=fixtureWaits=fixtureResults=fixtureCloses=0;
 fixtureParses=fixtureOpened=fixtureBindAttempts=fixtureInit=fixtureStartRead=0;
 fixtureLogs.clear();fixtureSiblingCloses={};g_oemProducerStates={};g_oemProducerEdgeHead=g_oemProducerEdgeCount=0;
 for(auto& value:g_oemProducerGenerations)value.store(0);for(auto& value:g_oemProducerAuthorities)value.store(0);
 fixtureProducerGeneration=41;fixtureProducerMask=0xf;
 g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].physicalMask=0xf;
 g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].authorityMask=0xf;
 g_oemProducerGenerations[static_cast<size_t>(OemProducerId::OneX)].store(41);
 g_oemProducerAuthorities[static_cast<size_t>(OemProducerId::OneX)].store(0xf);
 fixtureProducerRevokes=0;g_onexBack.actionGeneration=41;g_onexBack.readActionGeneration=41;
 g_onexBack.device=&fixtureHandles[0];g_onexBack.ovEvent=&fixtureHandles[1];g_onexBack.writeEvent=&fixtureHandles[2];
 g_onexBack.ov.hEvent=g_onexBack.ovEvent;g_onexBack.writeOv.hEvent=g_onexBack.writeEvent;
 g_onexBack.enabled=g_onexBack.bound=true;g_onexBack.state=3;g_onexBack.auxKeys=0xf;
 g_onexBack.buf.assign(64,0xb2);g_onexBack.writeBuf.assign(64,0xb4);
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
 });
 std::cout<<json{{"status","pass"},{"checks",checks},{"cases",results},{"scope","Exact production shutdown/OneX cancellation/service/pump/bind-admission code; deterministic in-memory OS/producer sinks; no actual Windows IO, binding, device, power callback, Host or Steam"}}.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cout<<json{{"status","fail"},{"section",section},{"checks",checks},{"error",e.what()}}.dump(2)<<'\n';return 1;}}
