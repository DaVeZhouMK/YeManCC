import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';

// A route SHA copied into a catalog does not authenticate its projected rows.
export function assertBatteryAuditRouteRows(battery, routeBytes) {
  const routeSha = createHash('sha256').update(routeBytes).digest('hex');
  assert.equal(battery.ruleDocumentSha256, routeSha, 'battery audit route document SHA differs');
  const routes = JSON.parse(routeBytes.toString('utf8').replace(/^\uFEFF/, ''));
  assert.ok(Array.isArray(routes) && Array.isArray(battery.rows), 'battery audit route arrays missing');
  assert.equal(battery.rows.length, routes.length, 'battery audit route row count differs');
  assert.equal(battery.registeredRules, routes.length, 'battery audit registered route count differs');
  for (const [index, route] of routes.entries()) {
    const { identity, firmwareGate, kind, min } = route;
    const { deviceClass } = identity;
    const provider = route.route;
    const row = battery.rows[index];
    const fields = { index, ruleId: `battery.rule.${provider}.${deviceClass}`, deviceClass, provider, identity, firmwareGate, kind, min };
    for (const [key, expected] of Object.entries(fields)) {
      assert.deepEqual(row?.[key], expected, `battery audit route row ${index} differs: ${key}`);
    }
  }
}

// Static model of the current catalog predicates, not a replacement dispatcher.
const exact = 'normalized-exact';
const hx370 = /\bHX\s*370\b/i;
const trim = value => value.replace(/^[\u0009-\u000D\u0085\p{Z}]+|[\u0009-\u000D\u0085\p{Z}]+$/gu, '');
const nonAscii = /[^\x00-\x7f]/;
const engineUnknown = 'engine-unknown';
const ordinalUnknown = 'ordinal-unknown';
const norm = value => trim(value).toUpperCase();
function literals(values, mode, allowed) {
  if (!allowed.includes(mode) || !values.length || values.some(v => !/^[ -~]+$/.test(v)))
    throw Error('unreviewed battery audit literal or predicate');
  return {values, mode};
}

function compatibleAsciiParts(literal, parts) {
  const last = parts.at(-1);
  if (!literal.startsWith(parts[0]) || !literal.endsWith(last)) return false;
  let cursor = parts[0].length;
  for (const part of parts.slice(1, -1)) {
    const index = literal.indexOf(part, cursor);
    if (index < 0) return false;
    cursor = index + part.length;
  }
  return cursor <= literal.length - last.length;
}

function matches({values, mode}, candidate) {
  if (mode === exact) {
    const trimmed = trim(candidate);
    // Reviewed literals are ASCII. Do not substitute JS full casing for .NET invariant casing.
    if (nonAscii.test(trimmed)) {
      const asciiParts = trimmed.split(/[^\x00-\x7f]+/).map(part => part.toUpperCase());
      return values.some(value => compatibleAsciiParts(norm(value), asciiParts)) ? engineUnknown : false;
    }
    return values.some(value => norm(value) === trimmed.toUpperCase());
  }
  return values.some(value => mode === 'ordinal-exact' ? candidate === value : candidate.includes(value));
}

function observedMatch(rule, reported, raw) {
  if (typeof raw === 'string') return matches(rule, raw);
  if (rule.mode === exact) return matches(rule, reported);
  // A plausible display value cannot prove an ordinal predicate on the missing raw value.
  const couldMatch = rule.values.some(value => rule.mode === 'ordinal-contains'
    ? norm(reported).includes(norm(value)) : norm(reported) === norm(value));
  return couldMatch ? ordinalUnknown : false;
}

function processorMatches(pattern, processor = '') {
  if (pattern == null) return true;
  // Retain the reviewed ASCII pattern, after translating only shared whitespace semantics.
  // Unmodeled characters may change .NET word boundaries or case matching.
  const text = (processor ?? '').replace(/[\u0009-\u000D\u0085\p{Z}]/gu, ' ');
  return nonAscii.test(text) ? engineUnknown : hx370.test(text);
}

