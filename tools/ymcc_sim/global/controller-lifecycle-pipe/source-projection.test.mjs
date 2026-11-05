import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import { generateWireInput, WIRE_FAULTS, mutateWireSource, wireSourceProjection } from './source-projection.mjs';

// Only pure generation against real source text: never import or run the producer.
const mainFile = new URL('../../../../native/main.cpp', import.meta.url);
const templateFile = new URL('../../../fixtures/input_lifecycle_integration/NativeEntry.cpp', import.meta.url);
const main = fs.readFileSync(mainFile, 'utf8');
const template = fs.readFileSync(templateFile, 'utf8');
const sha = text => crypto.createHash('sha256').update(text).digest('hex');
let generated;
const original = () => generated ??= generateWireInput(main, template);
const replaceOne = (source, before, after) => {
  assert.equal(source.split(before).length, 2, 'test edit needs one actual anchor: ' + before);
  return source.replace(before, after);
};

test('wire projection binds actual positive and both fixed mutation generated inputs', () => {
  assert.deepEqual(WIRE_FAULTS.map(({ id, expected }) => ({ id, expected })), [
    { id: 'sequence-sync-omitted', expected: 'business-refusal-sequence-synced-without-transport-fault' },
    { id: 'instance-fence-omitted', expected: 'stale-Host-instance-cannot-accept-ACK' }
  ]);
  const result = wireSourceProjection(main, template);
  assert.equal(result.schemaVersion, 1);
  assert.equal(result.kind, 'wire-generated-source-projection-v1');
  assert.equal(result.variants.length, 3);
  assert.deepEqual(result.variants.map(v => v.id), ['positive', ...WIRE_FAULTS.map(f => f.id)]);
  assert.equal(new Set(result.variants.map(v => v.sha256)).size, 3);
  assert.equal(original().cpp.includes('@@REQUEST@@'), false);
  assert(Array.isArray(original().bindings) && original().bindings.length > 0);
  for (const [index, variant] of result.variants.entries()) {
    const fault = WIRE_FAULTS[index - 1];
    const source = fault ? mutateWireSource(main, fault.id) : main;
    const { cpp } = generateWireInput(source, template);
    assert.equal(variant.sha256, sha(cpp), variant.id);
    assert.equal(variant.bytes, Buffer.byteLength(cpp), variant.id);
    assert.equal(variant.expectedRejection, fault?.expected ?? null);
    if (fault) {
      assert.notEqual(source, main);
      assert.equal(source, replaceOne(main, fault.before, fault.after));
      assert.notEqual(cpp, original().cpp);
      assert.equal(source.replace(fault.after, fault.before), main, 'inverse mutation restores actual source');
    }
  }
});

test('wire projection ignores an unrelated appended CPU CCD marker', () => {
  const changed = main + '\n// unrelated CPU/CCD topology marker: wire projection control only\n';
  assert.notEqual(sha(changed), sha(main));
  assert.equal(generateWireInput(changed, template).cpp, original().cpp);
  assert.deepEqual(wireSourceProjection(changed, template), wireSourceProjection(main, template));
});

test('wire projection changes for an internal actual request edit', () => {
  const anchor = 'if (advanceSequence && hostConsumed) g_inputHostSequence = sequenceValue;';
  const changed = replaceOne(main, anchor, anchor + ' /* request-internal projection sensitivity */');
  assert.notEqual(generateWireInput(changed, template).cpp, original().cpp);
  const before = wireSourceProjection(main, template), after = wireSourceProjection(changed, template);
  assert.notEqual(after.variants[0].sha256, before.variants[0].sha256);
});

test('wire projection changes every variant when the real template changes', () => {
  const changed = template + '\n// template projection sensitivity\n';
  const before = wireSourceProjection(main, template), after = wireSourceProjection(main, changed);
  for (let i = 0; i < before.variants.length; i++) {
    assert.notEqual(after.variants[i].sha256, before.variants[i].sha256);
    assert(after.variants[i].bytes > before.variants[i].bytes);
  }
});

