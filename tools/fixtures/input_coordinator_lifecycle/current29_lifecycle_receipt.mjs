import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {hash} from 'node:crypto';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
import {verifyCompiledSharedNativePower} from './native_power_peer.mjs';
import {terminal29CompileSourceProfile} from '../../input_coordinator_terminal29_source_profile.mjs';

const sha = bytes => hash('sha256', bytes);
const key = file => path.resolve(file).toLowerCase();
const proofs = new WeakMap();

const contractId = 'current105-terminal29-shared-input-power-v1';
const pins = {
  tupleGenerated: '9581059315d42a86e573e80a788b0fb0157caf7d06ac20c7c100eb236b7ca930',
  recoveryBody: '0d49af1d7d1f588ea0be9e22acd83d62a1d0f5514dc1e738296bb7a96f99a4fa',
  switchBody: 'bb874bfd1fc0505467d92a83d1188b23867992a4b80619c8d49ef1e2eebde36d',
};

function readPin(row, expected) {
  assert(row && path.isAbsolute(row.path) && /^[a-f0-9]{64}$/.test(expected) && row.sha256 === expected, 'CURRENT29_DECLARED_PIN');
  assert.equal(key(fs.realpathSync.native(row.path)), key(row.path), 'CURRENT29_PLAIN_INPUT');
  assert(fs.lstatSync(row.path).isFile(), 'CURRENT29_REGULAR_INPUT');
  const bytes = fs.readFileSync(row.path);
  assert.equal(sha(bytes), expected, 'CURRENT29_ACTUAL_PIN');
  return bytes;
}

function originalSwitchBody(n, source, layout, compiled) {
  const relative = 'native/virtual_controller_ui_output_runtime.h';
  const rows = source.records.filter(row => row.path === relative);
  assert.equal(rows.length, 1, 'CURRENT29_RADIAL_SOURCE');
  const row = rows[0];
  assert.equal(key(row.original), key(path.join(layout.sourceRoot, relative)));
  const header = readPin({path: row.original, sha256: row.sha256}, row.sha256);
  const radial = extract(header.toString('utf8'), 'radialInputIsolationOwnership').code;
  assert.equal(extract(compiled.get('main.inert.cpp').bytes.toString('utf8'), 'radialInputIsolationOwnership').code,
    radial, 'CURRENT29_RADIAL_COMPILED_SOURCE');
  const body = extract(compiled.get('fixture.cpp').bytes.toString('utf8'), 'switchRecoveryCases').code;
  if (radial.startsWith('static void radialInputIsolationOwnership(bool active) {')) return body;
  assert(radial.startsWith('static void radialInputIsolationOwnership(bool active,const ymcc::sleeppower::SharedPowerV1& power) {'),
    'CURRENT29_RADIAL_SOURCE_DECLARATION');
  assert.equal(n.generationPreflight?.radialFixturePower?.headerSha256, row.sha256, 'CURRENT29_RADIAL_PREFLIGHT_SOURCE');
  assert.equal(n.generationPreflight.radialFixturePower.relative, relative);
  assert.equal(n.generationPreflight.radialFixturePower.usesSharedPower, true);
  const observed = 'CachedRadialOwnerScope(){ymcc::sleeppower::SharedPowerV1 power; if(!captureSharedPowerLifecycle(power))throw std::logic_error("foreground-neutral-public-power-capture-failed"); radialInputIsolationOwnership(true,power);}';
  const original = 'CachedRadialOwnerScope(){radialInputIsolationOwnership(true);}';
  assert.equal(body.split(observed).length, 2, 'CURRENT29_EXACT_RADIAL_FIXTURE_CALL');
  const inverse = body.replace(observed, () => original);
  assert.equal(inverse.split(original).length, 2, 'CURRENT29_UNIQUE_ORIGINAL_RADIAL_FIXTURE_CALL');
  return inverse;
}

