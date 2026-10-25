/** Narrow identity proof for the two existing DLL-only software-engine consumers.
 * No process, assembly loader, machine gate, native resolver or hardware is invoked.
 */
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
import {auditSleepEngineReceipt} from './receipt-audit.mjs';
import {readPeModuleIdentity} from './pe-module-identity.mjs';
const modes = new Map([['sleep.engine-memory','memory'], ['sleep.engine-http-all','all']]);
const prefix = 'tools/ymcc_sim/global/consumer/sleep-engine/';
const toolNames = ['Program.cs','Scenarios.cs','MemoryScenarios.cs','SleepEngineProbe.csproj','Invoke-SleepEngine.ps1','receipt-contract.ps1'];
const auditNames = ['tools/ymcc_sim/global/sleep-engine-verify.mjs', prefix+'receipt-audit.mjs'];
const requiredSources = ['FanLab/real-host/HostProcessLifetime.cs','FanLab/real-host/Program.cs','tools/ymcc_sim/domains/sleep/sleep_forms.json',
  ...toolNames.map(name=>prefix+name), ...auditNames, 'tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/sleep-engine-entry.ps1'];
const allowedBinaries = ['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll'];
const sha = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const normalize = value => typeof value === 'string' ? path.resolve(value).replaceAll('\\','/').toLowerCase() : '';
const need = (condition, code) => { if (!condition) throw Error('SLEEP_DLL_'+code); };
const equalPath = (a,b) => typeof a === 'string' && typeof b === 'string' && path.isAbsolute(a) && path.isAbsolute(b) && normalize(a) === normalize(b);
const inside = (root,file) => { const rel=path.relative(root,file); return rel!=='' && rel!=='..' && !rel.startsWith('..'+path.sep) && !path.isAbsolute(rel); };
function ownFile(root,file) {
  need(typeof file==='string' && path.isAbsolute(file) && inside(root,file), 'OUTPUT_OUTSIDE_OWN_CASE');
  need(inside(fs.realpathSync(root),fs.realpathSync(file)), 'OUTPUT_OUTSIDE_OWN_CASE');
  return file;
}
function readJson(file) {
  need(fs.statSync(file).size <= 16*1024*1024, 'RECEIPT_SIZE_LIMIT');
  const bytes=fs.readFileSync(file); need(bytes.length <= 16*1024*1024, 'RECEIPT_SIZE_LIMIT');
  return {value:JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,'')),sha256:hash(bytes)};
}
export const isSleepEngineDllCase = id => modes.has(id);
export function auditSleepEngineDllPayload({item,caseDir,hostPayload,candidateInputs,repoRoot,executionRepoRoot}) {
  const mode=modes.get(item?.id);
  need(mode && item.scope==='host-mock' && item.domain==='sleep' && item.source==='tools/ymcc_sim/global/sleep-engine-entry.ps1', 'ENTRY_CONTRACT_DIFFERS');
  need(item.exitCode===0 && item.signal===null && item.timedOut===false && item.launchError===null && item.outputTruncated===false && item.sourceChanged===false, 'OUTER_NATURAL_EXIT_UNPROVEN');
  for (const key of ['physicalHardwareValidated','operatingSystemSleepPerformed','installedApplicationExercised']) need(item.claims?.[key]===false, 'OUTER_SAFETY_MISSING_OR_PROMOTED');
  const payloadRoot=candidateInputs?.payloadRoot;
  need(typeof payloadRoot==='string' && path.isAbsolute(payloadRoot) && !/[\0\r\n]/.test(payloadRoot) && equalPath(payloadRoot,hostPayload?.root), 'EXPLICIT_CALLER_PAYLOAD_ROOT_REQUIRED');
  const realPayload=fs.realpathSync(payloadRoot), binaries=hostPayload?.binaries;
  need(Array.isArray(binaries) && binaries.length<=allowedBinaries.length && binaries.every(row=>row && allowedBinaries.includes(row.name) && (row.sha256===null || sha(row.sha256))) && new Set(binaries.map(row=>row.name)).size===binaries.length, 'BINARY_DECLARATIONS_INVALID');
  const dlls = new Map();
  for (const name of ['YeManFanHost.dll','YeManFanBackend.dll']) need(binaries.some(row=>row.name===name && sha(row.sha256)), 'HOST_BACKEND_PAIR_REQUIRED');
  // Preserve declared absence too: an EXE or optional dependency appearing later expires evidence.
  for (const row of binaries) {
    const file=path.join(payloadRoot,row.name); let bytes=null;
    try { bytes=fs.readFileSync(file); } catch(error) { if(error.code!=='ENOENT') throw error; }
    need((bytes===null ? null : hash(bytes))===row.sha256, 'PAYLOAD_BYTES_CHANGED:'+row.name);
    if (bytes) {
      need(equalPath(path.dirname(fs.realpathSync(file)),realPayload), 'PAYLOAD_BINARY_OUTSIDE_ROOT');
      if (row.name.endsWith('.dll')) dlls.set(row.name,{path:file,sha256:row.sha256,bytes});
    }
  }
  const inputs=[...(item.sourceInputs??[]),...(item.dependencyInputs??[])];
  need(inputs.every(row=>row && typeof row.path==='string' && sha(row.sha256)) && new Set(inputs.map(row=>row.path)).size===inputs.length, 'SOURCE_DECLARATIONS_INVALID');
  const sourceMap=new Map(inputs.map(row=>[row.path,row.sha256]));
  need(typeof executionRepoRoot==='string' && path.isAbsolute(executionRepoRoot) && !/[\0\r\n]/.test(executionRepoRoot), 'EXECUTION_REPO_ROOT_REQUIRED');
  for (const name of requiredSources) {
    need(sourceMap.has(name), 'SOURCE_CLOSURE_DIFFERS:'+name);
    need(hash(fs.readFileSync(path.join(repoRoot,name)))===sourceMap.get(name), 'CURRENT_SOURCE_CLOSURE_DIFFERS:'+name);
    need(hash(fs.readFileSync(path.join(executionRepoRoot,name)))===sourceMap.get(name), 'EXECUTION_SOURCE_CLOSURE_DIFFERS:'+name);
  }
  need(item.processGuard?.strategy==='windows-job-kill-on-close-gated-child' && equalPath(item.processGuard.helper,path.join(executionRepoRoot,'tools/ymcc_sim/global/host-process.ps1')) && item.processGuard.sha256===sourceMap.get('tools/ymcc_sim/global/host-process.ps1'), 'PROCESS_GUARD_IDENTITY_DIFFERS');
  const output = name => {
    const file=ownFile(caseDir,path.join(caseDir,'engine',name)), receipt=readJson(file);
    const proofs=(item.structuredEvidence??[]).filter(row=>equalPath(row?.path,file));
    need(proofs.length===1 && proofs[0].sha256===receipt.sha256 && proofs[0].failed===false, 'OUTPUT_IDENTITY_DIFFERS:'+name);
    return {file,...receipt};
  };
  const raw=output('sleep-engine-result.json'), original=output('receipt-audit.json'), receipt=raw.value, inner=original.value;
  need(receipt?.schemaVersion===1 && receipt.mode===mode && receipt.kind==='sleep-engine-consumption', 'MODE_OR_RECEIPT_KIND_DIFFERS');
  need(inner?.schemaVersion===1 && inner.kind==='sleep-engine-receipt-audit' && inner.status==='PASS' && Array.isArray(inner.findings) && !inner.findings.length, 'ORIGINAL_INNER_AUDIT_NOT_PASS');
  need(equalPath(inner.inputReceipt?.path,raw.file) && inner.inputReceipt.sha256===raw.sha256, 'INNER_INPUT_RECEIPT_DIFFERS');
  need(isDeepStrictEqual(Object.keys(inner.sourceHashes??{}).sort(), [...auditNames].sort()) && auditNames.every(name=>inner.sourceHashes[name]===sourceMap.get(name)), 'ORIGINAL_VALIDATOR_SOURCE_DIFFERS');
  // Reuse the unchanged original validator, while binding its source check to the
  // caller's fixed repoRoot, not this D overlay's import.meta.dirname.
  need(hash(fs.readFileSync(fileURLToPath(new URL('./receipt-audit.mjs',import.meta.url))))===sourceMap.get(prefix+'receipt-audit.mjs'), 'IMPORTED_VALIDATOR_DIFFERS_FROM_FIXED_SOURCE');
  for (const rows of [receipt.sourceInputs,receipt.sourceInputsAfter]) {
    need(Array.isArray(rows) && rows.length===toolNames.length, 'TOOL_SOURCE_CLOSURE_INCOMPLETE');
    for (const name of toolNames) {
      const file=path.join(executionRepoRoot,prefix,name), entries=rows.filter(row=>equalPath(row?.path,file));
      need(entries.length===1 && entries[0].sha256===sourceMap.get(prefix+name), 'TOOL_SOURCE_IDENTITY_DIFFERS:'+name);
    }
  }
  need(receipt.sourceStability?.unchanged===true && receipt.sourceStability.beforeBuildChecked===true && receipt.sourceStability.beforeCount===6 && receipt.sourceStability.afterCount===6 && Array.isArray(receipt.sourceStability.differences) && !receipt.sourceStability.differences.length, 'SOURCE_STABILITY_UNPROVEN');
  const hostSha=dlls.get('YeManFanHost.dll').sha256, backendSha=dlls.get('YeManFanBackend.dll').sha256;
  const checked=auditSleepEngineReceipt(receipt,{expectedHostSha256:hostSha,expectedBackendSha256:backendSha,expectedMode:mode,verifySourceHashes:false});
  need(checked.status==='PASS', 'INNER_REVALIDATION_FAILED:'+(checked.findings?.[0]?.code??'UNKNOWN'));
  need(inner.scope===checked.scope && isDeepStrictEqual(inner.boundary,checked.boundary) &&
    isDeepStrictEqual(inner.summary,{...checked.summary,sourceHashesVerified:true,currentWrapperAcceptance:true}), 'ORIGINAL_INNER_AUDIT_CONTRADICTORY');
  const metadata=new Map();
  function loadedRows(rows) {
    need(Array.isArray(rows) && rows.length>=2 && rows.length<=dlls.size && new Set(rows.map(row=>row?.path && path.basename(row.path).toLowerCase())).size===rows.length, 'LOADED_MODULE_SET_INVALID');
    for (const name of ['YeManFanHost.dll','YeManFanBackend.dll']) need(rows.some(row=>equalPath(row?.path,path.join(payloadRoot,name))), 'LOADED_HOST_BACKEND_PAIR_MISSING');
    for (const row of rows) {
      const name=typeof row?.path==='string' ? path.basename(row.path) : '', declared=dlls.get(name);
      need(declared && equalPath(row.path,declared.path) && row.sha256===declared.sha256, 'LOADED_PATH_OR_SHA_DIFFERS');
      if (!metadata.has(name)) metadata.set(name,readPeModuleIdentity(declared.bytes));
      const actual=metadata.get(name);
      need(row.moduleVersionId===actual.moduleVersionId && row.identity===actual.identity && actual.moduleName===name, 'LOADED_METADATA_DIFFERS:'+name);
    }
  }
  function pinnedRows(pins,loaded) {
    need(Array.isArray(pins) && pins.length===dlls.size && new Set(pins.map(row=>row?.path)).size===pins.length, 'PINNED_ARTIFACT_SET_INVALID');
    for (const [name,declared] of dlls) {
      const entries=pins.filter(row=>equalPath(row?.path,declared.path));
      need(entries.length===1 && entries[0].sha256===declared.sha256 && entries[0].actuallyLoaded===loaded.some(row=>equalPath(row.path,declared.path)), 'PINNED_LOADED_IDENTITY_DIFFERS:'+name);
    }
  }
  function actualHost(actual,{pinned=false}={}) {
    need(equalPath(actual?.payloadRoot,payloadRoot) && actual.expectedSha256===hostSha && actual.expectedBackendSha256===backendSha, 'ACTUAL_PAYLOAD_ROOT_OR_PAIR_DIFFERS');
    loadedRows(actual.loaded);
    if (pinned) {
      need(actual.backendFileSha256===backendSha, 'ACTUAL_BACKEND_FILE_DIFFERS');
      pinnedRows(actual.pinnedArtifacts,actual.loaded);
    }
  }
  function safety(group) {
    need(group.status==='PASS' && group.failure===null && (group.failurePhase===null || group.failurePhase===undefined), 'GROUP_EXECUTION_FAILED');
    for (const key of ['operatingSystemSleepPerformed','hardwareWritesPerformed','hcLoaded','productionMainInvoked','nativeSchedulerExercised','physicalHardwareValidated','machineGateAcquired','existingHostStopped']) need(group.safety?.[key]===false, 'SAFETY_MISSING_OR_PROMOTED:'+key);
    need(Array.isArray(group.safety.nativeAttempts) && !group.safety.nativeAttempts.length, 'NATIVE_ATTEMPT_PRESENT');
    need(group.process?.mainCalled===false && group.process.naturalExitObservedByParent===true && group.process.exitCode===0 && group.process.forcedCleanup===false, 'NATURAL_EXIT_OR_MAIN_DIFFERS');
    for (const phase of ['before','after']) {
      const gates=group.productionMachineGates?.[phase];
      need(Array.isArray(gates) && gates.length===2 && ['Global\\YeManFanHost-v2','Local\\YeManFanHost-v2'].every(name=>gates.filter(row=>row?.name===name && row.readable===true && typeof row.exists==='boolean' && row.acquired===false).length===1), 'MACHINE_GATE_OBSERVATION_INVALID');
    }
    need(group.checks?.some(row=>row.name==='realBackend remains null' && row.passed===true) && group.checks.some(row=>row.name==='HC assembly and target native imports absent' && row.passed===true), 'NULL_BACKEND_OR_RESOLVER_DENY_PROOF_MISSING');
  }
  const wrapper=receipt.wrapperVerification;
  need(wrapper?.expectedHostSha256===hostSha && wrapper.actualHostSha256===hostSha && wrapper.expectedBackendSha256===backendSha && wrapper.actualBackendSha256===backendSha && wrapper.hostUnchanged===true && wrapper.childNaturalExit===true, 'WRAPPER_IDENTITY_OR_EXIT_DIFFERS');
  for (const key of ['mainMachineGateBypassed','productionMainGateExercised','realHostMainStarted']) need(wrapper[key]===false, 'WRAPPER_MAIN_BOUNDARY_DIFFERS');
  const groupOutputs=[];
  function rawOutput(group) {
    const file=ownFile(caseDir,group.evidencePath), rawGroup=readJson(file), value=rawGroup.value;
    need(equalPath(file,path.join(caseDir,'engine','groups',group.mode,'sleep-engine-result.json')), 'GROUP_OUTPUT_PATH_DIFFERS');
    need(value && typeof value==='object' && Object.keys(value).length>0 && isDeepStrictEqual(Object.keys(value).sort(), ['schemaVersion','kind','status','mode','summary','process','actualHost','http','safety','productionMachineGates','checks','scenarios','fixtureIdentities','compatibilityDifferences','inputConsumerStateFeedback','failure','failurePhase','sourceScope'].sort()), 'GROUP_OUTPUT_SCHEMA_DIFFERS');
    for (const [key,field] of Object.entries(value)) {
      if (key==='process') {
        // The child cannot observe its own exit. The original wrapper replaces
        // this false with the parent's exit observation, and PowerShell changes
        // the timestamp's timezone representation without changing the instant.
        need(field && field.naturalExitObservedByParent===false && group.process.naturalExitObservedByParent===true &&
          Object.entries(field).every(([name,data])=>name==='naturalExitObservedByParent' ||
            (name==='startedAtUtc' ? typeof data==='string' && Number.isFinite(Date.parse(data)) && Date.parse(data)===Date.parse(group.process[name]) : isDeepStrictEqual(group.process?.[name],data))), 'GROUP_OUTPUT_PROCESS_DIFFERS');
      }
      else need(isDeepStrictEqual(group[key],field), 'GROUP_OUTPUT_DIFFERS:'+key);
    }
    groupOutputs.push({path:file,sha256:rawGroup.sha256,matchingHashedAggregate:true});
  }
  actualHost(receipt.actualHost,{pinned:mode==='memory'});
  // Aggregate groups retain their own pins, process identities, safety and raw output.
  if (mode==='all') {
    const groups=receipt.groups, byProcess=receipt.actualHost.loadedByProcess;
    need(Array.isArray(byProcess) && byProcess.length===3, 'LOADED_PROCESS_SET_INVALID');
    for (const group of groups) {
      actualHost(group.actualHost,{pinned:true}); safety(group); rawOutput(group);
      const entries=byProcess.filter(row=>row?.group===group.mode && row.processId===group.process.pid);
      need(entries.length===1 && entries[0].backendFileSha256===backendSha && isDeepStrictEqual(entries[0].pinnedArtifacts,group.actualHost.pinnedArtifacts) && isDeepStrictEqual(entries[0].loaded,group.actualHost.loaded), 'LOADED_PROCESS_IDENTITY_DIFFERS');
      need(group.actualHost.loaded.length===receipt.actualHost.loaded.length && group.actualHost.loaded.every(row=>receipt.actualHost.loaded.some(other=>isDeepStrictEqual(row,other))), 'AGGREGATE_LOADED_IDENTITY_DIFFERS');
    }
  } else { safety(receipt); rawOutput(receipt); }
  const own=path.dirname(fileURLToPath(import.meta.url));
  return {kind:'explicit-sleep-engine-dll-payload',mode,payloadRoot,hostSha256:hostSha,backendSha256:backendSha,
    loadedModules:[...metadata].map(([name,data])=>({name,sha256:dlls.get(name).sha256,...data})),
    originalInnerAudit:{path:original.file,sha256:original.sha256,status:inner.status,sourceHashesVerified:true},
    revalidation:{coreValidatorReused:true,coreStatus:checked.status,fixedRootToolSourcesVerified:true,executionRepoRoot:path.resolve(executionRepoRoot),currentRepoRoot:path.resolve(repoRoot),executionAndCurrentToolSourcesVerified:true,
      coreValidatorRelocatedSourceCheck:false,sourceStabilityVerified:true,groupOutputs},
    auditToolSources:['dll-payload-audit.mjs','pe-module-identity.mjs','receipt-audit.mjs'].map(name=>({path:path.join(own,name),sha256:hash(fs.readFileSync(path.join(own,name)))})),
    productionMainInvoked:false,physicalHardwareAcceptance:false,executedTests:0};
}