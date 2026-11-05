// cpuctl.ts — CCD 核心控制 + CPU 降压（Undervolt）桥层
//
// 设计：
//  - 生产门保持关闭；未激活时探测/写入零动作、零外部进程。
//  - CCD 走 native cpu.topology / cpu.setCcdMode；操作语义是进程/L3 域亲和性，
//    不把 L3 域或亲和性回执说成物理 CCD 断电。
//  - 降压走 YeManTdpCtl uv 命令族；AMD 为 CO 步进、Intel 为 mV，无法读回时
//    currentVerified 必须为 null，不能用退出码 0 或零探测代替实际偏移。
//  - 静态偏移；accepted 与 applied 分列，写回执缺读回时 applicationState=unknown。

import { shell } from './api';
import { invoke } from './ipc';
import { CCD_MANUAL_CONTROL_ACTIVE, CPU_VOLTAGE_OFFSET_ACTIVE } from './cpuCapabilityPolicy.generated';

async function tdpCtlExe(): Promise<string> {
  const root = await invoke<unknown>('app.powerControlDir');
  if (typeof root !== 'string' || !root.trim()) throw new Error('功耗运行库目录不可用');
  return root.replace(/\//g, '\\').replace(/\\+$/, '') + '\\pawnio\\YeManTdpCtl.exe';
}

const isRecord = (value: unknown): value is Record<string, any> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);
const finiteInt = (value: unknown): value is number =>
  typeof value === 'number' && Number.isSafeInteger(value);
const finiteNumber = (value: unknown): value is number =>
  typeof value === 'number' && Number.isFinite(value);
const text = (value: unknown): value is string =>
  typeof value === 'string' && value.trim().length > 0;
const lastJsonLine = (stdout: string): Record<string, any> | null => {
  const lines = stdout.trim().split(/\r?\n/).filter(Boolean);
  if (!lines.length) return null;
  try {
    const parsed = JSON.parse(lines[lines.length - 1]);
    return isRecord(parsed) ? parsed : null;
  } catch {
    return null;
  }
};

// ── CCD 支持探测 ──
export interface CcdDomain {
  index: number;
  mask: string;
  logical: number;
}
export interface CcdLease {
  owner: string;
  generation: number;
  mode: number;
  active: boolean;
}
export interface CcdProbe {
  performanceSupported: boolean;
  supported: boolean;
  available: boolean;
  l3Domains: number;
  ccdMasks: string[];
  domains: CcdDomain[];
  physicalCores: number;
  logical: number;
  operation: 'process_affinity' | '';
  effectScope: string;
  mechanism: string;
  physicalCcdPowerOff: boolean;
  topologySource: string;
  lease: CcdLease | null;
  reason?: string;
}

function ccdUnavailable(reason: string): CcdProbe {
  return {
    performanceSupported: false, supported: false, available: false, l3Domains: 0, ccdMasks: [], domains: [],
    physicalCores: 0, logical: 0, operation: '', effectScope: '', mechanism: '',
    physicalCcdPowerOff: false, topologySource: '', lease: null, reason,
  };
}

const ccdApplicationStates = ['applied', 'pending', 'failed', 'unknown'] as const;
export type CcdApplicationState = typeof ccdApplicationStates[number];

export interface CcdSetReceipt {
  ok: boolean;
  requestId: string;
  generation: number;
  mode: number;
  operation: 'process_affinity' | '';
  effectScope: string;
  mechanism: string;
  physicalCcdPowerOff: boolean;
  targetMask: string;
  accepted: boolean;
  applicationState: CcdApplicationState;
  applied: boolean;
  observed: null;
  verification: string;
  affected: {
    attempted: number;
    verified: number;
    openFailed: number;
    setFailed: number;
    verifyFailed: number;
    captureFailed?: number;
    staleAborted?: number;
  };
  backgroundRefresh: boolean;
  lease: CcdLease | null;
  reason?: string;
}

function validCcdLease(value: unknown): CcdLease | null {
  if (!isRecord(value)) return null;
  if (!text(value.owner) || !finiteInt(value.generation) || value.generation < 0) return null;
  if (!finiteInt(value.mode) || value.mode < 0 || typeof value.active !== 'boolean') return null;
  return { owner: value.owner, generation: value.generation, mode: value.mode, active: value.active };
}

