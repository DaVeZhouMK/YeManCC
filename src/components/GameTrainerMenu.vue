<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import GameValueScanner from '@/components/GameValueScanner.vue';
import { isGameValueScanAvailable } from '@/bridge/gameValueScan';
import type { DetectedGame } from '@/bridge/gamedetect';
import { captureAndLockGameTarget, type LockedGameTarget } from '@/bridge/gameQuickSession';
import { focusGamepadElement, getGamepadPopupPlacement, getGamepadViewportSafeArea } from '@/gamepad/focus';
import { openCheatEngine, openGameTrainerManager } from '@/bridge/gameTrainer';
import { getUiSetting, loadUiSettings, setUiSettings } from '@/bridge/uiSettings';

const props = defineProps<{ gameName: string; busy: boolean; hint: string; hasGame: boolean; game?: DetectedGame | null }>();
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
  // Measure the complete list, not the height already clipped by a prior placement.
  const css = getComputedStyle(menu), pixels = (value: string) => Number.parseFloat(value) || 0;
  const desiredHeight = Math.ceil(list.scrollHeight + (menu.querySelector<HTMLElement>('.trainer-menu-heading')?.offsetHeight || 0)
    + (menu.querySelector<HTMLElement>('.trainer-menu-cancel')?.offsetHeight || 0) + pixels(css.rowGap) * 2
    + pixels(css.paddingTop) + pixels(css.paddingBottom) + pixels(css.borderTopWidth) + pixels(css.borderBottomWidth));
  const rect = anchor?.isConnected ? anchor.getBoundingClientRect() : null;
  const placement = getGamepadPopupPlacement(rect, Math.min(340, window.innerWidth - 16), desiredHeight, 8);
  const safe = getGamepadViewportSafeArea(), height = Math.max(1, Math.min(desiredHeight, safe.bottom - safe.top));
  const left = rect ? Math.max(safe.left, Math.min(rect.left, safe.right - Number.parseFloat(placement.style.width))) + 'px' : placement.style.left;
  if (Number.parseFloat(placement.style.maxHeight) + 1 < height) {
    // Long lists may overlap their opener and use the full safe viewport. Keep
    // left alignment with the opener, then move upward only as far as needed.
    const top = Math.max(safe.top, Math.min(rect ? rect.bottom + 8 : safe.top, safe.bottom - height));
    style.value = { position: 'fixed', left, width: placement.style.width, top: top + 'px', maxHeight: height + 'px' };
  } else style.value = { ...placement.style, left };
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
  if (opening.value || !props.game || !ceScanAvailable.value) return;
  opening.value = true;
  try {
    const target = await captureAndLockGameTarget(props.game);
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
          <AppIcon :name="busy ? 'close' : 'play'" /><span><strong>{{ launchLabel }}</strong><small>{{ hint }}</small></span>
        </button>
        <button type="button" class="trainer-menu-action" data-gp-row="1" data-gp-col="0" :disabled="opening" @click="openManager"><AppIcon name="folder" /><span><strong>打开修改器管理器</strong><small>Game Cheats Manager</small></span></button>
        <button v-if="ceScanAvailable" type="button" class="trainer-menu-action" data-gp-row="2" data-gp-col="0" data-trainer-value-scan :disabled="!game || opening" @click="openScanner"><AppIcon name="search" /><span><strong>简单改数值</strong><small>填数字 · 变化一次 · 一起修改</small></span></button>
        <button type="button" class="trainer-menu-action" data-gp-row="3" data-gp-col="0" data-trainer-ce :disabled="opening" @click="openCe"><AppIcon name="bolt" /><span><strong>启动 Cheat Engine</strong><small>与修改器管理器共用 CE 配置</small></span></button>
        <button type="button" class="trainer-menu-action" data-gp-row="4" data-gp-col="0" :disabled="saving" :aria-pressed="autoEnabled" @click="toggleAuto"><AppIcon name="rotate" /><span><strong>{{ autoEnabled ? '关闭自动下载修改器' : '开启自动下载修改器' }}</strong><small>{{ autoEnabled ? '识别游戏后，在额度内后台下载' : '自动下载已关闭' }}</small></span></button>
        </div>
        <button type="button" class="trainer-menu-cancel" data-gp-row="5" data-gp-col="0" @click="close"><strong class="trainer-key-b">B</strong>关闭页面</button>
      </section>
    </div>
  </Teleport>
  <GameValueScanner v-if="scanTarget" :target="scanTarget" @close="closeScanner" />
</template>

<style scoped>
.trainer-menu-shade { position: fixed; inset: 0; z-index: 30000; background: rgba(0,0,0,.18); }
.trainer-menu { box-sizing: border-box; display: flex; flex-direction: column; gap: 7px; padding: 12px; max-width: calc(100vw - 16px); max-height: calc(100vh - 16px); overflow: hidden; border: 1px solid rgba(255,255,255,.22); border-radius: 14px; background: var(--bg-solid); color: var(--text); box-shadow: 0 16px 48px rgba(0,0,0,.38); }
.trainer-menu-items { display: flex; flex-direction: column; gap: 7px; min-height: 0; overflow-y: auto; overscroll-behavior: contain; scroll-padding: 4px; }
.trainer-menu-heading { flex: 0 0 auto; display: flex; align-items: center; gap: 7px; padding: 3px 2px 8px; font-size: 13px; }
.trainer-menu :deep(svg) { width: 17px; height: 17px; flex: 0 0 auto; color: var(--accent); }
.trainer-menu button { font: inherit; color: inherit; cursor: pointer; border: 1px solid rgba(255,255,255,.22); border-radius: 9px; background: var(--bg-input); min-height: 44px; flex: 0 0 auto; }
.trainer-menu button:focus-visible, .trainer-menu button.gp-focus, .trainer-menu button:hover { outline: 2px solid var(--accent); outline-offset: -2px; }
.trainer-menu button:disabled { opacity: .5; cursor: default; }
.trainer-menu-action { display: flex; align-items: center; gap: 8px; padding: 9px; text-align: left; }
.trainer-menu-action span { display: grid; gap: 3px; min-width: 0; overflow-wrap: anywhere; }
.trainer-menu-action strong { font-size: 12px; font-weight: 650; display: -webkit-box; -webkit-box-orient: vertical; -webkit-line-clamp: 2; overflow: hidden; }
.trainer-menu-action small { font-size: 10px; color: var(--text-dim); }
.trainer-menu .trainer-menu-cancel { display: flex; align-items: center; justify-content: center; gap: 3px; min-height: 30px; font-size: 10px; border-color: rgba(255,255,255,.08); border-radius: 7px; }
.trainer-key-b { color: var(--danger); font-weight: 800; }
</style>
