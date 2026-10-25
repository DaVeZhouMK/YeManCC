import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
const outputIndex=args.indexOf('--output');
const output=path.resolve(outputIndex>=0?args[outputIndex+1]:path.join(repo,'work/shield-foreground-field'));
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const productFiles=['native/main.cpp','native/native_foreground_source_policy.h','native/msi_foreground_isolation_runtime.h',
    'native/msi_native_foreground_isolation_runtime.h','native/one_shot_helper_diagnostic.h','InputHost/MsiNativePortCycle.cs'];
const identities=productFiles.map(file=>({file,sha256:hash(fs.readFileSync(path.join(repo,file)))}));
fs.mkdirSync(output,{recursive:true});
const workspaceParent=path.resolve(repo,'../../../Testing/YMCC/Evidence/Build');
fs.mkdirSync(workspaceParent,{recursive:true});
const workspace=fs.mkdtempSync(path.join(workspaceParent,'S30-'));
const result={schema:1,passed:false,status:'started',identities,configurations:[],hardwareAccess:false,
    productMainExecuted:false,realSteamOrSleep:false,fullPersistentAcceptance:false,workspace,
    lifecycleComponent:{featureId:'controller.virtual-input',parentAdapterId:'controller.signal-evidence',
        persistence:'persistent',role:'foreground-visibility-component'},
    sourceHashes:Object.fromEntries(identities.map(row=>[row.file,row.sha256]))};
