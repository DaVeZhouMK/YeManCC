import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import {assertBatteryAuditRouteRows, auditBatteryRouteIdentity} from './lib/battery_route_identity_audit.mjs';

const [factsPath, out] = process.argv.slice(2);
assert.equal(process.argv.length, 4, 'usage: node tools/battery-field-firmware-audit.mjs <host facts.json> <new report.json>');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const readRepo = file => fs.readFileSync(new URL('../' + file, import.meta.url));
const parse = bytes => JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, ''));
const factsBytes = fs.readFileSync(factsPath);
const facts = parse(factsBytes);
assert.ok(facts.schemaVersion === 1 && Array.isArray(facts.errors) && facts.errors.length === 0 && Array.isArray(facts.groups),
  'incomplete battery Host facts');
const battery = parse(readRepo('capabilities/hc-device-catalog.json')).domains.battery;
assertBatteryAuditRouteRows(battery, readRepo('native/BatteryHost/battery-routes.json'));

// A changed runtime predicate needs a fresh review, not a stale projection.
const policy = readRepo('native/BatteryHost/Policy.cs');
const providers = readRepo('native/BatteryHost/Providers.cs');
const policyText = policy.toString('utf8').replace(/\r\n/g, '\n');
const providersText = providers.toString('utf8').replace(/\r\n/g, '\n');
const parts = [policyText.match(/^    static string Norm\([^\n]*$/gm),
  policyText.match(/^    public static bool FirmwareAllowed\([\s\S]*?^    \}$/gm),
  providersText.match(/^    internal const string X2MiniProRouteId=[^\n]*$/gm)];
assert.ok(parts.every(m => m?.length === 1), 'battery firmware source requires review');
const reviewedPredicateSha256 = sha(parts.map(([part]) => part).join('\n'));
assert.equal(reviewedPredicateSha256, '86872ac327249623c80a2846c8f3d8f2ca965675c7388fb112da30f03118df7f',
  'battery firmware source requires review');

const trim = text => text.replace(/^[\u0009-\u000D\u0085\p{Z}]+|[\u0009-\u000D\u0085\p{Z}]+$/gu, '');
const ascii = /^[\x00-\x7f]*$/;
const digest = /^[0-9a-f]{64}$/;
const identityFields = ['manufacturer', 'boardProduct', 'systemProduct', 'bios', 'ecMajor', 'ecMinor'];
const missing = (value, name) => name.startsWith('ec')
  ? !Number.isInteger(value) || value < -2147483648 || value > 2147483647
  : typeof value !== 'string' || !trim(value);
function project(row, {identity: id, sample, event, fact}) {
  if (!row) return {status: 'static-selection-unresolved'};
  const {kind, majorMin, minorMin, bios, ecMajor, ecMinor} = row.firmwareGate;
  const gate = (status, detail = {}) => ({kind, status, ...detail});
  const parent = sample?.identityEvidence;
  const identityEvent = event === 'identity' || event === 'in-process-start';
  if (typeof sample?.archive !== 'string' || !sample.archive || typeof sample.member !== 'string' || !sample.member ||
      !Number.isInteger(sample.pid) || sample.pid < 1 || !Number.isInteger(sample.line) ||
      !Number.isInteger(parent?.line) || parent.line < 1 || parent.line > sample.line ||
      !digest.test(sample.eventLineSha256 ?? '') || !digest.test(parent.eventLineSha256 ?? '') ||
      (identityEvent ? parent.line !== sample.line || parent.eventLineSha256 !== sample.eventLineSha256 : parent.line === sample.line))
    return gate('no-linked-identity');
  if (event === 'firmware-admission-rejected' && fact?.observed &&
      identityFields.some(key => Object.hasOwn(fact.observed, key) && fact.observed[key] !== id[key]))
    return gate('conflicting-historical-identity');
  if (kind === 'provider-probe-required') return gate('provider-probe-required');
  if (kind === 'exact-board-firmware') return gate('provider-board-firmware-pair-required');
  const fields = ['ecMajor', 'ecMinor'];
  if (kind === 'exact-reviewed-firmware') fields.push('bios');
  else if (kind === 'oxp-x2-mini-pro-field-firmware') fields.push('bios', 'systemProduct');
  else if (kind !== 'hc-ec-components') return gate('unreviewed-gate-kind');
  const missingFields = fields.filter(key => missing(id[key], key));
  if (missingFields.length) return gate('missing-or-invalid-fields', {fields: missingFields});
  let passes;
  if (kind === 'hc-ec-components') {
    passes = id.ecMajor >= 0 && id.ecMajor < 255 && id.ecMinor >= 0 && id.ecMinor < 255 &&
      id.ecMajor >= majorMin && id.ecMinor >= minorMin;
  } else if (kind === 'exact-reviewed-firmware') {
    // Keep the observed production regex behavior, including partial-version matches.
    if (!ascii.test(id.bios)) return gate('text-engine-unverified');
    passes = /(?:^|[^0-9])2\.10(?:$|[^0-9])/.test(id.bios) && id.ecMajor === ecMajor && id.ecMinor === ecMinor;
  } else {
    const system = trim(id.systemProduct);
    if (!ascii.test(system)) return gate('text-engine-unverified');
    passes = row.provider === 'oxp-x2-mini-pro-direct-ec' &&
      row.identity.values.some(value => trim(value).toUpperCase() === system.toUpperCase()) &&
      trim(id.bios) === bios && id.ecMajor === ecMajor && id.ecMinor === ecMinor;
  }
  return gate(passes ? 'policy-predicate-matches' : 'policy-predicate-rejects');
}

const events = new Set(['identity', 'in-process-start', 'request-result', 'firmware-admission-rejected']);
const groups = facts.groups.filter(group => events.has(group.event));
const {samples} = auditBatteryRouteIdentity(battery.rows,
  groups.map(({identity}, name) => ({identity, name})), {historical: true});
const byRule = new Map(battery.rows.map(row => [row.ruleId, row]));
const observations = samples.map(({name, identity, hits, selection}) => {
  const group = groups[name];
  const row = byRule.get(selection.ruleId);
  return {identity, event: group.event, count: group.count, sample: group.sample, hits, selection,
    route: row?.provider ?? null, model: row?.deviceClass ?? null,
    currentFirmwareProjection: project(row, group), historicalFact: group.fact};
});
const selected = new Set(observations.map(item => item.selection.ruleId));
const report = {schemaVersion: 1,
  scope: 'Offline projection of reviewed current Policy against the same historical identity row; no Provider, historical build admission inference, write, readback, or physical cutoff proof.',
  fieldFactsSha256: sha(factsBytes), ruleDocumentSha256: battery.ruleDocumentSha256,
  reviewedPredicateSha256, policySourceSha256: sha(policy), providerSourceSha256: sha(providers),
  ruleCoverage: battery.rows.map(row => ({ruleId: row.ruleId, model: row.deviceClass, route: row.provider,
    gate: row.firmwareGate.kind, selectedInFacts: selected.has(row.ruleId)})), observations};
fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', {flag: 'wx'});
console.log(`${observations.length} Host fact groups, ${battery.rows.filter(row => selected.has(row.ruleId)).length}/${battery.rows.length} rules uniquely selected.`);
