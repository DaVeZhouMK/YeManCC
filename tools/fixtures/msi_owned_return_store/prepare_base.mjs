import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';

// Original preparation and extraction, shared by the legacy and explicit real-store modes.
export function prepareOwnedReturnSources({repo,output,selectedProfiles=['Release','Checked'],runName}={}) {
output = path.resolve(output);
assert(output.toLowerCase() !== repo.toLowerCase() && !output.toLowerCase().startsWith(repo.toLowerCase() + path.sep), 'Evidence must stay outside the source repository');
fs.mkdirSync(output, { recursive: true });
const run = path.join(output, runName ?? new Date().toISOString().replace(/[:.]/g, '-'));
fs.mkdirSync(run);
const snapshot = path.join(run, 'snapshot');
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const save = (name, value) => fs.writeFileSync(path.join(run, name), JSON.stringify(value, null, 2) + '\n');

// A balanced C++ scanner avoids regex truncation at nested initializers/comments.
function endBrace(text, start) {
  let depth = 0, mode = 'code';
  for (let i = start; i < text.length; ++i) {
    const c = text[i], n = text[i + 1];
    if (mode === 'line') { if (c === '\n') mode = 'code'; continue; }
    if (mode === 'block') { if (c === '*' && n === '/') { mode = 'code'; ++i; } continue; }
    if (mode === 'string' || mode === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (mode === 'string' ? '"' : "'")) mode = 'code';
      continue;
    }
    if (c === '/' && n === '/') { mode = 'line'; ++i; continue; }
    if (c === '/' && n === '*') { mode = 'block'; ++i; continue; }
    if (c === '"' || c === "'") { mode = c === '"' ? 'string' : 'char'; continue; }
    if (c === '{') ++depth;
    if (c === '}' && --depth === 0) return i + 1;
  }
  throw new Error('Unclosed C++ declaration');
}
const sources = [
  'native/main.cpp',
  'native/sleep_power_state.h',
  'native/virtual_controller_stop_runtime.h',
  'native/msi_owned_return_runtime.h',
  'native/msi_owned_return_recovery.h',
  'native/msi_owned_return_off_endpoint.h',
  'native/virtual_output_status.h',
  'native/msi_crash_recovery_journal.h',
  'native/xinput_identity_refresh_policy.h',
  'native/touch_haptic_dispatch.h',
  'deps/json/json.hpp',
  'tools/msi_owned_return_runtime_selftest.cpp',
  'tools/verify-msi-owned-return-runtime.mjs',
  'tools/fixtures/msi_owned_return_store/prepare_base.mjs',
];
const frozen = new Map(sources.map(name => [name, fs.readFileSync(path.join(repo, name))]));
for (const [name, bytes] of frozen) {
  const target = path.join(snapshot, name);
  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, bytes);
}
const ranges = [];
function extract(kind, name, owner = 'native/main.cpp') {
  const main = frozen.get(owner).toString('utf8');
  const prefix = kind === 'function' ? '^static[^\\n;{]*\\b' + name + '\\s*\\('
    : '^' + (kind === 'enum' ? 'enum class' : 'struct') + '\\s+' + name + '\\s*\\{';
  const re = new RegExp(prefix, 'mg');
  let match;
  while ((match = re.exec(main))) {
    // A default std::function argument can contain {} before the body. Track
    // the complete parameter list so its initializer is never extracted as it.
    let brace = main.indexOf('{', match.index);
    if (kind === 'function') {
      let depth = 0, mode = 'code'; brace = -1;
      for (let i = main.indexOf('(', match.index); i < main.length; ++i) {
        const c = main[i], n = main[i + 1];
        if (mode === 'line') { if (c === '\n') mode = 'code'; continue; }
        if (mode === 'block') { if (c === '*' && n === '/') { mode = 'code'; ++i; } continue; }
        if (mode === 'string' || mode === 'char') {
          if (c === '\\') { ++i; continue; }
          if (c === (mode === 'string' ? '"' : "'")) mode = 'code';
          continue;
        }
        if (c === '/' && n === '/') { mode = 'line'; ++i; continue; }
        if (c === '/' && n === '*') { mode = 'block'; ++i; continue; }
        if (c === '"' || c === "'") { mode = c === '"' ? 'string' : 'char'; continue; }
        if (c === '(') ++depth;
        else if (c === ')') --depth;
        else if (depth === 0 && c === ';') break;
        else if (depth === 0 && c === '{') { brace = i; break; }
      }
    }
    const semicolon = kind === 'function' ? -1 : main.indexOf(';', match.index);
    if (brace < 0 || (semicolon >= 0 && semicolon < brace)) continue;
    let end = endBrace(main, brace);
    if (kind !== 'function') { assert.equal(main[end], ';', name); ++end; }
    const code = main.slice(match.index, end);
    ranges.push({ name, source: owner,
      startLine: main.slice(0, match.index).split('\n').length,
      endLine: main.slice(0, end).split('\n').length,
      startUtf8Byte: Buffer.byteLength(main.slice(0, match.index)),
      endUtf8ByteExclusive: Buffer.byteLength(main.slice(0, end)), sha256: sha(code) });
    return code;
  }
  throw new Error('Missing production definition: ' + name);
}
const types = [
  ['struct', 'InputCaptureGamepadSnapshot'], ['struct', 'MsiClawHidDiag'],
  ['enum', 'MsiClawIoState'], ['struct', 'MsiClawWriteResult'],
  ['struct', 'MsiClawCommandOutcome'], ['function', 'msiClawIoStateName'],
  ['struct', 'MsiModeReadbackFacts'], ['struct', 'MsiMode1WritebackVerdict'],
].map(([kind, name]) => extract(kind, name)).join('\n\n');
const debt = [
  ['enum', 'MsiReturnDebtBasis'], ['function', 'msiReturnDebtBasisName'],
  ['function', 'msiReturnDebtDischarge'], ['function', 'msiDinputTryDischargeReturnDebt'],
  ['function', 'msiTerminalReturnMissingTransport'],
  ['function', 'xinputNonVirtualConnectedMask'], ['function', 'xinputPhysicalConnectedMask'],
].map(([kind, name]) => extract(kind, name)).join('\n\n');
fs.writeFileSync(path.join(snapshot, 'tools/production_types.h'), '#pragma once\n#include "../native/touch_haptic_dispatch.h"\n' + types + '\n');
fs.writeFileSync(path.join(snapshot, 'tools/production_debt_helpers.h'), '#pragma once\n' + debt + '\n');
const restore = [
  'msiModeReadbackFacts', 'msiMode1TerminalReason', 'msiMode1WritebackVerdict',
  'msiClawFinishXinputRestore', 'msiClawRestoreXInputMode',
].map(name => extract('function', name)).concat(
  ['inputHostOwnedCleanupProvenLocked', 'inputHostDisabledCleanupProvenLocked'].map(name =>
    extract('function', name, 'native/virtual_controller_stop_runtime.h'))).join('\n\n');
