<script setup lang="ts">
// [AI:USER:TOUCH-MOUSE] Independent resident page; entering selects the mouse
// tab only. Page navigation must never change the persisted mouse policy.
import {computed,nextTick,onActivated,onBeforeUnmount,onMounted,ref} from 'vue';
import IntegratedPage from '@/components/IntegratedPage.vue';
import AppIcon from '@/components/AppIcon.vue';
import ControllerShortcutsCard from '@/components/ControllerShortcutsCard.vue';
import ScreenTouchpadsSettings from '@/components/ScreenTouchpadsSettings.vue';
import {useInputShortcutRuntime} from '@/bridge/inputShortcutRuntimeState';
import {CONTROLLER_PERSONA_OPTIONS} from '@/bridge/controllerPersona';
import {focusGamepadElement} from '@/gamepad/focus';
const runtime=useInputShortcutRuntime();
type Section='mouse'|'touch';
const section=ref<Section>('mouse'),root=ref<HTMLElement|null>(null);
const persona=computed(()=>runtime.value?.virtualEnabled?runtime.value.persona:'disabled');
const steamDeckActive=computed(()=>runtime.value?.virtualEnabled===true&&runtime.value.persona==='steamdeck');
const ps5Active=computed(()=>runtime.value?.virtualEnabled===true&&runtime.value.persona==='dualsense-edge');
const personaLabel=computed(()=>!runtime.value?'手柄状态待确认':persona.value==='disabled'?'本机手柄':CONTROLLER_PERSONA_OPTIONS.find(option=>option.value===persona.value)?.label??'手柄状态待确认');
function select(next:Section){section.value=next;void nextTick(()=>root.value?.closest('.app-content')?.scrollTo({top:0}));}
function back(event:Event){
  if(event.defaultPrevented||!root.value?.isConnected||!root.value.getClientRects().length||document.querySelector('.dd-open,[data-gp-modal],.mouse-rule-editor,.mouse-backend-popover'))return;
  const active=document.activeElement as HTMLElement|null;
  if(!root.value.contains(active)||active?.closest('.touch-mouse-tabs'))return;
  event.preventDefault();focusGamepadElement(root.value.querySelector<HTMLElement>('[data-touch-mouse-section="'+section.value+'"]'));
}
onMounted(()=>window.addEventListener('ipc:gamepad-back',back));
onActivated(()=>select('mouse'));
onBeforeUnmount(()=>window.removeEventListener('ipc:gamepad-back',back));
</script>
<template>
  <IntegratedPage title="触控鼠标" hide-heading>
    <div ref="root" class="page touch-mouse-page" data-gp-scope="touch-mouse-page" data-gp-spatial="nearest" :data-touch-mouse-panel="section">
      <header class="console-heading touch-mouse-header"><h1><AppIcon name="mouse-solid"/>触控鼠标</h1><span class="touch-mouse-persona" aria-label="当前手柄，只读"><AppIcon name="gamepad"/>{{personaLabel}}</span></header>
      <nav class="touch-mouse-tabs" aria-label="触控鼠标分区">
        <button type="button" data-touch-mouse-section="mouse" :aria-pressed="section==='mouse'" @click="select('mouse')"><AppIcon name="mouse-solid"/>鼠标</button>
        <button type="button" data-touch-mouse-section="touch" :aria-pressed="section==='touch'" @click="select('touch')"><AppIcon name="touch-steam"/>触控</button>
      </nav>
      <ControllerShortcutsCard v-if="section==='mouse'" mouse-settings-only :persona="persona" :steam-deck-active="steamDeckActive" :disabled="!runtime" />
      <section v-else class="card touch-mouse-touch-card" aria-label="屏幕触控板设置"><ScreenTouchpadsSettings :key="persona" :persona="persona" :steam-deck-enabled="steamDeckActive" :ps5-enabled="ps5Active" :disabled="!runtime" /></section>
    </div>
  </IntegratedPage>
