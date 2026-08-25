<script setup lang="ts">
import { ref, computed, nextTick, onBeforeUnmount, onMounted } from 'vue';
import { focusGamepadElement, getGamepadPopupPlacement } from '@/gamepad/focus';

export interface DropdownOption {
  value: string | number;
  label: string;
  sub?: string;
  disabled?: boolean;
}

const props = defineProps<{
  modelValue: string | number;
  options: readonly DropdownOption[];
  disabled?: boolean;
  color?: 'ac' | 'dc' | 'accent';
  ariaLabel?: string;
  placeholder?: string;
  /** 在触发器内显示当前选项的说明文字。 */
  showSelectedSub?: boolean;
  /** 可选的触发器专用说明文字；下拉菜单仍使用选项自身的 sub。 */
  selectedSubText?: string;
  /** 可选固定宽度（如 '120px'）；不传则填满父容器 */
  width?: string;
  /** Optional class for the teleported popup (the menu is rendered under body). */
  popupClass?: string;
  /** Optional unified gamepad spatial-navigation coordinates for the trigger. */
  gpRow?: number | string;
  gpCol?: number | string;
}>();

const emit = defineEmits<{
  (e: 'update:modelValue', v: string | number): void;
  (e: 'change', v: string | number): void;
}>();

const open = ref(false);
const rootEl = ref<HTMLElement | null>(null);
const triggerEl = ref<HTMLElement | null>(null);
const menuEl = ref<HTMLElement | null>(null);
const highlight = ref(0);
// 弹层用 fixed 定位到视口（teleport 到 body），避免被 overflow 滚动容器裁切
const menuStyle = ref<Record<string, string>>({});
let measureCanvas: HTMLCanvasElement | null = null;

const selectedIndex = computed(() => {
  // modelValue 不在选项中时返回 -1：菜单打开不高亮任何项，避免误导「第一个就是当前值」
  const idx = props.options.findIndex((o) => o.value === props.modelValue);
  return idx >= 0 ? idx : -1;
});
const selectedLabel = computed(() => {
  const o = props.options.find((op) => op.value === props.modelValue);
  return o ? o.label : props.placeholder ?? '—';
});
const selectedSub = computed(() => {
  const o = props.options.find((op) => op.value === props.modelValue);
  return props.selectedSubText ?? o?.sub ?? '';
});

function colorVar(): string {
  if (props.color === 'dc') return 'var(--dc-accent)';
  return 'var(--accent)';
}

function getGlobalUiScale(): number {
  const rootValue = Number.parseFloat(
    getComputedStyle(document.documentElement).getPropertyValue('--ui-scale'),
  );
  if (Number.isFinite(rootValue) && rootValue > 0) {
    return Math.max(0.5, Math.min(4, rootValue));
  }

  // 兼容尚未发布 --ui-scale 的首帧：直接从全局缩放容器读取视觉比例。
  const stage = document.querySelector<HTMLElement>('.app-stage');
  if (!stage || stage.clientWidth <= 0) return 1;
  const visualWidth = stage.getBoundingClientRect().width;
  return visualWidth > 0
    ? Math.max(0.5, Math.min(4, visualWidth / stage.clientWidth))
    : 1;
}

function measureOptionText(text: string, fontSize: number, fontWeight: number): number {
  measureCanvas ??= document.createElement('canvas');
  const context = measureCanvas.getContext('2d');
  if (!context) return text.length * fontSize;
  const family = getComputedStyle(document.body).fontFamily || 'sans-serif';
  context.font = `${fontWeight} ${fontSize}px ${family}`;
  return context.measureText(text).width;
}

function getMenuWidth(triggerWidth: number, scale: number): number {
  const optionFont = 14 * scale;
  const menuPadding = 5 * scale;
  const optionPadding = 12 * scale;
  const optionGap = 8 * scale;
  const subGap = 6 * scale;
  const checkSize = 14 * scale;
  const widthSafety = 12 * scale;
  let contentWidth = 0;

  for (const option of props.options) {
    const selected = option.value === props.modelValue;
    let width = measureOptionText(option.label, optionFont, selected ? 600 : 400);
    if (option.sub) {
      width += subGap + measureOptionText(option.sub, optionFont, 400);
    }
    // 每个选项统一预留勾选槽位，避免某一项被选中后文字列突然缩窄。
    width += optionGap + checkSize;
    contentWidth = Math.max(contentWidth, width);
  }

  // 保持单行并允许弹层宽于触发框；定位函数仍会将最终宽度限制在视口安全区内。
  return Math.ceil(Math.max(
    triggerWidth,
    contentWidth + menuPadding * 2 + optionPadding * 2 + widthSafety + 2,
  ));
}

