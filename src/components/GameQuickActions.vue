<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import OverflowGameName from './OverflowGameName.vue';
import AppIcon from '@/components/AppIcon.vue';
import GameTrainerMenu from '@/components/GameTrainerMenu.vue';
import { OPTI_OFFLINE_GUIDE, optiAntiCheatWarning, optiMayOfferLaunch } from '@/bridge/optiImportPolicy';
import GameCustomProfilePanel from '@/components/GameCustomProfilePanel.vue';
import GameRulePanel from '@/components/GameRulePanel.vue';
import Dropdown from '@/components/Dropdown.vue';
import SwitchControl from '@/components/SwitchControl.vue';
import {losslessProgramLabel,type LosslessProgramState} from '@/bridge/losslessProgramState';
import {retainLosslessObservation,readLosslessProgram} from '@/bridge/losslessRuntimeState';
import { focusGamepadElement, getGamepadPopupPlacement } from '@/gamepad/focus';
import { getCurrentGame, isCurrentGameTarget, refreshGameStatusStrict, validateGameTarget, detectedGameName, type DetectedGame } from '@/bridge/gamedetect';
import { GameTrainerCancelledError, openOrSearchGameTrainer, cancelGameTrainerPreparation, getReadyGameTrainer, getGameTrainerState, subscribeGameTrainerState } from '@/bridge/gameTrainer';
import {
  getLosslessGameState, setLosslessScalingEnabled, onLosslessScalingChanged, type LosslessGameState,
  oneClickOptiScaler,
  type OptiBackend,
  type OptiAction,
  optiscalerAnalyze,
  optiConsoleInstalled,
  openOptiConsole,
  OPTISCALER_CLIENT_DIR,
  OPTISCALER_CLIENT_EXE,
  OPTISCALER_CLIENT_URL,
} from '@/bridge/quickapp';
import { SPEED_PRESETS, applyGameSpeed, clearGameSpeed, getGameSpeedState, isMinecraftTarget } from '@/bridge/speedhack';
import {
  closeGame,
  hasSuspendedState,
  QUICKAPP_SUSPENDED_EVENT,
  resumeGame,
  suspendGame,
  toggleMouseMode,
  getMouseModeState,
  waitForProcessExit,
} from '@/bridge/gameproc';
import {
  getLockedGameTarget,
  isGameTargetSame,
  captureAndLockGameTarget,
  unlockGameTarget,
  type LockedGameTarget,
} from '@/bridge/gameQuickSession';
import { dialog, proc, shell, windowApi } from '@/bridge/api';
import { subscribePolicyGameStatus, getPolicyGame } from '@/bridge/gamePolicyTarget';
import { tryAcquireQuickAction } from '@/bridge/quickActionLock';
import { isMouseModeSuppressed } from '@/gamepad/engine';
import { readActivationEvidence } from '@/gamepad/activationEvidence';

const props = defineProps<{ game: DetectedGame | null; open: boolean }>();
const emit = defineEmits<{
  (e: 'status', value: { message?: string; error?: string }): void;
  (e: 'close-request'): void;
}>();

const locked = ref<LockedGameTarget | null>(getLockedGameTarget());
const busy = ref(false);
const trainerBusy = ref(false);
const trainerMenuOpen = ref(false);
const trainerCancelRequested = ref(false);
const trainerStateRevision = ref(0);
let stopTrainerState: (() => void) | null = null;
const speedBusy = ref(false);
const pauseBusy = ref(false);
const closeBusy = ref(false);
const mouseBusy = ref(false);
const taskViewBusy = ref(false);
const losslessState = ref<LosslessGameState | null>(null);
const losslessReading = ref(false);
let losslessReadEpoch = 0;
let menuStateEpoch = 0;
let controlStateEpoch = 0;
let targetEpoch = 0;
let disposed = false;
let trainerOperationTarget: LockedGameTarget | null = null;
let fsrTargetGuard: { target: LockedGameTarget; allowExit: boolean; invalidated: boolean } | null = null;
let stopLosslessState: (() => void) | null = null;
let stopLosslessObservation:(()=>void)|null=null;
const losslessProgram=ref<LosslessProgramState|null>(null);
const losslessHint=computed(()=>losslessProgramLabel(losslessProgram.value));
let taskViewActionSequence = 0;
const paused = ref(false);
const mouseOn = ref(false);
const mousePolicy = ref<'auto'|'on'|'off'>('off');
const mouseScene = ref('unknown');
const mouseControlAllowed = ref(false);
const mouseVerification = ref('unobserved');
const mouseProvider = ref<'ymcc'|'steam'>('ymcc');
let mouseStateEpoch = 0;
const mouseNotice = ref('');
const statusMsg = ref('');
const errMsg = ref('');
const speedFactor = ref(1);
// 2026-09-30 用户裁决：当前输出人格决定鼠标从哪来——SteamDeck 虚拟手柄时鼠标由
// Steam 接管，这一行要如实显示「Steam鼠标」而不是笼统的「模拟鼠标」。
const padPersona = ref('disabled');
const steamDeckPadActive = computed(() => padPersona.value === 'steamdeck');
const mouseModeLabel = computed(() => (steamDeckPadActive.value || mouseProvider.value === 'steam' ? 'Steam鼠标' : '模拟鼠标'));
const mouseStateLabel = computed(() => steamDeckPadActive.value ? 'Steam管理'
  : !mouseControlAllowed.value ? '状态待确认'
  : mouseProvider.value === 'steam' ? (mouseOn.value ? '开启 · Steam桌面布局' : '关闭')
  : mousePolicy.value === 'auto' ? (mouseScene.value === 'ymcc' ? '自动 · 已暂停' : mouseOn.value ? '自动 · 鼠标' : '自动 · 手柄')
  : mouseOn.value ? (mouseVerification.value === 'process-liveness' ? '开启 · 映射待确认' : '开启 · 已开启') : '关闭');
const rulesOpen = ref(false);
const speedOptions = SPEED_PRESETS.map((value) => ({ value, label: `${value}×` }));

const fsrDialogOpen = ref(false);
const fsrDialogTitle = ref('');
const fsrDialogDescription = ref('');
const fsrDialogConfirmLabel = ref('确认');
const fsrDialogCancelLabel = ref('取消');
const fsrDialogAbove = ref(false);
const fsrDialogStyle = ref<Record<string, string>>({ position: 'fixed' });
const fsrDialogConfirmEl = ref<HTMLElement | null>(null);
const fsrDialogCancelEl = ref<HTMLElement | null>(null);
const fsrDialogPanelEl = ref<HTMLElement | null>(null);
const fsrDialogMode = ref<'confirm' | 'backend'>('confirm');
const fsrBackendChoice = ref<OptiAction>('fsr');
const OPTI_BACKEND_CHOICES: OptiBackend[] = ['fsr', 'xess'];
let fsrResolve: ((value: boolean | OptiAction | null) => void) | null = null;

