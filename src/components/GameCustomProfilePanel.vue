<script setup lang="ts">
// [AI:USER:PROFILE] 设为专属→只对已确认的当前游戏使用专属设置；不能把游戏切换中的旧编辑写入另一目标或公用配置。
// 取消解除→保留原配置；确认解除→删除该游戏专属配置，性能/虚拟手柄/陀螺仪恢复公用，不只是离开编辑页。
// 删除成功但恢复未完成须分别显示；具体执行读 bridge/performanceSchedule.ts 与 gameInputEditor.ts。
import { useVirtualTargetPolicy, currentVirtualTargetPolicy } from '@/bridge/virtualTargetPolicyState';
import { virtualTargetOptions, virtualTargetSelectionError, requireVirtualTargetAllowed, STEAM_DECK_SELF_OUTPUT_MESSAGE } from '@/bridge/virtualTargetPolicy';
import ContinuousPerformancePanel from './ContinuousPerformancePanel.vue';
import OverflowGameName from './OverflowGameName.vue';
import GameProfileRemoveDialog from './GameProfileRemoveDialog.vue';
import { computed, nextTick, onMounted, onUnmounted, ref, watch, toRaw } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import Dropdown from '@/components/Dropdown.vue';
import {
  getContinuousPerformanceSnapshot,
  loadGameCustomConfig,
  loadPerformanceSchedule,
  onGameCustomConfigChanged,
  restorePerformanceScheduleIfConfigured,
  patchGameCustomProfile,
  removeGameCustomProfile,
  type GameCustomProfileTarget,
  type GameCustomConfig,
  type GameCustomProfile,
  type PerformanceScheduleConfig,
} from '@/bridge/performanceSchedule';
import { detectedGameName, type DetectedGame } from '@/bridge/gamedetect';
import { tryAcquireQuickAction } from '@/bridge/quickActionLock';
import { closeJoyxoffIfRunning } from '@/bridge/gameproc';
import { getSettingsGeneration } from '@/bridge/settingsRepository';

const props = defineProps<{ game: DetectedGame | null; open: boolean }>();
const emit = defineEmits<{
  (e: 'status', value: { message?: string; error?: string }): void;
  (e: 'close'): void;
  (e: 'changed'): void;
}>();

type PadPersonaOverride = NonNullable<GameCustomProfile['padPersona']>;
type GyroOverride = NonNullable<GameCustomProfile['gyroOverride']>;

const PAD_PERSONA_OPTIONS: Array<{ value: PadPersonaOverride; label: string; sub: string }> = [
  { value: 'disabled', label: '本机手柄', sub: '关闭虚拟手柄，保持本机手柄' },
  { value: 'steamdeck', label: 'SteamDeck', sub: '虚拟手柄：SteamDeck 布局' },
  { value: 'dualsense-edge', label: 'PS5', sub: '虚拟手柄：DualSense Edge 布局' },
  { value: 'elite', label: 'Xbox', sub: '虚拟手柄：Elite 布局' },
  { value: 'follow', label: '无操作', sub: '不下发覆盖，遵循全局手柄设置' },
];
const virtualTargetPolicy = useVirtualTargetPolicy();
const availablePadOptions = computed(() => virtualTargetOptions(PAD_PERSONA_OPTIONS, virtualTargetPolicy.value));
const GYRO_OVERRIDE_OPTIONS: Array<{ value: GyroOverride; label: string; sub: string }> = [
  { value: 'follow', label: '遵循全局', sub: '跟随陀螺仪页面开关与预设' },
  { value: 'off', label: '陀螺仪关闭', sub: '本游戏强制关闭陀螺仪' },
  { value: 'fps', label: 'FPS射击', sub: '启用陀螺仪并使用 FPS 射击预设' },
  { value: 'racing', label: '赛车', sub: '启用陀螺仪并使用赛车预设' },
  { value: 'custom', label: '自定义', sub: '启用陀螺仪并使用自定义预设' },
  { value: 'steam', label: 'Steam', sub: '启用陀螺仪并使用 Steam 预设' },
];
const GYRO_LEGACY_OPTIONS = [
  { value: 'on' as GyroOverride, label: '开启', sub: '兼容旧配置' },
];

function isPadPersonaOverride(value: unknown): value is PadPersonaOverride {
  return PAD_PERSONA_OPTIONS.some((item) => item.value === value);
}
function isGyroOverride(value: unknown): value is GyroOverride {
  return GYRO_OVERRIDE_OPTIONS.some((item) => item.value === value) ||
    GYRO_LEGACY_OPTIONS.some((item) => item.value === value);
}

