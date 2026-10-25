// Executed against verbatim extracted production code, with ports.hpp only.
#include "production.inc"
J results=J::array();std::string group="selector";
void clearPorts(){
  need(f.handles.empty()&&f.strings.empty(),"fixture-resources-still-live");f=FixturePorts{};
  gCurrent=[]{++f.currentCalls;if(f.throwCurrent)throw std::runtime_error("fixture-current-exception");return f.live;};
}
void resetFixture(){clearPorts();gSourceContextInit={};}
void expect(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class Fn> void test(const char* name,Fn fn){
  J row={{"name",name},{"group",group},{"passed",false}};
  try{resetFixture();fn();expect(f.handles.empty()&&f.strings.empty(),"resource-leak");row["passed"]=true;}
  catch(const std::exception& e){row["error"]=e.what();}
  row["trace"]=f.events;row["portCounts"]={{"capture",f.captures},{"queryFire",f.queryFires},{"policy",f.policies},
    {"census",f.censuses},{"openThread",f.opened},{"lastErrorReads",f.lastErrorReads},{"prepare",f.prepares}};
  // Leak failures are preserved, but isolate subsequent cases without dereferencing bad handles.
  for(auto* p:f.strings)delete[] p;for(auto* h:f.handles)delete h;f.strings.clear();f.handles.clear();
  results.push_back(std::move(row));
}
Scope makeScope(){Scope s;for(unsigned i=0;i<4;++i){s.roles[i].pid=11+i;s.roles[i].born=22+i;s.roles[i].path=L"C:\\owned\\steam.exe";}return s;}
struct Outcome {J context,summary;std::string error;bool returned=false,prepared=false;};
Outcome invoke(bool old=false,bool prepare=false,const std::function<void(Scope&)>& alter={}){
  Outcome o;auto scope=makeScope();if(alter)alter(scope);Snapshot sample;QueryActor query(77);
  try{
    o.context=old?oldSourceGetterContext(scope,sample,77,query,o.summary):sourceGetterContext(scope,sample,77,query,o.summary);
    o.returned=true;f.note("selection:return");
    // Explicit fixture port, not the driver: tests prove ordering up to this boundary.
    if(prepare){requireCurrent();need(scope.owned(),"fixture-prepare-current");++f.prepares;f.note("Prepare");o.prepared=true;}
  }catch(const std::exception& e){o.error=e.what();f.note("selection:refused");}
  return o;
}
void prime(){auto o=invoke();expect(o.returned&&f.captures==1,"prime-did-not-full-capture");auto s=makeScope();expect(gSourceContextInit.matches(s.roles[0]),"prime-did-not-establish-witness");clearPorts();}
void expectCold(const Outcome& o,const char* reason){
  expect(o.returned&&f.captures==1,"expected-original-capture");
  expect(o.summary.at("sourceContextSelection").at("mode")=="full-capture","fallback-mode");
  expect(o.summary.at("sourceContextSelection").at("fallbackReason")==reason,"fallback-reason");
  expect(o.context.at("expectedAccountMatched")==true&&o.context.at("originalInterfaceOwnerMatched")==true,"capture-port-result-lost");
  expect(f.witnessEmptyAtCapture,"stale-or-premature-witness-at-capture");
}
void expectWarm(const Outcome& o){
  expect(o.returned&&f.captures==0&&f.queryFires==1&&f.policies==1,"warm-did-not-query-without-capture");
  const auto& d=o.summary.at("sourceContextSelection");
  expect(d.at("mode")=="same-worker-initialized-thread-candidate"&&d.at("preAttachPolicyVerified")==true,"warm-mode-or-policy");
  expect(d.at("candidateGrantsPublication")==false,"candidate-granted-publication");
  expect(o.context.at("expectedAccountMatched")==false&&o.context.at("originalInterfaceOwnerMatched")==false,"warm-reused-owner-or-account-authorization");
  expect(o.context.at("tid")==41&&o.context.at("threadBirthFileTime")=="4294967351","warm-candidate-identity");
  // Warm diagnostics must reflect the successfully executed pre-attach policy.
  expect(o.summary.at("firstAttachExitPolicyPreinitialized")==true,"warm-policy-summary-stale");
}
std::size_t position(const char* s){auto i=std::find(f.events.begin(),f.events.end(),J(s));expect(i!=f.events.end(),"trace-event-missing");return std::size_t(i-f.events.begin());}
void addThread(DWORD tid,const std::wstring& name=L"other",DWORD owner=11){ThreadSpec t;t.tid=tid;t.pid=owner;t.description=name;f.threads.push_back(t);f.rows.push_back({sizeof(THREADENTRY32),tid,owner});}
void rejectCandidate(const char* reason){
  J detail;auto c=findSourceThreadCandidate(11,detail);expect(!c,"candidate-accepted-invalid-census");
  expect(detail.at("fallbackReason")==reason,"candidate-rejection-reason");
}
void selectorCases(){
  group="old-new-differential";
  test("new-worker-cold-equals-old-direct-capture",[]{
    auto old=invoke(true,true);expect(old.prepared&&f.captures==1&&f.policies==0&&f.censuses==0,"old-baseline-not-direct-capture");
    auto context=old.context;resetFixture();auto now=invoke(false,true);
    expectCold(now,"same-worker-prior-capture-witness-unavailable");expect(now.context==context&&now.prepared,"cold-old-new-result-diff");
    expect(f.policies==0&&f.censuses==0,"cold-used-candidate-ports");
  });
  test("same-worker-completed-capture-old-captures-new-queries",[]{
    prime();auto old=invoke(true,true);expect(old.returned&&f.captures==1,"old-warm-path-skipped-capture");clearPorts();
    auto now=invoke(false,true);expectWarm(now);expect(now.prepared,"warm-not-before-prepare");
    expect(position("policy:false")<position("census:begin")&&position("census:end")<position("query:fire")&&position("query:result")<position("Prepare"),"warm-order");
  });
  group="selector";
  test("new-os-worker-after-parent-warm-still-cold",[]{
    prime();bool inherited=true,passed=false;std::string error;
    std::thread worker([&]{try{clearPorts();inherited=gSourceContextInit.matches(makeScope().roles[0]);auto o=invoke();passed=o.returned&&f.captures==1&&f.policies==0;}catch(const std::exception& e){error=e.what();}});worker.join();
    expect(!inherited&&passed&&error.empty(),"new-worker-inherited-witness-or-used-warm");
    expect(gSourceContextInit.matches(makeScope().roles[0]),"child-modified-parent-witness");
  });
  for(const auto* field:{"pid","birth","path"})test((std::string("changed-steam-")+field+"-uses-capture").c_str(),[=]{
    prime();auto o=invoke(false,true,[=](Scope& s){if(std::string(field)=="pid")++s.roles[0].pid;else if(std::string(field)=="birth")++s.roles[0].born;else s.roles[0].path=L"C:\\different\\steam.exe";});
    expectCold(o,"same-worker-prior-capture-witness-unavailable");expect(f.policies==0&&f.censuses==0&&o.prepared,"lifetime-change-used-warm");
  });
  for(const auto* field:{"pid","birth","path"})test((std::string("incomplete-witness-")+field).c_str(),[=]{
    prime();if(std::string(field)=="pid")gSourceContextInit.steamPid=0;else if(std::string(field)=="birth")gSourceContextInit.steamBirth=0;else gSourceContextInit.steamPath.clear();
    expectCold(invoke(),"same-worker-prior-capture-witness-unavailable");
  });
  test("policy-initialization-failure-uses-original-capture",[]{prime();f.policyOk=false;expectCold(invoke(false,true),"same-worker-debug-policy-verification-failed");expect(f.censuses==0,"enumerated-after-policy-failure");});
  const std::vector<std::pair<std::string,std::function<void()>>> fallback{
    {"zero",[]{f.threads[0].description=L"other";}},
    {"multiple",[]{addThread(42,L"IPC:CSteamEngine");}},
    {"duplicate-row",[]{f.rows.push_back(f.rows[0]);}},
    {"snapshot-unavailable",[]{f.snapshotOk=false;}},
    {"query-incomplete",[]{addThread(42);f.threads.back().open=false;}},
    {"enumeration-incomplete",[]{f.terminalError=ERROR_ACCESS_DENIED;}}
  };
  for(const auto& entry:fallback)test(("warm-"+entry.first+"-falls-back-before-prepare").c_str(),[=]{
    prime();entry.second();auto o=invoke(false,true);expect(o.prepared&&f.captures==1&&f.witnessEmptyAtCapture,"invalid-census-bypassed-capture");
    expect(position("capture:complete")<position("Prepare"),"fallback-after-prepare");
  });
  for(const auto* stage:{"open","attach","policy","census","getter","detach"})test((std::string("failed-full-capture-")+stage+"-never-sets-witness").c_str(),[=]{
    // Begin with a real warm witness, then force a cold fallback and fail it.
    prime();f.policyOk=false;f.captureFail=stage;auto o=invoke(false,true);
    expect(!o.returned&&!o.prepared&&f.captures==1,"failed-capture-authorized");
    expect(!gSourceContextInit.steamPid&&!gSourceContextInit.steamBirth&&gSourceContextInit.steamPath.empty(),"failed-capture-created-witness");
    expect(f.witnessEmptyAtCapture,"witness-not-cleared-before-capture");
  });
  test("capture-return-revokes-current-before-witness",[]{f.captureRevoke=true;auto o=invoke(false,true);expect(!o.returned&&!o.prepared&&o.error=="native-current-guard-revoked","capture-return-current-bypassed");expect(!gSourceContextInit.steamPid,"revoked-capture-established-witness");});
  const std::vector<std::pair<std::string,std::function<void()>>> guards{
    {"entry",[]{f.live=false;}},{"callback-throws",[]{f.throwCurrent=true;}},
    {"after-census",[]{f.afterCensus=[]{f.live=false;};}},
    {"after-empty-census",[]{f.threads[0].description=L"other";f.afterCensus=[]{f.live=false;};}},
    {"after-query",[]{f.afterQuery=[]{f.live=false;};}},
    {"ownership-after-census",[]{f.afterCensus=[]{f.exact=false;};}},
    {"ownership-after-query",[]{f.afterQuery=[]{f.exact=false;};}}
  };
  for(const auto& entry:guards)test(("warm-current-"+entry.first+"-terminal-no-fallback").c_str(),[=]{
    prime();entry.second();auto o=invoke(false,true);expect(!o.returned&&!o.prepared&&f.captures==0,"revoked-warm-guard-fell-through");
  });
  test("policy-failure-and-current-revocation-cannot-authorize-fallback",[]{prime();f.policyOk=false;f.live=false;auto o=invoke(false,true);expect(!o.returned&&!o.prepared,"revoked-policy-fallback-authorized");expect(f.captures==0,"entry-guard-bypassed");});
  for(const auto* failure:{"dispatch","timeout","error","wrong-account"})test((std::string("warm-normal-query-")+failure+"-terminal").c_str(),[=]{
    prime();if(std::string(failure)=="dispatch")f.queryDispatch=false;else if(std::string(failure)=="timeout")f.queryComplete=false;else if(std::string(failure)=="error")f.queryError="fixture-normal-query-failed";else f.queryResult=78;
    auto o=invoke(false,true);expect(!o.returned&&!o.prepared&&f.captures==0,"warm-query-failure-fell-back-or-authorized");expect(!o.error.empty(),"warm-query-error-lost");
  });
  for(const auto* failure:{"timeout","error","wrong-account"})test((std::string("cold-normal-query-")+failure+"-no-witness").c_str(),[=]{
    if(std::string(failure)=="timeout")f.queryComplete=false;else if(std::string(failure)=="error")f.queryError="fixture-normal-query-failed";else f.queryResult=78;
    auto o=invoke(false,true);expect(!o.returned&&!o.prepared&&!gSourceContextInit.steamPid,"failed-cold-query-established-witness");
  });
  const std::vector<std::pair<std::string,std::function<void()>>> changes{
    {"birth",[]{++f.threads[0].birth;}},{"owner",[]{++f.threads[0].pid;}},
    {"name",[]{f.threads[0].description=L"renamed";}},{"exit",[]{f.threads[0].wait=WAIT_OBJECT_0;}},
    {"times-unavailable",[]{f.threads[0].times=false;}},{"description-unavailable",[]{f.threads[0].hr=-1;}}
  };
  for(const auto& change:changes)for(const bool afterQuery:{false,true})test((std::string("warm-thread-")+change.first+(afterQuery?"-after-query":"-after-census")).c_str(),[=]{
    prime();if(afterQuery)f.afterQuery=change.second;else f.afterCensus=change.second;
    auto o=invoke(false,true);expectCold(o,afterQuery?"source-thread-changed-after-query":"source-thread-changed-after-census");
    expect(o.prepared&&position("capture:complete")<position("Prepare"),"changed-thread-reached-prepare-without-capture");
    expect(f.queryFires==(afterQuery?2:1),"fallback-normal-query-count");
  });
}
void censusCases(){
  group="actual-findSourceThreadCandidate";
  test("complete-unique-census-and-fresh-last-error",[]{
    addThread(42);J detail;{auto c=findSourceThreadCandidate(11,detail);expect(bool(c),"valid-unique-census-rejected");expect(c->tid==41&&c->birth==0x100000037ULL,"candidate-identity");
      expect(detail.at("queriedThreads")==2&&detail.at("matchingNames")==1,"census-counts");
      expect(f.threads[0].timeCalls==2&&f.threads[0].descriptionCalls==2,"missing-post-census-revalidation");
      expect(f.lastErrorReads==1&&f.lastError==ERROR_ACCESS_DENIED,"last-error-not-snapshotted-before-revalidation");}
  });
  test("foreign-census-owner-skipped-before-open",[]{f.rows.insert(f.rows.begin(),{sizeof(THREADENTRY32),999,888});J d;auto c=findSourceThreadCandidate(11,d);expect(bool(c)&&f.opened==1&&f.openedTids==std::vector<DWORD>{41},"foreign-row-opened-or-counted");});
  test("zero-target-threads",[]{f.rows[0].th32OwnerProcessID=999;rejectCandidate("source-thread-census-incomplete-or-not-unique");expect(!f.opened,"foreign-only-opened");});
  test("zero-name-matches",[]{f.threads[0].description=L"other";rejectCandidate("source-thread-census-incomplete-or-not-unique");});
  test("multiple-distinct-candidates",[]{addThread(42,L"IPC:CSteamEngine");rejectCandidate("source-thread-census-incomplete-or-not-unique");});
  test("duplicate-row-is-not-unique",[]{f.rows.push_back(f.rows[0]);rejectCandidate("source-thread-census-incomplete-or-not-unique");});
  for(const auto* kind:{"snapshot","first","empty"})test((std::string("unavailable-census-")+kind).c_str(),[=]{if(std::string(kind)=="snapshot")f.snapshotOk=false;else if(std::string(kind)=="first")f.firstOk=false;else f.rows.clear();rejectCandidate("source-thread-census-unavailable");});
  for(const auto* kind:{"open","foreign-owner","owner-query-zero","exited","wait-failed"})test((std::string("incomplete-thread-query-")+kind).c_str(),[=]{
    auto& t=f.threads[0];if(std::string(kind)=="open")t.open=false;else if(std::string(kind)=="foreign-owner")t.pid=12;else if(std::string(kind)=="owner-query-zero")t.pid=0;else t.wait=std::string(kind)=="exited"?WAIT_OBJECT_0:WAIT_FAILED;
    rejectCandidate("source-thread-query-incomplete");
  });
  test("thread-birth-query-fails",[]{f.threads[0].times=false;rejectCandidate("source-thread-birth-unavailable");});
  for(const auto* kind:{"hr-fails-with-allocation","null-description","zero-birth"})test(kind,[=]{
    auto& t=f.threads[0];if(std::string(kind)=="hr-fails-with-allocation")t.hr=-1;else if(std::string(kind)=="null-description")t.nullDescription=true;else t.birth=0;
    rejectCandidate("source-thread-description-unavailable");expect(f.strings.empty(),"failed-description-allocation-leaked");
  });
  test("partial-census-after-valid-candidate-is-rejected",[]{addThread(42);f.threads[1].open=false;rejectCandidate("source-thread-query-incomplete");});
  test("terminal-last-error-is-not-no-more-files",[]{f.terminalError=ERROR_ACCESS_DENIED;rejectCandidate("source-thread-census-incomplete-or-not-unique");});
  test("stale-last-error-before-normal-end-does-not-reject",[]{f.lastError=ERROR_ACCESS_DENIED;J d;auto c=findSourceThreadCandidate(11,d);expect(bool(c)&&f.lastErrorReads==1,"stale-error-used-instead-of-terminal-error");});
  test("terminal-success-code-zero-is-not-complete",[]{f.terminalError=0;rejectCandidate("source-thread-census-incomplete-or-not-unique");});
  test("4096-target-rows-complete-is-accepted",[]{for(DWORD i=1;i<4096;++i)addThread(41+i);J d;auto c=findSourceThreadCandidate(11,d);expect(bool(c)&&f.opened==4096&&d.at("queriedThreads")==4096,"bounded-complete-census-refused");});
  test("4097-target-rows-stops-before-extra-query",[]{for(DWORD i=1;i<4097;++i)addThread(41+i);rejectCandidate("source-thread-census-limit");expect(f.opened==4096,"census-limit-not-enforced");});
}
void currentCases(){
  group="actual-currentSourceThread";
  const std::vector<std::pair<std::string,std::function<void(SourceThreadCandidate&)>>> bad{
    {"zero-tid",[](auto& c){c.tid=0;}},{"zero-birth",[](auto& c){c.birth=0;}},
    {"null-handle",[](auto& c){c.handle=H{};}},{"invalid-handle",[](auto& c){c.handle=H(INVALID_HANDLE_VALUE);}},
    {"foreign-pid",[](auto&){f.threads[0].pid=12;}},{"owner-query-zero",[](auto&){f.threads[0].pid=0;}},
    {"exited",[](auto&){f.threads[0].wait=WAIT_OBJECT_0;}},{"wait-failed",[](auto&){f.threads[0].wait=WAIT_FAILED;}},
    {"birth-query-fails",[](auto&){f.threads[0].times=false;}},{"birth-low-changed",[](auto&){++f.threads[0].birth;}},
    {"birth-high-changed",[](auto&){f.threads[0].birth+=U64(1)<<32;}},
    {"description-hr-fails",[](auto&){f.threads[0].hr=-1;}},{"description-null",[](auto&){f.threads[0].nullDescription=true;}},
    {"description-case",[](auto&){f.threads[0].description=L"ipc:CSteamEngine";}},
    {"description-suffix",[](auto&){f.threads[0].description+=L" ";}},
    {"description-empty",[](auto&){f.threads[0].description.clear();}}
  };
  for(const auto& entry:bad)test(("current-rejects-"+entry.first).c_str(),[=]{SourceThreadCandidate c;c.tid=41;c.birth=0x100000037ULL;c.handle=H(OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,41));entry.second(c);expect(!currentSourceThread(c,11),"invalid-current-thread-accepted");expect(f.strings.empty(),"current-description-leak");});
  test("current-valid-full-birth-and-exact-description",[]{SourceThreadCandidate c;c.tid=41;c.birth=0x100000037ULL;c.handle=H(OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,41));expect(currentSourceThread(c,11),"valid-current-thread-rejected");expect(f.localFrees==1,"description-not-freed");});
}
int main(){
  selectorCases();censusCases();currentCases();
  group="verbatim-engine-checks";
#include "engine-checks.inc"
  unsigned passed=0;for(const auto& r:results)if(r.at("passed")==true)++passed;
  std::cout<<J{{"success",passed==results.size()},{"passed",passed},{"total",results.size()},{"cases",results}}.dump(2)<<"\n";
  return passed==results.size()?0:1;
}
