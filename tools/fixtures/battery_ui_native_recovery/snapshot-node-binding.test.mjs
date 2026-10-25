import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {captureSnapshotNodeToolGraph} from '../../ymcc_sim/global/snapshot-node-tools.mjs';
import {resolveSnapshotNodeTools,mapBoundNodeRows,validateBoundNodeDeclaration,nodeBuildConfiguration} from './snapshot-node-binding.mjs';
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const digest=x=>createHash('sha256').update(x).digest('hex');
function fixture(t,{sharedToolRoot=false}={}){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-ui-node-binding-'));
 t.after(()=>{assert.equal(path.dirname(path.resolve(root)),path.resolve(os.tmpdir()));assert.ok(path.basename(root).startsWith('ymcc-ui-node-binding-'));assert.equal(fs.realpathSync(root).toLowerCase(),path.resolve(root).toLowerCase());fs.rmSync(root,{recursive:true,force:true});});
 const primary=path.join(root,'primary'),repo=path.join(root,'snapshot'),manifestFile=path.join(repo,'.ymcc-source-snapshot/manifest.json'),bindingManifestFile=path.join(repo,'.ymcc-source-snapshot/node-tools-binding.json');
 const put=(p,text)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,text);};const json=(p,x)=>put(p,JSON.stringify(x));
 json(path.join(primary,'package.json'),{name:'local-contract-only'});
 for(const name of ['typescript','vue','esbuild','@esbuild/win32-x64']){
  const pkg=path.join(primary,'node_modules',name);json(path.join(pkg,'package.json'),{name,version:'1.0.0',main:'index.js',...(name==='esbuild'?{dependencies:{'@esbuild/win32-x64':'1.0.0'}}:{})});put(path.join(pkg,'index.js'),'module.exports={};\n');
  if(name==='@esbuild/win32-x64')put(path.join(pkg,'esbuild.exe'),'unexecuted-local-binary-fixture');
 }
 if(sharedToolRoot){const logical=path.join(primary,'node_modules'),owner=path.join(root,'tool-owner'),physical=path.join(owner,'node_modules');for(const file of [logical,physical]){const relative=path.relative(root,path.resolve(file));assert(relative&&!relative.startsWith('..')&&!path.isAbsolute(relative));}fs.mkdirSync(owner);fs.renameSync(logical,physical);fs.symlinkSync(physical,logical,'junction');}
 json(manifestFile,{schemaVersion:1,kind:'ymcc-fixed-source-snapshot',sourceRoot:primary,snapshotRoot:repo,status:'PASS'});
 const graph=captureSnapshotNodeToolGraph({primaryRepo:primary});
 const saved={schemaVersion:1,kind:'ymcc-snapshot-node-tools-binding',status:'PASS',sourceRoot:primary,snapshotRoot:repo,snapshotManifestFile:manifestFile,expectedSnapshotManifestSha256:hash(manifestFile),bindingManifestFile,sourceApprovalGranted:false,executionPerformed:false,graph:graph.graph,graphBeforeSha256:graph.sha256,graphAfterSha256:graph.sha256};
 const bind=(update={})=>{json(bindingManifestFile,{...saved,...update});return{bindingManifestFile,expectedBindingSha256:hash(bindingManifestFile)};};
 return {root,repo,primary,manifestFile,bindingManifestFile,put,json,saved,bind,options:bind()};
}
test('ordinary non-snapshot root has no remapping authority',t=>{const f=fixture(t);assert.equal(resolveSnapshotNodeTools(f.primary),null);assert.deepEqual(nodeBuildConfiguration({},f.primary),{packageRepo:f.primary,nodeModulesPath:path.join(f.primary,'node_modules')});});
test('snapshot must have explicit binding and exact SHA',t=>{const f=fixture(t);for(const opts of [{},{bindingManifestFile:f.bindingManifestFile},{...f.options,expectedBindingSha256:'0'.repeat(64)}])assert.throws(()=>resolveSnapshotNodeTools(f.repo,opts));});
test('successful binding verifies the complete live canonical graph and declares authority bytes',t=>{const f=fixture(t),a=resolveSnapshotNodeTools(f.repo,f.options);assert.equal(a.files.length,f.saved.graph.fileCount);assert.equal(a.proof.graphSha256,f.saved.graphBeforeSha256);assert.equal(a.packageRepo,f.primary);assert.ok(a.files.every(x=>x.path.startsWith(path.join(f.primary,'node_modules'))));assert.ok(a.metadata.some(x=>x.path===f.bindingManifestFile));assert.ok(a.metadata.some(x=>x.path===f.manifestFile));assert.equal(a.proof.sourceApprovalGranted,false);});
test('old Node members must be exact bound members; missing, changed hash and path escape reject',t=>{const f=fixture(t),a=resolveSnapshotNodeTools(f.repo,f.options),row=a.files[0],repoRelative=path.relative(f.primary,row.path).replaceAll('\\','/');const good={repoRelative,sha256:row.sha256};assert.equal(mapBoundNodeRows([good],a)[0].path,row.path);for(const bad of [{...good,sha256:'0'.repeat(64)},{...good,repoRelative:'node_modules/not-bound/file.js'},{...good,repoRelative:'../outside.js'}])assert.throws(()=>mapBoundNodeRows([bad],a),/BATTERY_UI_/);});
test('whole graph declaration cannot omit even a non-entry file or its metadata',t=>{const f=fixture(t),a=resolveSnapshotNodeTools(f.repo,f.options),all=[...a.files,...a.metadata];validateBoundNodeDeclaration(all,a);for(const removed of [0,all.length-1])assert.throws(()=>validateBoundNodeDeclaration(all.filter((_,i)=>i!==removed),a),/MEMBER_UNDECLARED/);assert.throws(()=>validateBoundNodeDeclaration(all.map((r,i)=>i? r:{...r,sha256:'0'.repeat(64)}),a),/MEMBER_UNDECLARED/);});
test('changed package byte and extra package member invalidate a saved graph',t=>{const f=fixture(t),a=resolveSnapshotNodeTools(f.repo,f.options),file=a.files.find(x=>x.path.endsWith('index.js')).path;fs.appendFileSync(file,'changed');assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.options),/NODE_GRAPH_CHANGED/);fs.writeFileSync(file,'module.exports={};\n');f.put(path.join(path.dirname(file),'unbound-extra.js'),'extra');assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.options),/NODE_GRAPH_CHANGED/);});
test('binding body, graph digest and source manifest are independently pinned',t=>{const f=fixture(t);fs.appendFileSync(f.bindingManifestFile,' ');assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.options),/BINDING_HASH_CHANGED/);const opts=f.bind({graphBeforeSha256:'0'.repeat(64)});assert.throws(()=>resolveSnapshotNodeTools(f.repo,opts),/GRAPH_DIGEST_CHANGED/);const normal=f.bind();fs.appendFileSync(f.manifestFile,' ');assert.throws(()=>resolveSnapshotNodeTools(f.repo,normal),/SOURCE_MANIFEST_CHANGED/);});
test('wrong snapshot root and unsuccessful binding never supply fallback packages',t=>{const f=fixture(t);assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.bind({snapshotRoot:f.primary})),/ROOT_MISMATCH/);assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.bind({status:'FAIL'})),/VERIFIED_NODE_BINDING_REQUIRED/);});
test('historical snapshot requires captured integrity proof and pinned verifier code',t=>{const f=fixture(t);assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.bind({allowHistoricalSnapshot:true})),/CAPTURE_PROOF_REQUIRED/);const proof={status:'FROZEN_SOURCE_VERIFIED',snapshotIntegrityVerified:true,sourceApprovalGranted:false,manifestSha256:f.saved.expectedSnapshotManifestSha256,snapshotRoot:f.repo};const verifier=path.join(f.primary,'verifier.mjs');f.put(verifier,'// pinned test verifier, never executed');const extra={allowHistoricalSnapshot:true,captureVerification:proof,captureVerificationBefore:proof,wholeCaptureVerification:proof,verifierSource:{path:verifier,sha256:hash(verifier)},verifierCodeSha256:hash(verifier)};const opts=f.bind(extra);resolveSnapshotNodeTools(f.repo,opts);fs.appendFileSync(verifier,'changed');assert.throws(()=>resolveSnapshotNodeTools(f.repo,opts),/NODE_VERIFIER_CHANGED/);});
test('forged graph member is rejected against an actual capture even if its JSON digest is valid',t=>{const f=fixture(t),graph=structuredClone(f.saved.graph);graph.packages[0].files[0].sha256='0'.repeat(64);const sha=digest(JSON.stringify(graph));assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.bind({graph,graphBeforeSha256:sha,graphAfterSha256:sha})),/NODE_GRAPH_CHANGED/);});