function openMenu(fromGamepad = false) {
  if (props.disabled || open.value) return;
  computePosition();
  // modelValue 不在选项中时（selectedIndex=-1）不高亮任何项，避免误导
  highlight.value = Math.max(0, selectedIndex.value);
  open.value = true;
  nextTick(() => {
    const el = menuEl.value?.querySelector<HTMLElement>('[data-hl="true"]');
    // 焦点落到高亮项：手柄 A 打开菜单后可直接 A 确认 / B 取消 / 上下移动（与鼠标菜单一致）
    if (!el) return;
    if (fromGamepad) focusGamepadElement(el);
    else {
      el.scrollIntoView({ block: 'nearest' });
      el.focus({ preventScroll: true });
    }
  });
}

function closeMenu() {
  open.value = false;
}

function restoreTriggerFocus() {
  const trigger = triggerEl.value;
  if (!trigger) return;
  focusGamepadElement(trigger);
}

// 计算弹层 fixed 定位（对齐 trigger；空间不足则向上翻转）
function computePosition() {
  const r = triggerEl.value?.getBoundingClientRect();
  if (!r) return;
  const scale = getGlobalUiScale();
  const menuWidth = getMenuWidth(r.width, scale);
  const placement = getGamepadPopupPlacement(r, menuWidth, 360 * scale, 6 * scale);
  menuStyle.value = {
    ...placement.style,
    '--dd-accent': colorVar(),
    '--dd-popup-font-size': `${14 * scale}px`,
    '--dd-popup-menu-padding': `${5 * scale}px`,
    '--dd-popup-menu-gap': `${2 * scale}px`,
    '--dd-popup-menu-radius': `${10 * scale}px`,
    '--dd-popup-option-py': `${11 * scale}px`,
    '--dd-popup-option-px': `${12 * scale}px`,
    '--dd-popup-option-gap': `${8 * scale}px`,
    '--dd-popup-option-radius': `${8 * scale}px`,
    '--dd-popup-sub-gap': `${6 * scale}px`,
    '--dd-popup-icon-size': `${14 * scale}px`,
    '--dd-popup-enter-offset': `${4 * scale}px`,
  };
    // 向上：bottom = 视口底 - trigger顶 + 6
}

function toggle() {
  if (open.value) closeMenu();
  else openMenu();
}

defineExpose({ openMenu, closeMenu, toggle });

function select(i: number) {
  const o = props.options[i];
  if (!o || o.disabled) return;
  emit('update:modelValue', o.value);
  emit('change', o.value);
  closeMenu();
  restoreTriggerFocus();
}

function move(dir: 1 | -1) {
  if (!open.value || !menuEl.value) return;
  const n = props.options.length;
  if (n === 0) return; // 选项为空：不导航，避免除零/越界（2026-08-05 修复）
  // 选项列表在打开期间可能被父组件改短：先钳到合法范围再取模，避免指向陈旧下标
  let i = Math.max(0, Math.min(n - 1, highlight.value));
  for (let k = 0; k < n; k++) {
    i = (i + dir + n) % n;
    if (!props.options[i].disabled) break;
  }
  highlight.value = i;
  const el = menuEl.value?.querySelectorAll<HTMLElement>('.dd-option')[i];
  if (el) focusGamepadElement(el);
}

function onTriggerKey(e: KeyboardEvent) {
  if (props.disabled) return;
  if (e.key === 'Enter' || e.key === ' ' || e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    e.preventDefault();
    openMenu(true);
    if (e.key === 'ArrowDown') nextTick(() => move(1));
    else if (e.key === 'ArrowUp') nextTick(() => move(-1));
  }
}

