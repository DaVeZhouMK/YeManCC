import test,{after} from 'node:test';
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
 const guarded=['radial.native-controls','radial.monitor-handoff'];
 const hosts=['radial.action-host-development','radial.action-host'];
 let calls=0,tools={artifactInputs:[path.join(repo,evidence)]};
 let hostCalls=0,hostTools={artifactInputs:[path.join(repo,evidence)]};
 const register=options=>getSignalAdapters(repo,{...options,readRadialHostTools:()=>{hostCalls++;return hostTools;}});
 const read=()=>{calls++;return tools;};const full=register({readRadialTools:read});assert.equal(calls,1);assert.equal(hostCalls,1);
 const artifactLists=guarded.map(id=>full.find(r=>r.id===id).artifactInputs);
 assert.deepEqual(artifactLists,[tools.artifactInputs,tools.artifactInputs]);
 assert.notEqual(artifactLists[0],artifactLists[1]);assert.notEqual(artifactLists[0],tools.artifactInputs);assert.notEqual(artifactLists[1],tools.artifactInputs);
 const hostLists=hosts.map(id=>full.find(r=>r.id===id).artifactInputs);
 assert.deepEqual(hostLists,[hostTools.artifactInputs,hostTools.artifactInputs]);
 assert.notEqual(hostLists[0],hostLists[1]);assert.notEqual(hostLists[0],hostTools.artifactInputs);assert.notEqual(hostLists[1],hostTools.artifactInputs);
 const unrelated=register({selectedIds:['radial.action-host-development'],readRadialTools:()=>{throw Error('UNREQUESTED_NATIVE_UI_TOOLS');}});assert.equal(hostCalls,2);assert.deepEqual(unrelated.find(r=>r.id==='radial.action-host-development'),full.find(r=>r.id==='radial.action-host-development'));
 for(const id of guarded){assert(unrelated.find(r=>r.id===id).inputRegistrationDeferred);calls=0;const rows=register({selectedIds:[id],readRadialTools:read});assert.equal(calls,1);assert.deepEqual(rows.find(r=>r.id===id),full.find(r=>r.id===id));}
 assert.deepEqual(unrelated.map(r=>r.id),full.map(r=>r.id));
 tools=declared;hostTools={artifactInputs:[],blockedReason:'ORIGINAL_HOST_AUTHORITY_REJECTION'};
 calls=0;hostCalls=0;const blocked=register({selectedIds:guarded,readRadialTools:read});assert.equal(calls,1);assert.equal(hostCalls,1);
 for(const id of guarded){const row=blocked.find(r=>r.id===id);assert.equal(row.blockedReason,declared.blockedReason);assert.deepEqual(row.artifactInputs,[]);assert(!row.inputRegistrationDeferred);}
 for(const id of hosts){const row=blocked.find(r=>r.id===id);assert.equal(row.blockedReason,hostTools.blockedReason);assert.deepEqual(row.artifactInputs,[]);assert(!row.inputRegistrationDeferred);}
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
import {simulationCorePath} from './release-host-environment.mjs';
import fsIndex from 'node:fs';import {createHash as indexHash} from 'node:crypto';
const indexPrefix='scoped-registration-index-';
const samePhysicalPath=(actual,expected)=>process.platform==='win32'?actual.toLowerCase()===expected.toLowerCase():actual===expected;
let indexOwned,indexFailed=false;
function indexTest(name,run){
 test(name,t=>{
  let passed=false;
  try{
   if(!indexOwned){
    const workspace=simulationCorePath('Workspace'),parent=fsIndex.lstatSync(workspace);
    assert(parent.isDirectory()&&!parent.isSymbolicLink()&&samePhysicalPath(fsIndex.realpathSync.native(workspace),workspace));
    const root=fsIndex.mkdtempSync(path.join(workspace,indexPrefix));
    indexOwned={workspace,root,created:fsIndex.lstatSync(root,{bigint:true})};
   }
   run(indexOwned.root,path.join(indexOwned.root,'source.txt'));
   passed=true;
  }finally{if(!passed||t.signal.aborted)indexFailed=true;}
 });
}
after(()=>{
 if(!indexOwned)return;
 const {workspace,root,created}=indexOwned;
 if(indexFailed){console.error('Retained failed registration index controls: '+root);return;}
 assert.equal(path.dirname(path.resolve(root)),workspace);assert(path.basename(root).startsWith(indexPrefix));
 const current=fsIndex.lstatSync(root,{bigint:true});
 assert(current.isDirectory()&&!current.isSymbolicLink()&&current.dev===created.dev&&current.ino===created.ino);
 assert(samePhysicalPath(fsIndex.realpathSync.native(root),root));
 fsIndex.unlinkSync(path.join(root,'source.txt'));
 fsIndex.rmdirSync(root);
});
const indexDigest=text=>indexHash('sha256').update(text).digest('hex');
indexTest('registration identity accepts only declared LF/CRLF equivalence and rejects changed source bytes',(indexFixture,indexFile)=>{
 fsIndex.writeFileSync(indexFile,'alpha\r\nbeta\r\n');
 const index={sources:[{path:'source.txt',sha256:indexDigest('alpha\nbeta\n'),contentNormalization:'utf8-lf'}]};
 assert(registrationIndexCurrent(index,indexFixture));
 fsIndex.writeFileSync(indexFile,'alpha\nbeta\n');assert(registrationIndexCurrent(index,indexFixture));
 fsIndex.writeFileSync(indexFile,'alpha\nzeta\n');assert.equal(registrationIndexCurrent(index,indexFixture),false);
});
indexTest('legacy raw registration pins remain byte-exact and unknown normalizations fail closed',(indexFixture,indexFile)=>{
 fsIndex.writeFileSync(indexFile,'alpha\r\nbeta\r\n');
 const legacy={sources:[{path:'source.txt',sha256:indexDigest('alpha\nbeta\n')}]};
 assert.equal(registrationIndexCurrent(legacy,indexFixture),false);
 legacy.sources[0].sha256=indexDigest('alpha\r\nbeta\r\n');assert(registrationIndexCurrent(legacy,indexFixture));
 legacy.sources[0].contentNormalization='ignore-content';assert.equal(registrationIndexCurrent(legacy,indexFixture),false);
 legacy.sources[0].path='missing.txt';assert.equal(registrationIndexCurrent(legacy,indexFixture),false);
});
