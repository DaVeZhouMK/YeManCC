<script setup lang="ts">
import { computed, nextTick, onActivated, onBeforeUnmount, onMounted, ref } from 'vue';
import Dropdown from '@/components/Dropdown.vue';
import GamepadVisualizer from '@/components/GamepadVisualizer.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import SegButton from '@/components/SegButton.vue';
import { on as onIpc, isNativeRuntime } from '@/bridge/ipc';
import { oemKeysGet, setShortcutRecording, summonGet, summonSet, type GamepadSettings } from '@/bridge/yeman';
import { compareAndSwapInputSettings, loadSettings, type InputSettingsSnapshot } from '@/bridge/settingsRepository';
import { focusGamepadElement } from '@/gamepad/focus';
import {
  CONTROLLER_BUTTONS,
  CONTROLLER_SHORTCUT_MAX_INPUTS,
  CONTROLLER_SHORTCUT_MIN_INPUTS,
  CONTROLLER_SHORTCUT_ACTIONS,
  CONTROLLER_TRIGGER_OPTIONS,
  NATIVE_DEFAULT_SHORTCUTS,
  OEM_EMPTY_SNAPSHOT,
  actionById,
  isControllerShortcutActionVisible,
  defaultParameters,
  formatControllerShortcutInputs,
  newControllerShortcutRule,
  nativeDefaultRule,
  reconcileControllerShortcutRules,
  readControllerShortcutRules,
  mergeControllerShortcutDraft,
  shortcutRecordMode,
  validateControllerShortcutRules,
  type ControllerButtonCode,
  type ControllerShortcutInput,
  type ControllerShortcutRecordMode,
  type ControllerShortcutRule,
  type OemKeysSnapshot,
  type OemSpecialKey,
} from '@/bridge/controllerShortcutRules';

const emit = defineEmits<{ (event: 'close'): void }>();
const dialogEl = ref<HTMLElement | null>(null);
const snapshot = ref<InputSettingsSnapshot | null>(null);
const gamepad = ref<GamepadSettings>({ enabled: true, bDoubleMinimize: true, startDoubleF7: true, tdpShortcut: true, fpsShortcut: true, killGame: true, openKeyboard: true, returnDesktop: true, mouseToggle: true, mouseBackend: 'joyxoff' });
const rules = ref<ControllerShortcutRule[]>([]);
const activeId = ref('');
const dirty = ref(false);
const busy = ref(false);
const status = ref('读取规则配置…');
const recordMode = ref<ControllerShortcutRecordMode>('controller');
const recording = ref(false);
const recordingBusy = ref(false);
const recordingStatus = ref('选择录制模式后开始录入组合输入。');
// Keep an in-progress capture separate from the saved rule. Cancelling a
// recording must leave the previous shortcut untouched.
const recordingInputs = ref<ControllerShortcutInput[]>([]);
const editorScale = ref(1);
const allActionOptions = CONTROLLER_SHORTCUT_ACTIONS.filter((action) => action.available).map((action) => ({ value: action.id, label: `${action.category} · ${action.label}` }));
const recordModeOptions = [
  { value: 'controller', label: '手柄录制' },
  { value: 'keyboard-mouse', label: '键盘 + 鼠标' },
  { value: 'oem', label: '背部和专用' },
];
const holdTimeOptions = [100, 200, 300, 400, 500, 750, 1000, 1500, 2000, 3000, 5000].map((value) => ({ value, label: `${value} ms` }));
const repeatIntervalOptions = [40, 60, 80, 100, 150, 200, 250, 300, 500, 750, 1000].map((value) => ({ value, label: `${value} ms` }));
const visibleRules = computed(() => rules.value.filter((rule) => isControllerShortcutActionVisible(rule.actionId)));
const activeRule = computed(() => visibleRules.value.find((rule) => rule.id === activeId.value) || null);
const activeAction = computed(() => activeRule.value ? actionById(activeRule.value.actionId) : undefined);
const keyGridParameter = computed(() => activeAction.value?.parameters?.find((parameter) => parameter.presentation === 'key-grid'));
const validation = computed(() => validateControllerShortcutRules(visibleRules.value, oemKeys.value));

// ── 掌机专用键（背部/专用）输入：机型库清单由 native oem.keys.get 提供 ──
const oemKeys = ref<OemKeysSnapshot>(OEM_EMPTY_SNAPSHOT);
const oemSupported = computed(() => oemKeys.value.supported);
/** 背部键位 1-4 → 键（机型库 backIndex；视觉左上/右上/左下/右下，语义固定 1-4） */
const oemByBack = computed(() => {
  const map = new Map<number, OemSpecialKey>();
  for (const key of oemKeys.value.keys) {
    if (key.backIndex !== null && key.backIndex >= 1 && key.backIndex <= 4) map.set(key.backIndex, key);
  }
  return map;
});
/**
 * 专属按钮清单 = 仅非背部键（backIndex == null）。
 * 背部键（M1/M2/L4/R4 等）由背部卡牌区独占展示——同一物理键不得同时出现在
 * 专属按钮区和背部卡牌区（对齐 HC 每键单入口原则：背部组与 OEM 组分离）。
 * 排序沿用机型库 HC ButtonFlags 数值序。
 */
const oemSpecialKeys = computed(() => oemKeys.value.keys.filter((key) => key.backIndex === null));
const activeOemInput = computed(() => activeRule.value?.inputs.find((input) => input.source === 'oem') ?? null);
const activeOemKey = computed(() => {
  const input = activeOemInput.value;
  if (!input) return undefined;
  return oemKeys.value.keys.find((key) => key.keyId === input.code);
});
/** WMI 一次性事件（MSI CLAW/QS）仅支持 press/double；hold/release/repeat 禁用 */
const activeOemClickOnly = computed(() => activeOemKey.value?.triggerCapability === 'click-only');
const oemTriggerOptions = computed(() => {
  if (recordMode.value !== 'oem' || !activeOemClickOnly.value) return CONTROLLER_TRIGGER_OPTIONS;
  return CONTROLLER_TRIGGER_OPTIONS.filter((option) => option.value === 'press' || option.value === 'double');
});
function oemButtonLabel(index: number): string {
  return index < 26 ? `专属${String.fromCharCode(65 + index)}` : `专属${index + 1}`;
}
function bindOemKey(keyId: string): void {
  const key = oemKeys.value.keys.find((candidate) => candidate.keyId === keyId);
  const patch: Partial<ControllerShortcutRule> = { inputs: [{ source: 'oem', code: keyId }] };
  if (key?.triggerCapability === 'click-only' && activeRule.value?.trigger !== 'press' && activeRule.value?.trigger !== 'double') {
    patch.trigger = 'press';
  }
  updateRule(patch);
}
function clearOemInputs(): void {
  if (!activeRule.value) return;
  updateRule({ inputs: [] });
  status.value = '已清空专用键绑定。';
}
const actionOptions = computed(() => allActionOptions);
const highlightedButtons = computed(() => {
  if (!activeRule.value) return [];
  const inputs = recording.value ? recordingInputs.value : activeRule.value.inputs;
  return inputs
    .filter((input) => input.source === 'controller')
    .map((input) => CONTROLLER_BUTTONS.find((item) => item.value === input.code)?.gamepadIndex)
    .filter((value): value is number => value !== undefined);
});
// The saved button-mapping section is a configuration draft.  It is not a
// Coordinator/InputHost runtime receipt, so legacy or unknown persisted
// `closure: CLOSED` fields must not be presented as an executed route.
const closure = computed(() => 'UNENCLOSED');
const activeInputText = computed(() => {
  if (recording.value) return formatControllerShortcutInputs(recordingInputs.value) || '等待录入组合输入';
  return activeRule.value ? formatControllerShortcutInputs(activeRule.value.inputs) || '尚未录入组合输入' : '未选择规则';
});
const visibleRecordedInputs = computed(() => recording.value ? recordingInputs.value : (activeRule.value?.inputs || []));
const editorStageStyle = computed(() => ({ '--editor-scale': String(editorScale.value) }));

