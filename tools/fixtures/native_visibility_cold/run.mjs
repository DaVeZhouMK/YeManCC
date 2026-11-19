import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {calibrationToolchainPreamble} from '../hc_reviewed_calibration/toolchain.mjs';

// Called by the existing foreground-shield adapter. Actual cold consumer and
// durable Session/store run; only OS/device effect ports in fixture.cpp are inert.
export function runNativeVisibilityCold({repo,workspace,output,execute,selection='candidate-selected'}) {
    if(selection!=='candidate-selected'){validateColdEmptyTailSelection(selection);return runEmptyTailSelected({repo,workspace,output,execute,selection});}
    const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    const directory=path.join(workspace,'cold'),snapshot=path.join(directory,'source');
    fs.mkdirSync(snapshot,{recursive:true});
    fs.cpSync(path.join(repo,'native'),path.join(snapshot,'native'),{recursive:true});
    fs.mkdirSync(path.join(snapshot,'deps/json'),{recursive:true});
    fs.copyFileSync(path.join(repo,'deps/json/json.hpp'),path.join(snapshot,'deps/json/json.hpp'));
    fs.copyFileSync(path.join(repo,'tools/fixtures/native_visibility_cold/fixture.cpp'),path.join(snapshot,'fixture.cpp'));
    const header=fs.readFileSync(path.join(snapshot,'native/native_visibility_cold_runtime.h'),'utf8');
    const anchor='    const Context context{currentPowerGeneration(),g_inputHostVirtualIntentRevision.load(std::memory_order_acquire),';
    assert.equal(header.split(anchor).length,2,'Unique actual cold Context anchor');
    fs.writeFileSync(path.join(snapshot,'candidate_cold.h'),header,{flag:'wx'});
    fs.writeFileSync(path.join(snapshot,'baseline_cold.h'),header.replace(anchor,'    pending=true; // historical false debt before live-context rejection\n'+anchor),{flag:'wx'});
    const pins=['candidate_cold.h','baseline_cold.h','fixture.cpp'].map(relative=>({path:relative,sha256:hash(path.join(snapshot,relative))}));
    const argumentsList=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/Od','/MD',
        '/I'+snapshot,'/I'+path.join(snapshot,'native'),'/I'+path.join(snapshot,'deps/json'),path.join(snapshot,'fixture.cpp'),
        '/Fo'+path.join(directory,'fixture.obj'),'/Fe'+path.join(directory,'Fixture.exe'),
        '/sourceDependencies',path.join(directory,'dependencies.json')];
    const command=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+
        'where cl.exe > toolpaths.txt\r\nwhere link.exe >> toolpaths.txt\r\n'+
        'cl '+argumentsList.map(value=>'"'+value+'"').join(' ')+'\r\nexit /b %errorlevel%\r\n';
    fs.writeFileSync(path.join(directory,'build.cmd'),command,{flag:'wx'});
    const build=execute('cold-build','cmd.exe',['/d','/c',path.join(directory,'build.cmd')],directory,120000);
    assert.equal(build.status,0,'Actual cold consumer compile: '+path.join(output,'cold-build.log'));
    const deps=JSON.parse(fs.readFileSync(path.join(directory,'dependencies.json'),'utf8').replace(/^\uFEFF/,''));
    const inputs=[...new Set([deps.Data.Source,...deps.Data.Includes])].map(file=>({path:file,sha256:hash(file)}));
    const tools=fs.readFileSync(path.join(directory,'toolpaths.txt'),'utf8').trim().split(/\r?\n/).map(file=>({path:file,sha256:hash(file)}));
    const positive=execute('cold-positive',path.join(directory,'Fixture.exe'),['candidate-selected',path.join(directory,'positive')],directory,30000);
    assert.equal(positive.status,0,'Actual cold cases: '+path.join(output,'cold-positive.log'));
    const cases=[...(positive.stdout??'').matchAll(/^CASE (.+)$/gm)].map(match=>match[1].trim());
    assert.deepEqual(cases,['empty-schema2-host-live','durable-parent-alive','durable-unhide-fail','durable-returned','family-msi-excluded','family-rog-excluded','family-onex-excluded','family-aya-excluded']);
    const summary=/RESULT selectedCases=(\d+) checks=(\d+) failed=0/.exec(positive.stdout??'');
    assert(summary);assert.equal(Number(summary[1]),cases.length);assert.equal(Number(summary[2]),19);
    const negative=execute('cold-false-pending-negative',path.join(directory,'Fixture.exe'),['baseline-hostlive',path.join(directory,'negative')],directory,30000);
    assert.equal(negative.status,23,'Original false-pending counterexample must fail semantically');
    assert.deepEqual([...(negative.stdout??'').matchAll(/^FAIL (.+)$/gm)].map(match=>match[1].trim()),['empty-schema2-host-live-must-not-create-pending']);
    for(const row of pins)assert.equal(hash(path.join(snapshot,row.path)),row.sha256);
    for(const row of [...inputs,...tools])assert.equal(hash(row.path),row.sha256,'Cold compiled input changed: '+row.path);
    const result={passed:true,cases,checks:Number(summary[2]),negative:{expectedExit:23,actualExit:negative.status,assertion:'empty-schema2-host-live-must-not-create-pending'},
        sourcePins:pins,actualCompilerInputs:inputs,tools,arguments:argumentsList,sourceSnapshot:snapshot,executable:{path:path.join(directory,'Fixture.exe'),sha256:hash(path.join(directory,'Fixture.exe'))},
        realDeviceIo:false,hardwarePassed:false,productMainExecuted:false};
    fs.writeFileSync(path.join(output,'cold-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
    return result;
}


export const coldEmptyTailCases=Object.freeze([
 'empty-tail-off-no-location','empty-tail-steamdeck-changed-location',
 'empty-tail-live-parent','empty-tail-unknown-parent','empty-tail-writers-busy',
 'empty-tail-owner-lock-held','empty-tail-machine-mismatch',
 'empty-tail-probe-power-change','empty-tail-probe-intent-change',
 'issued-tail-changed-location','issued-tail-same-location','family-msi-excluded']);
export function validateColdEmptyTailSelection(selection){
    assert(selection==='empty-tail'||selection==='baseline-empty-tail'||selection==='mutation-current-empty-tail'||
        (typeof selection==='string'&&selection.startsWith('empty-tail:')&&coldEmptyTailCases.includes(selection.slice('empty-tail:'.length))),
        'Unknown/empty cold empty-tail selector');
    return selection;
}
function runEmptyTailSelected({repo,workspace,output,execute,selection}){
    const directory=path.join(workspace,'cold'),snapshot=path.join(directory,'source');
    fs.mkdirSync(snapshot,{recursive:true});
    fs.cpSync(path.join(repo,'native'),path.join(snapshot,'native'),{recursive:true});
    fs.mkdirSync(path.join(snapshot,'deps/json'),{recursive:true});
    fs.copyFileSync(path.join(repo,'deps/json/json.hpp'),path.join(snapshot,'deps/json/json.hpp'));
    fs.copyFileSync(path.join(repo,'tools/fixtures/native_visibility_cold/fixture.cpp'),path.join(snapshot,'fixture.cpp'));
    const header=fs.readFileSync(path.join(snapshot,'native/native_visibility_cold_runtime.h'),'utf8').replaceAll('\r\n','\n');
    const branchStart='    // An empty persisted set has no remaining path or device action to return.\n';
    const branchEnd='    const auto validScope=[&](){\n';
    const context='    const Context context{currentPowerGeneration(),g_inputHostVirtualIntentRevision.load(std::memory_order_acquire),';
    const guard='if(!current(context) || !ymcc::inputHost::targetReturnRuntime::physicalWritersQuiescent())';
    assert.equal(header.split(branchStart).length,2);assert.equal(header.split(branchEnd).length,2);
    assert.equal(header.split(context).length,2);assert.equal(header.split(guard).length,2);
    const begin=header.indexOf(branchStart),end=header.indexOf(branchEnd,begin);
    assert(end>begin&&header.slice(begin,end).includes('if(!migration && duty.paths.empty())'));
    // Reverse this precise new branch only; no archived Evidence dependency.
    const before=header.slice(0,begin)+header.slice(end);
    const bodies={
        'candidate_cold.h':header,
        'baseline_cold.h':header.replace(context,'    pending=true; // historical false debt before live-context rejection\n'+context),
        'before_empty_tail_cold.h':before,
        'current_removed_empty_tail_cold.h':header.replace(guard,'if(!ymcc::inputHost::targetReturnRuntime::physicalWritersQuiescent())'),
    };
    for(const [name,body] of Object.entries(bodies))fs.writeFileSync(path.join(snapshot,name),body,{flag:'wx'});
    const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    const sourcePins=[...Object.keys(bodies),'fixture.cpp'].map(name=>({path:path.join(snapshot,name),sha256:hash(path.join(snapshot,name))}));
    const argumentsList=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/Od','/MD','/DYMCC_NATIVE_VISIBILITY_EMPTY_TAIL_TEST',
        '/I'+snapshot,'/I'+path.join(snapshot,'native'),'/I'+path.join(snapshot,'deps/json'),path.join(snapshot,'fixture.cpp'),
        '/Fo'+path.join(directory,'fixture.obj'),'/Fe'+path.join(directory,'Fixture.exe'),'/sourceDependencies',path.join(directory,'dependencies.json')];
    const command=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+
        'where cl.exe > toolpaths.txt\r\nwhere link.exe >> toolpaths.txt\r\ncl '+argumentsList.map(value=>'"'+value+'"').join(' ')+'\r\nexit /b %errorlevel%\r\n';
    fs.writeFileSync(path.join(directory,'build.cmd'),command,{flag:'wx'});
    const build=execute('cold-empty-tail-build','cmd.exe',['/d','/c',path.join(directory,'build.cmd')],directory,120000);
    assert.equal(build.status,0,'Actual selected cold consumer compile');
    const deps=JSON.parse(fs.readFileSync(path.join(directory,'dependencies.json'),'utf8').replace(/^\uFEFF/,''));
    const inputs=[...new Set([deps.Data.Source,...deps.Data.Includes])].map(file=>({path:file,sha256:hash(file)}));
    const tools=fs.readFileSync(path.join(directory,'toolpaths.txt'),'utf8').trim().split(/\r?\n/).map(file=>({path:file,sha256:hash(file)}));
    const scopes=selection==='empty-tail'?['empty-tail','baseline-empty-tail','mutation-current-empty-tail']:[selection];
    const executions=[];
    for(const scope of scopes){
        const run=execute('cold-'+scope.replaceAll(':','-'),path.join(directory,'Fixture.exe'),[scope,path.join(directory,scope.replaceAll(':','-'))],directory,30000);
        const expectedExit=scope==='baseline-empty-tail'||scope==='mutation-current-empty-tail'?23:0;
        assert.equal(run.status,expectedExit,'Selected original cold consumer semantics');
        const cases=[...(run.stdout??'').matchAll(/^CASE (.+)$/gm)].map(row=>row[1].trim());
        const pass=[...(run.stdout??'').matchAll(/^PASS (.+)$/gm)].map(row=>row[1].trim());
        const fail=[...(run.stdout??'').matchAll(/^FAIL (.+)$/gm)].map(row=>row[1].trim());
        const expectedCases=scope==='empty-tail'?coldEmptyTailCases:scope==='baseline-empty-tail'?[coldEmptyTailCases[0]]:
            scope==='mutation-current-empty-tail'?['empty-tail-probe-power-change']:[scope.slice('empty-tail:'.length)];
        assert.deepEqual(cases,expectedCases);
        if(expectedExit)assert.deepEqual(fail,[scope==='baseline-empty-tail'?'current-empty-or-issued-tail-must-complete':'unsafe-empty-or-issued-tail-must-refuse']);
        else{
            assert.deepEqual(fail,[]);
            const summary=/RESULT selectedCases=(\d+) checks=(\d+) failed=0/.exec(run.stdout??'');
            assert(summary);assert.equal(Number(summary[1]),cases.length);assert.equal(Number(summary[2]),pass.length);
            if(scope==='empty-tail')assert.equal(pass.length,50);
        }
        executions.push({selection:scope,expectedExit,actualExit:run.status,cases,checks:pass,failed:fail});
    }
    for(const row of [...sourcePins,...inputs,...tools])assert.equal(hash(row.path),row.sha256,'Selected cold input changed: '+row.path);
    const result={passed:true,selection,cases:executions[0].cases,checks:executions[0].checks.length,executions,
        sourcePins,actualCompilerInputs:inputs,tools,arguments:argumentsList,sourceSnapshot:snapshot,
        executable:{path:path.join(directory,'Fixture.exe'),sha256:hash(path.join(directory,'Fixture.exe'))},
        beforeGeneratedFromNewBranchOnly:true,realDeviceIo:false,hardwarePassed:false,productMainExecuted:false};
    fs.writeFileSync(path.join(output,'cold-empty-tail-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
    return result;
}
