<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import GameCustomProfilePanel from '@/components/GameCustomProfilePanel.vue';
import GameRulePanel from '@/components/GameRulePanel.vue';
import Dropdown from '@/components/Dropdown.vue';
import { focusGamepadElement, getGamepadPopupPlacement } from '@/gamepad/focus';
import { detectGame, detectedGameName, type DetectedGame } from '@/bridge/gamedetect';
import { GameTrainerCancelledError, openOrSearchGameTrainer } from '@/bridge/gameTrainer';
import {
  oneClickFrameGen,
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
  waitForProcessExit,
} from '@/bridge/gameproc';
import {
  getLockedGameTarget,
  lockGameTarget,
  unlockGameTarget,
  validateLockedGameTarget,
  type LockedGameTarget,
} from '@/bridge/gameQuickSession';
import { dialog, proc, shell, windowApi } from '@/bridge/api';
import { subscribePolicyGameStatus, getPolicyGame } from '@/bridge/gamePolicyTarget';
import { effectiveInputPersona } from '@/bridge/gameInputOverride';
import { loadSettings } from '@/bridge/settingsRepository';
import { tryAcquireQuickAction } from '@/bridge/quickActionLock';
import { isMouseModeSuppressed } from '@/gamepad/engine';

const props = defineProps<{ game: DetectedGame | null; open: boolean }>();
const emit = defineEmits<{
  (e: 'game-updated', game: DetectedGame | null): void;
  (e: 'status', value: { message?: string; error?: string }): void;
  (e: 'close-request'): void;
}>();

const locked = ref<LockedGameTarget | null>(getLockedGameTarget());
const busy = ref(false);
const trainerBusy = ref(false);
const trainerCancelRequested = ref(false);
const trainerWorkerPid = ref(0);
const speedBusy = ref(false);
const pauseBusy = ref(false);
const closeBusy = ref(false);
const mouseBusy = ref(false);
const taskViewBusy = ref(false);
const paused = ref(false);
const mouseOn = ref(false);
const mouseNotice = ref('');
const statusMsg = ref('');
const errMsg = ref('');
const speedFactor = ref(1);
// 2026-09-30 用户裁决：当前输出人格决定鼠标从哪来——SteamDeck 虚拟手柄时鼠标由
// Steam 接管，这一行要如实显示「Steam鼠标」而不是笼统的「模拟鼠标」。
const padPersona = ref('disabled');
const steamDeckPadActive = computed(() => padPersona.value === 'steamdeck');
const mouseModeLabel = computed(() => (steamDeckPadActive.value ? 'Steam鼠标' : '模拟鼠标'));
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
const fsrDialogPanelEl = ref<HTMLElement | null>(null);
const fsrDialogMode = ref<'confirm' | 'backend'>('confirm');
const fsrBackendChoice = ref<OptiAction>('fsr');
const OPTI_BACKEND_CHOICES: OptiBackend[] = ['fsr', 'xess'];
let fsrResolve: ((value: boolean | OptiAction | null) => void) | null = null;

// 只打开 OPT 客户端，不读取或关闭它的运行进程。
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
    await openOptiConsole();
    status('已打开 OPT 客户端');
  } catch (error) {
    status('', `OPT 客户端打开失败：${(error as Error).message}`);
  }
}

const policyGame = ref<DetectedGame | null>(getPolicyGame());
let stopPolicyGame: (() => void) | null = null;
const targetGame = computed(() => locked.value || props.game);
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

function publishGame(game: DetectedGame | null): void {
  emit('game-updated', game);
}

function sameTarget(
  a: Pick<DetectedGame, 'pid' | 'processCreated'> | null,
  b: Pick<DetectedGame, 'pid' | 'processCreated'> | null,
): boolean {
  return !!a && !!b && a.pid === b.pid && String(a.processCreated) === String(b.processCreated);
}

function fileNameFromPath(path: string): string {
  const parts = path.trim().split(/[\\/]/);
  return parts[parts.length - 1] || path.trim();
}

async function ensureTarget(): Promise<LockedGameTarget> {
  if (locked.value) {
    const current = await validateLockedGameTarget();
    if (current) {
      locked.value = current;
      publishGame(current);
      return current;
    }
    // Transport errors keep the shared lock. Only a cleared session proves that
    // the process identity disappeared or changed.
    if (getLockedGameTarget()) throw new Error('锁定游戏校验失败，请稍后重试');
    locked.value = null;
    publishGame(null);
    throw new Error('锁定游戏已退出或目标已变化，请按 Y 重新搜索');
  }
  const candidate = await detectGame(true);
  if (!candidate) throw new Error('当前没有识别到游戏');
  const next = lockGameTarget(candidate, 'action');
  locked.value = next;
  publishGame(next);
  return next;
}

