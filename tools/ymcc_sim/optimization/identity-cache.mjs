/**
 * README / integration contract (synchronous; Node standard library only)
 *
 * fingerprintCase({repoRoot, adapter, toolIdentities=[], options={}}) returns
 * {schemaVersion:1, identity:sha256|null, cacheable, reasons, manifest}.
 * Require reviewed options.closureComplete === true, options.resolvedInputs
 * (paths or {path,sha256?,status?} rows), and toolClosureComplete === true.
 * adapter.dependencyInputs, if declared, uses the same array format. These
 * declarations are caller audits, never inferred from extension or old PASS.
 * toolIdentities contains absolute paths or {path,sha256?}; non-Node kinds
 * require explicit tools. Node and this engine are automatically fingerprinted.
 * Sources stay inside repoRoot; artifacts/tools are absolute regular files.
 * All adapter fields except id/title/cacheIdentity, all options, resolved paths,
 * file bytes and node/version/platform/arch participate. No mtime/Git shortcuts.
 *
 * inspectCachedResult({identity:<full fingerprint return>, cacheEntry}) accepts
 * {identity:<hash>, result:{path:<absolute original result.json>,sha256},
 *  evidencePins:[{path,sha256}, ...]}.
 * Pins MUST cover result.json, sibling stdout.log/stderr.log, adapter.resultFiles,
 * structuredEvidence, bundle and bundledOutputs. All pins and identity inputs
 * are read again. Raw result status/flags cannot be overridden by cache metadata.
 * Returns {hit,reusable,status,previousStatus,executedThisRun:false,
 * actualPassedThisRun:false,reasons,reference}. PASS becomes CACHED_REFERENCE;
 * valid FAIL/UNKNOWN/BLOCKED/TIMEOUT/INCOMPLETE/NOT_RUN becomes
 * UNCHANGED_FAILURE_REFERENCE (reusable:true, hit:false). Invalid entries are
 * CACHE_MISS; unproven identities are UNCACHEABLE. No return has status PASS.
 * PASS requires sourceChanged strictly false, exitCode 0, and no failed
 * runner evidence/completion flags. candidateArtifactChanged must be false when
 * any candidate is declared/recorded; absent is N/A only with no candidates or
 * bundle outputs. Never manufacture absent historic flags.
 * The caller owns the cache index, execution/retry policy and post-execution
 * fingerprinting. Hashes detect changed bytes; they do not authenticate a writer
 * that can replace both original evidence and the entire trusted cache index.
 *
 * deduplicateCases(adapters) returns {canonical,aliases,deduplicated}; canonical
 * retains original objects, aliases are {id,canonicalId}, deduplicated adds
 * reason. Conflicting same IDs throw code ADAPTER_ID_CONFLICT. Same ID and exact
 * definition may dedup. Different IDs require adapter.cacheIdentity containing
 * a full, current fingerprint matching that adapter and an identical execution
 * manifest. No parent/child/name/containment guesses. Unproven cases stay separate.
 *
 * reasons is always an array of {code,field?,path?}. Missing/unresolved/non-file
 * inputs or symlinks escaping their declared root prohibit reuse. No processes,
 * registry scans, cache writes, product changes or formal fixtures run here.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const SCHEMA_VERSION = 1;
const SELF = fileURLToPath(import.meta.url);
const SHA = /^[a-f0-9]{64}$/i;
const FAILURE_STATUSES = new Set(['FAIL', 'UNKNOWN', 'BLOCKED', 'TIMEOUT', 'INCOMPLETE', 'NOT_RUN']);
const compare = (a, b) => a < b ? -1 : a > b ? 1 : 0;
const digest = value => crypto.createHash('sha256').update(value).digest('hex');
const problem = (code, field, entry) => ({ code, ...(field ? { field } : {}), ...(entry !== undefined ? { path: entry } : {}) });
const inside = (root, target) => {
  const relative = path.relative(root, target);
  return relative === '' || (relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative));
};

// Reject values JSON would silently drop/coerce, including cycles and NaN.
function stable(value, ancestors = new Set(), depth = 0) {
  if (depth > 100) throw new Error('NON_JSON_CONFIGURATION');
  if (value === null || typeof value === 'string' || typeof value === 'boolean') return JSON.stringify(value);
  if (typeof value === 'number' && Number.isFinite(value)) return JSON.stringify(value);
  if (typeof value !== 'object' || ancestors.has(value)) throw new Error('NON_JSON_CONFIGURATION');
  if (!Array.isArray(value) && ![Object.prototype, null].includes(Object.getPrototypeOf(value))) throw new Error('NON_JSON_CONFIGURATION');
  if (Object.getOwnPropertySymbols(value).length) throw new Error('NON_JSON_CONFIGURATION');
  ancestors.add(value);
  let result;
  if (Array.isArray(value)) {
    if (Object.keys(value).length !== value.length) throw new Error('NON_JSON_CONFIGURATION');
    result = `[${Array.from(value, item => stable(item, ancestors, depth + 1)).join(',')}]`;
  } else {
    result = `{${Object.keys(value).sort(compare).map(key => `${JSON.stringify(key)}:${stable(value[key], ancestors, depth + 1)}`).join(',')}}`;
  }
  ancestors.delete(value);
  return result;
}

function executionDescription(adapter) {
  return Object.fromEntries(Object.entries(adapter).filter(([key]) => !['id', 'title', 'cacheIdentity'].includes(key)));
}
function definition(adapter) {
  return Object.fromEntries(Object.entries(adapter).filter(([key]) => key !== 'cacheIdentity'));
}
function shaFile(file) {
  const hash = crypto.createHash('sha256');
  const fd = fs.openSync(file, 'r');
  try {
    if (!fs.fstatSync(fd).isFile()) throw new Error('NOT_A_FILE');
    const buffer = Buffer.allocUnsafe(1024 * 1024);
    let bytes;
    while ((bytes = fs.readSync(fd, buffer, 0, buffer.length, null)) > 0) hash.update(buffer.subarray(0, bytes));
    return hash.digest('hex');
  } finally { fs.closeSync(fd); }
}

function snapshot(entry, { role, root, relativeOnly = false } = {}) {
  const pin = typeof entry === 'string' ? { path: entry } : entry;
  if (!pin || typeof pin.path !== 'string' || !pin.path || /[\0\r\n]/.test(pin.path)) throw new Error('INVALID_INPUT_PATH');
  if (pin.status !== undefined && pin.status !== 'OK') throw new Error('INPUT_NOT_RESOLVED');
  if (pin.sha256 !== undefined && !SHA.test(pin.sha256)) throw new Error('INVALID_INPUT_SHA256');
  if (relativeOnly && (path.isAbsolute(pin.path) || pin.path.split(/[\\/]/).includes('..'))) throw new Error('INPUT_OUTSIDE_ROOT');
  if (!root && !path.isAbsolute(pin.path)) throw new Error('ABSOLUTE_PATH_REQUIRED');
  const absolute = path.resolve(root ?? path.dirname(pin.path), pin.path);
  const boundary = root ?? path.dirname(absolute);
  if (!inside(boundary, absolute)) throw new Error('INPUT_OUTSIDE_ROOT');
  const actual = fs.realpathSync(absolute);
  if (!inside(boundary, actual)) throw new Error('SYMLINK_OUTSIDE_ROOT');
  const sha256 = shaFile(actual);
  if (pin.sha256 !== undefined && sha256 !== pin.sha256.toLowerCase()) throw new Error('INPUT_SHA256_MISMATCH');
  return { role, path: absolute, realPath: actual, sha256 };
}

/** See README above: complete declarations are mandatory, not automatically inferred. */
export function fingerprintCase({ repoRoot, adapter, toolIdentities = [], options = {} } = {}) {
  const reasons = [], inputs = [], tools = [];
  let root = null, description = null, normalizedOptions = null;
  try { root = fs.realpathSync(repoRoot); if (!fs.statSync(root).isDirectory()) throw new Error(); }
  catch { reasons.push(problem('REPOSITORY_UNAVAILABLE', 'repoRoot')); }
  try {
    if (!adapter || typeof adapter !== 'object' || Array.isArray(adapter)) throw new Error();
    description = JSON.parse(stable(executionDescription(adapter)));
    normalizedOptions = JSON.parse(stable(options));
    if (typeof adapter.path !== 'string' || !['node', 'typescript', 'powershell'].includes(adapter.kind) ||
        typeof adapter.scope !== 'string' || !Number.isInteger(adapter.timeoutMs) || adapter.timeoutMs < 100 ||
        !Array.isArray(adapter.args) || adapter.args.some(arg => typeof arg !== 'string' || /[\0\r\n]/.test(arg))) {
      reasons.push(problem('INVALID_ADAPTER', 'adapter'));
    }
    if (adapter.blockedReason) reasons.push(problem('ADAPTER_BLOCKED', 'adapter.blockedReason'));
    if (adapter.resultFiles !== undefined && (!Array.isArray(adapter.resultFiles) || adapter.resultFiles.some(file =>
      typeof file !== 'string' || !file || path.isAbsolute(file) || file.split(/[\\/]/).includes('..') || /[\0\r\n]/.test(file)))) {
      reasons.push(problem('INVALID_RESULT_FILES', 'adapter.resultFiles'));
    }
  } catch { reasons.push(problem('NON_JSON_CONFIGURATION', 'adapter/options')); }
  if (options?.closureComplete !== true) reasons.push(problem('INPUT_CLOSURE_UNPROVEN', 'options.closureComplete'));
  if (options?.toolClosureComplete !== true) reasons.push(problem('TOOL_CLOSURE_UNPROVEN', 'options.toolClosureComplete'));
  if (!Array.isArray(options?.resolvedInputs)) reasons.push(problem('RESOLVED_INPUTS_REQUIRED', 'options.resolvedInputs'));
  if (!Array.isArray(toolIdentities)) reasons.push(problem('INVALID_TOOL_IDENTITIES', 'toolIdentities'));
  else if (adapter?.kind !== 'node' && toolIdentities.length === 0) reasons.push(problem('EXPLICIT_TOOL_REQUIRED', 'toolIdentities'));
  const add = (entry, field, role, external = false, relativeOnly = false) => {
    if (!external && !root) return;
    try { (external ? tools : inputs).push(snapshot(entry, { role, ...(external ? {} : { root }), relativeOnly })); }
    catch (error) { reasons.push(problem(error.code ?? error.message, field, typeof entry === 'string' ? entry : entry?.path)); }
  };
  if (adapter) {
    add(adapter.path, 'adapter.path', 'source', false, true);
    for (const [field, role, required] of [['evidenceInputs', 'evidence-input', true], ['referenceInputs', 'reference-input', false], ['dependencyInputs', 'dependency-input', false]]) {
      if (adapter[field] === undefined && !required) continue;
      if (!Array.isArray(adapter[field])) { reasons.push(problem('INVALID_INPUT_LIST', `adapter.${field}`)); continue; }
      for (const entry of adapter[field]) add(entry, `adapter.${field}`, role, false, field !== 'dependencyInputs');
    }
    if (adapter.artifactInputs !== undefined) {
      if (!Array.isArray(adapter.artifactInputs)) reasons.push(problem('INVALID_INPUT_LIST', 'adapter.artifactInputs'));
      else for (const entry of adapter.artifactInputs) {
        try { inputs.push(snapshot(entry, { role: 'artifact' })); }
        catch (error) { reasons.push(problem(error.code ?? error.message, 'adapter.artifactInputs', typeof entry === 'string' ? entry : entry?.path)); }
      }
    }
  }
  if (Array.isArray(options?.resolvedInputs)) for (const entry of options.resolvedInputs) add(entry, 'options.resolvedInputs', 'resolved-input');
  add(process.execPath, 'runtime.node', 'node', true);
  add(SELF, 'runtime.engine', 'cache-engine', true);
  if (Array.isArray(toolIdentities)) for (const entry of toolIdentities) add(entry, 'toolIdentities', 'tool', true);
  const unique = rows => [...new Map(rows.map(row => [stable(row), row])).values()].sort((a, b) => compare(stable(a), stable(b)));
  const manifest = { schemaVersion: SCHEMA_VERSION, repoRoot: root, adapter: description, options: normalizedOptions,
    runtime: { node: process.version, platform: process.platform, arch: process.arch }, inputs: unique(inputs), tools: unique(tools) };
  return { schemaVersion: SCHEMA_VERSION, identity: reasons.length ? null : digest(stable(manifest)), cacheable: reasons.length === 0, reasons, manifest };
}

