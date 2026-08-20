<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import GameCustomProfilePanel from '@/components/GameCustomProfilePanel.vue';
import GameRulePanel from '@/components/GameRulePanel.vue';
import Dropdown from '@/components/Dropdown.vue';
import { focusGamepadElement, getGamepadPopupPlacement } from '@/gamepad/focus';
import { detectGame, detectedGameName, type DetectedGame } from '@/bridge/gamedetect';
import { openOrSearchGameTrainer } from '@/bridge/gameTrainer';
import { oneClickFrameGen, optiscalerStatus, oneClickOptiScaler } from '@/bridge/quickapp';
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
import { shell } from '@/bridge/api';
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
let fsrResolve: ((value: boolean) => void) | null = null;

const targetGame = computed(() => locked.value || props.game);
const targetName = computed(() => targetGame.value
  ? (detectedGameName(targetGame.value) || targetGame.value.name)
  : '未识别游戏');
const disabledForAction = computed(() => busy.value || trainerBusy.value || speedBusy.value || pauseBusy.value || closeBusy.value || mouseBusy.value || taskViewBusy.value);

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
  if (!target) {
    paused.value = false;
    return;
  }
  paused.value = (await hasSuspendedState(target.pid).catch(() => ({ suspended: false }))).suspended;
}

function onSuspendedStateSync(e: Event): void {
  paused.value = Boolean((e as CustomEvent<{ suspended?: boolean }>).detail?.suspended);
}

