<script setup lang="ts">
import BootModePreview from './BootModePreview.vue';
import '@/styles/bootModeGlow.css';
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue';
import InlineIcon from './InlineIcon.vue';
import Dropdown from './Dropdown.vue';
import BootModeFields from './BootModeFields.vue';
import ControllerShortcutEditorView from '@/views/ControllerShortcutEditorView.vue';
import { windowApi } from '@/bridge/api';
import { isNativeRuntime } from '@/bridge/ipc';
import { focusGamepadElement } from '@/gamepad/focus';
import { BOOT_MODE_IDS, BOOT_MODE_LABELS, BOOT_MODE_CATEGORIES, cloneBootValue, resolveBootMode, setBootModeModuleScope, type BootModeDocument, type BootModeSnapshot, type BootModeId, type BootModeCategory, type BootModeModule, type BootConfig } from '@/bridge/bootModeModel';
import type { BootDeviceCapabilities } from '@/bridge/bootModeModel';
import type { ControllerShortcutRule } from '@/bridge/controllerShortcutRules';

const props = withDefaults(defineProps<{
  document: BootModeDocument; initialMode: BootModeId; baseline?: BootModeSnapshot;
  support: BootDeviceCapabilities; apps: Array<{ name: string; path: string }>;
  active?: boolean; saving?: boolean; saveReceipt?: number; error?: string; externalModal?: boolean; returnFocus?: HTMLElement | null;
}>(), { active: true, saving: false, saveReceipt: 0, error: '', externalModal: false });
const emit = defineEmits<{
  (event: 'save', document: BootModeDocument): void;
  (event: 'category-change', category: BootModeCategory): void;
  (event: 'close'): void;
  (event: 'minimize', draft: BootModeDocument): void;
  (event: 'shared-apps'): void;
}>();
const draft = ref(cloneBootValue(props.document));
const selected = ref(props.initialMode), category = ref<BootModeCategory>('performance');
const controllerOpen = ref(false), notice = ref(''), supportDialog = ref('');
const confirmation = ref<'discard' | 'baseline' | 'preset' | 'replace' | ''>('');
const sceneName = ref(''), scenePreset = ref(''), templateName = ref('');
const root = ref<HTMLElement | null>(null), dialog = ref<HTMLElement | null>(null), scroll = ref<HTMLElement | null>(null);
const initialGlobal = cloneBootValue(props.baseline || props.document.global);
const snapshot = computed(() => resolveBootMode(draft.value, selected.value));
const currentCategory = computed(() => BOOT_MODE_CATEGORIES.find(item => item.id === category.value)!);
const dirty = computed(() => JSON.stringify(draft.value) !== JSON.stringify(props.document));
const blocked = computed(() => props.saving || props.externalModal);
const localModal = computed(() => !!supportDialog.value || !!confirmation.value);
const own = (module: BootModeModule) => selected.value === 'global' || !!draft.value.scenes[selected.value].overrides[module];
const presetOptions = computed(() => Object.keys(draft.value.presets[selected.value] || {}).map(value => ({ value, label: value })));
const names: Record<BootModeModule, string> = { performance: '性能调度', fan: '风扇控制', charge: '充电限制', input: '手柄与鼠标', gyro: '陀螺仪', controller: '手柄自定义', touch: '触摸板与屏幕按键', audioOptimization: '掌机音质优化 · FxSound', frameGeneration: '小黄鸭 · 缩放插帧', softwareLaunch: '自定义软件联动', steamAppearance: 'Steam 大屏美化', steamWallpaper: 'Steam 动态壁纸', steamDecky: 'Decky', monitor: '性能监控', display: '屏幕与显示', background: 'YMCC 界面外观', sleep: '睡眠优化' };
const supportKeys: Partial<Record<BootModeModule, keyof BootDeviceCapabilities>> = { performance: 'performance', fan: 'fan', charge: 'charge', gyro: 'gyro', sleep: 'hibernate' };
function warning(module: BootModeModule) {
  const key = supportKeys[module];
  if (!key) return null;
  const info = props.support[key];
  if (module === 'charge' && info?.state === 'supported' && snapshot.value.charge.enabled && info.choices?.length && !info.choices.includes(Number(snapshot.value.charge.limit)))
    return { label: '档位不支持', reason: '设备提供 ' + info.choices.map(n => n + '%').join('、') };
  if (info?.state === 'supported') return null;
  return { label: info?.state === 'unsupported' ? '不支持' : '支持待确认', reason: info?.reason || '设备能力尚未确认' };
}
function update(module: BootModeModule, value: BootConfig) {
  if (!own(module) || blocked.value) return;
  if (selected.value === 'global') draft.value.global[module] = cloneBootValue(value);
  else draft.value.scenes[selected.value].overrides[module] = cloneBootValue(value);
}
function scope(module: BootModeModule, value: boolean) {
  if (!blocked.value) setBootModeModuleScope(draft.value, selected.value, module, value);
}
function changeCategory(value: BootModeCategory) {
  category.value = value;
  void nextTick(() => { if (scroll.value) scroll.value.scrollTop = 0; });
}
watch(category, value => emit('category-change', value), { immediate: true });
watch(selected, () => { scenePreset.value = ''; sceneName.value = ''; templateName.value = ''; notice.value = ''; if (scroll.value) scroll.value.scrollTop = 0; });
// A successful save supplies the normalized document and its new revision.
// Replacing the draft here makes the next save use that revision as well.
let saveRequested = false, observedSaveReceipt = props.saveReceipt;
watch(() => props.document, (value, previous) => {
  const savedByThisEditor = saveRequested && props.saveReceipt !== observedSaveReceipt;
  observedSaveReceipt = props.saveReceipt;
  if (savedByThisEditor || JSON.stringify(draft.value) === JSON.stringify(previous)) {
    draft.value = cloneBootValue(value);
    if (savedByThisEditor) {
      notice.value = '预设已保存，重启后生效；开机模式可在主面板选择。';
      saveRequested = false;
    }
    return;
  }
  // Minimized drafts retain edits when only the panel's current/boot choice changes.
  const config = (doc: BootModeDocument) => JSON.stringify([doc.global, doc.scenes, doc.presets, doc.controllerTemplates]);
  if (config(value) === config(previous)) {
    const updated = cloneBootValue(value);
    draft.value = { ...updated, global: draft.value.global, scenes: draft.value.scenes, presets: draft.value.presets, controllerTemplates: draft.value.controllerTemplates };
  } else notice.value = '配置已在外部更新，草稿已保留；保存冲突时请收回并重新载入。';
});
watch(() => props.saving, value => { if (!value && props.error) saveRequested = false; });
function save() {
  if (blocked.value || !dirty.value) return;
  saveRequested = true;
  emit('save', cloneBootValue(draft.value));
}
function requestClose() {
  if (blocked.value) return;
  if (dirty.value) confirmation.value = 'discard';
  else emit('close');
}
function saveScene() {
  const name = sceneName.value.trim();
  if (blocked.value || !name) return;
  if (['__proto__', 'prototype', 'constructor'].includes(name)) { notice.value = '请使用其他预设名称。'; return; }
  const entries = draft.value.presets[selected.value] || {};
  if (Object.prototype.hasOwnProperty.call(entries, name) && confirmation.value !== 'replace') { confirmation.value = 'replace'; return; }
  if (!Object.prototype.hasOwnProperty.call(entries, name) && Object.keys(entries).length >= 30) { notice.value = '每个模式最多保存 30 个场景预设，请覆盖已有名称。'; return; }
  draft.value.presets[selected.value] = { ...entries, [name]: { name, overrides: cloneBootValue(selected.value === 'global' ? draft.value.global : draft.value.scenes[selected.value].overrides) } };
  scenePreset.value = name; sceneName.value = ''; notice.value = '整套场景及各模块的跟随关系已加入草稿，请保存更改。';
}
function restoreScene() {
  const preset = draft.value.presets[selected.value]?.[scenePreset.value];
  if (!preset || blocked.value) return;
  if (selected.value === 'global') draft.value.global = { ...draft.value.global, ...cloneBootValue(preset.overrides) };
  else draft.value.scenes[selected.value] = cloneBootValue(preset);
  notice.value = '已恢复场景预设到草稿，请保存更改。';
}
function confirm() {
  const action = confirmation.value;
  if (action === 'discard') { emit('close'); return; }
  if (action === 'baseline') { draft.value.global = cloneBootValue(initialGlobal); notice.value = '已恢复本次载入的全局配置到草稿，请保存更改。'; }
  if (action === 'preset') restoreScene();
  if (action === 'replace') saveScene();
  confirmation.value = '';
}
function saveTemplate() {
  const name = templateName.value.trim();
  if (blocked.value || !own('controller') || !name) return;
  if (['__proto__', 'prototype', 'constructor'].includes(name)) { notice.value = '请使用其他手柄预设名称。'; return; }
  if (!Object.prototype.hasOwnProperty.call(draft.value.controllerTemplates, name) && Object.keys(draft.value.controllerTemplates).length >= 30) { notice.value = '最多保存 30 个手柄预设，请使用已有名称。'; return; }
  draft.value.controllerTemplates = { ...draft.value.controllerTemplates, [name]: cloneBootValue(snapshot.value.controller) };
  templateName.value = ''; notice.value = '手柄预设已加入草稿，请保存更改。';
}
function applyRules(rules: ControllerShortcutRule[]) { update('controller', { ...snapshot.value.controller, name: '自定义手柄预设', rules: cloneBootValue(rules) } as unknown as BootConfig); }
function chooseTemplate(value: string | number) { const preset = draft.value.controllerTemplates[String(value)]; if (preset) update('controller', preset); }

