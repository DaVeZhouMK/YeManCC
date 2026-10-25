/** [LIFECYCLE:FAN:CAPABILITY-ADMISSION] 将Host真实能力握手投影为原前端准入结果。
 * 路线/alias/writer的权威来源仍是capabilities总库和Host；这里不维护第二份全机型表。
 * DEVICE_PROFILES仅保留原GPDWin5/Xbox身份兼容规则；其它映射沿原Host握手分支，勿借整理扩门。
 * 识别路线不等于写入/恢复已证；修改判据须同步实际Host消费者与既有正反验证。
 */
import type { FanHandshake } from '../fanApi';

export interface FanDeviceGateResult {
  allowed: boolean;
  /** The route is mapped and may be shown, but writes are blocked until the
   * Host reports a route-specific restore/readback capability. */
  writeReady: boolean;
  deviceFamily?: 'gpd-win5' | 'rog-xbox' | 'hc-mapped-fan';
  reason: string;
}


const DEVICE_PROFILES = [
  {
    family: 'gpd-win5' as const,
    factoryTypes: new Set(['HandheldCompanion.Devices.GPDWin5']),
    manufacturers: new Set(['gpd']),
  },
  {
    family: 'rog-xbox' as const,
    factoryTypes: new Set([
      'HandheldCompanion.Devices.XboxROGAlly',
      'HandheldCompanion.Devices.XboxROGAllyX',
    ]),
    // The HC factory type is the authoritative route gate. Manufacturer/model
    // metadata is checked when present but is not hard-coded because ROG Xbox
    // firmware reports different product strings across BIOS revisions.
    manufacturers: new Set<string>(),
  },
];


function normalizedIdentity(identity: Record<string, unknown> | null | undefined): Record<string, string> {
  const source = identity ?? {};
  const out: Record<string, string> = {};
  // Host readback uses HC field names (`ManufacturerName`, `SystemModel`,
  // `ProductName`, `Version`), while persisted WMI/preflight data uses the
  // short lowercase names. Normalize both forms or a restart/recovery would
  // incorrectly turn a previously accepted device into “风扇不支持”.
  const aliases: Record<string, string[]> = {
    manufacturer: ['manufacturer', 'ManufacturerName'],
    model: ['model', 'SystemModel'],
    product: ['product', 'ProductName'],
    bios: ['bios', 'Version'],
    ecRevision: ['ecRevision', 'EcRevision', 'ECRevision'],
  };
  for (const [key, names] of Object.entries(aliases)) {
    const value = names.map((name) => source[name]).find((candidate) => typeof candidate === 'string' && candidate.trim());
    if (typeof value === 'string' && value.trim()) out[key] = value.trim().toLowerCase();
  }
  return out;
}


function identityMatches(
  actual: Record<string, string>,
  saved: Record<string, string> | null,
): boolean {
  if (!saved) return true;
  const keys = Object.keys(saved);
  if (keys.length === 0) return true;
  return keys.every((key) => actual[key] === saved[key]);
}


export function evaluateFanDeviceGate(
  handshake: FanHandshake,
  savedIdentity?: Record<string, unknown> | null,
): FanDeviceGateResult {
  if (!handshake.ok || !handshake.supported) {
    const reason = handshake.reason
      ? handshake.reason
        .replace(/HandheldCompanion/gi, '设备数据组件')
        .replace(/\bHC\b/gi, '设备数据')
        .replace(/\bfactory\b/gi, '设备类型')
      : '握手未确认设备支持';
    return { allowed: false, writeReady: false, reason };
  }
  const profile = DEVICE_PROFILES.find((candidate) =>
    typeof handshake.deviceClass === 'string' && candidate.factoryTypes.has(handshake.deviceClass),
  );
  // Batch-03 mapped HC fan routes are admitted by a successful handshake even
  // when that device family has not yet had a dedicated real-machine session.
  // The runtime Host still keeps hardware control behind its explicit write
  // session; a failed/unknown handshake never reaches this branch.
  if (!profile && typeof handshake.fanRoute === 'string' && handshake.fanRoute.trim()) {
    return {
      allowed: true,
      writeReady: handshake.fanRouteWriteReady === true,
      deviceFamily: 'hc-mapped-fan',
      reason: handshake.fanRouteWriteReady === true
        ? `已识别可写风扇路线：${handshake.fanRoute}`
        : `已识别风扇路线，但尚未完成写入/恢复验证：${handshake.fanRoute}`,
    };
  }
  if (!profile) {
    return {
      allowed: false,
      writeReady: false,
      reason: '未识别风扇映射路由（源码可达/授权/实机验证三项均未建立；不作设备不支持结论）',
    };
  }

  const actual = normalizedIdentity(handshake.deviceIdentity);
  if (profile.manufacturers.size > 0 && actual.manufacturer && !profile.manufacturers.has(actual.manufacturer)) {
    return { allowed: false, writeReady: false, reason: '设备制造商与设备类型路由不一致' };
  }
  const saved = savedIdentity ? normalizedIdentity(savedIdentity) : null;
  if (!identityMatches(actual, saved)) {
    return { allowed: false, writeReady: false, reason: '当前设备身份与已保存 Fan 配置不匹配' };
  }
  const writeReady = handshake.fanRouteWriteReady === true;
  return {
    allowed: true,
    writeReady,
    deviceFamily: profile.family,
    reason: writeReady ? '设备身份与风扇数据路线通过，可写' : '设备身份已识别，但真实写入/恢复验证未完成',
  };
}
