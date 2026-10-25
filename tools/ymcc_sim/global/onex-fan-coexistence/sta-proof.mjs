import fs from 'node:fs';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import path from 'node:path';
import crypto from 'node:crypto';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?tree(path.join(dir,e.name)):[path.join(dir,e.name)]);
const ordered=values=>JSON.stringify([...values].sort());
export const staCaseIds=['sta-existing-real-dispatcher-selftest','sta-public-handshake-real-pump','sta-public-initialization-retry-same-thread',
 'sta-public-reentrant-handshake-refused','sta-normal-disposal-joins-owned-thread','sta-start-failure-is-terminal-and-disposable',
 'sta-disposed-before-start-does-not-bootstrap','sta-disposal-from-own-thread-stops-pump','sta-disposal-during-startup-does-not-leak-pump',
 'sta-ended-pump-cannot-be-reused','sta-concurrent-cold-start-has-one-owner','sta-dispose-before-thread-start-does-not-join-unstarted',
 'sta-busy-admission-serializes-public-handshake','sta-workitem-error-retires-before-next-call','sta-pump-exit-rejects-pending-public-work',
 'sta-failed-factory-disposal-joins-thread'];
const expected={
 'self-stop':{'sta-disposal-from-own-thread-stops-pump':'Disposal on the lifecycle STA must stop its own message pump'},
 'startup-stop':{'sta-disposal-during-startup-does-not-leak-pump':'A disposed pre-publication STA must not enter an unowned message pump'},
 'ended-owner':{'sta-ended-pump-cannot-be-reused':'Expected ObjectDisposedException; got'},
 'start-atomic':{'sta-concurrent-cold-start-has-one-owner':'The actual Thread.Start boundary must remain inside its published owner gate',
  'sta-dispose-before-thread-start-does-not-join-unstarted':'Concurrent disposal must not Join an unstarted thread'},
 'pending-work':{'sta-pump-exit-rejects-pending-public-work':'Pending public work must be rejected promptly by its ended dispatcher'}};
export function runStaProof({out,harness,candidate,payload,helper,hc,run}){
 const root=path.join(out,'sta-bootstrap');fs.mkdirSync(root);const rows=[];
 for(const mode of ['positive',...Object.keys(expected)]){
  const dir=path.join(root,mode);fs.mkdirSync(dir);const leaf=path.join(dir,'leaf-payload');stageFaultPayload(payload,leaf);
  const relative=name=>path.relative(out,path.join(dir,name));let input=candidate,mutation=null;
  if(mode!=='positive'){
   input=path.join(dir,'faulted-host.dll');const proof=path.join(dir,'fault-proof.json');
   run('dotnet',[harness,'fault-sta-'+mode,candidate,input,proof],relative('fault-generation.log'),30000);
   mutation=read(proof);const method=mode==='self-stop'?'::Dispose(':mode==='start-atomic'?'::EnsureStaDispatcher(':mode==='pending-work'?'::RejectUnexecuted(':'::StaDispatcherLoop(';
   if(mutation.status!=='PASS'||mutation.sourceSha256!==hash(candidate)||mutation.faultSha256!==hash(input)||mutation.changedMethods.length!==1||!mutation.changedMethods[0].includes(method))throw Error('STA fault must change exactly its named method: '+mode);
  }
  const host=path.join(leaf,'YeManFanHost.dll'),portsPath=path.join(dir,'ports-proof.json');
  run('dotnet',[harness,'sta-leaf-ports',input,host,portsPath],relative('ports.log'),30000);
  const ports=read(portsPath),methods=['ValidateFormalSessionCapabilityAcl','ValidateDeploymentGenerationForHcLoad','ValidateGamepadMotion','ConfigureHcRuntime','ResolveDependency','ReadCurrentOsThreadId','EnsureStaDispatcher'];
  if(ports.status!=='PASS'||ports.sourceSha256!==hash(input)||ports.instrumentedSha256!==hash(host)||ports.originalStaInstructionsRetained!==true||ports.branchEncodingNormalized!==true||ports.changedMethods.length!==7||methods.some(name=>ports.changedMethods.filter(row=>row.includes('::'+name+'(')).length!==1))throw Error('STA instrumentation must retain normalized startup IL and change only six I/O leaves plus one scheduling probe');
  const resultPath=path.join(dir,'result.json');run('dotnet',[harness,'sta-init',host,helper,hc,resultPath],relative('run.log'),45000,mode==='positive'?0:2);
  const result=read(resultPath),failures=result.cases.filter(row=>row.status==='FAIL');
  if(result.nativeAttempts!==0||result.hostMainStarted!==false||result.hardwareIo!==false||result.candidateHostSha256!==hash(host)||ordered(result.cases.map(row=>row.id))!==ordered(staCaseIds)||ordered(result.staEvidence.map(row=>row.id))!==ordered(staCaseIds)||result.staEvidence.some(row=>!row.allOwnedThreadsExited))throw Error('STA result identity, case set, cleanup or zero-device-I/O contract failed');
  if(mode==='positive'){
   if(result.status!=='PASS'||failures.length||result.checks<100||result.staEvidence.some(row=>row.startGateHeld.some(value=>value!==true)))throw Error('Actual public STA positive consumers failed');
   const healthy=result.staEvidence.find(row=>row.id==='sta-public-handshake-real-pump');
   if(healthy.createdThreads!==1||healthy.readOnlyOsIdentityCalls!==1)throw Error('Healthy public entry did not execute its actual lifecycle thread and read-only OS identity');
  }else if(result.status!=='FAIL'||ordered(failures.map(row=>row.id))!==ordered(Object.keys(expected[mode]))||failures.some(row=>!row.error?.includes(expected[mode][row.id])))throw Error('STA fault did not fail its exact business consumers: '+mode);
  rows.push({mode,mutation,ports,resultPath,portsProofPath:portsPath,cases:result.cases.length,checks:result.checks,failedCases:failures.map(row=>row.id),staEvidence:result.staEvidence});
 }
 const proof={status:'PASS',sourceCandidateSha256:hash(candidate),positiveCases:staCaseIds.length,positiveChecks:rows[0].checks,caseIds:staCaseIds,
  privateFaults:rows.slice(1).map(row=>({mode:row.mode,count:row.failedCases.length,ids:row.failedCases})),executions:rows,
  actualPublicHandshakeExecuted:true,actualStaThreadAndDispatcherExecuted:true,readOnlyOsIdentityEndpoint:'kernel32.GetCurrentThreadId',
  sixExplicitIoLeaves:true,oneNormalizedInstructionPreservingStartProbe:true,allOwnedThreadsExited:true,
  productionValidationExecuted:false,originalDeviceAndSensorAlgorithmsExecuted:false,hostMainStarted:false,hardwareIo:false};
 fs.writeFileSync(path.join(root,'proof.json'),JSON.stringify(proof,null,2));return{proof,evidence:tree(root).map(file=>({path:file,sha256:hash(file)}))};
}
