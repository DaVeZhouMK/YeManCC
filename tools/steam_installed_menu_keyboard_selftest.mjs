// Execute only bounded method bodies extracted from the installed Valve client
// in a VM with fake receivers. Never connect to Steam or inject system input.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
const [steamRoot, output] = process.argv.slice(2);
if (!steamRoot || !output || !path.isAbsolute(steamRoot) || !path.isAbsolute(output))
  throw Error('Usage: node tool.mjs <absolute Steam directory> <absolute report.json>');
const hash = s => createHash('sha256').update(s).digest('hex');
const libraryFile = path.join(steamRoot, 'steamui', 'library.js');
const library = fs.readFileSync(libraryFile, 'utf8');
const enums = {};
for (const match of library.matchAll(/\w\[\w\.(\w+)=(-?\d+)\]="\1"/g)) {
  if (enums[match[1]] !== undefined && enums[match[1]] !== +match[2]) continue;
  enums[match[1]] = +match[2];
}
for (const name of ['INVALID', 'STEAM_GUIDE', 'STEAM_QUICK_MENU']) assert(Number.isInteger(enums[name]), name);
const translateMarker = 'TranslateKey(e){const t=this.GetKeycodeFromEvent(e)';
const routerMarker = 'function g(){const e=(0,n.$2)();return s.useCallback';
const files = fs.readdirSync(path.join(steamRoot, 'steamui')).filter(n => /^chunk.*\.js$/.test(n));
const candidates = files.map(n => {
  const file = path.join(steamRoot, 'steamui', n), source = fs.readFileSync(file, 'utf8');
  return { file, source };
}).filter(v => v.source.includes(translateMarker) && v.source.includes(routerMarker));
assert.equal(candidates.length, 1, 'client schema changed: expected one keyboard/router chunk');
const { file, source } = candidates[0];
// These audited methods contain no regex literals or nested template literals.
// Any changed shape fails the chain assertions instead of touching the client.
function extract(marker, start = 0) {
  const offset = source.indexOf(marker, start);
  assert(offset >= 0, marker);
  const brace = source.indexOf('{', offset); let depth = 0, quote = null;
  for (let i = brace; i < source.length; i++) {
    const c = source[i];
    if (quote) { if (c === '\\') i++; else if (c === quote) quote = null; continue; }
    if (c === '"' || c === "'" || c === '`') { quote = c; continue; }
    if (c === '{') depth++;
    if (c === '}' && --depth === 0) return { offset, text: source.slice(offset, i + 1) };
  }
  throw Error('unterminated extracted method');
}
const translateAt = source.indexOf(translateMarker);
const keyboardStart = source.lastIndexOf('OnKeyDown(e){', translateAt);
assert(keyboardStart >= 0 && translateAt - keyboardStart < 6000, 'keyboard class schema changed');
const methodNames = ['OnKeyDown', 'OnKeyUp', 'Reset', 'GetKeycodeFromEvent', 'BShouldSwallowEventForTextInputWorkaround', 'TranslateKey'];
const extracted = methodNames.map(name => extract(name + (name === 'Reset' ? '(){' : '(e){'), keyboardStart));
assert.equal(extracted.at(-1).offset, translateAt);
const router = extract(routerMarker);
assert(router.text.includes('e.OnHomeButtonPressed()') && router.text.includes('e.OnQuickAccessButtonPressed()'));
const calls = []; let stops = 0;
const receiver = { BrowserWindow: { name: 'isolated-fixture' },
  OnHomeButtonPressed() { calls.push('OnHomeButtonPressed'); },
  OnQuickAccessButtonPressed() { calls.push('OnQuickAccessButtonPressed'); } };
const context = vm.createContext({
  a: { pR: enums }, o: { pR: enums, TS: { PLATFORM: 'windows' } },
  s: { kD: node => !!node, useCallback: fn => fn }, n: { $2: () => receiver },
  console: { log() {} },
});
const globalDown = vm.runInContext(`(${router.text})()`, context, { timeout: 1000 });
const keyboard = { m_lastButtonDown: enums.INVALID,
  OnButtonDown(button) { globalDown({ detail: { button }, stopPropagation() { stops++; } }); },
  OnButtonUp(button) { calls.push(`UP:${button}`); } };