// 打开前尝试同步暂存登记；不关闭客户端或游戏进程。
async function onOpenOptiConsole(): Promise<void> {
  status('');
  try {
    if (!(await optiConsoleInstalled())) {
      const goDownload = await openDialog(
        '未找到 OPT 客户端',
        `未检测到 ${OPTISCALER_CLIENT_EXE}。\n请前往 GitHub 下载 OptiscalerClient，并把文件复制到 ${OPTISCALER_CLIENT_DIR}\\ 后重试。`,
        '打开下载页',
        '知道了',
      );
      if (goDownload) {
        await shell.open(OPTISCALER_CLIENT_URL).catch((error) => {
          status('', `打开下载页失败：${(error as Error).message}`);
        });
      }
      return;
    }
    const notice = await openOptiConsole();
    status(`已打开 OPT 客户端${notice ? `\n${notice}` : ''}`);
  } catch (error) {
    status('', `OPT 客户端打开失败：${(error as Error).message}`);
  }
}

const policyGame = ref<DetectedGame | null>(getPolicyGame());
let stopPolicyGame: (() => void) | null = null;
// The lock is an operation credential, never a second source of game identity.
const targetGame = computed(() => policyGame.value);
const trainerButtonLabel = computed(() => {
  void trainerStateRevision.value;
  const game = targetGame.value;
  return game && getReadyGameTrainer(detectedGameName(game), game.path, game) ? '已发现修改器' : '游戏修改器';
});
const trainerButtonHint = computed(() => {
  void trainerStateRevision.value;
  if (trainerBusy.value) return '准备并启动中…点击取消';
  const state = getGameTrainerState(detectedGameName(targetGame.value), targetGame.value?.path || '');
  if (state?.state === 'ready') return trainerButtonLabel.value === '已发现修改器' ? '已就绪 · 点击启动' : '身份待确认 · 打开管理器核对';
  if (state?.state === 'downloading') return '后台下载中 · 点击等待启动';
  if (state?.state === 'queued' || state?.state === 'searching') return '后台准备中 · 点击等待启动';
  if (state?.state === 'budget-limited') return '下载额度已用完 · 已有文件可启动';
  if (state?.state === 'failed') return '准备失败 · 点击重试';
  if (state?.state === 'unavailable') return '暂无匹配资源 · 点击重试';
  return '等待游戏启动';
});
const targetName = computed(() => targetGame.value
  ? (detectedGameName(targetGame.value) || targetGame.value.name)
  : '未识别游戏');
// 修改器是独立的异步任务：它处理中只锁住自身按钮，不能阻塞游戏菜单
// 的其它功能（暂停、变速、FSR、黑白名单、专属配置等）。
const disabledForAction = computed(() => busy.value || speedBusy.value || pauseBusy.value || closeBusy.value || mouseBusy.value || taskViewBusy.value);

function status(message = '', error = ''): void {
  statusMsg.value = message;
  errMsg.value = error;
  emit('status', { message, error });
}

function sameTarget(
  a: Pick<DetectedGame, 'pid' | 'processCreated'> & Partial<DetectedGame> | null,
  b: Pick<DetectedGame, 'pid' | 'processCreated'> & Partial<DetectedGame> | null,
): boolean {
  return isGameTargetSame(a, b);
}

function isTargetCurrent(target: DetectedGame | null): boolean {
  return !disposed && (target
    ? isCurrentGameTarget(target) && sameTarget(targetGame.value, target)
    : getCurrentGame() === null && targetGame.value === null);
}

function isActionTargetCurrent(target: LockedGameTarget): boolean {
  return isTargetCurrent(target) && getLockedGameTarget()?.lockGeneration === target.lockGeneration;
}

function assertActionTarget(target: LockedGameTarget): void {
  if (!isActionTargetCurrent(target)) throw new Error('游戏目标已变化，已停止后续步骤');
}

function releaseTarget(target: LockedGameTarget): void {
  if (unlockGameTarget(target.lockGeneration) && locked.value?.lockGeneration === target.lockGeneration) locked.value = null;
}

function fileNameFromPath(path: string): string {
  const parts = path.trim().split(/[\\/]/);
  return parts[parts.length - 1] || path.trim();
}

async function ensureTarget(): Promise<LockedGameTarget> {
  const epoch = targetEpoch;
  const candidate = targetGame.value;
  if (!candidate || !isTargetCurrent(candidate)) throw new Error('当前没有识别到游戏或目标已变化');
  const existing = getLockedGameTarget();
  const checked = await captureAndLockGameTarget(candidate, 'action');
  if (epoch !== targetEpoch || !props.open || !isTargetCurrent(candidate)) {
    if (!existing && checked) releaseTarget(checked);
    throw new Error('游戏目标已变化，请重试');
  }
  if (!checked) {
    if (existing && getLockedGameTarget()?.lockGeneration === existing.lockGeneration) {
      throw new Error('锁定游戏校验失败，请稍后重试');
    }
    throw new Error('游戏已退出或快捷会话已变化，请重新获取游戏');
  }
  assertActionTarget(checked);
  locked.value = checked;
  return checked;
}

async function refreshGame(): Promise<void> {
  if (disabledForAction.value) return;
  const epoch = ++menuStateEpoch;
  status();
  try {
    await refreshGameStatusStrict();
    if (epoch !== menuStateEpoch || disposed) return;
    const current = getCurrentGame();
    locked.value = getLockedGameTarget();
    await refreshSpeedState(current);
    await refreshControlState(current);
    if (epoch !== menuStateEpoch || !isTargetCurrent(current)) return;
    status(current ? '已刷新游戏识别' : '刷新完成，当前未识别游戏');
  } catch (error) {
    if (epoch === menuStateEpoch && !disposed) status('', '刷新游戏识别失败：' + (error as Error).message);
  }
}

async function refreshSpeedState(target: DetectedGame | null): Promise<void> {
  if (!isTargetCurrent(target)) return;
  const state = getGameSpeedState();
  speedFactor.value = target && state && sameTarget(state, target) && state.factor > 0 ? state.factor : 1;
}

async function refreshControlState(target: DetectedGame | null): Promise<void> {
  const epoch = ++controlStateEpoch;
  await refreshPadPersona();
  if (epoch !== controlStateEpoch || !isTargetCurrent(target)) return;
  if (!target) {
    paused.value = false;
    return;
  }
  try {
    const state = await hasSuspendedState(target.pid);
    if (epoch === controlStateEpoch && isTargetCurrent(target)) paused.value = state.suspended;
  } catch { /* Transport failure does not prove that this game resumed. */ }
}

// 顶部菜单要如实反映当前输出人格（专属配置的覆盖也走同一份设置），
// 读取失败保持上一次的值，不用错误值覆盖显示。
async function refreshPadPersona(): Promise<void> {
  const epoch = ++mouseStateEpoch;
  mouseControlAllowed.value = false;
  try {
    const state = await getMouseModeState();
    if (epoch !== mouseStateEpoch || disposed) return;
    mouseProvider.value = state.provider === 'steam' ? 'steam' : 'ymcc';
    padPersona.value = state.provider === 'steam' ? 'steamdeck' : 'disabled';
    mouseOn.value = state.on;
    mousePolicy.value = state.mode || 'off';
    mouseScene.value = state.scene || 'unknown';
    mouseVerification.value = state.verification || 'unobserved';
    mouseControlAllowed.value = state.controlAllowed === true && state.stateKnown === true && !steamDeckPadActive.value;
  } catch { /* 保持显示，状态未确认时不开放控制。 */ }
}

