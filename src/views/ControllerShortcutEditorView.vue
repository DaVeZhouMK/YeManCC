<script setup lang="ts">
import { useVirtualTargetPolicy, currentVirtualTargetPolicy } from '@/bridge/virtualTargetPolicyState';
import { virtualTargetOptions, virtualTargetSelectionError, requireVirtualTargetAllowed, STEAM_DECK_SELF_OUTPUT_MESSAGE } from '@/bridge/virtualTargetPolicy';
import { virtualTargetShortcutError } from '@/bridge/virtualTargetPolicy';
import { computed, nextTick, onActivated, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import Dropdown from '@/components/Dropdown.vue';
import { preserveControllerShortcutRecords } from '@/bridge/controllerShortcutStorage';
import GamepadVisualizer from '@/components/GamepadVisualizer.vue';
import { useShortcutPreviewFocus } from '@/composables/useShortcutPreviewFocus';
import InlineIcon from '@/components/InlineIcon.vue';
import SegButton from '@/components/SegButton.vue';
import ControllerActionControls from '@/components/ControllerActionControls.vue';
import ControllerEditorGlobalActions from '@/components/ControllerEditorGlobalActions.vue';
import ControllerComboInput from '@/components/ControllerComboInput.vue';
import ControllerOutputAction from '@/components/ControllerOutputAction.vue';
import { useKeyboardPressFeedback } from '@/composables/useKeyboardPressFeedback';
import ControllerKeyboardPreview from '@/components/ControllerKeyboardPreview.vue';
import { on as onIpc, isNativeRuntime } from '@/bridge/ipc';
import { windowApi } from '@/bridge/api';
import { oemKeysGet, setShortcutRecording, summonGet, summonSet, type GamepadSettings } from '@/bridge/yeman';
import { compareAndSwapInputSettings, loadSettings, type InputSettingsSnapshot } from '@/bridge/settingsRepository';
import { focusGamepadElement } from '@/gamepad/focus';
import { formatControllerShortcutDisplayInputs } from '@/bridge/controllerOemPresentation';
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
  newControllerShortcutRule,
  nativeDefaultRule,
  reconcileControllerShortcutRules,
  readControllerShortcutRules,
  mergeControllerShortcutDraft,
  shortcutRecordMode,
  shortcutPreviewKind,
  isHandheldExtendedKey,
  keyboardCodePattern,
  validateControllerShortcutRules,
  type ControllerButtonCode,
  type ControllerShortcutInput,
  type ControllerShortcutRecordMode,
  type ControllerShortcutRule,
  type OemKeysSnapshot,
} from '@/bridge/controllerShortcutRules';

export interface ControllerShortcutPresetDraft { name:string; rules:ControllerShortcutRule[]; apps?:Array<{name:string;path:string}>; }
const props = withDefaults(defineProps<{ presetDraft?: ControllerShortcutPresetDraft; initialActionId?: string; persona?: string; teleportTo?: string | HTMLElement; layoutVariant?: 'dock' | 'inline' | 'popover'; globalActionsLayout?: 'top' | 'bottom' | 'split'; inputFlowVariant?: 'quick' | 'confirm' | 'picker'; outputActionVariant?: 'tiles' | 'steps' | 'library' }>(), { persona: '', teleportTo: 'body', layoutVariant: 'popover', globalActionsLayout: 'top', inputFlowVariant: 'quick', outputActionVariant: 'tiles' });
const emit = defineEmits<{ (event: 'close'): void; (event:'preset-apply',rules:ControllerShortcutRule[]):void }>();
const dialogEl = ref<HTMLElement | null>(null);
const snapshot = ref<InputSettingsSnapshot | null>(null);
const selectedDiagramPersona = ref('');
const quickAppChoices = ref<Array<{value:string;label:string}>>([]);
const diagramPersonaOptions = [{value:'steamdeck',label:'Steam Deck'},{value:'dualsense-edge',label:'PS5'},{value:'elite',label:'Xbox'}];
watch(()=>props.persona,()=>{selectedDiagramPersona.value=''});
const keyboardLiveCodes=useKeyboardPressFeedback(dialogEl);
const diagramPersona = computed(() => selectedDiagramPersona.value || props.persona || (snapshot.value?.outputTarget?.buttonMappingEnabled ? snapshot.value.outputTarget.persona : 'xbox'));
const diagramTitle = computed(() => diagramPersona.value === 'steamdeck' ? 'Steam Deck' : ['ps5', 'dualsense-edge'].includes(diagramPersona.value) ? 'PS5' : 'Xbox');
const gamepad = ref<GamepadSettings>({ enabled: true, bDoubleMinimize: true, startDoubleF7: false, tdpShortcut: true, fpsShortcut: true, killGame: true, openKeyboard: true, returnDesktop: true, mouseToggle: true, mouseBackend: 'joyxoff' });
const rules = ref<ControllerShortcutRule[]>([]);
const activeId = ref('');
const previewFocus = useShortcutPreviewFocus();
const managementId = ref('');
const outputPickerOpen = ref(false);
const newDraft = ref<ControllerShortcutRule|null>(null);
const newStep = ref<'input'|'trigger'|'output'|'finish'>('input');
const newOutputChosen = ref(false);
const creationProblem = ref('');
const creating = computed(()=>Boolean(newDraft.value && activeId.value===newDraft.value.id));
const creationSteps = [{id:'input',label:'录制输入'},{id:'trigger',label:'设置触发'},{id:'output',label:'选择动作'},{id:'finish',label:'完成新建'}] as const;
const creationHelp = computed(()=>({input:'点击“录制输入”，按下你要用的组合，再全部松开。',trigger:'选择按下、长按或双击，决定什么时候执行动作。',output:'点击“点击更换动作”，选择这个组合要执行的功能。',finish:'核对输入和输出，点击“完成新建”加入动作列表。'})[newStep.value]);
const dirty = ref(false);
const presetCloseConfirm = ref(false);
const presetCloseCancel = ref<HTMLButtonElement | null>(null);
const presetCloseDiscard = ref<HTMLButtonElement | null>(null);
let presetCloseFocus: HTMLElement | null = null;
const busy = ref(false);
const status = ref('读取规则配置…');
const recordMode = ref<ControllerShortcutRecordMode>('controller');
const recording = ref(false);
const recordingBusy = ref(false);
const recordingStatus = ref('选择录制模式后开始录入组合输入。');
// Keep an in-progress capture separate from the saved rule. Cancelling a
// recording must leave the previous shortcut untouched.
const recordingInputs = ref<ControllerShortcutInput[]>([]);
const pendingCombination = ref<ControllerShortcutInput[] | null>(null);
watch(() => props.inputFlowVariant, () => { pendingCombination.value = null; cancelRecording(); });
const widthOwner = `controller-editor-${Date.now()}-${Math.random().toString(36).slice(2)}`;
const widthSession = ref(false);
let editorDisposed = false;
const ownsMainWindow = isNativeRuntime && !/[?&]__ymccChild=\d+/.test(window.location.hash);
let editorWidthRequest: Promise<void> | null = null;
function setEditorWindowWidth(): Promise<void> {
  if (!ownsMainWindow || editorDisposed) return Promise.resolve();
  if (editorWidthRequest) return editorWidthRequest;
  const task = (async () => {
    try {
      const receipt = await windowApi.controllerEditorWidth(widthOwner, true, 2);
      if (editorDisposed) {
        if (receipt.ok) await windowApi.controllerEditorWidth(widthOwner, false);
        return;
      }
      widthSession.value = receipt.ok;
      if (!receipt.ok) status.value = '当前窗口未能加宽，请检查窗口状态。';
    } catch { if (!editorDisposed) status.value = '窗口加宽失败，仍可继续编辑。'; }
  })();
  editorWidthRequest = task;
  void task.finally(() => { if (editorWidthRequest === task) editorWidthRequest = null; });
  return task;
}
async function releaseEditorWindowWidth(): Promise<void> {
  await editorWidthRequest;
  if (!widthSession.value) return;
  widthSession.value = false;
  await windowApi.controllerEditorWidth(widthOwner, false).catch(() => undefined);
}
defineExpose({ releaseEditorWindowWidth });
const allActionOptions = CONTROLLER_SHORTCUT_ACTIONS.filter((action) => action.available).map((action) => ({ value: action.id, label: `${action.category} · ${action.label}` }));
const holdTimeOptions = [100, 200, 300, 400, 500, 750, 1000, 1500, 2000, 3000, 5000].map((value) => ({ value, label: `${value} ms` }));
const repeatIntervalOptions = [40, 60, 80, 100, 150, 200, 250, 300, 500, 750, 1000].map((value) => ({ value, label: `${value} ms` }));
const visibleRules = computed(() => [...rules.value.filter((rule) => isControllerShortcutActionVisible(rule.actionId)), ...(newDraft.value ? [newDraft.value] : [])]);
const RULES_PER_PAGE = 10;
const rulePage = ref(0);
const rulePageCount = computed(() => Math.max(1, Math.ceil(visibleRules.value.length / RULES_PER_PAGE)));
const safeRulePage = computed(() => Math.min(rulePage.value, rulePageCount.value - 1));
const pagedRules = computed(() => visibleRules.value.slice(safeRulePage.value * RULES_PER_PAGE, (safeRulePage.value + 1) * RULES_PER_PAGE));
function turnRulePage(delta: number): void {
  if (recording.value || recordingBusy.value || busy.value) return;
  rulePage.value = Math.max(0, Math.min(rulePageCount.value - 1, safeRulePage.value + delta));
  managementId.value = '';
  previewFocus.reset();
}
const activeRule = computed(() => visibleRules.value.find((rule) => rule.id === activeId.value) || null);
// Hover/focus previews a rule without changing the rule being edited.
const previewRule = computed(() => {
  if (recording.value || recordingBusy.value || newDraft.value || pendingCombination.value || managementId.value || outputPickerOpen.value) return activeRule.value;
  return pagedRules.value.find(rule => rule.id === previewFocus.activeId.value) || activeRule.value;
});
const virtualTargetPolicy = useVirtualTargetPolicy();
const activeAction = computed(() => {
  const action = activeRule.value ? actionById(activeRule.value.actionId) : undefined;
  if (action?.id === 'app.launchQuickApp') return { ...action, parameters: action.parameters?.map(parameter => parameter.id === 'appPath' ? { ...parameter, options: quickAppChoices.value } : parameter) };
  if (action?.id !== 'input.virtualGamepadToggle') return action;
  return { ...action, description: action.description + (virtualTargetPolicy.value?.steamDeckSelfOutputBlocked ? ' ' + STEAM_DECK_SELF_OUTPUT_MESSAGE : ''),
    parameters: action.parameters?.map(parameter => parameter.id === 'preset'
      ? { ...parameter, options: virtualTargetOptions(parameter.options || [], virtualTargetPolicy.value) } : parameter) };
});
const keyGridParameter = computed(() => activeAction.value?.parameters?.find((parameter) => parameter.presentation === 'key-grid'));
const validation = computed(() => validateControllerShortcutRules(rulesToValidate(rules.value), oemKeys.value));
const inputIssue = computed(()=>creationProblem.value || validation.value[0] || '');
const notice = computed(()=>/失败|已保存|已取消全部|无法|未能|已加入|草稿已保留/.test(status.value)?status.value:'');

