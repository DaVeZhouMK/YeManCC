// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Actual shared observer / Decky admission worker with inert readers and rejected
// preflight, plus the production telemetry allowlist. No real Steam or loader.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_ui_mode_observer.h"
#include "decky_sidebar_runtime.h"
#include "decky_sidebar_diagnostics.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
using namespace ymcc;
static unsigned passed=0;
static void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);++passed;std::cout<<"PASS "<<name<<'\n';}
template<class F>bool until(F f,DWORD ms=800){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
static steamui::ModeJson mode(const steamsession::Session& s,const char* name){return {{"known",true},{"running",true},{"mode",name},{"modeEpoch","same-renderer"},{"pid",s.pid},{"birth",std::to_string(s.created)}};}
int main(){try{
 const steamsession::Session session{L"X:\\NeverRealSteam",123,0,100};
 steamui::ModeObserver observer;deckysidebar::Runtime decky;
 std::atomic<unsigned> reads{0},changed{0},stable{0},preflight{0};std::atomic<bool> desktop{false};
 steamui::ModeObserverConfig c;c.debounceMs=15;c.minimumGapMs=180;c.failureGapMs=300;
 c.read=[&](const auto& s){++reads;return mode(s,desktop?"desktop":"bigpicture");};
 c.changed=[&](const auto&,const auto&){++changed;};
#ifndef YMCC_READINESS_BASELINE
 c.stableBigPicture=[&](const auto& s){++stable;if(s==session)decky.requestLaunchAdmissionRefresh();};
#endif
 check(observer.initialize(c),"Shared observer initialized with inert verified reader");
 observer.setDemand(steamui::ModeConsumer::Decky,true);observer.observeSteam(session);
 check(until([&]{return changed==1;}),"Initial genuine Big Picture change is delivered once");
 check(reads==1&&stable==0,"Mode edge does not also generate an unchanged-mode callback");
 check(observer.requestRefresh(session),"A new window hint invalidates the cached mode");
 check(observer.snapshot(session).value("mode",std::string{})=="unknown","Decky cannot consume the invalidated confirmation");
 deckysidebar::Config d;d.executable=L"NeverLaunch.exe";
 d.preflight=[&]{++preflight;return std::string("inert-preflight-stop");};
 d.launchAdmission=[&]{const auto state=observer.snapshot(session);return deckysidebar::LaunchAdmission{state.value("mode",std::string{})=="bigpicture","steam-ui-state-pending",true};};
 decky.initialize(d);check(true,"Real Decky worker initialized; all launch attempts stop at preflight");
 decky.observeSteam(1,true);decky.setEnabled(true);
 check(until([&]{return decky.snapshot().phase=="waiting-manual-steam";},100),"Decky is waiting while the shared mode is pending");
 const auto began=GetTickCount64();
 const bool prompt=until([&]{return preflight==1;},650);
 std::cout<<"UNCHANGED_MODE_ADMISSION elapsedMs="<<(GetTickCount64()-began)<<" reads="<<reads<<" stable="<<stable<<'\n';
 check(prompt,"Unchanged verified Big Picture completion wakes waiting Decky before its 1s/30s retry");
 check(changed==1&&stable==1&&reads==2,"One original read and one stable completion suffice; no duplicate reader");
 check(until([&]{return decky.snapshot().phase=="unavailable";}),"Original resource preflight remains authoritative");
 check(decky.snapshot().pid==0,"No real loader process was created");
 check(!decky.requestLaunchAdmissionRefresh(),"A completed/nonwaiting admission rejects extra wakes");
 observer.requestRefresh(session);check(until([&]{return stable==2;}),"A later genuine read may deliver another stable result");
 Sleep(60);check(preflight==1,"Nonwaiting Decky cannot restart from unchanged-mode notifications");
 desktop=true;observer.requestRefresh(session);check(until([&]{return observer.snapshot(session).value("mode",std::string{})=="desktop";}),"Desktop mode is observed normally");
 const auto old=stable.load();observer.requestRefresh(session);Sleep(260);check(stable==old,"Unchanged desktop mode cannot authorize Decky startup");
 auto stale=session;++stale.created;check(!observer.requestRefresh(stale),"A mismatched process birth cannot request current-session observation");
 observer.setDemand(steamui::ModeConsumer::Decky,false);const auto atStop=reads.load();Sleep(240);check(reads==atStop,"Disabled demand leaves no recurring UI reader");decky.shutdown();observer.shutdown();
#ifndef YMCC_READINESS_BASELINE
 const auto input=deckydiag::Json::parse(R"({"event":"snapshot-read-finished","readId":4,"demandEpoch":2,"elapsedMs":500,"powerMs":2,"monitorMs":490,"ok":true,"stage":"settled","discarded":false,"token":"SECRET","game":"PRIVATE"})");
 const auto clean=deckydiag::frontendRecord(input),record=deckydiag::record("snapshot-read-finished",clean);
 check(clean.value("elapsedMs",0)==500&&record.value("monitorMs",0)==490,"Numeric stage timings survive both native allowlists");
 check(!clean.contains("token")&&!record.contains("game"),"Paths, tokens and game content never enter the telemetry");
 check(deckydiag::frontendRecord({{"event","arbitrary-log"}}).is_null(),"Unknown diagnostic events are rejected");
 check(deckydiag::frontendRecord(deckydiag::Json::parse(R"({"event":"snapshot-read-start","readId":"42","elapsedMs":-1,"stage":"C:/secret"})")).size()==1,"Wrong scalar types and arbitrary text are omitted");
 check(deckydiag::frontendRecord(deckydiag::Json::parse(R"({"event":"snapshot-read-finished","elapsedMs":999999999})")).value("elapsedMs",0)==3600000,"Telemetry timings are capped");
 check(deckydiag::record("snapshot-demand",{{"subscribe",false},{"demandEpoch",3}}).contains("subscribe"),"Pause demand is distinguishable from an active snapshot request");
 check(deckydiag::record("mirror-snapshot-request",{{"subscribe",true}}).value("subscribe",false),"Native request logs preserve the true subscription direction");
 const std::string id(64,'a');check(deckydiag::validBuildId(id),"Fixed-size lower-case hex build fingerprint is accepted");
 check(!deckydiag::validBuildId("path/secret")&&!deckydiag::validBuildId(std::string(65,'a'))&&!deckydiag::validBuildId(std::string(64,'G')),"Arbitrary or overlong build labels are rejected");
 check(deckydiag::record("plugin-build",{{"buildId",id},{"token","SECRET"}}).size()==2,"Build receipt includes only its fingerprint and event");
 check(!deckydiag::record("snapshot-read-finished",{{"buildId",id}}).contains("buildId"),"Build identity has its own dedicated event scope");
 const auto sessionDiagnostic=deckydiag::record("steam-session-check",{{"stage","binding-identity-incomplete"},{"steamPid",123u},{"steamBirth","456"},{"pidPresent",true},{"birthPresent",true},{"accountPresent",false},{"rootPresent",true},{"error",0u},{"account",12345u},{"root","C:/PRIVATE"},{"token","SECRET"}});
 check(sessionDiagnostic.value("stage",std::string{})=="binding-identity-incomplete"&&sessionDiagnostic.value("steamPid",0u)==123&&sessionDiagnostic.contains("accountPresent")&&!sessionDiagnostic["accountPresent"].get<bool>(),"Session failure reports its missing condition with the observed process lease");
 check(!sessionDiagnostic.contains("account")&&!sessionDiagnostic.contains("root")&&!sessionDiagnostic.contains("token"),"Session diagnostic never exposes account values, paths or tokens");
 const auto invalidSessionDiagnostic=deckydiag::record("steam-session-check",{{"stage","C:/PRIVATE"},{"steamPid","123"},{"steamBirth","birth/secret"},{"pidPresent",1},{"error",-1},{"enabled",true}});
 check(invalidSessionDiagnostic.size()==1,"Session diagnostic rejects arbitrary stages, wrong types and generic payload fields");
 check(!deckydiag::record("runtime",{{"accountPresent",true}}).contains("accountPresent"),"Session presence flags cannot widen unrelated events");
#endif
 std::cout<<"DECKY_READINESS_DIAGNOSTICS_OK passed="<<passed<<" realSteam=0 realLoader=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
