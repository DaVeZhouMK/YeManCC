#!/usr/bin/env python3
"""R34 fixed ASCII source key emission, offline source-bound MSVC fixture.
--prepare snapshots/generates only. --verify-run builds and checks correctness.
Timing requires a separate parent permission receipt and one permanent attempt.
All writes are confined to R34/key-worker. Production and R31 remain untouched.
"""
from __future__ import annotations
import argparse,datetime as dt,difflib,hashlib,json,re,subprocess,sys,uuid
from pathlib import Path
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[1]
OUTPUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R34-20261015/key-worker'
VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN={'schemaVersion':1,'candidate':'R34-fixed-ASCII-key-emission-only','profiles':['Release','Checked'],
 'campaigns':2,'pairedRounds':11,'minimumFasterRounds':10,'minimumMedianPairedReduction':.05,
 'minimumRatioOfMediansReduction':.05,'noAllocationIncrease':'ordinary/aligned requests and requested bytes independently',
 'scenarios':['synthetic-neutral','synthetic-dynamic-no-touch','synthetic-imu-touch-screen'],
 'scopes':['full-serializer-and-output-disposal','current-assembly-serializer-and-disposal'],
 'totalRows':24,'batchSize':32,'measurementBatches':128,'warmupBatches':6,'allocationBatches':4,
 'profileOrder':['Release','Checked','Checked','Release'],'pairOrder':'alternate first variant by round+campaign',
 'cacheState':'Actual independent production thread-local caches; warm both before each row; no cache candidate or timing-driven mutation.',
 'fixtures':'Synthetic only; no hardware archive or live API reads. Predetermined numeric/touch/screen values reside in frozen fixture.cpp.',
 'timingBoundary':'Scope1 receives preassembled actual frame; scope2 invokes current actual frame assembly per operation. Both include output destruction; scope2 also includes frame destruction. Context/input construction, oracle/parsing, reporting and allocation-count instrumentation are outside timing.',
 'decision':'Every affected row in both profiles/campaigns must pass all gates. No threshold/workload relaxation, retuning or retiming.',
 'permissions':'Prepare and correctness build authorized; performance held until separate parent review and explicit release.',
 'productionEdits':False,'wholeHardwareClaim':False}

def sha(x):return hashlib.sha256(x.encode('utf-8') if isinstance(x,str) else x).hexdigest()
def write(p,x,exclusive=False):
 p=Path(p).resolve()
 if not p.is_relative_to(OUTPUT.resolve()):raise RuntimeError('write outside R34 scope: '+str(p))
 p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('xb' if exclusive else 'wb') as f:f.write(x.encode('utf-8') if isinstance(x,str) else x)
def save(p,x,exclusive=False):write(p,json.dumps(x,ensure_ascii=False,indent=2)+'\n',exclusive)
def mask(s):return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',lambda m:''.join('\n' if c=='\n' else ' ' for c in m.group()),s)
def close(s,a):
 t=mask(s);depth=0
 for i in range(a,len(t)):
  if t[i]=='{':depth+=1
  elif t[i]=='}':
   depth-=1
   if not depth:return i
 raise RuntimeError('unterminated source block')
def extract(s,marker,cls=False):
 hits=[]
 for m in re.finditer(re.escape(marker),s):
  a=m.start();b=s.find('{',a);semi=s.find(';',a)
  if b>=0 and (semi<0 or b<semi):
   e=close(s,b)+1
   if cls:
    if s[e]!=';':raise RuntimeError('missing struct terminator')
    e+=1
   hits.append(s[a:e])
 if len(hits)!=1:raise RuntimeError(f'expected one definition {marker}: {len(hits)}')
 return hits[0]
def uncomment(s):
 return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',lambda m:(' '*len(m.group()) if m.group().startswith(('//','/*')) else m.group()),s)

