import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {sourcePath,runProcess} from '../runner.mjs';
import {CASE_ID,MODULE,REPO,WORKSPACE,sourceFiles,artifactFiles,managedContract,inside} from './adapter.mjs';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const need=(v,m)=>{if(!v)throw Error(m);};
const write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'OUTPUT_LINK');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
function verifyManaged(contract){
 const {pin,manifest,value}=contract;
 need(hash(manifest)===pin.sha256,'MANAGED_CONTRACT_STALE_MANIFEST');
 need(value.sourceStable&&value.toolchainStable&&value.normalHostMainExecuted===false&&value.realSdkLoaded===false&&value.hardwareIo===false,'MANAGED_BUILD_BOUNDARY');
 for(const row of value.inputBindings.sources)need(hash(row.path)===row.sha256&&hash(row.snapshot)===row.sha256,'MANAGED_CONTRACT_STALE_SOURCE:'+row.path);
 for(const row of value.inputBindings.generatedInputs)need(hash(row.path)===row.sha256,'MANAGED_CONTRACT_STALE_GENERATED:'+row.path);
 for(const row of value.runtimeArtifacts)need(hash(row.path)===row.sha256,'MANAGED_CONTRACT_STALE_PAYLOAD:'+row.path);
 need(hash(value.generatedIdentity.path)===value.generatedIdentity.sha256,'MANAGED_CONTRACT_STALE_IDENTITY');
}
export async function execute(root){
 root=path.resolve(root);need((inside(path.join(WORKSPACE,'Build'),root)||inside(path.join(WORKSPACE,'Testing/YMCC/Evidence'),root))&&!fs.existsSync(root),'FRESH_OWNED_EVIDENCE_REQUIRED');fs.mkdirSync(root,{recursive:true});
 const steps=[];
 try{
  const contract=managedContract();verifyManaged(contract);
  const sources=sourceFiles().map(p=>({path:p,sha256:hash(sourcePath(REPO,p))})),artifacts=artifactFiles().map(p=>({path:p,sha256:hash(p)}));need(artifacts.length<=640,'ARTIFACT_BOUND_EXCEEDED');
  write(path.join(root,'inputs-before.json'),{sources,artifacts,managedManifest:{path:contract.manifest,sha256:hash(contract.manifest)}});
  const dotnet='C:/Program Files/dotnet/dotnet.exe';const env={...process.env,DOTNET_ROOT:path.dirname(dotnet),DOTNET_MULTILEVEL_LOOKUP:'0',YMCC_VCVARS64:'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'};
  for(const key of Object.keys(env))if(/^(?:CL|_CL_|LINK|_LINK_|NODE_OPTIONS|NODE_PATH)$/i.test(key))delete env[key];
  async function suite(name,source,expected){
   const out=path.join(root,name),args=[sourcePath(REPO,'tools/input_lifecycle_pipe_integration.mjs'),'--out',out,'--source',source,'--managed-dll',contract.value.fixtureDll,'--dotnet',dotnet,...(expected?['--expect-rejection',expected]:[])];
   const p=await runProcess(process.execPath,args,{cwd:REPO,env,timeoutMs:65000,outputLimit:2*1024*1024});fs.writeFileSync(path.join(root,name+'.stdout.log'),p.stdout);fs.writeFileSync(path.join(root,name+'.stderr.log'),p.stderr);const{stdout,stderr,...terminal}=p;steps.push({name,args,...terminal});write(path.join(root,'process-ledger.json'),{steps});
   need(p.exitCode===0&&!p.timedOut&&!p.launchError&&!p.signal&&!p.outputTruncated,'INTEGRATION_RUNNER_NOT_COMPLETED:'+name);
   const r=read(path.join(out,'RESULT.json'));need(r.status==='PASS'&&r.passed===true&&r.sourceUnchanged&&r.configurations.length===2,'INTEGRATION_FINAL_REPORT_INVALID:'+name);
   need(r.managedDll.sha256===contract.value.runtimeArtifacts.find(row=>path.resolve(row.path)===path.resolve(contract.value.fixtureDll)).sha256,'WRONG_MANAGED_FIXTURE_USED');
   for(const row of r.sources){const relative=path.relative(REPO,row.path).replaceAll('\\','/');if(inside(root,row.path))continue;need(sources.some(s=>s.path===relative&&s.sha256===row.sha256),'UNBOUND_ORIGINAL_RUNNER_SOURCE:'+row.path);}
   for(const c of r.configurations){
    need(c.includes.length>20,'COMPILER_INCLUDE_OBSERVATION_MISSING');need(c.linkerLibraries.length>0,'LINKER_LIBRARY_OBSERVATION_MISSING');for(const file of [...c.includes,...c.linkerLibraries])need(inside(out,file)||artifacts.some(row=>row.path.toLowerCase()===file.toLowerCase()),'UNPINNED_COMPILER_INPUT:'+file);
    if(expected){need(c.status==='EXPECTED_SEMANTIC_REJECTION'&&c.exitCode===1&&c.result.error.includes(expected),'WRONG_NEGATIVE_ASSERTION:'+name);}
    else{
     need(c.status==='PASS'&&c.exitCode===0&&c.result.cases.length===16&&c.result.cases.every(item=>item.passed)&&c.result.nativeTransport.pending===0&&c.result.nativeTransport.payloadBytes===0,'INCOMPLETE_ACTUAL_INTEGRATION:'+c.mode);
     const local=c.result.beforeParentExitResources;need(c.result.nativeTransport.stopped&&['ownedOriginals','ownedDuplicates','ownedEvents','failedCloseDebt','retainedOperations','cachedOperations'].every(k=>c.result.nativeTransport[k]===0),'FULL_NATIVE_PIPE_RETURN_NOT_PROVEN');
     need(local&&local.privateHostHandles===0&&local.privateParentHandles===0&&local.closeFailures===0&&local.hostForcedBackstops===0&&local.hostProcessJoins===16&&local.hostPrimaryJoins===16&&!local.processExitUsedAsCleanup,'PRIVATE_CHILD_HANDLES_NOT_RETURNED_BEFORE_PARENT');
     const parents=c.result.cases.filter(item=>item.parentLifetime);need(parents.length===9,'HELD_SESSION_PARENT_CASES_MISSING');
     for(const persona of ['steamdeck','dualsense-edge','elite']){
      const rows=parents.filter(item=>item.persona===persona);need(rows.length===3,'PARENT_EXIT_PERSONA_COVERAGE_MISSING:'+persona);
      for(const [abrupt,retry]of [[false,false],[true,false],[true,true]]){
       const item=rows.find(row=>row.parentLifetime.abruptExit===abrupt&&row.parentLifetime.firstStopFailure===retry);need(item,'PARENT_EXIT_KIND_MISSING:'+persona);
       const p=item.parentLifetime,child=item.managedReceipt;need(p.actualParentHandle&&p.serverHeldOpenUntilHostExit&&p.watchedPid!==p.supervisorPid&&p.watchedPid===child.parentPid&&p.creationTime.length>0&&p.nativeReleaseCommands===0,'PARENT_EXIT_NOT_ISOLATED_FROM_PIPE_CLOSE');
       need(item.commands.length===5&&item.commands.every(row=>!['QUIESCE','RELEASE_TARGET','SHUTDOWN'].includes(row.command))&&child.creates===1,'PARENT_EXIT_USED_NORMAL_RELEASE_OR_RECREATED');
       need(child.exitReasons.length===1&&child.exitReasons[0].reason==='parent-exited'&&child.pipeLoopExitCode===0&&child.finalCleanupOk&&!child.hasController&&!child.hasContext,'PARENT_EXIT_DID_NOT_FINISH_ACTUAL_SESSION');
       need(child.actualProductionTerminalSettlement&&child.terminalSettlement.ManagedReleaseComplete&&child.terminalSettlement.ExitCode===0&&child.stateAfterLoop.hasController&&child.stateAfterLoop.hasContext,'PARENT_EXIT_UNIQUE_TERMINAL_SETTLEMENT_MISSING');
       const releases=child.terminalReleaseAttempts;need(releases.length===(retry?2:1)&&releases.at(-1).ok,'PARENT_EXIT_RELEASE_ATTEMPT_BUDGET');
       if(retry)need(!releases[0].ok&&releases[0].after.localReleasePending&&releases[0].after.hasController&&releases[0].after.hasContext&&child.outputStopResults.length===2&&!child.outputStopResults[0].result&&child.outputStopResults[1].result,'PARENT_EXIT_RETRY_BARRIER_MISSING');
      }
     }
     for(const item of c.result.cases){
      const child=item.managedReceipt;need(child.actualProductionPipeLoop&&child.actualProductionMessageCodec&&child.actualProductionParentProcessLifetime&&child.actualProductionHostSessionConsumer&&!child.productMainExecuted&&!child.hardwareIo&&!child.realSdkLoaded,'CHILD_BOUNDARY_INVALID');
      need(child.status==='OBSERVED'&&child.failure===null&&child.inputManifestSha256===contract.value.inputManifestSha256,'CHILD_BINDING_INVALID');
      for(const row of child.finalBindings.loadedAssemblies)need(artifacts.some(a=>a.path.toLowerCase()===row.path.toLowerCase()&&a.sha256===row.sha256),'UNPINNED_ACTUALLY_LOADED_ASSEMBLY:'+row.path);
     }
    }
   }
   return {path:path.join(out,'RESULT.json'),sha256:hash(path.join(out,'RESULT.json')),profiles:r.configurations.map(c=>({mode:c.mode,status:c.status,exitCode:c.exitCode,checks:c.result.checks,cases:c.result.cases?.map(item=>({id:item.id,checks:item.checks,passed:item.passed}))??null,error:c.result.error??null})),expectedRejection:!!expected};
  }
  const positive=await suite('positive',sourcePath(REPO,'native/main.cpp'));
  const main=fs.readFileSync(sourcePath(REPO,'native/main.cpp'),'utf8'),faults=[
   {id:'sequence-sync-omitted',before:'if (advanceSequence && hostConsumed) g_inputHostSequence = sequenceValue;',after:'(void)hostConsumed;',expected:'business-refusal-sequence-synced-without-transport-fault'},
   {id:'instance-fence-omitted',before:'if (!tupleMatches || !identityOk) {',after:'(void)identityOk; if (!tupleMatches) {',expected:'stale-Host-instance-cannot-accept-ACK'}
  ],negative=[];
  for(const fault of faults){need(main.split(fault.before).length===2,'SOURCE_FAULT_ANCHOR_CHANGED:'+fault.id);const source=path.join(root,fault.id+'.cpp');fs.writeFileSync(source,main.replace(fault.before,fault.after));negative.push({id:fault.id,before:fault.before,after:fault.after,originalAssertionsUnchanged:true,proof:await suite('negative-'+fault.id,source,fault.expected)});}
  verifyManaged(contract);need(sources.every(row=>hash(sourcePath(REPO,row.path))===row.sha256),'SOURCE_CHANGED_DURING_RUN');need(artifacts.every(row=>hash(row.path)===row.sha256),'ARTIFACT_CHANGED_DURING_RUN');
  const result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,positive,negative,sourceHashes:[...sources,...walk(root).map(p=>({path:p,sha256:hash(p)}))],toolchainArtifacts:artifacts,sourceBoundManagedFixture:{manifest:contract.manifest,sha256:hash(contract.manifest),dll:contract.value.fixtureDll,actualLoadedAssemblyHashesVerified:true,compilerExecutedInThisAdapter:false},boundary:{completeNativeRequestExecuted:true,managedPipeAndSessionExecuted:true,heldSessionParentCasesPerProfile:9,parentNormalExitAndAbruptTermination:true,parentExitWithFirstStopFailure:true,realCrossProcessWindowsPipes:true,sdkIo:false,deviceIo:false,normalProductMainExecuted:false,fullCoordinatorLifecycleExecuted:false,physicalReturnAccepted:false,osSleepExecuted:false,cpuAcceptance:false,productionModified:false}};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){const blocked=error.message.startsWith('MANAGED_CONTRACT_STALE');write(path.join(root,'failure.json'),{status:blocked?'BLOCKED':'FAIL',error:error.message,steps,compilationFailureCountedAsNegative:false});if(blocked)error.exitCode=7;throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{need(process.argv.length===4&&process.argv[2]==='--evidence-dir','EVIDENCE_REQUIRED');const r=await execute(process.argv[3]);console.log(JSON.stringify({id:r.id,status:r.status,profiles:2,casesPerProfile:16,negativeKinds:2,physicalReturnAccepted:false}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=error.exitCode??2;}
}
