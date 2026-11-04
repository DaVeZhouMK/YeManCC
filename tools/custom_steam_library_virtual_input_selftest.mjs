import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import {snapshotVirtualControllerSource} from './virtual_controller_runtime_source.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';

const args=process.argv.slice(2);let rootArgument=null;const options=new Map();
for(let i=0;i<args.length;++i){const arg=args[i];if(!arg.startsWith('--')){assert(i===0&&rootArgument===null,'Unexpected positional argument: '+arg);rootArgument=arg;continue;}assert(['--case','--variants'].includes(arg),'Unknown argument: '+arg);assert(!options.has(arg),'Duplicate argument: '+arg);assert(args[i+1]&&!args[i+1].startsWith('--'),arg+' requires a value');options.set(arg,args[++i]);}
const selectedCases=options.get('--case')?.split(',')??[];assert(new Set(selectedCases).size===selectedCases.length&&selectedCases.every(Boolean),'Invalid case selection');
const originalVariants=['production','background-owner-original','original-child-bypass','msi-original-bound-gap','msi-family-gate-removed','virtual-ui-original-gap','dsu-native-original-bypass'];
const selectedVariants=options.get('--variants')?.split(',')??originalVariants;
assert(selectedVariants.length&&new Set(selectedVariants).size===selectedVariants.length&&selectedVariants.every(v=>[...originalVariants,'background-discovery-every-tick'].includes(v)),'Invalid explicit variant selection');
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const build = path.resolve(repo, '../../Build/CustomSteamLibrary');
const coreEvidence = path.resolve(repo, '../../../Testing/YMCC/Evidence');
const root = path.resolve(rootArgument || path.join(coreEvidence, 'Build', 'CustomLibrary-VirtualInput-' + Date.now()));
const within = (base, file) => { const relative = path.relative(base, file); return relative !== '' && relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative); };
assert([build, coreEvidence].some(base => within(base, root)) && !fs.existsSync(root), 'Use a fresh isolated legacy Build or Testing/YMCC/Evidence directory');
fs.mkdirSync(root, { recursive: true });
const preparationInputs=['tools/custom_steam_library_virtual_input_selftest.mjs','tools/custom_steam_library_virtual_input_fixture.cpp','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','native/dsu_server.cpp','native/controller_persona.h','native/custom-steam-library/workspace_host.cpp','native/dsu_frame.h','native/dsu_server.h','deps/json/json.hpp'].map(relative=>{
 const bytes=fs.readFileSync(path.join(repo,relative)),snapshot=path.join(root,'preparation-inputs',relative);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});return{relative,snapshot,sha256:createHash('sha256').update(bytes).digest('hex')};
});
const mainFile = path.join(repo, 'native/main.cpp');
const expanded = snapshotVirtualControllerSource(mainFile, path.join(root, 'source/native'));
const bytes = expanded.inputs[0].data;
const source = expanded.source.replaceAll('\r\n', '\n');
const expansionHelper = path.join(repo, 'tools/virtual_controller_runtime_source.mjs');
const helperBytes = fs.readFileSync(expansionHelper);
fs.copyFileSync(expansionHelper, path.join(root, 'source/virtual_controller_runtime_source.mjs'));
function block(marker, declaration = false) {
  const start = source.indexOf(marker); assert(start >= 0, 'Missing production marker: ' + marker);
  const open = source.indexOf('{', start); assert(open > start);
  let depth = 0, state = '', escaped = false;
  for (let i = open; i < source.length; i++) {
    const character = source[i], next = source[i + 1];
    if (state === '//') { if (character === '\n') state = ''; continue; }
    if (state === '/*') { if (character === '*' && next === '/') { state = ''; i++; } continue; }
    if (state === '"' || state === "'") {
      if (escaped) { escaped = false; continue; }
      if (character === '\\') { escaped = true; continue; }
      if (character === state) state = ''; continue;
    }
    if (character === '/' && next === '/') { state = '//'; i++; continue; }
    if (character === '/' && next === '*') { state = '/*'; i++; continue; }
    if (character === '"' || character === "'") { state = character; continue; }
    if (character === '{') depth++;
    if (character === '}' && --depth === 0) return source.slice(start, i + 1) + (declaration ? ';' : '');
  }
  throw new Error('Unterminated production block: ' + marker);
}
function declaration(name) {
  const line = source.split('\n').find(line => line.startsWith('static ') && new RegExp('\\b' + name + '\\b').test(line) && line.trim().endsWith(';'));
  assert(line, 'Missing production declaration: ' + name); return line;
}
const outputGates = [block('static bool steamDeckUiOwnsInput('), block('static bool ps5UiOwnsInput('), block('static bool msiUiOwnsInput('), block('static bool virtualUiOwnsInput(')].join('\n');
assert(outputGates.match(/return gamepadUiOutputIsolationEligible\(\);/g)?.length === 4,
  'Every virtual output must reuse authoritative output isolation admission, not a second policy');
