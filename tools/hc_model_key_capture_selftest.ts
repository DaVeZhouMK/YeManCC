/** Offline capability/shortcut regression. Run from the source root; IPC/storage are in-memory fixtures. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import * as schema from '../src/bridge/controllerShortcutRules';
import * as presentation from '../src/bridge/gamepadPresentation';

const root = process.cwd();
const requireLocal = createRequire(path.join(root, 'package.json'));
const vue = requireLocal('vue');
const { parse, compileScript } = requireLocal('vue/compiler-sfc');
const { transformSync } = requireLocal('esbuild');
const hint = '此按键的输入暂不可用';
const reason = 'vendor-hid-reader-not-integrated';
const cases: string[] = [];
async function test(name: string, run: () => unknown) { await run(); cases.push(name); }
// Persisted fixtures are JSON; serialization also unwraps Vue reactive proxies.
const clone = <T>(value: T): T => JSON.parse(JSON.stringify(value));
const rawKey = (keyId = 'home', extra: Record<string, unknown> = {}) => ({ keyId, label: keyId === 'home' ? 'Home' : keyId.toUpperCase(), backIndex: 0, triggerCapability: 'full', physicalForm: 'hid-button', ...extra });
const catalog = (keys: unknown[], familyId = 'onex-fixture') => schema.normalizeOemKeysSnapshot({ familyId, supported: true, keys });
const oemRule = (code = 'home', id = 'oem-rule'): schema.ControllerShortcutRule => ({ ...schema.newControllerShortcutRule(id, 'keyboard.numeric'), id, inputs: [{ source: 'oem', code }], params: { key: 'F8' } });
const normal = oemRule('m1', 'normal');
const unavailable = catalog([rawKey('home', { captureImplemented: false, captureReason: reason }), rawKey('m1', { backIndex: 1 }), rawKey('m2', { backIndex: 2, captureImplemented: true })]);

// Compile the production SFCs and mount their real templates in Vue's custom renderer.
// Exposed test hooks are appended only to this in-memory copy of the editor.
function evaluate(source: string, imports: Record<string, unknown>, globals: Record<string, unknown>) {
  const module = { exports: {} as any };
  vm.runInNewContext(transformSync(source, { loader: 'ts', format: 'cjs' }).code, {
    module, exports: module.exports, console, structuredClone, TextEncoder, performance,
    require(name: string) {
      if (name === 'vue') return vue;
      if (name in imports) return imports[name];
      throw Error(`Unexpected fixture import: ${name}`);
    },
    fetch() { throw Error('Network access is forbidden in this selftest'); },
    ...globals,
  });
  return module.exports;
}
function component(file: string, imports: Record<string, unknown>, globals: Record<string, unknown>, exposed = '') {
  let source = fs.readFileSync(path.join(root, file), 'utf8');
  if (exposed) source = source.replace('</script>', `\ndefineExpose({ ${exposed} });\n</script>`);
  const { descriptor, errors } = parse(source, { filename: file });
  assert.equal(errors.length, 0);
  return evaluate(compileScript(descriptor, { id: 'hc-model-key-capture-selftest', inlineTemplate: true,
    templateOptions: { compilerOptions: { hoistStatic: false } } }).content, imports, globals).default;
}
function element(type: string, text = ''): any {
  return { type, text, props: {}, children: [], parent: null,
    get textContent() { return this.text + this.children.map((child: any) => child.textContent).join(''); },
    querySelector() { return null; }, focus() {}, contains() { return false; },
  };
}
function rendererFixture() {
  const body = element('body');
  const renderer = vue.createRenderer({
    querySelector: (selector: string) => selector === 'body' ? body : null,
    createElement: (type: string) => element(type), createText: (text: string) => element('#text', text), createComment: (text: string) => element('#comment', text),
    setText(node: any, text: string) { node.text = text; }, setElementText(node: any, text: string) { node.text = text; node.children = []; },
    parentNode: (node: any) => node.parent, nextSibling: (node: any) => node.parent?.children[node.parent.children.indexOf(node) + 1] || null,
    insert(node: any, parent: any, anchor: any = null) {
      if (node.parent) node.parent.children.splice(node.parent.children.indexOf(node), 1);
      node.parent = parent;
      const index = anchor ? parent.children.indexOf(anchor) : -1;
      if (index < 0) parent.children.push(node); else parent.children.splice(index, 0, node);
    },
    remove(node: any) { if (node.parent) node.parent.children.splice(node.parent.children.indexOf(node), 1); node.parent = null; },
    patchProp(node: any, key: string, previous: unknown, next: unknown) { node.props[key] = next; },
  });
  return { body, renderer };
}
const descendants = (node: any): any[] => [node, ...node.children.flatMap(descendants)];
const hasClass = (node: any, name: string) => String(node.props.class || '').split(/\s+/).includes(name);
async function flush() { for (let index = 0; index < 30; index++) { await Promise.resolve(); await vue.nextTick(); } }
const stub = { setup: () => () => vue.h('div') };
const stubImport = { __esModule: true, default: stub };
function globalsFixture() {
  const events = new Map<string, Set<Function>>();
  const window = {
    innerWidth: 1280, innerHeight: 800, location: { hash: '?__ymccChild=1' },
    setTimeout, clearTimeout,
    addEventListener(name: string, callback: Function) { if (!events.has(name)) events.set(name, new Set()); events.get(name)!.add(callback); },
    removeEventListener(name: string, callback: Function) { events.get(name)?.delete(callback); },
    dispatchEvent(event: any) { for (const callback of events.get(event.type) || []) callback(event); },
  };
  return { window, document: { addEventListener() {}, removeEventListener() {}, querySelectorAll() { return []; } },
    CustomEvent: class { type: string; detail: unknown; constructor(type: string, options?: any) { this.type = type; this.detail = options?.detail; } },
  };
}
async function editorFixture(keys = unavailable, entries: schema.ControllerShortcutRule[] = [normal], conflictEntries?: schema.ControllerShortcutRule[]) {
  const { body, renderer } = rendererFixture();
  let persisted: any = { revision: 1, buttonMapping: { rules: { schemaVersion: 2, closure: 'UNENCLOSED', shortcutRules: clone(entries) } } };
  const writes: any[] = [], nativeWrites: any[] = [], recordingCalls: boolean[] = [];
  const nativeEvents = new Map<string, Function>();
  const gamepad = { enabled: true, ...Object.fromEntries(schema.NATIVE_DEFAULT_SHORTCUTS.map(definition => [definition.defaultKey, true])) };
  const view = component('src/views/ControllerShortcutEditorView.vue', {
    '@/components/Dropdown.vue': stubImport, '@/components/GamepadVisualizer.vue': stubImport,
    '@/components/InlineIcon.vue': stubImport, '@/components/SegButton.vue': stubImport,
    '@/bridge/ipc': { isNativeRuntime: true, on(name: string, callback: Function) { nativeEvents.set(name, callback); return () => nativeEvents.delete(name); } },
    '@/bridge/api': { windowApi: { controllerEditorWidth() { throw Error('Unexpected real-window operation'); } } },
    '@/bridge/controllerShortcutRules': schema,
    '@/bridge/yeman': {
      oemKeysGet: async () => clone(keys), summonGet: async () => clone(gamepad),
      summonSet: async (patch: unknown) => { nativeWrites.push(clone(patch)); return clone(gamepad); },
      setShortcutRecording: async (active: boolean) => { recordingCalls.push(active); return { active }; },
    },
    '@/bridge/settingsRepository': {
      loadSettings: async () => ({ input: clone(persisted) }),
      compareAndSwapInputSettings: async (revision: number, patch: any) => {
        writes.push({ revision, patch: clone(patch) });
        if (conflictEntries && writes.length === 1) {
          persisted = { ...persisted, revision: 2, buttonMapping: { rules: { shortcutRules: clone(conflictEntries) } } };
          return { ok: false, reason: 'revision-conflict', value: clone(persisted) };
        }
        persisted = { ...persisted, revision: revision + 1, buttonMapping: clone(patch.buttonMapping) };
        return { ok: true, value: clone(persisted) };
      },
    },
    '@/gamepad/focus': { focusGamepadElement() {} },
  }, globalsFixture(), 'rules, activeId, activeRule, oemKeys, dirty, busy, status, validation, recordMode, recording, recordingBusy, recordingInputs, recordingStatus, bindOemKey, clearOemInputs, startRecording, finishRecording, apply, selectRule');
  const app = renderer.createApp(view);
  const api: any = app.mount(element('root'));
  await flush();
  api.selectRule(entries.find(rule => schema.isControllerShortcutActionVisible(rule.actionId))?.id);
  await flush();
  assert.equal(api.activeRule?.id, entries.find(rule => schema.isControllerShortcutActionVisible(rule.actionId))?.id);
  return { app, api, body, writes, nativeWrites, recordingCalls, nativeEvents, get saved() { return clone(persisted.buttonMapping.rules.shortcutRules); } };
}

async function main() {
  await test('legacy normalization preserves exact shape, order and input ownership', () => {
    const raw = { familyId: 'legacy', supported: true, keys: [rawKey('m2', { backIndex: 1 }), rawKey('m1', { backIndex: 2 }), rawKey()] };
    const before = clone(raw), normalized = schema.normalizeOemKeysSnapshot(raw);
    assert.deepEqual(normalized, { familyId: 'legacy', supported: true, keys: raw.keys.map(key => ({ ...key, backIndex: key.backIndex || null })) });
    for (const key of normalized.keys) { assert.equal(Object.hasOwn(key, 'captureImplemented'), false); assert.equal(Object.hasOwn(key, 'captureReason'), false); }
    assert.deepEqual(raw, before);
    assert.deepEqual(schema.normalizeOemKeysSnapshot(normalized), normalized);
  });
  await test('boolean true/false and string reason round-trip without removing directory keys', () => {
    assert.equal(unavailable.supported, true);
    assert.deepEqual(unavailable.keys.map(key => key.keyId), ['home', 'm1', 'm2']);
    assert.equal(unavailable.keys[0].captureImplemented, false);
    assert.equal(unavailable.keys[0].captureReason, reason);
    assert.equal(unavailable.keys[2].captureImplemented, true);
    assert.equal(catalog([rawKey('home', { captureImplemented: false })]).supported, true);
    assert.deepEqual(schema.normalizeOemKeysSnapshot(unavailable), unavailable);
  });
  for (const [index, flag] of [undefined, null, 0, 1, '', 'false', 'true', [], {}, new Boolean(true)].entries()) {
    await test(`malformed present capture flag ${index} fails closed in normalization and validation`, () => {
      const normalized = catalog([rawKey('home', { captureImplemented: flag })]);
      assert.equal(Object.hasOwn(normalized.keys[0], 'captureImplemented'), true);
      assert.equal(normalized.keys[0].captureImplemented, false);
      assert(schema.validateControllerShortcutRules([oemRule()], normalized).some(problem => problem.includes(hint)));
    });
  }
  await test('only string reasons survive; reason alone does not disable legacy capture', () => {
    for (const value of [undefined, null, false, 123, [], {}]) assert.equal(Object.hasOwn(catalog([rawKey('home', { captureReason: value })]).keys[0], 'captureReason'), false);
    for (const value of ['', reason]) {
      const normalized = catalog([rawKey('home', { captureReason: value })]);
      assert.equal(normalized.keys[0].captureReason, value);
      assert.equal(Object.hasOwn(normalized.keys[0], 'captureImplemented'), false);
      assert.deepEqual(schema.validateControllerShortcutRules([oemRule()], normalized), []);
    }
  });
  await test('unavailable capture rejected for every trigger/enabled state, with no internal reason leak', () => {
    for (const trigger of ['press', 'double', 'hold', 'release', 'repeat'] as const) for (const enabled of [true, false]) {
      const problems = schema.validateControllerShortcutRules([{ ...oemRule(), trigger, enabled }], unavailable);
      assert(problems.some(problem => problem.includes('oem-rule') && problem.includes('home') && problem.includes(hint)));
      assert.equal(problems.join('').includes(reason), false);
    }
    assert(schema.validateControllerShortcutRules([oemRule()], { ...unavailable, supported: false }).some(problem => problem.includes(hint)));
  });
  await test('legacy, implemented, controller, keyboard and native defaults retain previous rule behavior', () => {
    const legacy = catalog([rawKey('m1', { backIndex: 1 })]);
    const rules = [normal, { ...schema.newControllerShortcutRule('controller', 'keyboard.numeric') },
      { ...schema.newControllerShortcutRule('keyboard', 'keyboard.numeric'), inputs: [{ source: 'keyboard' as const, code: 'F8' }] }];
    for (const context of [undefined, schema.OEM_EMPTY_SNAPSHOT, legacy, unavailable]) assert.deepEqual(schema.validateControllerShortcutRules(rules, context), []);
    assert.deepEqual(schema.validateControllerShortcutRules([oemRule('m2')], unavailable), []);
    const defaults = schema.NATIVE_DEFAULT_SHORTCUTS.map(definition => schema.nativeDefaultRule(definition.defaultKey));
    assert.deepEqual(schema.validateControllerShortcutRules(defaults, unavailable), []);
    const saved = [...defaults, ...rules, oemRule()];
    assert.deepEqual(schema.readControllerShortcutRules({ shortcutRules: saved }), saved);
    assert(schema.validateControllerShortcutRules([oemRule('not-in-catalog')], unavailable).some(problem => problem.includes('不属于本机机型库')));
  });
  await test('real editor disables special and rear entry points; handler bypass cannot bind; legacy clicks still work', async () => {
    const keys = catalog([...unavailable.keys, rawKey('unavailable-rear', { backIndex: 3, captureImplemented: false })]);
    const f = await editorFixture(keys);
    try {
      const buttons = descendants(f.body).filter(node => hasClass(node, 'oem-action'));
      assert.deepEqual(buttons.map(node => node.textContent), ['Home']);
      assert.equal(buttons[0].props.disabled, true); assert.equal(buttons[0].props.title, hint);
      const slots = descendants(f.body).filter(node => hasClass(node, 'oem-back-slot'));
      assert.equal(slots[0].props.disabled, false); assert.equal(slots[1].props.disabled, false);
      assert.equal(slots[2].props.disabled, true); assert.equal(slots[2].props.title, hint);
      const previous = clone(f.api.activeRule);
      f.api.bindOemKey('home'); f.api.bindOemKey('unavailable-rear');
      // Even synthetic calls of disabled template handlers must leave the draft intact.
      buttons[0].props.onClick(); slots[2].props.onClick();
      assert.deepEqual(clone(f.api.activeRule), previous); assert.equal(f.api.dirty, false); assert.equal(f.api.status, hint);
      assert(f.body.textContent.includes(hint)); assert.equal(f.body.textContent.includes(reason), false);
      slots[1].props.onClick(); assert.equal(f.api.activeRule.inputs[0].code, 'm2');
      slots[0].props.onClick(); assert.equal(f.api.activeRule.inputs[0].code, 'm1');
      assert.equal(f.writes.length, 0); assert.equal(f.nativeWrites.length, 0);
    } finally { f.app.unmount(); }
  });
  await test('saved unavailable binding remains editable but cannot appear selected or be applied', async () => {
    const saved = oemRule(); const f = await editorFixture(unavailable, [saved]);
    try {
      assert.deepEqual(clone(f.api.activeRule), saved);
      assert(descendants(f.body).filter(node => hasClass(node, 'oem-action')).every(node => !hasClass(node, 'active')));
      f.api.dirty = true; await f.api.apply();
      assert.equal(f.writes.length, 0); assert.equal(f.nativeWrites.length, 0); assert(f.api.status.includes(hint));
      await f.api.startRecording(); assert.equal(f.api.recordingStatus, hint);
      assert.equal(f.recordingCalls.includes(true), false); assert.equal(f.api.recording, false);
      f.api.bindOemKey('m1'); await f.api.apply();
      assert.equal(f.writes.length, 1); assert.equal(f.nativeWrites.length, 1);
      assert.equal(f.saved.find((rule: schema.ControllerShortcutRule) => rule.id === saved.id).inputs[0].code, 'm1');
      f.api.clearOemInputs(); assert.deepEqual(clone(f.api.activeRule.inputs), []);
    } finally { f.app.unmount(); }
  });
  await test('record completion rejects an unavailable OEM input without overwriting a good rule', async () => {
    const f = await editorFixture();
    try {
      const before = clone(f.api.activeRule);
      f.api.recording = true; f.api.recordingInputs = [{ source: 'oem', code: 'home' }];
      f.api.finishRecording([{ source: 'oem', code: 'home' }]);
      assert.deepEqual(clone(f.api.activeRule), before); assert.equal(f.api.recording, false);
      assert.deepEqual(clone(f.api.recordingInputs), []); assert.equal(f.api.recordingStatus, hint);
      assert.equal(f.api.dirty, false); assert.equal(f.writes.length, 0);
      f.api.recordMode = 'controller'; f.api.recording = true;
      f.nativeEvents.get('shortcut.recording.gamepad')!({ codes: ['home', { source: 'oem', code: 'home' }, 'lb', 'rb'] });
      assert.deepEqual(clone(f.api.activeRule.inputs), [{ source: 'controller', code: 'lb' }, { source: 'controller', code: 'rb' }]);
    } finally { f.app.unmount(); }
  });
  await test('ordinary saving preserves legacy, default and hidden rules exactly', async () => {
    const hidden = { ...oemRule('m1', 'hidden'), actionId: 'app.exit', params: {} };
    const entries = [...schema.NATIVE_DEFAULT_SHORTCUTS.map(definition => schema.nativeDefaultRule(definition.defaultKey)), normal, hidden];
    const f = await editorFixture(unavailable, entries);
    try { f.api.dirty = true; await f.api.apply(); assert.equal(f.writes.length, 1); assert.deepEqual(f.saved, entries); }
    finally { f.app.unmount(); }
  });
  await test('hidden rules with explicit unavailable capture cannot bypass saving', async () => {
    const hidden = { ...oemRule('home', 'hidden-unavailable'), actionId: 'app.exit', params: {} };
    const f = await editorFixture(unavailable, [normal, hidden]);
    try { f.api.dirty = true; await f.api.apply(); assert.equal(f.writes.length, 0); assert(f.api.status.includes(hint)); }
    finally { f.app.unmount(); }
  });
  await test('revision-conflict merge revalidates newly introduced unavailable capture before retry', async () => {
    const remote = [normal, oemRule('home', 'remote-unavailable')];
    const f = await editorFixture(unavailable, [normal], remote);
    try {
      f.api.bindOemKey('m2'); await f.api.apply();
      assert.equal(f.writes.length, 1); assert.equal(f.nativeWrites.length, 0);
      assert(f.api.status.includes(hint)); assert.equal(f.api.dirty, true);
      assert.equal(f.api.activeRule.inputs[0].code, 'm2');
    } finally { f.app.unmount(); }
  });
  await test('real visualizer keeps front/rear/additional keys visible while suppressing unavailable live/selection states', async () => {
    const { body, renderer } = rendererFixture();
    const preview = component('src/components/GamepadVisualizer.vue', {
      '@/components/Toggle.vue': stubImport, '@/bridge/controllerShortcutRules': schema,
      '@/bridge/ipc': { on() { return () => {}; } }, '@/bridge/uiLifecycle': { isUiVisible: () => true, onUiVisibilityChange: () => () => {} },
      '@/bridge/gamepadPresentation': presentation,
    }, globalsFixture(), 'oemPressedKeys');
    const keys = catalog([rawKey('home', { captureImplemented: false, captureReason: reason }),
      rawKey('m1', { backIndex: 1, captureImplemented: false }), rawKey('m2', { backIndex: 2 }),
      rawKey('legionl', { captureImplemented: false }), rawKey('legionm2')], 'lenovo-legion-go');
    const ids = keys.keys.map(key => key.keyId);
    const app = renderer.createApp(preview, { settings: {}, oemKeys: keys, highlightOemKeys: ids, showTestMode: false });
    const api: any = app.mount(body);
    try {
      api.oemPressedKeys = ids; await flush();
      const nodes = descendants(body).filter(node => node.props['data-oem-key']);
      assert.equal(nodes.length, 5);
      for (const key of keys.keys) {
        const node = nodes.find(node => node.props['data-oem-key'] === key.keyId);
        assert(node);
        if (key.captureImplemented === false) {
          assert.equal(node.props['aria-disabled'], true); assert(node.props['aria-label'].includes(hint));
          assert.equal(hasClass(node, 'mapped'), false); assert.equal(hasClass(node, 'live'), false);
        } else {
          assert.equal(node.props['aria-disabled'], undefined); assert.equal(hasClass(node, 'mapped'), true); assert.equal(hasClass(node, 'live'), true);
        }
      }
      assert.equal(body.textContent.includes(reason), false);
      assert.equal(JSON.stringify(nodes.map(node => node.props)).includes(reason), false);
      const special = descendants(body).find(node => hasClass(node, 'oem-special-exterior'));
      const additional = descendants(body).find(node => hasClass(node, 'oem-additional-back'));
      assert.deepEqual(descendants(special).filter(node => node.props['data-oem-key']).map(node => node.props['data-oem-key']), ['home']);
      assert.deepEqual(descendants(additional).filter(node => node.props['data-oem-key']).map(node => node.props['data-oem-key']), ['legionl', 'legionm2']);
    } finally { app.unmount(); }
  });
  const baselineDir = process.argv.find(argument => argument.startsWith('--baseline-dir='))?.slice('--baseline-dir='.length);
  if (baselineDir) await test('pre-edit baseline agrees on legacy catalogs, rules, validation and default shortcuts', () => {
    const old = evaluate(fs.readFileSync(path.join(baselineDir, 'controllerShortcutRules.ts.before'), 'utf8'), {}, {});
    const normalizeRealm = (value: unknown) => JSON.parse(JSON.stringify(value));
    assert.deepEqual(normalizeRealm(old.NATIVE_DEFAULT_SHORTCUTS), schema.NATIVE_DEFAULT_SHORTCUTS);
    for (const definition of schema.NATIVE_DEFAULT_SHORTCUTS) assert.deepEqual(normalizeRealm(old.nativeDefaultRule(definition.defaultKey)), schema.nativeDefaultRule(definition.defaultKey));
    for (const familyId of ['asus-rog-ally-x', 'msi-claw', 'onex-legacy']) {
      const raw = { familyId, supported: true, keys: [rawKey('m2', { backIndex: 1 }), rawKey('home', { triggerCapability: 'click-only', physicalForm: 'wmi-event' }), rawKey('m1', { backIndex: 2 })] };
      const current = schema.normalizeOemKeysSnapshot(raw);
      assert.deepEqual(normalizeRealm(old.normalizeOemKeysSnapshot(raw)), current);
      const rules = [normal, oemRule('home'), oemRule('unknown'), { ...normal, id: 'mixed', inputs: [{ source: 'oem', code: 'm1' }, { source: 'controller', code: 'a' }] }, ...schema.NATIVE_DEFAULT_SHORTCUTS.map(definition => schema.nativeDefaultRule(definition.defaultKey))];
      assert.deepEqual(normalizeRealm(old.validateControllerShortcutRules(rules, current)), schema.validateControllerShortcutRules(rules as schema.ControllerShortcutRule[], current));
      assert.deepEqual(normalizeRealm(old.readControllerShortcutRules({ shortcutRules: rules })), schema.readControllerShortcutRules({ shortcutRules: rules }));
    }
  });
  const outputDir = process.argv.find(argument => argument.startsWith('--out='))?.slice('--out='.length);
  const out = path.resolve(root, outputDir || '../../../Build/Validation/HC-ModelCapabilities-20261017/Remaining9/key-capture');
  fs.mkdirSync(out, { recursive: true });
  const receipt = { status: 'PASS', passedGroups: cases.length, cases, boundary: 'Offline normalization/validation and real Vue templates with in-memory IPC/settings; no network, native/device access, deployment or hardware capture verification.' };
  fs.writeFileSync(path.join(out, 'hc_model_key_capture_selftest.receipt.json'), JSON.stringify(receipt, null, 2) + '\n');
  console.log(JSON.stringify(receipt, null, 2));
}
main().catch(error => { console.error(error); process.exitCode = 1; });
