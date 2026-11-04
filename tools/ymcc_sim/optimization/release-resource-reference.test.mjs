// INERT metadata controls only. Every .exe below is a short text file and is never executed.
// Synthetic receipt verdict fields exercise admission; they are not native/business PASS evidence.
// No child_process, shell, compiler, device, Loader or product execution is used in this file.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import test from 'node:test';
import {verifyNativeResourceReference} from './release-resource-reference.mjs';
import {resourceCacheReferenceContract} from '../../decky_resource_cache_selftest.mjs';
import {resourceWorkerReferenceContract} from '../../decky_resource_worker_selftest.mjs';
import {resourceFixtureEnvironment} from '../global/decky-persistent-lifecycle/baseline-environment.mjs';
import {simulationCorePath} from '../global/release-host-environment.mjs';

const CONTROLS = path.join(simulationCorePath('Evidence'), 'FixtureIntake-20261030', 'Continuation-20261031',
  'FullReleaseActual-20261031', 'CostFollowup', 'ResourceControls');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const pin = file => ({path:path.resolve(file),sha256:hash(file)});
const write = (file, value) => {
  fs.mkdirSync(path.dirname(file), {recursive:true});
  fs.writeFileSync(file, typeof value === 'string' || Buffer.isBuffer(value) ? value : JSON.stringify(value, null, 2) + '\n');
  return file;
};
const key = file => path.resolve(file).toLowerCase();
const time = ms => new Date(Date.UTC(2026, 9, 31, 12, 0, 0) + ms).toISOString();

