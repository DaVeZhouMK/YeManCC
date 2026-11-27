import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

// Complete BUS admission dispatch between the reconcile preamble and frame sampling.
// Reusable by the full-pipe fixture without introducing a second copy of this policy.
export function extractGpdFirstOnBusAdmission(source){
 const begin='    const int hostLiveness = inputHostProcessLivenessTry();';
 const end='    preambleHostReconcileCost.finish();';
 const start=source.indexOf(begin),finish=source.indexOf(end);
 assert(start>=0&&finish>start&&source.lastIndexOf(begin)===start&&source.lastIndexOf(end)===finish,'BUS admission anchors changed');
 const fragment=source.slice(start,finish);
 assert(fragment.includes('inputHostMayQueueGpdNativeWriterWait()')&&fragment.includes('const bool queuedStart=inputHostQueueLifecycleWork(personaTransition'),'Original BUS queue dispatch not found');
 return {fragment,start,end:finish};
}

// Optional leaf of the existing controller.native-foreground-latency entry.
// Every control decision is extracted from the selected production source.
export function runGpdIdleWake({repo,args,output,extract,calibrationToolchainPreamble,runnerPath}) {
 const started=Date.now(),values=new Map();let prepareOnly=false;
 const allowed=new Set(['--case','--output','--runtime-source','--baseline-runtime','--variant']);
 for(let i=0;i<args.length;i++){
  const key=args[i];if(key==='--prepare-only'){assert(!prepareOnly);prepareOnly=true;continue;}
  assert(allowed.has(key),'Unknown idle-wake option: '+key);assert(!values.has(key),'Duplicate '+key);
  const value=args[++i];assert(value&&!value.startsWith('--'),'Missing '+key);values.set(key,value);
 }
 assert.equal(values.get('--case'),'gpd-idle-wake');assert(values.has('--output'),'Explicit output required');
 const variant=values.get('--variant')||'candidate';assert(['baseline','candidate','both'].includes(variant));
 if(variant!=='candidate')assert(values.has('--baseline-runtime'),'Explicit frozen baseline required');
 const deadline=Number(process.env.YMCC_GPD_IDLE_WAKE_DEADLINE_MS||started+180000);
 assert(Number.isSafeInteger(deadline)&&deadline>started,'Deadline exhausted');
 const remaining=cap=>{const n=Math.min(cap,deadline-Date.now());assert(n>0,'Deadline exhausted');return n;};
 assert(!fs.existsSync(output)||fs.readdirSync(output).length===0,'Output must be fresh');
 const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
 const leaf=fileURLToPath(import.meta.url),templatePath=path.join(path.dirname(leaf),'gpd-idle-wake.cpp.in');
 const inputs={main:path.join(repo,'native/main.cpp'),start:path.join(repo,'native/virtual_controller_start_runtime.h'),stop:path.join(repo,'native/virtual_controller_stop_runtime.h'),
  wake:path.join(repo,'native/virtual_wake_execution_runtime.h'),protection:path.join(repo,'native/virtual_start_protection.h'),
  queue:path.join(repo,'native/virtual_controller_queued_start_runtime.h'),contract:path.join(repo,'native/virtual_controller_runtime_contract.h'),
  persona:path.join(repo,'native/controller_persona.h'),liveness:path.join(repo,'native/msi_host_liveness_admission.h'),
  json:path.join(repo,'deps/json/json.hpp'),runner:runnerPath,leaf,template:templatePath,
  extractor:path.join(repo,'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs'),
  toolchain:path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')};
 const sources={candidate:path.resolve(values.get('--runtime-source')||path.join(repo,'native/virtual_controller_start_runtime.h')),
  ...(values.has('--baseline-runtime')?{baseline:path.resolve(values.get('--baseline-runtime'))}:{})};
 const names=variant==='both'?['baseline','candidate']:[variant];
 const snapshots=new Map([...new Set([...Object.values(inputs),...names.map(n=>sources[n])])].map(p=>[p,fs.readFileSync(p)]));
 const identities=[...snapshots].map(([file,b])=>({file,bytes:b.length,sha256:sha(b)}));
 fs.mkdirSync(output,{recursive:true});
 const save=(file,value)=>fs.writeFileSync(path.join(output,file),typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 const report={id:'controller.native-foreground-latency',case:'gpd-idle-wake',status:'PREPARING',startedAt:new Date(started).toISOString(),deadline,identities,prepareOnly,variants:[],hardwareAccess:false,wholeStartExecuted:false};
 save('INPUT.json',report);
 try {
  const main=snapshots.get(inputs.main).toString('utf8');
  const {fragment,start,end}=extractGpdFirstOnBusAdmission(main);
  const bindCall='const std::string requested = ymcc::inputHost::queuedPlayerStart::bind(original);';
  assert.equal(main.split(bindCall).length,2,'Production queue ticket binding call changed');
  for(const name of names){
   const spans=[],directory=path.join(output,name),include=path.join(directory,'include');fs.mkdirSync(include,{recursive:true});
   spans.push({source:inputs.main,symbol:'inputCaptureBusTick/admission-dispatch',start,end,sha256:sha(fragment)});
   const take=(file,symbol)=>extract(snapshots.get(file).toString('utf8'),'function',symbol,file,spans);
   // The optional BUS baseline changes its own admission functions, not these downstream Start excerpts.
   const actualStart=take(inputs.start,'inputHostStart'),{startUtf8Byte,startLine}=spans.pop();
   const extractStart=(symbol,pattern)=>{
    const matches=[...actualStart.matchAll(pattern)];assert.equal(matches.length,1,'Start excerpt: '+symbol);
    const {0:body,index}=matches[0],before=actualStart.slice(0,index),begin=startUtf8Byte+Buffer.byteLength(before);
    const line=startLine+before.split('\n').length-1;
    spans.push({symbol,kind:'excerpt',file:inputs.start,startLine:line,endLine:line+body.split('\n').length-1,
     startUtf8Byte:begin,endUtf8ByteExclusive:begin+Buffer.byteLength(body),sha256:sha(body)});
    return body;
   };
   const replacements={
    '@@CLEANUP@@':take(inputs.stop,'inputHostOwnedCleanupProvenLocked'),
    '@@ARM@@':take(inputs.wake,'inputHostArmWakePhysicalHold'),
    '@@RETIRE@@':take(sources[name],'inputHostRetireUnownedRogWakeHoldForStart'),
    '@@MAYQUEUE@@':take(sources[name],'inputHostMayQueueGpdNativeWriterWait'),
    '@@BUS_ADMISSION@@':fragment,'@@BUS_QUEUE_BIND@@':bindCall,
    '@@BUS_LIVENESS_GATE@@':take(inputs.main,'msiHostLifecycleAdmissionAllowed'),
    '@@START_OUTCOME@@':extractStart('inputHostStart/RequestOutcome',/^    struct RequestOutcome \{[\s\S]*?^    \} requestOutcome\{requestTicket\};/gm),
    '@@START_QUARANTINE@@':extractStart('inputHostStart/release-quarantine',/^    if \(g_inputHostReleaseQuarantined\) \{[\s\S]*?^    \}/gm),
    '@@START_PROTECTION_FINISH@@':take(inputs.main,'inputHostStartProtectionFinish')};
   assert(replacements['@@START_QUARANTINE@@'].includes('"release-quarantine"'));
   const template=snapshots.get(templatePath).toString('utf8');let code=template;const edits=[];
   for(const [marker,body] of Object.entries(replacements)){assert.equal(code.split(marker).length,2);const offset=code.indexOf(marker);edits.push({marker,offset,body});code=code.slice(0,offset)+body+code.slice(offset+marker.length);}
   let inverse=code;for(const e of edits.toReversed()){assert.equal(inverse.slice(e.offset,e.offset+e.body.length),e.body);inverse=inverse.slice(0,e.offset)+e.marker+inverse.slice(e.offset+e.body.length);}
   assert.equal(inverse,template);assert(!code.includes('@@'));
   fs.writeFileSync(path.join(directory,'Actual.cpp'),code,{flag:'wx'});
   for(const input of [inputs.json,inputs.protection,inputs.queue,inputs.contract,inputs.persona,inputs.liveness])fs.writeFileSync(path.join(include,path.basename(input)),snapshots.get(input),{flag:'wx'});
   const entry={name,spans,generatedSha256:sha(code),inverseExact:true,status:'PREPARED_NOT_RUN'};report.variants.push(entry);
   fs.writeFileSync(path.join(directory,'SOURCE-BINDING.json'),JSON.stringify({entry,edits},null,2),{flag:'wx'});
   if(prepareOnly)continue;
   const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
   const command=calibrationToolchainPreamble(vcvars)+
    'set VSCMD_SKIP_SENDTELEMETRY=1\r\nset VSCMD_DISABLE_VCTIP=1\r\n'+
    'for %%i in (cl.exe) do @echo YMCC_CL=%%~$PATH:i\r\nfor %%i in (link.exe) do @echo YMCC_LINK=%%~$PATH:i\r\n'+
    'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /wd4505 /O2 /MD /sourceDependencies dependencies.json /I"include" Actual.cpp /FoFixture.obj /FeFixture.exe\r\nexit /b %errorlevel%\r\n';
   const build=path.join(directory,'build.cmd');fs.writeFileSync(build,command,{flag:'wx'});
   const run=(exe,argv,cap,stem)=>{const began=Date.now();const r=spawnSync(exe,argv,{cwd:directory,encoding:'utf8',windowsHide:true,timeout:remaining(cap),maxBuffer:8*1024*1024});
    const receipt={exe,argv,status:r.status,signal:r.signal,error:r.error?String(r.error):null,elapsedMs:Date.now()-began};
    fs.writeFileSync(path.join(directory,stem+'.stdout.log'),r.stdout??'',{flag:'wx'});fs.writeFileSync(path.join(directory,stem+'.stderr.log'),r.stderr??'',{flag:'wx'});
    fs.writeFileSync(path.join(directory,stem+'.PROCESS.json'),JSON.stringify(receipt,null,2),{flag:'wx'});return receipt;};
   entry.build=run('cmd.exe',['/d','/c',build],60000,'build');assert.equal(entry.build.status,0,'Compile failed; original diagnostics retained');
   entry.run=run(path.join(directory,'Fixture.exe'),[path.join(directory,'RESULT.json')],10000,'run');
   entry.result=JSON.parse(fs.readFileSync(path.join(directory,'RESULT.json'),'utf8'));
   if(name==='baseline'){assert.equal(entry.run.status,1);assert.deepEqual(entry.result.failures,['gpd-idle-wake-first-on-reaches-original-bus-gate']);entry.status='EXPECTED_SEMANTIC_FAIL';}
   else{assert.equal(entry.run.status,0);assert(entry.result.passed);assert.deepEqual(entry.result.failures,[]);entry.status='PASS';}
   entry.binarySha256=sha(fs.readFileSync(path.join(directory,'Fixture.exe')));
  }
  report.status=prepareOnly?'PREPARED_NOT_COMPILED':'SCOPED_COMPLETE';
 }catch(error){report.status='FAIL';report.error=error.stack;throw error;}
 finally {
  report.elapsedMs=Date.now()-started;report.closing=identities.map(r=>({...r,currentSha256:sha(fs.readFileSync(r.file))}));
  report.inputDrift=report.closing.filter(r=>r.currentSha256!==r.sha256);if(report.inputDrift.length)report.status='INPUT_DRIFT';save('result.json',report);
 }
 assert.deepEqual(report.inputDrift,[],'Source changed during execution');
 console.log(JSON.stringify({status:report.status,variants:report.variants.map(r=>({name:r.name,status:r.status,checks:r.result?.checks,failures:r.result?.failures}))}));
}
