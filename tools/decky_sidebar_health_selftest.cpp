// Offline consumers of the exact production health policy, never launches any product.
#include "../native/decky_sidebar_health.h"
#include <iostream>
#include <stdexcept>
using namespace ymcc::deckyhealth;
int main(){unsigned checks=0;auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;};try{
 const std::string run(32,'a');
 Json base={{"method","Runtime.exceptionThrown"},{"params",{{"exceptionDetails",{{"executionContextId",101},{"exception",{{"subtype","error"},{"className","TypeError"},{"description","TypeError: Cannot read properties of undefined (reading 'createElement')"}}},{"stackTrace",{{"callFrames",Json::array({{{"url","http://127.0.0.1:1337/frontend/index.js"}}})}}}}}}}};
 check(exception(base,101)=="decky-frontend-incompatible","real uncaught core entry error is candidate, further live readback required");
 check(exception(base,0).empty()&&exception(base,102).empty(),"historical and foreign context rejected");
 for(const std::string url:{"http://127.0.0.1:1337/plugins/p/index.js","decky://decky/plugin/p/index.js","http://localhost:1337/frontend/index.jsEvil","http://127.0.0.1:1337/frontend/catalog.js","http://localhost:13370/frontend/index.js","http://evil.invalid/frontend/index.js","http://localhost:1337/frontend/../index.js"}){auto p=base;p["params"]["exceptionDetails"]["stackTrace"]["callFrames"][0]["url"]=url;check(exception(p,101).empty(),"nonboot/plugin/foreign source is never fatal");}
 for(const std::string message:{"TypeError: Failed to fetch","Error: HTTP 403","Error: Steam updated","Error: connection closed","TypeError: Cannot read properties of undefined\nat decky://decky/plugin/p/index.js"}){auto p=base;p["params"]["exceptionDetails"]["exception"]["description"]=message;check(exception(p,101).empty(),"network and plugin noise ignored");}
 auto p=base;p["params"]["exceptionDetails"]["exception"]["description"]=std::string(8193,'x');check(exception(p,101).empty(),"oversize description bounded");p=base;p["params"]["exceptionDetails"]["stackTrace"]["callFrames"].push_back({{"url","http://localhost:1337/plugins/p/index.js"}});check(exception(p,101).empty(),"plugin caller excludes core frame");
 Json signal={{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",101},{"payload",Json{{"runId",run},{"code","decky-render-failed"}}.dump()}}}};
 check(boundarySignal(signal,101,run),"scoped health marker admitted for live readback");check(!boundarySignal(signal,102,run)&&!boundarySignal(signal,101,std::string(32,'b')),"foreign context/session marker rejected");signal["params"]["payload"]="{}";check(!boundarySignal(signal,101,run),"incomplete marker rejected");signal["params"]["payload"]=std::string(161,'x');check(!boundarySignal(signal,101,run),"marker byte bound");
 Json state={{"enabled",true},{"phase","loader-started"},{"revision",7}},event={{"reason","decky-render-failed"},{"runId",run}};
 check(matches(event,state,run),"live queued fault accepted");check(!matches(event,state,""),"stopped broker cannot accept stale fault");check(!matches(event,state,std::string(32,'b')),"new broker rejects old session");state["enabled"]=false;check(!matches(event,state,run),"already off rejects queued fault");state["enabled"]=true;state["phase"]="waiting-steam";check(!matches(event,state,run),"navigation/session stop rejects queued fault");
 event={{"reason","loader-crash-loop"},{"revision",7}};state["phase"]="failed";state["reason"]="loader-crash-loop";check(matches(event,state,""),"current crash breaker can close after broker exits");event["revision"]=6;check(!matches(event,state,""),"old crash event cannot close new intent");event["reason"]="HTTP-403";check(!matches(event,state,run)&&!terminal("HTTP-403"),"only explicit terminal codes admitted");
 Json retained={{"enabled",true},{"phase","frontend-attached"},{"reason","retained-frontend-connected"},{"pid",0},{"steamKnown",true},{"steamRunning",true}},fault={{"reason","decky-render-failed"},{"runId",run}};
 check(matches(fault,retained,run),"same retained frontend fatal page is admitted without Loader ownership");
 check(!matches(fault,retained,std::string(32,'b'))&&!matches(fault,retained,""),"retained old or stopped broker cannot close a new connection");
 for(const char* field:{"enabled","steamKnown","steamRunning"}){auto invalid=retained;invalid[field]=false;check(!matches(fault,invalid,run),"retained OFF or unconfirmed Steam cannot accept fatal");}
 for(const std::string phase:{"reconnecting","waiting-steam","disabled","failed"}){auto invalid=retained;invalid["phase"]=phase;check(!matches(fault,invalid,run),"unaccepted phase cannot claim retained fatal authority");}
 auto invalidRetained=retained;invalidRetained["reason"]="retained-frontend-rebind";check(!matches(fault,invalidRetained,run),"pending retained rebind is not an established retained connection");
 invalidRetained=retained;invalidRetained["pid"]=1;check(!matches(fault,invalidRetained,run),"retained phase cannot claim a Loader PID");
 invalidRetained=retained;invalidRetained.erase("pid");check(!matches(fault,invalidRetained,run),"retained state must include the observed zero Loader PID");
 invalidRetained=retained;invalidRetained["pid"]="0";check(!matches(fault,invalidRetained,run),"malformed retained PID rejected");
 auto noise=fault;noise["reason"]="HTTP-403";check(!matches(noise,retained,run),"retained connection still ignores ordinary network errors");
 PendingStop queued;Json terminalEvidence={{"bootstrapStage","terminal"},{"reason","decky-render-failed"},{"runId",run}};
 check(!queued.active(11)&&!queued.matches(fault,11,run),"unobserved terminal cannot authorize owner OFF");
 check(queued.capture(terminalEvidence,11,run)&&queued.active(11)&&queued.matches(fault,11,run),"exact authenticated terminal retains the current intent");
 check(!queued.matches(fault,12,run)&&!queued.active(12),"new intent rejects predecessor terminal even before broker replacement");
 check(!queued.matches(fault,11,std::string(32,'b'))&&!queued.matches(fault,11,""),"pending stop requires still-current live broker run");
 auto wrongFault=fault;wrongFault["reason"]="decky-frontend-incompatible";check(!queued.matches(wrongFault,11,run),"pending reason must match the observed reason");
 for(const std::string field:{"bootstrapStage","reason","runId"}){auto invalid=terminalEvidence;invalid[field]="other";check(!queued.capture(invalid,11,run)&&queued.matches(fault,11,run),"bad packet cannot replace an admitted terminal");}
 for(const std::string reason:{"HTTP-403","loader-crash-loop","mirror-bootstrap-ws-closed"}){auto invalid=terminalEvidence;invalid["reason"]=reason;PendingStop candidate;check(!candidate.capture(invalid,11,run),"ordinary outage or crash breaker cannot claim watch-terminal authority");}
 PendingStop invalidPending;check(!invalidPending.capture(terminalEvidence,0,run)&&!invalidPending.capture(terminalEvidence,11,"bad-run"),"pending intent and run identity are required");
 check(!invalidPending.capture(Json(),11,run)&&!queued.matches(Json(),11,run),"malformed pending terminal fails closed");
 queued.clear();check(!queued.active(11)&&!queued.matches(fault,11,run),"OFF clears terminal authority");
 auto nextEvidence=terminalEvidence;nextEvidence["runId"]=std::string(32,'b');auto nextFault=fault;nextFault["runId"]=std::string(32,'b');check(queued.capture(nextEvidence,12,std::string(32,'b'))&&queued.matches(nextFault,12,std::string(32,'b'))&&!queued.matches(fault,11,run),"new authenticated binding can hold its own independent terminal");
 check(exception(Json(),101).empty()&&!boundarySignal(Json(),101,run)&&!matches(Json(),Json(),run),"malformed input fails closed");
 std::cout<<"DECKY_HEALTH_POLICY_OK checks="<<checks<<" realSteamActions=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
