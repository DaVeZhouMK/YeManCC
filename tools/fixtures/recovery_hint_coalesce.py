"""Directed ordinary notification extension; original dual-route default is unchanged.
Compile complete extracted enqueue/cancellation/takeNext, real thread/cv, SDK clock leaf.
This is queue dispatch coverage, NOT runOne/route/SourceLease or physical CPU proof.
"""
from pathlib import Path
import datetime,hashlib,json,os,re,subprocess,time

def digest(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def pin(p):
 p=Path(p).resolve();return {'path':str(p),'bytes':p.stat().st_size,'sha256':digest(p)}
def save(p,b):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('xb') as f:f.write(b.encode('utf-8') if isinstance(b,str) else b)
def output(p,v):save(p,json.dumps(v,ensure_ascii=False,indent=2)+'\n')
def run(args,extract):
 if args.notification_case=='command-ready-registration':return prepare_command_ready_registration(args,extract)
 prepared=prepare_ordinary_notification_inputs(args,extract)
 begin=prepared['begin'];repo=prepared['repo'];runtime=prepared['runtime'];out=prepared['out'];template=prepared['template'];source=prepared['source'];policy=prepared['policy']
 names=prepared['names'];extracted=prepared['extracted'];mutations=prepared['mutations'];adapter=prepared['adapter'];dependencyInputs=prepared['dependencyInputs']
 authority=repo/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
 tool=json.loads(authority.read_text(encoding='utf-8-sig'))['native']
 env={k.upper():v for k,v in os.environ.items()}
 env['PATH']=';'.join([str(Path(tool['compiler']).parent),'C:/Windows/System32','C:/Windows'])
 env['INCLUDE']=';'.join(tool['include']);env['LIB']=';'.join(tool['lib'])
 for k in ['CL','_CL_','LINK','_LINK_']:env[k]=''
 env['VSCMD_SKIP_SENDTELEMETRY']='1'
 # Before compiler invocation declare/pin every possible configured SDK/STL header,
 # configured .lib, and the fixed compiler's executable/DLL directory. No source tree copy.
 allowed={}
 def add(p):
  p=Path(p).resolve();allowed[str(p).lower()]=pin(p)
 for root in tool['include']:
  for p in Path(root).rglob('*'):
   if p.is_file():add(p)
 for root in tool['lib']:
  for p in Path(root).glob('*.lib'):add(p)
 for p in Path(tool['compiler']).parent.rglob('*'):
  if p.is_file() and p.suffix.lower() in ['.exe','.dll','.config']:add(p)
 for p in [source,out/policy.name,authority,tool['commandProcessor'],Path(__file__),template,runtime,Path(__file__).parents[1]/'steam_original_recovery_dual_route_selftest.py']:add(p)
 for p in dependencyInputs:add(p)
 exe=out/'fixture.exe';obj=out/'fixture.obj'
 command=[tool['compiler'],'/nologo','/std:c++20','/W4','/WX','/wd4505','/O2','/EHsc','/MT','/utf-8','/showIncludes',str(source),'/Fe:'+str(exe),'/Fo:'+str(obj),'/link','/incremental:no','/verbose:lib']
 declaration={'declaredUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'command':command,'cwd':str(out),'environment':{k:env[k] for k in ['PATH','INCLUDE','LIB','CL','_CL_','LINK','_LINK_','VSCMD_SKIP_SENDTELEMETRY']},'inputs':list(allowed.values()),'extractions':extracted,'mutations':mutations,'oldAdapter':adapter,'selection':args.notification_case,'expectedCases':names if args.notification_case=='all' else [args.notification_case],'actualThreadAndCv':True,'clockLeaf':'SDK GetTickCount64 or controlled atomic tick; no live process/Steam/sleep/device action','fullProductTypeOrLinkClaimed':False}
 output(out/'DECLARATION.json',declaration)
 built=subprocess.run(command,cwd=out,env=env,capture_output=True,timeout=120)
 save(out/'compile.stdout',built.stdout);save(out/'compile.stderr',built.stderr)
 report={'selection':args.notification_case,'compileExit':built.returncode,'elapsedSeconds':time.time()-begin,'source':pin(source),'declaration':pin(out/'DECLARATION.json'),'runtime':pin(runtime),'mutations':mutations,'cases':[],'oldAdapter':adapter}
 if built.returncode==0:
  # MSVC emits include paths and /verbose:lib inputs. Every observed existing file
  # must belong to the predeclared exact path/hash set; no arbitrary inclusion.
  log=(built.stdout+built.stderr).decode('utf-8',errors='replace')
  paths=set()
  for line in log.splitlines():
   for match in re.finditer(r'([A-Za-z]:[\\/].*?\.(?:h|hpp|hxx|inl|inc|lib))(?=\s*[:(\r\n]|\s*$)',line,re.I):
    p=Path(match[1].strip()).resolve()
    if p.is_file():paths.add(str(p))
  observed=[]
  for p in sorted(paths):
   row=pin(p);prior=allowed.get(p.lower())
   if prior!=row:raise ValueError('actual compiler input undeclared/drifted:'+p)
   observed.append(row)
  if not any(p.lower().endswith('.lib') for p in paths):raise ValueError('actual link library graph missing')
  if not any(p.lower().endswith('thread') or 'thread' in Path(p).name.lower() for p in paths):
   # Extensionless STL includes are separately emitted; consume every showIncludes line.
   pass
  for line in log.splitlines():
   m=re.search(r'([A-Za-z]:[\\/].*)$',line)
   if not m:continue
   p=Path(m[1].strip())
   if p.is_file() and p.suffix.lower()!='.lib':
    row=pin(p);key=str(p.resolve()).lower()
    if key not in allowed or row!=allowed[key]:raise ValueError('actual header undeclared/drifted:'+str(p))
    if row not in observed:observed.append(row)
  output(out/'ACTUAL-GRAPH.json',{'inputs':observed,'predeclaredMembers':len(allowed),'source':pin(source),'exe':pin(exe),'object':pin(obj),'compilerAndLinker': [pin(tool['compiler']),pin(tool['linker'])],'runtimeLoadedDllClosureClaimed':False})
  ran=subprocess.run([str(exe),'--case',args.notification_case],cwd=out,env=env,capture_output=True,timeout=20)
  save(out/'run.stdout',ran.stdout);save(out/'run.stderr',ran.stderr)
  detail=json.loads(ran.stdout);report.update({'runExit':ran.returncode,**detail,'exe':pin(exe)})
  selected=names if args.notification_case=='all' else [args.notification_case]
  report['selectionExact']=[c['name'] for c in detail['cases']]==selected
  report['success']=ran.returncode==0 and report['selectionExact'] and all(c['passed'] for c in detail['cases'])
  # Same compiled binary rejects unknown and empty selection before any case execution.
  if args.notification_case=='all' and args.notification_control=='none':
   controls=[]
   for i,selection in enumerate(['not-a-real-case','']):
    bad=subprocess.run([str(exe),'--case',selection],cwd=out,env=env,capture_output=True,timeout=3)
    save(out/f'unknown-{i}.stdout',bad.stdout);save(out/f'unknown-{i}.stderr',bad.stderr)
    controls.append({'selection':selection,'exit':bad.returncode,'stdoutBytes':len(bad.stdout),'rejectedBeforeCases':bad.returncode==2 and not bad.stdout})
   report['selectorControls']=controls;report['success']=report['success'] and all(x['rejectedBeforeCases'] for x in controls)
 else:report['success']=False
 report['elapsedSeconds']=time.time()-begin
 output(out/'report.json',report)
 print(json.dumps({k:v for k,v in report.items() if k not in ['declaration','source','runtime','exe']},ensure_ascii=False))
 return 0 if report['success'] else 1


def prepare_command_ready_registration(args, extract):
 repo=(args.repo or Path(__file__).resolve().parents[2]).resolve()
 runtime=(args.runtime_source or repo/'native/steam_original_recovery_runtime.h').resolve()
 if not args.prepare_only or args.notification_control!='none':
  raise ValueError('command-ready-registration requires preparation-only original producer composition')
 text=runtime.read_text(encoding='utf-8-sig');out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
 template=Path(__file__).with_suffix('.cpp.in');cpp=template.read_text(encoding='utf-8-sig');fragments=[]
 def take(name,pattern=None,semi=False):
  value,start,end=extract(text,pattern or r'^static\s+[^;{}]*?\b'+name+r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{',semi)
  save(out/(name+'.inc'),value);fragments.append({'name':name,'source':str(runtime),'startLine':text.count('\n',0,start)+1,'endLine':text.count('\n',0,end)+1,'bytes':len(value.encode()),'sha256':hashlib.sha256(value.encode()).hexdigest()});return value
 def typ(name,kind='struct'):return take(name,r'\b'+kind+r'\s+'+name+r'\b[^;{}]*\{',True)
 types='\n'.join(['using RecoveryRoute=ymcc::steamOriginal::RecoveryRoute;using Identity=ymcc::steamOriginal::Identity;using Scope=ymcc::steamOriginal::Scope;',
 'struct Control;struct StartupFreshRequest;struct StartupFreshWait;struct StartupCommandWait;',typ('Pending'),typ('StartupFreshRequest'),typ('StartupFreshPhase','enum class'),typ('StartupFreshWait'),typ('StartupCommandSignal'),typ('StartupCommandPhase','enum class'),typ('StartupCommandWait'),typ('CommandRetirement'),typ('GroupRequestTarget','enum class'),typ('ActiveGroupRequest'),r'''
// Storage only. No worker/runOne/engine/identity implementation is imitated.
struct Control {
 std::mutex mx;std::condition_variable cv;bool stop=false;std::deque<Pending> pending;
 std::shared_ptr<void> observationEvent;
 std::shared_ptr<ymcc::steamcommand::AvailabilityEvent> mutationAvailability;
 std::shared_ptr<StartupCommandWait> startupCommandWait;
 std::shared_ptr<ActiveGroupRequest> activeGroupRequest;
 std::shared_ptr<StartupFreshRequest> startupFreshRequest;
 std::array<std::shared_ptr<StartupFreshWait>,2> startupFreshWaits;
 U64 sequence=0,observationSequence=0,latestPower=0;std::atomic<bool> ownsRunning{false};
 Identity boundGame,boundSteam;std::atomic<bool> active{false};bool bindingQualified=false;U64 bindingSerial=0;
 std::string boundGameId;std::uint32_t boundAppId=0;std::shared_ptr<void> boundSleepRequest;
};
'''])
 notify=take('StartupCommandSignal_notify',r'^inline\s+void\s+StartupCommandSignal::notify\(\)noexcept\s*\{')
 predicate=take('startupCommandNotifiedLocked');old='    return c->startupCommandWait&&c->startupCommandWait->signal->notified.load(std::memory_order_acquire);';new='    const bool observed=c->startupCommandWait&&c->startupCommandWait->signal->notified.load(std::memory_order_acquire);\n    fixtureCommandPredicate(c.get(),observed);return observed;'
 if predicate.count(old)!=1:raise ValueError('exact false-read predicate instrumentation anchor')
 instrumented=predicate.replace(old,new)
 prelude=r'''
#include <optional>
#include "json.hpp"
#include "steam_original_recovery_dual_route.h"
#include "steam_command_availability.h"
using json=nlohmann::json;
struct NativeGameSelectionReceipt {unsigned long long powerGeneration=1;};
namespace ymcc::steamnative {struct ReadScope {};}
namespace ymcc::steamgamefacts {
struct RequestCancellation {std::atomic<unsigned> cancels{0};void cancel()noexcept{++cancels;}};
struct TrackerCompletionWitness {};struct TrackerCompletionHint {};
struct TrackerCompletionSubscription {std::atomic<unsigned> cancels{0};void cancel()noexcept{++cancels;}};
}
static void fixtureCommandPredicate(const void*,bool)noexcept;
'''
 leaf=r'''
// Exact fixture-current memory leaf only; no scope/account/identity qualification.
static bool startupFreshCurrent(const std::shared_ptr<Control>& c,const std::shared_ptr<StartupFreshRequest>& request)noexcept {
 if(!c||!request)return false;std::lock_guard<std::mutex> lock(c->mx);
 return !c->stop&&c->startupFreshRequest==request&&c->sequence==request->sequence&&!request->cancelled.load()&&!request->terminal.load();
}
'''
 functions='\n'.join([notify,take('retireStartupCommandLocked'),leaf,take('startupCommandCurrent'),instrumented,take('takeReadyStartupCommand')])
 parts={'COMMAND_READY':'1','COMMAND_PRELUDE':prelude,'COMMAND_TYPES':types,'COMMAND_FUNCTIONS':functions,'SIGNAL_LOCKS_CONTROL':'1' if 'lock(held->mx)' in notify else '0',
 'PHASE':'','PENDING':'','CANCEL_GROUP':take('cancelGroupRequestLocked'),'CANCEL_FRESH':take('cancelStartupFreshLocked'),'ENQUEUE':take('enqueue'),'TAKE':take('takeNext'),'STOP':take('steamOriginalRecoveryRequestStop'),'TRACKER':take('steamOriginalRecoveryTrackerObserved'),'TRACKER_HAS_ABSENCE':'1' if 'bool definitivelyAbsent' in text else '0','HAS_DEADLINE':'true','NATIVE_COMMAND_CLOSURE':'0'}
 for k,v in parts.items():
  token='@@'+k+'@@'
  if cpp.count(token)!=1:raise ValueError('command-ready token missing/nonunique:'+token)
  cpp=cpp.replace(token,v)
 if '@@' in cpp:raise ValueError('unreplaced command-ready fixture token')
 save(out/'fixture.cpp',cpp)
 output(out/'PREPARED.json',{'status':'PREPARED_ONLY_NO_COMPILE_NO_BUSINESS','selection':args.notification_case,'runtime':pin(runtime),'source':pin(out/'fixture.cpp'),'template':pin(template),'fragments':fragments,'instrumentation':{'function':'startupCommandNotifiedLocked','before':old,'after':new,'inverseExact':instrumented.replace(new,old)==predicate},'signalLocksControl':parts['SIGNAL_LOCKS_CONTROL']=='1','originalTakeNextWaitUnchanged':True,'controlStoragePort':True,'startupFreshCurrent':'memory leaf only; no true scope/account or engine wiring','sourceReady':False})
 print(json.dumps({'status':'PREPARED_ONLY_NO_COMPILE_NO_BUSINESS','selection':args.notification_case,'fragments':len(fragments),'signalLocksControl':parts['SIGNAL_LOCKS_CONTROL']=='1'}))
 return 0


def _ordinary_complete_function(text,name,extract):
 clean=extract.__globals__['masked'](text);found=[]
 for m in re.finditer(r'^static\s+[^;{}]*?\b'+re.escape(name)+r'\s*\(',clean,re.M):
  p=clean.index('(',m.start())+1;depth=1
  while depth:depth+=(clean[p]=='(')-(clean[p]==')');p+=1
  while clean[p].isspace():p+=1
  if clean[p:p+8]=='noexcept':
   p+=8
   while clean[p].isspace():p+=1
  if clean[p]!='{':continue
  end=p+1;depth=1
  while depth:depth+=(clean[end]=='{')-(clean[end]=='}');end+=1
  found.append(text[clean.rfind('\n',0,m.start())+1:end])
 if len(found)!=1:raise ValueError('unique complete ordinary dependency '+name+': '+str(len(found)))
 return found[0]

def _ordinary_current_command_closure(repo,runtime,text,out,extract):
 """Actual command/Current bodies; original legacy storage ports stay explicit.
 No Observer/Steam/engine is constructed. Native/account/power qualification is
 NOT established by an old queue case merely compiling these unselected guards.
 """
 records=[]
 def body(name,origin=runtime,source=text):
  value=_ordinary_complete_function(source,name,extract)
  save(out/'actual-dependencies'/(name+'.inc'),value)
  records.append({'name':name,'origin':pin(origin),'bytes':len(value.encode()),'sha256':hashlib.sha256(value.encode()).hexdigest(),'wholeDefinitionExactAfterTextNewlineDecode':True})
  return value
 def typ(name,kind='struct',source=text,origin=runtime):
  value=extract(source,r'\b'+kind+r'\s+'+re.escape(name)+r'\b[^;{}]*\{',True)[0]
  save(out/'actual-dependencies'/(name+'.inc'),value)
  records.append({'name':name,'origin':pin(origin),'bytes':len(value.encode()),'sha256':hashlib.sha256(value.encode()).hexdigest(),'wholeDefinitionExactAfterTextNewlineDecode':True})
  return value
 mainPath=repo/'native/main.cpp';main=mainPath.read_text(encoding='utf-8-sig')
 mainTypes='\n'.join(typ(n,k,main,mainPath) for n,k in [('NativeGameDiscoveryState','enum class'),('NativeDetectedGame','struct'),('NativeGameValveSnapshot','struct'),('NativeGameSelectionReceipt','struct')])
 # Existing state leaves, never a replacement Current implementation. These
 # globals are not populated/elected by the queue tests and grant nothing.
 state=r'''
static std::atomic<bool> g_exitRequested{false},g_sleepRecoveryAutoEnabled{false};
static std::atomic<unsigned long long> g_sleepRecoveryAutoConfigEpoch{0},g_gameRulesEpoch{0},g_powerGeneration{0};
enum class PowerLifecycle {Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::mutex g_gameValveMx;
static NativeGameValveSnapshot g_gameValveTarget;
'''
 prelude='\n'.join(['#include "include/steam_command_availability.h"','#include "include/steam_native_read_scope.h"','#include "include/steam_game_tracker_facts.h"','#include "include/steam_original_recovery_dual_route.h"',mainTypes,state,body('currentPowerGeneration',mainPath,main),body('nativeValveSelectionCurrentLocked',mainPath,main),body('nativeValveSelectionCurrent',mainPath,main)])
 types='\n'.join(['using RecoveryRoute=ymcc::steamOriginal::RecoveryRoute;using Identity=ymcc::steamOriginal::Identity;using Scope=ymcc::steamOriginal::Scope;','struct Control;struct StartupCommandWait;',typ('Pending'),typ('StartupCommandSignal'),typ('StartupCommandPhase','enum class'),typ('StartupCommandWait'),typ('CommandRetirement')])
 notify=extract(text,r'^inline\s+void\s+StartupCommandSignal::notify\(\)noexcept\s*\{')[0]
 save(out/'actual-dependencies/StartupCommandSignal-notify.inc',notify)
 records.append({'name':'StartupCommandSignal::notify','origin':pin(runtime),'bytes':len(notify.encode()),'sha256':hashlib.sha256(notify.encode()).hexdigest(),'wholeDefinitionExactAfterTextNewlineDecode':True})
 functions='\n'.join([notify]+[body(n) for n in ['retireStartupCommandLocked','startupFreshRouteIndex','startupFreshScopeCurrent','startupFreshCurrent','startupCommandCurrent','startupCommandNotifiedLocked','takeReadyStartupCommand']])
 copied={};edges=[]
 def header(p):
  p=p.resolve()
  if str(p) in copied:return
  relative=p.relative_to(repo);relativeText=relative.as_posix()
  if relative.parts[0]=='native':destination=out/'include'/Path(*relative.parts[1:])
  elif relative.parts[0]=='deps':destination=out/relative
  else:raise ValueError('unmapped actual ordinary header '+relativeText)
  save(destination,p.read_bytes());copied[str(p)]={'input':pin(p),'output':pin(destination)}
  for quoted in re.findall(r'^\s*#\s*include\s*"([^"\r\n]+)"',p.read_text(encoding='utf-8-sig'),re.M):
   choices=[p.parent/quoted,repo/'native'/quoted,repo/'deps/json'/quoted]
   dependency=next((v.resolve() for v in choices if v.is_file()),None)
   if dependency is None:raise ValueError('unresolved actual ordinary header '+relativeText+': '+quoted)
   dependency.relative_to(repo)
   edges.append({'parent':relativeText,'include':quoted,'dependency':dependency.relative_to(repo).as_posix()});header(dependency)
 for name in ['steam_original_recovery_policy.h','steam_command_availability.h','steam_native_read_scope.h','steam_game_tracker_facts.h','steam_original_recovery_dual_route.h']:header(repo/'native'/name)
 metadata={'status':'ACTUAL_COMMAND_AND_CURRENT_SOURCE_CLOSURE_ONLY','runtime':pin(runtime),'main':pin(mainPath),'definitions':records,'headers':list(copied.values()),'quotedEdges':edges,'currentChain':'actual startupCommandCurrent -> startupFreshCurrent -> startupFreshScopeCurrent -> actual selection comparison and actual public ReadScope::current','legacyStoragePorts':'Existing default-constructible StartupFreshRequest/Wait and counted group cancellation storage are retained and field-extended for the actual guards; not original production Control/Tracker implementation.','guardsExercisedByLegacyCases':False,'scopeOrAccountAuthorityProven':False,'compilerInvoked':False,'businessExecuted':False}
 output(out/'ACTUAL-COMMAND-CURRENT-CLOSURE.json',metadata)
 return {'PENDING':types,'COMMAND_PRELUDE':prelude,'COMMAND_FUNCTIONS':functions,'NATIVE_COMMAND_CLOSURE':'1'},[mainPath]+[Path(v[k]['path']) for v in copied.values() for k in ['input','output']],metadata

def prepare_ordinary_notification_inputs(args,extract):
 """Source-only callable for one existing ordinary case; never invokes a toolchain.
 The original CLI still decides execution; this callable is also the separately
 authorized future prepare-only correspondence hook, not a new CLI or case.
 """
 begin=time.time();repo=(args.repo or Path(__file__).resolve().parents[2]).resolve()
 runtime=(args.runtime_source or repo/'native/steam_original_recovery_runtime.h').resolve()
 template=Path(__file__).with_suffix('.cpp.in');text=runtime.read_text(encoding='utf-8-sig')
 cpp=template.read_text(encoding='utf-8-sig')
 table=cpp[cpp.index('static const Case cases[]={'):cpp.index('int main(int argc,char** argv)')]
 if table.count('#else')!=1 or table.count('#endif')!=1:raise ValueError('unique ordinary case table branch')
 ordinaryTable=table.split('#else',1)[1].split('#endif',1)[0]
 names=re.findall(r'\{"([^"\n]+)",\w+\}',ordinaryTable)
 if not args.notification_case or (args.notification_case!='all' and args.notification_case not in names):
  raise ValueError('unknown/empty ordinary notification selection: '+args.notification_case)
 out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
 extracted=[]
 def take(name,pattern=None,semi=False):
  code,start,end=extract(text,pattern or r'^static\s+[^;{}]*?\b'+name+r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{',semi)
  row={'name':name,'source':str(runtime),'startLine':text.count('\n',0,start)+1,'endLine':text.count('\n',0,end)+1,'sha256':hashlib.sha256(code.encode()).hexdigest(),'bytes':len(code.encode())}
  extracted.append(row);save(out/(name+'.inc'),code);return code
 parts={'PENDING':take('Pending',r'\bstruct\s+Pending\s*\{',True),
        'PHASE':take('StartupFreshPhase',r'\benum\s+class\s+StartupFreshPhase\s*\{',True),
        'CANCEL_GROUP':take('cancelGroupRequestLocked'),'CANCEL_FRESH':take('cancelStartupFreshLocked'),
        'ENQUEUE':take('enqueue'),'STOP':take('steamOriginalRecoveryRequestStop'),
        'TRACKER':take('steamOriginalRecoveryTrackerObserved'),
        'TRACKER_HAS_ABSENCE':'1' if 'bool definitivelyAbsent' in text else '0'}
 has='static bool takeNext(' in text
 adapter=None
 if has:
  parts['TAKE']=take('takeNext')
  if 'if(!takeNext(c,next))break;' not in text:raise ValueError('production run not wired to exact helper')
 else:
  # Exact old run wait/pop body; only loop break becomes a bool return for the fixture.
  original='{std::unique_lock<std::mutex> lock(c->mx);c->cv.wait(lock,[&]{return c->stop||!c->pending.empty();});\n            if(c->stop)break;next=c->pending.front();c->pending.pop_front();}'
  if text.count(original)!=1:raise ValueError('old production wait block missing/ambiguous')
  save(out/'old-run-wait-block.inc',original)
  parts['TAKE']='static bool takeNext(const std::shared_ptr<Control>& c,Pending& next)'+original.replace('if(c->stop)break;','if(c->stop)return false;')+'\nreturn true;\n}'
  # Put the original block inside the function rather than use it as the function braces.
  parts['TAKE']='static bool takeNext(const std::shared_ptr<Control>& c,Pending& next){\n'+original.replace('if(c->stop)break;','if(c->stop)return false;')+'\nreturn true;\n}'
  adapter={'source':'original run wait/pop block','onlySemanticAdaptation':'loop break to false return; true on successful pop','originalSha256':hashlib.sha256(original.encode()).hexdigest()}
 mutations=[]
 if args.notification_control=='delay-urgent':
  old='&&!urgent';new='&&(urgent||!urgent)'
  if parts['TAKE'].count(old)!=1:raise ValueError('urgent mutation not unique')
  parts['TAKE']=parts['TAKE'].replace(old,new);mutations.append({'part':'takeNext','old':old,'new':new})
 elif args.notification_control=='refresh-deadline':
  old='if(item.kind==kind&&item.generation==generation)return;'
  new='if(item.kind==kind&&item.generation==generation){if(kind==Trigger::TargetChanged)c->pending.front().notBeforeTick=GetTickCount64()+100;return;}'
  if parts['ENQUEUE'].count(old)!=1:raise ValueError('refresh mutation not unique')
  parts['ENQUEUE']=parts['ENQUEUE'].replace(old,new);mutations.append({'part':'enqueue','old':old,'new':new})
 parts['HAS_DEADLINE']='true' if has else 'false'
 parts.update({'COMMAND_READY':'0','COMMAND_PRELUDE':'','COMMAND_TYPES':'','COMMAND_FUNCTIONS':'','SIGNAL_LOCKS_CONTROL':'0','NATIVE_COMMAND_CLOSURE':'0'})
 dependencyInputs=[];actualClosure=None
 if 'struct StartupCommandWait' in text:
  commandParts,dependencyInputs,actualClosure=_ordinary_current_command_closure(repo,runtime,text,out,extract)
  parts.update(commandParts)
  cpp=cpp.replace('#include "steam_original_recovery_policy.h"','#include "include/steam_original_recovery_policy.h"',1)
 for key,value in parts.items():
  token='@@'+key+'@@'
  if cpp.count(token)!=1:raise ValueError('fixture token missing/nonunique:'+token)
  cpp=cpp.replace(token,value)
 if '@@' in cpp:raise ValueError('unreplaced fixture token')
 source=out/'fixture.cpp';save(source,cpp)
 policy=repo/'native/steam_original_recovery_policy.h';save(out/policy.name,policy.read_bytes())
 prepared={'begin':begin,'repo':repo,'runtime':runtime,'out':out,'template':template,'source':source,'policy':policy,'names':names,'extracted':extracted,'mutations':mutations,'adapter':adapter,'dependencyInputs':dependencyInputs,'actualClosure':actualClosure}
 output(out/'ORDINARY-SOURCE-PREPARED.json',{'status':'SOURCE_ONLY_NO_COMPILE_NO_BUSINESS','selection':args.notification_case,'expectedCases':names if args.notification_case=='all' else [args.notification_case],'runtime':pin(runtime),'source':pin(source),'template':pin(template),'actualCommandCurrentClosure':pin(out/'ACTUAL-COMMAND-CURRENT-CLOSURE.json') if actualClosure else None,'compile':0,'business':0,'sourceReady':False})
 return prepared