// ── 掌机专用键（背部/专用）输入：机型库清单由 native oem.keys.get 提供 ──
const oemKeys = ref<OemKeysSnapshot>(OEM_EMPTY_SNAPSHOT);
const oemSupported = computed(() => oemKeys.value.supported);
const oemCaptureUnavailableHint = '此按键的输入暂不可用';
function hasUnavailableOemInput(inputs: readonly ControllerShortcutInput[]): boolean {
  return inputs.some((input) => input.source === 'oem' && oemKeys.value.keys.some((key) => key.keyId === input.code && key.captureImplemented === false));
}
function rulesToValidate(entries: ControllerShortcutRule[]): ControllerShortcutRule[] {
  return entries.filter((rule) => isControllerShortcutActionVisible(rule.actionId) || hasUnavailableOemInput(rule.inputs));
}
const activeOemInput = computed(() => activeRule.value?.inputs.find((input) => input.source === 'oem') ?? null);
const previewInputs = computed(() => recording.value ? recordingInputs.value : previewRule.value?.inputs || []);
const previewKind = computed(() => shortcutPreviewKind(previewInputs.value));
const previewKeyboardKeys = computed(() => previewInputs.value.filter(input => input.source === 'keyboard' || input.source === 'mouse'));
const highlightedOemKeys = computed(() => previewInputs.value.filter(input => input.source === 'oem').map(input => input.code));
const activeOemKey = computed(() => {
  const input = activeOemInput.value;
  if (!input) return undefined;
  return oemKeys.value.keys.find((key) => key.keyId === input.code);
});
/** WMI 一次性事件（MSI CLAW/QS）仅支持 press/double；hold/release/repeat 禁用 */
const activeOemClickOnly = computed(() => activeRule.value?.inputs.some(input => input.source === 'oem' && oemKeys.value.keys.some(key => key.keyId === input.code && key.triggerCapability === 'click-only')) || false);
const oemTriggerOptions = computed(() => {
  if (!activeOemClickOnly.value) return CONTROLLER_TRIGGER_OPTIONS;
  return CONTROLLER_TRIGGER_OPTIONS.filter((option) => option.value === 'press' || option.value === 'double');
});
function bindOemKey(keyId: string): void {
  if (busy.value || recording.value || recordingBusy.value) return;
  const key = oemKeys.value.keys.find((candidate) => candidate.keyId === keyId);
  if (key?.captureImplemented === false) { status.value = oemCaptureUnavailableHint; return; }
  if (!oemSupported.value || !key) return;
  stageCombination([{ source: 'oem', code: keyId }]);
}
function commitCombination(inputs: ControllerShortcutInput[]): void {
  const clickOnly = inputs.some(input => input.source === 'oem' && oemKeys.value.keys.some(key => key.keyId === input.code && key.triggerCapability === 'click-only'));
  updateRule({ inputs, ...(clickOnly && activeRule.value?.trigger !== 'press' && activeRule.value?.trigger !== 'double' ? { trigger: 'press' as const } : {}) });
}
function stageCombination(inputs: ControllerShortcutInput[]): void {
  pendingCombination.value = null;
  commitCombination(inputs);
  recordMode.value = shortcutRecordMode(inputs);
  if (creating.value) {newStep.value='trigger';creationProblem.value=''}
}

function acceptCombination(): void {
  if (!pendingCombination.value || busy.value || recording.value || recordingBusy.value) return;
  if (hasUnavailableOemInput(pendingCombination.value)) { status.value = oemCaptureUnavailableHint; return; }
  commitCombination(pendingCombination.value);
  pendingCombination.value = null;
}
function toggleManualButton(code: string): void {
  if (!activeRule.value || busy.value || recording.value || recordingBusy.value || !controllerCodes.has(code as ControllerButtonCode)) return;
  const inputs = activeRule.value.inputs.filter(input => input.source === 'controller');
  const present = inputs.some(input => input.code === code);
  if (!present && inputs.length >= CONTROLLER_SHORTCUT_MAX_INPUTS) return;
  updateRule({ inputs: present ? inputs.filter(input => input.code !== code) : [...inputs, { source: 'controller', code }] });
}
function clearCombination(): void {
  if (busy.value || recording.value || recordingBusy.value) return;
  pendingCombination.value = null;
  if (recordMode.value === 'oem') clearOemInputs();
  else updateRule({ inputs: [] });
}
function clearOemInputs(): void {
  if (!activeRule.value) return;
  updateRule({ inputs: [] });
  status.value = '已清空专用键绑定。';
}
const actionOptions = computed(() => allActionOptions);
const highlightedButtons = computed(() => {
  const inputs = previewInputs.value;
  return inputs
    .filter((input) => input.source === 'controller')
    .map((input) => CONTROLLER_BUTTONS.find((item) => item.value === input.code)?.gamepadIndex)
    .filter((value): value is number => value !== undefined);
});
// The saved button-mapping section is a configuration draft.  It is not a
// Coordinator/InputHost runtime receipt, so legacy or unknown persisted
// `closure: CLOSED` fields must not be presented as an executed route.
const closure = computed(() => 'UNENCLOSED');
function formatInputs(inputs: readonly ControllerShortcutInput[]): string {
  return formatControllerShortcutDisplayInputs(inputs, diagramPersona.value, oemKeys.value);
}
const activeInputText = computed(() => {
  if (recording.value) return formatInputs(recordingInputs.value) || '等待录入组合输入';
  return previewRule.value ? formatInputs(previewInputs.value) || '尚未录入组合输入' : '未选择规则';
});

const controllerCodes = new Set(CONTROLLER_BUTTONS.map((item) => item.value));
const desktopHeld = new Set<string>();
const desktopRecorded = new Map<string, ControllerShortcutInput>();
let nativeHeld = false;
const nativeRecorded = new Map<string, ControllerShortcutInput>();
let desktopReleaseTimer: ReturnType<typeof window.setTimeout> | null = null;
let stopGamepadRecording: (() => void) | null = null;
let stopGamepadRecordingState: (() => void) | null = null;

function inputIdentity(input: ControllerShortcutInput): string {
  return `${input.source}:${input.code}`;
}

