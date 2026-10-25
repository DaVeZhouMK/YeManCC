import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {bindSnapshotNodeTools,verifySnapshotNodeTools,captureSnapshotNodeToolGraph,DEFAULT_SEED_PACKAGES,NODE_TOOLS_BINDING_NAME,parseSnapshotNodeToolArguments} from './snapshot-node-tools.mjs';
import {createSourceSnapshot,compareSourceSnapshot} from './source-snapshot.mjs';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const writeJson=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const failResult=fn=>{try{const result=fn();assert.equal(result.status,'FAIL');return result;}catch(error){if(error.code==='ERR_ASSERTION')throw error;return {status:'FAIL',failure:{code:error.code??error.message}};}};
const throwing='throw new Error("SYNTHETIC_TOOL_MUST_NEVER_BE_EXECUTED");\n';
function fixture(t){
 const temp=path.resolve(os.tmpdir()),base=fs.mkdtempSync(path.join(temp,'ymcc-node-tools-test-'));
 t.after(()=>{const resolved=path.resolve(base);assert.equal(path.dirname(resolved),temp);assert(path.basename(resolved).startsWith('ymcc-node-tools-test-'));assert(!fs.lstatSync(resolved).isSymbolicLink());assert.equal(fs.realpathSync(resolved).toLowerCase(),resolved.toLowerCase());fs.rmSync(resolved,{recursive:true,force:true});});
 const parent=path.join(base,'workspace','Mainline','YeManCC-source'),repo=path.join(parent,'Primary'),snapshot=path.join(parent,'Frozen'),nm=path.join(repo,'node_modules');fs.mkdirSync(nm,{recursive:true});
 const write=(rel,bytes)=>{const file=path.join(repo,...rel.split('/'));fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return file;};
 write('package.json',JSON.stringify({name:'synthetic-primary',private:true,type:'module'}));write('native/main.cpp','// SYNTHETIC, uncommitted\nint entireMain=1;\n');write('src/contract.txt','controlled');
 const roots={};
 function pkg(name,metadata={},files={}){
  const slug=name.replaceAll('/','+'),root=path.join(nm,'.pnpm',slug+'@1.0.0','node_modules',...name.split('/'));fs.mkdirSync(root,{recursive:true});
  writeJson(path.join(root,'package.json'),{name,version:'1.0.0',main:'index.js',...metadata});fs.writeFileSync(path.join(root,'index.js'),throwing);fs.writeFileSync(path.join(root,'NOTICE.txt'),'whole package non-entry '+name);
  for(const [rel,bytes] of Object.entries(files)){const file=path.join(root,...rel.split('/'));fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);}
  roots[name]=root;return root;
 }
 function linkAt(parentModules,name,root){const alias=path.join(parentModules,...name.split('/'));fs.mkdirSync(path.dirname(alias),{recursive:true});fs.symlinkSync(root,alias,'junction');return alias;}
 function dep(from,name){const root=roots[from];let modules=path.dirname(root);if(from.startsWith('@'))modules=path.dirname(modules);return linkAt(modules,name,roots[name]);}
 pkg('typescript');pkg('vue',{exports:{'.':{import:'./index.js',require:'./index.js'},'./compiler-sfc':'./compiler-sfc/index.mjs'},dependencies:{'@vue/compiler-sfc':'1.0.0'}},{'compiler-sfc/index.mjs':throwing});
 pkg('esbuild',{optionalDependencies:{'@esbuild/win32-x64':'1.0.0','@esbuild/linux-x64':'1.0.0'}});
 pkg('@vue/compiler-sfc',{exports:{'.':'./index.js'},dependencies:{codec:'1.0.0'},peerDependencies:{typescript:'*','optional-peer':'*'},peerDependenciesMeta:{'optional-peer':{optional:true}}});
 pkg('codec',{}, {'data/all-bytes.bin':Buffer.from(Array.from({length:256},(_,i)=>i))});pkg('@esbuild/win32-x64',{os:['win32'],cpu:['x64']},{'bin/tool.fake':'SYNTHETIC BINARY, NEVER RUN'});
 for(const name of DEFAULT_SEED_PACKAGES)linkAt(nm,name,roots[name]);dep('vue','@vue/compiler-sfc');dep('@vue/compiler-sfc','codec');dep('esbuild','@esbuild/win32-x64');
 const resolver=`import {createRequire} from 'node:module';\nconst specs=['typescript','vue/compiler-sfc','esbuild'];\nconsole.log(JSON.stringify({urls:specs.map(name=>import.meta.resolve(name)),cjs:createRequire(import.meta.url).resolve('esbuild')}));\n`;
 write('tools/resolve-only.mjs',resolver);
 const capture=createSourceSnapshot({repoRoot:repo,outDir:snapshot});assert.equal(capture.status,'PASS',JSON.stringify(capture.failure));const manifest=capture.manifestFile,manifestBytes=fs.readFileSync(manifest),manifestSha=sha(manifestBytes),bindingFile=path.join(snapshot,'.ymcc-source-snapshot',NODE_TOOLS_BINDING_NAME);
 return {base,parent,repo,snapshot,nm,roots,write,pkg,dep,linkAt,manifest,manifestBytes,manifestSha,bindingFile,bind(options={}){return bindSnapshotNodeTools({manifestFile:manifest,expectedSnapshotManifestSha256:manifestSha,primaryRepo:repo,...options});},verify(){return verifySnapshotNodeTools({bindingManifestFile:bindingFile});},mutatePackage(name,fn){const file=path.join(roots[name],'package.json'),value=read(file);fn(value);writeJson(file,value);}};
}
function bound(f){const result=f.bind();assert.equal(result.status,'PASS',JSON.stringify(result.failure));return result;}
function naturalNode(t,args){const r=spawnSync(process.execPath,args,{windowsHide:true,encoding:'utf8',timeout:20000});t.diagnostic(JSON.stringify({kind:'Node resolver/binding CLI over synthetic fixture only',exitCode:r.status,signal:r.signal,error:r.error?.code??null,stdout:r.stdout,stderr:r.stderr}));assert.equal(r.error,undefined);assert.equal(r.signal,null);return r;}

