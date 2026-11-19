#pragma once
// Included inside ymcc::screenpads, after Core/globals. This is a visual-only
// fallback only for contacts explicitly configured as direct Mouse.
// Native Deck/PS5 contacts never grant software cursor display or retention.
// No ShowCursor counter changes, mouse injection, input hook, timer or worker.
// A scoped WinEvent observer handles visibility edges; it does not drive movement.
#include "screen_touchpad_cursor_visibility.h"
inline constexpr UINT kCursorSyncMessage=WM_APP+174;
inline std::atomic<bool> cursorTracking{false},cursorRetained{false},cursorSyncPending{false};
inline constexpr ULONGLONG kCursorHandoffMs=80,kCursorRetainedPollMs=24;
inline std::atomic<ULONGLONG> cursorNextRetainedPoll{0},cursorNextActivePoll{0};
inline constexpr ULONGLONG kCursorActiveFallbackMs=24; // Mouse/edges update immediately; Deck MOVE shares the existing BUS deadline.
inline constexpr ULONGLONG kCursorMaxHandoffSampleGapMs=3*kCursorActiveFallbackMs;
inline std::atomic<bool> cursorDeckOnlyTracking{false}; // UI writes; BUS reads only to order the existing display request.
inline ULONGLONG cursorDeckNextPositionTick=0; // Shared by periodic and visibility-triggered positioning.
// Published on the existing BUS after-report request, at most once per slot.
// This proves only that observation lane is alive, not Steam consumption.
inline std::atomic<ULONGLONG> cursorDeckReportPulseTick{0};
inline constexpr ULONGLONG kCursorDeckMoveFallbackMs=2*kCursorActiveFallbackMs;
inline unsigned long long cursorDeckMovePostReportDeferrals=0,cursorDeckMoveFallbackRequests=0; // UI-owned.
inline unsigned long long cursorDeckSystemHandoffs=0;
// Deck contact is asynchronous. Observe only existing samples; admit a new
// fallback after real OS movement or a shape confirmed on this contact. A new
// stationary contact with unknown shape must not resurrect an old bitmap.
inline bool cursorDeckMotionPointKnown=false,cursorDeckSuppressedMotion=false;
inline POINT cursorDeckMotionPoint{};
// A valid shape observed on this contact can bridge touch suppression before
// Steam moves the mouse. An old bitmap or a later visibility hint cannot arm it.
inline HCURSOR cursorDeckContactShape=nullptr;
inline unsigned long long cursorDeckContactShapeBridgeShows=0;
// Retired hidden-state override counters remain for diagnostic compatibility.
inline unsigned long long cursorDeckForcedHiddenSamples=0,cursorDeckForcedShows=0;
inline unsigned long long cursorDeckStationaryDeferrals=0,cursorDeckMotionStarts=0;
inline void cursorResetDeckMotion() {
    cursorDeckMotionPointKnown=false;cursorDeckSuppressedMotion=false;cursorDeckMotionPoint={};cursorDeckContactShape=nullptr;
}
inline void cursorObserveDeckMotion(const CURSORINFO& ci) {
    const bool suppressed=(ci.flags&CURSOR_SUPPRESSED) && !(ci.flags&CURSOR_SHOWING);
    if(!suppressed){cursorDeckSuppressedMotion=false;cursorDeckContactShape=nullptr;}
    else if(cursorDeckContactShape && ci.hCursor)cursorDeckContactShape=ci.hCursor; // Refresh an already-authorized contact, never arm from a hint.
    if(suppressed && cursorDeckMotionPointKnown && (ci.ptScreenPos.x!=cursorDeckMotionPoint.x || ci.ptScreenPos.y!=cursorDeckMotionPoint.y)) {
        if(!cursorDeckSuppressedMotion)++cursorDeckMotionStarts;
        cursorDeckSuppressedMotion=true;
    }
    cursorDeckMotionPoint=ci.ptScreenPos;cursorDeckMotionPointKnown=true;
}
inline bool cursorShowingObserved=false;
inline ULONGLONG cursorShowingSince=0,cursorShowingLastSample=0;
inline unsigned cursorShowingSamples=0;
inline std::atomic<HWND> cursorDispatchWindow{nullptr};
inline HWND cursorWindow=nullptr;
inline HCURSOR cursorShape=nullptr;
inline POINT cursorHotspot{};
inline SIZE cursorSize{};
inline bool cursorVisible=false,cursorPositionValid=false;
inline bool cursorImageUploaded=false,cursorDeckPositionOnlyEnabled=true;
inline bool cursorLayeredResetPending=false,cursorLayeredResetting=false; // UI-owned surface repair only.
inline unsigned long long cursorLayeredResets=0,cursorLayeredResetFailures=0;
inline unsigned long long cursorDeckPositionOnlyMoves=0,cursorDeckPositionFallbacks=0;
inline unsigned long long cursorDeckPresentRechecks=0,cursorDeckPresentDeferrals=0;
inline DWORD cursorDeckPositionLastError=0;
inline POINT cursorPosition{};
inline unsigned long long cursorUploads=0,cursorSyncs=0,cursorQueries=0,cursorShows=0,cursorHides=0,cursorHandoffs=0;
inline unsigned long long cursorDeckDeferredMoves=0,cursorFlagTransitions=0;
// Counters cover the SetWindowPos branch too: zero position-only uploads does
// not establish a stationary cursor when z-order recovery uses that branch.
inline unsigned long long cursorWindowPlacements=0,cursorRaisePlacements=0,cursorWindowPlacementFailures=0;
inline unsigned long long cursorPositionUnchanged=0;
inline unsigned long long cursorStackRaises=0,cursorStackRaiseFailures=0;
inline HWND cursorLastRaisePredecessor=nullptr; // Observed handle, not a durable window identity.
inline ULONGLONG cursorLastRaiseTick=0;
inline unsigned long long cursorQueryFailures=0,cursorHandoffGapResets=0;
inline DWORD cursorLastFlags=0;
inline bool cursorFlagsObserved=false; // Sampled states only; not a trace of every OS transition.
inline DWORD cursorError=0;
enum class CursorSampleSource : BYTE { Periodic, Contact, Release, Visibility, Lifecycle, Preparation };
// Bounded window context for an actual SHOWING -> flags0 -> SHOWING pair.
// UI-owned, two captures per Deck gesture at most. It never changes ownership,
// pixels, focus or input. This is window context, not a cursor API caller trace.
namespace cursorcontext {
struct Window { ULONG_PTR handle=0;DWORD thread=0,pid=0;BYTE kind=0; };
struct Entry { ULONGLONG tick=0,gesture=0,sequence=0;POINT point{};DWORD flags=0;ULONG_PTR shape=0;
    CursorSampleSource source=CursorSampleSource::Periodic;bool recovery=false;Window hit{},root{},foreground{};ULONG_PTR foregroundAfter=0; };
inline std::array<Entry,16> entries{};
inline unsigned long long count=0,gestures=0;
inline bool enabled=false,sawShowing=false,hiddenCaptured=false,recoveryCaptured=false;
inline void end() { enabled=false;sawShowing=false;hiddenCaptured=false;recoveryCaptured=false; }
inline void begin(bool deck) { end();enabled=deck;if(deck)++gestures; }
inline BYTE windowKind(HWND h) {
    wchar_t name[64]{};if(!h||GetClassNameW(h,name,64)<=0)return 0;
    constexpr const wchar_t* names[]={L"Progman",L"WorkerW",L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",
        L"SHELLDLL_DefView",L"SysListView32",L"SDL_app",L"Chrome_WidgetWin_1",L"CabinetWClass",
        L"QQ",L"YeManControlledTouchChild",L"YeManScreenTouchpad",L"YeManScreenTouchpadOutline",
        L"YeManScreenTouchpadCursor",L"YeManScreenTouchContact",L"YeManScreenButton"};
    for(BYTE i=0;i<sizeof(names)/sizeof(names[0]);++i)if(std::wcscmp(name,names[i])==0)return i+1;
    return 0; // Never export arbitrary window names, titles or text.
}
inline const char* windowKindName(BYTE kind) {
    constexpr const char* names[]={"unknown","Progman","WorkerW","Shell_TrayWnd","Shell_SecondaryTrayWnd",
        "SHELLDLL_DefView","SysListView32","SDL_app","Chrome_WidgetWin_1","CabinetWClass",
        "QQ","YeManControlledTouchChild","YeManScreenTouchpad","YeManScreenTouchpadOutline",
        "YeManScreenTouchpadCursor","YeManScreenTouchContact","YeManScreenButton"};
    return kind<sizeof(names)/sizeof(names[0])?names[kind]:"unknown";
}
inline Window window(HWND h) { Window w;w.handle=(ULONG_PTR)h;
    if(h){w.thread=GetWindowThreadProcessId(h,&w.pid);w.kind=windowKind(h);}return w; }
inline void observe(const CURSORINFO& ci,bool valid,CursorSampleSource source,ULONGLONG tick,ULONGLONG sequence,bool active,bool deck) {
    if(!enabled||!active||!deck||!valid)return;
    const bool showing=(ci.flags&CURSOR_SHOWING)&&ci.hCursor;
    bool recovery=false;
    if(ci.flags==0&&sawShowing&&!hiddenCaptured)hiddenCaptured=true;
    else if(showing){sawShowing=true;if(!hiddenCaptured||recoveryCaptured)return;recoveryCaptured=true;recovery=true;}
    else return;
    const DWORD savedError=GetLastError();
    Entry e;e.tick=tick;e.gesture=gestures;e.sequence=sequence;e.point=ci.ptScreenPos;e.flags=ci.flags;
    e.shape=(ULONG_PTR)ci.hCursor;e.source=source;e.recovery=recovery;
    const HWND foreground=GetForegroundWindow();const HWND hit=WindowFromPoint(ci.ptScreenPos);
    e.hit=window(hit);e.root=window(hit?GetAncestor(hit,GA_ROOT):nullptr);e.foreground=window(foreground);
    e.foregroundAfter=(ULONG_PTR)GetForegroundWindow();entries[count++%entries.size()]=e;SetLastError(savedError);
}
inline Json windowState(const Window& w) { return {{"hwnd",w.handle},{"threadId",w.thread},{"processId",w.pid},
    {"windowClass",windowKindName(w.kind)},{"identityReadOk",w.handle!=0&&w.thread!=0&&w.pid!=0}}; }
inline Json state() {
    Json rows=Json::array();const auto first=count>entries.size()?count-entries.size():0;
    for(auto i=first;i<count;++i){const auto& e=entries[i%entries.size()];
        const char* source=e.source==CursorSampleSource::Contact?"contact":e.source==CursorSampleSource::Release?"release":
            e.source==CursorSampleSource::Visibility?"visibility":e.source==CursorSampleSource::Preparation?"preparation":"periodic";
        rows.push_back({{"sequence",i+1},{"cursorTraceSequence",e.sequence},{"gesture",e.gesture},{"tick",e.tick},
            {"flags",e.flags},{"shape",e.shape},{"x",e.point.x},{"y",e.point.y},{"source",source},
            {"edge",e.recovery?"showing-after-hidden":"hidden-after-showing"},{"hit",windowState(e.hit)},
            {"hitRoot",windowState(e.root)},{"foregroundBefore",windowState(e.foreground)},
            {"foregroundAfter",e.foregroundAfter},{"foregroundHandleStable",e.foreground.handle==e.foregroundAfter}});
    }
    return {{"version",1},{"capacity",entries.size()},{"total",count},{"gestures",gestures},{"maxCapturesPerGesture",2},{"entries",std::move(rows)},
        {"scope","Sequential current-window reads at first active Deck SHOWING/hidden pair only; WindowFromPoint is context, not hardware hit delivery, cursor owner/API caller, historical process identity, or an atomic snapshot"},
        {"cursorApiCallerProven",false},{"windowIdentityHistorical",false},{"windowTextRetained",false}};
}
} // namespace cursorcontext
struct CursorTransition {
    ULONGLONG tick=0;DWORD flags=0;POINT point{};ULONG_PTR shape=0;
    CursorSampleSource source=CursorSampleSource::Periodic;
    bool valid=false,visible=false,active=false,retained=false,deck=false;
    ULONGLONG lastTick=0;
    POINT lastPoint{},minimum{},maximum{};
    unsigned long long samples=1,positionChanges=0;
};
inline std::array<CursorTransition,64> cursorTransitions{};
inline unsigned long long cursorTransitionCount=0;
inline void cursorRecordSample(const CURSORINFO& ci,bool valid,CursorSampleSource source) {
    CursorTransition* previous=cursorTransitionCount?&cursorTransitions[(cursorTransitionCount-1)%cursorTransitions.size()]:nullptr;
    if(!cursorDeckOnlyTracking && (!previous || !previous->deck))return;
    CursorTransition entry{GetTickCount64(),ci.flags,ci.ptScreenPos,(ULONG_PTR)ci.hCursor,source,valid,
        cursorVisible,cursorTracking.load(std::memory_order_acquire),cursorRetained.load(std::memory_order_acquire),cursorDeckOnlyTracking};
    entry.lastTick=entry.tick;entry.lastPoint=entry.minimum=entry.maximum=entry.point;
    // Keep the first position plus a bounded motion summary. A round trip can
    // finish at its initial coordinate; equal DOWN/UP endpoints do not prove
    // that Steam left the OS cursor stationary between them. Use existing
    // samples only: no extra query, allocation, clock call or per-MOVE log.
    if(previous && entry.valid==previous->valid && entry.flags==previous->flags && entry.shape==previous->shape &&
       entry.visible==previous->visible && entry.active==previous->active && entry.retained==previous->retained && entry.deck==previous->deck) {
        previous->lastTick=entry.tick;++previous->samples;
        if(entry.valid && (entry.point.x!=previous->lastPoint.x || entry.point.y!=previous->lastPoint.y)) {
            ++previous->positionChanges;previous->lastPoint=entry.point;
            previous->minimum.x=std::min(previous->minimum.x,entry.point.x);previous->minimum.y=std::min(previous->minimum.y,entry.point.y);
            previous->maximum.x=std::max(previous->maximum.x,entry.point.x);previous->maximum.y=std::max(previous->maximum.y,entry.point.y);
        }
        cursorcontext::observe(ci,valid,source,entry.tick,cursorTransitionCount,entry.active,entry.deck);
        return;
    }
    cursorTransitions[cursorTransitionCount++%cursorTransitions.size()]=entry;
    cursorcontext::observe(ci,valid,source,entry.tick,cursorTransitionCount,entry.active,entry.deck);
}
struct CursorSampleScope {
    const CURSORINFO& sample;CursorSampleSource source;bool valid=true;
    ~CursorSampleScope(){cursorRecordSample(sample,valid,source);}
};
inline Json cursorTransitionState() {
    Json entries=Json::array();
    const auto count=std::min<unsigned long long>(cursorTransitionCount,cursorTransitions.size());
    for(auto sequence=cursorTransitionCount-count;sequence<cursorTransitionCount;++sequence) {
        const auto& e=cursorTransitions[sequence%cursorTransitions.size()];
        const char* source=e.source==CursorSampleSource::Contact?"contact":e.source==CursorSampleSource::Release?"release":
            e.source==CursorSampleSource::Visibility?"visibility":e.source==CursorSampleSource::Lifecycle?"lifecycle":e.source==CursorSampleSource::Preparation?"preparation":"periodic";
        entries.push_back({{"sequence",sequence+1},{"tick",e.tick},{"source",source},{"valid",e.valid},{"flags",e.flags},
            {"x",e.point.x},{"y",e.point.y},{"shape",e.shape},{"fallbackVisible",e.visible},{"active",e.active},{"retained",e.retained},{"deck",e.deck},
            {"lastTick",e.lastTick},{"samples",e.samples},{"positionChanges",e.positionChanges},{"lastX",e.lastPoint.x},{"lastY",e.lastPoint.y},
            {"minX",e.minimum.x},{"minY",e.minimum.y},{"maxX",e.maximum.x},{"maxY",e.maximum.y}});
    }
    return {{"capacity",cursorTransitions.size()},{"total",cursorTransitionCount},{"entries",std::move(entries)},
        {"evidenceVersion",1},{"placements",{{"setWindowPos",cursorWindowPlacements},{"deckRaises",cursorRaisePlacements},
            {"setWindowPosFailures",cursorWindowPlacementFailures},{"unchanged",cursorPositionUnchanged},
            {"lastRaisePredecessor",(ULONG_PTR)cursorLastRaisePredecessor},{"lastRaiseTick",cursorLastRaiseTick},
            {"stackRaises",cursorStackRaises},{"stackRaiseFailures",cursorStackRaiseFailures}}}};
}
inline bool cursorFallbackRequired(bool tracking,DWORD flags,HCURSOR shape) {
    // Do not draw over a visible system cursor or an explicitly hidden one.
    return tracking && shape && (flags&CURSOR_SUPPRESSED) && !(flags&CURSOR_SHOWING);
}
inline void cursorHideVisual(HWND target=nullptr) {
    const bool wasVisible=cursorVisible,force=target!=nullptr;
    if(!target)target=cursorWindow;
    cursorVisible=false;cursorPositionValid=false;
    if(target && (wasVisible || force)){ShowWindow(target,SW_HIDE);if(wasVisible)++cursorHides;}
}
inline void cursorObserveFlags(DWORD flags) {
    if(cursorFlagsObserved && cursorLastFlags!=flags)++cursorFlagTransitions;
    cursorLastFlags=flags;cursorFlagsObserved=true;
}
inline LRESULT CALLBACK cursorWindowProc(HWND h,UINT m,WPARAM w,LPARAM lp) {
    switch(m) {
    case WM_STYLECHANGED:case WM_ENABLE: {
        if(cursorLayeredResetting)return 0; // Our off/on reset completes before revalidating the policy.
        const bool lostImage=m==WM_STYLECHANGED && w==GWL_EXSTYLE && lp &&
            !(reinterpret_cast<const STYLESTRUCT*>(lp)->styleNew&WS_EX_LAYERED);
        if(lostImage){cursorImageUploaded=false;cursorLayeredResetPending=true;}
        if(!ymcc::screeninput::ensureDisplay(h) || lostImage) {
            // Keep the visual cache in step with this existing protection path.
            // Input ownership and retained sampling are deliberately untouched.
            cursorHideVisual(h);
        }
        return 0;
    }
    case WM_WINDOWPOSCHANGED:
        cursorPositionValid=false;
        if(lp && (reinterpret_cast<const WINDOWPOS*>(lp)->flags&SWP_HIDEWINDOW)) {
            if(cursorVisible)++cursorHides;
            cursorVisible=false;
        }
        break;
    case WM_NCDESTROY:
        if(h==cursorWindow){cursorWindow=nullptr;cursorVisible=false;cursorImageUploaded=false;cursorPositionValid=false;cursorLayeredResetPending=false;}
        break;
    case WM_SETCURSOR:return TRUE; // Never replace the underlying application cursor.
    case WM_NCHITTEST:return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:return PA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    }return DefWindowProcW(h,m,w,lp);
}
inline void cursorResetHandoff() {cursorShowingObserved=false;cursorShowingSince=0;cursorShowingLastSample=0;cursorShowingSamples=0;}
inline void cursorHide() {
    cursorHideVisual();cursorResetHandoff();
    cursorRetained.store(false,std::memory_order_release);cursorNextRetainedPoll.store(0,std::memory_order_relaxed);
    if(!cursorTracking.load(std::memory_order_acquire)) {
        cursorDispatchWindow.store(nullptr,std::memory_order_release);cursorvisibility::stop();
    }
}
inline void cursorStop() {
    cursorcontext::end();
    cursorvisibility::stop();cursorDeckOnlyTracking.store(false,std::memory_order_release);cursorDeckNextPositionTick=0;cursorDeckReportPulseTick.store(0,std::memory_order_release);cursorResetDeckMotion();
    cursorTracking.store(false,std::memory_order_release);
    cursorNextActivePoll.store(0,std::memory_order_relaxed);
    cursorDispatchWindow.store(nullptr,std::memory_order_release);
    cursorSyncPending.store(false,std::memory_order_release);
    cursorHide();
    CURSORINFO stopped{sizeof(stopped)};cursorRecordSample(stopped,false,CursorSampleSource::Lifecycle);
}
inline void cursorShutdown() {
    cursorStop();cursorvisibility::releaseRetiredHook();if(cursorWindow)DestroyWindow(cursorWindow);
    cursorWindow=nullptr;cursorShape=nullptr;cursorSize={};cursorHotspot={};cursorPositionValid=false;cursorImageUploaded=false;
}
inline void cursorRequestSync(bool afterDeckReport=false,ULONGLONG sampledTick=0) {
    // Only direct Mouse owns tracking. A virtual Deck report cannot request pixels.
    if(afterDeckReport)return;
    const bool active=cursorTracking.load(std::memory_order_acquire);
    if(!active && !cursorRetained.load(std::memory_order_acquire))return;
    const auto now=sampledTick?sampledTick:GetTickCount64();
    if(!active) {
        if(now<cursorNextRetainedPoll.load(std::memory_order_relaxed))return;
        cursorNextRetainedPoll.store(now+kCursorRetainedPollMs,std::memory_order_relaxed);
    } else {
        if(now<cursorNextActivePoll.load(std::memory_order_relaxed))return;
        cursorNextActivePoll.store(now+kCursorActiveFallbackMs,std::memory_order_relaxed);
    }
    if(cursorSyncPending.exchange(true,std::memory_order_acq_rel))return;
    HWND h=cursorDispatchWindow.load(std::memory_order_acquire);
    if(!h || !PostMessageW(h,kCursorSyncMessage,0,0))cursorSyncPending.store(false,std::memory_order_release);
}
// Reuse the existing BUS only for direct Mouse; native report output is independent.
inline void cursorRequestBeforeReport() {cursorRequestSync();}
inline void cursorRequestAfterReport() {}
// UI-only display entitlement. A retained Mouse frame ends when any native-pad
// contact takes over. Recheck after GDI preparation because it can reenter.
inline bool cursorMouseDisplayAllowed() {
    if(cursorDeckOnlyTracking.load(std::memory_order_acquire))return false;
    std::lock_guard<std::mutex> lock(mutex);bool any=false;
    for(int i=0;i<2;++i)if(core.pads[i].down){any=true;if(core.mode(i)==Mode::Mouse)return true;}
    return !any && cursorRetained.load(std::memory_order_acquire);
}
inline bool cursorRepairLayeredSurface() {
    if(!cursorLayeredResetPending)return true;
    if(!cursorWindow){cursorLayeredResetPending=false;return true;}
    // Restoring WS_EX_LAYERED recursively inside WM_STYLECHANGED can leave
    // User32's backing surface invalid (UpdateLayeredWindow then returns 87).
    // Finish the off/on transition outside that notification, once per loss.
    // The damaged display was already hidden; no touch/input HWND is changed.
    bool repaired=false;
    {
        struct Guard {Guard(){cursorLayeredResetting=true;}~Guard(){cursorLayeredResetting=false;}} guard;
        const auto style=GetWindowLongPtrW(cursorWindow,GWL_EXSTYLE);
        SetLastError(ERROR_SUCCESS);
        const bool removed=SetWindowLongPtrW(cursorWindow,GWL_EXSTYLE,style&~WS_EX_LAYERED)!=0 || GetLastError()==ERROR_SUCCESS;
        if(removed) {
            cursorImageUploaded=false;cursorPositionValid=false;
            SetLastError(ERROR_SUCCESS);
            repaired=SetWindowLongPtrW(cursorWindow,GWL_EXSTYLE,style|ymcc::screeninput::displayExStyle)!=0 || GetLastError()==ERROR_SUCCESS;
        }
        if(!repaired){cursorError=GetLastError();if(!cursorError)cursorError=ERROR_GEN_FAILURE;}
    }
    if(repaired && !ymcc::screeninput::ensureDisplay(cursorWindow)) {
        repaired=false;cursorError=GetLastError();if(!cursorError)cursorError=ERROR_ACCESS_DENIED;
    }
    if(!repaired){++cursorLayeredResetFailures;return false;} // Original BUS budget owns retry.
    cursorLayeredResetPending=false;++cursorLayeredResets;return true;
}
inline bool cursorUpload(const CURSORINFO& ci) {
    if(!cursorMouseDisplayAllowed())return false;
    if(!cursorRepairLayeredSurface())return false;
    ICONINFO icon{};if(!GetIconInfo(ci.hCursor,&icon)){cursorError=GetLastError();return false;}
    BITMAP bitmap{};const HBITMAP source=icon.hbmColor?icon.hbmColor:icon.hbmMask;
    const bool measured=source && GetObjectW(source,sizeof(bitmap),&bitmap);
    const LONG width=measured?bitmap.bmWidth:0,height=measured?(icon.hbmColor?bitmap.bmHeight:bitmap.bmHeight/2):0;
    if(width<=0 || height<=0 || width>512 || height>512) {
        if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
        cursorError=ERROR_INVALID_DATA;return false;
    }
    if(!cursorWindow) {
        WNDCLASSW wc{};wc.lpfnWndProc=cursorWindowProc;wc.hInstance=instance;wc.lpszClassName=L"YeManScreenTouchpadCursor";
        if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) {
            if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
            cursorError=GetLastError();return false;
        }
        cursorImageUploaded=false;
        cursorWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,
            wc.lpszClassName,L"YeManCC Touchpad Cursor",WS_POPUP|WS_DISABLED,0,0,width,height,nullptr,nullptr,instance,nullptr);
        if(cursorWindow&&!ymcc::screeninput::ensureDisplay(cursorWindow)){DestroyWindow(cursorWindow);cursorWindow=nullptr;}
        if(!cursorWindow) {if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);cursorError=GetLastError();return false;}
    }
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr),blackDC=screen?CreateCompatibleDC(screen):nullptr,whiteDC=screen?CreateCompatibleDC(screen):nullptr;
    void *blackBits=nullptr,*whiteBits=nullptr;
    HBITMAP black=screen?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&blackBits,nullptr,0):nullptr;
    HBITMAP white=screen?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&whiteBits,nullptr,0):nullptr;
    bool ok=false;HGDIOBJ oldBlack=nullptr,oldWhite=nullptr;
    if(screen && blackDC && whiteDC && black && white && blackBits && whiteBits) {
        oldBlack=SelectObject(blackDC,black);oldWhite=SelectObject(whiteDC,white);
        auto b=static_cast<DWORD*>(blackBits),w=static_cast<DWORD*>(whiteBits);
        const size_t count=(size_t)width*height;std::fill_n(b,count,0u);std::fill_n(w,count,0x00ffffffu);
        // Rendering against black/white reconstructs premultiplied alpha for
        // both modern alpha cursors and classic monochrome AND/XOR masks.
        if(DrawIconEx(blackDC,0,0,ci.hCursor,width,height,0,nullptr,DI_NORMAL) &&
           DrawIconEx(whiteDC,0,0,ci.hCursor,width,height,0,nullptr,DI_NORMAL)) {
            GdiFlush();
            for(size_t i=0;i<count;i++) {
                int difference=0;for(int shift:{0,8,16})difference=std::max(difference,(int)((w[i]>>shift)&255)-(int)((b[i]>>shift)&255));
                b[i]=(b[i]&0x00ffffffu)|((DWORD)(255-difference)<<24);
            }
            POINT destination{ci.ptScreenPos.x-(LONG)icon.xHotspot,ci.ptScreenPos.y-(LONG)icon.yHotspot},origin{};
            SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            cursorPositionValid=false;
            ok=UpdateLayeredWindow(cursorWindow,screen,&destination,&size,blackDC,&origin,0,&blend,ULW_ALPHA)!=FALSE;
            if(ok){cursorImageUploaded=true;cursorDeckPositionOnlyEnabled=true;cursorShape=ci.hCursor;cursorHotspot={(LONG)icon.xHotspot,(LONG)icon.yHotspot};cursorSize=size;cursorPosition=destination;cursorPositionValid=true;++cursorUploads;cursorError=0;}
        }
    }
    if(!ok){cursorError=GetLastError();if(!cursorError)cursorError=ERROR_NOT_ENOUGH_MEMORY;}
    if(oldBlack)SelectObject(blackDC,oldBlack);if(oldWhite)SelectObject(whiteDC,oldWhite);
    if(black)DeleteObject(black);if(white)DeleteObject(white);
    if(blackDC)DeleteDC(blackDC);if(whiteDC)DeleteDC(whiteDC);if(screen)ReleaseDC(nullptr,screen);
    if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
    return ok;
}
inline void cursorSyncInfo(CURSORINFO ci,ULONGLONG now=GetTickCount64(),bool /*liveSample*/=false,CursorSampleSource source=CursorSampleSource::Periodic) {
    if(!cursorMouseDisplayAllowed()){cursorStop();return;}
    CURSORINFO observed=ci;CursorSampleScope sampleScope{observed,source};
    ++cursorSyncs;cursorObserveFlags(ci.flags);
    const bool active=cursorTracking.load(std::memory_order_acquire);
    const bool retained=cursorRetained.load(std::memory_order_acquire);
    if(!active && !retained){cursorHide();return;}
    const bool suppressed=(ci.flags&CURSOR_SUPPRESSED) && !(ci.flags&CURSOR_SHOWING);
    if(suppressed)cursorResetHandoff();
    else if((ci.flags&CURSOR_SHOWING) && ci.hCursor) {
        if(!cursorVisible){cursorHide();return;}
        if(!active){++cursorHandoffs;cursorHide();return;}
        if(cursorShowingObserved && (now<cursorShowingLastSample || now-cursorShowingLastSample>kCursorMaxHandoffSampleGapMs)) {
            ++cursorHandoffGapResets;cursorResetHandoff();
        }
        if(!cursorShowingObserved){cursorShowingObserved=true;cursorShowingSince=now;cursorShowingSamples=1;}
        else if(cursorShowingSamples<2)++cursorShowingSamples;
        cursorShowingLastSample=now;
        if(cursorShowingSamples>=2 && now-cursorShowingSince>=kCursorHandoffMs) {
            ++cursorHandoffs;cursorHide();return;
        }
    } else if(ci.flags&CURSOR_SHOWING) {
        cursorResetHandoff();
    } else {cursorHide();return;}
    // This fallback belongs solely to direct Mouse. Native touch reports never
    // borrow a cached shape, GetCursor or the default Windows arrow.
    if(!ci.hCursor){ci.hCursor=cursorShape;if(!ci.hCursor)ci.hCursor=GetCursor();if(!ci.hCursor)ci.hCursor=LoadCursorW(nullptr,IDC_ARROW);}
    if(!ci.hCursor){cursorHide();return;}
    const bool preparedImage=!cursorWindow || !cursorImageUploaded || cursorShape!=ci.hCursor;
    const bool imageReady=!preparedImage || cursorUpload(ci);
    if(!imageReady && (!cursorWindow || !cursorImageUploaded || !cursorShape)) {
        cursorHideVisual();cursorResetHandoff();return;
    }
    if(!cursorMouseDisplayAllowed() || !(cursorTracking.load(std::memory_order_acquire) || cursorRetained.load(std::memory_order_acquire))) {
        cursorStop();return;
    }
    if(!cursorVisible && !ymcc::screeninput::ensureDisplay(cursorWindow)) {
        cursorError=GetLastError();if(!cursorError)cursorError=ERROR_ACCESS_DENIED;return;
    }
    const LONG x=ci.ptScreenPos.x-cursorHotspot.x,y=ci.ptScreenPos.y-cursorHotspot.y;
    if(!cursorVisible || !cursorPositionValid || cursorPosition.x!=x || cursorPosition.y!=y) {
        if(!SetWindowPos(cursorWindow,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)) {
            ++cursorWindowPlacementFailures;cursorError=GetLastError();cursorPositionValid=false;return;
        }
        ++cursorWindowPlacements;cursorPosition={x,y};cursorPositionValid=true;if(imageReady)cursorError=0;
    } else ++cursorPositionUnchanged;
    if(!cursorVisible)++cursorShows;
    cursorVisible=true;
}
inline void cursorVisibilitySync(HWND receiver,WPARAM generation) {
    // Retired Deck visibility messages cannot query or restore a cursor.
    if(cursorvisibility::consume(receiver,generation))cursorvisibility::stop();
}
inline void cursorSync(CursorSampleSource source=CursorSampleSource::Periodic) {
    if(!cursorTracking.load(std::memory_order_acquire) && !cursorRetained.load(std::memory_order_acquire))return;
    if(!cursorMouseDisplayAllowed()){cursorStop();return;}
    ++cursorQueries;
    const auto sampledAt=GetTickCount64();
    cursorNextActivePoll.store(sampledAt+kCursorActiveFallbackMs,std::memory_order_relaxed);
    // A delayed owner message starts its next slot at the actual query, not
    // the old BUS enqueue time. Failed reads use the same bounded retry slot.
    if(cursorRetained.load(std::memory_order_acquire))
        cursorNextRetainedPoll.store(sampledAt+kCursorRetainedPollMs,std::memory_order_relaxed);
    CURSORINFO ci{sizeof(ci)};if(!GetCursorInfo(&ci)){
        ++cursorQueryFailures;cursorResetHandoff();cursorRecordSample(ci,false,source);return;
    }
    cursorSyncInfo(ci,GetTickCount64(),true,source);
}
inline bool cursorRetainAfterUp(HWND dispatch,const CURSORINFO& ci,bool liveSample=false) {
    const bool waiting=((ci.flags&CURSOR_SUPPRESSED) && !(ci.flags&CURSOR_SHOWING)) ||
        ((ci.flags&CURSOR_SHOWING) && !ci.hCursor && cursorVisible);
    if(!cursorTracking.load(std::memory_order_acquire) || cursorDeckOnlyTracking || !waiting)return false;
    const auto sampledAt=GetTickCount64();
    // UP already supplied this sample. Keep any pending query, but publish
    // the next deadline before allowing BUS reads to observe retention.
    cursorNextRetainedPoll.store(sampledAt+kCursorRetainedPollMs,std::memory_order_relaxed);
    cursorTracking.store(false,std::memory_order_release);cursorRetained.store(true,std::memory_order_release);
    cursorDispatchWindow.store(dispatch,std::memory_order_release);
    cursorSyncInfo(ci,sampledAt,liveSample,CursorSampleSource::Release);cursorRequestSync();return true;
}
inline void cursorContactChanged(HWND dispatch,bool normalUp=false,bool /*deferDeckMove*/=false,ULONGLONG /*dispatchTick*/=0) {
    bool mouse=false,any=false;{std::lock_guard<std::mutex> lock(mutex);
        for(int i=0;i<2;i++)if(core.pads[i].down){any=true;mouse=mouse || core.mode(i)==Mode::Mouse;}}
    if(!mouse) {
        // Native Deck/PS5 DOWN/MOVE/UP never start a cursor, observer or poll.
        // In particular they cannot inherit the last direct-Mouse bitmap.
        if(!any && normalUp && cursorTracking.load(std::memory_order_acquire) && !cursorDeckOnlyTracking) {
            CURSORINFO ci{sizeof(ci)};++cursorQueries;
            if(GetCursorInfo(&ci)) {
                if(cursorRetainAfterUp(dispatch,ci,true))return;
            } else {++cursorQueryFailures;cursorResetHandoff();cursorRecordSample(ci,false,CursorSampleSource::Release);}
        }
        if(cursorTracking.load(std::memory_order_acquire) || cursorRetained.load(std::memory_order_acquire) || cursorVisible || cursorDeckOnlyTracking)
            cursorStop();
        return;
    }
    cursorDeckOnlyTracking.store(false,std::memory_order_release);
    cursorRetained.store(false,std::memory_order_release);cursorNextRetainedPoll.store(0,std::memory_order_relaxed);
    cursorDispatchWindow.store(dispatch,std::memory_order_release);cursorTracking.store(true,std::memory_order_release);
    cursorvisibility::stop();
    cursorSync(CursorSampleSource::Contact);cursorRequestSync();
}


