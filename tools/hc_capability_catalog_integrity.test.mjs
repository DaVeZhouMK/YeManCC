import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {buildArtifactManifest,validateArtifactManifest,LEGACY_ARTIFACT_FLOOR,verifyBaseline,copyBaseline,
 INTEGRITY_CASES,applyIntegrityFault,auditVariant,expectedStaleTarget,classifyIntegrityRun} from './hc_capability_catalog_integrity.mjs';
import {getCapabilityCatalogAdapters} from './ymcc_sim/global/capability-catalog/adapter.mjs';
const clone=value=>JSON.parse(JSON.stringify(value));
function fixture(t){
 const temp=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(temp,'ymcc-catalog-integrity-'));
 t.after(()=>{const actual=fs.realpathSync(root);assert.equal(path.dirname(actual),temp);assert(path.basename(actual).startsWith('ymcc-catalog-integrity-'));fs.rmSync(actual,{recursive:true,force:true});});
 const repo=path.join(root,'repo');fs.mkdirSync(repo);
 const artifacts=Object.fromEntries(LEGACY_ARTIFACT_FLOOR.map(file=>[file,'baseline '+file+'\n']));
 artifacts['capabilities/hc-device-catalog.json']=JSON.stringify({scope:{factoryInstantiableClasses:88},domains:{controllerFeedback:{gameOutputCallbackBound:false}}});
 artifacts['capabilities/hc-performance-metadata.json']=JSON.stringify({rows:[{cpuIdentity:{exactSourceNames:['reviewed cpu']}}]});
 for(const spec of INTEGRITY_CASES.filter(spec=>spec.kind==='replace'))artifacts[spec.target]+='\n'+spec.from;
 artifacts['capabilities/HcControllerConfigurationCapabilityCatalog.generated.h']='configuration reviewed\n';
 artifacts['capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs']='game feedback reviewed\n';
 // A future generator output must be copied without editing any integrity copy list.
 artifacts['capabilities/FutureGeneratedDomain.generated.h']='future domain 未启用\n';
 for(const [file,text]of Object.entries(artifacts)){const target=path.join(repo,file);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,text);}
 return {root,repo,manifest:buildArtifactManifest(artifacts),artifacts};
}
test('manifest records exact ordered UTF-8 bytes, empty content and all generator keys',()=>{
 const artifacts={'a.txt':'中文','empty.txt':''},manifest=buildArtifactManifest(artifacts);
 assert.deepEqual(manifest.artifacts.map(row=>row.path),Object.keys(artifacts));assert.equal(manifest.artifacts[0].size,6);assert.equal(manifest.artifacts[1].size,0);
 assert.equal(manifest.artifactCount,2);
});
for(const invalid of ['../escape','a/../b','/absolute','C:/drive','C:relative','//server/share','a\\b','a//b','./a','a/./b','a/','a\u0000b','a.','a /b','a:stream',''])test('reject unsafe manifest path '+JSON.stringify(invalid),()=>{
 const manifest=buildArtifactManifest({'ok.txt':'x'});manifest.artifacts[0].path=invalid;assert.throws(()=>validateArtifactManifest(manifest),/CATALOG_MANIFEST_PATH/);
});
test('reject duplicate and case-fold duplicate paths',()=>{
 for(const file of ['ok.txt','OK.txt']){const manifest=buildArtifactManifest({'ok.txt':'x'});manifest.artifacts.push({...manifest.artifacts[0],path:file});manifest.artifactCount++;assert.throws(()=>validateArtifactManifest(manifest),/CATALOG_MANIFEST_DUPLICATE/);}
});
test('reject count, byte size and hash metadata corruption',()=>{
 const good=buildArtifactManifest({'a.txt':'x'});
 for(const patch of [{artifactCount:0},{artifactCount:2}])assert.throws(()=>validateArtifactManifest({...good,...patch}),/CATALOG_MANIFEST_COUNT/);
 for(const patch of [{size:-1},{size:0.1},{sha256:'bad'},{sha256:null}]){const manifest=clone(good);Object.assign(manifest.artifacts[0],patch);assert.throws(()=>validateArtifactManifest(manifest),/CATALOG_MANIFEST_IDENTITY/);}
});
test('reject omission from historical floor even if attacker adjusts count',t=>{
 const f=fixture(t);const manifest=clone(f.manifest);manifest.artifacts.shift();manifest.artifactCount--;
 assert.throws(()=>validateArtifactManifest(manifest,{requiredPaths:LEGACY_ARTIFACT_FLOOR}),/CATALOG_MANIFEST_REQUIRED_MEMBER/);
});
test('reject omission/reorder/hash rewrite against the exact trusted complete manifest',t=>{
 const f=fixture(t);
 for(const change of [m=>{m.artifacts.pop();m.artifactCount--;},m=>m.artifacts.reverse(),m=>m.artifacts[0].sha256='a'.repeat(64)]){
  const manifest=clone(f.manifest);change(manifest);assert.throws(()=>validateArtifactManifest(manifest,{expectedManifest:f.manifest}),/CATALOG_MANIFEST_BASELINE_CHANGED/);
 }
});
test('baseline rejects incorrect digest or size and does not repair stale repo',t=>{
 const f=fixture(t),target=path.join(f.repo,f.manifest.artifacts[0].path);
 for(const patch of [{sha256:'0'.repeat(64)},{size:9999}]){const manifest=clone(f.manifest);Object.assign(manifest.artifacts[0],patch);assert.throws(()=>verifyBaseline(f.repo,manifest),/CATALOG_BASELINE_BYTES_DIFFER/);}
 fs.appendFileSync(target,'stale');assert.throws(()=>copyBaseline(f.repo,path.join(f.root,'copy'),f.manifest),/CATALOG_BASELINE_BYTES_DIFFER/);assert(fs.readFileSync(target,'utf8').endsWith('stale'));
});
test('copy follows every current and future generator artifact with exact original bytes',t=>{
 const f=fixture(t),copy=path.join(f.root,'copy');copyBaseline(f.repo,copy,f.manifest);
 for(const row of f.manifest.artifacts)assert.deepEqual(fs.readFileSync(path.join(copy,row.path)),fs.readFileSync(path.join(f.repo,row.path)));
 assert.equal(f.manifest.artifactCount,17);verifyBaseline(f.repo,f.manifest);
 assert.throws(()=>copyBaseline(f.repo,copy,f.manifest),/CATALOG_FIXTURE_ALREADY_EXISTS/);
});
for(const spec of INTEGRITY_CASES)test('original variant changes only its own exact targets: '+spec.id,t=>{
 const f=fixture(t),copy=path.join(f.root,spec.id);copyBaseline(f.repo,copy,f.manifest);
 const audit=applyIntegrityFault(copy,f.manifest,spec);assert.equal(audit.status,'PASS');
 assert.equal(audit.changes.length,spec.kind==='current'?0:spec.kind==='missing-generated'?15:1);
 assert.equal(audit.unchangedArtifacts+audit.changes.length,17);verifyBaseline(f.repo,f.manifest);
 auditVariant(copy,f.manifest,spec,{expectedSnapshot:audit.snapshot});
 const untouched=f.manifest.artifacts.find(row=>!audit.targetPaths.includes(row.path));fs.appendFileSync(path.join(copy,untouched.path),'unrelated damage');
 assert.throws(()=>auditVariant(copy,f.manifest,spec),/CATALOG_VARIANT_UNEXPECTED_CHANGE/);
});
test('a target changed again during the child check is rejected',t=>{
 const f=fixture(t),copy=path.join(f.root,'copy'),spec=INTEGRITY_CASES.find(s=>s.id==='wrong-controller-motion');copyBaseline(f.repo,copy,f.manifest);
 const audit=applyIntegrityFault(copy,f.manifest,spec);fs.appendFileSync(path.join(copy,spec.target),'additional target drift');
 assert.throws(()=>auditVariant(copy,f.manifest,spec,{expectedSnapshot:audit.snapshot}),/CATALOG_VARIANT_CHANGED_DURING_CHECK/);
});
test('reject added unmanifested scratch members',t=>{
 const f=fixture(t),copy=path.join(f.root,'copy');copyBaseline(f.repo,copy,f.manifest);fs.writeFileSync(path.join(copy,'undeclared.txt'),'x');
 assert.throws(()=>auditVariant(copy,f.manifest,INTEGRITY_CASES[0]),/CATALOG_VARIANT_UNDECLARED_MEMBER/);
});
test('real directory junction cannot redirect a manifest source',t=>{
 const f=fixture(t),outside=path.join(f.root,'outside');fs.mkdirSync(outside);fs.writeFileSync(path.join(outside,'file.txt'),'x');
 fs.symlinkSync(outside,path.join(f.repo,'redirect'),'junction');
 const manifest=buildArtifactManifest({'redirect/file.txt':'x'});assert.throws(()=>verifyBaseline(f.repo,manifest),/CATALOG_ARTIFACT_LINK/);
});
for(const spec of INTEGRITY_CASES.slice(1))test('exact target oracle rejects earlier unrelated failure: '+spec.id,t=>{
 const f=fixture(t),target=expectedStaleTarget(f.manifest,spec),stderr='Error: STALE_HC_CAPABILITY_CATALOG: '+target+'\n at generator';
 const good={status:1,signal:null,error:null,stdout:'',stderr};assert.equal(classifyIntegrityRun(good,f.manifest,spec).accepted,true);
 for(const patch of [{status:0},{status:2},{status:null},{signal:'SIGTERM'},{error:{code:'ETIMEDOUT'}},{stderr:'Error: compilation failed'},{stderr:'Error: STALE_HC_CAPABILITY_CATALOG: capabilities/HcControllerConfigurationCapabilityCatalog.generated.h\n'},{stderr:stderr+'\nError: STALE_HC_CAPABILITY_CATALOG: another.file'}])assert.equal(classifyIntegrityRun({...good,...patch},f.manifest,spec).accepted,false);
});
test('current requires natural zero and original CURRENT result claims',t=>{
 const f=fixture(t),spec=INTEGRITY_CASES[0],good={status:0,stdout:JSON.stringify({status:'CURRENT',hardwareIo:false,assemblyLoaded:false}),stderr:''};
 assert.equal(classifyIntegrityRun(good,f.manifest,spec).accepted,true);
 for(const patch of [{status:1},{stdout:'{}'},{stdout:'not json'},{stdout:JSON.stringify({status:'GENERATED',hardwareIo:false,assemblyLoaded:false})}])assert.equal(classifyIntegrityRun({...good,...patch},f.manifest,spec).accepted,false);
});
test('same original 16 case IDs, unchanged exact motion target and native baseline membership',()=>{
 assert.equal(INTEGRITY_CASES.length,16);assert.equal(new Set(INTEGRITY_CASES.map(s=>s.id)).size,16);
 for(const id of ['wrong-controller-motion','wrong-controller-motion-provider'])assert.equal(INTEGRITY_CASES.find(s=>s.id===id).target,'capabilities/HcControllerMotionCapabilityCatalog.generated.h');
 assert(LEGACY_ARTIFACT_FLOOR.includes('native/main.cpp'));
});
test('both catalog adapters declare every new actual source and prior runtime tooling',()=>{
 const required=['tools/hc_capability_catalog_integrity.mjs','tools/hc_capability_catalog_integrity.test.mjs','tools/lib/hc_controller_configuration_catalog.mjs','capabilities/hc-controller-configuration-policy.json','capabilities/HcControllerConfigurationCapabilityCatalog.generated.h','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs','native/msi_rear_protocol.h','capabilities/CONTROLLER-GAME-MOTOR-RECEIPT.json','capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json','capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json'];
 for(const adapter of getCapabilityCatalogAdapters()){
  for(const file of required)assert(adapter.evidenceInputs.includes(file),adapter.id+': '+file);
  for(const file of LEGACY_ARTIFACT_FLOOR)assert(adapter.evidenceInputs.includes(file),adapter.id+': historical '+file);
  assert(adapter.artifactInputs.some(file=>file.replaceAll('\\','/').endsWith('/deps/hidmaestro/HIDMaestro.Core.dll')));
 }
});