function onSuspendedStateSync(): void {
  // This legacy event has no PID/birth. Reread the current target instead of
  // assigning an old game's suspended bit to the new game's button.
  if (props.open) void refreshControlState(targetGame.value);
}

function onMouseModeSync(e: Event): void {
  const detail = (e as CustomEvent<{ on?: boolean; verification?: string; controlAllowed?: boolean; stateKnown?: boolean; provider?: string; mode?: 'auto'|'on'|'off'; scene?: string }>).detail;
  if (!detail) return;
  ++mouseStateEpoch;
  if (detail.provider === 'steam' || detail.provider === 'ymcc') mouseProvider.value = detail.provider;
  mouseOn.value = Boolean(detail.on);
  if (detail.mode) mousePolicy.value = detail.mode;
  if (detail.scene) mouseScene.value = detail.scene;
  mouseVerification.value = detail.verification || 'unobserved';
  if (typeof detail.controlAllowed === 'boolean') mouseControlAllowed.value = detail.controlAllowed && detail.stateKnown !== false;
  if (detail.provider === 'steam') { padPersona.value = 'steamdeck'; mouseControlAllowed.value = false; }
}

// 覆盖下发/恢复后 native 侧真正生效的人格由 gameInputOverride 广播（含游戏退出恢复）。
function onPadPersonaSync(e: Event): void {
  const persona = (e as CustomEvent<{ persona?: string }>).detail?.persona;
  if (typeof persona === 'string' && persona) {
    padPersona.value = persona;
    ++mouseStateEpoch;
    mouseControlAllowed.value = false;
    void refreshPadPersona();
  }
}

async function togglePause(): Promise<void> {
  if (pauseBusy.value) return;
  const epoch = targetEpoch;
  let target: LockedGameTarget | null = null;
  pauseBusy.value = true;
  ++controlStateEpoch;
  try {
    target = await ensureTarget();
    assertActionTarget(target);
    if (paused.value) {
      const result = await resumeGame();
      assertActionTarget(target);
      ++controlStateEpoch;
      paused.value = result.failCount > 0;
      status(result.okCount > 0 ? '已继续 ' + result.okCount + ' 个被冻结的进程' : (result.ok ? '游戏已继续' : '继续失败：' + (result.msgs.join('；') || '没有待恢复的进程')));
    } else {
      const result = await suspendGame(target.pid, target.name || target.title || '当前游戏', target.processCreated);
      assertActionTarget(target);
      ++controlStateEpoch;
      paused.value = result.ok;
      status(result.ok ? '已暂停 ' + (target.name || target.title || '当前游戏') : '暂停失败：' + (result.msgs.join('；') || '没有可暂停的游戏进程'));
    }
  } catch (error) {
    if (epoch === targetEpoch && !disposed && (!target || isActionTargetCurrent(target))) status('', '暂停/继续失败：' + (error as Error).message);
  } finally {
    pauseBusy.value = false;
  }
}

async function closeCurrentGame(): Promise<void> {
  if (closeBusy.value) return;
  const epoch = targetEpoch;
  let target: LockedGameTarget | null = null;
  closeBusy.value = true;
  try {
    target = await ensureTarget();
    assertActionTarget(target);
    const result = await closeGame(target.pid, target.name, target.processCreated);
    if (!result.ok) throw new Error(result.msgs.join('；') || '关闭失败');
    if (isActionTargetCurrent(target)) status('已关闭 ' + target.name);
    releaseTarget(target);
    // Exit is published by the sole owner; never publish a local null target.
    await refreshGameStatusStrict();
  } catch (error) {
    if (epoch === targetEpoch && !disposed && (!target || isTargetCurrent(target))) status('', '关闭游戏失败：' + (error as Error).message);
  } finally {
    closeBusy.value = false;
  }
}

// 系统 Shell 切换请求有独立超时；只有请求被接受后才收起 YMCC。
// 两次回执共享诊断关联号，不将 API 返回当成窗口已经切换完成。
async function openTaskView(event?: MouseEvent): Promise<void> {
  if (taskViewBusy.value) return;
  const activationEvidence = readActivationEvidence(event);
  const previousFocus = document.activeElement instanceof HTMLElement ? document.activeElement : null;
  const restoreControllerFocus = previousFocus?.matches('[data-gp-game-control="switch-program"]') &&
    previousFocus.classList.contains('focused');
  let handoverAccepted = false;
  taskViewBusy.value = true;
  try {
    const evidence = {
      diagnosticAction: 'game-recognition-switch-program' as const,
      diagnosticActionId: `switch-program:${Date.now().toString(36)}:${(++taskViewActionSequence).toString(36)}`,
      requestedAtUnixMs: Date.now(),
      ...activationEvidence,
    };
    await shell.taskViewWithEvidence(evidence);
    // 系统只回执请求是否接受；收起窗口的回执失败需要单独提示。
    try {
      const minimized = await windowApi.minimizeWithEvidence(evidence);
      if (!minimized) throw new Error('窗口未接受收起请求');
      handoverAccepted = true;
    } catch (error) {
      status('', `已请求切换程序，但 YMCC 未能收起：${(error as Error).message}`);
      return;
    }
    status('已请求切换程序');
  } catch (error) {
    status('', `切换 Windows 任务视图失败：${(error as Error).message}`);
  } finally {
    taskViewBusy.value = false;
    if (!handoverAccepted && restoreControllerFocus && previousFocus) {
      // A disabled focused button can leave DOM focus on body while its gamepad
      // highlight survives. Restore only this failed action's empty DOM focus;
      // never reclaim a hidden page, another window, or a newly selected control.
      void nextTick(() => {
        if (document.visibilityState !== 'visible' || !document.hasFocus() ||
          (document.activeElement !== document.body && document.activeElement !== document.documentElement) ||
          !previousFocus.isConnected || !previousFocus.classList.contains('focused')) return;
        focusGamepadElement(previousFocus);
      });
    }
  }
}

async function toggleMouse(): Promise<void> {
  if (mouseBusy.value || steamDeckPadActive.value || !mouseControlAllowed.value) return;
  const epoch = targetEpoch;
  mouseBusy.value = true;
  mouseNotice.value = '';
  try {
    const result = await toggleMouseMode();
    if (disposed || epoch !== targetEpoch) {
      if (!disposed) void refreshPadPersona();
      return;
    }
    if (!result.ok) {
      if (result.provider === 'steam') { padPersona.value = 'steamdeck'; mouseControlAllowed.value = false; }
      throw new Error(result.error || (mouseModeLabel.value + '切换失败'));
    }
    mouseOn.value = result.on;
    mousePolicy.value = result.mode || 'off';
    mouseScene.value = result.scene || 'unknown';
    mouseVerification.value = result.verification || 'unobserved';
    window.dispatchEvent(new CustomEvent('gp:mouse-mode', { detail: { ...result } }));
    status(result.provider === 'steam' ? (result.on ? 'Steam 桌面鼠标已开启' : 'Steam 桌面鼠标已关闭') : result.pending ? '正在切换模拟鼠标挡位' : result.mode === 'auto' ? '自动挡：YMCC 内暂停，最小化后按前台恢复' : result.on && result.verification === 'process-liveness' ? 'JoyXoff 已启动，映射待确认' : result.on ? `${mouseModeLabel.value}已开启` : `${mouseModeLabel.value}已关闭`);
  } catch (error) {
    if (disposed || epoch !== targetEpoch) return;
    mouseNotice.value = (error as Error).message;
    status('', `${mouseModeLabel.value}切换失败：${(error as Error).message}`);
    void refreshPadPersona();
  } finally {
    mouseBusy.value = false;
  }
}

