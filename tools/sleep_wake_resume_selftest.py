"""Compile the production wake recovery header with deterministic OS boundaries.
Never enumerate/resume live software or trigger system sleep. Real OS probing is
covered by the independently owned-process companion smoke test.
"""
from pathlib import Path
import argparse, subprocess, re
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/SleepResumeRetry-20261003'))
a = parser.parse_args(); out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
r = Path.cwd(); source = (r/'native/main.cpp').read_text('utf-8-sig')
h = (r/'native/wake_game_recovery.h').read_text('utf-8')
assert 'sgBeginWakeGameRecovery(expectedGeneration);' in source
wake = source[source.index('static void sgRealWake('):source.index('static SgSleepMode sgPowerButtonSleepMode()')]
user = wake[wake.index('if (strcmp(src, "resume_suspend") == 0)'):wake.index('if (!hadSleepCycle)')]
assert 'sgResumeSleepTarget(' not in user and 'sgBeginWakeGameRecovery' in user
assert not re.search(r'\bSleep\s*\(', h) and 'wait_for' not in h
assert source.index('sgPrepareWakeRecoveryForPauseUnlocked(rootPid)') < source.index('const std::vector<DWORD> targets{rootPid};')
assert 'sgCancelWakeGameRecoveryUnlocked("application-exit")' in source
assert h.count('nativeMonitorExcluded(') == 3 and h.count('sgNameExcludedBy(') == 3
cpp = r"""
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <functional>
#include "json.hpp"
#include "wake_game_resume.h"
using json=nlohmann::json; namespace fspath=std::filesystem;
enum class PowerLifecycle { Ready, Suspending, Suspended, Resuming };
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
std::atomic<bool> g_inputReady{true},g_exitRequested{false},g_sgWakeRecoveryPending{false},g_sgInSuspend{true},g_sgGameActuallySuspended{true};
std::atomic<DWORD> g_manualPausedPid{0};
std::atomic<unsigned long long> g_sgExplicitGameWakeGeneration{0},g_sgExplicitGameWakePauseFence{0};
std::mutex g_sgOpMtx,g_gameRulesMx;
std::condition_variable g_sgWorkCv;
bool g_guardEnabled=true,g_sgPauseResume=true,retry=false;
ULONGLONG generation=7,tick=0;
std::atomic<bool> g_sleepRecoveryAutoEnabled{false},g_sleepRecoveryAutoPending{false};
std::atomic<ULONGLONG> g_sleepRecoveryAutoConfigEpoch{1};
bool sleepRecoveryAutoAllows(ULONGLONG gen,ULONGLONG epoch){return g_sleepRecoveryAutoEnabled&&gen==generation&&epoch==g_sleepRecoveryAutoConfigEpoch&&!g_exitRequested;}
void sleepRecoveryAutoArm(ULONGLONG){}
std::wstring SG_DIR,SG_SLEEP_LEASE_DIR,SG_MANUAL_DIR;
constexpr ULONGLONG SG_MIN_WS=500ULL*1024*1024,SG_CAPTURE_MIN_WS=50ULL*1024*1024;
struct SgSleepTarget{DWORD pid=0;ULONGLONG processCreated=0,powerGeneration=0;bool markerOwned=false;};
SgSleepTarget owner;
struct NativeDetectedGame{DWORD pid=0;ULONGLONG processCreated=0,workingSet=0;std::wstring path;bool whitelisted=false;};
enum class NativeOnePidResult{Ok,Rejected};
struct Proc{ULONGLONG created=1234,ws=600ULL*1024*1024;int depth=1,calls=0,fail=0;bool sticky=false,known=true,exited=false,sameUser=true,sameSession=true,debug=false,whitelist=false;DWORD openError=0;std::wstring name=L"game.exe";unsigned threadTotal=2;int suspendedThreads=-1;};
std::map<DWORD,Proc> procs;
std::vector<DWORD> enumeration;
size_t enumIndex=0;DWORD error=0;
std::vector<std::pair<std::string,json>> facts;int focusCalls=0,emitCalls=0;
std::vector<std::wstring> blacklist;
bool markerWritesAllowed=true;
std::function<void(DWORD)> onThreadQuery,onIdentityQuery;
ULONGLONG currentPowerGeneration(){return generation;}
bool sgSleepRetryActive(){return retry;}
ULONGLONG fakeTick(){return tick;}
DWORD fakeError(){return error;}
HANDLE fakeOpen(DWORD,BOOL,DWORD pid){auto i=procs.find(pid);if(i==procs.end()){error=ERROR_INVALID_PARAMETER;return nullptr;}if(i->second.openError){error=i->second.openError;return nullptr;}return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));}
DWORD pidOf(HANDLE h){return static_cast<DWORD>(reinterpret_cast<uintptr_t>(h));}
BOOL fakeClose(HANDLE){return TRUE;}
DWORD fakeWait(HANDLE h,DWORD){return procs.at(pidOf(h)).exited?WAIT_OBJECT_0:WAIT_TIMEOUT;}
DWORD fakeSelf(){return 999;}
BOOL fakeSession(DWORD pid,DWORD* s){*s=pid==999||procs.at(pid).sameSession?1:2;return TRUE;}
BOOL fakeDebug(HANDLE h,PBOOL v){*v=procs.at(pidOf(h)).debug;return TRUE;}
HANDLE fakeSnapshot(DWORD,DWORD){enumIndex=0;return reinterpret_cast<HANDLE>(123456);}
BOOL fakeFirst(HANDLE,PROCESSENTRY32W* p){if(enumeration.empty())return FALSE;p->th32ProcessID=enumeration[0];wcscpy_s(p->szExeFile,procs.at(enumeration[0]).name.c_str());return TRUE;}
BOOL fakeNext(HANDLE,PROCESSENTRY32W* p){if(++enumIndex>=enumeration.size())return FALSE;p->th32ProcessID=enumeration[enumIndex];wcscpy_s(p->szExeFile,procs.at(enumeration[enumIndex]).name.c_str());return TRUE;}
ULONGLONG nativeProcessCreatedFromHandle(HANDLE h){return procs.at(pidOf(h)).created;}
bool focusQueryProcessIdentity(DWORD pid,void*,ULONGLONG* c){auto hook=std::move(onIdentityQuery);if(hook)hook(pid);if(!procs.count(pid)||procs.at(pid).exited)return false;*c=procs.at(pid).created;return true;}
namespace WakeGameResume {
ThreadState mockQueryThreads(DWORD pid){auto hook=std::move(onThreadQuery);if(hook)hook(pid);if(!procs.count(pid))return {};auto& p=procs.at(pid);return {p.known,p.threadTotal,p.suspendedThreads>=0?static_cast<unsigned>(p.suspendedThreads):p.depth>0?p.threadTotal:0u};}
bool mockSameUser(HANDLE h){return procs.at(pidOf(h)).sameUser;}
}
std::wstring sgBaseName(const std::wstring& p){return fspath::path(p).filename().wstring();}
bool nativeMonitorExcluded(const std::wstring& n){return n==L"browser.exe"||n==L"system.exe";}
bool sgNameExcludedBy(const std::wstring& n,const std::vector<std::wstring>& v){return std::find(v.begin(),v.end(),n)!=v.end();}
std::vector<std::wstring> sgExcludes(){return blacklist;}
std::vector<std::wstring> sgGameWhitelist(){return {};}
std::vector<std::wstring> nativeConfiguredGameExes(){return {};}
NativeOnePidResult nativeScanGameOnePid(DWORD pid,const std::wstring&,const std::vector<std::wstring>&,const std::vector<std::wstring>&,const std::vector<std::wstring>&,NativeDetectedGame* g){auto& p=procs.at(pid);*g={pid,p.created,p.ws,p.name,p.whitelist};return NativeOnePidResult::Ok;}
std::string sgReadFile(const std::wstring& p){std::ifstream f{fspath::path(p)};return {std::istreambuf_iterator<char>(f),{}};}
bool sgEnsureMarkerDir(const std::wstring& d){std::error_code ec;fspath::create_directories(d,ec);return !ec;}
bool sgWriteFileAtomic(const std::wstring& p,const std::string& text){if(!markerWritesAllowed)return false;std::ofstream f{fspath::path(p)};f<<text;return bool(f);}
double sgNowEpoch(){return tick/1000.0;}
const std::string& sgManualPauseRunToken(){static const std::string token="runA";return token;}
ULONGLONG sgMarkerCreated(const std::wstring& d,DWORD pid){auto t=sgReadFile(d+L"\\"+std::to_wstring(pid)+L".txt");auto p=t.find("|created=");if(p==std::string::npos)return 0;return std::stoull(t.substr(p+9));}
unsigned long long sgMarkerGeneration(const std::wstring& d,DWORD pid){auto t=sgReadFile(d+L"\\"+std::to_wstring(pid)+L".txt");auto p=t.find("|generation=");if(p==std::string::npos)return 0;return std::stoull(t.substr(p+12));}
bool sgMarkerIsSuspended(const std::wstring& d,DWORD pid){return sgReadFile(d+L"\\"+std::to_wstring(pid)+L".txt").find("|state=suspended")!=std::string::npos;}
bool sgHasMarkerFiles(const std::wstring& d){for(auto& f:fspath::directory_iterator(d))if(f.is_regular_file())return true;return false;}
bool sgReadSleepTarget(SgSleepTarget& t){t=owner;return owner.pid!=0;}
void sgClearSleepTargetIfMatches(const SgSleepTarget& t){if(owner.pid==t.pid&&owner.processCreated==t.processCreated&&owner.powerGeneration==t.powerGeneration)owner={};}
void sgRecordFact(const char* name,json data=json::object()){facts.push_back({name,data});}
void focusSingleResumedGame(std::vector<DWORD>,ULONGLONG,ULONGLONG){++focusCalls;}
void ipc_emit(const char*,json){++emitCalls;}
void sgInitNt(){}
bool sgDrainProcessSuspendCount(HANDLE h){auto& p=procs.at(pidOf(h));++p.calls;if(p.fail>0){--p.fail;return false;}if(!p.sticky)p.depth=std::max(0,p.depth-8);return true;}
bool fnNtResume=true;
// Only hold a file in the owned fixture tree to force a real remove failure.
HANDLE holdMarker(const std::wstring& path){return CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);}
void releaseMarker(HANDLE h){assert(CloseHandle(h));}
#define GetTickCount64 fakeTick
#define GetLastError fakeError
#define OpenProcess fakeOpen
#define CloseHandle fakeClose
#define WaitForSingleObject fakeWait
#define GetCurrentProcessId fakeSelf
#define ProcessIdToSessionId fakeSession
#define CheckRemoteDebuggerPresent fakeDebug
#define CreateToolhelp32Snapshot fakeSnapshot
#define Process32FirstW fakeFirst
#define Process32NextW fakeNext
#define queryThreads mockQueryThreads
#define sameUser mockSameUser
#include "wake_game_recovery.h"
#undef queryThreads
#undef sameUser
int passed=0;
void pass(const char* n){++passed;std::cout<<"PASS "<<n<<"\n";}
const json& lastFact(const char* name){for(auto i=facts.rbegin();i!=facts.rend();++i)if(i->first==name)return i->second;assert(false);return facts.back().second;}
void reset(){
 for(auto& dir:{SG_SLEEP_LEASE_DIR,SG_MANUAL_DIR})for(auto& f:fspath::directory_iterator(dir)){assert(f.is_regular_file());fspath::remove(f.path());}
 fspath::remove(SG_DIR+L"\\quickapp_suspended.json");
 procs.clear();enumeration.clear();blacklist.clear();facts.clear();focusCalls=emitCalls=0;
 markerWritesAllowed=true;onThreadQuery={};onIdentityQuery={};
 generation=7;tick=0;error=0;retry=false;owner={};g_sgExplicitGameWakeGeneration=0;g_sgExplicitGameWakePauseFence=0;g_sgWakeRecovery={};g_sgLastGameWakeGeneration=0;
 g_sgWakeRecoveryPending=false;g_exitRequested=false;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_sgInSuspend=true;g_sgGameActuallySuspended=true;g_manualPausedPid=0;g_guardEnabled=true;g_sgPauseResume=true;
}
void marker(DWORD pid,bool manual=false){auto& p=procs[pid];sgEnsureMarkerDir(manual?SG_MANUAL_DIR:SG_SLEEP_LEASE_DIR);auto path=(manual?SG_MANUAL_DIR:SG_SLEEP_LEASE_DIR)+L"\\"+std::to_wstring(pid)+L".txt";assert(sgWriteFileAtomic(path,"pid="+std::to_string(pid)+"|created="+std::to_string(p.created)+"|state=suspended|generation=7"));if(!manual)owner={pid,p.created,7,true};}
void at(ULONGLONG ms){tick=ms;sgWakeGameRecoveryTick();}
bool exists(DWORD pid,bool manual=false){return fspath::exists((manual?SG_MANUAL_DIR:SG_SLEEP_LEASE_DIR)+L"\\"+std::to_wstring(pid)+L".txt");}
void window(){for(auto t:WakeGameResume::attemptOffsetsMs)at(t);}
int wmain(int argc,wchar_t** argv){assert(argc==2);SG_DIR=argv[1];assert(fspath::path(SG_DIR).is_absolute());assert(SG_DIR.find(L"SleepResumeRetry-20261003")!=std::wstring::npos);SG_SLEEP_LEASE_DIR=SG_DIR+L"\\sleep";SG_MANUAL_DIR=SG_DIR+L"\\manual";sgEnsureMarkerDir(SG_SLEEP_LEASE_DIR);sgEnsureMarkerDir(SG_MANUAL_DIR);
 reset();marker(100);procs[100].threadTotal=54;sgBeginWakeGameRecovery(7);assert(procs[100].calls==0&&exists(100));at(0);
 assert(procs[100].calls==1&&procs[100].depth==0&&!exists(100)&&!owner.markerOwned&&focusCalls==1&&emitCalls==1&&!g_sgWakeRecoveryPending&&!g_sgInSuspend&&!g_sgGameActuallySuspended);
 assert(lastFact("sleep-game-recovery-queued")["attemptOffsetsMs"]==json({0,2500,4500,7000,10000}));
 assert(lastFact("sleep-game-recovery-confirmed")["basis"]=="current-attempt-zero-suspend-counts");
 assert(lastFact("sleep-game-recovery-attempt")["threads"]==54&&lastFact("sleep-game-recovery-attempt")["suspendedThreads"]==0);
 const auto complete=lastFact("sleep-game-recovery-complete");assert(complete["basis"]=="all-targets-done"&&complete["attempt"]==1&&complete["pendingTargets"]==0&&complete["retainedTargets"]==0&&complete["elapsedMs"]==0&&!complete["final"]&&!complete["healthVerified"]);
 window();assert(procs[100].calls==1&&focusCalls==1&&emitCalls==1);pass("Ready first attempt retires exact confirmed lease and completes at zero offset without later calls");
 reset();marker(100);procs[100].fail=2;sgBeginWakeGameRecovery(7);at(0);assert(procs[100].calls==1&&exists(100)&&g_sgWakeRecovery.nextAttempt==1);at(2499);assert(procs[100].calls==1);at(2500);assert(procs[100].calls==2&&exists(100)&&g_sgWakeRecovery.nextAttempt==2);at(4499);assert(procs[100].calls==2);at(4500);assert(procs[100].calls==3&&procs[100].depth==0&&!exists(100)&&!g_sgWakeRecoveryPending&&lastFact("sleep-game-recovery-complete")["attempt"]==3);window();assert(procs[100].calls==3);pass("first two API failures do not lose state; later attempts recover");
 reset();marker(100);procs[100].sticky=true;sgBeginWakeGameRecovery(7);window();assert(exists(100)&&owner.markerOwned&&procs[100].calls==5&&!g_sgWakeRecoveryPending&&focusCalls==0);pass("API accepted but still suspended: bounded terminal failure retains lease");
 reset();marker(100);procs[100].fail=10;sgBeginWakeGameRecovery(7);window();at(20000);assert(exists(100)&&procs[100].calls==5);pass("all attempts fail: no indefinite loop, identity remains retryable");
 reset();marker(100);procs[100].known=false;sgBeginWakeGameRecovery(7);window();assert(exists(100));pass("unsupported suspend-count query cannot falsely confirm recovery");
 reset();marker(100);marker(101,true);sgWriteFileAtomic(SG_DIR+L"\\quickapp_suspended.json","old-manual-state");g_manualPausedPid=101;sgBeginWakeGameRecovery(7);window();assert(procs[101].calls==1&&!exists(101,true)&&g_manualPausedPid==0&&!fspath::exists(SG_DIR+L"\\quickapp_suspended.json"));pass("pre-wake manual games resume; corresponding frontend state cleaned");
 reset();marker(100);sgBeginWakeGameRecovery(7);marker(101,true);window();assert(procs[101].calls==0&&exists(101,true));pass("new manual PID after wake is outside the immutable snapshot");
 reset();marker(100,true);sgBeginWakeGameRecovery(7);at(1000);sgPrepareWakeRecoveryForPauseUnlocked(100);assert(!exists(100,true));procs[100].depth=1;marker(100,true);at(2500);at(4500);at(7000);at(10000);assert(procs[100].calls==1&&procs[100].depth==1&&exists(100,true));pass("new pause of same PID after successful first attempt is protected");
 reset();marker(100);sgBeginWakeGameRecovery(7);at(1000);generation=8;g_powerLifecycle=PowerLifecycle::Suspending;at(2500);assert(procs[100].calls==1&&!exists(100)&&!g_sgWakeRecoveryPending&&!owner.markerOwned&&!g_sgInSuspend&&!g_sgGameActuallySuspended);pass("new power generation retires confirmed-running old marker and clears stale suspend flags");
 reset();marker(100);sgBeginWakeGameRecovery(7);retry=true;at(1000);assert(procs[100].calls==0&&exists(100)&&g_sgInSuspend&&g_sgGameActuallySuspended);pass("active re-sleep cancels recovery, not a resume trigger, and retains suspend flags");
 reset();marker(100);sgBeginWakeGameRecovery(7);g_exitRequested=true;at(1000);assert(procs[100].calls==0&&exists(100)&&!g_sgWakeRecoveryPending);pass("exit cancels timed work; exit-owned cleanup remains independent");
 reset();marker(100);sgBeginWakeGameRecovery(7);procs[100].created++;window();assert(procs[100].calls==0&&!exists(100));pass("reused PID is never resumed");
 reset();marker(100);sgBeginWakeGameRecovery(7);procs[100].exited=true;window();assert(procs[100].calls==0&&!exists(100));pass("confirmed exit removes only stale original lease");
 reset();marker(100);sgBeginWakeGameRecovery(7);procs[100].openError=ERROR_ACCESS_DENIED;window();assert(exists(100)&&procs[100].calls==0);pass("access denied is not mistaken for exit");
 reset();marker(100);sgBeginWakeGameRecovery(7);g_inputReady=false;window();assert(procs[100].calls==0&&exists(100)&&!g_sgWakeRecoveryPending);pass("input never Ready: 10s timeout retains state, no early thaw");
 reset();marker(100);procs[100].fail=1;sgBeginWakeGameRecovery(7);at(8000);assert(procs[100].calls==1);at(8050);assert(procs[100].calls==1);at(10000);assert(procs[100].calls==2&&!exists(100));pass("overdue stages are skipped instead of an immediate resume burst");
 reset();marker(100);procs[100].known=false;sgBeginWakeGameRecovery(7);at(0);assert(procs[100].calls==1&&exists(100));procs[100].known=true;procs[100].fail=1;at(10000);assert(exists(100)&&owner.markerOwned&&procs[100].calls==2&&!g_sgWakeRecoveryPending&&lastFact("sleep-game-recovery-complete")["pendingTargets"]==1);pass("a final failure cannot borrow success from an earlier attempt even with zero counts now");
 reset();marker(100);sgBeginWakeGameRecovery(7);auto path=SG_SLEEP_LEASE_DIR+L"\\100.txt";sgWriteFileAtomic(path,sgReadFile(path)+"|new-owner=true");window();assert(procs[100].calls==0&&exists(100));pass("marker replacement supersedes old recovery snapshot");
 reset();marker(100);sgBeginWakeGameRecovery(7);window();marker(101,true);sgBeginWakeGameRecovery(7);at(20000);assert(procs[101].calls==0);pass("duplicate explicit wake cannot capture a later manual pause");
 reset();marker(100);fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");sgBeginWakeGameRecovery(7);window();assert(procs[100].calls==1&&procs[100].depth==0);pass("in-memory owner reconstructs missing marker with original identity");
 reset();procs[100]={};enumeration={100};sgBeginWakeGameRecovery(7);assert(exists(100)&&g_sgWakeRecovery.entries[0].fallback);window();assert(procs[100].calls==1&&!exists(100));pass("missing PID recovered via memory + same-user + confirmed all-thread suspension");
 for(int n=0;n<7;++n){reset();procs[100]={};enumeration={100};auto& p=procs[100];if(n==0)p.name=L"browser.exe";if(n==1)blacklist={L"game.exe"};if(n==2)p.ws=10;if(n==3)p.sameUser=false;if(n==4)p.sameSession=false;if(n==5)p.debug=true;if(n==6)p.depth=0;p.whitelist=true;sgBeginWakeGameRecovery(7);assert(!exists(100)&&p.calls==0&&g_sgWakeRecovery.entries.empty());}pass("system blacklist, user blacklist, low memory, other user/session, debugger and running apps rejected (whitelist cannot bypass)");
 reset();procs[100]={};procs[100].known=false;enumeration={100};sgBeginWakeGameRecovery(7);assert(!exists(100)&&!g_sgWakeRecoveryPending);pass("unknown thread state fails closed for unrecorded candidate");
 reset();marker(100,true);sgWriteFileAtomic(SG_DIR+L"\\quickapp_suspended.json","old");sgBeginWakeGameRecovery(7);sgWriteFileAtomic(SG_DIR+L"\\quickapp_suspended.json","new");window();assert(sgReadFile(SG_DIR+L"\\quickapp_suspended.json")=="new");pass("late cleanup never deletes newly written frontend state");
 reset();marker(100);sgBeginWakeGameRecovery(7);at(20000);assert(procs[100].calls==1&&!exists(100)&&!g_sgWakeRecoveryPending);pass("late scheduler still performs one terminal attempt, no endless confirmation");
 reset();marker(100);sgBeginWakeGameRecovery(7);at(1000);generation=8;sgPrepareWakeRecoveryForSleep(8);assert(!exists(100)&&!owner.markerOwned&&!g_sgWakeRecoveryPending&&procs[100].calls==1);pass("new sleep clears only confirmed-running old markers before new pause capture");
 reset();marker(100,true);procs[100].known=false;sgBeginWakeGameRecovery(7);at(0);assert(!sgPrepareWakeRecoveryForPauseUnlocked(100));at(2500);assert(procs[100].calls==1&&exists(100,true));pass("unknown state cannot report false new pause or consume new user intent");
 reset();marker(100,true);g_sgExplicitGameWakeGeneration=7;g_sgExplicitGameWakePauseFence=2;auto newManual=SG_MANUAL_DIR+L"\\100.txt";sgWriteFileAtomic(newManual,sgReadFile(newManual)+"|pauseRun=runA|pauseSerial=3");enumeration={100};sgBeginWakeGameRecovery(7);assert(g_sgWakeRecovery.entries.empty()&&procs[100].calls==0&&exists(100,true));pass("manual pause after wake EVENT but before worker snapshot is not captured or rediscovered");
 reset();procs[100]={};enumeration={100};sgBeginWakeGameRecovery(7);blacklist={L"game.exe"};window();assert(procs[100].calls==0&&exists(100));pass("new user blacklist vetoes previously discovered orphan before each attempt");
 reset();procs[100]={};enumeration={100};sgBeginWakeGameRecovery(7);procs[100].debug=true;window();assert(procs[100].calls==0&&exists(100));pass("debugger attached after snapshot vetoes orphan recovery");
 reset();marker(100,true);g_sgExplicitGameWakeGeneration=7;g_sgExplicitGameWakePauseFence=2;auto oldRunPath=SG_MANUAL_DIR+L"\\100.txt";sgWriteFileAtomic(oldRunPath,sgReadFile(oldRunPath)+"|pauseRun=oldRun|pauseSerial=999");sgBeginWakeGameRecovery(7);window();assert(procs[100].calls==1&&!exists(100,true));pass("previous-run manual lease does not get mistaken for a new post-wake pause");
 reset();marker(100);sgBeginWakeGameRecovery(7);fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");window();assert(procs[100].calls==1&&!exists(100)&&procs[100].depth==0);pass("marker lost during delayed recovery is reconstructed from captured identity");
 reset();marker(100);procs[100].known=false;sgBeginWakeGameRecovery(7);at(0);procs[100].known=true;fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");at(2500);assert(procs[100].calls==1&&!owner.markerOwned);pass("missing marker on already-running process is retired without further thaw");
 reset();marker(100);sgBeginWakeGameRecovery(7);generation=8;g_powerLifecycle=PowerLifecycle::Suspending;at(1000);assert(exists(100)&&owner.markerOwned&&procs[100].calls==0&&!g_sgWakeRecoveryPending);pass("new sleep before any success preserves the still-suspended exact lease");
 reset();marker(100);procs[100].known=false;sgBeginWakeGameRecovery(7);at(0);generation=8;g_powerLifecycle=PowerLifecycle::Suspending;at(2500);assert(exists(100)&&owner.markerOwned&&procs[100].calls==1);pass("cancel race with unknown counts retains recovery evidence without more resume calls");

 reset();marker(100);generation=8;sgBeginWakeGameRecovery(8);assert(facts.back().second.at("inheritedSleepTargets")==1);window();assert(std::any_of(facts.begin(),facts.end(),[](const auto& f){return f.first=="sleep-game-recovery-attempt"&&f.second.at("generation")==8&&f.second.at("sleepOwnerGeneration")==7;}));assert(procs[100].depth==0&&!exists(100)&&!owner.markerOwned&&!g_sgInSuspend&&!g_sgGameActuallySuspended);pass("retained gen7 lease recovered by gen8 clears its original exact owner");
 reset();marker(100);procs[100].known=false;sgBeginWakeGameRecovery(7);at(0);generation=8;g_powerLifecycle=PowerLifecycle::Suspending;at(2500);assert(owner.markerOwned&&exists(100));g_powerLifecycle=PowerLifecycle::Ready;procs[100].known=true;sgBeginWakeGameRecovery(8);window();assert(!exists(100)&&!owner.markerOwned&&!g_sgInSuspend&&!g_sgGameActuallySuspended);pass("unknown-count cancel followed by re-sleep recovers and retires inherited owner");
 reset();marker(100);generation=8;sgBeginWakeGameRecovery(8);at(1000);generation=9;g_powerLifecycle=PowerLifecycle::Suspending;at(2500);assert(procs[100].calls==1&&!exists(100)&&!owner.markerOwned&&!g_sgInSuspend&&!g_sgGameActuallySuspended);pass("canceling new batch retires confirmed-running inherited lease without another thaw");
 reset();marker(100);generation=8;sgBeginWakeGameRecovery(8);owner.powerGeneration=9;window();assert(!exists(100)&&owner.markerOwned&&owner.powerGeneration==9&&g_sgInSuspend&&g_sgGameActuallySuspended);pass("old recovery never clears a replacement owner generation on the same PID");
 reset();marker(100);generation=8;sgBeginWakeGameRecovery(8);auto changedPath=SG_SLEEP_LEASE_DIR+L"\\100.txt";sgWriteFileAtomic(changedPath,sgReadFile(changedPath)+"|newOwner=9");owner.powerGeneration=9;window();assert(procs[100].calls==0&&exists(100)&&owner.markerOwned);pass("changed sleep marker and new owner supersede old recovery snapshot");
 reset();procs[100]={};enumeration={100};sgBeginWakeGameRecovery(7);procs[100].sticky=true;window();assert(exists(100)&&!owner.markerOwned&&g_sgInSuspend&&g_sgGameActuallySuspended);pass("retained orphan sleep marker preserves pause flags at terminal recovery");

 reset();marker(100);marker(101);procs[101].sticky=true;sgBeginWakeGameRecovery(7);at(0);
 assert(procs[100].calls==1&&!exists(100)&&procs[101].calls==1&&exists(101)&&g_sgWakeRecoveryPending&&focusCalls==1&&emitCalls==0);
 assert(lastFact("sleep-game-recovery-progress")["pendingTargets"]==1&&lastFact("sleep-game-recovery-progress")["attempt"]==1);
 procs[101].sticky=false;at(2500);assert(procs[100].calls==1&&procs[101].calls==2&&!exists(101)&&!g_sgWakeRecoveryPending&&focusCalls==2&&emitCalls==1);pass("partial success retires only confirmed entries and completes when the last target succeeds");
 reset();marker(100);marker(101);procs[101].known=false;sgBeginWakeGameRecovery(7);window();
 assert(procs[100].calls==1&&!exists(100)&&procs[101].calls==5&&exists(101)&&owner.markerOwned&&!g_sgWakeRecoveryPending);
 assert(lastFact("sleep-game-recovery-complete")["basis"]=="attempt-window-exhausted"&&lastFact("sleep-game-recovery-complete")["pendingTargets"]==1);pass("unconfirmed target keeps its full bounded retry schedule after another target completes");
 for(int state=0;state<3;++state){reset();marker(100);procs[100].threadTotal=state==0?0:54;procs[100].suspendedThreads=state==1?1:0;procs[100].known=state!=2;sgBeginWakeGameRecovery(7);at(0);assert(exists(100)&&owner.markerOwned&&g_sgWakeRecoveryPending);window();assert(procs[100].calls==5&&exists(100)&&!g_sgWakeRecoveryPending);}pass("empty, partly suspended and unknown/changing thread sets cannot retire accepted targets");
 reset();marker(100);sgBeginWakeGameRecovery(7);g_inputReady=false;at(0);at(2500);assert(procs[100].calls==0&&g_sgWakeRecovery.nextAttempt==0&&exists(100));g_inputReady=true;at(2501);assert(procs[100].calls==1&&!exists(100)&&!g_sgWakeRecoveryPending);pass("input Ready gates zero-offset dispatch without spending attempts while waiting");
 reset();marker(100);sgBeginWakeGameRecovery(7);g_powerLifecycle=PowerLifecycle::Resuming;at(0);at(4500);assert(procs[100].calls==0&&g_sgWakeRecovery.nextAttempt==0);g_powerLifecycle=PowerLifecycle::Ready;at(4501);assert(procs[100].calls==1&&!exists(100));pass("power Ready is required in addition to input Ready for first dispatch");
 reset();marker(100);sgBeginWakeGameRecovery(7);g_powerLifecycle=PowerLifecycle::Resuming;window();assert(procs[100].calls==0&&exists(100)&&owner.markerOwned&&!g_sgWakeRecoveryPending&&lastFact("sleep-game-recovery-complete")["pendingTargets"]==1);pass("final deadline while power is not Ready retains lease without resuming");
 reset();marker(100);sgBeginWakeGameRecovery(7);fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");markerWritesAllowed=false;at(0);assert(procs[100].calls==0&&owner.markerOwned&&g_sgWakeRecoveryPending&&g_sgWakeRecovery.nextAttempt==1);markerWritesAllowed=true;at(2500);assert(procs[100].calls==1&&!owner.markerOwned&&!g_sgWakeRecoveryPending);pass("failed marker rebuild spends its attempt, preserves owner and retries before any resume");
 reset();marker(100);sgBeginWakeGameRecovery(7);fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");markerWritesAllowed=false;window();assert(procs[100].calls==0&&owner.markerOwned&&!g_sgWakeRecoveryPending&&lastFact("sleep-game-recovery-complete")["pendingTargets"]==1);pass("persistent marker write failure ends within the original deadline and retains exact owner");
 reset();marker(100);sgBeginWakeGameRecovery(7);auto held=holdMarker(SG_SLEEP_LEASE_DIR+L"\\100.txt");assert(held!=INVALID_HANDLE_VALUE);at(0);assert(procs[100].calls==1&&exists(100)&&owner.markerOwned&&g_sgWakeRecoveryPending&&focusCalls==0);releaseMarker(held);at(2500);assert(procs[100].calls==2&&!exists(100)&&!owner.markerOwned&&!g_sgWakeRecoveryPending&&focusCalls==1);pass("failed marker deletion cannot retire or focus a confirmed target until a later current success");
 reset();marker(100);sgBeginWakeGameRecovery(7);auto heldFail=holdMarker(SG_SLEEP_LEASE_DIR+L"\\100.txt");assert(heldFail!=INVALID_HANDLE_VALUE);at(0);releaseMarker(heldFail);procs[100].fail=10;window();assert(procs[100].calls==5&&exists(100)&&owner.markerOwned&&focusCalls==0);pass("earlier accepted call with failed deletion cannot authorize retirement on subsequent failures");
 reset();marker(100);sgBeginWakeGameRecovery(7);onIdentityQuery=[](DWORD pid){++procs[pid].created;};at(0);assert(procs[100].calls==1&&exists(100)&&owner.markerOwned&&g_sgWakeRecoveryPending);at(2500);assert(procs[100].calls==1&&!exists(100));pass("PID birth is checked again after successful resume and full running-thread evidence");
 reset();marker(100);sgBeginWakeGameRecovery(7);onThreadQuery=[](DWORD pid){auto path=SG_SLEEP_LEASE_DIR+L"\\"+std::to_wstring(pid)+L".txt";sgWriteFileAtomic(path,sgReadFile(path)+"|changed-during-query=true");};at(0);assert(exists(100)&&owner.markerOwned&&g_sgWakeRecoveryPending&&focusCalls==0);at(2500);assert(procs[100].calls==1&&exists(100)&&owner.markerOwned&&!g_sgWakeRecoveryPending);pass("marker changed during thread inspection survives early retirement and supersedes old batch");
 reset();marker(100);sgBeginWakeGameRecovery(7);onThreadQuery=[](DWORD){generation=8;g_powerLifecycle=PowerLifecycle::Suspending;};at(0);assert(exists(100)&&owner.markerOwned&&g_sgWakeRecoveryPending&&focusCalls==0);at(2500);assert(procs[100].calls==1&&!g_sgWakeRecoveryPending);pass("new generation during current confirmation blocks early retirement and uses original cancellation");
 reset();marker(100);sgBeginWakeGameRecovery(7);onThreadQuery=[](DWORD){g_inputReady=false;};at(0);assert(exists(100)&&owner.markerOwned&&g_sgWakeRecoveryPending&&focusCalls==0);at(10000);assert(procs[100].calls==1&&exists(100)&&owner.markerOwned&&!g_sgWakeRecoveryPending);pass("loss of Ready during confirmation cannot borrow acceptedOnce at the terminal deadline");
 reset();marker(100);SgWakeRecoveryEntry inspected{SG_SLEEP_LEASE_DIR,100,procs[100].created,sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")};inspected.sleepOwnerGeneration=7;assert(sgBeginUserSleepGameRecoveryUnlocked(7,{inspected},"user-repair"));at(0);assert(procs[100].calls==1&&!exists(100)&&!g_sgWakeRecoveryPending&&focusCalls==0&&lastFact("sleep-game-recovery-complete")["requestId"]=="user-repair");pass("actual user-repair batch shares early completion without enabling focus");

 reset();marker(100,true);sgBeginWakeGameRecovery(7);assert(sgPrepareWakeRecoveryForPauseUnlocked(100));marker(100,true);at(0);window();assert(procs[100].calls==0&&procs[100].depth==1&&exists(100,true)&&!g_sgWakeRecoveryPending);pass("same-PID manual pause before the first immediate tick supersedes recovery without a thaw");
 reset();marker(100);inspected={SG_SLEEP_LEASE_DIR,100,procs[100].created,sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")};inspected.sleepOwnerGeneration=7;assert(sgBeginUserSleepGameRecoveryUnlocked(7,{inspected},"pause-during-confirmation"));onIdentityQuery=[](DWORD pid){marker(pid,true);procs[pid].depth=1;};at(0);assert(exists(100)&&exists(100,true)&&owner.markerOwned&&g_sgWakeRecoveryPending);window();assert(procs[100].calls==1&&procs[100].depth==1&&exists(100)&&exists(100,true));pass("manual pause during final identity proof prevents retirement and all later user-repair resumes");
 std::cout<<"sleep wake recovery production-header selftest: "<<passed<<" groups PASS\n";
}
"""

