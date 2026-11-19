"""Additive Wake preparation for the original success-chain entry.
Only selected by --command-wake-only --prepare-only. Never compiles or launches.
Product definitions come from the current engine, not a retained generated CPP.
"""
from pathlib import Path
from .recovery_command_group_member_wait import CASES as GROUP_MEMBER_CASES
import hashlib,json,re

CASES=['occupied-release-ready-still-4', 'occupied-release-ready-now-8196', 'original-physical-remainder-expires', 'power-cancel-and-availability', 'identity-cancel-and-availability', 'process-event-and-availability', 'actual-closed-event-wait-failed', 'driver-readonly-preflight-no-durable-consume', 'healthy-wake-no-consume', 'memory-tuple-mismatch-refused']+GROUP_MEMBER_CASES
BASE=['need', 'utf16', 'utf8', 'decimal', 'dword', 'digest', 'textHash', 'fileHash', 'freshUuid', 'absolute', 'statePresent', 'readJson', 'durableCreate', 'atomicJson']
REFERENCES=['pin', 'pins', 'life', 'referenceScope', 'referenceSchema', 'scopeKey', 'auditedPrepublicationSource', 'auditedLegacyTransfer', 'consume', 'groupScalar', 'referenceNonceValid', 'matchReferenceIdentity', 'referencePinComparison', 'matchReference', 'commitHealthyReferenceFile', 'replaceHealthyReference', 'legacyReferenceScope', 'legacyReferenceKey', 'readLegacyReferenceBytes']
NATIVE_HEADERS=['steam_command_coordinator.h','steam_command_availability.h','steam_command_disposition.h','steam_original_recovery_storage.h','steam_original_recovery_config.h','steam_original_recovery_wake_watch.h','steam_original_recovery_file_hash.h']
SUPPORT=['recovery_startup_handoff_unsupported.h','resource_diag.h','resource_diag_json.h','owned_resource_accounting.h','owned_resource_accounting_json.h']

def sha(data):return hashlib.sha256(data).hexdigest()
def pin(path):
 data=path.read_bytes();return {'path':str(path),'bytes':len(data),'sha256':sha(data)}
def write(path,data):
 with path.open('xb') as stream:stream.write(data)

def declaration(text,marker,mask):
 clean=mask(text);hits=list(re.finditer(re.escape(marker)+r'\s*(?:final\s*)?\{',clean))
 if len(hits)!=1:raise RuntimeError('ambiguous declaration '+marker)
 start=hits[0].start();end=clean.index('{',start)+1;depth=1
 while depth and end<len(clean):depth+=(clean[end]=='{')-(clean[end]=='}');end+=1
 if depth or clean[end:].lstrip()[:1]!=';':raise RuntimeError('unbalanced declaration '+marker)
 return text[start:clean.index(';',end)+1]

