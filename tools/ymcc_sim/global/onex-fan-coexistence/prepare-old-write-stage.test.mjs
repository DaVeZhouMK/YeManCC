import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {prepareOldWriteStage,validateOldWriteDerivedPlan,checkedOldWritePin,OLD_WRITE_GATE} from './prepare-old-write-stage.mjs';
import {auditOldWriteMetadata} from './old-write-metadata-audit.mjs';
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
export function runOldWritePrepareTests({inputsPin,preparedDir,evidenceDir}){
  checkedOldWritePin(inputsPin);assert(path.isAbsolute(preparedDir)&&path.isAbsolute(evidenceDir)&&!fs.existsSync(evidenceDir));
  const input=read(inputsPin.path),plan=read(path.join(preparedDir,'OLD-WRITE-PREPARED-PLAN.json')),policy=read(plan.policy.path),argv=read(plan.argumentMetadata.path),reports=input.rawReports.map(p=>read(checkedOldWritePin(p).path)),analysis=auditOldWriteMetadata(reports);
  fs.mkdirSync(evidenceDir);const tests=[],test=(id,fn)=>{fn();tests.push({id,status:'PASS',producerExecuted:false});};
  test('actual-prepared-policy-argv-and-original6-9-gate',()=>{assert.equal(validateOldWriteDerivedPlan(plan,policy,argv),true);assert.deepEqual(plan.gate,OLD_WRITE_GATE);assert.equal(plan.args.length,4);assert.equal(plan.dotnetArguments.length,9);assert.equal(plan.metadataValidateArguments.length,5);assert.equal(plan.businessPassed,false);});
  test('37-identical-owned-copies-and44plus2-context-pins',()=>{assert.equal(plan.outputs.length,37);for(const p of plan.outputs){assert.equal(hash(p.source),p.sha256);assert.equal(hash(p.path),p.sha256);}assert.equal(policy.contexts[0].managed.length,44);assert.equal(policy.contexts[1].managed.length,2);});
  test('exact-OneX-hierarchy-predicate-and-critical-local-closures',()=>{assert.equal(analysis.fixedBranchProof.proven,true);assert.equal(analysis.oneXLifecycleDirectFrontier.length,0);assert.equal(analysis.syntheticDirectFrontier.length,0);assert.equal(analysis.missingReflectionBindings.length,0);assert(analysis.excludedMsiMethods.length>0);assert(analysis.frontierCount>0);assert.equal(analysis.completeReachableGraphProven,false);});
  test('changed-hierarchy-cannot-prune-MSI',()=>{const changed=structuredClone(reports);for(const r of changed)for(const a of r.rows)if(a.identity.startsWith('HandheldCompanion,'))for(const t of a.selections)if(t.type==='HandheldCompanion.Devices.OneXPlayerX1AMD')t.baseType='HandheldCompanion.Devices.ClawA1M';const result=auditOldWriteMetadata(changed);assert.equal(result.fixedBranchProof.proven,false);assert.equal(result.excludedMsiMethods.length,0);assert(result.frontierCount>analysis.frontierCount);});
  test('changed-original-case-golden-budget-refused',()=>{for(const field of ['cases','goldenWrites','budgetMs']){const changed=structuredClone(plan);changed.gate[field]++;assert.throws(()=>validateOldWriteDerivedPlan(changed,policy,argv),e=>e.code==='OLD_WRITE_ORIGINAL_GATE_CHANGED');}});
  test('wrong-argv-or-context-refused',()=>{const p=structuredClone(plan);p.dotnetArguments[1]='validate';assert.throws(()=>validateOldWriteDerivedPlan(p,policy,argv),e=>e.code==='OLD_WRITE_EXACT_LAUNCH_ARGV');const q=structuredClone(policy);q.contexts[1].id='Default';assert.throws(()=>validateOldWriteDerivedPlan(plan,q,argv),e=>e.code==='OLD_WRITE_CONTEXT_OWNERSHIP_CHANGED');});
  test('real-HC-runtime-policy-refused',()=>{const q=structuredClone(policy);q.contexts[0].managed.find(p=>p.identity.startsWith('HandheldCompanion,')).sha256=plan.rawHc.sha256;assert.throws(()=>validateOldWriteDerivedPlan(plan,q,argv),e=>e.code==='OLD_WRITE_SYNTHETIC_HC_REQUIRED');});
  test('unknown-request-path-refused',()=>{const q=structuredClone(policy);q.contexts[1].requests[0].targetPath=path.join(path.dirname(plan.host.path),'Unknown.dll');assert.throws(()=>validateOldWriteDerivedPlan(plan,q,argv),e=>e.code==='OLD_WRITE_UNPINNED_REQUEST');});
  test('run-mode-and-wrong-input-SHA-refused-before-output',()=>{const out=path.join(evidenceDir,'not-created');assert.throws(()=>prepareOldWriteStage({mode:'run',inputsPin,outDir:out}),e=>e.code==='OLD_WRITE_ONLY_EXPLICIT_PREPARE_ALLOWED');assert.throws(()=>prepareOldWriteStage({mode:'prepare-only',inputsPin:{...inputsPin,sha256:'0'.repeat(64)},outDir:out}),e=>e.code==='OLD_WRITE_PIN_CHANGED');assert(!fs.existsSync(out));});
  test('existing-prepare-output-refused-and-target-receipts-absent',()=>{assert.throws(()=>prepareOldWriteStage({mode:'prepare-only',inputsPin,outDir:preparedDir}),e=>e.code==='OLD_WRITE_FRESH_OUTPUT_REQUIRED');for(const p of [plan.businessResult,plan.launchReceipt,plan.metadataValidateReceipt])assert(!fs.existsSync(p));});
  const result={schemaVersion:1,status:'PASS_10_FINITE_PREPARE_GATES',tests,checkCount:tests.length,newBusinessCasesExecuted:0,targetExecutions:0,metadataValidateExecuted:false,
    methods:analysis.uniqueMethods,frontier:analysis.frontierCount,oneXLifecycleLocalOpen:0,syntheticLocalOpen:0,missingReflectionBindings:0,msiExcludedMethods:analysis.excludedMsiMethods.length,
    originalGate:OLD_WRITE_GATE,originalBusinessGateExecuted:false,sourceApprovalGranted:false,fullHostApproved:false};
  fs.writeFileSync(path.join(evidenceDir,'FINITE-GATES.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
if(process.argv[1]&&path.resolve(process.argv[1]).toLowerCase()===fileURLToPath(import.meta.url).toLowerCase()){
  try{const args=process.argv.slice(2);assert.equal(args.length,8);assert.deepEqual(args.filter((_,i)=>i%2===0),['--inputs','--sha256','--prepared-dir','--evidence-dir']);const result=runOldWritePrepareTests({inputsPin:{path:args[1],sha256:args[3]},preparedDir:args[5],evidenceDir:args[7]});console.log(JSON.stringify(result));}catch(e){console.error(e.stack);process.exitCode=2;}
}
