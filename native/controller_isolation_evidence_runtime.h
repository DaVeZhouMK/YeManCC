#pragma once

static json controllerIsolationPhysicalEvidence() noexcept {
    const DWORD saved=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{saved};
    try {
        const auto begin=GetTickCount64();
        std::unique_lock sampleLock(g_inputCaptureGamepadSnapshotMx,std::try_to_lock);
        if(!sampleLock.owns_lock())return {{"captured",false},{"reason","snapshot-contended"}};
        const auto& sample=g_inputCaptureGamepadSnapshot;
        const auto end=GetTickCount64();
        return {{"captured",true},{"captureBeginTick",begin},{"captureEndTick",end},
            {"sampleSequence",sample.readAttemptSequence},{"inputSource",sample.inputSource},{"live",sample.live},
            {"readStartedTick",sample.readStartedTick},{"readCompletedTick",sample.readCompletedTick},
            {"readAgeMs",sample.readCompletedTick&&end>=sample.readCompletedTick?json(end-sample.readCompletedTick):json(nullptr)},
            {"readContextStable",sample.readContextStable},{"powerGeneration",sample.readPowerGeneration},
            {"readWindowCurrent",ymcc::virtualOutputSourceCurrent({sample.readAttemptSequence,sample.readStartedTick,
                sample.readCompletedTick,sample.readPowerGeneration,sample.readTopologyGeneration,sample.readContextStable},
                end,currentPowerGeneration(),g_xinputTopologyEpoch.load(std::memory_order_acquire))&&
                sample.readIdentityGeneration==g_xinputSlotIdentityEpoch.load(std::memory_order_acquire)},
            {"topologyGeneration",sample.readTopologyGeneration},{"identityGeneration",sample.readIdentityGeneration},
            {"currentPowerGeneration",currentPowerGeneration()},{"currentTopologyGeneration",g_xinputTopologyEpoch.load(std::memory_order_acquire)},
            {"currentIdentityGeneration",g_xinputSlotIdentityEpoch.load(std::memory_order_acquire)},
            {"primarySlot",sample.primarySlot},{"connectedMask",sample.connectedMask},
            {"buttons",sample.pad.wButtons},{"guide",sample.specialXbox},
            {"lt",sample.pad.bLeftTrigger},{"rt",sample.pad.bRightTrigger},
            {"lx",sample.pad.sThumbLX},{"ly",sample.pad.sThumbLY},{"rx",sample.pad.sThumbRX},{"ry",sample.pad.sThumbRY},
            {"gameReleaseReceiptProven",false},{"scope","existing-canonical-cache-only;no-device-read-or-game-receipt"}};
    }catch(...){return nullptr;}
}

static json controllerIsolationWindowEvidence(HWND window) noexcept {
    const DWORD saved=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{saved};
    try {
        json result={{"hwnd",reinterpret_cast<std::uintptr_t>(window)},{"valid",window&&IsWindow(window)}};
        if(!window||!IsWindow(window))return result;
        DWORD pid=0;const auto thread=GetWindowThreadProcessId(window,&pid);
        RECT bounds{},frame{};DWORD cloaked=0;
        const bool haveBounds=GetWindowRect(window,&bounds)!=FALSE;
        const auto cloakResult=DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        const auto frameResult=DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame));
        MONITORINFO monitor{sizeof(monitor)};
        const bool haveMonitor=GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE;
        RECT intersection{};
        result.update({{"pid",pid},{"thread",thread},{"visibleFlag",IsWindowVisible(window)!=FALSE},
            {"iconic",IsIconic(window)!=FALSE},{"enabled",IsWindowEnabled(window)!=FALSE},
            {"style",static_cast<unsigned long long>(GetWindowLongPtrW(window,GWL_STYLE))},
            {"exStyle",static_cast<unsigned long long>(GetWindowLongPtrW(window,GWL_EXSTYLE))},
            {"parent",reinterpret_cast<std::uintptr_t>(GetParent(window))},
            {"owner",reinterpret_cast<std::uintptr_t>(GetWindow(window,GW_OWNER))},
            {"boundsRead",haveBounds},{"bounds",json::array({bounds.left,bounds.top,bounds.right,bounds.bottom})},
            {"positiveArea",haveBounds&&bounds.right>bounds.left&&bounds.bottom>bounds.top},
            {"intersectsMonitor",haveBounds&&haveMonitor&&IntersectRect(&intersection,&bounds,&monitor.rcMonitor)!=FALSE},
            {"cloakHresult",static_cast<long long>(cloakResult)},{"cloaked",SUCCEEDED(cloakResult)?json(cloaked):json(nullptr)},
            {"frameHresult",static_cast<long long>(frameResult)},
            {"frameBounds",SUCCEEDED(frameResult)?json::array({frame.left,frame.top,frame.right,frame.bottom}):json(nullptr)},
            {"screenVisibleVerified",false}});
        if(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED){
            COLORREF key=0;BYTE alpha=0;DWORD flags=0;
            const bool read=GetLayeredWindowAttributes(window,&key,&alpha,&flags)!=FALSE;
            result["layered"]={{"attributesRead",read},{"alpha",read?json(alpha):json(nullptr)},
                {"flags",read?json(flags):json(nullptr)}};
        }
        return result;
    }catch(...){return nullptr;}
}

