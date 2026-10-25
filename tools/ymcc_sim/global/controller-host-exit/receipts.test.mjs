import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {test} from 'node:test';
import {REPO,contract,sourceInputs,getControllerHostExitAdapters,artifactRows} from './adapter.mjs';
import {toolInputs} from '../controller-signal-evidence/adapter.mjs';
import {validateHelper,validateParent,validateGates,validateNativeExit} from './receipts.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
test('native exit registry names all three actual producers and preserves historical negative identity',()=>{const a=getControllerHostExitAdapters()[0];assert.equal(a.id,'controller.host-exit-owned');assert.equal(a.blockedReason,undefined);assert.equal(a.resultFiles[0],'host-exit-owned/result.json');assert.equal(new Set(sourceInputs).size,sourceInputs.length);assert(sourceInputs.every(n=>fs.existsSync(path.join(REPO,n))));assert(artifactRows().length<3072);assert.equal(read(contract.historicalFailure.path).status,'ACTUAL_NATIVE_EXIT_PARENT_CONTRACT_FAIL_REPRODUCED');});
const file=process.env.YMCC_NATIVE_HOST_EXIT_RECEIPT;
if(file){const r=read(file),ctx={repo:REPO,sources:r.sourceHashes,tools:toolInputs(REPO),sourceMode:'captured-execution-only'},p=read(r.children.parent.receipt.path),g=read(r.children.gates.receipt.path),h=read(r.children.helper.receipt.path);
 test('captured formal native exit snapshot executes all three actual consumers before parent fixture exit',()=>{const v=validateNativeExit(r,ctx);assert.equal(v.parent.scenariosPerProfile,3);assert.equal(v.gates.specifiedNegativeBuilds,6);assert.equal(v.gates.privateThreadsJoined,224);assert.equal(v.fullLifecycleAcceptance,'INCOMPLETE');});
 for(const [name,change,expected] of [
 ['false whole lifecycle',x=>x.fullLifecycleAcceptance='PASS',/NATIVE_EXIT_AGGREGATE_SCOPE/],
 ['missing actual child',x=>delete x.children.parent,/NATIVE_EXIT_THREE_REAL_CHILDREN/],
 ['lost tool seal',x=>x.toolsAfter.pop(),/NATIVE_EXIT_COMPLETE_SOURCE_TOOL_SEALS/],
 ['lost full nested closure',x=>delete x.nestedAfter,/NATIVE_EXIT_NESTED_SEAL_PIN/],
 ['unsettled child execution',x=>x.children.parent.timedOut=true,/NATIVE_EXIT_ACTUAL_CHILD_COMPLETION/],
 ['changed executed receipt',x=>x.children.parent.receipt.sha256='0'.repeat(64),/NATIVE_EXIT_ACTUAL_CHILD_COMPLETION/],
 ])test('fresh native aggregate rejects '+name,()=>{const x=structuredClone(r);change(x);assert.throws(()=>validateNativeExit(x,ctx),expected);});
 for(const [name,change,expected] of [
 ['success before process join',x=>x.profiles[0].scenarios[1].result.productionResourceProof.processExitedAndJoined=false,/NATIVE_EXIT_PARENT_KERNEL_RETURN/],
 ['pending pipe owner',x=>x.profiles[0].scenarios[1].result.productionResourceProof.pipeRuntime.pending=1,/NATIVE_EXIT_PARENT_PIPE_RETURN/],
 ['fixture kill masquerades as product',x=>x.profiles[0].scenarios[1].result.fixtureCleanup.childKillOrEventBackstop=true,/NATIVE_EXIT_PARENT_NO_SALVAGE/],
 ['physical return invented',x=>x.profiles[0].scenarios[1].result.endpointAndPhysicalReleaseProven=true,/NATIVE_EXIT_PARENT_NO_SALVAGE/],
 ['READY during retained debt',x=>x.profiles[0].scenarios[2].result.beforeFixtureFaultRelief.readyPosts=1,/NATIVE_EXIT_PARENT_RETENTION/],
 ['repeat force budget',x=>x.profiles[0].scenarios[1].result.productionResourceProof.forceAttempts=2,/NATIVE_EXIT_PARENT_KERNEL_RETURN/],
 ])test('fresh full native owner rejects '+name,()=>{const x=structuredClone(p);change(x);assert.throws(()=>validateParent(x,ctx,r.children.parent.output),expected);});
 for(const [name,change,expected] of [
 ['lost negative route',x=>x.profiles.pop(),/NATIVE_EXIT_GATES_EIGHT_BUILDS/],
 ['uncaught old completion gate',x=>x.profiles.find(p=>p.route==='historical-before-host-gate').failures=0,/NATIVE_EXIT_GATES_SPECIFIED_ASSERTIONS/],
 ['private handle remains',x=>x.profiles[0].privateHandlesRemaining=1,/NATIVE_EXIT_GATES_BEFORE_EXIT_ZERO/],
 ['process exit used as cleanup',x=>x.profiles[0].processExitUsedAsCleanup=true,/NATIVE_EXIT_GATES_BEFORE_EXIT_ZERO/],
 ])test('fresh completion consumers reject '+name,()=>{const x=structuredClone(g);change(x);assert.throws(()=>validateGates(x,ctx,r.children.gates.output),expected);});
 test('fresh helper rejects a force request without actual private process destruction',()=>{const x=structuredClone(h);x.profiles[0].result.rows[0].evidence.processAndThreadJoined=false;assert.throws(()=>validateHelper(x,ctx,r.children.helper.output),/NATIVE_EXIT_HELPER_REAL_DESTRUCTION/);});
}
