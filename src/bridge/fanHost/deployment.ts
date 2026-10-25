/** [LIFECYCLE:FAN:DEPLOYMENT] 开启时的小清单/部署记录合同；IO仍由原启动方提供。
 * 规则改变须提升原validation版本；清单/记录命中不证明设备会话、写许可或物理控制。
 * 保持原记录格式、验证时点及失败行为；实际进程/凭据与HC初始化仍由原Launcher/Host管理。
 */
import { fanDiagnosticLog } from '../fanDiagnostics';
import { joinWindowsPath, parseJson, type FanHostConfig, type FanHostPayloadManifest } from './startupConfiguration';

/**
 * FAN-927 §2（2026-09-27 用户批准）：**完整文件校验归部署边界**。
 *
 * 这里只有：① 两个**小清单**的摘要（允许的固定开销）；② 一份小"部署记录"的匹配。
 * 正常开启不再逐文件 `exists`、不再为每个文件起一次 `certutil`、不再运行安装脚本、
 * 不再遍历依赖目录。触发一次完整验证（安装脚本进程内 SHA-256）仅限：新装/更新/回滚部署、
 * 包代描述变更、记录缺失或不匹配、显式修复——其中也包含便携包/旧安装的**首次明确开启**一次
 * 快速批量初始化。
 *
 * 明确接受的信任调整：部署记录证明该包代在部署时完整，不保证每个 DLL 此刻仍与发布字节相同；
 * 绕过更新流程只改 DLL 并保持清单不变的离线替换不再由每次开启的全量哈希发现。完整性由
 * 构建、部署与修复环节负责。
 */
const FAN_HOST_PAYLOAD_MANIFEST_FILE = 'YeManFanHost.payload.json';

const FAN_HOST_RUNTIME_MANIFEST_FILE = 'HandheldCompanion.runtime.json';

const FAN_HOST_DEPLOYMENT_RECORD_FILE = 'YeManFanHost.deployment.json';

const FAN_HOST_DEPLOYMENT_RECORD_SCHEMA = 1;

/**
 * 部署验证规则版本：任何改变"一个包代是否算已完整验证"判据的改动都必须提升它，
 * 使既有记录自动失效并触发一次重新验证。这是"运行不遍历 DLL"的唯一替代物。
 */
const FAN_HOST_VALIDATION_RULES_VERSION = 1;


interface FanHostDeploymentIdentity {
  payloadRoot: string;
  runtimeRoot: string;
  payloadManifestSha256: string;
  runtimeManifestSha256: string;
  runtimeId: string;
}


interface FanHostDeploymentRecord extends FanHostDeploymentIdentity {
  schemaVersion: number;
  validationRulesVersion: number;
  result: string;
}