def audit_prior():
 roots=sorted((REPO.parents[2]/'Build/Validation').glob('VirtualInputPerf*'))
 files=list(REPO.glob('tools/*perf*.py'))+list(REPO.glob('tools/*perf*.cpp'))
 for root in roots:
  if root.resolve()==OUTPUT.parent.resolve():continue
  files.extend(p for p in root.rglob('*') if p.is_file() and (p.suffix=='.patch' or (p.suffix in ('.py','.hpp','.cpp') and ('emitter' in p.name or p.name.endswith('selftest.py') or p.name in ('before.hpp','candidate.hpp','generated.hpp')))))
 hits=[];bindings={}
 for p in sorted(set(files)):
  if p.name in ('virtual_wire_key_perf_selftest.py','virtual_wire_key_perf_selftest.cpp'):continue
  text=p.read_text(encoding='utf-8-sig',errors='replace');bindings[str(p)]=sha(p.read_bytes())
  if p.suffix=='.patch':
   additions='\n'.join(line[1:] for line in text.splitlines() if line.startswith('+') and not line.startswith('+++'))
   if re.search(r'out\s*(?:\+=\s*name\s*;|\.append\(name\))',additions):hits.append(str(p))
  for match in re.finditer(r'auto key\s*=\s*\[&\]\(const char\* name\)\s*\{',text):
   body=text[match.start():close(text,text.index('{',match.start()))+1]
   if 'c6aAppendEscaped' not in body and re.search(r'out\s*\+=\s*name\s*;',body):hits.append(str(p))
 report={'candidateAttemptedPreviously':bool(hits),'exactCandidateHits':sorted(set(hits)),
  'examinedArtifactCount':len(bindings),'examinedSha256':bindings,
  'scope':'Earlier VirtualInputPerf candidate patches/emitter/generated source and existing performance tools. R4 number formatter and R9 field iterator are separate changes.',
  'stopIfAttempted':True}
 save(OUTPUT/'PRIOR-ATTEMPT-AUDIT.json',report)
 if hits:raise RuntimeError('fixed key candidate already attempted: STOP; see prior audit')
 return report

def fixed_calls(builder):
 relay=extract(builder,'auto field = [&](const char* name)')
 if len(re.findall(r'\bkey\s*\(\s*name\s*\)',relay))!=1:raise RuntimeError('field forwarding changed')
 relay_start=builder.index(relay);relay_end=relay_start+len(relay)
 calls=[];allowed={builder.index('auto key')+5,builder.index('auto field')+5}
 body=uncomment(builder)
 for m in re.finditer(r'\b(key|field)\s*\(([^()]*)\)',body):
  kind,arg=m.groups();arg=arg.strip();allowed.add(m.start())
  if kind=='key' and arg=='name' and relay_start<=m.start()<relay_end:continue
  literal=re.fullmatch(r'"([A-Za-z][A-Za-z0-9]*)"',arg)
  if not literal:raise RuntimeError('dynamic/invalid builder key invocation: '+m.group())
  calls.append({'call':kind,'name':literal.group(1),'offset':m.start()})
 for m in re.finditer(r'\b(key|field)\b',mask(builder)):
  if m.start() not in allowed:raise RuntimeError('aliased or indirect key/field reference')
 if len({c['name'] for c in calls})!=len(calls):raise RuntimeError('duplicate fixed builder key')
 return calls

def guard_checks(builder):
 variants={
  'dynamic-field':builder.replace('field("buttons");','field(dynamicName);',1),
  'dynamic-key-outside-relay':builder.replace('field("buttons");','field("buttons"); key(name);',1),
  'non-ASCII-key':builder.replace('field("buttons");','field("中文");',1),
  'underscore-key':builder.replace('field("buttons");','field("bad_key");',1),
  'duplicate-key':builder.replace('field("buttons");','field("backButtons");',1),
  'aliased-key':builder.replace('field("buttons");','field("buttons"); auto alias=key; alias(name);',1),
  'indirect-key':builder.replace('field("buttons");','field("buttons"); std::invoke(key,name);',1)}
 results=[]
 for name,value in variants.items():
  try:fixed_calls(value)
  except RuntimeError as e:results.append({'case':name,'rejected':True,'reason':str(e)})
  else:raise RuntimeError('static key guard accepted forbidden variant: '+name)
 return results

