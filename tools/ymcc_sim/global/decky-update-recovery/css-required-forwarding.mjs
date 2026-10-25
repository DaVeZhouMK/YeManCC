import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const REQUIRED = Object.freeze([
  'appearance-retired-ui.json',
  'appearance-observe-only-validation.json',
  'appearance-refresh/result.json',
  'appearance-native/result.json',
]);
const VALIDATION = REQUIRED[1];
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const need = (ok, why) => { if (!ok) throw Error(`CSS_REQUIRED_FORWARDING_${why}`); };
const identity = file => process.platform === 'win32' ? file.toLowerCase() : file;
const inside = (root, file) => {
  const relative = path.relative(root, file);
  return relative !== '' && relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
};
function exactAbsolute(file, label) {
  need(typeof file === 'string' && path.isAbsolute(file) && file === path.resolve(file), `${label}_NONCANONICAL_PATH`);
  if (process.platform === 'win32') need(!file.slice(2).includes(':') && !/[. ](?:[\\/]|$)/.test(file), `${label}_PATH_ALIAS`);
  return file;
}
function plain(file, caseDir, type) {
  need(file === caseDir || inside(caseDir, file), `OUTSIDE_CASE:${file}`);
  need(fs.realpathSync(file) === file, `REALPATH_ALIAS_OR_ESCAPE:${file}`);
  for (let current = file; ; current = path.dirname(current)) {
    need(!fs.lstatSync(current).isSymbolicLink(), `REPARSE_ALIAS_OR_ESCAPE:${current}`);
    if (current === caseDir) break;
  }
  const stat = fs.statSync(file);
  need(type === 'directory' ? stat.isDirectory() : stat.isFile() && stat.nlink === 1, `TYPE_OR_HARDLINK_ALIAS:${file}`);
  return stat;
}
function exists(file) {
  try { fs.lstatSync(file); return true; } catch (error) { if (error.code === 'ENOENT') return false; throw error; }
}
// JSON.parse alone would silently discard duplicate sourceHashes/map object keys.
function parseJson(bytes, label) {
  const text = bytes.toString('utf8').replace(/^\uFEFF/, '');
  const result = JSON.parse(text);
  const tokens = text.match(/"(?:[^"\\]|\\.)*"|[{}\[\]:,]|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|true|false|null/g);
  let offset = 0;
  function visit() {
    const token = tokens[offset++];
    if (token === '{') {
      const keys = new Set();
      while (tokens[offset] !== '}') {
        const key = JSON.parse(tokens[offset++]);
        need(!keys.has(key), `${label}_DUPLICATE_JSON_KEY:${key}`);
        keys.add(key); offset++; visit();
        if (tokens[offset] !== ',') break;
        offset++;
      }
      offset++;
    } else if (token === '[') {
      while (tokens[offset] !== ']') {
        visit();
        if (tokens[offset] !== ',') break;
        offset++;
      }
      offset++;
    }
  }
  visit();
  return result;
}
function sha(value, label) {
  need(typeof value === 'string' && /^[a-f0-9]{64}$/.test(value), `${label}_SHA256`);
}

/**
 * Publish the four required CSS receipts into an existing exact case output.
 * cssRaw is workerReceipt.raw.originalRaw. Its pinned map is authoritative;
 * files must agree with that map. Only the validation sourceHashes keys change.
 * This synchronous API never opens the original Workspace source paths.
 * Invalid input/existing outputs fail before publication; an I/O error during
 * publication may leave exclusive partial outputs for the caller to preserve.
 */
