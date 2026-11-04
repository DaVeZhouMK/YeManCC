/** [LIFECYCLE:FAN:DEPLOYMENT] 开启时的小清单/部署记录合同；IO仍由原启动方提供。
 * 规则改变须提升原validation版本；清单/记录命中不证明设备会话、写许可或物理控制。
 * 保持原记录格式、验证时点及失败行为；实际进程/凭据与HC初始化仍由原Launcher/Host管理。
 */
import { fanDiagnosticLog } from '../fanDiagnostics';
import { joinWindowsPath, type FanHostConfig } from './startupConfiguration';

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



/** Faults deny launcher admission; a record is never a UI/hardware-ready receipt. */
export class FanHostDeploymentFault extends Error {
  constructor(readonly code: 'invalid' | 'future' | 'io' | 'changed' | 'timeout', message: string) { super(message); }
}
const DEPLOYMENT_IO_MS = 5000;
const DEPLOYMENT_MANIFEST_BYTES = 2 * 1024 * 1024;
async function bounded<T>(operation: Promise<T>, timeout = DEPLOYMENT_IO_MS): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  try { return await Promise.race([operation, new Promise<never>((_, reject) => { timer = setTimeout(() => reject(new FanHostDeploymentFault('timeout', 'Fan Host 部署读取/确认超时，请显式重试')), timeout); })]); }
  finally { if (timer !== undefined) clearTimeout(timer); }
}
const invalid = (message: string): never => { throw new FanHostDeploymentFault('invalid', message); };
function deploymentJson(raw: unknown, maximum: number): Record<string, any> {
  if (typeof raw !== 'string' || raw.length > maximum) return invalid('Fan Host 部署文档超限');
  let bytes = 0;
  for (const c of raw) { const n = c.codePointAt(0)!; if (n >= 0xd800 && n <= 0xdfff) return invalid('Fan Host 部署编码无效'); bytes += n < 128 ? 1 : n < 2048 ? 2 : n < 65536 ? 3 : 4; }
  if (bytes > maximum) return invalid('Fan Host 部署文档超限');
  const text = raw.charCodeAt(0) === 0xfeff ? raw.slice(1) : raw; let at = 0;
  const space = () => { while (/[ \t\r\n]/.test(text[at] || '\0')) ++at; };
  const string = (): string => {
    space(); if (text[at++] !== '"') return invalid('Fan Host 部署JSON无效'); const begin = at - 1;
    while (at < text.length) { const c = text[at++]; if (c === '\\') { ++at; continue; } if (c === '"') {
      let v: string; try { v = JSON.parse(text.slice(begin, at)); } catch { return invalid('Fan Host 部署JSON无效'); }
      for (const c of v) { const n = c.codePointAt(0)!; if (n >= 0xd800 && n <= 0xdfff) return invalid('Fan Host 部署编码无效'); } return v;
    } } return invalid('Fan Host 部署JSON截断');
  };
  const value = (depth: number): void => {
    space(); const c = text[at];
    if (c === '{' || c === '[') {
      if (depth > 16) return invalid('Fan Host 部署JSON过深'); const end = c === '{' ? '}' : ']'; ++at; space(); if (text[at] === end) { ++at; return; }
      const keys = new Set<string>();
      for (;;) { if (c === '{') { const name = string(); if (keys.has(name)) return invalid('Fan Host 部署字段重复'); keys.add(name); space(); if (text[at++] !== ':') return invalid('Fan Host 部署JSON无效'); } value(depth + 1); space(); const n = text[at++]; if (n === end) return; if (n !== ',') return invalid('Fan Host 部署JSON无效'); }
    }
    if (c === '"') { string(); return; }
    const token = /^(?:true|false|null|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?)/.exec(text.slice(at))?.[0];
    if (!token || (/^-?\d/.test(token) && !Number.isFinite(Number(token)))) return invalid('Fan Host 部署JSON数字无效'); at += token.length;
  };
  value(0); space(); if (at !== text.length) return invalid('Fan Host 部署JSON尾部无效');
  let doc: Record<string, any>; try { doc = JSON.parse(text); } catch { return invalid('Fan Host 部署JSON无效'); }
  if (!doc || typeof doc !== 'object' || Array.isArray(doc)) return invalid('Fan Host 部署根必须为对象'); return doc;
}
function schema(doc: Record<string, any>, expected: number) {
  if (Number.isSafeInteger(doc.schemaVersion) && doc.schemaVersion > expected) throw new FanHostDeploymentFault('future', 'Fan Host 未来部署版本已保留，未调用安装器');
  if (doc.schemaVersion !== expected) invalid('Fan Host 部署版本类型无效');
}
function absoluteDirectory(value: unknown): value is string {
  return typeof value === 'string' && /^(?:[a-z]:[\\/]|\\\\[^\\/]+[\\/][^\\/]+)/i.test(value) && !/[\x00-\x1f]/.test(value) && !value.split(/[\\/]/).some(p => p === '.' || p === '..');
}
const runtimeName = (value: unknown): value is string => typeof value === 'string' && /^[a-z0-9][a-z0-9._-]{0,159}$/i.test(value);
const digestText = (value: unknown): value is string => typeof value === 'string' && /^[0-9a-f]{64}$/i.test(value);
async function hashDeploymentFile(io: FanHostDeploymentIo, path: string): Promise<string> {
  const digest = await bounded(io.sha256File(path));
  if (typeof digest !== 'string' || !digestText(digest.trim())) return invalid('Fan Host 小清单摘要无效');
  return digest.trim().toLowerCase();
}
function validateRecord(doc: Record<string, any>): FanHostDeploymentRecord {
  schema(doc, FAN_HOST_DEPLOYMENT_RECORD_SCHEMA);
  if (Number.isSafeInteger(doc.validationRulesVersion) && doc.validationRulesVersion > FAN_HOST_VALIDATION_RULES_VERSION) throw new FanHostDeploymentFault('future', 'Fan Host 未来部署验证记录已保留');
  if (!Number.isSafeInteger(doc.validationRulesVersion) || doc.validationRulesVersion < 0 || !absoluteDirectory(doc.payloadRoot) || !absoluteDirectory(doc.runtimeRoot) || !digestText(doc.payloadManifestSha256) || !digestText(doc.runtimeManifestSha256) || !runtimeName(doc.runtimeId) || typeof doc.result !== 'string' || !['ok','failed'].includes(doc.result)) return invalid('Fan Host 部署记录字段无效');
  for (const name of ['payloadFileCount','runtimeFileCount']) if (doc[name] !== undefined && (!Number.isSafeInteger(doc[name]) || doc[name] <= 0 || doc[name] > 100000)) return invalid('Fan Host 部署记录数量无效');
  if (doc.deployedAtUtc !== undefined && (typeof doc.deployedAtUtc !== 'string' || !/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,7})?Z$/.test(doc.deployedAtUtc) || !Number.isFinite(Date.parse(doc.deployedAtUtc)))) return invalid('Fan Host 部署时间无效');
  return doc as FanHostDeploymentRecord;
}
type ReadRecord = { raw: string | null; record: FanHostDeploymentRecord | null };
async function readDeploymentRecord(io: FanHostDeploymentIo, recordPath: string): Promise<ReadRecord> {
  const exists = await bounded(io.exists(recordPath));
  if (exists === false) return { raw: null, record: null };
  if (exists !== true) throw new FanHostDeploymentFault('io', 'Fan Host 部署记录存在性未知，未按缺失初始化');
  const raw = await bounded(io.readTextFile(recordPath, 64 * 1024));
  return { raw, record: validateRecord(deploymentJson(raw, 64 * 1024)) };
}
function manifestFiles(doc: Record<string, any>) {
  if (!Array.isArray(doc.files) || doc.files.length === 0 || doc.files.length > 16384) return invalid('Fan Host 小清单条目无效');
  const seen = new Set<string>();
  for (const entry of doc.files) {
    if (!entry || typeof entry !== 'object' || Array.isArray(entry) || typeof entry.path !== 'string' || !entry.path || /[\x00-\x1f:]/.test(entry.path) || /^[\\/]/.test(entry.path) || entry.path.split(/[\\/]/).some((p: string) => !p || p === '.' || p === '..') || !digestText(entry.sha256)) return invalid('Fan Host 小清单路径/摘要无效');
    const name = entry.path.replace(/\//g, '\\').toLowerCase(); if (seen.has(name)) return invalid('Fan Host 小清单路径重复'); seen.add(name);
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

async function currentDeploymentIdentity(io: FanHostDeploymentIo, config: FanHostConfig): Promise<FanHostDeploymentIdentity & { payloadText: string; runtimeText: string }> {
  if (!absoluteDirectory(config.hostDirectory) || !absoluteDirectory(config.hcRuntimeDirectory)) return invalid('Fan Host 目录身份未知');
  const payloadPath = joinWindowsPath(config.hostDirectory, FAN_HOST_PAYLOAD_MANIFEST_FILE), runtimePath = joinWindowsPath(config.hcRuntimeDirectory, FAN_HOST_RUNTIME_MANIFEST_FILE);
  const payloadText = await bounded(io.readTextFile(payloadPath, DEPLOYMENT_MANIFEST_BYTES)), runtimeText = await bounded(io.readTextFile(runtimePath, DEPLOYMENT_MANIFEST_BYTES));
  const manifest = deploymentJson(payloadText, DEPLOYMENT_MANIFEST_BYTES), runtime = deploymentJson(runtimeText, DEPLOYMENT_MANIFEST_BYTES);
  schema(manifest, 2); schema(runtime, 1); manifestFiles(manifest); manifestFiles(runtime);
  if (!runtimeName(manifest.runtimeId) || runtime.runtimeId !== manifest.runtimeId || typeof manifest.runtimeManifest !== 'string' || !/^\.\.[\\/][^\\/:]+(?:[\\/][^\\/:]+)*[\\/]HandheldCompanion\.runtime\.json$/i.test(manifest.runtimeManifest) || manifest.runtimeManifest.slice(3).split(/[\\/]/).some((p: string) => p === '.' || p === '..')) return invalid('Fan Host runtime 清单绑定无效');
  const host = config.hostDirectory.replace(/[\\/]+$/, ''), parent = host.slice(0, Math.max(host.lastIndexOf('\\'), host.lastIndexOf('/')));
  if (normalizeDeploymentPath(joinWindowsPath(parent, manifest.runtimeManifest.slice(3))) !== normalizeDeploymentPath(runtimePath)) return invalid('Fan Host runtime 路径与清单不一致');
  const payloadManifestSha256 = await hashDeploymentFile(io, payloadPath), runtimeManifestSha256 = await hashDeploymentFile(io, runtimePath);
  if (await bounded(io.readTextFile(payloadPath, DEPLOYMENT_MANIFEST_BYTES)) !== payloadText || await bounded(io.readTextFile(runtimePath, DEPLOYMENT_MANIFEST_BYTES)) !== runtimeText) throw new FanHostDeploymentFault('changed', 'Fan Host 小清单读取期间已更替，请显式重试');
  return {payloadRoot:normalizeDeploymentPath(config.hostDirectory),runtimeRoot:normalizeDeploymentPath(config.hcRuntimeDirectory),payloadManifestSha256,runtimeManifestSha256,runtimeId:manifest.runtimeId,payloadText,runtimeText};
}


/** FAN-927 §2：部署边界判定的**唯一**实现（唯一副作用 = 必要时调用一次完整验证）。 */
export type FanHostDeploymentDecision = 'record-hit' | 'initialized';



async function confirmObservedIdentity(io: FanHostDeploymentIo, config: FanHostConfig, identity: { payloadText: string; runtimeText: string }): Promise<void> {
  if (await bounded(io.readTextFile(joinWindowsPath(config.hostDirectory, FAN_HOST_PAYLOAD_MANIFEST_FILE), DEPLOYMENT_MANIFEST_BYTES)) !== identity.payloadText ||
      await bounded(io.readTextFile(joinWindowsPath(config.hcRuntimeDirectory, FAN_HOST_RUNTIME_MANIFEST_FILE), DEPLOYMENT_MANIFEST_BYTES)) !== identity.runtimeText)
    throw new FanHostDeploymentFault('changed', 'Fan Host 小清单在部署记录观察期间已更替，请显式重试');
}

interface DeploymentRun { signature: string; task: Promise<FanHostDeploymentDecision>; installing: boolean; finished: boolean; }
const deploymentRuns = new Map<string, DeploymentRun>();
/** Coalesce only this frontend's explicit requests. The external installer is not an OS CAS writer. */
export function resolveFanHostDeployment(config: FanHostConfig, fanStateDirectory: string, io: FanHostDeploymentIo): Promise<FanHostDeploymentDecision> {
  const selected = { ...config };
  if (!absoluteDirectory(fanStateDirectory)) return Promise.reject(new FanHostDeploymentFault('invalid', 'Fan Host 状态目录未知'));
  const recordPath = joinWindowsPath(fanStateDirectory, FAN_HOST_DEPLOYMENT_RECORD_FILE), key = normalizeDeploymentPath(recordPath), signature = JSON.stringify([selected.hostDirectory,selected.hcRuntimeDirectory]);
  const previous = deploymentRuns.get(key);
  if (previous) return previous.signature === signature ? previous.task : Promise.reject(new FanHostDeploymentFault('changed', 'Fan Host 同目录仍有另一部署确认，未启动第二 writer'));
  const run: DeploymentRun = {signature,task:Promise.resolve('record-hit'),installing:false,finished:false};
  const release = () => { if (run.finished && !run.installing && deploymentRuns.get(key) === run) deploymentRuns.delete(key); };
  run.task = (async () => {
    const identity = await currentDeploymentIdentity(io, selected);
    const record = await readDeploymentRecord(io, recordPath);
    // Two bounded observations before granting either a hit or writer permission.
    const preimage = await readDeploymentRecord(io, recordPath);
    if (preimage.raw !== record.raw) throw new FanHostDeploymentFault('changed', 'Fan Host 部署原件已更替，未覆盖新文件');
    await confirmObservedIdentity(io, selected, identity);
    if (config.hostDirectory !== selected.hostDirectory || config.hcRuntimeDirectory !== selected.hcRuntimeDirectory) throw new FanHostDeploymentFault('changed', 'Fan Host 调用方目录代次已改变');
    if (record.record && deploymentRecordMatches(record.record, identity)) {
      fanDiagnosticLog('lifecycle.deployment-record-hit', {runtimeId:identity.runtimeId,payloadRoot:identity.payloadRoot,recordPath});
      return 'record-hit';
    }
    fanDiagnosticLog('lifecycle.deployment-record-miss', {reason:record.record?'identity-mismatch':'record-absent',runtimeId:identity.runtimeId,recordPath});
    // Version1 integrity rules still belong to the unchanged installer. This enforces
    // its existing supported document/identity shape; no new full-hash policy or receipt version.
    run.installing = true;
    const installer = Promise.resolve().then(() => io.runInstaller(selected, fanStateDirectory));
    void installer.finally(() => {run.installing=false;release();}).catch(() => {});
    await bounded(installer, 60000);
    const confirmed = await readDeploymentRecord(io, recordPath);
    const after = await currentDeploymentIdentity(io, selected);
    if (!confirmed.record || !deploymentRecordMatches(confirmed.record, identity) || !deploymentRecordMatches(confirmed.record, after) || after.payloadText !== identity.payloadText || after.runtimeText !== identity.runtimeText || config.hostDirectory !== selected.hostDirectory || config.hcRuntimeDirectory !== selected.hcRuntimeDirectory) throw new FanHostDeploymentFault('changed', 'Fan Host 部署/当前包代未确认，拒绝启动，请显式重试');
    if ((await readDeploymentRecord(io, recordPath)).raw !== confirmed.raw) throw new FanHostDeploymentFault('changed', 'Fan Host 部署回执已更替，未确认成功');
    await confirmObservedIdentity(io, selected, identity);
    fanDiagnosticLog('lifecycle.deployment-record-written', {runtimeId:identity.runtimeId,recordPath});
    return 'initialized';
  })().finally(() => {run.finished=true;release();});
  deploymentRuns.set(key,run);return run.task;
}
