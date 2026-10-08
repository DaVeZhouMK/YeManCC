<script setup lang="ts">
import { computed, nextTick, onActivated, onBeforeUnmount, onMounted, ref } from 'vue';
import GamepadVisualizer from '@/components/GamepadVisualizer.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import { summonGet, summonSet, type GamepadSettings } from '@/bridge/yeman';
import { getGameInputRedistState, installGameInputRedist, setMouseBackend, type MouseBackend, type MouseModeState } from '@/bridge/gameproc';
import { focusGamepadElement, getGamepadPopupPlacement } from '@/gamepad/focus';
import { compareAndSwapInputSettings, loadSettings, type InputSettingsSnapshot } from '@/bridge/settingsRepository';
import {
  NATIVE_DEFAULT_SHORTCUTS,
  actionById,
  isControllerShortcutActionVisible,
  formatControllerShortcutInputs,
  highlightedGamepadButtons,
  readControllerShortcutRules,
  reconcileControllerShortcutRules,
  type ControllerShortcutRule,
} from '@/bridge/controllerShortcutRules';

/**
 * 2026-09-28 用户裁决（消除“永远 2 份”）：
 *   本卡不再维护硬编码的 8 项快捷，而与「自定义快捷规则」编辑器共用**同一份
 *   真实规则**（默认在前、自定义在后）。卡内点选=开关该条规则并立即保存；
 *   组合/动作编辑一律在编辑器中完成。规则超过 8 条时卡片保持原尺寸并翻页。
 *   默认动作的运行时执行仍由 native 既有路由承担（组合编辑待 InputHost 接管）；
 *   keyboard.* 规则由规则表真实执行。
 */
// 2026-10-05：快捷开关与规则编辑共用同一张卡，页面负责原有编辑器生命周期。
const emit = defineEmits<{ (e: 'open-editor'): void }>();
const props = withDefaults(defineProps<{ steamDeckActive?: boolean }>(), { steamDeckActive: false });
const PAGE_SIZE = 8;
const SYNC_EVENT = 'ipc:controller-shortcuts.updated';
// 与虚拟手柄卡共用同一份输入配置快照（input 段）。
const rules = ref<ControllerShortcutRule[]>([]);
const snapshot = ref<InputSettingsSnapshot | null>(null);
const page = ref(0);
const busy = ref(false);
const status = ref('读取快捷规则…');
const hoverId = ref('');
const focusId = ref('');
const gridEl = ref<HTMLElement | null>(null);
const activeId = computed(() => hoverId.value || focusId.value);
const visibleRules = computed(() => rules.value.filter((rule) => isControllerShortcutActionVisible(rule.actionId)));
const activeRule = computed(() => visibleRules.value.find((rule) => rule.id === activeId.value) || null);
const enabledCount = computed(() => visibleRules.value.filter((rule) => rule.enabled).length);
const totalPages = computed(() => Math.max(1, Math.ceil(visibleRules.value.length / PAGE_SIZE)));
const pagedRules = computed(() => visibleRules.value.slice(page.value * PAGE_SIZE, (page.value + 1) * PAGE_SIZE));
const highlightedButtons = computed(() => highlightedGamepadButtons(activeRule.value));
const activeInputText = computed(() => activeRule.value ? (formatControllerShortcutInputs(activeRule.value.inputs) || '未录入输入') : '');
const mouseToggleEnabled = computed(() => rules.value.some((rule) => rule.enabled && rule.actionId === 'mouse.toggle'));
// The visualizer still consumes GamepadSettings for its layout; only mouseBackend
// is read back from it here (shortcut state now lives in the shared rule list).
const gamepad = ref<GamepadSettings>({
  enabled: true, bDoubleMinimize: true, startDoubleF7: true, tdpShortcut: true, fpsShortcut: true,
  killGame: true, openKeyboard: true, returnDesktop: true, mouseToggle: true, mouseBackend: 'joyxoff',
});
const mouseBackendBusy = ref(false);
const mouseBackendNotice = ref('');
const gameInputRepairOpen = ref(false);
const gameInputRepairBusy = ref(false);
const gameInputRepairStatus = ref('');
const gameInputRepairTriggerEl = ref<HTMLElement | null>(null);
const gameInputRepairPanelEl = ref<HTMLElement | null>(null);
const gameInputRepairCancelEl = ref<HTMLElement | null>(null);
const gameInputRepairStyle = ref<Record<string, string>>({});
const gameInputRepairAbove = ref(false);
let gameInputRepairPoll = 0;

