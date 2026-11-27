<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import GameValueScanner from '@/components/GameValueScanner.vue';
import { isGameValueScanAvailable, getGameValueSessionTarget } from '@/bridge/gameValueScan';
import type { DetectedGame } from '@/bridge/gamedetect';
import { captureAndLockGameTarget, type LockedGameTarget } from '@/bridge/gameQuickSession';
import { focusGamepadElement, getGamepadPopupPlacement, getGamepadViewportSafeArea } from '@/gamepad/focus';
import { openCheatEngine, openGameTrainerManager } from '@/bridge/gameTrainer';
import { getUiSetting, loadUiSettings, setUiSettings } from '@/bridge/uiSettings';

const props = defineProps<{ gameName: string; busy: boolean; hint: string; hasGame: boolean; game?: DetectedGame | null; message?: string; error?: string }>();
const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'launch'): void;
  (e: 'status', value: { message?: string; error?: string }): void;
}>();
const panel = ref<HTMLElement | null>(null);
const items = ref<HTMLElement | null>(null);
let menuResize: ResizeObserver | null = null;
const scanTarget = ref<LockedGameTarget | null>(null);
const style = ref<Record<string, string>>({ position: 'fixed' });
const autoEnabled = ref(getUiSetting('trainerAutoPrepare'));
const saving = ref(false);
const opening = ref(false);
const ceScanAvailable = ref(false);
const launchLabel = computed(() => props.busy ? '取消等待启动' : props.hasGame ? `启动 ${props.gameName} 游戏修改器` : '未识别到游戏');
let closed = false;
let anchor: HTMLElement | null = null;