async function refreshGame(): Promise<void> {
  if (disabledForAction.value) return;
  status();
  try {
    if (locked.value) {
      const current = await validateLockedGameTarget();
      if (!current) {
        if (!getLockedGameTarget()) {
          onTargetLost();
          status('锁定游戏已退出，请重新搜索');
        } else {
          status('', '锁定游戏校验失败，请稍后重试');
        }
        return;
      }
      locked.value = current;
      publishGame(current);
      await refreshSpeedState(current);
      await refreshControlState(current);
      status('已刷新锁定游戏');
      return;
    }
    const current = await detectGame(true);
    publishGame(current);
    await refreshSpeedState(current);
    await refreshControlState(current);
    status(current ? '已刷新游戏识别' : '刷新完成，当前未识别游戏');
  } catch (error) {
    status('', `刷新游戏识别失败：${(error as Error).message}`);
  }
}

async function refreshSpeedState(target: DetectedGame | null): Promise<void> {
  if (!target) {
    speedFactor.value = 1;
    return;
  }
  const state = getGameSpeedState();
  speedFactor.value = state && sameTarget(state, target) && state.factor > 0 ? state.factor : 1;
}

async function refreshControlState(target: DetectedGame | null): Promise<void> {
  mouseOn.value = isMouseModeSuppressed();
  await refreshPadPersona();
  if (!target) {
    paused.value = false;
    return;
  }
  paused.value = (await hasSuspendedState(target.pid).catch(() => ({ suspended: false }))).suspended;
}

// 顶部菜单要如实反映当前输出人格（专属配置的覆盖也走同一份设置），
// 读取失败保持上一次的值，不用错误值覆盖显示。
async function refreshPadPersona(): Promise<void> {
  try {
    const settings = await loadSettings();
    padPersona.value = effectiveInputPersona(settings.input);
  } catch { /* 保持上值 */ }
}

function onSuspendedStateSync(e: Event): void {
  paused.value = Boolean((e as CustomEvent<{ suspended?: boolean }>).detail?.suspended);
}

function onMouseModeSync(e: Event): void {
  mouseOn.value = Boolean((e as CustomEvent<{ on?: boolean }>).detail?.on);
}

// 覆盖下发/恢复后 native 侧真正生效的人格由 gameInputOverride 广播（含游戏退出恢复）。
function onPadPersonaSync(e: Event): void {
  const persona = (e as CustomEvent<{ persona?: string }>).detail?.persona;
  if (typeof persona === 'string' && persona) padPersona.value = persona;
}

async function togglePause(): Promise<void> {
  if (pauseBusy.value) return;
  pauseBusy.value = true;
  try {
    const target = await ensureTarget();
    if (paused.value) {
      const result = await resumeGame();
      paused.value = result.failCount > 0;
      status(result.okCount > 0 ? `已继续 ${result.okCount} 个被冻结的进程` : (result.ok ? '游戏已继续' : `继续失败：${result.msgs.join('；') || '没有待恢复的进程'}`));
    } else {
      const result = await suspendGame(target.pid, target.name || target.title || '当前游戏', target.processCreated);
      paused.value = result.ok;
      status(result.ok ? `已暂停 ${target.name || target.title || '当前游戏'}` : `暂停失败：${result.msgs.join('；') || '没有可暂停的游戏进程'}`);
    }
  } catch (error) {
    status('', `暂停/继续失败：${(error as Error).message}`);
  } finally {
    pauseBusy.value = false;
  }
}

async function closeCurrentGame(): Promise<void> {
  if (closeBusy.value) return;
  closeBusy.value = true;
  try {
    const target = await ensureTarget();
    const result = await closeGame(target.pid, target.name, target.processCreated);
    if (!result.ok) throw new Error(result.msgs.join('；') || '关闭失败');
    status(`已关闭 ${target.name}`);
    onTargetLost();
  } catch (error) {
    status('', `关闭游戏失败：${(error as Error).message}`);
  } finally {
    closeBusy.value = false;
  }
}

