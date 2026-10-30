#pragma once
// Included after the existing keyboard raw-name observer. Hooks only observe and
// always forward; no OEM rule, key state, focus or device ownership is changed.
static HHOOK g_inputEdgeKeyboardHook=nullptr,g_inputEdgeMouseHook=nullptr;
static std::atomic<bool> g_inputEdgeHooksWanted{false};
static std::atomic<bool> g_inputEdgeKeyboardInstalled{false},g_inputEdgeMouseInstalled{false};
static std::atomic<DWORD> g_inputEdgeKeyboardError{0},g_inputEdgeMouseError{0};
static std::atomic<std::uint64_t> g_inputEdgeHookPostsFailed{0},g_inputEdgeLogFailures{0};
static std::atomic<std::uint64_t> g_inputEdgeHookCallbacks{0},g_inputEdgeRawCallbacks{0};
static std::atomic<ULONGLONG> g_inputEdgeLastKeyboardTick{0},g_inputEdgeLastMouseTick{0},g_inputEdgeHookReconcileTick{0};
static std::atomic<std::uint64_t> g_inputEdgeMouseMoves{0},g_inputEdgeUnicodeOmitted{0};
static ymcc::inputEdges::Event inputEdgeBase(ymcc::inputEdges::Channel channel) noexcept {
    ymcc::inputEdges::Event e{};e.channel=channel;e.tick=GetTickCount64();
    e.powerGeneration=g_powerGeneration.load(std::memory_order_acquire);
    e.deviceGeneration=g_keyboardModifierDeviceGeneration.load(std::memory_order_acquire);return e;
}
static void inputEdgeObserveRaw(const RAWINPUT& raw) noexcept {
    if(!g_inputEdgeEvents.enabled())return;
    const DWORD saved=GetLastError();
    if(raw.header.dwType==RIM_TYPEKEYBOARD){
        auto e=inputEdgeBase(ymcc::inputEdges::Channel::RawKeyboard);const auto& k=raw.data.keyboard;
        e.device=reinterpret_cast<std::uintptr_t>(raw.header.hDevice);e.extraInfo=k.ExtraInformation;
        e.code=k.VKey;e.scan=k.MakeCode;e.flags=k.Flags;e.message=k.Message;e.down=(k.Flags&RI_KEY_BREAK)==0;
        g_inputEdgeRawCallbacks.fetch_add(1,std::memory_order_relaxed);g_inputEdgeEvents.record(e);
    }else if(raw.header.dwType==RIM_TYPEMOUSE){
        const auto& m=raw.data.mouse;
        if(m.lLastX||m.lLastY)g_inputEdgeMouseMoves.fetch_add(1,std::memory_order_relaxed);
        if(m.usButtonFlags){auto e=inputEdgeBase(ymcc::inputEdges::Channel::RawMouse);
            e.device=reinterpret_cast<std::uintptr_t>(raw.header.hDevice);e.flags=m.usButtonFlags;
            e.extraInfo=m.ulExtraInformation;e.code=m.usButtonFlags;
            if(m.usButtonFlags&(RI_MOUSE_WHEEL|RI_MOUSE_HWHEEL))e.wheel=static_cast<SHORT>(m.usButtonData);
            g_inputEdgeRawCallbacks.fetch_add(1,std::memory_order_relaxed);g_inputEdgeEvents.record(e);}
    }
    SetLastError(saved);
}
static void inputEdgeObserveSelf(ymcc::keyboardEvents::SourceId source,const INPUT* inputs,UINT requested,UINT inserted) noexcept {
    if(!inputs||!g_inputEdgeEvents.enabled())return;
    const DWORD saved=GetLastError();const UINT inspected=(std::min)(requested,64u);
    for(UINT i=0;i<inspected;++i){const auto& input=inputs[i];
        auto e=inputEdgeBase(ymcc::inputEdges::Channel::SelfSend);e.source=static_cast<unsigned>(source);
        e.inputIndex=i;e.requested=requested;e.returned=inserted;
        if(input.type==INPUT_KEYBOARD){const auto& k=input.ki;e.flags=k.dwFlags;e.extraInfo=k.dwExtraInfo;
            if(k.dwFlags&KEYEVENTF_UNICODE){g_inputEdgeUnicodeOmitted.fetch_add(1,std::memory_order_relaxed);continue;}
            e.code=k.wVk;e.scan=k.wScan;e.down=(k.dwFlags&KEYEVENTF_KEYUP)==0;e.message=INPUT_KEYBOARD;
        }else if(input.type==INPUT_MOUSE){const auto& m=input.mi;
            if(m.dwFlags&MOUSEEVENTF_MOVE)g_inputEdgeMouseMoves.fetch_add(1,std::memory_order_relaxed);
            if(!(m.dwFlags&~(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|MOUSEEVENTF_MOVE_NOCOALESCE)))continue;
            e.code=m.dwFlags;e.flags=m.dwFlags;e.extraInfo=m.dwExtraInfo;e.message=INPUT_MOUSE;
            if(m.dwFlags&(MOUSEEVENTF_WHEEL|MOUSEEVENTF_HWHEEL))e.wheel=static_cast<LONG>(m.mouseData);
        }else continue;
        g_inputEdgeEvents.record(e);
    }
    SetLastError(saved);
}
struct InputEdgeForegroundOps {
    std::uintptr_t foreground() const noexcept {return reinterpret_cast<std::uintptr_t>(GetForegroundWindow());}
    bool foregroundPid(std::uintptr_t window,std::uint32_t& pid,std::uint32_t& error) const noexcept {
        DWORD value=0;SetLastError(ERROR_SUCCESS);
        const bool ok=GetWindowThreadProcessId(reinterpret_cast<HWND>(window),&value)!=0;
        pid=value;error=ok?ERROR_SUCCESS:GetLastError();return ok;
    }
};
static LRESULT CALLBACK inputEdgeKeyboardHookProc(int code,WPARAM message,LPARAM data) {
    if(code==HC_ACTION&&data&&g_inputEdgeEvents.enabled()){
        const DWORD saved=GetLastError();const auto& k=*reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        auto e=inputEdgeBase(ymcc::inputEdges::Channel::HookKeyboard);e.code=k.vkCode;e.scan=k.scanCode;
        e.flags=k.flags;e.extraInfo=k.dwExtraInfo;e.message=static_cast<DWORD>(message);e.eventTime=k.time;e.down=(k.flags&LLKHF_UP)==0;
        InputEdgeForegroundOps foregroundOps;ymcc::inputEdges::captureInjectedGamepadForeground(e,foregroundOps);
        if(k.vkCode==VK_PACKET){e.scan=0;g_inputEdgeUnicodeOmitted.fetch_add(1,std::memory_order_relaxed);}
        g_inputEdgeHookCallbacks.fetch_add(1,std::memory_order_relaxed);g_inputEdgeLastKeyboardTick.store(e.tick,std::memory_order_relaxed);g_inputEdgeEvents.record(e);
        // Reuse the ordinary aggregate only when no OEM callback supplies it.
        // The edge recorder above stays independent even if an OEM handle is stale.
        if(!g_oemBackHook)keyboardModifierObserveLowLevel(k,message);
        SetLastError(saved);
    }
    return CallNextHookEx(nullptr,code,message,data);
}
static LRESULT CALLBACK inputEdgeMouseHookProc(int code,WPARAM message,LPARAM data) {
    if(code==HC_ACTION&&data&&g_inputEdgeEvents.enabled()){
        const DWORD saved=GetLastError();const auto& m=*reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        if(message==WM_MOUSEMOVE)g_inputEdgeMouseMoves.fetch_add(1,std::memory_order_relaxed);
        else {auto e=inputEdgeBase(ymcc::inputEdges::Channel::HookMouse);e.message=static_cast<DWORD>(message);e.code=static_cast<DWORD>(message);
            e.flags=m.flags;e.extraInfo=m.dwExtraInfo;e.eventTime=m.time;
            if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL)e.wheel=static_cast<SHORT>(HIWORD(m.mouseData));
            if(message==WM_XBUTTONDOWN||message==WM_XBUTTONUP)e.scan=HIWORD(m.mouseData);
            g_inputEdgeHookCallbacks.fetch_add(1,std::memory_order_relaxed);g_inputEdgeLastMouseTick.store(e.tick,std::memory_order_relaxed);g_inputEdgeEvents.record(e);
            if(!g_oemMouseHook)inputSignalObserveMouse(m,message,false);}
        SetLastError(saved);
    }
    return CallNextHookEx(nullptr,code,message,data);
}
static void inputEdgeReconcileOnUi(bool destroying) noexcept {
    if(!destroying&&(!g_hwnd||GetCurrentThreadId()!=GetWindowThreadProcessId(g_hwnd,nullptr)))return;
    const DWORD saved=GetLastError();const bool wanted=!destroying&&g_inputEdgeHooksWanted.load(std::memory_order_acquire)&&!g_exitRequested.load();
    if(wanted){
        if(!g_inputEdgeKeyboardHook){g_inputEdgeKeyboardHook=SetWindowsHookExW(WH_KEYBOARD_LL,inputEdgeKeyboardHookProc,GetModuleHandleW(nullptr),0);g_inputEdgeKeyboardError.store(g_inputEdgeKeyboardHook?ERROR_SUCCESS:GetLastError());}
        if(!g_inputEdgeMouseHook){g_inputEdgeMouseHook=SetWindowsHookExW(WH_MOUSE_LL,inputEdgeMouseHookProc,GetModuleHandleW(nullptr),0);g_inputEdgeMouseError.store(g_inputEdgeMouseHook?ERROR_SUCCESS:GetLastError());}
    }else{
        if(g_inputEdgeKeyboardHook){if(UnhookWindowsHookEx(g_inputEdgeKeyboardHook))g_inputEdgeKeyboardHook=nullptr;else g_inputEdgeKeyboardError.store(GetLastError());}
        if(g_inputEdgeMouseHook){if(UnhookWindowsHookEx(g_inputEdgeMouseHook))g_inputEdgeMouseHook=nullptr;else g_inputEdgeMouseError.store(GetLastError());}
    }
    g_inputEdgeKeyboardInstalled.store(g_inputEdgeKeyboardHook!=nullptr);g_inputEdgeMouseInstalled.store(g_inputEdgeMouseHook!=nullptr);g_inputEdgeHookReconcileTick.store(GetTickCount64(),std::memory_order_release);SetLastError(saved);
}
static void inputEdgeUpdateGate(bool wanted,ULONGLONG now) noexcept {
    const DWORD saved=GetLastError();
    if(!g_inputEdgeEvents.enable(wanted)){SetLastError(saved);return;}
    const bool changed=g_inputEdgeHooksWanted.exchange(wanted,std::memory_order_acq_rel)!=wanted;
    static ULONGLONG lastPost=0;
    const bool missing=wanted&&(!g_inputEdgeKeyboardInstalled.load()||!g_inputEdgeMouseInstalled.load());
    if(changed||(missing&&(!lastPost||now-lastPost>=5000))){
        lastPost=now;
        if(g_hwnd&&GetCurrentThreadId()==GetWindowThreadProcessId(g_hwnd,nullptr))inputEdgeReconcileOnUi(false);
        else if(!g_hwnd||!PostMessageW(g_hwnd,WM_INPUT_EVIDENCE_RECONCILE,0,0))g_inputEdgeHookPostsFailed.fetch_add(1,std::memory_order_relaxed);
    }
    SetLastError(saved);
}
struct InputEdgeDrainReceipt {
    bool attempted=false,drained=false,enabled=false,emissionAttempted=false;
    std::uint64_t drainId=0,events=0,overwritten=0,contention=0,lastSequence=0;
    const char* status="not-attempted";
};
static std::mutex g_inputEdgeDrainMutex;
static std::uint64_t g_inputEdgeDrainSequence=0;
static std::atomic<std::uint64_t> g_inputEdgeDrainContention{0};
static json inputEdgeDrainReceiptJson(const InputEdgeDrainReceipt& r) {
    return {{"attempted",r.attempted},{"drained",r.drained},{"enabled",r.enabled},{"emissionAttempted",r.emissionAttempted},{"drainId",r.drainId},{"events",r.events},{"overwritten",r.overwritten},{"recorderContention",r.contention},{"lastEventSequence",r.lastSequence},{"status",r.status},
        {"drainOwnerContentionSinceProcessStart",g_inputEdgeDrainContention.load()},{"durableWriteProven",false},{"scope","This export invocation only. Join drainId to focus-trace.log input-edge-window;missing row or failed drain is a coverage gap,not absence of input."}};
}
static void inputEdgeFlushEvidence(const json& idle,ULONGLONG now,bool force=false,InputEdgeDrainReceipt* receipt=nullptr) noexcept {
    const DWORD saved=GetLastError();
    if(receipt){*receipt={};receipt->attempted=true;}
    try{
        std::unique_lock<std::mutex> drainOwner(g_inputEdgeDrainMutex,std::try_to_lock);
        if(!drainOwner){g_inputEdgeDrainContention.fetch_add(1,std::memory_order_relaxed);if(receipt)receipt->status="drain-owner-contended";SetLastError(saved);return;}
        const auto snapshot=g_inputEdgeEvents.take();
        if(receipt){receipt->drained=snapshot.drained;receipt->enabled=snapshot.enabled;receipt->events=snapshot.count;receipt->overwritten=snapshot.overwritten;receipt->contention=snapshot.contention;receipt->lastSequence=snapshot.count?snapshot.events[snapshot.count-1].sequence:0;}
        if(!snapshot.drained){if(receipt)receipt->status="recorder-contended";SetLastError(saved);return;}
        static bool previousKeyboard=false,previousMouse=false,haveCoverage=false;
        const bool keyboard=g_inputEdgeKeyboardInstalled.load(),mouse=g_inputEdgeMouseInstalled.load();
        const bool coverageChanged=!haveCoverage||keyboard!=previousKeyboard||mouse!=previousMouse;
        if(!snapshot.enabled&&!snapshot.count&&!snapshot.discardedOnConfig){if(receipt)receipt->status="diagnostics-disabled";SetLastError(saved);return;}
        if(!force&&!coverageChanged&&!snapshot.count&&!snapshot.overwritten&&!snapshot.contention&&!snapshot.discardedOnConfig){if(receipt)receipt->status="empty-unchanged-window";SetLastError(saved);return;}
        auto events=json::array();auto devices=json::array();unsigned nameBudget=4;
        std::array<std::uintptr_t,4> named{};std::size_t namedCount=0;
        for(std::size_t i=0;i<snapshot.count;++i){const auto& e=snapshot.events[i];
            const char* channel=e.channel==ymcc::inputEdges::Channel::HookKeyboard?"passive-keyboard-hook":e.channel==ymcc::inputEdges::Channel::RawKeyboard?"raw-keyboard":e.channel==ymcc::inputEdges::Channel::HookMouse?"passive-mouse-hook":e.channel==ymcc::inputEdges::Channel::RawMouse?"raw-mouse":"YMCC-self-send-return";
            events.push_back({{"channel",channel},{"tick",e.tick},{"sequence",e.sequence},{"powerGeneration",e.powerGeneration},{"deviceGeneration",e.deviceGeneration},
                {"deviceHandle",e.device},{"code",e.code},{"scan",e.scan},{"flags",e.flags},{"message",e.message},{"eventTime32",e.eventTime},{"extraInfo",e.extraInfo},
                {"down",e.down},{"wheelDelta",e.wheel},{"sourceId",e.source},{"requestedInputs",e.requested},{"returnedInputs",e.returned},{"inputIndex",e.inputIndex}});
            if(e.foreground.attempted){const auto& f=e.foreground;
                events.back()["injectedGamepadForeground"]={{"version",1},{"capturePhase","event-callback"},
                    {"windowBefore",f.before},{"windowAfter",f.after},{"pidKnown",f.pidKnown},
                    {"foregroundPid",f.pidKnown?json(f.pid):json(nullptr)},
                    {"foregroundPidReadError",f.before&&!f.pidKnown?json(f.error):json(nullptr)},
                    {"sameWindowAcrossPidQuery",f.before&&f.pidKnown?json(f.before==f.after):json(nullptr)},
                    {"senderPidProven",false},{"consumerReceiptProven",false}};
            }
            if(e.device&&namedCount<named.size()&&std::find(named.begin(),named.begin()+namedCount,e.device)==named.begin()+namedCount){
                ymcc::keyboardEvents::Event origin{};origin.device=e.device;origin.deviceGeneration=e.deviceGeneration;origin.tick=e.tick;
                devices.push_back({{"deviceHandle",e.device},{"observed",keyboardModifierRawNameAtDrain(origin,nameBudget)}});named[namedCount++]=e.device;
            }
        }
        const auto drainId=++g_inputEdgeDrainSequence;
        json row={{"schemaVersion",1},{"drainId",drainId},{"drainTick",now},{"configEpoch",snapshot.epoch},{"enabled",snapshot.enabled},{"events",std::move(events)},{"rawDeviceNamesAtDrain",std::move(devices)},
            {"capacity",ymcc::inputEdges::capacity},{"overwrittenOldest",snapshot.overwritten},{"droppedContention",snapshot.contention},{"discardedOnConfig",snapshot.discardedOnConfig},{"coalescedRepeatDowns",snapshot.repeats},
            {"passiveKeyboardHookInstalledCached",keyboard},{"passiveMouseHookInstalledCached",mouse},{"keyboardHookInstallError",g_inputEdgeKeyboardError.load()},{"mouseHookInstallError",g_inputEdgeMouseError.load()},
            {"lastKeyboardHookTick",g_inputEdgeLastKeyboardTick.load()},{"lastMouseButtonHookTick",g_inputEdgeLastMouseTick.load()},{"hookCallbacksSinceProcessStart",g_inputEdgeHookCallbacks.load()},{"rawCallbacksSinceProcessStart",g_inputEdgeRawCallbacks.load()},
            {"hookPostFailures",g_inputEdgeHookPostsFailed.load()},{"logFailures",g_inputEdgeLogFailures.load()},{"mouseMoveObservationsCoordinatesOmitted",g_inputEdgeMouseMoves.load()},{"selfUnicodeInputsContentsOmitted",g_inputEdgeUnicodeOmitted.load()},
            {"hookFlagMeaning","keyboard:1=extended,2=lower-IL-injected,16=injected,128=up;mouse:1=injected,2=lower-IL-injected;flags-are-not-sender-PID"},
            {"selfReturnMeaning","count-based prefix assumption only;not OS or consumer acceptance;inspection capped at64"},
            {"coverage","diagnostic-only passive LL callbacks, raw keyboard/mouse and observed YMCC own send returns;upstream hooks may suppress;Windows can silently remove timed-out hooks;installed handle is not liveness proof"},
            {"sequenceScope","callback-arrival order within this writer;not hardware,network or cross-process ordering"},{"completeSystemCoverage",false},{"externalSenderPidKnown",false},{"unicodePacketScanContentsOmitted",true},{"textDecoded",false},{"mouseCoordinatesRetained",false}};
        for(const char* key:{"hostRunId","hostEpoch","hostPersona","hostProcessPresent","samplePowerGeneration","recentStopSignalContext"})if(idle.contains(key))row[key]=idle[key];
        if(receipt){receipt->drainId=drainId;receipt->emissionAttempted=true;receipt->status="emission-attempted-no-sink-ack";}
        appendDomainLog("focus","input-edge-window",row);previousKeyboard=keyboard;previousMouse=mouse;haveCoverage=true;
    }catch(...){g_inputEdgeLogFailures.fetch_add(1,std::memory_order_relaxed);if(receipt)receipt->status="diagnostic-exception";}
    SetLastError(saved);
}