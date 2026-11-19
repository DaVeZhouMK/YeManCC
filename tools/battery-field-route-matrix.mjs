import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {assertBatteryAuditRouteRows, auditBatteryRouteIdentity} from './lib/battery_route_identity_audit.mjs';

const [factsPath, out] = process.argv.slice(2);
if (!factsPath || !out) throw Error('usage: node tools/battery-field-route-matrix.mjs <field facts.json> <matrix.json>');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const factsBytes = fs.readFileSync(factsPath);
const facts = JSON.parse(factsBytes.toString('utf8').replace(/^\uFEFF/, ''));
if (facts.schemaVersion !== 1 || facts.errors.length) throw Error('incomplete field scan');
const repoFile = file => new URL('../' + file, import.meta.url);
const battery = JSON.parse(fs.readFileSync(repoFile('capabilities/hc-device-catalog.json'), 'utf8')).domains.battery;
assertBatteryAuditRouteRows(battery, fs.readFileSync(repoFile('native/BatteryHost/battery-routes.json')));
const {rows, ruleDocumentSha256} = battery;
const firmwareOf = ({bios, ecMajor, ecMinor}) => ({bios: bios ?? null, ecMajor: ecMajor ?? null, ecMinor: ecMinor ?? null});
function thresholdEvidence(fact) {
  if (fact.ok !== true || fact.supported !== true || fact.batteryPresent !== true) return 'not-confirmed-by-result';
  if (!['firmware-fixed', 'firmware-percent'].includes(fact.kind)) return 'not-a-firmware-threshold-result';
  if (fact.verification !== 'readback' || !Number.isInteger(fact.currentLimit) || fact.currentLimit < 0 || fact.currentLimit > 100)
    return 'firmware-threshold-unconfirmed';
  // Apply may return the pre-read value without writing when the threshold already matches.
  switch (fact.operation) {
    case 'get': return 'reported-firmware-get-readback';
    case 'set': return 'reported-firmware-set-result-readback';
    case 'restore': return 'reported-firmware-restore-result-readback';
    case 'refresh': return 'reported-firmware-refresh-result-readback';
    case 'release': return 'reported-firmware-release-result-readback';
    default: return 'firmware-readback-operation-unrecognized';
  }
}

function compareSelectableThreshold(fact, currentRow) {
  if (!currentRow) return 'current-static-rule-unresolved';
  if (fact.kind !== currentRow.kind) return 'readback-kind-differs-from-current-rule';
  // Project Route.Choices for comparison only. A GET need not return a YMCC-selectable value.
  const min = Math.max(currentRow.min, 50);
  const first = Math.trunc(min / 5);
  const last = first + Math.trunc((100 - min) / 5);
  const selectable = value => Number.isInteger(value) && (currentRow.kind === 'firmware-fixed'
    ? value === 80 || value === 100 : value % 5 === 0 && value / 5 >= first && value / 5 <= last);
  if (!selectable(fact.currentLimit)) return 'readback-outside-current-selectable-targets';
  if (fact.operation !== 'set') return 'readback-is-current-selectable-target';
  if (!Number.isInteger(fact.requestedLimit)) return 'set-reported-target-unavailable';
  if (!selectable(fact.requestedLimit)) return 'set-reported-target-outside-current-selectable-targets';
  return fact.currentLimit === fact.requestedLimit
    ? 'set-readback-matches-current-selectable-target' : 'set-readback-differs-from-reported-target';
}

function firmwareRejectionEvidence(identity, fact, sample) {
  if (!sample?.identityEvidence) return 'legacy-sample-without-identity-line';
  const strings = ['manufacturer', 'boardProduct', 'systemProduct', 'bios'];
  const integers = ['ecMajor', 'ecMinor'];
  const observed = fact.observed ?? {};
  if ([...strings, ...integers].some(key => Object.hasOwn(observed, key) && observed[key] !== identity[key]))
    return 'identity-conflict';
  if (!strings.every(key => typeof observed[key] === 'string') || !integers.every(key => Number.isInteger(observed[key])))
    return 'observation-incomplete';
  if (fact.allowed !== false || fact.providerOpened !== false) return 'denial-or-provider-state-unconfirmed';
  return 'historical-rejection-before-provider-reported';
}

function rejectedRouteRelation(fact, selection, selectedRule) {
  if (!selectedRule) return `current-static-${selection.status}`;
  if (typeof fact.route !== 'string' || !fact.route || typeof fact.deviceClass !== 'string' || !fact.deviceClass)
    return 'historical-route-or-class-unrecorded';
  return selectedRule.route === fact.route && selectedRule.model === fact.deviceClass
    ? 'same-route-and-class-as-current-static-selection' : 'different-from-current-static-selection';
}

