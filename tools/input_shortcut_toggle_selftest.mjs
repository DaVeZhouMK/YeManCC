// Compile the real native parser/evaluator/action against in-memory boundaries.
// No installed settings, hardware reads, process operations or device creation.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import {createHash} from 'node:crypto';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cases = process.env.YMCC_SHORTCUT_CASES || 'all';
const virtualRearFirstFrame = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'first-frame';
const virtualRearUiFirstCross = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'ui-first-cross';
const virtualRearUiFirstMixedNames=['controller','desktop','click','controller-boundary','desktop-boundary','cutoff','controller-retire','msi-retire','msi-retire-tail','desktop-boundary-tail'];
const virtualRearUiFirstMixedIndex=virtualRearUiFirstMixedNames.indexOf((process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY||'').replace(/^ui-first-mixed-/,''));
const virtualRearUiFirstMixedKind=virtualRearUiFirstMixedIndex>=0&&process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY==='ui-first-mixed-'+virtualRearUiFirstMixedNames[virtualRearUiFirstMixedIndex]?virtualRearUiFirstMixedIndex:undefined;
const virtualRearUiFirstMixed = virtualRearUiFirstMixedKind !== undefined;
const virtualRearUiFirstClickExtra = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'ui-first-click-extra';
const virtualRearUiFirstClick = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'ui-first-click' || virtualRearUiFirstClickExtra;
const virtualRearUiFirstTailMsi = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'ui-first-tail-msi';
const virtualRearUiFirstTailIo = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'ui-first-tail-io';
const virtualRearUiFirstTail = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'ui-first-tail' || virtualRearUiFirstTailMsi || virtualRearUiFirstTailIo;
const virtualRearUiFirst = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'ui-first' || virtualRearUiFirstTail || virtualRearUiFirstCross || virtualRearUiFirstClick || virtualRearUiFirstMixed;
const virtualRearKeyboardMsiTail = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'keyboard-msi-tail';
const virtualRearKeyboardMsi = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'keyboard-msi' || virtualRearKeyboardMsiTail;
const virtualRearSourceLife = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'source-life';
const virtualRearGestures = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'gestures';
const virtualRearUnknown = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === 'unknown';
const virtualRearOnly = process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === '1' || virtualRearFirstFrame || virtualRearUiFirst || virtualRearKeyboardMsi || virtualRearSourceLife || virtualRearGestures || virtualRearUnknown;
assert(process.env.YMCC_SHORTCUT_VIRTUAL_REAR_ONLY === undefined || virtualRearOnly,
  'Unknown virtual rear variant');
assert(!virtualRearOnly || cases === 'native-oem-click-actions',
  'Virtual rear variant requires the existing native-oem-click-actions selector');
