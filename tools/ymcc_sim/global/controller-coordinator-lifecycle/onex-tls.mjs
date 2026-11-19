// Optional OneX startup boundary of controller.coordinator-lifecycle.
// This leaf accepts an explicit pinned tool/environment contract, never a saved
// before source, and never dispatches the full coordinator or a default suite.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';

const SELF=fileURLToPath(import.meta.url),HERE=path.dirname(SELF);
const ID='controller.coordinator-lifecycle',CASE='onex-diagnostic-tls-startup';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{p=path.resolve(p);const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:sha(b)};};
const save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
const key=p=>path.resolve(p).toLowerCase();
const check=r=>{const a=pin(r.path);assert.equal(a.bytes,r.bytes,'SIZE_DRIFT:'+r.path);assert.equal(a.sha256,r.sha256,'HASH_DRIFT:'+r.path);};
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const replace=(s,a,b)=>{assert.equal(s.split(a).length,2,'EXACT_SOURCE_ANCHOR:'+a);return s.replace(a,()=>b);};

export async function executeOneXTls({repo,root,toolPlan,deadlineEpochMs}){
  const began=Date.now(),capMs=120000;
  assert(Number.isSafeInteger(deadlineEpochMs)&&deadlineEpochMs>began,'INHERITED_DEADLINE_REQUIRED');
  const deadline=Math.min(deadlineEpochMs,began+capMs),parentBudgetMs=deadline-began;
  for(const p of [repo,root,toolPlan])assert(path.isAbsolute(p),'ABSOLUTE_PATH_REQUIRED');
  repo=path.resolve(repo);root=path.resolve(root);
  const evidence=path.resolve(repo,'../../../Testing/YMCC/Evidence');
  assert(key(root).startsWith(key(evidence)+path.sep),'CORE_EVIDENCE_REQUIRED');
  assert(!fs.existsSync(root),'FRESH_OUTPUT_REQUIRED');fs.mkdirSync(root,{recursive:true});
  const remaining=(reserve=3000)=>{assert(deadline-Date.now()>reserve,'SHARED_DEADLINE_EXHAUSTED');return deadline-Date.now()-reserve;};
  let proof=null,error=null,worker=null;
  try{
    remaining();const tooling=read(toolPlan);
    assert.equal(tooling.schemaVersion,1);
    assert(key(tooling.tools.compiler).includes('msvc\\14.44.35207\\bin\\hostx64\\x64\\cl.exe'),'FIXED_COMPILER_REQUIRED');
    assert(tooling.environment.INCLUDE.includes('10.0.26100.0'),'FIXED_SDK_REQUIRED');
    assert.equal(key(tooling.tools.node),key(process.execPath),'PINNED_NODE_REQUIRED');
    // Isolate all tool-created temporary/profile state to this owned output.
    const temp=path.join(root,'temp'),profile=path.join(root,'profile');
    for(const p of [temp,profile,path.join(profile,'AppData/Local'),path.join(profile,'AppData/Roaming')])fs.mkdirSync(p,{recursive:true});
    tooling.environment={...tooling.environment,TEMP:temp,TMP:temp,USERPROFILE:profile,LOCALAPPDATA:path.join(profile,'AppData/Local'),APPDATA:path.join(profile,'AppData/Roaming')};
    for(const row of tooling.toolArtifacts){remaining();check(row);}
    const rawModule=path.join(repo,'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs');
    const extractor=path.join(repo,'tools/fixtures/hc_onex_x2_application/extract.mjs');
    const {func}=await import(pathToFileURL(extractor));
    const {startOwnedRawProducer,verifyRawProducerBudget}=await import(pathToFileURL(rawModule));
    const sourcePath=path.join(repo,'native/virtual_controller_onex_start_runtime.h');
    const sourceBytes=fs.readFileSync(sourcePath),source=sourceBytes.toString('utf8');
    assert(Buffer.from(source).equals(sourceBytes),'SOURCE_UTF8_REQUIRED');
    const port=func(source,'inputHostOneXPortKind');
    const caller=func(source,'inputHostOneXPostHideBeforeTarget');
    const init='    json portEvidence=json::object();size_t locationCount=0;';
    const query='    try {(void)inputHostOneXPortKind(e.root,portEvidence);}catch(...) {portEvidence=json{{"reason","diagnostic-query-exception"}};}';
    const pair='{"usbTopology",portEvidence}';
    for(const fragment of [init,query,pair])assert.equal(caller.text.split(fragment).length,2,'CALLER_FRAGMENT_DRIFT');
    const oldPort='static thread_local json t_inputHostOneXPortEvidence=json::object();\r\n'+
      'static int inputHostOneXPortKind(const std::string& root) {return externalPriorityUsbPortKindWithEvidence(root,&t_inputHostOneXPortEvidence,0);}';
    // The semantic mutation below is the exact inverse of the five repaired
    // product edits. Its full original-byte identity is recorded, never required
    // as a future input. The original eager declaration/Port slice is SHA-bound.
    assert.equal(sha(oldPort+'\r\n'),'93b42f9ee0d991f921f175cd955c752ec90eb2724ecd1f4550d4e1f58b21119b');
    let before=replace(source,port.text,oldPort);
    before=replace(before,init,'    t_inputHostOneXPortEvidence=json::object();size_t locationCount=0;');
    before=replace(before,pair,'{"usbTopology",t_inputHostOneXPortEvidence}');
    before=replace(before,query,'    try {(void)inputHostOneXPortKind(e.root);}catch(...) {t_inputHostOneXPortEvidence=json{{"reason","diagnostic-query-exception"}};}');
    fs.writeFileSync(path.join(root,'runtime.candidate.h'),sourceBytes,{flag:'wx'});
    fs.writeFileSync(path.join(root,'runtime.before.h'),before,{flag:'wx'});
    fs.writeFileSync(path.join(root,'Port.candidate.inc'),port.text,{flag:'wx'});
    fs.writeFileSync(path.join(root,'Port.before.inc'),oldPort,{flag:'wx'});
    let fixture=fs.readFileSync(path.join(HERE,'onex-tls.cpp.in'),'utf8');
    for(const [marker,body] of [['// @@ACTUAL_INITIALIZATION@@',init],['// @@ACTUAL_QUERY_CATCH@@',query],['// @@ACTUAL_RECEIPT_PAIR@@',pair]])fixture=replace(fixture,marker,body);
    assert(!fixture.includes('@@'),'UNRESOLVED_TEMPLATE');fs.writeFileSync(path.join(root,'Fixture.cpp'),fixture,{flag:'wx'});
    const binding={canonicalTestId:ID,selectedCase:CASE,source:pin(sourcePath),port:{start:port.start,end:port.end,sha256:sha(port.text)},caller:{start:caller.start,end:caller.end,sha256:sha(caller.text)},
      fragments:[init,query,pair],oldPortSha256:sha(oldPort),derivedBefore:pin(path.join(root,'runtime.before.h')),
      reconstructsHistoricalBeforeEveryByte:sha(before)==='62a085e012db5bd42b36b4269eca31a15b011062da28fb86e7729aa183e0568d',
      oldSourceInputRequired:false,preCrtMethod:'Custom entry -> CreateThread unrelated Win32 body -> normal static mainCRTStartup. No pre-CRT JSON call.',wholeOneXStart:false,hardwareIo:false};
    save(path.join(root,'source-binding.json'),binding);
    const sourcePaths=[SELF,path.join(HERE,'run.mjs'),path.join(HERE,'onex-tls.cpp.in'),path.join(HERE,'onex-tls.py'),extractor,rawModule,tooling.host,toolPlan,sourcePath,path.join(repo,'deps/json/json.hpp'),
      ...['runtime.candidate.h','runtime.before.h','Port.candidate.inc','Port.before.inc','Fixture.cpp','source-binding.json'].map(n=>path.join(root,n))];
    const sources=sourcePaths.map(pin),plan={schemaVersion:1,canonicalTestId:ID,selectedCase:CASE,began,deadline,capMs,parentBudgetMs,cleanupReserveMs:3000,
      tools:tooling.tools,environment:tooling.environment,toolArtifacts:tooling.toolArtifacts,sources,jsonInclude:tooling.jsonInclude,
      configurations:[{name:'Before',exitCode:101},{name:'Release',exitCode:0,checks:22},{name:'Checked',exitCode:0,checks:22}],
      defaultCoordinatorDispatched:false,oldExecutionSessionReopened:false,fullOneXStart:false,hardwareIo:false};
    save(path.join(root,'plan.json'),plan);fs.mkdirSync(path.join(root,'Execution'));
    const ownerBegin=Date.now(),command='[Diagnostics.Process]::GetProcessById('+process.pid+').StartTime.ToUniversalTime().Ticks.ToString()';
    const birth=spawnSync(tooling.tools.powershell,['-NoLogo','-NoProfile','-NonInteractive','-Command',command],{env:tooling.environment,encoding:'utf8',windowsHide:true,timeout:Math.min(10000,remaining())});
    save(path.join(root,'owner-query.json'),{program:pin(tooling.tools.powershell),command,pid:birth.pid,exitCode:birth.status,signal:birth.signal,error:birth.error?.message??null,stdout:birth.stdout,stderr:birth.stderr,elapsedMs:Date.now()-ownerBegin});
    assert.equal(birth.status,0);assert(!birth.error&&!birth.signal&&/^\d{17,20}\s*$/.test(birth.stdout),'OWNER_QUERY_FAILED');
    const allocated=Date.now();remaining();
    const job=startOwnedRawProducer({id:ID+':'+CASE,program:tooling.tools.python,args:['-I','-S','-B',path.join(HERE,'onex-tls.py'),path.join(root,'plan.json'),root],
      cwd:root,env:tooling.environment,host:tooling.host,powershell:tooling.tools.powershell,ownerStartTicks:birth.stdout.trim(),directory:path.join(root,'owned'),
      stdoutPath:path.join(root,'worker.stdout.log'),stderrPath:path.join(root,'worker.stderr.log'),deadlineEpochMs:deadline,allocatedAtEpochMs:allocated,parentStartedAtEpochMs:began,
      parentBudgetMs,cleanupReserveMs:3000,sourceInputs:[...sources,pin(path.join(root,'plan.json'))],toolArtifacts:tooling.toolArtifacts,originalBudgetMs:capMs,
      parentRemainingBeforeMs:deadline-allocated,toolCleanupPolicy:tooling.cleanup});
    proof=await job.wait();save(path.join(root,'process.json'),proof);verifyRawProducerBudget(proof,capMs,parentBudgetMs);
    assert.equal(proof.exitCode,0);assert.equal(proof.error,null);assert.equal(proof.rawExecution.job.activeProcessesFinal,0);assert.equal(proof.rawExecution.job.zeroObservedBeforeDispose,true);
    worker=read(path.join(root,'worker-result.json'));assert.equal(worker.status,'PASS_SELECTED_SCOPE');
    assert.deepEqual(worker.reports.map(r=>[r.name,r.exitCode]),[['Before',101],['Release',0],['Checked',0]]);
    for(const row of [...sources,...tooling.toolArtifacts]){remaining(0);check(row);}remaining(100);
  }catch(cause){error=cause.stack??String(cause);}
  const result={schemaVersion:1,status:error?'FAIL':'PASS_SELECTED_SCOPE',passed:!error,error,canonicalTestId:ID,selectedCase:CASE,elapsedMs:Date.now()-began,
    deadlineEpochMs:deadline,capMs,parentBudgetMs,proof:proof?pin(path.join(root,'process.json')):null,worker:fs.existsSync(path.join(root,'worker-result.json'))?pin(path.join(root,'worker-result.json')):null,
    defaultCoordinatorDispatched:false,originalFamily47CasesExecutedThisRun:false,fullOneXStart:false,hardwareIo:false,wholeProductAcceptance:false,installedAcceptance:false};
  save(path.join(root,'result.json'),result);assert(!error,'ONEX_TLS_SELECTED_SCOPE_FAILED');return result;
}

if(process.argv[1]&&path.resolve(process.argv[1])===SELF){
  try{
    const args=process.argv.slice(2),v={};assert.equal(args.length,8,'EXACT_FOUR_OPTIONS_REQUIRED');
    for(let i=0;i<args.length;i+=2){assert(['--repo','--out','--tool-plan','--deadline-epoch-ms'].includes(args[i])&&!Object.hasOwn(v,args[i]),'UNKNOWN_OR_DUPLICATE_OPTION');v[args[i]]=args[i+1];}
    const r=await executeOneXTls({repo:v['--repo'],root:v['--out'],toolPlan:v['--tool-plan'],deadlineEpochMs:Number(v['--deadline-epoch-ms'])});console.log(JSON.stringify(r));
  }catch(e){console.error(e.stack??e);process.exitCode=1;}
}
