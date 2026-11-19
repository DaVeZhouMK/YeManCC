// Pure source projection. The caller owns external pins and the frozen epoch.
// This does not approve a whole current main, a compiler closure, or execution.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {extract} from '../legion_motion_consumer/extract_main.mjs';

const main = 'native/main.cpp';
const baselineMembers = Object.freeze([
  main, 'native/virtual_controller_start_runtime.h',
  'native/external_controller_priority_runtime.h', 'native/virtual_start_protection.h',
  'native/virtual_controller_policy_return_runtime.h',
]);
const candidateMembers = Object.freeze([
  ...baselineMembers, 'native/msi_crash_recovery_runtime.h', 'native/msi_owned_return_runtime.h',
]);
const unchangedFunctions = Object.freeze([
  'gamepadReadState', 'msiDinputPublishMainSourceIdentity',
  'msiDinputRevokeMainSourceIdentity', 'msiDinputTryDischargeReturnDebt', 'inputHostSubmitPad',
]);
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const pin = bytes => ({bytes: bytes.length, sha256: hash(bytes)});
const need = (value, reason) => assert(value, 'MSI_POLICY_PROJECTION_' + reason);

function checkProducts(products, members, role) {
  need(products instanceof Map && products.size === members.length, role + '_EXACT_MEMBER_SET');
  for (const [relative, bytes] of products) {
    need(members.includes(relative), role + '_UNKNOWN_MEMBER:' + relative);
    need(Buffer.isBuffer(bytes) && bytes.length > 0, role + '_BUFFER_REQUIRED:' + relative);
  }
  for (const relative of members) need(products.has(relative), role + '_MISSING_MEMBER:' + relative);
}

function sourceText(bytes) {
  const text = bytes.toString('utf8');
  need(Buffer.from(text, 'utf8').equals(bytes), 'LOSSLESS_UTF8_REQUIRED');
  return text;
}

function definition(text, bytes, symbol, kind = 'function') {
  const entry = extract(text, symbol, kind);
  const raw = bytes.subarray(entry.startUtf8Byte, entry.endUtf8ByteExclusive);
  need(raw.equals(Buffer.from(entry.code)), 'EXTRACTED_BYTES_CONFLICT:' + symbol);
  return {symbol, kind, startUtf8Byte: entry.startUtf8Byte,
    endUtf8ByteExclusive: entry.endUtf8ByteExclusive, bytes: raw};
}

function location(entry) {
  return {relative: main, startUtf8Byte: entry.startUtf8Byte,
    endUtf8ByteExclusive: entry.endUtf8ByteExclusive, ...pin(entry.bytes)};
}

function productPins(products) {
  return [...products].map(([relative, bytes]) => ({relative, ...pin(bytes)}));
}

