// Top bar projection only; the uniform collector/cache remains the sole reader.
import type { MonitorSnapshotView } from './monitorData';
import type { TopMonData } from './topmon';
import { projectMonitorSnapshotSignals } from './monitorSnapshotSignals';
export function projectTopMonitorView(view: MonitorSnapshotView | null, now: number): TopMonData | null {
  if (!view?.top) return null;
  const top = view.top, signals = projectMonitorSnapshotSignals(view, now);
  if (signals.stale) return null;
  const hwFresh = !view.hwDown && !top.hwDown && Number.isFinite(top.ts) && top.ts > 0 && now >= top.ts && now - top.ts < 6000;
  const batteryPowerValid = typeof signals.values.batteryPower === 'number', batteryTimeValid = typeof signals.values.batteryTime === 'number';
  return { ...top, hwDown: !hwFresh,
    ...(top.cpuUsageValid === undefined ? {} : { cpuUsageValid: typeof signals.values.cpuUsage === 'number', cpuUsageTs: typeof signals.values.cpuUsage === 'number' ? top.cpuUsageTs : 0 }),
    tdpW: hwFresh ? top.tdpW : 0, freqMhz: hwFresh ? top.freqMhz : 0, tempC: hwFresh ? top.tempC : 0, cpuUsage: typeof signals.values.cpuUsage === 'number' ? signals.values.cpuUsage : 0,
    gpuPowerW: hwFresh ? top.gpuPowerW : 0, gpuClockMhz: hwFresh ? top.gpuClockMhz : 0,
    thermalThrottleFound: hwFresh && top.thermalThrottleFound, thermalThrottleMax: hwFresh ? top.thermalThrottleMax : 0,
    virtualMemoryCommittedFound: hwFresh && top.virtualMemoryCommittedFound, virtualMemoryCommittedMb: hwFresh ? top.virtualMemoryCommittedMb : 0,
    virtualMemoryLoadFound: hwFresh && top.virtualMemoryLoadFound, virtualMemoryLoadPct: hwFresh ? top.virtualMemoryLoadPct : 0,
    batteryPercent: typeof signals.values.batteryPercent === 'number' ? signals.values.batteryPercent : -1,
    chargeW: batteryPowerValid ? Number(signals.values.batteryPower) : 0,
    batteryPowerValid, batteryPowerEstimated: batteryPowerValid && signals.batteryFresh && top.batteryPowerEstimated === true,
    remainMin: batteryTimeValid ? Number(signals.values.batteryTime) : -1,
    batteryTimeValid, batteryTimeEstimated: batteryTimeValid && signals.batteryTimeFresh && top.batteryTimeEstimated === true,
  };
}
