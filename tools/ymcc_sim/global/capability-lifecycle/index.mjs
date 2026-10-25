import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const validId = value => typeof value === 'string' && /^[a-z0-9][a-z0-9.-]{0,99}$/.test(value);
const validHash = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const relativeFile = value => typeof value === 'string' && value.length > 0 && !path.isAbsolute(value) && !/[\0\r\n]/.test(value) && !value.split(/[\\/]/).some(part => part === '..' || part === '');
const productFile = value => relativeFile(value) && /^(src|native|InputHost|FanLab|capabilities|CustomSteamLibrary|decky-plugin|PowerControl|GamepadPrerequisites)\//.test(value);
const inside = (root, target) => { const rel = path.relative(root, target); return rel === '' || (!path.isAbsolute(rel) && rel !== '..' && !rel.startsWith('..' + path.sep)); };
const successful = row => row && (row.passed === true || row.pass === true || row.ok === true || row.allPassed === true || /^(PASS|ALL_PASS)$/.test(row.status ?? '')) && ![row.passed,row.pass,row.ok,row.allPassed].includes(false) && (row.status === undefined || /^(PASS|ALL_PASS)$/.test(row.status)) && (row.verdict === undefined || /^(PASS|ALL_PASS)$/.test(row.verdict)) && !(row.exitCode > 0 || row.failedCount > 0 || row.fakeGreen === true);
export const LIFECYCLE_GATE_SOURCES = Object.freeze(['tools/ymcc_sim/global/capability-lifecycle/index.mjs']);
export const EXIT_REQUIREMENTS = Object.freeze(['destroyOnExit','destroyWhenStopStalls','preventResurrectionAfterExit','explicitOffStaysOff']);
export const PERSISTENT_REQUIREMENTS = Object.freeze(['recoverAfterSleep','recoverAfterTransientFailure','respectRecoveryLimits']);

/** Classification is reviewed by the AI from actual ownership and event paths.
 * A missing/unknown classification is an open work item, never one-shot by default. */
export function normalizeLifecycleContract(value) {
  if (!value || value.schemaVersion !== 1 || !Array.isArray(value.features) || !value.features.length || value.features.length > 128) throw Error('LIFECYCLE_CONTRACT_INVALID');
  const ids = new Set();
  return {schemaVersion:1, features:value.features.map(feature => {
    if (!validId(feature?.id) || ids.has(feature.id)) throw Error('LIFECYCLE_FEATURE_ID_INVALID_OR_DUPLICATE');
    ids.add(feature.id);
    if (!['persistent','one-shot','unknown'].includes(feature.persistence) || typeof feature.rationale !== 'string' || feature.rationale.trim().length < 16) throw Error('LIFECYCLE_CLASSIFICATION_AND_OWNERSHIP_REASON_REQUIRED');
    if (!Array.isArray(feature.sourceInputs) || !feature.sourceInputs.length || feature.sourceInputs.length > 256 || feature.sourceInputs.some(file => !productFile(file)) || new Set(feature.sourceInputs).size !== feature.sourceInputs.length) throw Error('LIFECYCLE_PRODUCT_SOURCE_REQUIRED');
    if (!Array.isArray(feature.resources) || !feature.resources.length || feature.resources.length > 32 || feature.resources.some(resource => !validId(resource)) || new Set(feature.resources).size !== feature.resources.length) throw Error('LIFECYCLE_OWNED_RESOURCE_CLASSES_REQUIRED');
    if (!relativeFile(feature.evidenceFile)) throw Error('LIFECYCLE_CASE_LOCAL_EVIDENCE_REQUIRED');
    // Explicit persistent ownership cannot be relabelled as a one-shot action.
    if (feature.persistence === 'one-shot' && feature.resources.some(resource => /^(subscriptions|streams|resident-processes|resident-windows|periodic-timers)$/.test(resource))) throw Error('LIFECYCLE_PERSISTENT_RESOURCE_MISCLASSIFIED');
    const normalized={id:feature.id,persistence:feature.persistence,rationale:feature.rationale.trim(),sourceInputs:[...feature.sourceInputs].sort(),resources:[...feature.resources].sort(),evidenceFile:feature.evidenceFile};
    if(feature.requiredComponents!==undefined){
      if(!Array.isArray(feature.requiredComponents)||!feature.requiredComponents.length||feature.requiredComponents.length>128||feature.requiredComponents.some(id=>!validId(id))||new Set(feature.requiredComponents).size!==feature.requiredComponents.length)throw Error('LIFECYCLE_REQUIRED_COMPONENTS_INVALID');
      normalized.requiredComponents=[...feature.requiredComponents].sort();
    }
    return normalized;
  }).sort((a,b)=>a.id.localeCompare(b.id))};
}
export const lifecycleContractFingerprint = value => hash(JSON.stringify(normalizeLifecycleContract(value)));

