import {
  CONTROLLER_SHORTCUT_MAX_INPUTS,
  CONTROLLER_SHORTCUT_MIN_INPUTS,
  CONTROLLER_SHORTCUT_ACTIONS,
  NATIVE_DEFAULT_SHORTCUTS,
  actionById,
  isControllerShortcutActionVisible,
  defaultParameters,
  formatControllerShortcutInputs,
  highlightedGamepadButtons,
  nativeDefaultRule,
  newControllerShortcutRule,
  normalizeOemKeysSnapshot,
  readControllerShortcutRules,
  mergeControllerShortcutDraft,
  reconcileControllerShortcutRules,
  shortcutRecordMode,
  validateControllerShortcutRules,
} from '../src/bridge/controllerShortcutRules';

import { assertKeyboardActionContract } from './ymcc_sim/global/input-signals/keyboard-contract';

// Check every admitted code, default, invalid code, validator and reload identity.
const keyboardContract = assertKeyboardActionContract();
console.log('keyboard identity contract:', keyboardContract.validMappings, 'mappings PASS');

const baseline = newControllerShortcutRule('baseline');
if (validateControllerShortcutRules([baseline]).length !== 0) throw new Error('baseline rule rejected');
const oneButton = { ...baseline, id: 'one-button', inputs: [baseline.inputs[0]] };
// 单手柄键的长按、按下、松开和双击均由正式规则与原生消费者支持。
if (validateControllerShortcutRules([oneButton]).length !== 0) throw new Error('single-button hold was rejected');
const tooShort = { ...baseline, id: 'too-short', inputs: [] };
if (validateControllerShortcutRules([tooShort]).length === 0) throw new Error('empty shortcut was accepted');
const fourInputs = { ...baseline, id: 'four-inputs', inputs: [
  { source: 'controller' as const, code: 'lb' },
  { source: 'controller' as const, code: 'rb' },
  { source: 'controller' as const, code: 'a' },
  { source: 'controller' as const, code: 'b' },
] };
if (validateControllerShortcutRules([fourInputs]).length !== 0 || fourInputs.inputs.length !== CONTROLLER_SHORTCUT_MAX_INPUTS) throw new Error('four-button shortcut rejected');
const fiveInputs = { ...fourInputs, id: 'five-inputs', inputs: [...fourInputs.inputs, { source: 'controller' as const, code: 'x' }] };
if (validateControllerShortcutRules([fiveInputs]).length === 0) throw new Error('five-button shortcut was accepted');
const loaded = readControllerShortcutRules({ shortcutRules: [{ ...baseline, inputs: ['lb', 'rb', 'lb', 'invalid'] }] });
if (loaded.length !== 1 || formatControllerShortcutInputs(loaded[0].inputs) !== 'LB + RB') throw new Error('legacy controller normalization failed');
if (highlightedGamepadButtons(loaded[0]).join(',') !== '4,5') throw new Error('gamepad highlight mapping failed');
// 2026-09-28 用户裁决（可以改一切，不再做限制）：同组合/同动作允许重复绑定。
if (validateControllerShortcutRules([baseline, { ...baseline, id: 'duplicate' }]).length !== 0) throw new Error('duplicate chord must now be allowed');
const sameActionDifferentInput = { ...baseline, id: 'same-action-different-input', inputs: [baseline.inputs[0], { source: 'controller' as const, code: 'a' }] };
// 裁决 B：不同输入可绑定同一动作（键组不同 → 接受；不再有"同一功能只能一个快捷"）
if (validateControllerShortcutRules([baseline, sameActionDifferentInput]).length !== 0) throw new Error('same action with different input was rejected');
const desktop = readControllerShortcutRules({ shortcutRules: [{ ...baseline, id: 'desktop', inputs: [{ source: 'keyboard', code: 'ControlLeft' }, { source: 'keyboard', code: 'KeyK' }, { source: 'keyboard', code: 'CapsLock' }, { source: 'mouse', code: 'MouseRight' }] }] });
if (desktop.length !== 1 || shortcutRecordMode(desktop[0].inputs) !== 'keyboard-mouse') throw new Error('desktop recording normalization failed');
if (formatControllerShortcutInputs(desktop[0].inputs) !== 'Ctrl + K + Caps + 鼠标右键') throw new Error('desktop labels failed');
const mixedDesktop = { ...baseline, inputs: [baseline.inputs[0], desktop[0].inputs[0]] };
if (validateControllerShortcutRules([mixedDesktop]).length !== 0) throw new Error('valid controller/keyboard chord rejected');
if (JSON.stringify(readControllerShortcutRules({shortcutRules:[mixedDesktop]})[0].inputs) !== JSON.stringify(mixedDesktop.inputs)) throw new Error('mixed source identities lost on reload');
const disabledConflict = { ...baseline, id: 'disabled-conflict', enabled: false };
if (validateControllerShortcutRules([baseline, disabledConflict]).length !== 0) throw new Error('disabled shortcut blocked an enabled rule');
// 2026-09-28 用户裁决（默认模板化）：默认项退居隐藏模板——
//   空配置 → 自动生成全部默认项；已有配置 → 只补“原生开关仍为开”的缺失项，
//   停用过的默认项不复活；自定义规则一律保留；输出“默认在前，自定义在后”。
const seededFromEmpty = reconcileControllerShortcutRules([], {});
if (seededFromEmpty.rules.length !== NATIVE_DEFAULT_SHORTCUTS.length || seededFromEmpty.seeded !== NATIVE_DEFAULT_SHORTCUTS.length) throw new Error('empty config did not seed every native default');
const reconciled = reconcileControllerShortcutRules([{ ...baseline, id: 'custom-summon' }], { enabled: true });
if (reconciled.seeded !== NATIVE_DEFAULT_SHORTCUTS.length || reconciled.rules.length !== NATIVE_DEFAULT_SHORTCUTS.length + 1) throw new Error('custom-only config must inject defaults while native switches stay on');
if (!reconciled.rules.some((rule) => rule.id === 'custom-summon' && rule.origin === 'custom')) throw new Error('custom rule was dropped by reconcile');
if (reconciled.rules.findIndex((rule) => rule.origin === 'custom') !== reconciled.rules.length - 1) throw new Error('defaults must precede custom rules');
const trimmed = reconcileControllerShortcutRules([{ ...baseline, id: 'custom-only' }], { enabled: true, killGame: false });
if (trimmed.seeded !== NATIVE_DEFAULT_SHORTCUTS.length - 1) throw new Error('disabled native switch must suppress its default row');
if (trimmed.rules.some((rule) => rule.defaultKey === 'killGame')) throw new Error('disabled native default must not resurrect');
if (!trimmed.rules.some((rule) => rule.defaultKey === 'enabled' && rule.enabled)) throw new Error('enabled native default must be injected on upgrade');
// 键组可重复（不再有占用检查）。
const sameChordDiffTrigger = { ...baseline, id: 'same-chord-diff-trigger', trigger: 'press' as const };
if (validateControllerShortcutRules([baseline, sameChordDiffTrigger]).length !== 0) throw new Error('same chord different trigger must now be allowed');
// 自定义规则：单手柄键任意触发方式均可（含“按下/弹起”，不再拦截）。
const singleButton = { ...baseline, id: 'single-button', inputs: [{ source: 'controller' as const, code: 'a' }] };
if (validateControllerShortcutRules([singleButton]).length !== 0) throw new Error('custom single-button hold was rejected');
const singlePress = { ...baseline, id: 'single-press', inputs: [{ source: 'controller' as const, code: 'a' }], trigger: 'press' as const };
if (validateControllerShortcutRules([singlePress]).length !== 0) throw new Error('single-button press must now be allowed');
const singleRelease = { ...baseline, id: 'single-release', inputs: [{ source: 'controller' as const, code: 'a' }], trigger: 'release' as const };
if (validateControllerShortcutRules([singleRelease]).length !== 0) throw new Error('single-button release must now be allowed');
const singleDouble = { ...baseline, id: 'single-double', inputs: [{ source: 'controller' as const, code: 'b' }], trigger: 'double' as const };
if (validateControllerShortcutRules([singleDouble]).length !== 0) throw new Error('single-button double was rejected');
// 专用/背部键（oem）单键任意触发方式允许（不受单键限制）
const oemPress = { ...baseline, id: 'oem-press', inputs: [{ source: 'oem' as const, code: 'm1' }], trigger: 'press' as const };
if (validateControllerShortcutRules([oemPress]).length !== 0) throw new Error('oem single-key press was rejected');
// 专属键：单键及混合组合均保留真实source/code；未知设备能力仍由runtime检查
const oemRule = { ...baseline, id: 'oem-rule', inputs: [{ source: 'oem' as const, code: 'm1' }] };
if (validateControllerShortcutRules([oemRule]).length !== 0) throw new Error('oem single-key rule rejected');
const mixedOem = { ...baseline, id: 'oem-mix', inputs: [{ source: 'oem' as const, code: 'm1' }, { source: 'controller' as const, code: 'a' }] };
if (validateControllerShortcutRules([mixedOem]).length !== 0) throw new Error('valid oem/controller chord rejected');
if (JSON.stringify(readControllerShortcutRules({shortcutRules:[mixedOem]})[0].inputs) !== JSON.stringify(mixedOem.inputs)) throw new Error('oem/controller identities lost on reload');
if (validateControllerShortcutRules([{...mixedOem,inputs:[mixedOem.inputs[0],mixedOem.inputs[0]]}]).length === 0) throw new Error('duplicate physical key accepted');
if (shortcutRecordMode(oemRule.inputs) !== 'controller') throw new Error('OEM keys must use the handheld recording presentation without changing physical source');
// 机型库规范化（native oem.keys.get JSON → 快照）：正反推演
const rogNative = {
  familyId: 'asus-rog-ally-x',
  supported: true,
  keys: [
    { keyId: 'cc', label: 'CC', backIndex: 0, triggerCapability: 'full', physicalForm: 'hid-button' },
    { keyId: 'ac', label: 'AC', backIndex: 0, triggerCapability: 'full', physicalForm: 'hid-button' },
    { keyId: 'm1', label: 'M1', backIndex: 1, triggerCapability: 'full', physicalForm: 'keyboard-vk' },
    { keyId: 'm2', label: 'M2', backIndex: 2, triggerCapability: 'full', physicalForm: 'keyboard-vk' },
    { keyId: 'lib', label: 'LIB', backIndex: 0, triggerCapability: 'full', physicalForm: 'hid-button' },
  ],
};
const rogSnapshot = normalizeOemKeysSnapshot(rogNative);
if (!rogSnapshot.supported || rogSnapshot.familyId !== 'asus-rog-ally-x' || rogSnapshot.keys.length !== 5) throw new Error('rog catalog normalization failed');
// 背部键 backIndex 保留 1/2；非背部键 backIndex=0（native 语义）统一归一为 null
if (rogSnapshot.keys[2].keyId !== 'm1' || rogSnapshot.keys[2].backIndex !== 1 || rogSnapshot.keys[2].physicalForm !== 'keyboard-vk') throw new Error('rog m1 key field mapping failed');
if (rogSnapshot.keys[0].keyId !== 'cc' || rogSnapshot.keys[0].backIndex !== null) throw new Error('rog non-back key must normalize to backIndex=null');
if (rogSnapshot.keys[4].keyId !== 'lib' || rogSnapshot.keys[4].backIndex !== null) throw new Error('rog lib key must normalize to backIndex=null');
// 分组：背部键(m1/m2)只进背部卡牌；非背部键(cc/ac/lib)只进专属按钮区——不重复
if (rogSnapshot.keys.filter((k) => k.backIndex !== null).map((k) => k.keyId).join(',') !== 'm1,m2') throw new Error('rog back-group split failed');
if (rogSnapshot.keys.filter((k) => k.backIndex === null).map((k) => k.keyId).join(',') !== 'cc,ac,lib') throw new Error('rog special-group split failed');
const msiNative = {
  familyId: 'msi-claw',
  supported: true,
  keys: [
    { keyId: 'claw', label: 'CLAW', backIndex: 0, triggerCapability: 'click-only', physicalForm: 'wmi-event' },
    { keyId: 'qs', label: 'QS', backIndex: 0, triggerCapability: 'click-only', physicalForm: 'wmi-event' },
    { keyId: 'm1', label: 'M1', backIndex: 1, triggerCapability: 'full', physicalForm: 'hid-button' },
  ],
};
const msiSnapshot = normalizeOemKeysSnapshot(msiNative);
if (msiSnapshot.keys[0].triggerCapability !== 'click-only' || msiSnapshot.keys[2].triggerCapability !== 'full') throw new Error('msi click-only mapping failed');
// 反推演：损坏/未知字段 fail-closed
const broken = normalizeOemKeysSnapshot({ familyId: 42, supported: true, keys: [{ keyId: '', label: '' }, { label: 'X' }, null] });
if (broken.supported || broken.familyId !== 'unknown' || broken.keys.length !== 0) throw new Error('broken catalog was not fail-closed');
const empty = normalizeOemKeysSnapshot(null);
if (empty.supported || empty.keys.length !== 0) throw new Error('null catalog was not fail-closed');
const unsupportedButKeys = normalizeOemKeysSnapshot({ familyId: 'x', supported: false, keys: [{ keyId: 'a', label: 'A', backIndex: 0 }] });
if (unsupportedButKeys.supported) throw new Error('supported=false with keys was not respected');
// Start → F7 is no longer a default; imported enabled legacy flags cannot seed it.
if (NATIVE_DEFAULT_SHORTCUTS.some((d) => String(d.defaultKey) === 'startDoubleF7')) throw new Error('retired Start/F7 remains a default');
const legacyF7 = {
  ...newControllerShortcutRule('legacy-start-f7', 'keyboard.numeric'),
  id: 'native-startDoubleF7', origin: 'native-default' as const, defaultKey: 'startDoubleF7',
  inputs: [{ source: 'controller' as const, code: 'start' }], trigger: 'double' as const,
  params: { key: 'F7' },
};
const customF7 = { ...legacyF7, id: 'custom-start-f7', origin: 'custom' as const };
const legacyFlags = { enabled: true, startDoubleF7: true };
for (const saved of [[], [legacyF7], [legacyF7, customF7]]) {
  const result = reconcileControllerShortcutRules(saved, legacyFlags);
  if (result.rules.some(rule => rule.origin === 'native-default' && rule.defaultKey === 'startDoubleF7')) throw new Error('retired default was seeded or restored');
  if (saved.includes(customF7) && !result.rules.some(rule => rule.id === customF7.id && rule.params.key === 'F7')) throw new Error('user-created Start/F7 was removed');
  if (!result.rules.some(rule => rule.defaultKey === 'enabled') || !result.rules.some(rule => rule.defaultKey === 'bDoubleMinimize')) throw new Error('other defaults changed');
  const reloaded = reconcileControllerShortcutRules(readControllerShortcutRules({ shortcutRules: result.rules }), legacyFlags);
  if (reloaded.seeded !== 0 || JSON.stringify(reloaded.rules) !== JSON.stringify(result.rules)) throw new Error('default retirement did not stay stable across reload');
}
if (validateControllerShortcutRules([customF7]).length !== 0) throw new Error('user-created Start/F7 was rejected');
console.log('Start/F7 default retirement: empty config, legacy true, persisted default, custom preservation and reload PASS');
// New switches store only a rule parameter, never global/game/startup intent.
for (const [id, label, presets] of [
  ['input.gyroToggle', '陀螺仪开关', ['fps', 'racing', 'custom', 'steam']],
  ['input.virtualGamepadToggle', '虚拟手柄开关', ['steamdeck', 'dualsense-edge', 'elite']],
] as const) {
  const action = actionById(id);
  if (!action?.available || action.scope !== 'native-global' || action.label !== label || !action.description?.includes('不记忆')) throw new Error(id + ' action missing/runtime semantics wrong');
  const values = action.parameters?.[0]?.options?.map(option => option.value);
  if (JSON.stringify(values) !== JSON.stringify(presets)) throw new Error(id + ' preset choices mismatch');
  for (const preset of presets) {
    const rule = { ...newControllerShortcutRule('switch-' + preset, id), params: { preset } };
    if (validateControllerShortcutRules([rule]).length) throw new Error(id + ' rejected preset ' + preset);
    const read = readControllerShortcutRules({ shortcutRules: [rule] });
    if (read.length !== 1 || read[0].params.preset !== preset) throw new Error(id + ' lost saved preset');
    if (reconcileControllerShortcutRules(read, { enabled: false }).rules.at(-1)?.actionId !== id) throw new Error(id + ' lost custom switch during reconciliation');
  }
  const invalid = { ...newControllerShortcutRule('bad-preset', id), params: { preset: 'invalid' } };
  if (!validateControllerShortcutRules([invalid]).length) throw new Error(id + ' accepted invalid preset');
}
console.log('controller shortcut rules selftest: PASS (including runtime-only input toggles)');

