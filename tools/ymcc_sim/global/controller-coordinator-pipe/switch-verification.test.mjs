import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {inputs,CASE_SPECS} from './adapter.mjs';
import {verifySwitchVerification,verifyLineageReceipt} from './run.mjs';
import {verifyR3ProtectedPs,R3_SPEC} from './r3-contract.mjs';

// Reuse actual frozen native+managed execution receipts, including rollback
// builds; no independent lifecycle model or synthetic positive replaces them.
const explicitPaths=process.env.VC_R3_TEST_RECEIPT_PATHS?JSON.parse(fs.readFileSync(process.env.VC_R3_TEST_RECEIPT_PATHS,'utf8')):null;
const protectedRows=new Map();
const receipts=new Map(CASE_SPECS.map(spec=>{
 const c=explicitPaths?null:inputs(spec.key),file=explicitPaths?.[spec.key]??path.join(c.native.value.root,'full-pipe/fixture-result.json');
 const r=JSON.parse(fs.readFileSync(file));assert.equal(r.cases.length,R3_SPEC.pipeCaseNames.length,'all actual fixed R3 groups required for receipt tests');
 protectedRows.set(spec.key,r.cases.find(row=>row.name==='PS-WAKE/central-protection-same-power-real-managed-pipe'));
 return [spec.key,r.cases.slice(0,-1).filter(row=>!row.name.startsWith('PS-WAKE/central-protection-')&&!row.name.startsWith('PS-WAKE/protected-boundary/'))];
}));
for(const [key,row]of protectedRows)test('actual protected same-power R3 timeline: '+key,()=>assert.doesNotThrow(()=>verifyR3ProtectedPs(row)));
for(const [key,groups] of receipts)test('actual verification receipts: '+key,()=>{
 assert.equal(groups.length,19);
 for(const group of groups)assert.doesNotThrow(()=>verifySwitchVerification(group));
});
const original=receipts.get('positive')[0];
const finals=group=>group.nativeEvents.filter(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='final');
const damage=(name,alter,expected)=>test('actual receipt damage rejected: '+name,()=>{
 const changed=structuredClone(original);alter(changed);
 assert.throws(()=>verifySwitchVerification(changed),expected);
});
damage('missing final',g=>{const row=finals(g)[0];g.nativeEvents.splice(g.nativeEvents.indexOf(row),1);},/SWITCH_FINAL_MISSING_OR_DUPLICATED/);
damage('duplicate final',g=>g.nativeEvents.push(structuredClone(finals(g)[0])),/SWITCH_FINAL_MISSING_OR_DUPLICATED/);
damage('opposite extra final',g=>{const row=structuredClone(finals(g)[0]);row.detail.outcome=row.detail.outcome==='success'?'failure':'success';g.nativeEvents.push(row);},/SWITCH_FINAL_MISSING_OR_DUPLICATED/);
damage('lost inspections',g=>{g.nativeEvents=g.nativeEvents.filter(e=>e.event!=='input-host-switch-verification'||e.detail.stage!=='inspection');},/SWITCH_INSPECTION_MISSING/);
damage('inspection relabeled final',g=>{g.nativeEvents.find(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='inspection').detail.stage='final';},/SWITCH_INSPECTION_SCOPE_MISSING|SWITCH_FINAL_MISSING_OR_DUPLICATED/);
damage('unknown stage',g=>{g.nativeEvents.find(e=>e.event==='input-host-switch-verification').detail.stage='candidate';},/SWITCH_VERIFICATION_STAGE_UNKNOWN/);
damage('unbound final outcome',g=>{const row=finals(g)[0];row.detail.outcome=row.detail.outcome==='success'?'failure':'success';},/SWITCH_FINAL_NOT_BOUND_TO_ACTUAL_VERIFICATION/);
damage('renewed original clock',g=>{g.playerLockWaits[0].before.remainingMs++;},/SWITCH_RELEASE_CHANGED_INTENT_OR_LEFT_LOCKED/);
damage('missing inspection scope',g=>{delete finals(g)[0].detail.inspectionFactsAvailable;},/SWITCH_INSPECTION_SCOPE_MISSING/);

for(const [key,groups] of receipts)test('actual managed accepted-lineage proof: '+key,()=>{
 assert.doesNotThrow(()=>verifyLineageReceipt(groups.find(g=>g.name==='VERIFICATION/current-source-lineage-real-managed-pipe')));
});
const lineageOriginal=receipts.get('positive').find(g=>g.name==='VERIFICATION/current-source-lineage-real-managed-pipe');
const lineageDamage=(name,alter,expected)=>test('actual lineage receipt damage rejected: '+name,()=>{
 const changed=structuredClone(lineageOriginal);alter(changed);
 assert.throws(()=>verifyLineageReceipt(changed),expected);
});
const lineageFinal=g=>g.nativeEvents.find(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='final'&&e.detail.requestId===g.lineageProof.verified.requestId).detail;
lineageDamage('unlocked before current ACK',g=>{g.lineageProof.withoutAck.locked=false;},/SOURCE_LINEAGE_UNLOCKED_BEFORE_CURRENT_ACK/);
lineageDamage('source lineage never advanced',g=>{g.lineageProof.newContinuity=g.lineageProof.oldContinuity;},/SOURCE_LINEAGE_DID_NOT_ADVANCE/);
lineageDamage('new ACK missing',g=>{g.lineageProof.newAck.hostSubmission='not-admitted-or-failed';},/SOURCE_LINEAGE_CURRENT_ACK_MISSING/);
lineageDamage('final borrows old source',g=>{lineageFinal(g).acceptedSourceContinuity=g.lineageProof.oldContinuity;},/SOURCE_LINEAGE_FINAL_USED_OLD_OR_FOREIGN_RECEIPT/);
lineageDamage('final borrows foreign request',g=>{lineageFinal(g).acceptedSourceRequestId++;},/SOURCE_LINEAGE_FINAL_USED_OLD_OR_FOREIGN_RECEIPT/);
lineageDamage('no unique successful ON',g=>{g.lineageProof.verified.phase='failure';},/SOURCE_LINEAGE_ON_NOT_VERIFIED/);
lineageDamage('child cleanup missing',g=>{g.managedReceipt.finalCleanupOk=false;},/SOURCE_LINEAGE_CHILD_NOT_CLEAN/);

lineageDamage('normal lineage falsely needs fallback',g=>{lineageFinal(g).fallbackAttempts=1;},/SOURCE_LINEAGE_NEEDED_FALLBACK/);
damage('missing lifecycle version',g=>{delete finals(g)[0].detail.lifecycleVersion;},/SWITCH_LIFECYCLE_CONTRACT_MISSING/);
damage('extra fallback attempt',g=>{finals(g)[0].detail.fallbackAttempts=2;},/SWITCH_LIFECYCLE_CONTRACT_MISSING/);

const ownedFaultOriginal=[...receipts.get('positive'),protectedRows.get('positive')].find(g=>g.nativeEvents.some(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='inspection'&&e.detail.state==='classified-owned-endpoint-fault'));
test('actual owned-endpoint fault never borrows an unrelated classification evidence',()=>{
 assert(ownedFaultOriginal,'real controlled owned endpoint fault receipt is required');
 const changed=structuredClone(ownedFaultOriginal);changed.nativeEvents.find(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='inspection'&&e.detail.state==='classified-owned-endpoint-fault').detail.evidence='foreign-or-unbound-observation';
 assert.throws(()=>verifySwitchVerification(changed),/SWITCH_INSPECTION_INVALID/);
});