const keyboardWorkerCases=['native-oem-keyboard-worker-owner','native-oem-keyboard-worker-revision','native-oem-keyboard-worker'].includes(cases);
const desktopCase=['native-recorded-mouse-only-hooks','native-recorded-mouse-only-retry','native-recorded-mouse-only-cold','native-recorded-mouse-independent-owner','native-recorded-mouse-publication','native-recorded-mouse-prior-owner','native-recorded-mouse-partial-hook','native-recorded-mouse-pulses','native-recorded-mouse-guards','native-recorded-mouse-replay','native-recorded-mouse-oem-compat','native-recorded-keyboard-diagnostics','native-recorded-keyboard-definitions','native-recorded-keyboard-preheld','native-recorded-keyboard-fallback-control','native-recorded-keyboard-guards-tail','native-recorded-keyboard-guards','native-recorded-keyboard-consume','native-recorded-keyboard-chord','native-rog-held-return','native-rog-source-owner','native-rog-held-boundaries','native-rog-held-source','native-rog-ll-fallback-transition','native-rog-ll-fallback','native-desktop-chord-shape','native-desktop-chord-admission','native-desktop-chord-guards','native-desktop-function-teardown-gate','native-desktop-function-teardown','native-desktop-function','native-desktop-function-boundaries','native-desktop-function-final','native-desktop-function-final-tail','native-desktop-function-proof','native-desktop-function-proof-tail','native-desktop-function-proof-final'].includes(cases);
const gateCases=desktopCase||['native-oem-gate-transitions','native-oem-gate-transitions-tail'].includes(cases);
const wmiMaintenance=cases==='wmi-maintenance';
const nativeGateTestMode=gateCases||cases==='native-oem-queued-owner'||cases==='native-oem-click-actions';
const nativeGateDependencies=nativeGateTestMode||wmiMaintenance;
assert(['native-recorded-mouse-only-hooks','native-recorded-mouse-only-retry','native-recorded-mouse-only-cold','native-recorded-mouse-independent-owner','native-recorded-mouse-publication','native-recorded-mouse-prior-owner','native-recorded-mouse-partial-hook','native-recorded-mouse-pulses','native-recorded-mouse-guards','native-recorded-mouse-replay','native-recorded-mouse-oem-compat','native-recorded-keyboard-diagnostics','native-recorded-keyboard-definitions','native-recorded-keyboard-preheld','native-recorded-keyboard-fallback-control','native-recorded-keyboard-guards-tail','native-recorded-keyboard-guards','native-recorded-keyboard-consume','native-recorded-keyboard-chord','native-rog-held-return','native-rog-source-owner','native-rog-held-boundaries','native-rog-held-source','native-rog-ll-fallback-transition','native-rog-ll-fallback','native-desktop-chord-shape','native-desktop-chord-admission','native-desktop-chord-guards','native-desktop-function-teardown-gate','native-desktop-function-teardown','all','frontend-buttons','retired-start-f7','baseline-without-retirement','summon-action-shield','summon-action-shield-custom','summon-action-shield-ordinary-key','onex-consumption','wmi-maintenance','native-oem-click-actions','native-oem-owner-boundary','native-oem-queued-owner','native-oem-ui-mailbox','native-oem-keyboard-worker-owner','native-oem-keyboard-worker-revision','native-oem-keyboard-worker','native-oem-keyboard-suppression','native-desktop-function','native-desktop-function-boundaries','native-desktop-function-final','native-desktop-function-final-tail','native-desktop-function-proof','native-desktop-function-proof-tail','native-desktop-function-proof-final','native-oem-aya-consumption','native-oem-aya-extensions','native-oem-gate-transitions','native-oem-gate-transitions-tail','joyxoff-session-cleanup'].includes(cases), 'Unknown shortcut case selection');
function shortcutEvidenceDirectory(explicitOut) {
  if (!explicitOut) {
    const base = path.resolve(root, '../../../Testing/YMCC/Evidence/InputShortcut');
    fs.mkdirSync(base, { recursive: true });
    return fs.mkdtempSync(path.join(base, 'input-shortcut-' + cases + '-'));
  }
  const directory = path.resolve(explicitOut);
  if (fs.existsSync(directory)) {
    assert(fs.statSync(directory).isDirectory(), 'Shortcut output must be a directory');
    for (const name of ['capability-headers','SOURCE-BINDINGS.json','PREPARED.json','RESULT.json','input-shortcut-real-route.fixture.cpp','input-shortcut-real-route.fixture.obj','input-shortcut-real-route.fixture.exe','compile-native-fixture.cmd','native-fixture-build.log','native-fixture-results.log','joyxoff-session-cleanup.current.inc'])
      assert(!fs.existsSync(path.join(directory, name)), 'Fresh shortcut output required: ' + name);
  }
  fs.mkdirSync(directory, { recursive: true });
  return directory;
}
const out = shortcutEvidenceDirectory(process.env.YMCC_SHORTCUT_OUT);
const mainPath = process.env.YMCC_SHORTCUT_SOURCE || path.join(root, 'native/main.cpp');
// Opt-in leaf only; the original all/default suite below is unchanged.
if (process.env.YMCC_SHORTCUT_CASES === 'joyxoff-session-cleanup') {
  const {runJoyXoffSessionCleanup} = await import('./fixtures/input_shortcut_toggle/joyxoff-session-cleanup.mjs');
  await runJoyXoffSessionCleanup({root, out, mainPath});
} else {
const invocationStarted=Date.now();
const bindingFiles=new Set([mainPath,fileURLToPath(import.meta.url),
  ...['tools/input_shortcut_toggle_selftest.cpp','tools/shortcut_summon_frontend_cases.h',
    'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(file=>path.join(root,file))]);
const inlineHeaders=['oem_vendor_stack.h','shortcut_feedback_native.h','shortcut_steam_buttons.h','shortcut_action_runtime.h',
  'virtual_controller_ui_output_runtime.h','msi_native_foreground_isolation_runtime.h','msi_foreground_isolation_runtime.h','screen_touchpads.h'];
const compileHeaders=['keyboard_shortcut_sequence.h','input_shortcut_toggle.h','shortcut_window_toggle.h','shortcut_frontend_button.h','shortcut_action_protocol.h',
  'keyboard_modifier_event_evidence.h','screen_special_actions.h','steam_qam_followup.h','virtual_target_policy.h','controller_persona.h',
  'boot_mode_shortcuts.h','radial_release.h','virtual_start_protection.h','msi_qs_keyboard_mirror.h','msi_rear_protocol.h','legion_motion_activation.h',
  'input_frame_submission.h','desktop_function_shortcut.h','keyboard_shortcut_sequence_runtime.h','sleep_power_state.h',
  'win5_menu_shortcut_policy.h','msi_key_event_class.h','log_snapshot_state.h'];
const pendingHeaders=[...inlineHeaders,...compileHeaders].map(file=>path.join(root,'native',file));
pendingHeaders.push(path.join(root,'capabilities/HcOemInputCapabilityBinding.h'));
const visitedHeaders=new Set();
while(pendingHeaders.length){
  const file=pendingHeaders.pop();if(visitedHeaders.has(file))continue;visitedHeaders.add(file);
  assert(fs.existsSync(file),'Missing declared shortcut input '+file);bindingFiles.add(file);
  for(const match of fs.readFileSync(file,'utf8').matchAll(/^\s*#include\s+"([^"\r\n]+)"/gm)){
    const dependency=[path.resolve(path.dirname(file),match[1]),path.join(root,'native',match[1]),path.join(root,'deps/json',match[1])].find(candidate=>fs.existsSync(candidate));
    if(dependency&&!visitedHeaders.has(dependency))pendingHeaders.push(dependency);
  }
}
// Extracted OneX consumers compile the real, copied capability header closure.
const capabilityCopies=[];
for(const file of bindingFiles){
 const relative=path.relative(root,file);if(!relative.startsWith('capabilities'+path.sep))continue;
 const copy=path.join(out,'capability-headers',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,fs.readFileSync(file));capabilityCopies.push({original:file,copy});
}
const capabilityPreamble='#include '+JSON.stringify(path.join(out,'capability-headers/capabilities/HcOemInputCapabilityBinding.h').replaceAll('\\','/'))+'\n';
const sourceBinding=[...bindingFiles].sort().map(file=>({path:file,sha256:createHash('sha256').update(fs.readFileSync(file)).digest('hex')}));
fs.writeFileSync(path.join(out,'SOURCE-BINDINGS.json'),JSON.stringify({sourceBinding,capabilityCopies,realProductStarted:false,realDeviceIo:false},null,2)+'\n');
let source = fs.readFileSync(mainPath, 'utf8');
if(virtualRearUnknown) {
  const allocation='auto next = std::make_shared<OemVirtualKeyboardSample>();';
  assert.equal(source.split(allocation).length,2,'Unknown-path control requires exact keyboard snapshot allocation');
  source=source.replace(allocation,'if(fixtureOemVirtualAllocationFailure)throw std::bad_alloc();\n    '+allocation);
}
// Keep extracting the real consumers as their helpers move into source headers.
for (const header of ['oem_vendor_stack.h','shortcut_feedback_native.h','shortcut_steam_buttons.h','shortcut_action_runtime.h','virtual_controller_ui_output_runtime.h']) {
  source = source.replace('#include "' + header + '"', () => fs.readFileSync(path.join(root, 'native', header), 'utf8'));
}
// Extract only physical UI admission bridges; never execute their I/O runtimes.
for (const header of ['msi_native_foreground_isolation_runtime.h','msi_foreground_isolation_runtime.h']) {
  source += '\n' + fs.readFileSync(path.join(root, 'native', header), 'utf8');
}
const shieldMode = {'summon-action-shield':0,'summon-action-shield-custom':1,'summon-action-shield-ordinary-key':2}[cases];
const fieldInput = process.env.YMCC_SHORTCUT_FIELD_SETTINGS ? JSON.parse(fs.readFileSync(process.env.YMCC_SHORTCUT_FIELD_SETTINGS, 'utf8').replace(/^\uFEFF/, '')).input : null;
function extract(name) {
  const marker = new RegExp(String.raw`^static [^\n]*\b` + name + String.raw`\([^;]*?\)\s*(?:noexcept\s*)?\{`, 'm');
  const definition = marker.exec(source); assert(definition, 'Missing function ' + name);
  const start = definition.index, bodyStart = start + definition[0].length - 1;
  let opened = false, depth = 0, state = 'code';
  for (let i = bodyStart; i < source.length; i++) {
    const c = source[i], next = source[i + 1];
    if (state === 'line') { if (c === '\n') state = 'code'; continue; }
    if (state === 'block') { if (c === '*' && next === '/') { state = 'code'; i++; } continue; }
    if (state === 'string' || state === 'char') { if (c === '\\') i++; else if (c === (state === 'string' ? '"' : "'")) state = 'code'; continue; }
    if (c === '/' && next === '/') { state = 'line'; i++; continue; }
    if (c === '/' && next === '*') { state = 'block'; i++; continue; }
    if (c === '"' || c === "'") { state = c === '"' ? 'string' : 'char'; continue; }
    if (c === '{') { opened = true; depth++; }
    if (c === '}' && --depth === 0 && opened) return source.slice(start, i + 1);
  }
  throw Error('Unterminated function ' + name);
}
const structStart = source.indexOf('struct NativeKeyboardShortcutRule {');
const structEnd = source.indexOf('static bool nativeKeyboardShortcutMaskBit(', structStart);
assert(structStart >= 0 && structEnd > structStart);
const hasWmi = source.includes("struct MsiWmiClickReceipt {");
const hasProducer = source.includes("struct OemProducerEdgeReceipt {");
const hasRogClick = source.includes("static void nativeRogClickDispatch(");
const hasReadSlots = source.includes("struct OemHidReadSession {");
const functions = [...(source.includes('static bool nativeDesktopChordSourcesAvailable(')?['nativeDesktopChordSourcesAvailable','nativeDesktopChordCaptureInterested']:[]),...(source.includes('static bool nativeDesktopMouseEvent(')?['nativeDesktopMouseKey','nativeDesktopChordInputKey','nativeDesktopChordInputAvailable','nativeDesktopMouseEvent']:[]),...(source.includes('static bool nativeDesktopChordCapture(')?['nativeDesktopChordKeyboardKey','nativeDesktopChordPhysicalKeys','nativeDesktopChordCancel','nativeDesktopChordMaintain','nativeDesktopChordCapture','nativeDesktopChordConsumeEligible','nativeDesktopChordConsumeCandidate','nativeDesktopChordConsumeComplete']:[]),...(source.includes('static bool nativeDesktopFunctionCapture(')?['nativeDesktopFunctionPhysicalKeys','nativeDesktopFunctionCancel','nativeDesktopFunctionMaintain','nativeDesktopFunctionCapture','nativeDesktopFunctionDrain']:[]),...(hasReadSlots ? ['oemHidReadTakeCompletion','oemHidReadFinalizeClose','oemHidReadServiceClosing','oemHidReadRequestClose','oemHidReadCanBind','oemHidReadCanBegin','legionBackStartRead','legionSBackStartRead','gpdBackStartRead','rogOemKeyStartRead','oemVendorHidShutdown','legionBackBind','legionBackSetEnabled','legionBackPump','legionSBackBind','legionSBackSetEnabled','legionSBackPump','gpdBackBind','gpdBackSetEnabled','gpdBackPump','rogOemKeyBind','rogOemKeySetEnabled','rogOemKeyPump','legionBackIsPid','legionSBackIsPid','gpdBackIsPid'] : []),'inputHostTargetKey', 'effectiveRuntimeInputSettings', 'inputShortcutRuntimeSnapshot', 'clearInputShortcutRuntime',
  'nativeKeyboardShortcutMaskBit', 'nativeKeyboardShortcutVirtualKey',
  'nativeKeyboardShortcutDuration', 'nativeKeyboardShortcutRequiredMask',
  'oemBackHookVkBit', 'oemChordEvalRules', 'oemBackHookRecompute', 'oemRawPathReset', ...(source.includes('static uint64_t oemShortcutEdgeToken(') ? ['oemShortcutEdgeToken', 'oemShortcutMessageTick', 'oemShortcutEdgeCurrent', 'oemShortcutPostPhysicalEdge'] : []),
  ...(source.includes('struct OemKeyboardConsumeState {') ? ['oemKeyboardConsumeSlot', 'oemKeyboardPhysicalMask', 'oemKeyboardConsumeSeedPassedKeys', 'oemKeyboardConsumeBusy', 'oemKeyboardConsumeEligibleMask', 'oemKeyboardConsumeCandidateBits', 'oemKeyboardConsumeReplay', 'oemKeyboardConsumeMaintain', 'oemKeyboardConsumeBuffer', 'oemKeyboardConsumeEvent'] : []),
  ...(source.includes('struct OemMouseConsumeState {') ? ['oemMouseConsumeSeedPassedKeys','oemMouseConsumeEligibleMask','oemMouseConsumeBusy','oemMouseConsumeReplay','oemMouseConsumeMaintain','oemMouseConsumeBuffer','oemMouseConsumeEvent'] : []),
  'oemBackHookProc', 'oemBackMouseHookProc', ...(source.includes('struct OemKeyboardConsumeState {') ? ['oemBackMappingReconcileOnUi', 'oemBackMappingSetEnabled'] : []),
  'nativeInputShortcutDesktopKey', 'nativeInputShortcutDesktopKeys',
  ...(hasWmi ? ['msiWmiGenerationCurrent','msiWmiShortcutAuthoritative','msiWmiWithdrawGeneration','msiWmiTryPopClick','msiWmiHandleEvent','msiWmiSubscriptionComplete','msiWmiReapThreadLocked','msiWmiStartLocked','msiWmiStart','msiWmiSetEnabled','msiWmiPump','nativeOemKeyMask','nativeKeyboardShortcutDispatch','nativeMsiWmiShortcutDrain'] : []),
  ...(hasProducer ? ['oemProducerName','oemProducerCurrent','oemProducerAuthorityMask','oemProducerTokensCurrent','oemProducerTokensFor','oemProducerBegin','oemProducerCancel','oemProducerActionMask','oemProducerPhysicalMask','oemProducerPhysicalBackState','oemProducerPublish',...(hasRogClick ? ['oemProducerPublishRogClick','nativeRogClickDispatch','rogOemKeyParse','rogOemKeyClose','rogOemKeyExpirePulse'] : []),'oemProducerTryPop','nativeOemKeyboardActionMask','nativeOemProducerDrain','oemBackMergedKeyMask','oemBackMergedDisplayState','oemLiveBackBits','oemHidClear','legionBackParse','legionSBackParse','gpdBackParse','onexBackParse','legionBackClose','legionSBackClose','gpdBackClose','onexBackClose','onexBackFinalizeClose','onexBackRetireStuck','onexBackRetireReadStuck','onexBackRequestReinit','msiDinputShortcutCancel','msiDinputShortcutBeginRead','msiDinputShortcutPublish'] : []),
  ...(source.includes('static bool msiQsKeyboardMirrorSourceCurrent(')?['msiQsKeyboardMirrorSourceCurrent']:[]), 'nativeInputShortcutToggle', ...(source.includes('static bool nativeYmccShortcutAction(') ? ['nativeYmccShortcutAction', 'nativeYmccShortcutDelta', 'nativeYmccShortcutSubmit', 'nativeYmccShortcutSummon', 'nativeFrontendButtonTargetEpoch', 'nativeFrontendButtonReceipt', 'nativeFrontendButtonSendGamebar', 'nativeYmccShortcutEmit'] : []), ...(source.includes('static int nativeShortcutLegacyDefaultSlot(') ? ['nativeShortcutLegacyDefaultSlot','nativeShortcutLegacyTemplateMatches','nativeShortcutLegacyDefaultAllowed'] : []),
  ...(source.includes('static std::vector<std::wstring> oemVendorServiceNamesForFamily(') ? ['oemVendorServiceNamesForFamily','oemVendorStackFamilySupported','oemVendorStackFrontMask','oemVendorStackRuleCanTakeover'] : []), 'gamepadUiOutputIsolationEligible', 'nativeKeyboardShortcutsRefresh', ...(source.includes('static void nativeOemKeyboardConsumePublish(') ? ['nativeOemKeyboardConsumePublish', 'nativeOemKeyboardConsumeMaintain'] : []), 'nativeKeyboardShortcutsEvaluate', ...(source.includes('static void nativeKeyboardShortcutHandleOemEdge(') ? ['nativeKeyboardShortcutHandleOemEdge'] : [])];
if(keyboardWorkerCases)functions.push('nativeKeyboardShortcutInputFlags');
if(source.includes('struct OemVirtualControllerSample {'))functions.push('msiDinputMainSourceEligible','msiDinputInvalidateCanonicalFrame');
if (virtualRearOnly) functions.push('focusIsolationBarrierApplyBack','msiBackkeyEffectiveBackBits');
if (source.includes('static unsigned long long oemVirtualBackBits(')) {
  functions.push('oemVirtualBackBits');
  if(source.includes('static void nativeOemVirtualConsumeGesture('))functions.push('nativeOemVirtualConsumeGesture');
  else functions.push('nativeOemVirtualPolicyPublish','nativeOemVirtualReserveFrameLocked');
  if(source.includes('static void nativeOemVirtualConsumeMatched('))
    functions.push('nativeOemVirtualConsumeMatched','nativeOemVirtualActionObservation');
}
if (source.includes('static bool oemVendorStackAutomaticTakeoverAllowed('))
  functions.push('oemVendorStackAutomaticTakeoverAllowed');
const familyStart = source.indexOf('enum class YmccFamily');
const familyEnum = source.slice(familyStart, source.indexOf('};',familyStart)+2);
const catalogDeclarations = source.slice(source.indexOf('enum class OemKeyForm'),source.indexOf('static OemCatalogRef oemCatalogForFamily('));
functions.push('legionHcInputReportView','oemCatalogForFamily');
const hasOneXBindingConsumer=['oemChordProfileIdFor','oemUnifiedInputBindingFor','onexCapDescriptorFor','oemProducerPublishOnexX2Home'].every(name=>source.includes(name+'('));
if(hasOneXBindingConsumer)functions.push('oemChordProfileIdFor','oemUnifiedInputBindingFor','onexCapDescriptorFor','oemProducerPublishOnexX2Home');
if(nativeGateDependencies){
  assert(hasOneXBindingConsumer,'Gate transition selection requires actual unified binding');
  functions.push('oemBackMappingRefreshGate','oemBoundChordProfileIdFor','ymccIsRogFamily','ayaOemKeyboardEvidenceBindingFor');
  if(source.includes('static uint64_t rogOemKeyboardFallbackMask() {'))functions.push('rogOemKeyboardFallbackMask');
}
if(cases==='onex-consumption')assert(hasOneXBindingConsumer,'Selected OneX group requires the actual OneX capability/publication consumer; unsupported sources are not a pass');
for (const name of ['nativeKeyboardShortcutAllInputs','nativeKeyboardShortcutClickMatches',
  'oemVendorTaskNamesForFamily','oemVendorProcessNamesForFamily','onexX2HomeDisplayMask','nativeOnexX2HomeDispatch',
  'nativeShortcutFeedback','nativeShortcutUnixMs','nativeShortcutUiDispatch','nativeShortcutTdpEvent','nativeShortcutTdpJob',
  'deckySidebarSteamMenuCompleted','nativeSteamMenuButton','nativeFrontendButtonNoVirtual','nativeFrontendButtonSendCtrl1','nativeSteamShortcutEmit']) {
  if (new RegExp('^static [^\\n]*\\b' + name + '\\(', 'm').test(source)) functions.push(name);
}
// Exact production types and final output expressions, consumed by this original fixture.
function shieldStruct(name) {
  const match = source.match(new RegExp(String.raw`^struct ${name} \{[\s\S]*?^\};`, 'm'));
  assert(match, 'Missing shield type ' + name); return match[0];
}
// Copy the actual revision fence, excluding only the OS event transport payload.
// The real current-power helper below runs against the existing fixture job lane.
const hasSerialRevisionFence = source.includes('static unsigned long long captureRevision() noexcept {');
const serialRevisionPrelude = hasSerialRevisionFence ? (() => {
  const declaration = shieldStruct('GamepadSerialEvent');
  const begin = declaration.indexOf('    static inline std::atomic<unsigned long long> ConfigurationRevision');
  const actionPower = source.match(/^static thread_local unsigned long long g_gamepadSerialActionPower[^\r\n]*/m);
  assert(begin >= 0 && actionPower, 'Missing actual serial revision/power declarations');
  return 'struct GamepadSerialEvent {\n' + declaration.slice(begin) + '\n' + actionPower[0];
})() : '';
const screenSource = fs.readFileSync(path.join(root, 'native/screen_touchpads.h'), 'utf8');
const screenBegin = screenSource.indexOf('struct Contact {');
const screenEnd = screenSource.indexOf('// POINTER_INFO.dwTime', screenBegin);
assert(screenBegin >= 0 && screenEnd > screenBegin, 'Missing pure screen snapshot definitions');
const shieldDecisions = ['steamDeckUiNeutral','ps5UiNeutral','msiUiNeutral','virtualUiCovered','virtualUiNeutral','submitNeutral'].map(name => {
  const line = source.split('\n').find(line => line.trimStart().startsWith('const bool ' + name + ' ='));
  assert(line, 'Missing shield output decision ' + name); return line;
}).join('\n');
const effectiveBegin = source.indexOf('    FocusFrameContent effectiveContent =');
assert(effectiveBegin >= 0, 'Missing actual effective frame selection');
const shieldEffective = source.slice(effectiveBegin, source.indexOf(';', effectiveBegin) + 1);
const shieldPrelude = `
${virtualRearOnly ? ['kFocusBarrierBackShift','kFocusBarrierBackBits'].map(name => {
  const line=source.split('\n').find(line=>line.startsWith('static constexpr unsigned long long '+name+' ='));
  assert(line,'Missing actual rear barrier constant '+name);return line;
}).join('\n') : ''}
namespace ymcc::screenpads { using Json = json; ${screenSource.slice(screenBegin,screenEnd)} }
${source.match(/^enum class FocusDecision \{[^\n]+/m)[0]}
${shieldStruct('FocusPlan')}
${shieldStruct('FocusFrameContent')}
struct { bool enabled=true, virtualTargetEnabled=true; } g_realStickTest;
static std::atomic<bool> fixtureRadialOwner{false};
static bool radialHasInputOwner() { return fixtureRadialOwner.load(); }
static bool physicalReleaseWindowActive() { return false; }
${['inputHostNormalizeAxis','focusIsolationNeutralContent','focusIsolationAssembleFrame','gamepadUiInputEligible','steamDeckUiOwnsInput','ps5UiOwnsInput','msiUiOwnsInput','virtualUiOwnsInput','msiNativeForegroundUiEligible','msiForegroundIsolationUiEligible'].map(extract).join('\n')}
// The bound plan and source availability are memory inputs. Decision expressions
// and effective frame selection are verbatim production consumers.
static json fixtureShieldOutput(const FocusPlan& focusPlan, const FocusFrameContent& frameContent) {
${shieldDecisions}
    const bool semanticNeutral = submitNeutral;
${shieldEffective}
    return focusIsolationAssembleFrame(effectiveContent);
}
`;
const oneXModelClasses=new Set();
const ayaModelClasses=new Set();
for(const match of fs.readFileSync(path.join(root,'capabilities/hc-device-catalog.generated.h'),'utf8').matchAll(/\{"hc\.device\.[^"]+", "([^"]+)", "([^"]+)"/g))
 for(const cls of [match[1],match[2]]){
  if(match[2].includes('OneXPlayer'))oneXModelClasses.add(cls);
  if(match[2].includes('AYANEO'))ayaModelClasses.add(cls);
 }
for(const cls of ['OneXPlayerX2','OneXPlayer3','OneXPlayerX2MiniPro'])oneXModelClasses.add('HandheldCompanion.Devices.'+cls);
// The existing resolver explicitly supports reviewed extensions absent from the
// frozen HC table. Enumerating that consumer must not invent a model record.
for(const match of extract('oemChordProfileIdFor').matchAll(/"(HandheldCompanion\.Devices\.AYANEO[^"]+)"/g))ayaModelClasses.add(match[1]);
const prelude = `${capabilityPreamble}${hasReadSlots ? "#define YMCC_HID_READ_SLOT_SOURCE\n" : ""}${hasRogClick ? "#define YMCC_ROG_CLICK_SOURCE\n" : ""}${hasProducer ? "#define YMCC_PRODUCER_EDGE_SOURCE\n" : ""}${hasWmi ? "#define YMCC_WMI_CLICK_SOURCE\n" : ""}${source.includes("static void nativeKeyboardShortcutHandleOemEdge(") ? "#define YMCC_SHORTCUT_EDGE_SOURCE\n" : ""}${source.includes("bool controllerAllowed = true") ? "#define YMCC_SHORTCUT_SOURCE_SCOPES\n" : ""}${source.includes("g_msiQsKeyboardMirrorPolicy")?"#define YMCC_MSI_QS_MIRROR_SOURCE":""}
#define YMCC_SHORTCUT_FIXTURE
${source.includes('static bool nativeDesktopChordCapture(')?'#define YMCC_RECORDED_CHORD_SOURCE':''}
${nativeGateTestMode ? '#define YMCC_SHORTCUT_GATE_CASE_MODE' : ''}
${cases === 'frontend-buttons' ? '#define YMCC_SHORTCUT_FRONTEND_CASE_MODE' : ''}
${['native-oem-keyboard-suppression','native-desktop-function','native-desktop-function-boundaries','native-desktop-function-final','native-desktop-function-final-tail','native-desktop-function-proof','native-desktop-function-proof-tail','native-desktop-function-proof-final','native-oem-aya-consumption','native-oem-aya-extensions','native-oem-gate-transitions','native-oem-gate-transitions-tail'].includes(cases) ? '#define YMCC_SHORTCUT_NATIVE_OEM_CASE_MODE' : ''}
${source.includes('static json nativeSteamMenuButton(') ? '#define YMCC_STEAM_MENU_DIRECT_SOURCE' : ''}
${hasOneXBindingConsumer ? "#define YMCC_SHORTCUT_ONEX_BINDING_SOURCE" : ""}
${hasOneXBindingConsumer && cases === "onex-consumption" ? "#define YMCC_SHORTCUT_ONEX_CASE_MODE 1" : hasOneXBindingConsumer && cases === "all" ? "#define YMCC_SHORTCUT_ONEX_CASE_MODE 0" : ""}
${shieldMode === undefined ? "" : "#define YMCC_SHORTCUT_SHIELD_CASE_MODE " + shieldMode}
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
${cases==='native-recorded-keyboard-chord'&&source.includes('static void nativeDesktopChordMaintain() {')&&extract('nativeDesktopChordMaintain').includes('const bool cancel=')?'#define YMCC_SHORTCUT_CHORD_MAINTAIN_DEMAND':''}
${cases==='native-desktop-function'&&source.includes('nativeDesktopFunctionPhysicalKeys()')&&extract('nativeDesktopFunctionPhysicalKeys').includes('vk<=VK_F24')?'#define YMCC_SHORTCUT_FUNCTION_SUBSET':''}
${cases==='native-desktop-function'&&source.includes('nativeDesktopFunctionCapture(')&&extract('nativeDesktopFunctionCapture').includes('!g_oemKeyboardConsume.physicalDown[slot^256u]')?'#define YMCC_SHORTCUT_FIFO_REJECTED_NEW_DOWN':''}
${source.includes('nativeDesktopChordPhysicalKeys()')&&extract('nativeDesktopChordPhysicalKeys').includes('_mm_movemask_epi8')?'#if defined(_M_X64) || defined(__x86_64__)\n#include <emmintrin.h>\n#endif':''}
${cases==='native-recorded-keyboard-chord'&&source.includes('nativeDesktopChordPhysicalKeys()')&&extract('nativeDesktopChordPhysicalKeys').includes('_mm_movemask_epi8')?'#define YMCC_SHORTCUT_SNAPSHOT_BLOCK':''}
#include <windows.h>
#include <Xinput.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <algorithm>
#include <thread>
#include <array>
#include <wbemidl.h>
#include <atomic>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "input_shortcut_toggle.h"
#include "shortcut_window_toggle.h"
#include "shortcut_frontend_button.h"
#include "shortcut_action_protocol.h"
#include "win5_menu_shortcut_policy.h"
#include "msi_key_event_class.h"
#include "log_snapshot_state.h"
#include "keyboard_shortcut_sequence.h"
#include "keyboard_modifier_event_evidence.h"
#include "screen_special_actions.h"
#include "virtual_target_policy.h"
#include "controller_persona.h"
#include "boot_mode_shortcuts.h"
#include "radial_release.h"
#include "virtual_start_protection.h"
#include "legion_motion_activation.h"
// Motion activation is a separate module; never perform its device I/O here.
namespace ymcc::legion_activation { struct Win32Port {
 DWORD error=0;bool open(HANDLE,unsigned){throw std::runtime_error("MOTION_ACTIVATION_IO_FORBIDDEN_IN_SHORTCUT_FIXTURE");}
 template<class Command>Completion submit(const Command&){throw std::runtime_error("MOTION_ACTIVATION_IO_FORBIDDEN_IN_SHORTCUT_FIXTURE");}
 Completion poll(){return Completion::Failed;}void cancel(){}bool release(){return true;}
}; }
#include <chrono>
#include <cstring>
${source.includes("g_msiQsKeyboardMirrorPolicy")?'#include "msi_qs_keyboard_mirror.h"':''}
${source.match(/enum OemVkBit : uint32_t \{[\s\S]*?\};/)[0]}
${source.slice(source.indexOf("static constexpr uint64_t kOemBitM1"), source.indexOf("// ── GP-FAMILY-CAPABILITY-4 C4-D2"))}
${source.slice(source.indexOf("struct OemChordRule {"), source.indexOf("static uint32_t oemBackHookVkBit("))}
using json = nlohmann::json;
static std::atomic<bool> g_oemInputCaptureOnly{false},g_oemInputCaptureInitRequested{false};
static std::string inputHostTargetKey(const json&);
// Lifecycle admission is an inert external port in this shortcut-only fixture;
// the actual VirtualStartProtection consumer has independent lifecycle tests.
static ymcc::VirtualStartProtection::Admission inputHostTargetAdmission(
    const std::string&,const std::string&,const char*) { return {true,0,false}; }
${familyEnum}
static const std::vector<std::string>fixtureOneXClasses=${JSON.stringify([...oneXModelClasses]).replace(/^\[/,"{").replace(/\]$/,"}")};
static const std::vector<std::string>fixtureAyaClasses=${JSON.stringify([...ayaModelClasses]).replace(/^\[/,"{").replace(/\]$/,"}")};
static struct {YmccFamily family=YmccFamily::Unknown;std::string deviceClass;bool resolved=false;}g_machineIdentity;
${catalogDeclarations}
static json fixtureInput;
static const json fixtureFieldInput = json::parse(R"FIELD(${JSON.stringify(fieldInput)})FIELD");
static std::vector<json> fixtureLogs;
static std::vector<std::pair<WORD, bool>> fixtureInjections;
static std::set<WORD> fixtureDesktopDown;
static std::vector<std::string> fixtureOwnActions;
static std::vector<int> fixtureDeltas;
static std::mutex g_gamepadSettingsMx;
static std::string g_mouseBackend = "native";
static std::atomic<bool> g_inputReady{true};
static HWND g_hwnd = reinterpret_cast<HWND>(1);
static std::atomic<bool> g_fullHeight{false};
static bool fixtureWindowVisible = true, fixtureWindowIconic = false, fixtureWindowZoomed = false, fixtureWindowForeground = false;
static BOOL fixtureIsWindow(HWND h) { return h != nullptr; }
static BOOL fixtureIsWindowVisible(HWND) { return fixtureWindowVisible; }
static BOOL fixtureIsIconic(HWND) { return fixtureWindowIconic; }
static BOOL fixtureIsZoomed(HWND) { return fixtureWindowZoomed; }
#define IsWindow fixtureIsWindow
#define IsWindowVisible fixtureIsWindowVisible
#define IsIconic fixtureIsIconic
#define IsZoomed fixtureIsZoomed
static bool focusMainWindowIsForeground() { return fixtureWindowForeground; }
static ymcc::FrontendButtonPulse g_frontendButtonPulse;
static std::atomic<unsigned long long> g_inputHostEpochMirror{1};
static std::string fixtureFrontendPersona = "disabled";
using InputHostMutex = std::mutex;
static InputHostMutex g_inputHostMx;
static HANDLE g_inputHostProcess = reinterpret_cast<HANDLE>(1);
static bool g_inputHostPrepared = true, g_inputHostNeutralized = true, g_inputHostTransportFault = false;
static std::atomic<bool> g_inputHostFaultHoldsStart{false};
static std::string g_inputHostBoundPersona = "disabled";
static const char* inputHostPersona() { return ${virtualRearOnly?'ymcc::controllerPersona::runtime(fixtureFrontendPersona)':'fixtureFrontendPersona.c_str()'}; }
static std::string inputHostBoundPersonaValue() { return g_inputHostBoundPersona; }
static bool fixtureWriterAllowed = true;
static bool sgVirtualWriterAllowedNow() { return fixtureWriterAllowed; }
static UINT fixtureSendCount = UINT_MAX;
static std::vector<std::vector<INPUT>> fixtureSendInputs;
static unsigned long long g_summonGeneration = 0;
${hasWmi ? 'static ULONGLONG fixtureTick = 10000;\n'+(keyboardWorkerCases?'':'static ULONGLONG fixtureTickCount64() { return fixtureTick; }\n#define GetTickCount64 fixtureTickCount64') : ''}
struct FocusTargetSnapshot { bool valid = true; unsigned long long token = 0; };
static FocusTargetSnapshot g_pendingSummonTarget, fixtureCapturedTarget;
static std::vector<FocusTargetSnapshot> fixturePreparedTargets;
static FocusTargetSnapshot focusCaptureForegroundTarget() { return fixtureCapturedTarget; }
// The overlay carrier is a separate UI resource; keep this shortcut fixture on
// its existing ordinary-window memory boundary, without starting a real carrier.
${source.includes('g_gameOverlay.active()') ? `
static struct { bool active()const{return false;} void requestCheck(){} } g_gameOverlay;
static bool gameOverlayInputEligible(){return false;}
static bool gameOverlayOutputOwns(){return false;}
static void gameOverlayStop(const char*){}
static void focusEndIsolationOnUi(){}
${source.includes('static HWND mainUiPresentationWindow()')?'static HWND mainUiPresentationWindow(){return g_hwnd;} // ordinary-window memory boundary; no overlay runtime':''}
enum class GameOverlayAttempt { Ordinary, Begun, Refused };
template<class Target>static GameOverlayAttempt gameOverlayTryBegin(const Target&,bool){return GameOverlayAttempt::Ordinary;}
static ymcc::ShortcutWindowVisibility readShortcutWindowVisibility(HWND h){return {IsWindowVisible(h)!=FALSE,IsIconic(h)!=FALSE};}
static bool shortcutSummonRenderBusy(){return false;}
template<class Target>static bool shortcutSummonWaitForRender(const Target&,bool){return false;}
static void shortcutSummonClearRenderWait(){}
` : ''}
// Windows/focus/timer edges of actual bringToFront are all inert memory ports.
${shieldStruct('FocusSessionState')}
static FocusSessionState g_focusSession;
static unsigned long long g_focusSessionSequence = 0;
static std::atomic<unsigned long long> g_summonRegistrationGeneration{0};
static std::atomic<bool> g_focusIsolationPrepared{false}, g_focusIsolationGameOpen{false};
// These inputs represent OS/game-observation and Steam lease proofs, not a
// successful real focus handoff. The original all suite defaults to no proof.
static bool fixtureFocusGameOpen=false, fixtureFocusNoGameQualified=false;
static bool fixtureFocusIsolationCurrent=false, fixtureSteamLightAttemptActive=false, fixtureSteamLightFallbackRequired=false;
static HWND fixtureExternalForeground = reinterpret_cast<HWND>(2);
static HWND fixtureGetForegroundWindow() { return fixtureWindowForeground ? g_hwnd : fixtureExternalForeground; }
#define GetForegroundWindow fixtureGetForegroundWindow
static void focusPrepareIsolationBeforeShow() {
    const bool first = !g_focusIsolationPrepared.exchange(true, std::memory_order_acq_rel);
    if (first || fixtureFocusGameOpen) g_focusIsolationGameOpen.store(fixtureFocusGameOpen, std::memory_order_release);
}
static bool focusLightNoGameQualified(ULONGLONG) { return fixtureFocusNoGameQualified && !g_focusIsolationGameOpen.load(std::memory_order_acquire); }
static bool focusLightIsolationCurrent() { return fixtureFocusIsolationCurrent && !g_focusIsolationGameOpen.load(std::memory_order_acquire); }
static bool steamLightGamepadAttemptActive() { return fixtureSteamLightAttemptActive; }
static bool steamLightGamepadFallbackRequired() { return fixtureSteamLightFallbackRequired; }
${extract('focusLightMayRefocus')}
static void ipc_emit(const char*, const json&);
static bool fixtureFocusAllowed = true;
static int fixtureSummonTimerStarts = 0, fixtureSummonTimerKills = 0;
static int g_summonFocusRetries = 0;
static bool g_summonAltTried = false;
static constexpr UINT_PTR SUMMON_FOCUS_TIMER_ID = 0x5151;
static bool focusPrepareSummonSession() {
    focusPrepareIsolationBeforeShow();
    if (g_focusSession.lightNoGame && g_focusSession.active && !g_focusSession.returning &&
        focusMainWindowIsForeground() && focusLightIsolationCurrent()) return true;
    fixturePreparedTargets.push_back(g_pendingSummonTarget);
    ++g_focusSessionSequence;
    if (g_pendingSummonTarget.valid || !focusMainWindowIsForeground()) {
        g_focusSession={}; g_focusSession.target=g_pendingSummonTarget;
    }
    g_pendingSummonTarget={};
    g_focusSession.lightNoGame=focusLightNoGameQualified(GetTickCount64());
    g_focusSession.strictReturnOnly=g_focusSession.lightNoGame;
    g_focusSession.lightHandoffAttempted=g_focusSession.lightForegroundAcquired=false;
    g_focusSession.summonOrigin=GetForegroundWindow();
    g_focusSession.summonDeadline=GetTickCount64()+1200; g_focusSession.summonNextAt=GetTickCount64()+80;
    g_focusSession.active=true; g_focusSession.returning=false;
    g_focusSession.returnStarted=g_focusSession.returnDeadline=0;
    return true;
}
static void focusReflowMainWindow() {}
static void showWindowAnimated(HWND, int command, bool) {
    fixtureOwnActions.push_back("window.summon");
    fixtureWindowVisible=true; fixtureWindowIconic=false;
    if(command==SW_MAXIMIZE)fixtureWindowZoomed=true;
}
static BOOL fixtureSetWindowPos(HWND,HWND,int,int,int,int,UINT) { return TRUE; }
static BOOL fixtureKillTimer(HWND,UINT_PTR) { ++fixtureSummonTimerKills; return TRUE; }
static UINT_PTR fixtureSetTimer(HWND,UINT_PTR id,UINT,TIMERPROC) { ++fixtureSummonTimerStarts; return id; }
static bool refocusWebView(bool) { fixtureWindowForeground=fixtureFocusAllowed; return fixtureFocusAllowed; }
static void focusClearSession() {
    ++g_focusSessionSequence;
    const bool strictReturnOnly=g_focusSession.lightNoGame || g_focusSession.strictReturnOnly;
    g_focusSession={}; g_focusSession.strictReturnOnly=strictReturnOnly;
    g_summonRegistrationGeneration.store(0, std::memory_order_release);
    g_pendingSummonTarget={}; g_summonFocusRetries=0; g_summonAltTried=false;
    fixtureFocusIsolationCurrent=false;
    fixtureKillTimer(g_hwnd,SUMMON_FOCUS_TIMER_ID);
}
static bool focusSummonRefocus(bool allowAltFallback) {
    const auto sequence=g_focusSessionSequence;
    if (!focusLightMayRefocus(GetTickCount64())) return false;
    const bool focused=refocusWebView(allowAltFallback && !g_focusSession.lightNoGame);
    if (sequence!=g_focusSessionSequence) return false;
    if (focused && g_focusSession.lightNoGame) g_focusSession.lightForegroundAcquired=true;
    return focused;
}
#define SetWindowPos fixtureSetWindowPos
#define KillTimer fixtureKillTimer
#define SetTimer fixtureSetTimer
${extract('bringToFront')}
static json nativeSummonCandidateSnapshot(const FocusTargetSnapshot&, unsigned long long) { return {}; }
static void queueSummonGameRegistration(const FocusTargetSnapshot&, unsigned long long) {}
static void focusBeginReturnToPreviousWindow(ULONGLONG) { fixtureOwnActions.push_back("window.hideToTray"); fixtureWindowVisible=false; fixtureWindowForeground=false; }
static void returnToDesktop() { fixtureOwnActions.push_back("os.returnDesktop"); }
static void runKillBat() { fixtureOwnActions.push_back("game.killCurrent"); }
// Game close is an unrelated OS-effect boundary for this input fixture.
static json nativeCaptureGameCloseTarget() { return json::object(); }
static void runKillBat(const json&) { fixtureOwnActions.push_back("game.killCurrent"); }

static void radialSummonRequest(const char*,int=0,int=0) { fixtureOwnActions.push_back("radial.summon"); }
static void openTouchKeyboard() { fixtureOwnActions.push_back("os.openTouchKeyboard"); }
static void toggleMouseMode(const std::string&) { fixtureOwnActions.push_back("mouse.toggle"); }
static bool hardwareWriteAllowed() { return true; }
static void gamepadSerialPostEvent(const char* event, const json& payload) { fixtureOwnActions.push_back(event); fixtureDeltas.push_back(payload.value("delta", 0)); }
static void nativeApplyBrightness(int delta, bool, int) { fixtureOwnActions.push_back("display.brightnessAdjust"); fixtureDeltas.push_back(delta); }
static uint64_t fixtureOemMask = 0;
static bool fixtureAssets = true;
static bool fixtureJobAccept = true;
static int fixtureRefreshes = 0, fixtureJoyxoffCloses = 0;
static std::atomic<bool> g_exitRequested{false}, g_shortcutRecordingActive{false};
static std::atomic<uint64_t> g_shortcutRecordingOemMask{0};
static std::atomic<bool>g_controllerFrontendIsolationEnabled{true};
static bool gamepadUiOutputIsolationEligible();
// Power, platform identity and evidence sinks are memory-only fixture boundaries.
enum class PowerLifecycle : uint8_t { Ready, Suspending, Suspended, Resuming };
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long fixturePowerGeneration = 1,fixturePublicationRevision=0;
static unsigned long long currentPowerGeneration() { return fixturePowerGeneration; }
static std::atomic<bool> g_inputReleaseRequired{false};
static std::atomic<bool> fixtureCustomLibraryOwner{false};
static bool customSteamLibraryInputSuppressed() { return fixtureCustomLibraryOwner; }
// Win5 Menu's physical source and recording diagnostic have their own fixture.
static std::atomic<bool> g_win5MenuRuleDemand{false};
static bool win5MenuBoard() noexcept { return false; }
static bool win5MenuActionSourceCurrent(const ymcc::win5Menu::Source&,uint64_t,uint64_t,uintptr_t) noexcept { return false; }
static void win5MenuRecordingDiagnostic(const char*,const char*) noexcept {}
#include "sleep_power_state.h"
// Existing memory power facts feed the shared ABI and its actual pure guards.
// No product observer, publication or sleep operation is started by this fixture.
static uint64_t fixtureNotificationSequence=0,fixtureSuspendSerial=0;
static int __cdecl fixtureSharedPowerRead(ymcc::sleeppower::SharedPowerV1* output, uint32_t bytes) {
    if (!output || bytes != sizeof(*output)) return 0;
    ymcc::sleeppower::SharedPowerV1 value;
    value.generation = fixturePowerGeneration;value.publicationRevision=fixturePublicationRevision;value.observedNotificationSequence=fixtureNotificationSequence;
    value.suspendSerial=fixtureSuspendSerial;
    value.phase = static_cast<uint32_t>(g_powerLifecycle.load(std::memory_order_acquire));
    value.flags = ymcc::sleeppower::kKnown;
    if (value.phase == 0 && !g_exitRequested.load(std::memory_order_acquire)) value.flags |= ymcc::sleeppower::kHardwareAllowed;
    *output = value; return 1;
}
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& output) noexcept {
    return ymcc::sleeppower::capturePower(fixtureSharedPowerRead, output);
}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& captured) noexcept {
    return ymcc::sleeppower::powerCurrent(fixtureSharedPowerRead, captured);
}

// Observation/recording ports do not own parser, evaluator or dispatch policy.
template<class... A>static void modelAdaptationChord(A&&...){}
template<class... A>static void modelAdaptationOnexReport(A&&...){}
template<class... A>static void modelAdaptationRogReport(A&&...){}
template<class... A>static void modelAdaptationProducer(A&&...){}
static void modelAdaptationFlush(){}
static void nativeOneXOemObserve(uint64_t,const char*){}
static void nativeShortcutRecordingStop(bool=true){throw std::runtime_error("RECORDING_LIFECYCLE_NOT_EXECUTED_IN_SHORTCUT_FIXTURE");}
static const char* inputHostMachineTargetRejectionReason(const std::string& persona) noexcept {
    return ymcc::input::virtualTargetPolicy::rejectionReason(false, persona);
}
static void focusShortcutInputEvidence(const char*, unsigned long long, bool) noexcept {}
// Recording notifications end at the same memory-only UI event boundary.
static std::vector<uint64_t> fixtureRecordedShortcutClicks;
static std::vector<uint64_t> fixtureRecordedShortcutChords;
static void gamepadRecordShortcutClick(uint64_t bit) { fixtureRecordedShortcutClicks.push_back(bit); }
static void gamepadRecordShortcutChord(uint64_t bits) { fixtureRecordedShortcutChords.push_back(bits); }
static std::atomic<bool> g_keyboardModifierHookInstalled{false}, g_inputSignalMouseHookInstalled{false};
static std::atomic<ULONGLONG> g_keyboardModifierHookObservedTick{0}, g_onexX2HomeDisplayUntil{0};
static void keyboardModifierObserveSelfSend(ymcc::keyboardEvents::SourceId,const INPUT*,UINT,UINT) noexcept {}
static void keyboardModifierObserveLowLevel(const KBDLLHOOKSTRUCT&,WPARAM,ymcc::keyboardEvents::HookObservation) noexcept {}
static void inputSignalObserveMouse(const MSLLHOOKSTRUCT&,WPARAM,bool) noexcept {}
static std::mutex g_inputShortcutRuntimeMx;
static ymcc::InputShortcutRuntime g_inputShortcutRuntime;
static const std::string& gameInputOwnerSession() { static const std::string s = "memory-session"; return s; }
static bool fixtureShortcutSettingsThrow=false;
static json ymSettingsSection(const char* section) { if (fixtureShortcutSettingsThrow || std::string(section) != "input") throw std::runtime_error("Fixture current settings read unavailable"); return fixtureInput; }
// In-memory settings seam; the production getter is checked by its real Win32 leaf.
static bool fixtureSettingsPrimaryRead = true;
static json ymSettingsFields(const char* section, std::initializer_list<const char*> fields, bool* primaryRead = nullptr) {
    if (primaryRead) *primaryRead=fixtureSettingsPrimaryRead;
    const auto input = ymSettingsSection(section);
    json selected = json::object();
    if (input.is_object()) for (const auto* key : fields) {
        const auto value = input.find(key);
        if (value != input.end()) selected[key] = *value;
    }
    return selected;
}
static std::shared_ptr<const json> ymSettingsSectionView(const char* section, bool* primaryRead=nullptr) {
    if (primaryRead) *primaryRead = fixtureSettingsPrimaryRead;
    return std::make_shared<const json>(ymSettingsSection(section));
}
static bool inputCaptureAssetsPresent() { return fixtureAssets; }
static void inputCaptureRefreshRealStickSettings(bool) { ++fixtureRefreshes; }
static void closeJoyXoffAsync(const char*) { ++fixtureJoyxoffCloses; }
static bool fixtureDesktopDiagnosticThrows=false;
static void appendNativeLifecycleLog(const char* event, const json& payload) {
    if(fixtureDesktopDiagnosticThrows&&payload.value("source",std::string{})=="explicit-keyboard-function")throw std::runtime_error("new optional diagnostic fault");
    auto row = payload; row["event"] = event; fixtureLogs.push_back(row); }
static std::vector<json> fixtureShortcutActionRequests;
static void ipc_emit(const char* name, const json& data) { if (std::string(name)=="input.shortcutAction") fixtureShortcutActionRequests.push_back(data); }
static void nativeKeyboardShortcutSend(WORD key, bool shift, bool = false) { fixtureInjections.emplace_back(key, shift); }
${hasWmi ? "static bool msiWmiShortcutAuthoritative();\nstatic uint64_t nativeOemKeyMask(bool forActions = false);" + (hasProducer ? "\nstatic uint64_t nativeOemKeyboardActionMask();\nstatic uint64_t oemProducerAuthorityMask();" : "") : "static uint64_t nativeOemKeyMask();"}
${source.includes("static bool msiQsKeyboardMirrorSourceCurrent(")?"static bool msiQsKeyboardMirrorSourceCurrent(uint64_t);":""}
static constexpr uint64_t kOemShortcutUnboundEpoch = UINT64_MAX;
${source.includes("struct OemKeyboardConsumeState {") ? `#define YMCC_OEM_CONSUME_SOURCE
${source.slice(source.indexOf("struct OemKeyboardConsumeState {"), source.indexOf("static unsigned oemKeyboardConsumeSlot("))}
${source.includes("struct OemMouseConsumeState {") ? "#define YMCC_OEM_MOUSE_CONSUME_SOURCE" : ""}
static std::atomic<bool> g_oemHooksWanted{true};
static void oemBackMappingReconcileOnUi();
static HHOOK g_oemBackHook = nullptr, g_oemMouseHook = nullptr;
static DWORD fixtureKernelThreadId(){return GetCurrentThreadId();}
static thread_local DWORD fixtureThreadId = 100;
static DWORD fixtureWindowThreadId = 100;
static DWORD fixtureCurrentThreadId() { return fixtureThreadId; }
static DWORD fixtureWindowThread(HWND, LPDWORD) { return fixtureWindowThreadId; }
#define GetCurrentThreadId fixtureCurrentThreadId
#define GetWindowThreadProcessId fixtureWindowThread
static int fixtureHookInstalls = 0, fixtureHookRemoves = 0;
static bool fixtureHookInstallOk = true;
static bool fixtureMouseHookInstallOk=true,fixtureKeyboardHookInstallOk=true;
static HHOOK fixtureSetHook(int type, HOOKPROC, HINSTANCE, DWORD) { ++fixtureHookInstalls; return fixtureHookInstallOk&&(type!=WH_KEYBOARD_LL||fixtureKeyboardHookInstallOk)&&(type!=WH_MOUSE_LL||fixtureMouseHookInstallOk) ? reinterpret_cast<HHOOK>(1) : nullptr; }
static BOOL fixtureUnhook(HHOOK) { ++fixtureHookRemoves; return TRUE; }
#define SetWindowsHookExW fixtureSetHook
#define UnhookWindowsHookEx fixtureUnhook
static void writeLog(const char*, const std::string&) {}
static int fixtureReconcilePosts = 0;
#define WM_OEM_HOOK_RECONCILE (WM_USER + 27)
static std::vector<INPUT> fixtureReplayed;
static UINT fixtureSendLimit = UINT_MAX;
static UINT fixtureSendInput(UINT count, LPINPUT input, int) {
    fixtureSendInputs.emplace_back(input, input + count);
    const UINT accepted = std::min(count, std::min(fixtureSendLimit, fixtureSendCount));
    for (UINT i = 0; i < accepted; ++i) fixtureReplayed.push_back(input[i]);
    return accepted;
}
#define SendInput fixtureSendInput
static int fixtureSystemMetrics(int code) { return code==SM_CXVIRTUALSCREEN?1920:code==SM_CYVIRTUALSCREEN?1080:0; }
#define GetSystemMetrics fixtureSystemMetrics
` : ""}
static uint64_t gamepadShortcutRecordingMask(WORD buttons, const XINPUT_GAMEPAD& pad) { return static_cast<uint64_t>(buttons) | (pad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 1ull << 16 : 0) | (pad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 1ull << 17 : 0); }
${virtualRearUnknown?'static bool fixtureOemVirtualAllocationFailure=false;':''}
${virtualRearGestures?'static std::function<void()> fixtureVirtualAsyncInterleave;':''}
static SHORT fixtureAsyncKeyState(WORD key) { ${virtualRearGestures?'if(fixtureVirtualAsyncInterleave){auto callback=std::move(fixtureVirtualAsyncInterleave);fixtureVirtualAsyncInterleave={};callback();}':''}return fixtureDesktopDown.count(key) ? static_cast<SHORT>(0x8000) : 0; }
#define GetAsyncKeyState fixtureAsyncKeyState
${hasWmi ? `
#define WM_MSI_WMI_CLICK (WM_USER + 28)
` : ""}
#define WM_DESKTOP_FUNCTION_EDGE (WM_USER + 32)
#define WM_OEM_SHORTCUT_EDGE (WM_USER + 26)
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{1}, g_oemShortcutEdgePostFailures{0};
static std::atomic<uint32_t> g_gpdBackIfaceGen{1};
static std::atomic<bool> g_gpdBackIfaceReady{true}, g_oemKeyHooksActive{true};
static std::atomic<bool> g_oemRawPathActive{false};
static std::atomic<bool> g_oemRawPathWanted{false};
#include "desktop_function_shortcut.h"
#include "input_frame_submission.h"
static std::array<ymcc::input::desktopFunction::Press,12> g_desktopFunctionPresses{};
static std::atomic<uint32_t>g_desktopFunctionPhysicalKeys{0},g_desktopFunctionLlOwned{0};
static ymcc::input::desktopFunction::EdgeQueue g_desktopFunctionEdges;
static std::atomic<uint32_t>g_desktopFunctionCaptureMask{0},g_desktopFunctionArmedMask{0},g_desktopFunctionKeys{0};
static std::atomic<bool>g_desktopFunctionHookActive{false};
${source.includes('static bool nativeDesktopChordCapture(')?`static std::atomic<bool>g_desktopChordWanted{false};
static ymcc::input::desktopFunction::KeyboardKeys g_desktopChordCaptureKeys,g_desktopChordKeys,g_desktopChordArmedKeys,g_desktopChordLlOwned;
static std::vector<ymcc::input::desktopFunction::KeyboardChord> g_desktopChordRules;
static ymcc::sleeppower::SharedPowerV1 g_desktopChordPower;
static uint32_t g_desktopChordEpoch=0;static bool g_desktopChordBlocked=false;`:''}
static uint32_t g_desktopFunctionRuleEpoch=0;
static std::atomic<uint32_t>g_desktopFunctionPostFailures{0},g_desktopFunctionConsumed{0};
static std::atomic<int> g_oemBackProfileId{4};
static std::atomic<uint64_t> g_oemVirtualKeyboardConsumed{0};
static std::atomic<int> g_oemVirtualKeyboardProfile{-1};
static std::atomic<uint64_t> g_oemKeyMask{0}, g_oemKeyMaskHid{0}, g_oemKeyMaskRog{0}, g_oemKeyMaskMsi{0}, g_oemKeyMaskMsiDinput{0};
${hasProducer ? source.slice(source.indexOf("enum class OemProducerId"), source.indexOf("static const char* oemProducerName(")) : ""}
${hasWmi ? source.slice(source.indexOf("static constexpr int kMsiWmiEventClaw"), source.indexOf("static void physicalReleaseWindowToggleRequest(const char* source);", source.indexOf("static constexpr int kMsiWmiEventClaw"))).replace("static std::atomic<uint64_t> g_oemKeyMaskMsi{0};", "") + `
static int fixtureWmiCreates = 0, fixtureWmiCloses = 0, fixtureWmiWakePosts = 0, fixtureLegacyClaw = 0;
static bool fixtureWmiTerminal = false, fixtureWmiCreateOk = true;
static DWORD fixtureLastWaitMs = UINT_MAX;
static DWORD WINAPI msiWmiThreadProc(LPVOID) { return 0; } // only external WMI subscription is mocked
static DWORD fixtureWmiWait(HANDLE, DWORD ms) { fixtureLastWaitMs = ms; return fixtureWmiTerminal ? WAIT_OBJECT_0 : WAIT_TIMEOUT; }
static BOOL fixtureWmiClose(HANDLE) { ++fixtureWmiCloses; return TRUE; }
static HANDLE fixtureWmiCreate(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID context, DWORD, LPDWORD) {
    ++fixtureWmiCreates;
    if (!fixtureWmiCreateOk) return nullptr;
    delete static_cast<uint64_t*>(context); fixtureWmiTerminal = false; // no real thread is started
    return reinterpret_cast<HANDLE>(0x101);
}
static int fixtureLegacyRog = 0;
static void physicalReleaseWindowToggleRequest(const char* from) { if (std::string(from)=="oem-claw") ++fixtureLegacyClaw; else ++fixtureLegacyRog; }
static void nativeMsiWmiShortcutDrain();
#define WaitForSingleObject fixtureWmiWait
#define CloseHandle fixtureWmiClose
#define CreateThread fixtureWmiCreate
` : ""}
${hasReadSlots ? source.slice(source.indexOf("struct OemHidReadSession {"),source.indexOf("static OemHidReadCompletion oemHidReadTakeCompletion(")) : ""}
${hasRogClick ? source.slice(source.indexOf("struct RogOemKeySession "),source.indexOf("static std::atomic<uint64_t> g_oemKeyMaskRog")) + source.slice(source.indexOf("static constexpr BYTE kRogKeyEventAc"),source.indexOf("static void rogOemKeyClose() {",source.indexOf("static constexpr BYTE kRogKeyEventAc"))) : ""}
${hasProducer ? `
#define WM_OEM_PRODUCER_EDGE (WM_USER + 29)
static int fixtureProducerPosts = 0, fixtureCancelCalls = 0;
static bool fixtureIoCancelOk=true; static DWORD fixtureIoCancelError=ERROR_SUCCESS;
static BOOL fixtureCancelIoEx(HANDLE, LPOVERLAPPED) { ++fixtureCancelCalls; if(!fixtureIoCancelOk) SetLastError(fixtureIoCancelError); return fixtureIoCancelOk; }
#define CancelIoEx fixtureCancelIoEx
static std::atomic<int> g_oemBackState{0}, g_msiDinputBackBits{0};
static std::atomic<uint64_t> g_oemRearMapToL5{0}, g_oemRearMapToR5{0};
static uint64_t fixtureRealOemBackMergedKeyMask();
static void nativeOemProducerDrain();
` + source.slice(source.indexOf("struct LegionBackSession "), source.indexOf("static bool legionBackIsPid(")) +
source.slice(source.indexOf("struct LegionSBackSession "), source.indexOf("static bool legionSBackIsPid(")) +
source.slice(source.indexOf("struct GpdBackSession "), source.indexOf("static bool gpdBackIsPid(")) +
source.match(/static constexpr BYTE kLegionSBackBitLeft[^\n]*\n/)[0] +
source.match(/static constexpr BYTE kLegionSBackBitRight[^\n]*\n/)[0] +
source.match(/static constexpr BYTE kGpdBackMask[^\n]*\n/)[0] +
source.slice(source.indexOf("static constexpr BYTE kOnexBackFrame"), source.indexOf("static OnexCapDescriptor onexCapDescriptorFor(")) +
source.slice(source.indexOf("struct OnexBackSession {"), source.indexOf("static OnexBackSession g_onexBack;")+"static OnexBackSession g_onexBack;".length) + `
// Device identity observation for the separately tested motion activation
// owner is a memory boundary. It cannot open or reconfigure a device here.
static std::string legionMotionResolveActivationIdentity(const std::wstring& path, SP_DEVINFO_DATA*, HDEVINFO) {
 return path.empty()?std::string{}:std::string("fixture-motion-identity");
}
static void legionMotionActivationTick(LegionBackSession&) {}
static void onexBackFinalizeClose();
static void rogOemKeyClose();
static void legionBackClose();
static void legionSBackClose();
static void gpdBackClose();
static std::atomic<bool> g_gpdBackEnabledSnap{false};
struct FixtureMsiDinput {
 uint64_t actionGeneration = 0;
 std::atomic<bool> enabled{true},mainInterfaceMatched{true},canonicalValid{true};
 std::atomic<unsigned> productId{0},canonicalSeq{0};
};
static FixtureMsiDinput g_msiDinput;
` : ""}
${hasReadSlots ? `
static const GUID kYeManGuidDeviceInterfaceHid={};
${source.slice(source.indexOf('static const USHORT kLegionBackPids[]'),source.indexOf('struct LegionBackSession '))}
${source.match(/static const USHORT kLegionSPids\[\][^\n]*\n/)[0]}
${source.match(/static const USHORT kGpdBackPids\[\][^\n]*\n/)[0]}
static std::string W2U(const std::wstring&) { return "fixture-hid"; }
struct FixtureRogControl { std::wstring controlPath=L"fixture-rog",inputPath;ULONG featureLen=64;bool hasControl=false,hasStableInput=false; };
static FixtureRogControl g_rog;
static int fixtureIoEnumCalls=0,fixtureIoOpenCalls=0,fixtureIoEventCreates=0;
static bool fixtureIoEnumAvailable=false,fixtureIoOpenOk=true,fixtureIoEventOk=true;
static USHORT fixtureIoVendor=0x17EF,fixtureIoProduct=0x6182,fixtureIoUsagePage=0xFFA0;
static HDEVINFO fixtureClassDevs(const GUID*,PCWSTR,HWND,DWORD){++fixtureIoEnumCalls;return fixtureIoEnumAvailable ? reinterpret_cast<HDEVINFO>(0x701) : INVALID_HANDLE_VALUE;}
static BOOL fixtureEnumInterfaces(HDEVINFO,PSP_DEVINFO_DATA,const GUID*,DWORD i,PSP_DEVICE_INTERFACE_DATA){return i==0;}
static BOOL fixtureInterfaceDetail(HDEVINFO,PSP_DEVICE_INTERFACE_DATA,PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,DWORD,PDWORD bytes,PSP_DEVINFO_DATA){if(bytes)*bytes=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)+128*sizeof(wchar_t);if(!detail)return FALSE;wcscpy_s(detail->DevicePath,128,L"fixture-hid");return TRUE;}
static BOOL fixtureDestroyList(HDEVINFO){return TRUE;}
static HANDLE fixtureCreateFile(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE){++fixtureIoOpenCalls;return fixtureIoOpenOk ? reinterpret_cast<HANDLE>(0x702) : INVALID_HANDLE_VALUE;}
static HANDLE fixtureCreateEvent(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR){++fixtureIoEventCreates;return fixtureIoEventOk ? reinterpret_cast<HANDLE>(0x703) : nullptr;}
static BOOLEAN fixtureHidAttributes(HANDLE,PHIDD_ATTRIBUTES attrs){attrs->VendorID=fixtureIoVendor;attrs->ProductID=fixtureIoProduct;return TRUE;}
static BOOLEAN fixtureHidPrep(HANDLE,PHIDP_PREPARSED_DATA* prep){*prep=reinterpret_cast<PHIDP_PREPARSED_DATA>(1);return TRUE;}
static NTSTATUS fixtureHidCaps(PHIDP_PREPARSED_DATA,PHIDP_CAPS caps){caps->UsagePage=fixtureIoUsagePage;caps->Usage=1;caps->InputReportByteLength=64;return HIDP_STATUS_SUCCESS;}
static BOOLEAN fixtureHidFree(PHIDP_PREPARSED_DATA){return TRUE;}
#define SetupDiGetClassDevsW fixtureClassDevs
#define SetupDiEnumDeviceInterfaces fixtureEnumInterfaces
#define SetupDiGetDeviceInterfaceDetailW fixtureInterfaceDetail
#define SetupDiDestroyDeviceInfoList fixtureDestroyList
#define CreateFileW fixtureCreateFile
#define CreateEventW fixtureCreateEvent
#define HidD_GetAttributes fixtureHidAttributes
#define HidD_GetPreparsedData fixtureHidPrep
#define HidP_GetCaps fixtureHidCaps
#define HidD_FreePreparsedData fixtureHidFree
static DWORD fixtureIoWait=WAIT_TIMEOUT, fixtureIoResultError=ERROR_IO_INCOMPLETE, fixtureIoReadError=ERROR_IO_PENDING;
static bool fixtureIoResultOk=false, fixtureIoReadOk=false;
static int fixtureIoResultCalls=0, fixtureIoReadCalls=0;
static std::vector<HANDLE> fixtureIoClosed;
static std::vector<DWORD> fixtureIoWaitBudgets;
static std::vector<BOOL> fixtureIoResultWaitFlags;
static DWORD fixtureIoBytes=64;
static BYTE fixtureIoData[64]{};
static DWORD fixtureBoundaryWait(HANDLE h,DWORD ms) {
 if(h==reinterpret_cast<HANDLE>(0x101)) return fixtureWmiWait(h,ms);
 fixtureIoWaitBudgets.push_back(ms);return fixtureIoWait;
}
static BOOL fixtureBoundaryClose(HANDLE h) {
 if(h==reinterpret_cast<HANDLE>(0x101)) return fixtureWmiClose(h);
 fixtureIoClosed.push_back(h);return TRUE;
}
static BOOL fixtureGetOverlappedResult(HANDLE,LPOVERLAPPED,DWORD* bytes,BOOL wait) {
 ++fixtureIoResultCalls;fixtureIoResultWaitFlags.push_back(wait);*bytes=fixtureIoBytes;
 SetLastError(fixtureIoResultError);return fixtureIoResultOk;
}
static BOOL fixtureReadFile(HANDLE,LPVOID buffer,DWORD len,LPDWORD got,LPOVERLAPPED ov) {
 ++fixtureIoReadCalls;std::copy(fixtureIoData,fixtureIoData+std::min<DWORD>(len,64),static_cast<BYTE*>(buffer));
 *got=fixtureIoReadOk ? fixtureIoBytes : 0;ov->Internal=fixtureIoReadOk ? 0 : fixtureIoReadError==ERROR_IO_PENDING ? 0x103 : 0;
 SetLastError(fixtureIoReadError);return fixtureIoReadOk;
}
static BOOL fixtureResetEvent(HANDLE) { return TRUE; }
#undef WaitForSingleObject
#undef CloseHandle
#define WaitForSingleObject fixtureBoundaryWait
#define CloseHandle fixtureBoundaryClose
#define GetOverlappedResult fixtureGetOverlappedResult
#define ReadFile fixtureReadFile
#define ResetEvent fixtureResetEvent
` : ""}
static int fixtureMsiAuthority = 0;
static int msiDinputBackAuthoritativeMask() { return fixtureMsiAuthority; }
static WORD g_curW = 0; static XINPUT_GAMEPAD g_curPad{};
static uint32_t g_oemKbDown = 0; static bool g_oemMouseLeft = false, g_oemMouseX2 = false;
static std::vector<std::pair<WPARAM, LPARAM>> fixturePhysicalReceipts;
static bool fixturePostOk = true;
static LONG fixtureMessageTime = 0;
static std::vector<LONG> fixtureReceiptTimes;
static LONG fixtureGetMessageTime() { return fixtureMessageTime; }
#define GetMessageTime fixtureGetMessageTime
static unsigned fixtureDesktopWake=0;
static BOOL fixturePostMessage(HWND, UINT message, WPARAM w, LPARAM l) {
    if(message==WM_DESKTOP_FUNCTION_EDGE){++fixtureDesktopWake;return fixturePostOk;}
#if defined(YMCC_OEM_CONSUME_SOURCE)
    if (message == WM_OEM_HOOK_RECONCILE) { ++fixtureReconcilePosts; return fixturePostOk; }
#endif
#if defined(YMCC_WMI_CLICK_SOURCE)
    if (message == WM_MSI_WMI_CLICK) { ++fixtureWmiWakePosts; return fixturePostOk; }
#if defined(YMCC_PRODUCER_EDGE_SOURCE)
    if (message == WM_OEM_PRODUCER_EDGE) { ++fixtureProducerPosts; return fixturePostOk; }
#endif
#endif
    if (!fixturePostOk) return FALSE;
    fixturePhysicalReceipts.emplace_back(w, l); fixtureReceiptTimes.push_back(static_cast<LONG>(GetTickCount64())); return TRUE;
}
static LRESULT fixtureCallNextHook(HHOOK, int, WPARAM, LPARAM) { return 0; }
#define PostMessageW fixturePostMessage
#define CallNextHookEx fixtureCallNextHook
// Windows API, current source identity and non-keyboard channels are fake;
// real profile/evaluator/recompute/hook/receipt/shortcut functions run below.
static std::atomic<uint32_t> g_oemKbDownRaw{0}, g_gpdChordSnapBaseKey{0}, g_gpdChordSnapPrefix{0}, g_gpdChordPubGen{0};
static std::atomic<int> g_oemBackStateKb{0}, g_gpdChordSnapPhysical{0};
${hasProducer ? "static uint64_t oemBackMergedKeyMask() { return fixtureOemMask | fixtureRealOemBackMergedKeyMask(); }" : hasWmi ? "static uint64_t oemBackMergedKeyMask() { return fixtureOemMask | (g_oemKeyMask.load() & ~static_cast<uint64_t>(fixtureMsiAuthority)) | g_oemKeyMaskMsiDinput.load(); }" : "static uint64_t nativeOemKeyMask() { return fixtureOemMask | g_oemKeyMask.load(); }"}
 enum class SerialJobKind { YmccOwn, ExternalInjection };
${serialRevisionPrelude}
static std::vector<std::pair<std::function<void()>, SerialJobKind>> fixtureJobs;
static bool gamepadSerialSubmit(std::function<void()> job, SerialJobKind kind = SerialJobKind::YmccOwn) { if (!fixtureJobAccept) return false; fixtureJobs.emplace_back(std::move(job), kind); return true; }
${/^static bool gamepadSerialSubmitCurrentPower\(/m.test(source) ? extract('gamepadSerialSubmitCurrentPower') : ''}
${keyboardWorkerCases ? `
#include <sstream>
#include <fstream>
struct FixtureKeyboardTrace {WORD vk;bool up;bool accepted;std::thread::id thread;};
static std::mutex fixtureKeyboardMx;static std::condition_variable fixtureKeyboardCv;
static std::vector<FixtureKeyboardTrace> fixtureKeyboardTrace;
static bool fixtureKeyboardPauseDown=false,fixtureKeyboardDownEntered=false,fixtureKeyboardResumeDown=false;
static std::atomic<bool> fixtureKeyboardRejectUps{false};
static UINT keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId,UINT count,INPUT* inputs) {
    std::unique_lock<std::mutex> lock(fixtureKeyboardMx);
    for(UINT n=0;n<count;++n){const bool up=(inputs[n].ki.dwFlags&KEYEVENTF_KEYUP)!=0;
        const bool accepted=!up||!fixtureKeyboardRejectUps.load();
        fixtureKeyboardTrace.push_back({inputs[n].ki.wVk,up,accepted,std::this_thread::get_id()});fixtureKeyboardCv.notify_all();
        if(!accepted)return n;
        if(!up&&fixtureKeyboardPauseDown){fixtureKeyboardPauseDown=false;fixtureKeyboardDownEntered=true;fixtureKeyboardCv.notify_all();
            if(!fixtureKeyboardCv.wait_for(lock,std::chrono::seconds(2),[]{return fixtureKeyboardResumeDown;}))throw std::runtime_error("Worker sender barrier timed out");}
    }return count;
}
` : `
static UINT keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId,UINT count,INPUT* inputs) {
    return fixtureSendInput(count,inputs,sizeof(INPUT));
}
`}
// Steam transport and identity are inert ports; the production shared sender,
// shortcut router, serial admission and receipt code are compiled unchanged.
static int fixtureSteamAttempts=0;
static bool fixtureSteamThrows=false;
static std::vector<json> fixtureSteamRequests;
static json fixtureSteamResult;
static std::function<void()> fixtureSteamBeforeSend;
static std::function<void()> fixtureSteamAfterSend;
namespace ymcc::steamsession {
struct Session {
    std::wstring root;
    DWORD pid=0,account=0;
    ULONGLONG created=0;
    bool operator==(const Session&) const = default;
};
}
static ymcc::steamsession::Session fixtureSteamSession;
static bool fixtureDeckyEnabled=true,fixtureDeckyFenced=false,fixtureDeckyModeDemand=true;
static unsigned fixtureDeckyRefreshes=0,fixtureDeckyChecks=0;
static json fixtureDeckyMenuDiagnostic;
static struct { bool isolated=false; } g_aiFanMockSession;
#include <source_location>
#include "steam_ui_mode_cache_miss.h"
static struct {
    bool requestConnectionRefresh(const ymcc::steamsession::Session&,std::source_location,ymcc::steamui::ConnectionRefreshQueue* out,
                                  bool,bool confirmedQamClose) {
        if(!fixtureDeckyModeDemand)return false;
        ++fixtureDeckyRefreshes;*out=confirmedQamClose?ymcc::steamui::ConnectionRefreshQueue{0,false,false,true}:
            ymcc::steamui::ConnectionRefreshQueue{600,false,false};return true;
    }
} g_steamModeObserver;
static struct {
    bool exitFenced() const { return fixtureDeckyFenced; }
    struct State { bool enabled; };
    State snapshot() const { return {fixtureDeckyEnabled}; }
    bool requestConnectionCheck() { ++fixtureDeckyChecks;return false; } // No attached plugin required.
} g_deckySidebarRuntime;
static void deckySidebarDiagnostic(const char*,const json& detail,bool) { fixtureDeckyMenuDiagnostic=detail; }
static bool sofRegDWORD(HKEY,const wchar_t*,const wchar_t*,DWORD* value) {*value=0;return false;}
struct FixtureSteamIdentity {
    ymcc::steamsession::Session session;
    bool currentRegistryAccountKnown() const { return false; }
};
static struct {
    std::shared_ptr<const FixtureSteamIdentity> identitySnapshot() const { return std::make_shared<FixtureSteamIdentity>(FixtureSteamIdentity{fixtureSteamSession}); }
    ymcc::steamsession::Session sessionSnapshot() const { return fixtureSteamSession; }
} g_steamSettingsObserver;
static json steamLiveRequest(DWORD,const json& request,const std::function<bool()>& allowed) {
    ++fixtureSteamAttempts;
    if(fixtureSteamBeforeSend)fixtureSteamBeforeSend();
    if(!allowed())return {{"ok",false},{"mutated",false},{"reason","request-cancelled"}};
    fixtureSteamRequests.push_back(request);
    if(fixtureSteamThrows)throw std::runtime_error("uncertain-transport");
    if(fixtureSteamAfterSend)fixtureSteamAfterSend();
    return fixtureSteamResult;
}
// Game/overlay identity is outside this shortcut consumer. Keep the shared
// sender's new routing dependency inert and forward its original admission
// callback through the same counted transport port.
#include "steam_qam_origin.h"
static NativeSteamQamOrigin nativeSteamQamOrigin() noexcept { return {}; } // No foreground access in this fixture.
static json nativeSteamMenuGameRequest(DWORD account,json request,const std::function<bool()>& allowed,ULONGLONG=0,unsigned long long=0,const NativeSteamQamOrigin* =nullptr) {
    return steamLiveRequest(account,request,allowed);
}
${(desktopCase||cases.startsWith('native-oem-')||cases==='onex-consumption'||wmiMaintenance)?'static ULONGLONG nativeSteamMenuColdDeadline(ULONGLONG){throw std::runtime_error("STEAM_ROUTE_NOT_SELECTED_BY_NATIVE_OEM_CLICK_TEST");}':'static ULONGLONG nativeSteamMenuColdDeadline(ULONGLONG started) noexcept {return started;}'}
// Cursor-policy wake is outside this input consumer's inert OS boundary.
static void steamControllerCursorSignal() noexcept {}
#include "steam_qam_followup.h"
static ymcc::ShortcutActionMailbox g_shortcutActionMailbox("memory-shortcut-session");
struct NativeKeyboardShortcutRule;
static bool nativeInputShortcutDesktopKey(const std::string&,WORD&);
static bool nativeInputShortcutDesktopKeys(const json&,std::vector<WORD>&);
static bool nativeKeyboardShortcutClickMatches(const NativeKeyboardShortcutRule&,uint64_t);
${keyboardWorkerCases ? `
static bool nativeKeyboardSequenceOwnsKey(WORD);
static bool nativeKeyboardSequenceDispatch(const NativeKeyboardShortcutRule&,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens);
` : `
static bool nativeKeyboardSequenceOwnsKey(WORD){return false;}
static bool nativeKeyboardSequenceDispatch(const NativeKeyboardShortcutRule&,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens){throw std::runtime_error("KEYBOARD_SEQUENCE_FORBIDDEN_IN_TOGGLE_FIXTURE");}
`}
static std::function<void()> nativeShortcutTdpJob(int);
`;
const hasDefaultOwner = source.includes('static int nativeShortcutLegacyDefaultSlot(');
const legacyStart = source.indexOf('    const bool shoulderDown = ', source.search(/^static void gamepadEval(?:Impl)?\([^;\r\n]*\)\s*\{/m));
let legacyEnd = source.indexOf('    gamepadProcessUiInput(w, now', legacyStart);
// The UI-input cost scope belongs to the following excluded consumer, not the
// retained shoulder shortcut. Do not leave its opening brace in this slice.
const legacyUiCostScope = source.slice(legacyStart,legacyEnd).match(/[ \t]*\{\r?\n\s*UiCostGamepadSpan uiInputCost\(kUiCostGamepadUiInput,uiStageCostOn\);\s*$/);
if(legacyUiCostScope)legacyEnd=legacyStart+legacyUiCostScope.index;
assert(legacyStart > 0 && legacyEnd > legacyStart);
const tdpMailboxRoute=cases==='native-oem-ui-mailbox';
const tdpWindowCase=tdpMailboxRoute?source.match(/^    case WM_GAMEPAD_TDP_DELTA: \{\r?\n[\s\S]*?^    \}/m)?.[0]:'';
assert(!tdpMailboxRoute||tdpWindowCase,'Missing actual TDP window route');
// Copy the complete production legacy dispatch region. Only boundary calls and
// the physical snapshot are mocked; none of its guards or branches are rewritten.
const legacyFixture = hasDefaultOwner ? `
#define YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE
static bool g_summonEnabled=true,g_bDoubleMinimize=true,g_tdpShortcut=true,g_fpsShortcut=true,g_killGame=true,g_openKeyboard=true,g_returnDesktop=true,g_mouseToggle=true;
static bool gpPrevB=false,gpKbArmed=true,gpKxArmed=true,gpKyArmed=true,g_bClosePending=false;
static ULONGLONG gpLastBPress=0,gpKbHoldStart=0,gpKxHoldStart=0,gpKyHoldStart=0,g_bClosePendingSince=0,g_bCloseReadyAt=0,s_dpHeldStart=0,s_dpLastEmit=0,s_brightnessHeldStart=0,s_brightnessLastEmit=0;
static ymcc::DefaultSummonHold g_defaultSummonHold;
static struct { ULONGLONG readContinuityGeneration=1; } g_inputCaptureGamepadSnapshot;
static bool nativeDefaultSummonTry(const FocusTargetSnapshot& target) { return nativeYmccShortcutSummon(target); }
static WORD g_prevW=0;

${tdpMailboxRoute?`#define YMCC_NATIVE_TDP_ROUTES
${extract('nativeAdjustTdp')}
static int fixtureWindowTdp(WPARAM w) {
  constexpr int WM_GAMEPAD_TDP_DELTA=1;
  switch(WM_GAMEPAD_TDP_DELTA) {
${tdpWindowCase}
  }
  return -1;
}
`:'static void nativeAdjustTdp(int delta) { fixtureOwnActions.push_back("legacy.tdp"); fixtureDeltas.push_back(delta); }'}
static void nativeAdjustBrightness(int delta) { fixtureOwnActions.push_back("legacy.brightness"); fixtureDeltas.push_back(delta); }
static void fixtureLegacyReset() {
 g_defaultSummonHold.release();gpKbArmed=gpKxArmed=gpKyArmed=true;gpPrevB=g_bClosePending=false;
 gpLastBPress=gpKbHoldStart=gpKxHoldStart=gpKyHoldStart=g_bClosePendingSince=g_bCloseReadyAt=s_dpHeldStart=s_dpLastEmit=s_brightnessHeldStart=s_brightnessLastEmit=0;g_prevW=0;
}
static void fixtureLegacyEval(WORD w, ULONGLONG now) {
 fixtureTick=now;
 const ULONGLONG HOLD_MS=500,B_DOUBLE_MS=500;
 const WORD shoulderBits=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;
 const bool both=(w&shoulderBits)==shoulderBits;
 const bool bPressed=(w&XINPUT_GAMEPAD_B)!=0,aPressed=(w&XINPUT_GAMEPAD_A)!=0,startHeld=(w&XINPUT_GAMEPAD_START)!=0,selectHeld=(w&XINPUT_GAMEPAD_BACK)!=0,xPressed=(w&XINPUT_GAMEPAD_X)!=0,yPressed=(w&XINPUT_GAMEPAD_Y)!=0;
 const bool dpUp=(w&XINPUT_GAMEPAD_DPAD_UP)!=0,dpDown=(w&XINPUT_GAMEPAD_DPAD_DOWN)!=0,dpLeft=(w&XINPUT_GAMEPAD_DPAD_LEFT)!=0,dpRight=(w&XINPUT_GAMEPAD_DPAD_RIGHT)!=0;
 ${source.slice(legacyStart,legacyEnd)}
 g_prevW=w;
}
` : '';
// Memory-only support for the real MSI keyboard policy introduced in main.
// Native firmware and observer runtimes have separate all-fake tests; they are
// not executed by the shortcut evaluator fixture.
const msiExtraPrelude=source.includes('static std::atomic<bool> g_msiNativeRearDemand') ? `
#define YMCC_MSI_NATIVE_ONLY_REAR_SOURCE
#include "msi_rear_protocol.h"
static std::atomic<bool> g_msiNativeRearDemand{false};
static bool fixtureMsiKnownBoard=true;
static bool msiClawKnownBoardAuthority(){return fixtureMsiKnownBoard&&g_oemBackProfileId.load()==static_cast<int>(OemChordProfileId::Msi);}
static bool msiTargetIntentEvidenceEnabled() noexcept{return false;}
static void msiTargetIntentObserveInput(const json&,const json&,bool,bool) noexcept{}
static void msiNativeRearKeyboardEdgeNote(uint64_t){}
static void msiOemSuppressionObservationPump(){}
static void msiOemSuppressionObserveClick(const MsiWmiClickReceipt&,bool,bool){}
` : '';
// The shared historical suite still lists two personas rejected by the current
// production contract. Migrate its fixture expectations without changing product headers.
let summonCases = fs.readFileSync(path.join(root, 'tools/shortcut_summon_frontend_cases.h'), 'utf8');
const oldPersonas = '{{"steam","steamdeck"},{"ps","dualsense-edge"},{"ps","dualsense"},{"xbox","elite"},{"xbox","xbox360"}}';
if (summonCases.includes(oldPersonas)) {
  summonCases = summonCases.replace(oldPersonas, '{{"steam","steamdeck"},{"ps","dualsense-edge"},{"xbox","elite"}}');
  const unsupportedCases = `    for (const auto& entry : std::vector<std::pair<std::string,std::string>>{{"ps","dualsense"},{"xbox","xbox360"}}) {
        resetRoute(json::array()); fixtureFrontendPersona=g_inputHostBoundPersona=entry.second;
        check(!nativeYmccShortcutEmit("input.frontendButton",static_cast<int>(ymcc::frontendButton(entry.first))) &&
              !g_frontendButtonPulse.pending(), "historical unsupported frontend persona is explicitly rejected: "+entry.second);
    }
`;
  summonCases = summonCases.replace('    resetRoute(json::array());fixtureFrontendPersona="steamdeck";g_inputHostBoundPersona="elite";', unsupportedCases + '    resetRoute(json::array());fixtureFrontendPersona="steamdeck";g_inputHostBoundPersona="elite";');
}
let regressionSource = fs.readFileSync(path.join(root, 'tools/input_shortcut_toggle_selftest.cpp'), 'utf8')
  .replace('#include "shortcut_summon_frontend_cases.h"', summonCases);

const retirementCall = '        startF7RetirementCases();';
assert(regressionSource.includes(retirementCall), 'Missing real-consumer retirement registration');
if (cases === 'retired-start-f7') {
  regressionSource = regressionSource.replace(retirementCall, retirementCall + '\n        std::cout << "retired Start F7 real consumer: " << passed << " cases passed\\n"; return 0;');
} else if (cases === 'baseline-without-retirement') {
  // Original product source is expected to fail the new case; run the preserved
  // old suite separately to identify unrelated baseline failures.
  regressionSource = regressionSource.replace(retirementCall, '        // New retirement cases excluded only for explicit old-suite baseline replay.');
} else if (cases === 'native-oem-keyboard-suppression') {
  assert(hasWmi && hasRogClick, 'Native keyboard selection requires current MSI source guards');
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  const keyEvent=regressionSource.slice(mainStart).match(/        const auto keyEvent = \[\][\s\S]*?\n        \};/);
  assert(keyEvent, 'Missing original keyboard event port');
  const exactLoop=marker=>{
    const begin=regressionSource.indexOf(marker,mainStart);assert(begin>=mainStart,'Missing original native keyboard loop '+marker);
    const end=regressionSource.indexOf('\n        }',begin);assert(end>begin,'Missing original native keyboard loop end');
    return regressionSource.slice(begin,end+'\n        }'.length);
  };
  const selected=[exactLoop("        for(const DWORD alias:{DWORD('G'),DWORD(VK_TAB)})for(bool wmiFirst:{false,true}) {"),exactLoop('        for(bool m1:{false,true}){')];
  regressionSource=regressionSource.slice(0,mainStart)+'int main() { try {\n'+keyEvent[0]+'\n'+selected.join('\n')+
    '\nstd::cout << "input shortcut native model/real route: " << passed << " cases passed\\n"; return 0;\n'+
    '} catch(const std::exception& error) { std::cerr << error.what(); return 1; } }\n';
 } else if (desktopCase) {
  if(cases==='native-recorded-mouse-oem-compat'){
    const begin=regressionSource.indexOf('        int mouseRows=0;');
    const final='        std::cout<<"R15 mouse-workload rows="<<mouseRows<<"\\n";';
    const end=regressionSource.indexOf(final,begin);
    assert(begin>=0&&end>begin,'Missing original R15 exact mouse-consumption loop');
    const main=regressionSource.indexOf('int main() {');assert(main>=0);
    regressionSource=regressionSource.slice(0,main)+'static void nativeRecordedMouseOemCompatibilityCases(){\n'+
      regressionSource.slice(begin,end+final.length)+'\n}\n'+regressionSource.slice(main);
  }
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  regressionSource=regressionSource.slice(0,mainStart)+`int main() { try {
    ${cases==='native-recorded-mouse-only-hooks'?'nativeRecordedMouseOnlyHookCases();':cases==='native-recorded-mouse-only-retry'?'nativeRecordedMouseOnlyRetryCases();':cases==='native-recorded-mouse-only-cold'?'nativeRecordedMouseOnlyColdCases();':cases==='native-recorded-mouse-independent-owner'?'nativeRecordedMouseIndependentOwnerCases();':cases==='native-recorded-mouse-publication'?'nativeRecordedMousePublicationCases();':cases==='native-recorded-mouse-prior-owner'?'nativeRecordedMousePriorOwnerCases();':cases==='native-recorded-mouse-partial-hook'?'nativeRecordedMousePartialHookCases();':cases==='native-recorded-mouse-pulses'?'nativeRecordedMousePulseCases();':cases==='native-recorded-mouse-guards'?'nativeRecordedMouseGuardCases();':cases==='native-recorded-mouse-replay'?'nativeRecordedMouseReplayCases();':cases==='native-recorded-mouse-oem-compat'?'nativeRecordedMouseOemCompatibilityCases();':cases==='native-recorded-keyboard-diagnostics'?'nativeRecordedKeyboardDiagnosticCases();':cases==='native-recorded-keyboard-definitions'?'nativeRecordedKeyboardDefinitionCases();':cases==='native-recorded-keyboard-preheld'?'nativeRecordedKeyboardGuardCases(true,false,true);':cases==='native-recorded-keyboard-fallback-control'?'nativeRecordedKeyboardGuardCases(false,true);':cases==='native-recorded-keyboard-guards-tail'?'nativeRecordedKeyboardGuardCases(true);':cases==='native-recorded-keyboard-guards'?'nativeRecordedKeyboardGuardCases();':cases==='native-recorded-keyboard-consume'?'nativeRecordedKeyboardConsumeCases();':cases==='native-recorded-keyboard-chord'?'nativeRecordedKeyboardChordCases();':cases==='native-rog-held-return'?'nativeRogHeldReturnCases();':cases==='native-rog-source-owner'?'nativeRogSourceOwnerCases();':cases==='native-rog-held-boundaries'?'nativeRogHeldSourceBoundaryCases();':cases==='native-rog-held-source'?'nativeRogHeldSourceCases();':cases==='native-rog-ll-fallback-transition'?'nativeRogLlSourceTransitionCases();':cases==='native-rog-ll-fallback'?'nativeRogLlFallbackCases();':cases==='native-desktop-chord-shape'?'nativeDesktopWitnessShapeCases();':cases==='native-desktop-chord-admission'?'nativeDesktopChordAdmissionCases();':cases==='native-desktop-chord-guards'?'nativeDesktopChordGuardCases();':cases==='native-desktop-function-teardown-gate'?'nativeDesktopFunctionTeardownGateCases();':cases==='native-desktop-function-teardown'?'nativeDesktopFunctionTeardownCases();':cases==='native-desktop-function-proof-final'?'nativeDesktopFunctionProofCases(true,true);':cases==='native-desktop-function-proof-tail'?'nativeDesktopFunctionProofCases(true);':cases==='native-desktop-function-proof'?'nativeDesktopFunctionProofCases();':cases==='native-desktop-function-final-tail'?'nativeDesktopFunctionFinalCases(true);':cases==='native-desktop-function-final'?'nativeDesktopFunctionFinalCases();':cases==='native-desktop-function-boundaries'?'nativeDesktopFunctionBoundaryCases();':'nativeDesktopFunctionCases();'}
    std::cout << "desktop explicit key route: " << passed << " cases passed\\n"; return 0;
  } catch(const std::exception& error) { std::cerr << error.what(); return 1; } }`;
} else if (gateCases) {
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  regressionSource=regressionSource.slice(0,mainStart)+`int main() { try {
    nativeOemGateTransitionCases(${cases.endsWith('-tail')?3:0});
    std::cout << "input shortcut native model/real route: " << passed << " cases passed\\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what(); return 1; } }
`;
} else if (cases === 'native-oem-aya-consumption' || cases === 'native-oem-aya-extensions') {
  assert(hasOneXBindingConsumer,'AYA selection requires the actual unified model binding consumer');
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  regressionSource=regressionSource.slice(0,mainStart)+`int main() { try {
    ayaNativeConsumptionCases(${cases==='native-oem-aya-extensions'});
    std::cout << "input shortcut native model/real route: " << passed << " cases passed\\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what(); return 1; } }
`;
 } else if (keyboardWorkerCases) {
  assert(hasProducer && hasSerialRevisionFence, 'Keyboard worker requires actual producer tokens and configuration fence');
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  regressionSource=regressionSource.slice(0,mainStart)+`int main() { try {
    nativeOemKeyboardWorkerCases(${cases==='native-oem-keyboard-worker-owner'?1:cases==='native-oem-keyboard-worker-revision'?2:0});
    std::cout << "input shortcut native model/real route: " << passed << " cases passed\\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what(); return 1; } }
`;
} else if (cases === 'native-oem-ui-mailbox') {
  assert(hasProducer && hasWmi, 'UI mailbox selection requires actual source/dispatch/claim consumers');
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  regressionSource=regressionSource.slice(0,mainStart)+`int main() { try {
    nativeOemUiMailboxCases();
    std::cout << "input shortcut native model/real route: " << passed << " cases passed\\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what(); return 1; } }
`;
} else if (cases === 'native-oem-queued-owner') {
  assert(hasProducer && hasWmi && source.includes('static void nativeKeyboardShortcutHandleOemEdge('), 'Queue owner selection requires actual source and serial consumers');
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  regressionSource=regressionSource.slice(0,mainStart)+`int main() { try {
    nativeOemQueuedOwnerCases();
    sampledTokenWindowCases();
    freshRetainedAfterRejectedSample();
    sampledTokenIndependentFenceCases();
    shortcutOemDemandCases();
    std::cout << "input shortcut native model/real route: " << passed << " cases passed\\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what(); return 1; } }
`;
} else if (cases === 'native-oem-owner-boundary') {
  assert(hasProducer && source.includes('static void nativeKeyboardShortcutHandleOemEdge('), 'Owner boundary selection requires both actual retained entries');
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  regressionSource=regressionSource.slice(0,mainStart)+`int main() { try {
    nativeOemOwnerBoundaryCases();
    std::cout << "input shortcut native model/real route: " << passed << " cases passed\\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what(); return 1; } }
`;
} else if (cases === 'native-oem-click-actions') {
  assert(hasWmi && hasRogClick, 'Native click selection requires both real producers');
  const mainStart=regressionSource.indexOf('int main() {');assert(mainStart>=0);
  regressionSource=regressionSource.slice(0,mainStart)+`int main() { try {
    ${virtualRearUiFirstMixed ? `nativeOemVirtualUiFirstMixedCases(${virtualRearUiFirstMixedKind})` : virtualRearUiFirstClick ? `nativeOemVirtualUiFirstClickCases(${virtualRearUiFirstClickExtra})` : virtualRearUiFirstCross ? 'nativeOemVirtualUiFirstCrossCases()' : virtualRearUiFirstTail ? `nativeOemVirtualUiFirstTailCases(${virtualRearUiFirstTailIo?11:virtualRearUiFirstTailMsi?9:0})` : virtualRearUiFirst ? 'nativeOemVirtualUiFirstCases()' : virtualRearFirstFrame ? 'nativeOemVirtualFirstFrameCases()' : virtualRearKeyboardMsi ? `nativeOemVirtualKeyboardMsiCases(${virtualRearKeyboardMsiTail})` : virtualRearSourceLife ? 'nativeOemVirtualSourceLifeCases()' : virtualRearGestures ? 'nativeOemVirtualGestureCases()' : virtualRearUnknown ? 'nativeOemVirtualUnknownCases()' : virtualRearOnly ? 'nativeOemVirtualRearConsumptionCases()' : 'nativeOemClickActionsCases()'};
    std::cout << "input shortcut native model/real route: " << passed << " cases passed\\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what(); return 1; } }
`;
} else if (wmiMaintenance) {
  assert(hasWmi, 'WMI maintenance requires the real WMI consumer');
  const mainStart = regressionSource.indexOf('int main() {');
  assert(mainStart >= 0, 'Missing original shortcut main');
  const selected = [];
  let cursor = mainStart;
  while ((cursor = regressionSource.indexOf('#if defined(YMCC_WMI_CLICK_SOURCE)', cursor)) >= 0) {
    let depth = 0, end = -1;
    for (const directive of regressionSource.slice(cursor).matchAll(/^#(?:if|ifdef|ifndef|endif)\b[^\r\n]*/gm)) {
      depth += directive[0].startsWith('#endif') ? -1 : 1;
      if (depth === 0) { end = cursor + directive.index + directive[0].length; break; }
    }
    assert(end > cursor, 'Missing original WMI group end');
    selected.push(regressionSource.slice(cursor, end)); cursor = end;
  }
  assert.equal(selected.length, 2, 'Original WMI and QS mirror groups must both be retained');
  const keyEvent = regressionSource.slice(mainStart).match(/        const auto keyEvent = \[\][\s\S]*?\n        \};/);
  assert(keyEvent, 'Missing original keyboard boundary used by the QS mirror group');
  regressionSource = regressionSource.slice(0, mainStart) + 'int main() { try {\n' + keyEvent[0] + '\n' + selected.join('\n') +
    '\nstd::cout << "WMI maintenance actual route: " << passed << " cases passed\\n"; return 0;\n' +
    '} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << "\\n"; return 1; } }\n';
}
const gatePrelude=nativeGateDependencies?`
static bool g_oemGateTargetEnabled=false,g_oemGateBackMapping=false;
static std::string g_oemGatePersona="disabled";
static std::atomic<bool> g_oemBackMappingEnabled{false};
static std::atomic<uint64_t> g_ayaOemKeyboardEvidenceBinding{0};
static bool g_oemRawVkAllowed[256]{};
static HANDLE g_oemRawAdmitDev=nullptr;
static bool fixtureRogReferenceReady=false;
static void ymccResolveMachineIdentityOnce(){} // resolved identity is supplied, never queried here
static bool deviceInterfaceContainerId(const wchar_t*,GUID*){return fixtureRogReferenceReady;}
static bool deviceInterfaceUsbInstance(const wchar_t*,std::wstring&){return false;}
static int oemBoundChordProfileIdFor(YmccFamily,const std::string&);
static uint64_t rogOemKeyboardFallbackMask();
`:`
// The shortcut action fixture does not start or refresh physical raw capture.
static bool g_oemGateTargetEnabled=false,g_oemGateBackMapping=false;
static std::string g_oemGatePersona="disabled";
static bool g_oemRawVkAllowed[256]{};
static HANDLE g_oemRawAdmitDev=nullptr;
static uint64_t rogOemKeyboardFallbackMask(){return 0;}
static void oemBackMappingRefreshGate(bool,const std::string&,bool){}
`;
const gateMutation=!desktopCase&&gateCases&&process.env.YMCC_OEM_NATIVE_BEFORE_CONTROL==='1';
const generatedFunctions=functions.map(extract).sort((a,b)=>source.indexOf(a)-source.indexOf(b)).map(text=>{
  if(virtualRearUiFirstMixedKind===5&&text.startsWith('static void msiWmiHandleEvent(')){
    const marker=/\r?\n    \{\r?\n        std::lock_guard<std::mutex> lock\(g_oemProducerMx\);/g;
    assert.equal([...text.matchAll(marker)].length,1,'WMI original admission interleave boundary');
    text=text.replace(marker,'\n    if(fixtureVirtualCaptureInterleave){auto callback=std::move(fixtureVirtualCaptureInterleave);fixtureVirtualCaptureInterleave={};callback();}\n    {\n        std::lock_guard<std::mutex> lock(g_oemProducerMx);');
  }
  if(virtualRearUiFirstMixedKind===6&&text.startsWith('static void nativeOemVirtualConsumeMatched(')){
    const marker=/bool active=false;\r?\n    std::lock_guard<std::mutex> lock\(g_oemProducerMx\);/g;
    assert.equal([...text.matchAll(marker)].length,1,'OEM original commit interleave boundary');
    text=text.replace(marker,'bool active=false;\n    if(fixtureVirtualCommitInterleave){auto callback=std::move(fixtureVirtualCommitInterleave);fixtureVirtualCommitInterleave={};callback();}\n    std::lock_guard<std::mutex> lock(g_oemProducerMx);');
  }
  if(gateMutation&&text.startsWith('static void oemBackMappingRefreshGate(')){
    const before='const bool wantHooks = (chordCapable && !rawExclusive) || g_desktopFunctionCaptureMask.load(std::memory_order_acquire)!=0;';assert.equal(text.split(before).length,2);
    text=text.replace(before,'const bool wantHooks = (backMappingGate && chordCapable && !rawExclusive) || g_desktopFunctionCaptureMask.load(std::memory_order_acquire)!=0;');
  }
  if(cases==='native-oem-queued-owner'&&text.startsWith('static OemProducerTokens oemProducerTokensFor(')) {
    const opening='static OemProducerTokens oemProducerTokensFor(uint64_t requiredMask) {';
    assert(text.startsWith(opening),'Token interleave boundary changed');
    text='#define YMCC_SHORTCUT_TOKEN_INTERLEAVE 1\nstatic std::function<void()> fixtureTokenCaptureInterleave;\n'+text.replace(opening,
      opening+'\n    if(fixtureTokenCaptureInterleave){auto action=std::move(fixtureTokenCaptureInterleave);fixtureTokenCaptureInterleave={};action();}');
  }
  if(cases==='native-oem-queued-owner'&&text.startsWith('static uint64_t nativeOemVirtualActionObservation(')) {
    const opening='static uint64_t nativeOemVirtualActionObservation(OemProducerTokens& tokens){';
    assert(text.startsWith(opening),'OEM demand read boundary changed');
    text='#define YMCC_SHORTCUT_OEM_DEMAND 1\nstatic unsigned fixtureDemandOemReads=0;\n'+text.replace(opening,opening+'\n    ++fixtureDemandOemReads;');
  }
  if(cases==='native-recorded-keyboard-chord'&&source.includes('static void nativeDesktopChordMaintain() {')&&extract('nativeDesktopChordMaintain').includes('const bool cancel=')&&text.startsWith('static ymcc::input::desktopFunction::KeyboardKeys nativeDesktopChordPhysicalKeys() {')) {
    const opening='static ymcc::input::desktopFunction::KeyboardKeys nativeDesktopChordPhysicalKeys() {';
    text='static unsigned fixtureMaintainSnapshotReads=0;\nstatic std::function<void()> fixtureSnapshotInterleave;\n'+text.replace(opening,opening+'\n    ++fixtureMaintainSnapshotReads;\n    if(fixtureSnapshotInterleave){auto callback=std::move(fixtureSnapshotInterleave);fixtureSnapshotInterleave={};callback();}');
  }
  return hasProducer?text.replace(/^static uint64_t oemBackMergedKeyMask\(/,'static uint64_t fixtureRealOemBackMergedKeyMask('):text;
}).join('\n');
const keyboardRuntime = fs.readFileSync(path.join(root,'native/keyboard_shortcut_sequence_runtime.h'),'utf8');
const hasMixedWitness = source.includes('struct OemVirtualControllerSample {');
const sequenceWitness = hasMixedWitness && !keyboardWorkerCases ? (()=>{
  const state=keyboardRuntime.match(/^static std::array<std::atomic<unsigned char>,256> g_keyboardSequenceInputFence[^\r\n]*/m)?.[0];
  const read=keyboardRuntime.match(/^static unsigned char nativeKeyboardSequenceInputFenceValue\(WORD key\) noexcept \{[\s\S]*?^\}/m)?.[0];
  assert(state&&read,'Missing actual nonmutating sequence ownership publication read');return state+'\n'+read;
})() : '';
const mixedContext = hasMixedWitness ? 'static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1};\nstatic std::atomic<uint64_t> g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};\n' : '';
const msiMixedContext=hasMixedWitness?shieldStruct('MsiClawCapabilityDescriptor')+'\n'+source.match(/^static constexpr MsiClawCapabilityDescriptor kMsiClawCapability\{[\s\S]*?^\};/m)[0]+'\nstatic std::atomic<bool>g_msiDinputSourceValid{true},g_msiDinputRouteConfirmed{true},g_msiDinputRouteDinputArmed{true};\nstatic std::function<void()>fixtureVirtualCaptureInterleave,fixtureVirtualCommitInterleave;\n':'';
const generated = path.join(out, 'input-shortcut-real-route.fixture.cpp');
const rearExpression = virtualRearOnly ? extract('inputHostSubmitPad').match(/frameContent\.backButtons\s*=[\s\S]*?;/)?.[0] : '';
assert(!virtualRearOnly || rearExpression, 'Missing actual final virtual rear expression');
const rearConsumer = virtualRearOnly ? `
#define YMCC_SHORTCUT_VIRTUAL_REAR_CASE 1
${virtualRearUnknown?'#define YMCC_SHORTCUT_VIRTUAL_UNKNOWN_CASE 1':''}
${virtualRearGestures?'#define YMCC_SHORTCUT_VIRTUAL_GESTURE_CASE 1':''}
${source.includes('struct OemVirtualBindingPolicy {')?'#define YMCC_VIRTUAL_POLICY_SOURCE 1':''}
${source.includes('static OemVirtualKeyboardSample g_oemVirtualKeyboardWriter;')?'#define YMCC_VIRTUAL_KEYBOARD_WRITER_SOURCE 1':''}
${source.includes('static void nativeOemVirtualConsumeMatched(')?'#define YMCC_VIRTUAL_UI_PRESS_SOURCE 1':''}
${hasMixedWitness?'#define YMCC_VIRTUAL_MIXED_SOURCE 1':''}
${source.includes('static std::atomic<uint64_t> g_oemVirtualMsiRevocation')?'#define YMCC_VIRTUAL_MSI_REVOCATION_SOURCE 1':''}
static int fixtureVirtualRear(const std::string& persona="steamdeck", bool semanticNeutral=false,
        unsigned long long barrierMask=0, bool backEnabled=true, const XINPUT_GAMEPAD& rawPad=XINPUT_GAMEPAD{}) {
    FocusFrameContent frameContent{};
    const std::atomic<bool> g_oemBackMappingEnabled{backEnabled};
    const auto previousPersona=fixtureFrontendPersona;
    fixtureFrontendPersona=persona;
    ${rearExpression}
    fixtureFrontendPersona=previousPersona;
    return frameContent.backButtons;
}
` : '';
fs.writeFileSync(generated, prelude + msiExtraPrelude + shieldPrelude + gatePrelude + mixedContext + msiMixedContext + sequenceWitness + '\n' + source.slice(structStart, structEnd) + '\n' + generatedFunctions + rearConsumer + (keyboardWorkerCases ? '\n#define YMCC_KEYBOARD_WORKER_SOURCE 1\n#include \"keyboard_shortcut_sequence_runtime.h\"\n' : '') + (hasWmi ? '\n' + source.slice(source.indexOf('class MsiWmiSink final'), source.indexOf('static DWORD WINAPI msiWmiThreadProc(')) : '') + legacyFixture + '\n' + regressionSource + '\n');
const exe = path.join(out, 'input-shortcut-real-route.fixture.exe');
const batch = path.join(out, 'compile-native-fixture.cmd');
fs.writeFileSync(batch, calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat') + 'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /Bv /sourceDependencies "' + path.join(out,'source-dependencies.json') + '" /I"' + path.join(root, 'native') + '" /I"' + path.join(root, 'deps/json') + '" "' + generated + '" /Fo"' + path.join(out, 'input-shortcut-real-route.fixture.obj') + '" /Fe"' + exe + '"' + (hasWmi ? ' /link oleaut32.lib wbemuuid.lib' : '') + '\r\nexit /b %errorlevel%\r\n');
if (process.env.YMCC_SHORTCUT_PREPARE_ONLY === '1') {
  assert(sourceBinding.every(row=>createHash('sha256').update(fs.readFileSync(row.path)).digest('hex')===row.sha256),
    'Source changed during shortcut preparation');
  fs.writeFileSync(path.join(out,'PREPARED.json'),JSON.stringify({status:'PREPARED_ONLY_NOT_EXECUTED',selectedCases:cases,
    generated,batch,sourceBinding,compiled:false,executed:false,actualPassedThisRun:false,normalProductStarted:false},null,2)+'\n');
  process.exit(0);
}
const compile = spawnSync('cmd.exe', ['/d', '/c', batch], { encoding: 'utf8', cwd: out,timeout:60000 });
fs.writeFileSync(path.join(out, 'native-fixture-build.log'), (compile.stdout || '') + (compile.stderr || ''));
process.stdout.write(compile.stdout || ''); process.stderr.write(compile.stderr || '');
assert.equal(compile.status, 0, 'Native fixture compilation failed');
const run = spawnSync(exe, [], { encoding: 'utf8', cwd: out,timeout:60000 });
fs.writeFileSync(path.join(out, 'native-fixture-results.log'), (run.stdout || '') + (run.stderr || ''));
process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
assert.equal(run.status, 0, 'Native route/model regression failed');
assert.equal((source.match(/ymcc\.effectiveGameInput\(ymSettingsSection/g) || []).length, 0, 'An effective reader bypasses the runtime switch');
assert(source.includes('"input.shortcutRuntime.clear"'));
const action = extract('nativeInputShortcutToggle');
assert(!/ymSettingsWrite|SettingsFileGuard|input\["revision"\]/.test(action), 'Toggle must never write settings or remembered state');
console.log('PASS all effective consumers use the runtime overlay; toggle action has no durable-write boundary');

const frontendRoute = extract('nativeYmccShortcutEmit');
assert(!/ymSettingsWrite|SettingsFileGuard|input\["revision"\]/.test(frontendRoute), 'Frontend buttons must not persist intent');
const frame = extract('inputHostSubmitPad');
const hostLock = frame.search(/std::lock_guard<(?:std::mutex|InputHostMutex)> lock\(g_inputHostMx\)/);
const pulseSample = frame.indexOf('g_frontendButtonPulse.sample');
assert(hostLock >= 0 && pulseSample > hostLock, 'Guide sample must be generation-bound under live target lock');
assert(frame.includes('hostFrameReady && !semanticNeutral && physicalSourceAdmitted'), 'Guide must respect frame/source admission');
assert(frame.includes('g_frontendButtonPulse.sample(GetTickCount64(), g_inputHostEpoch,'), 'Guide must use locked epoch, not a pre-lock mirror');
assert(!/SetTimer|std::thread|sleep_for/.test(fs.readFileSync(path.join(root,'native/shortcut_frontend_button.h'),'utf8')), 'Frontend pulse cannot add a timer or polling thread');
console.log('PASS Guide integration shares existing frame/target lock, source/neutral gates and no new timer or durable writes');

assert(sourceBinding.every(row=>createHash('sha256').update(fs.readFileSync(row.path)).digest('hex')===row.sha256),
  'Source or declared dependency changed during shortcut consumer verification');
const passCount=Number((run.stdout||'').match(/(?:frontend buttons|summon action shield|input shortcut native model\/real route|retired Start F7 real consumer|onex consumption|WMI maintenance actual route|desktop explicit key route): (\d+) cases passed/)?.[1]||0);
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify({status:'PASS',selectedCases:cases,selectedVariant:virtualRearUiFirstMixed?'virtual-rear-ui-first-mixed-'+virtualRearUiFirstMixedNames[virtualRearUiFirstMixedKind]:virtualRearUiFirstClickExtra?'virtual-rear-ui-first-click-extra':virtualRearUiFirstClick?'virtual-rear-ui-first-click':virtualRearUiFirstCross?'virtual-rear-ui-first-cross':virtualRearUiFirstTailIo?'virtual-rear-ui-first-tail-io':virtualRearUiFirstTailMsi?'virtual-rear-ui-first-tail-msi':virtualRearUiFirstTail?'virtual-rear-ui-first-tail':virtualRearUiFirst?'virtual-rear-ui-first':virtualRearFirstFrame?'virtual-rear-first-frame':virtualRearKeyboardMsiTail?'virtual-rear-keyboard-msi-tail':virtualRearKeyboardMsi?'virtual-rear-keyboard-msi':virtualRearSourceLife?'virtual-rear-source-life':virtualRearGestures?'virtual-rear-gestures':virtualRearUnknown?'virtual-rear-unknown':virtualRearOnly?'virtual-rear-consumption':null,checks:passCount,
  elapsedMs:Date.now()-invocationStarted,sourceStable:true,sourceBinding,actualProducerFunctions:functions,normalProductStarted:false,deviceIo:false,
  scope:cases==='wmi-maintenance'?'Both original WMI sink/click/pulse/lifecycle and QS mirror groups; real consumers with memory WMI/OS boundaries; other shortcut groups not executed':cases==='onex-consumption'?'Selected OneX real binding/profile/hook/HID/evaluator/dispatch/serial-action consumers; OS/Host, recording, model audit and motion boundaries are memory-only; common full suite not accepted':'Real shortcut parser/evaluator/dispatch/summon/UI ownership and final-frame consumers with declared memory OS/Host ports'},null,2)+'\n');

}