let originalFocus: HTMLElement | null = null, modalFocus: HTMLElement | null = null, disposed = false;
let widthHeld = false, widthQueue: Promise<void> = Promise.resolve();
const widthOwner = 'boot-mode-editor-' + Date.now() + '-' + Math.random().toString(36).slice(2);
function setWidth(expand: boolean) {
  if (!isNativeRuntime || /[?&]__ymccChild=\d+/.test(window.location.hash)) return;
  widthQueue = widthQueue.then(async () => {
    const wanted = expand && !disposed && props.active;
    if (widthHeld === wanted) return;
    const result = await windowApi.controllerEditorWidth(widthOwner, wanted, 2);
    if (result.ok) widthHeld = wanted;
    else if (wanted) notice.value = '窗口未能加宽，仍可编辑。';
  }).catch(() => { if (!disposed) notice.value = '窗口宽度调整失败，仍可编辑。'; });
}
const focusable = 'button:not(:disabled), input:not(:disabled), select:not(:disabled), textarea:not(:disabled), [tabindex="0"]';
function restoreFocus(target: HTMLElement | null) {
  void nextTick(() => { if (target?.isConnected && !target.closest('[inert]')) focusGamepadElement(target); });
}
watch(() => props.active, async value => {
  setWidth(value);
  if (value) { originalFocus = props.returnFocus || document.activeElement as HTMLElement; await nextTick(); focusGamepadElement(root.value?.querySelector('[data-boot-close]') || null); }
  else restoreFocus(originalFocus);
}, { immediate: true });
watch([localModal, controllerOpen, () => props.externalModal], async ([local, controller, external], previous) => {
  const wasBlocked = previous?.some(Boolean);
  if (local || controller || external) {
    if (!wasBlocked) modalFocus = document.activeElement as HTMLElement;
    if (local) { await nextTick(); focusGamepadElement(dialog.value?.querySelector(focusable) || null); }
  } else if (props.active) restoreFocus(modalFocus);
});
function back(event: Event) {
  if (!props.active || props.externalModal || controllerOpen.value || event.defaultPrevented) return;
  if (root.value?.querySelector('[aria-expanded="true"][aria-haspopup="listbox"]')) return;
  event.preventDefault();
  if (supportDialog.value) supportDialog.value = '';
  else if (confirmation.value) confirmation.value = '';
  else requestClose();
}
function keydown(event: KeyboardEvent) {
  if (!props.active || props.externalModal || event.defaultPrevented) return;
  if (root.value?.querySelector('[aria-expanded="true"][aria-haspopup="listbox"]')) return;
  if (event.key === 'Escape') { if (!controllerOpen.value) back(event); return; }
  if (event.key !== 'Tab') return;
  const nested = controllerOpen.value ? Array.from(root.value?.querySelectorAll<HTMLElement>('[data-gp-modal]') || []).filter(el => el.getClientRects().length).pop() : null;
  const owner = nested || (localModal.value ? dialog.value : root.value);
  const controls = Array.from(owner?.querySelectorAll<HTMLElement>(focusable) || []).filter(el => !el.closest('[inert], [hidden]') && el.getClientRects().length);
  if (!controls.length) return;
  const index = controls.indexOf(document.activeElement as HTMLElement);
  if (index < 0 || (!event.shiftKey && index === controls.length - 1) || (event.shiftKey && index === 0)) {
    event.preventDefault(); focusGamepadElement(controls[event.shiftKey ? controls.length - 1 : 0]);
  }
}
// Only the category rail and its active fields opt into this local row map.
// The shared engine still owns dropdowns, range editing and all other commands.
const rememberedFields = new Map<string, HTMLElement>();
const fieldMemoryKey = () => selected.value + ':' + category.value;
function admittedControl(el: HTMLElement): boolean {
  const style = getComputedStyle(el);
  return !el.matches(':disabled,[aria-disabled="true"],[tabindex="-1"],[data-gp-ignore]') &&
    !el.closest('[inert],[hidden],[aria-hidden="true"]') && !!el.getClientRects().length &&
    style.display !== 'none' && style.visibility !== 'hidden';
}
function fieldRows() {
  const items = Array.from(scroll.value?.querySelectorAll<HTMLElement>(focusable) || [])
    .filter(admittedControl).map(el => {
      const rect = el.getBoundingClientRect(), field = el.closest('.boot-field');
      return { el, x: rect.left + rect.width / 2, y: field?.getBoundingClientRect().top ?? rect.top + rect.height / 2 };
    }).sort((a, b) => a.y - b.y || a.x - b.x);
  const rows: Array<typeof items> = [];
  for (const item of items) {
    let row = rows[rows.length - 1];
    if (!row || Math.abs(item.y - row[0].y) > 12) rows.push(row = []);
    row.push(item);
  }
  for (const row of rows) row.sort((a, b) => a.x - b.x);
  return rows;
}
function rememberField(event: FocusEvent) {
  if (event.target instanceof HTMLElement && event.target.matches(focusable))
    rememberedFields.set(fieldMemoryKey(), event.target);
}
function enterCategoryFields() {
  const remembered = rememberedFields.get(fieldMemoryKey());
  const target = remembered?.isConnected && scroll.value?.contains(remembered) && admittedControl(remembered)
    ? remembered : fieldRows()[0]?.[0]?.el;
  if (target) focusGamepadElement(target);
}
function categoryNavigation(event: Event) {
  if (event.defaultPrevented || !props.active || blocked.value || controllerOpen.value || localModal.value) return;
  const action = (event as CustomEvent<{ action?: string }>).detail?.action;
  if (!action || !['nav-left', 'nav-right', 'nav-up', 'nav-down'].includes(action)) return;
  const host = root.value, active = document.activeElement;
  if (!host || !(active instanceof HTMLElement) || !host.contains(active) || active.closest('[inert]')) return;
  const modals = Array.from(document.querySelectorAll<HTMLElement>('[data-gp-modal]')).filter(admittedControl);
  if (modals[modals.length - 1] !== host || host.querySelector('[aria-expanded="true"][aria-haspopup="listbox"]')) return;
  if (active.matches('input[type="range"][data-boot-slider-editing]')) return;
  const rail = active.closest<HTMLElement>('[data-boot-category]');
  if (rail) {
    const buttons = Array.from(host.querySelectorAll<HTMLElement>('[data-boot-category]')).filter(admittedControl);
    const index = buttons.indexOf(rail);
    if (action === 'nav-up' && index === 0) return; // Keep the header reachable.
    event.preventDefault();
    if (action === 'nav-right') {
      const id = rail.dataset.bootCategory as BootModeCategory;
      if (id === category.value) enterCategoryFields();
      else {
        changeCategory(id);
        void nextTick(() => { if (!disposed && props.active && !controllerOpen.value && !localModal.value && !props.externalModal) enterCategoryFields(); });
      }
    } else if (action === 'nav-up' || action === 'nav-down') {
      const next = buttons[index + (action === 'nav-up' ? -1 : 1)];
      if (next) focusGamepadElement(next);
    }
    return;
  }
  if (!scroll.value?.contains(active)) return;
  const rows = fieldRows(), rowIndex = rows.findIndex(row => row.some(item => item.el === active));
  if (rowIndex < 0) return;
  const row = rows[rowIndex], column = row.findIndex(item => item.el === active);
  let target: HTMLElement | undefined;
  if (action === 'nav-left' || action === 'nav-right') {
    target = row[column + (action === 'nav-left' ? -1 : 1)]?.el;
    if (!target && action === 'nav-left') {
      rememberedFields.set(fieldMemoryKey(), active);
      target = host.querySelector<HTMLElement>('[data-boot-category="' + category.value + '"]') || undefined;
    }
  } else {
    const next = rows[rowIndex + (action === 'nav-up' ? -1 : 1)];
    if (next) target = [...next].sort((a, b) => Math.abs(a.x - row[column].x) - Math.abs(b.x - row[column].x))[0]?.el;
    else if (action === 'nav-up') return; // The first row may return to the header.
  }
  event.preventDefault(); // A row edge stays put instead of taking a diagonal fallback.
  if (target) focusGamepadElement(target);
}
window.addEventListener('gp:page-command', categoryNavigation);
window.addEventListener('ipc:gamepad-back', back);
window.addEventListener('keydown', keydown);
onBeforeUnmount(() => {
  disposed = true; rememberedFields.clear(); window.removeEventListener('gp:page-command', categoryNavigation); window.removeEventListener('ipc:gamepad-back', back); window.removeEventListener('keydown', keydown);
  setWidth(false); if (props.active) restoreFocus(originalFocus);
});
</script>

