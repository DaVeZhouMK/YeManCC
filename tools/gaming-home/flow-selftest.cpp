#include "flow.h"
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifdef YMCC_HOME_LEGACY
// Compatibility types only. The complete original header/run is unchanged.
namespace ymcc::gaminghome {
enum class Action { EnsureYmcc, StartSteam, ShowHome, FocusWindow };
using Current = std::function<bool()>;
}
#define YMCC_HOME_NEW_OVERRIDE
#else
#define YMCC_HOME_NEW_OVERRIDE override
#endif
using namespace ymcc::gaminghome;
struct Fake final:Ports {
    bool api=true,selected=true,locked=true,haveFiles=true,taskOk=true,batOk=true,uriOk=true,focusOk=true;
    bool active=true,ymcc=true,steam=false,cancel=false;
#ifndef YMCC_HOME_LEGACY
    bool steamProcess=false,steamObservationAvailable=true,steamObservationCompleted=true,uncertainDispatch=false;
    int steamObservationAt=-1;
    bool uncertain()const override{return uncertainDispatch;}
#endif
    int taskCalls=0,batCalls=0,uriCalls=0,focusCalls=0,steps=0,effects=0;
    int ymccAt=-1,steamAt=-1,cancelAt=-1,leaveAt=-1;
    std::uint64_t tick=0,stepMs=200;
    std::vector<std::string> events;
    bool requestCurrent=true,heldAlive=true;
    unsigned identity=1,heldIdentity=0;
    std::array<int,4> dispatchCalls{},callbackCalls{},rejected{},prepared{};
    std::function<void(Fake&,const char*)> eventHook;
    std::function<void(Fake&,Action)> prepareHook;
    static std::size_t index(Action a){return static_cast<std::size_t>(a);}
    bool available() override{return api;}
    bool selectedHome() override{return selected;}
    bool lock() override{return locked;}
    bool files() override{return haveFiles;}
    Snapshot snapshot() override {
        if(ymcc&&heldIdentity==0)heldIdentity=identity;
#ifndef YMCC_HOME_LEGACY
        const bool running=steam||steamProcess;
        return {active,ymcc,steam,cancel,steamPresence(steamObservationAvailable,steamObservationCompleted,running?1:0,running?42u:0u,running?1234u:0u)};
#else
        return {active,ymcc,steam,cancel};
#endif
    }
    // Identity is the first observed held process in this invocation, never a
    // newly discovered replacement. These are memory facts, not Windows calls.
    bool current(Action a) YMCC_HOME_NEW_OVERRIDE {
        return active&&selected&&!cancel&&requestCurrent&&
            (a==Action::EnsureYmcc||(ymcc&&heldAlive&&heldIdentity!=0&&identity==heldIdentity));
    }
    bool commit(Action a){
        ++effects;
        switch(a){
        case Action::EnsureYmcc:++taskCalls;return taskOk;
        case Action::StartSteam:++batCalls;return batOk;
        case Action::ShowHome:++uriCalls;return uriOk;
        case Action::FocusWindow:++focusCalls;return focusOk;
        }
        throw std::runtime_error("unknown memory action");
    }
    void prepare(Action a){++prepared[index(a)];if(prepareHook)prepareHook(*this,a);}
    bool dispatch(Action a,const Current& stillCurrent) YMCC_HOME_NEW_OVERRIDE {
        ++dispatchCalls[index(a)];prepare(a);++callbackCalls[index(a)];
        if(!stillCurrent()){++rejected[index(a)];return false;}
        return commit(a);
    }
#ifdef YMCC_HOME_LEGACY
    // Do not add Current checks here: that would repair the old control.
    bool legacy(Action a){++dispatchCalls[index(a)];prepare(a);return commit(a);}
    bool startYmccTask() override{return legacy(Action::EnsureYmcc);}
    bool startBat() override{return legacy(Action::StartSteam);}
    bool showSteam() override{return legacy(Action::ShowHome);}
    bool focusSteam() override{return legacy(Action::FocusWindow);}
#endif
    std::uint64_t now() override{return tick;}
    void wait() override {
        tick+=stepMs;++steps;if(steps>1000)throw std::runtime_error("unbounded wait");
        if(ymccAt>=0&&steps>=ymccAt)ymcc=true;
        if(steamAt>=0&&steps>=steamAt)steam=true;
#ifndef YMCC_HOME_LEGACY
        if(steamObservationAt>=0&&steps>=steamObservationAt)steamObservationCompleted=true;
#endif
        if(cancelAt>=0&&steps>=cancelAt)cancel=true;
        if(leaveAt>=0&&steps>=leaveAt)active=false;
    }
    void event(const char* e) override{events.emplace_back(e);if(eventHook)eventHook(*this,e);}
};
int checks=0;
void check(bool good,const char* msg){++checks;if(!good)throw std::runtime_error(msg);}
void result(Fake& p,Result want,const char* text){check(run(p)==want,text);}
int defaultSuite(){try {
    for(auto arg:{L"--activate",L"gamingapp:///",L"GAMINGAPP:///Home",L"yemancc-home:///home",L"YeManCC-Home:"})check(activation(arg),"allowed activation");
    for(auto arg:{L"",L"--activate --kill-xbox",L"https://x/",L"gamingappx:/",L"file:C:\\bad.exe",L"cmd /c anything"})check(!activation(arg),"rejected activation");
    {Fake p;p.api=false;result(p,Result::Unsupported,"missing API");check(p.effects==0,"API failure no effects");}
    {Fake p;p.selected=false;result(p,Result::OutsideFse,"other selected home");check(p.effects==0,"other home no effects");}
    {Fake p;p.locked=false;result(p,Result::Busy,"duplicate activation");check(p.effects==0,"duplicate activation no effects");}
    {Fake p;p.haveFiles=false;result(p,Result::MissingFiles,"missing fixed files");check(p.effects==0,"missing files no effects");}
    {Fake p;p.active=false;result(p,Result::OutsideFse,"desktop entry");check(p.effects==0,"desktop no effects");}
    {Fake p;p.cancel=true;result(p,Result::Cancelled,"initial cancellation");check(p.effects==0,"cancelled no effects");}
    {Fake p;p.steamAt=3;result(p,Result::Success,"YMCC already running, Steam cold");check(p.taskCalls==0&&p.batCalls==1&&p.uriCalls==0,"cold once through original BAT");}
    {Fake p;p.steam=true;result(p,Result::Success,"both running");check(p.taskCalls==0&&p.batCalls==0&&p.uriCalls==1&&p.focusCalls==1,"running show once, no BAT");}
    {Fake p;p.ymcc=false;p.ymccAt=4;p.steamAt=9;result(p,Result::Success,"elevated YMCC task then BAT");check(p.taskCalls==1&&p.batCalls==1&&p.uriCalls==0,"task and BAT exactly once");check(p.events.size()==4&&p.events[0]=="ymcc-task-request"&&p.events[1]=="ymcc-process-observed"&&p.events[2]=="bat-request","production sequence");}
    {Fake p;p.ymcc=false;p.ymccAt=5;p.steam=true;result(p,Result::Success,"Steam exists before YMCC");check(p.taskCalls==1&&p.batCalls==0&&p.uriCalls==1,"reuses existing Steam");}
    {Fake p;p.ymcc=false;p.taskOk=false;result(p,Result::TaskFailed,"task failure");check(p.batCalls==0&&p.uriCalls==0,"no Steam before YMCC task");}
    {Fake p;p.ymcc=false;result(p,Result::YmccTimeout,"YMCC timeout");check(p.tick==YmccDeadlineMs&&p.batCalls==0&&p.taskCalls==1,"bounded YMCC wait no retry");}
    {Fake p;p.ymcc=false;p.ymccAt=75;result(p,Result::YmccTimeout,"YMCC exact deadline");check(p.batCalls==0,"expired request no launch");}
    for(bool leave:{false,true}){Fake p;p.ymcc=false;if(leave)p.leaveAt=2;else p.cancelAt=2;result(p,Result::Cancelled,"cancel during YMCC wait");check(p.batCalls==0&&p.taskCalls==1,"cancel no BAT");}
    {Fake p;p.batOk=false;result(p,Result::BatFailed,"BAT dispatch failure");check(p.batCalls==1&&p.focusCalls==0,"no fallback launcher");}
    {Fake p;p.steam=true;p.uriOk=false;result(p,Result::UriFailed,"URI failure");check(p.batCalls==0,"URI failure no cold BAT fallback");}
    {Fake p;result(p,Result::Timeout,"Steam window timeout");check(p.tick==DeadlineMs&&p.batCalls==1&&p.taskCalls==0,"bounded Steam wait no retry");}
    {Fake p;p.ymcc=false;p.ymccAt=50;result(p,Result::Timeout,"shared activation deadline");check(p.tick==DeadlineMs,"task time counted in original deadline");}
    for(bool leave:{false,true}){Fake p;if(leave)p.leaveAt=2;else p.cancelAt=2;result(p,Result::Cancelled,"cancel during handoff wait");check(p.batCalls==1&&p.uriCalls==0,"no replay after cancellation");}
    {Fake p;p.steam=true;p.focusOk=false;result(p,Result::Timeout,"cannot focus target");check(p.uriCalls==1&&p.batCalls==0,"focus wait no launcher replay");}
    {Fake p;p.steam=true;p.stepMs=1000;p.focusOk=false;p.leaveAt=3;result(p,Result::Cancelled,"leave while focus fails");check(p.uriCalls==1,"no focus-driven relaunch");}
    check(windowCandidate(true,true,true,true,true,false,1280,800),"Steam big picture window candidate");
    check(!windowCandidate(false,true,true,true,true,false,1280,800),"hidden window rejected");
    check(!windowCandidate(true,false,true,true,true,false,1280,800),"other session rejected");
    check(!windowCandidate(true,true,false,true,true,false,1280,800),"foreign executable rejected");
    check(!windowCandidate(true,true,true,false,true,false,1280,800),"other class rejected");
    check(!windowCandidate(true,true,true,true,false,false,1280,800),"popup without app style rejected");
    check(!windowCandidate(true,true,true,true,true,true,1280,800),"resizable Steam desktop rejected");
    check(!windowCandidate(true,true,true,true,true,false,479,800),"small width rejected");
    check(!windowCandidate(true,true,true,true,true,false,1280,269),"small height rejected");
    std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"productExecuted\":false,\"windowsSettingsChanged\":false}\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}}

struct LateCase {std::string name;std::function<void()> body;};
std::string activeCase;
int casesStarted=0,casesPassed=0;
const char* resultName(Result r){
    switch(r){
    case Result::Success:return "Success";case Result::Unsupported:return "Unsupported";
    case Result::OutsideFse:return "OutsideFse";case Result::MissingFiles:return "MissingFiles";
    case Result::Busy:return "Busy";case Result::TaskFailed:return "TaskFailed";
    case Result::InternalError:return "InternalError";case Result::YmccTimeout:return "YmccTimeout";
    case Result::BatFailed:return "BatFailed";case Result::UriFailed:return "UriFailed";
    case Result::Timeout:return "Timeout";case Result::Cancelled:return "Cancelled";
#ifndef YMCC_HOME_LEGACY
    case Result::ActionUnknown:return "ActionUnknown";
#endif
    }return "Unknown";
}
Result observe(Fake& p){
    const auto got=run(p);
    std::cout<<"{\"kind\":\"observation\",\"case\":\""<<activeCase
        <<"\",\"result\":\""<<resultName(got)<<"\",\"tick\":"<<p.tick
        <<",\"task\":"<<p.taskCalls<<",\"bat\":"<<p.batCalls
        <<",\"uri\":"<<p.uriCalls<<",\"focus\":"<<p.focusCalls<<",\"dispatch\":[";
    for(std::size_t i=0;i<4;++i){if(i)std::cout<<',';std::cout<<p.dispatchCalls[i];}
    std::cout<<"],\"callbacks\":[";
    for(std::size_t i=0;i<4;++i){if(i)std::cout<<',';std::cout<<p.callbackCalls[i];}
    std::cout<<"],\"rejected\":[";
    for(std::size_t i=0;i<4;++i){if(i)std::cout<<',';std::cout<<p.rejected[i];}
    std::cout<<"],\"events\":[";
    for(std::size_t i=0;i<p.events.size();++i){if(i)std::cout<<',';std::cout<<'"'<<p.events[i]<<'"';}
    std::cout<<"],\"productExecuted\":false}"<<std::endl;
    return got;
}
Fake launchFake(Action a){Fake p;if(a==Action::EnsureYmcc)p.ymcc=false;else if(a==Action::ShowHome)p.steam=true;return p;}
const char* requestEvent(Action a){
    switch(a){case Action::EnsureYmcc:return "ymcc-task-request";
    case Action::StartSteam:return "bat-request";case Action::ShowHome:return "existing-steam-home-request";
    default:throw std::runtime_error("not a launch action");}
}
void noReplay(const Fake& p){check(p.taskCalls<=1&&p.batCalls<=1&&p.uriCalls<=1,"no launcher replay");}
void noEffects(const Fake& p){check(p.effects==0&&p.taskCalls==0&&p.batCalls==0&&p.uriCalls==0&&p.focusCalls==0,"no stale effect emitted");noReplay(p);}
std::vector<LateCase> lateCases(){
    std::vector<LateCase> v;
    const std::pair<Action,const char*> actions[]={
        {Action::EnsureYmcc,"task"},{Action::StartSteam,"bat"},{Action::ShowHome,"uri"}};
    for(const auto& entry:actions){
        const auto a=entry.first;const std::string label=entry.second;
        v.push_back({label+"-log-cancel",[a]{
            auto p=launchFake(a);p.eventHook=[a](Fake& f,const char* e){if(std::string_view(e)==requestEvent(a))f.cancel=true;};
            check(observe(p)==Result::Cancelled,"log callback cancellation result");noEffects(p);
            check(p.dispatchCalls[Fake::index(a)]==0,"cancelled log rejected before outlet");
        }});
        v.push_back({label+"-prepare-current-invalid",[a]{
            auto p=launchFake(a);p.prepareHook=[a](Fake& f,Action b){if(a==b)f.requestCurrent=false;};
            check(observe(p)==Result::Cancelled,"prepared stale request cancelled");noEffects(p);
            check(p.prepared[Fake::index(a)]==1&&p.callbackCalls[Fake::index(a)]==1&&p.rejected[Fake::index(a)]==1,"late Current synchronously refused once");
        }});
        v.push_back({label+"-prepare-expired",[a]{
            auto p=launchFake(a);p.tick=1234;const auto budget=a==Action::EnsureYmcc?YmccDeadlineMs:DeadlineMs;
            p.prepareHook=[a,budget](Fake& f,Action b){if(a==b)f.tick+=budget;};
            check(observe(p)==(a==Action::EnsureYmcc?Result::YmccTimeout:Result::Timeout),"prepare exact deadline result");
            noEffects(p);check(p.tick==1234+budget,"original begin expiry without refund");
            check(p.callbackCalls[Fake::index(a)]==1&&p.rejected[Fake::index(a)]==1,"deadline checked inside outlet");
        }});
        v.push_back({label+"-log-expired",[a]{
            auto p=launchFake(a);const auto budget=a==Action::EnsureYmcc?YmccDeadlineMs:DeadlineMs;
            p.eventHook=[a,budget](Fake& f,const char* e){if(std::string_view(e)==requestEvent(a))f.tick+=budget;};
            check(observe(p)==(a==Action::EnsureYmcc?Result::YmccTimeout:Result::Timeout),"log exact deadline result");
            noEffects(p);check(p.dispatchCalls[Fake::index(a)]==0,"expiry rejected before outlet");
        }});
        v.push_back({label+"-bool-failure",[a]{
            auto p=launchFake(a);if(a==Action::EnsureYmcc)p.taskOk=false;else if(a==Action::StartSteam)p.batOk=false;else p.uriOk=false;
            const auto want=a==Action::EnsureYmcc?Result::TaskFailed:(a==Action::StartSteam?Result::BatFailed:Result::UriFailed);
            check(observe(p)==want,"ordinary bool failure preserved");check(p.effects==1&&p.focusCalls==0,"failed action once without fallback");noReplay(p);
        }});
    }
    v.push_back({"focus-prepare-cancel",[]{
        Fake p;p.steam=true;p.prepareHook=[](Fake& f,Action a){if(a==Action::FocusWindow)f.cancel=true;};
        check(observe(p)==Result::Cancelled,"cancel immediately before focus");
        check(p.uriCalls==1&&p.focusCalls==0&&p.effects==1,"no cancelled foreground emission");noReplay(p);
        check(p.callbackCalls[3]==1&&p.rejected[3]==1,"focus Current refused once");
    }});
    v.push_back({"focus-prepare-expired",[]{
        Fake p;p.steam=true;p.prepareHook=[](Fake& f,Action a){if(a==Action::FocusWindow)f.tick=DeadlineMs;};
        check(observe(p)==Result::Timeout,"focus exact deadline");check(p.focusCalls==0&&p.uriCalls==1,"expired focus never emitted");noReplay(p);
    }});
    v.push_back({"focus-held-identity-lost",[]{
        Fake p;p.steam=true;p.prepareHook=[](Fake& f,Action a){if(a==Action::FocusWindow)f.heldAlive=false;};
        check(observe(p)==Result::Cancelled,"held YMCC exit before focus");check(p.focusCalls==0&&p.uriCalls==1,"no focus without held process");noReplay(p);
    }});
    for(const bool replacement:{false,true}){
        v.push_back({replacement?"ymcc-wait-held-identity-replaced":"ymcc-wait-held-identity-lost",[replacement]{
            Fake p;p.ymcc=false;p.ymccAt=2;
            p.eventHook=[replacement](Fake& f,const char* e){if(std::string_view(e)=="ymcc-process-observed"){
                f.heldAlive=false;if(replacement){++f.identity;f.ymcc=true;}else f.ymcc=false;
            }};
            check(observe(p)==Result::Cancelled,"first observed YMCC identity no longer alive");
            check(p.taskCalls==1&&p.batCalls==0&&p.uriCalls==0&&p.focusCalls==0,"no replacement adoption after wait");noReplay(p);
        }});
    }
    v.push_back({"ymcc-15s-preparation-not-refunded",[]{
        Fake p;p.tick=1234;p.ymcc=false;p.ymccAt=40;
        p.prepareHook=[](Fake& f,Action a){if(a==Action::EnsureYmcc)f.tick+=7000;};
        check(observe(p)==Result::YmccTimeout,"task preparation charged to original 15 seconds");
        check(p.tick==1234+YmccDeadlineMs&&p.taskCalls==1&&p.batCalls==0&&p.steps==40,"no new YMCC wait budget");noReplay(p);
    }});
    v.push_back({"total-60s-preparation-and-ymcc-not-refunded",[]{
        Fake p;p.tick=1234;p.ymcc=false;p.ymccAt=10;
        p.prepareHook=[](Fake& f,Action a){if(a==Action::EnsureYmcc)f.tick+=12000;else if(a==Action::StartSteam)f.tick+=3000;};
        check(observe(p)==Result::Timeout,"shared 60 seconds includes all preparation and waits");
        check(p.tick==1234+DeadlineMs&&p.taskCalls==1&&p.batCalls==1&&p.focusCalls==0,"original total deadline retained");noReplay(p);
    }});
    v.push_back({"focus-failure-no-launch-replay",[]{
        Fake p;p.steam=true;p.focusOk=false;p.stepMs=1000;
        check(observe(p)==Result::Timeout,"bool focus failure waits only original budget");
        check(p.tick==DeadlineMs&&p.uriCalls==1&&p.taskCalls==0&&p.batCalls==0&&p.focusCalls==60,"focus retries never replay URI/BAT/task");noReplay(p);
    }});
    v.push_back({"valid-preparation-one-handoff",[]{
        Fake p;p.ymcc=false;p.ymccAt=1;p.steamAt=2;
        p.prepareHook=[](Fake& f,Action){f.tick+=100;};
        check(observe(p)==Result::Success,"valid Current still dispatches");
        check(p.taskCalls==1&&p.batCalls==1&&p.uriCalls==0&&p.focusCalls==1,"one task BAT focus handoff");noReplay(p);
    }});
#ifndef YMCC_HOME_LEGACY
    v.push_back({"public-steam-presence-projection",[]{
        check(steamPresence(false,true,0,0,0)==SteamPresence::Unknown,"unavailable absence is not authority");
        check(steamPresence(true,false,0,0,0)==SteamPresence::Unknown,"pending absence is not authority");
        check(steamPresence(true,false,1,42,1234)==SteamPresence::Unknown,"pending window read retains original wait");
        check(steamPresence(true,true,-1,0,0)==SteamPresence::Unknown,"unknown process state remains unknown");
        check(steamPresence(true,true,0,42,1234)==SteamPresence::Unknown,"contradictory absence rejected");
        check(steamPresence(true,true,1,42,0)==SteamPresence::Unknown,"missing birth rejected");
        check(steamPresence(true,true,1,0,1234)==SteamPresence::Unknown,"missing PID rejected");
        check(steamPresence(true,true,0,0,0)==SteamPresence::Absent,"complete public absence");
        check(steamPresence(true,true,1,42,1234)==SteamPresence::Running,"complete public process identity");
    }});
    v.push_back({"live-steam-no-window-uses-home-request",[]{
        Fake p;p.steamProcess=true;p.steamAt=3;
        p.prepareHook=[](Fake& f,Action a){if(a==Action::ShowHome)check(!f.steam&&f.steps==0,"HomeShow before a visible window");};
        check(observe(p)==Result::Success,"wait for window after one public HomeShow");
        check(p.batCalls==0&&p.taskCalls==0&&p.uriCalls==1&&p.focusCalls==1,"running Steam cannot invoke BAT");noReplay(p);
    }});
    v.push_back({"public-observation-pending-then-live",[]{
        Fake p;p.steamProcess=true;p.steamObservationCompleted=false;p.steamObservationAt=3;p.steamAt=6;
        p.prepareHook=[](Fake& f,Action a){if(a==Action::ShowHome)check(!f.steam&&f.steps==3,"only act once the public observation completes");};
        check(observe(p)==Result::Success,"pending observation can complete with running Steam");
        check(p.batCalls==0&&p.uriCalls==1&&p.focusCalls==1,"pending did not trigger a cold action");noReplay(p);
    }});
    v.push_back({"public-observation-unknown-until-deadline",[]{
        Fake p;p.steamObservationAvailable=false;
        check(observe(p)==Result::Timeout&&p.tick==DeadlineMs,"unknown observes original activation deadline");noEffects(p);
    }});
    v.push_back({"public-observation-pending-cancelled",[]{
        Fake p;p.steamObservationCompleted=false;p.cancelAt=2;
        check(observe(p)==Result::Cancelled,"cancel while waiting for public facts");noEffects(p);
    }});
    v.push_back({"live-steam-no-window-unknown-home-outcome",[]{
        Fake p;p.steamProcess=true;p.uriOk=false;p.uncertainDispatch=true;
        check(observe(p)==Result::ActionUnknown,"unknown HomeShow outcome retained");
        check(p.batCalls==0&&p.uriCalls==1&&p.focusCalls==0,"uncertain submission is not cold-start authority");noReplay(p);
    }});
#endif
    return v;
}
int rejectSelection(){
    std::cout<<"{\"status\":\"SELECTION_REJECTED\",\"casesStarted\":0,\"checks\":0,\"productExecuted\":false}"<<std::endl;return 2;
}
#ifdef YMCC_GAMING_HOME_INFORMATION
#include "information-selftest.inc"
#endif
int main(int argc,char** argv){
#ifdef YMCC_GAMING_HOME_INFORMATION
    return HomeInformationFixture::main(argc,argv);
#endif
    if(argc==1)return defaultSuite();
    // Validate the complete selection before executing any default/new case.
    if(argc<2||argc>3||std::string_view(argv[1])!="--late-admission")return rejectSelection();
    const auto cases=lateCases();
    const std::string_view selected=argc==3?std::string_view(argv[2]):std::string_view{};
    if(argc==3){
        bool found=false;for(const auto& c:cases)if(selected==c.name)found=true;
        if(selected.empty()||!found)return rejectSelection();
    }
    try{
        for(const auto& c:cases){
            if(argc==3&&selected!=c.name)continue;
            activeCase=c.name;++casesStarted;const int before=checks;c.body();++casesPassed;
            std::cout<<"{\"kind\":\"case\",\"case\":\""<<c.name<<"\",\"status\":\"PASS\",\"checks\":"<<(checks-before)<<"}"<<std::endl;
        }
        std::cout<<"{\"status\":\"PASS\",\"mode\":\"late-admission\",\"casesStarted\":"<<casesStarted
            <<",\"casesPassed\":"<<casesPassed<<",\"checks\":"<<checks
            <<",\"defaultExecuted\":false,\"productExecuted\":false,\"windowsSettingsChanged\":false}"<<std::endl;
        return 0;
    }catch(const std::exception& e){
        std::cout<<"{\"status\":\"FAIL\",\"mode\":\"late-admission\",\"case\":\""<<activeCase
            <<"\",\"casesStarted\":"<<casesStarted<<",\"casesPassed\":"<<casesPassed
            <<",\"checks\":"<<checks<<",\"defaultExecuted\":false,\"productExecuted\":false}"<<std::endl;
        std::cerr<<"FAIL "<<activeCase<<" after "<<checks<<": "<<e.what()<<std::endl;return 1;
    }
}
