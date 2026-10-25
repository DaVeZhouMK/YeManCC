import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {REPO,MODULE} from './adapter.mjs';
import {materializeObserverContext,completeDefinition,sha256} from './observer-context.mjs';
const moduleDir=path.join(REPO,MODULE),review=JSON.parse(fs.readFileSync(path.join(moduleDir,'observer-context-review.json'),'utf8'));
const work='G:/YeManCC-Work/Testing/YMCC/Workspace/SleepGamePauseRepair-20261023';
function prepare(){
 const root=fs.mkdtempSync(path.join(work,'binding-test-')),project=path.join(root,'source'),out=path.join(root,'out');fs.mkdirSync(out);fs.mkdirSync(project);
 for(const row of review.sources){const target=path.join(project,row.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(REPO,row.path),target);}
 const row=review.definitions.find(x=>x.path==='native/main.cpp'),main=path.join(project,row.path);fs.mkdirSync(path.dirname(main),{recursive:true});fs.writeFileSync(main,completeDefinition(fs.readFileSync(path.join(REPO,row.path),'utf8'),row.signature));
 return {project,out,moduleDir,reviewSha256:sha256(fs.readFileSync(path.join(moduleDir,'observer-context-review.json')))};
}
test('materializes complete reviewed helper and real policy without changing the original generator',()=>{
 const context=materializeObserverContext(prepare()),text=fs.readFileSync(context.header.path,'utf8');
 for(const row of context.definitions.filter(x=>x.token))assert(text.includes(row.text));
 assert(text.includes('#include "msi_visibility_wake_diagnostics.h"'));
 assert.equal(context.expectedContextChecks,12);
 assert.equal(context.originalScript.sha256,'84784123cf7571f30267e4beff74887969ea1ffa2562cc53bfc03f8edfd56f3d');
});
test('refuses a changed real helper before original compilation',()=>{
 const config=prepare(),runtime=path.join(config.project,'native/msi_visibility_wake_runtime.h');
 fs.appendFileSync(runtime,'\n// source drift control\n');
 assert.throws(()=>materializeObserverContext(config),/GAME_MSI_SOURCE_NOT_REVIEWED/);
 assert(!fs.existsSync(path.join(config.out,'msi-observer-context.h')));
});
test('refuses a truncated complete owner function rather than accepting a call-site fragment',()=>{
 const row=review.definitions.find(x=>x.path==='native/main.cpp');
 const text=completeDefinition(fs.readFileSync(path.join(REPO,row.path),'utf8'),row.signature);
 assert.throws(()=>completeDefinition(text.slice(0,-1),row.signature),/GAME_MSI_DEFINITION_INCOMPLETE/);
});
