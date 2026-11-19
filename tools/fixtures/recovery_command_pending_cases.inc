namespace sf=ymcc::steamgamefacts;
using SC=SteamOriginalRecovery::Control;
static unsigned directedVariants=0;
static void clearHooks(){fixtureBeforeCompletionWatch={};fixtureBeforeSubscriptionInstall={};fixtureBeforeBindingRegister={};fixtureBeforeCurrentSelection={};}
static void stopController(const std::shared_ptr<SC>& c){
    clearHooks();fixtureFreshReadHook={};fixtureEngineReceipt={};engineAction={};
    steamOriginalRecoveryRequestStop();
    SteamOriginalRecovery::RetiredStartupFresh retired;
    {std::lock_guard<std::mutex> lock(c->mx);retired=SteamOriginalRecovery::retireStartupFreshLocked(c);}
    engineCalls.clear();SteamOriginalRecovery::callbackControl.reset();
}
struct RunCase final {
    std::shared_ptr<SC> c;sf::TrackerObservationKey key;
    SteamOriginalRecovery::Pending original{ymcc::steamOriginal::Trigger::Startup,0,0};
    RunCase(const InertRoles::InertTree& tree,const std::wstring& selected){
        clearHooks();fixtureEngineReceipt={};fixtureFreshReadHook={};
        NativeDetectedGame shipping;shipping.pid=tree.shippingPid;shipping.processCreated=tree.shippingCreated;shipping.path=selected;
        c=setup(tree,selected,shipping);c->latestPower=0;preparationPrivateReads=0;
        const auto scope=nativeGameFactScope(shipping,g_gameRulesEpoch.load(),currentPowerGeneration());
        key=scope?nativeSteamGameTrackerFactKey(*scope):nullptr;check(bool(key),"actual canonical original game fact key");
        SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Startup);
        {std::lock_guard<std::mutex> lock(c->mx);check(c->pending.size()==1,"original startup enqueue");original=c->pending.front();c->pending.pop_front();}
        check(original.sequence==c->sequence&&original.sequence!=0,"must use real original Pending sequence");
    }
    ~RunCase(){stopController(c);}
    void start(){c->active=true;SteamOriginalRecovery::runOne(c,original,config);c->active=false;}
};
struct BlockedFlight final {
    struct Block {std::promise<void> entered,release;std::shared_future<void> released=release.get_future().share();std::atomic<bool> first{true},returned{false};};
    std::shared_ptr<Block> state=std::make_shared<Block>();
    sf::TrackerDemand demand;sf::TrackerCompletionWitness witness;
    std::future<sf::TrackerFreshObservation> operation;
    bool joined=false;
    BlockedFlight(const RunCase& test,const InertRoles::InertTree& tree,const std::wstring& selected){
        auto& owner=sf::trackerOwner();auto issued=owner.beginDemand(test.key,{},[]{return true;});
        auto claim=owner.claim(std::move(issued.demand),[]{return true;},sf::TrackerReadMode::RequireNewSource);
        check(claim.status==sf::TrackerClaimStatus::Claimed&&bool(claim.completion),"new physical blocking flight actually claimed");
        witness=claim.completion;demand=std::move(claim.request);
        auto lease=sf::processOwner().acquire(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),{tree.shippingPid,tree.shippingCreated,selected});
        check(bool(lease),"actual held original process lease for blocking source");
        auto entered=state->entered.get_future();
        fixtureFreshReadHook=[block=state](const std::wstring& path){
            if(path.ends_with(L"console_log.txt")&&block->first.exchange(false)){block->entered.set_value();block->released.wait();}
        };
        operation=std::async(std::launch::async,[token=std::move(claim.token),lease]()mutable{
            return sf::trackerOwner().observeFresh(std::move(token),lease,[]{return true;},sf::TrackerGroupCapturePolicy::Capture);
        });
        check(entered.wait_for(std::chrono::seconds(5))==std::future_status::ready,"original Console stage holds physical token");
    }
    void finish(){
        if(joined)return;
        if(!state->returned.exchange(true))state->release.set_value();
        auto result=operation.get();joined=true;fixtureFreshReadHook={};
        check(result.complete&&result.processGroupCapture&&result.processGroupCapture->lease,"actual blocking source completed its original count2 observation");
    }
    ~BlockedFlight(){
        if(!joined){if(!state->returned.exchange(true))state->release.set_value();if(operation.valid())operation.wait();fixtureFreshReadHook={};}
    }
};
static void driveAll(const std::shared_ptr<SC>& c){unsigned count=0;while(driveOne(c)){if(++count>16)throw std::runtime_error("consumer created a continuation loop");}}
static void requireWaiting(const RunCase& test){
    check(engineCalls.empty()&&preparationPrivateReads==0,"Busy/terminal hint may not grant role or private read");
    check(test.c->startupFreshRequest&&test.c->startupFreshWaits[0]&&test.c->startupFreshWaits[1],"both original route waits retained");
    check(test.c->pending.empty()&&g_steamTrackerCompletionDispatcher->retained()==0,"Console held: no completion or requeued business yet");
    for(const auto& wait:test.c->startupFreshWaits)check(wait->original.sequence==test.original.sequence&&
        wait->request->operationReadScope&&wait->nativeReadScope&&wait->subscription,"same original pending and actual two scopes retained");
}
static void requireOriginalCalls(const RunCase& test,const InertRoles::InertTree& tree){
    check(engineCalls.size()==2,"one engine eligibility observation per original route");
    for(const auto& call:engineCalls){
        check(call.request.at("trigger").at("id").get<std::string>().ends_with("-"+std::to_string(test.original.sequence)),"completion must keep original event sequence");
        check(call.request.at("target").at("pid")==tree.shippingPid&&call.group&&call.group->selectedGame().pid==tree.shippingPid,"original Shipping not console launcher");
        check(call.request.at("target").at("gameId")==std::to_string(fixtureFull),"original full type2 ID");
        check(call.currentAtEntry&&call.observationAtEntry&&call.readScope,"original full current gates reached");
    }
    check(test.c->sequence==test.original.sequence&&test.c->pending.empty()&&!test.c->startupFreshRequest,"no new request, pending or retained completion scopes");
}
static void bumpSelection(){
    steamOriginalRecoveryBusinessCommit(true,[]{std::lock_guard<std::mutex> lock(g_gameValveMx);g_gameValveTarget.identityGeneration+=2;});
}