function identityProblems(identity, adapter) {
  const reasons = [];
  try {
    if (identity?.schemaVersion !== SCHEMA_VERSION || identity.cacheable !== true || !Array.isArray(identity.reasons) || identity.reasons.length ||
        !SHA.test(identity.identity) || digest(stable(identity.manifest)) !== identity.identity) throw new Error('INVALID_IDENTITY');
    const m = identity.manifest;
    if (m.schemaVersion !== SCHEMA_VERSION || m.options?.closureComplete !== true || m.options?.toolClosureComplete !== true ||
        !Array.isArray(m.options.resolvedInputs) || !Array.isArray(m.inputs) || !Array.isArray(m.tools) ||
        stable(m.runtime) !== stable({ node: process.version, platform: process.platform, arch: process.arch })) throw new Error('INCOMPLETE_IDENTITY');
    if (fs.realpathSync(m.repoRoot) !== m.repoRoot) throw new Error('REPOSITORY_CHANGED');
    if (adapter && stable(executionDescription(adapter)) !== stable(m.adapter)) throw new Error('ADAPTER_IDENTITY_MISMATCH');
    for (const [role, file] of [['node', process.execPath], ['cache-engine', SELF]]) {
      if (!m.tools.some(row => row.role === role && row.path === file)) throw new Error('RUNTIME_IDENTITY_MISSING');
    }
    for (const row of [...m.inputs, ...m.tools]) {
      const root = ['artifact', 'node', 'cache-engine', 'tool'].includes(row.role) ? undefined : m.repoRoot;
      const current = snapshot(row, { role: row.role, root });
      if (stable(current) !== stable(row)) throw new Error('IDENTITY_FILE_CHANGED');
    }
  } catch (error) { reasons.push(problem(error.code ?? error.message, 'identity')); }
  return reasons;
}

