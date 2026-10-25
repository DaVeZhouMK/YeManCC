import vm from 'node:vm';import crypto from 'node:crypto';
import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {inspectCurrentStage,requireCurrentStageRecorded} from './stage-evidence.mjs';
import {currentFeedbackHeaders,currentFeedbackSupport,CURRENT_GAME_FUNCTIONS} from '../../../fixtures/xinput_identity_feedback_context.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
function fixture(t){const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'xinput-stage-'));t.after(()=>{const resolved=fs.realpathSync(root);assert.equal(path.dirname(resolved),parent);assert(path.basename(resolved).startsWith('xinput-stage-'));fs.rmSync(resolved,{recursive:true,force:true});});return root;}
const write=(root,name,data)=>fs.writeFileSync(path.join(root,name+'.command.json'),JSON.stringify(data));
const good={status:0,error:null,signal:null,timedOut:false};
function oldStages(root){for(const label of ['old-off','old-on']){write(root,label+'-build',good);write(root,label+'-execute',{...good,status:1});}}
test('compile failure remains primary and missing execute is not reached',t=>{const root=fixture(t);oldStages(root);write(root,'fixed-off-build',{...good,status:2});const evidence=inspectCurrentStage(root);assert.equal(evidence.firstFailure.code,'ORIGINAL_XINPUT_BUILD_FAILED');assert.equal(evidence.firstFailure.status,2);assert.equal(evidence.stages[2].execute.state,'NOT_REACHED');assert.equal(evidence.stages[3].build.state,'NOT_REACHED');assert.throws(()=>requireCurrentStageRecorded(root),/ORIGINAL_XINPUT_BUILD_FAILED:fixed-off:2/);});
test('compiler timeout and raw error survive missing downstream files',t=>{const root=fixture(t);oldStages(root);write(root,'fixed-off-build',{status:null,timedOut:true,error:'ETIMEDOUT',signal:'SIGTERM'});const evidence=inspectCurrentStage(root);assert.equal(evidence.firstFailure.code,'ORIGINAL_XINPUT_BUILD_TIMEOUT');assert.equal(evidence.firstFailure.error,'ETIMEDOUT');assert.equal(evidence.stages[2].execute.state,'NOT_REACHED');});
test('an execution timeout remains an execution error',t=>{const root=fixture(t);oldStages(root);write(root,'fixed-off-build',good);write(root,'fixed-off-execute',{status:null,timedOut:true,error:'ETIMEDOUT'});assert.equal(inspectCurrentStage(root).firstFailure.code,'ORIGINAL_XINPUT_EXECUTION_TIMEOUT');});
test('successful compile with absent execute reports not reached without ENOENT',t=>{const root=fixture(t);oldStages(root);write(root,'fixed-off-build',good);assert.throws(()=>requireCurrentStageRecorded(root),/ORIGINAL_XINPUT_EXECUTION_NOT_REACHED:fixed-off/);});
test('normal positive and semantic negative process receipts remain available to original business oracles',t=>{const root=fixture(t);oldStages(root);for(const label of ['fixed-off','fixed-on']){write(root,label+'-build',good);write(root,label+'-execute',good);}assert.equal(requireCurrentStageRecorded(root).status,'RECORDED');write(root,'fixed-off-execute',{...good,status:1});assert.equal(requireCurrentStageRecorded(root,{negative:true}).status,'RECORDED');});
test('pure current header closure is complete and contains real frame/dispatch/diagnostic types',()=>{const headers=currentFeedbackHeaders(repo);for(const name of ['game_feedback_dispatch.h','game_feedback_source_ticket.h','game_feedback_receipt.h','game_feedback_diagnostics.h','input_frame_submission.h','virtual_output_status.h','touch_haptic_dispatch.h'])assert(headers.includes('native/'+name));for(const relative of headers){const source=fs.readFileSync(path.join(repo,relative),'utf8');for(const match of source.matchAll(/^\s*#include\s+"([^"]+)"/gm))assert(headers.includes('native/'+match[1]));}});
test('current support takes actual declarations and every production helper without replacing gates',()=>{const source=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),calls=[];const support=currentFeedbackSupport(source,name=>{calls.push(name);return '// exact extracted '+name;},(name,a,b)=>source.slice(a,b));assert.deepEqual(calls,[...CURRENT_GAME_FUNCTIONS]);assert(support.GAME_COMMAND.includes('g_gameMotorExecutionStats;'));assert(support.GAME_DISPATCH.includes('g_gamepadSerialGameFeedback;'));assert(!support.GAME_COMMAND.includes('return true;'));});

test('a never-started build is recorded as not reached without reading a missing JSON',t=>{const root=fixture(t);const evidence=inspectCurrentStage(root);assert.equal(evidence.firstFailure.code,'ORIGINAL_XINPUT_BUILD_NOT_REACHED');assert(evidence.stages.every(stage=>stage.build.state==='NOT_REACHED'&&stage.execute.state==='NOT_REACHED'));});

test('actual producer extraction wires the complete current game context without executing the producer',()=>{
 const driver=fs.readFileSync(path.join(repo,'tools/xinput_identity_feedback_lifecycle_selftest.mjs'),'utf8');
 const start=driver.indexOf('function extract(text,file)'),end=driver.indexOf('\nconst current=extract(',start);
 assert(start>=0&&end>start);
 const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
 const extract=vm.runInNewContext('('+driver.slice(start,end).trim()+')',{assert,Buffer,sha,currentFeedbackSupport});
 const source=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),actual=extract(source,path.join(repo,'native/main.cpp'));
 assert(actual.parts.GAME_COMMAND.includes('static bool nativeGameFeedbackFrameCurrent('));
 assert(actual.parts.GAME_COMMAND.includes('frameSubmission::decide(command.ticket,command.read,current,GetTickCount64())'));
 assert(actual.parts.GAME_DISPATCH.includes('g_gamepadSerialGameFeedback.manualStarted(generation)'));
 assert(actual.parts.GAME_DISPATCH.includes('g_gamepadSerialGameFeedback.ownerFinished(generation)'));
 for(const name of CURRENT_GAME_FUNCTIONS)assert(actual.ranges.some(row=>row.name===name&&row.code===source.slice(Buffer.from(source).subarray(0,row.start_utf8_byte).toString('utf8').length,Buffer.from(source).subarray(0,row.end_utf8_byte_exclusive).toString('utf8').length)));
});
