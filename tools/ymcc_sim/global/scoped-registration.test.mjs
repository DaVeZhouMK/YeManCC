import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {getControllerCoordinatorPipeAdapters,CASE_SPECS} from './controller-coordinator-pipe/adapter.mjs';
import {getGpuMemoryAdapters} from './gpu-memory-signals/adapter.mjs';
import {getSignalAdapters} from './signal-adapters.mjs';
const repo=path.resolve(import.meta.dirname,'../../..');
const evidence='tools/ymcc_sim/global/scoped-registration.test.mjs';
const declared={artifactInputs:[],blockedReason:'EXACT_SELECTED_REVIEW_REQUIRED'};

test('default controller registration still admits every original case',()=>{
 const seen=[];const rows=getControllerCoordinatorPipeAdapters({readSources:key=>{seen.push('S:'+key);return[evidence];},readArtifacts:key=>{seen.push('A:'+key);return[];}});
 assert.deepEqual(rows.map(r=>r.id),CASE_SPECS.map(r=>r.id));assert.equal(seen.length,8);assert(rows.every(r=>!r.inputRegistrationDeferred));
});
test('an unrelated explicit test never reads controller prerequisites and still retains all IDs',()=>{
 const rows=getControllerCoordinatorPipeAdapters({selectedIds:['network.http-deadline'],readSources:()=>{throw Error('UNREQUESTED_SOURCE_READ');},readArtifacts:()=>{throw Error('UNREQUESTED_ARTIFACT_READ');}});
 assert.deepEqual(rows.map(r=>r.id),CASE_SPECS.map(r=>r.id));assert(rows.every(r=>r.inputRegistrationDeferred&&r.blockedReason==='COORDINATOR_INPUTS_DEFERRED_OUTSIDE_EXPLICIT_SELECTION'&&r.artifactInputs.length===0));
});
for(const spec of CASE_SPECS)test('selecting controller '+spec.key+' preserves its complete original descriptor and validation calls',()=>{
 const io={readSources:()=>[evidence],readArtifacts:()=>[]},full=getControllerCoordinatorPipeAdapters(io),seen=[];
 const selected=getControllerCoordinatorPipeAdapters({selectedIds:[spec.id],readSources:key=>{seen.push('S:'+key);return[evidence];},readArtifacts:key=>{seen.push('A:'+key);return[];}});
 assert.deepEqual(selected.find(r=>r.id===spec.id),full.find(r=>r.id===spec.id));assert.deepEqual(seen,['S:'+spec.key,'A:'+spec.key]);assert(selected.filter(r=>r.id!==spec.id).every(r=>r.inputRegistrationDeferred));
});
test('a selected controller authority failure remains blocked with the original reason',()=>{
 const row=getControllerCoordinatorPipeAdapters({selectedIds:[CASE_SPECS[0].id],readSources:()=>{throw Error('ORIGINAL_AUTHORITY_REJECTION');},readArtifacts:()=>{throw Error('UNREACHABLE');}})[0];
 assert.equal(row.blockedReason,'COORDINATOR_PREREQUISITE_UNAVAILABLE: ORIGINAL_AUTHORITY_REJECTION');assert(!row.inputRegistrationDeferred);
});
test('lightweight/unrelated selections do not require GPU workflow inputs, while selected/default guards remain exact',()=>{
 const full=getGpuMemoryAdapters(repo,{readTools:()=>structuredClone(declared)});
 for(const id of ['network.http-deadline','performance.gpu-log-signals']){const rows=getGpuMemoryAdapters(repo,{selectedIds:[id],readTools:()=>{throw Error('UNREQUESTED_GPU_TOOLS');}});assert(rows[0].inputRegistrationDeferred);assert.deepEqual(rows[1],full[1]);assert.deepEqual(rows.map(r=>r.id),full.map(r=>r.id));}
 assert.deepEqual(getGpuMemoryAdapters(repo,{selectedIds:['performance.gpu-dedicated-memory-workflow'],readTools:()=>structuredClone(declared)}),full);
});
test('native radial tools are scoped, both selected runtime guards and the development action host remain original',()=>{
 let calls=0;const read=()=>{calls++;return structuredClone(declared);};const full=getSignalAdapters(repo,{readRadialTools:read});assert.equal(calls,2);
 const unrelated=getSignalAdapters(repo,{selectedIds:['radial.action-host-development'],readRadialTools:()=>{throw Error('UNREQUESTED_NATIVE_UI_TOOLS');}});assert.deepEqual(unrelated.find(r=>r.id==='radial.action-host-development'),full.find(r=>r.id==='radial.action-host-development'));
 for(const id of ['radial.native-controls','radial.monitor-handoff']){assert(unrelated.find(r=>r.id===id).inputRegistrationDeferred);calls=0;const rows=getSignalAdapters(repo,{selectedIds:[id],readRadialTools:read});assert.equal(calls,1);assert.deepEqual(rows.find(r=>r.id===id),full.find(r=>r.id===id));}
 assert.deepEqual(unrelated.map(r=>r.id),full.map(r=>r.id));
});
test('malformed scope selectors cannot silently bypass registration input gates',()=>{
 for(const selectedIds of [null,'name',[1],['']]){
  assert.throws(()=>getControllerCoordinatorPipeAdapters({selectedIds}),/COORDINATOR_SELECTION_SHAPE/);
  assert.throws(()=>getGpuMemoryAdapters(repo,{selectedIds}),/GPU_SELECTION_SHAPE/);
  assert.throws(()=>getSignalAdapters(repo,{selectedIds}),/SIGNAL_SELECTION_SHAPE/);
 }
});
import {getControllerGameFeedbackWriterConsumerAdapters as getWriter,CASE_ID as writerId} from './controller-game-feedback-writer-consumer/adapter.mjs';
test('unrelated writer registration defers inputs while default and selected writer keep original rejection',()=>{
 const reads={readSources:()=>[evidence],readArtifacts:()=>[]},full=getWriter(reads);
 const unrelated=getWriter({selectedIds:['lighting.signal-consumer'],readSources:()=>{throw Error('UNREQUESTED_WRITER_SOURCE');},readArtifacts:()=>{throw Error('UNREQUESTED_WRITER_TOOL');}});
 assert.deepEqual(unrelated.map(row=>row.id),full.map(row=>row.id));assert(unrelated[0].inputRegistrationDeferred);assert.deepEqual(unrelated[0].artifactInputs,[]);
 assert.deepEqual(getWriter({...reads,selectedIds:[writerId]}),full);
 for(const selectedIds of [undefined,[writerId]])assert.throws(()=>getWriter({selectedIds,readSources:()=>[evidence],readArtifacts:()=>{throw Error('WRITER_TOOL_CLOSURE_EXCEEDS1024');}}),/WRITER_TOOL_CLOSURE_EXCEEDS1024/);
 for(const selectedIds of [null,'writer',[1],['']])assert.throws(()=>getWriter({selectedIds}),/WRITER_SELECTION_SHAPE/);
});

