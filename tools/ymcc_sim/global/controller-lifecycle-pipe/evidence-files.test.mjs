import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
import {collectWireEvidence} from './evidence-files.mjs';
const base=path.resolve('G:/YeManCC-Work/Testing/YMCC/Evidence/WireFinalEngine-20261105/CollectorControls/Private');fs.mkdirSync(base,{recursive:true});
const fixture=()=>fs.mkdtempSync(path.join(base,'case-'));
function file(root,relative,value='fixture-input'){const p=path.join(root,relative);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,value,{flag:'wx'});return p;}
test('wire collector preserves original raw sources receipts and compiled outputs',()=>{
 const root=fixture(),expected=['source-main.cpp','source-projection.json','p/RESULT.json','p/source/fixture.cpp','p/source-bindings.json','p/Release/fixture.exe','p/Release/run/steamdeck.json','n0/Checked/compile.log','n1/Checked/run/result.json'].map(r=>file(root,r));
 file(root,'AppData/Local/ambient.cache');file(root,'p/Release/AppData/Local/ambient.cache');const result=collectWireEvidence(root);assert.deepEqual(result.files.sort(),expected.sort());assert.equal(result.excludedProfileDirectories.length,2);
});
test('wire collector excludes inward Windows INetCache junction only as private AppData state',()=>{
 const root=fixture(),cache=path.join(root,'p/Release/AppData/Local/Microsoft/Windows/INetCache'),target=path.join(cache,'IE');fs.mkdirSync(target,{recursive:true});file(root,'p/Release/run/result.json');fs.symlinkSync(target,path.join(cache,'Content.IE5'),'junction');const r=collectWireEvidence(root);assert.equal(r.files.length,1);assert.equal(r.excludedProfileDirectories.length,1);assert(r.files[0].endsWith(path.join('run','result.json')));
});
test('wire collector rejects evidence junctions even to an owned target',()=>{
 const root=fixture(),target=path.join(root,'owned-target');fs.mkdirSync(target);const run=path.join(root,'p/Release/run');fs.mkdirSync(run,{recursive:true});fs.symlinkSync(target,path.join(run,'linked'),'junction');assert.throws(()=>collectWireEvidence(root),/OUTPUT_LINK/);
});
test('wire collector does not exempt source or receipt AppData directories',()=>{
 const root=fixture(),required=file(root,'p/source/AppData/required.json');assert(collectWireEvidence(root).files.includes(required));const target=path.join(root,'owned-target');fs.mkdirSync(target);fs.symlinkSync(target,path.join(root,'p/source/AppData/linked'),'junction');assert.throws(()=>collectWireEvidence(root),/OUTPUT_LINK/);
});
test('wire collector rejects redirected profile and evidence roots',()=>{
 const root=fixture(),target=path.join(root,'owned-target');fs.mkdirSync(target);fs.mkdirSync(path.join(root,'p/Release'),{recursive:true});fs.symlinkSync(target,path.join(root,'p/Release/AppData'),'junction');assert.throws(()=>collectWireEvidence(root),/PROFILE_ROOT_REDIRECTION/);const alias=path.join(base,'root-link');fs.symlinkSync(root,alias,'junction');assert.throws(()=>collectWireEvidence(alias),/OUTPUT_ROOT_REDIRECTION/);
});