/** A cached reference is never a PASS performed by this invocation. */
export function inspectCachedResult({ identity, cacheEntry } = {}) {
  const outcome = { hit: false, reusable: false, status: 'CACHE_MISS', previousStatus: null,
    executedThisRun: false, actualPassedThisRun: false, reasons: [], reference: null };
  if (identity?.cacheable !== true) return { ...outcome, status: 'UNCACHEABLE', reasons: identity?.reasons ?? [problem('INPUT_CLOSURE_UNPROVEN')] };
  outcome.reasons.push(...identityProblems(identity));
  if (outcome.reasons.length) return outcome;
  if (!cacheEntry) return { ...outcome, reasons: [problem('CACHE_ENTRY_MISSING')] };
  if (cacheEntry.identity !== identity.identity) return { ...outcome, reasons: [problem('IDENTITY_MISMATCH')] };
  try {
    if (!cacheEntry.result || !SHA.test(cacheEntry.result.sha256 ?? '')) throw new Error('RESULT_PIN_REQUIRED');
    if (!Array.isArray(cacheEntry.evidencePins) || cacheEntry.evidencePins.length === 0) throw new Error('EVIDENCE_PINS_REQUIRED');
    const pins = new Map();
    for (const pin of cacheEntry.evidencePins) {
      if (!pin || !SHA.test(pin.sha256 ?? '')) throw new Error('INVALID_EVIDENCE_PIN');
      const row = snapshot(pin, { role: 'receipt' });
      if (pins.has(row.path) && pins.get(row.path).sha256 !== row.sha256) throw new Error('CONFLICTING_EVIDENCE_PIN');
      pins.set(row.path, row);
    }
    const resultPin = snapshot(cacheEntry.result, { role: 'result' });
    const requirePin = (file, expected) => {
      if (typeof file !== 'string' || !path.isAbsolute(file)) throw new Error('ABSOLUTE_EVIDENCE_PATH_REQUIRED');
      const pin = pins.get(path.resolve(file));
      if (!pin || (expected !== undefined && (!SHA.test(expected) || pin.sha256 !== expected.toLowerCase()))) throw new Error('REQUIRED_EVIDENCE_PIN_MISSING_OR_MISMATCHED');
    };
    requirePin(resultPin.path, resultPin.sha256);
    const caseDir = path.dirname(resultPin.path);
    for (const file of ['stdout.log', 'stderr.log', ...(identity.manifest.adapter.resultFiles ?? [])]) {
      const absolute = path.resolve(caseDir, file);
      if (!inside(caseDir, absolute)) throw new Error('RESULT_FILE_OUTSIDE_CASE');
      requirePin(absolute);
    }
    const bytes = fs.readFileSync(resultPin.realPath);
    if (bytes.length > 16 * 1024 * 1024) throw new Error('RESULT_SIZE_LIMIT');
    if (digest(bytes) !== resultPin.sha256) throw new Error('RESULT_CHANGED_DURING_READ');
    const raw = JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, ''));
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) throw new Error('INVALID_RAW_RESULT');
    if (raw.status !== 'PASS' && !FAILURE_STATUSES.has(raw.status)) throw new Error('UNSUPPORTED_PREVIOUS_STATUS');
    if (raw.source !== undefined && raw.source !== identity.manifest.adapter.path) throw new Error('RESULT_SOURCE_MISMATCH');
    if (raw.scope !== undefined && raw.scope !== identity.manifest.adapter.scope) throw new Error('RESULT_SCOPE_MISMATCH');
    for (const field of ['structuredEvidence', 'bundledOutputs']) {
      if (raw[field] !== undefined && !Array.isArray(raw[field])) throw new Error('INVALID_RESULT_EVIDENCE');
      for (const row of raw[field] ?? []) requirePin(row?.path, row?.sha256 ?? '');
    }
    if (raw.bundle !== undefined) requirePin(raw.bundle?.path, raw.bundle?.sha256 ?? '');
    const declared = new Map([...identity.manifest.inputs, ...identity.manifest.tools].map(row => [row.path, row]));
    for (const field of ['sourceInputs', 'referenceInputs', 'dependencyInputs', 'toolDependencyInputs', 'candidateArtifacts']) {
      if (raw[field] !== undefined && !Array.isArray(raw[field])) throw new Error('INVALID_RESULT_INPUTS');
      for (const row of raw[field] ?? []) {
        const absolute = typeof row?.path === 'string' ? path.resolve(identity.manifest.repoRoot, row.path) : '';
        const expected = declared.get(absolute) ?? pins.get(absolute);
        if (!expected || !SHA.test(row?.sha256 ?? '') || expected.sha256 !== row.sha256.toLowerCase() ||
            (row.status !== undefined && row.status !== 'OK')) throw new Error('UNDECLARED_OR_CHANGED_RESULT_INPUT');
      }
    }
    if (raw.status === 'PASS') {
      const noCandidates = !(identity.manifest.adapter.artifactInputs?.length) && raw.candidateArtifacts === undefined && raw.bundle === undefined && raw.bundledOutputs === undefined;
      const candidateUnchanged = raw.candidateArtifactChanged === false || (raw.candidateArtifactChanged === undefined && noCandidates);
      if (raw.sourceChanged !== false || !candidateUnchanged) throw new Error('RESULT_CHANGED_FLAGS_NOT_FALSE');
      if (raw.exitCode !== 0 || raw.signal || raw.launchError || raw.timedOut || raw.outputTruncated || raw.missingInputs || raw.buildError ||
          raw.requiredEvidenceMissing || raw.requiredEvidenceFailed || raw.fakeGreen || raw.referenceInputChanges?.length ||
          raw.structuredEvidence?.some(row => row.failed) || (raw.completionProof !== undefined && raw.completionProof?.status !== 'PASS') ||
          (identity.manifest.adapter.completionContract !== undefined && raw.completionProof?.status !== 'PASS')) throw new Error('CONTRADICTORY_PASS_RESULT');
      const entry = path.resolve(identity.manifest.repoRoot, identity.manifest.adapter.path);
      if (!raw.sourceInputs?.some(row => path.resolve(identity.manifest.repoRoot, row.path) === entry)) throw new Error('RESULT_SOURCE_IDENTITY_MISSING');
    }
    outcome.previousStatus = raw.status;
    outcome.reference = raw;
    outcome.reusable = true;
    outcome.hit = raw.status === 'PASS';
    outcome.status = outcome.hit ? 'CACHED_REFERENCE' : 'UNCHANGED_FAILURE_REFERENCE';
  } catch (error) { outcome.reasons.push(problem(error.code ?? error.message, 'cacheEntry')); }
  return outcome;
}

