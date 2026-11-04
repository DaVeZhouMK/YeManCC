<script setup lang="ts">
import { ROUTES } from '@/router';
import { radialTestingEnabled } from '@/radial/testingState';
import { useRouter, useRoute } from 'vue-router';
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import {useInputShortcutRuntime} from '@/bridge/inputShortcutRuntimeState';
import MouseStatusIcon from '@/components/MouseStatusIcon.vue';
const inputRuntime=useInputShortcutRuntime();
const controllerGlyph=computed(()=>!inputRuntime.value?'gamepad':!inputRuntime.value.virtualEnabled?'controller-native':({'elite':'controller-xbox','steamdeck':'controller-sd','dualsense-edge':'controller-ps5'}[inputRuntime.value.persona]??'gamepad'));
import { app, shell, windowApi } from '@/bridge/api';
import AppIcon from '@/components/AppIcon.vue';
import { fanHostLifecycle } from '@/bridge/fanHost';
import { residentNavigationRoutes } from '@/bridge/residentNavigation';
import {
  fanNavigationMotion,
  fanNavigationSpinDuration,
} from '@/bridge/fanFeature';

const router = useRouter();
const route = useRoute();
// Retired pages stay absent in every performance mode and device state.
const visibleRoutes = computed(() => residentNavigationRoutes(ROUTES, false, radialTestingEnabled.value));

const railRoot=ref<HTMLElement|null>(null),railItems=ref<HTMLElement|null>(null);
let railObserver:ResizeObserver|undefined,railFrame=0,railMounted=false;
function setRailDensity(level:number,iconScale=1):void {
  const rail=railRoot.value;if(!rail)return;
  rail.dataset.navDensity=String(level);
  rail.style.setProperty('--nav-icon-scale',String(iconScale));
  rail.style.removeProperty('--nav-row-pad');
}
function railContentFits():boolean {
  const rail=railRoot.value,list=railItems.value,last=list?.lastElementChild;
  const footer=rail?.querySelector('.nav-foot');
  if(!rail||!list||!footer)return true;
  // Measure in the same zoomed coordinate space as the rendered controls.
  const rootRect=rail.getBoundingClientRect(),listRect=list.getBoundingClientRect();
  return (!last||last.getBoundingClientRect().bottom<=listRect.bottom+.25)&&
    footer.getBoundingClientRect().bottom<=rootRect.bottom+.25;
}
function balanceRailPadding(level:number):void {
  if(!level)return;
  const rail=railRoot.value!,list=railItems.value!,last=list.lastElementChild;
  if(!last)return;
  const zoom=rail.getBoundingClientRect().height/Math.max(1,rail.offsetHeight);
  const unused=Math.max(0,(list.getBoundingClientRect().bottom-last.getBoundingClientRect().bottom)/zoom-1);
  const current=parseFloat(getComputedStyle(rail).getPropertyValue('--nav-row-pad'));
  // Return any small surplus evenly to the rows, rather than leaving one
  // empty band above Minimize/Exit. Never expand past the preceding stage.
  const previous=[9,7,5,3,1][level-1];
  const padding=Math.min(previous,current+unused/(2*(list.childElementCount+1)));
  rail.style.setProperty('--nav-row-pad',padding+'px');
  if(!railContentFits())rail.style.removeProperty('--nav-row-pad');
}
function fitRail():void {
  if(!railMounted||!railRoot.value?.isConnected)return;
  // Start from the original spacing every time so a taller sidebar expands.
  // Remove whitespace in stages before reducing any icon. Labels stay legible.
  for(let level=0;level<=4;level++){
    setRailDensity(level);
    if(railContentFits()){balanceRailPadding(level);return;}
  }
  // Only the final stage scales icons; choose the largest fitting 5% step.
  // This bounded work runs on layout/count changes, never on a polling timer.
  let low=2,high=19,best=2;
  while(low<=high){
    const step=Math.floor((low+high)/2);setRailDensity(4,step/20);
    if(railContentFits()){best=step;low=step+1;}else high=step-1;
  }
  setRailDensity(4,best/20);
}
function queueRailFit():void {
  if(!railMounted||railFrame)return;
  railFrame=requestAnimationFrame(()=>{railFrame=0;fitRail();});
}
watch(()=>visibleRoutes.value.length,queueRailFit,{flush:'post'});
onMounted(()=>{
  railMounted=true;fitRail();
  railObserver=new ResizeObserver(queueRailFit);
  railObserver.observe(railRoot.value!,{box:'border-box'});
  void document.fonts.ready.then(queueRailFit);
});
onBeforeUnmount(()=>{
  railMounted=false;railObserver?.disconnect();
  if(railFrame)cancelAnimationFrame(railFrame);
});


