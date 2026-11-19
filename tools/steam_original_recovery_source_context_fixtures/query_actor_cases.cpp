#include "query_actor_ports.hpp"
#define LoadLibraryExW portLoadLibrary
#define GetProcAddress portGetProcAddress
#define FreeLibrary portFreeLibrary
namespace before {
#include "before-actor.inc" // Historical Current baseline, NOT this phase's original.
}
namespace original {
#include "original-module-actor.inc" // This phase's module-leak control.
}
namespace after {
#include "after-actor.inc"
}
#undef LoadLibraryExW
#undef GetProcAddress
#undef FreeLibrary
static_assert(std::is_constructible_v<after::QueryActor,U64,std::wstring,RecoveryRoute,std::string>);
static_assert(std::is_nothrow_destructible_v<after::QueryActor>);

struct Outcome {
 bool accepted=false,handlesReturned=false,cleanupMatched=false,newCleanupMatched=false;
 std::string reason;std::vector<std::string> steps;
};
std::vector<std::string> originalSdkTrace(const std::vector<std::string>& steps){
 std::vector<std::string> result;
 for(const auto& step:steps)if(step!="BShutdownIfAllPipesClosed"&&step!="FreeLibrary")result.push_back(step);
 return result;
}
template<class Actor> Outcome exercise(RecoveryRoute route,const std::string& cancel,bool fire=true,bool mismatch=false){
 FixtureState state;state.cancelAt=cancel;state.wrongAccount=mismatch;fixture=&state;
 gCurrent=[&]{return state.live.load();};gCancelSignal=0;gIdentityCancelSignal=0;gPublicationScope.reset();
 Outcome out;DWORD initial=0,final=0;
 need(GetProcessHandleCount(GetCurrentProcess(),&initial)!=FALSE,"fixture-handle-count-before");
 try{{Actor query(77,L"offline-not-a-dll",route,"qualified-client");query.waitReady();
   if(cancel=="before-dispatch")state.live=false;
   if(fire){query.fire();query.waitComplete();}out.accepted=true;
 }}catch(const std::exception& error){out.reason=error.what();}
 need(GetProcessHandleCount(GetCurrentProcess(),&final)!=FALSE,"fixture-handle-count-after");
 out.handlesReturned=initial==final;out.steps=state.steps;
 const auto count=[&](const char* name){return std::count(out.steps.begin(),out.steps.end(),name);};
 out.cleanupMatched=count("ReleaseUser")==count("ConnectToGlobalUser")&&count("BReleaseSteamPipe")==count("CreateSteamPipe");
 out.newCleanupMatched=state.loads==state.freeSuccess&&state.moduleRefs==0&&state.userOwned==0&&state.pipeOwned==0&&
  state.shutdownSuccess==(state.factories>0?1:0)&&!state.prematureUnload&&!state.portViolation;
 gCurrent={};gCancelSignal=0;gIdentityCancelSignal=0;gPublicationScope.reset();fixture=nullptr;return out;
}
bool contains(const Outcome& row,const char* name){return std::find(row.steps.begin(),row.steps.end(),name)!=row.steps.end();}
J outcome(const Outcome& row){
 return {{"accepted",row.accepted},{"handlesReturned",row.handlesReturned},{"cleanupMatched",row.cleanupMatched},
  {"newCleanupMatched",row.newCleanupMatched},{"reason",row.reason},{"steps",row.steps},{"originalSdkTrace",originalSdkTrace(row.steps)}};
}
int original14(){
 // Preserve the original order, all old semantic assertions, and 14 identities.
 // Only the newly introduced shutdown/unload actions leave the trace comparison.
 J rows=J::array();
 try{
  for(const auto route:{RecoveryRoute::LegacyDynamic,RecoveryRoute::SemiStatic}){
   const std::string prefix=route==RecoveryRoute::LegacyDynamic?"dynamic/":"semi-static/";
   for(const auto& cancel:std::vector<std::string>{"qualification","ConnectToGlobalUser","GetISteamUser","GetSteamID"}){
    const bool fire=cancel=="GetSteamID";
    auto old=exercise<before::QueryActor>(route,cancel,fire),now=exercise<after::QueryActor>(route,cancel,fire);
    const bool violation=cancel=="qualification"?contains(old,"LoadLibraryExW"):cancel=="ConnectToGlobalUser"?contains(old,"GetISteamUser"):old.accepted;
    const bool stopped=cancel=="qualification"?!contains(now,"LoadLibraryExW"):cancel=="ConnectToGlobalUser"?!contains(now,"GetISteamUser"):!now.accepted;
    const bool pass=violation&&stopped&&!now.accepted&&now.reason=="normal-query-current-revoked"&&
     old.handlesReturned&&now.handlesReturned&&old.cleanupMatched&&now.cleanupMatched&&now.newCleanupMatched;
    rows.push_back({{"name",prefix+"cancel-during-"+cancel},{"pass",pass},{"originalViolation",violation},{"before",outcome(old)},{"after",outcome(now)}});
   }
   for(const auto& selection:std::vector<std::string>{"healthy","before-dispatch","account-mismatch"}){
    const bool mismatch=selection=="account-mismatch";
    auto old=exercise<before::QueryActor>(route,selection,true,mismatch),now=exercise<after::QueryActor>(route,selection,true,mismatch);
    const bool correct=selection=="healthy"?old.accepted&&now.accepted:selection=="before-dispatch"?
     !old.accepted&&!now.accepted&&!contains(old,"GetSteamID")&&!contains(now,"GetSteamID"):
     !old.accepted&&!now.accepted&&old.reason=="normal-query-account-mismatch"&&now.reason==old.reason;
    const bool pass=correct&&originalSdkTrace(old.steps)==originalSdkTrace(now.steps)&&
     old.handlesReturned&&now.handlesReturned&&old.cleanupMatched&&now.cleanupMatched&&now.newCleanupMatched;
    rows.push_back({{"name",prefix+selection},{"pass",pass},{"originalViolation",false},{"before",outcome(old)},{"after",outcome(now)}});
   }
  }
  const bool success=std::all_of(rows.begin(),rows.end(),[](const J& row){return row.at("pass").get<bool>();});
  std::cout<<J({{"success",success},{"cases",rows},{"executedThisRun",rows.size()},
   {"steamCallsArePorts",true},{"realLocalThreadsAndEvents",true},{"steamProcessAccess",false},
   {"focusOrRecoveryVerified",false},{"lifetimeCasesExecuted",0}}).dump()<<std::endl;
  return success?0:2;
 }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 3;}
}

