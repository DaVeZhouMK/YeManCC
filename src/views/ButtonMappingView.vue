<script setup lang="ts">
// [AI:USER:CONTROLLER] 选择SteamDeck/PS5/Xbox→请求对应虚拟输出；关闭虚拟手柄→结束虚拟输出、返回本机手柄并停止相关体感联动。
// 切换、唤醒与退出不能留下旧输出继续响应；未确认恢复时须如实显示，保存选择不等于运行成功。
// 切页/重新进入不是新的开启动作；公用与当前游戏专属分别编辑，外置优先是独立选择。
// 手柄输入、虚拟输出、触摸反馈与游戏震动分别核实；具体执行见 native/main.cpp、InputHost/Program.cs，机型能力先查总库。
import { CONTROLLER_PERSONA_OPTIONS, isControllerPersona, migrateControllerPersona, type ControllerPersona } from '@/bridge/controllerPersona';
import ControllerChannelStatus from '@/components/ControllerChannelStatus.vue';
import Toggle from '@/components/Toggle.vue';
import GameProfileModeBanner from '@/components/GameProfileModeBanner.vue';
import GameProfileRemoveDialog from '@/components/GameProfileRemoveDialog.vue';
import {loadGameCustomConfig,removeGameCustomProfile,restorePerformanceScheduleIfConfigured,type GameCustomProfileTarget} from '@/bridge/performanceSchedule';
import {tryAcquireQuickAction} from '@/bridge/quickActionLock';
import {focusGamepadElement} from '@/gamepad/focus';
import IntegratedPage from '@/components/IntegratedPage.vue';
import UsbipUpgradeStatus from '@/components/UsbipUpgradeStatus.vue';
import { useVirtualTargetPolicy, currentVirtualTargetPolicy } from '@/bridge/virtualTargetPolicyState';
import { virtualTargetOptions, virtualTargetSelectionError, requireVirtualTargetAllowed, STEAM_DECK_SELF_OUTPUT_MESSAGE } from '@/bridge/virtualTargetPolicy';
import { computed, nextTick, onActivated, onDeactivated, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import InlineIcon from '@/components/InlineIcon.vue';
import ScreenTouchpadsSettings from '@/components/ScreenTouchpadsSettings.vue';
import ControllerShortcutsCard from '@/components/ControllerShortcutsCard.vue';
import {mouseModeState} from '@/bridge/mouseModeState';
import {mouseModeText} from '@/bridge/mouseModePresentation';
import ControllerFeedbackCapabilitiesCard from '@/components/ControllerFeedbackCapabilitiesCard.vue';
import ControllerCalibrationCard from '@/components/ControllerCalibrationCard.vue';
import ControllerShortcutEditorView from '@/views/ControllerShortcutEditorView.vue';
import { compareAndSwapInputSettings, onInputSettingsChanged, VirtualStartProtectionActive, type InputSettingsSnapshot } from '@/bridge/settingsRepository';
import { invoke, on } from '@/bridge/ipc';
import { gamepadEnvironmentNotice } from '@/bridge/gamepadEnvironmentNotice';
import type { HidHideSetupSnapshot } from '@/bridge/hidHideSetupPrompt';
import { normalizeOemKeysSnapshot } from '@/bridge/controllerShortcutRules';
import { normalizeOemInputUsage, type OemInputUsage } from '@/bridge/controllerFeatureStatus';
import { normalizeGyroTelemetry } from '@/bridge/inputContracts';
import { VirtualStartProtectionView, VIRTUAL_START_MAX_WAIT_MS } from '@/bridge/virtualStartProtectionView';
import { reportFrontendTrace } from '@/robust/frontendDiagnostics';
import { readGameInputEditor, saveGameInputEditor, onGameInputEditorChanged, type GameInputEditor } from '@/bridge/gameInputEditor';
import { closeJoyxoffIfRunning } from '@/bridge/gameproc';
import { acceptVirtualOutputRuntime, externalControllerPriorityPresentation, virtualOutputPresentation, type ExternalPriorityRuntimeState, type VirtualOutputRuntimeState, type VirtualStartProtectionState } from '@/bridge/virtualOutputStatus';

// Only the current three virtual outputs are selectable or admitted.
const basePersonaOptions = CONTROLLER_PERSONA_OPTIONS;
type ControllerPage = 'controller' | 'touchpad' | 'calibration' | 'feedback';
const controllerPage = ref<ControllerPage>('controller');
const controllerPages = [{id:'controller',title:'控制器',icon:'gamepad'}, {id:'touchpad',title:'触控板',icon:'mouse'}, {id:'calibration',title:'校对',icon:'gamepad'}, {id:'feedback',title:'灯光震动',icon:'volume'}] as const;
function selectControllerPage(page: ControllerPage): void {
  controllerPage.value = page;
  void nextTick(() => controllerRoot.value?.closest('.app-content')?.scrollTo({top:0}));
}
function backToControllerSection(event: Event): void {
  if (event.defaultPrevented || editorOpen.value || profileRemoval.value || document.querySelector('.dd-open,[data-gp-modal]')) return;
  const focused = document.activeElement as HTMLElement | null;
  if (!controllerRoot.value?.contains(focused) || focused?.closest('.controller-page-tabs')) return;
  event.preventDefault();
  focusGamepadElement(controllerRoot.value.querySelector<HTMLElement>('[data-controller-section="'+controllerPage.value+'"]'));
}
onMounted(() => window.addEventListener('ipc:gamepad-back', backToControllerSection));
onActivated(() => window.addEventListener('ipc:gamepad-back', backToControllerSection));
onDeactivated(() => window.removeEventListener('ipc:gamepad-back', backToControllerSection));
onBeforeUnmount(() => window.removeEventListener('ipc:gamepad-back', backToControllerSection));

const virtualTargetPolicy = useVirtualTargetPolicy();
const setupSnapshot = ref<HidHideSetupSnapshot>({ driverSetup: 0, hidHideSetupNotice: 0, driverSetupRedistDir: '' });
const environmentNotice = computed(() => gamepadEnvironmentNotice(setupSnapshot.value));
const faultHoldActive = ref(false);
const virtualOutputRuntime = ref<VirtualOutputRuntimeState | null>(null);
const physicalGyroSupported = ref<boolean | null>(null);
const physicalBackButtons = ref<number | null>(null);
const physicalBackButtonsCaptured = ref<number | null>(null);
let stopPhysicalGyro: (() => void) | null = null;
let channelStatusMounted = false;
let channelWasInactive = false;
let channelReadEpoch = 0;
const oemUsage = ref<OemInputUsage | null>(null);
const observedOemKeys = ref<string[]>([]);
// Presentation reads only the existing OEM catalog/source state. No sensor
// enumeration, remapping, target creation or settings write is added here.
async function readPhysicalChannelCapabilities(): Promise<void> {
  const epoch = ++channelReadEpoch;
  try {
    const raw = await invoke<unknown>('oem.keys.status', {});
    if (!channelStatusMounted || epoch !== channelReadEpoch) return;
    const usage = normalizeOemInputUsage(raw);
    oemUsage.value = usage;
    const state = usage || normalizeOemKeysSnapshot(await invoke<unknown>('oem.keys.get', {}));
    if (!channelStatusMounted || epoch !== channelReadEpoch) return;
    if (!state.supported) { physicalBackButtons.value = null; physicalBackButtonsCaptured.value = null; return; }
    const keys = state.keys.filter(key => key.backIndex !== null);
    physicalBackButtons.value = keys.length;
    physicalBackButtonsCaptured.value = keys.filter(key => key.captureImplemented !== false).length;
  } catch { if (channelStatusMounted && epoch === channelReadEpoch) oemUsage.value = null; }
}
const externalPriorityRuntime = ref<ExternalPriorityRuntimeState | null>(null);
const externalControllerPriority = ref(false);
const externalPriorityBusy = ref(false);
const externalPriorityError = ref('');
let stopInputSettings: (() => void) | null = null;
// GP-926R13 §4：恢复耗尽后的可见降级（0 无 / 1 已请求物理兜底 / 2 读回物理可用 / 3 兜底未证实）。
const recoveryFallbackState = ref(0);
// 用户裁决（2026-09-14）：DualShock 4 冲突告警机制（UI 与机制）整体移除——
// 不再监听 neteaseGvInput/viGEmBus，也不再渲染冲突提示。仅保留通用后端故障
// faultHoldActive（backendFaultHold）提示。
const editorOpen = ref(false);
const controllerPreviewHost = ref<HTMLElement | null>(null);
const snapshot = ref<InputSettingsSnapshot | null>(null);
const targetBusy = ref(false);
const startProtectionView = new VirtualStartProtectionView();
const startProtectionLocked = ref(false);
const targetProtectionNotice = ref('');
let targetSaveEpoch = 0;
let targetWaitTimer: ReturnType<typeof setTimeout> | undefined;
let targetWaitScheduledAt: number | null = null;
let targetOperationDeadline: number | null = null;
let targetOperationEpoch = 0;
let targetOperationInitialRequestId = 0;
let targetOperationPersona: ControllerPersona | null = null;
let lastProtectionExpiryRequest = -1;
function refreshTargetWait(): void {
  const now = performance.now();
  const view = startProtectionView.read(now);
  startProtectionLocked.value = view.locked;
  if (targetBusy.value && targetOperationDeadline !== null && view.requestId > targetOperationInitialRequestId) {
    const observedDeadline = view.deadline ?? (view.expired || view.terminal ? now : null);
    if (observedDeadline !== null) targetOperationDeadline = Math.min(targetOperationDeadline, observedDeadline);
  }
  if (view.expired && view.requestId !== lastProtectionExpiryRequest) {
    lastProtectionExpiryRequest = view.requestId;
    reportFrontendTrace('gamepad-admission', { stage: view.terminal ? 'virtual-start-ui-settle-complete' : 'virtual-start-ui-wait-expired',
      requestId: view.requestId, waitingForReply: targetBusy.value, backendAdmissionChanged: false, replay: false });
  }
  if (targetOperationDeadline !== null && now >= targetOperationDeadline) {
    if (targetOperationEpoch === targetSaveEpoch && targetBusy.value) {
      reportFrontendTrace('gamepad-admission', { stage: 'virtual-start-ui-reply-timeout',
        requestEpoch: targetOperationEpoch, persona: targetOperationPersona, maxMs: VIRTUAL_START_MAX_WAIT_MS,
        backendAdmissionChanged: false, replay: false });
      ++targetSaveEpoch;
      targetBusy.value = false;
      targetProtectionNotice.value = '未收到切换结果，可重试或切回本机手柄。';
    }
    targetOperationDeadline = null;
    targetOperationEpoch = 0;
    targetOperationPersona = null;
  }
  if (!view.locked && targetProtectionNotice.value.startsWith('手柄切换处理中'))
    targetProtectionNotice.value = view.expired && !view.terminal ? '等待确认已超时，可重试或切回本机手柄。' : '';
  const deadline = view.deadline === null ? targetOperationDeadline
    : targetOperationDeadline === null ? view.deadline : Math.min(view.deadline, targetOperationDeadline);
  if (deadline === targetWaitScheduledAt) return;
  if (targetWaitTimer !== undefined) clearTimeout(targetWaitTimer);
  targetWaitTimer = undefined;
  targetWaitScheduledAt = deadline;
  // One request-scoped wakeup, never a steady input poll or a request retry.
  if (deadline !== null) targetWaitTimer = setTimeout(() => {
    targetWaitTimer = undefined;
    targetWaitScheduledAt = null;
    refreshTargetWait();
  }, Math.max(0, Math.ceil(deadline - now)));
}
function acceptStartProtection(state?: VirtualStartProtectionState): void {
  const view = startProtectionView.observe(state, performance.now());
  virtualOutputRuntime.value = acceptVirtualOutputRuntime(virtualOutputRuntime.value, virtualOutputRuntime.value, view.requestId);
  targetProtectionNotice.value = view.locked ? '手柄切换处理中，请稍候。'
    : view.expired && !view.terminal ? '等待确认已超时，可重试或切回本机手柄。' : '';
  refreshTargetWait();
}
function targetRequestCurrent(epoch: number): boolean {
  if (epoch !== targetSaveEpoch) return false;
  refreshTargetWait();
  return epoch === targetSaveEpoch;
}
function finishTargetWait(epoch: number): void {
  if (epoch !== targetSaveEpoch) return;
  targetBusy.value = false;
  if (targetOperationEpoch === epoch) {
    targetOperationDeadline = null;
    targetOperationEpoch = 0;
    targetOperationPersona = null;
  }
  refreshTargetWait();
}
const inputEditor = ref<GameInputEditor | null>(null);
const editingGame = computed(() => inputEditor.value?.gameName || '');
const gameLocked = computed(() => !inputEditor.value);

const controllerRoot = ref<HTMLElement | null>(null);
const profileRemoval = ref<{target:GameCustomProfileTarget;gameName:string;revision:string}|null>(null);
const profileRemovalBusy = ref(false), profileRemovalError = ref(''), profileRemovalNote = ref('');
let controllerActive = true, profileRemovalEpoch = 0;
function restoreControllerFocus(selector = '[data-controller-profile-exit]'): void {
  void nextTick(() => {
    if (!controllerActive) return;
    const requested = controllerRoot.value?.querySelector<HTMLElement>(selector);
    const visible = requested && requested.getClientRects().length > 0 && !requested.matches(':disabled') && !requested.closest('[hidden],[inert]');
    focusGamepadElement(visible ? requested : controllerRoot.value?.querySelector<HTMLElement>('[data-controller-section="'+controllerPage.value+'"]') || null);
  });
}
async function requestProfileRemoval(): Promise<void> {
  const editor = inputEditor.value;
  if (!controllerActive || !editor?.profile || targetBusy.value || externalPriorityBusy.value || profileRemovalBusy.value) return;
  const target = {...editor.target}, gameName = editingGame.value, requestEpoch = ++profileRemovalEpoch;
  profileRemovalBusy.value = true; profileRemovalNote.value = '';
  try {
    const config = await loadGameCustomConfig(), current = inputEditor.value, entry = config.entries[target.exe];
    if (!controllerActive || requestEpoch !== profileRemovalEpoch) return;
    if (!current?.profile || current.ownerEpoch !== editor.ownerEpoch || current.target.exe !== target.exe || current.target.pid !== target.pid || current.target.processCreated !== target.processCreated || current.target.generation !== target.generation || !entry)
      throw new Error('当前游戏已变化，请重新确认退出专属');
    profileRemovalError.value = '';
    profileRemoval.value = {target,gameName,revision:JSON.stringify([entry,config.rtss[target.exe]])};
  } catch (error) {
    if (controllerActive && requestEpoch === profileRemovalEpoch) profileRemovalNote.value = `退出专属失败：${(error as Error).message}`;
  } finally { profileRemovalBusy.value = false; }
}
function cancelProfileRemoval(): void {
  if (profileRemovalBusy.value) return;
  profileRemoval.value = null; restoreControllerFocus();
}
async function confirmProfileRemoval(): Promise<void> {
  const request = profileRemoval.value;
  if (!request || profileRemovalBusy.value) return;
  const release = tryAcquireQuickAction('controller-profile-remove');
  if (!release) { profileRemovalError.value = '已有其它快捷操作正在执行，请稍候'; return; }
  profileRemovalBusy.value = true; profileRemovalError.value = ''; let deleted = false;
  try {
    await removeGameCustomProfile(request.target,request.revision);
    deleted = true; profileRemoval.value = null;
    await restorePerformanceScheduleIfConfigured();
  } catch (error) {
    if (deleted) profileRemovalNote.value = `专属已删除，恢复公用调度未完成：${(error as Error).message}`;
    else profileRemovalError.value = (error as Error).message;
  } finally {
    profileRemovalBusy.value = false; release();
    if (deleted && controllerActive) { await loadTarget().catch(() => undefined); restoreControllerFocus('.persona-btn.active:not(:disabled)'); }
  }
}

let stopOverride: (() => void) | null = null;
let stopBackendState: (() => void) | null = null;
let stopShortcutRuntime: (() => void) | null = null;
type ShortcutRuntimeState = { active: boolean; persona: string; virtualEnabled: boolean; gyroEnabled?: boolean };
const shortcutRuntime = ref<ShortcutRuntimeState | null>(null);
function applyShortcutRuntime(state: Partial<ShortcutRuntimeState> | null | undefined): void {
  if (typeof state?.active !== 'boolean') return;
  shortcutRuntime.value = state.active && typeof state.persona === 'string' && typeof state.virtualEnabled === 'boolean'
    ? state as ShortcutRuntimeState : null;
}
async function readShortcutRuntime(): Promise<void> {
  try { applyShortcutRuntime(await invoke<ShortcutRuntimeState>('input.shortcutRuntime.get', {})); } catch { /* Older hosts have no temporary switches. */ }
}
// 2026-09-27 用户裁决：删除底部状态行与其数据源（targetStatus）——成功/失败
// 都不再回显文案；保存失败时按钮高亮停在旧值。
const targetEnabled = ref(false);
const gyroEnabled = ref(false);
const persona = ref<ControllerPersona>('disabled');
// Read-only runtime feedback must never be copied into the saved page refs.
const displayPersona = computed(() => shortcutRuntime.value?.persona ?? persona.value);
const controllerTitle = computed(() => {
  if (!snapshot.value && !shortcutRuntime.value) return '控制器';
  if (displayPersona.value === 'disabled') return '本机控制器';
  const selected = basePersonaOptions.find(option => option.value === displayPersona.value);
  return selected ? `${selected.label} 控制器` : '控制器';
});
const displayTargetEnabled = computed(() => shortcutRuntime.value?.virtualEnabled ?? targetEnabled.value);
// Received-key feedback belongs only to this page/machine/mode. A later mode or
// cached-page entry cannot reuse an earlier press as evidence of current input.
watch(() => [displayTargetEnabled.value, displayPersona.value, oemUsage.value?.deviceClass], () => {
  observedOemKeys.value = []; physicalGyroSupported.value = null;
}, { flush: 'sync' });
const visiblePersonaOptions = computed(() => basePersonaOptions);
// 2026-09-30 用户裁决：背部按键气泡不再写死固定说明，改为如实
// 反馈当前输出人格的真实能力（陀螺仪走不走虚拟手柄、背键能识别几个）。口径与实现对齐：
//   背键：steamdeck → InputHost BuildSteamDeckState 只置 LeftPaddle/RightPaddle
//   （M1/M2→L5/R5，2 个）；dualsense-edge → MapDsEdgePaddles 同两位（G4 四键首期未接）；
//   elite 走 BuildEliteState 不消费 backButtons；disabled 只表示没有虚拟通道，物理能力另取回执。
//   陀螺仪：带 IMU 通道的虚拟手柄（SteamDeck / PS5）支持；Xbox 族无 IMU → 不支持。
const PERSONA_CAPABILITIES: Record<string, { gyro: boolean; backButtons: number }> = {
  disabled: { gyro: false, backButtons: 0 },
  steamdeck: { gyro: true, backButtons: 2 },
  'dualsense-edge': { gyro: true, backButtons: 2 },
  elite: { gyro: false, backButtons: 0 },
};
const virtualOutputStatus = computed(() => {
  const blocked = virtualTargetSelectionError(displayPersona.value, virtualTargetPolicy.value);
  return blocked ? { text: '虚拟手柄-本机不可用', detail: blocked, tone: 'warning' as const }
    : virtualOutputPresentation(virtualOutputRuntime.value, displayTargetEnabled.value, displayPersona.value, targetBusy.value, externalPriorityRuntime.value);
});
const externalPriorityStatus = computed(() => {
  if (externalPriorityError.value) return { text: externalPriorityError.value, detail: '设置未保存，请重试。', tone: 'warning' };
  return externalControllerPriorityPresentation(externalPriorityRuntime.value, externalControllerPriority.value, externalPriorityBusy.value);
});
const externalPriorityDescription = computed(() => externalPriorityStatus.value.text + '。' + externalPriorityStatus.value.detail);
const personaCapability = computed(() => PERSONA_CAPABILITIES[displayPersona.value] || PERSONA_CAPABILITIES.disabled);
// 背键映射（背键映射A任务书 2026-09-14 S6；2026-09-24 operator 裁决"开关去掉 改为常开"）：
// 桥只剩信息栏，无开关；输出人格支持背键位即生效。
// 真 Steam Deck 物理机自带背键（native 门控永不挂）。
// 2026-09-27 用户裁决：页面不再显示背部按键提示小字（功能一字未改），
// 原先只为提示文案服务的机器判定（machineFamily）随之移除。
// 用户裁决（2026-09-16）：删除「配置状态 / 运行时闭口」展示块的 computed
// 数据源——页面不再呈现内部账本字段（applyStatus / closure）。
// `outputTarget.closure` 是持久化配置元数据而非 Coordinator/InputHost 运行时
// 回执；不作为运行时闭口声明呈现。

// Runtime intent persists for live native consumption only; cold-start intent lives in startupDesired.
let loadGeneration = 0;
function applyInputSnapshot(input: InputSettingsSnapshot): void {
  // A late read/save reply must never roll the page back to an older revision.
  if (snapshot.value && input.revision < snapshot.value.revision) return;
  snapshot.value = input;
  externalControllerPriority.value = input.ownership?.externalControllerPriority === true;

  targetEnabled.value = input.outputTarget?.buttonMappingEnabled === true;
  gyroEnabled.value = input.outputTarget?.gyroEnabled === true;
  const value = String(input.outputTarget?.persona || 'disabled');
  persona.value = migrateControllerPersona(value);   // 批112
}
async function loadTarget(): Promise<void> {
  const generation = ++loadGeneration;
  const editor = await readGameInputEditor();
  if (generation !== loadGeneration) return;
  inputEditor.value = editor;
  applyInputSnapshot(editor.input);
  await readShortcutRuntime();

}
async function saveExternalPriority(): Promise<void> {
  if (!snapshot.value || externalPriorityBusy.value || targetBusy.value || profileRemovalBusy.value) return;
  const requested = !externalControllerPriority.value;
  externalPriorityError.value = '';
  externalPriorityBusy.value = true;
  try {
    // Retry one revision conflict with the same narrow intent. Never resend a
    // whole ownership/target snapshot that could erase another writer's fields.
    for (let attempt = 0; attempt < 2; attempt++) {
      const result = await compareAndSwapInputSettings(snapshot.value.revision, {
        ownership: { externalControllerPriority: requested },
      });
      inputEditor.value = await readGameInputEditor();
      applyInputSnapshot(inputEditor.value.input);
      if (result.ok === true || (result.reason === 'revision-conflict' && externalControllerPriority.value === requested)) {
        if (externalControllerPriority.value !== requested) externalPriorityError.value = '设置已被其他操作更新，请重试';
        return;
      }
      if (result.reason === 'revision-conflict' && attempt === 0) continue;
      externalPriorityError.value = result.reason === 'revision-conflict'
        ? '设置已被其他操作更新，请重试' : '保存失败，请重试';
      return;
    }
  } catch {
    externalPriorityError.value = '保存失败，请重试';
    await loadTarget().catch(() => undefined);
  } finally { externalPriorityBusy.value = false; }
}
async function saveTarget(nextEnabled: boolean, nextPersona: ControllerPersona): Promise<void> {   // 批112
  refreshTargetWait();
  if (gameLocked.value || !snapshot.value || externalPriorityBusy.value || profileRemovalBusy.value) return;
  if (targetBusy.value || startProtectionLocked.value) return;
  if (virtualTargetSelectionError(nextPersona, currentVirtualTargetPolicy(), nextEnabled)) return;
  const editorAtRequest = inputEditor.value!;
  const epoch = ++targetSaveEpoch;
  targetProtectionNotice.value = '';
  targetBusy.value = true;
  targetOperationEpoch = epoch;
  targetOperationInitialRequestId = startProtectionView.read(performance.now()).requestId;
  targetOperationPersona = nextPersona;
  targetOperationDeadline = performance.now() + VIRTUAL_START_MAX_WAIT_MS;
  refreshTargetWait();
  try {
    const patch: Record<string, unknown> = {
      outputTarget: { buttonMappingEnabled: nextEnabled, persona: nextPersona },
    };
    // 切回本机手柄时陀螺仪也必须停，否则 native 仍会因 gyroEnabled 继续请求虚拟目标。
    if (nextPersona === 'disabled') {
      (patch.outputTarget as Record<string, unknown>).gyroEnabled = false;
      patch.gyroMotion = { enabled: false };
    } else if (nextEnabled && snapshot.value.gyroMotion?.virtualPadLink !== false &&
      !(snapshot.value.outputTarget?.buttonMappingEnabled === true && snapshot.value.outputTarget?.persona !== 'disabled')) {
      // Live off -> on linkage belongs to the actual button action, NOT page
      // hydration (which would override an explicit startup gyro=off choice).
      (patch.outputTarget as Record<string, unknown>).gyroEnabled = true;
      patch.gyroMotion = { enabled: true, outputMode: 'virtual-stick' };
    }
    // Read the committed revision once when returning to the physical source.
    // All player switches, including OFF, share the same bounded admission.
    const editor = nextPersona === 'disabled' ? await readGameInputEditor() : editorAtRequest;
    if (!targetRequestCurrent(epoch) || editor.ownerEpoch !== editorAtRequest.ownerEpoch ||
      editor.target.generation !== editorAtRequest.target.generation) return;
    const result = await saveGameInputEditor(editor, patch, () => targetRequestCurrent(epoch) &&
      !startProtectionLocked.value &&
      !virtualTargetSelectionError(nextPersona, currentVirtualTargetPolicy(), nextEnabled));
    if (!targetRequestCurrent(epoch)) return;
    if (result.ok === false) {
      if (result.reason === 'virtual-start-protection-active') {
        acceptStartProtection(result.startProtection);
        return;
      }
      await loadTarget();
      return;
    }
    applyInputSnapshot(result.value);
    const committedEditor = await readGameInputEditor();
    if (!targetRequestCurrent(epoch)) return;
    inputEditor.value = committedEditor;
    // A manual selection wins over a runtime-only shortcut, even if reselecting the saved persona.
    const cleared = await invoke<{ ok?: boolean; reason?: string; startProtection?: VirtualStartProtectionState }>('input.shortcutRuntime.clear', { observeProtection: true });
    if (!targetRequestCurrent(epoch)) return;
    // Both successful and refused ACKs carry the producer's original deadline.
    // This observes admission only; it cannot declare target creation complete.
    if (cleared?.startProtection) acceptStartProtection(cleared.startProtection);
    if (cleared?.ok === false) return;
    shortcutRuntime.value = null;
    // S4 常驻（2026-09-15 用户定案）：关闭开关 = 停写不拆（虚拟设备保持
    // 存活、身份不变、物理手柄持续隐藏）；仅切回「本机手柄」才真正拆除解隐。
    // 2026-09-27 用户裁决：状态行整行移除，保存结果不再回显文案。
  } catch (error) {
    if (targetRequestCurrent(epoch) && error instanceof VirtualStartProtectionActive) acceptStartProtection(error.startProtection);
    /* 保存失败时保留已确认的选择。 */
  } finally { finishTargetWait(epoch); }
}
// 用户裁决（2026-09-16）：删除「启用虚拟手柄」开关——「本机手柄」即关闭态，
// 由输出控制器按钮直接控制（选中即启用，选回本机手柄即关闭/拆塔解隐）。
// 原 onTargetEnabled 已随之删除。
function onPersona(value: string | number): void {
  refreshTargetWait();
  if (gameLocked.value || externalPriorityBusy.value || profileRemovalBusy.value) return;
  const next = isControllerPersona(value) ? value : 'disabled';   // 批112
  if (targetBusy.value || startProtectionLocked.value) return;
  if (virtualTargetSelectionError(next, currentVirtualTargetPolicy())) return;
  // 2026-09-30 用户裁决：切到 SteamDeck 虚拟手柄时鼠标交给 Steam，JoyXoff 若在跑
  // 强行关掉（存在才关）；顶部专属菜单里切同一人格时走同一处理。
  if (next === 'steamdeck') void closeJoyxoffIfRunning().catch(() => undefined);
  void saveTarget(next !== 'disabled', next);
}
onMounted(() => {
  channelStatusMounted = true;
  void readPhysicalChannelCapabilities();
  stopPhysicalGyro = on<{ sensorPresent?: boolean; gamepadMotionPlane?: unknown }>('gyro.telemetry', state => {
    if (!controllerActive || !displayTargetEnabled.value) return;
    // Existing virtual telemetry only; native mode never starts a gyro query.
    if (state?.sensorPresent === true || normalizeGyroTelemetry(state)?.gamepadMotionPlane)
      physicalGyroSupported.value = true;
  });
  stopInputSettings = onInputSettingsChanged(() => { void loadTarget().catch(() => undefined); void readPhysicalChannelCapabilities(); });
  stopShortcutRuntime = on<ShortcutRuntimeState>('input.shortcutRuntime', applyShortcutRuntime);
  stopOverride = onGameInputEditorChanged(() => {
    void loadTarget().catch(() => undefined);
  });
  void loadTarget().catch(() => undefined);
  // Native publishes the read-only backend state with gamepad.state; the card
  // only re-renders, it never disables anything. 用户裁决（2026-09-14）：
  // DS4 冲突告警已整体移除，不再消费 virtualInputConflicts。
  stopBackendState = on<HidHideSetupSnapshot & { backendFaultHold?: boolean; recoveryFallback?: number; virtualOutput?: VirtualOutputRuntimeState; externalPriority?: ExternalPriorityRuntimeState; oemPressedKeys?: string[] }>('gamepad.state', (state) => {
    if (controllerActive && channelStatusMounted && Array.isArray(state.oemPressedKeys) && oemUsage.value) {
      const known = new Set(oemUsage.value.keys.filter(key => key.captureImplemented !== false).map(key => key.keyId));
      const pressed = state.oemPressedKeys.filter(key => typeof key === 'string' && known.has(key));
      if (pressed.some(key => !observedOemKeys.value.includes(key))) observedOemKeys.value = [...new Set([...observedOemKeys.value, ...pressed])];
    }
    const driverSetup = state.driverSetup ?? 0;
    const hidHideSetupNotice = state.hidHideSetupNotice ?? 0;
    const driverSetupRedistDir = state.driverSetupRedistDir ?? '';
    const previousSetup = setupSnapshot.value;
    // Runtime feedback can change frequently; only display fields invalidate the notice.
    if (previousSetup.driverSetup !== driverSetup || previousSetup.hidHideSetupNotice !== hidHideSetupNotice || previousSetup.driverSetupRedistDir !== driverSetupRedistDir) {
      setupSnapshot.value = { driverSetup, hidHideSetupNotice, driverSetupRedistDir };
    }
    acceptStartProtection(state.virtualOutput?.startProtection);
    virtualOutputRuntime.value = acceptVirtualOutputRuntime(virtualOutputRuntime.value, state.virtualOutput ?? null, startProtectionView.read(performance.now()).requestId);
    externalPriorityRuntime.value = state.externalPriority ?? null;
    faultHoldActive.value = state.backendFaultHold === true;
    recoveryFallbackState.value = state.recoveryFallback ?? 0;
  });
});
onBeforeUnmount(() => {
  channelStatusMounted = false; ++channelReadEpoch;
  stopPhysicalGyro?.();
  ++targetSaveEpoch;
  targetOperationDeadline = null; targetOperationEpoch = 0; targetOperationPersona = null;
  if (targetWaitTimer !== undefined) clearTimeout(targetWaitTimer);
  targetWaitTimer = undefined; targetWaitScheduledAt = null;
  controllerActive = false; ++profileRemovalEpoch; profileRemoval.value = null;
  stopOverride?.(); stopBackendState?.(); stopShortcutRuntime?.(); stopInputSettings?.();
});
onActivated(() => { controllerActive=true; refreshTargetWait(); void loadTarget().catch(() => undefined); if (channelWasInactive) { channelWasInactive=false; void readPhysicalChannelCapabilities(); } });
// The controller page is cached by KeepAlive. Do not leave a Teleport-ed
// modal visible after navigating to another page.
onDeactivated(() => { controllerActive=false; channelWasInactive=true; ++channelReadEpoch; observedOemKeys.value=[]; physicalGyroSupported.value=null; ++profileRemovalEpoch; editorOpen.value=false; profileRemoval.value=null; });
function openShortcutEditor(): void {
  if (editorOpen.value) return;
  editorOpen.value = true;
}
function closeShortcutEditor(): void {
  editorOpen.value = false;
}
</script>

<template>
  <IntegratedPage :title="controllerTitle" hide-heading>
  <div ref="controllerRoot" class="page controller-page controller-paged" :data-controller-panel="controllerPage" data-gp-scope="controller-page" data-gp-spatial="nearest">
    <header class="console-heading controller-unified-header"><h1 class="controller-page-title"><InlineIcon name="gamepad"/>{{controllerPage==='controller'?controllerTitle:controllerPage==='touchpad'?'触控板':controllerPage==='calibration'?'校对':'灯光与震动'}}</h1><Toggle v-if="controllerPage==='controller'" :model-value="externalControllerPriority" label="外置优先" compact aria-describedby="external-priority-status" :title="externalPriorityDescription" :disabled="!snapshot || externalPriorityBusy || targetBusy || profileRemovalBusy" @update:model-value="saveExternalPriority"/><span v-if="controllerPage==='controller' && mouseModeState?.on && mouseModeState.stateKnown && mouseModeState.provider === 'ymcc'" class="controller-mouse-active" role="status"><InlineIcon name="mouse"/>{{mouseModeState.verification === 'process-liveness' ? '鼠标映射待确认' : '模拟鼠标开启'}}</span><span v-if="controllerPage==='touchpad'" class="controller-current-persona" aria-label="当前虚拟手柄，只读"><InlineIcon name="gamepad"/>{{displayPersona==='disabled'?'本机手柄':basePersonaOptions.find(option=>option.value===displayPersona)?.label}}<span class="persona-readonly-dot" aria-hidden="true"/></span><span v-else-if="controllerPage==='feedback'" class="controller-current-persona"><InlineIcon name="gamepad"/>当前设备</span><span id="external-priority-status" class="policy-accessible-status" role="status" aria-live="polite">{{externalPriorityDescription}}</span></header>
    <GameProfileModeBanner v-if="editingGame" class="controller-profile-banner" :game-name="editingGame"><template #actions><button class="controller-profile-exit" data-controller-profile-exit data-gp-pane="controller-editor" :disabled="targetBusy || externalPriorityBusy || profileRemovalBusy" @click="requestProfileRemoval">退出专属</button></template></GameProfileModeBanner>
    <p v-if="profileRemovalNote" class="controller-profile-note" role="status">{{profileRemovalNote}}</p>
    <nav class="controller-page-tabs" :style="{ gridTemplateColumns: 'repeat(' + controllerPages.length + ', minmax(0, 1fr))' }" data-gp-pane="controller-editor" aria-label="控制器分区"><button v-for="page in controllerPages" :key="page.id" type="button" :data-controller-section="page.id" :aria-pressed="controllerPage===page.id" @click="selectControllerPage(page.id)"><InlineIcon :name="page.icon"/><span>{{page.title}}</span></button></nav>
    <div class="controller-page-content" data-gp-pane="controller-editor">
    <div v-show="controllerPage==='controller'" class="controller-general-panel">
    <!-- 2026-09-27 用户裁决：虚拟手柄卡移到页面顶部（手柄快捷操作之上）。 -->
    <section class="card virtual-card">


      <!-- 用户裁决（2026-09-16）：去掉「启用虚拟手柄」开关（「本机手柄」即关闭态）
           与说明小字；输出控制器四按钮改为性能调度同款独立按钮行（无 bubble
           包裹，按钮直接落在卡片上，具有独立外轮廓）。 -->
      <div class="persona-block">
        <UsbipUpgradeStatus errors-only />
        <!-- Environment preparation is advisory, including future driver versions and unknown inventories.
             Only actual backend/output failures below describe a failed virtual controller. -->

        <div v-if="environmentNotice" :class="environmentNotice.tone === 'warning' ? 'fault-line' : 'environment-note'" role="status">
          <strong>{{ environmentNotice.title }}</strong><span>{{ environmentNotice.description }}</span>
        </div>
        <div v-if="targetEnabled && faultHoldActive" class="fault-line">
          <strong>虚拟输出已停写</strong>
          <span>后端创建或输入输出失败，已停止虚拟输出。请导出日志查看原因，重新选择虚拟手柄可重试。</span>
        </div>
        <div v-else-if="targetEnabled && recoveryFallbackState > 0" class="fault-line">
          <strong>虚拟未恢复</strong>
          <span>{{ recoveryFallbackState === 2 ? '已请求物理兜底（读回：物理可用）。' : '已请求物理兜底（读回结果见日志）。' }}请重新开关虚拟手柄或切换人格重试。</span>
        </div>
        <div class="persona-actions">
          <button
            v-for="o in visiblePersonaOptions"
            :key="o.value"
            type="button"
            class="persona-btn"
            :class="{ active: o.value === displayPersona }"
            :data-gp-initial-focus="o.value === 'disabled' ? 'true' : undefined"
            :disabled="gameLocked || (targetBusy || startProtectionLocked) || externalPriorityBusy || profileRemovalBusy || !snapshot || !!virtualTargetSelectionError(o.value, virtualTargetPolicy)"
            :title="virtualTargetSelectionError(o.value, virtualTargetPolicy)"
            @click="onPersona(o.value)"
          >{{ o.label }}</button>
        </div>
<p v-if="startProtectionLocked || targetProtectionNotice" class="field-hint" role="status" aria-live="polite">{{ targetProtectionNotice || '手柄切换处理中，请稍候。' }}</p>
<div class="controller-preview-row"><div ref="controllerPreviewHost" class="controller-preview-host" aria-label="现有手柄实时预览"/><ControllerChannelStatus :status="virtualOutputStatus" :gyro-supported="personaCapability.gyro" :gyro-enabled="shortcutRuntime?.gyroEnabled ?? gyroEnabled" :back-buttons="personaCapability.backButtons" :physical-gyro-supported="physicalGyroSupported" :physical-back-buttons="physicalBackButtons" :physical-back-buttons-captured="physicalBackButtonsCaptured" :usage="oemUsage" :virtual-enabled="displayTargetEnabled" :persona="displayPersona" :observed-keys="observedOemKeys"/></div>
      </div>
      <p v-if="virtualTargetPolicy?.steamDeckSelfOutputBlocked" role="status" class="field-hint">{{ STEAM_DECK_SELF_OUTPUT_MESSAGE }}</p>
      <!-- 背键映射（2026-09-24 operator 裁决：去掉开关、改为**常开**。
           输出人格支持背键位即生效（native 侧同日常开）；真 Deck 物理机仍不挂。
           2026-09-27 用户裁决：提示小字不再显示。
           2026-09-30 用户裁决：气泡改为如实反馈当前人格能力（陀螺仪 / 背键数量）。 -->




    </section>
    <ControllerShortcutsCard :preview-target="controllerPreviewHost" :persona="displayPersona" :steam-deck-active="displayTargetEnabled && displayPersona === 'steamdeck'" @open-editor="openShortcutEditor" />
</div>
<section v-if="controllerPage==='touchpad'" class="card controller-touchpads-card" aria-label="屏幕触控板设置"><ScreenTouchpadsSettings :key="displayPersona" :persona="displayPersona"
        :steam-deck-enabled="displayTargetEnabled && displayPersona === 'steamdeck'"
        :ps5-enabled="displayTargetEnabled && displayPersona === 'dualsense-edge'" :disabled="gameLocked || targetBusy" /></section>
    <ControllerCalibrationCard v-if="controllerPage==='calibration'" :disabled="targetBusy || startProtectionLocked || profileRemovalBusy" />
    <ControllerFeedbackCapabilitiesCard v-if="controllerPage==='feedback'" />
    </div>
    <ControllerShortcutEditorView v-if="editorOpen" :persona="displayPersona" @close="closeShortcutEditor" />
    <GameProfileRemoveDialog v-if="profileRemoval" :game-name="profileRemoval.gameName" :busy="profileRemovalBusy" :error="profileRemovalError" @cancel="cancelProfileRemoval" @confirm="confirmProfileRemoval" />
  </div>

  </IntegratedPage>
</template>

<style scoped>
.page{padding-bottom:20px}.card-head{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}.card-title{margin:0}
.card-head-actions{display:flex;align-items:center;gap:8px}
.policy-accessible-status{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip-path:inset(50%);white-space:nowrap;border:0}
/* 输出控制器独立按钮行（2026-09-16 用户裁决：参考性能调度 .tool-actions 样式，
   按钮直接落在卡片上以获得对比与独立外轮廓；不再用 SegButton/bubble 包裹）。 */
.persona-block{margin-top:12px}
.game-override-notice{margin:0 0 8px;color:var(--accent);font-size:11px;font-weight:700}
.persona-actions{display:flex;align-items:center;gap:7px}
.persona-actions .persona-btn{flex:1 1 0;min-width:0;min-height:38px;padding:0 8px;border:1px solid rgba(255,255,255,.06);border-radius:8px;background:var(--bg-input);color:var(--text);font-size:11px;font-weight:700;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;white-space:nowrap;transition:background .12s,color .12s,border-color .12s}
.persona-actions .persona-btn:hover:not(:disabled){color:var(--text)}
.persona-actions .persona-btn.active{color:var(--accent);border-color:color-mix(in srgb,var(--accent) 45%,transparent);background:color-mix(in srgb,var(--accent) 10%,var(--bg-input))}
.persona-actions .persona-btn:disabled{opacity:.42;cursor:default}.fault-line{display:grid;gap:3px;margin:-2px 0 9px;padding:8px 9px;border-radius:var(--radius-ctrl)}.fault-line{border:1px solid rgba(244,163,61,.45);background:color-mix(in srgb,#f4a33d 10%,transparent)}.fault-line strong{color:#ffc46b;font-size:11px}.fault-line span{color:var(--text-dim);font-size:10px;line-height:1.45}.bubble-label{display:block;margin:0 0 8px;color:var(--text-dim);font-size:11px;font-weight:700}/* 能力反馈行（2026-09-30）：替换原来的「背键映射常开」说明文字。 */
.environment-note{display:grid;gap:3px;margin:-2px 0 9px;padding:8px 9px;border:1px solid rgba(255,255,255,.08);border-radius:var(--radius-ctrl);background:var(--bg-input)}.environment-note strong{color:var(--text);font-size:11px}.environment-note span{color:var(--text-dim);font-size:10px;line-height:1.45}
.controller-mouse-active{display:inline-flex;align-items:center;gap:5px;color:var(--accent);font-size:10px;white-space:nowrap;}
</style>

<style>
.controller-page .controller-preview-row{display:grid;grid-template-columns:minmax(0,1fr);gap:8px;margin:10px 0 6px;min-width:0}.controller-page .controller-preview-host{min-width:0}.controller-page .controller-preview-host .viz{margin-top:0}.controller-page .controller-preview-host .test-toggle{margin-top:8px}.controller-page .controller-preview-host .test-toggle .toggle-desc{font-size:12px}.controller-page .controller-touchpads-card .screen-touchpads-bubble{margin-top:0}
</style>


<style>
.controller-page .controller-unified-header{display:grid;grid-template-columns:minmax(0,1fr) auto;align-items:center;gap:8px;margin:0;padding:3px 1px 0;min-width:0}.controller-page .controller-unified-header>.controller-page-title{margin:0!important;padding:0!important;border:0!important;display:flex;align-items:center;gap:7px;margin:0;font-size:20px;font-weight:650;line-height:1.2;letter-spacing:.2px;white-space:nowrap}.controller-page .controller-unified-header>.controller-page-title::after{display:none!important}.controller-page .controller-page-title>svg{width:20px;height:20px;flex:0 0 auto;color:var(--accent)}.controller-page .controller-unified-header>.toggle-row{padding:0!important;min-height:36px;white-space:nowrap}.controller-page .controller-unified-header .toggle-label{font-size:13px}.controller-page .controller-unified-header .policy-accessible-status{position:absolute;width:1px;height:1px;clip:rect(0 0 0 0);overflow:hidden;white-space:nowrap}
</style>

<style scoped>
.controller-profile-banner{margin:0;min-width:0}
.controller-profile-exit{min-height:34px;padding:5px 12px;border:1px solid color-mix(in srgb,var(--danger) 50%,transparent);border-radius:7px;background:color-mix(in srgb,var(--danger) 12%,var(--bg-input));color:var(--danger);font:inherit;font-size:13px;font-weight:650;white-space:nowrap;cursor:pointer}
.controller-profile-exit:disabled{opacity:.5}
.controller-profile-note{margin:0;color:var(--danger);font-size:13px;line-height:1.5;overflow-wrap:anywhere}
</style>

<style>
.controller-paged{--cp-line:color-mix(in srgb,var(--accent) 20%,rgba(138,157,180,.18));--cp-soft:color-mix(in srgb,var(--accent) 5%,rgba(12,20,30,.72));gap:10px!important;min-width:0;padding-bottom:10px!important}
.controller-paged .controller-pages-workspace,.controller-paged .controller-page-content,.controller-paged .controller-general-panel{min-width:0}
.controller-paged .controller-general-panel{display:grid;gap:10px}
.controller-paged .controller-page-tabs{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;margin:0 0 10px;min-width:0}
.controller-paged .controller-page-tabs button{display:flex;align-items:center;justify-content:center;gap:6px;height:39px;min-width:0;padding:6px 5px;border:1px solid var(--cp-line);border-radius:9px;background:var(--console-surface-alt);color:var(--text-dim);font:inherit;font-size:14px;font-weight:600;cursor:pointer;white-space:nowrap}
.controller-paged .controller-page-tabs button svg{width:17px;height:17px;flex:0 0 auto}
.controller-paged .controller-page-tabs button[aria-pressed=true]{color:var(--accent);background:var(--accent-surface);border-color:var(--accent);box-shadow:inset 0 1px 0 rgba(255,255,255,.05)}
.controller-paged .controller-page-tabs button.focused,.controller-paged .controller-page-tabs button:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.controller-paged .controller-current-persona{display:inline-flex;align-items:center;justify-self:end;gap:6px;min-height:30px;padding:4px 8px;border:1px solid var(--cp-line);border-radius:7px;background:var(--console-surface-alt);font-size:13px;color:var(--text);white-space:nowrap}
.controller-paged .controller-current-persona svg{width:15px;height:15px;color:var(--accent)}
.controller-paged .persona-readonly-dot{width:5px;height:5px;border-radius:50%;background:var(--accent);margin-left:2px;opacity:.8}
.controller-paged .controller-profile-banner{font-size:13px;padding:7px 8px;gap:7px;border-radius:8px;margin:0!important}
.controller-paged .controller-profile-exit{min-height:30px;padding:4px 8px;font-size:12px}
.controller-paged .controller-touchpads-card{padding:12px!important;margin:0!important;border:1px solid var(--cp-line)!important;border-radius:12px!important;background:var(--console-surface)!important}
.controller-paged .touchpad-master{display:flex;align-items:center;justify-content:space-between;gap:9px;padding:0 0 11px;margin-bottom:10px;border-bottom:1px solid var(--cp-line)}
.controller-paged .touchpad-master-copy{display:grid;gap:3px;min-width:0}
.controller-paged .touchpad-master strong{font-size:15px;color:var(--text);font-weight:650;line-height:1.4}
.controller-paged .touchpad-master-copy>span{font-size:12px;color:var(--text-dim);line-height:1.4}
.controller-paged .touchpad-master .toggle-row{padding:0!important;min-height:32px}
.controller-paged .screen-touchpads-bubble{border:0;background:transparent;padding:0;margin:0;gap:9px 10px}
.controller-paged .screen-touchpad-field{gap:5px}
.controller-paged .screen-touchpad-field label{font-size:13px;line-height:17px;letter-spacing:0;color:var(--text-dim)}
.controller-paged .screen-touchpad-field .dd-trigger{height:34px;min-height:34px;padding:0 8px;font-size:13px;border-radius:7px}
.controller-paged .screen-control-selects{gap:9px 10px}
.controller-paged .screen-touchpad-sliders{gap:8px 10px;padding-top:8px;margin-top:2px;border-color:var(--cp-line)}
.controller-paged .screen-touchpad-sliders .slider{padding:2px 0!important}
.controller-paged .screen-touchpad-sliders .slider-label,.controller-paged .screen-touchpad-sliders .slider-val{font-size:13px}
.controller-paged .touchpad-memory-note{display:flex;align-items:center;gap:5px;margin:10px 0 0;font-size:11px;color:var(--text-dim);line-height:1.4}
.controller-paged .touchpad-memory-note>span{color:var(--accent);font-size:8px}
.controller-paged .touchpad-disabled{opacity:.45}
.controller-paged .feedback-card{padding:12px!important;margin:0!important;border:1px solid var(--cp-line)!important;border-radius:12px!important;background:var(--console-surface)!important}
.controller-paged .feedback-card>.card-head{display:none}
.controller-paged .feedback-grid{gap:10px;margin:0}
.controller-paged .feedback-bubble{padding:10px;border-color:var(--cp-line);border-radius:9px;background:var(--console-surface-alt)}
.controller-paged .feedback-title-row strong{font-size:15px}
.controller-paged .availability-chip{font-size:11px;min-height:22px;padding:0 7px}
.controller-paged .feedback-card .rumble-row{margin-top:9px;gap:9px}
.controller-paged .feedback-card .slider-label,.controller-paged .feedback-card .slider-val{font-size:13px}
.controller-paged .feedback-card .vibrate-button{height:33px;min-height:33px;font-size:13px;padding:0 10px}
.controller-paged .feedback-card .light-switch{margin-top:8px;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.controller-paged .feedback-card .light-switch .toggle-row{min-height:33px;padding:0!important;gap:5px}
.controller-paged .feedback-card .light-switch .toggle-label{font-size:13px}
.controller-paged .feedback-card .light-bubble> .toggle-row{margin-top:2px}
.controller-paged .feedback-card .field-label{font-size:13px;margin-bottom:5px}
.controller-paged .feedback-card .setting-field{margin-top:8px}
.controller-paged .feedback-card .light-bubble{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));column-gap:10px}
.controller-paged .feedback-card .light-bubble>.feedback-title-row,.controller-paged .feedback-card .light-bubble>.light-switch,.controller-paged .feedback-card .light-bubble>.toggle-row,.controller-paged .feedback-card .light-bubble>.light-slider-grid,.controller-paged .feedback-card .light-bubble>.unavailable-hint{grid-column:1/-1}
.controller-paged .feedback-card .effect-field{grid-column:1/-1}
.controller-paged .feedback-card .light-bubble .dd-trigger{min-height:33px;font-size:13px}
.controller-paged .feedback-card .light-slider-grid{margin-top:9px}

.app-root .controller-paged .feedback-card .light-bubble>.light-switch{grid-column:auto;display:block;min-width:0}
.app-root .controller-paged .feedback-card .light-switch>.toggle-row{display:flex;align-items:center;justify-content:space-between;gap:6px;width:100%}
.controller-paged .controller-general-panel[style*="display: none"]{display:none!important}
</style>
