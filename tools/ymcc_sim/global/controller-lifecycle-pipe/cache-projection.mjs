// Narrow source-equivalence contract for the original SDK-free wire adapter.
// Full source hashes stay in manifests; only the identity digest substitutes
// the complete positive and both original negative generated translation units.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {generateWireInput,wireSourceProjection,mutateWireSource,WIRE_FAULTS} from './source-projection.mjs';
export const WIRE_PROJECTION_CONTRACT='wire-native-main-v1';
export const WIRE_PROJECTION_MODULE='tools/ymcc_sim/global/controller-lifecycle-pipe/cache-projection.mjs';
export const WIRE_MAIN='native/main.cpp';
export const WIRE_TEMPLATE='tools/fixtures/input_lifecycle_integration/NativeEntry.cpp';
export const WIRE_PROJECTION_INPUTS=Object.freeze([
 WIRE_MAIN,WIRE_TEMPLATE,WIRE_PROJECTION_MODULE,
 'tools/ymcc_sim/global/controller-lifecycle-pipe/source-projection.mjs',
 'tools/ymcc_sim/global/controller-lifecycle-pipe/run.mjs',
 'tools/input_lifecycle_pipe_integration.mjs',
 'tools/fixtures/legion_motion_consumer/extract_main.mjs',
 'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs'
]);
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
const need=(ok,message)=>{if(!ok)throw Error('WIRE_PROJECTION_'+message);};
const inside=(root,file)=>{const r=path.relative(root,file);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
function enabled(manifest){
 const a=manifest?.adapter;
 if(a?.sourceProjectionContract===undefined){need(manifest?.sourceProjection===undefined,'UNDECLARED_PROJECTION');return false;}
 need(a.sourceProjectionContract===WIRE_PROJECTION_CONTRACT&&a.kind==='node'&&a.path==='tools/ymcc_sim/global/controller-lifecycle-pipe/run.mjs'&&JSON.stringify(a.args)===JSON.stringify(['--evidence-dir','{outDir}/lifecycle-pipe'])&&a.cacheOutputManifest==='lifecycle-pipe/result.json','DESCRIPTOR_CHANGED');
 return true;
}
function boundFile(manifest,relative){
 const file=path.resolve(manifest.repoRoot,relative),real=fs.realpathSync(file);
 need(inside(manifest.repoRoot,file)&&inside(manifest.repoRoot,real),'SOURCE_OUTSIDE_ROOT');
 const rows=manifest.inputs.filter(row=>row.path===file);
 need(rows.length>0&&rows.every(row=>row.realPath===real),'DECLARED_SOURCE_MISSING:'+relative);
 const bytes=fs.readFileSync(real);need(bytes.length<=16*1024*1024,'SOURCE_SIZE');
 const digest=sha(bytes);need(rows.every(row=>row.sha256===digest),'SOURCE_CHANGED:'+relative);
 return bytes;
}
export function wireProjectionForManifest(manifest){
 if(!enabled(manifest))return null;
 need(Array.isArray(manifest.inputs),'INPUTS_MISSING');
 // All executed extraction, mutation and template implementations remain full
 // inputs. The generic engine still binds every other source/tool/artifact.
 const values=new Map(WIRE_PROJECTION_INPUTS.map(relative=>[relative,boundFile(manifest,relative)]));
 const generated=wireSourceProjection(values.get(WIRE_MAIN).toString('utf8'),values.get(WIRE_TEMPLATE).toString('utf8'));
 return {schemaVersion:1,kind:WIRE_PROJECTION_CONTRACT,source:WIRE_MAIN,template:WIRE_TEMPLATE,generated};
}
export function wireIdentityManifest(manifest){
 if(!enabled(manifest))return manifest;
 const projection=manifest.sourceProjection;
 need(projection?.kind===WIRE_PROJECTION_CONTRACT&&projection.source===WIRE_MAIN&&projection.template===WIRE_TEMPLATE&&projection.generated?.variants?.length===3,'MANIFEST_PROJECTION_MISSING');
 const file=path.resolve(manifest.repoRoot,WIRE_MAIN),projectedSha=sha(JSON.stringify(projection));
 need(manifest.inputs.some(row=>row.path===file),'MAIN_INPUT_MISSING');
 return {...manifest,inputs:manifest.inputs.map(row=>row.path===file?{...row,sha256:projectedSha}:row)};
}
export function verifyWireProjectionReference({manifest,graph,caseDir,requirePin}){
 if(!enabled(manifest))return null;
 const root=path.resolve(caseDir,'lifecycle-pipe');
 need(graph?.id==='controller.lifecycle-native-managed-pipe'&&graph.status==='PASS'&&graph.passed===true,'BASELINE_NOT_PASS');
 const readPinned=(file,expected)=>{
  need(inside(caseDir,file),'EVIDENCE_OUTSIDE_CASE');
  const pin=expected===undefined?requirePin(file):requirePin(file,expected);
  const bytes=fs.readFileSync(file);need(bytes.length<=16*1024*1024&&sha(bytes)===pin.sha256,'EVIDENCE_CHANGED');return bytes;
 };
 const proofPath=path.join(root,'source-projection.json');
 need(graph.sourceProjection?.path===proofPath&&typeof graph.sourceProjection.sha256==='string','PROOF_MISSING');
 const proof=JSON.parse(readPinned(proofPath,graph.sourceProjection.sha256).toString('utf8'));
 need(proof.schemaVersion===1&&proof.kind==='wire-native-main-proof-v1'&&proof.source?.path===WIRE_MAIN,'PROOF_CONTRACT');
 const snapshot=path.join(root,'source-main.cpp');
 need(proof.snapshot?.path===snapshot&&proof.snapshot.sha256===proof.source.sha256,'SNAPSHOT_MAPPING');
 const mainBytes=readPinned(snapshot,proof.snapshot.sha256),main=mainBytes.toString('utf8');
 const template=boundFile(manifest,WIRE_TEMPLATE);
 need(proof.template?.path===WIRE_TEMPLATE&&proof.template.sha256===sha(template),'TEMPLATE_MAPPING');
 const projection=wireSourceProjection(main,template.toString('utf8'));
 need(JSON.stringify(projection)===JSON.stringify(manifest.sourceProjection.generated)&&JSON.stringify(proof.generated)===JSON.stringify(projection),'GENERATED_INPUT_CHANGED');
 need(graph.sourceHashes.some(row=>path.resolve(manifest.repoRoot,row.path)===path.resolve(manifest.repoRoot,WIRE_MAIN)&&row.sha256===proof.source.sha256),'BASELINE_MAIN_MAPPING_MISSING');
 need(Array.isArray(graph.negative)&&graph.negative.length===2,'NEGATIVE_SET_CHANGED');
 const variants=[{id:'positive',source:snapshot,dir:'p',expected:null,raw:graph.positive},...WIRE_FAULTS.map((fault,i)=>{
  const matches=graph.negative.filter(row=>row.id===fault.id);need(matches.length===1,'NEGATIVE_MISSING_OR_DUPLICATE');const row=matches[0];
  need(row.before===fault.before&&row.after===fault.after&&row.originalAssertionsUnchanged===true,'NEGATIVE_MUTATION_CHANGED');
  return {id:fault.id,source:path.join(root,fault.id+'.cpp'),dir:'n'+i,expected:fault.expected,raw:row.proof};
 })];
 for(const variant of variants){
  const source=variant.expected?mutateWireSource(main,variant.id):main;
  const generated=generateWireInput(source,template.toString('utf8'));
  need(sha(readPinned(variant.source))===sha(source),'VARIANT_SOURCE_CHANGED');
  const directory=path.join(root,variant.dir),cpp=path.join(directory,'source/fixture.cpp');
  need(sha(readPinned(cpp))===sha(generated.cpp),'COMPILED_INPUT_MAPPING');
  const receiptFile=path.join(directory,'RESULT.json');need(variant.raw?.path===receiptFile,'RESULT_MAPPING');
  const receipt=JSON.parse(readPinned(receiptFile,variant.raw.sha256).toString('utf8'));
  need(receipt.status==='PASS'&&receipt.passed===true&&receipt.sourceUnchanged===true&&receipt.expectedSemanticRejection===variant.expected,'RESULT_STATUS_MAPPING');
  need(receipt.sources?.some(row=>row.path===variant.source&&row.sha256===sha(source)),'RESULT_SOURCE_MAPPING');
  const bindings=JSON.parse(readPinned(path.join(directory,'source-bindings.json')).toString('utf8'));
  need(bindings.sources?.some(row=>row.path===variant.source&&row.sha256===sha(source))&&JSON.stringify(bindings.bindings)===JSON.stringify(generated.bindings),'EXTRACTION_BINDINGS_MAPPING');
  need(receipt.configurations?.length===2&&new Set(receipt.configurations.map(row=>row.mode)).size===2,'PROFILE_SET');
  for(const configuration of receipt.configurations){
   need(['Release','Checked'].includes(configuration.mode)&&configuration.status===(variant.expected?'EXPECTED_SEMANTIC_REJECTION':'PASS'),'PROFILE_STATUS');
   readPinned(path.join(directory,configuration.mode,'fixture.exe'),configuration.executableSha256);
  }
 }
 return new Map([[path.resolve(manifest.repoRoot,WIRE_MAIN),{sha256:proof.source.sha256}]]);
}
