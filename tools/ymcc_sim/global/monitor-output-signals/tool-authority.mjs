/** Monitor-local composition of the reviewed Node graph and complete UI tool declaration. */
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {lockBundledNodeTools,verifyBundledToolScope,verifyBundledImplementation,sameArtifactSet,BUNDLED_AUTHORITY_SOURCES} from '../bundled-node-tool-authority.mjs';
import {loadUiToolDeclaration,verifyUiToolDeclaration,resolveUiToolPackage,verifyDeclaredFile} from '../ui-navigation/tool-inputs.mjs';
import {resolveTemplateArtifact,verifyTemplateArtifact} from './template-artifact.mjs';
import {hash,root} from './common.mjs';
export const MONITOR_TOOL_LIMIT=3072;
export const MONITOR_TOOL_SOURCES=Object.freeze([...BUNDLED_AUTHORITY_SOURCES,
 'tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs','tools/ymcc_sim/global/ui-navigation/toolchain-inputs.json',
 'tools/ymcc_sim/global/monitor-output-signals/tool-authority.mjs']);
const key=p=>path.resolve(p).toLowerCase();
const need=(ok,message)=>{if(!ok)throw Error('MONITOR_TOOL_'+message);};
const digest=p=>hash(fs.readFileSync(p));
const inside=(base,p)=>{const r=path.relative(base,p);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
function context(repoRoot,env){
 const manifestPath=path.resolve(repoRoot,'tools/ymcc_sim/global/ui-navigation/toolchain-inputs.json');
 need(!env.YMCC_UI_TOOL_MANIFEST||key(env.YMCC_UI_TOOL_MANIFEST)===key(manifestPath),'AMBIENT_UI_MANIFEST_FORWARDING_REJECTED');
 const helper=path.resolve(repoRoot,'tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs');
 if(env.YMCC_UI_TOOL_HELPER_URL)need(key(fileURLToPath(env.YMCC_UI_TOOL_HELPER_URL))===key(helper),'AMBIENT_UI_HELPER_FORWARDING_REJECTED');
 need(!env.NODE_PATH&&!env.NODE_OPTIONS,'AMBIENT_NODE_FORWARDING_REJECTED');
 return manifestPath;
}
// Existing helpers own graph/package/membership validation. This function only unions their rows and roles.
export function composeMonitorToolArtifacts(nodeAuthority,uiDeclaration,templateArtifact){
 const rows=new Map();
 const add=(row,roles)=>{need(path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256??''),'ARTIFACT_IDENTITY_REQUIRED');const k=key(row.path),old=rows.get(k);need(!old||old.sha256===row.sha256,'ARTIFACT_HASH_CONFLICT');rows.set(k,{path:old?.path??path.resolve(row.path),sha256:row.sha256,status:'OK',roles:[...new Set([...(old?.roles??[]),...roles])].sort()});};
 for(const row of nodeAuthority.tools)add(row,['node:complete-bound-graph']);
 for(const row of uiDeclaration.files)add(row,['ui:complete-declaration',...(row.roles??[]).map(role=>'ui:'+role)]);
 add(templateArtifact.artifact,['template:'+templateArtifact.role]);
 for(const row of templateArtifact.metadata)add(row,['template:verified-origin-metadata']);
 const result=[...rows.values()].sort((a,b)=>key(a.path).localeCompare(key(b.path),'en'));
 need(result.length<=MONITOR_TOOL_LIMIT,'ARTIFACT_BUDGET_EXCEEDED');return result;
}
export function verifyMonitorArtifactTable(actual,expected){
 sameArtifactSet(actual,expected);
 need(JSON.stringify(actual)===JSON.stringify(expected),'ARTIFACT_ROLE_OR_ORDER_CHANGED');return true;
}
function sourceHelpers(repoRoot){
 verifyBundledImplementation(repoRoot);
 need(key(fs.realpathSync(fileURLToPath(import.meta.url)))===key(fs.realpathSync(path.join(repoRoot,'tools/ymcc_sim/global/monitor-output-signals/tool-authority.mjs'))),'HELPER_FORWARDING_REJECTED');
 return Object.fromEntries(MONITOR_TOOL_SOURCES.map(relative=>[relative,digest(path.join(repoRoot,relative))]));
}
function uiScope(repoRoot,nodeAuthority,templateArtifact,env){
 const manifestPath=context(repoRoot,env);
 // Snapshot tool roots come only from the independently verified original binding; no ambient primary fallback.
 const toolRoot=nodeAuthority.mode==='snapshot-binding'?templateArtifact.sourceRoot:repoRoot;
 const d=loadUiToolDeclaration({repoRoot:toolRoot,manifestPath});verifyUiToolDeclaration(d);
 need(key(fs.realpathSync(process.execPath))===key(d.executables.node)&&key(nodeAuthority.nodeExecutable)===key(d.executables.node),'NODE_EXECUTABLE_MISMATCH');
 need(key(nodeAuthority.compilerBinary)===key(fs.realpathSync(d.executables.esbuild)),'UI_NODE_COMPILER_IDENTITY_MISMATCH');
 const playwrightEntry=resolveUiToolPackage({name:'playwright',repoRoot:toolRoot,manifestPath});
 const playwrightCoreEntry=resolveUiToolPackage({name:'playwright-core',repoRoot:toolRoot,manifestPath});
 return {declaration:d,scope:{repoRoot:path.resolve(toolRoot),manifestPath,manifestSha256:d.manifestSha256,executables:d.executables,directories:d.directories,versions:d.manifest.versions,playwrightEntry,playwrightCoreEntry,completeDeclaredFiles:d.files.length,packageCodeExecuted:false,osLoadedModuleGraph:false}};
}
export function lockMonitorToolScope(repoRoot=root,{env=process.env,bindingOptions={},templateArtifact}={}){
 context(repoRoot,env);
 const template=templateArtifact??resolveTemplateArtifact(repoRoot,bindingOptions);verifyTemplateArtifact(template);
 const binding=template.nodeToolAuthority?template.bindingOptions:undefined;
 const node=lockBundledNodeTools(repoRoot,{env,binding,limit:MONITOR_TOOL_LIMIT}),ui=uiScope(repoRoot,node,template,env);
 const artifacts=composeMonitorToolArtifacts(node,ui.declaration,template);
 return {schemaVersion:1,kind:'ymcc-monitor-tool-authority',repoRoot:fs.realpathSync(repoRoot),sourceApproval:false,sourceApprovalGranted:false,
 node,ui:ui.scope,templateArtifact:template,artifacts,sourceHelpers:sourceHelpers(repoRoot),
 meaning:'Complete declared Node/UI closure; observed loads are recorded separately. No OS loaded-module graph or physical monitor acceptance.'};
}
export function verifyMonitorToolScope(saved,{repoRoot=root,env=process.env}={}){
 need(saved?.schemaVersion===1&&saved.kind==='ymcc-monitor-tool-authority'&&saved.sourceApproval===false,'SCOPE_REQUIRED');
 need(key(saved.repoRoot)===key(fs.realpathSync(repoRoot)),'REPO_FORWARDING_REJECTED');context(repoRoot,env);
 const node=verifyBundledToolScope(saved.node,repoRoot,{limit:MONITOR_TOOL_LIMIT});verifyTemplateArtifact(saved.templateArtifact);
 const ui=uiScope(repoRoot,node,saved.templateArtifact,env);
 need(JSON.stringify(ui.scope)===JSON.stringify(saved.ui),'UI_AUTHORITY_CHANGED');
 verifyMonitorArtifactTable(saved.artifacts,composeMonitorToolArtifacts(node,ui.declaration,saved.templateArtifact));
 need(JSON.stringify(sourceHelpers(repoRoot))===JSON.stringify(saved.sourceHelpers),'SOURCE_HELPER_CHANGED');return saved;
}
export function monitorToolArtifactInputs(scope){return scope.artifacts.map(row=>row.path);}
export function loadMonitorCompiler(scope){
 const req=createRequire(path.join(scope.repoRoot,'package.json'));
 for(const p of [scope.node.nodeExecutable,scope.node.compilerEntry,scope.node.compilerBinary]){const row=scope.artifacts.find(r=>key(r.path)===key(p));need(row,'COMPILER_NOT_DECLARED');verifyDeclaredFile(row);}
 need(key(fs.realpathSync(req.resolve('esbuild')))===key(scope.node.compilerEntry),'COMPILER_FORWARDING_REJECTED');
 return req(scope.node.compilerEntry);
}
export function loadMonitorPlaywright(scope){
 const req=createRequire(path.join(scope.repoRoot,'package.json'));
 for(const p of [scope.ui.playwrightEntry,scope.ui.playwrightCoreEntry,scope.ui.executables.edge]){const row=scope.artifacts.find(r=>key(r.path)===key(p));need(row,'BROWSER_NOT_DECLARED');verifyDeclaredFile(row);}
 return req(scope.ui.playwrightEntry);
}
export function observeMonitorToolRuntime(scope,{requireCache=createRequire(import.meta.url).cache,bundleInputs=[],ownedRoot}={}){
 const pins=new Map(scope.artifacts.map(row=>[key(row.path),row]));
 const admitted=p=>{const physical=fs.realpathSync(p),row=pins.get(key(physical));need(row,'LOADED_TOOL_OUTSIDE_DECLARATION:'+p);verifyDeclaredFile(row);return row;};
 const cjs=[];for(const mod of Object.values(requireCache))if(mod?.filename){const row=admitted(mod.filename);cjs.push({path:fs.realpathSync(mod.filename),sha256:row.sha256,roles:row.roles});}
 const inputs=[];for(const input of bundleInputs){
  if(input==='<stdin>'||input.startsWith('memory:')){inputs.push({path:input,role:'virtual-memory-input'});continue;}
  const p=path.resolve(scope.repoRoot,input),physical=fs.realpathSync(p),pin=pins.get(key(physical));
  if(pin){verifyDeclaredFile(pin);inputs.push({path:physical,sha256:pin.sha256,role:'declared-tool'});}
  else{need(!physical.split(/[\\/]/).some(part=>part.toLowerCase()==='node_modules'),'BUNDLE_TOOL_OUTSIDE_DECLARATION:'+p);need(inside(scope.repoRoot,physical)||(ownedRoot&&inside(ownedRoot,physical)),'BUNDLE_SOURCE_OUTSIDE_SCOPE:'+p);inputs.push({path:inside(scope.repoRoot,physical)?path.relative(scope.repoRoot,physical).replaceAll('\\','/'):physical,sha256:digest(physical),role:'repository-or-owned-source'});}
 }
 admitted(process.execPath);
 return {schemaVersion:1,sourceApproval:false,node:{path:fs.realpathSync(process.execPath),sha256:digest(process.execPath),pid:process.pid},compiler:{entry:scope.node.compilerEntry,binary:scope.node.compilerBinary,version:scope.node.compilerVersion},browserEntrypoints:{playwright:scope.ui.playwrightEntry,playwrightCore:scope.ui.playwrightCoreEntry,edge:scope.ui.executables.edge},actualCjsModules:cjs,actualBundleInputs:inputs,completeArtifactCount:scope.artifacts.length,observedLoadsAreNotCompleteClosure:true,osLoadedModuleGraph:false,sourceHelpers:scope.sourceHelpers};
}