<template>
  <Teleport to="body">
    <div v-if="active" ref="root" class="boot-workbench-overlay" :data-gp-modal="externalModal ? undefined : 'boot-mode-workbench'" data-gp-scope="boot-mode-workbench" data-gp-spatial="nearest" data-performance-editor-scale="1" :inert="externalModal || undefined">
      <section class="boot-workbench" role="dialog" aria-modal="true" aria-label="开机模式配置工作台" :inert="controllerOpen || localModal || undefined">
<header class="boot-workbench-top preview-editor-header">
    <div class="preview-editor-title"><small>开机模式</small><h2>编辑配置</h2></div>
    <div role="group" class="boot-mode-strip preview-editor-modes preview-modes-inline" aria-label="编辑模式选择">
  <button v-for="id in BOOT_MODE_IDS" :key="id" :data-preview-mode="id" :aria-label="'编辑' + BOOT_MODE_LABELS[id]" :aria-pressed="selected === id" :disabled="blocked" @click="selected = id">
    <span class="preview-mode-icon" aria-hidden="true"><img v-if="id === 'xbox'" class="boot-xbox-icon" src="/boot-modes/xbox.svg" alt=""><span v-else-if="id === 'global'" class="inline-icon boot-windows-icon"><svg viewBox="0 0 24 24" width="1em" height="1em"><path fill="currentColor" d="M2 2h9v9H2zm11 0h9v9h-9zM2 13h9v9H2zm11 0h9v9h-9z"/></svg></span><InlineIcon v-else name="steam"/></span>
    <strong><span>{{ ({global:'全局',xbox:'Xbox',steam:'Steam','xbox-steam':'Steam'})[id] }}</span><em v-if="id === 'steam'">Windows</em><em v-else-if="id === 'xbox-steam'">Xbox全屏</em></strong>
  </button>