function fixture(family = 'worker', parameterizedCache = false) {
  fs.mkdirSync(CONTROLS, {recursive:true});
  const root = fs.mkdtempSync(path.join(CONTROLS, 'inert-' + family + '-'));
  const repo = path.join(root, 'repository'), core = path.join(root, 'core');
  const baselineFile = path.join(core, 'Evidence', 'parent', 'baseline', 'result.json');
  const baselineProcessFile = path.join(core, 'Evidence', 'parent', 'processes', 'original.json');
  const out = path.join(path.dirname(baselineFile), 'resource-' + family);
  const contract = family === 'cache' ? resourceCacheReferenceContract() : resourceWorkerReferenceContract();
  const workParent = path.join(core, 'Workspace', contract.short);
  fs.mkdirSync(workParent, {recursive:true});
  const work = fs.mkdtempSync(path.join(workParent, 'r')), frozenRoot = path.join(work, 's');
  const fakeTools = path.join(root, 'INERT-tools');
  const toolFile = name => pin(write(path.join(fakeTools, name), 'INERT metadata file; never execute: ' + name + '\n'));
  const node = toolFile('node.exe'), compiler = toolFile('cl.exe'), linker = toolFile('link.exe');
  const dumpbin = toolFile('dumpbin.exe'), commandProcessor = toolFile('cmd.exe');
  const header = toolFile('inert-header.h'), library = toolFile('inert.lib');
  const additional = toolFile('inert-owned-declaration.txt'), fixed = toolFile('inert-resource-declaration.txt');
  const nativeTools = {compiler:compiler.path,linker:linker.path,dumpbin:dumpbin.path,commandProcessor:commandProcessor.path,
    include:[fakeTools],lib:[fakeTools]};
  const declared = [compiler,linker,dumpbin,commandProcessor,header,library];
  const official = pin(write(path.join(repo, 'PowerControl/decky/PluginLoader_noconsole.exe'), 'INERT official-data stand-in; never execute\n'));
  const expectedEnvironment = resourceFixtureEnvironment({systemroot:path.join(root,'INERT-Windows'),
    comspec:commandProcessor.path,Path:fakeTools,temp:path.join(root,'INERT-inherited-temp'),tmp:path.join(root,'INERT-inherited-temp'),
    ymcc_sim_core_root:core});
  const alias = 'tools/inert-baseline.mjs';
  const originalProcess = {id:'original.plugins.decky-update-recovery',pid:4242,startedAt:time(0),finishedAt:time(220000),
    elapsedMs:220000,program:node,arguments:[path.join(repo,alias),path.dirname(baselineFile)],cwd:repo,
    originalBudgetMs:240000,timeoutMs:240000,budgetClipped:false,exitCode:0,error:null,signal:null,timedOut:false,outputTruncated:false,
    stdout:pin(write(path.join(path.dirname(baselineProcessFile),'original.stdout.bin'),'INERT parent stdout\n')),
    stderr:pin(write(path.join(path.dirname(baselineProcessFile),'original.stderr.bin'),''))};
  const primaryCount = family === 'cache' ? contract.positiveCaseCount : contract.mutants[0].nativeCaseCount;
  const primaryNames = Array.from({length:primaryCount}, (_, i) => 'INERT metadata case ' + i);
  for (const [i, mutation] of contract.mutants.entries()) primaryNames[i + 2] = mutation.failure;
  const positiveNames = [...primaryNames], deadlineCase = 'stalled callback actual resources at deadline';
  if (family === 'worker') positiveNames.splice(positiveNames.indexOf(contract.mutants[0].failure) + 1, 0, deadlineCase);
  let cppText = positiveNames.map(name => primaryNames.includes(name) ?
    'check(' + JSON.stringify(name) + ', []{});' : 'cases.push_back({{"name",' + JSON.stringify(name) + '},{"pass",true}});').join('\n');
  if (parameterizedCache) {
    assert.equal(family, 'cache');
    const families = [
      ['ownerStorm','storm staging owner ',' preserves unknown provenance', ['empty','truncated','array','null','oversize','future','old-owner','float-schema','wrong-type','duplicate','escaped-duplicate','trailing']],
      ['slotStorm','storm slot ',' stays unowned and cannot repair', ['empty','truncated','array','null','oversize','deep','future','old-owner','schema-string','schema-float','complete-missing','complete-string','complete-null','manifest-empty','manifest-bytes-string','duplicate','nested-duplicate','trailing']],
      ['receiptStorm','storm managed receipt ',' cannot borrow cache authority', ['empty','truncated','array','null','oversize','future','old-owner','old-pin','schema-float','schema-string','files-object','bytes-string','bytes-bool','duplicate','nested-duplicate','escaped-duplicate','trailing']],
      [null,'storm IO ',' explicit retry publishes verified cache', ['owner-partial','owner-flush','metadata-write','metadata-replace','complete-replace']],
      [null,'storm occupied read ',' preserves bytes and explicit retry heals', ['staging-owner','slot-metadata','managed-receipt']],
    ];
    const dynamic = families.flatMap(([,prefix,suffix,values]) => values.map(value => prefix + value + suffix));
    primaryNames.splice(primaryNames.length - dynamic.length, dynamic.length, ...dynamic);
    positiveNames.splice(0, positiveNames.length, ...primaryNames);
    cppText = primaryNames.slice(0, -dynamic.length).map(name => 'check(' + JSON.stringify(name) + ', []{});').join('\n');
    for (const [variable,prefix,suffix,values] of families) {
      const list = values.map(value => JSON.stringify(value)).join(',');
      if (variable === 'ownerStorm') cppText += '\nconst std::vector<std::pair<std::string,std::string>> ownerStorm={\n' + values.map(value => '{' + JSON.stringify(value) + ',"INERT"}').join(',') + '\n};\nfor(const auto& [kind,bytes]:ownerStorm)';
      else if (variable) cppText += '\nconst std::vector<std::string> ' + variable + '={' + list + '};\nfor(const auto& kind:' + variable + ')';
      else cppText += '\nfor(const std::string kind:{' + list + '})';
      cppText += 'check((' + JSON.stringify(prefix) + '+kind+' + JSON.stringify(suffix) + ').c_str(), []{});';
    }
  }
  const cpp = 'tools/decky_resource_' + family + '_selftest.cpp';
  const nativeManifest = 'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json';
  const sourceText = new Map([
    [alias,'// INERT baseline entry; not executed\n'],
    [contract.module,'// INERT producer identity; not executable fixture code\nconst CONFIG = {"scope":"INERT_METADATA_ONLY"};\n'],
    [cpp,'// INERT scope declarations only\n' + cppText + '\n'],
    ['decky-plugin/vendor/decky-loader.lock.json',JSON.stringify({asset:{sha256:official.sha256}})],
    [nativeManifest,JSON.stringify({schemaVersion:1,native:nativeTools,files:declared})],
  ]);
  for (const mutant of contract.mutants) sourceText.set(mutant.path,
    (sourceText.get(mutant.path) ?? '// INERT mutation anchors, never compiled\n') + mutant.from + '\n');
  const sourceInputs = [], frozen = [];
  for (const [name, text] of sourceText) {
    write(path.join(repo,name), text); const captured = write(path.join(frozenRoot,name), text);
    sourceInputs.push({path:name,sha256:hash(path.join(repo,name))}); frozen.push({path:name,sha256:hash(captured),captured});
  }
  const sourceRows = sourceRoot => frozen.map(row => ({relative:row.path,...pin(path.join(sourceRoot,row.path))}));
  const owner = {schemaVersion:1,nonce:crypto.randomUUID(),processId:4242,module:contract.module,
    work,parent:workParent,callerEvidenceDir:out,createdAt:time(11)};
  const ownerFile = write(path.join(work,'OWNER.json'), owner);
  const workspace = {schemaVersion:1,work,callerEvidenceDir:out,sourceRoot:frozenRoot,
    workspaceOwner:pin(ownerFile),ancestorChecks:'INERT metadata ancestry',node};
  const freeze = {schemaVersion:1,root:repo,frozen:frozenRoot,files:frozen};
  const audit = {schemaVersion:1,sourceChanged:false,rows:frozen.map(row => ({path:row.path,beforeSha256:row.sha256,
    afterSha256:row.sha256,captured:pin(row.captured),unchanged:true}))};
  const observed = [{...header,role:'actual-compiler-input',originalDeclaration:true},
    {...library,role:'actual-link-library',originalDeclaration:true}];
  const graph = {schemaVersion:1,originalManifest:pin(path.join(repo,nativeManifest)),declaredTools:declared,
    additionalFixedDeclaration:[additional],resourceFixedDeclaration:[fixed],selected:nativeTools,
    actualInputs:observed,observedOutsideOriginalDeclaration:[],
    toolsAfter:declared.map(row => ({path:row.path,expectedSha256:row.sha256,actualSha256:row.sha256})),
    actualToolAfter:observed.map(row => ({...row,afterSha256:row.sha256})),node,officialData:official};
  const controls = [];
  const negatives = [];
  const entry = {schemaVersion:1,family,exportName:contract.exportName,options:{mutations:true},module:pin(path.join(repo,contract.module)),
    moduleSha256After:hash(path.join(repo,contract.module)),caller:{pid:4242,node,cwd:repo,arguments:originalProcess.arguments},
    environment:{...expectedEnvironment},startedAt:time(10),finishedAt:time(200000),elapsedMs:199990,files:{},runs:[],mutations:[]};
  for (const [index, mutant] of [null,...contract.mutants].entries()) {
    const name = mutant ? 'negative-' + mutant.name : 'positive', evidenceDir = path.join(out,name);
    const sourceRoot = mutant ? path.join(work,'v' + (index - 1)) : frozenRoot;
    if (mutant) for (const row of frozen) {
      const text = fs.readFileSync(row.captured,'utf8');
      write(path.join(sourceRoot,row.path), row.path === mutant.path ? text.replace(mutant.from,mutant.to) : text);
    }
    const dir = path.join(work, index === 0 ? 'p' : 'n' + (index - 1)), temp = path.join(dir,'t');
    const fixtureDir = path.join(dir, family === 'cache' ? 'resource-fixture' : 'worker-fixture');
    fs.mkdirSync(temp,{recursive:true}); fs.mkdirSync(fixtureDir,{recursive:true});
    const executable = pin(write(path.join(dir,'resource-cache.exe'),'INERT compiled-image stand-in; never execute\n'));
    const nativeFile = path.join(dir,'native.json'), dependenciesFile = path.join(dir,'d.json');
    const names = [...(mutant ? (family === 'cache' ? [mutant.failure] : primaryNames) : positiveNames)];
    // The real worker callback appends its auxiliary row before check() appends the owning primary row.
    if (family === 'worker' && !mutant) {
      const auxiliary = names.indexOf(deadlineCase);
      [names[auxiliary - 1],names[auxiliary]] = [names[auxiliary],names[auxiliary - 1]];
    }
    const native = {cases:names.map((name,i) => mutant ?
      {name,pass:family === 'worker' && i === names.length - 1,...(family === 'worker' && i === names.length - 1 ? {} : {error:'INERT semantic failure marker'})} :
      {name,pass:true}),count:names.length,passed:mutant ? (family === 'worker' ? 1 : 0) : names.length,
      pass:!mutant,officialLoaderExecuted:false,realSteamActions:0,
      ...(family === 'worker' ? {childIsOwnTestImage:true,originalStopDeadlineMs:3200} :
        {loaderFixtureAliasOnly:true,productProcessesLaunched:0,physicalSleep:false})};
    const sources = sourceRows(sourceRoot);
    const processEnv = {...expectedEnvironment,PATH:path.dirname(compiler.path) + ';' + expectedEnvironment.SystemRoot + '/System32',
      INCLUDE:nativeTools.include.join(';'),LIB:nativeTools.lib.join(';'),CL:'',_CL_:'',LINK:'',_LINK_:'',VSLANG:'1033',TEMP:temp,TMP:temp};
    const buildArgs = ['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE',
      '/I' + path.join(sourceRoot,'native'),'/I' + path.join(sourceRoot,'deps/json'),path.join(sourceRoot,cpp),
      '/Fo' + path.join(dir,'resource-cache.obj'),'/Fe' + executable.path,'/sourceDependencies',dependenciesFile,
      '/link','/verbose:lib','advapi32.lib','user32.lib','ws2_32.lib','bcrypt.lib'];
    const processRecord = (kind, program, args, offset, status) => ({kind,program,args,cwd:dir,pid:5000 + index * 2 + (kind === 'run' ? 1 : 0),
      startedAt:time(offset),elapsedMs:1,budgetMs:kind === 'build' ? 60000 : 30000,status,signal:null,error:null,natural:true,
      environment:{...processEnv},stdout:pin(write(path.join(evidenceDir,kind + '.stdout.log'),
        kind === 'build' ? library.path + '\n' : 'INERT native-output marker\n')),
      stderr:pin(write(path.join(evidenceDir,kind + '.stderr.log'),''))});
    const offset = 20 + index * 4;
    const build = processRecord('build', compiler, buildArgs, offset, 0);
    const run = processRecord('run', executable, [fixtureDir,nativeFile,official.path,
      ...(mutant?.selectedCase ? ['--case',mutant.selectedCase] : [])], offset + 2, mutant ? 2 : 0);
    const dependencies = {Version:'1.2',Data:{Source:path.join(sourceRoot,cpp),Includes:[header.path]}};
    let junction = null;
    if (family === 'cache') {
      const outside = path.join(fixtureDir,'outside'), link = path.join(fixtureDir,'reparse/PowerControl/decky/.ymcc-resource-cache/a');
      fs.mkdirSync(outside,{recursive:true}); fs.mkdirSync(path.dirname(link),{recursive:true});
      fs.symlinkSync(outside,link,'junction'); junction = {path:link,target:outside,realTarget:outside,insideExactOwnedWork:true};
    }
    const execution = {name,status:mutant ? 2 : 0,signal:null,native,exeSha256:executable.sha256,executable,
      actualPaths:{work,sourceRoot,dir,temp,fixture:fixtureDir,nativeResult:nativeFile,callerEvidenceDir:out},
      build,run,sourceInputs:sources,actualCompilerInputs:[{...sources.find(row => row.relative === cpp),role:'captured-source'},observed[0]],
      actualLinkLibraries:[observed[1]],junction,originalCompileBudgetMs:60000,originalNativeBudgetMs:30000,originalStopDeadlineMs:3200};
    const paths = {schemaVersion:1,name,callerEvidenceDir:out,work,sourceRoot,compileDir:dir,temp,executable:executable.path,
      nativeResult:nativeFile,fixture:fixtureDir,sources};
    let proof = null;
    if (mutant) {
      proof = {name:mutant.name,sourceRoot,path:mutant.path,from:mutant.from,to:mutant.to,expectedFailure:mutant.failure,
        beforeSha256:frozen.find(row => row.path === mutant.path).sha256,after:pin(path.join(sourceRoot,mutant.path)),sources};
      negatives.push({name:mutant.name,detected:true,failure:mutant.failure,execution:path.join(evidenceDir,'EXECUTION.json')});
    }
    controls.push({name,mutant,evidenceDir,nativeFile,dependenciesFile,native,execution,paths,dependencies,proof});
  }
  const result = {pass:true,nativeCases:contract.positiveCaseCount,negativeMutations:negatives,sourceHashes:frozen,
    officialDataSha256:official.sha256,officialLoaderExecuted:false,realSteamActions:0,installedWrites:0,physicalSleep:false,
    scope:'INERT_METADATA_ONLY',workRoot:work,callerEvidenceDir:out,sourceChanged:false,
    originalCompileBudgetMs:60000,originalNativeBudgetMs:30000,originalStopDeadlineMs:3200};
  const summary = {work,positive:{status:0,count:contract.positiveCaseCount},negativeMutations:negatives,executionError:null,sourceChanges:[]};
  const baselineValue = {resourceExecutions:{[family]:entry},[family === 'cache' ? 'resourceCache' : 'resourceWorker']:result,
    controlOnly:true,fullProductAcceptance:false};
  sourceInputs.push({path:'PowerControl/decky/PluginLoader_noconsole.exe',sha256:official.sha256});
  const toolArtifacts = [node,...declared,additional,fixed];
  function refresh() {
    write(ownerFile,owner); workspace.workspaceOwner = pin(ownerFile);
    entry.runs = []; entry.mutations = [];
    for (const control of controls) {
      const {execution,native,evidenceDir,name,dependencies,paths} = control;
      execution.native = native;
      write(control.nativeFile,native); write(control.dependenciesFile,dependencies);
      const mapping = {execution:['EXECUTION.json',execution],native:['native.json',native],paths:['PATHS.json',paths],
        buildProcess:['build.PROCESS.json',execution.build],runProcess:['run.PROCESS.json',execution.run],dependencies:['dependencies.json',dependencies]};
      const record = {name};
      for (const [field,[file,value]] of Object.entries(mapping)) record[field] = pin(write(path.join(evidenceDir,file),value));
      write(path.join(out,name + '.json'),execution);
      for (const kind of ['build','run']) write(path.join(evidenceDir,kind + '.log'), Buffer.concat([
        fs.readFileSync(execution[kind].stdout.path),fs.readFileSync(execution[kind].stderr.path)]));
      entry.runs.push(record);
      if (control.proof) entry.mutations.push({name:control.mutant.name,proof:pin(write(path.join(out,'mutation-' + control.mutant.name + '.json'),control.proof))});
    }
    const files = {result:['result.json',result],summary:['EXECUTION-SUMMARY.json',summary],workspace:['WORKSPACE.json',workspace],
      freeze:['SOURCE-FREEZE.json',freeze],audit:['SOURCE-AUDIT.json',audit],tools:['TOOL-GRAPH.json',graph]};
    for (const [field,[file,value]] of Object.entries(files)) entry.files[field] = pin(write(path.join(out,file),value));
    write(baselineProcessFile,originalProcess); write(baselineFile,baselineValue);
  }
  const options = {family,repoRoot:repo,coreRoot:core,baselineResult:baselineFile,baselineValue,originalProcess,baselineProcessFile,
    sourceInputs,toolArtifacts,expectedEnvironment,nativeTools,contract};
  refresh();
  return {root,repo,core,out,work,entry,result,baselineValue,originalProcess,baselineProcessFile,sourceInputs,toolArtifacts,
    options,owner,workspace,graph,controls,contract,frozen,audit,cpp,refresh,verify:() => verifyNativeResourceReference(options)};
}
function rejected(change, pattern = /RESOURCE_REFERENCE_/, family = 'worker') {
  const f = fixture(family); change(f); f.refresh(); assert.throws(f.verify,pattern); return f;
}

