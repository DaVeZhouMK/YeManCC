import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import crypto from 'node:crypto';
import {fileURLToPath, pathToFileURL} from 'node:url';

const moduleFile = fileURLToPath(import.meta.url);
const moduleDir = path.dirname(moduleFile);
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const pin = (file, bytes) => ({file, bytes: bytes.length, sha256: digest(bytes)});

// Source-only startup-overlay recipe. No compiler or fixture is invoked here.
// The caller retains the original 120000ms reference, tool pins and total budget.
export async function prepareSteamOverlayStartup({repoRoot, outputDir}) {
  assert.equal(typeof repoRoot,'string','repoRoot is required');
  assert.equal(typeof outputDir,'string','outputDir is required');
  assert(repoRoot.length&&outputDir.length,'Explicit nonempty paths required');
  const repo=fs.realpathSync(path.resolve(repoRoot)),out=path.resolve(outputDir);
  assert(!fs.existsSync(out),'Fresh startup overlay preparation directory required');
  assert(out!==repo&&out!==moduleDir,'Private preparation directory required');
  const sources=[];
  const save=(relative,value)=>{
    const bytes=Buffer.isBuffer(value)?value:Buffer.from(value),file=path.join(out,relative);
    fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes,{flag:'wx'});
    return pin(file,bytes);
  };
  const relativeInputs=[
    'tools/steam_settings_batch_selftest.cpp','tools/fixtures/steam_overlay_startup_identity_cases.inc',
    'native/main.cpp','native/steam_settings_runtime.h','native/steam_overlay_startup.h',
    'native/sleep_steam_preferences.h','native/steam_monitor_settings.h','native/steamdeck_mouse_vdf.h',
    'native/steam_session_observer.h','native/process_inventory.h','native/steam_window_event_budget.h',
    'native/worker_thread_name.h','native/steam_listener_executor.h','deps/json/json.hpp',
    'tools/fixtures/steam_session_observer_publication.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs',
    'tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/steam_settings_module_profile_preparation/prepare.mjs',
    'tools/fixtures/steam_settings_module_profile_preparation/fixture.inc',
    'tools/fixtures/steam_settings_module_profile_preparation/before-queue.inc',
    'tools/fixtures/steam_settings_module_profile_preparation/before-queue.source.json'
  ];
  // Read every selected input before writing. The new inc is required only when
  // the caller invokes preparation after staging its private product/fixture.
  // No fallback to another repo, cached header or behavior substitute is allowed.
  const selected=relativeInputs.map(relative=>{
    const source=path.join(repo,relative);return {relative,source,bytes:fs.readFileSync(source)};
  });
  // Follow quoted local includes only from compiled native headers. Full main
  // stays an extraction source, so its unrelated host/decky headers are excluded.
  const localIncludes=[],seen=new Set(selected.map(item=>path.normalize(item.source).toLowerCase()));
  const includeRoots=['native','deps/json','tools',''].map(relative=>path.join(repo,relative));
  for(let i=0;i<selected.length;++i){
    const item=selected[i];if(!item.relative.startsWith('native/')||!item.relative.endsWith('.h'))continue;
    for(const match of item.bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){
      const candidates=[path.resolve(path.dirname(item.source),match[1]),...includeRoots.map(root=>path.resolve(root,match[1]))];
      const source=candidates.find(file=>fs.existsSync(file)&&fs.statSync(file).isFile());
      assert(source,'Missing selected native local include: '+item.relative+' -> '+match[1]);
      const relative=path.relative(repo,source).split(path.sep).join('/');
      assert(relative&&!relative.startsWith('../')&&!path.isAbsolute(relative),'Local include must remain in selected repo');
      localIncludes.push({from:item.relative,include:match[1],relative});
      const key=path.normalize(source).toLowerCase();if(seen.has(key))continue;
      seen.add(key);selected.push({relative,source,bytes:fs.readFileSync(source)});
    }
  }
  selected.push({relative:'tools/steam_settings_batch_selftest.mjs',source:moduleFile,bytes:fs.readFileSync(moduleFile)});
  for(const {relative,source,bytes} of selected){
    const frozen=save(relative,bytes);sources.push({relative,source:pin(source,bytes),frozen});
  }
  const {extract}=await import(pathToFileURL(path.join(out,'tools/fixtures/legion_motion_consumer/extract_main.mjs')).href);
  const {steamSessionObserverPublication}=await import(pathToFileURL(path.join(out,'tools/fixtures/steam_session_observer_publication.mjs')).href);
  const observer=steamSessionObserverPublication(fs.readFileSync(path.join(out,'native/steam_session_observer.h')));
  const derivedObserver=save('native/steam_session_observer.fixture.h',observer.bytes);
  const main=fs.readFileSync(path.join(out,'native/main.cpp'));
  const kick=extract(main.toString('utf8'),'steamOverlayFixKick');
  assert.equal(main.subarray(kick.startUtf8Byte,kick.endUtf8ByteExclusive).toString('utf8'),kick.code,
    'Overlay kick must be the exact complete selected main function');
  const fixture=fs.readFileSync(path.join(out,'tools/steam_settings_batch_selftest.cpp'),'utf8');
  const placeholder='//__PRODUCTION_OVERLAY_KICK__';
  assert.equal(fixture.split(placeholder).length,2,'Exactly one original overlay kick placeholder required');
  const derivedProgram=save('fixture.cpp',fixture.replace(placeholder,()=>kick.code));
  const cpp=derivedProgram.file,program=path.join(out,'fixture.exe'),legacyProgram=path.join(out,'fixture-legacy.exe');
  const recipe=(shared,executable,object)=>({program:'cl.exe',cwd:out,args:[
    '/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2',
    ...(shared?['/DYMCC_STEAM_OVERLAY_STARTUP_SHARED_ONLY=1']:[]),
    '/I'+path.join(out,'native'),'/I'+path.join(out,'deps/json'),'/I'+path.join(out,'tools'),'/I'+out,
    cpp,'/Fo'+path.join(out,object),'/Fe'+executable,'/link','advapi32.lib','user32.lib'],
    requiredEnvironment:'Caller-pinned existing MSVC x64/SDK/CRT; caller owns actual compiler/include/library graph',
    toolIdentity:'NOT_OBSERVED_BY_PREPARE',originalOptimization:'/O2'});
  const {code,...kickSpan}=kick;
  const result={schemaVersion:1,status:'PREPARED_SOURCE_ONLY_NOT_COMPILED_OR_RUN',repoRoot:repo,outputDir:out,
    originalEntry:'tools/steam_settings_batch_selftest.mjs / .cpp',
    actualConsumer:'native/steam_overlay_startup.h + full native/steam_settings_runtime.h; native/main.cpp::steamOverlayFixKick',
    actualObserverPublication:true,
    scope:'Actual startup overlay and existing settings queue consumer with real Observer publication; controlled OS/config/transport leaves, no real Steam or physical acceptance',
    originalItemLimitMs:120000,originalLimitsMs:{compile:120000,native:120000,outerOwned:120000},
    budgetAllocatedByPrepare:false,newCanonicalId:null,minimumChanged:false,
    automaticDefaultSelection:false,defaultSuiteExecuted:false,sourceReady:false,
    cpp,program,args:['--startup-overlay-shared'],expectedExitCode:0,
    compile:recipe(true,program,'fixture.obj'),
    legacyCompile:recipe(false,legacyProgram,'fixture-legacy.obj'),
    legacyProgram,legacyArgs:['--startup-overlay-only'],legacyExpectedExitCode:0,
    caseSelection:{flag:'--case',usage:['--startup-overlay-shared','--case','<known>'],
      validation:'Known case names and rejection of unknown/empty selections belong to the selected actual CPP/inc; no independent case list or default full suite'},
    localIncludes,sources,sourceInputs:[...sources.map(item=>item.frozen),derivedObserver,derivedProgram].map(item=>
      ({path:item.file,bytes:item.bytes,sha256:item.sha256})),
    derivedProgram,observerDerivation:{original:observer.original,derived:observer.derived,generator:observer.generator},
    mainOverlayKick:{source:sources.find(item=>item.relative==='native/main.cpp').source,
      frozen:sources.find(item=>item.relative==='native/main.cpp').frozen,...kickSpan,
      role:'Only this complete function is extracted into the original placeholder; full main is not compiled'},
    callerResponsibilities:['Stage private selected CPP/inc/product before invocation','Pin compiler/SDK/CRT and verify actual discovered dependency graph',
      'Own total wall budget, compilation, exact selection and original raw process/resource receipts']};
  save('SOURCE-INPUTS.json',JSON.stringify(result,null,2)+'\n');return result;
}

