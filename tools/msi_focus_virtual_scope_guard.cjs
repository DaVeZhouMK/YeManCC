'use strict';
// Offline source-scope guard only. No product, process, device, registry,
// network, timer, deployment, or third-party code execution.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const assert = require('node:assert/strict');
const {readVirtualControllerSource} = require('./virtual_controller_runtime_source.mjs');
const protectedFunctions = Object.freeze([
  'focusMainWindowIsForeground',
  'gamepadUiInputEligible',
  'gamepadEmitUiAction',
  'customSteamLibraryInputSuppressed',
  'customSteamLibraryChildForeground',
  'hidHidePhysicalTargets',
  'hidHideGetPhysicalIdentities',
  'hidHideApplyPhysicalTransaction',
  'hidHideRefreshPhysicalHide',
  'hidHideRestorePhysicalTransaction',
  'hidHideReapplyReconnectedSource',
  'hidHideHoldPhysicalTransaction',
  'inputHostProcessLivenessTry',
  'inputHostQueueLifecycleWork',
  'inputHostRunQueuedLifecycleWork',
  'inputHostStopLocked',
  'inputHostStart',
  'msiUiOwnsInput',
  'msiUiOutputNote'
]);
const requiredUiEvents = Object.freeze(['WM_SHOWWINDOW', 'WM_ACTIVATE', 'WM_SIZE']);
const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');

