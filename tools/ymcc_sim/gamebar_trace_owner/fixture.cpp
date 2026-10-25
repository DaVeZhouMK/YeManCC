#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fixture {
using Clock = std::chrono::steady_clock;
static int checks = 0, failures = 0;
static void expect(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
struct Gate {
    std::mutex mx;
    std::condition_variable cv;
    bool entered = false, released = false;
    void block() {
        std::unique_lock<std::mutex> lock(mx);
        entered = true; cv.notify_all();
        if (!cv.wait_for(lock, std::chrono::seconds(5), [&] { return released; }))
            throw std::runtime_error("fixture gate timeout");
    }
    bool wait() {
        std::unique_lock<std::mutex> lock(mx);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; });
    }
    void release() { std::lock_guard<std::mutex> lock(mx); released = true; cv.notify_all(); }
    void mark() { std::lock_guard<std::mutex> lock(mx); entered = true; cv.notify_all(); }
};
template<class F> static bool until(F f) {
    const auto end = Clock::now() + std::chrono::seconds(3);
    do { if (f()) return true; std::this_thread::yield(); } while (Clock::now() < end);
    return f();
}
static std::atomic<ULONGLONG> tick{1000};
static std::mutex kernelMx;
static std::map<HANDLE, bool> handles; // true=thread, false=event; only fixture-created resources.
static HANDLE lastThread = nullptr;
static std::atomic<int> badWait{0}, badThreadClose{0}, badEventClose{0}, badCreateThread{0}, badCreateEvent{0};
static std::atomic<int> fallbackCleanup{0};
static std::atomic<bool> fastRetries{false};
static std::atomic<unsigned long long> retryWaitMs{0};
static std::atomic<Gate*> initializeGate{nullptr};
static std::atomic<int> closeCalls{0}, invalidSignals{0}, coInit{0}, coUninit{0};
static std::atomic<Gate*> signalGate{nullptr}, uninitGate{nullptr}, waitMark{nullptr};
static bool consume(std::atomic<int>& n) {
    int v=n.load(); while(v>0) { if(n.compare_exchange_weak(v,v-1)) return true; } return false;
}
static HANDLE WINAPI createEvent(LPSECURITY_ATTRIBUTES a, BOOL manual, BOOL initial, LPCWSTR name) {
    if(consume(badCreateEvent)) return nullptr;
    HANDLE h=::CreateEventW(a,manual,initial,name);
    if(h) { std::lock_guard<std::mutex> lock(kernelMx); handles[h]=false; }
    return h;
}
static HANDLE WINAPI createThread(LPSECURITY_ATTRIBUTES a,SIZE_T stack,LPTHREAD_START_ROUTINE fn,LPVOID arg,DWORD flags,LPDWORD id) {
    if(consume(badCreateThread)) return nullptr;
    HANDLE h=::CreateThread(a,stack,fn,arg,flags,id);
    if(h) { std::lock_guard<std::mutex> lock(kernelMx); handles[h]=true; lastThread=h; }
    return h;
}
static BOOL WINAPI setEvent(HANDLE h) {
    if(Gate* g=signalGate.exchange(nullptr)) g->block();
    { std::lock_guard<std::mutex> lock(kernelMx); if(!handles.count(h)) ++invalidSignals; }
    return ::SetEvent(h);
}
static DWORD WINAPI wait(HANDLE h,DWORD ms) {
    bool isThread=false;
    { std::lock_guard<std::mutex> lock(kernelMx); auto i=handles.find(h); isThread=i!=handles.end()&&i->second; }
    if(isThread) {
        if(Gate* g=waitMark.load()) g->mark();
        if(consume(badWait)) { ::SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    }
    if(!isThread && fastRetries.load()) { tick.fetch_add(ms); retryWaitMs.fetch_add(ms); return WAIT_TIMEOUT; }
    return ::WaitForSingleObject(h,ms);
}
static BOOL WINAPI close(HANDLE h) {
    ++closeCalls;
    bool isThread=false;
    { std::lock_guard<std::mutex> lock(kernelMx); auto i=handles.find(h); if(i==handles.end()) return FALSE; isThread=i->second; }
    if(consume(isThread?badThreadClose:badEventClose)) { ::SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    if(!::CloseHandle(h)) return FALSE;
    { std::lock_guard<std::mutex> lock(kernelMx); handles.erase(h); }
    return TRUE;
}
static ULONGLONG WINAPI now() { return tick.load(); }
static HRESULT WINAPI initialize(LPVOID,DWORD) {
    ++coInit; if(Gate* g=initializeGate.exchange(nullptr)) g->block(); return S_OK;
}
static void WINAPI uninitialize() {
    if(Gate* g=uninitGate.exchange(nullptr)) g->block();
    ++coUninit;
}
static std::atomic<int> logs{0};
static std::atomic<Gate*> logGate{nullptr};
struct LogAtom {
    std::string text;
    LogAtom(const char* v):text(v?v:""){}
    LogAtom(bool){} LogAtom(int){} LogAtom(unsigned long long){}
};
}
static std::atomic<bool> g_exitRequested{false};
static void appendNativeLifecycleLog(const char*,std::initializer_list<std::pair<const char*,fixture::LogAtom>>) {
    ++fixture::logs;
    if(fixture::Gate* g=fixture::logGate.exchange(nullptr)) g->block();
}

// Fake WinRT projection + retained activation factory + ABI HRESULT removers.
// All event delivery invokes the actual C++ lambdas installed by the product header.
namespace winrt {
struct event_token { long long value=0; };
namespace Windows::Foundation { struct IInspectable {}; }
}
namespace fixture {
using Inspectable=winrt::Windows::Foundation::IInspectable;
using Handler=std::function<void(const Inspectable&,const Inspectable&)>;
struct Factory {
    std::mutex mx;
    std::map<long long,Handler> visibility,redirected;
    std::vector<Handler> late;
    long long next=0; // Zero is deliberately a valid owned token.
    std::atomic<int> addV{0},addR{0},removeV{0},removeR{0},failAddV{0},failAddR{0},failRemoveV{0},failRemoveR{0};
    std::atomic<Gate*> addGate{nullptr}, removeGate{nullptr};
    winrt::event_token add(bool v, Handler h) {
        ++(v?addV:addR);
        if(consume(v?failAddV:failAddR)) throw std::runtime_error("fake add failed");
        winrt::event_token t;
        { std::lock_guard<std::mutex> lock(mx); t.value=next++; (v?visibility:redirected)[t.value]=h; late.push_back(h); }
        if(Gate* g=addGate.exchange(nullptr)) g->block();
        return t;
    }
    HRESULT remove(bool v,winrt::event_token t) noexcept {
        ++(v?removeV:removeR);
        try { if(Gate* g=removeGate.exchange(nullptr)) g->block(); } catch(...) {return E_FAIL;}
        if(consume(v?failRemoveV:failRemoveR)) return E_FAIL;
        std::lock_guard<std::mutex> lock(mx);
        auto& events=v?visibility:redirected;
        return events.erase(t.value)==1?S_OK:E_INVALIDARG;
    }
    HRESULT remove_VisibilityChanged(winrt::event_token t) noexcept { return remove(true,t); }
    HRESULT remove_IsInputRedirectedChanged(winrt::event_token t) noexcept { return remove(false,t); }
    size_t count(bool v) { std::lock_guard<std::mutex> lock(mx); return (v?visibility:redirected).size(); }
    Handler handler(bool v) { std::lock_guard<std::mutex> lock(mx); auto& map=v?visibility:redirected; return map.empty()?Handler{}:map.begin()->second; }
};
static auto api=std::make_shared<Factory>();
static std::atomic<int> factoryGets{0},visibleReads{0},redirectedReads{0},readFailures{0};
static std::atomic<bool> visible{true},redirected{false};
static std::atomic<Gate*> readGate{nullptr},factoryGate{nullptr};
static void invoke(const Handler& h) { if(h) h(Inspectable{},Inspectable{}); }
}
namespace winrt::Windows::Gaming::UI {
struct IGameBarStatics {
    std::shared_ptr<fixture::Factory> ptr;
    IGameBarStatics(std::nullptr_t=nullptr){}
    explicit IGameBarStatics(std::shared_ptr<fixture::Factory> p):ptr(std::move(p)){}
    explicit operator bool() const { return !!ptr; }
    IGameBarStatics& operator=(std::nullptr_t) { ptr.reset(); return *this; }
    template<class F> event_token VisibilityChanged(F f) const { return ptr->add(true,f); }
    template<class F> event_token IsInputRedirectedChanged(F f) const { return ptr->add(false,f); }
};
struct GameBar {
    static bool Visible() {
        ++fixture::visibleReads;
        if(fixture::Gate* g=fixture::readGate.exchange(nullptr)) g->block();
        if(fixture::consume(fixture::readFailures)) throw std::runtime_error("fake read failure");
        return fixture::visible.load();
    }
    static bool IsInputRedirected() { ++fixture::redirectedReads; return fixture::redirected.load(); }
};
}
namespace winrt {
template<class Class,class Interface> Interface get_activation_factory() {
    ++fixture::factoryGets;
    if(fixture::Gate* g=fixture::factoryGate.exchange(nullptr)) g->block();
    return Interface(fixture::api);
}
namespace impl { template<class T> using abi_t=fixture::Factory; }
static fixture::Factory* get_abi(const Windows::Gaming::UI::IGameBarStatics& p) { return p.ptr.get(); }
}
#define YMCC_GAMEBAR_TRACE_FAKE_WINRT
#define CreateEventW fixture::createEvent
#define CreateThread fixture::createThread
#define SetEvent fixture::setEvent
#define WaitForSingleObject fixture::wait
#define CloseHandle fixture::close
#define GetTickCount64 fixture::now
#define CoInitializeEx fixture::initialize
#define CoUninitialize fixture::uninitialize
#include "gamebar_trace_runtime.h"
#undef CreateEventW
#undef CreateThread
#undef SetEvent
#undef WaitForSingleObject
#undef CloseHandle
#undef GetTickCount64
#undef CoInitializeEx
#undef CoUninitialize

namespace fixture {
static bool finish() { return until([] {return gamebarTraceShutdown(0);}); }
static void ready() { expect(gamebarTraceInitAttempt(),"init succeeds"); }
static void joinNative() {
    expect(lastThread && ::WaitForSingleObject(lastThread,3000)==WAIT_OBJECT_0,"owned init thread really exited");
}
static std::vector<unsigned long long> snapshot() {
    return {g_gamebarTraceInitDone.load(),g_gamebarTraceAvailable.load(),
        static_cast<unsigned long long>(g_gamebarTraceVisible.load()),static_cast<unsigned long long>(g_gamebarTraceInputRedirected.load()),
        g_gamebarTraceObservedTick.load(),g_gamebarTraceVersion.load(),g_gamebarTraceLastAttemptTick.load(),
        static_cast<unsigned long long>(g_gamebarTraceInitAttempts.load()),g_gamebarTraceInitThreadStarted.load(),
        static_cast<unsigned long long>(logs.load()),static_cast<unsigned long long>(visibleReads.load()),static_cast<unsigned long long>(redirectedReads.load())};
}
static void run(const std::string& name) {
    expect(gamebarTraceStopped(),"initially resource-free");
    if(name=="normal") {
        gamebarTraceStartInitThread();
        expect(until([] {return g_gamebarTraceInitDone.load();}),"independent init ready");
        joinNative();
        expect(coInit==1 && coUninit==1,"COM scope balanced");
        expect(api->count(true)==1&&api->count(false)==1,"two events owned");
        expect(gamebar_trace_detail::visibilityToken.value==0,"zero token still owned");
        const auto ver=g_gamebarTraceVersion.load();
        visible=false; redirected=true; invoke(api->handler(true)); invoke(api->handler(false));
        expect(g_gamebarTraceVisible==0&&g_gamebarTraceInputRedirected==1,"actual delegates store both states");
        expect(g_gamebarTraceVersion==ver+2,"each callback increments version");
        readFailures=1; invoke(api->handler(true));
        expect(g_gamebarTraceVisible==-1&&g_gamebarTraceInputRedirected==-1,"callback read failure is Unknown");
        expect(gamebarTraceRefresh("probe"),"refresh succeeds");
        readFailures=1; expect(!gamebarTraceRefresh("probe-failed"),"refresh failure returns false");
        expect(g_gamebarTraceVisible==-1&&g_gamebarTraceInputRedirected==-1,"failed refresh is Unknown");
        expect(gamebarTraceRetryDelayMs(0)==2000&&gamebarTraceRetryDelayMs(1)==5000&&
            gamebarTraceRetryDelayMs(2)==15000&&gamebarTraceRetryDelayMs(3)==30000&&
            gamebarTraceRetryDelayMs(4)==60000&&gamebarTraceRetryDelayMs(99)==60000,"original retry table");
        expect(gamebarTraceShutdown(1000),"bounded normal cleanup");
    } else if(name=="partial"||name=="partial-stop") {
        api->failAddR=1;
        expect(!gamebarTraceInitAttempt(),"partial add fails");
        expect(gamebar_trace_detail::ownsVisibility&&!gamebar_trace_detail::ownsRedirected,"first token retained");
        expect(!g_gamebarTraceInitDone&&!g_gamebarTraceAvailable,"partial is not ready");
        expect(!gamebarTraceStopped(),"partial subscription owns resources");
        if(name=="partial-stop") {
            expect(gamebarTraceShutdown(0),"partial subscription directly shuts down");
            expect(api->removeV==1&&api->removeR==0,"only successfully acquired partial token revoked");
        } else {
        gamebarTraceEnsureInit(); expect(g_gamebarTraceInitAttempts==1,"scheduled retry throttled");
        tick.fetch_add(5000); gamebarTraceEnsureInit();
        expect(g_gamebarTraceInitDone&&api->addV==1&&api->addR==2,"retry fills only missing subscription");
        expect(factoryGets==1,"same factory retained through retry");
        }
    } else if(name=="worker-six-retries") {
        Gate gate; initializeGate=&gate; fastRetries=true; api->failAddR=100;
        gamebarTraceStartInitThread(); expect(gate.wait(),"real worker entered init"); gate.release(); joinNative();
        expect(g_gamebarTraceInitAttempts==6&&!g_gamebarTraceInitDone,"worker exhausts exactly six attempts");
        expect(api->addV==1&&api->addR==6,"worker retries do not stack partial subscription");
        expect(retryWaitMs==172000,"original 2/5/15/30/60/60 second retry waits consumed virtually");
        expect(!gamebarTraceStopped(),"exhausted worker still owns handle/event/token");
        fastRetries=false;
    } else if(name=="first-subscribe-failure") {
        api->failAddV=1; expect(!gamebarTraceInitAttempt(),"first subscription failure");
        expect(!gamebar_trace_detail::ownsVisibility&&!gamebar_trace_detail::ownsRedirected,"no invented event ownership");
        expect(!gamebarTraceStopped(),"activation factory still owned");
        expect(gamebarTraceShutdown(0),"failed first add factory released");
    } else if(name=="retry-budget") {
        api->failAddR=100;
        for(int i=0;i<6;++i) expect(!gamebarTraceInitAttempt(),"six initial attempts fail");
        expect(api->addV==1&&api->addR==6,"partial retry never stacks first event");
        expect(!gamebarTraceInitAttempt(),"initial six attempt cap");
        gamebarTraceEnsureInit(); gamebarTraceEnsureInitIfDue();
        expect(g_gamebarTraceInitAttempts==6,"no early demand retry");
        tick.fetch_add(59999); gamebarTraceEnsureInitIfDue(); expect(g_gamebarTraceInitAttempts==6,"59999ms not due");
        tick.fetch_add(1); gamebarTraceEnsureInitIfDue(); expect(g_gamebarTraceInitAttempts==7,"60000ms demand attempt");
        api->failAddR=0; tick.fetch_add(60000); gamebarTraceEnsureInitIfDue();
        expect(g_gamebarTraceInitDone&&api->addV==1&&api->count(true)==1&&api->count(false)==1,"demand heals without stacking");
    } else if(name=="concurrent-init") {
        Gate gate; api->addGate=&gate;
        std::thread init([] { (void)gamebarTraceInitAttempt(); });
        expect(gate.wait(),"first add blocked");
        tick.fetch_add(100000);
        gamebarTraceEnsureInit(); gamebarTraceEnsureInitIfDue(); gamebarTraceStartInitThread();
        expect(!gamebarTraceInitAttempt(),"busy init returns false");
        expect(!gamebarTraceStopped(),"query busy returns false");
        gate.release(); init.join();
        expect(api->addV==1&&api->addR==1&&g_gamebarTraceInitAttempts==1,"concurrent entries serialize subscriptions");
    } else if(name=="stop-during-init"||name=="stop-during-factory") {
        Gate gate;
        if(name=="stop-during-init") api->addGate=&gate; else factoryGate=&gate;
        gamebarTraceStartInitThread(); expect(gate.wait(),"init WinRT call blocked");
        const auto start=Clock::now();
        gamebarTraceRequestStop();
        expect(!gamebarTraceShutdown(20),"blocked init remains Unknown");
        expect(!gamebarTraceStopped(),"blocked init not stopped");
        expect(Clock::now()-start<std::chrono::milliseconds(500),"stop and busy query never wait for WinRT lock");
        const auto snap=snapshot(); gate.release(); joinNative();
        expect(snapshot()==snap,"return after cancellation publishes no state or log");
        expect(api->addR==0,"stop prevents remaining subscription");
        if(name=="stop-during-init") expect(gamebar_trace_detail::ownsVisibility,"token returned after stop retained for revoke");
        expect(finish(),"late init completion safely cleaned");
    } else if(name=="revoke-failure"||name=="revoke-both-fail"||name=="revoke-redirect-failure") {
        ready();
        const bool failV=name!="revoke-redirect-failure",failR=name!="revoke-failure";
        api->failRemoveV=failV?1:0; api->failRemoveR=failR?1:0;
        expect(!gamebarTraceShutdown(0),"failed ABI remove is false");
        expect(gamebar_trace_detail::ownsVisibility==failV&&gamebar_trace_detail::ownsRedirected==failR,"only failed tokens retained");
        expect(static_cast<bool>(gamebar_trace_detail::factory),"factory retained for retry");
        expect(!gamebarTraceStopped()&&(api->count(true)+api->count(false)>0),"failed subscription never called stopped");
        const auto snap=snapshot(); invoke(api->handler(true)); invoke(api->handler(false)); expect(snapshot()==snap,"retained late delegate has no effect");
        expect(gamebarTraceShutdown(0),"second removal succeeds");
        expect(api->removeV==(failV?2:1)&&api->removeR==(failR?2:1),"successful token not revoked twice");
    } else if(name=="inflight-callback"||name=="inflight-refresh"||name=="inflight-log") {
        ready(); Gate gate;
        if(name=="inflight-log") logGate=&gate; else readGate=&gate;
        const Handler h=api->handler(true);
        std::thread callbackThread([&] {if(name=="inflight-refresh") (void)gamebarTraceRefresh("inflight"); else invoke(h);});
        expect(gate.wait(),"actual callback/refresh entered blocked body");
        expect(gamebar_trace_detail::active==1,"real activity counted");
        gamebarTraceRequestStop(); const auto snap=snapshot(); const auto begin=Clock::now();
        expect(!gamebarTraceShutdown(20),"activity not drained returns false");
        expect(Clock::now()-begin<std::chrono::milliseconds(500),"callback drain respects wait budget");
        expect(!gamebarTraceStopped(),"inflight never stopped after revoke");
        expect(!gamebar_trace_detail::ownsVisibility&&!gamebar_trace_detail::ownsRedirected,"tokens removed while callback still inflight");
        expect(static_cast<bool>(gamebar_trace_detail::factory),"factory retained until drain");
        gate.release(); callbackThread.join();
        expect(snapshot()==snap,"callback returning after stop adds no writes or log");
        expect(gamebar_trace_detail::active==0,"actual callback exited");
        expect(finish(),"inflight then clean");
    } else if(name=="late-callback") {
        ready(); const auto late=api->late;
        expect(gamebarTraceShutdown(0),"subscriptions revoked");
        const auto snap=snapshot();
        for(const Handler& h:late) invoke(h);
        gamebarTraceEnsureInit(); gamebarTraceEnsureInitIfDue(); gamebarTraceStartInitThread();
        expect(!gamebarTraceInitAttempt()&&!gamebarTraceRefresh("late"),"closed admission rejected");
        expect(snapshot()==snap,"late delegates and entry points cannot write or reopen");
    } else if(name=="wait-failure"||name=="thread-close-failure"||name=="event-close-failure") {
        gamebarTraceStartInitThread(); expect(until([] {return g_gamebarTraceInitDone.load();}),"init ready"); joinNative();
        HANDLE originalThread=gamebar_trace_detail::thread,originalWake=gamebar_trace_detail::wake;
        if(name=="wait-failure") badWait=1;
        else if(name=="thread-close-failure") badThreadClose=1;
        else badEventClose=1;
        expect(!gamebarTraceShutdown(10),"injected kernel failure returns false");
        expect(!gamebarTraceStopped(),"kernel failure retains ownership");
        if(name!="event-close-failure") expect(gamebar_trace_detail::thread==originalThread,"exact thread handle retained");
        else expect(!gamebar_trace_detail::thread,"thread really closed before event failure");
        expect(gamebar_trace_detail::wake==originalWake,"event handle retained");
        gamebarTraceRequestStop(); expect(gamebarTraceShutdown(1000),"subsequent kernel cleanup succeeds");
    } else if(name=="wait-timeout"||name=="join-without-resource-lock") {
        Gate gate,waiting; uninitGate=&gate;
        gamebarTraceStartInitThread(); expect(gate.wait(),"worker blocked during COM uninit");
        if(name=="wait-timeout") {
            expect(!gamebarTraceShutdown(20),"real kernel thread timeout remains owned");
            expect(!gamebarTraceStopped()&&gamebar_trace_detail::thread,"live init thread not stopped");
            gate.release(); joinNative();
        } else {
            waitMark=&waiting; std::atomic<bool> gotLock{false};
            std::thread release([&] {
                (void)waiting.wait();
                std::unique_lock<std::mutex> lock(gamebar_trace_detail::resourceMx,std::try_to_lock);
                gotLock=lock.owns_lock(); gate.release();
            });
            expect(gamebarTraceShutdown(1000),"joining worker makes progress");
            release.join(); waitMark=nullptr;
            expect(gotLock,"resource mutex released during kernel thread wait");
        }
    } else if(name=="wake-borrow") {
        gamebarTraceStartInitThread(); expect(until([] {return g_gamebarTraceInitDone.load();}),"init ready"); joinNative();
        Gate gate; signalGate=&gate;
        std::thread stopper([] { gamebarTraceRequestStop(); });
        expect(gate.wait(),"stop signal owns event borrow");
        expect(!gamebarTraceShutdown(0),"event borrow prevents close");
        expect(gamebar_trace_detail::wake&&!gamebarTraceStopped(),"borrowed event remains owned");
        gate.release(); stopper.join();
        expect(finish(),"event closed after signal returns");
        expect(invalidSignals==0,"no SetEvent on closed handle");
    } else if(name=="create-failures") {
        badCreateEvent=1; gamebarTraceStartInitThread(); expect(gamebarTraceStopped(),"failed event creation owns nothing");
        badCreateThread=1; gamebarTraceStartInitThread();
        expect(!gamebarTraceStopped()&&gamebar_trace_detail::wake&&!gamebar_trace_detail::thread,"failed thread creation retains event");
        gamebarTraceStartInitThread(); expect(until([] {return g_gamebarTraceInitDone.load();}),"thread start retried"); joinNative();
    } else if(name=="blocked-remove") {
        ready(); Gate gate; api->removeGate=&gate;
        std::atomic<bool> done{false};
        std::thread shutdown([&] {done=gamebarTraceShutdown(0);});
        expect(gate.wait(),"ABI remove deliberately blocked");
        expect(!done&&!gamebarTraceStopped(),"waitMs zero cannot bound a blocked remover");
        expect(!gamebarTraceShutdown(0),"concurrent cleanup returns busy false");
        const auto snap=snapshot(); for(const Handler& h:api->late) invoke(h);
        expect(snapshot()==snap,"late events inert during blocked remove");
        gate.release(); shutdown.join(); expect(done,"removal completes only after explicit unblock");
    } else expect(false,"known case required");
    expect(finish(),"final explicit cleanup completes");
    expect(gamebarTraceShutdown(0)&&gamebarTraceStopped(),"idempotent cleanup and accurate stopped");
    expect(api->count(true)==0&&api->count(false)==0,"no fake subscriptions remain before process exit");
    expect(!gamebar_trace_detail::factory&&gamebar_trace_detail::active==0,"factory and callbacks drained");
    { std::lock_guard<std::mutex> lock(kernelMx); expect(handles.empty(),"actual owned kernel handles closed before exit"); }
    expect(invalidSignals==0,"no invalid event signals");
    // Corruption controls may deliberately erase a product handle. Record the
    // failure above, then explicitly recover ONLY fixture-created resources.
    // This fallback cannot count as a passing production cleanup observation.
    std::vector<std::pair<HANDLE,bool>> leftovers;
    { std::lock_guard<std::mutex> lock(kernelMx); for(const auto& item:handles) leftovers.push_back(item); }
    for(const auto& item:leftovers) {
        ++fallbackCleanup;
        if(item.second) expect(::WaitForSingleObject(item.first,3000)==WAIT_OBJECT_0,"negative control leftover thread really finished");
        expect(close(item.first)!=FALSE,"negative control explicit fixture handle recovery");
    }
}
}
int main(int argc,char** argv) {
    const std::string name=argc==2?argv[1]:"normal";
    try {fixture::run(name);} catch(const std::exception& e) {++fixture::failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"{\"case\":\""<<name<<"\",\"checks\":"<<fixture::checks<<",\"failures\":"<<fixture::failures
        <<",\"remainingKernelHandles\":"<<fixture::handles.size()<<",\"remainingSubscriptions\":"
        <<fixture::api->count(true)+fixture::api->count(false)<<",\"active\":"<<gamebar_trace_detail::active.load()<<",\"fallbackCleanup\":"<<fixture::fallbackCleanup.load()<<"}\n";
    return fixture::failures?1:0;
}
