import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const root=process.argv[2];const output=process.argv[3];
if(!root||!output)throw Error('Expected exported-log root and fixture output');
const issues=[];const inputs=[];
function rows(file){const name=path.join(root,file);const bytes=fs.readFileSync(name);inputs.push({file:name,sha256:crypto.createHash('sha256').update(bytes).digest('hex')});return bytes.toString('utf8').split(/\r?\n/).flatMap((line,i)=>{if(!line.trim())return [];try{return [{...JSON.parse(line),evidence:{file:name,line:i+1}}];}catch{issues.push({file:name,line:i+1});return [];}});}
const native=rows('native-lifecycle.log');const virtual=rows('virtual-gamepad.log');
const period='2026-10-11T00:37:';
const events=[...native.filter(r=>r.pid===8912 && r.time?.startsWith(period) && r.time>='2026-10-11T00:37:08' && r.time<'2026-10-11T00:37:17' && ['gamepad-physical-source-admission','msi-main-source-record','msi-claw-dinput-route','late-hide-exec-decision'].includes(r.event)),
...virtual.filter(r=>r.pid===8912 && r.event==='input-host-started' && r.epoch===2)].sort((a,b)=>a.time.localeCompare(b.time));
const a2Native=rows('258v-20261007/native-lifecycle.log');
const a2Gyro=rows('258v-20261007/gyro-motion.log');
const captures=rows('258v-20261007/input-capture.jsonl').filter(r=>r.kind==='sample'&&r.imu?.gyro&&r.imu?.raw&&r.event?.gyro?.timestamp100ns>0);
const a2Samples=captures.map(r=>({timestamp:r.event.gyro.timestamp100ns,generation:r.event.gyro.bindingGeneration,receipt:r.event.gyro.receiptTick,raw:{x:r.imu.raw.gx,y:r.imu.raw.gy,z:r.imu.raw.gz},candidate:r.hcMatrixCandidate.gyro,evidence:r.evidence}));
const fixture={schemaVersion:1,evidenceOnly:true,inputs,issues,
 g3Identity:native.find(r=>r.event==='machine-identity'&&r.pid===8912),
 g3Init:native.find(r=>r.event==='sensor-initialised'&&r.pid===8912),
 a2Identity:a2Native.find(r=>r.event==='machine-identity'),
 a2Init:a2Gyro.find(r=>r.event==='sensor-initialised'),
 g3Events:events,a2Samples,
 limitations:['G3E export contains no real COM motion sample; none are manufactured.','Replay verifies production decisions from recorded observations, not physical Steam suppression.']};
if(!fixture.g3Identity||!fixture.g3Init||!fixture.a2Init||!a2Samples.length||!events.some(r=>r.phase==='confirmed'))throw Error('Evidence missing for replay');
fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify(fixture,null,2));
console.log(JSON.stringify({output,a2Samples:a2Samples.length,g3Events:events.length,issues:issues.length}));