// Source-only preparation for the existing settings consumer. The caller owns
// its compiler, execution budget and exact raw-process receipts.
export async function prepareSteamMonitorLiveRead({repoRoot, outputDir, ownedApply=false, ordinaryWrite=false}) {
  assert.equal(typeof ownedApply,'boolean','Explicit monitor scope must be boolean');assert.equal(typeof ordinaryWrite,'boolean');assert(!(ordinaryWrite&&ownedApply));
  const repo=fs.realpathSync(path.resolve(repoRoot)),out=path.resolve(outputDir);
  assert(!fs.existsSync(out),'Fresh monitor shared-read output required');
  assert(out!==repo&&out!==moduleDir,'Private preparation directory required');
  const sources=[];
  const save=(relative,bytes)=>{
    const file=path.join(out,relative);fs.mkdirSync(path.dirname(file),{recursive:true});
    fs.writeFileSync(file,bytes,{flag:'wx'});return pin(file,Buffer.isBuffer(bytes)?bytes:Buffer.from(bytes));
  };
  const freeze=relative=>{
    const source=path.join(repo,relative),bytes=fs.readFileSync(source),frozen=save(relative,bytes);
    sources.push({relative,source:pin(source,bytes),frozen});return frozen;
  };
  for(const relative of ['tools/steam_settings_batch_selftest.cpp','tools/steam_settings_batch_selftest.mjs',
    'tools/fixtures/steam_monitor_live_read_cases.inc','native/steam_settings_runtime.h',
    'native/steam_session_observer.h','native/steam_window_event_budget.h','native/worker_thread_name.h','native/steam_listener_executor.h',
    'native/process_inventory.h','native/steam_monitor_settings.h','native/steamdeck_mouse_vdf.h','deps/json/json.hpp',
    'tools/fixtures/steam_session_observer_publication.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs',
    'tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/steam_settings_module_profile_preparation/prepare.mjs',
    'tools/fixtures/steam_settings_module_profile_preparation/fixture.inc','tools/fixtures/steam_settings_module_profile_preparation/before-queue.inc',
    'tools/fixtures/steam_settings_module_profile_preparation/before-queue.source.json'])freeze(relative);
  if(ownedApply)freeze('tools/fixtures/steam_monitor_owned_apply_cases.inc');
  if(ordinaryWrite)for(const relative of ['tools/fixtures/steam_monitor_write_shared_cases.inc','native/steam_overlay_startup.h','native/sleep_steam_preferences.h','native/main.cpp'])freeze(relative);
  const {extract}=await import(pathToFileURL(path.join(out,'tools/fixtures/legion_motion_consumer/extract_main.mjs')));
  const {steamSessionObserverPublication}=await import(pathToFileURL(path.join(out,'tools/fixtures/steam_session_observer_publication.mjs')));
  const observer=steamSessionObserverPublication(fs.readFileSync(path.join(out,'native/steam_session_observer.h')));
  const derivedObserver=save('native/steam_session_observer.fixture.h',observer.bytes);
  const text=fs.readFileSync(path.join(out,'native/steam_settings_runtime.h'),'utf8');
  const symbols=ownedApply?['steamMonitorUnknown','steamMonitorAccountValid','steamMonitorValuesValid','steamMonitorLiveEnvelopeValid','steamMonitorSetReceiptValid','steamMonitorApplyOwned']:
    ['steamMonitorUnknown','steamMonitorAccountValid','steamMonitorValuesValid','steamMonitorLiveEnvelopeValid','steamMonitorGetLocked'];
  const parts=symbols.map(symbol=>extract(text,symbol));
  const body=parts.map(item=>item.code).join('\n\n')+'\n';
  const derivedRead=save('candidate-read.inc',body);
  let cpp=path.join(out,'tools/steam_settings_batch_selftest.cpp');const program=path.join(out,'fixture.exe');let derivedProgram=null;
  if(ordinaryWrite){
    const main=fs.readFileSync(path.join(out,'native/main.cpp'),'utf8'),kick=extract(main,'steamOverlayFixKick').code;
    const source=fs.readFileSync(cpp,'utf8');assert(source.split('//__PRODUCTION_OVERLAY_KICK__').length===2);
    derivedProgram=save('fixture.cpp',source.replace('//__PRODUCTION_OVERLAY_KICK__',kick));cpp=derivedProgram.file;
  }
  const result={schemaVersion:1,status:'PREPARED_SOURCE_ONLY_NOT_COMPILED_OR_RUN',repoRoot:repo,outputDir:out,
    actualConsumer:'native/steam_settings_runtime.h::'+(ordinaryWrite?'steamMonitorSet+steamClientSettingsAttemptLocked':ownedApply?'steamMonitorApplyOwned':'steamMonitorGetLocked'),actualObserverPublication:true,
    scope:ordinaryWrite?'Actual ordinary monitor SET, queued Live attempt and real Observer publication; existing memory file/queue/transport ports, no real Steam or disk proof':ownedApply?'Actual boot monitor ApplyOwned and real Observer publication/epochs; boot owner, config and Live transport leaves controlled, no physical Steam proof':'Actual monitor getter and Observer publication/epochs, memory state/config and live transport leaves; no full settings mutation or real Steam proof',
    newCanonicalId:null,minimumChanged:false,defaultSuiteExecuted:false,budgetAllocatedByPrepare:false,
    cpp,program,args:[path.join(out,'result.json'),ordinaryWrite?'--monitor-write-shared':ownedApply?'--monitor-owned-shared':'--monitor-live-shared'],expectedExitCode:0,
    compile:{program:'cl.exe',args:['/nologo','/std:c++20','/utf-8','/EHsc','/MT',...(ordinaryWrite?['/DYMCC_STEAM_MONITOR_WRITE_SHARED_ONLY']:['/DYMCC_STEAM_MONITOR_LIVE_READ_ONLY']),...(ownedApply?['/DYMCC_STEAM_MONITOR_OWNED_APPLY_ONLY']:[]),
      '/I'+path.join(out,'native'),'/I'+path.join(out,'deps/json'),'/I'+path.join(out,'tools'),'/I'+out,
      cpp,'/Fo'+path.join(out,'fixture.obj'),'/Fe'+program,'/link','advapi32.lib','user32.lib'],
      requiredEnvironment:'Caller-pinned existing MSVC x64/SDK/CRT; actual include and library graph required'},
    sources,sourceInputs:[...sources.map(x=>x.frozen),derivedObserver,derivedRead,...(derivedProgram?[derivedProgram]:[])].map(x=>({path:x.file,bytes:x.bytes,sha256:x.sha256})),
    ordinaryWrite,derivedProgram,observerDerivation:{original:observer.original,derived:observer.derived,generator:observer.generator},
    derivedRead:{...derivedRead,symbols,spans:parts.map(({code,...span})=>span)},sourceReady:false};
  if(ordinaryWrite)result.outputOwnerSelection={program,args:[path.join(out,'early-output-result.json'),'--monitor-write-shared','--case','early-owner-boundaries'],controlArgs:[path.join(out,'early-output-old-result.json'),'--monitor-write-shared','--case','early-owner-control']};
  if(ownedApply)result.outputOwnerSelection={program,args:[path.join(out,'early-output-result.json'),'--monitor-owned-shared','--early-owner-boundaries'],controlArgs:[path.join(out,'early-output-old-result.json'),'--monitor-owned-shared','--early-owner-control']};
  if(!ordinaryWrite&&!ownedApply)result.earlyOwnerControl={program,args:[path.join(out,'early-owner-control-result.json'),'--monitor-live-shared','--early-owner-control'],expectedExitCode:0,scope:'Only original GET before-live-discovery owner revocation; no default suite'};
  save('SOURCE-INPUTS.json',JSON.stringify(result,null,2)+'\n');return result;
}

