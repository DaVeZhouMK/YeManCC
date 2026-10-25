"""Compile actual diagnostic/feedback IPC headers against owned-process mocks.
No sleep, live process enumeration/resume, device writes or Steam operations.
The wake suite is run first to generate the shared production fixture.
"""
from pathlib import Path
import argparse, subprocess, re, hashlib, json

def extract_native_function(text, name):
    """Unique complete C++ definition; comments, strings and raw strings are masked."""
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<rawtag>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=rawtag)"|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27', re.S)
    masked = tokens.sub(lambda match: ''.join('\n' if c == '\n' else ' ' for c in match.group()), text)
    definitions = list(re.finditer(r'\bstatic\s+void\s+' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', masked))
    if len(definitions) != 1:
        raise RuntimeError('expected one complete native definition: ' + name)
    match = definitions[0]; opening = masked.index('{', match.start()); depth = 1; end = opening + 1
    while depth and end < len(masked):
        depth += (masked[end] == '{') - (masked[end] == '}'); end += 1
    if depth:
        raise RuntimeError('unterminated native definition: ' + name)
    return text[match.start():end]

# The extractor must reject ambiguous/incomplete definitions and ignore decoys.
_example = 'static void target(); /* static void target(){} */ static void target(){const char* s=R"tag(} static void target(){)tag"; if(true){}} static void unrelated(){}'
assert extract_native_function(_example, 'target').endswith('if(true){}}')
for _bad in ['static void target(){', 'static void target(){} static void target(){}']:
    try: extract_native_function(_bad, 'target')
    except RuntimeError: pass
    else: raise AssertionError('native extraction accepted incomplete/ambiguous definition')

p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=Path('../../Build/Validation/SleepResumeRetry-20261003/Round7'))
out=p.parse_args().output.resolve();out.mkdir(parents=True,exist_ok=True);root=Path.cwd()
result=subprocess.run(['python','tools/sleep_wake_resume_selftest.py','--output',str(out)],capture_output=True)
if result.returncode: print(result.stdout.decode(errors='replace'));raise SystemExit(result.returncode)
cpp=(out/'sleep_wake_resume_native_selftest.cpp').read_text('utf-8')
cpp=cpp.replace('void sleepRecoveryAutoArm(ULONGLONG){}','static void sleepRecoveryAutoArm(ULONGLONG);',1)
cpp=cpp.replace('#include <windows.h>','#include <windows.h>\n#include <shlobj.h>\n#include <sstream>\n#include <functional>\n#include "sleep_recovery_diagnostics.h"',1)

# This also protects a direct original fixture run without the adapter's /FI guard.
# It intercepts only OS ports. The production queryThreads algorithm still returns unknown.
query_ports=r'''
#include <cstdio>
#include <cstdlib>
#include <tlhelp32.h>
#ifndef GetProcAddress
static unsigned sleepFixtureUnknownQueries=0,sleepFixtureDeniedOsCalls=0;
static FARPROC WINAPI sleepFixtureResolve(HMODULE,LPCSTR name){
 if(name&&reinterpret_cast<uintptr_t>(name)>65535&&std::strcmp(name,"NtQueryInformationThread")==0){++sleepFixtureUnknownQueries;return nullptr;}
 ++sleepFixtureDeniedOsCalls;std::fputs("SLEEP_API_UNEXPECTED_DYNAMIC_OS_PORT\n",stderr);ExitProcess(91);
}
static HANDLE WINAPI sleepFixtureNoSnapshot(DWORD,DWORD){++sleepFixtureDeniedOsCalls;std::fputs("SLEEP_API_REAL_ENUMERATION_REFUSED\n",stderr);ExitProcess(91);}
static HANDLE WINAPI sleepFixtureNoThread(DWORD,BOOL,DWORD){++sleepFixtureDeniedOsCalls;std::fputs("SLEEP_API_REAL_THREAD_REFUSED\n",stderr);ExitProcess(91);}
#define GetProcAddress sleepFixtureResolve
#define CreateToolhelp32Snapshot sleepFixtureNoSnapshot
#define OpenThread sleepFixtureNoThread
#endif
'''
assert cpp.count('#include "wake_game_resume.h"')==1
cpp=cpp.replace('#include "wake_game_resume.h"',query_ports+'\n#include "wake_game_resume.h"',1)

mocks=r'''
std::atomic<bool> g_sleepRecoveryOutputWanted{true};
#include "input_host_transaction_mutex.h"
#include "input_host_lifecycle_claim.h"
using InputHostMutex=ymcc::inputHost::TransactionMutex<>;
InputHostMutex g_inputHostMx;std::mutex g_sgFactMx;
std::vector<std::string> g_sgFacts;
std::atomic<ULONGLONG> g_inputHostEpochMirror{99},g_wakeKeptTargetGeneration{0};
ULONGLONG g_inputHostBoundPowerGeneration=7;
// Mock only the native owner proof snapshot. Actual main helper is covered by full-pipe cases.
ULONGLONG inputHostWakeBindingProofLocked(){return g_wakeKeptTargetGeneration.load();}
bool g_inputHostPrepared=true,g_inputHostReleaseQuarantined=false,g_inputHostTransportFault=false,hostAlive=true;
std::atomic<bool> g_inputHostFaultHoldsStart{false},g_inputHostPowerAdmissionRevoked{false};
struct {bool isolated=false;} g_aiFanMockSession;
bool writesAllowed=true;
std::vector<std::string> queuedInput;
bool hardwareWriteAllowed(){return writesAllowed&&g_powerLifecycle==PowerLifecycle::Ready&&!g_aiFanMockSession.isolated;}
bool inputHostProcessAliveLocked(){return hostAlive;}
bool inputHostQueueLifecycleWork(const char* reason){queuedInput.push_back(reason);return true;}
bool nativeValveReadCurrentTarget(NativeDetectedGame*){return false;}
std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
std::wstring sofRegSZ(HKEY,const wchar_t*,const wchar_t*){return {};}
void sofRegDWORD(HKEY,const wchar_t*,const wchar_t*,DWORD* d){*d=0;}
json steamLiveRequest(DWORD,json){return {{"ok",false},{"reason","steam-unavailable"},{"mutated",false}};}
static std::wstring getKnownFolder(REFKNOWNFOLDERID){return SG_DIR;}
std::wstring yemancc_data_dir(){return SG_DIR;}
std::wstring SG_FACT_LOG;
std::set<HANDLE> apiFileHandles;
#undef CloseHandle
HANDLE apiCreateFileW(LPCWSTR file,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD attrs,HANDLE tmpl){
 const auto h=::CreateFileW(file,access,share,security,disposition,attrs,tmpl);if(h!=INVALID_HANDLE_VALUE)apiFileHandles.insert(h);return h;}
BOOL apiClose(HANDLE h){if(apiFileHandles.erase(h))return ::CloseHandle(h);return fakeClose(h);}
#define CreateFileW apiCreateFileW
#define CloseHandle apiClose
#include "sleep_recovery_api.h"
std::vector<std::function<void()>> autoJobs;
bool autoQueueAllowed=true,preferenceSaveAllowed=true;
bool poolSubmit(std::function<void()> job){if(!autoQueueAllowed)return false;autoJobs.push_back(std::move(job));return true;}
bool ymSettingsPatchSection(const char*,const json&){return preferenceSaveAllowed;}
bool sleepSteamRecoveryAllows(ULONGLONG gen,ULONGLONG epoch){return g_sleepRecoveryAutoEnabled&&epoch!=0&&epoch==g_sleepRecoveryAutoConfigEpoch&&gen==currentPowerGeneration()&&!g_exitRequested;}
bool sgModernStandbyRepairSupported(){return false;}
ULONGLONG sgNowFileTime(){return 134362144573220276ULL+GetTickCount64()*10000;}
namespace SleepSteamRecovery {
bool sameClientEvidence(const json& a,const json& b){return a==b;}
struct Captured {ULONGLONG generation=0,configEpoch=0,boundaryGeneration=0,powerBoundaryFileTime=0;};
Captured capturedFor(ULONGLONG){return {};}
Captured wakeBoundaryFor(ULONGLONG){return {};}
void prepareSuspend(){}
void cancelPreparedSuspend(){}
std::mutex captureMx;
unsigned long long repeatedSuspendGeneration=0,repeatedSuspendEpoch=0;
/* EXACT_MEMORY_EPISODE_CLOSE_FUNCTION */
json inspect(ULONGLONG){return {{"scope","steam"},{"steamRecovery",{{"status","unknown"},{"reason","fixture-not-used"}}}};}
// Compatibility-only declarations. Actual Steam wake/input actions are covered
// by sleep_steam_only_selftest.py against the production headers, not this stub.
void captureSuspend(ULONGLONG){}
json capturedPauseState(ULONGLONG){return {{"available",false}};}
json reconcile(const json&,ULONGLONG,ULONGLONG,const std::function<bool()>& gate={}){return {{"accepted",false}};}
json resumeInputContext(const json&,ULONGLONG,ULONGLONG,const std::function<bool()>& gate={}){return {{"accepted",false}};}
}
// Compatibility boundaries only; the original Steam owner is not executed here.
bool steamOriginalRecoveryOwnsRunning(DWORD,ULONGLONG){return false;}
void steamOriginalRecoverySuspend(ULONGLONG){}
void steamOriginalRecoveryWake(ULONGLONG){}
void steamOriginalRecoveryPowerReady(ULONGLONG){}
#include "sleep_recovery_auto.h"
std::atomic<bool> g_xinputDiscoveryDirty{false};int sensorRefreshCalls=0;
void inputCaptureResumeSensorsForPower(){++sensorRefreshCalls;}

void resetApi(){reset();SleepRecoveryAuto::state={};SleepRecoveryAuto::kernelWake={};autoJobs.clear();autoQueueAllowed=preferenceSaveAllowed=true;fspath::remove(SleepRecoveryAuto::reportPath());SleepRecoveryApi::latest={};SleepRecoveryApi::serial=0;writesAllowed=true;
 g_aiFanMockSession.isolated=false;g_wakeKeptTargetGeneration=0;g_inputHostEpochMirror=99;g_inputHostBoundPowerGeneration=7;hostAlive=true;
 g_inputHostPrepared=true;g_inputHostReleaseQuarantined=g_inputHostTransportFault=false;
 g_inputHostFaultHoldsStart=g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 queuedInput.clear();g_sgFacts.clear();fspath::remove(SG_FACT_LOG);}
void executeAutoJob(){assert(!autoJobs.empty());auto job=std::move(autoJobs.front());autoJobs.erase(autoJobs.begin());job();}
void beginAuto(){SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoArm(generation);tick=1500;sleepRecoveryAutoTick();}

'''
# resetApi needs original reset declaration; API itself does not.
native=(root/'native/main.cpp').read_text('utf-8-sig')
worker=extract_native_function(native,'inputHostPerformSleepRepair')
episode_source=(root/'native/sleep_steam_recovery.h').read_text('utf-8-sig')
episode_close=extract_native_function(episode_source,'closeSuspendEpisode')
assert 'std::lock_guard<std::mutex> lock(captureMx);' in episode_close
assert 'repeatedSuspendGeneration=0;repeatedSuspendEpoch=0;' in episode_close
assert mocks.count('/* EXACT_MEMORY_EPISODE_CLOSE_FUNCTION */')==1
mocks=mocks.replace('/* EXACT_MEMORY_EPISODE_CLOSE_FUNCTION */',episode_close,1)
assert '#include' not in worker
assert 'std::lock_guard<InputHostMutex> lock(g_inputHostMx);' in worker
assert 'ymcc::sleeprecovery::inputGate(' in worker and 'inputBindingAllowed(' in worker
assert '!g_inputHostFaultHoldsStart.load()' in worker and 'sleepRecoveryAutoAllows(generation,autoEpoch)' in worker
(out/'native-sleep-repair-source-binding.json').write_text(json.dumps({
    'source':'native/main.cpp','sourceSha256':hashlib.sha256((root/'native/main.cpp').read_bytes()).hexdigest(),
    'function':'inputHostPerformSleepRepair','functionSha256':hashlib.sha256(worker.encode('utf-8')).hexdigest(),
    'exactFunctionText':worker,'uniqueCompleteDefinition':True,'allCurrentGuardsRetained':True,
    'unrelatedNeighborCodeIncluded':False,'productSourceChanged':False,
    'memoryDependencies':['native/input_host_transaction_mutex.h','native/input_host_lifecycle_claim.h'],
    'steamEpisodeClose':{'source':'native/sleep_steam_recovery.h','sourceSha256':hashlib.sha256((root/'native/sleep_steam_recovery.h').read_bytes()).hexdigest(),'exactFunctionText':episode_close,'functionSha256':hashlib.sha256(episode_close.encode('utf-8')).hexdigest(),'memoryContextOnly':True,'realSteamExecuted':False}
},indent=2)+'\n',encoding='utf-8')
cpp=cpp.replace('int passed=0;','void reset();\n'+mocks+'\n'+worker+'\nint passed=0;',1)
anchor=' std::cout<<"sleep wake recovery production-header selftest:'
tests=r'''
 SG_FACT_LOG=SG_DIR+L"\\sleep-facts.log";
 using namespace ymcc::sleeprecovery;
 assert(repairGate(false,false,false,true,true,false,false,true,0,0,7,7,true)=="confirmation-required");
 assert(repairGate(true,false,false,true,true,false,false,true,0,30001,7,7,true)=="inspection-expired");
 assert(repairGate(true,false,false,true,true,false,false,true,0,0,7,8,true)=="state-changed");
 for(int i=0;i<6;++i)assert(!inputGate(i==0,i==1,true,i!=2,true,true,true,true,i==3,i==4,7,7,99,i==5?100:99));
 ULONGLONG gen=0,epoch=0;assert(inputReason("sleep-repair-input:7:99",gen,epoch)&&gen==7&&epoch==99);
 for(auto s:{"sleep-repair-input:7:99:1","sleep-repair-input:0:99","sleep-repair-input:7:-1","power-rearm:7"})assert(!inputReason(s,gen,epoch));
 assert(field("created=1|created=2","created").empty());pass("pure policy gates reject unsafe input/generation/TTL and ambiguous fields");
 resetApi();g_inputHostPowerAdmissionRevoked=true;g_xinputDiscoveryDirty=false;sensorRefreshCalls=0;
 inputHostPerformSleepRepair("sleep-repair-input:7:99");assert(!g_inputHostPowerAdmissionRevoked&&g_xinputDiscoveryDirty&&sensorRefreshCalls==1);
 assert(facts.back().first=="sleep-input-user-repair-result"&&!facts.back().second["healthVerified"]&&!facts.back().second["targetRecreated"]);
 pass("actual input lifecycle execution refreshes original admitted Host without target recreation or health claim");
 for(int n=0;n<9;++n){resetApi();g_xinputDiscoveryDirty=false;sensorRefreshCalls=0;g_inputHostPowerAdmissionRevoked=true;
   if(n==0)hostAlive=false;if(n==1)g_inputHostPrepared=false;if(n==2)g_inputHostTransportFault=true;if(n==3)g_inputHostReleaseQuarantined=true;
   if(n==4)g_inputHostFaultHoldsStart=true;if(n==5)g_inputHostBoundPowerGeneration=6;if(n==6)g_sleepRecoveryOutputWanted=false;if(n==7)writesAllowed=false;if(n==8)g_inputHostEpochMirror=100;
   inputHostPerformSleepRepair("sleep-repair-input:7:99");assert(g_inputHostPowerAdmissionRevoked&&!g_xinputDiscoveryDirty&&sensorRefreshCalls==0&&!facts.back().second["applied"]);}
 pass("actual lifecycle worker refuses dead/unprepared/fault/quarantine/disabled/stale sessions without hardware writes");
 resetApi();g_inputHostBoundPowerGeneration=6;g_wakeKeptTargetGeneration=7;g_inputHostPowerAdmissionRevoked=true;
 inputHostPerformSleepRepair("sleep-repair-input:7:99");assert(facts.back().second["applied"]&&!g_inputHostPowerAdmissionRevoked&&g_inputHostBoundPowerGeneration==6);
 assert(!inputBindingAllowed(6,7,0)&&!inputBindingAllowed(6,7,8)&&!inputBindingAllowed(8,7,7)&&!inputBindingAllowed(0,7,7));
 pass("kept Modern Standby Host preserves old binding; only exact current-wake native proof authorizes refresh");
 resetApi();marker(100);auto report=sleepRecoveryInspect({});assert(report["ok"]&&report["repair"]["allowed"]);
 assert(procs[100].calls==0&&queuedInput.empty()&&exists(100));assert(!report["healthVerified"]);
 assert(report["games"][0]["created"]=="1234");assert(!report["steam"]["ok"]);pass("inspection reads original PID/thread/lease only, never resumes/launches or verifies game input");
 auto token=report["inspectionToken"].get<std::string>();auto denied=sleepRecoveryRepair({{"inspectionToken",token}});assert(!denied["accepted"]);
 denied=sleepRecoveryRepair({{"inspectionToken","wrong"},{"confirmed",true}});assert(!denied["accepted"]&&procs[100].calls==0);pass("repair requires explicit confirmation and server-owned latest ticket");
 auto receipt=sleepRecoveryRepair({{"inspectionToken",token},{"confirmed",true}});assert(receipt["accepted"]&&receipt["gameQueued"]&&receipt["inputQueued"]&&!receipt["healthVerified"]);
 assert(procs[100].calls==0&&g_sgWakeRecovery.requestId==token&& !g_sgWakeRecovery.focusOnComplete&&g_sgLastGameWakeGeneration==0);
 assert(!sleepRecoveryRepair({{"inspectionToken",token},{"confirmed",true}})["accepted"]);window();assert(procs[100].calls==1&&focusCalls==0&&!exists(100));pass("single-use user ticket queues bounded original-game recovery without focus/automatic wake dedupe changes");
 resetApi();marker(100);report=sleepRecoveryInspect({});tick=30001;assert(!sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}})["accepted"]);assert(procs[100].calls==0);pass("30s expired inspection cannot resume");
 resetApi();marker(100);report=sleepRecoveryInspect({});generation=8;assert(!sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}})["accepted"]);pass("new power generation rejects old ticket");
 resetApi();marker(100);report=sleepRecoveryInspect({});g_inputHostEpochMirror=100;assert(!sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}})["accepted"]);pass("new input Host epoch rejects old ticket");
 resetApi();marker(100);report=sleepRecoveryInspect({});marker(101);assert(!sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}})["accepted"]);assert(procs[101].calls==0);pass("new marker cannot be silently added to an inspected repair");
 resetApi();marker(100);report=sleepRecoveryInspect({});sgWriteFileAtomic(SG_SLEEP_LEASE_DIR+L"\\100.txt",sgReadFile(SG_SLEEP_LEASE_DIR+L"\\100.txt")+"|replacement=yes");assert(!sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}})["accepted"]);pass("changed marker content is fail-closed");
 resetApi();marker(100);marker(101,true);g_manualPausedPid=101;report=sleepRecoveryInspect({});receipt=sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}});window();assert(receipt["gameQueued"]&&procs[101].calls==0&&exists(101,true)&&g_manualPausedPid==101);pass("user repair excludes unrelated manual pause and preserves manual UI state");
 resetApi();marker(100);report=sleepRecoveryInspect({});marker(100,true);receipt=sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}});assert(!receipt.value("gameQueued",false)&&procs[100].calls==0);pass("manual pause inserted before acceptance prevents game repair");
 resetApi();marker(100);report=sleepRecoveryInspect({});sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}});marker(100,true);window();assert(procs[100].calls==0&&exists(100,true)&&exists(100));pass("manual pause inserted AFTER acceptance vetoes each user recovery attempt");
 resetApi();marker(100);fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");report=sleepRecoveryInspect({});assert(!exists(100));receipt=sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}});assert(receipt["gameQueued"]&&exists(100));window();assert(!owner.markerOwned);pass("inspection never reconstructs missing marker; confirmed repair uses exact memory owner identity");
 resetApi();marker(100);fspath::remove(SG_SLEEP_LEASE_DIR+L"\\100.txt");report=sleepRecoveryInspect({});owner.powerGeneration=9;receipt=sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}});assert(!receipt["accepted"]&&!exists(100));pass("changed memory owner cannot authorize synthetic-marker reconstruction");
 resetApi();hostAlive=false;g_inputHostPrepared=false;report=sleepRecoveryInspect({});receipt=sleepRecoveryRepair({{"inspectionToken",report["inspectionToken"]},{"confirmed",true}});assert(receipt["flagsReconciled"]&&!receipt["inputQueued"]&&queuedInput.empty()&&!g_sgInSuspend&&!g_sgGameActuallySuspended);pass("stale flags reconcile without creating a dead or disabled input target");
 resetApi();marker(100);retry=true;report=sleepRecoveryInspect({});assert(!report["repair"]["allowed"]);retry=false;sgBeginWakeGameRecovery(7);report=sleepRecoveryInspect({});assert(!report["repair"]["allowed"]);pass("re-sleep and automatic pending recovery disable user repair");
 resetApi();for(int n=0;n<65;++n)marker(100+n);report=sleepRecoveryInspect({});assert(report["state"]["markerLimit"]&&!report["repair"]["allowed"]);pass("lease scanning is bounded and incomplete scan never authorizes repair");
 resetApi();procs[100]={};sgWriteFileAtomic(SG_SLEEP_LEASE_DIR+L"\\100.txt","pid=100|created=1234|created=5678|state=suspended|generation=7");report=sleepRecoveryInspect({});assert(!report["repair"]["allowed"]);pass("ambiguous persisted marker is retained and reported, never recovered");
 resetApi();std::string big(2*1024*1024+100,'x');sgWriteFileAtomic(SG_FACT_LOG,big);auto tail=SleepRecoveryApi::readTail(SG_FACT_LOG);assert(tail.bytes.size()==2*1024*1024&&tail.meta["truncated"]);pass("feedback log tails are byte bounded with explicit truncation evidence");
 resetApi();marker(100);report=sleepRecoveryInspect({});json fb={{"gameInput","broken"},{"guide","unknown"},{"bigPicture","start"},{"overlay","enabled"},{"note","owned fixture, no live devices"}};
 auto bundle=sleepRecoveryExport(fb);assert(bundle["ok"]&&!bundle["uploaded"]);auto dir=fspath::path(std::wstring(bundle["path"].get<std::string>().begin(),bundle["path"].get<std::string>().end()));
 // Use one stable string for iterators (separate get() temporaries would be undefined behavior).
 const auto pathText=bundle["path"].get<std::string>();dir=std::wstring(pathText.begin(),pathText.end());
 auto manifest=json::parse(sgReadFile((dir/L"manifest.json").wstring()));assert(!manifest["uploaded"]&&!manifest["healthVerified"]);
 auto diag=json::parse(sgReadFile((dir/L"diagnosis.json").wstring()));assert(diag["inspectionToken"].is_null());
 auto human=json::parse(sgReadFile((dir/L"operator-feedback.json").wstring()));assert(human["source"]=="operator-reported-not-automatic-verification");
 assert(procs[100].calls==0&&queuedInput.empty());auto another=sleepRecoveryExport(fb);assert(another["path"]!=bundle["path"]);pass("feedback uses unique local folder, no upload/credentials and separates manual claims from diagnostics");
 bool invalid=false;try{sleepRecoveryInspect({{"appid",4294967296ULL}});}catch(...){invalid=true;}assert(invalid);
 invalid=false;try{fb["gameInput"]="verified";sleepRecoveryExport(fb);}catch(...){invalid=true;}assert(invalid);pass("native AppID and manual feedback inputs are validated before work");
 json state={{"leaseCount",0},{"ownerPresent",false},{"inSuspend",true},{"gameSuspended",true},{"inputReady",true},{"steamAvailable",false}};
 auto findings=checks(state,{"{bad json}",R"({"event":"sleep-game-pause-skipped","details":{"alreadyPaused":true}})",R"({"event":"sleep-game-recovery-queued","details":{"targets":0}})"});
 assert(std::any_of(findings.begin(),findings.end(),[](const auto& v){return v["id"]=="historical-empty-recovery";}));
 assert(std::any_of(findings.begin(),findings.end(),[](const auto& v){return v["id"]=="game-input-unverified";}));pass("diagnostic findings distinguish historical clues, stale flags and unverified real input");

 // New context checks follow every pre-existing assertion in its original order.
 resetApi();assert(!g_inputHostMx.ownedByCurrentThread());
 {std::lock_guard<InputHostMutex> held(g_inputHostMx);assert(g_inputHostMx.ownedByCurrentThread());assert(!g_inputHostMx.try_lock());}
 assert(!g_inputHostMx.ownedByCurrentThread()&&g_inputHostMx.try_lock());g_inputHostMx.unlock();
 pass("actual production transaction mutex rejects recursive ownership and releases its memory owner");
 std::atomic<bool> requested{false},inFlight{true};
 {ymcc::inputHost::LifecycleClaimGuard complete(requested,inFlight);assert(inFlight.load());}
 assert(!inFlight.load()&&!requested.load());inFlight=true;
 try{ymcc::inputHost::LifecycleClaimGuard complete(requested,inFlight);throw std::runtime_error("owned claim fault");}catch(const std::runtime_error&){}
 assert(!inFlight.load()&&requested.load());
 pass("actual production memory claim guard retires ownership and retains pending work after an exception");
 for(int n=0;n<8;++n){resetApi();g_inputHostPowerAdmissionRevoked=true;g_xinputDiscoveryDirty=false;sensorRefreshCalls=0;
  std::string reason="sleep-repair-input:7:99";
  if(n==0)g_aiFanMockSession.isolated=true;if(n==1)g_exitRequested=true;if(n==2)g_powerLifecycle=PowerLifecycle::Suspending;
  if(n==3)g_inputReady=false;if(n==4)g_inputHostBoundPowerGeneration=0;if(n==5)reason="sleep-repair-input:8:99";
  if(n==6)reason="sleep-repair-input:7:0";if(n==7)reason="sleep-repair-input:7:99:extra";
  inputHostPerformSleepRepair(reason);assert(!facts.back().second["applied"]&&g_inputHostPowerAdmissionRevoked&&!g_xinputDiscoveryDirty&&sensorRefreshCalls==0);
 }
 pass("exact current native repair function refuses isolation exit power input missing binding and stale malformed generations");
 resetApi();g_sleepRecoveryAutoEnabled=true;g_sleepRecoveryAutoConfigEpoch=5;g_inputHostPowerAdmissionRevoked=true;sensorRefreshCalls=0;
 inputHostPerformSleepRepair("sleep-auto-input:7:99:4");assert(!facts.back().second["applied"]&&sensorRefreshCalls==0&&g_inputHostPowerAdmissionRevoked);
 inputHostPerformSleepRepair("sleep-auto-input:7:99:5");assert(facts.back().second["applied"]&&sensorRefreshCalls==1&&!g_inputHostPowerAdmissionRevoked);
 generation=8;inputHostPerformSleepRepair("sleep-auto-input:7:99:5");assert(!facts.back().second["applied"]&&sensorRefreshCalls==1);
 pass("current automatic configuration epoch and power generation gate the real repair function");
 resetApi();marker(100);report=sleepRecoveryInspect({});assert(!report["games"][0]["threadsKnown"]&&!report["games"][0]["allRunning"]&&!report["healthVerified"]);
 assert(procs[100].calls==0&&queuedInput.empty());
 pass("actual diagnostic query returns unknown at its unavailable OS port without fabricating running or input success");
 SleepSteamRecovery::repeatedSuspendGeneration=7;SleepSteamRecovery::repeatedSuspendEpoch=9;
 sleepRecoveryAutoCloseSuspendEpisode();assert(SleepSteamRecovery::repeatedSuspendGeneration==0&&SleepSteamRecovery::repeatedSuspendEpoch==0);
 SleepSteamRecovery::repeatedSuspendGeneration=8;SleepSteamRecovery::repeatedSuspendEpoch=10;
 sleepRecoveryAutoArm(generation);assert(SleepSteamRecovery::repeatedSuspendGeneration==0&&SleepSteamRecovery::repeatedSuspendEpoch==0);
 pass("actual Steam episode memory close retains its mutex and resets both repeated-suspend identities");
 std::cout<<"NATIVE_API_CONTEXT_BOUNDARY "<<json({{"sourceFunctionExact",true},{"memoryMutexAndClaimExecuted",true},{"fullLifecycleQueueExecuted",false},{"actualEpisodeMemoryResetExecuted",true},{"realSteamExecuted",false},{"realThreadEnumeration",false}}).dump()<<"\n";

'''
# Steam-only automatic coordinator is covered in sleep_steam_only_selftest.py.

# Correct stable-path construction before compiling.
tests=tests.replace('auto dir=fspath::path(std::wstring(bundle["path"].get<std::string>().begin(),bundle["path"].get<std::string>().end()));','fspath::path dir;')
assert cpp.count(anchor)==1;cpp=cpp.replace(anchor,tests+'\n'+anchor,1)
cpp=cpp.replace('int wmain(int argc,wchar_t** argv){','int wmain(int argc,wchar_t** argv){std::cout<<std::unitbuf;')
source=out/'sleep_recovery_api_selftest.cpp';source.write_text(cpp,'utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat');exe=out/'sleep_recovery_api_selftest.exe';cmd=out/'run-sleep-recovery-api.cmd'
cmd.write_text(f'@echo off\r\ncall "{vs}" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{source}" /I"{root / "native"}" /I"{root / "deps/json"}" /Fo"{out / "sleep_recovery_api_selftest.obj"}" /Fe"{exe}" /link advapi32.lib ole32.lib shell32.lib\r\nif errorlevel 1 exit /b 1\r\n"{exe}" "{out / "owned-api-fixture-state"}"\r\n',encoding='ascii')
res=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'sleep-recovery-api-selftest.log').write_bytes(res.stdout);print(res.stdout.decode('utf-8',errors='replace'));print('Native fixture return:',res.returncode);raise SystemExit(res.returncode)
