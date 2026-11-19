"""Additive original group-member wait parameter on the original Wake entry.
Preparation only. Actual Observer/Tracker/GroupLease plus raw engine wait/guards;
no complete handle/SDK/publisher or installed Steam proof.
"""
import hashlib,json,re
from pathlib import Path
CASES=['required-launcher-exit','strict-unused-launcher-exit','request-cancel-and-launcher-exit']
PREFIX='#include <span>\n#include "steam_game_group_lease.h"\nnamespace SteamGroupMemberWait {\nusing Current=std::function<bool()>;using WakeSignal=std::uintptr_t;\ninline std::string selected;inline HANDLE waitEntered=nullptr;\ninline unsigned waitCalls=0,nativeWaitCount=0;\ninline void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}\n'
MONITOR='\nDWORD monitorWait(DWORD n,const HANDLE* h,BOOL all,DWORD ms){++waitCalls;nativeWaitCount=n;need(waitEntered&&SetEvent(waitEntered),"member entered notify");return ::WaitForMultipleObjects(n,h,all,ms);}\n#define WaitForMultipleObjects monitorWait\n'
TAIL='\n#undef WaitForMultipleObjects\ntemplate<class Lease>std::span<const HANDLE> borrowAdditional(const std::shared_ptr<Lease>& group){\n if constexpr(requires(const Lease& l){l.additionalProcessLiveness();})return group->additionalProcessLiveness();\n else return {};\n}\ntemplate<class Wait>WakeWait invokeWait(Wait wait,const std::array<HANDLE,4>& p,WakeSignal hint,WakeSignal cancelled,std::span<const HANDLE> extra){\n if constexpr(std::is_invocable_r_v<WakeWait,Wait,const std::array<HANDLE,4>&,WakeSignal,WakeSignal,WakeSignal,DWORD,WakeSignal,std::span<const HANDLE>>)\n  return wait(p,hint,0,0,1000,cancelled,extra);\n else return wait(p,hint,0,0,1000,cancelled);\n}\n}\n'
ADAPTER='#include <string>\n#include <string_view>\nstatic HANDLE WINAPI memberPrivateSourceMutex(LPSECURITY_ATTRIBUTES sa,BOOL owner,LPCWSTR name){\n const std::wstring prefix=L"Local\\\\YeManCC.SteamSource.v1.";\n if(!name||std::wstring_view(name).find(prefix)!=0){SetLastError(ERROR_INVALID_NAME);return nullptr;}\n const auto scope=L"Local\\\\YMCCFixture.GroupMemberWait."+std::to_wstring(GetCurrentProcessId())+L"."+std::wstring(name+prefix.size());\n return ::CreateMutexW(sa,owner,scope.c_str());\n}\n#define CreateMutexW memberPrivateSourceMutex\n'
SELECTOR='        if(argc==5&&std::wstring_view(argv[2])==L"--tracker-process-group-roles"&&std::wstring_view(argv[3])==L"--member-wait-only"){\n            SteamGroupMemberWait::selected=W2U(argv[4]);\n            if(SteamGroupMemberWait::selected!="required-launcher-exit"&&SteamGroupMemberWait::selected!="strict-unused-launcher-exit"&&SteamGroupMemberWait::selected!="request-cancel-and-launcher-exit")throw std::runtime_error("unknown group member wait case");\n            argc=4;argv[3]=const_cast<wchar_t*>(L"--fresh-opt-in-only");\n        }\n'