def prepare(args,repo,extract,mask):
 # Validate every selection before creating any output, event or process.
 if not args.command_wake_only or not args.prepare_only:raise ValueError('Wake requires explicit prepare-only')
 if args.case is not None and args.case not in CASES:raise ValueError('unknown/empty exact Wake case')
 if args.owned_availability_protect_close is not None and (args.owned_availability_protect_close!='6' or args.case!=CASES[0]):raise ValueError('protect-close6 requires exact original Recovered selector')
 if args.case in GROUP_MEMBER_CASES:
  from .recovery_command_group_member_wait import prepare as prepare_group_member_wait
  return prepare_group_member_wait(args,repo,extract,mask)
 out=args.out.resolve()
 if out.exists():raise FileExistsError('Wake preparation output must be new')
 here=Path(__file__).resolve().parent;source=repo/'native/steam_original_recovery_engine.cpp';template=here/'recovery_command_wake.cpp.in'
 raw=source.read_bytes();engine=raw.decode('utf-8-sig');template_raw=template.read_bytes();cpp=template_raw.decode('utf-8-sig');parts=[];bodies={}
 def take(name):
  body,start,end=extract(engine,name,balanced=True);bodies[name]=body
  parts.append({'name':name,'source':'native/steam_original_recovery_engine.cpp','startLine':engine.count('\n',0,start)+1,'endLine':engine.count('\n',0,end)+1,'bytes':len(body.encode()),'sha256':sha(body.encode()),'rawInternalEolRetained':True});return body
 def decl(name):
  body=declaration(engine,name,mask);bodies[name]=body;parts.append({'name':name,'source':'native/steam_original_recovery_engine.cpp','bytes':len(body.encode()),'sha256':sha(body.encode()),'rawInternalEolRetained':True});return body
 replacements={
  '@@BASE@@':take('need')+'\n'+decl('struct H')+'\n'+'\n'.join(take(n)for n in BASE if n!='need'),
  '@@REFERENCES@@':'\n'.join(take(n)for n in REFERENCES),
  '@@PUBLICATION@@':take('requirePublicationScope'),
  '@@CLOCKS@@':take('nowFileTime')+'\n'+take('threadCpuTicks')+'\n'+take('wakeDeadlineTick'),
  '@@NATIVE_TYPES@@':decl('enum class NativeRoleMode')+'\n'+decl('struct NativeRoleChoice')+'\n'+decl('struct Scope')+'\n'+take('groupActionCurrent')+'\n'+take('groupObservationCurrent'),
  '@@SNAPSHOT_TYPE@@':decl('struct Snapshot'),
  '@@SEED@@':take('importReference')+'\n'+take('referenceFromSeed'),
  '@@LOAD_REFERENCE@@':take('loadCompatibleReference'),
  '@@NATIVE_STATE@@':take('nativeState'),
  '@@WAKE_WAIT@@':decl('enum class WakeWait')+'\n'+take('waitWakeObservation'),
  '@@HANDLE@@':take('handle')}
 start=engine.index('enum class RecoveryRoute ');end=engine.index('std::atomic<bool> gActive',start)
 constants='\n'.join(re.search(r'constexpr const char\* '+n+r'="[^"]+";',engine)[0]for n in ['kEngineRevision','kModuleQualification'])
 replacements['@@ROUTES@@']=constants+'\n'+engine[start:end]
 for token,value in replacements.items():
  if cpp.count(token)!=1:raise RuntimeError('missing/nonunique token '+token)
  cpp=cpp.replace(token,value,1)
 if '@@'in cpp:raise RuntimeError('unreplaced Wake token')
 for name,body in bodies.items():
  if cpp.count(body)!=1:raise RuntimeError('nonunique full raw embedding '+name)
 restored=cpp
 for token,value in sorted(replacements.items(),key=lambda p:len(p[1]),reverse=True):
  if restored.count(value)!=1:raise RuntimeError('nonunique inverse token '+token)
  restored=restored.replace(value,token,1)
 if restored.encode()!=template_raw:raise RuntimeError('whole Wake fragment inverse mismatch')
 copies=[(repo/'native'/n,n)for n in NATIVE_HEADERS]+[(repo/'deps/json/json.hpp',n)for n in ['fixture_json.hpp','json.hpp']]+[(here/n,n)for n in SUPPORT]
 frozen={p:p.read_bytes()for p,_ in copies};frozen[source]=raw;frozen[template]=template_raw
 out.mkdir(parents=True,exist_ok=False)
 for origin,name in copies:write(out/name,frozen[origin])
 write(out/'command-wake-consumer.cpp',cpp.encode())
 if any(p.read_bytes()!=data for p,data in frozen.items()):raise RuntimeError('Wake input changed during preparation')
 native_args=['<new-owned-root>','--command-wake-only']
 if args.case is not None:native_args+=['--case',args.case]
 if args.owned_availability_protect_close is not None:native_args+=['--owned-availability-protect-close=6']
 report={'status':'PREPARED_NOT_COMPILED_NOT_EXECUTED','sourceReady':False,'compile':0,'native':0,'caseSelection':args.case,'runtimeArgumentsRecipe':native_args,'selectedCaseRun':False,'inputPins':[pin(p)for p in frozen]+[pin(Path(__file__)),pin(repo/'tools/steam_original_recovery_success_chain_selftest.py')],'source':pin(out/'command-wake-consumer.cpp'),'extractions':parts,'wholeFragmentInverseRawExact':True,'copied':[{'origin':str(p.relative_to(repo)),'destination':name,'bytes':len(frozen[p]),'sha256':sha(frozen[p])}for p,name in copies],'defaultSuccessChainTemplateUnchanged':True,'processWideResources':'UNKNOWN','resourcePass':False,'limitations':['Preparation only; no compiler/link/runtime validation.','Memory identity/SDK/engine leaves are explicit. Full groups, real Steam/game/SourceOwner and field recovery not validated.','Five direct-owned namespaces and original whole-process UNKNOWN remain separate; no count offset.','Join-refusal and cleanup allocation failure are not certified by normal source pairing.']}
 write(out/'PREPARED.json',(json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode());print(json.dumps(report,ensure_ascii=False));return 0