function uniqueInputs(inputs: readonly ControllerShortcutInput[]): ControllerShortcutInput[] {
  return inputs.filter((input, index, all) => all.findIndex((candidate) => inputIdentity(candidate) === inputIdentity(input)) === index);
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
  if (props.initialActionId && CONTROLLER_SHORTCUT_ACTIONS.some(action => action.id === props.initialActionId && action.available)) return props.initialActionId;
  const occupied = new Set(rules.value.filter((rule) => rule.enabled).map((rule) => rule.actionId));
  return allActionOptions.find((option) => actionById(option.value)?.available && !occupied.has(option.value))?.value
    ?? allActionOptions.find((option) => actionById(option.value)?.available)?.value
    ?? allActionOptions[0]?.value
    ?? 'window.summon';
}
function selectRule(id: string, manage = false): void {
  if (recording.value || recordingBusy.value || (manage && busy.value)) return;
  previewFocus.reset();
  const selected = visibleRules.value.find((rule) => rule.id === id);
  pendingCombination.value = null;
  activeId.value = selected?.id || '';
  managementId.value = manage && selected?.id !== newDraft.value?.id ? activeId.value : '';
  outputPickerOpen.value=false;creationProblem.value='';
  if (selected) rulePage.value = Math.floor(visibleRules.value.findIndex(rule => rule.id === selected.id) / RULES_PER_PAGE);
  if (selected) recordMode.value = shortcutRecordMode(selected.inputs);
}
function touch(): void { dirty.value = true; }
function updateRule(patch: Partial<ControllerShortcutRule>): void {
  const current = activeRule.value;
  if (!current) return;
  if (current.id === newDraft.value?.id) {newDraft.value={...current,...patch};return}
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
  const params = defaultParameters(actionId);
  if (actionId === 'input.virtualGamepadToggle' && virtualTargetSelectionError(params.preset, currentVirtualTargetPolicy())) {
    params.preset = 'dualsense-edge';
    status.value = '本机不可选择 SteamDeck，新快捷预设为 PS5；可改选 Xbox。';
  }
  updateRule({
    actionId,
    params,
    ...(actionId.startsWith('keyboard.') ? { trigger: 'press' as const } : {}),
  });
  if(creating.value){newOutputChosen.value=true;newStep.value='finish';creationProblem.value=''}
}
function outputLabel(rule: ControllerShortcutRule): string {
  const action = actionById(rule.actionId);
  if (rule.actionId === 'app.launchQuickApp') return '启动 · ' + (quickAppChoices.value.find(item=>item.value===rule.params.appPath)?.label || '选择快捷应用');
  if (rule.actionId === 'input.frontendButton') return action?.parameters?.[0].options?.find(option=>option.value===rule.params.button)?.label || action.label;
  if (rule.actionId.startsWith('keyboard.')) {
    const key = action?.parameters?.find(p => p.id === 'key')?.options?.find(o => o.value === rule.params.key)?.label || rule.params.key;
    return `键盘 · ${key || '未选按键'}`;
  }
  return action?.label || '未知动作';
}
function chooseOutputKey(actionId: string, code: string): void {
  if (activeRule.value?.origin === 'native-default') {
    if (activeRule.value.actionId.startsWith('keyboard.') && actionId.startsWith('keyboard.')) updateRule({ actionId, params: { ...defaultParameters(actionId), key: code } });
    return;
  }
  chooseAction(actionId);
  updateParameter('key', code);
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
  if (current.actionId === 'input.virtualGamepadToggle' && id === 'preset') {
    const blocked = virtualTargetSelectionError(value, currentVirtualTargetPolicy());
    if (blocked) { status.value = blocked; return; }
  }
  updateRule({ params: { ...current.params, [id]: value } });
}
function addRule(): void {
  if(newDraft.value){selectRule(newDraft.value.id);return}
  const next={...newControllerShortcutRule(`${Date.now()}-${rules.value.length}`,availableActionId()),inputs:[]};
  newDraft.value=next;newStep.value='input';newOutputChosen.value=false;creationProblem.value='';
  selectRule(next.id);
  void nextTick(()=>focusEditorControl());
}
function cancelNewAction(): void {
  if(recording.value||recordingBusy.value||busy.value)return;
  newDraft.value=null;newOutputChosen.value=false;creationProblem.value='';
  selectRule(visibleRules.value[0]?.id||'');
}
function finishNewAction(): void {
  if(!newDraft.value||!newOutputChosen.value||busy.value||recording.value)return;
  const problems=validateControllerShortcutRules([newDraft.value],oemKeys.value);
  if(problems.length){creationProblem.value=problems[0];newStep.value='input';return}
  const draft=newDraft.value;rules.value=[...rules.value,draft];newDraft.value=null;
  selectRule(draft.id);touch();status.value='新动作已加入列表，点击顶部“应用修改”保存。';
}
function returnToInputStep(): void {if(creating.value&&!recording.value)newStep.value='input'}