test('bind creates exactly three seed junctions inside an ordinary fresh root and never rewrites the snapshot manifest',t=>{
 const f=fixture(t),r=bound(f),root=path.join(f.snapshot,'node_modules');assert(fs.lstatSync(root).isDirectory());assert(!fs.lstatSync(root).isSymbolicLink());assert.deepEqual(fs.readdirSync(root).sort(),[...DEFAULT_SEED_PACKAGES].sort());
 for(const name of DEFAULT_SEED_PACKAGES){assert(fs.lstatSync(path.join(root,name)).isSymbolicLink());assert.equal(fs.realpathSync(path.join(root,name)),f.roots[name]);}
 assert(fs.readFileSync(f.manifest).equals(f.manifestBytes));assert.equal(r.expectedSnapshotManifestSha256,f.manifestSha);assert.equal(r.graphBeforeSha256,r.graphAfterSha256);assert.equal(r.packageCodeExecuted,false);assert.equal(r.sourceApprovalGranted,false);assert.equal(f.verify().status,'PASS');
});
test('actual ESM resolves all seed aliases and vue compiler-sfc; CJS resolves esbuild without executing packages',t=>{
 const f=fixture(t);bound(f);const r=naturalNode(t,[path.join(f.snapshot,'tools/resolve-only.mjs')]);assert.equal(r.status,0);const value=JSON.parse(r.stdout);assert.equal(value.urls.length,3);for(const url of value.urls)assert(url.startsWith('file:'));assert(value.urls[1].includes('compiler-sfc'));assert.equal(value.cjs,path.join(f.roots.esbuild,'index.js'));
});
test('full pnpm-style recursive graph includes exports fallback, binary bytes, required peers and distinct unresolved optional entries',t=>{
 const f=fixture(t),r=bound(f);assert.equal(r.graph.packageCount,6);assert.equal(r.graph.unresolvedOptional.length,2);assert.deepEqual(r.graph.unresolvedOptional.map(row=>row.name).sort(),['@esbuild/linux-x64','optional-peer']);
 for(const name of ['vue','@vue/compiler-sfc']){const entry=r.graph.seeds.find(row=>row.name===name)??r.graph.edges.find(row=>row.name===name);assert.equal(entry.resolution.via,'entry-upward-name-match');assert.equal(entry.resolution.metadataResolutionError,'ERR_PACKAGE_PATH_NOT_EXPORTED');}
 const codec=r.graph.packages.find(row=>row.name==='codec'),binary=codec.files.find(row=>row.path==='data/all-bytes.bin');assert.equal(binary.size,256);assert.equal(binary.sha256,sha(Buffer.from(Array.from({length:256},(_,i)=>i))));assert(codec.files.some(row=>row.path==='NOTICE.txt'));assert(codec.metadata.dependencies===undefined);assert(r.graph.edges.some(row=>row.name==='typescript'&&row.required&&row.resolved));
});
test('declared dependency cycles close by real package root without skipping package files',t=>{
 const f=fixture(t);f.mutatePackage('codec',m=>m.dependencies={vue:'*'});const r=bound(f);assert.equal(r.graph.packageCount,6);assert(r.graph.edges.some(row=>row.from===f.roots.codec&&row.name==='vue'));assert.equal(f.verify().status,'PASS');
});
test('optionalDependencies override a matching dependency while required peers remain required',t=>{
 const f=fixture(t);f.mutatePackage('codec',m=>{m.dependencies={absent:'*'};m.optionalDependencies={absent:'*'};});const r=bound(f),row=r.graph.unresolvedOptional.find(row=>row.name==='absent');assert(row);assert.equal(row.required,false);assert.equal(row.declarations.length,2);assert(row.declarations.every(row=>row.optional));
});
for(const [name,mutate] of [
 ['required dependency',f=>f.mutatePackage('codec',m=>m.dependencies={'required-not-installed':'*'})],
 ['required peer',f=>f.mutatePackage('codec',m=>m.peerDependencies={'required-peer-not-installed':'*'})],
 ['seed',f=>fs.unlinkSync(path.join(f.nm,'typescript'))],
])test('missing '+name+' fails with a retained FAIL receipt and cannot be retried',t=>{
 const f=fixture(t);mutate(f);const r=f.bind();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_REQUIRED_PACKAGE_MISSING');assert.equal(read(f.bindingFile).status,'FAIL');assert(fs.lstatSync(path.join(f.snapshot,'node_modules')).isDirectory());assert.equal(f.verify().status,'FAIL');assert.throws(()=>f.bind(),/FRESH_BINDING_REQUIRED/);
});
test('installed but unresolvable optional package fails rather than being reported as uninstalled',t=>{
 const f=fixture(t);f.pkg('broken-optional',{exports:{'./only':'./index.js'}});f.dep('esbuild','broken-optional');f.mutatePackage('esbuild',m=>m.optionalDependencies['broken-optional']='*');const r=f.bind();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_INSTALLED_PACKAGE_UNRESOLVABLE');
});
for(const [name,mutate] of [
 ['non-entry file bytes',f=>fs.appendFileSync(path.join(f.roots.codec,'NOTICE.txt'),'changed')],
 ['new package file',f=>fs.writeFileSync(path.join(f.roots.codec,'new-data.bin'),'new')],
 ['deleted package file',f=>fs.unlinkSync(path.join(f.roots.codec,'NOTICE.txt'))],
 ['new declared dependency',f=>{f.pkg('new-dependency');f.dep('codec','new-dependency');f.mutatePackage('codec',m=>m.dependencies={'new-dependency':'*'});}],
 ['previously missing optional now installed',f=>{f.pkg('@esbuild/linux-x64');f.dep('esbuild','@esbuild/linux-x64');}],
 ['primary package metadata',f=>f.write('package.json',JSON.stringify({name:'synthetic-primary',type:'module',description:'changed primary declaration'}))],
])test('verify rejects '+name+' even when all seed entry bytes remain identical',t=>{
 const f=fixture(t);bound(f);const before=DEFAULT_SEED_PACKAGES.map(name=>sha(fs.readFileSync(path.join(f.roots[name],'index.js'))));mutate(f);assert.deepEqual(DEFAULT_SEED_PACKAGES.map(name=>sha(fs.readFileSync(path.join(f.roots[name],'index.js')))),before);const r=f.verify();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_GRAPH_CHANGED');assert.equal(read(f.bindingFile).status,'PASS');
});
for(const [name,mutate] of [
 ['extra alias',f=>fs.symlinkSync(f.roots.codec,path.join(f.snapshot,'node_modules/unexpected'),'junction')],
 ['missing alias',f=>fs.unlinkSync(path.join(f.snapshot,'node_modules/vue'))],
 ['different target',f=>{fs.unlinkSync(path.join(f.snapshot,'node_modules/vue'));fs.symlinkSync(f.roots.codec,path.join(f.snapshot,'node_modules/vue'),'junction');}],
 ['ordinary directory instead of junction',f=>{fs.unlinkSync(path.join(f.snapshot,'node_modules/vue'));fs.mkdirSync(path.join(f.snapshot,'node_modules/vue'));}],
 ['junction root instead of ordinary directory',f=>{fs.renameSync(path.join(f.snapshot,'node_modules'),path.join(f.snapshot,'old-aliases'));fs.symlinkSync(path.join(f.snapshot,'old-aliases'),path.join(f.snapshot,'node_modules'),'junction');}],
])test('verify rejects '+name+' without changing any receipt',t=>{
 const f=fixture(t);bound(f);const bytes=fs.readFileSync(f.bindingFile);mutate(f);assert.equal(f.verify().status,'FAIL');assert(fs.readFileSync(f.bindingFile).equals(bytes));
});
test('primary seed alias retargeting cannot be hidden by Node resolver caches',t=>{
 const f=fixture(t);bound(f);const clone=path.join(f.nm,'.pnpm/vue-clone/node_modules/vue');fs.mkdirSync(path.dirname(clone),{recursive:true});fs.cpSync(f.roots.vue,clone,{recursive:true});fs.unlinkSync(path.join(f.nm,'vue'));fs.symlinkSync(clone,path.join(f.nm,'vue'),'junction');assert.equal(f.verify().status,'FAIL');
});
test('snapshot manifest byte changes reject verification even if its parsed JSON is identical',t=>{
 const f=fixture(t);bound(f);fs.appendFileSync(f.manifest,'\n');const r=f.verify();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_SNAPSHOT_SHA_CHANGED');
});
test('wrong caller expected snapshot SHA fails before creating aliases or a binding receipt',t=>{
 const f=fixture(t);assert.throws(()=>f.bind({expectedSnapshotManifestSha256:'0'.repeat(64)}),/SNAPSHOT_SHA_CHANGED/);assert(!fs.existsSync(path.join(f.snapshot,'node_modules')));assert(!fs.existsSync(f.bindingFile));
});
for(const [name,mutate,code] of [
 ['non-PASS snapshot',m=>m.status='FAIL','SUCCESSFUL_SNAPSHOT_REQUIRED'],
 ['wrong snapshot root',m=>m.snapshotRoot=path.dirname(m.snapshotRoot),'SNAPSHOT_ROOT_DIFFERS'],
 ['wrong original root',m=>m.sourceRoot=path.dirname(m.sourceRoot),'SNAPSHOT_ROOT_DIFFERS'],
])test('rejects '+name+' even with caller pin for those bytes',t=>{
 const f=fixture(t),m=read(f.manifest);mutate(m);writeJson(f.manifest,m);const pin=sha(fs.readFileSync(f.manifest));assert.throws(()=>f.bind({expectedSnapshotManifestSha256:pin}),new RegExp(code));assert(!fs.existsSync(path.join(f.snapshot,'node_modules')));
});
test('binding path cannot be moved to another location and reused',t=>{
 const f=fixture(t);bound(f);const relocated=path.join(f.base,'relocated-binding.json');fs.copyFileSync(f.bindingFile,relocated);assert.equal(verifySnapshotNodeTools({bindingManifestFile:relocated}).status,'FAIL');
});
test('fresh root rejects a preexisting node_modules and preserves its bytes',t=>{
 const f=fixture(t),root=path.join(f.snapshot,'node_modules');fs.mkdirSync(root);fs.writeFileSync(path.join(root,'owned.txt'),'existing');assert.throws(()=>f.bind(),/FRESH_NODE_MODULES_REQUIRED/);assert.equal(fs.readFileSync(path.join(root,'owned.txt'),'utf8'),'existing');assert(!fs.existsSync(f.bindingFile));
});
test('wx binding evidence refuses a preexisting receipt without rewriting it',t=>{
 const f=fixture(t);fs.writeFileSync(f.bindingFile,'preexisting receipt');assert.throws(()=>f.bind(),/FRESH_BINDING_REQUIRED/);assert.equal(fs.readFileSync(f.bindingFile,'utf8'),'preexisting receipt');assert(!fs.existsSync(path.join(f.snapshot,'node_modules')));
});
test('whole graph drift during bind leaves junctions and a FAIL receipt without deleting or retrying',t=>{
 const f=fixture(t),r=f.bind({onProgress:e=>{if(e.phase==='graph-before')fs.appendFileSync(path.join(f.roots.codec,'NOTICE.txt'),'concurrent change');}});assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_GRAPH_DRIFT');assert.notEqual(r.graphBeforeSha256,r.graphAfterSha256);assert(fs.lstatSync(path.join(f.snapshot,'node_modules/vue')).isSymbolicLink());assert.equal(read(f.bindingFile).status,'FAIL');
});
test('snapshot manifest drift during bind leaves a FAIL receipt and never rewrites that manifest',t=>{
 const f=fixture(t),r=f.bind({onProgress:e=>{if(e.phase==='aliases-created')fs.appendFileSync(f.manifest,'\n');}});assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_SNAPSHOT_SHA_CHANGED');assert(fs.readFileSync(f.manifest).equals(Buffer.concat([f.manifestBytes,Buffer.from('\n')])));
});
test('alias name injection during bind fails with all created evidence retained',t=>{
 const f=fixture(t),r=f.bind({onProgress:e=>{if(e.phase==='aliases-created')fs.writeFileSync(path.join(f.snapshot,'node_modules/unknown'),'unknown');}});assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_ALIAS_NAMES_CHANGED');assert(fs.existsSync(path.join(f.snapshot,'node_modules/unknown')));
});
test('tool PASS cannot approve changed original main; separate source compare remains STALE',t=>{
 const f=fixture(t);bound(f);f.write('native/main.cpp','// same hypothetical tested functions\nint unrelatedChanged=77;\n');const tools=f.verify(),source=compareSourceSnapshot({manifestFile:f.manifest,repoRoot:f.repo});assert.equal(tools.status,'PASS');assert.equal(tools.sourceCurrentnessChecked,false);assert.equal(tools.sourceApprovalGranted,false);assert.equal(tools.requiresSeparateSourceCurrentComparison,true);assert.equal(source.status,'STALE');assert(source.sourceDiff.changed.some(row=>row.path==='native/main.cpp'));assert.equal(source.snapshotDiff.equal,true);
});
test('binding receipt cannot promote tool PASS into source approval',t=>{
 const f=fixture(t);bound(f);const saved=read(f.bindingFile);saved.sourceApprovalGranted=true;writeJson(f.bindingFile,saved);const r=f.verify();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_SCOPE_CHANGED');
});
test('saved graph hash cannot omit a recorded package file',t=>{
 const f=fixture(t);bound(f);const saved=read(f.bindingFile);saved.graph.packages[0].files.pop();writeJson(f.bindingFile,saved);assert.equal(f.verify().failure.code,'NODE_TOOLS_SAVED_GRAPH_INVALID');
});
test('changed original capture receipt rejects binding before node_modules creation',t=>{
 const f=fixture(t);fs.appendFileSync(path.join(f.snapshot,'.ymcc-source-snapshot/source-before.json'),'\n');assert.throws(()=>f.bind(),/CAPTURE_REFERENCE_CHANGED/);assert(!fs.existsSync(path.join(f.snapshot,'node_modules')));
});
test('primary seed roots outside the primary node_modules realroot are rejected',t=>{
 const f=fixture(t),outside=path.join(f.base,'external-package');fs.mkdirSync(outside);writeJson(path.join(outside,'package.json'),{name:'typescript',version:'1.0.0',main:'index.js'});fs.writeFileSync(path.join(outside,'index.js'),throwing);fs.unlinkSync(path.join(f.nm,'typescript'));fs.symlinkSync(outside,path.join(f.nm,'typescript'),'junction');const r=f.bind();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_PACKAGE_OUTSIDE_PRIMARY');
});
test('package junction escaping the primary tool root is rejected; none of its bytes are authorized',t=>{
 const f=fixture(t),outside=path.join(f.base,'outside-package-tree');fs.mkdirSync(outside);fs.writeFileSync(path.join(outside,'opaque.fake'),'SYNTHETIC NOT A TOOL');fs.symlinkSync(outside,path.join(f.roots.codec,'escape'),'junction');const r=f.bind();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_PACKAGE_OUTSIDE_PRIMARY');
});
test('in-root package directory links record ordered paths and full target file hashes',t=>{
 const f=fixture(t);fs.symlinkSync(path.join(f.roots.codec,'data'),path.join(f.roots.codec,'linked-data'),'junction');const r=bound(f),p=r.graph.packages.find(row=>row.name==='codec');assert.equal(p.links.length,1);assert.equal(p.links[0].path,'linked-data');assert(p.files.some(row=>row.path==='linked-data/all-bytes.bin'));assert.equal(f.verify().status,'PASS');
});
test('package directory link cycles fail closed instead of silently omitting content',t=>{
 const f=fixture(t);fs.symlinkSync(f.roots.codec,path.join(f.roots.codec,'cycle'),'junction');const r=f.bind();assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'NODE_TOOLS_PACKAGE_LINK_CYCLE');
});
test('CLI parse rejects mixed, duplicate and incomplete authority arguments',()=>{
 assert.equal(parseSnapshotNodeToolArguments(['--help']).mode,'help');assert.equal(parseSnapshotNodeToolArguments(['--verify','B']).mode,'verify');assert.equal(parseSnapshotNodeToolArguments(['--bind','M','--repo','R','--expected-sha','S']).mode,'bind');
 for(const args of [[],['--verify'],['--bind','M','--repo','R'],['--verify','B','--repo','R'],['--verify','B','--verify','C'],['--bind','M','--expected-sha','S','--repo','R','--verify','B'],['--execute','tool']])assert.throws(()=>parseSnapshotNodeToolArguments(args));
});
test('actual Node binding CLI exits naturally for bind and verify; stale tool files give exit 2',t=>{
 const f=fixture(t),cli=path.join(path.dirname(fileURLToPath(import.meta.url)),'snapshot-node-tools.mjs');let r=naturalNode(t,[cli,'--bind',f.manifest,'--expected-sha',f.manifestSha,'--repo',f.repo]);assert.equal(r.status,0);assert.equal(JSON.parse(r.stdout).sourceApprovalGranted,false);r=naturalNode(t,[cli,'--verify',f.bindingFile]);assert.equal(r.status,0);fs.appendFileSync(path.join(f.roots.codec,'NOTICE.txt'),'tool drift');r=naturalNode(t,[cli,'--verify',f.bindingFile]);assert.equal(r.status,2);assert.equal(JSON.parse(r.stdout).status,'FAIL');
});

