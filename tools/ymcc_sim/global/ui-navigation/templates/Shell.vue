<script setup lang="ts">
import {computed,ref,nextTick,onMounted,onBeforeUnmount,watch} from 'vue';
import NavRail from '@/components/NavRail.vue';import AppIcon from '@/components/AppIcon.vue';import Performance from '@/components/ContinuousPerformancePanel.vue';import GameRecognitionControl from '@/components/GameRecognitionControl.vue';
import Fan from '@/views/FanView.vue';import Controller from '@/views/ButtonMappingView.vue';import Gyro from '@/views/GyroMotionView.vue';import Steam from '@/views/SteamView.vue';import Power from '@/views/PowerView.vue';import Sleep from '@/views/SleepGuardView.vue';import Monitor from '@/views/MonitorOutputView.vue';import Settings from '@/views/SettingsView.vue';import Quick from '@/views/QuickAppView.vue';
import Radial from '@/views/RadialMenuView.vue';
import {startRadialTestingHost} from '@/radial/testing';
let radialTestingHost:ReturnType<typeof startRadialTestingHost>|undefined;
import PerformanceSimulationWindow from '@/components/PerformanceSimulationWindow.vue';
import {previewRoute,navigate} from './previewState';
import {resetInnerPageViewport} from '@/bridge/pageViewport';
import {ymccDesignWidth,ymccStageWidth,YMCC_SHELL_BASE_HEIGHT,YMCC_SHELL_SCALE_BOOST,YMCC_SHELL_BASE_WIDTH} from '@/bridge/monitorLayout';
import {proposalVariant} from './proposalState';
const params=new URLSearchParams(location.search),variant='a';
const performanceSimulationOpen=ref(false);
const width=ref(innerWidth),height=ref(innerHeight),dedicated=ref(params.get('context')==='dedicated'),notice=ref(''),sectionTabs=ref<{id:string,title:string}[]>([]),selectedSection=ref(''),root=ref<HTMLElement|null>(null),background=ref(params.get('background')||'animated'),navTarget=ref<HTMLElement|null>(null);
const titleMap:any={'/radial':'触摸轮盘','/schedule':'性能调度','/fan':'风扇控制','/button-mapping':'控制器','/gyro-motion':'陀螺仪','/steam':'Steam 大屏','/power':'开机启动','/sleep':'睡眠优化','/rtss':'监控','/settings':'设置','/quick':'快捷应用'};
const pages:any={'/radial':Radial,'/fan':Fan,'/button-mapping':Controller,'/gyro-motion':Gyro,'/steam':Steam,'/power':Power,'/sleep':Sleep,'/rtss':Monitor,'/settings':Settings,'/quick':Quick};
const scale=computed(()=>Math.min(height.value/YMCC_SHELL_BASE_HEIGHT*YMCC_SHELL_SCALE_BOOST,width.value/YMCC_SHELL_BASE_WIDTH));
const stage=computed(()=>({width:ymccStageWidth(ymccDesignWidth(previewRoute.path),width.value,scale.value)+'px',height:Math.max(YMCC_SHELL_BASE_HEIGHT,Math.ceil(height.value/scale.value))+'px',zoom:scale.value,'--gamepad-safe-bottom':Math.max(32,Math.min(64,height.value*.06))/Math.max(.5,scale.value)+'px'}));
const game={name:'Cyberpunk2077.exe',path:'C:/Games/Cyberpunk2077.exe',pid:77,processCreated:'100077'};
let timer:any,observer:MutationObserver,menuObserver:MutationObserver;
function tell(message:string){notice.value=message;clearTimeout(timer);timer=setTimeout(()=>notice.value='',2200);}
function toggleDedicated(){window.dispatchEvent(new CustomEvent('ipc:gamepad-edit-game',{cancelable:true}));}
function collectSections(){if(!root.value)return;const header=root.value.querySelector('.console-heading');if(header&&(variant==='d'||variant==='e')){let host=header.parentElement!.querySelector<HTMLElement>('.preview-section-nav-host');if(!host){host=document.createElement('div');host.className='preview-section-nav-host';header.after(host);}if(navTarget.value!==host)navTarget.value=host;}else if(navTarget.value)navTarget.value=null;const nodes=[...root.value.querySelectorAll<HTMLElement>('.card,.fan-card,.monitor-output-card')].filter(el=>!el.parentElement?.closest('.card,.fan-card,.monitor-output-card'));
const next=nodes.map((el,i)=>{el.dataset.consoleSection='section-'+i;let t=el.querySelector<HTMLElement>('h2,h3,.card-title,.fan-card-title,.section-title')?.innerText||el.getAttribute('aria-label')||['基础控制','曲线与节点','高级选项'][i]||'其他设置';return {id:'section-'+i,title:t.replace(/\s+/g,' ').trim().slice(0,16)};});
if(JSON.stringify(next)!==JSON.stringify(sectionTabs.value))sectionTabs.value=next;if(!next.some(n=>n.id===selectedSection.value))selectedSection.value=next[0]?.id||'';
for(const el of nodes)el.hidden=variant==='e'&&el.dataset.consoleSection!==selectedSection.value;}
function selectSection(id:string){selectedSection.value=id;collectSections();const el=root.value?.querySelector<HTMLElement>('[data-console-section="'+id+'"]');if(variant!=='e')el?.scrollIntoView({block:'start',behavior:'smooth'});else document.querySelector('.app-content')?.scrollTo({top:0});}
watch(()=>previewRoute.path,async()=>{sectionTabs.value=[];selectedSection.value='';await nextTick();resetInnerPageViewport();collectSections();});
const resize=()=>{width.value=innerWidth;height.value=innerHeight;document.documentElement.style.setProperty('--ui-scale',String(scale.value));};
onMounted(()=>{radialTestingHost=startRadialTestingHost({currentRoute:{value:previewRoute},replace:navigate} as any);resize();observer=new MutationObserver(()=>collectSections());observer.observe(root.value!,{childList:true,subtree:true});collectSections();window.addEventListener('resize',resize);window.addEventListener('preview-toggle-dedicated',toggleDedicated);let lastContext='';menuObserver=new MutationObserver(()=>{dedicated.value=!!document.querySelector('.game-recognition-popover');const context=dedicated.value?document.querySelector('[data-gp-custom-panel][data-gp-expanded="true"]')?'dedicated':'menu':'global';if(context!==lastContext){lastContext=context;parent.postMessage({type:'preview-context',context},location.origin);}});menuObserver.observe(document.body,{childList:true,subtree:true});(window as any).__shellGeometry=()=>({scale:scale.value,baseWidth:580,baseHeight:780,stageWidth:ymccStageWidth(ymccDesignWidth(previewRoute.path),width.value,scale.value),viewport:[width.value,height.value]});(window as any).__navigate=navigate;if(params.get('context')==='menu'||params.get('context')==='dedicated'){setTimeout(()=>{toggleDedicated();if(params.get('context')==='dedicated')setTimeout(()=>document.querySelector<HTMLButtonElement>('[data-gp-custom-entry]')?.click(),180);},120);}});
onBeforeUnmount(()=>{radialTestingHost?.stop();observer?.disconnect();menuObserver?.disconnect();window.removeEventListener('resize',resize);window.removeEventListener('preview-toggle-dedicated',toggleDedicated);clearTimeout(timer);});
</script>
<template>
<div class="app-root console-preview" :data-proposal="proposalVariant" :class="['console-'+variant,{'quick-menu-open':dedicated}]">
 <div class="preview-background" :class="background" aria-hidden="true"><i v-for="n in 7" :key="n" :style="{'--i':n}"/></div><div class="app-backdrop"/>
 <div class="app-stage" :style="stage"><div class="app-body"><NavRail/><main class="app-main">
 <div class="topmon-bar"><GameRecognitionControl/><div class="tm-metrics"><div class="tm-cell"><div class="tm-label">TDP</div><div class="tm-value">24.0W</div></div><div class="tm-cell"><div class="tm-label">CPU</div><div class="tm-value">3.2G</div></div><div class="tm-cell"><div class="tm-label">温度</div><div class="tm-value">62°</div></div></div><button class="music-disc" aria-label="播放音乐" @click="tell('音乐控制已选择')"><span class="cd-shape"/></button></div>
 <div class="app-content"><Teleport :to="navTarget || '.app-content'" :disabled="!navTarget"><nav v-if="(variant==='d'||variant==='e')&&sectionTabs.length>1" class="console-section-nav" aria-label="页面分区"><button v-for="s in sectionTabs" :key="s.id" :class="{active:selectedSection===s.id}" @click="selectSection(s.id)">{{s.title}}</button></nav></Teleport>
 <div ref="root" class="route-view" :data-route="previewRoute.path"><Performance v-if="previewRoute.path==='/schedule'" scope="effective" show-simulation @simulation="performanceSimulationOpen=true"/><KeepAlive v-else-if="params.get('keepAlive')==='1'" :max="2"><component :is="pages[previewRoute.path]||Settings"/></KeepAlive><component v-else :is="pages[previewRoute.path]||Settings"/></div>
 </div>
 <PerformanceSimulationWindow v-if="performanceSimulationOpen" @close="performanceSimulationOpen=false" />
 </main></div>
 </div><div v-if="notice" class="preview-notice" role="status">{{notice}}</div>
</div>
</template>
