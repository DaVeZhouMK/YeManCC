<script setup lang="ts">
import { emptyRadialUtilities, normalizeRadialUtilities, isUtilityGroup, type RadialUtilityGroup, type RadialUtilityIntent } from '@/radial/utilities';
import { emptyRadialInput, normalizeRadialInput, type RadialInputIntent } from '@/radial/input';
import { emptyRadialFan, normalizeRadialFan, type RadialFanIntent } from '@/radial/fan';
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
import RadialWheel from '@/components/RadialWheel.vue';
import { invoke, on, isNativeRuntime } from '@/bridge/ipc';
import { defaultRadialSettings, normalizeRadialSettings, emptyTdp, type TdpDraft } from '@/radial/model';
import { emptyRadialPerformance, normalizeRadialPerformance, type RadialPerformanceFrame, type RadialPerformanceIntent } from '@/radial/performance';
const params=new URLSearchParams(location.search),wheelId=Number(params.get('wheelId')),nonce=params.get('nonce')??'';
const trusted=isNativeRuntime&&Number.isSafeInteger(wheelId)&&wheelId>0&&/^[a-zA-Z0-9-]{16,80}$/.test(nonce);
const identity={wheelId,nonce},wheel=ref<InstanceType<typeof RadialWheel>|null>(null),settings=ref(defaultRadialSettings()),tdp=ref<TdpDraft>(emptyTdp()),performance=ref<RadialPerformanceFrame>(emptyRadialPerformance()),fan=ref(emptyRadialFan()),input=ref(emptyRadialInput()),utilities=ref(emptyRadialUtilities()),page=ref(0),pending=ref(false),status=ref(''),phase=ref('opening'),scale=ref(1),enterMs=ref(240),exitMs=ref(200),outputRoot=ref<HTMLElement|null>(null),exitFrom=ref('none'),exitOpacity=ref('1');
const items=computed(()=>settings.value.pages[page.value]?.items??[]);let lastUtilityRequest:RadialUtilityGroup|null=null;let disposed=false,lastFanRequest=false,lastInputRequest=false,pendingId='',pointerContact:number|null=null,footerContact:{id:number;index:number;x:number;y:number}|null=null,closeTimer:ReturnType<typeof setTimeout>|undefined,openTimer:ReturnType<typeof setTimeout>|undefined,openFrame=0;
const footerIndex=(x:number,y:number)=>y>=506&&y<=550?x>=64&&x<=180?0:x>=188&&x<=304?1:x>=312&&x<=432?2:-1:-1;
const offFrame=on<{settings:unknown;tdp:TdpDraft;performance?:unknown;fan?:unknown;input?:unknown;utilities?:unknown;transitionOrigin?:{x:number;y:number};transition?:{enterMs:number;exitMs:number}}>('radial.frame',data=>{if(disposed||phase.value==='closing'||phase.value==='closed')return;if(data.transition){if(Number.isFinite(data.transition.enterMs))enterMs.value=Math.max(1,Math.min(500,data.transition.enterMs));if(Number.isFinite(data.transition.exitMs))exitMs.value=Math.max(1,Math.min(500,data.transition.exitMs));}const incoming=normalizeRadialSettings(data.settings);if(settings.value.revision!==incoming.revision||!settings.value.pages[0]?.items[0]||phase.value==='opening')settings.value=incoming;if(data.tdp&&Number.isFinite(data.tdp.min)&&Number.isFinite(data.tdp.max))tdp.value=data.tdp;if(data.performance)performance.value=normalizeRadialPerformance(data.performance);if(data.fan){const next=normalizeRadialFan(data.fan);if(lastFanRequest&&!pending.value&&next.sourceRevision!==fan.value.sourceRevision)status.value=next.notice;fan.value=next;}if(data.input){const next=normalizeRadialInput(data.input);if(lastInputRequest&&!pending.value&&next.sourceRevision!==input.value.sourceRevision)status.value=next.notice;input.value=next;}if(data.utilities){const next=normalizeRadialUtilities(data.utilities);if(lastUtilityRequest&&!pending.value&&next[lastUtilityRequest].sourceRevision!==utilities.value[lastUtilityRequest].sourceRevision)status.value=next[lastUtilityRequest].notice;utilities.value=next;}if(phase.value==='opening'&&!openFrame){openFrame=requestAnimationFrame(finishOpening);openTimer=setTimeout(finishOpening,32);}});
const offPointer=on<{id:number;nonce:string;phase:string;x:number;y:number;contact:number;cancelled:boolean}>('radial.pointer',data=>{
  if(disposed||phase.value==='closing'||phase.value==='closed'||data.id!==wheelId||data.nonce!==nonce)return;const at=footerIndex(data.x,data.y);
  // One contact owns the whole output, including footer and wheel. A second
  // finger cannot confirm while the first still owns a dial or sliding menu.
  if(data.phase==='down'){
    if(pointerContact!==null)return;pointerContact=data.contact;
    if(at>=0){footerContact={id:data.contact,index:at,x:data.x,y:data.y};return;}
  }else if(pointerContact!==data.contact)return;
  const ended=data.phase==='up'||data.phase==='cancel';
  if(footerContact?.id===data.contact){
    if(ended){const contact=footerContact;footerContact=null;pointerContact=null;
      if(!data.cancelled&&data.phase==='up'&&at===contact.index&&Math.hypot(data.x-contact.x,data.y-contact.y)<=12)wheel.value?.footer(at);
    }return;
  }
  wheel.value?.pointer(data.phase,data.x,data.y,data.contact,data.cancelled);
  if(ended)pointerContact=null;
});
const offPad=on<{lx?:number;ly?:number;action?:string;x?:number;y?:number;phase?:string;heldMs?:number}>('radial.gamepad',input=>{if(!disposed&&phase.value!=='closing'&&phase.value!=='closed'&&(!pending.value||input.action==='back'))wheel.value?.gamepad(input);});
const offResult=on<{requestId:string;ok:boolean;message:string;tdp?:TdpDraft;performance?:unknown;fan?:unknown;input?:unknown;utilities?:unknown}>('radial.result',result=>{if(disposed||result.requestId!==pendingId)return;pending.value=false;status.value=result.message;if(result.tdp)tdp.value=result.tdp;if(result.performance)performance.value=normalizeRadialPerformance(result.performance);if(result.fan)fan.value=normalizeRadialFan(result.fan);if(result.input)input.value=normalizeRadialInput(result.input);if(result.utilities)utilities.value=normalizeRadialUtilities(result.utilities);});
function regions(polygons:number[][][]){if(trusted&&!disposed)void invoke('radial.regions',{...identity,polygons},{timeoutMs:2500}).catch(()=>close());}
async function execute(itemId:string,value?:number,control?:RadialPerformanceIntent,fanIntent?:RadialFanIntent,inputIntent?:RadialInputIntent,utilityIntent?:RadialUtilityIntent){if(!trusted||pending.value)return;const group=settings.value.pages.flatMap(page=>page.items).find(item=>item.id===itemId)?.action;lastUtilityRequest=utilityIntent&&isUtilityGroup(group)?group:null;lastFanRequest=!!fanIntent;lastInputRequest=!!inputIntent;pending.value=true;pendingId=crypto.randomUUID();const now=Date.now();try{await invoke('radial.intent',{...identity,intent:{requestId:pendingId,itemId,revision:settings.value.revision,issuedAt:now,expiresAt:now+5000,...(value===undefined?{}:{value}),...(control?{control:control.key,controlRevision:control.sourceRevision,value:control.value}:{}),...(fanIntent?{fanChoice:fanIntent.choice,fanRevision:fanIntent.sourceRevision}:{}),...(inputIntent?{inputField:inputIntent.field,inputValue:inputIntent.value,inputRevision:inputIntent.sourceRevision}:{}),...(utilityIntent?{utilityChoice:utilityIntent.choice,utilityRevision:utilityIntent.sourceRevision}:{})}},{timeoutMs:2500});}catch(e){pending.value=false;status.value=(e as Error).message;}}
function finishOpening(){cancelAnimationFrame(openFrame);openFrame=0;clearTimeout(openTimer);if(!disposed&&phase.value==='opening')phase.value='open';}
function completeClose(){
  if(disposed||phase.value!=='closing')return;clearTimeout(closeTimer);phase.value='closed';
  if(trusted)void invoke('radial.transition.complete',{...identity,phase:'closed'},{timeoutMs:1500}).catch(()=>undefined);
}
function startClosing(duration=exitMs.value){
  if(disposed||phase.value==='closing'||phase.value==='closed')return;
  const element=outputRoot.value?.querySelector('.radial-wheel');if(element){const css=getComputedStyle(element);exitFrom.value=css.transform;exitOpacity.value=css.opacity;}
  exitMs.value=Math.max(1,Math.min(500,duration));phase.value='closing';pointerContact=null;footerContact=null;
  const reduced=window.matchMedia?.('(prefers-reduced-motion: reduce)').matches;
  closeTimer=setTimeout(completeClose,reduced?32:exitMs.value+60);
}
const offClosing=on<{wheelId:number;nonce:string;durationMs:number}>('radial.closing',event=>{
  if(event.wheelId===wheelId&&event.nonce===nonce)startClosing(event.durationMs);
});
function close(){if(disposed||phase.value==='closing'||phase.value==='closed')return;startClosing();if(trusted)void invoke('radial.close',identity,{timeoutMs:1500}).catch(()=>undefined);}
function animationEnd(event:AnimationEvent){if(event.animationName==='wheel-close')completeClose();}
function feedback(){if(trusted&&settings.value.feedback&&pointerContact===null&&phase.value==='open')void invoke('radial.feedback',identity,{timeoutMs:1000}).catch(()=>undefined);}
function changePage(delta:number){page.value=(page.value+delta+settings.value.pages.length)%settings.value.pages.length;}
function resize(){scale.value=Math.min(innerWidth/496,innerHeight/584);}
onMounted(async()=>{resize();window.addEventListener('resize',resize);if(trusted)try{await invoke('radial.ready',identity,{timeoutMs:5000});await nextTick();}catch{phase.value='closing';}else{phase.value='open';status.value='使用端界面预览';tdp.value={available:true,min:5,max:30,value:15,label:'预览 · 接电'};}});
onBeforeUnmount(()=>{disposed=true;offFrame();offPointer();offPad();offResult();offClosing();cancelAnimationFrame(openFrame);clearTimeout(openTimer);clearTimeout(closeTimer);window.removeEventListener('resize',resize);});
</script>
<template><div ref="outputRoot" class="radial-output" :class="phase" @animationend="animationEnd" :style="{transform:`scale(${scale})`,'--radial-enter-ms':enterMs+'ms','--radial-exit-ms':exitMs+'ms','--radial-exit-from':exitFrom,'--radial-exit-opacity':exitOpacity}"><div class="radial-output-caption">YMCC <span>{{settings.pages[page]?.name}}</span></div><RadialWheel ref="wheel" :items="items" :tdp="tdp" :performance="performance" :fan="fan" :input="input" :utilities="utilities" :pending="pending" :status="status" @regions="regions" @execute="execute" @control="(id,intent)=>execute(id,undefined,intent)" @fan="(id,intent)=>execute(id,undefined,undefined,intent)" @input="(id,intent)=>execute(id,undefined,undefined,undefined,intent)" @utility="(id,intent)=>execute(id,undefined,undefined,undefined,undefined,intent)" @close="close" @page="changePage" @feedback="feedback"/></div></template>
<style>
html,body,#radial{margin:0;width:100%;height:100%;overflow:hidden;background:transparent}.radial-output{position:absolute;left:0;top:0;width:496px;height:584px;transform-origin:0 0;color:#e5edf7;--text:#e5edf7;--accent:#77c6ff;--danger:#ff7b8e}.radial-output-caption{position:absolute;top:12px;left:20px;right:20px;font:600 12px 'Segoe UI','Microsoft YaHei',sans-serif;color:#b2c9df;pointer-events:none}.radial-output-caption span{float:right;font-weight:400}.radial-output .radial-wheel{transform-origin:248px 248px}.radial-output.open .radial-wheel{animation:wheel-open var(--radial-enter-ms,240ms) cubic-bezier(.18,.78,.24,1) both}.radial-output.closing .radial-wheel{animation:wheel-close var(--radial-exit-ms,200ms) ease-in both}.radial-output.opening .radial-wheel{opacity:0;transform:scale(.92)}.radial-output.closed{visibility:hidden;pointer-events:none}.radial-output.closing{pointer-events:none}.radial-output-caption{transition:opacity var(--radial-exit-ms,200ms) ease}.radial-output.opening .radial-output-caption,.radial-output.closing .radial-output-caption{opacity:0}@keyframes wheel-open{from{opacity:0;transform:scale(.92)}to{opacity:1;transform:scale(1)}}@keyframes wheel-close{from{opacity:var(--radial-exit-opacity,1);transform:var(--radial-exit-from,none)}to{opacity:0;transform:scale(.96)}}@media(prefers-reduced-motion:reduce){.radial-output .radial-wheel{animation-duration:.001s!important;transition-duration:.001s!important}}
</style>
