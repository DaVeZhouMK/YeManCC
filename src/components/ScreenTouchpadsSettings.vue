<script setup lang="ts">
// [AI:USER:TOUCHPAD] 布局/映射/专用键/背键→改变相应屏幕控件；自动隐藏后触摸原区域可恢复，隐藏不代表释放手柄功能。
// 不同虚拟输出可选项按实际支持呈现；触摸反馈不等于游戏震动，不能据同一开关推断全部反馈能力。
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref, watch } from 'vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import Slider from '@/components/Slider.vue';
import { on } from '@/bridge/ipc';
import { LEFT_TOUCHPAD_MODES, RIGHT_TOUCHPAD_MODES, SINGLE_TOUCHPAD_MODES, SCREEN_TOUCHPAD_LAYOUTS,
  SCREEN_SUMMON_POSITIONS, STANDALONE_SPECIAL_MODES, TOUCH_FEEDBACK_MODES, SCREEN_AUTO_HIDE_OPTIONS, screenTouchpadsGet, screenTouchpadsSet, screenTouchpadProfile, screenTouchpadDefaults,
  type ScreenTouchpadConfig, type ScreenTouchpadState } from '@/bridge/screenTouchpads';

const props = withDefaults(defineProps<{ disabled?: boolean; persona?: string; steamDeckEnabled?: boolean; ps5Enabled?: boolean; presetConfig?: ScreenTouchpadConfig }>(),
  { disabled: false, persona: 'steamdeck', steamDeckEnabled: false, ps5Enabled: false });
const emit = defineEmits<{ (event:'preset-update', config:ScreenTouchpadConfig):void }>();
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
const baseLocked = computed(() => props.disabled || loading.value);
const masterEnabled = computed(() => state.value.masterEnabled !== false);
const locked = computed(() => baseLocked.value || !masterEnabled.value);
const psAllowed = computed(() => profile === 'dualsense-edge' && props.ps5Enabled &&
  (props.presetConfig !== undefined || state.value.ps5Available === true));
const deckAllowed = computed(() => profile === 'steamdeck' && props.steamDeckEnabled && (props.presetConfig !== undefined || state.value.steamDeckAvailable === true));
// Keep the preset label visible while the virtual target is off, but not selectable.
const leftModes = computed(() => LEFT_TOUCHPAD_MODES.filter(mode =>
  (mode.value !== 'steamdeck' || profile === 'steamdeck') && (mode.value !== 'dualsense' || profile === 'dualsense-edge'))
  .map(mode => ({ ...mode, label: mode.label.replace(' (Steam内设置)', ''), sub: mode.label.includes('(Steam内设置)') ? 'Steam内设置' : undefined, disabled: (mode.value === 'steamdeck' && !deckAllowed.value) || (mode.value === 'dualsense' && !psAllowed.value) })));
const rightModes = computed(() => RIGHT_TOUCHPAD_MODES.filter(mode =>
  (mode.value !== 'steamdeck' || profile === 'steamdeck') && (mode.value !== 'dualsense' || profile === 'dualsense-edge'))
  .map(mode => ({ ...mode, label: mode.label.replace(' (Steam内设置)', ''), sub: mode.label.includes('(Steam内设置)') ? 'Steam内设置' : undefined, disabled: (mode.value === 'steamdeck' && !deckAllowed.value) || (mode.value === 'dualsense' && !psAllowed.value) })));
const singleModes = computed(() => SINGLE_TOUCHPAD_MODES.filter(mode => mode.value !== 'dualsense' || profile === 'dualsense-edge')
  .map(mode => ({ ...mode, label: mode.label.replace(' (Steam内设置)', ''), sub: mode.label.includes('(Steam内设置)') ? 'Steam内设置' : undefined, disabled: mode.value === 'dualsense' && !psAllowed.value })));
const rearSupported = profile === 'steamdeck' || props.persona === 'dualsense-edge';
const standaloneSpecial = profile === 'disabled';
const specialOptions = computed(() => standaloneSpecial ? STANDALONE_SPECIAL_MODES : profile === 'steamdeck'
  ? [{ value: 0, label: '关闭' }, { value: 1, label: '只开启 Steam' }, { value: 2, label: '只开启三点' }, { value: 3, label: '都开启' }]
  : profile === 'dualsense-edge'
    ? [{ value: 0, label: '关闭' }, { value: 1, label: '只开启 PS' }, { value: 2, label: '只开启静音' }, { value: 3, label: '都开启' }]
    : profile === 'elite' ? [{ value: 0, label: '关闭' }, { value: 1, label: '都开启', sub: 'Xbox 按键' }] : [{ value: 0, label: '关闭' }]);
