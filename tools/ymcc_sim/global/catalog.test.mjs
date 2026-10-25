import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { buildCatalog } from './catalog.mjs';

function fixture(t, files = {}) {
  const temporaryParent = fs.realpathSync(os.tmpdir());
  const root = fs.mkdtempSync(path.join(temporaryParent, 'ymcc-catalog-'));
  const write = (name, content) => {
    const absolute = path.resolve(root, name);
    assert.ok(absolute.startsWith(root + path.sep));
    fs.mkdirSync(path.dirname(absolute), { recursive: true });
    fs.writeFileSync(absolute, content, 'utf8');
  };
  write('package.json', JSON.stringify({ type: 'module', scripts: { 'test:fan': 'node NEVER_EXECUTE.mjs' } }, null, 2));
  for (const [file, content] of Object.entries(files)) write(file, content);
  t.after(() => {
    const target = fs.realpathSync(root);
    assert.equal(path.dirname(target), temporaryParent);
    assert.ok(path.basename(target).startsWith('ymcc-catalog-'));
    fs.rmSync(target, { recursive: true, force: true });
  });
  return { root, write, catalog: adapters => buildCatalog({ repoRoot: root, adapters }) };
}
const find = (catalog, kind, operation) => catalog.surfaces.find(surface => surface.kind === kind && surface.operation === operation);
const adapter = (id, evidenceInputs, scope = 'behavioral-mock') => ({ id, domain: 'fan', scope, evidenceInputs });

test('discovers Vue views, routes, bridge exports, IPC, native operations and package scripts without executing source', t => {
  const f = fixture(t, {
    'src/views/FanView.vue': '<template><button>Fan</button></template>\n<script setup lang="ts">\nimport { invoke } from "../bridge/ipc";\ninvoke("fan.enable");\n</script>',
    'src/router.ts': 'const routes = [{ path: "/fan", component: () => import("./views/FanView.vue") }];',
    'src/bridge/fan.ts': 'throw new Error("SOURCE_MUST_NOT_RUN");\nexport function readFan() { return invoke("fan.read"); }',
    'native/dispatch.h': 'ipc_on("display.setMode", handler);\nif (operation == "audio.volume.set") {}',
  });
  const c = f.catalog();
  assert.equal(c.schemaVersion, 1);
  for (const [kind, operation] of [['view', undefined], ['route', '/fan'], ['bridge-export', 'readFan'], ['ipc-call', 'fan.read'], ['native-ipc', 'display.setMode'], ['native-operation', 'audio.volume.set'], ['package-script', 'test:fan']]) assert.ok(find(c, kind, operation), `${kind}: ${operation}`);
  assert.equal(find(c, 'ipc-call', 'fan.enable').line, 4);
  assert.equal(find(c, 'native-ipc', 'display.setMode').domain, 'display');
  assert.equal(find(c, 'native-operation', 'audio.volume.set').domain, 'audio');
  assert.ok(c.surfaces.every(surface => surface.coverage === 'NOT_COVERED' && surface.adapterIds.length === 0));
  assert.equal(c.summary.uncovered, c.summary.surfaces);
  assert.equal(c.summary.registrationComplete, false);
  assert.equal(c.summary.behavioralVerified, false);
  assert.deepEqual(c.summary.discovery.diagnostics, []);
});