def parts(source,pad):
 builder=extract(source,'static std::string inputHostBuildSubmitFrameText(')
 key=extract(builder,'auto key = [&](const char* name)')
 calls=fixed_calls(builder)
 old=re.search(r'c6aAppendEscaped\(out, name\);\s*out\.push_back\(\':\'\);',key)
 if not old:raise RuntimeError('key lambda no longer matches reviewed two statements')
 nl='\r\n' if '\r\n' in key else '\n'
 replacement=r'''out.push_back('"');
        out += name;
        out += "\":";'''.replace('\n',nl)
 # Only the key lambda's two original statements are replaced.
 newkey=key[:old.start()]+replacement+key[old.end():]
 after=builder.replace(key,newkey,1)
 assert after.replace(newkey,key,1)==builder
 markers=[('class C6aNumberTextCache',True),('static C6aNumberTextCache& c6aNumberTextCache()',False),
  ('static void c6aAppendEscaped(std::string& out, const std::string& value)',False),
  ('static void c6aAppendEscaped(std::string& out, const char* value)',False),
  ('static void c6aAppendUInt(',False),('static void c6aAppendInt(',False),('static void c6aAppendJsonValue(',False)]
 helpers=[{'marker':m,'source':extract(source,m,c)} for m,c in markers]
 parity=extract(source,'static void inputHostVerifySubmitFrameParity(')
 oracle='''static std::string wireOracle(const json& frame,const std::string& requestId,unsigned long long sequence,
 unsigned long long sampleSequence,unsigned long long monotonicTick,const std::string& normalizedInputHash,const char* motionPairProofId) {
'''+parity[parity.index('    json reference = json{'):parity.index('    const std::string referenceText = reference.dump();')]+'    return reference.dump();\n}\n'
 constants=[]
 for name in ['kInputHostProtocolConfigHash','kInputHostTargetId','kInputHostOwnerIdentity']:
  values=re.findall(r'^static constexpr const char\* '+name+r'\s*=[^;]*;',source,re.M)
  if len(values)!=1:raise RuntimeError('constant declaration ambiguous: '+name)
  constants.append(values[0])
 pads=[extract(pad,m,c) for m,c in [('struct Contact {',True),('struct Snapshot {',True),('inline Json snapshotJson(',False)]]
 frame=extract(source,'struct FocusFrameContent',True);assembler=extract(source,'static json focusIsolationAssembleFrame(')
 normalize=extract(source,'static constexpr float inputHostNormalizeAxis(')
 return {'builder':builder,'candidateBuilder':after,'key':key,'candidateKey':newkey,'calls':calls,'helpers':helpers,
  'parity':parity,'oracle':oracle,'constants':constants,'pads':pads,'frame':frame,'assembler':assembler,'normalize':normalize}

def generated(p):
 out='\n'.join(p['constants'])+'''
// Explicit inert identity/context seams; no OS, Host, mutex, crypto or device calls.
static unsigned long long g_inputHostConfigRevision=17,g_inputHostEpoch=23;
static std::string g_inputHostRunId="01234567-89AB-4CDE-8123-0123456789AB";
static std::string g_inputHostStartupNonce="89ABCDEF-0123-4567-89AB-CDEF01234567";
static std::string fixturePersona="steamdeck",fixtureProfile="steam-deck-composite";
static unsigned long long fixturePower=29;
static const char* inputHostBoundOrCurrentPersona(){return fixturePersona.c_str();}
static const char* inputHostBoundOrCurrentProfileIdentity(){return fixtureProfile.c_str();}
static unsigned long long inputHostProtocolPowerGenerationLocked(){return fixturePower;}
'''+p['oracle']+'namespace ymcc::screenpads {using Json=json;\n'+'\n'.join(p['pads'])+'\n}\n'+p['frame']+'\n'+p['normalize']+'\n'+p['assembler']+'\n'
 for name,builder in [('baseline',p['builder']),('candidate',p['candidateBuilder'])]:
  out+='#define private public\nnamespace '+name+' {\n'+'\n'.join(h['source'] for h in p['helpers'])+'\n'+builder+'\n}\n#undef private\n'
 out+='static constexpr unsigned r34FixedKeyCount='+str(len(p['calls']))+';\n'
 return out

def bindings(run):return {str(p.relative_to(run)).replace('\\','/'):sha(p.read_bytes()) for p in sorted(run.rglob('*')) if p.is_file() and p.name!='manifest.json'}
def verify_bindings(run,m):
 for name,digest in m['artifacts'].items():
  p=(run/name).resolve()
  if not p.is_relative_to(run.resolve()) or sha(p.read_bytes())!=digest:raise RuntimeError('snapshot changed: '+name)
