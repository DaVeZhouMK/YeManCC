import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const self = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(self), '..');
const generated = path.join(repo, 'tools', '.generic-sdk-ingress-' + process.pid + '.mjs');
let text = fs.readFileSync(path.join(repo, 'tools/hc_windows_sensor_fallback_selftest.mjs'), 'utf8');
function replaceOnce(before, after) {
  if (text.split(before).length !== 2) throw Error('SDK runner anchor: ' + before.slice(0, 100));
  text = text.replace(before, () => after);
}
replaceOnce("import {calibrationToolchainPreamble}",
  "import {rebindGenericSdkIngress,extendGenericSdkHarness} from './fixtures/hc_generic_windows/sdk_ingress.mjs';\nimport {calibrationToolchainPreamble}");
replaceOnce('ControllerMotion/WindowsDefaultMotion/SensorIngress', 'ControllerMotion/GenericWindowsSdkIngress-20261018');
replaceOnce("new Date().toISOString().replace(/[:.]/g,'-')", "new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid+'-'+(process.argv[2]||'baseline')");
replaceOnce("['','--ignore-retirement','--mix-partial-winrt']", "['','--ignore-retirement','--mix-partial-winrt','--generic-sdk-wrong-matrix']");
replaceOnce('result.defaultWiringBindings=wired.bindings;',
  `result.defaultWiringBindings=wired.bindings;
 const generic=rebindGenericSdkIngress(base,source);base=generic.code;result.genericBindings=generic.bindings;
 fs.writeFileSync(path.join(out,'hc_generic_motion_catalog.h'),freeze(path.join(repo,'native/hc_generic_motion_catalog.h')));`);
replaceOnce("const harness=freeze(path.join(repo,'tools/fixtures/hc_windows_sensor_fallback/harness.cpp')).toString('utf8').replace('// @@SOURCE_FUNCTIONS@@',functions);",
  "const harness=extendGenericSdkHarness(freeze(path.join(repo,'tools/fixtures/hc_windows_sensor_fallback/harness.cpp')).toString('utf8')).replace('// @@SOURCE_FUNCTIONS@@',functions);");
replaceOnce(" fs.writeFileSync(path.join(out,'expectations.json'),JSON.stringify(oracle));",
  ` oracle.genericIngress=[
  {manufacturer:'UNLISTED MANUFACTURER',product:'GENERIC-NO-EXACT-MODEL',systemProduct:'',processor:'',systemVersion:'',expectedGenericClass:'HandheldCompanion.Devices.DefaultDevice'},
  {manufacturer:'ASUSTEK COMPUTER INC.',product:'GENERIC-NO-EXACT-MODEL',systemProduct:'',processor:'',systemVersion:'',expectedGenericClass:'HandheldCompanion.Devices.ASUS.ASUS'}
 ];
 oracle.expectedSdkProcess['hc-a250b19a:generic-DefaultDevice:gyro:x-z-y']=[1,3,2];
 oracle.expectedSdkProcess['hc-a250b19a:generic-DefaultDevice:accel:x-z-y']=[0.25,1,-0.5];
 // A deliberately wrong declared ID must still be judged by the fixed upstream numeric oracle.
 oracle.expectedSdkProcess['hc-a250b19a:generic-DefaultDevice:gyro:x-y-z']=[1,3,2];
 oracle.expectedSdkProcess['hc-a250b19a:generic-DefaultDevice:accel:x-y-z']=[0.25,1,-0.5];
 oracle.expectedSdkProcess['hc-a250b19a:generic-ASUS:gyro:x-y-z']=[1,2,3];
 oracle.expectedSdkProcess['hc-a250b19a:generic-ASUS:accel:x-y-z']=[0.25,-0.5,1];
 fs.writeFileSync(path.join(out,'expectations.json'),JSON.stringify(oracle));
 if(control==='--generic-sdk-wrong-matrix'){
  const file=path.join(out,'hc_generic_motion_catalog.h'),before=fs.readFileSync(file,'utf8');
  if(!before.includes(':gyro:x-z-y')||!before.includes(':accel:x-z-y'))throw Error('Generic matrix control anchor');
  fs.writeFileSync(file,before.replaceAll(':gyro:x-z-y',':gyro:x-y-z').replaceAll(':accel:x-z-y',':accel:x-y-z'));
 }`);
replaceOnce('freeze(fileURLToPath(import.meta.url));',
  'freeze(fileURLToPath(import.meta.url));freeze(' + JSON.stringify(self) + ");freeze(path.join(repo,'tools/hc_windows_sensor_fallback_selftest.mjs'));freeze(path.join(repo,'tools/fixtures/hc_generic_windows/sdk_ingress.mjs'));");
replaceOnce("(control==='--ignore-retirement'?'windows-fallback-retirement':'windows-fallback-partial')",
  "(control==='--ignore-retirement'?'windows-fallback-retirement':control==='--generic-sdk-wrong-matrix'?'windows-fallback-process':'windows-fallback-partial')");
replaceOnce('result.sourcesUnchanged=inputs.every(r=>sha(fs.readFileSync(r.path))===r.sha256);',
  `result.fullNativeSourceUnchanged=sha(fs.readFileSync(path.join(repo,'native/main.cpp')))===sha(Buffer.from(source));
 const scopedNames=[...new Set([...result.defaultWiringBindings,...result.genericBindings,...result.bindings,...result.wiring].map(b=>b.symbol||b.name))];
 for(const name of ['inputCaptureResolveNewHcMotionSelector','inputCaptureNewHcMotionSelectorBound','inputCaptureResolveLegacyHcMotionSelector','inputCaptureApplyHcMatrixCandidate','ymccResolveFamily'])scopedNames.push(name);
 const currentNative=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');
 result.scopedBindings=scopedNames.map(name=>({symbol:name,sha256:extract(source,name).sha256,currentSha256:extract(currentNative,name).sha256}));
 result.scopedSourceUnchanged=result.scopedBindings.every(b=>b.sha256===b.currentSha256);
 result.sourcesUnchanged=result.scopedSourceUnchanged&&inputs.filter(r=>r.path!==path.join(repo,'native/main.cpp')).every(r=>sha(fs.readFileSync(r.path))===r.sha256);`);
fs.writeFileSync(generated, text);
const result = spawnSync('node', [generated, ...process.argv.slice(2)], {
  cwd: repo, windowsHide: true, encoding: 'utf8', timeout: 240000, maxBuffer: 8e6
});
console.log(result.stdout || '');
if (result.stderr) console.error(result.stderr);
if (result.error) console.error(result.error);
process.exitCode = result.status ?? 1;