fs.writeFileSync(path.join(snapshot, 'tools/production_restore_helpers.h'), '#pragma once\n' + restore + '\n');
const test = frozen.get('tools/msi_owned_return_runtime_selftest.cpp').toString('utf8');
const realFunctions = ['msiOwnedReturnRecordInitial', 'msiOwnedReturnMaintenance',
  'msiOwnedReturnReadSignal', 'msiOwnedReturnRetryable', 'msiOwnedReturnRecordCurrent', 'msiOwnedReturnContext',
  'msiOwnedReturnObserveOffIntent', 'msiOwnedReturnOffEndpointAbsent', 'msiClawRestoreXInputMode', 'inputHostDisabledCleanupProvenLocked'];
for (const name of realFunctions) {
  assert(test.includes(name + '('), 'Missing direct execution: ' + name);
  assert(!new RegExp('(?:static\\s+)?(?:bool|void|MsiOwnedReturnSignal)\\s+' + name + '\\s*\\(').test(test), 'High-level stub forbidden: ' + name);
}
for (const header of ['msi_owned_return_runtime.h', 'msi_owned_return_recovery.h', 'msi_crash_recovery_journal.h'])
  assert(test.includes('#include "../native/' + header + '"'), 'Missing real include: ' + header);
const stubs = [...test.matchAll(/\/\/ STUB-BEGIN ([^\r\n]+)\r?\n([\s\S]*?)\/\/ STUB-END/g)].map(m => ({
  name: m[1], source: 'tools/msi_owned_return_runtime_selftest.cpp',
  startLine: test.slice(0, m.index).split('\n').length,
  endLine: test.slice(0, m.index + m[0].length).split('\n').length,
  sha256: sha(m[0]),
}));
assert(stubs.length >= 4, 'Boundary ranges must be explicit');
const receipt = {
  mode: 'actual-production-runtime-offline-integration', createdAt: new Date().toISOString(),
  output: run, noInputHost: true, noDeviceIo: true, noSdkDeviceIo: true,
  selectedProfiles,
  wholeMainCompiled: false, productionHeadersModifiedByVerifier: false,
  strictProfiles: { Release: ['/O2', '/MT', '/DNDEBUG'], Checked: ['/Od', '/MTd', '/RTC1', '/Zi', '/D_DEBUG'] },
  realFunctions, extractedDefinitions: ranges, stubs,
  sourceHashes: Object.fromEntries([...frozen].map(([name, bytes]) => [name, sha(bytes)])),
  scope: [
    'Real production runtime/recovery/journal/Slot; actual cached structs extracted byte-for-byte from main.cpp.',
    'Real debt-discharge, physical-mask, terminal gate, restore and finish consumers extracted byte-for-byte; durable-after-discharge is an in-memory sink.',
    'Legacy deferred-transport schedule, terminal command transaction and optional diagnostic trace are explicit fake boundaries; the restore gate/terminal suppression/finish and OFF intent remain production code.',
    'Fake WinAPI clock/process/handle types, board/power/Host context, USB parent/location mapping, command open/close/write, logs and durable store.',
    'No InputHost, SDK, SetupAPI, HID, XInput DLL, Steam, sleep, deployment or actual device command is invoked.',
    'Read-window freshness is production cache provenance, not proof of device-internal freshness or Steam consumption.',
  ],
};
save('source-binding.json', receipt);

return {repo,output,selectedProfiles,run,snapshot,sha,save,sources,frozen,receipt,ranges,extract,endBrace};
}
