// Extract complete, exact production helper definitions. Fail closed on missing
// or ambiguous symbols; do not substitute handwritten production behavior.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const symbols = ['legionMotionRevoke', 'legionMotionPublish', 'legionMotionSnapshot'];

function bodyEnd(text, start) {
  let depth = 0, mode = 'code';
  for (let i = start; i < text.length; ++i) {
    const c = text[i], next = text[i + 1];
    if (mode === 'line') { if (c === '\n') mode = 'code'; continue; }
    if (mode === 'block') { if (c === '*' && next === '/') { mode = 'code'; ++i; } continue; }
    if (mode === 'string' || mode === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (mode === 'string' ? '"' : "'")) mode = 'code';
      continue;
    }
    if (c === '/' && next === '/') { mode = 'line'; ++i; continue; }
    if (c === '/' && next === '*') { mode = 'block'; ++i; continue; }
    assert(!(c === 'R' && next === '"'), 'Raw literal in helper requires explicit extractor support');
    if (c === '"') { mode = 'string'; continue; }
    if (c === "'") { mode = 'char'; continue; }
    if (c === '{') ++depth;
    if (c === '}' && --depth === 0) return i + 1;
  }
  throw Error('Unclosed production helper');
}
function extract(text, symbol) {
  const pattern = new RegExp(`^static[^\\n;{]*\\b${symbol}\\s*\\(`, 'gm');
  const definitions = [];
  for (const match of text.matchAll(pattern)) {
    const open = text.indexOf('{', match.index), semi = text.indexOf(';', match.index);
    if (open < 0 || (semi >= 0 && semi < open)) continue; // A forward declaration.
    const end = bodyEnd(text, open);
    const code = text.slice(match.index, end);
    definitions.push({symbol, code, startLine: text.slice(0, match.index).split('\n').length,
      endLine: text.slice(0, end).split('\n').length,
      startUtf8Byte: Buffer.byteLength(text.slice(0, match.index)),
      endUtf8ByteExclusive: Buffer.byteLength(text.slice(0, end)), sha256: sha(code)});
  }
  assert.equal(definitions.length, 1, `Expected one real main.cpp definition of ${symbol}`);
  return definitions[0];
}
export function freezeMainHelpers(repo, out) {
  const file = path.join(repo, 'native/main.cpp');
  const bytes = fs.readFileSync(file), text = bytes.toString('utf8');
  const entries = symbols.map(symbol => extract(text, symbol));
  const anchorSymbols = ['physicalHidCaptureRawInput', 'inputCaptureSuspendSensorsForPower', 'inputCaptureStop',
    'legionMotionSourceForSelectedLocked', 'inputCaptureLegionControllerMotionRoute'];
  const anchorEntries = anchorSymbols.map(symbol => extract(text, symbol));
  const raw = anchorEntries[0].code, suspend = anchorEntries[1].code, stop = anchorEntries[2].code;
  const checks = [];
  const requireAnchor = (name, condition) => {
    assert(condition, 'Source anchor failed: ' + name);
    checks.push({name, passed: true, evidenceKind: 'source-only; not executed'});
  };
  const ordered = (code, needles) => {
    let previous = -1;
    return needles.every(needle => { const at = code.indexOf(needle); const ok = at > previous; previous = at; return ok; });
  };
  const compact = code => code.replace(/\s+/g, ' ');
  requireAnchor('capture-power-and-epoch-before-resolver-and-publish', ordered(raw, [
    'const auto motionPower = currentPowerGeneration();',
    'const auto motionEpoch = g_legionMotionCaptureEpoch.load(std::memory_order_acquire);',
    'hidHideResolveRawInputIdentity(', 'legionMotionPublish(sample,motionEpoch)']));
  requireAnchor('selected-physical-source-rejection-before-motion-family', ordered(raw, [
    'if (!selected.resolved)', 'if (!exactIdentity && !sameHardware)', 'if (inputCaptureLegionControllerMotionRoute()']));
  requireAnchor('resolved-generation-current-before-model-decoder', ordered(raw, [
    'resolvedGeneration == 0 || resolvedGeneration != g_hidResolveCacheGeneration', 'legionMotionDecodeReport(']));
  const model=anchorEntries[4].code;
  requireAnchor('exact-four-legion-models', ['LegionGoTablet','LegionGoTablet2','LegionGoSZ1','LegionGoSZ2'].every(name=>
    model.includes('"HandheldCompanion.Devices.'+name+'"')) && model.includes('YmccFamily::LenovoLegionGo'));
  requireAnchor('vendor-registration-before-decode', ordered(raw, [
    'g_legionRawInputSubscribed.load(std::memory_order_acquire)', 'legionMotionDecodeReport(']));
  requireAnchor('one-actual-bounded-report', raw.includes('const size_t reportBytes=raw->data.hid.dwSizeHid;') &&
    raw.includes('if (reportCount>payloadBytes/reportBytes) return;') &&
    raw.includes('legionMotionDecodeReport(resolved,packet,reportBytes,reportBytes,motionSensorIndex)'));
  const noSpace=raw.replace(/\s+/g,'');
  requireAnchor('sample-retains-sixaxes-power-generation', noSpace.includes(
    'constymcc::legion_motion::Samplesample{{axes->gx,axes->gy,axes->gz},{axes->ax,axes->ay,axes->az},*legionMessageReceivedAt,{motionPower,resolvedGeneration},'));
  requireAnchor('raw-rejects-missing-queue-time-before-decoder', ordered(raw, [
    'if (!legionMessageReceivedAt) return;', 'legionMotionDecodeReport(']));
  requireAnchor('raw-sample-captures-current-side-and-epoch', noSpace.includes(
    '{legionMotionStableIdentity(resolved),motionEpoch,motionSensorIndex}'));
  for (const entry of entries.filter(entry => entry.symbol !== 'legionMotionRevoke')) {
    requireAnchor(entry.symbol + '-selected-source-resolved-before-lease', ordered(entry.code, [
      'legionMotionSourceForSelectedLocked()', 'g_legionMotion.'
    ]));
    const locks = [...entry.code.matchAll(/std::lock_guard<std::mutex>\s+\w+\((g_hidHidePhysicalIdentityMx|g_legionImuMx)\)/g)]
      .map(m => m[1]);
    requireAnchor(entry.symbol + '-selected-then-lease-lock-order',
      locks.length === 2 && locks[0] === 'g_hidHidePhysicalIdentityMx' && locks[1] === 'g_legionImuMx');
  }
  requireAnchor('selected-source-provider-kept-as-unexecuted-source-anchor',
    anchorEntries[3].code.includes('ymcc::legion_motion::Source'));
  const retire = 'if (g_machineIdentity.family == YmccFamily::LenovoLegionGo) legionMotionRevoke();';
  requireAnchor('suspend-retires-only-lenovo-before-ready-early-return', ordered(suspend, [retire,
    'if (!g_inputCaptureReady || g_inputCaptureSensorsSuspended) return;']));
  requireAnchor('stop-flags-before-lenovo-retirement', ordered(stop, [
    'g_inputCaptureStop.store(true, std::memory_order_release);', retire]));
  requireAnchor('three-helpers-use-same-mutex', entries.every(entry =>
    /std::lock_guard<std::mutex>\s+\w+\(g_legionImuMx\)/.test(entry.code)));
  requireAnchor('revoke-advances-epoch-before-clearing-lease', ordered(entries[0].code, [
    'std::lock_guard<std::mutex> lock(g_legionImuMx);',
    'g_legionMotionCaptureEpoch.store(next, std::memory_order_release);', 'g_legionMotion.revoke();']));
  fs.writeFileSync(path.join(out, 'main.cpp.snapshot'), bytes);
  const anchorDir = path.join(out, 'source-anchors');
  fs.mkdirSync(anchorDir);
  for (const entry of anchorEntries) fs.writeFileSync(path.join(anchorDir, entry.symbol + '.cpp.txt'), entry.code);
  fs.writeFileSync(path.join(out, 'main-source-anchors.json'), JSON.stringify({
    scope: 'Read-only source anchors; these functions are not executed by the harness.', checks,
    entries: anchorEntries.map(({code, ...binding}) => binding),
  }, null, 2) + '\n');
  const generated = entries.map(entry => entry.code).join('\n\n') + '\n';
  fs.writeFileSync(path.join(out, 'main_helpers.generated.h'), generated);
  fs.writeFileSync(path.join(out, 'main-helper-source-bindings.json'), JSON.stringify({
    source: 'native/main.cpp', sourceSha256: sha(bytes), sourceBytes: bytes.length,
    generatedSha256: sha(generated),
    entries: entries.map(({code, ...binding}) => binding),
    scope: 'Three exact function definitions only. Globals, currentPowerGeneration, inputCaptureLegionControllerMotionRoute and legionMotionSourceForSelectedLocked are controlled stand-ins; source selection, call sites and Raw HID protocol are not executed.',
  }, null, 2) + '\n');
  return {sourceSha256: sha(bytes), entries, anchorEntries, sourceAnchorChecks: checks.length, generatedSha256: sha(generated)};
}
export function verifyMainHelpers(repo, before) {
  const bytes = fs.readFileSync(path.join(repo, 'native/main.cpp'));
  const text = bytes.toString('utf8');
  const entries = symbols.map(symbol => extract(text, symbol));
  return {fullSourceChanged: sha(bytes) !== before.sourceSha256,
    sourceAnchorsStable: before.anchorEntries.every(entry => extract(text, entry.symbol).sha256 === entry.sha256),
    selectedHelpersStable: entries.every((entry, i) => entry.sha256 === before.entries[i].sha256),
    sourceSha256After: sha(bytes)};
}
