import fs from 'node:fs';
import path from 'node:path';
import {createHash,randomUUID} from 'node:crypto';

export const SNAPSHOT_SCHEMA=1;
export const SNAPSHOT_METADATA='.ymcc-source-snapshot';
export const SNAPSHOT_MANIFEST=SNAPSHOT_METADATA+'/manifest.json';
const ROOT_RUNTIME_EXCLUSIONS=Object.freeze([
 Object.freeze({name:'_scratch',id:'root-scratch',reason:'Root _scratch holds temporary development/runtime products rather than fixed source inputs.'}),
 Object.freeze({name:'work',id:'root-work',reason:'Root work holds run evidence and working products rather than fixed source inputs.'}),
 Object.freeze({name:'System.Management.Automation.Internal.Host.InternalHost',id:'root-powershell-host',reason:'This root is a generated PowerShell host/output artifact, outside the fixed source scope.'}),
]);
export const SNAPSHOT_POLICY=Object.freeze({
 version:6,
 rules:Object.freeze([
  Object.freeze({id:'git-metadata',match:'Any entry named .git; never read or descend.',reason:'Version-control metadata is outside the raw working-source identity.'}),
  Object.freeze({id:'node-dependencies',match:'Any directory named node_modules; never descend.',reason:'Locally installed Node products are outside this fixed-source scope; source dependency declarations are retained.'}),
  Object.freeze({id:'root-generated',match:'Root directories dist, Build, artifacts only; nested vendor/deps directories retained.',reason:'Root build/run products are outside the fixed-source scope.'}),
  ...ROOT_RUNTIME_EXCLUSIONS.map(({name,id,reason})=>Object.freeze({id,match:'Root directory '+name+' only; no descent or content reads.',reason})),
  Object.freeze({id:'powercontrol-nonsource',match:'Within mixed root PowerControl, preserve source-code/script/project files including .py, package.json and the exact retainedStaticSourceData/retainedSourceBuildRecipes paths; exclude other opaque runtime/data files without opening their bytes.',reason:'PowerControl contains first-party catalog sources and operational payload together. Only source/code manifests are fixed; runtime settings, leases, sessions, logs and binaries are not.'}),
  Object.freeze({id:'ide-cache',match:'Directories named .vs outside a deps subtree.',reason:'Local IDE cache is a development product.'}),
  Object.freeze({id:'dotnet-output',match:'Any non-deps bin or obj directory whose immediate parent lists a .csproj/.fsproj/.vbproj filename, including FanLab and other source projects.',reason:'.NET project build output is generated; project/source files and all deps originals are retained.'}),
  Object.freeze({id:'runtime-logs',match:'Non-deps Logs/logs directories under root FanLab, a directory named runtime, or a .NET project ancestor; never tools/fixtures. Match is by path/project filenames only.',reason:'Operational project logs are outside this source scope; controlled tools/fixtures log samples and deps originals are retained.'}),
  Object.freeze({id:'python-cache',match:'PowerControl __pycache__ directories and .pyc files, plus native/tools caches outside deps; never descend or read cache contents.',reason:'Python bytecode caches are generated development products.'}),
 ]),
 retainedStaticSourceData:Object.freeze([Object.freeze({path:'PowerControl/GameTitlePolicy.json',consumedBy:'src/bridge/gamedetect.ts',reason:'Direct TypeScript JSON import: immutable game-title suffix rules compiled with the source, not operational device state.'})]),
 retainedSourceBuildRecipes:Object.freeze([Object.freeze({path:'PowerControl/pawnio/YeManTdpCtl.spec',reason:'Actual Python-coded PyInstaller Analysis/PYZ/EXE/COLLECT recipe for YeManTdpCtl.py; source recipe only, referenced binary payload is not included.'})]),
 rootRuntimeDirectoryExclusions:ROOT_RUNTIME_EXCLUSIONS,
 broadGitignoreNotApplied:true,
 unmatchedSourcesFixturesAndRuntimeDependenciesIncluded:true,
 installationPayloadFrozen:false,
 excludedDirectoryContentsRead:false,
});
const EXCLUSION_REASONS=new Map(SNAPSHOT_POLICY.rules.map(rule=>[rule.id,rule.reason]));
const MAX_MANIFEST_BYTES=128*1024*1024;
const MAX_FILES=500000;
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const key=file=>process.platform==='win32'?path.resolve(file).toLowerCase():path.resolve(file);
const slash=value=>value.replaceAll(path.sep,'/');
const need=(ok,code,details)=>{if(!ok)throw Object.assign(Error(code),{code,details});};
const exists=file=>{try{fs.lstatSync(file);return true;}catch(error){if(error.code==='ENOENT')return false;throw error;}};
const inside=(root,file)=>{const rel=path.relative(path.resolve(root),path.resolve(file));return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
function absolute(value){need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'SNAPSHOT_ABSOLUTE_PATH_REQUIRED');return path.resolve(value);}
function noReparse(file){
 let cursor=path.resolve(file);
 for(;;){
  if(exists(cursor)){const st=fs.lstatSync(cursor);need(!st.isSymbolicLink(),'SNAPSHOT_REPARSE_PATH',{path:cursor});need(key(fs.realpathSync(cursor))===key(cursor),'SNAPSHOT_REPARSE_RESOLUTION',{path:cursor});}
  const parent=path.dirname(cursor);if(parent===cursor)break;cursor=parent;
 }
}
function directory(file){noReparse(file);need(fs.lstatSync(file).isDirectory(),'SNAPSHOT_DIRECTORY_REQUIRED',{path:file});return fs.realpathSync(file);}
function relative(value){
 need(typeof value==='string'&&value.length>0&&!value.includes('\\')&&!value.includes('\0')&&!path.posix.isAbsolute(value)&&!path.win32.isAbsolute(value)&&path.posix.normalize(value)===value&&!value.split('/').some(p=>p==='..'||p==='.'||!p),'SNAPSHOT_RELATIVE_PATH_INVALID',{path:value});return value;
}
function member(root,rel){relative(rel);const file=path.resolve(root,...rel.split('/'));need(inside(root,file)&&key(root)!==key(file),'SNAPSHOT_MEMBER_ESCAPE');return file;}
// Windows Node can report dev=0 from path stats and a volume id from fstat.
// File index, size and nanosecond times still bind the path to the descriptor.
const fileState=stat=>[...(process.platform==='win32'?[]:[stat.dev]),stat.ino,stat.size,stat.mtimeNs,stat.ctimeNs].map(String).join(':');
/** The descriptor's bytes, rather than a VCS version or selected functions, are hashed. */
function readHash(file){
 noReparse(file);const initial=fs.lstatSync(file,{bigint:true});need(initial.isFile(),'SNAPSHOT_REGULAR_FILE_REQUIRED',{path:file});
 const fd=fs.openSync(file,fs.constants.O_RDONLY|(fs.constants.O_NOFOLLOW??0));
 try{
  const before=fs.fstatSync(fd,{bigint:true});need(before.isFile()&&fileState(initial)===fileState(before),'SNAPSHOT_FILE_CHANGED_DURING_READ',{path:file});
  const hash=createHash('sha256'),buffer=Buffer.allocUnsafe(1024*1024);let size=0,read;
  while((read=fs.readSync(fd,buffer,0,buffer.length,null))>0){hash.update(buffer.subarray(0,read));size+=read;need(Number.isSafeInteger(size),'SNAPSHOT_FILE_SIZE_LIMIT');}
  const after=fs.fstatSync(fd,{bigint:true});noReparse(file);const current=fs.lstatSync(file,{bigint:true});
  need(fileState(before)===fileState(after)&&fileState(after)===fileState(current)&&BigInt(size)===after.size,'SNAPSHOT_FILE_CHANGED_DURING_READ',{path:file});
  return {size,sha256:hash.digest('hex')};
 }finally{fs.closeSync(fd);}
}
function exclusion(rel,isDirectory,parentHasProject,projectContext){
 const parts=rel.split('/'),lower=parts.map(p=>p.toLowerCase()),name=lower.at(-1),inDeps=lower.includes('deps'),nativeOrTools=['native','tools'].includes(lower[0]);
 if((lower[0]==='powercontrol'||(!inDeps&&nativeOrTools))&&((isDirectory&&name==='__pycache__')||(!isDirectory&&name.endsWith('.pyc'))))return 'python-cache';
 if(lower[0]==='powercontrol'&&!isDirectory&&name!=='package.json'&&lower.join('/')!=='powercontrol/gametitlepolicy.json'&&!SNAPSHOT_POLICY.retainedSourceBuildRecipes.some(row=>row.path.toLowerCase()===lower.join('/'))&&!/\.(?:py|ts|tsx|js|jsx|mjs|cjs|vue|html|h|hpp|hh|cpp|cc|c|cs|fs|vb|ps1|psm1|psd1|bat|cmd|vbs|csproj|fsproj|vbproj|sln|props|targets|resx|manifest)$/i.test(name))return 'powercontrol-nonsource';
 if(name==='.git')return 'git-metadata';
 if(isDirectory&&name==='node_modules')return 'node-dependencies';
 if(isDirectory&&parts.length===1&&['dist','build','artifacts'].includes(name))return 'root-generated';
 if(isDirectory&&parts.length===1){const root=ROOT_RUNTIME_EXCLUSIONS.find(row=>row.name.toLowerCase()===name);if(root)return root.id;}
 if(!inDeps&&isDirectory&&name==='.vs')return 'ide-cache';
 if(!inDeps&&isDirectory&&['bin','obj'].includes(name)&&parentHasProject)return 'dotnet-output';
 const controlledFixtures=lower[0]==='tools'&&lower[1]==='fixtures';
 if(!inDeps&&!controlledFixtures&&isDirectory&&name==='logs'&&(lower[0]==='fanlab'||lower.slice(0,-1).includes('runtime')||projectContext))return 'runtime-logs';
 return null;
}
/** Enumerate only included content. Excluded directories are recorded as roots,
 * not traversed; their bytes are never claimed as frozen or current. */
