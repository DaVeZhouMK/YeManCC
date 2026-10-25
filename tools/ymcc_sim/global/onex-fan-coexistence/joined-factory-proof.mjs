import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import {openPreparedParentProofStage} from './parent-build-handoff.mjs';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const tree=dir=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?tree(path.join(dir,e.name)):[path.join(dir,e.name)]);
const sorted=rows=>JSON.stringify([...rows].sort());
const failures={
 'first-start':['joined-auto:AYANEOFlipDS:FLIP DS','joined-auto:AYANEOFlip1SDS:FLIP 1S DS','joined-diagnostic:HandheldCompanion.Devices.AYANEOFlipDS'],
 readiness:['joined-reentrant-handshake','joined-dispose-during-original-stage','joined-owner-replacement:device','joined-owner-replacement:assembly','joined-disposed-factory-stays-refused'],
 continuation:['joined-retry:board:baseboard:Manufacturer'],
 native:['joined-native:missing','joined-native:invalid','joined-native:long-missing','joined-native:long-invalid'],
 'native-path':['joined-native:long-valid']};
export function runJoinedFactoryProof({repo,out,harness,candidate,hc,run,preparedContext=null}){
 const root=path.join(out,'joined-factory');if(preparedContext)openPreparedParentProofStage(preparedContext,{repo,out,kind:'JoinedFactoryProbe'});else fs.mkdirSync(root);const relative=file=>path.relative(out,file);
 const runtime=path.dirname(hc),shared=path.join(repo,'PowerControl/ymcc-shared-runtime'),constructors=path.join(out,'original-constructors');
 const originalPorts=read(path.join(constructors,'ports-proof.json')),original=read(path.join(constructors,'proof.json'));
 if(original.status!=='PASS'||originalPorts.changedMethods.length!==7||original.originalHcSha256!==hash(hc)||original.originalConstructorsExecuted!==79)throw Error('Joined proof requires the same pinned original constructors and proven seven IO leaves');
 const build=path.join(root,'probe-build');run('dotnet',['build',path.join(repo,'tools/ymcc_sim/global/onex-fan-coexistence/JoinedFactoryProbe.csproj'),'--artifacts-path',build,'-c','Release','--nologo','-v:q','-m:1','-p:UseSharedCompilation=false','-nodeReuse:false'],relative(path.join(root,'probe-build.log')),120000);
 const probes=tree(path.join(build,'bin')).filter(f=>path.basename(f)==='JoinedFactoryProbe.dll');if(probes.length!==1)throw Error('Exactly one standalone joined probe required');const probe=probes[0];
 const image=path.join(root,'hc/HandheldCompanion.dll'),hcProof=path.join(root,'hc-ports-proof.json');
 run('dotnet',[harness,'joined-hc-ports',path.join(constructors,'leaf-hc/HandheldCompanion.dll'),image,hcProof,runtime,shared],relative(path.join(root,'hc-ports.log')),30000);
 const hcPorts=read(hcProof);if(hcPorts.status!=='PASS'||hcPorts.changedMethods.length!==2||hcPorts.instrumentedSha256!==hash(image)||hcPorts.originalAllInstanceConstructorsGetCurrentAndInitializeUnchanged!==true)throw Error('Joined HC escaped its two explicit IO leaves');
 const host=path.join(root,'host/YeManFanHost.dll');stageFaultPayload(path.dirname(candidate),path.dirname(host));const hostProof=path.join(root,'host-ports-proof.json');
 run('dotnet',[harness,'joined-host-ports',candidate,host,hostProof],relative(path.join(root,'host-ports.log')),30000);
 const hostPorts=read(hostProof);if(hostPorts.status!=='PASS'||hostPorts.changedMethods.length!==3||hostPorts.sourceSha256!==hash(candidate)||hostPorts.instrumentedSha256!==hash(host)||hostPorts.configureBodyExactExceptSingleTempGetter!==true||hostPorts.actualNativeAdmissionPreserved!==true||hostPorts.actualAssemblyLoadPreserved!==true)throw Error('Joined Host escaped its exact validation/configuration leaves');
 const basePins=read(path.join(constructors,'positive/runtime-pins.json')),executions=[],boundaryEvidence=[];let names;
 for(const mode of ['positive','first-start','readiness','continuation','native','native-path']){
  const dir=path.join(root,mode);fs.mkdirSync(dir);let selected=host,mutation=null;
  if(mode!=='positive'){
   selected=path.join(dir,'host/YeManFanHost.dll');stageFaultPayload(path.dirname(host),path.dirname(selected));const proof=path.join(dir,'fault-proof.json');
   const cli=mode==='first-start'?'fault-initialize-first-start':mode==='native-path'?'fault-native-path':mode==='native'?'fault-original-constructor-native':'fault-factory-'+mode;
   run('dotnet',[harness,cli,host,selected,proof,runtime,shared],relative(path.join(dir,'fault-generation.log')),30000);mutation=read(proof);
   if(mutation.status!=='PASS'||mutation.sourceSha256!==hash(host)||mutation.faultSha256!==hash(selected)||mutation.changedMethods.length!==1)throw Error('Joined private fault must change exactly its selected original method');
  }
  const pins={...basePins,[image]:hash(image)};for(const file of tree(path.dirname(selected)))pins[file]=hash(file);
  const pinFile=path.join(dir,'pins.json');fs.writeFileSync(pinFile,JSON.stringify(pins,null,2)+'\n');const resultFile=path.join(dir,'case/result.json');
  run('dotnet',[probe,selected,image,runtime,shared,pinFile,path.join(constructors,'inventory.json'),path.join(repo,'FanLab/real-host/YeManFanHost.json'),resultFile],relative(path.join(dir,'run.log')),60000,mode==='positive'?0:2);
  const result=read(resultFile),failed=result.cases.filter(r=>r.status!=='PASS');
  for(const file of result.nativeBoundaryFiles??[]){if(!path.isAbsolute(file.path)||path.relative(result.nativeBoundaryRoot,file.path).startsWith('..')||path.resolve(result.nativeBoundaryRoot)!==path.join(dir,'case','n')||file.sha256!==hash(path.join(runtime,'GamepadMotion.dll'))||hash(file.path)!==file.sha256)throw Error('Native boundary output escaped its owned pinned Build fixture');boundaryEvidence.push(file);}
  if(result.nativeBoundaryFiles?.length!==1)throw Error('Exact259-character original native-loader boundary was not exercised');
  if(result.cases.length!==123||result.hcSha256!==hash(image)||result.hostSha256!==hash(selected)||result.uniqueConstructedTypes.length!==79||result.deniedNative!==0||result.nativeMathBindings<=0||result.legacyFixtureReferenced!==false||result.managerStartCalled!==false||result.actualDeviceOpenBodyExecuted!==false||result.hostMainStarted!==false||result.hardwareIo!==false)throw Error('Joined original factory execution scope differs: '+mode);
  if(mode==='positive'){if(result.status!=='PASS'||failed.length)throw Error('Joined original factory consumers failed');names=result.cases.map(r=>r.id);}
  else{
   if(result.status!=='FAIL'||sorted(failed.map(r=>r.id))!==sorted(failures[mode]))throw Error('Joined private fault did not fail its exact original consumer set: '+mode);
   const markers=mode==='first-start'?['JOINED_SETTING_WRITE_REFUSED:']:mode==='continuation'?['cannot replay completed runtime configuration']:mode==='native-path'?['GamepadMotion.dll could not load']:mode==='native'?['Joined consumer must refuse GamepadMotion.dll']:['cannot publish readiness','Joined consumer must refuse HC_NOT_INITIALIZED','cannot publish late readiness','Replacement cannot inherit','Joined consumer must refuse ObjectDisposedException','Actual factory rejects reentrant readiness'];
   const readinessAssertions={
    'joined-reentrant-handshake':'Joined real factory publishes readiness after all original stages',
    'joined-dispose-during-original-stage':'Joined consumer must refuse ObjectDisposedException',
    'joined-owner-replacement:device':'Joined consumer must refuse HC_NOT_INITIALIZED',
    'joined-owner-replacement:assembly':'Joined consumer must refuse HC_NOT_INITIALIZED',
    'joined-disposed-factory-stays-refused':'Joined consumer must refuse ObjectDisposedException'};
   if(failed.some(r=>mode==='readiness'?!r.error.includes(readinessAssertions[r.id]):!markers.some(marker=>r.error.includes(marker))))throw Error('Joined fault failed before its intended business assertion: '+mode);
  }
  if(sorted(result.cases.map(r=>r.id))!==sorted(names))throw Error('Joined private fault case set differs');
  executions.push({mode,cases:result.cases.length,checks:result.checks,resultPath:resultFile,failedIds:failed.map(r=>r.id),mutation});
 }
 const proof={status:'PASS',originalHcSha256:hash(hc),candidateHostSha256:hash(candidate),hostPorts,hcPorts,positiveCases:123,positiveChecks:executions[0].checks,originalConstructorsExecuted:79,automaticIdentityCases:27,diagnosticAdmissionCases:79,executions,privateFaults:executions.slice(1).map(r=>({mode:r.mode,count:r.failedIds.length,ids:r.failedIds})),actualDefaultAssemblyLoadExecuted:true,actualOriginalGetCurrentAndConstructorsExecuted:true,actualConfigureAndNativeAdmissionExecuted:true,actualIdentityCapabilityAndAuthorizationExecuted:true,legacyFixtureReferenced:false,hardwareIo:false,managerStartCalled:false,hostMainStarted:false,scope:'Actual Host factory and Default assembly load, original79 constructors/GetCurrent/Initialize, real Configure/logger/dependency resolver/native admission, original identity/capability/write refusal. Seven constructor and two sensor/settings IO leaves, two independently proven validation leaves and one temp-root operand only; no Host Main or hardware.'};
 fs.writeFileSync(path.join(root,'proof.json'),JSON.stringify(proof,null,2)+'\n');return{proof,sourceHashes:{},evidence:[...tree(root).map(file=>({path:file,sha256:hash(file)})),...boundaryEvidence]};
}
