import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {extract} from './fixtures/msi_owned_return_retired_io/source-binding.mjs';
import {runNativeVisibilityCold,validateColdEmptyTailSelection} from './fixtures/native_visibility_cold/run.mjs';
import {resolvePreHostToolchain,preHostChildEnvironment} from './ymcc_sim/global/controller-foreground-latency/prehost-toolchain.mjs';
import {assertPreHostBatchSelection,collectPreHostOutputProvenance} from './ymcc_sim/global/controller-foreground-latency/prehost-output-provenance.mjs';
import {snapshotPreHostInputs,verifyPreHostInputs} from './ymcc_sim/global/controller-foreground-latency/prehost-input-provenance.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
const directed=args.includes('--native-visibility');
const handoffOnly=args.includes('--handoff-cycle');
const win5Only=args.includes('--win5-return-exit2');
const preHostOnly=args.includes('--prehost-writer')||win5Only;
const coldIndex=args.indexOf('--cold-visibility');
const coldOnly=coldIndex>=0;
const coldSelection=coldOnly?validateColdEmptyTailSelection(args[coldIndex+1]):null;
assert(Number(directed)+Number(handoffOnly)+Number(preHostOnly)+Number(coldOnly)<=1,'Choose one directed foreground-shield scope');
if(win5Only)assert(args.length===3&&args[0]==='--output'&&args[1]&&!args[1].startsWith('--')&&args[2]==='--win5-return-exit2','Win5 selection requires its own explicit output and no other scope');
for(let n=0;n<args.length;++n){if(args[n]==='--cold-visibility'){validateColdEmptyTailSelection(args[++n]);continue;}if(args[n]==='--output'){assert(args[++n],'Output path required');continue;}assert(['--native-visibility','--handoff-cycle','--prehost-writer','--win5-return-exit2'].includes(args[n]),'Unknown foreground-shield option');}
assert(preHostOnly||process.env.YMCC_GPD_PREHOST_WRITER_CASE===undefined,
    'Inherited YMCC_GPD_PREHOST_WRITER_CASE requires an explicit pre-Host scope');