assert(!outputGates.includes('CustomSteamLibraryInputPhase'), 'Child phase must not bypass shared output admission');
const child = fs.readFileSync(path.join(repo, 'native/custom-steam-library/workspace_host.cpp'), 'utf8');
assert(child.includes('if (parentInputModeEnabled()) {') && /g_launchOptions\.inputOwner\s*==\s*L"host"\s*&&\s*!g_updateHealthOnly/.test(child),
  'Parent integration must not start a second child XInput poller');
const propertiesStart = source.indexOf('static constexpr wchar_t kCustomSteamLibraryInputOwnerProperty[]');
const propertiesEnd = source.indexOf('static ULONG_PTR customSteamLibraryWindowProperty(', propertiesStart);
const radialHelpers=declaration('g_radialUiOwnsInput')+'\n'+declaration('g_radialReturning')+'\n'+block('static bool radialHasInputOwner(');
const windowHelpers = [
  'static bool focusMainWindowIsForeground(', 'static bool refocusWebView(', 'static HWND customSteamLibraryWindow(',
  'static ULONG_PTR customSteamLibraryWindowProperty(', 'static bool customSteamLibraryProcessMatches(',
  'static bool customSteamLibraryParentOwned(', 'static HWND customSteamLibraryParentWindow(',
  'static bool customSteamLibraryInputSuppressed(', 'static bool customSteamLibraryChildForeground(',
  'static void customSteamLibrarySetIntegrationSession(',
  'static bool gamepadUiInputEligible() {', 'static bool gamepadUiOutputIsolationEligible() {',
  'static const char* customSteamLibrarySemanticAction(',
].map(marker => block(marker)).join('\n');
assert(!block('static bool refocusWebView(').includes('AttachThreadInput('), 'Foreground recovery must not share a potentially hung game input queue');
const uiStart = source.indexOf('static WORD g_uiShoulderPending = 0;');
const uiEnd = source.indexOf('static constexpr SHORT GP_UI_THUMB_THRESHOLD = 16000;', uiStart)
  + 'static constexpr SHORT GP_UI_THUMB_THRESHOLD = 16000;'.length;
