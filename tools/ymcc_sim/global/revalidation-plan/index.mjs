/** Planning only. All reusable receipts come from the existing live audit.
 * No tests, subprocesses, product code, or candidate binaries execute here.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { auditReceipts, auditRegisteredScope, RECEIPT_AUDIT_LIMITS } from '../receipt-audit.mjs';
import { fingerprintInputs, fingerprintArtifacts, sha256File, validateAdapters } from '../runner.mjs';

const digest = value => crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex');
const distinct = values => [...new Set(values)].sort();
const same = (left, right) => JSON.stringify(left) === JSON.stringify(right);
const receiptBudget = () => ({ used: 0 });
// Each ordered read/hash pass gets its own formal byte budget. Check before
// reading and again after reading so a growing file cannot bypass the bound.
const readReceiptBytes = (file, budget) => {
  const size = fs.statSync(file).size;
  if (size > RECEIPT_AUDIT_LIMITS.maxSingleBytes) throw Error('RECEIPT_SIZE_LIMIT');
  if (budget.used + size > RECEIPT_AUDIT_LIMITS.maxTotalBytes) throw Error('RECEIPT_TOTAL_SIZE_LIMIT');
  const bytes = fs.readFileSync(file);
  if (bytes.length > RECEIPT_AUDIT_LIMITS.maxSingleBytes) throw Error('RECEIPT_SIZE_LIMIT');
  if (budget.used + bytes.length > RECEIPT_AUDIT_LIMITS.maxTotalBytes) throw Error('RECEIPT_TOTAL_SIZE_LIMIT');
  budget.used += bytes.length;
  return bytes;
};
const receiptHash = (file, budget) => crypto.createHash('sha256').update(readReceiptBytes(file, budget)).digest('hex');
const readRun = (file, budget) => JSON.parse(readReceiptBytes(file, budget).toString('utf8').replace(/^\uFEFF/, ''));
function receiptSnapshot(receiptFiles) {
  const budget = receiptBudget();
  return receiptFiles.map(file => {
    try {
      const bytes = readReceiptBytes(file, budget);
      return { path: path.resolve(file), sha256: crypto.createHash('sha256').update(bytes).digest('hex'), bytes: bytes.length };
    } catch (error) { return { path: path.resolve(file), sha256: null, bytes: null, readError: error.message }; }
  });
}
function readValidRun(file, budget) {
  const run = readRun(file, budget);
  if (!run || run.tool !== 'ymcc_sim-global' || run.schemaVersion !== 1
    || !['run', 'log-check'].includes(run.mode) || !Array.isArray(run.results) || !run.results.length)
    throw Error('INVALID_RUN_CONTAINER');
  const ids = new Set();
  for (const row of run.results) {
    if (typeof row?.id !== 'string' || !/^[a-z0-9][a-z0-9.-]{0,99}$/.test(row.id) || ids.has(row.id))
      throw Error('INVALID_RUN_CASE_IDS');
    ids.add(row.id);
  }
  return run;
}
const priority = status => status === 'FAIL' ? 0
  : status === 'STALE_OR_INVALID_EVIDENCE' || status === 'NEEDS_REAUDIT' ? 1
  : status === 'BLOCKED' ? 2 : status === 'NOT_RUN' ? 3
  : status === 'STALE_REGISTRATION' ? 4 : 5;

function selectAdapters(adapters, { profile, domains, tests }) {
  return adapters.filter(row => row.profiles.includes(profile)
    && (!domains.length || domains.includes(row.domain))
    && (!tests.length || tests.includes(row.id)));
}
function assignmentsByTest(assignments, adapters) {
  if (!Array.isArray(assignments)) throw Error('ASSIGNMENTS_ARRAY_REQUIRED');
  const known = new Set(adapters.map(row => row.id));
  const owners = new Map();
  for (const assignment of assignments) {
    if (!assignment || typeof assignment.owner !== 'string' || !assignment.owner.trim()
      || assignment.owner.length > 200 || /[\0\r\n]/.test(assignment.owner)
      || !Array.isArray(assignment.tests) || !assignment.tests.length) throw Error('ASSIGNMENT_INVALID');
    for (const id of assignment.tests) {
      if (!known.has(id)) throw Error(`ASSIGNMENT_TEST_NOT_REGISTERED:${id}`);
      if (owners.has(id)) throw Error(`DUPLICATE_TEST_ASSIGNMENT:${id}`);
      owners.set(id, assignment.owner);
    }
  }
  return owners;
}
function sourceSnapshot(repoRoot, adapters) {
  const inputs = adapters.flatMap(row => [row.path, ...(row.evidenceInputs ?? [])]);
  return fingerprintInputs(repoRoot, inputs);
}
function explicitFileSnapshot(receiptFiles) {
  return receiptFiles.map(file => {
    try { return { path: path.resolve(file), sha256: sha256File(file) }; }
    catch { return { path: typeof file === 'string' ? file : null, sha256: null }; }
  });
}
// Capture dynamically reported imports and explicit evidence/artifacts as well as
// today's registry inputs. The audit remains authoritative for their validity.
function recordedInputSnapshot(receiptFiles, selectedIds, repoRoot) {
  const sources = [], files = [], latest = new Map(), budget = receiptBudget();
  for (const receipt of receiptFiles) {
    let run;
    try { run = readValidRun(receipt, budget); } catch { continue; }
    for (const row of run.results) if (selectedIds.has(row.id)) latest.set(row.id, { row, run, receipt });
  }
  // Match the formal auditor's latest-per-case semantics. A superseded row's
  // imports/candidates/evidence must not invalidate the newer executed closure.
  // Invalid containers remain in the caller list for the formal audit to block.
  for (const { row, run, receipt } of latest.values()) {
    for (const entries of [row.sourceInputs, row.dependencyInputs])
      if (Array.isArray(entries)) for (const input of entries)
        if (typeof input?.path === 'string') sources.push(input.path);
    if (Array.isArray(row.candidateArtifacts)) for (const input of row.candidateArtifacts)
      if (typeof input?.path === 'string' && path.isAbsolute(input.path)) files.push(input.path);
    if (row.scope === 'host-mock' && typeof run.hostPayload?.root === 'string' && path.isAbsolute(run.hostPayload.root))
      for (const name of ['YeManFanHost.exe', 'YeManFanHost.dll', 'YeManFanBackend.dll', 'Microsoft.Windows.SDK.NET.dll', 'WinRT.Runtime.dll'])
        files.push(path.join(run.hostPayload.root, name));
    const caseDir = path.resolve(path.dirname(receipt), 'cases', row.id);
    // Completion proofs consume these exact raw streams, so protect their bytes
    // across the formal audit window just like nested structured evidence.
    if (row.completionProof !== undefined) for (const name of ['stdout.log', 'stderr.log']) {
      const file = path.join(caseDir, name);
      try {
        const relative = path.relative(fs.realpathSync(caseDir), fs.realpathSync(file));
        if (relative === '..' || relative.startsWith('..' + path.sep) || path.isAbsolute(relative)) continue;
      } catch { /* Missing streams remain a captured unavailable input. */ }
      files.push(file);
    }
    if (Array.isArray(row.structuredEvidence)) for (const input of row.structuredEvidence) {
      if (typeof input?.path !== 'string' || !path.isAbsolute(input.path)) continue;
      const relative = path.relative(caseDir, path.resolve(input.path));
      if (!relative || relative === '..' || relative.startsWith('..' + path.sep) || path.isAbsolute(relative)) continue;
      try {
        const realRelative = path.relative(fs.realpathSync(caseDir), fs.realpathSync(input.path));
        if (realRelative === '..' || realRelative.startsWith('..' + path.sep) || path.isAbsolute(realRelative)) continue;
      } catch { continue; }
      files.push(input.path);
    }
  }
  return { sources: fingerprintInputs(repoRoot, sources), artifacts: explicitFileSnapshot(distinct(files)) };
}
// Host roots and explicit artifacts stay separate even when the case IDs match.
// These are recorded execution bindings, not instructions to load or install.
function executionBindings(audit) {
  const latest = new Map(), beforeBudget = receiptBudget(), readBudget = receiptBudget(), afterBudget = receiptBudget();
  for (const source of audit.sourceRuns) {
    let run;
    try {
      if (receiptHash(source.file, beforeBudget) !== source.sha256) throw Error('RECEIPT_DRIFT');
      run = readRun(source.file, readBudget);
      if (receiptHash(source.file, afterBudget) !== source.sha256) throw Error('RECEIPT_DRIFT');
    } catch { throw Error('RECEIPT_CHANGED_DURING_PLANNING'); }
    for (const row of run.results) {
      const explicitArtifacts = Array.isArray(row.candidateArtifacts)
        ? row.candidateArtifacts.map(value => ({ path: value?.path ?? null, sha256: value?.sha256 ?? null }))
          .sort((a, b) => String(a.path).localeCompare(String(b.path))) : [];
      const host = row.scope === 'host-mock' ? {
        root: run.hostPayload?.root ?? null,
        binaries: Array.isArray(run.hostPayload?.binaries)
          ? run.hostPayload.binaries.map(value => ({ name: value.name, sha256: value.sha256 }))
            .sort((a, b) => a.name.localeCompare(b.name)) : [],
      } : null;
      latest.set(row.id, { host, explicitArtifacts });
    }
  }
  return latest;
}
function changeReasons(row) {
  const changes = (row.sourceMismatches ?? []).map(value => ({
    role: value.role ?? 'source', path: value.path ?? null,
    expected: value.expected ?? null, actual: value.actual ?? null,
  }));
  const registration = row.registrationMismatches ?? [];
  const diagnostics = row.diagnostics ?? [];
  return { changes, registration, diagnostics };
}
function changeGroup(row) {
  // Equal affected paths AND observed target bytes; no broad transitive union
  // that puts every main.cpp consumer into one enormous batch.
  if (row.sourceMismatches?.length) return row.sourceMismatches.map(value => ({
    role: value.role ?? 'source', path: value.path ?? null, actual: value.actual ?? null,
  })).sort((a, b) => JSON.stringify(a).localeCompare(JSON.stringify(b)));
  if (row.registrationMismatches?.length) return row.registrationMismatches;
  return distinct((row.diagnostics ?? []).map(value => value.code ?? 'UNSPECIFIED'));
}

