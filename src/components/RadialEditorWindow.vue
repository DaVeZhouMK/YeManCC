<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
import ControllerEditorGlobalActions from '@/components/ControllerEditorGlobalActions.vue';
import ControllerShortcutEditorView from '@/views/ControllerShortcutEditorView.vue';
import RadialWheel from '@/components/RadialWheel.vue';
import Dropdown from '@/components/Dropdown.vue';
import { windowApi } from '@/bridge/api';
import { isNativeRuntime } from '@/bridge/ipc';
import { focusGamepadElement } from '@/gamepad/focus';
import { ACTIONS, defaultRadialSettings, findRadialItem, type RadialSettings, type RadialItem, type RadialAction } from '@/radial/model';
import { loadRadialSettings, saveRadialSettings } from '@/radial/settings';
const emit=defineEmits<{close:[]}>();
const modal=ref<HTMLElement|null>(null),draft=ref<RadialSettings>(defaultRadialSettings()),baseline=ref(''),revision=ref(0),busy=ref(false),error=ref(''),recordOpen=ref(false),selected=ref('ymcc'),page=ref(0);
const dirty=computed(()=>JSON.stringify(draft.value)!==baseline.value),current=computed(()=>findRadialItem(draft.value,selected.value)),items=computed(()=>draft.value.pages[page.value]?.items??[]);
const owner=`radial-editor-${Date.now()}-${Math.random().toString(36).slice(2)}`;let alive=true,widthOwned=false;
let widthRequest:Promise<void>|null=null;
const recorder=ref<{releaseEditorWindowWidth:()=>Promise<void>}|null>(null);
function widen():Promise<void>{
  if(!isNativeRuntime)return Promise.resolve();
  if(widthRequest)return widthRequest;
  const task=(async()=>{try{const receipt=await windowApi.controllerEditorWidth(owner,true,2);if(!alive||recordOpen.value){if(receipt.ok)await windowApi.controllerEditorWidth(owner,false);return;}widthOwned=receipt.ok;}catch{}})();
  widthRequest=task;void task.finally(()=>{if(widthRequest===task)widthRequest=null;});return task;
}
async function releaseWidth(){await widthRequest;if(widthOwned){widthOwned=false;await windowApi.controllerEditorWidth(owner,false).catch(()=>undefined);}}
async function load(){busy.value=true;try{const settings=await loadRadialSettings();if(!alive)return;draft.value=settings;revision.value=settings.revision;baseline.value=JSON.stringify(settings);error.value='';}catch(e){error.value=(e as Error).message;}finally{busy.value=false;}}
async function apply(){if(busy.value)return;busy.value=true;try{const saved=await saveRadialSettings(revision.value,draft.value);if(!alive)return;draft.value=saved;revision.value=saved.revision;baseline.value=JSON.stringify(saved);error.value='已保存轮盘菜单。';}catch(e){error.value=(e as Error).message;}finally{busy.value=false;}}
function restore(){const settings=defaultRadialSettings();settings.revision=revision.value;draft.value=settings;selected.value='ymcc';page.value=0;}
function changeAction(value:string|number){const entry=current.value;if(!entry)return;entry.action=value as RadialAction;
  if(value==='submenu'&&!entry.children?.length)entry.children=[{id:`leaf-${Date.now()}`,label:'我的电脑',action:'windows.thisPc',enabled:true}];}