const controllerCodes = new Set(CONTROLLER_BUTTONS.map((item) => item.value));
const desktopHeld = new Set<string>();
const desktopRecorded = new Map<string, ControllerShortcutInput>();
let desktopReleaseTimer: ReturnType<typeof window.setTimeout> | null = null;
let stopGamepadRecording: (() => void) | null = null;
let stopGamepadRecordingState: (() => void) | null = null;

const EDITOR_DESIGN_WIDTH = 1280;
const EDITOR_DESIGN_HEIGHT = 720;
// Keep only a small safety margin. The editor is intended to use the usable
// viewport rather than float as a small dialog inside it.
const EDITOR_GUTTER = 12;
// On tall/high-resolution handheld screens a 1280x720 design surface may be
// enlarged. Capping at 1.6 keeps the text readable without making it absurdly
// large on a 4K desktop.
const EDITOR_MAX_SCALE = 1.6;

function inputIdentity(input: ControllerShortcutInput): string {
  return `${input.source}:${input.code}`;
}

function uniqueInputs(inputs: readonly ControllerShortcutInput[]): ControllerShortcutInput[] {
  return inputs.filter((input, index, all) => all.findIndex((candidate) => inputIdentity(candidate) === inputIdentity(input)) === index);
}

/** Keep the 16:9 three-pane workbench intact at every viewport size. */
function updateEditorScale(): void {
  // WebView2 can report a visualViewport in device/CSS scale units while the
  // layout viewport (and screenshot) remains in window CSS pixels. Preferring
  // visualViewport here caused a second shrink on high-DPI/handheld systems.
  // Use the layout viewport as the authoritative size and only fall back to
  // visualViewport when the host has not published a usable window size yet.
  const width = window.innerWidth > 0 ? window.innerWidth : (window.visualViewport?.width ?? 0);
  const height = window.innerHeight > 0 ? window.innerHeight : (window.visualViewport?.height ?? 0);
  const scale = Math.min(
    EDITOR_MAX_SCALE,
    (width - EDITOR_GUTTER) / EDITOR_DESIGN_WIDTH,
    (height - EDITOR_GUTTER) / EDITOR_DESIGN_HEIGHT,
  );
  editorScale.value = Number.isFinite(scale) ? Math.max(0.1, scale) : 1;
}

function controllerInputs(...codes: ControllerButtonCode[]): ControllerShortcutInput[] {
  return codes.map((code) => ({ source: 'controller', code }));
}
function cloneRules(value: ControllerShortcutRule[]): ControllerShortcutRule[] {
  return value.map((rule) => ({ ...rule, inputs: rule.inputs.map((input) => ({ ...input })), params: { ...rule.params } }));
}
function nativeEnabledMap(): Record<string, boolean> {
  return Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map((definition) => [definition.defaultKey, gamepad.value[definition.defaultKey]]));
}
/** 新建规则的默认动作：优先未占用的可用动作（仅作便利默认，不再限制重复绑定）。 */
function availableActionId(): string {
  const occupied = new Set(rules.value.filter((rule) => rule.enabled).map((rule) => rule.actionId));
  return allActionOptions.find((option) => actionById(option.value)?.available && !occupied.has(option.value))?.value
    ?? allActionOptions.find((option) => actionById(option.value)?.available)?.value
    ?? allActionOptions[0]?.value
    ?? 'window.summon';
}
function selectRule(id: string): void {
  if (recording.value || recordingBusy.value) return;
  const selected = visibleRules.value.find((rule) => rule.id === id);
  activeId.value = selected?.id || '';
  if (selected) recordMode.value = shortcutRecordMode(selected.inputs);
}
function touch(): void { dirty.value = true; }
function updateRule(patch: Partial<ControllerShortcutRule>): void {
  const current = activeRule.value;
  if (!current) return;
  rules.value = rules.value.map((rule) => rule.id === current.id ? { ...rule, ...patch } : rule);
  touch();
}
function chooseAction(value: string | number): void {
  if (activeRule.value?.origin === 'native-default') {
    status.value = '默认快捷的动作归属固定；可以编辑组合与触发方式，不能把它改成另一个功能。';
    return;
  }
  const actionId = String(value);
  if (!isControllerShortcutActionVisible(actionId)) return;
  updateRule({
    actionId,
    params: defaultParameters(actionId),
    ...(actionId.startsWith('keyboard.') ? { trigger: 'press' as const } : {}),
  });
}
function updateNumber(key: 'holdMs' | 'doubleWindowMs' | 'intervalMs', event: Event): void {
  const value = Number((event.target as HTMLInputElement).value);
  if (!Number.isFinite(value)) return;
  updateRule({ [key]: Math.round(value) });
}
function updateTiming(key: 'holdMs' | 'doubleWindowMs' | 'intervalMs', value: string | number): void {
  const numeric = Number(value);
  if (Number.isFinite(numeric)) updateRule({ [key]: Math.round(numeric) });
}
function updateParameter(id: string, value: string | number | boolean): void {
  const current = activeRule.value;
  if (!current) return;
  updateRule({ params: { ...current.params, [id]: value } });
}
function addRule(): void {
  const actionId = availableActionId();
  const next = newControllerShortcutRule(`${Date.now()}-${rules.value.length}`, actionId);
  rules.value = [...rules.value, next];
  selectRule(next.id);
  touch();
}
function removeRule(): void {
  const current = activeRule.value;
  if (!current) return;
  const index = visibleRules.value.findIndex((rule) => rule.id === current.id);
  rules.value = rules.value.filter((rule) => rule.id !== current.id);
  selectRule(visibleRules.value[Math.max(0, index - 1)]?.id || '');
  touch();
}
function restoreNativeDefaults(): void {
  const entries = NATIVE_DEFAULT_SHORTCUTS.map((definition) => nativeDefaultRule(definition.defaultKey, true));
  gamepad.value = {
    ...gamepad.value,
    ...Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map((definition) => [definition.defaultKey, true])),
  };
  // Hidden legacy actions are not user-reachable, but resetting visible defaults
  // must not silently delete their previously saved definitions.
  rules.value = [...entries, ...rules.value.filter((rule) => !isControllerShortcutActionVisible(rule.actionId))];
  selectRule(entries.find((rule) => rule.enabled)?.id || entries[0]?.id || '');
  touch();
  status.value = '已恢复原版默认设置；点击“应用规则”后写入。';
}
async function load(): Promise<void> {
  const settings = await loadSettings();
  snapshot.value = settings.input;
  gamepad.value = await summonGet().catch(() => gamepad.value);
  oemKeys.value = await oemKeysGet().catch(() => OEM_EMPTY_SNAPSHOT);
  const persisted = readControllerShortcutRules(settings.input.buttonMapping?.rules);
  const reconciled = reconcileControllerShortcutRules(persisted, nativeEnabledMap());
  rules.value = reconciled.rules;
  selectRule(visibleRules.value.find((rule) => rule.enabled)?.id || visibleRules.value[0]?.id || '');
  dirty.value = false;
  status.value = reconciled.seeded > 0
    ? `已按默认模板生成 ${reconciled.seeded} 条默认快捷；修改后点击“应用规则”才会写入配置。`
    : visibleRules.value.length ? '已读取规则草稿。修改后需点击“应用规则”才会写入配置。' : '尚无快捷规则。可新建规则，或恢复默认快捷键。';
}
async function apply(): Promise<void> {
  if (!snapshot.value || busy.value || recording.value) {
    if (recording.value) status.value = '请先完成或取消当前录制。';
    return;
  }
  if (validation.value.length) { status.value = validation.value[0]; return; }
  busy.value = true;
  try {
    const baseline = snapshot.value;
    const draft = cloneRules(rules.value);
    let savedRules = draft;
    const patchFor = (entries: ControllerShortcutRule[]) => ({
      buttonMapping: { rules: { schemaVersion: 2, closure: 'UNENCLOSED', shortcutRules: cloneRules(entries) } },
    });
    let result = await compareAndSwapInputSettings(baseline.revision, patchFor(savedRules));
    if (result.ok === false && result.reason === 'revision-conflict') {
      const merged = mergeControllerShortcutDraft(
        readControllerShortcutRules(baseline.buttonMapping?.rules), draft,
        readControllerShortcutRules(result.value.buttonMapping?.rules),
      );
      if (merged.ok === false) {
        dirty.value = true;
        status.value = `规则 ${merged.conflicts.join('、')} 已被其它页面改动，草稿已保留；关闭重开可读取最新规则。`;
        return;
      }
      const problems = validateControllerShortcutRules(merged.rules.filter((rule) => isControllerShortcutActionVisible(rule.actionId)), oemKeys.value);
      if (problems.length) { dirty.value = true; status.value = `合并未保存，草稿已保留：${problems[0]}`; return; }
      savedRules = merged.rules;
      result = await compareAndSwapInputSettings(result.value.revision, patchFor(savedRules)); // one retry only
    }
    if (result.ok === false) {
      dirty.value = true;
      status.value = result.reason === 'revision-conflict' ? '配置仍在变更，草稿已保留，请稍后再次应用。'
        : result.reason === 'write-failed' ? '保存失败，草稿已保留，请重试。'
        : result.reason === 'cancelled' ? '保存已取消，草稿已保留。' : '专属配置正在接管，草稿已保留。';
      return;
    }
    snapshot.value = result.value;
    rules.value = cloneRules(savedRules);
    const nativeSettings = Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map((definition) => [definition.defaultKey, savedRules.some((rule) => rule.origin === 'native-default' && rule.defaultKey === definition.defaultKey && rule.enabled)]));
    gamepad.value = await summonGet().catch(() => gamepad.value);
    let nativeSyncFailed = false;
    gamepad.value = await summonSet(nativeSettings).catch(() => { nativeSyncFailed = true; return gamepad.value; });
    dirty.value = nativeSyncFailed;
    // 单源同步（2026-09-28）：把原生开关推给前端手柄引擎，并通知控制器页卡片
    // 重新读取同一份规则，避免卡片停留在旧值（此前编辑器保存后两处不同步）。
    window.dispatchEvent(new CustomEvent('ipc:gamepad.settings', { detail: gamepad.value }));
    window.dispatchEvent(new CustomEvent('ipc:controller-shortcuts.updated'));
    status.value = nativeSyncFailed ? '规则已保存，但默认快捷开关未同步，草稿已保留，请再次应用。'
      : '规则已保存。请按键验证动作；保存成功不代表设备动作已执行。';
  } catch (error) { status.value = `保存失败：${(error as Error).message}`; }
  finally { busy.value = false; }
}

