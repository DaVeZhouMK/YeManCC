<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import { dialog } from '@/bridge/api';
import {
  refreshGameStatus,
  subscribeGameStatus,
  detectedGameName,
  type DetectedGame,
} from '@/bridge/gamedetect';
import { on as onIpc } from '@/bridge/ipc';
import { gameRuleNameFromPath, getGameRules, setGameRuleList, type GameRulesSnapshot } from '@/bridge/gameRules';
import { focusGamepadElement } from '@/gamepad/focus';

type SummonCandidate = {
  valid?: boolean;
  blocked?: boolean;
  name?: string;
  path?: string;
  pid?: number;
  summonGeneration?: number;
};

type SummonRegistrationResult = SummonCandidate & {
  processCreated?: string;
  workingSet?: number;
  autoAdded?: boolean;
};

const game = ref<DetectedGame | null>(null);
const summonCandidate = ref<SummonCandidate | null>(null);
const gameIdentifying = ref(false);
const showUnrecognizedPrompt = ref(false);
const gameRuleMenu = ref<'root' | 'edit' | 'whitelist' | 'manual' | null>(null);
const gameRuleTriggerEl = ref<HTMLElement | null>(null);
const gameRulePanelEl = ref<HTMLElement | null>(null);
const gameRules = ref<GameRulesSnapshot>({ blacklist: [], whitelist: [] });
const gameRulesBusy = ref(false);
const gameRuleDraft = ref('');
const manualGameDraft = ref('');
const statusMsg = ref('');
const errMsg = ref('');

let unsubGame: (() => void) | null = null;
let unsubSummonResult: (() => void) | null = null;
let summonIdentifyTimer: ReturnType<typeof setTimeout> | null = null;
let summonIdentifySequence = 0;
let activeSummonPid = 0;
let activeSummonGeneration = 0;
let blockedSummonPid = 0;

function displayProcessName(value: string): string {
  return value.trim().replace(/\.exe$/i, '').trim();
}

const gameDisplayName = computed(() => {
  if (gameIdentifying.value) return '识别中…';
  if (showUnrecognizedPrompt.value) {
    return summonCandidate.value?.name
      ? displayProcessName(summonCandidate.value.name)
      : '未识别游戏';
  }
  const current = game.value;
  if (!current) return '未识别游戏';
  return detectedGameName(current) || displayProcessName(current.name || '') || '未识别游戏';
});

const recognitionTitle = computed(() => {
  if (gameIdentifying.value) return '正在识别当前游戏';
  if (showUnrecognizedPrompt.value) return '点击识别游戏或按 Y 编辑名单';
  return game.value ? `当前游戏：${gameDisplayName.value}，按 Y 编辑名单` : '未识别游戏，点击识别游戏';
});

const gameButtonStatus = computed(() => {
  if (gameIdentifying.value) return '识别中…';
  if (game.value) return `${gameDisplayName.value} · 已识别`;
  return summonCandidate.value?.name
    ? `${displayProcessName(summonCandidate.value.name)} · 未识别`
    : '未识别游戏 · 点击识别游戏';
});

const ruleTarget = computed(() => game.value || summonCandidate.value);

function syncGame(next: DetectedGame | null, preserveRuleMenu = false): void {
  game.value = next;
  if (gameIdentifying.value) return;
  showUnrecognizedPrompt.value = !next;
  if (next) {
    summonCandidate.value = null;
    if (!preserveRuleMenu) gameRuleMenu.value = null;
  }
}

function focusGameRulePanel(): void {
  nextTick(() => {
    const first = gameRulePanelEl.value?.querySelector<HTMLElement>('button:not(:disabled), input:not(:disabled)');
    if (first) focusGamepadElement(first);
  });
}

async function loadGameRules(): Promise<void> {
  try {
    gameRules.value = await getGameRules();
  } catch {
    // Keep the last successful snapshot visible if IPC is temporarily unavailable.
  }
}

function openGameRuleRoot(): void {
  if (gameIdentifying.value) return;
  statusMsg.value = '';
  errMsg.value = '';
  gameRuleMenu.value = 'root';
  void loadGameRules();
}

