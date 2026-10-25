#pragma once
#include <cstdint>
#include <string>
namespace ymcc::lossless {
struct WindowRefreshState {
    bool valid=false, ready=false, inputKnown=false;
    std::uintptr_t foreground=0;
    std::uint32_t input=0;
};
struct WindowRefreshResult {
    bool attempted=false, helperCreated=false, helperDestroyed=false;
    bool leftGame=false, returnRequested=false, returned=false, refreshed=false, cancelled=false;
    // refreshed is a compatibility receipt for an accepted request, not a rendered-frame probe.
    bool apiAccepted=false, frameAccepted=false, redrawAccepted=false;
    bool topAccepted=false, foregroundAccepted=false;
    unsigned method=3;
    std::string methodName="foreground-request";
    std::uint64_t elapsedMs=0;
    std::string reason;
};
// One explicit native activation round trip. The game is never minimized or
// resized. The transient helper is always destroyed before this action returns.
// No keys, thread-input attachment, foreground retry or resident work.
template<class Ports> WindowRefreshResult refreshGameWindowOnce(Ports& p) {
    WindowRefreshResult r;r.method=0;r.methodName="normal-helper";
    const auto started=p.now();std::uint32_t inputToken=0;
    [&]{
        auto state=p.observe();
        if(!state.valid||!state.ready){r.reason="window-refresh-context-changed";return;}
        if(!state.inputKnown){r.reason="window-refresh-input-unavailable";return;}
        if(state.foreground!=p.target()){r.reason="window-refresh-target-changed";return;}
        const auto input=state.input;inputToken=input;
        const auto safe=[&](const WindowRefreshState& s){return s.valid&&s.ready&&s.inputKnown&&s.input==input;};
        if(!p.create()){r.reason="window-refresh-create-failed";return;}
        r.helperCreated=true;
        struct Cleanup { Ports& p; WindowRefreshResult& r;
            ~Cleanup(){r.helperDestroyed=p.destroy();} } cleanup{p,r};
        state=p.observe();
        if(!safe(state)||state.foreground!=p.target()){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
        r.attempted=true;
        const bool accepted=p.activateHelper();
        for(;;){
            state=p.observe();
            if(!safe(state)){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
            if(state.foreground==p.helper()){r.leftGame=true;break;}
            if(state.foreground!=p.target()){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
            if(!accepted||p.now()-started>=300){r.reason="window-refresh-activation-denied";return;}
            p.pause();
        }
        const auto leftAt=p.now();
        while(p.now()-leftAt<120){
            state=p.observe();
            if(!safe(state)||state.foreground!=p.helper()){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
            p.pause();
        }
        state=p.observe();
        if(!safe(state)||state.foreground!=p.helper()){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
        r.returnRequested=true;const bool returned=p.activateGame();
        for(;;){
            state=p.observe();
            if(!safe(state)){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
            if(state.foreground==p.target()){r.returned=true;r.refreshed=true;return;}
            if(state.foreground!=p.helper()){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
            if(!returned||p.now()-started>=900){r.reason="window-refresh-return-unconfirmed";return;}
            p.pause();
        }
    }();
    // Cleanup mutates the receipt before its final copy; no dependence on NRVO.
    r.elapsedMs=p.now()-started;
    if(r.helperCreated&&!r.helperDestroyed){r.refreshed=false;r.reason="window-refresh-cleanup-unconfirmed";}
    if(r.refreshed){
        const auto state=p.observe();
        if(!state.valid||!state.ready||!state.inputKnown||state.input!=inputToken||state.foreground!=p.target()){
            r.refreshed=false;r.cancelled=true;r.reason="window-refresh-cancelled";
        }
    }
    return r;
}
// Shared one-shot contract. Method 2 first posts the frame-change request;
// method 1 is retained as a regression control. Neither requests activation.
// API acceptance is not completion of painting. Input need not be idle.
template<bool Frame,class Ports> WindowRefreshResult requestGameWindowRefreshOnce(Ports& p) {
    WindowRefreshResult r;r.method=Frame?2:1;r.methodName=Frame?"frame-changed":"redraw";
    const auto started=p.now();
    [&]{
        const auto before=p.observe();
        if(!before.valid||!before.ready){r.reason="window-refresh-context-changed";return;}
        if(before.foreground!=p.target()){r.reason="window-refresh-target-changed";return;}
        r.attempted=true;
        if constexpr(Frame){
            r.frameAccepted=p.frameChanged();
            if(!r.frameAccepted){r.reason="window-frame-request-denied";return;}
            const auto between=p.observe();r.leftGame=between.foreground!=p.target();
            if(!between.valid||!between.ready||r.leftGame){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
        }
        r.redrawAccepted=p.redraw();r.apiAccepted=r.redrawAccepted;
        const auto after=p.observe();
        r.leftGame=after.foreground!=p.target();
        if(!after.valid||!after.ready||r.leftGame){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
        if(!r.apiAccepted){r.reason="window-redraw-request-denied";return;}
        r.refreshed=true;r.reason=Frame?"frame-request-accepted":"redraw-request-accepted";
    }();
    r.elapsedMs=p.now()-started;return r;
}
template<class Ports> WindowRefreshResult redrawGameWindowOnce(Ports& p) {return requestGameWindowRefreshOnce<false>(p);}
template<class Ports> WindowRefreshResult refreshGameFrameOnce(Ports& p) {return requestGameWindowRefreshOnce<true>(p);}
// Normal method 3: original game only, one top request and one foreground
// request. Observe between APIs so a user switch is never actively reclaimed.
template<class Ports> WindowRefreshResult requestGameForegroundOnce(Ports& p){
    WindowRefreshResult r;r.method=3;r.methodName="foreground-request";const auto started=p.now();
    [&]{
        const auto before=p.observe();
        if(!before.valid||!before.ready){r.reason="window-refresh-context-changed";return;}
        if(before.foreground!=p.target()){r.reason="window-refresh-target-changed";return;}
        if(!before.inputKnown){r.reason="window-refresh-input-unavailable";return;}
        const auto safe=[&](const WindowRefreshState& s){return s.valid&&s.ready&&s.inputKnown&&s.input==before.input;};
        r.attempted=true;r.returnRequested=true;r.topAccepted=p.bringToTop();
        const auto between=p.observe();r.leftGame=between.foreground!=p.target();
        if(!safe(between)||r.leftGame){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
        r.foregroundAccepted=p.requestForeground();r.apiAccepted=r.topAccepted&&r.foregroundAccepted;
        const auto after=p.observe();r.leftGame=after.foreground!=p.target();r.returned=!r.leftGame;
        if(!safe(after)||r.leftGame){r.cancelled=true;r.reason="window-refresh-cancelled";return;}
        if(!r.apiAccepted){r.reason="window-foreground-request-denied";return;}
        r.refreshed=true;r.reason="foreground-request-accepted";
    }();
    r.elapsedMs=p.now()-started;return r;
}
} // namespace ymcc::lossless