assert(uiStart >= 0 && uiEnd > uiStart);
const outputDecision = ['steamDeckUiNeutral', 'ps5UiNeutral', 'msiUiNeutral', 'virtualUiCovered', 'virtualUiNeutral', 'submitNeutral', 'dsuUiNeutral', 'dsuSubmitNeutral'].map(name => {
  const line = source.split('\n').find(line => line.trimStart().startsWith('const bool ' + name + ' ='));
  assert(line, 'Missing final-output decision: ' + name); return line;
}).join('\n');
const effectiveStart = source.indexOf('    FocusFrameContent effectiveContent =');
const effectiveEnd = source.indexOf('    // B1：hash', effectiveStart);
assert(effectiveStart >= 0 && effectiveEnd > effectiveStart);
const fixture = fs.readFileSync(path.join(repo, 'tools/custom_steam_library_virtual_input_fixture.cpp'), 'utf8');
const replacements = {
  '//__PRODUCTION_OWNER_DECLARATIONS__': block('enum class CustomSteamLibraryInputPhase', true) + '\n' +
    declaration('g_controllerFrontendIsolationEnabled') + '\n' + block('static bool controllerFrontendIsolationSetting(') + '\n' +
    declaration('g_inputReleaseRequired') + '\n' + declaration('g_customSteamLibraryInputPhase') + '\n' + declaration('g_customSteamLibraryInputDeadline') + '\n' +
    source.slice(propertiesStart, propertiesEnd),
  '//__PRODUCTION_WINDOW_HELPERS__': radialHelpers+'\n'+windowHelpers,
  '//__PRODUCTION_UI_GLOBALS__': source.slice(uiStart, uiEnd) + '\n' + declaration('g_gamepadAxisNeutralAdmissionRequired'),
  '//__PRODUCTION_OUTPUT_GATES__': block('enum class FocusDecision', true) + '\n' + block('struct FocusPlan {', true) + '\n' + block('static bool msiUiBoundTargetAdmitted(') + '\n' + block('static const char* virtualUiBoundPersona(') + '\n' + outputGates + '\n' + block('static void virtualUiOutputNote(') + '\n' + block('static FocusPlan focusIsolationPlanTick(') + '\n' + block('static void dsuUiOutputNote('),
  '//__PRODUCTION_FRAME_ASSEMBLY__': block('static constexpr float inputHostNormalizeAxis(') + '\n' +
    block('struct FocusFrameContent {', true) + '\n' + block('static FocusFrameContent focusIsolationNeutralContent(') + '\n' +
    block('static json focusIsolationAssembleFrame('),
  '//__PRODUCTION_UI_PROCESS__': block('static void gamepadProcessUiInput('),
  '//__PRODUCTION_FINAL_OUTPUT_DECISION__': outputDecision,
  '//__PRODUCTION_EFFECTIVE_FRAME__': source.slice(effectiveStart, effectiveEnd),
  '//__PRODUCTION_DSU_NEUTRAL__': block('struct FocusNeutralDsu {', true) + '\n' + block('static FocusNeutralDsu focusIsolationNeutralDsu('),
  '//__PRODUCTION_DSU_DECISION__': outputDecision,
  '//__PRODUCTION_DSU_FEED__': block('    {\n        TickPreHostSubScope dsuScope('),
};
function generate(mutant = false) {
  let result = fixture;
  for (const [marker, production] of Object.entries(replacements)) {
    assert(result.includes(marker), 'Missing fixture insertion: ' + marker);
    let fragment = production;
    if (mutant === 'background-discovery-every-tick' && marker === '//__PRODUCTION_WINDOW_HELPERS__'){const from='const HWND child = foregroundClaimsParent ? customSteamLibraryWindow() : nullptr;';assert(fragment.includes(from),'Missing actual foreground precheck');fragment=fragment.replace(from,'const HWND child = customSteamLibraryWindow();');}
    if (mutant === 'dsu-native-bypass' && marker === '//__PRODUCTION_DSU_FEED__') fragment = fragment.replace('dsuFrame.semanticNeutral=dsuSubmitNeutral;', 'dsuFrame.semanticNeutral=submitNeutral;');
    if (mutant === 'background-window-owner' && marker === '//__PRODUCTION_WINDOW_HELPERS__') {
      const original=block('static bool customSteamLibraryInputSuppressed() {');
      const restored=original.replace('const HWND child = foregroundClaimsParent ? customSteamLibraryWindow() : nullptr;', 'const HWND child = customSteamLibraryWindow();');
      const mutated=restored.replace(/const bool owns = [\s\S]*?;/,'const bool owns = child && customSteamLibraryParentOwned(child);');
      assert.notEqual(mutated,original,'Background-owner negative control must replace the real admission expression');
      fragment=fragment.replace(original,mutated);
    }
    if (mutant === 'virtual-ui-disabled' && marker === '//__PRODUCTION_FINAL_OUTPUT_DECISION__') fragment = fragment.replace(' : virtualUiOwnsInput(focusPlan);', ' : false;');
    if (mutant === 'msi-disabled' && marker === '//__PRODUCTION_OUTPUT_GATES__') fragment = fragment.replace('return msiClawKnownBoardAuthority() &&', 'return false &&');
    if (mutant === 'msi-unscoped' && marker === '//__PRODUCTION_OUTPUT_GATES__') fragment = fragment.replace('return msiClawKnownBoardAuthority() &&', 'return true &&');
    if (mutant === true && marker === '//__PRODUCTION_OUTPUT_GATES__') fragment = fragment.replaceAll('return gamepadUiOutputIsolationEligible();', `if (g_customSteamLibraryInputPhase.load(std::memory_order_acquire) !=
        static_cast<int>(CustomSteamLibraryInputPhase::disabled)) return false;
    const HWND window = g_hwnd;
    return window && IsWindow(window) && IsWindowVisible(window) &&
        !IsIconic(window) && focusMainWindowIsForeground();`);
    result = result.replace(marker, fragment);
  }
  assert(!result.includes('//__PRODUCTION_'), 'Unresolved fixture insertion'); return result;
}
function run(name, mutant = false) {
  const cpp = path.join(root, name + '.cpp'), exe = path.join(root, name + '.exe');
  fs.writeFileSync(cpp, generate(mutant));
  const bat = path.join(root, 'compile-' + name + '.bat');
  fs.writeFileSync(bat, calibrationToolchainPreamble("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat") + `cl /nologo /std:c++20 /utf-8 /EHsc /MT /DNDEBUG /I"${path.join(repo,'deps/json')}" /I"${path.join(repo,'native')}" "${cpp}" "${path.join(repo,'native/dsu_server.cpp')}" /Fo"${root}/" /Fe"${exe}" /link ws2_32.lib\r\nexit /b %errorlevel%\r\n`);
  const compileStarted=Date.now();
  const compiled = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d','/c',bat], {cwd:root,windowsHide:true,encoding:'utf8'});
  fs.writeFileSync(path.join(root, name + '-compiler.log'), (compiled.stdout || '') + (compiled.stderr || ''));
  assert(compiled.status === 0, 'Fixture compile failed: ' + name + '\n' + compiled.stdout + compiled.stderr);
  const compileCompleted=Date.now(),executeStarted=Date.now();
  const executed = spawnSync(exe, selectedCases, {cwd:root,windowsHide:true,encoding:'utf8',timeout:30000});
  assert(!executed.error, 'Fixture execution failed: ' + executed.error);
  fs.writeFileSync(path.join(root,name+'-process.json'),JSON.stringify({variant:name,cwd:root,selectedCases,compile:{startedAt:compileStarted,completedAt:compileCompleted,elapsedMs:compileCompleted-compileStarted,pid:compiled.pid,status:compiled.status},execute:{startedAt:executeStarted,completedAt:Date.now(),elapsedMs:Date.now()-executeStarted,pid:executed.pid,status:executed.status,error:executed.error?.message??null,timeoutMs:30000}},null,2));
  const report = JSON.parse(executed.stdout);assert(!selectedCases.length||report.caseCount===selectedCases.length,'Missing selected original cases'); fs.writeFileSync(path.join(root,name+'.json'),JSON.stringify(report,null,2));
  return {report,status:executed.status};
}
const current = selectedVariants.includes('production') ? run('production') : null;
if(current) assert(current.status === 0 && current.report.allPassed, 'Production input regressions failed: ' + JSON.stringify(current.report.cases.filter(item=>!item.passed)));
const backgroundOwner = selectedVariants.includes('background-owner-original') ? run('background-owner-original', 'background-window-owner') : null;
if(backgroundOwner) assert(backgroundOwner.status !== 0 && backgroundOwner.report.cases.some(item=>item.name==='background-visible-child-never-suppresses-parent-shortcuts' && !item.passed), 'Tests missed background child suppression regression');
const originalBypass = selectedVariants.includes('original-child-bypass') ? run('original-child-bypass', true) : null;
if(originalBypass) assert(originalBypass.status !== 0 && originalBypass.report.failedCount > 0, 'Tests did not detect the original child bypass');
if(originalBypass) assert(originalBypass.report.cases.some(item=>item.name==='deck-parent-owned-library-foreground-neutral' && !item.passed), 'Original Deck regression was not detected');
const msiMissing = selectedVariants.includes('msi-original-bound-gap') ? run('msi-original-bound-gap', 'msi-disabled') : null;
if(msiMissing) assert(msiMissing.status !== 0 && msiMissing.report.cases.some(item=>item.name==='msi-elite-bound-foreground-neutral' && !item.passed), 'Tests missed original MSI bound-persona isolation gap');
const msiUnscoped = selectedVariants.includes('msi-family-gate-removed') ? run('msi-family-gate-removed', 'msi-unscoped') : null;
if(msiUnscoped) assert(msiUnscoped.status !== 0 && msiUnscoped.report.cases.some(item=>item.name==='other-family-elite-msi-policy-inactive' && !item.passed), 'Tests allowed MSI policy to leak into another family');
const virtualMissing = selectedVariants.includes('virtual-ui-original-gap') ? run('virtual-ui-original-gap', 'virtual-ui-disabled') : null;
if(virtualMissing) assert(virtualMissing.status !== 0 && virtualMissing.report.cases.some(item=>item.name==='virtual-elite-main-foreground-neutral' && !item.passed)
  && virtualMissing.report.cases.some(item=>item.name==='virtual-elite-child-foreground-neutral' && !item.passed), 'Tests missed current Elite UI output gaps');
