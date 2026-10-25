import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath,pathToFileURL} from 'node:url';
import * as capturedSnapshot from './source-snapshot.mjs';

export const DEFAULT_SEED_PACKAGES=Object.freeze(['typescript','vue','esbuild']);
export const NODE_TOOLS_BINDING_NAME='node-tools-binding.json';
const METADATA='.ymcc-source-snapshot',MANIFEST='manifest.json',SCHEMA=1;
const LIMITS={metadata:16*1024*1024,receipt:256*1024*1024,packages:10000,files:500000};
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const key=file=>process.platform==='win32'?path.resolve(file).toLowerCase():path.resolve(file);
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const need=(ok,code,details)=>{if(!ok)throw Object.assign(Error(code),{code,details});};
const failure=error=>({code:error.code??'NODE_TOOLS_OPERATION_FAILED',message:String(error.message).slice(0,500),details:error.details??null});
const exists=file=>{try{fs.lstatSync(file);return true;}catch(error){if(error.code==='ENOENT')return false;throw error;}};
function absolute(value){need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'NODE_TOOLS_ABSOLUTE_PATH_REQUIRED');return path.resolve(value);}
function plain(file){
 for(let cursor=path.resolve(file);;cursor=path.dirname(cursor)){
  if(exists(cursor)){need(!fs.lstatSync(cursor).isSymbolicLink(),'NODE_TOOLS_REPARSE_PATH',{path:cursor});need(key(fs.realpathSync(cursor))===key(cursor),'NODE_TOOLS_REPARSE_PATH',{path:cursor});}
  if(path.dirname(cursor)===cursor)break;
 }
}
function plainDirectory(file){plain(file);need(fs.lstatSync(file).isDirectory(),'NODE_TOOLS_DIRECTORY_REQUIRED',{path:file});return fs.realpathSync(file);}
const state=stat=>[...(process.platform==='win32'?[]:[stat.dev]),stat.ino,stat.size,stat.mtimeNs,stat.ctimeNs].map(String).join(':');
function hashFile(file,{bytes=false,max=Infinity}={}){
 const initial=fs.lstatSync(file,{bigint:true});need(initial.isFile()&&!initial.isSymbolicLink(),'NODE_TOOLS_REGULAR_FILE_REQUIRED',{path:file});need(initial.size<=BigInt(Number.isFinite(max)?max:Number.MAX_SAFE_INTEGER),'NODE_TOOLS_FILE_SIZE_LIMIT',{path:file});
 const fd=fs.openSync(file,fs.constants.O_RDONLY|(fs.constants.O_NOFOLLOW??0));
 try{
  const before=fs.fstatSync(fd,{bigint:true});need(state(before)===state(initial),'NODE_TOOLS_FILE_CHANGED',{path:file});
  const h=createHash('sha256'),buffer=Buffer.allocUnsafe(1024*1024),chunks=[];let size=0,n;
  while((n=fs.readSync(fd,buffer,0,buffer.length,null))>0){size+=n;need(size<=max&&Number.isSafeInteger(size),'NODE_TOOLS_FILE_SIZE_LIMIT',{path:file});h.update(buffer.subarray(0,n));if(bytes)chunks.push(Buffer.from(buffer.subarray(0,n)));}
  need(state(before)===state(fs.fstatSync(fd,{bigint:true}))&&state(before)===state(fs.lstatSync(file,{bigint:true}))&&BigInt(size)===before.size,'NODE_TOOLS_FILE_CHANGED',{path:file});
  return {size,sha256:h.digest('hex'),...(bytes?{bytes:Buffer.concat(chunks)}:{})};
 }finally{fs.closeSync(fd);}
}
function jsonFile(file,max=LIMITS.metadata){const value=hashFile(file,{bytes:true,max});return {value:JSON.parse(value.bytes.toString('utf8').replace(/^\uFEFF/,'')),identity:{path:file,size:value.size,sha256:value.sha256}};}
const verifierSourceFile=fileURLToPath(new URL('./source-snapshot.mjs',import.meta.url));
// Pin the local source module for the process lifetime. Historical verification
// cannot silently use cached code while reporting the hash of newer disk bytes.
const verifierAtModuleLoad=hashFile(verifierSourceFile);
export function validateCaptureVerification(proof,{creationStatus,expectedManifestSha256}={}){
 need(proof?.status==='FROZEN_SOURCE_VERIFIED'&&proof.snapshotIntegrityVerified===true&&typeof proof.isCurrent==='boolean'&&proof.creationStatus===creationStatus&&['PASS','FAIL'].includes(creationStatus),'NODE_TOOLS_CAPTURE_INTEGRITY_REQUIRED');
 need(proof.sourceApprovalGranted===false&&/^[a-f0-9]{64}$/.test(expectedManifestSha256??'')&&proof.manifestSha256===expectedManifestSha256,'NODE_TOOLS_CAPTURE_MANIFEST_BINDING_INVALID');
 need(!proof.isCurrent||proof.sourceDiff?.equal===true,'NODE_TOOLS_CAPTURE_CURRENTNESS_INVALID');
 return proof;
}
const sourceComparisonChecked=proof=>typeof proof?.sourceDiff?.equal==='boolean'&&!proof.currentComparisonFailure;
function historicalAuthority({file,m,expectedSnapshotManifestSha256,primaryRepo}){
 need(m?.schemaVersion===1&&m.kind==='ymcc-fixed-source-snapshot'&&['PASS','FAIL'].includes(m.status)&&m.isCurrent===(m.status==='PASS')&&m.executionPerformed===false&&m.requiresFinalCurrentComparison===true&&m.snapshotPassApprovesChangedSource===false&&m.installationPayloadFrozen===false,'NODE_TOOLS_HISTORICAL_SNAPSHOT_INVALID');
 const sourceRoot=plainDirectory(absolute(primaryRepo)),snapshotRoot=plainDirectory(absolute(m.snapshotRoot));
 need(key(sourceRoot)===key(absolute(m.sourceRoot))&&key(file)===key(path.join(snapshotRoot,METADATA,MANIFEST))&&m.metadataDirectory===METADATA,'NODE_TOOLS_SNAPSHOT_ROOT_DIFFERS');
 need(!inside(sourceRoot,snapshotRoot)&&!inside(snapshotRoot,sourceRoot)&&key(path.dirname(sourceRoot))===key(path.dirname(snapshotRoot)),'NODE_TOOLS_SIBLING_SNAPSHOT_REQUIRED');plainDirectory(path.join(snapshotRoot,METADATA));
 need(typeof capturedSnapshot.verifyCapturedSourceSnapshot==='function','NODE_TOOLS_CAPTURE_VERIFIER_UNAVAILABLE');
 plain(verifierSourceFile);need(equal(hashFile(verifierSourceFile),verifierAtModuleLoad),'NODE_TOOLS_CAPTURE_VERIFIER_CODE_CHANGED');
 const captureVerification=validateCaptureVerification(capturedSnapshot.verifyCapturedSourceSnapshot({manifestFile:file,repoRoot:sourceRoot,expectedManifestSha256:expectedSnapshotManifestSha256}),{creationStatus:m.status,expectedManifestSha256:expectedSnapshotManifestSha256});
 need(equal(hashFile(verifierSourceFile),verifierAtModuleLoad),'NODE_TOOLS_CAPTURE_VERIFIER_CODE_CHANGED');need(hashFile(file).sha256===expectedSnapshotManifestSha256,'NODE_TOOLS_SNAPSHOT_SHA_CHANGED');
 return {sourceRoot,snapshotRoot,snapshotManifestFile:file,expectedSnapshotManifestSha256,creationStatus:m.status,isCurrent:captureVerification.isCurrent,sourceCurrentAtBind:captureVerification.isCurrent,verifierCodeSha256:verifierAtModuleLoad.sha256,verifierSource:{path:verifierSourceFile,...verifierAtModuleLoad},captureVerification,wholeCaptureVerification:captureVerification};
}
function authority({manifestFile,expectedSnapshotManifestSha256,primaryRepo,allowHistoricalSnapshot=false}){
 need(typeof allowHistoricalSnapshot==='boolean','NODE_TOOLS_HISTORICAL_FLAG_INVALID');
 need(/^[a-f0-9]{64}$/.test(expectedSnapshotManifestSha256??''),'NODE_TOOLS_EXPECTED_SNAPSHOT_SHA_REQUIRED');
 const file=absolute(manifestFile);plain(file);const read=jsonFile(file,LIMITS.receipt),m=read.value;
 need(read.identity.sha256===expectedSnapshotManifestSha256,'NODE_TOOLS_SNAPSHOT_SHA_CHANGED');
 if(allowHistoricalSnapshot)return historicalAuthority({file,m,expectedSnapshotManifestSha256,primaryRepo});
 need(m?.schemaVersion===1&&m.kind==='ymcc-fixed-source-snapshot'&&m.status==='PASS'&&m.isCurrent===true&&m.executionPerformed===false&&m.requiresFinalCurrentComparison===true&&m.snapshotPassApprovesChangedSource===false&&m.installationPayloadFrozen===false,'NODE_TOOLS_SUCCESSFUL_SNAPSHOT_REQUIRED');
 const sourceRoot=plainDirectory(absolute(primaryRepo)),snapshotRoot=plainDirectory(absolute(m.snapshotRoot));
 need(key(sourceRoot)===key(absolute(m.sourceRoot))&&key(file)===key(path.join(snapshotRoot,METADATA,MANIFEST))&&m.metadataDirectory===METADATA,'NODE_TOOLS_SNAPSHOT_ROOT_DIFFERS');
 need(!inside(sourceRoot,snapshotRoot)&&!inside(snapshotRoot,sourceRoot)&&key(path.dirname(sourceRoot))===key(path.dirname(snapshotRoot)),'NODE_TOOLS_SIBLING_SNAPSHOT_REQUIRED');
 plainDirectory(path.join(snapshotRoot,METADATA));
 need(m.sourceWorkspaceRoot===path.resolve(sourceRoot,'../../..')&&m.snapshotWorkspaceRoot===path.resolve(snapshotRoot,'../../..'),'NODE_TOOLS_WORKSPACE_DIFFERS');
 need(Array.isArray(m.files)&&m.files.length>0&&Array.isArray(m.directories)&&Array.isArray(m.excluded)&&m.copyVerifiedFiles===m.files.length&&m.sourceDiff?.equal===true&&m.snapshotDiff?.equal===true,'NODE_TOOLS_SNAPSHOT_CAPTURE_INCOMPLETE');
 need(m.policy?.rules?.some(rule=>rule.id==='node-dependencies')&&m.policy.installationPayloadFrozen===false,'NODE_TOOLS_NODE_MODULES_EXCLUSION_REQUIRED');
 const main=m.files.find(row=>row.path==='native/main.cpp');need(main&&/^[a-f0-9]{64}$/.test(main.sha256)&&equal(m.main,{path:'native/main.cpp',before:main,snapshot:main,sourceAfter:main}),'NODE_TOOLS_SNAPSHOT_MAIN_IDENTITY_REQUIRED');
 // Validate the three original capture receipts, without comparing live source.
 for(const [field,name] of [['sourceBefore','source-before.json'],['snapshotAfter','snapshot-after.json'],['sourceAfter','source-after.json']]){
  const ref=m[field];need(ref?.path===METADATA+'/'+name&&Number.isSafeInteger(ref.size)&&/^[a-f0-9]{64}$/.test(ref.sha256),'NODE_TOOLS_CAPTURE_REFERENCE_INVALID');
  const target=path.join(snapshotRoot,METADATA,name);plain(target);const captured=jsonFile(target,LIMITS.receipt);
  need(captured.identity.size===ref.size&&captured.identity.sha256===ref.sha256&&equal(captured.value.files,m.files)&&equal(captured.value.directories,m.directories)&&equal(captured.value.main,main),'NODE_TOOLS_CAPTURE_REFERENCE_CHANGED');
 }
 return {sourceRoot,snapshotRoot,snapshotManifestFile:file,expectedSnapshotManifestSha256};
}
function packageName(name){need(typeof name==='string'&&/^(?:@[a-zA-Z0-9_][a-zA-Z0-9_.-]*\/)?[a-zA-Z0-9_][a-zA-Z0-9_.-]*$/.test(name)&&name!=='.'&&name!=='..','NODE_TOOLS_PACKAGE_NAME_INVALID',{name});return name;}
function toolPath(root,file){const actual=fs.realpathSync(file);need(inside(root,actual)&&key(actual)!==key(root),'NODE_TOOLS_PACKAGE_OUTSIDE_PRIMARY',{path:file,realpath:actual});return actual;}
function packageLookup(req,name,bound){
 // Restrict candidate lookup before resolution, so a global installation cannot
 // satisfy a missing local prerequisite. No package code is imported or run.
 for(const lookup of req.resolve.paths(name)??[]){
  const candidate=path.join(lookup,...name.split('/'));
  if(!inside(bound.nodeModulesPath,candidate)&&!inside(bound.nodeModulesRealRoot,candidate))continue;
  if(!exists(candidate))continue;
  const root=toolPath(bound.nodeModulesRealRoot,candidate);need(fs.statSync(root).isDirectory(),'NODE_TOOLS_PACKAGE_DIRECTORY_REQUIRED',{name});
  return {candidate,root,link:fs.lstatSync(candidate).isSymbolicLink()?fs.readlinkSync(candidate):null};
 }
 return null;
}
function resolvePackage(req,name,bound){
 packageName(name);const located=packageLookup(req,name,bound);if(!located)return null;
 const metadataFile=path.join(located.root,'package.json');need(exists(metadataFile),'NODE_TOOLS_PACKAGE_METADATA_MISSING',{name,root:located.root});
 let resolvedMetadata,via='package-json',metadataError=null,entry=null,entryError=null;
 try{resolvedMetadata=req.resolve(name+'/package.json');}catch(error){metadataError=error.code??'RESOLVE_FAILED';}
 try{entry=req.resolve(name);}catch(error){entryError=error.code??'RESOLVE_FAILED';}
 if(entry!==null){need(path.isAbsolute(entry),'NODE_TOOLS_PACKAGE_OUTSIDE_PRIMARY',{name,entry});entry=toolPath(bound.nodeModulesRealRoot,entry);}
 if(resolvedMetadata){resolvedMetadata=toolPath(bound.nodeModulesRealRoot,resolvedMetadata);need(key(resolvedMetadata)===key(metadataFile),'NODE_TOOLS_RESOLVER_ROOT_DIFFERS',{name,resolvedMetadata,metadataFile});}
 else{
  need(entry!==null,'NODE_TOOLS_INSTALLED_PACKAGE_UNRESOLVABLE',{name,metadataError,entryError});via='entry-upward-name-match';let cursor=path.dirname(entry);
  for(;;){const candidate=path.join(cursor,'package.json');if(exists(candidate)){const candidateReal=toolPath(bound.nodeModulesRealRoot,candidate),parsed=jsonFile(candidateReal).value;if(parsed.name===name){resolvedMetadata=candidateReal;break;}}const parent=path.dirname(cursor);if(parent===cursor||!inside(bound.nodeModulesRealRoot,parent))break;cursor=parent;}
  need(resolvedMetadata&&key(resolvedMetadata)===key(metadataFile),'NODE_TOOLS_RESOLVER_ROOT_DIFFERS',{name,entry,metadataFile});
 }
 const read=jsonFile(metadataFile),metadata=read.value;need(metadata&&metadata.name===name&&typeof metadata.version==='string'&&metadata.version.length>0,'NODE_TOOLS_PACKAGE_METADATA_INVALID',{name,root:located.root});
 if(entry!==null)need(inside(located.root,entry),'NODE_TOOLS_PACKAGE_ENTRY_OUTSIDE_ROOT',{name,entry});
 return {root:located.root,name,version:metadata.version,metadata,metadataIdentity:read.identity,resolution:{candidate:located.candidate,candidateLink:located.link,root:located.root,metadataFile,via,metadataResolutionError:metadataError,entry:{resolved:entry!==null,path:entry,error:entryError}}};
}
function requirements(metadata){
 const byName=new Map();
 for(const kind of ['dependencies','optionalDependencies','peerDependencies']){
  const list=metadata[kind];if(list===undefined)continue;need(list&&typeof list==='object'&&!Array.isArray(list),'NODE_TOOLS_DEPENDENCY_METADATA_INVALID',{kind});
  for(const name of Object.keys(list).sort()){
   packageName(name);need(typeof list[name]==='string','NODE_TOOLS_DEPENDENCY_METADATA_INVALID',{kind,name});
   const optional=kind==='optionalDependencies'||(kind==='dependencies'&&Object.hasOwn(metadata.optionalDependencies??{},name))||(kind==='peerDependencies'&&metadata.peerDependenciesMeta?.[name]?.optional===true);
   if(!byName.has(name))byName.set(name,[]);byName.get(name).push({kind,spec:list[name],optional});
  }
 }
 return [...byName].sort(([a],[b])=>a<b?-1:a>b?1:0).map(([name,declarations])=>({name,declarations,required:declarations.some(row=>!row.optional)}));
}
function packageFiles(root,bound){
 const files=[],directories=[],links=[];let totalBytes=0;
 function walk(physical,prefix,ancestors){
  const real=toolPath(bound.nodeModulesRealRoot,physical);need(!ancestors.has(key(real)),'NODE_TOOLS_PACKAGE_LINK_CYCLE',{path:physical});
  const next=new Set(ancestors);next.add(key(real));const names=fs.readdirSync(real).sort();
  for(const name of names){
   const rel=prefix?prefix+'/'+name:name,file=path.join(real,name),st=fs.lstatSync(file);let target=file;
   if(st.isSymbolicLink()){target=toolPath(bound.nodeModulesRealRoot,file);links.push({path:rel,target:fs.readlinkSync(file),realpath:target});}
   const actual=fs.statSync(target);
   if(actual.isDirectory()){directories.push(rel);walk(target,rel,next);}
   else{need(actual.isFile(),'NODE_TOOLS_SPECIAL_PACKAGE_FILE',{path:file});need(files.length<LIMITS.files,'NODE_TOOLS_FILE_COUNT_LIMIT');const row={path:rel,...hashFile(target)};files.push(row);totalBytes+=row.size;need(Number.isSafeInteger(totalBytes),'NODE_TOOLS_TOTAL_SIZE_LIMIT');}
   need(key(fs.realpathSync(file))===key(target),'NODE_TOOLS_PACKAGE_PATH_CHANGED',{path:file});
  }
  need(equal(names,fs.readdirSync(real).sort()),'NODE_TOOLS_PACKAGE_DIRECTORY_CHANGED',{path:real});
 }
 walk(root,'',new Set());files.sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);directories.sort();links.sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
 return {files,directories,links,fileCount:files.length,totalBytes};
}
/** Full predeclared package contents and declared dependency resolution, not an
 * OS loaded-module graph. Only package metadata is parsed; no tool is executed. */