export async function probeCcd(): Promise<CcdProbe> {
  if (!CCD_MANUAL_CONTROL_ACTIVE) return ccdUnavailable('feature_inactive');
  try {
    const r = await invoke<any>('cpu.topology', {});
    // Native owns runtime representability and availability. Valid-looking
    // masks must never override an explicit native refusal.
    if (r?.supported !== true || r?.available !== true || r?.singleGroupRepresentable === false)
      return ccdUnavailable(text(r?.reason) ? r.reason : 'native_topology_unavailable');
    const count = (value: unknown) =>
      typeof value === 'number' && Number.isSafeInteger(value) && value >= 0 ? value : 0;
    const l3 = count(r?.l3Domains), logical = count(r?.logical);
    const masks: string[] = Array.isArray(r?.ccdMasks) ? r.ccdMasks : [];
    let union = 0n;
    const validMasks = masks.length === l3 && masks.every(mask => {
      if (typeof mask !== 'string' || !/^(?:0x)?[0-9a-f]{1,16}$/i.test(mask)) return false;
      const value = BigInt(/^0x/i.test(mask) ? mask : '0x' + mask);
      if (value === 0n || (union & value) !== 0n) return false;
      union |= value;
      return true;
    });
    const threads = union.toString(2).replace(/0/g, '').length;
    if (!validMasks || logical === 0 || logical > 64 || threads !== logical)
      return ccdUnavailable('invalid_topology');
    if (r?.physicalCcdPowerOff === true)
      return ccdUnavailable('physical_power_off_claim_rejected');
    const operation = r?.operation === 'process_affinity' ? 'process_affinity' as const : '';
    const effectScope = text(r?.effectScope) ? r.effectScope : '';
    const mechanism = text(r?.mechanism) ? r.mechanism : '';
    const topologySource = text(r?.topologySource) ? r.topologySource : '';
    const domains: CcdDomain[] = Array.isArray(r?.domains)
      ? r.domains
          .filter((d: unknown) => isRecord(d) && finiteInt(d.index) && text(d.mask) &&
            finiteInt(d.logical) && d.logical > 0)
          .map((d: any) => ({ index: d.index, mask: d.mask, logical: d.logical }))
      : [];
    const domainsValid = domains.length === l3 &&
      domains.every((d, index) => d.index === index + 1 && d.mask === masks[index] &&
        BigInt(/^0x/i.test(d.mask) ? d.mask : '0x' + d.mask).toString(2).replace(/0/g, '').length === d.logical);
    const lease = validCcdLease(r?.lease);
    const supported = l3 >= 2 && operation === 'process_affinity' &&
      !!effectScope && !!mechanism && !!topologySource &&
      domainsValid && lease !== null;
    if (!supported) {
      const reason = l3 < 2
        ? 'fewer_than_two_domains'
        : (!operation || !effectScope || !mechanism || !topologySource || !domainsValid)
          ? 'invalid_operation_contract'
          : 'invalid_lease';
      return ccdUnavailable(reason);
    }
    return {
      performanceSupported: r?.performanceSupported === true && r?.multiCcdConfirmed === true &&
        r?.coreArchitectureDetected === true && r?.heterogeneous === false,
      supported: true, available: true, l3Domains: l3, ccdMasks: masks, domains,
      physicalCores: count(r?.physicalCores), logical,
      operation, effectScope, mechanism, physicalCcdPowerOff: false, topologySource, lease,
    };
  } catch {
    return ccdUnavailable('probe_failed');
  }
}

