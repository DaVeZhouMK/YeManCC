import {captureOemInputHeaders, oemInputBindingFunctions} from './lib/hc_oem_input_fixture.mjs';
// Offline extracted production regression for the nine remaining fixed HC models.
// node tools/hc_remaining_model_capabilities_selftest.mjs [--mutation-checks] [--out directory]
// No network, Host, device enumeration, hooks, input injection or power operations.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { generateExpectations, closeBrace } from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
import { calibrationToolchainPreamble } from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const runnerPath = fileURLToPath(import.meta.url), repo = path.resolve(path.dirname(runnerPath), '..');
const fixtures = path.join(repo, 'tools/fixtures/hc_remaining_model_capabilities');
let workspace = repo;
while (!fs.existsSync(path.join(workspace, 'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))) {
  const parent = path.dirname(workspace); if (parent === workspace) throw Error('Fixed HC workspace inputs unavailable'); workspace = parent;
}
const options = {}, args = process.argv.slice(2);
for (let i = 0; i < args.length; ++i) {
  if (['--generate-only', '--expectations-only', '--mutation-checks'].includes(args[i])) options[args[i].slice(2)] = true;
  else if (['--out', '--source', '--before', '--vcvars'].includes(args[i]) && args[i+1]) options[args[i].slice(2)] = args[++i];
  else throw Error(`Unknown/incomplete argument ${args[i]}`);
}
const validation = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/Remaining9');
const out = path.resolve(options.out || path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/OneXApplication/model-regression'));
const sourcePath = path.resolve(options.source || path.join(repo, 'native/main.cpp'));
const beforePath = path.resolve(options.before || path.join(validation, 'main.before.cpp'));
const relativeOut = path.relative(repo, out);
if (relativeOut === '' || (!relativeOut.startsWith(`..${path.sep}`) && !path.isAbsolute(relativeOut))) throw Error('--out must be outside the production repository');
// Never replace either input snapshot with a generated evidence file.
for (const input of [sourcePath,beforePath]) if (path.dirname(input)===out) throw Error('--out cannot be the input snapshot directory');
fs.mkdirSync(out, { recursive:true });
const hash = value => createHash('sha256').update(value).digest('hex');
const writeJson = (name,value) => fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n');
const expectations = generateExpectations(workspace, fixtures);
writeJson('expectations.json', expectations);
if (options['expectations-only']) { console.log(`Independent oracle: ${expectations.identities.length} aliases, ${expectations.models.length-1} new models, 80 legacy classes; ${expectations.integrity.inputs.length} SHA256/gitBlob verified files`); process.exit(0); }
if (!fs.existsSync(beforePath)) throw Error(`Frozen preimage unavailable: ${beforePath}`);
const sourceBytes=fs.readFileSync(sourcePath), beforeBytes=fs.readFileSync(beforePath);
const source=sourceBytes.toString('utf8'), before=beforeBytes.toString('utf8'), bindings=[];
const oemHeaders=captureOemInputHeaders({repo,out,source});
function extract(text,snapshot) {
  const parts=[];
  const bind=(name,start,end,kind='compiled-definition')=>{
    const code=text.slice(start,end); bindings.push({snapshot,name,kind,line:text.slice(0,start).split('\n').length,byteOffset:Buffer.byteLength(text.slice(0,start)),bytes:Buffer.byteLength(code),sha256:hash(code)});return code;
  };
  const definition=(marker,name=marker)=>{
    let start=-1,open;
    do { start=text.indexOf(marker,start+1); if(start<0)throw Error(`${snapshot}: missing ${marker}`);open=text.indexOf('{',start); }
    while(text.indexOf(';',start)>=0&&text.indexOf(';',start)<open);
    let end=closeBrace(text,open)+1;if(text[end]===';')++end;return bind(name,start,end);
  };
  const fn=(name,kind='compiled-definition')=>{
    for(const m of text.matchAll(new RegExp(`^static [^\\n]*\\b${name}\\(`,'gm'))) {
      const open=text.indexOf('{',m.index),semi=text.indexOf(';',m.index);
      if(semi>=0&&semi<open)continue;return bind(name,m.index,closeBrace(text,open)+1,kind);
    } throw Error(`${snapshot}: missing function ${name}`);
  };
  const global=name=>{const m=text.match(new RegExp(`^static [^\\n;]*\\b${name}\\b[^;]*;`,'m'));if(!m)throw Error(`${snapshot}: missing state ${name}`);return bind(name,m.index,m.index+m[0].length);};
  parts.push(fn('ascii_lower'),fn('trim_ascii'),definition('enum class YmccFamily : int {'),definition('struct MachineIdentity {'),fn('ymccDmiNormalize'),fn('ymccResolveFamily'),fn('ymccFamilyName'),definition('enum OemVkBit : uint32_t {'));
  for(const m of text.matchAll(/^static constexpr uint64_t kOemBit[^;]+;/gm))parts.push(bind(m[0].match(/kOemBit\w+/)[0],m.index,m.index+m[0].length));
  parts.push(definition('struct OemChordRule {'),definition('struct OemChordProfile {'));
  for(const m of text.matchAll(/^static const OemChordRule (\w+)\[\] = \{/gm))parts.push(definition(m[0],m[1]));
  parts.push(definition('enum class OemChordProfileId : int {'),definition('static const OemChordProfile kOemProfiles[] = {'));
  for(const name of ['oemChordProfileFor','oemBackHookVkBit','oemChordEvalRules','oemChordProfileIdFor'])parts.push(fn(name));
  parts.push(definition('enum class OemKeyForm : int {'),definition('struct OemKeyDef {'),definition('struct OemCatalogRef {'));
  for(const m of text.matchAll(/^static const OemKeyDef (\w+)\[\] = \{/gm))parts.push(definition(m[0],m[1]));
  parts.push('template <size_t N>\n'+fn('oemCatalogOf'),fn('oemCatalogForFamily'),fn('nativeKeyboardShortcutMaskBit'));
  if(text.includes('oemUnifiedInputBindingFor('))for(const name of oemInputBindingFunctions)parts.push(fn(name));
  for(const name of ['ymccParseAxisToken','ymccSplitAxisRecipe','ymccApplyNamedImuMatrix','ymccDeviceClassLightingSet','ymccDeviceClassSupportsLighting'])parts.push(fn(name));
  parts.push(definition('enum class OnexInitProfile : int {'),definition('struct OnexCapDescriptor {'),fn('onexCapDescriptorFor'));
  parts.push(global('g_machineIdentity'),definition('enum class PowerLifecycle : uint8_t {'),global('g_powerLifecycle'));
  parts.push('// AoAc is a controlled in-memory input. No Windows power-capability API runs.\nstatic bool testAoAc=false; static int testAoAcQueries=0;\nstatic bool sgPlatformIsAlwaysOnAlwaysConnected(){++testAoAcQueries;return testAoAc;}');
  parts.push(fn('sgShouldKeepInputDuringStandby'),fn('sgVirtualWriterAllowedNow'));
  parts.push('static bool testLogging=true; static std::vector<json> testLogs;\nstatic bool domainLogEnabled(const char*){return testLogging;}\nstatic void appendNativeLifecycleLog(const char* event,json detail){detail["event"]=event;testLogs.push_back(std::move(detail));}');
  if(snapshot==='current') {
    // The query may inline route selection or use this pure production helper.
    // Include the actual helper when present; never supply a test-written route.
    if (/^static [^\n]*\boemKeyCaptureRoute\(/m.test(text)) parts.push(fn('oemKeyCaptureRoute'));
    parts.push(fn('oemKeyCaptureImplemented'));
  }
  const ipcAt=text.indexOf('ipc_on("oem.keys.get",');
  const lambdaStart=text.indexOf('[]',ipcAt),open=text.indexOf('{',lambdaStart),end=closeBrace(text,open)+1;
  if(ipcAt<0||lambdaStart<ipcAt||!text.slice(lambdaStart,open).includes('const json&'))throw Error('oem.keys.get lambda boundary moved');
  const lambda=bind('oem.keys.get:lambda',lambdaStart,end,'compiled-production-lambda');
  parts.push(`static json extractedOemKeysGet(const json& request){auto handler=${lambda};return handler(request);}`);
  if(snapshot==='current') {
    for(const name of ['g_oemBackProfileId','g_oemKbDown','g_oemKbDownRaw','g_oemMouseLeft','g_oemKeyHooksActive','g_oemKeyMask','g_oemBackStateKb','g_gpdBackIfaceGen','g_gpdBackIfaceReady','g_gpdChordSnapPhysical','g_gpdChordSnapBaseKey','g_gpdChordSnapPrefix','g_gpdChordPubGen','g_oemRearMapToL5','g_oemRearMapToR5'])parts.push(global(name));
    parts.push(fn('oemBackHookRecompute'),fn('oemRearMapRefresh'),fn('gamepadOemPressedKeys'));
    const parent=fn('oemBackMappingReconcileOnUi','source-only-parent'),parentAt=text.indexOf(parent);
    const start=text.indexOf('g_oemKeyHooksActive.store(false, std::memory_order_release);',parentAt),stop=text.indexOf('oemKeyboardConsumeMaintain(',start);
    if(start<parentAt||stop<start||stop>parentAt+parent.length)throw Error('Hook-disable publication extraction boundary moved');
    const prefix=bind('oemBackMappingReconcileOnUi:disable-publication-prefix',start,stop,'compiled-production-fragment');
    if(!prefix.includes('g_oemKbDown = 0;')||!prefix.includes('oemBackHookRecompute();'))throw Error('Production disable prefix does not clear/recompute');
    parts.push(`static void extractedDisablePublicationPrefix(){\n${prefix}\n}`);
  }
  return {code:parts.join('\n\n'),fn,definition};
}
const frozen=extract(before,'before'),actual=extract(source,'current');
const enumNames=code=>code.slice(code.indexOf('{')+1,code.lastIndexOf('}')).replace(/\/\/[^\n]*/g,'').split(',').map(s=>s.trim().split('=')[0].trim()).filter(Boolean);
const legacyProfiles=enumNames(frozen.definition('enum class OemChordProfileId : int {')).filter(n=>n!=='None');
const legacyFamilies=enumNames(frozen.definition('enum class YmccFamily : int {'));
const canonicalNames=[...frozen.fn('nativeKeyboardShortcutMaskBit').matchAll(/\{"([^"]+)",/g)].map(m=>m[1]);
expectations.canonicalNames=canonicalNames; writeJson('expectations.json',expectations);
const immutable=['onexCapDescriptorFor','ymccDeviceClassSupportsLighting','nativeKeyboardShortcutMaskBit','oemBackHookVkBit'];
const compareSourceBoundary=(name,oldCode,newCode)=>({name,unchanged:oldCode.replaceAll('\r\n','\n')===newCode.replaceAll('\r\n','\n'),comparison:'CRLF-to-LF-only',rawBytesEqual:oldCode===newCode,beforeRawSha256:hash(oldCode),currentRawSha256:hash(newCode),beforeNormalizedSha256:hash(oldCode.replaceAll('\r\n','\n')),currentNormalizedSha256:hash(newCode.replaceAll('\r\n','\n'))});
const sourceChecks=immutable.map(name=>compareSourceBoundary(name,frozen.fn(name),actual.fn(name)));
// Unified generation may reorder the source list; preserve exact registered identities instead.
const lightingIdentities=code=>[...code.matchAll(/"(HandheldCompanion\.Devices\.[A-Za-z0-9_.]+)"/g)].map(m=>m[1]).sort();
const beforeLightingCode=frozen.fn('ymccDeviceClassLightingSet'),currentLightingCode=actual.fn('ymccDeviceClassLightingSet');
const beforeLightingIdentities=lightingIdentities(beforeLightingCode),currentLightingIdentities=lightingIdentities(currentLightingCode);
sourceChecks.push({name:'ymccDeviceClassLightingSet',unchanged:JSON.stringify(beforeLightingIdentities)===JSON.stringify(currentLightingIdentities),comparison:'exact-identity-set; generated-order-independent',beforeRawSha256:hash(beforeLightingCode),currentRawSha256:hash(currentLightingCode),beforeIdentities:beforeLightingIdentities,currentIdentities:currentLightingIdentities,identitySetSha256:hash(JSON.stringify(beforeLightingIdentities))});
// Check the exact production OneX hardware admission block; do not execute BUS/hardware code.
const admissionMarker='const std::string& onexCls = g_machineIdentity.deviceClass;';
function admission(text,label){const at=text.indexOf(admissionMarker),end=text.indexOf('onexBackPump();',at);if(at<0||end<at||end-at>3500)throw Error('OneX BUS admission boundary moved');const code=text.slice(at,end+'onexBackPump();'.length);bindings.push({snapshot:label,name:'OneX BUS hardware admission',kind:'source-only-fragment',line:text.slice(0,at).split('\n').length,sha256:hash(code)});return code;}
sourceChecks.push(compareSourceBoundary('OneX BUS hardware admission',admission(before,'before'),admission(source,'current')));
if(sourceChecks.some(c=>!c.unchanged))throw Error(`Unapproved legacy source change: ${sourceChecks.filter(c=>!c.unchanged).map(c=>c.name).join(', ')}`);
const fixturePath=path.join(fixtures,'harness.cpp'),fixture=fs.readFileSync(fixturePath,'utf8');
const jsonPath=path.join(repo,'deps/json/json.hpp');
const preamble='#define NOMINMAX\n#define WIN32_LEAN_AND_MEAN\n#include <windows.h>\n#include <xinput.h>\n#include <algorithm>\n#include <array>\n#include <atomic>\n#include <cctype>\n#include <cmath>\n#include <cstdint>\n#include <fstream>\n#include <iostream>\n#include <map>\n#include <set>\n#include <stdexcept>\n#include <string>\n#include <utility>\n#include <vector>\n#include "json.hpp"\nusing json=nlohmann::json;\n';
const cpp=oemHeaders.preamble+preamble+`namespace before{\n${frozen.code}\n}\nnamespace current{\n${actual.code}\n}\n`+
  'struct LegacyIdPair{const char* name;int oldId,newId;};\nstatic const LegacyIdPair legacyProfiles[]={\n'+legacyProfiles.map(n=>`{"${n}",int(before::OemChordProfileId::${n}),int(current::OemChordProfileId::${n})},`).join('\n')+'\n};\n'+
  'static const LegacyIdPair legacyFamilies[]={\n'+legacyFamilies.map(n=>`{"${n}",int(before::YmccFamily::${n}),int(current::YmccFamily::${n})},`).join('\n')+'\n};\n'+
  'static int expectedProfileId(const std::string& name){\n'+[...new Set(expectations.models.map(m=>m.profile))].map(n=>`if(name=="${n}")return int(current::OemChordProfileId::${n});`).join('\n')+'\nthrow std::runtime_error("Unknown expected profile");}\n'+
  'static int expectedFamily(const std::string& name){\n'+[...new Set(expectations.models.map(m=>m.family))].map(n=>`if(name=="${n}")return int(current::YmccFamily::${n});`).join('\n')+'\nthrow std::runtime_error("Unknown expected family");}\n'+fixture;
const cppPath=path.join(out,'hc_remaining_models_extracted.cpp');fs.writeFileSync(cppPath,cpp);fs.writeFileSync(path.join(out,'main.current.cpp'),sourceBytes);
const evidence={schema:'hc-remaining-model-capabilities-offline-v2',startedAt:new Date().toISOString(),status:options['generate-only']?'generated-only-not-validated':'running',
  source:{path:sourcePath,sha256:hash(sourceBytes)},before:{path:beforePath,sha256:hash(beforeBytes)},hcCommit:expectations.hcCommit,integrity:expectations.integrity,
  runner:{path:runnerPath,sha256:hash(fs.readFileSync(runnerPath))},fixtures:['harness.cpp','expectations.mjs','README.md','source-pins.json'].map(n=>({path:path.join(fixtures,n),sha256:hash(fs.readFileSync(path.join(fixtures,n)))})),
  toolchain:{path:path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),sha256:hash(fs.readFileSync(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')))},
  oemInputHeaders:oemHeaders.inputs,jsonHeader:{path:jsonPath,sha256:hash(fs.readFileSync(jsonPath))},generatedCppSha256:hash(cpp),expectationsSha256:hash(fs.readFileSync(path.join(out,'expectations.json'))),bindings,sourceChecks,configurations:[],
  legacyCaptureCorrection:expectations.steamDeckCapturePolicy,
  boundaries:['Exact production identities/selectors/catalogs/profiles/evaluator/canonical names/matrix methods and oem.keys.get lambda execute in a standalone offline process.',
    'All 181 old aliases retain complete query/log equality after validating and removing only three added fields on the exact SteamDeck identity; its two Embedded catalog keys remain supported but uncapturable.',
    'captureImplemented and captureRoute describe reviewed software source ownership only; producer behavior, vendor duplicate filtering and runtime readiness require independent integration/session evidence.',
    'Catalog logging is captured into a vector by a test sink; no production log file is opened. AoAc is an explicit boolean test input, not an OS capability query.',
    'Power phases and profile-generation transitions are in-memory simulations. Only the exact production disable-publication prefix runs; full dispatcher/hook installation and Windows power transitions do not run.',
    'OneX BUS admission and legacy source gates are source-only hash comparisons. No hardware I/O, Windows sleep/resume, Host/Steam input consumption, fan or lighting acceptance is claimed.']};
const save=()=>writeJson('source-bindings.json',evidence);save();
function run(command,argv,log,cwd=out,env=process.env){const r=spawnSync(command,argv,{cwd,env,encoding:'utf8',windowsHide:true,timeout:180000,maxBuffer:16*1024*1024});fs.writeFileSync(path.join(out,log),(r.stdout||'')+(r.stderr||'')+(r.error?.message||''));if(r.error||r.status!==0)throw Error(`Command failed (${r.status}): ${log}`);return r.stdout;}
try {
  if(!options['generate-only']) {
    if(process.platform!=='win32')throw Error('Windows MSVC is required; use --generate-only elsewhere');
    let vcvars=options.vcvars;
    if(!vcvars){const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');const install=run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery.log').trim();vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');}
    if(!fs.existsSync(vcvars)||/[\r\n"%]/.test(vcvars))throw Error('MSVC environment path unavailable/unsupported');
    const envCmd=path.join(out,'compiler-environment.cmd');fs.writeFileSync(envCmd,calibrationToolchainPreamble(vcvars)+'set PATH\r\nset INCLUDE\r\nset LIB\r\nset VCTools\r\nset WindowsSdk\r\nexit /b 0\r\n');
    const r=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envCmd],{encoding:'utf8',windowsHide:true,timeout:30000});if(r.error||r.status!==0)throw Error('MSVC environment setup failed');
    const env={...process.env};for(const line of r.stdout.split(/\r?\n/)){const at=line.indexOf('=');if(at>0){const key=line.slice(0,at),existing=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[existing||key]=line.slice(at+1);}}
    fs.writeFileSync(path.join(out,'compiler-environment.log'),`vcvars=${vcvars}\nVCToolsVersion=${env.VCToolsVersion}\nWindowsSDKVersion=${env.WindowsSDKVersion}\n`);
    for(const [name,flags] of [['Release',['/O2','/DNDEBUG','/MD']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd']]]) {
      const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const exe=path.join(dir,'hc_remaining_models.exe');fs.rmSync(path.join(dir,'run.log'),{force:true});
      const command=['cl.exe','/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',...flags,`/I${path.dirname(jsonPath)}`,cppPath,`/Fo${path.join(dir,'harness.obj')}`,`/Fd${path.join(dir,'harness.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
      run(command[0],command.slice(1),`${name}/compile.log`,dir,env);
      const report=JSON.parse(run(exe,[path.join(out,'expectations.json')],`${name}/run.log`,dir,env).trim());if(report.status!=='pass')throw Error(`${name} harness did not pass`);
      evidence.configurations.push({name,flags,compileCommand:command,executableSha256:hash(fs.readFileSync(exe)),...report});save();console.log(`${name}: PASS (${report.checks} checks)`);
    }
    if(options['mutation-checks']) {
      const injectInFunction=(name,fragment)=>{const code=actual.fn(name),open=code.indexOf('{');return source.replace(code,code.slice(0,open+1)+'\n'+fragment+'\n'+code.slice(open+1));};
      const exactNewClass='(cls == "HandheldCompanion.Devices.OneXPlayerX2" || cls == "HandheldCompanion.Devices.OneXPlayer3" || cls == "HandheldCompanion.Devices.OneXPlayerX2MiniPro")';
      const mutateQuery=(transform)=>{
        const entry=bindings.find(b=>b.snapshot==='current'&&b.name==='oem.keys.get:lambda');
        const code=sourceBytes.subarray(entry.byteOffset,entry.byteOffset+entry.bytes).toString('utf8');
        const changed=transform(code);if(code===changed)throw Error('Query mutation did not change actual lambda');
        return source.replace(code,changed);
      };
      const injectBeforeQueryLog=fragment=>mutateQuery(code=>{
        const marker='if (domainLogEnabled("virtual")) {';
        if(code.split(marker).length!==2)throw Error('Query log boundary no longer unique');
        return code.replace(marker,fragment+'\n'+marker);
      });
      const cases=[
        {name:'steamdeck-default-true',section:'steamdeck-unimplemented-capture-gate',text:injectInFunction('oemKeyCaptureImplemented','if (family == YmccFamily::ValveSteamDeck && cls == "HandheldCompanion.Devices.SteamDeck") return true;')},
        {name:'identity-old-prefix',section:'identity-selector-catalog-profile',text:(source.split('p == "AYANEO 3" || p == "AB07"').length === 2 ? source.replace('p == "AYANEO 3" || p == "AB07"','p.find("AYANEO 3") != std::string::npos || p == "AB07"') : (()=>{throw Error('identity mutation anchor is not unique');})())},
        {name:'cycle-inverse',section:'matrix-remap-independent-helper',text:source.replace('hc-a250b19a:onexplayer-x2:gyro:z-x-neg-y','hc-a250b19a:onexplayer-x2:gyro:y-z-neg-x')},
        {name:'ayaneo3-win-d',section:'profile-evaluator-press-release-combinations',text:source.replace(actual.definition('static const OemChordRule kP_Ayaneo3[] = {'),actual.definition('static const OemChordRule kP_Ayaneo3[] = {').replace('kVkF24,','kVkWin | kVkD,'))},
        {name:'home-capture-removed',section:'oem.keys.get-lambda-return-and-log',text:injectInFunction('oemKeyCaptureImplemented',`if (family == YmccFamily::OneXPlayer && ${exactNewClass} && key == "home") return false;`)},
        {name:'unknown-key-accepted',section:'capture-gate-exact-five-keys',text:injectInFunction('oemKeyCaptureImplemented',`if (family == YmccFamily::OneXPlayer && ${exactNewClass} && key == "__not_catalog_key__") return true;`)},
        {name:'home-keyboard-route',section:'oem.keys.get-lambda-return-and-log',text:injectBeforeQueryLog('for (auto& key : arr) if (key.contains("captureRoute") && key.at("keyId") == "home") key["captureRoute"] = "keyboard-vk";')},
        {name:'mini-rear-vendor-route',section:'oem.keys.get-lambda-return-and-log',text:injectBeforeQueryLog('if (g_machineIdentity.deviceClass == "HandheldCompanion.Devices.OneXPlayerX2MiniPro") for (auto& key : arr) if (key.at("keyId") == "l4" || key.at("keyId") == "r4") key["captureRoute"] = "onex-x2-vendor-held";')},
        {name:'legacy-route-leak',section:'oem.keys.get-lambda-return-and-log',text:injectBeforeQueryLog('for (auto& key : arr) if (!key.contains("captureRoute")) key["captureRoute"] = "keyboard-vk";')},
        {name:'logged-route-dropped',section:'oem.keys.get-lambda-return-and-log',text:mutateQuery(code=>code.replace('{"keys", arr}', '{"keys", [&](){ json logged = arr; for (auto& key : logged) key.erase("captureRoute"); return logged; }()}'))},
      ];
      const outcomes=await Promise.allSettled(cases.map(async c=>{
        if(c.text===source)throw Error(`${c.name}: mutation did not change production snapshot`);
        const dir=path.join(out,'mutations',c.name);fs.mkdirSync(dir,{recursive:true});const mutant=path.join(dir,'main.mutant.cpp');fs.writeFileSync(mutant,c.text);const childOut=path.join(dir,'evidence');
        const argv=[runnerPath,'--source',mutant,'--before',beforePath,'--out',childOut,'--vcvars',vcvars];
        const result=await new Promise((resolve,reject)=>{const child=spawn(process.execPath,argv,{windowsHide:true,stdio:['ignore','pipe','pipe']});let output='';child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);const timer=setTimeout(()=>{child.kill();reject(Error('Mutation timeout'));},240000);child.on('error',e=>{clearTimeout(timer);reject(e);});child.on('close',status=>{clearTimeout(timer);resolve({status,output});});});
        fs.writeFileSync(path.join(dir,'driver.log'),result.output);
        const compile=path.join(childOut,'Release/compile.log'),log=path.join(childOut,'Release/run.log');
        if(result.status===0||!fs.existsSync(compile)||!fs.existsSync(log))throw Error(`${c.name}: no compiled semantic rejection`);
        const failure=fs.readFileSync(log,'utf8').trim();let failureJson;try{failureJson=JSON.parse(failure);}catch{throw Error(`${c.name}: invalid semantic failure report`);}
        if(failureJson.status!=='fail'||failureJson.section!==c.section)throw Error(`${c.name}: wrong boundary ${failure}`);
        return {name:c.name,status:'detected',mutantSha256:hash(c.text),expectedSection:c.section,failure:failureJson,compileLog:compile,runLog:log};
      }));
      evidence.mutations=outcomes.map((r,i)=>r.status==='fulfilled'?r.value:{name:cases[i].name,status:'invalid-control',error:r.reason.message});save();
      if(outcomes.some(r=>r.status==='rejected'))throw Error('At least one deliberate fault lacked a compiled semantic rejection');
    }
    const finishBytes=fs.readFileSync(sourcePath),finishSource=finishBytes.toString('utf8');
    evidence.sourceAtFinish={sha256:hash(finishBytes),wholeFileDrift:hash(finishBytes)!==hash(sourceBytes),
      extractedBindings:bindings.filter(b=>b.snapshot==='current').map(b=>({name:b.name,sha256:b.sha256,unchanged:finishSource.includes(sourceBytes.subarray(b.byteOffset,b.byteOffset+b.bytes).toString('utf8'))}))};
    if(!oemHeaders.unchanged()||evidence.sourceAtFinish.extractedBindings.some(b=>!b.unchanged))throw Error('Relevant extracted production code changed while tests ran; re-run against the final integration snapshot');
    evidence.status='pass';
  }
  evidence.finishedAt=new Date().toISOString();save();console.log(`Evidence: ${path.join(out,'source-bindings.json')}`);
} catch(error){evidence.status='fail';evidence.error=error.message;evidence.finishedAt=new Date().toISOString();save();console.error(error.message);process.exitCode=1;}