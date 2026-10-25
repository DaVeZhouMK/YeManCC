import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import contract from './lists.v1.json' with {type:'json'};

export const reviewedRegistryLists=contract;
const defaultRepo=fileURLToPath(new URL('../../../../',import.meta.url));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const hash=file=>digest(fs.readFileSync(file));
const key=file=>path.resolve(file).toLowerCase();
const inside=(base,file)=>{const r=path.relative(base,file);return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
function token(file,{repoRoot,primaryRoot,nodeRoot}){
 assert.equal(typeof file,'string','MAINTENANCE_ABSOLUTE_TOOL_PATH');
 assert(path.isAbsolute(file),'MAINTENANCE_ABSOLUTE_TOOL_PATH');
 const abs=path.resolve(file);
 for(const [prefix,base]of [['$NODE',nodeRoot],['$REPO',repoRoot],['$PRIMARY',primaryRoot]]){
  if(base&&inside(path.resolve(base),abs))return prefix+'/'+path.relative(path.resolve(base),abs).replaceAll('\\','/');
 }
 return abs.replaceAll('\\','/');
}
function exact(actual,expected,count,label,{fold=false}={}){
 assert(Array.isArray(actual),label+'_ARRAY');
 assert.equal(expected.length,count,label+'_CONTRACT_COUNT');
 assert.equal(new Set(expected.map(p=>p.toLowerCase())).size,count,label+'_CONTRACT_UNIQUE');
 assert.equal(actual.length,count,label+'_COUNT');
 assert.equal(new Set(actual.map(p=>p.toLowerCase())).size,count,label+'_UNIQUE');
 const normalize=p=>fold?p.toLowerCase():p;
 assert.deepEqual(actual.map(normalize).sort(),expected.map(normalize).sort(),label+'_EXACT_LIST');
}
function snapshotContext(repoRoot,options,reviewed){
 const manifestFile=path.join(repoRoot,'.ymcc-source-snapshot/manifest.json');
 const bindingFile=options.bindingManifestFile??process.env.YMCC_BATTERY_UI_NODE_TOOLS_BINDING;
 const expectedSha=options.expectedBindingSha256??process.env.YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256;
 assert(bindingFile&&path.isAbsolute(bindingFile),'MAINTENANCE_EXPLICIT_BINDING');
 assert.match(expectedSha??'',/^[a-f0-9]{64}$/,'MAINTENANCE_EXPLICIT_BINDING_SHA');
 assert.equal(key(bindingFile),key(path.join(repoRoot,'.ymcc-source-snapshot/node-tools-binding.json')),'MAINTENANCE_BINDING_ROOT');
 assert.equal(hash(bindingFile),expectedSha,'MAINTENANCE_BINDING_IDENTITY');
 const saved=read(bindingFile),manifest=read(manifestFile);
 assert.equal(saved.kind,'ymcc-snapshot-node-tools-binding','MAINTENANCE_BINDING_KIND');
 assert.equal(saved.schemaVersion,1);assert.equal(saved.status,'PASS');
 assert.equal(saved.sourceApprovalGranted,false);assert.equal(saved.executionPerformed,false);
 assert.equal(key(saved.snapshotRoot),key(repoRoot),'MAINTENANCE_SNAPSHOT_ROOT');
 assert.equal(key(saved.bindingManifestFile),key(bindingFile),'MAINTENANCE_BINDING_ROOT');
 assert.equal(key(saved.snapshotManifestFile),key(manifestFile),'MAINTENANCE_MANIFEST_ROOT');
 assert.equal(hash(manifestFile),saved.expectedSnapshotManifestSha256,'MAINTENANCE_MANIFEST_IDENTITY');
 assert.equal(manifest.kind,'ymcc-fixed-source-snapshot');
 assert.equal(key(manifest.snapshotRoot),key(repoRoot));assert.equal(key(manifest.sourceRoot),key(saved.sourceRoot));
 assert.equal(saved.graphBeforeSha256,saved.graphAfterSha256,'MAINTENANCE_GRAPH_IDENTITY');
 assert.equal(digest(JSON.stringify(saved.graph)),saved.graphBeforeSha256,'MAINTENANCE_GRAPH_IDENTITY');
 assert.equal(key(saved.graph.primaryRepo),key(saved.sourceRoot),'MAINTENANCE_GRAPH_PRIMARY_ROOT');
 const variant=reviewed.snapshotB40NodeGraph?.sourceGraphSha256===saved.graphBeforeSha256?{profile:'snapshot-b40',nodeGraph:reviewed.snapshotB40NodeGraph,verifierRequired:false}:{profile:'snapshot',nodeGraph:reviewed.nodeGraph,verifierRequired:true};
 const context={repoRoot,primaryRoot:saved.sourceRoot,nodeRoot:saved.graph.nodeModulesRealRoot,profile:variant.profile};
 const graph=JSON.parse(JSON.stringify(saved.graph,(_k,v)=>typeof v==='string'&&path.isAbsolute(v)?token(v,context):v));
 assert.equal(graph.packageCount,variant.nodeGraph.packageCount,'MAINTENANCE_GRAPH_PACKAGE_COUNT');
 assert.equal(graph.fileCount,variant.nodeGraph.fileCount,'MAINTENANCE_GRAPH_FILE_COUNT');
 assert.deepEqual(graph,variant.nodeGraph.graph,'MAINTENANCE_FULL_NODE_GRAPH');
 assert.equal(saved.graphBeforeSha256,variant.nodeGraph.sourceGraphSha256,'MAINTENANCE_DECLARED_GRAPH_SHA');
 if(variant.verifierRequired){assert(saved.verifierSource,'MAINTENANCE_VERIFIER_REQUIRED');
 assert.equal(token(saved.verifierSource.path,context),'$PRIMARY/tools/ymcc_sim/global/source-snapshot.mjs','MAINTENANCE_VERIFIER_PROVENANCE');
 assert.equal(saved.verifierSource.sha256,saved.verifierCodeSha256);
 assert.equal(hash(saved.verifierSource.path),saved.verifierCodeSha256,'MAINTENANCE_VERIFIER_IDENTITY');
 }else assert.equal(saved.verifierSource,undefined,'MAINTENANCE_UNREVIEWED_VERIFIER');
 return context;
}
/** Expected membership comes only from the reviewed versioned data, never a factory return value. */
export function assertReviewedRegistryRow(row,options={}){
 const reviewed=contract.consumers[row.id];assert(reviewed,'MAINTENANCE_REVIEWED_ID');
 const repoRoot=path.resolve(options.repoRoot??defaultRepo);
 exact(row.evidenceInputs,reviewed.sources,reviewed.sourceCount,'MAINTENANCE_SOURCES');
 for(const source of row.evidenceInputs)assert(!path.isAbsolute(source)&&source===source.replaceAll('\\','/')&&!source.split('/').some(p=>p==='..'||p==='.'||p===''),'MAINTENANCE_SOURCE_PROVENANCE');
 let profile='primary',context={repoRoot};
 if(reviewed.profiles&&(fs.existsSync(path.join(repoRoot,'.ymcc-source-snapshot/manifest.json'))||fs.existsSync(path.join(repoRoot,'.ymcc-source-snapshot/node-tools-binding.json')))){
  context=snapshotContext(repoRoot,options,reviewed);profile=context.profile;
 }
 const expected=reviewed.profiles?.[profile]??reviewed;
 const normalized=row.artifactInputs.map(file=>token(file,context));
 exact(normalized,expected.tools,expected.toolCount,'MAINTENANCE_TOOLS',{fold:true});
 return {revision:contract.revision,profile,sourceCount:reviewed.sourceCount,toolCount:expected.toolCount};
}