const preHostToolchain=preHostOnly?resolvePreHostToolchain(repo):null;
if(preHostToolchain)assert.equal(path.resolve(process.execPath).toLowerCase(),path.resolve(preHostToolchain.node).toLowerCase(),'Explicit pre-Host scope requires its declared Node engine');
const outputIndex=args.indexOf('--output');
const output=path.resolve(outputIndex>=0?args[outputIndex+1]:path.join(repo,'work/shield-foreground-field'));
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
// BEGIN_PREHOST_COMPILED_SOURCE_BINDING
function bindPreHostCompiledSources(repoRoot,snapshotRoot,compilerInputs,snapshotManifest,seedHashes){
    const root=fs.realpathSync(repoRoot),snapshot=fs.realpathSync(snapshotRoot);
    const key=file=>path.resolve(file).toLowerCase();
    const inside=(parent,file)=>{const rel=path.relative(parent,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
    assert(Array.isArray(compilerInputs)&&compilerInputs.length,'Actual compiler inputs required');
    assert(Array.isArray(snapshotManifest)&&snapshotManifest.length,'Original precompile snapshot manifest required');
    const snapshotPins=new Map();
    for(const row of snapshotManifest){
        assert(typeof row.Path==='string'&&/^[a-f0-9]{64}$/i.test(row.Hash??''),'Invalid precompile snapshot pin');
        const file=path.resolve(row.Path);
        assert(inside(snapshot,file),'Precompile snapshot pin outside actual snapshot');
        assert(!snapshotPins.has(key(file)),'Duplicate precompile snapshot pin');
        snapshotPins.set(key(file),row.Hash.toLowerCase());
    }
    const sourceHashes={...seedHashes},seen=new Set();let repositoryInputs=0,externalInputs=0;
    for(const row of compilerInputs){
        assert(typeof row.path==='string'&&/^[a-f0-9]{64}$/.test(row.sha256??''),'Invalid actual compiler input pin');
        const file=fs.realpathSync(row.path);
        assert.equal(hash(fs.readFileSync(file)),row.sha256,'Actual compiled input changed: '+row.path);
        if(seen.has(key(file)))continue;seen.add(key(file));
        let relative;
        if(inside(snapshot,file)){
            assert.equal(snapshotPins.get(key(file)),row.sha256,'Compiled snapshot differs from original precompile manifest: '+row.path);
            relative=path.relative(snapshot,file);
        }else if(inside(root,file))relative=path.relative(root,file);
        else {externalInputs++;continue;}
        const source=fs.realpathSync(path.join(root,relative));
        assert(inside(root,source),'Compiled source escapes repository');
        assert.equal(hash(fs.readFileSync(source)),row.sha256,'Current repository differs from compiled input: '+relative);
        const name=relative.replaceAll('\\','/');
        if(Object.hasOwn(sourceHashes,name))assert.equal(sourceHashes[name],row.sha256,'Captured source differs from compiled input: '+name);
        sourceHashes[name]=row.sha256;repositoryInputs++;
    }
    assert(repositoryInputs>0,'No actual compiled repository sources');
    for(const [relative,sha256] of Object.entries(sourceHashes)){
        assert(!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'Invalid reported source path');
        const source=fs.realpathSync(path.join(root,relative));
        assert(inside(root,source),'Reported source escapes repository');
        assert.equal(hash(fs.readFileSync(source)),sha256,'Reported repository source changed: '+relative);
    }
    return {sourceHashes,repositoryInputs,externalInputs,externalToolClosureAccepted:false};
}
// END_PREHOST_COMPILED_SOURCE_BINDING

if(coldOnly){
    fs.mkdirSync(output,{recursive:true});
    const parent=path.resolve(repo,'../../../Testing/YMCC/Workspace');fs.mkdirSync(parent,{recursive:true});
    const work=fs.mkdtempSync(path.join(parent,'W5Empty-'));
    const executeCold=(label,command,argumentsList,cwd,timeout)=>{
        const started=Date.now();
        const run=spawnSync(command,argumentsList,{cwd,encoding:'utf8',windowsHide:true,timeout});
        fs.writeFileSync(path.join(output,label+'.log'),(run.stdout??'')+(run.stderr??''),{flag:'wx'});
        fs.writeFileSync(path.join(output,label+'-process.json'),JSON.stringify({command,arguments:argumentsList,cwd,timeoutMs:timeout,elapsedMs:Date.now()-started,exitCode:run.status,signal:run.signal,spawnError:run.error?String(run.error):null},null,2)+'\n',{flag:'wx'});
        return run;
    };
    const cold=runNativeVisibilityCold({repo,workspace:work,output,execute:executeCold,selection:coldSelection});
    fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({schema:1,passed:true,status:'PASS',selection:coldSelection,cold,cases:cold.cases.map(id=>({id,passed:true})),hardwareAccess:false,productMainExecuted:false,realSteamOrSleep:false,fullPersistentAcceptance:false,workspace:work},null,2)+'\n',{flag:'wx'});
    console.log('cold visibility '+coldSelection+': selected software cases only');process.exit(0);
}

for(const name of preHostOnly?[]:['legacy','partial_before']){
    const provenance=JSON.parse(fs.readFileSync(path.join(repo,'tools/gpd_native_handoff_cycle_'+name+'.source.json'),'utf8'));
    const body=fs.readFileSync(path.join(repo,'tools/gpd_native_handoff_cycle_'+name+'.h'));
    assert.equal(body.length,provenance.generated.bytes);assert.equal(hash(body),provenance.generated.sha256,'Frozen whole-Step counterexample identity');
}
const productFiles=['native/main.cpp','native/native_foreground_source_policy.h','native/msi_foreground_isolation_runtime.h',
    'native/msi_native_foreground_isolation_runtime.h','native/native_visibility_cold_runtime.h','native/virtual_controller_start_runtime.h','native/virtual_runtime_recovery_runtime.h','native/one_shot_helper_diagnostic.h','InputHost/MsiNativePortCycle.cs'];
const preHostToolchainSources=preHostOnly?['tools/ymcc_sim/global/controller-foreground-latency/prehost-toolchain.mjs',
    'tools/ymcc_sim/global/controller-foreground-latency/prehost-toolchain-inputs.json',
    'tools/ymcc_sim/global/controller-foreground-latency/prehost-output-provenance.mjs',
    'tools/ymcc_sim/global/controller-foreground-latency/prehost-input-provenance.mjs',
    'tools/ymcc_sim/global/controller-foreground-latency/prehost-header-inputs.json']:[];
const identities=[...productFiles,...preHostToolchainSources].map(file=>({file,sha256:hash(fs.readFileSync(path.join(repo,file)))}));
fs.mkdirSync(output,{recursive:true});
const workspaceParent=path.resolve(repo,'../../../Testing/YMCC/Evidence/Build');
fs.mkdirSync(workspaceParent,{recursive:true});
const workspace=fs.mkdtempSync(path.join(workspaceParent,'S30-'));
const preHostEnvironment=preHostOnly?preHostChildEnvironment(preHostToolchain,path.join(workspace,'prehost-environment'),repo):null;
if(preHostEnvironment)for(const name of ['TEMP','TMP','USERPROFILE','APPDATA','LOCALAPPDATA','YEMAN_POWER_CONTROL_DIR'])fs.mkdirSync(preHostEnvironment[name],{recursive:true});
const result={schema:1,passed:false,status:'started',identities,configurations:[],hardwareAccess:false,
    selection:win5Only?'win5-return-exit2':preHostOnly?'prehost-writer':handoffOnly?'handoff-cycle':directed?'native-visibility':'default',
    productMainExecuted:false,realSteamOrSleep:false,fullPersistentAcceptance:false,workspace,
    lifecycleComponent:{featureId:'controller.virtual-input',parentAdapterId:'controller.signal-evidence',
        persistence:'persistent',role:'foreground-visibility-component'},
    sourceHashes:Object.fromEntries(identities.map(row=>[row.file,row.sha256]))};
const save=()=>fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2)+'\n');
save();
process.on('uncaughtExceptionMonitor',error=>{result.status='failed';result.error=String(error.message);save();});
function execute(label,command,argumentsList,cwd,timeout,environment){
    const started=Date.now();
    const run=spawnSync(command,argumentsList,{cwd,encoding:'utf8',windowsHide:true,timeout,...(environment?{env:environment}:{})});
    fs.writeFileSync(path.join(output,label+'.log'),(run.stdout??'')+(run.stderr??'')+(run.error?'\n'+run.error:''),{flag:'wx'});
    fs.writeFileSync(path.join(output,label+'-process.json'),JSON.stringify({command,arguments:argumentsList,cwd,timeoutMs:timeout,elapsedMs:Date.now()-started,exitCode:run.status,signal:run.signal,spawnError:run.error?{message:run.error.message,code:run.error.code}:null,...(environment?{environmentMode:'fixed-prehost-child',environment}:{} )},null,2)+'\n',{flag:'wx'});
    return run;
}
const positive=path.join(workspace,preHostOnly?'Unoptimized':'Release');
const preHostInputBefore=preHostOnly?snapshotPreHostInputs(repo):null;
if(preHostInputBefore){fs.writeFileSync(path.join(output,'prehost-inputs.before.json'),JSON.stringify(preHostInputBefore,null,2)+'\n',{flag:'wx'});result.inputSnapshotBefore={path:path.join(output,'prehost-inputs.before.json'),sha256:hash(fs.readFileSync(path.join(output,'prehost-inputs.before.json'))),inputCount:preHostInputBefore.inputCount,elapsedMs:preHostInputBefore.elapsedMs};save();}
const build=execute(preHostOnly?'Unoptimized-build':'Release-build',preHostOnly?preHostToolchain.powershell:'pwsh',['-NoProfile','-File',path.join(repo,'tools/verify-msi-foreground-isolation.ps1'),'-OutputRoot',positive,...(preHostOnly?['-CaseFilter','Priority','-PreHostWriterSelection',win5Only?'win5-return-exit2':'all','-BuildProfile','Unoptimized','-DeadlineEpochMs',String(Date.now()+240000)]:handoffOnly?['-CaseFilter','NativeHandoff']:directed?['-CaseFilter','NativeVisibility']:[])],repo,240000,preHostEnvironment);
assert.equal(build.status,0,'Actual production TU: '+path.join(output,'Release-build.log'));
const compilerInputs=new Map();
for(const name of ['regression','dsu','steam-recovery']){
    const dependencies=JSON.parse(fs.readFileSync(path.join(positive,name+'-dependencies.json'),'utf8').replace(/^\uFEFF/,''));
    for(const file of [dependencies.Data.Source,...dependencies.Data.Includes])compilerInputs.set(file,{path:file,sha256:hash(fs.readFileSync(file))});
}
result.actualCompilerInputs=[...compilerInputs.values()];
result.compilerTools=JSON.parse(fs.readFileSync(path.join(positive,'compiler-tools.json'),'utf8').replace(/^\uFEFF/,''));save();
function handoffChecks(positiveRun=null){
    const run=positiveRun??execute('handoff-cycle',path.join(positive,'msi-foreground-native-regression.exe'),[path.join(positive,'handoff-data'),'--native-handoff-cycle'],positive,30000);
    assert.equal(run.status,0,'Actual native handoff consumer');
    const summary=/NATIVE_HANDOFF_CYCLE checks=57 failures=0/.exec(run.stdout??'');assert(summary,'Exact handoff57 summary');
    const rows=[...(run.stdout??'').matchAll(/^(PASS|FAIL) gpd-handoff-cycle: (.+)$/gm)];
    assert.equal(rows.length,57);assert(rows.every(row=>row[1]==='PASS'));
    const negative=execute('handoff-original-skip-negative',path.join(positive,'msi-foreground-native-regression.exe'),[path.join(positive,'handoff-negative-data'),'--native-handoff-before'],positive,30000);
    assert.equal(negative.status,3,'Original prehidden skip must fail semantically');
    const negativeRows=[...(negative.stdout??'').matchAll(/^(PASS|FAIL) gpd-handoff-cycle: (.+)$/gm)];
    assert.equal(negativeRows.length,11);
    assert.deepEqual(negativeRows.filter(row=>row[1]==='FAIL').map(row=>row[2].trim()),['authenticated-native-handoff-must-start-original-posthide-cycle']);
    const partial=execute('handoff-partial-ownership-negative',path.join(positive,'msi-foreground-native-regression.exe'),[path.join(positive,'handoff-partial-data'),'--native-handoff-partial-before'],positive,30000);
    assert.equal(partial.status,3,'Mixed foreign/owned current-stamp negative must fail semantically');
    const partialRows=[...(partial.stdout??'').matchAll(/^(PASS|FAIL) gpd-handoff-cycle: (.+)$/gm)];
    assert.equal(partialRows.length,4);
    assert.deepEqual(partialRows.filter(row=>row[1]==='FAIL').map(row=>row[2].trim()),['foreign-carry-wrong-ticket-power-epoch-run-still-skip']);
    return {checks:57,failures:0,assertions:rows.map(row=>row[2].trim()),negativeChecks:11,negativeExpectedExit:3,negativeActualExit:negative.status,partialNegativeChecks:4,partialNegativeActualExit:partial.status,baseline:'whole original prehidden Step and R2 whole Step; functions renamed only',managedHostCreated:false,actualUsbCycleCompleted:false};
}
if(preHostOnly){
    const batchRoot=path.join(positive,'prehost-writer');
    const batch=JSON.parse(fs.readFileSync(path.join(batchRoot,'result.json'),'utf8'));
    const header=fs.readFileSync(path.join(positive,'source-snapshot/tools/msi_native_foreground_regression.h'),'utf8');
    const block=header.slice(header.indexOf('    if(preHostWriterCase){'));
    const allowed=block.match(/allowed=\{([\s\S]*?)\};/);
    assert(allowed,'Original pre-Host selection registration');
    const selections=win5Only?['win5-return-exit2']:[...allowed[1].matchAll(/"([^"]+)"/g)].map(row=>row[1]);
    assertPreHostBatchSelection(batch,selections,win5Only);
    for(const row of batch.cases){
        const text=fs.readFileSync(path.join(batchRoot,row.selection,'stdout.raw'),'utf8');
        const passed=[...text.matchAll(/^PASS native-only: (.+)$/gm)].map(match=>match[1].trim());
        assert(row.passed&&row.exitCode===0&&row.reason==='EXITED');
        assert.deepEqual(passed,row.expectedAssertions);
        assert.deepEqual(passed,row.assertions);
        assert(!/^FAIL /m.test(text));
        assert(row.job.atomicJobAdmission&&row.job.zeroObservedBeforeDispose&&row.job.activeProcessesFinal===0&&row.job.disposed&&!row.job.terminateRequested);
    }
    result.preHostWriter={...batch,buildProfile:'Unoptimized',optimizedReleaseAccepted:false,oldSourceSemanticNegative:'NOT_RUN_BY_THIS_SELECTION'};
    result.cases=batch.cases.map(row=>({id:row.selection,passed:row.passed}));
    result.currentSourceChanges=identities.filter(row=>hash(fs.readFileSync(path.join(repo,row.file)))!==row.sha256);
    result.frozenSourceIdentities=productFiles.filter(file=>file.startsWith('native/')).map(file=>({file,sha256:hash(fs.readFileSync(path.join(positive,'source-snapshot',file)))}));
    for(const row of result.frozenSourceIdentities)assert.equal(row.sha256,result.sourceHashes[row.file]);
    const compiledSources=bindPreHostCompiledSources(repo,path.join(positive,'source-snapshot'),result.actualCompilerInputs,
        JSON.parse(fs.readFileSync(path.join(positive,'compiled-snapshot-hashes.json'),'utf8').replace(/^\uFEFF/,'')),result.sourceHashes);
    result.sourceHashes=compiledSources.sourceHashes;
    result.preHostCompiledSourceBinding={repositoryInputs:compiledSources.repositoryInputs,externalInputs:compiledSources.externalInputs,
        externalToolClosureAccepted:compiledSources.externalToolClosureAccepted,scope:'Actual compiled repository dependencies and original precompile snapshot pins; not a cache or global execution proof'};
    const outputProvenanceStarted=Date.now();
    const outputProvenance=collectPreHostOutputProvenance(workspaceParent,workspace,positive,
        JSON.parse(fs.readFileSync(path.join(positive,'compiled-snapshot-hashes.json'),'utf8').replace(/^\uFEFF/,'')),batch,selections,{requireBuildDetails:true,directedWin5:win5Only});
    const outputProvenanceFile=path.join(output,'prehost-output-provenance.json');
    fs.writeFileSync(outputProvenanceFile,JSON.stringify(outputProvenance,null,2)+'\n',{flag:'wx'});
    result.outputProvenance={path:outputProvenanceFile,sha256:hash(fs.readFileSync(outputProvenanceFile)),
        outputCount:outputProvenance.outputPins.length,pinnedBytes:outputProvenance.pinnedBytes,elapsedMs:Date.now()-outputProvenanceStarted,
        cacheAdmissionGranted:false,toolClosureAccepted:false};
    const preHostInputAfter=snapshotPreHostInputs(repo);
    fs.writeFileSync(path.join(output,'prehost-inputs.after.json'),JSON.stringify(preHostInputAfter,null,2)+'\n',{flag:'wx'});
    result.inputProvenance=verifyPreHostInputs(repo,path.join(positive,'source-snapshot'),preHostInputBefore,preHostInputAfter,
        result.actualCompilerInputs,JSON.parse(fs.readFileSync(path.join(positive,'link-observed-libraries.json'),'utf8').replace(/^\uFEFF/,'')));
    result.inputProvenance.before=result.inputSnapshotBefore;
    result.inputProvenance.after={path:path.join(output,'prehost-inputs.after.json'),sha256:hash(fs.readFileSync(path.join(output,'prehost-inputs.after.json'))),elapsedMs:preHostInputAfter.elapsedMs};
    result.passed=true;result.status='PASS';save();
    console.log('prehost-writer: '+selections.length+' exact selections PASS; unoptimized software scope only');
}else if(handoffOnly){
    result.nativeHandoff=handoffChecks(build);
    result.cases=[{id:'gpd-native-handoff-cycle',passed:true},{id:'gpd-original-prehidden-skip-negative',passed:true},{id:'gpd-mixed-ownership-negative',passed:true}];
    result.currentSourceChanges=identities.filter(row=>hash(fs.readFileSync(path.join(repo,row.file)))!==row.sha256);
    result.frozenSourceIdentities=productFiles.filter(file=>file.startsWith('native/')).map(file=>({file,sha256:hash(fs.readFileSync(path.join(positive,'source-snapshot',file)))}));
    for(const row of result.frozenSourceIdentities)assert.equal(row.sha256,result.sourceHashes[row.file]);
    result.passed=true;result.status='PASS';save();console.log('native handoff: actual 57 checks and both semantic negatives PASS');
}else{
const nativeRun=directed?build:execute('native-visibility',path.join(positive,'msi-foreground-native-regression.exe'),[path.join(positive,'native-visibility-data'),'--native-visibility-write'],positive,30000);
assert.equal(nativeRun.status,0,'Actual native visibility consumer');
const nativeSummary=/NATIVE_VISIBILITY_WRITE checks=(\d+) failures=(\d+)/.exec(nativeRun.stdout??'');
assert(nativeSummary,'Native visibility summary required');
const nativeRows=[...(nativeRun.stdout??'').matchAll(/^(PASS|FAIL) native-write-ahead: (.+)$/gm)].map(match=>({passed:match[1]==='PASS',name:match[2].trim()}));
assert.equal(nativeRows.length,Number(nativeSummary[1]));assert.equal(nativeRows.length,31);assert.equal(Number(nativeSummary[2]),0);assert(nativeRows.every(row=>row.passed));
result.nativeVisibility={checks:nativeRows.length,failures:0,assertions:nativeRows};save();
result.cold=runNativeVisibilityCold({repo,workspace,output,execute});save();
result.nativeHandoff=handoffChecks();save();
if(directed){
    result.currentSourceChanges=identities.filter(row=>hash(fs.readFileSync(path.join(repo,row.file)))!==row.sha256);
    result.frozenSourceIdentities=productFiles.filter(file=>file.startsWith('native/')).map(file=>({file,sha256:hash(fs.readFileSync(path.join(positive,'source-snapshot',file)))}));
    for(const row of result.frozenSourceIdentities)assert.equal(row.sha256,result.sourceHashes[row.file],'Compiled snapshot differs from captured input: '+row.file);
    result.cases=[{id:'native-visibility-write',passed:true},...result.cold.cases.map(id=>({id,passed:true})),{id:'false-pending-negative',passed:true},{id:'gpd-native-handoff-cycle',passed:true},{id:'gpd-original-prehidden-skip-negative',passed:true},{id:'gpd-mixed-ownership-negative',passed:true}];
    result.passed=true;result.status='PASS';save();console.log('native visibility: 31 checks, cold 8 cases/19 checks, handoff57 and three semantic negatives PASS');
}else{
const summary=JSON.parse(fs.readFileSync(path.join(positive,'fixture-data/msi-foreground-native-summary.json'),'utf8').replace(/^\uFEFF/,''));
assert.equal(summary.failures,0);result.configurations.push({name:'Release',...summary});save();
// Same actual production-TU executable, with only the original mutation
// prerequisites/assertions selected. Prove that selection passes before mutating.
const probeArgs=[path.join(positive,'mutation-probe-data'),'--shield-mutation-probes'];
const probeRun=execute('mutation-probe-baseline',path.join(positive,'msi-foreground-native-regression.exe'),probeArgs,positive,30000);
assert.equal(probeRun.status,0,'Original mutation probes must pass without a mutation');
result.mutationProbeBaseline=JSON.parse(fs.readFileSync(path.join(positive,'mutation-probe-data/msi-foreground-native-summary.json'),'utf8'));
assert.equal(result.mutationProbeBaseline.scope,'shield-mutation-probes');
assert(result.mutationProbeBaseline.cases>0 && result.mutationProbeBaseline.failures===0);save();
const libs=['user32','gdi32','shell32','shlwapi','ole32','oleaut32','dwmapi','winhttp','advapi32','shcore','version','psapi','PowrProf','ws2_32'].map(name=>name+'.lib').join(' ');
for(const name of ['rog-hid-mutation-quarantine-disabled','original-xusb-and-journal','duplicate-shared-node-refresh']){
    const directory=path.join(workspace,name);fs.mkdirSync(directory,{recursive:true});
    const snapshot=path.join(directory,'source-snapshot');fs.cpSync(path.join(positive,'source-snapshot'),snapshot,{recursive:true});
    if(name==='rog-hid-mutation-quarantine-disabled'){
        const policy=path.join(snapshot,'native/native_foreground_source_policy.h');
        const source=fs.readFileSync(policy,'utf8');
        // Mutate only the existing leaf policy; newly integrated family/root
        // authority functions between it and xusbTargetAdmitted must survive.
        const signature='inline bool gameNodeRefreshMutationAllowed(';
        assert.equal(source.split(signature).length,2);
        // The existing locator accepts static functions; restore the equal-
        // length storage specifier before matching the exact original span.
        const probe=source.replace(signature,'static bool gameNodeRefreshMutationAllowed(');
        const body=extract(probe,'function','gameNodeRefreshMutationAllowed','native/native_foreground_source_policy.h',[]).replace(/^static /,'inline ');
        assert.equal(source.split(body).length,2);
        fs.writeFileSync(policy,source.replace(body,'inline bool gameNodeRefreshMutationAllowed(Profile,const std::string&,const std::string&) {return true;}'));
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
    const run=execute(name+'-run',path.join(directory,'Fixture.exe'),[path.join(directory,'fixture-data'),'--shield-mutation-probes'],directory,30000);
    assert.equal(run.status,3,name+' original bug must fail');
    const failures=(run.stdout??'').split(/\r?\n/).filter(line=>line.startsWith('FAIL '));
    if(name==='rog-hid-mutation-quarantine-disabled'){
        assert(failures.some(line=>line.includes('field ROGX known unsafe HID leaf policy remains quarantined')));
    }else if(name==='original-xusb-and-journal'){
        assert(failures.some(line=>line.includes('field ROG active HID XUSB identity remains recognized')));
        assert(failures.some(line=>line.includes('missing journal retains only')));
        assert(failures.some(line=>line.includes('journal continuation executes')));
    }else assert(failures.some(line=>line.includes('shared devnode refreshes exactly once')));
    const mutationSummary=JSON.parse(fs.readFileSync(path.join(directory,'fixture-data/msi-foreground-native-summary.json'),'utf8'));
    assert.equal(mutationSummary.scope,'shield-mutation-probes');
    assert.equal(mutationSummary.cases,result.mutationProbeBaseline.cases,'Mutation must consume the same selected original assertions');
    assert.equal(mutationSummary.failures,failures.length);
    result.configurations.push({name,expectedFailure:true,exitCode:run.status,scope:mutationSummary.scope,cases:mutationSummary.cases,failures});save();
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
result.cases.push({id:'native-visibility-write',passed:true},...result.cold.cases.map(id=>({id,passed:true})),{id:'false-pending-negative',passed:true});
result.cases.push({id:'gpd-native-handoff-cycle',passed:true},{id:'gpd-original-prehidden-skip-negative',passed:true},{id:'gpd-mixed-ownership-negative',passed:true});
result.passed=true;result.status='PASS';save();
console.log('foreground shield: '+summary.cases+' native checks, helper Release/Checked and original failure variants PASS');
}
}
