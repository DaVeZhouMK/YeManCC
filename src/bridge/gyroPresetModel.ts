// Shared original preset expansion; pure data, no IO or hardware owner.
export const GYRO_PRESET_KEYS = ['fps', 'racing', 'custom', 'steam'] as const;
export type GyroPresetKey = typeof GYRO_PRESET_KEYS[number];

export function isGyroPresetKey(value: unknown): value is GyroPresetKey {
  return typeof value === 'string' && (GYRO_PRESET_KEYS as readonly string[]).includes(value);
}

/**
 * 预设「定义字段」：与 GyroMotionView.applyPreset 的无记忆分支同一口径——
 * 用户没在陀螺仪页调过该预设时，就按这几项展开，其余参数沿用当前全局值。
 * （陀螺仪页调整预设默认时，这里要同步。）
 */
const GYRO_PRESET_DEFINING_FIELDS: Record<GyroPresetKey, Record<string, unknown>> = {
  fps: { motionMode: 'on', motionTrigger: '', outputStick: 'right', outputAxis: 'xy', motionInput: 'local-space', aimingSightsTrigger: 'LT', gyroWeight: 1.3 },
  racing: { motionMode: 'on', motionTrigger: '', outputStick: 'left', outputAxis: 'x', motionInput: 'joystick-steering' },
  steam: { motionMode: 'on', motionTrigger: '', motionInput: 'local-space' },
  custom: { motionMode: 'on', motionTrigger: '' },
};

/**
 * UI 形态的预设快照（gyroMotion.presets[key]）→ native 消费的持久形态，
 * 口径与 GyroMotionView.saveNow 完全一致：motionMode 'suppress' 落盘为
 * 'on' + 触发键；'on' 必须把触发键写成 null（常开）。
 */
export function gyroMotionParamsForPreset(kind: GyroPresetKey, presets: unknown): Record<string, unknown> {
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
  params.motionTrigger = uiMode === 'on' ? null : (trigger || 'LT');
  return params;
}


/** Expand an explicit power/link action, never page hydration or boot intent.
 * A remembered preset wins. Legacy configured top-level parameters remain usable;
 * the untouched off/empty-trigger baseline gets the selected preset's defaults.
 */
export function gyroMotionForEnable(value: unknown): Record<string, unknown> {
  const motion = value && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown> : {};
  const kind = isGyroPresetKey(motion.activePreset) ? motion.activePreset
    : isGyroPresetKey(motion.preset) ? motion.preset : 'fps';
  const presets = motion.presets && typeof motion.presets === 'object' && !Array.isArray(motion.presets)
    ? motion.presets as Record<string, unknown> : {};
  const stored = presets[kind];
  const hasStored = stored && typeof stored === 'object' && !Array.isArray(stored);
  const trigger = typeof motion.motionTrigger === 'string' ? motion.motionTrigger.trim() : '';
  const configured = motion.motionMode === 'on' ||
    (['off', 'toggle'].includes(String(motion.motionMode)) && trigger !== '');
  const params = hasStored || !configured ? gyroMotionParamsForPreset(kind, presets) : {};
  return { ...motion, ...params, preset: kind, activePreset: kind, enabled: true, outputMode: 'virtual-stick' };
}