function position(): void {
  const menu = panel.value, list = items.value;
  if (closed || scanTarget.value || !menu?.offsetWidth || !list) return;
  // This body-teleported menu must use the shell's visual scale, like its opener.
  const owner = anchor?.closest<HTMLElement>('.game-recognition-popover')
    || document.querySelector<HTMLElement>('.app-content');
  const ownerRect = owner?.getBoundingClientRect();
  const pixels = (value: string) => Number.parseFloat(value) || 0;
  const scale = Math.max(.5, Math.min(4, ownerRect && owner?.offsetWidth
    ? ownerRect.width / owner.offsetWidth
    : pixels(getComputedStyle(document.documentElement).getPropertyValue('--ui-scale')) || 1));
  const ownerCss = owner ? getComputedStyle(owner) : null;
  const anchorCss = anchor ? getComputedStyle(anchor) : null;
  const appearance = {
    'font-size': `${14 * scale}px`,
    'border-radius': `${pixels(ownerCss?.borderTopLeftRadius || '12px') * scale}px`,
    '--trainer-action-radius': `${pixels(anchorCss?.borderTopLeftRadius || '9px') * scale}px`,
  };
  const width = Math.min(340 * scale, window.innerWidth - 16);
  // Apply visual metrics and width before measuring wrapping and the complete list.
  for (const [key, value] of Object.entries(appearance)) menu.style.setProperty(key, value);
  menu.style.width = width + 'px';
  const css = getComputedStyle(menu);
  const desiredHeight = Math.ceil(list.scrollHeight + (menu.querySelector<HTMLElement>('.trainer-menu-heading')?.offsetHeight || 0)
    + (menu.querySelector<HTMLElement>('.trainer-menu-cancel')?.offsetHeight || 0) + pixels(css.rowGap) * 2
    + pixels(css.paddingTop) + pixels(css.paddingBottom) + pixels(css.borderTopWidth) + pixels(css.borderBottomWidth));
  const rect = anchor?.isConnected ? anchor.getBoundingClientRect() : null;
  const placement = getGamepadPopupPlacement(rect, width, desiredHeight, 8 * scale);
  const safe = getGamepadViewportSafeArea(), height = Math.max(1, Math.min(desiredHeight, safe.bottom - safe.top));
  const left = rect ? Math.max(safe.left, Math.min(rect.left, safe.right - Number.parseFloat(placement.style.width))) + 'px' : placement.style.left;
  if (Number.parseFloat(placement.style.maxHeight) + 1 < height) {
    // Long lists may overlap their opener and use the full safe viewport. Keep
    // left alignment with the opener, then move upward only as far as needed.
    const top = Math.max(safe.top, Math.min(rect ? rect.bottom + 8 * scale : safe.top, safe.bottom - height));
    style.value = { ...appearance, position: 'fixed', left, width: placement.style.width, top: top + 'px', maxHeight: height + 'px' };
  } else style.value = { ...appearance, ...placement.style, left };
}
function anchorScrolled(event: Event): void {
  const target = event.target;
  if (anchor && (target === document || target instanceof Element && target.contains(anchor))) position();
}
function close(): void {
  if (closed) return;
  closed = true;
  emit('close');
  void nextTick(() => { if (anchor?.isConnected) focusGamepadElement(anchor); });
}
function back(event: Event): void {
  if (closed || scanTarget.value) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  close();
}
function keydown(event: KeyboardEvent): void {
  if (event.key === 'Escape') back(event);
}
function syncSettings(): void { autoEnabled.value = getUiSetting('trainerAutoPrepare'); }
async function toggleAuto(): Promise<void> {
  if (saving.value) return;
  saving.value = true;
  try {
    await setUiSettings({ trainerAutoPrepare: !autoEnabled.value });
    syncSettings();
    emit('status', { message: autoEnabled.value ? '已开启自动下载修改器' : '已关闭自动下载修改器，已下载文件仍可启动' });
  } catch (error) { emit('status', { error: `修改自动下载设置失败：${(error as Error).message}` }); }
  finally { saving.value = false; }
}
async function openManager(): Promise<void> {
  if (opening.value) return;
  opening.value = true;
  try { await openGameTrainerManager(); emit('status', { message: '已打开修改器管理器' }); close(); }
  catch (error) { emit('status', { error: (error as Error).message }); }
  finally { opening.value = false; }
}
async function openCe(): Promise<void> {
  if (opening.value) return;
  opening.value = true;
  try { await openCheatEngine(); emit('status', { message: '已打开 Cheat Engine' }); close(); }
  catch (error) { emit('status', { error: (error as Error).message }); }
  finally { opening.value = false; }
}
async function openScanner(): Promise<void> {
  if (opening.value || (!props.game && !getGameValueSessionTarget()) || !ceScanAvailable.value) return;
  opening.value = true;
  try {
    const target = getGameValueSessionTarget() || await captureAndLockGameTarget(props.game);
    if (!target) throw new Error('当前游戏已退出，请重新获取游戏');
    scanTarget.value = target;
  } catch (error) { emit('status', { error: (error as Error).message }); }
  finally { opening.value = false; }
}
function closeScanner(): void { scanTarget.value = null; void nextTick(() => { position(); focusGamepadElement(panel.value?.querySelector<HTMLElement>('[data-trainer-value-scan]') || null); }); }
onMounted(() => {
  anchor = document.querySelector<HTMLElement>('[data-gp-game-control="trainer-menu"]');
  position();
  void nextTick(() => { position(); focusGamepadElement(panel.value?.querySelector<HTMLElement>('button:not(:disabled)') || null); });
  // The production controller engine dispatches this event for native and browser B.
  // Capture consumes it before the enclosing game menu can close as well.
  window.addEventListener('ipc:gamepad-back', back, true);
  window.addEventListener('keydown', keydown, true);
  menuResize = new ResizeObserver(position);
  if (panel.value) menuResize.observe(panel.value);
  if (items.value) menuResize.observe(items.value);
  window.addEventListener('scroll', anchorScrolled, true);
  window.addEventListener('resize', position);
  window.addEventListener('ui-settings:changed', syncSettings);
  void loadUiSettings().then(syncSettings).catch(() => {});
  void isGameValueScanAvailable().then(value => { if (!closed) { ceScanAvailable.value = value; void nextTick(position); } }).catch(() => {});
});
onBeforeUnmount(() => {
  window.removeEventListener('ipc:gamepad-back', back, true);
  window.removeEventListener('keydown', keydown, true);
  menuResize?.disconnect(); menuResize = null;
  window.removeEventListener('scroll', anchorScrolled, true);
  window.removeEventListener('resize', position);
  window.removeEventListener('ui-settings:changed', syncSettings);
});
</script>

