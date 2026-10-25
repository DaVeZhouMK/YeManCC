#pragma once
// Only the actor's Steam SDK worker is a port. Its four public query methods and
// waitQueryReturn are extracted verbatim from the production source.
class QueryActor {
  H command{makeHandle("query-command")},doneEvent{makeHandle("query-done")};
  std::atomic<bool> completedFlag{false};std::atomic<U64> result{0};
  std::mutex errorMutex;std::string error;U64 expected=0;
 public:
  explicit QueryActor(U64 account):expected(account){
    command.h->signal=[this]{
      ++f.queryFires;f.note("query:fire");if(!f.queryDispatch)return FALSE;
      result=f.queryResult;error=f.queryError;completedFlag=f.queryComplete;
      f.note("query:result");if(f.afterQuery)f.afterQuery();return TRUE;
    };
  }
#include "query-methods.inc"
};
// Opaque full-capture port. This is a scripted success/failure boundary, not an
// emulation or a claim to test the debugger/capture implementation. Both versions
// call this same port. Python verifies full old/new capture bodies are identical.
J captureGetterContext(Scope& scope,const Snapshot&,U64 account,QueryActor& query){
  ++f.captures;f.note("capture:entry");
  f.witnessEmptyAtCapture=f.witnessEmptyAtCapture&&gSourceContextInit.steamPid==0&&gSourceContextInit.steamBirth==0&&gSourceContextInit.steamPath.empty();
  requireCurrent();
  for(const auto* stage:{"open","attach","policy","census","getter","detach"}){
    f.note((std::string("capture:")+stage).c_str());
    if(f.captureFail==stage)throw std::runtime_error(std::string("fixture-capture-")+stage);
    if(std::string(stage)=="census"){need(scope.owned(),"fixture-capture-ownership");query.fire();}
  }
  query.waitComplete();requireCurrent();f.note("capture:complete");
  if(f.captureRevoke)f.live=false;if(f.afterCapture)f.afterCapture();
  need(account==77,"fixture-capture-account");
  return {{"tid",41},{"threadBirthFileTime","4294967351"},{"threadDescription","IPC:CSteamEngine"},
    {"expectedAccountMatched",true},{"originalInterfaceOwnerMatched",true}};
}