struct LifetimeCase {const char* name;FixtureFault fault;FixtureBlock block;bool readyOnly=false;};
inline const std::vector<LifetimeCase> lifetimeCases{
 {"normal-return",FixtureFault::None,FixtureBlock::None},
 {"factory-missing",FixtureFault::FactoryMissing,FixtureBlock::None},
 {"factory-null",FixtureFault::FactoryNull,FixtureBlock::None},
 {"release-user-throws",FixtureFault::ReleaseUserThrow,FixtureBlock::None},
 {"release-pipe-false",FixtureFault::PipeFalse,FixtureBlock::None},
 {"release-pipe-throws",FixtureFault::PipeThrow,FixtureBlock::None},
 {"shutdown-false",FixtureFault::ShutdownFalse,FixtureBlock::None},
 {"free-library-false",FixtureFault::FreeLibraryFalse,FixtureBlock::None},
 {"ready-before-cleanup",FixtureFault::None,FixtureBlock::ReleaseUser,true},
 {"reply-before-cleanup",FixtureFault::None,FixtureBlock::ReleasePipe},
 {"sdk-getsteamid-throws",FixtureFault::GetSteamIdThrow,FixtureBlock::None},
 {"reply-current-revoked",FixtureFault::CurrentAfterReply,FixtureBlock::None}
};
struct OwnerResult {
 std::mutex mutex;
 std::atomic<bool> readyReturned{false},queryReturned{false},finishReturned{false},destructorReturned{false},ownerReturned{false};
 bool finishAvailable=false,summaryAvailable=false,secondRejected=false,secondHadNoCalls=false;
 std::string queryError,finishError,ownerError,secondError;
 J summary=J::object(),beforeCleanup=J::object();
 std::weak_ptr<ymcc::steamnative::ReadScope> scope;
};
template<class Actor> std::unique_ptr<Actor> makeActor(const std::wstring& dll,RecoveryRoute route,J& summary){
 if constexpr(std::is_constructible_v<Actor,U64,std::wstring,RecoveryRoute,std::string,J*>)
  return std::make_unique<Actor>(77,dll,route,"qualified-client",&summary);
 else return std::make_unique<Actor>(77,dll,route,"qualified-client");
}
bool quarantines(FixtureFault fault){
 return fault==FixtureFault::ReleaseUserThrow||fault==FixtureFault::PipeFalse||fault==FixtureFault::PipeThrow||
  fault==FixtureFault::ShutdownFalse||fault==FixtureFault::FreeLibraryFalse||fault==FixtureFault::GetSteamIdThrow;
}
const char* expectedCleanupReason(FixtureFault fault){
 switch(fault){
  case FixtureFault::ReleaseUserThrow:return "normal-query-release-user-outcome-unknown";
  case FixtureFault::PipeFalse:return "normal-query-release-pipe-refused";
  case FixtureFault::PipeThrow:return "normal-query-release-pipe-outcome-unknown";
  case FixtureFault::ShutdownFalse:return "normal-query-sdk-shutdown-refused";
  case FixtureFault::FreeLibraryFalse:return "normal-query-module-return-unknown";
  case FixtureFault::GetSteamIdThrow:return "normal-query-sdk-call-outcome-unknown";
  default:return "";
 }
}
template<class Actor> void consumeActor(const LifetimeCase& selection,const std::shared_ptr<FixtureState>& state,RecoveryRoute route,OwnerResult& out){
 try{
  gCurrent=[state]{return state->live.load();};
  gPublicationScope=std::make_shared<ymcc::steamnative::ReadScope>(state);out.scope=gPublicationScope;
  gCancelSignal=reinterpret_cast<WakeSignal>(gPublicationScope->powerCancellationEvent());
  gIdentityCancelSignal=reinterpret_cast<WakeSignal>(gPublicationScope->identityCancellationEvent());
  out.summaryAvailable=std::is_constructible_v<Actor,U64,std::wstring,RecoveryRoute,std::string,J*>;
  auto query=makeActor<Actor>(state->dllPath,route,out.summary);
  gPublicationScope.reset(); // Actor/Debt must independently own this lifetime.
  try{
   query->waitReady();out.readyReturned=true;
   if(!selection.readyOnly){query->fire();query->waitComplete();out.queryReturned=true;}
  }catch(const std::exception& error){out.queryError=error.what();}
  catch(...){out.queryError="fixture-nonstd-query-exception";}
  {std::lock_guard<std::mutex> lock(out.mutex);out.beforeCleanup=out.summary;}
  if(!selection.readyOnly){
   if constexpr(requires(Actor& value){value.finish();}){
    out.finishAvailable=true;
    try{query->finish();out.finishReturned=true;}catch(const std::exception& error){out.finishError=error.what();}
    catch(...){out.finishError="fixture-nonstd-finish-exception";}
   }
  }
  query.reset();out.destructorReturned=true;
  if(quarantines(selection.fault)){
   const auto beforeCalls=state->counters();
   try{
    auto second=makeActor<Actor>(state->dllPath,route,out.summary);
    second->waitReady(); // Unexpected acceptance is a real failure, not bypassed.
   }catch(const std::exception& error){out.secondError=error.what();out.secondRejected=out.secondError=="normal-query-sdk-quarantined";}
   catch(...){out.secondError="fixture-nonstd-second-construction";}
   out.secondHadNoCalls=state->counters()==beforeCalls;
  }
 }catch(const std::exception& error){out.ownerError=error.what();}
 catch(...){out.ownerError="fixture-nonstd-owner-exception";}
 gPublicationScope.reset();gCurrent={};gCancelSignal=0;gIdentityCancelSignal=0;
 out.ownerReturned=true;SetEvent(state->ownerDone.h);
}
bool hasCleanupReturn(const J& summary){
 return summary.is_object()&&summary.contains("normalQueryCleanup")&&summary["normalQueryCleanup"].is_object()&&
  summary["normalQueryCleanup"].value("resourcesReturned",false);
}
bool numericHandleLeak(const J& value){
 if(value.is_object())for(auto it=value.begin();it!=value.end();++it){
  auto name=it.key();for(auto& c:name)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
  if(it.value().is_number()&&(name=="module"||name=="client"||name=="pipe"||name=="user"||name=="hmodule"||
    name=="hsteampipe"||name=="hsteamuser"||name.find("handle")!=std::string::npos||name.find("pointer")!=std::string::npos))return true;
  if(numericHandleLeak(it.value()))return true;
 }else if(value.is_array())for(const auto& member:value)if(numericHandleLeak(member))return true;
 return false;
}
template<class Actor> int lifetime(const LifetimeCase& selection,const std::wstring& dll,RecoveryRoute route,bool moduleControl){
 auto state=std::make_shared<FixtureState>();state->realLoader=true;state->dllPath=dll;state->fault=selection.fault;state->block=selection.block;
 fixture=state.get();need(state->releaseEntered.good()&&state->releasePermit.good()&&state->ownerDone.good(),"fixture-controller-events");
 HMODULE alreadyLoaded=nullptr;
 need(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,dll.c_str(),&alreadyLoaded),"fixture-dll-must-start-unloaded");
 DWORD handlesBefore=0,handlesAfter=0;
 need(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"fixture-handle-count-before");
 OwnerResult result;J checks=J::array(),blocked=J(nullptr);
 const auto check=[&](const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});};
 std::thread owner([&]{consumeActor<Actor>(selection,state,route,result);});
 // Always open the gate before joining, even when an observation fails.
 try{
  if(selection.block!=FixtureBlock::None){
   const auto entered=WaitForSingleObject(state->releaseEntered.h,fixtureRemaining());
   check("blocked-release-entered",entered==WAIT_OBJECT_0);
   if(entered==WAIT_OBJECT_0){
    J snapshot;{std::lock_guard<std::mutex> lock(result.mutex);snapshot=result.beforeCleanup;}
    const bool waiting=WaitForSingleObject(owner.native_handle(),0)==WAIT_TIMEOUT;
    blocked={{"ownerThreadStillRunning",waiting},{"readyReturned",result.readyReturned.load()},
     {"queryReturned",result.queryReturned.load()},{"finishReturned",result.finishReturned.load()},
     {"destructorReturned",result.destructorReturned.load()},{"scopeStillHeld",!result.scope.expired()},
     {"moduleStillLoaded",state->moduleLoaded()},{"summaryBeforeCleanup",snapshot},{"counters",state->counters()}};
    check("ready-return-does-not-attest-cleanup",result.readyReturned&&!hasCleanupReturn(snapshot));
    check("reply-state-observed-separately",result.queryReturned==!selection.readyOnly);
    check("join-still-waits-for-sdk-return",waiting&&!result.finishReturned&&!result.destructorReturned);
    check("blocked-read-scope-held",!result.scope.expired()&&state->scopeCreated==1&&state->scopeDestroyed==0);
    check("blocked-loader-reference-held",state->moduleRefs==1&&state->freeCalls==0&&state->moduleLoaded());
    check("blocked-cleanup-is-not-returned",state->pipeOwned==1&&state->releasePipeSuccess==0&&state->shutdownCalls==0);
    check("single-blocked-release-call",state->gateWaits==1);
   }
  }
 }catch(const std::exception& error){checks.push_back({{"name","blocked-observation-error"},{"pass",false},{"error",error.what()}});}
 catch(...){check("blocked-observation-nonstd-error",false);}
 SetEvent(state->releasePermit.h);
 const auto returned=WaitForSingleObject(state->ownerDone.h,fixtureRemaining());
 check("owned-consumer-return-observed",returned==WAIT_OBJECT_0);
 // No detach, thread termination, or alternate ownership on failure. The parent
 // owns the bounded process Job if a broken actor cannot return after the gate.
 owner.join();
 need(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE,"fixture-handle-count-after");
 const bool debt=quarantines(selection.fault);
 const bool factoryFailure=selection.fault==FixtureFault::FactoryMissing||selection.fault==FixtureFault::FactoryNull;
 const bool queryRejected=selection.fault==FixtureFault::GetSteamIdThrow||selection.fault==FixtureFault::CurrentAfterReply;
 check("owner-exceptions-contained",result.ownerError.empty()&&result.ownerReturned&&result.destructorReturned);
 check("ready-result-correct",result.readyReturned==!factoryFailure);
 check("query-result-correct",result.queryReturned==(!factoryFailure&&!selection.readyOnly&&!queryRejected));
 check("one-module-load",state->loads==1);
 check("controlled-loader-and-sdk-order",!state->portViolation&&!state->prematureUnload);
 check("single-scope-lifetime",state->scopeCreated==1);
 const J cleanup=result.summary.value("normalQueryCleanup",J::object());
 check("summary-is-present",result.summaryAvailable&&cleanup.is_object()&&cleanup.contains("resourcesReturned"));
 check("cleanup-completed-is-separate",cleanup.value("cleanupCompleted",false));
 check("summary-resources-returned-correct",cleanup.contains("resourcesReturned")&&cleanup.value("resourcesReturned",false)==!debt);
 check("summary-retention-correct",cleanup.contains("moduleRetained")&&cleanup.value("moduleRetained",false)==debt&&
  cleanup.contains("sdkQuarantined")&&cleanup.value("sdkQuarantined",false)==debt);
 check("summary-has-no-numeric-sdk-handles",!numericHandleLeak(result.summary));
 if(!selection.readyOnly){
  check("explicit-finish-contract",result.finishAvailable&&(debt?result.finishError=="normal-query-cleanup-not-confirmed":result.finishReturned&&result.finishError.empty()));
 }
 if(debt){
  check("unknown-resource-not-unloaded",state->moduleRefs==1&&state->freeSuccess==0&&state->moduleLoaded());
  check("quarantine-keeps-read-scope",!result.scope.expired()&&state->scopeDestroyed==0);
  check("quarantine-reason-correct",cleanup.value("reason",J(nullptr))==J(expectedCleanupReason(selection.fault)));
  check("second-actor-rejected",result.secondRejected);
  check("second-actor-has-zero-additional-sdk-or-load",result.secondHadNoCalls);
  check("only-retained-scope-handles-remain",handlesAfter==handlesBefore+2);
 }else{
  check("loader-reference-returned",state->freeCalls==1&&state->freeSuccess==1&&state->moduleRefs==0&&!state->moduleLoaded());
  check("read-scope-actually-returned",result.scope.expired()&&state->scopeDestroyed==1);
  check("local-handles-returned",handlesAfter==handlesBefore);
  check("owned-sdk-resources-returned",state->userOwned==0&&state->pipeOwned==0&&state->clientOwned==0);
 }
 if(factoryFailure){
  check("factory-failure-is-load-after-failure",state->lookups==1&&state->creates==0&&state->connects==0&&state->shutdownCalls==0);
  check("factory-error-preserved",result.queryError==(selection.fault==FixtureFault::FactoryMissing?"normal-query-factory":"normal-query-client"));
 }else if(selection.fault==FixtureFault::GetSteamIdThrow){
  check("unknown-sdk-call-not-released-or-retried",state->steamIds==1&&state->releaseUserCalls==0&&state->releasePipeCalls==0&&state->shutdownCalls==0&&state->freeCalls==0);
 }else{
  check("release-user-once",state->releaseUserCalls==1);
  if(selection.fault==FixtureFault::ReleaseUserThrow)
   check("failed-user-release-not-continued",state->releaseUserSuccess==0&&state->releasePipeCalls==0&&state->shutdownCalls==0&&state->freeCalls==0);
  else{
   check("release-user-success-before-pipe",state->releaseUserSuccess==1&&state->releasePipeCalls==1);
   if(selection.fault==FixtureFault::PipeFalse||selection.fault==FixtureFault::PipeThrow)
    check("failed-pipe-release-not-continued",state->releasePipeSuccess==0&&state->shutdownCalls==0&&state->freeCalls==0);
   else{
    check("pipe-release-success-before-shutdown",state->releasePipeSuccess==1&&state->shutdownCalls==1);
    check("shutdown-result-kept",state->shutdownSuccess==(selection.fault==FixtureFault::ShutdownFalse?0:1));
    if(selection.fault==FixtureFault::ShutdownFalse)check("failed-shutdown-never-unloads",state->freeCalls==0);
    if(selection.fault==FixtureFault::FreeLibraryFalse)check("failed-free-never-retried",state->freeCalls==1);
   }
  }
 }
 if(selection.fault==FixtureFault::CurrentAfterReply)
  check("business-revocation-does-not-mint-sdk-debt",result.queryError=="normal-query-current-revoked"&&!debt&&state->steamIds==1);
 const auto failures=std::count_if(checks.begin(),checks.end(),[](const J& row){return !row.at("pass").get<bool>();});
 J trace;{std::lock_guard<std::mutex> lock(state->mutex);trace={{"steps",state->steps},{"stepThreadIds",state->stepThreads}};}
 J row={{"name",selection.name},{"pass",failures==0},{"checks",checks},{"cleanupSummary",result.summary},
  {"queryError",result.queryError},{"finishError",result.finishError},{"ownerError",result.ownerError},
  {"secondConstructionError",result.secondError},{"counters",state->counters()},{"trace",trace},
  {"blocked",blocked},{"processHandlesBefore",handlesBefore},{"processHandlesAfter",handlesAfter},
  {"originalModuleLeakObserved",moduleControl&&state->moduleRefs==1&&state->freeSuccess==0&&state->moduleLoaded()}};
 std::cout<<J({{"success",failures==0},{"failures",failures},{"cases",J::array({row})},{"executedThisRun",1},
  {"default14Executed",false},{"actor",moduleControl?"module-before":"candidate"},
  {"route",route==RecoveryRoute::LegacyDynamic?"dynamic":"semi-static"},{"deadlineEpochMs",fixtureDeadline},
  {"actualLocalLoader",true},{"sdkAndQualificationArePorts",true},{"readScopeIsLifetimePort",true},
  {"realLocalThreadsAndEvents",true},{"sdkAbiOrNativeQualificationVerified",false},
  {"steamGameDeviceUacActions",false},{"wholeGoalComplete",false}}).dump()<<std::endl;
 fixture=nullptr;return failures==0?0:2;
}