test('new feature files, existing-file IPC, new native headers and package scripts are discovered on the next invocation', t => {
  const f = fixture(t, { 'src/bridge/fan.ts': 'export const read = () => invoke("fan.get");' });
  const before = f.catalog([adapter('fan.test', ['src/bridge/fan.ts'])]);
  f.write('src/views/NovelView.vue', '<template><div>new feature</div></template>');
  f.write('src/bridge/fan.ts', 'export const read = () => invoke("fan.get");\nexport const set = () => invoke("fan.newCommand");');
  f.write('native/new-feature.h', 'ipc_on("newSubsystem.enable", callback);');
  f.write('package.json', '{"scripts":{"new:feature":"DO_NOT_RUN"}}');
  const after = f.catalog([adapter('fan.test', ['src/bridge/fan.ts'])]);
  assert.ok(after.summary.surfaces > before.summary.surfaces);
  assert.ok(find(after, 'ipc-call', 'fan.newCommand'));
  assert.ok(find(after, 'native-ipc', 'newSubsystem.enable'));
  assert.equal(find(after, 'native-ipc', 'newSubsystem.enable').coverage, 'NOT_COVERED');
  assert.equal(find(after, 'package-script', 'new:feature').coverage, 'NOT_COVERED');
  assert.ok(after.summary.uncovered > before.summary.uncovered);
});

test('AST handles import aliases, namespaces, templates, concatenation and finite alternatives', t => {
  const f = fixture(t, { 'src/bridge/fan.ts': [
    'import { invoke as rpc, on as listen } from "./ipc";',
    'import * as host from "./ipc";',
    'const suffix = "get" as const;',
    'rpc(`fan.${suffix}`);',
    'rpc("battery." + "charge.get");',
    'host.invoke(flag ? "display.get" : "display.set");',
    'listen("fan.changed", () => {});',
    'host.on("fan.state", () => {});',
  ].join('\n') });
  const c = f.catalog();
  for (const operation of ['fan.get', 'battery.charge.get', 'display.get', 'display.set']) assert.ok(find(c, 'ipc-call', operation), operation);
  for (const operation of ['fan.changed', 'fan.state']) assert.ok(find(c, 'ipc-event', operation));
  assert.equal(c.summary.dynamic, 0);
});

test('unresolved templates, native computed registrations and runtime operation branches stay visible', t => {
  const f = fixture(t, {
    'src/bridge/battery.ts': 'export function call(operation: string) { return invoke(`battery.charge.${operation}`); }\ninvoke(flag ? "battery.get" : runtimeCommand);',
    'native/dispatch.cpp': 'ipc_on("battery.charge." + operation, handler);\nif (operation == dynamicCommand) {}',
  });
  const c = f.catalog();
  const dynamic = c.surfaces.filter(surface => surface.dynamic);
  assert.ok(dynamic.some(surface => surface.kind === 'ipc-call-dynamic' && surface.domain === 'battery'));
  assert.ok(dynamic.some(surface => surface.kind === 'native-ipc-dynamic' && surface.expression.includes('battery.charge.')));
  assert.ok(dynamic.some(surface => surface.kind === 'native-operation-dynamic' && surface.expression === 'dynamicCommand'));
  assert.ok(find(c, 'ipc-call', 'battery.get'));
  assert.ok(dynamic.every(surface => surface.coverage === 'NOT_COVERED' && Number.isInteger(surface.line)));
  assert.equal(c.summary.dynamic, dynamic.length);
  assert.equal(c.summary.registrationComplete, false);
});

test('lexical parameter shadowing and mutable commands do not become false static resolutions', t => {
  const f = fixture(t, { 'src/bridge/neutral.ts': 'const cmd = "fan.wrong";\nfunction call(cmd: string) { invoke(cmd); }\nlet mutable = "fan.original"; mutable = external; invoke(mutable);' });
  const c = f.catalog();
  assert.equal(c.surfaces.filter(surface => surface.kind === 'ipc-call').length, 0);
  assert.equal(c.surfaces.filter(surface => surface.kind === 'ipc-call-dynamic').length, 2);
});

test('imported route constants and nested route objects retain source lines', t => {
  const f = fixture(t, {
    'src/bridge/constants.ts': 'export const ROUTE = "/battery";',
    'src/router/index.ts': 'import { ROUTE as route } from "../bridge/constants";\nconst routes = [{\npath: route, component: View, children: [\n{ path: "charge", component: Child },\n{ path: runtimeRoute, redirect: "/battery" }\n]}];',
  });
  const c = f.catalog();
  assert.equal(find(c, 'route', '/battery').line, 3);
  assert.ok(find(c, 'route', 'charge'));
  assert.equal(c.surfaces.filter(surface => surface.kind === 'route-dynamic').length, 1);
});