async function refreshMenuState(): Promise<void> {
  if (!props.open || disabledForAction.value) return;
  const epoch = ++menuStateEpoch;
  const target = getCurrentGame();
  locked.value = getLockedGameTarget();
  await refreshSpeedState(target);
  if (epoch !== menuStateEpoch || !props.open || disposed) return;
  await refreshControlState(target);
}

function syncCurrentTarget(next: DetectedGame | null): void {
  next = getCurrentGame();
  const previous = policyGame.value;
  policyGame.value = next;
  const session = getLockedGameTarget();
  if (session && !sameTarget(session, next)) releaseTarget(session);
  locked.value = getLockedGameTarget();
  if (fsrTargetGuard && !sameTarget(fsrTargetGuard.target, next) && !(fsrTargetGuard.allowExit && !next)) {
    fsrTargetGuard.invalidated = true;
  }
  if ((!previous && !next) || sameTarget(previous, next)) return;
  ++targetEpoch;
  ++menuStateEpoch;
  ++controlStateEpoch;
  ++losslessReadEpoch;
  trainerCancelRequested.value = true;
  trainerMenuOpen.value = false;
  rulesOpen.value = false;
  paused.value = false;
  speedFactor.value = 1;
  losslessState.value = null;
  losslessReading.value = false;
  if (fsrResolve) closeDialog(false);
  status();
  if (props.open) {
    void refreshSpeedState(next);
    void refreshControlState(next);
  }
}

function onTargetLost(event: Event): void {
  const detail = (event as CustomEvent<{ target?: LockedGameTarget; lockGeneration?: number }>).detail;
  const lost = detail?.target;
  if (!lost || detail.lockGeneration !== lost.lockGeneration) return;
  const session = getLockedGameTarget();
  // A delayed A event must not unlock B or a newly captured A session.
  if (session && session.lockGeneration !== lost.lockGeneration) return;
  if (sameTarget(getCurrentGame(), lost)) return;
  releaseTarget(lost);
  syncCurrentTarget(getCurrentGame());
}

function closeTrainerMenu(): void {
  trainerMenuOpen.value = false;
  if (trainerBusy.value) { trainerCancelRequested.value = true; status('已取消等待启动，已下载文件会保留'); }
}

async function runTrainer(): Promise<void> {
  if (trainerBusy.value) {
    trainerCancelRequested.value = true;
    const target = trainerOperationTarget;
    if (target) await cancelGameTrainerPreparation(detectedGameName(target), target.path).catch(() => undefined);
    if (target && isActionTargetCurrent(target)) status('已取消修改器启动');
    return;
  }
  const release = tryAcquireQuickAction('top-trainer');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  const epoch = targetEpoch;
  let target: LockedGameTarget | null = null;
  trainerBusy.value = true;
  trainerCancelRequested.value = false;
  status('正在准备游戏修改器…');
  try {
    target = await ensureTarget();
    trainerOperationTarget = target;
    const captured = target;
    const name = detectedGameName(target);
    if (!name) throw new Error('未识别到真实游戏名，无法搜索修改器');
    const result = await openOrSearchGameTrainer(name, (progress) => {
      if (progress.message && !trainerCancelRequested.value && isActionTargetCurrent(captured)) status(progress.message);
    }, target.path, {
      processTarget: { pid: target.pid, processCreated: target.processCreated },
      isCancelled: () => trainerCancelRequested.value || !isActionTargetCurrent(captured),
      isTargetCurrent: async () => {
        if (!isActionTargetCurrent(captured)) return false;
        const current = await validateGameTarget(captured);
        return !!current && sameTarget(current, captured) && isActionTargetCurrent(captured);
      },
    });
    if (!trainerCancelRequested.value && isActionTargetCurrent(target)) {
      status(result.action === 'opened' ? '已打开游戏修改器' : '修改器搜索已完成');
    }
  } catch (error) {
    if (epoch === targetEpoch && !disposed && !(error instanceof GameTrainerCancelledError) && !trainerCancelRequested.value && (!target || isActionTargetCurrent(target))) {
      status('', '游戏修改器操作失败：' + (error as Error).message);
    }
  } finally {
    trainerOperationTarget = null;
    trainerBusy.value = false;
    trainerCancelRequested.value = false;
    release();
  }
}

async function refreshLosslessState(): Promise<void> {
  const epoch = ++losslessReadEpoch;
  const game = targetGame.value;
  if (!props.open) { losslessState.value = null; losslessProgram.value=null; losslessReading.value = false; return; }
  losslessReading.value = true;
  try {
    const [program,next]=await Promise.allSettled([readLosslessProgram(),game?getLosslessGameState(game.path):Promise.resolve(null)]);
    if(epoch===losslessReadEpoch&&props.open&&!disposed&&isTargetCurrent(game)){
      losslessProgram.value=program.status==='fulfilled'?program.value:null;
      losslessState.value=next.status==='fulfilled'?next.value:null;
    }
  } catch {
    if (epoch === losslessReadEpoch) losslessState.value = null;
  } finally { if (epoch === losslessReadEpoch) losslessReading.value = false; }
}
async function runLosslessScaling(): Promise<void> {
  if (disabledForAction.value) return;
  const release = tryAcquireQuickAction('top-lossless');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  const epoch = targetEpoch;
  let target: LockedGameTarget | null = null;
  busy.value = true;
  status('正在为当前游戏一键缩放…');
  try {
    target = await ensureTarget();
    const captured = target;
    const checkpoint = () => assertActionTarget(captured);
    checkpoint();
    const result = await setLosslessScalingEnabled(target.path, true, checkpoint, target);
    checkpoint();
    status(result.notice);
  } catch (error) {
    if (epoch === targetEpoch && !disposed && (!target || isActionTargetCurrent(target))) status('', '小黄鸭一键缩放失败：' + (error as Error).message);
  } finally {
    busy.value = false;
    release();
    if (!disposed) void refreshLosslessState();
  }
}