static json controllerIsolationPresentationEvidence() noexcept {
    const DWORD saved=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{saved};
    try {
        const auto binding=g_gameOverlay.snapshot();
        const HWND display=binding?binding->carrier:g_hwnd;
        const HWND foreground=GetForegroundWindow();
        json result={{"main",controllerIsolationWindowEvidence(g_hwnd)},
            {"display",controllerIsolationWindowEvidence(display)},{"foreground",controllerIsolationWindowEvidence(foreground)},
            {"overlayActive",binding!=nullptr},{"foregroundIsMain",foreground&&GetAncestor(foreground,GA_ROOT)==g_hwnd},
            {"foregroundIsTarget",binding&&foreground&&GetAncestor(foreground,GA_ROOT)==binding->target},
            {"screenVisibleVerified",false},{"crossSourceAtomic",false}};
        if(binding)result["overlay"]={{"session",binding->session},{"originalUiToken",binding->originalUiToken},
            {"originalUiInput",binding->originalUiInput},{"visualProbe",binding->visualProbe},{"globalProbe",binding->globalProbe},
            {"target",reinterpret_cast<std::uintptr_t>(binding->target)},{"targetPid",binding->pid},
            {"targetBirth",binding->birth},{"power",binding->power},{"classInputReady",g_gameOverlay.inputReady()}};
        RECT bounds{};
        if(display&&IsWindow(display)&&GetWindowRect(display,&bounds)){
            struct Scan {HWND display;RECT bounds;json above=json::array();unsigned visited=0;bool found=false,failed=false;};
            Scan scan{display,bounds};
            EnumWindows([](HWND candidate,LPARAM raw)->BOOL {
                auto& scan=*reinterpret_cast<Scan*>(raw);
                try {
                    if(++scan.visited>128)return FALSE;
                    if(candidate==scan.display){scan.found=true;return FALSE;}
                    RECT candidateBounds{},overlap{};
                    if(scan.above.size()>=8||!IsWindowVisible(candidate)||IsIconic(candidate)||
                            !GetWindowRect(candidate,&candidateBounds)||!IntersectRect(&overlap,&scan.bounds,&candidateBounds))return TRUE;
                    DWORD pid=0;GetWindowThreadProcessId(candidate,&pid);wchar_t className[128]{};
                    GetClassNameW(candidate,className,static_cast<int>(std::size(className)));
                    scan.above.push_back({{"hwnd",reinterpret_cast<std::uintptr_t>(candidate)},{"pid",pid},{"class",W2U(className)},
                        {"exStyle",static_cast<unsigned long long>(GetWindowLongPtrW(candidate,GWL_EXSTYLE))},
                        {"overlap",json::array({overlap.left,overlap.top,overlap.right,overlap.bottom})}});
                    return TRUE;
                }catch(...){scan.failed=true;return FALSE;}
            },reinterpret_cast<LPARAM>(&scan));
            result["zOrder"]={{"displayFound",scan.found},{"visited",scan.visited},{"truncated",!scan.found&&scan.visited>128},
                {"captureFailed",scan.failed},{"above",std::move(scan.above)},{"pixelOcclusionProven",false}};
        }
        return result;
    }catch(...){return nullptr;}
}

