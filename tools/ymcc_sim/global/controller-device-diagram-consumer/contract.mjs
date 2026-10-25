import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{createRequire}from'node:module';
import{lockBundledNodeTools,classifyBundledInput,verifyBundledToolScope,BUNDLED_AUTHORITY_SOURCES}from'../bundled-node-tool-authority.mjs';
import{loadUiToolDeclaration,verifyUiToolDeclaration}from'../ui-navigation/tool-inputs.mjs';
import{sourcePath}from'../runner.mjs';
export const MODULE='tools/ymcc_sim/global/controller-device-diagram-consumer';export const CASE_ID='controller.device-diagram-consumer';
export const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),hash=p=>sha(fs.readFileSync(p));
export const need=(v,code)=>{if(!v)throw Error(code);};
export const inside=(r,p)=>{const s=path.relative(path.resolve(r),path.resolve(p));return s===''||(!path.isAbsolute(s)&&s!=='..'&&!s.startsWith('..'+path.sep));};
export const key=p=>path.resolve(p).toLowerCase();
export const write=(p,v)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');};
export function pinSources(root){
 const spec=JSON.parse(fs.readFileSync(sourcePath(root,MODULE+'/spec.json'),'utf8')),targetRoot=fs.realpathSync(root),manifestFile=path.join(targetRoot,'.ymcc-source-snapshot','manifest.json');
 let sources=spec.sources,sourceAuthority={kind:'exact-finite-candidate-baseline',sourceRoot:targetRoot,sourceApprovalGranted:false};
 if(fs.existsSync(manifestFile)){
  const authority=lockBundledNodeTools(targetRoot);need(authority.mode==='snapshot-binding','FIXED_SOURCE_AUTHORITY_REQUIRED');
  const bindingFile=authority.proof.bindingManifestFile,binding=JSON.parse(fs.readFileSync(bindingFile,'utf8'));
  need(hash(bindingFile)===authority.proof.expectedBindingSha256&&hash(manifestFile)===binding.expectedSnapshotManifestSha256,'FIXED_SOURCE_MANIFEST_IDENTITY');
  const manifest=JSON.parse(fs.readFileSync(manifestFile,'utf8'));need(manifest.kind==='ymcc-fixed-source-snapshot'&&key(manifest.snapshotRoot)===key(targetRoot)&&Array.isArray(manifest.files),'FIXED_SOURCE_MANIFEST_SCOPE');
  sources=spec.sources.map(row=>{const fixed=manifest.files.find(item=>item.path===row.path);need(fixed&&/^[a-f0-9]{64}$/.test(fixed.sha256),'FIXED_SOURCE_MEMBER_UNDECLARED:'+row.path);return {path:row.path,sha256:fixed.sha256};});
  sourceAuthority={kind:'explicit-immutable-captured-source-manifest',sourceRoot:targetRoot,manifestFile,manifestSha256:hash(manifestFile),bindingFile,bindingSha256:hash(bindingFile),sourceApprovalGranted:false,baselineCandidatePinsAreReferenceOnly:true};
 }
 for(const row of sources)need(hash(sourcePath(targetRoot,row.path))===row.sha256,sourceAuthority.kind==='exact-finite-candidate-baseline'?'FIXED_SOURCE_MANIFEST_REQUIRED_FOR_CHANGED_DIAGRAM_INPUT:'+row.path:'CAPTURED_FIXED_SOURCE_CHANGED:'+row.path);
 return {...spec,baselineCandidateSources:spec.sources,sources,sourceAuthority};
}
export const TOOL_SOURCES=[...BUNDLED_AUTHORITY_SOURCES,'tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs','tools/ymcc_sim/global/ui-navigation/toolchain-inputs.json'];
/** Resolve UI package files only through the already validated original Node binding. */
export function resolveUiToolContext(root,authority){
 const target=fs.realpathSync(root),manifestPath=sourcePath(target,'tools/ymcc_sim/global/ui-navigation/toolchain-inputs.json'),snapshotManifest=path.join(target,'.ymcc-source-snapshot/manifest.json');
 need(key(authority?.repoRoot??'')===key(target),'UI_CONTEXT_REPO_AUTHORITY');
 if(!fs.existsSync(snapshotManifest)){need(authority.mode==='primary-captured-graph','UI_CONTEXT_PRIMARY_AUTHORITY');return {repoRoot:target,manifestPath};}
 need(authority.mode==='snapshot-binding','UI_CONTEXT_SNAPSHOT_AUTHORITY');
 const proof=authority.proof,file=path.join(target,'.ymcc-source-snapshot/node-tools-binding.json');
 need(proof&&key(proof.bindingManifestFile??'')===key(file)&&/^[a-f0-9]{64}$/.test(proof.expectedBindingSha256??'')&&hash(file)===proof.expectedBindingSha256,'UI_CONTEXT_BINDING_IDENTITY');
 const saved=JSON.parse(fs.readFileSync(file,'utf8')),manifest=JSON.parse(fs.readFileSync(snapshotManifest,'utf8'));
 need(saved.status==='PASS'&&key(saved.snapshotRoot)===key(target)&&key(saved.bindingManifestFile)===key(file)&&hash(snapshotManifest)===saved.expectedSnapshotManifestSha256,'UI_CONTEXT_CAPTURE_IDENTITY');
 need(manifest.kind==='ymcc-fixed-source-snapshot'&&key(manifest.snapshotRoot)===key(target)&&key(saved.sourceRoot)===key(manifest.sourceRoot)&&key(saved.sourceRoot)===key(saved.graph.primaryRepo),'UI_CONTEXT_ORIGINAL_TOOL_ROOT');
 const member=manifest.files.find(row=>row.path==='tools/ymcc_sim/global/ui-navigation/toolchain-inputs.json');need(member&&hash(manifestPath)===member.sha256,'UI_CONTEXT_CAPTURED_UI_MANIFEST');
 return {repoRoot:fs.realpathSync(saved.sourceRoot),manifestPath};
}
export function lockTools(root){const authority=lockBundledNodeTools(root),uiContext=resolveUiToolContext(root,authority),ui=loadUiToolDeclaration(uiContext);const uiProof=verifyUiToolDeclaration(ui),all=new Map();for(const row of [...authority.tools,...ui.files]){const p=fs.realpathSync(row.path),prev=all.get(key(p));need(!prev||prev.sha256===row.sha256,'TOOL_PIN_COLLISION');all.set(key(p),{path:p,sha256:row.sha256,status:'OK'});}need(all.size<=3072,'TOOL_BUDGET');return {authority,uiContext,ui,uiProof,artifacts:[...all.values()],byReal:all};}
export function verifyTools(root,t){verifyBundledToolScope(t.authority,root);const proof=verifyUiToolDeclaration(t.ui);need(proof.manifestSha256===t.uiProof.manifestSha256,'UI_MANIFEST_CHANGED');for(const r of t.artifacts)need(hash(r.path)===r.sha256,'TOOL_CHANGED:'+r.path);return proof;}
export function classify(root,p,bytes,t){const real=fs.realpathSync(p),pin=t.byReal.get(key(real));if(pin){need(hash(real)===pin.sha256&&sha(bytes)===pin.sha256,'OBSERVED_TOOL_CHANGED');return {role:'tool',path:real,sha256:pin.sha256};}const c=classifyBundledInput(root,p,bytes,t.authority);need(c.role==='source','TOOL_NOT_DECLARED');return {...c.row,role:'source'};}
export function loadedTools(t){return Object.keys(createRequire(import.meta.url).cache).filter(p=>t.byReal.has(key(p))).map(p=>({path:p,sha256:hash(p),observation:'require.cache'}));}
export function assertOwned(out,p){need(inside(out,p),'OUTPUT_OUTSIDE_OWN_CASE');return p;}