function cloneRules(value: readonly ControllerShortcutRule[]): ControllerShortcutRule[] {
  return value.map((rule) => ({ ...rule, inputs: rule.inputs.map((input) => ({ ...input })), params: { ...rule.params } }));
}
function originLabel(rule: ControllerShortcutRule): string {
  return rule.origin === 'native-default' ? '默认' : '自定义';
}
function clampPage(): void {
  page.value = Math.min(page.value, Math.max(0, totalPages.value - 1));
}
async function load(): Promise<void> {
  gamepad.value = await summonGet().catch(() => gamepad.value);
  const settings = await loadSettings();
  snapshot.value = settings.input;
  const nativeEnabled = Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map((definition) => [definition.defaultKey, gamepad.value[definition.defaultKey]]));
  const persisted = readControllerShortcutRules(settings.input.buttonMapping?.rules);
  const reconciled = reconcileControllerShortcutRules(persisted, nativeEnabled);
  rules.value = reconciled.rules;
  clampPage();
  status.value = '';
}
/**
 * 卡片开关走与编辑器“应用规则”完全相同的写入路径（CAS 配置 + 原生开关桥），
 * 这样卡片与编辑器永远基于同一份数据，不再出现两份并存的快捷。
 */
async function persistRules(next: ControllerShortcutRule[]): Promise<void> {
  if (!snapshot.value) throw new Error('规则配置尚未读取完成');
  const currentRules = snapshot.value.buttonMapping?.rules && typeof snapshot.value.buttonMapping.rules === 'object' ? snapshot.value.buttonMapping.rules : {};
  const nativeSettings = Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map((definition) => [
    definition.defaultKey,
    next.some((rule) => rule.origin === 'native-default' && rule.defaultKey === definition.defaultKey && rule.enabled),
  ]));
  const result = await compareAndSwapInputSettings(snapshot.value.revision, {
    buttonMapping: { rules: { ...currentRules, schemaVersion: 2, closure: 'UNENCLOSED', shortcutRules: cloneRules(next) } },
  });
  snapshot.value = result.value;
  if (!result.ok) throw new Error('配置已被其它页面更新，已刷新为最新值');
  gamepad.value = await summonSet(nativeSettings).catch(() => gamepad.value);
  window.dispatchEvent(new CustomEvent('ipc:gamepad.settings', { detail: gamepad.value }));
  window.dispatchEvent(new CustomEvent(SYNC_EVENT));
}
async function toggleRule(rule: ControllerShortcutRule): Promise<void> {
  if (busy.value || !isControllerShortcutActionVisible(rule.actionId)) return;
  const before = rules.value;
  const next = rules.value.map((item) => item.id === rule.id ? { ...item, enabled: !item.enabled } : item);
  busy.value = true;
  rules.value = next;
  try {
    await persistRules(next);
    status.value = '已保存，开关立即生效。';
  } catch (error) {
    // 失败后一律回读磁盘真值（含 CAS 冲突场景），状态行再回显失败原因。
    await load().catch(() => { rules.value = before; });
    status.value = `保存失败：${(error as Error).message}`;
  } finally { busy.value = false; }
}
/** 翻页后把焦点交给本页第一行，保证手柄连续操作不被丢焦点打断。 */
async function changePage(delta: number): Promise<void> {
  const next = Math.min(Math.max(0, page.value + delta), totalPages.value - 1);
  if (next === page.value) return;
  page.value = next;
  await nextTick();
  focusGamepadElement(gridEl.value?.querySelector<HTMLElement>('.shortcut-toggle') ?? null);
}
function onRulesUpdated(): void {
  if (!busy.value) void load().catch(() => undefined);
}
function applyMouseBackend(result: MouseModeState): void {
  gamepad.value = { ...gamepad.value, mouseBackend: result.backend };
  window.dispatchEvent(new CustomEvent('mouse-mode:backend-changed', { detail: { backend: result.backend } }));
  window.dispatchEvent(new CustomEvent('gp:mouse-mode', { detail: { on: result.on, backend: result.backend } }));
}
function openGameInputRepair(): void {
  const placement = getGamepadPopupPlacement(gameInputRepairTriggerEl.value?.getBoundingClientRect() ?? null, Math.min(420, window.innerWidth - 16), 250, 10);
  gameInputRepairStyle.value = placement.style;
  gameInputRepairAbove.value = placement.above;
  gameInputRepairStatus.value = '';
  gameInputRepairOpen.value = true;
  nextTick(() => focusGamepadElement(gameInputRepairCancelEl.value));
}
function closeGameInputRepair(): void {
  if (gameInputRepairBusy.value) return;
  gameInputRepairOpen.value = false;
  focusGamepadElement(gameInputRepairTriggerEl.value);
}
async function waitForGameInputRepair(): Promise<boolean> {
  for (let attempt = 0; attempt < 60; attempt += 1) {
    await new Promise((resolve) => { gameInputRepairPoll = window.setTimeout(resolve, 1000); });
    if ((await getGameInputRedistState()).mouseInterfaceAvailable) return true;
  }
  return false;
}
async function confirmGameInputRepair(): Promise<void> {
  if (gameInputRepairBusy.value) return;
  gameInputRepairBusy.value = true;
  gameInputRepairStatus.value = '正在请求管理员权限并安装 Microsoft GameInput Redist…';
  try {
    const install = await installGameInputRedist();
    if (!install.ok) { gameInputRepairStatus.value = install.error || '无法启动修复程序'; return; }
    gameInputRepairStatus.value = install.alreadyInstalled ? '正在验证 Microsoft 鼠标接口…' : '安装程序已启动，正在等待系统完成修复…';
    if (!await waitForGameInputRepair()) { gameInputRepairStatus.value = '尚未检测到组件。请完成安装窗口中的操作后重试。'; return; }
    if (props.steamDeckActive) { gameInputRepairStatus.value = 'SteamDeck 模式下鼠标由 Steam 管理；请先关闭 SteamDeck 虚拟手柄再切换后端。'; return; }
    const state = await setMouseBackend('gamebar');
    if (!state.ok) { gameInputRepairStatus.value = state.error || '组件已安装，但微软鼠标仍不可用。'; return; }
    applyMouseBackend(state);
    gameInputRepairOpen.value = false;
    mouseBackendNotice.value = 'Microsoft GameInput Redist 已修复，已切换到微软鼠标。';
  } catch (error) { gameInputRepairStatus.value = `修复失败：${error instanceof Error ? error.message : String(error)}`; }
  finally { gameInputRepairPoll = 0; gameInputRepairBusy.value = false; }
}
function onGameInputRepairPointer(event: PointerEvent): void {
  if (!gameInputRepairOpen.value || gameInputRepairBusy.value || gameInputRepairPanelEl.value?.contains(event.target as Node)) return;
  closeGameInputRepair();
}
function onGamepadBack(event: Event): void {
  if (!gameInputRepairOpen.value || gameInputRepairBusy.value) return;
  event.preventDefault();
  closeGameInputRepair();
}
async function onMouseBackend(backend: MouseBackend): Promise<void> {
  if (props.steamDeckActive || mouseBackendBusy.value || gamepad.value.mouseBackend === backend) return;
  mouseBackendBusy.value = true;
  mouseBackendNotice.value = '';
  const before = gamepad.value.mouseBackend;
  try {
    const result = await setMouseBackend(backend);
    if (!result.ok) {
      if (backend === 'gamebar' && result.reason === 'gameinput_redist_missing') { openGameInputRepair(); return; }
      mouseBackendNotice.value = result.error || '模拟鼠标方案不可用';
      return;
    }
    applyMouseBackend(result);
  } catch (error) { gamepad.value = { ...gamepad.value, mouseBackend: before }; mouseBackendNotice.value = `方案切换失败：${(error as Error).message}`; }
  finally { mouseBackendBusy.value = false; }
}

