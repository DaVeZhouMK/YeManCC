// Consume the original trainer owner's confirmed cache and existing-file launcher.
// Launcher receipts can wait for UAC; keep that wait local to the trainer button.
import { gameMirrorIdentity } from './deckyGameActions';
import type { DetectedGame } from './gamedetect';
import type { InstalledGameTrainer } from './gameTrainer';
import type { PowerLifecycleState } from './api';
import type { RelayMutationContext } from './deckyMirrorRelay';
import type { MirrorSnapshot } from '../../decky-plugin/src/mirrorClient';

export function trainerMirrorSource(trainer: InstalledGameTrainer | null): string {
  // modified is computed at lookup time by the owner, so it is not admission data.
  return JSON.stringify(trainer ? [trainer.gameName, trainer.origin, trainer.folder, trainer.path, trainer.sha256 ?? ''] : null);
}

export function createTrainerMirrorActions(deps: {
  allowed: () => boolean; game: () => DetectedGame | null; label: (game: DetectedGame) => string;
  ready: (name: string, path: string) => InstalledGameTrainer | null;
  power: () => Promise<PowerLifecycleState>; busy: () => boolean; invalidate: () => void;
  launch: (game: DetectedGame, isTargetCurrent: () => Promise<boolean>) => Promise<unknown>;
}) {
  let ended = false;
  let active: object | null = null;
  let outcome: { identity: string; source: string; notice: string } | null = null;
  const allowed = () => !ended && deps.allowed();
  return {
    view(game: DetectedGame | null, trainer: InstalledGameTrainer | null): NonNullable<MirrorSnapshot['trainer']> {
      const identity = gameMirrorIdentity(game), source = trainerMirrorSource(trainer);
      return { available: !!game && !!game.path && Number.isSafeInteger(game.pid) && game.pid > 0 && !!game.processCreated && !!trainer, pending: !!active,
        notice: outcome?.identity === identity && outcome.source === source ? outcome.notice : '' };
    },
    stop() { ended = true; outcome = null; },
    async execute(args: Record<string, unknown>, context: RelayMutationContext) {
      if (Object.keys(args).length !== 2 || args.field !== 'trainer' || args.value !== 'open') throw Error('TRAINER_MIRROR_INVALID_REQUEST');
      const selected = deps.game();
      if (!allowed() || !selected || !selected.path || !Number.isSafeInteger(selected.pid) || selected.pid <= 0 || !selected.processCreated ||
        !context.gameIdentity || gameMirrorIdentity(selected) !== context.gameIdentity) throw Error('TRAINER_MIRROR_TARGET_CHANGED');
      if (active || deps.busy()) throw Error('TRAINER_MIRROR_BUSY');
      const target = structuredClone(selected), name = deps.label(target), source = context.controlAdmission?.trainer;
      if (typeof source !== 'string' || source === 'null') throw Error('TRAINER_MIRROR_NOT_FOUND');
      const checkpoint = () => {
        context.checkpoint();
        if (!allowed() || gameMirrorIdentity(deps.game()) !== context.gameIdentity) throw Error('TRAINER_MIRROR_TARGET_CHANGED');
        const trainer = deps.ready(name, target.path);
        if (!trainer || trainerMirrorSource(trainer) !== source) throw Error('TRAINER_MIRROR_SOURCE_CHANGED');
      };
      const isTargetCurrent = async () => {
        try {
          checkpoint();
          const power = await deps.power();
          checkpoint();
          return power.generation === context.generation && power.phase === 'ready' && power.hardwareWritesAllowed === true;
        } catch { return false; }
      };
      checkpoint();
      const token = {}; active = token; outcome = null;
      try {
        if (!await isTargetCurrent()) throw Error('TRAINER_MIRROR_CONTEXT_CHANGED');
        if (deps.busy()) throw Error('TRAINER_MIRROR_BUSY');
        checkpoint();
        // The established launcher shares its deduplication map with the YMCC menu,
        // validates files/hash/current target, and owns the bounded receipt wait.
        void Promise.resolve().then(() => { checkpoint(); return deps.launch(target, isTargetCurrent); }).then(
          (receipt) => { if (!ended) outcome = { identity: context.gameIdentity!, source, notice: receipt && typeof receipt === 'object' && 'notice' in receipt && typeof receipt.notice === 'string' ? receipt.notice : '已启动当前游戏修改器' }; },
          () => { if (!ended) outcome = { identity: context.gameIdentity!, source, notice: '修改器启动未确认，请在 YMCC 查看；不会自动下载或重试' }; },
        ).finally(() => { if (active === token) active = null; if (!ended) deps.invalidate(); });
        deps.invalidate();
        return { saved: false, applied: false, pending: true, notice: '修改器正在启动，等待启动确认' };
      } catch (error) { if (active === token) active = null; if (!ended) deps.invalidate(); throw error; }
    },
  };
}