test('INERT resource valid cache and worker metadata retain every original negative FAIL and produce references only', () => {
  for (const family of ['cache','worker']) {
    const f = fixture(family), reference = f.verify();
    assert.equal(reference.status,'SHARED_REFERENCE'); assert.equal(reference.executedThisRun,false);
    assert.equal(reference.fullProductAcceptance,false); assert.equal(reference.cached,false);
    assert.equal(reference.originalProducerExecutionsByThisReference,0); assert.equal(reference.originalProducerExecutionsWithinParent,1);
    assert.equal(reference.ordinaryCases,f.contract.positiveCaseCount);
    assert.equal(reference.originalNegativeFailuresPreserved.length,f.contract.mutants.length);
    if (family === 'worker') {
      const names = f.controls[0].native.cases.map(row => row.name);
      assert(names.indexOf('stalled callback actual resources at deadline') < names.indexOf(f.contract.mutants[0].failure));
    }
    const negative = f.controls[1].native; assert.equal(negative.pass,false);
    assert.equal(negative.count,f.contract.mutants[0].nativeCaseCount); assert.equal(negative.passed,family === 'cache' ? 0 : 1);
    const mapOptions = {...f.options,sourceInputs:new Map(f.sourceInputs.map(row => [row.path,row.sha256]))};
    assert.equal(verifyNativeResourceReference(mapOptions).status,'SHARED_REFERENCE');
    const folded = Object.fromEntries(Object.entries(f.options.expectedEnvironment).map(([name,value]) => [name.toLowerCase(),value]));
    assert.equal(verifyNativeResourceReference({...f.options,expectedEnvironment:folded}).status,'SHARED_REFERENCE');
  }
});

test('INERT resource old receipts, missing real PIDs, owner identity and incomplete environment reject', () => {
  rejected(f => { delete f.baselineValue.resourceExecutions; }, /RESOURCE_REFERENCE_ENVELOPE_MISSING/);
  rejected(f => { delete f.originalProcess.pid; }, /RESOURCE_REFERENCE_PARENT_PID_MISSING/);
  rejected(f => { delete f.entry.caller.pid; }, /RESOURCE_REFERENCE_PARENT_PID_MISSING/);
  rejected(f => { delete f.controls[0].execution.run.pid; }, /RESOURCE_REFERENCE_PROCESS_PID_MISSING/);
  rejected(f => { f.owner.processId++; }, /RESOURCE_REFERENCE_OWNER_PID/);
  rejected(f => { f.owner.nonce = 'not-an-owned-nonce'; }, /RESOURCE_REFERENCE_OWNER_NONCE/);
  rejected(f => { delete f.entry.environment.PATH; }, /RESOURCE_REFERENCE_ENVIRONMENT_INCOMPLETE/);
  rejected(f => { delete f.controls[0].execution.build.environment.SystemRoot; }, /RESOURCE_REFERENCE_ENVIRONMENT_INCOMPLETE/);
});

test('INERT resource original process/result/raw logs and source or mutation bytes cannot be substituted', () => {
  const f = fixture(); write(f.entry.files.result.path,'{"pass":true}\n'); assert.throws(f.verify,/RESOURCE_REFERENCE_PIN_HASH/);
  const p = fixture(); write(p.baselineProcessFile,{...p.originalProcess,pid:9999}); assert.throws(p.verify,/RESOURCE_REFERENCE_PARENT_PROCESS_OBJECT_CHANGED/);
  rejected(g => { g.baselineValue.resourceWorker = {...g.result,installedWrites:1}; }, /RESOURCE_REFERENCE_RESULT_NOT_ORIGINAL/);
  const logs = fixture(); fs.appendFileSync(logs.controls[0].execution.build.stdout.path,'tamper'); assert.throws(logs.verify,/RESOURCE_REFERENCE_PIN_HASH/);
  const mutation = fixture(); fs.appendFileSync(mutation.controls[1].proof.after.path,'tamper'); assert.throws(mutation.verify,/RESOURCE_REFERENCE_PIN_HASH/);
  const source = fixture(); fs.appendFileSync(path.join(source.repo,source.contract.module),'tamper'); assert.throws(source.verify,/RESOURCE_REFERENCE_PIN_HASH/);
});

