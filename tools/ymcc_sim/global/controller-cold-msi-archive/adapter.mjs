import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {sourceInputs as nativeSources,artifactInputs as nativeArtifacts} from '../controller-msi-late-hide-pending/adapter.mjs';

export const MODULE='tools/ymcc_sim/global/controller-cold-msi-archive';
export const ID='controller.cold-msi-archive';
export const SUITE='cold-msi-archive';
export const MUTATION='rollback-cold-observed-id';
export const SENTINEL='MSI opaque OS ID permits durable two-duty cleanup before saved ON creation';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const MANAGED_DLL='G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/integration/managed/env-return200-r4/bin/Release/net10.0/Fixture.dll';
export const MANAGED_MANIFEST=path.join(path.dirname(MANAGED_DLL),'fixture-manifest.json');
const ownFiles=['adapter.mjs','run.mjs','README.md','field-record.json','provenance.json','toolchain-inputs.json'];
const supportFiles=['tools/virtual_controller_runtime_source.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/input_coordinator_lifecycle/compiler_inputs.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs'];
export const key=file=>path.resolve(file).toLowerCase();
export const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const fact=file=>{const bytes=fs.readFileSync(file);return {path:path.resolve(file),sha256:sha(bytes),bytes:bytes.length};};
function demand(ok,message){if(!ok)throw Error(message);}
function bounded(rows,max,label,min=1){demand(Array.isArray(rows)&&rows.length>=min&&rows.length<=max,'INVALID_BOUNDED_'+label);return rows;}

export function sourceInputs(repo=REPO){
 const files=new Set([...nativeSources(repo),...supportFiles,...ownFiles.map(name=>MODULE+'/'+name)]);
 // Follow only literal relative module imports, bounded to the modules already named
 // by this adapter/harness. The global registry is an input, not a traversal root.
 const pending=[MODULE+'/adapter.mjs',MODULE+'/run.mjs','tools/input_coordinator_lifecycle_selftest.mjs',...supportFiles];
 const followed=new Set();
 while(pending.length){
  const relative=pending.pop();if(followed.has(relative))continue;
  demand(followed.size<128,'RELATIVE_IMPORT_CLOSURE_LIMIT');followed.add(relative);files.add(relative);
  const text=fs.readFileSync(path.join(repo,relative),'utf8');
  for(const match of text.matchAll(/\b(?:from\s*|import\s*\(\s*|import\s*)['"](\.[^'"\r\n]+)['"]/g)){
   const imported=path.resolve(repo,path.dirname(relative),match[1]),rel=path.relative(repo,imported).replaceAll('\\','/');
   demand(!rel.startsWith('../')&&!path.isAbsolute(rel),'IMPORT_OUTSIDE_REPOSITORY:'+rel);
   demand(fs.statSync(imported).isFile(),'IMPORT_NOT_FILE:'+rel);files.add(rel);
   if(rel.endsWith('.mjs'))pending.push(rel);
  }
 }
 return [...files].sort();
}

// Every original, saved source, generated input, evaluated build input, runtime
// artifact and declared tool gets its own file binding. No directory enumeration.
export function inputPlan(repo=REPO){
 const spec=read(path.join(repo,MODULE,'toolchain-inputs.json'));
 const provenance=read(path.join(repo,MODULE,'provenance.json'));
 demand(spec.schemaVersion===1&&key(spec.managedDll)===key(MANAGED_DLL),'FIXED_MANAGED_DLL_REQUIRED');
 const expected=new Map();
 const bind=(file,digest,label)=>{
  demand(typeof file==='string'&&path.isAbsolute(file)&&/^[a-f0-9]{64}$/.test(digest??''),'INVALID_FILE_BINDING:'+label);
  const prior=expected.get(key(file));demand(!prior||prior.sha256===digest,'CONFLICTING_FILE_BINDING:'+file);
  expected.set(key(file),{path:path.resolve(file),sha256:digest});
 };
 bind(MANAGED_MANIFEST,spec.managedManifestSha256,'managed manifest');
 const manifest=read(MANAGED_MANIFEST),b=manifest.inputBindings;
 demand(manifest.status==='BUILT'&&manifest.semanticMutation===null&&manifest.realSdkLoaded===false&&manifest.hardwareIo===false&&manifest.normalHostMainExecuted===false&&manifest.sourceStable===true&&manifest.toolchainStable===true&&key(manifest.fixtureDll)===key(MANAGED_DLL),'UNSAFE_OR_UNBOUND_MANAGED_FIXTURE');
 for(const row of bounded(b?.sources,64,'MANAGED_SOURCES')){
  bind(row.path,row.sha256,'managed source');bind(row.snapshot,row.sha256,'managed source snapshot');
 }
 for(const [label,rows,limit]of [['GENERATED',b.generatedInputs,64],['RUNTIME',manifest.runtimeArtifacts,32],['TOOLS',b.toolchainAllowlist,1024],['EVALUATED',manifest.evaluatedCompilerInputs,512]])
  for(const row of bounded(rows,limit,label)){bind(row.path,row.sha256,label);if(row.snapshot)bind(row.snapshot,row.sha256,label+' snapshot');}
 demand(expected.has(key(MANAGED_DLL))&&expected.has(key(manifest.fixtureProject)),'MANAGED_PROJECT_OR_RUNTIME_UNBOUND');
 for(const row of bounded(spec.nativeArtifactBindings,1600,'NATIVE_ARTIFACTS')){
  // Repo-local payloads follow an immutable source snapshot; installed compilers
  // and the separately pinned managed fixture retain their absolute paths.
  const relative=path.relative(spec.originalRepositoryRoot,row.path);
  const local=relative!==''&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative);
  bind(local?path.resolve(repo,relative):row.path,row.sha256,'native artifact');
 }
 for(const file of nativeArtifacts(repo))demand(expected.has(key(file)),'INHERITED_NATIVE_ARTIFACT_UNBOUND:'+file);
 demand(key(spec.node)===key(process.execPath),'NODE_TOOL_PATH_CHANGED');
 demand(provenance.archive.sha256==='3efa8b18687158f6d4dcb9611197a2835f2cc050b608043fc47bb4e0ab5c5480'&&provenance.fieldRecord.bytes===1287&&provenance.fieldRecord.sha256==='e2390d9db29ec41ad5da1e3ff1ebd39edec0bdd9a106d3e76aef42042e36275a'&&provenance.currentOsState===false,'ARCHIVE_PROVENANCE_CHANGED');
 bind(provenance.archive.path,provenance.archive.sha256,'original ZIP');
 bind(provenance.extractionReceipt.path,provenance.extractionReceipt.sha256,'extraction receipt');
 bind(provenance.fieldRecord.selectedPath,provenance.fieldRecord.sha256,'selected field record');
 const field=fact(path.join(repo,MODULE,'field-record.json'));
 demand(field.sha256===provenance.fieldRecord.sha256&&field.bytes===1287,'FIELD_RECORD_NOT_BYTE_EXACT');
 const selected=read(provenance.extractionReceipt.path);
 demand(selected.archiveSha256===provenance.archive.sha256&&selected.selected.some(r=>r.name===provenance.fieldRecord.zipEntry&&r.sha256===field.sha256&&r.bytes===field.bytes),'EXTRACTION_SELECTION_UNBOUND');
 const artifacts=new Map();
 for(const file of [...nativeArtifacts(repo),...expected.values()].map(row=>typeof row==='string'?row:row.path))artifacts.set(key(file),path.resolve(file));
 demand(artifacts.size<=3072,'ARTIFACT_INPUT_LIMIT_EXCEEDED');
 return {spec,provenance,manifest,expected:[...expected.values()],artifacts:[...artifacts.values()].sort()};
}