function readOwned(root, relative, maxBytes = 16 * 1024 * 1024) {
  if (!relativeFile(relative)) throw Error('LIFECYCLE_EVIDENCE_PATH_INVALID');
  const actualRoot = fs.realpathSync(root), lexical = path.resolve(root, relative);
  if (!inside(path.resolve(root), lexical)) throw Error('LIFECYCLE_EVIDENCE_OUTSIDE_CASE');
  const file = fs.realpathSync(lexical);
  if (!inside(actualRoot, file) || !fs.statSync(file).isFile() || fs.statSync(file).size > maxBytes) throw Error('LIFECYCLE_EVIDENCE_OUTSIDE_CASE_OR_TOO_LARGE');
  const bytes = fs.readFileSync(file);
  return {file,sha256:hash(bytes),value:JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''))};
}
function checkMeasurements(requirement, measurement, resources) {
  const m = measurement ?? {}, errors = [];
  const check = (condition, code) => { if (!condition) errors.push(code); };
  if (['destroyOnExit','destroyWhenStopStalls','preventResurrectionAfterExit'].includes(requirement)) {
    check(m.remainingOwnedResources && resources.every(resource => m.remainingOwnedResources[resource] === 0), 'OWNED_RESOURCES_NOT_OBSERVED_ZERO');
    check(m.ownerRevoked === true && m.bounded === true, 'BOUNDED_OWNER_REVOCATION_NOT_OBSERVED');
  }
  if (requirement === 'destroyWhenStopStalls') check(m.fallbackExercised === true && m.fallbackOwnedOnly === true, 'OWNED_FORCED_DESTRUCTION_NOT_OBSERVED');
  if (requirement === 'preventResurrectionAfterExit' || requirement === 'explicitOffStaysOff') check(Number.isSafeInteger(m.lateCallbacksDelivered) && m.lateCallbacksDelivered > 0 && m.restartsAfterExit === 0, 'LATE_CALLBACK_NON_RESURRECTION_NOT_OBSERVED');
  if (requirement === 'explicitOffStaysOff') check(m.desiredEnabled === false, 'EXPLICIT_OFF_NOT_EXERCISED');
  if (requirement === 'recoverAfterSleep' || requirement === 'recoverAfterTransientFailure') {
    check(m.desiredEnabled === true && m.recovered === true && m.healthObserved === true && m.currentGeneration === true, 'CURRENT_GENERATION_HEALTH_NOT_OBSERVED');
    check(Number.isSafeInteger(m.attempts) && Number.isSafeInteger(m.maxAttempts) && m.attempts > 0 && m.maxAttempts > 0 && m.attempts <= m.maxAttempts, 'BOUNDED_RECOVERY_NOT_OBSERVED');
    check(m.unconfirmedWritesReplayed === 0, 'UNCERTAIN_WRITE_REPLAY_NOT_EXCLUDED');
  }
  if (requirement === 'recoverAfterSleep') check(m.sleepObserved === true && m.readyGranted === true, 'SLEEP_AND_COMMITTED_READY_NOT_EXERCISED');
  if (requirement === 'respectRecoveryLimits') check(m.limitReached === true && m.additionalStarts === 0 && m.explicitOffHonored === true, 'RECOVERY_LIMIT_OR_OFF_PROTECTION_NOT_OBSERVED');
  return errors;
}


/** A reviewed component belongs to an existing feature. It never substitutes
 * for that parent's seven exit/recovery requirements or claims full acceptance. */
