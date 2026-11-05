<script setup lang="ts">
import { nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import losslessAppIcon from '@/assets/apps/lossless-scaling.png';
import OverflowGameName from '@/components/OverflowGameName.vue';
import { focusGamepadElement, getGamepadPopupPlacement, getGamepadViewportSafeArea } from '@/gamepad/focus';

const props = defineProps<{ gameName: string; removable: boolean; disabled: boolean }>();
const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'action', action: 'off' | 'remove'): void;
}>();
const panel = ref<HTMLElement | null>(null);
const style = ref<Record<string, string>>({ position: 'fixed' });
let anchor: HTMLElement | null = null;
let resize: ResizeObserver | null = null;
let closed = false;

function position(): void {
  const menu = panel.value;
  if (closed || !menu?.offsetWidth) return;
  // Measure intrinsic content so a previous placement cannot clip later sizing.
  const css = getComputedStyle(menu), px = (value: string) => Number.parseFloat(value) || 0;
  const height = [...menu.children].reduce((n, el) => n + (el as HTMLElement).offsetHeight, 0)
    + px(css.rowGap) * (menu.children.length - 1) + px(css.paddingTop) + px(css.paddingBottom)
    + px(css.borderTopWidth) + px(css.borderBottomWidth);
  const rect = anchor?.isConnected ? anchor.getBoundingClientRect() : null;
  const placement = getGamepadPopupPlacement(rect, Math.min(300, window.innerWidth - 16), height, 8);
  const safe = getGamepadViewportSafeArea();
  // Use the safe viewport if neither side has room, rather than clipping actions.
  style.value = Number.parseFloat(placement.style.maxHeight) + 1 < height
    ? { position: 'fixed', left: placement.style.left, width: placement.style.width,
        top: Math.max(safe.top, Math.min(rect?.bottom ?? safe.top, safe.bottom - height)) + 'px',
        maxHeight: Math.max(1, safe.bottom - safe.top) + 'px' }
    : placement.style;
}
function anchorScrolled(event: Event): void {
  if (anchor && (event.target === document || event.target instanceof Element && event.target.contains(anchor))) position();
}
function close(): void {
  if (closed) return;
  closed = true;
  emit('close');
  void nextTick(() => { if (anchor?.isConnected) focusGamepadElement(anchor); });
}
function action(value: 'off' | 'remove'): void {
  if (closed || props.disabled || value === 'remove' && !props.removable) return;
  closed = true;
  emit('action', value);
}
function back(event: Event): void {
  if (closed) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  close();
}
function keydown(event: KeyboardEvent): void { if (event.key === 'Escape') back(event); }
onMounted(() => {
  anchor = document.querySelector<HTMLElement>('[data-gp-game-control="lossless-scaling"]');
  position();
  void nextTick(() => { position(); focusGamepadElement(panel.value?.querySelector<HTMLElement>('button:not(:disabled)') || null); });
  window.addEventListener('ipc:gamepad-back', back, true);
  window.addEventListener('keydown', keydown, true);
  window.addEventListener('resize', position);
  window.addEventListener('scroll', anchorScrolled, true);
  resize = new ResizeObserver(position);
  if (panel.value) resize.observe(panel.value);
});
onBeforeUnmount(() => {
  window.removeEventListener('ipc:gamepad-back', back, true);
  window.removeEventListener('keydown', keydown, true);
  window.removeEventListener('resize', position);
  window.removeEventListener('scroll', anchorScrolled, true);
  resize?.disconnect();
});
</script>

<template>
  <Teleport to="body">
    <div class="lossless-menu-shade" @click.self="close">
      <section ref="panel" class="lossless-menu" :style="style" data-gp-modal data-gp-scope="lossless-menu" data-gp-game-quick-dialog data-lossless-menu role="dialog" aria-modal="true" aria-label="小黄鸭操作">
        <div class="lossless-menu-heading"><img class="lossless-app-icon" :src="losslessAppIcon" alt="" aria-hidden="true" /><strong>一键插帧-小黄鸭</strong></div>
        <div class="lossless-menu-game"><OverflowGameName :name="gameName" /></div>
        <button type="button" class="lossless-menu-action" data-gp-row="0" data-gp-col="0" data-lossless-off :disabled="disabled" @click="action('off')"><span>关闭小黄鸭</span></button>
        <button type="button" class="lossless-menu-action danger" data-gp-row="1" data-gp-col="0" data-gp-game-control="lossless-remove" :disabled="disabled || !removable" @click="action('remove')"><AppIcon name="trash" /><span>删除此游戏</span></button>
        <button type="button" class="lossless-menu-cancel" data-gp-row="2" data-gp-col="0" @click="close"><b>B</b> 关闭</button>
      </section>
    </div>
  </Teleport>
</template>

<style scoped>
.lossless-menu-shade{position:fixed;inset:0;z-index:30000;background:rgba(0,0,0,.18)}
.lossless-menu{box-sizing:border-box;display:flex;flex-direction:column;gap:8px;padding:12px;max-width:calc(100vw - 16px);overflow-y:auto;overscroll-behavior:contain;scroll-padding:8px;border:1px solid var(--console-line,rgba(255,255,255,.22));border-radius:14px;background:var(--bg-solid);color:var(--text);box-shadow:0 16px 48px rgba(0,0,0,.38)}
.lossless-menu-heading{display:flex;align-items:center;gap:8px;font-size:14px;line-height:1.4;padding:2px 0}
.lossless-app-icon{width:17px;height:17px;object-fit:contain;flex:0 0 17px}
.lossless-menu-game{min-width:0;color:var(--text-dim);font-size:12px;line-height:1.4;margin-bottom:3px}
.lossless-menu :deep(svg){width:17px;height:17px;flex:0 0 auto;color:var(--accent)}
.lossless-menu button{flex:0 0 auto;min-height:42px;padding:9px 10px;border:1px solid var(--console-line,rgba(255,255,255,.16));border-radius:9px;background:var(--console-surface-alt,var(--bg-input));color:var(--text);font:inherit;font-size:13px;line-height:1.4;cursor:pointer}
.lossless-menu button:focus-visible,.lossless-menu button.gp-focus,.lossless-menu button:hover{outline:2px solid var(--accent);outline-offset:-2px}
.lossless-menu button:disabled{opacity:.5;cursor:default}
.lossless-menu-action{display:flex;align-items:center;gap:9px;text-align:left}
.lossless-menu .danger,.lossless-menu .danger :deep(svg){color:var(--danger)}
.lossless-menu .lossless-menu-cancel{display:flex;align-items:center;justify-content:center;gap:5px;min-height:32px;padding:5px;font-size:12px}
.lossless-menu-cancel b{color:var(--danger)}
</style>
