<script setup lang="ts">
// 专属只记忆性能与锁帧；输入设置由全局及四模式统一拥有。
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import AppIcon from './AppIcon.vue';
import OverflowGameName from './OverflowGameName.vue';
import GameProfileRemoveDialog from './GameProfileRemoveDialog.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { captureGameCustomProfileTarget } from '@/bridge/gameCustomProfileTarget';
import { detectedGameName, type DetectedGame } from '@/bridge/gamedetect';
import { gamePolicyKey } from '@/bridge/gamePolicyHysteresis';
import { getSettingsGeneration } from '@/bridge/settingsRepository';
import { tryAcquireQuickAction } from '@/bridge/quickActionLock';
import {
  createGameCustomProfile, loadGameCustomConfig, onGameCustomConfigChanged,
  removeGameCustomProfile, restorePerformanceScheduleIfConfigured, setGameCustomAutoMemory,
  type GameCustomConfig, type GameCustomProfileTarget,
} from '@/bridge/performanceSchedule';

const props = defineProps<{ game: DetectedGame | null; open: boolean }>();
const emit = defineEmits<{
  (e: 'status', value: { message?: string; error?: string }): void;
  (e: 'close'): void;
  (e: 'changed'): void;
}>();
const config = ref<GameCustomConfig>({ version: 1, entries: {}, rtss: {}, autoMemory: false });
const busy = ref(false), removalError = ref('');
const removal = ref<{ target: GameCustomProfileTarget; gameName: string; revision: string } | null>(null);
const key = computed(() => props.game ? gamePolicyKey(props.game) : '');
const entry = computed(() => config.value.entries[key.value]);
const gameAvailable = computed(() => !!props.game && !!key.value);
const gameName = computed(() => detectedGameName(props.game) || key.value || '未识别游戏');
const autoMemory = computed(() => config.value.autoMemory === true);
let alive = false, loadRevision = 0, stopConfigListener: (() => void) | undefined;

function announce(message = '', error = '') { if (alive) emit('status', { message, error }); }
function focusEntry() {
  void nextTick(() => {
    if (!alive || !props.open) return;
    const element = document.querySelector<HTMLElement>('[data-gp-custom-entry]:not(:disabled)')
      || document.querySelector<HTMLElement>('[data-gp-custom-auto-memory]:not(:disabled)');
    if (element) focusGamepadElement(element);
  });
}
async function reload() {
  const revision = ++loadRevision;
  try {
    const loaded = await loadGameCustomConfig();
    if (alive && props.open && revision === loadRevision) config.value = loaded;
  } catch (error) { if (revision === loadRevision) announce('', `读取专属配置失败：${(error as Error).message}`); }
}
async function addProfile() {
  if (busy.value || !props.game || !key.value || entry.value) return;
  const release = tryAcquireQuickAction('top-custom-profile-create');
  if (!release) { announce('', '已有其它快捷操作正在执行，请稍候'); return; }
  const target = captureGameCustomProfileTarget(props.game, getSettingsGeneration()), name = gameName.value;
  busy.value = true;
  let saved = false;
  try {
    saved = await createGameCustomProfile(target, name);
    config.value = await loadGameCustomConfig();
    const owner = await restorePerformanceScheduleIfConfigured();
    announce(`已添加 ${name} 专属配置${owner === 'none' ? '，等待性能调度应用' : ''}`);
    if (alive) emit('changed');
  } catch (error) {
    await reload();
    announce('', `${saved ? '专属配置已保存，应用尚未完成' : '添加专属失败'}：${(error as Error).message}`);
  } finally { busy.value = false; release(); focusEntry(); }
}
async function toggleAutoMemory() {
  if (busy.value) return;
  const generation = getSettingsGeneration(), enabled = !autoMemory.value;
  busy.value = true;
  try {
    await setGameCustomAutoMemory(enabled, generation);
    config.value = await loadGameCustomConfig();
    announce(enabled ? '已切换为自动加入，新识别的游戏将自动创建专属' : '已切换为手动添加，由你手动添加专属');
    if (alive) emit('changed');
  } catch (error) { await reload(); announce('', `保存添加方式失败：${(error as Error).message}`); }
  finally { busy.value = false; }
}
function requestRemoval() {
  if (busy.value || !entry.value || !props.game) return;
  removalError.value = '';
  removal.value = {
    target: captureGameCustomProfileTarget(props.game, getSettingsGeneration()), gameName: gameName.value,
    revision: JSON.stringify([entry.value, config.value.rtss[key.value]]),
  };
}
function cancelRemoval() { if (!busy.value) { removal.value = null; focusEntry(); } }
async function confirmRemoval() {
  const request = removal.value;
  if (!request || busy.value) return;
  const release = tryAcquireQuickAction('top-custom-profile-remove');
  if (!release) { removalError.value = '已有其它快捷操作正在执行，请稍候'; return; }
  busy.value = true; removalError.value = '';
  let deleted = false;
  try {
    await removeGameCustomProfile(request.target, request.revision);
    deleted = true;
    config.value = await loadGameCustomConfig(); removal.value = null;
    const owner = await restorePerformanceScheduleIfConfigured();
    announce(`已删除 ${request.gameName} 专属配置${owner === 'none' ? '，等待全局性能调度应用' : '，恢复全局性能设置'}`);
    if (alive) emit('changed');
  } catch (error) {
    if (deleted) { removal.value = null; announce('', `专属配置已删除，恢复全局性能调度未完成：${(error as Error).message}`); }
    else removalError.value = `删除专属失败：${(error as Error).message}`;
  } finally { busy.value = false; release(); if (deleted) focusEntry(); }
}
watch(() => [props.open, props.game?.pid, props.game?.processCreated, props.game?.identityGeneration, props.game?.path], () => {
  if (!busy.value) removal.value = null;
  if (props.open) void reload();
});
onMounted(() => {
  alive = true;
  stopConfigListener = onGameCustomConfigChanged(next => { if (alive) config.value = next; });
  if (props.open) void reload();
});
onUnmounted(() => { alive = false; ++loadRevision; stopConfigListener?.(); });
</script>

