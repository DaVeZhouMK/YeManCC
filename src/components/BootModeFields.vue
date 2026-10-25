<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue';
import Dropdown from './Dropdown.vue';
import Slider from './Slider.vue';
import Toggle from './Toggle.vue';
import ScreenTouchpadsSettings from './ScreenTouchpadsSettings.vue';
import type { ScreenTouchpadConfig } from '@/bridge/screenTouchpads';
import { screenTouchpadDefaults } from '@/bridge/screenTouchpads';
import { cloneBootValue, type BootConfig, type BootModeModule, type BootDeviceCapabilities } from '@/bridge/bootModeModel';
import { STEAM_APPEARANCE_THEMES } from '@/bridge/steamAppearance';
import { switchMonitorHudTemplate } from '@/bridge/monitorHudModel';
import { screenTouchpadProfile } from '@/bridge/screenTouchpads';
import { gyroMotionParamsForPreset, isGyroPresetKey } from '@/bridge/gyroPresetModel';
import { CONTROLLER_SHORTCUT_ACTIONS } from '@/bridge/controllerShortcutRules';
const props = defineProps<{
    module: BootModeModule;
    value: BootConfig;
    disabled: boolean;
    persona: string;
    support: BootDeviceCapabilities;
    apps: Array<{
        name: string;
        path: string;
    }>;
}>();
const emit = defineEmits<{
    (event: 'change', value: BootConfig): void;
    (event: 'edit-controller'): void;
    (event: 'shared-apps'): void;
}>();
const data = computed<any>(() => props.value), side = computed(() => powerSide.value);
const powerSide = ref<'ac' | 'dc'>('ac');
const options = (values: readonly (string | number)[]) => values.map(value => ({ value, label: String(value) }));
const cores = [{ value: 'bigFirst', label: '大核为主' }, { value: 'bigOnly', label: '仅大核' }, { value: 'littleFirst', label: '小核为主' }, { value: 'littleOnly', label: '仅小核' }];
const personas = [{ value: 'disabled', label: '关闭虚拟手柄' }, { value: 'elite', label: 'Xbox' }, { value: 'steamdeck', label: 'SteamDeck' }, { value: 'dualsense-edge', label: 'PS5' }];
type Field = {
    path: string;
    label: string;
    type: 'toggle' | 'range' | 'select';
    min?: number;
    max?: number;
    step?: number;
    unit?: string;
    disabled?: boolean;
    options?: Array<{
        value: string | number;
        label: string;
    }>;
};
const t = (label: string, path: string): Field => ({ label, path, type: 'toggle' }), r = (label: string, path: string, min: number, max: number, step = 1, unit = ''): Field => ({ label, path, type: 'range', min, max, step, unit }), s = (label: string, path: string, items: Field['options']): Field => ({ label, path, type: 'select', options: items });
function gyroUiMode(motion: any): string { return motion?.motionMode === 'on' && motion?.motionTrigger ? 'suppress' : String(motion?.motionMode || 'on'); }
function gyroParams(motion: any): Record<string, unknown> {
    const params = cloneBootValue(motion);
    for (const key of ['enabled', 'virtualPadLink', 'preset', 'presets']) delete params[key];
    params.motionMode = gyroUiMode(motion);
    params.motionTrigger = motion.motionTrigger || '';
    return params;
}
function get(path: string): any {
    if (props.module === 'gyro' && path === 'motion.motionMode') return gyroUiMode(data.value.motion);
    return path.split('.').reduce((value, key) => value?.[key], data.value);
}
function set(path: string, value: unknown) {
    if (props.disabled)
        return;
    const next = JSON.parse(JSON.stringify(data.value));
    let target = next;
    const parts = path.split('.');
    for (const key of parts.slice(0, -1))
        target = target[key] ??= {};
    target[parts[parts.length - 1]] = value;
    if (props.module === 'gyro') {
        const before = data.value.motion || {}, motion = next.motion;
        if (path === 'motion.preset' && isGyroPresetKey(value)) {
            const presets = { ...before.presets };
            if (isGyroPresetKey(before.preset)) presets[before.preset] = gyroParams(before);
            next.motion = { ...motion, ...gyroMotionParamsForPreset(value, presets), preset: value, presets };
        } else {
            if (path === 'motion.motionMode') {
                motion.motionMode = value === 'suppress' ? 'on' : value;
                if (value === 'on') motion.motionTrigger = null;
                else if (!motion.motionTrigger) motion.motionTrigger = 'LT';
            }
            if (path !== 'motion.enabled' && path !== 'motion.virtualPadLink' && isGyroPresetKey(motion.preset))
                motion.presets = { ...motion.presets, [motion.preset]: gyroParams(motion) };
        }
    }
    if (props.module === 'gyro' && ((path === 'motion.enabled' && value === true) || (path === 'motion.preset' && isGyroPresetKey(value)))) {
        next.motion.enabled = true;
        next.motion.outputMode = 'virtual-stick';
        next.motion.unit = 'deg/s';
    }
    if (props.module === 'input') {
        if (path === 'mouse' && value === 'steamdeck') next.persona = 'steamdeck';
        if (path === 'persona' && value === 'steamdeck' && next.mouse && next.mouse !== 'off') next.mouse = 'steamdeck';
        if (path === 'persona' && value !== 'steamdeck' && next.mouse === 'steamdeck') next.mouse = 'off';
    }
    if (props.module === 'monitor' && path === 'preferences.ymccTemplate')
        next.preferences = switchMonitorHudTemplate(next.preferences, value as 'A' | 'B');
    if (props.module === 'performance' && path.endsWith('.tdpCeiling')) {
        const bank = next.controls[side.value];
        bank.tdp = Math.min(bank.tdp, value as number);
        bank.auto.coreSplit = Math.max(3, Math.min(bank.auto.coreSplit, Number(value) - 1));
    }
    emit('change', next);
}
const fields = computed<Field[]>(() => {
    if (props.module === 'performance') {
        const prefix = 'controls.' + side.value + '.', bank = data.value.controls?.[side.value];
        if (!bank)
            return [];
        return [s('调度模式', prefix + 'mode', [{ value: 'auto', label: '自动模式' }, { value: 'manual', label: '手动模式' }]), r('TDP 上限', prefix + 'tdpCeiling', 3, 150, 1, ' W'), r('TDP 功耗', prefix + 'tdp', 3, Math.max(3, bank.tdpCeiling), 1, ' W'), ...(bank.mode === 'auto' ? [r('GPU 倾向（0% 为 CPU 优先）', prefix + 'auto.bias', 0, 100, 5, '%'), s('浮动节能', prefix + 'auto.saving', [{ value: 'none', label: '无浮动' }, { value: 'small', label: '小幅' }, { value: 'medium', label: '中幅' }, { value: 'large', label: '大幅' }, { value: 'aggressive', label: '激进' }]), s('3–' + bank.auto.coreSplit + ' W', prefix + 'auto.coreLow', cores), s(bank.tdp <= bank.auto.coreSplit ? '高功耗段（当前无区间）' : (bank.auto.coreSplit + 1) + '–' + bank.tdp + ' W', prefix + 'auto.coreHigh', cores), r('核心切换瓦数', prefix + 'auto.coreSplit', 3, Math.min(25, Math.max(3, bank.tdp)), 1, ' W')] : [r('CPU 频率', prefix + 'manual.frequencyMHz', 800, 6500, 100, ' MHz'), r('积极性', prefix + 'manual.aggressiveness', 0, 100, 1, '%'), t('睿频', prefix + 'manual.turbo'), t('不限频率', prefix + 'manual.unlimited'), s('大小核心', prefix + 'manual.coreMode', cores)]), r('目标帧率', 'frames.' + side.value + '.fps', 0, 300, 1, ' FPS')];
    }
    if (props.module === 'fan')
        return [t('启用风扇控制', 'enabled'), s('风扇预设', 'preset', [{ value: 'soft', label: '静音' }, { value: 'balanced', label: '均衡' }, { value: 'aggressive', label: '散热优先' }])];
    if (props.module === 'charge')
        return [t('随模式设置充电上限', 'enabled'), s('充电上限', 'limit', options([...new Set([...Array.from({ length: 11 }, (_, i) => 50 + 5 * i), ...(props.support.charge?.choices || [])])]).map(item => ({ ...item, label: item.label + '%' })))];
    if (props.module === 'input')
        return [s('虚拟手柄', 'persona', personas), s('模拟鼠标', 'mouse', [{ value: 'off', label: '关闭' }, ...(data.value.persona === 'steamdeck' ? [] : [{ value: 'microsoft', label: '微软鼠标' }, { value: 'joyxoff', label: 'JoyXoff' }]), { value: 'steamdeck', label: 'SteamDeck 鼠标' }]), t('外置优先', 'ownership.externalControllerPriority')];
    if (props.module === 'gyro') {
        const steering = data.value.motion?.motionInput === 'joystick-steering';
        const triggers = [{ value: '', label: '未设置' }, ...options(['LT', 'RT', 'LB', 'RB'])];
        return [t('开启陀螺仪', 'motion.enabled'), t('虚拟手柄联动', 'motion.virtualPadLink'),
            s('使用预设', 'motion.preset', [{ value: 'fps', label: 'FPS 射击' }, { value: 'racing', label: '赛车' }, { value: 'custom', label: '自定义' }, { value: 'steam', label: 'Steam' }]),
            s('输入方式', 'motion.motionInput', [{ value: 'local-space', label: '本地空间' }, { value: 'joystick-steering', label: '方向盘倾斜' }, { value: 'player-space', label: '玩家空间' }, { value: 'world-space', label: '世界空间' }]),
            s('触发方式', 'motion.motionMode', [{ value: 'on', label: '常开' }, { value: 'off', label: '按住触发' }, { value: 'suppress', label: '按住抑制' }, { value: 'toggle', label: '切换' }]),
            { ...s('触发键', 'motion.motionTrigger', triggers.filter(item => item.value !== '')), disabled: gyroUiMode(data.value.motion) === 'on' },
            s('输出到', 'motion.outputStick', [{ value: 'left', label: '左摇杆' }, { value: 'right', label: '右摇杆' }]),
            s('输出轴', 'motion.outputAxis', [{ value: 'xy', label: '水平 + 垂直' }, { value: 'x', label: '仅水平' }]),
            ...(steering ? [s('转向轴', 'motion.steeringAxis', [{ value: 'roll', label: '默认 (Z,X)' }, { value: 'yaw', label: '横轴 (-Y,X)' }, { value: 'auto', label: '竖轴（按传感器）' }]),
                r('最大倾角', 'motion.steeringMaxAngle', 10, 80, 1, '°'), r('转向曲线', 'motion.steeringPower', .2, 5, .1), r('倾斜死区', 'motion.steeringDeadzone', 0, 5, .1, '°')]
                : [r('水平灵敏度', 'motion.motionSensitivityX', .1, 3, .1, '×'), r('垂直灵敏度', 'motion.motionSensitivityY', .1, 3, .1, '×'),
                    s('变速触发键', 'motion.aimingSightsTrigger', triggers), r('变速倍率', 'motion.aimingSightsMultiplier', 0, 2, .1, '×'),
                    s('速度模式', 'motion.velocityMode', [{ value: 'default', label: '线性' }, { value: 'velocity', label: '加速度' }]),
                    r('加速度倍率', 'motion.velocityScale', .1, 5, .1, '×'), r('陀螺仪权重', 'motion.gyroWeight', 1, 3, .1, '×')])];
    }
    if (props.module === 'audioOptimization')
        return [t('掌机音质优化 · FxSound', 'enabled')];
    if (props.module === 'frameGeneration')
        return [t('小黄鸭 · 缩放插帧', 'enabled')];
    if (props.module === 'softwareLaunch')
        return [s('启动时机', 'timing', [{ value: 'scene', label: '开机应用此模式时' }, { value: 'steam', label: 'Steam 连接后' }, { value: 'manual', label: '仅手动' }]), r('启动延迟', 'delay', 0, 30, 1, ' 秒')];
    if (props.module === 'steamAppearance')
        return STEAM_APPEARANCE_THEMES.map(item => t(item.label, 'states.' + item.name));
    if (props.module === 'steamWallpaper')
        return [t('播放动态壁纸', 'enabled'), t('后台下载', 'download'), r('下载 CPU 暂停阈值', 'cpuThreshold', 5, 90, 5, '%')];
    if (props.module === 'steamDecky')
        return [t('启用 Decky', 'enabled')];
    if (props.module === 'monitor')
        return [t('显示监控', 'preferences.enabled'), s('监控模板', 'preferences.ymccTemplate', [{ value: 'A', label: 'A 模板' }, { value: 'B', label: 'B 模板' }]), s('监控位置', 'preferences.ymcc.position', [{ value: 1, label: '左上角' }, { value: 2, label: '右上角' }, { value: 4, label: '左下角' }, { value: 3, label: '右下角' }]), r('面板大小', 'preferences.ymcc.scale', .5, 1.5, .05, '×')];
    if (props.module === 'display')
        return [r(data.value.brightness == null ? '目标亮度（当前未读取）' : '屏幕亮度', 'brightness', 10, 100, 5, '%'),
            s('分辨率', 'resolution', [{ value: 'native', label: '沿用全局基准分辨率' }, ...options([...new Set([data.value.resolution, '1280x720', '1280x800', '1920x1080'].filter(value => value && value !== 'native'))])]),
            s('刷新率', 'hz', [{ value: 'native', label: '沿用全局基准刷新率' }, ...options([...new Set([data.value.hz, '60', '90', '120', '144'].filter(value => value && value !== 'native'))])])];
    if (props.module === 'background')
        return [s('界面配色', 'ui.theme', [{ value: 'blue-black', label: '蓝黑' }, { value: 'red-black', label: '红黑' }, { value: 'cyberpunk', label: '黄黑' }]), r('面板透明度', 'ui.backgroundOpacity', .2, .8, .01, '×'), r('背景模糊', 'ui.backgroundBlur', 0, 24, 1), t('YMCC 动态壁纸', 'ui.dynamicBackgroundEnabled')];
    if (props.module === 'sleep') {
        const buttons = [{ value: 0, label: 'S3 睡眠' }, { value: 1, label: 'S4 休眠' }, { value: 2, label: '不操作' }, { value: 3, label: '关闭显示器' }], timeouts = [0, 1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60, 120, 180, 240, 300, 480, 720, 1440, 2880, 5760].map(value => ({ value, label: value ? value + ' 分钟' : '从不' }));
        return [s('睡眠优化', 'config.mode', [{value:'off',label:'关闭守护'},{value:'custom',label:'自选优化'}]), t('睡眠时暂停游戏', 'config.pauseGameOnSleep'), t('睡眠唤醒 Steam 自动修复', 'steamAutoRecovery'), t('唤醒需要密码', 'config.wakePasswordEnabled'), t('睡眠电源计划优化', 'config.sleepPowerPlanOptimizationEnabled'), t('睡眠失效强制睡眠', 'config.retryOnEntryFailure'), t('意外唤醒重睡', 'config.retryOnNonUserWake'), s('插电 AC · 电源键', 'acButton', buttons), s('电池 DC · 电源键', 'dcButton', buttons), t('系统休眠开关', 'hibernate'), r('休眠文件大小', 'hibernateSize', 30, 100, 5, '%'), ...['Screen', 'Sleep', 'Hibernate'].flatMap((key, i) => ['ac', 'dc'].map(side => s(['关闭屏幕', '进入睡眠', '进入休眠'][i] + ' · ' + side.toUpperCase(), 'timeouts.' + side + key, timeouts)))];
    }
    return [];
});
const touchPersona = computed(() => screenTouchpadProfile(props.persona));
const touchConfig = computed(() => cloneBootValue((data.value.profiles || {})[touchPersona.value] || screenTouchpadDefaults(touchPersona.value)) as ScreenTouchpadConfig);
function updateTouch(config: ScreenTouchpadConfig) {
    if (props.disabled) return;
    emit('change', { ...data.value, profiles: { ...data.value.profiles, [touchPersona.value]: cloneBootValue(config) } });
}
function toggleApp(path: string) { const paths = Array.isArray(data.value.paths) ? data.value.paths : []; set('paths', paths.includes(path) ? paths.filter((p: string) => p !== path) : [...paths, path]); }
const ruleLabel = (action: string) => CONTROLLER_SHORTCUT_ACTIONS.find(item => item.id === action)?.label || action;
const fieldsRoot = ref<HTMLElement | null>(null);
const sliderDrafts = new Map<HTMLInputElement, BootConfig>();
let activeSlider: HTMLInputElement | null = null;
function beginSliderDraft(event: Event) {
    const input = event.target;
    if (props.disabled || !(input instanceof HTMLInputElement) || input.type !== 'range') return;
    if (!sliderDrafts.has(input)) sliderDrafts.set(input, cloneBootValue(props.value));
    activeSlider = input;
    input.dataset.bootSliderEditing = 'true';
}
function finishSliderDraft(event: Event, cancel = false) {
    const input = event.target instanceof HTMLInputElement ? event.target : activeSlider;
    if (!input) return;
    const before = sliderDrafts.get(input);
    sliderDrafts.delete(input);
    delete input.dataset.bootSliderEditing;
    if (input === activeSlider) activeSlider = null;
    // Restore all changes made by this field, including TDP's dependent clamps.
    if (cancel && before && !props.disabled) emit('change', cloneBootValue(before));
}
onBeforeUnmount(() => {
    if (activeSlider) {
        delete activeSlider.dataset.bootSliderEditing;
        window.dispatchEvent(new CustomEvent('gp:slider-edit-invalidated', { detail: { panel: fieldsRoot.value, input: activeSlider } }));
    }
    sliderDrafts.clear(); activeSlider = null;
});

