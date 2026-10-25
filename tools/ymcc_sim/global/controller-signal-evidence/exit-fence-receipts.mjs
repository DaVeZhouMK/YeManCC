import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'')),digest=value=>createHash('sha256').update(value).digest('hex'),hash=file=>digest(fs.readFileSync(file));
const need=(ok,code)=>assert.ok(ok,code),lf=text=>text.replace(/\r\n/g,'\n'),same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const all=()=>read(new URL('./consumer-schema.json',import.meta.url));
const schema=()=>all().exitFenceCurrentV2??all().exitFenceCurrent;
const exact=(rows,names,key,code)=>need(Array.isArray(rows)&&rows.length===names.length&&new Set(rows.map(row=>row[key])).size===names.length&&names.every(name=>rows.some(row=>row[key]===name)),code);
export function exitFenceArtifactRows(repo){
 const historical=all().exitFenceCurrent;need(hash(historical.delivery.path)===historical.delivery.sha256,'FENCE_CURRENT_DELIVERY_PIN');
 const old=read(historical.preservedConsumerReceipt.path),oldRoot=path.join(old.run,'source');need(hash(historical.preservedConsumerReceipt.path)===historical.preservedConsumerReceipt.sha256,'FENCE_HISTORICAL_RECEIPT_PIN');
 for(const [name,pin] of Object.entries(historical.deliveredSources))need(hash(path.join(oldRoot,name))===pin,'FENCE_HISTORICAL_CONSUMER_PIN:'+name);
 need(hash(path.join(repo,historical.sourceContract))===historical.sourceContractSha256,'FENCE_HISTORICAL_CONTRACT_PIN');
 const rows=[{...historical.delivery,origin:'exit-fence.historical-delivery'}],contract=schema();if(contract===historical||!all().exitFenceCurrentV2)return rows;
 const d=read(contract.delivery.path);need(hash(contract.delivery.path)===contract.delivery.sha256&&d.status==='PASS_BOUNDED_EXIT_FENCE_CURRENT_CONSUMER_V2'&&d.wholeMainAcceptance===false&&d.wholeLifecycleAcceptance===false&&d.fullGlobalRunPerformed===false,'FENCE_CURRENT_DELIVERY_BOUNDARY');
 need(same(d.historicalDelivery,historical.delivery),'FENCE_PREVIOUS_DELIVERY_RETAINED');for(const [name,pin] of Object.entries(contract.deliveredSources))need(d.ownedSourceChanges[name]===pin&&hash(path.join(repo,name))===pin,'FENCE_DELIVERED_CONSUMER_PIN:'+name);
 need(hash(path.join(repo,contract.sourceContract))===contract.sourceContractSha256,'FENCE_REVIEWED_CONTRACT_PIN');
 for(const [key,pin] of Object.entries({delivery:contract.delivery,review:contract.dependencyReview,policy:contract.policyFullImplementationResult,currentReceipt:contract.preservedConsumerReceipt})){need(pin&&hash(pin.path)===pin.sha256,'FENCE_V2_REVIEW_PIN:'+key);rows.push({...pin,origin:'exit-fence.v2.'+key});}
 const review=read(contract.dependencyReview.path),policy=read(contract.policyFullImplementationResult.path);
 validateFenceDependencyReview(review,policy,contract,repo);need(review.status==='REVIEWED_SUBSTANTIVE_CURRENT_IMPLEMENTATION_EXECUTED'&&review.rawEqual===false&&review.lfEqual===false&&review.oldContractRewritten===false&&review.wholeMainApprovalUpdated===false&&same(review.currentFullImplementationExecution,contract.policyFullImplementationResult),'FENCE_V2_EXPLICIT_IMPLEMENTATION_REVIEW');
 need(policy.status==='PASS'&&policy.sourceStable===true&&policy.profiles.length===2&&policy.profiles.every(row=>row.exitCode===0&&row.runtime.passed===37&&row.runtime.cases.includes(review.changedObservationAssertion)),'FENCE_V2_FULL_IMPLEMENTATION_EXECUTED');
 for(const row of policy.profiles)for(const item of Object.values(row.artifacts))need(hash(item.path)===item.sha256,'FENCE_V2_POLICY_EXECUTION_ARTIFACT');
 return rows;
}
export function validateFenceDependencyReview(review,policy,registered,repo){
 const before=fs.readFileSync(review.before.path),after=fs.readFileSync(review.current.path),name='native/input_startup_policy.h';
 need(digest(before)===review.before.sha256&&digest(after)===review.current.sha256&&review.current.sha256===registered.dependencyHashes[name]&&hash(path.join(repo,name))===review.current.sha256&&hash(review.fullDiff.path)===review.fullDiff.sha256,'FENCE_V2_COMPLETE_RAW_REVIEW_PINS');
 const anchor='    if (!boot.empty()) next["startupAppliedBoot"] = boot;\n',addition='    // Unknown observations must not make a previous boot\'s result look current.\n    next["startupObservedBoot"] = boot.empty() ? json(nullptr) : json(boot);\n';
 need(before.toString('utf8').split(anchor).length===2&&before.toString('utf8').replace(anchor,anchor+addition)===after.toString('utf8')&&lf(before.toString('utf8'))!==lf(after.toString('utf8')),'FENCE_V2_FULL_CONTENT_CHANGE_REVIEW');
 need(policy.sourceHashes[name]===review.current.sha256&&policy.profiles?.length===2,'FENCE_V2_POLICY_SOURCE_BOUND');for(const [name,pin] of Object.entries(policy.sourceHashes))need(hash(path.join(repo,name))===pin,'FENCE_V2_POLICY_TEST_SOURCE_CHANGED:'+name);
}
export function verifyFenceFunctionIdentity(functions,contract){
 const names=Object.keys(contract.reviewedFunctionBundles[0].functionHashesLf);need(Object.keys(functions??{}).length===5&&names.every(name=>typeof functions[name]==='string'),'FENCE_COMPLETE_FIVE_FUNCTIONS');
 const normalized=Object.fromEntries(names.map(name=>[name,digest(lf(functions[name]))]));
 const matches=contract.reviewedFunctionBundles.filter(row=>same(row.functionHashesLf,normalized));need(matches.length===1,'FENCE_UNREVIEWED_COMPLETE_FUNCTIONS');return {id:matches[0].id,hashesLf:normalized};
}
export function validateExitFence(r,{repo,source,tool,sdk,tools,current,registration=schema()}){
 const registered=registration,bound=source(r.sourceHashes,'exit-fence');
 need(r.pass===true&&r.status==='PASS'&&r.evidenceComplete===true&&r.sourceStable===true&&r.sourceScopeUnchanged===true&&r.productionMonotonicFenceAccepted===true&&r.currentProductionMonotonic===true&&r.realControlIO===false&&r.wholeLifecycleAcceptance===false&&r.wholeMainAcceptance===false&&bound.has('native/main.cpp'),'ACTUAL_CURRENT_MONOTONIC_FENCE_REQUIRED');
 tool(r.compiler?.path,tools.native.compiler,r.compiler?.sha256,'exit-fence');tool(r.compiler?.vcvarsPath,tools.vcvars,r.compiler?.vcvarsSha256,'exit-fence.vcvars');sdk(r.compiler?.sdkVersion,'exit-fence');
 const contract=read(path.join(repo,registered.sourceContract));need(bound.has(registered.sourceContract)&&r.sourceContractSha256===registered.sourceContractSha256&&hash(path.join(repo,registered.sourceContract))===r.sourceContractSha256,'FENCE_REVIEWED_CONTRACT_PIN');
 need(same(contract.dependencyHashes,registered.dependencyHashes)&&same(r.dependencyHashes,registered.dependencyHashes),'FENCE_ACTUAL_DEPENDENCY_PINS');
 for(const [name,pin] of Object.entries(contract.dependencyHashes))need(bound.has(name)&&r.sourceHashes[name]===pin&&hash(path.join(repo,name))===pin,'FENCE_ACTUAL_DEPENDENCY_PINS:'+name);
 const baselinePath='tools/fixtures/input_capture_timer_retirement/exit_fence_baseline.json',baseline=read(path.join(repo,baselinePath));need(hash(path.join(repo,baselinePath))===contract.historicalBaselineSha256&&same(r.legacyFunctionHashes,baseline.functionHashes),'FENCE_HISTORICAL_BASELINE_PIN');
 for(const [name,text] of Object.entries(baseline.functions))need(digest(text)===baseline.functionHashes[name],'FENCE_HISTORICAL_FUNCTION_PIN');
 const actual=Object.fromEntries(Object.keys(baseline.functions).map(name=>[name,current.fn(name)])),reviewed=verifyFenceFunctionIdentity(actual,contract);need(r.reviewedSourceIdentity===reviewed.id,'FENCE_REVIEWED_CURRENT_IDENTITY');
 need(Object.keys(r.actualFunctionHashes??{}).length===5&&Object.entries(actual).every(([name,text])=>r.actualFunctionHashes[name]===digest(text)),'FENCE_CURRENT_FUNCTION_IDENTITY_MISSING');
 need(r.nativeSelftestFixtureResetBound===true&&Object.keys(r.nativeSelftestFixtureHelperHashes??{}).length===2&&new Set(Object.values(r.nativeSelftestFixtureHelperHashes)).size===1&&r.nativeSelftestFixtureCalls?.inputHostExitFenceSelfTest===2&&r.nativeSelftestFixtureCalls?.msiG3eCapabilitySelfTest===6,'NATIVE_SELFTEST_FIXTURE_RESTORATION_NOT_BOUND');
 const scopeStart=path.join(r.run,'SOURCE-SCOPE-START.json'),scopeEnd=path.join(r.run,'SOURCE-SCOPE-END.json');need(hash(scopeStart)===r.sourceScopeStartSha256&&hash(scopeEnd)===r.sourceScopeEndSha256,'FENCE_SCOPED_SOURCE_RECEIPT_PIN');
 const scope=read(scopeStart);need(same(scope,read(scopeEnd))&&same(scope.completeFunctions,actual)&&digest(current.fn('inputCaptureInit'))===r.actualCaptureInitFunctionSha256&&scope.sourceBoundOnlyInputCaptureInit===current.fn('inputCaptureInit'),'FENCE_COMPLETE_TESTED_SCOPE');
 need(same(scope.globals,baseline.globals),'FENCE_TERMINAL_DEFAULT_MUTATION');
 for(const name of ['inputHostExitFenceSelfTest','msiG3eCapabilitySelfTest'])need(digest(scope.fixtureHelpers[name])===r.nativeSelftestFixtureHelperHashes[name]&&/^[a-f0-9]{64}$/.test(r.nativeSelftestFunctionHashes?.[name]??'')&&scope.fixtureCalls[name]===r.nativeSelftestFixtureCalls[name]&&lf(scope.fixtureHelpers[name])==='const auto resetExitFenceFixture = [](const char* owner) {\n        g_inputHostExitFence.store(false, std::memory_order_release);\n        inputHostResetExitFence(owner);\n    };','NATIVE_SELFTEST_FIXTURE_RESTORATION_NOT_BOUND');
 const restore=functions=>{const bundle={...functions},needle='g_inputHostExitFence.load(std::memory_order_acquire)';need(bundle.inputHostResetExitFence.split(needle).length===2,'FENCE_RESTORE_MUTANT_TARGET');bundle.inputHostResetExitFence=bundle.inputHostResetExitFence.replace(needle,'g_inputHostExitFence.exchange(false, std::memory_order_acq_rel)');return bundle;};
 const candidate={...baseline.functions},oldReset=baseline.functions.inputHostResetExitFence,nl=oldReset.includes('\r\n')?'\r\n':'\n';
 const clearAnchor='    const bool previousRaised = g_inputHostExitFence.exchange(false, std::memory_order_acq_rel);';need(oldReset.split(clearAnchor).length===2,'FENCE_PRESERVED_CANDIDATE_TARGET');
 candidate.inputHostResetExitFence=oldReset.replace(clearAnchor,[
 '    // The process default is open. Capture initialization may reset diagnostic',
 '    // counters, but must never reopen a terminal fence raised by app exit.',
 '    const bool previousRaised = g_inputHostExitFence.load(std::memory_order_acquire);',
 '    if (previousRaised) {',
 '        appendNativeLifecycleLog("input-host-exit-fence", {',
 '            {"phase", "reset-refused"}, {"reason", "terminal-process-exit"},',
 '            {"previousRaised", true}, {"owner", owner ? owner : "unknown"},',
 '            {"refusals", g_inputHostExitFenceRefusals.load(std::memory_order_acquire)}',
 '        });',
 '        return;',
 '    }'].join(nl)).replace('        {"phase", "reset"},','        {"phase", "reset"},'+nl+'        {"resetScope", "diagnostic-counters-only"},');
 const extension={...candidate};extension.inputHostResetExitFence=extension.inputHostResetExitFence.replace('    const bool previousRaised = ','    (void)owner;'+nl+'    const bool previousRaised = ');
 const routeBundles={legacy:baseline.functions,monotonic:actual,'negative-restore-clear':restore(actual),'historical-candidate':candidate,'historical-candidate-clear':restore(candidate),'reviewed-owner-noop-extension':extension};
 const expectedHashes=route=>Object.fromEntries(Object.entries(routeBundles[route]).map(([name,text])=>[name,digest(text)]));

 need(r.profiles?.length===6,'FENCE_VARIANTS_REQUIRED');need(r.controlProfiles?.length===6,'FENCE_PRESERVED_CONTROL_ROUTES');
 for(const profile of registered.profiles)for(const route of [...registered.routes,...registered.controlRoutes]){
  const collection=registered.routes.includes(route)?r.profiles:r.controlProfiles,rows=collection.filter(row=>row.profile===profile&&row.route===route);need(rows.length===1,'FENCE_PROFILE_ROUTE_REQUIRED');const row=rows[0],negative=['legacy','negative-restore-clear','historical-candidate-clear'].includes(route);
  need(row.cases?.length===6&&row.allThreadsExplicitlyJoined===true&&row.threadsStarted===9&&row.threadsJoined===9&&row.realControlIO===false&&row.wholeLifecycleAcceptance===false,'FENCE_THREADS_OR_BOUNDARY_UNPROVEN');
  need(row.checks===148&&row.failures===registered.failuresByRoute[route]&&row.testedScopeStable===true&&row.isCurrentProductionRoute===(route==='monotonic'),'FENCE_ACTUAL_148_ROUTE_CHECKS');
  if(negative)need(row.exitCode===1&&row.pass===false&&row.expectedFailureCaught===true&&row.failedChecks?.includes('late-init-must-not-clear-terminal-fence'),'FENCE_CLEAR_DEFECT_NOT_REJECTED');
  else need(row.exitCode===0&&row.pass===true&&row.failures===0&&row.cases.every(c=>c.safetyPass===true),'FENCE_CURRENT_SOURCE_NOT_ACCEPTED');
  need(same(row.compiledDependencyHashes,contract.dependencyHashes)&&row.dependencyCases?.length===2&&row.dependencyCases.every(c=>c.safetyPass===true),'FENCE_REAL_STARTUP_POLICY_EXECUTION');
  const target=path.join(r.run,profile+'-'+route),cpp=path.join(target,'fence.cpp'),deps=path.join(target,'deps.json'),exe=path.join(target,'fence.exe');need(hash(cpp)===row.sourceSha256&&hash(deps)===row.sourceDependenciesSha256&&hash(exe)===row.executableSha256,'FENCE_ACTUAL_COMPILED_ARTIFACTS');
  const expected=expectedHashes(route);if(expected)need(same(row.compiledFunctionHashes,expected),'FENCE_CURRENT_FUNCTION_IDENTITY_MISSING');
  const text=fs.readFileSync(cpp,'utf8'),mask=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g,s=>s.replace(/[^\n]/g,' ')),compiled={};
  for(const name of Object.keys(actual)){const matches=[...mask.matchAll(new RegExp('^static[^\\n;{}]*\\b'+name+'\\s*\\([^;{}]*\\)\\s*(?:noexcept\\s*)?\\{','gm'))];need(matches.length===1,'FENCE_COMPILED_COMPLETE_FUNCTION');const start=matches[0].index;let end=mask.indexOf('{',start)+1,depth=1;while(depth&&end<mask.length){depth+=(mask[end]==='{')-(mask[end]==='}');end++;}compiled[name]=text.slice(start,end);need(digest(compiled[name])===row.compiledFunctionHashes[name],'FENCE_COMPILED_COMPLETE_FUNCTION');}
  if(!negative&&route!=='historical-candidate')need(verifyFenceFunctionIdentity(compiled,contract).id===(route==='monotonic'?reviewed.id:'monotonic-owner-noop-reviewed-control'),'FENCE_COMPILED_REVIEWED_IDENTITY');
  if(route==='historical-candidate'||route==='historical-candidate-clear')need(Object.entries(compiled).every(([name,text])=>name==='inputHostResetExitFence'||digest(text)===baseline.functionHashes[name]),'FENCE_PRESERVED_CANDIDATE_IDENTITY');
  const imports=read(deps).Data.Includes,snapshot=path.join(r.run,'source'),local=imports.filter(file=>{const rel=path.relative(snapshot,file);return rel&&!path.isAbsolute(rel)&&!rel.startsWith('..');}).map(file=>path.relative(snapshot,file).replaceAll('\\','/'));need(local.length===4&&Object.keys(contract.dependencyHashes).every(name=>local.includes(name)),'FENCE_ACTUAL_DEPENDENCY_INCLUDE_CLOSURE');
  const jsonLines=fs.readFileSync(path.join(target,'run.log'),'utf8').split(/\r?\n/).filter(line=>line.trim().startsWith('{')&&line.trim().endsWith('}'));need(jsonLines.length===1,'FENCE_UNIQUE_RUNTIME_JSON');const observed=JSON.parse(jsonLines[0]);need(observed.checks===row.checks&&observed.failures===row.failures&&same(observed.cases,row.cases),'FENCE_REAL_RUNTIME_LOG');
 }
 exact(r.sourceGateControls,registered.sourceGateNames,'name','FENCE_SOURCE_CONTRACT_CONTROLS');
 for(const row of r.sourceGateControls){const negative=registered.sourceGateNegatives.includes(row.name);need(row.accepted===!negative&&(!negative||row.expectedFailureCaught===true&&row.compiled===false&&typeof row.error==='string'),'FENCE_SOURCE_CONTRACT_NEGATIVE');if(!negative)need(contract.reviewedFunctionBundles.some(bundle=>bundle.id===row.reviewedIdentity&&same(bundle.functionHashesLf,row.functionHashesLf)),'FENCE_SOURCE_CONTRACT_POSITIVE');}
 need(r.checks===888&&r.controlChecks===888,'FENCE_COMPLETE_PRIMARY_AND_CONTROL_ASSERTIONS');
 return {id:'actual-monotonic-exit-fence',pass:true,profiles:2,currentScenarios:12,historicalAndMutantScenarios:24,preservedControlBuilds:6,checksPerRoute:148,sourceContractNegativeControls:6,explicitlyJoinedThreads:108,wholeLifecycleAcceptance:false};
}
