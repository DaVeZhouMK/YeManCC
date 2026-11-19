import test,{after} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {CACHE_ENGINE_DEPENDENCIES as actualDependencies} from './identity-cache.mjs';
const originalRepo=fileURLToPath(new URL('../../../',import.meta.url));
// Standard discovery owns a fresh G core workspace; failed controls retain their files.
const workspace=path.resolve(originalRepo,'../../../Testing/YMCC/Workspace');
const prefix='identity-cache-engine-dependencies-';
const samePath=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
let owned,ready,failed=false;
let fingerprintCase,inspectCachedResult,CACHE_ENGINE_DEPENDENCIES;
after(()=>{
 if(!owned)return;
 const {output,created}=owned;
 if(failed){console.error('Retained failed engine dependency controls: '+output);return;}
 assert.equal(path.dirname(path.resolve(output)),workspace);assert(path.basename(output).startsWith(prefix));
 const current=fs.lstatSync(output,{bigint:true});
 assert(current.isDirectory()&&!current.isSymbolicLink()&&current.dev===created.dev&&current.ino===created.ino);
 assert(samePath(fs.realpathSync(output),output));fs.rmSync(output,{recursive:true,force:false});
});
async function prepareFixture(){
 assert(!fs.lstatSync(workspace).isSymbolicLink()&&samePath(fs.realpathSync(workspace),workspace));
 const output=fs.mkdtempSync(path.join(workspace,prefix));
 owned={output,created:fs.lstatSync(output,{bigint:true})};
 console.log('Engine dependency control workspace: '+output);
 const engineRoot=path.join(output,'engine');
 for(const source of [fileURLToPath(new URL('./identity-cache.mjs',import.meta.url)),...actualDependencies]){
  const relative=path.relative(originalRepo,source);assert(!relative.startsWith('..')&&!path.isAbsolute(relative));const target=path.join(engineRoot,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);
 }
 ({fingerprintCase,inspectCachedResult,CACHE_ENGINE_DEPENDENCIES}=await import(pathToFileURL(path.join(engineRoot,'tools/ymcc_sim/optimization/identity-cache.mjs')).href));
}
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),pin=file=>({path:file,sha256:sha(fs.readFileSync(file))});
const stable=v=>Array.isArray(v)?'['+v.map(stable).join(',')+']':v&&typeof v==='object'?'{'+Object.keys(v).sort().map(k=>JSON.stringify(k)+':'+stable(v[k])).join(',')+'}':JSON.stringify(v);
let next=0;
function fixture(){
 const root=path.join(owned.output,'case-'+next++),repoRoot=path.join(root,'repo'),caseDir=path.join(root,'result');fs.mkdirSync(repoRoot,{recursive:true});fs.mkdirSync(caseDir);fs.writeFileSync(path.join(repoRoot,'entry.mjs'),'export const fixtureOnly = true;\n',{flag:'wx'});
 const adapter={id:'ordinary.synthetic',kind:'node',path:'entry.mjs',args:[],scope:'source-contract',timeoutMs:1000,evidenceInputs:['entry.mjs']},options={closureComplete:true,toolClosureComplete:true,resolvedInputs:[]};
 const fingerprint=()=>fingerprintCase({repoRoot,adapter,options}),identity=fingerprint();assert(identity.cacheable,JSON.stringify(identity.reasons));
 for(const name of ['stdout.log','stderr.log'])fs.writeFileSync(path.join(caseDir,name),'',{flag:'wx'});
 const file=path.join(caseDir,'result.json');fs.writeFileSync(file,JSON.stringify({id:adapter.id,source:adapter.path,scope:adapter.scope,status:'PASS',sourceChanged:false,exitCode:0,sourceInputs:[{path:'entry.mjs',sha256:sha(fs.readFileSync(path.join(repoRoot,'entry.mjs'))),status:'OK'}]}),{flag:'wx'});
 const entry={identity:identity.identity,result:pin(file),evidencePins:[file,path.join(caseDir,'stdout.log'),path.join(caseDir,'stderr.log')].map(pin)};
 return {identity,entry,fingerprint};
}
function ordinaryTest(name,run){
 test(name,async t=>{
  let passed=false;
  try{ready??=prepareFixture();await ready;run();passed=true;}
  finally{if(!passed||t.signal.aborted)failed=true;}
 });
}
ordinaryTest('ordinary cache automatically pins every actually imported engine library',()=>{
 const f=fixture();assert(CACHE_ENGINE_DEPENDENCIES.some(file=>file.endsWith(path.join('controller-foreground-latency','prehost-cache-contract.mjs'))));
 for(const file of CACHE_ENGINE_DEPENDENCIES)assert(f.identity.manifest.tools.some(row=>row.role==='cache-engine'&&row.path===file&&row.sha256===pin(file).sha256));
 const r=inspectCachedResult({identity:f.identity,cacheEntry:f.entry});assert.equal(r.status,'CACHED_REFERENCE');assert.equal(r.actualPassedThisRun,false);assert.equal(r.executedThisRun,false);
});
ordinaryTest('forged ordinary identity cannot omit any mandatory imported engine library',()=>{
 const f=fixture();for(const file of CACHE_ENGINE_DEPENDENCIES){const identity=structuredClone(f.identity);identity.manifest.tools=identity.manifest.tools.filter(row=>row.path!==file);identity.identity=sha(stable(identity.manifest));const r=inspectCachedResult({identity,cacheEntry:{...f.entry,identity:identity.identity}});assert(!r.hit);assert(r.reasons.some(row=>row.code==='RUNTIME_IDENTITY_MISSING'),JSON.stringify(r.reasons));}
});
ordinaryTest('ordinary cache detects private imported library byte drift with no adapter declaration',()=>{
 const f=fixture();fs.appendFileSync(CACHE_ENGINE_DEPENDENCIES.at(-1),'\n// private engine dependency drift control\n');const r=inspectCachedResult({identity:f.identity,cacheEntry:f.entry});assert(!r.hit);assert(r.reasons.some(row=>row.code==='INPUT_SHA256_MISMATCH'),JSON.stringify(r.reasons));const after=f.fingerprint();assert(after.cacheable);assert.notEqual(after.identity,f.identity.identity);
});
// Historical wire-epoch comparison is an explicit diagnostic outside standard discovery.
