import {snapshotControllerMotionFixture,verifyControllerMotionFixture} from '../../lib/hc_controller_motion_fixture.mjs';
// Verbatim extraction only: no copied behavior substitutes or production edits.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
export const functions = [
  'ascii_lower', 'legionBackIsPid', 'legionSBackIsPid', 'legionMotionRevoke',
  'inputCaptureLegionMotionSessionRevoke', 'legionMotionModelBinding', 'inputCaptureLegionControllerMotionRoute', 'legionMotionMessageReceiptTick',
  'legionMotionSelectedSensorIndex', 'legionMotionConfiguredSensorIndex', 'legionMotionSelectSensorIndex',
  'legionHcInputReportView', 'legionMotionHardwareAdmitted', 'legionMotionCollectionAdmitted', 'legionMotionIdentityAdmitted',
  'legionMotionStableIdentity', 'legionMotionSelectedIdentityChanging',
  'legionMotionRegisterRawInput', 'gamepadRegisterRawInput', 'legionMotionDecodeReport',
  'legionMotionBoundSource', 'legionMotionSourceForSelectedLocked', 'legionMotionPublish', 'legionMotionSnapshot',
  'legionMotionSubmissionCurrent', 'inputCaptureControllerCalibration',
  'inputCaptureControllerCalibrationOps', 'inputCaptureControllerCalibrationSourceKey', 'inputCaptureControllerCalibrationOutcomeName', 'inputCaptureControllerCalibrationComplete', 'inputCaptureControllerCalibrationRequest', 'inputCaptureControllerCalibrationStatus', 'inputCaptureControllerCalibrationRestoreTrust', 'inputCaptureControllerCalibrationWriteAtomic', 'inputCaptureControllerCalibrationStore', 'inputCaptureControllerCalibrationAbort', 'inputCaptureControllerCalibrationAdvance',
  'inputCaptureLegionSelectionPending', 'inputCaptureLegionMotion',
  'inputHostWindowsMotionSubmissionCurrent', 'inputHostFrameSubmissionMayIssue', 'inputHostFrameSubmissionRequestCurrent',
];
export const structures = ['HidHidePhysicalIdentity', 'InputFrameSubmission',
  'LegionMotionSubmissionScope', 'InputFrameSubmissionScope', 'ControllerCalibrationRequest'];
