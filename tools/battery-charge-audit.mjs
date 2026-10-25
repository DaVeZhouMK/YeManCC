// Research-only capability audit. No firmware, WMI, EC or device writes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const catalogPath = path.join(root, 'docs/battery-charge-capabilities.json');
export const loadCatalog = () => JSON.parse(fs.readFileSync(catalogPath, 'utf8'));
const normalize = value => typeof value === 'string' ? value.trim().toUpperCase() : '';
const className = value => typeof value === 'string'
  ? value.replace(/^HandheldCompanion\.Devices\./, '') : '';
const validByte = value => Number.isInteger(value) && value >= 0 && value < 255;

export function validateCatalog(catalog, verifySources = false) {
  const errors = [];
  if (catalog.schemaVersion !== 1) errors.push('Unknown schema version');
  for (const field of ['runtimeImplemented', 'hardwareValidated', 'writeEnabled']) {
    if (catalog[field] !== false) errors.push(`Research catalog must not advertise ${field}`);
  }
  const ids = new Set(), classes = new Set();
  for (const route of catalog.routes ?? []) {
    if (!route.id || ids.has(route.id)) errors.push(`Duplicate or empty route: ${route.id}`);
    ids.add(route.id);
    if (!route.sourceRefs?.length) errors.push(`${route.id}: missing evidence`);
    for (const id of route.sourceRefs ?? []) {
      if (!catalog.sources?.[id]) errors.push(`${route.id}: unknown source ${id}`);
    }
    for (const field of ['runtimeImplemented', 'hardwareValidated', 'writeEnabled']) {
      if (route[field] !== false) errors.push(`${route.id}: research cannot advertise ${field}`);
    }
    for (const deviceClass of route.deviceClasses ?? []) {
      if (classes.has(deviceClass)) errors.push(`Ambiguous class route: ${deviceClass}`);
      classes.add(deviceClass);
    }
    if (route.kind === 'firmware-fixed' && !route.limits?.presets?.length)
      errors.push(`${route.id}: fixed mode must declare presets`);
    for (const identity of route.exactIdentities ?? []) {
      if (!identity.manufacturers?.length || !identity.values?.length ||
          !['boardProduct', 'systemProduct'].includes(identity.field))
        errors.push(`${route.id}: incomplete exact identity`);
    }
  }
  for (const source of Object.values(catalog.sources ?? {})) {
    if (!/^[0-9a-f]{64}$/.test(source.sha256 ?? '')) errors.push(`${source.path}: missing SHA256`);
    if (source.repo && !/^[0-9a-f]{40}$/.test(source.commit ?? '')) errors.push(`${source.path}: unpinned source`);
    if (verifySources) {
      const snapshot = path.resolve(path.dirname(catalogPath), source.snapshot);
      try {
        const hash = crypto.createHash('sha256').update(fs.readFileSync(snapshot)).digest('hex');
        if (hash !== source.sha256) errors.push(`${source.path}: snapshot hash mismatch`);
      } catch (error) { errors.push(`${source.path}: ${error.message}`); }
    }
  }
  return errors;
}

export function assessFirmware(route, firmware = {}) {
  const gate = route.firmwareGate;
  if (gate.kind === 'hc-ec-components') {
    if (!validByte(firmware.ecMajor) || !validByte(firmware.ecMinor)) return 'unknown';
    // Preserve the actual upstream predicate. This does NOT approve a new EC revision.
    return firmware.ecMajor >= gate.majorMin && firmware.ecMinor >= gate.minorMin
      ? 'source-predicate-matched' : 'source-predicate-rejected';
  }
  if (gate.kind === 'exact-board-firmware') {
    if (!Number.isInteger(firmware.boardId) || !Number.isInteger(firmware.firmware)) return 'unknown';
    return gate.pairs.some(pair => pair.boardId === firmware.boardId && pair.firmware === firmware.firmware)
      ? 'source-predicate-matched' : 'source-predicate-rejected';
  }
  if (gate.kind === 'exact-reviewed-firmware') {
    if (typeof firmware.bios !== 'string' || !validByte(firmware.ecMajor) || !validByte(firmware.ecMinor)) return 'unknown';
    return firmware.bios === gate.bios && firmware.ecMajor === gate.ecMajor && firmware.ecMinor === gate.ecMinor
      ? 'source-predicate-matched' : 'unreviewed-firmware';
  }
  return gate.kind; // provider-probe-required / windows-transport-unverified
}

export function supportsResearchValue(route, percent) {
  if (!Number.isInteger(percent)) return false;
  const limits = route.limits;
  if (!limits) return false;
  if (limits.presets) return limits.presets.includes(percent);
  return percent >= limits.min && percent <= limits.max && (percent - limits.min) % limits.step === 0;
}

