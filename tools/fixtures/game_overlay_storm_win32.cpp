// The production Carrier is compiled unchanged. Only named Win32 calls are
// observed/faulted; successful calls still reach owned off-screen test HWNDs.
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <json.hpp>

namespace fixture {
struct Hook {HWINEVENTHOOK handle;WINEVENTPROC callback;DWORD event;bool live;};
inline std::vector<Hook> hooks;
inline unsigned hookAttempts=0,failHookAt=0,posts=0,postFailures=0,timerFailures=0;
inline unsigned layouts=0,submits=0,opened=0,closed=0;
inline std::string lastClosedReason;
inline std::function<BOOL(HWND,UINT,WPARAM,LPARAM)> postOverride;
inline TIMERPROC timerCallback=nullptr;inline HWND timerWindow=nullptr;inline UINT_PTR timerId=0;
inline HWINEVENTHOOK hook(DWORD first,DWORD last,HMODULE module,WINEVENTPROC callback,DWORD pid,DWORD thread,DWORD flags){
    if(failHookAt&&++hookAttempts==failHookAt){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    const auto h=::SetWinEventHook(first,last,module,callback,pid,thread,flags);
    if(h)hooks.push_back({h,callback,first,true});return h;
}
inline BOOL unhook(HWINEVENTHOOK h){const BOOL ok=::UnhookWinEvent(h);if(ok)for(auto& item:hooks)if(item.handle==h&&item.live)item.live=false;return ok;}
inline BOOL post(HWND h,UINT m,WPARAM w,LPARAM l){++posts;if(postOverride){const auto override=postOverride;return override(h,m,w,l);}if(postFailures){--postFailures;SetLastError(ERROR_NOT_ENOUGH_QUOTA);return FALSE;}return ::PostMessageW(h,m,w,l);}
inline UINT_PTR timer(HWND h,UINT_PTR id,UINT ms,TIMERPROC cb){if(timerFailures){--timerFailures;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}const auto result=::SetTimer(h,id,ms,cb);if(result){timerCallback=cb;timerWindow=h;timerId=result;}return result;}
inline BOOL kill(HWND h,UINT_PTR id){return ::KillTimer(h,id);}
inline unsigned liveHooks(){return static_cast<unsigned>(std::count_if(hooks.begin(),hooks.end(),[](const auto& h){return h.live;}));}
}
#define SetWinEventHook fixture::hook
#define UnhookWinEvent fixture::unhook
#define PostMessageW fixture::post
#define SetTimer fixture::timer
#define KillTimer fixture::kill
#include "game_overlay_presentation.h"
#undef SetWinEventHook
#undef UnhookWinEvent
#undef PostMessageW
#undef SetTimer
#undef KillTimer

using nlohmann::json;
using ymcc::gameoverlay::Carrier;
using ymcc::gameoverlay::Binding;
static Carrier* activeCarrier=nullptr;
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    LRESULT result=0;
    if(activeCarrier&&activeCarrier->protectInput(h,m,w,l,result))return result;
    if(activeCarrier&&activeCarrier->handle(h,m,w,l))return 0;
    return DefWindowProcW(h,m,w,l);
}
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct OwnedWindow {
    HWND h=nullptr;
    OwnedWindow(){
        WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"YMCC_Storm_OwnedOffscreen";
        if(!RegisterClassW(&c)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("class");
        h=CreateWindowExW(WS_EX_TOOLWINDOW,c.lpszClassName,L"",WS_POPUP,-30000,-30000,2,2,nullptr,nullptr,c.hInstance,nullptr);
        require(h!=nullptr,"owned-window-create");
    }
    ~OwnedWindow(){if(h&&IsWindow(h))DestroyWindow(h);}
};
static Binding binding(){Binding b;b.globalProbe=true;b.originalUiInput=true;b.power=7;b.sharedPower.generation=7;b.sharedPower.flags=ymcc::sleeppower::kKnown;return b;}
static Carrier::Ports ports(HWND h,std::function<bool()> current={}){
    return {
        [current](const Binding&){return current?current():true;},
        [h]{++fixture::layouts;return SetWindowPos(h,nullptr,-30000,-30000,2,2,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;},
        []{++fixture::submits;return true;},
        [](bool on,const char* reason){if(on)++fixture::opened;else {++fixture::closed;fixture::lastClosedReason=reason;}},
        {},[]{return true;},{}
    };
}
static unsigned drain(HWND h,unsigned max=64){
    MSG m{};unsigned count=0;
    while(count<max&&PeekMessageW(&m,h,Carrier::message,Carrier::message,PM_REMOVE)){DispatchMessageW(&m);++count;}
    return count;
}
static void run(const std::string& name,json& facts){
    OwnedWindow window;Carrier carrier;activeCarrier=&carrier;
    struct Reset{~Reset(){activeCarrier=nullptr;for(auto& h:fixture::hooks)if(h.live){::UnhookWinEvent(h.handle);h.live=false;}}}reset;
    facts={{"realOwnedOffscreenWindows",true},{"realWin32Hooks",true},{"inputInjected",false}};
    if(name=="begin-reentry"){
        bool first=true,nested=false;
        const bool begun=carrier.begin(window.h,binding(),ports(window.h,[&]{if(first){first=false;nested=carrier.begin(window.h,binding(),ports(window.h));}return true;}));
        facts["outerAccepted"]=begun;facts["nestedAccepted"]=nested;facts["liveHooksBeforeClose"]=fixture::liveHooks();
        carrier.end("fixture-close");facts["hooksAfterClose"]=fixture::liveHooks();
        require(begun&&!nested,"nested-begin-must-not-replace-admitting-owner");
        require(fixture::liveHooks()==0,"all-admitted-hooks-returned");
    }else if(name=="admission-retry"){
        require(!carrier.begin(window.h,binding(),ports(window.h,[]{return false;})),"validation-refuses");
        require(!carrier.begin(window.h,binding(),ports(window.h,[]()->bool{throw std::runtime_error("validation-fault");})),"validation-exception-refuses");
        require(carrier.begin(window.h,binding(),ports(window.h)),"reservation-returned-after-refusal-and-exception");
        carrier.end("fixture-close");
    }else if(name=="begin-verify-retired"){
        bool once=false,successor=false;unsigned successorLayouts=0;
        auto p=ports(window.h);auto b=binding();b.detachedPointer=true;
        p.verifyDisplay=[&]{if(!once){once=true;carrier.end("begin-verify-retired");successor=carrier.begin(window.h,binding(),ports(window.h));successorLayouts=fixture::layouts;}return true;};
        const bool begun=carrier.begin(window.h,b,std::move(p));
        facts["oldBeginAccepted"]=begun;facts["successorAccepted"]=successor;facts["successorExtraLayouts"]=fixture::layouts-successorLayouts;
        require(!begun&&successor&&carrier.active()&&fixture::layouts==successorLayouts,"old-begin-verification-cannot-mutate-or-end-successor");carrier.end("fixture-close");
    }else if(name=="begin-retired-exception"){
        auto p=ports(window.h);bool successor=false;
        p.changed=[&](bool on,const char*){if(on){carrier.end("changed-retired");successor=carrier.begin(window.h,binding(),ports(window.h));throw std::runtime_error("old-changed-fault");}};
        const auto result=carrier.begin(window.h,binding(),std::move(p));
        facts["oldBeginAccepted"]=result;facts["successorAccepted"]=successor;
        require(!result&&successor&&carrier.active(),"old-begin-exception-cannot-close-successor");carrier.end("fixture-close");
    }else if(name.rfind("retire-",0)==0){
        const auto phase=name.substr(7);bool replace=false,once=false,successor=false;
        auto retire=[&]()->bool{if(!replace||once)return true;once=true;carrier.end("callback-retired");successor=carrier.begin(window.h,binding(),ports(window.h));if(phase.find("throws")!=std::string::npos)throw std::runtime_error("old-callback-fault");return false;};
        auto p=ports(window.h);auto b=binding();
        if(phase.rfind("current",0)==0)p.current=[&](const Binding&){return retire();};
        else if(phase.rfind("verify",0)==0){b.detachedPointer=true;p.verifyDisplay=[&]{return ymcc::hudinput::ensure(window.h)&&retire();};}
        else if(phase.rfind("layout",0)==0)p.layout=retire;
        else if(phase.rfind("submit",0)==0)p.submit=retire;
        else if(phase.rfind("order",0)==0){b.detachedPointer=true;p.verifyDisplay=[&]{return ymcc::hudinput::ensure(window.h);};p.orderInput=retire;}
        else throw std::runtime_error("unknown-phase");
        const bool begun=carrier.begin(window.h,b,std::move(p));facts["beginCloseReason"]=fixture::lastClosedReason;
        require(begun,"begin");drain(window.h);replace=true;
        if(phase.rfind("order",0)==0){fixture::hooks.front().callback(fixture::hooks.front().handle,EVENT_SYSTEM_FOREGROUND,window.h,OBJID_WINDOW,CHILDID_SELF,GetCurrentThreadId()+1,0);}
        else carrier.displayChanged();
        drain(window.h);require(once&&successor&&carrier.active(),"retired-callback-cannot-stop-successor");
        facts["successorAccepted"]=successor;carrier.end("fixture-close");
    }else if(name=="post-retired-failure"){
        require(carrier.begin(window.h,binding(),ports(window.h)),"begin");drain(window.h);fixture::posts=0;
        fixture::postOverride=[&](HWND,UINT,WPARAM,LPARAM)->BOOL{
            fixture::postOverride={};carrier.end("post-retired");require(carrier.begin(window.h,binding(),ports(window.h)),"successor-begin");carrier.displayChanged();return FALSE;
        };
        carrier.requestCheck();const auto offered=fixture::posts;carrier.requestCheck();
        facts["postsBeforeDuplicate"]=offered;facts["postsAfterDuplicate"]=fixture::posts;
        require(fixture::posts==offered,"old-post-failure-cannot-clear-successor-queue");require(drain(window.h)==1,"one-successor-check");carrier.end("fixture-close");
    }else if(name=="timer-budget"){
        require(carrier.begin(window.h,binding(),ports(window.h)),"begin");
        for(unsigned i=0;i<3;++i){require(carrier.ownsTimer(),"bounded-timer-armed");const auto callback=fixture::timerCallback;callback(fixture::timerWindow,WM_TIMER,fixture::timerId,0);}
        require(carrier.repairCount()==3&&!carrier.ownsTimer(),"maintenance-stops-after-three-passes");
        facts["repairPasses"]=carrier.repairCount();const auto before=fixture::layouts;
        for(unsigned i=0;i<10000;++i)carrier.requestCheck();drain(window.h);
        require(fixture::layouts==before&&!carrier.ownsTimer(),"idle-event-storm-does-not-layout-or-rearm");carrier.end("fixture-close");
    }else if(name=="thread-storm"){
        require(carrier.begin(window.h,binding(),ports(window.h)),"begin");drain(window.h);fixture::posts=0;
        std::vector<std::thread> threads;for(unsigned i=0;i<8;++i)threads.emplace_back([&]{for(unsigned j=0;j<25000;++j){carrier.requestCheck();carrier.displayChanged();}});
        for(auto& thread:threads)thread.join();require(fixture::posts==1&&drain(window.h)==1,"concurrent-producers-coalesce");
        facts["threads"]=8;facts["offeredRequests"]=400000;facts["posts"]=fixture::posts;carrier.end("fixture-close");
    }else if(name=="stale-hook"){
        require(carrier.begin(window.h,binding(),ports(window.h)),"first-begin");
        const auto old=fixture::hooks.front();carrier.end("old-close");
        require(carrier.begin(window.h,binding(),ports(window.h)),"second-begin");drain(window.h);fixture::posts=0;
        old.callback(old.handle,EVENT_SYSTEM_FOREGROUND,window.h,OBJID_WINDOW,CHILDID_SELF,GetCurrentThreadId()+1,0);
        facts["staleCallbackPosts"]=fixture::posts;
        require(fixture::posts==0,"retired-hook-must-not-repair-successor");carrier.end("fixture-close");
    }else if(name=="context-reentry"){
        bool replace=false,replaced=false,replacementAccepted=false;unsigned afterReplacementLayout=0;
        require(carrier.begin(window.h,binding(),ports(window.h,[&]{
            if(replace&&!replaced){replaced=true;carrier.end("context-retired");replacementAccepted=carrier.begin(window.h,binding(),ports(window.h));afterReplacementLayout=fixture::layouts;}
            return true;
        })),"first-begin");
        drain(window.h);replace=true;carrier.displayChanged();drain(window.h);
        facts["replacementAccepted"]=replacementAccepted;facts["lateSuccessorLayouts"]=fixture::layouts-afterReplacementLayout;
        require(replacementAccepted,"valid-explicit-successor-may-start-after-end");
        require(fixture::layouts==afterReplacementLayout,"retired-context-check-must-not-layout-successor");carrier.end("fixture-close");
    }else if(name=="peer-event-storm"){
        OwnedWindow peer;ShowWindow(peer.h,SW_SHOWNOACTIVATE);
        unsigned scans=0;auto p=ports(window.h);
        p.ownPeerOccluded=[&]{++scans;struct Query{HWND own,peer;bool occluded=false;} query{window.h,peer.h};
            EnumWindows([](HWND h,LPARAM value)->BOOL{auto& q=*reinterpret_cast<Query*>(value);if(h==q.own)return FALSE;if(h==q.peer&&IsWindowVisible(h)){RECT a{},b{},r{};q.occluded=GetWindowRect(q.own,&a)&&GetWindowRect(h,&b)&&IntersectRect(&r,&a,&b);}return TRUE;},reinterpret_cast<LPARAM>(&query));return query.occluded;
        };
        require(carrier.begin(window.h,binding(),std::move(p)),"peer-burst-begin");drain(window.h);
        const auto hook=fixture::hooks.back();json samples=json::array();unsigned maxScans=0;
        for(unsigned repeat=0;repeat<7;++repeat){
            const auto before=scans;FILETIME c{},e{},k0{},u0{},k1{},u1{};LARGE_INTEGER start{},finish{},frequency{};
            GetThreadTimes(GetCurrentThread(),&c,&e,&k0,&u0);QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
            for(unsigned i=0;i<5000;++i)hook.callback(hook.handle,EVENT_OBJECT_REORDER,peer.h,OBJID_WINDOW,CHILDID_SELF,GetCurrentThreadId(),0);
            drain(window.h);QueryPerformanceCounter(&finish);GetThreadTimes(GetCurrentThread(),&c,&e,&k1,&u1);
            const auto ticks=[](FILETIME t){return (static_cast<ULONGLONG>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
            const auto used=scans-before;maxScans=std::max(maxScans,used);
            samples.push_back({{"offeredPeerCallbacks",5000},{"occlusionScans",used},{"threadCpu100ns",ticks(k1)+ticks(u1)-ticks(k0)-ticks(u0)},{"wallMs",1000.0*(finish.QuadPart-start.QuadPart)/frequency.QuadPart}});
        }
        facts["samples"]=samples;facts["offeredPeerCallbacks"]=35000;facts["maxScansPerBurst"]=maxScans;
        facts["clockScope"]="real current-thread kernel+user FILETIME deltas; zero means below accounting granularity, not zero CPU";
        carrier.end("fixture-close");require(maxScans<=1,"peer-event-burst-must-coalesce-expensive-occlusion-check");
    }else if(name=="message-storm"){
        require(carrier.begin(window.h,binding(),ports(window.h)),"begin");drain(window.h);fixture::posts=0;
        const auto before=fixture::layouts;
        for(unsigned i=0;i<100000;++i){carrier.requestCheck();carrier.displayChanged();}
        facts["offeredRequests"]=200000;facts["postedChecks"]=fixture::posts;
        require(fixture::posts==1,"burst-is-one-queued-check");
        const auto delivered=drain(window.h);facts["deliveredChecks"]=delivered;facts["layoutCalls"]=fixture::layouts-before;
        require(delivered==1&&fixture::layouts-before==1,"one-burst-one-layout");carrier.end("fixture-close");
    }else if(name=="post-failure"){
        require(carrier.begin(window.h,binding(),ports(window.h)),"begin");drain(window.h);fixture::posts=0;fixture::postFailures=1;
        carrier.displayChanged();carrier.requestCheck();facts["postAttempts"]=fixture::posts;
        require(fixture::posts==2&&drain(window.h)==1,"failed-post-cannot-latch-the-queue");carrier.end("fixture-close");
    }else if(name=="hook-failure"){
        for(unsigned index=1;index<=5;++index){fixture::hookAttempts=0;fixture::failHookAt=index;
            require(!carrier.begin(window.h,binding(),ports(window.h)),"partial-hook-failure-refuses-begin");
            require(!carrier.active()&&fixture::liveHooks()==0&&!carrier.ownsTimer(),"partial-hooks-returned");
            fixture::failHookAt=0;require(carrier.begin(window.h,binding(),ports(window.h)),"next-explicit-begin-self-recovers");carrier.end("fixture-close");
        }facts["hookFailureBoundaries"]=5;
    }else if(name=="timer-failure"){
        fixture::timerFailures=1;require(!carrier.begin(window.h,binding(),ports(window.h)),"timer-failure-cannot-claim-active");
        require(!carrier.active()&&fixture::liveHooks()==0,"timer-failure-cleans-hooks");
        require(carrier.begin(window.h,binding(),ports(window.h)),"timer-fault-next-begin-recovers");carrier.end("fixture-close");
    }else if(name=="revoke-session-scope"){
        require(carrier.begin(window.h,binding(),ports(window.h)),"start old session");drain(window.h);
        const auto past=carrier.snapshot();
        require(carrier.inputReady(*past),"old session admitted while current");
        carrier.end("fixture-replace");
        require(carrier.begin(window.h,binding(),ports(window.h)),"start successor session");drain(window.h);
        const auto current=carrier.snapshot();
        require(current->session>past->session&&carrier.inputReady(*current),"fresh session admitted");
        const auto before=fixture::posts;
        std::thread late([&]{carrier.revoke(past);});late.join();
        drain(window.h);
        require(!carrier.inputReady(*past)&&carrier.snapshot()==current&&carrier.inputReady(*current)&&fixture::posts==before,
            "stale admission and revocation cannot cross into the replacement");
        fixture::postFailures=1;
        carrier.revoke(current);
        require(!carrier.inputReady(*current)&&carrier.snapshot()==current,"real revoke denies before teardown");
        const auto retry=fixture::posts;
        carrier.revoke(past);
        require(fixture::posts==retry&&!carrier.inputReady(*current),"old revoke cannot erase or re-post the new loss");
        for(unsigned i=0;i<512;++i)carrier.revoke(current);
        require(fixture::posts==retry+1&&drain(window.h)==1&&!carrier.active()&&fixture::lastClosedReason=="input-admission-revoked",
            "current revoke can retry a failed post and closes only once");
        facts["currentRevokeRetryPosts"]=fixture::posts-retry;
    }else if(name=="close-storm"){
        for(unsigned i=0;i<128;++i){
            require(carrier.begin(window.h,binding(),ports(window.h)),"cycle-begin");
            const auto session=carrier.snapshot()->session;
            for(unsigned j=0;j<64;++j)carrier.requestCheck();
            carrier.end("storm-close");for(unsigned j=0;j<64;++j)carrier.end("duplicate-close");
            require(!carrier.active()&&!carrier.ownsTimer()&&fixture::liveHooks()==0,"cycle-returns-every-resource");
            require(!IsWindowVisible(window.h)&&IsWindowEnabled(window.h),"cycle-restores-original-hidden-enabled-state");
            require(carrier.begin(window.h,binding(),ports(window.h)),"successor-begin");
            const auto layouts=fixture::layouts;carrier.handle(window.h,Carrier::message,static_cast<WPARAM>(session),0);
            require(fixture::layouts==layouts,"old-queued-session-ignored");carrier.end("successor-close");drain(window.h);
        }facts["cycles"]=128;facts["successfulSessions"]=256;
    }else throw std::runtime_error("unknown-case");
    carrier.end("fixture-final");require(!carrier.active()&&!carrier.ownsTimer()&&fixture::liveHooks()==0,"terminal-resource-zero");
    facts["terminalActive"]=carrier.active();facts["terminalHooks"]=fixture::liveHooks();facts["terminalTimer"]=carrier.ownsTimer();
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    json result={{"case",argv[1]},{"scope","production Carrier with owned offscreen Win32 windows; no full app, WebView, devices or installed settings"}};
    int exit=0;
    try{run(argv[1],result["facts"]);result["pass"]=true;}
    catch(const std::exception& error){result["pass"]=false;result["failure"]=error.what();result["hooksAfterFixtureCleanup"]=fixture::liveHooks();exit=1;}
    std::ofstream out(argv[2]);out<<result.dump(2);return out?exit:3;
}