<template>
  <Teleport to="body">
    <div v-show="!scanTarget" class="trainer-menu-shade" @click.self="close">
      <section ref="panel" class="trainer-menu" :style="style" data-gp-modal data-gp-scope="trainer-menu" data-gp-game-quick-dialog data-trainer-menu role="dialog" aria-modal="true" aria-label="游戏修改器菜单">
        <div class="trainer-menu-heading"><AppIcon name="gamepad" /><strong>游戏修改器</strong></div>
        <div ref="items" class="trainer-menu-items">
        <button type="button" class="trainer-menu-action" data-gp-row="0" data-gp-col="0" :disabled="!hasGame" @click="emit('launch')">
          <AppIcon :name="busy ? 'close' : 'play'" /><span><strong>{{ launchLabel }}</strong><small>{{ hint }}</small><small v-if="error || message" class="trainer-menu-result" :class="{ 'trainer-menu-error': error }" role="status" aria-live="polite">{{ error || message }}</small></span>
        </button>
        <button type="button" class="trainer-menu-action" data-gp-row="1" data-gp-col="0" :disabled="opening" @click="openManager"><AppIcon name="folder" /><span><strong>打开修改器管理器</strong><small>Game Cheats Manager</small></span></button>
        <button v-if="ceScanAvailable" type="button" class="trainer-menu-action" data-gp-row="2" data-gp-col="0" data-trainer-value-scan :disabled="(!game && !getGameValueSessionTarget()) || opening" @click="openScanner"><AppIcon name="search" /><span><strong>简单改数值</strong><small>填数字 · 变化一次 · 一起修改</small></span></button>
        <button type="button" class="trainer-menu-action" data-gp-row="3" data-gp-col="0" data-trainer-ce :disabled="opening" @click="openCe"><AppIcon name="bolt" /><span><strong>启动 Cheat Engine</strong><small>与修改器管理器共用 CE 配置</small></span></button>
        <button type="button" class="trainer-menu-action" data-gp-row="4" data-gp-col="0" :disabled="saving" :aria-pressed="autoEnabled" @click="toggleAuto"><AppIcon name="rotate" /><span><strong>{{ autoEnabled ? '关闭自动下载修改器' : '开启自动下载修改器' }}</strong><small>{{ autoEnabled ? '识别游戏后，在额度内后台下载' : '自动下载已关闭' }}</small></span></button>
        </div>
        <button type="button" class="trainer-menu-cancel" data-gp-row="5" data-gp-col="0" @click="close"><span class="controller-key trainer-key-b" data-controller-key="B">B</span>关闭页面</button>
      </section>
    </div>
  </Teleport>
  <GameValueScanner v-if="scanTarget" :target="scanTarget" @close="closeScanner" />
</template>

<style scoped>
.trainer-menu-shade { position: fixed; inset: 0; z-index: 30000; background: rgba(0,0,0,.18); }
.trainer-menu { box-sizing: border-box; display: flex; flex-direction: column; gap: .571429em; padding: .857143em; max-width: calc(100vw - 16px); max-height: calc(100vh - 16px); overflow: hidden; border: .071429em solid var(--console-line, var(--border)); border-radius: .857143em; background: var(--bg-solid); color: var(--text); font-size: 14px; line-height: 1.4; box-shadow: 0 16px 44px #0006; }
.trainer-menu-items { display: flex; flex-direction: column; gap: .571429em; min-height: 0; overflow-y: auto; overscroll-behavior: contain; scroll-padding: .285714em; }
.trainer-menu-heading { flex: 0 0 auto; display: flex; align-items: center; gap: .5em; padding: .214286em .142857em .571429em; font-size: 1em; }
.trainer-menu-heading strong { font-weight: 600; }
.trainer-menu :deep(svg) { width: 1.214286em; height: 1.214286em; flex: 0 0 auto; color: var(--accent); }
.trainer-menu button { font: inherit; color: inherit; cursor: pointer; border: .071429em solid var(--console-line, var(--border)); border-radius: var(--trainer-action-radius, .642857em); background: var(--console-surface-alt, var(--bg-input)); min-height: 4.214286em; flex: 0 0 auto; }
.trainer-menu button:focus-visible, .trainer-menu button.gp-focus, .trainer-menu button:hover { outline: .142857em solid var(--accent); outline-offset: -.142857em; }
.trainer-menu button:disabled { opacity: .5; cursor: default; }
.trainer-menu-action { display: flex; align-items: center; gap: .571429em; padding: .714286em; text-align: left; }
.trainer-menu-action span { display: grid; gap: .214286em; min-width: 0; overflow-wrap: anywhere; }
.trainer-menu-action strong { font-size: .928571em; line-height: 1.4; font-weight: 600; display: -webkit-box; -webkit-box-orient: vertical; -webkit-line-clamp: 2; overflow: hidden; }
.trainer-menu-action small { font-size: .857143em; line-height: 1.4; color: var(--text-dim); }
.trainer-menu-action .trainer-menu-result { white-space: pre-line; }
.trainer-menu-action .trainer-menu-error { color: var(--danger); }
.trainer-menu .trainer-menu-cancel { display: flex; align-items: center; justify-content: center; gap: .307692em; min-height: 2.615385em; padding: .538462em .692308em; font-size: .928571em; line-height: 1.35; }
.trainer-key-b { color: var(--danger); font-weight: 800; }
</style>
