<script setup lang="ts">
import { computed, onActivated, onDeactivated, onBeforeUnmount, onMounted, ref } from 'vue';
import InlineIcon from '@/components/InlineIcon.vue';
import SteamDeckMouseSensitivity from '@/components/SteamDeckMouseSensitivity.vue';
import ScreenTouchpadsSettings from '@/components/ScreenTouchpadsSettings.vue';
import ControllerShortcutsCard from '@/components/ControllerShortcutsCard.vue';
import ControllerFeedbackCapabilitiesCard from '@/components/ControllerFeedbackCapabilitiesCard.vue';
import ControllerShortcutEditorView from '@/views/ControllerShortcutEditorView.vue';
import { compareAndSwapInputSettings, loadSettings, type InputSettingsSnapshot } from '@/bridge/settingsRepository';
import { invoke, on } from '@/bridge/ipc';
import { shell } from '@/bridge/api';
import { subscribeGameInputOverrideState, getGameInputOverrideState } from '@/bridge/gameInputOverride';
import { closeJoyxoffIfRunning } from '@/bridge/gameproc';
import { virtualOutputPresentation, type VirtualOutputRuntimeState } from '@/bridge/virtualOutputStatus';

// 用户裁决（2026-09-14）：输出控制器文字简化——去掉各选项 detail 小字；
// 用户裁决（2026-09-16）：disabled 显示为「本机手柄」；dualshock4 改为
// 「PlayStation」；steamdeck 改为「SteamDeck」。
// 用户裁决（2026-09-24）：控制器页**只提供带背键位的人格**——本机手柄（关闭态）
// + SteamDeck / Xbox Elite / DualSense Edge (PS5)；老的三档（Xbox 360 /
// PlayStation / DualSense (PS5)）**页面不再提供（用户不可达）**，只备份不删。
// 边界（重要，防回归三条）：
//   ① 这是**纯可见性**改动——native/Host 的人格能力、老档 profile 与全部判据
//      **一个字节都没删**（前像见 mainline-write freeze 备份；恢复=删掉下方 legacy
//      过滤一行）；
//   ② 已保存的旧档配置仍被完整读取（loadTarget/校验/契约一字未改），否则任何一次
//      保存都会把 persona 写回 disabled = **静默关塔**（2026-09-16 修过的坑）；
//   ③ 当前若正停在旧档，legacy 数组把它**如实显示**（带「旧档」后缀）以便切走——
//      不让页面出现"没有任何一项被选中"。
const basePersonaOptions = [
  { value: 'disabled', label: '关闭虚拟手柄' },
  // SteamDeck（G2）：steam-deck-composite（usbip HID），Steam 原生识别 Deck
  // 背键/陀螺仪模板。需 usbip-win2 首次安装（HIDMaestro 内嵌自动装载）。
  { value: 'steamdeck', label: 'SteamDeck' },
  // 批112：新增两档（整体替换设备身份；老的四档一行未动）。
  //   dualsense-edge = dualsense-edge（054C:0DF2，USB；byte10 额外 4 个背键位）
  //   elite = xbox-elite-v2（045E:0B00，USB；Steam 侧按 Elite 识别）
  // 2026-09-27 用户裁决：顺序改为 本机手柄 → SteamDeck → PS5 → Xbox，并把两档
  // 显示名缩短为「PS5」「Xbox」（陀螺仪页、反馈卡的同名显示同步改短）。
  { value: 'dualsense-edge', label: 'PS5' },
  { value: 'elite', label: 'Xbox' },
];
// 旧档（页面不提供；仅在"当前正在使用"时如实显示并标注「旧档」）。
// 定义完整保留 ⇒ 恢复成本为零。
const legacyPersonaOptions = [
  { value: 'xbox360', label: 'Xbox 360（旧档）' },
  { value: 'dualshock4', label: 'PlayStation（旧档）' },
  { value: 'dualsense', label: 'DualSense (PS5)（旧档）' },
];

