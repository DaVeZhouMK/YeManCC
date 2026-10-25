// Thin mirror adapter only. All writes/start/stop are owned by YMCC quickapp.ts.
import { waitForDeckyLosslessAction, deckyLosslessText, type DeckyLosslessDiagnostic } from './deckyLosslessState';
import { powerLifecycle } from './api';
import { getLosslessGameState, setLosslessScalingEnabled, onLosslessScalingChanged } from './quickapp';
import { getPolicyGame } from './gamePolicyTarget';
import { gameMirrorIdentity } from './deckyGameActions';
import { tryAcquireQuickAction } from './quickActionLock';
import type { MirrorField } from '../../decky-plugin/src/mirrorClient';
import type { RelayMutationContext } from './deckyMirrorRelay';
import type { MirrorActionResult } from './deckyFanActions';
export { onLosslessScalingChanged };
export async function readLosslessScaling(gamePath:string){return getLosslessGameState(gamePath);}
export function losslessSwitchEnabled(document:LsDocument|null):boolean {
  return document?.running===true&&(document.enabled===true||(document.enabled===null&&document.configuredEnabled===true));
}
export function losslessField(document:Awaited<ReturnType<typeof readLosslessScaling>>|null,pending=false):MirrorField {
  // Decky shows ON only when the current game profile and LS process are both enabled.
  // UIAutomation is optional: a hidden LS window must still allow explicit control.
  const active=losslessSwitchEnabled(document);
  return {value:document ? document.running&&document.enabled===null?'unknown':active?'on':'off' : 'unknown',supported:!!document?.controlAvailable&&!pending&&!document.pending,
    choices:[{data:'off',label:'关闭'},{data:'on',label:'开启'}]};
}
export type LsDocument=Awaited<ReturnType<typeof readLosslessScaling>>;
export async function setLosslessFromMirror(args:Record<string,unknown>,context:RelayMutationContext,diagnostic?:DeckyLosslessDiagnostic):Promise<MirrorActionResult> {
  if(Object.keys(args).length!==2||args.field!=='losslessScaling'||!['on','off'].includes(String(args.value)))throw new Error('LOSSLESS_INVALID_REQUEST');
  const game=getPolicyGame(),identity=gameMirrorIdentity(game);
  if(!game||!game.path||!game.processCreated||!Number.isSafeInteger(game.pid)||game.pid<=0||identity!==context.gameIdentity||typeof context.gameAdmission?.lossless!=='string')throw new Error('LOSSLESS_GAME_UNCONFIRMED');
  const release=tryAcquireQuickAction('decky-lossless-mirror');if(!release)throw new Error('LOSSLESS_BUSY');
  const outcome=await waitForDeckyLosslessAction(async budgetCheckpoint=>{
  const checkpoint=()=>{budgetCheckpoint();if(gameMirrorIdentity(getPolicyGame())!==identity)throw new Error('LOSSLESS_GAME_CHANGED');};
  try {
    checkpoint();const power=await powerLifecycle.get();checkpoint();
    if(power.generation!==context.generation||power.phase!=='ready'||!power.hardwareWritesAllowed)throw new Error('LOSSLESS_POWER_NOT_READY');
    const document=await readLosslessScaling(game.path);checkpoint();
    if(document.xml!==context.gameAdmission!.lossless)throw new Error('GAME_MIRROR_SOURCE_CHANGED');
    if(!document.controlAvailable)throw new Error('LOSSLESS_CONTROL_UNAVAILABLE');
    const currentPower=await powerLifecycle.get();checkpoint();
    if(currentPower.generation!==context.generation||currentPower.phase!=='ready'||!currentPower.hardwareWritesAllowed)throw new Error('LOSSLESS_POWER_NOT_READY');
    // Do not build/patch an LS Profile, toggle hotkeys or invoke UI controls here.
    // The same original entry used by YMCC owns the real action and receipts.
    const result=await setLosslessScalingEnabled(game.path,args.value==='on',checkpoint,game);checkpoint();
    return {saved:false,applied:result.confirmed,pending:result.pending??!result.confirmed,notice:deckyLosslessText(result.notice)};
  }finally{release();}
  },context.checkpoint,diagnostic,15000);
  return outcome.pending===true?{saved:false,applied:false,pending:true,notice:outcome.expired?'LosslessScaling 自愈操作 40 秒内未获确认；已停止追加重试，可继续使用其他设置':'LosslessScaling 正在后台自愈；最多自动重试 1 次，完成后更新状态'}:outcome.result;
}
