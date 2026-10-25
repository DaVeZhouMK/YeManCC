import {createApp,h,ref,nextTick} from 'vue';
import Window from '@/components/PerformanceSimulationWindow.vue';
import {defaultPerformanceScheduleConfig,getContinuousPerformanceSnapshot,updateContinuousPerformanceControls} from '@/bridge/performanceSchedule';
import {createPerformanceSimulationSession} from '@/bridge/performanceSimulation';
import {normalizeContinuousControls} from '@/bridge/performanceControls';
import {powerSourceMode,notifyGame,listenerCounts} from './ports';
const w=window as any,clone=(x:any)=>structuredClone(x);
const config=defaultPerformanceScheduleConfig();config.controls=normalizeContinuousControls(undefined);
w.__memory={side:'ac',game:{name:'a.exe',path:'C:/owned/a.exe',pid:77,processCreated:'100077'},generation:1,settings:{performanceSchedule:config,gameCustom:{version:1,entries:{},rtss:{}},rtss:{frameRates:{ac:{fps:120,ceiling:120,lastFps:120},dc:{fps:60,ceiling:60,lastFps:60}},templateDefaults:false}},files:{},writes:[],attempts:0,fileWrites:[],systemCalls:[],events:[],holdWrite:false,failWrite:false};
const open=ref(true),app=createApp({render:()=>open.value?h(Window,{onClose:()=>{open.value=false;w.__memory.events.push({kind:'closed'})}}):null});
document.getElementById('opener')!.focus();app.mount('#mount');
w.__consumer={session:createPerformanceSimulationSession,read:getContinuousPerformanceSnapshot,commit:updateContinuousPerformanceControls,close:async()=>{open.value=false;await nextTick()},reopen:async()=>{open.value=true;await nextTick()},setSide:(v:any)=>{w.__memory.side=v;powerSourceMode.value=v},setGame:(g:any)=>{w.__memory.game=clone(g);notifyGame()},listeners:listenerCounts};
w.__ready=true;

