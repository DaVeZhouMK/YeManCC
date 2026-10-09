import asset from './fanHostInstallerAsset.json';
export interface FanInstallerRepairPort {
  exists(path: string): Promise<boolean>;
  readTextFile(path: string): Promise<string>;
  writeTextFileAtomic(path: string, content: string): Promise<void>;
  sha256File(path: string): Promise<string>;
}
/** Only at deployment boundaries: recover one missing manifest-bound script, never DLLs or an unrelated lane. */
export async function ensureFanHostInstaller(directory: string, port: FanInstallerRepairPort): Promise<string> {
  const root = directory.replace(/[\\/]+$/, '');
  const installer = root + '\\install-fan-host-payload.ps1';
  const manifestPath = root + '\\YeManFanHost.payload.json';
  if (!(await port.exists(manifestPath))) throw new Error('Fan Host 部署不完整：缺少 YeManFanHost.payload.json');
  const manifestText = await port.readTextFile(manifestPath);
  const manifest = JSON.parse(manifestText.replace(/^\uFEFF/, ''));
  const entries = Array.isArray(manifest.files) ? manifest.files.filter((f: { path?: unknown }) =>
    typeof f.path === 'string' && f.path.toLowerCase() === 'install-fan-host-payload.ps1') : [];
  if (manifest.schemaVersion !== 2 || entries.length !== 1 || !/^[0-9a-f]{64}$/i.test(entries[0].sha256 ?? ''))
    throw new Error('FAN_INSTALLER_MANIFEST_INVALID');
  const expected = entries[0].sha256.toLowerCase();
  if (!(await port.exists(installer))) {
    const actual = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(asset.content))))
      .map(b => b.toString(16).padStart(2, '0')).join('');
    if (asset.sha256 !== expected || actual !== expected) throw new Error('FAN_INSTALLER_REPAIR_VERSION_MISMATCH');
    if (await port.readTextFile(manifestPath) !== manifestText) throw new Error('FAN_INSTALLER_MANIFEST_CHANGED');
    if (!(await port.exists(installer))) await port.writeTextFileAtomic(installer, asset.content);
  }
  if ((await port.sha256File(installer)).toLowerCase() !== expected) throw new Error('FAN_INSTALLER_HASH_MISMATCH');
  return installer;
}