const dsuMissing = selectedVariants.includes('dsu-native-original-bypass') ? run('dsu-native-original-bypass', 'dsu-native-bypass') : null;
if(dsuMissing) assert(dsuMissing.status !== 0 && dsuMissing.report.cases.some(item=>item.name==='dsu-native-main-foreground-neutral-wire' && !item.passed)
  && dsuMissing.report.cases.some(item=>item.name==='dsu-native-child-foreground-neutral-wire' && !item.passed), 'Tests missed native DSU foreground bypass');
const backgroundDiscovery=selectedVariants.includes('background-discovery-every-tick')?run('background-discovery-every-tick','background-discovery-every-tick'):null;
if(backgroundDiscovery)assert(backgroundDiscovery.status!==0&&backgroundDiscovery.report.cases.some(c=>c.name==='background-library-repeated-probes-do-zero-process-identity-work'&&!c.passed),'Repeated global discovery negative was not detected');
const sourceStable = preparationInputs.every(row=>createHash('sha256').update(fs.readFileSync(path.join(repo,row.relative))).digest('hex')===row.sha256 && createHash('sha256').update(fs.readFileSync(row.snapshot)).digest('hex')===row.sha256) && expanded.inputs.every(row => createHash('sha256').update(fs.readFileSync(row.file)).digest('hex') === row.sha256)
  && expanded.manifest.inputs.every(row => createHash('sha256').update(fs.readFileSync(row.snapshot)).digest('hex') === row.sha256)
  && fs.readFileSync(expansionHelper).equals(helperBytes)
  && createHash('sha256').update(fs.readFileSync(expanded.manifest.expandedSnapshot)).digest('hex') === expanded.manifest.expandedSha256;