export function captureSnapshotNodeToolGraph({primaryRepo}={}){
 const sourceRoot=plainDirectory(absolute(primaryRepo)),primaryPackageJson=path.join(sourceRoot,'package.json');plain(primaryPackageJson);const primaryIdentity=hashFile(primaryPackageJson);
 const nodeModulesPath=path.join(sourceRoot,'node_modules');need(exists(nodeModulesPath),'NODE_TOOLS_PRIMARY_NODE_MODULES_REQUIRED');const nodeModulesRealRoot=fs.realpathSync(nodeModulesPath);need(fs.statSync(nodeModulesRealRoot).isDirectory(),'NODE_TOOLS_PRIMARY_NODE_MODULES_REQUIRED');
 const bound={nodeModulesPath,nodeModulesRealRoot},packages=new Map(),queue=[],seeds=[],edges=[],unresolvedOptional=[];
 const add=resolved=>{if(!packages.has(key(resolved.root))){need(packages.size<LIMITS.packages,'NODE_TOOLS_PACKAGE_COUNT_LIMIT');packages.set(key(resolved.root),resolved);queue.push(resolved);}return resolved;};
 const primaryRequire=createRequire(pathToFileURL(primaryPackageJson));
 for(const name of DEFAULT_SEED_PACKAGES){const found=resolvePackage(primaryRequire,name,bound);need(found,'NODE_TOOLS_REQUIRED_PACKAGE_MISSING',{name,from:sourceRoot});need(found.resolution.entry.resolved,'NODE_TOOLS_SEED_ENTRY_UNRESOLVABLE',{name});add(found);seeds.push({name,root:found.root,version:found.version,resolution:found.resolution});}
 for(let i=0;i<queue.length;i++){
  const current=queue[i],req=createRequire(pathToFileURL(path.join(current.root,'package.json')));
  for(const declaration of requirements(current.metadata)){
   const found=resolvePackage(req,declaration.name,bound),row={from:current.root,...declaration};
   if(found){add(found);edges.push({...row,resolved:true,to:found.root,resolution:found.resolution});}
   else{need(!declaration.required,'NODE_TOOLS_REQUIRED_PACKAGE_MISSING',{name:declaration.name,from:current.root});const missing={...row,resolved:false,reason:'NOT_INSTALLED_IN_PRIMARY_NODE_MODULES'};edges.push(missing);unresolvedOptional.push(missing);}
  }
 }
 const captured=[...packages.values()].sort((a,b)=>key(a.root)<key(b.root)?-1:key(a.root)>key(b.root)?1:0).map(found=>{
  const inventory=packageFiles(found.root,bound),metadataRow=inventory.files.find(row=>row.path==='package.json');need(metadataRow&&metadataRow.size===found.metadataIdentity.size&&metadataRow.sha256===found.metadataIdentity.sha256,'NODE_TOOLS_PACKAGE_METADATA_CHANGED',{root:found.root});
  return {root:found.root,name:found.name,version:found.version,metadata:found.metadata,metadataIdentity:found.metadataIdentity,...inventory};
 });
 need(equal(primaryIdentity,hashFile(primaryPackageJson))&&key(fs.realpathSync(nodeModulesPath))===key(nodeModulesRealRoot),'NODE_TOOLS_PRIMARY_AUTHORITY_CHANGED');
 const fileCount=captured.reduce((n,p)=>n+p.fileCount,0);need(fileCount<=LIMITS.files,'NODE_TOOLS_FILE_COUNT_LIMIT');
 const graph={schemaVersion:SCHEMA,kind:'predeclared-full-node-tool-package-graph',primaryRepo:sourceRoot,primaryPackageJson:{path:primaryPackageJson,...primaryIdentity},...bound,seedPackages:[...DEFAULT_SEED_PACKAGES],seeds,packages:captured,edges,unresolvedOptional,packageCount:captured.length,fileCount,totalBytes:captured.reduce((n,p)=>n+p.totalBytes,0),packageCodeExecuted:false,osLoadedModuleGraph:false};
 return {graph,sha256:digest(JSON.stringify(graph))};
}
function inspectAliases(snapshotRoot,seeds){
 const root=path.join(snapshotRoot,'node_modules');plainDirectory(root);need(!fs.lstatSync(root).isSymbolicLink(),'NODE_TOOLS_ALIAS_ROOT_MUST_BE_ORDINARY');
 const names=fs.readdirSync(root).sort(),expected=seeds.map(row=>row.name).sort();need(equal(names,expected),'NODE_TOOLS_ALIAS_NAMES_CHANGED',{actual:names,expected});
 return seeds.map(seed=>{const alias=path.join(root,seed.name),st=fs.lstatSync(alias);need(st.isSymbolicLink()&&fs.statSync(alias).isDirectory(),'NODE_TOOLS_ALIAS_JUNCTION_REQUIRED',{alias});const realpath=fs.realpathSync(alias);need(key(realpath)===key(seed.root),'NODE_TOOLS_ALIAS_TARGET_CHANGED',{alias,expected:seed.root,actual:realpath});return {name:seed.name,path:alias,target:fs.readlinkSync(alias),realpath};});
}
const scope={toolScope:'Predeclared full Node tool packages and declared dependency graph; not OS loaded modules.',packageCodeExecuted:false,osLoadedModuleGraph:false,sourceCurrentnessChecked:false,sourceApprovalGranted:false,requiresSeparateSourceCurrentComparison:true,executionPerformed:false};
/** Bind only into a fresh ordinary node_modules. Failures leave the directory,
 * any created junctions and a single wx FAIL receipt; never delete or retry. */
