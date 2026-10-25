import { detectCpuName } from './yeman';
// One static identity read, shared by editor and HUD. Sensor readings stay in the
// sole monitor publisher; a failed identity query remains an empty label.
let cpuName: Promise<string> | undefined;
export function readMonitorCpuName(): Promise<string> {
  return cpuName ??= detectCpuName().then(value => typeof value === 'string' ? value.trim() : '').catch(() => '');
}
