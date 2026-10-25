import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';

export const MODULE='tools/ymcc_sim/global/controller-pnp-absence';
export const ID='controller.pnp-absence-opaque-ids';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const TOOLCHAIN_ENV='YMCC_SIM_PNP_ABSENCE_TOOLCHAIN';
export const DEFAULT_TOOLCHAIN='G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261021-G3E-XboxSwitch/native-verified';
export const SELFTEST='tools/virtual_target_return_pnp_absence_selftest.mjs';
export const INPUTS=[
 'native/virtual_target_return_pnp_absence.h','native/virtual_target_return_record.h',
 'deps/json/json.hpp','InputHost/Program.cs',SELFTEST,
 'tools/fixtures/virtual_target_return_pnp_absence/fixture.cpp',
 'tools/fixtures/virtual_target_return_pnp_absence/README.md',
 'tools/ymcc_sim/global/extra-adapters.mjs',
 ...['adapter.mjs','run.mjs','runner.test.mjs','toolchain-inputs.json','field-record.json','README.md'].map(name=>MODULE+'/'+name),
];
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const key=file=>path.resolve(file).toLowerCase();

export function resolveToolchain({repo=REPO,env=process.env}={}) {
 const configured=env[TOOLCHAIN_ENV]??DEFAULT_TOOLCHAIN;
 assert(path.isAbsolute(configured),'PNP_ABSENCE_TOOLCHAIN_REQUIRES_ABSOLUTE_PATH');
 const root=fs.realpathSync(configured);
 const capturePath=path.join(root,'build-environment.json');
 const manifestPath=path.join(root,'toolchain-manifest.json');
 const capture=read(capturePath),manifest=read(manifestPath);
 const spec=read(path.join(repo,MODULE,'toolchain-inputs.json'));
 assert(spec.schemaVersion===1&&spec.dependencies.length>0,'PNP_ABSENCE_DEPENDENCY_INVENTORY_INVALID');
 const bindings=new Map(manifest.files.map(row=>[key(row.original),row]));
 const frozen=spec.dependencies.map(dependency=>{
  const row=bindings.get(key(dependency.original));
  assert(row&&row.sha256===dependency.sha256,'PNP_ABSENCE_FROZEN_DEPENDENCY_UNBOUND: '+dependency.original);
  return {path:path.resolve(row.frozen),sha256:dependency.sha256};
 });
 const tools=['cl.exe','link.exe','dumpbin.exe'].map(name=>{
  const file=capture.tools[name]?.paths?.[0];
  assert(file&&path.isAbsolute(file),'PNP_ABSENCE_CAPTURED_TOOL_MISSING: '+name);
  const row=manifest.tools.find(tool=>key(tool.original)===key(file));
  assert(row&&/^[a-f0-9]{64}$/i.test(row.sha256),'PNP_ABSENCE_CAPTURED_TOOL_UNBOUND: '+name);
  return {path:path.resolve(file),sha256:row.sha256};
 });
 const artifacts=[...new Set([process.execPath,capturePath,manifestPath,...tools.map(row=>row.path),...frozen.map(row=>row.path)])].map(file=>path.resolve(file)).sort();
 for(const file of artifacts) assert(fs.statSync(file).isFile(),'PNP_ABSENCE_ARTIFACT_MISSING: '+file);
 return {root,artifacts,bindings:[...tools,...frozen],frozen};
}

export function getPnpAbsenceAdapters(repo=REPO,{env=process.env}={}) {
 let inputs;
 try { inputs={artifactInputs:resolveToolchain({repo,env}).artifacts}; }
 catch(error) { inputs={artifactInputs:[],blockedReason:'PNP_ABSENCE_TOOLCHAIN_UNAVAILABLE: '+error.message}; }
 return [{id:ID,domain:'controller',title:'Actual PnP absence helper: opaque observed IDs, bounded census and four compiled semantic controls',
  kind:'node',path:MODULE+'/run.mjs',args:['--out','{outDir}/pnp-absence'],timeoutMs:180000,
  profiles:['desktop','full'],scope:'behavioral-mock',evidenceInputs:INPUTS,...inputs,
  resultFiles:['pnp-absence/RESULT.json'],
  boundary:'Source-bound production helper + fake Reader only; Release/Checked and four compiled negative controls. Actual ProductionReader ABI is compiled but never called. Field ZIP does not identify the rejected G3E ID; all sample IDs are contract inputs. No product/Host/device/driver/service execution or new hardware authority. The selftest historical status label does not describe current production integration.'}];
}
