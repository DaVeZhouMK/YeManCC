#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include "radial_input_isolation.h"
#include "monitor_hud_input_policy.h"
#include "game_overlay_interactive_input.h"
#include "presentation_power_scope.h"

// Presentation carrier. HWND placement is not proof of
// exclusive-fullscreen visibility. No renderer, settings or input reader lives here.
namespace ymcc::gameoverlay {
// Same bounded Z-stack/overlap observation used by the existing desktop HUD.
inline bool foregroundCovers(HWND carrier,HWND foreground) {
    if(!carrier||!foreground||foreground==carrier)return false;
    const HWND top=GetAncestor(foreground,GA_ROOT);
    if(!top||top==carrier||!IsWindowVisible(top)||IsIconic(top)||
       !(GetWindowLongPtrW(top,GWL_EXSTYLE)&WS_EX_TOPMOST))return false;
    RECT own{},other{},intersection{};
    if(!GetWindowRect(carrier,&own)||!GetWindowRect(top,&other)||!IntersectRect(&intersection,&own,&other))return false;
    HWND before=GetWindow(carrier,GW_HWNDPREV);
    for(unsigned count=0;before&&count<128;++count,before=GetWindow(before,GW_HWNDPREV))if(before==top)return true;
    return false;
}
struct Binding {
    HWND target=nullptr,carrier=nullptr;
    DWORD pid=0;
    bool globalProbe=false; // Explicit local global observation route.
    bool pointerInteractive=false; // Only the bounded UI rectangle receives pointer input.
    bool detachedPointer=false; // Monitor invariant: display tree disabled; separate panel receives pointers.
    bool observationHold=false; // Private display only; physical input remains with the game.
    bool visualProbe=false; // Private observation: no input or virtual ownership.
    bool originalUiInput=false; // Presentation adapter: use the existing native UI/isolation owner.
    std::uint64_t originalUiToken=0;
    std::uint64_t birth=0,power=0,host=0,source=0,topology=0,session=0;
    sleeppower::SharedPowerV1 sharedPower;
};
class Carrier {
public:
    static constexpr UINT message=WM_APP+0x49f;
    struct Ports {
        std::function<bool(const Binding&)> current;
        std::function<bool()> layout,submit;
        std::function<void(bool,const char*)> changed;
        std::function<bool()> verifyDisplay;
        std::function<bool()> orderInput;
        std::function<bool()> ownPeerOccluded; // Optional floating-surface display ordering only.
    };
private:
    HWND window=nullptr;
    RECT original{};
    LONG_PTR addedEx=0,removedEx=0;
    bool originallyEnabled=false,originallyTopmost=false,ending=false,preparing=false;
    bool desktopLayered=false;
    COLORREF desktopKey=0;
    BYTE desktopAlpha=255;
    DWORD desktopLayerFlags=LWA_ALPHA;
    std::array<HWINEVENTHOOK,5> hooks{};
    Ports ports;
    std::atomic<std::shared_ptr<const Binding>> published;
    std::atomic<std::uint64_t> queued{0};
    std::atomic<bool> raisePending{false},layoutPending{false},peerCheckPending{false};
    std::atomic<std::uint64_t> acceptedSession{0};
    std::atomic<std::uint64_t> revokedSession{0};
    std::uint64_t sequence=0,started=0,armedSequence=0;
    unsigned maintenance=0;
    UINT_PTR timer=0;
    inline static Carrier* owner=nullptr; // One HWND owner on the native UI thread.
    inline static UINT_PTR nextTimer=0x594f5600;
    static HWND root(HWND h) {return h?GetAncestor(h,GA_ROOT):nullptr;}
    static void CALLBACK observed(HWINEVENTHOOK hook,DWORD event,HWND h,LONG object,LONG child,DWORD sourceThread,DWORD) {
        auto* self=owner;if(!self||!self->active()||child!=CHILDID_SELF)return;
        bool owned=false;for(const auto live:self->hooks)if(live&&live==hook){owned=true;break;}
        if(!owned)return;
        const auto binding=self->snapshot();if(!binding)return;
      try {
        // Peer presentation roots are owned by this same UI thread. Their
        // original bounded repairs may run after ours. Reuse these hooks and
        // ignore our own placement echo once the actual stack is correct.
        if(self->ports.ownPeerOccluded&&sourceThread==GetCurrentThreadId()){
            if((event==EVENT_OBJECT_SHOW||event==EVENT_OBJECT_REORDER)&&
                (object==OBJID_WINDOW||object==OBJID_CLIENT)&&h!=binding->carrier&&
                (!h||!IsChild(binding->carrier,h))){
                // Many native peer events can describe one final stack. Scan
                // once on the existing queued lane, never once per callback.
                self->peerCheckPending.store(true,std::memory_order_release);self->requestCheck();
            }
            return;
        }
        const bool foregroundEvent=event==EVENT_SYSTEM_FOREGROUND;
        const HWND foreground=binding->globalProbe?GetForegroundWindow():nullptr;
        const HWND foregroundRoot=foreground?root(foreground):nullptr;
        const bool related=h==binding->target||h==GetDesktopWindow()||
            (binding->globalProbe&&(h==foreground||h==foregroundRoot));
        if(foregroundEvent||((object==OBJID_WINDOW||object==OBJID_CLIENT)&&related)){
            if(foregroundEvent||event==EVENT_OBJECT_SHOW||event==EVENT_OBJECT_REORDER||event==EVENT_SYSTEM_MINIMIZEEND)
                self->raisePending.store(true,std::memory_order_release);
            self->requestCheck();
        }
      }catch(...){if(self->snapshot()==binding)self->end("event-observation-exception");}
    }
    static void CALLBACK timed(HWND h,UINT,UINT_PTR id,DWORD) {
        auto* self=owner;
        if(!self||self->window!=h||self->timer!=id)return;
        KillTimer(h,id);self->timer=0;
        if(self->armedSequence!=self->sequence)return;
        const auto binding=self->snapshot();
        self->check(true);
        if(self->snapshot()==binding&&self->active()&&self->maintenance<3)self->arm();
    }
    void arm() {
        constexpr std::array<UINT,3> delays={80,200,800};
        if(maintenance>=delays.size()||timer)return;
        armedSequence=sequence;
        timer=SetTimer(window,++nextTimer,delays[maintenance],timed);
        if(!timer)end("maintenance-timer-failed");
    }
    const char* contextFailure(const std::shared_ptr<const Binding>& b,const Ports& work) const {
        if(!b)return "binding-missing";
        if(!window||!IsWindow(window))return "carrier-window-missing";
        if(!IsWindowVisible(window))return "carrier-hidden";
        if(IsIconic(window))return "carrier-minimized";
        if(b->globalProbe)return work.current&&work.current(*b)?nullptr:"global-native-contract-invalid";
        if(!IsWindow(b->target))return "target-window-missing";
        const bool hold=b->visualProbe&&b->observationHold;
        if(!hold&&root(GetForegroundWindow())!=b->target)return "target-not-foreground";
        if(!hold&&IsIconic(b->target))return "target-minimized";
        DWORD pid=0;GetWindowThreadProcessId(b->target,&pid);
        if(pid!=b->pid)return "target-pid-changed";
        if(!work.current||!work.current(*b))return "native-contract-invalid";
        return nullptr;
    }
    void check(bool repair) {
        const auto b=snapshot();if(!b)return;
        try {
        auto queuedSession=b->session;
        queued.compare_exchange_strong(queuedSession,0,std::memory_order_acq_rel);
        if(preparing)return; // Keep any pending raise until the display is committed.
        bool eventRaise=raisePending.exchange(false,std::memory_order_acq_rel);
        const bool eventLayout=layoutPending.exchange(false,std::memory_order_acq_rel);
        const bool eventPeer=peerCheckPending.exchange(false,std::memory_order_acq_rel);
        if(revokedSession.load(std::memory_order_acquire)==b->session){end("input-admission-revoked");return;}
        const auto work=ports; // A callback may retire this session synchronously.
        const auto failure=contextFailure(b,work);if(snapshot()!=b)return;
        if(failure){end(failure);return;}
        if(!(b->pointerInteractive&&!b->detachedPointer?gameoverlayinput::ready(window):hudinput::inert(window))){end("native-pointer-policy-lost");return;}
        if(b->detachedPointer){
            const bool verified=work.verifyDisplay&&work.verifyDisplay();if(snapshot()!=b)return;
            if(!verified){end("display-child-input-policy-lost");return;}
        }
        if(eventPeer&&work.ownPeerOccluded){
            const bool occluded=work.ownPeerOccluded();if(snapshot()!=b)return;
            eventRaise=eventRaise||occluded;
        }
        if(!b->originalUiInput&&!b->visualProbe&&!inputReady()&&GetTickCount64()-started>=1000){end("neutral-output-unconfirmed");return;}
        if(!repair&&(eventRaise||!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)||foregroundCovers(window,GetForegroundWindow()))){
            const bool positioned=SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE;
            if(snapshot()!=b)return;
            if(!positioned){
                end("event-topmost-repair-failed");return;
            }
            if(snapshot()!=b)return;
            if(b->detachedPointer){
                const bool ordered=work.orderInput&&work.orderInput();
                if(snapshot()!=b)return;
                if(!ordered){end("event-input-order-failed");return;}
            }
        }
        // Only the three scheduled passes perform layout/submission. Events repair
        // confirmed Z-order loss only, without layout, rendering, or idle polling.
        if(repair||eventLayout){
            if(repair)++maintenance;
            const bool laidOut=work.layout();if(snapshot()!=b)return;
            const bool positioned=laidOut&&SetWindowPos(window,HWND_TOPMOST,0,0,0,0,
                 SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            if(snapshot()!=b)return;
            if(!positioned){end("presentation-layout-failed");return;}
            const bool submitted=work.submit();if(snapshot()!=b)return;
            if(!submitted)end("presentation-submit-failed");
        }
        } catch(...) {if(snapshot()==b)end("maintenance-exception");}
    }
public:
    ~Carrier(){end("carrier-destroyed");}
    Carrier()=default;
    Carrier(const Carrier&)=delete;
    Carrier& operator=(const Carrier&)=delete;
    bool active() const {return published.load(std::memory_order_acquire)!=nullptr;}
    std::shared_ptr<const Binding> snapshot() const {return published.load(std::memory_order_acquire);}
private:
    bool sessionInputAccepted(const Binding& b) const {
        return (b.originalUiInput||!b.visualProbe)&&b.session==acceptedSession.load(std::memory_order_acquire)&&
            b.session!=revokedSession.load(std::memory_order_acquire);
    }
public:
    bool inputReady() const {const auto b=snapshot();return b&&sessionInputAccepted(*b);}
    bool inputReady(const Binding& expected) const {
        const auto b=snapshot();return b.get()==&expected&&sessionInputAccepted(expected);
    }
    void revoke(const std::shared_ptr<const Binding>& expected) {
        if(!expected||snapshot()!=expected)return;
        // A delayed old writer must not overwrite a newer session's revocation.
        auto revoked=revokedSession.load(std::memory_order_acquire);
        while(revoked<expected->session&&
            !revokedSession.compare_exchange_weak(revoked,expected->session,std::memory_order_acq_rel)){}
        if(revoked<=expected->session)requestCheck(expected);
    }
    void sharedPowerChanged(const sleeppower::SharedPowerV1& power) {
        const auto b=snapshot();if(!b)return;
        // The original common UI dispatcher owns this notification. Even a
        // coalesced final Ready must retire a pre-sleep presentation/session.
        const bool current=b->originalUiInput&&inputReady(*b)?presentationpower::sameReadyEpisode(b->sharedPower,power):
            presentationpower::current(b->sharedPower,power);
        if(!current)end("shared-power-context-retired");
    }
    void requestCheck() {
        requestCheck(snapshot());
    }
private:
    void requestCheck(const std::shared_ptr<const Binding>& b) {
        if(!b)return;
        auto pending=queued.load(std::memory_order_acquire);
        do {
            if(pending==b->session||snapshot()!=b)return;
        }while(!queued.compare_exchange_weak(pending,b->session,std::memory_order_acq_rel));
        if(snapshot()!=b||!PostMessageW(b->carrier,message,static_cast<WPARAM>(b->session),0)){
            auto submitted=b->session;queued.compare_exchange_strong(submitted,0,std::memory_order_acq_rel);
        }
    }
public:
    void noteOutput(std::uint64_t session,std::uint64_t host,std::uint64_t power,std::uint64_t source,bool neutralAccepted) {
        const auto b=snapshot();if(!b||b->originalUiInput||b->visualProbe||session!=b->session||host!=b->host||power!=b->power||source!=b->source)return;
        acceptedSession.store(neutralAccepted?session:0,std::memory_order_release);
        if(!neutralAccepted)requestCheck();
    }
    bool begin(HWND h,Binding b,Ports p) noexcept {
      std::shared_ptr<const Binding> admitted;
      try {
        if(ending||active()||owner||!h||!IsWindow(h)||IsIconic(h)||IsWindowVisible(h)||
           (!b.globalProbe&&(!b.target||!b.pid||!b.birth))||!presentationpower::ready(b.sharedPower)||b.power!=b.sharedPower.generation||(!b.originalUiInput&&!b.visualProbe&&!b.host)||!p.current||!p.layout||!p.submit||!p.changed||
           (!b.globalProbe&&root(GetForegroundWindow())!=b.target))return false;
        // Reserve admission before invoking consumer code. Nested begin calls
        // must not publish a second owner or overwrite its native resources.
        owner=this;
        const auto releaseAdmission=[](Carrier* self){if(owner==self&&!self->active())owner=nullptr;};
        const std::unique_ptr<Carrier,decltype(releaseAdmission)> admission(this,releaseAdmission);
        if(!p.current(b))return false;
        if(!GetWindowRect(h,&original))return false;
        window=h;preparing=true;ports=std::move(p);b.session=++sequence;b.carrier=h;started=GetTickCount64();maintenance=0;
        const LONG_PTR ex=GetWindowLongPtrW(h,GWL_EXSTYLE);
        desktopLayered=(ex&WS_EX_LAYERED)!=0;desktopKey=0;desktopAlpha=255;desktopLayerFlags=LWA_ALPHA;
        if(desktopLayered&&!GetLayeredWindowAttributes(h,&desktopKey,&desktopAlpha,&desktopLayerFlags)){
            ports={};window=nullptr;preparing=false;return false;
        }
        originallyEnabled=IsWindowEnabled(h)!=FALSE;originallyTopmost=(ex&WS_EX_TOPMOST)!=0;
        const bool directPointer=b.pointerInteractive&&!b.detachedPointer;
        addedEx=(directPointer?gameoverlayinput::requiredEx:hudinput::requiredExStyle)&~ex;
        removedEx=directPointer?(ex&WS_EX_TRANSPARENT):0;
        const auto binding=std::make_shared<const Binding>(b);
        admitted=binding;
        owner=this;
        published.store(binding,std::memory_order_release);
        acceptedSession.store(0,std::memory_order_release);
        SetLastError(ERROR_SUCCESS);
        const bool styleReady=SetWindowLongPtrW(h,GWL_EXSTYLE,(ex|addedEx)&~removedEx)||GetLastError()==ERROR_SUCCESS;
        if(snapshot()!=binding)return false;
        if(!styleReady){end("style-failed");return false;}
        // Disabling only native hit/input dispatch also covers WebView child
        // HWNDs. JS semantic navigation remains on the existing IPC consumer.
        const auto verify=ports.verifyDisplay;
        const bool pointerReady=directPointer?gameoverlayinput::ensure(h):b.detachedPointer?verify&&verify():hudinput::ensure(h);
        if(snapshot()!=binding)return false;
        if(!pointerReady||!hudinput::registerTouchPassthrough(h)){
            end("display-input-transparency-unavailable");return false;
        }
        if(GetCapture()==h||(GetCapture()&&IsChild(h,GetCapture())))ReleaseCapture();
        if(snapshot()!=binding)return false;
        const auto layout=ports.layout;
        const bool laidOut=layout();if(snapshot()!=binding)return false;
        if((directPointer?!IsWindowEnabled(h):IsWindowEnabled(h))||!laidOut){end("layout-or-mouse-gate-failed");return false;}
        constexpr std::array<DWORD,5> events={EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_REORDER,EVENT_OBJECT_DESTROY,EVENT_OBJECT_SHOW,EVENT_SYSTEM_MINIMIZEEND};
        for(size_t i=0;i<events.size();++i){
            const auto hook=SetWinEventHook(events[i],events[i],nullptr,observed,0,0,WINEVENT_OUTOFCONTEXT|(ports.ownPeerOccluded?0:WINEVENT_SKIPOWNPROCESS));
            if(snapshot()!=binding){if(hook)UnhookWinEvent(hook);return false;}
            hooks[i]=hook;if(!hook){end("event-hook-failed");return false;}
        }
        const auto changed=ports.changed;changed(true,"preparing");
        if(snapshot()!=binding)return false;
        if(!SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED)){
            end("show-placement-failed");return false;
        }
        if(snapshot()!=binding)return false;
        ShowWindow(h,SW_SHOWNOACTIVATE);
        if(snapshot()!=binding)return false;
        const auto submit=ports.submit;
        const bool submitted=submit();if(snapshot()!=binding)return false;
        if(!IsWindowVisible(h)||(!b.globalProbe&&root(GetForegroundWindow())!=b.target)||!submitted){
            end("show-or-submit-failed");return false;
        }
        if(b.originalUiInput)acceptedSession.store(b.session,std::memory_order_release);
        preparing=false;check(false);if(snapshot()!=binding)return false;
        arm();return snapshot()==binding;
      }catch(...){if(admitted&&snapshot()==admitted)end("begin-exception");if(!active()&&owner!=this){ports={};window=nullptr;}return false;}
    }
    // Same root display invariants as the monitor. Mutations are scoped to this
    // session; ending bypasses the guard before restoring desktop interaction.
    bool protectInput(HWND h,UINT m,WPARAM w,LPARAM l,LRESULT& result) {
        switch(m){
        case WM_STYLECHANGING:case WM_STYLECHANGED:case WM_ENABLE:
        case WM_NCHITTEST:case WM_TOUCHHITTESTING:case WM_POINTERACTIVATE:case WM_MOUSEACTIVATE:break;
        default:return false;
        }
        if(h!=window||ending||!active())return false;
        const auto binding=snapshot();const bool interactive=binding&&binding->pointerInteractive&&!binding->detachedPointer;
        if(m==WM_STYLECHANGING&&(interactive?gameoverlayinput::guardStyle(w,l):hudinput::guardStyle(w,l))){result=0;return true;}
        if(m==WM_STYLECHANGED||m==WM_ENABLE){
            if(!(interactive?gameoverlayinput::ensure(h):hudinput::ensure(h)))end("native-pointer-policy-unavailable");
            result=0;return true;
        }
        if(m==WM_NCHITTEST){result=interactive?HTCLIENT:HTTRANSPARENT;return true;}
        if(m==WM_TOUCHHITTESTING){
            const auto* input=reinterpret_cast<const TOUCH_HIT_TESTING_INPUT*>(l);RECT bounds{};
            if(!interactive||!input||!GetWindowRect(h,&bounds)||!PtInRect(&bounds,input->point)){result=hudinput::rejectTouchHitTest(l);return true;}
        }
        if(m==WM_POINTERACTIVATE){result=PA_NOACTIVATE;return true;}
        if(m==WM_MOUSEACTIVATE){result=MA_NOACTIVATE;return true;}
        return false;
    }
    void deferDesktopOpacity(bool layered,BYTE alpha) {
        if(!active())return;
        desktopLayered=layered;desktopKey=0;desktopAlpha=alpha;desktopLayerFlags=LWA_ALPHA;
    }
    void deferDesktopTopmost(bool topmost){if(active())originallyTopmost=topmost;}
    bool handle(HWND h,UINT m,WPARAM w,LPARAM) {
        if(h!=window)return false;
        if(m==message){
            const auto b=snapshot();
            if(b&&w==b->session)check(false);
            return true;
        }
        return false;
    }
    void displayChanged(){if(active()){layoutPending.store(true,std::memory_order_release);requestCheck();}}
    void end(const char* reason) noexcept {
        if(ending||!active())return;
        ending=true;preparing=false;
        // Stop semantic navigation first. Keep output ownership published while
        // synchronous hide/restore messages run; those messages must not save
        // the temporary overlay geometry or release held input to the game.
        const auto closing=snapshot();
        revokedSession.store(closing->session,std::memory_order_release);
        acceptedSession.store(0,std::memory_order_release);
        ++sequence;queued.store(0,std::memory_order_release);raisePending.store(false,std::memory_order_release);layoutPending.store(false,std::memory_order_release);peerCheckPending.store(false,std::memory_order_release);
        if(timer){KillTimer(window,timer);timer=0;}
        for(auto& hook:hooks){if(hook)UnhookWinEvent(hook);hook=nullptr;}
        if(owner==this)owner=nullptr;
        const auto h=window;
        if(h&&IsWindow(h)){
            ShowWindow(h,SW_HIDE); // Keep native disabled until fully hidden.
        }
        // The consumer arms return ownership and releases any editor width
        // before we restore desktop geometry or unpublish the active session.
        try{if(ports.changed)ports.changed(false,reason);}catch(...){}
        if(h&&IsWindow(h)){
            auto desktopEx=(GetWindowLongPtrW(h,GWL_EXSTYLE)&~addedEx)|removedEx;
            desktopEx=desktopLayered?(desktopEx|WS_EX_LAYERED):(desktopEx&~WS_EX_LAYERED);
            SetWindowLongPtrW(h,GWL_EXSTYLE,desktopEx);
            if(desktopLayered)SetLayeredWindowAttributes(h,desktopKey,desktopAlpha,desktopLayerFlags);
            RegisterTouchHitTestingWindow(h,TOUCH_HIT_TESTING_DEFAULT);
            SetWindowPos(h,originallyTopmost?HWND_TOPMOST:HWND_NOTOPMOST,original.left,original.top,
                original.right-original.left,original.bottom-original.top,SWP_NOACTIVATE|SWP_FRAMECHANGED);
            EnableWindow(h,originallyEnabled?TRUE:FALSE);
        }
        published.store({},std::memory_order_release);
        ports={};window=nullptr;ending=false;
    }
    unsigned repairCount() const {return maintenance;}
    unsigned ownedHookCount() const {unsigned n=0;for(auto hook:hooks)if(hook)++n;return n;}
    bool ownsTimer() const {return timer!=0;}
};
}
