<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import { keyboard, windowApi } from '@/bridge/api';
import { focusGamepadElement } from '@/gamepad/focus';
import { detectedGameName } from '@/bridge/gamedetect';
import { validateLockedGameTarget, type LockedGameTarget } from '@/bridge/gameQuickSession';
import { startGameValueSession, subscribeGameValueState, gameValueCommand, cancelGameValueScan, stopGameValueSession, getGameValueDraft, saveGameValueDraft, type GameValueState } from '@/bridge/gameValueScan';

const props = defineProps<{ target: LockedGameTarget }>();
const emit = defineEmits<{ (e: 'close'): void }>();
const panel = ref<HTMLElement | null>(null), state = ref<GameValueState | null>(null);
const firstValue = ref(''), nextValue = ref(''), desiredValue = ref('99999');
const operation = ref<'' | 'first' | 'next' | 'writeAll'>('');
const starting = ref(true), returning = ref(false), error = ref('');
const name = computed(() => detectedGameName(props.target) || props.target.name);
const busy = computed(() => starting.value || !!operation.value || returning.value || ['scanning', 'writing'].includes(state.value?.stage || ''));
const screen = computed(() => {
  const value = state.value;
  if (starting.value) return 'starting';
  if (operation.value === 'writeAll' || value?.stage === 'writing') return 'writing';
  if (operation.value || value?.stage === 'scanning') return 'scanning';
  if (!value || value.stage === 'closed') return 'unavailable';
  if (value.stage === 'failed') return 'retry';
  if (value.lastWriteCount > 0) return 'done';
  if (value.stage === 'idle') return 'first';
  if (value.count === 0) return 'empty';
  return value.scanRound > 0 && value.count <= value.bulkLimit ? 'modify' : 'change';
});
const step = computed(() => ['first', 'starting'].includes(screen.value) ? 1 : ['modify', 'writing', 'done'].includes(screen.value) ? 3 : 2);
const canContinue = computed(() => !busy.value && !!(screen.value === 'first' ? firstValue.value : nextValue.value).trim());
const scanPercent = computed(() => state.value?.totalBytes ? Math.min(100, Math.floor(state.value.scannedBytes / state.value.totalBytes * 100)) : 0);
async function cancelSearch(): Promise<void> {
  try { await cancelGameValueScan(); } catch (cause) { error.value = (cause as Error).message; }
}
const canWrite = computed(() => !busy.value && !!desiredValue.value.trim() && state.value?.stage === 'ready' && state.value.scanRound > 0 && state.value.count > 0 && state.value.count <= state.value.bulkLimit);
let unsub: (() => void) | null = null, mounted = true;
function saveDraft(): void { saveGameValueDraft(props.target, { firstValue: firstValue.value, nextValue: nextValue.value, desiredValue: desiredValue.value }); }
function receive(value: GameValueState | null): void {
  if (!mounted) return;
  if (!value || (value.pid === props.target.pid && value.processCreated === props.target.processCreated)) state.value = value;
}
async function initialize(): Promise<void> {
  starting.value = true; error.value = '';
  try {
    const value = await startGameValueSession(props.target);
    if (mounted) {
      state.value = value;
      const draft = getGameValueDraft(props.target);
      firstValue.value = draft.firstValue; nextValue.value = draft.nextValue; desiredValue.value = draft.desiredValue;
    }
  } catch (cause) { if (mounted) error.value = (cause as Error).message; }
  finally { if (mounted) starting.value = false; }
}
async function run(action: 'first' | 'next' | 'writeAll'): Promise<void> {
  if (busy.value) return;
  const value = (action === 'first' ? firstValue.value : action === 'next' ? nextValue.value : desiredValue.value).trim();
  if (!value) { error.value = action === 'writeAll' ? '填一下你想改成的数字。' : '填一下游戏里看到的数字。'; return; }
  saveDraft(); operation.value = action; error.value = '';
  try {
    const result = await gameValueCommand(action, { value, dataType: 'int32' });
    if (mounted) { state.value = result; if (action !== 'writeAll') { nextValue.value = ''; saveDraft(); } }
  } catch (cause) { if (mounted) error.value = (cause as Error).message; }
  finally { if (mounted) operation.value = ''; }
}
async function restart(): Promise<void> {
  if (busy.value) return;
  const last = nextValue.value || state.value?.searchValue || firstValue.value;
  await stopGameValueSession();
  await initialize();
  if (mounted) { firstValue.value = last; saveDraft(); }
}
async function returnToGame(): Promise<void> {
  if (busy.value) return;
  returning.value = true; error.value = ''; saveDraft();
  try {
    const current = await validateLockedGameTarget();
    if (!current || current.pid !== props.target.pid || current.processCreated !== props.target.processCreated || current.path.toLowerCase() !== props.target.path.toLowerCase()) throw new Error('当前游戏已退出或改变，请重新获取游戏');
    if (!(await windowApi.minimize())) throw new Error('未能返回游戏，请再试一次');
  } catch (cause) { if (mounted) error.value = (cause as Error).message; }
  finally { if (mounted) returning.value = false; }
}
async function inputKeyboard(selector: string): Promise<void> {
  panel.value?.querySelector<HTMLInputElement>(selector)?.focus({ preventScroll: true });
  try { await keyboard.open(); } catch (cause) { error.value = (cause as Error).message; }
}
function close(): void { if (operation.value === 'writeAll' || state.value?.stage === 'writing') return; saveDraft(); if (operation.value === 'first' || operation.value === 'next' || state.value?.stage === 'scanning') void stopGameValueSession(); emit('close'); }
function back(event: Event): void { event.preventDefault(); event.stopImmediatePropagation(); close(); }
function keydown(event: KeyboardEvent): void { if (event.key === 'Escape') back(event); }
watch([firstValue, nextValue, desiredValue], saveDraft, { flush: 'sync' });
watch(screen, () => { void nextTick(() => focusGamepadElement(panel.value?.querySelector<HTMLElement>('input:not(:disabled), button:not(:disabled)') || null)); });
onMounted(() => {
  unsub = subscribeGameValueState(receive);
  window.addEventListener('ipc:gamepad-back', back, true); window.addEventListener('keydown', keydown, true);
  void initialize();
});
onBeforeUnmount(() => { saveDraft(); mounted = false; unsub?.(); window.removeEventListener('ipc:gamepad-back', back, true); window.removeEventListener('keydown', keydown, true); });
</script>

