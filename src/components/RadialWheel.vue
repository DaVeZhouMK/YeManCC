<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import { radialItemIcon } from '@/radial/presentation';
import { WHEEL, primarySectors, childSectors, RadialSelection, sliderValue, RADIAL_SLIDER, type Sector } from '@/radial/geometry';
import type { RadialItem, TdpDraft } from '@/radial/model';
import { performanceControlId, performanceControlText, type RadialPerformanceFrame, type RadialPerformanceIntent } from '@/radial/performance';
import { RADIAL_FAN_CHOICES, fanControlId, fanChoiceText, type RadialFanFrame, type RadialFanIntent } from '@/radial/fan';
import { inputChoicesFor, inputControlId, inputChoiceLabel, type RadialInputFrame, type RadialInputIntent } from '@/radial/input';
import { isUtilityGroup, utilityControlId, type RadialUtilities, type RadialUtilityIntent } from '@/radial/utilities';
import type { PerformanceControl } from '../../decky-plugin/src/performanceModel';
const props=withDefaults(defineProps<{items:RadialItem[];edit?:boolean;selectedId?:string;tdp?:TdpDraft;performance?:RadialPerformanceFrame;fan?:RadialFanFrame;input?:RadialInputFrame;utilities?:RadialUtilities;pending?:boolean;status?:string}>(),{edit:false,pending:false,status:''});
const emit=defineEmits<{select:[id:string];execute:[id:string,value?:number];control:[parentId:string,intent:RadialPerformanceIntent];fan:[parentId:string,intent:RadialFanIntent];input:[parentId:string,intent:RadialInputIntent];utility:[parentId:string,intent:RadialUtilityIntent];close:[];page:[delta:number];feedback:[];regions:[polygons:number[][][]]}>();
const selection=reactive(new RadialSelection()),dial=ref<string|null>(null),draft=ref(0),childMode=ref(false),activePointer=ref<number|null>(null);
const controlMap=computed(()=>new Map(props.items.flatMap(parent=>parent.action==='performance'?(props.performance?.controls??[]).map(control=>[performanceControlId(parent.id,control.key),{parentId:parent.id,control}] as const):[])));
const fanMap=computed(()=>new Map(props.items.flatMap(parent=>parent.action==='fan'?RADIAL_FAN_CHOICES.map(choice=>[fanControlId(parent.id,choice.value),{parentId:parent.id,choice:choice.value}] as const):[])));
const inputMap=computed(()=>new Map(props.items.flatMap(parent=>parent.action==='input.pad'||parent.action==='input.gyro'?inputChoicesFor(parent.action,props.input).map(({control,choice})=>[inputControlId(parent.id,control.key,choice.value),{parentId:parent.id,control,choice}] as const):[])));
const utilityMap=computed(()=>new Map(props.items.flatMap(parent=>isUtilityGroup(parent.action)?(props.utilities?.[parent.action]?.choices??[]).map(choice=>[utilityControlId(parent.id,choice.value),{parentId:parent.id,group:parent.action,state:props.utilities?.[parent.action],choice}] as const):[])));
const runtimeItems=computed(()=>props.edit?props.items:props.items.map(parent=>parent.action==='performance'?{...parent,action:'submenu' as const,enabled:parent.enabled&&props.performance?.enabled===true,
  children:(props.performance?.controls??[]).map(control=>({id:performanceControlId(parent.id,control.key),label:control.key==='mode'?'调度模式':control.key==='bias'?'CPU/GPU 倾向':control.label,action:'performance.control' as const,enabled:control.enabled}))}:parent.action==='fan'?{...parent,action:'submenu' as const,enabled:parent.enabled&&props.fan?.available===true&&!props.fan.busy,
  children:RADIAL_FAN_CHOICES.map(choice=>({id:fanControlId(parent.id,choice.value),label:choice.label,action:'fan.control' as const,enabled:props.fan?.available===true&&!props.fan.busy}))}:parent.action==='input.pad'||parent.action==='input.gyro'?{...parent,action:'submenu' as const,enabled:parent.enabled&&props.input?.available===true&&!props.input.busy&&inputChoicesFor(parent.action,props.input).some(({control,choice})=>control.enabled&&choice.enabled),
  children:inputChoicesFor(parent.action,props.input).map(({control,choice})=>({id:inputControlId(parent.id,control.key,choice.value),label:inputChoiceLabel(control.key,choice.value),action:'input.control' as const,enabled:control.enabled&&choice.enabled}))}:isUtilityGroup(parent.action)?{...parent,action:'submenu' as const,enabled:parent.enabled&&props.utilities?.[parent.action]?.available===true&&!props.utilities[parent.action].busy&&props.utilities[parent.action].choices.some(choice=>choice.enabled),
  children:(props.utilities?.[parent.action]?.choices??[]).map(choice=>({id:utilityControlId(parent.id,choice.value),label:choice.label,action:'utility.control' as const,enabled:choice.enabled}))}:parent));