function clearDesktopRecording(): void {
  desktopHeld.clear();
  desktopRecorded.clear();
  recordingInputs.value = [];
  if (desktopReleaseTimer !== null) { window.clearTimeout(desktopReleaseTimer); desktopReleaseTimer = null; }
}
async function stopNativeRecording(): Promise<void> {
  if (!isNativeRuntime) return;
  await setShortcutRecording(false).catch(() => undefined);
}
function finishRecording(inputs: ControllerShortcutInput[]): void {
  if (!recording.value) return;
  const normalized = uniqueInputs(inputs);
  recordingInputs.value = normalized.slice(0, CONTROLLER_SHORTCUT_MAX_INPUTS);
  if (normalized.length > CONTROLLER_SHORTCUT_MAX_INPUTS) {
    recordingStatus.value = `组合最多录入 ${CONTROLLER_SHORTCUT_MAX_INPUTS} 个按键；已暂存前 ${CONTROLLER_SHORTCUT_MAX_INPUTS} 个，请取消后重录。`;
    return;
  }
  if (normalized.length < CONTROLLER_SHORTCUT_MIN_INPUTS) {
    recordingInputs.value = [];
    if (recordMode.value === 'keyboard-mouse') clearDesktopRecording();
    recordingStatus.value = `此次组合只有 ${normalized.length} 个按键，未写入；请同时按下至少 ${CONTROLLER_SHORTCUT_MIN_INPUTS} 个按键后再松开。`;
    return;
  }
  updateRule({ inputs: normalized });
  recording.value = false;
  recordingStatus.value = `已录入 ${normalized.length}/${CONTROLLER_SHORTCUT_MAX_INPUTS} 个按键：${formatControllerShortcutInputs(normalized)}`;
  void stopNativeRecording();
  clearDesktopRecording();
}
function cancelRecording(reason = '已取消录制。'): void {
  if (!recording.value && !recordingBusy.value) return;
  recording.value = false;
  recordingBusy.value = false;
  recordingInputs.value = [];
  recordingStatus.value = reason;
  clearDesktopRecording();
  void stopNativeRecording();
}
async function startRecording(): Promise<void> {
  if (!activeRule.value || busy.value || recording.value || recordingBusy.value) return;
  if (recordMode.value === 'oem') {
    recordingStatus.value = '背部和专用模式为按键点选，无需录制。';
    return;
  }
  recordingInputs.value = [];
  if (recordMode.value === 'keyboard-mouse') {
    recordingBusy.value = true;
    try {
      // Native desktop-toggle rules must not run while capturing their inputs.
      if (isNativeRuntime) {
        const result = await setShortcutRecording(true);
        if (!result.active) { recordingStatus.value = `无法开始键鼠录制：${result.reason}`; return; }
      }
      clearDesktopRecording();
      recording.value = true;
      recordingStatus.value = `请按住组合输入，全部松开后自动写入。至少 ${CONTROLLER_SHORTCUT_MIN_INPUTS} 个，最多 ${CONTROLLER_SHORTCUT_MAX_INPUTS} 个；后 2 个槽位可留空。录制期间不会触发 YMCC 快捷。`;
      await nextTick();
      dialogEl.value?.focus();
    } catch (error) { recordingStatus.value = `键鼠录制启动失败：${(error as Error).message}`; }
    finally { recordingBusy.value = false; }
    return;
  }
  if (!isNativeRuntime) {
    recordingStatus.value = '手柄录制仅在 YMCC 桌面程序中可用；浏览器预览不启动额外手柄读取。';
    return;
  }
  recordingBusy.value = true;
  try {
    const result = await setShortcutRecording(true);
    if (!result.active) { recordingStatus.value = `无法开始手柄录制：${result.reason}`; return; }
    recording.value = true;
    recordingStatus.value = `请按住所需手柄组合，全部松开后自动写入。至少 ${CONTROLLER_SHORTCUT_MIN_INPUTS} 个，最多 ${CONTROLLER_SHORTCUT_MAX_INPUTS} 个；后 2 个槽位可留空。录制期间不会触发 YMCC 快捷或页面导航。`;
  } catch (error) { recordingStatus.value = `手柄录制启动失败：${(error as Error).message}`; }
  finally { recordingBusy.value = false; }
}
function onRecordingMode(value: string | number): void {
  if (recording.value || recordingBusy.value) return;
  recordMode.value = value === 'keyboard-mouse' ? 'keyboard-mouse' : value === 'oem' ? 'oem' : 'controller';
}
function desktopInputFromMouse(button: number): ControllerShortcutInput | null {
  const code = ({ 0: 'MouseLeft', 1: 'MouseMiddle', 2: 'MouseRight', 3: 'MouseBack', 4: 'MouseForward' } as Record<number, string>)[button];
  return code ? { source: 'mouse', code } : null;
}
function armDesktopRelease(): void {
  if (desktopHeld.size !== 0 || !recording.value || recordMode.value !== 'keyboard-mouse') return;
  if (desktopReleaseTimer !== null) window.clearTimeout(desktopReleaseTimer);
  desktopReleaseTimer = window.setTimeout(() => {
    desktopReleaseTimer = null;
    finishRecording([...desktopRecorded.values()]);
  }, 30);
}
function onCaptureKeydown(event: KeyboardEvent): void {
  if (!recording.value || recordMode.value !== 'keyboard-mouse' || event.isComposing) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  const key = `keyboard:${event.code}`;
  desktopHeld.add(key);
  if (!desktopRecorded.has(key) && desktopRecorded.size >= CONTROLLER_SHORTCUT_MAX_INPUTS) {
    recordingStatus.value = `已达到 ${CONTROLLER_SHORTCUT_MAX_INPUTS} 个按键上限；请松开后确认，或取消后重录。`;
    return;
  }
  desktopRecorded.set(key, { source: 'keyboard', code: event.code });
  recordingInputs.value = [...desktopRecorded.values()];
}
function onCaptureKeyup(event: KeyboardEvent): void {
  if (!recording.value || recordMode.value !== 'keyboard-mouse') return;
  event.preventDefault();
  event.stopImmediatePropagation();
  desktopHeld.delete(`keyboard:${event.code}`);
  armDesktopRelease();
}
function onCapturePointerdown(event: PointerEvent): void {
  if (!recording.value || recordMode.value !== 'keyboard-mouse') return;
  const target = event.target as Element | null;
  if (target?.closest('[data-record-control]')) return;
  const input = desktopInputFromMouse(event.button);
  if (!input) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  const key = `${input.source}:${input.code}`;
  desktopHeld.add(key);
  if (!desktopRecorded.has(key) && desktopRecorded.size >= CONTROLLER_SHORTCUT_MAX_INPUTS) {
    recordingStatus.value = `已达到 ${CONTROLLER_SHORTCUT_MAX_INPUTS} 个按键上限；请松开后确认，或取消后重录。`;
    return;
  }
  desktopRecorded.set(key, input);
  recordingInputs.value = [...desktopRecorded.values()];
}
function onCapturePointerup(event: PointerEvent): void {
  if (!recording.value || recordMode.value !== 'keyboard-mouse') return;
  const target = event.target as Element | null;
  if (target?.closest('[data-record-control]')) return;
  const input = desktopInputFromMouse(event.button);
  if (!input) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  desktopHeld.delete(`${input.source}:${input.code}`);
  armDesktopRelease();
}
function onCaptureContextMenu(event: Event): void {
  if (recording.value && recordMode.value === 'keyboard-mouse') { event.preventDefault(); event.stopImmediatePropagation(); }
}
function onWindowBlur(): void { if (recording.value && recordMode.value === 'keyboard-mouse') cancelRecording('窗口失焦，键盘/鼠标录制已安全取消。'); }
function onNativeGamepadRecording(data: { codes?: unknown }): void {
  if (!recording.value || recordMode.value !== 'controller') return;
  const codes = Array.isArray(data?.codes) ? data.codes : [];
  const inputs = codes.filter((code): code is ControllerButtonCode => typeof code === 'string' && controllerCodes.has(code as ControllerButtonCode)).map((code) => ({ source: 'controller' as const, code }));
  recordingInputs.value = uniqueInputs(inputs).slice(0, CONTROLLER_SHORTCUT_MAX_INPUTS);
  finishRecording(inputs);
}
function onNativeRecordingStopped(data: { reason?: unknown }): void {
  if (!recording.value || recordMode.value !== 'controller') return;
  cancelRecording(`手柄录制已停止：${typeof data?.reason === 'string' ? data.reason : '输入所有权变化'}`);
}
/**
 * 2026-09-28 用户裁决：关闭=“取消并关闭”（不保存、直接退出）。
 * 顶部关闭 / B 键 / Esc / 点击遮罩全部走同一路径，未应用的草稿直接放弃，
 * 不再出现“关闭无反应”的拦截提示。
 */