// ── CCD 核心控制（层 1：全局进程亲和，需 native cpu.setCcdMode） ──
// 0=全核；1..N=仅第 N-1 个 L3 域。N 由 native 拓扑动态决定。
// 返回回执而不只返回布尔：accepted 与 applied 分列，worker 启动不当作全部生效。
export type CcdMode = number;
export async function setCcdMode(mode: CcdMode):
  Promise<{ ok: boolean; msg: string; receipt?: CcdSetReceipt }> {
  if (!CCD_MANUAL_CONTROL_ACTIVE) return { ok: false, msg: 'CCD 功能未激活' };
  if (!finiteInt(mode) || mode < 0) return { ok: false, msg: 'CCD 模式参数无效' };
  try {
    const result = await invoke<any>('cpu.setCcdMode', { mode });
    const state = result?.applicationState;
    const validState = ccdApplicationStates.includes(state);
    const affected = result?.affected;
    const affectedValid = isRecord(affected) &&
      ['attempted', 'verified', 'openFailed', 'setFailed', 'verifyFailed']
        .every(key => finiteInt(affected[key]) && affected[key] >= 0);
    const accepted = result?.ok !== false && result?.accepted === true && result?.mode === mode &&
      text(result?.requestId) && finiteInt(result?.generation) && result.generation >= 1 &&
      result?.operation === 'process_affinity' && text(result?.effectScope) &&
      text(result?.mechanism) && result?.physicalCcdPowerOff === false &&
      text(result?.targetMask) && validState && affectedValid &&
      result?.applied === (state === 'applied');
    if (!accepted) {
      return { ok: false, msg: result?.error || result?.reason || 'CCD 设置未取得匹配的接受回执' };
    }
    const receipt: CcdSetReceipt = {
      ok: true,
      requestId: result.requestId,
      generation: result.generation,
      mode: result.mode,
      operation: 'process_affinity',
      effectScope: result.effectScope,
      mechanism: result.mechanism,
      physicalCcdPowerOff: result.physicalCcdPowerOff,
      targetMask: result.targetMask,
      accepted: true,
      applicationState: state,
      applied: result.applied,
      observed: null,
      verification: typeof result.verification === 'string' ? result.verification : '',
      affected: {
        attempted: affected.attempted,
        verified: affected.verified,
        openFailed: affected.openFailed,
        setFailed: affected.setFailed,
        verifyFailed: affected.verifyFailed,
        ...(finiteInt(affected.captureFailed) ? { captureFailed: affected.captureFailed } : {}),
        ...(finiteInt(affected.staleAborted) ? { staleAborted: affected.staleAborted } : {}),
      },
      backgroundRefresh: result.backgroundRefresh === true,
      lease: validCcdLease(result.lease),
      ...(text(result?.reason) ? { reason: result.reason } : {}),
    };
    const msg = state === 'applied'
      ? ''
      : text(result?.reason) ? result.reason : `CCD 已接受，状态 ${state}`;
    return { ok: true, msg, receipt };
  } catch (e: any) {
    return { ok: false, msg: e?.message ?? 'native 未编译（需重编译壳）' };
  }
}

// ── CPU 降压（Undervolt，走 YeManTdpCtl uv 命令族） ──
export type UvVendor = 'amd' | 'intel' | '';
export type UvUnit = 'co_step' | 'mv' | '';
export type UvScopeKind = 'amd_all_core' | 'intel_fivr_plane' | '';
export type UvPlane = 'core' | 'cache';

export interface UvOffset {
  min: number;
  max: number;
  step: number;
  signed: boolean;
}
export interface UvScope {
  kind: UvScopeKind;
  planes: UvPlane[];
}
export interface UvIdentity {
  vendor: UvVendor;
  family?: string;
  familyTag?: string;
  cpuid?: { family: number; model: number };
}
export interface UvProbe {
  supported: boolean;
  available: boolean;
  vendor: UvVendor;
  /** Legacy display fallback. Use currentVerified/readback for actual observations. */
  current: number;
  currentVerified: number | null;
  readback: boolean;
  readbackCapability: boolean;
  currentSemantics: string;
  unit: UvUnit;
  offset: UvOffset;
  scope: UvScope;
  encoding: string;
  command: string;
  identity: UvIdentity | null;
  family?: string;
  planes: Partial<Record<UvPlane, number>>;
  reason?: string;
}

function uvUnavailable(reason: string, vendor: UvVendor = ''): UvProbe {
  return {
    supported: false, available: false, vendor, current: 0, currentVerified: null,
    readback: false, readbackCapability: false, currentSemantics: 'unavailable',
    unit: '', offset: { min: 0, max: 0, step: 0, signed: true },
    scope: { kind: '', planes: [] }, encoding: '', command: '', identity: null,
    planes: {}, reason,
  };
}

function validUvVendor(value: unknown): UvVendor {
  return value === 'intel' ? 'intel' : value === 'amd' ? 'amd' : '';
}

