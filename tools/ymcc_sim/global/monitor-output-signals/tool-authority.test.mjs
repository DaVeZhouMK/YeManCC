// Tool-only contracts. Owned inert files never approve current runtime tools or execute packages.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import{root,hash}from'./common.mjs';
import{lockMonitorToolScope,verifyMonitorToolScope,composeMonitorToolArtifacts,verifyMonitorArtifactTable,observeMonitorToolRuntime,MONITOR_TOOL_LIMIT}from'./tool-authority.mjs';
import{loadUiToolDeclaration,verifyUiToolDeclaration,resolveUiToolPackage}from'../ui-navigation/tool-inputs.mjs';
const out=path.resolve(process.argv[3]??'');assert.equal(process.argv[2],'--evidence-dir');assert(!fs.existsSync(out));fs.mkdirSync(out,{recursive:true});
const cases=[];function check(name,fn){try{fn();cases.push({name,status:'PASS'});}catch(error){cases.push({name,status:'FAIL',error:error.stack});}}
let realScope;
check('current full Node plus UI plus OVL closure locks without package execution',()=>{realScope=lockMonitorToolScope(root);assert(realScope.artifacts.length<=MONITOR_TOOL_LIMIT);assert(realScope.ui.completeDeclaredFiles>0);assert(realScope.node.packageCount>0);assert(realScope.node.fileCount>0);assert.equal(realScope.sourceApproval,false);assert(realScope.artifacts.some(r=>r.roles.some(x=>x.startsWith('ui:'))&&r.roles.some(x=>x.startsWith('node:'))));});
check('complete current authority revalidates before any compiler or browser',()=>verifyMonitorToolScope(realScope));
check('missing UI closure row rejects stale PASS table',()=>{const rows=realScope.artifacts.slice(1);assert.throws(()=>verifyMonitorArtifactTable(rows,realScope.artifacts),/CLOSURE_MISSING_OR_EXTRA/);});
check('duplicate UI or Node row rejects even when count is unchanged',()=>{const rows=realScope.artifacts.slice();rows[1]=rows[0];assert.throws(()=>verifyMonitorArtifactTable(rows,realScope.artifacts),/ROW_INVALID_OR_DUPLICATE/);});
check('one-byte identity mismatch rejects complete table',()=>{const rows=structuredClone(realScope.artifacts);rows[0].sha256='0'.repeat(64);assert.throws(()=>verifyMonitorArtifactTable(rows,realScope.artifacts),/CLOSURE_CHANGED/);});
check('UI versus Node role loss rejects table',()=>{const rows=structuredClone(realScope.artifacts);rows[0].roles=[];assert.throws(()=>verifyMonitorArtifactTable(rows,realScope.artifacts),/ROLE_OR_ORDER_CHANGED/);});
check('arbitrary UI manifest forwarding rejects before package code',()=>assert.throws(()=>lockMonitorToolScope(root,{env:{YMCC_UI_TOOL_MANIFEST:path.join(out,'unbound.json')}}),/AMBIENT_UI_MANIFEST_FORWARDING/));
check('ambient Node preload or package path rejects',()=>{for(const name of ['NODE_OPTIONS','NODE_PATH'])assert.throws(()=>lockMonitorToolScope(root,{env:{[name]:'unbound'}}),/AMBIENT_NODE_FORWARDING/);});
check('observed CJS equal bytes at undeclared path grants no authority',()=>{const f=path.join(out,'unbound.cjs');fs.writeFileSync(f,'module.exports = {};');assert.throws(()=>observeMonitorToolRuntime(realScope,{requireCache:{unbound:{filename:f}}}),/LOADED_TOOL_OUTSIDE_DECLARATION/);});
check('finite closure budget rejects rather than truncates',()=>{const rows=Array.from({length:3073},(_,i)=>({path:path.join(out,'fake-'+i),sha256:'1'.repeat(64)}));assert.throws(()=>composeMonitorToolArtifacts({tools:rows},{files:[]},{artifact:rows[0],metadata:[],role:'test'}),/ARTIFACT_BUDGET_EXCEEDED/);});
const owned=path.join(out,'inert-ui-contract'),mods=path.join(owned,'node_modules');fs.mkdirSync(owned,{recursive:true});
for(const name of ['node','powershell','edge','esbuild'])fs.writeFileSync(path.join(owned,name+'.exe'),'inert-'+name);
for(const name of ['playwright','playwright-core']){const dir=path.join(mods,name);fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'package.json'),JSON.stringify({name,main:'index.js'}));fs.writeFileSync(path.join(dir,'index.js'),'throw Error("INERT_TOOL_MUST_NEVER_EXECUTE");');}
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(dir,e.name)):[path.join(dir,e.name)]);}
const manifestFile=path.join(out,'inert-ui-declaration.json');
const manifest={schemaVersion:1,kind:'ymcc-ui-navigation-selected-toolchain',caseId:'controller.ui-navigation',maxArtifactInputs:3072,
 files:walk(owned).map(p=>({path:p,sha256:hash(fs.readFileSync(p)),roles:['inert-contract-fixture']})),
 executables:Object.fromEntries(['node','powershell','edge','esbuild'].map(n=>{const p=path.join(owned,n+'.exe');return[n,{path:p,sha256:hash(fs.readFileSync(p))}]})),
 directories:['playwright','playwright-core'].map(n=>({path:path.join(mods,n),membership:'complete-recursive-files-no-reparse'})),playwrightModuleRoot:mods,playwrightCreateRequireAnchor:path.join(mods,'_inert.cjs'),sourceMembers:['tools/ymcc_sim/global/ui-navigation/templates/build.mjs','tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs'],sourceReplacement:{historicalDObserverIsNotExecutedOrDeclaredAsAToolHere:true}};