def source_stable(m):return all(sha(Path(v['path']).read_bytes())==v['sha256'] for v in m['sourceInputs'].values())
def prepare(reference=None):
 OUTPUT.mkdir(parents=True,exist_ok=True);audit=audit_prior()
 if (OUTPUT/'PREDECLARED-PLAN.json').exists():
  if json.loads((OUTPUT/'PREDECLARED-PLAN.json').read_text(encoding='utf-8'))!=PLAN:raise RuntimeError('frozen R34 plan changed')
 else:save(OUTPUT/'PREDECLARED-PLAN.json',PLAN,True)
 run=OUTPUT/('prepare-'+dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'-'+uuid.uuid4().hex[:6]);run.mkdir()
 paths={'native-main.snapshot.cpp':REPO/'native/main.cpp','screen-touchpads.snapshot.h':REPO/'native/screen_touchpads.h','json.hpp':REPO/'deps/json/json.hpp','host-program.snapshot.cs':REPO/'InputHost/Program.cs'}
 raws={n:p.read_bytes() for n,p in paths.items()};source=raws['native-main.snapshot.cpp'].decode('utf-8-sig');pad=raws['screen-touchpads.snapshot.h'].decode('utf-8-sig');p=parts(source,pad)
 if reference:
  reference=Path(reference).resolve()
  if not reference.is_relative_to(OUTPUT.resolve()):raise RuntimeError('rebind reference outside R34 prepared artifacts')
  previous=json.loads((reference/'manifest.json').read_text(encoding='utf-8'));verify_bindings(reference,previous)
  oldraw=(reference/'native-main.snapshot.cpp').read_bytes()
  oldliteral=b'fdb9e78e1b75c72686f67707d9c6cab7e33d93f16269c4b554b0c574f7135997'
  newliteral=b'f6f19c08667409f6ffa3adf014c845181c589aa035cda1ab32fae39b0732849b'
  if oldraw.count(oldliteral)!=1 or oldraw.replace(oldliteral,newliteral,1)!=raws['native-main.snapshot.cpp']:
   raise RuntimeError('rebind exceeds exact parent-announced fan manifest literal drift')
  oldparts=parts(oldraw.decode('utf-8-sig'),(reference/'screen-touchpads.snapshot.h').read_bytes().decode('utf-8-sig'))
  if oldparts!=p:raise RuntimeError('input builder/helper/assembler/touch scope changed')
  if (reference/'fixture.cpp').read_bytes()!=Path(__file__).with_suffix('.cpp').read_bytes():raise RuntimeError('same frozen fixture required for rebind')
  if (reference/'plan.json').read_bytes()!=(OUTPUT/'PREDECLARED-PLAN.json').read_bytes():raise RuntimeError('same frozen plan required')
  for name in paths:
   if name!='native-main.snapshot.cpp' and (reference/name).read_bytes()!=raws[name]:raise RuntimeError('non-Native source dependency drifted')
  if (reference/'source-bound.h').read_bytes()!=generated(p).encode('utf-8'):raise RuntimeError('generated actual production bodies changed')
  save(run/'source-drift-rebind.json',{'referenceRun':str(reference),'oldNativeSha256':sha(oldraw),'newNativeSha256':sha(raws['native-main.snapshot.cpp']),
   'onlyWholeFileDifference':'updater expectedFanHostV2ManifestSha256 literal','oldLiteral':oldliteral.decode(),'newLiteral':newliteral.decode(),
   'allInputScopePartsByteIdentical':True,'allGeneratedProductionBodiesByteIdentical':True,
   'frozenFixtureSha256':sha((reference/'fixture.cpp').read_bytes()),'sameFixture':True,'samePlan':True,'fanChangePreserved':True,
   'freshCorrectnessRequestedByParent':True,'noFixtureTuning':True})
 for name,data in raws.items():write(run/name,data)
 write(run/'runner.snapshot.py',Path(__file__).read_bytes());write(run/'fixture.cpp',Path(__file__).with_suffix('.cpp').read_bytes())
 write(run/'source-bound.h',generated(p));save(run/'plan.json',PLAN)
 save(run/'static-key-guard-tests.json',{'pass':True,'cases':guard_checks(p['builder'])})
 save(run/'prior-attempt-audit.json',audit)
 for name,key in [('original-builder.cpp','builder'),('candidate-builder.cpp','candidateBuilder'),('actual-parity.cpp','parity'),('actual-assembler.cpp','assembler')]:write(run/name,p[key])
 newsource=source.replace(p['builder'],p['candidateBuilder'],1)
 assert newsource.replace(p['candidateBuilder'],p['builder'],1)==source
 write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(source.splitlines(True),newsource.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')))
 save(run/'source-extraction.json',{'fixedCalls':p['calls'],'fixedNamePattern':'[A-Za-z][A-Za-z0-9]*',
  'onlyDynamicInvocation':'key(name) inside field lambda; every caller field() has a checked ASCII source literal',
  'noDynamicExternalKeys':True,'originalKeyLambda':p['key'],'candidateKeyLambda':p['candidateKey'],
  'helperHashes':{h['marker']:sha(h['source']) for h in p['helpers']},'frameStruct':p['frame'],
  'touchSnapshotSources':p['pads'],'oracleSourceSha256':sha(p['parity']),'onlyKeyLambdaChanged':True,
  'genericValueAndNestedKeyPathsUnchanged':True,'contextSeams':'Only fixture identity globals/getters are inert explicit stubs; actual constants, append/cache bodies, frame assembler/normalizer, contact/snapshot and snapshotJson are copied verbatim.',
  'syntheticFixturesOnly':True,'priorAttemptFound':False})
 m={'recordType':'immutable-R34-source-preparation','runName':run.name,'prepared':True,'correctnessPassed':False,
  'buildReady':True,'benchmarkReady':False,'benchmarkExecuted':False,'productionEdited':False,
  'sourceInputs':{n:{'path':str(paths[n]),'sha256':sha(raw)} for n,raw in raws.items()},'planSha256':sha((run/'plan.json').read_bytes()),
  'sourceStable':all(paths[n].read_bytes()==raw for n,raw in raws.items()),'artifacts':bindings(run)}
 save(run/'manifest.json',m);save(OUTPUT/'READINESS.json',{'state':'build-ready-compile-and-correctness-next','run':str(run),'benchmarkExecuted':False,'performanceHeld':True,'fixedKeys':len(p['calls']),'sourceInputs':m['sourceInputs']})
 print(json.dumps({'run':str(run),'buildReady':True,'compiled':False,'fixedKeyCalls':len(p['calls']),'priorAttemptsFound':False,'performanceHeld':True},indent=2))
 return run

def invoke(args,run,stem,timeout=300):
 r=subprocess.run(args,cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=timeout)
 write(run/(stem+'.stdout.log'),r.stdout);write(run/(stem+'.stderr.log'),r.stderr)
 save(run/(stem+'-process.json'),{'returnCode':r.returncode,'args':args})
 return r

def verify(run):
 run=Path(run).resolve();m=json.loads((run/'manifest.json').read_text(encoding='utf-8'));verify_bindings(run,m)
 if not source_stable(m):raise RuntimeError('current source drift before correctness; rebind scope instead of restoring other work')
 if (run/'CORRECTNESS-ATTEMPT.json').exists():raise RuntimeError('correctness already attempted in immutable run; preserve results')
 save(run/'CORRECTNESS-ATTEMPT.json',{'startedUtc':dt.datetime.now(dt.timezone.utc).isoformat()},True)
 reports={}
 for profile in PLAN['profiles']:
  print('building/verifying '+profile+': '+run.name,flush=True)
  flags=['/O2','/DNDEBUG','/MT'] if profile=='Release' else ['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']
  args=['cl','/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/W4','/fp:precise',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(run/(profile+'.exe'))]
  batch=run/(profile+'-build.bat');write(batch,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\n')
  result=invoke(['cmd.exe','/d','/c',str(batch)],run,profile+'-build')
  if result.returncode:raise RuntimeError('MSVC compile failed: '+str(run))
  result=invoke([str(run/(profile+'.exe')),'verify'],run,profile+'-verify')
  report=json.loads(result.stdout);save(run/(profile+'-correctness.json'),report)
  reports[profile]={'exitCode':result.returncode,**report}
 passed=all(r['pass'] and r['exitCode']==0 for r in reports.values())
 ready={'pass':passed and source_stable(m),'profiles':reports,'sourceStable':source_stable(m),'preparedRun':str(run),'benchmarkExecuted':False,'performanceHeld':True,'planSha256':m['planSha256']}
 save(run/'correctness-result.json',ready)
 save(run/'verified-artifact-hashes.json',{'sha256':bindings(run)})
 save(OUTPUT/'READINESS.json',{'state':'correctness-ready-await-parent-performance-release' if ready['pass'] else 'correctness-failed','run':str(run),'profiles':{p:{k:r[k] for k in ['pass','assertions','failureCount','candidateSourceDifferences','oracleDifferences','wireCases']} for p,r in reports.items()},'benchmarkExecuted':False,'performanceHeld':True,'sourceInputs':m['sourceInputs']})
 print(json.dumps({'run':str(run),'pass':ready['pass'],'profiles':{p:{k:r[k] for k in ['pass','assertions','failureCount','candidateSourceDifferences','oracleDifferences','wireCases']} for p,r in reports.items()},'benchmarkExecuted':False},indent=2))
 return ready['pass']

def benchmark(args):
 run=Path(args.benchmark_ready_run).resolve();m=json.loads((run/'manifest.json').read_text(encoding='utf-8'))
 ready=json.loads((run/'correctness-result.json').read_text(encoding='utf-8'))
 if not ready['pass'] or not source_stable(m):raise RuntimeError('passing current source correctness required')
 verify_bindings(run,m)
 for name,digest in json.loads((run/'verified-artifact-hashes.json').read_text(encoding='utf-8'))['sha256'].items():
  if sha((run/name).read_bytes())!=digest:raise RuntimeError('verified artifact changed: '+name)
 if json.loads((run/'plan.json').read_text(encoding='utf-8'))!=PLAN:raise RuntimeError('frozen plan differs')
 permission=json.loads(Path(args.permission_file).read_text(encoding='utf-8'))
 for key,value in {'benchmarkApproved':True,'approver':'parent','preparedRunName':run.name,'nativeMainSha256':m['sourceInputs']['native-main.snapshot.cpp']['sha256']}.items():
  if permission.get(key)!=value:raise RuntimeError('parent permission does not bind '+key)
 claim={'preparedRun':str(run),'permission':permission,'singleAttempt':True,'noRetiming':True,'startedUtc':dt.datetime.now(dt.timezone.utc).isoformat()}
 save(OUTPUT/'FORMAL-BENCHMARK-ATTEMPT.json',claim,True);save(run/'BENCHMARK-ATTEMPT.json',claim,True)
 groups=[]
 for campaign in (1,2):
  for profile in (PLAN['profiles'] if campaign==1 else list(reversed(PLAN['profiles']))):
   print(f'campaign {campaign} {profile}',flush=True)
   r=invoke([str(run/(profile+'.exe')),'benchmark',str(campaign)],run,f'{profile}-campaign-{campaign}')
   report=json.loads(r.stdout);save(run/f'{profile}-campaign-{campaign}.json',report);groups.append({'profile':profile,'campaign':campaign,'exitCode':r.returncode,**report})
 summary={'pass':source_stable(m) and all(g['pass'] and g['exitCode']==0 for g in groups),'sourceStable':source_stable(m),'results':groups,'productionEdited':False,'retiming':False}
 save(run/'benchmark-summary.json',summary)
 print(json.dumps({'pass':summary['pass'],'passedRows':sum(r['pass'] for g in groups for r in g['rows']),'totalRows':24,'run':str(run)},indent=2))
 return summary['pass']

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepare',action='store_true');p.add_argument('--rebind-from');p.add_argument('--verify-run');p.add_argument('--benchmark-ready-run');p.add_argument('--permission-file');a=p.parse_args()
 if a.benchmark_ready_run:
  if not a.permission_file or a.prepare or a.verify_run:p.error('benchmark requires separate explicit --permission-file')
  return 0 if benchmark(a) else 2
 if a.permission_file:p.error('permission is only used for separately released benchmark')
 if a.verify_run:return 0 if verify(a.verify_run) else 2
 prepare(a.rebind_from);return 0
if __name__=='__main__':
 try:raise SystemExit(main())
 except Exception as e:print('R34 ERROR: '+str(e),file=sys.stderr);raise SystemExit(1)