<template>
  <div class="custom-top-panel" data-gp-custom-panel>
    <div class="custom-top-entry-row">
      <div class="custom-top-label">
        <AppIcon name="settings" aria-hidden="true" />
        <strong>专属配置</strong>
        <OverflowGameName class="custom-entry-game-name" :name="gameName" :interactive="false" />
      </div>
      <button type="button" class="custom-profile-action" :class="{ danger: !!entry }"
        :disabled="busy || !gameAvailable" data-gp-custom-entry data-gp-custom-remove data-gp-game-row="custom-entry"
        :aria-label="entry ? '删除当前游戏专属配置' : '添加当前游戏专属配置'"
        @click.stop="entry ? requestRemoval() : addProfile()">{{ entry ? '删除专属' : '添加专属' }}</button>
      <button type="button" class="custom-auto-memory" :class="{ active: autoMemory }"
        :disabled="busy" :aria-pressed="autoMemory" data-gp-custom-auto-memory data-gp-game-row="custom-entry"
        @click.stop="toggleAutoMemory">{{ autoMemory ? '自动加入' : '手动添加' }}</button>
    </div>
    <GameProfileRemoveDialog v-if="removal" :game-name="removal.gameName" :busy="busy" :error="removalError"
      @cancel="cancelRemoval" @confirm="confirmRemoval" />
  </div>
</template>

<style scoped>
.custom-top-panel{min-width:0}
/* Keep both action slots fixed; only the game-name viewport may shrink and scroll. */
.custom-top-entry-row{display:grid;grid-template-columns:minmax(0,1fr) 68px 68px;align-items:center;gap:8px;min-width:0;padding:8px 10px;border-radius:var(--radius-ctrl);background:var(--bg-input)}
.custom-top-label{display:flex;align-items:center;gap:7px;min-width:0}
.custom-top-label :deep(svg){width:15px;height:15px;flex:none;color:var(--accent)}
.custom-top-label strong{flex:none;font-size:13px;white-space:nowrap}
.custom-entry-game-name{min-width:0;flex:1;color:var(--text-dim);font-size:12px}
.custom-profile-action,.custom-auto-memory{flex:none;min-height:34px;padding:6px 9px;border:1px solid var(--border);border-radius:8px;background:var(--bg-input);color:var(--text);font:inherit;font-size:12px;font-weight:600;white-space:nowrap;cursor:pointer}
.custom-profile-action{color:var(--accent);border-color:var(--accent-border);background:var(--accent-surface)}
.custom-profile-action.danger{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 30%,var(--border));background:color-mix(in srgb,var(--danger) 8%,var(--bg-input))}
.custom-auto-memory.active{color:var(--accent);border-color:var(--accent-border);background:var(--accent-surface)}
.custom-profile-action.focused,.custom-auto-memory.focused{outline:2px solid var(--accent);outline-offset:-2px;box-shadow:inset 0 0 8px var(--accent-surface)}
button:disabled{opacity:.45;cursor:default}
</style>
