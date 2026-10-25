import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {captureSnapshotNodeToolGraph} from '../../ymcc_sim/global/snapshot-node-tools.mjs';
const need=(value,code)=>{if(!value)throw Error(code);};
const key=p=>path.resolve(p).toLowerCase();
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const digest=value=>createHash('sha256').update(value).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const sha=value=>/^[a-f0-9]{64}$/.test(value??'');
const inside=(root,p)=>{const r=path.relative(root,p);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
export const BINDING_FILE_ENV='YMCC_BATTERY_UI_NODE_TOOLS_BINDING';
export const BINDING_SHA_ENV='YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256';
/** Tool authority only. Source acceptance remains the parent's independent whole-snapshot gate. */
export function resolveSnapshotNodeTools(repo,options={}){
 repo=path.resolve(repo);const manifestFile=path.join(repo,'.ymcc-source-snapshot/manifest.json'),defaultBinding=path.join(repo,'.ymcc-source-snapshot/node-tools-binding.json');
 if(!fs.existsSync(manifestFile)&&!fs.existsSync(defaultBinding))return null;
 const bindingManifestFile=options.bindingManifestFile??process.env[BINDING_FILE_ENV],expectedBindingSha256=options.expectedBindingSha256??process.env[BINDING_SHA_ENV];
 need(typeof bindingManifestFile==='string'&&path.isAbsolute(bindingManifestFile)&&sha(expectedBindingSha256),'BATTERY_UI_EXPLICIT_NODE_BINDING_AND_SHA_REQUIRED');
 need(key(bindingManifestFile)===key(defaultBinding),'BATTERY_UI_NODE_BINDING_ROOT_MISMATCH');
 need(fs.existsSync(bindingManifestFile)&&hash(bindingManifestFile)===expectedBindingSha256,'BATTERY_UI_NODE_BINDING_HASH_CHANGED');
 const saved=read(bindingManifestFile);
 need(saved.schemaVersion===1&&saved.kind==='ymcc-snapshot-node-tools-binding'&&saved.status==='PASS'&&saved.sourceApprovalGranted===false&&saved.executionPerformed===false,'BATTERY_UI_VERIFIED_NODE_BINDING_REQUIRED');
 need(key(saved.snapshotRoot)===key(repo)&&key(saved.bindingManifestFile)===key(bindingManifestFile)&&key(saved.snapshotManifestFile)===key(manifestFile),'BATTERY_UI_NODE_BINDING_ROOT_MISMATCH');
 need(sha(saved.expectedSnapshotManifestSha256)&&hash(manifestFile)===saved.expectedSnapshotManifestSha256,'BATTERY_UI_NODE_SOURCE_MANIFEST_CHANGED');
 const manifest=read(manifestFile);
 need(manifest.kind==='ymcc-fixed-source-snapshot'&&key(manifest.snapshotRoot)===key(repo)&&key(manifest.sourceRoot)===key(saved.sourceRoot),'BATTERY_UI_NODE_SOURCE_ROOT_MISMATCH');
 if(saved.allowHistoricalSnapshot===true){
  for(const proof of [saved.captureVerification,saved.captureVerificationBefore,saved.wholeCaptureVerification])need(proof?.status==='FROZEN_SOURCE_VERIFIED'&&proof.snapshotIntegrityVerified===true&&proof.sourceApprovalGranted===false&&proof.manifestSha256===saved.expectedSnapshotManifestSha256&&key(proof.snapshotRoot)===key(repo),'BATTERY_UI_NODE_CAPTURE_PROOF_REQUIRED');
  need(saved.verifierSource&&sha(saved.verifierCodeSha256)&&saved.verifierSource.sha256===saved.verifierCodeSha256&&hash(saved.verifierSource.path)===saved.verifierCodeSha256,'BATTERY_UI_NODE_VERIFIER_CHANGED');
 }
 need(saved.graphBeforeSha256===saved.graphAfterSha256&&sha(saved.graphBeforeSha256)&&digest(JSON.stringify(saved.graph))===saved.graphBeforeSha256,'BATTERY_UI_NODE_GRAPH_DIGEST_CHANGED');
 const actual=captureSnapshotNodeToolGraph({primaryRepo:saved.sourceRoot});
 need(actual.sha256===saved.graphBeforeSha256&&JSON.stringify(actual.graph)===JSON.stringify(saved.graph),'BATTERY_UI_NODE_GRAPH_CHANGED');
 const graph=actual.graph;
 need(key(graph.primaryRepo)===key(saved.sourceRoot),'BATTERY_UI_NODE_GRAPH_ROOT_MISMATCH');
 const files=graph.packages.flatMap(pkg=>pkg.files.map(row=>{
  const file=path.resolve(pkg.root,row.path);need(inside(pkg.root,file)&&inside(graph.nodeModulesRealRoot,file)&&sha(row.sha256),'BATTERY_UI_NODE_GRAPH_PATH_ESCAPE');
  return {path:file,sha256:row.sha256,role:'complete verified canonical snapshot Node package graph'};
 }));
 const pins=new Map(files.map(row=>[key(row.path),row]));need(pins.size===graph.fileCount&&files.length===graph.fileCount,'BATTERY_UI_NODE_GRAPH_MEMBER_COUNT');
 const backend=graph.packages.find(pkg=>pkg.name==='@esbuild/win32-x64');
 need(backend,'BATTERY_UI_BOUND_ESBUILD_BACKEND_REQUIRED');const esbuildBinary=path.join(backend.root,'esbuild.exe');need(pins.has(key(esbuildBinary)),'BATTERY_UI_BOUND_ESBUILD_BACKEND_REQUIRED');
 const metadata=[{path:bindingManifestFile,sha256:expectedBindingSha256},{path:manifestFile,sha256:saved.expectedSnapshotManifestSha256},graph.primaryPackageJson,...(saved.verifierSource?[saved.verifierSource]:[])].map(row=>({...row,role:'explicit snapshot Node tool authority; no product source approval'}));
 need(hash(bindingManifestFile)===expectedBindingSha256,'BATTERY_UI_NODE_BINDING_HASH_CHANGED');
 return {files,metadata,packageRepo:graph.primaryRepo,nodeModulesPath:graph.nodeModulesPath,nodeModulesRealRoot:graph.nodeModulesRealRoot,esbuildBinary,proof:{bindingManifestFile:path.resolve(bindingManifestFile),expectedBindingSha256,snapshotRoot:repo,graphSha256:actual.sha256,packageCount:graph.packageCount,fileCount:graph.fileCount,sourceApprovalGranted:false}};
}
export function mapBoundNodeRows(specFiles,authority){
 const pins=new Map(authority.files.map(row=>[key(row.path),row]));
 return specFiles.map(row=>{
  if(!row.repoRelative)return {...row,path:path.resolve(row.path)};
  need(row.repoRelative.startsWith('node_modules/')&&!path.isAbsolute(row.repoRelative),'BATTERY_UI_NODE_RELATIVE_PATH_REQUIRED');
  const suffix=row.repoRelative.slice('node_modules/'.length);
  need(suffix.length>0&&!suffix.split(/[\\/]/).includes('..')&&!path.isAbsolute(suffix)&&!/[\0\r\n]/.test(suffix),'BATTERY_UI_NODE_RELATIVE_PATH_REQUIRED');
  const canonicalRoot=authority.nodeModulesRealRoot??path.join(authority.packageRepo,'node_modules');
  const file=path.resolve(canonicalRoot,suffix);
  need(inside(canonicalRoot,file)&&key(file)!==key(canonicalRoot),'BATTERY_UI_NODE_GRAPH_PATH_ESCAPE');
  const bound=pins.get(key(file));
  need(bound&&bound.sha256===row.sha256,'BATTERY_UI_NODE_TOOL_NOT_IN_BOUND_GRAPH:'+row.repoRelative);
  return {...row,path:file};
 });
}
export function nodeBuildConfiguration(tool,repo){
 return {packageRepo:tool.nodePackageRepo??repo,nodeModulesPath:tool.nodeModulesPath??path.join(repo,'node_modules'),...(tool.nodeToolBinding?{nodeToolBinding:tool.nodeToolBinding,esbuildBinary:tool.esbuildBinary}:{})};
}
export function validateBoundNodeDeclaration(rows,authority){
 const pins=new Map(rows.map(row=>[key(row.path),row.sha256]));
 for(const row of [...authority.files,...authority.metadata])need(pins.get(key(row.path))===row.sha256,'BATTERY_UI_BOUND_NODE_MEMBER_UNDECLARED:'+row.path);
}
