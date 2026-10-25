#include "flow.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace ymcc::gaminghome;
struct Fake final:Ports {
    bool api=true,selected=true,locked=true,haveFiles=true,taskOk=true,batOk=true,uriOk=true,focusOk=true;
    bool active=true,ymcc=true,steam=false,cancel=false;
    int taskCalls=0,batCalls=0,uriCalls=0,focusCalls=0,steps=0,effects=0;
    int ymccAt=-1,steamAt=-1,cancelAt=-1,leaveAt=-1;
    std::uint64_t tick=0,stepMs=200;
    std::vector<std::string> events;
    bool available() override{return api;}
    bool selectedHome() override{return selected;}
    bool lock() override{return locked;}
    bool files() override{return haveFiles;}
    Snapshot snapshot() override {return {active,ymcc,steam,cancel};}
    bool startYmccTask() override{++taskCalls;++effects;return taskOk;}
    bool startBat() override{++batCalls;++effects;return batOk;}
    bool showSteam() override{++uriCalls;++effects;return uriOk;}
    bool focusSteam() override{++focusCalls;++effects;return focusOk;}
    std::uint64_t now() override{return tick;}
    void wait() override {
        tick+=stepMs;++steps;if(steps>1000)throw std::runtime_error("unbounded wait");
        if(ymccAt>=0&&steps>=ymccAt)ymcc=true;
        if(steamAt>=0&&steps>=steamAt)steam=true;
        if(cancelAt>=0&&steps>=cancelAt)cancel=true;
        if(leaveAt>=0&&steps>=leaveAt)active=false;
    }
    void event(const char* e) override{events.emplace_back(e);}
};
int checks=0;
void check(bool good,const char* msg){++checks;if(!good)throw std::runtime_error(msg);}
void result(Fake& p,Result want,const char* text){check(run(p)==want,text);}
int main(){try {
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