export function normalizeLifecycleComponent(value,adapterId){
 if(!value||!validId(value.featureId)||!validId(value.parentAdapterId)||value.parentAdapterId===adapterId||!validId(value.role)||value.fullPersistentAcceptance!==false)throw Error('LIFECYCLE_COMPONENT_DECLARATION_INVALID');
 if(!['persistent','one-shot','unknown'].includes(value.persistence)||typeof value.rationale!=='string'||value.rationale.trim().length<16)throw Error('LIFECYCLE_COMPONENT_CLASSIFICATION_REQUIRED');
 if(!Array.isArray(value.sourceInputs)||!value.sourceInputs.length||value.sourceInputs.length>256||value.sourceInputs.some(file=>!productFile(file))||new Set(value.sourceInputs).size!==value.sourceInputs.length)throw Error('LIFECYCLE_COMPONENT_PRODUCT_SOURCE_REQUIRED');
 if(!Array.isArray(value.resources)||!value.resources.length||value.resources.length>32||value.resources.some(resource=>!validId(resource))||new Set(value.resources).size!==value.resources.length)throw Error('LIFECYCLE_COMPONENT_RESOURCE_CLASSES_REQUIRED');
 if(value.persistence==='one-shot'&&value.resources.some(resource=>/^(subscriptions|streams|resident-processes|resident-windows|periodic-timers)$/.test(resource)))throw Error('LIFECYCLE_PERSISTENT_RESOURCE_MISCLASSIFIED');
 if(!relativeFile(value.evidenceFile))throw Error('LIFECYCLE_COMPONENT_CASE_LOCAL_EVIDENCE_REQUIRED');
 return {featureId:value.featureId,parentAdapterId:value.parentAdapterId,persistence:value.persistence,role:value.role,rationale:value.rationale.trim(),sourceInputs:[...value.sourceInputs].sort(),resources:[...value.resources].sort(),evidenceFile:value.evidenceFile,fullPersistentAcceptance:false};
}
function originalExecutionAccepted(result){return result?.status==='PASS'&&result.exitCode===0&&!result.signal&&!result.timedOut&&!result.launchError&&!result.sourceChanged&&!result.candidateArtifactChanged&&!result.requiredEvidenceMissing&&!result.requiredEvidenceFailed&&!result.outputTruncated&&!result.buildError&&!result.missingInputs;}
function auditComponents(repoRoot,adapters,actual,report){
 const adapterIds=new Set();
 for(const adapter of adapters){if(!validId(adapter.id)||adapterIds.has(adapter.id)){report.diagnostics.push({adapterId:adapter.id,code:'LIFECYCLE_DUPLICATE_OR_INVALID_ADAPTER_ID'});continue;}adapterIds.add(adapter.id);}
 for(const adapter of adapters){
  if(adapter.lifecycleComponent===undefined)continue;
  if(adapter.lifecycleContract!==undefined){report.diagnostics.push({adapterId:adapter.id,code:'LIFECYCLE_ADAPTER_HAS_FEATURE_AND_COMPONENT_ROLES'});continue;}
  let component;try{component=normalizeLifecycleComponent(adapter.lifecycleComponent,adapter.id);}catch(error){report.diagnostics.push({adapterId:adapter.id,code:error.message});continue;}
  const row={adapterId:adapter.id,...component,status:'INCOMPLETE',componentExecutionStatus:'NOT_RUN',requiredByParent:false,parentAcceptance:'INCOMPLETE',fullFeatureAcceptance:false,diagnostics:[]};report.components.push(row);
  const parent=report.features.filter(feature=>feature.adapterId===component.parentAdapterId&&feature.id===component.featureId);
  if(parent.length!==1)row.diagnostics.push({code:'LIFECYCLE_COMPONENT_PARENT_FEATURE_MISSING'});
  else if(parent[0].persistence!==component.persistence)row.diagnostics.push({code:'LIFECYCLE_COMPONENT_PARENT_CLASSIFICATION_MISMATCH'});
  else{row.requiredByParent=parent[0].requiredComponents.includes(adapter.id);if(!row.requiredByParent)row.diagnostics.push({code:'LIFECYCLE_COMPONENT_NOT_REQUIRED_BY_PARENT'});}
  const result=actual.get(adapter.id);
  if(component.persistence==='unknown'){row.diagnostics.push({code:'LIFECYCLE_CLASSIFICATION_UNRESOLVED'});continue;}
  if(!result){row.diagnostics.push({code:'LIFECYCLE_COMPONENT_EXECUTION_MISSING'});continue;}
  if(!originalExecutionAccepted(result)){row.componentExecutionStatus=result.status==='FAIL'?'FAIL':'NOT_ACCEPTED';row.diagnostics.push({code:'LIFECYCLE_COMPONENT_ORIGINAL_EXECUTION_NOT_ACCEPTED'});continue;}
  try{
   if(!adapter.resultFiles?.includes(component.evidenceFile))throw Error('LIFECYCLE_COMPONENT_RESULT_NOT_REQUIRED');
   for(const file of LIFECYCLE_GATE_SOURCES)if(!adapter.evidenceInputs?.includes(file))throw Error('LIFECYCLE_COMPONENT_GATE_SOURCE_NOT_BOUND');
   const sourceRows=[...(result.sourceInputs??[]),...(result.dependencyInputs??[])],sourceMap=new Map(sourceRows.map(row=>[row.path,row]));if(sourceMap.size!==sourceRows.length)throw Error('LIFECYCLE_DUPLICATE_SOURCE_INPUT');
   const root=fs.realpathSync(repoRoot);
   for(const file of [adapter.path,...(adapter.evidenceInputs??[]),...component.sourceInputs]){const pin=sourceMap.get(file);if(!relativeFile(file)||!pin||!validHash(pin.sha256))throw Error('LIFECYCLE_COMPONENT_EXECUTED_SOURCE_MISSING');const current=fs.realpathSync(path.resolve(root,file));if(!inside(root,current)||hash(fs.readFileSync(current))!==pin.sha256)throw Error('LIFECYCLE_COMPONENT_SOURCE_CHANGED');}
   const evidence=readOwned(result.evidenceDir,component.evidenceFile),recorded=result.structuredEvidence?.filter(pin=>path.resolve(pin.path??'')===path.resolve(evidence.file));
   if(recorded?.length!==1||recorded[0].sha256!==evidence.sha256||recorded[0].failed!==false)throw Error('LIFECYCLE_COMPONENT_RAW_EVIDENCE_NOT_BOUND');
   if(!successful(evidence.value))throw Error('LIFECYCLE_COMPONENT_PRODUCER_NOT_PASS');
   const identity=evidence.value.lifecycleComponent;for(const field of ['featureId','parentAdapterId','persistence','role'])if(identity?.[field]!==component[field])throw Error('LIFECYCLE_COMPONENT_EXECUTED_IDENTITY_MISMATCH');
   if(!['fullPersistentAcceptance','fullPersistentRecoveryAcceptance','fullProductAcceptance'].some(field=>evidence.value[field]===false)||['fullPersistentAcceptance','fullPersistentRecoveryAcceptance','fullProductAcceptance'].some(field=>evidence.value[field]===true))throw Error('LIFECYCLE_COMPONENT_CANNOT_CLAIM_FULL_ACCEPTANCE');
   for(const file of component.sourceInputs)if(evidence.value.sourceHashes?.[file]!==sourceMap.get(file).sha256)throw Error('LIFECYCLE_COMPONENT_PRODUCT_SOURCE_NOT_REPORTED');
   row.componentExecutionStatus='PASS';row.status='COMPONENT_PASS';row.evidence={path:evidence.file,sha256:evidence.sha256};
  }catch(error){row.componentExecutionStatus='INCOMPLETE';row.diagnostics.push({code:error.message});}
 }
 for(const feature of report.features){
  feature.componentRequirements=[];
  for(const id of feature.requiredComponents){const matches=report.components.filter(row=>row.adapterId===id&&row.parentAdapterId===feature.adapterId&&row.featureId===feature.id&&row.persistence===feature.persistence),component=matches[0];const valid=matches.length===1&&component.requiredByParent&&component.componentExecutionStatus==='PASS';feature.componentRequirements.push({adapterId:id,status:valid?'PASS':'INCOMPLETE',diagnostics:valid?[]:['LIFECYCLE_REQUIRED_COMPONENT_NOT_PROVEN']});}
  if(feature.status==='PASS'&&feature.componentRequirements.some(row=>row.status!=='PASS'))feature.status='INCOMPLETE';
 }
 for(const component of report.components){const parent=report.features.find(feature=>feature.adapterId===component.parentAdapterId&&feature.id===component.featureId&&feature.persistence===component.persistence);component.parentAcceptance=parent?.status??'INCOMPLETE';component.fullFeatureAcceptance=component.requiredByParent&&component.componentExecutionStatus==='PASS'&&parent?.status==='PASS';}
 report.componentIntegrationGaps=report.components.filter(row=>!row.fullFeatureAcceptance).map(row=>({adapterId:row.adapterId,featureId:row.featureId,parentAdapterId:row.parentAdapterId,componentExecutionStatus:row.componentExecutionStatus,parentAcceptance:row.parentAcceptance,requiredByParent:row.requiredByParent}));
}

