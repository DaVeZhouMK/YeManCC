import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import {openPreparedParentProofStage} from './parent-build-handoff.mjs';
import{generateBootstrapFixture}from'./bootstrap-generate.mjs';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?tree(path.join(dir,e.name)):[path.join(dir,e.name)]);
const expected={model:['bootstrap-model:ROGAlly:RC71L','bootstrap-factory:healthy-original'],cache:['bootstrap-cache:valid-collect','bootstrap-factory:loaded-cache'],sensors:['bootstrap-sensors:gyro','bootstrap-sensors:accel','bootstrap-sensors:gyro-precedence','bootstrap-sensors:internal-remove-keeps-other-bits']};
const sorted=items=>JSON.stringify([...items].sort());
export function runBootstrapProof({repo,out,harness,candidate,payload,helper,hc,run,preparedContext=null}){
 const root=path.join(out,'original-bootstrap');if(preparedContext)openPreparedParentProofStage(preparedContext,{repo,out,kind:'BootstrapFixture'});else fs.mkdirSync(root);
 const relative=file=>path.relative(out,file);
 const generated=path.join(root,'generated');const provenance=generateBootstrapFixture({repo,out:generated});
 const build=path.join(root,'build');
 run('dotnet',['build',path.join(repo,'tools/ymcc_sim/global/onex-fan-coexistence/BootstrapFixture.csproj'),'--artifacts-path',build,'-p:BootstrapSource='+path.join(generated,'BootstrapFixture.g.cs'),'-c','Release','--nologo','-v:q','-m:1','-p:UseSharedCompilation=false','-nodeReuse:false'],relative(path.join(root,'build.log')),120000);
 const compiled=tree(path.join(build,'bin')).filter(file=>path.basename(file)==='YmccBootstrapFixture.dll');if(compiled.length!==1)throw Error('Exactly one compiled bootstrap skeleton required');
 const fixtureDir=path.join(root,'bound');fs.mkdirSync(fixtureDir);const fixture=path.join(fixtureDir,'YmccBootstrapFixture.dll'),binding=path.join(root,'binding-proof.json');
 run('dotnet',[harness,'bootstrap-bind-original',hc,compiled[0],fixture,binding],relative(path.join(root,'binding.log')),30000);
 const structure=read(binding+'.after.json');if(structure.status!=='PASS'||structure.methods.length!==42||structure.methods.some(m=>!m.matched)||structure.originalSha256!==hash(hc)||structure.fixtureSha256!==hash(fixture))throw Error('Complete original bootstrap instructions/locals/EH did not match pinned HC');
 const json=path.join(repo,'PowerControl/ymcc-shared-runtime/Newtonsoft.Json.dll');fs.copyFileSync(json,path.join(fixtureDir,'Newtonsoft.Json.dll'));
 const executions=[];let names=null;
 for(const mode of ['positive','model','cache','sensors']){
  const dir=path.join(root,mode);fs.mkdirSync(dir);const ownedPayload=path.join(dir,'leaf-payload');stageFaultPayload(payload,ownedPayload);
  const host=path.join(ownedPayload,'YeManFanHost.dll'),ports=path.join(dir,'host-ports-proof.json');
  run('dotnet',[harness,'factory-leaf-ports',candidate,host,ports],relative(path.join(dir,'host-ports.log')),30000);
  const hostPorts=read(ports);if(hostPorts.status!=='PASS'||hostPorts.changedMethods.length!==5||hostPorts.sourceSha256!==hash(candidate))throw Error('Bootstrap Host must preserve actual factory and exactly five admitted I/O leaves');
  let input=fixture,mutation=null;
  if(mode!=='positive'){
   const fault=path.join(dir,'fault-fixture');fs.mkdirSync(fault);input=path.join(fault,'YmccBootstrapFixture.dll');const proof=path.join(dir,'fault-proof.json');
   run('dotnet',[harness,'fault-bootstrap-'+mode,fixture,input,proof],relative(path.join(dir,'fault-generation.log')),30000);mutation=read(proof);
   fs.copyFileSync(json,path.join(fault,'Newtonsoft.Json.dll'));
   if(mutation.status!=='PASS'||mutation.sourceSha256!==hash(fixture)||mutation.faultSha256!==hash(input)||mutation.changedMethods.length!==1)throw Error('Bootstrap fault escaped one named algorithm');
  }
  const resultPath=path.join(dir,'result.json');run('dotnet',[harness,'bootstrap-init',host,helper,hc,resultPath,input],relative(path.join(dir,'run.log')),60000,mode==='positive'?0:2);
  const result=read(resultPath),failed=result.cases.filter(c=>c.status!=='PASS');
  if(result.cases.length!==56||result.nativeAttempts!==0||result.originalBootstrapAlgorithmsExecuted!==true||result.actualFactoryExecuted!==true||result.constructorsOriginalBodiesExecuted!==false||result.hardwareIo!==false||result.hostMainStarted!==false||result.bootstrapSha256!==hash(input)||result.candidateHostSha256!==hash(host))throw Error('Bootstrap execution contract or exact case set differs');
  if(mode==='positive'){if(result.status!=='PASS'||failed.length)throw Error('Actual original bootstrap consumers failed');names=result.cases.map(c=>c.id);}
  else if(result.status!=='FAIL'||sorted(failed.map(c=>c.id))!==sorted(expected[mode]))throw Error('Bootstrap fault did not fail exactly its original business consumers: '+mode+' '+failed.map(c=>c.id));
  if(names&&sorted(result.cases.map(c=>c.id))!==sorted(names))throw Error('Bootstrap positive/fault case sets differ');
  executions.push({mode,cases:result.cases.length,checks:result.checks,failedIds:failed.map(c=>c.id),hostSha256:hash(host),fixtureSha256:hash(input),resultPath,mutation});
 }
 for(const [file,sha]of Object.entries(provenance.sourceHashes))if(hash(path.join(repo,file))!==sha)throw Error('Bootstrap original source changed during execution: '+file);
 const proof={status:'PASS',originalHcSha256:hash(hc),candidateHostSha256:hash(candidate),originalMethodCount:42,positiveCases:56,positiveChecks:executions[0].checks,sourceProvenance:provenance,bindingProof:read(binding),structureProofPath:binding+'.after.json',privateFaults:executions.slice(1).map(r=>({mode:r.mode,ids:r.failedIds,count:r.failedIds.length})),executions,actualOriginalBootstrapAlgorithmsExecuted:true,actualFactoryAndIdentityExecuted:true,deviceConstructorBodiesExecuted:false,nativeBootstrapValidationExecuted:false,hardwareIo:false,hostMainStarted:false,scope:'Pinned original GetCurrent/PullSensors/Motherboard/Between PE bodies and wired Host factory; all constructors, WMI/sensor/USB/telemetry terminals and five previously proven Host IO leaves remain inert'};
 fs.writeFileSync(path.join(root,'proof.json'),JSON.stringify(proof,null,2)+'\n');return{proof,sourceHashes:provenance.sourceHashes,evidence:tree(root).map(file=>({path:file,sha256:hash(file)}))};
}
