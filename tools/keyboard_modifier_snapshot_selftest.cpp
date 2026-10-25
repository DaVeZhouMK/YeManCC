#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
static DWORD fakeError=777;static unsigned asyncCalls=0,queueCalls=0,foregroundCalls=0;
static bool queueOk=true,foregroundChanges=false;static DWORD queueFailure=5;
static std::array<SHORT,256> asyncStates{};static std::array<BYTE,256> queueStates{};
static DWORD fakeGetLastError(){return fakeError;}
static void fakeSetLastError(DWORD v){fakeError=v;}
static ULONGLONG fakeTick(){fakeError=101;return 1000+foregroundCalls;}
static DWORD fakeThread(){fakeError=102;return 42;}
static HWND fakeForeground(){++foregroundCalls;fakeError=103;return reinterpret_cast<HWND>(std::uintptr_t(foregroundChanges&&foregroundCalls==2?2:1));}
static DWORD fakePid(HWND h,DWORD* pid){*pid=h==reinterpret_cast<HWND>(1)?123:456;fakeError=104;return 55;}
static SHORT fakeAsync(int key){++asyncCalls;fakeError=105;return asyncStates[key];}
static BOOL fakeQueue(PBYTE out){++queueCalls;if(!queueOk){fakeError=queueFailure;return FALSE;}for(unsigned i=0;i<256;++i)out[i]=queueStates[i];fakeError=106;return TRUE;}
#define GetLastError fakeGetLastError
#define SetLastError fakeSetLastError
#define GetTickCount64 fakeTick
#define GetCurrentThreadId fakeThread
#define GetForegroundWindow fakeForeground
#define GetWindowThreadProcessId fakePid
#define GetAsyncKeyState fakeAsync
#define GetKeyboardState fakeQueue
#include "../native/keyboard_modifier_snapshot.h"
static unsigned checks=0,cases=0;
static void check(bool v,const char* label){++checks;if(!v){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
static void reset(){fakeError=777;asyncCalls=queueCalls=foregroundCalls=0;queueOk=true;foregroundChanges=false;asyncStates.fill(0);queueStates.fill(0);}
int main(){
 using namespace ymcc::keyboardEvidence;
 reset();auto s=read();auto j=toJson(s);++cases;
 check(asyncCalls==8&&queueCalls==1,"bounded existing-tick reads");check(fakeError==777,"Win32 error preserved");
 check(s.asyncDownMask==0&&s.queueReadOk,"all-up samples");check(j["asyncZeroIsAbsenceProof"]==false&&j["eventEdgesCaptured"]==false,"absence and edge limits");
 reset();asyncStates[VK_LCONTROL]=SHORT(0x8001);s=read();j=toJson(s);++cases;
 check(s.asyncDownMask==1&&j["modifiers"]["leftCtrl"]["asyncDownBit"]==true,"left Ctrl down");check(j["modifiers"]["rightCtrl"]["asyncDownBit"]==false,"side distinction");
 reset();asyncStates[VK_RCONTROL]=1;s=read();++cases;check(s.asyncDownMask==0,"low bit ignored");
 reset();asyncStates[VK_LCONTROL]=SHORT(0x8000);queueStates[VK_RCONTROL]=0x80;s=read();j=toJson(s);++cases;
 check(s.asyncDownMask==1&&s.callingThreadQueueDownMask==2,"queue and async not merged");check(j["sourceAttribution"]=="unobserved","no injected/physical inference");
 reset();queueOk=false;s=read();j=toJson(s);++cases;
 check(!s.queueReadOk&&s.queueError==5,"queue failure recorded");check(j["callingThreadQueueDownMask"].is_null()&&j["modifiers"]["leftCtrl"]["callingThreadQueueDown"].is_null(),"unknown queue is not zero");check(fakeError==777,"error preserved on failure");
 reset();foregroundChanges=true;s=read();j=toJson(s);++cases;
 check(j["foregroundStableDuringCapture"]==false&&s.foregroundPidBefore==123&&s.foregroundPidAfter==456,"foreground change preserved");check(j["crossKeyAtomicSnapshot"]==false,"no atomic claim");
 reset();for(const int key:modifierKeys){asyncStates[key]=SHORT(0x8000);queueStates[key]=0x80;}s=read();j=toJson(s);++cases;
 check(s.asyncDownMask==255&&s.callingThreadQueueDownMask==255,"all eight modifiers distinct");check(j["modifiers"].size()==8,"modifier-only schema");check(j["gameOrSteamKeyboardReceipt"]=="unobserved","no consumer inference");
 std::printf("KEYBOARD_MODIFIER_SNAPSHOT_PASS cases=%u checks=%u fakeWindowsOnly=true liveKeysRead=false\n",cases,checks);return 0;
}