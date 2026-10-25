<script setup lang="ts">
import { nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
import MonitorOutputSettings from './MonitorOutputSettings.vue';
import { windowApi } from '@/bridge/api';
import { isNativeRuntime } from '@/bridge/ipc';
import { focusGamepadElement } from '@/gamepad/focus';
const emit=defineEmits<{(event:'close'):void}>();
const modal=ref<HTMLElement|null>(null),editor=ref<InstanceType<typeof MonitorOutputSettings>|null>(null);
const closing=ref(false),closeError=ref('');
const owner=`monitor-editor-${Date.now()}-${Math.random().toString(36).slice(2)}`;
let alive=true,widthOwned=false;
async function widen(){
 if(!isNativeRuntime||/[?&]__ymccChild=\d+/.test(location.hash))return;
 try{const receipt=await windowApi.controllerEditorWidth(owner,true,2);if(!alive){if(receipt.ok)await windowApi.controllerEditorWidth(owner,false);return;}widthOwned=receipt.ok;}catch{/* The local editor remains usable. */}
}
async function releaseWidth(){if(widthOwned){widthOwned=false;await windowApi.controllerEditorWidth(owner,false).catch(()=>undefined);}}
async function close(){
 if(closing.value)return;closing.value=true;closeError.value='';
 try{await editor.value?.flushEdits();await releaseWidth();if(alive)emit('close');}
 catch{closeError.value='保存失败，请重试返回。';closing.value=false;}
}
function ownsBack(){const layers=[...document.querySelectorAll<HTMLElement>('[data-gp-modal]')].filter(el=>el.getBoundingClientRect().height>0);return layers[layers.length-1]===modal.value&&!modal.value?.querySelector('.dd-open,[aria-expanded="true"][aria-haspopup="listbox"]');}
function back(event:Event){if(event.defaultPrevented||!ownsBack())return;event.preventDefault();void close();}
function keydown(event:KeyboardEvent){if(event.key==='Escape')back(event);}
onMounted(async()=>{window.addEventListener('ipc:gamepad-back',back);window.addEventListener('keydown',keydown);void widen();await nextTick();focusGamepadElement(modal.value?.querySelector<HTMLElement>('.monitor-editor-close')??null);});
onBeforeUnmount(()=>{alive=false;window.removeEventListener('ipc:gamepad-back',back);window.removeEventListener('keydown',keydown);void releaseWidth();});
</script>
<template><Teleport to="body"><div ref="modal" class="monitor-editor-window" data-gp-modal="monitor-editor" data-gp-scope="monitor-editor" data-gp-spatial="nearest" role="dialog" aria-modal="true" aria-label="编辑自定义监控"><div class="monitor-editor-viewport" data-monitor-editor-viewport><MonitorOutputSettings ref="editor" @close="close"/><p v-if="closeError" class="monitor-editor-error" role="alert">{{closeError}}</p></div></div></Teleport></template>
<style scoped>
.monitor-editor-window{position:fixed;inset:0;z-index:1100;overflow:hidden;padding:12px;background:radial-gradient(ellipse at 10% 0%,color-mix(in srgb,var(--accent) 12%,transparent),transparent 60%),#0b111a;color:var(--text);font-family:'Segoe UI','Microsoft YaHei',system-ui,-apple-system,sans-serif;font-size:15px;box-sizing:border-box}.monitor-editor-viewport{width:100%;height:100%;min-width:0;min-height:0;overflow:hidden}.monitor-editor-error{position:absolute;bottom:12px;left:24px;color:var(--danger,#ff687d);background:var(--bg,#0b1019);padding:8px 12px}
</style>
