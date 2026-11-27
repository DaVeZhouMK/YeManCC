import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';

const cases: { name: string; passed: boolean }[] = [];

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(`CUSTOM_STEAM_LIBRARY_INPUT_ASSERT: ${message}`);
  cases.push({ name: message, passed: true });
}

const root = process.cwd();
const args = process.argv.slice(2);
const sessionOnly = args.includes('--session-only');
const native = fs.readFileSync(path.join(root, 'native', 'main.cpp'), 'utf8');
const controllerUiOutput = fs.readFileSync(path.join(root, 'native', 'virtual_controller_ui_output_runtime.h'), 'utf8');
const bridge = fs.readFileSync(path.join(root, 'src', 'bridge', 'customSteamLibrary.ts'), 'utf8');
const engine = fs.readFileSync(path.join(root, 'src', 'gamepad', 'engine.ts'), 'utf8');
const customLibraryUi = fs.readFileSync(path.join(root, 'CustomSteamLibrary', 'workspace-ui', 'app.js'), 'utf8');
const customLibraryHtml = fs.readFileSync(path.join(root, 'CustomSteamLibrary', 'workspace-ui', 'index.html'), 'utf8');
const customLibraryCss = fs.readFileSync(path.join(root, 'CustomSteamLibrary', 'workspace-ui', 'styles.css'), 'utf8');
const customLibraryWorker = fs.readFileSync(path.join(root, 'native', 'custom-steam-library', 'steam_artwork_lab.cpp'), 'utf8');

if (!sessionOnly) checkStaticContracts();

function checkStaticContracts(): void {
assert(customLibraryWorker.includes('prepareSteamRecentSignalUpdate('), 'worker must prepare optional recent metadata without requiring localconfig.vdf');
assert(customLibraryWorker.includes('const bool injectSimulatedRecent = recentUpdate.enabled;'), 'recent metadata must only enter the transaction after successful preparation');

assert(native.includes('return @($definitions.ToArray())'), 'PowerShell layout roots must return a plain array');
assert(native.includes('installRoot + L"\\\\YeManCC\\\\CustomSteamLibrary"'), 'native updater canonical child path is missing');
assert(!native.includes("CustomSteamLibrary target is fixed"), 'CustomSteamLibrary target is still hard-coded to the legacy sibling');
assert(bridge.includes("C:\\\\SOFT\\\\YeMan\\\\YeManCC\\\\CustomSteamLibrary"), 'bridge canonical child path is missing');
assert(bridge.indexOf("joinWindowsPath(exeDir, 'CustomSteamLibrary\\\\CustomSteamLibrary.exe')") >= 0, 'bridge must prefer the nested child beside YeManCC.exe');
assert(!bridge.includes('LEGACY_CUSTOM_STEAM_LIBRARY_ROOT'), 'bridge must not retain the removed legacy path fallback');

// The native YeManCC gamepad loop is the only arbitration owner. This test is
// intentionally a source contract check: a renderer-side gate must not be
// reintroduced as a second input state machine.
assert(native.includes('customSteamLibraryProcessMatches'), 'native child executable identification is missing');
assert(native.includes('customSteamLibraryParentOwned'), 'native parent-owner validation is missing');
assert(native.includes('customSteamLibrarySendSemanticAction'), 'native direct semantic forwarding is missing');
assert(native.includes('if (customSteamLibraryChildForeground())'), 'native child ownership branch is missing');
assert(native.includes('g_customSteamLibraryInputDeadline'), 'native launch/return deadline is missing');
assert(native.includes('gamepad.input-owner'), 'native ownership notification is missing');
assert(!native.includes('gamepadEmitUiAction("dropdown")'), 'native X button still emits dropdown');
assert(!engine.includes("if (pressed(2))"), 'renderer fallback still maps X to dropdown');
assert(!engine.includes("'dropdown'"), 'renderer still exposes dropdown as a native face-button action');
assert(!bridge.includes('customSteamLibraryInputGate'), 'renderer input gate is still imported');
assert(!bridge.includes('customSteamLibrarySessionActive'), 'renderer still decides input ownership');
assert(!engine.includes('customSteamLibrarySessionActive'), 'gamepad engine still delegates arbitration to renderer');
assert(engine.includes('nativeChildInputOwned'), 'renderer does not honor native ownership notification');
// The child receives the parent's physical sample. Keep Steam's virtual Deck
// desktop mapping quiet through the shared output isolation boundary; native
// navigation continues to use its own input admission when main isolation is OFF.
assert(native.includes('#include "virtual_controller_ui_output_runtime.h"'), 'main native consumer must include controller UI output runtime');
for (const gate of ['steamDeckUiOwnsInput', 'ps5UiOwnsInput', 'msiUiOwnsInput', 'virtualUiOwnsInput']) {
  const body = controllerUiOutput.match(new RegExp('static bool ' + gate + '\\([^)]*\\)\\s*\\{([\\s\\S]*?)\\n\\}'))?.[1] || '';
  assert(body.includes('return gamepadUiOutputIsolationEligible();'), gate + ' must reuse native UI output isolation admission');
  assert(!body.includes('CustomSteamLibraryInputPhase'), gate + ' must not bypass child foreground ownership');
}
assert(/const auto dsuDemand\s*=\s*dsu_frame_demand\(\)\s*;/.test(native), 'native DSU isolation must use the current subscriber demand');
assert(/const bool dsuUiNeutral\s*=\s*dsuDemand\s*&&\s*\(\s*g_realStickTest\.enabled\s*&&\s*g_realStickTest\.virtualTargetEnabled\s*&&\s*focusPlan\.virtualUiPersona\[0\]\s*\?\s*virtualUiNeutral\s*:\s*gamepadUiOutputIsolationEligible\(\)\s*\)\s*;/.test(native), 'native DSU fallback must reuse UI output isolation admission when demanded');
assert(/const bool dsuSubmitNeutral\s*=\s*submitNeutral\s*\|\|\s*dsuUiNeutral\s*;/.test(native) &&
  /dsuFrame\.semanticNeutral\s*=\s*dsuSubmitNeutral\s*;/.test(native), 'native DSU wire output must carry the combined neutral decision');
// Import eligibility is not Steam identity verification. Exact production
// helpers are exercised below, including waiting shortcuts without IDs/images.
assert(!customLibraryUi.includes('hasMinimumSteamArtwork(game)'), 'artwork must not be a prerequisite for shortcut import');
assert(customLibraryUi.includes("const steamIdPending = activeTab === 'not-in-steam' && !canSelect;"), 'non-selectable cards must retain their primary-click guard');
assert(customLibraryUi.includes('else if (!steamIdPending) openEdit(game);'), 'blocked cards must not open the editor on a single click');
assert(customLibraryUi.includes('function artworkSearchTypeForSlot(type) { return type; }'), 'each artwork slot must preserve its own search type');
assert(customLibraryUi.includes('for (const type of ["cover", "long", "wallpaper"])'), 'all three artwork slots must render and save independently');
assert(customLibraryUi.includes("const cardArtwork = artworkEntry(game, 'cover');"), 'card glow must use the same manual-over-auto artwork priority as the visible cover');
// Long/hero preview mapping is tested by executing artworkEntry(), rather
// than matching one spelling of optional chaining.
assert(customLibraryHtml.includes('data-artwork-type="long"') && customLibraryHtml.includes('artwork-long-preview'), 'editor must expose the editable horizontal cover slot');
assert(customLibraryHtml.includes('data-artwork-type="wallpaper"') && customLibraryHtml.includes('artwork-wallpaper-preview'), 'editor must expose the editable wallpaper slot');
assert(customLibraryHtml.match(/data-artwork-editable="true"/g)?.length === 6, 'all three artwork panes and their previews must be editable');
assert(customLibraryCss.includes('grid-template-columns:minmax(133px, .238fr) minmax(0, 1fr);'), 'editor artwork layout must reduce the portrait column and keep wallpaper beside it');
const horizontalSearchHelper = customLibraryWorker.match(/static bool isHorizontalArtworkSearchType\([^)]*\)\s*\{([^}]*)\}/)?.[1] || '';
assert(horizontalSearchHelper.includes('type == "wallpaper"') && horizontalSearchHelper.includes('type == "long"'), 'worker must accept long artwork requests from the editor');
assert(customLibraryWorker.includes('sources[std::to_string(ids.longId)] = manualLong;'), 'existing Steam games must refresh the Big Picture long artwork from the horizontal cover');
assert(customLibraryWorker.includes('stageOne(manualLong, targetGrid / toWide(longId + extension));'), 'new Steam shortcuts must stage the Big Picture long artwork');
for (const action of [
  'navigate-left', 'navigate-right', 'navigate-up', 'navigate-down',
  'accept', 'back', 'tab-previous', 'tab-next', 'edit',
]) {
  assert(native.includes(`"${action}"`), `native semantic action is missing: ${action}`);
}
}