// Windows 任务视图：由 native 注入真实 Win+Tab（与手柄类型解耦；不再走 explorer CLSID
// 转发——后者每次调用会驻留一个 explorer 实例且成功率不稳）。
// 切换程序是“离开 YMCC 去用别的窗口”：任务视图弹出后随即最小化本窗口（与界面上的
// 最小化按钮同一语义），否则选完目标 YMCC 仍以最大化窗口留在前台，还要手动再收一次。
async function openTaskView(): Promise<void> {
  if (taskViewBusy.value) return;
  taskViewBusy.value = true;
  try {
    await shell.taskView();
    // 注入失败时保持窗口原样、错误提示仍可见；最小化是随动作一起做的窗口交接。
    await windowApi.minimize().catch(() => false);
    status('已切换 Windows 任务视图');
  } catch (error) {
    status('', `切换 Windows 任务视图失败：${(error as Error).message}`);
  } finally {
    taskViewBusy.value = false;
  }
}

async function toggleMouse(): Promise<void> {
  if (mouseBusy.value) return;
  mouseBusy.value = true;
  mouseNotice.value = '';
  try {
    const result = await toggleMouseMode();
    if (!result.ok) throw new Error(result.error || `${mouseModeLabel.value}切换失败`);
    mouseOn.value = result.on;
    window.dispatchEvent(new CustomEvent('gp:mouse-mode', { detail: { on: result.on, backend: result.backend } }));
    status(result.on ? `${mouseModeLabel.value}已开启` : `${mouseModeLabel.value}已关闭`);
  } catch (error) {
    mouseNotice.value = (error as Error).message;
    status('', `${mouseModeLabel.value}切换失败：${(error as Error).message}`);
  } finally {
    mouseBusy.value = false;
  }
}

async function refreshMenuState(): Promise<void> {
  if (!props.open || disabledForAction.value) return;
  const target = locked.value
    ? await validateLockedGameTarget()
    : (props.game || await detectGame(true).catch(() => null));
  if (locked.value && !target) {
    if (!getLockedGameTarget()) {
      onTargetLost();
      status('锁定游戏已退出，请重新搜索');
    }
    return;
  }
  if (target) {
    if (!locked.value) publishGame(target);
    locked.value = getLockedGameTarget();
  }
  await refreshSpeedState(target || null);
  await refreshControlState(target || null);
}

function onTargetLost(): void {
  unlockGameTarget();
  locked.value = null;
  speedFactor.value = 1;
  paused.value = false;
  publishGame(null);
}

async function runTrainer(): Promise<void> {
  if (trainerBusy.value) {
    trainerCancelRequested.value = true;
    const pid = trainerWorkerPid.value;
    if (pid > 0) await proc.terminateTree(pid).catch(() => undefined);
    status('已终止游戏修改器搜索');
    return;
  }
  const release = tryAcquireQuickAction('top-trainer');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  trainerBusy.value = true;
  trainerCancelRequested.value = false;
  trainerWorkerPid.value = 0;
  status('正在准备游戏修改器…');
  try {
    const target = await ensureTarget();
    const name = detectedGameName(target);
    if (!name) throw new Error('未识别到真实游戏名，无法搜索修改器');
    const result = await openOrSearchGameTrainer(name, (progress) => {
      if (progress.message) status(progress.message);
    }, target.path, {
      isCancelled: () => trainerCancelRequested.value,
      onWorkerStarted: (pid) => { trainerWorkerPid.value = pid; },
    });
    if (!trainerCancelRequested.value) {
      status(result.action === 'opened' ? '已打开游戏修改器' : '修改器搜索已完成');
    }
  } catch (error) {
    if (!(error instanceof GameTrainerCancelledError) && !trainerCancelRequested.value) {
      status('', `游戏修改器操作失败：${(error as Error).message}`);
    }
  } finally {
    trainerBusy.value = false;
    trainerWorkerPid.value = 0;
    trainerCancelRequested.value = false;
    release();
  }
}

