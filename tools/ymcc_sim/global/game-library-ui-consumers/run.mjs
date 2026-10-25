import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { REPO, MODULE, SPECS, sourceFiles, toolRows } from './adapter.mjs';
import { hash, need, inside, sourceFile, applyFault, verifyOriginal } from './contract.mjs';
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n');
function outputs(root, typescriptRoot) {
  const files = [];
  function visit(dir) {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const file = path.join(dir, e.name);
      if (e.name === 'node_modules') {
        const members = fs.readdirSync(file);
        need(e.isDirectory() && members.length === 1 && members[0] === 'typescript', 'CONTROLLED_TOOL_ALIAS_MEMBERS');
        const alias = path.join(file, 'typescript');
        need(fs.lstatSync(alias).isSymbolicLink() && fs.realpathSync(alias).toLowerCase() === fs.realpathSync(typescriptRoot).toLowerCase(), 'CONTROLLED_TOOL_ALIAS_CHANGED');
        continue;
      }
      need(!e.isSymbolicLink(), 'OUTPUT_REPARSE');
      if (e.isDirectory()) visit(file); else { need(e.isFile(), 'OUTPUT_NONFILE'); files.push(file); }
      need(files.length <= 500, 'OUTPUT_LIMIT');
    }
  }
  visit(root); return files.sort();
}
export async function execute(key, root) {
  const spec = SPECS[key]; need(spec, 'CASE_UNKNOWN');
  root = path.resolve(root);
  need(path.isAbsolute(root) && !inside(REPO, root) && !fs.existsSync(root), 'FRESH_OWNED_EVIDENCE_REQUIRED');
  fs.mkdirSync(root, { recursive: true });
  const steps = [], tool = toolRows(REPO), sourceRows = [];
  need(fs.realpathSync(process.execPath).toLowerCase() === fs.realpathSync(tool.nodeExecutable).toLowerCase(), 'ACTUAL_NODE_EXECUTABLE_CHANGED');
  try {
    for (const relative of sourceFiles(key)) {
      const original = sourceFile(REPO, relative), snapshot = path.join(root, 'input-sources', relative);
      fs.mkdirSync(path.dirname(snapshot), { recursive: true }); fs.copyFileSync(original, snapshot, fs.constants.COPYFILE_EXCL);
      need(hash(original) === hash(snapshot), 'CAPTURE_BYTES_CHANGED');
      sourceRows.push({ relative, original, snapshot, sha256: hash(snapshot) });
    }
    need(sourceRows.find(r => r.relative === spec.producer).sha256 === spec.producerSha256, 'ORIGINAL_ASSERTION_SOURCE_CHANGED');
    write(path.join(root, 'input-snapshot.json'), { sourceRows, tool, positiveViewPolicy: 'Exact producer/product bytes; independent fresh view only relocates output paths', nativeOrSteamIo: false });
    function run(label, negative) {
      const stepRoot = path.join(root, label), view = path.join(stepRoot, 'source', 'repo'), viewSources = [];
      fs.mkdirSync(view, { recursive: true });
      for (const relative of ['package.json', spec.producer, ...spec.productSources]) {
        const row = sourceRows.find(r => r.relative === relative), target = path.join(view, relative);
        fs.mkdirSync(path.dirname(target), { recursive: true }); fs.copyFileSync(row.snapshot, target, fs.constants.COPYFILE_EXCL);
        need(hash(target) === row.sha256, 'VIEW_SOURCE_DIFFERS');
        viewSources.push({ relative, path: target, sha256: row.sha256, originalSource: row.original, positiveSameBytes: true, role: 'exact producer/product source view' });
      }
      let mutation = null;
      if (negative) {
        const relative = 'CustomSteamLibrary/workspace-ui/app.js', row = viewSources.find(r => r.relative === relative), original = fs.readFileSync(row.path, 'utf8'), after = applyFault(original, spec.fault);
        fs.writeFileSync(row.path, after); mutation = { relative, originalSha256: row.sha256, faultSha256: hash(row.path), ...spec.fault };
        row.sha256 = mutation.faultSha256; row.positiveSameBytes = false; row.role = 'exact single-function fault source';
      }
      const alias = path.join(view, 'node_modules', 'typescript'); fs.mkdirSync(path.dirname(alias), { recursive: true }); fs.symlinkSync(tool.typescriptRoot, alias, 'junction');
      const rawFile = path.join(stepRoot, 'Build', 'original-report.json'), traceFile = path.join(stepRoot, 'observations.json'), configFile = path.join(stepRoot, 'observer-config.json');
      fs.mkdirSync(path.dirname(rawFile), { recursive: true });
      write(configFile, { caseRoot: stepRoot, producer: path.join(view, spec.producer), trace: traceFile, reads: [...viewSources, ...tool.files.map(row => ({ ...row, role: 'exact Node/TypeScript tool' }))] });
      const args = ['--require', path.join(REPO, MODULE, 'observe.cjs'), path.join(view, spec.producer), rawFile], startedAt = new Date().toISOString();
      const proc = spawnSync(process.execPath, args, { cwd: view, env: { ...process.env, YMCC_LIBRARY_OBSERVER_CONFIG: configFile }, encoding: 'utf8', timeout: 30000, maxBuffer: 4 * 1024 * 1024, windowsHide: true });
      fs.writeFileSync(path.join(stepRoot, 'stdout.log'), proc.stdout ?? ''); fs.writeFileSync(path.join(stepRoot, 'stderr.log'), proc.stderr ?? '');
      const terminal = { label, negative, executable: process.execPath, args, cwd: view, startedAt, finishedAt: new Date().toISOString(), pid: proc.pid, exitCode: proc.status, signal: proc.signal, error: proc.error?.message ?? null };
      steps.push(terminal); write(path.join(root, 'process-ledger.json'), { steps });
      need(!proc.error && !proc.signal && Number.isInteger(proc.status), 'CHILD_NOT_NATURALLY_TERMINAL');
      need(fs.existsSync(rawFile) && fs.existsSync(traceFile), 'ORIGINAL_REPORT_OR_OBSERVER_MISSING');
      const raw = read(rawFile), trace = read(traceFile), verification = verifyOriginal(raw, spec, { negative, exitCode: proc.status });
      need(Array.isArray(trace.forbidden) && trace.forbidden.length === 0 && trace.physicalIo === false, 'IO_BOUNDARY_VIOLATION');
      for (const row of viewSources) need(hash(row.path) === row.sha256, 'EXECUTED_VIEW_CHANGED');
      for (const row of trace.sourceReads) need([...viewSources, ...tool.files].some(input => fs.realpathSync(input.path).toLowerCase() === row.realPath.toLowerCase() && input.sha256 === row.sha256), 'OBSERVED_INPUT_UNDECLARED');
      for (const relative of spec.productSources) need(trace.sourceReads.some(row => fs.realpathSync(row.path).toLowerCase() === fs.realpathSync(path.join(view, relative)).toLowerCase()), 'PRODUCT_SOURCE_NOT_ACTUALLY_READ');
      for (const row of trace.toolLoads) need(tool.files.some(input => fs.realpathSync(input.path).toLowerCase() === row.realPath.toLowerCase() && input.sha256 === row.sha256), 'LOADED_TOOL_UNDECLARED');
      need(hash(path.join(view, spec.producer)) === spec.producerSha256, 'ORIGINAL_ASSERTIONS_MODIFIED');
      if (negative) write(path.join(stepRoot, 'single-function-mutation.json'), mutation);
      write(path.join(stepRoot, 'view-provenance.json'), { actualViewRoot: view, actualProducer: path.join(view, spec.producer), sourceRows: viewSources, originalProducerUnchanged: true, actualToolLoads: trace.toolLoads, sourceReadsObserved: trace.sourceReads });
      return { ...verification, originalExitCode: proc.status, originalReport: rawFile, originalReportSha256: hash(rawFile), observation: traceFile, observationSha256: hash(traceFile), actualViewRoot: view, mutation, sourceReadCount: trace.sourceReads.length, toolLoadCount: trace.toolLoads.length };
    }
    const positive = run('positive', false), fault = run('fault', true);
    for (const row of sourceRows) need(hash(row.original) === row.sha256 && hash(row.snapshot) === row.sha256, 'SOURCE_CHANGED_DURING_RUN');
    for (const row of tool.files) need(hash(row.path) === row.sha256, 'TOOL_CHANGED_DURING_RUN');
    const owned = outputs(root, tool.typescriptRoot), result = { schemaVersion: 1, id: spec.id, status: 'PASS', passed: true, checks: positive.cases, positive, fault, sourceHashes: [...sourceRows.map(r => ({ path: r.relative, sha256: r.sha256 })), ...owned.map(file => ({ path: file, sha256: hash(file) }))], actualSourceViewsRelocated: true, originalProductInputCopiesVerified: true, originalProducerAssertionsUnchanged: true, sourceProvenance: sourceRows, toolIdentities: tool.files, process: { owned: steps.length, naturalExit: steps.every(r => !r.error && !r.signal && Number.isInteger(r.exitCode)) }, boundary: { actualWorkspaceFunctions: true, vmWithOriginalMemoryPorts: true, uiBrowserRendered: false, nativeChildExecuted: false, realSteamFilesModified: false, steamStoppedOrLaunched: false, networkUsed: false, physicalHardwareValidated: false }, fullProductAcceptance: false, goalComplete: false };
    write(path.join(root, 'result.json'), result); return result;
  } catch (error) { write(path.join(root, 'failure.json'), { status: 'FAIL', error: error.message, steps, compilationOrPreconditionIsNotSemanticFault: true }); throw error; }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { const args = process.argv.slice(2); need(args.length === 4 && args[0] === '--case' && args[2] === '--evidence-dir', 'ARGUMENTS_INVALID'); const r = await execute(args[1], args[3]); console.log(JSON.stringify({ id: r.id, status: r.status, checks: r.checks, namedFaults: r.fault.failedNames })); }
  catch (error) { console.error(error.stack ?? error.message); process.exitCode = 2; }
}