#pragma once
#include "steam_controller_cursor_policy.h"
#include "steam_controller_cursor_windows.h"
#include <algorithm>
#include <map>

// Included after game_steam_context.h. Association comes only from the public
// game/Steam join; this consumer never opens Steam logs or discovers a session.
namespace SteamControllerCursor {
inline constexpr UINT message=WM_APP+976;
inline constexpr UINT_PTR timer=0x5A49;
struct Entry {
    ymcc::steamcursor::Budget budget;
    HANDLE process=nullptr,steamProcess=nullptr;
    DWORD pid=0,steamPid=0;ULONGLONG birth=0,steamBirth=0;
    HANDLE exitWait=nullptr;
    std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> steamIdentity;
    bool confirmed=false,wakePending=false,mouseAwaitingPower=false;
    ~Entry(){if(exitWait)UnregisterWaitEx(exitWait,INVALID_HANDLE_VALUE);if(process)CloseHandle(process);if(steamProcess)CloseHandle(steamProcess);}
};
static std::map<std::pair<DWORD,ULONGLONG>,Entry> entries;
static Entry* current=nullptr;
static std::atomic<bool> enabled{true},signalPending{false};
// The low bit marks an in-flight pad message; every UI ticket is even.
static constexpr ULONGLONG padNoticePosted=1;
static std::atomic<ULONGLONG> padTicket{0};
static ULONGLONG currentTicket=0,suspendSerial=0;
static bool running=false,ready=false,suspended=false;
static ymcc::sleeppower::SharedPowerV1 powerScope;
static bool (*powerCurrent)(const ymcc::sleeppower::SharedPowerV1&)=nullptr;
static HWND gameWindow=nullptr;
static POINT mousePoint{};static bool mousePointKnown=false;
static ymcc::steamcursor::CursorVisual visual;
static unsigned long long checks=0,associationRequests=0,mouseVetoes=0,hideAttempts=0;
static void signal() noexcept {
    if(!enabled.load(std::memory_order_relaxed)||!g_hwnd||g_exitRequested.load())return;
    if(!signalPending.exchange(true)&&!PostMessageW(g_hwnd,message,0,0))signalPending.store(false);
}
static void retirePadTicket() noexcept {padTicket.store(0,std::memory_order_release);currentTicket+=2;}
static void stopTimer() noexcept {if(g_hwnd)KillTimer(g_hwnd,timer);retirePadTicket();}
static bool projectedPowerChanged=false;
static void projectPower(const ymcc::sleeppower::SharedPowerV1& state,ULONGLONG now) noexcept {
    const bool known=state.size==sizeof(state)&&state.version==1&&state.generation&&(state.flags&ymcc::sleeppower::kKnown);
    const bool wasReady=ready;
    suspended=known&&state.phase==2;
    // The serial belongs to the sole owner, not the delivered message. Consume
    // it before a mouse veto, including while Resuming, so late Ready cannot
    // refund mouse input that already belongs to this wake.
    const bool newer=known&&state.suspendSerial>suspendSerial;
    if(newer){
        suspendSerial=state.suspendSerial;
        for(auto& [identity,entry]:entries){
            entry.budget.reset(now);entry.wakePending=true;
            if(entry.mouseAwaitingPower)entry.budget.mouse(now);
        }
        retirePadTicket();
    }
    ready=known&&ymcc::sleeppower::powerAdmitted(state)&&state.suspendSerial>=suspendSerial;
    powerScope=ready?state:ymcc::sleeppower::SharedPowerV1{};
    if(ready&&(newer||!wasReady))for(auto& [identity,entry]:entries)if(entry.wakePending){
        // Start the 60-second clock at Ready, preserving any mouse/focus veto
        // observed since this new serial. Time asleep spends no hot budget.
        if(!entry.budget.mouseStopped)entry.budget.reset(now);
        entry.wakePending=false;entry.mouseAwaitingPower=false;
    }
    projectedPowerChanged=projectedPowerChanged||newer||wasReady!=ready;
}
static bool refreshPower(ULONGLONG) noexcept {
    // Read only the current proof supplied by the sole publisher. A missed or
    // superseded projection closes admission; it never invents another wake.
    const bool wasReady=ready;
    if(ready&&(!powerCurrent||!powerCurrent(powerScope)))ready=false;
    const bool changed=projectedPowerChanged||wasReady!=ready;
    projectedPowerChanged=false;return changed;
}
static void withdraw() noexcept {
    stopTimer();visual.restore();
    // A failed OS restore does not abandon its cleanup obligation. The same
    // mouse observer can retry; it never restarts the terminated game budget.
    inputEdgeSetCursorMouseWanted(visual.hidden()&&!g_oemMouseHook);
    gameWindow=nullptr;current=nullptr;
}
static void mouse() noexcept {
    if((!running||!enabled.load(std::memory_order_relaxed))&&!visual.hidden())return;
    const auto now=GetTickCount64();bool changed=false;
    refreshPower(now);
    if(current){
        if(!ready)current->mouseAwaitingPower=true;
        if(!current->budget.mouseStopped){current->budget.mouse(now);changed=true;}
    }
    if(changed)++mouseVetoes;
    // This callback always forwards input; restoration does not move the cursor.
    withdraw();
}
static void CALLBACK processExited(void* entry,BOOLEAN) {
    if(g_hwnd)PostMessageW(g_hwnd,message,4,reinterpret_cast<LPARAM>(entry));
}
static void arm(ULONGLONG now) noexcept {
    KillTimer(g_hwnd,timer);
    if(!current||current->budget.mouseStopped||!ready||!enabled.load())return;
    if(!current->budget.padSeen){
        if((padTicket.load(std::memory_order_acquire)&~padNoticePosted)!=currentTicket)
            padTicket.store(currentTicket,std::memory_order_release);
        // A pending restore is the only reason to wake without controller input.
        if(!visual.hidden()||visual.restore())return;
    }else padTicket.store(0,std::memory_order_release);
    const auto remaining=current->budget.due>now?current->budget.due-now:1;
    SetTimer(g_hwnd,timer,static_cast<UINT>((std::min)(remaining,30000ULL)),nullptr);
}
static void context() {
    const auto now=GetTickCount64();const bool powerChanged=refreshPower(now);
    if(!running||!enabled.load()||!g_mouseForegroundHook||g_exitRequested.load()){withdraw();return;}
    if(!ready){stopTimer();visual.restore();if(suspended){withdraw();return;}}
    const auto observation=g_gameValveObservation.load(std::memory_order_acquire);
    const auto window=GetForegroundWindow();DWORD pid=0;
    if(window)GetWindowThreadProcessId(window,&pid);
    if(!observation||observation->discoveryState!=NativeGameDiscoveryState::Complete||!pid||
        observation->game.pid!=pid||!observation->game.processCreated){if(current)current->budget.end();withdraw();return;}
    const auto key=std::make_pair(pid,observation->game.processCreated);
    if(current&&(current->pid!=pid||current->birth!=key.second||gameWindow!=window)){current->budget.end();withdraw();}
    auto found=entries.find(key);
    if(found==entries.end()){
        if(!ready){withdraw();return;}
        // One lifetime pin, no new process inventory. PID reuse gets a new budget.
        HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!process)return;
        if(nativeProcessCreatedFromHandle(process)!=key.second||WaitForSingleObject(process,0)!=WAIT_TIMEOUT){CloseHandle(process);return;}
        for(auto it=entries.begin();it!=entries.end();)if(WaitForSingleObject(it->second.process,0)==WAIT_OBJECT_0)it=entries.erase(it);else ++it;
        found=entries.try_emplace(key).first;auto& enrolled=found->second;enrolled.process=process;enrolled.pid=pid;enrolled.birth=key.second;enrolled.budget.reset(now);
        if(!RegisterWaitForSingleObject(&enrolled.exitWait,process,processExited,&enrolled,INFINITE,WT_EXECUTEONLYONCE)){
            enrolled.budget.end();return;
        }
    }
    auto& entry=found->second;
    if(entry.budget.mouseStopped||WaitForSingleObject(entry.process,0)!=WAIT_TIMEOUT){withdraw();return;}
    const bool observerUnchanged=g_oemMouseHook?!g_inputEdgeCursorMouseWanted.load(std::memory_order_relaxed):
        g_inputEdgeMouseHook&&g_inputEdgeCursorMouseWanted.load(std::memory_order_relaxed);
    if(current==&entry&&gameWindow==window&&observerUnchanged&&!powerChanged)return; // Repeated identity/title notices do not rearm the timer.
    if(current!=&entry||gameWindow!=window){if(current)current->budget.end();visual.restore();retirePadTicket();current=&entry;gameWindow=window;mousePointKnown=GetCursorPos(&mousePoint)!=FALSE;}
    // Prefer the existing OEM mouse observer. Otherwise share the diagnostics
    // hook with a mouse-only lease; never install another feature-specific hook.
    inputEdgeSetCursorMouseWanted(!g_oemMouseHook);
    // No hiding without the event path that can immediately undo it.
    if(!g_oemMouseHook&&!g_inputEdgeMouseHook){withdraw();return;}
    arm(now);
}
static void tick() {
    KillTimer(g_hwnd,timer);
    const auto now=GetTickCount64();refreshPower(now);
    if(!current||!enabled.load()||g_exitRequested.load()){withdraw();return;}
    if(!ready){stopTimer();visual.restore();if(suspended)withdraw();return;}
    auto* entry=current;
    if(entry->budget.mouseStopped){withdraw();return;}
    DWORD pid=0;const auto window=GetForegroundWindow();if(window)GetWindowThreadProcessId(window,&pid);
    const auto observation=g_gameValveObservation.load(std::memory_order_acquire);
    const bool foreground=window==gameWindow&&observation&&observation->discoveryState==NativeGameDiscoveryState::Complete&&
        observation->game.pid==pid&&pid==entry->pid&&observation->game.processCreated==entry->birth&&
        WaitForSingleObject(entry->process,0)==WAIT_TIMEOUT;
    if(!foreground){entry->budget.end();withdraw();return;}
    if(!entry->budget.check(now)){arm(now);return;}
    ++checks;
    // No controller operation means no Steam/file query at all. The existing
    // physical sample can satisfy this later without another input reader.
    if(!entry->budget.padSeen){arm(now);return;}
    const auto identity=g_steamSettingsObserver.identitySnapshot();
    const auto client=identity?identity->session:ymcc::steamsession::Session{};
    if(entry->confirmed&&(!identity||!g_steamSettingsObserver.sameIdentityEpoch(entry->steamIdentity)||
        client.pid!=entry->steamPid||client.created!=entry->steamBirth||
        WaitForSingleObject(entry->steamProcess,0)!=WAIT_TIMEOUT)){entry->confirmed=false;visual.restore();}
    if(!entry->confirmed&&identity&&identity->presence==1&&client.pid&&client.created&&!client.root.empty()){
        NativeGameSelectionReceipt selected;
        const bool selectedCurrent=nativeValveReadCachedSelection(&selected)&&selected.target.game.pid==entry->pid&&
            selected.target.game.processCreated==entry->birth&&nativeValveSelectionCurrent(selected);
        json association=json::object();
        if(selectedCurrent){
            const json expected={{"game",{{"pid",entry->pid},{"processCreated",std::to_string(entry->birth)},
                {"path",W2U(selected.target.game.path)},{"identityGeneration",selected.target.identityGeneration}}}};
            association=nativeGameSteamContextGet(expected);
            if(association.value("steam",json::object()).value("status",std::string())!="cached-association"){
                // Reuse the public single-flight queue. The existing five-second
                // budget owns the next observation; no UI wait or new timer.
                ++associationRequests;nativeGameSteamContextRefresh(expected);
            }
        }
        const auto fact=association.value("steam",json::object());
        // Historical tracker membership alone never replaces the original
        // native-primary requirement. Consume the common owner's bounded,
        // freshness-checked receipt; do not perform another native/log query.
        const auto link=steamGameLinkGet();
        const auto linkedGame=link.value("originalGame",json::object());
        const auto linkedSteam=link.value("steamClient",json::object());
        const bool nativePrimary=link.value("active",false)&&link.value("nativePrimaryExact",false)&&
            link.value("reason",std::string())!="current-link-read-not-fresh"&&
            linkedGame.value("pid",DWORD{0})==entry->pid&&linkedGame.value("created",std::string())==std::to_string(entry->birth)&&
            linkedSteam.value("pid",DWORD{0})==client.pid&&linkedSteam.value("created",std::string())==std::to_string(client.created);
        const bool confirmed=selectedCurrent&&fact.value("status",std::string())=="cached-association"&&
            nativePrimary&&
            fact.value("steamPid",DWORD{0})==client.pid&&fact.value("steamCreated",std::string())==std::to_string(client.created)&&
            g_steamSettingsObserver.sameIdentityEpoch(identity)&&nativeValveSelectionCurrent(selected);
        if(confirmed){
            if(entry->steamProcess)CloseHandle(entry->steamProcess);
            entry->steamProcess=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,client.pid);
            entry->confirmed=entry->steamProcess&&nativeProcessCreatedFromHandle(entry->steamProcess)==client.created&&WaitForSingleObject(entry->steamProcess,0)==WAIT_TIMEOUT;
            entry->steamPid=client.pid;entry->steamBirth=client.created;entry->steamIdentity=identity;
        }
    }
    if(entry->budget.mayHide(now,foreground,entry->confirmed,enabled.load(),ready)&&powerCurrent&&powerCurrent(powerScope)){
        ++hideAttempts;visual.hide(gameWindow);
        // Guard startup and User32 can run across a foreground/input change.
        // Recheck after the write so no late result owns the desktop cursor.
        if(current!=entry||entry->budget.mouseStopped||GetForegroundWindow()!=gameWindow||
           !powerCurrent||!powerCurrent(powerScope)||!g_steamSettingsObserver.sameIdentityEpoch(entry->steamIdentity)){
            entry->budget.end();withdraw();return;
        }
    }else visual.restore();
    // Only a confirmed Steam association enters the long-lived state. Unknown
    // games cannot keep parsing Steam logs every 30 seconds indefinitely.
    if(!entry->confirmed&&now>=entry->budget.started+60000){entry->budget.end();withdraw();return;}
    arm(now);
}
static void power(const ymcc::sleeppower::SharedPowerV1& state) {
    projectPower(state,GetTickCount64());
    context();
}
static void configure(const json& document) {
    const auto ui=document.find("ui");
    const bool wanted=ui==document.end()||!ui->is_object()||!ui->contains("steamControllerCursorHide")||(*ui)["steamControllerCursorHide"]!=false;
    const bool changed=enabled.exchange(wanted)!=wanted;
    if(changed&&g_hwnd)PostMessageW(g_hwnd,message,3,0);
}
static void start() {
    if(running||g_aiFanMockSession.isolated)return;running=true;
    configure(ymSettingsRead());signal(); // Existing shared publication supplies admission.
}
static void shutdown() noexcept {
    running=false;withdraw();visual.close();entries.clear();
}
static void deliver(WPARAM reason,LPARAM value) {
    if(reason==0)signalPending.store(false,std::memory_order_release);
    if(!running)return;
    if(reason==1){
        const auto ticket=static_cast<ULONGLONG>(value);
        auto posted=ticket|padNoticePosted;
        if(ticket!=currentTicket||!padTicket.compare_exchange_strong(posted,0,std::memory_order_acq_rel))return;
        const auto now=GetTickCount64();refreshPower(now);
        if(!current||!ready||!enabled.load()||g_exitRequested.load())return;
        if(GetForegroundWindow()!=gameWindow){current->budget.end();withdraw();return;}
        current->budget.gamepad();
        arm(now);
        return;
    }
    if(reason==4){
        for(auto it=entries.begin();it!=entries.end();++it)if(reinterpret_cast<LPARAM>(&it->second)==value&&WaitForSingleObject(it->second.process,0)==WAIT_OBJECT_0){
            if(current==&it->second){current->budget.end();withdraw();}
            entries.erase(it);break;
        }
        return;
    }
    context();
}
} // namespace SteamControllerCursor