// Execute production helpers with mocked filesystem/IPC. No fixture can start
// Steam, launch a real child or read real user data. Session timers below use a virtual clock.
// Resolve installed dependencies at runtime: bundling esbuild itself breaks
// its native executable lookup in a Build-directory bundle.
const runtimeRequire = createRequire(path.join(root, 'package.json'));
const { transformSync } = runtimeRequire('esbuild');
const ts = runtimeRequire('typescript');
const bridgeCode = transformSync(bridge, { loader: 'ts', format: 'cjs', target: 'es2020' }).code;
const separator = String.fromCharCode(92);
const windowsPath = (...parts: string[]) => parts.join(separator);

interface BridgeRuntime {
  globals: Record<string, unknown>;
  initialize(context: vm.Context): void;
  invoke(command: string, args?: object, options?: object): Promise<any>;
  on(event: string, handler: (data: unknown) => void): () => void;
  ipc(): any;
  power(): any;
}
function bridgeFixture(exeDir: string, existing: string[] = [], files: Record<string, string> = {}, denied: string[] = [], runtime?: BridgeRuntime) {
  const existsCalls: string[] = [], readCalls: string[] = [];
  const unexpected = () => { throw new Error('Unexpected process/window/IPC operation in read-only fixture'); };
  const api = {
    app: { exeDir: async () => exeDir, pid: runtime ? (timeoutMs?: number, signal?: AbortSignal) => runtime.invoke('app.pid', {}, { timeoutMs, signal }) : unexpected },
    fs: {
      exists: async (file: string) => {
        existsCalls.push(file);
        if (denied.includes(file)) throw new Error('Fixture access denied');
        return existing.includes(file);
      },
      readTextFile: async (file: string) => {
        readCalls.push(file);
        if (!Object.prototype.hasOwnProperty.call(files, file)) throw new Error('Fixture file unavailable');
        return files[file];
      },
    },
    shell: { hidden: runtime ? (program: string, args: string[], timeoutMs?: number, _creationReceipt?: boolean,
      options?: { powerScope?: readonly number[]; mutationTicket?: number }) =>
      runtime.invoke('shell.hidden', { program, args, ...options }, { timeoutMs }) : unexpected },
    windowApi: { minimize: runtime ? () => runtime.invoke('window.minimize') : unexpected,
      show: runtime ? () => runtime.invoke('window.show') : unexpected },
  };
  const module = { exports: {} };
  const context = vm.createContext({
    module, exports: module.exports,
    require: (name: string) => {
      if (name === '@/bridge/api') return api;
      if (name === '@/bridge/ipc') return runtime?.ipc() ?? { invoke: unexpected, on: unexpected };
      if (name === './powerLifecycleState') return runtime?.power() ?? {};
      throw new Error(`Unexpected dependency: ${name}`);
    },
    setTimeout: unexpected, setInterval: unexpected, clearTimeout: unexpected, clearInterval: unexpected,
    ...runtime?.globals,
  });
  runtime?.initialize(context);
  vm.runInContext(bridgeCode, context, { timeout: 1000 });
  return { existsCalls, readCalls,
    resolve: () => vm.runInContext('resolveExecutable()', context, { timeout: 1000 }) as Promise<string>,
    summary: () => vm.runInContext('readCustomSteamLibrarySummary()', context, { timeout: 1000 }) as Promise<any>,
    launch: () => vm.runInContext('launchCustomSteamLibrary()', context, { timeout: 1000 }) as Promise<any>,
    stop: () => vm.runInContext('stopCustomSteamLibrarySession()', context, { timeout: 1000 }) as Promise<void>,
    hasStatusTimer: () => vm.runInContext('sessionWatch?.timer != null', context, { timeout: 1000 }) as boolean,
  };
}

