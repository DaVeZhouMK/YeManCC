// Runs production workers with inert sessions and mock readers, never Steam/CSS.
// -I before/native can measure an older snapshot; YMCC_BASELINE_METRICS skips
// the new-budget assertions but keeps the same workload and JSON measurements.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_session_observer.h"
#include <json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
static std::atomic<unsigned long long> signals{0};
static BOOL countedSetEvent(HANDLE h) { ++signals; return ::SetEvent(h); }
#define SetEvent countedSetEvent
#include "steam_ui_mode_observer.h"
#undef SetEvent
#include "steam_cssloader_guard.h"
using namespace ymcc;
static void check(bool value,const char* label) { if(!value) throw std::runtime_error(label); }
static ULONGLONG cpu() {
 FILETIME birth{},exit{},kernel{},user{};check(GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user),"process times");
 return (ULONGLONG(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+(ULONGLONG(user.dwHighDateTime)<<32)+user.dwLowDateTime;
}
static ULONGLONG reads() { IO_COUNTERS io{};check(GetProcessIoCounters(GetCurrentProcess(),&io),"IO counters");return io.ReadOperationCount; }
template<class F> static void until(F f) { const auto end=GetTickCount64()+3000;while(!f()){check(GetTickCount64()<end,"worker deadline");Sleep(2);} }
int wmain(int argc,wchar_t** argv) { try {
 check(argc==2,"isolated output directory required");std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
 steamsession::Session session{L"X:\\inert-steam",42,0,100};
 steamui::ModeObserver shared;steamui::ModeObserverConfig config;std::atomic<unsigned> modeReads{0};
 config.debounceMs=1;config.minimumGapMs=60000;
 config.read=[&](const auto& s){++modeReads;return nlohmann::json{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","desktop"}};};
 check(shared.initialize(config),"shared initialize");shared.setDemand(steamui::ModeConsumer::Css,true);shared.observeSteam(session);
 until([&]{return shared.snapshot(session).value("mode",std::string{})=="desktop";});Sleep(20);
 const auto idleCpu=cpu(),idleIo=reads();Sleep(1000);
 const auto idleCpuUsed=cpu()-idleCpu,idleReads=reads()-idleIo;check(modeReads==1,"idle must not query mode");
 signals=0;const auto stormStart=GetTickCount64(),stormCpu=cpu();
 for(unsigned i=0;i<50000;++i){check(shared.requestRefresh(session),"accepted hint");SwitchToThread();}
 const auto stormCpuUsed=cpu()-stormCpu,stormWall=GetTickCount64()-stormStart,stormSignals=signals.load();
 check(modeReads==1,"storm must not bypass shared minimum read gap");shared.shutdown();
 const auto link=out/L"fixture"/L"link.txt";std::filesystem::create_directories(link.parent_path());
 {std::ofstream f(link);f<<"X:\\inert-css\\CssLoader-Standalone-Headless.exe";check(bool(f),"fixture write");}
 steamcss::Runtime css;steamcss::Config guard;std::atomic<unsigned> cssReads{0},cssProbes{0};
 guard.linkFile=link.wstring();guard.delays={10,20,30};guard.modeDelays={1,2,30};
 guard.readMode=[&](const auto&){++cssReads;return steamcss::UiMode::Desktop;};
 guard.ensure=[&](const auto&,const auto&,const auto&){++cssProbes;return steamcss::Result{nullptr,0,"waiting-manual-steam",{}};};
 check(css.initialize(guard),"guard initialize");css.observeSteam(session);
 until([&]{return css.snapshot().phase=="waiting-manual-steam"&&cssReads==1;});Sleep(30);
 const auto ioStart=reads();
 for(unsigned i=0;i<50;++i){const auto before=cssReads.load();css.requestUiRefresh();until([&]{return cssReads>before;});Sleep(3);}
 const auto unchangedModeIo=reads()-ioStart;const auto finalReads=cssReads.load(),finalProbes=cssProbes.load();
 const auto guardCpu=cpu(),guardIo=reads();Sleep(1000);
 const auto guardIdleCpu=cpu()-guardCpu,guardIdleIo=reads()-guardIo;
 check(cssReads==finalReads&&cssProbes==finalProbes,"guard idle must not scan or poll");
 std::filesystem::remove(link);until([&]{return css.snapshot().phase=="disabled";});css.shutdown();
#ifndef YMCC_BASELINE_METRICS
 check(stormSignals==1,"50000 pending hints must signal once");check(unchangedModeIo==0,"unchanged modes must not reread the link");
 check(idleCpuUsed<=312500&&guardIdleCpu<=312500,"idle worker CPU exceeds 31.25ms per one-second sample");
 check(idleReads==0&&guardIdleIo==0,"idle workers must not perform file reads");
#endif
 nlohmann::json record{{"ok",true},{"eventHints",50000},{"sharedWakeSignals",stormSignals},{"sharedModeReadsDuringStorm",modeReads.load()-1},{"stormWallMs",stormWall},{"stormCpuMs",stormCpuUsed/10000.0},{"sharedIdleSampleMs",1000},{"sharedIdleCpuMs",idleCpuUsed/10000.0},{"sharedIdleFileReads",idleReads},{"unchangedModeHints",50},{"unchangedModeFileReads",unchangedModeIo},{"guardIdleSampleMs",1000},{"guardIdleCpuMs",guardIdleCpu/10000.0},{"guardIdleFileReads",guardIdleIo},{"realSteamOperations",0},{"realCssOperations",0}};
 std::ofstream result(out/L"event-budget.json");result<<record.dump(2);std::cout<<record.dump(2)<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