const save=()=>fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2)+'\n');
save();
process.on('uncaughtExceptionMonitor',error=>{result.status='failed';result.error=String(error.message);save();});
function execute(label,command,argumentsList,cwd,timeout){
    const run=spawnSync(command,argumentsList,{cwd,encoding:'utf8',windowsHide:true,timeout});
    fs.writeFileSync(path.join(output,label+'.log'),(run.stdout??'')+(run.stderr??'')+(run.error?'\n'+run.error:''));
    return run;
}
const positive=path.join(workspace,'Release');
const build=execute('Release-build','pwsh',['-NoProfile','-File',path.join(repo,'tools/verify-msi-foreground-isolation.ps1'),'-OutputRoot',positive],repo,240000);
assert.equal(build.status,0,'Actual production TU: '+path.join(output,'Release-build.log'));
const summary=JSON.parse(fs.readFileSync(path.join(positive,'fixture-data/msi-foreground-native-summary.json'),'utf8').replace(/^\uFEFF/,''));
assert.equal(summary.failures,0);result.configurations.push({name:'Release',...summary});save();
const libs=['user32','gdi32','shell32','shlwapi','ole32','oleaut32','dwmapi','winhttp','advapi32','shcore','version','psapi','PowrProf','ws2_32'].map(name=>name+'.lib').join(' ');
for(const name of ['rog-hid-mutation-quarantine-disabled','original-xusb-and-journal','duplicate-shared-node-refresh']){
    const directory=path.join(workspace,name);fs.mkdirSync(directory,{recursive:true});
    const snapshot=path.join(directory,'source-snapshot');fs.cpSync(path.join(positive,'source-snapshot'),snapshot,{recursive:true});
    if(name==='rog-hid-mutation-quarantine-disabled'){
        const policy=path.join(snapshot,'native/native_foreground_source_policy.h');
        const source=fs.readFileSync(policy,'utf8');
        const begin=source.indexOf('inline bool gameNodeRefreshMutationAllowed(');
        const end=source.indexOf('inline bool xusbTargetAdmitted(',begin);
        assert(begin>=0&&end>begin);
        fs.writeFileSync(policy,source.slice(0,begin)+'inline bool gameNodeRefreshMutationAllowed(Profile,const std::string&,const std::string&) {return true;}\n'+source.slice(end));
    }else if(name==='original-xusb-and-journal'){
        const policy=path.join(snapshot,'native/native_foreground_source_policy.h');
        const source=fs.readFileSync(policy,'utf8');assert.equal(source.split('profile==Profile::Rog && target.compare').length,2);
        fs.writeFileSync(policy,source.replace('profile==Profile::Rog && target.compare','false && target.compare'));
        const runtime=path.join(snapshot,'native/msi_foreground_isolation_runtime.h');
        const managed=fs.readFileSync(runtime,'utf8');assert.equal(managed.split('state.journalWait=job;').length,2);
        fs.writeFileSync(runtime,managed.replace('state.journalWait=job;','(void)job;'));
    }else{
        const runtime=path.join(snapshot,'native/msi_native_foreground_isolation_runtime.h');
        const source=fs.readFileSync(runtime,'utf8');
        const begin=source.indexOf('            for(auto& target:lease.refreshTargets)if(ascii_lower(target.source.deviceInstanceId)');
        const end=source.indexOf('            if(!duplicate)',begin);assert(begin>=0&&end>begin);
        fs.writeFileSync(runtime,source.slice(0,begin)+'            for(const auto& target:lease.refreshTargets)if(target.xusb && ascii_lower(target.source.deviceInstanceId)==ascii_lower(source.deviceInstanceId))duplicate=true;\n'+source.slice(end));
    }
    const command=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+
        `cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1 /c "${snapshot}/tools/msi_foreground_isolation_native_regression.cpp" /I"${repo}/deps/webview2/build/native/include" /I"${snapshot}/deps/json" /I"%WindowsSdkDir%Include/%WindowsSDKVersion%/cppwinrt" /I"${snapshot}/native" /FoFixture.obj\r\nif errorlevel 1 exit /b 1\r\n`+
        `cl /nologo Fixture.obj "${positive}/dsu.obj" "${positive}/steam-recovery.obj" /FeFixture.exe /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "${repo}/deps/webview2/build/native/x64/WebView2LoaderStatic.lib" WindowsApp.lib ${libs}\r\nexit /b %errorlevel%\r\n`;
    fs.writeFileSync(path.join(directory,'build.cmd'),command);
    const compiled=execute(name+'-build','cmd.exe',['/d','/c',path.join(directory,'build.cmd')],directory,240000);
    assert.equal(compiled.status,0,name+' compile must succeed');
    const run=execute(name+'-run',path.join(directory,'Fixture.exe'),[path.join(directory,'fixture-data')],directory,30000);
    assert.equal(run.status,3,name+' original bug must fail');
    const failures=(run.stdout??'').split(/\r?\n/).filter(line=>line.startsWith('FAIL '));
    if(name==='rog-hid-mutation-quarantine-disabled'){
        assert(failures.some(line=>line.includes('native shielding never hides or refreshes quarantined HID game node')));
    }else if(name==='original-xusb-and-journal'){
        assert(failures.some(line=>line.includes('field ROGX active HID XUSB')));
        assert(failures.some(line=>line.includes('missing journal retains only')));
        assert(failures.some(line=>line.includes('journal continuation executes')));
    }else assert(failures.some(line=>line.includes('shared devnode refreshes exactly once')));
    result.configurations.push({name,expectedFailure:true,exitCode:run.status,failures});save();
    console.log(name+': caught '+failures.length+' failures');
}
const helper=execute('helper','node',[path.join(repo,'tools/native_game_hid_caps_selftest.mjs'),'--output',path.join(output,'helper')],repo,180000);
assert.equal(helper.status,0,'Actual helper: '+path.join(output,'helper.log'));
result.helper=JSON.parse(fs.readFileSync(path.join(output,'helper/RESULT.json'),'utf8'));
result.currentSourceChanges=identities.filter(row=>hash(fs.readFileSync(path.join(repo,row.file)))!==row.sha256);
const compiled=path.join(positive,'source-snapshot');
result.frozenSourceIdentities=productFiles.filter(file=>file.startsWith('native/')).map(file=>({file,sha256:hash(fs.readFileSync(path.join(compiled,file)))}));
for(const row of result.frozenSourceIdentities)assert.equal(row.sha256,result.sourceHashes[row.file],'Compiled snapshot identity must match captured input: '+row.file);
assert(!result.currentSourceChanges.some(row=>row.file!=='native/main.cpp'),'Shield consumer changed while running');
result.cases=result.configurations.map(row=>({id:row.name,passed:row.name==='Release'?row.failures===0:row.expectedFailure&&row.failures.length>0}));
result.cases.push(...result.helper.configurations.map(row=>({id:'helper-'+row.configuration,passed:row.passed===true||row.expectedFailure===true&&row.detectedFailures>0})));
result.passed=true;result.status='PASS';save();
console.log('foreground shield: '+summary.cases+' native checks, helper Release/Checked and original failure variants PASS');