function inventory(root,{snapshot=false,captureRoot=null,onCaptured=()=>{}}={}){
 directory(root);const files=[],directories=[],excluded=[];
 function walk(dir,prefix,ancestorHasProject=false){
  noReparse(dir);const names=fs.readdirSync(dir).sort(),parentHasProject=names.some(n=>/\.(?:csproj|fsproj|vbproj)$/i.test(n)),projectContext=ancestorHasProject||parentHasProject;
  for(const name of names){
   const rel=prefix?prefix+'/'+name:name,file=path.join(dir,name);relative(rel);
   noReparse(file);const st=fs.lstatSync(file);need(!st.isSymbolicLink(),'SNAPSHOT_REPARSE_PATH',{path:rel});
   if(!prefix&&name.toLowerCase()===SNAPSHOT_METADATA){need(snapshot,'SNAPSHOT_SOURCE_METADATA_COLLISION',{path:rel});need(st.isDirectory(),'SNAPSHOT_METADATA_DIRECTORY_REQUIRED');continue;}
   const reason=exclusion(rel,st.isDirectory(),parentHasProject,projectContext);
   if(reason){excluded.push({path:rel,type:st.isDirectory()?'directory':'file',rule:reason,reason:EXCLUSION_REASONS.get(reason)});continue;}
   if(st.isDirectory()){directories.push(rel);walk(file,rel,projectContext);}
   else{need(st.isFile(),'SNAPSHOT_SPECIAL_FILE_REJECTED',{path:rel});need(files.length<MAX_FILES,'SNAPSHOT_FILE_COUNT_LIMIT');if(captureRoot)onCaptured({phase:'before-capture-file',path:rel});const identity=captureRoot?captureOneAtRead(root,captureRoot,rel):readHash(file);files.push({path:rel,...identity});if(captureRoot)onCaptured({phase:'after-capture-file',path:rel});}
  }
 }
 walk(root,'');
 // Keep mixed-runtime directories only when they carry included source or an existing catalog root.
 const declaredPowerRoots=['PowerControl/decky/plugins/ymcc-sidebar/src','PowerControl/MemoryScan'];
 const carriesSource=dir=>files.some(row=>row.path.startsWith(dir+'/'));
 const keptDirs=directories.filter(dir=>dir.toLowerCase()!=='powercontrol'&&!dir.toLowerCase().startsWith('powercontrol/')||carriesSource(dir)||declaredPowerRoots.some(root=>root===dir||root.startsWith(dir+'/')));
 directories.splice(0,directories.length,...keptDirs);
 files.sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);directories.sort();excluded.sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
 const main=files.find(row=>row.path==='native/main.cpp')??null;
 return {files,directories,excluded,main,fileCount:files.length,totalBytes:files.reduce((sum,row)=>sum+row.size,0)};
}
function diff(expected,actual){
 const wanted=new Map(expected.files.map(row=>[row.path,row])),added=[],changed=[];
 for(const row of actual.files){const prev=wanted.get(row.path);wanted.delete(row.path);if(!prev)added.push(row.path);else if(prev.size!==row.size||prev.sha256!==row.sha256)changed.push({path:row.path,expected:prev,actual:row});}
 const oldDirs=new Set(expected.directories),newDirs=new Set(actual.directories);
 const result={added,removed:[...wanted.keys()].sort(),changed,addedDirectories:actual.directories.filter(p=>!oldDirs.has(p)),removedDirectories:expected.directories.filter(p=>!newDirs.has(p))};
 return {...result,equal:added.length===0&&result.removed.length===0&&changed.length===0&&result.addedDirectories.length===0&&result.removedDirectories.length===0};
}
function roots(sourceRoot,snapshotRoot,{fresh=false}={}){
 const source=directory(absolute(sourceRoot)),snapshot=absolute(snapshotRoot);
 need(!inside(source,snapshot)&&!inside(snapshot,source),'SNAPSHOT_ROOTS_OVERLAP');
 need(key(path.dirname(source))===key(path.dirname(snapshot)),'SNAPSHOT_SIBLING_DESTINATION_REQUIRED');
 noReparse(path.dirname(snapshot));need(directory(path.dirname(snapshot))===path.dirname(source),'SNAPSHOT_SHARED_PARENT_REQUIRED');
 noReparse(snapshot);if(fresh)need(!exists(snapshot),'SNAPSHOT_FRESH_DESTINATION_REQUIRED');
 return {sourceRoot:source,snapshotRoot:snapshot};
}
function writeJson(root,rel,value){
 const file=member(root,rel);noReparse(root);noReparse(path.dirname(file));noReparse(file);
 const temporary=path.join(path.dirname(file),'.write-'+randomUUID()+'.json');fs.writeFileSync(temporary,JSON.stringify(value,null,2)+'\n',{flag:'wx'});noReparse(file);fs.renameSync(temporary,file);return {path:rel,...readHash(file)};
}
function copyOne(sourceRoot,snapshotRoot,row){
 const source=member(sourceRoot,row.path),target=member(snapshotRoot,row.path);noReparse(source);noReparse(path.dirname(target));need(!exists(target),'SNAPSHOT_COPY_OVERWRITE_REJECTED',{path:row.path});
 const initial=fs.lstatSync(source,{bigint:true});need(initial.isFile(),'SNAPSHOT_REGULAR_FILE_REQUIRED');
 const from=fs.openSync(source,fs.constants.O_RDONLY|(fs.constants.O_NOFOLLOW??0));let to;
 try{
  const state=fs.fstatSync(from,{bigint:true});need(fileState(state)===fileState(initial),'SNAPSHOT_FILE_CHANGED_DURING_COPY',{path:row.path});
  to=fs.openSync(target,fs.constants.O_WRONLY|fs.constants.O_CREAT|fs.constants.O_EXCL,Number(state.mode&0o777n));
  const h=createHash('sha256'),buffer=Buffer.allocUnsafe(1024*1024);let size=0,n;
  while((n=fs.readSync(from,buffer,0,buffer.length,null))>0){h.update(buffer.subarray(0,n));size+=n;let offset=0;while(offset<n){const written=fs.writeSync(to,buffer,offset,n-offset);need(written>0,'SNAPSHOT_WRITE_INCOMPLETE',{path:row.path});offset+=written;}}
  fs.fsyncSync(to);noReparse(source);noReparse(target);
  need(fileState(state)===fileState(fs.fstatSync(from,{bigint:true}))&&fileState(state)===fileState(fs.lstatSync(source,{bigint:true}))&&size===row.size&&h.digest('hex')===row.sha256,'SNAPSHOT_FILE_CHANGED_DURING_COPY',{path:row.path});
 }finally{if(to!==undefined)fs.closeSync(to);fs.closeSync(from);}
 const copied=readHash(target);need(copied.size===row.size&&copied.sha256===row.sha256,'SNAPSHOT_COPIED_BYTES_DIFFER',{path:row.path});
}
/** Capture and hash one descriptor's bytes in one pass, without reopening a
 * later source revision for copy. A change DURING that read is still a failure. */