</script>
<template>
 <div ref="fieldsRoot" class="boot-mode-fields" data-gp-slider-draft @gp:slider-edit-start="beginSliderDraft" @gp:slider-edit-end="finishSliderDraft($event)" @gp:slider-edit-cancel="finishSliderDraft($event,true)">
 <div v-if="module==='performance'" class="boot-power-side"><button v-for="item in ['ac','dc'] as const" :key="item" :aria-pressed="powerSide===item" :disabled="disabled" @click="powerSide=item">{{item==='ac'?'交流电 AC':'电池 DC'}}</button></div>
 <ScreenTouchpadsSettings v-if="module==='touch'" :key="touchPersona" :persona="touchPersona" :preset-config="touchConfig" :disabled="disabled" :steam-deck-enabled="persona==='steamdeck'" :ps5-enabled="persona==='dualsense-edge'" @preset-update="updateTouch"/>
 <template v-else-if="module==='controller'"><div class="boot-controller-summary"><div><strong>{{(data.rules||[]).filter((rule:any)=>rule.enabled).length}} / {{(data.rules||[]).length}} 项已启用</strong><span>实体手柄按键、组合键与动作</span></div><button :disabled="disabled" class="boot-action primary" @click="emit('edit-controller')">编辑手柄自定义</button></div><div class="boot-rule-list"><div v-for="rule in data.rules" :key="rule.id"><i :class="{on:rule.enabled}"/><span>{{ruleLabel(rule.actionId)}}</span><kbd>{{(rule.inputs||[]).map((input:any)=>input.code).join(' + ')}}</kbd></div></div></template>
 <div v-else class="boot-fields" :class="{'monitor-fields':module==='monitor'}">
  <div v-for="field in fields" :key="field.path" class="boot-field" :data-field="module+'.'+field.path">
   <Toggle v-if="field.type==='toggle'" :model-value="get(field.path)===true" :label="field.label" :disabled="disabled||field.disabled" @update:model-value="set(field.path,$event)"/>
   <Slider v-else-if="field.type==='range'" :model-value="typeof get(field.path)==='number'?get(field.path):field.min" :label="field.label" :min="field.min" :max="field.max" :step="field.step" :unit="field.unit" :disabled="disabled||field.disabled" @update:model-value="set(field.path,$event)"/>
   <label v-else><span>{{field.label}}</span><Dropdown :model-value="get(field.path)??''" :options="field.options||[]" :aria-label="field.label" :disabled="disabled||field.disabled" @update:model-value="set(field.path,$event)"/></label>
  </div>
 </div>
 <p v-if="module==='display'&&data.brightness==null" class="boot-field-note">当前亮度尚未读取；调整目标值后才会写入草稿。</p>
 <template v-if="module==='softwareLaunch'"><div class="boot-app-list"><Toggle v-for="app in apps" :key="app.path" :model-value="(data.paths||[]).includes(app.path)" :label="app.name" :disabled="disabled" @update:model-value="toggleApp(app.path)"/></div><button class="boot-action" :disabled="disabled" @click="emit('shared-apps')">管理全局快捷应用</button></template>
 </div>
