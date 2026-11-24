// Explicit offline selection of the original game-visibility consumer.
// Includes the actual production visibility header; no real HWND/input APIs.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <algorithm>
#include <vector>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include "json.hpp"
using Json=nlohmann::json;
namespace visibilityfixture {
struct Window {bool visible=true,enabled=true,iconic=false,safe=true;DWORD pid=1;RECT rect{0,0,100,100};LONG_PTR ex=WS_EX_TOPMOST;};
static std::array<Window,32> nodes{};
static std::vector<HWND> stack;
static HWND foreground=nullptr,foregroundRoot=nullptr;
static std::array<unsigned,32> windowVisibleReads{};
static unsigned ancestorReads=0;
static unsigned posts=0,raises=0,changes=0,hookCalls=0,unhooks=0,armed=0,killed=0,unexpectedFlags=0;
static unsigned slotReads=0,surfaceChecks=0;
static unsigned ownerAdmissionReads=0,controlVisibleReads=0;
static unsigned foregroundVisibleReads=0,foregroundIconicReads=0,foregroundPidReads=0,foregroundRectReads=0;
static bool inputAllowed=true,timerFailure=false,postFailure=false,reenterDisplay=false;
static void (*displayReentry)()=nullptr;
static void (*surfaceReentry)()=nullptr;
static void (*showReentry)()=nullptr;
static void (*failureReentry)()=nullptr;
static void (*placeReentry)()=nullptr;
struct Timer {HWND window=nullptr;UINT_PTR id=0;UINT delay=0;TIMERPROC proc=nullptr;bool active=false;};
static Timer timer;
static HWND handle(int i){return reinterpret_cast<HWND>(static_cast<uintptr_t>(i));}
static Window& node(HWND h){return nodes[reinterpret_cast<uintptr_t>(h)];}
static BOOL WINAPI visible(HWND h){if(h){++windowVisibleReads[reinterpret_cast<uintptr_t>(h)];if(h==foreground)++foregroundVisibleReads;else ++controlVisibleReads;}return h&&node(h).visible;}
static BOOL WINAPI iconic(HWND h){if(h&&h==foreground)++foregroundIconicReads;return h&&node(h).iconic;}
static HWND WINAPI currentForeground(){return foreground;}
static HWND WINAPI ancestor(HWND h,UINT){++ancestorReads;return h==foreground&&foregroundRoot?foregroundRoot:h;}
static DWORD WINAPI threadPid(HWND h,LPDWORD p){if(!h)return 0;if(h==foreground)++foregroundPidReads;if(p)*p=node(h).pid;return 1;}
static DWORD WINAPI currentPid(){return 1;}
static HWND WINAPI desktop(){return handle(30);}
static HWND WINAPI shell(){return handle(31);}
static BOOL WINAPI rect(HWND h,LPRECT r){if(!h)return FALSE;if(h==foreground)++foregroundRectReads;*r=node(h).rect;return TRUE;}
static BOOL WINAPI intersect(LPRECT out,const RECT* a,const RECT* b){
 *out={std::max(a->left,b->left),std::max(a->top,b->top),std::min(a->right,b->right),std::min(a->bottom,b->bottom)};
 return out->left<out->right&&out->top<out->bottom;
}
static LONG_PTR WINAPI style(HWND h,int){return node(h).ex;}
static HWND WINAPI previous(HWND h,UINT){auto i=std::find(stack.begin(),stack.end(),h);return i==stack.end()||i==stack.begin()?nullptr:*(i-1);}
static BOOL WINAPI place(HWND h,HWND after,int,int,int,int,UINT flags){
 ++raises;if(after!=HWND_TOPMOST||flags!=(SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE))++unexpectedFlags;
 node(h).ex|=WS_EX_TOPMOST;stack.erase(std::remove(stack.begin(),stack.end(),h),stack.end());stack.insert(stack.begin(),h);
 if(const auto reenter=placeReentry){placeReentry=nullptr;reenter();}return TRUE;
}
static HWINEVENTHOOK WINAPI hook(DWORD,DWORD,HMODULE,WINEVENTPROC,DWORD,DWORD,DWORD){return reinterpret_cast<HWINEVENTHOOK>(static_cast<uintptr_t>(++hookCalls));}
static BOOL WINAPI unhook(HWINEVENTHOOK){++unhooks;return TRUE;}
static BOOL WINAPI post(HWND,UINT,WPARAM,LPARAM){++posts;return !postFailure;}
static BOOL WINAPI show(HWND h,int how){node(h).visible=how!=SW_HIDE;++changes;if(const auto reenter=showReentry){showReentry=nullptr;reenter();}return TRUE;}
static UINT_PTR WINAPI setTimer(HWND h,UINT_PTR id,UINT delay,TIMERPROC p){if(timerFailure)return 0;timer={h,id,delay,p,true};++armed;return id;}
static BOOL WINAPI killTimer(HWND h,UINT_PTR id){if(timer.window==h&&timer.id==id)timer.active=false;++killed;return TRUE;}
static void putAbove(HWND h){stack.erase(std::remove(stack.begin(),stack.end(),h),stack.end());stack.insert(stack.begin(),h);}
static void tick(){const auto t=timer;if(t.active)t.proc(t.window,WM_TIMER,t.id,0);}
}
namespace ymcc::screeninput {
inline bool ensureDisplay(HWND h){if(visibilityfixture::reenterDisplay&&visibilityfixture::displayReentry){visibilityfixture::reenterDisplay=false;visibilityfixture::displayReentry();}return h&&visibilityfixture::node(h).safe;}
struct Surface {
    unsigned calls=0;bool safe=true;HRESULT error=S_OK;
    bool ensureMousePass(){++calls;++visibilityfixture::surfaceChecks;if(const auto reenter=visibilityfixture::surfaceReentry){visibilityfixture::surfaceReentry=nullptr;reenter();}error=safe?S_OK:E_FAIL;return safe;}
};
}
namespace ymcc::customtouch {
struct Item {std::string id;HWND window=nullptr,outline=nullptr;ymcc::screeninput::Surface surface;int kind=13;bool wanted=true;};
inline std::vector<std::shared_ptr<Item>> items;
inline UINT64 refreshEpoch=0;
inline unsigned draws=0;
inline bool draw(Item&){++draws;return true;}
inline bool itemWanted(const Item& item){return item.wanted;}
inline bool visible(){for(const auto& item:items)if(visibilityfixture::visible(item->window))return true;return false;}
}
namespace ymcc::screenpads {
template<size_t N> struct CountedWindows : std::array<HWND,N> {
    HWND& operator[](size_t i){++visibilityfixture::slotReads;return std::array<HWND,N>::operator[](i);}
};
inline CountedWindows<2> windows{};inline CountedWindows<7> buttonWindows{};
inline std::array<HWND,2> outlines{};inline std::array<HWND,7> buttonOutlines{};
inline bool nativeInputCandidateRequested=true;
inline std::array<ymcc::screeninput::Surface,2> nativeInputSurfaces{};
inline std::array<ymcc::screeninput::Surface,7> nativeButtonSurfaces{};
inline UINT64 padRefreshEpoch=0,buttonRefreshEpoch=0;
inline unsigned failureReceipts=0;inline bool failedButton=false,failedCustom=false;inline int failedIndex=-1;
inline void recordNativeInputFailure(bool button,int index,const ymcc::screeninput::Surface&,bool custom){
    ++failureReceipts;failedButton=button;failedIndex=index;failedCustom=custom;
    if(const auto reenter=visibilityfixture::failureReentry){visibilityfixture::failureReentry=nullptr;reenter();}
}
inline std::atomic<HWND> owner{nullptr};inline bool initialized=true,inputTeardown=false;inline DWORD windowError=0,cursorError=0;
inline struct {bool pending=false,idleSuspended=false;} displayTransitionFence;
inline std::atomic<bool> presentationSuspended{false};
inline struct {
    bool hidden=false;
    UINT64 revealSerial=0,revealPadEpoch=0,revealButtonEpoch=0;
    std::array<DWORD,5> revealErrors{};
    unsigned retryRevealMask=0;
} autoHide;
inline constexpr std::array<unsigned,5> autoHideOutlineBits={1,2,4,8,16};
inline unsigned releaseCalls=0;inline bool heldTouch=false;
inline bool onOwnerThread(HWND h){++visibilityfixture::ownerAdmissionReads;return h==owner.load();}
inline bool sharedInputAllowed(){return visibilityfixture::inputAllowed;}
inline bool buttonsVisible(){for(auto h:buttonWindows)if(h&&visibilityfixture::visible(h))return true;return false;}
inline void release(){++releaseCalls;heldTouch=false;}
struct Visual{HWND window=nullptr;bool visible=false;};inline std::array<Visual,2> contactVisuals{};
inline void hideContactVisual(Visual& v){v.visible=false;visibilityfixture::node(v.window).visible=false;}
inline HWND cursorWindow=nullptr;inline bool cursorVisible=false;inline unsigned cursorStackRaises=0,cursorStackRaiseFailures=0;
inline void cursorHideVisual(){cursorVisible=false;}
enum class Layout {Dual,Single};
struct Config{int transparency=80;bool masterEnabled=true,enabled=true;Layout layout=Layout::Dual;};
inline Config getConfig(){return {};}
inline int surfaceMode(const Config&,int side){return side;}
inline bool permitted(int){return true;}
inline bool buttonPermitted(const Config&,int){return true;}
inline unsigned artworkCalls=0;inline bool pictureLost=false;
inline bool renderOutline(int,int,bool force){if(force){++artworkCalls;pictureLost=false;}return true;}
inline bool renderButton(int,int,bool force){if(force){++artworkCalls;pictureLost=false;}return true;}
}
#define IsWindowVisible visibilityfixture::visible
#define IsIconic visibilityfixture::iconic
#define GetForegroundWindow visibilityfixture::currentForeground
#define GetAncestor visibilityfixture::ancestor
#define GetWindowThreadProcessId visibilityfixture::threadPid
#define GetCurrentProcessId visibilityfixture::currentPid
#define GetDesktopWindow visibilityfixture::desktop
#define GetShellWindow visibilityfixture::shell
#define GetWindowRect visibilityfixture::rect
#define IntersectRect visibilityfixture::intersect
#define GetWindowLongPtrW visibilityfixture::style
#define GetWindow visibilityfixture::previous
#define SetWindowPos visibilityfixture::place
#define SetWinEventHook visibilityfixture::hook
#define UnhookWinEvent visibilityfixture::unhook
#define PostMessageW visibilityfixture::post
#define ShowWindow visibilityfixture::show
#define SetTimer visibilityfixture::setTimer
#define KillTimer visibilityfixture::killTimer
#include "screen_touchpad_visibility.h"
#undef IsWindowVisible
#undef IsIconic
#undef GetForegroundWindow
#undef GetAncestor
#undef GetWindowThreadProcessId
#undef GetCurrentProcessId
#undef GetDesktopWindow
#undef GetShellWindow
#undef GetWindowRect
#undef IntersectRect
#undef GetWindowLongPtrW
#undef GetWindow
#undef SetWindowPos
#undef SetWinEventHook
#undef UnhookWinEvent
#undef PostMessageW
#undef ShowWindow
#undef SetTimer
#undef KillTimer
#ifdef YMCC_SCREEN_VISIBILITY_OLD_CONTROL
namespace ymcc::screenpads {inline bool queueOwnerVisibility(bool raise,bool){return queueOwnerVisibility(raise);}}
#endif
using namespace ymcc::screenpads;
static void resetVisibilityFixture(){
 stopOwnerVisibility();visibilityfixture::nodes={};visibilityfixture::stack={};
 visibilityfixture::foregroundRoot=nullptr;visibilityfixture::ancestorReads=0;visibilityfixture::windowVisibleReads={};
 owner=visibilityfixture::handle(1);windows={{visibilityfixture::handle(2),visibilityfixture::handle(4)}};
 outlines={visibilityfixture::handle(3),visibilityfixture::handle(5)};buttonWindows={};buttonOutlines={};
 buttonWindows[1]=visibilityfixture::handle(6);buttonOutlines[1]=visibilityfixture::handle(7);
 initialized=true;inputTeardown=false;visibilityfixture::inputAllowed=true;visibilityfixture::foreground=visibilityfixture::handle(20);
 visibilityfixture::node(visibilityfixture::foreground).pid=2;visibilityfixture::node(visibilityfixture::foreground).ex=0;
 visibilityfixture::stack={outlines[0],windows[0],outlines[1],windows[1],buttonOutlines[1],buttonWindows[1],visibilityfixture::foreground};
 visibilityfixture::posts=visibilityfixture::raises=visibilityfixture::changes=visibilityfixture::armed=visibilityfixture::killed=visibilityfixture::unexpectedFlags=0;
 visibilityfixture::timerFailure=visibilityfixture::postFailure=visibilityfixture::reenterDisplay=false;
 releaseCalls=0;heldTouch=true;contactVisuals={};cursorWindow=nullptr;cursorVisible=false;
 nativeInputSurfaces={};nativeButtonSurfaces={};ymcc::customtouch::items.clear();ymcc::customtouch::draws=0;
 failureReceipts=0;failedButton=failedCustom=false;failedIndex=-1;windowError=cursorError=0;
 visibilityfixture::surfaceReentry=nullptr;visibilityfixture::showReentry=nullptr;displayTransitionFence.pending=false;
 visibilityfixture::failureReentry=nullptr;visibilityfixture::placeReentry=nullptr;
 autoHide={};displayTransitionFence.idleSuspended=false;presentationSuspended=false;
 visibilityfixture::slotReads=visibilityfixture::surfaceChecks=0;
}
static void consumeVisibility(){if(ownerVisibilityQueued)consumeOwnerVisibility(ownerVisibilityEpoch);}
static void beginVisibility(){resetVisibilityFixture();syncOwnerVisibility();consumeVisibility();}
#ifndef YMCC_SCREEN_VISIBILITY_OLD_CONTROL
static int checkReorderAdmission(const wchar_t* reportPath){
 using namespace visibilityfixture;
 Json cases=Json::array();bool ok=true;
 const auto check=[&](const char* name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 const auto observe=[](DWORD event,HWND h,LONG object=OBJID_WINDOW){screenVisibilityObserved(nullptr,event,h,object,CHILDID_SELF,0,0);};
 beginVisibility();stopOwnerVisibilityRechecks();
 // Dedicated-button-only operation: both pad checks fail before finding the button.
 node(windows[0]).visible=node(windows[1]).visible=false;
 node(outlines[0]).visible=node(outlines[1]).visible=false;
 const auto initialPosts=posts;const auto initialEvents=ownerVisibilityEvents;
 observe(EVENT_OBJECT_REORDER,desktop(),OBJID_CLIENT);
 check("first desktop reorder admits one check",posts==initialPosts+1&&ownerVisibilityQueued&&!ownerVisibilityRecheck.timer);
 ownerAdmissionReads=controlVisibleReads=0;
 for(int i=0;i<1000;++i)observe(EVENT_OBJECT_REORDER,i%2?foreground:desktop(),i%2?OBJID_WINDOW:OBJID_CLIENT);
 const auto ownerReads=ownerAdmissionReads,visibleReads=controlVisibleReads;
 check("queued reorders do not repeat owner or control visibility admission",ownerReads==0&&visibleReads==0);
 check("both reorder sources retain event accounting and one post",ownerVisibilityEvents-initialEvents==1001&&posts==initialPosts+1&&!ownerVisibilityRaiseRequested&&!ownerVisibilityArtworkRequested&&!ownerVisibilityRecheck.timer);
 putAbove(foreground);consumeVisibility();
 check("already queued check reads the live order and restores the dedicated button",!screenLayerOccluded(buttonWindows[1],foreground)&&!screenLayerOccluded(buttonOutlines[1],foreground)&&heldTouch&&unexpectedFlags==0);
 putAbove(foreground);observe(EVENT_OBJECT_REORDER,foreground);
 check("reorder after delivery cannot be dropped",posts==initialPosts+2&&ownerVisibilityQueued);

 const auto replacement=handle(21);foreground=replacement;node(replacement).pid=2;
 observe(EVENT_SYSTEM_FOREGROUND,replacement);
 check("a real foreground change still upgrades already queued work and arms verification",posts==initialPosts+2&&ownerVisibilityRaiseRequested&&ownerVisibilityRecheck.foreground==replacement&&ownerVisibilityRecheck.timer);
 const auto timerId=ownerVisibilityRecheck.timer;
 observe(EVENT_OBJECT_LOCATIONCHANGE,replacement);
 check("same-foreground location change retains the bounded deadline",ownerVisibilityRecheck.timer==timerId&&posts==initialPosts+2);
 observe(EVENT_OBJECT_REORDER,desktop(),OBJID_CLIENT);
 check("a following reorder does not erase the stronger raise",ownerVisibilityRaiseRequested);
 consumeVisibility();const auto repairs=ownerVisibilityDelayedChecks;artworkCalls=0;
 for(int i=0;i<3;++i){tick();consumeVisibility();}
 check("three checkpoints still run and redraw the visible button",ownerVisibilityDelayedChecks-repairs==3&&!timer.active&&artworkCalls==1);

 postFailure=true;observe(EVENT_OBJECT_REORDER,desktop(),OBJID_CLIENT);
 check("failed post never leaves a queued sentinel",!ownerVisibilityQueued);
 postFailure=false;observe(EVENT_OBJECT_REORDER,desktop(),OBJID_CLIENT);
 check("next reorder can retry a failed post",ownerVisibilityQueued);
 const auto stale=ownerVisibilityEpoch;cancelOwnerVisibilityWork();observe(EVENT_OBJECT_REORDER,desktop(),OBJID_CLIENT);
 check("a stale queued message cannot consume the replacement check",!consumeOwnerVisibility(stale)&&ownerVisibilityQueued);
 stopOwnerVisibility();
 const Json report={{"ok",ok},{"cases",cases},{"duplicateReorders",1000},{"ownerAdmissionReads",ownerReads},{"controlVisibleReads",visibleReads},{"systemInputInjected",false}};
 std::ofstream f(std::filesystem::path(reportPath),std::ios::binary);f<<report.dump(2);
 return ok&&f.good()?0:3;
}
static int checkPairGeometry(const wchar_t* reportPath){
 using namespace visibilityfixture;
 Json cases=Json::array();bool ok=true;
 const auto check=[&](const char* name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 const auto clear=[] {foregroundVisibleReads=foregroundIconicReads=foregroundRectReads=0;};
 const auto reads=[] {return std::array{foregroundVisibleReads,foregroundIconicReads,foregroundRectReads};};
 resetVisibilityFixture();clear();
 for(int i=0;i<1000;++i)restoreScreenLayerPair(windows[0],outlines[0],foreground,false,{ScreenPairInputKind::Pad,0});
 const auto stable=reads();
 check("one foreground geometry read per stable visible pair",stable==std::array{1000u,1000u,1000u}&&raises==0);

 resetVisibilityFixture();clear();
 const auto input=windows[0],artwork=outlines[0];
 stack={input,foreground,artwork,windows[1],outlines[1],buttonWindows[1],buttonOutlines[1]};
 restoreScreenLayerPair(input,artwork,foreground,false,{ScreenPairInputKind::Pad,0});
 check("current Z order still sees the outline alone occluded",raises==2&&stack[0]==artwork&&stack[1]==input&&reads()==std::array{1u,1u,1u});

 resetVisibilityFixture();clear();node(input).rect={200,200,250,250};
 restoreScreenLayerPair(input,artwork,foreground,true,{ScreenPairInputKind::Pad,0});
 check("forced raise still sees the outline alone overlapping",raises==2&&reads()==std::array{1u,1u,1u});

 resetVisibilityFixture();clear();node(foreground).iconic=true;
 restoreScreenLayerPair(input,artwork,foreground,true,{ScreenPairInputKind::Pad,0});
 check("minimized foreground is not eligible for either layer",raises==0&&reads()==std::array{1u,1u,0u});
 node(foreground).iconic=false;
 restoreScreenLayerPair(input,artwork,foreground,true,{ScreenPairInputKind::Pad,0});
 check("the next pass queries an unminimized foreground afresh",raises==2&&reads()==std::array{2u,2u,1u});

 resetVisibilityFixture();clear();
 placeReentry=[] {node(foreground).rect={400,400,500,500};};
 ownerVisibilityQueued=ownerVisibilityRaiseRequested=true;
 check("changes during a raise cannot stale the next pair geometry",consumeOwnerVisibility(ownerVisibilityEpoch)&&raises==2&&reads()==std::array{3u,3u,3u});

 resetVisibilityFixture();clear();node(input).visible=false;
 restoreScreenLayerPair(input,artwork,foreground,true,{ScreenPairInputKind::Pad,0});
 check("hidden input does not read foreground geometry or raise",raises==0&&reads()==std::array{0u,0u,0u});
 stopOwnerVisibility();
 const Json report={{"ok",ok},{"cases",cases},{"stablePairs",1000},{"foregroundGeometryReads",{{"visible",stable[0]},{"iconic",stable[1]},{"rect",stable[2]}}},{"systemInputInjected",false}};
 std::ofstream f(std::filesystem::path(reportPath),std::ios::binary);f<<report.dump(2);
 return ok&&f.good()?0:3;
}
static int checkBurstAdmission(const wchar_t* reportPath){
 using namespace visibilityfixture;
 Json cases=Json::array();bool ok=true;
 const auto check=[&](const char* name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 const auto queries=[] {return std::array{foregroundVisibleReads,foregroundIconicReads,foregroundPidReads};};
 const auto clearQueries=[] {foregroundVisibleReads=foregroundIconicReads=foregroundPidReads=0;};
 const auto observe=[](DWORD event){screenVisibilityObserved(nullptr,event,foreground,OBJID_WINDOW,CHILDID_SELF,0,0);};
 beginVisibility();clearQueries();const auto start=timer;const auto arms=armed,wasPosted=posts;
 // Repeat the Win5 layout: no visible pads, but one live dedicated button.
 node(windows[0]).visible=node(windows[1]).visible=false;
 node(outlines[0]).visible=node(outlines[1]).visible=false;
 ownerAdmissionReads=controlVisibleReads=0;
 // Keep delivery queued to count only the admission path, not the consumer.
 for(int i=0;i<1000;++i)observe(EVENT_OBJECT_LOCATIONCHANGE);
 const auto repeated=queries();const auto ownerReads=ownerAdmissionReads,controlReads=controlVisibleReads;
 check("stable pending locations admit only the first event",ownerReads==1&&controlReads==3);
 check("unchanged foreground burst does not re-read the three eligibility properties",repeated==std::array{0u,0u,0u});
 check("burst still posts one immediate check and keeps the existing deadline",ownerVisibilityQueued&&posts==wasPosted+1&&armed==arms&&timer.id==start.id&&timer.delay==start.delay);
 node(windows[0]).visible=node(windows[1]).visible=true;
 node(outlines[0]).visible=node(outlines[1]).visible=true;
 const auto other=handle(21);foreground=other;node(other).pid=2;node(other).iconic=true;clearQueries();observe(EVENT_SYSTEM_FOREGROUND);
 check("a minimized replacement still fails fresh admission",queries()==std::array{1u,1u,0u}&&timer.id==start.id&&armed==arms);
 node(other).iconic=false;node(other).pid=1;clearQueries();observe(EVENT_SYSTEM_FOREGROUND);
 check("an own-process replacement still fails fresh admission",queries()==std::array{1u,1u,1u}&&timer.id==start.id&&armed==arms);
 node(other).pid=2;clearQueries();observe(EVENT_SYSTEM_FOREGROUND);const auto next=timer;
 check("a different eligible foreground is probed and re-arms",queries()==std::array{1u,1u,1u}&&next.id!=start.id&&armed==arms+1&&ownerVisibilityRecheck.foreground==other);
 start.proc(start.window,WM_TIMER,start.id,0);
 check("the retired foreground timer cannot affect the replacement",timer.id==next.id&&timer.active);
 consumeVisibility();putAbove(other);
 for(int i=0;i<3;++i){tick();consumeVisibility();}
 check("replacement still completes the bounded repair and stops",!timer.active&&!ownerVisibilityRecheck.timer&&
   !screenLayerOccluded(outlines[0],other)&&!screenLayerOccluded(buttonOutlines[1],other));
 clearQueries();observe(EVENT_OBJECT_LOCATIONCHANGE);
 check("same foreground after expiry receives fresh eligibility checks",queries()==std::array{1u,1u,1u}&&timer.active);
 const auto currentTimer=timer.id;
 observe(EVENT_OBJECT_SHOW);
 check("same-root show keeps its explicit raise even with location work queued",ownerVisibilityRaiseRequested&&timer.id==currentTimer);
 ownerAdmissionReads=controlVisibleReads=0;
 const auto beforeTransitions=ownerVisibilityEvents;
 const auto transitionPosts=posts;
 for(int i=0;i<1000;++i)observe(i%2?EVENT_OBJECT_SHOW:EVENT_SYSTEM_MINIMIZEEND);
 const auto transitionOwnerReads=ownerAdmissionReads,transitionVisibleReads=controlVisibleReads;
 check("queued same-root transitions do not repeat owner or control visibility admission",
   transitionOwnerReads==0&&transitionVisibleReads==0);
 check("coalesced transitions retain event accounting and the original deadline",
   ownerVisibilityEvents-beforeTransitions==1000&&posts==transitionPosts&&timer.id==currentTimer&&ownerVisibilityRaiseRequested);
 const HWND changedTransition=handle(23);node(changedTransition).pid=3;foregroundRoot=changedTransition;
 observe(EVENT_SYSTEM_FOREGROUND);
 const auto nextDeadline=timer.id;
 check("a queued transition does not swallow a different foreground root",
   nextDeadline!=currentTimer&&ownerVisibilityRecheck.foreground==changedTransition);
 const HWND changedRoot=handle(22);node(changedRoot).pid=3;foregroundRoot=changedRoot;
 observe(EVENT_OBJECT_LOCATIONCHANGE);
 check("queued location for changed root admits a new burst",ownerVisibilityQueued&&timer.id!=nextDeadline&&ownerVisibilityRecheck.foreground==changedRoot);
 beginVisibility();stopOwnerVisibilityRechecks();observe(EVENT_OBJECT_REORDER);observe(EVENT_OBJECT_LOCATIONCHANGE);
 check("pending check without a timer can still start a burst",ownerVisibilityQueued&&timer.active&&ownerVisibilityRecheck.foreground==foreground);
 stopOwnerVisibility();
 const Json report={{"ok",ok},{"cases",cases},{"repeatedEvents",1000},{"sameBurstReads",{{"visible",repeated[0]},{"iconic",repeated[1]},{"pid",repeated[2]},{"ownerAdmission",ownerReads},{"controlVisible",controlReads}}},
   {"sameTransitionBurstReads",{{"ownerAdmission",transitionOwnerReads},{"controlVisible",transitionVisibleReads}}},{"systemInputInjected",false}};
 std::ofstream f(std::filesystem::path(reportPath),std::ios::binary);f<<report.dump(2);
 return ok&&f.good()?0:3;
}
#endif
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
static int checkSurfaceLookup(const wchar_t* reportPath){
 using namespace visibilityfixture;
 Json cases=Json::array();bool ok=true;
 const auto check=[&](const char* name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 resetVisibilityFixture();
 for(int i=0;i<7;++i){buttonWindows[i]=handle(6+2*i);buttonOutlines[i]=handle(7+2*i);}
 for(int i=0;i<4;++i){auto item=std::make_shared<ymcc::customtouch::Item>();item->window=handle(22+2*i);item->outline=handle(23+2*i);ymcc::customtouch::items.push_back(item);}
 const HWND left=windows[0],button=buttonWindows[6];
 const auto item=ymcc::customtouch::items[0],other=ymcc::customtouch::items[1];
 slotReads=0;
 check("pad verifies only its live slot",screenPairMouseSafe(left,outlines[0],{ScreenPairInputKind::Pad,0})&&slotReads==1&&nativeInputSurfaces[0].calls==1);
 slotReads=0;
 check("button verifies only its live slot",screenPairMouseSafe(button,buttonOutlines[6],{ScreenPairInputKind::Button,6})&&slotReads==1&&nativeButtonSurfaces[6].calls==1);
 slotReads=0;
 check("custom slot skips builtin scan",screenPairMouseSafe(item->window,item->outline,{ScreenPairInputKind::Custom,0,item.get()})&&slotReads==0&&item->surface.calls==1);
 slotReads=0;
 check("unknown custom slot uses the fallback",screenPairMouseSafe(item->window,item->outline)&&slotReads==9&&item->surface.calls==2);
 const auto before=surfaceChecks;artworkCalls=0;
 ownerVisibilityQueued=ownerVisibilityRaiseRequested=ownerVisibilityArtworkRequested=true;
 const bool consumed=consumeOwnerVisibility(ownerVisibilityEpoch);
 check("both real consumers validate each of thirteen live sources once",consumed&&surfaceChecks-before==26&&artworkCalls==9&&ymcc::customtouch::draws==4);
 check("stack actions retain no-activate and artwork stays available",raises==26&&unexpectedFlags==0&&releaseCalls==0&&heldTouch);

 auto calls=item->surface.calls;const auto otherCalls=other->surface.calls;
 displayReentry=[] {std::swap(ymcc::customtouch::items[0],ymcc::customtouch::items[1]);};reenterDisplay=true;
 check("display reentry re-resolves a moved custom slot",screenPairMouseSafe(item->window,item->outline,{ScreenPairInputKind::Custom,0,item.get()})&&item->surface.calls==calls+1&&other->surface.calls==otherCalls);
 calls=item->surface.calls;const auto priorRaises=raises;
 displayReentry=[] {ymcc::customtouch::items.erase(ymcc::customtouch::items.begin()+1);};reenterDisplay=true;
 restoreScreenLayerPair(item->window,item->outline,foreground,true,{ScreenPairInputKind::Custom,1,item.get()});
 check("retired source never verifies or raises",item->surface.calls==calls&&raises==priorRaises&&!visible(item->window)&&!visible(item->outline)&&releaseCalls==1);

 resetVisibilityFixture();nativeInputSurfaces[0].safe=false;
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("failed native policy still hides both layers and releases input",releaseCalls==1&&raises==0&&!visible(windows[0])&&!visible(outlines[0]));
 check("active pad failure emits a typed recovery receipt",failureReceipts==1&&!failedButton&&!failedCustom&&failedIndex==0&&windowError==(DWORD)E_FAIL);

 resetVisibilityFixture();node(outlines[0]).safe=false;
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("artwork-only failure does not request input recovery",failureReceipts==0&&releaseCalls==1&&windowError==0);

 resetVisibilityFixture();nativeInputSurfaces[0].safe=false;
 surfaceReentry=[] {++padRefreshEpoch;};
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("rebound pad does not inherit old input failure or hide",failureReceipts==0&&releaseCalls==0&&windowError==0&&visible(windows[0])&&visible(outlines[0]));

 resetVisibilityFixture();node(outlines[0]).safe=false;
 displayReentry=[] {cancelOwnerVisibilityWork();};reenterDisplay=true;
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("invalidated style callback does not verify or release the current source",surfaceChecks==0&&releaseCalls==0&&heldTouch&&visible(windows[0])&&visible(outlines[0])&&raises==0);

 resetVisibilityFixture();nativeInputSurfaces[0].safe=false;
 surfaceReentry=[] {cancelOwnerVisibilityWork();};
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("invalidated input policy failure does not hide or release current touch",surfaceChecks==1&&releaseCalls==0&&heldTouch&&visible(windows[0])&&visible(outlines[0])&&raises==0);

 resetVisibilityFixture();nativeInputSurfaces[0].safe=false;
 showReentry=[] {cancelOwnerVisibilityWork();node(windows[0]).visible=true;};
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("retiring during the first hide stops subsequent hide and release",changes==1&&releaseCalls==0&&heldTouch&&visible(windows[0])&&visible(outlines[0]));

 resetVisibilityFixture();node(outlines[0]).safe=false;
 displayReentry=[] {++padRefreshEpoch;};reenterDisplay=true;
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("display repair cannot validate or hide a rebound pad under a reused HWND",surfaceChecks==0&&changes==0&&releaseCalls==0&&heldTouch&&visible(windows[0])&&visible(outlines[0]));

 resetVisibilityFixture();nativeInputSurfaces[0].safe=false;
 failureReentry=[] {++padRefreshEpoch;windowError=0;};
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("already-reported failure cannot poison or hide the replacement",failureReceipts==1&&changes==0&&releaseCalls==0&&heldTouch&&windowError==0&&visible(windows[0])&&visible(outlines[0]));

 resetVisibilityFixture();nativeButtonSurfaces[1].safe=false;
 showReentry=[] {++buttonRefreshEpoch;node(buttonWindows[1]).visible=true;};
 restoreScreenLayerPair(buttonWindows[1],buttonOutlines[1],foreground,true,{ScreenPairInputKind::Button,1});
 check("first hide cannot hide the replacement button artwork or release its touch",changes==1&&releaseCalls==0&&heldTouch&&visible(buttonWindows[1])&&visible(buttonOutlines[1]));

 resetVisibilityFixture();const auto key=std::make_shared<ymcc::customtouch::Item>();
 key->window=handle(10);key->outline=handle(11);key->surface.safe=false;ymcc::customtouch::items.push_back(key);
 showReentry=[] {showReentry=[] {++ymcc::customtouch::refreshEpoch;};};
 restoreScreenLayerPair(key->window,key->outline,foreground,true,{ScreenPairInputKind::Custom,0,key.get()});
 check("second hide cannot release gestures from a rebound custom control",changes==2&&releaseCalls==0&&heldTouch);

 resetVisibilityFixture();nativeInputSurfaces[0].safe=false;
 showReentry=[] {++ymcc::customtouch::refreshEpoch;};
 restoreScreenLayerPair(windows[0],outlines[0],foreground,true,{ScreenPairInputKind::Pad,0});
 check("unrelated refresh does not strand a failed pad",changes==2&&releaseCalls==1&&!heldTouch&&!visible(windows[0])&&!visible(outlines[0]));

 resetVisibilityFixture();placeReentry=[] {++buttonRefreshEpoch;};
 restoreScreenLayerPair(buttonWindows[1],buttonOutlines[1],foreground,true,{ScreenPairInputKind::Button,1});
 check("the old restack cannot raise replacement artwork after the first raise rebinds it",raises==1&&releaseCalls==0&&heldTouch);

 beginVisibility();displayTransitionFence.pending=true;const auto checksBefore=surfaceChecks,postsBefore=posts,raisesBefore=raises;
 check("pending display reflow does not post fresh visibility work",!queueOwnerVisibility(true,true)&&posts==postsBefore);
 ownerVisibilityQueued=ownerVisibilityRaiseRequested=ownerVisibilityArtworkRequested=true;
 check("pending display reflow refuses a delivered pass",!consumeOwnerVisibility(ownerVisibilityEpoch)&&surfaceChecks==checksBefore&&raises==raisesBefore&&!timer.active);

 resetVisibilityFixture();windowError=cursorError=0;contactVisuals[0]={handle(8),true};node(handle(8)).safe=false;
 displayReentry=[] {cancelOwnerVisibilityWork();};reenterDisplay=true;restoreScreenFeedbackOrder();
 check("old contact visual failure cannot poison or hide the new visual",windowError==0&&contactVisuals[0].visible&&visible(handle(8))&&raises==0);

 resetVisibilityFixture();cursorError=0;cursorWindow=handle(8);cursorVisible=true;node(handle(8)).safe=false;
 displayReentry=[] {cancelOwnerVisibilityWork();};reenterDisplay=true;restoreScreenFeedbackOrder();
 check("old cursor visual failure cannot poison or hide the new visual",cursorError==0&&cursorVisible&&visible(handle(8))&&raises==0);
 stopOwnerVisibility();
 const Json report={{"ok",ok},{"cases",cases},{"systemInputInjected",false},{"realWindowsCreated",0}};
 std::ofstream f(std::filesystem::path(reportPath),std::ios::binary);f<<report.dump(2);
 return ok&&f.good()?0:3;
}
#endif
int wmain(int argc,wchar_t** argv){
#ifndef YMCC_SCREEN_VISIBILITY_OLD_CONTROL
 if(argc==3&&std::wstring(argv[2])==L"--display-reorder-admission")return checkReorderAdmission(argv[1]);
 if(argc==3&&std::wstring(argv[2])==L"--display-pair-geometry")return checkPairGeometry(argv[1]);
 if(argc==3&&std::wstring(argv[2])==L"--display-burst-admission")return checkBurstAdmission(argv[1]);
#endif
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
 if(argc==3&&std::wstring(argv[2])==L"--display-surface-lookup")return checkSurfaceLookup(argv[1]);
#endif
 if(argc!=3||(std::wstring(argv[2])!=L"--display-transitions"&&std::wstring(argv[2])!=L"--display-suspend-resize"&&std::wstring(argv[2])!=L"--display-artwork"&&std::wstring(argv[2])!=L"--display-storm"))return 2;
 const bool stormOnly=std::wstring(argv[2])==L"--display-storm";
 const bool artworkOnly=std::wstring(argv[2])==L"--display-artwork";
 const bool suffix=std::wstring(argv[2])==L"--display-suspend-resize";
 Json cases=Json::array(),queryCounts=Json::object();bool ok=true;auto check=[&](const std::string& name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 if(stormOnly){
  beginVisibility();artworkCalls=0;pictureLost=true;syncOwnerVisibility();consumeVisibility();
  check("storm-reshow-with-existing-hooks-submits-without-game-event",artworkCalls==3&&!pictureLost);
#ifdef YMCC_STORM_OLD_CONTROL
  if(!ok){stopOwnerVisibility();const Json r={{"ok",false},{"cases",cases},{"selector","--display-storm"}};std::ofstream f(std::filesystem::path(argv[1]),std::ios::binary);f<<r.dump(2);return 3;}
#endif
   unsigned locationAncestorReads=0;
   for(int round=0;round<64&&ok;++round){
   const auto label="storm-"+std::to_string(round);const auto old=visibilityfixture::timer;
   const auto next=visibilityfixture::handle(20+round%2);visibilityfixture::node(next).pid=2+round%2;visibilityfixture::node(next).rect={0,0,500+round%2*100,400};visibilityfixture::foreground=next;
   screenVisibilityObserved(nullptr,EVENT_SYSTEM_FOREGROUND,next,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
   const auto timerId=ownerVisibilityRecheck.timer;const auto arms=visibilityfixture::armed;const auto posts=visibilityfixture::posts;
    const auto ancestorBefore=visibilityfixture::ancestorReads;
    for(int i=0;i<1000;++i)screenVisibilityObserved(nullptr,EVENT_OBJECT_LOCATIONCHANGE,next,OBJID_WINDOW,CHILDID_SELF,0,0);
    locationAncestorReads+=visibilityfixture::ancestorReads-ancestorBefore;
   check(label+"-1000-events-one-post-no-rearm",ownerVisibilityRecheck.timer==timerId&&visibilityfixture::armed==arms&&visibilityfixture::posts==posts+1);consumeVisibility();
   if(old.id!=timerId){old.proc(old.window,WM_TIMER,old.id,0);check(label+"-old-game-timer-has-no-authority",ownerVisibilityRecheck.timer==timerId);}
   visibilityfixture::putAbove(next);for(int i=0;i<3;++i){visibilityfixture::tick();consumeVisibility();}
   check(label+"-three-checks-repair-stack-and-end",!ownerVisibilityRecheck.timer&&!visibilityfixture::timer.active&&!screenLayerOccluded(outlines[0],next));
   const auto count=artworkCalls;for(int i=0;i<100;++i){visibilityfixture::tick();consumeVisibility();}check(label+"-no-idle-work",artworkCalls==count);
   if(round%8==7){syncOwnerVisibility();consumeVisibility();check(label+"-reopen-submits-before-another-game-event",artworkCalls==count+3);}
  }
  const auto old=visibilityfixture::timer;const auto count=artworkCalls;inputTeardown=true;stopOwnerVisibility();old.proc(old.window,WM_TIMER,old.id,0);consumeVisibility();
  check("storm-late-timer-after-exit-cannot-submit",artworkCalls==count&&!ownerVisibilityQueued&&!ownerVisibilityRecheck.timer);
   queryCounts["ancestorDuring64000LocationNotifications"]=locationAncestorReads;
   check("exact-foreground-burst-keeps-only-fresh-recheck-root-query",locationAncestorReads==64000);
   beginVisibility();stopOwnerVisibilityRechecks();
   const auto root=visibilityfixture::handle(21);visibilityfixture::foregroundRoot=root;
   const auto posts=visibilityfixture::posts,rootReads=visibilityfixture::ancestorReads;
   screenVisibilityObserved(nullptr,EVENT_OBJECT_REORDER,visibilityfixture::handle(22),OBJID_WINDOW,CHILDID_SELF,0,0);
   check("nonforeground-window-still-rejected-after-root-lookup",visibilityfixture::posts==posts&&visibilityfixture::ancestorReads==rootReads+1);
   screenVisibilityObserved(nullptr,EVENT_OBJECT_REORDER,root,OBJID_WINDOW,CHILDID_SELF,0,0);
   check("foreground-root-event-still-posts-live-stack-check",visibilityfixture::posts==posts+1&&visibilityfixture::ancestorReads==rootReads+2);consumeVisibility();
 }
 if(!suffix&&!artworkOnly&&!stormOnly){
 beginVisibility();const auto before=ownerVisibilityRaises;
#ifndef YMCC_SCREEN_VISIBILITY_OLD_CONTROL
 const auto work=[] {return ownerVisibilityState().value("restackWork",Json::object());};
 const auto count=[](const Json& row,const char* key){return row.value(key,0ull);};
 const auto beforeForce=work();
#endif
 screenVisibilityObserved(nullptr,EVENT_SYSTEM_FOREGROUND,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
 check("foreground-transition-requests-raise-without-topmost-occlusion",ownerVisibilityRaises>before);
#ifndef YMCC_SCREEN_VISIBILITY_OLD_CONTROL
 const auto afterForce=work();
 check("forced transition export identifies the same admitted root and successful layer raises",
     count(afterForce,"forcedPasses")==count(beforeForce,"forcedPasses")+1&&
     count(afterForce,"forcedSameRootAsPreviousPass")==count(beforeForce,"forcedSameRootAsPreviousPass")+1&&
     count(afterForce,"windowTransitionForcedPasses")==count(beforeForce,"windowTransitionForcedPasses")+1&&
     count(afterForce,"forcedPairLayerRaises")==count(beforeForce,"forcedPairLayerRaises")+ownerVisibilityRaises-before);
 check("one-bounded-timer-after-transition",ownerVisibilityRecheck.timer!=0&&visibilityfixture::timer.active&&visibilityfixture::timer.delay==200);
 const auto firstTimer=ownerVisibilityRecheck.timer;const auto firstArms=visibilityfixture::armed;
 for(int i=0;i<1000;++i)screenVisibilityObserved(nullptr,EVENT_OBJECT_SHOW,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);
 check("event-burst-keeps-original-three-check-budget",ownerVisibilityRecheck.timer==firstTimer&&visibilityfixture::armed==firstArms);
 consumeVisibility();const auto beforeLate=ownerVisibilityRaises;
 const auto beforeOcclusion=work();
 visibilityfixture::putAbove(visibilityfixture::foreground); // No later event is delivered.
 check("late-normal-game-can-cover-degraded-stack",screenLayerOccluded(windows[0],visibilityfixture::foreground));
 visibilityfixture::tick();consumeVisibility();
 check("first-delayed-check-repairs-late-promotion",ownerVisibilityRaises>beforeLate&&!screenLayerOccluded(windows[0],visibilityfixture::foreground));
 const auto afterOcclusion=work();
 check("delayed repair export is separate from forced work",
     count(afterOcclusion,"occlusionPairLayerRaises")==count(beforeOcclusion,"occlusionPairLayerRaises")+ownerVisibilityRaises-beforeLate&&
     count(afterOcclusion,"forcedPairLayerRaises")==count(beforeOcclusion,"forcedPairLayerRaises"));
 const auto afterRepair=ownerVisibilityRaises;
 visibilityfixture::tick();consumeVisibility();visibilityfixture::tick();consumeVisibility();
 check("healthy-followups-do-not-repeat-raise",ownerVisibilityRaises==afterRepair);
 check("three-checks-complete-with-no-idle-timer",!ownerVisibilityRecheck.timer&&!visibilityfixture::timer.active);
 const auto idleChecks=ownerVisibilityChecks;for(int i=0;i<100;++i)visibilityfixture::tick();consumeVisibility();check("finished-burst-does-no-idle-work",ownerVisibilityChecks==idleChecks);
 check("touch-ownership-and-mouse-pass-layer-unchanged",heldTouch&&releaseCalls==0&&visibilityfixture::changes==0&&visibilityfixture::unexpectedFlags==0);
 beginVisibility();const auto beforeMerged=work();
 syncOwnerVisibility();screenVisibilityObserved(nullptr,EVENT_OBJECT_SHOW,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
 const auto afterMerged=work();
 check("merged owner refresh and window transition are one forced pass with both reasons",
     count(afterMerged,"forcedPasses")==count(beforeMerged,"forcedPasses")+1&&
     count(afterMerged,"ownerRefreshForcedPasses")==count(beforeMerged,"ownerRefreshForcedPasses")+1&&
     count(afterMerged,"windowTransitionForcedPasses")==count(beforeMerged,"windowTransitionForcedPasses")+1);
 beginVisibility();const auto beforeNewPid=work();
 ++visibilityfixture::node(visibilityfixture::foreground).pid;
 screenVisibilityObserved(nullptr,EVENT_SYSTEM_FOREGROUND,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
 const auto afterNewPid=work();
 check("reused root handle with changed PID is not same-root forced work",
     count(afterNewPid,"forcedPasses")==count(beforeNewPid,"forcedPasses")+1&&
     count(afterNewPid,"forcedSameRootAsPreviousPass")==count(beforeNewPid,"forcedSameRootAsPreviousPass"));
 beginVisibility();const auto beforeRestart=work();cancelOwnerVisibilityWork();
 syncOwnerVisibility();consumeVisibility();
 const auto afterRestart=work();
 check("a new visibility epoch is not same-root forced work",
     count(afterRestart,"forcedPasses")==count(beforeRestart,"forcedPasses")+1&&
     count(afterRestart,"forcedSameRootAsPreviousPass")==count(beforeRestart,"forcedSameRootAsPreviousPass"));
 beginVisibility();const auto beforeRejected=work();visibilityfixture::postFailure=true;
 screenVisibilityObserved(nullptr,EVENT_OBJECT_SHOW,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);
 visibilityfixture::postFailure=false;syncOwnerVisibility();consumeVisibility();
 const auto afterRejected=work();
 check("failed post cannot attribute a later refresh to the old transition",
     count(afterRejected,"forcedPasses")==count(beforeRejected,"forcedPasses")+1&&
     count(afterRejected,"ownerRefreshForcedPasses")==count(beforeRejected,"ownerRefreshForcedPasses")+1&&
     count(afterRejected,"windowTransitionForcedPasses")==count(beforeRejected,"windowTransitionForcedPasses"));
 beginVisibility();contactVisuals[0]={visibilityfixture::handle(8),true};const auto beforeFeedback=work();
 syncOwnerVisibility();consumeVisibility();const auto afterFeedback=work();
 check("feedback restack is separate and all exported layer raises reconcile",
     count(afterFeedback,"feedbackLayerRaises")==count(beforeFeedback,"feedbackLayerRaises")+1&&
     count(afterFeedback,"feedbackLayerRaises")+count(afterFeedback,"forcedPairLayerRaises")+
         count(afterFeedback,"occlusionPairLayerRaises")==ownerVisibilityRaises);
 beginVisibility();const auto timer=visibilityfixture::timer;const auto oldEpoch=ownerVisibilityEpoch;stopOwnerVisibility();timer.proc(timer.window,WM_TIMER,timer.id,0);
 check("stop-removes-hooks-and-timer-and-rejects-late-callback",!ownerVisibilityActive()&&!ownerVisibilityRecheck.timer&&!ownerVisibilityQueued&&!consumeOwnerVisibility(oldEpoch));
 beginVisibility();const auto old=visibilityfixture::timer;stopOwnerVisibility();syncOwnerVisibility();const auto freshId=ownerVisibilityRecheck.timer;old.proc(old.window,WM_TIMER,old.id,0);
 check("restart-keeps-new-timer-despite-old-delivery",freshId&&ownerVisibilityRecheck.timer==freshId);
 beginVisibility();visibilityfixture::inputAllowed=false;const auto raisesBefore=ownerVisibilityRaises;visibilityfixture::tick();consumeVisibility();
 check("power-closed-stops-rechecks-without-raising",!ownerVisibilityRecheck.timer&&ownerVisibilityRaises==raisesBefore);
 beginVisibility();inputTeardown=true;const auto terminalBefore=ownerVisibilityRaises;visibilityfixture::tick();consumeVisibility();
 check("teardown-rejects-all-delayed-work",!ownerVisibilityRecheck.timer&&ownerVisibilityRaises==terminalBefore);
 beginVisibility();visibilityfixture::foreground=visibilityfixture::handle(21);visibilityfixture::node(visibilityfixture::foreground).pid=3;visibilityfixture::tick();
 check("foreground-change-retires-old-transition",!ownerVisibilityRecheck.timer);
 beginVisibility();const auto forceBefore=ownerVisibilityRaises;visibilityfixture::node(visibilityfixture::foreground).rect={500,500,800,800};
 screenVisibilityObserved(nullptr,EVENT_OBJECT_SHOW,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
 check("nonoverlapping-window-does-not-raise-controls",ownerVisibilityRaises==forceBefore);
 beginVisibility();visibilityfixture::node(outlines[0]).visible=false;visibilityfixture::putAbove(visibilityfixture::foreground);screenVisibilityObserved(nullptr,EVENT_OBJECT_REORDER,visibilityfixture::desktop(),OBJID_CLIENT,CHILDID_SELF,0,0);consumeVisibility();
 check("auto-hidden-outline-is-not-reshown",!visibilityfixture::node(outlines[0]).visible&&visibilityfixture::changes==0);
 check("desktop-client-reorder-still-restores-input",!screenLayerOccluded(windows[0],visibilityfixture::foreground));
 beginVisibility();const auto queued=visibilityfixture::posts;for(int i=0;i<1000;++i)screenVisibilityObserved(nullptr,EVENT_OBJECT_LOCATIONCHANGE,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);
 check("location-event-storm-coalesces-without-timer-rearm",visibilityfixture::posts==queued+1&&ownerVisibilityQueued);consumeVisibility();
 beginVisibility();visibilityfixture::displayReentry=[] {inputTeardown=true;stopOwnerVisibility();};visibilityfixture::reenterDisplay=true;
 const auto reentryRaises=ownerVisibilityRaises;screenVisibilityObserved(nullptr,EVENT_OBJECT_SHOW,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
 check("display-repair-reentry-cannot-raise-after-retirement",ownerVisibilityRaises==reentryRaises&&!ownerVisibilityRecheck.timer);
 beginVisibility();visibilityfixture::node(outlines[0]).safe=false;screenVisibilityObserved(nullptr,EVENT_OBJECT_SHOW,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
 check("unsafe-mouse-pass-pair-stays-hidden-and-releases",!visibilityfixture::node(windows[0]).visible&&!visibilityfixture::node(outlines[0]).visible&&releaseCalls!=0);
 beginVisibility();contactVisuals[0]={visibilityfixture::handle(8),true};cursorWindow=visibilityfixture::handle(9);cursorVisible=true;
 screenVisibilityObserved(nullptr,EVENT_OBJECT_SHOW,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
 check("contact-and-direct-mouse-cursor-finish-above-outlines",visibilityfixture::stack[0]==cursorWindow&&visibilityfixture::stack[1]==contactVisuals[0].window);
 beginVisibility();for(auto h:windows)visibilityfixture::node(h).visible=false;for(auto h:buttonWindows)if(h)visibilityfixture::node(h).visible=false;syncOwnerVisibility();
 check("all-disabled-removes-hooks-and-bounded-work",!ownerVisibilityActive()&&!ownerVisibilityRecheck.timer&&!visibilityfixture::timer.active);
 resetVisibilityFixture();visibilityfixture::timerFailure=true;syncOwnerVisibility();consumeVisibility();check("timer-create-failure-keeps-immediate-display-path",!ownerVisibilityRecheck.timer&&ownerVisibilityActive());
 resetVisibilityFixture();visibilityfixture::postFailure=true;syncOwnerVisibility();check("post-failure-does-not-arm-delayed-work",!ownerVisibilityQueued&&!ownerVisibilityRecheck.timer);
#endif
 }
#ifndef YMCC_SCREEN_VISIBILITY_OLD_CONTROL
 if(suffix){
  beginVisibility();const auto pending=visibilityfixture::timer;const auto epoch=ownerVisibilityEpoch;cancelOwnerVisibilityWork();pending.proc(pending.window,WM_TIMER,pending.id,0);
  check("suspend-cancels-old-work-but-keeps-display-subscription",ownerVisibilityActive()&&!ownerVisibilityRecheck.timer&&!ownerVisibilityQueued&&!consumeOwnerVisibility(epoch));
  screenVisibilityObserved(nullptr,EVENT_SYSTEM_FOREGROUND,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
  check("legacy-ready-display-event-can-rearm-without-native-rebind",ownerVisibilityActive()&&ownerVisibilityRecheck.timer);
  for(int i=0;i<3;++i){visibilityfixture::tick();consumeVisibility();}
  const auto steady=ownerVisibilityRaises;screenVisibilityObserved(nullptr,EVENT_OBJECT_LOCATIONCHANGE,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
  check("same-window-fullscreen-resize-starts-bounded-checks",ownerVisibilityRecheck.timer&&ownerVisibilityRaises==steady);
  const auto originalTimer=ownerVisibilityRecheck.timer;for(int i=0;i<1000;++i)screenVisibilityObserved(nullptr,EVENT_OBJECT_LOCATIONCHANGE,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
  check("resize-storm-does-not-rearm-or-force-promote-healthy-stack",ownerVisibilityRecheck.timer==originalTimer&&ownerVisibilityRaises==steady);
  visibilityfixture::putAbove(visibilityfixture::foreground);visibilityfixture::tick();consumeVisibility();
  check("same-window-late-fullscreen-promotion-is-repaired",!screenLayerOccluded(windows[0],visibilityfixture::foreground)&&ownerVisibilityRaises>steady);
  for(int i=0;i<3;++i){visibilityfixture::tick();consumeVisibility();}check("resize-burst-stops-after-three-checks",!ownerVisibilityRecheck.timer&&!visibilityfixture::timer.active);
  const auto state=ownerVisibilityState();check("export-only-diagnostics-read-current-stack",state["version"]==1&&state["foregroundPid"]==2&&state["layers"].size()==3&&!state["recheckTimerArmed"].get<bool>());
  beginVisibility();contactVisuals[0]={visibilityfixture::handle(8),true};visibilityfixture::displayReentry=[] {inputTeardown=true;cancelOwnerVisibilityWork();};visibilityfixture::reenterDisplay=true;const auto beforeFeedback=ownerVisibilityRaises;restoreScreenFeedbackOrder();
  check("feedback-display-reentry-cannot-raise-after-retirement",ownerVisibilityRaises==beforeFeedback);
 }
#endif
if(artworkOnly){
  beginVisibility();artworkCalls=0;pictureLost=true;const auto stable=ownerVisibilityRaises;
  visibilityfixture::tick();consumeVisibility();visibilityfixture::tick();consumeVisibility();
  check("first-two-checks-do-not-submit-artwork",artworkCalls==0);
  visibilityfixture::tick();consumeVisibility();check("final-check-resubmits-visible-cached-artwork",artworkCalls==3&&!pictureLost&&ownerVisibilityRaises==stable);
#ifdef YMCC_SCREEN_VISIBILITY_OLD_CONTROL
  if(!ok){stopOwnerVisibility();const Json r={{"ok",false},{"cases",cases},{"selector","--display-artwork"}};std::ofstream f(std::filesystem::path(argv[1]),std::ios::binary);f<<r.dump(2);return 3;}
#endif
  for(int i=0;i<1000;++i){visibilityfixture::tick();consumeVisibility();}check("no-artwork-work-after-burst",artworkCalls==3&&!ownerVisibilityRecheck.timer);
  beginVisibility();artworkCalls=0;autoHide.hidden=true;visibilityfixture::node(outlines[0]).visible=false;for(int i=0;i<3;++i){visibilityfixture::tick();consumeVisibility();}
  check("auto-hidden-artwork-not-submitted-or-reshown",artworkCalls==2&&!visibilityfixture::node(outlines[0]).visible&&visibilityfixture::changes==0);
  beginVisibility();artworkCalls=0;visibilityfixture::displayReentry=[] {cancelOwnerVisibilityWork();};visibilityfixture::reenterDisplay=true;const auto beforeEpochChange=ownerVisibilityRaises;
  screenVisibilityObserved(nullptr,EVENT_OBJECT_SHOW,visibilityfixture::foreground,OBJID_WINDOW,CHILDID_SELF,0,0);consumeVisibility();
  check("epoch-change-without-teardown-stops-entire-pair-batch",ownerVisibilityRaises==beforeEpochChange&&artworkCalls==0);
  beginVisibility();artworkCalls=0;queueOwnerVisibility(false,true);visibilityfixture::inputAllowed=false;consumeVisibility();check("power-revocation-refuses-pending-artwork",artworkCalls==0);
  beginVisibility();artworkCalls=0;queueOwnerVisibility(false,true);const auto stale=ownerVisibilityEpoch;cancelOwnerVisibilityWork();consumeOwnerVisibility(stale);check("old-queued-artwork-cannot-cross-generation",artworkCalls==0);
  beginVisibility();artworkCalls=0;queueOwnerVisibility(false,true);inputTeardown=true;consumeVisibility();check("terminal-owner-refuses-artwork",artworkCalls==0);
  beginVisibility();artworkCalls=0;visibilityfixture::node(outlines[0]).rect={200,200,300,300};for(int i=0;i<3;++i){visibilityfixture::tick();consumeVisibility();}check("outside-game-artwork-not-resubmitted",artworkCalls==2);
  beginVisibility();artworkCalls=0;for(int i=0;i<3;++i){visibilityfixture::tick();consumeVisibility();}const auto exported=ownerVisibilityState();check("artwork-counter-is-export-only-observation",exported["artworkSubmissions"].is_number_unsigned()&&heldTouch&&releaseCalls==0);
#ifndef YMCC_SCREEN_VISIBILITY_OLD_CONTROL
   beginVisibility();artworkCalls=0;
   auto custom=std::make_shared<ymcc::customtouch::Item>();custom->window=visibilityfixture::handle(8);custom->outline=visibilityfixture::handle(9);ymcc::customtouch::items.push_back(custom);
   visibilityfixture::windowVisibleReads={};
   restoreOwnerArtwork(ownerVisibilityEpoch,visibilityfixture::foreground);
   const auto& reads=visibilityfixture::windowVisibleReads;
   queryCounts["artworkWindowVisibilityReads"]={reads[3],reads[5],reads[7],reads[9]};
   check("visible-pad-button-and-custom-artwork-each-checked-once",reads[3]==1&&reads[5]==1&&reads[7]==1&&reads[9]==1);
   check("single-visibility-read-preserves-all-artwork-submissions",artworkCalls==3&&ymcc::customtouch::draws==1&&releaseCalls==0);
   custom->wanted=false;visibilityfixture::node(custom->outline).visible=false;visibilityfixture::windowVisibleReads={};
   restoreOwnerArtwork(ownerVisibilityEpoch,visibilityfixture::foreground);
   check("hidden-custom-artwork-stays-hidden-and-is-not-submitted",reads[9]==1&&ymcc::customtouch::draws==1&&!visibilityfixture::node(custom->outline).visible);
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    const auto addKey=[](int input,int visual){
     const auto key=std::make_shared<ymcc::customtouch::Item>();
     key->window=visibilityfixture::handle(input);key->outline=visibilityfixture::handle(visual);
     ymcc::customtouch::items.push_back(key);return key;
    };
    beginVisibility();artworkCalls=0;const auto afterBadPad=addKey(8,9);nativeInputSurfaces[0].safe=false;
    const auto submitted=ownerVisibilityArtworkSubmissions;
    restoreOwnerArtwork(ownerVisibilityEpoch,visibilityfixture::foreground);
    check("only failed pad is hidden and all three healthy pairs are submitted",!visibilityfixture::node(windows[0]).visible&&!visibilityfixture::node(outlines[0]).visible&&artworkCalls==2&&ymcc::customtouch::draws==1&&ownerVisibilityArtworkSubmissions==submitted+3&&failureReceipts==1&&releaseCalls==1&&visibilityfixture::node(afterBadPad->outline).visible);

    beginVisibility();artworkCalls=0;const auto afterBadButton=addKey(8,9);nativeButtonSurfaces[1].safe=false;
    restoreOwnerArtwork(ownerVisibilityEpoch,visibilityfixture::foreground);
    check("failed button does not suppress the healthy custom key",artworkCalls==2&&ymcc::customtouch::draws==1&&!visibilityfixture::node(buttonOutlines[1]).visible&&visibilityfixture::node(afterBadButton->outline).visible&&failureReceipts==1&&failedButton);

    beginVisibility();artworkCalls=0;const auto badKey=addKey(8,9),afterBadKey=addKey(10,11);
    visibilityfixture::node(badKey->outline).safe=false;
    restoreOwnerArtwork(ownerVisibilityEpoch,visibilityfixture::foreground);
    check("failed custom display does not suppress the next key",artworkCalls==3&&ymcc::customtouch::draws==1&&!visibilityfixture::node(badKey->outline).visible&&visibilityfixture::node(afterBadKey->outline).visible&&releaseCalls==1);

    beginVisibility();artworkCalls=0;nativeInputSurfaces[0].safe=false;
    visibilityfixture::showReentry=[] {++padRefreshEpoch;};
    restoreOwnerArtwork(ownerVisibilityEpoch,visibilityfixture::foreground);
    check("failed pair with a rebound slot aborts the artwork pass",artworkCalls==0&&releaseCalls==0&&visibilityfixture::node(outlines[0]).visible);

    beginVisibility();artworkCalls=0;nativeInputSurfaces[0].safe=false;
    visibilityfixture::showReentry=[] {cancelOwnerVisibilityWork();};
    restoreOwnerArtwork(ownerVisibilityEpoch,visibilityfixture::foreground);
    check("failed pair cannot continue across owner retirement",artworkCalls==0&&releaseCalls==0&&!ownerVisibilityRecheck.timer);

    beginVisibility();artworkCalls=0;nativeInputSurfaces[0].safe=false;
    visibilityfixture::showReentry=[] {visibilityfixture::inputAllowed=false;};
    restoreOwnerArtwork(ownerVisibilityEpoch,visibilityfixture::foreground);
    check("failed pair cannot continue after power admission closes",artworkCalls==0&&!visibilityfixture::inputAllowed);
#endif
#endif
 }
 stopOwnerVisibility();const Json report={{"ok",ok},{"selector",stormOnly?"--display-storm":artworkOnly?"--display-artwork":suffix?"--display-suspend-resize":"--display-transitions"},{"cases",cases},{"queryCounts",queryCounts},{"realWindowsCreated",0},{"systemInputInjected",false},{"actualGame",false},{"ownedHooksRemaining",ownerVisibilityActive()},{"realTimersCreated",0}};
 std::ofstream f(std::filesystem::path(argv[1]),std::ios::binary);f<<report.dump(2);std::cout<<"VISIBILITY_TRANSITIONS checks="<<cases.size()<<" ok="<<ok<<"\n";return ok&&f.good()?0:3;
}