export function bindSnapshotNodeTools({manifestFile,expectedSnapshotManifestSha256,primaryRepo,allowHistoricalSnapshot=false,onProgress=()=>{}}={}){
 need(typeof onProgress==='function','NODE_TOOLS_PROGRESS_INVALID');const bound=authority({manifestFile,expectedSnapshotManifestSha256,primaryRepo,allowHistoricalSnapshot}),bindingManifestFile=path.join(bound.snapshotRoot,METADATA,NODE_TOOLS_BINDING_NAME),aliasRoot=path.join(bound.snapshotRoot,'node_modules');
 plain(bindingManifestFile);need(!exists(bindingManifestFile),'NODE_TOOLS_FRESH_BINDING_REQUIRED');need(!exists(aliasRoot),'NODE_TOOLS_FRESH_NODE_MODULES_REQUIRED');
 fs.mkdirSync(aliasRoot);const receipt={schemaVersion:SCHEMA,kind:'ymcc-snapshot-node-tools-binding',status:'CREATING',...bound,allowHistoricalSnapshot,bindingManifestFile,seedPackages:[...DEFAULT_SEED_PACKAGES],startedAt:new Date().toISOString(),...scope,sourceCurrentnessChecked:allowHistoricalSnapshot?sourceComparisonChecked(bound.captureVerification):false};
 try{
  const before=captureSnapshotNodeToolGraph({primaryRepo:bound.sourceRoot});receipt.graph=before.graph;receipt.graphBeforeSha256=before.sha256;onProgress({phase:'graph-before',snapshotRoot:bound.snapshotRoot});
  for(const seed of before.graph.seeds){plainDirectory(aliasRoot);fs.symlinkSync(seed.root,path.join(aliasRoot,seed.name),'junction');}
  receipt.aliases=inspectAliases(bound.snapshotRoot,before.graph.seeds);onProgress({phase:'aliases-created',snapshotRoot:bound.snapshotRoot});
  const after=captureSnapshotNodeToolGraph({primaryRepo:bound.sourceRoot});receipt.graphAfterSha256=after.sha256;need(before.sha256===after.sha256&&equal(before.graph,after.graph),'NODE_TOOLS_GRAPH_DRIFT');
  need(equal(receipt.aliases,inspectAliases(bound.snapshotRoot,after.graph.seeds)),'NODE_TOOLS_ALIAS_DRIFT');
  const finalAuthority=authority({manifestFile:bound.snapshotManifestFile,expectedSnapshotManifestSha256,primaryRepo:bound.sourceRoot,allowHistoricalSnapshot});
  if(allowHistoricalSnapshot){need(finalAuthority.verifierCodeSha256===bound.verifierCodeSha256,'NODE_TOOLS_CAPTURE_VERIFIER_CODE_CHANGED');receipt.captureVerificationBefore=bound.captureVerification;receipt.captureVerification=finalAuthority.captureVerification;receipt.wholeCaptureVerification=finalAuthority.captureVerification;receipt.sourceCurrentAtBind=finalAuthority.sourceCurrentAtBind;receipt.isCurrent=finalAuthority.sourceCurrentAtBind;receipt.sourceCurrentnessChecked=sourceComparisonChecked(finalAuthority.captureVerification);}
  receipt.status='PASS';
 }catch(error){receipt.status='FAIL';receipt.failure=failure(error);}
 receipt.finishedAt=new Date().toISOString();plain(bindingManifestFile);fs.writeFileSync(bindingManifestFile,JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});return receipt;
}
/** Read-only verification. Source mainline may be stale even when tools PASS. */
export function verifySnapshotNodeTools({bindingManifestFile}={}){
 const file=absolute(bindingManifestFile);plain(file);const read=jsonFile(file,LIMITS.receipt),saved=read.value,result={schemaVersion:SCHEMA,kind:'ymcc-snapshot-node-tools-verification',status:'FAIL',bindingManifestFile:file,bindingManifestSha256:read.identity.sha256,verifiedAt:new Date().toISOString(),...scope};
 try{
  need(saved?.schemaVersion===SCHEMA&&saved.kind==='ymcc-snapshot-node-tools-binding'&&saved.status==='PASS'&&equal(saved.seedPackages,[...DEFAULT_SEED_PACKAGES]),'NODE_TOOLS_SUCCESSFUL_BINDING_REQUIRED');
  need(Object.entries(scope).every(([name,value])=>saved[name]===(name==='sourceCurrentnessChecked'&&saved.allowHistoricalSnapshot===true?sourceComparisonChecked(saved.captureVerification):value)),'NODE_TOOLS_SCOPE_CHANGED');
  need(saved.allowHistoricalSnapshot===undefined||typeof saved.allowHistoricalSnapshot==='boolean','NODE_TOOLS_HISTORICAL_FLAG_INVALID');const allowHistoricalSnapshot=saved.allowHistoricalSnapshot===true;
  const bound=authority({manifestFile:saved.snapshotManifestFile,expectedSnapshotManifestSha256:saved.expectedSnapshotManifestSha256,primaryRepo:saved.sourceRoot,allowHistoricalSnapshot});
  if(allowHistoricalSnapshot){
   validateCaptureVerification(saved.captureVerification,{creationStatus:bound.creationStatus,expectedManifestSha256:saved.expectedSnapshotManifestSha256});validateCaptureVerification(saved.captureVerificationBefore,{creationStatus:bound.creationStatus,expectedManifestSha256:saved.expectedSnapshotManifestSha256});
   need(equal(saved.wholeCaptureVerification,saved.captureVerification)&&saved.isCurrent===saved.sourceCurrentAtBind&&saved.creationStatus===bound.creationStatus&&saved.sourceCurrentAtBind===saved.captureVerification.isCurrent&&saved.verifierCodeSha256===bound.verifierCodeSha256&&equal(saved.verifierSource,bound.verifierSource),'NODE_TOOLS_SAVED_CAPTURE_AUTHORITY_CHANGED');
  }
  need(key(file)===key(path.join(bound.snapshotRoot,METADATA,NODE_TOOLS_BINDING_NAME))&&saved.bindingManifestFile===file&&saved.snapshotRoot===bound.snapshotRoot,'NODE_TOOLS_BINDING_ROOT_DIFFERS');
  need(saved.graphBeforeSha256===saved.graphAfterSha256&&saved.graphBeforeSha256===digest(JSON.stringify(saved.graph)),'NODE_TOOLS_SAVED_GRAPH_INVALID');
  const first=captureSnapshotNodeToolGraph({primaryRepo:bound.sourceRoot}),aliases=inspectAliases(bound.snapshotRoot,first.graph.seeds),last=captureSnapshotNodeToolGraph({primaryRepo:bound.sourceRoot});
  need(equal(aliases,saved.aliases)&&equal(aliases,inspectAliases(bound.snapshotRoot,last.graph.seeds)),'NODE_TOOLS_ALIAS_CHANGED');
  need(first.sha256===last.sha256&&last.sha256===saved.graphBeforeSha256&&equal(last.graph,saved.graph),'NODE_TOOLS_GRAPH_CHANGED');
  const finalAuthority=authority({manifestFile:bound.snapshotManifestFile,expectedSnapshotManifestSha256:saved.expectedSnapshotManifestSha256,primaryRepo:bound.sourceRoot,allowHistoricalSnapshot});need(hashFile(file).sha256===read.identity.sha256,'NODE_TOOLS_BINDING_CHANGED_DURING_VERIFY');
  if(allowHistoricalSnapshot){need(finalAuthority.verifierCodeSha256===saved.verifierCodeSha256,'NODE_TOOLS_CAPTURE_VERIFIER_CODE_CHANGED');Object.assign(result,{allowHistoricalSnapshot:true,creationStatus:bound.creationStatus,isCurrent:finalAuthority.captureVerification.isCurrent,sourceCurrentAtBind:saved.sourceCurrentAtBind,sourceCurrentAtVerify:finalAuthority.captureVerification.isCurrent,verifierCodeSha256:bound.verifierCodeSha256,verifierSource:bound.verifierSource,captureVerification:finalAuthority.captureVerification,wholeCaptureVerification:finalAuthority.captureVerification,sourceCurrentnessChecked:sourceComparisonChecked(finalAuthority.captureVerification)});}
  Object.assign(result,{status:'PASS',graphSha256:last.sha256,packageCount:last.graph.packageCount,fileCount:last.graph.fileCount,unresolvedOptional:last.graph.unresolvedOptional});
 }catch(error){result.failure=failure(error);}
 return result;
}
export const SNAPSHOT_NODE_TOOLS_HELP=`Bind or verify shared primary Node tools; never import tools, launch a simulator or compile.
--bind ABS_SNAPSHOT_MANIFEST --expected-sha SHA256 --repo ABS_PRIMARY_REPO [--snapshot-only]
--verify ABS_BINDING_JSON
Creates one ordinary snapshot node_modules with exactly typescript/vue/esbuild junctions.
Records their complete primary package graph before and after; missing required packages fail.
Default bind requires the original PASS path. Explicit --snapshot-only permits a fully
captured historical snapshot only after the source verifier confirms complete snapshot integrity.
The original creation PASS/FAIL and current source status are recorded, never rewritten.
No snapshot manifest is rewritten. A tool PASS never approves current source; run the separate
source-snapshot current comparison after the full chain has run from the snapshot.
`;
export function parseSnapshotNodeToolArguments(args){
 if(args.length===1&&['--help','-h'].includes(args[0]))return {mode:'help'};
 const values={};let allowHistoricalSnapshot=false;
 for(let i=0;i<args.length;){
  const flag=args[i];if(flag==='--snapshot-only'){need(!allowHistoricalSnapshot,'NODE_TOOLS_ARGUMENTS_INVALID');allowHistoricalSnapshot=true;i++;continue;}
  const value=args[i+1];need(['--bind','--expected-sha','--repo','--verify'].includes(flag)&&typeof value==='string'&&value.length>0&&!value.startsWith('--')&&!Object.hasOwn(values,flag),'NODE_TOOLS_ARGUMENTS_INVALID');values[flag]=value;i+=2;
 }
 if(values['--verify']&&Object.keys(values).length===1&&!allowHistoricalSnapshot)return {mode:'verify',bindingManifestFile:values['--verify']};
 if(values['--bind']&&values['--expected-sha']&&values['--repo']&&Object.keys(values).length===3)return {mode:'bind',manifestFile:values['--bind'],expectedSnapshotManifestSha256:values['--expected-sha'],primaryRepo:values['--repo'],...(allowHistoricalSnapshot?{allowHistoricalSnapshot:true}:{})};
 throw Error('NODE_TOOLS_BIND_OR_VERIFY_REQUIRED');
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=parseSnapshotNodeToolArguments(process.argv.slice(2));if(args.mode==='help')console.log(SNAPSHOT_NODE_TOOLS_HELP);else{const result=args.mode==='bind'?bindSnapshotNodeTools(args):verifySnapshotNodeTools(args);console.log(JSON.stringify({status:result.status,bindingManifestFile:result.bindingManifestFile,bindingManifestSha256:result.bindingManifestSha256,snapshotRoot:result.snapshotRoot,graphSha256:result.graphAfterSha256??result.graphSha256,packageCount:result.graph?.packageCount??result.packageCount,fileCount:result.graph?.fileCount??result.fileCount,failure:result.failure,allowHistoricalSnapshot:result.allowHistoricalSnapshot,creationStatus:result.creationStatus,isCurrent:result.isCurrent,sourceCurrentAtBind:result.sourceCurrentAtBind,sourceCurrentAtVerify:result.sourceCurrentAtVerify,verifierCodeSha256:result.verifierCodeSha256,snapshotIntegrityVerified:result.captureVerification?.snapshotIntegrityVerified,...scope,sourceCurrentnessChecked:result.sourceCurrentnessChecked}));process.exitCode=result.status==='PASS'?0:2;}}
 catch(error){console.error(JSON.stringify({status:'FAIL',failure:failure(error),...scope}));process.exitCode=3;}
}
