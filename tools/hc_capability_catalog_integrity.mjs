// Integrity-only helpers. The generator owns membership; this historical floor is never a copy list.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
export const MANIFEST_FILE='generated-artifacts-manifest.json';
export const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const need=(condition,code)=>{if(!condition)throw Error(code);};
export const LEGACY_ARTIFACT_FLOOR=Object.freeze([
 'capabilities/HcControllerFeedbackCapabilityCatalog.generated.h','capabilities/hc-device-catalog.json',
 'capabilities/hc-performance-metadata.json','capabilities/HcMotionCapabilityCatalog.generated.h',
 'capabilities/HcControllerInputCapabilityCatalog.generated.h','capabilities/HcControllerMotionCapabilityCatalog.generated.h',
 'capabilities/HcDeviceCapabilityCatalog.generated.cs','capabilities/HcLightingCapabilityCatalog.generated.cs',
 'capabilities/HcBatteryCapabilityCatalog.generated.cs','capabilities/hc-device-catalog.generated.h',
 'native/main.cpp','native/oem_lighting_operations.h','native/lighting_device_core_binding.h',
 'FanLab/LightSetter/LightingAssemblyPins.cs',
]);
function validPath(file){
 return typeof file==='string'&&file.length>0&&!file.includes('\\')&&!file.includes(':')&&
  !/[\x00-\x1f\x7f]/.test(file)&&!path.posix.isAbsolute(file)&&!path.win32.isAbsolute(file)&&
  file.split('/').every(p=>p!==''&&p!=='.'&&p!=='..'&&!/[. ]$/.test(p));
}
export function validateArtifactManifest(manifest,{requiredPaths=[],expectedManifest}={}){
 need(manifest?.schemaVersion===1&&manifest.kind==='hc-capability-generated-artifacts'&&Array.isArray(manifest.artifacts),'CATALOG_MANIFEST_SCHEMA');
 need(Number.isSafeInteger(manifest.artifactCount)&&manifest.artifactCount>0&&manifest.artifactCount===manifest.artifacts.length,'CATALOG_MANIFEST_COUNT');
 const seen=new Set();
 for(const row of manifest.artifacts){
  need(validPath(row.path),'CATALOG_MANIFEST_PATH');
  const key=row.path.toLowerCase();need(!seen.has(key),'CATALOG_MANIFEST_DUPLICATE');seen.add(key);
  need(Number.isSafeInteger(row.size)&&row.size>=0&&typeof row.sha256==='string'&&/^[a-f0-9]{64}$/.test(row.sha256),'CATALOG_MANIFEST_IDENTITY');
 }
 for(const file of requiredPaths)need(manifest.artifacts.some(row=>row.path===file),'CATALOG_MANIFEST_REQUIRED_MEMBER:'+file);
 if(expectedManifest){validateArtifactManifest(expectedManifest);need(JSON.stringify(manifest)===JSON.stringify(expectedManifest),'CATALOG_MANIFEST_BASELINE_CHANGED');}
 return manifest;
}
export function buildArtifactManifest(artifacts){
 const rows=Object.entries(artifacts).map(([file,text])=>{const bytes=Buffer.from(text,'utf8');return {path:file,size:bytes.length,sha256:sha(bytes)};});
 return validateArtifactManifest({schemaVersion:1,kind:'hc-capability-generated-artifacts',artifactCount:rows.length,artifacts:rows});
}
function artifactPath(root,file){
 need(validPath(file),'CATALOG_MANIFEST_PATH');
 const absolute=path.resolve(root),resolved=path.resolve(absolute,...file.split('/'));
 need(resolved.startsWith(absolute+path.sep),'CATALOG_ARTIFACT_ESCAPE');
 let current=absolute;
 need(!fs.lstatSync(current).isSymbolicLink()&&fs.statSync(current).isDirectory(),'CATALOG_ARTIFACT_ROOT');
 for(const part of file.split('/')){current=path.join(current,part);if(fs.existsSync(current))need(!fs.lstatSync(current).isSymbolicLink(),'CATALOG_ARTIFACT_LINK');}
 return resolved;
}
export function snapshotArtifacts(root,manifest){
 validateArtifactManifest(manifest);
 return manifest.artifacts.map(row=>{
  const file=artifactPath(root,row.path);
  if(!fs.existsSync(file))return {path:row.path,missing:true};
  need(fs.statSync(file).isFile(),'CATALOG_ARTIFACT_NOT_FILE');
  const bytes=fs.readFileSync(file);return {path:row.path,size:bytes.length,sha256:sha(bytes)};
 });
}
export function verifyBaseline(root,manifest){
 const rows=snapshotArtifacts(root,manifest);
 need(JSON.stringify(rows)===JSON.stringify(manifest.artifacts),'CATALOG_BASELINE_BYTES_DIFFER');return rows;
}
export function copyBaseline(repo,fixture,manifest){
 validateArtifactManifest(manifest);need(!fs.existsSync(fixture),'CATALOG_FIXTURE_ALREADY_EXISTS');fs.mkdirSync(fixture,{recursive:true});
 for(const row of manifest.artifacts){
  const bytes=fs.readFileSync(artifactPath(repo,row.path));
  need(bytes.length===row.size&&sha(bytes)===row.sha256,'CATALOG_BASELINE_BYTES_DIFFER:'+row.path);
  const target=artifactPath(fixture,row.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});
 }
 return verifyBaseline(fixture,manifest);
}
const fault=(id,target,kind,from,to)=>Object.freeze({id,target,kind,from,to});
export const INTEGRITY_CASES=Object.freeze([
 fault('current',null,'current'),
 fault('wrong-source-count','capabilities/hc-device-catalog.json','source-count'),
 fault('wrong-consumer-alias','capabilities/HcDeviceCapabilityCatalog.generated.cs','replace','hc.device.ROGAlly"','hc.device.ROGAllyFake"'),
 fault('missing-generated-source',null,'missing-generated'),
 fault('wrong-lighting-operation','capabilities/HcLightingCapabilityCatalog.generated.cs','replace','HcSourceLedEffect.Ambilight','HcSourceLedEffect.Wave'),
 fault('wrong-native-backend','native/lighting_device_core_binding.h','replace','HandheldCompanion.Devices.OneXPlayerX1Mini','HandheldCompanion.Devices.ROGAlly'),
 fault('wrong-battery-rule','capabilities/HcBatteryCapabilityCatalog.generated.cs','replace','battery.rule.asus-ally-acpi.ROGAlly','battery.rule.unreviewed.ROGAlly'),
 fault('wrong-motion-pair','capabilities/HcMotionCapabilityCatalog.generated.h','replace','hc-a250b19a:ayaneo3:accel:neg-x-neg-z-y','hc-a250b19a:wrong:accel'),
 fault('wrong-private-motion-tuple','capabilities/HcMotionCapabilityCatalog.generated.h','replace','Kind::Private, 0, "motion.private.ClawA1M.0"','Kind::Private, 1, "motion.private.ClawA1M.0"'),
 fault('wrong-physical-feedback-writer','capabilities/HcControllerFeedbackCapabilityCatalog.generated.h','replace','"g_feedbackSetState"','"wrong-feedback-writer"'),
 fault('wrong-physical-feedback-game-claim','capabilities/hc-device-catalog.json','game-claim'),
 fault('wrong-controller-source','capabilities/HcControllerInputCapabilityCatalog.generated.h','replace','xinput-any-connected','unknown'),
 fault('wrong-controller-motion','capabilities/HcControllerMotionCapabilityCatalog.generated.h','replace','ProtocolKind::Tablet, 6127, 2','ProtocolKind::Tablet, 6127, 1'),
 fault('wrong-performance-cpu-name','capabilities/hc-performance-metadata.json','cpu-name'),
 fault('missing-performance-metadata','capabilities/hc-performance-metadata.json','remove'),
 fault('wrong-controller-motion-provider','capabilities/HcControllerMotionCapabilityCatalog.generated.h','replace','ProtocolKind::Tablet','ProtocolKind::GoS'),
]);
export function faultTargets(manifest,spec){
 validateArtifactManifest(manifest);
 const targets=spec.kind==='current'?[]:spec.kind==='missing-generated'?manifest.artifacts.filter(row=>!row.path.endsWith('.json')).map(row=>row.path):[spec.target];
 for(const target of targets)need(manifest.artifacts.some(row=>row.path===target),'CATALOG_FAULT_TARGET_ABSENT:'+target);
 need(spec.kind==='current'||targets.length>0,'CATALOG_FAULT_EMPTY');return targets;
}
export function expectedStaleTarget(manifest,spec){
 const targets=faultTargets(manifest,spec);return manifest.artifacts.find(row=>targets.includes(row.path))?.path??null;
}
export function applyIntegrityFault(root,manifest,spec){
 verifyBaseline(root,manifest);
 const targets=faultTargets(manifest,spec);
 for(const target of targets){
  const file=artifactPath(root,target);
  if(['remove','missing-generated'].includes(spec.kind)){fs.unlinkSync(file);continue;}
  const text=fs.readFileSync(file,'utf8');let changed;
  if(spec.kind==='replace')changed=text.replace(spec.from,spec.to);
  else {const data=JSON.parse(text);
   if(spec.kind==='source-count'){data.scope.factoryInstantiableClasses++;changed=JSON.stringify(data,null,2)+'\n';}
   else if(spec.kind==='game-claim'){data.domains.controllerFeedback.gameOutputCallbackBound=true;changed=JSON.stringify(data);}
   else if(spec.kind==='cpu-name'){data.rows.find(row=>row.cpuIdentity.exactSourceNames.length).cpuIdentity.exactSourceNames[0]='UNREVIEWED CPU NAME';changed=JSON.stringify(data,null,2)+'\n';}
   else throw Error('CATALOG_FAULT_UNKNOWN');
  }
  need(changed!==text,'CATALOG_FAULT_MARKER_ABSENT:'+spec.id);fs.writeFileSync(file,changed);
 }
 return auditVariant(root,manifest,spec);
}
function fixtureFiles(root,relative=''){
 const result=[];
 for(const entry of fs.readdirSync(path.join(root,relative),{withFileTypes:true})){
  need(!entry.isSymbolicLink(),'CATALOG_ARTIFACT_LINK');const file=relative?relative+'/'+entry.name:entry.name;
  if(entry.isDirectory())result.push(...fixtureFiles(root,file));else {need(entry.isFile(),'CATALOG_ARTIFACT_NOT_FILE');result.push(file);}
 }
 return result;
}
export function auditVariant(root,manifest,spec,{expectedSnapshot}={}){
 const rows=snapshotArtifacts(root,manifest),targets=faultTargets(manifest,spec),changes=[];
 for(let i=0;i<rows.length;i++){
  const changed=JSON.stringify(rows[i])!==JSON.stringify(manifest.artifacts[i]);
  need(changed===targets.includes(rows[i].path),'CATALOG_VARIANT_UNEXPECTED_CHANGE:'+rows[i].path);
  if(changed){const removed=['remove','missing-generated'].includes(spec.kind);need(Boolean(rows[i].missing)===removed,'CATALOG_VARIANT_CHANGE_KIND');changes.push({before:manifest.artifacts[i],after:rows[i]});}
 }
 const actual=fixtureFiles(root).sort(),expected=rows.filter(row=>!row.missing).map(row=>row.path).sort();
 need(JSON.stringify(actual)===JSON.stringify(expected),'CATALOG_VARIANT_UNDECLARED_MEMBER');
 if(expectedSnapshot)need(JSON.stringify(rows)===JSON.stringify(expectedSnapshot),'CATALOG_VARIANT_CHANGED_DURING_CHECK');
 return {status:'PASS',targetPaths:targets,expectedStaleTarget:expectedStaleTarget(manifest,spec),unchangedArtifacts:rows.length-targets.length,changes,snapshot:rows};
}
export function classifyIntegrityRun(run,manifest,spec){
 const expected=expectedStaleTarget(manifest,spec);
 const errors=[...(run.stderr??'').matchAll(/^Error: STALE_HC_CAPABILITY_CATALOG: ([^\r\n]+)\r?$/gm)].map(match=>match[1]);
 const natural=Number.isInteger(run.status)&&!run.signal&&!run.error;
 let accepted=natural&&run.status===1&&errors.length===1&&errors[0]===expected;
 if(spec.kind==='current'){
  let report;try{report=JSON.parse((run.stdout??'').trim());}catch{}
  accepted=natural&&run.status===0&&errors.length===0&&report?.status==='CURRENT'&&report.hardwareIo===false&&report.assemblyLoaded===false;
 }
 return {accepted,expectedExitCode:spec.kind==='current'?0:1,expectedStaleTarget:expected,observedStaleTargets:errors,naturalExit:natural};
}