import {registrationIndexCurrent} from './extra-adapter-assembly.mjs';
import fsIndex from 'node:fs';import {createHash as indexHash} from 'node:crypto';
const indexFixtureRoot='G:/YeManCC-Work/Testing/YMCC/Evidence/Formal037-20261030/RegistrationIndex';
fsIndex.mkdirSync(indexFixtureRoot,{recursive:true});
const indexFixture=fsIndex.mkdtempSync(path.join(indexFixtureRoot,'case-')),indexFile=path.join(indexFixture,'source.txt');
const indexDigest=text=>indexHash('sha256').update(text).digest('hex');
test('registration identity accepts only declared LF/CRLF equivalence and rejects changed source bytes',()=>{
 fsIndex.writeFileSync(indexFile,'alpha\r\nbeta\r\n');
 const index={sources:[{path:'source.txt',sha256:indexDigest('alpha\nbeta\n'),contentNormalization:'utf8-lf'}]};
 assert(registrationIndexCurrent(index,indexFixture));
 fsIndex.writeFileSync(indexFile,'alpha\nbeta\n');assert(registrationIndexCurrent(index,indexFixture));
 fsIndex.writeFileSync(indexFile,'alpha\nzeta\n');assert.equal(registrationIndexCurrent(index,indexFixture),false);
});
test('legacy raw registration pins remain byte-exact and unknown normalizations fail closed',()=>{
 fsIndex.writeFileSync(indexFile,'alpha\r\nbeta\r\n');
 const legacy={sources:[{path:'source.txt',sha256:indexDigest('alpha\nbeta\n')}]};
 assert.equal(registrationIndexCurrent(legacy,indexFixture),false);
 legacy.sources[0].sha256=indexDigest('alpha\r\nbeta\r\n');assert(registrationIndexCurrent(legacy,indexFixture));
 legacy.sources[0].contentNormalization='ignore-content';assert.equal(registrationIndexCurrent(legacy,indexFixture),false);
 legacy.sources[0].path='missing.txt';assert.equal(registrationIndexCurrent(legacy,indexFixture),false);
});
