<script setup lang="ts">
import {computed,nextTick,onBeforeUnmount,onMounted,ref} from 'vue';
import AppIcon from './AppIcon.vue';
import {dialog,keyboard} from '@/bridge/api';
import {getMouseModeState,setMouseModePolicy} from '@/bridge/gameproc';
import {mouseModeState,acceptMouseModeState} from '@/bridge/mouseModeState';
import {mouseModeText,type MousePolicyMode} from '@/bridge/mouseModePresentation';
import {getMouseAutoRules,setMouseAutoRuleList,type MouseAutoRules,type MouseRuleKind} from '@/bridge/mouseAutoRules';
import {focusGamepadElement} from '@/gamepad/focus';
const props=defineProps<{steamDeckActive?:boolean;disabled?:boolean}>();
const modes=[{value:'off',label:'关闭'},{value:'auto',label:'自动'},{value:'on',label:'开启'}] as const;
const lists=[{kind:'blacklist',label:'鼠标黑名单',hint:'对焦这些程序时关闭鼠标'},{kind:'whitelist',label:'鼠标白名单',hint:'对焦这些程序时开启鼠标'}] as const;
const rules=ref<MouseAutoRules>({blacklist:[],whitelist:[]});
const loaded=ref(false),busy=ref(false),error=ref('');
const editor=ref<MouseRuleKind|null>(null),draft=ref('');
const root=ref<HTMLElement|null>(null);
let alive=true;
const blocked=computed(()=>props.disabled||props.steamDeckActive||mouseModeState.value?.provider==='steam'||mouseModeState.value?.controlAllowed!==true||busy.value);
const presentation=computed(()=>mouseModeText(mouseModeState.value));
async function refresh(){
  error.value='';
  const state=await getMouseModeState().catch(e=>{if(alive)error.value=String(e.message||e);return null;});
  if(!alive)return;if(state)acceptMouseModeState(state);
  try{const value=await getMouseAutoRules();if(alive){rules.value=value;loaded.value=true;}}
  catch(e){if(alive){loaded.value=false;error.value=(e as Error).message;}}
}
async function selectMode(mode:MousePolicyMode){
  if(blocked.value)return;busy.value=true;error.value='';
  try{const state=await setMouseModePolicy(mode);if(acceptMouseModeState(state))window.dispatchEvent(new CustomEvent('gp:mouse-mode',{detail:state}));if(!state.ok)throw Error(state.error||'鼠标挡位切换失败');}
  catch(e){if(alive)error.value=(e as Error).message;}finally{busy.value=false;}
}
function focusAdd(kind:MouseRuleKind){void nextTick(()=>focusGamepadElement(root.value?.querySelector<HTMLElement>(`[data-mouse-add="${kind}"]`)??null));}
function closeEditor(){const kind=editor.value;editor.value=null;draft.value='';if(kind)focusAdd(kind);}
function openEditor(kind:MouseRuleKind){editor.value=kind;draft.value='';void nextTick(()=>focusGamepadElement(root.value?.querySelector<HTMLElement>('.mouse-rule-editor input')??null));}
async function save(kind:MouseRuleKind,values:string[]){
  if(blocked.value||!loaded.value)return false;busy.value=true;error.value='';
  try{const value=await setMouseAutoRuleList(kind,values);if(alive)rules.value=value;return true;}
  catch(e){if(alive)error.value=(e as Error).message;return false;}finally{busy.value=false;}
}
async function add(path=draft.value){
  const kind=editor.value;if(!kind)return;
  const file=path.trim().split(/[\\/]/).pop()?.toLowerCase()??'';
  if(!file.endsWith('.exe')||/[<>:"|?*\x00-\x1f]/.test(file)||file.length>200||file.length<=4){error.value='请输入完整进程名，例如 program.exe';return;}
  if(await save(kind,[...new Set([...rules.value[kind],file])]))closeEditor();
}
async function remove(kind:MouseRuleKind,file:string){if(await save(kind,rules.value[kind].filter(v=>v!==file)))focusAdd(kind);}
async function choose(){
  if(blocked.value)return;
  try{const path=await dialog.openFile([{name:'可执行程序',extensions:['exe']}]);if(path&&alive)await add(path);}
  catch(e){if(alive)error.value=(e as Error).message;}
}
function pageCommand(event:Event){if((event as CustomEvent<{action:string}>).detail?.action==='back')back(event);}
function back(event:Event){if(!editor.value||event.defaultPrevented||!root.value?.isConnected||!root.value.getClientRects().length||document.querySelector('.dd-open,[data-gp-modal]'))return;event.preventDefault();closeEditor();}
onMounted(()=>{void refresh();window.addEventListener('ipc:gamepad-back',back);window.addEventListener('gp:page-command',pageCommand);});
onBeforeUnmount(()=>{alive=false;window.removeEventListener('ipc:gamepad-back',back);window.removeEventListener('gp:page-command',pageCommand);});
</script>
<template>
  <div ref="root" class="mouse-settings" aria-label="鼠标模式设置">
    <h3 class="card-title mouse-settings-title"><AppIcon name="mouse-solid"/>鼠标模式</h3>
    <div class="mouse-policy-modes" role="group" aria-label="鼠标模式" data-gp-group="mouse-policy">
      <button v-for="mode in modes" :key="mode.value" type="button" :data-mouse-policy="mode.value" :aria-pressed="mouseModeState?.mode===mode.value" :disabled="blocked" @click="selectMode(mode.value)">{{mode.label}}</button>
    </div>
    <p class="mouse-settings-hint" role="status">{{steamDeckActive?'由 Steam 管理，挡位不可切换':presentation.title+' · '+presentation.detail}}</p>
    <slot name="backend" :busy="busy"/>
    <p class="mouse-settings-hint">自动、开启均遵循鼠标名单，YMCC 内暂停；开启挡允许游戏内使用，模拟器和游戏平台仍按名单处理。</p>
    <section v-for="list in lists" :key="list.kind" class="mouse-rule-list" :data-mouse-rule-list="list.kind">
      <div class="mouse-rule-heading"><h4>{{list.label}} <small>{{rules[list.kind].length}}</small></h4><button type="button" :data-mouse-add="list.kind" :disabled="blocked||!loaded" :aria-expanded="editor===list.kind" @click="editor===list.kind?closeEditor():openEditor(list.kind)">{{editor===list.kind?'收起':'添加程序'}}</button></div>
      <p class="mouse-settings-hint">{{list.hint}}，覆盖系统判断。</p>
      <ul v-if="rules[list.kind].length"><li v-for="file in rules[list.kind]" :key="file"><span>{{file}}</span><button type="button" :disabled="blocked" :aria-label="'移除 '+file" @click="remove(list.kind,file)">移除</button></li></ul>
      <p v-else class="mouse-rule-empty">{{loaded?'未添加程序，沿用系统规则':'名单尚未读取'}}</p>
      <form v-if="editor===list.kind" class="mouse-rule-editor" @submit.prevent="add()" @keydown.esc.prevent="closeEditor">
        <label :for="'mouse-rule-'+list.kind">进程名</label><input :id="'mouse-rule-'+list.kind" v-model="draft" :disabled="blocked" placeholder="program.exe" autocomplete="off" spellcheck="false"/>
        <div class="mouse-rule-actions"><button type="button" :disabled="blocked" @click="choose">选择 EXE</button><button type="button" :disabled="blocked" @click="keyboard.open().catch(()=>{})">键盘</button><button type="submit" :disabled="blocked||!draft.trim()">保存</button></div>
      </form>
    </section>
    <p class="mouse-settings-hint">同一程序只保留在一份名单中；添加到另一份时自动移出原名单。</p>
    <p v-if="error" role="alert" class="mouse-settings-error">{{error}} <button v-if="!loaded" type="button" :disabled="busy" @click="refresh">重新读取</button></p>
  </div>
</template>
<style scoped>
.mouse-settings{display:flex;flex-direction:column;gap:10px;min-width:0}
.mouse-settings-title{display:flex;align-items:center;gap:7px;margin:0}
.mouse-settings-title .app-icon{color:#eef0f2;font-size:21px}
.mouse-policy-modes{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:7px}
.mouse-settings button{min-height:34px;border:1px solid var(--console-line,#2a3342);border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text);padding:6px 10px;font:inherit;font-size:12px;cursor:pointer}
.mouse-policy-modes button[aria-pressed=true]{background:var(--accent-surface);border-color:var(--accent-border);color:var(--accent)}
.mouse-settings button:disabled{opacity:.45;cursor:default}
.mouse-settings button:focus-visible,.mouse-settings input:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.mouse-settings-hint{margin:0;color:var(--text-dim);font-size:11px;line-height:1.6}
.mouse-rule-list{border:1px solid var(--console-line,#2a3342);border-radius:var(--radius-ctrl);padding:10px;min-width:0}
.mouse-rule-heading{display:flex;align-items:center;justify-content:space-between;gap:8px}
.mouse-rule-heading h4{margin:0;font-size:13px}.mouse-rule-heading small{font-size:11px;color:var(--text-dim);margin-left:4px}
.mouse-rule-list ul{list-style:none;padding:0;margin:7px 0 0;display:flex;flex-direction:column;gap:5px}
.mouse-rule-list li{display:flex;align-items:center;gap:8px}.mouse-rule-list li span{flex:1;min-width:0;overflow-wrap:anywhere;font-size:12px}
.mouse-rule-empty{margin:8px 0 0;color:var(--text-dim);font-size:11px}
.mouse-rule-editor{display:flex;flex-direction:column;gap:7px;margin-top:9px;padding-top:9px;border-top:1px solid var(--console-line,#2a3342)}
.mouse-rule-editor label{font-size:11px;color:var(--text-dim)}
.mouse-rule-editor input{min-width:0;max-width:100%;width:100%;box-sizing:border-box;border:1px solid var(--console-line,#2a3342);border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text);padding:8px;font-size:13px}
.mouse-rule-actions{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px}
.mouse-settings-error{margin:0;color:var(--danger);font-size:12px;overflow-wrap:anywhere}
</style>