function onMenuKey(e: KeyboardEvent) {
  if (e.key === 'Escape') {
    e.preventDefault();
    closeMenu();
    restoreTriggerFocus();
  } else if (e.key === 'ArrowDown') {
    e.preventDefault();
    move(1);
  } else if (e.key === 'ArrowUp') {
    e.preventDefault();
    move(-1);
  } else if (e.key === 'Enter' || e.key === ' ') {
    e.preventDefault();
    select(highlight.value);
  }
}

// ── 手柄 B 键：菜单打开时拦截返回，关闭菜单并把焦点交还触发器 ──
function onGpBack(e: Event) {
  if (open.value) {
    e.preventDefault();
    closeMenu();
    restoreTriggerFocus();
  }
}
// 点击外部关闭（弹层已 teleport 到 body，故需同时判断 rootEl 与 menuEl）
function onDocPointer(e: PointerEvent) {
  if (!open.value) return;
  const t = e.target as Node;
  if ((rootEl.value && rootEl.value.contains(t)) || (menuEl.value && menuEl.value.contains(t))) return;
  closeMenu();
}
// 滚动关闭（避免错位；排除菜单自身 scrollIntoView 引发的滚动事件——根因：高亮最后一项时
// scrollIntoView 触发 window scroll → 旧逻辑立即 closeMenu → 用户看到菜单闪现即消失 = "卡死"）
function onScroll(e: Event) {
  if (!open.value) return;
  const t = e.target as HTMLElement;
  if (menuEl.value && menuEl.value.contains(t)) return;
  closeMenu();
}

onMounted(() => {
  document.addEventListener('pointerdown', onDocPointer);
  window.addEventListener('scroll', onScroll, true);
  window.addEventListener('ipc:gamepad-back', onGpBack);
});
onBeforeUnmount(() => {
  document.removeEventListener('pointerdown', onDocPointer);
  window.removeEventListener('scroll', onScroll, true);
  window.removeEventListener('ipc:gamepad-back', onGpBack);
});

// ── 手柄 A/X：打开菜单（与鼠标点击一致），不再顺序轮转档位 ──
function onGpOpen(e: Event) {
  e.preventDefault();
  openMenu(true);
}
// ── 手柄上下导航（来自引擎 gp:dropdown-nav）：在菜单项内移动高亮并聚焦 ──
function onGpNav(e: Event) {
  const dir = (e as CustomEvent<{ dir: number }>).detail?.dir;
  if (dir !== 1 && dir !== -1) return;
  move(dir);
}
</script>

<template>
  <div
    ref="rootEl"
    class="dd"
    :class="[`dd-${color ?? 'accent'}`, { 'dd-open': open, 'dd-disabled': disabled }]"
    :style="width ? { width } : undefined"
    data-gp-dropdown
    @pointerdown.stop
    @gp:dropdown-open="onGpOpen"
    @gp:dropdown-nav="onGpNav"
  >
    <button
      ref="triggerEl"
      type="button"
      class="dd-trigger"
      data-gp-dropdown
      :aria-label="ariaLabel"
      :disabled="disabled"
      :aria-expanded="open"
      aria-haspopup="listbox"
      :data-gp-row="gpRow"
      :data-gp-col="gpCol"
      @click="toggle"
      @keydown="onTriggerKey"
    >
      <span class="dd-value" :class="{ 'dd-placeholder': options.findIndex((o) => o.value === modelValue) < 0 }">
        <span class="dd-selected-label">{{ selectedLabel }}</span>
        <span v-if="showSelectedSub && selectedSub" class="dd-selected-sub">{{ selectedSub }}</span>
      </span>
      <svg class="dd-caret" viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
        <path d="M4 6l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" />
      </svg>
    </button>

    <Teleport to="body">
      <Transition name="dd-pop">
        <div
          v-if="open"
          ref="menuEl"
          :class="['dd-menu', popupClass]"
          :style="menuStyle"
          role="listbox"
          tabindex="-1"
          @keydown="onMenuKey"
        >
          <button
            v-for="(o, i) in options"
            :key="String(o.value)"
            type="button"
            class="dd-option"
            :class="{ 'dd-opt-selected': o.value === modelValue, 'dd-opt-disabled': o.disabled }"
            :data-hl="i === highlight"
            role="option"
            :aria-selected="o.value === modelValue"
            @mouseenter="highlight = i"
            @click="select(i)"
          >
            <span class="dd-opt-label">
              {{ o.label }}
              <span v-if="o.sub" class="dd-opt-sub">{{ o.sub }}</span>
            </span>
            <svg v-if="o.value === modelValue" class="dd-check" viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
              <path d="M3.5 8.5l3 3 6-7" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" />
            </svg>
          </button>
        </div>
      </Transition>
    </Teleport>
  </div>
