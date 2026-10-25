/** Source-bound ownership data for inventory/plan; never execution acceptance.
 * Call getOwnershipInventory({repoRoot, adapters}) or
 * getOwnershipPlan({repoRoot, ids, adapters}) with existing registration rows.
 * Only source files and routing identities are read. No product/adapter imports,
 * global lifecycle audit, process execution or network requests occur here.
 */
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath, pathToFileURL} from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CATALOG_SHA256='c6facc8249b03d83a9d40d9a745b778f58158a1a5ac496176167918af6e950eb';
const digest = value => createHash('sha256').update(value).digest('hex');
const need = (ok, reason) => { if (!ok) throw Error('OWNERSHIP_' + reason); };
const classifications = ['persistent', 'one-shot', 'unknown'];
const inside = (root, file) => {
  const relative = path.relative(root, file);
  return !path.isAbsolute(relative) && relative !== '..' && !relative.startsWith('..' + path.sep);
};
const canonical = value => Array.isArray(value) ? value.map(canonical)
  : value && typeof value === 'object'
    ? Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])]))
    : value;
const relativePath = file => typeof file === 'string' && file.length > 0
  && !path.isAbsolute(file) && !file.split(/[\\/]/).includes('..') && !/[\0\r\n]/.test(file);
const nonempty = value => typeof value === 'string' && value.length > 0;