const inputSource=ref<'pointer'|'gamepad'|'keyboard'>('pointer');
let pointerInChildren=false,pointerReturnedToRoot=false;
const primary=computed(()=>primarySectors(runtimeItems.value));
const activeControl=computed(()=>dial.value?controlMap.value.get(dial.value):undefined);
const dialSpec=computed(()=>{
  const control=activeControl.value?.control;
  if(control?.kind==='slider')return {available:control.enabled,min:control.min,max:control.max,value:control.value,step:control.step,label:control.label,suffix:control.suffix??''};
  if(control?.kind==='choice')return {available:control.enabled,min:0,max:control.choices.length-1,value:control.choices.findIndex(option=>option.data===control.value),step:1,label:control.label,suffix:''};
  const t=props.tdp;return t?{...t,step:1,label:'TDP',suffix:' W'}:null;
});
const dialText=computed(()=>{const control=activeControl.value?.control;if(control?.kind==='choice')return control.choices[draft.value]?.label??'不可用';if(control)return performanceControlText(control,draft.value);return draft.value+' W';});
const controlValue=(id:string)=>{const control=controlMap.value.get(id)?.control;if(control)return performanceControlText(control);const utility=utilityMap.value.get(id);if(utility)return !utility.state?.pending&&utility.state?.selected===utility.choice.value?'当前':'';const input=inputMap.value.get(id);if(input)return !input.choice.enabled?'本机不可用':!input.control.enabled&&input.control.key==='gyroPreset'?'先开启':input.control.value===input.choice.value?(props.input?.pending?'等待确认':'已选择'):'';const choice=fanMap.value.get(id);return choice&&props.fan?fanChoiceText(props.fan,choice.choice):'';};
const activeParent=computed(()=>primary.value.find(s=>props.edit?(s.id===props.selectedId||s.item.children?.some(c=>c.id===props.selectedId)):s.id===dial.value)??primary.value[selection.parent??-1]);
const children=computed(()=>!dial.value&&activeParent.value?.item.action==='submenu'&&(props.edit||inputSource.value!=='gamepad'||childMode.value||selection.enteredChildren)?childSectors(activeParent.value):[]);
const inSubmenu=computed(()=>childMode.value||selection.locked||selection.enteredChildren||children.value.length>0);
const backLabel=computed(()=>dial.value?'取消调节':inSubmenu.value?'返回上级':'返回监控');
const sliderRatio=computed(()=>{const t=dialSpec.value;return t&&t.max>t.min?Math.max(0,Math.min(1,(draft.value-t.min)/(t.max-t.min))):0;});
const sliderX=computed(()=>RADIAL_SLIDER.start+(RADIAL_SLIDER.end-RADIAL_SLIDER.start)*sliderRatio.value);
const sectors=computed(()=>[...primary.value,...children.value]);
const activeId=computed(()=>props.edit?props.selectedId:selection.leaf);
watch(()=>props.items,()=>{selection.reset();dial.value=null;childMode.value=false;});
watch(()=>props.tdp?.value,v=>{if(!dial.value&&typeof v==='number')draft.value=v;},{immediate:true});
watch(()=>props.performance?.sourceRevision,()=>{if(dial.value?.startsWith('@performance/'))dial.value=null;selection.leaf=null;activePointer.value=null;});
watch(()=>[props.utilities?.['monitor.templates']?.sourceRevision,props.utilities?.['steam.controls']?.sourceRevision],(next,before)=>{const group=props.items[selection.parent??-1]?.action;if(isUtilityGroup(group)){const at=group==='monitor.templates'?0:1;if(next[at]!==before?.[at]){selection.leaf=null;activePointer.value=null;}}});
watch(()=>props.input?.sourceRevision,()=>{if(selection.leaf?.startsWith('@input/'))selection.leaf=null;if(activeParent.value?.item.children?.some(child=>child.action==='input.control'))activePointer.value=null;});
watch(()=>props.fan?.sourceRevision,()=>{if(selection.leaf?.startsWith('@fan/'))selection.leaf=null;if(activeParent.value?.item.children?.some(child=>child.action==='fan.control'))activePointer.value=null;});
watch(()=>[...sectors.value.map(s=>s.path),dial.value],()=>{
  const polygons=sectors.value.filter(s=>s.item.enabled&&s.item.action!=='none').map(s=>s.polygon);
  if(dial.value){polygons.length=0;const b=RADIAL_SLIDER;polygons.push([[b.left,b.top],[b.left+b.width,b.top],[b.left+b.width,b.top+b.height],[b.left,b.top+b.height]]);}
  // Real native hit region includes controls below the wheel, never its centre.
  polygons.push([[64,506],[180,506],[180,550],[64,550]],[[188,506],[304,506],[304,550],[188,550]],[[312,506],[432,506],[432,550],[312,550]]);
  emit('regions',polygons);
},{immediate:true,flush:'post'});
function setSelected(s:Sector){if(!s.item.enabled||s.item.action==='none'||props.pending)return;selection.parent=s.parent??s.index;selection.leaf=s.id;emit('feedback');if(props.edit)emit('select',s.id);}
function activate(id=selection.leaf){
  if(!id||props.pending)return;const s=sectors.value.find(s=>s.id===id);if(!s||!s.item.enabled)return;
  if(props.edit){emit('select',id);return;}
  if(s.item.action==='submenu'){selection.parent=s.index;selection.leaf=null;selection.locked=true;selection.enteredChildren=true;childMode.value=true;return;}
  const utility=utilityMap.value.get(id);
  if(utility&&utility.state?.available&&!utility.state.busy&&utility.choice.enabled){emit('utility',utility.parentId,{choice:utility.choice.value,sourceRevision:utility.state.sourceRevision});return;}
  const input=inputMap.value.get(id);
  if(input&&props.input?.available&&!props.input.busy&&input.control.enabled&&input.choice.enabled){emit('input',input.parentId,{field:input.control.key,value:input.choice.value,sourceRevision:props.input.sourceRevision});return;}
  const fan=fanMap.value.get(id);
  if(fan&&props.fan?.available&&!props.fan.busy){emit('fan',fan.parentId,{choice:fan.choice,sourceRevision:props.fan.sourceRevision});return;}
  const runtime=controlMap.value.get(id);
  if(runtime){
    const control=runtime.control;if(!props.performance?.enabled||!control.enabled)return;
    if(control.kind==='toggle'){emit('control',runtime.parentId,{key:control.key,value:!control.value,sourceRevision:props.performance.sourceRevision});return;}
    dial.value=id;selection.parent=s.parent??s.index;
    draft.value=control.kind==='slider'?control.value:control.choices.findIndex(option=>option.data===control.value);return;
  }
  if(s.item.action==='tdp'){
    if(!props.tdp?.available)return;dial.value=id;draft.value=props.tdp.value;selection.parent=s.index;return;
  }
  emit('execute',id);
}
function move(x:number,y:number){if(props.pending||props.edit)return;
  if(dial.value&&activeParent.value&&dialSpec.value){const spec=dialSpec.value,v=sliderValue(x,y,spec.min,spec.max,sliderContact);if(v!==null)setDraft(v);return;}
  if(activePointer.value!==null){
    if(pointerReturnedToRoot)return;
    if(pointerInChildren&&Math.hypot(x-WHEEL.center,y-WHEEL.center)<WHEEL.exit){
      childMode.value=false;selection.reset();pointerReturnedToRoot=true;return;
    }
  }
  const before=selection.leaf;selection.move(runtimeItems.value,x,y);
  if(activePointer.value!==null&&selection.enteredChildren)pointerInChildren=true;if(selection.parent===null)childMode.value=false;if(before!==selection.leaf&&selection.leaf)emit('feedback');
}
function pointer(phase:string,x:number,y:number,id=1,cancelled=false){
  if(phase==='down'){if(activePointer.value!==null)return;cancelDirection();inputSource.value='pointer';pointerInChildren=false;pointerReturnedToRoot=false;activePointer.value=id;sliderContact=!!dial.value&&sliderValue(x,y,0,1)!==null;move(x,y);}
  else if(activePointer.value===id){if(phase==='move')move(x,y);else{activePointer.value=null;sliderContact=false;const returned=pointerReturnedToRoot;pointerInChildren=false;pointerReturnedToRoot=false;if(cancelled||returned)return;if(dial.value)return;activate();}}
}
function localPointer(event:PointerEvent,phase:string){if(props.edit)return;const el=event.currentTarget as SVGElement,r=el.getBoundingClientRect();if(phase==='down')el.setPointerCapture(event.pointerId);pointer(phase,(event.clientX-r.left)*496/r.width,(event.clientY-r.top)*496/r.height,event.pointerId,phase==='cancel');}
function back(){cancelDirection();if(props.pending){emit('close');return;}if(dial.value){dial.value=null;return;}if(inSubmenu.value){childMode.value=false;selection.reset();return;}emit('close');}
function confirm(){
  cancelDirection();
  if(!dial.value){activate();return;}
  if(!dialSpec.value?.available||props.pending)return;
  const runtime=activeControl.value;
  if(runtime&&props.performance){const control=runtime.control,value=control.kind==='choice'?control.choices[draft.value]?.data:draft.value;
    if(value===undefined||control.kind==='choice'&&control.choices[draft.value]?.disabled)return;
    emit('control',runtime.parentId,{key:control.key,value,sourceRevision:props.performance.sourceRevision});
  }else emit('execute',dial.value,draft.value);
}
function setDraft(value:number){const spec=dialSpec.value;if(!spec?.available||props.pending)return;
  const next=Math.max(spec.min,Math.min(spec.max,spec.min+Math.round((value-spec.min)/spec.step)*spec.step));
  const control=activeControl.value?.control;if(control?.kind==='choice'&&control.choices[next]?.disabled)return;
  if(next!==draft.value){draft.value=next;emit('feedback');}
}
function adjust(delta:number){if(dial.value&&dialSpec.value)setDraft(draft.value+delta*dialSpec.value.step);} 
// Native timestamps carry held duration; no extra poller or delayed execution.
const directionHoldMs=600;
let sliderContact=false;
let held:{x:number;y:number;parent:number|null;expanded:boolean;cancelled:boolean}|null=null;
let stickCandidate:string|null=null,stickSince=0,stickPeak=0,stickExpanded=false;
function cancelDirection(){held=null;stickCandidate=null;stickSince=0;stickPeak=0;stickExpanded=false;}
watch(()=>[props.items,props.pending,props.performance?.sourceRevision,props.fan?.sourceRevision,props.input?.sourceRevision,props.utilities?.['monitor.templates']?.sourceRevision,props.utilities?.['steam.controls']?.sourceRevision],()=>cancelDirection());
function rootLevel(){const parent=selection.parent;selection.reset();selection.parent=parent;selection.leaf=primary.value[parent??-1]?.id??null;childMode.value=false;}
function direction(input:{x?:number;y?:number;phase?:string;heldMs?:number}){
  if(props.pending)return;
  const x=input.x??0,y=input.y??0;if(!Number.isFinite(x)||!Number.isFinite(y))return;
  if(input.phase==='release'){
    const candidate=held;held=null;
    if(candidate&&!candidate.cancelled&&!dial.value)activate();
    return;
  }
  if(input.phase!=='down'&&input.phase!=='hold')return;
  if(dial.value){if(input.phase==='down'||input.phase==='hold')adjust(x!==0?Math.sign(x):-Math.sign(y));return;}
  if(input.phase==='down'){
    const p=activeParent.value,mid=p?(p.start+p.end)/2:0;
    if(childMode.value&&p&&x!==0&&y!==0&&(x*Math.cos(mid)+y*Math.sin(mid))<-.85*Math.hypot(x,y)){rootLevel();held={x,y,parent:selection.parent,expanded:false,cancelled:true};return;}
    held={x,y,parent:selection.parent,expanded:false,cancelled:false};
    if(childMode.value){
      const list=children.value.filter(s=>s.item.enabled&&s.item.action!=='none');
      const at=list.findIndex(s=>s.id===selection.leaf),step=x!==0?(x<0?-1:1):(y<0?-1:1);
      if(list.length)setSelected(list[Math.max(0,Math.min(list.length-1,at<0?0:at+step))]);
    }else selection.stick(runtimeItems.value,x,-y);
    held.parent=selection.parent;if(selection.leaf)emit('feedback');
  }
  if(!held||held.cancelled)return;
  if(!childMode.value&&!held.expanded&&(input.heldMs??0)>=directionHoldMs&&activeParent.value?.item.action==='submenu'){
    activate(activeParent.value.id);held.expanded=true;return;
  }
}
function stick(lx:number,ly:number){
  if(!Number.isFinite(lx)||!Number.isFinite(ly))return;
  held=null;const magnitude=Math.hypot(lx,ly),before=selection.leaf;
  // Returning through the centre always cancels; it is never a release-confirm.
  if(magnitude<.26){cancelDirection();childMode.value=false;selection.reset();return;}
  if(childMode.value&&stickPeak>=.72&&magnitude<.58){rootLevel();stickCandidate=null;stickSince=0;stickPeak=magnitude;stickExpanded=false;return;}
  stickPeak=Math.max(stickPeak,magnitude);
  selection.stick(runtimeItems.value,lx,ly,childMode.value);
  if(before!==selection.leaf&&selection.leaf)emit('feedback');
  const id=childMode.value?null:selection.leaf;
  if(id!==stickCandidate){stickCandidate=id;stickSince=performance.now();stickExpanded=false;}
  if(id&&!stickExpanded&&magnitude>=.72&&performance.now()-stickSince>=directionHoldMs&&activeParent.value?.item.action==='submenu'){
    activate(id);stickExpanded=true;
  }
}
function gamepad(input:{lx?:number;ly?:number;action?:string;x?:number;y?:number;phase?:string;heldMs?:number}){
  if(props.edit||activePointer.value!==null)return;
  if(input.action==='direction-cancel'){cancelDirection();return;}
  if(input.action==='direction'){inputSource.value='gamepad';direction(input);return;}
  if(input.action==='back'){back();return;}if(input.action==='accept'){confirm();return;}
  if(input.action==='page-prev'||input.action==='page-next'){if(!dial.value)emit('page',input.action==='page-prev'?-1:1);return;}
  if(input.action==='value-down'||input.action==='value-up'){adjust(input.action==='value-down'?-1:1);return;}
  if(input.action?.startsWith('nav-')){
    inputSource.value='keyboard';cancelDirection();
    const angles:Record<string,number>={'nav-right':0,'nav-down':Math.PI/2,'nav-left':Math.PI,'nav-up':-Math.PI/2};
    if(dial.value){adjust(input.action==='nav-right'||input.action==='nav-up'?1:-1);return;}
    if(childMode.value){const list=children.value.filter(s=>s.item.enabled&&s.item.action!=='none');if(!list.length)return;const at=list.findIndex(s=>s.id===selection.leaf);const d=input.action==='nav-left'||input.action==='nav-up'?-1:1;setSelected(list[Math.max(0,Math.min(list.length-1,at<0?0:at+d))]);}
    else{
      const from=primary.value.find(s=>s.id===selection.leaf);const a=angles[input.action];
      if(!from){const r=110;move(248+Math.cos(a)*r,248+Math.sin(a)*r);}
      else{const dx=Math.cos(a),dy=Math.sin(a);const candidates=primary.value.filter(s=>s.item.enabled&&s.item.action!=='none'&&s.id!==from.id).map(s=>{const x=s.x-from.x,y=s.y-from.y;return {s,forward:x*dx+y*dy,side:Math.abs(x*dy-y*dx),distance:Math.hypot(x,y)};}).filter(c=>c.forward>2).sort((a,b)=>(a.side*1.4+a.distance)-(b.side*1.4+b.distance));if(candidates[0])setSelected(candidates[0].s);}
    }return;
  }
  if(typeof input.lx==='number'&&typeof input.ly==='number'){
    inputSource.value='gamepad';
    if(dial.value){const delta=Math.abs(input.lx)>.55?Math.sign(input.lx):0;if(delta)adjust(delta);}
    else stick(input.lx,input.ly);
  }
}
function key(event:KeyboardEvent){const map:Record<string,string>={ArrowLeft:'nav-left',ArrowRight:'nav-right',ArrowUp:'nav-up',ArrowDown:'nav-down',Enter:'accept',Escape:'back','[':'page-prev',']':'page-next','-':'value-down','+':'value-up'};if(map[event.key]){event.preventDefault();gamepad({action:map[event.key]});}}
function footer(index:number){if(index===0)back();else if(index===1)confirm();else if(dial.value)adjust(1);else emit('page',1);}
let footerContact:{id:number;index:number;x:number;y:number}|null=null;
function footerPointer(event:PointerEvent,phase:'down'|'up'|'cancel'){
  if(event.pointerType!=='touch'||!event.isPrimary)return;
  if(phase==='cancel'){footerContact=null;return;}
  const button=(event.target as Element)?.closest<HTMLButtonElement>('button[data-radial-footer]');
  if(phase==='down'){
    if(!event.isPrimary||!button||button.disabled)return;
    footerContact={id:event.pointerId,index:Number(button.dataset.radialFooter),x:event.clientX,y:event.clientY};return;
  }
  const contact=footerContact;footerContact=null;
  if(!contact||contact.id!==event.pointerId||!button||button.disabled||contact.index!==Number(button.dataset.radialFooter))return;
  const r=button.getBoundingClientRect();
  if(Math.hypot(event.clientX-contact.x,event.clientY-contact.y)>12||event.clientX<r.left||event.clientX>r.right||event.clientY<r.top||event.clientY>r.bottom)return;
  footer(contact.index);
}

