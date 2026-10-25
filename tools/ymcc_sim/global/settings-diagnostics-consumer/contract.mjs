import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{createRequire}from'node:module';
import{lockBundledNodeTools,verifyBundledToolScope,classifyBundledInput,BUNDLED_AUTHORITY_SOURCES,verifyBundledImplementation}from'../bundled-node-tool-authority.mjs';
import{loadUiToolDeclaration,verifyUiToolDeclaration,resolveUiToolPackage}from'../ui-navigation/tool-inputs.mjs';
import{resolveUiToolContext}from'../controller-device-diagram-consumer/contract.mjs';
import{sourcePath}from'../runner.mjs';
export const MODULE='tools/ymcc_sim/global/settings-diagnostics-consumer',CASE_ID='settings.diagnostics-consumer';
export const TOOL_SOURCES=[...BUNDLED_AUTHORITY_SOURCES,'tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs','tools/ymcc_sim/global/ui-navigation/toolchain-inputs.json','tools/ymcc_sim/global/controller-device-diagram-consumer/contract.mjs'];
export const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),hash=p=>sha(fs.readFileSync(p)),key=p=>path.resolve(p).toLowerCase();
export const need=(v,m)=>{if(!v)throw Error(m);},read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
export const inside=(r,p)=>{const s=path.relative(path.resolve(r),path.resolve(p));return s===''||(!path.isAbsolute(s)&&s!=='..'&&!s.startsWith('..'+path.sep));};
export const write=(p,v)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');};
export function lockTools(root){
 need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH,'AMBIENT_NODE_FORWARDING_REJECTED');verifyBundledImplementation(root);
 const authority=lockBundledNodeTools(root),uiContext=resolveUiToolContext(root,authority);
 need(!process.env.YMCC_UI_TOOL_MANIFEST||key(process.env.YMCC_UI_TOOL_MANIFEST)===key(uiContext.manifestPath),'AMBIENT_UI_MANIFEST_REJECTED');
 const ui=loadUiToolDeclaration(uiContext),uiProof=verifyUiToolDeclaration(ui),map=new Map();
 for(const [role,rows]of[['node',authority.tools],['ui',ui.files]])for(const row of rows){const p=fs.realpathSync(row.path),k=key(p),old=map.get(k);need(!old||old.sha256===row.sha256,'TOOL_PIN_COLLISION');map.set(k,{path:p,sha256:row.sha256,status:'OK',roles:[...new Set([...(old?.roles??[]),role])].sort()});}
 need(map.size<=3072,'TOOL_ARTIFACT_BUDGET');need(key(authority.nodeExecutable)===key(ui.executables.node),'NODE_DECLARATION_MISMATCH');need(key(authority.compilerBinary)===key(fs.realpathSync(ui.executables.esbuild)),'COMPILER_DECLARATION_MISMATCH');
 const playwrightEntry=resolveUiToolPackage({name:'playwright',...uiContext}),piniaEntry=resolveUiToolPackage({name:'pinia',...uiContext});
 return{authority,uiContext,ui,uiProof,artifacts:[...map.values()].sort((a,b)=>key(a.path).localeCompare(key(b.path),'en')),byReal:map,playwrightEntry,piniaEntry};
}
export function verifyTools(root,t){verifyBundledToolScope(t.authority,root);const fresh=loadUiToolDeclaration(t.uiContext);need(fresh.manifestSha256===t.ui.manifestSha256,'UI_DECLARATION_CHANGED');verifyUiToolDeclaration(fresh);for(const row of t.artifacts)need(hash(row.path)===row.sha256,'TOOL_CHANGED:'+row.path);return{status:'PASS',files:t.artifacts.length,nodeGraphSha256:t.authority.graphSha256,uiManifestSha256:t.ui.manifestSha256};}
export function runtimeTools(t){const modules=[];for(const p of Object.keys(createRequire(import.meta.url).cache)){const real=fs.realpathSync(p),pin=t.byReal.get(key(real));need(pin&&hash(real)===pin.sha256,'AMBIENT_CJS_TOOL_REJECTED:'+p);modules.push({path:real,sha256:pin.sha256,roles:pin.roles});}return{node:{path:process.execPath,pid:process.pid,sha256:hash(process.execPath)},compiler:{entry:t.authority.compilerEntry,binary:t.authority.compilerBinary,version:t.authority.compilerVersion},playwrightEntry:t.playwrightEntry,edgeExecutable:t.ui.executables.edge,actualCjsModules:modules,completeDeclaredArtifactCount:t.artifacts.length,observedIsNotCompleteGraph:true,osLoadedModuleGraph:false};}
export function pinSources(root){const spec=read(sourcePath(root,MODULE+'/spec.json'));let rows=spec.sources,authority={kind:'B43-manifest-selected-finite-view',baselineManifestSha256:spec.baselineManifestSha256,sourceApproval:false};
 const mf=path.join(root,'.ymcc-source-snapshot/manifest.json');if(fs.existsSync(mf)){const node=lockBundledNodeTools(root);need(node.mode==='snapshot-binding','SOURCE_MANIFEST_BOUND_AUTHORITY_REQUIRED');const binding=read(node.proof.bindingManifestFile);need(hash(mf)===binding.expectedSnapshotManifestSha256,'SOURCE_MANIFEST_CHANGED');const m=read(mf);need(key(m.snapshotRoot)===key(root),'SOURCE_MANIFEST_ROOT_MISMATCH');rows=spec.sources.map(row=>{const member=m.files.find(r=>r.path===row.path);need(member,'SOURCE_MEMBER_MISSING:'+row.path);return{path:row.path,sha256:member.sha256};});authority={kind:'explicit-bound-source-manifest',manifestFile:mf,manifestSha256:hash(mf),sourceApproval:false};}
 for(const row of rows)need(hash(sourcePath(root,row.path))===row.sha256,'SOURCE_PIN_CHANGED:'+row.path);
 return{...spec,sources:rows,authority};
}
export function verifySources(root,rows){for(const row of rows)need(hash(sourcePath(root,row.path))===row.sha256,'SOURCE_CHANGED:'+row.path);}
export function classify(root,p,bytes,t,out){const real=fs.realpathSync(p),pin=t.byReal.get(key(real));if(pin){need(sha(bytes)===pin.sha256&&hash(real)===pin.sha256,'OBSERVED_TOOL_CHANGED');return{path:real,sha256:pin.sha256,role:'tool'};}if(out&&inside(out,real)){need(key(real)===key(p),'OWNED_SOURCE_FORWARDING');return{path:real,sha256:sha(bytes),role:'owned-case-source'};}const row=classifyBundledInput(root,p,bytes,t.authority);need(row.role==='source','UNBOUND_TOOL');return{...row.row,role:'source'};}
