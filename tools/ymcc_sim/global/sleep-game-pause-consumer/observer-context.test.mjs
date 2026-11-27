import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {REPO,MODULE} from './adapter.mjs';
import {materializeObserverContext,completeDefinition,sha256} from './observer-context.mjs';
import {verifySourceContinuity} from '../sleep-native-recovery-consumer/source-continuity.mjs';
import {verifyS4Review,reverseReviewedDelta} from '../sleep-native-recovery-consumer/script-review.mjs';
const moduleDir=path.join(REPO,MODULE),review=JSON.parse(fs.readFileSync(path.join(moduleDir,'observer-context-review.json'),'utf8'));
const spec=JSON.parse(fs.readFileSync(path.join(moduleDir,'spec.json'),'utf8'));
const work=fs.realpathSync('G:/YeManCC-Work/Testing/YMCC/Workspace/SleepGamePauseRepair-20261023');
const resumeSignature='static void handlePowerResumeNotification(SgWork work, const char* reason, SgWorkItem* originalS4=nullptr) {';
function prepare(t){
 const root=fs.mkdtempSync(path.join(work,'binding-test-')),project=path.join(root,'source'),out=path.join(root,'out');fs.mkdirSync(out);fs.mkdirSync(project);
 t.after(()=>{const actual=fs.realpathSync(root);assert.equal(actual,root);assert.equal(path.dirname(actual),work);fs.rmSync(actual,{recursive:true});});
 for(const row of review.sources){const target=path.join(project,row.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(REPO,row.path),target);}
 const row=review.definitions.find(x=>x.path==='native/main.cpp'),main=path.join(project,row.path);fs.mkdirSync(path.dirname(main),{recursive:true});fs.writeFileSync(main,completeDefinition(fs.readFileSync(path.join(REPO,row.path),'utf8'),resumeSignature));
 return {project,out,moduleDir,reviewSha256:spec.observerContextReview.sha256,compatibilityReview:spec.observerContextCompatibilityReview};
}
test('materializes complete reviewed helper and real policy without changing the original generator',t=>{
 const config=prepare(t),context=materializeObserverContext(config),text=fs.readFileSync(context.header.path,'utf8');
 for(const row of context.definitions.filter(x=>x.token))assert(text.includes(row.text));
 assert(text.includes('#include "msi_visibility_wake_diagnostics.h"'));
 assert.equal(context.expectedContextChecks,12);
 const generator=fs.readFileSync(path.join(config.project,context.originalScript.path));
 assert.equal(context.originalScript.sha256,sha256(generator));
 const approved=verifySourceContinuity(REPO,'pause',spec,(prior,readSource)=>verifyS4Review(prior,'pause',readSource).receipt,reverseReviewedDelta);
 approved.verifyCaptured(context.originalScript.path,generator);
 assert.equal(approved.historicalHash(context.originalScript.path),spec.originalScript.sha256);
 assert.equal(context.originalScript.historicalSha256??context.originalScript.sha256,review.originalScript.sha256);
});
test('refuses a changed real helper before original compilation',t=>{
 const config=prepare(t),runtime=path.join(config.project,'native/msi_visibility_wake_runtime.h');
 fs.appendFileSync(runtime,'\n// source drift control\n');
 assert.throws(()=>materializeObserverContext(config),/GAME_MSI_SOURCE_NOT_REVIEWED:native\/msi_visibility_wake_runtime\.h/);
 assert(!fs.existsSync(path.join(config.out,'msi-observer-context.h')));
});
test('refuses a truncated complete owner function rather than accepting a call-site fragment',()=>{
 const row=review.definitions.find(x=>x.path==='native/main.cpp');
 const text=completeDefinition(fs.readFileSync(path.join(REPO,row.path),'utf8'),resumeSignature);
 assert.throws(()=>completeDefinition(text.slice(0,-1),resumeSignature),/GAME_MSI_DEFINITION_INCOMPLETE/);
});
