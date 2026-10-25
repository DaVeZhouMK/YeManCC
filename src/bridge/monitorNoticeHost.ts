// Presentation only. Pre-own an inert surface while enabled; OFF never enables
// the persistent HUD, changes its carrier, or starts a telemetry subscription.
import { invoke, on, IpcRemoteError } from './ipc';
import { registerUpdateReadyPresenter } from './updateReadyNotice';
import { onShortcutActionFeedback, type ShortcutActionFeedback } from './shortcutActionFeedback';
import { createMonitorNoticeState, noticeAppearance } from './monitorShortcutNotice';
import type { MonitorOutputPreferences } from './monitorHudModel';

const RECOVERY_POLICY=Object.freeze({
 maxAttempts:3,retryDelaysMs:Object.freeze([250,1000]),readyTimeoutMs:10000,
 resetOn:Object.freeze(['current-owner-ready','explicit-enable','new-committed-power-generation']),
 explicitFeedbackMayAdvanceScheduledAttempt:true,
});
let diagnosticHostSerial=0;
type NoticeOwner={id:number;leaseId:number;nonce:string;ready:boolean;diagnosticEpoch?:number};
type NoticeStatus={id:number;leaseId?:number;leaseProtocol?:number;nonce?:string;available:boolean;ready?:boolean;recoveryBlocked?:boolean};
type Retirement={leaseRetired:boolean;resourcesDestroyed:boolean;ownedOnly:boolean};