test('INERT resource positive-only opts, omitted or duplicate negatives and reduced worker14 scope reject', () => {
  rejected(f => { f.entry.options.mutations = false; }, /RESOURCE_REFERENCE_OPTIONS/);
  rejected(f => { f.originalProcess.arguments.push('--positive-only'); }, /RESOURCE_REFERENCE_PARENT_ARGUMENTS/);
  rejected(f => { f.controls.pop(); }, /RESOURCE_REFERENCE_RUN_SCOPE/);
  rejected(f => { f.controls.push(f.controls[1]); }, /RESOURCE_REFERENCE_RUNS_DUPLICATE/);
  rejected(f => { f.result.negativeMutations.length = 0; }, /RESOURCE_REFERENCE_NEGATIVE_SCOPE/);
  rejected(f => { f.controls[1].native.cases = f.controls[1].native.cases.filter(c => c.name === f.contract.mutants[0].failure);
    f.controls[1].native.count = 1; f.controls[1].native.passed = 0; }, /RESOURCE_REFERENCE_NATIVE_COUNT/);
  rejected(f => { f.controls[0].native.cases[0].name = f.controls[0].native.cases[1].name; }, /RESOURCE_REFERENCE_NATIVE_CASE_SCOPE/);
  rejected(f => { f.controls[0].native.cases.find(row => row.name === 'stalled callback actual resources at deadline').name = 'INERT unknown replacement'; }, /RESOURCE_REFERENCE_NATIVE_CASE_SCOPE/);
});

test('INERT resource undeclared tools and unknown compiler/link dependency scope reject', () => {
  rejected(f => { f.toolArtifacts.splice(f.toolArtifacts.findIndex(row => row.path === f.graph.selected.compiler),1); }, /RESOURCE_REFERENCE_TOOL_UNDECLARED/);
  rejected(f => { f.controls[0].dependencies.Data.Includes.push(path.join(f.root,'unknown.h')); }, /RESOURCE_REFERENCE_COMPILER_INPUT_UNDECLARED/);
  rejected(f => { f.graph.selected = {...f.graph.selected,compiler:path.join(f.root,'unknown-cl.exe')}; }, /RESOURCE_REFERENCE_SELECTED_TOOLS/);
  rejected(f => { f.controls[0].execution.actualLinkLibraries = []; }, /RESOURCE_REFERENCE_LINK_TRACE_SCOPE/);
});

test('INERT resource producer/child time windows reject; receipt overhead alone does not change original spawn budgets', () => {
  rejected(f => { f.entry.startedAt = time(-1); }, /RESOURCE_REFERENCE_PRODUCER_OUTSIDE_PARENT/);
  rejected(f => { f.entry.finishedAt = time(700000); }, /RESOURCE_REFERENCE_PRODUCER_OUTSIDE_PARENT/);
  rejected(f => { f.controls[0].execution.run.startedAt = time(500001); }, /RESOURCE_REFERENCE_PROCESS_WINDOW/);
  rejected(f => { f.controls[0].execution.run.startedAt = time(9); }, /RESOURCE_REFERENCE_PROCESS_WINDOW/);
  const overhead = fixture();
  for (const [i,control] of overhead.controls.entries()) {
    control.execution.build.startedAt = time(20 + i * 100000); control.execution.build.elapsedMs = 60000.5;
    control.execution.run.startedAt = time(60022 + i * 100000); control.execution.run.elapsedMs = 30000.5;
  }
  overhead.entry.elapsedMs = 900000; // Wall clock and monotonic accounting need not agree.
  overhead.refresh(); assert.equal(overhead.verify().status,'SHARED_REFERENCE');
  rejected(f => { f.controls[0].execution.build.budgetMs = 60001; }, /RESOURCE_REFERENCE_PROCESS_BUDGET/);
});

test('INERT resource unknown family/path/case/environment and reparse artifact ancestry reject', () => {
  const family = fixture(); assert.throws(() => verifyNativeResourceReference({...family.options,family:'other'}),/RESOURCE_REFERENCE_UNKNOWN_FAMILY/);
  rejected(f => { f.controls[0].paths.temp = path.join(f.root,'outside-temp'); }, /RESOURCE_REFERENCE_PATHS_SCOPE/);
  rejected(f => { f.controls[0].native.cases[0].name = 'INERT unknown scope'; }, /RESOURCE_REFERENCE_NATIVE_CASE_SCOPE/);
  rejected(f => { f.entry.environment.UNDECLARED = 'inert'; }, /RESOURCE_REFERENCE_ENVIRONMENT_SCOPE/);
  rejected(f => { f.sourceInputs.push({path:'../unknown.cpp',sha256:'0'.repeat(64)}); }, /RESOURCE_REFERENCE_SOURCE_PATH_INVALID/);
  const linked = fixture(), dir = path.join(linked.root,'reparse-alias'); fs.symlinkSync(path.dirname(linked.controls[0].execution.build.stdout.path),dir,'junction');
  linked.controls[0].execution.build.stdout = {...linked.controls[0].execution.build.stdout,path:path.join(dir,'build.stdout.log')};
  linked.refresh(); assert.throws(linked.verify,/RESOURCE_REFERENCE_PIN_PATH|RESOURCE_REFERENCE_REPARSE_PATH/);
});

test('INERT resource actual FAIL, wrong semantic failure and non-natural exits are never admitted as a shared success', () => {
  rejected(f => { f.result.pass = false; }, /RESOURCE_REFERENCE_RESULT_NOT_PASS/);
  rejected(f => { f.originalProcess.exitCode = 2; }, /RESOURCE_REFERENCE_PARENT_EXIT/);
  rejected(f => { const c=f.controls[0].native.cases[0];c.pass=false;c.error='INERT failure';f.controls[0].native.passed--;f.controls[0].native.pass=false; }, /RESOURCE_REFERENCE_NATIVE_VERDICT/);
  rejected(f => { f.controls[1].native.cases.find(c => c.name === f.contract.mutants[0].failure).pass = true; f.controls[1].native.passed++; }, /RESOURCE_REFERENCE_SEMANTIC_FAILURE_MISSING/);
  rejected(f => { f.controls[1].execution.run.status = 1; }, /RESOURCE_REFERENCE_PROCESS_EXIT/);
  rejected(f => { f.controls[1].execution.run.natural = false; }, /RESOURCE_REFERENCE_PROCESS_NOT_NATURAL/);
  rejected(f => { f.controls[1].execution.run.signal = 'SIGTERM'; }, /RESOURCE_REFERENCE_PROCESS_SIGNAL/);
  rejected(f => { f.controls[1].execution.run.error = 'INERT timeout'; }, /RESOURCE_REFERENCE_PROCESS_ERROR/);
});

// Rebind INERT source metadata consistently so these controls reach the name parser.
function changeBoundCpp(f, change) {
  const live = path.join(f.repo, f.cpp), row = f.frozen.find(row => row.path === f.cpp);
  write(live, change(fs.readFileSync(live, 'utf8'))); write(row.captured, fs.readFileSync(live));
  row.sha256 = hash(live); f.sourceInputs.find(item => item.path === f.cpp).sha256 = row.sha256;
  const audit = f.audit.rows.find(item => item.path === f.cpp);
  audit.beforeSha256 = audit.afterSha256 = row.sha256; audit.captured = pin(row.captured);
}
test('INERT cache five parameter families expand exact93 names; missing duplicate unknown or runtime substitutions reject', () => {
  const good = fixture('cache', true); assert.equal(good.verify().ordinaryCases, 93);
  const badSource = [
    [text => text.replace('"trailing"};\nfor(const auto& kind:slotStorm)', '"duplicate"};\nfor(const auto& kind:slotStorm)'), /SOURCE_PARAMETER_VALUES/],
    [text => text.replace('"storm IO "+kind+', '"unknown IO "+kind+'), /SOURCE_PARAMETER_FAMILY/],
    [text => text.replace('check(("storm IO "+kind+', 'other(("storm IO "+kind+'), /SOURCE_PARAMETER_FAMILY_COUNT/],
    [text => text.replace('"storm IO "+kind+', '"storm IO "+other+'), /SOURCE_PARAMETER_EXPRESSION/],
  ];
  for (const [change, pattern] of badSource) {
    const f = fixture('cache', true); changeBoundCpp(f, change); f.refresh(); assert.throws(f.verify, pattern);
  }
  for (const action of [
    rows => { rows.find(row => row.name.startsWith('storm slot ')).name = 'unknown runtime substitution'; },
    rows => { const i = rows.findIndex(row => row.name.startsWith('storm slot ')); rows[i].name = rows[i + 1].name; },
    rows => { rows.splice(rows.findIndex(row => row.name.startsWith('storm slot ')), 1); },
  ]) {
    const f = fixture('cache', true); action(f.controls[0].native.cases); f.refresh();
    assert.throws(f.verify, /RESOURCE_REFERENCE_NATIVE_(CASE_SCOPE|CASE_COUNT|COUNT)/);
  }
});
test('INERT official Loader data requires exact parent source binding and cannot borrow tool authority', () => {
  const good = fixture(); assert(!good.toolArtifacts.some(row => row.path === good.graph.officialData.path));
  assert.equal(good.verify().status, 'SHARED_REFERENCE');
  rejected(f => { f.sourceInputs.splice(f.sourceInputs.findIndex(row => row.path === 'PowerControl/decky/PluginLoader_noconsole.exe'), 1); f.toolArtifacts.push(f.graph.officialData); }, /SOURCE_UNDECLARED/);
  rejected(f => { f.sourceInputs.find(row => row.path === 'PowerControl/decky/PluginLoader_noconsole.exe').sha256 = '0'.repeat(64); }, /SOURCE_NOT_PARENT_BOUND/);
  rejected(f => { write(f.graph.officialData.path, 'INERT changed official bytes'); }, /PIN_HASH/);
  rejected(f => { f.result.officialLoaderExecuted = true; }, /RESULT_SCOPE/);
});

