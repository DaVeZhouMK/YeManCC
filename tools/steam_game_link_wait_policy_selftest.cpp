#define NOMINMAX
#include <windows.h>
#include "../native/steam_game_link_state.h"
#include "../native/steam_game_link_wait_policy.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace ymcc::steamlink;
static ULONGLONG clockTick=0;
static DWORD lastTimeout=0,lastCount=0;
static unsigned hints=0,checks=0,stampReads=0;
static bool kernelWait=false;
static void expect(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
static void steamOriginalRecoveryObserveHint(){++hints;}
static ULONGLONG testTick(){return clockTick;}
static DWORD consumerWait(DWORD count,const HANDLE* handles,BOOL all,DWORD timeout){
    lastTimeout=timeout;lastCount=count;
    if(kernelWait)return ::WaitForMultipleObjects(count,handles,all,timeout);
    const auto signalled=::WaitForMultipleObjects(count,handles,all,0);
    if(signalled!=WAIT_TIMEOUT)return signalled;
    if(timeout!=INFINITE)clockTick+=timeout;
    return WAIT_TIMEOUT;
}
struct Handle {HANDLE value=CreateEventW(nullptr,TRUE,FALSE,nullptr);~Handle(){CloseHandle(value);}};
struct Control {HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr),pokeEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Control(){CloseHandle(stopEvent);CloseHandle(pokeEvent);}};
struct Revoked {Handle handle;};
struct Consumer {
    State state;std::shared_ptr<Control> c=std::make_shared<Control>();
    Handle clientHandle,gameHandle,dirHandle;
    HANDLE directory=dirHandle.value;
    std::vector<Revoked> revokedOriginals;
    waitpolicy::Policy waitPolicy;
    bool pending=false,forceObserve=false,enabled=true;
    ULONGLONG configEpoch=7;
    ULONGLONG idleDue=0;
    std::wstring watchedRoot=L"fixture";
    std::string previousGameStamp="g",previousConsoleStamp="c",previousConnectionStamp="n",gameValue="g",consoleValue="c",connectionValue="n";
    std::string stamp(const std::wstring& file){++stampReads;if(file.find(L"gameprocess")!=std::wstring::npos)return gameValue;if(file.find(L"console")!=std::wstring::npos)return consoleValue;return connectionValue;}
    BOOL FindNextChangeNotification(HANDLE h){return ResetEvent(h);}
    void closeDirectory(){directory=nullptr;waitPolicy.directoryChecked();}
    void directoryCheck(){const auto now=clockTick;
#include "runtime_directory.inc"
    }
    bool observeDue(){const auto now=clockTick,epoch=configEpoch;
#include "runtime_observe.inc"
    }
    bool waitOnce(){
#define GetTickCount64 testTick
#define WaitForMultipleObjects consumerWait
#include "runtime_wait.inc"
#undef WaitForMultipleObjects
#undef GetTickCount64
    }
    void healthy(){expect(state.begin(1,7,clockTick),"capture");sample();}
    PrimaryStatus primaryValue=PrimaryStatus::Exact;
    void sample(){Evidence e;e.sequence=state.snapshot().lastSequence+1;e.tick=clockTick;e.enabled=enabled;e.configEpoch=configEpoch;
        e.game=WaitForSingleObject(gameHandle.value,0)==WAIT_OBJECT_0?IdentityStatus::Exited:IdentityStatus::Same;
        e.steam=WaitForSingleObject(clientHandle.value,0)==WAIT_OBJECT_0?IdentityStatus::Exited:IdentityStatus::Same;
        e.account=AccountStatus::Same;e.tracker=TrackerStatus::Exact;e.primary=primaryValue;e.validator=ValidatorStatus::Accepted;e.ui=UiStatus::RunningAndListed;e.transportAvailable=true;
        waitPolicy.observed(clockTick);forceObserve=false;expect(state.observe(e),"state accepts actual evidence");}
};
int main(){try{
    {Consumer x;clockTick=0;x.healthy();expect(x.state.snapshot().dueTick==10000,"healthy due 10s");x.waitOnce();expect(lastTimeout==10000&&clockTick==10000,"actual wait consumer has one 10s wait");expect(x.observeDue(),"deadline runs");}
    for(bool steam:{false,true}){Consumer x;clockTick=123;x.healthy();SetEvent(steam?x.clientHandle.value:x.gameHandle.value);const auto before=::GetTickCount64();kernelWait=true;x.waitOnce();kernelWait=false;expect(::GetTickCount64()-before<1000,"signalled kernel handle bypasses 10s");expect(clockTick==123&&x.forceObserve&&x.observeDue(),"exit immediate before deadline");x.sample();expect(!x.state.active()&&x.state.snapshot().reason==(steam?"steam-exited":"game-exited"),"exit seals original");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints;SetEvent(x.c->pokeEvent);x.enabled=false;x.waitOnce();expect(hints==h+1&&x.observeDue(),"poke hints and accelerates disable");x.sample();expect(x.state.snapshot().phase=="cancelled","disable immediate");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints,reads=stampReads;SetEvent(x.directory);x.waitOnce();expect(x.waitPolicy.directoryPending(),"directory event queued");for(int i=0;i<1000;++i)x.waitPolicy.directoryNotified(i%100);x.waitOnce();expect(lastTimeout==100&&lastCount==4,"signalled directory excluded until fixed merge deadline");x.directoryCheck();expect(stampReads==reads+3&&hints==h&&!x.observeDue(),"unrelated log noise no hint or reparse");x.waitOnce();expect(lastTimeout==9900,"noise keeps remaining healthy deadline");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints;x.connectionValue="connection changed";SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();expect(hints==h+1&&!x.waitPolicy.readReady(clockTick)&&!x.observeDue(),"connection stamp hint only");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints;x.gameValue="changed";SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();expect(hints==h+1&&!x.observeDue(),"gameprocess burst throttled");x.waitOnce();expect(lastTimeout==400&&x.observeDue(),"gameprocess reads within 500ms");x.sample();expect(x.state.snapshot().dueTick==10500,"event read resets healthy due");}
    // Drive 200 console writes at 50ms intervals through the production wait,
    // merge and observation predicate. Count the initial read as a full read.
    {Consumer x;clockTick=0;x.healthy();unsigned fullReads=1;ULONGLONG lastRead=0;
        const auto beforeHints=hints;
        for(unsigned i=1;i<=200;++i){
            clockTick=i*50;x.directoryCheck();x.consoleValue="LoadedConfig/overlay "+std::to_string(i);
            SetEvent(x.directory);if(!x.waitPolicy.directoryPending())x.waitOnce();
            if(x.observeDue()){
                expect(clockTick-lastRead>=10000,"console-only read interval at least 10s");
                x.sample();lastRead=clockTick;++fullReads;
            }
        }
        expect(clockTick==10000&&fullReads==2,"200 console-only writes/10s permit just initial + one full read");
        expect(hints>beforeHints,"console-only changes still hint active wake");
        std::cout<<"PASS console-only storm events=200 durationMs=10000 fullReads="<<fullReads<<" (includes initial)\n";
    }
    {Consumer x;clockTick=0;x.healthy();x.consoleValue="primary changed";x.primaryValue=PrimaryStatus::Different;
        SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();
        expect(!x.observeDue()&&x.waitPolicy.nextDue(waitpolicy::maximum)==10000,"console-only background deadline keeps 10s fallback");
        x.waitOnce();expect(clockTick==10000&&x.observeDue(),"console-only primary read eventually due");
        x.sample();expect(x.state.snapshot().reason=="native-primary-different","console fallback observes changed primary");
    }
    for(bool backgroundFirst:{false,true}){Consumer x;clockTick=0;x.healthy();
        if(backgroundFirst)x.consoleValue="background";else x.gameValue="urgent";
        SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();
        if(backgroundFirst)x.gameValue="urgent";else x.consoleValue="background";
        SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();
        expect(x.waitPolicy.nextDue(waitpolicy::maximum)==500,"urgent deadline preempts background and cannot be postponed");
        x.waitOnce();expect(clockTick==500&&x.observeDue(),"mixed change read remains urgent");
    }
    {Consumer x;clockTick=0;auto h=hints;SetEvent(x.c->pokeEvent);x.waitOnce();expect(x.pending&&x.idleDue==0&&hints==h+1,"idle poke hints and discovers immediately");}
    {Consumer x;clockTick=0;x.healthy();SetEvent(x.c->stopEvent);expect(!x.waitOnce(),"stop immediate");}
    {Consumer x;clockTick=0;x.healthy();x.configEpoch=8;SetEvent(x.c->pokeEvent);x.waitOnce();expect(x.observeDue(),"epoch bypasses deadline");x.sample();expect(x.state.snapshot().reason=="config-epoch-changed","epoch cancellation consumed immediately");}
    {Consumer x;clockTick=0;x.healthy();unsigned samples=0;for(unsigned i=0;i<20;++i){x.gameValue=std::to_string(i);SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();if(x.observeDue()){x.sample();++samples;}}expect(clockTick==2000&&samples==4,"sustained burst bounded to one read per 500ms without starvation");}
    using namespace waitpolicy;expect(timeout(0,10000,true)==10000,"no fixed 250 or 1000 clamp");expect(timeout(42,41,true)==0,"overdue immediate");expect(timeout(0,maximum,true)==MAXDWORD-1,"finite overflow clamp");expect(timeout(0,0,false)==INFINITE,"idle infinite");expect(add(maximum-5,10)==maximum,"saturated addition");expect(timeout(maximum,maximum,true)==0,"saturated deadline ready");
    Policy p;p.directoryNotified(maximum-10);expect(!p.directoryReady(maximum-1)&&p.directoryReady(maximum),"directory saturation");p.observed(maximum-1);p.relevantChanged(maximum-1);expect(!p.readReady(maximum-1)&&p.readReady(maximum),"read saturation");
    Policy bg;bg.observed(maximum-1);bg.backgroundChanged(maximum-1);expect(!bg.readReady(maximum-1)&&bg.readReady(maximum),"background saturation");
    Policy late;late.observed(0);late.backgroundChanged(12000);expect(late.readReady(12000),"late background read is due immediately");
    Policy early;early.observed(0);early.backgroundChanged(100);early.backgroundChanged(9000);expect(early.nextDue(maximum)==10000,"repeated background changes cannot postpone fallback");
    std::cout<<"PASS production wait/directory/predicate consumer + policy: "<<checks<<" checks; hints="<<hints<<"; no real timed waits or target processes\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
