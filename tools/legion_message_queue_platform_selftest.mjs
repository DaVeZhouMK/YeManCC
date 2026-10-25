import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const runner = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(runner), '..');
const workspace = path.resolve(repo, '../../..');
const root = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/MessageQueuePlatform');
const options = {};
for (let i = 2; i < process.argv.length; ++i) {
  assert(['--out', '--vcvars'].includes(process.argv[i]) && process.argv[i + 1], `Invalid option ${process.argv[i]}`);
  options[process.argv[i].slice(2)] = process.argv[++i];
}
const out = path.resolve(options.out || path.join(root,
  `${new Date().toISOString().replace(/[:.]/g, '-')}-${crypto.randomBytes(3).toString('hex')}`));
assert.equal(path.dirname(out).toLowerCase(), root.toLowerCase(), 'Use a fresh direct child of MessageQueuePlatform');
fs.mkdirSync(root, {recursive: true});
assert.equal(fs.realpathSync(root).toLowerCase(), path.resolve(root).toLowerCase(), 'Evidence root redirected');
fs.mkdirSync(out);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const rel = file => path.relative(out, file).replaceAll('\\', '/');
const json = (file, data) => fs.writeFileSync(path.join(out, file), JSON.stringify(data, null, 2) + '\n');
const receipt = {schema: 1, status: 'running', startedAt: new Date().toISOString(),
  scope: 'Real Win32 private thread queue and message-only hidden window, production clock header and exact extracted main timestamp bridge',
  realApis: ['PostThreadMessageW', 'PostMessageW', 'PeekMessageW', 'GetMessageW', 'GetMessageTime', 'GetTickCount64', 'DispatchMessageW'],
  inputs: [], configurations: [], mutations: [], hardwareIo: false, actualSleepWake: false, rawInputRegistered: false,
  sendInput: false, productionApplicationStarted: false, devicesSettingsServicesChanged: false, neptuneTouched: false,
  boundaries: [
    'WM_APP test messages are not evidence of real WM_INPUT/HID delivery, timing or firmware sample time',
    'Only the extracted main timestamp function is executed; route predicate is a fixture boolean',
    'Epoch cutover uses actual GetTickCount64 plus production belongsToEpoch, not complete main revoke/publish/atomic ownership execution',
    'Synthetic signed-bit arithmetic supplements actual OS observations; no forced OS uptime sign-bit transition or full32 wrap',
    'Same-ms pairs use 24 fixed trials; a missing true cross-cutover equality witness is reported, not retried into a pass',
    'Actual suspend/resume, hardware input, HC end-to-end output and sleep stability are not verified',
  ]};