function openDialog(title: string, description: string, confirmLabel = '确认', cancelLabel = '取消', confirmFirst = true): Promise<boolean> {
  fsrDialogMode.value = 'confirm';
  fsrDialogTitle.value = title;
  fsrDialogDescription.value = description;
  fsrDialogConfirmLabel.value = confirmLabel;
  fsrDialogCancelLabel.value = cancelLabel;
  fsrDialogOpen.value = true;
  const anchor = document.querySelector<HTMLElement>('[data-gp-game-control="fsr-import"]')?.getBoundingClientRect()
    || document.querySelector<HTMLElement>('[data-gp-game-quick-menu]')?.getBoundingClientRect() || null;
  const placement = getGamepadPopupPlacement(anchor, Math.min(420, window.innerWidth - 16), 260, 8);
  fsrDialogAbove.value = placement.above;
  fsrDialogStyle.value = placement.style;
  nextTick(() => focusGamepadElement(confirmFirst ? fsrDialogConfirmEl.value : fsrDialogCancelEl.value));
  return new Promise<boolean>((resolve) => {
    fsrResolve = (value) => resolve(value === true);
  });
}

function openBackendDialog(): Promise<OptiAction | null> {
  fsrDialogMode.value = 'backend';
  fsrBackendChoice.value = 'fsr';
  fsrDialogTitle.value = '选择 OptiScaler 方案';
  fsrDialogDescription.value = 'FSR4 / XeSS 套用本地缓存并登记到 OPT 客户端；反作弊目标须确认风险，不执行绕过。';
  fsrDialogConfirmLabel.value = '确认方案并继续';
  fsrDialogCancelLabel.value = '取消';
  fsrDialogOpen.value = true;
  const anchor = document.querySelector<HTMLElement>('[data-gp-game-control="fsr-import"]')?.getBoundingClientRect()
    || document.querySelector<HTMLElement>('[data-gp-game-quick-menu]')?.getBoundingClientRect() || null;
  const placement = getGamepadPopupPlacement(anchor, Math.min(420, window.innerWidth - 16), 300, 8);
  fsrDialogAbove.value = placement.above;
  fsrDialogStyle.value = placement.style;
  nextTick(() => focusGamepadElement(document.querySelector<HTMLElement>('[data-gp-game-quick-dialog] .quick-opti-option-btn')));
  return new Promise<OptiAction | null>((resolve) => {
    fsrResolve = (value) => resolve(value === 'fsr' || value === 'xess' || value === 'client' || value === 'uninstall' || value === 'help' ? value : null);
  });
}

function closeDialog(value: boolean | OptiAction | null): void {
  if (!fsrDialogOpen.value && !fsrResolve) return;
  fsrDialogOpen.value = false;
  const resolve = fsrResolve;
  fsrResolve = null;
  resolve?.(value);
  nextTick(() => focusGamepadElement(document.querySelector<HTMLElement>('[data-gp-game-quick-menu] button:not([data-gp-ignore]):not(:disabled)')));
}

function onGamepadBack(e: Event): void {
  if (!fsrDialogOpen.value) return;
  e.preventDefault();
  closeDialog(false);
}

async function runFsr(): Promise<void> {
  if (busy.value) return;
  const release = tryAcquireQuickAction('top-fsr');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  busy.value = true;
  status('请选择 OptiScaler 方案…');
  let target: LockedGameTarget | null = null;
  let gamePath = '';
  let selectedGameName = '';
  const epoch = targetEpoch;
  const assertFsrContext = () => {
    if (disposed) throw new Error('快捷菜单已关闭，已停止后续步骤');
    if (!target) {
      if (epoch !== targetEpoch) throw new Error('游戏目标已变化，已停止后续步骤');
      return;
    }
    const current = getCurrentGame();
    if (!fsrTargetGuard || fsrTargetGuard.invalidated ||
      (fsrTargetGuard.allowExit ? !!current && !sameTarget(current, target) : !isActionTargetCurrent(target))) {
      throw new Error('游戏目标已变化，已停止后续步骤');
    }
  };
  const canReportFsr = () => !disposed && (target
    ? !!fsrTargetGuard && !fsrTargetGuard.invalidated && (!getCurrentGame() || isCurrentGameTarget(target))
    : epoch === targetEpoch);
  try {
    const picked = await openBackendDialog();
    if (!picked) { if (epoch === targetEpoch && !disposed) status('已取消 OptiScaler 操作'); return; }
    if (picked === 'client') { await onOpenOptiConsole(); return; }
    if (picked === 'help') { await openDialog('反作弊离线使用说明', OPTI_OFFLINE_GUIDE, '知道了', '关闭'); return; }
    if (targetGame.value) {
      target = await ensureTarget();
      fsrTargetGuard = { target, allowExit: false, invalidated: false };
      gamePath = target.path;
      selectedGameName = targetName.value;
    } else {
      // 无游戏识别时仍允许进入 OptiScaler 流程，直接手动选择目标 exe。
      status('未识别到游戏，请手动选择可执行程序…');
      const picked = await dialog.openFile([
        { name: '可执行程序', extensions: ['exe'] },
      ]).catch((error) => {
        if (canReportFsr()) status('', `打开程序选择器失败：${(error as Error).message}`);
        return null;
      });
      assertFsrContext();
      if (!picked) {
        status('已取消 OptiScaler 操作');
        return;
      }
      gamePath = picked.trim();
      if (!gamePath || !/\.exe$/i.test(gamePath)) {
        status('', '请选择 exe 可执行程序');
        return;
      }
      selectedGameName = fileNameFromPath(gamePath);
    }
    const uninstall = picked === 'uninstall';
    const action = uninstall ? '卸载' : '安装';
    const selectedBackend: OptiBackend | 'auto' = uninstall ? 'auto' : picked as OptiBackend;
    let antiCheatAcknowledged = false;
    if (!uninstall) {
      const preflight = await optiscalerAnalyze(gamePath);
      assertFsrContext();
      if (!preflight.ok || !preflight.scanComplete || !preflight.availableBackends?.includes(selectedBackend as OptiBackend)) {
        throw new Error(preflight.missing?.join('；') || '分析未通过或所选模式缺少缓存，请检查文件/控制器版本。');
      }
      if (preflight.antiCheat) {
        if (!await openDialog('检测到反作弊：仅允许风险确认后的文件导入', optiAntiCheatWarning(selectedGameName), '我了解风险，仍然导入', '取消', false)) {
          if (canReportFsr()) status('已取消导入，游戏文件未修改'); return;
        }
        assertFsrContext();
        antiCheatAcknowledged = true;
      }
    }
    if (target) {
      assertFsrContext();
      if (!await openDialog('需要结束当前游戏', `OptiScaler ${action}前需要结束「${selectedGameName}」。\n是否立即结束游戏并继续？`, '结束游戏并继续')) {
        if (canReportFsr()) status(`已取消${action}，游戏未结束`);
        return;
      }
      const validated = await validateGameTarget(target);
      if (!validated || !sameTarget(validated, target)) throw new Error('游戏目标已变化，已停止后续步骤');
      assertFsrContext();
      // Ending this captured game is an expected A -> none transition. A new
      // game (including A selected in a newer identity generation) cancels it.
      fsrTargetGuard!.allowExit = true;
      const closed = await closeGame(target.pid, target.name, target.processCreated);
      assertFsrContext();
      if (!closed.ok) throw new Error(closed.msgs?.join('；') || '关闭游戏失败');
      if (!(await waitForProcessExit(target.pid, target.processCreated))) throw new Error('游戏进程仍未退出');
      await refreshGameStatusStrict();
      assertFsrContext();
    }
    assertFsrContext();
    const result = await oneClickOptiScaler(gamePath, uninstall, selectedBackend, { acknowledgeAntiCheatRisk: antiCheatAcknowledged });
    assertFsrContext();
    if (!result.ok) throw new Error(result.msgs?.join('；') || `${action}失败`);
    const completedName = selectedGameName;
    if (canReportFsr()) status(`OptiScaler ${action}成功（${result.backend === 'xess' ? 'XeSS' : result.backend === 'fsr' ? 'FSR4' : '自动'}）${result.version ? ` · ${result.version}` : ''}${result.warnings?.length ? `\n${result.warnings.join('\n')}` : ''}${result.clientSyncMessage ? `\n${result.clientSyncMessage}` : ''}${antiCheatAcknowledged || result.antiCheat ? '\n未自动启动：请自行使用已确认的离线方式；未执行反作弊绕过。' : ''}`);
    if (target) releaseTarget(target);
    if (canReportFsr() && !uninstall && optiMayOfferLaunch(antiCheatAcknowledged, result.antiCheat === true) && await openDialog('是否启动游戏', `「${completedName}」已安装完成，是否现在启动游戏？`, '启动游戏')) {
      assertFsrContext();
      if (!canReportFsr()) return;
      await shell.execute(gamePath, []);
      if (canReportFsr()) status(`已启动：${completedName}，请按 Y 刷新`);
    }
  } catch (error) {
    if (canReportFsr()) status('', `OptiScaler 操作失败：${(error as Error).message}`);
  } finally {
    fsrTargetGuard = null;
    busy.value = false;
    release();
  }
}