function closeGameRuleMenus(): void {
  gameRuleMenu.value = null;
  gameRuleDraft.value = '';
  manualGameDraft.value = '';
  const trigger = gameRuleTriggerEl.value;
  nextTick(() => focusGamepadElement(trigger));
}

async function refreshGameRecognition(): Promise<void> {
  if (gameIdentifying.value) return;
  statusMsg.value = '';
  errMsg.value = '';
  gameIdentifying.value = true;
  try {
    const detected = await refreshGameStatus();
    gameIdentifying.value = false;
    // Refresh is an action inside the root rule menu. Keep that menu open so
    // Y does not look like a back/close operation after the result arrives.
    syncGame(detected, true);
    statusMsg.value = detected ? '已刷新游戏识别' : '刷新完成，当前未识别游戏';
  } catch (e) {
    gameIdentifying.value = false;
    errMsg.value = '刷新游戏识别失败：' + (e as Error).message;
  }
}

function displayRuleName(value: string): string {
  return value.includes('*') ? value : `${value}.exe`;
}

watch(gameRuleMenu, (menu) => {
  if (menu) focusGameRulePanel();
});

async function addTargetToBlacklist(): Promise<void> {
  const target = ruleTarget.value;
  if (!target?.name || gameRulesBusy.value) return;
  const ruleName = gameRuleNameFromPath(target.name);
  if (!ruleName) return;
  gameRulesBusy.value = true;
  try {
    gameRules.value = await setGameRuleList('blacklist', [...gameRules.value.blacklist, ruleName]);
    summonCandidate.value = null;
    closeGameRuleMenus();
    statusMsg.value = `已排除 ${displayProcessName(target.name)}`;
    await refreshGameStatus();
  } catch (e) {
    errMsg.value = '保存黑名单失败：' + (e as Error).message;
  } finally {
    gameRulesBusy.value = false;
  }
}

async function saveGameRuleDraft(kind: 'blacklist' | 'whitelist'): Promise<void> {
  if (gameRulesBusy.value) return;
  const value = gameRuleDraft.value.trim();
  if (!value) return;
  gameRulesBusy.value = true;
  try {
    const rule = gameRuleNameFromPath(value);
    const next = gameRules.value[kind].includes(rule)
      ? gameRules.value[kind]
      : [...gameRules.value[kind], rule];
    gameRules.value = await setGameRuleList(kind, next);
    gameRuleDraft.value = '';
    statusMsg.value = `${kind === 'blacklist' ? '黑名单' : '白名单'}已更新`;
  } catch (e) {
    errMsg.value = (e as Error).message;
  } finally {
    gameRulesBusy.value = false;
  }
}

async function removeGameRule(kind: 'blacklist' | 'whitelist', value: string): Promise<void> {
  if (gameRulesBusy.value) return;
  gameRulesBusy.value = true;
  try {
    gameRules.value = await setGameRuleList(kind, gameRules.value[kind].filter((item) => item !== value));
  } catch (e) {
    errMsg.value = (e as Error).message;
  } finally {
    gameRulesBusy.value = false;
  }
}

async function addManualGameRule(): Promise<void> {
  if (gameRulesBusy.value) return;
  const value = manualGameDraft.value.trim();
  if (!value) return;
  gameRulesBusy.value = true;
  try {
    const rule = gameRuleNameFromPath(value);
    const next = gameRules.value.whitelist.includes(rule)
      ? gameRules.value.whitelist
      : [...gameRules.value.whitelist, rule];
    gameRules.value = await setGameRuleList('whitelist', next);
    manualGameDraft.value = '';
    statusMsg.value = '已添加游戏白名单';
  } catch (e) {
    errMsg.value = (e as Error).message;
  } finally {
    gameRulesBusy.value = false;
  }
}

async function chooseManualGameExe(): Promise<void> {
  const picked = await dialog.openFile([{ name: '可执行程序', extensions: ['exe'] }]);
  if (picked) {
    manualGameDraft.value = picked;
    await addManualGameRule();
  }
}

