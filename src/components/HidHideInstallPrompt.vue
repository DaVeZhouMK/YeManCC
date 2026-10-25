<script setup lang="ts">
import { nextTick, onMounted, onUnmounted, ref } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import { invoke, on } from '@/bridge/ipc';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { focusGamepadElement } from '@/gamepad/focus';
import {
  createHidHideSetupPromptGate, HIDHIDE_INSTALLER_NAME,
  type HidHideManualInstallPrompt, type HidHideSetupSnapshot,
} from '@/bridge/hidHideSetupPrompt';

const gate = createHidHideSetupPromptGate();
const prompt = ref<HidHideManualInstallPrompt | null>(null);
const panel = ref<HTMLElement | null>(null);
const confirmButton = ref<HTMLButtonElement | null>(null);
const openingFolder = ref(false);
const error = ref('');
let ready = false;
let mounted = false;
let stateRevision = 0;
let previousFocus: HTMLElement | null = null;
let stopState: (() => void) | undefined;
let stopSetup: (() => void) | undefined;
let stopVisibility: (() => void) | undefined;

function hasStartupReceipt(): boolean {
  const receipt = (window as Window & {
    __YMCC_STARTUP_READY__?: { generation?: string; navigationId?: string };
  }).__YMCC_STARTUP_READY__;
  return !!receipt?.generation && !!receipt.navigationId && receipt.navigationId !== '0';
}
function ownsTopModal(): boolean {
  const modals = Array.from(document.querySelectorAll<HTMLElement>('[data-gp-modal]')).filter(el => {
    const style = getComputedStyle(el), rect = el.getBoundingClientRect();
    return style.display !== 'none' && style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0;
  });
  return modals[modals.length - 1] === panel.value;
}
function focusConfirm(): void {
  void nextTick(() => {
    if (mounted && prompt.value && isUiVisible() && !openingFolder.value && ownsTopModal())
      focusGamepadElement(confirmButton.value);
  });
}
function showWhenReady(): void {
  ready ||= hasStartupReceipt(); // Supports remounts and the existing degraded startup receipt.
  const next = gate.take(ready, isUiVisible());
  if (!next) return;
  previousFocus = document.activeElement instanceof HTMLElement ? document.activeElement : null;
  prompt.value = next;
  focusConfirm();
}
function acceptState(state: HidHideSetupSnapshot): void {
  ++stateRevision;
  gate.update(state);
  showWhenReady();
}
function onStartupReady(): void { ready = true; showWhenReady(); }
function close(): void {
  if (openingFolder.value) return;
  gate.dismiss();
  prompt.value = null;
  if (isUiVisible() && previousFocus?.isConnected) focusGamepadElement(previousFocus);
}
function onBack(event: Event): void {
  if (!prompt.value || !isUiVisible() || !ownsTopModal()) return;
  event.preventDefault();
  close();
}
async function openDirectory(): Promise<void> {
  if (!prompt.value || openingFolder.value) return;
  const directory = prompt.value.directory;
  openingFolder.value = true;
  error.value = '';
  try {
    // Only this folder action has the bounded wait; other shell.open callers are unchanged.
    const opened = await invoke<boolean>('shell.open', { url: directory }, { timeoutMs: 10000 });
    if (!mounted) return;
    if (!opened) throw new Error('open failed');
    openingFolder.value = false;
    close();
  } catch {
    if (mounted) error.value = '无法打开安装目录，请在资源管理器中手动打开下方路径。';
  } finally {
    openingFolder.value = false;
    if (mounted && prompt.value) focusConfirm();
  }
}
onMounted(() => {
  mounted = true;
  window.addEventListener('app-startup-ready', onStartupReady);
  window.addEventListener('ipc:gamepad-back', onBack);
  stopState = on<HidHideSetupSnapshot>('gamepad.state', acceptState);
  stopSetup = on<HidHideSetupSnapshot>('gamepad.driverSetup', acceptState);
  stopVisibility = onUiVisibilityChange(({ visible }) => {
    showWhenReady();
    if (visible && prompt.value) focusConfirm();
  });
  // The cached replay queued by on() establishes the baseline BEFORE the one current-state read.
  queueMicrotask(() => {
    if (!mounted) return;
    const revision = stateRevision;
    void invoke<HidHideSetupSnapshot>('gamepad.driverSetup.get', {}, { timeoutMs: 1500 }).then(state => {
      if (mounted && revision === stateRevision) acceptState(state);
    }).catch(() => { /* Native state events remain the authoritative fallback. */ });
  });
});
onUnmounted(() => {
  mounted = false;
  stopState?.(); stopSetup?.(); stopVisibility?.();
  window.removeEventListener('app-startup-ready', onStartupReady);
  window.removeEventListener('ipc:gamepad-back', onBack);
});
</script>

