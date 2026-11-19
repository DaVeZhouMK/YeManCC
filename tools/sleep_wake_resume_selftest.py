"""Compile the production wake recovery header with deterministic OS boundaries.
Never enumerate/resume live software or trigger system sleep. Real OS probing is
covered by the independently owned-process companion smoke test.
"""
from pathlib import Path
import argparse, subprocess, re
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/SleepResumeRetry-20261003'))
parser.add_argument('--p0-case', choices=['multi-target', 'per-nt', 'readiness', 'readiness-retirement', 'public-dispatch', 'wake-admission-replay'], default=None,
                    help='Run exactly one P0 control group instead of all original groups.')
a = parser.parse_args(); out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
r = Path.cwd(); source = (r/'native/main.cpp').read_text('utf-8-sig')
h = (r/'native/wake_game_recovery.h').read_text('utf-8')
assert 'sgBeginWakeGameRecovery(expectedGeneration);' in source
wake = source[source.index('static void sgRealWake('):source.index('static SgSleepMode sgPowerButtonSleepMode()')]
user = wake[wake.index('if (strcmp(src, "resume_suspend") == 0)'):wake.index('if (!hadSleepCycle)')]
assert 'sgResumeSleepTarget(' not in user and 'sgBeginWakeGameRecovery' in user
assert not re.search(r'\bSleep\s*\(', h) and 'wait_for' not in h
prepare_pause = list(re.finditer(r'\bsgPrepareWakeRecoveryForPauseUnlocked\(rootPid(?:, requestCurrent)?\)', source))
assert len(prepare_pause)==1 and prepare_pause[0].start() < source.index('const std::vector<DWORD> targets{rootPid};')
assert 'sgCancelWakeGameRecoveryUnlocked("application-exit")' in source
assert h.count('nativeMonitorExcluded(') == 3 and h.count('sgNameExcludedBy(') == 3
assert 'CreateToolhelp32Snapshot' not in h and 'Process32FirstW' not in h and 'Process32NextW' not in h
assert 'ymcc::processInventory::read()' in h
assert 'ymcc::processInventory::Status::Complete' in h
cpp = r"""
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <functional>
#include <deque>
#include <future>
#include <thread>
#include <chrono>
#include <utility>
#include "json.hpp"
#include "wake_game_resume.h"
#include "sleep_power_dispatch.h"
#include "sleep_power_observation.h"
#include "input_host_shared_power.h"
__FIXTURE_CURRENT_SLEEP_HEADERS__
static std::unique_ptr<ymcc::sleeppower::Dispatch> fixtureDispatch=std::make_unique<ymcc::sleeppower::Dispatch>();
#define g_sleepPowerDispatch (*fixtureDispatch)
using json=nlohmann::json; namespace fspath=std::filesystem;
enum class PowerLifecycle { Ready, Suspending, Suspended, Resuming };
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
std::atomic<bool> g_inputReady{true},g_exitRequested{false},g_sgWakeRecoveryPending{false},g_sgInSuspend{true},g_sgGameActuallySuspended{true};
// Inactive fixture input; actual Kernel publication is exercised by the owned cancellation fixture.
std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{0};
std::atomic<DWORD> g_manualPausedPid{0};
std::atomic<unsigned long long> g_sgExplicitGameWakeGeneration{0},g_sgExplicitGameWakePauseFence{0};
std::mutex g_sgOpMtx,g_gameRulesMx;
std::condition_variable g_sgWorkCv;
enum class SgWork : uint8_t { Suspend, WakeAutomatic, WakeSuspend, WakeHibernate };
struct SgWorkItem { SgWork kind; unsigned long long generation; };
std::mutex g_sgWorkMx;std::deque<SgWorkItem> g_sgWorkQ;
bool g_sgWorkStop=false;unsigned long long g_sgWorkWakeRevision=0;
__FIXTURE_STARTUP_RESUME_DELIVERY__
std::function<ULONGLONG(ULONGLONG)> workDelay;
ULONGLONG sleepFixtureAutoWaitMs(ULONGLONG now){return workDelay?workDelay(now):~ULONGLONG{0};}
__ACTUAL_WORK_NOTIFY__
bool g_guardEnabled=true,g_sgPauseResume=true,retry=false;
ULONGLONG generation=7,tick=0;
std::atomic<bool> g_sleepRecoveryAutoEnabled{false},g_sleepRecoveryAutoPending{false};
std::atomic<ULONGLONG> g_sleepRecoveryAutoConfigEpoch{1};
bool sleepRecoveryAutoAllows(ULONGLONG gen,ULONGLONG epoch){return g_sleepRecoveryAutoEnabled&&gen==generation&&epoch==g_sleepRecoveryAutoConfigEpoch&&!g_exitRequested;}
unsigned fixtureAutoArmCalls=0;
std::function<void()> fixtureRetryCheck;
void sleepRecoveryAutoArm(ULONGLONG){++fixtureAutoArmCalls;}
std::wstring SG_DIR,SG_SLEEP_LEASE_DIR,SG_MANUAL_DIR;
constexpr ULONGLONG SG_MIN_WS=500ULL*1024*1024,SG_CAPTURE_MIN_WS=50ULL*1024*1024;
struct SgSleepTarget{DWORD pid=0;ULONGLONG processCreated=0,powerGeneration=0;bool markerOwned=false,captured=false;};
SgSleepTarget owner;
struct NativeDetectedGame{DWORD pid=0;ULONGLONG processCreated=0,workingSet=0;std::wstring path;bool whitelisted=false;};
enum class NativeOnePidResult{Ok,Rejected};
struct Proc{ULONGLONG created=1234,ws=600ULL*1024*1024;int depth=1,calls=0,fail=0;bool sticky=false,known=true,exited=false,sameUser=true,sameSession=true,debug=false,whitelist=false;DWORD openError=0;std::wstring name=L"game.exe";unsigned threadTotal=2;int suspendedThreads=-1;int ntCalls=0;};
std::map<DWORD,Proc> procs;
std::vector<DWORD> enumeration;
size_t enumIndex=0;DWORD error=0;
unsigned inventoryReads=0,legacyEnumerationCalls=0;
bool markerDirectoryUnknown=false;
namespace ymcc::processInventory {
enum class Status{Complete,Unavailable,Failed};
struct Entry{DWORD pid;std::wstring name;};
struct Snapshot{Status status=Status::Complete;std::vector<Entry> rows;};
Status inventoryStatus=Status::Complete;
std::shared_ptr<const Snapshot> read(){
 ++inventoryReads;auto result=std::make_shared<Snapshot>();result->status=inventoryStatus;
 for(DWORD pid:enumeration)result->rows.push_back({pid,procs.at(pid).name});
 return result;
}
}
std::vector<std::pair<std::string,json>> facts;int focusCalls=0,emitCalls=0;
std::vector<std::wstring> blacklist;
bool markerWritesAllowed=true;
std::function<void(DWORD)> onThreadQuery,onIdentityQuery,onProcessOpen,onNtResume,onHandleIdentity;
std::function<void(const std::wstring&, const std::string&)> onFileReadComplete{};
int fixtureOpenHandles=0;
std::vector<DWORD> fixtureNtDispatches;
ULONGLONG currentPowerGeneration(){return generation;}
static ymcc::sleeppower::RevocationMirrors g_sharedPowerRevocations;
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,uint32_t size) noexcept {
 if(!output||size!=sizeof(*output)||g_exitRequested)return 0;
 *output={};output->generation=currentPowerGeneration();
 output->phase=static_cast<uint32_t>(g_powerLifecycle.load());
 output->observedNotificationSequence=g_sleepPowerDispatch.observedSequence();
 output->flags=ymcc::sleeppower::kKnown;
 if(output->phase==0)output->flags|=ymcc::sleeppower::kHardwareAllowed;
 if(g_sleepPowerDispatch.pendingSuspend())output->flags|=ymcc::sleeppower::kNotificationPending;
 return 1;
}
static auto fixtureSleepRepairAdmission(){
 return ymcc::inputHost::sharedPower::Scope::create(&readSharedSystemPower,g_sharedPowerRevocations,false);
}
bool sgSleepRetryActive(){if(fixtureRetryCheck)fixtureRetryCheck();return retry;}
ULONGLONG fakeTick(){return tick;}
DWORD fakeError(){return error;}
HANDLE fakeOpen(DWORD access,BOOL,DWORD pid){auto i=procs.find(pid);if(i==procs.end()){error=ERROR_INVALID_PARAMETER;return nullptr;}if(i->second.openError){error=i->second.openError;return nullptr;}++fixtureOpenHandles;if(access&PROCESS_SUSPEND_RESUME){auto hook=std::exchange(onProcessOpen,{});if(hook)hook(pid);}return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));}
DWORD pidOf(HANDLE h){return static_cast<DWORD>(reinterpret_cast<uintptr_t>(h));}
BOOL fakeClose(HANDLE){assert(fixtureOpenHandles>0);--fixtureOpenHandles;return TRUE;}
DWORD fakeWait(HANDLE h,DWORD){return procs.at(pidOf(h)).exited?WAIT_OBJECT_0:WAIT_TIMEOUT;}
DWORD fakeSelf(){return 999;}
BOOL fakeSession(DWORD pid,DWORD* s){*s=pid==999||procs.at(pid).sameSession?1:2;return TRUE;}
BOOL fakeDebug(HANDLE h,PBOOL v){*v=procs.at(pidOf(h)).debug;return TRUE;}
HANDLE fakeSnapshot(DWORD,DWORD){++legacyEnumerationCalls;enumIndex=0;return reinterpret_cast<HANDLE>(123456);}
BOOL fakeFirst(HANDLE,PROCESSENTRY32W* p){if(enumeration.empty())return FALSE;p->th32ProcessID=enumeration[0];wcscpy_s(p->szExeFile,procs.at(enumeration[0]).name.c_str());return TRUE;}
BOOL fakeNext(HANDLE,PROCESSENTRY32W* p){if(++enumIndex>=enumeration.size())return FALSE;p->th32ProcessID=enumeration[enumIndex];wcscpy_s(p->szExeFile,procs.at(enumeration[enumIndex]).name.c_str());return TRUE;}
ULONGLONG nativeProcessCreatedFromHandle(HANDLE h){const DWORD pid=pidOf(h);auto hook=std::exchange(onHandleIdentity,{});if(hook)hook(pid);return procs.at(pid).created;}
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
std::string sgReadFile(const std::wstring& p){std::ifstream f{fspath::path(p)};const std::string bytes{std::istreambuf_iterator<char>(f),{}};const auto hook=onFileReadComplete;if(hook)hook(p,bytes);return bytes;}
bool sgEnsureMarkerDir(const std::wstring& d){std::error_code ec;fspath::create_directories(d,ec);return !ec;}
bool sgWriteFileAtomic(const std::wstring& p,const std::string& text){if(!markerWritesAllowed)return false;std::ofstream f{fspath::path(p)};f<<text;return bool(f);}
double sgNowEpoch(){return tick/1000.0;}
const std::string& sgManualPauseRunToken(){static const std::string token="runA";return token;}
ULONGLONG sgMarkerCreated(const std::wstring& d,DWORD pid){auto t=sgReadFile(d+L"\\"+std::to_wstring(pid)+L".txt");auto p=t.find("|created=");if(p==std::string::npos)return 0;return std::stoull(t.substr(p+9));}
unsigned long long sgMarkerGeneration(const std::wstring& d,DWORD pid){auto t=sgReadFile(d+L"\\"+std::to_wstring(pid)+L".txt");auto p=t.find("|generation=");if(p==std::string::npos)return 0;return std::stoull(t.substr(p+12));}
bool sgMarkerIsSuspended(const std::wstring& d,DWORD pid){return sgReadFile(d+L"\\"+std::to_wstring(pid)+L".txt").find("|state=suspended")!=std::string::npos;}
bool sgHasMarkerFiles(const std::wstring& d){if(markerDirectoryUnknown)return true;for(auto& f:fspath::directory_iterator(d))if(f.is_regular_file())return true;return false;}
bool sgReadSleepTarget(SgSleepTarget& t){t=owner;return owner.pid!=0;}
void sgClearSleepTargetIfMatches(const SgSleepTarget& t){if(owner.pid==t.pid&&owner.processCreated==t.processCreated&&owner.powerGeneration==t.powerGeneration)owner={};}
void sgRecordFact(const char* name,json data=json::object()){facts.push_back({name,data});}
void focusSingleResumedGame(std::vector<DWORD>,ULONGLONG,const ymcc::sleeppower::SharedPowerV1& power){
 assert(power.generation==generation);++focusCalls;
}
void ipc_emit(const char*,json){++emitCalls;}
void sgInitNt(){}
// The NT API is a deterministic memory port; the drain loop below is the
// verbatim production main.cpp implementation, selected by macro names only.
LONG fixtureNtResume(HANDLE h){
 const DWORD pid=pidOf(h);auto& p=procs.at(pid);++p.ntCalls;fixtureNtDispatches.push_back(pid);
 if(p.fail>0){--p.fail;return -1;}
 if(!p.sticky)p.depth=std::max(0,p.depth-1);
 auto hook=std::exchange(onNtResume,{});if(hook)hook(pid);
 return 0;
}
using FixtureNtResume=LONG(*)(HANDLE);
FixtureNtResume fnNtResume=fixtureNtResume;
#define sgDrainProcessSuspendCountIfCurrent fixtureProductionDrainIfCurrent
#define sgDrainProcessSuspendCount fixtureProductionDrain
__ACTUAL_PRODUCTION_DRAIN_FUNCTIONS__
#undef sgDrainProcessSuspendCountIfCurrent
#undef sgDrainProcessSuspendCount
template<class Current> bool sgDrainProcessSuspendCountIfCurrent(HANDLE h,const Current& current){
 ++procs.at(pidOf(h)).calls;return fixtureProductionDrainIfCurrent(h,current);
}
bool sgDrainProcessSuspendCount(HANDLE h){
 ++procs.at(pidOf(h)).calls;return fixtureProductionDrain(h);
}
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
static bool sgReadResumeMarkerChecked(const std::wstring&,std::string&);
static bool nativeValveValidateOwnedSuspendedTarget(const std::wstring&,DWORD,ULONGLONG);
__FIXTURE_CURRENT_WAKE_PORTS__
#include "wake_game_recovery.h"
static bool sgBeginUserSleepGameRecoveryUnlocked(unsigned long long generation,
 const std::vector<SgWakeRecoveryEntry>& entries,const std::string& requestId){
 return sgBeginUserSleepGameRecoveryUnlocked(generation,entries,requestId,0,fixtureSleepRepairAdmission());
}
#undef queryThreads
#undef sameUser
int passed=0;
void pass(const char* n){assert(legacyEnumerationCalls==0&&fixtureOpenHandles==0);++passed;std::cout<<"PASS "<<n<<"\n";}
const json& lastFact(const char* name){for(auto i=facts.rbegin();i!=facts.rend();++i)if(i->first==name)return i->second;assert(false);return facts.back().second;}
void reset(){
 fixtureDispatch=std::make_unique<ymcc::sleeppower::Dispatch>();
 for(auto& dir:{SG_SLEEP_LEASE_DIR,SG_MANUAL_DIR})for(auto& f:fspath::directory_iterator(dir)){assert(f.is_regular_file());fspath::remove(f.path());}
 fspath::remove(SG_DIR+L"\\quickapp_suspended.json");
 procs.clear();enumeration.clear();blacklist.clear();facts.clear();focusCalls=emitCalls=0;
 assert(fixtureOpenHandles==0);fixtureNtDispatches.clear();markerWritesAllowed=true;onThreadQuery={};onIdentityQuery={};onProcessOpen={};onNtResume={};onHandleIdentity={};onFileReadComplete={};
 inventoryReads=legacyEnumerationCalls=0;markerDirectoryUnknown=false;ymcc::processInventory::inventoryStatus=ymcc::processInventory::Status::Complete;
 generation=7;tick=0;error=0;retry=false;owner={};g_sgExplicitGameWakeGeneration=0;g_sgExplicitGameWakePauseFence=0;g_sgWakeRecovery={};g_sgLastGameWakeGeneration=0;
 g_sgWakeRecoveryPending=false;g_exitRequested=false;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_sgInSuspend=true;g_sgGameActuallySuspended=true;g_manualPausedPid=0;g_guardEnabled=true;g_sgPauseResume=true;
}
void marker(DWORD pid,bool manual=false){auto& p=procs[pid];sgEnsureMarkerDir(manual?SG_MANUAL_DIR:SG_SLEEP_LEASE_DIR);auto path=(manual?SG_MANUAL_DIR:SG_SLEEP_LEASE_DIR)+L"\\"+std::to_wstring(pid)+L".txt";assert(sgWriteFileAtomic(path,"pid="+std::to_string(pid)+"|created="+std::to_string(p.created)+"|state=suspended|generation=7"));if(!manual)owner={pid,p.created,7,true,true};}
void at(ULONGLONG ms){tick=ms;sgWakeGameRecoveryTick();}
bool exists(DWORD pid,bool manual=false){return fspath::exists((manual?SG_MANUAL_DIR:SG_SLEEP_LEASE_DIR)+L"\\"+std::to_wstring(pid)+L".txt");}
void window(){for(auto t:WakeGameResume::attemptOffsetsMs)at(t);}
// Fixture-only extensions inside the two existing stale-target groups.
// They add no pass name, selector, retry offset, thread or product behavior.
unsigned fixtureStaleBasicSubcases = 0, fixtureStaleExtendedSubcases = 0;
unsigned fixtureStaleHoldsAcquired = 0, fixtureStaleHoldsReleased = 0;

unsigned fixtureStaleFactCount(const char* name) {
    unsigned result = 0;
    for (const auto& fact : facts) if (fact.first == name) ++result;
    return result;
}

void fixtureAssertStaleSafety() {
    assert(procs.at(100).calls == 0 && procs.at(100).ntCalls == 0);
    assert(fixtureNtDispatches.empty() && focusCalls == 0 && fixtureOpenHandles == 0);
    assert(fixtureStaleFactCount("sleep-game-recovery-confirmed") == 0);
}

void fixtureAssertStaleFlagsMatchResponsibilities() {
    SgSleepTarget remaining;
    const bool ownedSleepLease = sgReadSleepTarget(remaining) && remaining.markerOwned;
    const bool retainedSleepMarker = sgHasMarkerFiles(SG_SLEEP_LEASE_DIR);
    const bool expected = ownedSleepLease || retainedSleepMarker;
    assert(g_sgInSuspend.load() == expected && g_sgGameActuallySuspended.load() == expected);
}

struct FixtureStaleMarkerSnapshot {
    std::wstring path;
    std::string bytes;
    ULONGLONG created;
};

FixtureStaleMarkerSnapshot fixtureBeginExactStaleMarker(bool reused) {
    reset();
    marker(100);
    FixtureStaleMarkerSnapshot before{SG_SLEEP_LEASE_DIR + L"\\100.txt", "", procs.at(100).created};
    before.bytes = sgReadFile(before.path);
    assert(before.bytes == "pid=100|created=" + std::to_string(before.created) + "|state=suspended|generation=7");
    sgBeginWakeGameRecovery(7);
    assert(g_sgWakeRecoveryPending.load() && g_sgWakeRecovery.generation == 7);
    assert(g_sgWakeRecovery.entries.size() == 1);
    const auto& entry = g_sgWakeRecovery.entries.front();
    assert(entry.pid == 100 && entry.created == before.created && entry.markerText == before.bytes);
    assert(entry.dir == SG_SLEEP_LEASE_DIR);
    if (reused) ++procs.at(100).created;
    else procs.at(100).exited = true;
    return before;
}

struct FixtureOwnedStaleMarkerHold {
    HANDLE value;
    explicit FixtureOwnedStaleMarkerHold(const std::wstring& path) : value(holdMarker(path)) {
        assert(value != INVALID_HANDLE_VALUE);
        ++fixtureStaleHoldsAcquired;
    }
    void close() {
        if (value != INVALID_HANDLE_VALUE) {
            releaseMarker(value); // Original function resolves real CloseHandle before fakeClose is defined.
            value = INVALID_HANDLE_VALUE;
            ++fixtureStaleHoldsReleased;
        }
    }
    ~FixtureOwnedStaleMarkerHold() { close(); }
};

void fixtureAssertStaleRemovalFact(const json& fact, bool removed, bool retained) {
    assert(fact.contains("markerRemoved") && fact["markerRemoved"].is_boolean());
    assert(fact.contains("markerRemovalError") && fact["markerRemovalError"].is_number_integer());
    assert(fact.contains("cleanupRetained") && fact["cleanupRetained"].is_boolean());
    assert(fact["markerRemoved"] == removed && fact["cleanupRetained"] == retained);
    if (retained) assert(fact["markerRemovalError"].get<int>() != 0);
    else assert(fact["markerRemovalError"].get<int>() == 0);
}

void fixtureRunStaleMarkerBoundaryCases(bool reused) {
    // Temporary refusal is retried at the next original offset. This cleanup
    // assertion deliberately precedes all assertions of the new diagnostic fields.
    {
        const auto before = fixtureBeginExactStaleMarker(reused);
        FixtureOwnedStaleMarkerHold held(before.path);
        at(0);
        fixtureAssertStaleSafety();
        assert(fixtureStaleFactCount("sleep-game-recovery-stale") == 1);
        assert(exists(100) && sgReadFile(before.path) == before.bytes);
        const json refusedFact = lastFact("sleep-game-recovery-stale");
        held.close();
        assert(held.value == INVALID_HANDLE_VALUE);
        at(2500);
        fixtureAssertStaleSafety();
        const bool staleMarkerClearedAfterTransientRelease = !exists(100);
        assert(staleMarkerClearedAfterTransientRelease && "STALE_TRANSIENT_RELEASE_RETIRES_OLD_MARKER_AT_2500");
        fixtureAssertStaleFlagsMatchResponsibilities();
        fixtureAssertStaleRemovalFact(refusedFact, false, true);
        fixtureAssertStaleRemovalFact(lastFact("sleep-game-recovery-stale"), true, false);
        ++fixtureStaleExtendedSubcases;
    }
    // Permanent refusal consumes only the original bounded window. The file and
    // logical cleanup responsibility survive, without prescribing done/owner internals.
    {
        const auto before = fixtureBeginExactStaleMarker(reused);
        FixtureOwnedStaleMarkerHold held(before.path);
        for (auto offset : WakeGameResume::attemptOffsetsMs) {
            at(offset);
            fixtureAssertStaleSafety();
            assert(exists(100) && sgReadFile(before.path) == before.bytes);
        }
        assert(!g_sgWakeRecoveryPending.load());
        assert(fixtureStaleFactCount("sleep-game-recovery-stale") == WakeGameResume::attemptCount);
        assert(lastFact("sleep-game-recovery-complete")["healthVerified"] == false);
        fixtureAssertStaleRemovalFact(lastFact("sleep-game-recovery-stale"), false, true);
        fixtureAssertStaleFlagsMatchResponsibilities();
        const auto attemptsAtDeadline = fixtureStaleFactCount("sleep-game-recovery-stale");
        held.close();
        at(20000);
        fixtureAssertStaleSafety();
        assert(!g_sgWakeRecoveryPending.load());
        assert(fixtureStaleFactCount("sleep-game-recovery-stale") == attemptsAtDeadline);
        assert(exists(100) && sgReadFile(before.path) == before.bytes);
        fixtureAssertStaleFlagsMatchResponsibilities();
        ++fixtureStaleExtendedSubcases;
    }
    // A legal replacement marker and owner supersede the old batch. No resume
    // of the replacement is authorized by the old owner's stale cleanup.
    {
        const auto before = fixtureBeginExactStaleMarker(reused);
        FixtureOwnedStaleMarkerHold held(before.path);
        at(0);
        fixtureAssertStaleSafety();
        assert(exists(100) && sgReadFile(before.path) == before.bytes);
        held.close();
        ++procs.at(100).created;
        procs.at(100).exited = false;
        marker(100);
        const auto replacementBytes = sgReadFile(before.path);
        const auto replacementOwner = owner;
        assert(replacementBytes != before.bytes && replacementOwner.captured && replacementOwner.markerOwned);
        at(2500);
        fixtureAssertStaleSafety();
        assert(exists(100) && sgReadFile(before.path) == replacementBytes);
        assert(owner.pid == replacementOwner.pid && owner.processCreated == replacementOwner.processCreated);
        assert(owner.powerGeneration == replacementOwner.powerGeneration && owner.captured && owner.markerOwned);
        fixtureAssertStaleFlagsMatchResponsibilities();
        ++fixtureStaleExtendedSubcases;
    }
    // A new power generation cancels this window without deleting its retained
    // original responsibility or authorizing any later process action.
    {
        const auto before = fixtureBeginExactStaleMarker(reused);
        FixtureOwnedStaleMarkerHold held(before.path);
        at(0);
        fixtureAssertStaleSafety();
        assert(exists(100) && sgReadFile(before.path) == before.bytes);
        held.close();
        generation = 8;
        g_powerLifecycle = PowerLifecycle::Suspending;
        at(2500);
        fixtureAssertStaleSafety();
        assert(!g_sgWakeRecoveryPending.load());
        assert(exists(100) && sgReadFile(before.path) == before.bytes);
        fixtureAssertStaleFlagsMatchResponsibilities();
        ++fixtureStaleExtendedSubcases;
    }
}

// P0 cancellation controls: the first actual NT side effect is allowed only
// while the batch/entry remains current; subsequent side effects must stop.
void fixtureCancelBatch(unsigned mode){
 if(mode==0){generation=8;g_powerLifecycle=PowerLifecycle::Suspending;}
 else if(mode==1)g_exitRequested=true;
 else if(mode==2)retry=true;
 else {g_sleepRecoveryAutoEnabled=false;++g_sleepRecoveryAutoConfigEpoch;}
}
void fixtureArmAutomatic(unsigned mode){
 if(mode==3){g_sleepRecoveryAutoEnabled=true;g_sleepRecoveryAutoConfigEpoch=1;g_sgWakeRecovery.autoConfigEpoch=1;}
}
void fixtureRunCancellationControls(const std::string& selected="all"){
 if(selected=="all"||selected=="multi-target"){
 for(unsigned mode=0;mode<4;++mode){
  reset();marker(100);marker(101);sgBeginWakeGameRecovery(7);fixtureArmAutomatic(mode);
  onThreadQuery=[mode](DWORD){fixtureCancelBatch(mode);};at(0);
  assert(procs[100].calls+procs[101].calls==1);
  assert(fixtureNtDispatches.size()==8&&fixtureOpenHandles==0&&focusCalls==0);
  const DWORD retained=procs[100].calls==0?100:101;
  assert(procs[retained].ntCalls==0&&procs[retained].depth==1&&exists(retained));
  const auto ntBefore=fixtureNtDispatches.size();at(10000);
  assert(fixtureNtDispatches.size()==ntBefore&&!g_sgWakeRecoveryPending);
 }
 pass("P0 multi-target cancellation during first confirmation blocks all later native resumes for new sleep/exit/retry/auto-OFF");
 }
 if(selected=="all"){
 for(unsigned mode=0;mode<4;++mode){
  reset();marker(100);sgBeginWakeGameRecovery(7);fixtureArmAutomatic(mode);
  onProcessOpen=[mode](DWORD){fixtureCancelBatch(mode);};at(0);
  assert(procs[100].calls==0&&procs[100].ntCalls==0&&fixtureOpenHandles==0&&exists(100)&&owner.markerOwned&&focusCalls==0);
  at(10000);assert(procs[100].ntCalls==0&&!g_sgWakeRecoveryPending);
 }
 pass("P0 cancellation after process-handle open closes the handle and dispatches zero native resumes");
 }
 if(selected=="all"||selected=="per-nt"){
 for(unsigned mode=0;mode<4;++mode){
  reset();marker(100);procs[100].depth=3;sgBeginWakeGameRecovery(7);fixtureArmAutomatic(mode);
  onNtResume=[mode](DWORD){fixtureCancelBatch(mode);};at(0);
  assert(procs[100].calls==1&&procs[100].ntCalls==1&&procs[100].depth==2);
  assert(fixtureOpenHandles==0&&exists(100)&&owner.markerOwned&&focusCalls==0);
  at(2500);at(10000);
  assert(procs[100].ntCalls==1&&procs[100].depth==2&&exists(100)&&owner.markerOwned&&!g_sgWakeRecoveryPending);
 }
 pass("P0 actual production drain checks cancellation before each of its eight possible NT dispatches");
 }
 if(selected!="all")return;
 for(unsigned gate=0;gate<2;++gate){
  reset();marker(100);sgBeginWakeGameRecovery(7);
  onProcessOpen=[gate](DWORD){if(gate==0)g_inputReady=false;else g_powerLifecycle=PowerLifecycle::Resuming;};at(0);
  assert(procs[100].ntCalls==0&&exists(100)&&g_sgWakeRecoveryPending&&fixtureOpenHandles==0);
  g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;at(2500);
  assert(procs[100].calls==1&&procs[100].ntCalls==8&&!exists(100)&&!g_sgWakeRecoveryPending);
 }
 pass("P0 temporary input/power readiness loss retains the original batch and retry offsets");
 reset();marker(100,true);sgBeginWakeGameRecovery(7);assert(g_sgWakeRecovery.autoConfigEpoch==0);
 onProcessOpen=[](DWORD){g_sleepRecoveryAutoEnabled=false;++g_sleepRecoveryAutoConfigEpoch;};at(0);
 assert(procs[100].calls==1&&procs[100].ntCalls==8&&!exists(100,true)&&!g_sgWakeRecoveryPending);
 pass("P0 explicit wake keeps its pre-wake manual snapshot and is independent of automatic recovery OFF");
 reset();marker(100);
 SgWakeRecoveryEntry exact{SG_SLEEP_LEASE_DIR,100,procs[100].created,sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")};
 exact.sleepOwnerGeneration=7;assert(sgBeginUserSleepGameRecoveryUnlocked(7,{exact},"P0-manual-repair"));
 onProcessOpen=[](DWORD){g_sleepRecoveryAutoEnabled=false;++g_sleepRecoveryAutoConfigEpoch;};at(0);
 assert(procs[100].calls==1&&procs[100].ntCalls==8&&!exists(100)&&focusCalls==0&&!g_sgWakeRecoveryPending);
 pass("P0 explicit API repair with epoch zero remains available while automatic recovery is OFF");
 for(bool newGeneration:{false,true}){
  reset();marker(100);sgBeginWakeGameRecovery(7);procs[100].created=5678;
  onHandleIdentity=[newGeneration](DWORD pid){
   const auto path=SG_SLEEP_LEASE_DIR+L"\\"+std::to_wstring(pid)+L".txt";
   assert(sgWriteFileAtomic(path,sgReadFile(path)+"|replacement-during-handle-query=true"));
   if(newGeneration){generation=8;g_powerLifecycle=PowerLifecycle::Suspending;owner.powerGeneration=8;}
  };at(0);
  assert(procs[100].ntCalls==0&&fixtureOpenHandles==0&&exists(100)&&owner.markerOwned);
  assert(sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt").find("|replacement-during-handle-query=true")!=std::string::npos);
  at(2500);assert(exists(100)&&owner.markerOwned&&procs[100].ntCalls==0);
 }
 pass("P0 stale-handle cleanup preserves a replacement marker and current owner observed after identity query");
 reset();marker(100);sgBeginWakeGameRecovery(7);procs[100].depth=0;
 fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");
 onIdentityQuery=[](DWORD pid){marker(pid);assert(sgWriteFileAtomic(SG_SLEEP_LEASE_DIR+L"\\100.txt",sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")+"|replacement-during-missing-check=true"));};
 at(0);assert(procs[100].ntCalls==0&&fixtureOpenHandles==0&&exists(100)&&owner.markerOwned&&g_sgWakeRecoveryPending);
 at(2500);assert(exists(100)&&owner.markerOwned&&procs[100].ntCalls==0);
 pass("P0 already-running cleanup cannot clear an owner whose marker reappeared during identity confirmation");
}
void fixtureRunPostReadReadyControls(const std::string& selected="readiness") {
    if (selected != "readiness-retirement") {
    for (unsigned mode = 0; mode < 2; ++mode) {
        onFileReadComplete = {};
        reset();
        marker(100);
        procs[100].depth = 3;
        sgBeginWakeGameRecovery(7);
        const auto exactPath = SG_SLEEP_LEASE_DIR + L"\\100.txt";
        const auto originalBytes = sgReadFile(exactPath);
        unsigned readsAfterOpen = 0;
        bool readBoundaryObserved = false;
        onProcessOpen = [&, mode](DWORD pid) {
            assert(pid == 100);
            onFileReadComplete = [&, mode](const std::wstring& path, const std::string& bytes) {
                if (path != exactPath) return;
                assert(bytes == originalBytes);
                // First: accepted's outer entryCurrent. Second: the actual
                // production drain's first Current, immediately before its NT.
                if (++readsAfterOpen != 2) return;
                onFileReadComplete = {};
                readBoundaryObserved = true;
                if (mode == 0) g_inputReady = false;
                else g_powerLifecycle = PowerLifecycle::Resuming;
            };
        };
        at(0);
        onFileReadComplete = {};
        assert(readBoundaryObserved && readsAfterOpen == 2);
        assert(procs[100].calls == 1 && procs[100].ntCalls == 0 && procs[100].depth == 3);
        assert(fixtureNtDispatches.empty() && fixtureOpenHandles == 0 && focusCalls == 0);
        assert(exists(100) && owner.markerOwned && g_sgWakeRecoveryPending);
        g_inputReady = true;
        g_powerLifecycle = PowerLifecycle::Ready;
        at(2499);
        assert(procs[100].ntCalls == 0 && g_sgWakeRecoveryPending);
        at(2500);
        assert(procs[100].calls == 2 && procs[100].ntCalls == 8 && procs[100].depth == 0);
        assert(fixtureOpenHandles == 0 && !exists(100) && !owner.markerOwned && !g_sgWakeRecoveryPending);
    }
    }
    // Same named group, two additional modes. The exact subselector skips
    // the earlier inner-read controls and executes only this retirement loop.
    for (unsigned mode = 0; mode < 2; ++mode) {
        onFileReadComplete = {};
        reset();
        marker(100);
        procs[100].depth = 3;
        sgBeginWakeGameRecovery(7);
        const auto exactPath = SG_SLEEP_LEASE_DIR + L"\\100.txt";
        const auto originalBytes = sgReadFile(exactPath);
        unsigned readsAfterOpen = 0;
        bool retirementReadObserved = false;
        onProcessOpen = [&, mode](DWORD pid) {
            assert(pid == 100);
            onFileReadComplete = [&, mode](const std::wstring& path, const std::string& bytes) {
                if (path != exactPath) return;
                assert(bytes == originalBytes);
                // #1: outer entryCurrent; #2..#9: eight production drain
                // Current checks; #10: the final marker/owner confirmation.
                if (++readsAfterOpen != 10) return;
                onFileReadComplete = {};
                retirementReadObserved = true;
                if (mode == 0) g_inputReady = false;
                else g_powerLifecycle = PowerLifecycle::Resuming;
            };
        };
        at(0);
        onFileReadComplete = {};
        assert(retirementReadObserved && readsAfterOpen == 10);
        assert(procs[100].calls == 1 && procs[100].ntCalls == 8 && procs[100].depth == 0);
        assert(fixtureOpenHandles == 0);
        const auto confirmations = std::count_if(facts.begin(), facts.end(),
            [](const auto& fact) { return fact.first == "sleep-game-recovery-confirmed"; });
        std::cout << "CONTROL readiness-retirement mode=" << (mode == 0 ? "input-not-ready" : "Resuming")
                  << " markerReadOrdinal=" << readsAfterOpen << " ntCalls=" << procs[100].ntCalls
                  << " markerPresent=" << exists(100) << " ownerRetained=" << owner.markerOwned
                  << " pending=" << g_sgWakeRecoveryPending.load() << " confirmations=" << confirmations << std::endl;
        assert((exists(100) && owner.markerOwned && g_sgWakeRecoveryPending && focusCalls == 0 && confirmations == 0)
            && "P0 readiness-retirement must retain marker/owner and publish no confirmation");
        assert(g_sgWakeRecovery.started == 0 && g_sgWakeRecovery.nextAttempt == 1);
        g_inputReady = true;
        g_powerLifecycle = PowerLifecycle::Ready;
        at(2499);
        assert(procs[100].calls == 1 && procs[100].ntCalls == 8 && g_sgWakeRecoveryPending);
        at(2500);
        assert(procs[100].calls == 2 && procs[100].ntCalls == 16 && procs[100].depth == 0);
        assert(fixtureOpenHandles == 0 && !exists(100) && !owner.markerOwned && !g_sgWakeRecoveryPending && focusCalls == 1);
    }
    onFileReadComplete = {};
    pass("P0 observable marker-read readiness loss stops the next NT and retains the original retry slot");
}
int fixtureRunPublicDispatchControls(){
 json rows=json::array();
 for(const ULONG code:{PBT_APMSUSPEND,PBT_APMQUERYSUSPEND}){
  for(const std::string stage:{"before-tick","process-open","handle-birth","per-nt"}){
   reset();marker(100);procs[100].depth=3;sgBeginWakeGameRecovery(7);
   const auto publish=[code]{g_sleepPowerDispatch.callback(code,1,[](const auto&){return true;},100);};
   if(stage=="before-tick")publish();
   else if(stage=="process-open")onProcessOpen=[&](DWORD){publish();};
   else if(stage=="handle-birth")onHandleIdentity=[&](DWORD){publish();};
   else onNtResume=[&](DWORD){publish();};
   at(0);onProcessOpen={};onHandleIdentity={};onNtResume={};
   const unsigned expected=stage=="per-nt"?1U:0U;
   const bool stopped=procs[100].ntCalls==static_cast<int>(expected)&&exists(100)&&owner.markerOwned&&focusCalls==0&&fixtureOpenHandles==0&&g_sleepPowerDispatch.pendingSuspend();
   rows.push_back({{"type",code},{"stage",stage},{"ntCalls",procs[100].ntCalls},{"expectedNtCalls",expected},{"retainedExactOwner",owner.markerOwned},{"focusCalls",focusCalls},{"openHandles",fixtureOpenHandles},{"pendingSuspend",g_sleepPowerDispatch.pendingSuspend()},{"stopped",stopped}});
   if(!stopped){std::cout<<"PUBLIC_DISPATCH_BOUNDARY "<<rows.back().dump()<<std::endl;return 2;}
   at(2500);
   if(procs[100].ntCalls!=static_cast<int>(expected)||g_sgWakeRecoveryPending){std::cout<<"PUBLIC_DISPATCH_CANCEL_FAIL "<<rows.back().dump()<<std::endl;return 2;}
   const auto pending=g_sleepPowerDispatch.pendingReplay();const auto claimed=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,2);g_sleepPowerDispatch.complete(claimed,2);
   generation=8;g_powerLifecycle=PowerLifecycle::Ready;const auto started=tick;sgBeginWakeGameRecovery(8);at(started);
   if(procs[100].depth!=0||exists(100)||owner.markerOwned||g_sgWakeRecoveryPending||fixtureOpenHandles){std::cout<<"PUBLIC_DISPATCH_SUCCESSOR_FAIL "<<rows.back().dump()<<std::endl;return 2;}
   rows.back()["successorGeneration"]=8;rows.back()["successorReturnedExactLease"]=true;
  }
 }
 std::cout<<"PUBLIC_DISPATCH_RESULT "<<json({{"status","PASS"},{"cases",rows},{"count",rows.size()},{"actualPublicDispatch",true},{"actualOriginalRecoveryHeader",true},{"nativeMainCallbackExecuted",false},{"realProcessResume",false},{"realSystemSleep",false}}).dump()<<std::endl;
 return 0;
}

int fixtureRunWakeAdmissionReplay(){
 json rows=json::array();
 struct Blocker {const char* name;bool dispatch;ULONG type;};
 const Blocker blockers[]={{"retry",false,0},{"suspend",true,PBT_APMSUSPEND},{"query-suspend",true,PBT_APMQUERYSUSPEND}};
 unsigned rejections=0;
 for(const auto& block:blockers){
  reset();marker(100);procs[100].threadTotal=97;
  const auto birth=owner.processCreated;
  const auto path=SG_SLEEP_LEASE_DIR+L"\\100.txt";
  const auto originalMarker=sgReadFile(path);
  if(block.dispatch)assert(g_sleepPowerDispatch.callback(block.type,1,[](const auto&){return true;},100));
  else retry=true;
  sgBeginWakeGameRecovery(7);at(0);
  const auto earlyStamp=g_sgLastGameWakeGeneration;
  const bool earlyClean=fixtureNtDispatches.empty()&&fixtureOpenHandles==0&&facts.empty()&&inventoryReads==0&&
      !g_sgWakeRecoveryPending&&g_sgWakeRecovery.entries.empty()&&owner.markerOwned&&
      owner.pid==100&&owner.processCreated==birth&&sgReadFile(path)==originalMarker;
  if(block.dispatch){
   assert(g_sleepPowerDispatch.pendingSuspend());
   const auto pending=g_sleepPowerDispatch.pendingReplay();
   assert(pending&&pending.type==block.type);
   const auto claimed=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,2);
   assert(claimed&&claimed.sequence==pending.sequence&&claimed.type==block.type);
   g_sleepPowerDispatch.complete(claimed,2);assert(!g_sleepPowerDispatch.pendingSuspend());
  }else retry=false;
  tick=10;sgBeginWakeGameRecovery(7);
  const bool lateExact=g_sgLastGameWakeGeneration==7&&g_sgWakeRecoveryPending&&
      g_sgWakeRecovery.generation==7&&g_sgWakeRecovery.started==10&&g_sgWakeRecovery.entries.size()==1&&
      g_sgWakeRecovery.entries.front().pid==100&&g_sgWakeRecovery.entries.front().created==birth&&
      g_sgWakeRecovery.entries.front().sleepOwnerGeneration==7&&fixtureNtDispatches.empty();
  if(!earlyClean||earlyStamp||!lateExact){
   std::cout<<"WAKE_ADMISSION_REPLAY_REJECT "<<json({{"block",block.name},{"dispatch",block.dispatch},{"pbt",block.type},{"earlyStamp",earlyStamp},
       {"earlyClean",earlyClean},{"lateQueuedExact",lateExact},{"ntCalls",fixtureNtDispatches.size()}}).dump()<<std::endl;
   ++rejections;
   continue;
  }
  at(10);
  const auto factOrNull=[](const char* name)->json{
   for(auto i=facts.rbegin();i!=facts.rend();++i)if(i->first==name)return i->second;
   return nullptr;
  };
  // The NT port returns success even at depth zero. The product drains up to
  // eight times, checking the same lease before each call.
  const std::vector<DWORD> exactDispatches(8,100);
  const auto attempt=factOrNull("sleep-game-recovery-attempt");
  const auto confirmation=factOrNull("sleep-game-recovery-confirmed");
  const auto complete=factOrNull("sleep-game-recovery-complete");
  const json terminal={{"ntPids",fixtureNtDispatches},{"ntCalls",procs[100].ntCalls},
      {"inventoryReads",inventoryReads},{"legacyEnumerations",legacyEnumerationCalls},
      {"depth",procs[100].depth},{"markerExists",exists(100)},{"owner",owner.markerOwned},
      {"pending",g_sgWakeRecoveryPending.load()},{"entries",g_sgWakeRecovery.entries.size()},
      {"openHandles",fixtureOpenHandles},{"attempt",attempt},{"confirmation",confirmation},{"complete",complete}};
  std::cout<<"WAKE_ADMISSION_REPLAY_TERMINAL "<<terminal.dump()<<std::endl;
  const bool retired=fixtureNtDispatches==exactDispatches&&procs[100].ntCalls==8&&inventoryReads==0&&
      legacyEnumerationCalls==0&&procs[100].depth==0&&!exists(100)&&!owner.markerOwned&&
      !g_sgWakeRecoveryPending&&g_sgWakeRecovery.entries.empty()&&fixtureOpenHandles==0&&
      attempt.is_object()&&attempt["pid"]==100&&attempt["processCreated"]==std::to_string(birth)&&
      attempt["threadStateKnown"]==true&&attempt["threads"]==97&&attempt["suspendedThreads"]==0&&
      confirmation.is_object()&&confirmation["pid"]==100&&confirmation["processCreated"]==std::to_string(birth)&&
      confirmation["markerRemoved"]==true&&complete.is_object()&&complete["pendingTargets"]==0;
  assert(retired);
  marker(101,true);
  const auto manualPath=SG_MANUAL_DIR+L"\\101.txt";
  const auto newManual=sgReadFile(manualPath);
  const auto factsBeforeDuplicate=facts.size();
  sgBeginWakeGameRecovery(7);at(10020);
  const bool duplicateQuiet=fixtureNtDispatches==exactDispatches&&exists(101,true)&&!newManual.empty()&&
      sgReadFile(manualPath)==newManual&&procs[101].depth==1&&fixtureOpenHandles==0&&
      facts.size()==factsBeforeDuplicate&&!g_sgWakeRecoveryPending;
  assert(duplicateQuiet);
  rows.push_back({{"block",block.name},{"dispatch",block.dispatch},{"pbt",block.type},{"earlyStamp",earlyStamp},{"earlyClean",earlyClean},{"sameGeneration",7},
      {"lateQueuedExact",lateExact},{"originalLeaseRetired",retired},{"duplicateLeftNewManualLease",duplicateQuiet}});
 }
 if(rejections)return 23;
 unsigned queuedFailures=0;
 for(const std::string stage:{"queued", "opened", "failed-dispatch", "partial-dispatch"}){
  reset();marker(100);procs[100].depth=3;
  const auto originalMarker=sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt");
  const auto birth=owner.processCreated;
  assert(sgBeginWakeGameRecovery(7)&&g_sgWakeRecoveryPending);
  const bool action=stage=="failed-dispatch"||stage=="partial-dispatch";
  if(stage=="queued"){retry=true;at(0);}
  else if(stage=="opened"){
   onProcessOpen=[](DWORD){assert(g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,1,[](const auto&){return true;},100));};
   at(0);onProcessOpen={};assert(g_sleepPowerDispatch.pendingSuspend());
   const auto pending=g_sleepPowerDispatch.pendingReplay();assert(pending);
   const auto claim=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,2);assert(claim);
   g_sleepPowerDispatch.complete(claim,2);assert(!g_sleepPowerDispatch.pendingSuspend());
  }else{
   if(stage=="failed-dispatch")procs[100].fail=1;
   else onNtResume=[](DWORD){retry=true;};
   at(0);retry=true;at(1);
  }
  const auto initialCalls=fixtureNtDispatches.size();
  assert(initialCalls==unsigned(action)&&fixtureOpenHandles==0&&!g_sgWakeRecoveryPending&&
      owner.markerOwned&&owner.processCreated==birth&&sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")==originalMarker);
  marker(101,true);const auto laterManual=sgReadFile(SG_MANUAL_DIR+L"\\101.txt");
  retry=false;tick=20;assert(sgBeginWakeGameRecovery(7));
  const bool resumed=action?!g_sgWakeRecoveryPending:(g_sgWakeRecoveryPending&&
      g_sgWakeRecovery.entries.size()==1&&g_sgWakeRecovery.entries.front().created==birth&&
      g_sgWakeRecovery.entries.front().markerText==originalMarker);
  at(20);at(10020);assert(fixtureOpenHandles==0&&!g_sgWakeRecoveryPending&&inventoryReads==0&&
      sgReadFile(SG_MANUAL_DIR+L"\\101.txt")==laterManual&&procs[101].ntCalls==0);
  const bool healed=action?(fixtureNtDispatches.size()==initialCalls&&exists(100)&&owner.markerOwned):
      (fixtureNtDispatches==std::vector<DWORD>(8,100)&&!exists(100)&&!owner.markerOwned&&procs[100].depth==0);
  std::cout<<"WAKE_POST_QUEUE_REPLAY "<<json({{"stage",stage},{"beforeCalls",initialCalls},
      {"afterCalls",fixtureNtDispatches.size()},{"expectedSuccessor",!action},
      {"correctContinuation",resumed&&healed},{"newManualUntouched",true}}).dump()<<std::endl;
  queuedFailures+=!(resumed&&healed);
 }
 if(queuedFailures)return 25;
 reset();marker(100);retry=true;sgBeginWakeGameRecovery(7);assert(!g_sgLastGameWakeGeneration);
 retry=false;++procs[100].created;sgBeginWakeGameRecovery(7);at(0);
 const bool reuseRefused=fixtureNtDispatches.empty()&&procs[100].depth==1&&fixtureOpenHandles==0&&
     !exists(100)&&!owner.markerOwned&&!g_sgWakeRecoveryPending&&inventoryReads==0&&
     lastFact("sleep-game-recovery-stale")["markerRemoved"]==true;
 assert(reuseRefused);
 std::cout<<"WAKE_ADMISSION_REPLAY_RESULT "<<json({{"status","PASS"},{"cases",rows},{"reusedPidUntouched",reuseRefused},
     {"actualRecoveryHeader",true},{"actualPublicDispatch",true},{"actualNativeDrain",true},
     {"realProcessResume",false},{"realSystemSleep",false}}).dump()<<std::endl;
 return 0;
}

int wmain(int argc,wchar_t** argv){assert(argc==2||argc==3);SG_DIR=argv[1];assert(fspath::path(SG_DIR).is_absolute());assert(SG_DIR.find(L"SleepResumeRetry-20261003")!=std::wstring::npos);SG_SLEEP_LEASE_DIR=SG_DIR+L"\\sleep";SG_MANUAL_DIR=SG_DIR+L"\\manual";sgEnsureMarkerDir(SG_SLEEP_LEASE_DIR);sgEnsureMarkerDir(SG_MANUAL_DIR);
 if(argc==3){
  const std::wstring selected=argv[2];if(selected==L"wake-admission-replay")return fixtureRunWakeAdmissionReplay();assert(selected==L"multi-target"||selected==L"per-nt"||selected==L"readiness"||selected==L"readiness-retirement"||selected==L"public-dispatch");
  const std::string name(selected.begin(),selected.end());if(name=="public-dispatch")return fixtureRunPublicDispatchControls();if(name=="readiness"||name=="readiness-retirement")fixtureRunPostReadReadyControls(name);else fixtureRunCancellationControls(name);
  std::cout<<"sleep wake recovery selected P0 control: "<<name<<" "<<passed<<" group PASS\n";return 0;
 }
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
 reset();marker(100);sgBeginWakeGameRecovery(7);procs[100].created++;window();assert(procs[100].calls==0&&!exists(100));fixtureAssertStaleSafety();++fixtureStaleBasicSubcases;fixtureRunStaleMarkerBoundaryCases(true);pass("reused PID is never resumed");
 reset();marker(100);sgBeginWakeGameRecovery(7);procs[100].exited=true;window();assert(procs[100].calls==0&&!exists(100));fixtureAssertStaleSafety();++fixtureStaleBasicSubcases;fixtureRunStaleMarkerBoundaryCases(false);pass("confirmed exit removes only stale original lease");
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
 // Exact recovery must not discover an unrelated eligible suspended process.
 for(unsigned exact=0;exact<4;++exact){
  reset();marker(100,exact==1);procs[200]={};enumeration={100,200};
  if(exact==2){owner.powerGeneration=6;auto path=SG_SLEEP_LEASE_DIR+L"\\100.txt";auto text=sgReadFile(path);text.replace(text.find("generation=7"),12,"generation=6");assert(sgWriteFileAtomic(path,text));}
  if(exact==3)procs[100].openError=ERROR_ACCESS_DENIED;
  sgBeginWakeGameRecovery(7);assert(inventoryReads==0&&g_sgWakeRecovery.entries.size()==1&&!g_sgWakeRecovery.entries[0].fallback);
  window();assert(inventoryReads==0&&procs[200].calls==0&&!exists(200));
  if(exact==3)assert(procs[100].calls==0&&exists(100)&&owner.markerOwned);
  else assert(procs[100].calls==1);
 }pass("exact sleep/manual/old-generation/temporarily-denied target batches never discover other PIDs");
 reset();marker(100);owner.powerGeneration=6;fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");procs[200]={};enumeration={100,200};
 sgBeginWakeGameRecovery(7);assert(inventoryReads==0&&g_sgWakeRecovery.entries.size()==1&&g_sgWakeRecovery.entries[0].sleepOwnerGeneration==6);
 window();assert(procs[100].calls==1&&procs[200].calls==0&&!owner.markerOwned);
 pass("lost old-generation marker rebuilds only the fixed original PID and birth");
 reset();marker(100);fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");procs[100].openError=ERROR_ACCESS_DENIED;procs[200]={};enumeration={100,200};
 sgBeginWakeGameRecovery(7);assert(inventoryReads==0&&owner.markerOwned&&procs[100].calls==0&&procs[200].calls==0);
 pass("known owner with unavailable identity remains retained without broad recovery");
 reset();markerDirectoryUnknown=true;procs[100]={};enumeration={100};sgBeginWakeGameRecovery(7);
 assert(inventoryReads==0&&!g_sgWakeRecoveryPending&&procs[100].calls==0&&!exists(100));
 pass("unavailable marker evidence cannot authorize global discovery");
 for(auto state:{ymcc::processInventory::Status::Unavailable,ymcc::processInventory::Status::Failed}){
  reset();procs[100]={};enumeration={100};ymcc::processInventory::inventoryStatus=state;
  sgBeginWakeGameRecovery(7);assert(inventoryReads==1&&!g_sgWakeRecoveryPending&&!exists(100)&&procs[100].calls==0);
 }pass("unavailable/failed shared topology rejects even present rows without resuming");
 reset();procs[100]={};enumeration={100};sgBeginWakeGameRecovery(7);
 assert(inventoryReads==1&&g_sgWakeRecovery.entries.size()==1&&g_sgWakeRecovery.entries[0].fallback);
 window();assert(inventoryReads==1&&legacyEnumerationCalls==0&&procs[100].calls==1);
 pass("exception-only fallback borrows one shared complete topology and never rescans during attempts");
 std::cout<<"sleep wake recovery production-header selftest: "<<passed<<" groups PASS\n";
}
"""