const driverSetup = ref(0);
const faultHoldActive = ref(false);
const virtualOutputRuntime = ref<VirtualOutputRuntimeState | null>(null);
// GP-926R13 §4：恢复耗尽后的可见降级（0 无 / 1 已请求物理兜底 / 2 读回物理可用 / 3 兜底未证实）。
const recoveryFallbackState = ref(0);
// E4（2026-09-13 用户批）：usbip 后端缺失/装失败弹窗（参考性能调度“配置重制”
// 自绘确认弹窗模板）。点“是”跳官网下载页；同一类别每会话只弹一次。
const USBIP_RELEASES_URL = 'https://github.com/vadimgrn/usbip-win2/releases';
const usbipPrompt = ref<null | 'missing' | 'failed'>(null);
const usbipPromptDismissed = ref(0);
const usbipPromptText = computed(() => usbipPrompt.value === 'missing'
  ? '启用 Steam Deck 需要 USBip 后端，但本地未找到 USBip 安装器。是否前往官网下载？下载后请放入 C:\\SOFT\\YeMan\\PowerControl\\redist 目录再重试。'
  : 'USBip 后端自动安装没有成功。是否前往官网下载最新版手动安装？');
function dismissUsbipPrompt(): void {
  usbipPromptDismissed.value = usbipPrompt.value === 'missing' ? 1 : 2;
  usbipPrompt.value = null;
}
async function openUsbipSite(): Promise<void> {
  usbipPrompt.value = null;
  try { await shell.open(USBIP_RELEASES_URL); } catch { /* 打开失败不打断页面 */ }
}
// 用户裁决（2026-09-14）：DualShock 4 冲突告警机制（UI 与机制）整体移除——
// 不再监听 neteaseGvInput/viGEmBus，也不再渲染冲突提示。仅保留通用后端故障
// faultHoldActive（backendFaultHold）提示。
const editorOpen = ref(false);
const snapshot = ref<InputSettingsSnapshot | null>(null);
const targetBusy = ref(false);
const gameLocked = ref(getGameInputOverrideState().locked);
let stopOverride: (() => void) | null = null;
let stopBackendState: (() => void) | null = null;
let stopShortcutRuntime: (() => void) | null = null;
type ShortcutRuntimeState = { active: boolean; persona: string; virtualEnabled: boolean };
const shortcutRuntime = ref<ShortcutRuntimeState | null>(null);
function applyShortcutRuntime(state: Partial<ShortcutRuntimeState> | null | undefined): void {
  if (typeof state?.active !== 'boolean') return;
  shortcutRuntime.value = state.active && typeof state.persona === 'string' && typeof state.virtualEnabled === 'boolean'
    ? state as ShortcutRuntimeState : null;
}
async function readShortcutRuntime(): Promise<void> {
  try { applyShortcutRuntime(await invoke<ShortcutRuntimeState>('input.shortcutRuntime.get', {})); } catch { /* Older hosts have no temporary switches. */ }
}
// 2026-09-27 用户裁决：删除底部状态行与其数据源（targetStatus）——成功/失败
// 都不再回显文案；保存失败时按钮高亮停在旧值。
const targetEnabled = ref(false);
const gyroEnabled = ref(false);
const persona = ref<'disabled' | 'xbox360' | 'dualshock4' | 'steamdeck' | 'dualsense' | 'elite' | 'dualsense-edge'>('disabled');   // 批112
// Read-only runtime feedback must never be copied into the saved page refs.
const displayPersona = computed(() => shortcutRuntime.value?.persona ?? persona.value);
const displayTargetEnabled = computed(() => shortcutRuntime.value?.virtualEnabled ?? targetEnabled.value);
// 可见选项 = 常驻四档（关闭态 + 三档带背键）；当前配置停在旧档时把它原样追加
// （如实显示 + 可切走；不追加就会出"无选中"外观）。
const visiblePersonaOptions = computed(() => [
  ...basePersonaOptions,
  ...legacyPersonaOptions.filter((option) => option.value === displayPersona.value),
]);
// 2026-09-30 用户裁决：背部按键气泡不再写死固定说明，改为如实
// 反馈当前输出人格的真实能力（陀螺仪走不走虚拟手柄、背键能识别几个）。口径与实现对齐：
//   背键：steamdeck → InputHost BuildSteamDeckState 只置 LeftPaddle/RightPaddle
//   （M1/M2→L5/R5，2 个）；dualsense-edge → MapDsEdgePaddles 同两位（G4 四键首期未接）；
//   elite 走 BuildXbox360State 不消费 backButtons；本机手柄/旧档没有虚拟手柄 → 0。
//   陀螺仪：带 IMU 通道的虚拟手柄（SteamDeck / PS5）支持；Xbox 族无 IMU → 不支持。
const PERSONA_CAPABILITIES: Record<string, { gyro: boolean; backButtons: number }> = {
  disabled: { gyro: false, backButtons: 0 },
  steamdeck: { gyro: true, backButtons: 2 },
  'dualsense-edge': { gyro: true, backButtons: 2 },
  elite: { gyro: false, backButtons: 0 },
  // 旧档（页面不提供，仅在当前使用时如实显示）
  dualshock4: { gyro: true, backButtons: 0 },
  dualsense: { gyro: true, backButtons: 0 },
  xbox360: { gyro: false, backButtons: 0 },
};
const virtualOutputStatus = computed(() => virtualOutputPresentation(
  virtualOutputRuntime.value, displayTargetEnabled.value, displayPersona.value, targetBusy.value,
));
const personaCapability = computed(() => PERSONA_CAPABILITIES[displayPersona.value] || PERSONA_CAPABILITIES.disabled);
const gyroCapabilityText = computed(() => `陀螺仪-${personaCapability.value.gyro ? '支持' : '不支持'}`);
const backButtonCapabilityText = computed(() => `背部按键-${personaCapability.value.backButtons > 0
  ? `支持识别${personaCapability.value.backButtons}个按键`
  : '不支持'}`);