// These owned-producer receipts are INERT metadata too; the existing verifier is the consumer.
function ownedMetadataFixture(family){
 const f=fixture(family),base=path.dirname(f.out),owned=path.join(base,'owned-resource-'+family);
 const shell=pin(write(path.join(f.root,'INERT-tools/pwsh.exe'),'INERT shell, never executed\n'));
 f.toolArtifacts.push(shell);
 const hostRelative='tools/ymcc_sim/global/host-process.ps1',host=pin(write(path.join(f.repo,hostRelative),'INERT helper source, never executed\n'));
 f.sourceInputs.push({path:hostRelative,sha256:host.sha256});
 f.baselineValue.sourceInputs=f.sourceInputs;
 f.baselineValue.resourceScheduling={mode:'owned-parallel',mutations:true};
 const args=[path.join(f.repo,f.contract.module),f.out],argumentFile=path.join(owned,'arguments.json'),requestFile=path.join(owned,'request.json'),terminalFile=path.join(owned,'terminal.json'),processFile=path.join(base,'resource-'+family+'-process.json');
 const filetime=ms=>((BigInt(Date.parse(time(ms)))+11644473600000n)*10000n).toString();
 const job={disposed:true,cleanupError:null,activeProcessesFinal:0,zeroObservedBeforeDispose:true,terminateRequested:false};
 const business={pid:5252,program:f.entry.caller.node,arguments:args,cwd:f.repo,atomicJobAdmission:true,startFileTimeUtc:filetime(10)};
 f.owner.processId=business.pid;
 const terminal={kind:'ymcc-raw-program-terminal-v1',reason:'NATURAL',collectionComplete:true,declaredPinsUnchanged:true,logsClosedAndFlushed:true,businessExitCode:0,timedOut:false,outputLimitExceeded:false,business,owner:{pid:f.originalProcess.pid,expectedStartTicks:'638974512000000000'},launcher:{pid:5151,program:shell,helper:host},job,finishedEpochMs:Date.parse(time(200000)),deadlineEpochMs:Date.parse(time(220000)),businessExitFileTimeUtc:filetime(199999)};
 const request={cwd:f.repo,sources:f.sourceInputs.map(row=>({path:path.resolve(f.repo,row.path),sha256:row.sha256})),tools:f.toolArtifacts};
 const logs={};for(const stream of ['stdout','stderr']){const value=stream==='stdout'?'INERT raw data\n':'';logs[stream]=pin(write(path.join(owned,stream+'.raw'),value));write(path.join(base,'resource-'+family+'.'+stream+'.bin'),value);}terminal.logs=logs;
 const proof={id:'resource-'+family,pid:business.pid,program:business.program,arguments:args,cwd:f.repo,startedAt:time(10),finishedAt:time(200000),elapsedMs:199990,originalBudgetMs:240000,timeoutMs:240000,exitCode:0,error:null,signal:null,timedOut:false,outputTruncated:false,stdout:pin(path.join(base,'resource-'+family+'.stdout.bin')),stderr:pin(path.join(base,'resource-'+family+'.stderr.bin'))};
 function refreshOwned(){
  write(argumentFile,args);request.arguments=pin(argumentFile);write(requestFile,request);
  terminal.request=pin(requestFile);terminal.argumentsPin=pin(argumentFile);
  terminal.declaredInputsBefore=[...request.sources.map(row=>({...row,role:'sources'})),...request.tools.map(row=>({...row,role:'tools'}))];terminal.declaredInputsAfter=terminal.declaredInputsBefore.map(({path,sha256})=>({path,sha256}));write(terminalFile,terminal);
  proof.rawExecution={terminal:pin(terminalFile),request:pin(requestFile),arguments:pin(argumentFile),collectionComplete:true,actualBusiness:business,helperSource:host,helperPid:terminal.launcher.pid,job,completion:{kind:'NATURAL',forcedToolFinalization:false,productResourcesWaived:false}};
  write(processFile,proof);f.entry.ownedProducer={schemaVersion:1,mode:'complete-original-cli',process:pin(processFile)};f.refresh();
 }
 refreshOwned();return {...f,terminal,proof,filetime,refreshOwned};
}
test('INERT owned resource full-reference binds actual child owner and native lifetime without replay',()=>{
 for(const family of ['cache','worker']){const good=ownedMetadataFixture(family),result=good.verify();assert.equal(result.status,'SHARED_REFERENCE');assert.equal(result.ordinaryCases,good.contract.positiveCaseCount);assert.equal(result.originalNegativeFailuresPreserved.length,good.contract.mutants.length);assert.equal(result.originalProducerExecutionsByThisReference,0);}
 const staleOwner=ownedMetadataFixture('worker');staleOwner.owner.processId=staleOwner.originalProcess.pid;staleOwner.refreshOwned();assert.throws(staleOwner.verify,/RESOURCE_REFERENCE_OWNER_PID/);
 const missing=ownedMetadataFixture('worker');delete missing.entry.ownedProducer;missing.refresh();assert.throws(missing.verify,/RESOURCE_REFERENCE_OWNED_PRODUCER_REQUIRED_BY_SCHEDULE/);
 const costOnly=ownedMetadataFixture('worker');costOnly.entry.ownedProducer.mode='positive-only-cost-measurement';costOnly.refresh();assert.throws(costOnly.verify,/RESOURCE_REFERENCE_OWNED_MODE/);
 const afterExit=ownedMetadataFixture('worker');afterExit.terminal.businessExitFileTimeUtc=afterExit.filetime(15);afterExit.refreshOwned();assert.throws(afterExit.verify,/RESOURCE_REFERENCE_PROCESS_WINDOW/);
});


import {nativeHealthCompileRequest,nativeRuntimeCompileRequest,nativeHealthEnvironment,verifyNativeHealthReference,verifyNativeRuntimeReference} from './release-resource-reference.mjs';