// One combined in-memory transport case reuses the exact bridge and IPC modules
// in the same VM. Only the native endpoint and clock are simulated. The original
// path/summary fixtures above still reject all timers and app/IPC operations.
function sessionFixture() {
  let now = 100000, nextTimer = 1, intervalCallbacks = 0, peakPendingStatus = 0;
  const timers = new Map<number, { at: number; repeat: number; handoff: boolean; callback: () => void }>();
  const messages: { id: number; cmd: string; args: any; answered: boolean }[] = [];
  const events: { type: string; detail?: any }[] = [];
  const hold = new Set<string>(['status']);
  const faults = new Set<string>();
  const queued = new Set<string>();
  let ticket = 0;
  const receipts = new Map<number, { state: string; outcome?: object }>();
  let nativeEnabled = false, launchOk = true;
  let status: any = { configured: true, present: true, foreground: false, compatible: true, inputOwner: 'parent', pid: 77 };
  const initialPower = { phase: 'ready', known: true, generation: 3, suspendSerial: 1,
    hardwareWritesAllowed: true, observedNotificationSequence: 2, publicationRevision: 10 };
  let power = { ...initialPower };
  let receiver: ((event: any) => void) | undefined;
  let ipc: any, powerApi: any, pendingIds: () => number[];
  const flush = async () => { for (let i = 0; i < 48; ++i) await Promise.resolve(); };
  const setTimer = (callback: () => void, delay: number, repeat = 0) => {
    const id = nextTimer++; timers.set(id, { at: now + delay, repeat, handoff: delay === 800, callback }); return id;
  };
  const key = (message: { cmd: string; args: any }) => message.cmd === 'customSteamLibrary.status' ? 'status'
    : message.cmd === 'customSteamLibrary.setIntegrationSession' ? message.args.enabled ? 'enable' : 'disable'
    : message.cmd === 'shell.hidden' ? 'launch' : message.cmd === 'app.pid' ? 'pid' : message.cmd;
  const reply = (message: typeof messages[number], value?: any, error?: string, deliver = true) => {
    if (message.answered) throw new Error('Fixture response already delivered');
    message.answered = true;
    const kind = key(message);
    const mutationTicket = message.args?.mutationTicket;
    const tracked = ['enable', 'disable', 'launch'].includes(kind) && mutationTicket > 0;
    if (tracked && receipts.get(mutationTicket)?.state === 'retired') error = 'mutation retired';
    if (!error && kind === 'enable') nativeEnabled = true;
    if (!error && kind === 'disable') nativeEnabled = false;
    const result = value !== undefined ? value : (() => {
      switch (message.cmd) {
        case 'customSteamLibrary.mutationTicket':
          if ([...receipts.values()].some(r => r.state === 'running' || r.state === 'queued')) return 0;
          receipts.set(++ticket, { state: 'reserved' });
          return ticket;
        case 'customSteamLibrary.settleMutation': {
          const current = receipts.get(mutationTicket);
          if (current?.state === 'reserved' || current?.state === 'queued') current.state = 'retired';
          return { ticket: mutationTicket, ...(current ?? { state: 'unknown' }) };
        }
        case 'power.lifecycle': return { ...power };
        case 'customSteamLibrary.status': return { ...status };
        case 'app.pid': return 12;
        case 'shell.hidden': return { ok: launchOk, pid: 77 };
        case 'customSteamLibrary.setIntegrationSession':
        case 'window.minimize':
        case 'window.show': return true;
        default: throw new Error('Unexpected session fixture IPC: ' + message.cmd);
      }
    })();
    if (tracked && receipts.get(mutationTicket)?.state !== 'retired')
      receipts.set(mutationTicket, { state: 'completed', outcome: error ? { error } : { result } });
    if (deliver) receiver!({ data: error ? { id: message.id, error } : { id: message.id, result } });
  };
  const pendingStatus = () => {
    const ids = new Set(pendingIds()); return messages.filter(m => ids.has(m.id) && key(m) === 'status').length;
  };
  const webview = {
    addEventListener: (_type: string, listener: (event: any) => void) => { receiver = listener; },
    postMessage: (message: any) => {
      if (!['customSteamLibrary.status', 'customSteamLibrary.setIntegrationSession', 'app.pid',
        'customSteamLibrary.mutationTicket', 'customSteamLibrary.settleMutation',
        'shell.hidden', 'window.minimize', 'window.show', 'power.lifecycle'].includes(message.cmd)) {
        throw new Error('Unexpected session fixture IPC: ' + message.cmd);
      }
      const owned = { ...message, answered: false }; messages.push(owned);
      peakPendingStatus = Math.max(peakPendingStatus, pendingStatus());
      const kind = key(owned);
      if (['enable', 'disable', 'launch'].includes(kind) && owned.args.mutationTicket > 0)
        receipts.set(owned.args.mutationTicket, { state: queued.has(kind) ? 'queued' : 'running' });
      if (!hold.has(kind)) queueMicrotask(() => {
        if (!owned.answered) reply(owned, undefined, faults.has(kind) ? 'fixture native rejection: ' + kind : undefined);
      });
    },
  };
  const browserWindow = new EventTarget();
  const browserDocument = Object.assign(new EventTarget(), { visibilityState: 'hidden' });
  const runtime: BridgeRuntime = {
    globals: {
      Date: { now: () => now }, AbortController, queueMicrotask,
      CustomEvent,
      window: { chrome: { webview }, addEventListener: browserWindow.addEventListener.bind(browserWindow),
        removeEventListener: browserWindow.removeEventListener.bind(browserWindow), dispatchEvent: (event: CustomEvent) => {
          events.push({ type: event.type, detail: event.detail }); return browserWindow.dispatchEvent(event);
        } },
      document: browserDocument,
      setInterval: (callback: () => void, delay: number) => setTimer(callback, delay, delay),
      setTimeout: (callback: () => void, delay: number) => setTimer(callback, delay),
      clearInterval: (id: number) => timers.delete(id), clearTimeout: (id: number) => timers.delete(id),
    },
    initialize(context) {
      const source = fs.readFileSync(path.join(root, 'src/bridge/ipc.ts'), 'utf8');
      const code = transformSync(source, { loader: 'ts', format: 'cjs', target: 'es2020' }).code;
      // Test-only accessor observes the real private pending Map; it does not
      // replace invoke, its third argument, abort listener, or response handling.
      const actual = vm.runInContext('(function(){ const module={exports:{}}; const exports=module.exports;\n' + code +
        '\nreturn {api:module.exports,ids:()=>Array.from(pending.keys())}; })()', context, { timeout: 1000 });
      ipc = actual.api; pendingIds = actual.ids;
      const powerSource = fs.readFileSync(path.join(root, 'src/bridge/powerLifecycleState.ts'), 'utf8');
      const powerCode = transformSync(powerSource, { loader: 'ts', format: 'cjs', target: 'es2020' }).code;
      const loadPower = vm.runInContext('(ipc=>{ const module={exports:{}}; const exports=module.exports; ' +
        'const require=name=>{if(name!=="./ipc")throw Error(name);return ipc;};\n' + powerCode + '\nreturn module.exports; })',context);
      powerApi = loadPower(ipc);
    },
    invoke: (command, args, options) => ipc.invoke(command, args, options),
    on: (event, handler) => ipc.on(event, handler),
    power: () => powerApi,
    ipc: () => ipc,
  };
  const exeDir = windowsPath('G:', 'Fixture', 'Session');
  const bridge = bridgeFixture(exeDir, [windowsPath(exeDir, 'CustomSteamLibrary', 'CustomSteamLibrary.exe')], {}, [], runtime);
  let stopParentPower: (() => void) | undefined;
  const requests = (kind: string) => messages.filter(m => key(m) === kind);
  const outstanding = (kind: string) => requests(kind).filter(m => !m.answered);
  const snapshot = () => ({ statusRequests: requests('status').length, peakPendingStatus, pendingStatus: pendingStatus(),
    localPending: pendingIds().length, intervalCallbacks, timers: [...timers.values()].filter(t => !t.handoff).length,
    statusTimer: bridge.hasStatusTimer(),
    enables: requests('enable').length, disables: requests('disable').length, launches: requests('launch').length,
    shows: requests('window.show').length, minimizes: requests('window.minimize').length,
    closed: events.filter(e => e.type === 'customSteamLibrary:closed').length,
    conflicts: events.filter(e => e.type === 'customSteamLibrary:conflict').length, nativeEnabled });
  const advance = async (ms: number) => {
    // Real timer tasks run after the preceding microtask checkpoint.
    await flush();
    const end = now + ms;
    for (let turns = 0; ; ++turns) {
      if (turns > 10000) throw new Error('Virtual clock runaway');
      const next = [...timers.entries()].filter(([, t]) => t.at <= end).sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0];
      if (!next) break;
      now = next[1].at;
      if (next[1].repeat) { next[1].at += next[1].repeat; ++intervalCallbacks; } else timers.delete(next[0]);
      next[1].callback(); await flush();
    }
    now = end; await flush();
  };
  // Harness recovery between subcases; never counted as product cleanup proof.
  const reset = async () => {
    hold.clear(); faults.clear(); launchOk = true;
    queued.clear();
    for (let i = 0; i < 4; ++i) {
      for (const message of messages) if (!message.answered) reply(message);
      await flush();
    }
    const stopped = bridge.stop(); await flush(); await stopped;
    stopParentPower?.(); stopParentPower = undefined;
    ipc.rejectAllPending('fixture teardown'); await flush();
    timers.clear(); messages.length = 0; events.length = 0; intervalCallbacks = 0; peakPendingStatus = 0;
    receipts.clear();
    browserDocument.visibilityState = 'hidden';
    now += 20000; hold.add('status');
    power = { ...initialPower };
    status = { configured: true, present: true, foreground: false, compatible: true, inputOwner: 'parent', pid: 77 };
  };
  return { bridge, flush, advance, reset, hold, faults, queued, reply, requests, outstanding, pendingStatus, snapshot,
    completion: (receipt: number) => receiver!({ data: { event: 'customSteamLibrary.mutationCompleted', data: { ticket: receipt } } }),
    async startParentPower() {
      stopParentPower = powerApi.onSharedPowerLifecycle(() => {});
      await flush();
    },
    status: (value: any) => { status = { ...status, ...value }; }, launchOk: (value: boolean) => { launchOk = value; },
    focus: () => browserWindow.dispatchEvent(new Event('focus')),
    visibility: (state: string) => { browserDocument.visibilityState = state; browserDocument.dispatchEvent(new Event('visibilitychange')); },
    owner: (owner: string) => receiver!({ data: { event: 'gamepad.input-owner', data: { owner } } }),
    power: (next: Partial<typeof initialPower>) => {
      power = { ...power, ...next, publicationRevision: power.publicationRevision + 1 };
      receiver!({ data: { event: 'power.lifecycleChanged', data: { ...power } } });
    },
    activeStatus: () => { const ids = new Set(pendingIds()); return requests('status').filter(m => ids.has(m.id)); },
    unexpected: () => ipc.invoke('fixture.unexpected-session-command'),
    pending: () => pendingIds().length };
}