// New explicit cases only. The actual complete old fixture helpers above remain
// the source of public owner/shared32/Count2/ReadScope setup and flight control.
namespace cmd=ymcc::steamcommand;
struct PublicCommandOwner {
 cmd::Coordinator coordinator;cmd::Provider provider;cmd::Ticket held;
 PublicCommandOwner():provider(coordinator){
  check(bool(provider),"single original public Coordinator provider");const auto client=SleepSteamRecovery::session();
  held=coordinator.reserve({client.pid,client.created,client.root},cmd::Plan::Single);check(bool(held),"ordinary command occupies exact Steam instance");
 }
 ~PublicCommandOwner(){held.parentReturned();}
 void release(){held.parentReturned();}
};
static void resetCommandCounters(){fixtureAdmissionCalls=0;fixtureFreshReadCalls=0;fixturePublisherCalls=0;fixtureTrace={};fixtureRouteContext={};}
static unsigned fixtureCount(FixturePoint point,unsigned begin=0){
 unsigned result=0;for(unsigned i=begin;i<fixtureTrace.used;++i)if(fixtureTrace.rows[i].point==point)++result;return result;
}
static void fixtureRequireOrderedResume(unsigned begin,unsigned route,unsigned long long sequence){
 check(!fixtureTrace.overflow,"bounded POD observation buffer must not overflow");
 unsigned fresh=0,admit=0,engine=0,fi=256,ai=256,ei=256,otherFresh=0,otherAdmit=0,otherEngine=0;
 for(unsigned i=begin;i<fixtureTrace.used;++i){const auto& row=fixtureTrace.rows[i];
  std::fprintf(stderr,"OBS route=%u sequence=%llu point=%u mayMutate=%u commandSuffix=%u\n",row.route,row.sequence,static_cast<unsigned>(row.point),unsigned(row.mayMutate),unsigned(row.commandSuffix));
  check(row.sequence==sequence,"continuation observations keep original Pending sequence");
  if(row.route==route){
   check(row.commandSuffix&&row.mayMutate,"resumed route retains exact command suffix and original mutation role");
   if(row.point==FixturePoint::FreshApi){++fresh;fi=i;}
   if(row.point==FixturePoint::AdmitCall){++admit;ai=i;}
   if(row.point==FixturePoint::EngineEntry){++engine;ei=i;}
  }else{
   check(!row.commandSuffix&&!row.mayMutate,"other original route remains a separate read-only observation after terminal");
   if(row.point==FixturePoint::FreshApi)++otherFresh;
   if(row.point==FixturePoint::AdmitCall)++otherAdmit;
   if(row.point==FixturePoint::EngineEntry)++otherEngine;
  }
 }
 std::fprintf(stderr,"ORDER route=%u sequence=%llu fresh=%u admit=%u engine=%u otherFresh=%u otherAdmit=%u otherEngine=%u\n",route,sequence,fresh,admit,engine,otherFresh,otherAdmit,otherEngine);
 check(fresh==1&&admit==1&&engine==1,"Ready resumed route requires exactly one new fresh API entry, one actual admit call and one engine entry");
 check(fi<ai&&ai<ei,"exact resumed route ordering fresh then admit then engine");
}
static const char* fixtureSelectedCase=nullptr;
static unsigned fixtureExpectedCaseCount=6;
static unsigned fixtureAdvisoryMode=0;
static bool fixtureParseStartupSelection(int argc,wchar_t** argv) noexcept {
 if((argc!=3&&argc!=5)||!argv[1][0]||std::wstring_view(argv[1]).starts_with(L"--")||std::wstring_view(argv[2])!=L"--command-pending-only")return false;
 if(argc==3){fixtureSelectedCase=nullptr;fixtureExpectedCaseCount=6;return true;}
 if(std::wstring_view(argv[3])!=L"--case"||!argv[4][0])return false;
 const wchar_t* wide[]={L"command-pending-ready-invalidations-no-admit",L"command-pending-ordinary-off-exit-no-debt-wait",L"command-pending-tracker-and-command-suffix-no-replay",L"command-pending-consumed-receipt-no-refund",L"command-pending-start-signal-allocation-failure-worker-call"};
 const char* narrow[]={"command-pending-ready-invalidations-no-admit","command-pending-ordinary-off-exit-no-debt-wait","command-pending-tracker-and-command-suffix-no-replay","command-pending-consumed-receipt-no-refund","command-pending-start-signal-allocation-failure-worker-call"};
 for(unsigned i=0;i<5;++i)if(std::wstring_view(argv[4])==wide[i]){fixtureSelectedCase=narrow[i];fixtureExpectedCaseCount=1;return true;}
 @@ADVISORY_CASE_TABLE@@
 return false;
}
template<class F>static void startupCommandCase(const char* name,F&& body){
 if(fixtureSelectedCase&&std::string_view(name)!=fixtureSelectedCase)return;
 test(name,[&]{
  check(sleepHelperCalls.load()==0,"unselected sleep-scope helper count is zero before Startup case");
  try{body();}catch(...){check(sleepHelperCalls.load()==0,"Startup unexpectedly entered deny-only sleep-scope helper");throw;}
  check(sleepHelperCalls.load()==0,"Startup case never executes sleep-scope rules/publication ports");
  check(fixtureWakeShimCalls.load()==0&&fixtureRulesShimCalls.load()==0&&fixturePowerReadyCalls.load()==0,"all unselected wake/rules/PowerReady fixture ports remain unused");
  fixtureShimChecks.push_back({{"name",name},{"sleepHelperCalls",sleepHelperCalls.load()},{"wakeShimCalls",fixtureWakeShimCalls.load()},{"rulesShimCalls",fixtureRulesShimCalls.load()},{"powerReadyCalls",fixturePowerReadyCalls.load()}});
 });
}
enum class FixtureCase2Point:unsigned {Enter,WaitingCheck,Leave};
struct FixtureCase2Row {
 FixtureCase2Point point;int mode;bool waitExists,requestExists,waitRequestExists,snapshotKnown,snapshotLockAcquired,diagnosticError,retainedFacts;
 unsigned long long originalSequence,controlSequence,requestSequence,waitSequence,waitRequestSequence;
 unsigned admit,fresh,engine,publisher;char phase[64],reason[192];
};
struct FixtureCase2Buffer {FixtureCase2Row rows[64];unsigned used;bool overflow;};
static thread_local FixtureCase2Buffer fixtureCase2Buffer{};
static thread_local int fixtureCase2Mode=-1;
static void fixtureCase2Text(char* dest,unsigned capacity,const std::string& value) noexcept {
 unsigned i=0;for(;i+1<capacity&&i<value.size();++i)dest[i]=value[i];dest[i]=0;
}
static void fixtureCase2Append(FixtureCase2Row row) noexcept {
 row.admit=fixtureAdmissionCalls.load();row.fresh=fixtureFreshReadCalls.load();row.engine=fixtureCount(FixturePoint::EngineEntry);row.publisher=fixturePublisherCalls.load();
 if(fixtureCase2Buffer.used==64){fixtureCase2Buffer.overflow=true;return;}fixtureCase2Buffer.rows[fixtureCase2Buffer.used++]=row;
}
static void fixtureCase2WaitingLocked(const RunCase& t,const SteamOriginalRecovery::StartupCommandWait* wait) noexcept {
 if(fixtureCase2Mode<0)return;
 FixtureCase2Row row{};row.point=FixtureCase2Point::WaitingCheck;row.mode=fixtureCase2Mode;
 row.waitExists=wait!=nullptr;row.requestExists=bool(t.c->startupFreshRequest);row.waitRequestExists=wait&&bool(wait->request);
 row.originalSequence=t.original.sequence;row.controlSequence=t.c->sequence;
 if(t.c->startupFreshRequest)row.requestSequence=t.c->startupFreshRequest->sequence;
 if(wait){row.waitSequence=wait->original.sequence;if(wait->request)row.waitRequestSequence=wait->request->sequence;}
 try{std::unique_lock<std::mutex> snapshot(t.c->snapshotMx,std::try_to_lock);row.snapshotLockAcquired=snapshot.owns_lock();
  if(snapshot.owns_lock()&&t.c->snapshot.is_object()){
   const auto phase=t.c->snapshot.find("phase"),reason=t.c->snapshot.find("reason");
   if(phase!=t.c->snapshot.end()&&phase->is_string()){fixtureCase2Text(row.phase,64,phase->get_ref<const std::string&>());row.snapshotKnown=true;}
   if(reason!=t.c->snapshot.end()&&reason->is_string())fixtureCase2Text(row.reason,192,reason->get_ref<const std::string&>());
  }
 }catch(...){row.diagnosticError=true;}
 fixtureCase2Append(row);
}
struct FixtureCase2ModeGuard {
 int previous;
 explicit FixtureCase2ModeGuard(unsigned mode) noexcept:previous(fixtureCase2Mode){fixtureCase2Mode=static_cast<int>(mode);FixtureCase2Row row{};row.point=FixtureCase2Point::Enter;row.mode=fixtureCase2Mode;fixtureCase2Append(row);}
 ~FixtureCase2ModeGuard(){FixtureCase2Row row{};if(fixtureCase2Buffer.used){row=fixtureCase2Buffer.rows[fixtureCase2Buffer.used-1];row.retainedFacts=true;}row.point=FixtureCase2Point::Leave;row.mode=fixtureCase2Mode;fixtureCase2Append(row);fixtureCase2Mode=previous;}
};
static json fixtureCase2DiagnosticsJson() noexcept {
 try{json rows=json::array();for(unsigned i=0;i<fixtureCase2Buffer.used;++i){const auto& r=fixtureCase2Buffer.rows[i];
  rows.push_back({{"point",static_cast<unsigned>(r.point)},{"mode",r.mode},{"waitExists",r.waitExists},{"requestExists",r.requestExists},{"waitRequestExists",r.waitRequestExists},{"originalSequence",r.originalSequence},{"controlSequence",r.controlSequence},{"requestSequence",r.requestSequence},{"waitSequence",r.waitSequence},{"waitRequestSequence",r.waitRequestSequence},{"snapshotKnown",r.snapshotKnown},{"snapshotLockAcquired",r.snapshotLockAcquired},{"retainedFactsFromLastRecord",r.retainedFacts},{"diagnosticError",r.diagnosticError},{"phase",r.phase},{"reason",r.reason},{"admit",r.admit},{"fresh",r.fresh},{"engine",r.engine},{"publisher",r.publisher}});
 }return {{"rows",std::move(rows)},{"overflow",fixtureCase2Buffer.overflow}};}catch(...){return nullptr;}
}
static std::shared_ptr<SteamOriginalRecovery::StartupCommandWait> waitingCommand(const RunCase& t){
 std::lock_guard<std::mutex> lock(t.c->mx);auto wait=t.c->startupCommandWait;
 fixtureCase2WaitingLocked(t,wait.get());
 check(wait&&wait->request==t.c->startupFreshRequest&&wait->request->sequence==t.original.sequence,"single retained command slot keeps original request/sequence");
 check(wait->phase==SteamOriginalRecovery::StartupCommandPhase::Watching&&wait->intent->status()==cmd::IntentStatus::Waiting,"registered command waits without new queue or admission");return wait;
}
static SteamOriginalRecovery::Pending readySuffix(const RunCase& t){
 SteamOriginalRecovery::Pending next;check(SteamOriginalRecovery::takeReadyStartupCommand(t.c,next),"actual complete takeReadyStartupCommand");
 check(next.startupCommand&&next.sequence==t.original.sequence&&!next.startupFresh,"exact request-local command suffix");return next;
}
static void nativeAppRefusal(InertRoles::InertTree& tree,const std::wstring& selected){
 const auto before=assertions;
 constexpr std::uint32_t app=1158310;
 const auto gameId=std::to_string(app),oldGameId=std::to_string(fixtureFull);
 const auto stamp="["+SleepSteamRecovery::localSecond(tree.shippingCreated)+"] ";
 const auto pid=std::to_string(tree.shippingPid);
 const auto writeLog=[](const wchar_t* name,const std::string& row){
  std::ofstream file(std::filesystem::path(fixtureFilesystemRoot+L"\\logs\\"+name),std::ios::binary|std::ios::trunc);
  file<<row<<'\n';file.close();check(bool(file),"write isolated native-AppID logfile row");
 };
 writeLog(L"gameprocess_log.txt",stamp+"AppID "+gameId+" adding PID "+pid+" as a tracked process \""+W2U(selected)+"\"");
 writeLog(L"console_log.txt",stamp+"Game process added : AppID "+gameId+" fixture, ProcID "+pid+", IP 0.0.0.0:0");

 RunCase t(tree,selected);resetCommandCounters();
 t.c->snapshot["trackerGameId"]=oldGameId;
 t.c->snapshot["trackerRead"]={{"appid",fixtureApp},{"nativePrimary",{{"expectedPid",tree.rootPid}}},{"previousGameOnly",true}};
 // Retain the actual request's per-route snapshots across runOne's normal retirement.
 std::shared_ptr<SteamOriginalRecovery::StartupFreshRequest> request;
 fixtureFreshReadHook=[&](const std::wstring& path){
  if(!request&&sf::samePath(path,fixtureFilesystemRoot+L"\\logs\\gameprocess_log.txt")){
   std::lock_guard lock(t.c->mx);request=t.c->startupFreshRequest;
  }
 };
 t.start();fixtureFreshReadHook={};
 check(request&&request->sequence==t.original.sequence,"public Tracker read retained the current Startup request");
 check(!fixtureTrace.overflow&&fixtureFreshReadCalls==2&&fixtureCount(FixturePoint::FreshApi)==2,"both routes entered the public fresh Tracker API");
 check(!t.c->ownsRunning&&!t.c->startupCommandWait&&t.c->pending.empty(),"native AppID refusal leaves no running owner or command work");
 check(fixtureAdmissionCalls==0&&fixtureCount(FixturePoint::AdmitCall)==0,"native AppID refusal never admits either route");
 check(engineCalls.empty()&&fixtureCount(FixturePoint::EngineEntry)==0,"native AppID refusal never enters the engine port");
 check(fixturePublisherCalls==0,"native AppID refusal never reaches the publisher port");

 const auto& routes=t.c->snapshot.at("dualRoutes");
 check(routes.size()==2,"runOne reports exactly two routes");
 for(const auto route:{SteamOriginalRecovery::RecoveryRoute::LegacyDynamic,SteamOriginalRecovery::RecoveryRoute::SemiStatic}){
  const auto index=SteamOriginalRecovery::startupFreshRouteIndex(route);
  const auto& receipt=request->routeObservations[index];
  check(routes[index].at("route")==ymcc::steamOriginal::routeName(route)&&routes[index].at("phase")=="unqualified"&&
     routes[index].at("reason")=="non-type2-gameid"&&routes[index].at("engineDispatched")==false,"runOne reports non-type2 refusal for each route");
  check(receipt.at("trackerGameId")==gameId&&receipt.at("target").at("pid")==tree.shippingPid,"route receipt identifies the current GameID and PID");
  const auto& read=receipt.at("trackerRead");const auto& primary=read.at("nativePrimary");
  check(read.at("complete")==true&&read.at("bindingStatus")=="tracked"&&read.at("gameIdKind")=="native-app"&&
     read.at("appid")==app&&read.at("actionAuthority")==false,"route receipt reports complete native-AppID Tracker evidence without authority");
  check(primary.at("appid")==app&&primary.at("expectedPid")==tree.shippingPid&&primary.at("nativePrimaryPid")==tree.shippingPid,
     "route receipt joins the new AppID and PID from the isolated logs");
  check(!read.contains("previousGameOnly")&&receipt.at("trackerGameId")!=oldGameId&&primary.at("expectedPid")!=tree.rootPid,
     "route receipt does not retain the previous game's Tracker diagnostic");
 }
 check(t.c->snapshot.at("trackerGameId")==gameId&&t.c->snapshot.at("trackerRead")==request->routeObservations[0].at("trackerRead"),
    "final runOne snapshot retains the current refusal diagnostic");
 std::cout<<"NATIVE_APP_REFUSAL_OBSERVATION "<<json{{"assertions",assertions-before},{"appid",app},{"pid",tree.shippingPid},
    {"previousGameId",oldGameId},{"previousPid",tree.rootPid},{"routes",request->routeObservations},
    {"freshReads",fixtureFreshReadCalls.load()},{"admissionCalls",fixtureAdmissionCalls.load()},
    {"engineCalls",engineCalls.size()},{"publisherCalls",fixturePublisherCalls.load()}}.dump()<<std::endl;
}
static void advisoryScenarios(InertRoles::InertTree& tree,const std::wstring& selected){
 CompletionWindow window;
 test(fixtureSelectedCase,[&]{
  RunCase t(tree,selected);PublicCommandOwner publicOwner;resetCommandCounters();
  const auto oldSequence=t.c->sequence;
  const auto offerIdentity=[&]{
   auto c=t.c;SteamOriginalRecovery::Identity game,steam;std::string gameId;std::uint32_t app=0;
   {std::lock_guard lock(c->mx);game=c->boundGame;steam=c->boundSteam;gameId=c->boundGameId;app=c->boundAppId;}
   steamOriginalRecoveryIdentityReady(game.pid,game.birth,steam.pid,steam.birth,gameId,app,g_sleepRecoveryAutoConfigEpoch.load(),g_gameRulesEpoch.load());
  };
  if(fixtureAdvisoryMode==9){
   publicOwner.release();BlockedFlight source(t,tree,selected);t.start();requireWaiting(t);source.finish();pumpCompletion();auto request=t.c->startupFreshRequest;auto first=t.c->startupFreshWaits[0];
   {std::lock_guard lock(t.c->mx);t.c->pending.clear();first->phase=SteamOriginalRecovery::StartupFreshPhase::Running;t.c->startupFreshWaits[1]->phase=SteamOriginalRecovery::StartupFreshPhase::Cancelled;t.c->startupFreshWaits[1]->subscription.cancel();t.c->active=true;}
   check(SteamOriginalRecovery::startupFreshCurrent(t.c,request,first),"controlled Running boundary retains real original current scopes");steamOriginalRecoveryBindingReady();t.c->active=false;
   std::cout<<"NOTIFICATION_CONTINUITY_OBSERVATION "<<json{{"mode",fixtureAdvisoryMode},{"sequenceBefore",oldSequence},{"sequenceAfter",t.c->sequence},{"pending",t.c->pending.size()},{"requestCancelled",request->cancelled.load()},{"publisherCalls",fixturePublisherCalls.load()},{"controlledPhaseBoundary",true},{"originalRuntimeConsumer",true},{"actualTrackerFlight",true}}.dump()<<std::endl;
   check(request->cancelled&&t.c->sequence==oldSequence+1&&t.c->pending.size()==1,"Running phase alone never swallows the next readiness");return;
  }
  if(fixtureAdvisoryMode==5){
   publicOwner.release();BlockedFlight source(t,tree,selected);t.start();requireWaiting(t);auto request=t.c->startupFreshRequest;
   for(unsigned i=0;i<32;++i)check(postSteamTrackerCompletionDispatch([]{}),"fill existing bounded completion transport");
   check(!postSteamTrackerCompletionDispatch([]{}),"existing completion transport refuses overflow");source.finish();
   bool rejected=true;for(const auto& wait:t.c->startupFreshWaits)rejected=rejected&&wait&&wait->subscription.delivery()==sf::TrackerCompletionDelivery::Rejected;
   check(rejected,"actual completed source post rejected for both existing subscriptions");steamOriginalRecoveryBindingReady();pumpCompletion();
   std::cout<<"NOTIFICATION_CONTINUITY_OBSERVATION "<<json{{"mode",fixtureAdvisoryMode},{"sequenceBefore",oldSequence},{"sequenceAfter",t.c->sequence},{"pending",t.c->pending.size()},{"requestCancelled",request->cancelled.load()},{"publisherCalls",fixturePublisherCalls.load()},{"deliveryRejected",rejected},{"originalRuntimeConsumer",true},{"actualTrackerFlight",true}}.dump()<<std::endl;
   check(request->cancelled&&t.c->sequence==oldSequence+1&&t.c->pending.size()==1,"lost completion keeps original advisory fallback enqueue");return;
  }
  if(fixtureAdvisoryMode==2){
   publicOwner.release();BlockedFlight source(t,tree,selected);t.start();requireWaiting(t);const auto request=t.c->startupFreshRequest;
   const auto first=t.c->startupFreshWaits[0],second=t.c->startupFreshWaits[1];source.finish();pumpCompletion();const auto pendingBefore=t.c->pending.size();steamOriginalRecoveryBindingReady();
   std::cout<<"NOTIFICATION_CONTINUITY_OBSERVATION "<<json{{"mode",fixtureAdvisoryMode},{"sequenceBefore",oldSequence},{"sequenceAfter",t.c->sequence},{"pending",t.c->pending.size()},{"pendingBefore",pendingBefore},{"requestCancelled",request->cancelled.load()},{"firstCancelled",first->phase==SteamOriginalRecovery::StartupFreshPhase::Cancelled},{"secondCancelled",second->phase==SteamOriginalRecovery::StartupFreshPhase::Cancelled},{"powerReadyPortCalls",fixturePowerReadyCalls.load()},{"publisherCalls",fixturePublisherCalls.load()},{"freshReads",fixtureFreshReadCalls.load()},{"originalRuntimeConsumer",true},{"actualTrackerFlight",true}}.dump()<<std::endl;
   check(!request->cancelled&&t.c->sequence==oldSequence&&t.c->pending.size()==pendingBefore&&pendingBefore==2&&first->phase==SteamOriginalRecovery::StartupFreshPhase::Queued&&second->phase==SteamOriginalRecovery::StartupFreshPhase::Queued,"ADVISORY_CANCELLED_CURRENT_FRESH_CONTINUATION");
   fixtureEngineReceipt=[](const json&){return json{{"status","healthy"},{"consumed",false}};};source.finish();pumpCompletion();driveAll(t.c);check(engineCalls.size()==1&&fixturePublisherCalls==1,"first healthy original completion closes the other retained route without duplicate publisher");
   check(engineCalls.front().group&&engineCalls.front().group->graph().count==2&&engineCalls.front().request["target"]["pid"]==tree.shippingPid&&engineCalls.front().request["trigger"]["id"].get<std::string>().ends_with("-"+std::to_string(oldSequence)),"healthy completion keeps actual original Count2 and Pending identity");
   check(t.c->sequence==oldSequence&&t.c->pending.empty()&&!t.c->startupFreshRequest,"both original completion slots retire without a new request");
   return;
  }
  t.start();auto wait=waitingCommand(t);const auto request=wait->request;
  if(fixtureAdvisoryMode==8){
   auto oldWait=wait;const auto client=SleepSteamRecovery::session();auto intent=publicOwner.coordinator.prepareIntent({client.pid,client.created,client.root},oldWait->signal);
   check(intent&&intent->status()==cmd::IntentStatus::Observing,"actual unpromoted intent is Observing");
   wait=std::make_shared<SteamOriginalRecovery::StartupCommandWait>(oldWait->request,oldWait->original,oldWait->route,oldWait->scope,oldWait->gameId,oldWait->appid,oldWait->account64,oldWait->mayMutate,oldWait->signal,intent);
   {std::lock_guard lock(t.c->mx);t.c->startupCommandWait=wait;}
   check(SteamOriginalRecovery::startupCommandCurrent(t.c,wait),"controlled Arming boundary has real original current request");offerIdentity();
   std::cout<<"NOTIFICATION_CONTINUITY_OBSERVATION "<<json{{"mode",fixtureAdvisoryMode},{"sequenceBefore",oldSequence},{"sequenceAfter",t.c->sequence},{"pending",t.c->pending.size()},{"requestCancelled",request->cancelled.load()},{"publisherCalls",fixturePublisherCalls.load()},{"controlledPhaseBoundary",true},{"originalRuntimeConsumer",true},{"actualTrackerFlight",false}}.dump()<<std::endl;
   check(request->cancelled&&t.c->sequence==oldSequence+1&&t.c->pending.size()==1&&intent->status()==cmd::IntentStatus::Revoked,"Arming unpromoted intent cannot swallow readiness");return;
  }
  if(fixtureAdvisoryMode==6){t.c->deferredWake=currentPowerGeneration();t.c->latestPower=currentPowerGeneration();steamOriginalRecoveryBindingReady();}
  else if(fixtureAdvisoryMode==7)steamOriginalRecoveryBindingReady();
  else if(fixtureAdvisoryMode==3)SteamOriginalRecovery::enqueue(t.c,ymcc::steamOriginal::Trigger::TargetChanged);
  else{
   if(fixtureAdvisoryMode==4){Registry registry(contextRegistryPath,false);registry.number(L"ActiveUser",43);check(until([]{auto s=g_steamSettingsObserver.identitySnapshot();return s&&s->session.account==43;}),"actual public account epoch changed before advisory");}
   offerIdentity();
  }
  std::cout<<"NOTIFICATION_CONTINUITY_OBSERVATION "<<json{{"mode",fixtureAdvisoryMode},{"sequenceBefore",oldSequence},{"sequenceAfter",t.c->sequence},{"pending",t.c->pending.size()},{"requestCancelled",request->cancelled.load()},{"commandRevoked",wait->intent->status()==cmd::IntentStatus::Revoked},{"sameWait",t.c->startupCommandWait==wait},{"publisherCalls",fixturePublisherCalls.load()},{"freshReads",fixtureFreshReadCalls.load()},{"originalRuntimeConsumer",true},{"actualTrackerFlight",false}}.dump()<<std::endl;
  if(fixtureAdvisoryMode==1||fixtureAdvisoryMode==7){
   check(!request->cancelled&&wait->intent->status()==cmd::IntentStatus::Waiting&&t.c->startupCommandWait==wait&&t.c->sequence==oldSequence&&t.c->pending.empty(),"ADVISORY_CANCELLED_CURRENT_COMMAND_CONTINUATION");
   fixtureEngineReceipt=[](const json&){return json{{"status","healthy"},{"consumed",false}};};publicOwner.release();driveAll(t.c);
   check(fixturePublisherCalls==1&&engineCalls.size()==2&&t.c->sequence==oldSequence,"original Ready suffix resumes exact same request with one publisher");
  }else if(fixtureAdvisoryMode==6){
   bool wake=false;for(const auto& pending:t.c->pending)wake=wake||pending.kind==ymcc::steamOriginal::Trigger::Wake;
   check(wake&&request->cancelled&&wait->intent->status()==cmd::IntentStatus::Revoked,"actual PowerReady preserves deferred Wake cancellation and enqueue");
  }else{
   check(request->cancelled&&wait->intent->status()==cmd::IntentStatus::Revoked&&!t.c->startupCommandWait&&t.c->sequence==oldSequence+1&&t.c->pending.size()==1,"real change or stale scope still revokes and queues");
   check(engineCalls.empty()&&fixturePublisherCalls==0,"notification cannot grant engine or publisher permission");
  }
 });
}

