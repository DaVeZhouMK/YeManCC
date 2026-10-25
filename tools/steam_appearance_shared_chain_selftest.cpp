// Real shared observer + real CSS guard workers with mock reader/backend only.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_cssloader_guard.h"
#include "steam_ui_mode_observer.h"
#include <atomic>
#include <fstream>
#include <iostream>
using namespace ymcc;
static std::vector<std::string> cases;
static void check(bool value,const char* name){if(!value)throw std::runtime_error(name);cases.push_back(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool until(F f,DWORD ms=2000){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
static HANDLE duplicate(HANDLE source){HANDLE out=nullptr;if(!DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&out,SYNCHRONIZE,FALSE,0))throw std::runtime_error("duplicate fixture event");return out;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("isolated output required");const auto out=std::filesystem::path(argv[1]);std::filesystem::create_directories(out);
 const auto link=out/L"link.txt";auto enable=[&]{std::ofstream f(link);f<<"X:\\inert-css\\CssLoader-Standalone-Headless.exe";};enable();
 const steamsession::Session session{L"X:\\inert-steam",123,0,100};
 deckylaunch::Handle completion;completion.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 std::atomic<unsigned> reads{0},edges{0},uses{0},renews{0},cssReads{0},entry{1};std::atomic<bool> big{false},malformed{false},throwRead{false};
 steamui::ModeObserver shared;steamcss::Runtime css;
 steamui::ModeObserverConfig mode;mode.debounceMs=4;mode.minimumGapMs=25;mode.failureGapMs=100;
 mode.read=[&](const auto& s){++reads;if(throwRead)throw std::runtime_error("fixture transport unavailable");steamui::ModeJson result{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},
  {"mode",big?"bigpicture":"desktop"},{"modeEpoch","renderer:"+std::to_string(entry.load())}};if(malformed)result["pid"]=uint64_t{0x100000000ULL}+s.pid;return result;};
 mode.changed=[&](const auto& who,const auto& value){++edges;css.observeMode(who,{value.value("mode",std::string{})=="bigpicture"?steamcss::UiMode::BigPicture:steamcss::UiMode::Desktop,value.value("modeEpoch",std::string{})});};
 check(shared.initialize(mode),"shared mode worker initialized");
 steamcss::Config guard;guard.linkFile=link.wstring();guard.delays={5000,5000,5000};guard.modeDelays={3,15,100};
 guard.readMode=[&](const auto& s)->steamcss::ModeSample{++cssReads;const auto v=shared.snapshot(s);const auto m=v.value("mode",std::string{});return {m=="bigpicture"?steamcss::UiMode::BigPicture:m=="desktop"?steamcss::UiMode::Desktop:steamcss::UiMode::Unknown,v.value("modeEpoch",std::string{})};};
 guard.ensure=[](const auto&,const auto&,const auto&){return steamcss::Result{nullptr,0,"missing",{}};};
 guard.confirmIdle=[&](const auto&,bool start,bool renew){if(!start)throw std::runtime_error("expected missing mock backend");++uses;if(renew)++renews;return duplicate(completion.value);};
 guard.isIdle=[](const auto&){return true;};guard.changed=[&](const auto& s){shared.setDemand(steamui::ModeConsumer::Css,s.linked);};
 check(css.initialize(guard),"CSS worker initialized with the real shared-demand callback");shared.observeSteam(session);css.observeSteam(session);
 check(until([&]{return shared.snapshot(session).value("mode",std::string{})=="desktop"&&edges==1;}),"desktop baseline crosses the shared observer and CSS consumer");
 check(uses==0,"desktop detection cannot create a CSS mode-entry cycle");
 big=true;for(unsigned i=0;i<1000;++i)shared.requestRefresh(session);
 check(until([&]{return uses==1;}),"external bigpicture entry reaches CSS once through production workers");
 for(unsigned i=0;i<1000;++i)shared.requestRefresh(session);Sleep(120);
 check(uses==1&&renews==1,"same epoch event storms do not duplicate the CSS cycle");
 SetEvent(completion.value);check(until([&]{return css.snapshot().phase=="idle";}),"CSS completion parks the guard");
 // A rapid exit/re-entry between native reads is represented by a new renderer epoch.
 ResetEvent(completion.value);++entry;shared.requestRefresh(session);
 check(until([&]{return uses==2;}),"same PID and same bigpicture mode with a new epoch reapplies CSS once");
 SetEvent(completion.value);check(until([&]{return css.snapshot().phase=="idle";}),"second mode cycle also parks");
 std::filesystem::remove(link);check(until([&]{return css.snapshot().phase=="disabled";}),"unlink removes CSS demand through the directory event");
 Sleep(50);const auto disabledReads=reads.load();for(unsigned i=0;i<1000;++i)shared.requestRefresh(session);Sleep(80);
 check(reads==disabledReads&&uses==2,"no consumers means no CDP reader or CSS use after disable");
 // Decky can retain the shared reader independently, but cannot enable CSS.
 shared.setDemand(steamui::ModeConsumer::Decky,true);++entry;shared.requestRefresh(session);Sleep(100);
 check(reads>disabledReads&&uses==2,"Decky demand keeps detection alive without re-enabling CSS");
 malformed=true;++entry;shared.requestRefresh(session);
 check(until([&]{return shared.snapshot(session).value("mode",std::string{})=="unknown";}),"malformed refresh invalidates the previous verified mode before enable");
 enable();check(until([&]{return css.snapshot().linked;}),"explicit re-enable restores CSS consumer demand");Sleep(140);
 check(uses==2&&shared.snapshot(session).value("mode",std::string{})=="unknown","malformed common-interface identity never reaches CSS admission");
 malformed=false;throwRead=true;shared.requestRefresh(session);Sleep(200);
 check(uses==2,"transport exceptions cannot manufacture a bigpicture edge or launch CSS");
 throwRead=false;ResetEvent(completion.value);shared.requestRefresh(session);
 check(until([&]{return uses==3;}),"fresh valid event recovers enabled CSS after failures without restarting Steam");
 // Pending old read/session cannot survive Steam exit.
 shared.observeSteam({});css.observeSteam({});check(until([&]{return !css.snapshot().steamRunning;}),"Steam exit clears CSS session state");
 const auto stoppedReads=reads.load();for(unsigned i=0;i<1000;++i)shared.requestRefresh(session);Sleep(100);
 check(reads==stoppedReads&&uses==3,"late old-session hints cannot restart the reader or CSS");
 css.shutdown();shared.shutdown();
 nlohmann::json record{{"ok",true},{"passed",cases.size()},{"cases",cases},{"realSteamOperations",0},{"realCssOperations",0},{"cssUseCallbacks",uses.load()},{"modeReads",reads.load()}};
 std::ofstream f(out/L"chain.json");f<<record.dump(2);std::cout<<record.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
