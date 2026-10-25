import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { resolveUiToolPackage, isUiProductBundleInput, getUiNavigationArtifactInputs, loadUiToolDeclaration, validateToolManifest, verifyUiToolDeclaration, verifyUiNodeAndCjs, checkUiBrowserTerminal } from './tool-inputs.mjs';
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
function fixture(t){
 const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'ymcc-ui-tools-contract-'));
 t.after(()=>{assert.equal(fs.realpathSync(root),root);assert.equal(path.dirname(root),parent);assert(path.basename(root).startsWith('ymcc-ui-tools-contract-'));fs.rmSync(root,{recursive:true});});
 for(const name of ['playwright','playwright-core'])fs.mkdirSync(path.join(root,name));
 const files=['node.exe','pwsh.exe','edge.exe','esbuild.exe','playwright/index.cjs','playwright-core/index.cjs'].map(relative=>{const file=path.join(root,relative);fs.writeFileSync(file,relative);return {path:file,sha256:hash(file)};});
 const manifest={schemaVersion:1,kind:'ymcc-ui-navigation-selected-toolchain',caseId:'controller.ui-navigation',maxArtifactInputs:3072,files,directories:[{path:root,membership:'complete-recursive-files-no-reparse'},{path:path.join(root,'playwright'),membership:'complete-recursive-files-no-reparse'},{path:path.join(root,'playwright-core'),membership:'complete-recursive-files-no-reparse'}],executables:Object.fromEntries(['node','powershell','edge','esbuild'].map((name,i)=>[name,files[i]])),playwrightModuleRoot:root,playwrightCreateRequireAnchor:path.join(root,'_ymcc_ui.cjs'),sourceMembers:['tools/ymcc_sim/global/ui-navigation/templates/build.mjs','tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs'],sourceReplacement:{historicalDObserverIsNotExecutedOrDeclaredAsAToolHere:true}};
 return {root,manifest,declaration:validateToolManifest(manifest)};
}
test('unrelated case enumeration reads metadata without touching selected runtimes or hashing',()=>{
 const methods=['existsSync','lstatSync','statSync','readdirSync','realpathSync'],originals=new Map(methods.map(k=>[k,fs[k]])),read=fs.readFileSync,createHash=crypto.createHash;let reads=0;
 try{for(const name of methods)fs[name]=()=>{throw Error('RUNTIME_ACCESS_DURING_ENUMERATION');};crypto.createHash=()=>{throw Error('HASH_DURING_ENUMERATION');};fs.readFileSync=(file,...args)=>{assert.equal(path.basename(String(file)),'toolchain-inputs.json');reads++;return read(file,...args);};const inputs=getUiNavigationArtifactInputs();assert.equal(inputs.length,2304);assert.equal(reads,1);assert.equal(new Set(inputs.map(p=>p.toLowerCase())).size,2304);}finally{for(const [k,v]of originals)fs[k]=v;fs.readFileSync=read;crypto.createHash=createHash;}
});
test('runtime verification rejects changed bytes including the final declared member',t=>{
 const {declaration,manifest}=fixture(t);assert.equal(verifyUiToolDeclaration(declaration).verifiedFiles,6);const last=manifest.files.at(-1);fs.appendFileSync(last.path,'changed');assert.throws(()=>verifyUiToolDeclaration(declaration),/UI_TOOL_FILE_CHANGED/);
});
test('runtime directory membership rejects an undeclared package file',t=>{
 const {root,declaration}=fixture(t);fs.writeFileSync(path.join(root,'playwright','unexpected.cjs'),'undeclared');assert.throws(()=>verifyUiToolDeclaration(declaration),/UI_TOOL_DIRECTORY_MEMBERSHIP_CHANGED/);
});
test('schema rejects traversal, duplicate paths, incorrect executable membership and excess inputs',t=>{
 const {manifest}=fixture(t);const bad=change=>{const copy=structuredClone(manifest);change(copy);assert.throws(()=>validateToolManifest(copy));};
 bad(m=>{m.files[0]={repoRelative:'../escape.exe',sha256:m.files[0].sha256};});bad(m=>m.files.push(m.files[0]));bad(m=>{m.executables.edge={path:path.join(m.playwrightModuleRoot,'undeclared-edge.exe'),sha256:m.files[2].sha256};});bad(m=>{m.executables.edge={...m.files[2],sha256:'f'.repeat(64)};});bad(m=>{m.files=Array.from({length:3073},()=>m.files[0]);});
});
test('actual CJS admission rejects an existing file absent from the selected declaration',t=>{
 const {root,manifest}=fixture(t);manifest.executables.node={path:process.execPath,sha256:hash(process.execPath)};manifest.files.push(manifest.executables.node);const declaration=validateToolManifest(manifest);const unknown=path.join(root,'unknown-loaded.cjs');fs.writeFileSync(unknown,'unknown');assert.throws(()=>verifyUiNodeAndCjs(declaration,{unknown:{filename:unknown}}),/UI_TOOL_UNDECLARED_CJS_MEMBER/);
});
test('browser terminal contract refuses wrong path or incomplete exit; no browser is started',t=>{
 const {declaration}=fixture(t),processRecord={pid:123,spawnfile:declaration.executables.edge};assert.equal(checkUiBrowserTerminal(declaration,processRecord,{exitCode:0,signal:null}).ownedHeadlessBrowser,true);for(const terminal of [{exitCode:null,signal:null},{exitCode:1,signal:null},{exitCode:0,signal:'SIGTERM'}])assert.throws(()=>checkUiBrowserTerminal(declaration,processRecord,terminal),/UI_TOOL_BROWSER_TERMINAL_OR_EXECUTABLE_MISMATCH/);assert.throws(()=>checkUiBrowserTerminal(declaration,{...processRecord,spawnfile:declaration.executables.node},{exitCode:0,signal:null}),/UI_TOOL_BROWSER_TERMINAL_OR_EXECUTABLE_MISMATCH/);
});

