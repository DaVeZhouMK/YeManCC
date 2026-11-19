
"""Generate the original complete Startup composition for command pending only.
No compiler or native subprocess is launched here. Historical source is embedded
in the sibling reviewed template; all publication dependencies use repo headers.
"""
from pathlib import Path
import hashlib,json,re,os
ADVISORY_CASES=['command-pending-current-identity-advisory', 'command-pending-queued-binding-advisory', 'command-pending-current-ordinary-revoke', 'command-pending-stale-account-advisory', 'command-pending-rejected-completion-advisory', 'command-pending-deferred-wake-advisory', 'command-pending-current-binding-advisory', 'command-pending-arming-command-advisory', 'command-pending-running-fresh-advisory']

def pin(p):
 p=Path(p).resolve();b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def write(p,value):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('xb') as f:f.write(value.encode('utf-8') if isinstance(value,str) else value)

def function_body(text,name,extract):
 clean=extract.__globals__['masked'](text);found=[]
 for m in re.finditer(r'^static\s+[^;{}]*?\b'+re.escape(name)+r'\s*\(',clean,re.M):
  pos=clean.index('(',m.start())+1;depth=1
  while depth:
   depth+=(clean[pos]=='(')-(clean[pos]==')');pos+=1
  while clean[pos].isspace():pos+=1
  if clean[pos:pos+8]=='noexcept':
   pos+=8
   while clean[pos].isspace():pos+=1
  if clean[pos]!='{':continue
  end=pos+1;depth=1
  while depth:
   depth+=(clean[end]=='{')-(clean[end]=='}');end+=1
  found.append(text[clean.rfind('\n',0,m.start())+1:end])
 if len(found)!=1:raise ValueError('ambiguous complete function '+name+': '+str(len(found)))
 return found[0]

def body_opening(value,extract):
 clean=extract.__globals__['masked'](value);pos=clean.index('(')+1;depth=1
 while depth:
  depth+=(clean[pos]=='(')-(clean[pos]==')');pos+=1
 while clean[pos].isspace():pos+=1
 if clean[pos]!='{':raise ValueError('complete function body opening not found')
 return pos