const byIdentity = new Map();
for (const group of facts.groups) {
  const {identity} = group;
  const {manufacturer, boardProduct, systemProduct, processor, boardManufacturerRaw, boardProductRaw} = identity;
  const observed = {manufacturer, boardProduct, systemProduct, processor};
  if (typeof boardManufacturerRaw === 'string') observed.boardManufacturerRaw = boardManufacturerRaw;
  if (typeof boardProductRaw === 'string') observed.boardProductRaw = boardProductRaw;
  const key = JSON.stringify(observed);
  if (!byIdentity.has(key)) byIdentity.set(key, {identity: observed, groups: []});
  byIdentity.get(key).groups.push(group);
}
const samples = [...byIdentity.entries()].map(([name, {identity}]) => ({name, identity}));
const {samples: matches} = auditBatteryRouteIdentity(rows, samples, { historical: true });
const byRule = new Map(rows.map(row => [row.ruleId, row]));
const profiles = matches.map(({name, identity, hits, selection, indeterminateOrdinalRuleIds, indeterminateEngineRuleIds}) => {
  const groups = byIdentity.get(name).groups;
  const requests = groups.filter(g => g.event === 'request-result');
  const rejections = groups.filter(g => g.event === 'firmware-admission-rejected');
  const currentRow = byRule.get(selection.ruleId);
  const selectedRule = currentRow ? {route: currentRow.provider, model: currentRow.deviceClass, kind: currentRow.kind, firmwareGate: currentRow.firmwareGate} : null;
  return {identity, hits, selection, selectedRule,
    ...(indeterminateOrdinalRuleIds ? {indeterminateOrdinalRuleIds} : {}),
    ...(indeterminateEngineRuleIds ? {indeterminateEngineRuleIds} : {}),
    observedFirmware: [...new Set(groups.filter(g => g.identity.bios != null).map(g => JSON.stringify(firmwareOf(g.identity))))].map(value => JSON.parse(value)),
    observedFirmwareRejections: rejections.map(({identity: rejectionIdentity, fact, count, sample}) => ({
      firmware: firmwareOf(rejectionIdentity), evidence: firmwareRejectionEvidence(rejectionIdentity, fact, sample),
      currentStaticRelation: rejectedRouteRelation(fact, selection, selectedRule), fact, count, sample,
    })),
    observedRequests: requests.map(({identity: requestIdentity, fact, count, sample}) => {
      const evidence = thresholdEvidence(fact);
      const comparison = evidence.startsWith('reported-firmware-') ? compareSelectableThreshold(fact, currentRow) : undefined;
      return {firmware: firmwareOf(requestIdentity),
        replyIdentity: sample?.identityEvidence ? (fact.reportedModel ? 'same-member-pid-model-matched' : 'same-member-pid-model-unrecorded') : 'legacy-sample-without-identity-line',
        thresholdEvidence: evidence, currentSelectableThresholdRelation: comparison, fact, count, sample};
    })};
});
const matched = new Set(profiles.flatMap(p => p.hits));
const selections = new Set(profiles.map(p => p.selection.ruleId));
const data = {schemaVersion: 2, scope: 'Historical responses and reported firmware rejections beside current static, unique-only identity selection; no execution of firmware gates or providers. Threshold evidence describes the reported operation only. The current selectable threshold comparison does not validate a historical provider, prove a write, associate a GET with a SET, or establish physical charge stop.', fieldFactsSha256: sha(factsBytes), ruleDocumentSha256,
  ...(facts.replyLinkExclusions ? {replyLinkExclusions: facts.replyLinkExclusions} : {}),
  distinctIdentityProfiles: profiles.length, distinctDmiTriples: new Set(profiles.map(({identity: i}) => JSON.stringify([i.manufacturer, i.boardProduct, i.systemProduct]))).size,
  ruleHits: matched.size, rulesNeverMatched: rows.filter(r => !matched.has(r.ruleId)).map(r => r.ruleId),
  matchedButNeverSelected: rows.filter(r => matched.has(r.ruleId) && !selections.has(r.ruleId)).map(r => r.ruleId), profiles};
fs.writeFileSync(out, JSON.stringify(data, null, 2) + '\n');
console.log(`${data.distinctDmiTriples} observed DMI triples, ${matched.size}/${rows.length} current rule hits, ${profiles.filter(p => p.hits.length > 1).length} observed profile overlaps.`);