async function runSpeed(factor: number): Promise<void> {
  if (speedBusy.value) return;
  if (factor === 1 && speedFactor.value === 1) return;
  const release = tryAcquireQuickAction('top-speed');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  const epoch = targetEpoch;
  let target: LockedGameTarget | null = null;
  speedBusy.value = true;
  try {
    target = await ensureTarget();
    assertActionTarget(target);
    if (isMinecraftTarget(target)) throw new Error('当前游戏暂不支持安全变速');
    const result = factor === 1
      ? await clearGameSpeed(target.pid, 'user-reset')
      : await applyGameSpeed(target.pid, factor, target, 'user-factor');
    assertActionTarget(target);
    if (!result.ok || result.safeFallback || result.skipped) throw new Error(result.msgs.join('；') || '变速未应用');
    speedFactor.value = factor === 1 ? 1 : factor;
    status(factor === 1 ? '已恢复 1×' : '已应用 ' + factor + '× 游戏变速');
  } catch (error) {
    if (epoch === targetEpoch && !disposed && (!target || isActionTargetCurrent(target))) {
      await refreshSpeedState(targetGame.value);
      if (epoch === targetEpoch && !disposed && (!target || isActionTargetCurrent(target))) {
        status('', '游戏变速失败：' + (error as Error).message);
      }
    }
  } finally {
    speedBusy.value = false;
    release();
  }
}

function onWakeGameRecovery(): void {
  // Reread current state; a late old recovery event must not clear a new pause.
  void refreshMenuState();
}

function onCustomStatus(value: { message?: string; error?: string }): void {
  status(value.message || '', value.error || '');
}

function onRulesStatus(value: { message?: string; error?: string }): void {
  status(value.message || '', value.error || '');
}

function onRulesChanged(value: { currentBlacklisted?: boolean }): void {
  if (value.currentBlacklisted) {
    rulesOpen.value = false;
    // Rule changes are re-evaluated by the recognition owner, never locally.
    void refreshGameStatusStrict().catch(() => undefined);
  }
}

watch(() => props.open, (open) => { ++menuStateEpoch; if (open) void refreshMenuState(); else { ++controlStateEpoch; trainerMenuOpen.value = false; } });
watch(() => [props.open, targetGame.value?.pid, targetGame.value?.processCreated, targetGame.value?.path, targetGame.value?.identityGeneration], () => { stopLosslessObservation?.();stopLosslessObservation=props.open?retainLosslessObservation():null;void refreshLosslessState(); });

onMounted(() => {
  if(props.open&&!stopLosslessObservation)stopLosslessObservation=retainLosslessObservation();
  stopLosslessState = onLosslessScalingChanged(() => { if (props.open && !busy.value) void refreshLosslessState(); });
  void refreshLosslessState();
  stopTrainerState = subscribeGameTrainerState(() => { trainerStateRevision.value += 1; });
  stopPolicyGame = subscribePolicyGameStatus(syncCurrentTarget);
  window.addEventListener('ipc:gamepad-back', onGamepadBack);
  window.addEventListener(QUICKAPP_SUSPENDED_EVENT, onSuspendedStateSync as EventListener);
  window.addEventListener('ipc:game.wake-recovery', onWakeGameRecovery);
  window.addEventListener('gp:mouse-mode', onMouseModeSync as EventListener);
  window.addEventListener('gp:pad-persona', onPadPersonaSync as EventListener);
  window.addEventListener('game-quick-target-lost', onTargetLost);
  window.addEventListener('game-quick-refresh', refreshGame);
  void refreshMenuState();
});
onBeforeUnmount(() => {
  disposed = true;
  ++menuStateEpoch;
  ++controlStateEpoch;
  ++mouseStateEpoch;
  ++losslessReadEpoch;
  stopLosslessState?.();
  stopLosslessObservation?.();
  trainerCancelRequested.value = true;
  stopTrainerState?.();
  stopPolicyGame?.();
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
  window.removeEventListener(QUICKAPP_SUSPENDED_EVENT, onSuspendedStateSync as EventListener);
  window.removeEventListener('ipc:game.wake-recovery', onWakeGameRecovery);
  window.removeEventListener('gp:mouse-mode', onMouseModeSync as EventListener);
  window.removeEventListener('gp:pad-persona', onPadPersonaSync as EventListener);
  window.removeEventListener('game-quick-target-lost', onTargetLost);
  window.removeEventListener('game-quick-refresh', refreshGame);
  if (fsrResolve) closeDialog(false);
});
</script>

