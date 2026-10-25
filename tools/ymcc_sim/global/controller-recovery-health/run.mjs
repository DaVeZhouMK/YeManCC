import fs from 'node:fs';
import path from 'node:path';
import {isDeepStrictEqual} from 'node:util';
import {fileURLToPath} from 'node:url';
import {runProcess} from '../runner.mjs';
import {REPO,MODULE,CASE_ID,PRODUCT_INPUTS} from './adapter.mjs';
import {need,hash,bytesHash,readJson,writeJson,evidencePath,walk,captureInputs,verifyInputs,freezeProject,verifyOriginalReport,evaluatedInputs,verifyBindings,inside} from './provenance.mjs';
export function parseArgs(args){need(args.length===2&&args[0]==='--evidence-dir','RECOVERY_ARGUMENTS_INVALID');return {root:evidencePath(args[1])};}
function safeEnvironment(root,vcvars){
 const env={...process.env};
 for(const key of Object.keys(env))if(/^(?:CL|_CL_|LINK|_LINK_|NODE_OPTIONS|NODE_PATH|VSCMD_START_DIR|YMCC_VCVARS64)$/i.test(key))delete env[key];
 Object.assign(env,{CL:'/sourceDependencies .',LINK:'/verbose:lib',YMCC_VCVARS64:vcvars,TEMP:path.join(root,'temp'),TMP:path.join(root,'temp'),VSCMD_SKIP_SENDTELEMETRY:'1',VSCMD_SKIP_APPCOMPAT:'1'});
 fs.mkdirSync(env.TEMP,{recursive:true});return env;
}
export async function runRecoveryHealth(root){
 root=evidencePath(root);fs.mkdirSync(root,{recursive:true});
 const before=captureInputs(root),spec=readJson(path.join(root,'input-sources',MODULE,'spec.json')),
  project=freezeProject(root,before),steps=[],runner=path.join(project,'tools/input_host_recovery_health_selftest.mjs');
 const env=safeEnvironment(root,before.toolchain.vcvars64);
 writeJson(path.join(root,'compiler-options.json'),{CL:env.CL,LINK:env.LINK,YMCC_VCVARS64:env.YMCC_VCVARS64,MSVC:before.toolchain.msvcVersion,WindowsSDK:before.toolchain.windowsSdkVersion,ambientFlagsCleared:['CL','_CL_','LINK','_LINK_','NODE_OPTIONS','NODE_PATH'],secretsOrFullEnvironmentSaved:false});
 async function execute(label,{negative=false,negativeHeader}={}){
  const out=path.join(root,label),args=[runner,'--current',path.join(project,'native/main.cpp'),'--out',out];
  if(negative)args.push('--baseline-header',negativeHeader,'--expect-semantic-failure');
  const result=await runProcess(process.execPath,args,{cwd:project,env,timeoutMs:90000,outputLimit:2*1024*1024});
  fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'runner.stdout.log'),result.stdout);fs.writeFileSync(path.join(out,'runner.stderr.log'),result.stderr);
  const {stdout,stderr,...terminal}=result,step={label,executable:process.execPath,args,cwd:project,...terminal};steps.push(step);writeJson(path.join(root,'process-ledger.json'),{steps});
  need(!result.timedOut&&!result.launchError&&!result.signal&&!result.outputTruncated&&result.exitCode===0,'RECOVERY_RUNNER_INCOMPLETE:'+label);
  const summary=readJson(path.join(out,'summary.json'));need(summary.passed===true&&summary.deviceOrHostExecuted===false,'RECOVERY_RUNNER_SUMMARY_BOUNDARY');
  need(inside(out,summary.run),'RECOVERY_RUN_PATH_OUTSIDE_PROFILE');
  const profiles={};
  for(const suffix of ['off','on']){
   const name='current-diagnostic-'+suffix,dir=path.join(summary.run,name),build=readJson(path.join(summary.run,'build-'+name+'.command.json')),execution=readJson(path.join(summary.run,'execute-'+name+'.command.json'));
   need(build.status===0&&!build.error,'RECOVERY_NEGATIVE_COMPILE_FAILED');
   need(!execution.error,'RECOVERY_FIXTURE_NOT_EXECUTED');
   const rawFile=path.join(summary.run,name+'.results.json'),raw=readJson(rawFile),stdoutFile=path.join(dir,'execute.stdout.log');
   need(isDeepStrictEqual(raw,readJson(stdoutFile)),'RECOVERY_ORIGINAL_FAILURE_NOT_PRESERVED');
   const verdict=verifyOriginalReport(raw,spec,{negative,buildExitCode:build.status,executeExitCode:execution.status});
   need(summary.profiles[name].cases===raw.caseCount&&summary.profiles[name].failedCases===raw.failedCases,'RECOVERY_SUMMARY_COUNT_MISMATCH');
   need(summary.profiles[name].expectedSemanticFailures===negative,'RECOVERY_SEMANTIC_NEGATIVE_MODE_MISMATCH');
   const compileText=fs.readFileSync(path.join(dir,'compile.cmd'),'utf8');
   need(compileText.includes('/DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC')===(suffix==='on'),'RECOVERY_DIAGNOSTIC_SWITCH_NOT_BOUND');
   profiles[name]={...verdict,originalReport:{path:rawFile,sha256:hash(rawFile)},originalStdout:{path:stdoutFile,sha256:hash(stdoutFile)},compiledExecutable:{path:path.join(dir,'fixture.exe'),sha256:hash(path.join(dir,'fixture.exe'))},compilerInputs:evaluatedInputs(root,dir,before)};
  }
  const bindings=verifyBindings(summary.run,project,before,{negativeHeader});
  return {status:negative?'EXPECTED_SEMANTIC_FAILURE':'PASS',runnerExitCode:result.exitCode,run:summary.run,profiles,bindings,scope:summary.claim};
 }
 try{
  const positive=await execute('positive');
  const original=path.join(project,spec.negative.file),beforeBytes=fs.readFileSync(original),text=beforeBytes.toString('utf8');
  need(text.split(spec.negative.before).length===2,'RECOVERY_NEGATIVE_ANCHOR_NOT_UNIQUE');
  const changed=text.replace(spec.negative.before,spec.negative.after),fault=path.join(root,'fault-source',path.basename(original));
  fs.mkdirSync(path.dirname(fault),{recursive:true});fs.writeFileSync(fault,changed,{flag:'wx'});
  need(changed.replace(spec.negative.after,spec.negative.before)===text,'RECOVERY_FAULT_MORE_THAN_ONE_SITE');
  writeJson(path.join(root,'fault-proof.json'),{schemaVersion:1,productionSource:spec.negative.file,privateSource:fault,function:spec.negative.function,beforeSha256:bytesHash(beforeBytes),afterSha256:hash(fault),before:spec.negative.before,after:spec.negative.after,meaning:spec.negative.meaning,originalFixtureAssertionsUnchanged:true,historicalBefore:false});
  const negative=await execute('negative',{negative:true,negativeHeader:fault});
  for(const relative of PRODUCT_INPUTS)need(hash(path.join(project,relative))===before.sources.find(r=>r.path===relative).sha256,'RECOVERY_FROZEN_ORIGINAL_CHANGED:'+relative);
  verifyInputs(root,before);
  writeJson(path.join(root,'closure-verification.json'),{schemaVersion:1,sourceMembers:before.sources.length,artifactMembers:before.artifacts.length,sourceMembershipStable:true,toolMembershipStable:true,pinnedArtifactsStable:true,allFourCompilerInputLedgersVerified:true,originalFixtureAndFrozenInputsUnchanged:true,negativeSourceSites:1,negativeOriginalReportsPreserved:true,globalRegistryEdited:false});
  const files=walk(root),report={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,kind:'actual-native-recovery-health-consumer',positive,negative,
   sourceHashes:[...before.sources.map(({path,sha256})=>({path,sha256})),...files.map(file=>({path:file,sha256:hash(file)}))],
   sourceMembershipStable:true,toolMembershipStable:true,toolchain:{artifactCount:before.artifacts.length,identities:before.artifacts,compilerDependencyLedgers:4,platformBoundary:before.toolchain.osBoundary},
   executedProductModules:['native/main.cpp: inputHostRecoveryHealthContextLocked/Source/LatestSource/ObserveLocked/AcceptedLocked; original suppression/request/ACK branches; inputHostRecoveryMaintenanceTick; canonical producer continuity statements','native/input_host_recovery_health.h: Source/fresh/nextContinuity/Window'],
   inputConsumerStateFeedback:{input:'Unchanged original 47 scenarios and controlled time/power/Host/snapshot/validated ACK boundaries',consumer:'Source-exact production Context/Source/Window, request and ACK capture, source suppression and maintenance',state:'Original healthySince, attempts, source identity/continuity, gate and receipt assertions',feedback:'Both diagnostic modes pass on current helper; one private fresh() fault causes four exact original semantic assertion failures after successful compilation and fixture exit1'},
   boundary:{productMainExecuted:false,hostOrSdkExecuted:false,nativeManagedPipeExecuted:false,realReaderExecuted:false,hardwareIO:false,deviceOrRegistryActions:0,osSleepPerformed:false,recoveryActionExecuted:false,frameGuardTls:'Intentionally unbound with inert Current/Note seams',motion:'Null only; Legion seam rejects nonnull; no motion acceptance',cpuAcceptance:false,fixtureAssertionsChanged:false,productionStrategyChanged:false,globalRegistrationChanged:false},
   remainingGaps:['Actual native↔managed pipe/Host tuple verification and product full inputHostSubmitPad are not executed.','Original snapshots and validated receipt events are controlled inputs; no physical source, PnP, device operation, real self-heal action, kernel power callback or OS sleep.','This 47-case suite is not the separate kept-target 33-case full branch suite, nor repeated real sleep acceptance.','No installed build, SDK runtime, hardware performance or CPU reduction is accepted.'],fullLifecycleAccepted:false};
  writeJson(path.join(root,'result.json'),report);return report;
 }catch(error){writeJson(path.join(root,'failure.json'),{schemaVersion:1,status:'FAIL',reason:error.message,steps,originalReportsRetained:true,compilationFailureCountedAsSemanticRejection:false});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const {root}=parseArgs(process.argv.slice(2)),result=await runRecoveryHealth(root);console.log(JSON.stringify({id:result.id,status:result.status,positive:result.positive.profiles,negative:result.negative.profiles,sourceHashes:result.sourceHashes.length}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