// 背键映射（背键映射A任务书 2026-09-14 S6；2026-09-24 operator 裁决"开关去掉 改为常开"）：
// 桥只剩信息栏，无开关；输出人格支持背键位即生效。
// 真 Steam Deck 物理机自带背键（native 门控永不挂）。
// 2026-09-27 用户裁决：页面不再显示背部按键提示小字（功能一字未改），
// 原先只为提示文案服务的机器判定（machineFamily）随之移除。
// 用户裁决（2026-09-16）：删除「配置状态 / 运行时闭口」展示块的 computed
// 数据源——页面不再呈现内部账本字段（applyStatus / closure）。
// `outputTarget.closure` 是持久化配置元数据而非 Coordinator/InputHost 运行时
// 回执；不作为运行时闭口声明呈现。

// Runtime intent persists for live native consumption only; cold-start intent lives in startupDesired.
let loadGeneration = 0;
async function loadTarget(): Promise<void> {
  const generation = ++loadGeneration;
  const settings = await loadSettings();
  if (generation !== loadGeneration) return;
  snapshot.value = settings.input;
  gameLocked.value = getGameInputOverrideState().locked || !!settings.input.gameOverride;
  targetEnabled.value = settings.input.outputTarget?.buttonMappingEnabled === true;
  gyroEnabled.value = settings.input.outputTarget?.gyroEnabled === true;
  const value = String(settings.input.outputTarget?.persona || 'disabled');
  // 载入路径必须接受全部 persona（dualsense 2026-09-16 补漏）：否则已保存的
  // DualSense 选择在页面重载后被显示为「本机手柄」，随后任何一次保存都会把
  // persona 写回 disabled，等于静默关掉虚拟手柄。
  persona.value = value === 'dualshock4' || value === 'xbox360' || value === 'steamdeck' || value === 'dualsense' || value === 'elite' || value === 'dualsense-edge' ? value : 'disabled';   // 批112
  await readShortcutRuntime();

}
async function saveTarget(nextEnabled: boolean, nextPersona: 'disabled' | 'xbox360' | 'dualshock4' | 'steamdeck' | 'dualsense' | 'elite' | 'dualsense-edge'): Promise<void> {   // 批112
  if (gameLocked.value || !snapshot.value || targetBusy.value) return;
  targetBusy.value = true;
  try {
    const patch: Record<string, unknown> = {
      outputTarget: { buttonMappingEnabled: nextEnabled, persona: nextPersona },
    };
    // 切回本机手柄时陀螺仪也必须停，否则 native 仍会因 gyroEnabled 继续请求虚拟目标。
    if (nextPersona === 'disabled') {
      (patch.outputTarget as Record<string, unknown>).gyroEnabled = false;
      patch.gyroMotion = { enabled: false };
    } else if (nextEnabled && snapshot.value.gyroMotion?.virtualPadLink !== false &&
      !(snapshot.value.outputTarget?.buttonMappingEnabled === true && snapshot.value.outputTarget?.persona !== 'disabled')) {
      // Live off -> on linkage belongs to the actual button action, NOT page
      // hydration (which would override an explicit startup gyro=off choice).
      (patch.outputTarget as Record<string, unknown>).gyroEnabled = true;
      patch.gyroMotion = { enabled: true, outputMode: 'virtual-stick' };
    }
    const result = await compareAndSwapInputSettings(snapshot.value.revision, patch);
    snapshot.value = result.value;
    if (!result.ok) {
      await loadTarget();
      return;
    }
    // A manual selection wins over a runtime-only shortcut, even if reselecting the saved persona.
    await invoke('input.shortcutRuntime.clear', {}).catch(() => undefined);
    shortcutRuntime.value = null;
    targetEnabled.value = nextEnabled;
    persona.value = nextPersona;
    gyroEnabled.value = result.value.outputTarget?.gyroEnabled === true;
    // S4 常驻（2026-09-15 用户定案）：关闭开关 = 停写不拆（虚拟设备保持
    // 存活、身份不变、物理手柄持续隐藏）；仅切回「本机手柄」才真正拆除解隐。
    // 2026-09-27 用户裁决：状态行整行移除，保存结果不再回显文案。
  } catch {
    /* 状态行已按用户裁决移除：失败时按钮高亮停在旧值 */
  } finally { targetBusy.value = false; }
}
// 用户裁决（2026-09-16）：删除「启用虚拟手柄」开关——「本机手柄」即关闭态，
// 由输出控制器按钮直接控制（选中即启用，选回本机手柄即关闭/拆塔解隐）。
// 原 onTargetEnabled 已随之删除。
function onPersona(value: string | number): void {
  if (gameLocked.value || targetBusy.value) return;
  const next = value === 'xbox360' || value === 'dualshock4' || value === 'steamdeck' || value === 'dualsense' || value === 'elite' || value === 'dualsense-edge' ? String(value) : 'disabled';   // 批112
  // 2026-09-30 用户裁决：切到 SteamDeck 虚拟手柄时鼠标交给 Steam，JoyXoff 若在跑
  // 强行关掉（存在才关）；顶部专属菜单里切同一人格时走同一处理。
  if (next === 'steamdeck') void closeJoyxoffIfRunning().catch(() => undefined);
  void saveTarget(next !== 'disabled', next as 'disabled' | 'xbox360' | 'dualshock4' | 'steamdeck' | 'dualsense' | 'elite' | 'dualsense-edge');
}
onMounted(() => {
  stopShortcutRuntime = on<ShortcutRuntimeState>('input.shortcutRuntime', applyShortcutRuntime);
  stopOverride = subscribeGameInputOverrideState((state) => {
    gameLocked.value = state.locked;
    void loadTarget().catch(() => undefined);
  });
  void loadTarget().catch(() => undefined);
  // Native publishes the read-only backend state with gamepad.state; the card
  // only re-renders, it never disables anything. 用户裁决（2026-09-14）：
  // DS4 冲突告警已整体移除，不再消费 virtualInputConflicts。
  stopBackendState = on<{ driverSetup?: number; backendFaultHold?: boolean; backendInstallPrompt?: number; recoveryFallback?: number; virtualOutput?: VirtualOutputRuntimeState }>('gamepad.state', (state) => {
    driverSetup.value = state.driverSetup ?? 0;
    virtualOutputRuntime.value = state.virtualOutput ?? null;
    faultHoldActive.value = state.backendFaultHold === true;
    recoveryFallbackState.value = state.recoveryFallback ?? 0;
    // E4：native 侧 PREPARE_TARGET 拒绝时的 usbip 提示位（1=缺安装器 2=装失败）。
    const prompt = state.backendInstallPrompt;
    if (prompt && usbipPromptDismissed.value !== prompt && !usbipPrompt.value) {
      usbipPrompt.value = prompt === 1 ? 'missing' : 'failed';
    }
  });
});
onBeforeUnmount(() => { stopOverride?.(); stopBackendState?.(); stopShortcutRuntime?.(); });
onActivated(() => { void loadTarget().catch(() => undefined); });
// The controller page is cached by KeepAlive. Do not leave a Teleport-ed
// modal visible after navigating to another page.
onDeactivated(() => { editorOpen.value = false; });
function openShortcutEditor(): void {
  if (editorOpen.value) return;
  editorOpen.value = true;
}
function closeShortcutEditor(): void {
  editorOpen.value = false;
}
</script>