fs.writeFileSync(manifestFile,JSON.stringify(manifest));const opts={repoRoot:root,manifestPath:manifestFile},d=loadUiToolDeclaration(opts),entry=path.join(mods,'playwright/index.js');
check('inert explicit package contract resolves only pinned entry without executing it',()=>{verifyUiToolDeclaration(d);assert.equal(resolveUiToolPackage({name:'playwright',...opts}),entry);});
check('postflight catches changed byte in declared tool',()=>{const before=fs.readFileSync(entry);try{fs.appendFileSync(entry,'x');assert.throws(()=>verifyUiToolDeclaration(d),/UI_TOOL_FILE_CHANGED/);}finally{fs.writeFileSync(entry,before);}});
check('postflight catches missing declared tool',()=>{const before=fs.readFileSync(entry);try{fs.unlinkSync(entry);assert.throws(()=>verifyUiToolDeclaration(d),/UI_TOOL_FILE_CHANGED/);}finally{fs.writeFileSync(entry,before);}});
check('complete membership catches added undeclared file',()=>{const p=path.join(mods,'playwright/extra.js');try{fs.writeFileSync(p,'inert');assert.throws(()=>verifyUiToolDeclaration(d),/DIRECTORY_MEMBERSHIP_CHANGED/);}finally{fs.unlinkSync(p);}});
check('undeclared package never resolves ambient fallback',()=>assert.throws(()=>resolveUiToolPackage({name:'typescript',...opts}),/PACKAGE_NOT_UNIQUELY_DECLARED/));
// Descriptor imports are metadata-only; missing tool prerequisites must not break inventory.
const{getMonitorOutputAdapters}=await import('./adapter.mjs');
check('descriptor with absent artifact remains BLOCKED without throwing',()=>{const a=getMonitorOutputAdapters(path.join(out,'missing-repo'))[0];assert(a.blockedReason);assert.deepEqual(a.artifactInputs,[]);});
check('descriptor with available OVL but missing Node inputs remains BLOCKED',()=>{const repo=path.join(out,'owned-missing-node'),ovl=path.join(repo,realScope.templateArtifact.relative);fs.mkdirSync(path.dirname(ovl),{recursive:true});fs.copyFileSync(realScope.templateArtifact.artifact.path,ovl);fs.writeFileSync(path.join(repo,'package.json'),'{"name":"inert-missing-node","private":true}');const a=getMonitorOutputAdapters(repo)[0];assert.match(a.blockedReason,/MONITOR_DECLARED_INPUT_UNAVAILABLE/);assert.deepEqual(a.artifactInputs,[]);});
check('wrong approved UI tool pin rejects the owned declaration',()=>{const bad=structuredClone(d);bad.files[0].sha256='0'.repeat(64);assert.throws(()=>verifyUiToolDeclaration(bad),/UI_TOOL_FILE_CHANGED/);});
check('complete composer rejects cross-role conflicting hashes',()=>assert.throws(()=>composeMonitorToolArtifacts({tools:[{path:entry,sha256:'0'.repeat(64)}]},d,{artifact:d.files[0],metadata:[],role:'test'}),/ARTIFACT_HASH_CONFLICT/));
const report={schemaVersion:1,sourceApproval:false,kind:'monitor-tool-authority-contracts',status:cases.every(c=>c.status==='PASS')?'PASS':'FAIL',cases,realToolAuthority:realScope,inertFixtureRuntimeApproved:false,compilerExecuted:false,browserLaunched:false};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({status:report.status,cases:cases.length,completeArtifacts:realScope?.artifacts.length}));for(const c of cases)if(c.status!=='PASS')console.error(c.name+'\n'+c.error);process.exitCode=report.status==='PASS'?0:1;