export function createRevalidationPlan(receiptFiles, {
  repoRoot, adapters, profile = 'full', domains = [], tests = [],
  assignments = [], maxBatchSize = 6,
} = {}) {
  if (typeof repoRoot !== 'string' || !Array.isArray(receiptFiles)
    || !receiptFiles.length || receiptFiles.length > RECEIPT_AUDIT_LIMITS.maxFiles
    || receiptFiles.some(file => typeof file !== 'string' || !file || /[\0\r\n]/.test(file))
    || !Number.isSafeInteger(maxBatchSize) || maxBatchSize < 1 || maxBatchSize > 32)
    throw Error('REVALIDATION_PLAN_INPUT_INVALID');
  const reviewed = validateAdapters(adapters, repoRoot);
  // Validate explicit selections before deriving a batch from an empty scope.
  auditRegisteredScope({ cases: [], diagnostics: [], status: 'BLOCKED' }, reviewed, { profile, domains, tests });
  const selected = selectAdapters(reviewed, { profile, domains, tests });
  const owners = assignmentsByTest(assignments, reviewed);
  const beforeSources = sourceSnapshot(repoRoot, selected);
  const registryArtifacts = distinct(selected.flatMap(row => row.artifactInputs ?? []));
  const beforeRegistryArtifacts = fingerprintArtifacts(registryArtifacts);
  const beforeReceipts = receiptSnapshot(receiptFiles);
  const selectedIds = new Set(selected.map(row => row.id));
  const beforeRecorded = recordedInputSnapshot(receiptFiles, selectedIds, repoRoot);
  const audit = auditRegisteredScope(auditReceipts(receiptFiles, { repoRoot }), reviewed, { profile, domains, tests });
  const planDiagnostics = [];
  let bindings = new Map();
  try { bindings = executionBindings(audit); }
  catch { planDiagnostics.push({ code: 'RECEIPT_CHANGED_DURING_PLANNING' }); }
  const afterSources = sourceSnapshot(repoRoot, selected);
  const afterRegistryArtifacts = fingerprintArtifacts(registryArtifacts);
  if (!same(beforeRegistryArtifacts, afterRegistryArtifacts)) planDiagnostics.push({ code: 'REGISTERED_ARTIFACT_CHANGED_DURING_PLANNING' });
  const afterReceipts = receiptSnapshot(receiptFiles);
  const afterRecorded = recordedInputSnapshot(receiptFiles, selectedIds, repoRoot);
  if (!same(beforeRecorded, afterRecorded)) planDiagnostics.push({ code: 'RECORDED_INPUT_CHANGED_DURING_PLANNING' });
  if (!same(beforeSources, afterSources)) planDiagnostics.push({ code: 'SOURCE_CHANGED_DURING_PLANNING' });
  if (!same(beforeReceipts, afterReceipts)) planDiagnostics.push({ code: 'RECEIPT_CHANGED_DURING_PLANNING' });
  const blocked = planDiagnostics.length > 0 || audit.diagnostics.length > 0 || !selected.length;
  const audited = new Map(audit.cases.map(row => [row.id, row]));
  const rows = selected.map(adapter => {
    const original = audited.get(adapter.id) ?? { id: adapter.id, status: 'NOT_RUN', diagnostics: [{ code: 'NO_EXECUTION_RECEIPT_FOR_CURRENT_REGISTERED_TEST' }] };
    const status = blocked && original.status === 'PASS' ? 'NEEDS_REAUDIT' : original.status;
    return { id: adapter.id, domain: adapter.domain, scope: adapter.scope, status,
      auditedStatus: original.status, owner: owners.get(adapter.id) ?? null,
      requiredSourceInputs: distinct([adapter.path, ...(adapter.evidenceInputs ?? [])]),
      executionBinding: bindings.get(adapter.id) ?? { host: null, explicitArtifacts: [] },
      reasons: changeReasons(original), group: changeGroup(original),
    };
  });
  const reusable = rows.filter(row => row.status === 'PASS');
  const pending = rows.filter(row => row.status !== 'PASS')
    .sort((a, b) => priority(a.status) - priority(b.status) || a.id.localeCompare(b.id));
  const sourceStates = new Map([...afterRecorded.sources, ...afterSources].map(row => [row.path.replaceAll('\\', '/'), row]));
  const artifactStates = new Map([...afterRecorded.artifacts, ...afterRegistryArtifacts].map(row => [row.path, row]));
  const adapterMap = new Map(selected.map(row => [row.id, row]));
  const groups = new Map();
  for (const row of pending) {
    const key = digest({ priority: priority(row.status), status: row.status, owner: row.owner,
      binding: row.executionBinding, changes: row.group });
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(row);
  }
  const batches = [];
  for (const [key, group] of groups) {
    for (let offset = 0; offset < group.length; offset += maxBatchSize) {
      const chunk = group.slice(offset, offset + maxBatchSize);
      const sourcePaths = distinct(chunk.flatMap(row => [
        ...row.requiredSourceInputs, ...(audited.get(row.id)?.execution?.sourceInputs ?? []),
      ]).filter(value => typeof value === 'string'));
      const artifactPaths = distinct(chunk.flatMap(row => {
        const binding = row.executionBinding;
        const host = binding.host?.root ? binding.host.binaries
          .filter(binary => ['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll'].includes(binary.name))
          .map(binary => path.join(binding.host.root, binary.name)) : [];
        return [...(adapterMap.get(row.id).artifactInputs ?? []),
          ...binding.explicitArtifacts.map(input => input.path).filter(value => typeof value === 'string' && path.isAbsolute(value)), ...host];
      }));
      const inputState = {
        sources: sourcePaths.map(relative => sourceStates.get(relative.replaceAll('\\', '/'))
          ?? { path: relative, sha256: null, status: 'NOT_CAPTURED' }),
        artifacts: artifactPaths.map(file => ({ ...(artifactStates.get(file) ?? { path: file, sha256: null, status: 'MISSING_OR_NOT_A_FILE' }) })),
      };
      // Captured absent optional Host companions are a stable observation, not
      // a required dependency; other absent inputs still block readiness.
      for (const artifact of inputState.artifacts) if (artifact.sha256 === null) {
        const optional = chunk.every(row => {
          const host = row.executionBinding.host;
          return host && !['YeManFanHost.exe', 'YeManFanHost.dll'].includes(path.basename(artifact.path))
            && host.binaries.some(binary => path.join(host.root, binary.name) === artifact.path && binary.sha256 === null)
            && !(adapterMap.get(row.id).artifactInputs ?? []).includes(artifact.path);
        });
        if (optional) artifact.optionalAbsent = true;
      }
      batches.push({ id: `batch-${String(batches.length + 1).padStart(3, '0')}`,
        priority: priority(chunk[0].status), reasonStatus: chunk[0].status,
        owner: chunk[0].owner, assignmentMeaning: chunk[0].owner ? 'DECLARED_ASSIGNMENT_NOT_LIVE_PROCESS_PROOF' : 'UNASSIGNED',
        groupFingerprint: key, testIds: chunk.map(row => row.id),
        executionBinding: chunk[0].executionBinding,
        mustConfirmCandidateInputsBeforeRun: true,
        changedInputs: chunk[0].group,
        sourceInputs: sourcePaths, inputState, inputStateFingerprint: digest(inputState),
      });
    }
  }
  const covered = batches.flatMap(batch => batch.testIds);
  if (covered.length !== new Set(covered).size || !same(distinct(covered), distinct(pending.map(row => row.id))))
    throw Error('REVALIDATION_PLAN_LOST_OR_DUPLICATED_TEST');
  const hotspots = new Map();
  for (const row of pending) for (const change of row.reasons.changes) {
    const key = `${change.role}:${change.path}`;
    if (!hotspots.has(key)) hotspots.set(key, { role: change.role, path: change.path, testIds: [] });
    hotspots.get(key).testIds.push(row.id);
  }
  const plan = {
    schemaVersion: 1, kind: 'ymcc-revalidation-plan', generatedAt: new Date().toISOString(),
    status: blocked ? 'BLOCKED' : 'PLAN_ONLY', mode: 'read-only-no-execution', executedTests: 0,
    fullProductAcceptance: false, physicalHardwareAcceptance: false,
    selection: { profile, domains, tests, ids: selected.map(row => row.id) },
    summary: { selected: rows.length, reusableAtAudit: reusable.length, needsRerun: pending.length,
      assignedNeedsRerun: pending.filter(row => row.owner !== null).length,
      unassignedNeedsRerun: pending.filter(row => row.owner === null).length, batches: batches.length },
    sourceAudit: { generatedAt: audit.generatedAt, status: audit.status, sourceRuns: audit.sourceRuns },
    registryFingerprint: digest(reviewed), sourceSnapshotFingerprint: digest(afterSources),
    diagnostics: [...audit.diagnostics, ...planDiagnostics],
    reusableAtAudit: reusable.map(row => ({ id: row.id, status: row.status, scope: row.scope })),
    needsRerun: pending.map(({ group, ...row }) => row), batches,
    changedInputHotspots: [...hotspots.values()].sort((a, b) => b.testIds.length - a.testIds.length || String(a.path).localeCompare(String(b.path))),
    outsideSelection: audit.cases.filter(row => !selected.some(adapter => adapter.id === row.id)).map(row => ({ id: row.id, status: row.status })),
    acceptanceMeaning: 'Only scheduling of current registered tests. No tests execute and no PASS is produced by this plan. Assignments never remove revalidation obligations. Re-audit after changes and verify the final candidate separately.',
  };
  return { plan, audit };
}

