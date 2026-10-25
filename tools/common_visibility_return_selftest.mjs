import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

// Only extracts named consumer blocks. Never includes the whole main TU or its
// production runtime headers. All output and compiler temporaries stay in --out.
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2),options={};
for(let i=0;i<args.length;i+=2){
    assert(['--out','--toolchain','--before-main'].includes(args[i])&&args[i+1]&&path.isAbsolute(args[i+1]),
        'Use --out ABS_FRESH_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN [--before-main ABS_FROZEN_MAIN]');
    assert(!Object.hasOwn(options,args[i]),'Duplicate option: '+args[i]);options[args[i]]=path.resolve(args[i+1]);
}
assert(options['--out']&&options['--toolchain'],'Explicit --out and --toolchain are required');
const out=options['--out'],toolchain=options['--toolchain'];
assert(!fs.existsSync(out),'Evidence directory must be fresh; never overwrite earlier failures');
const relativeToRepo=path.relative(repo,out);
assert(relativeToRepo.startsWith('..'+path.sep)||path.isAbsolute(relativeToRepo),'--out must be outside the production repository');
fs.mkdirSync(out,{recursive:true});
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const digest=text=>crypto.createHash('sha256').update(text).digest('hex');
const read=file=>fs.readFileSync(file,'utf8');
const json=file=>JSON.parse(read(file).replace(/^\uFEFF/,''));
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const copy=(from,to)=>{fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(from,to);};
const key=file=>path.resolve(file).toLowerCase();
const productFiles=['tools/common_visibility_return_selftest.mjs','tools/fixtures/common_visibility_return/fixture.cpp','tools/fixtures/common_visibility_return/README.md'];
const productInputs=productFiles.map(relative=>({relative,path:path.join(repo,relative),sha256:sha(path.join(repo,relative))}));
for(const input of productInputs)copy(input.path,path.join(out,'entry-source',input.relative));
const sourceInputs=[];
const artifacts=[],results=[];
const newGate=/const bool (restored|ownedConfigurationReturned)\s*=\s*virtualEndpointAbsent\s*&&\s*ownedTargetsReturned\s*&&\s*\(!msiOwnedReturn\s*\|\|\s*ownedApplicationsReturned\)\s*;/g;
const oldGate=/const bool (restored|ownedConfigurationReturned)\s*=\s*virtualEndpointAbsent\s*&&\s*\(!msiOwnedReturn\s*\|\|\s*\(ownedTargetsReturned\s*&&\s*ownedApplicationsReturned\)\)\s*;/g;
const failureCases=['nonMSI-cli-false-owned-still-hidden','nonMSI-readback-unknown-cli-rejected',
    'nonMSI-host-ack-unknown-malformed-pipe','nonMSI-CLI-ACK-is-not-config-return','nonMSI-carried-duty-reattached-not-erased'];
const traceKeys=['driverMutationsReached','commandsReached','fakePipeRequests','driverHiddenAfter',
    'driverApplicationsAfter','driverCloakAfter','cycleLeafCalls','separateMsiCrashCallbackCalls'];