function captureOneAtRead(sourceRoot,snapshotRoot,rel){
 const source=member(sourceRoot,rel),target=member(snapshotRoot,rel);noReparse(source);noReparse(snapshotRoot);
 fs.mkdirSync(path.dirname(target),{recursive:true});noReparse(path.dirname(target));need(!exists(target),'SNAPSHOT_COPY_OVERWRITE_REJECTED',{path:rel});
 const initial=fs.lstatSync(source,{bigint:true});need(initial.isFile(),'SNAPSHOT_REGULAR_FILE_REQUIRED');
 const from=fs.openSync(source,fs.constants.O_RDONLY|(fs.constants.O_NOFOLLOW??0));let to,streamIdentity;
 try{
  const state=fs.fstatSync(from,{bigint:true});need(fileState(state)===fileState(initial),'SNAPSHOT_FILE_CHANGED_DURING_CAPTURE',{path:rel});
  to=fs.openSync(target,fs.constants.O_WRONLY|fs.constants.O_CREAT|fs.constants.O_EXCL,Number(state.mode&0o777n));
  const h=createHash('sha256'),buffer=Buffer.allocUnsafe(1024*1024);let size=0,n;
  while((n=fs.readSync(from,buffer,0,buffer.length,null))>0){h.update(buffer.subarray(0,n));size+=n;need(Number.isSafeInteger(size),'SNAPSHOT_FILE_SIZE_LIMIT');let offset=0;while(offset<n){const written=fs.writeSync(to,buffer,offset,n-offset);need(written>0,'SNAPSHOT_WRITE_INCOMPLETE',{path:rel});offset+=written;}}
  fs.fsyncSync(to);noReparse(source);noReparse(target);
  need(fileState(state)===fileState(fs.fstatSync(from,{bigint:true}))&&fileState(state)===fileState(fs.lstatSync(source,{bigint:true}))&&BigInt(size)===state.size,'SNAPSHOT_FILE_CHANGED_DURING_CAPTURE',{path:rel});
  streamIdentity={size,sha256:h.digest('hex')};
 }finally{if(to!==undefined)fs.closeSync(to);fs.closeSync(from);}
 const copied=readHash(target);need(same(copied,streamIdentity),'SNAPSHOT_COPIED_BYTES_DIFFER',{path:rel});return copied;
}
/** A callback is diagnostic/test instrumentation only. The CLI supplies none.
 * No retries, deletions, VCS checkout or program launch occur on any path. */
