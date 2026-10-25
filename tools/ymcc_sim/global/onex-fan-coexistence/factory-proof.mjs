import fs from 'node:fs';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import path from 'node:path';
import crypto from 'node:crypto';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?tree(path.join(dir,e.name)):[path.join(dir,e.name)]);
export const factoryCaseIds=[
 ...['False','True'].map(mode=>'factory-healthy-actual-orchestration:'+mode),
 ...['False','True'].map(mode=>'factory-sensor-discovery-not-called:'+mode),
 ...['device.initialize','device.identity'].flatMap(stage=>['factory-partial-cannot-publish-ready:','factory-retry-continues-same-device:'].map(prefix=>prefix+stage)),
 ...['assembly.configure','motherboard.collect','device.construct'].map(stage=>'factory-bootstrap-retry-does-not-replay-completed-stages:'+stage),
 'factory-write-admission-completes-pending-initialization','factory-readonly-sampling-never-uses-partial-core',
 'factory-authorization-refusal-precedes-new-work','factory-disposed-cannot-republish-or-reload',
 'factory-dispose-during-stage-cannot-publish','factory-reentrant-handshake-cannot-publish-partial-core',
 ...['device','assembly'].map(kind=>'factory-owner-change-during-stage-is-refused:'+kind),
 ...['device','assembly'].map(kind=>'factory-completed-owner-change-invalidates-ready:'+kind),
 'factory-readonly-sample-error-keeps-completed-factory','factory-disposal-preserves-another-resolver-owner',
 'factory-resolver-one-registration-and-owned-disposal'];
const readinessFailures={
 ...Object.fromEntries(['device.initialize','device.identity'].flatMap(stage=>[
  ['factory-partial-cannot-publish-ready:'+stage,'Partial factory must not publish IsLoaded or write readiness'],
  ['factory-retry-continues-same-device:'+stage,'Failed factory must resume to complete real readiness and identity']])),
 'factory-write-admission-completes-pending-initialization':'HC 核心初始化尚未完成',
 'factory-readonly-sampling-never-uses-partial-core':'Read-only evidence must not sample or report success from a partial factory',
 'factory-disposed-cannot-republish-or-reload':'Disposed factory cannot retain ready or write authority',
 'factory-dispose-during-stage-cannot-publish':'Disposed in-flight factory cannot publish a late ready result',
 'factory-reentrant-handshake-cannot-publish-partial-core':'Reentrant handshake cannot observe an unfinished initialization as ready',
 ...Object.fromEntries(['device','assembly'].flatMap(kind=>[
  ['factory-owner-change-during-stage-is-refused:'+kind,'Changed factory identity cannot be adopted by the old stage'],
  ['factory-completed-owner-change-invalidates-ready:'+kind,'Replaced factory object cannot inherit a completed initialization']]))};
const continuationFailures={
 ...Object.fromEntries(['device.initialize','device.identity'].map(stage=>['factory-retry-continues-same-device:'+stage,'Retry cannot replay completed bootstrap stages'])),
 ...Object.fromEntries(['motherboard.collect','device.construct'].map(stage=>['factory-bootstrap-retry-does-not-replay-completed-stages:'+stage,'Bootstrap retry must preserve completed configuration and motherboard stages'])),
 'factory-write-admission-completes-pending-initialization':'Actual write admission must finish pending initialization before accepting the same device',
 'factory-readonly-sampling-never-uses-partial-core':'Read-only evidence recovers only after the same actual factory completes'};
const ordered=value=>JSON.stringify([...value].sort());
export function runFactoryProof({out,harness,candidate,payload,helper,hc,run}){
 const root=path.join(out,'factory-initialization');fs.mkdirSync(root);
 const rows=[];
 for(const mode of ['positive','readiness','continuation']){
  const dir=path.join(root,mode);fs.mkdirSync(dir);const leafPayload=path.join(dir,'leaf-payload');stageFaultPayload(payload,leafPayload);
  const relative=log=>path.relative(out,path.join(dir,log));
  let input=candidate,mutation=null;
  if(mode!=='positive'){
   input=path.join(dir,'faulted-host.dll');const proof=path.join(dir,'fault-proof.json');
   run('dotnet',[harness,'fault-factory-'+mode,candidate,input,proof],relative('fault-generation.log'),30000);
   mutation=read(proof);const method=mode==='readiness'?'::get_IsLoaded(':'::LoadAssemblyAndFactory(';
   if(mutation.status!=='PASS'||mutation.sourceSha256!==hash(candidate)||mutation.faultSha256!==hash(input)||mutation.changedMethods.length!==1||!mutation.changedMethods[0].includes(method))throw Error('Factory fault must change exactly its named method: '+mode);
  }
  const host=path.join(leafPayload,'YeManFanHost.dll'),portsProof=path.join(dir,'ports-proof.json');
  run('dotnet',[harness,'factory-leaf-ports',input,host,portsProof],relative('ports-instrumentation.log'),30000);
  const ports=read(portsProof),leafNames=['ValidateFormalSessionCapabilityAcl','ValidateDeploymentGenerationForHcLoad','ValidateGamepadMotion','ConfigureHcRuntime','ResolveDependency'];
  if(ports.status!=='PASS'||ports.sourceSha256!==hash(input)||ports.instrumentedSha256!==hash(host)||ports.changedMethods.length!==5||leafNames.some(name=>ports.changedMethods.filter(method=>method.includes('::'+name+'(')).length!==1))throw Error('Factory ports must change exactly five named I/O leaves');
  const resultPath=path.join(dir,'result.json');run('dotnet',[harness,'factory-init',host,helper,hc,resultPath],relative('run.log'),30000,mode==='positive'?0:2);
  const result=read(resultPath),failures=result.cases.filter(row=>row.status==='FAIL');
  if(result.nativeAttempts!==0||result.hostMainStarted!==false||result.hardwareIo!==false||result.candidateHostSha256!==hash(host)||ordered(result.cases.map(row=>row.id))!==ordered(factoryCaseIds))throw Error('Factory suite identity, zero-I/O or exact case set failed');
  if(mode==='positive'){
   if(result.status!=='PASS'||failures.length||result.checks<157)throw Error('Actual factory consumer cases failed');
  }else{
   const expected=mode==='readiness'?readinessFailures:continuationFailures;
   if(result.status!=='FAIL'||ordered(failures.map(row=>row.id))!==ordered(Object.keys(expected))||failures.some(row=>!row.error?.includes(expected[row.id])))throw Error('Factory fault did not fail its exact business consumers: '+mode);
  }
  rows.push({mode,resultPath,portsProofPath:portsProof,ports,mutation,cases:result.cases.length,checks:result.checks,failedCases:failures.map(row=>row.id),nativeAttempts:0,hostMainStarted:false});
 }
 const proof={status:'PASS',sourceCandidateSha256:hash(candidate),positiveCases:factoryCaseIds.length,caseIds:factoryCaseIds,positiveChecks:rows[0].checks,
  privateFaults:rows.slice(1).map(row=>({mode:row.mode,count:row.failedCases.length,ids:row.failedCases})),executions:rows,
  scope:'Actual compiled factory, readiness, handshake, write admission, read-only sampling and disposal; exactly five I/O leaves replaced, fixture device/sensor terminals',
  productionValidationExecuted:false,actualSensorAndDeviceConstructionExecuted:false,hardwareIo:false,hostMainStarted:false};
 const proofPath=path.join(root,'proof.json');fs.writeFileSync(proofPath,JSON.stringify(proof,null,2));
 return {proof,evidence:tree(root).map(file=>({path:file,sha256:hash(file)}))};
}