<template>
  <div class="page">
    <!-- 2026-09-27 用户裁决：虚拟手柄卡移到页面顶部（手柄快捷操作之上）。 -->
    <section class="card virtual-card">
      <div class="card-head">
        <div><h3 class="card-title"><InlineIcon name="gamepad" /> 虚拟手柄</h3></div>
        <span class="state-chip" :class="{ on: targetEnabled }">{{ targetEnabled ? '已请求' : '未开启' }}</span>
      </div>
      <!-- 用户裁决（2026-09-16）：去掉「启用虚拟手柄」开关（「本机手柄」即关闭态）
           与说明小字；输出控制器四按钮改为性能调度同款独立按钮行（无 bubble
           包裹，按钮直接落在卡片上，具有独立外轮廓）。 -->
      <div class="persona-block">
        <span class="bubble-label">输出控制器</span>
        <p v-if="gameLocked" class="game-override-notice" role="status">游戏专属配置生效中</p>
        <div v-if="driverSetup === 1" class="fault-line">
          <strong>正在准备手柄环境</strong><span>自动补装仍在后台进行，可以继续尝试启用虚拟手柄。若本次创建失败，请等准备结束后重试。</span>
        </div>
        <div v-else-if="driverSetup === 4" class="fault-line">
          <strong>手柄驱动安装后需要重启</strong><span>可以继续尝试启用虚拟手柄，但屏蔽或输出可能尚未生效。请保存工作并重启 Windows；程序不会自动重启。</span>
        </div>
        <div v-else-if="driverSetup === 6" class="fault-line">
          <strong>缺少 HidHide 安装器</strong><span>可以继续尝试启用虚拟手柄。请用完整安装包恢复 C:\SOFT\YeMan\PowerControl\redist\HidHide_1.5.230_x64.exe，然后重启 YMCC 以自动补装。</span>
        </div>
        <div v-else-if="driverSetup === 7" class="fault-line">
          <strong>HidHide 安装器校验失败</strong><span>自动补装已跳过，可以继续尝试启用虚拟手柄。请用完整安装包恢复 C:\SOFT\YeMan\PowerControl\redist\HidHide_1.5.230_x64.exe，然后重启 YMCC。</span>
        </div>
        <div v-else-if="driverSetup === 3 || driverSetup === 5" class="fault-line">
          <strong>手柄环境提示</strong><span>{{ driverSetup === 5 ? '后台补装耗时较长，仍在观察同一次安装。' : '环境检查未通过或自动补装未完成，请导出日志查看原因。' }}可以继续尝试启用虚拟手柄；实际创建失败时会显示后端错误。</span>
        </div>
        <div v-else-if="driverSetup !== 0 && driverSetup !== 2" class="fault-line">
          <strong>手柄环境状态未确认</strong><span>可以继续尝试启用虚拟手柄，请导出日志查看环境检查结果。</span>
        </div>
        <div v-if="targetEnabled && faultHoldActive" class="fault-line">
          <strong>虚拟输出已停写</strong>
          <span>后端创建或输入输出失败，已停止虚拟输出。请导出日志查看原因，重新选择虚拟手柄可重试。</span>
        </div>
        <div v-else-if="targetEnabled && recoveryFallbackState > 0" class="fault-line">
          <strong>虚拟未恢复</strong>
          <span>{{ recoveryFallbackState === 2 ? '已请求物理兜底（读回：物理可用）。' : '已请求物理兜底（读回结果见日志）。' }}请重新开关虚拟手柄或切换人格重试。</span>
        </div>
        <div class="persona-actions">
          <button
            v-for="o in visiblePersonaOptions"
            :key="o.value"
            type="button"
            class="persona-btn"
            :class="{ active: o.value === displayPersona }"
            :disabled="gameLocked || targetBusy || !snapshot"
            @click="onPersona(o.value)"
          >{{ o.label }}</button>
        </div>
      </div>
      <!-- 背键映射（2026-09-24 operator 裁决：去掉开关、改为**常开**。
           输出人格支持背键位即生效（native 侧同日常开）；真 Deck 物理机仍不挂。
           2026-09-27 用户裁决：提示小字不再显示。
           2026-09-30 用户裁决：气泡改为如实反馈当前人格能力（陀螺仪 / 背键数量）。 -->
      <div class="persona-bubble" :class="{ disabled: !displayTargetEnabled }">
        <p class="persona-capabilities" role="status" aria-live="polite">
          <span :class="{ 'cap-on': virtualOutputStatus.tone === 'on', 'cap-off': virtualOutputStatus.tone === 'off', 'cap-warning': virtualOutputStatus.tone === 'warning', 'cap-pending': virtualOutputStatus.tone === 'pending' }" :title="virtualOutputStatus.detail">{{ virtualOutputStatus.text }}</span>
          <span :class="personaCapability.gyro ? 'cap-on' : 'cap-off'">{{ gyroCapabilityText }}</span>
          <span :class="personaCapability.backButtons > 0 ? 'cap-on' : 'cap-off'" title="当前输出模式的背键能力；实际按键信号及 Steam 映射仍需确认。">{{ backButtonCapabilityText }}</span>
        </p>
      </div>

      <SteamDeckMouseSensitivity v-if="displayTargetEnabled && displayPersona === 'steamdeck'" :disabled="gameLocked || targetBusy" />
      <ScreenTouchpadsSettings :key="displayPersona" :persona="displayPersona"
        :steam-deck-enabled="displayTargetEnabled && displayPersona === 'steamdeck'"
        :ps5-enabled="displayTargetEnabled && ['dualsense-edge', 'dualsense', 'dualshock4'].includes(displayPersona)" :disabled="gameLocked || targetBusy" />
    </section>
    <ControllerShortcutsCard :steam-deck-active="displayTargetEnabled && displayPersona === 'steamdeck'" @open-editor="openShortcutEditor" />
    <ControllerFeedbackCapabilitiesCard />
    <ControllerShortcutEditorView v-if="editorOpen" @close="closeShortcutEditor" />
    <!-- E4（2026-09-13 用户批）：usbip 后端引导弹窗。 -->
    <div v-if="usbipPrompt" class="usbip-prompt-backdrop" @click.self="dismissUsbipPrompt">
      <div class="usbip-prompt-card">
        <h3 class="card-title"><InlineIcon name="download" /> 需要 USBip 后端</h3>
        <p>{{ usbipPromptText }}</p>
        <div class="usbip-prompt-actions">
          <button type="button" class="usbip-btn" @click="dismissUsbipPrompt">稍后再说</button>
          <button type="button" class="usbip-btn primary" @click="openUsbipSite">是，去官网下载</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.page{padding-bottom:20px}.card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.card-title{margin:0}.state-chip{flex:0 0 auto;min-height:24px;display:inline-flex;align-items:center;padding:0 8px;border:1px solid rgba(255,255,255,.12);border-radius:999px;color:var(--text-dim);background:color-mix(in srgb,var(--bg-input) 86%,transparent);font-size:10px;font-weight:700}.state-chip.on{border-color:color-mix(in srgb,var(--accent) 58%,transparent);background:color-mix(in srgb,var(--accent) 14%,transparent);color:var(--accent)}.persona-bubble{border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent);margin-top:8px;padding:10px}.persona-bubble.disabled{opacity:.66}
