<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import Dropdown from '@/components/Dropdown.vue';
import Slider from '@/components/Slider.vue';
import { on } from '@/bridge/ipc';
import { LEFT_TOUCHPAD_MODES, RIGHT_TOUCHPAD_MODES, SINGLE_TOUCHPAD_MODES, SCREEN_TOUCHPAD_LAYOUTS,
  SCREEN_SUMMON_POSITIONS, STANDALONE_SPECIAL_MODES, screenTouchpadsGet, screenTouchpadsSet, screenTouchpadProfile, screenTouchpadDefaults,
  type ScreenTouchpadConfig, type ScreenTouchpadState } from '@/bridge/screenTouchpads';

const props = withDefaults(defineProps<{ disabled?: boolean; persona?: string; steamDeckEnabled?: boolean; ps5Enabled?: boolean }>(),
  { disabled: false, persona: 'steamdeck', steamDeckEnabled: false, ps5Enabled: false });
// The parent keys this instance by persona; pending saves keep their original slot.
const profile = screenTouchpadProfile(props.persona);
const state = ref<ScreenTouchpadState>({ ...screenTouchpadDefaults(profile), persona: profile, ok: true, available: true, visible: false });
const loading = ref(true), saving = ref(false);
const error = ref('');
let disposed = false, generation = 0;
let unsubscribe: (() => void) | undefined;
let queued: Partial<ScreenTouchpadConfig> = {};
let debounce: ReturnType<typeof setTimeout> | undefined;
let inFlight: Promise<ScreenTouchpadState> | undefined;
const locked = computed(() => props.disabled || loading.value);
const isPs4 = computed(() => props.persona === 'dualshock4');
const psAllowed = computed(() => profile === 'dualsense-edge' && props.ps5Enabled &&
  (state.value.ps5Available === true || state.value.ps4Available === true));
const psLabel = (label: string) => isPs4.value ? label.replace('PS5', 'PS4') : label;
const deckAllowed = computed(() => profile === 'steamdeck' && props.steamDeckEnabled && state.value.steamDeckAvailable === true);
// Keep the preset label visible while the virtual target is off, but not selectable.
const leftModes = computed(() => LEFT_TOUCHPAD_MODES.filter(mode =>
  (mode.value !== 'steamdeck' || profile === 'steamdeck') && (mode.value !== 'dualsense' || profile === 'dualsense-edge'))
  .map(mode => ({ ...mode, label: psLabel(mode.label), disabled: (mode.value === 'steamdeck' && !deckAllowed.value) || (mode.value === 'dualsense' && !psAllowed.value) })));
const rightModes = computed(() => RIGHT_TOUCHPAD_MODES.filter(mode =>
  (mode.value !== 'steamdeck' || profile === 'steamdeck') && (mode.value !== 'dualsense' || profile === 'dualsense-edge'))
  .map(mode => ({ ...mode, label: psLabel(mode.label), disabled: (mode.value === 'steamdeck' && !deckAllowed.value) || (mode.value === 'dualsense' && !psAllowed.value) })));
const singleModes = computed(() => SINGLE_TOUCHPAD_MODES.filter(mode => mode.value !== 'dualsense' || profile === 'dualsense-edge')
  .map(mode => ({ ...mode, label: psLabel(mode.label), disabled: mode.value === 'dualsense' && !psAllowed.value })));
const rearSupported = profile === 'steamdeck' || props.persona === 'dualsense-edge';
const standaloneSpecial = profile === 'disabled';
const specialOptions = computed(() => standaloneSpecial ? STANDALONE_SPECIAL_MODES : profile === 'steamdeck'
  ? [{ value: 0, label: '关闭' }, { value: 1, label: '只开启 Steam' }, { value: 2, label: '只开启三点' }, { value: 3, label: '都开启' }]
  : isPs4.value
    ? [{ value: 0, label: '关闭' }, { value: 1, label: '都开启', sub: 'PS 按键' }]
    : profile === 'dualsense-edge'
    ? [{ value: 0, label: '关闭' }, { value: 1, label: '只开启 PS' }, { value: 2, label: '只开启静音' }, { value: 3, label: '都开启' }]
    : profile === 'elite' ? [{ value: 0, label: '关闭' }, { value: 1, label: '都开启', sub: 'Xbox 按键' }] : [{ value: 0, label: '关闭' }]);
// Old Xbox toggle configurations used mask 3, but Xbox only exposes Guide.
const specialSelection = computed(() => standaloneSpecial ? state.value.standaloneSpecialMode ?? 'off'
  : profile === 'elite' || isPs4.value ? state.value.specialMask & 1 : state.value.specialMask);