function beginSummonIdentification(detail: SummonCandidate): void {
  if (summonIdentifyTimer !== null) clearTimeout(summonIdentifyTimer);
  const sequence = ++summonIdentifySequence;
  const generation = Number(detail?.summonGeneration);
  const capturedPid = Number(detail?.pid);
  const validPid = detail?.valid && !detail.blocked && Number.isSafeInteger(capturedPid) && capturedPid > 0;
  const preferredPid = validPid ? capturedPid : 0;
  activeSummonPid = preferredPid;
  activeSummonGeneration = Number.isSafeInteger(generation) && generation > 0 ? generation : 0;
  blockedSummonPid = 0;

  // A summon without a valid captured PID must stay unrecognized. Never fall
  // back to the ordinary process scan, which could select another process.
  // The first summon snapshot is intentionally transport-only and has not
  // passed the blacklist check yet. Do not expose its process name in the
  // status bar; the async registration result below is authoritative.
  summonCandidate.value = null;
  showUnrecognizedPrompt.value = false;
  gameIdentifying.value = true;
  gameRuleMenu.value = null;
  statusMsg.value = '';
  errMsg.value = '';
  summonIdentifyTimer = setTimeout(async () => {
    gameIdentifying.value = false;
    summonIdentifyTimer = null;
    const blocked = preferredPid > 0 && blockedSummonPid === preferredPid;
    const detected = !blocked && preferredPid > 0 ? await refreshGameStatus(preferredPid) : null;
    if (sequence !== summonIdentifySequence) return;
    if (blocked) {
      syncGame(null);
      showUnrecognizedPrompt.value = true;
      return;
    }
    if (detected) {
      // The status subscriber intentionally ignores background polls while the
      // two-second summon state is visible. Apply the authoritative result now.
      syncGame(detected);
    } else {
      showUnrecognizedPrompt.value = true;
    }
  }, 2000);
}

function onSummonRegistrationResult(raw: SummonRegistrationResult): void {
  const pid = Number(raw?.pid);
  if (!pid || pid !== activeSummonPid) return;
  const generation = Number(raw?.summonGeneration);
  // Native recognition is asynchronous. Ignore a late result from an older
  // summon, especially when Windows reused the same PID for the next game.
  if (activeSummonGeneration > 0 && generation !== activeSummonGeneration) return;
  if (raw.blocked || !raw.valid) {
    blockedSummonPid = pid;
    summonCandidate.value = null;
    if (!gameIdentifying.value) syncGame(null);
    return;
  }
  blockedSummonPid = 0;
  summonCandidate.value = raw;
  if (!gameIdentifying.value && !game.value) showUnrecognizedPrompt.value = true;
}

function cancelSummonIdentification(): void {
  summonIdentifySequence += 1;
  if (summonIdentifyTimer !== null) clearTimeout(summonIdentifyTimer);
  summonIdentifyTimer = null;
  activeSummonPid = 0;
  activeSummonGeneration = 0;
  blockedSummonPid = 0;
  gameIdentifying.value = false;
  if (!game.value) showUnrecognizedPrompt.value = true;
}

function onGamepadSummon(e: Event): void {
  beginSummonIdentification((e as CustomEvent<SummonCandidate>).detail || {});
}

function onGamepadBack(e: Event): void {
  if (!gameRuleMenu.value) return;
  e.preventDefault();
  closeGameRuleMenus();
}

function onGamepadEdit(e: Event): void {
  if (gameIdentifying.value) return;
  e.preventDefault();
  if (!gameRuleMenu.value) {
    openGameRuleRoot();
  } else if (gameRuleMenu.value === 'root') {
    void refreshGameRecognition();
  }
}

onMounted(() => {
  unsubGame = subscribeGameStatus(syncGame);
  unsubSummonResult = onIpc<SummonRegistrationResult>('gamepad.summon.result', onSummonRegistrationResult);
  window.addEventListener('ipc:gamepad.summon', onGamepadSummon as EventListener);
  window.addEventListener('ipc:gamepad-back', onGamepadBack);
  window.addEventListener('ipc:gamepad-edit-game', onGamepadEdit);
});

