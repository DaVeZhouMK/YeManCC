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
const rootReturnOnlyFlags=['--runtime-source','--diagnostic-source','--baseline-runtime','--baseline-diagnostic','--variant','--prepare-only'];
if(!['gpd-root-return','gpd-idle-wake'].includes(option('--case',null)))assert(!args.some(arg=>rootReturnOnlyFlags.includes(arg)),'Private source/variant options require an explicit GPD case');
if(args.includes('--case')&&option('--case',null)==='gpd-root-return'){
 const {runGpdRootReturn}=await import('./gpd-root-return.mjs');
 runGpdRootReturn({repo,args,output,extract,calibrationToolchainPreamble,runnerPath:fileURLToPath(import.meta.url)});
}else if(option('--case',null)==='gpd-idle-wake'){
 const {runGpdIdleWake}=await import('./gpd-idle-wake.mjs');
 runGpdIdleWake({repo,args,output,extract,calibrationToolchainPreamble,runnerPath:fileURLToPath(import.meta.url)});
}else if(args.includes('--case')){
 assert.equal(option('--case',null),'helper-exit-query','Unknown or empty foreground selection');
 assert(!args.includes('--power-order')&&args.includes('--output'),'helper-exit-query requires explicit output and exclusive selection');
 assert(args.length===4&&new Set(args.filter((_,i)=>i%2===0)).size===2&&args.filter((_,i)=>i%2===0).every(v=>['--case','--output'].includes(v)),'Unsupported foreground selection combination');
 runHelperExitQuery();
}else if(args.includes('--power-order')){
    runPowerOrder();
}else{
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
    {name:'negative-blocking-wait',flags:'/O2 /MD',mutation:'blocking-wait'},
    {name:'negative-unfenced-submit',flags:'/O2 /MD',mutation:'unfenced-submit'}];
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
        if(variant.mutation==='unfenced-submit'&&marker==='@@CYCLE@@'){
            const gate=/    if\(!powerCurrent\(\)\)return MsiNativeForegroundLease::kPowerDeferred;\r?\n(?=    markIssued\(\);)/g;
            assert.equal([...body.matchAll(gate)].length,1);
            selected=body.replace(gate,'');
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
        const expected={'split-query':['one-query-without-apply'],'blocking-wait':['no-lock-held-wait'],
            'unfenced-submit':['pending-during-helper-preparation','new-sleep-boundary-during-helper-preparation','fresh-generation-cannot-submit-old-helper','same-generation-revocation-cannot-submit-old-helper']}[variant.mutation];
        assert(expected.every(name=>result.failures.includes(name)));
    }else {assert.equal(executed.status,0,executed.stdout+executed.stderr);assert(result.passed);}
    configurations.push({name:variant.name,...result,expectedFailure:Boolean(variant.mutation),sourceSha256:hash(code),binarySha256:hash(fs.readFileSync(path.join(directory,'Fixture.exe')))});
    console.log(variant.name+': '+result.cases+' checks, '+result.failures.length+' failures'+(variant.mutation?' (expected)':''));
}
for(const identity of identities)assert.equal(hash(fs.readFileSync(path.join(repo,identity.file))),identity.sha256,'Source changed during test: '+identity.file);
const result={schema:1,passed:true,status:'PASS',identities,spans,configurations,
    coverage:'Actual snapshot parser, legacy snapshot counterexample, helper launch/wait/poll consumers; all CLI/process/device boundaries are inert.',
    hardwareAccess:false,realProcessLaunches:false,physicalContinuityVerified:false};
fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2)+'\n');
}

