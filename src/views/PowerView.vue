<script setup lang="ts">
import { ref, reactive, computed, onMounted, onUnmounted, onActivated, nextTick, inject, watch, type Ref } from 'vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import { normalizeInputStartupPreferences, STARTUP_PAD_OPTIONS, STARTUP_GYRO_OPTIONS,
  type StartupPadPersona, type StartupGyroPreset } from '@/bridge/inputStartupPolicy';
import WarnBar from '@/components/WarnBar.vue';
import {
  taskExists,
  toggleTask,
  fxExists,
  fxSet,
  joyExists,
  joySet,
  searchState,
  openSearchFolder,
  readGamingHomeConfiguration,
  writeGamingHomeStartup,
  BOOT_CONTROL_CENTER_TASK,
  readBootMirrorState,
  toggleBootMirror,
  BOOT_MIRROR_CHANGED_EVENT,
  readHardwareGpuSchedule,
  writeHardwareGpuSchedule,
  readBootRtssState,
  toggleBootRtss,
  steamMasterOn,
  steamMasterSet,
} from '@/bridge/yeman';
import { shell, fs } from '@/bridge/api';
import { readSettingsSection, saveSettingsSection } from '@/bridge/settingsRepository';
import { readTrayResident, setTrayResident } from '@/bridge/trayResident';
import { deleteAllYemanTasks, restoreAllYemanTasks } from '@/bridge/yeman';
import InlineIcon from '@/components/InlineIcon.vue';
import type { GamingHomeState } from '@/bridge/gamingHomePolicy';

const BOOT_TASK = BOOT_CONTROL_CENTER_TASK;
const BOOT_CFG = 'C:\\SOFT\\YeMan\\PowerControl\\boot_config.json';
const bootOn = ref(false);
const startupPad = ref<StartupPadPersona>('disabled');
const startupGyro = ref<StartupGyroPreset>('off');
const startupInputLoaded = ref(false);
const startupInputBusy = ref(false);
let startupInputGeneration = 0;
async function refreshInputStartup(): Promise<void> {
  const generation = ++startupInputGeneration;
  const raw = await readSettingsSection('startupDesired');
  if (generation !== startupInputGeneration || startupInputBusy.value) return;
  const preferences = normalizeInputStartupPreferences(raw);
  startupPad.value = preferences.virtualGamepadPersona;
  startupGyro.value = preferences.gyroPreset;
  startupInputLoaded.value = true;
}
async function saveInputStartup(rawPad: string | number, rawGyro: string | number): Promise<void> {
  if (!startupInputLoaded.value || startupInputBusy.value || busy.value) return;
  const next = normalizeInputStartupPreferences({ virtualGamepadPersona: rawPad, gyroPreset: rawGyro });
  startupInputGeneration += 1;
  startupInputBusy.value = true; errMsg.value = '';
  try {
    // Save boot intent only. Never alter the currently running input/persona.
    await saveSettingsSection('startupDesired', next);
    startupPad.value = next.virtualGamepadPersona; startupGyro.value = next.gyroPreset;
  } catch (error) {
    errMsg.value = '输入开机启动设置失败：' + (error as Error).message;
  } finally { startupInputBusy.value = false; }
}
function onStartupPad(raw: string | number): void {
  if (!STARTUP_PAD_OPTIONS.some(option => option.value === raw)) return;
  void saveInputStartup(raw, startupGyro.value);
}
function onStartupGyro(raw: string | number): void {
  if (startupPad.value === 'disabled' || !STARTUP_GYRO_OPTIONS.some(option => option.value === raw)) return;
  void saveInputStartup(startupPad.value, raw);
}
const fanControlOnBoot = ref(false);
const rtssBootOn = ref(false);

const tasks = reactive({ desktopMode: false, energyStar: false, cleanMem: false });
type TaskKey = keyof typeof tasks;
const audioOpt = ref(false);
const steamCommunityBoot = ref(false);
const STEAMCOMMUNITY_TASK = 'Steamcommunity_302';
const joyMouse = ref(false);
const searchSt = ref<'hidden' | 'trimmed' | 'full'>('full');

const busy = ref(false);
const errMsg = ref('');
const taskToolsBusy = ref(false);
const taskToolsMsg = ref('');

// 任务栏常驻（托盘）开关：默认不常驻；由前端持久化 + 启动期套用
const trayResident = ref(false);
const hardwareGpuSchedule = ref(false);