/** Cheap preflight of known planned inputs, never a test/acceptance result.
 * Run it immediately before a chosen batch; the real runner still fingerprints
 * the executed inputs and the final audit still checks the completed receipts.
 */
export function checkBatchInputs(plan, batchId, { repoRoot } = {}) {
  if (plan?.kind !== 'ymcc-revalidation-plan' || plan.schemaVersion !== 1 || plan.status !== 'PLAN_ONLY'
    || typeof repoRoot !== 'string' || typeof batchId !== 'string' || !Array.isArray(plan.batches))
    throw Error('BATCH_PLAN_INPUT_INVALID');
  const matches = plan.batches.filter(batch => batch.id === batchId);
  if (matches.length !== 1) throw Error('BATCH_ID_NOT_UNIQUE_OR_MISSING');
  const batch = matches[0], expected = batch.inputState;
  if (!expected || !Array.isArray(expected.sources) || !Array.isArray(expected.artifacts)
    || expected.sources.some(row => typeof row?.path !== 'string')
    || expected.artifacts.some(row => typeof row?.path !== 'string' || !path.isAbsolute(row.path))
    || digest(expected) !== batch.inputStateFingerprint) throw Error('BATCH_INPUT_STATE_INVALID');
  const sources = fingerprintInputs(repoRoot, expected.sources.map(row => row.path));
  const artifacts = fingerprintArtifacts(expected.artifacts.map(row => row.path));
  const sourceMap = new Map(sources.map(row => [row.path, row]));
  const artifactMap = new Map(artifacts.map(row => [row.path, row]));
  const changes = [], missing = [];
  for (const [kind, targets, current] of [['source', expected.sources, sourceMap], ['artifact', expected.artifacts, artifactMap]]) {
    for (const target of targets) {
      const actual = current.get(kind === 'source' ? target.path.replaceAll('\\', '/') : target.path);
      if (target.sha256 === null && !target.optionalAbsent) missing.push({ kind, path: target.path, code: 'PLAN_INPUT_WAS_NOT_AVAILABLE' });
      if (!actual || target.sha256 !== actual.sha256) changes.push({ kind, path: target.path, expected: target.sha256, actual: actual?.sha256 ?? null });
    }
  }
  return { schemaVersion: 1, kind: 'ymcc-batch-input-check', generatedAt: new Date().toISOString(),
    status: changes.length ? 'PLAN_INPUTS_CHANGED' : missing.length ? 'BLOCKED_INPUTS_MISSING' : 'INPUTS_MATCH_PLAN',
    batchId, testIds: batch.testIds, changes, missing, executedTests: 0,
    fullProductAcceptance: false, physicalHardwareAcceptance: false,
    meaning: 'Only known input bytes match the saved plan. No source lock, live worker, product execution, candidate approval or complete dependency coverage is proven. The actual runner and final audit remain required.',
  };
}