<template>
  <div class="game-quick-menu integrated-game-menu" data-gp-game-quick-menu>
    <div class="game-quick-header">
      <div class="game-quick-target"><AppIcon name="gamepad" /><OverflowGameName :name="targetName" /></div>
    </div>

    <!-- 视觉顺序即手柄横向顺序：data-gp-col 必须与按钮的 DOM 顺序一致（0 切换程序、
         1 暂停游戏、2 关闭游戏、3 模拟鼠标），否则手柄左右与看到的图标对不上。 -->
    <div class="quick-game-controls" data-gp-group="game-quick-game-controls" data-gp-game-row="controls">
      <button type="button" data-gp-row="0" data-gp-col="0" data-gp-game-control="switch-program" :disabled="disabledForAction" @click="openTaskView">
        <AppIcon name="monitor" />切换程序
      </button>
      <button ref="pauseButtonEl" type="button" data-gp-row="0" data-gp-col="1" :disabled="disabledForAction || !targetGame" @click="togglePause">
        <AppIcon :name="paused ? 'play' : 'pause'" />{{ paused ? '继续游戏' : '暂停游戏' }}
      </button>
      <button type="button" class="danger" data-gp-row="0" data-gp-col="2" :disabled="disabledForAction || !targetGame" @click="closeCurrentGame">
        <AppIcon name="close" />关闭游戏
      </button>
      <button type="button" data-gp-row="0" data-gp-col="3" :class="{ active: mouseOn && !steamDeckPadActive }" :disabled="disabledForAction || steamDeckPadActive || !mouseControlAllowed" :title="steamDeckPadActive ? '由 Steam 管理，仅显示状态' : '切换 YMCC 模拟鼠标'" @click="toggleMouse">
        <AppIcon name="mouse" /><span>{{ mouseModeLabel }}</span><small class="quick-control-state">{{ mouseStateLabel }}</small>
      </button>
    </div>
    <div v-if="mouseNotice" class="quick-control-notice">{{ mouseNotice }}</div>

    <!-- 第二排专属配置、第三排游戏黑白名单；视觉顺序与手柄导航一致。 -->
    <GameCustomProfilePanel
      :game="policyGame"
      :open="open"
      @status="onCustomStatus"
      @changed="refreshPadPersona"
    />

    <GameRulePanel
      :game="targetGame"
      :open="rulesOpen"
      @toggle="rulesOpen = !rulesOpen"
      @close="rulesOpen = false"
      @status="onRulesStatus"
      @changed="onRulesChanged"
    />

    <div class="game-quick-actions" data-gp-group="game-quick-actions">
      <button type="button" class="quick-action" data-gp-game-row="actions-1" data-gp-row="1" data-gp-col="0" data-gp-game-control="fsr-import" :disabled="disabledForAction" @click="runFsr">
        <AppIcon name="bolt" /><span><strong>FSR4.1/Xess-OPT自动导入</strong><small>FSR4 / XeSS 自动套用 · OPT 客户端</small></span>
      </button>
      <button type="button" class="quick-action quick-lossless" data-gp-game-row="actions-1" data-gp-row="1" data-gp-col="1" data-gp-game-control="lossless-scaling"
        :disabled="disabledForAction || !targetGame || losslessReading || losslessState?.pending || !losslessState?.controlAvailable" @click="runLosslessScaling">
        <AppIcon name="rocket" /><span class="quick-lossless-text"><strong>一键缩放</strong><small role="status" title="仅表示程序运行状态"><span class="lossless-program-dot" :class="{ running:losslessProgram?.known && losslessProgram.running,unknown:!losslessProgram?.known }" aria-hidden="true"></span>{{ losslessHint }}</small></span>
      </button>
      <button type="button" class="quick-action" data-gp-game-row="actions-2" data-gp-row="2" data-gp-col="0" data-gp-game-control="trainer-menu" :disabled="disabledForAction" :aria-expanded="trainerMenuOpen" aria-haspopup="dialog" @click="trainerMenuOpen = true">
        <AppIcon :name="trainerBusy ? 'close' : 'play'" /><span><strong>{{ trainerButtonLabel }}</strong><small>{{ trainerButtonHint }}</small></span>
      </button>
      <GameTrainerMenu v-if="trainerMenuOpen" :game="targetGame" :game-name="targetName" :has-game="!!targetGame" :busy="trainerBusy" :hint="trainerButtonHint" @close="closeTrainerMenu" @launch="runTrainer" @status="onCustomStatus" />
      <div class="quick-speed" :class="{ 'is-disabled': disabledForAction || !targetGame }" data-gp-game-row="actions-2" data-gp-row="2" data-gp-col="1">
        <span class="quick-speed-title"><AppIcon name="speed" /> 游戏变速</span>
        <Dropdown :model-value="speedFactor" :options="speedOptions" :disabled="disabledForAction || !targetGame" aria-label="游戏变速倍率" gp-row="2" gp-col="1" @change="(v) => runSpeed(Number(v))" />
      </div>
    </div>

    <div class="game-quick-footer" data-gp-group="game-quick-footer" data-gp-game-row="footer">
      <button type="button" :disabled="disabledForAction" @click="refreshGame"><strong class="quick-key-y">Y</strong>刷新游戏获取</button>
      <button type="button" class="cancel" @click="$emit('close-request')"><strong class="quick-key-b">B</strong>关闭页面</button>
    </div>

    <Teleport to="body">
      <Transition name="quick-dialog">
        <div v-if="fsrDialogOpen" ref="fsrDialogPanelEl" class="quick-dialog" :class="{ above: fsrDialogAbove }" :style="fsrDialogStyle" data-gp-modal data-gp-game-quick-dialog role="alertdialog" aria-modal="true">
          <div class="quick-dialog-title"><AppIcon name="bolt" />{{ fsrDialogTitle }}</div>
          <p>{{ fsrDialogDescription }}</p>
          <template v-if="fsrDialogMode === 'backend'">
            <div class="quick-opti-options" role="group" aria-label="OptiScaler 方案">
              <button
                v-for="backend in OPTI_BACKEND_CHOICES"
                :key="backend"
                type="button"
                class="quick-opti-option-btn"
                :class="{ selected: fsrBackendChoice === backend }"
                :aria-pressed="fsrBackendChoice === backend"
                @click="fsrBackendChoice = backend"
              >{{ backend === 'xess' ? 'XeSS' : 'FSR4' }}</button>
              <button type="button" class="quick-opti-option-btn" :class="{ selected: fsrBackendChoice === 'client' }" :aria-pressed="fsrBackendChoice === 'client'" @click="fsrBackendChoice = 'client'">OPT 客户端</button>
            </div>
            <div class="quick-opti-anticheat-actions" role="group" aria-label="反作弊说明与还原">
              <button type="button" class="quick-opti-option-btn" :class="{ selected: fsrBackendChoice === 'help' }" :aria-pressed="fsrBackendChoice === 'help'" @click="fsrBackendChoice = 'help'">反作弊离线说明</button>
              <button type="button" class="quick-opti-option-btn" :class="{ selected: fsrBackendChoice === 'uninstall' }" :aria-pressed="fsrBackendChoice === 'uninstall'" @click="fsrBackendChoice = 'uninstall'">还原</button>
            </div>
            <div class="quick-opti-maintenance">
              <button
                type="button"
                class="quick-opti-option-btn uninstall"
                :class="{ selected: fsrBackendChoice === 'uninstall' }"
                :aria-pressed="fsrBackendChoice === 'uninstall'"
                @click="fsrBackendChoice = 'uninstall'"
              >卸载并还原游戏文件</button>
            </div>
          </template>
          <div class="quick-dialog-actions">
            <button ref="fsrDialogConfirmEl" type="button" @click="closeDialog(fsrDialogMode === 'backend' ? fsrBackendChoice : true)">{{ fsrDialogConfirmLabel }}</button>
            <button ref="fsrDialogCancelEl" type="button" @click="closeDialog(false)">{{ fsrDialogCancelLabel }}</button>
          </div>
        </div>
      </Transition>
    </Teleport>
  </div>
</template>

