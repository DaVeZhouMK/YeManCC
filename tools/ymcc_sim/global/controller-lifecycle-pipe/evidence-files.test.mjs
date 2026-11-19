import test,{after} from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
import {collectWireEvidence} from './evidence-files.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';

const prefix='wire-collector-controls-';
const samePath=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
let owned,failed=false;
function fixture(){
 if(!owned){
  const workspace=simulationCorePath('Workspace'),parent=fs.lstatSync(workspace);
  assert(parent.isDirectory()&&!parent.isSymbolicLink()&&samePath(fs.realpathSync.native(workspace),workspace));
  const root=fs.mkdtempSync(path.join(workspace,prefix));
  owned={workspace,root,identity:fs.lstatSync(root,{bigint:true})};
 }
 return fs.mkdtempSync(path.join(owned.root,'case-'));
}
function wireTest(name,run){
 test(name,t=>{
  let passed=false;
  try{run();passed=true;}
  finally{if(!passed||t.signal.aborted)failed=true;}
 });
}
after(()=>{
 if(!owned)return;
 const {workspace,root,identity}=owned;
 if(failed){console.error('Retained failed wire collector controls: '+root);return;}
 assert.equal(path.dirname(path.resolve(root)),workspace);assert(path.basename(root).startsWith(prefix));
 const current=fs.lstatSync(root,{bigint:true});
 assert(current.isDirectory()&&!current.isSymbolicLink()&&current.dev===identity.dev&&current.ino===identity.ino);
 assert(samePath(fs.realpathSync.native(root),root));
 fs.rmSync(root,{recursive:true,force:false});
});
function file(root,relative,value='fixture-input'){const p=path.join(root,relative);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,value,{flag:'wx'});return p;}
wireTest('wire collector preserves original raw sources receipts and compiled outputs',()=>{
 const root=fixture(),expected=['source-main.cpp','source-projection.json','p/RESULT.json','p/source/fixture.cpp','p/source-bindings.json','p/Release/fixture.exe','p/Release/run/steamdeck.json','n0/Checked/compile.log','n1/Checked/run/result.json'].map(r=>file(root,r));
 file(root,'AppData/Local/ambient.cache');file(root,'p/Release/AppData/Local/ambient.cache');const result=collectWireEvidence(root);assert.deepEqual(result.files.sort(),expected.sort());assert.equal(result.excludedProfileDirectories.length,2);
});
wireTest('wire collector excludes inward Windows INetCache junction only as private AppData state',()=>{
 const root=fixture(),cache=path.join(root,'p/Release/AppData/Local/Microsoft/Windows/INetCache'),target=path.join(cache,'IE');fs.mkdirSync(target,{recursive:true});file(root,'p/Release/run/result.json');fs.symlinkSync(target,path.join(cache,'Content.IE5'),'junction');const r=collectWireEvidence(root);assert.equal(r.files.length,1);assert.equal(r.excludedProfileDirectories.length,1);assert(r.files[0].endsWith(path.join('run','result.json')));
});
wireTest('wire collector rejects evidence junctions even to an owned target',()=>{
 const root=fixture(),target=path.join(root,'owned-target');fs.mkdirSync(target);const run=path.join(root,'p/Release/run');fs.mkdirSync(run,{recursive:true});fs.symlinkSync(target,path.join(run,'linked'),'junction');assert.throws(()=>collectWireEvidence(root),/OUTPUT_LINK/);
});
wireTest('wire collector does not exempt source or receipt AppData directories',()=>{
 const root=fixture(),required=file(root,'p/source/AppData/required.json');assert(collectWireEvidence(root).files.includes(required));const target=path.join(root,'owned-target');fs.mkdirSync(target);fs.symlinkSync(target,path.join(root,'p/source/AppData/linked'),'junction');assert.throws(()=>collectWireEvidence(root),/OUTPUT_LINK/);
});
wireTest('wire collector rejects redirected profile and evidence roots',()=>{
 const root=fixture(),target=path.join(root,'owned-target');fs.mkdirSync(target);fs.mkdirSync(path.join(root,'p/Release'),{recursive:true});fs.symlinkSync(target,path.join(root,'p/Release/AppData'),'junction');assert.throws(()=>collectWireEvidence(root),/PROFILE_ROOT_REDIRECTION/);const alias=root+'-link';fs.symlinkSync(root,alias,'junction');assert.throws(()=>collectWireEvidence(alias),/OUTPUT_ROOT_REDIRECTION/);
});