export function bindCurrent29LifecycleCandidate({n, source}) {
  assert(n.role === 'positive' && n.mutation === null && n.status === 'NATIVE29_COMPILED_NOT_RUNTIME_ACCEPTED' &&
    n.managedInputContract === 'current105-terminal29-shared-input-power-v1' && n.batchAfter?.status === 'AUTHORITY_STABLE',
    'CURRENT29_POSITIVE_COMPILATION_REQUIRED');
  assert.deepEqual(source, JSON.parse(readPin(n.native.sourceManifest, n.native.sourceManifest.sha256)), 'CURRENT29_SOURCE_MANIFEST');
  const ledger = JSON.parse(readPin(n.compilerLedger, n.compilerLedger.sha256));
  assert.equal(ledger.status, 'ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED');
  assert.deepEqual(ledger.sourceManifest, n.native.sourceManifest);
  assert.deepEqual(ledger.buildScript, n.native.buildScript);
  readPin(n.native.exe, n.native.exe.sha256);
  const preparation = JSON.parse(readPin(n.preparation, n.preparation.sha256));
  assert.equal(preparation.status, 'MANAGED29_COMPILED_NOT_RUNTIME_ACCEPTED');
  const layout = JSON.parse(readPin(preparation.layout, preparation.layout.sha256));
  assert.equal(layout.managedInputContract, contractId);
  assert.equal(key(layout.sourceRoot), key(n.sourceRoot));
  assert.equal(key(source.records.find(row => row.path === 'native/main.cpp').original), key(path.join(layout.sourceRoot, 'native/main.cpp')));
  const candidateMain = layout.productMainSha256;
  const profile = terminal29CompileSourceProfile(layout.sourceRoot);
  const rawPipe = profile.inputs.find(row => row.relative === 'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc');
  assert(rawPipe, 'CURRENT29_PIPE_SOURCE_PROFILE');
  for (const [file, digest] of [['native/main.cpp', candidateMain],
    [profile.fixture.relative, profile.fixture.sha256], [rawPipe.relative, rawPipe.sha256]]) {
    const matches = source.records.filter(row => row.path === file);
    assert.equal(matches.length, 1, 'CURRENT29_ORIGINAL_SOURCE');
    assert.equal(key(matches[0].original), key(path.join(layout.sourceRoot,file)), 'CURRENT29_CAPTURED_ORIGINAL_SOURCE');
    for (const p of [matches[0].original, matches[0].snapshot]) readPin({path: p, sha256: matches[0].sha256}, digest);
  }
  const compiled = new Map();
  const generatedPipe = n.generationPreflight?.generatedOverrides?.filter(row=>row.relative===rawPipe.relative);
  assert.equal(generatedPipe?.length,1,'CURRENT29_PIPE_STATIC_PREFLIGHT');
  for (const name of ['fixture.cpp', 'main.inert.cpp', 'tuple_recovery_cases.inc', 'pipe_cases.inc']) {
    const matches = source.compiledGenerated.filter(row => path.basename(row.path) === name);
    assert.equal(matches.length, 1, 'CURRENT29_GENERATED_INPUT');
    const observed = ledger.inputs.filter(row => key(row.path) === key(matches[0].path));
    assert.equal(observed.length, 1, 'CURRENT29_GENERATED_COMPILER_INPUT');
    assert.equal(observed[0].sha256, matches[0].sha256, 'CURRENT29_COMPILER_INPUT_PIN');
    const expected = name === 'tuple_recovery_cases.inc' ? pins.tupleGenerated : name === 'pipe_cases.inc' ? generatedPipe[0].sha256 : matches[0].sha256;
    compiled.set(name, {file: matches[0].path, bytes: readPin(matches[0], expected)});
  }
  const fixture = compiled.get('fixture.cpp');
  assert.equal(ledger.explicitTranslationUnits.filter(file => key(file) === key(fixture.file)).length, 1, 'CURRENT29_FIXTURE_TU');
  const publicPower = verifyCompiledSharedNativePower({manifest: source, buildRoot: path.dirname(fixture.file),
    sourceRoot: layout.sourceRoot, managedInputContract: n.managedInputContract});
  assert.deepEqual(ledger.sharedNativePowerGeneration, publicPower, 'CURRENT29_SHARED_POWER_COMPILER_INPUTS');
  assert.equal(sha(extract(fixture.bytes.toString('utf8'), 'recoveryProtectionCases').code), pins.recoveryBody, 'CURRENT29_RECOVERY_BODY');
  assert.equal(sha(originalSwitchBody(n, source, layout, compiled)), pins.switchBody, 'CURRENT29_SWITCH_BODY');
  const proof = Object.freeze({});
  proofs.set(proof, sha(JSON.stringify(source)));
  return proof;
}