onUnmounted(() => {
  cancelSummonIdentification();
  unsubGame?.();
  unsubGame = null;
  unsubSummonResult?.();
  unsubSummonResult = null;
  window.removeEventListener('ipc:gamepad.summon', onGamepadSummon as EventListener);
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
  window.removeEventListener('ipc:gamepad-edit-game', onGamepadEdit);
});
</script>

<template>
  <div class="game-recognition-control">
    <button
      ref="gameRuleTriggerEl"
      type="button"
      class="game-recognition-status"
      :class="{ identifying: gameIdentifying, unrecognized: showUnrecognizedPrompt, recognized: !!game && !gameIdentifying && !showUnrecognizedPrompt }"
      :disabled="gameIdentifying"
      data-gp-group="game-recognition"
      data-gp-global-y
      :title="recognitionTitle"
      @click="openGameRuleRoot"
    >
      <AppIcon :name="gameIdentifying ? 'search' : showUnrecognizedPrompt ? 'warning' : 'gamepad'" />
      <span class="game-recognition-label">{{ gameButtonStatus }}</span>
      <small v-if="!gameIdentifying" class="game-recognition-hint">按<strong class="game-action-key game-action-key-y">Y</strong>编辑</small>
    </button>

    <div v-if="gameRuleMenu" ref="gameRulePanelEl" class="game-recognition-popover" data-gp-modal>
      <div v-if="gameRuleMenu === 'root'" class="game-rule-panel">
        <div class="game-rule-panel-title"><AppIcon :name="game ? 'gamepad' : 'warning'" />{{ game ? '已识别游戏' : '未识别游戏' }}</div>
        <div class="game-rule-panel-sub">
          {{ ruleTarget?.name ? displayProcessName(ruleTarget.name) : '没有可用的游戏进程' }}
          <template v-if="ruleTarget?.pid"> · PID {{ ruleTarget.pid }}</template>
        </div>
        <div class="game-rule-actions game-rule-root-actions" data-gp-group="game-recognition-root">
          <button type="button" class="danger" :disabled="!ruleTarget?.name || gameRulesBusy" @click="addTargetToBlacklist">
            <strong class="game-action-key game-action-key-x">X</strong>排除此游戏到黑名单
          </button>
          <button type="button" :disabled="gameRulesBusy" @click="gameRuleMenu = 'edit'; void loadGameRules()">
            <AppIcon name="list" />编辑名单
          </button>
          <button type="button" :disabled="gameRulesBusy || gameIdentifying" @click="refreshGameRecognition">
            按<strong class="game-action-key game-action-key-y">Y</strong> 刷新
          </button>
          <button type="button" class="editor-cancel" @click="closeGameRuleMenus">
            按<strong class="game-action-key game-action-key-b">B</strong>取消
          </button>
        </div>
      </div>

      <div v-else class="game-rule-panel game-rule-editor">
        <div class="game-rule-panel-title"><AppIcon name="list" />编辑名单</div>
        <div class="game-rule-actions game-rule-tabs" data-gp-group="game-recognition-edit">
          <button type="button" :class="{ active: gameRuleMenu === 'edit' }" @click="gameRuleMenu = 'edit'">黑名单</button>
          <button type="button" :class="{ active: gameRuleMenu === 'whitelist' }" @click="gameRuleMenu = 'whitelist'">白名单</button>
          <button type="button" :class="{ active: gameRuleMenu === 'manual' }" @click="gameRuleMenu = 'manual'">手动添加</button>
          <button type="button" class="editor-cancel" @click="closeGameRuleMenus">按<strong class="game-action-key game-action-key-b">B</strong>取消</button>
        </div>

        <div v-if="gameRuleMenu === 'edit'" class="game-rule-list-block">
          <div class="game-rule-list-title">用户黑名单</div>
          <div v-if="gameRules.blacklist.length" class="game-rule-list">
            <div v-for="item in gameRules.blacklist" :key="`b-${item}`" class="game-rule-item">
              <span>{{ displayRuleName(item) }}</span>
              <button type="button" aria-label="移除黑名单规则" :disabled="gameRulesBusy" @click="removeGameRule('blacklist', item)"><AppIcon name="trash" /></button>
            </div>
          </div>
          <div v-else class="game-rule-empty">暂无用户添加的黑名单规则</div>
          <div class="game-rule-add-row">
            <input v-model="gameRuleDraft" type="text" placeholder="输入 exe 名称或规则" @keyup.enter="saveGameRuleDraft('blacklist')" />
            <button type="button" :disabled="gameRulesBusy || !gameRuleDraft.trim()" @click="saveGameRuleDraft('blacklist')"><AppIcon name="plus" />添加</button>
          </div>
        </div>

        <div v-else-if="gameRuleMenu === 'whitelist'" class="game-rule-list-block">
          <div class="game-rule-list-title">白名单</div>
          <div v-if="gameRules.whitelist.length" class="game-rule-list">
            <div v-for="item in gameRules.whitelist" :key="`w-${item}`" class="game-rule-item">
              <span>{{ displayRuleName(item) }}</span>
              <button type="button" aria-label="移除白名单规则" :disabled="gameRulesBusy" @click="removeGameRule('whitelist', item)"><AppIcon name="trash" /></button>
            </div>
          </div>
          <div class="game-rule-add-row">
            <input v-model="gameRuleDraft" type="text" placeholder="输入 exe 名称或规则" @keyup.enter="saveGameRuleDraft('whitelist')" />
            <button type="button" :disabled="gameRulesBusy || !gameRuleDraft.trim()" @click="saveGameRuleDraft('whitelist')"><AppIcon name="plus" />添加</button>
          </div>
        </div>

        <div v-else class="game-rule-list-block">
          <div class="game-rule-list-title">手动添加指定 exe</div>
          <div class="game-rule-add-row">
            <input v-model="manualGameDraft" type="text" placeholder="输入 exe 名称或完整路径" @keyup.enter="addManualGameRule" />
            <button type="button" :disabled="gameRulesBusy || !manualGameDraft.trim()" @click="addManualGameRule"><AppIcon name="plus" />添加</button>
            <button type="button" :disabled="gameRulesBusy" aria-label="选择 exe 文件" @click="chooseManualGameExe"><AppIcon name="folder" /></button>
          </div>
        </div>
      </div>

      <div v-if="statusMsg || errMsg" class="game-recognition-message" :class="{ error: !!errMsg }">
        {{ errMsg || statusMsg }}
      </div>
    </div>
  </div>
