/** Finite ownership-data refusal controls. Fixture source is inert text, never
 * imported or compiled; scratch data stays in the G core workspace.
 */
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import test from 'node:test';
import {
  inspectOwnershipBinding, validateOwnershipCatalog, queryOwnershipCatalog,
  getOwnershipInventory, getOwnershipPlan,
} from './ownership-catalog.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(HERE, '../../../..');
const workspace = path.resolve(repoRoot, '../../../Testing/YMCC/Workspace/OwnershipCatalog-20261023');
fs.mkdirSync(workspace, {recursive: true});
const scratch = fs.mkdtempSync(path.join(workspace, 'refusal-'));
const hash = text => createHash('sha256').update(text).digest('hex');
const body = `void retire() {
  const auto marker = R"tag(} {)tag";
  const char brace = '}';
  const int budget = 1'000;
  /* } must not end the selected owner body. */
  if (budget) { restoreOwned(); }
  revoke();
}`;
function fixture(name) {
  const root = path.join(scratch, name);
  fs.mkdirSync(path.join(root, 'native'), {recursive: true});
  const file = path.join(root, 'native/owner.h');
  fs.writeFileSync(file, '// unrelated prefix\n' + body + '\nvoid other() {}\n');
  const data = {
    schemaVersion: 1,
    bindings: {owner: {path: 'native/owner.h', selector: {kind: 'braced', anchor: 'void retire() {'}, sha256: hash(body)}},
    features: [{
      id: 'fixture.owner', scope: 'confirmed-enabled', persistence: 'persistent',
      rationale: 'A retained source owner must restore and revoke across events; this is only an inert binder fixture.',
      bindingIds: ['owner'],
      anchors: [
        {id: 'owner', bindingId: 'owner', symbol: 'revoke()', role: 'owner'},
        {id: 'consumer', bindingId: 'owner', symbol: 'restoreOwned()', role: 'consumer'},
      ],
      resources: [{kind: 'leases', ownerAnchor: 'owner', lifetime: 'cross-event', groupId: 'fixture.retained', responsibility: 'Own the retained lease until exact retirement.'}],
      lifecycle: {exit: ['owner'], sleep: [], faultRecovery: ['consumer']},
      components: [{id: 'fixture.transaction', persistence: 'one-shot', anchor: 'consumer', reason: 'A finite call inside a retained owner.'}],
      testReferences: ['fixture.registered-check'], gaps: ['NOT_EXECUTED'],
    }],
  };
  const query = () => getOwnershipPlan({repoRoot: root, catalog: data, registeredTestIds: ['fixture.registered-check']});
  return {root, file, data, query};
}
function refuses(result, status = 'STALE') {
  assert.equal(result.features[0].status, status);
  assert.equal(result.features[0].persistence, 'unknown');
  assert.deepEqual(result.features[0].registeredTestIds, []);
  assert.equal(result.executedTests, 0);
  assert.equal(result.softwareAcceptance, false);
  assert.equal(result.hardwareAcceptance, false);
  assert.equal(result.cpuPowerAcceptance, false);
  assert.equal(result.checksToConsider[0].automaticExecution, false);
}

test('current eight enabled owners are source-bound inventory data, with no execution acceptance', () => {
  const result = getOwnershipInventory({repoRoot});
  const scope = JSON.parse(fs.readFileSync(path.join(repoRoot, 'capabilities/ACTIVE-INTEGRATION-SCOPE.json'), 'utf8'));
  assert.deepEqual(result.features.map(f => f.id).sort(), scope.confirmedEnabledCapabilities.map(f => f.id).sort());
  assert.equal(result.features.length, 8);
  assert.deepEqual(result.diagnostics, []);
  for (const feature of result.features) {
    assert.equal(feature.status, 'CLASSIFIED_SOURCE_BOUND', feature.id + JSON.stringify(feature.gaps));
    assert.equal(feature.persistence, 'persistent', feature.id);
    assert.equal(feature.executionAcceptance, false);
  }
  assert.equal(result.executedTests, 0);
  assert.equal(result.automaticExecution, false);
  assert.equal(result.softwareAcceptance, false);
  assert.equal(result.hardwareAcceptance, false);
  assert.equal(result.cpuPowerAcceptance, false);
  assert.equal(result.otherCapabilities, 'UNCLASSIFIED_NOT_ACCEPTED');
});