function go(path: string) {
  router.push(path);
}
async function openHome() {
  try {
    await shell.open('https://github.com/');
  } catch {
    /* ignore outside native shell */
  }
}
async function minimize() {
  try {
    await windowApi.minimize();
  } catch {
    /* ignore */
  }
}
async function quit() {
  // Start the normal lifecycle close, but never make the UI wait behind an
  // ACPI/HID call.  Native app exit immediately sends the authenticated
  // /api/parent-exit handoff; the Host's closeBoundaryClaimed gate serializes
  // whichever owner reaches CurrentDevice.Close first.  A failed renderer
  // close therefore cannot leave the Exit button dead or bypass recovery.
  void fanHostLifecycle.close().catch(() => {});
  try { await app.exit(0); } catch { /* native shell may already be exiting */ }
}
</script>

<template>
  <nav ref="railRoot" class="navrail">
    <div class="nav-brand app-region-drag">
      <AppIcon name="yeman" class="nb-bolt" />
      <div class="nb-titles">
        <span class="nb-title">YeManCC</span>
        <span class="nb-sub">野蛮系统控制中心</span>
      </div>
    </div>
    <div ref="railItems" class="nav-items">
      <button
        v-for="r in visibleRoutes"
        :key="r.path"
        class="nav-item"
        data-gp-ignore
        :class="{ active: route.path === r.path }"
        @click="go(r.path)"
      >
        <template v-if="r.path === '/touch-mouse'">
          <span class="nav-icon nav-touch-mouse"><MouseStatusIcon /></span>
          <span class="nav-label">触控鼠标</span>
        </template>
        <template v-else>
          <span class="nav-icon"><AppIcon :name="r.path === '/button-mapping' ? controllerGlyph : r.path === '/gyro-motion' ? (inputRuntime?.gyroEnabled ? 'gyro-on' : 'gyro-off') : r.icon" :stroke-width="['gauge', 'gamepad', 'rotate'].includes(r.icon) ? 1.2 : undefined" :class="{ spinning: r.path === '/fan' && fanNavigationMotion, 'nav-gyro-status':r.path==='/gyro-motion', 'nav-controller-status':r.path==='/button-mapping' }" :style="r.path === '/fan' ? { '--fan-spin-duration': fanNavigationSpinDuration } : undefined" /></span>
          <span class="nav-label">{{ r.title }}</span>
        </template>
      </button>
    </div>
    <div class="nav-foot app-region-no-drag">
      <button class="nav-item nav-foot-btn" data-gp-ignore @click="minimize">
        <span class="nav-icon"><AppIcon name="minimize" /></span>
        <span class="nav-label">最小化</span>
      </button>
      <button class="nav-quit" data-gp-ignore @click="quit">退出</button>
    </div>
  </nav>
</template>

