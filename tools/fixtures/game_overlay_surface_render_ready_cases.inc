// Ports for unrelated startup/input work; the render-ready decision is extracted verbatim.
#include "startup_readiness.h"
static int g_firstShowCmd=SW_SHOWNORMAL;
static std::atomic<unsigned long long> g_webviewGeneration=7;
static unsigned long long g_webviewNavigationGeneration=7,g_webviewCompletedNavigationId=11,g_webviewNavigationId=11;
static unsigned long long g_webviewRenderReadyGeneration=0,g_webviewRenderReadyNavigationId=0;
static bool g_webviewNavigationReady=true,g_webviewRouteReady=true,g_gamepadStatePublished=true,g_webviewNeedsShowNudge=false;
static std::string g_webviewRouteStatus="route-ready";
static std::atomic<ymcc::StartupInputState> g_startupInputState=ymcc::StartupInputState::Ready;
static std::atomic<bool> g_emptyTelemetryHiddenHint=false;
static constexpr UINT WEBVIEW_RENDER_READY_TIMER_ID=31,WEBVIEW_POST_SHOW_NUDGE_TIMER_ID=32;
static unsigned long long g_focusSessionSequence=0;
static unsigned g_summonFocusRetries=0;
static bool g_summonAltTried=false;
static unsigned regularReveals=0;
static void traceLog(const char*,...){}
static void gamepadDriverSetupRequestRead(){}
static void gamepadReadState(){}
static void signalUpdateHandshake(){}
static void appendWebViewDiagnostic(const json&){}
static void completeWebViewRecovery(){}
static void recordHealthyWebViewGpuBoot(){}
static bool focusLightMayRefocus(ULONGLONG){return true;}
static bool focusSummonRefocus(bool){return true;}
static void showWindowAnimated(HWND window,int cmd,bool){++regularReveals;ShowWindow(window,cmd);g_ctrl->put_IsVisible(TRUE);}
#include "finalize-consumer.inc"