// These digests cover the original C416 occurrence order and each assertion.
const modes = {
  'tuple-protection': {cases: 45, checks: 1347, peers: 63,
    caseSha: '075690b3a08546dac47fcd2d2c3f8012618f78e472b18f8bffc6660af20a2d9d',
    traceSha: 'fd12e587e6f372135c7b484efd81edcb5af42c577431538dd9da4936954dafd3'},
  'recovery-protection': {cases: 25, checks: 249, peers: 0,
    caseSha: '53baba61ec6b2a804afdde68075442d36d03e9a2c127ba6a1394b35b848ae4db',
    traceSha: '2cb0366ebaf1396e7182f3935dcfb7edd11e0bc213fe4b0ceac624f8d2857aff'},
  'switch-recovery': {cases: 98, checks: 1727, peers: 103,
    caseSha: '4a0cb9eda7098ba17fbd4e582f40de5a855510d5857db3b64d862cea3414c145',
    traceSha: '9b357175705574884409c1da71382d0fa60c9b7dfe2d506037ccd2ee666cf21b'},
};

export function verifyCurrent29CandidateLifecycle(mode, raw, proof, source) {
  assert(proofs.has(proof) && proofs.get(proof) === sha(JSON.stringify(source)), 'CURRENT29_LIVE_SOURCE_PROOF');
  assert(Object.hasOwn(modes, mode), 'CURRENT29_MODE_NOT_PROVEN');
  const contract = modes[mode];
  assert.equal(raw.status, 'PASS');
  assert.equal(raw.forbiddenIo, 0);
  assert.equal(raw.fullTranslationUnit, true);
  assert.equal(raw.normalProductMainExecuted, false);
  assert.equal(raw.realDeviceIo, false);
  assert.deepEqual(raw.failures, []);
  assert.equal(raw.cases.length, contract.cases);
  assert.equal(raw.checks, contract.checks);
  assert.equal(raw.assertionTrace.length, raw.checks);
  assert.equal(sha(JSON.stringify(raw.cases.map(({name, sourceOrdinal, nameOccurrence}) =>
    [name, sourceOrdinal, nameOccurrence]))), contract.caseSha, 'CURRENT29_ORIGINAL_CASE_IDENTITIES');
  assert.equal(sha(JSON.stringify(raw.assertionTrace)), contract.traceSha, 'CURRENT29_ORIGINAL_ASSERTION_TRACE');
  assert.equal(raw.nativePrivatePeers.length, contract.peers, 'CURRENT29_ORIGINAL_PEER_COUNT');
  const peers = new Set();
  for (const child of raw.nativePrivatePeers) {
    assert(Number.isSafeInteger(child.pid) && child.pid > 0 && /^[1-9][0-9]+$/.test(child.birth100ns), 'CURRENT29_PEER_IDENTITY');
    assert(child.exactCreatedIdentity === true && child.controlEventClosed === true &&
      child.processObserverClosed === true && child.exitKnown === true, 'CURRENT29_PEER_CLOSE');
    assert.equal(child.wait, 0);
    assert.equal(child.exitCode, 0);
    const identity = JSON.stringify([child.pid, child.birth100ns]);
    assert(!peers.has(identity), 'CURRENT29_DUPLICATE_PEER');
    peers.add(identity);
  }
  return {cases: contract.cases, checks: contract.checks, privatePeers: contract.peers, contract: 'C490_EXACT_ORIGINAL_TRACE'};
}