defineExpose({pointer,gamepad,back,footer});
</script>
<template>
  <div class="radial-wheel" :class="{'radial-wheel--edit':edit}" :data-radial-level="inSubmenu?'child':'primary'" data-radial-wheel @keydown="key">
    <div v-if="!edit&&activeParent?.item.children?.some(child=>child.action==='input.control')" class="wheel-input-scope" data-radial-input-scope>{{input?.label}}</div>
    <svg class="wheel-geometry" viewBox="0 0 496 496" aria-label="YMCC 空心轮盘" @pointerdown="localPointer($event,'down')" @pointermove="localPointer($event,'move')" @pointerup="localPointer($event,'up')" @pointercancel="localPointer($event,'cancel')">
      <g v-for="s in sectors" :key="s.id" :data-radial-sector="s.id" :data-radial-control="controlMap.get(s.id)?.control.key" :data-radial-fan="fanMap.get(s.id)?.choice" :data-radial-utility="utilityMap.get(s.id)?.choice.value" :data-radial-input="inputMap.has(s.id)?inputMap.get(s.id)?.control.key+':'+inputMap.get(s.id)?.choice.value:undefined" :class="{'sector-selected':s.id===activeId,'sector-parent':s.parent===null&&s.index===selection.parent,'sector-disabled':!s.item.enabled||s.item.action==='none'}">
        <path :d="s.path" class="wheel-sector" :class="{'wheel-child':s.parent!==null}" />
        <AppIcon :name="radialItemIcon(s.parent===null?(items[s.index]??s.item):s.item)" :width="24" :height="24" :x="s.x-12" :y="s.y-25" class="sector-icon" />
        <text :x="s.x" :y="s.y+16" text-anchor="middle" class="sector-label">{{s.item.label}}</text>
        <text v-if="controlMap.has(s.id)||fanMap.has(s.id)||inputMap.has(s.id)||utilityMap.has(s.id)" :x="s.x" :y="s.y+31" text-anchor="middle" class="sector-control-value">{{controlValue(s.id)}}</text>
      </g>
      <g v-if="dial&&dialSpec" class="radial-value-bubble" data-radial-value-bubble :data-radial-control-label="dialSpec.label" role="group" :aria-label="dialSpec.label+' 调节'">
        <rect x="128" y="0" width="240" height="180" rx="26" class="value-bubble-surface" />
        <text x="248" y="31" text-anchor="middle" class="value-bubble-label">{{dialSpec.label}}</text>
        <text x="248" y="80" text-anchor="middle" class="dial-value" :class="{'dial-value--choice':activeControl?.control.kind==='choice'}" data-radial-draft :data-radial-draft-value="draft">{{dialText}}</text>
        <text x="248" y="101" text-anchor="middle" class="value-bubble-hint">左右滑动调节 · A 应用</text>
        <rect x="142" y="106" width="212" height="44" rx="22" fill="transparent" data-radial-slider-hit />
        <path d="M154 126H342" class="value-slider-track" />
        <path :d="`M154 126H${sliderX}`" class="value-slider-fill" />
        <circle :cx="sliderX" cy="126" r="12" class="value-slider-thumb" data-radial-dial-thumb role="slider" tabindex="0" :aria-label="dialSpec.label" :aria-valuemin="dialSpec.min" :aria-valuemax="dialSpec.max" :aria-valuenow="draft" :aria-valuetext="dialText" />
        <text x="154" y="164" class="dial-range">{{activeControl?.control.kind==='choice'?'':dialSpec.min+dialSpec.suffix}}</text>
        <text x="342" y="164" text-anchor="end" class="dial-range">{{activeControl?.control.kind==='choice'?'':dialSpec.max+dialSpec.suffix}}</text>
      </g>
      <!-- No painted surface, label or button exists in the centre hole. -->
    </svg>
    <template v-if="edit"><button v-for="s in sectors" :key="`edit-${s.id}`" class="wheel-edit-hit" data-gp-ignore tabindex="-1" :style="{left:(s.x/496*100)+'%',top:(s.y/560*100)+'%'}" :aria-label="`编辑 ${s.item.label}`" :aria-pressed="s.id===selectedId" :data-radial-item="s.id" @click="setSelected(s)">{{s.item.label}}</button></template>
    <div v-if="!edit" class="wheel-actions" @touchstart.prevent @pointerdown="footerPointer($event,'down')" @pointerup="footerPointer($event,'up')" @pointercancel="footerPointer($event,'cancel')">
      <button data-radial-footer="0" :aria-label="backLabel" @click="footer(0)"><b class="key-b">B</b>{{backLabel}}</button>
      <button data-radial-footer="1" :disabled="pending||(!dial&&!selection.leaf)" @click="footer(1)"><b class="key-a">A</b>{{pending?'提交中…':dial?'应用 '+(dialSpec?.label??'TDP'):'确认'}}</button>
      <button data-radial-footer="2" :disabled="pending" @click="footer(2)">{{dial?'+ '+dialSpec?.step+(dialSpec?.suffix??''):'RB 下一页'}}</button>
    </div>
    <p class="wheel-status" role="status">{{status || (dial ? ((activeControl?performance?.label:tdp?.label)+' · LT / RT 微调，A 应用') : edit ? '' : inputSource==='pointer' ? (inSubmenu?'松手执行子项 · 向内滑或点“返回上级”返回':'点选或滑动选择，松手执行 · 返回监控可关闭') : '方向键松开确认 · 回中取消 · 长停留进入二级')}}</p>
  </div>
