import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {lockBundledNodeTools,BUNDLED_AUTHORITY_SOURCES,verifyBundledImplementation,verifyBundledToolScope} from '../bundled-node-tool-authority.mjs';
import {loadUiToolDeclaration,verifyUiToolDeclaration} from '../ui-navigation/tool-inputs.mjs';
import {resolveUiToolContext} from '../controller-device-diagram-consumer/contract.mjs';
import {sourcePath} from '../runner.mjs';
import {verifyCapturedSourceSnapshot} from '../source-snapshot.mjs';

export const ID='performance.simulation-draft-consumer';
export const MODULE='tools/ymcc_sim/global/performance-simulation-consumer';
export const TOOL_INPUT_LIMIT=3072;
export const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
export const hash=file=>sha(fs.readFileSync(file));
export const need=(value,message)=>{if(!value)throw Error(message)};
export const write=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n')};
export const inside=(root,file)=>{const relative=path.relative(path.resolve(root),path.resolve(file));return relative===''||(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep))};
const key=file=>path.resolve(file).toLowerCase();
const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export function sourceDeclaration(){const declaration=readJson(path.join(import.meta.dirname,'source-inputs.json'));need(declaration.schemaVersion===1&&Array.isArray(declaration.sources)&&declaration.sources.length>0&&new Set(declaration.sources).size===declaration.sources.length,'SOURCE_DECLARATION_INVALID');return declaration}

/**
 * Synchronous descriptor authority. This mirrors the existing gyro lifecycle
 * contract's lockTools shape and uses the shared standard authority modules.
 * Full captured-source verification remains in async lockContext below.
 */
export function lockToolsSync(repoRoot,{env=process.env,binding}={}){
 const root=fs.realpathSync(repoRoot);verifyBundledImplementation(root);const captured=fs.existsSync(path.join(root,'.ymcc-source-snapshot/manifest.json'))||fs.existsSync(path.join(root,'.ymcc-source-snapshot/node-tools-binding.json'));const explicit=captured?(binding??{bindingManifestFile:env.YMCC_BATTERY_UI_NODE_TOOLS_BINDING,expectedBindingSha256:env.YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256}):binding;if(captured)need(path.isAbsolute(explicit?.bindingManifestFile??'')&&/^[a-f0-9]{64}$/.test(explicit?.expectedBindingSha256??''),'EXPLICIT_SNAPSHOT_BINDING_AND_EXPECTED_SHA_REQUIRED');
 const authority=lockBundledNodeTools(root,{env,binding:explicit,limit:TOOL_INPUT_LIMIT});
 const uiContext=resolveUiToolContext(root,authority),ui=loadUiToolDeclaration(uiContext),uiProof=verifyUiToolDeclaration(ui),all=new Map();
 for(const row of [...authority.tools,...ui.files]){const real=fs.realpathSync(row.path),previous=all.get(key(real));need(!previous||previous.sha256===row.sha256,'TOOL_PIN_CONFLICT');all.set(key(real),{path:real,sha256:row.sha256,status:'OK'})}
 need(all.size<=TOOL_INPUT_LIMIT,'TOOL_BUDGET_EXCEEDED');
 need(key(fs.realpathSync(process.execPath))===key(fs.realpathSync(ui.executables.node))&&hash(process.execPath)===ui.manifest.executables.node.sha256,'NODE_TOOL_NOT_PINNED');
 return {authority,uiContext,ui,uiProof,artifacts:[...all.values()],byReal:all,bindingFile:authority.mode==='snapshot-binding'?authority.proof.bindingManifestFile:null,bindingSha256:authority.mode==='snapshot-binding'?authority.proof.expectedBindingSha256:null};
}