/* 输出控制器独立按钮行（2026-09-16 用户裁决：参考性能调度 .tool-actions 样式，
   按钮直接落在卡片上以获得对比与独立外轮廓；不再用 SegButton/bubble 包裹）。 */
.persona-block{margin-top:12px}
.game-override-notice{margin:0 0 8px;color:var(--accent);font-size:11px;font-weight:700}
.persona-actions{display:flex;align-items:center;gap:7px}
.persona-actions .persona-btn{flex:1 1 0;min-width:0;min-height:38px;padding:0 8px;border:1px solid rgba(255,255,255,.06);border-radius:8px;background:var(--bg-input);color:var(--text);font-size:11px;font-weight:700;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;white-space:nowrap;transition:background .12s,color .12s,border-color .12s}
.persona-actions .persona-btn:hover:not(:disabled){color:var(--text)}
.persona-actions .persona-btn.active{color:var(--accent);border-color:color-mix(in srgb,var(--accent) 45%,transparent);background:color-mix(in srgb,var(--accent) 10%,var(--bg-input))}
.persona-actions .persona-btn:disabled{opacity:.42;cursor:default}.fault-line{display:grid;gap:3px;margin:-2px 0 9px;padding:8px 9px;border-radius:var(--radius-ctrl)}.fault-line{border:1px solid rgba(244,163,61,.45);background:color-mix(in srgb,#f4a33d 10%,transparent)}.fault-line strong{color:#ffc46b;font-size:11px}.fault-line span{color:var(--text-dim);font-size:10px;line-height:1.45}.bubble-label{display:block;margin:0 0 8px;color:var(--text-dim);font-size:11px;font-weight:700}/* 能力反馈行（2026-09-30）：替换原来的「背键映射常开」说明文字。 */.persona-capabilities{display:flex;flex-wrap:wrap;gap:4px 14px;margin:0;font-size:11px;font-weight:700}.persona-capabilities .cap-on{color:var(--accent)}.persona-capabilities .cap-off{color:var(--text-dim)}.persona-capabilities .cap-warning{color:#ffc46b}.persona-capabilities .cap-pending{color:var(--text-dim)}.usbip-prompt-backdrop{position:fixed;inset:0;z-index:900;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,.55)}.usbip-prompt-card{width:min(420px,calc(100vw - 32px));padding:16px;border:1px solid rgba(255,255,255,.12);border-radius:var(--radius-ctrl);background:var(--bg-panel)}.usbip-prompt-card h3{display:flex;align-items:center;gap:6px;margin:0 0 8px;font-size:13px}.usbip-prompt-card p{margin:0 0 14px;color:var(--text-dim);font-size:11px;line-height:1.5}.usbip-prompt-actions{display:flex;justify-content:flex-end;gap:8px}.usbip-btn{min-height:30px;padding:0 12px;border:1px solid rgba(255,255,255,.14);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent);color:var(--text);font-size:11px;font-weight:700;cursor:pointer}.usbip-btn.primary{border-color:color-mix(in srgb,var(--accent) 55%,transparent);background:color-mix(in srgb,var(--accent) 14%,transparent);color:var(--accent)}
</style>