void scenarios(InertRoles::InertTree& tree,const std::wstring& selected,const NativeDetectedGame&){
 if(fixtureAdvisoryMode){advisoryScenarios(tree,selected);return;}
 CompletionWindow window;
 startupCommandCase("command-pending-busy-release-fresh-before-admit",[&]{
  RunCase t(tree,selected);PublicCommandOwner owner;resetCommandCounters();t.start();auto wait=waitingCommand(t);
  check(fixtureAdmissionCalls==0&&engineCalls.empty()&&fixturePublisherCalls==0&&fixtureCount(FixturePoint::AdmitCall)==0&&fixtureCount(FixturePoint::EngineEntry)==0,"Busy is before original admit and engine for both routes");
  const auto original=t.original.sequence;const auto resumedRoute=static_cast<unsigned>(wait->route);const auto observationBegin=fixtureTrace.used;
  fixtureEngineReceipt=[](const json&){return json{{"status","healthy"},{"consumed",false}};};owner.release();
  check(wait->intent->status()==cmd::IntentStatus::Ready,"atomic real Coordinator promotion");check(driveOne(t.c),"original serial worker business pass consumes Ready command");
  fixtureRequireOrderedResume(observationBegin,resumedRoute,original);
  check(fixturePublisherCalls==1&&t.c->sequence==original,"both original routes cannot elect another publisher after terminal receipt");
  check(engineCalls.front().group&&engineCalls.front().group->graph().count==2&&engineCalls.front().readScope,"actual Count2 and actual ReadScope reach typed adapter");
 });
 startupCommandCase("command-pending-ready-invalidations-no-admit",[&]{
  for(unsigned mode=0;mode<4;++mode){
   FixtureCase2ModeGuard diagnosticMode(mode);
   RunCase t(tree,selected);PublicCommandOwner owner;resetCommandCounters();
   t.c->snapshot["trackerRead"]={{"bindingStatus","unknown"},{"nativePrimary",{{"expectedPid",33208}}}};
   t.c->snapshot["trackerGameId"]="10963138536877850624";
   t.start();waitingCommand(t);
   check(t.c->snapshot.contains("trackerRead")&&t.c->snapshot["trackerRead"].is_null()&&
      t.c->snapshot["trackerGameId"]=="","previous game tracker diagnostic survived current Startup");
   owner.release();auto next=readySuffix(t);
   Registry registry(contextRegistryPath,false);
   if(mode==0){registry.number(L"ActiveUser",43);check(until([]{auto s=g_steamSettingsObserver.identitySnapshot();return s&&s->session.account==43;}),"actual Observer account changed after Ready");}
   if(mode==1)bumpSelection();
   if(mode==2){fixturePowerGeneration.fetch_add(1);g_powerLifecycle=PowerLifecycle::Resuming;}
   if(mode==3)g_gameRulesEpoch.fetch_add(1);
   SteamOriginalRecovery::runOne(t.c,next,config);
   check(fixtureAdmissionCalls==0&&engineCalls.empty()&&fixturePublisherCalls==0,"Ready never substitutes for fresh account/target/power/rules currentness");
   if(mode==0){registry.number(L"ActiveUser",42);check(until([]{auto s=g_steamSettingsObserver.identitySnapshot();return s&&s->session.account==42;}),"restore fixture account");}
  }
  if(fixtureSelectedCase)nativeAppRefusal(tree,selected);
 });
 startupCommandCase("command-pending-ordinary-off-exit-no-debt-wait",[&]{
  for(unsigned mode=0;mode<3;++mode){
   RunCase t(tree,selected);PublicCommandOwner owner;check(owner.held.attachChild(71)&&owner.held.grant(cmd::Effect::Mutation,0),"retained old logical child grant");resetCommandCounters();t.start();auto wait=waitingCommand(t);
   if(mode==0)SteamOriginalRecovery::enqueue(t.c,ymcc::steamOriginal::Trigger::SteamChanged);
   if(mode==1)steamOriginalRecoveryBusinessCommit(false,[]{g_sleepRecoveryAutoEnabled=false;g_sleepRecoveryAutoConfigEpoch.fetch_add(1);});
   if(mode==2){g_exitRequested=true;steamOriginalRecoveryRequestStop();}
   check(wait->intent->status()==cmd::IntentStatus::Revoked&&!t.c->startupCommandWait,"ordinary OFF exit retire only original pending intent");
   check(owner.held.held()&&fixtureAdmissionCalls==0&&engineCalls.empty(),"cancel never waits old debt or refunds its grant");
   SteamOriginalRecovery::Pending stale=wait->original;stale.startupCommand=wait;SteamOriginalRecovery::runOne(t.c,stale,config);
   check(fixtureAdmissionCalls==0&&engineCalls.empty(),"retired suffix cannot revive after ordinary/OFF/exit");
  }
  g_exitRequested=false;
 });
 startupCommandCase("command-pending-tracker-and-command-suffix-no-replay",[&]{
  RunCase t(tree,selected);BlockedFlight source(t,tree,selected);PublicCommandOwner owner;resetCommandCounters();t.start();requireWaiting(t);source.finish();pumpCompletion();
  check(driveOne(t.c),"actual public tracker completion reaches original worker");auto wait=waitingCommand(t);const auto request=wait->request;
  // Other route's tracker suffix must not retire this request's command slot.
  driveAll(t.c);check(t.c->startupFreshRequest==request&&t.c->startupCommandWait==wait&&!request->cancelled,"tracker suffix and command suffix retain same owner");
  fixtureEngineReceipt=[](const json&){return json{{"status","healthy"},{"consumed",false}};};owner.release();driveAll(t.c);const auto published=fixturePublisherCalls.load();
  pumpCompletion();driveAll(t.c);check(published==1&&fixturePublisherCalls==published&&g_steamTrackerCompletionDispatcher->retained()==0,"both suffix sources drain without second publisher or replay");
 });
 startupCommandCase("command-pending-consumed-receipt-no-refund",[&]{
  RunCase t(tree,selected);PublicCommandOwner owner;resetCommandCounters();t.start();waitingCommand(t);firstConsumed=true;owner.release();driveAll(t.c);
  check(fixturePublisherCalls==1,"original consumed raw receipt is terminal even without success");
  const auto calls=fixturePublisherCalls.load();SteamOriginalRecovery::enqueue(t.c,ymcc::steamOriginal::Trigger::Startup);driveAll(t.c);
  check(fixturePublisherCalls==calls,"old consumed original pair admission not refunded by new hint");
 });
 startupCommandCase("command-pending-start-signal-allocation-failure-worker-call",[&]{
  // A callback port, not a proof of a real product thread launch. The full
  // original Start body and exact catch/start ordering are preserved/inversed.
  SteamOriginalRecovery::callbackControl.reset();fixtureWorkerStartCalls=0;fixtureCapturedWorker={};fixtureFailAvailabilityAllocation=true;
  steamOriginalRecoveryStart();auto c=SteamOriginalRecovery::current();
  check(c&&!c->mutationAvailability&&fixtureWorkerStartCalls==1&&bool(fixtureCapturedWorker),"optional signal bad_alloc did not bypass original worker.start call");
  {std::lock_guard<std::mutex> lock(c->mx);check(c->pending.size()==1&&c->pending.front().kind==ymcc::steamOriginal::Trigger::Startup,"original Startup enqueue remains after callback registration");}
  steamOriginalRecoveryRequestStop();fixtureCapturedWorker={};c->observationEvent.reset();SteamOriginalRecovery::callbackControl.reset();
 });
 check(g_steamTrackerCompletionDispatcher->retained()==0,"actual shared32 transport drained");
}

