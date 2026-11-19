import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
import {extract} from './legion_motion_consumer/extract_main.mjs';

// Source preparation only. No compiler, subprocess, native consumer, fixture
// producer, service, Steam, identity provider, or existing NoIO pin is invoked.
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const modes = ['global', 'xbox', 'steam', 'xbox-steam'];
export function steamStartupOwnerSelection() {
  const rows = [];
  for (const mode of modes) {
    for (const change of ['revision', 'input-intent', 'mode', 'result', 'observed', 'unsupported', 'corrupt'])
      rows.push({scenario:'stale-dispatch', change, mode});
    for (const change of ['none', 'ack', 'unrelated']) rows.push({scenario:'current', change, mode});
    for (const change of ['mode', 'corrupt']) rows.push({scenario:'lock-wait', change, mode});
    for (const change of ['local', 'cross-process']) rows.push({scenario:'atomic-write', change, mode});
    for (const change of ['mode', 'exit']) rows.push({scenario:'service-late', change, mode});
    for (const change of ['before', 'after-write']) rows.push({scenario:'exit', change, mode});
    for (const change of ['absent-origin', 'ordinary', 'runtime']) rows.push({scenario:'ordinary', change, mode});
    rows.push({scenario:'custom-refused', change:'none', mode});
  }
  for (const change of ['origin-invalid', 'origin-type', 'context-without-origin', 'ordinary-context',
    'boot-origin-without-context', 'context-null', 'context-extra-key', 'context-input-revision',
    'context-mode-schema', 'enabled-type', 'repair-type', 'unknown-argument', 'isolated'])
    rows.push({scenario:'invalid', change, mode:'global'});
  for(const mode of modes){
    for(const change of ['mode','unrelated','ack','input-intent','observed','result','unsupported','corrupt','preparation-selection','preparation-new-request','preparation-disabled','preparation-current','preparation-state','preparation-rollback'])
      rows.push({scenario:'preparation',change,mode});
    for(const change of ['mode','preparation-selection','preparation-new-request'])rows.push({scenario:'preparation-lock-wait',change,mode});
  }
  for(const mode of modes){
    const steam=mode==='steam'||mode==='xbox-steam';
    rows.push({scenario:'route',change:'opposite',mode,enabled:!steam});
    rows.push({scenario:'preparation',change:'claim-current',mode});
    rows.push({scenario:'preparation',change:'claim',mode});
    rows.push({scenario:'preparation',change:'claim-rebased',mode});
    rows.push({scenario:'preparation-lock-wait',change:'claim-rebased',mode});
    if(steam){
      rows.push({scenario:'route',change:'disabled-opposite',mode,enabled:true});
      rows.push({scenario:'route',change:'disabled-current',mode,enabled:false});
    }
  }
  rows.push({scenario:'route',change:'unconfigured-opposite',mode:'global',enabled:true});
  rows.push({scenario:'route',change:'unconfigured-current',mode:'global',enabled:false});
  return rows.map((row, index) => ({index:index + 1, enabled:row.mode === 'steam' || row.mode === 'xbox-steam', ...row}));
}
function bodyEnd(text, open) {
  let depth=0, state='code', rawEnd='';
  for(let i=open;i<text.length;i++) {
    const c=text[i], next=text[i+1];
    if(state==='raw'){if(text.startsWith(rawEnd,i)){i+=rawEnd.length-1;state='code';}continue;}
    if(state==='line'){if(c==='\n')state='code';continue;}
    if(state==='block'){if(c==='*'&&next==='/'){i++;state='code';}continue;}
    if(state==='string'||state==='char'){if(c==='\\'){i++;continue;}if(c===(state==='string'?'"':"'"))state='code';continue;}
    if(c==='/'&&next==='/'){i++;state='line';continue;}
    if(c==='/'&&next==='*'){i++;state='block';continue;}
    if(c==='R'&&next==='"'){const end=text.indexOf('(',i+2);assert(end>i&&end-i<19);rawEnd=')'+text.slice(i+2,end)+'"';state='raw';i=end;continue;}
    if(c==='"'){state='string';continue;}if(c==="'"){state='char';continue;}
    if(c==='{')depth++;if(c==='}'&&--depth===0)return i+1;
  }
  throw Error('Unclosed original source fragment');
}
function fragment(text, regex, symbol, semicolon=false) {
  const matches=[...text.matchAll(regex)];assert.equal(matches.length,1,'Unique original '+symbol);
  const start=matches[0].index, open=text.indexOf('{',start);assert(open>=start);
  let end=bodyEnd(text,open);if(semicolon){assert.equal(text[end],';');end++;}
  const code=text.slice(start,end);
  return {symbol,code,sha256:sha(code),startUtf8Byte:Buffer.byteLength(text.slice(0,start)),
    endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,end)),startLine:text.slice(0,start).split('\n').length};
}
function plain(file) {
  const absolute=path.resolve(file);assert.equal(fs.realpathSync(absolute).toLowerCase(),absolute.toLowerCase(),'No reparse input '+file);
  assert(!fs.lstatSync(absolute).isSymbolicLink());return absolute;
}
export function prepareSteamStartupOwner(root, out, {authorizedWriteRoot, sourceBinding} = {}) {
  assert(typeof root==='string'&&path.isAbsolute(root),'Explicit absolute source root required');
  assert(typeof out==='string'&&path.isAbsolute(out),'Explicit absolute output root required');
  assert(typeof authorizedWriteRoot==='string'&&path.isAbsolute(authorizedWriteRoot),'Explicit authorizedWriteRoot required; no default');
  root=plain(root);out=path.resolve(out);authorizedWriteRoot=path.resolve(authorizedWriteRoot);
  assert.equal(out.toLowerCase(),authorizedWriteRoot.toLowerCase(),'Output must equal explicit authorized write root');
  plain(path.dirname(authorizedWriteRoot));
  if(fs.existsSync(authorizedWriteRoot))plain(authorizedWriteRoot);
  const relativeOut=path.relative(root,out);
  assert(relativeOut!==''&&(path.isAbsolute(relativeOut)||relativeOut==='..'||relativeOut.startsWith('..'+path.sep)),
    'Preparation artifacts must be outside candidate source');
  assert(sourceBinding&&sourceBinding.kind==='ymcc.steam-startup-owner-source.v1','Explicit source binding required');
  assert(typeof sourceBinding.root==='string'&&path.isAbsolute(sourceBinding.root),'Bound absolute source root required');
  assert(typeof sourceBinding.authorizedWriteRoot==='string'&&path.isAbsolute(sourceBinding.authorizedWriteRoot),'Bound absolute authorized write root required');
  assert.equal(plain(sourceBinding.root).toLowerCase(),root.toLowerCase(),'Bound source root');
  assert.equal(path.resolve(sourceBinding.authorizedWriteRoot).toLowerCase(),authorizedWriteRoot.toLowerCase(),'Source binding must authorize the same output root');
  assert(Array.isArray(sourceBinding.files)&&sourceBinding.files.length>0,'Bound source inputs required');
  const bound=new Map();
  for(const row of sourceBinding.files){
    assert(typeof row.relative==='string'&&!path.isAbsolute(row.relative)&&!row.relative.split(/[\\/]/).includes('..'),'Bound relative source path');
    assert(/^[a-f0-9]{64}$/.test(row.sha256)&&!bound.has(row.relative),'Unique exact source hash binding');bound.set(row.relative,row.sha256);
  }
  const sources=[], seen=new Map();
  const read=relative=>{assert(bound.has(relative),'Unbound preparation input '+relative);
    const file=plain(path.join(root,relative)),bytes=fs.readFileSync(file),digest=sha(bytes);
    assert.equal(digest,bound.get(relative),'Bound source changed '+relative);
    if(seen.has(relative))assert.equal(digest,seen.get(relative),'Source moved during preparation '+relative);
    else {seen.set(relative,digest);sources.push({relative,path:file,bytes:bytes.length,sha256:digest});}
    return bytes.toString('utf8');};
  const main=read('native/main.cpp'), windows=read('native/steam_native_launch_windows.h'), preferences=read('native/boot_mode_preferences.h');
  const launchPath=read('native/steam_launch_path.h');
  const guard=fragment(main,/^class SettingsFileGuard\s*\{/gm,'SettingsFileGuard',true);
  const match=extract(main,'ymSettingsBootWriteMatches'), owner=extract(main,'mouseModeBootOwnerCheck');
  const registration=fragment(main,/^    ipc_on\("steam\.startup\.set",/gm,'ipc_on(steam.startup.set)');
  const originalRegistration=registration.code+');';
  const registrationStart=main.indexOf(registration.code);assert.equal(main.slice(registrationStart+registration.code.length,registrationStart+registration.code.length+2),');');
  registration.code=originalRegistration;registration.sha256=sha(originalRegistration);registration.endUtf8ByteExclusive+=2;
  const constructor='const ymcc::steamlaunch::Startup startup(POWER_CONTROL_DIR);';
  const adaptedConstructor='const ymcc::steamlaunch::Startup startup(POWER_CONTROL_DIR, fixtureImage(), fixtureProfile());';
  assert.equal(originalRegistration.split(constructor).length,2,'One exact default-profile constructor adaptation');
  const adapted=originalRegistration.replace(constructor,adaptedConstructor);
  assert.equal(adapted.replace(adaptedConstructor,constructor),originalRegistration,'Reversible constructor-only adaptation');
  assert(!/boot_mode_identity\s*::\s*current\s*\(|bootModeStartupIdentity\s*\(/.test(adapted),'No real boot identity dependency');
  const origin=fragment(preferences,/^inline WriteOrigin readOrigin\(/gm,'readOrigin');
  const enumMatches=[...preferences.matchAll(/^enum class WriteOrigin[^\r\n]+;/gm)];assert.equal(enumMatches.length,1);
  const helpers=[['native/steam_launch_path.h',launchPath,['lower','trim']],['native/steam_native_launch_windows.h',windows,['readText','writeText']]]
    .flatMap(([source,text,symbols])=>symbols.map(symbol=>({...fragment(text,new RegExp('^inline[^\\r\\n;{]*\\b'+symbol+'\\s*\\(','gm'),symbol),source})));
  const observation=fragment(windows,/^struct TextObservation\s*\{/gm,'TextObservation',true);
  const startup=fragment(windows,/^struct Startup\s*\{/gm,'Startup',true);
  const legacyFlag=[...windows.matchAll(/^inline constexpr wchar_t legacyLogonFlag\[\][^\r\n]*;/gm)];
  assert.equal(legacyFlag.length,1,'Unique original Startup legacy flag');
  assert(guard.code.includes('std::unique_lock<std::timed_mutex>'),'Original nonrecursive local mutex');
  assert(guard.code.includes('Local\\\\YeManCC-UnifiedSettings'),'Exact original named mutex terminal');
  const selected=steamStartupOwnerSelection();
  const selectionLiterals=JSON.stringify(selected).match(/.{1,8192}/g).map(piece=>'R"YMCC_SELECTION('+piece+')YMCC_SELECTION"').join('\n');
  const generated=['// Original source fragments. Only the recorded IPC constructor is adapted.',
    'namespace ymcc::bootPreferences { using Json=nlohmann::json;',enumMatches[0][0],origin.code,'}',
    'namespace ymcc::steamlaunch { namespace pathfs=std::filesystem;',helpers[0].code,helpers[1].code,
    observation.code,helpers[2].code,helpers[3].code,legacyFlag[0][0],startup.code,'}',guard.code,match.code,owner.code,
    'static void registerStartupOwnerHandler(){',adapted,'}',
    'static json startupOwnerSelection(){return json::parse('+selectionLiterals+');}'].join('\n');
  assert(!generated.includes('#include "steam_native_launch_windows.h"'),'No accidental WindowsPorts/launcher in selected branch');
  for(const relative of ['native/settings_baseline.h','deps/json/json.hpp',"tools/boot_modes_fixture/steamStartupOwner.cpp.in",
    'tools/fixtures/steam_startup_owner.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs',
    'tools/lib/hc_controller_motion_fixture.mjs'])read(relative);
  // Inspect, retain and report the original review gate. Never refill pins or
  // declare this new selected branch approved by the old global terminal map.
  const reviewPath='tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/reviewed-source.json';
  const reviewed=JSON.parse(read(reviewPath));
  read('tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/reviewed-terminals.json');
  read('tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/terminal-adapter.h');
  const reviewComparison=reviewed.files.map(row=>{const actual=sha(read(row.path));return {relative:row.path,reviewedSha256:row.sha256,actualSha256:actual,matches:actual===row.sha256};});
  assert.deepEqual([...seen.keys()].sort(),[...bound.keys()].sort(),'Exact preparation dependency closure');
  for(const row of sources)assert.equal(sha(fs.readFileSync(row.path)),row.sha256,'Source moved before preparation seal '+row.relative);
  if(!fs.existsSync(out))fs.mkdirSync(out);plain(out);
  const write=(name,bytes)=>{assert.equal(path.basename(name),name,'Plain preparation filename required');
    const file=path.resolve(out,name);assert.equal(path.dirname(file).toLowerCase(),authorizedWriteRoot.toLowerCase(),'Confined preparation output');
    fs.writeFileSync(file,bytes,{flag:'wx'});};
  write('steam-startup-owner-original.inc',generated);
  write('selection.json',JSON.stringify(selected,null,2)+'\n');
  const fragments=[{...guard,source:'native/main.cpp'},{...match,source:'native/main.cpp'},{...owner,source:'native/main.cpp'},
    {...registration,source:'native/main.cpp'},{...origin,source:'native/boot_mode_preferences.h'},
    ...helpers,...[observation,startup].map(row=>({...row,source:'native/steam_native_launch_windows.h'}))];
  for(const row of fragments)write(row.symbol.startsWith('ipc_on')?'original-steam.startup.set.cpp':row.symbol+'.original.cpp',row.code);
  const strictRaw=/const auto current\s*=\s*ymcc::settingsBaseline::parseDocument\(ymSettingsReadRaw\(YM_SETTINGS_FILE\)\)/.test(adapted);
  const manifest={schemaVersion:1,status:'SOURCE_PREPARED_NOT_COMPILED_NOT_RUN',suite:'settings.boot-modes-file-ui original Steam startup ownership lane',
    selectionCount:selected.length,selector:'--startup-owner-only',compileDefine:'YMCC_STEAM_STARTUP_OWNER_SOURCE="<absolute preparation include>"',
    root,out,authorizedWriteRoot,sourceBindingSha256:sha(JSON.stringify(sourceBinding)),sources,fragments:fragments.map(({code,...row})=>row),
    generatedInclude:{path:path.join(out,'steam-startup-owner-original.inc'),bytes:Buffer.byteLength(generated),sha256:sha(generated)},
    constructorAdaptation:{from:constructor,to:adaptedConstructor,occurrences:1,reversible:true},strictRawObserved:strictRaw,
    terminals:{settings:'real owned primary/backup files; original nonrecursive timed mutex and named cross-process mutex',
      startup:'original Startup.set/readText/writeText and real owned temp profile files',
      serviceAndLog:'memory controlled ports only',profileDefault:'deny port; constructor supplies explicit isolated profile',
      child:'exact same image; hidden suspended create; assign kill-on-close Job before resume; bounded join and Job zero before close'},
    originalNoIoReview:{pinsModified:false,newBranchApprovedByOldReview:false,comparison:reviewComparison},
    dependencyLimits:'sources pins are source preparation only; compiler/SDK/link inputs and effective environment must be frozen by original authorized build before execution',
    compileInputs:["tools/boot_modes_fixture/steamStartupOwner.cpp.in",'native/settings_baseline.h','deps/json/json.hpp',path.join(out,'steam-startup-owner-original.inc')],
    compileOptions:['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE'],
    compileOptionBasis:'Original native-launch-noio/native-case.mjs buildCase recipe; no new warning override',
    directedSelection:'Optional --case-index <exact original selection index>; unknown/empty indices rejected before creating the run directory',
    defaultCases:'Existing default steam_native_launch_selftest.cpp stays byte-for-byte unchanged; this supporting ownership template does not inherit its NoIO approval',
    execution:{compiled:false,producerRuns:0,nativeRuns:0,installedApplicationActions:0,bootIdentityQueries:0},
    estimate:{basis:'static planning estimate only; no same-source timing evidence',compileSeconds:[15,45],selectedRunSeconds:[8,25],cleanupSeconds:[2,5]}};
  write('PREPARATION.json',JSON.stringify(manifest,null,2)+'\n');
  return {status:manifest.status,selectionCount:selected.length,strictRawObserved:strictRaw,reviewDrift:reviewComparison.filter(row=>!row.matches).map(row=>row.relative),manifest:path.join(out,'PREPARATION.json')};
}
if(process.argv[1]&&pathToFileURL(path.resolve(process.argv[1])).href===import.meta.url){
  assert.equal(process.argv[2],'--prepare-only','Only explicit source preparation is implemented');
  assert.equal(process.argv.length,7,'--prepare-only <candidate-root> <output-root> <authorized-write-root> <source-binding.json>');
  const sourceBinding=JSON.parse(fs.readFileSync(plain(process.argv[6]),'utf8'));
  console.log(JSON.stringify(prepareSteamStartupOwner(process.argv[3],process.argv[4],{authorizedWriteRoot:process.argv[5],sourceBinding}),null,2));
}
