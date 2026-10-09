// Thin mirror adapter only. All writes/start/stop are owned by YMCC quickapp.ts.
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
export function losslessField(document:Awaited<ReturnType<typeof readLosslessScaling>>|null):MirrorField {
  return {value:document?.enabled===null?'unknown':document?.enabled?'on':'off',supported:!!document?.profile&&document.enabled!==null,
    choices:[{data:'off',label:'关闭'},{data:'on',label:'开启'}]};
}
export type LsDocument=Awaited<ReturnType<typeof readLosslessScaling>>;
export async function setLosslessFromMirror(args:Record<string,unknown>,context:RelayMutationContext):Promise<MirrorActionResult> {
  if(Object.keys(args).length!==2||args.field!=='losslessScaling'||!['on','off'].includes(String(args.value)))throw new Error('LOSSLESS_INVALID_REQUEST');
  const game=getPolicyGame(),identity=gameMirrorIdentity(game);
  if(!game||!game.path||!game.processCreated||!Number.isSafeInteger(game.pid)||game.pid<=0||identity!==context.gameIdentity||typeof context.gameAdmission?.lossless!=='string')throw new Error('LOSSLESS_GAME_UNCONFIRMED');
  const release=tryAcquireQuickAction('decky-lossless-mirror');if(!release)throw new Error('LOSSLESS_BUSY');
  const checkpoint=()=>{context.checkpoint();if(gameMirrorIdentity(getPolicyGame())!==identity)throw new Error('LOSSLESS_GAME_CHANGED');};
  try {
    checkpoint();const power=await powerLifecycle.get();checkpoint();
    if(power.generation!==context.generation||power.phase!=='ready'||!power.hardwareWritesAllowed)throw new Error('LOSSLESS_POWER_NOT_READY');
    const document=await readLosslessScaling(game.path);checkpoint();
    if(!document.profile||document.xml!==context.gameAdmission!.lossless)throw new Error('GAME_MIRROR_SOURCE_CHANGED');
    if(document.enabled===null)throw new Error('LOSSLESS_STATE_UNCONFIRMED');
    const currentPower=await powerLifecycle.get();checkpoint();
    if(currentPower.generation!==context.generation||currentPower.phase!=='ready'||!currentPower.hardwareWritesAllowed)throw new Error('LOSSLESS_POWER_NOT_READY');
    // Do not build/patch an LS Profile, toggle hotkeys or invoke UI controls here.
    // The same original entry used by YMCC owns the real action and receipts.
    const result=await setLosslessScalingEnabled(game.path,args.value==='on',checkpoint);checkpoint();
    return {saved:false,applied:result.confirmed,pending:!result.confirmed,notice:result.notice};
  }finally{release();}
}