test('only the exact external product catalog is a source bundle member',()=>{
 const root=path.resolve('test-source');
 assert.equal(isUiProductBundleInput(root,path.join(root,'PowerControl/GameTitlePolicy.json')),true);
 assert.equal(isUiProductBundleInput(root,path.join(root,'PowerControl/settings-baseline.json')),true);
 assert.equal(isUiProductBundleInput(root,path.join(root,'PowerControl/unknown.json')),false);
 assert.equal(isUiProductBundleInput(root,path.join(root,'decky-plugin/src/performanceModel.ts')),true);
 assert.equal(isUiProductBundleInput(root,path.join(root,'decky-plugin/src/unknown.ts')),false);
 assert.equal(isUiProductBundleInput(root,path.join(root,'node_modules/unknown.json')),false);
 assert.equal(isUiProductBundleInput(root,path.resolve(root,'../PowerControl/GameTitlePolicy.json')),false);
});

test('a loaded physical file must resolve to an exact declared package member',t=>{
 const {root,manifest}=fixture(t);manifest.executables.node={path:process.execPath,sha256:hash(process.execPath)};manifest.files.push(manifest.executables.node);
 const physical=path.join(root,'physical-package');fs.mkdirSync(physical);const loaded=path.join(physical,'entry.cjs');fs.writeFileSync(loaded,'declared bytes');
 const link=path.join(root,'package-junction');fs.symlinkSync(physical,link,process.platform==='win32'?'junction':'dir');
 const declared=path.join(link,'entry.cjs');manifest.files.push({path:declared,sha256:hash(declared)});const declaration=validateToolManifest(manifest);
 assert.equal(verifyUiNodeAndCjs(declaration,{loaded:{filename:loaded}}).length,1);
 const unrelated=path.join(root,'same-bytes.cjs');fs.copyFileSync(loaded,unrelated);assert.throws(()=>verifyUiNodeAndCjs(declaration,{loaded:{filename:unrelated}}),/UI_TOOL_UNDECLARED_CJS_MEMBER/);
 fs.appendFileSync(loaded,'changed');assert.throws(()=>verifyUiNodeAndCjs(declaration,{loaded:{filename:loaded}}),/UI_TOOL_FILE_CHANGED/);
});

test('fixed source resolves only an exact declared package, entry and bytes without a root alias',t=>{
 const {root,manifest}=fixture(t),pkg=path.join(root,'package'),json=path.join(pkg,'package.json'),entry=path.join(pkg,'entry.cjs');fs.mkdirSync(pkg);
 fs.writeFileSync(json,JSON.stringify({name:'ymcc-pinned-tool',exports:'./entry.cjs'}));fs.writeFileSync(entry,'module.exports={};');
 manifest.files.push(...[json,entry].map(file=>({path:file,sha256:hash(file)})));const manifestPath=path.join(root,'tool-manifest.json');fs.writeFileSync(manifestPath,JSON.stringify(manifest));
 assert.equal(resolveUiToolPackage({name:'ymcc-pinned-tool',repoRoot:root,manifestPath}),entry);
 assert.throws(()=>resolveUiToolPackage({name:'../escape',repoRoot:root,manifestPath}),/UI_TOOL_PACKAGE_NAME_INVALID/);
 assert.throws(()=>resolveUiToolPackage({name:'unknown',repoRoot:root,manifestPath}),/UI_TOOL_PACKAGE_NOT_UNIQUELY_DECLARED/);
 fs.appendFileSync(entry,'changed');assert.throws(()=>resolveUiToolPackage({name:'ymcc-pinned-tool',repoRoot:root,manifestPath}),/UI_TOOL_FILE_CHANGED/);
});


test('shared tools remain fixed when the selected source root changes',()=>{
 const a=getUiNavigationArtifactInputs(),b=getUiNavigationArtifactInputs(path.resolve('absent-captured-ui-source'));
 assert.deepEqual(b,a,'captured source roots must not redirect the pinned shared tool files');
 assert.equal(a.length,2304);assert(a.every(p=>path.isAbsolute(p)));
 const selected=JSON.parse(fs.readFileSync(new URL('./toolchain-inputs.json',import.meta.url),'utf8'));
 assert(selected.files.every(p=>typeof p.path==='string'&&p.repoRelative===undefined));
 assert(selected.directories.every(p=>typeof p.path==='string'&&p.repoRelative===undefined));
 assert.equal(selected.directories.length,33);
 for(const spec of Object.values(selected.executables))assert(spec.repoRelative===undefined&&path.isAbsolute(spec.path));
});
