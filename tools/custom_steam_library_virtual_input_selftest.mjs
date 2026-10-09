import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const build = path.resolve(repo, '../../Build/CustomSteamLibrary');
const root = path.resolve(process.argv[2] || path.join(build, 'virtual-input-' + Date.now()));
assert(root.startsWith(build + path.sep) && !fs.existsSync(root), 'Use a fresh isolated Build directory');
fs.mkdirSync(root, { recursive: true });
const mainFile = path.join(repo, 'native/main.cpp');
const bytes = fs.readFileSync(mainFile);
const source = bytes.toString('utf8').replaceAll('\r\n', '\n');
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
const outputGates = [block('static bool steamDeckUiOwnsInput('), block('static bool ps5UiOwnsInput(')].join('\n');
assert(outputGates.match(/return gamepadUiInputEligible\(\);/g)?.length === 2,
  'Deck/PS5 output must reuse authoritative main-program admission, not a second policy');
assert(!outputGates.includes('CustomSteamLibraryInputPhase'), 'Child phase must not bypass shared output admission');
const child = fs.readFileSync(path.join(repo, 'native/custom-steam-library/workspace_host.cpp'), 'utf8');
assert(child.includes('if (parentInputModeEnabled()) {') && child.includes('g_launchOptions.inputOwner == L"host" && !g_updateHealthOnly'),
  'Parent integration must not start a second child XInput poller');
const propertiesStart = source.indexOf('static constexpr wchar_t kCustomSteamLibraryInputOwnerProperty[]');
const propertiesEnd = source.indexOf('static ULONG_PTR customSteamLibraryWindowProperty(', propertiesStart);
const windowHelpers = [
  'static bool focusMainWindowIsForeground(', 'static HWND customSteamLibraryWindow(',
  'static ULONG_PTR customSteamLibraryWindowProperty(', 'static bool customSteamLibraryProcessMatches(',
  'static bool customSteamLibraryParentOwned(', 'static HWND customSteamLibraryParentWindow(',
  'static bool customSteamLibraryInputSuppressed(', 'static bool customSteamLibraryChildForeground(',
  'static bool gamepadUiInputEligible() {', 'static const char* customSteamLibrarySemanticAction(',
].map(marker => block(marker)).join('\n');
const uiStart = source.indexOf('static WORD g_uiShoulderPending = 0;');
const uiEnd = source.indexOf('static constexpr SHORT GP_UI_THUMB_THRESHOLD = 16000;', uiStart)
  + 'static constexpr SHORT GP_UI_THUMB_THRESHOLD = 16000;'.length;
assert(uiStart >= 0 && uiEnd > uiStart);
const outputDecision = ['steamDeckUiNeutral', 'ps5UiNeutral', 'submitNeutral'].map(name => {
  const line = source.split('\n').find(line => line.trimStart().startsWith('const bool ' + name + ' ='));
  assert(line, 'Missing final-output decision: ' + name); return line;
}).join('\n');
const effectiveStart = source.indexOf('    FocusFrameContent effectiveContent =');
const effectiveEnd = source.indexOf('    // B1：hash', effectiveStart);
assert(effectiveStart >= 0 && effectiveEnd > effectiveStart);
const fixture = fs.readFileSync(path.join(repo, 'tools/custom_steam_library_virtual_input_fixture.cpp'), 'utf8');
const replacements = {
  '//__PRODUCTION_OWNER_DECLARATIONS__': block('enum class CustomSteamLibraryInputPhase', true) + '\n' +
    declaration('g_customSteamLibraryInputPhase') + '\n' + declaration('g_customSteamLibraryInputDeadline') + '\n' +
    source.slice(propertiesStart, propertiesEnd),
  '//__PRODUCTION_WINDOW_HELPERS__': windowHelpers,
  '//__PRODUCTION_UI_GLOBALS__': source.slice(uiStart, uiEnd) + '\n' + declaration('g_gamepadAxisNeutralAdmissionRequired'),
  '//__PRODUCTION_OUTPUT_GATES__': block('enum class FocusDecision', true) + '\n' + block('struct FocusPlan {', true) + '\n' + outputGates,
  '//__PRODUCTION_FRAME_ASSEMBLY__': block('static constexpr float inputHostNormalizeAxis(') + '\n' +
    block('struct FocusFrameContent {', true) + '\n' + block('static FocusFrameContent focusIsolationNeutralContent(') + '\n' +
    block('static json focusIsolationAssembleFrame('),
  '//__PRODUCTION_UI_PROCESS__': block('static void gamepadProcessUiInput('),
  '//__PRODUCTION_FINAL_OUTPUT_DECISION__': outputDecision,
  '//__PRODUCTION_EFFECTIVE_FRAME__': source.slice(effectiveStart, effectiveEnd),
};
function generate(mutant = false) {
  let result = fixture;
  for (const [marker, production] of Object.entries(replacements)) {
    assert(result.includes(marker), 'Missing fixture insertion: ' + marker);
    let fragment = production;
    if (mutant && marker === '//__PRODUCTION_OUTPUT_GATES__') fragment = fragment.replaceAll('return gamepadUiInputEligible();', `if (g_customSteamLibraryInputPhase.load(std::memory_order_acquire) !=
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
  fs.writeFileSync(bat, `@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /DNDEBUG /I"${path.join(repo,'deps/json')}" "${cpp}" /Fo"${path.join(root,name+'.obj')}" /Fe"${exe}"\r\nexit /b %errorlevel%\r\n`);
  const compiled = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d','/c',bat], {cwd:root,windowsHide:true,encoding:'utf8'});
  fs.writeFileSync(path.join(root, name + '-compiler.log'), (compiled.stdout || '') + (compiled.stderr || ''));
  assert(compiled.status === 0, 'Fixture compile failed: ' + name + '\n' + compiled.stdout + compiled.stderr);
  const executed = spawnSync(exe, [], {cwd:root,windowsHide:true,encoding:'utf8',timeout:30000});
  assert(!executed.error, 'Fixture execution failed: ' + executed.error);
  const report = JSON.parse(executed.stdout); fs.writeFileSync(path.join(root,name+'.json'),JSON.stringify(report,null,2));
  return {report,status:executed.status};
}
const current = run('production');
assert(current.status === 0 && current.report.allPassed, 'Production input regressions failed: ' + JSON.stringify(current.report.cases.filter(item=>!item.passed)));
const originalBypass = run('original-child-bypass', true);
assert(originalBypass.status !== 0 && originalBypass.report.failedCount > 0, 'Tests did not detect the original child bypass');
assert(originalBypass.report.cases.some(item=>item.name==='deck-parent-owned-library-foreground-neutral' && !item.passed), 'Original Deck regression was not detected');
const report = {...current.report, sourceSha256:createHash('sha256').update(bytes).digest('hex'),
  originalBypassDetected:true, originalBypassFailedCases:originalBypass.report.failedCount,
  childStillUsesParentSemanticInput:true, outputRoot:root};
fs.writeFileSync(path.join(root,'summary.json'),JSON.stringify(report,null,2));
console.log(`CUSTOM_STEAM_LIBRARY_VIRTUAL_INPUT_SELFTEST_OK (${report.caseCount} cases; original bypass fails ${report.originalBypassFailedCases})`);
console.log('REPORT=' + path.join(root,'summary.json'));