/** Remove only identical definitions or proven, current, identical execution manifests. */
export function deduplicateCases(adapters) {
  if (!Array.isArray(adapters)) throw new TypeError('ADAPTER_ARRAY_REQUIRED');
  const canonical = [], aliases = [], deduplicated = [], byId = new Map(), byExecution = new Map();
  for (const adapter of adapters) {
    if (!adapter || typeof adapter.id !== 'string' || !adapter.id) throw new TypeError('ADAPTER_ID_REQUIRED');
    const serialized = stable(definition(adapter));
    const prior = byId.get(adapter.id);
    if (prior && prior.definition !== serialized) {
      const error = new Error(`ADAPTER_ID_CONFLICT:${adapter.id}`);
      error.code = 'ADAPTER_ID_CONFLICT';
      throw error;
    }
    let retained = prior?.canonical, reason = 'IDENTICAL_ID_DEFINITION';
    const identity = adapter.cacheIdentity;
    let execution;
    if (!retained && identity?.cacheable === true && identityProblems(identity, adapter).length === 0) {
      execution = stable(identity.manifest);
      retained = byExecution.get(execution);
      reason = 'IDENTICAL_EXECUTION_IDENTITY';
    }
    if (retained) {
      aliases.push({ id: adapter.id, canonicalId: retained.id });
      deduplicated.push({ id: adapter.id, canonicalId: retained.id, reason });
    } else {
      retained = adapter;
      canonical.push(adapter);
      if (execution) byExecution.set(execution, adapter);
    }
    byId.set(adapter.id, { definition: serialized, canonical: retained });
  }
  return { canonical, aliases, deduplicated };
}