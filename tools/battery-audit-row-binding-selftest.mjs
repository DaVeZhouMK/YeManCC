import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const [crosswalkPath, factsPath, scratchPath] = process.argv.slice(2);
assert.equal(process.argv.length, 5,
  'usage: node tools/battery-audit-row-binding-selftest.mjs <pinned crosswalk> <reviewed field facts> <new scratch directory>');
const repo = fileURLToPath(new URL('../', import.meta.url));
const scratch = path.resolve(scratchPath);
const familyTool = 'tools/battery-oem-family-source-audit.mjs';
const fieldTool = 'tools/battery-field-route-matrix.mjs';
const catalogPath = 'capabilities/hc-device-catalog.json';
const routesPath = 'native/BatteryHost/battery-routes.json';
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const write = (file, contents) => fs.writeFileSync(file, JSON.stringify(contents) + '\n');
const { inputs } = read(crosswalkPath);
const refs = Object.fromEntries(['hcAudit', 'oneX', 'aya', 'oxpec'].map(name => {
  assert.equal(digest(fs.readFileSync(inputs[name].path)), inputs[name].sha256, name);
  return [name, inputs[name].path];
}));
fs.mkdirSync(scratch);
const pins = {};
for (const file of [familyTool, fieldTool, 'tools/lib/battery_onex_oem_audit.mjs',
  'tools/lib/battery_route_identity_audit.mjs', catalogPath, routesPath]) {
  const target = path.join(scratch, file);
  fs.mkdirSync(path.dirname(target), { recursive: true });
  const bytes = fs.readFileSync(path.join(repo, file));
  pins[file] = digest(bytes);
  fs.writeFileSync(target, bytes);
}
const require = createRequire(import.meta.url);
fs.mkdirSync(path.join(scratch, 'node_modules'));
fs.symlinkSync(path.dirname(require.resolve('typescript/package.json')), path.join(scratch, 'node_modules/typescript'),
  process.platform === 'win32' ? 'junction' : 'dir');