</template>
<style scoped>
.wheel-input-scope{position:absolute;top:0;left:10px;right:10px;text-align:center;font-size:11px;color:#a6b8cd;pointer-events:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.radial-wheel{position:relative;width:496px;height:584px;max-width:100%;aspect-ratio:496/584;color:var(--text,#e4eaf4);font-family:'Segoe UI','Microsoft YaHei',sans-serif;user-select:none;touch-action:none}.wheel-geometry{display:block;width:100%;height:auto;overflow:visible;touch-action:none}.wheel-sector{fill:#142234f2;stroke:#35485d;stroke-width:1;transition:fill .12s,stroke .12s}.wheel-child{fill:#1b3047f7}.sector-selected .wheel-sector{fill:#244f74;stroke:#75c9ff;stroke-width:2}.sector-parent .wheel-sector{stroke:#5b9dcd}.sector-disabled{opacity:.32}.sector-icon{color:#80cbff;font-size:24px;pointer-events:none}.sector-control-value{fill:#94b8d7;font-size:10px;pointer-events:none}.dial-value--choice{font-size:19px}.sector-label{fill:#e5edf7;font-size:12px;pointer-events:none}.wheel-edit-hit{position:absolute;transform:translate(-50%,-50%);width:72px;height:52px;background:transparent!important;color:transparent!important;border:0;border-radius:9px;padding:0}.wheel-edit-hit.focused,.wheel-edit-hit:focus-visible{outline:2px solid var(--accent,#67baff);outline-offset:2px}.dial-sector{fill:#1d344df7;stroke:#7dcdff}.dial-value{fill:#d9f1ff;font-size:26px;font-weight:650}.dial-range{fill:#98b1ce;font-size:12px}.wheel-actions{position:absolute;left:12.9%;right:12.9%;top:86.64%;display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}.wheel-actions button{min-height:44px;border:1px solid #354358;border-radius:7px;background:#152232ee;color:#e6edf8;font:600 12px 'Microsoft YaHei',sans-serif}.wheel-actions button:disabled{opacity:.5}.key-b{color:#ff7d89;margin-right:6px}.key-a{color:#85d6a9;margin-right:6px}.wheel-status{position:absolute;top:94.5%;width:100%;text-align:center;font-size:11px;color:#a6b8cd;margin:0;white-space:nowrap}.radial-wheel--edit{height:auto;aspect-ratio:496/560}.radial-wheel--edit .wheel-status{display:none}
</style>

<style scoped>
.radial-value-bubble{filter:drop-shadow(0 8px 18px #030c1b99)}.value-bubble-surface{fill:#13283bf7;stroke:#80d4ff;stroke-width:1.5}.value-bubble-label{fill:#cbeaff;font-size:14px;font-weight:600}.radial-value-bubble .dial-value{fill:#effbff;font-size:34px;font-weight:650;filter:drop-shadow(0 0 8px #6cd4ff88)}.radial-value-bubble .dial-value--choice{font-size:24px}.value-bubble-hint{fill:#a2bfd2;font-size:10px}.value-slider-track{stroke:#36536b;stroke-width:8;stroke-linecap:round}.value-slider-fill{stroke:#83dfff;stroke-width:8;stroke-linecap:round;filter:drop-shadow(0 0 5px #48c9ff99)}.value-slider-thumb{fill:#e8fbff;stroke:#8be2ff;stroke-width:3;filter:drop-shadow(0 0 8px #67d9ff)}.value-slider-thumb:focus-visible{outline:none;stroke:#fff;stroke-width:5}.radial-value-bubble .dial-range{fill:#9cb9ce;font-size:11px}
</style>