<style scoped>
.game-quick-menu { display: grid; gap: 8px; }
.game-quick-header { display: grid; gap: 7px; }
.game-quick-target { display: flex; align-items: center; gap: 6px; min-width: 0; color: var(--text); font-size: 12px; font-weight: 700; }
.game-quick-target :deep(svg) { width: 15px; height: 15px; color: var(--accent); flex: 0 0 auto; }
.game-quick-target span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.game-quick-target small { color: var(--text-dim); font-size: 9px; font-weight: 500; }
.game-quick-header-actions { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 6px; }
.game-quick-footer { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 6px; margin-top: 2px; }
.game-quick-footer button { min-height: 30px; border: 1px solid rgba(255,255,255,.08); border-radius: 7px; background: var(--bg-input); color: var(--text); font-size: 10px; cursor: pointer; }
.game-quick-footer button strong { margin-right: 3px; font-weight: 800; }
.game-quick-footer button.cancel { color: var(--text); }
.game-quick-header-actions button { min-height: 30px; border: 1px solid rgba(255,255,255,.08); border-radius: 7px; background: var(--bg-input); color: var(--text); font-size: 10px; cursor: pointer; }
.game-quick-header-actions button strong { margin-right: 3px; font-weight: 800; }
.quick-key-y { color: #f5b942; }
.quick-key-x { color: var(--accent); }
.quick-key-b { color: var(--danger); }
.game-quick-header-actions button.cancel { color: var(--text); }
.game-quick-actions { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 7px; }
.quick-action, .quick-speed { min-width: 0; min-height: 48px; padding: 7px 8px; border: 1px solid rgba(255,255,255,.08); border-radius: 8px; background: var(--bg-input); color: var(--text); }
.quick-action { display: flex; align-items: center; gap: 7px; text-align: left; cursor: pointer; }
.quick-action :deep(svg), .quick-speed-title :deep(svg) { width: 16px; height: 16px; color: var(--accent); flex: 0 0 auto; }
.quick-action span { display: grid; gap: 2px; min-width: 0; }
.quick-action strong { font-size: 11px; }
.quick-action small, .quick-speed-title { color: var(--text-dim); font-size: 9px; }
.quick-lossless-text { flex: 1; }
.lossless-program-dot { display:inline-block; width:7px; height:7px; border-radius:50%; background:#c4c4c4; margin-right:6px; }
.lossless-program-dot.running { background:#4cc38a; }
.lossless-program-dot.unknown { background:#d6a34c; }
.quick-action.active { border-color: color-mix(in srgb, var(--accent) 45%, transparent); color: var(--accent); }
.quick-speed { display: grid; grid-template-columns: minmax(0, 1fr) 82px; align-items: center; gap: 5px; }
.quick-speed.is-disabled { opacity: .45; cursor: default; }
.quick-speed.is-disabled :deep(.dd-trigger:disabled) { opacity: 1; }
.quick-speed-title { display: inline-flex; align-items: center; gap: 4px; color: var(--text); font-size: 11px; font-weight: 700; }
.quick-rules-entry { display: flex; align-items: center; gap: 7px; width: 100%; min-height: 42px; padding: 7px 8px; border: 1px solid rgba(255,255,255,.08); border-radius: 8px; background: var(--bg-input); color: var(--text); text-align: left; cursor: pointer; }
.quick-rules-entry :deep(svg) { width: 16px; height: 16px; color: var(--accent); flex: 0 0 auto; }
.quick-rules-entry span { display: grid; gap: 2px; min-width: 0; }
.quick-rules-entry strong { font-size: 11px; }
.quick-rules-entry small { color: var(--text-dim); font-size: 9px; }
.quick-rules-entry.active { border-color: color-mix(in srgb, var(--accent) 45%, transparent); color: var(--accent); }
.quick-submenu-chevron { display: inline-flex; align-items: center; justify-content: center; width: 22px; margin-left: auto; color: var(--accent); font-size: 16px; line-height: 1; }
.quick-submenu-pop-enter-active, .quick-submenu-pop-leave-active { transition: opacity .16s ease, transform .16s ease; transform-origin: top center; }
.quick-submenu-pop-enter-from, .quick-submenu-pop-leave-to { opacity: 0; transform: translateY(-5px) scale(.985); }
.quick-game-controls { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 6px; }
.quick-game-controls button { display: inline-flex; align-items: center; justify-content: center; gap: 5px; min-width: 0; min-height: 38px; padding: 6px 5px; border: 1px solid rgba(255,255,255,.08); border-radius: 8px; background: var(--bg-input); color: var(--text); font-size: 10px; cursor: pointer; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.quick-game-controls button :deep(svg) { width: 14px; height: 14px; flex: 0 0 auto; }
.quick-game-controls button.active { color: var(--accent); border-color: color-mix(in srgb, var(--accent) 45%, transparent); }
.quick-game-controls button.danger { color: var(--danger); }
.quick-control-notice { color: var(--danger); font-size: 10px; line-height: 1.35; }
.quick-status { color: var(--ok); font-size: 10px; line-height: 1.35; white-space: pre-line; }
.quick-status.error { color: var(--danger); }
.quick-dialog { box-sizing: border-box; overflow-y: auto; padding: 12px; border: 1px solid rgba(255,255,255,.12); border-radius: 10px; background: color-mix(in srgb, var(--bg-solid) 96%, #101722); box-shadow: 0 16px 40px rgba(0,0,0,.55); z-index: 1000; }
.quick-dialog-title { display: flex; align-items: center; gap: 7px; font-weight: 700; font-size: 13px; }
.quick-dialog-title :deep(svg) { width: 16px; height: 16px; color: var(--accent); }
.quick-dialog p { white-space: pre-line; color: var(--text-dim); font-size: 11px; line-height: 1.5; margin: 8px 0; }
.quick-dialog-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 7px; }
.quick-dialog-actions button { min-height: 34px; border: 1px solid rgba(255,255,255,.08); border-radius: 7px; background: var(--bg-input); color: var(--text); cursor: pointer; }
.quick-dialog-actions button:first-child { background: var(--accent); color: #07131d; font-weight: 700; }
.quick-opti-anticheat-actions { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 7px; margin: 8px 0; }
.quick-opti-maintenance { margin: 8px 0; }
.quick-opti-maintenance .quick-opti-option-btn { width: 100%; }
.quick-opti-options { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 7px; margin: 8px 0; }
.quick-opti-option-btn { min-height: 38px; border: 1px solid rgba(255,255,255,.1); border-radius: 7px; background: var(--bg-input); color: var(--text); font-size: 11px; font-weight: 700; cursor: pointer; }
.quick-opti-option-btn:hover, .quick-opti-option-btn.selected { border-color: var(--accent); background: color-mix(in srgb, var(--accent) 14%, var(--bg-input)); color: var(--accent); }
.quick-opti-option-btn.uninstall { color: var(--danger); }
.quick-opti-option-btn.uninstall:hover, .quick-opti-option-btn.uninstall.selected { border-color: var(--danger); background: color-mix(in srgb, var(--danger) 12%, var(--bg-input)); }
button:disabled { opacity: .45; cursor: default; }
</style>