for (const method of extracted) {
  const name = method.text.slice(0, method.text.indexOf('('));
  keyboard[name] = vm.runInContext(`(function${method.text.slice(name.length)})`, context, { timeout: 1000 });
}
const target = { nodeName: 'DIV', hasAttribute: () => false };
const event = (code, mods = {}) => ({ code, key: code === 'Digit1' ? '1' : '2',
  ctrlKey: true, altKey: false, shiftKey: false, target,
  preventDefault() {}, stopPropagation() {}, ...mods });
const cases = [];
const check = (name, fn) => { fn(); cases.push({ name, ok: true }); };
for (const [code, logical, handler] of [
  ['Digit1', 'STEAM_GUIDE', 'OnHomeButtonPressed'],
  ['Digit2', 'STEAM_QUICK_MENU', 'OnQuickAccessButtonPressed'],
]) {
  check(`${code}:Ctrl translation`, () => assert.equal(keyboard.TranslateKey(event(code)), enums[logical]));
  check(`${code}:DOWN enters native receiver once`, () => {
    calls.length = 0; keyboard.Reset(); calls.length = 0;
    keyboard.OnKeyDown(event(code)); assert.deepEqual(calls, [handler]);
  });
  check(`${code}:keyboard repeat does not invoke twice`, () => {
    keyboard.OnKeyDown(event(code)); assert.deepEqual(calls, [handler]);
  });
  check(`${code}:UP balances logical input`, () => {
    keyboard.OnKeyUp(event(code)); assert.deepEqual(calls, [handler, `UP:${enums[logical]}`]);
    assert.equal(keyboard.m_lastButtonDown, enums.INVALID);
  });
  for (const [name, mods] of [['no-Ctrl', { ctrlKey: false }], ['Alt', { altKey: true }], ['Shift', { shiftKey: true }],
    ['editable', { target: { nodeName: 'DIV', hasAttribute: () => true } }]]) {
    check(`${code}:${name} is not this shortcut`, () => assert.equal(keyboard.TranslateKey(event(code, mods)), enums.INVALID));
  }
}
check('Steam routing stops global event propagation', () => assert.equal(stops, 2));
const project = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const liveFile = path.join(project, 'native', 'steam_live.js'), live = fs.readFileSync(liveFile, 'utf8');
const branch = live.slice(live.indexOf("if (['menu.button'"), live.indexOf("if (request.operation === 'settings.get'"));
check('production uses both native handlers', () => {
  assert(branch.includes('OnHomeButtonPressed') && branch.includes('OnQuickAccessButtonPressed'));
  assert(branch.includes('await bounded(w[handler]())'));
});
check('production has no keyboard/focus fallback', () => {
  for (const term of ['SetFocus(', 'SetForegroundWindow', 'foregroundPid', 'dispatchEvent(', 'Input.dispatchKeyEvent', 'SendInput'])
    assert(!branch.includes(term), term);
});
const report = { ok: true, kind: 'installed Valve keyboard translation and global menu receiver fixture',
  atUtc: new Date().toISOString(), passed: cases.length, cases,
  sources: [{ file, sha256: hash(source) }, { file: libraryFile, sha256: hash(library) }, { file: liveFile, sha256: hash(live) }],
  methods: [...extracted, router].map(v => ({ offset: v.offset, sha256: hash(v.text), member: v.text.slice(0, v.text.indexOf('(')) })),
  logicalButtons: { STEAM_GUIDE: enums.STEAM_GUIDE, STEAM_QUICK_MENU: enums.STEAM_QUICK_MENU },
  systemInputInjected: false, realSteamMenuMutated: false, focusManipulated: false, hardwareWrites: 0,
  limitation: 'Runs installed client method bodies in an isolated VM with fake receivers; not live Windows SendInput or MSI handheld acceptance.' };
fs.mkdirSync(path.dirname(output), { recursive: true }); fs.writeFileSync(output, JSON.stringify(report, null, 2));
console.log(JSON.stringify({ ok: true, passed: report.passed, output, logicalButtons: report.logicalButtons }));