function requestClose(): void {
  if (busy.value) return;
  cancel();
}
function cancel(): void {
  cancelRecording();
  // 关闭路径必须总是能退出：重读失败也照常关闭（页面激活时会重新加载）。
  void load().catch(() => undefined).finally(() => emit('close'));
}
function onKeydown(event: KeyboardEvent): void {
  if (recording.value && recordMode.value === 'keyboard-mouse') return;
  if (event.key === 'Escape') { event.preventDefault(); requestClose(); }
}

/**
 * Enter the editor through a real controller target. The dialog element is
 * intentionally tabindex=-1 for accessibility, but it is not a gamepad
 * control; leaving focus there makes the shared engine recover to the page
 * underneath on the next A/D-pad frame.
 */
function focusEditorControl(): void {
  const root = dialogEl.value;
  if (!root) return;
  const actionTrigger = root.querySelector<HTMLElement>(
    '.edit-column [data-gp-dropdown] button[aria-haspopup="listbox"]',
  );
  const firstControl = root.querySelector<HTMLElement>(
    'button:not(:disabled):not([tabindex="-1"]), input:not(:disabled):not([tabindex="-1"]), select:not(:disabled):not([tabindex="-1"]), [tabindex]:not([tabindex="-1"])',
  );
  focusGamepadElement(actionTrigger || firstControl);
}

/** Keep B inside the editor. If a teleported dropdown is open, its own
 * listener closes that menu first; otherwise B closes this editor bubble. */
function onGamepadBack(event: Event): void {
  const root = dialogEl.value;
  if (!root) return;
  // A Dropdown listener may already have consumed B in the same dispatch.
  // Respect that decision and keep the editor mounted.
  if (event.defaultPrevented) return;
  const openTrigger = Array.from(document.querySelectorAll<HTMLElement>(
    '[aria-expanded="true"][aria-haspopup="listbox"]',
  )).find((el) => root.contains(el));
  if (openTrigger) return;
  event.preventDefault();
  requestClose();
}

onMounted(() => {
  void load()
    .catch((error) => { status.value = `读取失败：${(error as Error).message}`; })
    .finally(() => { void nextTick(() => focusEditorControl()); });
  updateEditorScale();
  window.addEventListener('resize', updateEditorScale);
  window.visualViewport?.addEventListener('resize', updateEditorScale);
  document.addEventListener('keydown', onKeydown);
  window.addEventListener('ipc:gamepad-back', onGamepadBack);
  window.addEventListener('keydown', onCaptureKeydown, true);
  window.addEventListener('keyup', onCaptureKeyup, true);
  window.addEventListener('pointerdown', onCapturePointerdown, true);
  window.addEventListener('pointerup', onCapturePointerup, true);
  window.addEventListener('contextmenu', onCaptureContextMenu, true);
  window.addEventListener('blur', onWindowBlur);
  stopGamepadRecording = onIpc('shortcut.recording.gamepad', onNativeGamepadRecording);
  stopGamepadRecordingState = onIpc('shortcut.recording.stopped', onNativeRecordingStopped);
  nextTick(() => dialogEl.value?.focus());
});
onActivated(() => { if (!dirty.value && !recording.value) void load().catch(() => undefined); });
onBeforeUnmount(() => {
  window.removeEventListener('resize', updateEditorScale);
  window.visualViewport?.removeEventListener('resize', updateEditorScale);
  document.removeEventListener('keydown', onKeydown);
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
  window.removeEventListener('keydown', onCaptureKeydown, true);
  window.removeEventListener('keyup', onCaptureKeyup, true);
  window.removeEventListener('pointerdown', onCapturePointerdown, true);
  window.removeEventListener('pointerup', onCapturePointerup, true);
  window.removeEventListener('contextmenu', onCaptureContextMenu, true);
  window.removeEventListener('blur', onWindowBlur);
  stopGamepadRecording?.();
  stopGamepadRecordingState?.();
  clearDesktopRecording();
  void stopNativeRecording();
});
</script>

