<script setup lang="ts">
import '@/styles/tsSimulation.css';
import {computed,nextTick,onMounted,onBeforeUnmount,ref,watch} from 'vue';
import ContinuousPerformancePanel from './ContinuousPerformancePanel.vue';
import AppIcon from './AppIcon.vue';
import Dropdown from './Dropdown.vue';
import {createPerformanceSimulationSession} from '@/bridge/performanceSimulation';
import type {PerformancePanelSource} from '@/bridge/performancePanelSource';
import type {ContinuousSideControls,ContinuousPerformanceSnapshot} from '@/bridge/performanceControls';
import {calibration,estimate,baselineValue,changePercent,sweep,checkpoints} from '@/performance/ts/model';
import {coreModel} from '@/performance/ts/calibration';
import {windowApi} from '@/bridge/api';
import {isNativeRuntime} from '@/bridge/ipc';
import {isUiVisible,onUiVisibilityChange} from '@/bridge/uiLifecycle';
import {focusGamepadElement} from '@/gamepad/focus';
import {powerSourceMode} from '@/bridge/powerSource';
import {subscribePolicyGameStatus} from '@/bridge/gamePolicyTarget';
import {onPerformanceScheduleChanged,onGameCustomConfigChanged} from '@/bridge/performanceSchedule';
import {onFrameRatesChanged} from '@/bridge/frameRateLimits';
const emit=defineEmits<{(e:'close'):void}>();
const session=createPerformanceSimulationSession(),source=ref<ContinuousPerformanceSnapshot|null>(null),draftBank=ref<ContinuousSideControls|null>(null),profile=ref('z2e'),viewWatts=ref(20),playing=ref(false),busy=ref(false),error=ref(''),saved=ref(false),confirmation=ref<'save'|'reset'|null>(null),details=ref(false),root=ref<HTMLElement|null>(null),editor=ref<HTMLElement|null>(null),cancelButton=ref<HTMLButtonElement|null>(null);
const copy=<T>(value:T):T=>JSON.parse(JSON.stringify(value));
const profiles=calibration.profileOptions(),entry=computed(()=>calibration.GPU_CAPABILITIES.entry(profile.value));
const fallback=()=>({mode:'auto',tdp:30,tdpCeiling:45,auto:{bias:50,saving:'none',coreSplit:15,coreLow:'littleOnly',coreHigh:'bigFirst'},manual:{frequencyMHz:3000,unlimited:false,aggressiveness:60,turbo:true,coreMode:'bigFirst'}});
const bank=computed(()=>draftBank.value||source.value?.bank||fallback()),smt=computed(()=>source.value?.hyperThreadMode!=='off');
const baseline=ref<any>({profile:'z2e',watts:20,label:'原版基准',kind:'stock',bank:null,smt:true}),pinned=computed(()=>baseline.value.kind==='captured');
const a=computed(()=>baselineValue(profile.value,baseline.value,viewWatts.value)),b=computed(()=>estimate(profile.value,bank.value,smt.value,viewWatts.value));
const curves=computed(()=>sweep(profile.value,bank.value,smt.value,baseline.value)),rows=computed(()=>checkpoints(profile.value,bank.value,smt.value,baseline.value,viewWatts.value));
const maxWatts=computed(()=>entry.value.maxWatts),fmt=(n:number)=>Math.round(n).toLocaleString('en-US');
const pct=(before:number,after:number)=>{const n=changePercent(before,after);return `${n>=0?'+':''}${n.toFixed(1)}%`;};
const deltaClass=(before:number,after:number)=>Math.abs(after-before)<.5?'equal':after>before?'up':'down';
const plot={w:280,h:138,l:43,r:13,t:12,b:26};
function maxScore(part:'gpu'|'cpu'){return Math.max(1,...curves.value.flatMap(r=>[r.a[part],r.b[part]]))*1.1;}
function x(w:number){return plot.l+(w-entry.value.minWatts)/Math.max(1,maxWatts.value-entry.value.minWatts)*(plot.w-plot.l-plot.r);}
function y(score:number,part:'gpu'|'cpu'){return plot.h-plot.b-score/maxScore(part)*(plot.h-plot.t-plot.b);}
function path(part:'gpu'|'cpu',policy:'a'|'b'){return curves.value.map((r,i)=>`${i?'L':'M'}${x(r.watts).toFixed(2)},${y(r[policy][part],part).toFixed(2)}`).join(' ');}
function ticks(part:'gpu'|'cpu'){return [0,.5,1].map(r=>({y:y(maxScore(part)*r,part),value:fmt(maxScore(part)*r)}));}
const xTicks=computed(()=>[entry.value.minWatts,Math.round((entry.value.minWatts+maxWatts.value)/2),maxWatts.value]);
const wattTicks=computed(()=>[...new Set([entry.value.minWatts,...(maxWatts.value<=20?[5,10,15]:maxWatts.value>45?[20,40,60]:[10,20,30]),maxWatts.value])].filter(w=>w>=entry.value.minWatts&&w<=maxWatts.value));
const wattPosition=(w:number)=>(w-entry.value.minWatts)/Math.max(1,maxWatts.value-entry.value.minWatts)*100;
let timer:ReturnType<typeof setInterval>|undefined,alive=true,widthOwned=false,viewEditStart=20,sourceTicket=0;
const viewport=ref({width:innerWidth,height:innerHeight,scale:Number(getComputedStyle(document.documentElement).getPropertyValue('--ui-scale'))||1});
const nativeStyle=computed(()=>({width:viewport.value.width/viewport.value.scale+'px',height:viewport.value.height/viewport.value.scale+'px'}));
function resize(){viewport.value={width:innerWidth,height:innerHeight,scale:Number(getComputedStyle(document.documentElement).getPropertyValue('--ui-scale'))||1};}
const scaleObserver=new MutationObserver(resize);
const opener=document.activeElement as HTMLElement|null,widthOwner=`ts-preview-${Date.now()}-${Math.random().toString(36).slice(2)}`;
function stopSweep(){if(timer!==undefined)clearInterval(timer);timer=undefined;playing.value=false;}
function selectWatts(value:number){stopSweep();if(Number.isFinite(value))viewWatts.value=Math.max(entry.value.minWatts,Math.min(entry.value.maxWatts,Math.round(value)));}
function toggleSweep(){if(playing.value){stopSweep();return;}if(viewWatts.value>=maxWatts.value)viewWatts.value=entry.value.minWatts;playing.value=true;timer=setInterval(()=>{viewWatts.value=Math.min(maxWatts.value,viewWatts.value+1);if(viewWatts.value>=maxWatts.value)stopSweep();},400);}
function scrub(event:Event){selectWatts(Number((event.target as HTMLInputElement).value));}
function beginViewEdit(){stopSweep();viewEditStart=viewWatts.value;}
function cancelViewEdit(){viewWatts.value=viewEditStart;}
function commitViewEdit(){viewEditStart=viewWatts.value;}
function capture(){baseline.value={profile:profile.value,watts:viewWatts.value,label:'固定基准',kind:'captured',bank:copy(bank.value),smt:smt.value};}
function original(){baseline.value={profile:profile.value,watts:viewWatts.value,label:'原版基准',kind:'stock',bank:null,smt:true};}
function selectProfile(id:string){profile.value=id;selectWatts(viewWatts.value);original();}
function acceptSource(value:ContinuousPerformanceSnapshot){source.value=copy(value);draftBank.value=null;}
const panelSource:PerformancePanelSource={sideLocked:true,nativeLayout:true,sharedNavigation:true,
 async read(side){const value=await session.read(side);if(alive)acceptSource(value);return value;},
 async commit(identity,patch){saved.value=false;const value=await session.commit(identity,patch);if(alive)acceptSource(value);return value;},
 subscribe:session.subscribe,
 preview(value,identity){if(source.value&&identity.side===source.value.side){draftBank.value=copy(value);if(JSON.stringify(value)!==JSON.stringify(source.value.bank))saved.value=false;}},
};
async function restore(){if(busy.value)return;busy.value=true;error.value='';try{acceptSource(await session.restore());saved.value=false;}catch(e){if(alive)error.value=(e as Error).message;}finally{busy.value=false;}}
async function confirmAction(){if(busy.value)return;busy.value=true;error.value='';try{if(confirmation.value==='save'){acceptSource(await session.save());saved.value=true;}else{acceptSource(await session.reset());saved.value=false;}confirmation.value=null;await nextTick();focusEditor();}catch(e){if(alive)error.value=(e as Error).message;}finally{busy.value=false;}}
function ask(action:'save'|'reset'){stopSweep();confirmation.value=action;void nextTick(()=>focusGamepadElement(cancelButton.value));}
function focusEditor(){return focusGamepadElement(editor.value?.querySelector<HTMLElement>('[data-gp-initial-focus="true"]')||null);}
function closeConfirmation(){if(busy.value)return;confirmation.value=null;void nextTick(focusEditor);}
function back(event:Event){if(event.defaultPrevented)return;event.preventDefault();if(busy.value)return;if(details.value){details.value=false;void nextTick(focusEditor);}else if(confirmation.value)closeConfirmation();else emit('close');}
function visibility(){if(document.hidden||!isUiVisible())stopSweep();}
function key(event:KeyboardEvent){if(event.key==='Escape'&&!event.defaultPrevented){back(event);}}
onMounted(async()=>{
 window.addEventListener('resize',resize);scaleObserver.observe(document.documentElement,{attributes:true,attributeFilter:['style']});resize();window.addEventListener('ipc:gamepad-back',back);window.addEventListener('keydown',key);document.addEventListener('visibilitychange',visibility);
 if(isNativeRuntime&&!/[?&]__ymccChild=\d+/.test(location.hash))try{const result=await windowApi.controllerEditorWidth(widthOwner,true,2);if(!alive){if(result.ok)void windowApi.controllerEditorWidth(widthOwner,false);}else widthOwned=result.ok;}catch{}
 try{const ticket=++sourceTicket,value=await session.read();if(alive&&ticket===sourceTicket){acceptSource(value);await nextTick();focusEditor();}}catch(e){if(alive)error.value=(e as Error).message;}
});
const focusObserver=new MutationObserver(()=>{if(editor.value?.querySelector('[data-gp-initial-focus="true"]')){focusEditor();focusObserver.disconnect();}});
watch(editor,value=>{if(value){if(value.querySelector('[data-gp-initial-focus="true"]'))focusEditor();else focusObserver.observe(value,{childList:true,subtree:true});}},{flush:'post'});
function sourceChanged(){if(alive&&!busy.value&&source.value){saved.value=false;error.value='正式供电、游戏或设置已变化，请恢复当前设置后再保存';}}
const contextStops=[watch(powerSourceMode,sourceChanged),subscribePolicyGameStatus(sourceChanged),onPerformanceScheduleChanged(sourceChanged),onGameCustomConfigChanged(sourceChanged),onFrameRatesChanged(sourceChanged)];
const stopVisibility=onUiVisibilityChange(visibility);
onBeforeUnmount(()=>{focusObserver.disconnect();scaleObserver.disconnect();window.removeEventListener('resize',resize);alive=false;sourceTicket++;stopSweep();session.dispose();stopVisibility();for(const stop of contextStops)stop();window.removeEventListener('ipc:gamepad-back',back);window.removeEventListener('keydown',key);document.removeEventListener('visibilitychange',visibility);if(widthOwned)void windowApi.controllerEditorWidth(widthOwner,false).catch(()=>undefined);opener?.focus({preventScroll:true});});
</script>
<template>
<Teleport to="body"><div class="performance-simulation-window app-root" ref="root" role="dialog" aria-modal="true" aria-label="模拟测试/重置" data-gp-modal="performance-simulation" data-gp-page-lock>
 <main class="ts-native-window" :style="nativeStyle" data-gp-performance-workbench data-gp-scope="ts-workbench" data-gp-spatial="nearest">
  <header class="workbench-header"><div class="page-title"><h1>模拟测试 / 重置</h1><span>TS 能力估算 · 非实跑</span></div><div class="header-actions"><button :disabled="busy" @click="restore">恢复当前</button><button :disabled="busy" @click="ask('reset')">重置预设</button><button :disabled="busy" class="save-current" @click="ask('save')">{{saved?'配置已保存':'保存当前设置'}}</button><button @click="details=true" aria-label="查看估算依据">i</button><button :disabled="busy" @click="emit('close')"><span class="controller-key" data-controller-key="B">B</span> 关闭页面</button></div></header>
  <div class="workbench-body">
   <section class="results-pane" data-gp-vertical-lane="ts-results" aria-label="TS 数据与曲线">
    <div class="machine-bar"><label class="machine-picker"><span>参考机型</span><Dropdown :model-value="profile" :options="profiles.map(p=>({value:p.id,label:p.label}))" @update:model-value="selectProfile(String($event))" aria-label="参考机型" :gp-row="0" :gp-col="0"/></label><span class="range-badge">{{entry.minWatts}}–{{entry.maxWatts}} W<span v-if="profile==='g3e'"> · 35 W 以上待标定</span></span></div>
    <div class="comparison-label"><h2>同瓦数提升 <span>相对基准 A</span></h2><div class="ab-legend"><span class="legend-a"><i/>A 基准</span><span class="legend-b"><i/>B 当前</span></div></div>
    <div class="watt-timeline" aria-label="独立瓦数轴">
     <div class="watt-timeline-heading"><label for="view-watts">查看瓦数 <strong data-view-watts>{{viewWatts}}<small> W</small></strong></label><span>独立查看 · 不随 TDP 设定</span><button @click="toggleSweep" :aria-pressed="playing" class="sweep-button" aria-label="逐瓦播放" data-gp-row="1" data-gp-col="0"><span aria-hidden="true">{{playing?'Ⅱ':'▷'}}</span>{{playing?'暂停查看':'逐瓦播放'}}</button></div>
     <div class="watt-scrubber" data-gp-slider-draft @gp:slider-edit-start="beginViewEdit" @gp:slider-edit-cancel="cancelViewEdit"><input id="view-watts" type="range" aria-label="查看瓦数" data-gp-row="2" data-gp-col="0" @change="commitViewEdit" :min="entry.minWatts" :max="maxWatts" step="1" :value="viewWatts" @input="scrub" @focus="stopSweep" @pointerdown="stopSweep" @gp:slider-edit-start="stopSweep" :style="{'--watt-position':wattPosition(viewWatts)+'%'}"/><div class="watt-ticks" aria-hidden="true"><span v-for="(tick,index) in wattTicks" :key="tick" :style="{left:wattPosition(tick)+'%'}" :class="{'tick-first':index===0,'tick-last':index===wattTicks.length-1}">{{tick}} W</span></div></div>
    </div>
    <div class="score-charts">
     <article v-for="part in (['gpu','cpu'] as const)" :key="part" class="chart" :class="part" :aria-label="part.toUpperCase()+' 同瓦数分数曲线'">
      <div class="chart-header"><strong>{{part.toUpperCase()}}<small>{{part==='gpu'?'同瓦数提升':'同瓦数变化'}}</small></strong><b class="chart-gain" :class="deltaClass(a[part],b[part])" :data-delta="part">{{pct(a[part],b[part])}}</b></div>
      <div class="chart-scores"><span>A <b>{{fmt(a[part])}}</b></span><span class="score-arrow">→</span><span>B <b :data-value="part">{{fmt(b[part])}}</b></span><span class="chart-watts">{{viewWatts}} W</span></div>
      <svg :viewBox="`0 0 ${plot.w} ${plot.h}`" role="img" :aria-label="part.toUpperCase()+' 分数曲线，基准和当前设置'">
       <g v-for="tick in ticks(part)" :key="tick.value" class="chart-axis"><line :x1="plot.l" :x2="plot.w-plot.r" :y1="tick.y" :y2="tick.y"/><text :x="plot.l-7" :y="tick.y+4" text-anchor="end">{{tick.value}}</text></g>
       <path :d="path(part,'a')" class="curve-a"/><path :d="path(part,'b')" class="curve-b"/>
       <line class="current-guide" :x1="x(viewWatts)" :x2="x(viewWatts)" :y1="plot.t" :y2="plot.h-plot.b"/>
       <circle class="current-dot" :cx="x(viewWatts)" :cy="y(b[part],part)" r="4.5"/>
       <g class="chart-axis"><text v-for="(tick,index) in xTicks" :key="tick" :x="x(tick)" :y="plot.h-10" :text-anchor="index===0?'start':index===2?'end':'middle'">{{tick}} W</text></g>
      </svg>
     </article>
    </div>
    <div v-if="b.smt.graphicsEfficiency==='possible-small-saving'&&!b.coreGpuGain" class="smt-impact-note" data-smt-efficiency>关超线程单项：CPU {{pct(1,b.smt.cpuFactor)}} · GPU TS 不加分 · 轻载可能略省电<span>（未标定）</span></div>
    <div v-if="b.coreGpuGain" class="core-gain-note" data-core-gpu-note>{{b.coreGpuGain.disabledGroup==='little'?'关小核':'关大核'}}单项：GPU <b :class="deltaClass(b.coreGpuGain.allCoreGpu,b.gpu)">{{pct(b.coreGpuGain.allCoreGpu,b.gpu)}}</b> · {{b.coreGpuGain.positiveGain>0?(profile==='z2e'?'AMD 簇调度校准':'正收益保留 20%'):'无额外正收益'}}<span v-if="b.smt.graphicsEfficiency==='possible-small-saving'"> · 关 SMT CPU {{pct(1,b.smt.cpuFactor)}}</span><span v-else>（同频全核对照）</span></div>
    <div class="table-heading"><h2>同瓦数对照</h2><div><button class="baseline-button" data-gp-row="3" data-gp-col="0" @click="capture">固定当前为 A</button><button v-if="pinned" class="baseline-button original-button" data-gp-row="3" data-gp-col="1" @click="original">原版 A</button></div></div>
    <div class="score-table-wrap"><table class="score-table"><colgroup><col style="width:15%"/><col style="width:28%"/><col style="width:28%"/><col style="width:29%"/></colgroup><thead><tr><th>瓦数</th><th>GPU <span>A → B</span></th><th>CPU <span>A → B</span></th><th>GPU 分 / W</th></tr></thead><tbody><tr v-for="(r,rowIndex) in rows" :key="r.watts" :class="{current:r.watts===viewWatts}"><th><button class="row-watt" :data-gp-row="4+rowIndex" data-gp-col="0" @click="selectWatts(r.watts)" :aria-label="'查看 '+r.watts+' W 对照'" :aria-pressed="r.watts===viewWatts">{{r.watts}}<small> W</small><i v-if="r.watts===viewWatts"/></button></th><td><span class="reference-number">{{fmt(r.a.gpu)}}</span><span class="table-arrow">→</span><b>{{fmt(r.b.gpu)}}</b><span class="table-percent" :class="deltaClass(r.a.gpu,r.b.gpu)" data-percent="gpu">{{pct(r.a.gpu,r.b.gpu)}}</span></td><td><span class="reference-number">{{fmt(r.a.cpu)}}</span><span class="table-arrow">→</span><b>{{fmt(r.b.cpu)}}</b><span class="table-percent" :class="deltaClass(r.a.cpu,r.b.cpu)" data-percent="cpu">{{pct(r.a.cpu,r.b.cpu)}}</span></td><td><b>{{(r.b.gpu/r.watts).toFixed(1)}}</b><span :class="deltaClass(r.a.gpu,r.b.gpu)">{{pct(r.a.gpu,r.b.gpu)}}</span></td></tr></tbody></table></div>

   </section>

   <aside class="editor-pane" aria-label="当前参数调节"><div class="editor-heading"><h2>调节面板 <span>B</span></h2><span>{{source?.identity.scope==='dedicated'?'编辑游戏专属':'编辑公用设置'}} · {{source?.side==='dc'?'电池':'插电'}}</span></div><p v-if="source?.identity.scope==='dedicated'" class="ts-game-name">{{source.gameName||source.identity.exe}}</p><div ref="editor" class="editor-scroll integrated-page-body"><ContinuousPerformancePanel v-if="source" scope="effective" :panel-source="panelSource" :readonly-preview="busy"/></div></aside>
  </div>
  <footer class="workbench-footer"><span>{{error||'左侧查看瓦数独立于 TDP · 调节暂存，确认保存后应用'}}</span><span>{{saved?'配置已保存':'临时草稿'}}</span></footer>
 </main>
 <div v-if="confirmation" class="ts-dialog-shade"><section role="dialog" aria-modal="true" :aria-label="confirmation==='save'?'保存当前设置':'重置预设'" data-gp-modal data-gp-page-lock class="ts-confirm-dialog"><h2>{{confirmation==='save'?'保存当前设置':'重置当前供电侧预设'}}</h2><p>{{confirmation==='save'?'将临时调节保存到当前公用或游戏专属配置，并由原性能调度接口应用。':'只重置本窗口草稿，确认保存后才会写入正式设置。'}}</p><p v-if="source?.identity.scope==='dedicated'" class="ts-game-name">{{source.gameName||source.identity.exe}}</p><p v-if="error" role="alert">{{error}}</p><div><button ref="cancelButton" :disabled="busy" @click="closeConfirmation"><span class="controller-key" data-controller-key="B">B</span> 取消</button><button :disabled="busy" @click="confirmAction">{{busy?'正在处理…':'确认'}}</button></div></section></div>
 <div v-if="details" class="ts-dialog-shade"><section role="dialog" aria-modal="true" aria-label="TS 估算依据" data-gp-modal data-gp-page-lock class="ts-confirm-dialog"><h2>TS 估算依据</h2><p>GPU 沿用机型 TS 瓦数曲线，CPU 为独立子测试估算。不是硬件实跑，不执行小游戏、FPS 场景或第二套浮动 TDP 调度。</p><p>5–10W 共享曲线已按两端平缓衔接。258V/G3E 关核额外 GPU 正收益保留 20%；Z2E 按 Zen 5/Zen 5c 核心簇模型保留 GPU 优化幅度，CPU 调度恢复不超过 10%。这些系数属于模拟校准，关核仍损失多线程吞吐。</p><p>超线程仅修正支持 SMT 的核心容量，不直接提高 GPU 分数；无功耗配对标定时不填写实际节能百分比。395 从 13W 起，Deck 到 16W，G3E 35W 以上待标定。</p><button @click="details=false;focusEditor()"><span class="controller-key" data-controller-key="B">B</span> 返回</button></section></div>
</div></Teleport>
</template>