<template>
  <Teleport to="body">
    <div class="value-scan-shade" @click.self="close">
      <section ref="panel" class="value-scan" data-gp-modal data-gp-game-quick-dialog data-value-scan role="dialog" aria-modal="true" :aria-label="name + ' 数值修改'">
        <header class="value-game" :title="name">{{ name }}</header>
        <ol class="value-steps" aria-label="修改步骤"><li v-for="(label, index) in ['记下数字', '数字变小', '改成新数']" :key="label" :aria-current="step === index + 1 ? 'step' : undefined" :class="{ done: step > index + 1 }">{{ index + 1 }} {{ label }}</li></ol>
        <div class="value-body" aria-live="polite">
          <template v-if="screen === 'first'">
            <h3>游戏里现在是多少？</h3><p class="value-hint">先填游戏里看到的金钱或数量。</p>
            <label for="value-first">现在的数值</label><div class="value-input-row"><input id="value-first" v-model="firstValue" data-value-search inputmode="numeric" autocomplete="off" maxlength="64" placeholder="例如 5000" @keydown.enter="run('first')" /><button type="button" aria-label="输入现在的数值" @click="inputKeyboard('[data-value-search]')">键盘</button></div>
            <button type="button" class="value-primary" data-value-first :disabled="!canContinue" @click="run('first')">开始找这个数</button>
          </template>
          <template v-else-if="screen === 'change'">
            <h3>{{ state && state.scanRound > 0 ? '再变小一次' : '让数字变小一次' }}</h3><p class="value-hint">{{ state && state.scanRound > 0 ? '范围还比较大，再花一点就能缩小。' : '回游戏花一点，再填新数字。' }}</p>
            <div class="value-change-line"><div class="value-old"><span class="value-label">原来的数</span><span>{{ state?.searchValue }}</span></div><span class="value-arrow" aria-hidden="true">→</span><div class="value-change-input"><label for="value-next">变小后的数</label><div class="value-input-row"><input id="value-next" v-model="nextValue" data-value-next inputmode="numeric" autocomplete="off" maxlength="64" placeholder="新数字" :disabled="busy" @keydown.enter="run('next')" /><button type="button" :disabled="busy" aria-label="输入变化后的数值" @click="inputKeyboard('[data-value-next]')">键盘</button></div></div></div>
            <button type="button" class="value-primary" data-value-refine :disabled="!canContinue" @click="run('next')">找出变化的数</button>
          </template>
          <template v-else-if="screen === 'modify'">
            <h3>找到了，可以一起改</h3><p class="value-hint">有 {{ state?.count }} 处数值跟着变化，将一起修改。</p>
            <label for="value-desired">你想改成多少？</label><div class="value-input-row"><input id="value-desired" v-model="desiredValue" data-value-new inputmode="numeric" autocomplete="off" maxlength="64" placeholder="例如 99999" :disabled="busy" @keydown.enter="run('writeAll')" /><button type="button" :disabled="busy" aria-label="输入想改成的数值" @click="inputKeyboard('[data-value-new]')">键盘</button></div>
            <button type="button" class="value-primary" data-value-write :disabled="!canWrite" @click="run('writeAll')">全部改为 {{ desiredValue || '…' }}</button>
            <p class="value-note"><AppIcon name="pause" />扫描沿用CE设置。</p>
          </template>
          <template v-else-if="screen === 'done'">
            <h3>改好了</h3><div class="value-result">{{ state?.lastWriteValue }}</div><p class="value-hint">{{ state?.lastWriteCount }} 处已一起修改。</p><button type="button" class="value-primary" data-value-return :disabled="busy" @click="returnToGame">返回游戏</button>
          </template>
          <template v-else-if="['starting', 'scanning', 'writing'].includes(screen)">
            <h3>{{ screen === 'writing' ? '正在修改数值' : screen === 'starting' ? '正在连接当前游戏' : '正在找这个数' }}</h3><p class="value-hint">{{ screen === 'writing' ? '稍等一下。' : screen === 'scanning' ? (state?.totalBytes ? 'CE正在搜索 · ' + scanPercent + '% · ' + Math.floor((state?.scanElapsedMs || 0) / 1000) + ' 秒' : 'CE正在准备搜索…') : '稍等一下。' }}</p>
            <template v-if="screen === 'scanning'"><progress data-value-progress :value="scanPercent" max="100" aria-label="CE搜索进度"></progress><button type="button" class="value-primary" data-value-cancel @click="cancelSearch">取消搜索</button></template>
          </template>
          <template v-else>
            <h3>{{ screen === 'empty' ? '这次没对上' : screen === 'retry' ? '这次没有完成' : '暂时无法连接游戏' }}</h3><p class="value-hint">{{ screen === 'empty' ? '核对游戏里当前的数字，再开始一次。' : state?.message || '确认游戏还在运行，再试一次。' }}</p><button type="button" class="value-primary" :disabled="busy" @click="restart">重新开始</button>
          </template>
          <p v-if="error" class="value-error" role="alert">{{ error }}</p>
        </div>
        <footer class="value-buttons" :class="{ single: screen === 'done' }"><button v-if="screen !== 'done'" type="button" data-value-return :disabled="busy" @click="returnToGame">返回游戏</button><button type="button" class="value-close" :disabled="screen === 'writing'" @click="close"><strong>B</strong>关闭页面</button></footer>
      </section>
    </div>
  </Teleport>
