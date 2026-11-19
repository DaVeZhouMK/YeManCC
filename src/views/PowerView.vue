<script setup lang="ts">
import BootModesPanel from '@/components/BootModesPanel.vue';
import SettingsSections from '@/components/SettingsSections.vue';
import IntegratedPage from '@/components/IntegratedPage.vue';
import Toggle from '@/components/Toggle.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import {computed,ref,reactive,onMounted,onUnmounted,onActivated,onDeactivated,inject,watch,type Ref} from 'vue';
import {taskExists,toggleTask,searchState,openSearchFolder,readBootMirrorState,toggleBootMirror,BOOT_MIRROR_CHANGED_EVENT,readHardwareGpuSchedule,writeHardwareGpuSchedule,readBootRtssState,toggleBootRtss,deleteAllYemanTasks,restoreAllYemanTasks} from '@/bridge/yeman';
import {shell} from '@/bridge/api';
import {isUiVisible,onUiVisibilityChange} from '@/bridge/uiLifecycle';
import {DESKTOP_MODE_BOOT_TASK,DESKTOP_MODE_BOOT_CHANGED_EVENT,setDesktopModeStartup} from '@/bridge/desktopModeStartup';
import {NETWORK_ACCELERATION_BOOT_TASK,NETWORK_ACCELERATION_BOOT_CHANGED_EVENT,setNetworkAccelerationStartup} from '@/bridge/networkAccelerationStartup';
import {readSettingsSection,saveSettingsSection} from '@/bridge/settingsRepository';
import {readGameBarControllerEnabled,setGameBarControllerEnabled,GAME_BAR_CONTROLLER_CHANGED_EVENT} from '@/bridge/gameBarControllerStartup';
const desktopBootOn=ref(false),bootOn=ref(false),fanControlOnBoot=ref(false),rtssBootOn=ref(false),steamCommunityBoot=ref(false);
const steamOverlayOnBoot=ref(true);
const gameBarControllerOn=ref(false),gameBarControllerKnown=ref(false);
const hardwareGpuSchedule=ref(false),gpuScheduleKnown=ref(false),gpuScheduleReadFailed=ref(false);
const gpuScheduleDescription=computed(()=>{
 const state=gpuScheduleReadFailed.value?'状态读取失败，请重新进入页面重试；':!gpuScheduleKnown.value?'正在读取系统状态；':`当前设置：${hardwareGpuSchedule.value?'开启':'关闭'}；`;
 return state+'通常建议开启，AMD Ryzen AI Max 385 / Max+ 388、395 建议关闭';
});
const tasks=reactive({energyStar:false,cleanMem:false});type TaskKey=keyof typeof tasks;
const STEAMCOMMUNITY_TASK=NETWORK_ACCELERATION_BOOT_TASK;
const searchSt=ref<'hidden'|'trimmed'|'full'>('full');
const busy=ref(false),errMsg=ref(''),taskToolsBusy=ref(false),taskToolsMsg=ref('');
let fanBootSettingRevision=0,desktopBootSettingRevision=0,networkBootSettingRevision=0,steamOverlayBootRevision=0,gameBarControllerRevision=0,refreshRevision=0;
async function safeExists(name:string){return taskExists(name).catch(()=>false);}
// Read-only presentation demand. Writes and their readbacks remain independent.
let refreshActive=false,refreshStopped=false,refreshDirty=false;
let refreshFlight:Promise<void>|null=null;
let refreshVisible=isUiVisible();
let stopRefreshVisibility:(()=>void)|undefined;
function canRefresh(){return !refreshStopped&&refreshActive&&isUiVisible();}
function drainRefresh():Promise<void>{
 if(refreshFlight)return refreshFlight;
 if(!refreshDirty||!canRefresh()||busy.value)return Promise.resolve();
 refreshFlight=Promise.resolve().then(async()=>{
  try{
   while(refreshDirty&&canRefresh()&&!busy.value){
    refreshDirty=false;
    await readRefresh();
   }
  }finally{
   refreshFlight=null;
   if(refreshDirty&&canRefresh()&&!busy.value)void drainRefresh();
  }
 });
 return refreshFlight;
}
function refresh():Promise<void>{
 if(refreshStopped)return Promise.resolve();
 ++refreshRevision;
 refreshDirty=true;
 return drainRefresh();
}
watch(busy,value=>{
 if(value){
  ++refreshRevision;
  if(refreshFlight)refreshDirty=true;
 }else void drainRefresh();
},{flush:'sync'});
async function readRefresh(){
 const revision=++refreshRevision,fanRevision=fanBootSettingRevision,desktopRevision=desktopBootSettingRevision,networkRevision=networkBootSettingRevision,overlayRevision=steamOverlayBootRevision,gameBarRevision=gameBarControllerRevision;
 const results=await Promise.allSettled([safeExists('节能-能源之星'),safeExists('内存-开机自动内存清理并关闭'),safeExists(STEAMCOMMUNITY_TASK),searchState(),readBootMirrorState(),readBootRtssState(),readSettingsSection<{fanControl?:boolean;steamOverlayOnBoot?:boolean}>('startupDesired'),readHardwareGpuSchedule(),safeExists(DESKTOP_MODE_BOOT_TASK),readGameBarControllerEnabled()]);
 if(revision!==refreshRevision||!canRefresh()||busy.value)return;
 const [energy,memory,community,search,boot,rtss,fan,gpu,desktop,gameBar]=results;
 if(energy.status==='fulfilled')tasks.energyStar=energy.value;if(memory.status==='fulfilled')tasks.cleanMem=memory.value;
 if(networkRevision===networkBootSettingRevision&&community.status==='fulfilled')steamCommunityBoot.value=community.value;if(search.status==='fulfilled')searchSt.value=search.value;
 if(boot.status==='fulfilled')bootOn.value=boot.value;if(rtss.status==='fulfilled')rtssBootOn.value=rtss.value;
 if(fanRevision===fanBootSettingRevision&&fan.status==='fulfilled')fanControlOnBoot.value=fan.value.fanControl===true;
 if(overlayRevision===steamOverlayBootRevision&&fan.status==='fulfilled')steamOverlayOnBoot.value=fan.value.steamOverlayOnBoot===undefined||fan.value.steamOverlayOnBoot===true;
 if(gpu.status==='fulfilled'){hardwareGpuSchedule.value=gpu.value;gpuScheduleKnown.value=true;gpuScheduleReadFailed.value=false;}
 else{gpuScheduleKnown.value=false;gpuScheduleReadFailed.value=true;}
 if(desktopRevision===desktopBootSettingRevision&&desktop.status==='fulfilled')desktopBootOn.value=desktop.value;
 if(gameBarRevision===gameBarControllerRevision){gameBarControllerKnown.value=gameBar.status==='fulfilled';if(gameBar.status==='fulfilled')gameBarControllerOn.value=gameBar.value;}
 if(results.some(result=>result.status==='rejected'))errMsg.value='部分通用启动状态未能读取，可收回后重新打开。';
}
async function onDesktopBootToggle(v:boolean) {
 errMsg.value='';busy.value=true;++desktopBootSettingRevision;const previous=desktopBootOn.value;
 try{await setDesktopModeStartup(v);desktopBootOn.value=v;}
 catch(error){desktopBootOn.value=previous;errMsg.value='桌面模式开机设置失败：'+(error as Error).message;}
 finally{busy.value=false;}
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

async function onSteamOverlayBootToggle(v:boolean):Promise<void>{
 if(busy.value)return;
 ++steamOverlayBootRevision;errMsg.value='';busy.value=true;
 const previous=steamOverlayOnBoot.value;steamOverlayOnBoot.value=v;
 try{await saveSettingsSection('startupDesired',{steamOverlayOnBoot:v});}
 catch(error){steamOverlayOnBoot.value=previous;errMsg.value='Steam 游戏中界面开机设置失败：'+(error as Error).message;}
 finally{busy.value=false;}
}
async function onGameBarControllerToggle(v:boolean):Promise<void>{
 if(busy.value)return;
 ++gameBarControllerRevision;errMsg.value='';busy.value=true;
 try{await setGameBarControllerEnabled(v);gameBarControllerOn.value=await readGameBarControllerEnabled();gameBarControllerKnown.value=true;}
 catch(error){
  errMsg.value='Game Bar 手柄入口设置失败：'+(error as Error).message;
  try{gameBarControllerOn.value=await readGameBarControllerEnabled();gameBarControllerKnown.value=true;}
  catch{gameBarControllerKnown.value=false;}
 }finally{busy.value=false;}
}
function onGameBarControllerChanged(event:Event):void{
 const enabled=(event as CustomEvent<{enabled?:unknown}>).detail?.enabled;
 if(typeof enabled!=='boolean')return;
 ++gameBarControllerRevision;gameBarControllerOn.value=enabled;gameBarControllerKnown.value=true;
}
async function onFanControlBootToggle(v: boolean): Promise<void> {
  if (busy.value) return;
  fanBootSettingRevision += 1;
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

async function onHardwareGpuSchedule(v: boolean) {
  if (busy.value || !gpuScheduleKnown.value) return;
  ++refreshRevision;
  busy.value = true;
  errMsg.value = '';
  const prev = hardwareGpuSchedule.value;
  hardwareGpuSchedule.value = v;
  try {
    const ok = await writeHardwareGpuSchedule(v);
    if (!ok) throw new Error('注册表写入或回读验证失败');
    errMsg.value = '硬件加速GPU计划已更新，重启电脑后生效';
  } catch (e) {
    hardwareGpuSchedule.value = prev;
    errMsg.value = '硬件加速GPU计划设置失败：' + (e as Error).message;
  } finally {
    ++refreshRevision;
    busy.value = false;
  }
}

async function onSteamCommunityBoot(v: boolean) {
  ++networkBootSettingRevision;
  errMsg.value = '';
  const prev = steamCommunityBoot.value;
  steamCommunityBoot.value = v;
  busy.value = true;
  try {
    await setNetworkAccelerationStartup(v);
    steamCommunityBoot.value = v;
  } catch (e) {
    steamCommunityBoot.value = prev;
    errMsg.value = 'Steamcommunity_302 开机启动设置失败：' + (e as Error).message;
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

async function onSearch() {
  try {
    await openSearchFolder();
  } catch {
    /* ignore */
  }
}
const globalRefreshKey=inject<Ref<number>>('globalRefreshKey');if(globalRefreshKey)watch(globalRefreshKey,()=>{void refresh();});
async function onBootMirrorChanged(){bootOn.value=await readBootMirrorState().catch(()=>bootOn.value);}
function onDesktopModeBootChanged(event:Event){const value=(event as CustomEvent<{enabled:boolean}>).detail?.enabled;if(typeof value==='boolean'){++desktopBootSettingRevision;desktopBootOn.value=value;}}
function onNetworkAccelerationBootChanged(event:Event){const value=(event as CustomEvent<{enabled:boolean}>).detail?.enabled;if(typeof value==='boolean'){++networkBootSettingRevision;steamCommunityBoot.value=value;}}
onMounted(()=>{
 stopRefreshVisibility=onUiVisibilityChange(state=>{
  if(refreshVisible===state.visible)return;
  refreshVisible=state.visible;
  void refresh();
 });
 window.addEventListener(BOOT_MIRROR_CHANGED_EVENT,onBootMirrorChanged);window.addEventListener(DESKTOP_MODE_BOOT_CHANGED_EVENT,onDesktopModeBootChanged);window.addEventListener(NETWORK_ACCELERATION_BOOT_CHANGED_EVENT,onNetworkAccelerationBootChanged);window.addEventListener(GAME_BAR_CONTROLLER_CHANGED_EVENT,onGameBarControllerChanged);});
onUnmounted(()=>{refreshStopped=true;refreshActive=false;refreshDirty=false;refreshRevision++;stopRefreshVisibility?.();window.removeEventListener(BOOT_MIRROR_CHANGED_EVENT,onBootMirrorChanged);window.removeEventListener(DESKTOP_MODE_BOOT_CHANGED_EVENT,onDesktopModeBootChanged);window.removeEventListener(NETWORK_ACCELERATION_BOOT_CHANGED_EVENT,onNetworkAccelerationBootChanged);window.removeEventListener(GAME_BAR_CONTROLLER_CHANGED_EVENT,onGameBarControllerChanged);});
onActivated(()=>{if(refreshStopped||refreshActive)return;refreshActive=true;void refresh();});
onDeactivated(()=>{refreshActive=false;refreshRevision++;refreshDirty=true;});
</script>

<template>
<IntegratedPage class="boot-startup-page" title="开机模式" hide-heading><div class="page startup-sections" data-gp-scope="power-startup"><BootModesPanel/><section class="boot-common-startup"><header class="boot-common-heading"><h2>通用开机启动</h2></header><div v-if="errMsg" class="err-bar">{{errMsg}}</div><div v-if="taskToolsMsg" class="info-bar">{{taskToolsMsg}}</div>
 <section class="settings-section-panel" aria-label="启动服务" data-boot-startup-group="services"><div class="startup-setting-list"><Toggle v-model="bootOn" label="YeManCC野蛮系统控制中心" description="开机启动此程序，推荐开启" :disabled="busy" @update:model-value="onBootToggle"/><Toggle :model-value="desktopBootOn" label="全局桌面模式" description="开启后解决桌面模式显示异常，推荐开启" :disabled="busy" @update:model-value="onDesktopBootToggle"/><Toggle :model-value="steamOverlayOnBoot" label="游戏中启用 Steam 界面" description="三点菜单呼出必备；关闭后不显示 SteamOS 界面，推荐开启" :disabled="busy" @update:model-value="onSteamOverlayBootToggle"/><Toggle :model-value="gameBarControllerOn" label="允许手柄打开 Game Bar" description="开启后可能导致 Steam 右下角弹出 Xbox 控制器错误提示，推荐关闭" :disabled="busy || !gameBarControllerKnown" @update:model-value="onGameBarControllerToggle"/><Toggle
        :model-value="fanControlOnBoot"
        label="风扇控制"
        description="开机应用风扇挡位并启动"
        color="accent"
        :disabled="busy"
        @update:model-value="onFanControlBootToggle"
      />
<Toggle
        v-model="rtssBootOn"
        label="RTSS 监控"
        description="帧率锁定需要，推荐开启"
        color="accent"
        :disabled="busy"
        @update:model-value="onBootRtssToggle"
      />
<Toggle v-model="tasks.energyStar" label="能源之星" description="开机为软件启用节能标识，推荐开启" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('节能-能源之星', v, 'energyStar')" />
<Toggle v-model="tasks.cleanMem" label="清理一次内存" description="开机执行一次内存清理，推荐开启" color="accent" :disabled="busy" @update:model-value="(v: boolean) => toggleTaskSafe('内存-开机自动内存清理并关闭', v, 'cleanMem')" />

<Toggle :model-value="steamCommunityBoot" label="网络加速" description="后台运行 Steam 网络加速，可能影响其他网络加速软件" color="accent" :disabled="busy" @update:model-value="onSteamCommunityBoot" />
<Toggle class="gpu-schedule-setting" :model-value="hardwareGpuSchedule" label="硬件加速GPU计划" :description="gpuScheduleDescription" color="accent" :disabled="busy || !gpuScheduleKnown" @update:model-value="onHardwareGpuSchedule" /></div></section>
 <section class="settings-section-panel" aria-label="系统工具" data-boot-startup-group="tools"><header class="settings-section-heading"><h3><InlineIcon name="tools"/>系统工具</h3></header><div class="startup-setting-list"><div class="row">
        <span><InlineIcon name="search" /> Windows 任务栏搜索</span>
        <button class="mini-btn" @click="onSearch">{{searchSt==='hidden'?'不可用':searchSt==='trimmed'?'恢复搜索':'精简搜索'}}</button>
      </div></div><h4 class="startup-tools-title">任务计划维护</h4><div class="task-tools" aria-label="任务计划批量管理">
        <button class="task-tool-btn" :disabled="taskToolsBusy || busy" @click="openBootTaskTool">打开启动管理</button>
        <button class="task-tool-btn danger" :disabled="taskToolsBusy || busy" @click="closeAllYemanTasks">关闭全部任务</button>
        <button class="task-tool-btn" :disabled="taskToolsBusy || busy" @click="restoreAllTasks">恢复全部任务</button>
      </div></section>
 </section></div></IntegratedPage>
</template>

<style scoped>
/* Async read receipts must not anchor the shared scrollport to lower startup controls. */
.boot-startup-page{overflow-anchor:none}

.boot-common-heading{display:flex;align-items:center;margin:6px 0 11px}.boot-common-heading h2{font-size:17px;margin:0}
.boot-common-startup{margin-top:14px!important;padding-top:12px!important}.settings-section-panel{min-width:0;margin:0 0 10px;padding:10px;border:1px solid var(--border);border-radius:9px;background:var(--console-surface,var(--bg-panel))}.settings-section-heading{padding-bottom:8px;margin-bottom:4px;border-bottom:1px solid var(--border)}.settings-section-heading h3{display:flex;align-items:center;gap:7px;font-size:16px;margin:0;color:var(--text)}.settings-section-heading :deep(.inline-icon){color:var(--accent)}
.boot-common-startup :deep(.toggle-desc){display:block;font-size:12px;line-height:1.5;margin-top:3px;color:var(--text-dim)}
.boot-common-startup :deep(.startup-setting-list>.toggle-row){padding:9px 0;min-width:0;gap:8px}.boot-common-startup .task-tools{grid-template-columns:repeat(3,minmax(0,1fr));gap:6px}.boot-common-startup .task-tool-btn{text-align:center;font-size:14px;padding:7px 5px}.boot-common-startup .startup-tools-title{font-size:14px;margin:10px 0 6px}.boot-common-startup .row{display:flex;justify-content:space-between;gap:12px}.boot-common-startup .mini-btn{font-size:14px;white-space:nowrap;min-width:110px;text-align:center}

.boot-common-startup{margin-top:8px;border-top:1px solid var(--border);padding-top:10px}.boot-common-startup>summary{cursor:pointer;font-size:13px;color:var(--text-dim);padding:8px 0}

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

<style>
.startup-sections .startup-setting-list>.toggle-row{padding:11px 0!important;margin:0!important;border-bottom:1px solid rgba(146,169,198,.11);border-radius:0!important;background:transparent!important}.startup-sections .startup-setting-list>.toggle-row:last-child{border-bottom:0}.startup-sections .toggle-row .toggle-label{font-size:14px!important}.startup-sections .toggle-row .toggle-desc{font-size:12px!important;line-height:1.5!important}
.startup-sections .startup-input-row{padding:14px 0;border-bottom:1px solid rgba(146,169,198,.12)}.startup-sections .startup-input-row>span{font-size:14px}.startup-sections .startup-input-row .dd{max-width:62%;width:190px;flex-basis:190px}.startup-sections .startup-input-row .dd-trigger{min-height:40px!important}
.startup-input-banner{display:flex;align-items:center;gap:12px;padding:18px 0 12px;color:var(--accent)}.startup-input-banner>svg{width:34px;height:34px}.startup-input-banner b{font-size:14px}.startup-input-banner p{font-size:12px;color:var(--text-dim);margin:4px 0 0;line-height:1.6}
.startup-tools-title{font-size:14px;margin:18px 0 10px;color:var(--text)}.startup-sections .task-tools{grid-template-columns:1fr;gap:8px}.startup-sections .task-tool-btn{font-size:13px;min-height:39px;text-align:left;padding:8px 12px}.startup-sections .row{padding:12px 0;margin:0;font-size:14px}.startup-sections .mini-btn{font-size:13px;min-height:36px}

</style>
