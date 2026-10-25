import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import {getSleepNativeRecoveryConsumerAdapters,moduleSources,REPO,MODULE}from'./adapter.mjs';
test('adapter retains both original consumers, exact boundary sources and a structured result',()=>{const [row]=getSleepNativeRecoveryConsumerAdapters();for(const f of['tools/sleep_recovery_api_selftest.py','tools/sleep_wake_resume_selftest.py','native/main.cpp','native/wake_game_recovery.h','native/sleep_recovery_api.h'])assert(row.evidenceInputs.includes(f));assert(row.evidenceInputs.includes(MODULE+'/boundary.h'));assert.equal(row.resultFiles.length,1);assert(row.artifactInputs.length>0);});
test('private source fault targets the original confirmation policy once',()=>{const spec=JSON.parse(fs.readFileSync(REPO+'/'+MODULE+'/spec.json','utf8')),source=fs.readFileSync(REPO+'/'+spec.fault.path,'utf8');assert.equal(source.split(spec.fault.before).length,2);assert.notEqual(spec.fault.before,spec.fault.after);assert.equal(spec.ioReview.originalAssertionsChanged,true);assert.equal(spec.ioReview.originalPolicyAssertionsChanged,false);});
test('all module sources are explicitly listed and exist',()=>{for(const relative of moduleSources())assert(fs.statSync(REPO+'/'+relative).isFile());});

import os from 'node:os';
import path from 'node:path';
import {allocateScratch,verifyScratch,archiveScratch,cleanupScratch} from './scratch.mjs';
test('short execution root binds the exact case, copies bytes, and cleans only after archive validation',()=>{
 const evidence=fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-sr-evidence-'));
 const record=allocateScratch(evidence),root=verifyScratch(record,evidence);
 assert(root.length<=100);assert.equal(path.dirname(root),fs.realpathSync(os.tmpdir()));
 const payload=path.join(root,'SleepResumeRetry-20261003');fs.mkdirSync(payload);
 const raw=path.join(payload,'original-log.json');const bytes=Buffer.from(JSON.stringify({originalRoot:root,meaning:'paths remain literal'})+'\n');fs.writeFileSync(raw,bytes);
 assert.throws(()=>verifyScratch({...record,nonce:'0'.repeat(32)},evidence),/OWNERSHIP|MARKER/);
 assert.throws(()=>verifyScratch(record,path.dirname(evidence)),/OWNERSHIP/);
 assert.throws(()=>verifyScratch({...record,root:evidence},evidence),/OWNERSHIP/);
 const marker=path.join(root,'.ymcc-sleep-owner.json'),saved=fs.readFileSync(marker);fs.writeFileSync(marker,'{}');
 assert.throws(()=>verifyScratch(record,evidence),/MARKER/);fs.writeFileSync(marker,saved);
 const link=path.join(root,'unrequested-junction');fs.symlinkSync(evidence,link,'junction');
 assert.throws(()=>archiveScratch(record,evidence),/REPARSE/);fs.unlinkSync(link);
 // The failed archive left only its empty destination; the owner root remains intact.
 assert.equal(fs.existsSync(root),true);fs.rmdirSync(path.join(evidence,'execution'));
 const mapping=archiveScratch(record,evidence),row=mapping.files.find(x=>x.originalPath===raw);assert(row);
 assert.deepEqual(fs.readFileSync(row.archivedPath),bytes);assert.equal(JSON.parse(fs.readFileSync(row.archivedPath,'utf8')).originalRoot,root);
 assert.throws(()=>cleanupScratch(record,evidence,{...mapping,evidenceRoot:path.dirname(evidence)}),/BINDING/);
 const archivedDir=path.dirname(row.archivedPath),relocated=path.join(evidence,'relocated-archive');fs.renameSync(archivedDir,relocated);fs.symlinkSync(relocated,archivedDir,'junction');assert.throws(()=>cleanupScratch(record,evidence,mapping),/REPARSE/);assert.equal(fs.existsSync(root),true);fs.unlinkSync(archivedDir);fs.renameSync(relocated,archivedDir);
 fs.writeFileSync(row.archivedPath,'tampered');assert.throws(()=>cleanupScratch(record,evidence,mapping),/MISMATCH/);assert.equal(fs.existsSync(root),true);fs.writeFileSync(row.archivedPath,bytes);
 const cleanup=cleanupScratch(record,evidence,mapping);assert.equal(cleanup.cleaned,true);assert.equal(fs.existsSync(root),false);
 // Remove only this exact verified test evidence, never the archive's recorded original root.
 assert.equal(path.dirname(evidence),fs.realpathSync(os.tmpdir()));assert(path.basename(evidence).startsWith('ymcc-sr-evidence-'));assert.equal(fs.lstatSync(evidence).isSymbolicLink(),false);fs.rmSync(evidence,{recursive:true,force:false});
});