</template>
<style scoped>
.boot-fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px 18px;align-items:start}.boot-field{min-width:0}.boot-field>label{display:grid;gap:6px;color:var(--text-dim);font-size:14px}.boot-field :deep(.slider-label){font-size:14px}.boot-power-side{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-bottom:14px}.boot-power-side button,.boot-action{min-height:35px;padding:7px 11px;border:1px solid var(--border);border-radius:6px;background:var(--bg-input);color:var(--text);font:inherit;font-size:14px;cursor:pointer}.boot-power-side button[aria-pressed=true],.primary{background:color-mix(in srgb,var(--accent) 28%,var(--bg-input));border-color:var(--accent)}.monitor-fields .boot-field:first-child,.monitor-fields .boot-field:last-child{grid-column:1/-1}.boot-controller-summary{display:flex;justify-content:space-between;align-items:center;gap:12px}.boot-controller-summary>div{display:grid;gap:4px}.boot-controller-summary strong{font-size:17px}.boot-controller-summary span{font-size:14px;color:var(--text-dim)}.boot-rule-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:14px}.boot-rule-list>div{display:flex;align-items:center;gap:6px;padding:9px;border:1px solid var(--border);border-radius:6px;background:var(--bg-input);min-width:0}.boot-rule-list i{width:6px;height:6px;border-radius:50%;background:#6b7a86}.boot-rule-list i.on{background:#6cd4b2}.boot-rule-list span{font-size:14px;flex:1}.boot-rule-list kbd{font-size:14px;max-width:40%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.boot-app-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:14px 0}.boot-action:disabled,.boot-power-side button:disabled{opacity:.5;cursor:default}
.boot-field :deep(.dd-trigger),.boot-field :deep(.toggle-label),.boot-field :deep(.slider-label),.boot-field :deep(.slider-val){font-size:14px}.boot-field-note{font-size:14px;line-height:1.5;color:var(--text-dim)}.boot-rule-list kbd{max-width:45%}:deep(.screen-touchpads-bubble){font-size:14px}:deep(.screen-touchpads-bubble label),:deep(.screen-touchpads-bubble .slider-label),:deep(.screen-touchpads-bubble .dd-trigger){font-size:14px}
:deep(.touchpad-master-copy span),:deep(.screen-touchpad-status),:deep(.screen-touchpad-sliders .slider-val){font-size:14px}
</style>