// Preserve literals verbatim; ignore only whitespace and actual C++ comments.
// Braces or comment-looking text inside literals must never affect extraction.
function tokens(source) {
  const result = [];
  for (let i = 0; i < source.length;) {
    if (/\s/.test(source[i])) { ++i; continue; }
    if (source.startsWith('//', i)) {
      let end = source.indexOf('\n', i + 2);
      // C++ line splicing extends // comments over escaped newlines. Ignoring
      // this would allow a comment-only-looking edit to disable a real gate.
      while (end >= 0) {
        const previous = source[end - 1] === '\r' ? end - 2 : end - 1;
        if (source[previous] !== '\\') break;
        end = source.indexOf('\n', end + 1);
      }
      i = end < 0 ? source.length : end + 1; continue;
    }
    if (source.startsWith('/*', i)) {
      const end = source.indexOf('*/', i + 2);
      if (end < 0) throw Error('Unterminated C++ comment');
      i = end + 2; continue;
    }
    const start = i;
    const raw = /^(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(/.exec(source.slice(i));
    if (raw) {
      const terminator = ')' + raw[1] + '"';
      const end = source.indexOf(terminator, i + raw[0].length);
      if (end < 0) throw Error('Unterminated C++ raw string');
      i = end + terminator.length;
    } else {
      const literal = /^(?:u8|u|U|L)?(["'])/.exec(source.slice(i));
      if (literal) {
        const quote = literal[1]; i += literal[0].length;
        let closed = false;
        while (i < source.length) {
          if (source[i] === '\\') { i += 2; continue; }
          if (source[i++] === quote) { closed = true; break; }
        }
        if (!closed) throw Error('Unterminated C++ literal');
      } else {
        const word = /^(?:[A-Za-z_][A-Za-z_0-9]*|[0-9][A-Za-z_0-9.']*|::|->\*|->|\.\.\.|<<=|>>=|<<|>>|\+\+|--|&&|\|\||==|!=|<=|>=|\+=|-=|\*=|\/=|%=|&=|\|=|\^=|##)/.exec(source.slice(i));
        i += word ? word[0].length : 1;
      }
    }
    result.push({ text: source.slice(start, i), start, end: i });
  }
  return result;
}
function matching(ts, begin, open, close) {
  let depth = 0;
  for (let i = begin; i < ts.length; ++i) {
    if (ts[i].text === open) ++depth;
    else if (ts[i].text === close && --depth === 0) return i;
  }
  throw Error('Unbalanced ' + open + ' at token ' + begin);
}
function extract(ts, name) {
  const definitions = [];
  for (let i = 0; i < ts.length - 1; ++i) {
    if (ts[i].text !== name || ts[i + 1].text !== '(') continue;
    const endArgs = matching(ts, i + 1, '(', ')');
    // Guarded production helpers are static, with an ordinary body directly
    // after their signature. Declarations and call sites are not definitions.
    if (ts[endArgs + 1]?.text !== '{') continue;
    let start = i - 1;
    while (start >= 0 && ![';', '{', '}', '#'].includes(ts[start].text)) {
      if (ts[start].text === 'static') break;
      --start;
    }
    if (start < 0 || ts[start].text !== 'static') continue;
    const end = matching(ts, endArgs + 1, '{', '}');
    definitions.push({ name, start: ts[start].start, end: ts[end].end,
      signatureAndBody: ts.slice(start, end + 1).map(t => t.text) });
  }
  if (definitions.length !== 1) throw Error(name + ': expected one static definition, found ' + definitions.length);
  return definitions[0];
}
function compareSources(baseline, candidate, names = protectedFunctions) {
  const left = tokens(baseline), right = tokens(candidate);
  return names.map(name => {
    const a = extract(left, name), b = extract(right, name);
    const baselineTokensSha256 = sha256(JSON.stringify(a.signatureAndBody));
    const candidateTokensSha256 = sha256(JSON.stringify(b.signatureAndBody));
    return { name, unchanged: baselineTokensSha256 === candidateTokensSha256,
      baselineTokensSha256, candidateTokensSha256,
      baselineLine: baseline.slice(0, a.start).split('\n').length,
      candidateLine: candidate.slice(0, b.start).split('\n').length };
  });
}
function uiBindings(source) {
  const all = tokens(source);
  const window = extract(all, 'WndProc');
  const ts = all.filter(t => t.start >= window.start && t.end <= window.end).map(t => t.text);
  return requiredUiEvents.map(event => {
    const expected = ['case', event, ':', 'msiNativeForegroundUiEvent', '(', ')', ';'];
    const count = ts.filter((_, i) => expected.every((token, j) => ts[i + j] === token)).length;
    return { event, present: count === 1, count, meaning: 'One direct native-isolation hook at the start of this root WndProc case; not a runtime delivery proof' };
  });
}
function analyze(baselineFile, candidateFile, expectedBaselineSha256) {
  if (!/^[0-9a-f]{64}$/i.test(expectedBaselineSha256 || '')) throw Error('Full approved baseline SHA256 is required');
  const baseline = fs.readFileSync(baselineFile);
  if (sha256(baseline) !== expectedBaselineSha256.toLowerCase()) throw Error('Approved baseline identity mismatch; do not silently rebase the guard');
  const baselineExpansion = readVirtualControllerSource(baselineFile, {readFile: file => path.resolve(file) === path.resolve(baselineFile) ? baseline : fs.readFileSync(file)});
  const candidateExpansion = readVirtualControllerSource(candidateFile);
  const candidate = candidateExpansion.inputs[0].data;
  const checks = compareSources(baselineExpansion.source, candidateExpansion.source);
  const baselineUiBindings = uiBindings(baselineExpansion.source);
  if (baselineUiBindings.some(b => !b.present)) throw Error('Approved baseline lacks an expected UI binding');
  const candidateUiBindings = uiBindings(candidateExpansion.source);
  return { schemaVersion: 1, generatedAt: new Date().toISOString(),
    mode: 'read-only-offline-source-scope-check',
    passed: checks.every(c => c.unchanged) && candidateUiBindings.every(b => b.present),
    baseline: { file: path.resolve(baselineFile), sha256: sha256(baseline), sourceExpansion: baselineExpansion.manifest },
    candidate: { file: path.resolve(candidateFile), sha256: sha256(candidate), sourceExpansion: candidateExpansion.manifest }, checks, candidateUiBindings,
    coverage: 'Exact token identity of the named foreground/HidHide/lifecycle/liveness helpers plus UI hook presence only; whitespace/comments excluded, literals preserved',
    notProven: ['Whole-program call-site or private-MSI invariance', 'Compiled profile or shipped binary identity', 'Target hardware, UI rendering, Steam button delivery, wake recovery'],
    realHardwareAccess: false, productLaunched: false, productCodeEdited: false };
}
function selfTest() {
  let n = 0;
  const check = fn => { fn(); ++n; };
  const same = (a, b) => compareSources(a, b, ['f'])[0].unchanged;
  const basic = 'static bool f() { return false; }';
  check(() => assert.equal(same(basic, basic), true));
  check(() => assert.equal(same(basic, 'static /* } f() { */ bool f ( ) {\n return false; // {\n}'), true));
  check(() => assert.equal(same(basic, basic.replace('false', 'true')), false));
  check(() => assert.equal(same(basic, basic.replace('bool', 'int')), false));
  check(() => assert.equal(same(basic, 'static bool f(int x) { return false; }'), false));
  check(() => assert.equal(same('static bool f();\n' + basic, basic), true));
  check(() => assert.equal(same('void x(){ f(); }\n' + basic, basic), true));
  check(() => assert.equal(same('static auto f(){return "} // /*";}', 'static auto f() { return "} // /*"; }'), true));
  check(() => assert.equal(same('static auto f(){return "old";}', 'static auto f(){return "new";}'), false));
  check(() => assert.equal(same('static auto f(){return R"tag( } // /* )tag";}', 'static auto f() {return R"tag( } // /* )tag";}'), true));
  check(() => assert.equal(same('static auto f(){return L\'}\';}', 'static auto f() {return L\'}\';}'), true));
  check(() => assert.equal(same("static auto f(){return 1'000;}", "static auto f() {return 1'000;}"), true));
  check(() => assert.throws(() => compareSources(basic, 'static bool other(){return false;}', ['f']), /found 0/));
  check(() => assert.throws(() => compareSources(basic, basic + basic, ['f']), /found 2/));
  check(() => assert.equal(same('static bool f(){\nreturn false;\n}', 'static bool f(){//\\\nreturn false;\n}'), false));
  check(() => assert.equal(same(basic, 'static bool f(){//\\\r\nreturn true;\nreturn false;}'), true));
  check(() => assert.throws(() => tokens('/* never closed'), /Unterminated/));
  check(() => assert.throws(() => tokens('R"x( never closed'), /Unterminated/));
  check(() => assert.equal(uiBindings('static void WndProc(){switch(m){case WM_SIZE: msiNativeForegroundUiEvent();}}')[2].present, true));
  check(() => assert.equal(uiBindings('static void WndProc(){switch(m){case WM_SIZE: /* msiNativeForegroundUiEvent(); */}}')[2].present, false));
  check(() => assert.equal(uiBindings('static void WndProc(){const auto s = \"case WM_SIZE: msiNativeForegroundUiEvent();\";}')[2].present, false));
  check(() => assert.equal(uiBindings('static void ChildWndProc(){switch(m){case WM_SIZE: msiNativeForegroundUiEvent();}} static void WndProc(){switch(m){case WM_SIZE: break;}}')[2].present, false));
  check(() => assert.throws(() => analyze(__filename, __filename, '0'.repeat(64)), /identity mismatch/));
  console.log('MSI_FOCUS_VIRTUAL_SCOPE_SELFTEST cases=' + n + ' failures=0');
  return { cases: n, failures: 0 };
}
if (require.main === module) {
  try {
    const args = process.argv.slice(2);
    if (args.length === 1 && args[0] === '--self-test') selfTest();
    else {
      const options = {};
      while (args.length) {
        const key = args.shift(), value = args.shift();
        if (!['--baseline', '--candidate', '--baseline-sha256', '--output'].includes(key) || !value || options[key]) throw Error('Invalid or duplicate argument ' + key);
        options[key] = value;
      }
      if (!options['--baseline'] || !options['--candidate']) throw Error('Usage: node msi_focus_virtual_scope_guard.cjs --baseline FILE --baseline-sha256 SHA256 --candidate FILE [--output JSON]');
      const report = analyze(options['--baseline'], options['--candidate'], options['--baseline-sha256']);
      if (options['--output']) fs.writeFileSync(options['--output'], JSON.stringify(report, null, 2) + '\n');
      console.log(JSON.stringify({ passed: report.passed, count: report.checks.length, changed: report.checks.filter(c => !c.unchanged).map(c => c.name), missingUiBindings: report.candidateUiBindings.filter(b => !b.present).map(b => b.event), coverage: report.coverage, output: options['--output'] || null }, null, 2));
      if (!report.passed) process.exitCode = 1;
    }
  } catch (e) { console.error(String(e)); process.exitCode = 1; }
}
module.exports = { tokens, extract, compareSources, uiBindings, analyze, selfTest, protectedFunctions, requiredUiEvents };