function removeRule(): void {
  const current = activeRule.value;
  if (!current || current.id===newDraft.value?.id) return;
  const index = visibleRules.value.findIndex((rule) => rule.id === current.id);
  rules.value = rules.value.filter((rule) => rule.id !== current.id);
  selectRule(visibleRules.value[Math.max(0, index - 1)]?.id || '');
  touch();
}
function restoreNativeDefaults(): void {
  newDraft.value=null;creationProblem.value='';
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
  status.value = '已恢复原版默认设置；点击“应用修改”后写入。';
}
async function load(): Promise<void> {
  if(props.presetDraft){
    rules.value=cloneRules(props.presetDraft.rules);quickAppChoices.value=(props.presetDraft.apps||[]).map(app=>({value:app.path,label:app.name}));
    gamepad.value={...gamepad.value,...Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map(definition=>[definition.defaultKey,rules.value.some(rule=>rule.origin==='native-default'&&rule.defaultKey===definition.defaultKey&&rule.enabled)]))};
    oemKeys.value=await oemKeysGet().catch(()=>OEM_EMPTY_SNAPSHOT);previewFocus.reset();newDraft.value=null;creationProblem.value='';rulePage.value=0;
    selectRule(visibleRules.value.find(rule=>rule.enabled)?.id||visibleRules.value[0]?.id||'');dirty.value=false;status.value='正在编辑 '+props.presetDraft.name+' 的手柄预设，应用修改后由外层保存。';return;
  }
  const settings = await loadSettings();
  snapshot.value = settings.input;
  const apps = settings.quickApps?.apps;
  quickAppChoices.value = Array.isArray(apps) ? apps.filter(app=>app && typeof app.path === 'string' && app.path && typeof app.name === 'string').map(app=>({value:app.path,label:app.name})) : [];
  gamepad.value = await summonGet().catch(() => gamepad.value);
  oemKeys.value = await oemKeysGet().catch(() => OEM_EMPTY_SNAPSHOT);
  const persisted = readControllerShortcutRules(settings.input.buttonMapping?.rules);
  const reconciled = reconcileControllerShortcutRules(persisted, nativeEnabledMap());
  previewFocus.reset();
  newDraft.value=null;creationProblem.value='';rulePage.value=0;
  rules.value = reconciled.rules;
  selectRule(visibleRules.value.slice(0, RULES_PER_PAGE).find((rule) => rule.enabled)?.id || visibleRules.value[0]?.id || '');
  dirty.value = false;
  status.value = reconciled.seeded > 0
    ? `已按默认模板生成 ${reconciled.seeded} 条默认快捷；修改后点击“应用修改”才会写入配置。`
    : visibleRules.value.length ? '已读取规则草稿。修改后需点击“应用修改”才会写入配置。' : '尚无快捷规则。可在左侧新建动作，或在顶部恢复默认。';
}
async function discardChanges(): Promise<void> {
  if (busy.value || recording.value || recordingBusy.value) return;
  busy.value = true;
  try { await load(); status.value = '已取消全部未应用修改。'; }
  catch { status.value = '重新读取配置失败，未应用的修改已保留。'; }
  finally { busy.value = false; }
}
async function apply(): Promise<void> {
  if (newDraft.value) {creationProblem.value='请先完成或取消正在新建的动作。';return}
  if (pendingCombination.value) { status.value = '请先确认新组合，或选择保留原组合。'; return; }
  if ((!snapshot.value&&!props.presetDraft) || busy.value || recording.value) {
    if (recording.value) status.value = '请先完成或取消当前录制。';
    return;
  }
  if (validation.value.length) { status.value = validation.value[0]; return; }
  if(props.presetDraft){emit('preset-apply',cloneRules(rules.value));dirty.value=false;status.value='手柄预设草稿已更新，请在开机模式中保存更改。';return;}
  busy.value = true;
  try {
    const baseline = snapshot.value!;
    const draft = cloneRules(rules.value);
    let savedRules = draft;
    let storageBase = baseline.buttonMapping?.rules;
    let storageRules = readControllerShortcutRules(storageBase);
    const targetAllowed = () => !virtualTargetShortcutError(storageRules, savedRules, currentVirtualTargetPolicy());
    const blocked = virtualTargetShortcutError(storageRules, savedRules, currentVirtualTargetPolicy());
    if (blocked) { status.value = blocked; return; }
    const patchFor = (entries: ControllerShortcutRule[]) => ({
      buttonMapping: { rules: { schemaVersion: 2, closure: 'UNENCLOSED', shortcutRules: preserveControllerShortcutRecords(storageBase, storageRules, entries) } },
    });
    let result = await compareAndSwapInputSettings(baseline.revision, patchFor(savedRules), targetAllowed);
    if (result.ok === false && result.reason === 'revision-conflict') {
      const merged = mergeControllerShortcutDraft(
        readControllerShortcutRules(baseline.buttonMapping?.rules), draft,
        readControllerShortcutRules(result.value.buttonMapping?.rules),
      );
      if (merged.ok === false) {
        dirty.value = true;
        status.value = '此动作已被其他页面修改，当前草稿已保留；请重新读取后再编辑。';
        return;
      }
      const problems = validateControllerShortcutRules(rulesToValidate(merged.rules), oemKeys.value);
      if (problems.length) { dirty.value = true; status.value = `合并未保存，草稿已保留：${problems[0]}`; return; }
      savedRules = merged.rules;
      storageBase = result.value.buttonMapping?.rules;
      storageRules = readControllerShortcutRules(storageBase);
      result = await compareAndSwapInputSettings(result.value.revision, patchFor(savedRules), targetAllowed); // one retry only
    }
    if (result.ok === false) {
      dirty.value = true;
      status.value = result.reason === 'revision-conflict' ? '配置仍在变更，草稿已保留，请稍后再次应用。'
        : result.reason === 'write-failed' ? '保存失败，草稿已保留，请重试。'
        : result.reason === 'cancelled' ? '保存已取消，草稿已保留。' : '专属配置正在接管，草稿已保留。';
      return;
    }
    snapshot.value = result.value;
    rules.value = readControllerShortcutRules(result.value.buttonMapping?.rules);
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
  nativeHeld = false;
  nativeRecorded.clear();
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
  if (hasUnavailableOemInput(normalized)) {
    cancelRecording(oemCaptureUnavailableHint);
    return;
  }
  recordingInputs.value = normalized.slice(0, CONTROLLER_SHORTCUT_MAX_INPUTS);
  if (normalized.length > CONTROLLER_SHORTCUT_MAX_INPUTS) {
    recordingStatus.value = `组合最多支持 ${CONTROLLER_SHORTCUT_MAX_INPUTS} 个输入按键，请取消后重新录制。`;
    return;
  }
  if (normalized.length < CONTROLLER_SHORTCUT_MIN_INPUTS) {
    recordingInputs.value = [];
    if (recordMode.value === 'keyboard-mouse') clearDesktopRecording();
    recordingStatus.value = `没有识别到输入，请按下至少一个手柄或键盘按键后松开。`;
    return;
  }
  stageCombination(normalized);
  recording.value = false;
  recordingStatus.value = `已录入 ${normalized.length}/${CONTROLLER_SHORTCUT_MAX_INPUTS} 个按键：${formatInputs(normalized)}`;
  void stopNativeRecording();
  clearDesktopRecording();
  if (props.inputFlowVariant !== 'confirm') void nextTick(() => focusEditorControl());
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
  pendingCombination.value = null;
  clearDesktopRecording();
  recordingBusy.value = true;
  try {
    if (isNativeRuntime) {
      const result = await setShortcutRecording(true);
      if (!result.active) { recordingStatus.value = `无法开始录制：${result.reason}`; return; }
    }
    recording.value = true;
    recordingStatus.value = '按住手柄、背键、专用键或键盘组合，全部松开后完成；允许混用，最多 4 键。';
  } catch (error) { recordingStatus.value = `录制启动失败：${(error as Error).message}`; }
  finally { recordingBusy.value = false; }
}

function onRecordingMode(value: string | number): void {
  if (recording.value || recordingBusy.value) return;
  pendingCombination.value = null;
  recordMode.value = value === 'keyboard-mouse' ? 'keyboard-mouse' : value === 'oem' ? 'oem' : 'controller';
}
function desktopInputFromMouse(button: number): ControllerShortcutInput | null {
  const code=({0:'MouseLeft',1:'MouseMiddle',2:'MouseRight',3:'MouseBack',4:'MouseForward'} as Record<number,string>)[button];
  return code?{source:'mouse',code}:null;
}
function armDesktopRelease(): void {
  if (desktopHeld.size !== 0 || nativeHeld || !recording.value) return;
  if (desktopReleaseTimer !== null) window.clearTimeout(desktopReleaseTimer);
  desktopReleaseTimer = window.setTimeout(() => {
    desktopReleaseTimer = null;
    if (!nativeHeld && desktopHeld.size === 0) finishRecording(uniqueInputs([...nativeRecorded.values(), ...desktopRecorded.values()]));
  }, 30);
}
function onCaptureKeydown(event: KeyboardEvent): void {
  if (!recording.value || event.isComposing) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  if (!keyboardCodePattern.test(event.code)) { recordingStatus.value = `暂未识别键码：${event.code || event.key}，请换一个按键或取消录制。`; return; }
  const key = `keyboard:${event.code}`;
  desktopHeld.add(key);
  if (!desktopRecorded.has(key) && desktopRecorded.size + nativeRecorded.size >= CONTROLLER_SHORTCUT_MAX_INPUTS) {
    recordingStatus.value = `已达到 ${CONTROLLER_SHORTCUT_MAX_INPUTS} 个按键上限；请松开后确认，或取消后重录。`;
  }
  desktopRecorded.set(key, { source: 'keyboard', code: event.code });
  recordingInputs.value = uniqueInputs([...nativeRecorded.values(), ...desktopRecorded.values()]);
}
function onCaptureKeyup(event: KeyboardEvent): void {
  if (!recording.value) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  desktopHeld.delete(`keyboard:${event.code}`);
  armDesktopRelease();
}
function onCapturePointerdown(event: PointerEvent): void {
  if(!recording.value)return;
  const path=event.composedPath();
  if(!path.includes(dialogEl.value as EventTarget)||path.some(node=>node instanceof Element&&node.matches('[data-record-control]')))return;
  const input=desktopInputFromMouse(event.button);if(!input)return;
  event.preventDefault();event.stopImmediatePropagation();const key=inputIdentity(input);
  desktopHeld.add(key);desktopRecorded.set(key,input);
  recordingInputs.value=uniqueInputs([...nativeRecorded.values(),...desktopRecorded.values()]);
}
function onCapturePointerup(event: PointerEvent): void {
  if(!recording.value)return;
  const path=event.composedPath();
  if(!path.includes(dialogEl.value as EventTarget)||path.some(node=>node instanceof Element&&node.matches('[data-record-control]')))return;
  const input=desktopInputFromMouse(event.button);if(!input)return;
  event.preventDefault();event.stopImmediatePropagation();desktopHeld.delete(inputIdentity(input));armDesktopRelease();
}
function onCaptureContextMenu(event: Event): void {if(recording.value){event.preventDefault();event.stopImmediatePropagation()}}
function onWindowBlur(): void { if (recording.value) cancelRecording('窗口失焦，键盘录制已取消。'); }
function onNativeGamepadRecording(data: { codes?: unknown; inputs?: unknown; released?: boolean }): void {
  if (!recording.value || !dialogEl.value?.getClientRects().length) return;
  const inputs: ControllerShortcutInput[] = [];
  for (const raw of Array.isArray(data?.inputs) ? data.inputs : []) {
    if (!raw || typeof raw !== 'object' || typeof raw.code !== 'string') continue;
    if (raw.source === 'controller' && controllerCodes.has(raw.code)) inputs.push({ source: 'controller', code: raw.code });
    if (raw.source === 'oem' && oemKeys.value.supported && oemKeys.value.keys.some(key => key.keyId === raw.code && key.captureImplemented !== false)) inputs.push({ source: 'oem', code: raw.code });
  }
  // Older native hosts emit a single released controller-code packet.
  if (!Array.isArray(data?.inputs)) for (const code of Array.isArray(data?.codes) ? data.codes : []) {
    if (typeof code === 'string' && controllerCodes.has(code as ControllerButtonCode)) inputs.push({ source: 'controller', code });
  }
  for (const input of inputs) nativeRecorded.set(inputIdentity(input), input);
  nativeHeld = data.released === false;
  recordingInputs.value = uniqueInputs([...nativeRecorded.values(), ...desktopRecorded.values()]);
  if (!nativeHeld) armDesktopRelease();
}

function onNativeRecordingStopped(data: { reason?: unknown }): void {
  if (!recording.value) return;
  cancelRecording(`录制已停止：${typeof data?.reason === 'string' ? data.reason : '输入所有权变化'}`);
}
/**
 * 2026-09-28 用户裁决：关闭=“取消并关闭”（不保存、直接退出）。
 * 普通编辑器保留直接取消并关闭；presetDraft 的未应用修改先确认放弃。
 * 顶部关闭 / B 键 / Esc / 点击遮罩全部走同一路径。
 */
function requestClose(): void {
  if (busy.value || editorDisposed) return;
  if (props.presetDraft && (dirty.value || newDraft.value || pendingCombination.value)) {
    if (presetCloseConfirm.value) return;
    cancelRecording();
    presetCloseFocus = document.activeElement as HTMLElement | null;
    presetCloseConfirm.value = true;
    void nextTick(() => { if (!editorDisposed && presetCloseConfirm.value) focusGamepadElement(presetCloseCancel.value); });
    return;
  }
  cancel();
}
function resumePresetEditing(): void {
  presetCloseConfirm.value = false;
  void nextTick(() => {
    if (editorDisposed || presetCloseConfirm.value) return;
    if (presetCloseFocus?.isConnected && dialogEl.value?.contains(presetCloseFocus) && focusGamepadElement(presetCloseFocus)) return;
    focusEditorControl();
  });
}
function discardPresetAndClose(): void {
  if (!presetCloseConfirm.value || busy.value || editorDisposed) return;
  presetCloseConfirm.value = false;
  cancel();
}
function presetCloseKeydown(event: KeyboardEvent): void {
  if (event.key !== 'Tab') return;
  const target = event.shiftKey ? presetCloseDiscard.value : presetCloseCancel.value;
  if (document.activeElement === (event.shiftKey ? presetCloseCancel.value : presetCloseDiscard.value)) {
    event.preventDefault();
    focusGamepadElement(target);
  }
}
function cancel(): void {
  if (editorDisposed) return;
  cancelRecording();
  // Preset rules belong to the outer draft; closing needs no native reload.
  if (props.presetDraft) { emit('close'); return; }
  // 关闭路径必须总是能退出：重读失败也照常关闭（页面激活时会重新加载）。
  void load().catch(() => undefined).finally(() => { if (!editorDisposed) emit('close'); });
}
function onKeydown(event: KeyboardEvent): void {
  if (event.defaultPrevented || !dialogEl.value?.getClientRects().length) return;
  if (recording.value) return;
  if (event.key === 'Escape') {
    event.preventDefault();
    if (presetCloseConfirm.value) resumePresetEditing();
    else requestClose();
  }
}

/**
 * Enter the editor through a real controller target. The dialog element is
 * intentionally tabindex=-1 for accessibility, but it is not a gamepad
 * control; leaving focus there makes the shared engine recover to the page
 * underneath on the next A/D-pad frame.
 */
function focusEditorControl(): void {
  if (editorDisposed) return;
  if (presetCloseConfirm.value) { focusGamepadElement(presetCloseCancel.value); return; }
  const root = dialogEl.value;
  if (!root) return;
  const actionTrigger = root.querySelector<HTMLElement>(
    '.edit-column .record-action:not(:disabled), .edit-column .manual-key:not(:disabled), .edit-column [data-gp-dropdown] button[aria-haspopup="listbox"]',
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
  if (!root || !root.getClientRects().length) return;
  // A Dropdown listener may already have consumed B in the same dispatch.
  // Respect that decision and keep the editor mounted.
  if (event.defaultPrevented || root.querySelector('[data-gp-modal="keyboard-output"]')) return;
  if (presetCloseConfirm.value) { event.preventDefault(); resumePresetEditing(); return; }
  const openTrigger = Array.from(root.querySelectorAll<HTMLElement>(
    '[aria-expanded="true"][aria-haspopup="listbox"]',
  )).find((el) => root.contains(el));
  if (openTrigger) return;
  event.preventDefault();
  requestClose();
}

onMounted(() => {
  void setEditorWindowWidth();
  void load()
    .then(() => { if (props.initialActionId) { const existing = rules.value.find(rule => rule.actionId === props.initialActionId); if (existing) selectRule(existing.id); else addRule(); } })
    .catch((error) => { status.value = `读取失败：${(error as Error).message}`; })
    .finally(() => { void nextTick(() => focusEditorControl()); });
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
  nextTick(() => dialogEl.value?.focus({ preventScroll: true }));
});
onActivated(() => { if (!props.initialActionId && !dirty.value && !recording.value) void load().catch(() => undefined); });
onBeforeUnmount(() => {
  editorDisposed = true;
  void releaseEditorWindowWidth();
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
  <Teleport :to="props.teleportTo">
    <div class="editor-overlay" data-gp-modal="controller-shortcut-editor" data-gp-scope="controller-shortcut-editor" data-gp-spatial="nearest" @pointerdown.self="requestClose">
      <div class="editor-stage">
      <section ref="dialogEl" class="editor-dialog" :inert="presetCloseConfirm || undefined" :data-layout-variant="layoutVariant" :data-global-layout="globalActionsLayout" :data-input-flow="'record'" :data-output-variant="outputActionVariant" role="dialog" aria-modal="true" aria-label="自定义快捷规则编辑器" tabindex="-1">
        <header class="editor-top">
          <div class="editor-heading"><div><h3>{{presetDraft?'手柄自定义':'快捷动作'}}</h3></div><span class="editor-save-state" :class="{ dirty: dirty || Boolean(newDraft) }" aria-live="polite"><i aria-hidden="true" />{{ newDraft ? '正在新建' : dirty ? '未应用修改' : '已同步' }}</span></div>
          <div class="editor-toolbar"><label class="preview-selector"><span>手柄图示</span><Dropdown :teleport-to="teleportTo" :model-value="selectedDiagramPersona || diagramPersona" :options="diagramPersonaOptions" aria-label="手柄图示" :disabled="busy || recording || recordingBusy" @update:model-value="selectedDiagramPersona=String($event)"/><small v-if="previewKind==='keyboard'">当前预览：键盘</small></label><ControllerEditorGlobalActions v-if="globalActionsLayout !== 'bottom'" :mode="globalActionsLayout === 'split' ? 'draft' : 'all'" :dirty="dirty" :pending="Boolean(pendingCombination || newDraft)" :busy="busy" :recording="recording || recordingBusy" @restore="restoreNativeDefaults" @discard="discardChanges" @apply="apply" @close="requestClose" /></div>
        </header>
        <main class="editor-layout">
          <aside class="preview-column">
              <section class="card mirror-card" style="--diagram-live-color:#4897d2">
                <div class="card-head"><div><span class="eyebrow">组合预览</span><h3>{{ previewKind === 'keyboard' ? '键盘' : diagramTitle }}</h3></div><span>{{ previewKind === 'keyboard' ? '键盘输入' : '手柄 / 掌机输入' }}</span></div>
                <div class="mirror-stage">
                <ControllerKeyboardPreview v-if="previewKind === 'keyboard'" :inputs="previewKeyboardKeys" :pressed-codes="keyboardLiveCodes" />
                <GamepadVisualizer v-else :persona="diagramPersona" :settings="gamepad" :highlight-buttons="highlightedButtons" :oem-keys="oemKeys" :highlight-oem-keys="highlightedOemKeys" :show-test-mode="false" minimum-press-feedback />
                <div v-if="previewKind === 'controller' && previewKeyboardKeys.length" class="handheld-key-bubbles" aria-live="polite"><span v-for="key in previewKeyboardKeys" :key="key.code" :data-handheld-extended="isHandheldExtendedKey(key)" :class="{live:keyboardLiveCodes.includes(key.code)}">{{ isHandheldExtendedKey(key) ? '掌机扩展键' : '混用键盘' }} · {{ formatInputs([key]) }}</span></div>
                </div>
                <div class="mirror-selection"><span>当前组合</span><strong>{{ activeInputText }}</strong><div class="mirror-legend"><span><i class="selected-dot" />关联按键</span><span><i class="live-dot" />实时输入</span></div></div>
              </section>
          </aside>

          <section class="rules-column" data-gp-pane="shortcut-list">
            <section class="card rule-list-card">
              <div class="card-head action-list-heading"><h3>动作列表</h3><span>{{ visibleRules.length - (newDraft ? 1 : 0) }} 个动作</span></div>
              <div v-if="!visibleRules.length" class="empty"><p>尚无动作</p></div>
              <div v-else class="rule-list">
                <div v-for="(rule, index) in pagedRules" :key="rule.id" class="rule-item" :class="{ selected: activeId === rule.id, managing: managementId === rule.id, 'menu-above': index >= pagedRules.length - 2, creating: rule.id===newDraft?.id }">
                  <button type="button" class="rule-row" :class="{ active: activeId === rule.id, disabled: !rule.enabled, creating: rule.id===newDraft?.id }" :disabled="busy || recording || recordingBusy" :aria-expanded="managementId === rule.id" @mouseenter="previewFocus.enter(rule.id)" @mouseleave="previewFocus.leave(rule.id)" @focus="previewFocus.focus(rule.id)" @blur="previewFocus.blur(rule.id)" @click="selectRule(rule.id, true)">
                    <span class="rule-copy"><b><template v-if="rule.id===newDraft?.id"><i class="creation-dot" aria-hidden="true"/>正在新建</template><template v-else>{{ outputLabel(rule) }}</template></b><span class="rule-metadata"><small>{{ rule.id===newDraft?.id ? (rule.inputs.length ? '已录制输入 · 等待完成' : '先录制输入') : formatInputs(rule.inputs) || '未录入输入' }}</small><span v-if="rule.id!==newDraft?.id" class="rule-state" :class="{ enabled: rule.enabled }"><i aria-hidden="true" />{{ rule.enabled ? '已启用' : '已停用' }}</span></span></span>
                  </button>
                  <div v-if="managementId === rule.id && rule.id!==newDraft?.id && layoutVariant !== 'dock'" class="rule-context" :class="{ 'rule-context-popover': layoutVariant === 'popover' }">
                    <span v-if="layoutVariant === 'popover'" class="rule-context-title">{{ actionById(rule.actionId)?.label || '动作操作' }}</span>
                    <ControllerActionControls :name="actionById(rule.actionId)?.label || '当前动作'" :enabled="rule.enabled" :disabled="busy || recording || recordingBusy" @toggle="updateRule({ enabled: !rule.enabled })" @remove="removeRule" @cancel="managementId = ''" />
                  </div>
                </div>
              </div>
              <div class="action-list-footer">
                <div v-if="layoutVariant === 'dock'" class="action-management-dock" :class="{ open: managementId && activeRule }">
                  <template v-if="managementId && activeRule">
                    <span class="rule-context-title">{{ activeAction?.label || '当前动作' }}</span>
                    <ControllerActionControls :name="activeAction?.label || '当前动作'" :enabled="activeRule.enabled" :disabled="busy || recording || recordingBusy" @toggle="updateRule({ enabled: !activeRule.enabled })" @remove="removeRule" @cancel="managementId = ''" />
                  </template>
                  <span v-else class="action-management-hint">选择卡片管理动作</span>
                </div>
                <button class="new-action small-action" type="button" :disabled="busy || recording || recordingBusy || Boolean(newDraft)" @click="addRule">{{newDraft?'正在新建…':'+ 新建动作'}}</button>
                <nav class="rule-pagination" aria-label="快捷规则分页"><button type="button" :disabled="safeRulePage === 0 || recording || recordingBusy || busy" @click="turnRulePage(-1)">上一页</button><span aria-live="polite">{{ safeRulePage + 1 }} / {{ rulePageCount }}</span><button type="button" :disabled="safeRulePage + 1 >= rulePageCount || recording || recordingBusy || busy" @click="turnRulePage(1)">下一页</button></nav>
              </div>
            </section>
          </section>

          <section class="edit-column" data-gp-pane="shortcut-settings">
            <section v-if="activeRule" class="card rule-editor-card" :class="{'rule-editor-card--creating':creating}">
              <nav class="io-flow" aria-label="输入到输出的编辑流程"><template v-for="(step,index) in creationSteps.slice(0,3)" :key="step.id"><span v-if="index" class="flow-arrow" aria-hidden="true">→</span><span :class="{current:creating && newStep===step.id}">{{step.label}}</span></template></nav>
              <div v-if="creating" class="creation-guide" aria-live="polite"><div class="creation-guide-row"><b>新建动作 · {{creationSteps.findIndex(step=>step.id===newStep)+1}} / 4</b><button v-if="newStep==='input'" class="creation-record-entry" type="button" :disabled="busy||recording||recordingBusy" @click="startRecording">开始录制</button></div><p>{{creationHelp}}</p></div>
              <p v-if="inputIssue" class="input-issue" role="alert" data-input-issue><InlineIcon name="warning"/>{{inputIssue}}</p>

              <div v-show="(!creating || newStep==='input') && (!outputPickerOpen || outputActionVariant === 'library')" class="editor-bubble recorder-bubble"><ControllerComboInput :variant="inputFlowVariant" :mode="recordMode" :persona="diagramPersona" :inputs="activeRule.inputs" :captured="recordingInputs" :candidate="pendingCombination" :catalog="oemKeys" :recording="recording" :busy="busy" :recording-busy="recordingBusy" :status="recordingStatus" @mode="onRecordingMode" @record="startRecording" @cancel-recording="cancelRecording" @clear="clearCombination" @oem="bindOemKey" @toggle-button="toggleManualButton" @accept="acceptCombination" @discard-candidate="pendingCombination = null" /></div>

              <div v-show="(!creating || newStep==='trigger') && (!outputPickerOpen || outputActionVariant === 'library')" class="editor-bubble action-trigger-bubble"><div class="action-trigger-grid"><div><span class="field-label">怎么触发</span><SegButton :model-value="activeRule.trigger" :options="oemTriggerOptions" full :disabled="busy || recording" @update:model-value="updateRule({ trigger: $event as ControllerShortcutRule['trigger'] })" /><p v-if="activeOemClickOnly" class="field-hint">当前专用键为一次性点击事件（WMI），仅支持按下/双击。</p><div class="timing-row"><label v-if="activeRule.trigger === 'hold' || activeRule.trigger === 'repeat'"><span>长按阈值</span><Dropdown :teleport-to="teleportTo" :model-value="activeRule.holdMs" :options="holdTimeOptions" color="accent" :disabled="busy || recording" @update:model-value="updateTiming('holdMs', $event)" /></label><label v-if="activeRule.trigger === 'double'"><span>双击窗口</span><Dropdown :teleport-to="teleportTo" :model-value="activeRule.doubleWindowMs" :options="holdTimeOptions" color="accent" :disabled="busy || recording" @update:model-value="updateTiming('doubleWindowMs', $event)" /></label><label v-if="activeRule.trigger === 'repeat'"><span>连发间隔</span><Dropdown :teleport-to="teleportTo" :model-value="activeRule.intervalMs" :options="repeatIntervalOptions" color="accent" :disabled="busy || recording" @update:model-value="updateTiming('intervalMs', $event)" /></label></div></div></div></div>
              <div v-show="!creating || newStep==='output' || newStep==='finish'" class="editor-bubble output-bubble"><ControllerOutputAction :summary-label="outputLabel(activeRule)" :variant="outputActionVariant" :unselected="creating && !newOutputChosen" :rule="activeRule" :disabled="busy || recording || recordingBusy" @action="chooseAction" @key="chooseOutputKey" @open="outputPickerOpen = $event">
              <div v-if="activeAction?.parameters?.some(p => p.presentation !== 'key-grid')" class="editor-bubble"><span class="field-label">动作参数</span><details v-if="activeAction.description" class="output-help"><summary>功能说明</summary><p class="field-hint">{{ activeAction.description }}</p></details><div class="parameter-grid" :class="{ 'keyboard-key-grid': keyGridParameter }" :style="keyGridParameter ? { '--key-grid-columns': keyGridParameter.gridColumns || 6 } : undefined"><template v-for="parameter in (activeAction?.parameters || []).filter(p => p.presentation !== 'key-grid')" :key="parameter.id"><template v-if="parameter.presentation === 'key-grid'"><button v-for="option in parameter.options || []" :key="option.value" class="keyboard-key-action" :class="{ active: String(activeRule.params[parameter.id] ?? parameter.default) === option.value }" type="button" :disabled="busy || recording" @click="updateParameter(parameter.id, option.value)">{{ option.label }}</button></template><label v-else-if="parameter.type !== 'boolean'"><span>{{ parameter.label }}</span><Dropdown :teleport-to="teleportTo" v-if="parameter.type === 'select'" :model-value="String(activeRule.params[parameter.id] ?? parameter.default)" :options="parameter.options || []" color="accent" :disabled="busy || recording" @update:model-value="updateParameter(parameter.id, $event)" /><input v-else type="number" :min="parameter.min" :max="parameter.max" :step="parameter.step || 1" :value="Number(activeRule.params[parameter.id] ?? parameter.default)" :disabled="busy || recording" @change="updateParameter(parameter.id, Number(($event.target as HTMLInputElement).value))"></label><label v-else class="parameter-toggle-field"><span>{{ parameter.label }}</span><button class="parameter-toggle-action" :class="{ active: Boolean(activeRule.params[parameter.id] ?? parameter.default) }" type="button" :disabled="busy || recording" @click="updateParameter(parameter.id, !Boolean(activeRule.params[parameter.id] ?? parameter.default))">{{ parameter.label }}{{ Boolean(activeRule.params[parameter.id] ?? parameter.default) ? '已开启' : '已关闭' }}</button></label></template></div></div>
              </ControllerOutputAction></div>

              <div v-if="creating && !recording" class="creation-controls"><button v-if="newStep!=='input'" type="button" class="creation-back" :disabled="busy" @click="returnToInputStep">重新录制输入</button><button v-if="newStep==='trigger'" class="creation-next" type="button" @click="newStep='output'">下一步：选择动作</button><button v-if="newStep==='finish'" class="creation-finish" type="button" @click="finishNewAction">完成新建</button><button type="button" class="creation-cancel" @click="cancelNewAction">取消新建</button></div>

            </section>
            <section v-else class="card empty-editor"><InlineIcon name="keyboard" /><h3>选择一个动作</h3><p>从左侧选择或新建动作，在这里编辑组合输入与参数。</p></section>
            <section v-if="notice && !inputIssue" class="status-bubble"><span>{{notice}}</span></section>
          </section>
        </main>
        <footer v-if="globalActionsLayout !== 'top'" class="editor-global-footer" :class="{ 'editor-global-footer-split': globalActionsLayout === 'split' }">
          <ControllerEditorGlobalActions :mode="globalActionsLayout === 'split' ? 'commit' : 'all'" :dirty="dirty" :pending="Boolean(pendingCombination || newDraft)" :busy="busy" :recording="recording || recordingBusy" @restore="restoreNativeDefaults" @discard="discardChanges" @apply="apply" @close="requestClose" />
        </footer>
      </section>
      </div>
      <div v-if="presetCloseConfirm" class="preset-close-shade" @pointerdown.self="resumePresetEditing">
        <section class="preset-close-dialog" role="dialog" aria-modal="true" aria-label="放弃未应用的手柄修改" data-gp-modal="controller-preset-discard" data-gp-scope="controller-preset-discard" data-gp-spatial="nearest" data-gp-page-lock @keydown="presetCloseKeydown">
          <h2>放弃未应用的手柄修改？</h2>
          <p>这些修改尚未应用到模式草稿。继续编辑可保留修改，放弃后返回配置工作台。</p>
          <div class="preset-close-actions"><button ref="presetCloseCancel" type="button" @click="resumePresetEditing"><b>B</b> 继续编辑</button><button ref="presetCloseDiscard" class="confirm" type="button" @click="discardPresetAndClose">放弃并返回</button></div>
        </section>
      </div>
    </div>
  </Teleport>
</template>

<style scoped>
/* Match the existing GameProfileRemoveDialog confirmation surface and actions. */
.preset-close-shade{position:fixed;inset:0;z-index:1500;display:grid;place-items:center;padding:20px;box-sizing:border-box;background:rgba(0,0,0,.45)}
.preset-close-dialog{width:min(440px,100%);max-height:calc(100vh - 40px);overflow-y:auto;box-sizing:border-box;border:1px solid var(--border);border-radius:12px;padding:18px 20px;background:var(--bg-panel);box-shadow:0 16px 40px #0008;display:flex;flex-direction:column;gap:12px;color:var(--text)}.preset-close-dialog h2{margin:0;font-size:17px}.preset-close-dialog p{margin:0;font-size:14px;line-height:1.65;color:var(--text-dim)}
.preset-close-actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:3px}.preset-close-actions button{min-height:44px;border:1px solid var(--border);border-radius:9px;background:var(--bg-input);color:var(--text);font:inherit;font-size:14px;font-weight:600;cursor:pointer}.preset-close-actions .confirm{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 45%,transparent);background:color-mix(in srgb,var(--danger) 12%,var(--bg-input))}.preset-close-actions b{margin-right:5px}

.editor-overlay{position:fixed;z-index:1100;inset:0;display:grid;place-items:center;padding:16px;background:rgba(2,7,13,.72);backdrop-filter:blur(8px)}.editor-dialog{width:min(1280px,calc(100vw - 32px));height:min(720px,calc(100dvh - 32px));box-sizing:border-box;overflow:auto;padding:14px;border:1px solid rgba(255,255,255,.11);border-radius:18px;background:linear-gradient(145deg,color-mix(in srgb,var(--bg-card) 97%,#152334),var(--bg-panel));box-shadow:0 28px 80px rgba(0,0,0,.58);color:var(--text);outline:none}.editor-top{display:grid;grid-template-columns:auto minmax(0,1fr) auto;align-items:center;gap:12px;margin-bottom:12px}.editor-top h3,.card h3{margin:1px 0 0;font-size:15px}.editor-top h3 .inline-icon{color:var(--accent)}.back,.small-action,.outline-action,.apply-action,.remove-action,.record-action,.clear-action{min-height:34px;padding:0 10px;border-radius:var(--radius-ctrl);font-size:11px;font-weight:800;cursor:pointer}.back,.outline-action,.clear-action{border:1px solid color-mix(in srgb,var(--accent) 52%,transparent);background:transparent;color:var(--accent)}.small-action,.apply-action,.record-action{border:1px solid var(--accent);background:var(--accent);color:#06121d}.remove-action{border:1px solid color-mix(in srgb,var(--danger) 48%,transparent);background:color-mix(in srgb,var(--danger) 9%,transparent);color:#ff9ea1}.back:disabled,.small-action:disabled,.outline-action:disabled,.apply-action:disabled,.remove-action:disabled,.record-action:disabled,.clear-action:disabled{opacity:.46;cursor:default}.eyebrow{color:var(--accent);font-size:10px;font-weight:800;letter-spacing:.08em}.state-chip{display:inline-flex;align-items:center;min-height:24px;padding:0 8px;border:1px solid color-mix(in srgb,var(--accent) 45%,transparent);border-radius:999px;background:color-mix(in srgb,var(--accent) 10%,transparent);color:var(--accent);font-size:10px;font-weight:800}.state-chip.dirty{border-color:color-mix(in srgb,#f5b93d 48%,transparent);background:color-mix(in srgb,#f5b93d 10%,transparent);color:#ffd47a}.editor-layout{display:grid;grid-template-columns:minmax(190px,.72fr) minmax(215px,.82fr) minmax(380px,1.32fr);gap:10px;align-items:start}.preview-column,.rules-column,.edit-column{display:grid;gap:10px;min-width:0}.card{padding:11px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-card);background:color-mix(in srgb,var(--bg-card) 93%,transparent)}.card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:8px}.card-head>span{color:var(--text-dim);font-size:10px;font-weight:700}.mirror-card :deep(.gamepad-visualizer){margin-top:8px;transform:scale(.82);transform-origin:top center;margin-bottom:-26px}.edit-head-preview{margin-left:9px;color:var(--accent);font-size:11px;font-weight:800;vertical-align:middle;white-space:nowrap}.route-note,.field-hint{margin:7px 0 0;color:var(--text-dim);font-size:10px;line-height:1.45}.route-note{display:flex;gap:5px}.route-note .inline-icon{flex:0 0 auto;color:#f5b93d}.rule-tools{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:9px 0}.rule-tools small{color:var(--text-dim);font-size:10px}.rule-list{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;max-height:510px;overflow:auto;padding-right:2px}.rule-row{min-height:50px;display:flex;align-items:center;justify-content:space-between;gap:8px;padding:8px;border:1px solid rgba(255,255,255,.09);border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text);text-align:left;cursor:pointer}.rule-row.active{border-color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,var(--bg-input));box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 36%,transparent)}.rule-row.disabled{opacity:.55}.rule-row span{display:grid;gap:2px;min-width:0}.rule-row b{font-size:11px}.rule-row small{overflow:hidden;color:var(--text-dim);font-size:10px;text-overflow:ellipsis;white-space:nowrap}.rule-row i{padding:3px 6px;border-radius:999px;background:rgba(255,255,255,.09);color:var(--text-dim);font-size:9px;font-style:normal;font-weight:800}.rule-editor-card{display:grid;gap:8px}.editor-bubble,.status-bubble{padding:10px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent)}.field-label{display:flex;align-items:center;justify-content:space-between;margin-bottom:7px;color:var(--text-dim);font-size:11px;font-weight:800}.field-label small{font-size:10px}.recorder-bubble :deep(.seg-button){margin-bottom:8px}.recorded-inputs{display:flex;align-items:center;flex-wrap:wrap;gap:5px;min-height:38px;padding:7px;border:1px dashed rgba(255,255,255,.15);border-radius:8px;background:var(--bg-panel);color:var(--text-dim);font-size:10px}.recorded-inputs.recording{border-color:var(--accent);box-shadow:inset 0 0 0 1px color-mix(in srgb,var(--accent) 28%,transparent);color:var(--accent)}.recorded-inputs kbd{padding:4px 6px;border:1px solid color-mix(in srgb,var(--accent) 46%,transparent);border-radius:6px;background:color-mix(in srgb,var(--accent) 13%,transparent);color:var(--text);font:700 10px/1 system-ui}.record-actions{display:grid;grid-template-columns:1fr auto;gap:7px;margin-top:7px}.record-action{display:flex;align-items:center;justify-content:center;gap:5px}.timing-row,.parameter-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:7px;margin-top:8px}.timing-row label,.parameter-grid label{position:relative;display:grid;gap:4px;color:var(--text-dim);font-size:10px;font-weight:700}.timing-row input,.parameter-grid input{min-width:0;height:30px;padding:0 25px 0 8px;border:1px solid rgba(255,255,255,.12);border-radius:7px;background:var(--bg-panel);color:var(--text);font-size:11px}.timing-row i{position:absolute;right:7px;bottom:8px;color:var(--text-dim);font-size:9px;font-style:normal}.parameter-grid :deep(.toggle-row){min-height:30px;padding:0}.parameter-grid :deep(.toggle-label){font-size:10px}.editor-actions{display:grid;grid-template-columns:auto 1fr auto;gap:7px}.status-bubble{display:flex;align-items:flex-start;gap:7px;color:var(--text-dim);font-size:10px;line-height:1.45}.status-bubble .inline-icon{flex:0 0 auto;color:var(--accent)}.status-bubble.warning{border-color:color-mix(in srgb,#f5b93d 45%,transparent);color:#ffd47a}.status-bubble.warning .inline-icon{color:#f5b93d}.empty,.empty-editor{display:grid;gap:8px;place-items:center;min-height:150px;text-align:center}.empty p,.empty-editor p{margin:0;color:var(--text-dim);font-size:11px}.empty-editor .inline-icon{color:var(--accent)}@media(max-width:980px){.editor-dialog{height:auto;max-height:calc(100dvh - 16px);width:min(840px,calc(100vw - 16px));padding:10px}.editor-layout{grid-template-columns:minmax(220px,.75fr) minmax(350px,1.25fr)}.rules-column{grid-column:1/-1}.rule-list{grid-template-columns:repeat(2,minmax(0,1fr));max-height:250px}.mirror-card :deep(.gamepad-visualizer){transform:scale(.74);margin-bottom:-34px}}@media(max-width:650px){.editor-overlay{padding:7px}.editor-dialog{width:calc(100vw - 14px);max-height:calc(100dvh - 14px);padding:8px}.editor-layout{grid-template-columns:1fr}.rules-column{grid-column:auto}.rule-list{grid-template-columns:repeat(2,minmax(0,1fr));max-height:none}.preview-column{display:none}.editor-top{grid-template-columns:auto 1fr}.editor-top .state-chip{display:none}.timing-row,.parameter-grid{grid-template-columns:1fr 1fr}.editor-actions{grid-template-columns:auto 1fr auto}}@media(max-width:420px){.timing-row,.parameter-grid{grid-template-columns:1fr}.editor-actions{grid-template-columns:1fr 1fr}.editor-actions span{display:none}}
/*
 * Keep the navigator and input mirror together in the first row.  The
 * selected-rule editor is the primary workspace, so it spans the full row
 * underneath them instead of being squeezed into a third column.
 */
.editor-overlay{padding:0;overflow:hidden}
.editor-stage{width:100%;height:100%;display:grid;place-items:center;overflow:hidden;transform:none}
.editor-dialog{width:calc(100% - 24px);height:calc(100% - 24px);max-height:none;box-sizing:border-box;overflow:auto;scrollbar-gutter:stable;padding:14px}
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
@media(min-width:860px){
  .editor-layout{grid-template-columns:minmax(200px,.75fr) minmax(230px,.85fr) minmax(0,1.6fr);gap:14px}
  .edit-column,.preview-column,.rules-column{grid-column:auto}
  .rule-list{grid-template-columns:repeat(2,minmax(0,1fr));max-height:540px}
  .rule-row{min-height:58px;padding:10px}
  .rule-row b{font-size:13px}.rule-row small{font-size:12px}
  .field-label{font-size:13px}.field-hint,.route-note{font-size:12px}
  .rule-editor-card{grid-template-columns:minmax(0,1fr)}
  .action-trigger-grid>div:first-child,.action-trigger-grid>div:nth-child(2),.recorder-bubble{grid-column:1;grid-row:auto}
  .record-action,.editor-actions>button,.oem-action,.oem-clear{min-height:40px;font-size:13px}
}
.rule-list-card{display:flex;flex-direction:column}
.rule-list-card .rule-list{max-height:none;overflow:visible}
.rule-pagination{display:flex;align-items:center;justify-content:space-between;gap:5px;margin-top:auto;padding-top:12px;color:var(--text-dim);font-size:11px}
.rule-pagination span{flex-shrink:0;white-space:nowrap}
.rule-pagination button{min-height:34px;padding:5px;border:1px solid rgba(255,255,255,.15);border-radius:7px;background:var(--bg-input);color:var(--text);font:inherit;white-space:nowrap;cursor:pointer}
.rule-pagination button:disabled{opacity:.4;cursor:default}
.mirror-card{display:grid;grid-template-rows:auto auto 112px;align-content:start;gap:16px;border-radius:var(--radius)}
.mirror-stage{flex:1;min-width:0;display:grid;place-items:center;padding:12px 0}
.mirror-stage :deep(.viz){width:100%;min-width:0}
.mirror-selection{display:grid;grid-template-rows:14px minmax(0,1fr) 14px;min-height:0;box-sizing:border-box;gap:10px;padding:14px;border:1px solid color-mix(in srgb,var(--accent) 22%,transparent);border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text-dim);font-size:12px}
.mirror-selection strong{min-height:0;overflow:auto;color:var(--text);font-size:17px;overflow-wrap:anywhere}
.mirror-legend{display:flex;flex-wrap:wrap;gap:14px;font-size:11px}
.mirror-legend span{display:inline-flex;align-items:center;gap:6px}
.mirror-legend i{width:6px;height:6px;border-radius:50%}.selected-dot{background:var(--accent)}.live-dot{background:var(--diagram-live-color,var(--ok))}
@media(min-width:980px){
  /* Parameter/recording panels scroll independently; never recenter the device. */
  .editor-dialog{display:flex;flex-direction:column;overflow:hidden}
  .editor-top,.editor-width-toolbar{flex-shrink:0}
  .editor-layout{flex:1;min-height:0;grid-template-rows:minmax(0,1fr)}
  .preview-column,.rules-column,.edit-column{min-height:0}
  .edit-column{overflow:auto;scrollbar-gutter:stable;align-content:start}
  .mirror-card{min-height:0;display:grid;grid-template-rows:auto minmax(0,1fr) 112px}
  .mirror-stage{min-height:0;overflow:visible;container-type:size}
  .mirror-stage :deep(.device-diagram){width:min(100cqw,calc((100cqh - 112px)*var(--device-ratio)));margin:0 auto}
  .mirror-selection{min-height:0;box-sizing:border-box;grid-template-rows:14px minmax(0,1fr) 14px}
  .mirror-selection strong{min-height:0;overflow:auto}
  .editor-layout{grid-template-columns:minmax(155px,.65fr) minmax(0,2fr) minmax(285px,1.25fr)}
  .rules-column{order:0}.preview-column{order:1}.edit-column{order:2}
  .rule-list-card .rule-list{grid-template-columns:minmax(0,1fr)}
}

/* Three source-backed proposals share the same actions, drafts and input pipeline. */
.action-list-heading{align-items:center;margin-bottom:10px;flex-shrink:0}
.action-list-heading h3{font-size:13px}
.rule-list-card{min-height:0}
.rule-list-card .rule-list{display:grid;grid-template-columns:minmax(0,1fr);gap:7px;align-content:start;min-height:0;overflow:visible;padding:0}
.rule-item{position:relative;min-width:0}
.rule-item.managing{z-index:3}
.rule-list-card .rule-row{width:100%;box-sizing:border-box;display:grid;grid-template-columns:minmax(0,1fr);gap:4px;padding:9px 10px;min-height:58px}
.rule-copy{display:grid;gap:5px;min-width:0}
.rule-row .rule-metadata{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;column-gap:7px;row-gap:3px}
.rule-list-card .rule-row b{font-size:12px;line-height:1.35}
.rule-list-card .rule-row small{font-size:11px;line-height:1.3}
.rule-list-card .rule-row.disabled{opacity:1;color:var(--text-dim)}
.rule-row .rule-state{display:flex;align-items:center;justify-self:start;gap:5px;color:var(--text-dim);font-size:10px;line-height:1.2}
.rule-row .rule-state i{display:block;width:5px;height:5px;padding:0;flex:0 0 auto;border-radius:50%;background:var(--text-dim)}
.rule-row .rule-state.enabled{color:#8cc7ef}
.rule-row .rule-state.enabled i{background:#4897d2}
.rule-context{box-sizing:border-box;padding:11px 9px;border:1px solid color-mix(in srgb,var(--accent) 50%,transparent);border-top:0;border-radius:0 0 8px 8px;background:var(--bg-panel)}
[data-layout-variant=inline] .rule-item.managing>.rule-row{border-radius:8px 8px 0 0}
.rule-context-title{display:block;min-width:0;overflow-wrap:anywhere;color:var(--text);font-size:12px;font-weight:700;line-height:1.4;margin-bottom:8px}
.rule-context-popover{position:absolute;inset:calc(100% + 5px) 0 auto;padding:12px 10px;border:1px solid #487292;border-radius:9px;background:var(--bg-panel);z-index:4}
.menu-above .rule-context-popover{inset:auto 0 calc(100% + 5px)}
.action-list-footer{display:grid;gap:9px;margin-top:auto;padding-top:12px;flex-shrink:0}
.new-action{width:100%;box-sizing:border-box;min-height:36px}
.rule-pagination{padding-top:0;margin-top:0}
.action-management-dock{box-sizing:border-box;min-height:117px;display:grid;align-content:center;padding:10px 0;border-top:1px solid rgba(255,255,255,.12)}
.action-management-hint{color:var(--text-dim);font-size:11px;text-align:center}
@media(min-width:980px){
 .rule-list-card .rule-list{flex:1;overflow-y:auto;overscroll-behavior:contain;scrollbar-gutter:stable}
 [data-layout-variant=popover] .rule-list-card .rule-list{overflow:visible}
 .editor-layout{grid-template-columns:minmax(185px,.78fr) minmax(0,1.9fr) minmax(285px,1.25fr)}
 .rule-list-card .rule-row{min-height:58px}
}
@media(max-width:650px){
 .rule-list-card .rule-list{grid-template-columns:repeat(2,minmax(0,1fr))}
 .action-management-dock{min-height:117px}
}

@media(max-width:380px){.rule-list-card .rule-list{grid-template-columns:minmax(0,1fr)}}
/* The accepted card popover stays fixed; only the global action placement varies. */
.editor-top{display:flex;align-items:center;justify-content:space-between;gap:16px;min-height:40px;margin-bottom:12px;flex-shrink:0}
.editor-heading{display:flex;align-items:center;gap:18px;min-width:0}
.editor-heading h3{font-size:15px;line-height:1.35;white-space:nowrap}
.editor-save-state{display:inline-flex;align-items:center;gap:6px;flex:0 0 auto;color:var(--text-dim);font-size:11px;white-space:nowrap}
.editor-save-state i{width:5px;height:5px;border-radius:50%;background:#4897d2}
.editor-save-state.dirty i{background:#f5b942}
.editor-global-footer{display:flex;align-items:center;justify-content:center;flex:0 0 auto;box-sizing:border-box;padding-top:10px;margin-top:10px;border-top:1px solid rgba(255,255,255,.08)}
.editor-global-footer-split{justify-content:flex-end}
@media(max-width:980px){.editor-top{align-items:flex-start;flex-wrap:wrap}.editor-heading{flex:1 1 100%}}
@media(max-width:650px){.editor-top{display:grid;grid-template-columns:minmax(0,1fr);gap:10px}.editor-heading{flex-wrap:wrap;gap:6px}.editor-global-footer{display:block}}
/* Accepted desktop layout: wider two-column cards, modestly smaller device. */
@media(min-width:980px){
 .editor-layout{grid-template-columns:minmax(300px,1fr) minmax(0,1.45fr) minmax(350px,1.25fr)}
 .rule-list-card .rule-list{grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
 .rule-list-card .rule-row{min-height:72px;padding:10px 9px}
 .rule-row .rule-metadata{row-gap:5px}
 .rule-context-popover{padding:10px 8px}
 .rule-context-title{font-size:11px}
}
.action-trigger-grid>div{min-width:0}
.rule-editor-card{grid-template-columns:minmax(0,1fr);grid-template-rows:none;grid-auto-rows:max-content;align-content:start;align-items:start}
.recorder-bubble{display:block;grid-template-rows:none;grid-row:auto;grid-column:1;align-content:start;align-self:start}
.action-trigger-bubble{display:block;grid-row:auto;grid-column:1;padding:0;border:0;background:transparent}
.action-trigger-grid{display:block}
.action-trigger-grid>div:first-child{grid-column:auto;grid-row:auto}
.handheld-key-bubbles span:not(.live){animation:handheld-mapped-fill .85s ease-in-out infinite}
@keyframes handheld-mapped-fill{0%,100%{background-color:color-mix(in srgb,var(--accent) 35%,var(--bg-input))}50%{background-color:var(--bg-input)}}
@media(prefers-reduced-motion:reduce){.handheld-key-bubbles span:not(.live){animation:none}}
</style>

<style scoped>
.output-bubble{border-color:color-mix(in srgb,var(--accent) 30%,var(--border))}.output-bubble :deep(.editor-bubble){border:0;padding:0;margin-top:2px;background:transparent}.handheld-key-bubbles{position:absolute;inset:8px 0 auto;display:flex;justify-content:center;flex-wrap:wrap;gap:6px}.handheld-key-bubbles span{font-size:11px;padding:6px 9px;border:1px solid var(--border);border-radius:20px;background:var(--bg-input);color:var(--accent)}.mirror-stage{position:relative}.rule-editor-card{gap:10px}.output-help{font-size:11px;color:var(--text-dim)}.output-help summary{padding:4px 0;cursor:pointer}
</style>

<style scoped>
.editor-top{display:flex;justify-content:space-between;align-items:center;gap:12px}.editor-heading h3{margin:0;font-size:17px}.editor-toolbar{display:flex;align-items:center;gap:10px;min-width:0}.preview-selector{display:flex;align-items:center;gap:7px;position:relative;font-size:11px;white-space:nowrap}.preview-selector :deep(.dd){width:142px}.preview-selector :deep(.dropdown-trigger),.preview-selector :deep(.dd-trigger){min-height:40px}.preview-selector small{position:absolute;right:0;bottom:-14px;font-size:10px;color:var(--text-dim)}.io-flow{display:flex;align-items:center;justify-content:space-between;gap:5px;color:var(--text-dim);font-size:12px;min-height:30px}.io-flow .current{color:var(--accent);font-weight:700}.flow-arrow{color:var(--accent)}.creation-guide{padding:12px;background:var(--bg-input);border-radius:8px;color:var(--text)}.creation-guide b{font-size:12px}.creation-guide p{font-size:12px;line-height:1.7;margin:6px 0 0;color:var(--text-dim)}.creation-controls{display:grid;grid-template-columns:1fr 1fr;gap:8px}.creation-controls button{min-height:42px;padding:8px;border:1px solid var(--border);border-radius:7px;background:var(--bg-input);font:inherit;font-size:12px;color:var(--text);cursor:pointer}.creation-controls .creation-next,.creation-controls .creation-finish{grid-column:1/-1;background:var(--accent);color:#fff;font-weight:700}.creation-controls .creation-cancel{grid-column:1/-1}.input-issue{display:flex;align-items:flex-start;gap:6px;padding:10px;margin:0;border-radius:7px;background:color-mix(in srgb,var(--danger) 12%,var(--bg-panel));color:var(--danger);font-size:12px;line-height:1.6}.rule-row.creating{border-style:dashed;animation:new-action-pending 1.6s ease-in-out infinite}.creation-dot{display:inline-block;width:6px;height:6px;margin-right:5px;border-radius:50%;background:var(--accent);animation:new-action-dot 1.6s ease-in-out infinite}.handheld-key-bubbles span.live{background:var(--accent);border-color:var(--accent);color:#fff}.rule-row.creating i{padding:0}.rule-row.creating .rule-copy b{color:var(--accent)}@keyframes new-action-pending{50%{background:color-mix(in srgb,var(--accent) 15%,var(--bg-input))}}@keyframes new-action-dot{50%{opacity:.3}}@media(prefers-reduced-motion:reduce){.rule-row.creating,.creation-dot{animation:none}}
</style>

<style scoped>
.rule-pagination{display:grid;grid-template-columns:minmax(0,1fr) auto minmax(0,1fr);align-items:center;gap:8px}.rule-pagination button{min-width:0;white-space:nowrap}.rule-pagination span{white-space:nowrap;text-align:center}
</style>

<style scoped>
.rule-editor-card--creating{display:flex!important;flex-direction:column;gap:12px;align-content:start}.rule-editor-card--creating>.editor-bubble{grid-column:auto;grid-row:auto;align-self:auto}.rule-editor-card--creating>.creation-controls{display:flex;flex-wrap:wrap;gap:8px;grid-column:auto}
</style>

<style scoped>
.creation-guide-row{display:flex;align-items:center;justify-content:space-between;gap:8px}.creation-record-entry{min-height:40px;border:1px solid var(--border);background:var(--bg-input);color:var(--accent);border-radius:7px;padding:0 10px;font:600 12px inherit}
</style>
