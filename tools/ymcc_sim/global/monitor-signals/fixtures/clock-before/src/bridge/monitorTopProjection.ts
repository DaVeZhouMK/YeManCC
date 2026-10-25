// Top bar projection only; the uniform collector/cache remains the sole reader.
import type { MonitorSnapshotView } from './monitorData';
import type { TopMonData } from './topmon';
import { hudFromUniformSnapshot } from './monitorHudModel';
export function projectTopMonitorView(view: MonitorSnapshotView | null, now: number): TopMonData | null {
  if (!view?.top) return null;
  const top = view.top, hud = hudFromUniformSnapshot(view, now);
  if (hud.stale) return null;
  const hwFresh = !view.hwDown && !top.hwDown && Number.isFinite(top.ts) && top.ts > 0 && now >= top.ts && now - top.ts < 6000;
  const batteryPowerValid = typeof hud.values.batteryPower === 'number', batteryTimeValid = typeof hud.values.batteryTime === 'number';
  return { ...top, hwDown: !hwFresh,
    ...(top.cpuUsageValid === undefined ? {} : { cpuUsageValid: typeof hud.values.cpuUsage === 'number', cpuUsageTs: typeof hud.values.cpuUsage === 'number' ? top.cpuUsageTs : 0 }),
    tdpW: hwFresh ? top.tdpW : 0, freqMhz: hwFresh ? top.freqMhz : 0, tempC: hwFresh ? top.tempC : 0, cpuUsage: typeof hud.values.cpuUsage === 'number' ? hud.values.cpuUsage : 0,
    gpuPowerW: hwFresh ? top.gpuPowerW : 0, gpuClockMhz: hwFresh ? top.gpuClockMhz : 0,
    thermalThrottleFound: hwFresh && top.thermalThrottleFound, thermalThrottleMax: hwFresh ? top.thermalThrottleMax : 0,
    virtualMemoryCommittedFound: hwFresh && top.virtualMemoryCommittedFound, virtualMemoryCommittedMb: hwFresh ? top.virtualMemoryCommittedMb : 0,
    virtualMemoryLoadFound: hwFresh && top.virtualMemoryLoadFound, virtualMemoryLoadPct: hwFresh ? top.virtualMemoryLoadPct : 0,
    batteryPercent: typeof hud.values.batteryPercent === 'number' ? hud.values.batteryPercent : -1,
    chargeW: batteryPowerValid ? Number(hud.values.batteryPower) : 0,
    batteryPowerValid, batteryPowerEstimated: batteryPowerValid && hud.batteryPowerEstimated === true,
    remainMin: batteryTimeValid ? Number(hud.values.batteryTime) : -1,
    batteryTimeValid, batteryTimeEstimated: batteryTimeValid && hud.batteryTimeEstimated === true,
  };
}