def _prepare_e4(args,extract):
 if not args.prepare_only:raise ValueError('command-pending-only is source preparation; compile/behavior require a separately allocated owned producer')
 repo=args.repo.resolve() if args.repo else Path(__file__).resolve().parents[2]
 out=args.out.resolve()
 if out.exists():raise FileExistsError('exclusive prepared output required')
 out.mkdir(parents=True)
 runtimePath=repo/'native/steam_original_recovery_runtime.h';runtime=runtimePath.read_text(encoding='utf-8-sig')
 mainPath=repo/'native/main.cpp';main=mainPath.read_text(encoding='utf-8-sig')
 template=Path(__file__).with_name('recovery_command_pending.cpp.in');source=template.read_text(encoding='utf-8-sig')
 casesPath=Path(__file__).with_name('recovery_command_pending_cases.inc')
 fragments=[];edits=[]
 def function(text,name):
  return function_body(text,name,extract)
 def record(name,body,origin):
  p=out/'extracted'/(name+'.inc');write(p,body)
  fragments.append({'name':name,'origin':str(origin),'complete':True,'fragment':pin(p)})
  return body
 def replace_once(value,old,new,label):
  if value.count(old)!=1:raise ValueError('nonunique composition anchor '+label)
  edits.append({'label':label,'before':old,'after':new})
  return value.replace(old,new)
 # Fresh original main functions only: do not pin an old whole main over current.
 mainNames=['postSteamTrackerCompletionDispatch','nativeValveSelectionCurrent','nativeValveReadCurrentSelection','nativeValveReadCachedSelection','nativeSteamGameTrackerReadFresh','nativeSteamPrepareModuleProfile']
 fresh={}
 for name in mainNames:fresh[name]=record(name,function(main,name),mainPath)
 locked=function(main,'nativeValveSelectionCurrentLocked');record('nativeValveSelectionCurrentLocked',locked,mainPath)
 source=replace_once(source,function(source,'nativeValveSelectionCurrent'),locked+'\n'+fresh['nativeValveSelectionCurrent'],'fresh valve selector')
 for name in mainNames:
  if name=='nativeValveSelectionCurrent':continue
  old=function(source,name)
  value=fresh[name]
  if name=='nativeSteamGameTrackerReadFresh':
   clean=extract.__globals__['masked'](value);opening=clean.index('(')+1;depth=1
   while depth:
    depth+=(clean[opening]=='(')-(clean[opening]==')');opening+=1
   while clean[opening].isspace():opening+=1
   if clean[opening]!='{':raise ValueError('fresh read function body not uniquely located')
   opening+=1
   value=value[:opening]+'\n    ++fixtureFreshReadCalls;fixtureRecord(FixturePoint::FreshApi);\n'+value[opening:]
   edits.append({'label':'fresh-read entry observation only','before':fresh[name],'after':value})
  source=replace_once(source,old,value,'fresh main '+name)
 # Keep actual public types from current main rather than reconstructing them.
 for name in ['NativeGameSelectionReceipt','NativeSteamTrackerFreshCompletion']:
  body=extract(main,r'\bstruct\s+'+name+r'\s*\{',True)[0];record(name,body,mainPath)
  old=extract(source,r'\bstruct\s+'+name+r'\s*\{',True)[0];source=replace_once(source,old,body,'fresh type '+name)
 # Complete r4 runtime namespace, including original run/takeNext business loop.
 ns=extract(runtime,r'^namespace\s+SteamOriginalRecovery\s*\{')[0];record('runtime-namespace-complete',ns,runtimePath)
 nsOriginal=ns
 ports={
 'root':'static std::wstring root(){return fixtureFilesystemRoot+L"\\evidence";}',
 'runtimeRoot':'static std::wstring runtimeRoot(){return fixtureFilesystemRoot+L"\\runtime";}',
 'readJson':'static ConfigRead readJson(const std::wstring&){return {json{{"enabled",true},{"targetMode","generic"}},true,false};}',
 'saveJson':'static void saveJson(const std::wstring& path,const json& value){saved[path]=value;}',
 'log':'static void log(const std::shared_ptr<Control>&,const char*,json){}',
 'publish':'static void publish(const std::shared_ptr<Control>& c,json value,bool=true){std::lock_guard<std::mutex> lock(c->snapshotMx);c->snapshot.update(value);}'
 }
 ns=ns.replace('static std::mutex lifecycleMx,shutdownMx;','static std::mutex lifecycleMx,shutdownMx;\nstatic std::map<std::wstring,json> saved;')
 edits.append({'label':'controlled IO storage declaration','before':'static std::mutex lifecycleMx,shutdownMx;','after':'static std::mutex lifecycleMx,shutdownMx;\nstatic std::map<std::wstring,json> saved;'})
 ns=ns.replace('static std::shared_ptr<Control> control;','static std::shared_ptr<Control> control;\nstatic auto& callbackControl=control;')
 edits.append({'label':'existing fixture callback alias','before':'static std::shared_ptr<Control> control;','after':'static std::shared_ptr<Control> control;\nstatic auto& callbackControl=control;'})
 for name,body in ports.items():
  old=function(ns,name);record('runtime-original-'+name,old,runtimePath);ns=replace_once(ns,old,body,'controlled IO leaf '+name)
 route=function(ns,'runRoute');record('runRoute',route,runtimePath)
 needle='decision=routeAdmission.admit(scope,{pending.kind,pending.generation,pending.sequence},enabled);'
 if route.count(needle)!=1:raise ValueError('exact route admission observation anchor')
 observed=route.replace(needle,'++fixtureAdmissionCalls;fixtureRecord(FixturePoint::AdmitCall);'+needle)
 routeOpening=body_opening(observed,extract)+1
 observed=observed[:routeOpening]+'\n    FixtureRouteObservationGuard fixtureRouteObservation{static_cast<unsigned>(route),pending.sequence,mayMutate,bool(pending.startupCommand)};\n'+observed[routeOpening:]
 edits.append({'label':'count original admit entry, never replace admission','before':route,'after':observed});ns=ns.replace(route,observed)
 for name in ['runOne','run','takeNext','retireStartupCommandLocked','startupCommandCurrent','startupCommandNotifiedLocked','takeReadyStartupCommand','prepareStartupCommand','cancelStartupFreshLocked','retireStartupFreshLocked','armStartupFreshCompletion','enqueue']:
  record(name,function(nsOriginal,name),runtimePath)
 for name in ['Pending','Control','StartupCommandSignal','StartupCommandWait','CommandRetirement','StartupFreshRequest','StartupFreshWait','StartupFreshRunReturn','StartupCommandRunReturn']:
  record(name,extract(nsOriginal,r'\bstruct\s+'+name+r'\b[^;{}]*\{',True)[0],runtimePath)
 # Replace the historical namespace as one unit; no stale runRouteBefore remains.
 oldNs=extract(source,r'^namespace\s+SteamOriginalRecovery\s*\{')[0]
 source=replace_once(source,oldNs,ns,'complete original runtime namespace')
 # Include every current global lifecycle/cancel callback, with only two private
 # Start observations. The worker-start port records a callable, never claims a
 # real product worker was started by the allocation-failure case.
 tail=runtime[runtime.index(nsOriginal)+len(nsOriginal):]
 record('runtime-global-entrypoints-complete',tail,runtimePath)
 runningLost='static void steamOriginalRecoveryRunningLost('
 if runningLost in tail:
  tail=replace_once(tail,runningLost,'[[maybe_unused]] '+runningLost,'uninvoked live running-loss entrypoint')
 for name in ['steamOriginalRecoveryBusinessCommit','steamOriginalRecoveryTrackerObserved','steamOriginalRecoveryRequestStop']:
  old=function(source,name);source=replace_once(source,old,'','remove stale global '+name)
 start=function(tail,'steamOriginalRecoveryStart');record('steamOriginalRecoveryStart',start,runtimePath)
 altered=start.replace('std::make_shared<ymcc::steamcommand::AvailabilityEvent>()','fixtureAllocateAvailability()').replace('SteamOriginalRecovery::worker.start([c]{','fixtureObserveWorkerStart([c]{')
 if altered==start:raise ValueError('new Start allocation/call observation missing')
 edits.append({'label':'optional signal allocation fault + worker start callback port; not actual worker launch','before':start,'after':altered});tail=tail.replace(start,altered)
 # An exact serial business pass comes from original run, not a second algorithm.
 run=function(nsOriginal,'run');a=run.index('        if(!next.startupFresh&&!next.startupCommand){');b=run.index('        c->active=false;c->cv.notify_all();',a)+len('        c->active=false;c->cv.notify_all();')
 businessPass=run[a:b];record('original-serial-worker-business-pass',businessPass,runtimePath)
 workerDriver='''static bool driveOne(const std::shared_ptr<SteamOriginalRecovery::Control>& c){
 using namespace SteamOriginalRecovery;
 {std::lock_guard<std::mutex> lock(c->mx);if(c->stop||(c->pending.empty()&&!startupCommandNotifiedLocked(c)))return false;}
 Pending next;if(!takeNext(c,next))return false;
'''+businessPass+'''\n return true;\n}\n'''
 # Fresh typed wrapper forwards all original public typed parameters into the
 # established controlled engine leaf; it does not import runTrial behavior.
 adapter=function_body(main,'steamOriginalRecoveryEngineHandle',extract);record('typed-main-adapter',adapter,mainPath)
 headerPath=repo/'native/steam_original_recovery_engine.h';record('typed-engine-interface',headerPath.read_text(encoding='utf-8-sig'),headerPath)
 oldDecl=source.index('static json steamOriginalRecoveryEngineHandle(const json&,')
 declEnd=source.index(';',oldDecl)+1
 source=replace_once(source,source[oldDecl:declEnd],'''static json steamOriginalRecoveryEngineHandle(const json&,const std::function<bool()>&,const std::function<void(const json&)>&,
 const std::function<bool()>&,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::shared_ptr<ymcc::steamnative::ReadScope>,
 std::shared_ptr<ymcc::steamgamefacts::ProcessGroupLease>,std::shared_ptr<ymcc::steamgamefacts::RequestCancellation>,
 std::shared_ptr<ymcc::steamcommand::Intent>,std::shared_ptr<ymcc::steamcommand::AvailabilityEvent>,
 std::shared_ptr<const ymcc::steamgamefacts::StartupHandoffEvidence>,const std::function<bool()>&,
 std::shared_ptr<const ymcc::steamgamefacts::HistoricalStartupHandoffEvidence> = {},const std::function<bool()>& = {});''','new typed declaration')
 oldLeaf=function(source,'steamOriginalRecoveryEngineHandle')
 leaf='''namespace ymcc::steamOriginalRecovery {
Json handle(const Json& request,Emit emit,Current current,Current observation,WakeSignal,WakeSignal,WakeSignal,
 std::shared_ptr<ymcc::steamnative::ReadScope> readScope,std::shared_ptr<ymcc::steamgamefacts::ProcessGroupLease> group,
 std::shared_ptr<ymcc::steamgamefacts::RequestCancellation> business,std::shared_ptr<ymcc::steamcommand::Intent> mutationIntent,
 std::shared_ptr<ymcc::steamcommand::AvailabilityEvent>,
 std::shared_ptr<const ymcc::steamgamefacts::StartupHandoffEvidence> startupHandoff,Current startupHandoffCurrent,
 std::shared_ptr<const ymcc::steamgamefacts::HistoricalStartupHandoffEvidence> historicalStartupHandoff,Current historicalStartupHandoffCurrent){
 if(startupHandoff||startupHandoffCurrent)throw std::logic_error("fixture-startup-handoff-unsupported");
 if(historicalStartupHandoff||historicalStartupHandoffCurrent)throw std::logic_error("fixture-historical-startup-handoff-unsupported");
 fixtureRecord(FixturePoint::EngineEntry);
 EngineCall row{request,group,business,readScope,current,observation};row.currentAtEntry=current();row.observationAtEntry=observation();
 if(mutationIntent){++fixturePublisherCalls;if(!mutationIntent->ticket())throw std::runtime_error("typed Startup lost Ready ticket");}
 if(engineAction)engineAction(row);engineCalls.push_back(std::move(row));
 json result={{"status","unqualified"},{"reason","controlled-engine-leaf-no-native-publication"},{"consumed",firstConsumed&&engineCalls.size()==1}};
 if(fixtureEngineReceipt)result=fixtureEngineReceipt(request);emit({{"event","job-finished"},{"result",result}});return result;
}
}
'''+adapter
 source=replace_once(source,oldLeaf,leaf,'actual typed adapter + controlled engine leaf')
 # Current all-callback tail is placed before setup, where existing tests already
 # see the real cancellation entrypoints. Forward PowerReady is unchanged.
 source=source.replace('struct EngineCall {',tail+'\nstruct EngineCall {',1)
 source=source.replace('enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};','''enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<unsigned> fixtureAdmissionCalls{0},fixtureFreshReadCalls{0},fixturePublisherCalls{0},fixtureWorkerStartCalls{0};
static std::atomic<unsigned> sleepHelperCalls{0};
enum class FixturePoint:unsigned {FreshApi,AdmitCall,EngineEntry};
struct FixtureRouteContext {unsigned route;unsigned long long sequence;bool active,mayMutate,commandSuffix;};
struct FixtureObservation {FixturePoint point;unsigned route;unsigned long long sequence;bool mayMutate,commandSuffix;};
struct FixtureTraceBuffer {FixtureObservation rows[256];unsigned used;bool overflow;};
static thread_local FixtureRouteContext fixtureRouteContext{};
static thread_local FixtureTraceBuffer fixtureTrace{};
static void fixtureRecord(FixturePoint point) noexcept {
 if(!fixtureRouteContext.active)return;
 if(fixtureTrace.used==256){fixtureTrace.overflow=true;return;}
 fixtureTrace.rows[fixtureTrace.used++]={point,fixtureRouteContext.route,fixtureRouteContext.sequence,fixtureRouteContext.mayMutate,fixtureRouteContext.commandSuffix};
}
struct FixtureRouteObservationGuard {
 FixtureRouteContext previous;
 FixtureRouteObservationGuard(unsigned route,unsigned long long sequence,bool mayMutate,bool commandSuffix) noexcept:previous(fixtureRouteContext){fixtureRouteContext={route,sequence,true,mayMutate,commandSuffix};}
 ~FixtureRouteObservationGuard(){fixtureRouteContext=previous;}
};
static std::atomic<bool> fixtureFailAvailabilityAllocation{false};
static std::wstring fixtureFilesystemRoot;
static std::function<void()> fixtureCapturedWorker;
static std::shared_ptr<ymcc::steamcommand::AvailabilityEvent> fixtureAllocateAvailability(){
 if(fixtureFailAvailabilityAllocation.exchange(false))throw std::bad_alloc();return std::make_shared<ymcc::steamcommand::AvailabilityEvent>();}
static void fixtureObserveWorkerStart(std::function<void()> fn){++fixtureWorkerStartCalls;fixtureCapturedWorker=std::move(fn);}
''')
 source=source.replace('#include "steam_native_read_scope.h"','#include "steam_native_read_scope.h"\n#include "steam_command_availability.h"\n#include "steam_original_recovery_engine.h"\n#include "steam_original_recovery_handoff.h"\n#include "steam_original_recovery_config.h"\n#include "steam_original_recovery_storage.h"\n#include "steam_original_recovery_worker.h"')
 source=source.replace('#include <filesystem>','#include <filesystem>\nnamespace fspath=std::filesystem;')
 # Fresh complete sleep definitions are source contracts only. These two APIs
 # are not selected by Startup; a call is denied and counted, never published.
 sleepPath=repo/'native/sleep_steam_recovery.h';sleep=sleepPath.read_text(encoding='utf-8-sig')
 sleepContracts={}
 for name in ['originalRulesCurrentLocked','originalRulesCurrent','withOriginalTargetPublication','captureRequestCurrentLocked','captureRequestCurrent','originalTargetCurrent','originalRulesCommit','withOriginalTarget']:
  sleepContracts[name]=record('sleep-contract-'+name,function(sleep,name),sleepPath)
 for name in ['CaptureRequest','Captured']:
  record('sleep-contract-'+name,extract(sleep,r'\bstruct\s+'+name+r'\s*\{',True)[0],sleepPath)
 sleepPorts=[]
 for name in ['originalRulesCurrent','withOriginalTargetPublication']:
  originalHelper=sleepContracts[name];signature=originalHelper[:body_opening(originalHelper,extract)]
  unused='(void)value;(void)originEpoch;' if name=='originalRulesCurrent' else '(void)value;(void)publication;'
  sleepPorts.append(signature+'{'+unused+'++sleepHelperCalls;return false;}')
 # Explicit unchanged-interface memory leaves used by the original foundation.
 support='''\nnamespace SleepRecoveryAuto {
struct FixtureSharedWakeFacts {decltype(SleepRecoveryAuto::kernelWake) kernelWake;decltype(SleepRecoveryAuto::kernelSleepEntry) kernelSleepEntry;};
static FixtureSharedWakeFacts sharedWakeFactsLocked(){return {kernelWake,kernelSleepEntry};}
}
struct NativeGameRulesSnapshot;
namespace SleepSteamRecovery {
template<class T>static void originalRulesCommit(const T&,unsigned long long,unsigned long long,const std::function<void(bool)>& publication){originalTargetCommit(false,[&]{publication(false);});}
}
'''
 support+='\nnamespace SleepSteamRecovery {\n'+'\n'.join(sleepPorts)+'\n}\n'
 source=source.replace('static json steamOriginalRecoveryEngineHandle(const json&,',support+'\nstatic json steamOriginalRecoveryEngineHandle(const json&,',1)
 source=source.replace('#include "cases.inc"',workerDriver+'\n#include "command-pending-cases.inc"')
 source=source.replace('std::wstring_view(argv[2])!=L"--startup-completion"','std::wstring_view(argv[2])!=L"--command-pending-only"')
 source=source.replace('cases.size()==3','cases.size()==6')
 source=source.replace('BEGIN startup-completion-only','BEGIN command-pending-only').replace('END startup-completion-only','END command-pending-only')
 source=source.replace('std::filesystem::create_directories(root+L"\\\\logs");','std::filesystem::create_directories(root+L"\\\\logs");fixtureFilesystemRoot=root;')
 # Read-only source composition: public headers close recursively from the
 # candidate overlay first, current repo second. No Evidence publication paths.
 seen=set();headerRows=[]
 def header(name,parent=None):
  if name in seen:return
  seen.add(name)
  if name in ('json.hpp','../deps/json/json.hpp'):
   p=repo/'deps/json/json.hpp';dest=out/('include/json.hpp' if name=='json.hpp' else 'deps/json/json.hpp')
  else:
   p=repo/'native'/name;dest=out/'include'/name
  if not p.exists():raise FileNotFoundError('current quoted public header closure: '+str(p))
  write(dest,p.read_bytes());headerRows.append({'input':pin(p),'output':pin(dest)})
  for child in re.findall(r'^\s*#\s*include\s*"([^"]+)"',p.read_text(encoding='utf-8-sig'),re.M):
   if child=='../deps/json/json.hpp':
    if not (out/'deps/json/json.hpp').exists():write(out/'deps/json/json.hpp',(repo/'deps/json/json.hpp').read_bytes())
   elif '/' not in child and '\\' not in child:header(child)
   elif child.startswith('steam_original_recovery_engine/'):
    cp=repo/'native'/child;write(out/'include'/child,cp.read_bytes());headerRows.append({'input':pin(cp),'output':pin(out/'include'/child)})
   else:raise ValueError('unmapped quoted header '+child+' from '+str(p))
 for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"',source,re.M):
  if name not in ('command-pending-cases.inc',):header(name)
 if not (out/'deps/json/json.hpp').exists():write(out/'deps/json/json.hpp',(repo/'deps/json/json.hpp').read_bytes())
 # Execution epoch2: adapt only historical fixture interfaces; all original
 # product functions and headers remain pinned and unchanged.
 executionEdits=[]
 def interface_once(old,new,label):
  nonlocal source
  if source.count(old)!=1:raise ValueError('nonunique fixture interface '+label)
  executionEdits.append({'label':label,'before':old,'after':new})
  source=source.replace(old,new)
 methodStart=main.index('TrackerTailRead MainReadTailPort::readTail(')
 methodEnd=main.index('\nstd::string MainReadTailPort::selectedPathUtf8',methodStart) if '\nstd::string MainReadTailPort::selectedPathUtf8' in main[methodStart:] else len(main)
 methodText=main[methodStart:methodEnd]
 methodClose=methodText.rfind('}')+1
 if 'static void steamOriginalRecoveryPowerReady' in methodText:methodClose=methodText.index('\nstatic void steamOriginalRecoveryPowerReady')
 currentMethod=methodText[:methodClose].rstrip()
 record('main-current-readTail',currentMethod,mainPath)
 oldStart=source.index('TrackerTailRead MainReadTailPort::readTail(');oldEnd=source.index('\nstd::string MainReadTailPort::selectedPathUtf8',oldStart)
 oldMethod=source[oldStart:oldEnd].rstrip()
 observedMethod=currentMethod.replace(' const {',' const {\n    ++fixtureProjectionSourceReads;\n    if(sourceKey.ends_with(L"gameprocess_log.txt"))++fixtureFreshGameReads;\n    if(sourceKey.ends_with(L"console_log.txt"))++fixtureFreshConsoleReads;',1)
 observedMethod=observedMethod.replace('    return out;','    if(fixtureProjectionAfterRead)fixtureProjectionAfterRead();\n    if(fixtureFreshReadHook)fixtureFreshReadHook(sourceKey);\n    return out;',1)
 interface_once(oldMethod,observedMethod,'actual current MainReadTailPort + existing fixture hooks')
 oldWake='struct FixtureSharedWakeFacts {decltype(SleepRecoveryAuto::kernelWake) kernelWake;decltype(SleepRecoveryAuto::kernelSleepEntry) kernelSleepEntry;};\nstatic FixtureSharedWakeFacts sharedWakeFactsLocked(){return {kernelWake,kernelSleepEntry};}'
 newWake='struct FixtureSharedWakeFacts {decltype(SleepRecoveryAuto::kernelWake) kernelWake;decltype(SleepRecoveryAuto::kernelSleepEntry) kernelSleepEntry;SleepRecoveryAuto::InteractiveResume interactiveResume;SleepRecoveryAuto::CompletedResume completedResume;};\nstatic FixtureSharedWakeFacts sharedWakeFactsLocked(){++fixtureWakeShimCalls;return {kernelWake,kernelSleepEntry,interactiveResume,completedResume};}'
 interface_once(oldWake,newWake,'complete unselected wake-memory fields and counted zero-use port')
 interface_once('static std::atomic<unsigned> sleepHelperCalls{0};','static std::atomic<unsigned> sleepHelperCalls{0},fixtureWakeShimCalls{0},fixtureRulesShimCalls{0},fixturePowerReadyCalls{0};','separate unselected shim counters')
 interface_once('originalTargetCommit(false,[&]{publication(false);});','++fixtureRulesShimCalls;originalTargetCommit(false,[&]{publication(false);});','count unchanged unselected rules commit port')
 interface_once('static void steamOriginalRecoveryStart(){','static void steamOriginalRecoveryPowerReady(unsigned long long generation);\nstatic void steamOriginalRecoveryStart(){','original current PowerReady forward declaration')
 if getattr(args,'command_pending_case',None) not in ADVISORY_CASES:
  interface_once('static void steamOriginalRecoveryPowerReady(unsigned long long generation){','static void steamOriginalRecoveryPowerReady(unsigned long long generation){\n    ++fixturePowerReadyCalls;','observe unselected original PowerReady entry')
 interface_once('L"'+chr(92)+'evidence"','L"'+chr(92)+chr(92)+'evidence"','fixture root C++ escaping')
 interface_once('L"'+chr(92)+'runtime"','L"'+chr(92)+chr(92)+'runtime"','fixture runtime root C++ escaping')
 interface_once('std::vector<json> cases;unsigned assertions=0;','std::vector<json> cases,fixtureShimChecks;unsigned assertions=0;','selected-case shim raw observations')
 interface_once('{"error",error},{"ownedTreeNaturalJobZeroBeforeClose",cleanup}','{"error",error},{"shimChecks",fixtureShimChecks},{"ownedTreeNaturalJobZeroBeforeClose",cleanup}','include per-case actual zero-shim counts in final raw')
 # Referencing unselected static callbacks is not execution or a warning waiver.
 callbacks=['steamOriginalRecoveryTargetChanged','steamOriginalRecoverySteamChanged','steamOriginalRecoverySuspend','steamOriginalRecoveryWake','steamOriginalRecoveryPowerReady','steamOriginalRecoveryObserveHint','steamOriginalRecoveryBindingReady','steamOriginalRecoveryIdentityReady','steamOriginalRecoveryOwnsRunning','steamOriginalRecoveryStopped','steamOriginalRecoveryShutdown','steamOriginalRecoveryExportSnapshot','steamOriginalRecoveryRulesCommit','requireOriginalCalls']
 interface_once('(void)&steamOriginalRecoveryBusinessCommit;(void)&steamOriginalRecoveryTrackerObserved;','(void)&steamOriginalRecoveryBusinessCommit;(void)&steamOriginalRecoveryTrackerObserved;'+''.join('(void)&'+name+';' for name in callbacks),'compile-visible current callback declarations without invoking')
 # An alternate registry key does not isolate the observer's source mutex.
 from fixtures.recovery_command_group_member_wait import ADAPTER
 mutexPrefix='YMCCFixture.NotificationContinuity.' if getattr(args,'command_pending_case',None) in ADVISORY_CASES else 'YMCCFixture.CommandPending.'
 interface_once('#include "steam_session_observer.h"',ADAPTER.replace('YMCCFixture.GroupMemberWait.',mutexPrefix)+'#include "steam_session_observer.h"\n#undef CreateMutexW','process-scoped private fixture source mutex')
 # Epoch4: pure selector validates before the one original Observer is created.
 diagnosticEdits=[]
 def diagnostic_once(old,new,label):
  nonlocal source
  if source.count(old)!=1:raise ValueError('nonunique epoch4 fixture '+label)
  diagnosticEdits.append({'label':label,'before':old,'after':new})
  source=source.replace(old,new)
 diagnostic_once('static Observer g_steamSettingsObserver(contextRegistryPath);','static std::unique_ptr<Observer> fixtureObserverOwner;\n#define g_steamSettingsObserver (*fixtureObserverOwner)','defer the same original Observer, preserve global owner destruction order')
 diagnostic_once('    if(argc!=3||std::wstring_view(argv[2])!=L"--command-pending-only")return 41;\n    if(!argv[1][0])return 41;','    if(!fixtureParseStartupSelection(argc,argv))return 41;\n    fixtureObserverOwner=std::make_unique<Observer>(contextRegistryPath);','reject unknown empty invalid combinations before Observer/event creation')
 diagnostic_once('cases.size()==6','cases.size()==fixtureExpectedCaseCount','selected one or unchanged default six expected count')
 diagnostic_once('{"shimChecks",fixtureShimChecks}','{"shimChecks",fixtureShimChecks},{"case2Diagnostics",fixtureCase2DiagnosticsJson()},{"selectedCase",fixtureSelectedCase?json(fixtureSelectedCase):json(nullptr)}','best effort POD diagnostic raw after original cleanup')
 casesBytes=casesPath.read_bytes();controlBoundary=b'\n// New source-prepared cases; no prior execution or formal case ID is claimed.'
 if casesBytes.count(controlBoundary)!=1:raise ValueError('unique original-six/Control boundary required')
 if not getattr(args,'production_control_only',False):
  casesBytes=casesBytes[:casesBytes.index(controlBoundary)]
  if b'@@PRODUCTION_START@@' in casesBytes:raise ValueError('Control token in ordinary Startup cases')
 table='const wchar_t* advisoryWide[]={'+','.join('L'+json.dumps(name) for name in ADVISORY_CASES)+'};const char* advisoryNarrow[]={'+','.join(json.dumps(name) for name in ADVISORY_CASES)+'};for(unsigned i=0;i<'+str(len(ADVISORY_CASES))+';++i)if(std::wstring_view(argv[4])==advisoryWide[i]){fixtureAdvisoryMode=i+1;fixtureSelectedCase=advisoryNarrow[i];fixtureExpectedCaseCount=1;return true;}'
 if casesBytes.count(b'@@ADVISORY_CASE_TABLE@@')!=1:raise ValueError('unique advisory case mapping required')
 casesBytes=casesBytes.replace(b'@@ADVISORY_CASE_TABLE@@',table.encode())
 write(out/'command-pending-cases.inc',casesBytes);write(out/'runtime-composition.cpp',source)
 write(out/'original-worker-business-pass.inc',businessPass)
 for row in edits:
  if row['label'].startswith('count original'):continue
 # Independently reverse all runtime port/instrument edits in the namespace,
 # checking complete product namespace bytes, not only removed token names.
 recovered=ns
 for edit in reversed(edits):
  selected=edit['label'] in ('controlled IO storage declaration','existing fixture callback alias','count original admit entry, never replace admission') or edit['label'].startswith('controlled IO leaf ')
  if selected and edit['after'] and recovered.count(edit['after'])==1:recovered=recovered.replace(edit['after'],edit['before'])
 if recovered!=nsOriginal:raise ValueError('whole runtime namespace instrumentation inverse differs')
 write(out/'runtime-namespace-inverse.inc',recovered)
 names=re.findall(r'(?:test|startupCommandCase)\("([^"\n]+)"',casesPath.read_text())+ADVISORY_CASES
 manifest={'status':'GENERATED_ONLY_NOT_COMPILED_NOT_RUN','compileAuthorized':False,'businessAuthorized':False,'fragments':fragments,'headers':headerRows,'privateEdits':edits,'runtimeNamespaceInverseExact':True,'expectedCases':names,'originalSerialWorkerBusinessPassExact':True,'typedAdapterComplete':True,'template':pin(template),'cases':pin(casesPath),'runtime':pin(runtimePath),'freshMainObservation':pin(mainPath),'currentMainSnapshotCopied':False,'outputs':[pin(out/'runtime-composition.cpp'),pin(out/'command-pending-cases.inc')],'limits':['engine/QueryActor/runTrial remain controlled leaf','Start allocation case observes worker.start callback only; no real worker-start proof','Native game-valve storage/power/rules and IO leaves retain original fixture scope; not physical acceptance','C++ compile and business not authorized in this round'],'diagnosticEdits':diagnosticEdits,'allowedExplicitCases':names[1:],'requestedCase':getattr(args,'command_pending_case',None),'businessAuthorizedThisTask':False,'executionInterfaceEdits':executionEdits,'sleepContract':pin(sleepPath),'sleepPortScope':'UNSELECTED_STARTUP_DENY_ONLY_ZERO_CALL_REQUIRED','observationScope':'POD_TLS_ROUTE_ORIGINAL_SEQUENCE_COMMAND_SUFFIX','sourceReady':False}
 write(out/'PREPARED.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
 print(json.dumps({'status':manifest['status'],'fragments':len(fragments),'headers':len(headerRows),'cases':names,'source':pin(out/'runtime-composition.cpp')}))
 return 0


def prepare(args,extract):
 if not getattr(args,'production_control_only',False):return _prepare_e4(args,extract)
 if not args.prepare_only:raise ValueError('production control preparation cannot compile/run')
 import copy,shutil
 out=args.out.resolve()
 if out.exists():raise FileExistsError('exclusive production-control output required')
 baseArgs=copy.copy(args);baseArgs.out=out/'startup-e4-base'
 _prepare_e4(baseArgs,extract)
 base=baseArgs.out;original=(base/'runtime-composition.cpp').read_text(encoding='utf8');s=original;edits=[]
 repo=args.repo.resolve() if args.repo else Path(__file__).resolve().parents[2]
 runtimePath=repo/'native/steam_original_recovery_runtime.h';runtime=runtimePath.read_text(encoding='utf-8-sig')
 def change(old,new,label):
  nonlocal s
  if s.count(old)!=1:raise ValueError('production control nonunique '+label)
  s=s.replace(old,new);edits.append({'label':label,'before':old,'after':new})
 support=Path(__file__).with_name('production_control_support.inc').read_text(encoding='utf8')
 gate=Path(__file__).with_name('production_control_gate.inc').read_text(encoding='utf8')
 change('#include "steam_command_availability.h"',support+'\n#define CreateEventW pcCreateEventW\n#define CloseHandle pcCloseHandle\n#include "steam_command_availability.h"\n#undef CloseHandle\n#undef CreateEventW','actual backend macro instrumentation, native API pass-through')
 change('enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};',gate+'\nenum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};','thread-local independent publish gate state')
 old=function_body(s,'readJson',extract);at=body_opening(old,extract)+1
 change(old,old[:at]+'if(pcActive.load()){if(wtMode.load()==3){++wtInvalidConfigReads;return {nullptr,true,true};}return {json{{"enabled",false},{"targetMode","generic"}},true,false};}'+old[at:],'valid disabled configuration leaf')
 old=function_body(s,'publish',extract);at=body_opening(old,extract)+1
 change(old,old[:at]+'pcPublishGate(value);'+old[at:],'waiting-events handshake in existing publish leaf')
 old=function_body(s,'originalTargetCommit',extract);at=body_opening(old,extract)+1
 change(old,old[:at]+'if(pcActive.load()){++pcStopCommitCalls;publication();return;}'+old[at:],'selected external sleep stop boundary only')
 old=function_body(s,'runOne',extract);at=body_opening(old,extract)+1
 change(old,old[:at]+'if(pcActive.load())++pcRunOneCalls;'+old[at:],'POD runOne entry observation only, expected zero')
 change('    if(!fixtureParseStartupSelection(argc,argv))return 41;','    if(const auto production=fixtureProductionControlDispatch(argc,argv))return *production;\n    if(!fixtureParseStartupSelection(argc,argv))return 41;','new selector branch before Observer and SourceOwner')
 enqueueOriginal=function_body(s,'enqueue',extract);enqueueAt=body_opening(enqueueOriginal,extract)+1
 enqueueObserved=enqueueOriginal[:enqueueAt]+'\n    QaEnqueueObservation initialQueueObservation;\n'+enqueueOriginal[enqueueAt:]
 change(enqueueOriginal,enqueueObserved,'TLS initial-queue allocation boundary observation; real deque unchanged')
 terminalTakeNext=function_body(s,'takeNext',extract);terminalAt=body_opening(terminalTakeNext,extract)+1
 terminalObserved=terminalTakeNext[:terminalAt]+'\n    wtBeforeTakeNext();\n'+terminalTakeNext[terminalAt:]
 change(terminalTakeNext,terminalObserved,'exact terminal takeNext exception boundary, inactive for other parameters')
 firstLine=s[:s.index('\n')+1]
 startFailurePrefix=Path(__file__).with_name('production_control_start_failure_prefix.inc').read_text(encoding='utf8')
 initialQueuePrefix=Path(__file__).with_name('production_control_initial_queue_failure_prefix.inc').read_text(encoding='utf8')
 beforeLaunch='return ::_beginthreadex(security,stack,task,value,flags,tid);'
 if startFailurePrefix.count(beforeLaunch)!=1:raise ValueError('exact actual successful thread-launch arm boundary')
 startFailurePrefix=initialQueuePrefix+startFailurePrefix.replace(beforeLaunch,'return qaAfterLaunch(::_beginthreadex(security,stack,task,value,flags,tid));',1)
 terminalPrefix=Path(__file__).with_name('production_control_worker_terminal_prefix.inc').read_text(encoding='utf8')
 startFailurePrefix=terminalPrefix+startFailurePrefix
 change(firstLine,startFailurePrefix+firstLine,'controlled CRT create failure leaf; inactive outside exact new selector')
 start=function_body(runtime,'steamOriginalRecoveryStart',extract)
 observed=start.replace('std::make_shared<ymcc::steamcommand::AvailabilityEvent>()','fixtureAllocateAvailability()')
 if observed==start:raise ValueError('exact optional allocation leaf missing')
 actual='#define CreateEventW pcCreateEventW\n#define CloseHandle pcCloseHandle\n'+observed+'\n#undef CloseHandle\n#undef CreateEventW'
 cases=(base/'command-pending-cases.inc').read_text(encoding='utf8')
 if cases.count('@@PRODUCTION_START@@')!=1:raise ValueError('actual Start marker')
 finalCases=cases.replace('@@PRODUCTION_START@@',actual)
 startFailureCases=Path(__file__).with_name('production_control_start_failure_cases.inc').read_text(encoding='utf8')
 dispatch='static std::optional<int> fixtureProductionControlDispatch(int argc,wchar_t** argv){'
 if finalCases.count(dispatch)!=1:raise ValueError('unique production dispatch boundary')
 initialQueueCases=Path(__file__).with_name('production_control_initial_queue_failure_cases.inc').read_text(encoding='utf8')
 terminalCases=Path(__file__).with_name('production_control_worker_terminal_cases.inc').read_text(encoding='utf8')
 finalCases=finalCases.replace(dispatch,startFailureCases+'\n'+initialQueueCases+'\n'+terminalCases+'\n'+dispatch,1)
 selector=' if(argc<3||!std::wstring_view(argv[2]).starts_with(L"--production-control-"))return {};'
 if finalCases.count(selector)!=1:raise ValueError('unique production selector boundary')
 finalCases=finalCases.replace(selector,' if(const auto terminal=wtDispatch(argc,argv))return terminal;\n if(const auto initialFailure=qaDispatch(argc,argv))return initialFailure;\n if(const auto startupFailure=sfDispatch(argc,argv))return startupFailure;\n'+selector,1)
 inverse=s
 for e in reversed(edits):
  if inverse.count(e['after'])!=1:raise ValueError('nonunique production inverse '+e['label'])
  inverse=inverse.replace(e['after'],e['before'])
 if inverse!=original:raise ValueError('whole production CPP inverse mismatch')
 shutil.copytree(base/'include',out/'include');shutil.copytree(base/'deps',out/'deps')
 write(out/'runtime-composition.cpp',s);write(out/'command-pending-cases.inc',finalCases)
 write(out/'production-control-start-original.inc',start);write(out/'production-control-cpp-inverse.inc',inverse)
 manifest={'status':'PREPARED_ONLY_NO_COMPILE_NATIVE','base':pin(base/'PREPARED.json'),'source':pin(out/'runtime-composition.cpp'),'cases':pin(out/'command-pending-cases.inc'),'runtime':pin(runtimePath),'worker':pin(repo/'native/steam_original_recovery_worker.h'),'generator':pin(Path(__file__)),'edits':edits,'wholeCppInverseExact':True,'actualStart':{'original':start,'observed':observed,'onlyExpressionReplacement':'std::make_shared<ymcc::steamcommand::AvailabilityEvent>() -> fixtureAllocateAvailability()','nativeCreateCloseMacroProjection':True,'actualRecoveryWorkerStartPreserved':'SteamOriginalRecovery::worker.start([c]{' in observed},'selectors':['--production-control-success','--production-control-optional-signal-bad-alloc','--production-control-availability-protect-close6','--production-control-worker-create-failure','--production-control-initial-enqueue-failure','--production-control-worker-storage-terminal','--production-control-worker-take-next-terminal','--production-control-worker-invalid-config-live'],'compileAuthorized':False,'nativeAuthorized':False,'sourceReady':False,'limits':['Old six cases are not executed','group-across-wait NOT_VALIDATED','Availability open false fallback NOT_RUN','Controlled CRT thread-create failure uses actual RecoveryWorker/Start; no actual OS exhaustion/whole-main claim; initial-event enqueue refusal has its own explicit controlled selector; later enqueue failures and permanent refusal remain unqualified','No SourceOwner or actual Steam/runtime/field/CPU claim']}
 write(out/'PREPARED.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
 print(json.dumps({'productionControlPrepared':str(out),'source':pin(out/'runtime-composition.cpp'),'selectors':manifest['selectors'],'compile':0,'native':0}))

def execute(args):
 """Run one explicitly selected original case through the shared owned producer."""
 import subprocess
 repo=args.repo.resolve() if args.repo else Path(__file__).resolve().parents[2]
 tools=json.loads((repo/'tools/ymcc_sim/global/sleep-native-recovery-consumer/toolchain-inputs.json').read_text(encoding='utf-8-sig'))
 helper=Path(__file__).with_name('recovery_command_pending_run.mjs')
 command=[tools['nodeExecutable'],str(helper),'--repo',str(repo),'--out',str(args.out.resolve()),'--case',args.command_pending_case]
 if args.command_pending_reuse:command+=['--reuse',str(args.command_pending_reuse.resolve())]
 return subprocess.call(command)