const mergeRule = (id: string, key = 'F8') => ({ ...newControllerShortcutRule(id, 'keyboard.numeric'), id, inputs: [{ source: 'oem' as const, code: 'm1' }], params: { key } });
let mergeCases = 0;
const mergeCheck = (ok: boolean, name: string) => { if (!ok) throw new Error(name); mergeCases++; };
const a = mergeRule('a'), b = mergeRule('b');
const mergedAdds = mergeControllerShortcutDraft([a], [a, mergeRule('local')], [a, mergeRule('remote')]);
mergeCheck(mergedAdds.ok && mergedAdds.rules.map(r=>r.id).join(',') === 'a,remote,local', 'disjoint rule additions preserve saved order and local addition');
const identical = mergeControllerShortcutDraft([a], [mergeRule('a','F9')], [mergeRule('a','F9')]);mergeCheck(identical.ok && identical.rules[0].params.key === 'F9', 'identical simultaneous edits converge');
const conflicting = mergeControllerShortcutDraft([a], [mergeRule('a','F9')], [mergeRule('a','F10')]);mergeCheck(!conflicting.ok && conflicting.conflicts[0]==='a','same-id edit conflicts');
mergeCheck(mergeControllerShortcutDraft([a], [], [a]).ok, 'local deletion with unchanged remote accepted');
mergeCheck(mergeControllerShortcutDraft([a], [a], []).ok, 'remote deletion with unchanged local accepted');
mergeCheck(!mergeControllerShortcutDraft([a], [], [mergeRule('a','F9')]).ok, 'local delete vs remote edit holds');
mergeCheck(!mergeControllerShortcutDraft([a], [mergeRule('a','F9')], []).ok, 'local edit vs remote delete holds');
const bothDeleted = mergeControllerShortcutDraft([a], [], []);mergeCheck(bothDeleted.ok && bothDeleted.rules.length===0, 'both deletions converge');
const independent = mergeControllerShortcutDraft([a,b], [mergeRule('a','F9'),b], [a,mergeRule('b','F10')]);mergeCheck(independent.ok && independent.rules[0].params.key==='F9' && independent.rules[1].params.key==='F10','different rule edits merge');
mergeCheck(!mergeControllerShortcutDraft([a,a], [a], [a]).ok, 'duplicate baseline id rejected');
mergeCheck(!mergeControllerShortcutDraft([a], [a,a], [a]).ok, 'duplicate draft id rejected');
mergeCheck(!mergeControllerShortcutDraft([a], [a], [a,a]).ok, 'duplicate remote id rejected');
const owned = mergeControllerShortcutDraft([a], [mergeRule('a','F9')], [a]);if(owned.ok){owned.rules[0].params.key='F11';owned.rules[0].inputs[0].code='m2';}mergeCheck(a.params.key==='F8' && a.inputs[0].code==='m1','merge owns independent output');
const keyOrderLocal = { ...a, params:{key:'F8',future:true} };const keyOrderRemote = { ...a, params:{future:true,key:'F8'} };mergeCheck(mergeControllerShortcutDraft([a],[keyOrderLocal],[keyOrderRemote]).ok,'parameter insertion order is not a conflict');
console.log(`shortcut draft merge: ${mergeCases} cases PASS`);