export function startMonitorNoticeHost(){
 let ended=false,ready=false,preferences:MonitorOutputPreferences|null=null,epoch=0,opening=false,prepareAgain=false;
 let own:NoticeOwner|null=null,earlyReady:{id:number;nonce:string}|null=null,sequence=0,pendingFeedbackId:string|null=null;
 let attempts=0,retryTimer:ReturnType<typeof setTimeout>|null=null,retryRevision=0;
 let readyTimer:ReturnType<typeof setTimeout>|null=null,readyRevision=0;
 let powerHeld=false,powerGeneration:number|null=null,committedGeneration=-1,probeOwner=false;
 let ambiguousCreate=false,ambiguousLease=0,carrierBlocked=false,lastRetiredLease=0;
 const retiring=new Map<number,Promise<boolean>>(),createRequests=new Map<number,AbortController>();
 let retirements=0,retirementFailures=0,fallbackRetirements=0,lastRetirement:Retirement|null=null;
 const theme=()=>document.documentElement.dataset.theme;
 const desired=()=>!ended&&ready&&preferences?.popupEnabled===true&&!powerHeld&&!ambiguousCreate&&!carrierBlocked&&retiring.size===0;
 const diagnosticHostId=++diagnosticHostSerial,diagnosticLast=new Map<string,string>();
 let diagnosticFailures=0,firstFrameLease=0;
 const diagnosticNow=()=>typeof performance!=='undefined'?performance.now():Date.now();
 function note(stage:string,owner:NoticeOwner|null=own,extra:Record<string,string|number|boolean|null>={}){
  try{
   const row={stage,hostId:diagnosticHostId,epoch,ownerEpoch:owner?.diagnosticEpoch??null,leaseId:owner?.leaseId??null,hudId:owner?.id??null,
    attempts,maxAttempts:RECOVERY_POLICY.maxAttempts,readyTimeoutMs:RECOVERY_POLICY.readyTimeoutMs,
    powerHeld,powerGeneration,committedGeneration:committedGeneration<0?null:committedGeneration,
    enabled:preferences?.popupEnabled===true,ended,ownerReady:owner?.ready===true,ambiguousCreate,carrierBlocked,...extra};
   const message=JSON.stringify(row);if(diagnosticLast.get(stage)===message)return;
   diagnosticLast.set(stage,message); // Fixed stage set; never record nonce, notification or free-form errors.
   void invoke<{ok?:boolean}>('diagnostics.frontendTrace',{where:'monitor-notice-lifecycle',message},{timeoutMs:1000})
    .then(result=>{if(result?.ok!==true)diagnosticFailures++;}).catch(()=>{diagnosticFailures++;});
  }catch{diagnosticFailures++;}
 }
 const errorKind=(error:unknown)=>error instanceof IpcRemoteError?'remote-error':error instanceof Error&&error.message.startsWith('IPC command timed out:')?'timeout':'transport-error';
 const elapsed=(start:number)=>Math.max(0,Math.round(diagnosticNow()-start));
 const state=createMonitorNoticeState({now:Date.now,setTimer:(fn,ms)=>setTimeout(fn,ms),clearTimer:id=>clearTimeout(id as ReturnType<typeof setTimeout>),changed:value=>{if(!value){pendingFeedbackId=null;hide();}}});
 function hide(){if(own)void invoke('monitorNotice.hide',{hudId:own.id,nonce:own.nonce,seq:sequence},{timeoutMs:2000}).catch(()=>{});}
 function cancelRecovery(){++retryRevision;if(retryTimer!==null){clearTimeout(retryTimer);note('retry-cancelled');}retryTimer=null;}
 function cancelReadyWait(){++readyRevision;if(readyTimer!==null){clearTimeout(readyTimer);note('ready-wait-cancelled');}readyTimer=null;}
 function closeNative(owner:NoticeOwner|null):Promise<boolean>{
  if(!owner||!Number.isSafeInteger(owner.leaseId)||owner.leaseId<1)return Promise.resolve(owner===null);
  const existing=retiring.get(owner.leaseId);if(existing)return existing;
  let finish:(value:boolean)=>void=()=>{};
  const pending=new Promise<boolean>(resolve=>{finish=resolve;});retiring.set(owner.leaseId,pending);
  void (async()=>{
   let confirmed=false,fallback=owner.id<1;const started=diagnosticNow();note('close-start',owner);
   try{
    if(owner.id>0){let reason='none';const start=diagnosticNow();
     const result=await invoke<boolean>('monitorNotice.close',{hudId:owner.id,nonce:owner.nonce},{timeoutMs:2000}).catch(error=>{reason=errorKind(error);return false;});
     fallback=!result;note('close-result',owner,{ok:result===true,reason,elapsedMs:elapsed(start),timeoutMs:2000});}
    if(fallback)fallbackRetirements++;
    for(let attempt=0;attempt<2&&!confirmed;attempt++){
     let reason='none';const start=diagnosticNow();
     const result=await invoke<Retirement & {obsolete?:boolean}>('monitorNotice.retire',{leaseId:owner.leaseId,nonce:owner.nonce},{timeoutMs:2000}).catch(error=>{reason=errorKind(error);return null;});
     note('retire-result',owner,{retireAttempt:attempt+1,fallback,reason,elapsedMs:elapsed(start),timeoutMs:2000,
      leaseRetired:result?.leaseRetired??null,resourcesDestroyed:result?.resourcesDestroyed??null,ownedOnly:result?.ownedOnly??null,obsolete:result?.obsolete??null});
     ++retirements;lastRetirement=result;
     confirmed=result?.leaseRetired===true&&result.resourcesDestroyed===true&&result.ownedOnly===true;
    }
    if(confirmed){
     lastRetiredLease=Math.max(lastRetiredLease,owner.leaseId);
     if(ambiguousLease<=lastRetiredLease){ambiguousCreate=false;ambiguousLease=0;}
     createRequests.get(owner.leaseId)?.abort();
    }else{retirementFailures++;ambiguousCreate=true;ambiguousLease=owner.leaseId;}
   }finally{
    retiring.delete(owner.leaseId);finish(confirmed);
    note(confirmed?'retire-confirmed':'retire-unconfirmed',owner,{fallback,elapsedMs:elapsed(started)});
    if(confirmed&&!own&&desired())scheduleRecovery();
   }
  })();
  return pending;
 }
 function close(){
  ++epoch;cancelRecovery();cancelReadyWait();prepareAgain=false;probeOwner=false;state.clear();
  const old=own;own=null;earlyReady=null;closeNative(old);
 }
 function scheduleRecovery(){
  if(!desired()||retryTimer!==null)return;
  if(attempts>=RECOVERY_POLICY.maxAttempts){note('retry-exhausted');return;}
  const revision=++retryRevision,delay=RECOVERY_POLICY.retryDelaysMs[Math.max(0,attempts-1)];
  note('retry-scheduled',own,{delayMs:delay});
  retryTimer=setTimeout(()=>{
   if(revision!==retryRevision)return;
   retryTimer=null;if(!desired())return;note('retry-fired');
   if(opening){prepareAgain=true;return;}
   void prepare();
  },delay);
 }
 function earlyReadyFor(id:number,nonce:string){return earlyReady?.nonce===nonce&&earlyReady.id===id;}
 function healthy(){
  if(!desired()||!own?.ready||own.id<1)return;
  note('healthy');attempts=0;probeOwner=false;cancelRecovery();cancelReadyWait();flushPendingFeedback();
 }
 function waitForReady(owner:NoticeOwner){
  cancelReadyWait();const revision=readyRevision;note('ready-wait-start',owner);
  readyTimer=setTimeout(()=>{
   if(revision!==readyRevision)return;
   readyTimer=null;if(!desired()||own!==owner||owner.ready)return;
   note('ready-timeout',owner,{timeoutMs:RECOVERY_POLICY.readyTimeoutMs});
   ++epoch;own=null;earlyReady=null;if(!pendingFeedbackId)state.clear();closeNative(owner);scheduleRecovery();
  },RECOVERY_POLICY.readyTimeoutMs);
 }
 async function prepare(advanceScheduled=false){
  if(!desired()||own&&!probeOwner||attempts>=RECOVERY_POLICY.maxAttempts){
   note('prepare-blocked',own,{reason:ended?'ended':!preferences?.popupEnabled?'off':powerHeld?'power-held':ambiguousCreate?'ambiguous':carrierBlocked?'carrier-blocked':retiring.size?'retiring':attempts>=RECOVERY_POLICY.maxAttempts?'budget':'already-ready'});return;
  }
  if(opening){if(advanceScheduled)prepareAgain=true;return;}
  if(retryTimer!==null&&!advanceScheduled)return;
  cancelRecovery();opening=true;++attempts;const ticket=++epoch,nonce=crypto.randomUUID();
  let phase:'status'|'close'|'reserve'|'create'='status',retryNeeded=false,leaseId=0;const attemptStarted=diagnosticNow();note('attempt-start');
  try{
   const status=await invoke<NoticeStatus>('monitorNotice.status',{}, {timeoutMs:3000});
   if(!desired()||ticket!==epoch)return;
   if(!status||status.leaseProtocol!==1||typeof status.available!=='boolean'||!Number.isSafeInteger(status.id)||status.id<0)throw Error('Invalid notice status or lease protocol');
   note('status-result',own,{ok:true,statusReady:status.ready===true,available:status.available,recoveryBlocked:status.recoveryBlocked===true});
   if(status.recoveryBlocked===true){carrierBlocked=true;note('prepare-blocked',own,{reason:'carrier-blocked'});return;}
   if(own&&status.available&&status.ready===true&&status.id===own.id&&status.nonce===own.nonce){own.ready=true;healthy();return;}
   own=null;earlyReady=null;cancelReadyWait();probeOwner=false;
   if(status.id>0){
    if(typeof status.nonce!=='string'||!status.nonce||!Number.isSafeInteger(status.leaseId)||status.leaseId!<1)throw Error('Invalid notice status owner');
    phase='close';const closed=await closeNative({id:status.id,leaseId:status.leaseId!,nonce:status.nonce,ready:status.ready===true});
    if(!closed){retryNeeded=true;return;}
   }
   if(!desired()||ticket!==epoch)return;
   if(!status.available){retryNeeded=true;return;}
   phase='reserve';const reserved=await invoke<{leaseId:number;nonce:string}>('monitorNotice.reserve',{nonce},{timeoutMs:3000});
   if(!Number.isSafeInteger(reserved.leaseId)||reserved.leaseId<1||reserved.nonce!==nonce)throw Error('Invalid notice reservation');
   leaseId=reserved.leaseId;note('reserve-result',{id:0,leaseId,nonce,ready:false,diagnosticEpoch:ticket},{ok:true});
   if(!desired()||ticket!==epoch){void closeNative({id:0,leaseId,nonce,ready:false});return;}
   // Register the native lease before ready can race the create response.
   own={id:0,leaseId,nonce,ready:false,diagnosticEpoch:ticket};phase='create';note('create-start');
   const abort=new AbortController();createRequests.set(leaseId,abort);
   const created=await invoke<{id:number;nonce:string}>('monitorNotice.create',{nonce,leaseId},{timeoutMs:10000,signal:abort.signal});
   if(!Number.isSafeInteger(created.id)||created.id<1||created.nonce!==nonce)throw Error('Invalid notice owner');
   if(!desired()||ticket!==epoch){note('create-stale',{id:created.id,leaseId,nonce,ready:false,diagnosticEpoch:ticket},{reason:'stale-owner'});if(own?.nonce===nonce){own=null;earlyReady=null;}void closeNative({id:created.id,leaseId,nonce,ready:false,diagnosticEpoch:ticket});return;}
   own={id:created.id,leaseId,nonce,ready:earlyReadyFor(created.id,nonce),diagnosticEpoch:ticket};note('create-result',own,{ok:true});
   if(own.ready)healthy();else waitForReady(own);
  }catch(error){
   note('attempt-error',own,{reason:errorKind(error),phase,elapsedMs:elapsed(attemptStarted)});
   // An IPC timeout does not cancel native create. Keep its uncertainty visible
   // and do not multiply surfaces while that operation lacks a native receipt.
   if(phase==='create'&&!(error instanceof IpcRemoteError)&&leaseId>lastRetiredLease){ambiguousCreate=true;ambiguousLease=leaseId;}
   if(ticket===epoch){const old=own;own=null;earlyReady=null;cancelReadyWait();closeNative(old);retryNeeded=!ambiguousCreate;}
  }finally{
   if(leaseId)createRequests.delete(leaseId);opening=false;const again=prepareAgain;prepareAgain=false;
   if(retryNeeded&&ticket===epoch)scheduleRecovery();
   if(again&&desired())void prepare(true);
  }
 }
 async function send(value:ShortcutActionFeedback):Promise<boolean>{
  if(!desired()||!preferences?.popupEnabled||!own?.ready||own.id<1||value.expiresAtUnixMs<=Date.now())return false;
  const frame={type:'monitor-notice',protocol:1,hudId:own.id,nonce:own.nonce,seq:++sequence,expiresAt:value.expiresAtUnixMs,feedback:value,appearance:noticeAppearance({opacity:preferences.ymcc.opacity,theme:theme()})};
  const recipient=own;
  return invoke<boolean>('monitorNotice.frame',{hudId:own.id,nonce:own.nonce,frame},{timeoutMs:2000}).then(result=>{
   if(result===true&&own===recipient&&desired()&&firstFrameLease!==recipient.leaseId){firstFrameLease=recipient.leaseId;note('frame-confirmed',recipient,{ok:true});}
   return result;
  }).catch(()=>false);
 }
 // Retain presentation until this owner is ready; never replay an action.
 function flushPendingFeedback(){
  const value=state.inspect().current;
  if(!pendingFeedbackId||!value||value.id!==pendingFeedbackId||!desired()||!own?.ready||own.id<1)return;
  pendingFeedbackId=null;void send(value);
 }
 const offReady=on<{id:number;nonce:string}>('monitorNotice.ready',event=>{
  if(!own||own.nonce!==event.nonce){note('ready-rejected',own,{reason:'stale-owner'});return;}earlyReady=event;
  if(!own.id||own.id===event.id)note('ready-accepted',own,{hudId:event.id});else note('ready-rejected',own,{reason:'stale-owner'});
  if(!own.id||own.id===event.id){own.ready=true;if(own.id>0)healthy();}
 });
 const offClosed=on<{id:number;nonce:string}>('monitorNotice.closed',event=>{
  if(!own||own.nonce!==event.nonce||own.id>0&&own.id!==event.id){note('closed-rejected',own,{reason:'stale-owner'});return;}note('owner-lost');
  ++epoch;own=null;earlyReady=null;cancelReadyWait();if(!pendingFeedbackId)state.clear();scheduleRecovery();
 });
 const offFeedback=onShortcutActionFeedback(value=>{
  if(!desired()){note('feedback-rejected',own,{reason:ended?'ended':!preferences?.popupEnabled?'off':powerHeld?'power-held':carrierBlocked?'carrier-blocked':'ambiguous'});return;}if(!state.accept(value))return;pendingFeedbackId=value.id;flushPendingFeedback();if(pendingFeedbackId)void prepare(true);
 });
 const offUpdateReady=registerUpdateReadyPresenter(async version=>{
  if(!desired()||!own?.ready||own.id<1){void prepare(true);return false;}
  const inspected=state.inspect();if(inspected.current)return false; // Don't replace a shortcut receipt.
  const now=Date.now();
  const value:ShortcutActionFeedback={schemaVersion:1,id:'update-ready-'+crypto.randomUUID(),actionId:'app.updateReady',origin:'updater',state:'saved',
   title:'YMCC 更新已就绪',message:'v'+version+' 已下载并校验，可前往设置 → 更新安装并重启',occurredAtUnixMs:now,expiresAtUnixMs:now+8000,
   powerGeneration:Math.max(0,inspected.generation),value:{text:'v'+version},presentation:{fullscreen:true,leftMonitor:true}};
  if(!state.accept(value))return false;
  const recipient=own;const delivered=await send(value);const accepted=delivered&&!ended&&preferences?.popupEnabled===true&&own===recipient&&state.inspect().current?.id===value.id;
  if(!accepted&&state.inspect().current?.id===value.id)state.clear();return accepted;
 });
 function powerEventGeneration(event:unknown):number|null|false{
  const value=(event as {generation?:unknown}|null)?.generation;
  if(value===undefined)return powerGeneration===null?null:false;
  return typeof value==='number'&&Number.isSafeInteger(value)&&value>=0?value:false;
 }
 function holdPower(event:unknown){
  const generation=powerEventGeneration(event);
  if(generation===false||generation!==null&&(generation<(powerGeneration??-1)||generation<=committedGeneration)){note('power-rejected',own,{reason:'stale-generation'});return;}
  if(generation!==null)powerGeneration=generation;
  if(powerHeld)return;
  powerHeld=true;++epoch;note('power-held');cancelRecovery();cancelReadyWait();prepareAgain=false;state.clear();
 }
 function resumePower(event:unknown){
  const generation=powerEventGeneration(event);
  if(generation===false||generation!==null&&(generation<(powerGeneration??-1)||generation<=committedGeneration)){note('power-rejected',own,{reason:'stale-generation'});return;}
  if(generation===null&&!powerHeld){state.clear();return;}
  if(generation!==null){powerGeneration=generation;committedGeneration=generation;}
  powerHeld=false;note('power-committed');state.clear();cancelRecovery();cancelReadyWait();
  if(!desired())return;
  attempts=0;probeOwner=true;if(opening)prepareAgain=true;else void prepare();
 }
 const offPower=[on('power.pending',holdPower),on('power.suspending',holdPower),on('power.resumed',resumePower)];
 const onTheme=()=>{const value=state.inspect().current;if(value)void send(value);};
 window.addEventListener('theme:changed',onTheme);window.addEventListener('ui-settings:changed',onTheme);
 return {
  ready(prefs:MonitorOutputPreferences){
   if(ended)return;
   const enable=!preferences?.popupEnabled&&prefs.popupEnabled;ready=true;preferences=prefs;
   if(enable){attempts=0;carrierBlocked=false;cancelRecovery();note('intent-on');}
   void prepare();
  },
  update(prefs:MonitorOutputPreferences){
   if(ended)return;
   const previous=preferences;
   const lockChanged=previous&&(previous.ymcc.dragLocked===true)!==(prefs.ymcc.dragLocked===true);
   const templateChanged=previous&&(previous.ymccTemplate??'A')!==(prefs.ymccTemplate??'A');
   preferences=prefs;
   if(!previous?.popupEnabled&&prefs.popupEnabled){attempts=0;carrierBlocked=false;cancelRecovery();note('intent-on');}
   if(previous?.popupEnabled&&!prefs.popupEnabled)note('intent-off');
   if(previous&&prefs.popupEnabled&&(lockChanged||templateChanged)){
    const now=Date.now(),locked=prefs.ymcc.dragLocked===true;
    const title=lockChanged?(locked?'监控拖动已锁定':'监控拖动已解锁'):'已切换到监控模板 '+(prefs.ymccTemplate??'A');
    const message=lockChanged?(locked?'鼠标和触摸均穿透。请在 YMCC → 监控 → 输出预览中解锁。':'现在可以拖动、双击切换模板。'):title;
    const value:ShortcutActionFeedback={schemaVersion:1,id:'monitor-'+crypto.randomUUID(),actionId:lockChanged?'monitor.dragLock':'monitor.template',origin:'monitor',state:'applied',title,message,occurredAtUnixMs:now,expiresAtUnixMs:now+4000,powerGeneration:Math.max(0,state.inspect().generation),presentation:{fullscreen:true,leftMonitor:true}};
    state.accept(value);
   }
   if(!prefs.popupEnabled)close();else {if(opening)prepareAgain=true;else void prepare();const value=state.inspect().current;if(value)void send(value);}
  },
  stop(){if(ended)return;ended=true;note('stop');offUpdateReady();offFeedback();offReady();offClosed();for(const stop of offPower)stop();window.removeEventListener('theme:changed',onTheme);window.removeEventListener('ui-settings:changed',onTheme);close();state.dispose();},
  inspect:()=>({diagnostics:{failedDeliveries:diagnosticFailures},owner:own?{...own}:null,opening,enabled:preferences?.popupEnabled===true,state:state.inspect(),recovery:{policy:{...RECOVERY_POLICY,retryDelaysMs:[...RECOVERY_POLICY.retryDelaysMs],resetOn:[...RECOVERY_POLICY.resetOn]},attempts,scheduled:retryTimer!==null,readyWaitPending:readyTimer!==null,powerHeld,powerGeneration,committedGeneration,ambiguousCreate,carrierBlocked},cleanup:{pending:retiring.size,retirements,fallbackRetirements,retirementFailures,lastRetirement:lastRetirement?{...lastRetirement}:null,lastRetiredLease}})
 };
}
