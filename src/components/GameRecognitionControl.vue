<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import GameQuickActions from '@/components/GameQuickActions.vue';
import {
  refreshGameStatus,
  refreshGameStatusStrict,
  subscribeGameStatus,
  detectedGameName,
  type DetectedGame,
} from '@/bridge/gamedetect';
import { on as onIpc } from '@/bridge/ipc';
import { focusGamepadElement, setGamepadFocused } from '@/gamepad/focus';
import { getLockedGameTarget, isGameTargetSame, unlockGameTarget } from '@/bridge/gameQuickSession';

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
const gameRuleMenu = ref<'quick' | null>(null);
const gameRuleTriggerEl = ref<HTMLElement | null>(null);
const gameRulePanelEl = ref<HTMLElement | null>(null);
const statusMsg = ref('');
const errMsg = ref('');

let unsubGame: (() => void) | null = null;
let unsubSummonResult: (() => void) | null = null;
let summonIdentifyTimer: ReturnType<typeof setTimeout> | null = null;
let summonIdentifySequence = 0;
let activeSummonPid = 0;
let activeSummonGeneration = 0;
let blockedSummonPid = 0;
let refreshAfterIdentification = false;

function displayProcessName(value: string): string {
  return value.trim().replace(/\.exe$/i, '').trim();
}

const gameDisplayName = computed(() => {
  if (gameIdentifying.value) return '识别中…';
  const current = game.value;
  if (!current) return '未识别游戏';
  return detectedGameName(current) || displayProcessName(current.name || '') || '未识别游戏';
});

const recognitionTitle = computed(() => game.value
  ? `当前游戏：${gameDisplayName.value}，按 Y 编辑`
  : '未识别游戏，按 Y 编辑');

const gameButtonStatus = computed(() => gameIdentifying.value
  ? '识别中…'
  : game.value
    ? `${gameDisplayName.value} · 已识别`
    : '未识别游戏');

function syncGame(next: DetectedGame | null, preserveRuleMenu = false): void {
  const locked = getLockedGameTarget();
  const previous = game.value;
  const menuWasOpen = gameRuleMenu.value !== null;
  // A locked top-menu target owns the quick-action session. Background polls
  // must not replace it with a newly detected foreground game.
  if (locked && !next) {
    // A real disappearance is authoritative: do not leave the top menu bound
    // to a dead PID while background polling continues.
    unlockGameTarget();
    window.dispatchEvent(new CustomEvent('game-quick-target-lost'));
  } else if (locked && !isGameTargetSame(locked, next)) {
    return;
  }
  game.value = next;
  if (gameIdentifying.value) return;
  if (next) {
    summonCandidate.value = null;
    // Background recognition publishes the same PID repeatedly. Keeping the
    // menu open across those snapshots is essential for controller users:
    // otherwise a normal poll races the Y-open animation and folds the menu
    // back up. A genuinely different game still invalidates the old menu.
    const sameTarget = isGameTargetSame(previous, next);
    if (!preserveRuleMenu && menuWasOpen && previous && !sameTarget) {
      gameRuleMenu.value = null;
    }
  }
}

function focusGameRulePanel(): void {
  nextTick(() => {
    const candidates = gameRulePanelEl.value
      ? Array.from(gameRulePanelEl.value.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled)'))
      : [];
    const first = candidates.find((el) =>
      !el.hasAttribute('data-gp-ignore') &&
      !el.closest('[data-gp-group="game-quick-game-controls"]'),
    );
    if (first) focusGamepadElement(first);
  });
}

async function refreshGameRecognition(): Promise<void> {
  if (gameIdentifying.value) {
    refreshAfterIdentification = true;
    return;
  }
  statusMsg.value = '';
  errMsg.value = '';
  gameIdentifying.value = true;
  try {
    const detected = await refreshGameStatusStrict();
    gameIdentifying.value = false;
    // Refresh is an action inside the root rule menu. Keep that menu open so
    // Y does not look like a back/close operation after the result arrives.
    syncGame(detected, true);
    // A Y request during recognition must finish in the same menu action:
    // recognized game -> quick actions; no game -> recognition root.
    gameRuleMenu.value = 'quick';
    refreshAfterIdentification = false;
    statusMsg.value = detected ? '已刷新游戏识别' : '刷新完成，当前未识别游戏';
  } catch (e) {
    gameIdentifying.value = false;
    refreshAfterIdentification = false;
    errMsg.value = '刷新游戏识别失败：' + (e as Error).message;
  }
}

watch(gameRuleMenu, (menu) => {
  if (menu) focusGameRulePanel();
});

function openGameRuleRoot(): void {
  statusMsg.value = '';
  errMsg.value = '';
  // There is exactly one top-level game menu. It is valid before, during and
  // after recognition; Y refreshes the same menu instead of switching roots.
  gameRuleMenu.value = 'quick';
}

