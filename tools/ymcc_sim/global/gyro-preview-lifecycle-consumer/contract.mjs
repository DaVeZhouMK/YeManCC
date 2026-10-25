import fs from 'node:fs';import path from 'node:path';import{createHash}from'node:crypto';
import{sourcePath}from'../runner.mjs';
import{lockBundledNodeTools,verifyBundledToolScope,verifyBundledImplementation,BUNDLED_AUTHORITY_SOURCES}from'../bundled-node-tool-authority.mjs';
import{loadUiToolDeclaration,verifyUiToolDeclaration}from'../ui-navigation/tool-inputs.mjs';
import{resolveUiToolContext,classify as classifyFixed}from'../controller-device-diagram-consumer/contract.mjs';
export const MODULE='tools/ymcc_sim/global/gyro-preview-lifecycle-consumer',CASE_ID='gyro.preview-lifecycle-consumer';
export const MODULE_FILES=['adapter.mjs','contract.mjs','build.mjs','browser-entry.ts','ports.ts','browser-cases.mjs','run.mjs','spec.json','README.md','current-source-contract.json'];
export const sha=b=>createHash('sha256').update(b).digest('hex'),hash=p=>sha(fs.readFileSync(p)),key=p=>path.resolve(p).toLowerCase();
export const need=(v,s)=>{if(!v)throw Error(s);},write=(p,v)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');};
export const TOOL_SOURCES=[...BUNDLED_AUTHORITY_SOURCES,'tools/ymcc_sim/global/controller-device-diagram-consumer/contract.mjs','tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs','tools/ymcc_sim/global/ui-navigation/toolchain-inputs.json'];
export function specAt(root){
 const baselineFile=sourcePath(root,MODULE+'/spec.json'),baseline=JSON.parse(fs.readFileSync(baselineFile,'utf8')),currentFile=sourcePath(root,MODULE+'/current-source-contract.json');
 const current=JSON.parse(fs.readFileSync(currentFile,'utf8'));
 need(current.schemaVersion===1&&current.kind==='reviewed-current-gyro-preview-source-contract'&&current.baselineSpecSha256===hash(baselineFile),'CURRENT_REVIEW_BASELINE_MISMATCH');
 return {...baseline,sources:current.sources,authoritySources:current.authoritySources,moduleFiles:current.moduleFiles,sourceReview:{path:MODULE+'/current-source-contract.json',sha256:hash(currentFile),baselineSpecSha256:current.baselineSpecSha256,removedHistoricalSource:current.removedHistoricalSource,replacement:current.replacement,lifecycleReview:current.lifecycleReview}};
}
export function sourcePins(root){
 const target=fs.realpathSync(root),baseline=specAt(target),manifestFile=path.join(target,'.ymcc-source-snapshot/manifest.json');
 need(JSON.stringify(baseline.moduleFiles)===JSON.stringify(MODULE_FILES),'MODULE_FILE_CONTRACT_CHANGED');
 need(Array.isArray(baseline.sources)&&baseline.sources.length>0&&JSON.stringify(baseline.authoritySources?.map(r=>r.path))===JSON.stringify(TOOL_SOURCES),'SOURCE_OR_AUTHORITY_SCHEMA');
 const baselineReference={fixedRoot:baseline.fixedRoot,manifestSha256:baseline.manifestSha256,bindingSha256:baseline.bindingSha256,specSha256:baseline.sourceReview.baselineSpecSha256,referenceOnly:true};
 let spec={...baseline,fixedRoot:target,manifestFile:null,manifestSha256:null,bindingManifestFile:null,bindingSha256:null,sourceFileCount:null,sourceAuthority:{kind:'exact-selected-source-baseline',actualSourceRoot:target,baselineReference,reviewedCurrentContract:baseline.sourceReview,sourceApprovalGranted:false}};
 if(fs.existsSync(manifestFile)){
  verifyBundledImplementation(target);const authority=lockBundledNodeTools(target);need(authority.mode==='snapshot-binding','EXPLICIT_CAPTURED_SOURCE_BINDING_REQUIRED');
  const bindingFile=authority.proof.bindingManifestFile,binding=JSON.parse(fs.readFileSync(bindingFile,'utf8'));
  need(hash(bindingFile)===authority.proof.expectedBindingSha256&&hash(manifestFile)===binding.expectedSnapshotManifestSha256,'CAPTURED_SOURCE_AUTHORITY_CHANGED');
  const manifest=JSON.parse(fs.readFileSync(manifestFile,'utf8'));need(manifest.kind==='ymcc-fixed-source-snapshot'&&key(manifest.snapshotRoot)===key(target)&&Array.isArray(manifest.files),'CAPTURED_SOURCE_ROOT_MISMATCH');
  const resolve=rows=>rows.map(row=>{const fixed=manifest.files.find(x=>x.path===row.path);need(fixed&&/^[a-f0-9]{64}$/.test(fixed.sha256),'CAPTURED_SOURCE_MEMBER_REQUIRED:'+row.path);return {path:row.path,sha256:fixed.sha256};});
  spec={...spec,sources:resolve(baseline.sources),authoritySources:resolve(baseline.authoritySources),manifestFile,manifestSha256:hash(manifestFile),bindingManifestFile:bindingFile,bindingSha256:hash(bindingFile),sourceFileCount:manifest.files.length,sourceAuthority:{kind:'explicit-complete-captured-source-manifest',actualSourceRoot:target,manifestFile,manifestSha256:hash(manifestFile),bindingFile,bindingSha256:hash(bindingFile),fullSourceFiles:manifest.files.length,creationStatus:manifest.status,baselineReference,sourceApprovalGranted:false}};
 }
 for(const r of [...spec.sources,...spec.authoritySources])need(/^[a-f0-9]{64}$/.test(r.sha256)&&hash(sourcePath(target,r.path))===r.sha256,'SELECTED_SOURCE_CHANGED:'+r.path);
 return spec;
}
export function lockTools(root){
 verifyBundledImplementation(root);const authority=lockBundledNodeTools(root),uiContext=resolveUiToolContext(root,authority),ui=loadUiToolDeclaration(uiContext),uiProof=verifyUiToolDeclaration(ui),all=new Map();
 for(const r of [...authority.tools,...ui.files]){const p=fs.realpathSync(r.path),old=all.get(key(p));need(!old||old.sha256===r.sha256,'TOOL_PIN_CONFLICT');all.set(key(p),{path:p,sha256:r.sha256,status:'OK'});}
 need(all.size<=3072,'TOOL_BUDGET_EXCEEDED');return{authority,uiContext,ui,uiProof,artifacts:[...all.values()],byReal:all};
}
export function verifyTools(root,t){verifyBundledImplementation(root);verifyBundledToolScope(t.authority,root);verifyUiToolDeclaration(t.ui);for(const r of t.artifacts)need(hash(r.path)===r.sha256,'TOOL_CHANGED:'+r.path);}
export const classify=(root,p,b,t)=>classifyFixed(root,p,b,t);