# Exercise the actual sgRealWake dispatch against the same production batch.
masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', lambda m:' '*len(m.group()),source,flags=re.S)
m = re.search(r'\bstatic void sgRealWake\([^;{}]*\)\s*\{', masked)
opening=masked.index('{',m.start());depth=1;end=opening+1
while depth:
    depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
wake_dispatch=source[m.start():end]
wake_boundaries=r"""
enum class SgSleepMode{S3,S4};
struct Task{ULONGLONG generation=7;SgSleepMode mode=SgSleepMode::S3;} g_sgTask;
bool g_sgRepairEligible=true,g_sgRetryInProgress=false;
std::atomic<bool> g_sgSleepCycleActive{true};
int inlineResumeCalls=0;
bool sgNonUserWakeGuardEnabled(){return false;}
bool sgExternalDeviceWakeIntentAge(ULONGLONG&){return false;}
void stopPowerResumeWatchdog(){}
void sgStopSleepRetry(){retry=false;}
struct SgResumeResult{int count=0;};
SgResumeResult sgResumeSleepTarget(ULONGLONG,bool){++inlineResumeCalls;return {};}
template<class... T>void traceLog(const char*,T...){}
"""
cpp=cpp.replace('int passed=0;',wake_boundaries+'\n'+wake_dispatch+'\nint passed=0;')
cpp=cpp.replace(' generation=7;tick=0;', ' g_sleepRecoveryAutoEnabled=false;g_sleepRecoveryAutoPending=false;g_sleepRecoveryAutoConfigEpoch=1;g_sgTask={};g_sgRepairEligible=true;g_sgRetryInProgress=false;g_sgSleepCycleActive=true;inlineResumeCalls=0;generation=7;tick=0;')
cpp=cpp.replace(' std::cout<<"sleep wake recovery production-header selftest:',r"""
 reset();marker(100);sgRealWake("resume_suspend",7);assert(g_sgWakeRecoveryPending&&procs[100].calls==0);sgRealWake("resume_auto",7);sgRealWake("monitor_on",7);assert(inlineResumeCalls==0&&exists(100)&&procs[100].calls==0);window();assert(procs[100].calls==1);pass("late automatic/monitor wake after explicit dispatch cannot bypass delayed batch");
 reset();marker(100);g_sgTask={};sgRealWake("resume_auto",7);assert(inlineResumeCalls==0&&procs[100].calls==0&&exists(100)&&!g_sgWakeRecoveryPending);pass("automatic wake with lost SleepTask does not authorize inline/global thaw");
 std::cout<<"sleep wake recovery production-header selftest:""")

cpp_path=out/'sleep_wake_resume_native_selftest.cpp'; cpp_path.write_text(cpp,'utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
exe=out/'sleep_wake_resume_native_selftest.exe'; cmd=out/'run-wake-recovery-selftest.cmd'
toolchain_js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain_prefix=subprocess.check_output(['node','--input-type=module','-e',toolchain_js,str(vs)],text=True,encoding='utf-8')
cmd.write_text(toolchain_prefix+f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{cpp_path}" /I"{r / "native"}" /I"{r / "deps/json"}" /Fo"{out / "sleep_wake_resume_selftest.obj"}" /Fe"{exe}" /link advapi32.lib\r\nif errorlevel 1 exit /b 1\r\n"{exe}" "{out / "owned-fixture-state"}"\r\n',encoding='ascii')
res=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'wake-recovery-selftest.log').write_bytes(res.stdout);print(res.stdout.decode('utf-8',errors='replace'));raise SystemExit(res.returncode)
