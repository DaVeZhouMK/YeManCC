#pragma once
#include "game_overlay_panel_receiver.h"
#include "game_overlay_display_tree_lease.h"
// The existing UI/browser is presented by one independent noactivate window.
// Production summon and optional local observation share this exact receiver.
static void gameOverlaySurfaceRecord(const char* phase,const json& facts=json::object()) noexcept {
    try{
    auto data=facts;data["phase"]=phase;
    const auto b=g_gameOverlay.snapshot();
    data["presentation"]=controllerIsolationPresentationEvidence();
    data["physical"]=controllerIsolationPhysicalEvidence();
    data["pointerInteractive"]=b&&b->pointerInteractive;
    data["controllerReady"]=g_gameOverlay.inputReady();
    data["screenVisibleVerified"]=false;
    data["displayReceiverSeparated"]=true;
    appendNativeLifecycleLog("game-overlay-surface",data);
    }catch(...){}
}
// Independent monitor-style HWND on the existing UI thread. The original main
// HWND stays hidden and its ordinary show/focus callbacks never host this view.
static ymcc::gameoverlayinput::PanelReceiver g_gameOverlayPanel;
static ymcc::gameoverlayinput::DisplayTreeLease g_gameOverlayDisplayTree;
// Peers keep their own visibility/input lifetime. Only repair confirmed overlap
// above this current surface; unrelated windows and notification HUDs are excluded.
static bool gameOverlaySurfacePeerOccluded() noexcept {
    const HWND display=g_gameOverlaySurface;
    if(!display||!IsWindowVisible(display)||!g_gameOverlayPanel.ready())return false;
    struct Check{HWND display;RECT bounds;bool occluded=false;} check{display,{}};
    if(!GetWindowRect(display,&check.bounds))return false;
    EnumWindows([](HWND h,LPARAM value)->BOOL{
        auto& current=*reinterpret_cast<Check*>(value);
        if(h==current.display)return FALSE;
        if(!IsWindowVisible(h)||!gameOverlayOwnPresentationPeer(h))return TRUE;
        RECT peer{},intersection{};
        if(GetWindowRect(h,&peer)&&IntersectRect(&intersection,&current.bounds,&peer)){
            current.occluded=true;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&check));
    return check.occluded;
}
static bool gameOverlaySurfaceRestore(){
    const HWND surface=g_gameOverlaySurface;
    if(!surface)return true;
    g_gameOverlayPanel.close();
    g_mainWebViewComposition.setBackgroundInput(false);
    if(g_controllerEditorWidth.active()){
        try{endControllerEditorWidth(g_controllerEditorWidth.owner);}catch(...){g_controllerEditorWidth={};}
    }
    if(g_ctrl)g_ctrl->put_IsVisible(FALSE);
    ShowWindow(surface,SW_HIDE);
    const bool treeRestored=g_gameOverlayDisplayTree.restore();
    if(!treeRestored)gameOverlaySurfaceRecord("display-child-restore-incomplete");
    const HRESULT restored=g_mainWebViewComposition.retarget(g_hwnd);
    gameOverlaySurfaceRecord("surface-return",{{"originalParentRestored",SUCCEEDED(restored)},{"hresult",static_cast<std::int64_t>(restored)}});
    if(FAILED(restored)||!treeRestored)return false; // Keep ownership for a bounded explicit retry.
    if(!DestroyWindow(surface)&&IsWindow(surface))return false;
    g_gameOverlaySurfaceAttached=false;g_gameOverlaySurface=nullptr;return true;
}
static LRESULT CALLBACK gameOverlaySurfaceProc(HWND h,UINT m,WPARAM w,LPARAM l){
    LRESULT result=0;
    if(g_gameOverlay.protectInput(h,m,w,l,result))return result;
    if(g_gameOverlay.handle(h,m,w,l))return 0;
    // The monitor display and every browser helper stay inert. Input arrives
    // only on the independent per-pixel panel, never through this root.
    if(m==WM_PARENTNOTIFY&&LOWORD(w)==WM_CREATE){if(!g_gameOverlayDisplayTree.protect()&&g_gameOverlay.active())gameOverlayStop("display-child-policy-unavailable");return 0;}
    if(m==WM_NCHITTEST)return HTTRANSPARENT;
    if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_SETFOCUS||m==WM_KILLFOCUS)return 0;
    if(m==WM_ACTIVATE){if(LOWORD(w)!=WA_INACTIVE&&g_gameOverlay.active())gameOverlayStop("independent-surface-unexpected-activation");return 0;}
    if(m==WM_WINDOWPOSCHANGED){if(g_gameOverlay.active()&&!(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_TOPMOST))g_gameOverlay.requestCheck();}
    if(m==WM_DISPLAYCHANGE||m==WM_DPICHANGED||m==WM_SETTINGCHANGE){g_gameOverlay.displayChanged();return 0;}
    if(m==WM_CLOSE){gameOverlayStop("independent-surface-close");return 0;}
    if(m==WM_SIZE&&g_gameOverlaySurfaceAttached&&h==g_gameOverlaySurface&&g_ctrl){RECT r{};GetClientRect(h,&r);g_ctrl->put_Bounds(r);g_gameOverlayPanel.synchronize();ipc_emit("window.resized",{{"w",r.right},{"h",r.bottom}});return 0;}
    if(m==WM_ERASEBKGND)return 1;
    return DefWindowProcW(h,m,w,l);
}
static bool gameOverlaySurfaceShow(const FocusTargetSnapshot& target,ymcc::gameoverlay::Binding binding,bool observation,std::function<void()> ended={}){
  try{
    const HWND foregroundAtEntry=GetAncestor(GetForegroundWindow(),GA_ROOT);
    if(g_gameOverlay.active()||(g_focusSession.active&&!binding.originalUiInput)||IsWindowVisible(g_hwnd)||!g_webviewReady||!g_ctrl||!g_view||!g_mainWebViewComposition.active())return false;
    if(radialHasInputOwner()||customSteamLibraryInputSuppressed()||g_shortcutRecordingActive.load())return false;
    if(!ymcc::presentationpower::capture(&readSharedSystemPower,binding.sharedPower)||
       binding.power!=binding.sharedPower.generation)return false;
    // A failed return retains its hidden HWND. One new explicit summon may
    // retry that return before borrowing the same controller again; never poll.
    if(g_gameOverlaySurface&&!gameOverlaySurfaceRestore())return false;
    binding.detachedPointer=true;
    const auto returnBinding=std::make_shared<const ymcc::gameoverlay::Binding>(binding);
    WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=gameOverlaySurfaceProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC_IndependentOverlaySurface";
    if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    RECT base{};if(!GetWindowRect(g_hwnd,&base))return false;
    HWND display=CreateWindowExW(static_cast<DWORD>(ymcc::hudinput::requiredExStyle)|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"YeManCC Overlay",WS_POPUP|WS_DISABLED,base.left,base.top,base.right-base.left,base.bottom-base.top,nullptr,nullptr,cls.hInstance,nullptr);
    if(!display)return false;
    if(!ymcc::hudinput::ensure(display)||!ymcc::hudinput::registerTouchPassthrough(display)){DestroyWindow(display);return false;}
    g_gameOverlaySurface=display;
    g_gameOverlayDisplayTree.begin(display);
    const HRESULT moved=g_mainWebViewComposition.retarget(display);
    if(FAILED(moved)){g_gameOverlayDisplayTree.restore();DestroyWindow(display);g_gameOverlaySurface=nullptr;gameOverlaySurfaceRecord("surface-move-failed",{{"hresult",static_cast<std::int64_t>(moved)}});return false;}
    g_gameOverlaySurfaceAttached=true;g_mainWebViewComposition.setBackgroundInput(true);
    if(!g_gameOverlayDisplayTree.protect()||!g_gameOverlayPanel.attach(display,g_mainWebViewComposition)){gameOverlaySurfaceRestore();return false;}
    g_gameOverlayReturn.invalidate();g_gameOverlayReturning.store({});
    const bool begun=g_gameOverlay.begin(display,binding,{
        [target,display](const auto& current){return current.power==currentPowerGeneration()&&
            !g_exitRequested.load()&&g_webviewReady&&g_ctrl&&g_view&&g_gameOverlaySurfaceAttached&&g_gameOverlaySurface==display&&
            !radialHasInputOwner()&&!customSteamLibraryInputSuppressed()&&(current.originalUiInput||!g_shortcutRecordingActive.load())&&
            !IsWindowVisible(g_hwnd)&&(current.visualProbe?
                ymcc::presentationpower::current(&readSharedSystemPower,current.sharedPower):gameOverlayBindingCurrent(current))&&
            (current.globalProbe||focusTargetIdentityMatches(target,target.pid));},
        [target,display,base]{
            if(g_controllerEditorWidth.active())return applyControllerEditorWidth();
            if(g_fullHeight)return applyFullHeightLayout(MonitorFromWindow(target.hwnd?target.hwnd:display,MONITOR_DEFAULTTONEAREST));
            MONITORINFO info{sizeof(info)};
            if(!GetMonitorInfoW(MonitorFromWindow(target.hwnd?target.hwnd:display,MONITOR_DEFAULTTONEAREST),&info))return false;
            const int width=std::min<int>(base.right-base.left,info.rcWork.right-info.rcWork.left);
            const int height=std::min<int>(base.bottom-base.top,info.rcWork.bottom-info.rcWork.top);
            return SetWindowPos(display,nullptr,windowPlacementIsLeft()?info.rcWork.left:info.rcWork.right-width,info.rcWork.top,width,height,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
        },
        [display]{
            RECT bounds{};
            if(!g_ctrl||!GetClientRect(display,&bounds)||bounds.right<=0||bounds.bottom<=0)return false;
            if(FAILED(g_ctrl->put_Bounds(bounds))||FAILED(g_ctrl->put_IsVisible(TRUE)))return false;
            g_ctrl->NotifyParentWindowPositionChanged();InvalidateRect(display,nullptr,FALSE);
            return g_gameOverlayDisplayTree.protect()&&g_gameOverlayPanel.synchronize();
        },
        [display,returnBinding,observation,ended=std::move(ended)](bool active,const char* reason){
            if(active){
                g_deferFirstShow=false;closeSplash();
                if(observation&&g_view)g_view->ExecuteScript(returnBinding->visualProbe?
                    LR"((()=>{document.getElementById('ymcc-visibility-probe')?.remove();const b=document.createElement('div');b.id='ymcc-visibility-probe';b.textContent='鼠标 / 触摸验证 · 手柄隔离未就绪 · Ctrl+Alt+F10 收回';b.style.cssText='position:fixed;bottom:10px;left:50%;transform:translateX(-50%);z-index:2147483647;background:#102433;color:#bce2ff;border:1px solid #64b3ee;padding:6px 10px;font-size:12px;white-space:nowrap;pointer-events:none';document.body.appendChild(b)})())":
                    LR"((()=>{document.getElementById('ymcc-visibility-probe')?.remove();const b=document.createElement('div');b.id='ymcc-visibility-probe';b.textContent='鼠标 / 触摸 / 手柄交互验证 · Ctrl+Alt+F10 收回';b.style.cssText='position:fixed;bottom:10px;left:50%;transform:translateX(-50%);z-index:2147483647;background:#102433;color:#bce2ff;border:1px solid #64b3ee;padding:6px 10px;font-size:12px;white-space:nowrap;pointer-events:none';document.body.appendChild(b)})())",nullptr);
            }else{
                if(!returnBinding->originalUiInput&&!returnBinding->visualProbe){g_gameOverlayReturning.store(returnBinding);g_gameOverlayReturn.begin(returnBinding->power,GetTickCount64());g_gameOverlayReturn.end(returnBinding->power,GetTickCount64());}
                try{if(ended)ended();}catch(...){gameOverlaySurfaceRecord("end-callback-failed");}
                try{if(observation&&g_view)g_view->ExecuteScript(L"document.getElementById('ymcc-visibility-probe')?.remove()",nullptr);}catch(...){}
                try{gameOverlaySurfaceRestore();}catch(...){gameOverlaySurfaceRecord("surface-return-exception");}
            }
            // Publish semantic ownership without an ordinary main-window focus/show.
            const auto session=g_gameOverlay.snapshot();
            ipc_emit("window.overlayPresentation",{{"active",active},{"session",active&&session?std::to_string(session->session):std::string()},{"candidate",true},{"reason",reason}});
            ipc_emit(active?"window.shown":"window.hidden",json::object());
            gameOverlaySurfaceRecord(active?"preparing":"hidden",{{"reason",reason},{"independentSurface",true},{"originalMainVisible",IsWindowVisible(g_hwnd)!=FALSE},{"carrierVisible",IsWindow(display)&&IsWindowVisible(display)!=FALSE},{"screenVisibleVerified",false}});
        },
        []{return g_gameOverlayDisplayTree.protect();},
        []{return g_gameOverlayPanel.synchronize();},
        []{return gameOverlaySurfacePeerOccluded();}
    });
    if(!begun&&g_gameOverlaySurface)gameOverlaySurfaceRestore();
    const HWND foregroundAfter=GetAncestor(GetForegroundWindow(),GA_ROOT);
    gameOverlaySurfaceRecord(begun?"presented":"begin-rejected",{{"foregroundAtEntry",reinterpret_cast<std::uintptr_t>(foregroundAtEntry)},
        {"foregroundAfter",reinterpret_cast<std::uintptr_t>(foregroundAfter)},{"target",reinterpret_cast<std::uintptr_t>(target.hwnd)},
        {"sameForeground",foregroundAtEntry==foregroundAfter},{"displayInert",begun&&ymcc::hudinput::inert(display)},
        {"panelReady",g_gameOverlayPanel.ready()},{"panelRetired",g_gameOverlayPanel.retired()}});
    if(begun)controllerIsolationOverlayRendererProbe();
    return begun;
  }catch(...){
    if(g_gameOverlay.active())gameOverlayStop("independent-surface-setup-failed");
    else if(g_gameOverlaySurface){try{gameOverlaySurfaceRestore();}catch(...){}}
    return false;
  }
}

// A failed parent restore retains its hidden HWND. Release it only after the
// original controller-close path has removed the controller and visual.
static void gameOverlaySurfaceControllerClosed(){
    if(g_gameOverlay.active()||g_mainWebViewComposition.active()||g_ctrl)return;
    g_gameOverlayPanel.close();
    g_gameOverlayDisplayTree.restore();
    const auto surface=g_gameOverlaySurface;
    g_gameOverlaySurface=nullptr;g_gameOverlaySurfaceAttached=false;
    if(surface&&IsWindow(surface))DestroyWindow(surface);
}
