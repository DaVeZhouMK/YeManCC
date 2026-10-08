import { compareAndSwapInputSettings, loadSettings, type InputSettingsSnapshot } from '@/bridge/settingsRepository';
import { subscribePolicyGameStatus, getPolicyGame, gamePolicyKey, isPolicyGameInitialized } from '@/bridge/gamePolicyTarget';
import { invoke } from '@/bridge/ipc';
import { registerScheduledTask } from '@/scheduler';
import { loadGameCustomConfig, onGameCustomConfigChanged, clearGameInputRestoreSnapshot, type GameCustomProfile } from '@/bridge/performanceSchedule';

/** Dedicated input is an effective overlay. The base settings never change. */
export type PadPersonaOverride = NonNullable<GameCustomProfile['padPersona']>;
export type GyroOverride = NonNullable<GameCustomProfile['gyroOverride']>;

const PAD_PERSONAS: PadPersonaOverride[] = ['follow', 'disabled', 'steamdeck', 'dualsense-edge', 'elite'];
const GYRO_OVERRIDES: GyroOverride[] = ['follow', 'on', 'off', 'fps', 'racing', 'custom', 'steam'];
// 2026-09-30 用户裁决：陀螺仪下拉的四个预设直接复用陀螺仪页的 fps/racing/custom/steam，
// 选中任一项 = 启用陀螺仪 + 展开该预设（不新建预设）。
const GYRO_PRESET_KEYS = ['fps', 'racing', 'custom', 'steam'] as const;
type GyroPresetKey = typeof GYRO_PRESET_KEYS[number];

function isGyroPresetKey(value: unknown): value is GyroPresetKey {
  return typeof value === 'string' && (GYRO_PRESET_KEYS as readonly string[]).includes(value);
}

/**
 * 预设「定义字段」：与 GyroMotionView.applyPreset 的无记忆分支同一口径——
 * 用户没在陀螺仪页调过该预设时，就按这几项展开，其余参数沿用当前全局值。
 * （陀螺仪页调整预设默认时，这里要同步。）
 */
const GYRO_PRESET_DEFINING_FIELDS: Record<GyroPresetKey, Record<string, unknown>> = {
  fps: { motionMode: 'on', motionTrigger: '', outputStick: 'right', outputAxis: 'xy', motionInput: 'local-space', aimingSightsTrigger: 'LT' },
  racing: { motionMode: 'on', motionTrigger: '', outputStick: 'left', outputAxis: 'x', motionInput: 'joystick-steering' },
  steam: { motionMode: 'on', motionTrigger: '', motionInput: 'local-space' },
  custom: { motionMode: 'on', motionTrigger: '' },
};

/**
 * UI 形态的预设快照（gyroMotion.presets[key]）→ native 消费的持久形态，
 * 口径与 GyroMotionView.saveNow 完全一致：motionMode 'suppress' 落盘为
 * 'on' + 触发键；'on' 必须把触发键写成 null（常开）。
 */
function gyroMotionParamsForPreset(kind: GyroPresetKey, presets: unknown): Record<string, unknown> {
  const stored = presets && typeof presets === 'object' && !Array.isArray(presets)
    ? (presets as Record<string, unknown>)[kind]
    : null;
  const source: Record<string, unknown> = stored && typeof stored === 'object' && !Array.isArray(stored)
    ? { ...(stored as Record<string, unknown>) }
    : { ...GYRO_PRESET_DEFINING_FIELDS[kind], innerDeadzone: 1, antiDeadzone: 20, autoCalibrate: true };
  const uiMode = typeof source.motionMode === 'string' ? source.motionMode : 'on';
  const trigger = typeof source.motionTrigger === 'string' ? source.motionTrigger.trim() : '';
  const params: Record<string, unknown> = { ...source };
  delete params.gyroMode; // legacy 死字段：陀螺仪页落盘时同样剔除。
  params.motionMode = uiMode === 'suppress' ? 'on' : uiMode;
  params.motionTrigger = uiMode === 'on' ? null : (trigger || null);
  return params;
}

export function isPadPersonaOverride(value: unknown): value is PadPersonaOverride {
  return typeof value === 'string' && (PAD_PERSONAS as string[]).includes(value);
}

export function isGyroOverride(value: unknown): value is GyroOverride {
  return typeof value === 'string' && (GYRO_OVERRIDES as string[]).includes(value);
}