const required = [
  { name: 'request function', anchor: 'static bool inputHostRequestPreparedLocked(', replacement: 'static bool missingRequestForProjection(', duplicate: () => original().replacements.REQUEST },
  { name: 'new-id function', anchor: () => original().replacements.NEW_ID, replacement: '/* actual new-id definition removed for rejection control */', duplicate: () => original().replacements.NEW_ID },
  { name: 'W2U pointer overload', anchor: 'static std::string W2U(const wchar_t*', replacement: 'static std::string missingPointerW2U(const wchar_t*', duplicate: () => 'static std::string W2U(const wchar_t* w) { return {}; }' },
  { name: 'W2U wide overload', anchor: 'static std::string W2U(const std::wstring& w)', replacement: 'static std::string missingWideW2U(const std::wstring& w)', duplicate: () => 'static std::string W2U(const std::wstring& w) { return {}; }' },
  { name: 'protocol constant', anchor: 'static constexpr const char* kInputHostProtocolConfigHash', replacement: 'static constexpr const char* missingProtocolHash', duplicate: () => 'static constexpr const char* kInputHostProtocolConfigHash = "duplicate";' },
  { name: 'receipt namespace', anchor: 'namespace c7aReceiptFast {', replacement: 'namespace missingReceiptNamespace {', duplicate: () => 'namespace c7aReceiptFast {}' },
  { name: 'OneX TLS', anchor: 'static thread_local OneXVirtualStartEpisode* t_inputHostOneXStartWrite=nullptr;', replacement: 'static thread_local OneXVirtualStartEpisode* missingOneXWrite=nullptr;', duplicate: () => 'static thread_local OneXVirtualStartEpisode* t_inputHostOneXStartWrite=nullptr;' },
  { name: 'deferred-hide TLS', anchor: 'static thread_local const InputHostDeferredPhysicalHide* t_inputHostDeferredPhysicalHideWrite=nullptr;', replacement: 'static thread_local const InputHostDeferredPhysicalHide* missingDeferredWrite=nullptr;', duplicate: () => 'static thread_local const InputHostDeferredPhysicalHide* t_inputHostDeferredPhysicalHideWrite=nullptr;' },
  { name: 'Start deadline signature', anchor: 'static uint64_t inputHostOneXStartDeadline();', replacement: 'static uint64_t missingStartDeadline();', duplicate: () => 'static uint64_t inputHostOneXStartDeadline();' },
  { name: 'compact reply binding', anchor: 'static std::string g_inputHostFrameReplyBinding;', replacement: 'static std::string missingFrameReplyBinding;', duplicate: () => 'static std::string g_inputHostFrameReplyBinding;' }
];
for (const row of required) {
  test('wire projection rejects missing ' + row.name, () => {
    const changed = replaceOne(main, typeof row.anchor === 'function' ? row.anchor() : row.anchor, row.replacement);
    assert.throws(() => generateWireInput(changed, template));
  });
  test('wire projection rejects duplicate ' + row.name, () => {
    assert.throws(() => generateWireInput(main + '\n' + row.duplicate() + '\n', template));
  });
}

for (const faultId of ['sequence-sync-omitted', 'instance-fence-omitted']) {
  test('wire projection rejects missing mutation anchor ' + faultId, () => {
    const fault = WIRE_FAULTS.find(row => row.id === faultId);
    assert(fault);
    const changed = replaceOne(main, fault.before, '/* original fault anchor intentionally absent */');
    assert.throws(() => mutateWireSource(changed, faultId));
    assert.throws(() => wireSourceProjection(changed, template));
  });
  test('wire projection rejects duplicate mutation anchor ' + faultId, () => {
    const fault = WIRE_FAULTS.find(row => row.id === faultId);
    assert(fault);
    const changed = main + '\n' + fault.before + '\n';
    assert.throws(() => mutateWireSource(changed, faultId));
    assert.throws(() => wireSourceProjection(changed, template));
  });
}

test('wire projection rejects unknown mutations and missing duplicate or unresolved template markers', () => {
  assert.throws(() => mutateWireSource(main, 'unknown-fault'));
  assert.throws(() => generateWireInput(main, replaceOne(template, '@@REQUEST@@', '')));
  assert.throws(() => generateWireInput(main, template + '\n@@REQUEST@@\n'));
  assert.throws(() => generateWireInput(main, template + '\n@@UNKNOWN_WIRE_MARKER@@\n'));
});
