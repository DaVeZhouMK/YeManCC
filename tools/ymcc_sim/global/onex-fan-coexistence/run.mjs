import fs from 'node:fs';
import {prepareParentBuild,verifyPreparedParentBuild,claimPreparedParentBuild,completePreparedParentBuild,replayPreparedParentCompile,verifyPreparedParentCompileReplay,PARENT_BUDGET_MS} from './parent-build-handoff.mjs';
import {stageFaultPayload} from './fault-payload-staging.mjs';
import {installToolPolicyPlan} from './tool-policy-runtime.mjs';
import {prepareFirstInvocation,validatePrepareOptions} from './prepare-first-invocation.mjs';

import path from 'node:path';

import crypto from 'node:crypto';

import { fileURLToPath } from 'node:url';

import { spawnSync } from 'node:child_process';

import { generateFixture } from './generate.mjs';
import { runSourceContracts, sourceContractInputs } from './source-contracts.mjs';
import { runFactoryProof } from './factory-proof.mjs';
import { runStaProof } from './sta-proof.mjs';
import { runValidationProof } from './validation-proof.mjs';
import { stageDeclaredHostPayload, verifyDeclaredHostPayload } from './payload-staging.mjs';
import { runInstallerProof } from './installer-proof.mjs';
import { runCapabilityAclProof } from './capability-acl-proof.mjs';
import { runBootstrapProof } from './bootstrap-proof.mjs';
import { runConstructorProof } from './constructor-proof.mjs';
import { runJoinedFactoryProof } from './joined-factory-proof.mjs';
import { runPublicFactoryProof } from './public-factory-proof.mjs';
import { runOriginalLifecycleProof } from './lifecycle-proof.mjs';
import { runOriginalMsiLifecycleProof } from './msi-lifecycle-proof.mjs';
import { runOriginalMsiManualProof } from './msi-manual-proof.mjs';
import { runOriginalMsiReplayProof } from './msi-replay-proof.mjs';

import { parseArgs, prepareFreshEvidence, resolveInputs, resolveWriteBaseline, BlockedInputError, baselineRelativeRoot } from './inputs.mjs';

export { getOneXFanCoexistenceAdapters } from './adapter.mjs';

const moduleRoot = path.dirname(fileURLToPath(import.meta.url));

const defaultRepo = path.resolve(moduleRoot, '../../../..');

const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));

const tree = dir => !fs.existsSync(dir) ? [] : fs.readdirSync(dir, { withFileTypes: true }).flatMap(e => e.isDirectory()

  ? ['obj','bin','deps'].includes(e.name) ? [] : tree(path.join(dir,e.name)) : [path.join(dir,e.name)]);

const find = (dir, name) => {

  const files=tree(dir).filter(f=>path.basename(f)===name && !/[\\/]ref(int)?[\\/]/.test(f));

  if(files.length !== 1) throw Error(`Expected one ${name}: ${files.length}`);

  return files[0];

};

export function resolveProofRuntimeSelection({repoRoot,candidateHostRoot,candidatePayloadBinding,hcAssembly}) {
  const sharedRuntimeRoot=path.resolve(repoRoot,'PowerControl/ymcc-shared-runtime');
  if(!candidateHostRoot) {
    if(candidatePayloadBinding)throw Error('Explicit payload binding requires candidateHostRoot');
    return {declaredCandidate:null,sharedRuntimeRoot};
  }
  const root=path.resolve(candidateHostRoot);
  if(!candidatePayloadBinding)throw new BlockedInputError('ONEX_CANDIDATE_BINDING_MISSING','Explicit candidate payload requires its manifest, candidate, runtime and shared SHA bindings',{root});
  const declared=verifyDeclaredHostPayload({...candidatePayloadBinding,payloadRoot:root});
  if(path.resolve(declared.runtimeRoot).toLowerCase()!==path.resolve(path.dirname(hcAssembly)).toLowerCase())throw Error('Declared runtime must equal the actual proof HC selection');
  return {declaredCandidate:declared,sharedRuntimeRoot:declared.sharedRuntimeRoot};
}