function closeQuickMenu(): void {
  gameRuleMenu.value = null;
  // 顶部识别入口只保留鼠标/触屏点击；Y 是它的手柄入口，因此关闭后也
  // 不把 DOM/视觉焦点恢复到这里，避免再次出现“手柄可选中”的假象。
  nextTick(() => {
    gameRuleTriggerEl.value?.blur();
    setGamepadFocused(null);
  });
}

function closeGameRuleMenus(): void {
  gameRuleMenu.value = null;
  nextTick(() => {
    gameRuleTriggerEl.value?.blur();
    setGamepadFocused(null);
  });
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
          if (refreshAfterIdentification) {
        gameRuleMenu.value = 'quick';
        refreshAfterIdentification = false;
      }
      return;
    }
    if (detected) {
      // The status subscriber intentionally ignores background polls while the
      // two-second summon state is visible. Apply the authoritative result now.
      syncGame(detected);
      if (refreshAfterIdentification) {
        gameRuleMenu.value = 'quick';
        refreshAfterIdentification = false;
      }
    } else {
      if (refreshAfterIdentification) {
        gameRuleMenu.value = 'quick';
        refreshAfterIdentification = false;
      }
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
}

function cancelSummonIdentification(): void {
  summonIdentifySequence += 1;
  if (summonIdentifyTimer !== null) clearTimeout(summonIdentifyTimer);
  summonIdentifyTimer = null;
  activeSummonPid = 0;
  activeSummonGeneration = 0;
  blockedSummonPid = 0;
  gameIdentifying.value = false;
}

function onGamepadSummon(e: Event): void {
  beginSummonIdentification((e as CustomEvent<SummonCandidate>).detail || {});
}

function onGamepadBack(e: Event): void {
  if (document.querySelector('[data-gp-game-quick-dialog]')) {
    e.preventDefault();
    return;
  }
  // A teleported dropdown is the deepest open editor. Let Dropdown consume B
  // first so it closes the option list instead of collapsing its parent panel.
  if (document.querySelector('[aria-expanded="true"][aria-haspopup="listbox"]')) {
    return;
  }
  // Transition nodes can remain mounted briefly after a submenu closes. Only
  // an actually expanded panel owns B; otherwise B must continue to the next
  // parent level and eventually close the top menu.
  if (document.querySelector('[data-gp-custom-panel][data-gp-expanded="true"]')) {
    e.preventDefault();
    window.dispatchEvent(new CustomEvent('game-quick-custom-back'));
    return;
  }
  if (document.querySelector('[data-gp-game-rules][data-gp-expanded="true"]')) {
    e.preventDefault();
    window.dispatchEvent(new CustomEvent('game-quick-rules-back'));
    return;
  }
  if (!gameRuleMenu.value) return;
  e.preventDefault();
  closeGameRuleMenus();
}

function onGamepadEdit(e: Event): void {
  if (document.querySelector('[data-gp-game-quick-dialog]')) {
    e.preventDefault();
    return;
  }
  // Y is a top-bar command. Do not let it refresh/search while a teleported
  // dropdown owns the current controller focus.
  if (document.querySelector('[aria-expanded="true"][aria-haspopup="listbox"]')) {
    e.preventDefault();
    return;
  }
  if (gameIdentifying.value) {
    e.preventDefault();
    refreshAfterIdentification = true;
    gameRuleMenu.value = 'quick';
    return;
  }
  e.preventDefault();
  if (!gameRuleMenu.value) {
    openGameRuleRoot();
  } else if (gameRuleMenu.value === 'quick') {
    window.dispatchEvent(new CustomEvent('game-quick-refresh'));
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
      :class="{ identifying: gameIdentifying, unrecognized: !game, recognized: !!game && !gameIdentifying }"
      :disabled="false"
      data-gp-group="game-recognition"
      data-gp-global-y
      data-gp-ignore
      :title="recognitionTitle"
      @click="openGameRuleRoot"
    >
      <AppIcon :name="gameIdentifying ? 'search' : 'gamepad'" />
      <span class="game-recognition-label">{{ gameButtonStatus }}</span>
      <small class="game-recognition-hint">按<strong class="game-action-key game-action-key-y">Y</strong>编辑</small>
    </button>

    <div v-if="gameRuleMenu" ref="gameRulePanelEl" class="game-recognition-popover" data-gp-modal>
      <div v-if="gameRuleMenu === 'quick'" class="game-rule-panel game-quick-panel">
        <GameQuickActions
          :game="game"
          :open="gameRuleMenu === 'quick'"
          @game-updated="(next) => syncGame(next, true)"
          @status="({ message, error }) => { statusMsg = message || ''; errMsg = error || ''; }"
          @close-request="closeQuickMenu"
        />
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