const sessionResults: { name: string; passed: boolean; error?: string; behavior: ReturnType<ReturnType<typeof sessionFixture>['snapshot']> }[] = [];
async function checkSessionContracts(): Promise<void> {
  const f = sessionFixture(); // one real-IPC combination, one VM, virtual time only
  const check = async (name: string, run: () => Promise<void>) => {
    await f.reset();
    await f.startParentPower();
    let error: string | undefined;
    try { await run(); } catch (failure) {
      error = failure instanceof Error ? failure.message : String(failure);
      cases.push({ name: name + ': ' + error, passed: false });
    }
    sessionResults.push({ name, passed: !error, error, behavior: f.snapshot() });
  };
  const assertSuccessorContinues = async (label: string) => {
    f.hold.add('status');
    if (f.activeStatus().length === 0) await f.advance(2000);
    const active = f.activeStatus();
    assert(active.length === 1, label + ' successor has exactly one actual pending status read');
    f.reply(active[0]); await f.flush(); const before = f.snapshot();
    await f.advance(2000); const next = f.snapshot();
    assert(next.statusRequests === before.statusRequests + 1 && next.pendingStatus === 1 && next.timers === 1,
      label + ' successor keeps a single pending audit on its own cadence');
    await f.advance(250); const busy = f.snapshot();
    assert(busy.statusRequests === next.statusRequests && busy.peakPendingStatus === 1,
      label + ' successor slow read remains single-flight after old completion');
    assert(busy.shows === before.shows && busy.disables === before.disables && busy.closed === before.closed && busy.conflicts === before.conflicts,
      label + ' next polling slot publishes no old-session side effects');
  };
  await check('unknown-ipc-rejected', async () => {
    await rejects(() => f.unexpected(), 'session transport rejects an unknown IPC instead of inventing success');
    assert(f.pending() === 0, 'unknown IPC refusal leaves the actual pending Map empty');
  });
  await check('healthy-hidden-parent-reduces-reads', async () => {
    f.hold.delete('status'); await f.bridge.launch(); await f.advance(16000);
    assert(f.snapshot().statusRequests === 9 && f.snapshot().intervalCallbacks === 0, 'healthy hidden parent issues one initial read and eight audits in 16 seconds');
    assert(f.snapshot().peakPendingStatus === 1, 'normal watch has only one actual IPC pending status');
    await f.bridge.stop(); await f.flush();
    f.focus(); f.visibility('visible'); f.owner('yemancc'); await f.advance(2000);
    assert(f.pending() === 0 && f.snapshot().timers === 0 && f.snapshot().statusRequests === 9, 'stop retires timer, pending read and return hints');
  });
  await check('slow-read-one-pending', async () => {
    await f.bridge.launch(); await f.advance(2000);
    assert(f.snapshot().statusRequests === 1 && f.pendingStatus() === 1, 'slow startup read stays single-flight');
    assert(f.snapshot().intervalCallbacks === 0 && f.snapshot().timers === 1, 'only the IPC timeout stays armed during a slow read');
    f.reply(f.outstanding('status')[0]); await f.flush(); await f.advance(2000);
    assert(f.snapshot().statusRequests === 2 && f.snapshot().peakPendingStatus === 1, 'settled compatible read arms one audit');
  });
  await check('stop-cleans-real-pending-late-response', async () => {
    await f.bridge.launch(); await f.flush(); const old = f.outstanding('status')[0];
    await f.bridge.stop(); await f.flush();
    assert(f.pending() === 0, 'stop aborts the status through the actual invoke third argument');
    f.reply(old, { present: true, compatible: false, inputOwner: 'host' }); await f.flush();
    assert(f.snapshot().shows === 0 && f.snapshot().conflicts === 0 && f.snapshot().disables === 1, 'late transport response after stop has no side effects');
  });
  await check('resolved-before-stop-continuation', async () => {
    await f.bridge.launch(); await f.flush(); f.reply(f.outstanding('status')[0], { present: true, compatible: false, inputOwner: 'host' });
    const stopped = f.bridge.stop(); await f.flush(); await stopped;
    assert(f.snapshot().shows === 0 && f.snapshot().conflicts === 0 && f.snapshot().disables === 1, 'already resolved old status cannot publish or disable again after stop');
  });
  await check('replace-pending-watch', async () => {
    await f.bridge.launch(); await f.flush(); const old = f.outstanding('status')[0]; await f.bridge.launch(); await f.flush();
    assert(f.pendingStatus() === 1 && f.snapshot().timers === 1, 'replacement aborts old pending before installing one new watch');
    f.reply(old, { present: true, compatible: false, inputOwner: 'host' }); await f.flush();
    assert(f.snapshot().shows === 0 && f.snapshot().disables === 0 && f.snapshot().conflicts === 0, 'old status cannot disable the replacement session');
    await assertSuccessorContinues('replaced pending watch');
  });
  await check('ten-second-startup-grace', async () => {
    f.hold.delete('status'); f.status({ present: false }); await f.bridge.launch(); await f.advance(9999);
    assert(f.snapshot().closed === 0 && f.snapshot().disables === 0, 'missing child retains the full original ten-second startup grace');
    await f.advance(1);
    assert(f.snapshot().closed === 1 && f.snapshot().shows === 1 && f.snapshot().disables === 1, 'missing child closes once at the ten-second boundary');
    assert(f.pending() === 0 && f.snapshot().timers === 0, 'successful startup-timeout close retires pending and watch');
  });
  await check('return-hints-coalesce-and-verify', async () => {
    f.hold.delete('status'); await f.bridge.launch(); await f.advance(500);
    f.owner('custom-steam-library'); f.visibility('hidden'); await f.advance(125);
    assert(f.snapshot().statusRequests === 1, 'child ownership and hidden visibility do not request reads');
    f.focus(); f.visibility('visible'); f.owner('yemancc'); await f.advance(124);
    assert(f.snapshot().statusRequests === 1 && f.snapshot().closed === 0, 'return hints allow the child to finish closing');
    f.status({ present: false }); await f.advance(1);
    assert(f.snapshot().statusRequests === 2 && f.snapshot().closed === 1 && f.snapshot().shows === 1 && f.snapshot().disables === 1,
      'coalesced hints result in one verified close before the audit');
    assert(f.pending() === 0 && f.snapshot().timers === 0, 'successful close owns no resident watch or pending');
  });
  await check('lost-return-hint-audit', async () => {
    f.hold.delete('status'); await f.bridge.launch(); await f.flush(); f.status({ present: false });
    await f.advance(1999);
    assert(f.snapshot().statusRequests === 1 && f.snapshot().closed === 0, 'without hints no fast heartbeat runs after admission');
    await f.advance(1);
    assert(f.snapshot().statusRequests === 2 && f.snapshot().closed === 1 && f.snapshot().shows === 1,
      'a lost hint still restores and closes on the first audit');
  });
  await check('return-arrives-during-read', async () => {
    f.hold.delete('status'); await f.bridge.launch(); await f.flush(); f.hold.add('status'); await f.advance(2000);
    f.owner('yemancc'); f.focus(); await f.advance(500);
    assert(f.snapshot().statusRequests === 2 && f.pendingStatus() === 1, 'return hints do not overtake an in-flight audit');
    f.hold.delete('status'); f.status({ present: false }); f.reply(f.activeStatus()[0], { present: true, compatible: true, inputOwner: 'parent' });
    await f.advance(0);
    assert(f.snapshot().statusRequests === 3 && f.snapshot().closed === 1 && f.snapshot().peakPendingStatus === 1,
      'a return observed during a stale audit gets one fresh read after it settles');
  });
  for (const [label, state] of [['incompatible', { compatible: false }], ['foreign-owner', { inputOwner: 'host' }], ['unknown-owner', { inputOwner: 'unknown' }]] as const) {
    await check(label + '-conflict', async () => {
      f.hold.delete('status'); f.status(state); await f.bridge.launch(); await f.flush();
      assert(f.snapshot().conflicts === 1 && f.snapshot().shows === 1 && f.snapshot().disables === 1, label + ' preserves the original present/compatible/inputOwner conflict');
      assert(f.pending() === 0 && f.snapshot().timers === 0, label + ' conflict retires its resident resources');
    });
  }
  await check('status-rejection-grace', async () => {
    f.hold.delete('status'); f.faults.add('status'); await f.bridge.launch(); await f.advance(9999);
    assert(f.snapshot().closed === 0, 'native status rejections before startup deadline do not fake child absence');
    await f.advance(1);
    assert(f.snapshot().closed === 1 && f.snapshot().disables === 1, 'native status rejection at deadline follows original close behavior');
  });
  await check('lost-status-response-bounded', async () => {
    await f.bridge.launch(); await f.advance(10000);
    assert(f.snapshot().statusRequests === 1 && f.snapshot().closed === 1, 'lost status response expires once at ten seconds without accumulating requests');
    assert(f.pending() === 0 && f.snapshot().timers === 0, 'real IPC timeout and session close retire all pending and timers');
  });
  await check('coalesced-ready-retires-old-window-status', async () => {
    await f.bridge.launch(); await f.flush();
    const old = f.activeStatus()[0]; assert(!!old, 'old window status read is held');
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 }); await f.advance(0);
    const successor = f.activeStatus()[0];
    const retired = !!successor && successor !== old;
    f.reply(old, { present: false }); await f.flush();
    const state = f.snapshot();
    assert(retired && state.statusRequests === 2 && state.pendingStatus === 1 && state.peakPendingStatus === 1,
      'coalesced Ready retires the old IPC and starts one present-scope read');
    assert(state.shows === 0 && state.disables === 0 && state.closed === 0, 'late absence cannot close the window session');
    f.reply(successor!); await f.flush();
    f.hold.delete('status'); f.status({ present: false }); await f.advance(2000);
    const ended = f.snapshot();
    assert(ended.closed === 1 && ended.disables === 1 && ended.shows === 1, 'fresh absence still returns the parent window once');
  });
  await check('suspend-retires-audit-until-confirmed-ready', async () => {
    await f.bridge.launch(); await f.flush(); f.reply(f.activeStatus()[0]);
    await f.advance(2000); const held = f.activeStatus()[0];
    assert(!!held, 'there is an established child and one held audit');
    f.power({ phase: 'suspending', hardwareWritesAllowed: false, suspendSerial: 2, observedNotificationSequence: 3 });
    f.focus(); f.visibility('visible'); f.owner('yemancc'); await f.advance(11000);
    const heldState = f.snapshot();
    assert(heldState.pendingStatus === 0 && !heldState.statusTimer && heldState.statusRequests === 2 && heldState.disables === 0,
      'no status timer, IPC, or disable survives the closed power gate');
    f.power({ phase: 'ready', hardwareWritesAllowed: true, generation: 4, observedNotificationSequence: 4 }); await f.advance(0);
    f.reply(held, { present: true, compatible: false, inputOwner: 'host' }); await f.flush();
    const state = f.snapshot();
    assert(state.statusRequests === 3 && state.pendingStatus === 1 && state.conflicts === 0 && state.disables === 0,
      'Ready starts one new audit and ignores the old conflict');
  });
  await check('wake-transport-failure-is-not-child-exit', async () => {
    await f.bridge.launch(); await f.flush();
    const old = f.activeStatus()[0]; assert(!!old, 'startup observation was interrupted before the first reply');
    f.power({ phase: 'suspended', hardwareWritesAllowed: false, suspendSerial: 2, observedNotificationSequence: 3 });
    await f.advance(12000);
    f.hold.delete('status'); f.faults.add('status');
    f.power({ phase: 'ready', hardwareWritesAllowed: true, generation: 4, observedNotificationSequence: 4 }); await f.advance(0);
    const failed = f.snapshot();
    assert(failed.statusRequests === 2 && failed.closed === 0 && failed.disables === 0 && failed.shows === 0 && failed.timers === 1,
      'failed post-wake startup observation is unknown even after the original deadline');
    f.faults.delete('status'); await f.advance(2000);
    const recovered = f.snapshot();
    assert(recovered.statusRequests === 3 && recovered.closed === 0 && recovered.nativeEnabled && recovered.timers === 1,
      'startup observation recovers on the existing audit cadence');
  });
  for (const reason of ['stop', 'closed', 'conflict']) {
    await check('reopen-during-' + reason + '-disable', async () => {
      f.hold.delete('status'); await f.bridge.launch(); await f.flush(); f.hold.add('disable');
      let stopped: Promise<void> | undefined;
      if (reason === 'stop') { stopped = f.bridge.stop(); await f.flush(); }
      else { f.status(reason === 'closed' ? { present: false } : { compatible: false }); f.focus(); await f.advance(250); }
      const disable = f.outstanding('disable')[0];
      assert(!!disable, reason + ' has an actual unacknowledged native disable');
      f.status({ present: true, compatible: true });
      const reopened = f.bridge.launch(); await f.flush();
      assert(f.snapshot().enables === 1 && f.snapshot().launches === 1, 'new enable and launch wait for the dispatched ' + reason + ' disable');
      f.hold.delete('disable'); f.reply(disable); await f.flush(); if (stopped) await stopped; await reopened; await f.flush();
      assert(f.snapshot().nativeEnabled && f.snapshot().timers === 1, 'old ' + reason + ' disable completion cannot clear the replacement watch');
      assert(f.snapshot().closed === 0 && f.snapshot().conflicts === 0, 'old ' + reason + ' completion cannot publish against replacement');
      await assertSuccessorContinues('old ' + reason + ' disable');
    });
  }
  await check('stop-before-enable-dispatch', async () => {
    const launching = f.bridge.launch(); const stopped = f.bridge.stop(); await f.flush(); await stopped;
    const result = await launching;
    assert(!result.ok && f.snapshot().enables === 0 && f.snapshot().launches === 0, 'superseded queued launch never dispatches enable or shell.hidden');
  });
  await check('old-enable-rejection-after-replacement', async () => {
    f.hold.add('enable'); const old = f.bridge.launch().then(v => v, () => ({ ok: false })); await f.flush();
    const enable = f.outstanding('enable')[0]; const current = f.bridge.launch(); await f.flush();
    assert(f.snapshot().enables === 1, 'new launch waits for the old enable acknowledgement');
    f.hold.delete('enable'); f.reply(enable, undefined, 'fixture rejected old enable'); await f.flush();
    assert(!(await old).ok && (await current).ok, 'old enable rejection cannot fail the new launch');
    assert(f.snapshot().disables === 0 && f.snapshot().nativeEnabled && f.snapshot().timers === 1, 'stale launch catch cannot stop the newer session');
  });
  await check('old-pid-response-after-new-session', async () => {
    f.hold.add('pid'); const old = f.bridge.launch(); await f.flush(); const pid = f.outstanding('pid')[0];
    f.hold.delete('pid'); const current = f.bridge.launch(); await f.flush(); await current;
    f.reply(pid, 12); await f.flush();
    assert(!(await old).ok && f.snapshot().launches === 1 && f.snapshot().minimizes === 1, 'stale launch cannot create a child or minimize after its delayed pid');
  });
  await check('old-shell-launch-already-dispatched', async () => {
    f.hold.add('launch'); const old = f.bridge.launch(); await f.flush(); const launch = f.outstanding('launch')[0];
    const current = f.bridge.launch(); await f.flush();
    assert(f.snapshot().enables === 1 && f.snapshot().launches === 1, 'replacement waits for an already dispatched shell launch receipt');
    f.hold.delete('launch'); f.reply(launch); await f.flush();
    assert(!(await old).ok && (await current).ok && f.snapshot().minimizes === 1, 'old shell receipt cannot publish success or minimize the replacement');
  });
  await check('power-advance-retires-queued-enable', async () => {
    f.hold.add('enable');
    const first = f.bridge.launch(); await f.flush();
    const enable = f.outstanding('enable')[0];
    assert(enable?.args.powerScope?.join(':') === '3:1:2', 'dispatched enable carries the original public scope');
    const queued = f.bridge.launch(); await f.flush();
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 }); await f.flush();
    f.hold.delete('enable'); f.reply(enable, undefined, 'power policy write scope expired'); await f.flush();
    assert(!(await first).ok && !(await queued).ok && f.snapshot().enables === 1 && f.snapshot().launches === 0,
      'a new boundary retires the queued old click without sending either mutation');
    f.hold.add('launch'); const fresh = f.bridge.launch(); await f.flush();
    const launch = f.outstanding('launch')[0];
    assert(launch?.args.powerScope?.join(':') === '3:2:4', 'fresh explicit launch carries the new public scope');
    f.hold.delete('launch'); f.reply(launch); await f.flush();
    assert((await fresh).ok && f.snapshot().launches === 1 && f.snapshot().minimizes === 1,
      'settled old enable leaves the current launch path usable');
  });
  await check('power-advance-retires-parent-pid-read-before-launch', async () => {
    f.hold.add('pid');
    let oldDone = false;
    const old = f.bridge.launch().then(result => { oldDone = true; return result; });
    await f.flush(); const pid = f.outstanding('pid')[0];
    assert(!!pid && f.snapshot().enables === 1 && f.snapshot().launches === 0, 'PID lookup begins after one accepted enable');
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 }); await f.flush();
    const beforeLateReply = { settled: oldDone, pending: f.pending(), disables: f.snapshot().disables };
    f.reply(pid, 12); await f.flush(); const result = await old;
    assert(beforeLateReply.settled && beforeLateReply.pending === 0 && beforeLateReply.disables === 1 && !result.ok &&
      f.snapshot().launches === 0 && f.snapshot().minimizes === 0, 'old PID wait retires before the late response with zero shell dispatch');
    f.hold.delete('pid'); const fresh = await f.bridge.launch(); await f.flush();
    assert(fresh.ok && f.snapshot().launches === 1 && f.snapshot().minimizes === 1, 'new explicit launch succeeds once in the current scope');
  });
  await check('power-advance-does-not-replay-an-issued-child-launch', async () => {
    f.hold.add('launch'); const old = f.bridge.launch(); await f.flush(); const issued = f.outstanding('launch')[0];
    assert(!!issued && f.snapshot().launches === 1, 'one child launch is already dispatched before power changes');
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 }); await f.flush();
    f.reply(issued); const result = await old; await f.flush();
    const current = f.snapshot();
    assert(result.ok && current.launches === 1 && current.disables === 0 && current.minimizes === 0 && current.pendingStatus === 1,
      'issued child receipt establishes observation without replay or stale parent minimize');
  });
  await check('power-advance-lost-launch-reply-does-not-lock-renderer', async () => {
    f.hold.add('launch');
    let done = false;
    const first = f.bridge.launch().then(() => { done = true; }, () => { done = true; });
    await f.flush();
    assert(f.snapshot().launches === 1, 'one launch has been posted');
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 });
    await f.advance(10000);
    const bounded = done;
    const sends = f.snapshot();
    // Let the old fixture tear down even when the production queue is locked.
    const outstanding = f.outstanding('launch')[0];
    if (outstanding) f.reply(outstanding);
    f.hold.delete('launch'); await first; await f.flush();
    assert(bounded && sends.launches === 1 && sends.minimizes === 0,
      'loss of a launch acknowledgement cannot lock the renderer or replay the launch');
  });
  await check('power-advance-lost-disable-reply-does-not-lock-renderer', async () => {
    await f.bridge.launch(); await f.flush(); f.hold.add('disable');
    let done = false;
    const stopping = f.bridge.stop().then(() => { done = true; }, () => { done = true; });
    await f.flush();
    assert(f.snapshot().disables === 1, 'one stop has been posted');
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 });
    await f.advance(10000);
    const bounded = done;
    const outstanding = f.outstanding('disable')[0];
    if (outstanding) f.reply(outstanding);
    f.hold.delete('disable'); await stopping; await f.flush();
    assert(bounded, 'loss of a stop acknowledgement cannot keep the session queue locked');
  });
  await check('read-back-executed-launch-after-wake', async () => {
    f.hold.add('launch');
    const first = f.bridge.launch(); await f.flush();
    const launch = f.outstanding('launch')[0];
    f.reply(launch, { ok: true, pid: 77 }, undefined, false);
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 });
    await f.advance(4000);
    const result = await first;
    assert(result.ok && result.pid === 77 && f.snapshot().launches === 1 && f.snapshot().minimizes === 0 && f.pendingStatus() === 1,
      'retained completion rejoins the original child in the new scope without replay or focus handoff');
  });
  await check('late-completion-during-first-receipt-read', async () => {
    f.hold.add('launch'); f.hold.add('customSteamLibrary.settleMutation');
    const first = f.bridge.launch(); await f.flush();
    const launch = f.outstanding('launch')[0]; const mutationTicket = launch.args.mutationTicket;
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 });
    await f.advance(4000);
    const read = f.outstanding('customSteamLibrary.settleMutation')[0];
    assert(!!read, 'first bounded receipt read is in flight');
    f.reply(launch, { ok: true, pid: 77 }, undefined, false);
    f.completion(mutationTicket); await f.flush();
    f.hold.delete('customSteamLibrary.settleMutation');
    f.reply(read, { ticket: mutationTicket, state: 'running' });
    await first; await f.flush();
    assert(f.requests('customSteamLibrary.settleMutation').length === 2 && f.snapshot().launches === 1 &&
      f.snapshot().minimizes === 0 && f.pendingStatus() === 1, 'completion owns one follow-up after the stale running response');
  });
  await check('read-back-retires-queued-launch-before-reopen', async () => {
    f.queued.add('launch'); f.hold.add('launch');
    const first = f.bridge.launch(); await f.flush(); const old = f.outstanding('launch')[0];
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 }); await f.advance(4000);
    assert(!(await first).ok && f.snapshot().minimizes === 0 && f.snapshot().disables === 1,
      'a queued ticket is retired and the already enabled session is cleaned up');
    f.queued.delete('launch'); f.hold.delete('launch');
    const second = await f.bridge.launch(); f.reply(old); await f.flush();
    assert(second.ok && f.snapshot().launches === 2 && f.snapshot().minimizes === 1 && f.pendingStatus() === 1,
      'fresh launch after queued retirement survives the late old reply');
  });
  await check('stop-wins-over-late-recovered-launch', async () => {
    f.hold.add('launch');
    const first = f.bridge.launch(); await f.flush(); const launch = f.outstanding('launch')[0];
    f.power({ suspendSerial: 2, observedNotificationSequence: 4 }); await f.advance(4000);
    assert(!(await first).ok && f.snapshot().launches === 1, 'running launch leaves a bounded unconfirmed result');
    await f.bridge.stop(); await f.flush();
    assert(f.snapshot().disables === 0, 'stop does not race unknown native execution');
    f.reply(launch, { ok: true, pid: 77 }, undefined, false); f.completion(launch.args.mutationTicket); await f.flush();
    assert(f.snapshot().disables === 1 && f.pendingStatus() === 0 && f.snapshot().minimizes === 0,
      'confirmed completion resumes stop only, without reattaching the child');
  });
  for (const failure of ['enable', 'launch', 'launch-false']) {
    await check('launch-fault-' + failure, async () => {
      if (failure === 'launch-false') f.launchOk(false); else f.faults.add(failure);
      const result = await f.bridge.launch().then(v => v, () => ({ ok: false })); await f.flush();
      assert(!result.ok && f.snapshot().timers === 0 && f.pending() === 0, failure + ' cannot start a watcher or retain local pending');
      assert(f.snapshot().disables === 1 && !f.snapshot().nativeEnabled, failure + ' retires the current native session intent');
      f.faults.clear(); f.launchOk(true); const recovered = await f.bridge.launch();
      assert(recovered.ok, failure + ' rejection does not poison later explicitly requested launch');
    });
  }
  await f.reset();
  assert(f.pending() === 0 && f.snapshot().timers === 0, 'combined actual IPC fixture finishes with no pending or resident watch');
}

