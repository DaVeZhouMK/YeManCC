import type { Router } from 'vue-router';
import { isRadialTestingEnabled } from './testingState';
import { invoke, isNativeRuntime, on } from '@/bridge/ipc';
import { powerLifecycle, shell, windowApi, type PowerLifecycleState } from '@/bridge/api';
import { beginSharedPowerLifecycleReadback, getSharedPowerLifecycle, onSharedPowerLifecycle, isSharedPowerLifecycleAdmitted, type SharedPowerLifecycleSnapshot } from '@/bridge/powerLifecycleState';
import { getSettingsGeneration } from '@/bridge/settingsRepository';
import { getContinuousPerformanceSnapshot, updateContinuousPerformanceControls } from '@/bridge/performanceSchedule';
import { getTdpExecutionCapability } from '@/bridge/yeman';
import type { ContinuousPerformanceSnapshot } from '@/bridge/performanceControls';
import { emptyTdp, findRadialItem, type RadialSettings, type TdpDraft } from './model';
import { loadRadialSettings } from './settings';
import { emptyRadialUtilities, isUtilityGroup, type RadialUtilities, type RadialUtilityGroup } from './utilities';
import { createLocalRadialUtilityActions } from './utilitiesHost';
import { emptyRadialInput, isRadialInputField, type RadialInputFrame } from './input';
import { createLocalRadialInputActions } from './inputHost';
import { emptyRadialFan, isRadialFanChoice, type RadialFanFrame } from './fan';
import { createLocalRadialFanActions } from './fanHost';
import { onFanMirrorDisplay, onFanMirrorUiGate } from '@/bridge/deckyFanDisplay';
import { emptyRadialPerformance, radialPerformanceFrame, applyRadialPerformance, type RadialPerformanceFrame, type RadialPerformanceIntent } from './performance';
interface Identity {wheelId:number;nonce:string}
interface PowerPeriod {powerGeneration:number;powerSuspendSerial:number}
interface SummonRead extends PowerPeriod {controller:AbortController}
interface SummonRequest extends Partial<PowerPeriod> {source?:string;x?:number;y?:number;issuedAt?:number;expiresAt?:number}
interface Session extends Identity, PowerPeriod {settings:RadialSettings;generation:number;tdp:TdpDraft;performance:RadialPerformanceFrame;fan:RadialFanFrame;powerRead:AbortController;input:RadialInputFrame;utilities:RadialUtilities;snapshot?:ContinuousPerformanceSnapshot;pending:boolean;seen:Set<string>}
const periodAdmitted=(period:PowerPeriod,power:SharedPowerLifecycleSnapshot)=>isSharedPowerLifecycleAdmitted(power)&&!!power.inputReady&&
  power.generation===period.powerGeneration&&power.suspendSerial===period.powerSuspendSerial;
const newerPeriod=(next:PowerPeriod,prior:PowerPeriod)=>next.powerGeneration>=prior.powerGeneration&&next.powerSuspendSerial>=prior.powerSuspendSerial&&
  (next.powerGeneration>prior.powerGeneration||next.powerSuspendSerial>prior.powerSuspendSerial);
/** Main-document action owner. The overlay imports neither this file nor the
 * scheduler/settings tree, so there is only one performance scheduling owner. */
