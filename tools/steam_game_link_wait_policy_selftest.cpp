#define NOMINMAX
#include <windows.h>
#include "../native/steam_game_link_state.h"
#include "../native/steam_game_link_wait_policy.h"
#include "../native/steam_game_link_log_watch.h"
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
using namespace ymcc::steamlink;
static ULONGLONG clockTick=0;
static DWORD lastTimeout=0,lastCount=0;
static unsigned hints=0,checks=0,stampReads=0;
static bool kernelWait=false;
static bool kernelClock=false;
static unsigned predicatePolls=0;
static unsigned signalAttempts=0,signalFailures=0;
static std::function<void()> afterKernelWait,afterSignal;
static BOOL testSetEvent(HANDLE event){
    ++signalAttempts;
    if(signalFailures){--signalFailures;return FALSE;}
    const auto result=::SetEvent(event);
    if(afterSignal){auto hook=std::move(afterSignal);afterSignal=nullptr;hook();}
    return result;
}
static void expect(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
static void steamOriginalRecoveryObserveHint(){++hints;}
static ULONGLONG testTick(){return kernelClock?::GetTickCount64():clockTick;}
[[maybe_unused]] static DWORD predicateWait(HANDLE handle,DWORD timeout){++predicatePolls;return ::WaitForSingleObject(handle,timeout);}
static DWORD consumerWait(DWORD count,const HANDLE* handles,BOOL all,DWORD timeout){
    lastTimeout=timeout;lastCount=count;
    if(kernelWait)return ::WaitForMultipleObjects(count,handles,all,timeout);
    const auto signalled=::WaitForMultipleObjects(count,handles,all,0);
    if(afterKernelWait){auto hook=std::move(afterKernelWait);afterKernelWait=nullptr;hook();}
    if(signalled!=WAIT_TIMEOUT)return signalled;
    if(timeout!=INFINITE)clockTick+=timeout;
    return WAIT_TIMEOUT;
}
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;
    explicit Handle(bool):value(CreateEventW(nullptr,TRUE,FALSE,nullptr)){}
    Handle(Handle&& other) noexcept:value(std::exchange(other.value,nullptr)){}
    Handle& operator=(Handle&& other) noexcept {
        if(this!=&other){if(value)CloseHandle(value);value=std::exchange(other.value,nullptr);}return *this;
    }
    ~Handle(){if(value)CloseHandle(value);}
};
struct Control {
    HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr),pokeEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    std::atomic<unsigned> hintsPending{0};
    bool hintWakeSucceeded=false;
    std::mutex observationMx;
    std::deque<std::function<void()>> observationJobs;
    std::atomic<bool> stop{false},observationsClosed{false};
    unsigned directoryNotices=0;
    ULONGLONG lastDirectoryTick=0;
    ~Control(){CloseHandle(stopEvent);CloseHandle(pokeEvent);}
};
namespace SteamGameLink {
using ::Control;
static std::mutex lifecycleMx;
static std::shared_ptr<Control> control;
static constexpr std::size_t observationQueueLimit=4;
#include "runtime_observation_drain.inc"
}
static std::atomic<bool> g_exitRequested{false};
#define SetEvent testSetEvent
#include "runtime_hint_notify.inc"
#include "runtime_observation_submit.inc"
#undef SetEvent
static void noteStartupDirectory(const std::shared_ptr<Control>& c,ULONGLONG tick){++c->directoryNotices;c->lastDirectoryTick=tick;}
struct Revoked {int game;Handle handle;};
struct NativeGameNoGameReceipt {};
struct DirectoryFake {
    HANDLE value=nullptr;
    unsigned changes=logs::All;
    HANDLE handle() const {return value;}
    operator HANDLE() const {return value;}
    bool consume(const std::wstring&,unsigned& mask){mask=changes;return ResetEvent(value)!=FALSE;}
};
struct Consumer {
    State state;std::shared_ptr<Control> c=std::make_shared<Control>();
    Handle clientHandle{true},gameHandle{true},dirHandle{true};
    DirectoryFake directory{dirHandle.value};
    std::vector<Revoked> revokedOriginals;
    int game=1,currentRecordPre=1;
    waitpolicy::Policy waitPolicy;
    bool pending=false,forceObserve=false,enabled=true,startupCapturePending=false;
    bool qualifiedNoGame=false;
    ULONGLONG configEpoch=7;
    ULONGLONG idleDue=0;
    std::wstring watchedLogs=L"fixture\\logs";
    std::string previousGameStamp="g",previousConsoleStamp="c",previousConnectionStamp="n",gameValue="g",consoleValue="c",connectionValue="n";
    std::string stamp(const std::wstring& file){++stampReads;if(file.find(L"gameprocess")!=std::wstring::npos)return gameValue;if(file.find(L"console")!=std::wstring::npos)return consoleValue;return connectionValue;}
    void closeDirectory(){directory.value=nullptr;waitPolicy.directoryChecked();startupCapturePending=true;}
    bool nativeValveCaptureNoGame(NativeGameNoGameReceipt*) const {return qualifiedNoGame;}
    Consumer(){SteamGameLink::control=c;}
    ~Consumer(){SteamGameLink::control.reset();}
    void normalHint(){steamGameLinkNotify(false);}
    void windowHint(){steamGameLinkNotify(true);}
    void directoryCheck(){const auto now=clockTick;
#include "runtime_directory.inc"
    }
    bool observeDue(){const auto now=clockTick,epoch=configEpoch;
#define WaitForSingleObject predicateWait
#include "runtime_observe.inc"
#undef WaitForSingleObject
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
    AccountStatus accountValue=AccountStatus::Same;
    void sample(){Evidence e;e.sequence=state.snapshot().lastSequence+1;e.tick=clockTick;e.enabled=enabled;e.configEpoch=configEpoch;
        e.game=WaitForSingleObject(gameHandle.value,0)==WAIT_OBJECT_0?IdentityStatus::Exited:IdentityStatus::Same;
        e.steam=WaitForSingleObject(clientHandle.value,0)==WAIT_OBJECT_0?IdentityStatus::Exited:IdentityStatus::Same;
        e.account=accountValue;e.tracker=TrackerStatus::Exact;e.primary=primaryValue;e.validator=ValidatorStatus::Accepted;e.ui=UiStatus::RunningAndListed;e.transportAvailable=true;
        waitPolicy.observed(clockTick);forceObserve=false;expect(state.observe(e),"state accepts actual evidence");
        const auto s=state.snapshot();
#include "runtime_link_retirement.inc"
    }
};
static void linkRetirementCases(){
    for(bool steam:{false,true}){
        Consumer x;clockTick=0;x.healthy();clockTick=125;
        x.normalHint();SetEvent(steam?x.clientHandle.value:x.gameHandle.value);
        const auto beforeHints=hints;
        x.waitOnce();expect(x.forceObserve,"a ready exit or successor hint schedules a read");
        x.sample();expect(!x.state.active()&&x.state.snapshot().terminal,"old link seals before fresh discovery");
        expect(!x.game&&!x.currentRecordPre&&!x.gameHandle.value&&!x.clientHandle.value,"old game and record authority discarded");
        expect(x.pending&&x.idleDue==clockTick,"retirement must preserve one discovery without another hint");
        x.waitOnce();expect(lastTimeout==0,"pending discovery bypasses the former healthy cadence");
        expect(!x.c->hintsPending&&hints>beforeHints,"successor hint is delivered by the one pending discovery");
        x.pending=false; // The real idle-discovery branch consumes the pending bit before reading.
        x.waitOnce();expect(lastTimeout==INFINITE,"no successor and no new hint do not cause a polling loop");
    }
    {Consumer x;clockTick=0;x.healthy();x.enabled=false;x.sample();
        expect(!x.pending&&x.state.snapshot().reason=="off","turning off does not schedule discovery");}
    {Consumer x;clockTick=0;x.healthy();x.configEpoch=8;x.sample();
        expect(x.pending&&x.state.snapshot().reason=="config-epoch-changed","enabled configuration change schedules fresh discovery");}
    {Consumer x;clockTick=0;x.healthy();const auto held=x.gameHandle.value;x.accountValue=AccountStatus::Changed;x.sample();
        expect(x.pending&&x.state.snapshot().phase=="revoked"&&!x.gameHandle.value&&!x.clientHandle.value,"account change seals old link and schedules fresh discovery");
        expect(x.revokedOriginals.size()==1&&x.revokedOriginals.front().game==1&&x.revokedOriginals.front().handle.value==held,"revoked original remains blocked by retained identity and handle");}
}
namespace ymcc::steamgamefacts {
struct StartupTrackerFake { unsigned retired=0; void retireStartupLaunches(){++retired;} };
static StartupTrackerFake& trackerOwner(){static StartupTrackerFake value;return value;}
}
struct StartupEnrollment {
#include "runtime_startup_scope_init.inc"
    unsigned c=0,watchOpens=0,watchCloses=0;
    bool startupCapturePending=true,allowWatch=true;
    Handle fakeWatch{true};
    struct StartupDirectory : DirectoryFake {
        StartupEnrollment* owner;
        explicit StartupDirectory(StartupEnrollment* owner):owner(owner){}
        bool open(const std::wstring&){++owner->watchOpens;value=owner->allowWatch?owner->fakeWatch.value:nullptr;return value!=nullptr;}
    } directory{this};
    std::wstring watchedRoot,watchedLogs;
    std::string previousGameStamp,previousConsoleStamp,previousConnectionStamp;
    void retireStartupCaptureDiagnostic(unsigned,const char*){++c;}
    void scope(const std::array<unsigned long long,2>& captureScope){
#include "runtime_startup_scope.inc"
    }
    void closeDirectory(){++watchCloses;ymcc::steamgamefacts::trackerOwner().retireStartupLaunches();directory.value=nullptr;watchedRoot.clear();startupCapturePending=true;}
    std::string stamp(const std::wstring& path){return std::to_string(path.size());}
    struct Observed { DWORD pid=100; unsigned long long created=200; std::wstring root; };
    void enroll(const Observed& observed){
#include "runtime_startup_watch.inc"
    }
};
static void startupEnrollmentCases(){
    auto& tracker=ymcc::steamgamefacts::trackerOwner();
    tracker.retired=0;StartupEnrollment x;
    x.scope({11,22});
    expect(x.startupCaptureScope&&x.c==0&&tracker.retired==0&&x.startupCapturePending,"first startup scope does not retire early window evidence");
    x.startupCapturePending=false;x.scope({11,22});
    expect(x.c==0&&tracker.retired==0&&!x.startupCapturePending,"unchanged scope does not retire or reschedule");
    x.scope({11,23});
    expect(x.c==1&&tracker.retired==1&&x.startupCapturePending,"observed config change still retires evidence and reschedules");
    x.startupCapturePending=false;x.scope({12,23});
    expect(x.c==2&&tracker.retired==2&&x.startupCapturePending,"observed rules change still retires evidence and reschedules");
    tracker.retired=0;StartupEnrollment watch;
    watch.allowWatch=false;watch.enroll({100,200,L"test-root"});
    expect(!watch.directory&&watch.watchedRoot.empty()&&watch.watchOpens==1&&watch.watchCloses==0&&tracker.retired==0,"first watch enrollment does not retire early window evidence");
    watch.startupCapturePending=false;watch.allowWatch=true;watch.enroll({100,200,L"test-root"});
    expect(watch.directory&&watch.watchedRoot==L"test-root"&&watch.watchOpens==2&&tracker.retired==0,"retry after absent watch preserves retained evidence");
    expect(watch.startupCapturePending,"watch enrollment still schedules a catch-up read");
    watch.enroll({100,200,L"test-root"});
    expect(watch.watchOpens==2&&tracker.retired==0,"same watch root needs no reenrollment");
    watch.enroll({101,201,L"replacement-root"});
    expect(watch.watchOpens==3&&watch.watchCloses==1&&tracker.retired==1&&watch.watchedRoot==L"replacement-root","replacement of an active watch still retires evidence");
}
static void idleWindowCases(){
    {Consumer x;clockTick=0;x.qualifiedNoGame=true;x.waitPolicy.observed(clockTick);
        const auto h=hints;unsigned reads=0;
        for(unsigned i=1;i<=50;++i){clockTick=i*200;x.windowHint();x.waitOnce();x.directoryCheck();
            if(x.pending&&clockTick>=x.idleDue){++reads;x.pending=false;x.forceObserve=false;x.waitPolicy.observed(clockTick);}}
        std::cout<<"idle_window_workload observations="<<reads<<" recovery_hints="<<hints-h<<'\n';
        expect(reads==1&&hints==h+50,"qualified idle window storm allows one audit and forwards all hints");
        x.waitOnce();expect(lastTimeout==INFINITE&&!x.pending,"idle audit does not add recurring polling");}
    for(unsigned reason=0;reason<4;++reason){Consumer x;clockTick=100;x.waitPolicy.observed(0);
        x.qualifiedNoGame=reason!=0;
        if(reason==1)x.directory.value=nullptr;
        if(reason==2)x.startupCapturePending=true;
        if(reason==3){x.pending=true;x.idleDue=30000;}
        x.windowHint();x.waitOnce();expect(x.pending&&x.idleDue==100&&x.forceObserve,"incomplete idle evidence preserves immediate discovery");}
    {Consumer x;clockTick=100;x.qualifiedNoGame=true;x.waitPolicy.observed(0);
        x.windowHint();x.waitOnce();expect(!x.pending&&!x.forceObserve,"qualified idle hint does not force discovery");
        clockTick=120;x.windowHint();x.normalHint();x.waitOnce();expect(x.pending&&x.idleDue==120&&x.forceObserve,"urgent selection or session notice preempts idle audit");}
    {Consumer x;clockTick=100;x.qualifiedNoGame=true;x.waitPolicy.observed(0);
        x.windowHint();x.waitOnce();x.gameValue="new game";
        SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();
        expect(!x.pending&&x.waitPolicy.nextDue(waitpolicy::maximum)==500,"game log preempts idle window audit");
        x.waitOnce();x.directoryCheck();expect(x.pending&&x.idleDue==500,"game log runs on original urgent deadline");}
}
namespace idleDiscoveryFixture {
using json=std::map<std::string,std::string>;
using Client=unsigned;
using Game=unsigned;
struct NativeGameSelectionReceipt { unsigned pid=0; };
static bool qualifiedEmpty=false, clientAvailable=true, dropDuringClientRead=false;
static unsigned selected=0, clientReads=0, gameReads=0;
static std::string outcome;
static bool nativeValveCaptureNoGame(NativeGameNoGameReceipt*){return qualifiedEmpty;}
static bool nativeValveReadCachedSelection(NativeGameSelectionReceipt* receipt){receipt->pid=selected;return selected!=0;}
static json discoveryEvidence(){return {{"bindingPolicy","fixture"}};}
static void noteStartupClientDiscovery(const std::shared_ptr<Control>&,const json& diagnostic){outcome=diagnostic.at("clientDiscoveryOutcome");}
static Client discoverClient(unsigned long long epoch,json* diagnostic){
    expect(epoch==7,"original config epoch reaches Steam read");
    ++clientReads;*diagnostic={{"clientDiscoveryOutcome","read"}};
    if(dropDuringClientRead)selected=0;
    return clientAvailable?1:0;
}
namespace SleepSteamRecovery { static bool validSession(Client client){return client!=0;} }
static Game discoverGame(Client,DWORD session,json& binding,NativeGameSelectionReceipt* receipt){
    expect(session==5,"original Windows session reaches game read");
    ++gameReads;receipt->pid=selected;binding=discoveryEvidence();return selected;
}
#include "runtime_idle_discovery.inc"
static void run(){
    Consumer wait;clockTick=0;Client client=0;Game game=0;json binding;NativeGameSelectionReceipt receipt;
    const auto read=[&]{receipt={};binding.clear();discoverIdleGame(wait.c,7,5,client,game,binding,receipt);};
    for(unsigned i=0;i<50;++i){wait.windowHint();wait.waitOnce();
        expect(wait.pending,"unqualified window hint keeps discovery available");wait.pending=false;read();}
    expect(clientReads==0&&gameReads==0&&!client&&!game&&!receipt.pid&&outcome=="skipped-selection-unavailable"&&
        binding.at("reason")=="running-link-selected-target-unavailable","no selected target skips Steam round trip without proving absence");
    wait.waitOnce();expect(lastTimeout==INFINITE,"selection wait adds no heartbeat");
    qualifiedEmpty=true;read();qualifiedEmpty=false;
    expect(clientReads==0&&outcome=="skipped-qualified-no-game","qualified empty keeps original skip");
    selected=29;clockTick=15000;wait.normalHint();wait.waitOnce();
    expect(wait.pending&&wait.idleDue==clockTick,"selection notification wakes without a deadline");
    read();expect(clientReads==1&&gameReads==1&&game==29&&receipt.pid==29,"selection starts the original two reads");
    dropDuringClientRead=true;read();dropDuringClientRead=false;
    expect(clientReads==2&&gameReads==2&&!game&&!receipt.pid,"Steam read cannot bind a selection discarded during the call");
    clientAvailable=false;selected=41;read();
    expect(clientReads==3&&gameReads==2&&!game&&!receipt.pid,"unavailable Steam still prevents association");
    std::cout<<"unqualified_idle_windows=50 Steam_reads=0 selected_and_loss_paths=3\n";
}
}
static void hintWakeCases(){
    {Consumer x;clockTick=0;x.healthy();clockTick=200;
        const auto attempts=signalAttempts,h=hints;
        for(unsigned i=0;i<200;++i)x.windowHint();
        std::cout<<"window_burst notifications=200 SetEvent_calls="<<signalAttempts-attempts<<'\n';
        expect(signalAttempts==attempts+1,"a pending window burst sends one kernel signal");
        x.waitOnce();expect(hints==h+1&&!x.forceObserve,"burst delivers one quiet window hint");
        x.waitOnce();expect(lastTimeout==9800&&clockTick==10000,"coalescing adds no extra wait before the watchdog");}
    {Consumer x;clockTick=0;x.healthy();clockTick=200;
        const auto attempts=signalAttempts,h=hints;
        x.windowHint();
        afterKernelWait=[&]{x.normalHint();};
        x.waitOnce();expect(hints==h+1&&x.forceObserve&&x.observeDue(),"urgency upgrade after auto-reset still reaches the current exchange");
        expect(signalAttempts==attempts+1,"upgrade before exchange uses the outstanding wake");
        x.sample();x.normalHint();x.waitOnce();
        expect(signalAttempts==attempts+2&&hints==h+2&&x.forceObserve,"next hint after exchange sends a new wake");}
    {Consumer x;clockTick=0;const auto attempts=signalAttempts,h=hints;
        afterSignal=[&]{x.waitOnce();}; // Consumer exchanges before the producer records successful SetEvent.
        x.windowHint();x.normalHint();x.waitOnce();
        expect(signalAttempts==attempts+2&&hints==h+2&&x.forceObserve,"success recorded after exchange cannot suppress the next batch");}
    {Consumer x;clockTick=0;const auto attempts=signalAttempts,h=hints;
        signalFailures=2;x.windowHint();x.windowHint();
        expect(signalAttempts==attempts+2,"duplicate retries a failed wake");
        x.waitOnce();expect(lastTimeout==INFINITE&&!x.pending&&hints==h,"failed wakes leave idle consumer waiting");
        x.normalHint();x.windowHint();x.waitOnce();
        expect(signalAttempts==attempts+3&&hints==h+1&&x.pending&&x.forceObserve,"successful retry delivers retained hint and urgency");}
    {Consumer x;clockTick=0;x.healthy();clockTick=200;
        const auto attempts=signalAttempts,h=hints;unsigned jobs=0;
        x.windowHint();signalFailures=1;
        expect(!steamGameLinkSubmitObservation([&]{++jobs;}),"failed job wake does not borrow the hint wake");
        SteamGameLink::drainObservation(x.c);expect(jobs==0,"failed job signal removes its job");
        expect(steamGameLinkSubmitObservation([&]{++jobs;}),"job can use the same event with a pending hint");
        x.waitOnce();SteamGameLink::drainObservation(x.c);
        expect(signalAttempts==attempts+3&&hints==h+1&&jobs==1&&!x.forceObserve,"shared wake delivers both job and quiet hint");
        expect(steamGameLinkSubmitObservation([&]{++jobs;}),"mailbox alone submits after hints drain");
        x.waitOnce();SteamGameLink::drainObservation(x.c);
        expect(hints==h+1&&jobs==2&&!x.forceObserve,"ordinary mailbox wake does not invent a hint");
        x.normalHint();expect(steamGameLinkSubmitObservation([&]{++jobs;}),"job pending before stop");
        x.c->stop.store(true,std::memory_order_release);SetEvent(x.c->stopEvent);
        expect(!x.waitOnce()&&hints==h+1,"stop wins over the signalled shared event");
        SteamGameLink::drainObservation(x.c);expect(jobs==2,"stop does not let a pending job run");}
}
static void exitPriorityCases(){
    for(bool steam:{false,true}){
        Consumer x;clockTick=0;x.healthy();clockTick=200;
        x.windowHint();SetEvent(x.directory);SetEvent(steam?x.clientHandle.value:x.gameHandle.value);
        expect(x.waitOnce()&&x.forceObserve,"held exit outranks noisy window and directory handles");
        expect(x.c->hintsPending==waitpolicy::Window,"exit does not discard a queued hint");
        x.sample();expect(x.state.snapshot().reason==(steam?"steam-exited":"game-exited"),"held process exit seals the exact link");
        expect(x.pending&&x.idleDue==clockTick,"exit schedules one immediate successor discovery");
        x.waitOnce();expect(!x.c->hintsPending&&x.pending&&lastTimeout==0,"queued hint folds into successor discovery");
    }
    Consumer x;clockTick=0;x.healthy();SetEvent(x.gameHandle.value);x.normalHint();SetEvent(x.c->stopEvent);
    expect(!x.waitOnce()&&!x.forceObserve,"stop outranks process exits and hints");
}
static unsigned exitWaitCount(){
    Consumer x;clockTick=0;x.healthy();const auto before=predicatePolls;
    for(unsigned i=1;i<=200;++i){clockTick=i;x.windowHint();expect(x.waitOnce()&&!x.observeDue(),"quiet window wake leaves the observation deadline unchanged");}
    expect(x.state.snapshot().dueTick==10000,"healthy deadline survives quiet wakes");
    const auto polls=predicatePolls-before;
    std::cout<<"quiet_window_wakes=200 predicate_zero_waits="<<polls<<'\n';
    return polls;
}
static void kernelFallbackCase(){
    Consumer x;kernelWait=true;kernelClock=true;clockTick=::GetTickCount64();x.healthy();
    const auto started=clockTick,deadline=x.state.snapshot().dueTick;
    const auto attempts=signalAttempts,h=hints;
    signalFailures=1;x.normalHint();
    expect(x.c->hintsPending==waitpolicy::Immediate,"failed signal retains the pending notification");
    x.primaryValue=PrimaryStatus::Different;
    expect(x.waitOnce(),"watchdog wakes without a delivered notification");
    clockTick=::GetTickCount64();const auto degradedAt=clockTick;
    expect(clockTick>=deadline&&x.observeDue(),"real healthy deadline schedules a fresh read");
    expect(!x.forceObserve&&hints==h&&signalAttempts==attempts+1&&x.c->hintsPending==waitpolicy::Immediate,"no wake or hint was delivered before fallback");
    x.sample();expect(!x.state.linked()&&x.state.active()&&x.state.snapshot().reason=="native-primary-different","watchdog observes changed association");
    const auto retry=x.state.snapshot().dueTick;x.primaryValue=PrimaryStatus::Exact;
    expect(x.waitOnce(),"degraded link wakes for the existing retry");
    clockTick=::GetTickCount64();expect(clockTick>=retry&&x.observeDue(),"real degraded deadline schedules a fresh read");
    x.sample();expect(x.state.linked()&&hints==h,"successful retry restores the same link with no delivered hint");
    kernelWait=false;kernelClock=false;
    std::cout<<"healthy_wait_ms="<<degradedAt-started<<" retry_wait_ms="<<clockTick-degradedAt<<" delivered_hints=0\n";
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string_view(argv[1])=="--exit-wait-count"){
        exitWaitCount();return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--exit-priority-only"){
        exitPriorityCases();std::cout<<"PASS exit priority: "<<checks<<" checks\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--kernel-fallback-only"){
        kernelFallbackCase();std::cout<<"PASS kernel fallback: "<<checks<<" checks\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--hint-wake-only"){
        hintWakeCases();std::cout<<"PASS hint wake: "<<checks<<" checks\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--idle-discovery-only"){
        idleDiscoveryFixture::run();std::cout<<"PASS idle discovery: "<<checks<<" checks\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--idle-window-only"){
        idleWindowCases();std::cout<<"PASS idle window audits: "<<checks<<" checks\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--startup-only"){
        startupEnrollmentCases();std::cout<<"PASS startup enrollment: "<<checks<<" checks\n";return 0;
    }
    if(argc==2&&std::string_view(argv[1])=="--retirement-only"){
        linkRetirementCases();std::cout<<"PASS link retirement: "<<checks<<" checks\n";return 0;
    }
    {Consumer x;clockTick=0;x.healthy();expect(x.state.snapshot().dueTick==10000,"healthy due 10s");x.waitOnce();expect(lastTimeout==10000&&clockTick==10000,"actual wait consumer has one 10s wait");expect(x.observeDue(),"deadline runs");}
    for(bool steam:{false,true}){Consumer x;clockTick=123;x.healthy();SetEvent(steam?x.clientHandle.value:x.gameHandle.value);const auto before=::GetTickCount64();kernelWait=true;x.waitOnce();kernelWait=false;expect(::GetTickCount64()-before<1000,"signalled kernel handle bypasses 10s");expect(clockTick==123&&x.forceObserve&&x.observeDue(),"exit immediate before deadline");x.sample();expect(!x.state.active()&&x.state.snapshot().reason==(steam?"steam-exited":"game-exited"),"exit seals original");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints;SetEvent(x.c->pokeEvent);x.waitOnce();expect(hints==h&&!x.forceObserve&&!x.observeDue(),"mailbox poke alone cannot force a read");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints;x.normalHint();x.normalHint();x.waitOnce();expect(hints==h+1&&x.forceObserve&&x.observeDue(),"normal hints coalesce and force one read");x.sample();expect(!x.c->hintsPending&&!x.forceObserve&&!x.observeDue(),"normal hint does not replay");}
    {Consumer x;clockTick=0;x.healthy();unsigned reads=0;auto h=hints;
        for(unsigned i=1;i<=50;++i){clockTick=i*200;x.windowHint();x.waitOnce();if(x.observeDue()){x.sample();++reads;}}
        std::cout<<"window_workload observations="<<reads<<" recovery_hints="<<hints-h<<'\n';
        expect(reads==1&&hints==h+50,"healthy window churn uses only the watchdog while forwarding recovery hints");
        x.waitOnce();expect(lastTimeout==10000,"no extra polling after window notifications stop");}
    {Consumer x;clockTick=0;x.healthy();clockTick=200;x.windowHint();x.waitOnce();
        expect(!x.forceObserve&&!x.observeDue(),"one quiet window hint does not schedule a full read");
        x.waitOnce();expect(lastTimeout==9800&&clockTick==10000&&x.observeDue(),"single window hint leaves the watchdog deadline unchanged");}
    for(bool windowFirst:{false,true}){Consumer x;clockTick=0;x.healthy();clockTick=200;
        if(windowFirst){x.windowHint();x.normalHint();}else{x.normalHint();x.windowHint();}
        x.waitOnce();expect(x.forceObserve&&x.observeDue(),"queued urgent hint wins regardless of window hint order");}
    for(bool steam:{false,true}){Consumer x;clockTick=0;x.healthy();clockTick=200;
        x.windowHint();x.waitOnce();SetEvent(steam?x.clientHandle.value:x.gameHandle.value);
        x.waitOnce();expect(clockTick==200&&x.forceObserve&&x.observeDue(),"kernel exit stays immediate after a window hint");}
    {Consumer x;clockTick=0;x.healthy();clockTick=200;x.windowHint();x.waitOnce();
        x.gameValue="changed";SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();x.waitOnce();
        expect(clockTick==500&&x.observeDue(),"game log notification keeps the earlier deadline");}
    {Consumer x;clockTick=0;x.healthy();clockTick=200;x.windowHint();x.waitOnce();x.configEpoch=8;
        expect(x.observeDue(),"config epoch invalidates immediately after a window hint");}
    {Consumer x;clockTick=50;x.windowHint();x.waitOnce();expect(x.pending&&x.idleDue==50,"unbound window hint preserves immediate discovery");}
    {Consumer x;clockTick=0;x.healthy();x.primaryValue=PrimaryStatus::Different;x.sample();clockTick=200;
        x.windowHint();x.waitOnce();expect(x.state.active()&&!x.state.linked()&&x.forceObserve,"degraded window hint preserves immediate recovery read");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints;x.normalHint();x.enabled=false;x.waitOnce();expect(hints==h+1&&x.observeDue(),"poke hints and accelerates disable");x.sample();expect(x.state.snapshot().phase=="cancelled","disable immediate");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints,reads=stampReads;SetEvent(x.directory);x.waitOnce();expect(x.waitPolicy.directoryPending(),"directory event queued");expect(x.c->directoryNotices==1&&x.c->lastDirectoryTick==0,"observed directory signal records one timestamp");for(int i=0;i<1000;++i)x.waitPolicy.directoryNotified(i%100);x.waitOnce();expect(lastTimeout==100&&lastCount==4,"signalled directory excluded until fixed merge deadline");x.directoryCheck();expect(stampReads==reads+3&&hints==h&&!x.observeDue(),"unrelated log noise no hint or reparse");expect(!x.startupCapturePending&&x.c->directoryNotices==1,"directory noise does not queue startup capture");x.waitOnce();expect(lastTimeout==9900,"noise keeps remaining healthy deadline");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints;x.connectionValue="connection changed";SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();expect(hints==h+1&&!x.waitPolicy.readReady(clockTick)&&!x.observeDue(),"connection stamp hint only");}
    {Consumer x;clockTick=0;x.healthy();auto h=hints;x.gameValue="changed";SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();expect(hints==h+1&&!x.observeDue(),"gameprocess burst throttled");expect(x.startupCapturePending,"gameprocess change queues startup capture");x.waitOnce();expect(lastTimeout==400&&x.observeDue(),"gameprocess reads within 500ms");x.sample();expect(x.state.snapshot().dueTick==10500,"event read resets healthy due");}
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
        expect(!x.startupCapturePending,"console-only storm does not queue startup capture");
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
    {Consumer x;clockTick=0;auto h=hints;SetEvent(x.c->pokeEvent);x.waitOnce();expect(!x.pending&&hints==h,"idle mailbox poke alone cannot start discovery");x.normalHint();x.waitOnce();expect(x.pending&&x.idleDue==0&&hints==h+1,"idle normal hint discovers immediately");}
    {Consumer x;clockTick=0;x.healthy();SetEvent(x.c->stopEvent);expect(!x.waitOnce(),"stop immediate");}
    {Consumer x;clockTick=0;x.healthy();const auto reads=stampReads,h=hints;
        x.directory.changes=0;SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();
        expect(stampReads==reads&&hints==h&&!x.observeDue(),"irrelevant batch does no timestamp reads or downstream observation");
        x.directory.changes=logs::Connection;x.connectionValue="new connection";
        SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();
        expect(stampReads==reads+1&&hints==h+1&&!x.observeDue(),"connection batch reads one timestamp and still hints recovery");
        x.directory.changes=logs::Game|logs::Console;x.gameValue="new game";x.consoleValue="new console";
        SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();
        expect(stampReads==reads+3&&hints==h+2&&x.startupCapturePending,"mixed batch reads both changed log timestamps");}
    {Consumer x;clockTick=0;x.healthy();x.configEpoch=8;SetEvent(x.c->pokeEvent);x.waitOnce();expect(!x.forceObserve&&x.observeDue(),"epoch bypasses deadline without normal hint");x.sample();expect(x.state.snapshot().reason=="config-epoch-changed","epoch cancellation consumed immediately");}
    {Consumer x;clockTick=0;x.healthy();unsigned samples=0;for(unsigned i=0;i<20;++i){x.gameValue=std::to_string(i);SetEvent(x.directory);x.waitOnce();x.waitOnce();x.directoryCheck();if(x.observeDue()){x.sample();++samples;}}expect(clockTick==2000&&samples==4,"sustained burst bounded to one read per 500ms without starvation");}
    using namespace waitpolicy;expect(timeout(0,10000,true)==10000,"no fixed 250 or 1000 clamp");expect(timeout(42,41,true)==0,"overdue immediate");expect(timeout(0,maximum,true)==MAXDWORD-1,"finite overflow clamp");expect(timeout(0,0,false)==INFINITE,"idle infinite");expect(add(maximum-5,10)==maximum,"saturated addition");expect(timeout(maximum,maximum,true)==0,"saturated deadline ready");
    Policy p;p.directoryNotified(maximum-10);expect(!p.directoryReady(maximum-1)&&p.directoryReady(maximum),"directory saturation");p.observed(maximum-1);p.relevantChanged(maximum-1);expect(!p.readReady(maximum-1)&&p.readReady(maximum),"read saturation");
    Policy bg;bg.observed(maximum-1);bg.backgroundChanged(maximum-1);expect(!bg.readReady(maximum-1)&&bg.readReady(maximum),"background saturation");
    Policy late;late.observed(0);late.backgroundChanged(12000);expect(late.readReady(12000),"late background read is due immediately");
    Policy early;early.observed(0);early.backgroundChanged(100);early.backgroundChanged(9000);expect(early.nextDue(maximum)==10000,"repeated background changes cannot postpone fallback");
    linkRetirementCases();
    startupEnrollmentCases();
    idleWindowCases();
    idleDiscoveryFixture::run();
    hintWakeCases();
    exitPriorityCases();
    expect(exitWaitCount()==0,"quiet window wakes must not probe the held processes");
    std::cout<<"PASS production wait/directory/predicate consumer + policy: "<<checks<<" checks; hints="<<hints<<"; no real timed waits or target processes\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