static void steamControllerCursorSignal() noexcept {SteamControllerCursor::signal();}
static void steamControllerCursorForegroundChanged() noexcept {
    // Existing foreground WinEvent delivery, no second focus subscription.
    if(SteamControllerCursor::running&&SteamControllerCursor::current&&GetForegroundWindow()!=SteamControllerCursor::gameWindow){
        SteamControllerCursor::current->budget.end();SteamControllerCursor::withdraw();
    }
    SteamControllerCursor::signal();
}
static void steamControllerCursorMouseHook(WPARAM kind,const POINT& point) noexcept {
    if(!SteamControllerCursor::current&&!SteamControllerCursor::visual.hidden())return;
    struct PreserveError{DWORD saved=GetLastError();~PreserveError(){SetLastError(saved);}} preserveError;
    if(kind==WM_MOUSEMOVE){
        const bool changed=SteamControllerCursor::mousePointKnown&&(point.x!=SteamControllerCursor::mousePoint.x||point.y!=SteamControllerCursor::mousePoint.y);
        SteamControllerCursor::mousePoint=point;SteamControllerCursor::mousePointKnown=true;
        if(changed)SteamControllerCursor::mouse();return;
    }
    if(kind==WM_LBUTTONDOWN||kind==WM_LBUTTONUP||kind==WM_RBUTTONDOWN||kind==WM_RBUTTONUP||
       kind==WM_MBUTTONDOWN||kind==WM_MBUTTONUP||kind==WM_XBUTTONDOWN||kind==WM_XBUTTONUP||kind==WM_MOUSEWHEEL||kind==WM_MOUSEHWHEEL)SteamControllerCursor::mouse();
}
static void steamControllerCursorPad(bool live,const XINPUT_GAMEPAD& pad) noexcept {
    using namespace SteamControllerCursor;
    const auto ticket=padTicket.load(std::memory_order_relaxed);
    if(!ticket||(ticket&padNoticePosted))return;
    const ymcc::steamcursor::PadSample sample{pad.wButtons,pad.sThumbLX,pad.sThumbLY,pad.sThumbRX,pad.sThumbRY,pad.bLeftTrigger,pad.bRightTrigger};
    static ymcc::steamcursor::PadSample previous;
    static ULONGLONG retryTicket=0,retryAt=0;
    const bool activity=live&&sample!=previous&&ymcc::steamcursor::padActive(sample);
    if(!activity){previous=sample;return;}
    if(retryTicket==ticket&&GetTickCount64()<retryAt)return;
    auto available=ticket;
    const auto posted=ticket|padNoticePosted;
    if(!padTicket.compare_exchange_strong(available,posted,std::memory_order_acq_rel))return;
    if(g_hwnd&&PostMessageW(g_hwnd,message,1,static_cast<LPARAM>(ticket))){previous=sample;retryTicket=0;return;}
    // A failed post must not lose the only wake or reopen an obsolete ticket.
    auto failed=posted;
    padTicket.compare_exchange_strong(failed,ticket,std::memory_order_acq_rel);
    retryTicket=ticket;retryAt=GetTickCount64()+50;
}
static void steamControllerCursorConfigure(const json& document){SteamControllerCursor::configure(document);}
static void steamControllerCursorShutdown() noexcept {SteamControllerCursor::shutdown();}