function parseUvProbe(j: Record<string, any>): UvProbe {
  const vendor = validUvVendor(j.vendor);
  const unit: UvUnit = j.unit === 'co_step' ? 'co_step' : j.unit === 'mv' ? 'mv' : '';
  const offset = isRecord(j.offset) ? {
    min: j.offset.min, max: finiteInt(j.offset.max) ? Math.min(0, j.offset.max) : j.offset.max,
    step: j.offset.step, signed: true,
  } : null;
  const offsetValid = offset !== null && finiteInt(offset.min) && finiteInt(offset.max) &&
    finiteInt(offset.step) && offset.step > 0 && offset.min <= offset.max;
  const scopeKind: UvScopeKind = j.scope?.kind === 'amd_all_core' ? 'amd_all_core'
    : j.scope?.kind === 'intel_fivr_plane' ? 'intel_fivr_plane' : '';
  const allowedPlanes: UvPlane[] = ['core', 'cache'];
  const scopePlanes: UvPlane[] = Array.isArray(j.scope?.planes)
    ? j.scope.planes.filter((p: unknown): p is UvPlane =>
        typeof p === 'string' && allowedPlanes.includes(p as UvPlane))
    : [];
  const identity = isRecord(j.identity) && validUvVendor(j.identity.vendor) === vendor
    ? {
        vendor,
        ...(text(j.identity.family) ? { family: j.identity.family } : {}),
        ...(text(j.identity.familyTag) ? { familyTag: j.identity.familyTag } : {}),
        ...(isRecord(j.identity.cpuid) && finiteInt(j.identity.cpuid.family) &&
            finiteInt(j.identity.cpuid.model)
          ? { cpuid: { family: j.identity.cpuid.family, model: j.identity.cpuid.model } }
          : {}),
      }
    : null;
  const contractValid = vendor !== '' && unit !== '' && offsetValid && scopeKind !== '' &&
    (vendor === 'amd'
      ? unit === 'co_step' && scopeKind === 'amd_all_core'
      : unit === 'mv' && scopeKind === 'intel_fivr_plane' && scopePlanes.length > 0 &&
          Array.isArray(j.scope?.planes) && j.scope.planes.every((p: unknown) => allowedPlanes.includes(p as UvPlane))) &&
    identity !== null && text(j.operation) && j.operation === 'static_offset' &&
    text(j.encoding) && text(j.command);
  if (!contractValid) {
    return uvUnavailable('invalid_capability_contract', vendor);
  }
  const supported = j.supported === true && j.available !== false;
  const readback = supported && j.readback === true;
  const numericCurrent = finiteNumber(j.current);
  const planes: Partial<Record<UvPlane, number>> = {};
  if (readback && isRecord(j.planes)) {
    for (const plane of allowedPlanes) {
      const value = j.planes[plane];
      if (finiteNumber(value)) planes[plane] = value;
    }
  }
  const currentVerified = readback && numericCurrent ? j.current : null;
  return {
    supported,
    available: supported,
    vendor,
    current: numericCurrent ? j.current : 0,
    currentVerified,
    readback: readback && currentVerified !== null,
    readbackCapability: j.readbackCapability === true,
    currentSemantics: typeof j.current_semantics === 'string'
      ? j.current_semantics : typeof j.currentSemantics === 'string'
        ? j.currentSemantics : 'not_observed',
    unit,
    offset: { min: offset!.min, max: offset!.max, step: offset!.step, signed: true },
    scope: { kind: scopeKind, planes: scopePlanes },
    encoding: j.encoding,
    command: j.command,
    identity,
    ...(typeof j.family === 'string' ? { family: j.family } : {}),
    planes: readback ? planes : {},
    ...(typeof j.reason === 'string' ? { reason: j.reason } : {}),
  };
}

export async function probeUndervolt(): Promise<UvProbe> {
  if (!CPU_VOLTAGE_OFFSET_ACTIVE) return uvUnavailable('feature_inactive');
  try {
    const r = await shell.run(await tdpCtlExe(), ['uv', 'probe'], 30000);
    if (r.exitCode !== 0) return uvUnavailable('probe_failed');
    const j = lastJsonLine(r.stdout || '');
    if (!j) return uvUnavailable('probe_failed');
    return parseUvProbe(j);
  } catch {
    return uvUnavailable('probe_failed');
  }
}

export interface UvCommandRoute {
  transport: 'mp1' | 'rsmu' | 'psmu' | 'hsmp';
  command: number;
}
export interface UvCommandAttempt extends UvCommandRoute {
  origin: string | null;
  mailboxStatus: number | null;
  accepted: boolean;
}

