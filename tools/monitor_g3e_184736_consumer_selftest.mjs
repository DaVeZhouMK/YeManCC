// Replay only the user's archived unified monitoring packets. No live probes.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {createRequire} from 'node:module';import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=path.resolve(process.env.YMCC_HWINFO_RTSS_MIRROR_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-duplicate-frame-research-20261016'),{build}=createRequire(path.join(root,'package.json'))('esbuild');
const files=['monitorData','monitorHudModel','monitorHudFreshness'];
const mocks={'./ipc':'export const isNativeRuntime=false;export const invoke=()=>{throw Error("Unexpected IPC")};export const on=()=>()=>{};','./api':'export const fs={};export const windowApi={};','./monitorAppliedTdp':'export const advanceMonitorTdpDisplay=()=>{};export const subscribeMonitorTdpState=()=>()=>{};'};
const b=await build({stdin:{resolveDir:root,loader:'ts',contents:files.map(n=>'export * from '+JSON.stringify('./src/bridge/'+n+'.ts')+';').join('\n')},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'no-io',setup(b){b.onResolve({filter:/.*/},a=>mocks[a.path]?{path:a.path,namespace:'mock'}:undefined);b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:mocks[a.path],loader:'ts'}));}}]});
fs.writeFileSync(path.join(out,'g3e-consumer.mjs'),b.outputFiles[0].text);const m=await import(pathToFileURL(path.join(out,'g3e-consumer.mjs')).href);
const rows=JSON.parse(fs.readFileSync(path.join(out,'G3E-REPLAY-INPUT.json'),'utf8')).rows;
let at=0;const noIO=()=>{throw Error('Unexpected IO/timer')};
const hub=m.createMonitorDataHub({now:()=>at,invoke:noIO,readTextFile:noIO,setTimer:noIO,clearTimer(){}});
const stats={admitted:0,healthy:0,unhealthy:0,selectedVram:0,batteryStatus:0,expiredHardwareWithheld:0,measuredFps:0,missingGpuPower:0,missingVoltage:0};
const failures=[];
for(const row of rows){
    at=row.observedAtMs;const raw=row.snapshot,top=JSON.parse(raw.top),fps=raw.fps?JSON.parse(raw.fps):null;
    const view=hub.admitDisplayPublication(raw);assert(view,'real publication rejected: '+at);++stats.admitted;
    const data=m.hudFromUniformSnapshot(view,at,null,false),v=data.values;
    if(raw.hwDown){
        ++stats.unhealthy;
        for(const key of ['cpuPower','cpuTemp','gpuPower','gpuClock','vramUsed','ramUsed','ramUsage','netDown','netUp'])assert.equal(v[key]??null,null,'unhealthy '+key);
    }else{
        ++stats.healthy;
        assert.equal(v.cpuPower,top.cpuPowerValid?top.tdpW:null);
        assert.equal(v.cpuTemp,top.cpuTempValid?top.tempC:null);
        assert.equal(v.ramUsed,top.physicalMemoryUsedMb/1024);assert.equal(v.ramUsage,top.physicalMemoryLoadPct);
        assert.equal(v.vramUsed,top.gpuMemoryUsedMb/1024);assert.equal(v.vramUsage,top.gpuMemoryLoadPct);++stats.selectedVram;
        assert.equal(v.netDown,top.networkDownMBps);assert.equal(v.netUp,top.networkUpMBps);
    }
    assert.equal(v.batteryPercent,96);assert.equal(v.batteryPower,0);assert.equal(v.batteryTime??null,null);++stats.batteryStatus;
    assert.equal(v.gpuPower??null,null);++stats.missingGpuPower;
    assert.equal(v.cpuVoltage??null,null);assert.equal(v.gpuVoltage??null,null);++stats.missingVoltage;
    if(fps&&v.fps!=null){assert.equal(v.fps,fps.fps);assert(v.fps>=0&&v.fps<=1000000);++stats.measuredFps;}
    const expired=m.ageHudData(data,at+65000);
    for(const key of ['cpuPower','cpuTemp','ramUsed','ramUsage','vramUsed','vramUsage','netDown','netUp'])assert.equal(expired.values[key]??null,null,'expired '+key);
    ++stats.expiredHardwareWithheld;
}
const cases=[
    'all 204 complete real publications admitted through the same actual unified hub',
    '203 healthy rows preserve physical RAM, selected GPU VRAM, CPU and network values',
    'the single stale row withholds hardware values rather than using its numeric remnants',
    'AC 96 percent and real zero charging power remain valid; discharge duration stays absent',
    'absent GPU power and voltages stay unknown rather than synthetic estimates',
    'available FPS retains its source value; this replay does not claim Presented acquisition completeness',
    'all 204 held hardware publications expire without freshening source timestamps'
];
assert.equal(stats.admitted,204);assert.equal(stats.healthy,203);assert.equal(stats.unhealthy,1);assert.equal(stats.selectedVram,203);assert.equal(stats.batteryStatus,204);assert.equal(stats.expiredHardwareWithheld,204);
const result={ok:true,checks:cases.length,cases,stats,rawHWiNFORowsAvailable:false,actualUnifiedHubAndModel:true,liveHardwareOrProcessOperations:false,newTimers:false,cpuPercentageMeasured:false,sourceSha256:Object.fromEntries(files.map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge',n+'.ts'))).digest('hex')]))};
fs.writeFileSync(path.join(out,'G3E-CONSUMER-RESULT.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:cases.length,stats},null,2));