const frontend = actionById('input.frontendButton');
if (!frontend?.available || frontend.scope !== 'native-global') throw new Error('frontend action is not native/global');
if (defaultParameters('input.frontendButton').button !== 'steam') throw new Error('frontend button default is wrong');
if (frontend.parameters?.[0].options?.map(option => option.value).join(',') !== 'steam,steamQuickAccess,ps,xbox,gamebar') throw new Error('frontend options changed');
for (const button of ['steam','steamQuickAccess','ps','xbox','gamebar']) {
  const rule = { ...newControllerShortcutRule('frontend-' + button, 'input.frontendButton'), params: { button } };
  if (validateControllerShortcutRules([rule]).length) throw new Error('valid frontend button rejected: ' + button);
  const read = readControllerShortcutRules({ shortcutRules: [rule] });
  if (read[0]?.params.button !== button) throw new Error('frontend button lost after reload');
}
const badFrontend = { ...newControllerShortcutRule('bad-frontend','input.frontendButton'), params: {button: 'invalid'} };
if (!validateControllerShortcutRules([badFrontend]).length) throw new Error('invalid frontend button accepted');
const hiddenIds = CONTROLLER_SHORTCUT_ACTIONS.filter(action => !action.available).map(action => action.id);
// Radial testing admission starts closed; its retained schema stays available for saved-rule round trips.
if (hiddenIds.join(',') !== 'radial.summon,app.exit,performance.autoAdjust,performance.editMode,performance.switchMode') throw new Error('hidden schema unexpectedly removed/changed');
for (const id of hiddenIds) {
  if (isControllerShortcutActionVisible(id)) throw new Error('pending action visible: ' + id);
  const rule = newControllerShortcutRule('hidden-' + id, id);
  if (readControllerShortcutRules({ shortcutRules: [rule] })[0]?.actionId !== id) throw new Error('hidden saved action lost: ' + id);
}
if (!isControllerShortcutActionVisible('input.frontendButton')) throw new Error('frontend action hidden');
console.log('frontend buttons/hidden action schema: PASS');