/** Re-read the actual case-local receipt and compare current source bytes.
 * This is an evidence gate, not a simulator or a claim of physical teardown. */
export function auditLifecycleAcceptance({repoRoot, adapters = [], results = [], requireAll = false} = {}) {
  const report = {schemaVersion:1,kind:'ymcc-capability-lifecycle-acceptance',status:'INCOMPLETE',features:[],components:[],componentIntegrationGaps:[],unclassifiedAdapterIds:[],diagnostics:[],requireAll,softwareAcceptance:false,physicalHardwareAcceptance:false,operatingSystemSleepPerformed:false,executedTests:0};
  const actual = new Map();
  for (const row of results) { if (!validId(row?.id) || actual.has(row.id)) {report.diagnostics.push({code:'LIFECYCLE_DUPLICATE_OR_INVALID_EXECUTION_ID'});continue;} actual.set(row.id,row); }
  for (const adapter of adapters) {
    if (adapter.lifecycleContract === undefined) {if(adapter.lifecycleComponent===undefined)report.unclassifiedAdapterIds.push(adapter.id);continue;}
    let contract;
    try {contract = normalizeLifecycleContract(adapter.lifecycleContract);} catch (error) {report.diagnostics.push({adapterId:adapter.id,code:error.message});continue;}
    const fingerprint = lifecycleContractFingerprint(contract), result = actual.get(adapter.id);
    for (const feature of contract.features) {
      const row = {adapterId:adapter.id,id:feature.id,persistence:feature.persistence,rationale:feature.rationale,status:'INCOMPLETE',contractFingerprint:fingerprint,requiredComponents:feature.requiredComponents??[],requirements:[],diagnostics:[]};
      report.features.push(row);
      if (feature.persistence === 'unknown') {row.diagnostics.push({code:'LIFECYCLE_CLASSIFICATION_UNRESOLVED'});continue;}
      if (!result) {row.diagnostics.push({code:'LIFECYCLE_RUNTIME_EVIDENCE_MISSING'});continue;}
      if (result.status !== 'PASS' || result.exitCode !== 0 || result.signal || result.timedOut || result.launchError || result.sourceChanged || result.candidateArtifactChanged || result.requiredEvidenceMissing || result.requiredEvidenceFailed || result.outputTruncated || result.buildError || result.missingInputs) {row.diagnostics.push({code:'LIFECYCLE_ORIGINAL_EXECUTION_NOT_ACCEPTED'});}
      try {
        if (!adapter.resultFiles?.includes(feature.evidenceFile)) throw Error('LIFECYCLE_RESULT_NOT_REQUIRED_BY_ADAPTER');
        for (const file of LIFECYCLE_GATE_SOURCES) if (!adapter.evidenceInputs?.includes(file)) throw Error('LIFECYCLE_GATE_SOURCE_NOT_BOUND');
        const sourceRows = [...(result.sourceInputs ?? []),...(result.dependencyInputs ?? [])];
        const sourceMap = new Map(sourceRows.map(source => [source.path,source]));
        if (sourceMap.size !== sourceRows.length) throw Error('LIFECYCLE_DUPLICATE_SOURCE_INPUT');
        for (const file of [adapter.path,...(adapter.evidenceInputs ?? []),...feature.sourceInputs]) {
          const source = sourceMap.get(file);
          if (!source || !validHash(source.sha256) || !relativeFile(file)) throw Error('LIFECYCLE_EXECUTED_SOURCE_MISSING');
          const root = fs.realpathSync(repoRoot), current = fs.realpathSync(path.resolve(root,file));
          if (!inside(root,current) || hash(fs.readFileSync(current)) !== source.sha256) throw Error('LIFECYCLE_SOURCE_CHANGED');
        }
        const evidence = readOwned(result.evidenceDir,feature.evidenceFile);
        const recorded = result.structuredEvidence?.filter(proof => path.resolve(proof.path ?? '') === path.resolve(evidence.file));
        if (recorded?.length !== 1 || recorded[0].sha256 !== evidence.sha256 || recorded[0].failed !== false) throw Error('LIFECYCLE_RAW_EVIDENCE_NOT_BOUND_OR_CHANGED');
        if (!successful(evidence.value)) throw Error('LIFECYCLE_PRODUCER_NOT_PASS');
        const proof = evidence.value.lifecycleEvidence;
        if (proof?.schemaVersion !== 1 || proof.contractFingerprint !== fingerprint || !Array.isArray(proof.features)) throw Error('LIFECYCLE_CONTRACT_NOT_EXECUTED');
        const proofs = proof.features.filter(item => item.id === feature.id);
        if (proofs.length !== 1 || !Array.isArray(proofs[0].observations)) throw Error('LIFECYCLE_FEATURE_OBSERVATIONS_MISSING_OR_DUPLICATE');
        for (const file of feature.sourceInputs) if (evidence.value.sourceHashes?.[file] !== sourceMap.get(file).sha256) throw Error('LIFECYCLE_PRODUCT_SOURCE_NOT_REPORTED_BY_CONSUMER');
        for (const requirement of [...EXIT_REQUIREMENTS,...(feature.persistence === 'persistent' ? PERSISTENT_REQUIREMENTS : [])]) {
          const observations = proofs[0].observations.filter(item => item.requirement === requirement);
          const observation = observations[0], checks = [];
          if (observations.length !== 1) checks.push('LIFECYCLE_REQUIREMENT_MISSING_OR_DUPLICATE');
          else {
            const cases = evidence.value.cases?.filter(item => (item.id ?? item.name) === observation.caseId);
            if (observation.passed !== true || cases?.length !== 1 || !successful(cases[0])) checks.push('LIFECYCLE_BUSINESS_CASE_NOT_PASS');
            checks.push(...checkMeasurements(requirement,observation.measurements,feature.resources));
          }
          row.requirements.push({requirement,caseId:observation?.caseId ?? null,status:checks.length ? 'FAIL' : 'PASS',diagnostics:checks});
        }
        row.status = row.diagnostics.length || row.requirements.some(check=>check.status !== 'PASS') ? 'FAIL' : 'PASS';
      } catch (error) {row.diagnostics.push({code:error.message});row.status = 'INCOMPLETE';}
    }
  }
  auditComponents(repoRoot,adapters,actual,report);
  const failures = report.features.filter(row=>row.status === 'FAIL').length;
  const incomplete = report.features.filter(row=>row.status === 'INCOMPLETE').length;
  report.summary = {declaredFeatures:report.features.length,persistent:report.features.filter(row=>row.persistence === 'persistent').length,passed:report.features.filter(row=>row.status === 'PASS').length,failed:failures,incomplete,unclassifiedAdapters:report.unclassifiedAdapterIds.length,declaredComponents:report.components.length,passingComponents:report.components.filter(row=>row.componentExecutionStatus==='PASS').length,componentIntegrationGaps:report.componentIntegrationGaps.length};
  report.status = report.diagnostics.length || failures || report.components.some(row=>row.componentExecutionStatus==='FAIL') ? 'FAIL' : incomplete || !report.features.length || (requireAll && (report.unclassifiedAdapterIds.length||report.componentIntegrationGaps.length)) ? 'INCOMPLETE' : 'PASS';
  report.softwareAcceptance = report.status === 'PASS';
  report.meaning = 'Classification and current-source software lifecycle evidence only. Unknown capabilities remain open; requested/connected does not prove healthy. No installed application, physical sleep or hardware acceptance is implied.';
  return report;
}