export function startRadialHost(router:Router){
  if (!isRadialTestingEnabled()) return { stop() {} };
  let ended=false,epoch=0,opening:SummonRead|null=null,summonProbe:SummonRead|null=null,session:Session|null=null;
  let observedNotificationSequence:number|undefined;
  // The host can remain admitted while the wheel is closed. Only an actual
  // session owns the existing two-second keep-alive and its pending read.
  let heartbeat:ReturnType<typeof setInterval>|null=null;
  let heartbeatRequest:{session:Session;controller:AbortController}|null=null;
  function stopHeartbeat(){
    if(heartbeat!==null)clearInterval(heartbeat);
    heartbeat=null;
    const request=heartbeatRequest;heartbeatRequest=null;
    request?.controller.abort();
  }
  function startHeartbeat(s:Session){
    stopHeartbeat();
    heartbeat=setInterval(()=>{
      if(ended||session!==s||heartbeatRequest)return;
      const request={session:s,controller:new AbortController()};heartbeatRequest=request;
      void invoke<boolean>('radial.keepAlive',identity(s),{timeoutMs:2000,signal:request.controller.signal}).then(ok=>{
        if(ended||session!==s)return;
        if(!ok){void close();return;}
        void refreshFan(s);void refreshInput(s);void refreshUtility(s,'monitor.templates');void refreshUtility(s,'steam.controls');
      }).catch(()=>{if(!ended&&session===s)void close();}).finally(()=>{
        if(heartbeatRequest===request)heartbeatRequest=null;
      });
    },2000);
  }

  const fanActions=createLocalRadialFanActions(()=>!ended);
  const inputActions=createLocalRadialInputActions(()=>!ended);
  const utilityActions=createLocalRadialUtilityActions(()=>!ended);
  const utilityCheckpoint=(s:Session,expiresAt?:number)=>{if(ended||session!==s||s.generation!==getSettingsGeneration()||expiresAt!==undefined&&expiresAt<=Date.now())throw Error('菜单操作会话已结束或过期');};
  async function refreshUtility(s:Session,group:RadialUtilityGroup,force=false){if(s.pending)return;const signal=s.powerRead.signal;
    try{const next=await utilityActions.read(group,()=>{checkPowerRead(signal);utilityCheckpoint(s);},force,load=>readPower(signal,load));
      if(session!==s||s.pending||next.sourceRevision===s.utilities[group].sourceRevision)return;s.utilities[group]=next;
    }catch(e){if(session!==s||signal.aborted)return;s.utilities[group]={...emptyRadialUtilities()[group],notice:(e as Error).message};}
    if(session===s&&!s.pending&&!signal.aborted)await invoke('radial.frame',{...identity(s),frame:frame(s)},{timeoutMs:2500}).catch(()=>{if(session===s&&!signal.aborted)void close();});
  }
  const inputCheckpoint=(s:Session,expiresAt?:number)=>{if(ended||session!==s||s.generation!==getSettingsGeneration()||expiresAt!==undefined&&expiresAt<=Date.now())throw Error('输入操作会话已结束或过期');};
  async function refreshInput(s:Session){if(s.pending)return;const signal=s.powerRead.signal;
    try{const next=await inputActions.read(()=>{checkPowerRead(signal);inputCheckpoint(s);},load=>readPower(signal,load));
      if(session!==s||s.pending||next.sourceRevision===s.input.sourceRevision)return;s.input=next;
    }catch(e){if(session!==s||signal.aborted)return;s.input={...emptyRadialInput(),notice:(e as Error).message};}
    if(session===s&&!s.pending&&!signal.aborted)await invoke('radial.frame',{...identity(s),frame:frame(s)},{timeoutMs:2500}).catch(()=>{if(session===s&&!signal.aborted)void close();});
  }
  const fanCheckpoint=(s:Session,expiresAt?:number)=>{if(ended||session!==s||s.generation!==getSettingsGeneration()||expiresAt!==undefined&&expiresAt<=Date.now())throw Error('风扇操作会话已结束或过期');};
  const powerChanged=()=>Error('电源状态已变化，请重新操作。');
  const checkPowerRead=(signal:AbortSignal)=>{if(signal.aborted)throw powerChanged();};
  async function readPower<T>(signal:AbortSignal,load:(signal:AbortSignal)=>Promise<T>):Promise<T>{
    checkPowerRead(signal);
    let abort!:()=>void;
    const retired=new Promise<never>((_,reject)=>{abort=()=>reject(powerChanged());});
    signal.addEventListener('abort',abort,{once:true});
    try{const value=await Promise.race([retired,load(signal)]);checkPowerRead(signal);return value;}
    finally{signal.removeEventListener('abort',abort);}
  }
  async function refreshFan(s:Session){if(s.pending)return;try{const next=await fanActions.read(()=>fanCheckpoint(s));if(session!==s||s.pending||next.sourceRevision===s.fan.sourceRevision)return;s.fan=next;}catch(e){if(session!==s)return;s.fan={...emptyRadialFan(),notice:(e as Error).message};}
    if(session===s&&!s.pending)await invoke('radial.frame',{...identity(s),frame:frame(s)},{timeoutMs:2500}).catch(()=>{if(session===s)void close();});
  }
  const frame=(s:Session)=>({settings:s.settings,tdp:s.tdp,performance:s.performance,fan:s.fan,input:s.input,utilities:s.utilities,feedback:s.settings.feedback});
  const matches=(data:Identity)=>!!session&&session.wheelId===data.wheelId&&session.nonce===data.nonce;
  const identity=(s:Identity)=>({wheelId:s.wheelId,nonce:s.nonce});
  function retireSession(){const s=session;session=null;stopHeartbeat();s?.powerRead.abort();return s;}
  async function closeSession(){const s=retireSession();if(s)await invoke('radial.close',identity(s),{timeoutMs:2500}).catch(()=>undefined);}
  function retireOpening(){++epoch;const old=opening;opening=null;old?.controller.abort();}
  function retireSummonProbe(){const old=summonProbe;summonProbe=null;old?.controller.abort();}
  async function close(){retireOpening();retireSummonProbe();await closeSession();}
  async function configure(){if(!isNativeRuntime||ended)return;const value=await loadRadialSettings();if(ended)return;await invoke('radial.configure',{enabled:value.enabled,monitorHold:value.monitorHold,holdMs:value.holdMs,fullScreenHold:value.fullScreenHold},{timeoutMs:2500});if(session)await close();}
  async function loadTdp(s:Session){
    try{const capability=await getTdpExecutionCapability(true),snapshot=await getContinuousPerformanceSnapshot('effective');if(ended||session!==s)return;
      s.snapshot=snapshot;s.performance=radialPerformanceFrame(snapshot,capability,crypto.randomUUID());const min=Math.max(snapshot.hardwareMinimum,capability.minPL1),max=Math.min(snapshot.hardwareMaximum,capability.maxPL1,snapshot.bank?.tdpCeiling??0);
      s.tdp={available:capability.ready&&!!snapshot.side&&!!snapshot.bank&&min<=max,min,max,value:snapshot.bank?.tdp??min,label:(snapshot.identity.scope==='dedicated'?(snapshot.gameName||'游戏专属'):'全局')+' · '+(snapshot.side==='ac'?'接电':'电池'),reason:capability.ready?'当前电源或游戏策略不可调节':capability.error||'TDP 写入通道未就绪'};
    }catch(e){if(session!==s)return;s.tdp={...emptyTdp(),label:'TDP 不可用',reason:(e as Error).message};s.performance={...emptyRadialPerformance(),feedback:(e as Error).message};}
    if(session===s)await invoke('radial.frame',{...identity(s),frame:frame(s)},{timeoutMs:2500}).catch(()=>{if(session===s)void close();});
  }
  async function summon(data:SummonRequest){
    if(ended||!isNativeRuntime||!Number.isSafeInteger(data.issuedAt)||!Number.isSafeInteger(data.expiresAt)||data.issuedAt!>Date.now()+500||data.expiresAt!<=Date.now()||data.expiresAt!-data.issuedAt!>5000||
      !Number.isSafeInteger(data.powerGeneration)||data.powerGeneration!<=0||!Number.isSafeInteger(data.powerSuspendSerial)||data.powerSuspendSerial!<0)return;
    const period={powerGeneration:data.powerGeneration!,powerSuspendSerial:data.powerSuspendSerial!};
    if(session?.powerGeneration===period.powerGeneration&&session.powerSuspendSerial===period.powerSuspendSerial){
      if(data.source==='shortcut'||data.source==='settings'||!data.source)await close();return;
    }
    const sharedAdmits=()=>{const shared=getSharedPowerLifecycle();return !!shared&&periodAdmitted(period,shared);};
    let confirmed:PowerLifecycleState|undefined;
    if(opening||summonProbe){
      if((opening&&!newerPeriod(period,opening))||(summonProbe&&!newerPeriod(period,summonProbe)))return;
      // A Native summon can arrive before its Ready event. Only a matching
      // readback accepted by the shared source can displace the old opening.
      retireSummonProbe();const probe=summonProbe={...period,controller:new AbortController()};
      const accept=beginSharedPowerLifecycleReadback();
      try{
        confirmed=await readPower(probe.controller.signal,signal=>powerLifecycle.get(undefined,signal));
        if(ended||summonProbe!==probe||data.expiresAt!<=Date.now()||!periodAdmitted(period,confirmed))return;
        accept(confirmed);
        if(summonProbe!==probe||opening||!sharedAdmits())return;
      }catch{return;}
      finally{if(summonProbe===probe)summonProbe=null;}
    }
    const work=opening={...period,controller:new AbortController()};const ticket=++epoch;
    const accept=beginSharedPowerLifecycleReadback();
    try{const [settings,power]=await Promise.all([loadRadialSettings(),confirmed??readPower(work.controller.signal,signal=>powerLifecycle.get(undefined,signal))]);
      if(ended||ticket!==epoch||!settings.enabled||!periodAdmitted(period,power)||data.expiresAt!<=Date.now())return;
      if(!confirmed)accept(power);
      if(ticket!==epoch||!sharedAdmits())return;
      if(session)await closeSession();
      if(ended||ticket!==epoch||!sharedAdmits()||data.expiresAt!<=Date.now())return;
      const nonce=crypto.randomUUID(),s:Session={wheelId:0,nonce,settings,generation:getSettingsGeneration(),...period,tdp:emptyTdp(),performance:emptyRadialPerformance(),fan:emptyRadialFan(),powerRead:new AbortController(),input:emptyRadialInput(),utilities:emptyRadialUtilities(),pending:false,seen:new Set()};
      const source=data.source==='monitor'?'monitor':data.source==='fullscreen'?'fullscreen':data.source==='fullscreen-raw'?'fullscreen-raw':'shortcut';
      const receipt=await invoke<{id:number;nonce:string}>('radial.create',{nonce,source,...period,x:Number.isFinite(data.x)?Math.round(data.x!):0,y:Number.isFinite(data.y)?Math.round(data.y!):0,frame:frame(s)},{timeoutMs:7000});
      if(!Number.isSafeInteger(receipt.id)||receipt.id<1||receipt.nonce!==nonce)throw new Error('轮盘窗口回执无效');s.wheelId=receipt.id;
      if(ended||ticket!==epoch){await invoke('radial.close',identity(s));return;}session=s;startHeartbeat(s);void loadTdp(s);void refreshFan(s);void refreshInput(s);utilityActions.resetSession();void refreshUtility(s,'monitor.templates');void refreshUtility(s,'steam.controls',true);
    }catch(e){if(!work.controller.signal.aborted)console.warn('[radial] summon',e);}
    finally{if(opening===work)opening=null;work.controller.abort();}
  }
  async function readActionPower(s:Session,signal:AbortSignal,load:(signal:AbortSignal)=>Promise<PowerLifecycleState>){
    const accept=beginSharedPowerLifecycleReadback();
    const power=await readPower(signal,load);
    // Native can observe the next boundary before the window delivers its event.
    accept(power);checkPowerRead(signal);
    const shared=getSharedPowerLifecycle();
    if(!shared||!periodAdmitted(s,shared)||!periodAdmitted(s,power)||power.observedNotificationSequence!==shared.observedNotificationSequence)throw powerChanged();
    return power;
  }
  async function checkpoint(s:Session,signal:AbortSignal){if(ended||session!==s||s.generation!==getSettingsGeneration())throw new Error('会话或设置已变化，请重新呼出。');
    await readActionPower(s,signal,signal=>powerLifecycle.get(undefined,signal));
    if(session!==s)throw new Error('菜单已经关闭。');
    const current=await readPower(signal,loadRadialSettings);
    if(ended||session!==s||s.generation!==getSettingsGeneration()||current.revision!==s.settings.revision||!current.enabled)throw new Error('菜单设置已变化，请重新呼出。');
  }
  async function execute(data:Identity&{intent:unknown}){
    if(!matches(data))return;const s=session!,v=data.intent as Record<string,unknown>;
    if(!v||typeof v!=='object'||typeof v.requestId!=='string'||v.requestId.length>100||s.seen.has(v.requestId)||s.pending)return;
    const id=v.requestId;s.seen.add(id);if(s.seen.size>64){await close();return;}
    const result=(ok:boolean,message:string,extra:Record<string,unknown>={})=>invoke('radial.result',{...identity(s),result:{requestId:id,ok,message,...extra}},{timeoutMs:2500});
    let refreshRetiredAction:(()=>void)|undefined;
    s.pending=true;
    try{
      if(!Number.isSafeInteger(v.issuedAt)||!Number.isSafeInteger(v.expiresAt)||Number(v.issuedAt)>Date.now()+500||Number(v.expiresAt)<=Date.now()||Number(v.expiresAt)<=Number(v.issuedAt)||Number(v.expiresAt)-Number(v.issuedAt)>5000||v.revision!==s.settings.revision||typeof v.itemId!=='string')throw new Error('菜单操作已过期。');
      const item=findRadialItem(s.settings,v.itemId);const parent=s.settings.pages.flatMap(p=>p.items).find(p=>p.children?.some(c=>c.id===v.itemId));
      if(!item||!item.enabled||item.action==='none'||item.action==='submenu'||(parent&&(!parent.enabled||parent.action!=='submenu')))throw new Error('动作已停用。');
      const signal=s.powerRead.signal,sequence=observedNotificationSequence,expiresAt=Number(v.expiresAt);
      const scopeAdmitted=()=>{const shared=getSharedPowerLifecycle();return !ended&&s.generation===getSettingsGeneration()&&Date.now()<expiresAt&&!!shared&&periodAdmitted(s,shared)&&shared.observedNotificationSequence===sequence;};
      const actionAdmitted=()=>session===s&&!signal.aborted&&scopeAdmitted();
      const checkAdmission=()=>{if(!actionAdmitted())throw powerChanged();};
      const actionPower=(load:(signal:AbortSignal)=>Promise<PowerLifecycleState>)=>readActionPower(s,signal,load);
      await checkpoint(s,signal);checkAdmission();
      if(isUtilityGroup(item.action)){
        if(typeof v.utilityChoice!=='string'||typeof v.utilityRevision!=='string')throw Error('菜单选项请求无效');
        const group=item.action,observe=<T>(load:(signal?:AbortSignal)=>Promise<T>)=>readPower(signal,load);
        refreshRetiredAction=()=>{if(signal.aborted)void refreshUtility(s,group);};
        try{const outcome=await utilityActions.apply(group,{choice:v.utilityChoice,sourceRevision:v.utilityRevision},{generation:s.powerGeneration,checkpoint:()=>utilityCheckpoint(s,expiresAt),readPower:actionPower,checkAdmission,observe});
          if(session!==s)return;s.utilities[group]=outcome.frame;await result(true,outcome.notice,{utilities:s.utilities,saved:outcome.saved,applied:outcome.applied,pending:outcome.pending,proof:outcome.proof});return;
        }catch(error){if(session===s&&!signal.aborted){const next=await utilityActions.read(group,()=>utilityCheckpoint(s),false,observe).catch(()=>({...emptyRadialUtilities()[group],notice:(error as Error).message}));
          if(session===s&&!signal.aborted)s.utilities[group]=next;}throw error;}
      }
      if(item.action==='input.pad'||item.action==='input.gyro'){
        if(!isRadialInputField(v.inputField)||typeof v.inputValue!=='string'||typeof v.inputRevision!=='string')throw Error('输入选项请求无效');
        refreshRetiredAction=()=>{if(signal.aborted)void refreshInput(s);};
        try{const outcome=await inputActions.apply(item.action,{field:v.inputField,value:v.inputValue,sourceRevision:v.inputRevision},{generation:s.powerGeneration,checkpoint:checkAdmission,readPower:actionPower,readSettings:load=>readPower(signal,load),settlementCheckpoint:()=>inputCheckpoint(s)});
          if(session!==s)return;s.input=outcome.frame;await result(true,outcome.notice,{input:s.input,saved:outcome.saved,applied:outcome.applied,pending:outcome.pending===true});return;
        }catch(error){if(session===s&&!signal.aborted)s.input=await inputActions.read(()=>inputCheckpoint(s)).catch(()=>({...emptyRadialInput(),notice:(error as Error).message}));throw error;}
      }
      if(item.action==='fan'){
        if(!isRadialFanChoice(v.fanChoice)||typeof v.fanRevision!=='string')throw Error('风扇档位请求无效');
        try{const outcome=await fanActions.apply({choice:v.fanChoice,sourceRevision:v.fanRevision},{generation:s.powerGeneration,
          checkpoint:()=>{checkPowerRead(signal);fanCheckpoint(s,expiresAt);},readPower:actionPower});
          if(session!==s)return;s.fan=outcome.frame;await result(outcome.applied||outcome.saved||outcome.pending===true,outcome.frame.pending?outcome.notice:outcome.frame.notice,{fan:s.fan,saved:outcome.saved,applied:outcome.applied,pending:outcome.pending===true});return;
        }catch(error){if(session===s)s.fan=await fanActions.read(()=>fanCheckpoint(s)).catch(()=>({...emptyRadialFan(),notice:(error as Error).message}));throw error;}
      }
      if(item.action==='performance'){
        if(!s.snapshot||typeof v.control!=='string'||typeof v.controlRevision!=='string'||!['string','number','boolean'].includes(typeof v.value))throw Error('性能控件请求无效');
        const intent:RadialPerformanceIntent={key:v.control,value:v.value as RadialPerformanceIntent['value'],sourceRevision:v.controlRevision};
        const outcome=await applyRadialPerformance(s.snapshot,s.performance,intent,{
          read:()=>readPower(signal,()=>getContinuousPerformanceSnapshot('effective')),capability:()=>readPower(signal,()=>getTdpExecutionCapability(true)),checkpoint:()=>checkpoint(s,signal),
          admit:actionAdmitted,
          update:updateContinuousPerformanceControls,nextRevision:()=>crypto.randomUUID(),
        });
        if(session!==s)return;s.snapshot=outcome.snapshot;s.performance=outcome.frame;
        const tdp=outcome.frame.controls.find(control=>control.key==='tdp');if(tdp?.kind==='slider')s.tdp={...s.tdp,min:tdp.min,max:tdp.max,value:tdp.value};
        await result(outcome.ok,outcome.message,{performance:s.performance,tdp:s.tdp,saved:outcome.saved,applied:outcome.applied,proof:outcome.proof});return;
      }
      if(item.action==='tdp'){
        if(!s.snapshot||!s.tdp.available||typeof v.value!=='number'||!Number.isInteger(v.value)||v.value<s.tdp.min||v.value>s.tdp.max)throw new Error('当前 TDP 值超出设备或策略限值。');
        const capability=await readPower(signal,()=>getTdpExecutionCapability(true));if(!capability.ready||v.value<capability.minPL1||v.value>capability.maxPL1)throw new Error('TDP 通道或限值已变化。');
        await checkpoint(s,signal);const receipt=await updateContinuousPerformanceControls(s.snapshot.identity,{tdp:v.value},actionAdmitted);
        if(session!==s)return;s.snapshot=receipt;s.tdp.value=receipt.bank?.tdp??v.value;
        const proof=receipt.tdpWriteReceipt;await result(!!proof?.ok,proof?.ok?`TDP 已确认写入 ${proof.accepted} W`:'目标已保存；本次硬件写入尚未确认。',{tdp:s.tdp,proof:proof?.proof??'unconfirmed'});return;
      }
      const routes:Record<string,string>={'page.schedule':'/schedule','page.fan':'/fan','page.monitor':'/rtss','page.settings':'/settings','page.radial':'/radial'};
      if(item.action==='ymcc.show'||routes[item.action]){await result(true,'正在打开 YMCC');if(!actionAdmitted())return;await close();if(!scopeAdmitted())return;if(!await windowApi.show())throw new Error('YMCC 窗口未确认显示');if(routes[item.action]&&scopeAdmitted())await router.push(routes[item.action]);return;}
      let ok=false;
      if(item.action==='windows.thisPc')ok=await shell.execute('explorer.exe',['shell:MyComputerFolder']);
      else if(item.action==='windows.browser')ok=await shell.open('https://www.bing.com/');
      else if(item.action==='windows.taskView')ok=await shell.taskView();
      if(!ok)throw new Error('Windows 未确认接受操作。');await result(true,'已发送到 Windows');await close();
    }catch(e){if(session===s)await result(false,(e as Error).message,{fan:s.fan,input:s.input,utilities:s.utilities}).catch(()=>undefined);}
    finally{s.pending=false;if(session===s)refreshRetiredAction?.();}
  }
  const refreshFanDisplay=()=>{const s=session;if(s&&!s.pending)void refreshFan(s);};
  const offUtility=utilityActions.observe(group=>{const s=session;if(s&&!s.pending)void refreshUtility(s,group);});
  const offInput=inputActions.observe(()=>{const s=session;if(s&&!s.pending)void refreshInput(s);});
  const offFanDisplay=onFanMirrorDisplay(refreshFanDisplay),offFanGate=onFanMirrorUiGate(refreshFanDisplay);
  const offSummon=on<SummonRequest>('radial.summon-request',data=>{void summon(data);});
  const offIntent=on<Identity&{intent:unknown}>('radial.intent',data=>{void execute(data);});
  const offClosed=on<Identity>('radial.closed',data=>{if(matches(data))retireSession();});
  const offPower=[onSharedPowerLifecycle(snapshot=>{
    if(ended)return;
    const notificationChanged=observedNotificationSequence!==snapshot.observedNotificationSequence;
    observedNotificationSequence=snapshot.observedNotificationSequence;
    if(opening&&!periodAdmitted(opening,snapshot))retireOpening();
    if(session&&!periodAdmitted(session,snapshot))void closeSession();
    else if(session&&notificationChanged){const old=session.powerRead;session.powerRead=new AbortController();old.abort();}
  },{observeSuspendSerial:true,observeNotificationSequence:true}),on('power.sourceChanged',()=>{void close();})];
  const offPad=on<{connected?:boolean}>('gamepad.state',data=>{if(data.connected===false&&session)void close();});
  const settingsChanged=()=>{void configure().catch(e=>console.warn('[radial]',e));};window.addEventListener('ymcc:radial-settings-changed',settingsChanged);
  if(isNativeRuntime)void configure().catch(e=>console.warn('[radial]',e));
  return {stop(){if(ended)return;ended=true;utilityActions.stop();offUtility();inputActions.stop();offInput();fanActions.stop();offFanDisplay();offFanGate();offSummon();offIntent();offClosed();offPower.forEach(off=>off());offPad();stopHeartbeat();window.removeEventListener('ymcc:radial-settings-changed',settingsChanged);void close();}};
}