// Source preparation only: no compiler, fixture, child process or OS discovery.
// The caller owns the original consumer's 120000ms bound and actual tool pins.
export async function prepareSteamSharedPresence({repoRoot, outputDir}) {
  assert.equal(typeof repoRoot, 'string', 'repoRoot is required');
  assert.equal(typeof outputDir, 'string', 'outputDir is required');
  assert(repoRoot.length && outputDir.length, 'Explicit nonempty paths required');
  const repo = fs.realpathSync(path.resolve(repoRoot));
  const out = path.resolve(outputDir);
  assert(!fs.existsSync(out) || fs.readdirSync(out).length === 0, 'Output must be new or empty');
  assert(out !== repo && out !== moduleDir, 'Output must be a private preparation directory');
  const mainFile = path.join(repo, 'native/main.cpp');
  // One read binds both complete extracted functions to the same full main.
  const mainBytes = fs.readFileSync(mainFile);
  const mainText = mainBytes.toString('utf8');
  const inputs = [];
  const save = (relative, bytes) => {
    const target = path.join(out, relative);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.writeFileSync(target, bytes, {flag: 'wx'});
    return pin(target, Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes));
  };
  const freeze = (relative, source, role) => {
    const bytes = fs.readFileSync(source);
    const frozen = save(relative, bytes);
    const entry = {relative, role, source: pin(source, bytes), frozen};
    inputs.push(entry); return entry;
  };
  const support = path.resolve(moduleDir, '../metadata/source-dependencies');
  const resolveInput = relative => {
    const preferred = path.join(repo, relative);
    if (fs.existsSync(preferred)) return preferred;
    const adjacent = path.resolve(moduleDir, '..', relative);
    if (fs.existsSync(adjacent)) return adjacent;
    const snapshot = path.join(support, relative);
    assert(fs.existsSync(snapshot), `Missing declared source input: ${relative}`);
    return snapshot;
  };
  const fixture = freeze('tools/steam_settings_batch_selftest.cpp',
    path.join(moduleDir, 'steam_settings_batch_selftest.cpp'), 'compiled candidate fixture');
  freeze('tools/steam_settings_batch_selftest.mjs', moduleFile, 'preparation source; legacy body retained');
  for (const relative of ['tools/fixtures/process_inventory_ports.h', 'native/process_inventory.h',
    'native/monitor_identity.h', 'native/fxsound_recovery_policy.h', 'deps/json/json.hpp']) {
    freeze(relative, resolveInput(relative), 'actual compiled header/ports input');
  }
  // Freeze both parser modules before loading the existing extractor. Its
  // imported controller-motion helper is pinned, but never called here.
  const parserRelative = 'tools/fixtures/legion_motion_consumer/extract_main.mjs';
  const parser = freeze(parserRelative, resolveInput(parserRelative), 'existing complete-function parser');
  freeze('tools/lib/hc_controller_motion_fixture.mjs',
    resolveInput('tools/lib/hc_controller_motion_fixture.mjs'), 'parser module dependency; no fixture invoked');
  const {extract} = await import(pathToFileURL(parser.frozen.file).href);
  const base = extract(mainText, 'sgBaseName');
  const presence = extract(mainText, 'sofSteamRunning');
  assert(base.code.includes('return nativeIdentityNormalizeName(path);'), 'Real forwarding sgBaseName required');
  const needle = 'currentTime, 0';
  assert.equal(presence.code.split(needle).length - 1, 1, 'Exactly one fresh-zero-age call required');
  assert(presence.code.includes('ymcc::processInventory::read(') &&
    presence.code.includes('std::any_of(') && presence.code.includes('sgBaseName(row.name)') &&
    presence.code.includes('Status::Complete') && /catch\s*\(\s*\.\.\.\s*\)/.test(presence.code),
    'Expected real shared-owner after function, not a copied behavior substitute');
  const cached = presence.code.replace(needle, 'currentTime, 250');
  const generated = '// Derived verbatim from the single pinned native/main.cpp.\n' +
    'namespace actual_presence {\n' + base.code + '\n\n' + presence.code + '\n}\n' +
    'namespace cached_control {\n' + base.code + '\n\n' + cached + '\n}\n';
  const derived = save('sof-steam-presence.inc', generated);
  const frozenMain = save('native/main.cpp.snapshot', mainBytes);
  const spans = [base, presence].map(({code, ...span}) => span);
  const compile = {program: 'cl.exe', cwd: out, args: ['/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2',
    '/DYMCC_STEAM_SHARED_PRESENCE_ONLY=1', '/I.', 'tools/steam_settings_batch_selftest.cpp',
    '/Fofixture.obj', '/Fefixture.exe', '/link', 'kernel32.lib'],
    requiredLibraries: ['kernel32.lib'],
    requiredEnvironment: 'Caller-owned, pinned existing MSVC x64 + Windows SDK + CRT environment',
    toolIdentity: 'NOT_OBSERVED_BY_PREPARE', originalOptimization: '/O2'};
  const program = path.join(out, 'fixture.exe');
  const result = {schemaVersion: 1, status: 'PREPARED_SOURCE_ONLY_NOT_COMPILED_OR_RUN',
    actualConsumer: 'native/main.cpp::sofSteamRunning + sgBaseName',
    actualOwner: 'native/process_inventory.h global owner()/read(); unchanged',
    realWindowsOrProcesses: false, repoRoot: repo, outputDir: out,
    originalEntry: 'tools/steam_settings_batch_selftest.mjs / .cpp',
    automaticDefaultSelection: false, newRegistrationId: null, minimumChanged: false,
    originalItemLimitMs: 120000, budgetAllocatedByPrepare: false,
    originalLimitsMs: {compile: 120000, native: 120000, outerOwned: 120000},
    cpp: fixture.frozen.file, includeRoots: [out],
    sourceInputs: [...inputs.map(item => item.frozen), derived].map(item =>
      ({path: item.file, bytes: item.bytes, sha256: item.sha256})),
    selection: '--shared-presence-only', negativeSelection: '--shared-presence-cached-control',
    negativeExpectedAssertion: 'fresh-absence-must-not-hide-steam-start',
    caseIds: [
      ...Array.from({length: 6}, (_, index) => `complete-steam-name-variant-${index + 1}`),
      'complete-other-process-is-absent',
      'webhelper-and-lookalikes-do-not-count-as-steam',
      'native-success-zero-returned-bytes-is-rejected',
      'native-malformed-is-conservative',
      'native-failed-is-conservative',
      'native-allocation-failure-is-conservative',
      'unsupported-central-toolhelp-complete-absent',
      'unsupported-central-toolhelp-complete-steam',
      'unsupported-central-toolhelp-complete-empty-is-absent',
      'unsupported-partial-other-prefix-cannot-prove-absence',
      'unsupported-partial-steam-prefix-remains-failed',
      'unsupported-central-first-failure-is-conservative',
      'unsupported-central-snapshot-failure-is-conservative',
      'warm-absent-plus-one-ms-steam-start-is-fresh',
      'warm-present-plus-one-ms-steam-exit-is-fresh',
      'warm-absent-plus-one-ms-zero-byte-success-is-rejected',
      'warm-present-plus-one-ms-zero-byte-success-is-rejected',
      'warm-absent-plus-one-ms-malformed',
      'warm-absent-plus-one-ms-failed',
      'warm-absent-plus-one-ms-allocation-failure',
      'warm-present-plus-one-ms-malformed',
      'warm-present-plus-one-ms-failed',
      'warm-present-plus-one-ms-allocation-failure',
    ],
    negativeCaseIds: ['warm-absent-plus-one-ms-steam-start-is-fresh'],
    compile, program, args: ['--shared-presence-only'], expectedExitCode: 0,
    cachedControl: {program, args: ['--shared-presence-cached-control'], expectedExitCode: 1,
      expectedCase: 'warm-absent-plus-one-ms-steam-start-is-fresh',
      expectedAssertion: 'fresh-absence-must-not-hide-steam-start',
      onlyNecessaryWarmBoundary: true},
    rejectedSelections: {expectedExitCode: 2, unknown: true, empty: true, combinations: true},
    sources: inputs, main: {source: pin(mainFile, mainBytes), frozen: frozenMain, spans,
      role: 'extraction source only; full main translation unit and unrelated host includes are not compiled'},
    generated: {...derived, actualPresenceFunctionSha256: presence.sha256,
      baseNameFunctionSha256: base.sha256, cachedFunctionSha256: digest(cached),
      cachedEdit: {find: needle, replacement: 'currentTime, 250', count: 1}},
    compileInputs: [fixture.frozen, ...inputs.filter(item => item.role === 'actual compiled header/ports input').map(item => item.frozen), derived],
    scope: 'Virtual OS facts only. Real shared Store and extracted real consumer; no process, PID, configuration, Steam, device or network operations. SDK/CRT/compiler/linker closure must be pinned by the caller before actual owned compilation.'};
  fs.writeFileSync(path.join(out, 'SOURCE-INPUTS.json'), JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
  return result;
}