export function createSourceSnapshot({repoRoot,outDir,onProgress=()=>{}}={}){
 need(typeof onProgress==='function','SNAPSHOT_PROGRESS_CALLBACK_INVALID');
 const bound=roots(repoRoot,outDir,{fresh:true}),{sourceRoot,snapshotRoot}=bound;
 fs.mkdirSync(snapshotRoot);fs.mkdirSync(path.join(snapshotRoot,SNAPSHOT_METADATA));
 const manifest={schemaVersion:SNAPSHOT_SCHEMA,kind:'ymcc-fixed-source-snapshot',status:'CREATING',...bound,sourceWorkspaceRoot:path.resolve(sourceRoot,'../../..'),snapshotWorkspaceRoot:path.resolve(snapshotRoot,'../../..'),startedAt:new Date().toISOString(),policy:SNAPSHOT_POLICY,metadataDirectory:SNAPSHOT_METADATA,files:[],directories:[],excluded:[],main:{path:'native/main.cpp',before:null,snapshot:null,sourceAfter:null},isCurrent:false,currentAsOf:null,requiresFinalCurrentComparison:true,snapshotPassApprovesChangedSource:false,copyVerifiedFiles:0,executionPerformed:false,installationPayloadFrozen:false};
 const manifestFile=path.join(snapshotRoot,...SNAPSHOT_MANIFEST.split('/'));writeJson(snapshotRoot,SNAPSHOT_MANIFEST,manifest);
 try{
  const before=inventory(sourceRoot);need(before.main,'SNAPSHOT_MAIN_CPP_REQUIRED');
  manifest.files=before.files;manifest.directories=before.directories;manifest.excluded=before.excluded;manifest.main.before=before.main;
  manifest.sourceBefore=writeJson(snapshotRoot,SNAPSHOT_METADATA+'/source-before.json',before);writeJson(snapshotRoot,SNAPSHOT_MANIFEST,manifest);
  onProgress({phase:'source-before',sourceRoot,snapshotRoot,fileCount:before.fileCount});
  for(const dir of before.directories){const target=member(snapshotRoot,dir);noReparse(path.dirname(target));fs.mkdirSync(target);}
  for(const row of before.files){onProgress({phase:'before-copy-file',path:row.path,sourceRoot,snapshotRoot});copyOne(sourceRoot,snapshotRoot,row);manifest.copyVerifiedFiles++;onProgress({phase:'after-copy-file',path:row.path,sourceRoot,snapshotRoot});}
  onProgress({phase:'copy-complete',sourceRoot,snapshotRoot});
  const copied=inventory(snapshotRoot,{snapshot:true});manifest.snapshotAfter=writeJson(snapshotRoot,SNAPSHOT_METADATA+'/snapshot-after.json',copied);manifest.main.snapshot=copied.main;manifest.snapshotDiff=diff(before,copied);
  onProgress({phase:'before-source-after',sourceRoot,snapshotRoot});
  const after=inventory(sourceRoot);manifest.sourceAfter=writeJson(snapshotRoot,SNAPSHOT_METADATA+'/source-after.json',after);manifest.main.sourceAfter=after.main;manifest.sourceDiff=diff(before,after);
  need(manifest.snapshotDiff.equal&&manifest.sourceDiff.equal,'SNAPSHOT_SOURCE_OR_COPY_DRIFT');
  manifest.status='PASS';manifest.isCurrent=true;manifest.currentAsOf=new Date().toISOString();
 }catch(error){manifest.status='FAIL';manifest.isCurrent=false;manifest.failure={code:error.code??'SNAPSHOT_OPERATION_FAILED',message:String(error.message).slice(0,500),details:error.details??null};}
 manifest.finishedAt=new Date().toISOString();writeJson(snapshotRoot,SNAPSHOT_MANIFEST,manifest);return {manifestFile,...manifest};
}
/** Explicit historical-version capture. Normal createSourceSnapshot remains
 * strict. Every byte is saved from the same descriptor that was hashed, then the
 * entire tree is independently verified. No atomic cross-file time is claimed;
 * source-after drift remains an original FAIL and cannot approve current source. */
