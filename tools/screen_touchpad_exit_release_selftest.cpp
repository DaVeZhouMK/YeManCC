// Offline execution host: all policy functions and Surface state declarations are
// included verbatim by the runner from SHA-bound production source segments.
// SDK types only; every used window/input/WinRT boundary below is a memory mock.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
using Json=nlohmann::json;
namespace mock {
struct Window {bool live=true,visible=true,enabled=true;DWORD thread=71,pid=81;LONG_PTR style=WS_EX_LAYERED|WS_EX_NOACTIVATE;HWND parent=nullptr;int failures=0,attempts=0,reads=0;};
inline std::map<HWND,Window> windows;
inline Json events=Json::array(),cases=Json::array();
inline std::vector<std::vector<INPUT>> batches;
inline std::deque<UINT> sends;
inline DWORD thread=71,pid=81,lastError=0;
inline HWND capture=nullptr;
inline int nextWindow=100,releaseCaptureCalls=0;
inline unsigned faults=0,unexpected=0;
inline bool rejectAll=false,watchWithdrawal=false,reenter=false,callbackBusy=false;
inline std::function<void(HWND)> mutation;
inline std::function<void()> recursive,withdrawal;
inline std::map<std::string,int> ops;
inline std::string scenario;
inline std::uintptr_t id(HWND h){return reinterpret_cast<std::uintptr_t>(h);}
inline void log(const char* op,HWND h=nullptr){events.push_back({{"op",op},{"hwnd",id(h)}});}
inline void check(const std::string& name,bool ok,Json detail=Json::object()){
 cases.push_back({{"name",name},{"ok",ok},{"detail",std::move(detail)}});
 std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';
}
inline void callback(){if(reenter&&recursive){if(callbackBusy)return;callbackBusy=true;recursive();callbackBusy=false;}}
inline void boundary(const char* op,unsigned bit=0){++ops[op];log(op);if(watchWithdrawal&&withdrawal)withdrawal();callback();if(faults&bit)throw 1;}
inline HWND window(HWND parent=nullptr){auto h=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(nextWindow++));windows[h].parent=parent;return h;}
inline void clear(){windows.clear();events=Json::array();batches.clear();sends.clear();thread=71;pid=81;lastError=0;capture=nullptr;nextWindow=100;releaseCaptureCalls=0;faults=unexpected=0;rejectAll=watchWithdrawal=reenter=callbackBusy=false;mutation={};recursive={};withdrawal={};ops.clear();}
inline void mutated(HWND h){if(mutation&&!callbackBusy){callbackBusy=true;mutation(h);callbackBusy=false;}}
}
// Macros apply only after SDK declarations, so no Windows import can be called.
inline BOOL MockIsWindow(HWND h){auto it=mock::windows.find(h);if(it==mock::windows.end())return FALSE;++it->second.reads;return it->second.live;}
inline BOOL MockIsWindowVisible(HWND h){auto it=mock::windows.find(h);return it!=mock::windows.end()&&it->second.live&&it->second.visible;}
inline BOOL MockIsWindowEnabled(HWND h){auto it=mock::windows.find(h);return it!=mock::windows.end()&&it->second.live&&it->second.enabled;}
inline BOOL MockShowWindow(HWND h,int show){mock::log(show==SW_HIDE?"hide":"show",h);auto it=mock::windows.find(h);if(it==mock::windows.end())return FALSE;bool was=it->second.visible;it->second.visible=show!=SW_HIDE;mock::mutated(h);return was;}
inline BOOL MockEnableWindow(HWND h,BOOL enabled){mock::log(enabled?"enable":"disable",h);auto it=mock::windows.find(h);if(it==mock::windows.end())return FALSE;bool was=it->second.enabled;it->second.enabled=enabled!=FALSE;mock::mutated(h);return !was;}
inline HWND MockGetCapture(){mock::log("getCapture");return mock::capture;}
inline BOOL MockReleaseCapture(){++mock::releaseCaptureCalls;mock::log("releaseCapture",mock::capture);mock::capture=nullptr;return TRUE;}
inline DWORD MockGetCurrentThreadId(){return mock::thread;}
inline DWORD MockGetCurrentProcessId(){return mock::pid;}
inline DWORD MockGetWindowThreadProcessId(HWND h,DWORD* pid){auto it=mock::windows.find(h);if(it==mock::windows.end()){*pid=0;return 0;}*pid=it->second.pid;return it->second.thread;}
inline BOOL MockDestroyWindow(HWND h){mock::log("destroy",h);auto it=mock::windows.find(h);if(it==mock::windows.end()||!it->second.live)return FALSE;auto& w=it->second;++w.attempts;if(w.failures){if(w.failures>0)--w.failures;mock::lastError=5;return FALSE;}w.live=w.visible=w.enabled=false;for(auto& [child,v]:mock::windows)if(v.parent==h)v.live=v.visible=v.enabled=false;return TRUE;}
inline DWORD MockGetLastError(){return mock::lastError;}
inline void MockSetLastError(DWORD value){mock::lastError=value;}
inline LONG_PTR MockGetWindowLongPtrW(HWND h,int){return mock::windows.at(h).style;}
inline LONG_PTR MockSetWindowLongPtrW(HWND h,int,LONG_PTR style){mock::log("style",h);auto& w=mock::windows.at(h);auto old=w.style;w.style=style;return old;}
inline BOOL MockSetWindowPos(HWND h,HWND,int,int,int,int,UINT){mock::log("position",h);return TRUE;}
inline BOOL MockGetLayeredWindowAttributes(HWND,COLORREF*,BYTE*,DWORD*){return FALSE;}
inline BOOL MockGetClientRect(HWND,RECT* r){*r={0,0,320,200};return TRUE;}
inline BOOL MockInvalidateRect(HWND,const RECT*,BOOL){return TRUE;}
inline UINT MockMapVirtualKeyW(UINT vk,UINT){return vk;}
inline UINT MockSendInput(UINT count,INPUT* input,int size){
 if(size!=sizeof(INPUT)){++mock::unexpected;return 0;}
 mock::batches.emplace_back(input,input+count);mock::log("sendUpBatch");
 UINT accepted=count;if(mock::rejectAll)accepted=0;else if(!mock::sends.empty()){accepted=std::min(count,mock::sends.front());mock::sends.pop_front();}
 mock::lastError=accepted==count?0:ERROR_ACCESS_DENIED;return accepted;
}
#define IsWindow MockIsWindow
#define IsWindowVisible MockIsWindowVisible
#define IsWindowEnabled MockIsWindowEnabled
#define ShowWindow MockShowWindow
#define EnableWindow MockEnableWindow
#define GetCapture MockGetCapture
#define ReleaseCapture MockReleaseCapture
#define GetCurrentThreadId MockGetCurrentThreadId
#define GetCurrentProcessId MockGetCurrentProcessId
#define GetWindowThreadProcessId MockGetWindowThreadProcessId
#define DestroyWindow MockDestroyWindow
#define GetLastError MockGetLastError
#define SetLastError MockSetLastError
#define GetWindowLongPtrW MockGetWindowLongPtrW
#define SetWindowLongPtrW MockSetWindowLongPtrW
#define SetWindowPos MockSetWindowPos
#define GetLayeredWindowAttributes MockGetLayeredWindowAttributes
#define GetClientRect MockGetClientRect
#define InvalidateRect MockInvalidateRect
#define MapVirtualKeyW MockMapVirtualKeyW
#define SendInput MockSendInput
#ifndef EXIT_RELEASE_SIGNALS_ONLY
namespace winrt {
struct event_token {long long value=0;};
struct hresult_error {HRESULT value;HRESULT code()const{return value;}};
[[noreturn]] inline void throw_hresult(HRESULT e){throw hresult_error{e};}
[[noreturn]] inline void throw_last_error(){throw_hresult(E_FAIL);}
template<int Role> struct Handle {
 bool present=false,transparent=true,captured=true;
 Handle(std::nullptr_t=nullptr){};explicit operator bool()const{return present;}
 Handle& operator=(std::nullptr_t){present=false;return *this;}
 void PointerPressed(event_token){mock::boundary("revoke0",1);}
 void PointerMoved(event_token){mock::boundary("revoke1",2);}
 void PointerReleased(event_token){mock::boundary("revoke2",4);}
 void PointerCaptureLost(event_token){mock::boundary("revoke3",8);}
 bool HasCapture()const{return present&&captured;}
 void ReleasePointerCapture(){mock::boundary("pointerRelease",16);captured=false;}
 void Close(){if constexpr(Role==1)mock::boundary("controllerClose",32);else mock::boundary("targetClose",64);}
 bool IsTransparentForUncontrolledInput()const{return transparent;}
 void IsTransparentForUncontrolledInput(bool value){transparent=value;}
};
namespace Windows::UI::Composition {using SpriteVisual=Handle<3>;namespace Desktop {using DesktopWindowTarget=Handle<2>;}}
namespace Windows::UI::Core {using CoreIndependentInputSourceController=Handle<1>;using CoreIndependentInputSource=Handle<0>;}
}
namespace ymcc::screeninput {
enum class Phase {Cancel};
struct Event {int surface;UINT32 id;POINTER_INPUT_TYPE type;Phase phase;float x,y;bool cancelled;};
using Sink=void(*)(HWND,const Event&,void*);
class Surface {
public: // Fixture access; exact production fields, no policy implementation here.
#include "surface_fields.inc"
 HRESULT error=S_OK;UINT64 uploads=0,cacheHits=0;
 void emit(const Event&){mock::boundary("cancelSink");}
 bool uploadZero(SIZE size){uploaded_=true;uploadedSize_=size;++uploads;mock::log("uploadZero",root_);return true;}
#include "surface_methods.inc"
 void seed(HWND root,HWND child){root_=root;child_=child;thread_=mock::thread;oldStyle_=WS_EX_NOACTIVATE;layered_=uploaded_=true;uploadedSize_={320,200};source_.present=controller_.present=target_.present=visual_.present=true;for(int i=0;i<4;++i)tokens_[i].value=i+1;occupied_[0]=true;contacts_[0]=123;}
};
}
#endif
namespace sp {
inline void (*quickAccessEdgeObserver)(int,bool)=nullptr;
#include "action.inc"
inline constexpr ULONG_PTR kInjectedTag=0x594d5450;
inline unsigned sentKeys=0;inline bool sentClick=false,dryRun=false;
inline DWORD inputError=0,windowError=0;
inline unsigned long long injectedCount=0;
inline void (*outputEvidenceObserver)(const INPUT*,UINT,UINT,DWORD,bool) noexcept=nullptr;
inline bool initialized=false,inputTeardown=false;
inline std::atomic<bool> enabled{false},nativeMode{false},available{false},psAvailable{false},controlTargetEnabled{false},controlEdgeEnabled{false};
inline std::atomic<unsigned> screenButtonMask{0};
inline std::atomic<HWND> owner{nullptr};
inline std::mutex mutex;
struct Core {struct Pad{bool down=false;};struct Click{bool active=false;};std::array<Pad,2> pads{};std::array<Click,2> clicks{};Action cancel(){pads={};clicks={};return {};}};
inline Core core;
struct Feedback {unsigned value=0;std::vector<unsigned> sequence;size_t reads=0;unsigned mask(){unsigned r=reads<sequence.size()?sequence[reads]:value;++reads;return r;}void cancel(){value=0;sequence.clear();}};
inline Feedback feedbackOnlyContacts;
inline std::array<HWND,2> windows{},outlines{};
inline std::array<HWND,7> buttonWindows{},buttonOutlines{};
#ifndef EXIT_RELEASE_SIGNALS_ONLY
inline std::array<ymcc::screeninput::Surface,2> nativeInputSurfaces;
inline std::array<ymcc::screeninput::Surface,7> nativeButtonSurfaces;
#endif
inline void outputHaptic(const Action&){} // External haptic projection is out of scope.
#include "evidence_declarations.inc"
#include "sender.inc"
#include "open_sources.inc"
#include "signals.inc"
#ifndef EXIT_RELEASE_SIGNALS_ONLY
struct Image {void reset(){}};
inline std::array<int,2> outlineUploads{};inline std::array<int,7> buttonUploads{},buttonArtwork{};
inline std::array<Image,2> outlineBitmaps{};inline std::array<std::array<Image,2>,2> outlineFrames{};
inline std::array<Image,7> buttonArtBitmaps{};inline std::array<std::array<Image,2>,7> buttonFrames{};
struct ContactVisual {HWND window=nullptr;};inline std::array<ContactVisual,2> contactVisuals{};inline HWND cursorWindow=nullptr;
struct AutoHide {std::array<int,2> specialContacts{};};inline AutoHide autoHide;
inline void cursorStop(){mock::log("cursorStop");}inline void cursorShutdown(){mock::log("cursorShutdown");}
inline void autoHideStop(){mock::log("autoHideStop");}inline void autoHideSync(){}
inline void hideContactVisual(ContactVisual&){mock::log("hideVisual");}inline void shutdownContactVisuals(){mock::log("shutdownVisuals");}
inline void syncOutlineFeedback(){}inline void syncContactVisuals(){}
inline std::array<unsigned,7> buttonContacts{};inline std::array<bool,7> buttonArmed{},buttonFeedbackContacts{},buttonFeedbackPressed{};inline unsigned buttonHapticContacts=0;
struct Config {int transparency=80;};inline Config getConfig(){return {};}
inline void standaloneCancel(){mock::log("standaloneCancel");}inline void renderButton(int,int){}
#include "withdrawal.inc"
#endif
}
namespace tests {
inline Json snapshots=Json::array();
inline void reset(){mock::clear();sp::sentKeys=0;sp::sentClick=false;sp::dryRun=false;sp::inputError=sp::windowError=0;sp::injectedCount=0;sp::inputTeardown=true;sp::initialized=false;sp::screenButtonMask=0;sp::core={};sp::feedbackOnlyContacts={};sp::windows={};sp::outlines={};sp::buttonWindows={};sp::buttonOutlines={};sp::exitInputWindows={};sp::exitInputWindowCount=0;sp::exitInputChecks={};sp::exitInputCheckCount=0;sp::owner=nullptr;
#ifndef EXIT_RELEASE_SIGNALS_ONLY
 for(auto& s:sp::nativeInputSurfaces){s.~Surface();new(&s)ymcc::screeninput::Surface;}
 for(auto& s:sp::nativeButtonSurfaces){s.~Surface();new(&s)ymcc::screeninput::Surface;}
#endif
}
inline bool upsOnly(){for(const auto& batch:mock::batches)for(const auto& e:batch){if(e.type==INPUT_KEYBOARD){if(!(e.ki.dwFlags&KEYEVENTF_KEYUP)||e.ki.wVk||e.ki.dwExtraInfo!=sp::kInjectedTag)return false;}else if(e.type!=INPUT_MOUSE||e.mi.dwFlags!=MOUSEEVENTF_LEFTUP||e.mi.dx||e.mi.dy)return false;}return mock::unexpected==0;}
inline Json capture(const char* name){auto s=sp::exitInputReleaseState();snapshots.push_back({{"name",name},{"state",s},{"events",mock::events},{"sendBatches",mock::batches.size()},{"contactReads",sp::feedbackOnlyContacts.reads}});return s;}
inline unsigned cleanTail(){unsigned n=0;for(unsigned i=sp::exitInputCheckCount;i&&sp::exitInputChecks[i-1].ok;--i)++n;return n;}
inline void signals(){
 reset();auto initial=sp::exitInputReleaseState();unsigned required=sp::kExitInputRequiredCleanRounds,maximum=static_cast<unsigned>(sp::exitInputChecks.size());
 mock::check("header-contract-requiredConsecutive-3-maximum-6",required==3&&maximum==6,initial);
 reset();mock::check("zero-rounds-cannot-verify",!sp::exitInputReleaseState().value("verified",true));
 sp::confirmExitInputs();auto s=capture("healthy");mock::check("healthy-requires-three-fresh-clean-reads",s.value("verified",false)&&sp::exitInputCheckCount>=required&&cleanTail()>=required&&sp::feedbackOnlyContacts.reads==sp::exitInputCheckCount&&mock::batches.empty(),s);
 reset();sp::sentKeys=255;sp::sentClick=true;mock::sends={0};sp::confirmExitInputs();s=capture("first-send-rejected");mock::check("first-UP-rejected-compensation-recovers-and-confirms",s.value("verified",false)&&mock::batches.size()>=2&&!sp::sentKeys&&!sp::sentClick&&cleanTail()>=required&&upsOnly(),s);
 reset();sp::sentKeys=255;sp::sentClick=true;mock::sends={0,0};sp::confirmExitInputs();s=capture("first-round-rejected");mock::check("failed-round-followed-by-three-clean-rounds-can-verify",s.value("verified",false)&&!sp::exitInputChecks[0].ok&&sp::exitInputCheckCount>=required+1&&cleanTail()>=required&&!sp::sentKeys&&!sp::sentClick&&upsOnly(),s);
 reset();sp::sentKeys=255;sp::sentClick=true;mock::rejectAll=true;sp::confirmExitInputs();s=capture("permanent-send-rejection");mock::check("permanent-UP-rejection-exhausts-maximum-without-verification",!s.value("verified",true)&&sp::exitInputCheckCount==maximum&&sp::sentKeys==255&&sp::sentClick&&mock::batches.size()<=2*maximum&&upsOnly(),s);
 reset();sp::sentKeys=255;sp::sentClick=true;mock::sends={3,0};sp::confirmExitInputs();s=capture("partial-UP-prefix");mock::check("accepted-UP-prefix-preserves-unreleased-ownership-for-next-round",s.value("verified",false)&&sp::exitInputChecks[0].keys==248&&sp::exitInputChecks[0].mouse&&!sp::sentKeys&&!sp::sentClick&&upsOnly(),s);
 reset();sp::feedbackOnlyContacts.sequence={0,0,1,0,0,0};sp::confirmExitInputs();s=capture("fresh-read-resets-streak");mock::check("third-new-read-sees-contact-and-resets-confirmation-streak",s.value("verified",false)&&sp::exitInputCheckCount==maximum&&sp::exitInputChecks[2].contacts&&cleanTail()==required&&sp::feedbackOnlyContacts.reads==maximum,s);
 reset();sp::feedbackOnlyContacts.sequence={0,0,1,0,0,1};sp::confirmExitInputs();s=capture("late-contact-never-three-consecutive");mock::check("nonconsecutive-clean-rounds-cannot-verify",!s.value("verified",true)&&sp::exitInputCheckCount==maximum&&cleanTail()==0,s);
 for(int fail:{1,-1}){reset();HWND saved=mock::window();sp::rememberExitInputWindow(saved);sp::rememberExitInputWindow(saved);mock::windows[saved].failures=fail;sp::windows[0]=nullptr;sp::confirmExitInputs();s=capture(fail==1?"destroy-recovery":"destroy-permanent");bool recovered=fail==1;mock::check(recovered?"saved-original-HWND-retried-after-DestroyWindow-failure":"permanent-DestroyWindow-failure-cannot-verify-after-owner-null",sp::exitInputWindowCount==1&&mock::windows[saved].attempts==(recovered?2:static_cast<int>(maximum))&&s.value("verified",false)==recovered&&(!recovered?sp::exitInputChecks[maximum-1].liveWindows==1:cleanTail()>=required),s);}
 for(bool otherPid:{false,true}){reset();HWND h=mock::window();sp::rememberExitInputWindow(h);if(otherPid)++mock::windows[h].pid;else ++mock::windows[h].thread;sp::confirmExitInputs();s=capture(otherPid?"foreign-pid":"foreign-thread");mock::check(otherPid?"foreign-PID-window-never-destroyed":"foreign-thread-window-never-destroyed",mock::windows[h].attempts==0&&mock::windows[h].live&&!s.value("verified",true)&&sp::exitInputCheckCount==maximum,s);}
 reset();sp::inputTeardown=false;sp::sentKeys=17;sp::sentClick=true;auto alive=mock::window();sp::rememberExitInputWindow(alive);sp::confirmExitInputs();s=capture("admission-open");mock::check("nonterminal-confirm-is-inert-and-cannot-verify",!s.value("verified",true)&&sp::exitInputCheckCount==0&&sp::sentKeys==17&&sp::sentClick&&mock::batches.empty()&&mock::windows[alive].attempts==0&&mock::windows[alive].live,s);
 reset();sp::Action late;late.keys=255;late.heldClick=true;late.tap=true;late.dx=15;late.dy=-9;sp::output(late);mock::check("terminal-output-refuses-late-DOWN-MOVE-tap-from-neutral",mock::batches.empty()&&!sp::sentKeys&&!sp::sentClick);
 sp::sentKeys=255;sp::sentClick=true;sp::output(late);mock::check("terminal-output-neutralizes-late-request-while-releasing-owned-signals",!mock::batches.empty()&&!sp::sentKeys&&!sp::sentClick&&upsOnly());
 reset();sp::screenButtonMask=127;sp::core.pads[0].down=true;sp::core.clicks[1].active=true;auto c=sp::checkExitInputs();mock::check("direct-check-observes-sender-buttons-and-both-contact-families",c.buttons==127&&c.contacts&&!c.ok);
 reset();s=sp::exitInputReleaseState();mock::check("receipt-scope-does-not-claim-driver-or-consumer-delivery",!s.value("driverReceiptProven",true)&&!s.value("downstreamConsumptionProven",true),s);
}
#ifndef EXIT_RELEASE_SIGNALS_ONLY
inline bool withdrawn(){if(!sp::inputTeardown||sp::initialized||sp::enabled||sp::nativeMode||sp::available||sp::psAvailable||sp::controlTargetEnabled||sp::controlEdgeEnabled||sp::screenButtonMask)return false;for(const auto& [h,w]:mock::windows){(void)h;if(w.live&&(w.visible||w.enabled))return false;}return true;}
inline void seedAll(){reset();sp::inputTeardown=false;sp::initialized=true;sp::enabled=sp::nativeMode=sp::available=sp::psAvailable=sp::controlTargetEnabled=sp::controlEdgeEnabled=true;sp::screenButtonMask=127;
 for(int i=0;i<2;++i){sp::windows[i]=mock::window();auto child=mock::window(sp::windows[i]);sp::nativeInputSurfaces[i].seed(sp::windows[i],child);sp::outlines[i]=mock::window();mock::windows[sp::outlines[i]].enabled=false;}
 for(int i=0;i<7;++i){sp::buttonWindows[i]=mock::window();auto child=mock::window(sp::buttonWindows[i]);sp::nativeButtonSurfaces[i].seed(sp::buttonWindows[i],child);sp::buttonOutlines[i]=mock::window();mock::windows[sp::buttonOutlines[i]].enabled=false;}
}
inline void surfaces(){using ymcc::screeninput::Surface;
 for(int which=0;which<4;++which){reset();Surface s;auto root=mock::window(),child=mock::window(root),other=mock::window();s.seed(root,child);auto descendant=mock::window(child);mock::capture=which==0?root:which==1?child:which==2?other:descendant;auto originalCapture=mock::capture;unsigned blocked=0;mock::mutation=[&](HWND){if(!s.ensureMousePass())++blocked;};auto generation=s.generation_;bool ok=s.quiesce();mock::check("surface-quiesce-latches-before-native-notifications-"+std::to_string(which),ok&&s.quiesced_&&s.generation_==generation+1&&blocked>=4&&!IsWindowVisible(root)&&!IsWindowEnabled(root)&&!IsWindowVisible(child)&&!IsWindowEnabled(child)&&!s.ensureMousePass(true)&&!s.policyEnabled()&&!s.active());mock::check("capture-is-exact-root-child-only-"+std::to_string(which),mock::releaseCaptureCalls==(which<2?1:0)&&mock::capture==(which<2?nullptr:originalCapture));mock::mutation={};auto eventCount=mock::events.size();s.quiesce();mock::check("repeated-quiesce-does-not-advance-generation-"+std::to_string(which),s.generation_==generation+1&&mock::events.size()>=eventCount);s.close();}
 {reset();Surface s;auto root=mock::window(),child=mock::window(root);s.seed(root,child);mock::capture=child;auto fields=s.generation_;auto e=mock::events.size();++mock::thread;bool q=s.quiesce();s.close();s.cancel();bool pass=s.ensureMousePass(true);mock::check("wrong-thread-quiesce-close-cancel-have-no-state-or-API-side-effects",!q&&!pass&&s.error==RPC_E_WRONG_THREAD&&!s.quiesced_&&!s.closing_&&s.root()==root&&s.child()==child&&s.generation_==fields&&s.hasContacts()&&mock::capture==child&&mock::events.size()==e);--mock::thread;s.close();}
 for(unsigned fault:{0u,1u,2u,4u,8u,16u,32u,64u,127u}){reset();Surface s;auto root=mock::window(),child=mock::window(root);s.seed(root,child);s.quiesce();mock::faults=fault;s.close();bool closes=true;for(auto op:{"revoke0","revoke1","revoke2","revoke3","pointerRelease","controllerClose","targetClose"})closes=closes&&mock::ops[op]==(std::string(op)=="pointerRelease"&&(fault&16)?2:1);mock::check("close-continues-through-exception-mask-"+std::to_string(fault),closes&&!mock::windows[child].live&&!s.root()&&!s.child()&&!s.hasContacts());if(fault&112)mock::check("terminal-failure-survives-member-nulling-"+std::to_string(fault),!s.terminalReleased());else if(!fault)mock::check("clean-terminal-close-reports-released",s.terminalReleased());snapshots.push_back({{"name","surface-exceptions-"+std::to_string(fault)},{"events",mock::events},{"terminalReleased",s.terminalReleased()}});}
 {reset();Surface s;auto root=mock::window(),child=mock::window(root);s.seed(root,child);s.quiesce();mock::reenter=true;mock::recursive=[&](){s.close();};s.close();mock::recursive={};mock::reenter=false;mock::check("recursive-close-from-cancel-revocation-and-Close-is-fenced",mock::ops["cancelSink"]==1&&mock::ops["controllerClose"]==1&&mock::ops["targetClose"]==1&&mock::ops["pointerRelease"]==1&&s.terminalReleased());}
 {reset();Surface s;auto root=mock::window(),child=mock::window(root),monitor=mock::window();s.seed(root,child);s.close();bool hid=false;for(const auto& event:mock::events)hid=hid||event["op"]=="hide"||event["op"]=="disable";mock::check("ordinary-close-keeps-root-and-monitor-visible-without-terminal-quiesce",!hid&&mock::windows[root].live&&mock::windows[root].visible&&mock::windows[root].enabled&&mock::windows[monitor].visible&&mock::ops["pointerRelease"]==1);}
 {reset();Surface s;auto root=mock::window(),child=mock::window(root);s.seed(root,child);s.source_.captured=false;s.close();mock::check("ordinary-close-without-owned-capture-skips-pointer-release",mock::ops["pointerRelease"]==0&&mock::windows[root].visible&&mock::windows[root].enabled);}
 {seedAll();bool allBefore=true;unsigned boundaries=0;mock::watchWithdrawal=true;mock::withdrawal=[&](){++boundaries;allBefore=allBefore&&withdrawn();};sp::shutdown();mock::watchWithdrawal=false;auto s=capture("two-pads-seven-buttons-shutdown");mock::check("all-two-pads-seven-buttons-withdraw-before-any-cancel-or-Close",allBefore&&boundaries>=9*4,s);mock::check("shutdown-records-original-root-child-and-decoration-HWNDs",sp::exitInputWindowCount==27&&s.value("verified",false),s);}
 {seedAll();auto original=sp::windows[0];mock::windows[original].failures=-1;sp::shutdown();auto s=capture("shutdown-permanent-root-destroy-failure");mock::check("shutdown-null-owner-array-does-not-hide-live-original-window",!sp::windows[0]&&mock::windows[original].live&&mock::windows[original].attempts>1&&!s.value("verified",true),s);}
 for(bool buttons:{false,true}){seedAll();auto& chosen=buttons?sp::nativeButtonSurfaces[3]:sp::nativeInputSurfaces[1];chosen.quiesce();mock::faults=16;chosen.close();mock::faults=0;sp::shutdown();auto s=capture(buttons?"button-terminal-source-failure":"pad-terminal-source-failure");mock::check(buttons?"buttonsOpenInputSources-retains-one-terminal-failure":"checkExitInputs-retains-one-terminal-failure",!s.value("verified",true)&&sp::exitInputCheckCount>0&&sp::exitInputChecks[sp::exitInputCheckCount-1].openSources==1&&sp::buttonsOpenInputSources()==(buttons?1u:0u),s);}
}
#endif
}
int main(int argc,char** argv){if(argc!=2)return 2;try{tests::signals();
#ifndef EXIT_RELEASE_SIGNALS_ONLY
 tests::surfaces();
#endif
 unsigned failed=0;for(const auto& c:mock::cases)if(!c["ok"].get<bool>())++failed;
 Json result={{"ok",failed==0},{"checks",mock::cases.size()},{"failed",failed},{"cases",mock::cases},{"replays",tests::snapshots},{"actualOSInput",false},{"realWindowsCreated",false},{"hooksInstalled",false},{"devicesOpened",false},{"productStarted",false},{"scope","SHA-bound production method bodies in an in-memory API host; sender-owned touchpad projection only"}};
 std::ofstream out(argv[1],std::ios::binary);out<<result.dump(2)<<'\n';out.close();if(!out)return 2;std::cout<<"RESULT checks="<<mock::cases.size()<<" failed="<<failed<<'\n';return failed?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}catch(...){return 2;}}