export function execute(options) {
  const executionStartedMs=Date.now();
  if(options.prepareOnly&&options.preparedParentBuild)throw Error('Parent build resume cannot use prepare-only');
  if(options.parentBuildRequest&&!options.prepareParentBuildOnly)throw Error('Parent build request requires preparation mode');

  const repo=path.resolve(options.repoRoot??defaultRepo);
  if(options.candidatePayloadBinding&&!options.candidateHostRoot)throw Error('Explicit payload binding requires candidateHostRoot');

  const here=path.join(repo,'tools/ymcc_sim/global/onex-fan-coexistence');

  if(options.prepareOnly===true)validatePrepareOptions(options);
  if(options.prepareParentBuildOnly===true){if(options.prepareOnly||options.preparedParentBuild)throw Error('Parent build preparation cannot execute or resume');return prepareParentBuild({repoRoot:repo,evidenceDir:options.evidenceDir,requestPin:{path:options.parentBuildRequest,sha256:options.parentBuildRequestSha256}});}
  let preparedContext=options.preparedParentBuild?verifyPreparedParentBuild({repoRoot:repo,manifestPin:{path:options.preparedParentBuild,sha256:options.preparedParentBuildSha256},toolLoadPlan:options.toolLoadPlan,toolLoadPlanSha256:options.toolLoadPlanSha256,executionStartedMs}):null;
  if(preparedContext&&path.resolve(options.evidenceDir)!==preparedContext.evidenceDir)throw Error('PARENT_BUILD_EVIDENCE_DIRECTORY_MISMATCH');
  const out=preparedContext?preparedContext.evidenceDir:prepareFreshEvidence(options.evidenceDir);
  if(options.prepareOnly===true)return prepareFirstInvocation({...options,evidenceDir:out}).then(result=>{fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;});
  let toolGate;
  try { toolGate=installToolPolicyPlan({...options,repoRoot:repo}); }
  catch(error) {
    const blocked={schemaVersion:1,kind:'onex-fan-lighting-coexistence',status:'BLOCKED',code:error.code??'ONEX_TOOL_LOAD_PLAN_INVALID',error:error.message,detail:error.detail??null,toolPolicyPreflight:true,producerEntryExecuted:false,hardwareIo:false,hostMainStarted:false,installedApplicationMutated:false,fullProductAcceptance:false};
    if(preparedContext){const refusal=path.join(out,'parent-build-preparation','policy-refusal-'+crypto.randomUUID()+'.json');fs.writeFileSync(refusal,JSON.stringify(blocked,null,2),{flag:'wx'});blocked.preparedParentRefusal={path:refusal,sha256:hash(refusal)};}else fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(blocked,null,2));return blocked;
  }

  const sourceFiles=[...tree(path.join(repo,'FanLab/real-host')),...tree(path.join(repo,'FanLab/fan-backend')),...tree(here),

    ...['native/ProcessEfficiency.cs','FanLab/DeviceCoreSharedDependencies.cs','FanLab/RuntimeDependencyContract.cs',
      'PowerControl/ymcc-shared-runtime/Newtonsoft.Json.dll','PowerControl/ymcc-shared-runtime/manifest.json',

      'tools/ymcc_sim/global/fan-open-signals/extract.mjs','tools/ymcc_sim/global/fan-open-signals/inputs.mjs',

      'tools/hc-application-dependency-trim/ApplicationTrim.csproj'].map(f=>path.join(repo,f))]

    .filter(f=>fs.existsSync(f)&&/\.(cs|csproj|props|mjs|json|md|dll|ps1)$/.test(f));

  sourceFiles.push(...['src/bridge/fanHost.ts','src/bridge/fanApi.ts','tools/fan_control_self_heal_selftest.ts'].map(file=>path.join(repo,file)));
  sourceFiles.push(...['fan_hc_enable_alignment_selftest.ps1','fan_hc_lifecycle_order_audit.ps1','fan_hc_lifecycle_deep_selftest.ps1'].map(file=>path.join(repo,'tools',file)));

  const sourceContractMembers=sourceContractInputs(repo);
  sourceFiles.push(...sourceContractMembers.map(file=>path.join(repo,file)));
  const before=Object.fromEntries([...new Set(sourceFiles)].map(f=>[path.relative(repo,f).replaceAll('\\','/'),hash(f)]));

  const external=[];let result;let inputEvidence={};let candidateBefore=null;

  const run=(exe,args,log,timeout=120000,expectedExit=0)=>{

    const replay=preparedContext?replayPreparedParentCompile(preparedContext,args):null;
    const routed=toolGate.route(exe,replay?.args??args,out);
    const remaining=preparedContext?PARENT_BUDGET_MS-preparedContext.preparedDurationMs-(Date.now()-preparedContext.startedMs):timeout;
    if(remaining<=0)throw Object.assign(Error('PARENT_BUILD_PARENT_BUDGET_EXHAUSTED'),{code:'PARENT_BUILD_PARENT_BUDGET_EXHAUSTED'});
    const childStartedMs=Date.now();const child=spawnSync(exe,routed.args,{cwd:replay?.cwd??repo,...(replay?{env:replay.env}:{}),windowsHide:true,encoding:'utf8',timeout:Math.min(timeout,remaining),maxBuffer:6*1024*1024});
    if(replay)verifyPreparedParentCompileReplay(preparedContext,replay,{...child,args:routed.args,durationMs:Date.now()-childStartedMs});

    fs.writeFileSync(path.join(out,log),(child.stdout??'')+'\n'+(child.stderr??'')+'\n'+(child.error?.message??''));

    if(child.error?.code==='ENOENT')throw new BlockedInputError('ONEX_DOTNET_MISSING','Required local dotnet runtime/toolchain is missing',{executable:exe});

    if(child.status!==expectedExit)throw Error(`${log}: exit=${child.status} ${child.error?.message??''}`);
    toolGate.verifyAfter(routed.policyReceipt);

  };

  try {

    if(preparedContext&&options.cecilPath!==undefined&&path.resolve(options.cecilPath).toLowerCase()!==path.resolve(preparedContext.manifest.cecil.path).toLowerCase())throw Object.assign(Error('PARENT_BUILD_CECIL_PATH_MISMATCH'),{code:'PARENT_BUILD_CECIL_PATH_MISMATCH'});
    inputEvidence=resolveInputs({...options,repoRoot:repo,...(preparedContext?{cecilPath:preparedContext.manifest.cecil.path}:{})});
    if(preparedContext)preparedContext=claimPreparedParentBuild(preparedContext);

    const {baseline,hc,cecil}=inputEvidence;const fixed=baseline.root;
    const runtimeSelection=resolveProofRuntimeSelection({repoRoot:repo,candidateHostRoot:options.candidateHostRoot,candidatePayloadBinding:options.candidatePayloadBinding,hcAssembly:hc.path});
    const sharedJson=path.join(runtimeSelection.sharedRuntimeRoot,'Newtonsoft.Json.dll');
    const sharedJsonManifest=path.join(runtimeSelection.sharedRuntimeRoot,'manifest.json');
    const sharedJsonPin='a28c251dfe36d881e9e2462e171441b8b0ec156fe3f452602c9149b1b9efe05b';
    const sharedJsonRecord=read(sharedJsonManifest).files?.filter(file=>file.packagedPath==='Newtonsoft.Json.dll');
    if(!fs.existsSync(sharedJson)||hash(sharedJson)!==sharedJsonPin||sharedJsonRecord?.length!==1||sharedJsonRecord[0].sha256!==sharedJsonPin||sharedJsonRecord[0].version!=='13.0.4'||sharedJsonRecord[0].nugetBinaryPath!=='lib/net6.0/Newtonsoft.Json.dll')throw Object.assign(Error('Shared Newtonsoft must match the byte-identical locked NuGet original; no new trim/rewrite is admitted'),{code:'ONEX_SHARED_JSON_IDENTITY'});
    inputEvidence.sharedJson={path:sharedJson,sha256:sharedJsonPin,sourceManifest:sharedJsonManifest,sourceManifestSha256:hash(sharedJsonManifest),version:'13.0.4',nugetBinaryPath:'lib/net6.0/Newtonsoft.Json.dll'};
    external.push({path:sharedJson,sha256:sharedJsonPin},{path:sharedJsonManifest,sha256:hash(sharedJsonManifest)});

    external.push(...baseline.files,{path:baseline.manifestPath,sha256:baseline.manifestSha256},hc,{path:cecil.path,sha256:cecil.sha256});

    if(options.candidateHostRoot) {

      const root=path.resolve(options.candidateHostRoot);

      if(!options.candidatePayloadBinding)throw new BlockedInputError('ONEX_CANDIDATE_BINDING_MISSING','Explicit candidate payload requires its manifest, candidate, runtime and shared SHA bindings',{root});
      if(options.candidatePayloadBinding) {
        const declared=runtimeSelection.declaredCandidate;
        if(!declared.runtimeMembers.some(row=>path.basename(row.path)==='HandheldCompanion.dll'&&row.sha256===hc.sha256))throw Error('Declared runtime does not contain the exact admitted HC');
        if(!declared.sharedMembers.some(row=>path.basename(row.path)==='Newtonsoft.Json.dll'&&row.sha256===sharedJsonPin))throw Error('Declared shared runtime differs from the locked JSON reference');
        if(path.resolve(declared.runtimeRoot).toLowerCase()!==path.resolve(path.dirname(hc.path)).toLowerCase()||path.resolve(declared.sharedRuntimeRoot).toLowerCase()!==path.resolve(runtimeSelection.sharedRuntimeRoot).toLowerCase())throw Error('Declared runtime/shared roots must equal the actual proof runtime/shared selection');
        inputEvidence.declaredCandidatePayload=declared;external.push(...declared.inputPins);
      }

      for(const name of ['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','YeManFanHost.deps.json','YeManFanHost.runtimeconfig.json','YeManFanHost.json','YeManFanHost.payload.json']) {

        if(!fs.existsSync(path.join(root,name)))throw new BlockedInputError('ONEX_CANDIDATE_MISSING','Explicit candidate payload input is missing',{path:path.join(root,name)});

      }

      candidateBefore=tree(root).map(file=>({path:file,sha256:hash(file)}));external.push(...candidateBefore);

    }

    const writeBaseline=resolveWriteBaseline(repo);inputEvidence.writeBaseline=writeBaseline;

    external.push(...writeBaseline.files,{path:writeBaseline.manifestPath,sha256:writeBaseline.manifestSha256});

    const generated=path.join(out,'generated');const fixture=generateFixture(repo,generated,path.join(here,'Fixture.template.cs'));

    const testBuild=path.join(out,'test-build');

    const buildArgs=['-c','Release','--nologo','-v:q','-m:1','-p:UseSharedCompilation=false','-nodeReuse:false'];

    run('dotnet',['build',path.join(here,'Harness.csproj'),'--artifacts-path',testBuild,'-p:FixtureSource='+path.join(generated,'Fixture.g.cs'),'-p:CecilPath='+cecil.path,...buildArgs],'harness-build.log');

    for(const kind of ['STA','MTA'])run('dotnet',['build',path.join(here,'load-authority',kind+'.csproj'),'--artifacts-path',testBuild,...buildArgs],kind.toLowerCase()+'-policy-launcher-build.log');
    const harness=find(path.join(testBuild,'bin'),'OneXFanCoexistence.dll');
    const compiledJson=tree(path.join(testBuild,'bin')).filter(file=>path.basename(file)==='Newtonsoft.Json.dll');
    if(!compiledJson.length||compiledJson.some(file=>hash(file)!==sharedJsonPin))throw Error('Compiled fixture JSON dependency drifted from the locked original');

    run('dotnet',[harness,'old',path.join(fixed,'YeManFanHost.dll'),path.join(fixed,'YeManLightSetter.dll'),hc.path,path.join(out,'old-binary-result.json')],'old-binary.log',45000);

    const old=read(path.join(out,'old-binary-result.json'));

    if(old.status!=='DEFECT_REPRODUCED_IN_MEMORY'||old.nativeAttempts!==0||old.cases.length<8||old.checks.length<63||old.checks.some(c=>!c.passed))throw Error('Exact old defects did not reproduce');

    run('dotnet',[harness,'old-write',path.join(writeBaseline.root,'YeManFanHost.dll'),hc.path,path.join(out,'old-write-result.json')],'old-write.log',45000);

    const oldWrite=read(path.join(out,'old-write-result.json'));

    if(oldWrite.status!=='DEFECT_REPRODUCED_IN_MEMORY'||oldWrite.cases.length!==6||oldWrite.goldenWrites.length!==9||oldWrite.checks.some(c=>!c.passed)||oldWrite.nativeAttempts!==0)throw Error('Prior actual write/monitor defects did not reproduce');

    let payload=options.candidateHostRoot?path.resolve(options.candidateHostRoot):null;

    if(!payload) {

      const build=path.join(out,'host-build');

      run('dotnet',['build',path.join(repo,'FanLab/real-host/YeManFanHost.csproj'),'--artifacts-path',build,...buildArgs],'host-build.log');

      const dll=find(path.join(build,'bin'),'YeManFanHost.dll');payload=path.join(out,'host-payload');
      const staging=stageDeclaredHostPayload({buildRoot:path.dirname(dll),templateRoot:path.join(repo,'PowerControl/fan-host'),targetRoot:payload});
      external.push(...staging.inputPins);fs.writeFileSync(path.join(out,'candidate-staging.json'),JSON.stringify(staging,null,2));

      candidateBefore=tree(payload).map(file=>({path:file,sha256:hash(file)}));external.push(...candidateBefore);

    }

    const candidate=path.join(payload,'YeManFanHost.dll');

    run('dotnet',[harness,candidate,path.join(fixed,'YeManLightSetter.dll'),hc.path,path.join(out,'candidate-result.json')],'candidate.log',60000);

    const behavior=read(path.join(out,'candidate-result.json'));

    const initializationIds=['initialization-original-methods-match-pinned-pe','initialization-actual-factory-stage-wiring','initialization-actual-callback-arguments-and-virtual-dispatch',
      ...['IDevice','AYANEOFlipDS','AYANEOFlip1SDS'].map(name=>'initialization-pinned-no-first-start:'+name),
      'initialization-first-start-leaf-is-observable','initialization-original-failure-is-preserved:False','initialization-original-failure-is-preserved:True'];
    const initializationCases=behavior.cases.filter(c=>c.id.startsWith('initialization-'));
    if(initializationCases.length!==initializationIds.length||initializationIds.some(id=>!initializationCases.some(c=>c.id===id&&c.status==='PASS')))throw Error('Compiled initialization consumer cases are missing or failed');
    const initializationFaultRoot=path.join(out,'private-initialization-fault');stageFaultPayload(payload,initializationFaultRoot);
    const initializationFaultHost=path.join(initializationFaultRoot,'YeManFanHost.dll'),initializationFaultProofPath=path.join(out,'private-initialization-fault-proof.json');
    run('dotnet',[harness,'fault-initialize-first-start',candidate,initializationFaultHost,initializationFaultProofPath],'private-initialization-fault-generation.log',45000);
    const initializationMutation=read(initializationFaultProofPath);
    if(initializationMutation.status!=='PASS'||initializationMutation.sourceSha256!==hash(candidate)||initializationMutation.faultSha256!==hash(initializationFaultHost)||initializationMutation.changedMethods.length!==1||!initializationMutation.changedMethods[0].includes('LoadAssemblyAndFactory'))throw Error('Initialization fault is not bound to exactly the wired factory callback');
    const initializationFaultResultPath=path.join(out,'private-initialization-fault-result.json');
    run('dotnet',[harness,initializationFaultHost,path.join(fixed,'YeManLightSetter.dll'),hc.path,initializationFaultResultPath],'private-initialization-fault-run.log',60000,2);
    const initializationFaultResult=read(initializationFaultResultPath),initializationFailures=initializationFaultResult.cases.filter(c=>c.status==='FAIL');
    const expectedInitializationFaultIds=['initialization-actual-callback-arguments-and-virtual-dispatch','initialization-pinned-no-first-start:AYANEOFlipDS','initialization-pinned-no-first-start:AYANEOFlip1SDS'];
    if(initializationFaultResult.status!=='FAIL'||initializationFaultResult.nativeAttempts!==0||initializationFaultResult.hostMainStarted!==false||initializationFaultResult.cases.length!==behavior.cases.length||JSON.stringify(initializationFailures.map(c=>c.id).sort())!==JSON.stringify(expectedInitializationFaultIds.sort())||initializationFailures.some(c=>!c.error?.includes('Actual factory callback must dispatch Initialize(false,false) exactly once')&&!c.error?.includes('Host handshake cannot activate pinned FirstStart settings')))throw Error('Compiled initialization fault did not fail exactly the three argument/first-start consumers');
    const initializationProof={status:'PASS',positiveCases:initializationIds.length,positiveIds:initializationIds,exactCompiledFault:initializationMutation,expectedFaultIds:expectedInitializationFaultIds,actualFailureCount:initializationFailures.length,faultResultPath:initializationFaultResultPath,faultProofPath:initializationFaultProofPath,actualCompiledFactoryDelegateAndStageExecuted:true,fullFactoryAssemblyLoadingAndValidationExecuted:false,hardwareIo:false};
    for(const file of [...tree(initializationFaultRoot),initializationFaultProofPath,initializationFaultResultPath,path.join(out,'private-initialization-fault-generation.log'),path.join(out,'private-initialization-fault-run.log')])external.push({path:file,sha256:hash(file)});
    const factory=runFactoryProof({out,harness,candidate,payload,helper:path.join(fixed,'YeManLightSetter.dll'),hc:hc.path,run});
    const factoryInitializationProof=factory.proof;external.push(...factory.evidence);
    const sta=runStaProof({out,harness,candidate,payload,helper:path.join(fixed,'YeManLightSetter.dll'),hc:hc.path,run});
    const staBootstrapProof=sta.proof;external.push(...sta.evidence);
    const validation=runValidationProof({out,harness,candidate,payload,helper:path.join(fixed,'YeManLightSetter.dll'),hc:hc.path,run});
    const deploymentValidationProof=validation.proof;external.push(...validation.evidence,...validation.external);
    const installer=runInstallerProof({repo,out,run});
    const installerAdmissionProof=installer.proof;external.push(...installer.evidence,...installer.inputs);
    const capabilityAcl=runCapabilityAclProof({out,harness,candidate,payload,helper:path.join(fixed,'YeManLightSetter.dll'),hc:hc.path,run});
    const formalCapabilityAclProof=capabilityAcl.proof;external.push(...capabilityAcl.evidence);
    const bootstrap=runBootstrapProof({repo,out,harness,candidate,payload,helper:path.join(fixed,'YeManLightSetter.dll'),hc:hc.path,run,preparedContext});
    const originalBootstrapProof=bootstrap.proof;external.push(...bootstrap.evidence);
    const constructors=runConstructorProof({repo,out,harness,candidate,hc:hc.path,run});
    const originalConstructorProof=constructors.proof;external.push(...constructors.evidence,...constructors.inputs);
    const joined=runJoinedFactoryProof({repo,out,harness,candidate,hc:hc.path,run,preparedContext});
    const joinedFactoryProof=joined.proof;external.push(...joined.evidence);
    const publicFactory=runPublicFactoryProof({repo,out,harness,candidate,hc:hc.path,run});
    const publicFactoryProof=publicFactory.proof;external.push(...publicFactory.evidence);
    const originalLifecycle=runOriginalLifecycleProof({repo,out,harness,candidate,hc:hc.path,run});
    const originalLifecycleProof=originalLifecycle.proof;external.push(...originalLifecycle.evidence);
    const originalMsiLifecycle=runOriginalMsiLifecycleProof({repo,out,harness,candidate,hc:hc.path,run});
    const originalMsiLifecycleProof=originalMsiLifecycle.proof;external.push(...originalMsiLifecycle.evidence);
    const originalMsiManual=runOriginalMsiManualProof({repo,out,harness,candidate,hc:hc.path,run});
    const originalMsiManualProof=originalMsiManual.proof;external.push(...originalMsiManual.evidence);
    const originalMsiReplay=runOriginalMsiReplayProof({repo,out,harness,candidate,hc:hc.path,run});
    const originalMsiReplayProof=originalMsiReplay.proof;external.push(...originalMsiReplay.evidence);
    const sourceContractRoot=path.join(out,'source-contracts');
    const sourceContractProof=runSourceContracts({repo,out:sourceContractRoot,run});
    for(const file of tree(sourceContractRoot))external.push({path:file,sha256:hash(file)});
    const mixedIds=['mixed-events-full-original-methods-match-pe',
      ...Array.from({length:8},(_,mask)=>[false,true].map(ready=>'mixed-events-initial-state:'+mask+':'+(ready?'True':'False'))).flat(),
      ...['settings','device','profile','system'].map(name=>'mixed-events-late-initialization:'+name),
      'mixed-events-original-device-filter-and-unsubscribe','mixed-events-original-controller-settings-chain','mixed-events-original-battery-threshold-chain','mixed-events-original-system-power-and-lock-chain','mixed-events-close-blocks-all-late-initialization','mixed-events-repeated-host-entry-is-idempotent','mixed-events-settings-exception-retains-close-owner',
      'mixed-events-partial-entry-requires-close-before-retry','mixed-events-superseded-finalize-does-not-repeat-original-entry',
      'mixed-events-complete-close-reopen-starts-new-entry','mixed-events-throwing-close-retains-raw-entry-owner','mixed-events-late-close-exception-retains-debt-until-return',...['settings','device','profile'].map(name=>'mixed-events-combined-manager-flags-retain-cleanup:'+name),...['Halting, Busy','Halted, Busy'].map(status=>'mixed-events-faulted-manager-flags-retain-cleanup:'+status),...['settings','device','profile'].map(name=>'mixed-events-combined-flags-reach-public-cleanup:'+name)];
    const mixedCases=behavior.cases.filter(c=>c.id.startsWith('mixed-events-'));
    if(mixedCases.length!==mixedIds.length||mixedIds.some(id=>!mixedCases.some(c=>c.id===id&&c.status==='PASS'))||behavior.cases.some(c=>c.mixedSubscriptions&&Object.values(c.mixedSubscriptions).some(count=>count!==0)))throw Error('Original mixed lifecycle cases or subscription settlement are incomplete');
    const mixedPositivePath=path.join(out,'mixed-reentry-positive.json');
    run('dotnet',[harness,'mixed-reentry',candidate,path.join(fixed,'YeManLightSetter.dll'),hc.path,mixedPositivePath],'mixed-reentry-positive.log',30000);
    const mixedPositive=read(mixedPositivePath);
    const mixedFaultRoot=path.join(out,'private-mixed-reentry-fault');stageFaultPayload(payload,mixedFaultRoot);
    const mixedFaultHost=path.join(mixedFaultRoot,'YeManFanHost.dll'),mixedFaultProofPath=path.join(out,'private-mixed-reentry-fault-proof.json');
    run('dotnet',[harness,'fault-mixed-reentry',candidate,mixedFaultHost,mixedFaultProofPath],'private-mixed-reentry-fault-generation.log',30000);
    const mixedMutation=read(mixedFaultProofPath);
    if(mixedMutation.status!=='PASS'||mixedMutation.sourceSha256!==hash(candidate)||mixedMutation.faultSha256!==hash(mixedFaultHost)||mixedMutation.changedMethods.length!==1||!mixedMutation.changedMethods[0].includes('::OpenEventsBeginCore('))throw Error('Mixed fault changed more than its existing once guard');
    const mixedFaultResultPath=path.join(out,'private-mixed-reentry-fault-result.json');
    run('dotnet',[harness,'mixed-reentry',mixedFaultHost,path.join(fixed,'YeManLightSetter.dll'),hc.path,mixedFaultResultPath],'private-mixed-reentry-fault-run.log',30000,2);
    const mixedFault=read(mixedFaultResultPath),mixedFailures=mixedFault.cases.filter(c=>c.status==='FAIL');
    const mixedFailureIds=['mixed-events-partial-entry-requires-close-before-retry','mixed-events-superseded-finalize-does-not-repeat-original-entry'];
    if(mixedPositive.status!=='PASS'||mixedPositive.cases.length!==2||mixedPositive.nativeAttempts!==0||mixedFault.status!=='FAIL'||mixedFault.nativeAttempts!==0||mixedFault.hostMainStarted!==false||JSON.stringify(mixedPositive.cases.map(c=>c.id).sort())!==JSON.stringify(mixedFault.cases.map(c=>c.id).sort())||JSON.stringify(mixedFailures.map(c=>c.id).sort())!==JSON.stringify(mixedFailureIds.sort())||mixedFailures.some(c=>!c.error?.includes('A partial original mixed entry must require same-owner Close before retry')&&!c.error?.includes('Original mixed subscriptions cannot be repeated after superseded Host finalization')))throw Error('Mixed compiled fault did not fail exactly the same two reentry consumers');
    const mixedEventsProof={status:'PASS',positiveCases:mixedIds.length,positiveIds:mixedIds,fullOriginalMethods:22,completeOriginalManagerEnum:true,actualOriginalSettingsDeviceBatteryInputCallbacks:true,fullManagerImplementationsExecuted:false,exactCompiledFault:mixedMutation,expectedFaultIds:mixedFailureIds,actualFailureCount:mixedFailures.length,pairedReentryCases:2,positiveReentryReceipt:mixedPositivePath,faultResultPath:mixedFaultResultPath,faultProofPath:mixedFaultProofPath,hardwareIo:false};
    for(const file of [...tree(mixedFaultRoot),mixedFaultProofPath,mixedFaultResultPath,mixedPositivePath,path.join(out,'mixed-reentry-positive.log'),path.join(out,'private-mixed-reentry-fault-generation.log'),path.join(out,'private-mixed-reentry-fault-run.log')])external.push({path:file,sha256:hash(file)});
    const flagPositivePath=path.join(out,'mixed-flags-positive.json');
    run('dotnet',[harness,'mixed-flags',candidate,path.join(fixed,'YeManLightSetter.dll'),hc.path,flagPositivePath],'mixed-flags-positive.log',30000);
    const flagPositive=read(flagPositivePath);
    const flagFaultRoot=path.join(out,'private-manager-flags-fault');stageFaultPayload(payload,flagFaultRoot);
    const flagFaultHost=path.join(flagFaultRoot,'YeManFanHost.dll'),flagFaultProofPath=path.join(out,'private-manager-flags-fault-proof.json');
    run('dotnet',[harness,'fault-mixed-manager-flags',candidate,flagFaultHost,flagFaultProofPath],'private-manager-flags-fault-generation.log',30000);
    const flagMutation=read(flagFaultProofPath);
    if(flagMutation.status!=='PASS'||flagMutation.sourceSha256!==hash(candidate)||flagMutation.faultSha256!==hash(flagFaultHost)||flagMutation.changedMethods.length!==1||!flagMutation.changedMethods[0].includes('::HasManagerStatusFlag('))throw Error('Manager flag fault changed more than the original flag consumer');
    const flagFaultResultPath=path.join(out,'private-manager-flags-fault-result.json');
    run('dotnet',[harness,'mixed-flags',flagFaultHost,path.join(fixed,'YeManLightSetter.dll'),hc.path,flagFaultResultPath],'private-manager-flags-fault-run.log',30000,2);
    const flagFault=read(flagFaultResultPath),flagFailures=flagFault.cases.filter(c=>c.status==='FAIL');
    const flagIds=[...['settings','device','profile'].map(name=>'mixed-events-combined-manager-flags-retain-cleanup:'+name),...['Halting, Busy','Halted, Busy'].map(status=>'mixed-events-faulted-manager-flags-retain-cleanup:'+status),...['settings','device','profile'].map(name=>'mixed-events-combined-flags-reach-public-cleanup:'+name)];
    if(flagPositive.status!=='PASS'||flagPositive.cases.length!==8||flagPositive.nativeAttempts!==0||flagFault.status!=='FAIL'||flagFault.nativeAttempts!==0||flagFault.hostMainStarted!==false||JSON.stringify(flagPositive.cases.map(c=>c.id).sort())!==JSON.stringify(flagFault.cases.map(c=>c.id).sort())||JSON.stringify(flagFailures.map(c=>c.id).sort())!==JSON.stringify(flagIds.sort())||flagFailures.some(c=>!c.error?.includes('Expected HC_CLOSE_PENDING')))throw Error('Manager flag compiled fault did not fail exactly the same eight backend/engine/public cleanup consumers');
    const managerFlagsProof={status:'PASS',pairedCases:8,positiveReceipt:flagPositivePath,exactCompiledFault:flagMutation,expectedFaultIds:flagIds,actualFailureCount:flagFailures.length,faultResultPath:flagFaultResultPath,faultProofPath:flagFaultProofPath,hardwareIo:false};
    for(const file of [...tree(flagFaultRoot),flagFaultProofPath,flagFaultResultPath,flagPositivePath,path.join(out,'mixed-flags-positive.log'),path.join(out,'private-manager-flags-fault-generation.log'),path.join(out,'private-manager-flags-fault-run.log')])external.push({path:file,sha256:hash(file)});
    const genericIds=['generic-ec-frozen-write-contract','generic-ec-rejected-duty','generic-ec-rejected-primed-duty','generic-ec-rejected-software-profile','generic-ec-rejected-hardware-profile','generic-ec-rejected-transport-exit','generic-ec-original-rounding-and-curve-scale','generic-ec-original-nonzero-minimum-and-255-scale','generic-ec-superseded-before','generic-ec-superseded-during','generic-ec-metadata-and-owned-readiness-cannot-drift','generic-ec-cache-and-owner-reference-do-not-authorize-retired-write','generic-ec-vendor-overrides-retain-their-consumers','generic-ec-lost-or-replaced-transport-never-reopens-driver','generic-ec-temperature-failure-reaches-existing-engine-recovery'];
    const generic=behavior.cases.filter(c=>c.id.startsWith('generic-ec-'));
    if(generic.length!==genericIds.length||genericIds.some(id=>!generic.some(c=>c.id===id&&c.status==='PASS')))throw Error('Inherited EC production consumer cases are missing or failed');
    const faultRoot=path.join(out,'private-bool-fault');stageFaultPayload(payload,faultRoot);
    const faultHost=path.join(faultRoot,'YeManFanHost.dll');
    const faultProofPath=path.join(out,'private-bool-fault-proof.json');
    run('dotnet',[harness,'fault-ec-bool',candidate,faultHost,faultProofPath],'private-bool-fault-generation.log');
    const faultProof=read(faultProofPath);
    if(faultProof.status!=='PASS'||faultProof.sourceSha256!==hash(candidate)||faultProof.faultSha256!==hash(faultHost)||faultProof.changedMethods.length!==1)throw Error('Exact compiled bool fault identity is invalid');
    const faultResultPath=path.join(out,'private-bool-fault-result.json');
    run('dotnet',[harness,faultHost,path.join(fixed,'YeManLightSetter.dll'),hc.path,faultResultPath],'private-bool-fault-run.log',60000,2);
    const faultResult=read(faultResultPath),faultFailures=faultResult.cases.filter(c=>c.status==='FAIL');
    const expectedFaultIds=['generic-ec-rejected-duty','generic-ec-rejected-primed-duty','generic-ec-rejected-software-profile','generic-ec-rejected-hardware-profile','generic-ec-temperature-failure-reaches-existing-engine-recovery'];
    if(faultResult.status!=='FAIL'||faultResult.nativeAttempts!==0||faultResult.hostMainStarted!==false||faultResult.cases.length!==behavior.cases.length||JSON.stringify(faultFailures.map(c=>c.id).sort())!==JSON.stringify(expectedFaultIds.sort())||faultFailures.some(c=>!c.error?.includes('Expected EC fan write was rejected')&&!c.error?.includes('Current temperature producer must report rejected inherited EC write')))throw Error('Compiled bool fault did not fail the exact inherited EC business assertions');
    const inheritedEcProof={status:'PASS',positiveCases:genericIds.length,positiveIds:genericIds,exactCompiledFault:faultProof,expectedFaultIds,actualFailureCount:faultFailures.length,faultResultPath,faultProofPath,hardwareIo:false};
    for(const file of [faultHost,faultProofPath,faultResultPath,path.join(out,'private-bool-fault-generation.log'),path.join(out,'private-bool-fault-run.log')])external.push({path:file,sha256:hash(file)});
    const closeIds=['generic-close-full-frozen-contract','generic-close-source-order-unsubscribes-all',
      'generic-close-rejected-release-keeps-debt:suspend','generic-close-rejected-release-keeps-debt:process',
      'generic-close-no-write-history-keeps-honest-close','generic-close-rgb-failure-retains-owner',
      'generic-close-dispose-failure-retains-owner','generic-close-closed-event-failure-never-fabricates-release'];
    const closeCases=behavior.cases.filter(c=>c.id.startsWith('generic-close-'));
    if(closeCases.length!==closeIds.length||closeIds.some(id=>!closeCases.some(c=>c.id===id&&c.status==='PASS')))throw Error('Complete original Close consumers are missing or failed');
    const closeFaultRoot=path.join(out,'private-close-fault');stageFaultPayload(payload,closeFaultRoot);
    const closeFaultHost=path.join(closeFaultRoot,'YeManFanHost.dll');
    const closeFaultProofPath=path.join(out,'private-close-fault-proof.json');
    run('dotnet',[harness,'fault-close-release',candidate,closeFaultHost,closeFaultProofPath],'private-close-fault-generation.log',45000);
    const closeFaultMutation=read(closeFaultProofPath);
    if(closeFaultMutation.status!=='PASS'||closeFaultMutation.sourceSha256!==hash(candidate)||closeFaultMutation.faultSha256!==hash(closeFaultHost)||closeFaultMutation.changedMethods.length!==1||!closeFaultMutation.changedMethods[0].includes('CloseHcSessionForLifecycle'))throw Error('Close fault is not bound to this exact tested Host method');
    const closeFaultResultPath=path.join(out,'private-close-fault-result.json');
    run('dotnet',[harness,closeFaultHost,path.join(fixed,'YeManLightSetter.dll'),hc.path,closeFaultResultPath],'private-close-fault-run.log',60000,2);
    const closeFaultResult=read(closeFaultResultPath),closeFailures=closeFaultResult.cases.filter(c=>c.status==='FAIL');
    const expectedCloseFaultIds=['generic-close-rejected-release-keeps-debt:process','generic-close-rejected-release-keeps-debt:suspend'];
    if(closeFaultResult.status!=='FAIL'||closeFaultResult.nativeAttempts!==0||closeFaultResult.hostMainStarted!==false||closeFaultResult.cases.length!==behavior.cases.length||JSON.stringify(closeFailures.map(c=>c.id).sort())!==JSON.stringify(expectedCloseFaultIds.sort())||closeFailures.some(c=>!c.error?.includes('A returned Close must not settle unproven EC release')))throw Error('Compiled close fault did not fail exactly the two unresolved-release consumers');
    const genericCloseProof={status:'PASS',positiveCases:closeIds.length,positiveIds:closeIds,exactCompiledFault:closeFaultMutation,expectedFaultIds:expectedCloseFaultIds,actualFailureCount:closeFailures.length,faultResultPath:closeFaultResultPath,faultProofPath:closeFaultProofPath,existingLifecycleThreadFixture:true,dispatcherBootstrapCovered:false,hardwareIo:false};
    for(const file of [...tree(closeFaultRoot),closeFaultProofPath,closeFaultResultPath,path.join(out,'private-close-fault-generation.log'),path.join(out,'private-close-fault-run.log')])external.push({path:file,sha256:hash(file)});
    const ownerIds=['generic-owner-complete-original-cec-open','generic-owner-foreign-close-refused:before-duty',
      'generic-owner-foreign-close-refused:after-duty','generic-owner-temperature-teardown-keeps-close-identity',
      'generic-owner-temperature-teardown-cannot-rebind-write','generic-owner-replaced-device-close-refused',
      'generic-owner-open-cannot-borrow-foreign-library','generic-owner-second-backend-cannot-borrow-active-session',
      'generic-owner-missing-close-keeps-debt','generic-owner-failed-original-open-cleans-before-retry:false',
      'generic-owner-failed-original-open-cleans-before-retry:throw','generic-owner-close-and-reopen-refreshes-only-after-retirement',
      'generic-owner-duplicate-open-cannot-overwrite-live-claim','generic-owner-concurrent-original-opens-serialize',
      'generic-owner-reentrant-original-open-cannot-borrow','generic-owner-close-cannot-use-drifted-ec-address',
      'generic-owner-restore-rejects-foreign-after-monitor-detach','generic-owner-healthy-restore-retains-same-transport',
      'generic-owner-late-closed-reference-change-is-unsettled','generic-owner-close-engine-feedback-retains-foreign-owner',
      'generic-owner-new-vendor-open-cannot-abandon-ec-debt','generic-owner-verified-retirement-permits-vendor-open'];
    const ownerCases=behavior.cases.filter(c=>c.id.startsWith('generic-owner-'));
    if(ownerCases.length!==ownerIds.length||ownerIds.some(id=>!ownerCases.some(c=>c.id===id&&c.status==='PASS')))throw Error('Inherited EC original lifecycle ownership cases are missing or failed');
    const ownerFaultRoot=path.join(out,'private-session-fault');stageFaultPayload(payload,ownerFaultRoot);
    const ownerFaultHost=path.join(ownerFaultRoot,'YeManFanHost.dll');
    const ownerFaultProofPath=path.join(out,'private-session-fault-proof.json');
    run('dotnet',[harness,'fault-ec-session',candidate,ownerFaultHost,ownerFaultProofPath],'private-session-fault-generation.log',45000);
    const ownerFaultMutation=read(ownerFaultProofPath);
    if(ownerFaultMutation.status!=='PASS'||ownerFaultMutation.sourceSha256!==hash(candidate)||ownerFaultMutation.faultSha256!==hash(ownerFaultHost)||ownerFaultMutation.changedMethods.length!==3||!['RequireCurrent','RequireFreshOpen','DetachCpuTemperatureLoop'].every(name=>ownerFaultMutation.changedMethods.some(method=>method.includes('::'+name+'('))))throw Error('Session fault changed more than its exact guards/cache-lifetime boundaries');
    const ownerFaultResultPath=path.join(out,'private-session-fault-result.json');
    run('dotnet',[harness,ownerFaultHost,path.join(fixed,'YeManLightSetter.dll'),hc.path,ownerFaultResultPath],'private-session-fault-run.log',60000,2);
    const ownerFaultResult=read(ownerFaultResultPath),ownerFailures=ownerFaultResult.cases.filter(c=>c.status==='FAIL');
    const expectedOwnerFaultIds=['generic-owner-foreign-close-refused:before-duty','generic-owner-foreign-close-refused:after-duty',
      'generic-owner-temperature-teardown-keeps-close-identity','generic-owner-temperature-teardown-cannot-rebind-write',
      'generic-owner-replaced-device-close-refused','generic-owner-open-cannot-borrow-foreign-library',
      'generic-owner-second-backend-cannot-borrow-active-session','generic-owner-missing-close-keeps-debt',
      'generic-owner-concurrent-original-opens-serialize','generic-owner-reentrant-original-open-cannot-borrow',
      'generic-owner-restore-rejects-foreign-after-monitor-detach','generic-owner-close-engine-feedback-retains-foreign-owner',
      'events-admission-inherited-foreign-before-raw-events'];
    const businessMessages=['Expected transport ownership changed','Expected existing owned EC transport',
      'Stale backend cannot close the replacement device or original transport','Expected foreign EC session',
      'Concurrent original Opens cannot publish two owners',"Reentrant callback cannot borrow the first initializer's published reference",
      'Actual engine failure cleanup preserves foreign resources','Obsolete OpenEvents must be refused before publishing ready or retaining handlers'];
    if(ownerFaultResult.status!=='FAIL'||ownerFaultResult.nativeAttempts!==0||ownerFaultResult.hostMainStarted!==false||ownerFaultResult.cases.length!==behavior.cases.length||JSON.stringify(ownerFailures.map(c=>c.id).sort())!==JSON.stringify(expectedOwnerFaultIds.sort())||ownerFailures.some(c=>!businessMessages.some(message=>c.error?.includes(message))))throw Error('Compiled session fault did not fail exactly the thirteen expected ownership business assertions');
    const inheritedEcSessionProof={status:'PASS',positiveCases:ownerIds.length,positiveIds:ownerIds,exactCompiledFault:ownerFaultMutation,expectedFaultIds:expectedOwnerFaultIds,actualFailureCount:ownerFailures.length,faultResultPath:ownerFaultResultPath,faultProofPath:ownerFaultProofPath,hardwareIo:false,originalVirtualLifecyclePreserved:true,profileCloneSource:'complete original extracted HC PowerProfileManager.CloneProfile; unchanged shared Newtonsoft'};
    for(const file of [...tree(ownerFaultRoot),ownerFaultProofPath,ownerFaultResultPath,path.join(out,'private-session-fault-generation.log'),path.join(out,'private-session-fault-run.log')])external.push({path:file,sha256:hash(file)});
    const eventTypes=['OneXPlayerX1AMD','AYANEOFlipDS'];
    const eventIds=eventTypes.flatMap(type=>[
      ...['sleep','close','timeout','marker','generation','device'].map(kind=>`events-admission-between-${kind}:${type}`),
      ...['Applied','Discarded'].map(stage=>`events-admission-during-subscribe-${stage}:${type}`),
      `events-admission-rollback-preserves-new-generation:${type}`,`events-admission-healthy-public-entry:${type}`,
      `events-admission-old-preparation-cannot-publish-new-open:${type}`]);
    eventIds.push('events-admission-nonpower-rejection-uses-existing-recovery','events-admission-public-insertion-wait:healthy','events-admission-public-insertion-wait:generation-change','events-admission-rollback-remove-failure-retains-close-debt','events-admission-public-failure-reaches-existing-engine-feedback','events-admission-inherited-foreign-before-raw-events');
    const eventCases=behavior.cases.filter(c=>c.id.startsWith('events-admission-'));
    if(eventCases.length!==eventIds.length||eventIds.some(id=>!eventCases.some(c=>c.id===id&&c.status==='PASS')))throw Error('Actual OpenEvents admission/rollback/feedback cases are missing or failed');
    const eventFaultRoot=path.join(out,'private-events-fault');stageFaultPayload(payload,eventFaultRoot);
    const eventFaultHost=path.join(eventFaultRoot,'YeManFanHost.dll');const eventFaultProofPath=path.join(out,'private-events-fault-proof.json');
    run('dotnet',[harness,'fault-open-events-admission',candidate,eventFaultHost,eventFaultProofPath],'private-events-fault-generation.log',45000);
    const eventMutation=read(eventFaultProofPath);
    if(eventMutation.status!=='PASS'||eventMutation.sourceSha256!==hash(candidate)||eventMutation.faultSha256!==hash(eventFaultHost)||eventMutation.changedMethods.length!==1||!eventMutation.changedMethods[0].includes('::RequireOpenEventsAdmissionCurrent('))throw Error('Event fault is not bound to exactly the tested admission method');
    const eventFaultResultPath=path.join(out,'private-events-fault-result.json');
    run('dotnet',[harness,eventFaultHost,path.join(fixed,'YeManLightSetter.dll'),hc.path,eventFaultResultPath],'private-events-fault-run.log',60000,2);
    const eventFaultResult=read(eventFaultResultPath),eventFailures=eventFaultResult.cases.filter(c=>c.status==='FAIL');
    const expectedEventFaultIds=eventTypes.flatMap(type=>[
      ...['sleep','close','timeout','marker','device'].map(kind=>`events-admission-between-${kind}:${type}`),
      ...['Applied','Discarded'].map(stage=>`events-admission-during-subscribe-${stage}:${type}`),`events-admission-rollback-preserves-new-generation:${type}`]);
    expectedEventFaultIds.push('mixed-events-superseded-finalize-does-not-repeat-original-entry','events-admission-nonpower-rejection-uses-existing-recovery','events-admission-rollback-remove-failure-retains-close-debt','events-admission-public-failure-reaches-existing-engine-feedback','events-admission-inherited-foreign-before-raw-events');
    if(eventFaultResult.status!=='FAIL'||eventFaultResult.nativeAttempts!==0||eventFaultResult.hostMainStarted!==false||eventFaultResult.cases.length!==behavior.cases.length||JSON.stringify(eventFailures.map(c=>c.id).sort())!==JSON.stringify(expectedEventFaultIds.sort())||eventFailures.some(c=>!c.error?.includes('Obsolete OpenEvents must be refused before publishing ready or retaining handlers')))throw Error('Compiled event fault did not fail the exact twenty original plus one new mixed stale-admission business assertions');
    const openEventsAdmissionProof={status:'PASS',positiveCases:eventIds.length,positiveIds:eventIds,exactCompiledFault:eventMutation,expectedFaultIds:expectedEventFaultIds,actualFailureCount:eventFailures.length,faultResultPath:eventFaultResultPath,faultProofPath:eventFaultProofPath,publicBackendOpenEventsExecuted:true,existingEngineRecoveryAndPublicSnapshotExecuted:true,existingLifecycleThreadFixture:true,originalMixedOpenEventsBodyCoverageExpanded:true,hardwareIo:false};
    for(const file of [...tree(eventFaultRoot),eventFaultProofPath,eventFaultResultPath,path.join(out,'private-events-fault-generation.log'),path.join(out,'private-events-fault-run.log')])external.push({path:file,sha256:hash(file)});
    const bridgeRoot=path.join(out,'bridge-admission');const bridgeResultPath=path.join(out,'bridge-admission-result.json');
    run(process.execPath,[path.join(repo,'tools/ymcc_sim/global/consumer/fan-global/run.mjs'),'--test','control-self-heal','--evidence-root',bridgeRoot,'--output',bridgeResultPath],'bridge-admission-run.log',60000);
    const bridge=read(bridgeResultPath),bridgeCase=bridge.cases?.find(row=>row.id==='control-self-heal');
    const bridgeEvents=bridgeCase?.originalReport?.results?.filter(row=>row.name.startsWith('OpenEvents admission '))??[];
    if(bridge.status!=='PASS'||bridge.summary?.requested!==1||bridge.summary.executed!==1||bridge.summary.passed!==1||bridge.summary.failed!==0||bridge.summary.hardwareCalls!==0||bridge.summary.realHostStarts!==0||bridgeEvents.length!==10||bridgeEvents.some(row=>!row.ok)||!bridgeCase.sourceStable||bridge.sourceChanges?.length||bridge.diagnostics?.length)throw Error('Actual lifecycle bridge admission error policy did not pass its exact ten new consumers');
    const bridgeInputs=Object.fromEntries(bridge.sourceHashes.map(row=>[row.path,row.sha256]));
    for(const [file,pin]of Object.entries(bridgeInputs))if(before[file]&&before[file]!==pin)throw Error('Bridge/native sources mixed during combined consumer proof: '+file);
    const bridgeAdmissionProof={status:'PASS',existingRunner:'tools/ymcc_sim/global/consumer/fan-global/run.mjs',selectedExistingSuite:'control-self-heal',checks:bridgeCase.originalReport.checks,newEventCases:bridgeEvents.length,resultPath:bridgeResultPath,sourceHashes:bridge.sourceHashes,scope:'Actual FanHostLifecycle and inert Host adapter; no HTTP listener/Host Main/browser/native IO',hardwareIo:false};
    for(const file of [...tree(bridgeRoot),bridgeResultPath,path.join(out,'bridge-admission-run.log')])external.push({path:file,sha256:hash(file)});
    toolGate.verifyFinished();
    const combined={...before,...fixture.sourceHashes,...bootstrap.sourceHashes,...constructors.sourceHashes,...bridgeInputs};

    const changes=Object.entries(combined).filter(([f,pin])=>!fs.existsSync(path.join(repo,f))||hash(path.join(repo,f))!==pin).map(([path])=>path);
    if(JSON.stringify(sourceContractMembers)!==JSON.stringify(sourceContractInputs(repo)))changes.push('Source contract device-file membership changed');

    const inputChanges=external.filter(x=>!fs.existsSync(x.path)||hash(x.path)!==x.sha256);

    const candidateAfter=tree(payload).map(file=>({path:file,sha256:hash(file)}));

    const candidateChanged=JSON.stringify(candidateBefore)!==JSON.stringify(candidateAfter);

    const evidenceFiles=[...toolGate.receipts.map(r=>({path:r.receipt,sha256:hash(r.receipt)})),...candidateAfter,...(fs.existsSync(path.join(out,'candidate-staging.json'))?[{path:path.join(out,'candidate-staging.json'),sha256:hash(path.join(out,'candidate-staging.json'))}]:[]),...factory.evidence,...sta.evidence,...validation.evidence,...bootstrap.evidence,...constructors.evidence,...joined.evidence,...publicFactory.evidence,...originalLifecycle.evidence,...originalMsiLifecycle.evidence,...originalMsiManual.evidence,...originalMsiReplay.evidence,...tree(flagFaultRoot).map(file=>({path:file,sha256:hash(file)})),
      ...[flagFaultProofPath,flagFaultResultPath,flagPositivePath,path.join(out,'mixed-flags-positive.log'),path.join(out,'private-manager-flags-fault-generation.log'),path.join(out,'private-manager-flags-fault-run.log')].map(file=>({path:file,sha256:hash(file)})),...tree(mixedFaultRoot).map(file=>({path:file,sha256:hash(file)})),
      ...[mixedFaultProofPath,mixedFaultResultPath,mixedPositivePath,path.join(out,'mixed-reentry-positive.log'),path.join(out,'private-mixed-reentry-fault-generation.log'),path.join(out,'private-mixed-reentry-fault-run.log')].map(file=>({path:file,sha256:hash(file)})),...tree(sourceContractRoot).map(file=>({path:file,sha256:hash(file)})),...tree(initializationFaultRoot).map(file=>({path:file,sha256:hash(file)})),
      ...[initializationFaultProofPath,initializationFaultResultPath,path.join(out,'private-initialization-fault-generation.log'),path.join(out,'private-initialization-fault-run.log')].map(file=>({path:file,sha256:hash(file)})),...tree(faultRoot).map(file=>({path:file,sha256:hash(file)})),
      ...tree(bridgeRoot).map(file=>({path:file,sha256:hash(file)})),
      ...[bridgeResultPath,path.join(out,'bridge-admission-run.log')].map(file=>({path:file,sha256:hash(file)})),
      ...tree(eventFaultRoot).map(file=>({path:file,sha256:hash(file)})),
      ...[eventFaultProofPath,eventFaultResultPath,path.join(out,'private-events-fault-generation.log'),path.join(out,'private-events-fault-run.log')].map(file=>({path:file,sha256:hash(file)})),
      ...tree(ownerFaultRoot).map(file=>({path:file,sha256:hash(file)})),
      ...[ownerFaultProofPath,ownerFaultResultPath,path.join(out,'private-session-fault-generation.log'),path.join(out,'private-session-fault-run.log')].map(file=>({path:file,sha256:hash(file)})),
      ...tree(closeFaultRoot).map(file=>({path:file,sha256:hash(file)})),
      ...[closeFaultProofPath,closeFaultResultPath,path.join(out,'private-close-fault-generation.log'),path.join(out,'private-close-fault-run.log')].map(file=>({path:file,sha256:hash(file)})),
      ...tree(path.join(testBuild,'bin')).map(file=>({path:file,sha256:hash(file)})),
      ...[faultProofPath,faultResultPath,path.join(out,'candidate-result.json'),path.join(out,'private-bool-fault-generation.log'),path.join(out,'private-bool-fault-run.log'),path.join(generated,'Fixture.g.cs'),path.join(generated,'fixture-provenance.json')].map(file=>({path:file,sha256:hash(file)}))];
    const protectedEvidence=[...new Map(evidenceFiles.map(row=>[row.path,row])).values()].map(row=>({...row,evidenceKind:'owned compiled fixture or original consumer receipt'}));
    result={schemaVersion:1,kind:'onex-fan-lighting-coexistence',status:behavior.status==='PASS'&&behavior.cases.length>=69&&behavior.checks>=280&&!changes.length&&!inputChanges.length&&!candidateChanged?'PASS':'FAIL',

      sourceHashes:[...Object.entries(combined).map(([path,sha256])=>({path,sha256})),...protectedEvidence],protectedEvidence,sourceChanges:changes,externalInputs:external,externalInputChanges:inputChanges,inputEvidence,

      fixture,initializationProof,factoryInitializationProof,staBootstrapProof,deploymentValidationProof,installerAdmissionProof,formalCapabilityAclProof,originalBootstrapProof,originalConstructorProof,joinedFactoryProof,publicFactoryProof,originalLifecycleProof,originalMsiLifecycleProof,originalMsiManualProof,originalMsiReplayProof,sourceContractProof,mixedEventsProof,managerFlagsProof,inheritedEcProof,genericCloseProof,inheritedEcSessionProof,openEventsAdmissionProof,bridgeAdmissionProof,legacy:{resultPath:path.join(out,'old-binary-result.json'),status:old.status,cases:old.cases.length,checks:old.checks.length},

      oldWrite:{resultPath:path.join(out,'old-write-result.json'),status:oldWrite.status,cases:oldWrite.cases.length,checks:oldWrite.checks.length,goldens:oldWrite.goldenWrites.length},

      candidate:{resultPath:path.join(out,'candidate-result.json'),status:behavior.status,cases:behavior.cases.length,checks:behavior.checks,sha256:hash(candidate),builtFromCurrentSources:!options.candidateHostRoot,installed:false},

      hostPayloadRoot:payload,hostPayloadFiles:candidateAfter,candidateBefore,candidateAfter,candidateChanged,

      toolPolicyPlan:toolGate.planPin,toolPolicyLaunches:toolGate.receipts,hardwareIo:false,installedApplicationMutated:false,hostMainStarted:false,physicalConflictObserved:false,fullProductAcceptance:false};

  } catch(error) {

    if(preparedContext&&!preparedContext.executionMarker)throw error;
    result={schemaVersion:1,kind:'onex-fan-lighting-coexistence',status:error instanceof BlockedInputError||error.toolPolicyBlocked?'BLOCKED':'FAIL',code:error.code??null,error:error.stack,detail:error.detail??null,

      sourceHashes:Object.entries(before).map(([path,sha256])=>({path,sha256})),externalInputs:external,inputEvidence,candidateBefore,

      hardwareIo:false,hostMainStarted:false,installedApplicationMutated:false,fullProductAcceptance:false};

  }

  if(preparedContext){result.preparedParentBuild=completePreparedParentBuild(preparedContext,result);if(result.preparedParentBuild.budgetExceeded||!result.preparedParentBuild.originalInitialCompileCallsStillExecuted||(result.status==='PASS'&&!result.preparedParentBuild.originalLaterCompileCallsStillExecuted)){result.status='FAIL';result.code='PARENT_BUILD_ORIGINAL_COMPILE_OR_PARENT_BUDGET_NOT_PROVEN';}}
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;

}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {

  try {

    const result=await execute(parseArgs(process.argv.slice(2)));

    console.log(JSON.stringify({status:result.status,code:result.code,legacy:result.legacy,oldWrite:result.oldWrite,candidate:result.candidate,hostPayloadRoot:result.hostPayloadRoot,error:result.error}));

    process.exitCode=['PASS','PREPARED_NOT_EXECUTED','PREPARED_INITIAL_THREE_TOOLS_COMPILED_NOT_EXECUTED'].includes(result.status)?0:result.status==='BLOCKED'?3:2;

  } catch(error) {

    console.error(JSON.stringify({status:'FAIL',code:'ONEX_ARGUMENT_ERROR',error:error.message,...(error.prepareBlocked?{prepareCode:error.code,detail:error.detail??null}:{})}));process.exitCode=2;

  }

}