// New source-prepared cases; no prior execution or formal case ID is claimed.
namespace FixtureProductionControlOriginal {
@@PRODUCTION_START@@
}
struct PCLife {unsigned serial=0;std::uintptr_t handle=0;bool live=false,refused=false;unsigned attempts=0;LONG status=0;DWORD error=0;};
struct PCAccount {PCLife nodes[16]{};unsigned used=0,live=0,refusals=0;bool fault=false;};
static PCAccount pcAccountAfterJoin()noexcept {
 PCAccount out;const auto count=pcUsed.load(std::memory_order_acquire);
 if(pcOverflow||count>=std::size(pcRecords)){out.fault=true;return out;}
 for(unsigned i=0;i<count;++i){const auto& r=pcRecords[i];if(!r.thread){out.fault=true;continue;}
  if(r.kind==1){if(!r.result||!r.handle||out.used>=std::size(out.nodes)){out.fault=true;continue;}
   for(unsigned j=0;j<out.used;++j)if(out.nodes[j].live&&out.nodes[j].handle==r.handle)out.fault=true;
   out.nodes[out.used++]={i+1,r.handle,true,false,0,0,0};
  }else if(r.kind==2){PCLife* node=nullptr;for(unsigned j=out.used;j>0;--j)if(out.nodes[j-1].live&&out.nodes[j-1].handle==r.handle){node=&out.nodes[j-1];break;}
   if(!node){out.fault=true;continue;}++node->attempts;node->status=r.result;node->error=r.error;
   if(r.result!=FALSE)node->live=false;else{node->refused=true;++out.refusals;out.fault=true;}
  }else out.fault=true;
 }
 for(unsigned i=0;i<out.used;++i)if(out.nodes[i].live)++out.live;return out;
}
static const PCLife* pcFind(const PCAccount& a,HANDLE h)noexcept {for(unsigned i=a.used;i>0;--i)if(a.nodes[i-1].handle==reinterpret_cast<std::uintptr_t>(h))return &a.nodes[i-1];return nullptr;}
static void pcRequire(bool v,const char* text){if(!v)throw std::runtime_error(text);}
static int pcRun(const std::wstring& argument,unsigned mode){
 bool complete=false,joined=false,protectedHandle=false,lateReturned=false,initialRefusal=false,initialAvailabilityStrict=false;
 unsigned shutdownCalls=0;std::string error;HANDLE signalHandle=nullptr,observationHandle=nullptr;
 std::weak_ptr<SteamOriginalRecovery::Control> weakControl;std::weak_ptr<ymcc::steamcommand::AvailabilityEvent> weakSignal;std::weak_ptr<void> weakObservation;
 SteamOriginalRecovery::Control* originalControl=nullptr;ymcc::steamcommand::AvailabilityEvent* originalSignal=nullptr;
 std::shared_ptr<SteamOriginalRecovery::Control> held;
 PCAccount initial{};PCAccount final{};
 pcActive=true;
 try{
  const auto root=std::filesystem::absolute(argument);pcRequire(!std::filesystem::exists(root),"exclusive production-control root required");
  std::filesystem::create_directories(root);fixtureFilesystemRoot=root.wstring();
  pcEntered=pcCreateEventW(nullptr,TRUE,FALSE,nullptr);pcRelease=pcCreateEventW(nullptr,TRUE,FALSE,nullptr);pcRequire(pcEntered&&pcRelease,"owned handshake events");
  fixtureFailAvailabilityAllocation=(mode==1);
  FixtureProductionControlOriginal::steamOriginalRecoveryStart();
  held=SteamOriginalRecovery::current();pcRequire(bool(held),"original Control published");originalControl=held.get();weakControl=held;
  pcRequire(WaitForSingleObject(pcEntered,5000)==WAIT_OBJECT_0,"real worker waiting-events handshake");
  pcRequire(pcWorkerTid.load()!=0&&pcWorkerTid.load()!=GetCurrentThreadId()&&pcWaitingPublishes==1,"actual RecoveryWorker thread entered original run");
  pcRequire(!SteamOriginalRecovery::worker.finished(),"worker held inside original publish");
  {std::lock_guard<std::mutex> lock(held->mx);pcRequire(held->pending.size()==1&&held->pending.front().kind==ymcc::steamOriginal::Trigger::Startup,"Start enqueued original Startup before returning");}
  pcRequire(!fixtureFailAvailabilityAllocation.load(),"optional allocation fault consumed exactly at original leaf");
  if(mode==1)pcRequire(!held->mutationAvailability,"make_shared throw leaves optional signal absent, not a null-handle open failure");
  else{pcRequire(bool(held->mutationAvailability),"actual Availability object retained by Control");originalSignal=held->mutationAvailability.get();weakSignal=held->mutationAvailability;signalHandle=originalSignal->event();pcRequire(signalHandle!=nullptr,"actual Availability opened");}
  observationHandle=static_cast<HANDLE>(held->observationEvent.get());weakObservation=held->observationEvent;pcRequire(observationHandle!=nullptr,"original observation event allocated");
  steamOriginalRecoveryRequestStop();
  {std::lock_guard<std::mutex> lock(held->mx);pcRequire(held->stop&&held->pending.empty(),"original RequestStop clears Startup before worker gate release");}
  pcRequire(SetEvent(pcRelease)!=FALSE,"release original publish after stop");
  if(mode==2){pcRequire(SetHandleInformation(signalHandle,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE)!=FALSE,"protect exact owned Availability");protectedHandle=true;}
  steamOriginalRecoveryShutdown();++shutdownCalls;joined=true;
  SteamOriginalRecovery::worker.rethrowIfFailed();
  pcRequire(SteamOriginalRecovery::worker.finished()&&pcStoppedPublishes==1&&!pcGateTimedOut,"original run -> takeNext stop -> natural return and real join");
  pcRequire(pcRunOneCalls==0&&fixtureAdmissionCalls==0&&fixtureFreshReadCalls==0&&fixturePublisherCalls==0&&engineCalls.empty()&&registryReads==0&&nativeReadProcessQueries==0,"no runOne/fresh/engine/Observer/SourceOwner consumption");
  initial=pcAccountAfterJoin();const auto* signalNode=signalHandle?pcFind(initial,signalHandle):nullptr;
  if(mode==2){const auto status=originalSignal->status();pcRequire(status.opened&&status.closing&&status.closeError==ERROR_INVALID_HANDLE,"actual original close6 retains resource");pcRequire(signalNode&&signalNode->live&&signalNode->refused&&signalNode->status==FALSE&&signalNode->error==ERROR_INVALID_HANDLE,"actual failed CloseHandle and original creation serial debt");initialRefusal=true;initialAvailabilityStrict=false;}
  else{if(originalSignal){const auto status=originalSignal->status();pcRequire(!status.opened&&status.closeError==ERROR_SUCCESS&&signalNode&&!signalNode->live&&!signalNode->refused,"joined Shutdown returned exact Availability");}initialAvailabilityStrict=!initial.fault;}
  held.reset();
  pcRequire(!weakControl.expired()&&weakControl.use_count()==1&&SteamOriginalRecovery::control.get()==originalControl,"only original global Control owns itself after test strong reference dropped");
  if(originalSignal)pcRequire(!weakSignal.expired()&&weakSignal.use_count()==1&&originalControl->mutationAvailability.get()==originalSignal,"original Control alone retains same Availability object");
  const auto* observationNode=pcFind(initial,observationHandle);pcRequire(!weakObservation.expired()&&observationNode&&observationNode->live,"Shutdown did not release original observationEvent");
  if(mode==2){pcRequire(SetHandleInformation(signalHandle,HANDLE_FLAG_PROTECT_FROM_CLOSE,0)!=FALSE,"clear protection on exact retained handle");protectedHandle=false;
   steamOriginalRecoveryShutdown();++shutdownCalls;const auto status=originalSignal->status();pcRequire(!status.opened&&status.closeError==ERROR_SUCCESS&&originalControl->mutationAvailability.get()==originalSignal,"same Control and signal retry through original Shutdown");lateReturned=true;
   const auto retry=pcAccountAfterJoin();const auto* returned=pcFind(retry,signalHandle);pcRequire(returned&&signalNode&&returned->serial==signalNode->serial&&!returned->live&&returned->refused&&returned->attempts==2&&retry.fault,"late return never washes initial refusal");}
  {std::lock_guard<std::mutex> lock(SteamOriginalRecovery::lifecycleMx);SteamOriginalRecovery::control.reset();}
  pcRequire(weakControl.expired()&&weakObservation.expired()&&(!originalSignal||weakSignal.expired()),"final original owner destruction releases observation and object captures");
  const auto ownerReturned=pcAccountAfterJoin();const auto* observationReturn=pcFind(ownerReturned,observationHandle);pcRequire(observationReturn&&!observationReturn->live&&observationReturn->status!=FALSE,"actual observation CloseHandle only after final owner teardown");
  pcRequire(pcCloseHandle(pcRelease)!=FALSE,"return own release event");pcRelease=nullptr;pcRequire(pcCloseHandle(pcEntered)!=FALSE,"return own entered event");pcEntered=nullptr;
  final=pcAccountAfterJoin();pcRequire(final.live==0&&final.refusals==(mode==2?1u:0u)&&final.fault==(mode==2),"explicit event return and sticky refusal accounting");complete=true;
 }catch(const std::exception& e){error=e.what();
  try{steamOriginalRecoveryRequestStop();if(pcRelease)(void)SetEvent(pcRelease);if(protectedHandle&&signalHandle)(void)SetHandleInformation(signalHandle,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);steamOriginalRecoveryShutdown();++shutdownCalls;joined=true;}catch(...){}
  held.reset();if(joined){std::lock_guard<std::mutex> lock(SteamOriginalRecovery::lifecycleMx);SteamOriginalRecovery::control.reset();}
  if(joined){if(pcRelease){if(pcCloseHandle(pcRelease))pcRelease=nullptr;}if(pcEntered){if(pcCloseHandle(pcEntered))pcEntered=nullptr;}final=pcAccountAfterJoin();}
 }
 json records=json::array();const auto count=pcUsed.load();for(unsigned i=0;i<(std::min)(count,static_cast<unsigned>(std::size(pcRecords)));++i){const auto& r=pcRecords[i];records.push_back({{"slot",i},{"kind",r.kind},{"handle",std::to_string(r.handle)},{"result",r.result},{"error",r.error},{"tid",r.thread}});}
 std::cout<<json{{"schema","ymcc.private-production-control-combination.v1"},{"proposedCase",mode},{"checksComplete",complete},{"error",error},{"actualWorkerTid",pcWorkerTid.load()},{"fixtureTid",GetCurrentThreadId()},{"shutdownCalls",shutdownCalls},{"workerJoined",joined},{"runOneCalls",pcRunOneCalls.load()},{"initialAvailabilityStrict",initialAvailabilityStrict},{"initialRefusal",initialRefusal},{"lateReturned",lateReturned},{"initialSnapshotNotRecomputed",true},{"observationReturnedByFinalOwnerOnly",complete},{"eventLiveDebt",final.live},{"eventRefusals",final.refusals},{"eventStrictSuccess",!final.fault},{"eventRecords",records},{"sourceReady",false},{"processWideResourcePass",false},{"groupAcrossWait","NOT_VALIDATED"},{"nativeSteamAuthority",false},{"openFailureFallback","NOT_RUN"}}.dump(2)<<'\n';
 return complete?(mode==2?1:0):2;
}
static std::optional<int> fixtureProductionControlDispatch(int argc,wchar_t** argv){
 if(argc<3||!std::wstring_view(argv[2]).starts_with(L"--production-control-"))return {};
 if(argc!=3||!argv[1][0])return 41;
 const std::wstring_view selector(argv[2]);unsigned mode;
 if(selector==L"--production-control-success")mode=0;
 else if(selector==L"--production-control-optional-signal-bad-alloc")mode=1;
 else if(selector==L"--production-control-availability-protect-close6")mode=2;
 else return 41;
 return pcRun(argv[1],mode);
}