function normalizeDeploymentPath(value: string): string {
  return value.replace(/[\\/]+$/, '').replace(/\//g, '\\').toLowerCase();
}


/**
 * FAN-927 §2：部署边界判定所需的**全部** I/O。生产为真实 `fs`/安装脚本；
 * 自测注入替身，以便用**实际调用次数**证明"记录命中 ⇒ 逐文件 certutil=0、
 * 安装全验证进程=0、全文件遍历/哈希=0"。
 */
export interface FanHostDeploymentIo {
  exists(path: string): Promise<boolean>;
  readTextFile(path: string, maxBytes: number): Promise<string>;
  sha256File(path: string): Promise<string>;
  /** 完整验证（新装/更新/回滚/修复/无记录首次开启）。只允许在记录未命中时调用。 */
  runInstaller(config: FanHostConfig, fanStateDirectory: string): Promise<void>;
}


/** 只对**单个小清单**取摘要；失败/不可读一律返回 null（fail-closed）。 */
async function hashDeploymentFile(io: FanHostDeploymentIo, path: string): Promise<string | null> {
  try {
    const digest = await io.sha256File(path);
    return typeof digest === 'string' && /^[0-9a-f]{64}$/i.test(digest.trim())
      ? digest.trim().toLowerCase()
      : null;
  } catch {
    return null;
  }
}


async function readDeploymentRecord(
  io: FanHostDeploymentIo,
  recordPath: string,
): Promise<FanHostDeploymentRecord | null> {
  try {
    if (!(await io.exists(recordPath))) return null;
    const raw = await io.readTextFile(recordPath, 64 * 1024);
    const record = parseJson<FanHostDeploymentRecord>(raw);
    if (!record || typeof record !== 'object') return null;
    if (record.schemaVersion !== FAN_HOST_DEPLOYMENT_RECORD_SCHEMA) return null;
    if (typeof record.payloadRoot !== 'string' || typeof record.runtimeRoot !== 'string') return null;
    if (typeof record.payloadManifestSha256 !== 'string' || typeof record.runtimeManifestSha256 !== 'string') return null;
    if (typeof record.runtimeId !== 'string' || typeof record.result !== 'string') return null;
    return record;
  } catch {
    return null;
  }
}


function deploymentRecordMatches(record: FanHostDeploymentRecord, identity: FanHostDeploymentIdentity): boolean {
  return record.result === 'ok'
    && record.validationRulesVersion === FAN_HOST_VALIDATION_RULES_VERSION
    && record.payloadRoot === identity.payloadRoot
    && record.runtimeRoot === identity.runtimeRoot
    && record.payloadManifestSha256 === identity.payloadManifestSha256
    && record.runtimeManifestSha256 === identity.runtimeManifestSha256
    && record.runtimeId === identity.runtimeId;
}


/** 当前包代描述。只读 payload manifest（取摘要 + runtimeId）与 runtime manifest（取摘要）。 */
async function currentDeploymentIdentity(
  io: FanHostDeploymentIo,
  config: FanHostConfig,
): Promise<FanHostDeploymentIdentity | null> {
  const payloadManifestPath = joinWindowsPath(config.hostDirectory, FAN_HOST_PAYLOAD_MANIFEST_FILE);
  const runtimeManifestPath = joinWindowsPath(config.hcRuntimeDirectory, FAN_HOST_RUNTIME_MANIFEST_FILE);
  const payloadManifestSha256 = await hashDeploymentFile(io, payloadManifestPath);
  if (!payloadManifestSha256) return null;
  const runtimeManifestSha256 = await hashDeploymentFile(io, runtimeManifestPath);
  if (!runtimeManifestSha256) return null;
  let runtimeId = '';
  try {
    const manifest = parseJson<FanHostPayloadManifest>(await io.readTextFile(payloadManifestPath, 2 * 1024 * 1024));
    runtimeId = typeof manifest.runtimeId === 'string' ? manifest.runtimeId.trim() : '';
  } catch {
    return null;
  }
  if (!runtimeId) return null;
  return {
    payloadRoot: normalizeDeploymentPath(config.hostDirectory),
    runtimeRoot: normalizeDeploymentPath(config.hcRuntimeDirectory),
    payloadManifestSha256,
    runtimeManifestSha256,
    runtimeId,
  };
}


/** FAN-927 §2：部署边界判定的**唯一**实现（唯一副作用 = 必要时调用一次完整验证）。 */
export type FanHostDeploymentDecision = 'record-hit' | 'initialized';


export async function resolveFanHostDeployment(
  config: FanHostConfig,
  fanStateDirectory: string,
  io: FanHostDeploymentIo,
): Promise<FanHostDeploymentDecision> {
  const recordPath = joinWindowsPath(fanStateDirectory, FAN_HOST_DEPLOYMENT_RECORD_FILE);
  const identity = await currentDeploymentIdentity(io, config);
  if (!identity) {
    throw new Error(`Fan Host 部署身份不可读：缺少或无法解析 ${FAN_HOST_PAYLOAD_MANIFEST_FILE}/${FAN_HOST_RUNTIME_MANIFEST_FILE}`);
  }
  const record = await readDeploymentRecord(io, recordPath);
  if (record && deploymentRecordMatches(record, identity)) {
    fanDiagnosticLog('lifecycle.deployment-record-hit', {
      runtimeId: identity.runtimeId,
      payloadRoot: identity.payloadRoot,
      recordPath,
    });
    return 'record-hit';
  }
  fanDiagnosticLog('lifecycle.deployment-record-miss', {
    reason: record ? 'identity-mismatch' : 'record-absent',
    runtimeId: identity.runtimeId,
    recordPath,
  });
  // 新装/更新/回滚、包代描述变更、记录缺失或不匹配、显式修复、portable 首次明确开启：
  // 只在这里做**一次**完整验证（安装脚本进程内 SHA-256），成功即继续。
  await io.runInstaller(config, fanStateDirectory);
  const confirmed = await readDeploymentRecord(io, recordPath);
  if (!confirmed || !deploymentRecordMatches(confirmed, identity)) {
    throw new Error('Fan Host 部署记录未生成或与当前包代不一致，拒绝启动（完整性由部署/修复环节负责）');
  }
  fanDiagnosticLog('lifecycle.deployment-record-written', { runtimeId: identity.runtimeId, recordPath });
  return 'initialized';
}