</template>

<style scoped>
.value-scan-shade { position: fixed; inset: 0; z-index: 31000; background: rgba(0,0,0,.25); display: flex; align-items: center; justify-content: center; padding: 12px; box-sizing: border-box; }
.value-scan { width: 340px; max-width: 100%; max-height: calc(100vh - 32px); overflow-y: auto; box-sizing: border-box; border: 1px solid rgba(255,255,255,.22); border-radius: 14px; color: var(--text); background: var(--bg-solid); box-shadow: 0 16px 48px rgba(0,0,0,.38); font-size: 12px; }
.value-game { padding: 15px 16px; border-bottom: 1px solid rgba(255,255,255,.12); overflow-wrap: anywhere; font-size: 14px; font-weight: 650; }
.value-steps { margin: 16px 16px 0; padding: 0; display: flex; gap: 8px; list-style: none; }
.value-steps li { flex: 1; min-width: 0; border-top: 3px solid var(--bg-input); padding-top: 7px; color: var(--text-dim); font-size: 10px; }
.value-steps li[aria-current=step] { color: var(--text); border-top-color: var(--accent); }
.value-steps li.done { border-top-color: rgba(255,255,255,.25); }
.value-body { padding: 20px 16px 18px; min-height: 210px; }
.value-body h3 { margin: 0 0 9px; font-size: 16px; font-weight: 600; }
.value-scan p { margin: 0; line-height: 1.5; }
.value-hint { color: var(--text-dim); margin-bottom: 18px !important; }
.value-scan label, .value-label { display: block; color: var(--text-dim); margin-bottom: 7px; font-size: 11px; }
.value-scan input, .value-scan button { box-sizing: border-box; min-width: 0; min-height: 44px; border: 1px solid rgba(255,255,255,.22); border-radius: 8px; background: var(--bg-input); color: var(--text); font: inherit; }
.value-scan input { width: 100%; min-height: 54px; padding: 9px 10px; font-size: 22px; font-variant-numeric: tabular-nums; }
.value-scan button { padding: 9px; cursor: pointer; }
.value-scan button:disabled { opacity: .45; cursor: default; }
.value-scan button:focus-visible, .value-scan button.gp-focus, .value-scan input:focus { outline: 2px solid var(--accent); outline-offset: -2px; }
.value-input-row { display: grid; grid-template-columns: minmax(0,1fr) 44px; gap: 5px; }
.value-input-row button { padding: 4px; font-size: 10px; }
.value-scan .value-primary { display: block; width: 100%; margin-top: 20px; border-color: var(--accent); background: color-mix(in srgb, var(--accent) 18%, var(--bg-input)); color: var(--text); font-weight: 650; }
.value-change-line { display: grid; grid-template-columns: minmax(60px,.7fr) 18px minmax(0,1.4fr); gap: 6px; align-items: end; }
.value-old > span:last-child { display: flex; align-items: center; min-height: 54px; font-size: 19px; font-variant-numeric: tabular-nums; overflow-wrap: anywhere; }
.value-arrow { display: flex; align-items: center; justify-content: center; min-height: 54px; color: var(--text-dim); font-size: 21px; }
.value-change-input { min-width: 0; }
.value-change-input input { padding-left: 7px; padding-right: 7px; font-size: 20px; }
.value-note { display: flex; align-items: flex-start; gap: 5px; margin-top: 13px !important; color: var(--text-dim); font-size: 10px; }
.value-note :deep(svg) { width: 13px; height: 13px; flex-shrink: 0; margin-top: 1px; }
.value-scan progress { display: block; width: 100%; height: 7px; accent-color: var(--accent); }
.value-result { font-size: 30px; font-variant-numeric: tabular-nums; margin: 18px 0 10px; overflow-wrap: anywhere; }
.value-buttons { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: 6px; padding: 12px; border-top: 1px solid rgba(255,255,255,.12); }
.value-buttons.single { grid-template-columns: 1fr; }
.value-error { color: var(--danger); margin-top: 12px !important; overflow-wrap: anywhere; }
.value-close strong { color: var(--danger); margin-right: 4px; }
</style>