if a.p0_case == 'public-dispatch':
    masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m.group()),source,flags=re.S)
    def exact_function(name):
        m=re.search(r'\bstatic (?:bool|void) '+name+r'\([^;{}]*\)\s*\{',masked);assert m,name
        first=masked.index('{',m.start());end=first+1;depth=1
        while depth:depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
        return source[m.start():end]
    cpp=cpp.replace('__ACTUAL_WORK_NOTIFY__',exact_function('sgNotifyWork'))
    cpp=cpp.replace('__ACTUAL_PRODUCTION_DRAIN_FUNCTIONS__','template<class Current>\n'+exact_function('sgDrainProcessSuspendCountIfCurrent')+'\n'+exact_function('sgDrainProcessSuspendCount'))
else:
    # Exercise the actual sgRealWake dispatch against the same production batch.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', lambda m:' '*len(m.group()),source,flags=re.S)
    m = re.search(r'\bstatic void sgRealWake\([^;{}]*\)\s*\{', masked)
    opening=masked.index('{',m.start());depth=1;end=opening+1
    while depth:
        depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
    wake_dispatch=source[m.start():end]
    hibernate_match = re.search(r'\bstatic SgResumeResult sgResumeHibernateSleepTarget\([^;{}]*\)\s*\{', masked)
    hibernate_opening=masked.index('{',hibernate_match.start());hibernate_depth=1;hibernate_end=hibernate_opening+1
    while hibernate_depth:
        hibernate_depth+=(masked[hibernate_end]=='{')-(masked[hibernate_end]=='}');hibernate_end+=1
    hibernate_dispatch=source[hibernate_match.start():hibernate_end]
    marker_match = re.search(r'\bstatic bool sgReadResumeMarkerChecked\([^;{}]*\)\s*\{', masked)
    marker_opening=masked.index('{',marker_match.start());marker_depth=1;marker_end=marker_opening+1
    while marker_depth:
        marker_depth+=(masked[marker_end]=='{')-(masked[marker_end]=='}');marker_end+=1
    marker_dispatch=source[marker_match.start():marker_end]
    wake_boundaries=r"""
    enum class SgSleepMode{S3,S4};
    struct Task{ULONGLONG generation=7;SgSleepMode mode=SgSleepMode::S3;} g_sgTask;
    bool g_sgRepairEligible=true,g_sgRetryInProgress=false;
std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLastPowerButtonWakeEventFileTime{0},g_sgLatestUserSleepEntryFileTime{0},g_sgOriginalPowerButtonWakeFileTime{0},g_sgClosedSleepIntentFileTime{0};
std::atomic<bool> g_sgEntryHandoffPending{false};
ULONGLONG g_sgSleepTriggerTick=0;
bool fixtureClassificationHeld=false,fixtureClassificationObserved=false;
bool sgResumeClassificationHeld(unsigned long long){return fixtureClassificationHeld;}
bool sgResumeClassificationObserved(unsigned long long){return fixtureClassificationObserved;}
    std::atomic<bool> g_sgSleepCycleActive{true};
    int inlineResumeCalls=0;
    bool sgNonUserWakeGuardEnabled(){return false;}
    bool sgExternalDeviceWakeIntentAge(ULONGLONG&){return false;}
    void stopPowerResumeWatchdog(){}
    void sgStopSleepRetry(bool=false){retry=false;}
    struct SgResumeResult{int count=0;};
    SgResumeResult sgResumeSleepTarget(ULONGLONG,bool){++inlineResumeCalls;return {};}
    SgResumeResult sgResumeSleepTargetUnlocked(ULONGLONG ownerGeneration,bool focus,ULONGLONG wakeGeneration){assert(ownerGeneration==owner.powerGeneration&&!focus&&wakeGeneration==generation);++inlineResumeCalls;return {};}
    template<class... T>void traceLog(const char*,T...){}
    """
    # The extracted reader uses real files in the enforced owned root. Its exact
    # production text must not consume the process-only CloseHandle mock counter.
    intent_match=re.search(r'\bstatic bool sgUserWakeCancelsSleepIntent\([^;{}]*\)\s*\{',masked)
    assert intent_match
    intent_open=masked.index('{',intent_match.start());intent_end=intent_open+1;intent_depth=1
    while intent_depth:intent_depth+=(masked[intent_end]=='{')-(masked[intent_end]=='}');intent_end+=1
    wake_boundaries+='\n'+source[intent_match.start():intent_end]+'\n'
    cpp=cpp.replace('int passed=0;',wake_boundaries+'\n#undef CloseHandle\n'+marker_dispatch+'\n#define CloseHandle fakeClose\n'+hibernate_dispatch+'\n'+wake_dispatch+'\nint passed=0;')
    cpp=cpp.replace(' generation=7;tick=0;', ' g_sleepRecoveryAutoEnabled=false;g_sleepRecoveryAutoPending=false;g_sleepRecoveryAutoConfigEpoch=1;g_sgTask={};g_sgRepairEligible=true;g_sgRetryInProgress=false;g_sgSleepCycleActive=true;inlineResumeCalls=0;generation=7;tick=0;')
    if a.p0_case == 'wake-admission-replay':
        replay_anchor=' if(rejections)return 23;'
        assert cpp.count(replay_anchor)==1
        cpp=cpp.replace(replay_anchor,replay_anchor+r'''
 reset();marker(100);procs[100].threadTotal=97;retry=true;
 const auto entryMarker=sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt");
 sgRealWake("resume_suspend",7);at(0);
 assert(g_sgLastGameWakeGeneration==0&&!g_sgWakeRecoveryPending&&fixtureNtDispatches.empty()&&
        inlineResumeCalls==0&&focusCalls==0&&facts.empty()&&owner.markerOwned&&
        sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")==entryMarker);
 retry=false;tick=10;sgRealWake("resume_suspend",7);
 assert(g_sgWakeRecoveryPending&&g_sgWakeRecovery.entries.size()==1&&
        g_sgWakeRecovery.entries.front().pid==100&&g_sgWakeRecovery.entries.front().created==owner.processCreated&&
        fixtureNtDispatches.empty()&&inlineResumeCalls==0);
 at(10);
 const auto entryAttempt=lastFact("sleep-game-recovery-attempt");
 assert(fixtureNtDispatches==std::vector<DWORD>(8,100)&&entryAttempt["threads"]==97&&
        entryAttempt["suspendedThreads"]==0&&procs[100].depth==0&&!exists(100)&&!owner.markerOwned&&
        !g_sgWakeRecoveryPending&&fixtureOpenHandles==0&&inventoryReads==0&&inlineResumeCalls==0&&
        lastFact("sleep-game-recovery-confirmed")["markerRemoved"]==true);
 std::cout<<"WAKE_ADMISSION_REPLAY_ENTRY "<<json({{"source","resume_suspend"},{"earlyActions",0},
     {"sameGeneration",7},{"ntCalls",fixtureNtDispatches.size()},{"threadCounts",entryAttempt},
     {"originalLeaseRetired",true},{"inlineResumeCalls",inlineResumeCalls}}).dump()<<std::endl;
 unsigned wrong=0;
 for(bool dispatch:{false,true}){
  reset();marker(100);procs[100].threadTotal=97;fixtureAutoArmCalls=0;
  const auto birth=owner.processCreated;
  const auto saved=sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt");
  unsigned checks=0;
  fixtureRetryCheck=[&]{if(++checks!=2)return;
   if(dispatch)assert(g_sleepPowerDispatch.callback(PBT_APMSUSPEND,1,[](const auto&){return true;},100));
   else retry=true;
  };
  sgRealWake("resume_suspend",7);
  fixtureRetryCheck={};
  const bool gateClosed=dispatch?g_sleepPowerDispatch.pendingSuspend():retry;
  assert(checks>=2&&gateClosed&&g_sgLastGameWakeGeneration==0&&!g_sgWakeRecoveryPending&&
      g_sgWakeRecovery.entries.empty()&&fixtureNtDispatches.empty()&&fixtureOpenHandles==0&&
      inventoryReads==0&&facts.empty()&&g_sgTask.generation==7&&owner.markerOwned&&
      owner.processCreated==birth&&sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")==saved);
  const auto earlyArms=fixtureAutoArmCalls;
  if(dispatch){
   const auto p=g_sleepPowerDispatch.pendingReplay();assert(p&&p.type==PBT_APMSUSPEND);
   const auto claim=g_sleepPowerDispatch.forwarded(p.sequence,p.type,2);assert(claim);
   g_sleepPowerDispatch.complete(claim,2);assert(!g_sleepPowerDispatch.pendingSuspend());
  }else retry=false;
  tick=10;sgRealWake("resume_suspend",7);
  assert(g_sgWakeRecoveryPending&&g_sgWakeRecovery.entries.size()==1&&g_sgLastGameWakeGeneration==7&&
      g_sgWakeRecovery.entries.front().pid==100&&g_sgWakeRecovery.entries.front().created==birth&&
      fixtureAutoArmCalls==earlyArms+1&&fixtureNtDispatches.empty());
  at(10);
  assert(fixtureNtDispatches==std::vector<DWORD>(8,100)&&procs[100].depth==0&&!exists(100)&&
      !owner.markerOwned&&!g_sgWakeRecoveryPending&&fixtureOpenHandles==0&&inventoryReads==0&&
      lastFact("sleep-game-recovery-confirmed")["markerRemoved"]==true);
  std::cout<<"WAKE_ADMISSION_HANDOFF "<<json({{"newGate",dispatch?"suspend":"retry"},
      {"earlyAutoArms",earlyArms},{"laterAutoArms",fixtureAutoArmCalls-earlyArms},
      {"originalLeaseRetired",true},{"generation",7},{"earlyStamped",false}}).dump()<<std::endl;
  wrong+=earlyArms!=0;
 }
 if(wrong)return 24;
''')
    cpp=cpp.replace(' std::cout<<"sleep wake recovery production-header selftest:',r"""
     reset();marker(100);sgRealWake("resume_suspend",7);assert(g_sgWakeRecoveryPending&&procs[100].calls==0);sgRealWake("resume_auto",7);sgRealWake("monitor_on",7);assert(inlineResumeCalls==0&&exists(100)&&procs[100].calls==0);window();assert(procs[100].calls==1);pass("late automatic/monitor wake after explicit dispatch cannot bypass delayed batch");
     reset();marker(100);g_sgTask={};sgRealWake("resume_auto",7);assert(inlineResumeCalls==0&&procs[100].calls==0&&exists(100)&&!g_sgWakeRecoveryPending);pass("automatic wake with lost SleepTask does not authorize inline/global thaw");
     reset();marker(100);generation=8;sgBeginWakeGameRecovery(8);sgRealWake("resume_hibernate",8);
     assert(inlineResumeCalls==0&&procs[100].calls==0&&g_sgWakeRecoveryPending&&g_sgWakeRecovery.generation==8&&exists(100));
     window();assert(procs[100].calls==1&&!exists(100)&&!owner.markerOwned&&!g_sgWakeRecoveryPending);
     pass("S4 delegates to the existing current recovery batch and the inherited owner is retired without recreation");
     reset();marker(100);sgBeginWakeGameRecovery(7);generation=8;sgRealWake("resume_hibernate",8);
     assert(inlineResumeCalls==1&&!g_sgWakeRecoveryPending&&procs[100].calls==0&&exists(100)&&owner.markerOwned);
     at(10000);assert(procs[100].calls==0&&exists(100));
     pass("S4 cancels the old actual recovery batch before dispatching its exact retained owner to the strict resume port");
     reset();marker(100);generation=8;sgBeginWakeGameRecovery(8);g_sgWakeRecovery.entries.front().done=true;sgRealWake("resume_hibernate",8);
     assert(inlineResumeCalls==1&&g_sgWakeRecoveryPending&&g_sgWakeRecovery.entries.front().done&&exists(100));
     at(10000);assert(procs[100].calls==0&&exists(100));
     pass("S4 does not delegate to a done entry in the actual current recovery batch");
     std::cout<<"sleep wake recovery production-header selftest:""")

    def production_function(name):
        pattern=re.search(r'\bstatic (?:bool|void) '+name+r'\([^;{}]*\)\s*\{',masked)
        assert pattern,name
        first=masked.index('{',pattern.start());pos=first+1;depth=1
        while depth:
            depth+=(masked[pos]=='{')-(masked[pos]=='}');pos+=1
        return source[pattern.start():pos]
    drain_if_current=production_function('sgDrainProcessSuspendCountIfCurrent')
    drain_plain=production_function('sgDrainProcessSuspendCount')
    assert 'if (!current()) break;' in drain_if_current
    assert 'attempt < 8' in drain_if_current
    assert '[] { return true; }' in drain_plain
    cpp=cpp.replace('__ACTUAL_PRODUCTION_DRAIN_FUNCTIONS__',
        'template<class Current>\n'+drain_if_current+'\n'+drain_plain)
    cpp=cpp.replace('__ACTUAL_WORK_NOTIFY__',production_function('sgNotifyWork'))
    # Current actual queue reader closure; original wait assertions stay below.
    if 'struct SgRetainedWorkDelivery {' in source:
        returned=(r/'native/sleep_returned_pause_native.inc').read_text('utf-8-sig')
        def queue_definition(text,pattern):
            masked_text=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m.group()),text,flags=re.S)
            matches=list(re.finditer(pattern,masked_text));assert len(matches)==1,pattern
            opening=masked_text.index('{',matches[0].start());pos=opening+1;depth=1
            while depth:depth+=(masked_text[pos]=='{')-(masked_text[pos]=='}');pos+=1
            return text[matches[0].start():pos]
        actual_item=queue_definition(source,r'\bstruct SgWorkItem\s*\{')+';'
        original_item='struct SgWorkItem { SgWork kind; unsigned long long generation; };'
        assert cpp.count(original_item)==1;cpp=cpp.replace(original_item,actual_item)
        cell=queue_definition(source,r'\bstruct SgRetainedWorkDelivery\s*\{')+';'
        queue_globals=[line for line in source.splitlines() if line.startswith('static std::array<SgRetainedWorkDelivery,') or line.startswith('static unsigned long long g_sgRetainedWorkOrder=')]
        assert len(queue_globals)==2
        cpp=cpp.replace('#include <atomic>','#include <atomic>\n#include <array>',1)
        anchor='std::mutex g_sgWorkMx;std::deque<SgWorkItem> g_sgWorkQ;'
        assert cpp.count(anchor)==1;cpp=cpp.replace(anchor,anchor+'\n'+cell+'\n'+'\n'.join(queue_globals))
        # Explicit UTC input port, never a Windows clock query or action permit.
        queue_helpers='ULONGLONG sgNowFileTime(){return 100000ULL;}\n'
        for name in ['sgOrdinaryPauseCurrent','sgReturnedPauseCurrent','sgRetainedWorkDeliveryPendingLocked','sgPruneRetainedWorkDeliveryLocked','sgTakeWorkDeliveryLocked']:
            queue_helpers+=queue_definition(returned,r'\bstatic (?:bool|void) '+name+r'\([^;{}]*\)\s*(?:noexcept\s*)?\{')+'\n'
        cpp=cpp.replace('int passed=0;',queue_helpers+'\nint passed=0;')
    cpp=cpp.replace('int passed=0;',production_function('sgWaitForWork').replace('sleepRecoveryAutoWaitMs(', 'sleepFixtureAutoWaitMs(')+'\nint passed=0;')
    cpp=cpp.replace(' std::cout<<"sleep wake recovery production-header selftest:',r'''
     reset();assert(sgWakeGameRecoveryWaitMs(tick)==~ULONGLONG{0});marker(100);procs[100].fail=10;sgBeginWakeGameRecovery(7);
     unsigned deadlineWakes=0;while(g_sgWakeRecoveryPending){const auto delay=sgWakeGameRecoveryWaitMs(tick);assert(delay!=~ULONGLONG{0});tick+=delay;sgWakeGameRecoveryTick();assert(++deadlineWakes<=5);}
     assert(deadlineWakes==5&&procs[100].calls==5&&tick==10000);pass("actual deadline scheduling needs five wakes for the unchanged ten-second five-attempt window, formerly 200 polling wakes");
     reset();marker(100);sgBeginWakeGameRecovery(7);g_inputReady=false;
     assert(sgWakeGameRecoveryWaitMs(tick)==250);tick=9990;assert(sgWakeGameRecoveryWaitMs(tick)==10);tick=10000;assert(sgWakeGameRecoveryWaitMs(tick)==0);
     g_exitRequested=true;assert(sgWakeGameRecoveryWaitMs(tick)==0);sgWakeGameRecoveryTick();assert(!g_sgWakeRecoveryPending);pass("actual readiness fallback is bounded and cancellation has no polling delay");
     reset();g_sgWorkStop=false;g_sgWorkQ.clear();g_sgWorkWakeRevision=0;
     {std::promise<void> calculated,release;auto releaseWait=release.get_future().share();
      workDelay=[&](ULONGLONG){calculated.set_value();releaseWait.wait();return ~ULONGLONG{0};};
      auto waiter=std::async(std::launch::async,[]{SgWorkItem item{};return __FIXTURE_WAIT_FOR_WORK__;});
      assert(calculated.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
      sgNotifyWork();release.set_value();assert(waiter.wait_for(std::chrono::seconds(2))==std::future_status::ready&&waiter.get());workDelay={};
     }pass("actual production wait retains a completion arriving between deadline computation and blocking");
     {std::promise<void> calculated;workDelay=[&](ULONGLONG){calculated.set_value();return ~ULONGLONG{0};};
      auto waiter=std::async(std::launch::async,[]{SgWorkItem item{};return __FIXTURE_WAIT_FOR_WORK__;});
      assert(calculated.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
      assert(waiter.wait_for(std::chrono::milliseconds(120))==std::future_status::timeout);
      {std::lock_guard<std::mutex> lock(g_sgWorkMx);g_sgWorkStop=true;}g_sgWorkCv.notify_all();
      assert(waiter.wait_for(std::chrono::seconds(2))==std::future_status::ready&&!waiter.get());workDelay={};g_sgWorkStop=false;
     }pass("actual idle wait owns no repeating timeout and wakes immediately for shutdown");
     std::cout<<"sleep wake recovery production-header selftest:''')
    # Append controls only after all original C++ groups were assembled.
    cpp=cpp.replace(' std::cout<<"sleep wake recovery production-header selftest:',
                    ' fixtureRunCancellationControls();\n fixtureRunPostReadReadyControls();\n std::cout<<"sleep wake recovery production-header selftest:')