export function createCapturedSourceSnapshot({repoRoot,outDir,onProgress=()=>{}}={}){
 need(typeof onProgress==='function','SNAPSHOT_PROGRESS_CALLBACK_INVALID');
 const bound=roots(repoRoot,outDir,{fresh:true}),{sourceRoot,snapshotRoot}=bound;
 fs.mkdirSync(snapshotRoot);fs.mkdirSync(path.join(snapshotRoot,SNAPSHOT_METADATA));
 const manifest={schemaVersion:SNAPSHOT_SCHEMA,kind:'ymcc-fixed-source-snapshot',captureMode:'same-descriptor-capture',atomicSourceTreeClaimed:false,status:'CREATING',...bound,sourceWorkspaceRoot:path.resolve(sourceRoot,'../../..'),snapshotWorkspaceRoot:path.resolve(snapshotRoot,'../../..'),startedAt:new Date().toISOString(),policy:SNAPSHOT_POLICY,metadataDirectory:SNAPSHOT_METADATA,files:[],directories:[],excluded:[],main:{path:'native/main.cpp',before:null,snapshot:null,sourceAfter:null},isCurrent:false,currentAsOf:null,requiresFinalCurrentComparison:true,snapshotPassApprovesChangedSource:false,copyVerifiedFiles:0,executionPerformed:false,installationPayloadFrozen:false};
 const manifestFile=path.join(snapshotRoot,...SNAPSHOT_MANIFEST.split('/'));writeJson(snapshotRoot,SNAPSHOT_MANIFEST,manifest);
 try{
  const before=inventory(sourceRoot,{captureRoot:snapshotRoot,onCaptured:event=>{if(event.phase==='after-capture-file')manifest.copyVerifiedFiles++;onProgress({...event,sourceRoot,snapshotRoot});}});need(before.main,'SNAPSHOT_MAIN_CPP_REQUIRED');
  manifest.files=before.files;manifest.directories=before.directories;manifest.excluded=before.excluded;manifest.main.before=before.main;
  manifest.sourceBefore=writeJson(snapshotRoot,SNAPSHOT_METADATA+'/source-before.json',before);writeJson(snapshotRoot,SNAPSHOT_MANIFEST,manifest);
  onProgress({phase:'source-before',sourceRoot,snapshotRoot,fileCount:before.fileCount});
  for(const dir of before.directories){const target=member(snapshotRoot,dir);noReparse(path.dirname(target));if(!exists(target))fs.mkdirSync(target);else directory(target);}
  onProgress({phase:'copy-complete',sourceRoot,snapshotRoot});
  const copied=inventory(snapshotRoot,{snapshot:true});manifest.snapshotAfter=writeJson(snapshotRoot,SNAPSHOT_METADATA+'/snapshot-after.json',copied);manifest.main.snapshot=copied.main;manifest.snapshotDiff=diff(before,copied);
  onProgress({phase:'before-source-after',sourceRoot,snapshotRoot});
  const after=inventory(sourceRoot);manifest.sourceAfter=writeJson(snapshotRoot,SNAPSHOT_METADATA+'/source-after.json',after);manifest.main.sourceAfter=after.main;manifest.sourceDiff=diff(before,after);
  need(manifest.copyVerifiedFiles===before.files.length&&manifest.snapshotDiff.equal&&manifest.sourceDiff.equal,'SNAPSHOT_SOURCE_OR_COPY_DRIFT');
  manifest.status='PASS';manifest.isCurrent=true;manifest.currentAsOf=new Date().toISOString();
 }catch(error){manifest.status='FAIL';manifest.isCurrent=false;manifest.failure={code:error.code??'SNAPSHOT_OPERATION_FAILED',message:String(error.message).slice(0,500),details:error.details??null};}
 manifest.finishedAt=new Date().toISOString();writeJson(snapshotRoot,SNAPSHOT_MANIFEST,manifest);return {manifestFile,...manifest};
}
function readJson(file){noReparse(file);const st=fs.lstatSync(file);need(st.isFile()&&st.size<=MAX_MANIFEST_BYTES,'SNAPSHOT_MANIFEST_SIZE_OR_TYPE');return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));}
function validateInventory(value){
 need(value&&Array.isArray(value.files)&&value.files.length<=MAX_FILES&&Array.isArray(value.directories),'SNAPSHOT_INVENTORY_INVALID');const seen=new Set();
 for(const row of value.files){relative(row?.path);need(!seen.has(row.path)&&Number.isSafeInteger(row.size)&&row.size>=0&&/^[a-f0-9]{64}$/.test(row.sha256),'SNAPSHOT_INVENTORY_ROW_INVALID');need(!row.path.toLowerCase().startsWith(SNAPSHOT_METADATA+'/'),'SNAPSHOT_INVENTORY_METADATA_COLLISION');seen.add(row.path);}
 const dirs=new Set();for(const dir of value.directories){relative(dir);need(!seen.has(dir)&&!dirs.has(dir),'SNAPSHOT_INVENTORY_DIRECTORY_INVALID');dirs.add(dir);}
 need(value.main&&same(value.main,value.files.find(row=>row.path==='native/main.cpp')),'SNAPSHOT_MAIN_IDENTITY_REQUIRED');
 return value;
}
function readReference(snapshotRoot,ref,name){
 need(ref&&ref.path===SNAPSHOT_METADATA+'/'+name&&Number.isSafeInteger(ref.size)&&/^[a-f0-9]{64}$/.test(ref.sha256),'SNAPSHOT_EVIDENCE_REFERENCE_INVALID');const file=member(snapshotRoot,ref.path),actual=readHash(file);need(actual.size===ref.size&&actual.sha256===ref.sha256,'SNAPSHOT_EVIDENCE_HASH_CHANGED');return validateInventory(readJson(file));
}
/** Read-only current comparison: full main and every included byte/path matter.
 * Snapshot PASS is never a waiver for a changed current source tree. */