</template>

<style scoped>
.dd {
  position: relative;
  display: inline-block;
  width: 100%;
  --dd-accent: var(--accent);
}
.dd-dc { --dd-accent: var(--dc-accent); }
.dd-ac { --dd-accent: var(--accent); }

.dd-trigger {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: var(--btn-py) var(--btn-px);
  min-height: var(--btn-min-h);
  background: var(--bg-input);
  color: var(--text);
  border: 1px solid #2a3342;
  border-radius: var(--radius-ctrl);
  font-size: 13px;
  font-family: inherit;
  cursor: pointer;
  transition: border-color 0.12s, background 0.12s, box-shadow 0.12s;
}
.dd-trigger:hover:not(:disabled) {
  border-color: #3a4658;
  background: #1f2937;
}
.dd-trigger:focus-visible {
  outline: none;
  border-color: var(--dd-accent);
  box-shadow: 0 0 0 2px rgba(46, 166, 255, 0.25);
}
.dd-trigger:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.dd-value {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
  flex: 1 1 auto;
  overflow: hidden;
  white-space: nowrap;
}
.dd-selected-label { flex: 0 0 auto; }
.dd-selected-sub {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  color: var(--text-dim);
  font-size: 11px;
}
.dd-placeholder {
  color: var(--text-dim);
}
.dd-caret {
  flex: 0 0 auto;
  color: var(--text-dim);
  transition: transform 0.18s ease;
}
.dd-open .dd-caret {
  transform: rotate(180deg);
  color: var(--dd-accent);
}

.dd-menu {
  z-index: 1000;
  background: #161d29;
  border: 1px solid #2a3342;
  border-radius: var(--dd-popup-menu-radius, 10px);
  padding: var(--dd-popup-menu-padding, 5px);
  box-shadow: 0 14px 36px rgba(0, 0, 0, 0.5);
  max-height: 360px;
  min-height: 0;
  overflow-y: auto;
  overflow-x: hidden;
  display: flex;
  flex-direction: column;
  gap: var(--dd-popup-menu-gap, 2px);
}
.dd-option {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: var(--dd-popup-option-gap, 8px);
  width: 100%;
  padding: var(--dd-popup-option-py, 11px) var(--dd-popup-option-px, 12px);
  background: transparent;
  border: none;
  border-radius: var(--dd-popup-option-radius, var(--radius-ctrl));
  color: var(--text);
  font-size: var(--dd-popup-font-size, 14px);
  line-height: var(--btn-line-height);
  font-family: inherit;
  text-align: left;
  white-space: nowrap;
  overflow: hidden;
  cursor: pointer;
  transition: background 0.1s, color 0.1s;
}
.dd-option[data-hl='true'] {
  background: rgba(46, 166, 255, 0.14);
}
.dd-opt-selected {
  color: var(--dd-accent);
  font-weight: 600;
}
.dd-opt-sub {
  display: inline;
  font-size: inherit;
  font-weight: 400;
  color: var(--text-dim);
  margin-left: var(--dd-popup-sub-gap, 6px);
  white-space: nowrap;
}
.dd-opt-label {
  min-width: 0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.dd-opt-disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
.dd-check {
  flex: 0 0 auto;
  width: var(--dd-popup-icon-size, 14px);
  height: var(--dd-popup-icon-size, 14px);
  color: var(--dd-accent);
}

/* 弹出动画 */
.dd-pop-enter-active,
.dd-pop-leave-active {
  transition: opacity 0.14s ease, transform 0.14s ease;
}
.dd-pop-enter-from,
.dd-pop-leave-to {
  opacity: 0;
  transform: translateY(calc(-1 * var(--dd-popup-enter-offset, 4px)));
}
</style>