function onMouseModeSync(e: Event): void {
  mouseOn.value = Boolean((e as CustomEvent<{ on?: boolean }>).detail?.on);
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

// Windows 原生任务视图入口：直接调用 Explorer Shell 项，不模拟 Win+Tab/Alt+Tab 按键。
const WINDOWS_TASK_VIEW_SHELL = 'shell:::{3080F90E-D7AD-11D9-BD98-0000947B0257}';

async function openTaskView(): Promise<void> {
  if (taskViewBusy.value) return;
  taskViewBusy.value = true;
  try {
    await shell.execute('explorer.exe', [WINDOWS_TASK_VIEW_SHELL]);
    status('已打开 Windows 窗口切换');
  } catch (error) {
    status('', `打开 Windows 窗口切换失败：${(error as Error).message}`);
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
    if (!result.ok) throw new Error(result.error || '模拟鼠标切换失败');
    mouseOn.value = result.on;
    window.dispatchEvent(new CustomEvent('gp:mouse-mode', { detail: { on: result.on, backend: result.backend } }));
    status(result.on ? '模拟鼠标已开启' : '模拟鼠标已关闭');
  } catch (error) {
    mouseNotice.value = (error as Error).message;
    status('', `模拟鼠标切换失败：${(error as Error).message}`);
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
  if (trainerBusy.value) return;
  const release = tryAcquireQuickAction('top-trainer');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  trainerBusy.value = true;
  status('正在准备游戏修改器…');
  try {
    const target = await ensureTarget();
    const name = detectedGameName(target);
    if (!name) throw new Error('未识别到真实游戏名，无法搜索修改器');
    const result = await openOrSearchGameTrainer(name, (progress) => {
      if (progress.message) status(progress.message);
    }, target.path);
    status(result.action === 'opened' ? '已打开游戏修改器' : '修改器搜索已完成');
  } catch (error) {
    status('', `游戏修改器操作失败：${(error as Error).message}`);
  } finally {
    trainerBusy.value = false;
    release();
  }
}

async function runLosslessScaling(): Promise<void> {
  if (busy.value) return;
  const release = tryAcquireQuickAction('top-lossless');
  if (!release) { status('', '已有其它快捷操作正在执行，请稍候'); return; }
  busy.value = true;
  status('正在启动小黄鸭…');
  try {
    const target = await ensureTarget();
    const result = await oneClickFrameGen(target.path);
    status(result.alreadyHadProfile ? '已启动小黄鸭' : '已写入预设并启动小黄鸭');
  } catch (error) {
    status('', `小黄鸭启动失败：${(error as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

function openDialog(title: string, description: string, confirmLabel = '确认', cancelLabel = '取消'): Promise<boolean> {
  fsrDialogTitle.value = title;
  fsrDialogDescription.value = description;
  fsrDialogConfirmLabel.value = confirmLabel;
  fsrDialogCancelLabel.value = cancelLabel;
  fsrDialogOpen.value = true;
  const anchor = document.querySelector<HTMLElement>('[data-gp-game-quick-menu]')?.getBoundingClientRect() || null;
  const placement = getGamepadPopupPlacement(anchor, Math.min(420, window.innerWidth - 16), 260, 8);
  fsrDialogAbove.value = placement.above;
  fsrDialogStyle.value = placement.style;
  nextTick(() => focusGamepadElement(fsrDialogConfirmEl.value));
  return new Promise<boolean>((resolve) => { fsrResolve = resolve; });
}

function closeDialog(value: boolean): void {
  if (!fsrDialogOpen.value && !fsrResolve) return;
  fsrDialogOpen.value = false;
  const resolve = fsrResolve;
  fsrResolve = null;
  resolve?.(value);
  nextTick(() => focusGamepadElement(document.querySelector<HTMLElement>('[data-gp-game-quick-menu] button')));
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
  status('正在读取 FSR4.1 状态…');
  let target: LockedGameTarget | null = null;
  try {
    target = await ensureTarget();
    const state = await optiscalerStatus(target.path);
    if (!state.ok) throw new Error(state.msgs?.filter(Boolean).join('；') || '无法读取 FSR4.1 状态');
    const uninstall = state.installed;
    const action = uninstall ? '卸载' : '安装';
    if (!await openDialog(`确认${action} FSR4.1`, `当前游戏：${targetName.value}\n是否${action} OptiScaler (FSR4.1)？`, `确认${action}`)) {
      status(`已取消${action}`);
      return;
    }
    if (!await openDialog('需要结束当前游戏', `FSR4.1 ${action}前需要结束「${targetName.value}」。\n是否立即结束游戏并继续？`, '结束游戏并继续')) {
      status(`已取消${action}，游戏未结束`);
      return;
    }
    const closed = await closeGame(target.pid, target.name, target.processCreated);
    if (!closed.ok) throw new Error(closed.msgs?.join('；') || '关闭游戏失败');
    if (!(await waitForProcessExit(target.pid, target.processCreated))) throw new Error('游戏进程仍未退出');
    const result = await oneClickOptiScaler(target.path, uninstall);
    if (!result.ok) throw new Error(result.msgs?.join('；') || `${action}失败`);
    const completedName = targetName.value;
    status(`FSR4.1 ${action}成功`);
    onTargetLost();
    if (!uninstall && await openDialog('是否启动游戏', `「${completedName}」已安装完成，是否现在启动游戏？`, '启动游戏')) {
      await shell.execute(target.path, []);
      status(`已启动：${completedName}，请按 Y 刷新`);
    }
  } catch (error) {
    status('', `FSR4.1 操作失败：${(error as Error).message}`);
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
    status(factor === 1 ? '已恢复 1×' : `已应用 ${factor}× 游戏加速`);
  } catch (error) {
    speedFactor.value = 1;
    status('', `游戏加速失败：${(error as Error).message}`);
  } finally {
    speedBusy.value = false;
    release();
  }
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
  window.addEventListener('ipc:gamepad-back', onGamepadBack);
  window.addEventListener(QUICKAPP_SUSPENDED_EVENT, onSuspendedStateSync as EventListener);
  window.addEventListener('gp:mouse-mode', onMouseModeSync as EventListener);
  window.addEventListener('game-quick-target-lost', onTargetLost);
  window.addEventListener('game-quick-refresh', refreshGame);
  void refreshMenuState();
});
onBeforeUnmount(() => {
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
  window.removeEventListener(QUICKAPP_SUSPENDED_EVENT, onSuspendedStateSync as EventListener);
  window.removeEventListener('gp:mouse-mode', onMouseModeSync as EventListener);
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

    <div class="quick-game-controls" data-gp-group="game-quick-game-controls">
      <button ref="pauseButtonEl" type="button" :disabled="disabledForAction || !targetGame" @click="togglePause">
        <AppIcon :name="paused ? 'play' : 'pause'" />{{ paused ? '继续游戏' : '暂停游戏' }}
      </button>
      <button type="button" class="danger" :disabled="disabledForAction || !targetGame" @click="closeCurrentGame">
        <AppIcon name="close" />关闭游戏
      </button>
      <button type="button" :disabled="disabledForAction" @click="openTaskView">
        <AppIcon name="monitor" />切换程序
      </button>
      <button type="button" :class="{ active: mouseOn }" :disabled="disabledForAction" @click="toggleMouse">
        <AppIcon name="mouse" />{{ mouseOn ? '模拟鼠标已开启' : '模拟鼠标已关闭' }}
      </button>
    </div>
    <div v-if="mouseNotice" class="quick-control-notice">{{ mouseNotice }}</div>

    <div class="game-quick-actions" data-gp-group="game-quick-actions">
      <button type="button" class="quick-action" :disabled="disabledForAction || !targetGame" @click="runTrainer">
        <AppIcon name="play" /><span><strong>游戏修改器</strong><small>{{ trainerBusy ? '处理中…' : '识别后打开' }}</small></span>
      </button>
      <button type="button" class="quick-action" :disabled="disabledForAction || !targetGame" @click="runLosslessScaling">
        <AppIcon name="rocket" /><span><strong>小黄鸭</strong><small>一键插帧</small></span>
      </button>
      <button type="button" class="quick-action" :disabled="disabledForAction || !targetGame" @click="runFsr">
        <AppIcon name="bolt" /><span><strong>FSR4.1</strong><small>安装 / 卸载</small></span>
      </button>
      <div class="quick-speed">
        <span class="quick-speed-title"><AppIcon name="speed" /> 游戏加速</span>
        <Dropdown :model-value="speedFactor" :options="speedOptions" :disabled="disabledForAction || !targetGame" aria-label="游戏加速倍率" @change="(v) => runSpeed(Number(v))" />
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
      :game="targetGame"
      :open="open"
      @status="onCustomStatus"
    />

    <div class="game-quick-footer" data-gp-group="game-quick-footer">
      <button type="button" :disabled="disabledForAction" @click="refreshGame"><strong class="quick-key-y">Y</strong>刷新游戏获取</button>
      <button type="button" class="cancel" @click="$emit('close-request')"><strong class="quick-key-b">B</strong>关闭页面</button>
    </div>

    <Teleport to="body">
      <Transition name="quick-dialog">
        <div v-if="fsrDialogOpen" ref="fsrDialogPanelEl" class="quick-dialog" :class="{ above: fsrDialogAbove }" :style="fsrDialogStyle" data-gp-modal data-gp-game-quick-dialog role="alertdialog" aria-modal="true">
          <div class="quick-dialog-title"><AppIcon name="bolt" />{{ fsrDialogTitle }}</div>
          <p>{{ fsrDialogDescription }}</p>
          <div class="quick-dialog-actions">
            <button ref="fsrDialogConfirmEl" type="button" @click="closeDialog(true)">{{ fsrDialogConfirmLabel }}</button>
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
.quick-speed { display: grid; grid-template-columns: minmax(0, 1fr) 82px; align-items: center; gap: 5px; }
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
.quick-dialog { padding: 12px; border: 1px solid rgba(255,255,255,.12); border-radius: 10px; background: color-mix(in srgb, var(--bg-solid) 96%, #101722); box-shadow: 0 16px 40px rgba(0,0,0,.55); z-index: 1000; }
.quick-dialog-title { display: flex; align-items: center; gap: 7px; font-weight: 700; font-size: 13px; }
.quick-dialog-title :deep(svg) { width: 16px; height: 16px; color: var(--accent); }
.quick-dialog p { white-space: pre-line; color: var(--text-dim); font-size: 11px; line-height: 1.5; margin: 8px 0; }
.quick-dialog-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 7px; }
.quick-dialog-actions button { min-height: 34px; border: 1px solid rgba(255,255,255,.08); border-radius: 7px; background: var(--bg-input); color: var(--text); cursor: pointer; }
.quick-dialog-actions button:first-child { background: var(--accent); color: #07131d; font-weight: 700; }
button:disabled { opacity: .45; cursor: default; }
</style>