export function compareSourceSnapshot({manifestFile,repoRoot}={}){
 const file=absolute(manifestFile);noReparse(file);const manifest=readJson(file);
 need(manifest?.schemaVersion===SNAPSHOT_SCHEMA&&manifest.kind==='ymcc-fixed-source-snapshot'&&manifest.status==='PASS'&&manifest.isCurrent===true&&manifest.requiresFinalCurrentComparison===true&&manifest.snapshotPassApprovesChangedSource===false&&manifest.executionPerformed===false&&manifest.installationPayloadFrozen===false,'SNAPSHOT_SUCCESSFUL_MANIFEST_REQUIRED');
 need(same(manifest.policy,SNAPSHOT_POLICY)&&manifest.metadataDirectory===SNAPSHOT_METADATA,'SNAPSHOT_EXCLUSION_POLICY_DIFFERS');
 const bound=roots(repoRoot,manifest.snapshotRoot);need(key(bound.sourceRoot)===key(absolute(manifest.sourceRoot))&&key(file)===key(path.join(bound.snapshotRoot,...SNAPSHOT_MANIFEST.split('/'))),'SNAPSHOT_MANIFEST_ROOT_DIFFERS');
 need(manifest.sourceWorkspaceRoot===path.resolve(bound.sourceRoot,'../../..')&&manifest.snapshotWorkspaceRoot===path.resolve(bound.snapshotRoot,'../../..'),'SNAPSHOT_WORKSPACE_IDENTITY_DIFFERS');
 const before=readReference(bound.snapshotRoot,manifest.sourceBefore,'source-before.json'),savedAfter=readReference(bound.snapshotRoot,manifest.sourceAfter,'source-after.json'),savedSnapshot=readReference(bound.snapshotRoot,manifest.snapshotAfter,'snapshot-after.json');
 need(diff(before,savedAfter).equal&&diff(before,savedSnapshot).equal&&same(manifest.files,before.files)&&same(manifest.directories,before.directories)&&same(manifest.excluded,before.excluded)&&same(manifest.main,{path:'native/main.cpp',before:before.main,snapshot:savedSnapshot.main,sourceAfter:savedAfter.main})&&manifest.copyVerifiedFiles===before.files.length,'SNAPSHOT_CAPTURE_IDENTITY_DIFFERS');
 const result={schemaVersion:SNAPSHOT_SCHEMA,kind:'ymcc-source-snapshot-current-comparison',status:'FAIL',isCurrent:false,...bound,manifestFile:file,manifestSha256:readHash(file).sha256,comparedAt:new Date().toISOString(),requiresFinalCurrentComparison:true,snapshotPassApprovesChangedSource:false,executionPerformed:false,installationPayloadFrozen:false};
 try{
  const first=inventory(bound.sourceRoot),snapshot=inventory(bound.snapshotRoot,{snapshot:true}),last=inventory(bound.sourceRoot);
  result.sourceDiff=diff(before,last);result.snapshotDiff=diff(before,snapshot);result.comparisonSourceDrift=diff(first,last);result.main={path:'native/main.cpp',captured:before.main,snapshot:snapshot.main,current:last.main};
  result.excludedNow=last.excluded;result.isCurrent=result.sourceDiff.equal&&result.snapshotDiff.equal&&result.comparisonSourceDrift.equal;result.status=result.isCurrent?'CURRENT':'STALE';
 }catch(error){result.failure={code:error.code??'SNAPSHOT_COMPARISON_FAILED',message:String(error.message).slice(0,500),details:error.details??null};}
 return result;
}

