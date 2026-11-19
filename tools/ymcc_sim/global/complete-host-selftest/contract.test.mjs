import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {fileURLToPath,pathToFileURL} from 'node:url';
import {validateContract,parseCompleteResult,assessHostOutcome,assessAdapterExecution,digest} from './contract.mjs';
import {buildCompleteHostReport,finalizeReport} from './report.mjs';import {getCompleteHostSelfTestAdapter} from './adapter.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';
const ownRoot=path.dirname(fileURLToPath(import.meta.url)),root=path.dirname(ownRoot),fixtures=path.join(ownRoot,'fixtures');const actualRepo=process.env.YMCC_COMPLETE_HOST_TEST_REPO??path.resolve(ownRoot,'../../../..');const clone=x=>structuredClone(x);const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
function readArchivedStream(epoch,stream,verification){
 const file=path.join(fixtures,epoch,stream+'.raw');
 const bytes=fs.readFileSync(file);
 assert.equal(digest(bytes),verification[stream].sha256,'Archived Host output changed: '+file);
 return bytes;
}
const contract=read(fixtures+'/98-success-contract.json');const receipt=read(fixtures+'/98-success/verification.json');
const stdout=readArchivedStream('98-success','stdout',receipt),stderr=readArchivedStream('98-success','stderr',receipt);
const positiveResult=JSON.parse(JSON.stringify(parseCompleteResult(stdout).result));
function execution(){const payload=path.join(root,'synthetic-payload');return{mode:'complete-host-selftest',started:true,pid:receipt.pid,arguments:['--self-test'],processExitCode:0,naturalExit:true,timedOut:false,forcedTermination:false,startedUtc:receipt.startedUtc,finishedUtc:receipt.finishedUtc,payloadRoot:payload,executable:path.join(payload,'YeManFanHost.exe'),tuple:clone(contract.expectedTuple),payloadBefore:clone(contract.payloadFiles),payloadAfter:clone(contract.payloadFiles),noConsumers:[{phase:'initial',status:'PASS',consumerCount:0},{phase:'immediately-before-start',status:'PASS',consumerCount:0}],guard:{globalRunner:true,jobMember:true,helperSha256:contract.tools.jobHelperSha256},outputComplete:true,stdoutSha256:digest(stdout),stderrSha256:digest(stderr),nodeSha256:contract.tools.nodeSha256,realBackendRequested:false,hardwareWritesRequested:false,realTokenFileRequested:false,installedApplicationMutated:false,candidateInstalled:false};}
const assess=(e=execution(),out=stdout,err=stderr)=>assessAdapterExecution({contract,execution:e,stdout:out,stderr:err});
test('actual archived98 output has a genuine428-entry list; no process is launched',()=>{const r=parseCompleteResult(stdout);assert.deepEqual(r.issues,[]);assert.equal(r.checksPassed,428);assert.equal(r.checkCountObserved,true);});
test('actual149 FAN934 failure is FAIL and its count remains unobserved',()=>{const c=read(fixtures+'/149-failure-contract.json'),v=read(fixtures+'/149-failure/verification.json'),live=read(fixtures+'/149-failure/live-process.json');const r=assessHostOutcome({contract:c,execution:{...v,tuple:c.expectedTuple,arguments:live.arguments,forcedTermination:false},stdout:readArchivedStream('149-failure','stdout',v),stderr:readArchivedStream('149-failure','stderr',v)});assert.equal(r.status,'FAIL');assert.equal(r.checksPassed,null);assert.equal(r.checkCountObserved,false);assert(r.issues.includes('ORIGINAL_HOST_EXCEPTION_OBSERVED'));assert(r.issues.includes('HOST_NONZERO_OR_UNKNOWN_EXIT'));});
test('synthetic completed envelope is the only contract PASS and is labeled as data',()=>{const r=assess();assert.equal(r.status,'PASS');assert.equal(r.checksPassed,428);});
for(const [name,mutate] of [
 ['additional Host option',e=>e.arguments.push('--real-backend')],['Host option substitution',e=>e.arguments=['--msi-readonly-sample']],['missing PID',e=>e.pid=null],['nonzero exit',e=>e.processExitCode=1],['unknown exit',e=>e.processExitCode=null],['timeout',e=>e.timedOut=true],['forced termination',e=>e.forcedTermination=true],['missing natural exit',e=>e.naturalExit=null],['unknown finish time',e=>e.finishedUtc=null],['reverse time',e=>e.finishedUtc='2000-01-01T00:00:00Z'],['tuple mismatch',e=>e.tuple.hostDllSha256='0'.repeat(64)],['backend tuple mismatch',e=>e.tuple.backendSha256='0'.repeat(64)],['manifest tuple mismatch',e=>e.tuple.manifestSha256='0'.repeat(64)],['wrong apphost tuple',e=>e.tuple.apphostSha256='0'.repeat(64)],['real backend request',e=>e.realBackendRequested=true],['unknown hardware write request',e=>delete e.hardwareWritesRequested],['real token request',e=>e.realTokenFileRequested=true],['installed application mutation',e=>e.installedApplicationMutated=true],['installed candidate',e=>e.candidateInstalled=true],['no gated runner',e=>e.guard.globalRunner=false],['no job membership',e=>e.guard.jobMember=false],['unknown helper identity',e=>e.guard.helperSha256='0'.repeat(64)],['first NoConsumers missing',e=>e.noConsumers.shift()],['second NoConsumers nonzero',e=>e.noConsumers[1].consumerCount=1],['NoConsumers unknown',e=>e.noConsumers[1].status='UNKNOWN'],['changed payload file',e=>e.payloadAfter[0].sha256='0'.repeat(64)],['missing payload input',e=>e.payloadBefore.pop()],['extra payload output',e=>e.payloadAfter.push({name:'extra.dll',sha256:'0'.repeat(64)})],['raw output not drained',e=>e.outputComplete=false],['stdout hash mismatch',e=>e.stdoutSha256='0'.repeat(64)],['stderr hash mismatch',e=>e.stderrSha256='0'.repeat(64)],['unexpected node',e=>e.nodeSha256='0'.repeat(64)],['wrong executable',e=>e.executable=path.join(e.payloadRoot,'Other.exe')],['wrong execution mode',e=>e.mode='readonly-contract']]){
 test('refuse '+name,()=>{const e=execution();mutate(e);const r=assess(e);assert.equal(r.status,'FAIL');assert(r.issues.length);});
}
test('blocked preflight does not consume a historical success',()=>{const r=assess({started:false,blockReason:'EXISTING_RUNTIME_CONSUMER'});assert.equal(r.status,'BLOCKED');assert.equal(r.checksPassed,null);assert.equal(r.result,null);});
for(const [name,value] of [ ['empty stdout',''],['subset count',JSON.stringify({ok:true,checksPassed:400})],['API handshake',JSON.stringify({ok:true,state:'Open'})],['duplicate whole marker',stdout.toString()+'\n'+JSON.stringify(positiveResult)],['trailing fake success',stdout.toString()+'\nALL_PASS'],['empty checks',JSON.stringify({...positiveResult,checks:[]})],['non-string checks',JSON.stringify({...positiveResult,checks:[{}]})],['ok false',JSON.stringify({...positiveResult,ok:false})],['final write flag true',JSON.stringify({...positiveResult,finalState:{...positiveResult.finalState,hardwareWritesObserved:true}})],['unknown safe handshake',JSON.stringify({...positiveResult,handshake:{}})] ]){
 test('refuse output '+name,()=>{assert(parseCompleteResult(value).issues.length);});
}
test('integer checksPassed cannot override genuine checks list',()=>{const r=parseCompleteResult(JSON.stringify({...positiveResult,checksPassed:999999}));assert.equal(r.checksPassed,428);assert.equal(r.checkCountObserved,true);});
test('log byte limit is enforced',()=>{const r=parseCompleteResult(stdout,{maxLogBytes:1024});assert.deepEqual(r.issues,['STDOUT_LOG_SIZE_LIMIT']);});
test('escaped braces in checks do not split final JSON',()=>{const r=parseCompleteResult(JSON.stringify({...positiveResult,checks:['brace } and " quote {']}));assert.equal(r.checksPassed,1);assert.deepEqual(r.issues,[]);});
test('unhandled exception even with final marker refuses PASS',()=>{const err=Buffer.from('Unhandled exception. System.Exception');const e=execution();e.stderrSha256=digest(err);assert.equal(assess(e,stdout,err).status,'FAIL');});
for(const [name,mutate] of [['missing tuple',c=>delete c.expectedTuple],['arbitrary argv',c=>c.hostArguments=['--self-test','--real-backend']],['unbounded timeout',c=>c.hostTimeoutMs=1000000],['bad hash',c=>c.expectedTuple.hostDllSha256='bad'],['path traversal',c=>c.payloadFiles[0].name='../escape.dll'],['duplicate member',c=>c.payloadFiles.push(c.payloadFiles[0])],['missing pinned member',c=>c.payloadFiles=c.payloadFiles.filter(r=>r.name!=='YeManFanHost.dll')],['wrong pinned identity',c=>c.payloadFiles.find(r=>r.name==='YeManFanHost.dll').sha256='0'.repeat(64)],['unknown tool identity',c=>delete c.tools]]){
 test('reject invalid data contract '+name,()=>{const c=clone(contract);mutate(c);assert.throws(()=>validateContract(c));});
}
test('descriptor uses existing powershell guard path and explicit complete artifact closure',async()=>{const payloadRoot=path.join(root,'synthetic-payload');const a=getCompleteHostSelfTestAdapter({contractPath:fixtures+'/98-success-contract.json',nodeExecutable:process.execPath,payloadRoot,contract});assert.equal(a.id,'fan.complete-host-selftest');assert.equal(a.kind,'powershell');assert.equal(a.scope,'host-mock');assert.equal(a.requiresExclusiveHost,true);assert.equal(a.requiresPayloadRoot,true);assert.equal(a.artifactInputs.length,contract.payloadFiles.length+2);assert(contract.payloadFiles.every(r=>a.artifactInputs.includes(path.join(payloadRoot,r.name))));const {validateAdapters}=await import(pathToFileURL(path.join(actualRepo,'tools/ymcc_sim/global/runner.mjs')));assert.equal(validateAdapters([a],root).length,1);});
test('report preserves unobserved failed count and scope limits',()=>{const e=execution();e.processExitCode=1;const r=buildCompleteHostReport({contract,execution:e,stdout:Buffer.alloc(0),stderr:Buffer.from('FAN-934: victim route')});assert.equal(r.status,'FAIL');assert.equal(r.checksPassed,null);assert.equal(r.checkCountObserved,false);assert.equal(r.fullGoalComplete,false);assert.equal(r.candidateInstalled,false);});
const scratchPrefix='ymcc-complete-host-contract-';
const samePath=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
function directDirectory(dir){
 const stat=fs.lstatSync(dir,{bigint:true});
 assert(stat.isDirectory()&&!stat.isSymbolicLink()&&samePath(fs.realpathSync.native(dir),dir),'Not a direct directory: '+dir);
 return stat;
}
function assertSameDirectory(dir,owned){
 const current=directDirectory(dir);
 assert(current.dev===owned.dev&&current.ino===owned.ino,'Fixture directory identity changed: '+dir);
}
test('prepared report works with actual public structured reader and rejects changed raw output',async t=>{
 const override=process.env.YMCC_COMPLETE_HOST_TEST_ROOT;
 const parent=path.resolve(override??simulationCorePath('Workspace'));
 if(override!==undefined){
  let ancestor=parent;
  while(!fs.lstatSync(ancestor,{throwIfNoEntry:false}))ancestor=path.dirname(ancestor);
  directDirectory(ancestor);
  fs.mkdirSync(parent,{recursive:true});
 }
 const parentIdentity=directDirectory(parent);
 const dir=fs.mkdtempSync(path.join(parent,scratchPrefix));
 let passed=false;
 try{
  assert(samePath(path.dirname(path.resolve(dir)),parent)&&path.basename(dir).startsWith(scratchPrefix),'Fixture directory is outside its owner: '+dir);
  const owned=directDirectory(dir);
  const caseDir=path.join(dir,'case'),work=path.join(caseDir,'complete-host-selftest'),repo=path.join(dir,'repo');
  fs.mkdirSync(work,{recursive:true});
  const codeDir=path.join(repo,'tools/ymcc_sim/global/complete-host-selftest');
  fs.mkdirSync(codeDir,{recursive:true});
  for(const name of ['adapter.mjs','contract.mjs','report.mjs','validate-contract.mjs','Run-CompleteHost.ps1'])fs.copyFileSync(path.join(ownRoot,name),path.join(codeDir,name));
  fs.writeFileSync(work+'/stdout.log',stdout);
  fs.writeFileSync(work+'/stderr.log',stderr);
  fs.writeFileSync(work+'/process.json',JSON.stringify(execution()));
  fs.writeFileSync(work+'/contract.json',JSON.stringify(contract));
  const r=finalizeReport({evidenceDir:caseDir,contractPath:work+'/contract.json'});
  assert.equal(r.status,'PASS');
  const {checkStructuredEvidence}=await import(pathToFileURL(path.join(actualRepo,'tools/ymcc_sim/global/runner.mjs')));
  const first=checkStructuredEvidence(caseDir,{resultFiles:['complete-host-selftest.json']},repo);
  assert.equal(first.failed,false);
  assert.equal(first.paths[0].reportedSourceCount,9);
  fs.appendFileSync(work+'/stdout.log','changed');
  const second=checkStructuredEvidence(caseDir,{resultFiles:['complete-host-selftest.json']},repo);
  assert.equal(second.failed,true);
  assert(second.paths[0].sourceMismatches.includes(path.join(work,'stdout.log')));
  if(!t.signal.aborted){
   assertSameDirectory(parent,parentIdentity);
   assertSameDirectory(dir,owned);
   fs.rmSync(dir,{recursive:true});
   passed=true;
  }
 }finally{
  if(!passed)console.error('Retained report reader fixture for diagnosis: '+dir);
 }
});
