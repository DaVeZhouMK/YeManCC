// Two carriers remain subroutes of ONE YMCC output. Default desktop visibility
// never queries Steam. Steam preference is explicit; an unknown close is not a hide.
import { normalizeMonitorOutput, type MonitorOutputPreferences } from './monitorHudModel';
import type { OutputAck } from './monitorOutputCoordinator';
export interface HudCarrierPort {available():Promise<boolean>|boolean;visible():boolean;state?():'shown'|'hidden'|'unknown';setVisible(show:boolean,prefs:MonitorOutputPreferences):Promise<OutputAck>;}
export function createHudCarrierRouter(steam:HudCarrierPort,desktop:HudCarrierPort,policy:'steam-first'|'desktop-global'='steam-first'){
  let queue:Promise<unknown>=Promise.resolve(),active:'steam'|'desktop'|null=null,steamTouched=false;
  let preferences=normalizeMonitorOutput();
  const portState=(port:HudCarrierPort)=>port.state?.()??(port.visible()?'shown':'hidden');
  async function kind(prefs:MonitorOutputPreferences=preferences){
    if(policy==='desktop-global'){
      if(prefs.ymccCarrier==='steam-preferred'){
        try{if(await steam.available()&&portState(steam)!=='unknown')return 'steam';}catch{/* Unavailable Steam must not veto a usable desktop. */}
      }
      return await desktop.available()?'desktop':null;
    }
    return await steam.available()?'steam':await desktop.available()?'desktop':null;
  }
  async function apply(show:boolean,raw:MonitorOutputPreferences):Promise<OutputAck>{
    preferences=normalizeMonitorOutput(raw);
    const destination=show?await kind(preferences):null;
    if(show&&!destination)return {confirmed:false,reason:'原生监控输出承载暂不可用。'};
    // An untouched/unknown Steam plugin cannot veto the default global desktop.
    // Once this owner attempted Steam, it must reclaim that exact carrier first.
    const includeSteam=policy==='steam-first'||steamTouched||destination==='steam';
    if(includeSteam)steamTouched=true;
    const carriers:ReadonlyArray<readonly ['steam'|'desktop',HudCarrierPort]>=includeSteam?[['steam',steam],['desktop',desktop]]:[['desktop',desktop]];
    for(const [name,carrier]of carriers){
      const ack=await carrier.setVisible(false,preferences);
      if(!ack.confirmed)return {confirmed:false,reason:ack.reason??name+' 旧窗口关闭未确认。'};
      if(name==='steam')steamTouched=false;
      if(active===name)active=null;
    }
    if(!show)return {confirmed:true};
    const current=await kind(preferences);
    // A disappeared Steam binding should fall back to the globally usable desktop,
    // not turn a ready desktop into a game prerequisite. Other changes stay guarded.
    const selected=policy==='desktop-global'&&destination==='steam'&&current==='desktop'?'desktop':destination;
    if(current!==selected)return {confirmed:false,reason:'监控承载在切换中改变；没有开启新窗口。'};
    if(selected==='steam')steamTouched=true;
    const ack=await (selected==='steam'?steam:desktop).setVisible(true,preferences);
    if(ack.confirmed){active=selected;return ack;}
    if(policy!=='desktop-global'||selected!=='steam')return ack;
    // A failed create may still have made a real Steam popup. Never forge closure.
    const closed=await steam.setVisible(false,preferences);
    if(!closed.confirmed)return {confirmed:false,reason:'Steam HUD 失败且旧窗口关闭未确认；未叠开桌面窗口。'+(closed.reason??'')};
    steamTouched=false;
    if(!await desktop.available())return {confirmed:false,reason:'Steam HUD 未显示，桌面载体也不可用。'};
    const fallback=await desktop.setVisible(true,preferences);
    if(fallback.confirmed){active='desktop';return {confirmed:true,reason:'Steam 内部 HUD 未确认显示，已确认关闭并恢复全局桌面 HUD。'};}
    return {confirmed:false,reason:fallback.reason??'Steam HUD 未显示，桌面恢复未确认。'};
  }
  function state():'shown'|'hidden'|'unknown'{
    const values=(policy==='desktop-global'&&!steamTouched&&active!=='steam'?[desktop]:[steam,desktop]).map(portState);
    if(values.includes('unknown')||values.filter(v=>v==='shown').length>1)return 'unknown';
    return values.includes('shown')?'shown':'hidden';
  }
  return {available:async(prefs?:MonitorOutputPreferences)=>!!await kind(prefs),kind,state,
    visible:()=>policy==='desktop-global'&&!steamTouched&&active!=='steam'?desktop.visible():steam.visible()||desktop.visible(),active:()=>active,
    setVisible(show:boolean,prefs:MonitorOutputPreferences){const snapshot=normalizeMonitorOutput(prefs);const result=queue.then(()=>apply(show,snapshot));queue=result.catch(()=>{});return result;}};
}
