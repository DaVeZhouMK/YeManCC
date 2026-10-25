import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {REPO,MODULE,FIXTURE,CASE_ID,sourceFiles,toolchain,verifyTools,read,write,hash,need,inside,walk,checkPlan,stopFallbackFault} from '../../../fixtures/battery_ui_native_recovery/contract.mjs';
import {nodeBuildConfiguration} from '../../../fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs';
import {compileManaged} from '../../../fixtures/battery_native_host_roundtrip/compile-managed.mjs';
import {compileNative} from '../../../fixtures/battery_ui_native_recovery/compile-native.mjs';
import {executor} from '../../../fixtures/battery_ui_native_recovery/execute.mjs';
import {verifyExecution} from '../../../fixtures/battery_ui_native_recovery/verify.mjs';
import {allocateScratch,verifyScratch,archiveScratch,cleanupScratch} from '../sleep-native-recovery-consumer/scratch.mjs';
export function runBatteryUiNativeRecovery(value){
 const evidence=path.resolve(value);
 need(path.isAbsolute(value)&&!inside(REPO,evidence)&&!fs.existsSync(evidence),'BATTERY_UI_FRESH_EXTERNAL_EVIDENCE_REQUIRED');
 const tool=toolchain(REPO),plan=checkPlan(read(path.join(REPO,FIXTURE,'cases.json')));
 need(path.resolve(process.execPath).toLowerCase()===path.resolve(tool.node).toLowerCase(),'BATTERY_UI_PINNED_NODE_REQUIRED');
 verifyTools(tool);fs.mkdirSync(evidence,{recursive:true});
 const sources=sourceFiles(REPO).map(relative=>({relative,path:path.join(REPO,relative),sha256:hash(path.join(REPO,relative))}));
 const owner=allocateScratch(evidence),root=verifyScratch(owner,evidence);write(path.join(evidence,'scratch-allocation.json'),owner);
 const inputRoot=path.join(root,'input-sources');
 for(const row of sources)for(const base of [inputRoot,path.join(evidence,'input-sources')]){
  const destination=path.join(base,row.relative);fs.mkdirSync(path.dirname(destination),{recursive:true});
  fs.copyFileSync(row.path,destination,fs.constants.COPYFILE_EXCL);need(hash(destination)===row.sha256,'BATTERY_UI_SOURCE_CAPTURE_CHANGED');
 }
 write(path.join(root,'execution-owner.json'),owner);
 write(path.join(root,'complete-artifact-inputs-before.json'),{files:tool.files,externalFiles:tool.externalCount,currentSourceArtifacts:tool.currentSourceArtifactCount,truncated:false});
 write(path.join(evidence,'input-snapshot.json'),{sources,tools:tool.files,externalToolFiles:tool.externalCount,currentSourceArtifacts:tool.currentSourceArtifactCount,toolInputsTruncated:false,oldPrivateBinariesReused:false});
 const ledger=[],execute=executor(root,ledger),records=[];let failure=null,managed=null,native=null;
 try{
  managed=compileManaged({inputRoot,root,tool,execute});native=compileNative({inputRoot,root,tool,execute});
  for(const name of ['temp','local','roaming','runs'])fs.mkdirSync(path.join(root,name));
  const originalComponent=path.join(inputRoot,'src/components/BatteryChargeSettings.vue');
  const fault=stopFallbackFault(fs.readFileSync(originalComponent,'utf8')),faultComponent=path.join(root,'stop-fallback-removed.vue');
  fs.writeFileSync(faultComponent,fault.source);
  write(path.join(root,'source-fault-binding.json'),{...fault,source:undefined,originalComponent,originalComponentSha256:hash(originalComponent),faultComponent,faultComponentSha256:hash(faultComponent),onlyDeclaredFallbackGuardChanged:true,expectedOriginalFailure:plan.fault.expectedFailed});
  for(const variant of ['positive','source-fault']){
   const run=path.join(root,'runs',variant);fs.mkdirSync(run);
   const component=variant==='positive'?originalComponent:faultComponent;
   const boundInputs=[path.join(inputRoot,'src/bridge/batteryCharge.ts'),path.join(inputRoot,FIXTURE,'ui-driver.mjs'),native.executable,component].map(p=>({path:p,sha256:hash(p)}));
   const config={root,repo:inputRoot,...nodeBuildConfiguration(tool,REPO),variant,artifactSnapshot:path.join(root,'complete-artifact-inputs-before.json'),artifactSnapshotSha256:hash(path.join(root,'complete-artifact-inputs-before.json')),...(variant==='source-fault'?{component}:{}),dotnet:tool.dotnet,edge:tool.edge,browserModuleRoot:tool.browserModuleRoot,boundInputs};
   write(path.join(run,'config.json'),config);
   const expected={status:variant==='positive'?'PASS':'FAIL',naturalExit:variant==='positive'?0:plan.fault.expectedExit,failed:variant==='positive'?[]:plan.fault.expectedFailed,configSha256:hash(path.join(run,'config.json')),toolCount:tool.files.length,completeToolDeclarationSha256:hash(path.join(REPO,FIXTURE,'toolchain-inputs.json')),artifactSnapshotSha256:config.artifactSnapshotSha256,businessNotYetExecuted:true};
   write(path.join(run,'fixed-expected-before.json'),expected);
   const env={...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1',YMCC_BATTERY_RUNTIME_ALLOWLIST:managed.allowFile,YMCC_BATTERY_RUNTIME_RECEIPT:path.join(run,'guardian-runtime-boundary.json')};
   for(const key of ['NODE_OPTIONS','NODE_PATH','DOTNET_STARTUP_HOOKS','DOTNET_ADDITIONAL_DEPS','DOTNET_SHARED_STORE'])delete env[key];
   const command=execute('owned-ui-'+variant,tool.dotnet,[managed.guardian,tool.node,path.join(inputRoot,FIXTURE,'ui-driver.mjs'),run,owner.nonce],{cwd:run,env,timeout:180000,expectedExit:expected.naturalExit});
   records.push(verifyExecution({run,root,nonce:owner.nonce,variant,expected,component,tool,command}));
  }
 }catch(error){failure={message:error.message,stack:error.stack};write(path.join(root,'run-failure.json'),failure);}
 if(tool.nodeToolBinding){try{verifyTools(tool);}catch(error){failure??={message:error.message,stack:error.stack};write(path.join(root,'node-tools-postcheck-failure.json'),{message:error.message,stack:error.stack});}}
 const sourceChanges=sources.filter(x=>!fs.existsSync(x.path)||hash(x.path)!==x.sha256).map(x=>x.relative);
 const toolChanges=tool.files.filter(x=>!fs.existsSync(x.path)||hash(x.path)!==x.sha256).map(x=>x.path);
 const mapping=archiveScratch(owner,evidence);write(path.join(evidence,'execution-archive-mapping.json'),mapping);
 const innerPassed=!failure&&records.length===2&&records.every(x=>x.status==='PASS')&&!sourceChanges.length&&!toolChanges.length;
 let cleanup={root,cleaned:false,retainedForFailure:true};if(innerPassed)cleanup=cleanupScratch(owner,evidence,mapping);
 write(path.join(evidence,'scratch-cleanup.json'),cleanup);
 const passed=innerPassed&&cleanup.cleaned;
 const result={schemaVersion:1,id:CASE_ID,status:passed?'PASS':'FAIL',passed,failure,positiveCases:3,sourceFaults:1,records,sourceChanges,toolChanges,actualRpcPairs:records.reduce((n,x)=>n+x.hostRpcPairs,0),sourceHashes:[...sources.map(x=>({path:x.relative,sha256:x.sha256})),...walk(evidence).map(p=>({path:p,sha256:hash(p)}))],toolInputs:tool.files,toolScope:{externalFiles:tool.externalCount,currentSourceArtifacts:tool.currentSourceArtifactCount,total:tool.files.length,limit:3072,truncated:false,oldLeaf1024LimitUnchanged:true,privateGeneratedFilesRebuiltFromCurrentSources:13},executionArchive:{mapping:path.join(evidence,'execution-archive-mapping.json'),mappingSha256:hash(path.join(evidence,'execution-archive-mapping.json')),originalRoot:root,files:mapping.files.length,originalContentsRewritten:false,cleaned:cleanup.cleaned},inputConsumerStateFeedback:{input:'Three fixed software-owner structured native failure/throw/fresh unknown scenarios and one actual stop-fallback guard source fault',consumer:'Whole current native request function through real Host request/RuntimeContract/dispatch/Controller plus complete current batteryCharge bridge and mounted real Vue component',state:'Unchanged original Rig/MemoryStore/Fake; memory Win32 power/pipe/clock and stream transport only',feedback:'Actual error DOM, retained software ownership, explicit user100/Apply reaching original Release, request/response and natural Job/browser/native/.NET receipts'},boundary:{currentGPositiveCapturedWithoutCandidateInjection:true,wholeOriginalNativeHeader:true,originalHostRequestStatements:true,existingRigAndMemoryStore:true,wholeCurrentBridge:true,mountedCurrentComponent:true,explicitUserReleaseOnly:true,automaticMutation:false,memoryWin32AndHostStream:true,realOwnedStdinStdout:true,realNamedPipe:false,productionHostMain:false,productionServe:false,physicalHardware:false,installedApplicationWrites:false,gamepadNavigationAcceptance:false,fullProductAcceptance:false},compatibility:{sourceCurrent:sourceChanges.length===0,toolsCurrent:toolChanges.length===0,compileFailureCountsAsSourceFault:false,missingActualToolMemberRejected:true,toolsTruncated:false},remainingGaps:['Physical root cause of a reconnection timeout and real namedpipe ACL/kernel cancellation','Production Host Main/Serve/device ownership and normal installation','Complete gamepad UI navigation gate and physical input acceptance'],goalComplete:false};
 write(path.join(evidence,'result.json'),result);return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=process.argv.slice(2);need(args.length===2&&args[0]==='--evidence-dir'&&typeof args[1]==='string','BATTERY_UI_ARGUMENTS');const result=runBatteryUiNativeRecovery(args[1]);console.log(JSON.stringify({id:CASE_ID,status:result.status,positiveCases:result.positiveCases,sourceFaults:result.sourceFaults,failure:result.failure,sourceChanges:result.sourceChanges,toolChanges:result.toolChanges,tools:result.toolInputs.length,root:args[1]}));process.exitCode=result.passed?0:2;}catch(error){console.error(error.stack);process.exitCode=2;}
}