static void controllerIsolationSummonEvidence(const char* phase,unsigned long long generation,bool targetCaptured) noexcept {
    const DWORD saved=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{saved};
    try {
        const auto game=g_gameValveObservation.load(std::memory_order_acquire);
        json view={{"ready",g_webviewReady},{"controllerPresent",g_ctrl!=nullptr},{"viewPresent",g_view!=nullptr}};
        if(g_ctrl){
            BOOL visible=FALSE;RECT bounds{};
            const auto visibleResult=g_ctrl->get_IsVisible(&visible),boundsResult=g_ctrl->get_Bounds(&bounds);
            view.update({{"visibleHresult",static_cast<long long>(visibleResult)},
                {"visible",SUCCEEDED(visibleResult)?json(visible!=FALSE):json(nullptr)},
                {"boundsHresult",static_cast<long long>(boundsResult)},
                {"bounds",SUCCEEDED(boundsResult)?json::array({bounds.left,bounds.top,bounds.right,bounds.bottom}):json(nullptr)}});
        }
        appendNativeLifecycleLog("controller-isolation-boundary",{{"schema",1},{"phase",phase},{"summonGeneration",generation},
            {"targetCaptured",targetCaptured},{"isolationEnabled",g_controllerFrontendIsolationEnabled.load(std::memory_order_acquire)},
            {"gameOpenLatched",g_focusIsolationGameOpen.load(std::memory_order_acquire)},
            {"gameObservationAvailable",game!=nullptr},{"gamePid",game?game->game.pid:0},
            {"gameObservationGeneration",game?game->generation:0},{"physical",controllerIsolationPhysicalEvidence()},
            {"presentation",controllerIsolationPresentationEvidence()},{"webview",std::move(view)},
            {"keyReleaseProven",false},{"screenVisibleVerified",false}});
    }catch(...){}
}

static void controllerIsolationOverlayRendererProbe() noexcept {
    const DWORD saved=GetLastError();
    struct RestoreError {DWORD value;~RestoreError(){SetLastError(value);}} restoreError{saved};
    try {
        const auto binding=g_gameOverlay.snapshot();
        if(!binding||!g_view)return;
        const auto session=binding->session,requestedAt=GetTickCount64(),power=currentPowerGeneration();
        const auto viewGeneration=g_webviewGeneration.load(std::memory_order_acquire);
        static constexpr wchar_t script[]=LR"((()=>{const a=document.getElementById('app'),b=document.body,r=a?.getBoundingClientRect(),s=a?getComputedStyle(a):null;return JSON.stringify({readyState:document.readyState,visibility:document.visibilityState,hasFocus:document.hasFocus(),app:!!a,body:!!b,children:a?.childElementCount??0,width:innerWidth,height:innerHeight,pixelRatio:devicePixelRatio,appBounds:r?{x:r.x,y:r.y,width:r.width,height:r.height}:null,appStyle:s?{display:s.display,visibility:s.visibility,opacity:s.opacity}:null})})())";
        const auto dispatch=g_view->ExecuteScript(script,Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [session,requestedAt,power,viewGeneration](HRESULT status,LPCWSTR value)->HRESULT {
                const DWORD callbackError=GetLastError();
                struct RestoreCallbackError {DWORD value;~RestoreCallbackError(){SetLastError(value);}} restoreCallbackError{callbackError};
                try {
                    const auto current=g_gameOverlay.snapshot();json renderer=nullptr;const char* parse="unavailable";
                    if(SUCCEEDED(status)&&value){
                        if(wcsnlen_s(value,8193)<=8192){
                            auto outer=json::parse(W2U(value));renderer=outer.is_string()?json::parse(outer.get<std::string>()):outer;
                            parse="parsed";
                        }else parse="oversize";
                    }
                    appendNativeLifecycleLog("game-overlay-surface",{{"phase","renderer-replied"},{"session",session},
                        {"requestedTick",requestedAt},{"replyTick",GetTickCount64()},{"hresult",static_cast<long long>(status)},
                        {"parse",parse},{"renderer",std::move(renderer)},
                        {"currentSession",current&&current->session==session&&currentPowerGeneration()==power&&
                            g_webviewGeneration.load(std::memory_order_acquire)==viewGeneration},
                        {"screenVisibleVerified",false}});
                }catch(...){
                    try{appendNativeLifecycleLog("game-overlay-surface",{{"phase","renderer-reply-invalid"},{"session",session},
                        {"requestedTick",requestedAt},{"replyTick",GetTickCount64()},
                        {"hresult",static_cast<long long>(status)},{"screenVisibleVerified",false}});}catch(...){}
                }
                return S_OK;
            }).Get());
        appendNativeLifecycleLog("game-overlay-surface",{{"phase","renderer-requested"},{"session",session},
            {"requestedTick",requestedAt},{"hresult",static_cast<long long>(dispatch)},{"screenVisibleVerified",false}});
    }catch(...){}
}
