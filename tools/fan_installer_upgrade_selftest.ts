import { createHash, webcrypto } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { ensureFanHostInstaller, type FanInstallerRepairPort } from '../src/bridge/fanHostInstallerRepair';
import asset from '../src/bridge/fanHostInstallerAsset.json';
if (!globalThis.crypto) Object.defineProperty(globalThis, 'crypto', { value: webcrypto });
const assert = (v: unknown, m: string) => { if (!v) throw new Error(m); };
const root = 'C:\\Fixture\\PowerControl\\fan-host-v2';
const script = root + '\\install-fan-host-payload.ps1';
const manifest = root + '\\YeManFanHost.payload.json';
const digest = (s: string | Buffer) => createHash('sha256').update(s).digest('hex');
class Port implements FanInstallerRepairPort {
  files = new Map<string, string>(); writes: string[] = []; failWrite = false; corruptWrite = false; changeManifest = false; reads = 0;
  constructor(hash = asset.sha256) { this.files.set(manifest, JSON.stringify({ schemaVersion: 2, files: [{ path: 'install-fan-host-payload.ps1', sha256: hash }] })); }
  async exists(p: string) { return this.files.has(p); }
  async readTextFile(p: string) { this.reads += 1; const s = this.files.get(p); if (s == null) throw new Error('missing'); return this.changeManifest && this.reads > 1 ? s + ' ' : s; }
  async writeTextFileAtomic(p: string, s: string) { if (this.failWrite) throw new Error('write-denied'); this.writes.push(p); this.files.set(p, this.corruptWrite ? 'corrupt' : s); }
  async sha256File(p: string) { return digest(await this.readTextFile(p)); }
}
let checks = 0;
function check(v: unknown, m: string) { assert(v,m); checks += 1; }
async function rejected(p: Port, code: string) { await ensureFanHostInstaller(root,p).then(()=>{throw new Error('unexpected success');},e=>check(String(e).includes(code),code)); }
async function main() {
  check(digest(asset.content) === asset.sha256, 'asset UTF8/BOM byte identity');
  check(digest(readFileSync('PowerControl/fan-host/install-fan-host-payload.ps1')) === asset.sha256, 'single authoritative source');
  const upgrade = new Port();
  check(await ensureFanHostInstaller(root,upgrade) === script, '0.28 upgrade target stays v2');
  check(upgrade.writes.length === 1 && upgrade.writes[0] === script, 'only missing installer is repaired');
  check(digest(upgrade.files.get(script)!) === asset.sha256, 'repair matches manifest');
  await ensureFanHostInstaller(root,upgrade);
  check(upgrade.writes.length === 1, 'repeat deployment never rewrites existing script');
  const stale = new Port('0'.repeat(64)); await rejected(stale,'VERSION_MISMATCH'); check(stale.writes.length === 0,'no mixed-version repair');
  const corrupt = new Port(); corrupt.files.set(script,'old-script'); await rejected(corrupt,'HASH_MISMATCH'); check(corrupt.writes.length === 0,'existing script not overwritten');
  const absent = new Port(); absent.files.clear(); await rejected(absent,'YeManFanHost.payload.json'); check(absent.writes.length === 0,'no manifest no repair');
  const changed = new Port(); changed.changeManifest=true; await rejected(changed,'MANIFEST_CHANGED'); check(changed.writes.length === 0,'upgrade race does not write');
  const denied = new Port(); denied.failWrite=true; await rejected(denied,'write-denied');
  const damaged = new Port(); damaged.corruptWrite=true; await rejected(damaged,'HASH_MISMATCH');
  const duplicate = new Port(); duplicate.files.set(manifest,JSON.stringify({schemaVersion:2,files:[{path:'install-fan-host-payload.ps1',sha256:asset.sha256},{path:'install-fan-host-payload.ps1',sha256:asset.sha256}]}));
  await rejected(duplicate,'MANIFEST_INVALID');
  const existing = new Port(); existing.files.set(script,asset.content); await ensureFanHostInstaller(root,existing); check(existing.writes.length===0,'complete ZIP installs unchanged');
  console.log('fan installer upgrade selftest: PASS ('+checks+' checks; missing/complete/stale/corrupt/race/write failure)');
}
void main();
