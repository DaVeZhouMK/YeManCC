import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {loadCurrentCandidateReviewDocuments,reviewCurrentCandidateDocuments,bindCurrentCandidatePlan} from './current-candidate-branch.mjs';
// Finite in-memory mutations of authenticated raw metadata, never target code.
const a=process.argv.slice(2);
assert.equal(a.length,6);assert.deepEqual(a.filter((_,i)=>i%2===0),['--inputs','--sha256','--out']);
assert(path.isAbsolute(a[5])&&!fs.existsSync(a[5]));
const inputPin={path:a[1],sha256:a[3]},docs=loadCurrentCandidateReviewDocuments(inputPin),rows=[];
function gate(id,expected,mutate,execute=reviewCurrentCandidateDocuments){
 const d=structuredClone(docs);mutate?.(d);let actual='PASS',detail=null;
 try{execute(d);}catch(e){actual=e.code??e.name;detail=e.detail??e.message;}
 rows.push({id,expected,actual,passed:actual===expected,detail});assert.equal(actual,expected,id);
}
function mutateMethod(d,part,fn){let count=0;for(const r of d.reports)for(const i of r.rows)for(const t of i.selections)for(const m of t.methods)if(m.method.includes(part)){fn(m);count++;}assert(count>0,part);}
gate('real-current-default-documents-and-parent-plan','PASS',null,d=>{const r=reviewCurrentCandidateDocuments(d);assert.equal(r.definiteDeviceRegistryForeignProcessIoRisks.length,0);assert.equal(r.requiredReflectionIncompatibilities.length,0);bindCurrentCandidatePlan(d);});
gate('old7985-cannot-substitute-current-host','CANDIDATE_PARENT_IMAGE_CHANGED',d=>{d.plan.host.sha256='7985'.padEnd(64,'0');},bindCurrentCandidatePlan);
gate('rawHC-cannot-become-runtimeHC','CANDIDATE_RUNTIME_SYNTHETIC_ONLY',d=>{d.policy.contexts.find(c=>c.id==='Default').managed.find(p=>p.identity.startsWith('HandheldCompanion,')).sha256=d.input.images.rawHc.sha256;},bindCurrentCandidatePlan);
gate('same-name-unregistered-ALC-rejected','CANDIDATE_EXACT_CUSTOM_CONTEXT',d=>{d.policy.contexts[1].id='Harness.ProductContext|UNREGISTERED';},bindCurrentCandidatePlan);
gate('initialization-true-flag-rejected','CANDIDATE_INITIALIZE_FALSE_FALSE_CHANGED',d=>mutateMethod(d,'::<LoadAssemblyAndFactory>b__574_6(',m=>{m.instructions.find(i=>i.offset===19).opcode='ldc.i4.1';}));
gate('fresh-monitor-timer-intrusion-rejected','CANDIDATE_FRESH_MONITOR_RESOURCE_INTRUSION',d=>mutateMethod(d,'HWiNFOTemperatureMonitor::.ctor(',m=>{m.instructions.push({offset:1000,opcode:'stfld',operand:'System.Threading.Timer YeManFanHost.HWiNFOTemperatureMonitor::timer [scope=YeManFanHost.dll]',operandToken:null});}));
gate('watchdog-null-guard-removal-rejected','CANDIDATE_NULL_WATCHDOG_RETURN_CHANGED',d=>mutateMethod(d,'::RequestPowerProfileWatchdogTick(',m=>{m.instructions[3].opcode='nop';}));
gate('mark-reopen-cannot-arm-timer','CANDIDATE_REOPEN_MARK_SCHEDULE_INTRUSION',d=>mutateMethod(d,'::MarkReopenRequiredLocked(',m=>{m.calls.push({offset:1000,opcode:'call',target:'System.Void YeManFanHost.FanHostEngine::ScheduleRecoveryRetry() [scope=YeManFanHost.dll]',token:100667805});}));
gate('original-candidate60-budget-preserved','CANDIDATE_ORIGINAL_GATE_CHANGED',d=>{d.plan.gate.candidateBudgetMs=45000;},bindCurrentCandidatePlan);
gate('changed-OneX-reflection-field-contract-rejected','CANDIDATE_FIXTURE_FIELD_MISMATCH',d=>{let count=0;for(const r of d.reports)for(const i of r.rows)for(const t of i.selections)if(t.type==='HandheldCompanion.Devices.OneXPlayerX1'){t.fields.find(f=>f.name==='_vendorHidMonitor').type='Unapproved.Device.Handle';count++;}assert(count>0);});
const result={schemaVersion:1,status:'PASS_FINITE_METADATA_GATES',input:inputPin,gates:rows.length,passed:rows.filter(r=>r.passed).length,rows,policyGenerated:false,producerExecuted:false,targetExecuted:false,targetAssembliesLoaded:0,oldTestsRecounted:0};
fs.writeFileSync(a[5],JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({status:result.status,gates:result.gates,passed:result.passed,targetExecuted:false}));
