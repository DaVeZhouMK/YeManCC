import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {extract} from './fixtures/msi_owned_return_retired_io/source-binding.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
const option=(name,fallback)=>{const index=args.indexOf(name);return index<0?fallback:args[index+1];};
const output=path.resolve(option('--output',path.join(repo,'work/native-foreground-latency')));
const hash=data=>crypto.createHash('sha256').update(data).digest('hex');
const files=['native/main.cpp','native/msi_native_foreground_isolation_runtime.h','tools/fixtures/native_foreground_snapshot/fixture.cpp'];
const sources=Object.fromEntries(files.map(file=>[file,fs.readFileSync(path.join(repo,file),'utf8')]));
const identities=files.map(file=>({file,sha256:hash(fs.readFileSync(path.join(repo,file)))}));
const spans=[];
const main=sources[files[0]],runtime=sources[files[1]];
const pick=(text,kind,name,file)=>extract(text,kind,name,file,spans);
const bindings={
    '@@PATH_PARSER@@':pick(main,'function','hidHideParseCommandPaths',files[0]),
    '@@LEGACY_SNAPSHOT@@':pick(main,'function','hidHideCaptureVisibilitySnapshotViaCli',files[0]),
    '@@SNAPSHOT@@':pick(runtime,'function','msiNativeForegroundSnapshot',files[1]),
    '@@LEASE@@':pick(runtime,'struct','MsiNativeForegroundLease',files[1]),
    '@@CYCLE@@':pick(runtime,'function','msiNativeForegroundCycleLocked',files[1]),
    '@@POLL@@':pick(runtime,'function','msiNativeForegroundPollCycleLocked',files[1]),
};
fs.mkdirSync(output,{recursive:true});
const variants=[{name:'Release',flags:'/O2 /MD'},{name:'Checked',flags:'/Od /RTC1 /MDd'},
    {name:'negative-split-query',flags:'/O2 /MD',mutation:'split-query'},
    {name:'negative-blocking-wait',flags:'/O2 /MD',mutation:'blocking-wait'}];
const configurations=[];
for(const variant of variants){
    const directory=path.join(output,variant.name);fs.mkdirSync(directory,{recursive:true});
    let code=sources[files[2]];
    for(const [marker,body] of Object.entries(bindings)){
        let selected=body;
        if(variant.mutation==='split-query'&&marker==='@@SNAPSHOT@@')selected='static bool msiNativeForegroundSnapshot(HidHideVisibilitySnapshot& snapshot) { return hidHideCaptureVisibilitySnapshotViaCli(snapshot); }';
        if(variant.mutation==='blocking-wait'&&marker==='@@CYCLE@@'){
            assert.equal(body.split('WaitForSingleObject(pi.hProcess,0)').length,2);
            selected=body.replace('WaitForSingleObject(pi.hProcess,0)','WaitForSingleObject(pi.hProcess,5000)');
        }
        assert.equal(code.split(marker).length,2);code=code.replace(marker,()=>selected);
    }
    const actual=path.join(directory,'Actual.cpp');fs.writeFileSync(actual,code);
    const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
    const command=calibrationToolchainPreamble(vcvars)+`cl /nologo /std:c++20 /utf-8 /EHsc ${variant.flags} /I"${path.join(repo,'deps/json')}" Actual.cpp /FoFixture.obj /FeFixture.exe\r\nexit /b %errorlevel%\r\n`;
    const build=path.join(directory,'build.cmd');fs.writeFileSync(build,command);
    const built=spawnSync('cmd.exe',['/d','/c',build],{encoding:'utf8',cwd:directory,timeout:60000,windowsHide:true});
    fs.writeFileSync(path.join(directory,'build.log'),(built.stdout??'')+(built.stderr??''));
    assert.equal(built.status,0,'Build failed: '+directory+'/build.log');
    const executed=spawnSync(path.join(directory,'Fixture.exe'),[path.join(directory,'RESULT.json')],{encoding:'utf8',cwd:directory,timeout:10000,windowsHide:true});
    fs.writeFileSync(path.join(directory,'run.log'),(executed.stdout??'')+(executed.stderr??''));
    const result=JSON.parse(fs.readFileSync(path.join(directory,'RESULT.json'),'utf8'));
    if(variant.mutation){
        assert.equal(executed.status,1);
        assert(result.failures.some(name=>variant.mutation==='split-query'?name==='one-query-without-apply':name==='no-lock-held-wait'));
    }else {assert.equal(executed.status,0,executed.stdout+executed.stderr);assert(result.passed);}
    configurations.push({name:variant.name,...result,expectedFailure:Boolean(variant.mutation),sourceSha256:hash(code),binarySha256:hash(fs.readFileSync(path.join(directory,'Fixture.exe')))});
    console.log(variant.name+': '+result.cases+' checks, '+result.failures.length+' failures'+(variant.mutation?' (expected)':''));
}
for(const identity of identities)assert.equal(hash(fs.readFileSync(path.join(repo,identity.file))),identity.sha256,'Source changed during test: '+identity.file);
const result={schema:1,passed:true,status:'PASS',identities,spans,configurations,
    coverage:'Actual snapshot parser, legacy snapshot counterexample, helper launch/wait/poll consumers; all CLI/process/device boundaries are inert.',
    hardwareAccess:false,realProcessLaunches:false,physicalContinuityVerified:false};
fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2)+'\n');