// Old Xbox toggle configurations used mask 3, but Xbox only exposes Guide.
const specialSelection = computed(() => standaloneSpecial ? state.value.standaloneSpecialMode ?? 'off'
  : profile === 'elite' ? state.value.specialMask & 1 : state.value.specialMask);
const rearOptions = computed(() => profile === 'steamdeck'
  ? [{ value: 10, label: 'L5+R5' }, { value: 5, label: 'L4+R4' }, { value: 15, label: '全开启' }, { value: 0, label: '全关闭' }]
  : profile === 'dualsense-edge'
    ? [{ value: 5, label: 'LFN+RFN' }, { value: 10, label: 'LB+RB' }, { value: 15, label: '全开启' }, { value: 0, label: '全关闭' }]
    : [{ value: 0, label: '全关闭' }]);
const overlaysEnabled = computed(() => (standaloneSpecial && state.value.standaloneSpecialMode !== undefined && state.value.standaloneSpecialMode !== 'off') || state.value.layout !== 'off' || state.value.summonPosition !== 'off' || state.value.specialMask !== 0 || state.value.rearMask !== 0 || state.value.summonEnabled || state.value.specialEnabled || state.value.rearEnabled);
const mouseEnabled = computed(() => state.value.layout === 'single' ? state.value.singleMode === 'mouse' : state.value.leftMode === 'mouse' || state.value.rightMode === 'mouse');
// Standalone actions surface real backend failures instead of a successful UI-only click.
const specialErrors: Record<string,string> = {
  'action-queue-unavailable': '专用按键执行队列不可用，请重试。',
  'input-power-gate': '专用按键暂不可用，请等待唤醒恢复完成。',
  'steam-menu-unavailable': 'Steam 菜单不可用：请检查大屏原生按键入口和本地调试通道；不要求 Steam 位于前台。',
  'steam-menu-result-uncertain': 'Steam 菜单未确认响应，请先检查当前菜单状态，避免重复触发。',
  'microphone-mute-failed': '默认麦克风静音失败，请检查录音设备与权限。',
  'keyboard-injection-failed': 'Steam 快捷键发送失败，请检查权限。',
  'physical-modifiers-held': '请先松开 Ctrl、Shift、Alt 或 Windows 键后重试。',
};
function nativeFailure(state: ScreenTouchpadState): string {
  if (!state.error) return '';
  const code = state.error >>> 0;
  const label = code >= 0x80000000 ? '0x' + code.toString(16).toUpperCase().padStart(8, '0') : String(code);
  const prefix = state.settingsSaved ? '设置已保存。' : '';
  if (code === 0x80004004) return prefix + 'Windows 上次触摸板操作已中止（' + label + '）。如触摸正常可继续使用，无响应时再重新开启。';
  return prefix + '触摸板操作返回错误（Windows ' + label + '），无响应时可重新开启后重试。';
}
const status = computed(() => error.value || (standaloneSpecial && specialErrors[state.value.standaloneSpecialStatus ?? '']) || nativeFailure(state.value));
async function refresh(): Promise<void> {
  if (disposed || saving.value || Object.keys(queued).length) return;
  if(props.presetConfig){state.value={...props.presetConfig,persona:profile,ok:true,available:true,visible:false,reason:'boot-mode-preset'};loading.value=false;return;}
  const ticket = ++generation;
  try { const next = await screenTouchpadsGet(profile); if (!disposed && ticket === generation) { state.value = next; error.value = ''; } }
  catch (e) { if (!disposed && ticket === generation) error.value = e instanceof Error ? e.message : String(e); }
  finally { if (!disposed && ticket === generation) loading.value = false; }
}
async function flush(): Promise<void> {
  if (debounce !== undefined) { clearTimeout(debounce); debounce = undefined; }
  if (disposed || baseLocked.value || saving.value || !Object.keys(queued).length) return;
  const patch = queued; queued = {}; ++generation; saving.value = true;
  try {
    inFlight = screenTouchpadsSet(patch, profile);
    const next = await inFlight;
    if (!disposed) {
      if (!next.ok && !next.settingsSaved) {
        error.value = next.reason === 'settings-write-failed' ? '设置保存失败，未应用。' : '设置未成功应用，请重试。';
        await refreshAfterFailure();
      } else {
        state.value = { ...next, ...queued };
        // A committed config with a native error is rendered from actual state;
        // do not label it unsaved or freeze a stale runtime message in error.value.
        error.value = !next.ok && !next.error ? '设置已保存，但触摸板暂未就绪，请重新开启后重试。' : '';
      }
    }
  } catch (e) { if (!disposed) { error.value = e instanceof Error ? e.message : String(e); await refreshAfterFailure(); } }
  finally { inFlight = undefined; if (!disposed) { saving.value = false; if (Object.keys(queued).length) void flush(); } }
}
async function refreshAfterFailure(): Promise<void> {
  try { const actual = await screenTouchpadsGet(profile); if (!disposed) state.value = { ...actual, ...queued }; } catch { /* retain failure */ }
}
function change(patch: Partial<ScreenTouchpadConfig>, delayed = false): void {
  if (baseLocked.value || disposed || (!masterEnabled.value && patch.masterEnabled === undefined)) return;
  state.value = { ...state.value, ...patch };
  if(props.presetConfig){const config={...props.presetConfig,...patch};if(patch.layout!==undefined)config.enabled=patch.layout!=='off';emit('preset-update',config);return;}
  queued = { ...queued, ...patch };
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
watch(()=>props.presetConfig,()=>{if(props.presetConfig)void refresh();});
onMounted(() => { void refresh(); if(props.presetConfig)return;unsubscribe = on<ScreenTouchpadState>('screenTouchpads.updated', next => {
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
  <div class="touchpad-page-settings">
    <div class="touchpad-master"><div class="touchpad-master-copy"><strong>触控板总开关</strong><span>{{masterEnabled?'当前手柄的设置已启用':'已关闭，保留当前手柄的设置'}}</span></div><Toggle :model-value="masterEnabled" aria-label="触控板总开关" :disabled="baseLocked || saving" @update:model-value="change({masterEnabled:$event})"/></div>
    <div class="screen-touchpads-bubble" :class="{'touchpad-disabled':!masterEnabled}">
    <div class="screen-touchpad-layout screen-touchpad-field"><label>屏幕触摸板</label>
      <Dropdown :model-value="state.layout" :options="SCREEN_TOUCHPAD_LAYOUTS" :disabled="locked"
        aria-label="屏幕触摸板布局" @update:model-value="changeLayout($event as ScreenTouchpadConfig['layout'])" />
    </div>
    <div class="screen-touchpad-auto-hide screen-touchpad-field" title="触摸板和专用按键闲置后完全不可见，触摸原区域即可恢复；保持按住时不会隐藏。">
      <label>自动隐藏</label>
      <Dropdown :model-value="state.autoHideSeconds ?? 60" :options="SCREEN_AUTO_HIDE_OPTIONS" :disabled="locked"
        aria-label="触摸板和专用按键自动隐藏" @update:model-value="change({ autoHideSeconds: $event as ScreenTouchpadConfig['autoHideSeconds'] })" />
    </div>
    <template v-if="state.layout !== 'off'">
      <div v-if="state.layout === 'single'" class="screen-touchpad-single-mode screen-touchpad-field"><label>触摸板映射</label>
        <Dropdown :model-value="state.singleMode" :options="singleModes" :disabled="locked"
          aria-label="单触摸板映射模式" @update:model-value="change({ singleMode: $event as ScreenTouchpadConfig['singleMode'] })" />
      </div>
      <template v-else>
        <div class="screen-touchpad-mode screen-touchpad-field"><label>左侧映射</label>
          <Dropdown :model-value="state.leftMode" :options="leftModes" :disabled="locked"
            aria-label="左触摸板映射模式" @update:model-value="change({ leftMode: $event as ScreenTouchpadConfig['leftMode'] })" />
        </div>
        <div class="screen-touchpad-mode screen-touchpad-field"><label>右侧映射</label>
          <Dropdown :model-value="state.rightMode" :options="rightModes" :disabled="locked"
            aria-label="右触摸板映射模式" @update:model-value="change({ rightMode: $event as ScreenTouchpadConfig['rightMode'] })" />
        </div>
      </template>
    </template>
    <div class="screen-control-selects" data-gp-group="screen-control-selects">
      <div class="screen-control-select screen-touchpad-field"><label>YMCC呼出</label>
        <Dropdown :model-value="state.summonPosition" :options="SCREEN_SUMMON_POSITIONS" :disabled="locked"
          aria-label="YMCC呼出位置" @update:model-value="change({ summonPosition: $event as ScreenTouchpadConfig['summonPosition'], summonEnabled: $event !== 'off' })" />
      </div>
      <div class="screen-control-select screen-touchpad-field"><label>{{presetConfig?'屏幕专用按键 · 触摸':'专用按键'}}</label>
        <Dropdown :model-value="specialSelection" :options="specialOptions" :disabled="locked"
          aria-label="专用按键组合" @update:model-value="changeSpecial($event)" />
      </div>
      <div class="screen-control-select screen-touchpad-field"><label>{{presetConfig?'屏幕背部按键 · 触摸':'背部按键'}}</label>
        <Dropdown :model-value="state.rearMask" :options="rearOptions" :disabled="locked || !rearSupported"
          aria-label="背部按键组合" placeholder="选择组合" @update:model-value="change({ rearMask: Number($event), rearEnabled: Number($event) !== 0 })" />
      </div>
      <div v-if="overlaysEnabled" class="screen-touchpad-feedback screen-touchpad-field" title="触摸板和屏幕按键共用掌机内置手柄马达：按下轻震，触摸板滑动一段距离后再轻震。无需开启虚拟手柄；实际手感依机型而定。"><label>触摸反馈</label>
        <Dropdown :model-value="state.touchFeedback ?? 'medium'" :options="TOUCH_FEEDBACK_MODES" :disabled="locked"
          aria-label="触摸板触摸反馈" @update:model-value="change({ touchFeedback: $event as ScreenTouchpadConfig['touchFeedback'] })" />
      </div>
    </div>
    <div v-if="overlaysEnabled" class="screen-touchpad-sliders">
      <Slider :model-value="state.scale" label="触摸板缩放" unit="%" :min="50" :max="200" :step="5"
        :accelerate="false" :disabled="locked" @update:model-value="change({ scale: $event }, true)" />
      <Slider :model-value="state.transparency" label="触摸板显示透明度" unit="%" :min="0" :max="100" :step="5" title="触摸板和屏幕按键按住时可见度提升 50%（最高 100%）并显示外发光，松开恢复。"
        :disabled="locked" @update:model-value="change({ transparency: $event }, true)" />
      <Slider v-if="state.layout !== 'off' && mouseEnabled" class="screen-touchpad-mouse-sensitivity" :model-value="state.mouseSensitivity" label="触摸板鼠标灵敏度" unit="%"
        :min="10" :max="300" :step="5" :disabled="locked" @update:model-value="change({ mouseSensitivity: $event }, true)" />
    </div>
    <p v-if="status" class="muted screen-touchpad-status" role="status">{{ status }}</p>
    </div>
    <p class="touchpad-memory-note"><span aria-hidden="true">●</span>按虚拟手柄分别记忆 · 切换请返回控制器页</p>
  </div>
</template>

<style scoped>
.screen-touchpads-bubble{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px 10px;margin-top:10px;padding:10px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent)}
.screen-touchpad-field{min-width:0;display:grid;grid-template-columns:minmax(0,1fr);align-content:start;gap:4px}
.screen-touchpad-field label{font-size:11px;line-height:14px;color:var(--text-dim);white-space:nowrap}
.screen-touchpad-field :deep(.dd){min-width:0;max-width:100%}
.screen-touchpad-field :deep(.dd-trigger){min-width:0;min-height:34px;padding:0 8px;gap:6px;font-size:12px}
.screen-touchpad-field :deep(.dd-selected-label){min-width:0;flex:1 1 auto;overflow:hidden;text-overflow:ellipsis}
.screen-touchpad-single-mode,.screen-control-selects,.screen-touchpad-sliders,.screen-touchpad-status{grid-column:1 / -1}
.screen-control-selects,.screen-touchpad-sliders{min-width:0;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px 10px}
.screen-touchpad-sliders{padding-top:2px;border-top:1px solid rgba(255,255,255,.06)}
.screen-touchpad-sliders :deep(.slider){min-width:0;padding:4px 0 0}
.screen-touchpad-sliders :deep(.slider-head){gap:6px;margin-bottom:4px}
.screen-touchpad-sliders :deep(.slider-label){font-size:11px;white-space:nowrap}
.screen-touchpad-sliders :deep(.slider-val){flex-shrink:0;font-size:12px}
.screen-touchpad-mouse-sensitivity{grid-column:1 / -1}
.screen-touchpad-status{font-size:11px;line-height:1.6;margin:0;overflow-wrap:anywhere}
</style>