function intersection(a, b) {
  const candidates = [...a.values, ...b.values, ...a.values.map(norm), ...b.values.map(norm)];
  if (a.mode === 'ordinal-contains' && b.mode === 'ordinal-contains')
    candidates.push(`${a.values[0]} ${b.values[0]}`);
  return candidates.find(value => matches(a, value) === true && matches(b, value) === true);
}

function selection(hits, indeterminate = []) {
  // The binder rejects multiple matches regardless of registration order.
  if (hits.length > 1) return {status: 'ambiguous', ruleId: null};
  if (indeterminate.length) return {status: 'indeterminate', ruleId: null};
  return hits.length === 1 ? {status: 'unique', ruleId: hits[0]} : {status: 'no-match', ruleId: null};
}

export function auditBatteryRouteIdentity(rows, samples = [], { historical = false } = {}) {
  const rules = rows.map(({ruleId, identity}) => {
    const {field, manufacturers, values, manufacturerMatch = exact, valueMatch = exact, processorPattern} = identity;
    if (!['boardProduct', 'systemProduct'].includes(field) || (processorPattern != null && processorPattern !== hx370.source))
      throw Error('unreviewed battery audit identity field or processor pattern: ' + ruleId);
    return {ruleId, field, manufacturer: literals(manufacturers, manufacturerMatch, [exact, 'ordinal-contains']),
      product: literals(values, valueMatch, [exact, 'ordinal-exact']), processorPattern};
  });
  const evaluate = (identity, useRaw) => {
    const hits = [], ordinal = [], engine = [];
    for (const r of rules) {
      const make = useRaw ? observedMatch(r.manufacturer, identity.manufacturer, identity.boardManufacturerRaw) : matches(r.manufacturer, identity.manufacturer);
      const product = useRaw ? observedMatch(r.product, identity[r.field], r.field === 'boardProduct' ? identity.boardProductRaw : undefined) : matches(r.product, identity[r.field]);
      const predicates = [make, product, processorMatches(r.processorPattern, identity.processor)];
      if (predicates.includes(false)) continue;
      if (predicates.every(value => value === true)) hits.push(r.ruleId);
      if (predicates.includes(ordinalUnknown)) ordinal.push(r.ruleId);
      if (predicates.includes(engineUnknown)) engine.push(r.ruleId);
    }
    return {hits, selection: selection(hits, [...ordinal, ...engine]),
      ...(ordinal.length ? {indeterminateOrdinalRuleIds: ordinal} : {}),
      ...(engine.length ? {indeterminateEngineRuleIds: engine} : {})};
  };
  const pairs = [];
  for (const [i, first] of rules.entries()) for (const second of rules.slice(i + 1)) {
    const manufacturer = intersection(first.manufacturer, second.manufacturer);
    if (manufacturer === undefined) continue;
    const processor = first.processorPattern || second.processorPattern ? 'AMD Ryzen AI 9 HX 370' : '';
    const witness = {manufacturer, boardProduct: '', systemProduct: '', processor};
    const sameField = first.field === second.field;
    if (sameField) {
      const product = intersection(first.product, second.product);
      if (product === undefined) continue;
      witness[first.field] = product;
    } else {
      witness[first.field] = first.product.values[0];
      witness[second.field] = second.product.values[0];
    }
    const {hits, selection: witnessSelection} = evaluate(witness, false);
    if (!hits.includes(first.ruleId) || !hits.includes(second.ruleId)) throw Error('invalid identity overlap witness');
    pairs.push({kind: sameField ? 'same-field' : 'cross-field', rules: [first.ruleId, second.ruleId], witness, selection: witnessSelection});
  }
  return {pairsExamined: rules.length * (rules.length - 1) / 2, pairs,
    samples: samples.map(({name, identity}) => ({name, identity, ...evaluate(identity, historical)}))};
}