test('inventory/plan consume supplied routing IDs without treating registration as a receipt', () => {
  const result = getOwnershipPlan({repoRoot, ids: ['fan'], adapters: [{id: 'fan.unified-capability-dispatch'}]});
  assert.deepEqual(result.features[0].registeredTestIds, ['fan.unified-capability-dispatch']);
  assert.ok(result.features[0].unregisteredTestIds.includes('fan.lifecycle-wake-display'));
  assert.equal(result.registration.executionEvidence, false);
  assert.equal(result.features[0].executionAcceptance, false);
  assert.equal(result.checksToConsider[0].automaticExecution, false);
});

test('unclassified capabilities remain UNKNOWN and dormant/excluded routes stay INACTIVE', () => {
  const result = queryOwnershipCatalog({repoRoot, ids: ['not-reviewed', 'model.secondary-display', 'settings.wake-password-ui', 'controller.experimental-native-rear-keyboard-writer']});
  assert.deepEqual(result.features.map(f => f.status), ['UNKNOWN', 'INACTIVE', 'INACTIVE', 'INACTIVE']);
  for (const feature of result.features) {
    assert.equal(feature.persistence, 'unknown');
    assert.equal(feature.active, false);
    assert.deepEqual(feature.registeredTestIds, []);
  }
});

test('binds the complete owner body despite raw strings/comments; unrelated main text is outside the binding', () => {
  const f = fixture('complete-body');
  assert.equal(inspectOwnershipBinding(f.root, f.data.bindings.owner).text, body);
  assert.equal(f.query().features[0].status, 'CLASSIFIED_SOURCE_BOUND');
  fs.writeFileSync(f.file, '// changed unrelated prefix\nvoid different() {}\n' + body + '\n');
  assert.equal(f.query().features[0].status, 'CLASSIFIED_SOURCE_BOUND');
});

test('changing consumed owner semantics with the same signature yields STALE and unknown components', () => {
  const f = fixture('source-drift');
  fs.writeFileSync(f.file, body.replace('restoreOwned();', 'forgetOwned();'));
  const result = f.query();
  refuses(result);
  assert.deepEqual(result.features[0].staleBindings, ['owner']);
  assert.equal(result.features[0].components[0].persistence, 'unknown');
  assert.notEqual(result.features[0].sourceBindings[0].sha256, result.features[0].sourceBindings[0].observedSha256);
});

test('missing or ambiguous original anchors refuse source classification', () => {
  const f = fixture('anchor-refusal');
  fs.writeFileSync(f.file, body.replace('void retire()', 'void renamed()'));
  refuses(f.query());
  fs.writeFileSync(f.file, body + '\n' + body);
  refuses(f.query());
});

test('missing classification and unsupported classification are UNKNOWN, never implicit one-shot', () => {
  const f = fixture('unknown-classification');
  delete f.data.features[0].persistence;
  assert.throws(() => validateOwnershipCatalog(f.data), /CLASSIFICATION_REQUIRED/);
  refuses(f.query(), 'UNKNOWN');
  f.data.features[0].persistence = 'assume-one-shot';
  refuses(f.query(), 'UNKNOWN');
  f.data.features[0].persistence = 'unknown';
  refuses(f.query(), 'UNKNOWN');
});