function parentOf(id:string){return items.value.find(i=>i.children?.some(c=>c.id===id));}
const actionOptions=computed(()=>parentOf(selected.value)?ACTIONS.filter(a=>a.value!=='submenu'&&a.value!=='performance'&&a.value!=='fan'&&a.value!=='input.pad'&&a.value!=='input.gyro'&&a.value!=='monitor.templates'&&a.value!=='steam.controls'):ACTIONS);
function addChild(){const entry=current.value;if(!entry||entry.action!=='submenu'||(entry.children?.length??0)>=3)return;(entry.children??=[]).push({id:`leaf-${Date.now()}-${entry.children.length}`,label:'新动作',action:'ymcc.show',enabled:true});}
function removeChild(){const parent=parentOf(selected.value);if(!parent)return;parent.children=parent.children?.filter(c=>c.id!==selected.value);selected.value=parent.id;}
function swap(delta:number){const at=items.value.findIndex(i=>i.id===selected.value),to=at+delta;if(at<0||to<0||to>=items.value.length)return;const next=[...items.value];[next[at],next[to]]=[next[to],next[at]];draft.value.pages[page.value].items=next;}
async function close(){if(busy.value)return;await releaseWidth();if(alive)emit('close');}
let recordingTransition=false;
async function recording(){
  if(recordOpen.value||recordingTransition||!alive)return;
  recordingTransition=true;
  try{await releaseWidth();if(alive)recordOpen.value=true;}finally{recordingTransition=false;}
}
let recordingClosing=false;
async function recordingClosed(){
  if(!recordOpen.value||recordingClosing)return;
  recordingClosing=true;
  // Await an accepted but still unacknowledged child acquisition before
  // releasing it. Vue unmount alone cannot order a late native reply.
  await recorder.value?.releaseEditorWindowWidth();
  recordingClosing=false;if(!alive)return;
  recordOpen.value=false;
  // The recorder releases its native width owner during unmount. Preserve IPC
  // order before the parent reacquires the same single-owner window session.
  await nextTick();if(!alive)return;
  await widen();if(!alive)return;
  focusGamepadElement(modal.value?.querySelector<HTMLElement>('[data-radial-record]')??null);
}
function ownsBack(){const layers=[...document.querySelectorAll<HTMLElement>('[data-gp-modal]')].filter(el=>el.getClientRects().length);return !recordOpen.value&&layers[layers.length-1]===modal.value&&!document.querySelector('.dd-open,[aria-expanded="true"][aria-haspopup="listbox"]');}
function back(event:Event){if(event.defaultPrevented||!ownsBack())return;event.preventDefault();void close();}
function key(event:KeyboardEvent){if(event.key==='Escape')back(event);}
onMounted(async()=>{window.addEventListener('ipc:gamepad-back',back);window.addEventListener('keydown',key);void widen();await load();await nextTick();focusGamepadElement(modal.value?.querySelector<HTMLElement>('[data-radial-select]')??null);});
onBeforeUnmount(()=>{alive=false;window.removeEventListener('ipc:gamepad-back',back);window.removeEventListener('keydown',key);void releaseWidth();});
</script>
<template><Teleport to="body"><div ref="modal" class="radial-editor-window" data-gp-modal="radial-editor" data-gp-scope="radial-editor" data-gp-spatial="nearest" role="dialog" aria-modal="true" aria-label="编辑触摸轮盘" :inert="recordOpen||undefined">
  <header class="radial-editor-header"><div><h2>编辑触摸轮盘</h2><p>选择扇区，添加 YMCC 功能或 Windows 操作</p></div><ControllerEditorGlobalActions :dirty="dirty" :busy="busy" :recording="false" @restore="restore" @discard="load" @apply="apply" @close="close"/></header>
  <div class="radial-editor-body">
    <section class="radial-editor-list" aria-label="菜单动作"><h3>常用菜单 <span>8 个扇区</span></h3><button v-for="(entry,index) in items" :data-radial-select="entry.id" :key="entry.id" :class="{selected:entry.id===selected}" @click="selected=entry.id"><b>{{index+1}}</b><span>{{entry.label}}<small>{{ACTIONS.find(a=>a.value===entry.action)?.label}}</small></span><em v-if="entry.action==='submenu'">{{entry.children?.length??0}} 项</em></button><button class="record-entry" data-radial-record @click="recording">录制呼出按键</button></section>
    <section class="radial-editor-workbench" aria-label="菜单预览"><div class="workbench-label">{{draft.pages[page]?.name}}<span>选择扇区编辑</span></div><div class="wheel-stage"><RadialWheel :items="items" edit :selected-id="selected" @select="selected=$event"/></div><div class="workbench-footer">手柄从列表选择 · 触摸轮盘可直接编辑</div></section>
    <section class="radial-editor-properties" aria-label="动作设置" v-if="current"><h3>{{parentOf(current.id)?'二级动作':'扇区设置'}}</h3><label>显示名称<input v-model="current.label" maxlength="20" aria-label="显示名称" /></label><label>功能<Dropdown :model-value="current.action" :options="actionOptions" aria-label="扇区功能" @update:model-value="changeAction"/></label><button :aria-pressed="current.enabled" @click="current.enabled=!current.enabled">{{current.enabled?'已启用':'已停用'}}</button>
      <div v-if="current.action==='submenu'" class="child-list"><h4>二级滑动菜单 <span>{{current.children?.length??0}} / 3</span></h4><button v-for="child in current.children" :data-radial-select="child.id" :key="child.id" @click="selected=child.id">{{child.label}}<span>编辑 ›</span></button><button :disabled="(current.children?.length??0)>=3" @click="addChild">＋ 添加二级动作</button></div>
      <template v-if="!parentOf(current.id)"><div class="order-buttons"><button :disabled="items[0]?.id===current.id" @click="swap(-1)">向前移动</button><button :disabled="items[7]?.id===current.id" @click="swap(1)">向后移动</button></div></template><button v-else class="remove-child" @click="removeChild">移除二级动作</button>
      <p class="property-hint">{{current.action==='monitor.templates'?'切换原监控 A/B 外观，保留两套模板和当前监控开关。':current.action==='steam.controls'?'使用现有 Steam 启动与同实例切换、普通退出流程，按实际状态确认。':current.action==='input.pad'||current.action==='input.gyro'?'在全局或当前游戏范围内选择；沿用原输入流程，游戏首次选择会启用原专属配置。':current.action==='fan'?'从主环向外展开关闭、轻柔、均衡和强劲档位；沿用已保存曲线，等待 YMCC 原风扇控制确认。':current.action==='performance'?'按当前供电与游戏策略显示自动、手动模式及滑块；外延转盘确认后提交，返回丢弃草稿。':current.action==='tdp'?'TDP 在当前电源与游戏策略的限值内调节；确认后应用。':current.action==='submenu'?'从一级扇区向外滑动选择二级动作，向内退回。':'可通过手柄确认或触摸滑动选择。'}}</p>
    </section>
  </div><footer class="radial-editor-footer"><span role="status">{{error || (dirty?'有未应用的修改':'已保存的菜单')}} </span><span><b>A</b> 选择 / 编辑　<b class="back-key">B</b> 取消并关闭</span></footer>
