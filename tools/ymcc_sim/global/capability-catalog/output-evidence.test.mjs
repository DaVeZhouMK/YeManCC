import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {capturePaths,sealPaths,evidenceRoot,hash,REPO} from './output-evidence.mjs';
import {getCapabilityCatalogAdapters} from './adapter.mjs';
import {checkStructuredEvidence} from '../runner.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';
const dispatch='fan.unified-capability-dispatch',roundtrip='fan.unified-capability-roundtrip';
const samePath=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
function fixture(t,id=dispatch){
 const workspace=simulationCorePath('Workspace'),parent=fs.realpathSync.native(workspace);
 assert(samePath(parent,workspace)&&!fs.lstatSync(workspace).isSymbolicLink());
 const root=fs.mkdtempSync(path.join(parent,'ymcc-catalog-closure-')),identity=fs.lstatSync(root,{bigint:true});
 t.after(()=>{
  const actual=fs.realpathSync.native(root),current=fs.lstatSync(root,{bigint:true});
  assert(samePath(actual,root)&&samePath(path.dirname(actual),parent));
  assert(current.isDirectory()&&!current.isSymbolicLink()&&current.dev===identity.dev&&current.ino===identity.ino);
  fs.rmSync(actual,{recursive:true,force:true});
 });
 const write=(rel,value)=>{const file=path.join(root,rel);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,value);return file;};
 const input=write('input/source.cs','fixture source before consumer'),artifact=write('input/reference.dll','fixture reference bytes'),inputs={sources:[input],artifacts:[artifact]};capturePaths(id,root,inputs);
 const assemblies=id===dispatch?['YeManFanHost','YeManFanBackend','HcCapabilityCatalogSelfTest','HcCapabilityHotPathSelfTest']:['FanCapabilityRoundtrip','YeManFanBackend'];for(const name of assemblies)for(const suffix of ['.dll','.pdb'])write('build/bin/'+name+'/release_win-x64/'+name+suffix,'fixture generated '+name+suffix);
 const report=id===dispatch?'result.json':'actual-roundtrip-results.json',host=path.join(root,'build/bin/YeManFanHost/release_win-x64/YeManFanHost.dll');
 const raw=id===dispatch?{fixtureOnly:true,status:'PASS',checks:3,failures:[],hostSha256:hash(host),catalogSha256:'a'.repeat(64)}:{fixtureOnly:true,status:'passed',passed:true,checks:3,binaryStatus:'passed-actual-locked-dll',shippingOneXStatus:'passed-actual-shipping-one-x-contract',hardwareAccess:'none'};
 write(report,JSON.stringify(raw,null,2));write('consumer.log','fixture consumer completed');return{root,id,inputs,report,raw,write,host};
}
for(const id of [dispatch,roundtrip])test('seal actual evidence fields and reject consumed generated DLL mutation: '+id,t=>{
 const f=fixture(t,id),file=path.join(f.root,f.report),original=fs.readFileSync(file);
 if(id===roundtrip){
  f.write(f.report,JSON.stringify({...f.raw,shippingOneXStatus:undefined}));
  assert.throws(()=>sealPaths(id,f.root,f.inputs),/ROUNDTRIP_ORIGINAL_NOT_COMPLETE/);
  assert(!fs.existsSync(path.join(f.root,'consumer-original-report.json')));
  f.write(f.report,original);
 }
 const result=sealPaths(id,f.root,f.inputs),closure=result.evidenceClosure;
 assert.deepEqual(fs.readFileSync(closure.originalReport.path),original);assert(result.sourceHashes.length>10);
 for(const sealed of [closure.originalReport,closure.inputSnapshot]){
  assert.deepEqual(result.sourceHashes.find(row=>row.path===sealed.path),sealed);
  assert.equal(hash(sealed.path),sealed.sha256);
 }
 const contract={resultFiles:[f.report]};assert.equal(checkStructuredEvidence(f.root,contract,REPO).failed,false);
 const name=id===dispatch?'YeManFanHost':'FanCapabilityRoundtrip',dll=path.join(f.root,'build/bin/'+name+'/release_win-x64/'+name+'.dll');fs.appendFileSync(dll,'corrupt copied generated artifact');
 const changed=checkStructuredEvidence(f.root,contract,REPO);assert.equal(changed.failed,true);assert(changed.paths[0].sourceMismatches.includes(dll));
});
test('sealing rejects output or checkpoint bytes changed after JSON validation',t=>{
 for(const [mode,error] of [
  ['report',/ORIGINAL_REPORT_CHANGED_DURING_SEAL/],
  ['checkpoint',/INPUT_SNAPSHOT_CHANGED_DURING_SEAL/],
  ['host',/CONSUMED_HOST_CHANGED_DURING_SEAL/],
  ['input-copy',/INPUT_COPY_CHANGED_DURING_SEAL/],
 ]){
  const f=fixture(t),file=path.join(f.root,f.report),checkpoint=path.join(f.root,'catalog-input-snapshot.json');
  const copyPath=JSON.parse(fs.readFileSync(checkpoint,'utf8')).inputs.find(row=>row.kind==='sources').snapshotPath;
  const changed={report:file,checkpoint,host:f.host,'input-copy':copyPath}[mode],copy=fs.copyFileSync;
  const marker='\n ';
  const hook=t.mock.method(fs,'copyFileSync',(source,output,flags)=>{
   if(source===file)fs.appendFileSync(changed,marker);
   return copy(source,output,flags);
  });
  try{
   assert.throws(()=>sealPaths(dispatch,f.root,f.inputs),error,mode);
   assert(fs.readFileSync(changed,'utf8').endsWith(marker));
   assert(!Object.hasOwn(JSON.parse(fs.readFileSync(file,'utf8')),'evidenceClosure'));
  }finally{hook.mock.restore();}
 }
 for(const id of [dispatch,roundtrip]){
  const f=fixture(t,id),file=path.join(f.root,f.report),original=fs.readFileSync(file);
  const pdb=path.join(f.root,'build/bin/YeManFanBackend/release_win-x64/YeManFanBackend.pdb');
  const copy=fs.copyFileSync,hook=t.mock.method(fs,'copyFileSync',(source,output,flags)=>{
   copy(source,output,flags);
   if(source===file)fs.unlinkSync(pdb);
  });
  try{
   assert.throws(()=>sealPaths(id,f.root,f.inputs),/REQUIRED_GENERATED_ARTIFACT_MISSING/,id);
   assert(!fs.existsSync(pdb));
   assert.deepEqual(fs.readFileSync(file),original);
   assert.deepEqual(fs.readFileSync(path.join(f.root,'consumer-original-report.json')),original);
  }finally{hook.mock.restore();}
 }
});
test('missing PDB rejects a green original report',t=>{const f=fixture(t);fs.unlinkSync(f.host.replace(/\.dll$/,'.pdb'));assert.throws(()=>sealPaths(f.id,f.root,f.inputs),/REQUIRED_GENERATED_ARTIFACT_MISSING/);assert(!fs.existsSync(path.join(f.root,'consumer-original-report.json')));});
test('claimed consumed Host checksum must match the generated artifact',t=>{const f=fixture(t);f.raw.hostSha256='0'.repeat(64);f.write(f.report,JSON.stringify(f.raw));assert.throws(()=>sealPaths(f.id,f.root,f.inputs),/CONSUMED_HOST_IDENTITY_DIFFERS/);});
test('changed source input rejects before the original report is wrapped',t=>{const f=fixture(t);fs.appendFileSync(f.inputs.sources[0],'changed while consumer was running');assert.throws(()=>sealPaths(f.id,f.root,f.inputs),/INPUT_CHANGED/);assert(!fs.existsSync(path.join(f.root,'consumer-original-report.json')));});
test('same-count source member substitution cannot hide input membership drift',t=>{const f=fixture(t),other=f.write('input/other.cs','different source');assert.throws(()=>sealPaths(f.id,f.root,{...f.inputs,sources:[other]}),/INPUT_MEMBERSHIP_CHANGED/);});
test('failed original business assertions stay failed and unmodified',t=>{const f=fixture(t);f.raw.status='FAIL';f.raw.failures=['retained fixture failure'];f.write(f.report,JSON.stringify(f.raw));const original=fs.readFileSync(path.join(f.root,f.report));assert.throws(()=>sealPaths(f.id,f.root,f.inputs),/ORIGINAL_REPORT_NOT_PASSED/);assert.deepEqual(fs.readFileSync(path.join(f.root,f.report)),original);});
test('capture is fresh and cannot overwrite prior source evidence',t=>{const f=fixture(t);assert.throws(()=>capturePaths(f.id,f.root,f.inputs),/SNAPSHOT_ALREADY_EXISTS/);});
test('new catalog source fence declares project imports, content and every permitted sibling DLL',()=>{const adapters=getCapabilityCatalogAdapters(REPO);for(const a of adapters){for(const name of ['FanLab/real-host/Directory.Build.props','FanLab/real-host/YeManFanHost.json','tools/ymcc_sim/global/capability-catalog/output-evidence.mjs','tools/ymcc_sim/global/capability-catalog/toolchain-inputs.json'])assert(a.evidenceInputs.includes(name),name);const hc=a.artifactInputs.find(file=>path.basename(file)==='HandheldCompanion.dll'),dir=path.dirname(hc);for(const e of fs.readdirSync(dir,{withFileTypes:true}).filter(e=>e.isFile()&&e.name.toLowerCase().endsWith('.dll')))assert(a.artifactInputs.includes(path.join(dir,e.name)),e.name);for(const name of ['Microsoft.CodeAnalysis.dll','Microsoft.CodeAnalysis.CSharp.dll'])assert(a.artifactInputs.some(file=>path.basename(file)===name),name);}});
test('evidence CLI requires the global job and rejects repository output before writing',t=>{const f=fixture(t);assert.throws(()=>evidenceRoot(REPO),/REPOSITORY_BOUNDARY/);const helper=path.join(REPO,'tools/ymcc_sim/global/capability-catalog/output-evidence.mjs'),call=spawnSync(process.execPath,[helper,'seal','--case',dispatch,'--evidence-dir',f.root],{env:{...process.env,YMCC_SIM_GLOBAL_RUN:'0'},windowsHide:true,encoding:'utf8',timeout:10000});assert.equal(call.status,2);assert.match(call.stderr,/GLOBAL_OWNED_JOB_REQUIRED/);assert(!fs.existsSync(path.join(f.root,'consumer-original-report.json')));});
test('retained input copy corruption rejects the green report',t=>{const f=fixture(t),point=JSON.parse(fs.readFileSync(path.join(f.root,'catalog-input-snapshot.json'),'utf8')),copy=point.inputs.find(r=>r.kind==='sources').snapshotPath;assert(copy);assert.notEqual(copy,f.inputs.sources[0]);fs.appendFileSync(copy,'corrupt saved input');assert.throws(()=>sealPaths(f.id,f.root,f.inputs),/INPUT_SNAPSHOT_COPY_CHANGED/);});
test('new frozen Devices member is declared even when the old catalog omits it',t=>{const f=fixture(t),repo=path.join(f.root,'fixture-repo'),devices=path.join(repo,'deps/handheldcompanion-runtime/source/Devices');fs.mkdirSync(devices,{recursive:true});fs.mkdirSync(path.join(repo,'capabilities'));fs.writeFileSync(path.join(repo,'capabilities/hc-device-catalog.json'),JSON.stringify({sourceFiles:[{file:'Existing.cs'}]}));fs.writeFileSync(path.join(devices,'Existing.cs'),'fixture existing');fs.writeFileSync(path.join(devices,'Added.cs'),'fixture added');for(const adapter of getCapabilityCatalogAdapters(repo)){assert(adapter.evidenceInputs.includes('deps/handheldcompanion-runtime/source/Devices/Existing.cs'));assert(adapter.evidenceInputs.includes('deps/handheldcompanion-runtime/source/Devices/Added.cs'));}});
