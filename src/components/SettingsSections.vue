<script setup lang="ts">
import {ref,watch,onMounted,onActivated,onDeactivated,onBeforeUnmount} from 'vue';
import SectionIcon from './SettingsSectionIcon.vue';
import {focusGamepadElement} from '@/gamepad/focus';
export interface SettingsSection { id:string; title:string; description:string; icon:string }
const props=defineProps<{sections:SettingsSection[];label:string;layoutVariant?:'a'|'b'|'c';shareNavigationScope?:boolean}>();
const selected=ref(props.layoutVariant?'appearance':props.sections[0]?.id||'');const root=ref<HTMLElement|null>(null);
watch(()=>props.sections,items=>{if(!items.some(item=>item.id===selected.value))selected.value=items[0]?.id||'';});
function backToSection(event:Event){
 if(event.defaultPrevented||[...document.querySelectorAll('.dd-open,[data-gp-modal]')].some(modal=>!modal.contains(root.value)))return;
 const focused=document.activeElement as HTMLElement|null;
 if(!root.value?.contains(focused)||focused?.closest('.settings-section-tabs'))return;
 const target=root.value.querySelector<HTMLElement>('[data-settings-section="'+selected.value+'"]');
 if(target && props.layoutVariant!=='c'){event.preventDefault();focusGamepadElement(target);}
}
let listening=false;
function listen(){if(!listening){window.addEventListener('ipc:gamepad-back',backToSection);listening=true;}}
function stop(){window.removeEventListener('ipc:gamepad-back',backToSection);listening=false;}
onMounted(listen);onActivated(listen);onDeactivated(stop);onBeforeUnmount(stop);
</script>
<template>
<div ref="root" class="settings-sections" :data-gp-scope="shareNavigationScope ? undefined : 'settings-sections'" data-gp-spatial="nearest">
 <div v-if="$slots.top && layoutVariant!=='a'" class="settings-sections-top"><slot name="top" /></div>
 <div :class="{'settings-section-workspace':$slots.preview}"><div v-if="$slots.preview && layoutVariant==='c'" class="settings-section-basics" data-gp-pane="appearance"><slot name="output"/></div><aside v-if="$slots.preview" class="settings-section-preview" data-gp-pane="preview"><div v-if="layoutVariant==='a' && $slots.top" class="settings-sections-top"><slot name="top"/></div><section v-if="layoutVariant==='a'" class="settings-section-bank-actions" aria-label="当前模板数据操作"><slot name="fields-actions"/></section><section v-if="layoutVariant==='a'" class="settings-section-basics" aria-label="基础设置"><slot name="output"/></section><slot name="preview" /></aside><div :class="{'settings-section-editor':$slots.preview}">
 <nav class="settings-section-tabs" :aria-label="label+'分区'">
  <button v-for="item in sections.filter(item=>layoutVariant!=='a'||item.id!=='output')" :key="item.id" type="button" :data-settings-section="item.id" :aria-pressed="selected===item.id" @click="selected=item.id"><SectionIcon :name="item.icon"/><span>{{item.title}}</span></button>
 </nav>
 <section v-for="(item,index) in sections.filter(item=>!['a','c'].includes(layoutVariant||'')||item.id!=='output')" :key="item.id" class="settings-section-panel" :hidden="layoutVariant!=='c' && selected!==item.id" :data-gp-pane="item.id==='fields' && layoutVariant==='a'?undefined:item.id==='appearance'?'appearance':item.id==='fields'?'fields':'output'" :aria-label="item.title">
  <header class="settings-section-heading"><div><h3><SectionIcon :name="item.icon"/>{{item.title}}</h3><p v-if="item.description">{{item.description}}</p></div><div v-if="$slots[item.id+'-actions'] && layoutVariant!=='a'" class="settings-section-actions"><slot :name="item.id+'-actions'" /></div><span v-else>{{index+1}} / {{sections.length}}</span></header>
  <slot :name="item.id" :active="selected===item.id"/>
 </section>
 </div></div>
</div>
</template>
<style scoped>
.settings-sections{--section-border:color-mix(in srgb,var(--accent) 19%,#273547);min-width:0;font-size:14px}.settings-section-tabs{display:grid;grid-auto-flow:column;grid-auto-columns:1fr;gap:5px;margin:0 0 12px}.settings-section-tabs button{display:flex;align-items:center;justify-content:center;gap:5px;min-width:0;min-height:40px;padding:8px 5px;border:1px solid var(--section-border);border-radius:9px;background:var(--console-surface-alt);color:var(--text-dim);cursor:pointer;font:inherit;font-size:14px;font-weight:650;white-space:nowrap}.settings-section-tabs button[aria-pressed=true]{background:var(--accent-surface);border-color:var(--accent);color:var(--accent)}.settings-section-tabs button:focus-visible,.settings-section-tabs button.focused{outline:2px solid var(--accent);outline-offset:2px}.settings-section-panel[hidden]{display:none!important}.settings-section-panel{min-width:0;background:var(--console-surface);border:1px solid var(--section-border);border-radius:12px;padding:12px}.settings-section-heading{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;padding:0 0 12px;border-bottom:1px solid rgba(146,169,198,.12);margin-bottom:4px}.settings-section-heading h3{display:flex;align-items:center;gap:7px;margin:0;font-size:16px;color:var(--text)}.settings-section-heading h3 svg{color:var(--accent)}.settings-section-heading p{font-size:12px;line-height:1.5;color:var(--text-dim);margin:5px 0 0}.settings-section-heading>span{font-size:12px;font-variant-numeric:tabular-nums;white-space:nowrap;color:var(--text-dim);padding-top:3px}
.settings-sections-top{margin-bottom:12px}.settings-sections-top :deep(.card){margin-bottom:0}
</style>

<style scoped>
.settings-section-actions{display:flex;align-items:center;justify-content:flex-end;gap:7px;min-width:0}
.settings-section-heading:has(.settings-section-actions){align-items:center;gap:8px}
</style>
