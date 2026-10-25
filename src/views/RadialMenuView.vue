<script setup lang="ts">
import { nextTick, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import IntegratedPage from '@/components/IntegratedPage.vue';
import Slider from '@/components/Slider.vue';
import RadialEditorWindow from '@/components/RadialEditorWindow.vue';
import ControllerShortcutEditorView from '@/views/ControllerShortcutEditorView.vue';
import { defaultRadialSettings } from '@/radial/model';
import { loadRadialSettings, saveRadialSettings } from '@/radial/settings';
import { invoke, isNativeRuntime, on as onNative } from '@/bridge/ipc';
import { focusGamepadElement } from '@/gamepad/focus';
const settings=ref(defaultRadialSettings()),editorOpen=ref(false),recordOpen=ref(false),busy=ref(false),status=ref(''),holdDraft=ref(650),page=ref<HTMLElement|null>(null);
let alive=true,statusTicket=0;let stopTouchStatus:(()=>void)|undefined;
const touchStatus=ref('');
interface TouchSourceStatus {fullScreenRequested?:boolean;fullScreenInstalled?:boolean;rawObservedDevices?:number}
function showTouchStatus(source:TouchSourceStatus){
  if(!alive||!settings.value.fullScreenHold)return;
  if(source.fullScreenRequested===false)touchStatus.value='全屏长按正在准备…';
  else if(source.fullScreenInstalled===false)touchStatus.value='全屏长按尚未就绪，可使用监控长按或呼出按键。';
  else if((source.rawObservedDevices??0)>0)touchStatus.value='已检测到触摸屏输入，请长按屏幕验证呼出。';
  else if(source.fullScreenInstalled===true)touchStatus.value='等待触摸验证；长按无响应时可使用监控或呼出按键。';
}
async function readTouchStatus(){
  if(!isNativeRuntime||!settings.value.fullScreenHold)return;
  const ticket=++statusTicket,revision=settings.value.revision;
  try{const source=await invoke<TouchSourceStatus>('radial.status',{});
    if(!alive||ticket!==statusTicket||revision!==settings.value.revision||!settings.value.fullScreenHold||busy.value)return;
    showTouchStatus(source);
  }catch{}
}
async function load(){++statusTicket;try{settings.value=await loadRadialSettings();holdDraft.value=settings.value.holdMs;void readTouchStatus();}catch(e){status.value=(e as Error).message;}}
async function save(){if(busy.value)return;busy.value=true;try{settings.value=await saveRadialSettings(settings.value.revision,settings.value);status.value='已保存。';}catch(e){status.value=(e as Error).message;await load();}finally{busy.value=false;}}
async function toggle(key:'enabled'|'monitorHold'|'feedback'|'fullScreenHold'){settings.value[key]=!settings.value[key];await save();}
async function applyHold(){settings.value.holdMs=holdDraft.value;await save();}
async function preview(){if(!isNativeRuntime){status.value='使用端可在主线测试窗口中呼出。';return;}try{const result=await invoke<{ok:boolean;reason?:string}>('radial.request',{source:'settings'});if(!result.ok)throw new Error(result.reason||'呼出失败');}catch(e){status.value=(e as Error).message;}}
async function closed(){editorOpen.value=false;recordOpen.value=false;await load();await nextTick();focusGamepadElement(page.value?.querySelector<HTMLElement>('[data-radial-edit]')??null);}
onMounted(()=>{stopTouchStatus=onNative('radial.source-status',showTouchStatus);void load();});onActivated(load);onBeforeUnmount(()=>{alive=false;++statusTicket;stopTouchStatus?.();});onDeactivated(()=>{editorOpen.value=false;recordOpen.value=false;});
</script>
<template><IntegratedPage title="触摸轮盘" hide-heading><div ref="page" class="radial-settings-page" :inert="editorOpen||recordOpen||undefined"><header><h2>触摸轮盘</h2><p>长按监控，松手呼出；手柄、触摸与快捷键共用一个菜单</p></header>
  <section class="radial-settings-card"><div class="setting-row"><div><h3>启用一体化轮盘</h3><p>保留监控位置，关闭后返回监控</p></div><button :disabled="busy" :aria-pressed="settings.enabled" @click="toggle('enabled')">{{settings.enabled?'已开启':'已关闭'}}</button></div><div class="setting-row"><div><h3>监控长按呼出</h3><p>按住达到时长后松手呼出；拖动可取消呼出并移动监控</p></div><button :disabled="busy" :aria-pressed="settings.monitorHold" @click="toggle('monitorHold')">{{settings.monitorHold?'已开启':'已关闭'}}</button></div><div data-gp-slider-draft @gp:slider-edit-cancel="holdDraft=settings.holdMs"><Slider v-model="holdDraft" :min="400" :max="1500" :step="50" label="长按时长" aria-label="长按呼出时长" unit="ms" :disabled="busy"/></div><button :disabled="busy||holdDraft===settings.holdMs" @click="applyHold">应用长按时长</button><div class="setting-row"><div><h3>触觉反馈</h3><p>沿用触摸板滑动反馈和强度设置，与游戏反馈协调</p></div><button :disabled="busy" :aria-pressed="settings.feedback" @click="toggle('feedback')">{{settings.feedback?'已开启':'已关闭'}}</button></div></section>
  <section class="radial-settings-card"><h3>菜单与呼出</h3><div class="radial-entry-actions"><button data-radial-edit @click="editorOpen=true">编辑菜单 <span>8 个扇区 / 二级滑动</span></button><button @click="recordOpen=true">录制呼出按键 <span>键盘 / 手柄 / 单个背键</span></button><button @click="preview">呼出轮盘 <span>按键呼出位于 YMCC 对侧</span></button></div></section>
  <section class="radial-settings-card"><div class="setting-row"><div><h3>全屏触摸长按</h3><p>全屏长按呼出；轮盘显示在 YMCC 对侧，设备支持需实测</p></div><button :disabled="busy" :aria-pressed="settings.fullScreenHold" @click="toggle('fullScreenHold')">{{settings.fullScreenHold?'已开启':'已关闭'}}</button></div><p v-if="settings.fullScreenHold&&touchStatus" role="status">{{touchStatus}}</p></section><p role="status" class="radial-settings-status">{{status}}</p>
</div></IntegratedPage><RadialEditorWindow v-if="editorOpen" @close="closed"/><ControllerShortcutEditorView v-if="recordOpen" initial-action-id="radial.summon" @close="closed"/></template>
<style scoped>
.radial-settings-page{padding:16px;display:flex;flex-direction:column;gap:14px;color:var(--text);font-size:13px}.radial-settings-page header h2{font-size:20px;font-weight:600;margin:0 0 8px}.radial-settings-page p{font-size:12px;color:var(--text-dim);line-height:1.6;margin:5px 0 0}.radial-settings-card{padding:16px;background:var(--bg-card);border:1px solid var(--border);border-radius:10px}.radial-settings-card h3{font-size:13px;margin:0;font-weight:600}.setting-row{display:flex;gap:14px;align-items:center;justify-content:space-between;padding:9px 0}.setting-row+ .setting-row{border-top:1px solid var(--border);margin-top:8px;padding-top:15px}.radial-settings-page button{min-height:40px;padding:8px 16px;background:var(--bg-input);color:var(--text);border:1px solid var(--border);border-radius:7px;white-space:nowrap;font:600 12px inherit}.radial-settings-page button[aria-pressed=true]{color:var(--accent);border-color:color-mix(in srgb,var(--accent) 45%,transparent)}.radial-entry-actions{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin-top:14px}.radial-entry-actions button{min-height:60px;white-space:normal;text-align:left}.radial-entry-actions span{display:block;font-size:10px;font-weight:400;color:var(--text-dim);margin-top:6px}.radial-settings-page button.focused{box-shadow:var(--focus-ring)}.radial-settings-page button:disabled{opacity:.45}.radial-settings-status{min-height:20px}@media(max-width:560px){.radial-entry-actions{grid-template-columns:1fr}.radial-settings-page{padding:10px}.radial-settings-card{padding:12px}}
</style>
