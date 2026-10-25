// Log regression: verified Big Picture must not wait for a second polling window.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_guard.h"
#include <json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
using namespace ymcc;
template<class F> static bool until(F f,DWORD ms=1000){auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("isolated output required");std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);auto link=out/L"link.txt";
 {std::ofstream f(link);f<<"X:\\fixture\\CssLoader-Standalone-Headless.exe";}
 const steamsession::Session session{L"X:\\fixture-steam",123,0,100};
 std::atomic<unsigned> reads{0},uses{0};std::atomic<bool> big{false};steamcss::Runtime guard;
 steamcss::Config cfg;cfg.linkFile=link.wstring();cfg.delays={60000,60000,60000};cfg.modeDelays={10,5000,30000};
 cfg.readMode=[&](const auto&){++reads;return big?steamcss::UiMode::BigPicture:steamcss::UiMode::Desktop;};
 cfg.ensure=[](const auto&,const auto&,const auto&){return steamcss::Result{nullptr,0,"missing",{}};};
 cfg.confirmIdle=[&](const auto&,bool,bool){++uses;return HANDLE{nullptr};};cfg.isIdle=[](const auto&){return false;};
 guard.initialize(cfg);guard.observeSteam(session);if(!until([&]{return reads==1;}))throw std::runtime_error("no desktop baseline");Sleep(30);big=true;
 const auto started=GetTickCount64();
#ifdef YMCC_BASELINE_METRICS
 guard.requestUiRefresh();
#else
 guard.observeMode(session,{steamcss::UiMode::BigPicture,"renderer:1"});
#endif
 const bool immediate=until([&]{return uses==1;},300);const auto elapsed=GetTickCount64()-started;
#ifndef YMCC_BASELINE_METRICS
 for(unsigned i=0;i<1000;++i)guard.observeMode(session,{steamcss::UiMode::BigPicture,"renderer:1"});Sleep(60);
 const bool noDup=uses==1&&reads==1;
 auto other=session;++other.created;guard.observeMode(other,{steamcss::UiMode::BigPicture,"renderer:2"});Sleep(40);
 const bool staleRejected=uses==1;
 std::filesystem::remove(link);until([&]{return guard.snapshot().phase=="disabled";});guard.observeMode(session,{steamcss::UiMode::BigPicture,"renderer:3"});Sleep(40);
 const bool disabled=uses==1;
#else
 const bool noDup=true,staleRejected=true,disabled=true;
#endif
 guard.shutdown();const bool ok=immediate&&noDup&&staleRejected&&disabled;
 nlohmann::json r{{"ok",ok},{"sharedConfirmedToCssMs",elapsed},{"immediateWithin300Ms",immediate},{"noDuplicateOrExtraModeRead",noDup},{"staleSessionRejected",staleRejected},{"disabledRejected",disabled},{"modeReads",reads.load()},{"cssUses",uses.load()},{"realSteamOperations",0},{"realCssOperations",0}};
 std::ofstream f(out/L"confirmed-delivery.json");f<<r.dump(2);std::cout<<r.dump(2)<<'\n';return ok?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
