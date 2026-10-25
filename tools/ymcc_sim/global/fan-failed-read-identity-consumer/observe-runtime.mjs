import '../consumer/fan-global/runtime-guard.mjs';
import fs from 'node:fs';import path from 'node:path';
const config=JSON.parse(fs.readFileSync(process.env.YMCC_FAN_GLOBAL_BOUNDARY,'utf8'));
const observation={kind:'unmodified-fan-failed-read-runtime-observation',nodeWindowAbsentAtStart:typeof globalThis.window==='undefined',nodeChromeAbsentAtStart:typeof globalThis.chrome==='undefined',beforeExitObserved:false};
if(!observation.nodeWindowAbsentAtStart||!observation.nodeChromeAbsentAtStart)throw Error('FAILED_READ_UNREVIEWED_NATIVE_BRIDGE');
process.once('beforeExit',code=>{observation.beforeExitObserved=true;observation.exitCode=code;observation.nodeWindowAbsentAtEnd=typeof globalThis.window==='undefined';observation.nodeChromeAbsentAtEnd=typeof globalThis.chrome==='undefined';observation.activeResources=process.getActiveResourcesInfo();fs.writeFileSync(path.join(config.caseRoot,'runtime-observation.json'),JSON.stringify(observation,null,2)+'\n');});
