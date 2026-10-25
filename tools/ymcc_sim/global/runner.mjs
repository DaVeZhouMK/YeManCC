import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,verifyBundledImplementation,classifyBundledInput,bundledArtifacts,verifyBundledNodeReceipt,verifyBundledToolScope} from './bundled-node-tool-authority.mjs';
import {normalizeCompletionContract,verifyExecutionCompletion} from './execution-completion/index.mjs';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';

export const SCHEMA_VERSION = 1;
const MAX_OUTPUT = 2 * 1024 * 1024;
// The reviewed InputHost compiler/runtime closure has 572 members; keep a finite bound.
export const MAX_ARTIFACT_INPUTS = 3072;
const KINDS = new Set(['node', 'typescript', 'powershell']);
const SCOPES = new Set(['behavioral-mock', 'source-contract', 'host-mock']);
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
export const sha256File = file => hash(fs.readFileSync(file));
const slash = value => value.replaceAll('\\', '/');

/** Resolve reviewed entry points only inside the checkout, including symlink targets. */
export function sourcePath(repoRoot, relative) {
  if (typeof relative !== 'string' || !relative || path.isAbsolute(relative) || relative.split(/[\\/]/).includes('..')) throw new Error('SOURCE_PATH_OUTSIDE_REPOSITORY');
  const root = fs.realpathSync(repoRoot);
  const target = fs.realpathSync(path.resolve(root, relative));
  const rel = path.relative(root, target);
  if (rel === '..' || rel.startsWith(`..${path.sep}`) || path.isAbsolute(rel)) throw new Error('SOURCE_PATH_OUTSIDE_REPOSITORY');
  return target;
}
export function validateAdapters(adapters, repoRoot) {
  const ids = new Set();
  for (const item of adapters) {
    if (!/^[a-z0-9][a-z0-9.-]{0,99}$/.test(item.id) || ids.has(item.id)) throw new Error('ADAPTER_ID_INVALID_OR_DUPLICATE');
    ids.add(item.id);
    if(item.completionContract!==undefined)normalizeCompletionContract(item.completionContract);
    if(item.blockedExitCodes!==undefined && (!Array.isArray(item.blockedExitCodes)||item.blockedExitCodes.length>8||new Set(item.blockedExitCodes).size!==item.blockedExitCodes.length||item.blockedExitCodes.some(code=>!Number.isInteger(code)||code<1||code>255)))throw new Error(`ADAPTER_BLOCKED_EXITS_INVALID:${item.id}`);
    if(item.artifactInputs!==undefined && (!Array.isArray(item.artifactInputs)||item.artifactInputs.length>MAX_ARTIFACT_INPUTS||item.artifactInputs.some(value=>typeof value!=='string'||!path.isAbsolute(value)||/[\0\r\n]/.test(value))))throw new Error(`ADAPTER_ARTIFACT_INPUTS_INVALID:${item.id}`);
    if(item.referenceInputs!==undefined && (!Array.isArray(item.referenceInputs)||item.referenceInputs.some(value=>typeof value!=='string')))throw new Error(`ADAPTER_REFERENCE_INPUTS_INVALID:${item.id}`);
    if (!KINDS.has(item.kind) || !SCOPES.has(item.scope) || !Array.isArray(item.profiles) || !Array.isArray(item.evidenceInputs)) throw new Error(`ADAPTER_SCHEMA_INVALID:${item.id}`);
    if (!Number.isInteger(item.timeoutMs) || item.timeoutMs < 100 || item.timeoutMs > 3600000) throw new Error(`ADAPTER_TIMEOUT_INVALID:${item.id}`);
    if (!Array.isArray(item.args) || item.args.some(arg => typeof arg !== 'string' || /[\0\r\n]/.test(arg))) throw new Error(`ADAPTER_ARGUMENT_INVALID:${item.id}`);
    if (item.kind === 'powershell' && item.args.some(arg => /real-backend|allow-hardware|SetSuspendState|ExecutionPolicy|\bBypass\b|^-Attach$|^-StartApp$|^-WithApp$/i.test(arg))) throw new Error(`ADAPTER_UNSAFE_ARGUMENT:${item.id}`);
    // Missing source is represented later as BLOCKED, never dropped from the plan.
    if (fs.existsSync(path.resolve(repoRoot, item.path))) sourcePath(repoRoot, item.path);
  }
  return adapters;
}
export function fingerprintInputs(repoRoot, entries) {
  return [...new Set(entries)].sort().map(entry => {
    try {
      const file = sourcePath(repoRoot, entry);
      if (!fs.statSync(file).isFile()) return { path: slash(entry), sha256: null, status: 'NOT_A_FILE' };
      return { path: slash(entry), sha256: sha256File(file), status: 'OK' };
    } catch { return { path: slash(entry), sha256: null, status: 'MISSING_OR_OUTSIDE_REPOSITORY' }; }
  });
}
export function fingerprintArtifacts(entries) {
  return [...new Set(entries)].sort().map(file=>{
    try {if(!path.isAbsolute(file)||!fs.statSync(file).isFile())throw Error();return {path:file,sha256:sha256File(file),status:'OK'};}
    catch{return {path:file,sha256:null,status:'MISSING_OR_NOT_A_FILE'};}
  });
}
/** A process result is not a hardware result; missing/timeout/stale evidence never passes. */
export function evaluateResult(result) {
  if (result.timedOut) return 'TIMEOUT';
  if (result.launchError || result.exitCode === null || result.exitCode === undefined || result.signal) return 'FAIL';
  if (result.exitCode !== 0) return 'FAIL';
  if (result.outputTruncated || result.sourceChanged || result.candidateArtifactChanged || result.missingInputs || result.buildError) return 'FAIL';
  if (result.requiredEvidenceMissing || result.requiredEvidenceFailed) return 'FAIL';
  if (result.completionProof!==undefined && result.completionProof?.status!=='PASS') return 'FAIL';
  return 'PASS';
}
export function verdictFor({ results, selectedCount, uncoveredCount = 0, requireAll = false }) {
  if (results.some(result => ['FAIL', 'TIMEOUT'].includes(result.status))) return { status: 'FAIL', exitCode: 2 };
  if (selectedCount === 0 || results.length !== selectedCount || results.some(result => result.status !== 'PASS')) return { status: 'BLOCKED', exitCode: 10 };
  if (requireAll && uncoveredCount > 0) return { status: 'INCOMPLETE_COVERAGE', exitCode: 10 };
  return { status: 'PASS_SELECTED_SCOPE', exitCode: 0 };
}

