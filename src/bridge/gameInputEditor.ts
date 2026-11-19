import { gyroMotionForEnable } from './gyroPresetModel';
import { loadSettings, compareAndSwapInputSettings, getSettingsGeneration, assertSettingsGeneration, onInputSettingsChanged,
  type InputSettingsSnapshot, type SettingsCasResult } from './settingsRepository';
import type { GameCustomProfile, GameCustomProfileTarget } from './performanceSchedule';

/** Compatibility shape for ordinary pages; every input editor owns global input. */
export interface GameInputEditor {
  input: InputSettingsSnapshot;
  target: GameCustomProfileTarget;
  profile?: GameCustomProfile;
  gameName: string;
  ownerEpoch: number;
}
export function onGameInputEditorChanged(cb: () => void): () => void {
  return onInputSettingsChanged(cb);
}
function currentEditor(editor: GameInputEditor): boolean {
  return editor.target.generation === getSettingsGeneration() && editor.ownerEpoch === getSettingsGeneration();
}
export async function readGameInputEditor(): Promise<GameInputEditor> {
  const generation = getSettingsGeneration();
  const settings = await loadSettings();
  assertSettingsGeneration(generation);
  return { input: structuredClone(settings.input), ownerEpoch: generation, gameName: '',
    target: { exe: '', pid: 0, processCreated: '', path: '', generation } };
}
/** Save through global input CAS; game/profile changes do not change ownership. */
export async function saveGameInputEditor(editor: GameInputEditor, patch: Record<string, any>, isCurrent: () => boolean = () => true): Promise<SettingsCasResult> {
  if (!currentEditor(editor) || !isCurrent()) throw new Error('当前编辑对象已变化，请重新调整');
  if (patch.gyroMotion?.enabled === true && !Object.prototype.hasOwnProperty.call(patch.gyroMotion, 'motionMode')) {
    patch = { ...patch, gyroMotion: { ...gyroMotionForEnable(editor.input.gyroMotion), ...patch.gyroMotion } };
  }
  // Retire saved overlays alongside the global edit, including documents whose
  // overlay would otherwise block the repository's legacy CAS guard.
  return compareAndSwapInputSettings(editor.input.revision, { ...patch, gameOverride: null }, () => currentEditor(editor) && isCurrent());
}