test('INERT native health single producer shared reference metadata control',()=>{
  const evidence=path.join(simulationCorePath('Evidence'),'ParentSecondaryDedup-20261103/NativeHealthSharedReference/Helper');
  fs.mkdirSync(evidence,{recursive:true});
  const batch=fs.mkdtempSync(path.join(evidence,'metadata-'));
  const coverage={schemaVersion:1,scope:'INERT metadata only; all executable files are text and never executed',positive:0,rejections:[]};
  const saveCoverage=()=>write(path.join(batch,'COVERAGE.json'),coverage);
  const clone=value=>JSON.parse(JSON.stringify(value));
  function nativeFixture(family){
    const runtime=family==='runtime',toolchainField=runtime?'nativeRuntimeToolchain':'nativeHealthToolchain',inputsField=runtime?'nativeRuntimeInputs':'nativeHealthInputs';
    const compileRequest=runtime?nativeRuntimeCompileRequest:nativeHealthCompileRequest,verifyReference=runtime?verifyNativeRuntimeReference:verifyNativeHealthReference;
    const header='native/decky_sidebar_'+family+'.h',marker=runtime?'YMCC Decky sidebar native no-launch tests passed:':'DECKY_HEALTH_POLICY_OK';
    const root=fs.mkdtempSync(path.join(batch,'control-')),repo=path.join(root,'repo'),core=path.join(root,'core');
    const work=path.join(core,'Workspace/parent-owned'),parentResult=path.join(core,'Evidence/parent/result.json');
    const sourceRoot=path.join(work,'src'),outputRoot=path.join(work,'n',family);
    fs.mkdirSync(outputRoot,{recursive:true});
    const inputs=['tools/decky_sidebar_'+family+'_selftest.cpp',header,'deps/json/json.hpp'];
    const sourceInputs=new Map();
    for(const file of inputs){const content='INERT frozen source '+file+'\n';write(path.join(repo,file),content);write(path.join(sourceRoot,file),content);sourceInputs.set(file,hash(path.join(repo,file)));}
    const compiler=write(path.join(root,'tools/cl.exe'),'INERT compiler text NEVER EXECUTE\n');
    const linker=write(path.join(root,'tools/link.exe'),'INERT linker text NEVER EXECUTE\n');
    const external=write(path.join(root,'tools/sdk.h'),'INERT external dependency\n');
    const library=write(path.join(root,'tools/advapi32.lib'),'INERT library\n');
    const include=path.dirname(external),lib=path.dirname(library);
    const authority={compiler,linker,include:[include],lib:[lib]};
    const toolArtifacts=[compiler,linker,external,library].map(pin);
    for(const name of ["user32.lib", "userenv.lib", "winhttp.lib", "ws2_32.lib", "bcrypt.lib", ...(runtime?["shell32.lib"]:[]), "libcmt.lib", "libcpmt.lib", "libvcruntime.lib", "libucrt.lib", "oldnames.lib", "kernel32.lib"]){toolArtifacts.push(pin(write(path.join(lib,name),'INERT required library '+name)));}
    const recipe=compileRequest({sourceRoot,outputRoot,compiler});
    write(recipe.image,'INERT image text NEVER EXECUTE\n');
    const graph={Version:'1.2',Data:{Source:recipe.source,Includes:[...inputs.slice(1).map(file=>path.join(sourceRoot,file)),external]}};
    write(recipe.graph,graph);
    const system=nativeHealthEnvironment(process.env);
    const env=nativeHealthEnvironment({SystemRoot:system.SystemRoot,ComSpec:system.ComSpec,
      PATH:path.dirname(compiler)+';'+path.dirname(linker)+';'+system.SystemRoot+'/System32',INCLUDE:include,LIB:lib,
      LIBPATH:'',CL:'',_CL_:'',LINK:'',_LINK_:'',TEMP:work,TMP:work});
    const parent={work,startedAt:time(0),finishedAt:time(1000),[toolchainField]:{compiler:pin(compiler),linker:pin(linker),include:[include],lib:[lib]},cases:[],processes:[]};
    const processPath=id=>path.join(path.dirname(parentResult),'processes',id+'.json');
    const proof=(id,program,args,start,finish,budget,stdout)=>({id,pid:start+100,program:pin(program),arguments:args,cwd:outputRoot,
      startedAt:time(start),finishedAt:time(finish),elapsedMs:finish-start,originalBudgetMs:budget,timeoutMs:budget,
      exitCode:0,signal:null,error:null,timedOut:false,outputTruncated:false,
      stdout:pin(write(processPath(id).replace(/\.json$/,'.stdout.bin'),stdout)),
      stderr:pin(write(processPath(id).replace(/\.json$/,'.stderr.bin'),'')),[inputsField]:{schemaVersion:1,before:clone(env),after:clone(env)}});
    const compile=proof('build.original-native-'+family,compiler,recipe.arguments,10,100,60000,'INERT compiler log\n');
    const run=proof('original.native-'+family,recipe.image,[],101,200,15000,marker+' 42\n');
    parent.processes.push(compile,run);
    parent.cases.push({id:compile.id,passed:true,...(runtime?{legacyMacro:true}:{}),compilerGraph:pin(recipe.graph),exe:pin(recipe.image)},
      {id:run.id,passed:true,...(runtime?{legacyRegressionOnly:true,currentTypedAcceptance:false}:{}),exe:pin(recipe.image),process:null});
    const refresh=()=>{for(const row of parent.processes)write(processPath(row.id),row);parent.cases[1].process=pin(processPath(run.id));write(parentResult,parent);};
    refresh();
    return {family,header,marker,toolchainField,inputsField,verifyReference,root,repo,core,work,sourceRoot,outputRoot,sourceInputs,toolArtifacts,authority,recipe,graph,env,parent,parentResult,compile,run,external,compiler,linker,processPath,refresh,
      verify:()=>verifyReference({repoRoot:repo,coreRoot:core,parentResult,parent,sourceInputs,toolArtifacts,nativeAuthority:authority})};
  }
  for(const family of ['health','runtime']){
  const healthFixture=()=>nativeFixture(family),positiveBefore=coverage.positive,rejectionsBefore=coverage.rejections.length;
  const good=healthFixture(),result=good.verify();
  assert.equal(result.status,'SHARED_REFERENCE');assert.equal(result.requiredReleaseBuildStep,'official-native-'+family+'-build');assert.equal(result.requiredReleaseRunStep,'official-native-'+family);
  for(const field of ['cached','executedThisRun','actualPassedThisRun'])assert.equal(result[field],false);
  for(const field of ['originalProducerExecutionsByThisReference','compilerInvocationsByThisReference'])assert.equal(result[field],0);
  for(const field of ['originalProducerExecutionsWithinParent','compilerInvocationsWithinParent'])assert.equal(result[field],1);
  assert.deepEqual(result.originalProgram,pin(good.recipe.image));assert.deepEqual(result.compilerGraph,pin(good.recipe.graph));
  assert.deepEqual(result.originalCompileProcess,pin(good.processPath(good.compile.id)));assert.deepEqual(result.originalRunProcess,pin(good.processPath(good.run.id)));
  assert.equal(result.originalCompileElapsedMs,90);assert.equal(result.originalRunElapsedMs,99);
  const folded=nativeHealthEnvironment({systemroot:'x',pAtH:'p',cl:'/DNDEBUG',SECRET:'private'});
  assert.equal(folded.SystemRoot,'x');assert.equal(folded.PATH,'p');assert.equal(folded.CL,'/DNDEBUG');assert.equal(folded.LIB,null);assert.equal(Object.hasOwn(folded,'SECRET'),false);
  assert.throws(()=>nativeHealthEnvironment({CL:'',cl:'/DNDEBUG'}),/ENVIRONMENT_CASE_COLLISION/);
  assert.deepEqual(Object.keys(good.recipe),['source','image','graph','compiler','cwd','arguments','runArguments']);
  const independentArguments=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE',
    '/I'+path.join(good.sourceRoot,'native'),'/I'+path.join(good.sourceRoot,'deps/json'),good.recipe.source,
    '/sourceDependencies',good.recipe.graph,'/Fo'+good.recipe.image+'.obj','/Fe'+good.recipe.image,'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64',
    'advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib',...(family==='runtime'?['shell32.lib']:[])];
  assert.deepEqual(good.recipe.arguments,independentArguments);assert.deepEqual(good.recipe.runArguments,[]);assert.equal(result.fullProductAcceptance,false);coverage.positive++;saveCoverage();
  const unused=healthFixture(),unusedPath=write(path.join(unused.root,'unrelated-tool.bin'),'INERT unused tool'),unusedPin=pin(unusedPath);unused.toolArtifacts.push(unusedPin);unused.refresh();
  const originalReadFile=fs.readFileSync;let unusedReads=0;
  try{fs.readFileSync=function(file,...args){if(typeof file==='string'&&key(file)===key(unusedPath))unusedReads++;return originalReadFile.call(this,file,...args);};assert.equal(unused.verify().status,'SHARED_REFERENCE');}finally{fs.readFileSync=originalReadFile;}
  assert.equal(unusedReads,0,'The leaf must not rehash unrelated tools already checked by its complete-parent caller');coverage.positive++;
  coverage.unrelatedToolReads=unusedReads;
  const reject=(name,change,reason,after)=>{
    const f=healthFixture();change(f);f.refresh();if(after)after(f);
    let observed;
    assert.throws(f.verify,error=>{observed=String(error.message);return observed.startsWith('NATIVE_'+family.toUpperCase()+'_REFERENCE_') && reason.test(observed);},name);
    coverage.rejections.push({family,name,expected:reason.source,observed});saveCoverage();
  };
  reject('extra producer with original ID',f=>f.parent.processes.push(clone(f.compile)),/PRODUCER_EXECUTIONS_NOT_UNIQUE/);
  reject('extra producer with alternate ID',f=>f.parent.processes.push({...clone(f.compile),id:'unexpected.health'}),/PRODUCER_EXECUTIONS_NOT_UNIQUE/);
  reject('extra image invocation',f=>f.parent.processes.push({...clone(f.run),id:'unexpected.run'}),/PRODUCER_EXECUTIONS_NOT_UNIQUE/);
  reject('duplicate case',f=>f.parent.cases.push(clone(f.parent.cases[0])),/CASE_NOT_UNIQUE/);
  reject('failed build case',f=>f.parent.cases[0].passed=false,/CASE_NOT_PASSED/);
  reject('CL NDEBUG injection',f=>{f.compile[f.inputsField].before.CL='/DNDEBUG';f.compile[f.inputsField].after.CL='/DNDEBUG';},/ENVIRONMENT_FIXED/);
  reject('run CL injection',f=>{f.run[f.inputsField].before.CL='/DNDEBUG';f.run[f.inputsField].after.CL='/DNDEBUG';},/ENVIRONMENT_FIXED/);
  for(const envKey of ['LIBPATH','_CL_','LINK','_LINK_','INCLUDE','LIB','PATH','TEMP','TMP','SystemRoot','ComSpec'])
    reject('fixed environment '+envKey,f=>{f.compile[f.inputsField].before[envKey]='unexpected';f.compile[f.inputsField].after[envKey]='unexpected';},/ENVIRONMENT_FIXED/);
  reject('environment changes after invocation',f=>f.compile[f.inputsField].after.CL='/DNDEBUG',/ENVIRONMENT_FIXED|ENVIRONMENT_CHANGED/);
  reject('missing observation',f=>delete f.run[f.inputsField],/ENVIRONMENT_OBSERVATION_REQUIRED/);
  reject('private full environment is not a proof',f=>{f.compile[f.inputsField].before.PRIVATE_SECRET='not allowed';},/ENVIRONMENT_OBSERVATION_SCOPE/);
  reject('NDEBUG flag',f=>f.compile.arguments.push('/DNDEBUG'),/PROCESS_ARGUMENTS/);
  reject('missing console flag',f=>f.compile.arguments=f.compile.arguments.filter(arg=>arg!=='/SUBSYSTEM:CONSOLE'),/PROCESS_ARGUMENTS/);
  reject('missing x64 flag',f=>f.compile.arguments=f.compile.arguments.filter(arg=>arg!=='/MACHINE:x64'),/PROCESS_ARGUMENTS/);
  reject('missing sourceDependencies switch',f=>f.compile.arguments=f.compile.arguments.filter(arg=>arg!=='/sourceDependencies'),/PROCESS_ARGUMENTS/);
  reject('extra shell32 library',f=>f.compile.arguments.push('shell32.lib'),/PROCESS_ARGUMENTS/);
  reject('run arguments',f=>f.run.arguments.push('--unknown'),/PROCESS_ARGUMENTS/);
  reject('cwd drift',f=>f.compile.cwd=f.repo,new RegExp(family.toUpperCase()+'_PROCESS_CWD'));
  reject('compiler pin drift',f=>f.compile.program.sha256='0'.repeat(64),/PIN_HASH/);
  reject('compiler authority drift',f=>f.authority.compiler=f.linker,/PIN_PATH/);
  reject('linker undeclared',f=>f.toolArtifacts.splice(1,1),/TOOL_UNDECLARED/);
  reject('required link library undeclared',f=>{const at=f.toolArtifacts.findIndex(x=>path.basename(x.path)==='user32.lib');f.toolArtifacts.splice(at,1);},/LIBRARY_TOOL_UNDECLARED/);
  reject('required link library changed',f=>write(path.join(f.authority.lib[0],'user32.lib'),'CHANGED LIBRARY'),/PIN_HASH/);
  reject('current-directory library shadow',f=>write(path.join(f.outputRoot,'user32.lib'),'INERT undeclared shadow'),/LIBRARY_CWD_SHADOW/);
  reject('toolchain include drift',f=>f.parent[f.toolchainField].include=[],/TOOLCHAIN_INCLUDE/);
  reject('external tool bytes drift',f=>write(f.external,'changed SDK bytes'),/PIN_HASH/);
  reject('frozen source drift',f=>write(f.recipe.source,'changed cpp'),/FROZEN_SOURCE_HASH/);
  reject('current cpp source drift',f=>write(path.join(f.repo,path.relative(f.sourceRoot,f.recipe.source)),'changed live cpp'),/CURRENT_SOURCE_HASH/);
  reject('current header drift',f=>write(path.join(f.repo,f.header),'changed live header'),/CURRENT_SOURCE_HASH/);
  reject('undeclared frozen header',f=>f.sourceInputs.delete(f.header),/GRAPH_SOURCE_UNDECLARED/);
  const changeGraph=(f,change)=>{change(f.graph);write(f.recipe.graph,f.graph);f.parent.cases[0].compilerGraph=pin(f.recipe.graph);};
  reject('graph unknown superset',f=>changeGraph(f,g=>g.Data.Includes.push(write(path.join(f.root,'unknown.h'),'unknown'))),/GRAPH_TOOL_UNDECLARED/);
  reject('graph source mismatch',f=>changeGraph(f,g=>g.Data.Source=path.join(f.sourceRoot,f.header)),new RegExp(family.toUpperCase()+'_GRAPH_SOURCE'));
  reject('graph duplicate dependency',f=>changeGraph(f,g=>g.Data.Includes.push(g.Data.Includes[0])),/GRAPH_DUPLICATE/);
  reject('graph dependency escapes freeze',f=>{const live=path.join(f.repo,f.header);f.toolArtifacts.push(pin(live));changeGraph(f,g=>g.Data.Includes[0]=live);},/GRAPH_INPUT_OUTSIDE_FREEZE/);
  reject('graph artifact SHA drift',f=>write(f.recipe.graph,'{}'),/PIN_HASH/);
  reject('image bytes drift',f=>write(f.recipe.image,'changed image text'),/PIN_HASH/);
  reject('run image mismatch',f=>f.run.program=pin(f.compiler),/PIN_PATH/);
  reject('run process case pin mismatch',f=>{},/PIN_HASH/,f=>{f.parent.cases[1].process.sha256='0'.repeat(64);write(f.parentResult,f.parent);});
  reject('original process object mismatch',f=>{},/PROCESS_ORIGINAL_CHANGED/,f=>{const p=clone(f.compile);p.pid++;write(f.processPath(p.id),p);});
  reject('original parent object mismatch',f=>{},/PARENT_ORIGINAL_CHANGED/,f=>write(f.parentResult,{...f.parent,finishedAt:time(999)}));
  reject('compile outside parent window',f=>f.compile.startedAt=time(-1),/PROCESS_WINDOW/);
  reject('run before compile completed',f=>f.run.startedAt=time(50),/PROCESS_ORDER/);
  reject('invalid elapsed duration',f=>f.run.elapsedMs=-1,/PROCESS_TIMING/);
  reject('missing PID',f=>f.run.pid=null,/PROCESS_PID/);
  // PID reuse and post-return bookkeeping cannot invent a child timeout or a second producer.
  const reusedPid=healthFixture();reusedPid.run.pid=reusedPid.compile.pid;reusedPid.refresh();assert.equal(reusedPid.verify().status,'SHARED_REFERENCE');coverage.positive++;
  const bookkeeping=healthFixture();bookkeeping.run.elapsedMs=15001;bookkeeping.refresh();assert.equal(bookkeeping.verify().status,'SHARED_REFERENCE');coverage.positive++;
  coverage.allowedBoundaries=['non-overlapping original invocations may reuse PID','natural-exit receipt bookkeeping is not a child timeout'];
  for(const [field,value] of [['exitCode',1],['signal','SIGTERM'],['error','spawn failed'],['timedOut',true],['outputTruncated',true]])
    reject('non-natural '+field,f=>f.run[field]=value,new RegExp('PROCESS_'+field));
  reject('explicit non-natural exit',f=>f.run.natural=false,/PROCESS_NOT_NATURAL/);
  reject('marker missing',f=>{write(f.run.stdout.path,'INERT no health marker\n');f.run.stdout=pin(f.run.stdout.path);},/STDOUT_MARKER/);
  reject('marker suffix spoof',f=>{write(f.run.stdout.path,f.marker+(family==='runtime'?' 42_SPOOF':'_SPOOF')+'\n');f.run.stdout=pin(f.run.stdout.path);},/STDOUT_MARKER/);
  reject('stdout original pin drift',f=>write(f.run.stdout.path,'changed log'),/PIN_HASH/);
  reject('stderr original pin drift',f=>write(f.compile.stderr.path,'changed log'),/PIN_HASH/);
  reject('evidence escapes core',f=>{const outside=write(path.join(f.root,'outside-result.json'),f.parent);f.verify=()=>f.verifyReference({repoRoot:f.repo,coreRoot:f.core,parentResult:outside,parent:f.parent,sourceInputs:f.sourceInputs,toolArtifacts:f.toolArtifacts,nativeAuthority:f.authority});},/PARENT_OUTSIDE_CORE/);
  reject('work escapes core',f=>f.parent.work=f.repo,/WORK_OUTSIDE_CORE/);
  // Both wrappers must reject borrowed family identities and keep the same original-file gates.
  const otherFamily=family==='health'?'runtime':'health';
  const otherToolchain=family==='health'?'nativeRuntimeToolchain':'nativeHealthToolchain',otherInputs=family==='health'?'nativeRuntimeInputs':'nativeHealthInputs';
  reject('other family toolchain',f=>{f.parent[otherToolchain]=f.parent[f.toolchainField];delete f.parent[f.toolchainField];},/TOOLCHAIN_REQUIRED/);
  reject('other family environment observation',f=>{f.run[otherInputs]=f.run[f.inputsField];delete f.run[f.inputsField];},/ENVIRONMENT_OBSERVATION_REQUIRED/);
  for(const envKey of ['LIBPATH','_CL_','LINK','_LINK_','INCLUDE','LIB','PATH','TEMP','TMP','SystemRoot','ComSpec'])
    reject('run fixed environment '+envKey,f=>{f.run[f.inputsField].before[envKey]='unexpected';f.run[f.inputsField].after[envKey]='unexpected';},/ENVIRONMENT_FIXED/);
  reject('other family case IDs',f=>{f.parent.cases[0].id='build.original-native-'+otherFamily;f.parent.cases[1].id='original.native-'+otherFamily;},/CASE_NOT_UNIQUE/);
  reject('other family compile source',f=>{const other=path.join(f.sourceRoot,'tools/decky_sidebar_'+otherFamily+'_selftest.cpp');f.compile.arguments=f.compile.arguments.map(arg=>arg===f.recipe.source?other:arg);},/PROCESS_ARGUMENTS/);
  reject('alternate relative producer source',f=>f.parent.processes.push({...clone(f.compile),id:'unexpected.relative',program:pin(f.linker),arguments:['tools\\decky_sidebar_'+family+'_selftest.cpp']}),/PRODUCER_EXECUTIONS_NOT_UNIQUE/);
  reject('other family stdout marker',f=>{write(f.run.stdout.path,family==='health'?'YMCC Decky sidebar native no-launch tests passed: 42\n':'DECKY_HEALTH_POLICY_OK 42\n');f.run.stdout=pin(f.run.stdout.path);},/STDOUT_MARKER/);
  reject('compile budget changed',f=>f.compile.originalBudgetMs=60001,/PROCESS_BUDGET/);
  reject('run budget changed',f=>f.run.originalBudgetMs=15001,/PROCESS_BUDGET/);
  reject('compile timeout expanded',f=>f.compile.timeoutMs=60001,/PROCESS_TIMING/);
  reject('run timeout expanded',f=>f.run.timeoutMs=15001,/PROCESS_TIMING/);
  reject('zero run timeout',f=>f.run.timeoutMs=0,/PROCESS_TIMING/);
  reject('missing compile timeout',f=>delete f.compile.timeoutMs,/PROCESS_TIMING/);
  reject('graph schema version changed',f=>changeGraph(f,g=>g.Version='1.3'),/GRAPH_VERSION/);
  reject('graph dependency list missing',f=>changeGraph(f,g=>delete g.Data.Includes),/GRAPH_REQUIRED/);
  reject('run stdout borrowed from another original',f=>{f.run.stdout=pin(write(path.join(f.root,'borrowed.stdout.bin'),f.marker+' 42\n'));},/PIN_PATH/);
  reject('compile process case pin forged',f=>{f.parent.cases[0].process={...pin(f.processPath(f.compile.id)),sha256:'0'.repeat(64)};},/PIN_HASH/);
  reject('original run object mismatch',f=>{},/PROCESS_ORIGINAL_CHANGED/,f=>{const p=clone(f.run);p.arguments=['--forged'];write(f.processPath(p.id),p);});
  const clipped=healthFixture();clipped.compile.timeoutMs=50000;clipped.run.timeoutMs=10000;clipped.refresh();assert.equal(clipped.verify().status,'SHARED_REFERENCE');coverage.positive++;
  const lf=healthFixture();write(lf.run.stdout.path,'INERT prior output\n'+lf.marker+' 42\r\n');lf.run.stdout=pin(lf.run.stdout.path);lf.refresh();assert.equal(lf.verify().status,'SHARED_REFERENCE');coverage.positive++;
  if(family==='runtime'){
    reject('runtime legacy build role missing',f=>delete f.parent.cases[0].legacyMacro,/LEGACY_BUILD_ROLE/);
    reject('runtime legacy run role changed',f=>f.parent.cases[1].legacyRegressionOnly=false,/LEGACY_RUN_ROLE/);
    reject('runtime relabelled current typed',f=>f.parent.cases[1].currentTypedAcceptance=true,/LEGACY_TYPED_SCOPE/);
    reject('runtime missing shell32 argument',f=>f.compile.arguments=f.compile.arguments.filter(arg=>arg!=='shell32.lib'),/PROCESS_ARGUMENTS/);
    reject('runtime shell32 tool undeclared',f=>{f.toolArtifacts.splice(f.toolArtifacts.findIndex(row=>path.basename(row.path)==='shell32.lib'),1);},/LIBRARY_TOOL_UNDECLARED/);
    for(const [name,text] of [
      ['missing numeric checks','YMCC Decky sidebar native no-launch tests passed: \n'],
      ['non-numeric checks','YMCC Decky sidebar native no-launch tests passed: checks\n'],
      ['signed checks','YMCC Decky sidebar native no-launch tests passed: +42\n'],
      ['decimal checks','YMCC Decky sidebar native no-launch tests passed: 42.0\n'],
      ['extra marker suffix','YMCC Decky sidebar native no-launch tests passed: 42 extra\n'],
      ['marker prefix spoof','SPOOF YMCC Decky sidebar native no-launch tests passed: 42\n'],
      ['non-final marker','YMCC Decky sidebar native no-launch tests passed: 42\nFAIL\n'],
      ['extra trailing blank line','YMCC Decky sidebar native no-launch tests passed: 42\n\n']
    ])reject('runtime '+name,f=>{write(f.run.stdout.path,text);f.run.stdout=pin(f.run.stdout.path);},/STDOUT_MARKER/);
  }
  coverage.families??={};coverage.families[family]={positive:coverage.positive-positiveBefore,rejections:coverage.rejections.length-rejectionsBefore};
  }
  saveCoverage();console.log('NATIVE_HEALTH_METADATA_COVERAGE '+JSON.stringify({positive:coverage.positive,rejections:coverage.rejections.length,file:path.join(batch,'COVERAGE.json')}));
});
