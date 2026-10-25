import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {resolveSnapshotNodeTools} from '../../../fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs';
const moduleRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const pinFile=fileURLToPath(new URL('./template-artifact-pin.json',import.meta.url));
const approved=Object.freeze({"schemaVersion":1,"kind":"ymcc-monitor-static-release-template","relative":"PowerControl/RTSS-Overlays/YeManOBS-W-1.ovl","sha256":"b8558bc179c7f4383512c4ce86c266df0e9b117f9917311db85fe98999d88a6f","bytes":23703,"role":"static-release-overlay-template"});
const need=(value,code)=>{if(!value)throw Error(code);};
const key=file=>path.resolve(file).toLowerCase();
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const inside=(root,file)=>{const relative=path.relative(root,file);return relative!==''&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
export function validateTemplatePin(pin){need(pin&&Object.keys(pin).sort().join(',')===Object.keys(approved).sort().join(','),'TEMPLATE_ARTIFACT_PIN_SCHEMA');for(const [name,value]of Object.entries(approved))need(pin[name]===value,'TEMPLATE_ARTIFACT_PREAPPROVED_PIN_MISMATCH');return Object.freeze({...pin});}
function exactFile(root,relative){
 need(typeof root==='string'&&path.isAbsolute(root),'TEMPLATE_ARTIFACT_ABSOLUTE_REPO_REQUIRED');
 need(typeof relative==='string'&&relative&&!path.isAbsolute(relative)&&!/[\\:\0\r\n]/.test(relative)&&!relative.split('/').some(part=>!part||part==='.'||part==='..'),'TEMPLATE_ARTIFACT_RELATIVE_PATH_REQUIRED');
 root=path.resolve(root);const file=path.resolve(root,relative);need(inside(root,file),'TEMPLATE_ARTIFACT_PATH_ESCAPE');
 let cursor=root;for(const part of ['',...relative.split('/')]){if(part)cursor=path.join(cursor,part);need(fs.existsSync(cursor),'TEMPLATE_ARTIFACT_MISSING');need(!fs.lstatSync(cursor).isSymbolicLink(),'TEMPLATE_ARTIFACT_REPARSE_REJECTED');}
 need(key(fs.realpathSync(root))===key(root)&&key(fs.realpathSync(file))===key(file),'TEMPLATE_ARTIFACT_REAL_PATH_CHANGED');need(fs.statSync(file).isFile(),'TEMPLATE_ARTIFACT_NOT_FILE');return file;
}
function identity(file){const stat=fs.statSync(file),bytes=fs.readFileSync(file);return {path:file,sha256:digest(bytes),bytes:bytes.length,device:String(stat.dev),inode:String(stat.ino),mtimeMs:stat.mtimeMs,ctimeMs:stat.ctimeMs};}
function checkPinned(file,pin){const actual=identity(file);need(actual.sha256===pin.sha256&&actual.bytes===pin.bytes,'TEMPLATE_ARTIFACT_BYTES_CHANGED');return actual;}
export function resolveTemplateArtifact(repo=moduleRoot,bindingOptions={}){
 const pin=validateTemplatePin(readJson(pinFile));need(typeof repo==='string'&&path.isAbsolute(repo),'TEMPLATE_ARTIFACT_ABSOLUTE_REPO_REQUIRED');repo=path.resolve(repo);
 const metadataRoot=path.join(repo,'.ymcc-source-snapshot');let authority=null,sourceRoot=repo,kind='PRIMARY_EXACT_STATIC_RELEASE_PATH',metadata=[];
 if(fs.existsSync(metadataRoot)){
  authority=resolveSnapshotNodeTools(repo,bindingOptions);need(authority,'TEMPLATE_ARTIFACT_SNAPSHOT_AUTHORITY_REQUIRED');
  sourceRoot=path.resolve(authority.packageRepo);kind='VERIFIED_SNAPSHOT_BINDING_SOURCE_ROOT';
  metadata=authority.metadata.filter(row=>['manifest.json','node-tools-binding.json'].includes(path.basename(row.path))).map(row=>({path:row.path,sha256:row.sha256,role:'verified snapshot artifact-origin authority; not source approval'}));
 }
 const file=exactFile(sourceRoot,pin.relative),artifact=checkPinned(file,pin);
 const options=authority?{bindingManifestFile:authority.proof.bindingManifestFile,expectedBindingSha256:authority.proof.expectedBindingSha256}:{};
 return {schemaVersion:1,kind:'ymcc-monitor-template-artifact',authorityRoot:repo,sourceRoot,originKind:kind,relative:pin.relative,role:pin.role,artifact,pin:{path:pinFile,sha256:digest(fs.readFileSync(pinFile))},metadata,bindingOptions:options,nodeToolAuthority:authority?.proof??null,sourceApprovalGranted:false};
}
export function verifyTemplateArtifact(expected){
 need(expected?.kind==='ymcc-monitor-template-artifact','TEMPLATE_ARTIFACT_DESCRIPTOR_REQUIRED');
 const actual=resolveTemplateArtifact(expected.authorityRoot,expected.bindingOptions);
 need(JSON.stringify(actual)===JSON.stringify(expected),'TEMPLATE_ARTIFACT_ORIGIN_CHANGED');return actual;
}
export function templateArtifactInputs(artifact){verifyTemplateArtifact(artifact);return [artifact.artifact.path,...artifact.metadata.map(row=>row.path)];}
export function copyTemplateArtifact(artifact,caseDir){
 verifyTemplateArtifact(artifact);need(path.isAbsolute(caseDir)&&fs.existsSync(caseDir),'TEMPLATE_ARTIFACT_OWNED_CASE_REQUIRED');
 need(key(fs.realpathSync(caseDir))===key(caseDir)&&!fs.lstatSync(caseDir).isSymbolicLink(),'TEMPLATE_ARTIFACT_OWNED_CASE_REPARSE');
 const directory=path.join(caseDir,'owned-template');fs.mkdirSync(directory,{recursive:true});need(!fs.lstatSync(directory).isSymbolicLink(),'TEMPLATE_ARTIFACT_OWNED_CASE_REPARSE');
 const owned=path.join(directory,'YeManOBS-W-1.ovl');need(inside(caseDir,owned),'TEMPLATE_ARTIFACT_OWNED_PATH_ESCAPE');
 const bytes=fs.readFileSync(artifact.artifact.path);need(digest(bytes)===approved.sha256,'TEMPLATE_ARTIFACT_COPY_INPUT_CHANGED');fs.writeFileSync(owned,bytes,{flag:'wx'});
 const copy=checkPinned(exactFile(caseDir,'owned-template/YeManOBS-W-1.ovl'),approved);verifyTemplateArtifact(artifact);
 return {schemaVersion:1,status:'PREPARED',artifact,ownedCopy:copy,artifactIsSource:false,copyRole:'owned exact-byte execution input; absolute case-local source hash is allowed',originalApprovedHash:approved.sha256};
}
export function verifyTemplateCopy(receipt){
 verifyTemplateArtifact(receipt.artifact);const caseDir=path.dirname(path.dirname(receipt.ownedCopy.path));const file=exactFile(caseDir,'owned-template/YeManOBS-W-1.ovl');
 const actual=checkPinned(file,approved);need(JSON.stringify(actual)===JSON.stringify(receipt.ownedCopy),'TEMPLATE_ARTIFACT_OWNED_COPY_CHANGED');return {...receipt,status:'VERIFIED_AFTER_EXECUTION'};
}