assert(sourceStable, 'Actual main/runtime sources or their immutable snapshots changed during focus verification');
const fullOriginalMatrix=!selectedCases.length&&selectedVariants.length===originalVariants.length&&selectedVariants.every((v,i)=>v===originalVariants[i]);
const report = {...(current?.report??{allPassed:null,caseCount:0,cases:[]}),sourceStable,preparationInputs,sourceExpansion:expanded.manifest,sourceSha256:createHash('sha256').update(bytes).digest('hex'),
 selectedCases,selectedVariants,fullOriginalMatrix,nativeProductionExecuted:!!current,selectedValidationPassed:true,
 originalBypassDetected:originalBypass?true:null,originalBypassFailedCases:originalBypass?.report.failedCount??null,
 msiOriginalGapDetected:msiMissing?true:null,msiUnscopedMutationDetected:msiUnscoped?true:null,
 dsuOriginalBypassDetected:dsuMissing?true:null,dsuOriginalBypassFailedCases:dsuMissing?.report.failedCount??null,virtualOriginalGapDetected:virtualMissing?true:null,
 backgroundOwnerNegativeDetected:backgroundOwner?true:null,backgroundDiscoveryNegativeDetected:backgroundDiscovery?true:null,
 dsuSourceSha256:createHash('sha256').update(fs.readFileSync(path.join(repo,'native/dsu_server.cpp'))).digest('hex'),
 childStillUsesParentSemanticInput:fullOriginalMatrix?true:null,retiredPersonasRemainRejected:fullOriginalMatrix?true:null,
 retiredExpectationReason:'Legacy DS4 rows retain original retired-admission assertions; unselected originals are NOT_RUN',outputRoot:root};
fs.writeFileSync(path.join(root,'summary.json'),JSON.stringify(report,null,2));
console.log(fullOriginalMatrix ? `CUSTOM_STEAM_LIBRARY_VIRTUAL_INPUT_SELFTEST_OK (${report.caseCount} cases; original bypass fails ${report.originalBypassFailedCases})` : `CUSTOM_STEAM_LIBRARY_VIRTUAL_INPUT_SELECTED_SCOPE_OK (production=${current ? report.caseCount : 'NOT_RUN'}; variants=${selectedVariants.join(',')})`);
console.log('REPORT=' + path.join(root,'summary.json'));
