import {snapshotControllerMotionFixture,verifyControllerMotionFixture} from '../../lib/hc_controller_motion_fixture.mjs';
// Reuse only the exact-body extractor, never the older broad freeze/test runner.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
export const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
export const functions=['powerLifecycleName','legionMotionModelBinding','inputCaptureLegionControllerMotionRoute','legionMotionSelectedSensorIndex','legionMotionBoundSource','legionMotionSourceForSelectedLocked','legionMotionSnapshot','inputCaptureControllerSensorStatus','exportControllerMotionEvidence'];
function anchor(text,name,code){
 const i=text.indexOf(code);assert(i>=0&&text.lastIndexOf(code)===i,'Missing/ambiguous static anchor '+name);
 return {name,kind:'source-only',code,sha256:sha(code),startLine:text.slice(0,i).split('\n').length,startUtf8Byte:Buffer.byteLength(text.slice(0,i)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,i+code.length))};
}
function powerEnum(text){
 const matches=[...text.matchAll(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^}]*\};/gm)];
 assert.equal(matches.length,1,'Exact production PowerLifecycle enum');
 return {...anchor(text,'PowerLifecycle',matches[0][0]),symbol:'PowerLifecycle',kind:'enum'};
}
export function freeze(repo,out){
 const bytes=fs.readFileSync(path.join(repo,'native/main.cpp')),text=bytes.toString('utf8'),entries=functions.map(name=>extract(text,name)),type=powerEnum(text);
 const ipc=/ipc_on\("gyro\.controllerSensor\.get",\s*\[\]\(const json&\)\s*->\s*json\s*\{\s*return inputCaptureControllerSensorStatus\(\);\s*\}\);/.exec(text);
 const log=/const auto controllerMotionEvidence=exportControllerMotionEvidence\(\);\s*if\(!controllerMotionEvidence\.empty\(\)\)addFile\(controllerMotionEvidence\);\s*else availability\["files"\]\.push_back\(\{\{"name","controller-motion-evidence\.json"\},\{"present",false\},\s*\{"missingReason","current-controller-motion-snapshot-not-saved"\}\}\);/.exec(text);
 const manifest=/manifest << "controllerMotionEvidence=controller-motion-evidence\.json;readOnly=true;queueTimeNotFirmwareSampling=true;hostAndGameConsumptionNotProven=true\\n";/.exec(text);
 assert(ipc&&log&&manifest,'Expected static IPC/export/manifest wiring');
 const anchors=[anchor(text,'gyro.controllerSensor.get delegation',ipc[0]),anchor(text,'logs.exportAll only includes successful current export',log[0]),anchor(text,'manifest read-only evidence boundary',manifest[0])];
 fs.writeFileSync(path.join(out,'main.source.snapshot.cpp'),bytes);
 const motionBinding=snapshotControllerMotionFixture(repo,path.join(out,'controller-motion'));
 fs.writeFileSync(path.join(out,'production-functions.generated.h'),motionBinding.preamble+entries.map(e=>e.code).join('\n\n')+'\n');
 fs.writeFileSync(path.join(out,'production-types.generated.h'),type.code+'\n');
 const binding={sourceSha256:sha(bytes),sourceBytes:bytes.length,entries:entries.map(({code,...e})=>e),type,anchors,scope:'Seven exact main functions and actual enum execute; IPC handler/log-package/manifest wiring is source-only. Identity discovery and OS file operations are mocked.'};
 fs.writeFileSync(path.join(out,'SOURCE-BINDINGS.json'),JSON.stringify(binding,null,2)+'\n');
 return {...binding,entries,motionBinding};
}
export function verify(repo,frozen){
 const bytes=fs.readFileSync(path.join(repo,'native/main.cpp')),text=bytes.toString('utf8');
 return {methodsStable:verifyControllerMotionFixture(frozen.motionBinding).every(row=>row.unchanged)&&frozen.entries.every(e=>extract(text,e.symbol).sha256===e.sha256),typeStable:powerEnum(text).sha256===frozen.type.sha256,anchorsStable:frozen.anchors.every(a=>text.includes(a.code)),currentMainSha256:sha(bytes),wholeMainDrifted:sha(bytes)!==frozen.sourceSha256};
}