<template>
  <Teleport to="body">
    <Transition name="rc-pop">
      <div v-if="prompt" class="hidhide-install-shade">
        <section ref="panel" class="reset-confirm" role="alertdialog" aria-modal="true"
          aria-labelledby="hidhide-install-title" aria-describedby="hidhide-install-description"
          data-gp-modal data-gp-scope="hidhide-install" @pointerdown.stop @keydown.esc.prevent="close">
          <div id="hidhide-install-title" class="rc-title"><AppIcon name="warning" />HidHide 自动安装未完成</div>
          <p id="hidhide-install-description" class="rc-desc">{{ prompt.description }}点击“确定”打开安装目录，运行 {{ HIDHIDE_INSTALLER_NAME }}，安装完成后按提示重启电脑。</p>
          <p class="rc-path">{{ prompt.directory }}</p>
          <p v-if="error" class="rc-error" role="status">{{ error }}</p>
          <div class="rc-actions" data-gp-group="hidhide-install-confirm">
            <button type="button" data-gp-group="hidhide-install-confirm" :disabled="openingFolder" @click="close">取消</button>
            <button ref="confirmButton" type="button" class="confirm" data-gp-group="hidhide-install-confirm"
              :disabled="openingFolder" @click="openDirectory">{{ openingFolder ? '正在打开…' : '确定' }}</button>
          </div>
        </section>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.hidhide-install-shade { position: fixed; inset: 0; z-index: 1400; display: grid; place-items: center; padding: 20px; box-sizing: border-box; background: rgba(0, 0, 0, .4); }
.reset-confirm { width: min(440px, 100%); max-height: calc(100vh - 40px); box-sizing: border-box; overflow-y: auto; background: var(--bg-panel, #161d29); border: 1px solid var(--border, #2a3342); border-radius: 12px; padding: 18px 20px 17px; box-shadow: 0 16px 40px rgba(0, 0, 0, .55); display: flex; flex-direction: column; gap: 12px; }
.rc-title { display: flex; align-items: center; gap: 9px; font-size: 16px; font-weight: 700; color: var(--text); }
.rc-title :deep(svg) { width: 20px; height: 20px; flex-shrink: 0; color: var(--danger); }
.rc-desc { margin: 0; color: var(--text-dim); font-size: 14px; line-height: 1.6; }
.rc-path { margin: 0; color: var(--text); font-size: 12px; line-height: 1.5; overflow-wrap: anywhere; user-select: text; }
.rc-error { margin: 0; color: var(--danger); font-size: 13px; line-height: 1.5; }
.rc-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 3px; }
.rc-actions button { min-height: 44px; border: 1px solid rgba(255, 255, 255, .08); border-radius: 9px; background: var(--bg-input); color: var(--text); font-size: 14px; font-weight: 600; cursor: pointer; }
.rc-actions button.confirm { color: var(--accent); border-color: color-mix(in srgb, var(--accent) 45%, transparent); background: color-mix(in srgb, var(--accent) 10%, transparent); }
.rc-actions button:disabled { opacity: .55; cursor: wait; }
.rc-pop-enter-active, .rc-pop-leave-active { transition: opacity .15s ease; }
.rc-pop-enter-from, .rc-pop-leave-to { opacity: 0; }
</style>