export function projectMsiPolicyProducts({baselineProducts, candidateProducts}) {
  const options = arguments[0];
  need(Object.keys(options).length === 2 && Object.hasOwn(options, 'baselineProducts') &&
    Object.hasOwn(options, 'candidateProducts'), 'EXACT_ARGUMENTS_REQUIRED');
  checkProducts(baselineProducts, baselineMembers, 'BASELINE');
  checkProducts(candidateProducts, candidateMembers, 'CANDIDATE');
  const before = baselineProducts.get(main), donor = candidateProducts.get(main);
  const beforeText = sourceText(before), donorText = sourceText(donor);
  need(!/\bInputCapturePhysicalReturnWindow\b/.test(beforeText), 'NEW_TYPE_ALREADY_IN_BASELINE');

  const oldSnapshot = definition(beforeText, before, 'InputCaptureGamepadSnapshot', 'struct');
  const newSnapshot = definition(donorText, donor, 'InputCaptureGamepadSnapshot', 'struct');
  const newType = definition(donorText, donor, 'InputCapturePhysicalReturnWindow', 'struct');
  const oldRead = definition(beforeText, before, 'gamepadReadStateUnlocked');
  const newRead = definition(donorText, donor, 'gamepadReadStateUnlocked');
  const signature = 'static void gamepadReadStateUnlocked(bool publishUi) {';
  need(oldRead.bytes.subarray(0, signature.length).equals(Buffer.from(signature)) &&
    newRead.bytes.subarray(0, signature.length).equals(Buffer.from(signature)), 'READ_SIGNATURE_CHANGED');

  const fields = [...newSnapshot.bytes.toString('utf8').matchAll(
    /^    InputCapturePhysicalReturnWindow physicalReturnRead\{\};\r?\n/gm)];
  need(fields.length === 1 &&
    [...newSnapshot.bytes.toString('utf8').matchAll(/\bphysicalReturnRead\b/g)].length === 1,
  'UNIQUE_PHYSICAL_RETURN_FIELD_REQUIRED');
  const field = fields[0][0];
  need(newSnapshot.bytes.toString('utf8').endsWith(field + '};'), 'PHYSICAL_RETURN_FIELD_POSITION');
  const snapshotPreimage = Buffer.from(newSnapshot.bytes.toString('utf8').replace(field, ''));
  need(snapshotPreimage.equals(oldSnapshot.bytes), 'SNAPSHOT_PREIMAGE_DRIFT');
  const wholeDefinitionEquality = [{symbol: oldSnapshot.symbol, kind: 'struct',
    comparison: 'candidate minus its sole physicalReturnRead field',
    baseline: location(oldSnapshot), donor: location(newSnapshot),
    comparedDonor: pin(snapshotPreimage), wholeBytesEqual: true}];
  for (const symbol of unchangedFunctions) {
    const oldDefinition = definition(beforeText, before, symbol);
    const donorDefinition = definition(donorText, donor, symbol);
    need(oldDefinition.bytes.equals(donorDefinition.bytes), 'UNCHANGED_DEFINITION_DRIFT:' + symbol);
    wholeDefinitionEquality.push({symbol, kind: 'function', baseline: location(oldDefinition),
      donor: location(donorDefinition), wholeBytesEqual: true});
  }

  need(newType.endUtf8ByteExclusive < newSnapshot.startUtf8Byte, 'DONOR_TYPE_ORDER');
  const separator = donor.subarray(newType.endUtf8ByteExclusive, newSnapshot.startUtf8Byte);
  need(separator.equals(Buffer.from('\r\n')) || separator.equals(Buffer.from('\n')),
    'DONOR_TYPE_SNAPSHOT_BOUNDARY');
  need(oldSnapshot.endUtf8ByteExclusive < oldRead.startUtf8Byte, 'BASELINE_DEFINITION_ORDER');
  const edits = [
    {symbol: newType.symbol, kind: 'insert-type', start: oldSnapshot.startUtf8Byte,
      end: oldSnapshot.startUtf8Byte, after: Buffer.concat([newType.bytes, separator]), donor: newType},
    {symbol: newSnapshot.symbol, kind: 'replace-whole-struct', start: oldSnapshot.startUtf8Byte,
      end: oldSnapshot.endUtf8ByteExclusive, after: newSnapshot.bytes, donor: newSnapshot},
    // Copy the entire actual donor, including its UI assembly changes.
    {symbol: newRead.symbol, kind: 'replace-whole-function', start: oldRead.startUtf8Byte,
      end: oldRead.endUtf8ByteExclusive, after: newRead.bytes, donor: newRead},
  ];
  const chunks = [], changedRanges = [];
  let cursor = 0, delta = 0;
  for (const edit of edits) {
    need(edit.start >= cursor && edit.end >= edit.start, 'OVERLAPPING_RANGES');
    const oldBytes = Buffer.from(before.subarray(edit.start, edit.end));
    const newBytes = Buffer.from(edit.after), projectedStart = edit.start + delta;
    chunks.push(before.subarray(cursor, edit.start), newBytes);
    changedRanges.push({symbol: edit.symbol, kind: edit.kind,
      sourceBefore: {relative: main, startUtf8Byte: edit.start,
        endUtf8ByteExclusive: edit.end, ...pin(oldBytes)},
      sourceAfter: {relative: main, startUtf8Byte: projectedStart,
        endUtf8ByteExclusive: projectedStart + newBytes.length, ...pin(newBytes)},
      donor: location(edit.donor), beforeBytes: oldBytes, afterBytes: newBytes});
    cursor = edit.end;
    delta += newBytes.length - oldBytes.length;
  }
  chunks.push(before.subarray(cursor));
  const projected = Buffer.concat(chunks);
  let inverse = projected;
  for (const range of [...changedRanges].reverse()) {
    const {startUtf8Byte: start, endUtf8ByteExclusive: end} = range.sourceAfter;
    need(inverse.subarray(start, end).equals(range.afterBytes), 'INVERSE_RANGE_CONFLICT');
    inverse = Buffer.concat([inverse.subarray(0, start), range.beforeBytes, inverse.subarray(end)]);
  }
  need(inverse.equals(before), 'WHOLE_MAIN_EXACT_INVERSE_FAILED');

  const projectedText = sourceText(projected);
  const wholeDonorDefinitionEquality = [newType, newSnapshot, newRead].map(entry => {
    const actual = definition(projectedText, projected, entry.symbol, entry.kind);
    need(actual.bytes.equals(entry.bytes), 'WHOLE_DONOR_DEFINITION_CONFLICT:' + entry.symbol);
    return {symbol: entry.symbol, kind: entry.kind, donor: location(entry),
      projected: location(actual), wholeBytesEqual: true};
  });
  for (const symbol of unchangedFunctions) {
    need(definition(projectedText, projected, symbol).bytes.equals(
      definition(beforeText, before, symbol).bytes), 'PROJECTED_UNCHANGED_DEFINITION_DRIFT:' + symbol);
  }
  const products = new Map(candidateMembers.map(relative =>
    [relative, relative === main ? projected : Buffer.from(candidateProducts.get(relative))]));
  return {products, projection: {
    changedRanges,
    sourcePins: {before: productPins(baselineProducts), donor: productPins(candidateProducts),
      after: productPins(products)},
    exactInverses: {rangeCount: changedRanges.length, wholeMainToBaselineRaw: true,
      recovered: pin(inverse), baseline: pin(before)},
    wholeDefinitionEquality, wholeDonorDefinitionEquality,
    sourceApprovalGranted: false, runtimeAuthorityGranted: false, wholeCurrentMainAccepted: false,
    compilerClosureComplete: false, onRuntimeAccepted: false,
  }};
}
