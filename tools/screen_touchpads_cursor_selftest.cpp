#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
#include <thread>
using namespace ymcc::screenpads;
static void pump(DWORD ms,bool bus=true) {
    const auto deadline=GetTickCount64()+ms;MSG message{};
    do {
        if(bus)snapshot();
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
        Sleep(1);
    }while(GetTickCount64()<deadline);
}
// RealTouch-only observation: never consumes, modifies or blocks input.
// Tag our two restoration moves so external mouse traffic is not confused with
// test-generated input. A contaminated suppression run must never count as PASS.
static constexpr ULONG_PTR kTestMouseTag=0x594d4352;
static unsigned long long externalMouseMoves=0,externalInjectedMouseMoves=0,testMouseMoves=0;
static LRESULT CALLBACK testMouseObserver(int code,WPARAM message,LPARAM data) {
    if(code==HC_ACTION && message==WM_MOUSEMOVE) {
        const auto& event=*reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        if(event.dwExtraInfo==kTestMouseTag)++testMouseMoves;
        else if(event.flags&LLMHF_INJECTED)++externalInjectedMouseMoves;
        else ++externalMouseMoves;
    }
    return CallNextHookEx(nullptr,code,message,data);
}
struct TestMouseObservation {
    HHOOK hook=SetWindowsHookExW(WH_MOUSE_LL,testMouseObserver,GetModuleHandleW(nullptr),0);
    ~TestMouseObservation(){if(hook)UnhookWindowsHookEx(hook);}
};
static Json sample() {
    CURSORINFO ci{sizeof(ci)};const bool read=GetCursorInfo(&ci)!=FALSE;RECT rect{};
    if(cursorWindow)GetWindowRect(cursorWindow,&rect);
    return {{"shape",(uintptr_t)ci.hCursor},{"tracking",cursorTracking.load()},{"pending",cursorSyncPending.load()},{"fallbackError",cursorError},{"syncs",cursorSyncs},{"cursorRead",read},{"flags",ci.flags},{"x",ci.ptScreenPos.x},{"y",ci.ptScreenPos.y},
        {"leftDown",core.pads[0].down},{"rightDown",core.pads[1].down},{"events",eventCount},
        {"inputVisible",windows[0] && IsWindowVisible(windows[0])!=0},{"retained",cursorRetained.load()},{"shows",cursorShows},{"hides",cursorHides},{"handoffs",cursorHandoffs},{"fallbackVisible",cursorVisible},
        {"fallbackX",rect.left},{"fallbackY",rect.top},{"hotspotX",cursorHotspot.x},{"hotspotY",cursorHotspot.y}};
}
static int modelTest(const char* path) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HWND foreground=GetForegroundWindow();const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    Json cases=Json::array();auto check=[&](const std::string& name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
    Config cfg;cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;dryRun=true;
    initialize(GetModuleHandleW(nullptr),nullptr,cfg);pump(15,false);
    HCURSOR arrow=LoadCursorW(nullptr,IDC_ARROW);CURSORINFO ci{sizeof(ci)};ci.hCursor=arrow;ci.flags=CURSOR_SUPPRESSED;ci.ptScreenPos={400,300};
    check("policy-suppressed",cursorFallbackRequired(true,CURSOR_SUPPRESSED,arrow));
    check("policy-visible-not-duplicated",!cursorFallbackRequired(true,CURSOR_SHOWING,arrow));
    check("policy-explicit-hidden-not-forced",!cursorFallbackRequired(true,0,arrow));
    check("policy-no-gesture",!cursorFallbackRequired(false,CURSOR_SUPPRESSED,arrow));
    check("policy-null-shape-before-resolution",!cursorFallbackRequired(true,CURSOR_SUPPRESSED,nullptr));
    check("policy-visible-bit-wins",!cursorFallbackRequired(true,CURSOR_SUPPRESSED|CURSOR_SHOWING,arrow));
    for(Mode mode:{Mode::Mouse,Mode::Deck}) {
        const std::string prefix=modeName(mode);
        if(mode==Mode::Deck){setContext("steamdeck",true);cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;configure(cfg);pump(10,false);}
        {std::lock_guard<std::mutex> lock(mutex);core.event(0,77,0,70,80,sizes[0].cx,sizes[0].cy,GetTickCount64());}
        cursorContactChanged(windows[0]);check(prefix+"-real-Core-contact-enables-tracking",cursorTracking.load());
        ci.flags=CURSOR_SUPPRESSED;ci.hCursor=arrow;cursorSyncInfo(ci);
        check(prefix+"-suppressed-fixture-shows-real-layered-window",cursorWindow && IsWindowVisible(cursorWindow) && cursorVisible);
        const auto uploads=cursorUploads;bool positioned=true;
        for(int n=0;n<100;n++) {
            ci.ptScreenPos={400+n,300+n};cursorSyncInfo(ci);RECT rect{};GetWindowRect(cursorWindow,&rect);
            positioned=positioned && rect.left+cursorHotspot.x==ci.ptScreenPos.x && rect.top+cursorHotspot.y==ci.ptScreenPos.y;
        }
        check(prefix+"-100-positions-preserve-hotspot",positioned);
        check(prefix+"-100-moves-do-not-rerender-bitmap",cursorUploads==uploads);
        // R2 regression: touch and mouse outputs can report SUPPRESSED/SHOWING
        // back-to-back. A single SHOWING sample is not a safe compositor handoff.
        bool interleaveContinuous=true;unsigned visibilityEdges=0;bool lastVisible=cursorVisible;
        for(int n=0;n<128;n++) {
            ci.flags=(n&1)?CURSOR_SHOWING:CURSOR_SUPPRESSED;
            cursorSyncInfo(ci);
            if(lastVisible!=cursorVisible)++visibilityEdges;
            lastVisible=cursorVisible;interleaveContinuous=interleaveContinuous && cursorVisible;
        }
        check(prefix+"-rapid-touch-mouse-interleave-no-flicker",interleaveContinuous && visibilityEdges==0);
        for(ULONGLONG cadence:{4ULL,8ULL,16ULL}) {
            ci.flags=CURSOR_SUPPRESSED;ci.hCursor=arrow;cursorSyncInfo(ci,1000);
            const auto hides=cursorHides,shows=cursorShows;bool stable=true;
            for(unsigned n=0;n<128;n++) {
                ci.flags=(n&1)?CURSOR_SHOWING:CURSOR_SUPPRESSED;
                if(n%4==1)ci.hCursor=nullptr;else ci.hCursor=arrow;
                ci.ptScreenPos={400+(LONG)n,300+(LONG)n};cursorSyncInfo(ci,1000+n*cadence);
                RECT rect{};GetWindowRect(cursorWindow,&rect);
                stable=stable && cursorVisible && IsWindowVisible(cursorWindow) &&
                    rect.left+cursorHotspot.x==ci.ptScreenPos.x && rect.top+cursorHotspot.y==ci.ptScreenPos.y;
            }
            check(prefix+"-interleave-cadence-"+std::to_string(cadence)+"ms-no-visibility-edges",stable && cursorHides==hides && cursorShows==shows);
        }
        ci.flags=CURSOR_SUPPRESSED;ci.hCursor=arrow;cursorSyncInfo(ci,5000);
        ci.flags=CURSOR_SHOWING;cursorSyncInfo(ci,5010);cursorSyncInfo(ci,5080);
        ci.flags=CURSOR_SUPPRESSED;cursorSyncInfo(ci,5085);ci.flags=CURSOR_SHOWING;cursorSyncInfo(ci,5090);
        check(prefix+"-suppression-resets-stable-handoff-clock",cursorVisible);
        cursorSyncInfo(ci,5169);check(prefix+"-handoff-not-early-after-suppression",cursorVisible);
        cursorSyncInfo(ci,5170);check(prefix+"-handoff-completes-after-new-stable-80ms",!cursorVisible);
        ci.flags=CURSOR_SUPPRESSED;ci.hCursor=arrow;cursorSyncInfo(ci);
        const auto validShape=cursorShape;ci.hCursor=(HCURSOR)(uintptr_t)0x123456;
        cursorSyncInfo(ci);check(prefix+"-transient-shape-error-keeps-last-good-frame",cursorVisible && cursorShape==validShape);
        ci.hCursor=arrow;cursorSyncInfo(ci);
        ci.flags=CURSOR_SUPPRESSED;cursorSyncInfo(ci);
        const auto styles=GetWindowLongPtrW(cursorWindow,GWL_EXSTYLE);
        check(prefix+"-noactivate-click-through-styles",(styles&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_LAYERED))==(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_LAYERED));
        check(prefix+"-cursor-hit-test-transparent",SendMessageW(cursorWindow,WM_NCHITTEST,0,MAKELPARAM(400,300))==HTTRANSPARENT);
        const ULONGLONG handoffStart=1000;ci.flags=CURSOR_SHOWING;cursorSyncInfo(ci,handoffStart);
        check(prefix+"-single-SHOWING-sample-does-not-blink",cursorVisible);
        cursorSyncInfo(ci,handoffStart+79);check(prefix+"-handoff-grace-preserves-visual",cursorVisible);
        cursorSyncInfo(ci,handoffStart+80);check(prefix+"-stable-system-cursor-handoff",!cursorVisible && !IsWindowVisible(cursorWindow));
        ci.flags=0;ci.hCursor=nullptr;cursorSyncInfo(ci);check(prefix+"-explicit-hidden-null-stays-hidden",!cursorVisible);
        ci.flags=CURSOR_SUPPRESSED;cursorSyncInfo(ci);check(prefix+"-suppressed-null-shape-resolves-safely",cursorVisible && cursorShape);
        const auto syncs=cursorSyncs;std::thread bus([]{for(int n=0;n<10000;n++)cursorRequestSync();});bus.join();
        check(prefix+"-BUS-only-posts-does-not-render",cursorSyncs==syncs);check(prefix+"-BUS-posts-coalesced",cursorSyncPending.load());
        // Drain on the actual HWND owner, but do not invoke another BUS tick.
        pump(2,false);check(prefix+"-BUS-dispatches-one-UI-sync",cursorSyncs==syncs+1);
        ci.flags=CURSOR_SUPPRESSED;ci.hCursor=arrow;cursorSyncInfo(ci);
        {std::lock_guard<std::mutex> lock(mutex);core.event(0,77,2,140,180,sizes[0].cx,sizes[0].cy,GetTickCount64());}
        cursorContactChanged(windows[0]);check(prefix+"-Core-up-clears-fallback",!cursorVisible && !cursorTracking.load());
        const auto before=cursorSyncs;for(int n=0;n<1000000;n++)cursorRequestSync();pump(2,false);
        check(prefix+"-million-inactive-requests-no-post-or-render",cursorSyncs==before && !cursorSyncPending.load());
        // Normal lift keeps a suppressed cursor visible; cancel/close never do.
        {std::lock_guard<std::mutex> lock(mutex);core.event(0,78,0,70,80,sizes[0].cx,sizes[0].cy,GetTickCount64());}
        cursorContactChanged(windows[0]);ci.flags=CURSOR_SUPPRESSED;ci.hCursor=arrow;cursorSyncInfo(ci);
        {std::lock_guard<std::mutex> lock(mutex);core.pads[0]={};}
        const auto liftHides=cursorHides;
        check(prefix+"-normal-up-enters-passive-retention",cursorRetainAfterUp(windows[0],ci));
        check(prefix+"-lift-does-not-remove-suppressed-cursor",!cursorTracking.load() && cursorRetained.load() && cursorVisible && cursorHides==liftHides);
        SendMessageW(windows[0],WM_POINTERCAPTURECHANGED,78,0);
        check(prefix+"-post-UP-capture-notification-does-not-clear-retention",cursorRetained.load() && cursorVisible);
        ci.flags=0;cursorSyncInfo(ci);check(prefix+"-explicit-game-hide-clears-retained-visual",!cursorVisible && !cursorRetained.load());
        cursorTracking.store(true);ci.flags=CURSOR_SUPPRESSED;cursorSyncInfo(ci);cursorRetainAfterUp(windows[0],ci);
        cursorSyncPending.store(false);cursorNextRetainedPoll.store(GetTickCount64()+10000);
        for(int n=0;n<10000;n++)cursorRequestSync();check(prefix+"-passive-BUS-poll-is-throttled",!cursorSyncPending.load());
        ci.flags=CURSOR_SHOWING;cursorSyncInfo(ci);check(prefix+"-physical-mouse-handoff-after-lift-is-immediate",!cursorRetained.load() && !cursorVisible);
        cursorTracking.store(true);ci.flags=CURSOR_SUPPRESSED;cursorSyncInfo(ci);cursorRetainAfterUp(windows[0],ci);
        {std::lock_guard<std::mutex> lock(mutex);core.event(0,79,0,70,80,sizes[0].cx,sizes[0].cy,GetTickCount64());}
        cursorContactChanged(windows[0]);ci.flags=CURSOR_SUPPRESSED;cursorSyncInfo(ci);
        check(prefix+"-next-swipe-reuses-the-same-cursor-window",cursorTracking.load() && !cursorRetained.load() && cursorVisible);
        cursorTracking.store(true);ci.flags=CURSOR_SUPPRESSED;ci.hCursor=arrow;cursorSyncInfo(ci);
        const auto swipeShows=cursorShows,swipeHides=cursorHides;bool swipeContinuous=true;
        for(unsigned swipe=0;swipe<32;swipe++) {
            {std::lock_guard<std::mutex> lock(mutex);core.pads[0]={};}
            swipeContinuous=cursorRetainAfterUp(windows[0],ci) && swipeContinuous;
            SendMessageW(windows[0],WM_POINTERCAPTURECHANGED,300+swipe,0);
            swipeContinuous=swipeContinuous && cursorVisible && cursorRetained.load();
            {std::lock_guard<std::mutex> lock(mutex);core.event(0,300+swipe,0,70,80,sizes[0].cx,sizes[0].cy,GetTickCount64());}
            cursorContactChanged(windows[0]);ci.ptScreenPos.x++;cursorSyncInfo(ci);
            swipeContinuous=swipeContinuous && cursorVisible && cursorTracking.load() && !cursorRetained.load();
        }
        check(prefix+"-32-lift-refinger-cycles-no-window-visibility-edges",swipeContinuous && cursorShows==swipeShows && cursorHides==swipeHides);
        {std::lock_guard<std::mutex> lock(mutex);core.pads[0]={};}
        cursorRetainAfterUp(windows[0],ci);
        cursorSyncPending.store(false);cursorNextRetainedPoll.store(0);cursorRequestSync();
        check(prefix+"-passive-BUS-posts-one-UI-check",cursorSyncPending.load());
        release();check(prefix+"-cancel-clears-actual-passive-retention",!cursorRetained.load() && !cursorVisible && !cursorTracking.load());
        pump(2,false);check(prefix+"-stale-post-after-cancel-does-not-recreate-cursor",!cursorVisible && !cursorRetained.load());
        cursorTracking.store(true);ci.flags=CURSOR_SUPPRESSED;cursorSyncInfo(ci);cursorRetainAfterUp(windows[0],ci);
        Config changed=cfg;changed.enabled=false;configure(changed);
        check(prefix+"-configuration-change-clears-passive-retention",!cursorVisible && !cursorRetained.load());
        configure(cfg);pump(2,false);
        release();check(prefix+"-forced-release-clears-passive-retention",!cursorTracking.load() && !cursorRetained.load() && !cursorVisible);
        ci.flags=CURSOR_SHOWING;cursorSyncInfo(ci);
        check(prefix+"-new-gesture-never-starts-fallback-over-visible-system-cursor",!cursorVisible);
    }
    {std::lock_guard<std::mutex> lock(mutex);core.event(0,1,0,50,50,sizes[0].cx,sizes[0].cy,1);core.event(1,2,0,60,60,sizes[1].cx,sizes[1].cy,1);}
    cursorContactChanged(windows[0]);
    {std::lock_guard<std::mutex> lock(mutex);core.event(0,1,2,150,150,sizes[0].cx,sizes[0].cy,100);}
    cursorContactChanged(windows[0]);check("other-Deck-contact-retains-tracking",!core.pads[0].down && core.pads[1].down && cursorTracking.load());
    SendMessageW(windows[1],WM_POINTERCAPTURECHANGED,2,0);check("capture-loss-clears-fallback",!cursorTracking.load() && !cursorVisible);
    for(Mode mode:{Mode::Wasd,Mode::Arrows,Mode::DualSense,Mode::Off}) {
        {std::lock_guard<std::mutex> lock(mutex);core.config.left=mode;core.pads[0].down=true;}
        cursorContactChanged(windows[0]);check(std::string(modeName(mode))+"-no-mouse-fallback",!cursorTracking.load());release();
    }
    ci.flags=CURSOR_SUPPRESSED;
    for(const wchar_t* shape:{IDC_ARROW,IDC_IBEAM,IDC_CROSS,IDC_WAIT}) {ci.hCursor=LoadCursorW(nullptr,shape);check("shape-upload-"+std::to_string((uintptr_t)shape),cursorUpload(ci));}
    cursorHide();cfg.enabled=false;configure(cfg);pump(2,false);const auto syncs=cursorSyncs,uploads=cursorUploads;const auto start=std::chrono::steady_clock::now();
    for(int n=0;n<1000000;n++)snapshot();pump(2,false);const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    check("disabled-million-snapshots-no-render-or-post",cursorSyncs==syncs && cursorUploads==uploads && !cursorSyncPending.load());
    check("fixture-does-not-inject-system-input",dryRun && !inputError);check("cursor-render-error-zero",cursorError==0);
    check("no-focus-stolen",foreground==GetForegroundWindow());shutdown();
    check("shutdown-clears-window-and-tracking",!cursorWindow && !cursorTracking.load() && !cursorSyncPending.load());
    const DWORD gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);check("no-GDI-leak",gdiAfter<=gdiBefore+2);
    bool ok=true;for(const auto& c:cases)ok=ok && c["ok"].get<bool>();
    Json result={{"ok",ok},{"suite","fixture-not-real-touch"},{"cases",cases},{"gdiBefore",gdiBefore},{"gdiAfter",gdiAfter},
        {"disabledMillionSnapshotsUs",us},{"windowsTouchInjected",false},{"systemMouseOrKeyboardInjected",false},{"virtualTargetStarted",false}};
    std::ofstream out(path);out<<result.dump(2);std::cout<<(ok?"PASS ":"FAIL ")<<cases.size()<<" fixture cases: "<<path<<"\n";
    for(const auto& c:cases)if(!c["ok"].get<bool>())std::cout<<c["name"]<<"\n";return ok?0:1;
}
int main(int argc,char** argv) {
    if(argc>1 && std::string(argv[1])=="--model-only")return modelTest(argc>2?argv[2]:"cursor-model-selftest.json");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT original{};GetCursorPos(&original);const HWND foreground=GetForegroundWindow();
    if(!SetCursorPos(original.x,original.y)) {
        const DWORD error=GetLastError();const char* path=argc>1?argv[1]:"cursor-real-selftest.json";
        Json blocked={{"ok",false},{"status","blocked"},{"suite","real-touch"},{"reason","Windows rejected even same-position SetCursorPos; input injection cannot be validated in this desktop state."},{"win32Error",error},{"windowsTouchInjected",false}};
        std::ofstream out(path);out<<blocked.dump(2);std::cout<<"BLOCKED real-touch desktop preflight: "<<path<<"\n";return 2;
    }
    TestMouseObservation observation;
    const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    Json cases=Json::array(),samples=Json::array();
    auto check=[&](const std::string& name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
    HCURSOR arrow=LoadCursorW(nullptr,IDC_ARROW);
    check("suppressed-cursor-needs-fallback",cursorFallbackRequired(true,CURSOR_SUPPRESSED,arrow));
    check("no-active-mouse-gesture-no-fallback",!cursorFallbackRequired(false,CURSOR_SUPPRESSED,arrow));
    check("visible-system-cursor-never-duplicated",!cursorFallbackRequired(true,CURSOR_SHOWING,arrow));
    check("explicitly-hidden-cursor-not-forced-visible",!cursorFallbackRequired(true,0,arrow));
    check("null-cursor-shape-not-forced-arrow",!cursorFallbackRequired(true,CURSOR_SUPPRESSED,nullptr));
    check("visible-bit-wins-over-suppressed-bit",!cursorFallbackRequired(true,CURSOR_SHOWING|CURSOR_SUPPRESSED,arrow));
    check("initialize-real-touch-injection",InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE)!=FALSE);
    Json trace=Json::array();testTrace=&trace;dryRun=true;Config cfg;cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;
    initialize(GetModuleHandleW(nullptr),nullptr,cfg);pump(40);
    auto isolateWindows=[&] {
        // Keep injected contacts away from the running product touch surfaces.
        POINT origin{0,0};MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY),&monitor);
        for(int i=0;i<2;i++)if(windows[i]) {
            SetWindowPos(windows[i],HWND_TOPMOST,monitor.rcWork.left+80+i*600,monitor.rcWork.top+100,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
            renderOutline(i,cfg.transparency);
        }pump(30);
    };
    isolateWindows();
    std::array<POINTER_TOUCH_INFO,2> touches{};
    auto contact=[&](int index,float x,float y,DWORD flags) {
        auto& t=touches[index];const POINT previous=t.pointerInfo.ptPixelLocation;t={};
        t.pointerInfo.pointerType=PT_TOUCH;t.pointerInfo.pointerId=index;
        POINT p{(LONG)(sizes[index].cx*x),(LONG)(sizes[index].cy*y)};
        ClientToScreen(windows[getConfig().layout==Layout::Single?0:index],&p);
        if(flags==POINTER_FLAG_UP)p=previous;
        t.pointerInfo.ptPixelLocation=p;t.pointerInfo.pointerFlags=flags;
        t.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE;t.pressure=512;
        t.rcContact={p.x-2,p.y-2,p.x+2,p.y+2};
    };
    auto inject=[&](UINT count){const bool ok=InjectTouchInput(count,touches.data())!=FALSE;pump(25);return ok;};
    const DWORD down=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;
    const DWORD move=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;
    for(int modeIndex=0;modeIndex<2;modeIndex++) {
        const std::string name=modeIndex==0?"mouse":"steamdeck";
        if(modeIndex==1){setContext("steamdeck",true);cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;configure(cfg);pump(30);isolateWindows();}
        RECT padRect{};GetWindowRect(windows[0],&padRect);const bool cursorPlaced=SetCursorPos(padRect.left+24,padRect.top+24)!=FALSE;const DWORD placeError=GetLastError();SetCursor(arrow);pump(30);samples.push_back(Json{{"mode",name},{"step","setup"},{"placed",cursorPlaced},{"placeError",placeError},{"wantedX",padRect.left+24},{"wantedY",padRect.top+24},{"sample",sample()}});
        contact(0,.25f,.5f,down);check(name+"-touch-target-is-isolated-production-handler",WindowFromPoint(touches[0].pointerInfo.ptPixelLocation)==windows[0]);check(name+"-inject-down",inject(1));
        bool injected=true,continuous=true,visible=true,suppressed=true,fallback=true,position=true;
        const auto inputStart=injectedCount;const auto uploadsStart=cursorUploads;
        for(int i=0;i<32;i++) {
            contact(0,.25f+i*.012f,.5f,move);injected=inject(1) && injected;
            if(i<3)continue;
            const Json s=sample();samples.push_back(Json{{"mode",name},{"step",i},{"sample",s}});
            continuous=continuous && s["leftDown"].get<bool>();visible=visible && s["inputVisible"].get<bool>();
            suppressed=suppressed && (s["flags"].get<DWORD>()&CURSOR_SUPPRESSED);
            fallback=fallback && s["fallbackVisible"].get<bool>();
            position=position && s["fallbackX"].get<LONG>()+s["hotspotX"].get<LONG>()==s["x"].get<LONG>() &&
                s["fallbackY"].get<LONG>()+s["hotspotY"].get<LONG>()==s["y"].get<LONG>();
        }
        check(name+"-real-touch-updates-injected",injected);
        check(name+"-contacts-continuous-not-lost",continuous);
        check(name+"-touchpad-window-never-hidden",visible);
        check(name+"-system-cursor-suppression-reproduced",suppressed);
        check(name+"-fallback-visible-through-all-suppressed-samples",fallback);
        check(name+"-fallback-uses-real-cursor-position-and-hotspot",position);
        check(name+"-mapper-runs-dry-without-system-input",dryRun && !inputError);
        if(modeIndex==1)check(name+"-native-gesture-adds-no-mouse-action",injectedCount==inputStart);
        check(name+"-cursor-bitmap-reused-on-move",cursorUploads<=uploadsStart+1);
        if(cursorWindow) {
            const LONG_PTR styles=GetWindowLongPtrW(cursorWindow,GWL_EXSTYLE);
            check(name+"-cursor-is-layered-click-through-noactivate",(styles&(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE));
            RECT r{};GetWindowRect(cursorWindow,&r);POINT hit{r.left+cursorHotspot.x,r.top+cursorHotspot.y};
            check(name+"-cursor-does-not-intercept-input",WindowFromPoint(hit)!=cursorWindow);
        }else check(name+"-fallback-window-created",false);
        pump(3,false);const auto beforeSync=cursorSyncs;
        std::thread bus([]{for(int n=0;n<10000;n++)cursorRequestSync();});bus.join();
        check(name+"-BUS-never-draws-or-allocates-GDI",cursorSyncs==beforeSync);
        check(name+"-BUS-request-is-coalesced",cursorSyncPending.load());pump(3,false);
        check(name+"-10000-BUS-requests-dispatch-once",cursorSyncs==beforeSync+1);
        // Native movement may arrive after the touch callback. Moving the real
        // cursor without mouse input preserves suppression and tests BUS sync.
        POINT old{};GetCursorPos(&old);SetCursorPos(old.x-11,old.y-7);pump(3);
        CURSORINFO ci{sizeof(ci)};GetCursorInfo(&ci);RECT r{};if(cursorWindow)GetWindowRect(cursorWindow,&r);
        check(name+"-BUS-follows-delayed-external-cursor-position",cursorVisible && r.left+cursorHotspot.x==ci.ptScreenPos.x && r.top+cursorHotspot.y==ci.ptScreenPos.y);
        check(name+"-fallback-has-no-render-error",cursorError==0);
        // R2: Lift/re-touch between strokes before restoring the system cursor.
        contact(0,.65f,.5f,POINTER_FLAG_UP);check(name+"-lift-inject-up",inject(1));
        CURSORINFO lifted{sizeof(lifted)};GetCursorInfo(&lifted);
        samples.push_back(Json{{"mode",name},{"step","lift"},{"sample",sample()}});
        check(name+"-lift-really-leaves-system-cursor-suppressed",(lifted.flags&CURSOR_SUPPRESSED)!=0);
        check(name+"-lift-has-no-invisible-cursor-gap",!core.pads[0].down && !cursorTracking.load() && cursorRetained.load() && cursorVisible);
        const HWND reused=cursorWindow;
        contact(0,.3f,.5f,down);check(name+"-refinger-inject-down",inject(1));
        for(int n=1;n<=4;n++){contact(0,.3f+n*.02f,.5f,move);inject(1);}
        check(name+"-refinger-keeps-same-window-and-restarts-active-tracking",core.pads[0].down && cursorTracking.load() && !cursorRetained.load() && cursorWindow==reused && cursorVisible);
        // A true mouse move unhides the system cursor; hand it back without a
        // duplicate. This test-only input is not part of the production fallback.
        INPUT input{};input.type=INPUT_MOUSE;input.mi.dx=1;input.mi.dwFlags=MOUSEEVENTF_MOVE;input.mi.dwExtraInfo=kTestMouseTag;
        check(name+"-test-only-mouse-move-accepted",SendInput(1,&input,sizeof(input))==1);pump(120);
        samples.push_back(Json{{"mode",name},{"step","handoff"},{"sample",sample()}});CURSORINFO handoff{sizeof(handoff)};const bool handoffRead=GetCursorInfo(&handoff)!=FALSE;check(name+"-visible-system-cursor-handoff",handoffRead && (handoff.flags&CURSOR_SHOWING) && !cursorVisible);
        contact(0,.65f,.5f,POINTER_FLAG_UP);check(name+"-inject-up",inject(1));
        check(name+"-up-stops-fallback-and-BUS-posts",!cursorTracking.load() && !cursorVisible && !cursorSyncPending.load());
    }
    // One remaining independent Deck contact must retain the visual fallback.
    contact(0,.3f,.5f,down);contact(1,.7f,.5f,down);inject(2);
    for(int i=0;i<8;i++){contact(0,.3f+i*.02f,.5f,move);contact(1,.7f-i*.02f,.5f,move);inject(2);}
    check("two-Deck-contacts-both-active",core.pads[0].down && core.pads[1].down);
    contact(0,.4f,.5f,POINTER_FLAG_UP);contact(1,.6f,.5f,move);inject(2);
    check("one-Deck-up-other-keeps-fallback-tracking",!core.pads[0].down && core.pads[1].down && cursorTracking.load());
    SendMessageW(windows[1],WM_POINTERCAPTURECHANGED,core.pads[1].id,0);
    check("capture-loss-hides-and-clears-fallback",!cursorTracking.load() && !cursorVisible);
    contact(1,.6f,.5f,POINTER_FLAG_UP); // Already released first contact must not be sent again.
    touches[0]=touches[1];inject(1);
    release();
    for(Mode mode:{Mode::Wasd,Mode::DualSense}) {
        {std::lock_guard<std::mutex> lock(mutex);core.config.left=mode;core.pads[0].down=true;}
        cursorContactChanged(windows[0]);check(std::string(modeName(mode))+"-does-not-enable-mouse-cursor-fallback",!cursorTracking.load());release();
    }
    // Exercise shape upload/alpha including a classic monochrome system cursor.
    CURSORINFO synthetic{sizeof(synthetic)};synthetic.flags=CURSOR_SUPPRESSED;synthetic.ptScreenPos=original;
    for(const wchar_t* name:{IDC_ARROW,IDC_IBEAM,IDC_CROSS,IDC_WAIT}) {
        synthetic.hCursor=LoadCursorW(nullptr,name);check("system-cursor-shape-upload-"+std::to_string((uintptr_t)name),cursorUpload(synthetic));
    }
    cursorHide();cfg.enabled=false;configure(cfg);pump(15);const auto idleSyncs=cursorSyncs;
    const auto idleUploads=cursorUploads,events=eventCount;const auto start=std::chrono::steady_clock::now();
    for(int n=0;n<1000000;n++)snapshot();pump(10);
    check("disabled-million-snapshots-no-post-draw-or-event",idleSyncs==cursorSyncs && idleUploads==cursorUploads && events==eventCount);
    const auto disabledUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    check("overlay-never-steals-foreground",foreground==GetForegroundWindow());
    shutdown();check("shutdown-destroys-cursor-window-and-clears-tracking",!cursorWindow && !cursorTracking.load() && !cursorSyncPending.load());
    SetCursorPos(original.x,original.y);pump(10,false);
    const DWORD gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    check("no-GDI-object-leak",gdiAfter<=gdiBefore+2);
    bool ok=true;for(const auto& c:cases)ok=ok && c["ok"].get<bool>();
    Json result={{"ok",ok},{"trace",trace},{"cases",cases},{"samples",samples},{"disabledMillionSnapshotsUs",disabledUs},
        {"gdiBefore",gdiBefore},{"gdiAfter",gdiAfter},{"realTouchInjected",true},{"testOnlyMouseMoves",2},
        {"virtualTargetStarted",false},{"productionFallbackInjectsInput",false},
        {"mouseObservationAvailable",observation.hook!=nullptr},{"externalMouseMoves",externalMouseMoves},
        {"externalInjectedMouseMoves",externalInjectedMouseMoves},{"observedTestMouseMoves",testMouseMoves},
        {"status",ok?"passed":(externalMouseMoves || externalInjectedMouseMoves)?"failed-with-external-mouse-traffic":"failed"}};
    const char* path=argc>1?argv[1]:"screen-touchpads-cursor-selftest.json";std::ofstream out(path);out<<result.dump(2);
    std::cout<<(ok?"PASS ":"FAIL ")<<cases.size()<<" cases: "<<path<<"\n";
    for(const auto& c:cases)if(!c["ok"].get<bool>())std::cout<<c["name"]<<"\n";
    return ok?0:1;
}