function extract(main,mainFile,destination){
    const spans=[];
    function ending(start){
        const first=main.indexOf('{',start);assert(first>=0,'Block has no body');let depth=0,state='code';
        for(let i=first;i<main.length;++i){
            const c=main[i],n=main[i+1];
            if(state==='line'){if(c==='\n')state='code';continue;}
            if(state==='block'){if(c==='*'&&n==='/'){state='code';++i;}continue;}
            if(state==='string'||state==='char'){if(c==='\\'){++i;continue;}if(c===(state==='string'?'"':"'"))state='code';continue;}
            if(c==='/'&&n==='/'){state='line';++i;continue;}
            if(c==='/'&&n==='*'){state='block';++i;continue;}
            if(c==='"'){state='string';continue;}if(c==="'"){state='char';continue;}
            if(c==='{')++depth;if(c==='}'&&--depth===0)return i+1;
        }
        throw Error('Unclosed extracted block');
    }
    function span(name,start,end){
        const text=main.slice(start,end),offset=Buffer.byteLength(main.slice(0,start)),bytes=Buffer.byteLength(text);
        assert.equal(digest(fs.readFileSync(mainFile).subarray(offset,offset+bytes)),digest(text));
        const info={name,startLine:main.slice(0,start).split('\n').length,endLine:main.slice(0,end).split('\n').length,
            startByte:offset,bytes,sha256:digest(text),verbatim:true};spans.push(info);
        return '#line '+info.startLine+' "'+mainFile.replaceAll('\\','/')+'"\n'+text+'\n';
    }
    function block(name,kind='function'){
        const expression=new RegExp('^'+(kind==='struct'?'struct '+name+'\\b':'static [^\\n]*\\b'+name+'\\('),'gm');
        const matches=[...main.matchAll(expression)].filter(match=>{
            const brace=main.indexOf('{',match.index),semicolon=main.indexOf(';',match.index);return brace>=0&&(semicolon<0||semicolon>brace);
        });
        assert.equal(matches.length,1,'Expected exactly one actual definition: '+name);
        const start=matches[0].index;let end=ending(start);
        if(kind==='struct'){assert.equal(main[end],';');++end;}return span(name,start,end);
    }
    const generated=[];
    const write=(name,text)=>{const file=path.join(destination,name);fs.writeFileSync(file,text);generated.push({path:file,sha256:sha(file)});};
    write('actual-types.inc',['HidHidePhysicalIdentity','HidHideVisibilitySnapshot','HidHideVisibilityJournal','HidHideCarriedResponsibility'].map(name=>block(name,'struct')).join('\n'));
    write('actual-helpers.inc',['ascii_lower','trim_ascii','hidHideContainsPath','hidHideParseCommandPaths','hidHideIdentityJson',
        'hidHideShouldUnregisterExemptApplication','hidHideExemptApplicationPaths','hidHideApplicationReturnPaths',
        'hidHideShouldReturnApplication','hidHideVirtualEndpointAbsent','hidHideRestoreStatus'].map(name=>block(name)).join('\n'));
    const ownerAnchor='static std::mutex g_hidHideMx;',cliAnchor='static bool hidHideRun(const std::wstring& arguments';
    assert.equal(main.split(ownerAnchor).length,2);assert.equal(main.split(cliAnchor).length,2);
    write('actual-cli.inc',span('CLI owner + RAII + actual hidHideRun',main.indexOf(ownerAnchor),ending(main.indexOf(cliAnchor))));
    write('actual-snapshot.inc',['hidHideParseVisibilitySnapshotDetail','hidHideCaptureVisibilitySnapshotViaCli',
        'hidHideCaptureVisibilitySnapshotViaPipe','hidHideCaptureVisibilitySnapshot'].map(name=>block(name)).join('\n'));
    write('actual-carried.inc',['hidHideCarriedMergeList','hidHideCarriedTransactionId','hidHideCarriedSnapshot','hidHideCarriedClear'].map(name=>block(name)).join('\n'));
    write('actual-sweep.inc',block('hidHideSweepAllHiddenDevices'));
    write('actual-restore.inc',block('hidHideRestorePhysicalTransaction'));
    assert.equal(spans.length,26,'Consumer scope changed; review isolation instead of pulling in runtime');
    save(path.join(destination,'EXTRACTION.json'),{mainFile,mainSha256:sha(mainFile),spans,totalBlocks:spans.length,
        method:'Exact contiguous UTF-8 spans with #line wrappers outside the spans; no fault-inject restore shortcut'});
    return {spans,generated};
}