export function inspectIdentity(catalog, identity = {}) {
  const maker = normalize(identity.manufacturer);
  const board = normalize(identity.boardProduct ?? identity.product);
  const systemProduct = normalize(identity.systemProduct);
  const suppliedClass = className(identity.deviceClass);
  const candidates = [];
  for (const route of catalog.routes) {
    const exact = (route.exactIdentities ?? []).filter(rule =>
      rule.manufacturers.some(value => normalize(value) === maker) &&
      rule.values.some(value => normalize(value) === (rule.field === 'boardProduct' ? board : systemProduct)) &&
      (!rule.processorPattern || new RegExp(rule.processorPattern, 'i').test(identity.processor ?? '')));
    const linuxDmi = route.evidencePlatform === 'linux-reference' && (route.dmiCandidates ?? []).some(rule =>
      normalize(rule.manufacturer) === maker && normalize(rule.boardProduct) === board &&
      (!rule.processorContains || normalize(identity.processor).includes(normalize(rule.processorContains))));
    const classMatch = suppliedClass && route.deviceClasses.includes(suppliedClass);
    if (!exact.length && !linuxDmi && !classMatch) continue;
    candidates.push({ routeId: route.id, family: route.family, kind: route.kind,
      evidencePlatform: route.evidencePlatform,
      identityEvidence: exact.length || linuxDmi ? 'exact-source-identity' : 'class-only-untrusted',
      classContradiction: !!(exact.length && suppliedClass && !exact.some(rule => rule.deviceClass === suppliedClass)),
      firmwareEvidence: assessFirmware(route, identity.firmware),
      runtimeImplemented: false, hardwareValidated: false, writeEnabled: false });
  }
  const exclusions = catalog.exclusions.filter(entry => entry.deviceClasses.includes(suppliedClass));
  // Class labels are not write permissions. Even a fully matching reference stays read-only.
  return { identity, candidates, exclusions, writeEnabled: false,
    reason: candidates.length ? 'research-evidence-only' : 'no-established-route' };
}

export function summarize(catalog) {
  const count = evidence => catalog.inventory.filter(item => item.evidence === evidence).length;
  const mainText = fs.readFileSync(path.join(root, 'native/main.cpp'), 'utf8');
  const classMentions = new Set([...mainText.matchAll(/HandheldCompanion\.Devices\.([A-Za-z0-9_]+)/g)].map(match => match[1]));
  return {
    reviewedAt: catalog.reviewedAt, routes: catalog.routes.length,
    inventoryEntries: catalog.inventory.length,
    upstreamClassCandidates: count('upstream-candidate'),
    explicitExclusions: count('explicit-exclusion'),
    unestablishedClasses: count('not-established'),
    linuxDmiCandidates: catalog.routes.filter(route => route.id === 'oxp-linux-platform').flatMap(route => route.dmiCandidates).length,
    candidatesAbsentFromMainlineClassMentions: catalog.inventory.filter(item => item.route && !classMentions.has(item.deviceClass)).map(item => item.deviceClass),
    runtimeImplemented: false, hardwareValidated: false, writeEnabled: false,
    warning: 'Counts are source class entries, not unique physical models or tested devices. Class mention is not exact DMI support.'
  };
}

function main() {
  const args = process.argv.slice(2);
  const allowed = new Set(['--json', '--verify-sources', '--identity', '--help']);
  let identityPath;
  for (let i = 0; i < args.length; i++) {
    if (!allowed.has(args[i])) throw new Error(`Unknown argument ${args[i]}`);
    if (args[i] === '--identity') {
      identityPath = args[++i];
      if (!identityPath || identityPath.startsWith('--')) throw new Error('--identity requires a JSON file');
    }
  }
  if (args.includes('--help')) {
    console.log('node tools/battery-charge-audit.mjs [--json] [--verify-sources] [--identity probe.json]\nRead-only source/identity audit. Never writes hardware or enables a runtime feature.');
    return;
  }
  const catalog = loadCatalog();
  const errors = validateCatalog(catalog, args.includes('--verify-sources'));
  const output = { ...summarize(catalog), errors };
  if (identityPath) {
    const probe = JSON.parse(fs.readFileSync(identityPath, 'utf8').replace(/^\uFEFF/, ''));
    output.identityAssessment = inspectIdentity(catalog, probe.identity ?? probe);
  }
  if (args.includes('--json')) console.log(JSON.stringify(output, null, 2));
  else {
    console.log(`Battery research: ${output.routes} routes, ${output.upstreamClassCandidates} candidate classes, ${output.explicitExclusions} exclusions, ${output.unestablishedClasses} unestablished classes.`);
    console.log(`Additional OXP/AOKZOE Linux table: ${output.linuxDmiCandidates} board names (overlap exists; do not add counts).`);
    console.log('Research snapshot only: this audit never enables writes. Current Settings implementation: docs/BATTERY-CHARGE-LIMIT.md and native/BatteryHost/battery-routes.json; physical handheld validation remains pending.');
    if (output.identityAssessment) console.log(JSON.stringify(output.identityAssessment, null, 2));
    for (const error of errors) console.error(error);
  }
  if (errors.length) process.exitCode = 1;
}
if (process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url) {
  try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
