// Freeze/current regression of the actual ordinary-worker RTSS sampling branch.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=process.cwd().replaceAll('\\','/');
const out=path.resolve(process.env.YMCC_RTSS_RESCUE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/rtss-rescue-without-curve-20261016').replaceAll('\\','/');
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const warm=process.argv.includes('--warm-standby');
const summary={ok:true,warmStandby:warm,realHardwareOrRtssReads:false,sides:{}};
const sides=process.argv.includes('--before-only')?['before']:process.argv.includes('--current-only')?['after']:['before','after'];
for(const side of sides){
    const source=side==='before'?out+'/before':root;
    const dir=out+'/worker/'+side;fs.mkdirSync(dir,{recursive:true});
    const main=fs.readFileSync(source+'/native/main.cpp','utf8');
    const packetBoundary=main.includes('            monitorFpsText = frameOutput.packetTextView(nowMs);')?'            monitorFpsText = frameOutput.packetTextView(nowMs);':main.includes('            const json& fpsJson=frameOutput.packetView(nowMs);')?'            const json& fpsJson=frameOutput.packetView(nowMs);':'            const json fpsJson=frameOutput.packet(nowMs);';
    const end=main.indexOf(packetBoundary);
    assert(end>=0,'ordinary branch packet boundary not found');
    const start=main.lastIndexOf('            if (',end);
    assert(start>=0&&end>start,'ordinary sampling branch not found');
    const branch=main.slice(start,end);assert.equal((branch.match(/sampleRtss\(/g)||[]).length,1);
    assert(branch.length<700,'unexpected expansion of the worker sampling branch');
    const template=fs.readFileSync(root+'/tools/monitor_rtss_rescue_worker_selftest.cpp','utf8');
    const test=template.replace('        // YMCC_WORKER_SAMPLING_BRANCH',branch.trimEnd());
    assert.notEqual(test,template,'worker branch was not inserted');
    // Generated source has absolute include paths, so all header implementations
    // are compiled from exactly the chosen frozen/current source trees.
    const testFile=dir+'/worker.cpp';
    fs.writeFileSync(testFile,test.replace('"../deps/json/json.hpp"','"'+root+'/deps/json/json.hpp"'));
    fs.writeFileSync(dir+'/WORKER-BRANCH.cpp',branch);
    const def=n=>'/D'+n+'=\\"'+source+'/native/'+(n==='YMCC_RTSS_HEADER'?'monitor_rtss_frame.h':'monitor_frame_output.h')+'\\"';
    const batch=dir+'/compile.bat';
    fs.writeFileSync(batch,calibrationToolchainPreamble(vc)+`cl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 /DUNICODE /D_UNICODE ${warm?'/DYMCC_RTSS_WARM_STANDBY ':''}${def('YMCC_RTSS_HEADER')} ${def('YMCC_FRAME_OUTPUT_HEADER')} "${testFile}" /Fo"${dir}/worker.obj" /Fe"${dir}/worker.exe"\r\nexit /b %ERRORLEVEL%\r\n`);
    const compiled=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:3e6});
    fs.writeFileSync(dir+'/COMPILE.log',(compiled.stdout||'')+(compiled.stderr||''));assert.equal(compiled.status,0,compiled.stdout+compiled.stderr);
    const ran=spawnSync(dir+'/worker.exe',[dir+'/RESULT.json'],{encoding:'utf8',windowsHide:true,timeout:10000});
    fs.writeFileSync(dir+'/RUN.log',(ran.stdout||'')+(ran.stderr||''));
    const result=JSON.parse(fs.readFileSync(dir+'/RESULT.json','utf8'));
    if(side==='before'&&warm){
        assert.equal(ran.status,1,'cold standby baseline must fail behavior');
        for(const name of ['active game warms RTSS once per ordinary iteration with curve off','primary expiry has an already observed RTSS fallback without a cold-read gap'])assert(result.cases.some(c=>c.name===name&&!c.ok),name);
    }else if(side==='before'){
        assert.equal(ran.status,1,'frozen source must reproduce the missing-rescue failure');
        assert(result.cases.some(c=>c.name==='curve-off absent hardware emits the genuine fallback FPS'&&!c.ok));
        assert(result.cases.some(c=>c.name==='300 curve-off missing-primary iterations each read RTSS once'&&!c.ok));
        assert.equal(result.operationCounts.healthyCurveOffRtssReads,0);
        assert.equal(result.operationCounts.missingPrimaryCurveOffRtssReads,0);
    }else{
        assert.equal(ran.status,0,ran.stdout+ran.stderr);assert.equal(result.ok,true);
        fs.writeFileSync(out+'/FRAMES-RESULT.json',JSON.stringify({ok:true,after:result,realHardwareReads:false,realRtssMappingReads:false},null,2)+'\n');
    }
    summary.sides[side]={...result,sourceHashes:{output:sha(fs.readFileSync(source+'/native/monitor_frame_output.h')),samplingBranch:sha(Buffer.from(branch))},exitCode:ran.status};
    console.log(JSON.stringify({side,checks:result.checks,failed:result.cases.filter(c=>!c.ok).length,operationCounts:result.operationCounts}));
}
fs.writeFileSync(out+'/WORKER-RESULT.json',JSON.stringify(summary,null,2)+'\n');