async function runLosslessScaling(): Promise<void> {
  if (busy.value) return;
  const release = tryAcquireQuickAction('top-lossless');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  busy.value = true;
  status('正在启动 Lossless Scaling…');
  try {
    const target = await ensureTarget();
    const result = await oneClickFrameGen(target.path);
    status(result.alreadyHadProfile ? '已启动 Lossless Scaling' : '已写入预设并启动 Lossless Scaling');
  } catch (error) {
    status('', `Lossless Scaling 启动失败：${(error as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

function openDialog(title: string, description: string, confirmLabel = '确认', cancelLabel = '取消'): Promise<boolean> {
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
  nextTick(() => focusGamepadElement(fsrDialogConfirmEl.value));
  return new Promise<boolean>((resolve) => {
    fsrResolve = (value) => resolve(value === true);
  });
}

function openBackendDialog(): Promise<OptiAction | null> {
  fsrDialogMode.value = 'backend';
  fsrBackendChoice.value = 'fsr';
  fsrDialogTitle.value = '选择 OptiScaler 方案';
  fsrDialogDescription.value = 'FSR4 / XeSS 自动套用本地缓存与 Auto x2 配置；OPT 客户端只打开程序。';
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
    fsrResolve = (value) => resolve(value === 'fsr' || value === 'xess' || value === 'client' || value === 'uninstall' ? value : null);
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
  try {
    const picked = await openBackendDialog();
    if (!picked) { status('已取消 OptiScaler 操作'); return; }
    if (picked === 'client') { await onOpenOptiConsole(); return; }
    if (targetGame.value) {
      target = await ensureTarget();
      gamePath = target.path;
      selectedGameName = targetName.value;
    } else {
      // 无游戏识别时仍允许进入 OptiScaler 流程，直接手动选择目标 exe。
      status('未识别到游戏，请手动选择可执行程序…');
      const picked = await dialog.openFile([
        { name: '可执行程序', extensions: ['exe'] },
      ]).catch((error) => {
        status('', `打开程序选择器失败：${(error as Error).message}`);
        return null;
      });
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
    if (!uninstall) {
      const preflight = await optiscalerAnalyze(gamePath);
      if (!preflight.ok || preflight.antiCheat || !preflight.availableBackends?.includes(selectedBackend as OptiBackend)) {
        throw new Error(preflight.antiCheat ? '检测到反作弊，不进行自动注入。' : (preflight.missing?.join('；') || '所选模式缺少本地缓存，请先准备文件。'));
      }
    }
    if (target) {
      if (!await openDialog('需要结束当前游戏', `OptiScaler ${action}前需要结束「${selectedGameName}」。\n是否立即结束游戏并继续？`, '结束游戏并继续')) {
        status(`已取消${action}，游戏未结束`);
        return;
      }
      const closed = await closeGame(target.pid, target.name, target.processCreated);
      if (!closed.ok) throw new Error(closed.msgs?.join('；') || '关闭游戏失败');
      if (!(await waitForProcessExit(target.pid, target.processCreated))) throw new Error('游戏进程仍未退出');
    }
    const result = await oneClickOptiScaler(gamePath, uninstall, selectedBackend);
    if (!result.ok) throw new Error(result.msgs?.join('；') || `${action}失败`);
    const completedName = selectedGameName;
    status(`OptiScaler ${action}成功（${result.backend === 'xess' ? 'XeSS' : result.backend === 'fsr' ? 'FSR4' : '自动'}）${result.version ? ` · ${result.version}` : ''}${result.warnings?.length ? `\n${result.warnings.join('\n')}` : ''}`);
    if (target) onTargetLost();
    if (!uninstall && await openDialog('是否启动游戏', `「${completedName}」已安装完成，是否现在启动游戏？`, '启动游戏')) {
      await shell.execute(gamePath, []);
      status(`已启动：${completedName}，请按 Y 刷新`);
    }
  } catch (error) {
    status('', `OptiScaler 操作失败：${(error as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

async function runSpeed(factor: number): Promise<void> {
  if (speedBusy.value) return;
  if (factor === 1 && speedFactor.value === 1) return;
  const release = tryAcquireQuickAction('top-speed');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  speedBusy.value = true;
  try {
    const target = await ensureTarget();
    if (isMinecraftTarget(target)) throw new Error('当前游戏暂不支持安全变速');
    const result = factor === 1
      ? await clearGameSpeed(target.pid, 'user-reset')
      : await applyGameSpeed(target.pid, factor, target, 'user-factor');
    if (!result.ok && !result.safeFallback) throw new Error(result.msgs.join('；') || '变速失败');
    speedFactor.value = factor === 1 ? 1 : factor;
    status(factor === 1 ? '已恢复 1×' : `已应用 ${factor}× 游戏变速`);
  } catch (error) {
    speedFactor.value = 1;
    status('', `游戏变速失败：${(error as Error).message}`);
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
    onTargetLost();
  }
}

watch(() => props.game, (game) => {
  if (!locked.value || sameTarget(locked.value, game)) void refreshSpeedState(game);
});
watch(() => props.open, (open) => { if (open) void refreshMenuState(); });

onMounted(() => {
  stopPolicyGame = subscribePolicyGameStatus((game) => { policyGame.value = game; });
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
  <div class="game-quick-menu" data-gp-game-quick-menu>
    <div class="game-quick-header">
      <div class="game-quick-target"><AppIcon :name="locked ? 'lock' : 'gamepad'" /><span>{{ targetName }}</span><small>{{ locked ? '已锁定' : '未锁定' }}</small></div>
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
      <button type="button" data-gp-row="0" data-gp-col="3" :class="{ active: mouseOn }" :disabled="disabledForAction" @click="toggleMouse">
        <AppIcon name="mouse" />{{ mouseModeLabel }}{{ mouseOn ? '已开启' : '已关闭' }}
      </button>
    </div>
    <div v-if="mouseNotice" class="quick-control-notice">{{ mouseNotice }}</div>

    <div class="game-quick-actions" data-gp-group="game-quick-actions">
      <button type="button" class="quick-action" data-gp-game-row="actions-1" data-gp-row="1" data-gp-col="0" data-gp-game-control="fsr-import" :disabled="disabledForAction" @click="runFsr">
        <AppIcon name="bolt" /><span><strong>FSR4.1/Xess-OPT自动导入</strong><small>FSR4 / XeSS 自动套用 · OPT 客户端</small></span>
      </button>
      <button type="button" class="quick-action" data-gp-game-row="actions-1" data-gp-row="1" data-gp-col="1" :disabled="disabledForAction || !targetGame" @click="runLosslessScaling">
        <AppIcon name="rocket" /><span><strong>Lossless Scaling</strong><small>小黄鸭一键插帧</small></span>
      </button>
      <button type="button" class="quick-action" data-gp-game-row="actions-2" data-gp-row="2" data-gp-col="0" :disabled="disabledForAction || !targetGame" @click="runTrainer">
        <AppIcon :name="trainerBusy ? 'close' : 'play'" /><span><strong>游戏修改器</strong><small>{{ trainerBusy ? '处理中…点击终止' : '识别后打开' }}</small></span>
      </button>
      <div class="quick-speed" :class="{ 'is-disabled': disabledForAction || !targetGame }" data-gp-game-row="actions-2" data-gp-row="2" data-gp-col="1">
        <span class="quick-speed-title"><AppIcon name="speed" /> 游戏变速</span>
        <Dropdown :model-value="speedFactor" :options="speedOptions" :disabled="disabledForAction || !targetGame" aria-label="游戏变速倍率" gp-row="2" gp-col="1" @change="(v) => runSpeed(Number(v))" />
      </div>
    </div>

    <GameRulePanel
      :game="targetGame"
      :open="rulesOpen"
      @toggle="rulesOpen = !rulesOpen"
      @close="rulesOpen = false"
      @status="onRulesStatus"
      @changed="onRulesChanged"
    />

    <GameCustomProfilePanel
      :game="policyGame"
      :open="open"
      @status="onCustomStatus"
      @changed="refreshPadPersona"
    />

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
            <button type="button" @click="closeDialog(false)">{{ fsrDialogCancelLabel }}</button>
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
.quick-opti-maintenance { margin: 8px 0; }
.quick-opti-maintenance .quick-opti-option-btn { width: 100%; }
.quick-opti-options { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 7px; margin: 8px 0; }
.quick-opti-option-btn { min-height: 38px; border: 1px solid rgba(255,255,255,.1); border-radius: 7px; background: var(--bg-input); color: var(--text); font-size: 11px; font-weight: 700; cursor: pointer; }
.quick-opti-option-btn:hover, .quick-opti-option-btn.selected { border-color: var(--accent); background: rgba(46,166,255,.14); color: var(--accent); }
.quick-opti-option-btn.uninstall { color: var(--danger); }
.quick-opti-option-btn.uninstall:hover, .quick-opti-option-btn.uninstall.selected { border-color: var(--danger); background: color-mix(in srgb, var(--danger) 12%, var(--bg-input)); }
button:disabled { opacity: .45; cursor: default; }
</style>