test('operation switches, lists, object dispatch keys and dynamic list members are inventoried', t => {
  const f = fixture(t, {
    'native/operations.js': 'const OPERATIONS = ["fan.read", runtimeOperation];\nconst handlers = {"display.set": () => {}, [externalOperation]: handler};\nswitch(request.operation) { case "audio.mute": break; case selected: break; }',
    'native/dispatch.h': 'const char* commands[] = {"battery.get", "battery.set"};\nswitch (command) { case "sleep.inspect": break; case dynamic: break; }',
  });
  const c = f.catalog();
  assert.ok(find(c, 'operation-list', 'fan.read'));
  assert.ok(find(c, 'operation-list', 'display.set'));
  assert.ok(find(c, 'operation-branch', 'audio.mute'));
  assert.ok(find(c, 'native-operation', 'sleep.inspect'));
  assert.ok(find(c, 'native-operation-list', 'battery.get'));
  assert.ok(find(c, 'native-operation-list', 'battery.set'));
  assert.ok(c.surfaces.some(surface => surface.kind === 'operation-list-dynamic'));
  assert.ok(c.surfaces.some(surface => surface.kind === 'native-operation-dynamic'));
});

test('registration uses exact source files, directory prefixes and globs with path boundaries', t => {
  const f = fixture(t, {
    'src/bridge/fan.ts': 'invoke("fan.get");',
    'src/bridge/fan/deep.ts': 'invoke("fan.deep");',
    'src/bridge/fanfare.ts': 'invoke("novel.action");',
  });
  const c = f.catalog([
    adapter('file', ['src\\bridge\\fan.ts']),
    adapter('prefix', ['src/bridge/fan/']),
    adapter('glob', ['src/bridge/fan/**']),
    adapter('domain-only', []),
  ]);
  assert.deepEqual(find(c, 'ipc-call', 'fan.get').adapterIds, ['file']);
  assert.deepEqual(find(c, 'ipc-call', 'fan.deep').adapterIds, ['glob', 'prefix']);
  assert.deepEqual(find(c, 'ipc-call', 'novel.action').adapterIds, []);
  assert.equal(find(c, 'ipc-call', 'novel.action').coverage, 'NOT_COVERED');
  assert.equal(find(c, 'package-script', 'test:fan').coverage, 'NOT_COVERED');
  assert.ok(c.summary.registered > 0 && c.summary.uncovered > 0);
});

test('source-contract and behavioral/host mock associations remain distinct, even for a shared source', t => {
  const f = fixture(t, { 'src/bridge/fan.ts': 'invoke("fan.read");' });
  const c = f.catalog([
    adapter('static', ['src/bridge/fan.ts'], 'source-contract'),
    adapter('behavior', ['src/bridge/fan.ts']),
    adapter('host', ['src/bridge/fan.ts'], 'host-mock'),
  ]);
  const surface = find(c, 'ipc-call', 'fan.read');
  assert.equal(surface.coverage, 'REGISTERED_TEST');
  assert.deepEqual(surface.evidenceScopes, ['behavioral-mock', 'host-mock', 'source-contract']);
  assert.ok(c.summary.byScope['source-contract'] > 0);
  assert.equal(c.summary.byScope['behavioral-mock'], c.summary.byScope['source-contract']);
  assert.equal(c.summary.behavioralVerified, false);
  assert.ok(!JSON.stringify(c).includes('"PASS"'));
});

