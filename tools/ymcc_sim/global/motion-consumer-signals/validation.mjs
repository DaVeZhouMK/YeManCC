import {CONFIGURATIONS,EXPECTED_MUTATIONS,SOURCE_FINISH_FLAGS,ORIGINAL_CONSUMER_ASSERTIONS,DEFAULT_TRANSITION_ADDITIONAL_ASSERTIONS,needsCapabilityGrid,validHash,validCount} from './contracts.mjs';
const object = value => value!==null && typeof value==='object' && !Array.isArray(value);
const exactConfigurations = rows => Array.isArray(rows) && rows.length===2 && new Set(rows.map(row=>row?.name)).size===2 && CONFIGURATIONS.every(name=>rows.some(row=>row?.name===name));
/** Structural validation is separate from execution and filesystem verification. */
export function validateMotionReport(report,expectations) {
 const issues=[];const add=code=>{if(!issues.includes(code))issues.push(code);};
 if(!object(report)||report.schema!=='hc-model-motion-consumer-evidence-v4'||report.status!=='pass'||report.mode!=='candidate'||report.consumerOnlyControl!==false||report.error!==undefined)add('FULL_ORIGINAL_REPORT_REQUIRED');
 if(!exactConfigurations(report?.configurations))add('BOTH_POSITIVE_CONFIGURATIONS_REQUIRED');
 const subtotals=[];
 for(const config of Array.isArray(report?.configurations)?report.configurations:[]) {
  const value=config?.report,groups=value?.groups;
  if(config?.accepted!==true||config.exitCode!==0||!validHash(config.executableSha256)||!Array.isArray(config.compileCommand)||config.compileCommand[0]!=='cl.exe'||!config.compileCommand.includes(config.name==='Release'?'/O2':'/Od'))add('POSITIVE_EXECUTION_OR_COMPILATION_INVALID');
  if(!object(value)||value.status!=='pass'||!Array.isArray(value.failures)||value.failures.length||!validCount(value.checks)||value.checks<314600||!object(groups)||Object.values(groups).some(n=>!validCount(n))||Object.values(groups).reduce((n,v)=>n+v,0)!==value.checks)add('POSITIVE_ASSERTIONS_NOT_PROVEN');
  const unified=value?.unifiedBindingCoverage;
  if(!object(unified)||unified.registeredRecipes!==95||unified.controllerPreferredWindowsBlocked!==4||unified.actualSelectedConsumers!==242||unified.checks!==3448||unified.privateSelectedBoardCases!==7||unified.completeMotionDomain!==false||unified.physicalAcceptance!==false||groups?.['unified-actual-pair']!==242||groups?.['private-final-effect']!==7||groups?.['private-refusal']!==98)add('UNIFIED_ACTUAL_BINDING_CONSUMPTION_REQUIRED');
  const original=validCount(value?.consumerChecks)&&validCount(value?.reviewedResourceChecks)?value.consumerChecks-value.reviewedResourceChecks-DEFAULT_TRANSITION_ADDITIONAL_ASSERTIONS:null;
  if(original!==ORIGINAL_CONSUMER_ASSERTIONS||value?.reviewedResourceChecks!==385||groups?.['capability-prior-regression']!==1||!validCount(value?.capabilityChecks)||value.capabilityChecks+value.consumerChecks!==value.checks)add('ORIGINAL_CONSUMER_SUBTOTAL_NOT_PRESERVED');
  if(value?.aliases!==63||value?.imuClasses!==9||value?.legacyAliases!==181||value?.legacyConsumerCases!==168||value?.reviewedResourceModels!==18||value?.legacyFeatureAvailable!==true||value?.new9FeatureAvailable!==true)add('ORIGINAL_MODEL_AND_CASE_SETS_REQUIRED');
  if(value?.capabilityCoverage?.available!==true||!validCount(value.capabilityCoverage.executions)||value.capabilityCoverage.executions<33264||value.capabilityCoverage.skipped===true||value?.evidenceGenerationCoverage?.available!==true||value.evidenceGenerationCoverage.pureCases<14||value.evidenceGenerationCoverage.sequenceSteps<12||value.evidenceGenerationCoverage.emittedEvents<7)add('REAL_CAPABILITY_GRID_AND_DIAGNOSTICS_REQUIRED');
  if(value?.realDevicesAccessed!==false||value?.realSleepExecuted!==false||value?.fullBusExecuted!==false)add('SOFTWARE_SCOPE_EXCEEDED');
  subtotals.push({name:config?.name,originalConsumerAssertions:original,reviewedResourceAssertions:value?.reviewedResourceChecks,defaultTransitionAdditionalAssertions:DEFAULT_TRANSITION_ADDITIONAL_ASSERTIONS,consumerAssertions:value?.consumerChecks,capabilityAssertions:value?.capabilityChecks,totalAssertions:value?.checks});
 }
 const mutations=report?.mutations;
 if(!Array.isArray(mutations)||mutations.length!==EXPECTED_MUTATIONS.length||new Set(mutations.map(row=>row?.name)).size!==EXPECTED_MUTATIONS.length||!EXPECTED_MUTATIONS.every(name=>mutations.some(row=>row?.name===name)))add('COMPLETE_FIXED_FAULT_SET_REQUIRED');
 for(const mutation of Array.isArray(mutations)?mutations:[]) {
  if(mutation?.detected!==true||mutation.childExitCode!==1||!exactConfigurations(mutation.configurations))add('FAULT_NOT_SEMANTICALLY_DETECTED');
  for(const config of Array.isArray(mutation?.configurations)?mutation.configurations:[]) {
   if(config?.exitCode!==1||!validCount(config.checks)||!validCount(config.failedAssertions)||config.failedAssertions<1||config.failedAssertions>config.checks||!Array.isArray(config.groups)||!config.groups.length||config.groups.some(value=>typeof value!=='string'||!value))add('FAULT_NOT_SEMANTICALLY_DETECTED');
   if(needsCapabilityGrid(mutation?.name??'')&&(!validCount(config?.checks)||config.checks<314600))add('GENERIC_OR_CAPABILITY_FAULT_SKIPPED_ITS_GRID');
  }
 }
 if(!object(report?.sourceAtFinish)||SOURCE_FINISH_FLAGS.some(name=>report.sourceAtFinish[name]!==true)||!validHash(report.sourceAtFinish.sha256)||report.sourceAtFinish.sha256!==report?.source?.sha256)add('SOURCE_CLOSURE_MISSING_OR_CHANGED');
 const checks=report?.sourceChecks,writer=checks?.evidenceWriterChecks;
 if(!object(writer)||['ordinaryUint64Pair','powerUsesCurrentGeneration','bindingUsesAtomicActiveGeneration','noNonatomicSensorGeneration'].some(name=>writer[name]!==true)||writer.powerAssignments!==2||writer.bindingAssignments!==2||checks?.cacheBoundHasOnlyCatalogFind!==true||checks?.legacyCacheNoResolutionOrIO!==true)add('PRODUCTION_SOURCE_GATES_NOT_VERIFIED');
 const permitted=new Set(['inputCaptureResolveMsiMotionSelector','ROG registry fallback and exception handler','Original ASUS fast path and MSI-bound comparison tail','struct HcMotionSelectorReceipt {','inputCaptureLegacyHcMotionSelectorBound','inputCaptureLegacySensorFieldsSnapshot','inputCaptureMotionSelectorBound','BUS:motion-pair-admission','BUS:GamepadMotion-condition-and-call-prefix']);
 for(const key of ['capabilityControlChecks','legacySourceChecks']) {
  const rows=checks?.[key];if(!Array.isArray(rows)||rows.length!==(key==='capabilityControlChecks'?26:10)){add('HISTORICAL_SOURCE_CONTROLS_MISSING');continue;}
  for(const row of rows)if(!object(row)||!validHash(row.beforeSha256)||!validHash(row.currentSha256)||(row.unchanged!==true&&!(permitted.has(row.name)&&row.controlledRefactorVerified===true)))add('HISTORICAL_SOURCE_CONTROL_REJECTED');
 }
 if(report?.boundaries?.realDevicesAccessed!==false||report?.boundaries?.realSleepExecuted!==false||report?.boundaries?.fullBusExecuted!==false)add('SOFTWARE_SCOPE_EXCEEDED');
 if(!object(expectations)||expectations.hcCommit!=='a250b19a4b7e29dac3b383f2d1ac517d1af43e4e'||expectations.models?.length!==10||expectations.identities?.length!==63||expectations.legacyIdentities?.length!==181||expectations.legacy?.cases?.length!==215||expectations.legacy?.sourceChecks?.length!==77||expectations.reviewed?.length!==18||expectations.genericFallbackPolicy?.machineIdentityMutable!==false||expectations.genericFallbackPolicy?.physicalAcceptance!==false)add('INDEPENDENT_FIXED_ORACLE_REQUIRED');
 return {status:issues.length?'FAIL':'PASS',issues,subtotals,expectedFaults:EXPECTED_MUTATIONS.length,structuralValidationOnly:true};
}
