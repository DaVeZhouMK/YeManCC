import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {REPO,MODULE,CASES,moduleSources,toolchainFiles} from './adapter.mjs';
export const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export const bytesHash=b=>crypto.createHash('sha256').update(b).digest('hex');
export const readJson=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
export const writeJson=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
export function need(value,code){if(!value)throw Error(code);}
export function inside(root,p){const rel=path.relative(path.resolve(root),path.resolve(p));return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));}
export function evidencePath(value){
 need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'INPUTHOST_ABSOLUTE_EVIDENCE_REQUIRED');
 const root=path.resolve(value);
 need(!inside(REPO,root)&&!inside(root,REPO),'INPUTHOST_EVIDENCE_REPO_BOUNDARY');
 // A fresh evidence directory may be on any explicit local non-system volume.
 // Keep the original installed/system-drive exclusion; do not accept UNC or device paths.
 need(/^[a-z]:[\\/]/i.test(value),'INPUTHOST_LOCAL_VOLUME_EVIDENCE_REQUIRED');
 const systemVolume=(process.env.SystemDrive||path.parse(process.env.SystemRoot||'C:\\Windows').root).replace(/[\\/]+$/,'').toLowerCase();
 need(path.parse(root).root.replace(/[\\/]+$/,'').toLowerCase()!==systemVolume,'INPUTHOST_SYSTEM_VOLUME_EVIDENCE_REFUSED');
 for(let current=root;;){
  let info;
  try{info=fs.lstatSync(current);}catch(error){need(error.code==='ENOENT','INPUTHOST_EVIDENCE_PATH_UNREADABLE');}
  if(info){need(!info.isSymbolicLink(),'INPUTHOST_REPARSE_EVIDENCE');need(info.isDirectory(),'INPUTHOST_EVIDENCE_ANCESTOR_NOT_DIRECTORY');}
  const parent=path.dirname(current);if(parent===current)break;current=parent;
 }
 need(!fs.existsSync(root),'INPUTHOST_FRESH_EVIDENCE_REQUIRED');return root;
}
export function inputsFor(id){need(CASES[id],'INPUTHOST_CASE_UNKNOWN');return{sources:[...new Set([...moduleSources(REPO,id),...CASES[id].sources])].sort(),artifacts:toolchainFiles()};}
export function captureInputs(id,root){const inputs=inputsFor(id);const sourceRows=inputs.sources.map(relative=>{const p=path.join(REPO,relative),bytes=fs.readFileSync(p),saved=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes,{flag:'wx'});return{path:relative,sha256:bytesHash(bytes),saved};});const artifactDir=path.join(root,'input-artifacts');fs.mkdirSync(artifactDir);const artifactRows=inputs.artifacts.map(p=>{need(fs.lstatSync(p).isFile(),'INPUTHOST_ARTIFACT_NOT_FILE');const sha256=hash(p),saved=path.join(artifactDir,sha256+'-'+path.basename(p));if(!fs.existsSync(saved))fs.copyFileSync(p,saved,fs.constants.COPYFILE_EXCL);need(hash(saved)===sha256,'INPUTHOST_ARTIFACT_COPY_CHANGED');return{path:p,sha256,saved};});const result={schemaVersion:1,caseId:id,createdAt:new Date().toISOString(),sources:sourceRows,artifacts:artifactRows,artifactMeaning:'Declared compiler/framework allowlist; evaluated-input ledgers identify actually consumed Compile/Reference/Analyzer rows.'};writeJson(path.join(root,'input-snapshot.json'),result);return result;}
export function verifyInputs(id,root,before){const current=inputsFor(id);need(JSON.stringify(current.sources)===JSON.stringify(before.sources.map(r=>r.path))&&JSON.stringify(current.artifacts)===JSON.stringify(before.artifacts.map(r=>r.path)),'INPUTHOST_INPUT_MEMBERSHIP_CHANGED');for(const row of before.sources){need(hash(path.join(REPO,row.path))===row.sha256,'INPUTHOST_SOURCE_CHANGED:'+row.path);need(hash(row.saved)===row.sha256,'INPUTHOST_SOURCE_SNAPSHOT_CHANGED:'+row.path);}for(const row of before.artifacts){need(hash(row.path)===row.sha256,'INPUTHOST_TOOL_CHANGED:'+row.path);need(hash(row.saved)===row.sha256,'INPUTHOST_TOOL_SNAPSHOT_CHANGED:'+row.path);}return true;}
export function walkFiles(root){const files=[];function visit(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name);need(!fs.lstatSync(p).isSymbolicLink(),'INPUTHOST_REPARSE_OUTPUT');if(e.isDirectory())visit(p);else{need(e.isFile(),'INPUTHOST_OUTPUT_NOT_FILE');files.push(p);need(files.length<=20000,'INPUTHOST_OUTPUT_LIMIT');}}}visit(root);return files.sort();}
export function recordEvaluatedInputs(root,before){const external=new Map(before.artifacts.map(r=>[path.resolve(r.path).toLowerCase(),r]));const ledgers=walkFiles(root).filter(p=>/evaluated-inputs-(Release|Debug)\.txt$/.test(p));need(ledgers.length>=2,'INPUTHOST_EVALUATED_INPUT_LEDGER_MISSING');const rows=[];for(const ledger of ledgers){for(const line of fs.readFileSync(ledger,'utf8').replace(/^\uFEFF/,'').split(/\r?\n/)){if(!line)continue;const [kind,...parts]=line.split('\t');const p=parts.join('\t');if(!p||!fs.existsSync(p))continue;const absolute=path.resolve(p);if(!inside(root,absolute)){const declared=external.get(absolute.toLowerCase());need(declared,'INPUTHOST_UNDECLARED_COMPILER_INPUT:'+absolute);need(hash(absolute)===declared.sha256,'INPUTHOST_COMPILER_INPUT_CHANGED:'+absolute);}rows.push({ledger:path.relative(root,ledger).replaceAll('\\','/'),kind,path:absolute,sha256:hash(absolute)});}}need(rows.some(r=>r.kind==='Compile')&&rows.some(r=>r.kind==='Reference'),'INPUTHOST_ACTUAL_COMPILE_REFERENCE_SET_MISSING');writeJson(path.join(root,'evaluated-compiler-inputs.json'),{schemaVersion:1,ledgers:ledgers.length,rows});return{ledgers:ledgers.length,compileRows:rows.filter(r=>r.kind==='Compile').length,referenceRows:rows.filter(r=>r.kind==='Reference').length};}
export function seal(root,id,before,result){verifyInputs(id,root,before);const own=walkFiles(root);const sourceRows=before.sources.map(r=>({path:r.path,sha256:r.sha256}));const rows=[...sourceRows,...own.map(p=>({path:p,sha256:hash(p)}))];const sealed={...result,schemaVersion:1,caseId:id,sourceHashes:rows,sourceMembershipStable:true,toolMembershipStable:true,hardwareIo:false,productEntryExecuted:false,sdkInputAssembliesLoaded:false,performanceMeasurementExecuted:false};writeJson(path.join(root,'result.json'),sealed);return sealed;}
export const buildTargets=`<Project><Target Name="RecordInputHostFixtureInputs" AfterTargets="CoreCompile"><WriteLinesToFile File="$(MSBuildProjectDirectory)/evaluated-inputs-$(Configuration).txt" Lines="@(Compile-&gt;'Compile&#x9;%(FullPath)');@(ReferencePath-&gt;'Reference&#x9;%(FullPath)');@(Analyzer-&gt;'Analyzer&#x9;%(FullPath)');Project&#x9;$(MSBuildProjectFullPath);Import&#x9;$(DirectoryBuildPropsPath);Import&#x9;$(DirectoryBuildTargetsPath)" Overwrite="true" Encoding="UTF-8" /></Target></Project>`;