</div>
    <div class="preview-editor-actions"><span class="preview-save-state" :class="{dirty}"><i></i>{{ dirty ? '未保存' : '已保存' }}</span><button v-if="selected === 'global'" :disabled="blocked" @click="confirmation = 'baseline'">恢复预设</button><button :disabled="blocked" @click="emit('minimize', cloneBootValue(draft))">收回</button><button data-boot-close :disabled="blocked" @click="requestClose">返回</button><button class="primary" :disabled="blocked || !dirty" @click="save">{{ saving ? '保存中…' : '保存预设' }}</button></div>
  </header>
  
        <div class="boot-workbench-columns">
          <aside>
            <div class="boot-workbench-image"><BootModePreview :mode="selected" :active="active && !externalModal && !localModal && !controllerOpen"/></div>

            <nav aria-label="开机模式设置分类"><button v-for="item in BOOT_MODE_CATEGORIES" :key="item.id" :data-boot-category="item.id" :aria-pressed="category === item.id" :disabled="blocked" @click="changeCategory(item.id)"><InlineIcon :name="item.icon"/><span>{{ item.label }}</span><small>{{ item.modules.filter(id => own(id)).length }}/{{ item.modules.length }}</small></button></nav>
          </aside>
          <main class="boot-workbench-main">
            <div class="boot-category-heading"><h3>{{ currentCategory.label }}</h3><p>选择分类，编辑此模式的配置。</p></div>
            <div ref="scroll" class="boot-config-scroll" :class="{ 'optimization-grid': category === 'optimization' }" data-gp-pane="boot-mode-fields" @focusin="rememberField">
              <article v-for="module in currentCategory.modules" :key="selected + module" class="boot-module" :data-boot-module="module">
                <header><h3>{{ names[module] }}</h3><button v-if="warning(module)" class="boot-support" :disabled="blocked" @click="supportDialog = warning(module)!.reason"><i>!</i>{{ warning(module)!.label }}</button><small>{{ selected === 'global' ? '全局配置' : own(module) ? '模式预设' : '跟随全局' }}</small></header>
                <div v-if="selected !== 'global'" class="boot-scope"><button :aria-pressed="!own(module)" :disabled="blocked" @click="scope(module, false)">跟随全局</button><button :aria-pressed="own(module)" :disabled="blocked" @click="scope(module, true)">模式单独设置</button></div>
                <p v-if="module === 'touch'" class="boot-touch-note">此处为屏幕触摸按钮；实体按键请在「手柄自定义」设置。</p>
                <div v-if="module === 'performance' && support.hybridCores?.state !== 'supported'" class="boot-core-notice"><span>大小核调度</span><button class="boot-support" :disabled="blocked" @click="supportDialog = support.hybridCores?.reason || 'CPU 架构尚未确认'"><i>!</i>{{ support.hybridCores?.state === 'unsupported' ? '不支持' : '支持待确认' }}</button></div>
                <div v-if="module === 'controller'" class="boot-template-row">
                  <label><span>手柄预设</span><Dropdown :model-value="''" :options="Object.keys(draft.controllerTemplates).map(value => ({ value, label: value }))" :disabled="!own(module) || blocked" placeholder="选择手柄预设" aria-label="手柄自定义预设" @update:model-value="chooseTemplate"/></label>
                  <input v-model="templateName" maxlength="60" :disabled="!own(module) || blocked" aria-label="手柄预设名称" placeholder="手柄预设名称"><button :disabled="!own(module) || blocked || !templateName.trim()" @click="saveTemplate">另存手柄预设</button>
                </div>
                <BootModeFields :module="module" :value="snapshot[module]" :disabled="!own(module) || blocked" :persona="String(snapshot.input.persona)" :support="support" :apps="apps" @change="update(module, $event)" @edit-controller="controllerOpen = true" @shared-apps="emit('shared-apps')"/>
              </article>
            </div>
          </main>
        </div>
        <footer><p v-if="error" class="boot-editor-error" role="alert">{{ error }}</p><p v-else role="status">{{ notice || '编辑只修改预设草稿；保存后需重启生效。' }}</p><span>游戏专属配置优先</span></footer>
      </section>
      <div v-if="localModal" ref="dialog" class="boot-support-overlay" role="dialog" aria-modal="true" :aria-label="supportDialog ? '设备兼容性' : '确认更改'" data-gp-modal="boot-mode-confirm" data-gp-scope="boot-mode-confirm" data-gp-spatial="nearest">
        <div v-if="supportDialog"><h3><i>!</i>设备兼容性</h3><p>{{ supportDialog }}</p><p>仍可编辑此项目。实际应用结果以设备接口回执为准。</p><button @click="supportDialog = ''">继续设置</button></div>
        <div v-else><h3>{{ confirmation === 'discard' ? '放弃未保存的修改？' : confirmation === 'baseline' ? '恢复本次载入的全局配置？' : confirmation === 'replace' ? '覆盖同名场景预设？' : '恢复选中的场景预设？' }}</h3><p>{{ confirmation === 'discard' ? '返回会丢弃草稿；使用「收回」可保留修改。' : confirmation === 'baseline' ? '使用打开此面板时捕获的全局配置，修改先写入草稿。' : '此操作修改所选开机预设，保存后重启生效。' }}</p><div class="boot-dialog-actions"><button @click="confirmation = ''">继续编辑</button><button class="primary" @click="confirm">{{ confirmation === 'discard' ? '放弃并返回' : '确认' }}</button></div></div>
      </div>
      <ControllerShortcutEditorView v-if="controllerOpen && !externalModal" :teleport-to="root || 'body'" :persona="String(snapshot.input.persona) === 'disabled' ? 'elite' : String(snapshot.input.persona)" :preset-draft="{ name: BOOT_MODE_LABELS[selected], rules: snapshot.controller.rules as any, apps }" @preset-apply="applyRules" @close="controllerOpen = false"/>
    </div>
  </Teleport>