const save = () => json('summary.json', receipt);
const run = (exe, args, log, cwd = out) => {
  const result = spawnSync(exe, args, {cwd, windowsHide: true, encoding: 'utf8', timeout: 45000,
    maxBuffer: 8 * 1024 * 1024});
  fs.mkdirSync(path.dirname(path.join(out, log)), {recursive: true});
  fs.writeFileSync(path.join(out, log), (result.stdout || '') + (result.stderr || ''));
  if (result.error) throw result.error;
  assert.equal(result.signal, null, `Signal while running ${log}`);
  return result;
};
const ok = (result, label) => { assert.equal(result.status, 0, `${label} failed; evidence ${out}`); return result.stdout; };
const safeBatchPath = file => {
  assert(!/[\r\n"%&|<>^!]/.test(file), 'Unsupported batch path characters'); return `"${file}"`;
};
const extractFunction = text => {
  const signature = 'static std::optional<uint64_t> legionMotionMessageReceiptTick()';
  const start = text.indexOf(signature);
  assert(start >= 0 && text.indexOf(signature, start + 1) < 0, 'Unique actual main bridge required');
  const open = text.indexOf('{', start); let depth = 0;
  for (let i = open; i < text.length; ++i) {
    if (text[i] === '{') ++depth;
    if (text[i] === '}' && --depth === 0) return {code: text.slice(start, i + 1),
      firstLine: text.slice(0, start).split('\n').length};
  }
  throw Error('Unbalanced main bridge');
};
save();
try {
  assert.equal(process.platform, 'win32'); assert.equal(process.arch, 'x64');
  const header = path.join(repo, 'native/legion_motion_message_clock.h');
  const main = path.join(repo, 'native/main.cpp');
  const statusHeader = path.join(repo, 'native/virtual_output_status.h');
  const fixtures = path.join(repo, 'tools/fixtures/legion_message_queue_platform');
  const sourceFiles = [runner, header, ...fs.readdirSync(fixtures).sort().map(name => path.join(fixtures, name))];
  for (const file of sourceFiles) {
    assert(fs.statSync(file).isFile()); const bytes = fs.readFileSync(file);
    const frozen = path.join(out, 'source', path.relative(repo, file));
    fs.mkdirSync(path.dirname(frozen), {recursive: true}); fs.writeFileSync(frozen, bytes);
    receipt.inputs.push({path: file, frozen: rel(frozen), bytes: bytes.length, sha256: sha(bytes)});
  }
  const mainBytes = fs.readFileSync(main), mainText = mainBytes.toString('utf8');
  const extracted = extractFunction(mainText);
  assert(extracted.code.includes('static_cast<uint32_t>(GetMessageTime())'));
  assert(extracted.code.includes('GetTickCount64(), queuedAt, ymcc::kVirtualOutputSourceWindowMaxAgeMs'));
  const constantPattern = /inline constexpr std::uint64_t kVirtualOutputSourceWindowMaxAgeMs\s*=\s*(\d+)\s*;/g;
  const constants = [...fs.readFileSync(statusHeader, 'utf8').matchAll(constantPattern)];
  assert.equal(constants.length, 1); const constant = constants[0][0];
  const epochPattern = /g_legionMotionEpochStartedAt\.store\(GetTickCount64\(\),\s*std::memory_order_release\);/g;
  const epochAnchors = [...mainText.matchAll(epochPattern)].map(match => ({code: match[0],
    firstLine: mainText.slice(0, match.index).split('\n').length}));
  assert(epochAnchors.length >= 2, 'Actual epoch cutover anchors missing');
  const wmInputAnchor = /case WM_INPUT:\s*\{\s*const auto legionMessageReceivedAt = legionMotionMessageReceiptTick\(\);/.exec(mainText);
  assert(wmInputAnchor, 'Actual WM_INPUT entry call missing');
  const generated = path.join(out, 'generated'); fs.mkdirSync(generated);
  const generatedHeader = path.join(generated, 'main_message_clock_bridge.h');
  fs.writeFileSync(generatedHeader, `#pragma once\nnamespace ymcc {\n${constant}\n}\n${extracted.code}\n`);
  receipt.sourceBinding = {main, mainSha256AtExtraction: sha(mainBytes), extractedFunction: {...extracted,
    sha256: sha(extracted.code)}, constant: {origin: statusHeader, code: constant, value: +constants[0][1], sha256: sha(constant)},
    wmInputEntry: {code: wmInputAnchor[0], firstLine: mainText.slice(0, wmInputAnchor.index).split('\n').length},
    epochAnchors, generatedHeader: rel(generatedHeader), generatedSha256: sha(fs.readFileSync(generatedHeader)),
    realWin32Imports: true, fixtureOnlyPredicate: 'inputCaptureLegionControllerMotionRoute'};
  json('source-bindings.json', receipt.sourceBinding);
  let vcvars = options.vcvars;
  if (!vcvars) {
    const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe');
    const install = ok(run(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
      '-property', 'installationPath'], 'compiler-discovery.log'), 'Compiler discovery').trim();
    assert(install && !/[\r\n]/.test(install)); vcvars = path.join(install, 'VC/Auxiliary/Build/vcvars64.bat');
  }
  assert(fs.existsSync(vcvars)); const setupLine = `call ${safeBatchPath(vcvars)} >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n`;
  const metadataCmd = path.join(out, 'compiler-metadata.cmd');
  // Never execute bare SET or capture the complete environment. Only compiler
  // path/version/SDK metadata is emitted. Compilation inherits setup in-process.
  fs.writeFileSync(metadataCmd, '@echo off\r\n' + setupLine +
    'where cl.exe\r\necho VCToolsVersion=%VCToolsVersion%\r\necho WindowsSDKVersion=%WindowsSDKVersion%\r\n');
  const metadata = ok(run(process.env.ComSpec || 'cmd.exe', ['/d', '/c', metadataCmd], 'compiler-metadata.log'), 'Compiler setup');
  const compiler = metadata.split(/\r?\n/).find(line => /\\cl\.exe$/i.test(line.trim()))?.trim();
  assert(compiler && fs.existsSync(compiler));
  receipt.compiler = {path: compiler, sha256: sha(fs.readFileSync(compiler)),
    vcToolsVersion: /VCToolsVersion=([^\r\n]+)/.exec(metadata)?.[1],
    windowsSdkVersion: /WindowsSDKVersion=([^\r\n]+)/.exec(metadata)?.[1], fullEnvironmentCaptured: false};
  const harness = path.join(out, 'source/tools/fixtures/legion_message_queue_platform/harness.cpp');
  const oldInclusiveRoot = path.join(out, 'mutations/epoch-cutover-old-inclusive');
  fs.mkdirSync(oldInclusiveRoot, {recursive: true});
  const originalHeader = fs.readFileSync(header, 'utf8');
  const strictExpression = 'receivedAt != 0 && receivedAt > epochStart';
  assert.equal(originalHeader.split(strictExpression).length, 2, 'Strict epoch production predicate required');
  const inclusiveHeader = originalHeader.replace(strictExpression, 'receivedAt != 0 && receivedAt >= epochStart');
  fs.writeFileSync(path.join(oldInclusiveRoot, 'legion_motion_message_clock.h'), inclusiveHeader);
  receipt.mutationSpec = {name: 'epoch-cutover-old-inclusive', headerSha256: sha(inclusiveHeader),
    before: strictExpression, after: 'receivedAt != 0 && receivedAt >= epochStart',
    expectedFailure: 'same-ms.ambiguous-old-and-new-both-rejected', compileFailureCountsAsKill: false};
  const variants = [{mutant: false, root: out, include: path.dirname(header)},
    {mutant: true, root: oldInclusiveRoot, include: oldInclusiveRoot}];
  for (const variant of variants) for (const [name, flags] of [['Release', ['/O2', '/DNDEBUG', '/MT']],
      ['Checked', ['/Od', '/RTC1', '/Zi', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd']]]) {
    const directory = path.join(variant.root, name); fs.mkdirSync(directory);
    const exe = path.join(directory, 'legion_message_queue_platform.exe');
    const arguments_ = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', '/WX', '/permissive-', '/showIncludes', ...flags,
      `/I${variant.include}`, `/I${generated}`, harness, `/Fo${path.join(directory, 'harness.obj')}`,
      `/Fd${path.join(directory, 'compiler.pdb')}`, `/Fe${exe}`, '/link', '/INCREMENTAL:NO', 'user32.lib'];
    const responseFile = path.join(directory, 'compile.rsp');
    fs.writeFileSync(responseFile, arguments_.map(argument => `"${argument}"`).join('\r\n') + '\r\n');
    const buildCmd = path.join(directory, 'compile.cmd');
    fs.writeFileSync(buildCmd, '@echo off\r\n' + setupLine + 'set CL=\r\nset _CL_=\r\nset LINK=\r\nset _LINK_=\r\n' +
      `${safeBatchPath(compiler)} @${safeBatchPath(responseFile)}\r\nexit /b %errorlevel%\r\n`);
    const buildLog = ok(run(process.env.ComSpec || 'cmd.exe', ['/d', '/c', buildCmd], `${rel(directory)}/compile.log`, directory), `${name} compile`);
    assert(buildLog.toLowerCase().includes(path.join(variant.include, 'legion_motion_message_clock.h').toLowerCase()), 'Selected production/mutant clock header not included');
    assert(buildLog.toLowerCase().includes(generatedHeader.toLowerCase()), 'Actual extracted main bridge not included');
    const result = run(exe, [], `${rel(directory)}/run.log`, directory);
    const summaryLines = [...result.stdout.matchAll(/^SUMMARY (.+)\r?$/gm)];
    assert.equal(summaryLines.length, 1, `${name} no normal summary; exit ${result.status}`);
    assert([0, 1].includes(result.status), `${name} infrastructure crash`);
    const summary = JSON.parse(summaryLines[0][1]);
    const observations = [...result.stdout.matchAll(/^OBS (.+)\r?$/gm)].map(match => JSON.parse(match[1]));
    const failures = [...result.stdout.matchAll(/^FAIL (.+)\r?$/gm)].map(match => JSON.parse(match[1]));
    const signs = [...result.stdout.matchAll(/^SIGN (.+)\r?$/gm)].map(match => JSON.parse(match[1]));
    assert.equal(observations.length, summary.observations); assert.equal(failures.length, summary.failures);
    assert.equal(result.status, summary.failures ? 1 : 0); assert(summary.cleanupComplete);
    assert.equal(observations.length, 89); assert.equal(signs.length, 4);
    const deltas = observations.map(observation => observation.queueMinusPostAfterLow32);
    const record = {name, exitCode: result.status, status: summary.failures ? 'platform-incompatibility-observed' : 'passed',
      ...summary, observations: observations.length, queueMinusPostAfterLow32: {min: Math.min(...deltas), max: Math.max(...deltas)},
      executableSha256: sha(fs.readFileSync(exe)), buildArguments: arguments_, failureIds: [...new Set(failures.map(f => f.id))]};
    if (variant.mutant) {
      assert.equal(result.status, 1, 'Old inclusive epoch mutant must fail semantically');
      assert(summary.sameActualCutoverPairs > 0, 'Actual same-tick OS witness required to kill the mutant');
      assert(record.failureIds.includes(receipt.mutationSpec.expectedFailure), 'Missing planned real queue counterexample');
      assert.equal(summary.sameActualCutoverRejectedPairs, 0, 'Old inclusive implementation must admit same-tick messages');
      assert.equal(summary.nextTickAllowed, 24, 'Mutant must complete all next-tick recovery cases');
      record.status = 'compiled-and-semantically-killed'; receipt.mutations.push(record);
    } else {
      assert.equal(summary.sameActualCutoverRejectedPairs, summary.sameActualCutoverPairs, 'Every ambiguous pair must be rejected');
      assert.equal(summary.nextTickAllowed, 24); receipt.configurations.push(record);
    }
    json(rel(path.join(directory, 'observations.json')), {summary, observations, failures, supplementalSigns: signs}); save();
    console.log(`${variant.mutant ? 'Old >= mutant ' : ''}${name}: ${record.status}; ${summary.checks} checks, ${summary.failures} failures, ${observations.length} actual OS observations; equal pairs rejected ${summary.sameActualCutoverRejectedPairs}/${summary.sameActualCutoverPairs}, next tick ${summary.nextTickAllowed}/24`);
  }
  for (const input of receipt.inputs) assert.equal(sha(fs.readFileSync(input.path)), input.sha256, `Input changed: ${input.path}`);
  const finalMainBytes = fs.readFileSync(main), finalMain = finalMainBytes.toString('utf8');
  assert.equal(sha(extractFunction(finalMain).code), receipt.sourceBinding.extractedFunction.sha256, 'Main clock bridge changed during test');
  const finalConstants = [...fs.readFileSync(statusHeader, 'utf8').matchAll(constantPattern)];
  assert.equal(finalConstants.length, 1); assert.equal(finalConstants[0][0], constant);
  receipt.mainWholeFileChangedElsewhere = sha(finalMainBytes) !== receipt.sourceBinding.mainSha256AtExtraction;
  receipt.sourceHashesRevalidated = true;
  assert.equal(receipt.mutations.length, 2, 'Old inclusive mutant must compile and be killed in both configurations');
  receipt.mutationCoverage = {mutants: 1, configurations: 2, compiledAndSemanticallyKilled: 2, actualQueueWitnessRequired: true};
  receipt.status = receipt.configurations.some(c => c.failures) ? 'platform-incompatibility-observed' : 'passed-with-stated-boundaries';
  receipt.completedAt = new Date().toISOString();
  receipt.sameMsWitness = receipt.configurations.every(c => c.sameActualCutoverPairs > 0) ? 'actual-equality-observed-in-both' : 'actual-equality-not-observed-in-every-build';
  receipt.osSignedBitWitness = receipt.configurations.some(c => c.negativeOsTimes > 0) ? 'negative-actual-OS-values-observed' : 'negative-actual-OS-values-not-observed-supplemental-arithmetic-only';
  save();
  const lines = receipt.configurations.map(c => `- ${c.name}: ${c.status}; ${c.checks} checks, ${c.failures} failures, ${c.observations} real observations; queue-post low32 delta ${c.queueMinusPostAfterLow32.min}..${c.queueMinusPostAfterLow32.max} ms; same queue-time pairs ${c.sameQueueTimePairs}/24, actual cutover equality pairs ${c.sameActualCutoverPairs}/24; ambiguous pairs rejected ${c.sameActualCutoverRejectedPairs}/${c.sameActualCutoverPairs}; next provably newer tick allowed ${c.nextTickAllowed}/24.`);
  fs.writeFileSync(path.join(out, 'REPORT.md'), '# Legion real Windows message queue clock boundary\n\n' +
    `Status: ${receipt.status}.\n\n${lines.join('\n')}\n\n` +
    'Both builds execute the real GetMessageTime/GetTickCount64 imports and the verbatim extracted main bridge, including its production 1500-ms window. The route predicate alone is fixture supplied. The actual production clock header is directly included, frozen, and rehashed. source-bindings.json preserves exact main function, constant, WM_INPUT call-site and epoch-store anchors. Main itself and its full epoch ownership path are not linked or executed.\n\n' +
    'Real cases cover hidden message-only window and private thread queue; PostMessage/PostThreadMessage; Peek without removal and delayed removal; GetMessage with an exclusive-consumer presence precondition; DispatchMessage; old/new queue items across an actual GetTickCount64 cutover; expiry after the production age window; 24 fixed same-ms trials; route gate. Every raw MSG.time, signed GetMessageTime, post/dequeue tick interval, wrapper optional result and epoch decision is retained in each observations.json. A broad-age diagnostic normalization is explicitly separate from the actual 1500-ms production wrapper.\n\n' +
    'Compatibility failures are retained and produce exit 1. No tolerance, clock offset correction, retry-until-pass, fabricated OS timestamp, or processing-time replacement is used. Strict queue timestamp containment within the sender GetTickCount64 interval is tested as a platform assumption, rather than asserted as a universal Windows guarantee.\n\n' +
    `Same-ms witness: ${receipt.sameMsWitness}. Signed OS witness: ${receipt.osSignedBitWitness}. Four signed bit patterns are supplemental memory/arithmetic cases only. Strict > in the production epoch predicate rejects both chronological sides when their actual ticks equal the cutover. The next provably newer actual OS tick recovers. The old >= implementation is compiled as an explicit mutant and must reproduce the real same-tick acceptance counterexample in both configurations.\n\n` +
    `Old-inclusive mutant: ${receipt.mutations.length}/2 builds compiled and semantically killed by the declared real-queue assertion; no compile error or crash counts as a kill.\n\n` +
    `Boundaries:\n${receipt.boundaries.map(boundary => `- ${boundary}`).join('\n')}\n\n` +
    'No RawInput registration, SendInput, device/settings/service operation, normal application startup, actual suspend/resume, or Neptune work. Private thread/window/event handles are cleaned up before normal summary output. Compiler setup emits only compiler location/version/SDK metadata, never a full environment dump.\n');
  console.log(JSON.stringify({status: receipt.status, evidence: out, configurations: receipt.configurations,
    sameMsWitness: receipt.sameMsWitness, osSignedBitWitness: receipt.osSignedBitWitness}, null, 2));
  if (receipt.status === 'platform-incompatibility-observed') process.exitCode = 1;
} catch (error) {
  receipt.status = 'failed-infrastructure-or-binding'; receipt.error = error.stack || String(error); save();
  console.error(receipt.error); console.error(`Evidence: ${out}`); process.exitCode = 2;
}