// Source preparation only; this function never invokes a compiler or process.
export async function prepareSteamObservedSettings({repoRoot, outputDir}) {
  assert.equal(typeof repoRoot, 'string'); assert.equal(typeof outputDir, 'string');
  assert(repoRoot.length && outputDir.length, 'Explicit nonempty paths required');
  const repo = fs.realpathSync(path.resolve(repoRoot)), out = path.resolve(outputDir);
  assert(!fs.existsSync(out) || fs.readdirSync(out).length === 0, 'Output must be new or empty');
  assert(out !== repo && out !== moduleDir, 'Private preparation directory required');
  const dependencies = path.resolve(moduleDir, '../metadata/source-dependencies');
  const resolveInput = relative => {
    for (const base of [repo, path.resolve(moduleDir, '..'), dependencies]) {
      const file = path.join(base, relative); if (fs.existsSync(file)) return file;
    }
    throw Error(`Missing declared observed-settings input: ${relative}`);
  };
  const inputs = [];
  const save = (relative, value) => {
    const bytes = Buffer.isBuffer(value) ? value : Buffer.from(value);
    const file = path.join(out, relative); fs.mkdirSync(path.dirname(file), {recursive:true});
    fs.writeFileSync(file, bytes, {flag:'wx'}); return pin(file, bytes);
  };
  const freeze = (relative, source, role) => {
    const bytes = fs.readFileSync(source), frozen = save(relative, bytes);
    const entry = {relative, role, source:pin(source,bytes), frozen}; inputs.push(entry); return entry;
  };
  const mainFile = path.join(repo, 'native/main.cpp');
  const mainBytes = fs.readFileSync(mainFile), mainText = mainBytes.toString('utf8');
  // r2 is under ObservedSettingsCandidate/ReadPidContext/candidate. Retain the
  // original pre-observed selector, rather than an intermediate r1 wrapper.
  const beforeFile = path.resolve(repo, '../../original/native/main.cpp');
  const beforeBytes = fs.readFileSync(beforeFile), beforeText = beforeBytes.toString('utf8');
  const runtimeFile = path.join(repo, 'native/steam_settings_runtime.h');
  const runtimeBytes = fs.readFileSync(runtimeFile), runtimeText = runtimeBytes.toString('utf8');
  const cpp = freeze('tools/steam_settings_batch_selftest.cpp', path.join(moduleDir, 'steam_settings_batch_selftest.cpp'), 'compiled fixture');
  freeze('tools/steam_settings_batch_selftest.mjs', moduleFile, 'preparer and preserved existing entry');
  for (const relative of ['native/process_inventory.h','native/steam_window_event_budget.h','native/worker_thread_name.h','native/steam_listener_executor.h',
    'native/steam_monitor_settings.h','native/steamdeck_mouse_vdf.h','deps/json/json.hpp']) {
    freeze(relative, resolveInput(relative), 'actual compiled dependency');
  }
  // These two inputs MUST come from the selected product root. The public API
  // generator checks its exact product/derived pins and preserves the real body.
  const observer = freeze('native/steam_session_observer.h', path.join(repo, 'native/steam_session_observer.h'), 'publication product preimage');
  const publication = freeze('tools/fixtures/steam_session_observer_publication.mjs',
    path.join(repo, 'tools/fixtures/steam_session_observer_publication.mjs'), 'existing publication generator');
  const parser = freeze('tools/fixtures/legion_motion_consumer/extract_main.mjs',
    resolveInput('tools/fixtures/legion_motion_consumer/extract_main.mjs'), 'existing complete-function parser');
  freeze('tools/lib/hc_controller_motion_fixture.mjs', resolveInput('tools/lib/hc_controller_motion_fixture.mjs'), 'parser import dependency; no fixture invoked');
  const {extract} = await import(pathToFileURL(parser.frozen.file).href);
  const {steamSessionObserverPublication} = await import(pathToFileURL(publication.frozen.file).href);
  const adapter = steamSessionObserverPublication(fs.readFileSync(observer.frozen.file));
  const derivedObserver = save('native/steam_session_observer.fixture.h', adapter.bytes);
  const at = extract(mainText, 'sofFindLocalConfigAt'), fresh = extract(mainText, 'sofFindLocalConfig');
  const observed = extract(mainText, 'steamSettingsObservedLocalConfig');
  const before = extract(beforeText, 'sofFindLocalConfig');
  // The established parser requires one definition. Mask only the OTHER
  // overload's three ASCII name bytes. Coordinates stay identical; each selected
  // function is verified against the unmodified original main byte span.
  const conversions = ['const wchar_t*', 'const std::wstring&'].map(signature => {
    const masked = mainText.replace(/^static std::string W2U\([^\r\n]*/gm,
      line => line.includes(signature) ? line : line.replace('W2U(', 'W2X('));
    const entry = extract(masked, 'W2U');
    assert.equal(mainBytes.subarray(entry.startUtf8Byte,entry.endUtf8ByteExclusive).toString('utf8'),entry.code);
    return {...entry, overload:signature};
  });
  assert(observed.code.includes('g_steamSettingsObserver.identitySnapshot()') && observed.code.includes('identity->registryRoot'), 'Real memory-only observed wrapper required');
  const open = observed.code.indexOf('{'); assert(open >= 0);
  const pidParameter = /DWORD\s*\*\s*(\w+)\s*=\s*nullptr/.exec(observed.code.slice(0,open));
  assert(pidParameter, 'Real optional captured-PID parameter required');
  const pid = pidParameter[1];
  const mutation = observed.code.slice(0,open+1) + '\n' + [
    `    if (${pid}) {`,
    `        *${pid}=0;`,
    `        sofRegDWORD(HKEY_CURRENT_USER,L"Software\\\\Valve\\\\Steam\\\\ActiveProcess",L"pid",${pid});`,
    '    }',
    '    return sofFindLocalConfig(pathOut, accountOut);',
    '}',
  ].join('\n');
  const functions = conversions.map(entry=>entry.code).join('\n\n') + '\n\n' + at.code + '\n\n' + fresh.code +
    '\n\nnamespace actual_observed {\n' + observed.code + '\n}\n' +
    'namespace fresh_read_control {\n// MUTATION: only the observed wrapper body calls the real fresh selector.\n' + mutation + '\n}\n' +
    'namespace before_selector {\n' + before.code + '\n}\n';
  const derivedFunctions = save('observed-settings-functions.inc', functions);
  const state = extract(runtimeText, 'steamMonitorStateLocked');
  const declarationAt = runtimeText.indexOf('static void steamClientSettingsAttemptLocked(');
  assert(declarationAt > 0 && state.endUtf8ByteExclusive <= Buffer.byteLength(runtimeText.slice(0,declarationAt)), 'Exact state prefix before action consumers required');
  const statePrefix = Buffer.concat([runtimeBytes.subarray(0,state.endUtf8ByteExclusive), Buffer.from('\n')]);
  const derivedState = save('steam-monitor-state.inc', statePrefix);
  const frozenMain = save('native/main.cpp.snapshot', mainBytes);
  const frozenBefore = save('before/native/main.cpp.snapshot', beforeBytes);
  const frozenRuntime = save('native/steam_settings_runtime.h.snapshot', runtimeBytes);
  const spans = [...conversions,at,fresh,observed].map(({code,...entry})=>entry);
  const validators = ['steamMonitorUnknown','steamMonitorAccountValid','steamMonitorValuesValid',
    'steamMonitorSavedValid','steamMonitorLiveEnvelopeValid','steamMonitorSetReceiptValid',
    'steamMonitorSavedSection','steamMonitorStateLocked'].map(symbol=>{
      const {code,...entry}=extract(runtimeText,symbol); return entry;
    });
  const program = path.join(out,'fixture.exe');
  const caseIds = [
    'observed/root-normalized-and-raw-spelling-one-read',
    'observed/root-failure-clears-raw',
    'observed/saved-path-and-live-values-exact',
    'observed/offline-retains-candidate-without-live-confirmation',
    'observed/zero-account-latest-fallback-not-login-proof',
    'observed/account-read-error-latest-fallback',
    'observed/missing-active-file-latest-fallback',
    'observed/no-root-receipt-rejects-without-private-discovery',
    'observed/no-receipt-rejects-without-private-discovery',
    'observed/unknown-userdata-directory',
    'observed/empty-userdata-directory',
    'observed/producer-replacement-consumes-new-facts',
    'observed/old-receipt-remains-immutable',
    'observed/case-only-root-publication-keeps-epochs',
    'observed/old-selector-and-selectorAt-equivalent',
    'observed/repeated-display-zero-discovery-1001',
    'observed/vdf-read-error-preserves-original',
    'observed/invalid-saved-section-rejected-before-filesystem',
  ];
  const derived = [derivedObserver,derivedFunctions,derivedState];
  const result = {schemaVersion:1,status:'PREPARED_SOURCE_ONLY_NOT_COMPILED_OR_RUN',
    cpp:cpp.frozen.file,includeRoots:[out,path.join(out,'native'),path.join(out,'deps/json')],
    sourceInputs:[...inputs.map(entry=>entry.frozen),...derived].map(entry=>({path:entry.file,bytes:entry.bytes,sha256:entry.sha256})),
    selection:'--observed-settings-only',negativeSelection:'--observed-settings-fresh-read-control',
    caseIds,negativeCaseIds:['observed/fresh-read-control-repeated-display'],
    negativeExpectedAssertion:'memory-settings-consumer-must-not-reread-registry',
    program,args:['--observed-settings-only'],expectedExitCode:0,
    negativeControl:{program,args:['--observed-settings-fresh-read-control'],expectedExitCode:1,
      expectedCase:'observed/fresh-read-control-repeated-display',expectedAssertion:'memory-settings-consumer-must-not-reread-registry',
      warmCalls:1,repeatedCalls:1,normalGroupExecuted:false,mutation:'only observed helper body reads fresh PID then calls fresh sofFindLocalConfig; captured PID is initialized before state validation'},
    compile:{program:'cl.exe',cwd:out,args:['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2',
      '/DYMCC_STEAM_SETTINGS_OBSERVATION_ONLY=1','/I.','/Inative','/Ideps/json',
      'tools/steam_settings_batch_selftest.cpp','/Fofixture.obj','/Fefixture.exe','/link','kernel32.lib','advapi32.lib','user32.lib'],
      requiredLibraries:['kernel32.lib','advapi32.lib','user32.lib'],toolIdentity:'NOT_OBSERVED_BY_PREPARE'},
    originalLimitsMs:{compile:120000,native:120000,outerOwned:120000},budgetAllocatedByPrepare:false,
    priorAccountReferenceOnly:{priorChargedMs:32667.3287,productPreparationUserReportedMs:94,newBudget:false,identityOrPeerBudgetBorrowed:false},
    originalEntry:'tools/steam_settings_batch_selftest.cpp/.mjs',automaticDefaultSelection:false,newRegistrationId:null,minimumChanged:false,
    sources:inputs,main:{source:pin(mainFile,mainBytes),frozen:frozenMain,spans},
    beforeSelector:{source:pin(beforeFile,beforeBytes),frozen:frozenBefore,span:(({code,...entry})=>entry)(before)},
    runtime:{source:pin(runtimeFile,runtimeBytes),frozen:frozenRuntime,spans:validators,
      actualCompiledPrefixBytes:state.endUtf8ByteExclusive,actualCompiledPrefixSha256:digest(runtimeBytes.subarray(0,state.endUtf8ByteExclusive))},
    observerPublication:{original:adapter.original,derived:adapter.derived,generator:publication,transformation:adapter.transformation},
    generated:{observer:derivedObserver,functions:derivedFunctions,state:derivedState,
      mutation:{sourceFunctionSha256:observed.sha256,derivedFunctionSha256:digest(mutation),changedFunctionOnly:'steamSettingsObservedLocalConfig',capturedPidParameter:pid,freshPidAssignedBeforeSelector:true}},
    actualConsumer:'verbatim steamMonitorStateLocked + validation prefix and real selector/wrappers',
    actualOwner:'real public Observer/IdentitySnapshot; existing fixture publication API invokes real publishIdentity',
    realRegistryOrFilesystem:false,observerStarted:false,selfProcessTerminalFactsOnly:true,
    scope:'Memory OS/FS ports only. Display root/account/registeredPid share one public immutable receipt; root/ActiveUser/PID discovery increments must all be zero across repeated state reads. FS reads are counted separately. Cached PID and other display facts never authorize current account/Running/deployment. Full main files are extraction sources, not recursive device include roots. Fresh Get/ApplyOwned fences remain outside this consumer scope and unchanged by this fixture adaptation.'};
  fs.writeFileSync(path.join(out,'SOURCE-INPUTS.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
  return result;
}

export {prepareModuleProfilePreparation} from './fixtures/steam_settings_module_profile_preparation/prepare.mjs';
import {prepareModuleProfilePreparation} from './fixtures/steam_settings_module_profile_preparation/prepare.mjs';

const isMain = !!process.argv[1] && path.resolve(process.argv[1]) === path.resolve(moduleFile);
let selection = null;
if (isMain && process.argv.length > 2) {
  const args = process.argv.slice(2), values = new Map();
  let invalid = false;
  for (let i = 0; i < args.length; ++i) {
    const flag = args[i];
    if (values.has(flag)) { invalid = true; break; }
    if (flag === '--repo-root' || flag === '--output-dir') {
      const value = args[++i];
      if (!value || value.startsWith('--')) { invalid = true; break; }
      values.set(flag, value);
    } else if (['--monitor-write-shared-only', '--monitor-owned-apply-only', '--monitor-live-read-only', '--shared-presence-only', '--shared-presence-cached-control', '--observed-settings-only', '--observed-settings-fresh-read-control', '--module-profile-preparation', '--module-profile-preparation-before-control', '--prepare-only'].includes(flag)) {
      values.set(flag, true);
    } else { invalid = true; break; }
  }
  const selections = ['--monitor-write-shared-only', '--monitor-owned-apply-only', '--monitor-live-read-only', '--shared-presence-only', '--shared-presence-cached-control', '--observed-settings-only', '--observed-settings-fresh-read-control', '--module-profile-preparation', '--module-profile-preparation-before-control'].filter(flag => values.has(flag));
  if (invalid || selections.length !== 1 || !values.has('--prepare-only') ||
    !values.has('--repo-root') || !values.has('--output-dir') || values.size !== 4 ||
    process.env.YMCC_STEAM_SETTINGS_STARTUP_ONLY === '1' || process.env.YMCC_STEAM_SETTINGS_SECTION_ONLY === '1') {
    selection = 'rejected'; process.exitCode = 2;
    console.error('Exactly one presence selection requires --prepare-only --repo-root <path> --output-dir <path>; no compile/run CLI is provided.');
  } else {
    selection = selections[0];
    try {
      const preparer = (selection === '--monitor-live-read-only'||selection === '--monitor-owned-apply-only'||selection === '--monitor-write-shared-only') ? prepareSteamMonitorLiveRead : selection.startsWith('--module-profile-preparation') ? prepareModuleProfilePreparation : selection.startsWith('--observed-settings-') ? prepareSteamObservedSettings : prepareSteamSharedPresence;
      const prepared = await preparer({repoRoot: values.get('--repo-root'), outputDir: values.get('--output-dir'), ...(selection === '--monitor-write-shared-only'?{ordinaryWrite:true}:selection === '--monitor-owned-apply-only'?{ownedApply:true}:{})});
      const invocation = selection === '--shared-presence-cached-control' ? prepared.cachedControl :
        selection === '--observed-settings-fresh-read-control' ? prepared.negativeControl :
        selection === '--module-profile-preparation-before-control' ? prepared.negativeControl : prepared;
      console.log(JSON.stringify({...prepared, selection, requestedSelection: selection,
        program: invocation.program, args: invocation.args, expectedExitCode: invocation.expectedExitCode}));
    } catch (error) { console.error(error?.stack || String(error)); process.exitCode = 1; }
  }
}

// BEGIN VERBATIM LEGACY EXECUTION BODY (all original flags, recipe and limits).
if (isMain && !selection) {
const root=process.cwd(),out=process.env.YMCC_STEAM_SETTINGS_OUT||path.resolve(root,'../../Build/Validation/monitor-steam-settings-20261009/batch');fs.mkdirSync(out,{recursive:true});
const main=fs.readFileSync('native/main.cpp','utf8');
const header=process.env.YMCC_STEAM_SETTINGS_HEADER||path.join(root,'native/steam_settings_runtime.h');
fs.copyFileSync(header,path.join(out,'steam_settings_runtime.h'));
fs.copyFileSync(path.join(root,'native/steam_overlay_startup.h'),path.join(out,'steam_overlay_startup.h')); // Always bind the selected bytes; never reuse a stale local header.
const kick=main.slice(main.indexOf('static void steamOverlayFixKick(const char* reason) {'),main.indexOf('// SteamDeck desktop right-stick sensitivity.'));
assert(!kick.includes('steamSettingsQueue(reason)'));assert(kick.includes('g_sofEnabled.store(false'));assert(kick.includes('g_sofPending.store(false'));assert(!kick.includes('poolSubmit'));assert(!kick.includes('SetTimer'));
const input=fs.readFileSync('tools/steam_settings_batch_selftest.cpp','utf8').replace('//__PRODUCTION_OVERLAY_KICK__',kick);
fs.writeFileSync(path.join(out,'fixture.cpp'),input);
const setup=process.env.YMCC_STEAM_SETTINGS_ENV_CMD||'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
if(!fs.existsSync(setup)||/["%\r\n]/.test(setup))throw Error('Invalid compiler environment');
const bat='@echo off\r\ncall "'+setup+'" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"'+root+'\\native" /I"'+root+'\\deps\\json" "'+out+'\\fixture.cpp" /Fo"'+out+'\\fixture.obj" /Fe"'+out+'\\fixture.exe"\r\nexit /b %errorlevel%\r\n';
fs.writeFileSync(path.join(out,'compile.cmd'),bat.replaceAll('\\r\\n','\r\n').replaceAll('\\\\','\\'));
const build=spawnSync('cmd.exe',['/d','/c',path.join(out,'compile.cmd')],{encoding:'utf8',cwd:out,windowsHide:true,timeout:120000});fs.writeFileSync(path.join(out,'build.log'),(build.stdout||'')+(build.stderr||''));console.log(build.stdout);console.error(build.stderr);assert.equal(build.status,0);
const sources=['tools/steam_settings_batch_selftest.cpp','tools/steam_settings_batch_selftest.mjs','native/steam_overlay_startup.h','native/steam_monitor_settings.h','native/sleep_steam_preferences.h','native/steamdeck_mouse_vdf.h','deps/json/json.hpp'].map(file=>({file:path.join(root,file),sha256:crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')}));sources.push({file:path.resolve(header),sha256:crypto.createHash('sha256').update(fs.readFileSync(header)).digest('hex')});fs.writeFileSync(path.join(out,'SOURCE-INPUTS.json'),JSON.stringify({sources,compiledRuntimeHeader:{file:path.join(out,'steam_settings_runtime.h'),sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(out,'steam_settings_runtime.h'))).digest('hex')},mainOverlayKick:{source:path.join(root,'native/main.cpp'),sha256:crypto.createHash('sha256').update(kick).digest('hex')},scope:'production steamMonitor consumer; Steam/root store ports controlled; preimage assertions are store-port evidence, not root filesystem acceptance'},null,2));
const run=spawnSync(path.join(out,'fixture.exe'),process.env.YMCC_STEAM_SETTINGS_STARTUP_ONLY==='1'?['--startup-overlay-only']:process.env.YMCC_STEAM_SETTINGS_SECTION_ONLY==='1'?['--section-only']:[],{encoding:'utf8',cwd:out,windowsHide:true,timeout:120000});fs.writeFileSync(path.join(out,'results.log'),(run.stdout||'')+(run.stderr||''));console.log(run.stdout);console.error(run.stderr);assert.equal(run.status,0);
const retry=main.slice(main.indexOf('static void steamDeckMouseRetry('),main.indexOf('static json steamDeckMouseGet()'));
assert(retry.includes('steamSettingsQueue("mouse-retry")')&&!retry.includes('poolSubmit'));
const mouse=main.slice(main.indexOf('static json steamDeckMouseSet('),main.indexOf('#include "steam_settings_runtime.h"'));
assert(mouse.includes('steamSettingsQueue("mouse-user")')&&!mouse.includes('sdmApplyLocked();'));
const observer=main.slice(main.indexOf('static void steamSettingsObserverStart()'),main.indexOf('static bool sofApplyValueToConfigText(const std::string& in, int target, std::string* out) {'));
assert(observer.includes('steamSettingsQueue("steam-session",sessionChanged);'));assert(observer.includes('g_steamClientPending.load()'));assert(!observer.includes('SetTimer'));
console.log('PASS monitor/mouse adapters keep one write lane; abandoned Overlay kick cannot queue');

const adapters = ['src/bridge/steamDeckMouse.ts','src/bridge/steamOverlayFix.ts'].map(file=>fs.readFileSync(file,'utf8'));
assert(adapters.every(text=>text.includes("'steam.settings.get'")));
assert(adapters[0].includes("'steam.settings.set'"));
const bridge=fs.readFileSync('src/bridge/yeman.ts','utf8');
const sleepSetter=bridge.slice(bridge.indexOf('export async function sleepGuardSetConfig('),bridge.indexOf('export interface SleepFactEvent'));
assert(!sleepSetter.includes('overlayOffFix:cfg.steamOverlayOffFix'));assert(sleepSetter.includes("'steamOverlayOffFix' in cfg"));
console.log('PASS active monitor/mouse use unified entry; sleep overlay frontend is removed and blocked');

}
// END VERBATIM LEGACY EXECUTION BODY.
