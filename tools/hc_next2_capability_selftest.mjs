import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {captureOemInputHeaders, oemInputBindingFunctions} from './lib/hc_oem_input_fixture.mjs';
// Offline production-function extraction: no Host, HID, hook installation or Steam APIs.
// node tools/hc_next2_capability_selftest.mjs --out <validation-directory>
// Release (/O2 /DNDEBUG) and Checked (/Od /RTC1 /D_ITERATOR_DEBUG_LEVEL=2)
// execute the same always-on checks. Generated files stay beneath --out.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
let workspace = repo;
while (!fs.existsSync(path.join(workspace, 'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))) {
  const parent = path.dirname(workspace);
  if (parent === workspace) throw Error('Cannot locate fixed HC research inputs above repository');
  workspace = parent;
}
const args = process.argv.slice(2), options = {};
for (let i = 0; i < args.length; ++i) {
  const key = args[i];
  if (key === '--generate-only') options.generateOnly = true;
  else if (key === '--mutation-checks') options.mutations = true;
  else if (['--out', '--source', '--before', '--vcvars'].includes(key) && args[i + 1]) options[key.slice(2)] = args[++i];
  else throw Error(`Unknown/incomplete argument: ${key}`);
}
const validation = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/NEXT2');
const out = path.resolve(options.out || path.join(validation, 'selftest'));
const sourcePath = path.resolve(options.source || path.join(repo, 'native/main.cpp'));
const beforePath = path.resolve(options.before || path.join(validation, 'main.before.cpp'));
if (!fs.existsSync(beforePath)) throw Error(`Frozen preimage is not ready: ${beforePath}; rerun after the main thread provides it`);
// Prevent an accidental --out from writing generated evidence into production sources.
const relativeOut = path.relative(repo, out);
if (relativeOut === '' || (!relativeOut.startsWith(`..${path.sep}`) && !path.isAbsolute(relativeOut)))
  throw Error('--out must be outside the source repository (use Build/Validation in the workspace)');
fs.mkdirSync(out, { recursive: true });
const sha = value => createHash('sha256').update(value).digest('hex');
const writeJson = (name, value) => fs.writeFileSync(path.join(out, name), JSON.stringify(value, null, 2) + '\n');
const sourceBytes = fs.readFileSync(sourcePath), beforeBytes = fs.readFileSync(beforePath);
const source = sourceBytes.toString('utf8'), before = beforeBytes.toString('utf8');
const oemHeaders = captureOemInputHeaders({repo, out, source});
const bindings = [];

// Lexical brace matching preserves complete production text, including nested
// branches/lambdas, and ignores braces inside comments and quoted literals.
function closingBrace(text, open) {
  let depth = 0, state = 'code';
  for (let i = open; i < text.length; ++i) {
    const c = text[i], next = text[i + 1];
    if (state === 'line') { if (c === '\n') state = 'code'; continue; }
    if (state === 'block') { if (c === '*' && next === '/') { state = 'code'; ++i; } continue; }
    if (state === 'string' || state === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (state === 'string' ? '"' : "'")) state = 'code';
      continue;
    }
    if (c === '/' && next === '/') { state = 'line'; ++i; }
    else if (c === '/' && next === '*') { state = 'block'; ++i; }
    else if (c === '"') state = 'string';
    else if (c === "'") state = 'char';
    else if (c === '{') ++depth;
    else if (c === '}' && --depth === 0) return i;
  }
  throw Error('Unbalanced production definition');
}
function extractor(text, label) {
  function bind(name, start, end, kind = 'compiled-definition') {
    const code = text.slice(start, end);
    bindings.push({ snapshot: label, name, kind, line: text.slice(0, start).split('\n').length,
      byteOffset: Buffer.byteLength(text.slice(0, start)), bytes: Buffer.byteLength(code), sha256: sha(code) });
    return code;
  }
  function definition(marker, name = marker) {
    let start = -1, open;
    do {
      start = text.indexOf(marker, start + 1);
      if (start < 0) throw Error(`${label}: missing definition ${marker}`);
      open = text.indexOf('{', start);
    } while (text.indexOf(';', start) < open); // Skip forward declarations.
    let end = closingBrace(text, open) + 1;
    if (text[end] === ';') ++end;
    return bind(name, start, end);
  }
  function fn(name, kind = 'compiled-definition') {
    const re = new RegExp(`^static [^\\n]*\\b${name}\\(`, 'gm');
    const matches = [...text.matchAll(re)];
    for (const m of matches) {
      const open = text.indexOf('{', m.index);
      if (text.indexOf(';', m.index) < open) continue;
      return bind(name, m.index, closingBrace(text, open) + 1, kind);
    }
    throw Error(`${label}: missing function ${name}`);
  }
  function global(name) {
    const re = new RegExp(`^static [^\\n;]*\\b${name}\\b[^;]*;`, 'm');
    const m = text.match(re);
    if (!m) throw Error(`${label}: missing state declaration ${name}`);
    return bind(name, m.index, m.index + m[0].length);
  }
  const parts = [fn('ascii_lower'), fn('trim_ascii'), definition('enum class YmccFamily : int {'),
    definition('struct MachineIdentity {'), fn('ymccDmiNormalize'), fn('ymccResolveFamily'),
    definition('enum OemVkBit : uint32_t {')];
  for (const m of text.matchAll(/^static constexpr uint64_t kOemBit[^;]+;/gm))
    parts.push(bind(m[0].match(/kOemBit\w+/)[0], m.index, m.index + m[0].length));
  parts.push(definition('struct OemChordRule {'), definition('struct OemChordProfile {'));
  for (const m of text.matchAll(/^static const OemChordRule (\w+)\[\] = \{/gm))
    parts.push(definition(m[0], m[1]));
  parts.push(definition('enum class OemChordProfileId : int {'), definition('static const OemChordProfile kOemProfiles[] = {'),
    fn('oemChordProfileFor'), fn('oemBackHookVkBit'), fn('oemChordEvalRules'), fn('oemChordProfileIdFor'),
    definition('enum class OemKeyForm : int {'), definition('struct OemKeyDef {'), definition('struct OemCatalogRef {'));
  for (const m of text.matchAll(/^static const OemKeyDef (\w+)\[\] = \{/gm)) parts.push(definition(m[0], m[1]));
  parts.push('template <size_t N>\n' + fn('oemCatalogOf'), fn('oemCatalogForFamily'),
    fn('nativeKeyboardShortcutMaskBit'), fn('ymccParseAxisToken'), fn('ymccSplitAxisRecipe'), fn('ymccApplyNamedImuMatrix'),
    fn('ymccDeviceClassLightingSet'), fn('ymccDeviceClassSupportsLighting'));
  if(text.includes('oemUnifiedInputBindingFor('))for(const name of oemInputBindingFunctions)parts.push(fn(name));
  if (label !== 'before') {
    for (const name of ['g_machineIdentity', 'g_oemBackProfileId', 'g_oemKbDown', 'g_oemKbDownRaw',
      'g_oemMouseLeft', 'g_oemKeyHooksActive', 'g_oemKeyMask', 'g_oemBackStateKb', 'g_gpdBackIfaceGen',
      'g_gpdBackIfaceReady', 'g_gpdChordSnapPhysical', 'g_gpdChordSnapBaseKey', 'g_gpdChordSnapPrefix',
      'g_gpdChordPubGen', 'g_oemRearMapToL5', 'g_oemRearMapToR5']) parts.push(global(name));
    parts.push(fn('oemBackHookRecompute'), fn('oemRearMapRefresh'), fn('gamepadOemPressedKeys'));
    const parent = fn('oemBackMappingReconcileOnUi', 'source-only-parent'); // Bind the complete parent, but do not execute Windows calls.
    const parentAt = text.indexOf(parent);
    const start = text.indexOf('g_oemKeyHooksActive.store(false, std::memory_order_release);', parentAt);
    const end = text.indexOf('oemKeyboardConsumeMaintain(', start);
    if (start < parentAt || end < start || end > parentAt + parent.length)
      throw Error('Production hook-disable publication prefix moved; review extraction boundary');
    const prefix = bind('oemBackMappingReconcileOnUi:disable-publication-prefix', start, end, 'compiled-production-fragment');
    if (!prefix.includes('g_oemKbDown = 0;') || !prefix.includes('oemBackHookRecompute();'))
      throw Error('Production disable prefix lacks clear/recompute');
    parts.push(`// Only this exact production prefix is executed; no dispatcher/hook/sleep API is substituted.\n` +
      `static void extractedDisablePublicationPrefix() {\n${prefix}\n}`);
  }
  return { code: parts.join('\n\n'), fn, definition };
}
const frozen = extractor(before, 'before');
const actual = extractor(source, 'current');

// Expectations come from fixed HC sources plus the explicitly agreed stable ID
// contract. Never derive NEXT2 chords, VKs, bits, signs or expected masks from
// the table/function under test.
const upstream = path.join(workspace, 'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream');
const hcCommit = 'a250b19a4b7e29dac3b383f2d1ac517d1af43e4e';
const pinned = [
  ['HandheldCompanion/Devices/AYANEO/AYANEONEXT2.cs', 'b47a90a52912eebd2568e920da7c1d0a264a286bb06657f9e274feb7f5da91c2'],
  ['HandheldCompanion/Resources/Devices/AYANEONEXT2.json', 'bbef5352a5fcc4857e3f86d44955dfcb4206d6e17335bd8262090e424b034f67'],
];
const manifestBytes = fs.readFileSync(path.join(upstream, 'source-manifest.json'));
const manifest = JSON.parse(manifestBytes);
const hcInputs = pinned.map(([relative, digest]) => {
  const file = path.join(upstream, 'sources/hc', relative), bytes = fs.readFileSync(file);
  const row = manifest.find(r => r.project === 'hc' && r.path === relative);
  if (!row || row.commit !== hcCommit || row.sha256.toLowerCase() !== digest || sha(bytes) !== digest)
    throw Error(`Fixed HC input/manifest hash mismatch: ${relative}`);
  return { path: file, relative, sha256: digest, text: bytes.toString('utf8') };
});
const stableIds = {
  'AYANEO Button': ['ckbig', 27], 'Custom Key Small': ['cksmall', 28], LC: ['cktl', 29], RC: ['cktr', 30],
  T: ['ayat', 31], LC1: ['m1', 20], LC2: ['l4', 33], RC1: ['m2', 21], RC2: ['r4', 32],
};
function hcVk(name) {
  if (/^F(?:[1-9]|1[0-9]|2[0-4])$/.test(name)) return 0x6f + Number(name.slice(1));
  if (name === 'LWin') return 0x5b;
  if (name === 'D') return 0x44;
  throw Error(`Unreviewed HC key token: ${name}`);
}
function expectedVkBit(vk) {
  const names = [0x11, 0x12, 0x10, 0x5b, 0x72, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e,
    0x7f, 0x80, 0x81, 0x84, 0x85, 0x86, 0x87, 0x4c, 0x52, 0x4f, 0x47, 0x54, 0x44,
    0x1b, 0x2e, 0x2c, 0x09, 0x82, 0x83];
  const i = names.indexOf(vk);
  if (i < 0) throw Error(`Missing agreed VK bit: ${vk}`);
  return 2 ** i;
}
const chordPattern = /OEMChords\.Add\(new KeyboardChord\("([^"]+)",\s*\[([^\]]+)\],\s*\[([^\]]+)\],\s*(true|false),\s*ButtonFlags\.(OEM\d+)\)\);/g;
const keys = [...hcInputs[0].text.matchAll(chordPattern)].map(m => {
  const parse = list => list.split(',').map(s => s.trim().replace(/^KeyCode\./, ''));
  const press = parse(m[2]), release = parse(m[3]), mapping = stableIds[m[1]];
  if (!mapping || JSON.stringify(press) !== JSON.stringify(release) || m[4] !== 'false')
    throw Error(`Unexpected fixed HC chord semantics: ${m[1]}`);
  const vks = press.map(hcVk);
  return { hcName: m[1], id: mapping[0], canonicalBit: (1n << BigInt(mapping[1])).toString(),
    press, release, vks, kbMask: vks.reduce((mask, vk) => mask + expectedVkBit(vk), 0),
    flag: m[5], backIndex: 0, clickOnly: false };
});
if (keys.length !== 9 || new Set(keys.map(k => k.id)).size !== 9 || !hcInputs[0].text.includes('OEMChords.Clear();'))
  throw Error('Fixed HC input does not contain the reviewed replacement nine-chord contract');
const imu = JSON.parse(hcInputs[1].text), axes = ['X', 'Y', 'Z'];
const samples = [[1, 2, 3], [1, 0, 0], [0, 1, 0], [0, 0, 1], [-7.25, 2.5, -0.125], [0, 0, 0],
  [1234.5, -678.25, 90.125], [1e-9, -1e-8, 1e-7]];
const matrices = ['GyroMatrix', 'AcceleroMatrix'].map((name, i) => {
  const matrix = imu[name], sensor = i === 0 ? 'gyro' : 'accel';
  // HC Axis gives output-coordinate signs; AxisSwap selects the source axis.
  const tokens = axes.map(axis => `${matrix.Axis[axis] < 0 ? 'neg-' : ''}${matrix.AxisSwap[axis].toLowerCase()}`);
  const recipe = tokens.join('-');
  if (recipe !== 'neg-x-neg-z-y') throw Error(`Unexpected fixed ${name} recipe: ${recipe}`);
  return { sensor, id: `hc-a250b19a:ayanero-next2:${sensor}:${recipe}`, matrix,
    samples: samples.map(input => ({ input, output: axes.map(axis => matrix.Axis[axis] * input[axes.indexOf(matrix.AxisSwap[axis])]) })) };
});
const profileEnum = frozen.definition('enum class OemChordProfileId : int {');
const legacyProfiles = profileEnum.slice(profileEnum.indexOf('{') + 1, profileEnum.lastIndexOf('}'))
  .replace(/\/\/[^\n]*/g, '').split(',').map(s => s.trim().split('=')[0].trim()).filter(n => n && n !== 'None');
const canonicalPattern = /\{"([^"]+)",/g;
const canonicalNames = [...frozen.fn('nativeKeyboardShortcutMaskBit').matchAll(canonicalPattern)].map(m => m[1]);
const currentCanonical = [...actual.fn('nativeKeyboardShortcutMaskBit').matchAll(canonicalPattern)].map(m => m[1]);
if (JSON.stringify(canonicalNames) !== JSON.stringify(currentCanonical)) throw Error('Stable canonical name list changed from preimage');
const expectations = { hcCommit, keys, matrices, canonicalNames,
  manufacturers: ['AYANEO', 'AYADEVICE', 'AYA'], models: ['NEXT II', 'NEXT 2', 'AYANEO NEXT II', 'AYANEO NEXT 2', 'AB09'],
  inputHidKey: 'ayanero-next2:keyboard-oem', deviceClass: 'HandheldCompanion.Devices.AYANEONEXT2' };
writeJson('expectations.json', expectations);
fs.writeFileSync(path.join(out, 'main.current.cpp'), sourceBytes);
const fixturePath = path.join(repo, 'tools/fixtures/hc_next2_capability/harness.cpp');
const fixture = fs.readFileSync(fixturePath, 'utf8');
const jsonPath = path.join(repo, 'deps/json/json.hpp');
const cpp = oemHeaders.preamble + `// Generated from exact production definitions. See source-bindings.json.\n` +
  '#define NOMINMAX\n#define WIN32_LEAN_AND_MEAN\n#include <windows.h>\n#include <xinput.h>\n' +
  '#include <algorithm>\n#include <array>\n#include <atomic>\n#include <cctype>\n#include <cmath>\n#include <cstdint>\n#include <fstream>\n#include <iostream>\n#include <map>\n#include <set>\n#include <stdexcept>\n#include <string>\n#include <utility>\n#include <vector>\n' +
  '#include "json.hpp"\nusing json = nlohmann::json;\n' +
  `namespace before {\n${frozen.code}\n}\nnamespace current {\n${actual.code}\n}\n` +
  'struct LegacyProfilePair { const char* name; int oldId, newId; };\nstatic const LegacyProfilePair legacyProfiles[] = {\n' +
  legacyProfiles.map(n => `{"${n}", static_cast<int>(before::OemChordProfileId::${n}), static_cast<int>(current::OemChordProfileId::${n})},`).join('\n') +
  '\n};\n' + fixture;
const cppPath = path.join(out, 'hc_next2_extracted.cpp');
fs.writeFileSync(cppPath, cpp);
const evidence = { schema: 'hc-next2-offline-production-v1', startedAt: new Date().toISOString(), source: { path: sourcePath, sha256: sha(sourceBytes) },
  before: { path: beforePath, sha256: sha(beforeBytes) }, hcCommit, upstreamManifestSha256: sha(manifestBytes),
  oemInputHeaders: oemHeaders.inputs, hcInputs: hcInputs.map(({ text, ...r }) => r), jsonHeader: { path: jsonPath, sha256: sha(fs.readFileSync(jsonPath)) },
  runner: { path: fileURLToPath(import.meta.url), sha256: sha(fs.readFileSync(fileURLToPath(import.meta.url))) },
  fixture: { path: fixturePath, sha256: sha(fixture) }, generatedCppSha256: sha(cpp), expectationsSha256: sha(fs.readFileSync(path.join(out, 'expectations.json'))), bindings,
  boundaries: [
    'Exact extracted production functions and the hook-disable publication prefix run in a standalone process.',
    'The full hook reconciliation function is hash-bound only; Windows dispatch, hook installation and replay are not executed.',
    'Profile-generation and sleep-like sequences are explicit in-memory simulations, not Windows sleep/resume acceptance.',
    'No Host/HID/Steam process, device I/O, injected key event, firmware write or physical NEXT2 validation occurs.',
    'Lighting is checked through the production gate; fan/lighting hardware routes are not exercised or advertised as supported.',
  ], configurations: [], status: options.generateOnly ? 'generated-only-not-validated' : 'running' };
writeJson('source-bindings.json', evidence);
function run(command, argv, logName, cwd = out) {
  const result = spawnSync(command, argv, { cwd, encoding: 'utf8', windowsHide: true, timeout: 180000, maxBuffer: 16 * 1024 * 1024 });
  fs.writeFileSync(path.join(out, logName), (result.stdout || '') + (result.stderr || '') + (result.error ? `\n${result.error.message}` : ''));
  if (result.error || result.status !== 0) throw Error(`${logName}: ${result.error?.message || `exit ${result.status}`} (see ${path.join(out, logName)})`);
  return result.stdout;
}
try {
  if (!options.generateOnly) {
    if (process.platform !== 'win32') throw Error('This runner requires the Windows MSVC toolchain; generated harness is offline');
    let vcvars = options.vcvars;
    if (!vcvars) {
      const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe');
      const install = run(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], 'toolchain-discovery.log').trim();
      vcvars = path.join(install, 'VC/Auxiliary/Build/vcvars64.bat');
    }
    if (!fs.existsSync(vcvars)) throw Error(`MSVC environment script is unavailable: ${vcvars}`);
    const envCmd = path.join(out, 'compiler-environment.cmd');
    // Only fixed tool commands and a validated local batch path; no user text is interpolated as shell code.
    if (/[\r\n"%]/.test(vcvars)) throw Error('Unsupported characters in vcvars path');
    fs.writeFileSync(envCmd, calibrationToolchainPreamble(vcvars) + 'set PATH\r\nset INCLUDE\r\nset LIB\r\nset VCTools\r\nset WindowsSdk\r\nexit /b 0\r\n');
    const envResult = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/c', envCmd], { encoding: 'utf8', windowsHide: true, timeout: 30000 });
    if (envResult.error || envResult.status !== 0) throw Error('MSVC environment setup failed');
    const envText = envResult.stdout;
    const compilerEnv = { ...process.env };
    for (const line of envText.split(/\r?\n/)) { const at = line.indexOf('='); if (at > 0) { const key = line.slice(0, at); const existing = Object.keys(compilerEnv).find(k => k.toLowerCase() === key.toLowerCase()); compilerEnv[existing || key] = line.slice(at + 1); } }
    // Environment evidence contains paths only, not arbitrary inherited secrets.
    fs.writeFileSync(path.join(out, 'compiler-environment.log'), `vcvars=${vcvars}\nVCToolsVersion=${compilerEnv.VCToolsVersion}\nWindowsSDKVersion=${compilerEnv.WindowsSDKVersion}\n`);
    for (const [name, flags] of [['Release', ['/O2', '/DNDEBUG', '/MD']], ['Checked', ['/Od', '/RTC1', '/Zi', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MDd']]]) {
      const dir = path.join(out, name); fs.mkdirSync(dir, { recursive: true });
      const exe = path.join(dir, 'hc_next2_capability.exe');
      fs.rmSync(path.join(dir, 'run.log'), { force: true }); // Stale output can never count as a fresh semantic result.
      const command = ['cl.exe', '/nologo', '/std:c++20', '/EHsc', '/utf-8', '/W4', '/permissive-', ...flags,
        `/I${path.dirname(jsonPath)}`, cppPath, `/Fo${path.join(dir, 'harness.obj')}`, `/Fd${path.join(dir, 'harness.pdb')}`, `/Fe${exe}`, '/link', '/INCREMENTAL:NO'];
      const result = spawnSync(command[0], command.slice(1), { cwd: dir, env: compilerEnv, encoding: 'utf8', windowsHide: true, timeout: 180000, maxBuffer: 16 * 1024 * 1024 });
      fs.writeFileSync(path.join(dir, 'compile.log'), (result.stdout || '') + (result.stderr || '') + (result.error?.message || ''));
      if (result.error || result.status !== 0) throw Error(`${name} compilation failed; see ${path.join(dir, 'compile.log')}`);
      const output = run(exe, [path.join(out, 'expectations.json')], `${name}/run.log`, dir);
      const report = JSON.parse(output.trim());
      if (report.status !== 'pass') throw Error(`${name}: harness did not pass`);
      evidence.configurations.push({ name, flags, compileCommand: command, executableSha256: sha(fs.readFileSync(exe)), ...report });
      console.log(`${name}: PASS (${report.checks} checks)`);
    }
    if (options.mutations) {
      // Each deliberate fault is confined to an output snapshot. A negative
      // control counts only if compilation succeeds and the expected semantic
      // assertion fails; extraction/compiler failures do not count as detection.
      const cases = [
        ['reviewed-profile-tail-swapped', 'Ayaneo3, OneXX2MiniPro,', 'OneXX2MiniPro, Ayaneo3,', 'frozen-legacy-profile-numbers-tables-and-evaluator-parity'],
        ['wrong-F19-VK', 'case 0x82: return kVkF19;', 'case 0x82: return kVkF20;', 'VK-to-mask-and-edge-events'],
        ['LC1-canonical-collision', '{ kVkF19, kOemBitM1, 0, 0, false },         // LC1', '{ kVkF19, kOemBitM2, 0, 0, false },         // LC1', 'independent-HC-nine-key-contract-and-canonical-IDs'],
        ['wrong-IMU-output-sign', 'oy = sign[1] * in[axis[1]];', 'oy = -sign[1] * in[axis[1]];', 'independent-HC-IMU-recipes-and-production-parser'],
        ['stale-mask-after-disable', 'g_oemKbDown = 0; g_oemMouseLeft = false; g_oemMouseX2 = false;',
          'g_oemKbDown = 0; g_oemMouseLeft = false; g_oemMouseX2 = false; g_oemKbDown = kVkF19;',
          'production-disable-prefix-and-simulated-profile-sleep-sequences'],
      ];
      const outcomes = await Promise.allSettled(cases.map(async ([name, needle, replacement, expectedSection]) => {
        if (source.split(needle).length !== 2) throw Error(name + ': mutation anchor is not unique');
        const dir = path.join(out, 'mutations', name); fs.mkdirSync(dir, { recursive: true });
        const mutantPath = path.join(dir, 'main.mutant.cpp'), childOut = path.join(dir, 'validation');
        const mutant = source.replace(needle, replacement); fs.writeFileSync(mutantPath, mutant);
        const argv = [fileURLToPath(import.meta.url), '--source', mutantPath, '--before', beforePath, '--out', childOut, '--vcvars', vcvars];
        const exitCode = await new Promise((resolve, reject) => {
          const child = spawn(process.execPath, argv, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
          let log = ''; child.stdout.on('data', chunk => { log += chunk; }); child.stderr.on('data', chunk => { log += chunk; });
          child.once('error', reject); child.once('close', code => { fs.writeFileSync(path.join(dir, 'runner.log'), log); resolve(code); });
        });
        const runLog = path.join(childOut, 'Release/run.log');
        if (exitCode !== 1 || !fs.existsSync(runLog)) throw Error(name + ': expected a compiled semantic failure');
        const failure = JSON.parse(fs.readFileSync(runLog, 'utf8').trim());
        if (failure.status !== 'fail' || failure.section !== expectedSection) throw Error(name + ': wrong failure boundary');
        return { name, expectedSection, exitCode, mutantSourceSha256: sha(mutant), failure };
      }));
      evidence.mutations = outcomes.map((r, i) => r.status === 'fulfilled' ? r.value : { name: cases[i][0], error: r.reason.message });
      if (outcomes.some(r => r.status === 'rejected')) throw Error('A mutation negative control was not rejected at its semantic boundary');
      console.log('Mutation controls: '+cases.length+'/'+cases.length+' rejected by semantic checks');
    }
    const completionBytes = fs.readFileSync(sourcePath);
    evidence.wholeFileChanged = sha(completionBytes) !== evidence.source.sha256;
    evidence.currentAtCompletionSha256 = sha(completionBytes);
    const bindingMap = label => Object.fromEntries(bindings.filter(b => b.snapshot === label).map(b => [b.name + ':' + b.kind, b.sha256]));
    const originalBindings = bindingMap('current');
    const completion = extractor(completionBytes.toString('utf8'), 'current-at-completion');
    const latestBindings = bindingMap('current-at-completion');
    evidence.relevantDefinitionsUnchanged = actual.code === completion.code && Object.entries(originalBindings).every(([name, digest]) => latestBindings[name] === digest);
    evidence.beforeUnchangedAtCompletion = sha(fs.readFileSync(beforePath)) === evidence.before.sha256;
    if (!oemHeaders.unchanged() || !evidence.relevantDefinitionsUnchanged || !evidence.beforeUnchangedAtCompletion)
      throw Error('Relevant production definitions/preimage changed during validation; rerun against the latest snapshot');
    evidence.status = 'pass';
  }
} catch (error) {
  evidence.status = 'fail'; evidence.error = error.message;
  console.error(error.message); process.exitCode = 1;
} finally {
  evidence.completedAt = new Date().toISOString();
  writeJson('source-bindings.json', evidence);
}
console.log(`Evidence: ${path.join(out, 'source-bindings.json')}`);
