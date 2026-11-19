// Read original global input settings for the mirror. No writes or hardware API.
import { readSettingsSection } from './settingsRepository';
import type { GameMirrorGyroDefaults } from './deckyGameActions';
// Display shares only this transaction's original input read; actions remain fresh.
export async function readGameMirrorGyroDefaults(readInput:()=>ReturnType<typeof readSettingsSection>=()=>readSettingsSection('input')): Promise<GameMirrorGyroDefaults> {
  const input=await readInput();
  const motion=input.gyroMotion||{}, target=input.outputTarget||{};
  const preset=['fps','racing','custom','steam'].includes(motion.preset)?motion.preset:'fps';
  const virtualPad=typeof target.persona==='string' && target.persona!=='disabled';
  const enabled=virtualPad && target.gyroEnabled===true && motion.enabled===true;
  const padPersona=['steamdeck','dualsense-edge','elite','disabled'].includes(target.persona)?target.persona:'disabled';
  return {preset,enabled,virtualPad,padPersona,source:JSON.stringify({persona:target.persona,gyroEnabled:target.gyroEnabled,motionEnabled:motion.enabled,preset})};
}