/** Suggest only removal of fully superseded valid run containers from audit
 * inputs. Originals are never written or removed. This produces no PASS and
 * never merges or re-signs execution receipts. Malformed files stay retained.
 */
export function selectContributingReceipts(receiptFiles) {
  if (!Array.isArray(receiptFiles) || !receiptFiles.length || receiptFiles.length > 256
    || receiptFiles.some(file => typeof file !== 'string' || !file || /[\0\r\n]/.test(file)))
    throw Error('RECEIPT_SELECTION_INPUT_INVALID');
  const before = receiptSnapshot(receiptFiles), budget = receiptBudget();
  const rows = receiptFiles.map((file, index) => {
    try { return { file, index, run: readValidRun(file, budget), valid: true }; }
    catch { return { file, index, run: null, valid: false }; }
  });
  const latest = new Map();
  for (const row of rows) if (row.valid) for (const item of row.run.results) latest.set(item.id, row.index);
  const contributors = new Set(latest.values());
  const kept = rows.filter(row => !row.valid || contributors.has(row.index));
  const omitted = rows.filter(row => row.valid && !contributors.has(row.index)).map(row => ({
    file: row.file, inputOrder: row.index + 1, sha256: before[row.index].sha256,
    reason: 'ALL_CASES_HAVE_LATER_VALID_CONTAINER_RECEIPTS',
    replacedCases: row.run.results.map(item => ({ id: item.id, latestInputOrder: latest.get(item.id) + 1,
      latestFile: rows[latest.get(item.id)].file })),
  }));
  const after = receiptSnapshot(receiptFiles);
  const changed = !same(before, after);
  return { schemaVersion: 1, kind: 'ymcc-contributing-receipt-selection', generatedAt: new Date().toISOString(),
    status: changed ? 'BLOCKED_INPUT_CHANGED' : 'SELECTION_ONLY', executedTests: 0,
    inputCount: rows.length, retainedCount: kept.length, supersededCount: omitted.length,
    receiptFiles: changed ? [...receiptFiles] : kept.map(row => row.file),
    retained: kept.map(row => ({ file: row.file, inputOrder: row.index + 1, sha256: after[row.index].sha256,
      bytes: after[row.index].bytes, readError: after[row.index].readError ?? null,
      invalidContainerRetained: !row.valid, latestCaseIds: row.valid ? row.run.results.filter(item => latest.get(item.id) === row.index).map(item => item.id) : [] })),
    omitted: changed ? [] : omitted, inputFiles: before,
    readLimits: RECEIPT_AUDIT_LIMITS,
    withinFormalAuditReceiptLimit: !changed && kept.length <= RECEIPT_AUDIT_LIMITS.maxFiles
      && kept.every(row => after[row.index].sha256 !== null),
    meaning: 'Only an ordered suggestion for the existing auditor. Invalid containers remain. Latest FAIL/BLOCKED replaces older PASS. Keep original files and run the formal audit on the retained list; count/byte limits and unchanged inputs are not audit acceptance. Unreadable or over-budget originals remain retained.',
  };
}
