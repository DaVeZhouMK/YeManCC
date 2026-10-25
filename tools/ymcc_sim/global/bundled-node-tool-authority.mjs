/** Complete, precompile Node tool authority; product sourcePath remains unchanged. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {captureSnapshotNodeToolGraph} from './snapshot-node-tools.mjs';
import {resolveSnapshotNodeTools} from '../../fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs';
export const BUNDLED_TOOL_SCHEMA=1;
export const BUNDLED_AUTHORITY_SOURCES=Object.freeze([
 'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/bundled-node-tool-authority.mjs',
 'tools/ymcc_sim/global/snapshot-node-tools.mjs','tools/ymcc_sim/global/source-snapshot.mjs',
 'tools/fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs','tools/ymcc_sim/global/execution-completion/index.mjs',
]);
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex'),hash=file=>sha(fs.readFileSync(file));
const key=file=>path.resolve(file).toLowerCase();
const need=(ok,code)=>{if(!ok)throw Error('BUNDLED_NODE_'+code);};
const inside=(root,file)=>{const r=path.relative(root,file);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const hasNodeModules=file=>file.split(/[\\/]/).some(part=>part.toLowerCase()==='node_modules');
function unique(rows){const result=new Map();for(const row of rows){const previous=result.get(key(row.path));need(!previous||previous.sha256===row.sha256,'DUPLICATE_IDENTITY_CONFLICT');result.set(key(row.path),{path:path.resolve(row.path),sha256:row.sha256,status:'OK'});}return [...result.values()].sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);}
export function lockBundledNodeTools(repoRoot,{env=process.env,binding,limit=3072}={}){
 const repo=fs.realpathSync(repoRoot),snapshot=fs.existsSync(path.join(repo,'.ymcc-source-snapshot/manifest.json'))||fs.existsSync(path.join(repo,'.ymcc-source-snapshot/node-tools-binding.json'));
 let bound,graph,graphSha256,metadata,files,proof;
 if(snapshot){
  bound=resolveSnapshotNodeTools(repo,binding??{bindingManifestFile:env.YMCC_BATTERY_UI_NODE_TOOLS_BINDING,expectedBindingSha256:env.YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256});
  const saved=JSON.parse(fs.readFileSync(bound.proof.bindingManifestFile,'utf8'));graph=saved.graph;graphSha256=bound.proof.graphSha256;files=bound.files;metadata=bound.metadata;proof=bound.proof;
 }else{
  need(!binding&&!env.YMCC_BATTERY_UI_NODE_TOOLS_BINDING&&!env.YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256,'PRIMARY_CANNOT_FORWARD_SNAPSHOT_BINDING');
  const captured=captureSnapshotNodeToolGraph({primaryRepo:repo});graph=captured.graph;graphSha256=captured.sha256;
  files=graph.packages.flatMap(pkg=>pkg.files.map(row=>({path:fs.realpathSync(path.join(pkg.root,row.path)),sha256:row.sha256})));
  metadata=[graph.primaryPackageJson];proof={graphSha256,packageCount:graph.packageCount,fileCount:graph.fileCount,sourceApprovalGranted:false};
 }
 const req=createRequire(path.join(repo,'package.json')),entry=fs.realpathSync(req.resolve('esbuild')),pkg=graph.packages.find(row=>row.name==='esbuild'),backend=graph.packages.find(row=>row.name==='@esbuild/win32-x64');
 need(pkg&&backend&&inside(pkg.root,entry),'COMPILER_NOT_IN_GRAPH');
 const compilerBinary=fs.realpathSync(path.join(backend.root,'esbuild.exe'));
 const node=fs.realpathSync(process.execPath),tools=unique([...files,...metadata,{path:node,sha256:hash(node)}]);
 need(tools.length<=limit,'ARTIFACT_BUDGET_EXCEEDED');
 const pins=new Map(tools.map(row=>[key(row.path),row.sha256]));need(pins.has(key(entry))&&pins.has(key(compilerBinary)),'COMPILER_NOT_PINNED');
 need(!env.ESBUILD_BINARY_PATH||key(fs.realpathSync(env.ESBUILD_BINARY_PATH))===key(compilerBinary),'COMPILER_OVERRIDE_NOT_BOUND');
 need(!process.env.ESBUILD_BINARY_PATH||key(fs.realpathSync(process.env.ESBUILD_BINARY_PATH))===key(compilerBinary),'COMPILER_PROCESS_OVERRIDE_NOT_BOUND');
 for(const row of tools)need(hash(row.path)===row.sha256,'TOOL_CHANGED_BEFORE_COMPILE');
 // A snapshot alias must resolve to the same bound compiler, not another checkout.
 need(key(entry)===key(path.join(pkg.root,'lib/main.js')),'COMPILER_FORWARDING_REJECTED');
 return {schemaVersion:BUNDLED_TOOL_SCHEMA,mode:snapshot?'snapshot-binding':'primary-captured-graph',repoRoot:repo,graphSha256,packageCount:graph.packageCount,fileCount:graph.fileCount,nodeModulesRealRoot:graph.nodeModulesRealRoot,nodeExecutable:node,compilerEntry:entry,compilerBinary,compilerVersion:pkg.version,tools,proof,packageCodeExecuted:false,osLoadedModuleGraph:false};
}
export function verifyBundledImplementation(repoRoot){
 const actual=fs.realpathSync(fileURLToPath(import.meta.url)),expected=fs.realpathSync(path.join(repoRoot,'tools/ymcc_sim/global/bundled-node-tool-authority.mjs'));
 need(key(actual)===key(expected),'EXECUTING_HELPER_OUTSIDE_SOURCE_REPO');
 return BUNDLED_AUTHORITY_SOURCES.map(relative=>{const file=fs.realpathSync(path.join(repoRoot,relative));need(inside(fs.realpathSync(repoRoot),file),'IMPLEMENTATION_SOURCE_ESCAPE');return {path:relative,sha256:hash(file),status:'OK'};});
}
export function classifyBundledInput(repoRoot,inputPath,contents,authority){
 const actual=fs.realpathSync(inputPath),digest=sha(contents),pin=authority.tools.find(row=>key(row.path)===key(actual));
 if(pin&&inside(authority.nodeModulesRealRoot,actual)){need(pin.sha256===digest&&hash(actual)===digest,'LOADED_TOOL_CHANGED');return {role:'tool',row:{path:actual,sha256:digest,status:'OK'}};}
 need(!hasNodeModules(inputPath)&&!hasNodeModules(actual),'LOADED_TOOL_NOT_IN_BOUND_GRAPH');
 const root=fs.realpathSync(repoRoot),relative=path.relative(root,actual);need(inside(root,actual)&&relative!==''&&!path.isAbsolute(relative),'PRODUCT_SOURCE_OUTSIDE_REPOSITORY');
 return {role:'source',row:{path:relative.replaceAll('\\','/'),sha256:digest,status:'OK'}};
}
export function bundledArtifacts(authority,adapterArtifacts=[],outputs=[],limit=3072){
 // Existing explicit non-Node payload declarations keep their existing role.
 for(const row of adapterArtifacts)if(hasNodeModules(row.path))need(authority.tools.some(tool=>key(tool.path)===key(row.path)&&tool.sha256===row.sha256),'EXTRA_NODE_TOOL_NOT_IN_GRAPH');
 const rows=unique([...authority.tools,...adapterArtifacts,...outputs]);need(rows.length<=limit,'ARTIFACT_BUDGET_EXCEEDED');return rows;
}
export function sameArtifactSet(actual,expected){
 need(Array.isArray(actual)&&actual.length===expected.length,'TOOL_ARTIFACT_CLOSURE_MISSING_OR_EXTRA');
 const keys=new Set();for(const row of actual){need(row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256??'')&&row.status==='OK'&&!keys.has(key(row.path)),'TOOL_ARTIFACT_ROW_INVALID_OR_DUPLICATE');keys.add(key(row.path));}
 need(JSON.stringify(unique(actual))===JSON.stringify(unique(expected)),'TOOL_ARTIFACT_CLOSURE_CHANGED');
}
/** Rehash the complete original scope; reusable by nested compilers without bundle-path rules. */
export function verifyBundledToolScope(saved,repoRoot,{limit=3072}={}){
 need(saved?.schemaVersion===BUNDLED_TOOL_SCHEMA,'AUTHORITY_REQUIRED_RERUN');
 need(key(saved.repoRoot)===key(repoRoot),'RECEIPT_REPO_FORWARDING_REJECTED');
 const binding=saved.mode==='snapshot-binding'?{bindingManifestFile:saved.proof?.bindingManifestFile,expectedBindingSha256:saved.proof?.expectedBindingSha256}:undefined;
 const actual=lockBundledNodeTools(repoRoot,{env:{},binding,limit});
 need(JSON.stringify(saved)===JSON.stringify(actual),'AUTHORITY_IDENTITY_CHANGED');
 return actual;
}
export function verifyBundledNodeReceipt(item,repoRoot,caseDir,{limit=3072}={}){
 const actual=verifyBundledToolScope(item.bundledToolAuthority,repoRoot,{limit});
 const ownRoot=fs.realpathSync(caseDir),outputs=['entry.mjs','entry.mjs.map'].map(name=>{const file=path.resolve(caseDir,name),real=fs.realpathSync(file);need(inside(ownRoot,real)&&key(file)===key(real),'BUNDLE_OUTPUT_FORWARDING_REJECTED');return {path:file,sha256:hash(file),status:'OK'};});
 need(item.bundle?.path===outputs[0].path&&item.bundle.sha256===outputs[0].sha256&&item.bundle.esbuildVersion===actual.compilerVersion,'BUNDLE_OUTPUT_CHANGED');
 sameArtifactSet(item.bundledOutputs,outputs);
 need(Array.isArray(item.explicitAdapterArtifacts)&&item.explicitAdapterArtifacts.length<=limit,'EXPLICIT_ADAPTER_ARTIFACT_SCHEMA');
 const expected=bundledArtifacts(actual,item.explicitAdapterArtifacts,outputs,limit);sameArtifactSet(item.candidateArtifacts,expected);
 for(const row of item.candidateArtifacts)need(hash(row.path)===row.sha256,'TOOL_OR_BUNDLE_CHANGED');
 need(Array.isArray(item.toolDependencyInputs)&&item.toolDependencyInputs.every(row=>row.status==='OK'&&inside(actual.nodeModulesRealRoot,row.path)&&actual.tools.some(tool=>tool.path===row.path&&tool.sha256===row.sha256)),'OBSERVED_TOOL_NOT_IN_GRAPH');
 need(new Set(item.toolDependencyInputs.map(row=>key(row.path))).size===item.toolDependencyInputs.length,'OBSERVED_TOOL_DUPLICATE');
 for(const relative of BUNDLED_AUTHORITY_SOURCES){const row=item.sourceInputs?.find(row=>row.path===relative);need(row?.sha256===hash(path.join(repoRoot,relative)),'IMPLEMENTATION_SOURCE_NOT_RECORDED');}
 return {schemaVersion:BUNDLED_TOOL_SCHEMA,mode:actual.mode,graphSha256:actual.graphSha256,packageCount:actual.packageCount,fileCount:actual.fileCount,artifactCount:expected.length,toolAuthorityVerified:true};
}