# The canceled-owner path depends on these original main functions in every
# generated mode. Bind their bodies once; OS/process ports remain explicit.
marker_mask=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m.group()),source,flags=re.S)
def original_marker_function(name):
    m=re.search(r'\bstatic bool '+name+r'\([^;{}]*\)\s*\{',marker_mask);assert m,name
    first=marker_mask.index('{',m.start());end=first+1;depth=1
    while depth:depth+=(marker_mask[end]=='{')-(marker_mask[end]=='}');end+=1
    return source[m.start():end]
for marker_name in ['sgReadResumeMarkerChecked','nativeValveValidateOwnedSuspendedTarget']:
    if not re.search(r'\bstatic bool '+marker_name+r'\([^;{}]*\)\s*\{',cpp):
        original_body=original_marker_function(marker_name)
        if marker_name=='sgReadResumeMarkerChecked':cpp+='\n#undef CloseHandle\n'+original_body+'\n#define CloseHandle fakeClose\n'
        else:cpp+='\n'+original_body+'\n'
current_headers=[]
current_wake_ports=[]
if 'sgAcquireResumeFiles(' in h:
    start='template<class Current>\nstatic ymcc::sleepLegacyResume::ResumeLease sgAcquireResumeFiles('
    assert source.count(start)==1
    first=source.index(start);end=source.index('\n}\n',first)+2
    # Real files under the fixture root and distinct in-memory process handles.
    current_headers.append(r'''
static constexpr uintptr_t fixtureProcessHandleTag=0x100000000000ULL;
BOOL fixtureCloseResumeLeaseHandle(HANDLE value);
#define CloseHandle fixtureCloseResumeLeaseHandle
#include "sleep_legacy_resume_journal.h"
#undef CloseHandle
''')
    current_wake_ports.extend([source[first:end],r'''
BOOL fixtureCloseResumeLeaseHandle(HANDLE value) {
 if((reinterpret_cast<uintptr_t>(value)&fixtureProcessHandleTag)!=0)return fakeClose(value);
 return releaseRealResumeLeaseHandle(value);
}
'''])
    cpp=cpp.replace('static_cast<uintptr_t>(pid));}', 'fixtureProcessHandleTag|static_cast<uintptr_t>(pid));}')
    cpp=cpp.replace('void releaseMarker(HANDLE h){', 'BOOL releaseRealResumeLeaseHandle(HANDLE value){return CloseHandle(value);}\nvoid releaseMarker(HANDLE h){')