int selectionRejected(const char* reason){
 std::cout<<J({{"success",false},{"selectionRejected",true},{"reason",reason},
  {"executedThisRun",0},{"cases",J::array()},{"default14Executed",false}}).dump()<<std::endl;return 64;
}
int wmain(int argc,wchar_t** argv){
 if(argc==1){fixtureDeadline=fixtureNowMs()+30000;return original14();} // Original default only.
 std::wstring selected,dll,routeText=L"semi-static",actorText=L"candidate",deadlineText;
 bool seenCase=false,seenDll=false,seenRoute=false,seenActor=false,seenDeadline=false;
 for(int i=1;i<argc;i+=2){
  if(i+1>=argc)return selectionRejected("missing-option-value");
  const std::wstring key=argv[i],value=argv[i+1];
  if(value.empty())return selectionRejected("empty-option-value");
  if(key==L"--case"&&!seenCase){selected=value;seenCase=true;}
  else if(key==L"--dll"&&!seenDll){dll=value;seenDll=true;}
  else if(key==L"--route"&&!seenRoute){routeText=value;seenRoute=true;}
  else if(key==L"--actor"&&!seenActor){actorText=value;seenActor=true;}
  else if(key==L"--deadline"&&!seenDeadline){deadlineText=value;seenDeadline=true;}
  else return selectionRejected("unknown-or-duplicate-option");
 }
 if(!seenCase||!seenDll||!seenDeadline)return selectionRejected("explicit-case-dll-and-parent-deadline-required");
 const auto found=std::find_if(lifetimeCases.begin(),lifetimeCases.end(),[&](const auto& value){
  return selected==std::wstring(value.name,value.name+std::strlen(value.name));
 });
 if(found==lifetimeCases.end())return selectionRejected("unknown-case");
 if(routeText!=L"semi-static"&&routeText!=L"dynamic")return selectionRejected("unknown-route");
 if(actorText!=L"candidate"&&actorText!=L"module-before")return selectionRejected("unknown-actor");
 if(actorText==L"module-before"&&found->fault!=FixtureFault::None&&found->fault!=FixtureFault::FactoryMissing&&found->fault!=FixtureFault::FactoryNull)
  return selectionRejected("old-sdk-exception-controls-forbidden");
 if(actorText==L"module-before"&&found->block!=FixtureBlock::None)return selectionRejected("old-blocked-controls-not-selected");
 U64 deadline=0;
 for(const auto c:deadlineText){
  if(c<L'0'||c>L'9')return selectionRejected("invalid-parent-deadline");
  const auto digit=static_cast<U64>(c-L'0');
  if(deadline>((std::numeric_limits<U64>::max)()-digit)/10)return selectionRejected("invalid-parent-deadline");
  deadline=deadline*10+digit;
 }
 if(deadline<=fixtureNowMs())return selectionRejected("parent-deadline-expired");
 const std::filesystem::path dllFile(dll);
 if(!dllFile.is_absolute()||_wcsicmp(dllFile.filename().c_str(),L"query_actor_fixture.dll")!=0)
  return selectionRejected("owned-absolute-fixture-dll-required");
 fixtureDeadline=deadline;
 const auto route=routeText==L"dynamic"?RecoveryRoute::LegacyDynamic:RecoveryRoute::SemiStatic;
 try{
  if(actorText==L"module-before")return lifetime<original::QueryActor>(*found,dll,route,true);
  return lifetime<after::QueryActor>(*found,dll,route,false);
 }catch(const std::exception& error){
  std::cout<<J({{"success",false},{"error",error.what()},{"executedThisRun",1},
   {"cases",J::array({J{{"name",found->name},{"pass",false}}})},{"default14Executed",false}}).dump()<<std::endl;return 3;
 }
}