<style scoped>
.nav-gyro-status{font-size:calc(23px * var(--nav-icon-scale));color:inherit}
.nav-icon.nav-touch-mouse{font-size:calc(19.2px * var(--nav-icon-scale));line-height:1}
.nav-controller-status{font-size:calc(23px * var(--nav-icon-scale));color:inherit}
.nav-brand {
  flex-shrink: 0;
  display: flex;
  align-items: center;
  gap: 4px;
  padding: var(--nav-brand-top) 3px var(--nav-brand-bottom);
  margin-bottom: var(--nav-brand-margin);
  border-bottom: 1px solid #1c2533;
  min-width: 0;
  overflow: hidden;
}
.nb-bolt {
  flex: 0 0 auto;
  color: var(--accent-2);
  font-size: 16px;
  line-height: 1;
}
.nb-titles {
  display: flex;
  flex: 1 1 auto;
  min-width: 0;
  flex-direction: column;
  line-height: 1.15;
  white-space: nowrap;
}
.nb-title {
  font-size: 11.7px;
  font-weight: 700;
  letter-spacing: 0;
  color: var(--text);
  white-space: nowrap;
}
.nb-sub {
  font-size: 8px;
  font-weight: 500;
  letter-spacing: 0;
  color: var(--text-dim);
  white-space: nowrap;
}
.navrail {
  --nav-icon-scale: 1;
  --nav-row-gap: 4px;
  --nav-row-pad: 9px;
  --nav-label-gap: 3px;
  --nav-outer-pad: 8px;
  --nav-brand-top: 6px;
  --nav-brand-bottom: 10px;
  --nav-brand-margin: 6px;
  --nav-foot-gap: 6px;
  --nav-foot-margin: 8px;
  --nav-quit-pad: 8px;
  width: var(--nav-w);
  flex: 0 0 var(--nav-w);
  background: color-mix(in srgb, var(--bg-nav) 70%, transparent);
  position: relative;
  z-index: 1;
  border-right: 1px solid #1c2533;
  display: flex;
  flex-direction: column;
  padding: var(--nav-outer-pad) 4px;
}
.navrail[data-nav-density="1"]{--nav-row-gap:3px;--nav-row-pad:7px;--nav-brand-bottom:8px;--nav-brand-margin:5px;--nav-foot-margin:7px}
.navrail[data-nav-density="2"]{--nav-row-gap:2px;--nav-row-pad:5px;--nav-label-gap:2px;--nav-outer-pad:7px;--nav-brand-top:5px;--nav-brand-bottom:6px;--nav-brand-margin:4px;--nav-foot-gap:5px;--nav-foot-margin:6px}
.navrail[data-nav-density="3"]{--nav-row-gap:1px;--nav-row-pad:3px;--nav-label-gap:2px;--nav-outer-pad:5px;--nav-brand-top:3px;--nav-brand-bottom:4px;--nav-brand-margin:3px;--nav-foot-gap:4px;--nav-foot-margin:4px;--nav-quit-pad:7px}
.navrail[data-nav-density="4"]{--nav-row-gap:0px;--nav-row-pad:1px;--nav-label-gap:1px;--nav-outer-pad:3px;--nav-brand-top:1px;--nav-brand-bottom:2px;--nav-brand-margin:1px;--nav-foot-gap:3px;--nav-foot-margin:2px;--nav-quit-pad:5px}
.nav-items {
  min-height: 0;
  overflow: clip;
  flex: 1 1 auto;
  display: flex;
  flex-direction: column;
  gap: var(--nav-row-gap);
}
.nav-item {
  flex: 0 0 auto;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: var(--nav-label-gap);
  padding: var(--nav-row-pad) 2px;
  border: 1px solid transparent;
  border-radius: var(--radius-ctrl);
  background: transparent;
  color: var(--text-dim);
  cursor: pointer;
  transition: background 0.12s, color 0.12s;
  white-space: nowrap;
}
.nav-item:hover {
  background: color-mix(in srgb, var(--text) 6%, var(--bg-input));
  color: var(--text);
}
.nav-item.active {
  background: var(--accent-surface);
  border-color: var(--accent-border);
  color: var(--accent);
}
.nav-item:focus-visible {
  box-shadow: var(--focus-ring);
}
.nav-icon {
  font-size: calc(17px * var(--nav-icon-scale));
  line-height: 1;
}
.nav-icon .spinning {
  animation: nav-fan-spin var(--fan-spin-duration, 1.1s) linear infinite;
}
@keyframes nav-fan-spin {
  to { transform: rotate(360deg); }
}
.nav-label {
  font-size: 10px;
  text-align: center;
  white-space: nowrap;
}
.nav-foot {
  flex: 0 0 auto;
  display: flex;
  flex-direction: column;
  gap: var(--nav-foot-gap);
  margin-top: var(--nav-foot-margin);
}
.nav-foot-btn {
  /* 与上方导航项完全一致：字体/气泡/图标，仅不显示 active 高亮 */
  width: 100%;
}
.nav-foot-btn:hover {
  background: color-mix(in srgb, var(--text) 6%, var(--bg-input));
  color: var(--text);
}
.nav-quit {
  font-size: 12px;
  font-weight: 600;
  color: #fff;
  background: var(--danger);
  border: none;
  border-radius: var(--radius-ctrl);
  padding: var(--nav-quit-pad);
  cursor: pointer;
}
.nav-quit:hover {
  filter: brightness(1.1);
}
.nav-quit:focus-visible {
  box-shadow: var(--focus-ring);
}

/* Keep navigation ownership unchanged; only the selected item emits light. */
.nav-items>.nav-item{position:relative;isolation:isolate;border-radius:0}
.nav-items>.nav-item.active{border-color:transparent;border-radius:0;background-color:color-mix(in srgb,var(--accent) 22%,var(--bg-panel));background-image:linear-gradient(100deg,color-mix(in srgb,var(--accent) 32%,transparent),color-mix(in srgb,var(--accent) 8%,transparent) 65%,transparent);color:var(--text);box-shadow:inset 3px 0 var(--accent),inset 0 1px 0 #b9e8ff1f,0 0 14px color-mix(in srgb,var(--accent) 10%,transparent)}
.nav-items>.nav-item.active::before{content:"";position:absolute;left:0;top:2px;bottom:2px;width:3px;border-radius:2px;background:linear-gradient(180deg,color-mix(in srgb,var(--accent) 35%,transparent),color-mix(in srgb,var(--accent) 45%,white) 42%,var(--accent) 78%,color-mix(in srgb,var(--accent) 40%,transparent));box-shadow:0 0 7px color-mix(in srgb,var(--accent) 60%,transparent),0 0 2px #d2f3ff80;pointer-events:none;opacity:.82}
.nav-items>.nav-item.active .nav-icon,.nav-items>.nav-item.active .nav-label{color:var(--text)}
.nav-items>.nav-item.active:focus-visible{outline:2px solid var(--accent);outline-offset:-2px}



</style>