function snapshot(label,originalMain){
    const dir=path.join(out,label);fs.mkdirSync(dir);
    const mainFile=path.join(dir,'source/native/main.cpp');copy(originalMain,mainFile);
    const mainSha256=sha(mainFile);assert.equal(sha(originalMain),mainSha256,'Main changed while freezing its source bytes');
    sourceInputs.push({role:label,relative:'native/main.cpp',path:originalMain,sha256:mainSha256,frozen:mainFile});
    const dependencyNames=['native/input_host_transport_state.h','native/input_host_transaction_mutex.h',
        'native/msi_steamdeck_release_fence_1726.h','native/ps_endpoint_ownership.h','deps/json/json.hpp'];
    const origins=[];
    for(const relative of dependencyNames){
        // A frozen main may be accompanied by its own headers. Otherwise use the
        // explicit current header and record that fallback, never its includes.
        const sibling=path.join(path.dirname(originalMain),'..',relative);
        const from=label==='before'&&fs.existsSync(sibling)?sibling:path.join(repo,relative);
        const to=path.join(dir,'source',relative);copy(from,to);
        assert.equal(sha(from),sha(to),'Dependency changed while freezing: '+from);
        const input={role:label,relative,path:from,sha256:sha(to),frozen:to,
            selection:label==='before'&&key(from)===key(sibling)?'frozen-main sibling':'current explicit dependency'};
        origins.push(input);sourceInputs.push(input);
    }
    copy(path.join(out,'entry-source/tools/fixtures/common_visibility_return/fixture.cpp'),path.join(dir,'fixture.cpp'));
    const extracted=extract(read(mainFile),mainFile,dir);
    const restore=read(path.join(dir,'actual-restore.inc'));
    const strictMatches=[...restore.matchAll(newGate)],legacyMatches=[...restore.matchAll(oldGate)];
    const strict=strictMatches.length,legacy=legacyMatches.length;
    const decisionSymbol=(strictMatches[0]??legacyMatches[0])?.[1];
    const durableBoundary=decisionSymbol==='ownedConfigurationReturned';
    if(durableBoundary){
        assert(/const bool durablePhysicalSettlementAttempted\s*=\s*ownedConfigurationReturned\s*;/.test(restore),'Keep durable attempted gate');
        assert(/const bool durablePhysicalSettlementComplete\s*=\s*ownedConfigurationReturned\s*&&\s*inputHostTargetReturnAfterPhysicalRestoreLocked\(restoreAfter,\s*restoreReadback,\s*journal\.hiddenByUs\)\s*;/.test(restore),'AfterRestore must remain in the actual durable completion gate');
        assert(/const bool restored\s*=\s*ownedConfigurationReturned\s*&&\s*durablePhysicalSettlementComplete\s*;/.test(restore),'Final restored must retain durable completion');
    }
    assert(strict===1&&legacy===0||label==='before'&&strict===0&&legacy===1,
        'Expected reviewed owned-target gate in current consumer (or legacy gate only in --before-main)');
    const value={label,dir,mainFile,mainSha256:sha(mainFile),strictGate:strict===1,decisionSymbol,durableBoundary,dependencyOrigins:origins,extraction:extracted};
    save(path.join(dir,'SNAPSHOT.json'),{...value,extraction:undefined});return value;
}

function rollback(current){
    const dir=path.join(out,'negative-gate');fs.cpSync(current.dir,dir,{recursive:true,errorOnExist:true,force:false});
    const file=path.join(dir,'actual-restore.inc'),before=read(file),matches=[...before.matchAll(newGate)];assert.equal(matches.length,1);
    const exactAfter='const bool '+matches[0][1]+'=virtualEndpointAbsent &&\n        (!msiOwnedReturn || (ownedTargetsReturned&&ownedApplicationsReturned));';
    const after=before.replace(newGate,()=>exactAfter);
    assert.equal(after.replace(exactAfter,matches[0][0]),before,'Only the owned-target configuration expression may mutate; leave AfterRestore intact');
    fs.writeFileSync(file,after);
    save(path.join(dir,'MUTATION.json'),{file,beforeSha256:digest(before),afterSha256:digest(after),
        exactBefore:matches[0][0],exactAfter,durableBoundaryPreserved:current.durableBoundary,
        expectedFailureCases:failureCases,scope:'Only reverse the common ownedTargetsReturned configuration gate; do not bypass AfterRestore or change real mutation leaves'});
    save(path.join(dir,'SNAPSHOT.json'),{label:'negative-gate',originalSourceSnapshot:path.join(current.dir,'SNAPSHOT.json'),strictGate:false,mutation:path.join(dir,'MUTATION.json'),extractionNote:'EXTRACTION.json binds the original source spans; only actual-restore.inc is changed by the explicit mutation.'});
    return {...current,label:'negative-gate',dir,strictGate:false,mainFile:current.mainFile};
}

function filesUnder(directory){
    const files=[];for(const entry of fs.readdirSync(directory,{withFileTypes:true})){
        const full=path.join(directory,entry.name);if(entry.isDirectory())files.push(...filesUnder(full));else files.push(full);
    }return files;
}
function compareTraces(reference,other,label){
    const comparisons=[];
    assert.deepEqual(reference.cases.map(c=>[c.name,c.assertions]),other.cases.map(c=>[c.name,c.assertions]),'Preserve all 11/97 scenario assertions');
    for(let i=0;i<reference.cases.length;++i){
        const a=reference.cases[i],b=other.cases[i];
        for(const field of traceKeys)assert.deepEqual(a[field],b[field],label+': mutation/query scope changed in '+a.name+' / '+field);
        comparisons.push({case:a.name,referenceReturned:a.actualRestored,otherReturned:b.actualRestored,
            referenceJournalCleared:a.journalCleared,otherJournalCleared:b.journalCleared,
            mutationQueryPipeTracesIdentical:true,commandResult:a.actualReceipt.commandResult??'not-attempted',
            configReadback:a.actualReceipt.configReadback??'none',fakeMutationLeafCount:a.driverMutationsReached.length});
    }
    const file=path.join(out,'TRACE-'+label+'.json');save(file,{pass:true,reference:'current production consumer',other:label,comparisons});return file;
}