<template>
  <Teleport to="body">
    <div class="editor-overlay" data-gp-modal="controller-shortcut-editor" @pointerdown.self="requestClose">
      <div class="editor-stage" :style="editorStageStyle">
      <section ref="dialogEl" class="editor-dialog" role="dialog" aria-modal="true" aria-label="自定义快捷规则编辑器" tabindex="-1">
        <header class="editor-top">
          <button class="back" type="button" @click="requestClose"><InlineIcon name="list" /> 关闭</button>
          <div><span class="eyebrow">控制器 / 自定义快捷</span><h3><InlineIcon name="keyboard" /> 快捷规则编辑器</h3></div>
          <span class="state-chip" :class="{ dirty }">{{ dirty ? '未应用' : '已同步' }}</span>
        </header>

        <main class="editor-layout">
          <aside class="preview-column">
              <section class="card mirror-card">
                <div class="card-head"><div><span class="eyebrow">组合预览</span><h3>输入镜像</h3></div><span>{{ recordMode === 'controller' ? '手柄' : '键鼠' }}</span></div>
                <GamepadVisualizer :settings="gamepad" :highlight-buttons="highlightedButtons" :show-test-mode="false" />
              </section>
          </aside>

          <section class="rules-column">
            <section class="card rule-list-card">
              <div class="card-head"><div><span class="eyebrow">规则列表</span><h3>已知动作</h3></div></div>
              <div v-if="!visibleRules.length" class="empty"><p>尚无规则。</p><button class="small-action" type="button" @click="addRule">新建规则</button></div>
              <div v-else class="rule-list"><button v-for="rule in visibleRules" :key="rule.id" type="button" class="rule-row" :class="{ active: activeId === rule.id, disabled: !rule.enabled }" @click="selectRule(rule.id)"><span><b>{{ actionById(rule.actionId)?.label || '未知动作' }}</b><small>{{ formatControllerShortcutInputs(rule.inputs) || '未录入输入' }}</small></span><i>{{ rule.origin === 'native-default' ? '默认' : (shortcutRecordMode(rule.inputs) === 'controller' ? '手柄' : shortcutRecordMode(rule.inputs) === 'oem' ? '背部/专用' : '键鼠') }}</i></button></div>
            </section>
          </section>

          <section class="edit-column">
            <section v-if="activeRule" class="card rule-editor-card">
              <div class="card-head"><div><span class="eyebrow">正在编辑</span><h3>{{ activeAction?.label || '快捷规则' }}<span v-if="activeInputText" class="edit-head-preview">{{ activeInputText }}</span></h3></div><div class="edit-head-actions"><button class="small-action" type="button" :disabled="busy || recording" @click="addRule">+ 新建规则</button><button class="rule-enabled-action" :class="{ active: activeRule.enabled }" type="button" :disabled="busy || recording" @click="updateRule({ enabled: !activeRule.enabled })">{{ activeRule.enabled ? '已启用' : '已关闭' }}</button><button class="remove-action" type="button" :disabled="busy || recording" @click="removeRule">删除动作</button></div></div>
              <div class="editor-bubble action-trigger-bubble"><div class="action-trigger-grid"><div><span class="field-label">动作</span><Dropdown :model-value="activeRule.actionId" :options="actionOptions" color="accent" :disabled="busy || recording || activeRule.origin === 'native-default'" @update:model-value="chooseAction" /><p class="field-hint">{{ activeRule.origin === 'native-default' ? '默认快捷归属固定；可编辑组合和触发方式' : (activeAction?.scope === 'native-global' ? '已有原生动作' : activeAction?.scope === 'frontend' ? '前端动作' : '原生调节动作') }}{{ activeAction?.available ? '' : ' · 当前不可执行' }}</p></div><div><span class="field-label">触发方式</span><SegButton :model-value="activeRule.trigger" :options="oemTriggerOptions" full :disabled="busy || recording" @update:model-value="updateRule({ trigger: $event as ControllerShortcutRule['trigger'] })" /><p v-if="activeOemClickOnly" class="field-hint">当前专用键为一次性点击事件（WMI），仅支持按下/双击。</p><div class="timing-row"><label v-if="activeRule.trigger === 'hold' || activeRule.trigger === 'repeat'"><span>长按阈值</span><Dropdown :model-value="activeRule.holdMs" :options="holdTimeOptions" color="accent" :disabled="busy || recording" @update:model-value="updateTiming('holdMs', $event)" /></label><label v-if="activeRule.trigger === 'double'"><span>双击窗口</span><Dropdown :model-value="activeRule.doubleWindowMs" :options="holdTimeOptions" color="accent" :disabled="busy || recording" @update:model-value="updateTiming('doubleWindowMs', $event)" /></label><label v-if="activeRule.trigger === 'repeat'"><span>连发间隔</span><Dropdown :model-value="activeRule.intervalMs" :options="repeatIntervalOptions" color="accent" :disabled="busy || recording" @update:model-value="updateTiming('intervalMs', $event)" /></label></div></div></div></div>
              <div class="editor-bubble recorder-bubble"><span class="field-label">组合输入 <small>{{ recordMode === 'oem' ? '背部/专用键点选' : `${visibleRecordedInputs.length}/${CONTROLLER_SHORTCUT_MAX_INPUTS} · 至少 ${CONTROLLER_SHORTCUT_MIN_INPUTS} 个` }}</small></span><SegButton :model-value="recordMode" :options="recordModeOptions" full :disabled="busy || recording || recordingBusy" @update:model-value="onRecordingMode" /><template v-if="recordMode === 'oem'"><div class="oem-back-slots"><button v-for="slot in 4" :key="slot" type="button" class="oem-back-slot" :class="{ active: activeOemInput?.code === (oemByBack.get(slot)?.keyId ?? null), unsupported: !oemByBack.has(slot) }" :disabled="!oemSupported || !oemByBack.has(slot) || busy || recording" :title="oemByBack.get(slot)?.label ?? ''" @click="bindOemKey(oemByBack.get(slot)!.keyId)"><span class="oem-back-slot-index">{{ ['左上', '右上', '左下', '右下'][slot - 1] }}</span><span class="oem-back-slot-name">背部{{ slot }}</span><span class="oem-back-slot-key">{{ oemByBack.get(slot)?.label ?? (oemSupported ? '无此键' : '不支持') }}</span></button></div><div class="oem-actions" data-record-control><template v-if="oemSupported"><template v-if="oemSpecialKeys.length"><button v-for="(key, index) in oemSpecialKeys" :key="key.keyId" type="button" class="oem-action" :class="{ active: activeOemInput?.code === key.keyId }" :disabled="busy || recording" :title="key.label" @click="bindOemKey(key.keyId)">{{ oemButtonLabel(index) }}</button></template><button v-else type="button" class="oem-action oem-unsupported" disabled>无专属键</button></template><button v-else type="button" class="oem-action oem-unsupported" disabled>不支持</button><button class="oem-clear" type="button" :disabled="busy || recording || !activeOemInput" @click="clearOemInputs">清空专用</button></div><p class="field-hint">{{ oemSupported ? '点选背部卡牌或专属按钮绑定本规则；清空专用会一并清除背部按键与专用键。' : '本机机型库无专用键适配。' }}</p></template><template v-else><div class="recorded-inputs" :class="{ recording }"><button v-for="slot in CONTROLLER_SHORTCUT_MAX_INPUTS" :key="slot" type="button" class="input-slot" :class="{ filled: Boolean(visibleRecordedInputs[slot - 1]) }" disabled><span class="input-slot-number">槽位 {{ slot }}</span><span class="input-slot-value">{{ visibleRecordedInputs[slot - 1] ? formatControllerShortcutInputs([visibleRecordedInputs[slot - 1]]) : '未设置' }}</span></button></div><div class="record-actions" data-record-control><button class="record-action" type="button" :disabled="busy || recordingBusy" @click="recording ? cancelRecording() : void startRecording()"><InlineIcon :name="recording ? 'warning' : 'edit'" />{{ recording ? '取消录制' : '开始录制' }}</button><button class="clear-action" type="button" :disabled="busy || recording || !activeRule.inputs.length" @click="updateRule({ inputs: [] })">清空</button></div><p class="field-hint">{{ recordingStatus }}</p></template></div>
              <div v-if="activeAction?.parameters?.length" class="editor-bubble"><span class="field-label">动作参数</span><p v-if="activeAction.description" class="field-hint">{{ activeAction.description }}</p><div class="parameter-grid" :class="{ 'keyboard-key-grid': keyGridParameter }" :style="keyGridParameter ? { '--key-grid-columns': keyGridParameter.gridColumns || 6 } : undefined"><template v-for="parameter in activeAction.parameters" :key="parameter.id"><template v-if="parameter.presentation === 'key-grid'"><button v-for="option in parameter.options || []" :key="option.value" class="keyboard-key-action" :class="{ active: String(activeRule.params[parameter.id] ?? parameter.default) === option.value }" type="button" :disabled="busy || recording" @click="updateParameter(parameter.id, option.value)">{{ option.label }}</button></template><label v-else-if="parameter.type !== 'boolean'"><span>{{ parameter.label }}</span><Dropdown v-if="parameter.type === 'select'" :model-value="String(activeRule.params[parameter.id] ?? parameter.default)" :options="parameter.options || []" color="accent" :disabled="busy || recording" @update:model-value="updateParameter(parameter.id, $event)" /><input v-else type="number" :min="parameter.min" :max="parameter.max" :step="parameter.step || 1" :value="Number(activeRule.params[parameter.id] ?? parameter.default)" :disabled="busy || recording" @change="updateParameter(parameter.id, Number(($event.target as HTMLInputElement).value))"></label><label v-else class="parameter-toggle-field"><span>{{ parameter.label }}</span><button class="parameter-toggle-action" :class="{ active: Boolean(activeRule.params[parameter.id] ?? parameter.default) }" type="button" :disabled="busy || recording" @click="updateParameter(parameter.id, !Boolean(activeRule.params[parameter.id] ?? parameter.default))">{{ parameter.label }}{{ Boolean(activeRule.params[parameter.id] ?? parameter.default) ? '已开启' : '已关闭' }}</button></label></template></div></div>
              <div class="editor-actions"><button class="outline-action" type="button" :disabled="busy || recording" @click="restoreNativeDefaults">恢复默认快捷键</button><button class="outline-action" type="button" :disabled="busy || recording" @click="cancel">取消并关闭</button><button class="apply-action" type="button" :disabled="busy || recording || !dirty" @click="apply">{{ busy ? '保存中…' : '应用规则' }}</button></div>
            </section>
            <section v-else class="card empty-editor"><InlineIcon name="keyboard" /><h3>选择或新建一条规则</h3><p>规则在此弹窗中录制，完成后才写入配置。</p><button class="small-action" type="button" :disabled="busy || recording" @click="addRule">+ 新建规则</button></section>
            <section class="status-bubble" :class="{ warning: validation.length }"><InlineIcon :name="validation.length ? 'warning' : 'check'" /><span>{{ validation.length ? validation[0] : status }}</span></section>
          </section>
        </main>
      </section>
      </div>
    </div>
  </Teleport>
