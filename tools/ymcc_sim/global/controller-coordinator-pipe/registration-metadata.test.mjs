import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {getControllerCoordinatorPipeAdapters,CASE_ID} from './adapter.mjs';
import {validateAdapters,executeAdapter} from '../runner.mjs';
import {auditRegisteredScope} from '../receipt-audit.mjs';

function unavailable(message='MANAGED_BINDINGS_INVALID') {
 let artifactsCalled=false;
 const [row]=getControllerCoordinatorPipeAdapters({readSources(){throw Error(message);},readArtifacts(){artifactsCalled=true;return [];}});
 assert.equal(artifactsCalled,false);
 return row;
}
function reportFor(adapter) {
 return {schemaVersion:1,status:'CURRENT_RECEIPTS_VALID',diagnostics:[],cases:[{id:adapter.id,status:'PASS',caseDir:'D:/unit-fixture-only',structuredEvidence:(adapter.resultFiles??[]).map(value=>({path:path.resolve('D:/unit-fixture-only',value)})),execution:{source:adapter.path,scope:adapter.scope,sourceInputs:[adapter.path,...adapter.evidenceInputs]}}],summary:{cases:1,currentPass:1,stale:0,blocked:0,failed:0,referenceOnly:0},needsRerun:[]};
}

test('unavailable managed metadata retains the exact registered case and error',()=>{
 const row=unavailable();
 assert.equal(row.id,CASE_ID);
 assert.equal(row.kind,'node');
 assert.equal(row.path,'tools/ymcc_sim/global/controller-coordinator-pipe/run.mjs');
 assert.match(row.blockedReason,/MANAGED_BINDINGS_INVALID/);
 assert.deepEqual(row.artifactInputs,[]);
 assert(row.evidenceInputs.includes('tools/ymcc_sim/global/controller-lifecycle-pipe/managed-fixture.json'));
});

test('normal metadata retains all concrete source and artifact inputs',()=>{
 const source=['native/main.cpp'],artifacts=[path.resolve('D:/unit-artifact-only.dll')];
 const [row]=getControllerCoordinatorPipeAdapters({readSources:()=>source,readArtifacts:()=>artifacts});
 assert.strictEqual(row.evidenceInputs,source);assert.strictEqual(row.artifactInputs,artifacts);
 assert.equal(row.blockedReason,undefined);
});

test('an artifact metadata failure is also contained and preserves its error',()=>{
 const [row]=getControllerCoordinatorPipeAdapters({readSources:()=>['native/main.cpp'],readArtifacts(){throw Error('MANAGED_CONTRACT_SCOPE_INVALID');}});
 assert.match(row.blockedReason,/MANAGED_CONTRACT_SCOPE_INVALID/);
 assert.equal(row.id,CASE_ID);
});

test('unavailable leaf is BLOCKED without execution and an unrelated leaf actually runs',async()=>{
 const ownedParent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(ownedParent,'ymcc-registration-isolation-'));
 // Keep this small owned regression fixture for inspection; no product process runs.
 fs.writeFileSync(path.join(root,'unrelated.mjs'),"console.log('actual unrelated runner executed');\n");
 const blocked=unavailable(),unrelated={id:'regression.unrelated',domain:'regression',title:'Actual isolated framework child',kind:'node',path:'unrelated.mjs',args:[],timeoutMs:5000,profiles:['desktop','full'],scope:'behavioral-mock',evidenceInputs:[]};
 assert.doesNotThrow(()=>validateAdapters([blocked,unrelated],root));
 const opts={repoRoot:root,runDir:path.join(root,'evidence')};
 const first=await executeAdapter(blocked,opts);assert.equal(first.status,'BLOCKED');assert.equal(first.pid,undefined);assert.match(first.reason,/MANAGED_BINDINGS_INVALID/);
 const second=await executeAdapter(unrelated,opts);assert.equal(second.status,'PASS');assert.equal(second.exitCode,0);assert(second.pid);
 assert.match(fs.readFileSync(path.join(second.evidenceDir,'stdout.log'),'utf8'),/actual unrelated runner executed/);
});

test('a prior PASS cannot certify a currently unavailable selected prerequisite',()=>{
 const row=unavailable(),report=reportFor(row);
 const audited=auditRegisteredScope(report,[row],{profile:'full',tests:[row.id]});
 assert.equal(audited.cases[0].status,'STALE_REGISTRATION');
 assert(audited.cases[0].registrationMismatches.some(value=>value.code==='REGISTERED_PREREQUISITE_UNAVAILABLE'&&value.reason.includes('MANAGED_BINDINGS_INVALID')));
 assert.equal(audited.summary.currentPass,0);assert.equal(audited.summary.registeredNotPassed,1);
 assert.equal(audited.registeredScope.complete,false);
});

test('an unavailable unselected leaf does not poison an unrelated receipt audit',()=>{
 const unavailableRow=unavailable(),other={id:'regression.current',domain:'regression',profiles:['full'],path:'actual.mjs',scope:'behavioral-mock',evidenceInputs:[]};
 const audited=auditRegisteredScope(reportFor(other),[unavailableRow,other],{profile:'full',tests:[other.id]});
 assert.equal(audited.cases[0].status,'PASS');assert.equal(audited.registeredScope.complete,true);
 assert.deepEqual(audited.registeredScope.ids,[other.id]);
});