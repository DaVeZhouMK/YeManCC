#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "controller_persona_settings.h"
#include "touch_haptic_dispatch.h"
#include "touch_feedback_lease.h"
using Json=nlohmann::json;
namespace mem {
struct Window {bool exists=true,visible=true,enabled=true,iconic=false;LONG_PTR style=0;DWORD pid=1,thread=11;RECT rect{0,0,300,300};HWND previous=nullptr;};
inline std::map<HWND,Window> windows;inline ULONGLONG tick=1000;inline DWORD error=0,thread=11,pid=1;inline HWND foreground=nullptr;
inline Json operations=Json::array();inline unsigned sends=0,timers=0,kills=0,posts=0,hooks=0,unhooks=0,refreshes=0,closes=0,visuals=0,hapticCalls=0;inline bool postOkay=true,positionOkay=true;
inline HWND handle(uintptr_t n){return reinterpret_cast<HWND>(n);}
inline Window& window(HWND h){return windows.at(h);}
inline BOOL isWindow(HWND h){return h&&windows.count(h)&&window(h).exists;}
inline BOOL visible(HWND h){return isWindow(h)&&window(h).visible;}
inline BOOL enabled(HWND h){return isWindow(h)&&window(h).enabled;}
inline DWORD ownerThread(HWND h,LPDWORD process){if(!isWindow(h))return 0;if(process)*process=window(h).pid;return window(h).thread;}
inline BOOL post(HWND h,UINT msg,WPARAM w,LPARAM){++posts;operations.push_back({{"api","PostMessage"},{"window",(uintptr_t)h},{"message",msg},{"epoch",w}});return postOkay;}
inline UINT send(UINT n,INPUT*,int){++sends;operations.push_back({{"api","memory-input-batch"},{"count",n}});return n;}
inline BOOL show(HWND h,int command){if(isWindow(h))window(h).visible=command!=SW_HIDE;operations.push_back({{"api","ShowWindow"},{"window",(uintptr_t)h},{"command",command}});return TRUE;}
inline BOOL position(HWND h,HWND,int,int,int,int,UINT flags){operations.push_back({{"api","SetWindowPos"},{"window",(uintptr_t)h},{"flags",flags}});return positionOkay;}
inline UINT_PTR timer(HWND h,UINT_PTR id,UINT delay,TIMERPROC){++timers;operations.push_back({{"api","SetTimer"},{"window",(uintptr_t)h},{"id",id},{"delay",delay}});return id+100;}
inline BOOL kill(HWND h,UINT_PTR id){++kills;operations.push_back({{"api","KillTimer"},{"window",(uintptr_t)h},{"id",id}});return TRUE;}
inline BOOL rect(HWND h,LPRECT out){if(!isWindow(h))return FALSE;*out=window(h).rect;return TRUE;}
inline BOOL intersect(LPRECT out,const RECT* a,const RECT* b){*out={std::max(a->left,b->left),std::max(a->top,b->top),std::min(a->right,b->right),std::min(a->bottom,b->bottom)};return out->right>out->left&&out->bottom>out->top;}
inline HWINEVENTHOOK hook(DWORD,DWORD,HMODULE,WINEVENTPROC,DWORD,DWORD,DWORD){++hooks;return reinterpret_cast<HWINEVENTHOOK>((uintptr_t)hooks+10);}
inline BOOL unhook(HWINEVENTHOOK){++unhooks;return TRUE;}
inline BOOL destroy(HWND h){if(isWindow(h)){window(h).exists=false;window(h).visible=false;}return TRUE;}
}
#define GetTickCount64() mem::tick
#define GetCurrentThreadId() mem::thread
#define GetCurrentProcessId() mem::pid
#define GetWindowThreadProcessId mem::ownerThread
#define GetLastError() mem::error
#define SetLastError(e) (mem::error=(e))
#define PostMessageW mem::post
#define SendInput mem::send
#define MapVirtualKeyW(v,f) (UINT)(v)
#define InvalidateRect(h,r,b) TRUE
#define IsWindow mem::isWindow
#define IsWindowVisible mem::visible
#define IsWindowEnabled mem::enabled
#define EnableWindow(h,e) (mem::window(h).enabled=(e))
#define GetWindowLongPtrW(h,x) (mem::isWindow(h)?mem::window(h).style:0)
#define SetWindowLongPtrW(h,x,s) (mem::window(h).style=(s))
#define IsIconic(h) (mem::window(h).iconic)
#define ShowWindow mem::show
#define SetWindowPos mem::position
#define GetWindowRect mem::rect
#define IntersectRect mem::intersect
#define GetWindow(h,x) (mem::window(h).previous)
#define GetForegroundWindow() mem::foreground
#define GetAncestor(h,x) (h)
#define GetDesktopWindow() mem::handle(90)
#define GetShellWindow() mem::handle(91)
#define SetTimer mem::timer
#define KillTimer mem::kill
#define SetWinEventHook mem::hook
#define UnhookWinEvent mem::unhook
#define DestroyWindow mem::destroy
#include "screen_touchpad_display_policy.h"
#include "touch-core.inc"
namespace ymcc::screenpads {
#include "touch-state.inc"
inline std::array<HWND,2> windows{},outlines{};inline std::array<HWND,7> buttonWindows{},buttonOutlines{};inline std::array<UINT32,7> buttonContacts{};
struct Visual {HWND window=nullptr;bool visible=false;};inline std::array<Visual,2> contactVisuals{};inline HWND cursorWindow=nullptr;inline bool cursorVisible=false;inline DWORD cursorError=0;inline unsigned long long cursorStackRaises=0,cursorStackRaiseFailures=0;
#include "touch-policy.inc"

inline std::atomic<bool> controlTargetEnabled{false},controlEdgeEnabled{false};inline unsigned buttonHapticContacts=0;
#include "touch-button-policy.inc"

inline void buttonsReleaseMapped(){}
inline void buttonsRelease(){buttonContacts={};}
inline void buttonsNativeInputDisable(){++mem::closes;}
inline void cursorStop(){cursorVisible=false;}
inline void nativeInputCancel(){++mem::closes;}
inline void nativeInputClose(int){++mem::closes;}
inline void syncOutlineFeedback(int=-1){}
inline void syncContactVisuals(int=-1,bool=false){++mem::visuals;}
inline bool renderOutline(int,int){return true;}
inline bool renderButton(int,int){return true;}
inline void buttonsRepaint(int){}

inline void hideContactVisual(Visual& v){v.visible=false;ShowWindow(v.window,SW_HIDE);}
inline void cursorHideVisual(){cursorVisible=false;}
inline void cursorShutdown(){cursorVisible=false;}
inline void buttonsShutdown(){buttonContacts={};}
inline void shutdownContactVisuals(){for(auto& v:contactVisuals)v.visible=false;}

struct Bitmap {void reset(){}};inline std::array<Bitmap,2> outlineBitmaps{};inline std::array<std::array<Bitmap,2>,2> outlineFrames{};inline std::array<int,2> outlineUploads{};
inline void refresh(bool=false){++mem::refreshes;} // Window/compositor boundary does not create real HWNDs.
#include "touch-autohide-state.inc"
inline AutoHideState autoHide;inline void autoHideSync(bool reset=false);
inline void autoHideTouch();inline void autoHideStop();
}
#include "touch-output.inc"
namespace ymcc::screenpads {
#include "screen_touchpad_auto_hide.h"
inline bool onOwnerThread(HWND) noexcept;
}
#include "screen_touchpad_visibility.h"
#include "touch-configure.inc"
#include "touch-owner.inc"
using namespace ymcc::screenpads;
inline Json evidence=Json::array();inline unsigned failed=0;
static void feedback(unsigned contacts,bool pulse,int strength){++mem::hapticCalls;mem::operations.push_back({{"api","memory-feedback-sink"},{"contacts",contacts},{"pulse",pulse},{"strength",strength}});}
static Json leaseStats();
static Json snapshot(){return {{"lease",leaseStats()},{"config",configJson(core.config)},{"contacts",snapshotJson(core.snapshot(mem::tick))},{"leftOwner",core.pads[0].id},{"rightOwner",core.pads[1].id},{"sentKeys",sentKeys},{"sentClick",sentClick},{"ownerEpoch",ownerLifecycleEpoch},{"ownerQueued",ownerRebindQueued},{"visibilityEpoch",ownerVisibilityEpoch},{"autoHidden",autoHide.hidden},{"timer",autoHide.timer},{"nativeBlocked",core.nativeOutputBlocked}};}
static void check(const std::string& name,bool pass,const Json& input,const std::string& consumer){if(!pass)++failed;evidence.push_back({{"id",name},{"status",pass?"PASS":"FAIL"},{"provenance","source-derived-fixture"},{"input",input},{"consumer",consumer},{"state",snapshot()},{"feedback",{{"memoryOperations",mem::operations},{"physicalOutputVerified",false}}}});mem::operations=Json::array();}
static void setup(){
 mem::windows.clear();mem::operations=Json::array();mem::tick=1000;mem::thread=11;mem::pid=1;mem::error=0;mem::postOkay=mem::positionOkay=true;
 for(uintptr_t i:{1,2,3,4,5,6,7,8,9,90,91})mem::windows[mem::handle(i)]={};
 mem::foreground=mem::handle(9);mem::window(mem::foreground).pid=2;mem::window(mem::foreground).style=WS_EX_TOPMOST;
 owner=mem::handle(1);windows={mem::handle(2),mem::handle(3)};outlines={mem::handle(4),mem::handle(5)};
 for(HWND h:{windows[0],windows[1],outlines[0],outlines[1]})mem::window(h).previous=mem::foreground;
 core=Core{};core.config=profileDefault(1);core.config.autoHideSeconds=15;initialized=true;enabled=true;available=true;nativeMode=true;nativeInputCandidateRequested=true;sentKeys=0;sentClick=false;dryRun=false;
 buttonWindows={};buttonOutlines={};buttonContacts={};contactVisuals={};cursorWindow=nullptr;cursorVisible=false;nativeTransport={};autoHide={};ownerRebindQueued=false;++ownerLifecycleEpoch;ownerVisibilityHooks={};ownerVisibilityQueued=false;++ownerVisibilityEpoch;
 touchHapticPulseSink=feedback;touchHapticEventGate.reset();feedbackOnlyContacts.cancel();
}
struct LeaseOps {
 struct Spec{unsigned reportLength=32;};struct Resource{bool pending=false;};
 inline static unsigned opens=0,writes=0,closed=0,cancels=0;inline static bool pendingWrite=false,reapReady=false,premature=false;
 static std::uint64_t now(){return mem::tick;}
 static Resource* open(const Spec&,std::uint32_t&){++opens;mem::operations.push_back({{"api","memory-lease-open"}});return new Resource;}
 static bool pending(const Resource& resource){return resource.pending;}
 static void cancel(Resource&){++cancels;mem::operations.push_back({{"api","memory-lease-cancel"}});}
 static bool reap(Resource& resource){if(resource.pending&&!reapReady)return false;resource.pending=false;return true;}
 static void close(Resource* resource){++closed;premature|=resource->pending;delete resource;mem::operations.push_back({{"api","memory-lease-close"}});}
 static void quarantine(Resource*){}
 static ymcc::touchhaptic::Receipt write(Resource& resource,unsigned a,unsigned b){++writes;ymcc::touchhaptic::Receipt result;result.attempted=true;result.ok=!pendingWrite;result.written=result.ok?32:0;
  if(pendingWrite){resource.pending=true;result.reason=ymcc::touchhaptic::Reason::IoPending;result.error=996;}
  mem::operations.push_back({{"api","memory-lease-write"},{"small",a},{"large",b},{"ok",result.ok}});return result;}
};
static Json leaseStats(){return {{"opens",LeaseOps::opens},{"writes",LeaseOps::writes},{"closed",LeaseOps::closed},{"cancels",LeaseOps::cancels},{"prematureClose",LeaseOps::premature},{"reapReady",LeaseOps::reapReady}};}
int main(int argc,char** argv){try{
 if(argc==2&&std::string(argv[1])=="--config"){
  ProfileBank bank;std::string line;
  while(std::getline(std::cin,line)){
   try{const Json message=Json::parse(line),args=message.value("args",Json::object());const std::string cmd=message.value("cmd","");const auto persona=args.value("persona",std::string("steamdeck"));const int profile=profileIndex(persona);if(profile<0)throw std::runtime_error("TOUCH_PERSONA_REJECTED");
    if(cmd=="screenTouchpads.set"){Config next;if(!parseConfig(args.at("config"),bank.slots[profile],next))throw std::runtime_error("TOUCH_CONFIG_REJECTED");bank.slots[profile]=next;}
    else if(cmd!="screenTouchpads.get")throw std::runtime_error("TOUCH_COMMAND_REJECTED");
    auto reply=configJson(bank.slots[profile]);reply["persona"]=persona;reply["ok"]=true;reply["available"]=true;reply["visible"]=false;reply["settingsSaved"]=false;reply["steamDeckAvailable"]=false;reply["ps5Available"]=false;reply["reason"]="owned-memory-config-only";
    std::cout<<Json{{"result",reply}}.dump()<<std::endl;
   }catch(const std::exception& error){std::cout<<Json{{"error",error.what()}}.dump()<<std::endl;}
  }return 0;
 }
 setup();output(core.event(0,11,0,20,20,200,200,1000));const auto first=core.pads[0].id;output(core.event(0,22,0,180,180,200,200,1001));
 check("second-finger-cannot-steal-surface",first==11&&core.pads[0].id==11,{{"downIds",{11,22}}},"Core.event/output");
 output(core.event(0,22,2,20,20,200,200,1002));check("foreign-UP-does-not-release-owner",core.pads[0].down,{{"upId",22}},"Core.event");
 output(core.event(1,11,0,120,20,200,200,1003));output(core.event(0,11,2,20,20,200,200,1400));
 check("same-id-other-surface-independent",!core.pads[0].down&&core.pads[1].down,{{"surface",0},{"upId",11}},"Core.event/output");
 release();check("cancel-clears-contact-output-feedback",!core.pads[0].down&&!core.pads[1].down&&sentKeys==0&&!sentClick,{{"event","cancel"}},"release/releaseMappedState/Core.cancel/outputHaptic/output");
 output(core.event(0,11,1,100,100,200,200,1401));check("late-MOVE-after-cancel-is-no-op",!core.pads[0].down,{{"phase","MOVE"}},"Core.event");
 setup();output(core.event(0,1,0,20,20,200,200,1000));Config cfg=core.config;cfg.transparency=90;const bool opacity=configure(cfg);
 check("opacity-only-preserves-touch",!opacity&&core.pads[0].down,{{"patch",{{"transparency",90}}}},"configure");
 cfg.enabled=false;configure(cfg);output(core.event(0,2,0,20,20,200,200,1100));
 check("disable-cleans-and-rejects-new-touch",!enabled&&!core.pads[0].down&&sentKeys==0,{{"patch",{{"enabled",false}}}},"configure/release/Core.event");
 setup();output(core.event(0,1,0,20,20,200,200,1000));observeNativeTransport(1,true);output(core.event(0,2,0,30,30,200,200,1001));
 observeNativeTransport(2,false);check("target-removal-retires-owned-native-contact",core.nativeOutputBlocked&&!core.pads[0].down&&feedbackOnlyContacts.mask()!=0,{{"transportEpoch",2},{"ready",false}},"observeNativeTransportLocked");
 output(core.event(0,3,0,30,30,200,200,1002));check("removed-target-rejects-input",!core.pads[0].down,{{"id",3}},"Core.event");
 mem::tick=2000;observeNativeTransport(3,true);output(core.event(0,3,0,30,30,200,200,1500));check("restored-target-does-not-replay-old-DOWN",!core.pads[0].down,{{"sourceTime",1500}},"observeNativeTransportLocked/Core.event");
 output(core.event(0,4,0,30,30,200,200,2000));check("restored-target-admits-fresh-DOWN",core.pads[0].down&&core.pads[0].id==4,{{"sourceTime",2000}},"Core.event");
 setup();check("owner-rejects-foreign-process",!onOwnerThread(mem::foreground),{{"owner","foreign-process"}},"onOwnerThread");
 mem::thread=12;check("owner-rejects-wrong-thread",!onOwnerThread(owner.load()),{{"thread",12}},"onOwnerThread");mem::thread=11;
 const auto postBefore=mem::posts;queueOwnerResumeRebind();const auto oldEpoch=ownerLifecycleEpoch;queueOwnerResumeRebind();
 check("resume-rebind-coalesces",mem::posts==postBefore+1&&ownerRebindQueued,{{"resumeNotifications",2}},"queueOwnerResumeRebind");
 ownerOnSuspend();check("suspend-invalidates-old-resume-epoch",!consumeOwnerRebind(oldEpoch)&&!ownerRebindQueued,{{"epoch",oldEpoch}},"ownerOnSuspend/consumeOwnerRebind");
 queueOwnerResumeRebind();check("fresh-resume-closes-old-sources-once",consumeOwnerRebind(ownerLifecycleEpoch)&&!ownerRebindQueued,{{"epoch",ownerLifecycleEpoch}},"consumeOwnerRebind");
 check("duplicate-rebind-consumption-rejected",!consumeOwnerRebind(ownerLifecycleEpoch),{{"duplicate",true}},"consumeOwnerRebind");
 setup();available=false;autoHideSync(true);check("native-unavailable-gate-excludes-touch-visual",autoHide.visibleMask==0,{{"available",false}},"permitted/autoHideVisibleMask");available=true;
 setup();autoHideSync(true);const auto id=autoHide.timer;const auto inputVisible=IsWindowVisible(windows[0]);mem::tick=16000;autoHideTimerProc(owner.load(),WM_TIMER,id,0);
 check("auto-hide-hides-visual-not-input",autoHide.hidden&&inputVisible&&IsWindowVisible(windows[0])&&!IsWindowVisible(outlines[0]),{{"time",16000}},"autoHideTimerProc/autoHideSync/autoHideSetHidden");
 autoHideTouch();const auto newTimer=autoHide.timer;check("first-touch-wakes-with-new-timer",!autoHide.hidden&&IsWindowVisible(outlines[0])&&newTimer!=id,{{"event","touch"}},"autoHideTouch");
 autoHideTimerProc(owner.load(),WM_TIMER,id,0);check("queued-old-timer-cannot-hide-new-gesture",autoHide.timer==newTimer&&!autoHide.hidden,{{"oldTimer",id}},"autoHideTimerProc");
 output(core.event(0,7,0,20,20,200,200,mem::tick));mem::tick+=15000;autoHideTimerProc(owner.load(),WM_TIMER,newTimer,0);
 check("stationary-held-contact-prevents-idle-hide",!autoHide.hidden&&core.pads[0].down&&!autoHide.timer,{{"held",true}},"autoHideContactHeld/autoHideSync");
 setup();contactVisuals[0]={mem::handle(6),true};contactVisuals[1]={mem::handle(7),false};ShowWindow(mem::handle(7),SW_HIDE);cursorWindow=mem::handle(8);cursorVisible=true;
 syncOwnerVisibility();const auto visEpoch=ownerVisibilityEpoch;const auto beforeEvents=ownerVisibilityEvents;
 screenVisibilityObserved(nullptr,EVENT_OBJECT_REORDER,GetDesktopWindow(),OBJID_CLIENT,CHILDID_SELF,0,0);
 check("desktop-client-reorder-is-consumed",ownerVisibilityEvents==beforeEvents+1&&ownerVisibilityQueued,{{"event","EVENT_OBJECT_REORDER"},{"object","OBJID_CLIENT"}},"screenVisibilityObserved/queueOwnerVisibility");
 mem::operations=Json::array();const bool consumed=consumeOwnerVisibility(visEpoch);std::vector<uintptr_t> raised;for(const auto& op:mem::operations)if(op["api"]=="SetWindowPos")raised.push_back(op["window"]);
 check("feedback-restores-after-input-and-outline-cursor-last",consumed&&raised==std::vector<uintptr_t>{2,4,3,5,6,8}&&!IsWindowVisible(mem::handle(7)),{{"reorder",true}},"consumeOwnerVisibility/restoreScreenLayerPair/restoreScreenFeedbackOrder");
 const auto stale=ownerVisibilityEpoch;stopOwnerVisibility();check("stopped-visibility-rejects-stale-message",!consumeOwnerVisibility(stale),{{"epoch",stale}},"stopOwnerVisibility/consumeOwnerVisibility");
 setup();output(core.event(0,8,0,20,20,200,200,1000));queueOwnerResumeRebind();const auto pending=ownerLifecycleEpoch;shutdownForOwner();
 check("close-destroys-memory-surfaces-clears-output-and-old-rebind",!initialized&&!owner.load()&&!windows[0]&&!outlines[0]&&sentKeys==0&&!sentClick&&!consumeOwnerRebind(pending),{{"event","close"}},"shutdownForOwner/shutdown/release");
 setup();mem::postOkay=false;check("post-failure-does-not-stick-queued-owner",!queueOwnerResumeRebind()&&!ownerRebindQueued,{{"PostMessage",false}},"queueOwnerResumeRebind");
 setup();using namespace ymcc::touchhaptic;SourceToken token{Backend::Msi,1,3,7,4};FeedbackLease<LeaseOps> lease;
 auto receipt=lease.write(token,{32},20,20,[]{return true;});lease.write(token,{32},20,20,[]{return true;});
 check("feedback-lease-reuses-owned-generation",receipt.ok&&LeaseOps::opens==1&&LeaseOps::writes==2,{{"tokenGeneration",1},{"receiptOk",receipt.ok}},"FeedbackLease.write/ensure");
 auto next=token;++next.generation;lease.write(next,{32},20,20,[]{return true;});
 check("feedback-source-change-retires-old-resource",LeaseOps::opens==2&&LeaseOps::closed==1,{{"tokenGeneration",next.generation}},"FeedbackLease.ensure/retire");
 LeaseOps::pendingWrite=true;receipt=lease.write(next,{32},20,20,[]{return true;});const auto oldWrites=LeaseOps::writes;lease.invalidate();auto newer=next;++newer.power;
 auto rejected=lease.write(newer,{32},20,20,[]{return true;});
 check("cancelled-pending-lease-blocks-new-generation-without-early-close",!receipt.ok&&receipt.reason==Reason::IoPending&&!rejected.ok&&LeaseOps::writes==oldWrites&&LeaseOps::closed==1&&!LeaseOps::premature,{{"power",newer.power},{"receiptReason","IoPending"}},"FeedbackLease.write/invalidate/retire");
 LeaseOps::pendingWrite=false;LeaseOps::reapReady=true;mem::tick+=5000;receipt=lease.write(newer,{32},20,20,[]{return true;});
 check("pending-completion-reaped-before-replacement-opens",receipt.ok&&LeaseOps::closed==2&&LeaseOps::opens==3&&!LeaseOps::premature,{{"reapReady",true}},"FeedbackLease.ensure/retire/write");
 const auto beforeDeny=LeaseOps::writes;receipt=lease.write(newer,{32},20,20,[]{return false;});
 check("lease-authority-rechecked-before-memory-write",!receipt.ok&&receipt.reason==Reason::SourceChanged&&LeaseOps::writes==beforeDeny,{{"allowed",false}},"FeedbackLease.write");
 std::cout<<Json{{"schemaVersion",1},{"status",failed?"FAIL":"PASS"},{"checks",evidence.size()},{"failed",failed},{"inputConsumerStateFeedback",evidence},{"boundaries",{{"actualOsInput",false},{"actualHooks",false},{"actualWindows",false},{"physicalFeedback",false}}}}.dump()<<'\n';return failed?1:0;
 }catch(const std::exception& e){std::cout<<Json{{"status","FAIL"},{"error",e.what()}}.dump()<<'\n';return 2;}}




