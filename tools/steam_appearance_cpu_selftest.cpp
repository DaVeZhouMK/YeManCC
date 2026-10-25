#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_ui_read_budget.h"
#include "steam_live_cdp.h"
#include "steam_cssloader_idle.h"
#include "steam_cssloader_use.h"
#include <fstream>
#include <iostream>
#include <atomic>
using namespace ymcc;
static unsigned passed=0;
static void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);++passed;std::cout<<"PASS "<<name<<'\n';}
int main(int argc,char** argv){try{
    steamui::ReadBudget budget;
    check(budget.begin(0),"first automatic read admitted");budget.finish(true);
    unsigned admitted=0;for(unsigned i=0;i<5000;++i) admitted+=budget.begin(i);
    check(!admitted,"5000 event edges cannot bypass the 5s gap");
    check(budget.begin(5000),"one read at next 5s boundary");budget.finish(false);
    check(budget.begin(10000),"second failure attempt bounded");budget.finish(false);
    check(budget.begin(15000),"third failure attempt bounded");budget.finish(false);
    for(unsigned i=20000;i<100000;i++) admitted+=budget.begin(i);
    check(!admitted,"exhausted queries remain paused despite event storms");
    check(budget.begin(100000,true),"explicit force renews failed budget");budget.finish(true);
    check(!budget.begin(104999),"manual read also suppresses immediate auto query");
    budget.reset();check(budget.begin(105000),"new Steam session resets the read cache budget");
    std::atomic<bool> held=false,release=false;
    std::thread other([&]{std::lock_guard<std::mutex> lock(steamlive::transportMutex);held=true;while(!release)Sleep(1);});
    while(!held)Sleep(1);const auto before=GetTickCount64();
    const auto busy=steamlive::run(LR"(c:\inert)",{{"operation","ui.mode.get"}},[]{return true;},4000);
    release=true;other.join();
    check(busy.value("reason",std::string{})=="live-busy"&&!busy.value("mutated",true)&&GetTickCount64()-before<500,"short observation returns busy immediately without IO or mutex queueing");
    const auto path=std::filesystem::path(argv[1])/L"native-idle.json";std::filesystem::create_directories(path.parent_path());
    steamsession::Session session{LR"(c:\inert-steam)",42,0,100};
    nlohmann::json marker{{"version",1},{"pid",42},{"birth","100"},{"root","c:\\inert-steam"}};
    {std::ofstream f(path);f<<marker.dump();}
    check(steamcssidle::receiptMatches(path.wstring(),session),"idle receipt matches only live process identity");
    session.created=101;check(!steamcssidle::receiptMatches(path.wstring(),session),"new birth rejects copied idle receipt");
    session.created=100;session.pid=43;check(!steamcssidle::receiptMatches(path.wstring(),session),"PID mismatch rejects idle receipt");
    {std::ofstream f(path);f<<"{bad";}check(!steamcssidle::receiptMatches(path.wstring(),session),"corrupt marker fails closed");
    steamcssuse::Runtime use;check(!use.confirmAndIdle(session),"missing helper is not spawned");
    std::cout<<nlohmann::json{{"ok",true},{"passed",passed},{"realSteamOperations",0},{"realCssOperations",0}}.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
