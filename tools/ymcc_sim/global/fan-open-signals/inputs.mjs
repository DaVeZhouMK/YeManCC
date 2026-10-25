import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';

export const baselinePins = Object.freeze({
  'YeManFanHost.dll': '5e94170105c65bb0503751460082db2a0827061b075c55afe92f7ef9b3b91993',
  'YeManFanBackend.dll': '3b2d584d66dff1d50b7dc07e09de95608f4241a15953de5ccc392b6678c9691c',
});
export const baselineRelativeRoot = 'tools/ymcc_sim/global/fan-open-signals/fixtures/legacy-host-5e941';
export const cecilToolProject = 'tools/hc-application-dependency-trim/ApplicationTrim.csproj';
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const isFile = file => fs.existsSync(file) && fs.statSync(file).isFile();
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));

export class BlockedInputError extends Error {
  constructor(code, message, detail) {
    super(message);
    this.name = 'BlockedInputError';
    this.code = code;
    this.detail = detail;
  }
}

export function resolveBaseline({ repoRoot, legacyHostRoot }) {
  const fixtureRoot = path.join(repoRoot, baselineRelativeRoot);
  const manifestPath = path.join(fixtureRoot, 'provenance.json');
  if (!isFile(manifestPath)) {
    throw new BlockedInputError('FAN_OPEN_BASELINE_MISSING', 'Fixed pre-fix baseline provenance is missing', { manifestPath });
  }
  const manifest = read(manifestPath);
  if (manifest.schemaVersion !== 1 || manifest.id !== 'host-5e941-backend-3b2d' ||
      !Array.isArray(manifest.files) || manifest.files.length !== 2 ||
      Object.entries(baselinePins).some(([name, pin]) =>
        manifest.files.filter(item => item.path === name && item.sha256 === pin).length !== 1)) {
    throw Object.assign(new Error('Fixed baseline provenance does not match the hard-pinned pre-fix identities'), { code: 'FAN_OPEN_BASELINE_IDENTITY' });
  }
  const root = path.resolve(legacyHostRoot ?? fixtureRoot);
  const files = Object.entries(baselinePins).map(([name, pin]) => {
    const file = path.join(root, name);
    if (!isFile(file)) {
      throw new BlockedInputError('FAN_OPEN_BASELINE_MISSING', 'Fixed pre-fix baseline DLL is missing', { path: file });
    }
    const actual = hash(file);
    if (actual !== pin) {
      throw Object.assign(new Error(`Pre-fix baseline identity mismatch: ${name}; expected ${pin}, received ${actual}`), {
        code: 'FAN_OPEN_BASELINE_IDENTITY',
      });
    }
    return { path: file, sha256: actual };
  });
  return { root, source: legacyHostRoot === undefined ? 'repository-fixed-fixture' : 'explicit-legacy-host-root',
    manifestPath, manifestSha256: hash(manifestPath), files };
}

export function resolveCecil({ repoRoot, cecilPath, env = process.env, userHome = os.homedir() }) {
  if (cecilPath !== undefined) {
    const file = path.resolve(cecilPath);
    if (!isFile(file)) {
      throw new BlockedInputError('FAN_OPEN_CECIL_MISSING', 'Explicit Cecil DLL is missing; no fallback is used', { path: file });
    }
    return { path: file, sha256: hash(file), source: 'explicit-cecil-path' };
  }
  const project = path.join(repoRoot, cecilToolProject);
  // Follow the version already approved by the repository tool; never scan
  // historical evidence folders, download a package, or guess a new version.
  const hint = isFile(project) ? fs.readFileSync(project, 'utf8')
    .match(/<Reference\s+Include="Mono\.Cecil"\s*>\s*<HintPath>([^<]+)<\/HintPath>/)?.[1] : undefined;
  const normalized = hint?.replaceAll('\\', '/');
  const packagePath = normalized?.match(/^\$\(UserProfile\)\/\.nuget\/packages\/(microsoft\.net\.illink\.tasks\/[^/]+\/tools\/net\/Mono\.Cecil\.dll)$/)?.[1];
  if (!packagePath) {
    throw new BlockedInputError('FAN_OPEN_CECIL_MISSING', 'Approved repository Cecil dependency hint is unavailable; supply --cecil-path', { project });
  }
  const candidates = [];
  if (env.NUGET_PACKAGES) candidates.push(path.resolve(env.NUGET_PACKAGES, packagePath));
  const profile = env.USERPROFILE || userHome;
  if (profile) candidates.push(path.resolve(profile, '.nuget', 'packages', packagePath));
  for (const file of [...new Set(candidates)]) {
    if (isFile(file)) return { path: file, sha256: hash(file), source: 'approved-repository-tool-dependency',
      toolProject: project, toolProjectSha256: hash(project) };
  }
  throw new BlockedInputError('FAN_OPEN_CECIL_MISSING', 'Approved local Cecil dependency is missing; supply --cecil-path', { project, searched: candidates });
}

const flags = Object.freeze({
  '--evidence-dir': 'evidenceDir',
  '--legacy-host-root': 'legacyHostRoot',
  '--hc-assembly': 'hcAssembly',
  '--cecil-path': 'cecilPath',
});
export function parseArgs(args) {
  const parsed = {};
  for (let index = 0; index < args.length; index++) {
    const flag = args[index];
    const key = flags[flag];
    if (!Object.hasOwn(flags, flag)) throw new Error(`Unknown argument: ${flag}`);
    if (Object.hasOwn(parsed, key)) throw new Error(`Duplicate argument: ${flag}`);
    const value = args[++index];
    if (typeof value !== 'string' || !value.trim() || value.startsWith('--')) {
      throw new Error(`Missing value for ${flag}`);
    }
    parsed[key] = value;
  }
  if (!parsed.evidenceDir) throw new Error('--evidence-dir is required');
  return parsed;
}