</template>

<style scoped>
.editor-overlay{position:fixed;z-index:1100;inset:0;display:grid;place-items:center;padding:16px;background:rgba(2,7,13,.72);backdrop-filter:blur(8px)}.editor-dialog{width:min(1280px,calc(100vw - 32px));height:min(720px,calc(100dvh - 32px));box-sizing:border-box;overflow:auto;padding:14px;border:1px solid rgba(255,255,255,.11);border-radius:18px;background:linear-gradient(145deg,color-mix(in srgb,var(--bg-card) 97%,#152334),var(--bg-panel));box-shadow:0 28px 80px rgba(0,0,0,.58);color:var(--text);outline:none}.editor-top{display:grid;grid-template-columns:auto minmax(0,1fr) auto;align-items:center;gap:12px;margin-bottom:12px}.editor-top h3,.card h3{margin:1px 0 0;font-size:15px}.editor-top h3 .inline-icon{color:var(--accent)}.back,.small-action,.outline-action,.apply-action,.remove-action,.record-action,.clear-action{min-height:34px;padding:0 10px;border-radius:var(--radius-ctrl);font-size:11px;font-weight:800;cursor:pointer}.back,.outline-action,.clear-action{border:1px solid color-mix(in srgb,var(--accent) 52%,transparent);background:transparent;color:var(--accent)}.small-action,.apply-action,.record-action{border:1px solid var(--accent);background:var(--accent);color:#06121d}.remove-action{border:1px solid color-mix(in srgb,var(--danger) 48%,transparent);background:color-mix(in srgb,var(--danger) 9%,transparent);color:#ff9ea1}.back:disabled,.small-action:disabled,.outline-action:disabled,.apply-action:disabled,.remove-action:disabled,.record-action:disabled,.clear-action:disabled{opacity:.46;cursor:default}.eyebrow{color:var(--accent);font-size:10px;font-weight:800;letter-spacing:.08em}.state-chip{display:inline-flex;align-items:center;min-height:24px;padding:0 8px;border:1px solid color-mix(in srgb,var(--accent) 45%,transparent);border-radius:999px;background:color-mix(in srgb,var(--accent) 10%,transparent);color:var(--accent);font-size:10px;font-weight:800}.state-chip.dirty{border-color:color-mix(in srgb,#f5b93d 48%,transparent);background:color-mix(in srgb,#f5b93d 10%,transparent);color:#ffd47a}.editor-layout{display:grid;grid-template-columns:minmax(190px,.72fr) minmax(215px,.82fr) minmax(380px,1.32fr);gap:10px;align-items:start}.preview-column,.rules-column,.edit-column{display:grid;gap:10px;min-width:0}.card{padding:11px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-card);background:color-mix(in srgb,var(--bg-card) 93%,transparent)}.card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:8px}.card-head>span{color:var(--text-dim);font-size:10px;font-weight:700}.mirror-card :deep(.gamepad-visualizer){margin-top:8px;transform:scale(.82);transform-origin:top center;margin-bottom:-26px}.edit-head-preview{margin-left:9px;color:var(--accent);font-size:11px;font-weight:800;vertical-align:middle;white-space:nowrap}.route-note,.field-hint{margin:7px 0 0;color:var(--text-dim);font-size:10px;line-height:1.45}.route-note{display:flex;gap:5px}.route-note .inline-icon{flex:0 0 auto;color:#f5b93d}.rule-tools{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:9px 0}.rule-tools small{color:var(--text-dim);font-size:10px}.rule-list{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;max-height:510px;overflow:auto;padding-right:2px}.rule-row{min-height:50px;display:flex;align-items:center;justify-content:space-between;gap:8px;padding:8px;border:1px solid rgba(255,255,255,.09);border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text);text-align:left;cursor:pointer}.rule-row.active{border-color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,var(--bg-input));box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 36%,transparent)}.rule-row.disabled{opacity:.55}.rule-row span{display:grid;gap:2px;min-width:0}.rule-row b{font-size:11px}.rule-row small{overflow:hidden;color:var(--text-dim);font-size:10px;text-overflow:ellipsis;white-space:nowrap}.rule-row i{padding:3px 6px;border-radius:999px;background:rgba(255,255,255,.09);color:var(--text-dim);font-size:9px;font-style:normal;font-weight:800}.rule-editor-card{display:grid;gap:8px}.editor-bubble,.status-bubble{padding:10px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent)}.field-label{display:flex;align-items:center;justify-content:space-between;margin-bottom:7px;color:var(--text-dim);font-size:11px;font-weight:800}.field-label small{font-size:10px}.recorder-bubble :deep(.seg-button){margin-bottom:8px}.recorded-inputs{display:flex;align-items:center;flex-wrap:wrap;gap:5px;min-height:38px;padding:7px;border:1px dashed rgba(255,255,255,.15);border-radius:8px;background:var(--bg-panel);color:var(--text-dim);font-size:10px}.recorded-inputs.recording{border-color:var(--accent);box-shadow:inset 0 0 0 1px color-mix(in srgb,var(--accent) 28%,transparent);color:var(--accent)}.recorded-inputs kbd{padding:4px 6px;border:1px solid color-mix(in srgb,var(--accent) 46%,transparent);border-radius:6px;background:color-mix(in srgb,var(--accent) 13%,transparent);color:var(--text);font:700 10px/1 system-ui}.record-actions{display:grid;grid-template-columns:1fr auto;gap:7px;margin-top:7px}.record-action{display:flex;align-items:center;justify-content:center;gap:5px}.timing-row,.parameter-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:7px;margin-top:8px}.timing-row label,.parameter-grid label{position:relative;display:grid;gap:4px;color:var(--text-dim);font-size:10px;font-weight:700}.timing-row input,.parameter-grid input{min-width:0;height:30px;padding:0 25px 0 8px;border:1px solid rgba(255,255,255,.12);border-radius:7px;background:var(--bg-panel);color:var(--text);font-size:11px}.timing-row i{position:absolute;right:7px;bottom:8px;color:var(--text-dim);font-size:9px;font-style:normal}.parameter-grid :deep(.toggle-row){min-height:30px;padding:0}.parameter-grid :deep(.toggle-label){font-size:10px}.editor-actions{display:grid;grid-template-columns:auto 1fr auto;gap:7px}.status-bubble{display:flex;align-items:flex-start;gap:7px;color:var(--text-dim);font-size:10px;line-height:1.45}.status-bubble .inline-icon{flex:0 0 auto;color:var(--accent)}.status-bubble.warning{border-color:color-mix(in srgb,#f5b93d 45%,transparent);color:#ffd47a}.status-bubble.warning .inline-icon{color:#f5b93d}.empty,.empty-editor{display:grid;gap:8px;place-items:center;min-height:150px;text-align:center}.empty p,.empty-editor p{margin:0;color:var(--text-dim);font-size:11px}.empty-editor .inline-icon{color:var(--accent)}@media(max-width:980px){.editor-dialog{height:auto;max-height:calc(100dvh - 16px);width:min(840px,calc(100vw - 16px));padding:10px}.editor-layout{grid-template-columns:minmax(220px,.75fr) minmax(350px,1.25fr)}.rules-column{grid-column:1/-1}.rule-list{grid-template-columns:repeat(2,minmax(0,1fr));max-height:250px}.mirror-card :deep(.gamepad-visualizer){transform:scale(.74);margin-bottom:-34px}}@media(max-width:650px){.editor-overlay{padding:7px}.editor-dialog{width:calc(100vw - 14px);max-height:calc(100dvh - 14px);padding:8px}.editor-layout{grid-template-columns:1fr}.rules-column{grid-column:auto}.rule-list{grid-template-columns:repeat(2,minmax(0,1fr));max-height:none}.preview-column{display:none}.editor-top{grid-template-columns:auto 1fr}.editor-top .state-chip{display:none}.timing-row,.parameter-grid{grid-template-columns:1fr 1fr}.editor-actions{grid-template-columns:auto 1fr auto}}@media(max-width:420px){.timing-row,.parameter-grid{grid-template-columns:1fr}.editor-actions{grid-template-columns:1fr 1fr}.editor-actions span{display:none}}
/*
 * Keep the navigator and input mirror together in the first row.  The
 * selected-rule editor is the primary workspace, so it spans the full row
 * underneath them instead of being squeezed into a third column.
 */
.editor-overlay{padding:0;overflow:hidden}
.editor-stage{width:100vw;height:100vh;display:grid;place-items:center;overflow:hidden;transform:none}
.editor-dialog{width:calc(100vw - 24px);height:calc(100vh - 24px);max-height:none;box-sizing:border-box;overflow:auto;padding:14px}
.editor-layout{grid-template-columns:repeat(2,minmax(0,1fr));align-items:stretch}
.edit-column{grid-column:1/-1}
.preview-column,.rules-column{align-self:stretch}
.preview-column>.card,.rules-column>.card{height:100%;box-sizing:border-box}

/* Keep the rule controls compact and lock the left/right row relationship. */
.rule-editor-card{width:100%;box-sizing:border-box;margin:0;grid-template-columns:minmax(0,.8fr) minmax(0,1fr);grid-template-rows:auto minmax(0,auto) minmax(0,auto);justify-content:stretch;align-items:stretch}
.rule-editor-card>.card-head,.rule-editor-card>.editor-actions,.rule-editor-card>.editor-bubble:not(.action-trigger-bubble):not(.recorder-bubble){grid-column:1/-1}
.action-trigger-bubble,.action-trigger-grid{display:contents}
.action-trigger-grid>div{padding:10px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent)}
.action-trigger-grid>div:first-child{grid-column:1;grid-row:2}
.action-trigger-grid>div:nth-child(2){grid-column:1;grid-row:3}
.recorder-bubble{display:grid;grid-column:2;grid-row:2 / span 2;grid-template-rows:repeat(5,auto);align-content:space-between;gap:10px;align-self:stretch;width:100%;min-width:0;box-sizing:border-box}
.recorder-bubble>.field-label,.recorder-bubble>.field-hint{margin:0}
.recorder-bubble .recorded-inputs{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;min-height:0;padding:0;border:0;background:transparent}
.recorder-bubble :deep(.seg){gap:7px;margin:0;padding:0;background:transparent}
.recorder-bubble :deep(.seg-btn){min-height:34px;border:1px solid color-mix(in srgb,var(--accent) 52%,transparent);border-radius:var(--radius-ctrl);background:transparent;color:var(--accent)}
.recorder-bubble :deep(.seg-btn.active){border-color:var(--accent);background:var(--accent);color:#06121d}
.recorder-bubble .input-slot{display:grid;gap:4px;min-width:0;min-height:54px;padding:7px 5px;border:1px dashed rgba(255,255,255,.2);border-radius:8px;background:var(--bg-panel);color:var(--text-dim);text-align:center;cursor:default;opacity:1}
.recorder-bubble .input-slot.filled{border-color:color-mix(in srgb,var(--accent) 52%,transparent);background:color-mix(in srgb,var(--accent) 13%,var(--bg-panel));color:var(--text)}
.recorder-bubble .record-actions{margin-top:0}
.input-slot-number{font-size:9px;font-weight:800;letter-spacing:.04em}
.input-slot-value{overflow:hidden;color:inherit;font-size:11px;font-weight:800;line-height:1.2;text-overflow:ellipsis;white-space:nowrap}
/* ── 背部/专用键模式：4 张背部卡牌（可横向延展）+ 专属按钮排 ── */
.recorder-bubble .oem-back-slots{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;min-height:0;padding:0;border:0;background:transparent}
.oem-back-slot{display:grid;gap:3px;min-width:0;min-height:54px;padding:7px 5px;border:1px dashed rgba(255,255,255,.2);border-radius:8px;background:var(--bg-panel);color:var(--text-dim);text-align:center;cursor:pointer;opacity:1}
.oem-back-slot:disabled{cursor:default;opacity:.46}
.oem-back-slot:not(:disabled):hover{border-color:color-mix(in srgb,var(--accent) 52%,transparent)}
.oem-back-slot.active{border-color:var(--accent);background:color-mix(in srgb,var(--accent) 13%,var(--bg-panel));color:var(--text);box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 36%,transparent)}
.oem-back-slot.unsupported:disabled{opacity:.28}
.oem-back-slot-index{font-size:9px;font-weight:800;letter-spacing:.04em}
.oem-back-slot-name{font-size:11px;font-weight:800;color:inherit}
.oem-back-slot-key{overflow:hidden;color:inherit;font-size:9px;text-overflow:ellipsis;white-space:nowrap;opacity:.75}
.oem-actions{display:flex;gap:7px;margin-top:0}
.oem-actions>*{flex:1 1 0;min-width:0}
.oem-action{min-height:34px;padding:0 6px;border:1px solid color-mix(in srgb,var(--accent) 52%,transparent);border-radius:var(--radius-ctrl);background:transparent;color:var(--accent);font-size:11px;font-weight:800;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer}
.oem-action.active{border-color:var(--accent);background:var(--accent);color:#06121d;box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 35%,transparent)}
.oem-action:disabled{opacity:.46;cursor:default}
.oem-action.oem-unsupported{color:var(--text-dim)}
.oem-clear{min-height:34px;padding:0 6px;border:1px solid rgba(255,255,255,.16);border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text-dim);font-size:11px;font-weight:800;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer}
.oem-clear:disabled{opacity:.46;cursor:default}
.parameter-grid{grid-template-columns:repeat(3,minmax(0,1fr));grid-auto-rows:51px;align-items:stretch}
.parameter-grid>label{display:grid;grid-template-rows:14px 33px;align-content:start;align-items:stretch;gap:4px;min-width:0;height:51px;min-height:51px}
.parameter-grid>label>span{display:block;height:14px;overflow:hidden;line-height:14px;text-overflow:ellipsis;white-space:nowrap}
.parameter-grid :deep(.dd){width:100%;height:33px;min-height:33px}
.parameter-grid :deep(.dd-trigger){height:33px;min-height:33px;box-sizing:border-box;padding:0 8px;font-size:11px;line-height:1}
.parameter-grid :deep(.dd-value){min-height:0;line-height:1}
.parameter-toggle-field{color:var(--text-dim);font-size:10px;font-weight:700}
.parameter-toggle-action{width:100%;height:33px;min-height:33px;box-sizing:border-box;padding:0 6px;border:1px solid color-mix(in srgb,var(--accent) 52%,transparent);border-radius:var(--radius-ctrl);background:transparent;color:var(--accent);font-size:10px;font-weight:800;line-height:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer}
.parameter-toggle-action.active{border-color:var(--accent);background:var(--accent);color:#06121d}
.parameter-toggle-action:disabled{opacity:.46;cursor:default}
.parameter-grid.keyboard-key-grid{grid-template-columns:repeat(var(--key-grid-columns),minmax(0,1fr));grid-auto-rows:33px}
.keyboard-key-action{min-width:0;height:33px;min-height:33px;box-sizing:border-box;padding:0 4px;border:1px solid color-mix(in srgb,var(--accent) 52%,transparent);border-radius:var(--radius-ctrl);background:transparent;color:var(--accent);font-size:10px;font-weight:800;line-height:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer}
.keyboard-key-action.active{border-color:var(--accent);background:var(--accent);color:#06121d;box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 35%,transparent)}
.keyboard-key-action:disabled{opacity:.46;cursor:default}
.edit-head-actions{display:flex;align-items:center;gap:7px}
.rule-enabled-action{min-height:34px;padding:0 10px;border:1px solid rgba(255,255,255,.16);border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text-dim);font-size:11px;font-weight:800;cursor:pointer}
.rule-enabled-action.active{border-color:#5bb5ff;background:#2495ef;color:#eef8ff;box-shadow:0 0 0 1px rgba(91,181,255,.3)}
.rule-enabled-action:disabled{opacity:.46;cursor:default}
.editor-actions{grid-template-columns:repeat(3,minmax(0,1fr))}
.editor-actions>*{min-width:0;padding:0 6px;white-space:nowrap}

/* 实心蓝底按钮的手柄焦点白边（2026-09-28 用户裁决）：与「手柄录制」选中表现
   一致——手柄移动到按钮上时，除原有蓝色焦点环外增加白色内描边。
   .focused 由手柄引擎显式切换（tokens.css 只提供基础蓝环）。 */
.small-action.focused,.apply-action.focused,.record-action.focused,.rule-enabled-action.active.focused{box-shadow:inset 0 0 0 2px #fff,var(--focus-ring)}

@media(max-width:980px){
  .editor-layout{grid-template-columns:repeat(2,minmax(0,1fr))}
  .preview-column,.rules-column{grid-column:auto}
  .edit-column{grid-column:1/-1}
  .rule-list{grid-template-columns:repeat(2,minmax(0,1fr));max-height:250px}
  .mirror-card :deep(.gamepad-visualizer){transform:scale(.74);margin-bottom:-34px}
}
@media(max-width:650px){
  .editor-layout{grid-template-columns:repeat(2,minmax(0,1fr));align-items:stretch}
  .preview-column{display:grid}
  .preview-column,.rules-column{grid-column:auto}
  .edit-column{grid-column:1/-1}
  .rule-list{grid-template-columns:repeat(2,minmax(0,1fr));max-height:250px}
  .editor-top{grid-template-columns:auto 1fr}
  .editor-top .state-chip{display:none}
  .timing-row{grid-template-columns:1fr 1fr}
  .editor-actions{grid-template-columns:repeat(3,minmax(0,1fr))}
}
@media(max-width:420px){
  .timing-row{grid-template-columns:1fr}
  .editor-actions{grid-template-columns:repeat(3,minmax(0,1fr))}
  .editor-actions span{display:none}
}

/* Reflow only cramped controls; keep the established desktop sizes and fonts.
   Container queries use the actual card width after Windows DPI / host zoom,
   not the physical monitor resolution. The dialog remains vertically scrollable. */
.rule-list-card{container-type:inline-size;container-name:shortcut-list}
.rule-editor-card{container-type:inline-size;container-name:shortcut-editor}
.action-trigger-grid>div{min-width:0;container-type:inline-size;container-name:shortcut-field}
.recorder-bubble{container-type:inline-size;container-name:shortcut-recorder}

@container shortcut-list (max-width:300px){
  .rule-row{display:grid;grid-template-columns:minmax(0,1fr);gap:4px}
  .rule-row b{overflow-wrap:anywhere}
  .rule-row small{white-space:normal;overflow:visible;overflow-wrap:anywhere;text-overflow:clip}
  .rule-row i{justify-self:end;white-space:nowrap}
}
@container shortcut-field (max-width:235px){
  .action-trigger-grid :deep(.seg.full){display:grid;grid-template-columns:repeat(3,minmax(0,1fr))}
  .timing-row{grid-template-columns:repeat(auto-fit,minmax(min(100%,120px),1fr))}
  .action-trigger-grid :deep(.dd-value){white-space:normal;overflow:visible}
  .action-trigger-grid :deep(.dd-selected-label){flex:1 1 auto;min-width:0;white-space:normal;overflow-wrap:anywhere;text-align:left}
}
@container shortcut-recorder (max-width:250px){
  .recorder-bubble :deep(.seg.full){flex-wrap:wrap}
  .recorder-bubble :deep(.seg.full .seg-btn){flex-basis:80px}
  .field-label{flex-wrap:wrap;gap:4px 8px}
  .input-slot-value,.oem-back-slot-key{white-space:normal;overflow-wrap:anywhere;overflow:visible;text-overflow:clip}
}
@container shortcut-editor (max-width:600px){
  .card-head{flex-wrap:wrap}
  .edit-head-actions{flex-wrap:wrap}
  .edit-head-actions>button{white-space:nowrap}
  .edit-head-preview{display:inline-block;max-width:100%;white-space:normal;overflow-wrap:anywhere}
}
@container shortcut-editor (max-width:400px){
  .parameter-grid:not(.keyboard-key-grid){grid-template-columns:repeat(2,minmax(0,1fr))}
  .parameter-grid.keyboard-key-grid{grid-template-columns:repeat(auto-fit,minmax(44px,1fr))}
  .editor-actions{grid-template-columns:repeat(auto-fit,minmax(110px,1fr))}
}
@media(max-width:520px){
  .editor-layout{grid-template-columns:minmax(0,1fr)}
  .rule-editor-card{grid-template-columns:minmax(0,1fr);grid-template-rows:none}
  .action-trigger-grid>div:first-child,.action-trigger-grid>div:nth-child(2),.recorder-bubble{grid-column:1;grid-row:auto}
  .mirror-card :deep(.gamepad-visualizer){transform:scale(.74);margin-bottom:-34px}
}
</style>