</template>
<style scoped>
.touch-mouse-page{--touch-mouse-line:color-mix(in srgb,var(--accent) 20%,rgba(138,157,180,.18));gap:10px!important;min-width:0;padding-bottom:10px!important}
.touch-mouse-header{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:0;padding:3px 1px 0}
.touch-mouse-header h1{display:flex;align-items:center;gap:7px;min-width:0;margin:0!important;padding:0!important;border:0!important;white-space:nowrap;font-size:20px;font-weight:650;line-height:1.2}
.touch-mouse-header h1 .app-icon{font-size:20px;color:#eef0f2}
.touch-mouse-persona{display:inline-flex;align-items:center;gap:6px;min-height:30px;padding:4px 8px;border:1px solid var(--touch-mouse-line);border-radius:7px;background:var(--console-surface-alt);font-size:13px;color:var(--text);white-space:nowrap}
.touch-mouse-persona .app-icon{font-size:15px;color:var(--accent)}
.touch-mouse-tabs{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;min-width:0}
.touch-mouse-tabs button{display:flex;align-items:center;justify-content:center;gap:6px;height:39px;min-width:0;padding:6px 5px;border:1px solid var(--touch-mouse-line);border-radius:9px;background:var(--console-surface-alt);color:var(--text-dim);font:inherit;font-size:14px;font-weight:600;cursor:pointer;white-space:nowrap}
.touch-mouse-tabs .app-icon{font-size:17px}
.touch-mouse-tabs button[aria-pressed=true]{color:var(--accent);background:var(--accent-surface);border-color:var(--accent);box-shadow:inset 0 1px 0 rgba(255,255,255,.05)}
.touch-mouse-tabs button.focused,.touch-mouse-tabs button:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
</style>
<style>
.touch-mouse-page .touch-mouse-touch-card{padding:12px!important;margin:0!important;border:1px solid var(--touch-mouse-line)!important;border-radius:12px!important;background:var(--console-surface)!important}
.touch-mouse-page .touchpad-master{display:flex;align-items:center;justify-content:space-between;gap:9px;padding:0 0 11px;margin-bottom:10px;border-bottom:1px solid var(--touch-mouse-line)}
.touch-mouse-page .touchpad-master-copy{display:grid;gap:3px;min-width:0}
.touch-mouse-page .touchpad-master strong{font-size:15px;color:var(--text);font-weight:650;line-height:1.4}
.touch-mouse-page .touchpad-master-copy>span{font-size:12px;color:var(--text-dim);line-height:1.4}
.touch-mouse-page .touchpad-master .toggle-row{padding:0!important;min-height:32px}
.touch-mouse-page .screen-touchpads-bubble{border:0;background:transparent;padding:0;margin:0;gap:9px 10px}
.touch-mouse-page .screen-touchpad-field{gap:5px}
.touch-mouse-page .screen-touchpad-field label{font-size:13px;line-height:17px;letter-spacing:0;color:var(--text-dim)}
.touch-mouse-page .screen-touchpad-field .dd-trigger{height:34px;min-height:34px;padding:0 8px;font-size:13px;border-radius:7px}
.touch-mouse-page .screen-control-selects{gap:9px 10px}
.touch-mouse-page .screen-touchpad-sliders{gap:8px 10px;padding-top:8px;margin-top:2px;border-color:var(--touch-mouse-line)}
.touch-mouse-page .screen-touchpad-sliders .slider{padding:2px 0!important}
.touch-mouse-page .screen-touchpad-sliders .slider-label,.touch-mouse-page .screen-touchpad-sliders .slider-val{font-size:13px}
.touch-mouse-page .touchpad-memory-note{display:flex;align-items:center;gap:5px;margin:10px 0 0;font-size:11px;color:var(--text-dim);line-height:1.4}
.touch-mouse-page .touchpad-memory-note>span{color:var(--accent);font-size:8px}
.touch-mouse-page .touchpad-disabled{opacity:.45}
</style>