const config = ref<GameCustomConfig>({ version: 1, entries: {}, rtss: {} });
const schedule = ref<PerformanceScheduleConfig | null>(null);
const busy = ref(false);
const error = ref('');
const message = ref('');
const expanded = ref(false);
const loadRevision = ref(0);
let stopConfigListener: (() => void) | null = null;
const padPersona = ref<PadPersonaOverride>('follow');
const gyroOverride = ref<GyroOverride>('follow');
const removal = ref<{ target: GameCustomProfileTarget; gameName: string; revision: string } | null>(null);
const removalError = ref('');

const key = computed(() => {
  const target = props.game;
  const raw = target?.path?.split(/[\\/]/).pop() || target?.name || '';
  const value = raw.toLowerCase().trim();
  return value ? (value.endsWith('.exe') ? value : `${value}.exe`) : '';
});
const entry = computed(() => key.value ? config.value.entries[key.value] : undefined);
const hasConfiguration = computed(() => !!entry.value);
const customEnabled = computed(() => entry.value?.enabled === true);
const gameAvailable = computed(() => !!props.game && !!key.value);
const gameName = computed(() => detectedGameName(props.game) || props.game?.name || key.value || '当前游戏');
const gyroLockedByNativePad = computed(() => padPersona.value === 'disabled');
const gyroOverrideOptions = computed(() => GYRO_OVERRIDE_OPTIONS);

function announce(messageText = '', errorText = ''): void {
  message.value = messageText;
  error.value = errorText;
  emit('status', { message: messageText, error: errorText });
}

function clone<T>(value: T): T {
  return structuredClone(toRaw(value));
}

function makeEntry(
  current: GameCustomProfile | undefined,
  currentSchedule: PerformanceScheduleConfig,
  controls: NonNullable<Awaited<ReturnType<typeof getContinuousPerformanceSnapshot>>>['controls'],
): GameCustomProfile {
  return {
    ...(current ? clone(current) : {}),
    controls: current?.controls ? clone(current.controls) : clone(controls),
    displayName: current?.displayName || gameName.value,
    enabled: current?.enabled ?? false,
    // These fields are retained only as the on-disk migration shape. They are
    // never selected or applied by this panel; ContinuousPerformancePanel is
    // the sole performance editor and runtime owner.
    ac: current?.ac || clone(currentSchedule.profiles.ac[currentSchedule.active.ac]),
    dc: current?.dc || clone(currentSchedule.profiles.dc[currentSchedule.active.dc]),
    padPersona: isPadPersonaOverride(current?.padPersona) ? current.padPersona : 'follow',
    gyroOverride: isGyroOverride(current?.gyroOverride) ? current.gyroOverride : 'follow',
    gyroPreset: ['fps', 'racing', 'custom', 'steam'].includes(current?.gyroOverride || '')
      ? current!.gyroOverride as GameCustomProfile['gyroPreset']
      : current?.gyroPreset,
    smtIndependent: current?.smtIndependent,
  };
}

function closePanel(): void {
  if (removal.value) return;
  expanded.value = false;
  emit('close');
  nextTick(() => {
    const entryEl = document.querySelector<HTMLElement>('[data-gp-custom-entry]');
    if (entryEl) focusGamepadElement(entryEl);
  });
}
function onGamepadCustomBack(): void {
  if (expanded.value) closePanel();
}
function disableLeavingBody(el: Element): void {
  if (!(el instanceof HTMLElement)) return;
  el.inert = true;
  el.setAttribute('aria-hidden', 'true');
}
function focusExpandedEntry(): void {
  nextTick(() => {
    const first = document.querySelector<HTMLElement>(
      '[data-gp-custom-body] button:not(:disabled), [data-gp-custom-body] select:not(:disabled), [data-gp-custom-body] input:not(:disabled), [data-gp-custom-body] [tabindex]:not([tabindex="-1"])',
    );
    if (first) focusGamepadElement(first);
  });
}

function togglePanel(): void {
  if (!gameAvailable.value) return;
  if (!hasConfiguration.value) {
    void createConfiguration();
    return;
  }
  expanded.value = !expanded.value;
  if (expanded.value) focusExpandedEntry();
}