export interface UvPlaneWriteReceipt {
  ioctl_written: boolean;
  firmware_accepted: null;
  readback: false;
  readback_mv: null;
  reason: string;
}
export interface UvSetReceipt {
  vendor: 'amd' | 'intel';
  ok: boolean;
  accepted: boolean;
  operation: 'static_offset';
  unit: 'co_step' | 'mv';
  requested: { value: number; scope: UvScopeKind; planes: UvPlane[] };
  applicationState: 'unknown' | 'failed';
  applied: null;
  readback: false;
  observed: null;
  reason: string;
  verification: string;
  helperExitCode: number;
  planes?: Partial<Record<UvPlane, UvPlaneWriteReceipt>>;
  failure?: UvFailure;
  attempts?: UvCommandAttempt[];
  route?: UvCommandRoute;
}

export interface UvFailure {
  origin: string;
  stage: string;
  helperExitCode: number;
  mailboxStatus: number | null;
}
export interface UvSetResult {
  ok: boolean;
  accepted: boolean;
  applicationState: CcdApplicationState;
  applied: null;
  msg: string;
  reason?: string;
  failure?: UvFailure;
  attempts?: UvCommandAttempt[];
  route?: UvCommandRoute;
  receipt?: UvSetReceipt;
}

const uvFailureOrigins = ['firmware_response', 'ready_wait', 'response_wait', 'gate', 'capability',
  'driver', 'module', 'validation', 'transport'];