test('a declared shared real tool root maps canonical members independently of the source root',t=>{
 const f=fixture(t,{sharedToolRoot:true}),a=resolveSnapshotNodeTools(f.repo,f.options);
 assert.equal(a.packageRepo,f.primary);assert.notEqual(a.nodeModulesRealRoot,a.nodeModulesPath);
 const row=a.files.find(x=>x.path.endsWith('index.js')),repoRelative='node_modules/'+path.relative(a.nodeModulesRealRoot,row.path).replaceAll('\\','/');
 const mapped=mapBoundNodeRows([{repoRelative,sha256:row.sha256}],a)[0];assert.equal(mapped.path,row.path);assert.equal(hash(mapped.path),row.sha256);
 assert.equal(a.proof.sourceApprovalGranted,false);validateBoundNodeDeclaration([...a.files,...a.metadata],a);
});
test('shared-root canonical mapping still refuses missing, changed and escaping declared members',t=>{
 const f=fixture(t,{sharedToolRoot:true}),a=resolveSnapshotNodeTools(f.repo,f.options),row=a.files.find(x=>x.path.endsWith('index.js'));
 const repoRelative='node_modules/'+path.relative(a.nodeModulesRealRoot,row.path).replaceAll('\\','/');
 for(const bad of [{repoRelative,sha256:'0'.repeat(64)},{repoRelative:'node_modules/not-in-the-graph/file.js',sha256:row.sha256},{repoRelative:'node_modules/../package.json',sha256:row.sha256},{repoRelative:'node_modules/../../outside/index.js',sha256:row.sha256}])assert.throws(()=>mapBoundNodeRows([bad],a),/BATTERY_UI_/);
 assert.throws(()=>mapBoundNodeRows([{repoRelative,sha256:row.sha256}],{...a,files:a.files.filter(x=>x.path!==row.path)}),/NOT_IN_BOUND_GRAPH/);
});
test('a changed shared-root package or retargeted logical root invalidates the original full graph',t=>{
 const f=fixture(t,{sharedToolRoot:true}),a=resolveSnapshotNodeTools(f.repo,f.options),row=a.files.find(x=>x.path.endsWith('index.js'));
 fs.appendFileSync(row.path,'changed');assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.options),/NODE_GRAPH_CHANGED/);
 fs.writeFileSync(row.path,'module.exports={};\n');
 const logical=path.join(f.primary,'node_modules'),moved=path.join(f.root,'tool-owner','moved-node_modules');
 for(const file of [a.nodeModulesRealRoot,moved]){const relative=path.relative(f.root,path.resolve(file));assert(relative&&!relative.startsWith('..')&&!path.isAbsolute(relative));}
 fs.cpSync(a.nodeModulesRealRoot,moved,{recursive:true,errorOnExist:true,force:false});fs.unlinkSync(logical);fs.symlinkSync(moved,logical,'junction');
 assert.throws(()=>resolveSnapshotNodeTools(f.repo,f.options),/NODE_GRAPH_CHANGED|NODE_TOOLS_PACKAGE_OUTSIDE_PRIMARY/);
});
