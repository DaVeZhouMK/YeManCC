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
const native = fs.readFileSync(path.join(root, 'native', 'main.cpp'), 'utf8');
const bridge = fs.readFileSync(path.join(root, 'src', 'bridge', 'customSteamLibrary.ts'), 'utf8');
const engine = fs.readFileSync(path.join(root, 'src', 'gamepad', 'engine.ts'), 'utf8');
const customLibraryUi = fs.readFileSync(path.join(root, 'CustomSteamLibrary', 'workspace-ui', 'app.js'), 'utf8');
const customLibraryHtml = fs.readFileSync(path.join(root, 'CustomSteamLibrary', 'workspace-ui', 'index.html'), 'utf8');
const customLibraryCss = fs.readFileSync(path.join(root, 'CustomSteamLibrary', 'workspace-ui', 'styles.css'), 'utf8');
const customLibraryWorker = fs.readFileSync(path.join(root, 'native', 'custom-steam-library', 'steam_artwork_lab.cpp'), 'utf8');

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
// desktop mapping quiet through the same native admission boundary as YMCC.
for (const gate of ['steamDeckUiOwnsInput', 'ps5UiOwnsInput']) {
  const body = native.match(new RegExp('static bool ' + gate + '\\([^)]*\\)\\s*\\{([\\s\\S]*?)\\n\\}'))?.[1] || '';
  assert(body.includes('return gamepadUiInputEligible();'), gate + ' must reuse main native UI admission');
  assert(!body.includes('CustomSteamLibraryInputPhase'), gate + ' must not bypass child foreground ownership');
}
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


// Execute production helpers with mocked filesystem/IPC. No fixture can start
// Steam, launch a child, start a session timer or read real user data.
// Resolve installed dependencies at runtime: bundling esbuild itself breaks
// its native executable lookup in a Build-directory bundle.
const runtimeRequire = createRequire(path.join(root, 'package.json'));
const { transformSync } = runtimeRequire('esbuild');
const ts = runtimeRequire('typescript');
const bridgeCode = transformSync(bridge, { loader: 'ts', format: 'cjs', target: 'es2020' }).code;
const separator = String.fromCharCode(92);
const windowsPath = (...parts: string[]) => parts.join(separator);

function bridgeFixture(exeDir: string, existing: string[] = [], files: Record<string, string> = {}, denied: string[] = []) {
  const existsCalls: string[] = [], readCalls: string[] = [];
  const unexpected = () => { throw new Error('Unexpected process/window/IPC operation in read-only fixture'); };
  const api = {
    app: { exeDir: async () => exeDir, pid: unexpected },
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
    shell: { hidden: unexpected }, windowApi: { minimize: unexpected, show: unexpected },
  };
  const module = { exports: {} };
  const context = vm.createContext({
    module, exports: module.exports,
    require: (name: string) => {
      if (name === '@/bridge/api') return api;
      if (name === '@/bridge/ipc') return { invoke: unexpected };
      throw new Error(`Unexpected dependency: ${name}`);
    },
    setTimeout: unexpected, setInterval: unexpected, clearTimeout: unexpected, clearInterval: unexpected,
  });
  vm.runInContext(bridgeCode, context, { timeout: 1000 });
  return { existsCalls, readCalls,
    resolve: () => vm.runInContext('resolveExecutable()', context, { timeout: 1000 }) as Promise<string>,
    summary: () => vm.runInContext('readCustomSteamLibrarySummary()', context, { timeout: 1000 }) as Promise<any>,
  };
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

void checkCurrentContracts().then(() => {
  const output = path.resolve(process.argv[2] || path.join(root, '../../Build/Validation/SelfTests/custom_steam_library_integration_summary.json'));
  const relative = path.relative(path.resolve(root, '../../Build'), output);
  if (!relative || relative === '..' || relative.startsWith('..' + path.sep) || path.isAbsolute(relative)) {
    throw new Error('Integration report must stay inside Mainline Build');
  }
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, JSON.stringify({
    allPassed: true, caseCount: cases.length, failedCount: 0, cases,
    contract: 'nested-child-only; optional-shortcut-metadata; independent-artwork-slots',
    filesystemAndIpcMocked: true, liveNetworkUsed: false,
    realSteamFilesModified: false, steamStoppedOrLaunched: false,
  }, null, 2));
  console.log(`CUSTOM_STEAM_LIBRARY_NATIVE_ARBITRATION_SELFTEST_OK (${cases.length} checks)`);
  console.log(`summary: ${output}`);
}).catch(error => {
  console.error(error);
  process.exitCode = 1;
});
