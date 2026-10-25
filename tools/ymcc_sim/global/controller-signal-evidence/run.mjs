import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {REPO, sourceInputs, toolInputs, historicalInputs} from './adapter.mjs';
import {hashFile, validateReceipts} from './receipts.mjs';
import {consumerEnvironment} from './environment.mjs';
import {createOwnedWorkspace} from './owned-workspace.mjs';

const out = path.resolve(process.argv[2] ?? '');
if (!process.argv[2] || !path.isAbsolute(process.argv[2])) throw Error('ABSOLUTE_CASE_OUTPUT_REQUIRED');
fs.mkdirSync(out, {recursive: true});
const result = {schemaVersion: 1, pass: false, status: 'FAIL', startedAt: new Date().toISOString(),
  scope: 'actual-source diagnostic consumers with explicit OS/input leaves; private real kernel wait/close where declared',
  physicalHardwareAcceptance: false, installedApplicationExercised: false,
  callbacksDrainedProven: false, physicalSuppressionProven: false, gameConsumptionProven: false,
  persistentLifecycleMeasured: false, cases: []};
try {
  const sources = Object.fromEntries(sourceInputs.map(relative => [relative, hashFile(path.join(REPO, relative))]));
  result.sourceHashes = sources;
  const tools = toolInputs(REPO);
  const history = historicalInputs(REPO);
  result.historicalInputs = history;
  const verifyTools = () => {
    for (const row of tools.files) if (hashFile(row.path) !== row.sha256) throw Error('CONTROLLER_DIAGNOSTIC_TOOL_CHANGED:' + row.path);
  };
  verifyTools();
  result.tools = tools.files;
  // The runner owns all output locations and tool overrides. Inherited diagnostic
  // overrides may not redirect a global run into another checkout or toolchain.
  const workspace = createOwnedWorkspace({repo:REPO, output:out, primary:tools.primary, sourceHashes:sources});
  const owned = workspace.allocate('acquisition-and-rejection');
  result.executionWorkspace = owned;
  const env = {...consumerEnvironment({ambient: process.env, tools, out, nodeExecutable: process.execPath}), TEMP:owned.temp, TMP:owned.temp};
  fs.mkdirSync(env.TEMP, {recursive: true});
  if (fs.existsSync(env.YMCC_MSI_RAW_OUT)) throw Error('FRESH_CONSUMER_OUTPUT_REQUIRED');
  const child = spawnSync(process.execPath, [path.join(REPO, 'tools/msi_external_raw_receipts_selftest.mjs')], {
    cwd: REPO, env, encoding: 'utf8', windowsHide: true, timeout: 1800000, maxBuffer: 4 * 1024 * 1024});
  fs.writeFileSync(path.join(out, 'consumer-stdout.log'), child.stdout ?? '');
  fs.writeFileSync(path.join(out, 'consumer-stderr.log'), child.stderr ?? '');
  result.consumerExitCode = child.status;
  if (child.error || child.signal || child.status !== 0) throw Error('DIAGNOSTIC_CONSUMER_FAILED:' + (child.error?.message ?? child.signal ?? child.status));
  Object.assign(result, validateReceipts({repo: REPO, output: path.join(out, 'consumers'), sourceHashes: sources, tools}));
  const bindingFile = path.join(out, 'source-inputs.json');
  fs.writeFileSync(bindingFile, JSON.stringify(sources, null, 2) + '\n');
  const rejection = spawnSync(process.execPath, ['--test', path.join(REPO, 'tools/ymcc_sim/global/controller-signal-evidence/receipts.test.mjs')], {
    cwd: REPO, env: {...env, YMCC_SIGNAL_EVIDENCE_RECEIPTS: path.join(out, 'consumers'), YMCC_SIGNAL_EVIDENCE_SOURCE_FILE: bindingFile},
    encoding: 'utf8', windowsHide: true, timeout: 120000, maxBuffer: 2 * 1024 * 1024});
  fs.writeFileSync(path.join(out, 'receipt-rejection-tests.log'), (rejection.stdout ?? '') + (rejection.stderr ?? ''));
  if (rejection.error || rejection.signal || rejection.status !== 0) throw Error('DIAGNOSTIC_RECEIPT_REJECTION_TEST_FAILED');
  result.receiptRejectionTestsPassed = true;
  for (const [relative, sha256] of Object.entries(sources))
    if (hashFile(path.join(REPO, relative)) !== sha256) throw Error('DIAGNOSTIC_SOURCE_CHANGED:' + relative);
  verifyTools();
  historicalInputs(REPO);
  workspace.verify(owned);
  result.pass = true; result.status = 'PASS';
} catch (error) { result.error = error.message; }
result.finishedAt = new Date().toISOString();
fs.writeFileSync(path.join(out, 'diagnostic-result.json'), JSON.stringify(result, null, 2) + '\n');
console.log(result.status + ': controller diagnostic consumers; persistent lifecycle and physical input remain unaccepted');
if (!result.pass) {console.error(result.error); process.exitCode = 1;}