function captureProfileTarget() {
  const target = props.game;
  if (!target || !key.value) throw new Error('当前游戏已变化，请重新打开专属配置');
  return { exe: key.value, pid: target.pid, processCreated: String(target.processCreated || ''), generation: getSettingsGeneration() };
}

async function saveInputPatch(patch: Partial<GameCustomProfile>, successMessage: string): Promise<void> {
  if (busy.value || !props.game || !key.value) return;
  const release = tryAcquireQuickAction('top-custom-input');
  if (!release) {
    announce('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  busy.value = true;
  const previous = entry.value;
  const target = captureProfileTarget();
  try {
    requireVirtualTargetAllowed(patch.padPersona ?? previous?.padPersona, currentVirtualTargetPolicy(),
      patch.padPersona !== undefined || (!!patch.gyroOverride && patch.gyroOverride !== 'off' && patch.gyroOverride !== 'follow'));
    const currentSchedule = schedule.value || await loadPerformanceSchedule();
    const controls = (await getContinuousPerformanceSnapshot('global')).controls;
    const nextEntry = { ...makeEntry(previous, currentSchedule, controls), ...patch, enabled: previous?.enabled ?? false };
    if (patch.gyroOverride && ['fps', 'racing', 'custom', 'steam'].includes(patch.gyroOverride)) {
      nextEntry.gyroPreset = patch.gyroOverride as GameCustomProfile['gyroPreset'];
    }
    requireVirtualTargetAllowed(nextEntry.padPersona, currentVirtualTargetPolicy(),
      patch.padPersona !== undefined || (!!patch.gyroOverride && patch.gyroOverride !== 'off' && patch.gyroOverride !== 'follow'));
    await patchGameCustomProfile(target, patch, nextEntry);
    config.value = await loadGameCustomConfig();
    syncInputState();
    announce(successMessage);
    emit('changed');
  } catch (e) {
    config.value = await loadGameCustomConfig().catch(() => config.value);
    syncInputState();
    announce('', `保存专属设置失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

function selectGyroOverride(value: string | number): void {
  if (!isGyroOverride(value) || gyroLockedByNativePad.value) return;
  void saveInputPatch({ gyroOverride: value }, `已选择陀螺仪：${GYRO_OVERRIDE_OPTIONS.find((item) => item.value === value)?.label || value}`);
}
function selectPadPersona(value: string | number): void {
  if (!isPadPersonaOverride(value)) return;
  const blocked = virtualTargetSelectionError(value, currentVirtualTargetPolicy());
  if (blocked) { announce('', blocked); return; }
  const patch: Partial<GameCustomProfile> = { padPersona: value };
  if (value === 'disabled') patch.gyroOverride = 'follow';
  const label = PAD_PERSONA_OPTIONS.find((item) => item.value === value)?.label || value;
  if (value === 'steamdeck') {
    void selectSteamDeckPad(label);
    return;
  }
  void saveInputPatch(patch, `已选择手柄：${label}`);
}
async function selectSteamDeckPad(label: string): Promise<void> {
  const blocked = virtualTargetSelectionError('steamdeck', currentVirtualTargetPolicy());
  if (blocked) { announce('', blocked); return; }
  const closed = await closeJoyxoffIfRunning().catch(() => false);
  await saveInputPatch({ padPersona: 'steamdeck' }, closed ? `已选择手柄：${label}，并已关闭 JoyXoff` : `已选择手柄：${label}`);
}

async function createConfiguration(): Promise<void> {
  if (hasConfiguration.value || busy.value || !gameAvailable.value) {
    expanded.value = true;
    return;
  }
  const release = tryAcquireQuickAction('top-custom-profile-create');
  if (!release) {
    announce('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  busy.value = true;
  const target = captureProfileTarget();
  expanded.value = true;
  try {
    const currentSchedule = await loadPerformanceSchedule();
    const controls = (await getContinuousPerformanceSnapshot('global')).controls;
    const nextEntry = makeEntry(undefined, currentSchedule, controls);
    await patchGameCustomProfile(target, {}, nextEntry, { createOnly: true });
    config.value = await loadGameCustomConfig();
    schedule.value = currentSchedule;
    syncInputState();
    announce(`已添加 ${gameName.value} 专属配置，当前未启用`);
    emit('changed');
  } catch (e) {
    announce('', `添加专属配置失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
    if (expanded.value) focusExpandedEntry();
  }
}

async function toggleCustomEnabled(): Promise<void> {
  const current = entry.value;
  if (!current || busy.value || !props.game || !key.value) return;
  const blocked = virtualTargetSelectionError(current.padPersona, currentVirtualTargetPolicy(), current.enabled === false);
  if (blocked) { announce('', blocked); return; }
  const release = tryAcquireQuickAction('top-custom-profile-toggle');
  if (!release) {
    announce('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  busy.value = true;
  const target = captureProfileTarget();
  let saved = false;
  const nextEntry = { ...current, enabled: current.enabled === false };
  try {
    await patchGameCustomProfile(target, { enabled: nextEntry.enabled }, undefined, { expectedEnabled: current.enabled !== false });
    saved = true;
    config.value = await loadGameCustomConfig();
    const owner = await restorePerformanceScheduleIfConfigured();
    if (nextEntry.enabled && owner === 'none') throw new Error('当前游戏专属连续调度未能接管');
    announce(nextEntry.enabled ? '已启用并应用专属连续调度' : '已关闭专属配置，已恢复全局连续调度');
    emit('changed');
  } catch (e) {
    if (saved) {
      try { await patchGameCustomProfile(target, { enabled: current.enabled !== false }, undefined, { expectedEnabled: nextEntry.enabled !== false }); } catch { /* Preserve concurrent edits; reload the authoritative record below. */ }
    }
    config.value = await loadGameCustomConfig().catch(() => config.value);
    syncInputState();
    announce('', `${nextEntry.enabled ? '启用' : '关闭'}专属配置失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

async function enableDirectly(): Promise<void> {
  if (busy.value || !gameAvailable.value || customEnabled.value) return;
  const release = tryAcquireQuickAction('top-custom-profile-enable');
  if (!release) { announce('', '已有其它快捷操作正在执行，请稍候'); return; }
  const target = captureProfileTarget(), previous = entry.value;
  const blocked = virtualTargetSelectionError(previous?.padPersona, currentVirtualTargetPolicy(), true);
  if (blocked) { release(); announce('', blocked); return; }
  busy.value = true;
  try {
    const currentSchedule = schedule.value || await loadPerformanceSchedule();
    const controls = (await getContinuousPerformanceSnapshot('global')).controls;
    const seed = { ...makeEntry(previous, currentSchedule, controls), enabled: true };
    await patchGameCustomProfile(target, { enabled: true }, seed,
      previous ? { expectedEnabled: false } : { expectedAbsent: true });
    config.value = await loadGameCustomConfig(); syncInputState();
    const owner = await restorePerformanceScheduleIfConfigured();
    if (owner === 'none') throw new Error('配置已启用，性能调度未能接管，请稍后重试');
    announce(`已设为专属：${gameName.value}`); emit('changed');
  } catch (e) {
    config.value = await loadGameCustomConfig().catch(() => config.value); syncInputState();
    announce('', `启用专属失败：${(e as Error).message}`);
  } finally { busy.value = false; release(); }
}

function requestRemoval(): void {
  if (!entry.value || busy.value) return;
  removalError.value = '';
  removal.value = { target: captureProfileTarget(), gameName: gameName.value,
    revision: JSON.stringify([entry.value, config.value.rtss[key.value]]) };
}
function cancelRemoval(): void {
  if (busy.value) return;
  removal.value = null;
  void nextTick(() => {
    const button = document.querySelector<HTMLElement>('[data-gp-custom-remove]');
    if (button) focusGamepadElement(button);
  });
}
async function confirmRemoval(): Promise<void> {
  const request = removal.value;
  if (!request || busy.value) return;
  const release = tryAcquireQuickAction('top-custom-profile-remove');
  if (!release) { removalError.value = '已有其它快捷操作正在执行，请稍候'; return; }
  busy.value = true; removalError.value = '';
  let deleted = false;
  try {
    await removeGameCustomProfile(request.target, request.revision);
    deleted = true;
    config.value = await loadGameCustomConfig();
    expanded.value = false; removal.value = null;
    await restorePerformanceScheduleIfConfigured();
    announce(`已解除 ${request.gameName} 专属，恢复公用设置`);
    emit('changed');
  } catch (e) {
    const text = (e as Error).message;
    if (deleted) announce('', `专属配置已删除，恢复公用性能调度未完成：${text}`);
    else removalError.value = `解除专属失败：${text}`;
  } finally {
    busy.value = false; release();
    if (deleted) void nextTick(() => {
      const button = document.querySelector<HTMLElement>('[data-gp-custom-entry]');
      if (button) focusGamepadElement(button);
    });
  }
}

function syncInputState(): void {
  const current = entry.value;
  padPersona.value = isPadPersonaOverride(current?.padPersona) ? current!.padPersona! : 'follow';
  gyroOverride.value = isGyroOverride(current?.gyroOverride) ? current!.gyroOverride! : 'follow';
}

async function load(): Promise<void> {
  if (!props.open) return;
  const revision = ++loadRevision.value;
  const [loaded, loadedSchedule] = await Promise.all([loadGameCustomConfig(), loadPerformanceSchedule()]);
  if (revision !== loadRevision.value || busy.value || !props.open) return;
  config.value = loaded;
  schedule.value = loadedSchedule;
  syncInputState();
}

watch(() => [props.open, props.game?.pid, props.game?.processCreated], () => {
  if (props.open) void load();
});
watch(() => gameAvailable.value, (available) => {
  if (!available) expanded.value = false;
});

onMounted(() => {
  window.addEventListener('game-quick-custom-back', onGamepadCustomBack);
  stopConfigListener = onGameCustomConfigChanged((next) => {
    if (!busy.value) {
      config.value = next;
      syncInputState();
    }
  });
  void load();
});
onUnmounted(() => {
  window.removeEventListener('game-quick-custom-back', onGamepadCustomBack);
  stopConfigListener?.();
  stopConfigListener = null;
});
</script>

<template>
  <div
    class="custom-top-panel"
    :class="{ expanded, disabled: !gameAvailable }"
    data-gp-custom-panel
    :data-gp-expanded="expanded && gameAvailable"
  >
    <div class="custom-top-entry-row">
    <button
      type="button"
      :class="{ active: expanded }"
      class="custom-top-head game-menu-steam-row"
      :tabindex="!gameAvailable ? -1 : 0"
      data-gp-custom-entry
      data-gp-game-row="custom-entry"
      :aria-expanded="expanded"
      :aria-disabled="!gameAvailable"
      @click.stop="togglePanel"
    >
      <div class="custom-top-label">
        <AppIcon name="settings" class="custom-top-label-icon" />
        <strong>{{ customEnabled ? '专属模式' : '专属配置' }}</strong>
        <OverflowGameName class="custom-entry-game-name" :name="hasConfiguration ? gameName : '未配置'" :interactive="false" />

      </div>
      <div class="custom-top-head-actions">
        <span class="custom-top-chevron" aria-hidden="true">{{ expanded ? '⌃' : '⌄' }}</span>
      </div>
    </button>
    <button v-if="gameAvailable" type="button" class="custom-remove-entry" :class="{ 'custom-enable-entry': !customEnabled }" :disabled="busy" data-gp-custom-remove data-gp-game-row="custom-entry" :aria-label="customEnabled ? '解除当前游戏专属配置' : '将当前游戏设为专属'" @click.stop="customEnabled ? requestRemoval() : enableDirectly()">{{ customEnabled ? '解除专属' : '设为专属' }}</button>
    </div>
    <GameProfileRemoveDialog v-if="removal" :game-name="removal.gameName" :busy="busy" :error="removalError" @cancel="cancelRemoval" @confirm="confirmRemoval" />

    <Transition name="custom-submenu-pop" @before-leave="disableLeavingBody">
      <div v-if="expanded && gameAvailable" class="custom-top-body" data-gp-custom-body>
      <ContinuousPerformancePanel v-if="hasConfiguration" scope="dedicated" @changed="load(); emit('changed')" />
      <div class="game-setting-bubble" data-gp-group="custom-input-override">
        <div class="game-setting-fields">
          <div class="game-setting-field" data-gp-game-row="custom-input-pad">
            <small>选择手柄</small>
            <Dropdown
              :model-value="padPersona"
              :options="availablePadOptions"
              :disabled="busy"
              color="accent"
              aria-label="当前游戏专属手柄"
              @update:model-value="selectPadPersona"
            />
            <small v-if="virtualTargetPolicy?.steamDeckSelfOutputBlocked" role="status">{{ STEAM_DECK_SELF_OUTPUT_MESSAGE }}</small>
          </div>
          <div
            class="game-setting-field"
            :class="{ locked: gyroLockedByNativePad }"
            data-gp-game-row="custom-input-gyro"
          >
            <small>陀螺仪开关</small>
            <Dropdown
              :model-value="gyroOverride"
              :options="gyroOverrideOptions"
              :disabled="busy || gyroLockedByNativePad"
              color="dc"
              aria-label="当前游戏专属陀螺仪开关"
              @update:model-value="selectGyroOverride"
            />
          </div>
        </div>
      </div>

      <div v-if="hasConfiguration" class="custom-top-actions" data-gp-custom-actions data-gp-game-row="custom-actions">
        <button
          type="button"
          data-gp-custom-action
          :class="{ danger: customEnabled }"
          :disabled="busy"
          @click.stop="toggleCustomEnabled"
        >
          <AppIcon :name="customEnabled ? 'close' : 'bolt'" />{{ customEnabled ? '关闭专属配置' : '点击启用配置' }}
        </button>
        <button type="button" data-gp-custom-action class="custom-close-action" @click.stop="closePanel"><strong>B</strong>关闭页面</button>
      </div>

      <small v-if="!game" class="custom-top-hint">识别到游戏后可选择专属性能档位</small>

      </div>
    </Transition>
  </div>
</template>

<style scoped>
.custom-top-entry-row{display:flex;gap:8px;align-items:stretch;min-width:0}.custom-top-entry-row .custom-top-head{flex:3;width:0;min-width:0}.custom-remove-entry{flex:1;min-width:0;min-height:43px;border:1px solid color-mix(in srgb,var(--danger) 30%,var(--border));border-radius:10px;background:color-mix(in srgb,var(--danger) 8%,var(--bg-input));color:var(--danger);font:inherit;font-size:13px;font-weight:650;cursor:pointer}.custom-remove-entry:disabled{opacity:.5}.custom-remove-entry.custom-enable-entry{color:var(--accent);border-color:var(--accent-border);background:var(--accent-surface)}.custom-top-entry-row .custom-top-label{min-width:0}.custom-top-entry-row .custom-top-label .custom-entry-game-name{flex:1;font-size:12px;color:var(--text-dim);min-width:0}

.custom-top-panel {
  display: block;
}
.custom-top-panel.disabled {
  opacity: .45;
  filter: saturate(.65);
}
.custom-top-panel.disabled .custom-top-head {
  cursor: default;
}
.custom-top-panel.expanded {
  display: block;
}
.custom-top-head {
  width: 100%;
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  text-align: left;
  box-sizing: border-box;
  display: grid;
  grid-template-columns: minmax(0, 1fr) 22px;
  align-items: center;
  gap: 8px;
  min-height: 48px;
  padding: 7px 9px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  box-shadow: 0 12px 30px rgba(0,0,0,.28);
  cursor: pointer;
}
.custom-top-head.game-menu-steam-row {
  display: flex;
  align-items: center;
  width: 100%;
  min-height: 43.2px;
  gap: 10px;
  padding: 8px 12px;
  border: 0;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
}
.custom-top-head.game-menu-steam-row .custom-top-label {
  flex: 1 1 auto;
}
.custom-top-head.game-menu-steam-row .custom-top-head-actions {
  margin-left: auto;
}
.custom-top-head.active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
  background: color-mix(in srgb, var(--accent) 10%, var(--bg-input));
}
/* 与游戏黑/白名单一致：父气泡 overflow:hidden 会裁掉全局外发光，
 * 手柄焦点改用内描边。 */
.custom-top-head.focused {
  box-shadow: inset 0 0 0 2px var(--accent), inset 0 0 10px color-mix(in srgb, var(--accent) 35%, transparent);
}
.custom-top-head > div:first-child { min-width: 0; }
.custom-top-label { display: flex; align-items: center; gap: 7px; min-width: 0; text-align: center; }
.custom-top-label small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.custom-top-active {
  flex: 0 0 auto;
  padding: 2px 6px;
  border-radius: 999px;
  background: color-mix(in srgb, var(--ok) 16%, var(--bg-input));
  color: var(--ok);
  font-size: 10px;
  font-weight: 800;
  line-height: 1.2;
  white-space: nowrap;
}
.custom-top-label-icon { width: 15px; height: 15px; color: var(--accent); flex: 0 0 auto; }
.custom-top-head strong { font-size: 13px; }
.custom-top-head-actions { display: inline-flex; align-items: center; justify-content: flex-end; gap: 7px; min-width: 0; }
.custom-top-head-actions button,
.custom-top-actions button,
.custom-delete-confirm button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  min-height: 24px;
  padding: 4px 8px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: 7px;
  background: color-mix(in srgb, var(--bg-solid) 38%, transparent);
  color: var(--text);
  font-size: 10px;
  cursor: pointer;
}
.custom-top-head-actions button :deep(svg), .custom-top-actions button :deep(svg) { width: 13px; height: 13px; }
.custom-top-chevron { display: inline-flex; align-items: center; justify-content: center; width: 22px; color: var(--accent); font-size: 16px; line-height: 1; }
.custom-top-head small { color: var(--text-dim); font-size: 10px; }
.custom-top-body {
  display: grid;
  gap: 6.4px;
  margin: 6.4px 0 0;
  padding: 6.4px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  box-shadow: 0 16px 40px rgba(0,0,0,.4);
}
.custom-submenu-pop-enter-active, .custom-submenu-pop-leave-active {
  transition: opacity .16s ease, transform .16s ease;
  transform-origin: top center;
}
.custom-submenu-pop-enter-from, .custom-submenu-pop-leave-to {
  opacity: 0;
  transform: translateY(-5px) scale(.985);
}
.custom-top-actions { display: flex; gap: 5px; flex-wrap: nowrap; justify-content: flex-end; min-width: 0; }
.custom-top-actions button { background: var(--bg-input); white-space: nowrap; }
.custom-top-actions .danger, .custom-delete-confirm .danger { color: var(--danger); }
.custom-close-action strong { margin-right: 4px; color: var(--danger); font-weight: 800; }
.power-mode-list { display: grid; gap: 8px; }
.power-mode-row {
  display: grid;
  grid-template-columns: 22px 52px minmax(0, 1fr);
  align-items: center;
  gap: 10px;
  padding: 12px 11px;
  border-radius: 9px;
  background: var(--bg-input);
  border: 1px solid rgba(255,255,255,.055);
}
.side-icon { width: 14px; height: 14px; justify-self: center; color: var(--text-dim); }
.power-mode-row.current.ac .side-icon { color: var(--accent); }
.power-mode-row.current.dc .side-icon { color: var(--dc-accent); }
.power-mode-row.current.ac { border-color: color-mix(in srgb, var(--accent) 42%, transparent); }
.power-mode-row.current.dc { border-color: color-mix(in srgb, var(--dc-accent) 42%, transparent); }
.side-name { display: flex; flex-direction: column; align-items: flex-start; gap: 2px; text-align: left; }
.side-name strong { font-size: 13px; }
.side-name small, .mode-picker > small, .custom-top-hint { color: var(--text-dim); font-size: 11px; white-space: nowrap; }
.mode-picker { min-width: 0; display: grid; gap: 4px; }
.mode-picker > small { overflow: hidden; text-overflow: ellipsis; }
/* 设置气泡：一行内「左标题 + 右侧若干平行下拉」。 */
.game-setting-bubble {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 7.2px 10px;
  border: 1px solid rgba(255,255,255,.055);
  border-radius: 9px;
  background: var(--bg-input);
}
.power-mode-content { display: grid; gap: 10px; min-width: 0; }
.mode-frame-controls { min-width: 0; padding-top: 2px; }
.game-setting-title { flex: 0 0 auto; min-width: 0; }
.game-setting-title strong { font-size: 12px; }
.game-setting-fields {
  flex: 1 1 auto;
  min-width: 0;
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px;
  align-items: end;
}
.game-setting-field { min-width: 0; display: grid; grid-template-rows: auto minmax(28px, auto); gap: 3.2px; }
.game-setting-field > small { color: var(--text-dim); font-size: 10px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.game-setting-field :deep(.dd), .game-setting-field :deep(.dd-trigger) { width: 100%; }
/* A3：选「本机手柄」时陀螺仪无法操作，整块暗调。 */
.game-setting-field.locked { opacity: .45; filter: saturate(.6); }
.custom-top-hint { display: block; }
.custom-delete-confirm {
  display: grid;
  gap: 5px;
  padding: 10px;
  border: 1px solid color-mix(in srgb, var(--danger) 35%, transparent);
  border-radius: 8px;
  background: var(--bg-solid);
}
.custom-delete-confirm small { color: var(--text-dim); }
.custom-delete-confirm > div { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; }
button:disabled { opacity: .45; cursor: default; }
.game-setting-field :deep(.dd-trigger){height:28px;min-height:28px;padding-block:4px;line-height:1.2}
</style>


