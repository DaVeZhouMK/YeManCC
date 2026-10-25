import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import childProcess from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { IPC_SOURCE } from './memory-ipc.mjs';
export const MODULE = 'tools/ymcc_sim/global/cpulock-consumer';
export const ID = 'performance.cpulock-consumer';
const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_REPO = path.resolve(HERE, '../../../..');
const EXPECTED_NODE = 'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe';
const FIXTURE_FILES = ['adapter.mjs', 'run.mjs', 'worker.mjs', 'scenarios.mjs', 'memory-ipc.mjs', 'spec.json', 'toolchain-inputs.json', 'source-inputs.json', 'README.md'];
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const hash = file => sha(fs.readFileSync(file));
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n');
const need = (condition, code) => { if (!condition) throw new Error('CPULOCK_' + code); };
const inside = (root, file) => { const relative = path.relative(root, file); return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith('..' + path.sep)); };
const relative = (root, file) => path.relative(root, file).replaceAll('\\', '/');
function allFiles(root) {
  return fs.readdirSync(root, { withFileTypes: true }).flatMap(item => {
    const file = path.join(root, item.name);
    need(!item.isSymbolicLink(), 'OUTPUT_LINK_REJECTED');
    return item.isDirectory() ? allFiles(file) : [file];
  }).sort();
}
function supportClosure(repo, entry) {
  const rows = new Map();
  function visit(file) {
    file = fs.realpathSync(file);
    if (rows.has(file)) return;
    need(inside(repo, file) && !file.includes('node_modules'), 'SUPPORT_SOURCE_ESCAPE');
    const bytes = fs.readFileSync(file);
    rows.set(file, { path: relative(repo, file), actualPath: file, sha256: sha(bytes), role: 'declared-esm-support-closure' });
    const source = bytes.toString('utf8');
    for (const match of source.matchAll(/(?:from\s*|import\s*)['"](\.[^'"]+)['"]/g)) visit(path.resolve(path.dirname(file), match[1]));
  }
  visit(entry);
  return [...rows.values()].sort((a, b) => a.path.localeCompare(b.path));
}
export function parseArgs(args) {
  const result = { repo: DEFAULT_REPO, out: null, prepare: false, audit: false };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--source-root') result.repo = args[++i];
    else if (arg === '--evidence-dir') result.out = args[++i];
    else if (arg === '--prepare-toolchain') result.prepare = true;
    else if (arg === '--audit') result.audit = true;
    else throw new Error('CPULOCK_UNKNOWN_ARGUMENT:' + arg);
  }
  need(path.isAbsolute(result.repo) && (result.prepare || path.isAbsolute(result.out ?? '')), 'ABSOLUTE_PATH_REQUIRED');
  result.repo = fs.realpathSync(result.repo);
  if (result.out) result.out = path.resolve(result.out);
  return result;
}
export async function auditReceipt(repo, root) {
  const report = read(path.join(root, 'result.json'));
  need(report.passed === true && report.id === ID, 'AUDIT_REPORT_NOT_PASS');
  for (const row of report.sourceHashes) {
    const file = path.isAbsolute(row.path) ? row.path : path.join(repo, row.path);
    need(inside(root, file) || inside(repo, file), 'AUDIT_SOURCE_ESCAPE');
    need(hash(file) === row.sha256, 'AUDIT_SOURCE_CHANGED:' + row.path);
  }
  for (const row of report.fixtureInputs) need(hash(row.actualPath) === row.sha256, 'AUDIT_FIXTURE_CHANGED:' + row.path);
  for (const row of report.toolInputs) need(hash(row.path) === row.sha256, 'AUDIT_TOOL_CHANGED:' + row.path);
  for (const row of report.processLedger) need(row.naturalExit === true && row.signal === null && row.error === null, 'AUDIT_UNNATURAL_EXIT');
  return { status: 'PASS', sourceRows: report.sourceHashes.length, fixtureRows: report.fixtureInputs.length, toolRows: report.toolInputs.length, fullProductAcceptance: false };
}
export async function runConsumer({ repo, out: root, prepare = false, audit = false }) {
  need(fs.realpathSync(process.execPath).toLowerCase() === fs.realpathSync(EXPECTED_NODE).toLowerCase(), 'FIXED_NODE_REQUIRED');
  if (audit) return auditReceipt(repo, root);
  need(!process.env.NODE_OPTIONS && !process.env.NODE_PATH && !process.env.ESBUILD_BINARY_PATH, 'UNDECLARED_RUNTIME_OVERRIDE');
  const helper = path.join(repo, 'tools/ymcc_sim/global/bundled-node-tool-authority.mjs');
  const supportInputs = supportClosure(repo, helper);
  const { lockBundledNodeTools } = await import(pathToFileURL(helper).href);
  const authority = lockBundledNodeTools(repo);
  if (prepare) {
    write(path.join(HERE, 'toolchain-inputs.json'), { schemaVersion: 1, nodeExecutable: fs.realpathSync(EXPECTED_NODE),
      compilerEntry: authority.compilerEntry, compilerBinary: authority.compilerBinary, compilerVersion: authority.compilerVersion,
      graphSha256: authority.graphSha256, packageCount: authority.packageCount,
      files: authority.tools, declaration: 'Existing global complete package authority; predeclared scope, not an observed OS loader graph.' });
    return { status: 'PASS', prepared: true, files: authority.tools.length };
  }
  const tool = read(path.join(HERE, 'toolchain-inputs.json'));
  need(JSON.stringify(tool.files) === JSON.stringify(authority.tools) && tool.graphSha256 === authority.graphSha256, 'TOOLCHAIN_PIN_CHANGED');
  need(!inside(repo, root) && !fs.existsSync(root), 'FRESH_EXTERNAL_EVIDENCE_REQUIRED');
  fs.mkdirSync(root, { recursive: true });
  const startedAt = new Date().toISOString();
  const spec = read(path.join(HERE, 'spec.json'));
  const declaredSources = read(path.join(HERE, 'source-inputs.json'));
  const fixtureInputs = FIXTURE_FILES.map(name => ({ path: MODULE + '/' + name, actualPath: path.join(HERE, name), sha256: hash(path.join(HERE, name)) }));
  for (const row of fixtureInputs) {
    const target = path.join(root, 'input-fixture', path.basename(row.path)); fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.copyFileSync(row.actualPath, target);
  }
  const originalSource = fs.readFileSync(path.join(repo, spec.product), 'utf8');
  const exports = originalSource.split(/\r?\n/).flatMap((line, i) => {
    const match = /^export\s+(?:(async)\s+)?(const|function|type|interface)\s+(\w+)/.exec(line);
    return match ? [{ line: i + 1, operation: match[3], declaration: match[2], runtime: !['type', 'interface'].includes(match[2]) }] : [];
  });
  write(path.join(root, 'export-manifest.json'), { path: spec.product, sha256: sha(originalSource), declarations: exports,
    runtimeFunctions: spec.runtimeFunctions, uniqueRuntimeExports: spec.runtimeFunctions.length + spec.runtimeConstants.length,
    repeatedOverloadsAreNotIndependentOperations: true, typesHaveNoRuntimeBehavior: true,
    aliases: { isCpuSideLocked: 'isCpuLocked(side)', getCpuLockSide: 'sideCfg(side)', updateCpuLockSide: 'updateCpuLock(side, values)', unlockCpuSide: 'clearCpuLock(side)' },
    ipcOperationsInThisFile: [], ipcOperationsInActualDependencyChain: ['settings.read', 'settings.write', 'registry.read', 'registry.writePowerBatch'] });
  const sourceMap = new Map();
  const referenceInputs = [{ path: 'src/bridge/ipc.ts', actualPath: path.join(repo, 'src/bridge/ipc.ts'), sha256: hash(path.join(repo, 'src/bridge/ipc.ts')), role: 'replaced-external-transport-not-executed' }];
  const processLedger = [], compilerChildren = [], compilerProcesses = [], spawnOriginal = childProcess.spawn;
  let compiler;
  try {
    childProcess.spawn = function(executable, args, options) {
      need(fs.realpathSync(executable).toLowerCase() === fs.realpathSync(tool.compilerBinary).toLowerCase(), 'UNDECLARED_COMPILER_CHILD');
      const child = spawnOriginal.call(this, executable, args, { ...options, windowsHide: true });
      const row = { role: 'esbuild-service', terminationMethod: 'stdin-eof', executable, executableSha256: hash(executable), args, pid: child.pid, exitCode: null, signal: null, error: null, naturalExit: false };
      processLedger.push(row); compilerProcesses.push(child);
      compilerChildren.push(new Promise(resolve => {
        child.once('error', error => { row.error = error.message; });
        child.once('close', (code, signal) => { row.exitCode = code; row.signal = signal; row.naturalExit = code === 0 && !signal && !row.error; resolve(); });
      }));
      return child;
    };
    compiler = createRequire(path.join(repo, 'package.json'))(tool.compilerEntry);
    async function execute(label, fault) {
      const caseRoot = path.join(root, label); fs.mkdirSync(caseRoot);
      const observed = new Map();
      const entry = `import * as cpulock from './src/bridge/cpulock.ts';\nimport { normalizeSettings, setSettingsDirectory, clearSettingsCache } from './src/bridge/settingsRepository.ts';\nexport { cpulock, normalizeSettings, setSettingsDirectory, clearSettingsCache };`;
      fs.writeFileSync(path.join(caseRoot, 'entry.ts'), entry);
      const options = { stdin: { contents: entry, resolveDir: repo, sourcefile: 'cpulock-fixture-entry.ts', loader: 'ts' },
        absWorkingDir: repo, bundle: true, platform: 'browser', format: 'iife', globalName: 'YmccCpuLockConsumer',
        write: false, treeShaking: true, logLevel: 'silent', tsconfigRaw: {}, alias: { '@': path.join(repo, 'src') }, define: { 'process.env.NODE_ENV': '"production"' },
        plugins: [{ name: 'actual-ts-only-external-ipc-replaced', setup(build) {
          build.onResolve({ filter: /^\.{1,2}\/.+|^\.\/ipc$/ }, args => {
            if (path.resolve(args.resolveDir, args.path).replaceAll('\\', '/') === path.join(repo, 'src/bridge/ipc').replaceAll('\\', '/')) return { path: 'ipc-memory-port', namespace: 'fixture' };
            return undefined;
          });
          build.onLoad({ filter: /.*/, namespace: 'fixture' }, () => ({ contents: IPC_SOURCE, loader: 'js' }));
          build.onLoad({ filter: /\.(?:ts|js|mjs|cjs|json)$/ }, args => {
            const actual = fs.realpathSync(args.path), bytes = fs.readFileSync(actual), digest = sha(bytes);
            const toolRow = tool.files.find(row => path.resolve(row.path).toLowerCase() === actual.toLowerCase());
            if (actual.split(/[\\/]/).includes('node_modules')) need(toolRow?.sha256 === digest, 'UNDECLARED_PACKAGE_INPUT:' + actual);
            else {
              need(inside(path.join(repo, 'src'), actual) || relative(repo, actual) === 'PowerControl/GameTitlePolicy.json', 'UNREVIEWED_SOURCE:' + actual);
              const rel = relative(repo, actual);
              const previous = sourceMap.get(rel);
              need(!previous || previous.sha256 === digest, 'SOURCE_CHANGED_BETWEEN_BUILDS:' + rel);
              sourceMap.set(rel, { path: rel, actualPath: actual, sha256: digest });
              const captured = path.join(root, 'input-sources', rel); fs.mkdirSync(path.dirname(captured), { recursive: true });
              if (!fs.existsSync(captured)) fs.writeFileSync(captured, bytes);
            }
            let source = bytes.toString('utf8');
            if (fault && relative(repo, actual) === spec.product) {
              need(source.split(fault.before).length === 2, 'FAULT_ANCHOR_MUST_BE_UNIQUE');
              source = source.replace(fault.before, fault.after);
              fs.writeFileSync(path.join(caseRoot, 'mutated-cpulock.ts'), source);
            }
            observed.set(actual, { path: actual, originalFileSha256: digest, actualCompilerContentSha256: sha(source), deliberateProductMutation: Boolean(fault && relative(repo, actual) === spec.product), role: toolRow ? 'tool-package-compiler-input' : 'product-source-compiler-input' });
            return { contents: source, loader: path.extname(actual).slice(1) === 'ts' ? 'ts' : path.extname(actual) === '.json' ? 'json' : 'js', resolveDir: path.dirname(actual) };
          });
        } }] };
      const build = await compiler.build({ ...options, metafile: true });
      const withoutMetadata = await compiler.build({ ...options, metafile: false });
      need(build.outputFiles.length === 1 && withoutMetadata.outputFiles.length === 1 && Buffer.from(build.outputFiles[0].contents).equals(Buffer.from(withoutMetadata.outputFiles[0].contents)), 'METADATA_CHANGED_EXECUTABLE');
      need(Object.values(build.metafile.outputs).every(row => row.imports.length === 0), 'RUNTIME_EXTERNAL_IMPORT_FORBIDDEN');
      need(![...observed.keys()].some(file => relative(repo, file) === 'src/bridge/ipc.ts'), 'REAL_TRANSPORT_LOADED');
      const bundle = path.join(caseRoot, 'actual-product.js'); fs.writeFileSync(bundle, build.outputFiles[0].contents);
      write(path.join(caseRoot, 'metafile.json'), build.metafile); write(path.join(caseRoot, 'compiler-inputs.json'), [...observed.values()]);
      fs.writeFileSync(path.join(caseRoot, 'memory-transport.js'), IPC_SOURCE);
      const workerOutput = path.join(caseRoot, 'worker-result.json');
      const env = { ...process.env }; delete env.NODE_OPTIONS; delete env.NODE_PATH;
      const args = [path.join(HERE, 'worker.mjs'), bundle, path.join(HERE, 'spec.json'), workerOutput];
      const child = childProcess.spawnSync(process.execPath, args, { cwd: caseRoot, env, encoding: 'utf8', windowsHide: true, timeout: 25000, maxBuffer: 2 * 1024 * 1024 });
      const ledgerRow = { role: label, executable: process.execPath, args, pid: child.pid, exitCode: child.status, signal: child.signal, error: child.error?.code ?? null,
        naturalExit: child.status !== null && child.signal === null && !child.error };
      processLedger.push(ledgerRow);
      fs.writeFileSync(path.join(caseRoot, 'stdout.log'), child.stdout ?? ''); fs.writeFileSync(path.join(caseRoot, 'stderr.log'), child.stderr ?? '');
      write(path.join(caseRoot, 'execution.json'), ledgerRow);
      need(ledgerRow.naturalExit && fs.existsSync(workerOutput), 'WORKER_NO_NATURAL_RECEIPT:' + label);
      const receipt = read(workerOutput);
      need(receipt.scenarioCount === spec.expectedScenarios.length && JSON.stringify(receipt.scenarios.map(row => row.id)) === JSON.stringify(spec.expectedScenarios), 'INCOMPLETE_SCENARIOS');
      need(receipt.scenarios.every(row => row.denied?.length === 0), 'UNEXPECTED_IPC_SWALLOWED');
      if (!fault) {
        need(child.status === 0 && receipt.status === 'PASS' && receipt.failedScenarios.length === 0, 'POSITIVE_FAILED');
        need(JSON.stringify(receipt.executedPublicFunctions) === JSON.stringify([...spec.runtimeFunctions].sort()), 'PUBLIC_FUNCTION_NOT_EXECUTED');
      } else {
        const target = receipt.failedScenarios.find(row => row.id === fault.expectedFailedScenario);
        need(child.status === 1 && receipt.status === 'FAIL' && target?.error.code === 'ERR_ASSERTION' && target.error.message.includes(fault.expectedAssertion), 'EXPECTED_BUSINESS_ASSERTION_NOT_CAUGHT:' + fault.id);
        need(receipt.failedScenarios.every(row => row.error.code === 'ERR_ASSERTION'), 'NEGATIVE_TOOL_OR_SETUP_FAILURE');
        write(path.join(caseRoot, 'actual-fault.json'), { ...fault, product: spec.product, originalSha256: sha(originalSource), mutatedSha256: hash(path.join(caseRoot, 'mutated-cpulock.ts')), currentPrivateSourceFault: true, historicalFailure: false, scenarioAssertionsChanged: false });
      }
      return { label, passed: true, expectedExitCode: fault ? 1 : 0, actualExitCode: child.status, naturalExit: true, scenarioCount: receipt.scenarioCount,
        scenarioPassCount: receipt.scenarios.filter(row => row.status === 'PASS').length, failedScenarios: receipt.failedScenarios,
        detectedDeliberateFault: Boolean(fault), workerReceipt: workerOutput, bundleSha256: hash(bundle), observerPreservedProgramBytes: true,
        observedCompilerInputCount: observed.size };
    }
    const positive = await execute('positive', null);
    const negative = [];
    for (const fault of spec.faults) negative.push(await execute('negative-' + fault.id, fault));
    for (const child of compilerProcesses) { child.ref(); child.stdin.end(); }
    let compilerExitDeadline;
    try { await Promise.race([Promise.all(compilerChildren), new Promise((_, reject) => { compilerExitDeadline = setTimeout(() => reject(new Error('CPULOCK_COMPILER_EOF_EXIT_TIMEOUT')), 5000); })]); }
    finally { clearTimeout(compilerExitDeadline); }
    compiler.stop();
    childProcess.spawn = spawnOriginal;
    need(processLedger.filter(row => row.role === 'esbuild-service').length === 1, 'COMPILER_CHILD_COUNT_CHANGED');
    need(processLedger.every(row => row.naturalExit), 'CHILD_DID_NOT_EXIT_NATURALLY');
    for (const row of [...sourceMap.values(), ...supportInputs, ...referenceInputs, ...fixtureInputs]) need(hash(row.actualPath) === row.sha256, 'SOURCE_DRIFT:' + row.path);
    for (const row of tool.files) need(hash(row.path) === row.sha256, 'TOOL_DRIFT:' + row.path);
    const actualDeclaredSources = [...new Set([...sourceMap.values(), ...supportInputs, ...referenceInputs].map(row => row.path))].sort();
    need(JSON.stringify(actualDeclaredSources) === JSON.stringify([...declaredSources.files].sort()), 'DECLARED_SOURCE_GRAPH_CHANGED_REVIEW_REQUIRED');
    const positiveMeta = read(path.join(root, 'positive', 'metafile.json'));
    const contributionRows = Object.values(positiveMeta.outputs).flatMap(output => Object.entries(output.inputs).map(([file, value]) => ({
      path: file, bytesInOutput: value.bytesInOutput, role: file.startsWith('node_modules/') ? 'tool-package-bundled-runtime' : file.startsWith('fixture:') ? 'external-memory-transport' : 'product-or-entry',
      executedFunctionCoverageInferred: false,
    })));
    write(path.join(root, 'bundle-contributions.json'), { inputs: contributionRows, note: 'Compiler parse inputs, emitted module contributions, and executed functions are distinct evidence scopes.' });
    const sourceChanges = [];
    write(path.join(root, 'process-ledger.json'), { processLedger });
    write(path.join(root, 'actual-source-inputs.json'), { product: [...sourceMap.values()], support: supportInputs, referenceOnly: referenceInputs, fixture: fixtureInputs });
    write(path.join(root, 'tool-authority.json'), authority);
    const outputRows = allFiles(root).map(file => ({ path: file, sha256: hash(file) }));
    const report = { schemaVersion: 1, id: ID, kind: 'actual-cpulock-ts-memory-ipc-consumer', status: 'PASS', passed: true, startedAt, finishedAt: new Date().toISOString(), repoRoot: repo,
      positive, negative, sourceChanges, sourceStableThroughEnd: true, fixtureInputs, toolInputs: tool.files,
      sourceHashes: [...sourceMap.values(), ...supportInputs, ...referenceInputs].map(({ path, sha256 }) => ({ path, sha256 })).concat(outputRows),
      processLedger, actualProductSourceInputs: [...sourceMap.values()], bundledToolAuthority: authority,
      evidence: { input: 'Source-derived fixtures; no user files, archive literals or hardware probes', consumer: 'Actual cpulock and its actual settingsRepository/yeman/api dependencies; only ipc.ts is substituted',
        state: 'Real bridge cache, queues, settings generation, normalization, owned patches and observer semantics', feedback: 'Original public function return/rejection, actual emitted IPC payloads and explicit memory receipts' },
      scope: 'behavioral-mock', productSourceModified: false, sharedRegistryModified: false, globalRegistryIntegrationExecuted: false,
      boundary: { realCpuWrites: 0, realRegistryCalls: 0, realSystemCommands: 0, networkActions: 0, nativeCpuImplementationExecuted: false,
        ymccHostManagerStarted: false, browserUiExecuted: false, hardwareEffectVerified: false, operatingSystemSleepPerformed: false,
        ipcTransport: 'memory only', nativeWriteSuccessProven: false, fullProductAcceptance: false },
      remainingGaps: ['Legacy bridge is no longer called by current page/App/autofloat per product comment; no current UI routing accepted',
        'Native registry.writePowerBatch implementation, CPU/power/OEM permission and hardware effects not executed',
        'cpulock applyPowerParams catch/false and readPowerParams-null branches are not reachable through current best-effort yeman failures; not artificially replaced to claim coverage',
        'settings native locking, disk durability, corruption recovery and real WebView lifecycle require separate tests',
        'No global registration, combined fan/UI regression, native/Host startup, deployment or physical sleep acceptance'], fullGoalComplete: false };
    write(path.join(root, 'result.json'), report);
    return { status: report.status, positiveScenarios: positive.scenarioCount, negativeFaults: negative.length, productSources: sourceMap.size, sourceHashes: report.sourceHashes.length,
      toolFiles: tool.files.length, compilerAndWorkerNaturalExits: processLedger.length, fullProductAcceptance: false };
  } catch (error) {
    write(path.join(root, 'failure.json'), { status: 'FAIL', code: error.message, stack: error.stack, processLedger, originalFailurePreserved: true });
    throw error;
  } finally {
    compiler?.stop(); childProcess.spawn = spawnOriginal;
  }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { console.log(JSON.stringify(await runConsumer(parseArgs(process.argv.slice(2))))); }
  catch (error) { console.error(error.stack ?? error.message); process.exitCode = 2; }
}