if 'postWakeGameRecoveryStatus(' in h:
    current_wake_ports.append('static void postWakeGameRecoveryStatus(ULONGLONG,bool,const std::string&,size_t,ptrdiff_t) noexcept {++emitCalls;}')
if 'sgPrepareWakeRecoveryForPauseUnlocked(rootPid, requestCurrent)' in source:
    cpp=cpp.replace('sgPrepareWakeRecoveryForPauseUnlocked(100)', 'sgPrepareWakeRecoveryForPauseUnlocked(100, []{return true;})')
if 'g_startupResumeDelivery.runnable()' in source:
    current_headers.append('#include "startup_resume_delivery.h"')
    cpp=cpp.replace('__FIXTURE_STARTUP_RESUME_DELIVERY__','static ymcc::sleepStartupResume::Delivery g_startupResumeDelivery;')
    cpp=cpp.replace('__FIXTURE_WAIT_FOR_WORK__','[&]{std::optional<ymcc::sleeppower::SharedPowerV1> startup;return sgWaitForWork(item,startup);}()')
cpp=cpp.replace('__FIXTURE_STARTUP_RESUME_DELIVERY__','').replace('__FIXTURE_WAIT_FOR_WORK__','sgWaitForWork(item)')
cpp=cpp.replace('__FIXTURE_CURRENT_SLEEP_HEADERS__','\n'.join(current_headers)).replace('__FIXTURE_CURRENT_WAKE_PORTS__','\n'.join(current_wake_ports))
if 'userRepairPower' in h:
    footer=' std::cout<<"sleep wake recovery production-header selftest:'
    assert cpp.count(footer)==1
    cpp=cpp.replace(footer,r'''
     for(const bool beforeNt:{false,true}){
      reset();marker(100);const auto prior=sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt");
      SgWakeRecoveryEntry entered{SG_SLEEP_LEASE_DIR,100,procs[100].created,prior};entered.sleepOwnerGeneration=7;
      const auto power=fixtureSleepRepairAdmission();
      assert(power&&sgBeginUserSleepGameRecoveryUnlocked(7,{entered},"public-power-revoked",0,power));
      bool hookRan=false;
      if(beforeNt)onProcessOpen=[&](DWORD){hookRan=true;g_sharedPowerRevocations.revoke();};
      else g_sharedPowerRevocations.revoke();
      at(0);onProcessOpen={};
      assert(hookRan==beforeNt&&!power->current()&&fixtureNtDispatches.empty()&&owner.markerOwned&&
             sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")==prior&&!g_sgWakeRecoveryPending&&
             !g_sgWakeRecovery.waitingForNextWake);
     }
     pass("public manual repair revocation rejects queued work and a late process-open without retiring its lease");
'''+footer)
cpp_path=out/'sleep_wake_resume_native_selftest.cpp'; cpp_path.write_text(cpp,'utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
exe=out/'sleep_wake_resume_native_selftest.exe'; cmd=out/'run-wake-recovery-selftest.cmd'
toolchain_js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain_prefix=subprocess.check_output(['node','--input-type=module','-e',toolchain_js,str(vs)],text=True,encoding='utf-8')
toolchain_prefix+=f'where cl.exe > "{out / "resolved-cl.txt"}"\r\nif errorlevel 1 exit /b 1\r\n'
p0_argument = (' "'+a.p0_case+'"') if a.p0_case else ''
source_options = ('/sourceDependencies "'+str(out/'dependencies.json')+'" ') if a.p0_case in ('public-dispatch', 'wake-admission-replay') else ''
link_options = '/VERBOSE:LIB ' if a.p0_case in ('public-dispatch', 'wake-admission-replay') else ''
journal_lib=' crypt32.lib' if '#include "sleep_legacy_resume_journal.h"' in cpp else ''
cmd.write_text(toolchain_prefix+f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 {source_options}"{cpp_path}" /I"{r / "native"}" /I"{r / "deps/json"}" /Fo"{out / "sleep_wake_resume_selftest.obj"}" /Fe"{exe}" /link {link_options}advapi32.lib{journal_lib}\r\nif errorlevel 1 exit /b 1\r\n"{exe}" "{out / "owned-fixture-state"}"{p0_argument}\r\n',encoding='ascii')
res=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'wake-recovery-selftest.log').write_bytes(res.stdout);print(res.stdout.decode('utf-8',errors='replace'));raise SystemExit(res.returncode)