function runPowerOrder(){
    const started=Date.now();
    const deadline=Number(process.env.YMCC_POWER_ORDER_DEADLINE_MS||started+480000);
    assert(Number.isFinite(deadline)&&deadline>started,'Original preparation/run budget exhausted');
    const remaining=cap=>{const ms=Math.min(cap,deadline-Date.now());assert(ms>0,'Original preparation/run budget exhausted');return ms;};
    fs.mkdirSync(output,{recursive:true});
    const save=(name,value)=>fs.writeFileSync(path.join(output,name),typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{flag:'wx'});
    const inputFiles=['native/msi_native_foreground_isolation_runtime.h','tools/fixtures/native_foreground_snapshot/fixture.cpp',
        'tools/native_foreground_latency_selftest.mjs','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs',
        'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','deps/json/json.hpp',
        'tools/ymcc_sim/global/controller-foreground-latency/win5-power-field.json','native/sleep_power_state.h','native/main.cpp'];
    const bytes=Object.fromEntries(inputFiles.map(file=>[file,fs.readFileSync(path.join(repo,file))]));
    const identities=inputFiles.map(file=>({file,bytes:bytes[file].length,sha256:hash(bytes[file])}));
    const runtime=bytes[inputFiles[0]].toString('utf8'),spans=[];
    const take=name=>extract(runtime,'function',name,inputFiles[0],spans);
    const main=bytes[inputFiles[8]].toString('utf8');
    const fromMain=(name,kind='function')=>extract(main,kind,name,inputFiles[8],spans);
    const drain=fromMain('inputHostRunQueuedLifecycleWork');
    const pop=drain.match(/reason = std::move\(g_inputHostLifecycleWorkQueue\.front\(\)\.reason\);[\s\S]*?g_inputHostLifecycleWorkQueue\.pop_front\(\);/)[0];
    const dispatch=drain.match(/if \(reason == "msi-native-foreground"\) \{\s*msiNativeForegroundHandleWork\(foregroundReady\?&\*foregroundReady:nullptr\);\s*\}/)[0];
    const queueForward=main.match(/^static bool inputHostQueueLifecycleWorkInternal\([^;]+;/m)[0];
    // Keep source offsets while masking the default initializers in the declaration.
    const queueDefinition=extract(main.replace(queueForward,queueForward.replace(/[{}]/g,' ')),
        'function','inputHostQueueLifecycleWorkInternal',inputFiles[8],spans);
    const field=JSON.parse(bytes[inputFiles[6]].toString('utf8'));
    assert.equal(field.canonicalTestId,'controller.native-foreground-latency');
    assert.equal(field.archive.sha256,'b8930e6025774d57be6c1d1e7c5e843e2710cec9b7669e524e36a095ae54fd16');
    assert.deepEqual(field.observations.map(row=>[row.event,row.generation,row.phase,row.line]),[
        [4,9892,'ready',92],[18,9893,'suspending',97],[7,9893,'suspending',98],
        [4,9893,'resuming',101],[18,9893,'ready',118],[7,9893,'ready',120]]);
    const phaseNames={ready:'Ready',suspending:'Suspending',suspended:'Suspended',resuming:'Resuming'};
    const fieldBinding='static const std::vector<PowerFieldObservation> kWin5PowerObservations={\n'+field.observations.map(row=>
        `    {${row.event},${row.generation},PowerLifecycle::${phaseNames[row.phase]},${row.line}}`).join(',\n')+'\n};';
    const bindings={
        '@@POWER_FAMILY@@':take('nativeForegroundFamilyAllowed'),
        '@@QUEUE_TYPES@@':['MsiNativeForegroundReadyWork','InputHostLifecycleWork'].map(name=>fromMain(name,'struct')).join('\n'),
        '@@AUDIT_APPLIES@@':fromMain('inputHostSwitchAuditApplies'),
        '@@QUEUE_FUNCTIONS@@':'static bool msiNativeForegroundReadyWorkCurrent(const MsiNativeForegroundReadyWork&) noexcept;\n'+queueForward+'\n'+
            ['inputHostQueueLifecycleWork','inputHostQueueMsiNativeForegroundReady','inputHostSameForegroundReady','inputHostSleepRepairWork'].map(name=>fromMain(name)).join('\n')+'\n'+queueDefinition,
        '@@QUEUE_POP@@':pop,
        '@@QUEUE_DISPATCH@@':dispatch,
        '@@POWER_QUEUE@@':['msiNativeForegroundReadyWorkCurrent','msiNativeForegroundDeferRevokedReady','msiNativeForegroundQueue'].map(take).join('\n'),
        '@@POWER_UI@@':take('msiNativeForegroundUiEvent'),
        '@@POWER_EVENT@@':['msiNativeForegroundHoldPowerLocked','msiNativeForegroundPowerEvent'].map(take).join('\n'),
        '@@POWER_RELEASE@@':take('msiNativeForegroundTryReleasePowerHold'),
        '@@POWER_READY@@':['msiNativeForegroundContinueDeferredReady','msiNativeForegroundSharedPowerReady','msiNativeForegroundReadyWorkAdmitted'].map(take).join('\n'),
        '@@POWER_FIELD@@':fieldBinding,
    };
    const maintenance=take('msiNativeForegroundMaintenance');
    assert(maintenance.includes('(void)msiNativeForegroundTryReleasePowerHold();'),'Original maintenance hook missing');
    assert(runtime.includes('static std::mutex g_msiNativeForegroundPowerMx;'),'Shared production scalar mutex missing');
    assert(bindings['@@POWER_EVENT@@'].includes('std::unique_lock powerLock(g_msiNativeForegroundPowerMx);'),'PowerEvent scalar transaction missing');
    assert(bindings['@@POWER_RELEASE@@'].includes('std::unique_lock powerLock(g_msiNativeForegroundPowerMx,std::try_to_lock);'),'TryRelease must defer when busy');
    for(const marker of ['@@POWER_EVENT@@','@@POWER_RELEASE@@'])
        assert(/powerLock\.unlock\(\);\s*msiNativeForegroundUiEvent\(true(?:,false,&power)?\);/.test(bindings[marker]),'UI terminal must run after unlocking');
    const sharedCommitPredicate='if(!sharedCommit)return false;';
    assert.equal(bindings['@@POWER_RELEASE@@'].split(sharedCommitPredicate).length,2,'Exact final admission mutation anchor');
    // Select one existing cpp branch before generation: latency consumers are
    // neither extracted nor compiled in this explicitly directed invocation.
    const template=bytes[inputFiles[1]].toString('utf8');
    const start=template.indexOf('#ifdef YMCC_NATIVE_FOREGROUND_POWER_ORDER\n');
    const startCrlf=template.indexOf('#ifdef YMCC_NATIVE_FOREGROUND_POWER_ORDER\r\n');
    const branchStart=start>=0?start:startCrlf;
    assert(branchStart>=0);
    const branchEnd=template.indexOf('#else',branchStart);
    assert(branchEnd>branchStart);
    const base=template.slice(0,branchEnd)+'\n#endif\n';
    const configurations=[];
    save('runtime-snapshot.h',bytes[inputFiles[0]].toString('utf8'));
    save('fixture-snapshot.cpp',template);
    save('runner-snapshot.mjs',bytes[inputFiles[2]].toString('utf8'));
    save('field-input.json',field);
    save('INPUT.json',{id:'controller.native-foreground-latency',mode:'power-order',startedAt:new Date(started).toISOString(),deadlineMs:deadline,identities,spans,
        defaultLatency:'NOT_RUN',fullMode:'NOT_RUN',hardwareAccess:false,maintenanceHook:'source-inspected; full maintenance not executed'});
    try{
        // Keep the original FAIL raw; a compile error can never kill this mutation.
        for(const variant of [{name:'before-old-generation-only',mutation:true},{name:'unbound-retry',queueMutation:true},{name:'current'}]){
            const directory=path.join(output,variant.name);fs.mkdirSync(directory);
            let code=base;
            for(const [marker,body] of Object.entries(bindings)){
                const selected=variant.mutation&&marker==='@@POWER_RELEASE@@'?body.replace(sharedCommitPredicate,
                    'if(!sharedCommit || generation==g_msiNativeForegroundSuspendedGeneration.load(std::memory_order_acquire))return false;'):
                    variant.queueMutation&&marker==='@@POWER_QUEUE@@'?body.replace('try {if(inputHostQueueMsiNativeForegroundReady(*ready))return;}','try {if(inputHostQueueLifecycleWork("msi-native-foreground"))return;}'):body;
                assert.equal(code.split(marker).length,2);code=code.replace(marker,()=>selected);
            }
            assert(!code.includes('@@'),'Unbound power-only source');
            fs.writeFileSync(path.join(directory,'Actual.cpp'),code,{flag:'wx'});
            const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
            fs.writeFileSync(path.join(directory,'sleep_power_state.h'),bytes[inputFiles[7]],{flag:'wx'});
            const command=calibrationToolchainPreamble(vcvars)+`for %%i in (cl.exe) do @echo YMCC_CL=%%~$PATH:i\r\nfor %%i in (link.exe) do @echo YMCC_LINK=%%~$PATH:i\r\ncl /nologo /std:c++20 /utf-8 /EHsc /Od /MD /DYMCC_NATIVE_FOREGROUND_POWER_ORDER /I"${path.join(repo,'deps/json')}" Actual.cpp /FoFixture.obj /FeFixture.exe\r\nexit /b %errorlevel%\r\n`;
            const build=path.join(directory,'build.cmd');fs.writeFileSync(build,command,{flag:'wx'});
            const invoke=(program,argv,cap,phase)=>{
                const begin=Date.now(),timeout=remaining(cap);
                const result=spawnSync(program,argv,{encoding:'utf8',cwd:directory,timeout,windowsHide:true});
                const receipt={program,argv,elapsedMs:Date.now()-begin,timeoutMs:timeout,exitCode:result.status,signal:result.signal,
                    error:result.error?{code:result.error.code,message:result.error.message}:null};
                fs.writeFileSync(path.join(directory,phase+'.log'),(result.stdout??'')+(result.stderr??''),{flag:'wx'});
                fs.writeFileSync(path.join(directory,phase+'.json'),JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});
                return result;
            };
            const built=invoke(process.env.ComSpec||'C:/Windows/System32/cmd.exe',['/d','/c',build],60000,'build');
            assert.equal(built.status,0,'Build failed: '+directory+'/build.log');
            const toolPaths=[...built.stdout.matchAll(/^YMCC_(?:CL|LINK)=(.+)\r?$/gm)].map(match=>match[1].trim());
            assert.equal(toolPaths.length,2,'Actual compiler/linker selection missing');
            fs.writeFileSync(path.join(directory,'tools.json'),JSON.stringify({tools:[process.execPath,vcvars,...toolPaths].map(file=>({file,sha256:hash(fs.readFileSync(file))})),
                pathCharacters:(process.env.PATH||'').length,fullToolClosureVerified:false,cacheEligible:false},null,2)+'\n',{flag:'wx'});
            const executed=invoke(path.join(directory,'Fixture.exe'),[path.join(directory,'RESULT.json'),...(variant.mutation?['old-generation-only']:variant.queueMutation?['queue-only']:[])],10000,'run');
            const result=JSON.parse(fs.readFileSync(path.join(directory,'RESULT.json'),'utf8'));
            assert.equal(result.mode,'power-order');assert.equal(result.caseNames.length,result.cases);
            if(variant.mutation){
                assert.equal(executed.status,1,'Mutation must reach a semantic failure');
                assert.equal(result.passed,false);
                assert.deepEqual(result.scenarios,['msi:late-suspend-same-generation']);
                assert.deepEqual(result.failures,['msi:late-suspend-same-generation:released']);
                const fieldExecuted=invoke(path.join(directory,'Fixture.exe'),[path.join(directory,'FIELD-RESULT.json'),'field-replay-only'],10000,'field-run');
                const fieldResult=JSON.parse(fs.readFileSync(path.join(directory,'FIELD-RESULT.json'),'utf8'));
                assert.equal(fieldExecuted.status,1,'Original gate must fail the recorded Win5 sequence');
                assert.equal(fieldResult.passed,false);assert.equal(fieldResult.cases,6);
                assert.deepEqual(fieldResult.scenarios,['gpd:field-win5-111926-notification-order']);
                assert.deepEqual(fieldResult.failures,['gpd:field-win5-111926-notification-order:step-4-ready-releases',
                    'gpd:field-win5-111926-notification-order:step-5-duplicate-is-inert']);
                assert.deepEqual(fieldResult.fieldTrace.map(row=>row.sourceLine),field.observations.map(row=>row.line));
                result.fieldReplayCounterexample=fieldResult;
            }else if(variant.queueMutation){
                assert.equal(executed.status,1);assert.equal(result.cases,3);
                assert.deepEqual(result.failures,['msi:retry-queue-revocation:stale-retry-refused','msi:retry-queue-revocation:zero-action-deferred']);
            }else{
                assert.equal(executed.status,0);assert.equal(result.passed,true);assert.deepEqual(result.failures,[]);
                const scenarios=['ready-same-generation','cross-generation','late-suspend-same-generation','resume-without-suspended-owner','duplicate-notifications','input-not-ready',
                    'power-not-ready','maintenance-observes-transition','two-rounds-no-transition-credit','shared-ready-required','power-mutex-defers-new-suspend',
                    'missing-resume-new-commit','missing-resume-old-commit','missing-resume-wrong-generation','missing-resume-zero-commit',
                    'field-win5-111926-notification-order','repeated-sleep-wake-cycles','shared-commit-waits-input',
                    'next-suspend-rebases-commit','shared-commit-waits-power'];
                assert.deepEqual(result.scenarios,['msi','rog','gpd'].flatMap(family=>scenarios.map(scenario=>family+':'+scenario)).concat('unsupported:no-business',
                    ['retry-queue-revocation','dequeued-ready-revocation','raw-suspend-before-public-latch','new-ready-during-old-ui-read','bounded-allocation-failure','queue-must-not-coalesce-across-power'].map(scenario=>'msi:'+scenario)));
                assert.equal(result.cases,195);
                assert.deepEqual(result.fieldTrace.map(row=>row.family+':'+row.sourceLine),
                    ['msi','rog','gpd'].flatMap(family=>field.observations.map(row=>family+':'+row.line)));
            }
            const expectedFailure=Boolean(variant.mutation||variant.queueMutation);
            configurations.push({name:variant.name,...result,expectedFailure,sourceSha256:hash(code),binarySha256:hash(fs.readFileSync(path.join(directory,'Fixture.exe')))});
            console.log(variant.name+': '+result.cases+' checks, '+result.failures.length+' failures'+(expectedFailure?' (expected)':''));
        }
    }finally{
        const closing=inputFiles.map(file=>({file,sha256:hash(fs.readFileSync(path.join(repo,file)))}));
        const drift=closing.filter(item=>item.sha256!==identities.find(before=>before.file===item.file).sha256);
        save('CLOSING.json',{elapsedMs:Date.now()-started,deadlineMs:deadline,closing,parallelDrift:drift,configurationsCompleted:configurations.length});
        assert.equal(drift.length,0,'Concurrent source drift; snapshot results retained, current-source acceptance incomplete');
    }
    save('result.json',{schema:1,id:'controller.native-foreground-latency',mode:'power-order',status:'PASS',passed:true,identities,spans,configurations,
        elapsedMs:Date.now()-started,defaultLatency:'NOT_RUN',fullMode:'NOT_RUN',hardwareAccess:false,realProcessLaunches:false,
        coverage:'Extracted power consumers, lifecycle queue insertion, pop and foreground dispatch branch with controlled power facts and inert action terminal. No device writer, full maintenance, or physical continuity proof.'});
}