import {verifyReviewedOriginalScripts} from './run.mjs';
test('reviewed API maintenance recovers all exact historical bytes and actual owner-proof consumer',()=>{
 const spec=JSON.parse(fs.readFileSync(REPO+'/'+MODULE+'/spec.json','utf8')),review=verifyReviewedOriginalScripts(spec);
 assert.equal(review.exactHistoricalRemainderVerified,true);assert.equal(review.priorVersion1Review.onlyAddedMemorySnapshotDeclaration,true);assert.equal(review.exactReverseDeltaVerified,true);assert.equal(review.wakeFixtureMaintainedForCurrentSource,true);assert.equal(review.historicalWakeBytesUnchanged,false);assert.equal(review.realOwnerProofCalculationCovered,false);
 assert.equal(review.priorVersion1Review.historicalApiSha256,'332bc485c61d2083afd36d429580f4e90698345912bcb889819181c88ef7f4c6');
 assert.equal(review.historicalWakeSha256,'88a8f3b95e1b64b4d4bd873c9401e0d247091953b2a348a9fab0c07f984d0f13');
});
test('hash resigning or altered original assertion is refused by the exact maintenance contract',()=>{
 const spec=JSON.parse(fs.readFileSync(REPO+'/'+MODULE+'/spec.json','utf8')),copy=structuredClone(spec);copy.originalScripts[0].sha256='0'.repeat(64);
 assert.throws(()=>verifyReviewedOriginalScripts(copy),/REVIEWED_SCRIPT_SET_CHANGED/);
 const read=relative=>{const bytes=fs.readFileSync(REPO+'/'+relative);return relative==='tools/sleep_recovery_api_selftest.py'?Buffer.concat([bytes,Buffer.from('\n# unreviewed\n')]):bytes;};
 assert.throws(()=>verifyReviewedOriginalScripts(spec,read),/ORIGINAL_SCRIPT_NEEDS_REVIEW/);
 const changed=structuredClone(spec);changed.scriptCompatibilityReview.exactDifferenceSha256='0'.repeat(64);assert.throws(()=>verifyReviewedOriginalScripts(changed),/REVIEW_CONTRACT_CHANGED/);
});
test('missing real power-binding consumer cannot be accepted by a matching fixture stub',()=>{
 const spec=JSON.parse(fs.readFileSync(REPO+'/'+MODULE+'/spec.json','utf8'));
 assert.throws(()=>verifyReviewedOriginalScripts(spec,relative=>{const bytes=fs.readFileSync(REPO+'/'+relative);return relative==='native/sleep_recovery_api.h'?Buffer.from(bytes.toString('utf8').replaceAll('powerBindingProofGeneration','wrongProof')):bytes;}),/OWNER_PROOF_CONSUMER_NEEDS_REVIEW/);
});

test('version 1 compatibility still checks the exact inserted block and historical remainder',()=>{
 const manifest=JSON.parse(fs.readFileSync(REPO+'/'+MODULE+'/script-maintenance-v2.json','utf8'));
 const prior=new Map();for(const delta of manifest.deltas){const current=fs.readFileSync(REPO+'/'+delta.path),parts=[];let cursor=0;for(const edit of delta.edits){parts.push(current.subarray(cursor,edit.afterOffset),Buffer.from(edit.beforeBase64,'base64'));cursor=edit.afterOffset+Buffer.from(edit.afterBase64,'base64').length;}parts.push(current.subarray(cursor));prior.set(delta.path,Buffer.concat(parts));}
 const oldSpec={originalScripts:manifest.priorOriginalScripts,scriptCompatibilityReview:manifest.priorVersion1Review},read=relative=>prior.get(relative)??fs.readFileSync(REPO+'/'+relative);
 const review=verifyReviewedOriginalScripts(oldSpec,read);assert.equal(review.schemaVersion,1);assert.equal(review.exactHistoricalRemainderVerified,true);
 const altered=structuredClone(oldSpec);altered.scriptCompatibilityReview.addedBlock+='/* extra */';assert.throws(()=>verifyReviewedOriginalScripts(altered,read),/REVIEW_BLOCK_CHANGED/);
});
test('version 2 rejects an altered wake consumer, policy assertion or exact difference file',()=>{
 const spec=JSON.parse(fs.readFileSync(REPO+'/'+MODULE+'/spec.json','utf8'));
 for(const target of ['tools/sleep_wake_resume_selftest.py',spec.scriptCompatibilityReview.exactDifferencePath]){
  assert.throws(()=>verifyReviewedOriginalScripts(spec,relative=>{const bytes=fs.readFileSync(REPO+'/'+relative);return relative===target?Buffer.concat([bytes,Buffer.from('\n# unreviewed')]):bytes;}),/ORIGINAL_SCRIPT_NEEDS_REVIEW|REVIEW_DELTA_HASH_CHANGED/);
 }
 const source=fs.readFileSync(REPO+'/tools/sleep_recovery_api_selftest.py','utf8');assert(source.includes(spec.fault.expectedAssertion));
 const tampered=Buffer.from(source.replace(spec.fault.expectedAssertion,spec.fault.expectedAssertion.replace('false','true')));
 assert.throws(()=>verifyReviewedOriginalScripts(spec,relative=>relative==='tools/sleep_recovery_api_selftest.py'?tampered:fs.readFileSync(REPO+'/'+relative)),/ORIGINAL_SCRIPT_NEEDS_REVIEW/);
});
test('version 2 requires the actual current success and deletion gates in the production consumer',()=>{
 const spec=JSON.parse(fs.readFileSync(REPO+'/'+MODULE+'/spec.json','utf8'));
 for(const gate of ['if (accepted && threads.allRunning()) {','if (removed && !ec) {','if (final || pending == 0) {']){
  assert.throws(()=>verifyReviewedOriginalScripts(spec,relative=>{const bytes=fs.readFileSync(REPO+'/'+relative);return relative==='native/wake_game_recovery.h'?Buffer.from(bytes.toString('utf8').replace(gate,'if (true) {')):bytes;}),/EARLY_COMPLETION_CONSUMER_NEEDS_REVIEW/);
 }
});
