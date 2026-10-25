<script setup lang="ts">
import SwitchControl from './SwitchControl.vue';
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
import { STEAM_APPEARANCE_THEMES, STEAM_APPEARANCE_DEFAULT_ENABLED, type AppearanceSnapshot, type AppearanceThemeName } from '@/bridge/steamAppearance';
import { focusGamepadElement } from '@/gamepad/focus';
const props = defineProps<{ snapshot: AppearanceSnapshot | null; changes?: Partial<Record<AppearanceThemeName, boolean>>; busy: boolean; notice: string; pending?: boolean; saving?: boolean }>();
const emit = defineEmits<{ (e: 'close'): void; (e: 'change', name: AppearanceThemeName, enabled: boolean): void; (e: 'refresh'): void; (e: 'reset', mode: 'preset' | 'previous'): void }>();
const panel = ref<HTMLElement | null>(null);
const resetPrompt = ref(false), resetButton = ref<HTMLButtonElement | null>(null);
async function openReset() { resetPrompt.value = true; await nextTick(); const first = panel.value?.querySelector<HTMLElement>('[data-appearance-reset-dialog] button:not(:disabled)'); if (first) focusGamepadElement(first); }
async function closeReset() { resetPrompt.value = false; await nextTick(); const target = resetButton.value?.disabled ? panel.value?.querySelector<HTMLElement>('.appearance-back') : resetButton.value; if (target) focusGamepadElement(target); }
function reset(mode: 'preset' | 'previous') { void closeReset(); emit('reset', mode); }
let appRoot: HTMLElement | null = null, rootWasInert = false;
const columns = ref(window.matchMedia("(max-width:480px)").matches ? 1 : 2);
function resize() { columns.value = window.matchMedia("(max-width:480px)").matches ? 1 : 2; }
const rows = computed(() => STEAM_APPEARANCE_THEMES.map(theme => {
  const state = props.snapshot?.themes.find(row => row.name === theme.name);
  return { ...theme, state, configured: props.changes?.[theme.name] ?? state?.configured ?? STEAM_APPEARANCE_DEFAULT_ENABLED };
}));
const count = computed(() => rows.value.filter(row => row.configured).length);
function refreshGamepad(event: Event) { event.preventDefault(); event.stopImmediatePropagation(); if (!props.busy && !resetPrompt.value) emit("refresh"); }
function back(event: Event) { event.preventDefault(); event.stopImmediatePropagation(); if (resetPrompt.value) void closeReset(); else emit('close'); }
function keydown(event: KeyboardEvent) {
  if (event.key === 'Escape') { back(event); return; }
  if (event.key !== 'Tab') return;
  const scope = panel.value?.querySelector<HTMLElement>('[data-appearance-reset-dialog]') || panel.value;
  const buttons = Array.from(scope?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') || []);
  if (!buttons.length) return;
  const index = buttons.indexOf(document.activeElement as HTMLButtonElement);
  event.preventDefault(); focusGamepadElement(buttons[(index + (event.shiftKey ? -1 : 1) + buttons.length) % buttons.length]);
}
onMounted(async () => {
  appRoot = document.getElementById("app"); rootWasInert = appRoot?.hasAttribute("inert") ?? false; appRoot?.setAttribute("inert", "");
  window.addEventListener('ipc:gamepad-back', back, true);
  window.addEventListener('ipc:gamepad-edit-game', refreshGamepad, true);
  window.addEventListener('keydown', keydown, true);
  window.addEventListener('resize', resize);
  await nextTick(); const first = panel.value?.querySelector<HTMLElement>('.appearance-theme-switch:not(:disabled)') || panel.value?.querySelector<HTMLElement>('button');
  if (first) focusGamepadElement(first);
});
onBeforeUnmount(() => { if (appRoot && !rootWasInert) appRoot.removeAttribute('inert'); window.removeEventListener('ipc:gamepad-back', back, true); window.removeEventListener('ipc:gamepad-edit-game', refreshGamepad, true); window.removeEventListener('keydown', keydown, true); window.removeEventListener('resize', resize); });
</script>

<template>
  <Teleport to="body">
    <section ref="panel" class="appearance-editor" data-gp-modal data-gp-page-lock data-gp-scope="steam-appearance-editor" role="dialog" aria-modal="true" aria-labelledby="appearance-editor-title">
      <header class="appearance-editor-header">
        <div><span class="appearance-eyebrow">STEAM · 外观编辑</span><h2 id="appearance-editor-title">定制你的大屏界面</h2><p>只切换效果，不改动你已经调好的参数。</p></div>
        <div class="appearance-header-actions"><button class="appearance-apply" :disabled="busy" data-gp-row="0" data-gp-col="1" @click="emit('refresh')">重新应用外观 <kbd>Y</kbd></button><button class="appearance-back" data-gp-row="0" data-gp-col="2" @click="emit('close')">返回 <kbd>B</kbd></button></div>
      </header>
      <div class="appearance-editor-scroll">
        <div class="appearance-editor-summary"><span><strong>{{ count }}</strong> / {{ rows.length }} 项{{ pending ? '已选择 · 待应用' : snapshot ? '已配置开启' : '默认开启 · 状态待确认' }}</span><span>全局外观 · 不随游戏切换</span></div>
        <div class="appearance-theme-grid">
          <article v-for="(theme, index) in rows" :key="theme.name" class="appearance-theme" :class="{ unavailable: !theme.state?.installed }">
            <div class="appearance-theme-copy"><span class="appearance-theme-group">{{ theme.group }}</span><h3>{{ theme.label }}</h3><p>{{ theme.description }}</p><span class="appearance-theme-original">{{ theme.name }}</span></div>
            <SwitchControl class="appearance-theme-switch" :model-value="theme.configured" :label="theme.label" :disabled="busy" :gp-row="1 + Math.floor(index / columns)" :gp-col="index % columns" @update:model-value="value => emit('change', theme.name, value)" />
            <span v-if="!theme.state?.installed && !snapshot?.packAvailable" class="appearance-theme-unavailable">未安装或配置不可读</span>
          </article>
        </div>
        <div class="appearance-parameter-note"><span class="appearance-preserved-dot"></span><span>保留全部现有参数。参数调整将使用独立编辑区，不占用此页。</span></div>
      </div>
      <footer class="appearance-editor-footer">
        <div><span v-if="busy">正在恢复预设…</span><span v-else-if="notice" role="status">{{ notice }}</span><span v-else-if="saving">后台应用中，可继续调整开关…</span><span v-else-if="pending">最后一次操作后 3 秒自动应用</span><span v-else>方向键 / 左摇杆移动 · A 切换 · B 返回 · Y 重新应用</span></div>
        <div class="appearance-footer-actions">
          <button ref="resetButton" class="appearance-reset" :disabled="busy || (!snapshot?.packAvailable && !snapshot?.canRestore)" :data-gp-row="1 + Math.ceil(rows.length / columns)" data-gp-col="0" @click="openReset">恢复预设</button>

        </div>
      </footer>
      <div v-if="resetPrompt" class="appearance-reset-shade">
        <section class="appearance-reset-dialog" data-gp-modal data-gp-scope="steam-appearance-reset" data-appearance-reset-dialog role="alertdialog" aria-modal="true" aria-labelledby="appearance-reset-title">
          <h3 id="appearance-reset-title">恢复外观</h3>
          <p>恢复 YMCC 预设会开启全部 {{ rows.length }} 项并恢复默认参数，同时暂停其它主题。原文件会备份，不会直接删除。尚未提交的开关选择将取消。</p>
          <button class="preset-confirm" :disabled="busy || !snapshot?.packAvailable" data-gp-row="0" data-gp-col="0" @click="reset('preset')">恢复 YMCC 预设</button>
          <button :disabled="busy || !snapshot?.canRestore" data-gp-row="1" data-gp-col="0" @click="reset('previous')">恢复接管前的外观</button>
          <button data-gp-row="2" data-gp-col="0" @click="closeReset">取消 <kbd>B</kbd></button>
        </section>
      </div>
    </section>
  </Teleport>
</template>

<style scoped>
.appearance-editor{position:fixed;inset:0;z-index:15000;background:var(--bg-solid);color:var(--text);display:flex;flex-direction:column;padding:clamp(16px,3vw,30px);box-sizing:border-box;gap:16px;font-family:inherit}
.appearance-editor-header{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;flex-shrink:0}.appearance-eyebrow{color:var(--accent);font-size:11px;letter-spacing:.13em}.appearance-editor-header h2{font-size:24px;margin:9px 0 7px;font-weight:600}.appearance-editor-header p{font-size:12px;color:var(--text-dim);margin:0}.appearance-header-actions{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.appearance-apply{border:2px solid var(--accent)!important;color:var(--accent)!important}.appearance-back,.appearance-apply,.appearance-editor-footer button{border:1px solid color-mix(in srgb,var(--text-dim) 25%,transparent);background:var(--bg-input);color:var(--text);border-radius:var(--radius-ctrl);padding:10px 14px;font-size:12px;cursor:pointer;white-space:nowrap}.appearance-back kbd{display:inline-flex;border:1px solid var(--text-dim);border-radius:50%;width:20px;height:20px;align-items:center;justify-content:center;margin-left:12px;font:inherit}.appearance-editor-scroll{overflow:auto;min-height:0;flex:1;padding:3px;scroll-padding:14px}.appearance-editor-summary{display:flex;justify-content:space-between;gap:12px;font-size:12px;color:var(--text-dim);margin:0 0 15px}.appearance-editor-summary strong{font-size:20px;color:var(--text)}.appearance-theme-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.appearance-theme{display:flex;align-items:center;justify-content:space-between;gap:12px;position:relative;min-height:112px;box-sizing:border-box;padding:16px;background:var(--bg-panel);border:1px solid color-mix(in srgb,var(--text-dim) 16%,transparent);border-radius:var(--radius)}.appearance-theme-copy{min-width:0}.appearance-theme-group{font-size:10px;color:var(--accent)}.appearance-theme h3{font-size:15px;margin:5px 0;font-weight:500}.appearance-theme p{font-size:11px;margin:0 0 7px;color:var(--text-dim)}.appearance-theme-original{font-size:10px;color:var(--text-dim);overflow-wrap:anywhere}button:disabled{opacity:.45;cursor:wait}.appearance-theme-unavailable{position:absolute;bottom:5px;color:var(--danger);font-size:10px}.appearance-parameter-note{display:flex;align-items:center;gap:8px;color:var(--text-dim);font-size:11px;margin-top:16px}.appearance-preserved-dot{width:6px;height:6px;border-radius:50%;background:var(--ok);flex:0 0 auto}.appearance-editor-footer{flex-shrink:0;display:flex;align-items:center;justify-content:space-between;gap:12px;border-top:1px solid color-mix(in srgb,var(--text-dim) 16%,transparent);padding-top:12px;font-size:11px;color:var(--text-dim)}.appearance-editor-footer kbd{font:inherit;border:1px solid var(--accent);border-radius:50%;width:18px;height:18px;display:inline-flex;align-items:center;justify-content:center;margin-left:8px}.appearance-editor-footer>div{min-width:0}.appearance-editor-footer button{border-color:color-mix(in srgb,var(--accent) 55%,transparent);color:var(--accent)}button:focus-visible,button.focused{outline:none;box-shadow:var(--focus-ring)}
@media(max-width:480px){.appearance-theme-grid{grid-template-columns:1fr}.appearance-editor-header h2{font-size:20px}.appearance-editor-summary{font-size:11px}.appearance-editor-footer{align-items:flex-start}.appearance-theme{min-height:104px}}
.appearance-footer-actions{display:flex;align-items:center;gap:8px;flex-shrink:0}.appearance-editor-footer .appearance-reset{color:var(--text-dim);border-color:color-mix(in srgb,var(--text-dim) 25%,transparent)}.appearance-reset-shade{position:absolute;inset:0;z-index:1;background:color-mix(in srgb,var(--bg-solid) 90%,transparent);display:grid;place-items:center;padding:20px;box-sizing:border-box}.appearance-reset-dialog{width:min(100%,390px);background:var(--bg-panel);border:1px solid color-mix(in srgb,var(--text-dim) 25%,transparent);border-radius:var(--radius);padding:22px;box-sizing:border-box;display:flex;flex-direction:column;gap:10px}.appearance-reset-dialog h3{font-size:18px;margin:0}.appearance-reset-dialog p{font-size:12px;line-height:1.6;color:var(--text-dim);margin:0 0 8px}.appearance-reset-dialog button{min-height:38px;background:var(--bg-input);border:1px solid color-mix(in srgb,var(--text-dim) 25%,transparent);color:var(--text);border-radius:var(--radius-ctrl);font-size:12px;cursor:pointer}.appearance-reset-dialog .preset-confirm{border-color:var(--accent);color:var(--accent)}.appearance-reset-dialog kbd{font:inherit;margin-left:12px;color:var(--text-dim)}
@media(max-width:480px){.appearance-editor-footer{flex-wrap:wrap}.appearance-footer-actions{width:100%}.appearance-footer-actions button{flex:1}}
</style>