export const LOW_BATTERY_STATIC_PERCENT = 20;
export const LOW_BATTERY_RESUME_PERCENT = 25;

export interface BatteryPolicySample {
  ac: number;
  hasBattery: boolean;
  batteryPercent: number;
  chargeW: number;
}

// `videoDesired` records intent, not the browser's actual media state.
export function shouldSkipVideoReconcile(
  desired: boolean,
  shouldPlay: boolean,
  actualPlaying: boolean,
): boolean {
  return desired === shouldPlay && actualPlaying === shouldPlay;
}

// 低电量策略带滞回：放电 <=20% 进入静态；充电、非放电或 >=25% 才恢复视频。
// 电量未知时保留当前状态，避免一次坏采样把视频错误恢复或错误关闭。
export function nextLowBatteryStatic(
  current: boolean,
  sample: BatteryPolicySample | null | undefined,
  enabled: boolean,
): boolean {
  if (!enabled) return false;
  if (!sample?.hasBattery || sample.batteryPercent < 0) return current;
  const discharging = sample.ac === 0 && sample.chargeW < -0.5;
  if (current) return !(sample.ac === 1 || !discharging || sample.batteryPercent >= LOW_BATTERY_RESUME_PERCENT);
  return discharging && sample.batteryPercent <= LOW_BATTERY_STATIC_PERCENT;
}