export interface GameInputOverrideState { locked: boolean; identity: string }
let lockState: GameInputOverrideState = { locked: false, identity: '' };
const lockListeners = new Set<(state: GameInputOverrideState) => void>();
export function getGameInputOverrideState(): GameInputOverrideState { return lockState; }
export function subscribeGameInputOverrideState(cb: (state: GameInputOverrideState) => void): () => void {
  lockListeners.add(cb); cb(lockState);
  return () => { lockListeners.delete(cb); };
}
function publishLock(locked: boolean, identity = ''): void {
  if (lockState.locked === locked && lockState.identity === identity) return;
  lockState = { locked, identity };
  for (const cb of [...lockListeners]) cb(lockState);
}
export function effectiveInputPersona(input: InputSettingsSnapshot): string {
  const overlay = sessionId && input.gameOverride?.sessionId === sessionId ? input.gameOverride : null;
  return String(overlay?.outputTarget?.persona ?? input.outputTarget?.persona ?? 'disabled');
}
export function buildGameInputOverlay(input: InputSettingsSnapshot, profile: GameCustomProfile | undefined,
  identity: string, sessionId: string): Record<string, unknown> | null {
  if (!identity || !sessionId || !profile || profile.enabled === false) return null;
  const pad = isPadPersonaOverride(profile.padPersona) ? profile.padPersona : 'follow';
  const gyro = isGyroOverride(profile.gyroOverride) ? profile.gyroOverride : 'follow';
  if (pad === 'follow' && gyro === 'follow') return null;
  const target = structuredClone(input.outputTarget || {});
  const motion = structuredClone(input.gyroMotion || {});
  if (pad !== 'follow') {
    target.persona = pad;
    target.buttonMappingEnabled = pad !== 'disabled';
  }
  const disabled = String(target.persona || 'disabled') === 'disabled';
  const kind = isGyroPresetKey(gyro) ? gyro : null;
  if (disabled || gyro === 'off') { target.gyroEnabled = false; motion.enabled = false; }
  else if (kind || gyro === 'on') { target.gyroEnabled = true; motion.enabled = true; motion.outputMode = 'virtual-stick'; }
  if (kind && !disabled) {
    Object.assign(motion, gyroMotionParamsForPreset(kind, input.gyroMotion?.presets));
    motion.enabled = true; motion.preset = kind; motion.activePreset = kind; motion.outputMode = 'virtual-stick';
  }
  return { identity, sessionId, outputTarget: target, gyroMotion: motion };
}

let stopGame: (() => void) | null = null;
let stopConfig: (() => void) | null = null;
let stopRetry: (() => void) | null = null;
let sessionId = '';
let epoch = 0;
let running = false;
let pending = false;
let active = false;

async function evaluate(): Promise<void> {
  if (!active || !isPolicyGameInitialized()) return;
  if (running) { pending = true; return; }
  running = true;
  const generation = epoch;
  try {
    if (!sessionId) sessionId = await invoke<string>('input.gameOverride.session', {});
    if (!sessionId || !active || generation !== epoch) return;
    // Old versions rewrote the base. Recover that durable snapshot once before overlays.
    const config = await loadGameCustomConfig();
    if (config.inputRestore) {
      const restore = config.inputRestore;
      publishLock(true, 'legacy-restore');
      const restoreId = JSON.stringify(restore);
      const input = (await loadSettings()).input;
      if (!active || generation !== epoch) return;
      if (input.legacyGameRestoreId !== restoreId) {
        const result = await compareAndSwapInputSettings(input.revision, {
          legacyGameRestoreId: restoreId,
          // A cold-start policy already replaced the old runtime intent. Do not resurrect it.
          ...(input.startupAppliedSession === sessionId ? {} : {
            outputTarget: { persona: restore.persona, buttonMappingEnabled: restore.buttonMappingEnabled, gyroEnabled: restore.gyroEnabled },
            gyroMotion: { ...restore.gyroMotion, enabled: restore.motionEnabled },
          }), gameOverride: null,
        }, () => active && generation === epoch);
        if (!result.ok) return;
      }
      await clearGameInputRestoreSnapshot(restore);
    }
    for (let attempt = 0; attempt < 3; attempt += 1) {
      const latestConfig = await loadGameCustomConfig();
      const game = getPolicyGame();
      const key = gamePolicyKey(game);
      const identity = game ? game.pid + ':' + game.processCreated + ':' + key : '';
      const input = (await loadSettings()).input;
      const overlay = buildGameInputOverlay(input, latestConfig.entries[key], identity, sessionId);
      if (!active || generation !== epoch) return;
      // Lock before dispatch. Unlock only after the effective overlay is removed.
      if (overlay) publishLock(true, identity);
      if (JSON.stringify(input.gameOverride ?? null) === JSON.stringify(overlay)) {
        publishLock(!!overlay, overlay ? identity : ''); return;
      }
      const result = await compareAndSwapInputSettings(input.revision, { gameOverride: overlay }, () => active && generation === epoch);
      if (!result.ok) continue;
      publishLock(!!overlay, overlay ? identity : '');
      if (typeof window !== 'undefined') window.dispatchEvent(new CustomEvent('gp:pad-persona', {
        detail: { persona: effectiveInputPersona(result.value) },
      }));
      return;
    }
  } catch {
    // Keep the prior effective overlay and lock on failure; resident retry heals it.
  } finally {
    running = false;
    if (pending && active) { pending = false; void evaluate(); }
  }
}
export function startGameInputOverrideWatch(): void {
  if (active) return;
  active = true; epoch += 1;
  stopGame = subscribePolicyGameStatus(() => { epoch += 1; void evaluate(); });
  stopConfig = onGameCustomConfigChanged(() => { epoch += 1; void evaluate(); });
  stopRetry = registerScheduledTask('game-input-overlay-retry', 2000, evaluate, { pauseWhenHidden: false });
}
export function stopGameInputOverrideWatch(): void {
  active = false; epoch += 1; pending = false;
  stopGame?.(); stopGame = null; stopConfig?.(); stopConfig = null; stopRetry?.(); stopRetry = null;
}