</template>

<style scoped>
.game-recognition-control {
  position: relative;
  flex: 0 1 auto;
  min-width: 0;
  z-index: 50;
}
.game-recognition-status {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  width: min(245px, 34vw);
  min-width: 100px;
  height: 34px;
  padding: 0 9px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 8px;
  background: color-mix(in srgb, var(--bg-input) 85%, transparent);
  color: var(--text-dim);
  font: inherit;
  font-size: 11px;
  text-align: left;
  cursor: pointer;
}
.game-recognition-status span {
  min-width: 0;
  flex: 1 1 auto;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.game-recognition-hint {
  flex: 0 0 auto;
  color: var(--accent);
  font-size: 10px;
  white-space: nowrap;
}
.game-action-key {
  font-weight: 800;
  letter-spacing: 0;
}
.game-action-key-x { font-weight: 800; }
.game-action-key-y { color: #f5b942; }
.game-action-key-b { color: var(--danger); }
.game-recognition-status :deep(svg) { width: 15px; height: 15px; flex: 0 0 auto; }
.game-recognition-status.recognized { color: var(--text); }
.game-recognition-status.recognized :deep(svg) { color: var(--accent); }
.game-recognition-status.unrecognized { color: #f5b942; border-color: color-mix(in srgb, #f5b942 42%, transparent); }
.game-recognition-status.unrecognized :deep(svg) { color: #f5b942; }
.game-recognition-status.identifying { color: var(--accent); cursor: wait; }
.game-recognition-status:disabled { opacity: 1; }
.game-recognition-status:focus-visible { outline: none; box-shadow: var(--focus-ring); }

.game-recognition-popover {
  position: absolute;
  top: calc(100% + 7px);
  left: 0;
  width: min(460px, calc(100vw - 24px));
  padding: 10px 12px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 10px;
  background: color-mix(in srgb, var(--bg-solid) 96%, #101722);
  box-shadow: 0 12px 30px rgba(0, 0, 0, 0.4);
}
.game-rule-panel { display: grid; gap: 8px; }
.game-rule-panel-title { display: flex; align-items: center; gap: 7px; font-size: 13px; font-weight: 700; }
.game-rule-panel-title :deep(svg) { color: #f5b942; width: 15px; height: 15px; }
.game-rule-panel-sub, .game-rule-list-title { color: var(--text-dim); font-size: 11px; }
.game-rule-actions { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 7px; }
.game-rule-root-actions { grid-template-columns: repeat(2, minmax(0, 1fr)); }
.game-rule-actions button, .game-rule-add-row button {
  min-height: 34px;
  border: 1px solid rgba(255,255,255,.06);
  border-radius: 8px;
  background: var(--bg-input);
  color: var(--text);
  cursor: pointer;
}
.game-rule-actions button { display: inline-flex; align-items: center; justify-content: center; gap: 5px; padding: 6px 8px; font-size: 11px; }
.game-rule-actions button :deep(svg), .game-rule-add-row button :deep(svg) { width: 14px; height: 14px; }
.game-rule-actions button.danger { color: var(--danger); }
.game-rule-actions button.active { color: var(--accent); border-color: color-mix(in srgb, var(--accent) 45%, transparent); }
.game-rule-actions button:disabled, .game-rule-add-row button:disabled { opacity: .5; cursor: default; }
.game-rule-editor { gap: 10px; }
.game-rule-tabs { grid-template-columns: repeat(4, minmax(0, 1fr)); }
.game-rule-list-block { display: grid; gap: 7px; }
.game-rule-list { display: grid; gap: 4px; max-height: 150px; overflow-y: auto; }
.game-rule-empty { color: var(--text-dim); font-size: 11px; }
.game-rule-system-block { display: grid; gap: 6px; margin-top: 3px; }
.game-rule-section-toggle { display: flex; align-items: center; justify-content: space-between; gap: 8px; min-height: 32px; padding: 6px 8px; border: 1px solid rgba(255,255,255,.06); border-radius: 7px; background: var(--bg-input); color: var(--text-dim); font-size: 11px; text-align: left; cursor: pointer; }
.game-rule-section-toggle > span:last-child { color: var(--accent); font-size: 15px; line-height: 1; }
.game-rule-system-list { max-height: 180px; }
.game-rule-item { display: flex; align-items: center; gap: 8px; min-height: 28px; padding: 4px 7px; border-radius: 6px; background: var(--bg-input); font-size: 11px; }
.game-rule-item span { min-width: 0; flex: 1; overflow-wrap: anywhere; }
.game-rule-item.protected small { color: var(--text-dim); font-size: 10px; white-space: nowrap; }
.game-rule-item button { width: 26px; height: 26px; padding: 0; border: 0; border-radius: 6px; background: transparent; color: var(--danger); }
.game-rule-add-row { display: grid; grid-template-columns: minmax(0, 1fr) auto auto; gap: 6px; }
.game-rule-add-row input { min-width: 0; height: 34px; padding: 6px 8px; border: 1px solid #2a3342; border-radius: 7px; background: var(--bg-input); color: var(--text); font-size: 11px; }
.game-rule-add-row button { display: inline-flex; align-items: center; justify-content: center; gap: 4px; padding: 6px 9px; font-size: 11px; }
.game-rule-add-row button:last-child { width: 34px; padding: 0; }
.game-rule-add-row input:focus-visible, .game-rule-add-row button:focus-visible, .game-rule-actions button:focus-visible { outline: none; box-shadow: var(--focus-ring); }
.game-recognition-message { margin-top: 8px; color: var(--ok); font-size: 11px; }
.game-recognition-message.error { color: var(--danger); }
@media (max-width: 620px) {
  .game-recognition-status { width: 188px; min-width: 120px; }
  .game-rule-actions, .game-rule-tabs { grid-template-columns: repeat(2, minmax(0, 1fr)); }
}
</style>