export function verifyPlan(plan){
 for(const row of plan.expected){let actual;try{actual=fact(row.path);}catch{throw Error('BOUND_INPUT_MISSING:'+row.path);}
  demand(actual.sha256===row.sha256,'BOUND_INPUT_SHA_MISMATCH:'+row.path);
 }
 return plan;
}
export function artifactInputs(repo=REPO){return verifyPlan(inputPlan(repo)).artifacts;}
export function assertPositiveExpectation(spec){
 demand(spec.expectedPositive?.cases===14&&spec.expectedPositive.checks===545,'FINAL_POSITIVE_CHECK_COUNT_NOT_CONFIRMED: cases=14; historical 13/503 is not the final gate');
}
export function getColdMsiArchiveAdapters(repo=REPO){
 let inputs;
 try{const plan=verifyPlan(inputPlan(repo));inputs={evidenceInputs:sourceInputs(repo),artifactInputs:plan.artifacts};assertPositiveExpectation(plan.spec);}
 catch(error){inputs={evidenceInputs:inputs?.evidenceInputs??ownFiles.map(name=>MODULE+'/'+name),artifactInputs:inputs?.artifactInputs??[],blockedReason:'COLD_MSI_ARCHIVE_INPUTS_UNAVAILABLE: '+error.message};}
 return [{id:ID,domain:'controller',title:'G3E archived two-duty MSI cold startup: actual full TU and managed pipe with one compiled ID rollback',kind:'node',path:MODULE+'/run.mjs',args:['--out','{outDir}/cold-msi-archive','--compile-out','{outDir}/../../cold-msi-tu'],profiles:['full'],timeoutMs:900000,scope:'behavioral-mock',...inputs,resultFiles:['cold-msi-archive/RESULT.json'],boundary:'Byte-exact historical G3E target-return record; controlled census, writer, ancestry, visibility and SDK leaves. Existing native cold return/session/queue and real private managed pipe. Exactly two serial compiler invocations, green then rollback-cold-observed-id. Original reports retained. No normal product Main, real device/driver/service, current OS state claim or production edit.'}];
}