import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {lockBundledNodeTools,verifyBundledToolScope,classifyBundledInput,bundledArtifacts,sameArtifactSet,BUNDLED_AUTHORITY_SOURCES} from '../bundled-node-tool-authority.mjs';
export {lockBundledNodeTools,BUNDLED_AUTHORITY_SOURCES};
const need=(ok,code)=>{if(!ok)throw Error('UPDATE_TOOL_'+code);};
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const inside=(root,file)=>{const r=path.relative(root,file);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
export function ownedUpdateOutputs(root){
 root=fs.realpathSync(root);const result=[];
 function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);need(!entry.isSymbolicLink(),'OUTPUT_REPARSE');if(entry.isDirectory())visit(file);else{need(entry.isFile()&&inside(root,fs.realpathSync(file)),'OUTPUT_OUTSIDE_OWN_CASE');if(!['worker-result.json','result.json'].includes(entry.name))result.push({path:file,sha256:hash(file),status:'OK'});}need(result.length<=500,'OUTPUT_LIMIT');}}
 visit(root);return result.sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
}
export function verifyUpdateToolEvidence(worker,repoRoot,evidenceDir,parentAuthority){
 const actual=verifyBundledToolScope(worker?.bundledToolAuthority,repoRoot);
 need(JSON.stringify(actual)===JSON.stringify(parentAuthority),'PARENT_WORKER_AUTHORITY_DIFFERENT');
 const caseRoot=fs.realpathSync(evidenceDir),tools=new Map(actual.tools.map(row=>[key(row.path),row.sha256]));
 need(Array.isArray(worker.compiledToolInputs)&&worker.compiledToolInputs.length<=3072,'OBSERVED_TOOL_SCHEMA');
 const observed=new Set();for(const row of worker.compiledToolInputs){need(row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&row.status==='OK'&&!observed.has(key(row.path)),'OBSERVED_TOOL_DUPLICATE_OR_PATH');observed.add(key(row.path));need(tools.get(key(row.path))===row.sha256&&hash(row.path)===row.sha256,'OBSERVED_TOOL_OUTSIDE_GRAPH_OR_CHANGED');}
 const expectedSources=new Map(),expectedObservedTools=new Map();const outputs=[];need(Array.isArray(worker.bundles)&&worker.bundles.length===11,'ORIGINAL_BUNDLES_INCOMPLETE');
 for(const bundle of worker.bundles){need(typeof bundle.path==='string'&&path.basename(bundle.path)===bundle.path&&bundle.path.endsWith('.mjs'),'BUNDLE_RELATIVE_NAME_REQUIRED');const file=path.resolve(caseRoot,bundle.path),real=fs.realpathSync(file);need(inside(caseRoot,real)&&key(real)===key(file),'BUNDLE_ESCAPE');need(hash(file)===bundle.sha256&&bundle.esbuildVersion===actual.compilerVersion,'BUNDLE_IDENTITY_CHANGED');const metafile=file.slice(0,-4)+'.metafile.json';need(fs.statSync(metafile).size<=8*1024*1024,'METAFILE_LIMIT');const meta=JSON.parse(fs.readFileSync(metafile,'utf8'));need(meta.inputs&&typeof meta.inputs==='object'&&!Array.isArray(meta.inputs)&&Object.keys(meta.inputs).length<=3072,'METAFILE_INPUT_SCHEMA');for(const input of Object.keys(meta.inputs)){if(['fixture:gamePolicyTarget','game-fixture:game-io-only'].includes(input))continue;const source=path.resolve(repoRoot,input),classified=classifyBundledInput(repoRoot,source,fs.readFileSync(source),actual),table=classified.role==='tool'?expectedObservedTools:expectedSources;table.set(classified.row.path,classified.row);}for(const target of [file,metafile])outputs.push({path:target,sha256:hash(target),status:'OK'});}
 sameArtifactSet(worker.compiledToolInputs,[...expectedObservedTools.values()]);const actualSources=worker.compiledInputs.map(row=>({path:row.path,sha256:row.sha256,status:'OK'})).sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0),sourceExpected=[...expectedSources.values()].sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);need(new Set(actualSources.map(row=>row.path)).size===actualSources.length&&JSON.stringify(actualSources)===JSON.stringify(sourceExpected),'METAFILE_SOURCE_CLOSURE_MISSING_EXTRA_OR_CHANGED');const expected=bundledArtifacts(actual,[],outputs);sameArtifactSet(worker.toolArtifacts,expected);
 for(const row of worker.toolArtifacts)need(hash(row.path)===row.sha256,'TOOL_ARTIFACT_CHANGED');
 need(Array.isArray(worker.compiledInputs)&&worker.compiledInputs.every(row=>typeof row.path==='string'&&!path.isAbsolute(row.path)&&!row.path.split(/[\\/]/).some(part=>part==='..'||part.toLowerCase()==='node_modules')),'PRODUCT_SOURCE_CONTAINS_EXTERNAL_TOOL');
 return {schemaVersion:1,status:'PASS',mode:actual.mode,graphSha256:actual.graphSha256,packageCount:actual.packageCount,fileCount:actual.fileCount,observedTools:observed.size,artifactCount:expected.length};
}