// Snapshot-only additions: the preceding original 42 tests remain byte-identical.
import {validateCaptureVerification} from './snapshot-node-tools.mjs';
import {verifyCapturedSourceSnapshot} from './source-snapshot.mjs';
function historicalFixture(t,{partial=false}={}){
 const f=fixture(t),historicalRoot=path.join(f.parent,'Historical');let changed=false;
 const capture=createSourceSnapshot({repoRoot:f.repo,outDir:historicalRoot,onProgress:event=>{
  if(partial&&event.phase==='after-copy-file')throw Object.assign(Error('SYNTHETIC_PARTIAL_CAPTURE'),{code:'SYNTHETIC_PARTIAL_CAPTURE'});
  if(!partial&&event.phase==='before-source-after'){changed=true;f.write('src/contract.txt','UI SOURCE MOVED AFTER COMPLETE COPY');}
 }});
 assert.equal(capture.status,'FAIL');assert.equal(capture.isCurrent,false);if(!partial){assert(changed);assert.equal(capture.failure.code,'SNAPSHOT_SOURCE_OR_COPY_DRIFT');assert.equal(capture.snapshotDiff.equal,true);assert.equal(capture.copyVerifiedFiles,capture.files.length);}
 const historicalManifest=capture.manifestFile,pin=sha(fs.readFileSync(historicalManifest)),bindingFile=path.join(historicalRoot,'.ymcc-source-snapshot',NODE_TOOLS_BINDING_NAME);
 return {...f,historicalRoot,historicalManifest,pin,historicalCapture:capture,historicalBindingFile:bindingFile,bindHistorical(options={}){return bindSnapshotNodeTools({manifestFile:historicalManifest,expectedSnapshotManifestSha256:pin,primaryRepo:f.repo,allowHistoricalSnapshot:true,...options});},verifyHistorical(){return verifySnapshotNodeTools({bindingManifestFile:bindingFile});}};
}
function rejectsHistoricalBeforeWrite(f,options={}){
 const r=failResult(()=>f.bindHistorical(options));assert(!fs.existsSync(path.join(f.historicalRoot,'node_modules')));assert(!fs.existsSync(f.historicalBindingFile));return r;
}
test('snapshot-only complete historical FAIL may bind tools while source remains unapproved and original FAIL bytes remain intact',t=>{
 const f=historicalFixture(t),before=fs.readFileSync(f.historicalManifest),r=f.bindHistorical();assert.equal(r.status,'PASS',JSON.stringify(r.failure));assert.equal(r.allowHistoricalSnapshot,true);assert.equal(r.creationStatus,'FAIL');assert.equal(r.isCurrent,false);assert.equal(r.sourceCurrentAtBind,false);assert.equal(r.sourceApprovalGranted,false);assert.equal(r.sourceCurrentnessChecked,true);assert.equal(r.wholeCaptureVerification.status,'FROZEN_SOURCE_VERIFIED');assert.equal(r.wholeCaptureVerification.snapshotIntegrityVerified,true);assert.equal(r.wholeCaptureVerification.manifestSha256,f.pin);assert.deepEqual(r.wholeCaptureVerification,r.captureVerification);assert.equal(r.captureVerificationBefore.snapshotIntegrityVerified,true);assert.equal(r.verifierCodeSha256,sha(fs.readFileSync(path.join(path.dirname(fileURLToPath(import.meta.url)),'source-snapshot.mjs'))));assert.equal(r.verifierSource.sha256,r.verifierCodeSha256);
 const v=f.verifyHistorical();assert.equal(v.status,'PASS',JSON.stringify(v.failure));assert.equal(v.creationStatus,'FAIL');assert.equal(v.sourceCurrentAtBind,false);assert.equal(v.sourceCurrentAtVerify,false);assert.equal(v.isCurrent,false);assert.equal(v.sourceApprovalGranted,false);assert.equal(v.wholeCaptureVerification.sourceDiff.equal,false);assert.equal(v.wholeCaptureVerification.snapshotDiff.equal,true);assert(fs.readFileSync(f.historicalManifest).equals(before));assert.equal(read(f.historicalManifest).status,'FAIL');assert.throws(()=>compareSourceSnapshot({manifestFile:f.historicalManifest,repoRoot:f.repo}),/SUCCESSFUL_MANIFEST_REQUIRED/);
});
test('snapshot-only historical FAIL is still refused by unspecified or explicitly false default authority',t=>{
 const f=historicalFixture(t);for(const options of [{manifestFile:f.historicalManifest,expectedSnapshotManifestSha256:f.pin,primaryRepo:f.repo},{manifestFile:f.historicalManifest,expectedSnapshotManifestSha256:f.pin,primaryRepo:f.repo,allowHistoricalSnapshot:false}])assert.throws(()=>bindSnapshotNodeTools(options),/SUCCESSFUL_SNAPSHOT_REQUIRED/);assert(!fs.existsSync(path.join(f.historicalRoot,'node_modules')));
});
test('snapshot-only flag must be a real boolean and caller SHA must bind the original FAIL manifest',t=>{
 const f=historicalFixture(t);assert.equal(rejectsHistoricalBeforeWrite(f,{allowHistoricalSnapshot:'true'}).failure.code,'NODE_TOOLS_HISTORICAL_FLAG_INVALID');assert.equal(rejectsHistoricalBeforeWrite(f,{expectedSnapshotManifestSha256:'0'.repeat(64)}).failure.code,'NODE_TOOLS_SNAPSHOT_SHA_CHANGED');
});
test('snapshot-only also accepts a complete PASS capture through the same real verifier without granting source approval',t=>{
 const f=fixture(t),r=f.bind({allowHistoricalSnapshot:true});assert.equal(r.status,'PASS',JSON.stringify(r.failure));assert.equal(r.creationStatus,'PASS');assert.equal(r.isCurrent,true);assert.equal(r.sourceCurrentAtBind,true);assert.equal(r.sourceApprovalGranted,false);assert.equal(r.wholeCaptureVerification.snapshotIntegrityVerified,true);assert.equal(f.verify().status,'PASS');
});
for(const [name,mutate] of [
 ['changed included file',f=>fs.appendFileSync(path.join(f.historicalRoot,'src/contract.txt'),'tampered copy')],
 ['changed full main outside hypothetical tested functions',f=>fs.writeFileSync(path.join(f.historicalRoot,'native/main.cpp'),'// tested method unchanged\nint unrelatedMain=999;\n')],
 ['new included file',f=>fs.writeFileSync(path.join(f.historicalRoot,'extra-source.mjs'),'new copy member')],
 ['deleted included file',f=>fs.unlinkSync(path.join(f.historicalRoot,'src/contract.txt'))],
])test('snapshot-only refuses '+name+' before any tool aliases are created',t=>{
 const f=historicalFixture(t);mutate(f);const r=rejectsHistoricalBeforeWrite(f);assert.equal(r.failure.code,'SNAPSHOT_CAPTURED_VERSION_CHANGED');
});
test('snapshot-only partial copy cannot be bound even with its exact caller-pinned FAIL manifest',t=>{
 const f=historicalFixture(t,{partial:true}),r=rejectsHistoricalBeforeWrite(f);assert.equal(r.failure.code,'SNAPSHOT_COMPLETE_CAPTURE_MANIFEST_REQUIRED');
});
test('snapshot-only forged drift label cannot turn partial copy into a complete captured source',t=>{
 const f=historicalFixture(t,{partial:true}),m=read(f.historicalManifest);m.failure.code='SNAPSHOT_SOURCE_OR_COPY_DRIFT';writeJson(f.historicalManifest,m);const pin=sha(fs.readFileSync(f.historicalManifest));const r=rejectsHistoricalBeforeWrite(f,{expectedSnapshotManifestSha256:pin});assert.notEqual(r.status,'PASS');
});
test('snapshot-only forged copy count is rejected by parent verifier even with a newly matching caller SHA',t=>{
 const f=historicalFixture(t),m=read(f.historicalManifest);m.copyVerifiedFiles--;writeJson(f.historicalManifest,m);const r=rejectsHistoricalBeforeWrite(f,{expectedSnapshotManifestSha256:sha(fs.readFileSync(f.historicalManifest))});assert.equal(r.failure.code,'SNAPSHOT_CAPTURE_IDENTITY_DIFFERS');
});
test('snapshot-only changed saved capture reference is not excused by source drift',t=>{
 const f=historicalFixture(t);fs.appendFileSync(path.join(f.historicalRoot,'.ymcc-source-snapshot/source-before.json'),'\n');const r=rejectsHistoricalBeforeWrite(f);assert.equal(r.failure.code,'SNAPSHOT_EVIDENCE_HASH_CHANGED');
});
test('snapshot-only read-only verification rejects copy corruption introduced after successful tool bind',t=>{
 const f=historicalFixture(t),r=f.bindHistorical();assert.equal(r.status,'PASS');const bytes=fs.readFileSync(f.historicalBindingFile);fs.appendFileSync(path.join(f.historicalRoot,'src/contract.txt'),'late copy corruption');const v=f.verifyHistorical();assert.equal(v.status,'FAIL');assert.equal(v.failure.code,'SNAPSHOT_CAPTURED_VERSION_CHANGED');assert(fs.readFileSync(f.historicalBindingFile).equals(bytes));assert.equal(read(f.historicalManifest).status,'FAIL');
});
test('snapshot-only copy corruption during binding yields retained tool FAIL evidence and never rewrites creation FAIL',t=>{
 const f=historicalFixture(t),r=f.bindHistorical({onProgress:event=>{if(event.phase==='aliases-created')fs.appendFileSync(path.join(f.historicalRoot,'src/contract.txt'),'during tool bind');}});assert.equal(r.status,'FAIL');assert.equal(r.failure.code,'SNAPSHOT_CAPTURED_VERSION_CHANGED');assert.equal(read(f.historicalBindingFile).status,'FAIL');assert(fs.lstatSync(path.join(f.historicalRoot,'node_modules/vue')).isSymbolicLink());assert.equal(read(f.historicalManifest).status,'FAIL');assert.equal(sha(fs.readFileSync(f.historicalManifest)),f.pin);
});
test('snapshot-only verify recomputes strict current source status independently of captured creation FAIL and tool status',t=>{
 const f=historicalFixture(t),r=f.bindHistorical();assert.equal(r.status,'PASS');assert.equal(r.sourceCurrentAtBind,false);f.write('src/contract.txt','controlled');const v=f.verifyHistorical();assert.equal(v.status,'PASS',JSON.stringify(v.failure));assert.equal(v.creationStatus,'FAIL');assert.equal(v.sourceCurrentAtBind,false);assert.equal(v.sourceCurrentAtVerify,true);assert.equal(v.isCurrent,true);assert.equal(v.sourceApprovalGranted,false);assert.equal(v.wholeCaptureVerification.isCurrent,true);assert.equal(read(f.historicalManifest).status,'FAIL');
});
test('snapshot-only guard schema samples are contract counterexamples only, never evidence of executing a verifier',()=>{
 const pin='a'.repeat(64),sample={status:'FROZEN_SOURCE_VERIFIED',snapshotIntegrityVerified:true,creationStatus:'FAIL',isCurrent:false,sourceApprovalGranted:false,manifestSha256:pin,sourceDiff:{equal:false}};const options={creationStatus:'FAIL',expectedManifestSha256:pin};assert.equal(validateCaptureVerification(sample,options),sample);
 for(const mutate of [p=>p.status='PASS',p=>p.snapshotIntegrityVerified=false,p=>p.snapshotIntegrityVerified='true',p=>p.creationStatus='PASS',p=>p.isCurrent='false',p=>p.sourceApprovalGranted=true,p=>p.manifestSha256='b'.repeat(64),p=>p.isCurrent=true]){const p=structuredClone(sample);mutate(p);assert.throws(()=>validateCaptureVerification(p,options));}
});
for(const [name,mutate] of [
 ['whole verification receipt',m=>m.wholeCaptureVerification.snapshotIntegrityVerified=false],
 ['source current at bind',m=>m.isCurrent=true],
 ['verifier code SHA',m=>m.verifierCodeSha256='0'.repeat(64)],
])test('snapshot-only rejects tampered '+name+' on read-only verify',t=>{
 const f=historicalFixture(t),r=f.bindHistorical();assert.equal(r.status,'PASS');const m=read(f.historicalBindingFile);mutate(m);writeJson(f.historicalBindingFile,m);assert.equal(f.verifyHistorical().status,'FAIL');
});
test('snapshot-only CLI flag is explicit, unique and forbidden on verify',()=>{
 for(const args of [['--snapshot-only','--bind','M','--expected-sha','S','--repo','R'],['--bind','M','--expected-sha','S','--repo','R','--snapshot-only']])assert.equal(parseSnapshotNodeToolArguments(args).allowHistoricalSnapshot,true);
 assert.equal(parseSnapshotNodeToolArguments(['--bind','M','--expected-sha','S','--repo','R']).allowHistoricalSnapshot,undefined);
 for(const args of [['--snapshot-only'],['--snapshot-only','true'],['--verify','B','--snapshot-only'],['--bind','M','--expected-sha','S','--repo','R','--snapshot-only','--snapshot-only']])assert.throws(()=>parseSnapshotNodeToolArguments(args));
});
test('snapshot-only actual CLI refuses default FAIL then binds and verifies complete historical capture naturally',t=>{
 const f=historicalFixture(t),cli=path.join(path.dirname(fileURLToPath(import.meta.url)),'snapshot-node-tools.mjs'),args=[cli,'--bind',f.historicalManifest,'--expected-sha',f.pin,'--repo',f.repo];let r=naturalNode(t,args);assert.equal(r.status,3);assert.equal(JSON.parse(r.stderr).failure.code,'NODE_TOOLS_SUCCESSFUL_SNAPSHOT_REQUIRED');r=naturalNode(t,[...args,'--snapshot-only']);assert.equal(r.status,0);const value=JSON.parse(r.stdout);assert.equal(value.creationStatus,'FAIL');assert.equal(value.isCurrent,false);assert.equal(value.snapshotIntegrityVerified,true);assert.equal(value.sourceApprovalGranted,false);r=naturalNode(t,[cli,'--verify',f.historicalBindingFile]);assert.equal(r.status,0);assert.equal(JSON.parse(r.stdout).sourceApprovalGranted,false);assert.equal(read(f.historicalManifest).status,'FAIL');
});
