<script setup lang="ts">
import { ref, computed, nextTick, onBeforeUnmount, onMounted } from 'vue';

export interface DropdownOption {
  value: string | number;
  label: string;
  sub?: string;
  disabled?: boolean;
}

const props = defineProps<{
  modelValue: string | number;
  options: DropdownOption[];
  disabled?: boolean;
  color?: 'ac' | 'dc' | 'accent';
  ariaLabel?: string;
  placeholder?: string;
  /** 可选固定宽度（如 '120px'）；不传则填满父容器 */
  width?: string;
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

const selectedIndex = computed(() =>
  Math.max(0, props.options.findIndex((o) => o.value === props.modelValue))
);
const selectedLabel = computed(() => {
  const o = props.options.find((op) => op.value === props.modelValue);
  return o ? o.label : props.placeholder ?? '—';
});

function colorVar(): string {
  if (props.color === 'dc') return 'var(--accent-2)';
  return 'var(--accent)';
}

function openMenu() {
  if (props.disabled || open.value) return;
  computePosition();
  highlight.value = selectedIndex.value;
  open.value = true;
  nextTick(() => {
    const el = menuEl.value?.querySelector<HTMLElement>('[data-hl="true"]');
    el?.scrollIntoView({ block: 'nearest' });
  });
}

function closeMenu() {
  open.value = false;
}

// 计算弹层 fixed 定位（对齐 trigger；空间不足则向上翻转）
function computePosition() {
  const r = triggerEl.value?.getBoundingClientRect();
  if (!r) return;
  const MENU_MAX = 240;
  const style: Record<string, string> = {
    position: 'fixed',
    left: r.left + 'px',
    width: r.width + 'px',
  };
  const spaceBelow = window.innerHeight - r.bottom;
  if (spaceBelow < MENU_MAX + 8 && r.top > spaceBelow) {
    // 向上：bottom = 视口底 - trigger顶 + 6
    style.bottom = window.innerHeight - r.top + 6 + 'px';
    style.top = 'auto';
  } else {
    style.top = r.bottom + 6 + 'px';
    style.bottom = 'auto';
  }
  menuStyle.value = style;
}

function toggle() {
  if (open.value) closeMenu();
  else openMenu();
}

function select(i: number) {
  const o = props.options[i];
  if (!o || o.disabled) return;
  emit('update:modelValue', o.value);
  emit('change', o.value);
  closeMenu();
  triggerEl.value?.focus({ preventScroll: true });
}

function move(dir: 1 | -1) {
  let i = highlight.value;
  const n = props.options.length;
  for (let k = 0; k < n; k++) {
    i = (i + dir + n) % n;
    if (!props.options[i].disabled) break;
  }
  highlight.value = i;
  const el = menuEl.value?.querySelectorAll<HTMLElement>('.dd-option')[i];
  el?.scrollIntoView({ block: 'nearest' });
}

function onTriggerKey(e: KeyboardEvent) {
  if (props.disabled) return;
  if (e.key === 'Enter' || e.key === ' ' || e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    e.preventDefault();
    openMenu();
    if (e.key === 'ArrowDown') move(1);
    else if (e.key === 'ArrowUp') move(-1);
  }
}

function onMenuKey(e: KeyboardEvent) {
  if (e.key === 'Escape') {
    e.preventDefault();
    closeMenu();
    triggerEl.value?.focus({ preventScroll: true });
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

// 点击外部关闭（弹层已 teleport 到 body，故需同时判断 rootEl 与 menuEl）
function onDocPointer(e: PointerEvent) {
  if (!open.value) return;
  const t = e.target as Node;
  if ((rootEl.value && rootEl.value.contains(t)) || (menuEl.value && menuEl.value.contains(t))) return;
  closeMenu();
}
// 滚动关闭（避免错位）
function onScroll() {
  if (open.value) closeMenu();
}

onMounted(() => {
  document.addEventListener('pointerdown', onDocPointer);
  window.addEventListener('scroll', onScroll, true);
});
onBeforeUnmount(() => {
  document.removeEventListener('pointerdown', onDocPointer);
  window.removeEventListener('scroll', onScroll, true);
});

// ── 手柄 X/A 循环（对齐原生 select 的旧行为）──
function cycle() {
  if (props.disabled) return;
  const n = props.options.length;
  if (n === 0) return;
  let i = selectedIndex.value;
  for (let k = 0; k < n; k++) {
    i = (i + 1) % n;
    if (!props.options[i].disabled) break;
  }
  select(i);
}
function onGpCycle(e: Event) {
  e.preventDefault();
  cycle();
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
    @gp:dropdown-cycle="onGpCycle"
  >
    <button
      ref="triggerEl"
      type="button"
      class="dd-trigger"
      :aria-label="ariaLabel"
      :disabled="disabled"
      :aria-expanded="open"
      aria-haspopup="listbox"
      @click="toggle"
      @keydown="onTriggerKey"
    >
      <span class="dd-value" :class="{ 'dd-placeholder': options.findIndex((o) => o.value === modelValue) < 0 }">
        {{ selectedLabel }}
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
          class="dd-menu"
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
.dd-dc { --dd-accent: var(--accent-2); }
.dd-ac { --dd-accent: var(--accent); }

.dd-trigger {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 8px 10px;
  background: var(--bg-input);
  color: var(--text);
  border: 1px solid #2a3342;
  border-radius: var(--radius-ctrl);
  font-size: 12px;
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
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
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
  border-radius: 10px;
  padding: 5px;
  box-shadow: 0 14px 36px rgba(0, 0, 0, 0.5);
  max-height: 240px;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.dd-option {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  width: 100%;
  padding: 9px 10px;
  background: transparent;
  border: none;
  border-radius: var(--radius-ctrl);
  color: var(--text);
  font-size: 13px;
  font-family: inherit;
  text-align: left;
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
  font-size: 12px;
  font-weight: 400;
  color: var(--text-dim);
  margin-left: 6px;
}
.dd-opt-disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
.dd-check {
  flex: 0 0 auto;
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
  transform: translateY(-4px);
}
</style>
