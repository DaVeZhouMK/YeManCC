// Offline API-substitution harness: executes the selected real production headers.
// No User32/GDI libraries, real HWND, hook, cursor query or input calls are used.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using Json=nlohmann::json;
namespace sim {
struct Message {HWND target;UINT id;WPARAM w;LPARAM l;};
struct Bitmap {int width=16,height=16;std::vector<DWORD> pixels=std::vector<DWORD>(256);};
struct Window {bool visible=false,enabled=false;LONG_PTR style=0;POINT position{};SIZE size{};WNDPROC proc=nullptr;};
inline ULONGLONG now=1000;
inline DWORD thread=7,error=0;
inline uintptr_t nextId=100;
inline CURSORINFO current{sizeof(CURSORINFO),CURSOR_SUPPRESSED,(HCURSOR)(uintptr_t)1,{200,150}};
inline std::deque<bool> readResults;
inline std::map<uintptr_t,Bitmap> bitmaps;
inline std::map<uintptr_t,uintptr_t> dcs;
inline std::map<uintptr_t,Window> windows;
inline std::deque<Message> messages;
inline WNDPROC registered=nullptr;
inline HWINEVENTHOOK activeHook=nullptr;
inline WINEVENTPROC callback=nullptr;
inline HWND above=nullptr;
inline bool failCreate=false,failUpload=false,failMove=false,failPosition=false,failIcon=false,failPost=false,failHook=false,failUnhook=false;
inline unsigned reads=0,uploads=0,moves=0,positions=0,shows=0,hides=0,hookInstalls=0,hookRemovals=0;
inline unsigned long long tick64Reads=0;
// R4 faults are armed only after the external style-loss notification has
// completed. They fail the requested API call without mutating the HWND.
inline bool failLayeredOff=false,failLayeredOn=false;
inline unsigned styleWrites=0,styleOffWrites=0,styleOnWrites=0,styleFailures=0,styleDepth=0,maxStyleDepth=0;
inline Json styleCalls=Json::array();
inline void clearStyleObservations(){styleWrites=styleOffWrites=styleOnWrites=styleFailures=styleDepth=maxStyleDepth=0;styleCalls=Json::array();}
inline std::function<void()> afterUpload;
inline Json calls=Json::array();
inline HWND dispatchWindow=(HWND)(uintptr_t)2;
inline void record(const char* name){calls.push_back({{"call",name},{"tick",now}});}
inline DWORD lastError(){return error;}
inline void setError(DWORD value){error=value;}
inline ULONGLONG tick64(){++tick64Reads;return now;}
inline DWORD tick32(){return (DWORD)now;}
inline DWORD threadId(){return thread;}
inline BOOL cursorInfo(CURSORINFO* out){++reads;if(!readResults.empty()){bool ok=readResults.front();readResults.pop_front();if(!ok){error=ERROR_ACCESS_DENIED;return FALSE;}}*out=current;return TRUE;}
inline HCURSOR getCursor(){return current.hCursor;}
inline HCURSOR loadCursor(HINSTANCE,LPCWSTR){return (HCURSOR)(uintptr_t)1;}
inline BOOL post(HWND target,UINT id,WPARAM w,LPARAM l){if(failPost){error=ERROR_NOT_ENOUGH_QUOTA;return FALSE;}messages.push_back({target,id,w,l});return TRUE;}
inline HWINEVENTHOOK hook(DWORD lo,DWORD hi,HMODULE,WINEVENTPROC cb,DWORD pid,DWORD tid,DWORD flags){
    if(lo!=EVENT_OBJECT_SHOW||hi!=EVENT_OBJECT_HIDE||pid||tid||flags!=WINEVENT_OUTOFCONTEXT)throw std::runtime_error("hook scope changed");
    ++hookInstalls;if(failHook){error=ERROR_ACCESS_DENIED;return nullptr;}callback=cb;activeHook=(HWINEVENTHOOK)++nextId;return activeHook;
}
inline BOOL unhook(HWINEVENTHOOK value){++hookRemovals;if(failUnhook){error=ERROR_ACCESS_DENIED;return FALSE;}if(activeHook==value)activeHook=nullptr;return TRUE;}
inline BOOL isWindow(HWND h){return windows.count((uintptr_t)h)!=0;}
inline BOOL enabled(HWND h){return windows.at((uintptr_t)h).enabled;}
inline LONG_PTR getStyle(HWND h,int){return windows.at((uintptr_t)h).style;}
inline LONG_PTR setStyle(HWND h,int index,LONG_PTR style){
    struct Depth {Depth(){++styleDepth;maxStyleDepth=std::max(maxStyleDepth,styleDepth);}~Depth(){--styleDepth;}} depth;
    if(styleDepth>8)throw std::runtime_error("recursive style repair exceeded fixture bound");
    if(index!=GWL_EXSTYLE)throw std::runtime_error("unexpected style index");
    auto& w=windows.at((uintptr_t)h);const auto old=w.style;const bool layered=(style&WS_EX_LAYERED)!=0;
    ++styleWrites;if(layered)++styleOnWrites;else ++styleOffWrites;
    const bool failed=layered?failLayeredOn:failLayeredOff;
    styleCalls.push_back({{"tick",now},{"layered",layered},{"oldStyle",old},{"newStyle",style},
        {"depth",styleDepth},{"failed",failed},{"error",failed?ERROR_ACCESS_DENIED:ERROR_SUCCESS}});
    if(failed){++styleFailures;error=ERROR_ACCESS_DENIED;return 0;}
    w.style=style;STYLESTRUCT s{(DWORD)old,(DWORD)style};if(w.proc)w.proc(h,WM_STYLECHANGED,(WPARAM)GWL_EXSTYLE,(LPARAM)&s);return old;
}
inline BOOL enable(HWND h,BOOL value){auto& w=windows.at((uintptr_t)h);const bool old=w.enabled;w.enabled=value!=FALSE;if(w.proc)w.proc(h,WM_ENABLE,value,0);return old;}
inline void changed(HWND h,UINT flags){auto& w=windows.at((uintptr_t)h);WINDOWPOS pos{h,nullptr,w.position.x,w.position.y,w.size.cx,w.size.cy,flags};if(w.proc)w.proc(h,WM_WINDOWPOSCHANGED,0,(LPARAM)&pos);}
inline BOOL show(HWND h,int command){auto& w=windows.at((uintptr_t)h);if(command!=SW_HIDE)throw std::runtime_error("unexpected ShowWindow");const bool old=w.visible;w.visible=false;++hides;record("hide");changed(h,SWP_HIDEWINDOW|SWP_NOMOVE|SWP_NOSIZE);return old;}
inline BOOL destroy(HWND h){auto found=windows.find((uintptr_t)h);if(found==windows.end())return FALSE;auto proc=found->second.proc;if(proc)proc(h,WM_NCDESTROY,0,0);windows.erase((uintptr_t)h);return TRUE;}
inline LRESULT defaultProc(HWND,UINT,WPARAM,LPARAM){return 0;}
inline HDC beginPaint(HWND,PAINTSTRUCT*){return nullptr;}
inline BOOL endPaint(HWND,const PAINTSTRUCT*){return TRUE;}
inline ATOM registerClass(const WNDCLASSW* wc){registered=wc->lpfnWndProc;return 1;}
inline HWND create(DWORD ex,LPCWSTR,LPCWSTR,DWORD style,int x,int y,int cx,int cy,HWND,HMENU,HINSTANCE,LPVOID){if(failCreate){error=ERROR_NOT_ENOUGH_MEMORY;return nullptr;}HWND h=(HWND)++nextId;windows[(uintptr_t)h]={false,(style&WS_DISABLED)==0,(LONG_PTR)ex,{x,y},{cx,cy},registered};return h;}
inline HBITMAP bitmap(int width=16,int height=16){const auto id=++nextId;bitmaps[id]={width,height,std::vector<DWORD>((size_t)width*height)};return (HBITMAP)id;}
inline BOOL iconInfo(HICON,ICONINFO* out){if(failIcon){error=ERROR_INVALID_HANDLE;return FALSE;}out->xHotspot=2;out->yHotspot=3;out->hbmColor=bitmap();out->hbmMask=bitmap();return TRUE;}
inline int object(HGDIOBJ h,int,LPVOID out){auto& b=bitmaps.at((uintptr_t)h);auto* result=(BITMAP*)out;result->bmWidth=b.width;result->bmHeight=b.height;return sizeof(BITMAP);}
inline HDC getDC(HWND){return (HDC)(uintptr_t)10;}
inline int releaseDC(HWND,HDC){return 1;}
inline HDC compatibleDC(HDC){auto id=++nextId;dcs[id]=0;return (HDC)id;}
inline BOOL deleteDC(HDC dc){dcs.erase((uintptr_t)dc);return TRUE;}
inline HBITMAP dib(HDC,const BITMAPINFO* info,UINT,void** bits,HANDLE,DWORD){auto b=bitmap(info->bmiHeader.biWidth,std::abs(info->bmiHeader.biHeight));*bits=bitmaps.at((uintptr_t)b).pixels.data();return b;}
inline HGDIOBJ select(HDC dc,HGDIOBJ b){auto& item=dcs.at((uintptr_t)dc);auto old=item;item=(uintptr_t)b;return (HGDIOBJ)(old?old:1);}
inline BOOL deleteObject(HGDIOBJ b){bitmaps.erase((uintptr_t)b);return TRUE;}
inline BOOL draw(HDC dc,int,int,HICON,int,int,UINT,HBRUSH,UINT){auto& pixels=bitmaps.at(dcs.at((uintptr_t)dc)).pixels;std::fill(pixels.begin(),pixels.end(),0x00ffffffu);return TRUE;}
inline BOOL flush(){return TRUE;}
inline HWND getWindow(HWND,UINT){return above;}
inline BOOL layered(HWND h,HDC dst,POINT* p,SIZE* size,HDC src,POINT* origin,COLORREF,BLENDFUNCTION* blend,DWORD flags){
    auto& w=windows.at((uintptr_t)h);
    if(!src){if(dst||size||origin||blend||flags)throw std::runtime_error("invalid position-only ULW arguments");if(failMove){error=ERROR_INVALID_PARAMETER;return FALSE;}++moves;record("move");}
    else {if(!size||!blend||flags!=ULW_ALPHA)throw std::runtime_error("invalid image upload arguments");if(failUpload){error=ERROR_NOT_ENOUGH_MEMORY;return FALSE;}w.size=*size;++uploads;record("upload");}
    if(p)w.position=*p;changed(h,SWP_NOACTIVATE);
    if(src&&afterUpload){auto fn=std::move(afterUpload);afterUpload=nullptr;fn();}return TRUE;
}
inline BOOL position(HWND h,HWND,int x,int y,int,int,UINT flags){
    if(failPosition){error=ERROR_ACCESS_DENIED;return FALSE;}auto& w=windows.at((uintptr_t)h);++positions;record("position");
    if(!(flags&SWP_NOACTIVATE))throw std::runtime_error("activation requested");
    if(!(flags&SWP_NOMOVE))w.position={x,y};if(flags&SWP_SHOWWINDOW){w.visible=true;++shows;record("show");}
    if(flags&SWP_HIDEWINDOW)throw std::runtime_error("unexpected hide placement");changed(h,flags);return TRUE;
}
}
namespace sim {
inline unsigned contextForegroundReads=0,contextHitReads=0,contextAncestorReads=0,contextIdentityReads=0,contextClassReads=0;
inline HWND contextForeground=(HWND)(uintptr_t)800,contextHit=(HWND)(uintptr_t)801,contextRoot=(HWND)(uintptr_t)802;
inline HWND contextForegroundAfter=nullptr;inline bool contextIdentityFails=false;
inline HWND foregroundContext(){++contextForegroundReads;return contextForegroundAfter&&contextForegroundReads%2==0?contextForegroundAfter:contextForeground;}
inline HWND hitContext(POINT){++contextHitReads;return contextHit;}
inline HWND ancestorContext(HWND,UINT){++contextAncestorReads;return contextRoot;}
inline DWORD identityContext(HWND h,LPDWORD pid){++contextIdentityReads;if(contextIdentityFails){*pid=0;return 0;}*pid=(DWORD)(uintptr_t)h+1000;return (DWORD)(uintptr_t)h+2000;}
inline const wchar_t* contextClass=L"SDL_app";
inline int classContext(HWND,LPWSTR out,int size){++contextClassReads;if(size<=0)return 0;const int n=(std::min)((int)std::wcslen(contextClass),size-1);std::copy_n(contextClass,n,out);out[n]=0;return n;}
}
#define GetForegroundWindow sim::foregroundContext
#define WindowFromPoint sim::hitContext
#define GetAncestor sim::ancestorContext
#define GetWindowThreadProcessId sim::identityContext
#define GetClassNameW sim::classContext
// Redirect every OS dependency in the production cursor and display-policy code.
#define GetLastError sim::lastError
#define SetLastError sim::setError
#define GetTickCount64 sim::tick64
#define GetTickCount sim::tick32
#define GetCurrentThreadId sim::threadId
#define GetCursorInfo sim::cursorInfo
#define GetCursor sim::getCursor
#define LoadCursorW sim::loadCursor
#define PostMessageW sim::post
#define SetWinEventHook sim::hook
#define UnhookWinEvent sim::unhook
#define IsWindow sim::isWindow
#define IsWindowEnabled sim::enabled
#define GetWindowLongPtrW sim::getStyle
#define SetWindowLongPtrW sim::setStyle
#define EnableWindow sim::enable
#define ShowWindow sim::show
#define DestroyWindow sim::destroy
#define DefWindowProcW sim::defaultProc
#define BeginPaint sim::beginPaint
#define EndPaint sim::endPaint
#define RegisterClassW sim::registerClass
#define CreateWindowExW sim::create
#define GetIconInfo sim::iconInfo
#undef GetObject
#define GetObject sim::object
#define GetObjectW sim::object
#define GetDC sim::getDC
#define ReleaseDC sim::releaseDC
#define CreateCompatibleDC sim::compatibleDC
#define DeleteDC sim::deleteDC
#define CreateDIBSection sim::dib
#define SelectObject sim::select
#define DeleteObject sim::deleteObject
#define DrawIconEx sim::draw
#define GdiFlush sim::flush
#define GetWindow sim::getWindow
#define UpdateLayeredWindow sim::layered
#define SetWindowPos sim::position
#include "screen_touchpad_display_policy.h"
namespace ymcc::screenpads {
// Only the surrounding contact owner is supplied by the harness. The cursor
// decisions, resource preparation, polling and observer all come from real headers.
inline HINSTANCE instance=nullptr;
inline std::mutex mutex;
enum class Mode {Mouse,Deck};
struct Owner {struct Pad{bool down=false;};std::array<Pad,2> pads{};std::array<Mode,2> modes{Mode::Deck,Mode::Deck};Mode mode(int side)const{return modes[side];}};
inline Owner core;
#include "screen_touchpad_cursor.h"
}
using namespace ymcc::screenpads;
static Json cases=Json::array();
static unsigned failures=0;
static void check(const char* name,bool passed){cases.push_back({{"name",name},{"ok",passed},{"tick",sim::now},{"cursorFlags",sim::current.flags},{"cursorShape",(uintptr_t)sim::current.hCursor},
    {"fallbackVisible",cursorVisible},{"tracking",cursorTracking.load()},{"retained",cursorRetained.load()},{"nextPositionTick",cursorDeckNextPositionTick},
    {"queries",sim::reads},{"uploads",sim::uploads},{"moves",sim::moves},{"placements",sim::positions},{"shows",sim::shows},{"hides",sim::hides}});if(!passed){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
static void reset(){
    sim::failLayeredOff=sim::failLayeredOn=false;sim::clearStyleObservations();
    sim::failUnhook=false;sim::thread=7;cursorShutdown();cursorvisibility::releaseRetiredHook();
    sim::now=1000;sim::error=0;sim::messages.clear();sim::readResults.clear();sim::afterUpload=nullptr;sim::above=nullptr;
    sim::failCreate=sim::failUpload=sim::failMove=sim::failPosition=sim::failIcon=sim::failPost=sim::failHook=false;
    sim::current={sizeof(CURSORINFO),CURSOR_SUPPRESSED,(HCURSOR)(uintptr_t)1,{200,150}};
    sim::reads=sim::uploads=sim::moves=sim::positions=sim::shows=sim::hides=sim::hookInstalls=sim::hookRemovals=0;sim::tick64Reads=0;sim::calls=Json::array();
    core={};cursorQueries=cursorShows=cursorHides=cursorUploads=cursorSyncs=cursorHandoffs=0;cursorFlagsObserved=false;cursorTransitionCount=0;cursorTransitions={};
    cursorDeckPositionOnlyMoves=cursorDeckPositionFallbacks=0;cursorDeckPositionLastError=0;
    // New placement globals are absent in Before headers. Evidence tests use
    // exported before/after deltas so counters need no version-specific reset.
    cursorvisibility::notifications=cursorvisibility::coalesced=cursorvisibility::stale=cursorvisibility::posts=0;
    cursorvisibility::queries=cursorvisibility::edgeUpdates=cursorvisibility::unchanged=cursorvisibility::positionDeferrals=0;
}
// Existing rendering/failure regressions need an already-moving suppressed
// cursor. Unknown-shape stationary DOWN and contact-confirmed known shapes are
// covered separately. Prime with an unknown shape through real production
// samples (no gate/state bypass), keeping
// the requested test tick/coordinate and recording the extra query in totals.
static void down(){
#ifdef YMCC_CURSOR_MOTION_GATE_FIXTURE_ENABLED
    if(core.modes[0]==Mode::Deck && !cursorTracking && !cursorVisible &&
       (sim::current.flags&CURSOR_SUPPRESSED) && !(sim::current.flags&CURSOR_SHOWING)) {
        const auto point=sim::current.ptScreenPos;const auto tick=sim::now;const auto shape=sim::current.hCursor;
        auto scriptedReads=std::move(sim::readResults);sim::readResults.clear();
        sim::now=tick-24;sim::current.ptScreenPos.x=point.x-1;sim::current.hCursor=nullptr;
        core.pads[0].down=true;cursorContactChanged(sim::dispatchWindow);
        sim::now=tick;sim::current.ptScreenPos=point;sim::current.hCursor=shape;sim::readResults=std::move(scriptedReads);
    }
#endif
    core.pads[0].down=true;cursorContactChanged(sim::dispatchWindow);
}
static void dispatch(const sim::Message& m){
    // Same two message adapters as the production screen_touchpads windowProc;
    // neither duplicates cursor policy or performs real message dispatch.
    if(m.id==kCursorSyncMessage){cursorSyncPending.store(false);cursorSync();}
    else if(m.id==cursorvisibility::kMessage)cursorVisibilitySync(m.target,m.w);
    else throw std::runtime_error("unexpected message");
}
static void drain(){unsigned guard=0;while(!sim::messages.empty()){if(++guard>100)throw std::runtime_error("message loop did not settle");auto m=sim::messages.front();sim::messages.pop_front();dispatch(m);}}
static void edge(DWORD event=EVENT_OBJECT_SHOW,LONG object=OBJID_CURSOR,LONG child=0){cursorvisibility::onEvent(cursorvisibility::hook,event,nullptr,object,child,0,(DWORD)sim::now);}
static void bus(){cursorRequestSync();drain();}
static bool visible(){return cursorVisible&&cursorWindow&&sim::windows.at((uintptr_t)cursorWindow).visible;}
static void run(){
    reset();down();check("moving-suppressed-contact-prepares-and-shows",visible()&&sim::uploads==1&&sim::shows==1);
    const auto initialDeadline=cursorDeckNextPositionTick;
    for(unsigned i=1;i<24;++i){sim::now=1000+i;sim::current.ptScreenPos.x++;cursorContactChanged(sim::dispatchWindow,false,true);edge(EVENT_OBJECT_HIDE);drain();bus();}
    check("MOVE-and-visibility-notifications-do-not-spend-position-before-24ms",sim::moves==0&&sim::positions==1&&cursorDeckNextPositionTick==initialDeadline);
    sim::now=1024;bus();check("periodic-position-due-at-24ms",sim::moves==1);
    const auto deadline=cursorDeckNextPositionTick;sim::now=1025;sim::current.flags=CURSOR_SHOWING;edge();drain();
    check("system-handoff-hides-without-advancing-position-deadline",!visible()&&cursorDeckNextPositionTick==deadline);
    sim::current.flags=CURSOR_SUPPRESSED;sim::now=1026;edge(EVENT_OBJECT_HIDE);drain();
    check("event-show-before-budget-is-deferred",!visible()&&cursorvisibility::positionDeferrals==1&&cursorDeckNextPositionTick==deadline);
    sim::now=deadline;sim::current.ptScreenPos.x++;cursorRequestSync();auto periodic=sim::messages.front();sim::messages.pop_front();edge(EVENT_OBJECT_HIDE);
    auto notification=sim::messages.front();sim::messages.pop_front();dispatch(notification);const auto placed=sim::positions;const auto queries=sim::reads;dispatch(periodic);
    check("event-show-and-already-queued-periodic-share-one-slot",visible()&&sim::positions==placed&&sim::reads==queries);
    sim::current.flags=0;sim::now++;edge(EVENT_OBJECT_HIDE);drain();check("explicit-hidden-state-is-respected",!visible());

    reset();down();sim::current.flags=CURSOR_SHOWING;sim::current.hCursor=nullptr;sim::now+=24;bus();
    check("SHOWING-null-preserves-an-already-visible-last-frame",visible());
    sim::current.hCursor=(HCURSOR)(uintptr_t)1;sim::now+=24;bus();check("valid-system-shape-retires-last-frame",!visible());
    sim::current.hCursor=nullptr;sim::now+=24;bus();
    check("SHOWING-null-does-not-resurrect-hidden-fallback",!visible());

    reset();down();core.pads[0].down=false;sim::now+=5;cursorContactChanged(sim::dispatchWindow,true);check("normal-UP-suppressed-keeps-retained-ownership",visible()&&cursorRetained&&!cursorTracking&&cursorvisibility::hook);
    sim::now+=24;sim::current.ptScreenPos.x+=10;bus();check("retained-position-poll-remains-active",visible()&&sim::moves>=1);
    sim::current.flags=CURSOR_SHOWING;edge();drain();check("retained-handoff-releases-observer-and-owner",!visible()&&!cursorRetained&&!cursorvisibility::hook);

    reset();down();core.pads[0].down=false;sim::now+=5;sim::readResults.push_back(false);cursorContactChanged(sim::dispatchWindow,true);
    check("UP-query-failure-keeps-known-visible-Deck-frame-retained",visible()&&cursorRetained&&!cursorTracking&&cursorvisibility::hook);
    sim::now=1024;sim::current.ptScreenPos.x+=10;bus();check("UP-query-failure-recovers-through-existing-BUS",visible()&&sim::moves==1);
    sim::current.flags=CURSOR_SHOWING;edge();drain();check("UP-query-failure-still-hands-back-on-valid-system-sample",!visible()&&!cursorRetained);
    reset();down();const auto releaseDeadline=cursorDeckNextPositionTick;const auto releaseReads=sim::reads;const auto releaseWrites=sim::uploads+sim::moves+sim::positions;const auto releaseHides=sim::hides;
    core.pads[0].down=false;sim::now=1005;sim::readResults.push_back(false);cursorContactChanged(sim::dispatchWindow,true);
    check("UP-query-failure-does-not-show-move-hide-or-advance-budget",cursorDeckNextPositionTick==releaseDeadline&&sim::uploads+sim::moves+sim::positions==releaseWrites&&sim::hides==releaseHides);
    for(sim::now=1006;sim::now<releaseDeadline;++sim::now)bus();
    check("UP-query-failure-does-not-poll-before-existing-slot",sim::reads==releaseReads+1);
    sim::readResults.push_back(false);bus();check("retained-repeat-query-failure-keeps-frame",visible()&&cursorRetained);
    sim::current.flags=0;edge(EVENT_OBJECT_HIDE);drain();check("retained-after-query-failure-respects-explicit-hidden",!visible()&&!cursorRetained);
    reset();sim::current.flags=CURSOR_SHOWING;down();core.pads[0].down=false;sim::readResults.push_back(false);cursorContactChanged(sim::dispatchWindow,true);
    check("UP-query-failure-with-no-fallback-does-not-create-owner",!visible()&&!cursorRetained&&!cursorvisibility::hook);
    reset();core.modes[0]=Mode::Mouse;down();core.pads[0].down=false;sim::readResults.push_back(false);cursorContactChanged(sim::dispatchWindow,true);
    check("Mouse-UP-query-failure-retains-original-stop-behavior",!visible()&&!cursorRetained);
    reset();down();sim::current.flags=CURSOR_SHOWING;edge();drain();sim::current.flags=CURSOR_SUPPRESSED;sim::current.hCursor=nullptr;sim::current.ptScreenPos.x++;sim::now+=24;bus();
    check("suppressed-null-can-still-reuse-cached-shape",visible());
    reset();down();core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,false);check("cancel-still-stops-without-retention",!visible()&&!cursorRetained&&!cursorvisibility::hook);

    reset();sim::failCreate=true;down();check("window-creation-failure-does-not-show",!visible()&&!cursorWindow&&cursorTracking);
    sim::failCreate=false;sim::now+=24;bus();check("window-creation-failure-recovers-at-existing-budget",visible());
    reset();sim::failUpload=true;down();core.pads[0].down=false;sim::now+=5;cursorContactChanged(sim::dispatchWindow,true);
    check("failed-image-preparation-preserves-retained-owner",!visible()&&cursorRetained);
    sim::failUpload=false;sim::now+=24;bus();check("failed-image-preparation-retries-through-BUS",visible()&&cursorRetained);
    reset();sim::readResults={true,false};down();check("prepare-recheck-failure-is-recorded-invalid",!visible()&&cursorTransitionCount>0&&!cursorTransitions[(cursorTransitionCount-1)%cursorTransitions.size()].valid);
    sim::now+=24;bus();check("prepare-recheck-failure-recovers",visible());
    reset();sim::afterUpload=[]{sim::current.flags=CURSOR_SHOWING;};down();check("system-handoff-during-preparation-never-shows-stale-image",!visible()&&sim::shows==0);
    reset();down();sim::failMove=true;sim::now+=24;sim::current.ptScreenPos.x++;bus();check("position-only-failure-uses-one-fallback-without-hide",visible()&&sim::positions==2&&!cursorDeckPositionOnlyEnabled);
    sim::failMove=false;sim::now+=24;sim::current.hCursor=(HCURSOR)(uintptr_t)3;bus();check("new-upload-restores-position-only-path-without-extra-placement",visible()&&cursorDeckPositionOnlyEnabled&&sim::positions==2&&sim::uploads==2);

    reset();down();const auto posts=sim::messages.size();edge(EVENT_OBJECT_SHOW,OBJID_WINDOW);edge(EVENT_OBJECT_SHOW,OBJID_CURSOR,1);check("own-window-and-child-events-are-filtered",sim::messages.size()==posts);
    edge();edge(EVENT_OBJECT_HIDE);check("visibility-notifications-coalesce",sim::messages.size()==posts+1&&cursorvisibility::coalesced==1);
    auto old=sim::messages.back();sim::messages.clear();cursorStop();down();edge();const auto pending=cursorvisibility::pending;dispatch(old);
    check("old-generation-cannot-clear-current-pending",pending&&cursorvisibility::pending);drain();check("current-generation-still-consumes",!cursorvisibility::pending);
    sim::failPost=true;edge();check("post-failure-clears-pending-for-retry",!cursorvisibility::pending);sim::failPost=false;edge();drain();check("notification-after-post-failure-recovers",!cursorvisibility::pending);
    sim::thread=8;const auto hook=cursorvisibility::hook;cursorvisibility::stop();check("wrong-thread-stop-preserves-hook",cursorvisibility::hook==hook);sim::thread=7;
    sim::failUnhook=true;cursorStop();check("failed-unhook-is-retained-but-gated-off",!cursorvisibility::hook&&cursorvisibility::retiredHook==hook);
    const auto installs=sim::hookInstalls;down();check("failed-cleanup-does-not-install-second-hook",sim::hookInstalls==installs);
    sim::failUnhook=false;cursorStop();down();check("new-session-retries-retired-cleanup",cursorvisibility::hook&&!cursorvisibility::retiredHook);
    reset();sim::failHook=true;down();check("hook-failure-keeps-polling-fallback",visible()&&!cursorvisibility::hook);sim::now+=24;sim::current.ptScreenPos.x++;bus();check("hook-failure-does-not-disable-position-poll",sim::moves==1);
    reset();down();sim::now+=24;sim::readResults.push_back(false);bus();check("periodic-query-failure-keeps-existing-frame",visible());
    reset();core.modes[0]=Mode::Mouse;down();sim::now+=1;sim::current.flags=CURSOR_SHOWING;cursorSync();sim::now+=40;cursorSync();check("Mouse-handoff-retains-original-grace",visible()&&!cursorvisibility::hook);sim::now+=40;cursorSync();check("Mouse-handoff-completes-original-grace",!visible());
    reset();check("all-fake-resources-released",sim::windows.empty()&&sim::bitmaps.empty()&&sim::dcs.empty()&&!sim::activeHook);
}
// R3 long sequences keep the original 49 checks above unchanged. These are API
// workload/oracle checks, not a replacement implementation of cursor policy.
static Json longScenarios=Json::array();
struct LongScenario {
    const char* name;unsigned assertions=0,violations=0;Json firstFailures=Json::array(),metrics=Json::object();
    void expect(bool ok,const char* invariant,unsigned step=0) {
        ++assertions;if(ok)return;++violations;
        if(firstFailures.size()<12) {
            Json tail=Json::array();const size_t begin=sim::calls.size()>6?sim::calls.size()-6:0;
            for(size_t i=begin;i<sim::calls.size();++i)tail.push_back(sim::calls[i]);
            firstFailures.push_back({{"invariant",invariant},{"step",step},{"tick",sim::now},
                {"flags",sim::current.flags},{"visible",cursorVisible},{"retained",cursorRetained.load()},
                {"nextPositionTick",cursorDeckNextPositionTick},{"queries",sim::reads},{"uploads",sim::uploads},
                {"moves",sim::moves},{"placements",sim::positions},{"shows",sim::shows},{"hides",sim::hides},
                {"pendingMessages",sim::messages.size()},{"lastApiCalls",std::move(tail)}});
        }
    }
    void finish() {
        check(name,violations==0);
        longScenarios.push_back({{"name",name},{"ok",violations==0},{"assertions",assertions},
            {"violations",violations},{"firstFailures",firstFailures},{"metrics",metrics}});
    }
};
static unsigned positionCalls(){return sim::uploads+sim::moves+sim::positions;}
static bool atCurrentPosition() {
    if(!cursorWindow)return false;const auto it=sim::windows.find((uintptr_t)cursorWindow);if(it==sim::windows.end())return false;
    return it->second.position.x+cursorHotspot.x==sim::current.ptScreenPos.x &&
        it->second.position.y+cursorHotspot.y==sim::current.ptScreenPos.y;
}
// Count actual successful API writes. Image upload and initial show can be two
// calls at the same timestamp; report that separately from distinct update ticks.
// Contact edges are excluded explicitly at each call site, never by production
// source enum/deadline values. A >=24ms gap is an external test requirement.
static Json auditPositionTicks(LongScenario& test,size_t from,ULONGLONG prior=0) {
    unsigned raw=0,ticks=0,sameTick=0;ULONGLONG last=prior,minGap=~ULONGLONG{0};
    for(size_t i=from;i<sim::calls.size();++i) {
        const auto& call=sim::calls[i];const auto name=call["call"].get<std::string>();
        if(name!="move"&&name!="position"&&name!="upload")continue;
        ++raw;const auto tick=call["tick"].get<ULONGLONG>();
        if(last==tick){++sameTick;continue;}
        if(last){const auto gap=tick-last;minGap=std::min(minGap,gap);test.expect(tick>=last&&gap>=24,"continuous-position-gap-at-least-24ms",(unsigned)i);}
        last=tick;++ticks;
    }
    return {{"successfulPositionApiCalls",raw},{"distinctUpdateTicks",ticks},{"sameTickAdditionalCalls",sameTick},
        {"minimumGapMs",minGap==~ULONGLONG{0}?Json(nullptr):Json(minGap)}};
}
static void auditTrace(LongScenario& test,bool wrapped) {
    const auto trace=cursorTransitionState();const auto& entries=trace["entries"];
    test.expect(trace["capacity"]==64&&entries.size()<=64,"production-trace-capacity-is-64");
    test.expect(entries.size()==std::min<unsigned long long>(cursorTransitionCount,64),"trace-length-matches-bounded-history");
    unsigned long long sequence=cursorTransitionCount-(unsigned long long)entries.size();
    for(const auto& entry:entries)test.expect(entry["sequence"]==++sequence,"trace-retains-contiguous-last-sequences");
    if(wrapped)test.expect(cursorTransitionCount>64&&entries.size()==64,"long-sequence-exercises-ring-wrap");
    test.metrics["traceTotal"]=cursorTransitionCount;test.metrics["traceEntries"]=entries.size();
}
static void normalStopAndIdle(LongScenario& test,unsigned idleTicks) {
    sim::current.flags=CURSOR_SHOWING;sim::current.hCursor=(HCURSOR)(uintptr_t)1;
    core.pads[0].down=core.pads[1].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
    test.expect(!cursorTracking&&!cursorRetained&&!visible()&&!cursorvisibility::hook,"normal-UP-releases-display-and-observer");
    const auto reads=sim::reads,writes=positionCalls(),shows=sim::shows;
    for(unsigned i=0;i<idleTicks;++i){++sim::now;bus();}
    test.expect(sim::reads==reads&&positionCalls()==writes&&sim::shows==shows&&sim::messages.empty(),"stopped-owner-does-no-background-work");
}
static void longSteadyMoves() {
    reset();LongScenario test{"r3-long-Deck-MOVE-BUS-budget"};down();const auto begin=sim::calls.size();const auto start=sim::now;
    constexpr unsigned duration=12000,movesPerTick=3,busPerTick=4;size_t maxPending=0;
    for(unsigned tick=1;tick<=duration;++tick) {
        sim::now=start+tick;
        for(unsigned n=0;n<movesPerTick;++n){sim::current.ptScreenPos={(LONG)(200+tick*3+n),(LONG)(150+tick)};cursorContactChanged(sim::dispatchWindow,false,true);}
        for(unsigned n=0;n<busPerTick;++n)cursorRequestSync();maxPending=std::max(maxPending,sim::messages.size());drain();
        test.expect(visible()&&cursorTracking&&!cursorRetained,"continuous-Deck-contact-keeps-one-fallback",tick);
        test.expect(sim::uploads==1&&sim::shows==1&&sim::hides==0,"steady-moves-never-reupload-or-reshow",tick);
    }
    test.expect(maxPending<=1&&sim::moves==duration/24&&atCurrentPosition(),"BUS-coalesces-and-position-catches-up-without-starvation");
    test.metrics=auditPositionTicks(test,begin,start);test.metrics["virtualDurationMs"]=duration;
    test.metrics["deckMoveCalls"]=duration*movesPerTick;test.metrics["busRequests"]=duration*busPerTick;test.metrics["maxPendingMessages"]=maxPending;
    test.metrics["bitmapUploads"]=sim::uploads;test.metrics["showsDuringGesture"]=sim::shows;
    normalStopAndIdle(test,1000);auditTrace(test,false);test.finish();
}
static void longHintBursts() {
    reset();LongScenario test{"r3-long-same-frame-hints-use-current-state"};down();const auto begin=sim::calls.size();const auto start=sim::now;
    constexpr unsigned bursts=320,hints=64;size_t maxPending=0;
    for(unsigned round=1;round<=bursts;++round) {
        sim::now=start+round*24;sim::current.ptScreenPos.x+=3;
        const bool finalSuppressed=(round%2)==0;const auto beforeQueries=cursorvisibility::queries;
        for(unsigned hint=0;hint<hints;++hint) {
            // Alternate transient snapshots without dispatching; the last hint id
            // deliberately disagrees with the state ultimately read by the UI.
            sim::current.flags=(hint%2)?CURSOR_SHOWING:CURSOR_SUPPRESSED;
            edge(hint+1==hints?(finalSuppressed?EVENT_OBJECT_SHOW:EVENT_OBJECT_HIDE):((hint%2)?EVENT_OBJECT_SHOW:EVENT_OBJECT_HIDE));
        }
        maxPending=std::max(maxPending,sim::messages.size());test.expect(sim::messages.size()==1,"same-frame-hints-have-one-pending-message",round);
        sim::current.flags=finalSuppressed?CURSOR_SUPPRESSED:CURSOR_SHOWING;drain();
        test.expect(cursorvisibility::queries==beforeQueries+1,"hint-burst-performs-one-current-state-query",round);
        test.expect(visible()==finalSuppressed,"last-event-id-does-not-control-visibility",round);
        const auto writes=positionCalls(),shows=sim::shows,hides=sim::hides;
        for(unsigned repeat=0;repeat<8;++repeat)bus();
        test.expect(positionCalls()==writes&&sim::shows==shows&&sim::hides==hides,"same-tick-BUS-does-not-repeat-visibility-or-position",round);
    }
    test.expect(sim::uploads==1&&sim::shows==1+bursts/2&&sim::hides==bursts/2,"unchanged-image-handoffs-do-not-reupload");
    test.metrics=auditPositionTicks(test,begin,start);test.metrics["hintCount"]=bursts*hints;test.metrics["bursts"]=bursts;test.metrics["maxPendingMessages"]=maxPending;
    test.metrics["bitmapUploads"]=sim::uploads;auditTrace(test,true);normalStopAndIdle(test,200);test.finish();
}
static void longFailureRecovery() {
    reset();LongScenario test{"r3-long-upload-query-failures-recover"};sim::failUpload=true;down();const auto start=sim::now;const auto begin=sim::calls.size();
    constexpr unsigned slots=200;unsigned injectedQueryFailures=0;size_t maxPending=0;
    for(unsigned slot=1;slot<=slots;++slot) {
        sim::now=start+slot*24;sim::current.ptScreenPos.x+=2;
        // A long cold upload outage, then a visible shape replacement outage,
        // then twenty consecutive periodic-query failures. No replayed backlog.
        sim::failUpload=slot<32||(slot>=64&&slot<72);
        if(slot==64)sim::current.hCursor=(HCURSOR)(uintptr_t)3;
        const bool queryFails=(slot<32&&slot%4==0)||(slot>=96&&slot<116);
        if(queryFails){sim::readResults.push_back(false);++injectedQueryFailures;}
        const auto writes=positionCalls(),uploads=sim::uploads,shows=sim::shows,hides=sim::hides;
        for(unsigned repeat=0;repeat<4;++repeat)cursorRequestSync();maxPending=std::max(maxPending,sim::messages.size());drain();
        test.expect(sim::readResults.empty(),"each-injected-query-failure-was-consumed",slot);
        if(slot<32)test.expect(!visible()&&cursorTracking&&!cursorRetained&&sim::uploads==0&&sim::shows==0,"cold-upload-failure-does-not-invent-a-frame",slot);
        else test.expect(visible()&&sim::shows==1&&sim::hides==0,"warm-failures-preserve-last-visible-frame",slot);
        if(slot>=64&&slot<72)test.expect(cursorShape==(HCURSOR)(uintptr_t)1&&sim::uploads==1,"failed-replacement-keeps-previous-image",slot);
        if(slot>=72)test.expect(cursorShape==(HCURSOR)(uintptr_t)3&&sim::uploads==2,"replacement-recovers-once-without-reupload-loop",slot);
        if(queryFails)test.expect(positionCalls()==writes&&sim::uploads==uploads&&sim::shows==shows&&sim::hides==hides,"query-failure-does-not-submit-or-hide",slot);
    }
    test.expect(maxPending<=1&&sim::uploads==2&&sim::shows==1&&atCurrentPosition(),"long-fault-sequence-recovers-through-normal-BUS");
    test.metrics=auditPositionTicks(test,begin);test.metrics["periodicSlots"]=slots;test.metrics["injectedQueryFailures"]=injectedQueryFailures;
    test.metrics["bitmapUploads"]=sim::uploads;test.metrics["maxPendingMessages"]=maxPending;
    normalStopAndIdle(test,200);auditTrace(test,false);test.finish();
}
static void longAlternatingContacts() {
    reset();LongScenario test{"r3-long-dual-contact-mode-switch-stale-messages"};
    const HWND second=(HWND)(uintptr_t)3;constexpr unsigned rounds=32,duration=192;unsigned moves=0,staleDeliveries=0;Json phases=Json::array();
    for(unsigned round=0;round<rounds;++round) {
        core.modes={Mode::Deck,Mode::Deck};core.pads[0].down=true;cursorContactChanged(sim::dispatchWindow);
        core.pads[1].down=true;cursorContactChanged(second);const auto oldHook=cursorvisibility::hook;const auto oldGeneration=cursorvisibility::generation;
        const auto installs=sim::hookInstalls;const auto start=sim::now;const auto begin=sim::calls.size();
        for(unsigned tick=1;tick<=duration;++tick) {
            sim::now=start+tick;sim::current.ptScreenPos.x++;
            cursorContactChanged((tick%2)?sim::dispatchWindow:second,false,true);++moves;
            if(tick%5==0)edge(EVENT_OBJECT_HIDE);bus();
            test.expect(cursorvisibility::hook==oldHook&&cursorvisibility::generation==oldGeneration&&sim::hookInstalls==installs,"alternating-Deck-MOVE-does-not-reinstall-observer",tick);
        }
        phases.push_back(auditPositionTicks(test,begin,start));
        edge();test.expect(sim::messages.size()==1,"capture-one-old-generation-visibility-message",round);
        const auto oldMessage=sim::messages.front();sim::messages.pop_front();
        // Configuration changes release before changing modes in production.
        // Invoke that cursor boundary here; do not invent a live config mutation.
        core.pads[0].down=core.pads[1].down=false;cursorContactChanged(sim::dispatchWindow,false);
        core.modes={Mode::Deck,Mode::Mouse};core.pads[1].down=true;cursorContactChanged(second);
        core.pads[0].down=true;cursorContactChanged(sim::dispatchWindow);
        test.expect(!cursorDeckOnlyTracking&&!cursorvisibility::hook,"mixed-contact-mode-has-no-Deck-observer",round);
        const auto reads=sim::reads,writes=positionCalls();dispatch(oldMessage);++staleDeliveries;
        test.expect(sim::reads==reads&&positionCalls()==writes,"old-Deck-message-cannot-drive-mixed-owner",round);
        core.pads[1].down=false;cursorContactChanged(second,true);edge(EVENT_OBJECT_HIDE);
        test.expect(cursorDeckOnlyTracking&&cursorvisibility::pending,"remaining-Deck-contact-gets-fresh-observer",round);
        const auto newGeneration=cursorvisibility::generation;dispatch(oldMessage);++staleDeliveries;
        cursorvisibility::onEvent(oldHook,EVENT_OBJECT_HIDE,nullptr,OBJID_CURSOR,0,0,(DWORD)sim::now);
        test.expect(cursorvisibility::generation==newGeneration&&cursorvisibility::pending&&sim::messages.size()==1,"old-message-and-old-hook-cannot-clear-fresh-pending",round);
        drain();test.expect(visible()&&!cursorvisibility::pending,"current-generation-still-renders-after-stale-delivery",round);
        sim::current.flags=CURSOR_SHOWING;core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();
        test.expect(!visible()&&!cursorTracking&&!cursorRetained&&!cursorvisibility::hook,"mixed-to-Deck-release-completes",round);
        sim::current.flags=CURSOR_SUPPRESSED;sim::now+=24;
    }
    test.expect(sim::uploads==1,"mode-transitions-reuse-unchanged-cached-image");
    test.metrics={{"rounds",rounds},{"alternatingMoves",moves},{"staleMessageDeliveries",staleDeliveries},{"continuousPhaseBudgets",phases},{"bitmapUploads",sim::uploads}};
    auditTrace(test,true);normalStopAndIdle(test,200);test.finish();
}
static void longDelayedPeriodic() {
    reset();LongScenario test{"r3-long-delayed-periodic-and-event-share-budget"};down();const auto begin=sim::calls.size();const auto start=sim::now;
    constexpr unsigned rounds=180;const std::array<unsigned,6> delays{0,1,23,24,80,241};size_t maxPending=0;unsigned delayedTicks=0;
    for(unsigned round=0;round<rounds;++round) {
        // Start each experiment at least one real budget interval after the last
        // dispatch. Timer requests are BUS-only while UI dispatch is delayed.
        sim::now+=24;sim::current.flags=CURSOR_SHOWING;edge();drain();
        test.expect(!visible(),"system-cursor-handoff-precedes-delayed-show",round);
        sim::current.flags=CURSOR_SUPPRESSED;sim::current.ptScreenPos.x+=7;
        const unsigned delay=delays[round%delays.size()];delayedTicks+=delay;
        cursorRequestSync();edge(EVENT_OBJECT_SHOW);
        for(unsigned tick=0;tick<delay;++tick) {
            ++sim::now;sim::current.ptScreenPos.x++;
            for(unsigned n=0;n<4;++n)cursorRequestSync();edge((tick%2)?EVENT_OBJECT_SHOW:EVENT_OBJECT_HIDE);
            maxPending=std::max(maxPending,sim::messages.size());
            test.expect(sim::messages.size()<=2,"UI-stall-keeps-one-periodic-and-one-visibility-message",tick);
        }
        test.expect(sim::messages.size()==2,"both-producers-queued-once-before-resume",round);
        const auto first=sim::messages.front();sim::messages.pop_front();const auto second=sim::messages.front();sim::messages.pop_front();
        const auto& periodic=first.id==kCursorSyncMessage?first:second;const auto& notification=first.id==cursorvisibility::kMessage?first:second;
        const auto writes=positionCalls(),reads=sim::reads,shows=sim::shows;
        if(round%2){dispatch(periodic);dispatch(notification);}else{dispatch(notification);dispatch(periodic);}
        test.expect(visible()&&atCurrentPosition()&&sim::shows==shows+1&&positionCalls()==writes+1,"resume-shows-latest-position-once-without-catchup-burst",round);
        test.expect(sim::reads-reads<=(round%2?2u:1u),"queued-periodic-cannot-double-query-after-event-spent-slot",round);
        const auto after=positionCalls();for(unsigned n=0;n<16;++n)bus();
        test.expect(positionCalls()==after&&sim::messages.empty(),"same-tick-BUS-after-delayed-resume-does-no-extra-position",round);
    }
    test.expect(sim::uploads==1,"UI-delays-do-not-retransmit-stable-bitmap");
    test.metrics=auditPositionTicks(test,begin,start);test.metrics["rounds"]=rounds;test.metrics["virtualStallMs"]=delayedTicks;
    test.metrics["maximumSingleStallMs"]=241;test.metrics["maxPendingMessages"]=maxPending;
    auditTrace(test,true);normalStopAndIdle(test,400);test.finish();
}
static void longRetainedCycles() {
    reset();LongScenario test{"r3-long-release-retained-failure-and-idle-cycles"};constexpr unsigned gestures=96;unsigned retainedFaults=0;Json retainedBudgets=Json::array();
    for(unsigned gesture=0;gesture<gestures;++gesture) {
        sim::current.flags=CURSOR_SUPPRESSED;sim::current.hCursor=(HCURSOR)(uintptr_t)1;down();const auto start=sim::now;
        for(unsigned tick=1;tick<=48;++tick){sim::now=start+tick;sim::current.ptScreenPos.x++;cursorContactChanged(sim::dispatchWindow,false,true);bus();}
        ++sim::now;core.pads[0].down=false;if(gesture%3==0){sim::readResults.push_back(false);++retainedFaults;}
        cursorContactChanged(sim::dispatchWindow,true);drain();
        test.expect(visible()&&cursorRetained&&!cursorTracking&&cursorvisibility::hook,"normal-or-failed-UP-retains-the-existing-suppressed-frame",gesture);
        const auto retainedBegin=sim::calls.size(),retainedStart=sim::now;const auto uploads=sim::uploads,shows=sim::shows,hides=sim::hides;
        // An ordinary UP is an explicitly immediate contact edge. Its placement
        // is outside the MOVE budget; audit all subsequent retained updates.
        for(unsigned slot=1;slot<=6;++slot) {
            sim::now=retainedStart+slot*24;sim::current.ptScreenPos.x++;
            const bool fail=slot==2||slot==3;if(fail){sim::readResults.push_back(false);++retainedFaults;}
            const auto writes=positionCalls();bus();
            test.expect(visible()&&cursorRetained&&sim::uploads==uploads&&sim::shows==shows&&sim::hides==hides,"retained-query-outages-preserve-frame-without-redraw",slot);
            if(fail)test.expect(positionCalls()==writes,"failed-retained-query-cannot-position",slot);
        }
        retainedBudgets.push_back(auditPositionTicks(test,retainedBegin));
        sim::current.flags=CURSOR_SHOWING;sim::current.hCursor=nullptr;sim::now+=24;bus();
        test.expect(visible()&&cursorRetained,"retained-SHOWING-null-keeps-last-frame",gesture);
        sim::current.hCursor=(HCURSOR)(uintptr_t)1;edge(EVENT_OBJECT_HIDE);drain();
        test.expect(!visible()&&!cursorRetained&&!cursorvisibility::hook,"current-valid-system-state-completes-retained-handoff",gesture);
        const auto reads=sim::reads,writes=positionCalls();for(unsigned tick=0;tick<96;++tick){++sim::now;bus();}
        test.expect(sim::reads==reads&&positionCalls()==writes&&sim::messages.empty(),"completed-retained-gesture-has-no-tail-polling",gesture);
    }
    test.expect(sim::uploads==1&&sim::shows==gestures&&sim::hides==gestures,"long-gestures-show-and-hide-only-on-real-owner-edges");
    test.metrics={{"gestures",gestures},{"injectedQueryFailures",retainedFaults},{"bitmapUploads",sim::uploads},{"shows",sim::shows},{"hides",sim::hides},{"retainedPhaseBudgets",retainedBudgets}};
    auditTrace(test,true);normalStopAndIdle(test,200);test.finish();
}
static void runLongScenarios() {
    longSteadyMoves();longHintBursts();longFailureRecovery();longAlternatingContacts();longDelayedPeriodic();longRetainedCycles();
    reset();
    if(!sim::windows.empty()||!sim::bitmaps.empty()||!sim::dcs.empty()||sim::activeHook)throw std::runtime_error("long scenarios leaked fake resources");
}

// R4 adds three fault scenarios; each exercises both active and retained owners.
// Style callbacks, repair guard and retry policy remain actual production code.
static Json guardScenarios=Json::array();
static Json guardSnapshot(const char* phase) {
    return {{"phase",phase},{"tick",sim::now},{"window",(uintptr_t)cursorWindow},
        {"pending",cursorLayeredResetPending},{"busy",cursorLayeredResetting},{"active",cursorTracking.load()},
        {"retained",cursorRetained.load()},{"visible",cursorVisible},{"uploaded",cursorImageUploaded},
        {"nextPositionTick",cursorDeckNextPositionTick},{"nextActivePoll",cursorNextActivePoll.load()},
        {"nextRetainedPoll",cursorNextRetainedPoll.load()},{"cursorError",cursorError},
        {"styleOffCalls",sim::styleOffWrites},{"styleOnCalls",sim::styleOnWrites},
        {"styleFailures",sim::styleFailures},{"maxStyleDepth",sim::maxStyleDepth},
        {"bitmapUploads",sim::uploads},{"positionCalls",positionCalls()},{"shows",sim::shows},{"hides",sim::hides}};
}
static void waitOriginalRepairSlot(LongScenario& test) {
    const auto due=cursorDeckNextPositionTick;const auto writes=positionCalls(),styles=sim::styleWrites,reads=sim::reads;
    const bool pending=cursorLayeredResetPending;
    for(++sim::now;sim::now<due;++sim::now) {
        for(unsigned n=0;n<4;++n)bus();
        test.expect(positionCalls()==writes&&sim::styleWrites==styles&&sim::reads==reads,"repair-does-not-run-before-existing-position-slot");
        test.expect(cursorDeckNextPositionTick==due&&cursorLayeredResetPending==pending,"waiting-keeps-original-budget-and-pending");
    }
    sim::now=due;bus();
    test.expect(cursorDeckNextPositionTick==due+24,"repair-attempt-spends-only-normal-periodic-slot");
}
static void runGuardFault(const char* name,unsigned fault) {
    LongScenario test{name};Json lanes=Json::array();
    for(bool retained:{false,true}) {
        reset();down();
        if(retained){sim::now+=5;core.pads[0].down=false;cursorContactChanged(sim::dispatchWindow,true);drain();}
        const HWND originalWindow=cursorWindow;const auto originalHook=cursorvisibility::hook;
        const auto generation=cursorvisibility::generation;const HWND originalDispatch=cursorDispatchWindow.load();
        const auto budget=cursorDeckNextPositionTick,activePoll=cursorNextActivePoll.load(),retainedPoll=cursorNextRetainedPoll.load();
        const auto resetCount=cursorLayeredResets,resetFailures=cursorLayeredResetFailures;
        Json snapshots=Json::array();snapshots.push_back(guardSnapshot("before-damage"));
        // This real production callback immediately restores policy, hides the
        // damaged frame and marks the next upload for out-of-callback repair.
        const auto style=sim::getStyle(originalWindow,GWL_EXSTYLE);
        sim::setStyle(originalWindow,GWL_EXSTYLE,style&~(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE));
        test.expect(cursorWindow==originalWindow&&cursorLayeredResetPending&&!cursorImageUploaded&&!visible(),"style-loss-marks-hidden-same-window-for-repair");
        test.expect(ymcc::screeninput::displayInert(originalWindow),"external-style-loss-immediately-restores-inert-policy");
        test.expect(cursorDeckNextPositionTick==budget&&cursorNextActivePoll==activePoll&&cursorNextRetainedPoll==retainedPoll,"style-callback-does-not-reschedule-any-budget");
        const Json lossCallbacks=sim::styleCalls;sim::clearStyleObservations();
        const auto uploads=sim::uploads,shows=sim::shows,hides=sim::hides,writes=positionCalls();
        if(fault==0)sim::failLayeredOff=true;
        else if(fault==1)sim::failLayeredOn=true;
        else sim::failUpload=true;
        const auto assertOwner=[&] {
            test.expect(cursorWindow==originalWindow&&cursorTracking.load()==!retained&&cursorRetained.load()==retained,"repair-keeps-original-window-and-active-retained-owner");
            test.expect(cursorvisibility::hook==originalHook&&cursorvisibility::generation==generation&&cursorDispatchWindow==originalDispatch,"repair-does-not-rebind-observer-or-dispatch");
            test.expect(!cursorLayeredResetting&&sim::styleDepth==0&&sim::maxStyleDepth<=1,"busy-guard-unwinds-and-prevents-recursive-style-writes");
            const auto actual=sim::getStyle(originalWindow,GWL_EXSTYLE);
            test.expect(!sim::enabled(originalWindow)&&(actual&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE),"failed-repair-remains-disabled-transparent-and-nonactivating");
        };
        waitOriginalRepairSlot(test);assertOwner();snapshots.push_back(guardSnapshot("first-failure"));
        test.expect(!visible()&&!cursorImageUploaded&&sim::uploads==uploads&&sim::shows==shows&&sim::hides==hides&&positionCalls()==writes,"failure-does-not-show-move-reupload-or-repeat-hide");
        if(fault<2) {
            test.expect(cursorLayeredResetPending&&cursorLayeredResetFailures==resetFailures+1&&cursorLayeredResets==resetCount,"style-error-preserves-pending-and-is-counted-once");
            test.expect(sim::styleOffWrites==1&&sim::styleOnWrites==(fault==0?0u:1u)&&sim::styleFailures==1&&cursorError==ERROR_ACCESS_DENIED,"style-fault-hits-only-requested-off-or-on-call");
            const bool layered=(sim::getStyle(originalWindow,GWL_EXSTYLE)&WS_EX_LAYERED)!=0;
            test.expect(layered==(fault==0),"off-failure-keeps-layered-on-failure-leaves-layered-off");
            sim::failLayeredOff=sim::failLayeredOn=false;
        } else {
            test.expect(!cursorLayeredResetPending&&cursorLayeredResets==resetCount+1&&cursorLayeredResetFailures==resetFailures,"successful-style-reset-is-not-undone-by-upload-failure");
            test.expect(sim::styleOffWrites==1&&sim::styleOnWrites==1&&sim::styleFailures==0&&cursorError==ERROR_NOT_ENOUGH_MEMORY,"upload-fault-occurs-after-one-successful-off-on-pair");
            // Keep the bitmap failure for another budget slot. The guard must not
            // turn a normal upload retry into repeated layered off/on operations.
            waitOriginalRepairSlot(test);assertOwner();snapshots.push_back(guardSnapshot("second-upload-failure"));
            test.expect(sim::styleWrites==2&&!cursorLayeredResetPending&&cursorLayeredResets==resetCount+1&&!visible(),"repeated-upload-failure-does-not-repeat-layered-reset");
            sim::failUpload=false;
        }
        waitOriginalRepairSlot(test);assertOwner();snapshots.push_back(guardSnapshot("recovered"));
        test.expect(visible()&&cursorImageUploaded&&!cursorLayeredResetPending&&ymcc::screeninput::displayInert(originalWindow),"retry-recovers-valid-same-window-surface");
        test.expect(cursorLayeredResets==resetCount+1&&sim::uploads==uploads+1&&sim::shows==shows+1,"recovery-has-one-successful-reset-upload-and-show");
        test.expect(sim::styleOffWrites==(fault<2?2u:1u)&&sim::styleOnWrites==(fault==0?1u:fault==1?2u:1u),"retry-style-call-count-is-bounded");
        const auto styles=sim::styleWrites,stableShows=sim::shows,stableHides=sim::hides;
        const auto historyBegin=sim::calls.size(),historyStart=sim::now;
        for(unsigned frame=1;frame<=32;++frame) {
            sim::now=historyStart+frame*24;sim::current.ptScreenPos.x++;
            if(fault==2)sim::current.hCursor=(HCURSOR)(uintptr_t)((frame%2)?3:1);
            bus();assertOwner();
            test.expect(visible()&&sim::styleWrites==styles&&!cursorLayeredResetPending&&cursorLayeredResets==resetCount+1,"later-frames-never-repeat-off-on-after-success",frame);
            test.expect(sim::shows==stableShows&&sim::hides==stableHides,"later-moves-or-shape-updates-do-not-toggle-visibility",frame);
            if(fault<2)test.expect(sim::uploads==uploads+1,"stable-image-is-not-reuploaded-after-repair",frame);
        }
        const auto positionAudit=auditPositionTicks(test,historyBegin,historyStart);snapshots.push_back(guardSnapshot("after-32-frames"));
        const Json repairStyleCalls=sim::styleCalls;
        normalStopAndIdle(test,96);cursorShutdown();
        test.expect(!cursorWindow&&!cursorLayeredResetPending&&!cursorLayeredResetting&&!cursorvisibility::hook,"shutdown-clears-repair-state-and-owner");
        test.expect(sim::windows.empty()&&sim::bitmaps.empty()&&sim::dcs.empty(),"fault-scenario-releases-all-fake-resources");
        lanes.push_back({{"owner",retained?"retained":"active"},{"snapshots",snapshots},{"externalLossStyleCalls",lossCallbacks},
            {"repairStyleCalls",repairStyleCalls},{"continuousPositionAudit",positionAudit}});
    }
    check(name,test.violations==0);
    guardScenarios.push_back({{"name",name},{"ok",test.violations==0},{"assertions",test.assertions},{"violations",test.violations},
        {"firstFailures",test.firstFailures},{"lanes",lanes}});
}
static void runGuardScenarios() {
    runGuardFault("r4-guard-layered-off-failure-retries",0);
    runGuardFault("r4-guard-layered-on-failure-retries",1);
    runGuardFault("r4-guard-success-then-upload-failure-does-not-reset-again",2);
    reset();
}

// Field-evidence regressions execute the same header against deterministic API
// substitutes. Legacy headers must keep passing behavior checks; only explicitly
// named evidence checks require the new JSON schema. No new diagnostic globals
// are referenced, so this translation unit also compiles with Before headers.
static Json diagnosticScenarios=Json::array();
static Json placements(const Json& trace){return trace.value("placements",Json::object());}
static unsigned long long number(const Json& value,const char* key){return value.value(key,0ull);}
static bool changedBy(const Json& before,const Json& after,const char* key,unsigned long long delta){
    return number(after,key)>=number(before,key)&&number(after,key)-number(before,key)==delta;
}
static void saveDiagnostic(const char* name,const Json& before,const Json& after){
    diagnosticScenarios.push_back({{"name",name},{"before",before},{"after",after},
        {"apiQueries",sim::reads},{"apiUploads",sim::uploads},{"apiPositionOnlyMoves",sim::moves},
        {"apiSetWindowPos",sim::positions},{"apiShows",sim::shows},{"apiHides",sim::hides}});
}
static void diagnosticRoundTrip(){
    reset();down();drain();const auto before=cursorTransitionState();
    const auto first=before.at("entries").back();const auto start=sim::now;
    const std::array<POINT,3> path={POINT{240,120},POINT{180,190},POINT{200,150}};
    bool cadence=true;
    for(size_t i=0;i<path.size();++i){
        const auto reads=sim::reads,writes=positionCalls();sim::now=start+(i+1)*24-1;bus();
        cadence=cadence&&sim::reads==reads&&positionCalls()==writes;
        sim::current.ptScreenPos=path[i];++sim::now;bus();
        cadence=cadence&&sim::reads==reads+1&&positionCalls()==writes+1&&atCurrentPosition();
    }
    const auto after=cursorTransitionState();const auto last=after.at("entries").back();
    check("r5-round-trip-preserves-motion-ownership-and-budget",cadence&&visible()&&cursorTracking&&!cursorRetained&&
        core.pads[0].down&&cursorvisibility::hook&&sim::uploads==1&&sim::moves==3&&sim::positions==1&&
        sim::shows==1&&sim::hides==0&&after.at("total")==before.at("total")&&
        last.at("x")==first.at("x")&&last.at("y")==first.at("y"));
    check("r5-round-trip-summary-retains-intermediate-extrema",after.value("evidenceVersion",0)==1&&
        changedBy(first,last,"samples",3)&&changedBy(first,last,"positionChanges",3)&&
        number(last,"lastTick")==sim::now&&last.value("lastX",-1)==200&&last.value("lastY",-1)==150&&
        last.value("minX",-1)==180&&last.value("maxX",-1)==240&&
        last.value("minY",-1)==120&&last.value("maxY",-1)==190);
    saveDiagnostic("round-trip",before,after);
}
static void diagnosticForcedRaises(){
    reset();down();drain();const auto before=cursorTransitionState(),counts=placements(before);const auto start=sim::now;
    // A persistent synthetic predecessor forces the production needsRaise branch.
    // It is not a claim that contact MOVE normally raises a real HWND each time.
    sim::above=(HWND)(uintptr_t)0x900;constexpr unsigned steps=32;bool cadence=true;
    for(unsigned i=1;i<=steps;++i){
        const auto reads=sim::reads,writes=positionCalls();sim::now=start+i*24-1;bus();
        cadence=cadence&&sim::reads==reads&&positionCalls()==writes;
        sim::current.ptScreenPos={(LONG)(200+i*2),(LONG)(150+i)};++sim::now;bus();
        cadence=cadence&&sim::reads==reads+1&&sim::positions==i+1&&sim::moves==0&&atCurrentPosition();
    }
    const auto after=cursorTransitionState(),actual=placements(after);
    check("r5-forced-raise-keeps-legacy-movement-and-budget",cadence&&visible()&&cursorTracking&&!cursorRetained&&
        core.pads[0].down&&cursorvisibility::hook&&cursorDeckPositionOnlyEnabled&&cursorDeckPositionOnlyMoves==0&&
        sim::uploads==1&&sim::positions==steps+1&&sim::shows==1&&sim::hides==0);
    check("r5-forced-raise-counts-uncounted-placements",changedBy(counts,actual,"setWindowPos",steps)&&
        changedBy(counts,actual,"deckRaises",steps)&&changedBy(counts,actual,"setWindowPosFailures",0)&&
        number(actual,"lastRaisePredecessor")==0x900&&number(actual,"lastRaiseTick")==sim::now);
    saveDiagnostic("forced-raises",before,after);
}
static void diagnosticStationary(){
    reset();down();drain();const auto before=cursorTransitionState(),counts=placements(before);
    const auto first=before.at("entries").back();const auto start=sim::now;const auto reads=sim::reads;
    // Even a preceding HWND must not spend positioning work for unchanged XY.
    sim::above=(HWND)(uintptr_t)0x901;constexpr unsigned samples=96;bool cadence=true;
    for(unsigned i=1;i<=samples;++i){
        const auto priorReads=sim::reads;sim::now=start+i*24-1;bus();cadence=cadence&&sim::reads==priorReads;
        ++sim::now;bus();cadence=cadence&&sim::reads==priorReads+1;
    }
    const auto after=cursorTransitionState(),actual=placements(after);const auto last=after.at("entries").back();
    check("r5-stationary-samples-do-no-positioning-or-drawing",cadence&&sim::reads==reads+samples&&
        visible()&&atCurrentPosition()&&cursorTracking&&!cursorRetained&&core.pads[0].down&&cursorvisibility::hook&&
        cursorDeckNextPositionTick==sim::now+24&&sim::uploads==1&&sim::moves==0&&sim::positions==1&&
        sim::shows==1&&sim::hides==0&&after.at("total")==before.at("total"));
    check("r5-stationary-summary-counts-observations-without-motion",changedBy(first,last,"samples",samples)&&
        changedBy(first,last,"positionChanges",0)&&number(last,"lastTick")==sim::now&&
        last.value("minX",-1)==200&&last.value("maxX",-1)==200&&last.value("lastX",-1)==200&&
        last.value("minY",-1)==150&&last.value("maxY",-1)==150&&last.value("lastY",-1)==150&&
        changedBy(counts,actual,"unchanged",samples)&&changedBy(counts,actual,"setWindowPos",0)&&
        changedBy(counts,actual,"deckRaises",0)&&changedBy(counts,actual,"setWindowPosFailures",0));
    saveDiagnostic("stationary",before,after);
}
static void diagnosticInvalidSamples(){
    reset();down();drain();const auto before=cursorTransitionState();const auto reads=sim::reads,writes=positionCalls();
    const auto start=sim::now;
    for(unsigned i=0;i<2;++i){sim::readResults.push_back(false);sim::now+=24;bus();}
    bool behavior=visible()&&cursorTracking&&!cursorRetained&&sim::reads==reads+2&&positionCalls()==writes;
    for(const POINT point:{POINT{240,130},POINT{260,170},POINT{240,130}}){sim::current.ptScreenPos=point;sim::now+=24;bus();behavior=behavior&&atCurrentPosition();}
    const auto after=cursorTransitionState();Json entries=Json::array();
    // Inspect the visible segment under test plus its failures/recovery. The
    // earlier stationary motion-priming sample remains in the saved full trace.
    const auto firstSequence=number(before.at("entries").back(),"sequence");
    for(const auto& entry:after.at("entries"))if(number(entry,"sequence")>=firstSequence)entries.push_back(entry);
    behavior=behavior&&entries.size()==3&&entries.at(0).at("valid")==true&&entries.at(1).at("valid")==false&&
        entries.at(2).at("valid")==true&&entries.at(0).at("x")==200&&entries.at(2).at("x")==240;
    check("r5-query-failures-preserve-owner-frame-and-recovery",behavior&&sim::uploads==1&&sim::moves==3&&
        sim::positions==1&&sim::shows==1&&sim::hides==0&&core.pads[0].down&&cursorvisibility::hook);
    bool evidence=entries.size()==3;
    if(evidence){
        const auto& original=entries.at(0);const auto& invalid=entries.at(1);const auto& recovered=entries.at(2);
        evidence=number(original,"samples")==1&&original.value("minX",-1)==200&&original.value("maxX",-1)==200&&
            original.value("minY",-1)==150&&original.value("maxY",-1)==150&&
            number(invalid,"samples")==2&&number(invalid,"positionChanges")==0&&number(invalid,"lastTick")==start+48&&
            number(recovered,"samples")==3&&number(recovered,"positionChanges")==2&&number(recovered,"lastTick")==sim::now&&
            recovered.value("minX",-1)==240&&recovered.value("maxX",-1)==260&&recovered.value("lastX",-1)==240&&
            recovered.value("minY",-1)==130&&recovered.value("maxY",-1)==170&&recovered.value("lastY",-1)==130;
    }
    check("r5-invalid-observations-do-not-pollute-valid-segment-bounds",evidence);
    saveDiagnostic("invalid-samples",before,after);
}
static void diagnosticRingOverwrite(){
    reset();cursorDeckOnlyTracking=true;const auto before=cursorTransitionState();
    constexpr unsigned segments=80;bool behavior=true,evidence=true;
    for(unsigned i=0;i<segments;++i){
        CURSORINFO sample{sizeof(CURSORINFO),(DWORD)(i%2?CURSOR_SHOWING:CURSOR_SUPPRESSED),(HCURSOR)(uintptr_t)1,
            {(LONG)(500+i*10),(LONG)(300+i*4)}};
        const POINT first=sample.ptScreenPos;
        for(unsigned n=0;n<3;++n){
            sample.ptScreenPos=n==1?POINT{first.x+3,first.y-2}:first;
            sim::now=2000+i*12+n*3;cursorRecordSample(sample,true,CursorSampleSource::Periodic);
        }
    }
    const auto after=cursorTransitionState();const auto& entries=after.at("entries");
    behavior=after.at("capacity")==64&&after.at("total")==segments&&entries.size()==64&&cursorTransitions.size()==64;
    unsigned long long sequence=segments-64;
    for(const auto& entry:entries){
        ++sequence;const auto index=sequence-1;const auto x=500+index*10,y=300+index*4,tick=2000+index*12;
        behavior=behavior&&entry.at("sequence")==sequence&&entry.at("x")==x&&entry.at("y")==y&&
            entry.at("tick")==tick&&entry.at("valid")==true;
        evidence=evidence&&number(entry,"samples")==3&&number(entry,"positionChanges")==2&&number(entry,"lastTick")==tick+6&&
            number(entry,"lastX")==x&&number(entry,"lastY")==y&&number(entry,"minX")==x&&number(entry,"maxX")==x+3&&
            number(entry,"minY")==y-2&&number(entry,"maxY")==y;
    }
    check("r5-ring-overwrite-keeps-exact-last-64-transitions",behavior&&sim::reads==0&&positionCalls()==0&&
        sim::shows==0&&sim::hides==0&&sim::windows.empty()&&sim::messages.empty());
    check("r5-ring-overwrite-reinitializes-each-motion-summary",evidence&&entries.size()==64&&
        after.value("evidenceVersion",0)==1);
    saveDiagnostic("ring-overwrite",before,after);
}
static void diagnosticPlacementFailure(){
    reset();down();drain();const auto before=cursorTransitionState(),counts=placements(before);
    sim::above=(HWND)(uintptr_t)0x902;sim::current.ptScreenPos.x+=8;sim::failPosition=true;sim::now+=24;bus();
    const auto failed=cursorTransitionState(),failedCounts=placements(failed);
    bool behavior=visible()&&cursorTracking&&!cursorRetained&&!cursorPositionValid&&cursorError==ERROR_ACCESS_DENIED&&
        sim::positions==1&&sim::moves==0&&sim::uploads==1;
    sim::failPosition=false;sim::now+=24;bus();const auto after=cursorTransitionState(),actual=placements(after);
    check("r5-placement-failure-retries-without-changing-ownership",behavior&&visible()&&atCurrentPosition()&&
        cursorTracking&&!cursorRetained&&cursorPositionValid&&cursorError==0&&core.pads[0].down&&cursorvisibility::hook&&
        sim::positions==2&&sim::moves==0&&sim::uploads==1&&sim::shows==1&&sim::hides==0&&cursorDeckPositionOnlyMoves==0);
    check("r5-placement-evidence-separates-failure-from-successful-raise",changedBy(counts,failedCounts,"setWindowPos",0)&&
        changedBy(counts,failedCounts,"deckRaises",0)&&changedBy(counts,failedCounts,"setWindowPosFailures",1)&&
        number(failedCounts,"lastRaisePredecessor")==number(counts,"lastRaisePredecessor")&&
        number(failedCounts,"lastRaiseTick")==number(counts,"lastRaiseTick")&&
        changedBy(counts,actual,"setWindowPos",1)&&changedBy(counts,actual,"deckRaises",1)&&
        changedBy(counts,actual,"setWindowPosFailures",1)&&number(actual,"lastRaisePredecessor")==0x902&&
        number(actual,"lastRaiseTick")==sim::now);
    diagnosticScenarios.push_back({{"name","placement-failure"},{"failure",failed}});
    saveDiagnostic("placement-retry",before,after);
}
static void runDiagnosticScenarios(){
    diagnosticRoundTrip();diagnosticForcedRaises();diagnosticStationary();
    diagnosticInvalidSamples();diagnosticRingOverwrite();diagnosticPlacementFailure();reset();
}

int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: cursor-r1-simulation.exe report.json\n";return 2;}
    try{run();runLongScenarios();runGuardScenarios();runDiagnosticScenarios();}catch(const std::exception& e){++failures;cases.push_back({{"name","harness-exception"},{"ok",false},{"error",e.what()}});}
    Json result={{"ok",failures==0},{"failures",failures},{"cases",cases},{"scope","actual production cursor headers with deterministic API substitutes"},
        {"realWindowsCreated",0},{"realHooksRegistered",0},{"realInputInjected",0},{"hardwareCalls",0},{"fieldFlickerVerified",false},{"longScenarios",longScenarios},{"longScenarioCount",longScenarios.size()},
        {"notificationCoverageVerified",false},{"processCpuMeasured",false},
        {"guardScenarioCount",guardScenarios.size()},{"guardScenarios",guardScenarios},{"diagnosticScenarios",diagnosticScenarios}};
    std::ofstream out(argv[1]);out<<result.dump(2)<<'\n';if(!out)return 2;std::cout<<"Cases: "<<cases.size()<<", failures: "<<failures<<'\n';return failures?1:0;
}