/** The caller supplies snapshot binding identity; full source verification is async. */
export async function lockContext(repoRoot,{env=process.env,binding}={}){
 const root=fs.realpathSync(repoRoot),tools=lockToolsSync(root,{env,binding}),declaration=sourceDeclaration(),authority=tools.authority;
 const selected=declaration.sources.map(relative=>({path:relative,sha256:hash(sourcePath(root,relative))}));
 if(inside(root,import.meta.dirname))for(const name of declaration.moduleFiles)selected.push({path:MODULE+'/'+name,sha256:hash(sourcePath(root,MODULE+'/'+name))});
 let manifestFile=null,bindingFile=tools.bindingFile,manifest=null,sourceByPath=new Map(selected.map(row=>[row.path,row])),captureVerification=null,verifyCaptured=null,sourceAuthority;
 if(authority.mode==='snapshot-binding'){
  bindingFile=authority.proof.bindingManifestFile;const saved=readJson(bindingFile);manifestFile=saved.snapshotManifestFile;manifest=readJson(manifestFile);
  need(hash(bindingFile)===authority.proof.expectedBindingSha256&&hash(manifestFile)===saved.expectedSnapshotManifestSha256,'CAPTURE_AUTHORITY_CHANGED');
  need(manifest.kind==='ymcc-fixed-source-snapshot'&&key(manifest.snapshotRoot)===key(root)&&Array.isArray(manifest.files)&&new Set(manifest.files.map(row=>row.path)).size===manifest.files.length,'CAPTURE_MANIFEST_SCHEMA');
  verifyCaptured=()=>verifyCapturedSourceSnapshot({manifestFile,repoRoot:saved.sourceRoot,expectedManifestSha256:saved.expectedSnapshotManifestSha256});
  captureVerification=verifyCaptured();need(captureVerification.status==='FROZEN_SOURCE_VERIFIED'&&captureVerification.snapshotIntegrityVerified===true&&captureVerification.fileCount===manifest.files.length,'COMPLETE_CAPTURE_VERIFICATION_REQUIRED');
  sourceByPath=new Map(manifest.files.map(row=>[row.path,row]));
  for(const row of selected)need(sourceByPath.get(row.path)?.sha256===row.sha256,'SELECTED_SOURCE_NOT_IN_CAPTURE:'+row.path);
  sourceAuthority={kind:'explicit-complete-captured-source-manifest',mode:authority.mode,sourceRoot:root,manifestFile,manifestSha256:hash(manifestFile),bindingFile,bindingSha256:hash(bindingFile),fullCaptureFiles:manifest.files.length,creationStatus:captureVerification.creationStatus,primaryCurrent:captureVerification.isCurrent,captureVerification,sourceApprovalGranted:false};
 }else{
  need(authority.mode==='primary-captured-graph','UNKNOWN_TOOL_AUTHORITY_MODE');
  sourceAuthority={kind:'exact-selected-primary-source-before-after',mode:authority.mode,sourceRoot:root,manifestFile:null,manifestSha256:null,bindingFile:null,bindingSha256:null,fullCaptureFiles:null,creationStatus:null,primaryCurrent:null,selectedSourceCount:selected.length,selectedSourcesBefore:selected,sourceApprovalGranted:false};
 }
 const TOOL_SOURCES=[...BUNDLED_AUTHORITY_SOURCES,'tools/ymcc_sim/global/controller-device-diagram-consumer/contract.mjs','tools/ymcc_sim/global/ui-navigation/tool-inputs.mjs','tools/ymcc_sim/global/ui-navigation/toolchain-inputs.json'];
 const verifyTools=(target,value)=>{verifyBundledImplementation(target);verifyBundledToolScope(value.authority,target,{limit:TOOL_INPUT_LIMIT});need(hash(value.ui.manifestPath)===value.ui.manifestSha256,'UI_MANIFEST_CHANGED');verifyUiToolDeclaration(value.ui);for(const row of value.artifacts)need(hash(row.path)===row.sha256,'TOOL_CHANGED:'+row.path)};
 return {root,tools,manifest,manifestFile,bindingFile,sourceByPath,selectedBefore:selected,sourceAuthority,captureVerification,verifyCaptured,sourcePath,c:{TOOL_SOURCES,verifyTools},implementation:tools.authority};
}
export function verifyContext(context,observed=[]){
 context.c.verifyTools(context.root,context.tools);
 for(const row of context.selectedBefore)need(hash(context.sourcePath(context.root,row.path))===row.sha256,'SELECTED_SOURCE_CHANGED:'+row.path);
 for(const row of observed)if(row.role==='source')need(hash(context.sourcePath(context.root,row.path))===row.sha256,'ACTUAL_SOURCE_CHANGED:'+row.path);
 if(context.verifyCaptured){need(hash(context.manifestFile)===context.sourceAuthority.manifestSha256&&hash(context.bindingFile)===context.sourceAuthority.bindingSha256,'CAPTURE_AUTHORITY_CHANGED');context.captureVerificationAfter=context.verifyCaptured();need(context.captureVerificationAfter.status==='FROZEN_SOURCE_VERIFIED'&&context.captureVerificationAfter.fileCount===context.sourceAuthority.fullCaptureFiles,'COMPLETE_CAPTURE_CHANGED')}
 context.sourceAuthorityAfter={...context.sourceAuthority,...(context.captureVerificationAfter?{primaryCurrent:context.captureVerificationAfter.isCurrent,captureVerification:context.captureVerificationAfter}:{selectedSourcesAfter:context.selectedBefore.map(row=>({...row,sha256:hash(context.sourcePath(context.root,row.path))}))})};return context.sourceAuthorityAfter;
}
export function observeSource(context,file,bytes){const relative=path.relative(context.root,file).replaceAll('\\','/');need(inside(context.root,file),'SOURCE_OUTSIDE_REPOSITORY');const actual=context.sourcePath(context.root,relative),row=context.sourceByPath.get(relative);need(row&&/^[a-f0-9]{64}$/.test(row.sha256)&&row.sha256===sha(bytes),'SOURCE_CHANGED_OR_NOT_DECLARED:'+relative);need(key(actual)===key(fs.realpathSync(file)),'SOURCE_PATH_FORWARDING');return {...row,role:'source',observation:'actual-read'}}
export function collectFiles(root){const result=[];for(const entry of fs.readdirSync(root,{withFileTypes:true})){const file=path.join(root,entry.name);need(!entry.isSymbolicLink(),'OUTPUT_REPARSE_NOT_ALLOWED:'+file);if(entry.isDirectory())result.push(...collectFiles(file));else if(entry.isFile())result.push(file)}return result}