/** Preserve every existing nonzero verdict; a lifecycle failure can only tighten it. */
export function applyLifecycleVerdict(verdict, audit, {requireAll = false} = {}) {
  if (verdict.exitCode !== 0) return verdict;
  if (audit.status === 'FAIL') return {status:'FAIL_LIFECYCLE_ACCEPTANCE',exitCode:2};
  if ((requireAll || audit.features.length > 0) && audit.status !== 'PASS') return {status:'INCOMPLETE_LIFECYCLE_ACCEPTANCE',exitCode:10};
  return verdict;
}

export function readLifecycleExecutionReceipts(files) {
  if (!Array.isArray(files) || !files.length || files.length > 128) throw Error('LIFECYCLE_RECEIPT_LIST_INVALID');
  const latest = new Map(); let total = 0;
  for (const locator of files) {
    const file = fs.realpathSync(locator), size = fs.statSync(file).size;
    total += size;
    if (size > 32 * 1024 * 1024 || total > 512 * 1024 * 1024) throw Error('LIFECYCLE_RECEIPT_SIZE_LIMIT');
    const receipt = JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
    if (receipt.tool !== 'ymcc_sim-global' || receipt.schemaVersion !== 1 || !['run','log-check'].includes(receipt.mode) || !Array.isArray(receipt.results)) throw Error('LIFECYCLE_RECEIPT_SCHEMA_INVALID');
    const ids = new Set();
    for (const row of receipt.results) {
      if (!validId(row?.id) || ids.has(row.id)) throw Error('LIFECYCLE_RECEIPT_DUPLICATE_OR_INVALID_ID');
      ids.add(row.id);
      latest.set(row.id,{...row,evidenceDir:path.join(path.dirname(file),'cases',row.id)});
    }
  }
  return [...latest.values()];
}