static void prepareRecovery(bool firstShow,int showCmd=SW_SHOWNORMAL){
    ShowWindow(g_hwnd,SW_RESTORE);ShowWindow(g_hwnd,SW_HIDE);
    g_webviewReady=false;g_deferFirstShow=firstShow;g_firstShowCmd=showCmd;
    g_webviewNeedsShowNudge=false;controller.visible=false;regularReveals=0;
    KillTimer(g_hwnd,WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
    g_mainWindowWasMaximized=false;refuseRoute=false;ordinaryRoute=false;
    summonForeground=g_hwnd;summonTick=200;shortcutSummonClearRenderWait();
}
static const FocusTargetSnapshot currentTarget(){return {g_hwnd,GetCurrentProcessId(),true};}
static void ready(){finalizeWebViewRenderReady(7,11,"fixture-render-ready");}
static bool ownsVisibleController(){return g_gameOverlay.active()&&g_gameOverlay.inputReady()&&g_gameOverlaySurface&&IsWindowVisible(g_gameOverlaySurface)&&GetParent(child)==g_gameOverlaySurface&&controller.visible;}
static bool occupiesWorkArea(){RECT actual{},wanted{};return g_gameOverlaySurface&&GetWindowRect(g_gameOverlaySurface,&actual)&&ymcc::windowlayout::safeMonitorArea(MonitorFromWindow(g_gameOverlaySurface,MONITOR_DEFAULTTONEAREST),wanted)&&EqualRect(&actual,&wanted);}

template<class Test>static void runReadinessCases(Test test){
    test("hidden-main-does-not-hide-controller-after-replay",[]{
        prepareRecovery(false);
        need(nativeYmccShortcutSummon(currentTarget(),true)&&g_pendingShortcutSummon,"accepted while renderer blocked");
        summonTick+=2300;ready();
        need(g_gameOverlay.active()&&occupiesWorkArea(),"actual maximize replay reached native surface");
        need(ownsVisibleController()&&!gameOverlayMainPresented(g_hwnd)&&regularReveals==0&&!g_webviewNeedsShowNudge,"hidden main must not hide overlay controller or schedule ordinary nudge");
    });
    test("deferred-first-show-follows-accepted-overlay",[]{
        prepareRecovery(true,SW_MAXIMIZE);
        need(nativeYmccShortcutSummon(currentTarget())&&g_pendingShortcutSummon,"pending action before scheduled startup show");
        summonTick+=1200;ready();
        need(ownsVisibleController()&&occupiesWorkArea()&&!gameOverlayMainPresented(g_hwnd)&&regularReveals==0,"one maximized floating owner retains startup geometry");
    });
    test("matching-minimized-maximized-recovery-keeps-main-iconic",[]{
        prepareRecovery(false);
        ShowWindow(g_hwnd,SW_SHOWMINNOACTIVE);g_mainWindowWasMaximized=true;
        need(IsIconic(g_hwnd)&&nativeYmccShortcutSummon(currentTarget())&&g_pendingShortcutSummon->maximize,"iconic HWND captures original maximize");
        ready();ready();
        need(ownsVisibleController()&&occupiesWorkArea()&&IsIconic(g_hwnd)&&!g_pendingShortcutSummon,"single recovery retains maximum layout without restoring main");
        need(nativeYmccShortcutSummon(currentTarget())&&!g_gameOverlay.active()&&IsIconic(g_hwnd)&&GetParent(child)==g_hwnd,"same input returns the borrowed controller without raising main");
    });
    test("normal-first-show-unchanged-without-request",[]{
        prepareRecovery(true);
        ready();
        need(gameOverlayMainPresented(g_hwnd)&&!g_gameOverlay.active()&&regularReveals==1&&controller.visible&&g_webviewNeedsShowNudge,"standalone startup unchanged");
    });
    test("expired-request-does-not-replace-startup",[]{
        prepareRecovery(true);
        need(nativeYmccShortcutSummon(currentTarget()),"request accepted temporarily");
        summonTick+=ymcc::kSummonIntentLifetimeMs+1;ready();
        need(gameOverlayMainPresented(g_hwnd)&&!g_gameOverlay.active()&&regularReveals==1&&!g_pendingShortcutSummon,"expired queued request must not present");
    });
    test("refused-surface-returns-to-scheduled-startup",[]{
        prepareRecovery(true);
        need(nativeYmccShortcutSummon(currentTarget()),"request accepted before fault");
        attachFailures=1;ready();
        need(gameOverlayMainPresented(g_hwnd)&&!g_gameOverlay.active()&&regularReveals==1&&!g_gameOverlaySurface&&GetParent(child)==g_hwnd,"no leaked owner on failed attachment; scheduled desktop fallback remains");
    });
    test("ordinary-summon-with-overlay-off-reveals-only-once",[]{
        prepareRecovery(true,SW_MAXIMIZE);ordinaryRoute=true;
        need(nativeYmccShortcutSummon(currentTarget()),"request queued in independent-window mode");
        const auto shows=ordinaryShows;ready();
        need(gameOverlayMainPresented(g_hwnd)&&IsZoomed(g_hwnd)&&!g_gameOverlay.active(),"independent window remains available and maximized");
        need(ordinaryShows+regularReveals==shows+1&&controller.visible,"one ordinary reveal and a visible controller");
    });
    test("wrong-navigation-does-not-consume-request",[]{
        prepareRecovery(false);
        need(nativeYmccShortcutSummon(currentTarget()),"request queued in matching navigation");
        finalizeWebViewRenderReady(6,11,"wrong-generation");
        finalizeWebViewRenderReady(7,10,"wrong-navigation");
        need(g_pendingShortcutSummon&&!g_webviewReady&&!g_gameOverlay.active(),"wrong generation and navigation cannot consume request");
        shortcutSummonClearRenderWait();ready();
        need(!gameOverlayMainPresented(g_hwnd)&&!g_gameOverlay.active()&&!controller.visible,"explicit close still cancels a pending request");
    });
}