function endBody(text, start) {
  let depth = 0, mode = 'code', rawEnd = '';
  for (let i = start; i < text.length; ++i) {
    const c = text[i], next = text[i + 1];
    if (mode === 'raw') { if (text.startsWith(rawEnd, i)) { i += rawEnd.length - 1; mode = 'code'; } continue; }
    if (mode === 'line') { if (c === '\n') mode = 'code'; continue; }
    if (mode === 'block') { if (c === '*' && next === '/') { ++i; mode = 'code'; } continue; }
    if (mode === 'string' || mode === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (mode === 'string' ? '"' : "'")) mode = 'code';
      continue;
    }
    if (c === '/' && next === '/') { ++i; mode = 'line'; continue; }
    if (c === '/' && next === '*') { ++i; mode = 'block'; continue; }
    if (c === 'R' && next === '"') {
      const open = text.indexOf('(', i + 2); assert(open >= 0 && open - i <= 18);
      rawEnd = ')' + text.slice(i + 2, open) + '"'; mode = 'raw'; i = open; continue;
    }
    if (c === '"') { mode = 'string'; continue; }
    if (c === "'") { mode = 'char'; continue; }
    if (c === '{') ++depth;
    if (c === '}' && --depth === 0) return i + 1;
  }
  throw Error('Unclosed source body');
}
export function extract(text, symbol, kind = 'function') {
  const regex = kind === 'function' ? new RegExp(`^static[^\\n;{]*\\b${symbol}\\s*\\(`, 'gm') :
    new RegExp(`^struct ${symbol}\\s*\\{`, 'gm');
  const found = [];
  for (const match of text.matchAll(regex)) {
    const open = text.indexOf('{', match.index), semi = text.indexOf(';', match.index);
    if (open < 0 || (semi >= 0 && semi < open)) continue;
    let end = endBody(text, open);
    if (kind === 'struct') { assert.equal(text[end], ';'); ++end; }
    const code = text.slice(match.index, end);
    found.push({symbol, kind, code, sha256: sha(code),
      startLine: text.slice(0, match.index).split('\n').length,
      endLine: text.slice(0, end).split('\n').length,
      startUtf8Byte: Buffer.byteLength(text.slice(0, match.index)),
      endUtf8ByteExclusive: Buffer.byteLength(text.slice(0, end))});
  }
  assert.equal(found.length, 1, `Expected one ${kind} definition: ${symbol}`);
  return found[0];
}
function extractWmInput(text) {
  const matches=[...text.matchAll(/^\s*case WM_INPUT:\s*\{/gm)];assert.equal(matches.length,1,'Expected one WM_INPUT block');
  const start=matches[0].index,open=text.indexOf('{',start),end=endBody(text,open),code=text.slice(start,end);
  return {symbol:'WM_INPUT',kind:'block',code,sha256:sha(code),startLine:text.slice(0,start).split('\n').length,
    endLine:text.slice(0,end).split('\n').length,startUtf8Byte:Buffer.byteLength(text.slice(0,start)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,end))};
}
export function freeze(repo, out) {
  const bytes = fs.readFileSync(path.join(repo, 'native/main.cpp')), text = bytes.toString('utf8');
  const entries = [...functions.map(s => extract(text, s)), ...structures.map(s => extract(text, s, 'struct'))];
  const pidPattern = /^static const USHORT kLegionBackPids\[\] = \{[^}]+\};/gm;
  const pids = [...text.matchAll(pidPattern)]; assert.equal(pids.length, 1);
  const pidCode = pids[0][0];
  const sPids = [...text.matchAll(/^static const USHORT kLegionSPids\[\] = \{[^}]+\};/gm)];
  assert.equal(sPids.length,1);
  const sPid = sPids[0];
  entries.push({symbol:'kLegionSPids',kind:'constant',code:sPid[0],sha256:sha(sPid[0]),
    startUtf8Byte:Buffer.byteLength(text.slice(0,sPid.index)),
    endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,sPid.index+sPid[0].length))});
  const timerMatches = [...text.matchAll(/^#define GP_STATE_FALLBACK_TIMER_ID[^\r\n]*/gm)];
  assert.equal(timerMatches.length,1);
  const timer = timerMatches[0];
  entries.push({symbol:'GP_STATE_FALLBACK_TIMER_ID',kind:'constant',code:timer[0],sha256:sha(timer[0]),
    startUtf8Byte:Buffer.byteLength(text.slice(0,timer.index)),
    endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,timer.index+timer[0].length))});
  const sideMatches = [...text.matchAll(/^static constexpr int kLegionGyroSide = \d+;/gm)];
  assert.equal(sideMatches.length, 1);
  const side = sideMatches[0];
  entries.push({symbol: 'kLegionGyroSide', kind: 'constant', code: side[0], sha256: sha(side[0]),
    startUtf8Byte: Buffer.byteLength(text.slice(0, side.index)),
    endUtf8ByteExclusive: Buffer.byteLength(text.slice(0, side.index + side[0].length))});
  entries.push({symbol: 'kLegionBackPids', kind: 'constant', code: pidCode, sha256: sha(pidCode),
    startUtf8Byte: Buffer.byteLength(text.slice(0, pids[0].index)),
    endUtf8ByteExclusive: Buffer.byteLength(text.slice(0, pids[0].index + pidCode.length))});
  const epochDeclarations=[...text.matchAll(/^static std::atomic<(?:std::)?uint64_t> g_legionMotionEpochStartedAt\s*\{0\};/gm)];
  assert.equal(epochDeclarations.length,1,'Actual cutover atomic declaration required');
  const epochDeclaration=epochDeclarations[0];entries.push({symbol:'g_legionMotionEpochStartedAt',kind:'constant',code:epochDeclaration[0],sha256:sha(epochDeclaration[0]),
    startUtf8Byte:Buffer.byteLength(text.slice(0,epochDeclaration.index)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,epochDeclaration.index+epochDeclaration[0].length))});
  const anchors = ['physicalHidCaptureRawInput', 'inputCaptureOnGamepad', 'inputHostSubmitPad',
    'hidHideObserveRawInputDevice', 'hidHideGetPhysicalIdentities', 'inputCaptureShutdown',
    'inputCaptureEmergencyCleanup', 'inputCaptureRefreshRealStickSettings', 'gamepadRecoverAfterResume',
    'hidHideResolveInterfaceIdentity', 'hidHideResolveRawInputIdentity', 'hidHideInvalidateRawInputIdentityCache',
    'hidHideEvictOldestResolveEntryLocked', 'hidHideResolvePhysicalIdentities'].map(s => extract(text, s));
  anchors.push(extractWmInput(text));
  const get = name => [...entries, ...anchors].find(e => e.symbol === name).code;
  const compact = code => code.replace(/\s+/g, ' ');
  const ordered = (code, needles) => { let prior = -1; return needles.every(n => {
    const at = code.indexOf(n, prior + 1), ok = at > prior; prior = at; return ok;
  }); };
  const checks = [];
  const require = (name, ok) => { assert(ok, 'Source anchor: ' + name); checks.push({name, passed: true, kind: 'source-only; not executed'}); };
  const raw = get('physicalHidCaptureRawInput');
  require('WM_INPUT-queue-clock-before-raw-read-and-forwarded-optional',ordered(get('WM_INPUT'),[
    'const auto legionMessageReceivedAt = legionMotionMessageReceiptTick();','GetRawInputData(', 'physicalHidCaptureRawInput(raw,legionMessageReceivedAt)']));
  require('physical-raw-accepts-optional-queue-time',/physicalHidCaptureRawInput\(RAWINPUT\* raw, std::optional<uint64_t> legionMessageReceivedAt\)/.test(raw));
  require('vendor-null-queue-time-before-report-decode',ordered(raw,['if (!legionMessageReceivedAt) return;','const size_t reportBytes','legionMotionDecodeReport(']));
  require('vendor-sample-uses-queue-time-not-parse-now',raw.replace(/\s+/g,'').includes('*legionMessageReceivedAt,{motionPower,resolvedGeneration}'));
  for(const symbol of ['legionMotionRevoke','legionMotionSelectSensorIndex']) require(symbol+'-cutover-store-before-captureEpoch-store',ordered(get(symbol),[
    'g_legionMotionEpochStartedAt.store(GetTickCount64()', 'g_legionMotionCaptureEpoch.store(']));
  const sideCapture = /const\s+(?:auto|unsigned|unsigned int)\s+(\w+)\s*=\s*legionMotionSelectedSensorIndex\(\)/.exec(raw);
  require('raw-side-captured-after-epoch', sideCapture && ordered(raw,
    ['g_legionMotionCaptureEpoch.load(', sideCapture[0]]));
  require('raw-full-source-stamp', sideCapture && raw.replace(/\s+/g,'').includes(
    'legionMotionBoundSource(resolved,motionEpoch,' + sideCapture[1] + ')'));
  require('raw-calls-production-model-decode-helper', raw.includes('legionMotionDecodeReport('));
  require('existing-resume-calls-existing-raw-registration', get('gamepadRecoverAfterResume').includes('gamepadRegisterRawInput()'));
  require('settings-family-guard-before-selected-side-application', ordered(get('inputCaptureRefreshRealStickSettings'),
    ['inputCaptureLegionControllerMotionRoute()', 'legionMotionSelectSensorIndex(']));
  require('onGamepad-real-controller-sample-snapshot-and-process', ordered(get('inputCaptureOnGamepad'),
    ['legionMotionSnapshot(', 'inputCaptureLegionMotion(controllerMotion,']));
  const gamepadBody=get('inputCaptureOnGamepad'),calibrationAt=gamepadBody.indexOf('inputCaptureGyroCalibrationTick(');
  const calibrationGate=gamepadBody.lastIndexOf('if (!controllerMotionRoute) {',calibrationAt);
  require('onGamepad-windows-calibration-route-is-excluded',calibrationAt>=0&&calibrationGate>=0&&
    calibrationAt<endBody(gamepadBody,gamepadBody.indexOf('{',calibrationGate)));
  require('controller-route-keeps-raw-dps-not-windows-threshold', compact(get('inputCaptureOnGamepad')).includes(
    'if (!controllerMotionRoute) { gx = hcClip(gx); gy = hcClip(gy); gz = hcClip(gz); }'));
  require('onGamepad-controller-readback-finite-before-motion-pair', ordered(get('inputCaptureOnGamepad'),
    ['const bool controllerOutputFinite=controllerProcessed', 'const bool controllerMotionPair=']));
  require('onGamepad-controller-wire-orientation-finite', compact(get('inputCaptureOnGamepad')).includes(
    '(!controllerMotionRoute || (std::isfinite(orientationW) && std::isfinite(orientationX) &&'));
  require('onGamepad-controller-scope-before-host-submit', ordered(get('inputCaptureOnGamepad'),
    ['const LegionMotionSubmissionScope controllerSubmission(', 'hostFrame = inputHostSubmitPad(']));
  require('host-binds-controller-pointer-and-frame-scope', ordered(get('inputHostSubmitPad'),
    ['frameSubmission.controllerMotion=t_legionMotionSubmission;', 'InputFrameSubmissionScope submissionScope(']));
  for (const [symbol, value] of [['hidHideObserveRawInputDevice', 'resolved'], ['hidHideGetPhysicalIdentities', 'selected']])
    require(symbol + '-retires-before-selected-assignment', ordered(get(symbol), [
      `legionMotionSelectedIdentityChanging(g_hidHidePhysicalIdentity, ${value});`, `g_hidHidePhysicalIdentity = ${value};`]));
  for (const symbol of ['inputCaptureShutdown', 'inputCaptureEmergencyCleanup'])
    require(symbol + '-controller-revoke-before-unload', ordered(get(symbol),
      ['inputCaptureLegionMotionSessionRevoke();', 'FreeLibrary(g_gmDll)']));
  require('snapshot-nonlegion-early-return-before-identity-lock', ordered(get('legionMotionSnapshot'),
    ['if (!inputCaptureLegionControllerMotionRoute()) return std::nullopt;', 'selectedLock(g_hidHidePhysicalIdentityMx)']));
  for (const symbol of ['legionMotionPublish', 'legionMotionSnapshot']) {
    require(symbol + '-physical-then-lease-locks', ordered(get(symbol),
      ['selectedLock(g_hidHidePhysicalIdentityMx)', 'lock(g_legionImuMx)', 'legionMotionSourceForSelectedLocked()']));
  }

  require('sensor-selection-stores-side-before-epoch-and-lease-revoke', ordered(get('legionMotionSelectSensorIndex'),
    ['g_legionMotionSensorIndex.store(', 'g_legionMotionCaptureEpoch.store(', 'g_legionMotion.revoke()']));
  require('registration-retires-before-vendor-API', ordered(get('legionMotionRegisterRawInput'),
    ['g_legionRawInputSubscribed.store(false', 'legionMotionRevoke()', 'RegisterRawInputDevices(']));
  require('base-error-and-keyboard-channel-before-vendor-registration', ordered(get('gamepadRegisterRawInput'),
    ['RegisterRawInputDevices(rid, 5', 'rawInputError', 'g_keyboardModifierRawSubscribed.store(', 'legionMotionRegisterRawInput(']));
  for (const name of ['legionMotionPublish','legionMotionSnapshot','legionMotionSubmissionCurrent'])
    require(name+'-requires-vendor-subscription',get(name).includes('g_legionRawInputSubscribed.load('));
  for (const name of ['hidHideResolveInterfaceIdentity','hidHideResolveRawInputIdentity','hidHideEvictOldestResolveEntryLocked'])
    require(name+'-default-motion-scope-false',/bool\s+allowLegionMotionCollection\s*=\s*false/.test(get(name)));
  for (const name of ['hidHideResolveRawInputIdentity','hidHideEvictOldestResolveEntryLocked'])
    require(name+'-scope-selects-separate-map',/allowLegionMotionCollection\s*\?\s*g_legionMotionResolveCache\s*:\s*g_hidResolveCache/.test(get(name)));
  require('both-resolve-cache-namespaces-invalidated-together',
    get('hidHideInvalidateRawInputIdentityCache').includes('g_hidResolveCache.clear()') &&
    get('hidHideInvalidateRawInputIdentityCache').includes('g_legionMotionResolveCache.clear()') &&
    get('hidHideInvalidateRawInputIdentityCache').includes('g_hidResolveCacheMx'));
  require('motion-map-same-cache-type-source-declaration',/^static decltype\(g_hidResolveCache\) g_legionMotionResolveCache;/m.test(text));
  for (const name of ['hidHideObserveRawInputDevice','hidHideGetPhysicalIdentities','hidHideResolvePhysicalIdentities'])
    require(name+'-no-motion-cache-opt-in',!get(name).includes('g_legionMotionResolveCache') &&
      !get(name).includes('allowLegionMotionCollection'));

  const registryStart=text.indexOf('static std::mutex g_controllerCalibrationMx;');
  const abortDeclaration=/^static void inputCaptureControllerCalibrationAbort\(\)(?: noexcept)?;/m.exec(text.slice(registryStart));
  const registryEnd=abortDeclaration ? registryStart+abortDeclaration.index : -1;
  assert(registryStart>=0&&registryEnd>registryStart,'Exact calibration registry declarations');
  const registry=text.slice(registryStart,registryEnd);
  entries.push({symbol:'controllerCalibrationRegistry',kind:'constant',code:registry,sha256:sha(registry),
    startUtf8Byte:Buffer.byteLength(text.slice(0,registryStart)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,registryEnd))});
  const types = entries.filter(e => e.kind === 'struct' && !e.symbol.endsWith('Scope')).map(e => e.code).join('\n\n');
  const scopes = entries.filter(e => e.symbol.endsWith('Scope')).map(e => e.code).join('\n\n');
  const motionBinding=snapshotControllerMotionFixture(repo,path.join(out,'controller-motion'));
  const generated = motionBinding.preamble + registry + '\n'+abortDeclaration[0]+'\n' + epochDeclaration[0] + '\n' + timer[0] + '\n' + sPid[0] + '\n' + pidCode + '\n' + side[0] + '\n\n' + functions.map(s => get(s)).join('\n\n') + '\n';
  fs.writeFileSync(path.join(out, 'main.cpp.snapshot'), bytes);
  fs.writeFileSync(path.join(out, 'production_types.generated.h'), types + '\n');
  fs.writeFileSync(path.join(out, 'production_scopes.generated.h'), scopes + '\n');
  fs.writeFileSync(path.join(out, 'production_functions.generated.h'), generated);
  fs.mkdirSync(path.join(out, 'source-anchors'));
  for (const e of anchors) fs.writeFileSync(path.join(out, 'source-anchors', e.symbol + '.cpp.txt'), e.code);
  const binding = {controllerMotionInputs:motionBinding.inputs,source: 'native/main.cpp', sourceSha256: sha(bytes), sourceBytes: bytes.length,
    functionsAndTypes: entries.map(({code, ...e}) => e), anchors: anchors.map(({code, ...e}) => e), checks,
    scope: 'Verbatim listed functions/types including queue clock wrapper and cutover predicates executed with controlled globals, effects, clocks and actual JSON files. Wrapper-to-Sample timestamp transfer is fixture orchestration. Whole WM_INPUT, physicalHidCaptureRawInput, onGamepad, Host, setters, resolver and shutdown functions are source-only anchors.'};
  fs.writeFileSync(path.join(out, 'main-source-bindings.json'), JSON.stringify(binding, null, 2) + '\n');
  return {binding, entries, anchors, generated, motionBinding};
}
export function verify(repo, before) {
  const bytes = fs.readFileSync(path.join(repo, 'native/main.cpp')), text = bytes.toString('utf8');
  const selected = before.entries.filter(e => e.kind !== 'constant').every(e => extract(text, e.symbol, e.kind).sha256 === e.sha256);
  const constants = before.entries.filter(e => e.kind === 'constant');
  return {fullSourceChanged: sha(bytes) !== before.binding.sourceSha256,
    selectedMethodsStable: selected && constants.every(e => text.includes(e.code)) && verifyControllerMotionFixture(before.motionBinding).every(row=>row.unchanged),
    anchorsStable: before.anchors.every(e => (e.kind==='block'?extractWmInput(text):extract(text, e.symbol)).sha256 === e.sha256), sourceSha256After: sha(bytes)};
}