const uvCode = (value: unknown): string => typeof value === 'string' && /^[a-zA-Z0-9_.-]{1,96}$/.test(value) ? value : '';
function uvFailure(value: unknown, exitCode: number): UvFailure | undefined {
  if (!isRecord(value) || !uvFailureOrigins.includes(value.origin) || !uvCode(value.stage) ||
      !finiteInt(value.helperExitCode) || value.helperExitCode !== exitCode ||
      !(value.mailboxStatus === null || finiteInt(value.mailboxStatus) && value.mailboxStatus >= 0 && value.mailboxStatus <= 255) ||
      (value.origin === 'firmware_response' && (value.stage !== 'response' || value.mailboxStatus === null || value.mailboxStatus === 1)) ||
      (value.origin !== 'firmware_response' && value.mailboxStatus !== null)) return undefined;
  return { origin: value.origin, stage: value.stage, helperExitCode: exitCode, mailboxStatus: value.mailboxStatus };
}
function uvCommandRoute(value: unknown): UvCommandRoute | undefined {
  if (!isRecord(value) || !['mp1', 'rsmu', 'psmu', 'hsmp'].includes(value.transport) ||
      !finiteInt(value.command) || value.command <= 0 || value.command > 255) return undefined;
  return { transport: value.transport, command: value.command };
}
function uvCommandAttempts(value: unknown): UvCommandAttempt[] | null {
  if (value === undefined) return []; // Legacy receipts have no attempt trace.
  if (!Array.isArray(value) || value.length > 8) return null;
  const rows: UvCommandAttempt[] = [];
  for (const attempt of value) {
    const route = uvCommandRoute(attempt);
    if (!route || typeof attempt.accepted !== 'boolean' ||
        !(attempt.origin === null || uvFailureOrigins.includes(attempt.origin)) ||
        !(attempt.mailboxStatus === null || finiteInt(attempt.mailboxStatus) && attempt.mailboxStatus >= 0 && attempt.mailboxStatus <= 255) ||
        (attempt.origin === 'firmware_response' ? attempt.mailboxStatus === null : attempt.mailboxStatus !== null) ||
        (attempt.accepted !== (attempt.origin === 'firmware_response' && attempt.mailboxStatus === 1)) ||
        rows.some(row => row.accepted)) return null;
    rows.push({ ...route, origin: attempt.origin, mailboxStatus: attempt.mailboxStatus, accepted: attempt.accepted });
  }
  return rows;
}
function uvPlaneWrites(value: unknown, requested: UvPlane[], accepted: boolean):
    Partial<Record<UvPlane, UvPlaneWriteReceipt>> | null {
  if (value === undefined) return {}; // Older receipts have no per-plane evidence.
  if (!isRecord(value)) return null;
  const rows: Partial<Record<UvPlane, UvPlaneWriteReceipt>> = {};
  for (const [name, plane] of Object.entries(value)) {
    if ((name !== 'core' && name !== 'cache') || !requested.includes(name) || !isRecord(plane) ||
        typeof plane.ioctl_written !== 'boolean' || plane.readback !== false || plane.readback_mv !== null ||
        plane.firmware_accepted != null || (accepted && !plane.ioctl_written)) return null;
    rows[name] = { ioctl_written: plane.ioctl_written, firmware_accepted: null, readback: false, readback_mv: null,
      reason: uvCode(plane.reason) };
  }
  if (Object.keys(rows).length !== requested.length) return null;
  return rows;
}
function uvPlaneWriteText(planes: Partial<Record<UvPlane, UvPlaneWriteReceipt>>): string {
  return (['core', 'cache'] as const).filter(name => planes[name]).map(name =>
    `${name === 'core' ? '核心' : '缓存'}${planes[name]!.ioctl_written ? '请求已写入' : '写入失败'}`).join(' / ');
}
function uvFailureText(reason: string, failure: UvFailure | undefined, exitCode: number): string {
  if (failure) {
    if (failure.origin === 'firmware_response') {
      const status = '0x' + failure.mailboxStatus!.toString(16).toUpperCase().padStart(2, '0');
      const meaning: Record<number, string> = { 0xFE: '未识别该命令', 0xFD: '前置条件未满足', 0xFC: '固件暂忙', 0xFF: '固件执行失败' };
      return `CPU 电压偏移失败：SMU 未接受本次全核 CO 请求（固件响应 ${status}：${meaning[failure.mailboxStatus!] || '未接受'}，助手退出码 ${exitCode}）。TDP 可用不代表固件接受电压偏移。`;
    }
    if (failure.origin === 'ready_wait') return 'CPU 电压偏移未发送：等待 SMU 通道空闲超时，可稍后手动重试。';
    if (failure.origin === 'response_wait') return 'CPU 电压偏移命令已发送，但等待 SMU 响应超时；是否已应用未知，未自动重发。';
    if (failure.origin === 'gate') return 'CPU 电压偏移未发送：未能取得 SMU 写入锁，可稍后手动重试。';
    if (failure.origin === 'capability') return 'CPU 电压偏移未发送：当前 CPU 缺少已适配的全核 CO 命令或通道。';
    if (failure.origin === 'driver') return 'CPU 电压偏移失败：无法打开 PawnIO 驱动通道，请核对 YMCC 权限和驱动状态。';
    if (failure.origin === 'module') return 'CPU 电压偏移失败：无法加载 RyzenSMU 模块，请核对功耗运行库。';
    if (failure.origin === 'validation') return 'CPU 电压偏移未发送：请求超出当前全核 CO 范围。';
    if (failure.origin === 'transport') {
      const stages: Record<string, string> = { ready_read: '读取通道状态', clear_response: '准备响应寄存器',
        argument_write: '写入命令参数', command_write: '提交命令', response_read: '读取命令响应',
        invalid_response: '校验命令响应', backend_open: '打开驱动通道' };
      return `CPU 电压偏移失败：SMU 通道在${stages[failure.stage] || '通信'}阶段出错；未确认应用，未自动重发。`;
    }
  }
  const legacy = /^smu_write_failed_rc_(\d+)$/.exec(reason);
  if (legacy) return `CPU 电压偏移失败：助手退出码 ${legacy[1]}；旧回执未提供底层响应，无法区分固件拒绝、通道忙或超时。未确认应用。`;
  if (reason === 'need_admin') return 'CPU 电压偏移未发送：当前权限不足。';
  if (reason === 'oc_mailbox_firmware_unsupported') return 'CPU 电压偏移失败：当前 Intel 固件不支持此电压偏移命令。';
  if (reason === 'invalid_receipt') return `CPU 电压偏移未取得与本次请求匹配的有效回执（助手退出码 ${exitCode}）；当前值未知。`;
  if (reason === 'power_transition_not_ready') return '系统仍在睡眠恢复阶段，CPU 电压偏移未获准执行；恢复就绪后可手动重试。';
  if (reason === 'helper_timeout') return '等待 CPU 电压偏移助手超时；是否已应用未知，未自动重发。';
  if (reason === 'runtime_directory_unavailable') return 'CPU 电压偏移运行库目录不可用；本次未发送。';
  if (reason === 'helper_start_failed') return 'CPU 电压偏移助手未能正常启动或返回；未确认应用。';
  return `CPU 电压偏移未被确认（${reason || 'helper_exit_' + exitCode}）；当前值未知。`;
}
let uvRequestSequence = 0;
function logUvResult(requestId: string, requested: number, result: UvSetResult,
    exitCode: number | null, vendor: UvVendor, reason: string, failure?: UvFailure): void {
  // One terminal event per explicit action, using app.log already collected by
  // the normal export. No raw helper output, timer or hardware probe is added.
  try {
    void invoke('log.write', { level: result.accepted ? 'info' : 'warn',
      message: '[cpu.voltage-offset] ' + JSON.stringify({ requestId, requested, vendor,
        unit: vendor === 'amd' ? 'co_step' : vendor === 'intel' ? 'mv' : '',
        accepted: result.accepted, applicationState: result.applicationState,
        applied: null, readback: false, helperExitCode: exitCode, reason, failure: failure ?? null,
        attempts: result.attempts ?? [], route: result.route ?? null, planes: result.receipt?.planes ?? {} }),
    }, { timeoutMs: 1000 }).catch(() => {});
  } catch { /* Diagnostics cannot change a write result or trigger a retry. */ }
}