</template>

<style scoped>
.boot-workbench-overlay{position:fixed;inset:0;z-index:1200;display:grid;place-items:center;padding:12px;background:#000a;color:var(--text);font-size:14px}
.boot-workbench{width:min(1220px,100%);height:100%;max-height:850px;min-height:0;display:grid;grid-template-rows:auto auto auto minmax(0,1fr) auto;border:1px solid var(--border);border-radius:12px;background:var(--bg-panel);overflow:hidden;box-shadow:0 16px 60px #0008}
button,input{font:inherit;color:var(--text);border:1px solid var(--border);background:var(--bg-input);border-radius:6px;padding:7px 10px;min-height:34px;min-width:0}button{cursor:pointer}button:disabled{opacity:.5;cursor:default}button:focus-visible,input:focus-visible{outline:2px solid var(--accent);outline-offset:2px}.primary,button[aria-pressed=true]{background:color-mix(in srgb,var(--accent) 25%,var(--bg-input));border-color:var(--accent)}
.boot-workbench-top{display:flex;align-items:center;gap:8px;padding:9px 14px;border-bottom:1px solid var(--border)}.boot-workbench-top>div{flex:1;min-width:0}.boot-workbench-top h2{font-size:18px;margin:2px 0 0}.boot-workbench-top small{font-size:14px;color:var(--accent)}.boot-workbench-top>span{color:var(--text-dim)}
.boot-mode-strip{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:7px;padding:8px 14px}.boot-mode-strip button{min-height:58px;display:flex;align-items:center;gap:8px;text-align:left;position:relative;padding:18px 9px 8px}.boot-mode-strip strong{font-size:14px;font-weight:500}.boot-mode-strip small{position:absolute;right:6px;top:2px;font-size:14px;color:var(--text-dim)}.boot-mode-strip .inline-icon{font-size:23px;flex-shrink:0}.boot-xbox-icon{width:24px;height:24px;flex-shrink:0}
.boot-scene-presets{display:flex;align-items:flex-end;gap:8px;padding:0 14px 9px}.boot-scene-presets label{display:grid;gap:3px;flex:1;min-width:140px;color:var(--text-dim)}.boot-scene-presets>input{width:150px}.boot-scene-presets button{white-space:nowrap}.boot-scene-presets :deep(.dd-trigger){font-size:14px}
.boot-workbench-columns{display:grid;grid-template-columns:220px minmax(0,1fr);gap:14px;padding:0 14px 8px;min-height:0}.boot-workbench-columns>aside{display:flex;flex-direction:column;gap:8px;min-height:0}.boot-workbench-image{height:clamp(62px,11vh,110px);flex-shrink:0;overflow:hidden;border:1px solid var(--border);border-radius:7px}.boot-workbench-image>img{width:100%;height:100%;object-fit:cover}.boot-global-image{display:flex;height:100%;align-items:center;justify-content:center;gap:12px;background:radial-gradient(ellipse at 30% 40%,#145f9b,#0d2030);color:#d8efff}.boot-global-image>.inline-icon{font-size:30px}.boot-global-image>span{font-size:18px;display:grid;gap:3px}.boot-global-image small{font-size:14px;color:#a3c1d8}
.boot-workbench-columns nav{display:grid;grid-template-rows:repeat(8,minmax(34px,1fr));gap:5px;flex:1;min-height:0;overflow:auto;padding:2px}.boot-workbench-columns nav button{display:flex;align-items:center;gap:7px;text-align:left;padding:5px 8px;font-size:14px;background:color-mix(in srgb,var(--accent) 6%,var(--bg-input));border-color:transparent}.boot-workbench-columns nav button[aria-pressed=true]{border-color:var(--accent);background:color-mix(in srgb,var(--accent) 24%,var(--bg-input));box-shadow:inset 3px 0 var(--accent)}nav button>span:not(.inline-icon){flex:1;white-space:nowrap}nav button small{font-size:14px;color:var(--text-dim)}
.boot-workbench-main{display:flex;flex-direction:column;min-width:0;min-height:0}.boot-category-heading{padding:0 3px 8px}.boot-category-heading h3{font-size:17px;margin:0}.boot-category-heading p{font-size:14px;color:var(--text-dim);margin:3px 0 0}.boot-config-scroll{min-height:0;overflow-y:auto;padding:2px 5px 5px 2px;scrollbar-width:thin;scroll-padding:8px}.boot-module{background:color-mix(in srgb,var(--accent) 5%,var(--bg-card));padding:12px;border:1px solid var(--border);border-radius:8px;margin-bottom:10px;min-width:0}.boot-module>header{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:11px}.boot-module>header h3{font-size:16px;margin:0}.boot-module>header>small{margin-left:auto;font-size:14px;color:var(--text-dim)}.boot-support{display:flex;align-items:center;gap:5px;font-size:14px;color:#ffb5c1;background:#62304255;border-color:#b35b6d;min-height:30px;padding:3px 7px}.boot-support i,.boot-support-overlay i{font-style:normal;border-radius:50%;background:#dc5770;color:white;font-weight:bold;width:16px;height:16px;display:inline-grid;place-items:center}
.boot-scope{display:grid;grid-template-columns:1fr 1fr;gap:4px;padding:3px;border:1px solid var(--border);border-radius:6px;margin-bottom:12px}.boot-scope button{border-color:transparent}.boot-touch-note{font-size:14px;color:var(--text-dim);margin:0 0 12px}.boot-core-notice{display:flex;justify-content:space-between;align-items:center;color:var(--text-dim);margin-bottom:10px}.boot-template-row{display:flex;align-items:flex-end;flex-wrap:wrap;gap:8px;margin-bottom:15px}.boot-template-row label{flex:1;min-width:160px;display:grid;gap:5px;color:var(--text-dim)}.boot-template-row input{width:140px}.boot-template-row button{white-space:nowrap}.boot-template-row :deep(.dd-trigger){font-size:14px}
footer{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 14px;border-top:1px solid var(--border);color:var(--text-dim);min-width:0}footer p{margin:0;overflow-wrap:anywhere;max-height:78px;overflow:auto}footer>span{white-space:nowrap}.boot-editor-error{color:#ffb5c1}.optimization-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;align-content:start}.optimization-grid .boot-module{margin:0}.optimization-grid .boot-module:last-child{grid-column:1/-1}.optimization-grid :deep(.boot-fields){grid-template-columns:1fr}
.boot-support-overlay{position:fixed;inset:0;z-index:1300;display:grid;place-items:center;background:#0009;padding:20px}.boot-support-overlay>div{width:min(500px,100%);max-height:calc(100vh - 40px);overflow:auto;background:var(--bg-panel);border:1px solid var(--border);border-radius:12px;padding:22px}.boot-support-overlay h3{font-size:18px;display:flex;gap:8px;margin:0 0 14px}.boot-support-overlay p{font-size:14px;color:var(--text-dim);line-height:1.6}.boot-dialog-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:16px}
@media(max-width:950px){.boot-workbench-columns{grid-template-columns:205px minmax(0,1fr);gap:10px}.boot-scene-presets{flex-wrap:wrap}.boot-mode-strip button{gap:5px}.boot-mode-strip strong{font-size:14px}.boot-scene-presets>input{width:120px}}
@media(max-height:740px){.boot-workbench-image{height:62px}.boot-workbench-top{padding-top:6px;padding-bottom:6px}.boot-mode-strip button{min-height:54px}.boot-mode-strip{padding-top:6px;padding-bottom:6px}.boot-scene-presets label{gap:2px}}
.boot-workbench-overlay :deep(.editor-dialog){font-size:14px}.boot-workbench-overlay :deep(.editor-dialog button),.boot-workbench-overlay :deep(.editor-dialog label),.boot-workbench-overlay :deep(.editor-dialog .slider-label),.boot-workbench-overlay :deep(.editor-dialog .dd-trigger){font-size:14px}
.boot-workbench-overlay :deep(.editor-dialog :is(button,input,label,small,p,b,.eyebrow,.state-chip,.field-label,.field-hint,.route-note,.status-bubble,.recorded-inputs,.rule-pagination,.rule-state,.rule-context-title,.action-management-hint,.editor-save-state,.output-help,.preview-selector,.io-flow,.mirror-selection,.mirror-legend,.parameter-toggle-field,.parameter-toggle-action,.keyboard-key-action,.rule-enabled-action)){font-size:14px}
.inline-icon.boot-windows-icon{display:inline-flex;align-items:center;justify-content:center;vertical-align:-0.18em;width:1.1em;height:1.1em;line-height:1;flex-shrink:0}

/* Candidate editor headers. Preserve the accepted main page. */
.boot-workbench-overlay{padding:12px;background:#03070dd9}
.boot-workbench{grid-template-rows:auto minmax(0,1fr) auto;background:linear-gradient(150deg,color-mix(in srgb,var(--accent) 6%,var(--bg-panel)) 0,var(--bg-panel) 44%);border-color:var(--border)}
.preview-editor-header{min-height:66px;padding:8px 14px;gap:14px;background:linear-gradient(100deg,color-mix(in srgb,var(--accent) 8%,transparent),transparent);border-bottom:1px solid #ffffff0d}
.preview-editor-title{flex:0 0 96px!important;min-width:0}
.preview-editor-title>small{font-size:13px;color:#91aac1;line-height:1.2}
.preview-editor-title h2{font-size:19px;font-weight:650;line-height:1.25;margin:3px 0 0;white-space:nowrap}
.preview-editor-actions{display:flex;align-items:center;gap:6px;flex:0 0 auto!important;min-width:0;margin-left:auto}
.preview-editor-actions>button{font-size:14px;min-height:36px;padding:7px 11px;white-space:nowrap;border-radius:7px}
.preview-editor-actions>.primary{border-color:var(--accent);background:linear-gradient(130deg,color-mix(in srgb,var(--accent) 28%,var(--bg-input)),color-mix(in srgb,var(--accent) 16%,var(--bg-input)));color:var(--text)}
.preview-save-state{display:flex;align-items:center;gap:5px;font-size:13px;color:#92a8b9;margin-right:3px;white-space:nowrap}
.preview-save-state>i{width:5px;height:5px;background:#7ba2ac;border-radius:50%}
.preview-save-state.dirty>i{background:#f8c56d}
.preview-editor-modes{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:0;padding:0!important;background:var(--bg-input);border:1px solid var(--border);border-radius:9px;overflow:visible}
.preview-editor-modes button{position:relative;isolation:isolate;display:flex;align-items:center;justify-content:center;gap:9px;min-width:0;min-height:48px;padding:5px 9px!important;border:0!important;border-right:1px solid #ffffff09!important;border-radius:0;background:transparent!important;box-shadow:none;white-space:nowrap;color:#9eafbf}
.preview-editor-modes button:first-child{border-radius:8px 0 0 8px}
.preview-editor-modes button:last-child{border-right:0!important;border-radius:0 8px 8px 0}
.preview-editor-modes strong{display:grid;gap:2px;font-weight:500;font-size:15px;line-height:1.15;text-align:left}
.preview-editor-modes strong em{font-size:13px;line-height:1.1;font-weight:400;font-style:normal;color:#9eb1c4}
.preview-mode-icon{display:inline-flex;align-items:center;justify-content:center;flex:0 0 21px!important;width:21px;height:22px}
.preview-mode-icon .inline-icon,.preview-mode-icon .boot-xbox-icon{font-size:20px;width:20px;height:20px}


.preview-editor-modes button:focus-visible{outline:2px solid color-mix(in srgb,var(--accent) 60%,white);outline-offset:2px;z-index:2}


.boot-workbench-columns{padding-top:13px;gap:14px}
.boot-workbench-columns>aside{gap:9px}
.boot-workbench-image{height:102px;aspect-ratio:16/9;border-radius:9px;border-color:#304556}
.boot-workbench-image>img{object-position:center}
.boot-workbench-columns nav[aria-label="开机模式设置分类"] button[aria-pressed=true]{background:linear-gradient(100deg,color-mix(in srgb,var(--accent) 22%,transparent),color-mix(in srgb,var(--accent) 10%,transparent))!important;border-color:var(--accent);box-shadow:inset 3px 0 var(--accent),0 0 10px color-mix(in srgb,var(--accent) 8%,transparent)}
.boot-category-heading p{color:#8da3b7;margin-top:4px}
.boot-workbench>footer{background:var(--bg-panel);border-top:1px solid #ffffff0b}
.preview-modes-inline{flex:1;max-width:588px;min-width:440px;margin:0}
.boot-workbench-image{height:100px}

@media(max-width:1100px){.preview-editor-header{gap:9px}.preview-editor-title{flex-basis:82px!important}.preview-modes-inline{min-width:370px}.preview-save-state{display:none}}

</style>
