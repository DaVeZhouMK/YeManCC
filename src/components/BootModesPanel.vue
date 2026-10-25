<script lang="ts">
// Renderer-session preference; ordinary component remounts retain it.
let mutedThisSession = false;
</script>
<script setup lang="ts">
import BootModePreview from './BootModePreview.vue';
import '@/styles/bootModeGlow.css';
import { computed, nextTick, onActivated, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import InlineIcon from './InlineIcon.vue';
import BootModeWorkbench from './BootModeWorkbench.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { BOOT_MODE_IDS, BOOT_MODE_LABELS, bootStartupStatus, type BootStartupResult, cloneBootValue, resolveBootMode, type BootModeDocument, type BootModeSnapshot, type BootModeId, type BootModeCategory, type BootDeviceCapabilities } from '@/bridge/bootModeModel';
import { loadBootModes, readBootModeSupport, saveBootModes, chooseBootMode, bootModeConnections, addBootSharedApplication, type BootModeReceipt } from '@/bridge/bootModes';

const documentState = ref<BootModeDocument | null>(null), baseline = ref<BootModeSnapshot>();
const selected = ref<BootModeId>('global'), support = ref<BootDeviceCapabilities>({});
const apps = ref<Array<{ name: string; path: string }>>([]);
const editor = ref(false), editorMounted = ref(false), retainedDirty = ref(false);
const panel = ref<HTMLElement | null>(null), requiresCapture = ref(false);
let selectedByUser = false;
const busy = ref(false), error = ref(''), editorError = ref(''), status = ref('');
const startupResult = ref<BootStartupResult>(), saveReceipt = ref(0);
const statusText = computed(() => {
  const startup = bootStartupStatus(startupResult.value);
  return startupResult.value && startupResult.value.status !== 'applied' ? [startup, status.value].filter(Boolean).join(' · ') : status.value;
});
const connections = ref<Array<{ label: string; connected: boolean; detail?: string }> | null>(null);
const mute = ref(false), pendingMode = ref<BootModeId>('global');
const appDialog = ref(false), appName = ref(''), appPath = ref(''), dialogError = ref('');
const dialog = ref<HTMLElement | null>(null);
const modalOpen = computed(() => !!connections.value || appDialog.value);
const snapshot = computed(() => documentState.value ? resolveBootMode(documentState.value, selected.value) : null);
const personaLabels: Record<string, string> = { disabled: '关闭', elite: 'Xbox', steamdeck: 'SteamDeck', 'dualsense-edge': 'PS5' };
const mouseLabels: Record<string, string> = { off: '关闭', microsoft: '微软鼠标', joyxoff: 'JoyXoff', steamdeck: 'SteamDeck 鼠标' };
let disposed = false, returnFocus: HTMLElement | null = null;
let editorSupportEpoch = 0;
const editorSupportReads = new Map<BootModeCategory, Promise<BootDeviceCapabilities>>();
async function readEditorCategory(category: BootModeCategory) {
  if (disposed || !editorMounted.value) return;
  const epoch = editorSupportEpoch;
  let request = editorSupportReads.get(category);
  if (!request) { request = readBootModeSupport(category); editorSupportReads.set(category, request); }
  try {
    const value = await request;
    if (!disposed && editorMounted.value && epoch === editorSupportEpoch) support.value = { ...support.value, ...value };
  } catch {
    // A missing observation stays unknown. Re-entry may retry, never fabricate support.
    if (editorSupportReads.get(category) === request) editorSupportReads.delete(category);
  }
}
const message = (e: unknown) => e instanceof Error ? e.message : String(e);
async function load() {
  if (busy.value) return;
  busy.value = true; error.value = '';
  try {
    const state = await loadBootModes({ overviewOnly: true });
    if (disposed) return;
    documentState.value = state.document;
    requiresCapture.value = state.requiresCapture === true;
    startupResult.value = state.startupResult;
    // Keep the initial capture stable across saves and mode switches.
    baseline.value ??= cloneBootValue(state.baseline || state.document.global);
    if (!selectedByUser) selected.value = state.document.bootEnabled ? state.document.boot : 'global';
    support.value = state.support; apps.value = state.apps;
  } catch (e) { if (!disposed) error.value = message(e); }
  finally { if (!disposed) busy.value = false; }
}
const editorOpener = ref<HTMLElement | null>(null);
async function openEditor() {
  if (!documentState.value || busy.value) return;
  editorOpener.value = document.activeElement instanceof HTMLElement ? document.activeElement : null;
  editorError.value = ''; error.value = '';
  if (editorMounted.value) { editor.value = true; return; }
  busy.value = true;
  try {
    // Saved presets are sufficient to edit. Native capability reads are
    // started by the active category after the editor is already visible.
    const state = await loadBootModes({ editorOnly: true });
    if (disposed) return;
    documentState.value = state.document;
    startupResult.value = state.startupResult;
    baseline.value = cloneBootValue(state.baseline || state.document.global);
    requiresCapture.value = state.requiresCapture === true;
    support.value = state.support; apps.value = state.apps;
    editorSupportEpoch++; editorSupportReads.clear();
    editorMounted.value = true; editor.value = true;
  } catch (e) { if (!disposed) error.value = message(e); }
  finally { if (!disposed) busy.value = false; }
}
async function save(value: BootModeDocument) {
  if (busy.value) return;
  busy.value = true; editorError.value = '';
  try {
    const saved = await saveBootModes(value);
    if (disposed) return;
    documentState.value = saved; retainedDirty.value = false; saveReceipt.value++;
    status.value = '预设已保存，需重启后生效；请确认所需的下次开机模式已启用。';
  } catch (e) { if (!disposed) editorError.value = message(e); }
  finally { if (!disposed) busy.value = false; }
}
function closeEditor() { editorSupportEpoch++; editorSupportReads.clear(); editor.value = false; editorMounted.value = false; retainedDirty.value = false; editorError.value = ''; }
function minimize(value: BootModeDocument) {
  retainedDirty.value = JSON.stringify(value) !== JSON.stringify(documentState.value);
  editor.value = false;
}
function receiptStatus(receipt: BootModeReceipt): string {
  return '本次开机预设已处理' + (receipt.skipped.length ? ' · 跳过：' + receipt.skipped.join('、') : '') + (receipt.pending.length ? ' · 待确认：' + receipt.pending.join('、') : '');
}
let refreshPending = false, refreshRunning = false, pendingReceipt: BootModeReceipt | null = null;
async function refreshStartupResult() {
  if (disposed || busy.value || refreshRunning || !refreshPending || !documentState.value) return;
  refreshRunning = true; refreshPending = false;
  const receipt = pendingReceipt; pendingReceipt = null;
  try {
    const state = await loadBootModes({ overviewOnly: true });
    if (disposed) return;
    if (busy.value) { refreshPending = true; pendingReceipt ??= receipt; return; }
    // Keep the user's selected next mode and any retained draft; only the live
    // document/result is refreshed. The editor reconciles metadata separately.
    documentState.value = state.document; startupResult.value = state.startupResult;
    requiresCapture.value = state.requiresCapture === true;
    if (Object.keys(state.support).length) support.value = state.support;
    apps.value = state.apps;
    if (receipt?.mode === state.document.current && (!state.startupResult || receipt.pending.some(value => value.startsWith('开机处理结果：')))) status.value = receiptStatus(receipt);
  } catch (e) { if (!disposed) error.value = '开机处理结果更新未完成：' + message(e); }
  finally {
    refreshRunning = false;
    if (!disposed && !busy.value && refreshPending) void refreshStartupResult();
  }
}
function bootModeChanged(event: Event) {
  const receipt = (event as CustomEvent<BootModeReceipt>).detail;
  if (disposed || !receipt || !BOOT_MODE_IDS.includes(receipt.mode) || !Array.isArray(receipt.skipped) || !Array.isArray(receipt.pending)) return;
  pendingReceipt = cloneBootValue(receipt); refreshPending = true;
  void refreshStartupResult();
}
watch(busy, value => { if (!value && refreshPending) void refreshStartupResult(); });
async function execute(id: BootModeId) {
  if (!documentState.value) return;
  busy.value = true; error.value = ''; status.value = '';
  try {
    // A settings-only first overview is display data, never a complete scene
    // to persist. Capture on this explicit action before writing the preset.
    if (requiresCapture.value) {
      const state = await loadBootModes();
      if (disposed) return;
      documentState.value = state.document; baseline.value = cloneBootValue(state.baseline || state.document.global);
      startupResult.value = state.startupResult; support.value = state.support; apps.value = state.apps;
      requiresCapture.value = false;
    }
    const saved = await chooseBootMode(id, documentState.value, true);
    if (disposed) return;
    documentState.value = saved;
    status.value = saved.bootNotice ? '开机模式已保存，部分启动配置待确认。' : '已设为开机模式。';
  } catch (e) { if (!disposed) error.value = message(e); }
  finally { if (!disposed) busy.value = false; }
}
async function requestBoot() {
  if (busy.value || !documentState.value) return;
  pendingMode.value = selected.value;
  if (mutedThisSession) { await execute(pendingMode.value); return; }
  busy.value = true; error.value = '';
  let rows: Array<{ label: string; connected: boolean; detail?: string }> = [];
  try { rows = await bootModeConnections(pendingMode.value); }
  catch (e) { error.value = message(e); }
  finally { if (!disposed) busy.value = false; }
  if (disposed || error.value) return;
  if (rows.some(row => !row.connected)) { mute.value = false; connections.value = rows; }
  else await execute(pendingMode.value);
}
async function continueConnection() {
  if (busy.value) return;
  mutedThisSession ||= mute.value; connections.value = null;
  await execute(pendingMode.value);
}
async function addApp() {
  if (busy.value || !appName.value.trim() || !appPath.value.trim()) return;
  const pathKey = (path: string) => path.trim().replace(/\//g, '\\').toLowerCase();
  if (apps.value.some(app => pathKey(app.path) === pathKey(appPath.value))) {
    dialogError.value = '该应用已在全局快捷应用库中，请直接选择联动。';
    return;
  }
  busy.value = true; dialogError.value = '';
  try {
    await addBootSharedApplication(appName.value.trim(), appPath.value.trim());
    if (disposed) return;
    const app = { name: appName.value.trim(), path: appPath.value.trim() };
    const samePath = (path: string) => path.toLocaleLowerCase() === app.path.toLocaleLowerCase();
    apps.value = [...apps.value.filter(item => !samePath(item.path)), app];
    appDialog.value = false; appName.value = ''; appPath.value = '';
  } catch (e) { if (!disposed) dialogError.value = message(e); }
  finally { if (!disposed) busy.value = false; }
}
function sharedApps() { dialogError.value = ''; appDialog.value = true; }
const focusable = 'button:not(:disabled), input:not(:disabled), [tabindex="0"]';
watch(modalOpen, async value => {
  if (value) { returnFocus = document.activeElement as HTMLElement; await nextTick(); focusGamepadElement(dialog.value?.querySelector(focusable) || null); }
  else { await nextTick(); if (returnFocus?.isConnected && !returnFocus.closest('[inert]')) focusGamepadElement(returnFocus); }
});
function back(event: Event) {
  if (!modalOpen.value || event.defaultPrevented) return;
  event.preventDefault();
  if (busy.value) return;
  connections.value = null; appDialog.value = false;
}
function keydown(event: KeyboardEvent) {
  if (!modalOpen.value || event.defaultPrevented) return;
  if (event.key === 'Escape') { back(event); return; }
  if (event.key !== 'Tab') return;
  const controls = Array.from(dialog.value?.querySelectorAll<HTMLElement>(focusable) || []).filter(el => el.getClientRects().length);
  const index = controls.indexOf(document.activeElement as HTMLElement);
  if (controls.length && (index < 0 || (!event.shiftKey && index === controls.length - 1) || (event.shiftKey && index === 0))) {
    event.preventDefault(); focusGamepadElement(controls[event.shiftKey ? controls.length - 1 : 0]);
  }
}
onMounted(() => { window.addEventListener('boot-mode:changed', bootModeChanged); void load(); window.addEventListener('ipc:gamepad-back', back); window.addEventListener('keydown', keydown); });
onBeforeUnmount(() => { disposed = true; editorSupportEpoch++; editorSupportReads.clear(); window.removeEventListener('boot-mode:changed', bootModeChanged); window.removeEventListener('ipc:gamepad-back', back); window.removeEventListener('keydown', keydown); });
// The shared app-content may still carry the previous page's scroll offset.
// Restore only on route activation, never while a user is editing or scrolling.
onActivated(() => { const content = panel.value?.closest<HTMLElement>('.app-content'); if (content) content.scrollTop = 0; });
function selectMode(id: BootModeId) { selectedByUser = true; selected.value = id; }
function revealOverviewTop() {
  // The top mode row has no actionable title above it. Returning here must also
  // reveal its cover/title, rather than leave them unreachable above the row.
  const content = panel.value?.closest<HTMLElement>('.app-content');
  if (content) content.scrollTop = 0;
}
</script>

<template>
  <section ref="panel" class="boot-modes-panel">
    <div class="boot-panel-content" :inert="editor || modalOpen || undefined">
      <header class="boot-heading"><h1>开机模式</h1></header>
      <button v-if="!documentState && !busy" @click="load">重新读取</button>
        <div class="boot-current"><span>当前：{{ documentState ? BOOT_MODE_LABELS[documentState.current] : '读取中…' }}</span><span>开机：{{ documentState ? documentState.bootEnabled ? BOOT_MODE_LABELS[documentState.boot] : BOOT_MODE_LABELS.global : '读取中…' }}</span></div>
        <div class="boot-mode-cards" @focusin="revealOverviewTop">
          <button v-for="id in BOOT_MODE_IDS" :key="id" :aria-pressed="selected === id" :disabled="busy && !!documentState" @click="selectMode(id)">
            <img v-if="id === 'xbox'" class="boot-xbox-icon" src="/boot-modes/xbox.svg" alt=""><span v-else-if="id === 'global'" class="inline-icon boot-windows-icon" aria-hidden="true"><svg viewBox="0 0 24 24" width="1em" height="1em" focusable="false"><path fill="currentColor" d="M2 2h9v9H2zm11 0h9v9h-9zM2 13h9v9H2zm11 0h9v9h-9z"/></svg></span><InlineIcon v-else name="steam"/>
            <strong><span>{{ ({global:'全局',xbox:'Xbox',steam:'Steam','xbox-steam':'Steam'})[id] }}</span><em v-if="id==='xbox-steam'">Xbox全屏</em></strong>
          </button>
        </div>
        <div class="boot-main-image"><BootModePreview :mode="selected" :active="!editor && !modalOpen"/><header class="boot-mode-caption"><strong>{{selected==='steam'?'Steam 大屏':selected==='xbox-steam'?'Steam 大屏 · Xbox全屏':BOOT_MODE_LABELS[selected]}}</strong></header></div>
        <div class="boot-panel-actions">
          <button :disabled="busy || !documentState" @click="openEditor">{{ retainedDirty ? '继续编辑' : '编辑配置' }}</button>
          <button class="primary" :disabled="busy || !documentState" @click="requestBoot">设为开机模式</button>
          
        </div>
        <div class="boot-summary"><span>手柄<b>{{ personaLabels[String(snapshot?.input.persona)] || snapshot?.input.persona || '未读取' }}</b></span><span>鼠标<b>{{ mouseLabels[String(snapshot?.input.mouse)] || snapshot?.input.mouse || '未读取' }}</b></span><span>按键<b>{{ (snapshot?.controller.rules as any[])?.length || 0 }} 项</b></span><span>联动<b>{{ (snapshot?.softwareLaunch.paths as any[])?.length || 0 }} 个</b></span></div>
      <p v-if="error" class="boot-error" role="alert">{{ error }}</p>
      <p v-if="statusText" class="boot-status" :class="{'boot-error': startupResult?.status === 'failed'}" :role="startupResult?.status === 'failed' ? 'alert' : 'status'" :title="statusText">{{ statusText }}</p>
      <p v-if="documentState?.bootNotice" class="boot-boot-notice" role="status">{{ documentState.bootNotice }}</p>
    </div>
    <BootModeWorkbench v-if="editorMounted && documentState" :document="documentState" :baseline="baseline" :active="editor" :initial-mode="selected" :return-focus="editorOpener" :support="support" :apps="apps" :saving="busy" :save-receipt="saveReceipt" :error="editorError" :external-modal="appDialog" @category-change="readEditorCategory" @save="save" @close="closeEditor" @minimize="minimize" @shared-apps="sharedApps"/>
    <Teleport to="body">
      <div v-if="modalOpen" ref="dialog" class="boot-notice-overlay" :data-gp-modal="appDialog ? 'boot-mode-apps' : 'boot-mode-connection'" data-gp-scope="boot-mode-notice" data-gp-spatial="nearest" role="dialog" aria-modal="true" :aria-label="appDialog ? '全局快捷应用库' : '连接状态'">
        <div v-if="connections"><h2>连接状态</h2><p>Steam 连接尚未确认；仍可保存为开机模式，启动准备结果会单独显示。</p><div v-for="row in connections" :key="row.label" class="boot-connection"><strong>{{ row.label }}</strong><span>{{ row.detail || (row.connected ? '已连接' : '未连接或尚未确认') }}</span></div><label class="boot-mute"><input v-model="mute" type="checkbox">本次不再提示</label><div class="boot-dialog-actions"><button :disabled="busy" @click="connections = null">返回</button><button class="primary" :disabled="busy" @click="continueConnection">仍保存开机模式</button></div></div>
        <div v-else><h2>全局快捷应用库</h2><p>添加后全部模式可见，各模式分别选择是否联动启动。</p><p v-if="dialogError" class="boot-error" role="alert">{{ dialogError }}</p><div class="boot-library"><div v-for="app in apps" :key="app.path"><b>{{ app.name }}</b><small>{{ app.path }}</small></div><p v-if="!apps.length">尚未添加快捷应用</p></div><label>应用名称<input v-model="appName" :disabled="busy" aria-label="应用名称"></label><label>应用路径<input v-model="appPath" :disabled="busy" aria-label="应用路径"></label><div class="boot-dialog-actions"><button :disabled="busy" @click="appDialog = false">返回</button><button class="primary" :disabled="busy || !appName.trim() || !appPath.trim()" @click="addApp">添加到全局应用库</button></div></div>
      </div>
    </Teleport>
  </section>
</template>

<style scoped>
.boot-modes-panel,.boot-panel-content{display:flex;flex-direction:column;gap:6px;min-height:0;color:var(--text);font-size:14px}.boot-modes-panel{padding-bottom:6px}.boot-heading{display:flex;align-items:baseline;justify-content:space-between;gap:8px;flex-wrap:wrap}.boot-heading h1{font-size:22px;margin:0}.boot-heading>span{color:var(--text-dim)}button,input{font:inherit;color:var(--text);border:1px solid var(--border);background:var(--bg-input);border-radius:6px;padding:8px 10px;min-height:36px;min-width:0}button{cursor:pointer}button:disabled{opacity:.5;cursor:default}button:focus-visible,input:focus-visible{outline:2px solid var(--accent);outline-offset:2px}button[aria-pressed=true],.primary{background:color-mix(in srgb,var(--accent) 25%,var(--bg-input));border-color:var(--accent)}.boot-current{display:flex;justify-content:space-between;gap:8px;flex-wrap:wrap;color:var(--text-dim)}.boot-mode-cards{display:grid;grid-template-columns:1fr 1fr;gap:6px}.boot-mode-cards button{display:flex;align-items:center;gap:8px;text-align:left;position:relative;min-height:56px;padding:18px 8px 6px;line-height:1.2}.boot-mode-cards strong{text-wrap:balance;font-size:14px;font-weight:500}.boot-mode-cards .inline-icon{font-size:24px;flex-shrink:0}.boot-xbox-icon{width:24px;height:24px;flex-shrink:0}.boot-mode-cards small{position:absolute;right:5px;top:3px;font-size:14px;color:var(--text-dim)}.boot-main-image{position:relative;height:110px;min-height:110px;flex-shrink:0;border:1px solid var(--border);border-radius:8px;overflow:hidden;background:#0d2134}.boot-main-image>img{position:absolute;width:100%;height:100%;object-fit:cover}.boot-main-image>div{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;gap:16px;background:radial-gradient(ellipse,#165d98,#081b2a);color:#d9efff}.boot-main-image>div>.inline-icon{font-size:42px}.boot-main-image>div>span{font-size:20px;display:grid;gap:3px}.boot-main-image small{font-size:14px;color:#a6c5dd}.boot-main-image>button{position:absolute;bottom:6px;right:7px;background:#102f48ed}.boot-summary{display:grid;grid-template-columns:1fr 1fr;gap:5px 12px;color:var(--text-dim)}.boot-summary span{display:flex;align-items:baseline;justify-content:space-between;gap:6px;min-width:0}.boot-summary b{font-size:14px;color:var(--text);font-weight:500}.boot-panel-actions{display:grid;grid-template-columns:1fr 1fr;gap:6px}.boot-panel-actions>button{padding:6px 8px;min-height:36px}.boot-restart-note{display:flex;align-items:center;justify-content:center;gap:7px;min-height:36px;color:var(--text-dim);font-size:14px}.boot-error,.boot-status,.boot-boot-notice{padding:8px 10px;border:1px solid var(--border);border-radius:6px;font-size:14px;overflow-wrap:anywhere;margin:0}.boot-error{color:#ffb5c1;border-color:#a45368}.boot-status{color:#a5d1e8}.boot-boot-notice{color:#ebc17e;border-color:#8c7044}.boot-loading{padding:24px;text-align:center;color:var(--text-dim)}.boot-notice-overlay{position:fixed;inset:0;z-index:1500;display:grid;place-items:center;background:#0009;color:var(--text);padding:20px;font-size:14px}.boot-notice-overlay>div{width:min(510px,100%);max-height:calc(100vh - 40px);overflow:auto;padding:22px;border:1px solid var(--border);border-radius:12px;background:var(--bg-panel)}.boot-notice-overlay h2{font-size:20px;margin:0 0 12px}.boot-notice-overlay p{color:var(--text-dim);line-height:1.6}.boot-connection{display:flex;justify-content:space-between;gap:12px;padding:12px;border:1px solid #6b5639;border-radius:6px;margin:8px 0}.boot-connection span{color:#e4b56d}.boot-mute{display:flex;align-items:center;gap:7px;margin-top:12px}.boot-mute input{min-height:0}.boot-dialog-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:16px}.boot-notice-overlay label:not(.boot-mute){display:grid;gap:6px;margin:8px 0}.boot-library{max-height:170px;overflow:auto}.boot-library>div{display:grid;gap:3px;padding:7px;border-bottom:1px solid var(--border)}.boot-library small{font-size:14px;color:var(--text-dim);overflow-wrap:anywhere}
.inline-icon.boot-windows-icon{display:inline-flex;align-items:center;justify-content:center;vertical-align:-0.18em;width:1.1em;height:1.1em;line-height:1;flex-shrink:0}

/* Accepted cover layout: four fixed modes below the 16:9 preview. */
.boot-panel-content{display:flex;flex-direction:column;gap:6px}.boot-heading{order:0}.boot-heading h1{font-size:21px}.boot-current{order:1;font-size:14px}.boot-main-image{order:2;height:auto;min-height:0;aspect-ratio:16/9;border-radius:10px 10px 0 0;border-bottom:0}.boot-mode-cards{order:3;grid-template-columns:repeat(4,minmax(0,1fr));gap:0;margin-top:-6px;border:1px solid var(--border);border-top:0;border-radius:0 0 10px 10px;background:var(--console-surface,var(--bg-panel));overflow:hidden}.boot-mode-cards button{justify-content:center;gap:5px;min-height:48px;padding:8px 3px;border:0;border-radius:0!important;background:transparent;text-align:center}.boot-mode-cards strong{display:grid;gap:2px;font-size:14px;font-weight:500;line-height:1.3}.boot-mode-cards strong em{font-size:12px;color:var(--text-dim);font-weight:400;font-style:normal;white-space:nowrap}.boot-mode-cards .inline-icon,.boot-mode-cards .boot-xbox-icon{width:17px;height:17px;font-size:17px;flex-shrink:0}.boot-mode-caption{position:absolute;left:0;right:0;bottom:0;padding:32px 13px 12px;background:linear-gradient(transparent,#07101bf0);color:#f1f8ff;pointer-events:none}.boot-mode-caption strong{font-size:20px;font-weight:600;text-shadow:0 1px 4px #000}.boot-panel-actions{order:4;grid-template-columns:1fr 1.4fr;gap:6px;margin-top:2px}.boot-panel-actions>button{min-height:35px;padding:7px 8px;font-size:14px}.boot-summary{order:5;padding:8px;border:1px solid var(--border);border-radius:8px;background:var(--console-surface,var(--bg-panel));gap:7px 12px}.boot-summary b{font-size:14px}

/* Startup failures stay readable when gamepad focus scrolls to the mode cards. */
.boot-status{position:sticky;top:0;z-index:2;background:var(--bg-panel);display:-webkit-box;-webkit-box-orient:vertical;-webkit-line-clamp:3;overflow:hidden;flex-shrink:0}
.boot-status.boot-error{color:#ffb5c1;border-color:#a45368}
</style>