test('one-shot cannot hide a retained lease or resident timer', () => {
  const f = fixture('mislabel-one-shot');
  f.data.features[0].persistence = 'one-shot';
  assert.throws(() => validateOwnershipCatalog(f.data), /ONE_SHOT_PERSISTENT_RESOURCE/);
  refuses(f.query(), 'UNKNOWN');
  f.data.features[0].resources[0].lifetime = 'operation';
  f.data.features[0].resources[0].kind = 'periodic-timers';
  assert.throws(() => validateOwnershipCatalog(f.data), /ONE_SHOT_PERSISTENT_RESOURCE/);
});

test('JSON consumer-row changes stale the binding; unrelated row formatting does not', () => {
  const f = fixture('json-drift');
  const file = path.join(f.root, 'catalog.json');
  fs.writeFileSync(file, JSON.stringify({domains: [{id: 'enabled', consumer: 'owner-v1'}, {id: 'other', consumer: 'unused'}]}));
  const binding = {path: 'catalog.json', selector: {kind: 'json-id', collection: '/domains', id: 'enabled'}};
  const initial = inspectOwnershipBinding(f.root, binding).sha256;
  fs.writeFileSync(file, JSON.stringify({domains: [{consumer: 'owner-v1', id: 'enabled'}, {id: 'other', consumer: 'changed'}]}, null, 2));
  assert.equal(inspectOwnershipBinding(f.root, binding).sha256, initial);
  fs.writeFileSync(file, JSON.stringify({domains: [{id: 'enabled', consumer: 'owner-v2'}]}));
  assert.notEqual(inspectOwnershipBinding(f.root, binding).sha256, initial);
  f.data.bindings.catalog = {...binding, sha256: initial};
  f.data.features[0].bindingIds.push('catalog');
  refuses(f.query());
});

test('bindings cannot escape the repository or point an owner to a missing anchor', () => {
  const f = fixture('source-boundary');
  f.data.bindings.owner.path = '../outside.h';
  assert.throws(() => validateOwnershipCatalog(f.data), /BINDING_IDENTITY/);
  refuses(f.query(), 'UNKNOWN');
  f.data.bindings.owner.path = 'native/owner.h';
  f.data.features[0].resources[0].ownerAnchor = 'nonexistent';
  assert.throws(() => validateOwnershipCatalog(f.data), /RESOURCE_OWNER/);
  refuses(f.query(), 'UNKNOWN');
});


test('explicit reviewed LF region identity tolerates CRLF checkout but still rejects changed owner code', () => {
 const f=fixture('explicit-lf-owner');f.data.bindings.owner.lineEndings='lf';
 const first=f.query();assert.equal(first.features[0].status,'CLASSIFIED_SOURCE_BOUND');const rawBefore=first.features[0].sourceBindings[0].observedRawFileSha256;
 fs.writeFileSync(f.file,fs.readFileSync(f.file,'utf8').replaceAll('\n','\r\n'));
 const converted=f.query();assert.equal(converted.features[0].status,'CLASSIFIED_SOURCE_BOUND');assert.notEqual(converted.features[0].sourceBindings[0].observedRawFileSha256,rawBefore);assert.equal(converted.features[0].sourceBindings[0].sourceNormalization,'lf-source-region-v1');assert.equal(converted.executedTests,0);assert.equal(converted.softwareAcceptance,false);
 fs.writeFileSync(f.file,fs.readFileSync(f.file,'utf8').replace('restoreOwned();','keepOwned();'));refuses(f.query());
});
test('line ending normalization cannot widen an entire file or accept an unknown normalization', () => {
 const f=fixture('invalid-normalization');f.data.bindings.owner.lineEndings='lf';f.data.bindings.owner.selector={kind:'file'};assert.throws(()=>validateOwnershipCatalog(f.data),/SOURCE_TEXT_NORMALIZATION/);
 f.data.bindings.owner.selector={kind:'braced',anchor:'void retire() {'};f.data.bindings.owner.lineEndings='strip-whitespace';assert.throws(()=>validateOwnershipCatalog(f.data),/SOURCE_TEXT_NORMALIZATION/);
});