async function rejects(task: () => Promise<any>, message: string): Promise<void> {
  let failed = false;
  try { await task(); } catch { failed = true; }
  assert(failed, message);
}

// AST extraction executes exact helpers/constants and does not duplicate the
// eligibility or artwork policy in a second test-only implementation.
const uiAst = ts.createSourceFile('app.js', customLibraryUi, ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
function uiFixture(names: string[], constants: string[] = [], scope: Record<string, any> = {}) {
  const functions = names.map(name => {
    const node = uiAst.statements.find((n: any) => ts.isFunctionDeclaration(n) && n.name?.text === name);
    if (!node) throw new Error(`Missing production UI function: ${name}`);
    return node.getText(uiAst);
  });
  const variables = constants.map(name => {
    const node = uiAst.statements.find((n: any) => ts.isVariableStatement(n) &&
      n.declarationList.declarations.some((d: any) => d.name?.text === name));
    if (!node) throw new Error(`Missing production UI constant: ${name}`);
    return node.getText(uiAst);
  });
  const context = vm.createContext({ snapshot: { config: {}, dataRoot: windowsPath('G:', 'Fixture', 'data') }, ...scope });
  vm.runInContext([...variables, ...functions].join('\n'), context, { timeout: 1000 });
  return context;
}

async function checkCurrentContracts(): Promise<void> {
  const canonical = windowsPath('C:', 'SOFT', 'YeMan', 'YeManCC', 'CustomSteamLibrary', 'CustomSteamLibrary.exe');
  const legacy = windowsPath('C:', 'SOFT', 'YeMan', 'CustomSteamLibrary', 'CustomSteamLibrary.exe');
  const exeDir = windowsPath('G:', 'Fixture', '便携 YMCC');
  const nested = windowsPath(exeDir, 'CustomSteamLibrary', 'CustomSteamLibrary.exe');
  const preferred = bridgeFixture(exeDir, [nested, canonical, legacy]);
  assert(await preferred.resolve() === nested, 'resolver prefers nested child when all executable locations exist');
  assert(JSON.stringify(preferred.existsCalls) === JSON.stringify([nested]), 'resolver stops probing after nested child success');
  const fallback = bridgeFixture(exeDir, [canonical, legacy]);
  assert(await fallback.resolve() === canonical, 'resolver falls back to canonical nested installation');
  assert(JSON.stringify(fallback.existsCalls) === JSON.stringify([nested, canonical]), 'resolver candidate order is nested then canonical, never old sibling');
  const oldOnly = bridgeFixture(exeDir, [legacy]);
  await rejects(oldOnly.resolve, 'old sibling alone must not be launched');
  assert(JSON.stringify(oldOnly.existsCalls) === JSON.stringify([nested, canonical]), 'resolver must not probe the removed legacy sibling');
  await rejects(bridgeFixture(exeDir).resolve, 'missing child fails closed');
  assert(await bridgeFixture(exeDir, [canonical], {}, [nested]).resolve() === canonical, 'inaccessible nested candidate permits canonical fallback');
  for (const suffix of [separator, '/', separator + '/']) {
    assert(await bridgeFixture(exeDir + suffix, [nested]).resolve() === nested, `resolver trims trailing separators ${JSON.stringify(suffix)}`);
  }
  const uncRoot = windowsPath(separator, 'server', 'share', 'YeManCC');
  const uncChild = windowsPath(uncRoot, 'CustomSteamLibrary', 'CustomSteamLibrary.exe');
  assert(await bridgeFixture(uncRoot, [uncChild]).resolve() === uncChild, 'resolver preserves UNC roots without live UNC IO');

  const durablePlan = windowsPath('D:', 'YeMan', 'CustomSteamLibrary', 'data', 'state', 'steam-add-plan.json');
  const portablePlan = windowsPath(exeDir, 'CustomSteamLibrary', 'data', 'state', 'steam-add-plan.json');
  const canonicalPlan = windowsPath('C:', 'SOFT', 'YeMan', 'YeManCC', 'CustomSteamLibrary', 'data', 'state', 'steam-add-plan.json');
  const oldPlan = windowsPath('D:', 'YeMan', 'Steam大屏', 'state', 'steam-add-plan.json');
  const oldSiblingPlan = windowsPath('C:', 'SOFT', 'YeMan', 'CustomSteamLibrary', 'data', 'state', 'steam-add-plan.json');
  const durableRoot = windowsPath('D:', 'YeMan', 'CustomSteamLibrary', 'data');
  const portableRoot = windowsPath(exeDir, 'CustomSteamLibrary', 'data');
  const canonicalRoot = windowsPath('C:', 'SOFT', 'YeMan', 'YeManCC', 'CustomSteamLibrary', 'data');
  const plan = (root: string, waiting: number, items: any[] = []) => JSON.stringify({
    schemaVersion: 1,
    dataRoot: root,
    libraryState: windowsPath(root, 'state', 'library-scan.json'),
    summary: { scannedGames: waiting, readyToAdd: waiting },
    items,
  });
  const durable = bridgeFixture(exeDir, [], { [durablePlan]: plan(durableRoot, 2), [portablePlan]: plan(portableRoot, 4), [oldPlan]: plan(oldPlan, 99) });
  assert((await durable.summary()).waiting === 2, 'summary prefers durable data over portable and legacy caches');
  assert(JSON.stringify(durable.readCalls) === JSON.stringify([durablePlan]), 'summary stops after the first readable durable plan');
  const portableSummary = bridgeFixture(exeDir, [], { [durablePlan]: '{broken', [portablePlan]: plan(portableRoot, 4) });
  assert((await portableSummary.summary()).waiting === 4, 'corrupt durable plan falls back to portable child data');
  const malformedFirst = bridgeFixture(exeDir, [], { [durablePlan]: '{}', [portablePlan]: plan(portableRoot, 4) });
  assert((await malformedFirst.summary()).waiting === 4, 'shape-invalid durable plan does not shadow a valid portable plan');
  const emptyCounters = bridgeFixture(exeDir, [], {
    [durablePlan]: JSON.stringify({ dataRoot: durableRoot, libraryState: windowsPath(durableRoot, 'state', 'library-scan.json'), summary: {}, items: [] }),
    [portablePlan]: plan(portableRoot, 4),
  });
  assert((await emptyCounters.summary()).waiting === 4, 'empty plan counters are rejected instead of becoming a false zero summary');
  const malformedCounters = bridgeFixture(exeDir, [], {
    [durablePlan]: JSON.stringify({ dataRoot: durableRoot, libraryState: windowsPath(durableRoot, 'state', 'library-scan.json'), summary: { scannedGames: '4', readyToAdd: '4' }, items: [] }),
    [portablePlan]: plan(portableRoot, 4),
  });
  assert((await malformedCounters.summary()).waiting === 4, 'string plan counters are rejected instead of being coerced to a false summary');
  const inconsistentCounters = bridgeFixture(exeDir, [], {
    [durablePlan]: JSON.stringify({ dataRoot: durableRoot, libraryState: windowsPath(durableRoot, 'state', 'library-scan.json'), summary: { scannedGames: 0, readyToAdd: 4 }, items: [] }),
    [portablePlan]: plan(portableRoot, 4),
  });
  assert((await inconsistentCounters.summary()).waiting === 4, 'contradictory summary counters are rejected instead of being clamped to zero');
  const foreignFirst = bridgeFixture(exeDir, [], { [durablePlan]: plan(portableRoot, 88), [portablePlan]: plan(portableRoot, 4) });
  assert((await foreignFirst.summary()).waiting === 4, 'plan from another data root is rejected before fallback');
  const canonicalSummary = bridgeFixture(exeDir, [], { [canonicalPlan]: plan(canonicalRoot, 6) });
  assert((await canonicalSummary.summary()).waiting === 6, 'missing portable plan falls back to canonical nested data');
  const postCommit = bridgeFixture(exeDir, [], { [portablePlan]: plan(portableRoot, 4, [
    { gameDirectory: windowsPath('G:', 'Games', 'One'), status: 'ready-not-selected' },
    { gameDirectory: windowsPath('G:', 'Games', 'Two'), status: 'added-to-steam' },
    { gameDirectory: windowsPath('G:', 'Games', 'Three'), status: 'non-game' },
    { gameDirectory: windowsPath('G:', 'Games', 'Four'), status: 'needs-identity-and-artwork' },
  ]) });
  assert(JSON.stringify(await postCommit.summary()) === JSON.stringify({ waiting: 1, joined: 1, needs: 1, excluded: 1 }), 'summary derives waiting/joined from item status instead of stale counters');
  const malformedItemStatus = bridgeFixture(exeDir, [], { [portablePlan]: plan(portableRoot, 4, [{ gameDirectory: windowsPath('G:', 'Games', 'Broken'), status: 42 }]), [canonicalPlan]: plan(canonicalRoot, 6) });
  assert((await malformedItemStatus.summary()).waiting === 6, 'malformed item status falls through to the next valid data root');
  const legacySummary = bridgeFixture(exeDir, [], { [oldPlan]: plan(oldPlan, 99), [oldSiblingPlan]: plan(oldSiblingPlan, 99) });
  assert(await legacySummary.summary() === null, 'removed legacy caches cannot populate the current summary');
  assert(!legacySummary.readCalls.includes(oldPlan) && !legacySummary.readCalls.includes(oldSiblingPlan), 'summary never probes old sibling or Steam大屏 data');
  const scanPath = portablePlan.replace('steam-add-plan.json', 'library-scan.json');
  const scan = bridgeFixture(exeDir, [], { [scanPath]: JSON.stringify({
    games: [{ gameDirectory: windowsPath('G:', 'Games', 'One') }, { gameDirectory: windowsPath('G:', 'Games', 'Two') }],
    summary: { games: 2, ready: 1, nonGames: 1 },
  }) });
  assert(JSON.stringify(await scan.summary()) === JSON.stringify({ waiting: 1, joined: 0, needs: 1, excluded: 1 }), 'summary uses nested scan cache without subtracting skipped non-games twice');
  const legacyScan = bridgeFixture(exeDir, [], { [scanPath]: JSON.stringify({
    games: [{ gameDirectory: windowsPath('G:', 'Games', 'Ready'), status: 'ready' }, { gameDirectory: windowsPath('G:', 'Games', 'Review'), status: 'needs-review' }],
    summary: {},
  }) });
  assert((await legacyScan.summary()).waiting === 1, 'legacy scan cache derives waiting count from row status instead of returning false zero');

  const waiting = { gameDirectory: windowsPath('G:', 'Games', 'NoId'), primaryExecutable: windowsPath('G:', 'Games', 'NoId', 'game.exe'), status: 'ready', steam: { status: 'waiting-steam-verification' } };
  const planMerge = uiFixture(['normalizedUiPath', 'keyFor', 'planMap', 'manualOverride', 'resolvedIdentity', 'isSteamReady', 'mergedGames'], ['steamReadyStatuses'], {
    snapshot: {
      config: {}, dataRoot: windowsPath('G:', 'Fixture', 'data'), manualOverrides: { items: {} }, resolvedIdentities: {},
      library: { games: [{ gameDirectory: windowsPath('G:', 'Games', 'PathCase'), primaryExecutable: windowsPath('G:', 'Games', 'PathCase', 'new.exe'), directoryName: 'PathCase' }] },
      steamPlan: { items: [{ gameDirectory: 'g:/games/pathcase/', primaryExecutable: 'g:/games/pathcase/old.exe', status: 'ready-to-add' }] },
    },
  });
  const mergedPathGame = planMerge.mergedGames()[0];
  assert(mergedPathGame.steam.status === 'ready-to-add', 'plan joins use normalized slash/case paths and retain directory fallback');
  const nativeGame = { ...waiting, gameDirectory: windowsPath('G:', 'Games', 'Native'), steamNative: true };
  const added = { ...waiting, gameDirectory: windowsPath('G:', 'Games', 'Added'), steam: { status: 'already-in-steam' } };
  const unknown = { ...waiting, gameDirectory: windowsPath('G:', 'Games', 'Unknown'), steam: {} };
  const excluded = { ...waiting, gameDirectory: windowsPath('G:', 'Games', 'Tool'), contentType: 'non-game', steam: {} };
  const games = [waiting, nativeGame, added, unknown, excluded];
  const selection = uiFixture([
    'normalizedUiPath', 'keyFor', 'validManualBucket', 'manualBucket', 'isNativeSteamGame', 'isNonGame', 'isSteamReady',
    'shouldPreserveWaitingCard', 'category', 'isSteamImportReady', 'defaultSelectedDirectories',
    'selectedCommitDirectories', 'selectedTargetArguments',
  ], ['steamReadyStatuses', 'WAITING_KEEP_STATUSES', 'MANUAL_BUCKETS'], {
    mergedGames: () => games, activeTab: 'not-in-steam', selectedGameDirectories: null, selectedCategoryDirectories: {},
  });
  assert(selection.isSteamImportReady(waiting) === true, 'waiting shortcut is importable without SteamID or artwork');
  assert(selection.isSteamReady(waiting) === false, 'waiting importability must not grant verified Steam identity');
  assert(selection.isSteamImportReady({ ...waiting, steam: { status: 'ready-to-add' } }) === true, 'ready shortcut does not require redundant AppID metadata');
  assert(selection.isSteamImportReady(nativeGame) === false, 'native Steam game cannot enter shortcut import queue');
  assert(selection.isSteamImportReady(added) === false, 'already-added game cannot enter shortcut import queue');
  assert(selection.isSteamImportReady(unknown) === false, 'unclassified game is not silently default-selected');
  assert(selection.isSteamImportReady(excluded) === false, 'non-game item is not silently default-selected');
  const waitingKeys = [windowsPath('g:', 'games', 'noid')];
  assert(JSON.stringify(selection.defaultSelectedDirectories()) === JSON.stringify(waitingKeys), 'default selection includes only the no-ID waiting game');
  assert(JSON.stringify(selection.selectedTargetArguments().selectedGameDirectories) === JSON.stringify(waitingKeys), 'actual commit arguments retain the selected no-ID shortcut');
  selection.snapshot.config.manualBuckets = { [windowsPath('g:', 'games', 'unknown')]: { bucket: 'not-in-steam' } };
  assert(selection.isSteamImportReady(unknown) === true, 'explicit manual waiting bucket is independent of Steam metadata');
  selection.snapshot.config.manualBuckets = { [windowsPath('g:', 'games', 'unknown')]: { bucket: 'typoed-bucket' } };
  assert(selection.category(unknown) === 'unclassified', 'unknown manual bucket values cannot make a card disappear from all tabs');

  const artwork = uiFixture(['normalizedUiPath', 'artworkOverride', 'artworkPreviewUrl', 'artworkEntry', 'artworkDraftFromGame', 'artworkSearchTypeForSlot']);
  const visualGame = { steam: { artworkPreview: {
    tall: { url: 'https://fixture.invalid/portrait.png' },
    long: { url: 'https://fixture.invalid/horizontal.png' },
    hero: { url: 'https://fixture.invalid/background.png' },
  } } };
  assert(artwork.artworkEntry(visualGame, 'cover').url.endsWith('/portrait.png'), 'portrait slot uses tall preview without Steam identity');
  assert(artwork.artworkEntry(visualGame, 'long').url.endsWith('/horizontal.png'), 'horizontal slot uses long preview instead of hero background');
  assert(artwork.artworkEntry(visualGame, 'wallpaper').url.endsWith('/background.png'), 'wallpaper slot uses hero preview instead of horizontal cover');
  const manualVisual = { ...visualGame, override: { long: { url: 'https://fixture.invalid/manual-horizontal.png' } } };
  assert(artwork.artworkEntry(manualVisual, 'long').priority === 'manual', 'manual horizontal cover wins its own automatic slot');
  assert(artwork.artworkEntry(manualVisual, 'wallpaper').url.endsWith('/background.png'), 'manual horizontal cover does not override automatic wallpaper');
  const draft = artwork.artworkDraftFromGame(manualVisual);
  assert(draft.long.state === 'set' && draft.long.asset.url.endsWith('/manual-horizontal.png'), 'editor retains the manual horizontal cover');
  assert(draft.wallpaper.state === 'inherit' && draft.wallpaper.asset.url.endsWith('/background.png'), 'editor independently inherits automatic wallpaper');
  const deleted = { ...visualGame, override: { artworkProtection: { long: 'deleted' } } };
  assert(artwork.artworkEntry(deleted, 'long') === null, 'explicit horizontal deletion blocks stale automatic preview');
  assert(artwork.artworkEntry(deleted, 'wallpaper') !== null, 'horizontal deletion does not delete wallpaper');
  for (const slot of ['cover', 'long', 'wallpaper']) {
    assert(artwork.artworkSearchTypeForSlot(slot) === slot, `artwork search preserves ${slot} independently`);
  }
}

const outputArg = args.find(arg => arg !== '--session-only');
void (sessionOnly ? Promise.resolve() : checkCurrentContracts()).then(checkSessionContracts).then(() => {
  const output = path.resolve(outputArg || path.join(root, '../../Build/Validation/SelfTests', sessionOnly
    ? 'custom_steam_library_session_summary.json' : 'custom_steam_library_integration_summary.json'));
  const relative = path.relative(path.resolve(root, '../../Build'), output);
  if (!relative || relative === '..' || relative.startsWith('..' + path.sep) || path.isAbsolute(relative)) {
    throw new Error('Integration report must stay inside Mainline Build');
  }
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, JSON.stringify({
    allPassed: cases.every(entry => entry.passed), caseCount: cases.length, failedCount: cases.filter(entry => !entry.passed).length, cases,
    sessionIpcCombination: { actualBridgeAndIpc: true, actualSharedPowerSource: true, sameVm: true, virtualClock: true, subcases: sessionResults },
    contract: sessionOnly ? 'session-notifications-and-audit' : 'nested-child-only; optional-shortcut-metadata; independent-artwork-slots',
    filesystemAndIpcMocked: true, liveNetworkUsed: false,
    realSteamFilesModified: false, steamStoppedOrLaunched: false,
  }, null, 2));
  if (cases.some(entry => !entry.passed)) throw new Error('Session semantic failures: ' + sessionResults.filter(entry => !entry.passed).map(entry => entry.name).join(', '));
  console.log(`CUSTOM_STEAM_LIBRARY_NATIVE_ARBITRATION_SELFTEST_OK (${cases.length} checks)`);
  console.log(`summary: ${output}`);
}).catch(error => {
  console.error(error);
  process.exitCode = 1;
});