export async function setUndervolt(v: number,
    expected?: Pick<UvProbe, 'vendor' | 'unit' | 'scope'>): Promise<UvSetResult> {
  const rejected = (msg: string): UvSetResult => ({
    ok: false, accepted: false, applicationState: 'failed', applied: null, msg,
  });
  if (!CPU_VOLTAGE_OFFSET_ACTIVE) return rejected('CPU 电压偏移功能未激活');
  if (!Number.isSafeInteger(v)) return rejected('电压偏移必须是有限整数');
  if (v > 0) return rejected('仅允许降压（负偏移）或以 0 恢复默认');
  const requestId = `cpu-uv:${Date.now()}:${++uvRequestSequence}`;
  try {
    const r = await shell.run(await tdpCtlExe(), ['uv', 'set', String(v), '--receipt'], 20000);
    const j = lastJsonLine(r.stdout || '');
    const vendor = validUvVendor(j?.vendor);
    const planes = j?.requested?.planes;
    const planesValid = Array.isArray(planes) &&
      (vendor === 'amd' ? planes.length === 0 : planes.length <= 2 &&
        planes.every((p: unknown) => p === 'core' || p === 'cache') && new Set(planes).size === planes.length);
    const requestMatches = vendor !== '' && j?.operation === 'static_offset' && j?.requested?.value === v &&
      planesValid && (vendor === 'amd'
        ? j?.unit === 'co_step' && j?.requested?.scope === 'amd_all_core'
        : j?.unit === 'mv' && j?.requested?.scope === 'intel_fivr_plane') &&
      (!expected || expected.vendor === vendor && expected.unit === j?.unit &&
        expected.scope.kind === j?.requested?.scope && planes.every((p: UvPlane) => expected.scope.planes.includes(p)) &&
        (j?.accepted !== true || planes.length === expected.scope.planes.length));
    const envelopeValid = requestMatches && typeof j?.accepted === 'boolean' && j?.ok === j?.accepted &&
      (j?.applicationState === 'unknown' || j?.accepted === false && j?.applicationState === 'failed') &&
      j?.applied === null && j?.readback === false && j?.observed === null &&
      finiteInt(r.exitCode) && r.exitCode >= 0 &&
      (j?.accepted !== true || r.exitCode === 0 && (vendor === 'amd' || planes.length > 0));
    // Do not attach a route or firmware cause from a different/malformed request.
    const failure = envelopeValid && vendor === 'amd' ? uvFailure(j?.failure, r.exitCode) : undefined;
    const planeWrites: Partial<Record<UvPlane, UvPlaneWriteReceipt>> | null = envelopeValid && vendor === 'intel' ? uvPlaneWrites(j?.planes, planes, j?.accepted === true) : {};
    const parsedAttempts = envelopeValid ? uvCommandAttempts(j?.attempts) : null;
    const candidateRoute = envelopeValid ? uvCommandRoute(j?.route) : undefined;
    const lastAttempt = parsedAttempts?.[parsedAttempts.length - 1];
    const traceValid = parsedAttempts !== null &&
      (j?.route == null || candidateRoute !== undefined && lastAttempt !== undefined &&
        candidateRoute.transport === lastAttempt.transport && candidateRoute.command === lastAttempt.command) &&
      (vendor === 'amd' || parsedAttempts.length === 0 && j?.route == null) &&
      (!lastAttempt || lastAttempt.accepted === j?.accepted &&
        (j?.failure == null || failure !== undefined) &&
        (!failure || failure.origin === lastAttempt.origin && failure.mailboxStatus === lastAttempt.mailboxStatus)) &&
      (j?.accepted !== true || j?.failure == null);
    if (!envelopeValid || !traceValid || planeWrites === null) {
      const result: UvSetResult = { ok: false, accepted: false, applicationState: 'unknown',
        applied: null, msg: uvFailureText('invalid_receipt', undefined, r.exitCode), reason: 'invalid_receipt' };
      logUvResult(requestId, v, result, r.exitCode, '', 'invalid_receipt');
      return result;
    }
    const attempts = parsedAttempts!;
    const route = candidateRoute;
    const accepted = j!.accepted === true;
    // Failed I/O after submission cannot establish that the device was unchanged.
    const uncertain = failure?.origin === 'response_wait' || failure?.origin === 'transport' &&
      ['command_write', 'response_read', 'invalid_response'].includes(failure.stage) ||
      !failure && (lastAttempt?.origin === 'response_wait' || lastAttempt?.origin === 'transport') ||
      Object.keys(planeWrites).length > 0;
    const applicationState = accepted || j!.applicationState === 'unknown' || uncertain ? 'unknown' : 'failed';
    const reason = uvCode(j!.reason) || (accepted ? 'accepted_no_readback' : 'helper_failure');
    const receipt: UvSetReceipt = {
      vendor: vendor as 'amd' | 'intel', ok: accepted, accepted,
      operation: 'static_offset', unit: j!.unit,
      requested: { value: v, scope: j!.requested.scope, planes: [...planes] },
      applicationState, applied: null, readback: false, observed: null,
      reason, verification: uvCode(j!.verification), helperExitCode: r.exitCode,
      ...(vendor === 'intel' ? { planes: planeWrites } : {}),
      ...(failure ? { failure } : {}), attempts, ...(route ? { route } : {}),
    };
    const planeText = vendor === 'intel' ? uvPlaneWriteText(planeWrites) : '';
    const msg = accepted ? (vendor === 'intel' ? '驱动写请求已提交；固件是否接受及实际偏移未知。' : reason)
      : planeText ? `CPU 电压偏移请求失败：${planeText}；实际偏移未知。` : uvFailureText(reason, failure, r.exitCode);
    const result: UvSetResult = { ok: accepted, accepted, applicationState, applied: null,
      msg, reason, receipt,
      ...(failure ? { failure } : {}), attempts, ...(route ? { route } : {}) };
    logUvResult(requestId, v, result, r.exitCode, vendor, reason, failure);
    return result;
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    const reason = /hardware writes are blocked during power transition|系统电源恢复尚未完成/i.test(message)
      ? 'power_transition_not_ready' : /timeout|timed out/i.test(message) ? 'helper_timeout'
        : /功耗运行库目录不可用/.test(message) ? 'runtime_directory_unavailable' : 'helper_start_failed';
    const result = { ...rejected(uvFailureText(reason, undefined, -1)),
      applicationState: reason === 'power_transition_not_ready' || reason === 'runtime_directory_unavailable' ? 'failed' as const : 'unknown' as const,
      reason };
    logUvResult(requestId, v, result, null, '', reason);
    return result;
  }
}

// ── Compatibility-only preset conversion (not part of the Settings UI contract) ──
export const UV_PRESETS = [
  { key: 'off', label: '关闭', amd: 0, intel: 0 },
  { key: 'safe', label: '安全', amd: -8, intel: -25 },
  { key: 'balance', label: '平衡', amd: -14, intel: -45 },
  { key: 'risk', label: '风险', amd: -24, intel: -75 },
];
export function uvPresetValue(vendor: 'amd' | 'intel' | '', key: string): number {
  const p = UV_PRESETS.find((x) => x.key === key);
  if (!p) return 0;
  return vendor === 'intel' ? p.intel : p.amd;
}
