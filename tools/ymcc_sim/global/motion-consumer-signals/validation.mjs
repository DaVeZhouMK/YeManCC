import {CONFIGURATIONS,EXPECTED_MUTATIONS,SOURCE_FINISH_FLAGS,ORIGINAL_CONSUMER_ASSERTIONS,DEFAULT_TRANSITION_ADDITIONAL_ASSERTIONS,needsCapabilityGrid,validHash,validCount} from './contracts.mjs';
const object = value => value!==null && typeof value==='object' && !Array.isArray(value);
const exactConfigurations = rows => Array.isArray(rows) && rows.length===2 && new Set(rows.map(row=>row?.name)).size===2 && CONFIGURATIONS.every(name=>rows.some(row=>row?.name===name));
const normalizedPath=value=>typeof value==='string'?value.replaceAll('\\','/').replace(/\/$/,'').toLowerCase():'';
const exactInput=(rows,file,sha)=>{
 const matches=Array.isArray(rows)?rows.filter(r=>normalizedPath(r?.path)===file):[];
 return matches.length===1&&matches[0].sha256===sha;
};
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
  if(!object(unified)||unified.registeredRecipes!==96||unified.controllerPreferredWindowsBlocked!==4||unified.actualSelectedConsumers!==242||unified.checks!==3458||unified.privateSelectedBoardCases!==7||unified.completeMotionDomain!==false||unified.physicalAcceptance!==false||groups?.['unified-actual-pair']!==242||groups?.['private-final-effect']!==7||groups?.['private-refusal']!==98)add('UNIFIED_ACTUAL_BINDING_CONSUMPTION_REQUIRED');
  const original=validCount(value?.consumerChecks)&&validCount(value?.reviewedResourceChecks)?value.consumerChecks-value.reviewedResourceChecks-DEFAULT_TRANSITION_ADDITIONAL_ASSERTIONS:null;
  if(original!==ORIGINAL_CONSUMER_ASSERTIONS||value?.reviewedResourceChecks!==406||groups?.['capability-prior-regression']!==1||!validCount(value?.capabilityChecks)||value.capabilityChecks+value.consumerChecks!==value.checks)add('ORIGINAL_CONSUMER_SUBTOTAL_NOT_PRESERVED');
  if(value?.aliases!==63||value?.imuClasses!==9||value?.legacyAliases!==181||value?.legacyConsumerCases!==168||value?.reviewedResourceModels!==19||value?.legacyFeatureAvailable!==true||value?.new9FeatureAvailable!==true)add('ORIGINAL_MODEL_AND_CASE_SETS_REQUIRED');
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
  const parserRows=rows.filter(row=>row?.name==='ymccApplyNamedImuMatrix');
  const busRows=rows.filter(row=>row?.name==='BUS:matrix-candidate-preparation');
  const oneXRows=rows.filter(row=>row?.name==='ymccResolveFamily');
  for(const row of rows) {
   const msiDemandRefactor=key==='legacySourceChecks'&&object(row)&&row.name==='MSI legacy field override tail'&&
    row.controlledRefactorVerified===true&&row.refactorScope==='fresh-negative-legacy-fields-skip-pure-lookup-only'&&
    row.beforeSha256!==row.currentSha256;
   const claimsCachedParser=object(row)&&row.refactorScope==='bounded-thread-local-axis-parser-contract';
   const cachedAxisParser=claimsCachedParser&&row.name==='ymccApplyNamedImuMatrix'&&parserRows.length===1&&
    row.unchanged===false&&row.controlledRefactorVerified===true&&
    row.beforeSha256==='6ec35a253da070abcc474e733f7e6bf5d0e3fc04b5a966a7aaad5bc279833aa9'&&
    row.currentSha256==='6f916d6102e44e353df1749e77574457344280501532211dd9a31e5492c08071'&&
    row.expectedAfterSha256===row.currentSha256&&
    row.immutableComparisonSha256==='cdef906232005b1456c91490ea23511231563e594da0d0de46181d51dabce035';
   const claimsMatrixPublication=object(row)&&row.refactorScope==='exact-deferred-bus-matrix-label-publication';
   const matrixPublication=claimsMatrixPublication&&row.name==='BUS:matrix-candidate-preparation'&&busRows.length===1&&
    row.unchanged===false&&row.controlledRefactorVerified===true&&
    row.beforeSha256==='240a7184fda9592bdacfb98be4734171b60c9bce0bad4957b93f03a1a1493b87'&&
    row.currentSha256==='a8332593171a93b4ff49cdf9d3452cc2584dcc0e2816aec6018e96d3d26fb794'&&
    row.identityPublicationSha256==='41d53b8a7e6077914d1eb510c98fed0882710031ec6ea3940b63235c0e625e43'&&
    row.identityActivationSha256==='f205d92d9b258207c4f9db1745a61c20d3cca687d08e1e7ca77b404d3109003c'&&
    row.identityControllerSha256==='3e2688cf0182b7114069c5b1b7bab86f23947c951ea1d6f259c86cf663b7d2d5';
   const exactOneXAfter='ec8deb9b4a113ab095f0d46728a8f14d28facf2eda8d1c6e40f2fad70bb1e1c5';
   const claimsOneX=object(row)&&(row.refactorScope==='exact-onex-factory-identity'||(row.name==='ymccResolveFamily'&&row.currentSha256===exactOneXAfter&&row.beforeSha256!==row.currentSha256));
   const repo=normalizedPath(report?.runner?.path).replace(/\/tools\/hc_model_motion_consumer_selftest\.mjs$/,'');
   const out=normalizedPath(report?.out);
   const exactOneX=claimsOneX&&key==='capabilityControlChecks'&&oneXRows.length===1&&row.name==='ymccResolveFamily'&&
    row.unchanged===false&&row.controlledRefactorVerified===true&&row.refactorScope==='exact-onex-factory-identity'&&
    row.beforeSha256==='a871cb030687e83e831123df7f47a506a4b67a882b84f3ddf9ffcf7193b60f1b'&&row.currentSha256===exactOneXAfter&&
    row.oneXBindingHeaderSha256==='5d9e26557993cfb0eb1fd9f67c438b1545e8871b7f0295d2669787f9b164a2f4'&&
    row.oneXFactorySourceSha256==='f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47'&&
    repo&&repo!==normalizedPath(report.runner.path)&&out&&
    exactInput(report.fixtures,repo+'/capabilities/hcoeminputcapabilitybinding.h',row.oneXBindingHeaderSha256)&&
    exactInput(report.fixtures,repo+'/capabilities/upstream/f2e58468/idevice.cs',row.oneXFactorySourceSha256)&&
    exactInput(report.generatedInputsAtCreation,out+'/hcoeminputcapabilitybinding.h',row.oneXBindingHeaderSha256);
   if(!object(row)||!validHash(row.beforeSha256)||!validHash(row.currentSha256)||
    (claimsCachedParser&&!cachedAxisParser)||
    (claimsMatrixPublication&&!matrixPublication)||
    (claimsOneX&&!exactOneX)||
    (row.unchanged!==true&&!(permitted.has(row.name)&&row.controlledRefactorVerified===true)&&!msiDemandRefactor&&!cachedAxisParser&&!matrixPublication&&!exactOneX))add('HISTORICAL_SOURCE_CONTROL_REJECTED');
  }
 }
 if(report?.boundaries?.realDevicesAccessed!==false||report?.boundaries?.realSleepExecuted!==false||report?.boundaries?.fullBusExecuted!==false)add('SOFTWARE_SCOPE_EXCEEDED');
 if(!object(expectations)||expectations.hcCommit!=='a250b19a4b7e29dac3b383f2d1ac517d1af43e4e'||expectations.models?.length!==10||expectations.identities?.length!==63||expectations.legacyIdentities?.length!==181||expectations.legacy?.cases?.length!==215||expectations.legacy?.sourceChecks?.length!==77||expectations.reviewed?.length!==19||expectations.genericFallbackPolicy?.machineIdentityMutable!==false||expectations.genericFallbackPolicy?.physicalAcceptance!==false)add('INDEPENDENT_FIXED_ORACLE_REQUIRED');
 return {status:issues.length?'FAIL':'PASS',issues,subtotals,expectedFaults:EXPECTED_MUTATIONS.length,structuralValidationOnly:true};
}