onMounted(() => {
  void load().catch((error) => { status.value = `读取失败：${(error as Error).message}`; });
  window.addEventListener('ipc:gamepad-back', onGamepadBack);
  window.addEventListener(SYNC_EVENT, onRulesUpdated);
  document.addEventListener('pointerdown', onGameInputRepairPointer);
});
onActivated(() => { void load().catch(() => undefined); });
onBeforeUnmount(() => {
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
  window.removeEventListener(SYNC_EVENT, onRulesUpdated);
  document.removeEventListener('pointerdown', onGameInputRepairPointer);
  if (gameInputRepairPoll) window.clearTimeout(gameInputRepairPoll);
});
</script>

<template>
  <section class="card shortcuts-card">
    <div class="card-head">
      <h3 class="card-title"><InlineIcon name="keyboard" /> 手柄快捷操作</h3>
      <div class="shortcut-header-actions">
        <span class="state-chip" :class="{ on: enabledCount > 0 }">{{ enabledCount }}/{{ visibleRules.length }} 已启用</span>
        <button type="button" class="shortcut-editor-open" aria-haspopup="dialog" @click="emit('open-editor')">打开编辑器</button>
      </div>
    </div>
    <div v-if="!visibleRules.length" class="shortcut-empty">尚无快捷规则。请打开编辑器新建，或恢复默认快捷键。</div>
    <div v-else ref="gridEl" class="shortcut-grid">
      <button v-for="rule in pagedRules" :key="rule.id" class="shortcut-toggle"
        :class="{ on: rule.enabled, sel: activeId === rule.id }" :disabled="busy"
        @mouseenter="hoverId = rule.id" @mouseleave="hoverId = ''" @focus="focusId = rule.id" @blur="focusId = ''" @click="toggleRule(rule)">
        <span><b>{{ actionById(rule.actionId)?.label || '未知动作' }}</b><small>{{ originLabel(rule) }} · {{ formatControllerShortcutInputs(rule.inputs) || '未录入输入' }}</small></span><i>{{ rule.enabled ? '开' : '关' }}</i>
      </button>
    </div>
    <div v-if="totalPages > 1" class="shortcut-pager">
      <button type="button" :disabled="busy || page === 0" @click="changePage(-1)">‹ 上一页</button>
      <span>第 {{ page + 1 }}/{{ totalPages }} 页 · 共 {{ rules.length }} 条</span>
      <button type="button" :disabled="busy || page >= totalPages - 1" @click="changePage(1)">下一页 ›</button>
    </div>
    <div v-if="mouseToggleEnabled" class="backend-bubble">
      <span>鼠标模式后端</span>
      <div class="mouse-backend-control">
        <div class="mouse-backend-buttons">
          <button ref="gameInputRepairTriggerEl" type="button" :class="{ active: !steamDeckActive && gamepad.mouseBackend === 'gamebar' }" :disabled="steamDeckActive || mouseBackendBusy" @click="onMouseBackend('gamebar')">微软鼠标</button>
          <button type="button" :class="{ active: !steamDeckActive && gamepad.mouseBackend === 'joyxoff' }" :disabled="steamDeckActive || mouseBackendBusy" @click="onMouseBackend('joyxoff')">JoyXoff</button>
          <button type="button" class="steamdeck-backend" :class="{ active: steamDeckActive }" disabled :aria-pressed="steamDeckActive" title="开启 SteamDeck 虚拟手柄时由 Steam 接管鼠标；此项仅展示状态，不可手动选择">SteamDeck鼠标</button>
        </div>
        <Transition name="mouse-pop"><div v-if="mouseBackendNotice" class="mouse-backend-popover" role="alert"><InlineIcon name="warning" size="15px" /><span>{{ mouseBackendNotice }}</span><button type="button" aria-label="关闭提示" @click="mouseBackendNotice = ''">×</button></div></Transition>
      </div>
    </div>
    <p class="shortcut-hint"><template v-if="activeRule"><b>{{ actionById(activeRule.actionId)?.label || '未知动作' }}</b> · <span class="key">{{ activeInputText }}</span> · {{ originLabel(activeRule) }}</template><template v-else>鼠标悬停，或用手柄焦点选择快捷项，可查看对应按键。</template></p>
    <GamepadVisualizer :settings="gamepad" :highlight-buttons="highlightedButtons" />
    <p v-if="status" class="status-line" role="status"><InlineIcon name="check" /> {{ status }}</p>
  </section>

  <Teleport to="body"><Transition name="gameinput-repair-pop"><div v-if="gameInputRepairOpen" ref="gameInputRepairPanelEl" class="gameinput-repair-confirm" :class="{ above: gameInputRepairAbove }" :style="gameInputRepairStyle" role="alertdialog" aria-modal="true" aria-label="修复微软鼠标组件" data-gp-modal @pointerdown.stop @keydown.esc.prevent="closeGameInputRepair">
    <div class="gameinput-repair-title"><InlineIcon name="warning" size="20px" />修复微软鼠标组件</div>
    <p class="gameinput-repair-desc">当前系统缺少 Microsoft GameInput Redist。确认后将从微软官方源下载并安装所需组件，完成后自动切换。</p>
    <p v-if="gameInputRepairStatus" class="gameinput-repair-status">{{ gameInputRepairStatus }}</p>
    <div class="gameinput-repair-actions" data-gp-group="gameinput-repair-confirm"><button ref="gameInputRepairCancelEl" type="button" data-gp-group="gameinput-repair-confirm" :disabled="gameInputRepairBusy" @click="closeGameInputRepair">取消 <small>B</small></button><button type="button" data-gp-group="gameinput-repair-confirm" class="danger" :disabled="gameInputRepairBusy" @click="confirmGameInputRepair">{{ gameInputRepairBusy ? '正在修复…' : '确认修复' }}</button></div>
  </div></Transition></Teleport>