try{
    const capture=json(path.join(toolchain,'build-environment.json')),manifest=json(path.join(toolchain,'toolchain-manifest.json'));
    const env={...capture.environment};for(const name of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(name))delete env[name];
    env.INCLUDE=manifest.roots.filter(r=>r.category==='INCLUDE').map(r=>r.frozen).join(';');
    env.LIB=manifest.roots.filter(r=>r.category==='LIB').map(r=>r.frozen).join(';');env.LIBPATH=env.LIB;
    env.TEMP=path.join(out,'temp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
    const cl=capture.tools['cl.exe'].paths[0],link=capture.tools['link.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0];
    const toolIdentities=[cl,link,dump].map(file=>{
        const bound=manifest.tools.find(t=>key(t.original)===key(file));assert(bound&&sha(file)===bound.sha256,'Captured tool identity changed');
        return {path:file,sha256:bound.sha256};
    });
    const byTool=new Map(manifest.files.map(file=>[key(file.frozen),file]));
    const current=snapshot('current',path.join(repo,'native/main.cpp'));
    const before=options['--before-main']?snapshot('before',options['--before-main']):null;
    const mutant=rollback(current),reportByName=new Map();
    const variants=[{snapshot:current,name:'Release',flags:['/O2']},{snapshot:current,name:'Checked',flags:['/Od','/RTC1']},
        ...(before?[{snapshot:before,name:'Release',flags:['/O2']},{snapshot:before,name:'Checked',flags:['/Od','/RTC1']}]:[]),
        {snapshot:mutant,name:'Release',flags:['/O2']}];
    save(path.join(out,'SOURCES.json'),{productInputs,sourceInputs,toolIdentities,currentMainSha256:current.mainSha256,
        beforeMainSha256:before?.mainSha256,scope:'Named consumer extraction only; no main TU or target runtime compilation'});
    for(const variant of variants){
        const s=variant.snapshot,dir=path.join(s.dir,variant.name);fs.mkdirSync(dir);
        const sourceFiles=filesUnder(s.dir).filter(file=>!file.includes(path.sep+'Release'+path.sep)&&!file.includes(path.sep+'Checked'+path.sep));
        const boundFiles=sourceFiles.map(file=>({path:file,sha256:sha(file)}));
        const bySource=new Map(boundFiles.map(file=>[key(file.path),file]));
        save(path.join(dir,'GENERATED.json'),{sources:boundFiles,mainSha256:s.mainSha256,strictGate:s.strictGate,mutant:s.label==='negative-gate'});
        const exe=path.join(dir,'CommonVisibilityReturnFixture.exe');
        const command=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/DUNICODE','/D_UNICODE',...variant.flags,
            '/sourceDependencies',path.join(dir,'dependencies.json'),path.join(s.dir,'fixture.cpp'),'/I'+s.dir,
            '/I'+path.join(s.dir,'source/deps/json'),'/Fo'+path.join(dir,'fixture.obj'),'/Fe'+exe];
        const built=spawnSync(cl,command,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024});
        fs.writeFileSync(path.join(dir,'build.log'),(built.stdout??'')+(built.stderr??''));
        save(path.join(dir,'COMMAND.json'),{executable:cl,arguments:command,exit:built.status,error:built.error?.message});
        assert.equal(built.status,0,'Actual consumer compilation failed; inspect '+path.join(dir,'build.log'));
        const dependencies=json(path.join(dir,'dependencies.json'));
        const actualInputs=[dependencies.Data.Source,...dependencies.Data.Includes].map(file=>{
            const bound=bySource.get(key(file))??byTool.get(key(file));assert(bound&&sha(file)===bound.sha256,'Unbound actual compiler input: '+file);
            assert(!/virtual_target_return_(session|runtime)|target_return_runtime/i.test(file),'Target persistence/runtime must not enter compiler closure');
            return {path:file,sha256:bound.sha256,scope:bySource.has(key(file))?'frozen source/generated consumer/explicit fake leaves':'captured toolchain'};
        });
        const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});
        fs.writeFileSync(path.join(dir,'imports.log'),(imports.stdout??'')+(imports.stderr??''));assert.equal(imports.status,0);
        const importedDlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];
        assert.deepEqual(importedDlls,['kernel32.dll'],'No product/device/SDK imports are allowed');
        const forbiddenImportedFunctions=[...imports.stdout.matchAll(/^\s+[0-9A-F]+\s+((?:CreateProcess[AW]|DeviceIoControl|CreatePipe|PeekNamedPipe|WaitForSingleObject|GetExitCodeProcess|ShellExecute\w*|SetupDi\w*|CM_\w*|SHGet\w*|RegOpen\w*|XInput\w*))\s*$/gmi)].map(m=>m[1]);
        assert.deepEqual(forbiddenImportedFunctions,[],'Fake process/driver/pipe/persistence calls must not link real leaves');
        save(path.join(dir,'COMPILE-CLOSURE.json'),{inputs:actualInputs,unbound:[],toolIdentities,binarySha256:sha(exe),
            importedDlls,forbiddenImportedFunctions,scope:'Static import audit; CRT private report IO is allowed. Dynamic CRT module loading is not traced.'});
        const reportFile=path.join(dir,'RESULT.json'),ran=spawnSync(exe,[reportFile],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:15000,maxBuffer:16*1024*1024});
        fs.writeFileSync(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));assert(fs.existsSync(reportFile),'Terminal fixture report required');
        const report=json(reportFile);assert.equal(report.caseCount,11);assert.equal(report.assertions,97);
        assert(!report.failures.some(f=>f.assertion.startsWith('fixture-exception:')),'Exceptions never count as gate detection');
        if(s.strictGate){assert.equal(ran.status,0);assert(report.pass&&report.failures.length===0);}
        else{
            assert.equal(ran.status,1,'Gate rollback must fail original business assertions, not compile/crash');
            assert.equal(report.failures.length,10);assert.deepEqual([...new Set(report.failures.map(f=>f.case))],failureCases);
        }
        for(const file of boundFiles)assert.equal(sha(file.path),file.sha256,'Frozen/generated source drift');
        const id=s.label+'-'+variant.name,receipt={variant:id,pass:s.strictGate?report.pass:undefined,
            expectedGateFailure:!s.strictGate,caseCount:11,assertions:97,failures:report.failures.length,fixtureExit:ran.status,
            compilerInputs:actualInputs.length,binarySha256:sha(exe),result:reportFile,mainSha256:s.mainSha256};
        reportByName.set(id,report);results.push(receipt);console.log(JSON.stringify(receipt));
    }
    const traces=[compareTraces(reportByName.get('current-Release'),reportByName.get('current-Checked'),'current-Checked'),
        compareTraces(reportByName.get('current-Release'),reportByName.get('negative-gate-Release'),'gate-rollback')];
    if(before)for(const name of ['Release','Checked'])traces.push(compareTraces(reportByName.get('current-Release'),reportByName.get('before-'+name),'before-'+name));
    const sourceDrift=[...productInputs,...sourceInputs].filter(file=>sha(file.path)!==file.sha256);assert.equal(sourceDrift.length,0,'Live source changed during run; keep evidence and rerun against a stable main');
    const result={pass:true,status:'PERMANENT_REAL_CONSUMER_REGRESSION_PASSED',completedAt:new Date().toISOString(),
        entry:path.join(repo,'tools/common_visibility_return_selftest.mjs'),currentMainSha256:current.mainSha256,
        beforeMainSha256:before?.mainSha256,results,traceComparisons:traces,productInputs,sourceInputs,sourceDrift,
        toolchain:{path:toolchain,manifestSha256:sha(path.join(toolchain,'toolchain-manifest.json')),toolIdentities},
        extractionBlocks:26,caseCountPerVariant:11,assertionsPerVariant:97,full105or108RegressionRun:false,
        realProductOrCliSpawned:false,realDeviceApisCalled:0,installedAppDataAccessed:false,
        physicalPersistenceBoundary:{symbol:'inputHostTargetReturnAfterPhysicalRestoreLocked',behavior:'explicit external return-true stub',actualAlgorithmExecuted:false,scope:'Original11/97 local return gate, not true Session/BeforeHide/AfterRestore durable settlement'},
        limits:['Actual common consumer only; no whole-main TU or target runtime is compiled.',
            'Existing non-MSI global sweep/cloak leaves remain visible in fake traces; this test does not authorize or broaden them.',
            'Owned configuration readback is separate from ACK and UNKNOWN game-consumer evidence.',
            'Physical AfterRestore persistence, separate MSI callback, pipe transport and physical cycles are explicit cut ports; no true durable settlement or real device claim.']};
    save(path.join(out,'RESULT.json'),result);save(path.join(out,'READY.json'),{ready:true,result:path.join(out,'RESULT.json'),
        currentMainSha256:current.mainSha256,entry:result.entry,productionRuntimeIntegrated:false});
}catch(error){save(path.join(out,'FAILED.json'),{pass:false,error:error.stack,results,productInputs,sourceInputs,artifacts});throw error;}