test('unknown capabilities stay unclassified and prevent a complete-registration claim', t => {
  const f = fixture(t, { 'src/bridge/novel.ts': 'export const mystery = () => invoke("quantumFlux.begin");' });
  const c = f.catalog([adapter('broad', ['src/']), adapter('scripts', ['package.json'])]);
  assert.equal(find(c, 'ipc-call', 'quantumFlux.begin').domain, 'unclassified');
  assert.ok(c.summary.unclassified > 0);
  assert.equal(c.summary.registrationComplete, false);
  assert.equal(c.summary.behavioralVerified, false);
  assert.ok(!c.surfaces.some(surface => surface.coverage === 'PASS'));
});

test('domain ids align with adapters; shared-container operations determine their own domain', t => {
  const operations = { lighting: 'lighting.setColor', fan: 'fan.enable', performance: 'tdp.set', sensor: 'sensor.read', controller: 'input.enable', gyro: 'gyro.get', touch: 'touchpad.set', display: 'display.get', audio: 'audio.get', battery: 'battery.get', sleep: 'sleep.inspect', steam: 'steam.uiState', 'game-library': 'library.scan', plugins: 'plugin.enable', network: 'network.get', settings: 'settings.read', lifecycle: 'app.quit', unclassified: 'novel.begin' };
  const f = fixture(t, { 'native/main.cpp': Object.values(operations).map(operation => `ipc_on("${operation}", handler);`).join('\n') });
  const c = f.catalog();
  assert.deepEqual(new Set(c.domains.map(domain => domain.id)), new Set(Object.keys(operations)));
  for (const [domain, operation] of Object.entries(operations)) {
    assert.equal(find(c, 'native-ipc', operation).domain, domain, operation);
    assert.ok(c.summary.byDomain[domain].uncovered > 0, domain);
  }
  assert.equal(c.domains.reduce((sum, domain) => sum + domain.uncovered, 0), c.summary.uncovered);
});

test('comments, strings, raw C++ scripts and test/vendor source do not invent IPC registrations', t => {
  const f = fixture(t, {
    'src/bridge/neutral.ts': '// invoke("fake.comment");\nconst ignored = "invoke(\\\"fake.string\\\")";\ninvoke("display.real");',
    'native/dispatch.cpp': '// ipc_on("fake.comment", x);\n/* ipc_on("fake.block", x); */\nconst char* raw = R"JS(ipc_on("fake.raw", x))JS";\nipc_on("audio.real", x);',
    'src/bridge/neutral.test.ts': 'invoke("fake.test");',
    'src/fixtures/fake.ts': 'invoke("fake.fixture");',
    'src/vendor/fake.ts': 'invoke("fake.vendor");',
    'src/node_modules/fake.ts': 'invoke("fake.dependency");',
  });
  const c = f.catalog();
  assert.ok(find(c, 'ipc-call', 'display.real'));
  assert.ok(find(c, 'native-ipc', 'audio.real'));
  assert.ok(!c.surfaces.some(surface => surface.operation?.startsWith('fake.')));
});

test('new first-party packages contribute scripts and source without a manual capability list', t => {
  const f = fixture(t, {
    'new-extension/package.json': '{"scripts":{"new:plugin":"NEVER_EXECUTE"}}',
    'new-extension/src/index.ts': 'invoke("plugin.discover");',
    'deps/vendor-package/package.json': '{"scripts":{"fake":"NEVER_EXECUTE"}}',
  });
  const c = f.catalog();
  assert.ok(find(c, 'package-script', 'new:plugin'));
  assert.ok(find(c, 'ipc-call', 'plugin.discover'));
  assert.equal(find(c, 'package-script', 'fake'), undefined);
  assert.ok(c.summary.discovery.roots.includes('new-extension/src'));
});

test('parse gaps remain uncovered despite broad adapter prefixes', t => {
  const f = fixture(t, { 'src/bridge/fan.ts': 'export const broken = ( ;' });
  const c = f.catalog([adapter('broad', ['src/bridge/'])]);
  assert.ok(c.summary.discovery.diagnostics.length > 0);
  assert.ok(c.surfaces.some(surface => surface.kind === 'discovery-gap' && surface.coverage === 'NOT_COVERED' && surface.domain === 'unclassified'));
  assert.equal(c.summary.registrationComplete, false);
});

