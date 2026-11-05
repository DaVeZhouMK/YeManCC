import {computed,readonly,shallowReactive} from 'vue';
import {invoke,on} from './ipc';
import {probeUndervolt,setUndervolt,type UvProbe,type UvSetReceipt,type UvSetResult} from './cpuctl';

export interface UndervoltPreference {schema:1;enabled:boolean;value:number;vendor:string;unit:string;identity:UvProbe['identity']}
interface NativeTrial {ok:boolean;token:number;pending:boolean;remaining:number;stage:string;error:string;saved:UndervoltPreference|null;candidate?:UndervoltPreference|null;excluded:boolean}
const state=shallowReactive({capability:null as UvProbe|null,ready:false,busy:false,enabled:false,value:0,
  pending:false,remaining:0,stage:'idle',error:'',notice:'',excluded:false,saved:null as UndervoltPreference|null,
  receipt:null as UvSetReceipt|null,applicationState:'unknown' as UvSetResult['applicationState']});
export const undervoltState=readonly(state);
export const undervoltAvailable=computed(()=>!state.excluded&&state.capability?.supported===true&&state.capability.available===true);
export const undervoltLabel=computed(()=>!state.ready?'待确认':!undervoltAvailable.value?'不支持':state.enabled?'开启':'关闭');
let initialized:Promise<void>|undefined,stopEvents:(()=>void)|undefined,noticeTimer:ReturnType<typeof setTimeout>|undefined;
let operation=0,closed=false;
let nativeTrial:NativeTrial|undefined,capabilityProbe:Promise<void>|undefined;
function notice(text:string){if(noticeTimer)clearTimeout(noticeTimer);state.notice=text;noticeTimer=setTimeout(()=>{state.notice='';noticeTimer=undefined;},4000);}
function accept(snapshot:NativeTrial):boolean{
 if(closed||!snapshot||snapshot.ok!==true||typeof snapshot.pending!=='boolean'||typeof snapshot.excluded!=='boolean'||
  !Number.isSafeInteger(snapshot.token)||snapshot.token<0||!Number.isSafeInteger(snapshot.remaining)||snapshot.remaining<0||snapshot.remaining>30||
  typeof snapshot.stage!=='string'||snapshot.pending!==(snapshot.stage==='pending'))return false;
 if(nativeTrial){
  if(snapshot.token<nativeTrial.token)return false;
  if(snapshot.token===nativeTrial.token){
   // A trial may advance applying -> pending -> saved/failed, never backwards.
   if(nativeTrial.pending){
    if(snapshot.pending?snapshot.remaining>nativeTrial.remaining:!['saved','failed'].includes(snapshot.stage))return false;
   }else if(nativeTrial.stage!=='applying')return false;
  }
 }
 nativeTrial={...snapshot};
 state.pending=snapshot.pending;state.remaining=snapshot.remaining;state.stage=snapshot.stage;state.saved=snapshot.saved;state.excluded=snapshot.excluded;
 state.error=snapshot.error||'';return true;
}
function retryableProbe(){return !state.excluded&&(!state.capability||state.capability.reason==='probe_failed');}
async function refreshCapability(){
 if(closed)return;
 if(capabilityProbe)return capabilityProbe;
 const epoch=operation;
 const pending=(async()=>{
  try{const p=await probeUndervolt();if(closed||epoch!==operation)return;state.capability=p;
   state.error=p.reason==='probe_failed'?'设备能力探测暂时失败，请再次操作重试。':'';
  }catch(error){if(!closed&&epoch===operation)state.error=(error as Error).message;}
 })();
 capabilityProbe=pending;
 try{await pending;}finally{if(capabilityProbe===pending)capabilityProbe=undefined;}
}
function validOffset(value:number,p:UvProbe){return Number.isSafeInteger(value)&&value<=0&&value>=p.offset.min&&value<=Math.min(0,p.offset.max)&&p.offset.step>0&&Math.abs((value-p.offset.min)/p.offset.step-Math.round((value-p.offset.min)/p.offset.step))<1e-7;}
function sameDevice(saved:UndervoltPreference,p:UvProbe){
 const a=saved.identity,b=p.identity;
 return saved.vendor===p.vendor&&saved.unit===p.unit&&!!a&&!!b&&a.vendor===b.vendor&&a.family===b.family&&a.familyTag===b.familyTag&&
  a.cpuid?.family===b.cpuid?.family&&a.cpuid?.model===b.cpuid?.model&&validOffset(saved.value,p);
}
export function ensureUndervoltRuntime():Promise<void>{
 if(initialized)return initialized;
 closed=false;
 stopEvents=on<NativeTrial>('cpu.undervolt.state',accept);
 initialized=(async()=>{
  try{
   const snapshot=await invoke<NativeTrial>('cpu.undervolt.state');if(closed)return;
   if(!accept(snapshot)&&!nativeTrial)throw Error('降压试用状态无效，请重试');
   if(state.excluded){state.ready=true;return;}
   const p=await probeUndervolt();if(closed)return;state.capability=p;state.ready=true;
   const current=nativeTrial!;const saved=current.saved;
   // A renderer refresh must rejoin the native trial, not reapply an old value
   // or restart its deadline. Page navigation never recreates this owner.
   if(current.pending){
    if(current.candidate&&undervoltAvailable.value&&sameDevice(current.candidate,p)){
     state.enabled=current.candidate.enabled;state.value=current.candidate.value;
    }
    return;
   }
   if(current.stage==='applying'||current.token!==snapshot.token||current.stage!==snapshot.stage)return;
   if(saved&&undervoltAvailable.value&&sameDevice(saved,p)){
    state.value=saved.value;
    {state.busy=true;const result=await setUndervolt(saved.enabled?saved.value:0,p);if(closed||nativeTrial?.token!==current.token||nativeTrial?.stage!==current.stage)return;state.receipt=result.receipt??null;state.applicationState=result.applicationState;
     if(result.accepted&&result.receipt)state.enabled=saved.enabled;else state.error=result.msg||'上次降压参数未取得接受回执';}
   }
  }catch(error){if(!closed){state.ready=true;state.error=(error as Error).message;}}
  finally{if(!closed)state.busy=false;}
 })();return initialized;
}
async function apply(enabled:boolean,value:number){
 if(closed)return;
 if(!Number.isSafeInteger(value)||value>0){notice('仅允许非正整数电压偏移。');return;}
 if(!state.ready){void ensureUndervoltRuntime();notice('正在确认设备支持情况，请稍后重试。');return;}
 await ensureUndervoltRuntime();if(closed||state.busy)return;
 // An explicit new action may retry a transient read; it never replays startup writes.
 if(retryableProbe())await refreshCapability();
 if(closed||state.busy)return;
 const p=state.capability;
 if(!undervoltAvailable.value||!p){notice(retryableProbe()?'设备能力暂未确认，请稍后重试。':'当前设备不支持降压，不能启用。');return;}
 if(!validOffset(value,p)){notice('电压偏移超出当前设备支持范围。');return;}
 const epoch=++operation;let trialToken=nativeTrial?.token;state.busy=true;state.error='';state.receipt=null;
 try{
  // Retire the previous save before starting another potentially slow hardware write.
  const begun=await invoke<NativeTrial>('cpu.undervolt.begin');if(closed||epoch!==operation)return;
  if(!accept(begun)||begun.stage!=='applying'){notice('降压试用已被更新，本次不提交。');return;}trialToken=begun.token;
  const result=await setUndervolt(enabled?value:0,p);if(closed||epoch!==operation||nativeTrial?.token!==begun.token)return;
  state.receipt=result.receipt??null;state.applicationState=result.applicationState;
  if(!result.accepted||!result.receipt){state.error=result.msg||'电压请求未取得接受回执，本次不保存';state.stage='failed';return;}
  state.enabled=enabled;state.value=value;
  const preference:UndervoltPreference={schema:1,enabled,value,vendor:p.vendor,unit:p.unit,identity:p.identity};
  const armed=await invoke<NativeTrial>('cpu.undervolt.arm',{token:begun.token,preference,receipt:result.receipt});
  if(closed||epoch!==operation)return;if(!armed.ok)throw Error(armed.error||'降压参数保存倒计时未启动');if(!accept(armed))return;
  notice(`${enabled?'降压已开启':'降压已关闭'}。30 秒后自动保存，切换页面或进入后台仍继续计时。`);
 }catch(error){if(!closed&&epoch===operation&&nativeTrial?.token===trialToken){state.error=(error as Error).message;state.stage='failed';}}
 finally{if(!closed&&epoch===operation)state.busy=false;}
}
export async function refreshUndervoltRuntime(){await ensureUndervoltRuntime();if(!closed)try{accept(await invoke<NativeTrial>('cpu.undervolt.state'));}catch{}}
export function setUndervoltEnabled(enabled:boolean){return apply(enabled,state.value);}
export function toggleUndervolt(){return setUndervoltEnabled(!state.enabled);}
export function applyUndervoltOffset(value:number){return apply(state.enabled,value);}
export async function cancelUndervoltTrial(){
 if(closed||state.busy)return;const saved=state.saved;
 if(saved&&state.capability&&sameDevice(saved,state.capability))await apply(saved.enabled,saved.value);
 else await apply(false,0);
 if(closed||state.stage!=='pending'||!state.receipt?.accepted)return;
 const snapshot=await invoke<NativeTrial>('cpu.undervolt.cancel');if(accept(snapshot))notice('已恢复上次保存的参数。');
}
export function resetUndervoltDefaults(){return apply(false,0);}
// App lifetime only. Neither settings nor performance page owns/disposes this runtime.
export function stopUndervoltRuntime(){void invoke('cpu.undervolt.cancel').catch(()=>{});closed=true;++operation;stopEvents?.();stopEvents=undefined;if(noticeTimer)clearTimeout(noticeTimer);noticeTimer=undefined;}
