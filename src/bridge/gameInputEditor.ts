import { snapshotSettingsData } from './settingsSnapshot';
import { loadSettings, compareAndSwapInputSettings, getSettingsGeneration, assertSettingsGeneration, mergeSettings,
  type InputSettingsSnapshot, type SettingsCasResult } from './settingsRepository';
import { loadGameCustomConfig, onGameCustomConfigChanged, patchGameCustomProfile, gameProfileInputRevision,
  type GameCustomConfig, type GameCustomProfile, type GameCustomProfileTarget } from './performanceSchedule';
import { detectPolicyGame, getPolicyGame, gamePolicyKey, subscribePolicyGameStatus } from './gamePolicyTarget';
import { buildGameInputOverlay } from './gameInputOverride';
import { detectedGameName } from './gamedetect';

/** The ordinary pages and the Y menu edit the same per-game record. */
export interface GameInputEditor {
  input: InputSettingsSnapshot;
  target: GameCustomProfileTarget;
  profile?: GameCustomProfile;
  gameName: string;
  ownerEpoch: number;
}
let epoch = 0;
let started = false;
let ownerEpoch = 0;
let ownerSignature = '';
let latestConfig: GameCustomConfig | undefined;
function trackOwner(config = latestConfig): void {
  latestConfig = config;
  const game = getPolicyGame(), exe = game ? gamePolicyKey(game) : '';
  const entry = config?.entries[exe];
  const signature = JSON.stringify([getSettingsGeneration(), exe, game?.pid, game?.processCreated, game?.identityGeneration, !!entry && entry.enabled !== false]);
  if (signature !== ownerSignature) { ownerSignature = signature; ownerEpoch++; }
}
const listeners = new Set<() => void>();
function start(): void {
  if (started) return;
  started = true;
  const changed = (config?: GameCustomConfig) => { trackOwner(config); epoch++; for (const cb of [...listeners]) cb(); };
  subscribePolicyGameStatus(() => changed());
  onGameCustomConfigChanged(config => changed(config));
}
export function onGameInputEditorChanged(cb: () => void): () => void {
  start(); listeners.add(cb);
  return () => { listeners.delete(cb); };
}
function sameTarget(editor: GameInputEditor): boolean {
  const game = getPolicyGame();
  return editor.target.generation === getSettingsGeneration() && editor.ownerEpoch === ownerEpoch &&
    (game?.pid || 0) === editor.target.pid && String(game?.processCreated || '') === editor.target.processCreated &&
    (game ? gamePolicyKey(game) : '') === editor.target.exe;
}
export async function readGameInputEditor(): Promise<GameInputEditor> {
  start();
  const generation = getSettingsGeneration();
  // Initialize the confirmed game lease before sampling the data and epoch.
  await detectPolicyGame();
  const ticket = epoch;
  const [settings, config] = await Promise.all([loadSettings(), loadGameCustomConfig()]);
  assertSettingsGeneration(generation);
  if (ticket !== epoch) return readGameInputEditor();
  const game = getPolicyGame();
  const exe = game ? gamePolicyKey(game) : '';
  const entry = config.entries[exe];
  const profile = entry && entry.enabled !== false ? entry : undefined;
  trackOwner(config);
  const input = structuredClone(settings.input);
  const overlay = profile ? buildGameInputOverlay(input, profile, 'editor', 'editor') : null;
  if (overlay) {
    input.outputTarget = overlay.outputTarget as InputSettingsSnapshot['outputTarget'];
    input.gyroMotion = overlay.gyroMotion as InputSettingsSnapshot['gyroMotion'];
  }
  return { input, profile, ownerEpoch, gameName: profile ? profile.displayName || detectedGameName(game) || exe : '',
    target: { exe, pid: game?.pid || 0, processCreated: String(game?.processCreated || ''), generation } };
}

/** Keep page writes in their captured owner; never fall back to a different game or the base. */
export async function saveGameInputEditor(editor: GameInputEditor, patch: Record<string, any>, isCurrent: () => boolean = () => true): Promise<SettingsCasResult> {
  if (!sameTarget(editor) || !isCurrent()) throw new Error('当前编辑对象已变化，请重新调整');
  if (!editor.profile) return compareAndSwapInputSettings(editor.input.revision, patch, () => sameTarget(editor) && isCurrent());
  if (Object.keys(patch).some(key => !['outputTarget', 'gyroMotion'].includes(key))) throw new Error('此设置属于公用配置');
  const fields: Partial<GameCustomProfile> = {
    inputOverrides: mergeSettings(snapshotSettingsData(editor.profile.inputOverrides || {}), patch),
  };
  if (typeof patch.outputTarget?.persona === 'string') fields.padPersona = patch.outputTarget.persona;
  if (patch.gyroMotion) {
    const motion = patch.gyroMotion;
    const selected = motion.activePreset ?? motion.preset ?? editor.profile.gyroPreset;
    if (['fps', 'racing', 'custom', 'steam'].includes(selected)) fields.gyroPreset = selected;
    if (typeof motion.enabled === 'boolean') fields.gyroOverride = motion.enabled ? fields.gyroPreset || 'on' : 'off';
  }
  await patchGameCustomProfile(editor.target, fields, undefined, {
    expectedEnabled: true, expectedInputRevision: gameProfileInputRevision(editor.profile),
  });
  const next = await readGameInputEditor();
  if (next.target.exe !== editor.target.exe || next.target.pid !== editor.target.pid ||
      next.target.processCreated !== editor.target.processCreated || !next.profile) throw new Error('当前编辑对象已变化');
  return { ok: true, revision: next.input.revision, value: next.input };
}