function pointer(value, location) {
  need(typeof location === 'string' && (location === '' || location.startsWith('/')), 'JSON_POINTER');
  for (const token of location.split('/').slice(1)) {
    const key = token.replaceAll('~1', '/').replaceAll('~0', '~');
    need(value !== null && typeof value === 'object' && Object.hasOwn(value, key), 'JSON_POINTER_MISSING');
    value = value[key];
  }
  return value;
}
function once(text, anchor) {
  need(nonempty(anchor), 'ANCHOR_REQUIRED');
  const start = text.indexOf(anchor);
  need(start >= 0 && text.lastIndexOf(anchor) === start, 'ANCHOR_MISSING_OR_AMBIGUOUS');
  return start;
}
// Bind entire C++ bodies, including all branches. Comments and quoted/raw
// strings are not structural braces. No fallback to a fixed line window.
function braced(text, anchor) {
  const start = once(text, anchor);
  need(anchor.trimEnd().endsWith('{'), 'BRACED_ANCHOR_MUST_INCLUDE_OPEN');
  const open = start + anchor.lastIndexOf('{');
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    if (text.startsWith('//', i)) {
      const end = text.indexOf('\n', i + 2);
      i = end < 0 ? text.length : end;
      continue;
    }
    if (text.startsWith('/*', i)) {
      const end = text.indexOf('*/', i + 2);
      need(end >= 0, 'COMMENT_UNTERMINATED');
      i = end + 1;
      continue;
    }
    const raw = text[i] === 'R' && text.slice(i, i + 20).match(/^R"([^\s()\\]{0,16})\(/);
    if (raw) {
      const close = ')' + raw[1] + '"';
      const end = text.indexOf(close, i + raw[0].length);
      need(end >= 0, 'RAW_STRING_UNTERMINATED');
      i = end + close.length - 1;
      continue;
    }
    const c = text[i];
    const digitSeparator = c === "'" && /[0-9]/.test(text[i - 1] ?? '') && /[0-9]/.test(text[i + 1] ?? '');
    if (c === '"' || (c === "'" && !digitSeparator)) {
      let end = i + 1;
      for (; end < text.length; end++) {
        if (text[end] === '\\') { end++; continue; }
        if (text[end] === c) break;
      }
      need(end < text.length, 'STRING_UNTERMINATED');
      i = end;
      continue;
    }
    if (c === '{') depth++;
    else if (c === '}' && --depth === 0) return {text: text.slice(start, i + 1), start, end: i + 1};
  }
  throw Error('OWNERSHIP_BODY_UNTERMINATED');
}
function sourceFile(root, relative, cache) {
  need(relativePath(relative), 'SOURCE_PATH');
  const file = path.resolve(root, relative);
  need(inside(root, file) && inside(fs.realpathSync(root), fs.realpathSync(file)), 'SOURCE_ESCAPE');
  if (!cache.has(file)) cache.set(file, fs.readFileSync(file));
  return cache.get(file);
}
export function inspectOwnershipBinding(repoRoot, binding, cache = new Map()) {
  const bytes = sourceFile(path.resolve(repoRoot), binding.path, cache);
  const normalize = binding.lineEndings === 'lf';
  need(binding.lineEndings === undefined || normalize, 'SOURCE_TEXT_NORMALIZATION');
  need(!normalize || ['braced','between'].includes(binding.selector?.kind), 'NORMALIZATION_REQUIRES_COMPLETE_SOURCE_REGION');
  const text = normalize ? bytes.toString('utf8').replaceAll('\r\n','\n') : bytes.toString('utf8');
  const selector = normalize ? Object.fromEntries(Object.entries(binding.selector).map(([key,value]) => [key, typeof value === 'string' ? value.replaceAll('\r\n','\n') : value])) : binding.selector;
  let selected, start = 0, end = text.length;
  if (selector.kind === 'file') selected = bytes;
  else if (selector.kind === 'braced') {
    const body = braced(text, selector.anchor);
    selected = body.text; start = body.start; end = body.end;
  } else if (selector.kind === 'between') {
    start = once(text, selector.start); end = once(text, selector.end);
    need(end > start, 'RANGE_ORDER');
    selected = text.slice(start, end);
  } else if (selector.kind === 'json-pointer') {
    selected = JSON.stringify(canonical(pointer(JSON.parse(text.replace(/^\ufeff/, '')), selector.pointer)));
  } else if (selector.kind === 'json-id') {
    const rows = pointer(JSON.parse(text.replace(/^\ufeff/, '')), selector.collection);
    need(Array.isArray(rows), 'JSON_ID_COLLECTION');
    const found = rows.filter(row => row.id === selector.id);
    need(found.length === 1, 'JSON_ID_MISSING_OR_DUPLICATE');
    selected = JSON.stringify(canonical(found[0]));
  } else throw Error('OWNERSHIP_SELECTOR_KIND');
  return {
    sha256: digest(selected), text: Buffer.isBuffer(selected) ? selected.toString('utf8') : selected,
    rawFileSha256: digest(bytes), sourceNormalization: normalize ? 'lf-source-region-v1' : 'raw-bytes-v1',
    startLine: text.slice(0, start).split('\n').length, endLine: text.slice(0, end).split('\n').length,
  };
}
export function validateOwnershipCatalog(data) {
  need(data?.schemaVersion === 1 && Array.isArray(data.features) && data.features.length > 0, 'SCHEMA');
  need(data.bindings && typeof data.bindings === 'object' && !Array.isArray(data.bindings), 'BINDINGS');
  for (const [id, binding] of Object.entries(data.bindings)) {
    need(id && relativePath(binding.path) && /^[a-f0-9]{64}$/.test(binding.sha256 ?? ''), 'BINDING_IDENTITY');
    need(['file', 'braced', 'between', 'json-pointer', 'json-id'].includes(binding.selector?.kind), 'BINDING_SELECTOR');
    need(binding.lineEndings === undefined || (binding.lineEndings === 'lf' && ['braced','between'].includes(binding.selector.kind)), 'SOURCE_TEXT_NORMALIZATION');
  }
  const ids = new Set();
  for (const feature of data.features) {
    need(nonempty(feature.id) && !ids.has(feature.id), 'FEATURE_ID_DUPLICATE');
    ids.add(feature.id);
    need(classifications.includes(feature.persistence), 'CLASSIFICATION_REQUIRED');
    need(typeof feature.rationale === 'string' && feature.rationale.length >= 40, 'CLASSIFICATION_REASON');
    need(Array.isArray(feature.bindingIds) && feature.bindingIds.length > 0
      && feature.bindingIds.every(id => data.bindings[id]), 'FEATURE_BINDINGS');
    need(Array.isArray(feature.resources) && Array.isArray(feature.anchors)
      && Array.isArray(feature.testReferences) && feature.testReferences.every(nonempty)
      && Array.isArray(feature.gaps), 'FEATURE_DATA');
    const anchorIds = new Set();
    for (const anchor of feature.anchors) {
      need(nonempty(anchor.id) && !anchorIds.has(anchor.id), 'ANCHOR_ID');
      anchorIds.add(anchor.id);
      need(data.bindings[anchor.bindingId] && feature.bindingIds.includes(anchor.bindingId)
        && nonempty(anchor.symbol) && ['owner', 'consumer'].includes(anchor.role), 'ANCHOR_BINDING');
    }
    for (const resource of feature.resources) {
      need(anchorIds.has(resource.ownerAnchor) && ['operation', 'session', 'cross-event', 'process'].includes(resource.lifetime)
        && nonempty(resource.kind) && nonempty(resource.groupId) && nonempty(resource.responsibility), 'RESOURCE_OWNER');
    }
    if (feature.persistence === 'one-shot') {
      need(!feature.resources.some(resource => resource.lifetime !== 'operation'
        || ['subscriptions', 'periodic-timers', 'resident-processes'].includes(resource.kind)), 'ONE_SHOT_PERSISTENT_RESOURCE');
    }
    if (feature.persistence !== 'unknown') {
      need(feature.anchors.some(a => a.role === 'owner' && !data.bindings[a.bindingId].path.startsWith('capabilities/')), 'ACTUAL_OWNER_REQUIRED');
      need(feature.anchors.some(a => a.role === 'consumer' && !data.bindings[a.bindingId].path.startsWith('capabilities/')), 'ACTUAL_CONSUMER_REQUIRED');
      need(feature.resources.length > 0, 'OWNED_RESOURCES_REQUIRED');
    }
    for (const kind of ['exit', 'sleep', 'faultRecovery']) {
      need(Array.isArray(feature.lifecycle?.[kind]), 'LIFECYCLE_ENTRY_LIST');
      for (const id of feature.lifecycle[kind]) need(anchorIds.has(id), 'LIFECYCLE_ANCHOR_MISSING');
    }
    const componentIds = new Set();
    for (const component of feature.components ?? []) {
      need(nonempty(component.id) && !componentIds.has(component.id) && anchorIds.has(component.anchor)
        && classifications.includes(component.persistence) && nonempty(component.reason), 'COMPONENT_CLASSIFICATION');
      componentIds.add(component.id);
    }
  }
  return data;
}
function registrations(repoRoot, options) {
  if (options.adapters || options.registeredTestIds) {
    const ids = options.adapters ? options.adapters.map(row => row.id) : options.registeredTestIds;
    need(Array.isArray(ids) && ids.every(nonempty) && new Set(ids).size === ids.length, 'REGISTRATION_IDS');
    return {ids: new Set(ids), count: ids.length, authority: 'caller-provided-routing-identities-only', executionEvidence: false};
  }
  const file = path.join(repoRoot, 'tools/ymcc_sim/global/extra-adapter-index.json');
  try {
    const bytes = fs.readFileSync(file), index = JSON.parse(bytes);
    const ids = index.groups.flatMap(group => group.rows.map(row => row.id));
    need(ids.every(nonempty) && new Set(ids).size === ids.length, 'REGISTRATION_DUPLICATE');
    return {
      ids: new Set(ids), count: ids.length, authority: 'extra-index-routing-identities-only-not-execution',
      file, sha256: digest(bytes), sourceBoundRoutingNotRevalidated: true, executionEvidence: false,
    };
  } catch (error) {
    return {ids: new Set(), count: 0, authority: 'unavailable', diagnostic: String(error.message), executionEvidence: false};
  }
}
/** catalog is an explicit caller-supplied source-review dataset (also used for
 * refusal fixtures); the default dataset additionally requires its pinned byte
 * hash. Neither data source is execution authority. No option disables source
 * checks. Any stale feature and its components become unknown, with no suggested
 * registered checks until reviewed against the changed consumer sources.
 */