</div><ControllerShortcutEditorView ref="recorder" v-if="recordOpen" initial-action-id="radial.summon" @close="recordingClosed"/></Teleport></template>
<style scoped>
.radial-editor-window{position:fixed;inset:0;z-index:1100;display:flex;flex-direction:column;gap:12px;padding:12px;box-sizing:border-box;background:radial-gradient(ellipse at 10% 0%,color-mix(in srgb,var(--accent) 10%,transparent),transparent 60%),#0b111a;color:var(--text);font:13px 'Segoe UI','Microsoft YaHei',sans-serif;overflow:hidden}.radial-editor-header{display:flex;align-items:center;justify-content:space-between;gap:16px;min-height:60px}.radial-editor-header h2{font-size:18px;margin:0 0 6px;font-weight:650}.radial-editor-header p{font-size:12px;color:var(--text-dim);margin:0}.radial-editor-body{display:grid;grid-template-columns:200px minmax(260px,1fr) 280px;gap:12px;flex:1;min-height:0}.radial-editor-list,.radial-editor-properties,.radial-editor-workbench{border:1px solid var(--border,#243143);border-radius:10px;background:var(--bg-card,#111b29);min-height:0;overflow:auto;padding:12px}.radial-editor-list h3,.radial-editor-properties h3{font-size:13px;margin:0 0 12px}.radial-editor-list h3 span{float:right;font-size:11px;font-weight:400;color:var(--text-dim)}.radial-editor-window button,.radial-editor-window input{font-family:inherit}.radial-editor-list button{display:flex;align-items:center;gap:10px;width:100%;min-height:48px;padding:6px 10px;margin-bottom:6px;text-align:left;border:1px solid transparent;border-radius:7px;background:var(--bg-input,#172333);color:var(--text);font-size:12px}.radial-editor-list button b{font-size:11px;color:var(--text-dim);font-weight:400}.radial-editor-list button span{flex:1}.radial-editor-list small{display:block;color:var(--text-dim);font-size:10px;margin-top:4px}.radial-editor-list em{font-size:10px;color:var(--accent);font-style:normal}.radial-editor-list button.selected{border-color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,var(--bg-input))}.radial-editor-list .record-entry{justify-content:center;text-align:center;margin-top:14px;color:var(--accent);min-height:40px}.radial-editor-workbench{position:relative;display:flex;flex-direction:column;overflow:hidden;padding:12px;background:radial-gradient(ellipse at center,#152a4050,transparent 75%),var(--bg-card,#111b29)}.workbench-label{display:flex;justify-content:space-between;color:var(--text);font-size:12px}.workbench-label span{color:var(--text-dim);font-size:11px}.wheel-stage{flex:1;min-height:0;display:grid;place-items:center;overflow:hidden}.wheel-stage :deep(.radial-wheel){width:min(100%,496px);max-height:100%}.workbench-footer{text-align:center;font-size:11px;color:var(--text-dim);padding:8px 0}.radial-editor-properties label{display:grid;gap:8px;margin:14px 0;font-size:12px;color:var(--text-dim)}.radial-editor-properties input{box-sizing:border-box;width:100%;height:40px;padding:0 10px;background:var(--bg-input);border:1px solid var(--border,#273548);border-radius:7px;color:var(--text);font-size:13px}.radial-editor-properties button{min-height:40px;padding:8px 12px;background:var(--bg-input);border:1px solid var(--border,#273548);border-radius:7px;color:var(--text);font-size:12px}.radial-editor-properties>button{width:100%}.child-list{margin:20px 0 12px}.child-list h4{font-size:12px;font-weight:500}.child-list h4 span{float:right;color:var(--text-dim)}.child-list button{display:flex;justify-content:space-between;width:100%;margin:6px 0}.child-list button span{font-size:11px;color:var(--text-dim)}.order-buttons{display:flex;gap:8px;margin-top:18px}.order-buttons button{flex:1;min-width:0}.property-hint{font-size:11px;line-height:1.8;color:var(--text-dim);margin-top:20px}.radial-editor-window button:disabled{opacity:.4}.radial-editor-window button.focused,.radial-editor-window input.focused{box-shadow:var(--focus-ring)}.radial-editor-footer{display:flex;justify-content:space-between;gap:10px;min-height:28px;align-items:center;font-size:11px;color:var(--text-dim)}.radial-editor-footer b{color:var(--accent)}.radial-editor-footer .back-key{color:var(--danger)}
@media(max-width:960px){.radial-editor-body{grid-template-columns:164px minmax(220px,1fr) 240px}.radial-editor-header{flex-wrap:wrap}.radial-editor-header p{display:none}}@media(max-width:700px){.radial-editor-window{overflow:auto}.radial-editor-body{grid-template-columns:minmax(0,1fr);overflow:auto;display:flex;flex-direction:column}.radial-editor-list,.radial-editor-properties{overflow:visible;flex:none}.radial-editor-list{display:flex;flex-wrap:wrap;gap:6px}.radial-editor-list h3{width:100%}.radial-editor-list button{width:calc(50% - 3px);margin:0}.radial-editor-workbench{flex:none;min-height:340px}.radial-editor-list .record-entry{width:100%}.radial-editor-footer{flex-wrap:wrap}.wheel-stage :deep(.radial-wheel){max-width:340px}}
</style>
