import assert from 'node:assert/strict';
import { createGamepadPresenter, EMPTY_GAMEPAD_PRESENTATION, reduceGamepadPresentation } from '../src/bridge/gamepadPresentation';

let assertions = 0;
function check(ok: unknown, message: string): void { assert.ok(ok, message); assertions++; }
const payload = { connected: true, name: 'ROG test fixture', buttons: [false, true, false], axes: [0, 0.125, -0.5, 1] };
const first = reduceGamepadPresentation(EMPTY_GAMEPAD_PRESENTATION, payload);
check(first.connected && first.name === payload.name, 'native readiness/name preserved');
check(first.axes[1] === 0.125 && first.axes[2] === -0.5, 'axes are not quantized or rounded');
check(reduceGamepadPresentation(first, { ...payload, tickMs: 123 }) === first, 'unchanged display snapshot retains object identity');
const axisOnly = reduceGamepadPresentation(first, { ...payload, axes: [0, 0.126, -0.5, 1] });
check(axisOnly !== first && axisOnly.buttons === first.buttons && axisOnly.axes !== first.axes, 'axis changes cannot restart button watchers');
const buttonOnly = reduceGamepadPresentation(first, { ...payload, buttons: [true, true, false] });
check(buttonOnly.axes === first.axes && buttonOnly.buttons !== first.buttons, 'button changes do not invalidate axis consumers');
const normalized = reduceGamepadPresentation(first, { ...payload, buttons: [0, 1, null], axes: ['0', '0.125', -0.5, 1] });
check(normalized === first, 'normalization is equivalent to legacy Boolean/Number maps');
const bad = reduceGamepadPresentation(first, { connected: false, name: '', buttons: null, axes: [NaN, Infinity, -Infinity, undefined] });
check(!bad.connected && bad.name === '手柄' && bad.buttons.length === 0 && bad.axes.every((v) => v === 0), 'malformed/nonfinite values use original fallback semantics');
check(reduceGamepadPresentation(bad, { connected: false, axes: [0, 0, 0, 0] }) === bad, 'repeated malformed snapshots do not allocate');
let events: typeof first[] = [];
const presenter = createGamepadPresenter((next) => events.push(next));
presenter.accept(payload);
check(events.length === 0, 'inactive view does no presentation work');
presenter.setVisible(true);
check(events.length === 1 && events[0].buttons[1], 'first activation immediately replays latest native state');
for (let n = 0; n < 10000; n++) presenter.accept({ ...payload, nativeSequence: n });
check(events.length === 1, '10000 equivalent visible snapshots create zero reactive replacements');
presenter.setVisible(false);
for (let n = 0; n < 10000; n++) presenter.accept({ ...payload, axes: [n / 10000, 0.125, -0.5, 1] });
check(events.length === 1, '10000 changing hidden snapshots create zero reactive replacements');
presenter.setVisible(true);
check(events.length === 2 && events[1].axes[0] === 0.9999, 'resume replays exactly the latest state, not an event backlog');
presenter.setVisible(true);
check(events.length === 2, 'duplicate activation is idempotent');
presenter.accept({ ...payload, connected: false, buttons: [] });
check(events.length === 3 && !events[2].connected, 'disconnect is reflected immediately when visible');
presenter.dispose();
presenter.accept(payload);presenter.setVisible(false);presenter.setVisible(true);
check(events.length === 3, 'disposed view never receives a late update');

// Randomized parity with the original listener; no deadzone/new threshold is introduced.
let state = EMPTY_GAMEPAD_PRESENTATION;
let seed = 17;
const rand = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 2 ** 32; };
for (let n = 0; n < 2000; n++) {
  const raw = { connected: rand() > 0.2, name: n % 3 === 0 ? '' : 'pad', buttons: Array.from({ length: 16 }, () => rand() > 0.5), axes: Array.from({ length: 4 }, () => rand() * 2 - 1) };
  state = reduceGamepadPresentation(state, raw);
  assert.deepEqual(state, { connected: Boolean(raw.connected), name: String(raw.name || '手柄'), buttons: raw.buttons.map(Boolean), axes: raw.axes.map((v) => Number.isFinite(Number(v)) ? Number(v) : 0) });
}
assertions++;

// Load the actual IPC bridge only after installing a harmless in-memory WebView.
const fakeWindow = new EventTarget() as EventTarget & { chrome: unknown };
const bridgeListeners: ((event: { data: unknown }) => void)[] = [];
let post: ((message: any) => void) | null = null;
fakeWindow.chrome = { webview: { addEventListener: (_: unknown, fn: (event: { data: unknown }) => void) => bridgeListeners.push(fn), postMessage: (m: unknown) => post?.(m) } };
(globalThis as any).window = fakeWindow;
const ipc = await import('../src/bridge/ipc');
const send = (data: unknown) => bridgeListeners.forEach((fn) => fn({ data }));
send({ event: 'gamepad.state', data: { connected: true, name: 'cached' } });
let replayed = 0;
const stop = ipc.on('gamepad.state', () => replayed++);stop();await Promise.resolve();
check(replayed === 0, 'unsubscribe cancels pending cached replay');
const received: string[] = [];
const stop2 = ipc.on<{ name: string }>('gamepad.state', (m) => received.push(m.name));
send({ event: 'gamepad.state', data: { name: 'newer' } });await Promise.resolve();
check(received.length === 1 && received[0] === 'newer', 'fresh native event supersedes delayed cached replay');
stop2();
const cached: string[] = [];
const stop3 = ipc.on<{ name: string }>('gamepad.state', (m) => cached.push(m.name));await Promise.resolve();
check(cached.length === 1 && cached[0] === 'newer', 'late-mounted view still gets latest snapshot');stop3();
let actions = 0;const stopAction = ipc.on('gamepad.ui.action', () => actions++);
for (let n = 0; n < 5; n++) send({ event: 'gamepad.ui.action', data: { action: 'confirm' } });
check(actions === 5, 'one-shot native actions are never coalesced');stopAction();
post = (m) => send({ id: m.id, result: 42 });
check(await ipc.invoke('read-only-test') === 42, 'normal IPC request/reply stays functional');
post = () => undefined;
const pending = ipc.invoke('pending-read-only-test').then(() => false, (e) => String(e).includes('recovering'));
ipc.rejectAllPending();check(await pending, 'recovery still rejects pending IPC');
console.log(`gamepad presentation selftest: PASS (${assertions} assertions + 2000 randomized parity cases; no device/system mutation)`);