UNUSED=['static std::wstring U2W(','static void nativeSteamGameTrackerClose(','static void nativeSteamGameTrackerReleaseExecution(','static NativePrimaryRead readNativePrimary(']
def pin(path):
 b=path.read_bytes();return {'path':str(path),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 with path.open('xb')as f:f.write(data)
def declaration(text,name,mask):
 clean=mask(text);hits=list(re.finditer(re.escape(name)+r'\s*\{',clean))
 if len(hits)!=1:raise RuntimeError('nonunique group wait declaration '+name)
 begin=hits[0].start();end=clean.index('{',begin)+1;depth=1
 while depth and end<len(clean):depth+=(clean[end]=='{')-(clean[end]=='}');end+=1
 if depth:raise RuntimeError('unbalanced group wait declaration '+name)
 return text[begin:clean.index(';',end)+1]
def prepare(args,repo,extract,mask):
 if not args.command_wake_only or not args.prepare_only or args.case not in CASES:raise ValueError('exact group-member Wake prepare selection required')
 if args.owned_availability_protect_close is not None:raise ValueError('group-member wait cannot select availability close fault')
 out=args.out.resolve()
 if out.exists():raise FileExistsError('new group-member preparation output required')
 here=Path(__file__).resolve().parent;source=repo/'native/steam_original_recovery_engine.cpp';raw=source.read_bytes();engine=raw.decode('utf-8-sig')
 basecpp=repo/'tools/steam_session_observer_selftest.cpp';baseinc=repo/'tools/steam_game_group_role_cases.inc';rawcpp=basecpp.read_bytes();rawinc=baseinc.read_bytes()
 bodyfile=here/'recovery_command_group_member_wait_cases.inc';body=bodyfile.read_bytes().decode('utf-8');parts=[]
 def take(name):
  value,_,_=extract(engine,name,balanced=True);parts.append({'name':name,'bytes':len(value.encode()),'sha256':hashlib.sha256(value.encode()).hexdigest()});return value
 pieces=[declaration(engine,'enum class NativeRoleMode',mask),declaration(engine,'struct NativeRoleChoice',mask),take('groupActionCurrent'),take('groupObservationCurrent'),declaration(engine,'enum class WakeWait',mask)]
 wait=take('waitWakeObservation');helper=PREFIX+'\n'.join(pieces)+MONITOR+wait+TAIL
 changes=[];cpp=rawcpp.decode('utf-8-sig')
 def change(a,b):
  nonlocal cpp
  if cpp.count(a)!=1:raise RuntimeError('nonunique original group fixture anchor '+a[:80])
  cpp=cpp.replace(a,b,1);changes.append((a,b))
 change('#include "steam_game_group_role_cases.inc"',helper+'#include "steam_game_group_role_cases.inc"')
 change('        const bool optInOnly=argc==4&&',SELECTOR+'        const bool optInOnly=argc==4&&')
 change('#include "steam_session_observer.h"',ADAPTER+'#include "steam_session_observer.h"\n#undef CreateMutexW')
 change('#include <windows.h>\r\n','#include <windows.h>\r\n#include "steam_command_availability.h"\n')
 a='if(argc!=3||(std::wstring_view(argv[2])!=L"--steam-log-source"&&std::wstring_view(argv[2])!=L"--tracker-fresh-gamelink"))'
 b='if(!(argc==5&&std::wstring_view(argv[2])==L"--tracker-process-group-roles"&&std::wstring_view(argv[3])==L"--member-wait-only")&&(argc!=3||(std::wstring_view(argv[2])!=L"--steam-log-source"&&std::wstring_view(argv[2])!=L"--tracker-fresh-gamelink")))'
 change(a,b)
 for a in UNUSED:change(a,'[[maybe_unused]] '+a)
 inverse=cpp
 for a,b in reversed(changes):
  if inverse.count(b)!=1:raise RuntimeError('group fixture inverse ambiguity')
  inverse=inverse.replace(b,a,1)
 if inverse.encode()!=rawcpp:raise RuntimeError('whole original group cpp inverse mismatch')
 inc=rawinc.decode('utf-8-sig');anchor='    if(optInOnly){'
 if inc.count(anchor)!=1:raise RuntimeError('original group case boundary not unique')
 inc=inc.replace(anchor,body+'\n'+anchor,1)
 if inc.replace(body+'\n','',1).encode()!=rawinc:raise RuntimeError('whole original cases inverse mismatch')
 queue=[basecpp,baseinc,repo/'native/steam_command_availability.h'];frozen={};missing=[]
 while queue:
  p=queue.pop().resolve()
  if p in frozen:continue
  data=p.read_bytes();frozen[p]=data
  for n in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"',data,re.M):
   name=n.decode();found=next((q.resolve()for q in(p.parent/name,repo/'native'/name,repo/'tools'/name,repo/'deps/json'/name)if q.is_file()),None)
   if found:queue.append(found)
   else:missing.append((str(p),name))
 if missing:raise RuntimeError('unresolved original group quote inputs '+repr(missing))
 for p,data in frozen.items():
  generated=cpp.encode()if p==basecpp else inc.encode()if p==baseinc else data
  write(out/p.relative_to(repo),generated)
 if source.read_bytes()!=raw or any(p.read_bytes()!=b for p,b in frozen.items()):raise RuntimeError('input changed during group wait preparation')
 report={'status':'PREPARED_NOT_COMPILED_NOT_EXECUTED','sourceReady':False,'caseSelection':args.case,
  'runtimeArgumentsRecipe':['<new-owned-root>','--tracker-process-group-roles','--member-wait-only',args.case],
  'source':pin(out/'tools/steam_session_observer_selftest.cpp'),'cases':pin(out/'tools/steam_game_group_role_cases.inc'),
  'inputPins':[pin(p)for p in frozen]+[pin(source),pin(bodyfile),pin(Path(__file__)),pin(repo/'tools/fixtures/recovery_command_wake.py'),pin(repo/'tools/steam_original_recovery_success_chain_selftest.py')],
  'extractions':parts,'wholeCppInverseRawExact':True,'wholeCasesInverseRawExact':True,'privateOriginalSourceNamespaceOnly':True,
  'copied':[{'origin':str(p.relative_to(repo)),'destination':str(p.relative_to(repo)),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}for p,b in frozen.items()],
  'defaultGroupsExecuted':False,'compile':0,'native':0,'completeEngineHandleExecuted':False,'resourcePass':False,
  'limitations':['Actual original Observer/Tracker/GroupLease and raw engine wait/guards only.','Full Engine handle/native SDK/typed publisher, installed process lifecycle, input/sleep and CPU not validated.','No new permanent test ID, minimum or release gate.']}
 write(out/'PREPARED.json',(json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode());print(json.dumps({'status':report['status'],'caseSelection':args.case,'source':report['source'],'copied':len(frozen)}));return 0