export function forwardCssRequiredReceipts({ cssRaw, out }) {
  exactAbsolute(out, 'OUT');
  need(path.basename(out) === 'decky-update-recovery', 'EXACT_OUTPUT_DIRECTORY');
  const caseDir = path.dirname(out);
  plain(caseDir, caseDir, 'directory');
  plain(out, caseDir, 'directory');
  const canonical = path.join(out, 'css-appearance-worker', 'css');
  const mapPath = path.join(out, 'css-appearance-worker', 'CSS-RAW-MAP.json');
  need(cssRaw && cssRaw.canonical === canonical && cssRaw.map?.path === mapPath, 'EXACT_CANONICAL_AND_MAP_OWNER');
  plain(canonical, caseDir, 'directory');
  plain(mapPath, caseDir, 'file');
  sha(cssRaw.map.sha256, 'MAP');
  const mapBytes = fs.readFileSync(mapPath);
  need(digest(mapBytes) === cssRaw.map.sha256, 'MAP_CHANGED');
  const map = parseJson(mapBytes, 'MAP');
  need(map.schemaVersion === 1 && map.byteExact === true && map.canonical === canonical && Array.isArray(map.files), 'MAP_CONTRACT');
  exactAbsolute(map.source, 'MAP_SOURCE');
  const bySource = new Map(), byPath = new Map();
  function checkRow(row, sources, destinations) {
    need(row && typeof row === 'object', 'INVALID_MAPPING');
    exactAbsolute(row.source, 'MAPPING_SOURCE'); exactAbsolute(row.path, 'MAPPING_DESTINATION');
    sha(row.sha256, 'MAPPING');
    need(Number.isSafeInteger(row.bytes) && row.bytes >= 0, 'MAPPING_BYTES');
    need(inside(map.source, row.source) && inside(canonical, row.path), 'MAPPING_OUTSIDE_CANONICAL');
    need(row.path === path.join(canonical, path.relative(map.source, row.source)), 'MAPPING_RELATIVE_ALIAS');
    need(!sources.has(identity(row.source)) && !destinations.has(identity(row.path)), 'DUPLICATE_MAPPING_OR_ALIAS');
    sources.set(identity(row.source), row); destinations.set(identity(row.path), row);
  }
  for (const row of map.files) checkRow(row, bySource, byPath);
  // The worker's in-memory files list cannot replace or fill gaps in the map.
  need(Array.isArray(cssRaw.files) && cssRaw.files.length === map.files.length, 'FILES_MAP_MISMATCH');
  const receiptSources = new Map(), receiptPaths = new Map();
  for (const row of cssRaw.files) {
    checkRow(row, receiptSources, receiptPaths);
    const authoritative = bySource.get(identity(row.source));
    need(authoritative && ['source', 'path', 'sha256', 'bytes'].every(key => row[key] === authoritative[key]), 'FILES_MAP_MISMATCH');
  }
  const snapshots = new Map([[mapPath, mapBytes]]);
  function readCanonical(row) {
    const stat = plain(row.path, caseDir, 'file');
    const bytes = fs.readFileSync(row.path);
    need(stat.size === row.bytes && bytes.length === row.bytes, `CANONICAL_BYTES_CHANGED:${row.path}`);
    need(digest(bytes) === row.sha256, `CANONICAL_SHA_CHANGED:${row.path}`);
    snapshots.set(row.path, bytes);
    return bytes;
  }
  const plans = REQUIRED.map(relative => {
    const source = path.join(canonical, relative), destination = path.join(out, relative);
    const row = byPath.get(identity(source));
    need(row && row.path === source, `REQUIRED_MAPPING_MISSING:${relative}`);
    const bytes = readCanonical(row);
    return { relative, source, destination, bytes, originalSha256: row.sha256, byteExact: true, derived: false };
  });
  const validation = plans.find(plan => plan.relative === VALIDATION);
  const wrapper = parseJson(validation.bytes, 'VALIDATION');
  need(wrapper && typeof wrapper === 'object' && !Array.isArray(wrapper) && !Object.hasOwn(wrapper, 'cssRequiredForwarding'), 'VALIDATION_CONTRACT');
  need(wrapper.sourceHashes && typeof wrapper.sourceHashes === 'object' && !Array.isArray(wrapper.sourceHashes), 'VALIDATION_SOURCE_HASHES');
  const entries = Object.entries(wrapper.sourceHashes);
  need(entries.length === 3, 'VALIDATION_THREE_SOURCES_REQUIRED');
  const sourceHashes = Object.create(null), dependencies = [], seenSources = new Set(), seenPaths = new Set();
  for (const [source, expectedSha] of entries) {
    exactAbsolute(source, 'VALIDATION_SOURCE'); sha(expectedSha, 'VALIDATION_SOURCE');
    const row = bySource.get(identity(source));
    need(row && row.source === source, `SOURCE_MAPPING_MISSING_OR_ALIAS:${source}`);
    need(!seenSources.has(identity(source)) && !seenPaths.has(identity(row.path)), 'DEPENDENCY_DUPLICATE_OR_ALIAS');
    seenSources.add(identity(source)); seenPaths.add(identity(row.path));
    need(row.sha256 === expectedSha, `DEPENDENCY_SHA_MISMATCH:${source}`);
    need(!plans.some(plan => plan.source === row.path), 'DEPENDENCY_RECEIPT_ALIAS');
    readCanonical(row);
    sourceHashes[row.path] = expectedSha;
    dependencies.push({ source, destination: row.path, sha256: expectedSha, bytes: row.bytes });
  }
  const provenance = {
    schemaVersion: 1,
    kind: 'derived-css-required-validation',
    originalValidation: { path: validation.source, sha256: validation.originalSha256, bytes: validation.bytes.length },
    map: { path: mapPath, sha256: cssRaw.map.sha256 },
    sourceMappings: dependencies,
  };
  validation.bytes = Buffer.from(JSON.stringify({ ...wrapper, sourceHashes, cssRequiredForwarding: provenance }, null, 2) + '\n');
  validation.byteExact = false; validation.derived = true; validation.provenance = provenance;

  // Preflight every destination before creating any output or output directory.
  for (const plan of plans) {
    need(!exists(plan.destination), `OUTPUT_EXISTS:${plan.destination}`);
    let parent = path.dirname(plan.destination);
    while (!exists(parent)) parent = path.dirname(parent);
    plain(parent, caseDir, 'directory');
  }
  const unchanged = () => {
    for (const [file, bytes] of snapshots) {
      plain(file, caseDir, 'file');
      need(fs.readFileSync(file).equals(bytes), `INPUT_CHANGED_DURING_FORWARDING:${file}`);
    }
  };
  unchanged();
  const records = [];
  for (const plan of plans) {
    const parent = path.dirname(plan.destination);
    fs.mkdirSync(parent, { recursive: true }); plain(parent, caseDir, 'directory');
    fs.writeFileSync(plan.destination, plan.bytes, { flag: 'wx' });
    plain(plan.destination, caseDir, 'file');
    need(fs.readFileSync(plan.destination).equals(plan.bytes), `OUTPUT_CHANGED:${plan.relative}`);
    records.push({
      relative: plan.relative, source: plan.source, destination: plan.destination,
      sha256: digest(plan.bytes), bytes: plan.bytes.length, originalSha256: plan.originalSha256,
      byteExact: plan.byteExact, derived: plan.derived,
      ...(plan.provenance ? { provenance: plan.provenance } : {}),
    });
  }
  unchanged();
  return records;
}