export function queryOwnershipCatalog(options = {}) {
  const repoRoot = path.resolve(options.repoRoot ?? path.join(HERE, '../../../..'));
  const mode = options.mode ?? 'inventory';
  need(['inventory', 'plan'].includes(mode), 'MODE');
  let data, invalid = null, fallbackIds = [], catalogIdentity = null;
  try {
    const bytes = options.catalog ? null : fs.readFileSync(path.join(HERE, 'ownership-catalog.json'));
    const candidate = options.catalog ?? JSON.parse(bytes);
    catalogIdentity = bytes
      ? {authority: 'pinned-default-source-review', expectedSha256: CATALOG_SHA256, observedSha256: digest(bytes)}
      : {authority: 'caller-provided-source-review-not-execution', semanticSha256: digest(JSON.stringify(canonical(candidate)))};
    fallbackIds = (candidate?.features ?? []).map(f => f.id).filter(nonempty);
    if (bytes) need(digest(bytes) === CATALOG_SHA256, 'CATALOG_IDENTITY_CHANGED');
    data = validateOwnershipCatalog(candidate);
  } catch (error) {
    invalid = String(error.message);
    data = {features: [], bindings: {}, inactive: []};
  }
  const ids = options.ids ?? (invalid ? fallbackIds : data.features.map(f => f.id));
  need(Array.isArray(ids) && ids.every(nonempty), 'QUERY_IDS');
  const registration = registrations(repoRoot, options);
  const cache = new Map(), observations = new Map(), finalCache = new Map();
  const selected = ids.map(id => data.features.find(feature => feature.id === id));
  for (const feature of selected.filter(Boolean)) {
    for (const key of feature.bindingIds) {
      if (observations.has(key)) continue;
      const binding = data.bindings[key];
      try {
        const current = inspectOwnershipBinding(repoRoot, binding, cache);
        observations.set(key, {...current, status: current.sha256 === binding.sha256 ? 'CURRENT' : 'STALE'});
      } catch (error) {
        observations.set(key, {status: 'STALE', error: String(error.message)});
      }
    }
  }
  // Re-read the consumed regions at query completion. Edits outside a bound
  // function are not changes to that function's reviewed semantics.
  for (const [key, observation] of observations) {
    try {
      const after = inspectOwnershipBinding(repoRoot, data.bindings[key], finalCache);
      if (after.sha256 !== observation.sha256) Object.assign(observation, {status: 'STALE', error: 'SOURCE_CHANGED_DURING_QUERY'});
    } catch (error) {
      Object.assign(observation, {status: 'STALE', error: String(error.message)});
    }
  }
  const features = ids.map((id, index) => {
    const feature = selected[index];
    if (!feature) {
      const inactive = data.inactive?.find(row => row.id === id);
      return {
        id, active: false, persistence: 'unknown', status: inactive ? 'INACTIVE' : 'UNKNOWN',
        rationale: inactive?.reason ?? 'Not classified by this bounded catalog.',
        gaps: [invalid ?? 'UNCLASSIFIED_CAPABILITY'], testReferences: [], registeredTestIds: [], executionAcceptance: false,
      };
    }
    const stale = feature.bindingIds.filter(key => observations.get(key)?.status !== 'CURRENT');
    const anchorErrors = feature.anchors.filter(a => !observations.get(a.bindingId)?.text?.includes(data.bindings[a.bindingId].lineEndings === 'lf' ? a.symbol.replaceAll('\r\n','\n') : a.symbol)).map(a => a.id);
    const unknown = feature.persistence === 'unknown', current = !stale.length && !anchorErrors.length;
    const status = current ? (unknown ? 'UNKNOWN' : 'CLASSIFIED_SOURCE_BOUND') : 'STALE';
    const registered = feature.testReferences.filter(id => registration.ids.has(id));
    const missing = feature.testReferences.filter(id => !registration.ids.has(id));
    return {
      ...structuredClone(feature), persistence: current ? feature.persistence : 'unknown', status,
      components: (feature.components ?? []).map(component => ({
        ...component, persistence: current && !unknown ? component.persistence : 'unknown', status,
      })),
      classificationEvidence: 'source-review-only-not-execution', staleBindings: stale, unresolvedAnchors: anchorErrors,
      registeredTestIds: current && !unknown ? registered : [], unregisteredTestIds: missing,
      gaps: [
        ...feature.gaps, ...stale.map(id => 'SOURCE_BINDING_STALE:' + id),
        ...anchorErrors.map(id => 'CONSUMER_ANCHOR_NOT_FOUND:' + id), ...missing.map(id => 'REGISTRATION_NOT_IN_SUPPLIED_SET:' + id),
      ],
      sourceBindings: feature.bindingIds.map(id => {
        const binding = data.bindings[id], observation = observations.get(id);
        return {
          id, ...binding, observedSha256: observation?.sha256, observedRawFileSha256: observation?.rawFileSha256, sourceNormalization: observation?.sourceNormalization, status: observation?.status, error: observation?.error,
          startLine: observation?.startLine, endLine: observation?.endLine,
        };
      }),
      executionAcceptance: false,
    };
  });
  const {ids: registrationIds, ...registrationInfo} = registration;
  return {
    schemaVersion: 1, kind: 'ymcc-capability-ownership-query', mode,
    status: invalid ? 'UNKNOWN' : features.some(f => f.status === 'STALE') ? 'STALE'
      : features.some(f => f.status === 'UNKNOWN') ? 'UNKNOWN' : 'SOURCE_REVIEW_ONLY',
    diagnostics: invalid ? [invalid] : [], catalogIdentity, repoRoot, features, inactive: structuredClone(data.inactive ?? []),
    registration: registrationInfo, otherCapabilities: 'UNCLASSIFIED_NOT_ACCEPTED', executedTests: 0,
    automaticExecution: false, softwareAcceptance: false, hardwareAcceptance: false, cpuPowerAcceptance: false,
    ...(mode === 'plan' ? {checksToConsider: features.map(feature => ({
      capabilityId: feature.id, registeredTestIds: feature.registeredTestIds,
      remainingRequirements: feature.gaps, automaticExecution: false,
    }))} : {}),
  };
}
export const getOwnershipInventory = options => queryOwnershipCatalog({...options, mode: 'inventory'});
export const getOwnershipPlan = options => queryOwnershipCatalog({...options, mode: 'plan'});
if (process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url) {
  const args = process.argv.slice(2), options = {};
  for (let i = 0; i < args.length; i += 2) {
    need(i + 1 < args.length, 'CLI_VALUE');
    if (args[i] === '--repo') options.repoRoot = args[i + 1];
    else if (args[i] === '--mode') options.mode = args[i + 1];
    else if (args[i] === '--ids') options.ids = args[i + 1].split(',');
    else throw Error('OWNERSHIP_CLI_ARGUMENT');
  }
  console.log(JSON.stringify(queryOwnershipCatalog(options), null, 2));
}