</template>

<style scoped>
.card-head { display:flex; align-items:center; justify-content:space-between; gap:12px; }.card-title{margin:0}.state-chip{flex:0 0 auto;min-height:24px;display:inline-flex;align-items:center;padding:0 8px;border:1px solid rgba(255,255,255,.12);border-radius:999px;color:var(--text-dim);background:color-mix(in srgb,var(--bg-input) 86%,transparent);font-size:10px;font-weight:700}.state-chip.on{border-color:color-mix(in srgb,var(--accent) 58%,transparent);background:color-mix(in srgb,var(--accent) 14%,transparent);color:var(--accent)}.shortcut-empty{margin-top:10px;padding:14px 12px;border:1px dashed rgba(255,255,255,.16);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent);color:var(--text-dim);font-size:11px;line-height:1.5;text-align:center}.backend-bubble,.shortcut-toggle{border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent)}.shortcut-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-top:8px}.shortcut-toggle{min-height:64px;display:flex;align-items:center;justify-content:space-between;gap:8px;padding:9px 10px;color:var(--text);text-align:left;cursor:pointer;transition:border-color .12s,background .12s,transform .08s}.shortcut-toggle:active{transform:scale(.98)}.shortcut-toggle span{display:grid;gap:3px;min-width:0}.shortcut-toggle b{font-size:12px}.shortcut-toggle small{color:var(--text-dim);font-size:10px;line-height:1.3}.shortcut-toggle i{font-style:normal;flex:0 0 auto;padding:2px 7px;border-radius:999px;background:rgba(255,255,255,.1);color:var(--text-dim);font-size:10px;font-weight:700}.shortcut-toggle.on{border-color:color-mix(in srgb,var(--accent) 55%,transparent);background:color-mix(in srgb,var(--accent) 13%,transparent)}.shortcut-toggle.on i{background:var(--accent);color:#06121d}.shortcut-toggle.sel,.shortcut-toggle:focus-visible{outline:none;border-color:var(--accent);box-shadow:0 0 0 1px var(--accent),0 0 8px color-mix(in srgb,var(--accent) 40%,transparent)}.shortcut-toggle:disabled{opacity:.55;cursor:default}.shortcut-pager{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-top:8px;padding:6px 9px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent)}.shortcut-pager button{min-height:28px;padding:0 10px;border:1px solid color-mix(in srgb,var(--accent) 45%,transparent);border-radius:7px;background:transparent;color:var(--accent);font-size:11px;font-weight:700;cursor:pointer}.shortcut-pager button:focus-visible{outline:none;box-shadow:0 0 0 1px var(--accent),0 0 8px color-mix(in srgb,var(--accent) 40%,transparent)}.shortcut-pager button:disabled{opacity:.4;cursor:default;border-color:rgba(255,255,255,.12);color:var(--text-dim)}.shortcut-pager span{color:var(--text-dim);font-size:10px;font-weight:700}.backend-bubble{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-top:8px;padding:8px 10px}.backend-bubble>span{color:var(--text-dim);font-size:11px;font-weight:700}.mouse-backend-control{position:relative;min-width:0}.mouse-backend-buttons{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:3px;padding:3px;border-radius:9px;background:var(--bg-panel)}.mouse-backend-buttons button{min-height:28px;padding:4px 7px;border:1px solid transparent;border-radius:6px;background:transparent;color:var(--text-dim);font-size:10px;font-weight:700;cursor:pointer}.mouse-backend-buttons button.active{border-color:color-mix(in srgb,var(--accent) 48%,transparent);background:color-mix(in srgb,var(--accent) 18%,transparent);color:var(--accent)}.mouse-backend-buttons button:disabled{opacity:.55;cursor:default}.mouse-backend-popover{position:absolute;z-index:25;top:calc(100% + 7px);right:0;width:min(360px,calc(100vw - 24px));display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:10px;padding:12px 14px;border:1px solid color-mix(in srgb,#f5b93d 58%,transparent);border-radius:10px;background:color-mix(in srgb,var(--bg-card) 94%,#17120a);color:#ffd47a;box-shadow:0 8px 22px rgba(0,0,0,.42);font-size:12px;line-height:1.45}.mouse-backend-popover>button{width:26px;height:26px;padding:0;border:0;border-radius:6px;background:rgba(255,255,255,.08);color:inherit;cursor:pointer}.shortcut-hint{min-height:36px;margin:8px 2px 4px;color:var(--text-dim);font-size:11px;line-height:1.5}.shortcut-hint b{color:var(--accent)}.shortcut-hint .key{color:#f5b93d;font-size:1.2em;font-weight:700}.status-line{display:flex;align-items:flex-start;gap:5px;margin:9px 2px 0;color:var(--text-dim);font-size:11px;line-height:1.45}.status-line .inline-icon{color:var(--accent);margin-top:1px}.gameinput-repair-confirm{z-index:1200;background:#161d29;border:1px solid #2a3342;border-radius:12px;padding:18px 20px 17px;box-shadow:0 16px 40px rgba(0,0,0,.55);display:flex;flex-direction:column;gap:12px;max-width:calc(100vw - 16px);min-height:0;overflow-y:auto}.gameinput-repair-confirm::before{content:'';position:absolute;width:13px;height:13px;background:#161d29;border-left:1px solid #2a3342;border-top:1px solid #2a3342;transform:rotate(45deg);top:-7px;right:32px}.gameinput-repair-confirm.above::before{top:auto;bottom:-7px;border-left:none;border-top:none;border-right:1px solid #2a3342;border-bottom:1px solid #2a3342}.gameinput-repair-title{display:flex;align-items:center;gap:9px;color:var(--text);font-size:16px;font-weight:700}.gameinput-repair-title .inline-icon{color:var(--danger)}.gameinput-repair-desc,.gameinput-repair-status{margin:0;color:var(--text-dim);font-size:14px;line-height:1.6}.gameinput-repair-status{color:#ffd47a}.gameinput-repair-actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:3px}.gameinput-repair-actions button{min-height:44px;border:1px solid rgba(255,255,255,.08);border-radius:9px;background:var(--bg-input);color:var(--text);font-size:14px;font-weight:600;cursor:pointer}.gameinput-repair-actions button small{margin-left:4px;color:var(--text-dim);font-size:11px}.gameinput-repair-actions button.danger{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 46%,transparent);background:color-mix(in srgb,var(--danger) 8%,var(--bg-input))}.gameinput-repair-actions button:disabled{cursor:default;opacity:.6}.gameinput-repair-pop-enter-active,.gameinput-repair-pop-leave-active,.mouse-pop-enter-active,.mouse-pop-leave-active{transition:opacity .14s ease,transform .14s ease}.gameinput-repair-pop-enter-from,.gameinput-repair-pop-leave-to,.mouse-pop-enter-from,.mouse-pop-leave-to{opacity:0;transform:translateY(-5px)}@media(max-width:580px){.shortcut-grid{grid-template-columns:1fr}.shortcut-pager{align-items:stretch;flex-direction:column}.shortcut-pager button{width:100%}.backend-bubble{align-items:flex-start;flex-direction:column}.mouse-backend-control{width:100%}.mouse-backend-buttons{width:100%}}
/* Controller page contract: keep the top shortcut controls two-up at the
   supported 1280x720, 1280x800, and high-DPI equivalent layouts.
   2026-09-28：每页固定 8 条（2×4），超出翻页——卡片尺寸不变。 */
.shortcut-grid{grid-template-columns:repeat(2,minmax(0,1fr))}
.shortcut-toggle{min-width:0}
.shortcut-header-actions{display:flex;align-items:center;justify-content:flex-end;gap:8px;flex:0 0 auto}
.shortcut-editor-open{min-height:32px;flex:0 0 auto;padding:0 10px;border:1px solid color-mix(in srgb,var(--accent) 55%,transparent);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--accent) 12%,transparent);color:var(--accent);font-size:11px;font-weight:700;white-space:nowrap;cursor:pointer}
.shortcut-editor-open:hover{background:color-mix(in srgb,var(--accent) 20%,transparent)}
.shortcut-editor-open:focus-visible{outline:none;box-shadow:0 0 0 1px var(--accent),0 0 8px color-mix(in srgb,var(--accent) 40%,transparent)}
@media(max-width:420px){.shortcut-header-actions{gap:5px}.shortcut-editor-open{padding:0 7px}.state-chip{padding:0 5px}}
.mouse-backend-buttons button{white-space:nowrap}.mouse-backend-buttons .steamdeck-backend.active:disabled{opacity:1}.backend-bubble{flex-wrap:wrap}.mouse-backend-control{max-width:100%}
@media(max-width:480px){.backend-bubble{align-items:stretch}.mouse-backend-control{width:100%}}
</style>
