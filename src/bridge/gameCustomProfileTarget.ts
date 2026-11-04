import { gamePolicyKey } from './gamePolicyHysteresis';
import type { DetectedGame } from './gamedetect';

/** Carry the complete observed identity; never refresh an old action onto a new process. */
export function captureGameCustomProfileTarget(game: DetectedGame, generation: number) {
  return {
    exe: gamePolicyKey(game), pid: game.pid, processCreated: String(game.processCreated || ''),
    path: game.path, identityGeneration: game.identityGeneration, generation,
  };
}