test('catalog is awaitable, deterministic and rejects invalid registrations rather than overmatching', async t => {
  const f = fixture(t, { 'src/bridge/fan.ts': 'invoke("fan.read");' });
  const before = await f.catalog([adapter('one', ['src/bridge/fan.ts'])]);
  const after = await f.catalog([adapter('one', ['src/bridge/fan.ts'])]);
  assert.deepEqual(before, after);
  assert.equal(new Set(before.surfaces.map(surface => surface.id)).size, before.surfaces.length);
  for (const invalid of [[adapter('dup', []), adapter('dup', [])], [adapter('escape', ['../src/'])], [adapter('absolute', ['C:\\outside\\file.ts'])], [adapter('scope', [], 'hardware')]]) assert.throws(() => f.catalog(invalid), TypeError);
});

test('exported API object methods, class methods, default exports and runtime reexports are visible', t => {
  const f = fixture(t, { 'src/bridge/fan.ts': 'export const api = { read() {}, set: () => {} };\nexport class Fan { open() {} private internal() {} }\nexport default { close() {} };\nexport { api as other };\nexport type { SomeType };' });
  const c = f.catalog();
  for (const operation of ['api', 'api.read', 'api.set', 'Fan', 'Fan.open', 'default', 'default.close', 'other']) assert.ok(find(c, 'bridge-export', operation), operation);
  assert.equal(find(c, 'bridge-export', 'Fan.internal'), undefined);
  assert.equal(find(c, 'bridge-export', 'SomeType'), undefined);
});

test('standalone shipped HTML and prerequisite/memory hosts are inventoried with inline source lines',t=>{
 const f=fixture(t,{
  'public/performance-simulation/index.html':'<html>\n<script type="application/json">{"ignore":"invoke(fake)"}</script>\n<script>\ninvoke("performance.workload");\n</script></html>',
  'public/performance-simulation/model.js':'export const model = () => invoke("performance.model");',
  'public/third-party.min.js':'invoke("vendor.must-not-be-a-product-capability");',
  'GamepadPrerequisites/Program.cs':'if(operation == "install-driver") {}',
  'PowerControl/MemoryScan/MemoryScanHost.cs':'if(command == "memory.scan") {}',
 });
 const c=f.catalog();
 assert.ok(find(c,'standalone-page',undefined));
 assert.equal(find(c,'ipc-call','performance.workload').line,4);
 assert.ok(find(c,'ipc-call','performance.model'));
 assert.ok(find(c,'native-operation','install-driver'));
 assert.ok(find(c,'native-operation','memory.scan'));
 assert.equal(find(c,'ipc-call','vendor.must-not-be-a-product-capability'),undefined);
 assert.deepEqual(c.summary.discovery.diagnostics,[]);
});
test('reviewed battery fixtures are not product capabilities; sibling production modules remain visible', t => {
  const f = fixture(t, {
    'native/BatteryHostTests/Program.cs': 'if (op == "fixture.only") {}',
    'native/BatteryHost/Program.cs': 'if (op == "battery.charge.get") {}',
    'native/BatteryHostTestsExtra/Dispatch.cs': 'if (op == "battery.production.sibling") {}',
  });
  const c = f.catalog();
  assert(!c.surfaces.some(row => row.path.startsWith('native/BatteryHostTests/')));
  assert(c.surfaces.some(row => row.path === 'native/BatteryHost/Program.cs'));
  assert(c.surfaces.some(row => row.path === 'native/BatteryHostTestsExtra/Dispatch.cs'));
  assert.deepEqual(c.sourceExclusions.map(row => row.path), ['native/BatteryHostTests/Program.cs']);
  assert.equal(c.sourceExclusions[0].role, 'reviewed-test-source');
});
