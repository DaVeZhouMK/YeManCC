import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import ts from 'typescript';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = fs.readFileSync(path.join(repo, 'src/gamepad/activationEvidence.ts'), 'utf8');
const apiSource = fs.readFileSync(path.join(repo, 'src/bridge/api.ts'), 'utf8');
const compiled = ts.transpileModule(source, {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS },
  reportDiagnostics: true,
});
assert(!(compiled.diagnostics || []).some(d => d.category === ts.DiagnosticCategory.Error));
const exports = {};
// No DOM, timers, IPC or input facilities: execute only the real evidence module.
vm.runInNewContext(compiled.outputText, { exports }, { timeout: 1000 });
const { withGamepadActivation: withActivation, readActivationEvidence: readEvidence } = exports;
const activations = ['gamepad-engine', 'trusted-touch', 'trusted-pen', 'trusted-mouse', 'trusted-other', 'scripted', 'unknown'];
let checks = 0;
function check(condition, message) { checks++; assert(condition, message); }
function read(event) {
  const evidence = readEvidence(event);
  check(activations.includes(evidence.diagnosticActivation), 'activation must be a fixed enum');
  check(Object.keys(evidence).every(k => ['diagnosticActivation', 'diagnosticEventTrusted', 'diagnosticClickDetail'].includes(k)), 'must not retain events or arbitrary input fields');
  check(!('diagnosticEventTrusted' in evidence) || typeof evidence.diagnosticEventTrusted === 'boolean', 'trusted field must be boolean');
  check(!('diagnosticClickDetail' in evidence) || (Number.isInteger(evidence.diagnosticClickDetail) && evidence.diagnosticClickDetail >= 0 && evidence.diagnosticClickDetail <= 3), 'click detail must be finite and bounded');
  return evidence;
}
const target = {}, other = {};
const click = (currentTarget = target) => ({ currentTarget, isTrusted: false, detail: 0 });
check(read().diagnosticActivation === 'unknown', 'missing event stays compatible');
check(!('diagnosticEventTrusted' in read()), 'missing event must not invent trusted state');
check(read(click()).diagnosticActivation === 'scripted', 'synthetic click must not impersonate gamepad');
check(withActivation(target, () => {
  check(read(click()).diagnosticActivation === 'gamepad-engine', 'engine marker must match currentTarget');
  check(read({ ...click(other), target }).diagnosticActivation === 'scripted', 'event.target is not currentTarget');
  check(read({ ...click(), target: other }).diagnosticActivation === 'gamepad-engine', 'descendant event.target must not hide matching currentTarget');
  check(read({ ...click(other), isTrusted: true, pointerType: 'touch' }).diagnosticActivation === 'trusted-touch', 'other currentTarget must retain touch source inside a scope');
  check(read(click(null)).diagnosticActivation === 'scripted', 'null currentTarget must not match');
  check(read().diagnosticActivation === 'unknown', 'scope without event must not infer gamepad');
  check(read(click()).diagnosticEventTrusted === false, 'engine must preserve synthetic trusted flag');
  return 17;
}) === 17, 'scope must preserve callback result');
check(read(click()).diagnosticActivation === 'scripted', 'scope must clear after normal return');
withActivation(target, () => {
  withActivation(other, () => {
    check(read(click(other)).diagnosticActivation === 'gamepad-engine', 'nested scope must use its own target');
    check(read(click()).diagnosticActivation === 'scripted', 'outer target must not leak into nested scope');
  });
  check(read(click()).diagnosticActivation === 'gamepad-engine', 'nested return must restore outer marker');
  withActivation(target, () => check(read(click()).diagnosticActivation === 'gamepad-engine', 'same-target reentry must work'));
  check(read(click()).diagnosticActivation === 'gamepad-engine', 'same-target reentry must preserve outer scope');
  const failure = new Error('nested-click-failure');
  let caught;
  try {
    withActivation(other, () => { throw failure; });
  } catch (error) { caught = error; }
  check(caught === failure, 'scope must preserve exception identity');
  check(read(click()).diagnosticActivation === 'gamepad-engine', 'exception must restore previous marker');
});
let caught;
const failure = new Error('outer-click-failure');
try { withActivation(target, () => { throw failure; }); } catch (error) { caught = error; }
check(caught === failure, 'outer exception must propagate');
check(read(click()).diagnosticActivation === 'scripted', 'throwing outer scope must not leak');
const pending = withActivation(target, () => Promise.resolve().then(() => read(click()).diagnosticActivation));
check(read(click()).diagnosticActivation === 'scripted', 'promise return must restore marker synchronously');
check(await pending === 'scripted', 'marker must not persist to a microtask');
for (const [pointerType, expected] of [['touch', 'trusted-touch'], ['pen', 'trusted-pen'], ['mouse', 'trusted-mouse'], ['', 'trusted-other'], [undefined, 'trusted-other'], ['keyboard', 'trusted-other'], ['private-device-name', 'trusted-other']]) {
  const event = { ...click(), isTrusted: true, pointerType, detail: 1 };
  check(read(event).diagnosticActivation === expected, 'trusted pointer classification: ' + pointerType);
  check(read({ ...event, isTrusted: false }).diagnosticActivation === 'scripted', 'pointerType alone must not imply trusted input');
}
for (const trusted of [undefined, null, 0, 1, 'true', 'false']) {
  const evidence = read({ ...click(), isTrusted: trusted, pointerType: 'touch' });
  check(evidence.diagnosticActivation === 'unknown' && !('diagnosticEventTrusted' in evidence), 'malformed trusted state must remain unknown');
}
for (const [detail, expected] of [[0, 0], [1, 1], [2, 2], [3, 3], [-1, 0], [4, 3], [1000000, 3], [1.9, 1], [-0.5, 0]]) {
  check(read({ ...click(), detail }).diagnosticClickDetail === expected, 'click detail normalization: ' + detail);
}
for (const detail of [NaN, Infinity, -Infinity, undefined, null, '2', {}]) {
  check(!('diagnosticClickDetail' in read({ ...click(), detail })), 'non-finite or non-number detail must be omitted');
}
// Keep the public bridge contract optional and closed, including future edits.
const api = ts.createSourceFile('api.ts', apiSource, ts.ScriptTarget.Latest, true);
const contract = api.statements.find(n => ts.isInterfaceDeclaration(n) && n.name.text === 'WindowActionEvidence');
for (const name of ['diagnosticActivation', 'diagnosticEventTrusted', 'diagnosticClickDetail']) {
  const member = contract.members.find(n => n.name?.getText(api) === name);
  check(!!member?.questionToken, name + ' must remain optional');
  if (name === 'diagnosticActivation') {
    check(ts.isUnionTypeNode(member.type), 'activation contract must use a literal enum');
    assert.deepEqual(member.type.types.map(n => n.literal.text).sort(), [...activations].sort()); checks++;
  } else {
    check(member.type.kind === (name === 'diagnosticEventTrusted' ? ts.SyntaxKind.BooleanKeyword : ts.SyntaxKind.NumberKeyword), name + ' contract type');
  }
}
check(!/\b(?:setTimeout|setInterval|requestAnimationFrame|addEventListener|dispatchEvent|KeyboardEvent|PointerEvent)\b/.test(source), 'evidence module must remain synchronous and passive');
check(fs.readFileSync(path.join(repo, 'src/gamepad/activationEvidence.ts'), 'utf8') === source, 'module changed while testing');
console.log(JSON.stringify({ pass: true, checks, moduleSha256: crypto.createHash('sha256').update(source).digest('hex'), scope: 'Real TypeScript module in a memory fixture; no DOM, IPC, OS input or Shell API', actualUiAcceptance: false }));