export function runProcess(executable, args, { cwd, env, timeoutMs, outputLimit = MAX_OUTPUT } = {}) {
  return new Promise(resolve => {
    const started = Date.now();
    let stdout = '', stderr = '', outputBytes = 0, truncated = false, timedOut = false, launchError = null;
    let child;
    try { child = spawn(executable, args, { cwd, env, shell: false, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] }); }
    catch (error) { resolve({ exitCode: null, launchError: error.code ?? 'SPAWN_FAILED', durationMs: Date.now() - started, stdout, stderr }); return; }
    const append = (kind, data) => {
      const remaining = Math.max(0, outputLimit - outputBytes);
      outputBytes += data.length;
      if (remaining < data.length) truncated = true;
      const text = data.subarray(0, remaining).toString('utf8');
      if (kind === 'stdout') stdout += text; else stderr += text;
    };
    child.stdout.on('data', data => append('stdout', data));
    child.stderr.on('data', data => append('stderr', data));
    const timer = setTimeout(() => { timedOut = true; child.kill(); }, timeoutMs);
    child.once('error', error => { launchError = error.code ?? 'SPAWN_FAILED'; });
    child.once('close', (exitCode, signal) => {
      clearTimeout(timer);
      resolve({ pid: child.pid ?? null, exitCode, signal, timedOut, launchError, outputTruncated: truncated, durationMs: Date.now() - started, stdout, stderr });
    });
  });
}

