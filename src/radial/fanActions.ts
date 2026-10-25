import type { FanNode } from '@/bridge/fanApi';
import type { FanPreset } from '@/bridge/fanFeature';
import { createFanMirrorActions, type FanMirrorDeps, type MirrorMutationContext } from '@/bridge/deckyFanActions';
import type { FanMirrorDisplay } from '@/bridge/deckyFanDisplay';
import { emptyRadialFan, isRadialFanChoice, type RadialFanFrame, type RadialFanIntent } from './fan';
type FanSource=Record<string,unknown>;
export interface RadialFanDeps extends Omit<FanMirrorDeps,'curve'|'save'|'display'> {
  read:()=>Promise<FanSource>;
  curves:(source:FanSource,preset:FanPreset)=>FanNode[];
  save:(preset:FanPreset,expected:FanSource)=>Promise<void>;
  display:FanMirrorDeps['display'];
  currentDisplay:()=>FanMirrorDisplay|null;
  nextRevision:()=>string;
}
/** A bounded adapter of the existing four-choice action, never a fan owner. */
export function createRadialFanActions(deps:RadialFanDeps) {
  let commandSource:FanSource={},fingerprint='',frame=emptyRadialFan(),stopped=false,applying=false;
  const actions=createFanMirrorActions({...deps,curve:preset=>deps.curves(commandSource,preset),
    save:(_nodes,preset)=>deps.save(preset,commandSource)});
  async function read(checkpoint:()=>void=()=>{}) {
    checkpoint();const source=await deps.read();checkpoint();if(stopped)throw Error('轮盘风扇控制已结束');
    const active=actions.pendingIntent||deps.enabled()||deps.owner.hasControlIntent,display=deps.currentDisplay();
    const confirmed=active&&deps.enabled()&&deps.owner.hasControlIntent&&deps.owner.controlReady
      &&deps.owner.state==='ready'&&!deps.owner.recoveryActive&&!deps.owner.recoveryNeedsAttention;
    const pending=actions.pendingIntent||deps.owner.recoveryActive||display?.pending===true&&!confirmed;
    const available=deps.featureAllowed()&&source.featureEnabled!==false,busy=deps.uiBusy();
    const value:RadialFanFrame={sourceRevision:'',available,busy,choice:active?deps.currentPreset():'off',active,pending,
      confirmed:confirmed&&!pending,notice:!available?'风扇控制当前不可用':busy?'请先完成风扇页面中的操作':display?.notice||
        (confirmed?'YMCC 已确认风扇控制':pending?'等待 YMCC 确认风扇状态':'风扇控制未启用')};
    const next=JSON.stringify([source.preset,source.nodes,source.presetCurves,source.featureEnabled,
      deps.owner.controlIntentRevision,deps.owner.controlCancellationRevision,value]);
    const changed=next!==fingerprint;
    if(changed){fingerprint=next;frame={...value,sourceRevision:deps.nextRevision()};}
    return {frame:{...frame},source,changed};
  }
  return {
    async read(checkpoint?:()=>void){return (await read(checkpoint)).frame;},
    async apply(intent:RadialFanIntent,context:MirrorMutationContext) {
      if(stopped||applying)throw Error('风扇操作正在处理，请稍后重试');
      if(!intent||!isRadialFanChoice(intent.choice)||typeof intent.sourceRevision!=='string')throw Error('风扇档位无效');
      applying=true;
      try {
        const current=await read(context.checkpoint);
        if(!current.frame.available||current.frame.busy)throw Error(current.frame.notice);
        if(!intent.sourceRevision||intent.sourceRevision!==current.frame.sourceRevision)throw Error('风扇状态已变化，请重新选择');
        commandSource=structuredClone(current.source);
        const outcome=await actions.request('fan.setChoice',{choice:intent.choice},context);
        // Pending is the original owner's accepted intent, not a hardware receipt.
        const refreshed=await read(context.checkpoint);
        return {...outcome,frame:refreshed.frame};
      } finally {applying=false;}
    },
    stop(){if(stopped)return;stopped=true;actions.stop();},
  };
}
