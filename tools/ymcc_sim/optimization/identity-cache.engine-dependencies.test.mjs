import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {inspectCachedResult as inspectCurrent,CACHE_ENGINE_DEPENDENCIES as actualDependencies} from './identity-cache.mjs';
const originalRepo=fileURLToPath(new URL('../../../',import.meta.url));
const output=path.resolve('G:/YeManCC-Work/Testing/YMCC/Evidence/VirtualWireProjection-20261105/EngineDependencyControls/Private');
assert(!fs.existsSync(output),'Fresh private control directory required');fs.mkdirSync(output,{recursive:true});
const engineRoot=path.join(output,'engine');
for(const source of [fileURLToPath(new URL('./identity-cache.mjs',import.meta.url)),...actualDependencies]){
 const relative=path.relative(originalRepo,source);assert(!relative.startsWith('..')&&!path.isAbsolute(relative));const target=path.join(engineRoot,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);
}
const {fingerprintCase,inspectCachedResult,CACHE_ENGINE_DEPENDENCIES}=await import(pathToFileURL(path.join(engineRoot,'tools/ymcc_sim/optimization/identity-cache.mjs')).href);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),pin=file=>({path:file,sha256:sha(fs.readFileSync(file))});
const stable=v=>Array.isArray(v)?'['+v.map(stable).join(',')+']':v&&typeof v==='object'?'{'+Object.keys(v).sort().map(k=>JSON.stringify(k)+':'+stable(v[k])).join(',')+'}':JSON.stringify(v);
let next=0;
function fixture(){
 const root=path.join(output,'case-'+next++),repoRoot=path.join(root,'repo'),caseDir=path.join(root,'result');fs.mkdirSync(repoRoot,{recursive:true});fs.mkdirSync(caseDir);fs.writeFileSync(path.join(repoRoot,'entry.mjs'),'export const fixtureOnly = true;\n',{flag:'wx'});
 const adapter={id:'ordinary.synthetic',kind:'node',path:'entry.mjs',args:[],scope:'source-contract',timeoutMs:1000,evidenceInputs:['entry.mjs']},options={closureComplete:true,toolClosureComplete:true,resolvedInputs:[]};
 const fingerprint=()=>fingerprintCase({repoRoot,adapter,options}),identity=fingerprint();assert(identity.cacheable,JSON.stringify(identity.reasons));
 for(const name of ['stdout.log','stderr.log'])fs.writeFileSync(path.join(caseDir,name),'',{flag:'wx'});
 const file=path.join(caseDir,'result.json');fs.writeFileSync(file,JSON.stringify({id:adapter.id,source:adapter.path,scope:adapter.scope,status:'PASS',sourceChanged:false,exitCode:0,sourceInputs:[{path:'entry.mjs',sha256:sha(fs.readFileSync(path.join(repoRoot,'entry.mjs'))),status:'OK'}]}),{flag:'wx'});
 const entry={identity:identity.identity,result:pin(file),evidencePins:[file,path.join(caseDir,'stdout.log'),path.join(caseDir,'stderr.log')].map(pin)};
 return {identity,entry,fingerprint};
}
test('ordinary cache automatically pins all six actually imported engine libraries',()=>{
 const f=fixture();assert.equal(CACHE_ENGINE_DEPENDENCIES.length,6);
 for(const file of CACHE_ENGINE_DEPENDENCIES)assert(f.identity.manifest.tools.some(row=>row.role==='cache-engine'&&row.path===file&&row.sha256===pin(file).sha256));
 const r=inspectCachedResult({identity:f.identity,cacheEntry:f.entry});assert.equal(r.status,'CACHED_REFERENCE');assert.equal(r.actualPassedThisRun,false);assert.equal(r.executedThisRun,false);
});
test('forged ordinary identity cannot omit any mandatory imported engine library',()=>{
 const f=fixture();for(const file of CACHE_ENGINE_DEPENDENCIES){const identity=structuredClone(f.identity);identity.manifest.tools=identity.manifest.tools.filter(row=>row.path!==file);identity.identity=sha(stable(identity.manifest));const r=inspectCachedResult({identity,cacheEntry:{...f.entry,identity:identity.identity}});assert(!r.hit);assert(r.reasons.some(row=>row.code==='RUNTIME_IDENTITY_MISSING'),JSON.stringify(r.reasons));}
});
test('ordinary cache detects private imported library byte drift with no adapter declaration',()=>{
 const f=fixture();fs.appendFileSync(CACHE_ENGINE_DEPENDENCIES.at(-1),'\n// private engine dependency drift control\n');const r=inspectCachedResult({identity:f.identity,cacheEntry:f.entry});assert(!r.hit);assert(r.reasons.some(row=>row.code==='INPUT_SHA256_MISMATCH'),JSON.stringify(r.reasons));const after=f.fingerprint();assert(after.cacheable);assert.notEqual(after.identity,f.identity.identity);
});
test('current engine refuses the preceding wire epoch without rekeying its real index or raw',()=>{
 const out=path.resolve('G:/YeManCC-Work/Testing/YMCC/Evidence/VirtualWireProjection-20261105'),identity=JSON.parse(fs.readFileSync(path.join(out,'ACTUAL-BASELINE-IDENTITY.json'),'utf8')),entry=JSON.parse(fs.readFileSync(path.join(out,'cache-before-reference.bin'),'utf8'));const cache=path.resolve('G:/YeManCC-Work/Testing/YMCC/State/ExecutionCache/controller.lifecycle-native-managed-pipe.json'),before=fs.readFileSync(cache);const r=inspectCurrent({identity,cacheEntry:entry});assert(!r.hit);assert(r.reasons.some(row=>row.code==='RUNTIME_IDENTITY_MISSING'),JSON.stringify(r.reasons));assert(fs.readFileSync(cache).equals(before));assert.equal(r.actualPassedThisRun,false);
});
