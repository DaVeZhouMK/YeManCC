"""Exact production original-instance success-chain consumer, owned fixture only.
No Steam/game process access, debugger attach, sleep or remote publication.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,subprocess,sys,time
sys.dont_write_bytecode=True

def sha(b):return hashlib.sha256(b).hexdigest()
def mask(text):
 return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27',lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text,flags=re.S)
def extract(text,name):
 clean=mask(text)
 pattern=r'^(?:[\w:<>]+\s+)+?'+re.escape(name)+r'\s*\([^;{}]*\)\s*(?:const\s*)?\{'
 hits=list(re.finditer(pattern,clean,re.M))
 if len(hits)!=1:raise RuntimeError(f'ambiguous/missing exact function {name}: {len(hits)}')
 start=hits[0].start();op=clean.index('{',start);depth=1;i=op+1
 while depth and i<len(clean):depth+=(clean[i]=='{')-(clean[i]=='}');i+=1
 if depth:raise RuntimeError('unbalanced '+name)
 return text[start:i],start,i

def compiler_dependencies(runout,generated,copied,include_roots):
 raw=json.loads((runout/'dependencies.json').read_text(encoding='utf-8-sig'))['Data']
 if Path(raw['Source']).resolve()!=generated.resolve():raise RuntimeError('compiler-source-mismatch')
 closure=[];unknown=[]
 for file in sorted({Path(name).resolve() for name in raw['Includes']},key=lambda p:str(p).lower()):
  origin=copied.get(file)
  kind='copied-input' if origin else 'toolchain-header'
  if not origin and not any(file.is_relative_to(root) for root in include_roots):unknown.append(str(file))
  data=file.read_bytes();row={'path':str(file),'bytes':len(data),'sha256':sha(data),'kind':kind}
  if origin:row['source']=str(origin)
  closure.append(row)
 return {'discovered':closure,'unmapped':unknown,'valid':not unknown}

def main():
 task_started=time.perf_counter()
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--negative-controls',action='store_true');parser.add_argument('--subset',choices=['all','config-storm','runtime-config'],default='all');parser.add_argument('--runtime-source',type=Path,help='Explicit retained runtime baseline for before/after evidence');args=parser.parse_args()
 repo=Path(__file__).resolve().parent.parent;out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
 source=repo/'native/steam_original_recovery_engine.cpp';template=repo/'tools/steam_original_recovery_success_chain_fixture.cpp.in'
 runtime=args.runtime_source.resolve() if args.runtime_source else repo/'native/steam_original_recovery_runtime.h'
 dependencies=[repo/'native/steam_original_recovery_storage.h',repo/'native/steam_original_recovery_config.h',repo/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json']
 inputs={str(p):p.read_bytes() for p in [runtime,*dependencies,source,template,Path(__file__).resolve(),repo/'deps/json/json.hpp',repo/'native/steam_original_recovery_wake_watch.h',repo/'native/steam_original_recovery_file_hash.h']}
 text=inputs[str(source)].decode('utf-8-sig').replace('\r\n','\n');parts=[]
 def take(name):
  body,start,end=extract(text,name);parts.append({'name':name,'source':str(source),'startLine':text.count('\n',0,start)+1,'endLine':text.count('\n',0,end)+1,'sha256':sha(body.encode('utf-8')),'bytes':len(body.encode('utf-8'))});(out/(name+'.inc')).write_text(body,encoding='utf-8');return body
 base=['need','utf16','utf8','decimal','dword','digest','textHash','fileHash','freshUuid','absolute','statePresent','readJson','durableCreate','atomicJson']
 # Minimal RAII for durableCreate's own file handle; no remote API access.
 own_handle='struct H {HANDLE h;explicit H(HANDLE v):h(v){}~H(){if(good())CloseHandle(h);}bool good()const{return h&&h!=INVALID_HANDLE_VALUE;}};'
 original=take('handle')
 cpp=inputs[str(template)].decode('utf-8-sig').replace('\r\n','\n')
 cpp=cpp.replace('@@BASE@@',take('need')+'\n'+own_handle+'\n'+'\n'.join(take(n) for n in base if n!='need'))
 cpp=cpp.replace('@@REFERENCES@@','\n'.join(take(n) for n in ['pin','pins','life','referenceScope','scopeKey','auditedPrepublicationSource','auditedLegacyTransfer','consume','referenceNonceValid','matchReference','wakeDeadlineTick']))
 cpp=cpp.replace('@@SEED@@',take('referenceFromSeed'))
 runtime_text=inputs[str(runtime)].decode('utf-8-sig').replace('\r\n','\n')
 runtime_parts=[]
 if 'struct ConfigRead {' in runtime_text:
  start=runtime_text.index('struct ConfigRead {');clean=mask(runtime_text);end=clean.index('{',start)+1;depth=1
  while depth and end<len(clean):depth+=(clean[end]=='{')-(clean[end]=='}');end+=1
  if depth or clean[end:].lstrip()[:1]!=';':raise RuntimeError('unbalanced ConfigRead')
  end=clean.index(';',end)+1;runtime_parts.append(runtime_text[start:end]);parts.append({'name':'ConfigRead','source':str(runtime),'startLine':runtime_text.count('\n',0,start)+1,'endLine':runtime_text.count('\n',0,end)+1,'bytes':len(runtime_text[start:end].encode('utf-8')),'sha256':sha(runtime_text[start:end].encode('utf-8'))})
 for name in ['readJson','run']:
  body,start,end=extract(runtime_text,name);runtime_parts.append(body);parts.append({'name':'runtime-'+name,'source':str(runtime),'startLine':runtime_text.count('\n',0,start)+1,'endLine':runtime_text.count('\n',0,end)+1,'sha256':sha(body.encode('utf-8')),'bytes':len(body.encode('utf-8'))});(out/('runtime-'+name+'.inc')).write_text(body,encoding='utf-8')
 cpp=cpp.replace('@@RUNTIME@@','\n'.join(runtime_parts))
 cpp=cpp.replace('@@HANDLE@@',original)
 if '@@' in cpp:raise RuntimeError('unreplaced fixture token')
 revision=re.search(r'kEngineRevision\s*=\s*"([^"]+)"',text)[1];module=re.search(r'kModuleQualification\s*=\s*"([^"]+)"',text)[1]
 route_begin=text.index('enum class RecoveryRoute ');route_end=text.index('std::atomic<bool> gActive',route_begin)
 route_definitions=text[route_begin:route_end];parts.append({'name':'route-definitions','startLine':text.count('\n',0,route_begin)+1,'sha256':sha(route_definitions.encode('utf-8'))})
 cpp=cpp.replace('std::atomic<bool> gActive{false};',f'constexpr const char* kEngineRevision="{revision}";constexpr const char* kModuleQualification="{module}";\n{route_definitions}std::atomic<bool> gActive{{false}};')
 copied={}
 for src,dest in [(repo/'deps/json/json.hpp','json.hpp'),(repo/'native/steam_original_recovery_storage.h','steam_original_recovery_storage.h'),(repo/'native/steam_original_recovery_config.h','steam_original_recovery_config.h'),(repo/'deps/json/json.hpp','fixture_json.hpp'),(repo/'native/steam_original_recovery_wake_watch.h','steam_original_recovery_wake_watch.h'),(repo/'native/steam_original_recovery_file_hash.h','steam_original_recovery_file_hash.h')]:
  target=(out/dest).resolve();target.write_bytes(inputs[str(src)]);copied[target]=src
 tools=json.loads((repo/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json').read_text(encoding='utf-8-sig'))['native']
 env={k.upper():v for k,v in os.environ.items()};env['PATH']=';'.join([str(Path(tools['compiler']).parent),'C:/Windows/System32','C:/Windows']);env['INCLUDE']=';'.join(tools['include']);env['LIB']=';'.join(tools['lib']);
 for k in ['CL','_CL_','LINK','_LINK_']:env[k]=''
 report={'schema':'ymcc.steam-success-chain-retention-selftest.v1','startedAtUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'repo':str(repo),'revision':revision,'inputs':[{'path':p,'bytes':len(b),'sha256':sha(b)} for p,b in inputs.items()],'extractions':parts,'productionMethodsReimplemented':False,'ports':['openScope native identity and own fixture handles','snapshot native bytes/compatibility -> controlled values','loadCompatibleReference file selection -> current-key-only port; actual matchReference executes','QueryActor/sourceGetterContext/runTrial -> controlled original identity calls','WakeProbeReader/Win32 wait clock -> virtual time and controlled native flags','WorkerCost clocks -> neutral fixture stamps'],'subset':args.subset,'runtimeSource':str(runtime),'realConsumers':['complete runtime run/readJson and production storage/configuration','complete handle','pin/referenceScope/scopeKey/matchReference','consume','durableCreate/atomicJson/readJson','WakeWatch policy and physical wake mutation deadline'],'notValidated':['Real Steam load/debugger/native callback execution','loader/module compatibility; separate existing tests','UI/input/Continue/real wake','legacy reference migration; separate engine119 tests','reference seed import uses a throwing port; prior separate engine tests only','runtime event branch and worker startup are not executed; stop=true and runOne throwing port'],'sceneProcessOpens':0,'sleepActions':0,'runs':[]}
 variants=[('current',cpp,False)]
 if args.negative_controls:
  controls=[('missing-consume','consume(event,scope,*data);summary["consumed"]=true;','summary["consumed"]=false;'),('publication-silently-skipped','result=runTrial(request,query,beforeMutation);','throw std::runtime_error("negative-publication-skipped");'),('truncated-gameid','{"gameId",std::to_string(scope.gameId)},{"appid",scope.appid},{"nonce",nonce}','{"gameId",std::to_string(scope.appid)},{"appid",scope.appid},{"nonce",nonce}'),('reference-pins-bypassed','need(ref.at("pins")==pins(sample,scope.roles[0].born,account,ref.at("bindingNonce").get<std::string>()),"saved-healthy-pins-changed");','(void)sample; /* negative control removes exact pin check */')]
  for name,old,new in controls:
   if cpp.count(old)!=1:raise RuntimeError(f'nonunique mutation {name}: {cpp.count(old)}')
   variants.append((name,cpp.replace(old,new),True))
 for variant_index,(name,variant,negative) in enumerate(variants):
  runout=out/('v'+str(variant_index));runout.mkdir();p=runout/'consumer.cpp';p.write_text(variant,encoding='utf-8');exe=runout/'consumer.exe'
  command=[tools['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O1','/W4','/WX','/bigobj','/sourceDependencies',str(runout/'dependencies.json'),'/I'+str(out),str(p),'/Fo'+str(runout/'consumer.obj'),'/Fe'+str(exe),'/link','bcrypt.lib','/INCREMENTAL:NO','/DYNAMICBASE','/NXCOMPAT']
  compile_started=time.perf_counter();compile_result=subprocess.run(command,cwd=out,env=env,capture_output=True,timeout=60);(runout/'compile.log').write_bytes(compile_result.stdout+compile_result.stderr)
  row={'compileCommand':command,'linkerSha256':sha(Path(tools['linker']).read_bytes()),'pythonSha256':sha(Path(sys.executable).read_bytes()),'name':name,'negative':negative,'outputDirectory':str(runout),'compileExit':compile_result.returncode,'compileMs':(time.perf_counter()-compile_started)*1000,'compilerSha256':sha(Path(tools['compiler']).read_bytes()),'generatedSourceSha256':sha(p.read_bytes())}
  if compile_result.returncode==0:
   try:row['dependencies']=compiler_dependencies(runout,p,copied,[Path(root).resolve() for root in tools['include']])
   except Exception as error:row['dependencies']={'valid':False,'error':str(error),'discovered':[],'unmapped':[]}
   if not row['dependencies']['valid']:
    row['detected']=False;report['runs'].append(row);continue
   selected=('negative-pins' if name=='reference-pins-bypassed' else 'negative-startup') if negative else args.subset
   execution=[str(exe),str(runout/'state'),selected];row['executionCommand']=execution
   execution_started=time.perf_counter();completed=subprocess.run(execution,cwd=out,env=env,capture_output=True,timeout=45);(runout/'run.log').write_bytes(completed.stdout+completed.stderr);row['runExit']=completed.returncode;row['executionMs']=(time.perf_counter()-execution_started)*1000;row['binarySha256']=sha(exe.read_bytes())
   try:r=json.loads(completed.stdout.decode('utf-8-sig'));row.update(tests=r['tests'],passed=r['passed'],checks=r['checks'],failedCases=[x for x in r['cases'] if not x['passed']]);(runout/'result.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
   except Exception as exc:row['parseError']=str(exc)
   if negative:
    expected={'missing-consume':('original-startup-lost-recovers','driver-started-before-durable-consume'),'publication-silently-skipped':('original-startup-lost-recovers','negative-publication-skipped'),'truncated-gameid':('original-startup-lost-recovers','publisher-identity-replaced'),'reference-pins-bypassed':('record-replaced-after-publish-refuses','record-migration-accepted')}[name]
    matching=[case for case in row.get('failedCases',[]) if case['name']==expected[0] and (case.get('lastReason')==expected[1] or case.get('reason')==expected[1])]
    row['expectedFailure']={'case':expected[0],'reason':expected[1]};row['detected']=completed.returncode==1 and len(matching)==1
   else:row['detected']=completed.returncode==0
   row['dependenciesUnchanged']=all(sha(Path(item['path']).read_bytes())==item['sha256'] for item in row['dependencies']['discovered'])
   row['detected']=row['detected'] and row['dependencies']['valid'] and row['dependenciesUnchanged']
  else:row['detected']=False
  report['runs'].append(row)
 report['inputSourcesUnchanged']=all(Path(p).read_bytes()==b for p,b in inputs.items());report['success']=all(r['detected'] for r in report['runs']) and report['inputSourcesUnchanged'];report['fieldAcceptance']=False;report['totalMs']=(time.perf_counter()-task_started)*1000
 (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({'success':report['success'],'runs':[{k:v for k,v in r.items() if k not in ['failedCases','dependencies']} for r in report['runs']],'report':str(out/'report.json')},ensure_ascii=False,indent=2));return 0 if report['success'] else 1
if __name__=='__main__':raise SystemExit(main())
