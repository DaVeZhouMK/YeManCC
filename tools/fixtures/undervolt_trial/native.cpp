#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;
enum class YmccFamily {Unknown,AsusRogAlly,AsusRogAllyX,AsusRogAllyClassic,AsusRogAllyXClassic};
struct {YmccFamily family=YmccFamily::Unknown;std::string deviceClass;} g_machineIdentity;
static HWND g_hwnd=nullptr;static bool realTimer=false,timerOn=false,writeOk=true,compatible=true;static ULONGLONG fakeNow=0;static int writes=0,checks=0;static json disk;
struct SettingsFileGuard{};static std::wstring YM_SETTINGS_FILE=L"memory-only";
static std::string ymSettingsReadRaw(const std::wstring&){return disk.dump();}
static json ymSettingsReadUnlocked(){return disk;}static json ymSettingsRead(){return disk;}
static bool ymSettingsHasCompatibleWriteBase(const std::string&,const json&){return compatible;}
static bool ymSettingsWriteDocumentUnlocked(const std::string& next,void*,const json*){if(!writeOk)return false;disk=json::parse(next);++writes;return true;}
static void ipc_emit(const std::string&,const json&){}
static ULONGLONG testNow(){return realTimer?::GetTickCount64():fakeNow;}
static UINT_PTR testSetTimer(HWND h,UINT_PTR id,UINT ms,TIMERPROC proc){timerOn=true;return realTimer?::SetTimer(h,id,ms,proc):id;}
static BOOL testKillTimer(HWND h,UINT_PTR id){timerOn=false;return realTimer?::KillTimer(h,id):TRUE;}
#define GetTickCount64 testNow
#define SetTimer testSetTimer
#define KillTimer testKillTimer
#include "cpu_undervolt_trial_native.h"
#undef GetTickCount64
#undef SetTimer
#undef KillTimer
static void check(const char* name,bool pass){++checks;std::cout<<name<<"="<<(pass?"PASS":"FAIL")<<std::endl;if(!pass)throw std::runtime_error(name);}
static json pref(int value=-5,bool enabled=true){return {{"schema",1},{"enabled",enabled},{"value",value},{"vendor","amd"},{"unit","co_step"},{"identity",{{"vendor","amd"},{"family","fixture"}}}};}
static json request(const json& p,unsigned long long token){return {{"token",token},{"preference",p},{"receipt",{{"accepted",true},{"vendor","amd"},{"unit","co_step"},{"requested",{{"value",p["enabled"].get<bool>()?p["value"].get<int>():0}}}}}};}
static void reset(){cpuUvTrialStop("reset");g_cpuUvTrial=CpuUvTrialState{};fakeNow=0;writes=0;writeOk=true;compatible=true;g_machineIdentity.family=YmccFamily::Unknown;disk={{"schemaVersion",1},{"cpu",{{"otherSetting",44}}},{"input",{{"unchanged",true}}}};}
static void arm(const json& p){auto b=cpuUvTrialBegin();check("arm-accepted",cpuUvTrialArm(request(p,b["token"]))["pending"]==true);}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_TIMER&&w==CPU_UV_TRIAL_TIMER){cpuUvTrialTick();return 0;}return DefWindowProcW(h,m,w,l);}
int main(int argc,char** argv){try{
 if(argc>1&&std::string(argv[1])=="--background"){
  realTimer=true;WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCCUvIsolatedTimer";RegisterClassW(&wc);g_hwnd=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
  reset();arm(pref());const auto start=::GetTickCount64();bool earlyWrite=false;
  while(::GetTickCount64()-start<34000&&writes==0){MsgWaitForMultipleObjects(0,nullptr,FALSE,100,QS_ALLINPUT);MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(writes&&::GetTickCount64()-start<30000)earlyWrite=true;}
  check("hidden-native-window-saves-after-30s-without-renderer",writes==1&&!earlyWrite&&!g_cpuUvTrial.pending);check("native-timer-retired-after-save",!timerOn);DestroyWindow(g_hwnd);return 0;
 }
 reset();arm(pref());fakeNow=29999;cpuUvTrialTick();check("no-early-persistence",writes==0&&g_cpuUvTrial.pending);disk["cpu"]["otherSetting"]=33;fakeNow=30000;cpuUvTrialTick();check("one-save-at-30s",writes==1&&g_cpuUvTrial.stage=="saved");check("unrelated-concurrent-data-preserved",disk["cpu"]["otherSetting"]==33&&disk["input"]["unchanged"]==true);cpuUvTrialTick();check("no-duplicate-save",writes==1);
 for(const auto reason:{"exit","interrupted","suspended"}){reset();arm(pref());fakeNow=29000;cpuUvTrialStop(reason);fakeNow=40000;cpuUvTrialTick();check(reason,writes==0&&!timerOn);}
 reset();auto first=cpuUvTrialBegin(),second=cpuUvTrialBegin();check("old-result-cannot-arm-new-trial",cpuUvTrialArm(request(pref(),first["token"]))["ok"]==false);check("new-token-still-works",cpuUvTrialArm(request(pref(),second["token"]))["pending"]==true);
 reset();arm(pref());fakeNow=20000;arm(pref(-8));fakeNow=30000;cpuUvTrialTick();check("edit-restarts-full-30s",writes==0);fakeNow=50000;cpuUvTrialTick();check("latest-only-saved",disk["cpu"]["undervolt"]["value"]==-8&&writes==1);
 reset();arm(pref());disk["cpu"]["undervolt"]=pref(-2);fakeNow=30000;cpuUvTrialTick();check("concurrent-voltage-edit-not-overwritten",writes==0&&g_cpuUvTrial.error=="voltage_settings_changed");
 reset();arm(pref());writeOk=false;fakeNow=30000;cpuUvTrialTick();check("save-failure-no-retry-loop",writes==0&&!timerOn&&g_cpuUvTrial.stage=="failed");
 for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic}){reset();g_machineIdentity.family=family;auto b=cpuUvTrialBegin();check("rog-family-rejected",cpuUvTrialArm(request(pref(),b["token"]))["ok"]==false&&!timerOn);}
 reset();check("unsigned-overflow-preference-rejected",!cpuUvPreferenceValid(json{{"schema",1},{"enabled",true},{"value",18446744073709551615ull},{"vendor","amd"},{"unit","co_step"},{"identity",json::object()}}));auto b=cpuUvTrialBegin();check("positive-voltage-rejected",cpuUvTrialArm(request(pref(1),b["token"]))["ok"]==false);auto bad=request(pref(),b["token"]);bad["receipt"]["requested"]["value"]=-8;check("mismatched-receipt-rejected",cpuUvTrialArm(bad)["ok"]==false);bad=request(pref(),b["token"]);bad["receipt"]["accepted"]=false;check("unaccepted-write-not-saved",cpuUvTrialArm(bad)["ok"]==false);
 reset();arm(pref(-5,false));fakeNow=30000;cpuUvTrialTick();check("disabled-state-remembers-offset-after-zero-write",disk["cpu"]["undervolt"]["enabled"]==false&&disk["cpu"]["undervolt"]["value"]==-5);
 std::cout<<"checks="<<checks<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