// Persistent system state is independent of Steam, tray and legacy task existence.
const startupGamingHome = ref(false);
const handheldConfigured = ref(false);
const steamEarlyStart = ref(false);

async function safeExists(name: string): Promise<boolean> {
  try {
    return await taskExists(name);
  } catch {
    return false;
  }
}

async function readBootState(): Promise<boolean> {
  let configured = false;
  try {
    const startup = await readSettingsSection<any>('startupDesired');
    configured = startup.bootControlCenter === true;
  } catch { /* 使用任务计划真实状态 */ }
  const actual = await readBootMirrorState();
  if (configured && !actual) {
    try {
      await toggleBootMirror(true);
      return true;
    } catch { /* 页面显示真实状态 */ }
  }
  return actual;
}

const xboxSuiteEnabled = computed(() => handheldConfigured.value && startupGamingHome.value);
function applyGamingHomeState(state: GamingHomeState): void {
  handheldConfigured.value = state?.deviceForm === 46;
  startupGamingHome.value = state?.startupEnabled === true;
}
async function refreshXboxSuiteState(): Promise<void> {
  // Observe only. Task absence, a failed query or a page visit must NEVER disable startup.
  applyGamingHomeState(await readGamingHomeConfiguration());
}
async function onXboxSuiteToggle(enabled: boolean): Promise<void> {
  if (busy.value) return;
  errMsg.value = '';
  busy.value = true;
  try {
    if (!await writeGamingHomeStartup(enabled)) throw new Error('全屏启动配置写入失败');
    await refreshXboxSuiteState();
    if (enabled !== xboxSuiteEnabled.value) throw new Error('全屏启动配置回读不一致');
  } catch (e) {
    await refreshXboxSuiteState().catch(() => {});
    errMsg.value = '全屏游戏模式设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function refresh() {
  errMsg.value = '';
  void refreshInputStartup().catch((error) => { errMsg.value = '读取输入开机配置失败：' + (error as Error).message; });
  // 清理旧版本遗留的 AMD395 任务，避免它继续在开机时执行。
  await toggleTask('Bug修复-AMD-395', false).catch(() => {});
  // 并行异步加载（不串行等待，不阻塞渲染）
  const [dmRes, steamEarlyStartRes, gamingRes, esRes, cmRes, fxRes, steamCommunityRes, joyRes, searchRes, bootRes, rtssBootRes, fanBootRes] = await Promise.allSettled([
    safeExists('桌面模式-开机设置为桌面模式'),
    steamMasterOn(),
    readGamingHomeConfiguration(),
    safeExists('节能-能源之星'),
    safeExists('内存-开机自动内存清理并关闭'),
    fxExists(),
    safeExists(STEAMCOMMUNITY_TASK),
    joyExists(),
    searchState(),
    readBootState(),
    readBootRtssState(),
    readSettingsSection<{ fanControl?: boolean }>('startupDesired'),
  ]);
  if (dmRes.status === 'fulfilled') tasks.desktopMode = dmRes.value;
  if (steamEarlyStartRes.status === 'fulfilled') steamEarlyStart.value = steamEarlyStartRes.value;
  if (gamingRes.status === 'fulfilled') applyGamingHomeState(gamingRes.value);
  else errMsg.value = '读取全屏启动配置失败：' + String(gamingRes.reason);
  if (esRes.status === 'fulfilled') tasks.energyStar = esRes.value;
  if (cmRes.status === 'fulfilled') tasks.cleanMem = cmRes.value;
  if (fxRes.status === 'fulfilled') audioOpt.value = fxRes.value;
  if (steamCommunityRes.status === 'fulfilled') steamCommunityBoot.value = steamCommunityRes.value;
  if (joyRes.status === 'fulfilled') joyMouse.value = joyRes.value;
  if (searchRes.status === 'fulfilled') searchSt.value = searchRes.value;
  if (bootRes.status === 'fulfilled') bootOn.value = bootRes.value;
  if (rtssBootRes.status === 'fulfilled') rtssBootOn.value = rtssBootRes.value;
  if (fanBootRes.status === 'fulfilled') fanControlOnBoot.value = fanBootRes.value.fanControl === true;
  trayResident.value = await readTrayResident().catch(() => false);
  hardwareGpuSchedule.value = await readHardwareGpuSchedule().catch(() => false);
  // Refresh never writes gaming configuration or changes taskbar residency.
}

async function onBootToggle(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  const prev = bootOn.value;
  bootOn.value = v; // 乐观更新
  try {
    await saveSettingsSection('startupDesired', { bootControlCenter: v });
    await toggleBootMirror(v);
  } catch (e) {
    bootOn.value = (await readSettingsSection<any>('startupDesired').catch(() => ({ bootControlCenter: prev }))).bootControlCenter === true; // Display the durable preference, including saved-but-not-applied intent.
    errMsg.value = '设置失败：' + (e as Error).message + '（创建开机任务需管理员权限，请右键以管理员身份运行 YeManCC）';
  } finally {
    busy.value = false;
  }
}

async function onFanControlBootToggle(v: boolean): Promise<void> {
  errMsg.value = '';
  const prev = fanControlOnBoot.value;
  fanControlOnBoot.value = v;
  busy.value = true;
  try {
    await saveSettingsSection('startupDesired', { fanControl: v });
  } catch (e) {
    fanControlOnBoot.value = prev;
    errMsg.value = '风扇控制开机启动设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function toggleTaskSafe(name: string, on: boolean, key: TaskKey) {
  errMsg.value = '';
  busy.value = true;
  try {
    await toggleTask(name, on);
    // 参考其他正确操作：切换后重新检测真实状态，而非信任返回值（避免创建失败却显示已开）
    tasks[key] = await safeExists(name);
  } catch (e) {
    tasks[key] = await safeExists(name).catch(() => !on); // 回滚到真实状态
    errMsg.value = '任务计划操作失败：' + (e as Error).message + '（需管理员权限）';
  } finally {
    busy.value = false;
  }
}

// 任务栏常驻：开→任务栏按钮(移除托盘)；关→仅托盘（默认）
async function onTrayResident(v: boolean) {
  if (busy.value) return;
  errMsg.value = '';
  busy.value = true;
  const prev = trayResident.value;
  trayResident.value = v; // 乐观更新
  try {
    await setTrayResident(v);
  } catch (e) {
    trayResident.value = await readTrayResident().catch(() => prev); // Do not claim rollback unless it persisted.
    errMsg.value = '任务栏常驻设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function openBootTaskTool() {
  try {
    await shell.execute('C:\\SOFT\\开机启动-计划任务.exe', []);
  } catch (e) {
    errMsg.value = '打开开机启动调节程序失败：' + (e as Error).message;
  }
}

async function closeAllYemanTasks() {
  taskToolsBusy.value = true;
  taskToolsMsg.value = '';
  try {
    await deleteAllYemanTasks();
    taskToolsMsg.value = '已删除「野蛮优化整合系统」任务目录中的全部任务。';
    await refresh();
    window.dispatchEvent(new CustomEvent('ipc:gamepad.refresh'));
  } catch (e) {
    errMsg.value = '关闭全部任务失败：' + (e as Error).message + '（需管理员权限）';
  } finally {
    taskToolsBusy.value = false;
  }
}

async function restoreAllTasks() {
  taskToolsBusy.value = true;
  taskToolsMsg.value = '';
  try {
    const result = await restoreAllYemanTasks();
    await refresh();
    window.dispatchEvent(new CustomEvent('ipc:gamepad.refresh'));
    if (result.failed.length) {
      taskToolsMsg.value = `已恢复 ${result.imported} 个任务，${result.failed.length} 个任务失败：${result.failed.join('、')}`;
    } else {
      taskToolsMsg.value = `已从 PowerControl 恢复 ${result.imported} 个任务。`;
    }
  } catch (e) {
    errMsg.value = '恢复全部任务失败：' + (e as Error).message + '（需管理员权限）';
  } finally {
    taskToolsBusy.value = false;
  }
}

async function onFx(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    await fxSet(v);
    audioOpt.value = v;
  } catch (e) {
    audioOpt.value = !v;
    errMsg.value = 'FxSound 设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function onSteamEarlyStart(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    await steamMasterSet(v);
    steamEarlyStart.value = v;
  } catch (e) {
    steamEarlyStart.value = !v;
    errMsg.value = '写入 .earlystart 失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function onHardwareGpuSchedule(v: boolean) {
  const prev = hardwareGpuSchedule.value;
  hardwareGpuSchedule.value = v;
  try {
    const ok = await writeHardwareGpuSchedule(v);
    if (!ok) throw new Error('注册表写入失败');
    errMsg.value = '硬件加速GPU计划已更新，重启或重新登录后完全生效';
  } catch (e) {
    hardwareGpuSchedule.value = prev;
    errMsg.value = '硬件加速GPU计划设置失败：' + (e as Error).message;
  }
}
async function onSteamCommunityBoot(v: boolean) {
  errMsg.value = '';
  const prev = steamCommunityBoot.value;
  steamCommunityBoot.value = v;
  busy.value = true;
  try {
    await toggleTask(STEAMCOMMUNITY_TASK, v);
    steamCommunityBoot.value = await safeExists(STEAMCOMMUNITY_TASK);
  } catch (e) {
    steamCommunityBoot.value = prev;
    errMsg.value = 'Steamcommunity_302 开机启动设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}
async function onJoy(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    await joySet(v);
    joyMouse.value = v;
  } catch (e) {
    joyMouse.value = !v;
    errMsg.value = 'Joyxoff 设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}
async function onBootRtssToggle(v: boolean) {
  const prev = rtssBootOn.value;
  rtssBootOn.value = v;
  busy.value = true;
  try {
    rtssBootOn.value = await toggleBootRtss(v);
  } catch (e) {
    rtssBootOn.value = prev;
    errMsg.value = '开机启动 RTSS 监控设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

const searchText = computed(() => {
  if (searchSt.value === 'hidden') return '不可用（系统版本差异）';
  if (searchSt.value === 'trimmed') return '任务栏搜索已精简 [点击恢复]';
  return '任务栏搜索存在多进程 [点击精简]';
});
async function onSearch() {
  try {
    await openSearchFolder();
  } catch {
    /* ignore */
  }
}


// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  // watch 已在顶部静态导入；动态 import('vue') 会造成异步微任务延迟注册，
  // 刷新事件可能在注册前触发而丢失（2026-08-05 修复）。
  watch(globalRefreshKey, () => refresh());
}

onMounted(() => nextTick(refresh));
async function onBootMirrorChanged() {
  try {
    bootOn.value = await readBootMirrorState();
  } catch {
  }
}
onMounted(() => window.addEventListener(BOOT_MIRROR_CHANGED_EVENT, onBootMirrorChanged));
onUnmounted(() => window.removeEventListener(BOOT_MIRROR_CHANGED_EVENT, onBootMirrorChanged));
// KeepAlive 缓存下切回本页时主动刷新，避免「其它页面改了开机启动项切回来仍显示旧状态」
// （2026-08-05 补充 onActivated）。
onActivated(() => {
  refresh().catch(() => {});
});
</script>

<template>
  <div class="page">
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div v-if="taskToolsMsg" class="info-bar">{{ taskToolsMsg }}</div>

    <section class="card">
      <h3 class="card-title steam-xbox-title"><InlineIcon name="rocket" /> Xbox全屏游戏模式</h3>
      <p class="steam-xbox-subtitle">基于Xbox全屏兼容Steam大屏启动</p>
      <Toggle
        :model-value="xboxSuiteEnabled"
        label="开机进入Xbox全屏游戏模式"
        color="accent"
        :disabled="busy"
        @update:model-value="onXboxSuiteToggle"
      />
      <Toggle :model-value="trayResident" label="任务栏常驻" description="独立控制全屏模式中的 YMCC 任务栏入口" color="accent" :disabled="busy" @update:model-value="onTrayResident" />
      <WarnBar v-if="startupGamingHome && !trayResident" text="任务栏常驻关闭时，全屏模式中的 YMCC 入口可能不可用；不影响系统全屏自启动配置" />
    </section>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="steam" /> Steam大屏开机启动</h3>
      <Toggle v-model="steamEarlyStart" label="Steam高级开机启动(earlystart)" description="开启的是Steam大屏页面的联动模式" color="accent" :disabled="busy" @update:model-value="onSteamEarlyStart" />
    </section>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="star" /> 开机启动项</h3>
      <div class="startup-input-row" data-input-startup="pad">
        <span>开机启动虚拟手柄</span>
        <Dropdown :model-value="startupPad" :options="STARTUP_PAD_OPTIONS" aria-label="开机启动虚拟手柄"
          :disabled="busy || startupInputBusy || !startupInputLoaded" @update:model-value="onStartupPad" />
      </div>
      <div class="startup-input-row" data-input-startup="gyro" :class="{ disabled: startupPad === 'disabled' }">
        <span>开机启动陀螺仪</span>
        <Dropdown :model-value="startupPad === 'disabled' ? 'off' : startupGyro" :options="STARTUP_GYRO_OPTIONS" aria-label="开机启动陀螺仪"
          :disabled="busy || startupInputBusy || !startupInputLoaded || startupPad === 'disabled'" @update:model-value="onStartupGyro" />
      </div>
      <Toggle
        v-model="bootOn"
        label="开机启动野蛮控制中心"
        description="登录系统后自动启动控制中心"
        color="accent"
        :disabled="busy"
        @update:model-value="onBootToggle"
      />
      <Toggle
        v-model="fanControlOnBoot"
        label="开机/休眠唤醒启动风扇"
        description="根据适配机型自动启动风扇"
        color="accent"
        :disabled="busy"
        @update:model-value="onFanControlBootToggle"
      />
      <Toggle v-model="tasks.desktopMode" label="桌面模式" description="开机设置为桌面模式" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('桌面模式-开机设置为桌面模式', v, 'desktopMode')" />
      <Toggle
        v-model="rtssBootOn"
        label="开机启动 RTSS 监控"
        description="登录系统后自动启动 RTSS 监控"
        color="accent"
        :disabled="busy"
        @update:model-value="onBootRtssToggle"
      />
      <Toggle v-model="tasks.energyStar" label="开机能源之星自动优化" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('节能-能源之星', v, 'energyStar')" />
      <Toggle v-model="tasks.cleanMem" label="开机清理一次内存" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('内存-开机自动内存清理并关闭', v, 'cleanMem')" />
      <Toggle v-model="audioOpt" label="开机启动音质优化 (FxSound)" color="accent" :disabled="busy" @update:model-value="onFx" />
      <Toggle v-model="steamCommunityBoot" label="开机启动加速Steam(Steamcommunity)" color="accent" :disabled="busy" @update:model-value="onSteamCommunityBoot" />
      <Toggle v-model="joyMouse" label="开机启动手柄模拟鼠标 (Joyxoff)" color="accent" :disabled="busy" @update:model-value="onJoy" />
      <Toggle v-model="hardwareGpuSchedule" label="硬件加速GPU计划" description="关闭时解决AMD395游戏BUG" color="accent" :disabled="busy" @update:model-value="onHardwareGpuSchedule" />
      <div class="row">
        <span><InlineIcon name="search" /> Windows 任务栏搜索</span>
        <button class="mini-btn" @click="onSearch">{{ searchText }}</button>
      </div>
      <div class="task-tools" aria-label="任务计划批量管理">
        <button class="task-tool-btn" :disabled="taskToolsBusy || busy" @click="openBootTaskTool">开机启动调节程序</button>
        <button class="task-tool-btn danger" :disabled="taskToolsBusy || busy" @click="closeAllYemanTasks">关闭野蛮系统全部任务</button>
        <button class="task-tool-btn" :disabled="taskToolsBusy || busy" @click="restoreAllTasks">恢复野蛮系统全部任务</button>
      </div>
    </section>
  </div>
</template>

<style scoped>
.startup-input-row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:9px 0}
.startup-input-row>span{flex:0 0 auto;font-size:12px}
.startup-input-row :deep(.dd){width:180px;max-width:55%;flex:0 1 180px}
.startup-input-row.disabled>span{opacity:.45}

.steam-xbox-title {
  font-size: 16px;
  font-weight: 700;
}
.steam-xbox-subtitle {
  margin: 0 0 8px;
  color: var(--text-dim);
  font-size: 13px;
  font-weight: 600;
  line-height: 1.5;
}
.page {
  padding-bottom: 20px;
}
.info-bar {
  background: rgba(46, 166, 255, 0.12);
  border: 1px solid rgba(46, 166, 255, 0.4);
  color: #7dd3fc;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.err-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 8px;
}
.small {
  font-size: 11px;
  margin: 0 0 8px;
}
.task-tools {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 6px;
  margin: 10px 0;
}
.task-tool-btn {
  min-width: 0;
  min-height: var(--btn-min-h);
  padding: 6px 5px;
  border: 1px solid rgba(46, 166, 255, 0.45);
  border-radius: var(--radius-ctrl);
  background: rgba(46, 166, 255, 0.08);
  color: var(--accent);
  font-size: 10px;
  font-weight: 700;
  line-height: 1.25;
  cursor: pointer;
}
.task-tool-btn.danger {
  border-color: rgba(229, 72, 77, 0.45);
  background: rgba(229, 72, 77, 0.08);
  color: #ff9ea1;
}
.task-tool-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.task-tool-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.mini-btn {
  background: var(--bg-input);
  color: var(--text);
  border: 1px solid #2a3342;
  border-radius: 6px;
  padding: 5px 8px;
  font-size: 11px;
  cursor: pointer;
  max-width: 200px;
  text-align: right;
}
.mini-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
</style>