function cleanEnvironment(env, caseDir, payloadRoot) {
  const result = { ...env, YMCC_SIM_GLOBAL_RUN: '1', YMCC_SIM_EVIDENCE_DIR: caseDir };
  // No legacy fixture switch or payload override may silently change this run.
  for (const key of Object.keys(result)) if (/^YMCC_AC\d|^YEMAN_SIM_PAYLOAD_ROOT$|^YEMAN_SIM_/i.test(key)) delete result[key];
  if (payloadRoot) result.YEMAN_SIM_PAYLOAD_ROOT = payloadRoot;
  return result;
}

/** Product hashes stay repository-relative. Run-local fixture sources may
 * use absolute paths, but both their lexical and resolved targets must remain
 * inside this exact owned case directory. */
function receiptSourcePath(repoRoot, caseDir, entry) {
  if(typeof entry!=='string')throw Error('RECEIPT_SOURCE_INVALID');
  if(!path.isAbsolute(entry))return sourcePath(repoRoot,entry);
  const inside=(root,target)=>{const rel=path.relative(root,target);return rel===''||(!rel.startsWith(`..${path.sep}`)&&rel!=='..'&&!path.isAbsolute(rel));};
  if(!inside(path.resolve(caseDir),path.resolve(entry)))throw Error('RECEIPT_FIXTURE_OUTSIDE_CASE');
  const root=fs.realpathSync(caseDir),actual=fs.realpathSync(entry);
  if(!inside(root,actual))throw Error('RECEIPT_FIXTURE_ESCAPED_CASE');
  return actual;
}
export function checkStructuredEvidence(caseDir, adapter, repoRoot) {
  if (!adapter.resultFiles?.length) return { paths: [], missing: false, failed: false };
  const paths = [], problems = [];
  for (const relative of adapter.resultFiles) {
    const file = path.join(caseDir, relative);
    try {
      if(fs.statSync(file).size>16*1024*1024)throw new Error('RECEIPT_SIZE_LIMIT');
      const value = JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
      const cases = Array.isArray(value.cases) ? value.cases : null;
      const caseSuccess = item => item && typeof item === 'object' && (item.passed === true || item.pass === true || item.ok === true);
      const affirmative = value.ok === true || value.pass === true || value.passed === true || value.allPassed === true ||
        (/^(PASS|ALL_PASS)$/.test(value.verdict ?? '') || /^(PASS|ALL_PASS)$/.test(value.status ?? '')) || (cases?.length > 0 && cases.every(caseSuccess));
      const failed = !affirmative || value.ok === false || value.pass === false || value.passed === false || value.allPassed === false ||
        value.exitCode > 0 || value.failedCount > 0 || [value.verdict,value.status].some(status => /^(FAIL|CHECK_FAIL|BLOCKED|TIMEOUT|INCOMPLETE|NOT_RUN|NOT_COVERED)(?:_|$)/.test(status ?? '')) ||
        value.fakeGreen === true || (cases !== null && cases.length === 0) || (cases?.some(item => item && typeof item === 'object' && (item.passed === false || item.pass === false || item.ok === false || /^(FAIL|BLOCKED|TIMEOUT|INCOMPLETE|NOT_RUN|NOT_COVERED)(?:_|$)/.test(item.status ?? ''))) ?? false);
      const reported = value.sourceHashes;
      const sourceRows = Array.isArray(reported) ? reported : reported && typeof reported === 'object'
        ? Object.entries(reported).map(([path,sha256])=>({path,sha256})) : [];
      const sourceMismatches=[];
      for(const source of sourceRows) {
        try {
          if(!source || !/^[a-f0-9]{64}$/i.test(source.sha256 ?? '') || sha256File(receiptSourcePath(repoRoot,caseDir,source.path))!==source.sha256.toLowerCase())
            sourceMismatches.push(source?.path ?? 'INVALID_SOURCE_RECEIPT');
        } catch { sourceMismatches.push(source?.path ?? 'SOURCE_OUTSIDE_REPOSITORY_OR_MISSING'); }
      }
      paths.push({ path: file, sha256: sha256File(file), failed:failed || sourceMismatches.length>0,
        reportedSourceCount:sourceRows.length, sourceMismatches });
    } catch { problems.push(relative); }
  }
  return { paths, missing: problems.length > 0, missingPaths: problems, failed: paths.some(item => item.failed) };
}
export async function executeAdapter(adapter, { repoRoot, runDir, powershell = 'pwsh.exe', payloadRoot, env = process.env } = {}) {
  const caseDir = path.join(runDir, 'cases', adapter.id);
  fs.mkdirSync(caseDir, { recursive: true });
  const result = {
    id: adapter.id, domain: adapter.domain, title: adapter.title, scope: adapter.scope, status: 'BLOCKED',
    startedAt: new Date().toISOString(), source: adapter.path, evidenceDir: caseDir,
    claims: { physicalHardwareValidated: false, operatingSystemSleepPerformed: false, installedApplicationExercised: false },
  };
  if (adapter.blockedReason) {
    result.reason = adapter.blockedReason;
    fs.writeFileSync(path.join(caseDir, 'result.json'), JSON.stringify(result, null, 2));
    return result;
  }
  const inputs = fingerprintInputs(repoRoot, [adapter.path, ...adapter.evidenceInputs, ...(adapter.kind === 'typescript' ? BUNDLED_AUTHORITY_SOURCES : [])]);
  result.sourceInputs = inputs;
  if(adapter.referenceInputs?.length) {
    result.referenceInputs=fingerprintInputs(repoRoot,adapter.referenceInputs);
    result.referenceClosure='UNVERIFIED_NOT_EXECUTED_BY_THIS_TEST';
    result.claims.referenceSourcesBehaviorVerified=false;
  }
  if (inputs.some(item => item.status !== 'OK')) {
    result.reason = 'REQUIRED_INPUT_MISSING'; result.missingInputs = true;
    fs.writeFileSync(path.join(caseDir, 'result.json'), JSON.stringify(result, null, 2));
    return result;
  }
  if(adapter.artifactInputs?.length){
    result.candidateArtifacts=fingerprintArtifacts(adapter.artifactInputs);
    if(result.candidateArtifacts.some(row=>row.status!=='OK')){
      result.reason='EXPLICIT_CANDIDATE_ARTIFACT_MISSING';result.missingInputs=true;
      fs.writeFileSync(path.join(caseDir,'result.json'),JSON.stringify(result,null,2));return result;
    }
  }
  if (adapter.kind === 'powershell' && adapter.scope === 'host-mock' && !payloadRoot) {
    result.reason = 'EXPLICIT_CURRENT_HOST_PAYLOAD_REQUIRED';
    fs.writeFileSync(path.join(caseDir, 'result.json'), JSON.stringify(result, null, 2));
    return result;
  }
  try {
    const entry = sourcePath(repoRoot, adapter.path);
    let executable = process.execPath, args = [...adapter.args];
    if (adapter.kind === 'typescript') {
      result.compilerKind = 'typescript-esbuild-bundle';
      verifyBundledImplementation(repoRoot);
      result.bundledToolAuthority = lockBundledNodeTools(repoRoot, {env, limit:MAX_ARTIFACT_INPUTS});
      result.explicitAdapterArtifacts = result.candidateArtifacts ?? [];
      result.candidateArtifacts = bundledArtifacts(result.bundledToolAuthority, result.explicitAdapterArtifacts, [], MAX_ARTIFACT_INPUTS);
      const require = createRequire(path.join(repoRoot, 'package.json'));
      const esbuild = require('esbuild');
      const outfile = path.join(caseDir, 'entry.mjs');
      const loadedInputs = new Map(), loadedTools = new Map();
      await esbuild.build({ entryPoints: [entry], outfile, bundle: true, platform: 'node', format: 'esm', target: 'node18', logLevel: 'silent', sourcemap: 'external',
        plugins: [{ name: 'record-exact-source-inputs', setup(build) {
          build.onLoad({ filter: /\.(?:[cm]?js|jsx|tsx?|json)$/ }, async ({ path: inputPath }) => {
            const contents = fs.readFileSync(inputPath);
            const observed = classifyBundledInput(repoRoot, inputPath, contents, result.bundledToolAuthority);
            (observed.role === 'tool' ? loadedTools : loadedInputs).set(observed.row.path, observed.row);
            const extension = path.extname(inputPath).slice(1);
            return { contents: contents.toString('utf8'), resolveDir: path.dirname(inputPath), loader: ['ts','tsx','jsx','json'].includes(extension) ? extension : 'js' };
          });
        } }],
      });
      result.dependencyInputs = [...loadedInputs.values()].sort((a,b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
      result.toolDependencyInputs = [...loadedTools.values()].sort((a,b) => a.path.localeCompare(b.path));
      result.bundle = { path: outfile, sha256: sha256File(outfile), esbuildVersion: esbuild.version };
      result.bundledOutputs = [outfile, outfile + '.map'].map(file => ({path:file, sha256:sha256File(file), status:'OK'}));
      result.candidateArtifacts = bundledArtifacts(result.bundledToolAuthority, result.explicitAdapterArtifacts, result.bundledOutputs, MAX_ARTIFACT_INPUTS);
      args.unshift(outfile);
    } else if (adapter.kind === 'node') args.unshift(entry);
    else {
      executable = powershell;
      const helper = sourcePath(repoRoot, 'tools/ymcc_sim/global/host-process.ps1');
      const argumentsPath = path.join(caseDir, 'arguments.json');
      fs.writeFileSync(argumentsPath, JSON.stringify(args.map(arg => arg.replaceAll('{outDir}', caseDir))));
      result.processGuard = { strategy: 'windows-job-kill-on-close-gated-child', helper, sha256: sha256File(helper) };
      args = ['-NoLogo', '-NoProfile', '-NonInteractive', '-File', helper, '-ScriptPath', entry, '-ArgumentsPath', argumentsPath];
    }
    args = args.map(arg => arg.replaceAll('{outDir}', caseDir));
    const processResult = await runProcess(executable, args, { cwd: repoRoot, env: cleanEnvironment(env, caseDir, payloadRoot), timeoutMs: adapter.timeoutMs });
    const { stdout, stderr, ...safeResult } = processResult;
    Object.assign(result, safeResult);
    fs.writeFileSync(path.join(caseDir, 'stdout.log'), stdout);
    fs.writeFileSync(path.join(caseDir, 'stderr.log'), stderr);
    if(result.referenceInputs) {
      result.referenceInputsAfter=fingerprintInputs(repoRoot,result.referenceInputs.map(item=>item.path));
      result.referenceInputChanges=result.referenceInputs.filter((item,index)=>JSON.stringify(item)!==JSON.stringify(result.referenceInputsAfter[index])).map(item=>item.path);
    }
    const after = fingerprintInputs(repoRoot, inputs.map(item => item.path));
    result.sourceChanged = JSON.stringify(inputs) !== JSON.stringify(after);
    if (result.dependencyInputs) result.sourceChanged ||= JSON.stringify(result.dependencyInputs) !== JSON.stringify(fingerprintInputs(repoRoot, result.dependencyInputs.map(item => item.path)));
    if(result.candidateArtifacts)result.candidateArtifactChanged=JSON.stringify(result.candidateArtifacts)!==JSON.stringify(fingerprintArtifacts(result.candidateArtifacts.map(row=>row.path)));
    if(result.bundledToolAuthority)result.bundledToolProof=verifyBundledNodeReceipt(result,repoRoot,caseDir,{limit:MAX_ARTIFACT_INPUTS});
    if(adapter.completionContract!==undefined)result.completionProof=verifyExecutionCompletion({...result,stdout,stderr},adapter.completionContract);
    const evidence = checkStructuredEvidence(caseDir, adapter, repoRoot);
    result.structuredEvidence = evidence.paths;
    result.requiredEvidenceMissing = evidence.missing;
    result.requiredEvidenceFailed = evidence.failed;
    const explicitBlockedExit = Array.isArray(adapter.blockedExitCodes) && adapter.blockedExitCodes.includes(result.exitCode) && !result.sourceChanged && !result.outputTruncated;
    result.status = !result.timedOut && !result.candidateArtifactChanged && ((adapter.kind === 'powershell' && result.exitCode === 10) || explicitBlockedExit) ? 'BLOCKED' : evaluateResult(result);
  } catch (error) {
    result.status = 'FAIL'; result.buildError = true;
    result.reason = String(error.message ?? error).slice(0, 1600);
  }
  if(result.bundledToolAuthority) {
    try {
      result.bundledToolAuthorityAfter=verifyBundledToolScope(result.bundledToolAuthority,repoRoot,{limit:MAX_ARTIFACT_INPUTS});
      result.candidateArtifactChanged ||= JSON.stringify(result.bundledToolAuthority)!==JSON.stringify(result.bundledToolAuthorityAfter);
      result.sourceChanged ||= JSON.stringify(inputs)!==JSON.stringify(fingerprintInputs(repoRoot,inputs.map(row=>row.path)));
      if(result.candidateArtifactChanged||result.sourceChanged)result.status='FAIL';
    } catch(error) { result.status='FAIL';result.candidateArtifactChanged=true;result.toolAuthorityError=String(error.message); }
  }
  result.finishedAt = new Date().toISOString();
  fs.writeFileSync(path.join(caseDir, 'result.json'), JSON.stringify(result, null, 2));
  return result;
}

export async function runAdapters(adapters, options = {}) {
  validateAdapters(adapters, options.repoRoot);
  const results = [];
  // Sequential execution avoids shared ports and legacy fixture-output races.
  for (const adapter of adapters) {
    options.onProgress?.({ id: adapter.id, phase: 'start' });
    const result = await executeAdapter(adapter, options);
    results.push(result);
    options.onProgress?.({ id: adapter.id, phase: 'complete', status: result.status, durationMs: result.durationMs ?? 0 });
    fs.writeFileSync(path.join(options.runDir, 'progress.json'), JSON.stringify({ completed: results.length, total: adapters.length, results }, null, 2));
  }
  return results;
}

/** Compare equivalent tests only; different profile/source/scope is not a performance regression proof. */
export function compareRuns(before, after) {
  if (!Array.isArray(before.results) || !Array.isArray(after.results) || before.results.length === 0 || after.results.length === 0) throw new Error('COMPARISON_RUN_EVIDENCE_REQUIRED');
  for (const run of [before, after]) {
    const ids = new Set();
    for (const item of run.results) {
      if (!item.id || ids.has(item.id) || !['PASS','FAIL','TIMEOUT','BLOCKED'].includes(item.status)) throw new Error('COMPARISON_RESULT_INVALID');
      ids.add(item.id);
    }
  }
  const byId = new Map((before.results ?? []).map(item => [item.id, item]));
  const differences = (after.results ?? []).map(current => {
    const previous = byId.get(current.id);
    byId.delete(current.id);
    const equivalent = Boolean(previous && previous.scope === current.scope && previous.source === current.source);
    return {
      id: current.id, previousStatus: previous?.status ?? 'NOT_RUN', currentStatus: current.status,
      comparableCase: equivalent, regressed: equivalent && previous.status === 'PASS' && current.status !== 'PASS',
      recovered: equivalent && previous.status !== 'PASS' && current.status === 'PASS',
      sourceChanged: Boolean(previous && JSON.stringify(previous.sourceInputs) !== JSON.stringify(current.sourceInputs)),
      previousEvidence: previous?.evidenceDir ?? null, currentEvidence: current.evidenceDir,
      // Timings are diagnostic, never CPU/power evidence.
      durationDeltaMs: equivalent && Number.isFinite(previous.durationMs) && Number.isFinite(current.durationMs) ? current.durationMs - previous.durationMs : null,
    };
  });
  for (const previous of byId.values()) differences.push({ id: previous.id, previousStatus: previous.status, currentStatus: 'NOT_RUN', comparableCase: false, regressed: false, missingFromCandidate: true, scopeGap: true });
  return { schemaVersion: 1, kind: 'test-result-diff', baselineRunId: before.runId ?? null, candidateRunId: after.runId ?? null, differences, regressions: differences.filter(item => item.regressed).length, scopeGaps: differences.filter(item => item.scopeGap).length, comparisonComplete: differences.every(item => item.comparableCase), performanceImprovementProven: false };
}

export function makeRunDirectory(parent) {
  fs.mkdirSync(parent, { recursive: true });
  const runId = `${new Date().toISOString().replace(/[:.]/g, '-')}-${crypto.randomUUID().slice(0, 8)}`;
  const runDir = path.join(parent, runId);
  fs.mkdirSync(runDir);
  return { runId, runDir };
}
export function defaultEvidenceRoot() {
  return path.join(process.env.LOCALAPPDATA ?? os.tmpdir(), 'YeManCC', 'sim-evidence', 'global');
}
/** A review queue is not a coverage receipt. Whole-file associations cannot close behavioral gaps. */
export function makeCoverageWorkItems(catalog, results = []) {
  const items = new Map();
  for (const surface of catalog.surfaces ?? []) {
    const key = `${surface.domain}:${surface.path}`;
    if (!items.has(key)) items.set(key, {
      id: `coverage-${hash(key).slice(0, 16)}`, domain: surface.domain, path: surface.path,
      firstLine: surface.line, surfaceCount: 0, uncovered: 0, dynamic: 0, sourceContractOnly: 0,
      adapterIds: new Set(), operations: new Set(), state: 'NEEDS_BEHAVIOR_REVIEW',
    });
    const item = items.get(key);
    item.surfaceCount++;
    if (surface.coverage === 'NOT_COVERED') item.uncovered++;
    if (surface.dynamic) item.dynamic++;
    if (surface.evidenceScopes?.length && surface.evidenceScopes.every(scope => scope === 'source-contract')) item.sourceContractOnly++;
    surface.adapterIds?.forEach(id => item.adapterIds.add(id));
    if (surface.operation && item.operations.size < 12) item.operations.add(surface.operation);
  }
  const list = [...items.values()].map(item => ({ ...item,
    adapterIds: [...item.adapterIds].sort(), operations: [...item.operations],
    failedAdapterIds: results.filter(result => item.adapterIds.has(result.id) && result.status !== 'PASS').map(result => result.id),
    reviewPriority: item.domain === 'unclassified' ? 1 : item.dynamic ? 2 : item.uncovered ? 3 : item.sourceContractOnly ? 4 : 5,
    nextAction: item.domain === 'unclassified' ? 'Classify actual capability before claiming coverage.' : item.uncovered ? 'Review existing tests, bind or implement real behavior assertions for the named source.' : item.sourceContractOnly ? 'Add runtime behavior checks; static wiring cannot establish behavior.' : 'Review which operations and branches the associated passing tests actually exercise.',
    nativeBoundaryReviewRequired: /^(capabilities|native|InputHost|FanLab)\//.test(item.path),
  }));
  return { schemaVersion: 1, kind: 'implementation-review-queue-not-test-results',
    fullBehaviorAcceptanceProven: false,
    items: list.sort((a,b) => (a.failedAdapterIds.length ? 0 : a.reviewPriority) - (b.failedAdapterIds.length ? 0 : b.reviewPriority) || b.uncovered - a.uncovered || a.path.localeCompare(b.path)),
  };
}