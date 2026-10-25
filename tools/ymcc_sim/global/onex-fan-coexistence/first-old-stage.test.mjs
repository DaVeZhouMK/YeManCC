import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {loadStageContract,validateStageDocuments,deriveManagedAllowance,reviewManagedGraph,assessBusinessResult,
  validateReviewRebinding,assertFreshOutputs,preparationSummary,parseStageArguments,checkedPin,fileHash,BASE_CLR_IDENTITIES} from './first-old-stage.mjs';
const json=pin=>JSON.parse(fs.readFileSync(checkedPin(pin).path,'utf8').replace(/^\uFEFF/,''));
const clone=value=>structuredClone(value);
export function runPlanTests({inputPin,evidenceDir}){
  const inputs=json(inputPin),ctx=loadStageContract(inputs.stage),business=json(inputs.business),launch=json(inputs.launch),processReceipt=json(inputs.process);
  assert(path.isAbsolute(evidenceDir)&&!fs.existsSync(evidenceDir));fs.mkdirSync(evidenceDir);
  const checks=[],test=(id,fn)=>{fn();checks.push({id,status:'PASS',targetExecuted:false});};
  const negative=(id,mutate,code)=>test(id,()=>{const d=clone(ctx);mutate(d);assert.throws(()=>validateStageDocuments(d),e=>e.code===code);});
  let positive;
  test('recorded-r2-business-and33-managed-exact-positive',()=>{
    positive=assessBusinessResult({business,launch,processReceipt,allowance:ctx.allowance,invocation:ctx.invocation});
    assert.equal(positive.passed,true);assert.equal(positive.graph.accepted.length,33);assert.deepEqual(positive.graph.invalid,[]);assert.deepEqual(positive.graph.missingBaseClr,[]);
  });
  test('five-preload-CLR-plus-one-framework-metadata-and-MTA',()=>{
    const rows=positive.graph.accepted;
    assert.equal(rows.filter(r=>r.acceptedBy==='exact-existing-preload-row').length,5);
    assert.equal(rows.filter(r=>r.acceptedBy==='exact-pinned-framework-metadata-row').length,1);
    assert.equal(rows.filter(r=>r.acceptedBy==='exact-reviewed-MTA').length,1);
    assert.equal(rows.filter(r=>r.acceptedBy==='scoped-policy').length,26);
  });
  negative('changed-business-budget-refused',d=>d.invocation.originalBusinessBudgetMs=45001,'FIRST_OLD_ORIGINAL_BUDGET_REQUIRED');
  negative('different-target-mode-refused',d=>d.invocation.firstOldArgs[0]='current','FIRST_OLD_EXACT_MODE_AND_ARGC');
  negative('unbound-launch-argv-refused',d=>d.invocation.dotnetArguments[1]='validate','FIRST_OLD_EXACT_ARGV_REQUIRED');
  negative('unreviewed-MTA-refused',d=>d.invocation.launcher.sha256='0'.repeat(64),'FIRST_OLD_REVIEWED_MTA_REQUIRED');
  negative('wrong-preload-policy-refused',d=>d.preload.policySha256='0'.repeat(64),'FIRST_OLD_PRELOAD_POLICY_MISMATCH');
  negative('review-unsafe-branch-refused',d=>d.review.branchLimits.rawHcClrLoadPermitted=true,'FIRST_OLD_REVIEW_UNSAFE_BRANCH');
  negative('missing-netstandard-metadata-row-refused',d=>d.frameworkMetadata.rows=d.frameworkMetadata.rows.filter(r=>!r.identity.startsWith('netstandard,')),'FIRST_OLD_NETSTANDARD_METADATA_ROW_REQUIRED');
  negative('wrong-netstandard-metadata-SHA-refused',d=>d.frameworkMetadata.rows.find(r=>r.identity.startsWith('netstandard,')).sha256='0'.repeat(64),'FIRST_OLD_NETSTANDARD_METADATA_ROW_REQUIRED');
  test('same-directory-unrecorded-framework-refused',()=>{
    const actual=clone(launch.actualLoadedManaged);actual.push({...actual[0],identity:'System.Unknown, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',path:path.join(path.dirname(actual[0].path),'System.Unknown.dll')});
    assert.equal(reviewManagedGraph(actual,ctx.allowance).allManagedPinned,false);
  });
  test('wrong-identity-same-path-and-SHA-refused',()=>{
    const actual=clone(launch.actualLoadedManaged);actual[0].identity=actual[0].identity.replace('10.0.0.0','99.0.0.0');assert.equal(reviewManagedGraph(actual,ctx.allowance).allManagedPinned,false);
  });
  test('raw-HC-loaded-refused',()=>{
    const actual=clone(launch.actualLoadedManaged);actual.find(r=>r.identity.startsWith('HandheldCompanion,')).sha256='08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583';
    const result=assessBusinessResult({business,launch:{...launch,actualLoadedManaged:actual},processReceipt,allowance:ctx.allowance,invocation:ctx.invocation});assert.equal(result.passed,false);assert(result.checks.some(x=>x.id==='raw-HC-not-runtime-loaded'&&!x.passed));
  });
  test('timeout-is-not-natural-business-success',()=>{
    const result=assessBusinessResult({business,launch,processReceipt:{...processReceipt,naturalExit:false,timeout:true},allowance:ctx.allowance,invocation:ctx.invocation});assert.equal(result.passed,false);
  });
  test('original-case-check-failure-is-retained',()=>{
    const bad=clone(business);bad.checks[0].passed=false;
    assert.equal(assessBusinessResult({business:bad,launch,processReceipt,allowance:ctx.allowance,invocation:ctx.invocation}).passed,false);
  });
  test('r2-already-consumed-outputs-refused',()=>{
    assert.throws(()=>assertFreshOutputs(ctx.invocation,path.join(evidenceDir,'not-created')),e=>e.code==='FIRST_OLD_FRESH_OUTPUT_REQUIRED');
  });
  test('prepare-only-can-never-claim-business-pass',()=>{
    const summary=preparationSummary(inputs.stage,ctx.invocation);assert.equal(summary.status,'PREPARED_NOT_EXECUTED');assert.equal(summary.passed,false);assert.equal(summary.producerExecuted,false);
  });
  test('wrong-plan-SHA-refused',()=>assert.throws(()=>checkedPin({...inputs.stage,sha256:'0'.repeat(64)}),e=>e.code==='FIRST_OLD_PIN_CHANGED'));
  test('arbitrary-command-CLI-refused',()=>assert.throws(()=>parseStageArguments(['--command','dotnet run']),e=>e.code==='FIRST_OLD_EXACT_CLI_ARGUMENTS'));
  const rebinding=json(inputs.rebind),next=json(rebinding.toInvocation),nextPolicy=json(next.policy),nextArgs=json(next.argumentMetadata);
  test('r2-to-r3-exact-metadata-rebind-positive',()=>assert.equal(validateReviewRebinding(ctx,next,nextPolicy,nextArgs),true));
  test('rebind-policy-context-change-refused',()=>{
    const changed=clone(nextPolicy);changed.contexts[1].id+='-unregistered';assert.throws(()=>validateReviewRebinding(ctx,next,changed,nextArgs),e=>e.code==='FIRST_OLD_REBIND_POLICY_SEMANTICS_CHANGED');
  });
  test('rebind-one-changed-compiled-byte-pin-refused',()=>{
    const changed=clone(next);changed.outputs[0].sha256='0'.repeat(64);assert.throws(()=>validateReviewRebinding(ctx,changed,nextPolicy,nextArgs),e=>e.code==='FIRST_OLD_REBIND_OUTPUT_BYTES_OR_SET_CHANGED');
  });
  const result={schemaVersion:1,status:'PASS_PURE_PLAN_AND_RECORDED_RESULT_TESTS',targetExecutions:0,childProcessesStartedByTests:0,
    checks,checkCount:checks.length,recordedPositive:{sourceBusiness:inputs.business,sourceLaunch:inputs.launch,sourceProcess:inputs.process,
      caseCount:business.cases.length,originalChecks:business.checks.length,nativeAttempts:business.nativeAttempts,actualManagedCount:launch.actualLoadedManaged.length,assessment:positive,
      recordedResultReplayedOnly:true,newBusinessExecution:false},inputPin,runner:{path:fileURLToPath(new URL('./first-old-stage.mjs',import.meta.url)),sha256:fileHash(fileURLToPath(new URL('./first-old-stage.mjs',import.meta.url)))},sourceApprovalGranted:false,fullHostApproved:false};
  fs.writeFileSync(path.join(evidenceDir,'FINITE-GATES.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
if(process.argv[1]&&path.resolve(process.argv[1]).toLowerCase()===fileURLToPath(import.meta.url).toLowerCase()){
  try{const args=process.argv.slice(2);assert.deepEqual(args.filter((_,i)=>i%2===0),['--inputs','--sha256','--evidence-dir']);assert.equal(args.length,6);const result=runPlanTests({inputPin:{path:args[1],sha256:args[3]},evidenceDir:args[5]});console.log(JSON.stringify({status:result.status,checks:result.checkCount,targetExecutions:0}));}
  catch(e){console.error(e.stack);process.exitCode=2;}
}
