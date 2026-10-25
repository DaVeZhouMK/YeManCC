// Pure memory fixture. No Windows SDK headers, product executable, keyboard or mouse APIs.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
using UINT=unsigned;using UINT32=std::uint32_t;using DWORD=std::uint32_t;using WORD=std::uint16_t;
using ULONG_PTR=std::uintptr_t;using UINT_PTR=std::uintptr_t;using ULONGLONG=std::uint64_t;using HWND=std::uintptr_t;using LONG=std::int32_t;
constexpr int FALSE=0;
constexpr UINT INPUT_MOUSE=0,INPUT_KEYBOARD=1,MAPVK_VK_TO_VSC=0;
constexpr DWORD KEYEVENTF_EXTENDEDKEY=1,KEYEVENTF_KEYUP=2,KEYEVENTF_SCANCODE=8;
constexpr DWORD MOUSEEVENTF_MOVE=1,MOUSEEVENTF_LEFTDOWN=2,MOUSEEVENTF_LEFTUP=4,ERROR_ACCESS_DENIED=5;
constexpr WORD VK_UP=0x26,VK_DOWN=0x28,VK_LEFT=0x25,VK_RIGHT=0x27;
struct KEYBDINPUT {WORD wVk,wScan;DWORD dwFlags,time;ULONG_PTR dwExtraInfo;};
struct MOUSEINPUT {LONG dx,dy;DWORD mouseData,dwFlags,time;ULONG_PTR dwExtraInfo;};
struct INPUT {DWORD type;union {MOUSEINPUT mi;KEYBDINPUT ki;};};using LPINPUT=INPUT*;
namespace model {
struct Return {UINT inserted;DWORD error;};
struct Call {UINT requested,inserted;std::vector<INPUT> events;};
struct Evidence {UINT requested=0,inserted=0;DWORD error=0;bool simulated=false;std::array<INPUT,12> events{};};
inline std::deque<Return> returns;
inline std::vector<Call> calls;
inline std::array<Evidence,8> evidence{};
inline unsigned evidenceCount=0,keys=0,unknownEvents=0,unownedUps=0,repeatedDowns=0;
inline bool left=false;
inline long long dx=0,dy=0;
inline DWORD lastError=0;
inline unsigned checks=0,failed=0;
inline unsigned long long normalCases=0,faultCases=0;
inline unsigned keyBit(WORD code){constexpr WORD vk[8]={'W','S','A','D',VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT};for(unsigned i=0;i<8;++i)if(code==vk[i])return 1u<<i;return 0;}
inline void apply(const INPUT& in){if(in.type==INPUT_KEYBOARD){const auto bit=keyBit(in.ki.wScan);if(!bit||!(in.ki.dwFlags&KEYEVENTF_SCANCODE)||in.ki.wVk)++unknownEvents;if(in.ki.dwFlags&KEYEVENTF_KEYUP){if(!(keys&bit))++unownedUps;keys&=~bit;}else{if(keys&bit)++repeatedDowns;keys|=bit;}}else if(in.type==INPUT_MOUSE){if(in.mi.mouseData||(in.mi.dwFlags&~(MOUSEEVENTF_MOVE|MOUSEEVENTF_LEFTDOWN|MOUSEEVENTF_LEFTUP)))++unknownEvents;if(in.mi.dwFlags&MOUSEEVENTF_LEFTDOWN){if(left)++repeatedDowns;left=true;}if(in.mi.dwFlags&MOUSEEVENTF_LEFTUP){if(!left)++unownedUps;left=false;}if(in.mi.dwFlags&MOUSEEVENTF_MOVE){dx+=in.mi.dx;dy+=in.mi.dy;}}else ++unknownEvents;}
inline bool same(const INPUT& a,const INPUT& b){if(a.type!=b.type)return false;if(a.type==INPUT_KEYBOARD)return a.ki.wVk==b.ki.wVk&&a.ki.wScan==b.ki.wScan&&a.ki.dwFlags==b.ki.dwFlags&&a.ki.time==b.ki.time&&a.ki.dwExtraInfo==b.ki.dwExtraInfo;return a.mi.dx==b.mi.dx&&a.mi.dy==b.mi.dy&&a.mi.mouseData==b.mi.mouseData&&a.mi.dwFlags==b.mi.dwFlags&&a.mi.time==b.mi.time&&a.mi.dwExtraInfo==b.mi.dwExtraInfo;}
inline bool same(const std::vector<Call>& a,const std::vector<Call>& b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i){if(a[i].requested!=b[i].requested||a[i].inserted!=b[i].inserted||a[i].events.size()!=b[i].events.size())return false;for(std::size_t j=0;j<a[i].events.size();++j)if(!same(a[i].events[j],b[i].events[j]))return false;}return true;}
inline void clear(unsigned heldKeys=0,bool heldLeft=false){returns.clear();calls.clear();evidenceCount=0;keys=heldKeys;left=heldLeft;unknownEvents=0;unownedUps=repeatedDowns=0;lastError=0;dx=dy=0;}
inline void check(const char* name,bool ok){++checks;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';}
}
// Local sender applies only the accepted prefix to memory and exposes a scripted count.
inline UINT SendInput(UINT n,LPINPUT input,int size){if(size!=sizeof(INPUT)){++model::unknownEvents;return 0;}model::Return next{n,0};if(!model::returns.empty()){next=model::returns.front();model::returns.pop_front();}next.inserted=std::min(next.inserted,n);model::lastError=next.error;model::calls.push_back({n,next.inserted,std::vector<INPUT>(input,input+n)});for(UINT i=0;i<next.inserted;++i)model::apply(input[i]);return next.inserted;}
inline UINT MapVirtualKeyW(WORD vk,UINT){return vk;} // stable scan-code surrogate; not a layout test
inline DWORD GetLastError(){return model::lastError;}
inline void SetLastError(DWORD value){model::lastError=value;}
inline void InvalidateRect(HWND,const void*,int){}
namespace ymcc::touchhaptic {struct Motion {void begin(float,float,int,int,ULONGLONG){}bool move(float,float,ULONGLONG){return false;}};}
#include "screen_touchpad_partial_send_extracted.inc"
namespace sp=ymcc::screenpads;
// Deliberately changes last-error: output must restore it after this observer.
static void observe(const INPUT* inputs,UINT requested,UINT inserted,DWORD error,bool simulated) noexcept {
    if(model::evidenceCount<model::evidence.size()){
        auto& e=model::evidence[model::evidenceCount++];e.requested=requested;e.inserted=inserted;e.error=error;e.simulated=simulated;
        for(UINT i=0;i<std::min(requested,UINT(e.events.size()));++i)e.events[i]=inputs[i];
    }
    SetLastError(0xdead);
}
static void reset(unsigned keys=0,bool left=false){model::clear(keys,left);sp::core={};sp::sentKeys=keys;sp::sentClick=left;sp::dryRun=false;sp::inputError=0;sp::injectedCount=0;sp::outputEvidenceObserver=observe;}
static bool ownedMatches(){return sp::sentKeys==model::keys&&sp::sentClick==model::left&&model::unknownEvents==0;}
static void config(sp::Mode mode){sp::core.config.enabled=true;sp::core.config.left=mode;sp::core.config.right=sp::Mode::Off;sp::core.config.touchFeedback=sp::TouchFeedbackMode::Off;}
static sp::Action pointer(int phase,ULONGLONG time,UINT32 id=7,float x=20,float y=20){return sp::core.event(0,id,phase,x,y,200,200,time);}
static bool keyEvent(const INPUT& in,WORD key,bool up){return in.type==INPUT_KEYBOARD&&in.ki.wScan==key&&bool(in.ki.dwFlags&KEYEVENTF_KEYUP)==up;}
static bool mouseEvent(const INPUT& in,DWORD flags){return in.type==INPUT_MOUSE&&in.mi.dwFlags==flags;}
int main(){using model::check;
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));sp::output(pointer(2,200));check("normal tap: original single DOWN/UP batch, no added sends",model::calls.size()==1&&model::calls[0].requested==2&&mouseEvent(model::calls[0].events[0],MOUSEEVENTF_LEFTDOWN)&&mouseEvent(model::calls[0].events[1],MOUSEEVENTF_LEFTUP)&&ownedMatches()&&!sp::sentClick&&sp::injectedCount==2&&sp::inputError==0);
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));model::returns={{1,0}};sp::output(pointer(2,200));check("partial tap: immediately compensate only owned LEFTUP",model::calls.size()==2&&model::calls[1].requested==1&&mouseEvent(model::calls[1].events[0],MOUSEEVENTF_LEFTUP)&&!sp::sentClick&&ownedMatches()&&sp::injectedCount==2&&sp::inputError==5);
check("observer sees primary and compensation with raw zero error",model::evidenceCount==2&&model::evidence[0].requested==2&&model::evidence[0].inserted==1&&model::evidence[0].error==0&&!model::evidence[0].simulated&&model::evidence[1].requested==1&&model::evidence[1].inserted==1&&GetLastError()==0);
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));model::returns={{1,123},{0,456}};sp::output(pointer(2,200));check("partial tap + failed compensation: retain LEFTDOWN ownership",sp::sentClick&&model::left&&model::calls.size()==2&&sp::injectedCount==1&&sp::inputError==123);
check("observer does not clobber last error and distinguishes both failures",GetLastError()==456&&model::evidenceCount==2&&model::evidence[0].error==123&&model::evidence[1].error==456);
model::returns={{0,87},{0,88}};sp::release();check("cleanup still fails: at most two tries and responsibility survives",sp::sentClick&&model::left&&model::calls.size()==4&&sp::inputError==87&&sp::injectedCount==1&&GetLastError()==88);
sp::release();check("later existing release succeeds and clears only accepted UP",!sp::sentClick&&!model::left&&model::calls.size()==5&&sp::injectedCount==2&&sp::inputError==0);
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));model::returns={{0,5}};sp::output(pointer(2,200));const auto zeroTapCalls=model::calls.size();sp::release();check("zero inserted tap creates neither ownership nor compensating UP",!sp::sentClick&&ownedMatches()&&zeroTapCalls==1&&model::calls.size()==zeroTapCalls&&sp::injectedCount==0);
reset();config(sp::Mode::Wasd);model::returns={{1,5}};sp::output(pointer(0,100));check("partial W+A registers only W, no unsolicited compensation",sp::sentKeys==1&&ownedMatches()&&model::calls.size()==1&&sp::injectedCount==1);sp::output(pointer(1,200));check("same requested direction inserts missing A without repeating W",sp::sentKeys==5&&ownedMatches()&&model::calls.size()==2&&model::calls.back().requested==1&&keyEvent(model::calls.back().events[0],'A',false));sp::output(pointer(2,300));check("normal directional UP after convergence releases both",sp::sentKeys==0&&ownedMatches()&&model::calls.back().requested==2);
reset();config(sp::Mode::Wasd);model::returns={{0,5}};sp::output(pointer(0,100));sp::release();check("zero inserted keyboard batch cannot manufacture W/A UP",sp::sentKeys==0&&ownedMatches()&&model::calls.size()==1&&sp::injectedCount==0);
reset(5);model::returns={{1,5}};sp::release();check("partial cleanup compensates only A UP; already accepted W UP not repeated",sp::sentKeys==0&&ownedMatches()&&model::calls.size()==2&&model::calls[1].requested==1&&keyEvent(model::calls[1].events[0],'A',true));
reset(5);model::returns={{1,5},{0,6}};sp::release();check("partial cleanup and failed retry retain only remaining A",sp::sentKeys==4&&ownedMatches()&&model::calls.size()==2);sp::release();check("later cleanup emits only outstanding A UP",sp::sentKeys==0&&ownedMatches()&&model::calls.size()==3&&keyEvent(model::calls.back().events[0],'A',true));
reset(5);model::returns={{0,0},{0,0}};sp::release();check("zero cleanup + zero retry retain original directional ownership",sp::sentKeys==5&&ownedMatches()&&model::calls.size()==2);sp::ownerOnSuspend();check("existing owner suspend releases retained keys without new timer",sp::sentKeys==0&&ownedMatches());
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));model::returns={{1,5},{0,6}};sp::output(pointer(2,200));const auto epoch=sp::ownerLifecycleEpoch;sp::ownerOnSuspend();check("actual extracted suspend reaches release for pending tap",!sp::sentClick&&ownedMatches()&&sp::ownerLifecycleEpoch==epoch+1);
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));model::returns={{1,5},{0,6}};sp::output(pointer(2,200));model::returns={{0,6},{0,6}};sp::output(pointer(0,1000));model::returns={{1,7},{0,8}};sp::output(pointer(1,1500,7,30,20));check("successful MOVE prefix cannot hide failed outstanding LEFTUP",sp::sentClick&&ownedMatches()&&sp::inputError==7&&model::calls.size()==6&&mouseEvent(model::calls[4].events[0],MOUSEEVENTF_MOVE));sp::release();check("pending mouse state remains recoverable after move failure",!sp::sentClick&&ownedMatches());
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));sp::output(pointer(1,1100));sp::output(pointer(2,2100));check("mouse long hold still sends no click or modifier",model::calls.empty()&&ownedMatches());
reset();config(sp::Mode::Wasd);sp::output(pointer(0,100));const auto heldCalls=model::calls.size();sp::output(pointer(1,1100));sp::output(pointer(2,1200,99));check("stationary hold and foreign UP do not repeat or release keys",model::calls.size()==heldCalls&&sp::sentKeys==5&&ownedMatches());sp::output(pointer(2,2100));check("matching long-hold UP still clears directional keys",sp::sentKeys==0&&ownedMatches());
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));sp::output(pointer(2,200));const auto tapCalls=model::calls.size();sp::output(pointer(2,201));check("duplicate pointer UP does not duplicate click",model::calls.size()==tapCalls&&ownedMatches());
reset();config(sp::Mode::Mouse);sp::output(pointer(0,100));model::returns={{0,9}};sp::output(pointer(1,500,7,30,20));sp::output(pointer(1,600,7,30,20));check("lost movement is not replayed by compensation or unchanged position",model::calls.size()==1&&model::dx==0&&ownedMatches());
reset();sp::dryRun=true;SetLastError(321);sp::Action simulated;simulated.keys=5;simulated.tap=true;sp::output(simulated);check("dryRun has explicit simulated evidence and never calls sender",model::calls.empty()&&model::evidenceCount==1&&model::evidence[0].simulated&&model::evidence[0].requested==4&&model::evidence[0].inserted==4&&model::evidence[0].error==0&&GetLastError()==321&&sp::sentKeys==5&&!sp::sentClick);
reset();sp::output({});check("no-op output emits neither send nor observer call",model::calls.empty()&&model::evidenceCount==0);
reset();sp::outputEvidenceObserver=nullptr;sp::Action one;one.keys=1;model::returns={{1,432}};sp::output(one);check("optional observer unset leaves ordinary send and last error unchanged",sp::sentKeys==1&&ownedMatches()&&model::evidenceCount==0&&GetLastError()==432&&sp::inputError==0);
reset();model::returns={{1,432}};sp::output(one);check("full send observer reports success without leaking stale error",GetLastError()==432&&model::evidenceCount==1&&model::evidence[0].error==0&&!model::evidence[0].simulated);
// Healthy transition matrix includes every initial/desired directional mask and
// all oldLeft/heldClick/tap combinations. Baseline is separately extracted old source.
bool healthy=true,baselineSame=true;
for(unsigned old=0;old<256;++old)for(unsigned desired=0;desired<256;++desired)for(unsigned mouse=0;mouse<8;++mouse){
 reset(old,(mouse&1)!=0);sp::outputEvidenceObserver=nullptr;sp::Action a;a.keys=desired;a.heldClick=(mouse&2)!=0;a.tap=(mouse&4)!=0;a.dx=((old^desired)&1)?3:0;a.dy=a.dx?-2:0;sp::output(a);++model::normalCases;
 healthy=healthy&&ownedMatches()&&sp::sentKeys==desired&&sp::sentClick==a.heldClick&&model::calls.size()<=1&&sp::inputError==0;
#ifdef PARTIAL_SEND_HAS_BASELINE
 const auto actual=model::calls;const auto count=sp::injectedCount;model::clear(old,(mouse&1)!=0);baseline::sentKeys=old;baseline::sentClick=(mouse&1)!=0;baseline::dryRun=false;baseline::inputError=0;baseline::injectedCount=0;baseline::output(a);
 baselineSame=baselineSame&&model::same(actual,model::calls)&&baseline::sentKeys==sp::sentKeys&&baseline::sentClick==sp::sentClick&&baseline::injectedCount==count&&baseline::inputError==0;
#endif
}
check("524288 healthy transitions retain one-batch behavior and state",healthy);
#ifdef PARTIAL_SEND_HAS_BASELINE
check("healthy batches exactly match extracted original order/fields/count",baselineSame);
#else
(void)baselineSame;
#endif
// Fault matrix covers initial state, mixed UP/DOWN direction transitions,
// mouse movement, tap/held changes, every insertion prefix and failed cleanup.
bool prefixes=true,bounded=true,converged=true,noOtherSignals=true,observed=true,releaseOnly=true,paired=true;
constexpr unsigned desiredMasks[]={0,1,2,4,8,15,16,128,255};
for(unsigned old=0;old<256;++old)for(unsigned desired:desiredMasks)for(unsigned mouse=0;mouse<8;++mouse){
 unsigned differences=old^desired,n=0;for(unsigned b=differences;b;b>>=1)n+=b&1;
 const bool oldLeft=(mouse&1)!=0,held=(mouse&2)!=0,tap=(mouse&4)!=0;const bool move=((old^desired)&1)!=0;
 n+=move?1u:0u;n+=oldLeft!=held?1u:0u;n+=tap&&!held&&!oldLeft?2u:0u;
 for(unsigned prefix=0;prefix<=n;++prefix)for(unsigned failRecovery=0;failRecovery<2;++failRecovery){
  reset(old,oldLeft);sp::Action a;a.keys=desired;a.heldClick=held;a.tap=tap;a.dx=move?3:0;a.dy=move?-2:0;
  model::returns={{prefix,87}};if(failRecovery)model::returns.push_back({0,88});sp::output(a);++model::faultCases;
  prefixes=prefixes&&ownedMatches();bounded=bounded&&model::calls.size()<=2;noOtherSignals=noOtherSignals&&model::unknownEvents==0;
  observed=observed&&model::calls.size()==model::evidenceCount;
  if(model::calls.size()==2)for(const auto& e:model::calls[1].events)releaseOnly=releaseOnly&&(e.type==INPUT_KEYBOARD?bool(e.ki.dwFlags&KEYEVENTF_KEYUP):e.type==INPUT_MOUSE&&e.mi.dwFlags==MOUSEEVENTF_LEFTUP);
  for(unsigned i=0;i<model::evidenceCount;++i){const auto& e=model::evidence[i];const auto& c=model::calls[i];observed=observed&&e.requested==c.requested&&e.inserted==c.inserted&&!e.simulated;for(UINT j=0;j<c.requested;++j)observed=observed&&model::same(e.events[j],c.events[j]);}
  model::returns.clear();a.tap=false;a.dx=a.dy=0;sp::output(a);converged=converged&&ownedMatches()&&sp::sentKeys==desired&&sp::sentClick==held;
  sp::release();converged=converged&&ownedMatches()&&sp::sentKeys==0&&!sp::sentClick;paired=paired&&model::unownedUps==0&&model::repeatedDowns==0;
 }
}
check("every count prefix owns exactly inserted keys/buttons, including tap DOWN",prefixes);
check("all failure paths are bounded to primary plus one release-only batch",bounded);
check("subsequent input/release converges after failures without unowned UP",converged);
check("all real-memory sender calls have matching unsimulated observer evidence",observed);
check("fault matrix emits only fixed non-modifier keys and left/move mouse events",noOtherSignals);
check("compensation contains only UP, never movement/tap/DOWN replay",releaseOnly);
check("no accepted unowned UP or repeated DOWN across failure/convergence/cleanup",paired);
std::cout<<"SUMMARY {\"checks\":"<<model::checks<<",\"failed\":"<<model::failed<<",\"normalTransitions\":"<<model::normalCases<<",\"faultScenarios\":"<<model::faultCases<<",\"actualOSInput\":false,\"fieldRootCause\":\"unknown\"}\n";
return model::failed?1:0;
}