const run = (tool, ...args) => {
  const result = spawnSync(process.execPath, [path.join(scratch, tool), ...args], {
    cwd: scratch, encoding: 'utf8', timeout: 30_000, windowsHide: true,
  });
  if (result.error) throw result.error;
  return result;
};
const matrix = path.join(scratch, 'FIELD-MATRIX.json');
const field = out => run(fieldTool, path.resolve(factsPath), out);
const family = (input, hc = refs.hcAudit) => run(familyTool, hc, input, refs.oneX, refs.aya, refs.oxpec);
const success = ({ status, stderr }) => assert.equal(status, 0, stderr);
const evidence = (result, message) => ({ status: result.status, noStdout: result.stdout === '', rejectedBy: result.stderr.includes(message) });
success(field(matrix));
const baseline = family(matrix);
success(baseline);
fs.writeFileSync(path.join(scratch, 'CROSSWALK.json'), baseline.stdout);
const a1x = data => data.oneX.branches.flatMap(x => x.labels).find(x => x.product === 'AOKZOE A1X');
assert.deepEqual(a1x(JSON.parse(baseline.stdout)).routeLiteralMatches, []);
const catalogFile = path.join(scratch, catalogPath);
const original = fs.readFileSync(catalogFile);
const minipro = rows => rows.find(r => r.deviceClass === 'OneXPlayerX2MiniPro');
const cases = [];
for (const [name, mutate] of [
  ['false-a1x-literal', rows => Object.assign(minipro(rows).identity, { manufacturers: ['AOKZOE'], values: ['AOKZOE A1X'] })],
  ['mini-pro-firmware', rows => { minipro(rows).firmwareGate.bios = '0.23'; }],
  ['provider', rows => { minipro(rows).provider = 'oxp-x1-amd-ec'; }],
  ['rule-id', rows => { minipro(rows).ruleId += '.bogus'; }],
  ['row-order', rows => { [rows[0], rows[1]] = [rows[1], rows[0]]; }],
  ['row-count', rows => { rows.pop(); }],
]) {
  const catalog = JSON.parse(original);
  mutate(catalog.domains.battery.rows);
  const { catalogSha256: old, ...metadata } = catalog.domains.battery;
  catalog.domains.battery.catalogSha256 = digest(JSON.stringify(metadata));
  assert.equal(catalog.domains.battery.ruleDocumentSha256, pins[routesPath]);
  write(catalogFile, catalog);
  const output = path.join(scratch, name + '-matrix.json');
  const fieldResult = field(output);
  const familyResult = family(matrix);
  const record = { name, field: evidence(fieldResult, 'battery audit route'),
    family: evidence(familyResult, 'battery audit route'), matrixWritten: fs.existsSync(output) };
  if (name === 'false-a1x-literal' && familyResult.status === 0) {
    record.falseMatches = a1x(JSON.parse(familyResult.stdout)).routeLiteralMatches;
    fs.writeFileSync(path.join(scratch, 'FALSE-A1X-CROSSWALK.json'), familyResult.stdout);
  }
  cases.push(record);
}
fs.writeFileSync(catalogFile, original);
const badMatrix = read(matrix);
const profile = badMatrix.profiles.find(p => p.identity.manufacturer.toUpperCase().includes('ONE-NETBOOK'));
assert.ok(profile);
profile.selection = {status: 'unique', ruleId: 'battery.rule.nonexistent'};
const falseMatrix = path.join(scratch, 'FALSE-SELECTION.json');
write(falseMatrix, badMatrix);
const staleMatrix = evidence(family(falseMatrix), 'field matrix selection differs');
const legacyMatrix = path.join(scratch, 'LEGACY-MATRIX.json');
write(legacyMatrix, {...read(matrix), schemaVersion: 1});
const legacy = evidence(family(legacyMatrix), 'field matrix predates unique-only selection');
const authorityCases = [];
for (const [name, mutate, expected] of [
  ['changed-field-firmware', field => {
    const profile = field.profiles.find(p => p.identity.boardProduct === 'ONEXPLAYER X2Mini PRO');
    assert.ok(profile);
    profile.observedFirmware = [{bios: '0.99', ecMajor: 0, ecMinor: 99}];
  }, 'battery crosswalk field matrix is not reviewed'],
  ['removed-field-model', field => {
    const before = field.profiles.length;
    field.profiles = field.profiles.filter(p => p.identity.boardProduct !== 'ONEXPLAYER X2Mini PRO');
    assert.ok(field.profiles.length < before);
  }, 'battery crosswalk field matrix is not reviewed'],
]) {
  const changed = read(matrix);
  mutate(changed);
  assert.equal(changed.ruleDocumentSha256, pins[routesPath]);
  const file = path.join(scratch, name + '.json');
  write(file, changed);
  const result = family(file);
  if (result.status === 0) fs.writeFileSync(path.join(scratch, name + '-crosswalk.json'), result.stdout);
  authorityCases.push({name, ...evidence(result, expected)});
}
const changedHc = read(refs.hcAudit);
const g1 = changedHc.datasets.locked.rows.find(r => r.deviceClass === 'OneXPlayerG1AMD');
assert.ok(g1 && g1.effectiveStatus === 'not-declared');
g1.effectiveStatus = 'declared-runtime-probe-required';
assert.equal(changedHc.runtimeRouteSha256, pins[routesPath]);
const changedHcPath = path.join(scratch, 'hc-g1-changed.json');
write(changedHcPath, changedHc);
const hcResult = family(matrix, changedHcPath);
if (hcResult.status === 0) fs.writeFileSync(path.join(scratch, 'hc-g1-changed-crosswalk.json'), hcResult.stdout);
authorityCases.push({name: 'changed-hc-g1-status', ...evidence(hcResult, 'battery crosswalk HC audit is not reviewed')});
assert.equal(digest(fs.readFileSync(path.join(scratch, routesPath))), pins[routesPath]);
console.log(JSON.stringify({ pins, fieldFacts: digest(fs.readFileSync(factsPath)),
  baseline: JSON.parse(baseline.stdout).oneX.summary, cases, staleMatrix, legacy, authorityCases }, null, 2));
assert.ok(cases.every(c => c.field.status === 1 && c.field.rejectedBy && !c.matrixWritten &&
  c.family.status === 1 && c.family.rejectedBy && c.family.noStdout), 'route-drift negative controls');
assert.deepEqual(staleMatrix, { status: 1, noStdout: true, rejectedBy: true });
assert.deepEqual(legacy, { status: 1, noStdout: true, rejectedBy: true });
assert.ok(authorityCases.every(c => c.status === 1 && c.noStdout && c.rejectedBy), 'unreviewed HC/field input controls');