function runHelperExitQuery(){
 const selected=option('--case',null);assert.equal(selected,'helper-exit-query');
 fs.mkdirSync(output,{recursive:true});const save=(name,value)=>fs.writeFileSync(path.join(output,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 const files=['native/msi_native_foreground_isolation_runtime.h','tools/fixtures/native_foreground_snapshot/helper-exit-query.cpp.in','tools/native_foreground_latency_selftest.mjs','deps/json/json.hpp','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
 const bytes=Object.fromEntries(files.map(f=>[f,fs.readFileSync(path.join(repo,f))]));const runtime=bytes[files[0]].toString('utf8'),template=bytes[files[1]].toString('utf8'),spans=[];
 const poll=extract(runtime,'function','msiNativeForegroundPollCycleLocked',files[0],spans),cycle=extract(runtime,'function','msiNativeForegroundCycleLocked',files[0],spans),receipt=extract(runtime,'function','msiNativeForegroundReceipt',files[0],spans);
 const state=runtime.match(/^    bool helperExitQueryAttempted=false,helperExitQuerySucceeded=false;\r?\n    DWORD helperExitQueryError=0;/m);assert(state);
 const reset=cycle.match(/s\.helperExitQueryAttempted=false;s\.helperExitQuerySucceeded=false;s\.helperExitQueryError=0;/);assert(reset);
 const direct=cycle.slice(cycle.indexOf('    DWORD exitCode=1;'),cycle.indexOf('    msiNativeForegroundSettleHelperDiagnosticLocked(exitCode);'));assert(direct.includes('helperExitQuerySucceeded=GetExitCodeProcess'));
 const jsonStart=receipt.indexOf('{"helperExitQuery",'),jsonEnd=receipt.indexOf('\n        {"helperDiagnostic",',jsonStart);assert(jsonStart>=0&&jsonEnd>jsonStart);
 const jsonMember=receipt.slice(jsonStart,jsonEnd).trim().replace(/,$/,'');
 const bindings={'@@QUERY_STATE@@':state[0],'@@QUERY_RESET@@':reset[0],'@@QUERY_DIRECT@@':direct,'@@QUERY_JSON@@':jsonMember,'@@POLL@@':poll};
 let generated=template;for(const [marker,body] of Object.entries(bindings)){assert.equal(generated.split(marker).length,2,marker);generated=generated.replace(marker,()=>body);}assert(!generated.includes('@@'));
 const capture='s.helperExitQueryAttempted=true;\n    s.helperExitQuerySucceeded=GetExitCodeProcess(s.cycleProcess,&code)!=FALSE;\n    s.helperExitQueryError=s.helperExitQuerySucceeded?ERROR_SUCCESS:GetLastError();\n    const bool ok=s.helperExitQuerySucceeded && code==0;';
 const normalized=generated.replace(/\r\n/g,'\n');assert.equal(normalized.split(capture).length,2);
 const missing=normalized.replace(capture,'const bool ok=GetExitCodeProcess(s.cycleProcess,&code)!=FALSE && code==0;');
 const expected=[...template.matchAll(/check\("([^"]+)"/g)].map(m=>m[1]);assert.equal(new Set(expected).size,expected.length);
 const result={id:'controller.native-foreground-latency',mode:selected,status:'RUNNING',defaultLatencyExecuted:false,powerOrderExecuted:false,sourceInputs:files.map(f=>({file:f,bytes:bytes[f].length,sha256:hash(bytes[f])})),spans,configurations:[]};
 fs.writeFileSync(path.join(output,'INPUT.json'),JSON.stringify({...result,expectedCaseNames:expected},null,2)+'\n',{flag:'wx'});
 try{
  for(const variant of [{name:'current-query-evidence',code:generated},{name:'missing-query-capture',code:missing,expectedFailure:'terminal-zero-query-provenance'}]){
   const d=path.join(output,variant.name);fs.mkdirSync(d);fs.writeFileSync(path.join(d,'Actual.cpp'),variant.code,{flag:'wx'});
   const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
   const command=calibrationToolchainPreamble(vcvars)+`cl /nologo /std:c++20 /utf-8 /EHsc /O2 /MD /showIncludes /sourceDependencies dependencies.json /I"${path.join(repo,'deps/json')}" Actual.cpp /FoFixture.obj /FeFixture.exe /link /VERBOSE:LIB /INCREMENTAL:NO\r\nexit /b %errorlevel%\r\n`;
   fs.writeFileSync(path.join(d,'build.cmd'),command,{flag:'wx'});
   const built=spawnSync('cmd.exe',['/d','/c',path.join(d,'build.cmd')],{cwd:d,encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(d,'build.log'),(built.stdout||'')+(built.stderr||''),{flag:'wx'});assert.equal(built.status,0,'Build failed '+d);
   const ran=spawnSync(path.join(d,'Fixture.exe'),[],{cwd:d,encoding:'utf8',windowsHide:true,timeout:10000});fs.writeFileSync(path.join(d,'run.log'),(ran.stdout||'')+(ran.stderr||''),{flag:'wx'});const report=JSON.parse(ran.stdout);
   if(variant.expectedFailure){assert.equal(ran.status,1);assert.equal(report.failure,variant.expectedFailure);assert.deepEqual(report.caseNames,expected.slice(0,expected.indexOf(variant.expectedFailure)+1));}
   else{assert.equal(ran.status,0);assert(report.passed);assert.deepEqual(report.caseNames,expected);}
   result.configurations.push({name:variant.name,exitCode:ran.status,expectedFailure:variant.expectedFailure||null,report,sourceSha256:hash(variant.code),binarySha256:hash(fs.readFileSync(path.join(d,'Fixture.exe')))});
  }
  for(const f of files)assert.equal(hash(fs.readFileSync(path.join(repo,f))),hash(bytes[f]),'source drift '+f);
  result.status='PASS_HELPER_QUERY_EVIDENCE';save('result.json',result);
 }catch(error){save('FAIL.json',{...result,error:error.stack});throw error;}
 console.log(JSON.stringify({status:result.status,checks:result.configurations.map(q=>q.report.checks)}));
}