const rearOptions = computed(() => profile === 'steamdeck'
  ? [{ value: 10, label: 'L5+R5' }, { value: 5, label: 'L4+R4' }, { value: 15, label: '全开启' }, { value: 0, label: '全关闭' }]
  : profile === 'dualsense-edge'
    ? [{ value: 5, label: 'LFN+RFN' }, { value: 10, label: 'LB+RB' }, { value: 15, label: '全开启' }, { value: 0, label: '全关闭' }]
    : [{ value: 0, label: '全关闭' }]);
const overlaysEnabled = computed(() => (standaloneSpecial && state.value.standaloneSpecialMode !== undefined && state.value.standaloneSpecialMode !== 'off') || state.value.layout !== 'off' || state.value.summonPosition !== 'off' || state.value.specialMask !== 0 || state.value.rearMask !== 0 || state.value.summonEnabled || state.value.specialEnabled || state.value.rearEnabled);
const mouseEnabled = computed(() => state.value.layout === 'single' ? state.value.singleMode === 'mouse' : state.value.leftMode === 'mouse' || state.value.rightMode === 'mouse');
// Keep actionable errors; omit the normal-operation explanatory text.
const status = computed(() => error.value || (state.value.error
  ? `触摸板操作失败（Windows ${state.value.error}）；请关闭开关后重试。` : ''));
async function refresh(): Promise<void> {
  if (disposed || saving.value || Object.keys(queued).length) return;
  const ticket = ++generation;
  try { const next = await screenTouchpadsGet(profile); if (!disposed && ticket === generation) { state.value = next; error.value = ''; } }
  catch (e) { if (!disposed && ticket === generation) error.value = e instanceof Error ? e.message : String(e); }
  finally { if (!disposed && ticket === generation) loading.value = false; }
}
async function flush(): Promise<void> {
  if (debounce !== undefined) { clearTimeout(debounce); debounce = undefined; }
  if (disposed || locked.value || saving.value || !Object.keys(queued).length) return;
  const patch = queued; queued = {}; ++generation; saving.value = true;
  try {
    inFlight = screenTouchpadsSet(patch, profile);
    const next = await inFlight;
    if (!disposed) {
      if (!next.ok) { error.value = next.reason === 'settings-write-failed' ? '设置保存失败，未应用。' : '设置未成功应用，请重试。'; await refreshAfterFailure(); }
      else { state.value = { ...next, ...queued }; error.value = ''; }
    }
  } catch (e) { if (!disposed) { error.value = e instanceof Error ? e.message : String(e); await refreshAfterFailure(); } }
  finally { inFlight = undefined; if (!disposed) { saving.value = false; if (Object.keys(queued).length) void flush(); } }
}
async function refreshAfterFailure(): Promise<void> {
  try { const actual = await screenTouchpadsGet(profile); if (!disposed) state.value = { ...actual, ...queued }; } catch { /* retain failure */ }
}
function change(patch: Partial<ScreenTouchpadConfig>, delayed = false): void {
  if (locked.value || disposed) return;
  state.value = { ...state.value, ...patch }; queued = { ...queued, ...patch };
  if (debounce !== undefined) clearTimeout(debounce);
  if (delayed) debounce = setTimeout(() => { debounce = undefined; void flush(); }, 180);
  else { debounce = undefined; void flush(); }
}
function changeSpecial(value: string | number): void {
  if (standaloneSpecial) {
    // Persist only the disabled overlay slot; never mutate outputTarget or a PS/Deck slot.
    if (STANDALONE_SPECIAL_MODES.some(mode => mode.value === value))
      change({ standaloneSpecialMode: value as ScreenTouchpadConfig['standaloneSpecialMode'] });
  } else change({ specialMask: Number(value), specialEnabled: Number(value) !== 0 });
}
function changeLayout(layout: ScreenTouchpadConfig['layout']): void {
  const patch: Partial<ScreenTouchpadConfig> = { layout, enabled: layout !== 'off' };
  // New PS profiles seed native halves in defaults; changing layout must not
  // replace independently saved keyboard/mouse/native mappings.
  if (layout === 'dual' && profile === 'dualsense-edge' && !psAllowed.value) {
    if (state.value.leftMode === 'dualsense') patch.leftMode = 'wasd';
    if (state.value.rightMode === 'dualsense') patch.rightMode = 'mouse';
  }
  if (layout === 'single' && state.value.singleMode === 'dualsense' && !psAllowed.value) patch.singleMode = 'mouse';
  change(patch);
}
onMounted(() => { void refresh(); unsubscribe = on<ScreenTouchpadState>('screenTouchpads.updated', next => {
  if (!disposed && next.persona === profile && !saving.value && !Object.keys(queued).length) { ++generation; state.value = next; loading.value = false; }
}); });
onActivated(() => { void refresh(); });
onDeactivated(() => { void flush(); });
onBeforeUnmount(() => {
  // Drain the final debounced value behind an existing save, including on fast navigation.
  const finalPatch = queued; queued = {};
  if (Object.keys(finalPatch).length) {
    const prior = inFlight ?? Promise.resolve();
    void prior.catch(() => undefined).then(() => screenTouchpadsSet(finalPatch, profile)).catch(() => undefined);
  }
  disposed = true; ++generation; unsubscribe?.();
  if (debounce !== undefined) clearTimeout(debounce);
});
</script>