/** Read-only version provenance. An unchanged full copy may be tested even when
 * primary moved after capture. The original creation FAIL is never rewritten;
 * this proves captured bytes only, with strict independent whole-source current comparison. */
export function verifyCapturedSourceSnapshot({manifestFile,repoRoot,expectedManifestSha256}={}){
 const file=absolute(manifestFile);noReparse(file);const identity=readHash(file);
 need(/^[a-f0-9]{64}$/.test(expectedManifestSha256??'')&&identity.sha256===expectedManifestSha256,'SNAPSHOT_EXPECTED_MANIFEST_SHA_REQUIRED');
 const manifest=readJson(file),historical=manifest?.status==='FAIL'&&manifest.failure?.code==='SNAPSHOT_SOURCE_OR_COPY_DRIFT';
 need(manifest.captureMode===undefined||manifest.captureMode==='same-descriptor-capture'&&manifest.atomicSourceTreeClaimed===false,'SNAPSHOT_CAPTURE_MODE_INVALID');
 need(manifest?.schemaVersion===SNAPSHOT_SCHEMA&&manifest.kind==='ymcc-fixed-source-snapshot'&&(manifest.status==='PASS'||historical)&&manifest.requiresFinalCurrentComparison===true&&manifest.snapshotPassApprovesChangedSource===false&&manifest.executionPerformed===false&&manifest.installationPayloadFrozen===false,'SNAPSHOT_COMPLETE_CAPTURE_MANIFEST_REQUIRED');
 need(same(manifest.policy,SNAPSHOT_POLICY)&&manifest.metadataDirectory===SNAPSHOT_METADATA,'SNAPSHOT_EXCLUSION_POLICY_DIFFERS');
 const bound=roots(repoRoot,manifest.snapshotRoot);need(key(bound.sourceRoot)===key(absolute(manifest.sourceRoot))&&key(file)===key(path.join(bound.snapshotRoot,...SNAPSHOT_MANIFEST.split('/'))),'SNAPSHOT_MANIFEST_ROOT_DIFFERS');
 need(manifest.sourceWorkspaceRoot===path.resolve(bound.sourceRoot,'../../..')&&manifest.snapshotWorkspaceRoot===path.resolve(bound.snapshotRoot,'../../..'),'SNAPSHOT_WORKSPACE_IDENTITY_DIFFERS');
 const before=readReference(bound.snapshotRoot,manifest.sourceBefore,'source-before.json'),savedAfter=readReference(bound.snapshotRoot,manifest.sourceAfter,'source-after.json'),savedSnapshot=readReference(bound.snapshotRoot,manifest.snapshotAfter,'snapshot-after.json');
 const capturedDiff=diff(before,savedSnapshot),createdSourceDiff=diff(before,savedAfter);
 need(capturedDiff.equal&&same(manifest.snapshotDiff,capturedDiff)&&same(manifest.sourceDiff,createdSourceDiff)&&same(manifest.files,before.files)&&same(manifest.directories,before.directories)&&same(manifest.excluded,before.excluded)&&same(manifest.main,{path:'native/main.cpp',before:before.main,snapshot:savedSnapshot.main,sourceAfter:savedAfter.main})&&manifest.copyVerifiedFiles===before.files.length,'SNAPSHOT_CAPTURE_IDENTITY_DIFFERS');
 need(manifest.status==='PASS'?manifest.isCurrent===true&&createdSourceDiff.equal:manifest.isCurrent===false&&!createdSourceDiff.equal,'SNAPSHOT_CREATION_OUTCOME_INCONSISTENT');
 const copied=inventory(bound.snapshotRoot,{snapshot:true}),snapshotDiff=diff(before,copied);need(snapshotDiff.equal&&same(copied.main,before.main),'SNAPSHOT_CAPTURED_VERSION_CHANGED');
 const result={schemaVersion:SNAPSHOT_SCHEMA,kind:'ymcc-captured-source-version-verification',status:'FROZEN_SOURCE_VERIFIED',snapshotIntegrityVerified:true,creationStatus:manifest.status,creationFailure:manifest.failure??null,createdPrimaryMatched:createdSourceDiff.equal,isCurrent:false,sourceApprovalGranted:false,...bound,manifestFile:file,manifestSha256:identity.sha256,verifiedAt:new Date().toISOString(),fileCount:before.files.length,copyVerifiedFiles:manifest.copyVerifiedFiles,main:{captured:before.main,snapshot:copied.main,current:null},snapshotDiff,createdSourceDiff,requiresFinalCurrentComparison:true,snapshotPassApprovesChangedSource:false,executionPerformed:false,installationPayloadFrozen:false};
 try{
  const first=inventory(bound.sourceRoot),last=inventory(bound.sourceRoot);result.sourceDiff=diff(before,last);result.comparisonSourceDrift=diff(first,last);result.main.current=last.main;result.isCurrent=result.sourceDiff.equal&&result.comparisonSourceDrift.equal;
 }catch(error){result.currentComparisonFailure={code:error.code??'SNAPSHOT_CURRENT_COMPARISON_FAILED',message:String(error.message).slice(0,500),details:error.details??null};}
 need(readHash(file).sha256===identity.sha256,'SNAPSHOT_MANIFEST_CHANGED_DURING_VERIFICATION');
 return result;
}
