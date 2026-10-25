// Compile frozen/current production Output; compare every serialized packet.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_FRAME_REUSE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/frame-hardware-object-reuse-20261016').replaceAll('\\','/');
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',results={};
for(const side of process.argv.includes('--before-only')?['before']:['before','after']){
    const dir=out+'/native/'+side;fs.mkdirSync(dir,{recursive:true});
    const source=side==='before'?out+'/before/native/monitor_frame_output.h':root+'/native/monitor_frame_output.h';
    const batch=dir+'/compile.bat';
    fs.writeFileSync(batch,`@echo off\r\ncall "${vc}" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 /DUNICODE /D_UNICODE /DYMCC_FRAME_OUTPUT_HEADER=\\"${source}\\" /I"${root}/native" "${root}/tools/monitor_frame_hardware_reuse_selftest.cpp" /Fo"${dir}/reuse.obj" /Fe"${dir}/reuse.exe"\r\nexit /b %ERRORLEVEL%\r\n`);
    const c=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:3e6});fs.writeFileSync(dir+'/COMPILE.log',(c.stdout||'')+(c.stderr||''));assert.equal(c.status,0,c.stdout+c.stderr);
    const r=spawnSync(dir+'/reuse.exe',[dir+'/RESULT.json'],{encoding:'utf8',windowsHide:true,timeout:15000});fs.writeFileSync(dir+'/RUN.log',(r.stdout||'')+(r.stderr||''));assert.equal(r.status,0,r.stdout+r.stderr);
    results[side]=JSON.parse(fs.readFileSync(dir+'/RESULT.json','utf8'));assert(results[side].ok);
    results[side].sourceSha256=crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex');
    console.log(JSON.stringify({side,checks:results[side].checks,packets:results[side].packetComparisons,workloads:results[side].workloads}));
}
if(results.after){
    assert.equal(results.before.trace.length,results.after.trace.length);
    let proofFieldsAdded=0,expiredHardwareWithdrawals=0,invalidHardwareWithdrawals=0;
    for(let i=0;i<results.after.trace.length;++i){
        const prior=results.before.trace[i],next=results.after.trace[i];
        assert.equal(next.label,prior.label);assert.equal(next.at,prior.at);assert.equal(next.rescue,prior.rescue);
        if(next.packet===null){assert.equal(prior.packet,null);assert.equal(next.packetBytes,prior.packetBytes);continue;}
        assert(prior.packet!==null);
        const expected=structuredClone(prior.packet),actual=structuredClone(next.packet);
        const proof=['hardwareObservedAt','hardwareValidUntil','hardwarePollPeriodMs'];
        if(proof.some(k=>k in actual)){
            assert(proof.every(k=>Number.isSafeInteger(actual[k])&&actual[k]>=0));
            assert(actual.hardwareObservedAt>0&&actual.hardwareValidUntil>actual.hardwareObservedAt);
            const budget=Math.max(5000,Math.min(60000,Math.max(500,actual.hardwarePollPeriodMs))+3000);
            assert(actual.hardwareValidUntil-actual.hardwareObservedAt<=budget);++proofFieldsAdded;
        }
        const changed=actual.gpu!==expected.gpu||actual.packagePower!==expected.packagePower;
        assert.deepEqual(next.hardwareInput,prior.hardwareInput);
        if(changed){
            const expired=actual.hardwareObservedAt>0&&next.at>=actual.hardwareValidUntil;
            if(actual.gpu!==expected.gpu){
                assert.equal(actual.gpu,null);
                assert(expired || (prior.hardwareInput && (prior.hardwareInput.gpu===null || prior.hardwareInput.gpu<0 || prior.hardwareInput.gpu>100)));
                expected.gpu=null;
            }
            if(actual.packagePower!==expected.packagePower){
                assert.equal(actual.packagePower,-1);
                assert(expired || (prior.hardwareInput && (prior.hardwareInput.power===null || prior.hardwareInput.power<0 || prior.hardwareInput.power>=2000)));
                expected.packagePower=-1;
            }
            if(expired)++expiredHardwareWithdrawals;else ++invalidHardwareWithdrawals;
        }
        for(const key of proof)delete actual[key];
        assert.deepEqual(actual,expected,'non-hardware packet field changed at '+next.label);
        // Values above are compared semantically and invalid/expired changes
        // require explicit proof. Preserve exact bytes for every other field.
        const stripHardware=text=>text.replace(/,"hardware(?:ObservedAt|ValidUntil|PollPeriodMs)":-?[0-9]+/g,'').replace(/,"gpu":(?:-?[0-9]+(?:\.[0-9]+)?|null)/,'').replace(/,"packagePower":(?:-?[0-9]+(?:\.[0-9]+)?|null)/,'');
        assert.equal(stripHardware(next.packetBytes),stripHardware(prior.packetBytes),'serialized FPS/LOW/curve/identity changed at '+next.label);
    }
    assert(proofFieldsAdded>0);
    const before=results.before.workloads,after=results.after.workloads;
    assert.equal(before.length,after.length);
    const objectSizeDelta=after[0].outputObjectBytes-before[0].outputObjectBytes;
    assert(objectSizeDelta>=0&&objectSizeDelta<=256);
    for(let i=0;i<after.length;++i){assert.equal(after[i].calls,300);assert.equal(after[i].allocations,0);assert(after[i].allocations<before[i].allocations);assert.equal(after[i].outputObjectBytes-before[i].outputObjectBytes,objectSizeDelta)}
    fs.writeFileSync(out+'/REUSE-RESULT.json',JSON.stringify({ok:true,checks:results.after.checks,packetComparisons:results.after.packetComparisons,serializedFpsLowCurveAndIdentityIdentical:true,independentProofPackets:proofFieldsAdded,expiredHardwareWithdrawals,invalidHardwareWithdrawals,objectSizeDelta,before:before,after:after,sourceSha256:{before:results.before.sourceSha256,after:results.after.sourceSha256},realHardwareOrRtssReads:false,timingOrCpuPercentageMeasured:false},null,2)+'\n');
}