<template>
  <div class="screen-touchpads-bubble">
    <div class="screen-touchpad-layout"><label>屏幕触摸板</label>
      <Dropdown :model-value="state.layout" :options="SCREEN_TOUCHPAD_LAYOUTS" :disabled="locked"
        aria-label="屏幕触摸板布局" @update:model-value="changeLayout($event as ScreenTouchpadConfig['layout'])" />
    </div>
    <template v-if="state.layout !== 'off'">
      <div v-if="state.layout === 'single'" class="screen-touchpad-single-mode"><label>触摸板映射</label>
        <Dropdown :model-value="state.singleMode" :options="singleModes" :disabled="locked"
          aria-label="单触摸板映射模式" @update:model-value="change({ singleMode: $event as ScreenTouchpadConfig['singleMode'] })" />
      </div>
      <template v-else>
      <div class="screen-touchpad-mode"><label>左侧映射</label>
        <Dropdown :model-value="state.leftMode" :options="leftModes" :disabled="locked"
          aria-label="左触摸板映射模式" @update:model-value="change({ leftMode: $event as ScreenTouchpadConfig['leftMode'] })" />
      </div>
      <div class="screen-touchpad-mode"><label>右侧映射</label>
        <Dropdown :model-value="state.rightMode" :options="rightModes" :disabled="locked"
          aria-label="右触摸板映射模式" @update:model-value="change({ rightMode: $event as ScreenTouchpadConfig['rightMode'] })" />
      </div>
      </template>
    </template>
    <div class="screen-control-selects" data-gp-group="screen-control-selects">
      <div class="screen-control-select"><label>YMCC呼出</label>
        <Dropdown :model-value="state.summonPosition" :options="SCREEN_SUMMON_POSITIONS" :disabled="locked"
          aria-label="YMCC呼出位置" @update:model-value="change({ summonPosition: $event as ScreenTouchpadConfig['summonPosition'], summonEnabled: $event !== 'off' })" />
      </div>
      <div class="screen-control-select"><label>专用按键</label>
        <Dropdown :model-value="specialSelection" :options="specialOptions" :disabled="locked"
          aria-label="专用按键组合" @update:model-value="changeSpecial($event)" />
      </div>
      <div class="screen-control-select"><label>背部按键</label>
        <Dropdown :model-value="state.rearMask" :options="rearOptions" :disabled="locked || !rearSupported"
          aria-label="背部按键组合" placeholder="选择组合" @update:model-value="change({ rearMask: Number($event), rearEnabled: Number($event) !== 0 })" />
      </div>
    </div>
    <template v-if="overlaysEnabled">
      <Slider :model-value="state.scale" label="触摸板缩放" unit="%" :min="50" :max="200" :step="5"
        :accelerate="false" :disabled="locked" @update:model-value="change({ scale: $event }, true)" />
      <Slider :model-value="state.transparency" label="触摸板显示透明度" unit="%" :min="0" :max="100" :step="5"
        :disabled="locked" @update:model-value="change({ transparency: $event }, true)" />
      <Slider v-if="state.layout !== 'off' && mouseEnabled" :model-value="state.mouseSensitivity" label="触摸板鼠标灵敏度" unit="%"
        :min="10" :max="300" :step="5" :disabled="locked" @update:model-value="change({ mouseSensitivity: $event }, true)" />
    </template>
    <p v-if="status" class="muted screen-touchpad-status" role="status">{{ status }}</p>
  </div>
</template>

<style scoped>
.screen-touchpads-bubble{margin-top:10px;padding:10px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent)}
.screen-touchpad-layout,.screen-touchpad-single-mode,.screen-touchpad-mode{display:grid;grid-template-columns:76px 1fr;align-items:center;gap:10px;margin:10px 0}
.screen-touchpad-layout label,.screen-touchpad-single-mode label,.screen-touchpad-mode label{font-size:12px;color:var(--text-secondary)}
.screen-control-selects{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:7px;margin:12px 0}
.screen-control-select{min-width:0;display:grid;gap:5px}
.screen-control-select label{font-size:11px;color:var(--text-secondary);white-space:nowrap}
.screen-control-select :deep(button){min-width:0;min-height:38px;font-size:11px;padding:0 7px}
.screen-touchpad-status{font-size:11px;line-height:1.6;margin:8px 0 0}
</style>
