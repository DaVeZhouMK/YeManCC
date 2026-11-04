import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = fs.readFileSync(path.join(root, 'CustomSteamLibrary/workspace-ui/app.js'), 'utf8');
const html = fs.readFileSync(path.join(root, 'CustomSteamLibrary/workspace-ui/index.html'), 'utf8');
const begin = source.indexOf('function isWorkspaceRecord(');
const end = source.indexOf('function invoke(', begin);
assert(begin >= 0 && end > begin, 'Missing production snapshot guard');
const previous = { config: { roots: ['G:\\Games'] }, library: { games: [{ gameDirectory: 'G:\\Games\\Good', primaryExecutable: 'G:\\Games\\Good\\Game.exe', status: 'ready' }] }, steamPlan: { items: [{ gameDirectory: 'G:\\Games\\Good', status: 'already-in-steam' }], summary: {} } };
const context = vm.createContext({ snapshot: structuredClone(previous) });
vm.runInContext(source.slice(begin, end), context);
const normalize = (value, prior = previous) => context.normalizeWorkspaceSnapshot(value, prior);
const reports = [];
function run(name, action) {
  try { action(); reports.push({ name, passed: true }); }
  catch (error) { reports.push({ name, passed: false, error: error.message }); }
}
for (const [index, value] of [null, undefined, false, 7, 'text', []].entries()) {
  run(`reject-non-object-snapshot-${index}`, () => assert.throws(() => normalize(value)));
}
run('null-library-preserves-current-games', () => assert.equal(normalize({ library: null }).library.games[0].gameDirectory, previous.library.games[0].gameDirectory));
run('malformed-games-array-preserves-current-games', () => assert.equal(normalize({ library: { games: 'broken' } }).library.games.length, 1));
run('all-malformed-rows-are-not-authoritative-empty', () => assert.equal(normalize({ library: { games: [null, 7, 'broken'] } }).library.games.length, 1));
run('intentional-empty-library-remains-empty', () => assert.equal(normalize({ library: { games: [] } }).library.games.length, 0));
run('mixed-valid-and-malformed-games-preserve-previous-complete-set', () => { const fresh = { gameDirectory: 'G:\\Games\\New', primaryExecutable: 'G:\\Games\\New\\Game.exe' }; const next = normalize({ library: { games: [null, fresh] } }); assert.equal(next.library.games.length, 1); assert.equal(next.library.games[0].gameDirectory, previous.library.games[0].gameDirectory); });
run('non-string-primary-is-not-rendered-or-imported', () => { const next = normalize({ library: { games: [{ gameDirectory: 'G:\\Games\\Bad', primaryExecutable: 7, status: 'ready' }] } }); assert.equal(next.library.games[0].primaryExecutable, undefined); assert.equal(next.library.games[0].status, 'data-invalid-review-required'); });
run('invalid-plan-fields-cannot-be-import-ready', () => { const next = normalize({ steamPlan: { items: [{ gameDirectory: 'G:\\Games\\Bad', primaryExecutable: {}, status: 'ready-to-add' }] } }); assert.equal(next.steamPlan.items[0].status, 'data-invalid-review-required'); });
run('invalid-root-field-keeps-user-settings', () => assert.equal(normalize({ config: { roots: null } }).config.roots[0], previous.config.roots[0]));
run('invalid-overrides-and-accounts-are-safe', () => { const next = normalize({ manualOverrides: { items: null }, steamAccounts: { accounts: [null, 7, { accountId: '19627', steamRoot: 'G:\\Steam' }] } }); assert.equal(Object.keys(next.manualOverrides.items).length, 0); assert.equal(next.steamAccounts.accounts.length, 1); });
run('different-steam-roots-same-account-are-retained', () => { const next = normalize({ steamAccounts: { accounts: [{ accountId: '19627', steamRoot: 'G:\\Steam' }, { accountId: '19627', steamRoot: 'H:\\Steam' }] } }); assert.equal(next.steamAccounts.accounts.length, 2); });
run('snapshot-normalization-does-not-mutate-prior-or-input', () => { const input = { library: { games: [{ gameDirectory: 'G:\\Games\\Bad', primaryExecutable: 7 }] } }; const before = JSON.stringify(input), prior = JSON.stringify(previous); normalize(input); assert.equal(JSON.stringify(input), before); assert.equal(JSON.stringify(previous), prior); });
run('raw-download-result-is-not-treated-as-snapshot', () => { const result = { path: 'G:\\Images\\Cover.png' }; assert.equal(context.workspaceBridgeResult('downloadArtwork', result), result); });
run('state-response-without-snapshot-is-rejected', () => assert.throws(() => context.workspaceBridgeResult('state', { path: 'file.png' })));
run('steam-write-error-refreshes-durable-state', () => assert(source.includes("['commit', 'deleteFromSteam', 'refreshSteamArtwork'].includes(command)") && source.includes("applyLibrarySnapshot(await invoke('state'))")));
run('steam-open-does-not-relabel-join-button', () => { assert(source.includes("const label = '点击加入';")); assert(!source.includes("const label = running === true ? '点击关闭Steam'")); assert(html.includes('点击加入 <span class="count-label">[已选0个]</span>')); });
run('malformed-bridge-message-does-not-access-array-shape', () => assert(source.includes('isWorkspaceRecord(event.data) ? event.data : {}')));
run('deterministic-snapshot-field-fuzz-1000-combinations', () => {
  const values = [null, 7, false, 'bad', [], {}, [null], previous.library.games];
  for (let i = 0; i < 1000; i++) {
    const next = normalize({ library: { games: values[i % values.length] }, steamPlan: { items: values[(i * 3) % values.length], summary: values[(i * 7) % values.length] }, config: { roots: values[(i * 5) % values.length] }, steamAccounts: { accounts: values[(i * 11) % values.length] }, manualOverrides: { items: values[(i * 13) % values.length] } });
    assert(Array.isArray(next.library.games)); assert(Array.isArray(next.steamPlan.items)); assert(Array.isArray(next.steamAccounts.accounts)); assert(context.isWorkspaceRecord(next.manualOverrides.items));
  }
});
// Evaluate the exact production artwork helpers, not a reimplementation.
const artworkContext = vm.createContext({ snapshot: { dataRoot: 'G:\\Fixture\\data' } });
for (const name of ['normalizedUiPath', 'escapeHtml', 'initials', 'artworkMarkup']) {
  const line = source.split(/\r?\n/).find(line => line.startsWith('function ' + name + '('));
  assert(line, 'Missing production helper ' + name); vm.runInContext(line, artworkContext);
}
const artworkBegin = source.indexOf('function artworkOverride(');
const artworkEnd = source.indexOf('function artworkDropMarkup(', artworkBegin);
vm.runInContext(source.slice(artworkBegin, artworkEnd), artworkContext);
const autoGame = () => ({ steam: { status: 'waiting-steam-verification', artworkPreview: {
  tall: { url: 'https://fixture.invalid/auto-cover.png', provider: 'playnite-igdb' },
  long: { url: 'https://fixture.invalid/auto-long.png', provider: 'baidu-image' },
  hero: { url: 'https://fixture.invalid/auto-hero.png', provider: 'playnite-igdb' }
} } });
run('no-steamid-igdb-and-baidu-artwork-is-visible', () => { const game = autoGame(); for (const slot of ['cover', 'long', 'wallpaper']) assert.equal(artworkContext.artworkEntry(game, slot).priority, 'automatic'); assert.match(artworkContext.artworkMarkup(game, 'Fixture'), /<img/); });
run('manual-file-cover-wins-grid-and-editor', () => { const game = autoGame(); game.override = { cover: { portableFile: 'G:\\Fixture\\data\\manual.png', file: 'G:\\OldJob\\gone.png' } }; assert.equal(artworkContext.artworkEntry(game, 'cover').priority, 'manual'); assert.match(artworkContext.artworkMarkup(game, 'Fixture'), /manual.png/); });
run('manual-cover-does-not-hide-automatic-background', () => { const game = autoGame(); game.override = { cover: { url: 'https://fixture.invalid/manual.png' } }; assert.equal(artworkContext.artworkEntry(game, 'wallpaper').priority, 'automatic'); assert.equal(artworkContext.artworkEntry(game, 'long').priority, 'automatic'); });
run('manual-wallpaper-does-not-hide-automatic-cover', () => { const game = autoGame(); game.override = { wallpaper: { url: 'https://fixture.invalid/manual.png' } }; assert.equal(artworkContext.artworkEntry(game, 'cover').priority, 'automatic'); assert.equal(artworkContext.artworkEntry(game, 'wallpaper').priority, 'manual'); });
run('manual-deletion-wins-over-stale-manual-and-auto-assets', () => { const game = autoGame(); game.override = { cover: { url: 'https://fixture.invalid/manual.png' }, artworkProtection: { cover: 'deleted' } }; assert.equal(artworkContext.artworkEntry(game, 'cover'), null); assert.doesNotMatch(artworkContext.artworkMarkup(game, 'Fixture'), /<img/); });
run('missing-manual-file-falls-back-to-automatic-preview', () => { const game = autoGame(); game.override = { cover: { available: false, portableFile: 'G:\\Fixture\\data\\missing.png' }, artworkProtection: { cover: 'manual' } }; assert.equal(artworkContext.artworkEntry(game, 'cover').priority, 'automatic'); });
run('automatic-preview-wins-over-initials', () => assert.match(artworkContext.artworkMarkup(autoGame(), 'Fixture'), /auto-cover.png/));
run('no-image-uses-initials-without-throwing', () => { assert.equal(artworkContext.artworkEntry({}, 'cover'), null); assert.equal(artworkContext.artworkMarkup({}, 'Fixture Game'), 'FG'); });
run('portable-local-copy-wins-over-dead-download-url', () => assert.match(artworkContext.artworkPreviewUrl({ portableFile: 'G:\\Fixture\\data\\saved.png', url: 'https://fixture.invalid/dead.png' }), /steam-library-data.localhost\/saved.png/));
run('legacy-manual-artwork-uses-the-same-priority', () => { const game = autoGame(); game.override = { artwork: { cover: { url: 'https://fixture.invalid/legacy.png' } } }; assert.match(artworkContext.artworkMarkup(game, 'Fixture'), /legacy.png/); });
run('artwork-source-priority-1000-slot-combinations', () => {
  for (let i = 0; i < 1000; i++) {
    const game = autoGame(); game.override = { artworkProtection: {} };
    for (const [j, slot] of ['cover', 'long', 'wallpaper'].entries()) {
      const mode = (i + j) % 4;
      if (mode === 0) game.override[slot] = { url: 'https://fixture.invalid/manual.png' };
      if (mode === 1) game.override[slot] = { available: false, url: 'https://fixture.invalid/missing.png' };
      if (mode === 2) game.override.artworkProtection[slot] = 'deleted';
      const item = artworkContext.artworkEntry(game, slot);
      assert.equal(item?.priority || 'none', mode === 0 ? 'manual' : mode === 2 ? 'none' : 'automatic');
    }
  }
});

run('duplicate-exe-old-snapshot-is-one-card', () => { const a={gameDirectory:'G:/Game/Cyberpunk 2077',primaryExecutable:'G:/Game/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe',status:'ready'};const b={...a,gameDirectory:a.gameDirectory+'/bin/x64',primaryExecutable:a.primaryExecutable.toLowerCase()};for(const games of [[a,b],[b,a]]){const next=normalize({library:{games},steamPlan:{items:games.map(g=>({...g,status:'already-in-steam'}))}});assert.equal(next.library.games.length,1);assert.equal(next.steamPlan.items.length,1);assert.equal(next.library.games[0].gameDirectory,a.gameDirectory);}});
run('duplicate-exe-keeps-owned-plan-not-pending-plan', () => {const a={gameDirectory:'G:/Games/A',primaryExecutable:'G:/Games/A/Game.exe',status:'waiting-steam-verification'};const b={...a,status:'already-in-steam',artworkPreview:{tall:{url:'https://fixture.invalid/steam.png'}}};const next=normalize({steamPlan:{items:[a,b]}});assert.equal(next.steamPlan.items.length,1);assert.equal(next.steamPlan.items[0].status,'already-in-steam');assert.equal(next.steamPlan.items[0].artworkPreview.tall.url,b.artworkPreview.tall.url);});
run('same-filename-distinct-exes-remain-separate-cards', () => {const games=['A','B'].map(n=>({gameDirectory:'G:/Games/'+n,primaryExecutable:'G:/Games/'+n+'/Game.exe',status:'ready'}));assert.equal(normalize({library:{games}}).library.games.length,2);});


run('builtin-steam-tool-all-snapshot-boundaries', () => {
  const good = {gameDirectory:'G:/Games/Portal 2',primaryExecutable:'G:/Games/Portal 2/portal2.exe',status:'already-in-steam'};
  const rows = [good, ...[
    {nativeSteamAppId:228980},{nativeSteam:{appId:'228980'}},{formalName:'Steamworks Common Redistributables'},
    {gameDirectory:'G:/Steam/steamapps/common/Steamworks Shared',primaryExecutable:''},
    {primaryExecutable:'G:/Steam/steamapps/common/Steamworks Shared/vcredist.exe'},
    {match:{steamAppId:228980}}
  ].map((fields,i)=>({...good,gameDirectory:'G:/Fixture/Tool'+i,...fields}))];
  const next = normalize({library:{games:rows,unscannedGames:rows,missingGames:rows},steamPlan:{items:rows}});
  for (const section of ['games','unscannedGames','missingGames']) assert.equal(next.library[section].length,1);
  assert.equal(next.steamPlan.items.length,1);
});
run('builtin-steam-tool-non-authoritative-fallback-still-filters', () => {
  const row={gameDirectory:'G:/Tool',primaryExecutable:'G:/Tool/Tool.exe',steamAppId:228980};
  const next=normalize({library:null,steamPlan:null},{library:{games:[row]},steamPlan:{items:[row]}});
  assert.equal(next.library.games.length,0);assert.equal(next.steamPlan.items.length,0);
});
run('builtin-steam-tool-strict-id-and-exact-name', () => {
  for(const id of [228980,'228980',' 00228980 ']) assert.equal(context.isBuiltinExcludedSteamTool({appId:id}),true);
  for(const id of [null,-228980,228980.5,'228980bad','2289800','+228980','228980.0']) assert.equal(context.isBuiltinExcludedSteamTool({appId:id}),false);
  for(const name of ['Steamworks Common Redistributables','Unreal Engine','UE_5.4','Unity Hub','Steam Linux Runtime 3.0']) assert.equal(context.isBuiltinExcludedSteamTool({formalName:name}),true);
  for(const name of ['Steamworks Simulator','My Steamworks Shared Adventure','Steamworks Shared 2','Unreal Tournament','UnrealEngineGame','UE Game','游戏 Steamworks Shared']) assert.equal(context.isBuiltinExcludedSteamTool({formalName:name}),false);
  assert.equal(context.isBuiltinExcludedSteamTool({igdbId:228980,artwork:{appId:228980}}),false);
});
// Reuse the actual identity and merge functions. No media identity gate is mocked.
const mergeContext=vm.createContext({snapshot:{},activeTab:'not-in-steam',selectedGameDirectories:null,selectedSteamDirectories:null,selectedCategoryDirectories:{unclassified:new Set(),'non-game':new Set()}});
const mergeBegin=source.indexOf('function defaultSelectedDirectories('),mergeEnd=source.indexOf('const WAITING_KEEP_STATUSES',mergeBegin);
assert(mergeBegin>=0&&mergeEnd>mergeBegin,'Missing production library merge boundaries');
vm.runInContext(source.slice(mergeBegin,mergeEnd),mergeContext);
for(const name of ['formalSteamAppId','confirmedMediaAppId','pageSelectable','pageSelectionKeys','isPageSelected']){
 const line=source.split(/\r?\n/).find(value=>value.startsWith('function '+name+'('));assert(line,'Missing production '+name);
 if(name==='pageSelectionKeys'){const begin=source.indexOf('function pageSelectionKeys('),end=source.indexOf('\nfunction defaultPageKeys(',begin);assert(end>begin);vm.runInContext(source.slice(begin,end),mergeContext);}else vm.runInContext(line,mergeContext);
}
const mergeMetrics=[];
function mergeFixture(n){const games=[],plans=[],entries={};for(let i=0;i<n;i++){const primaryExecutable='G:/Fixture/Game'+String(i).padStart(4,'0')+'/game.exe',gameDirectory='G:/Fixture/Game'+String(i).padStart(4,'0');games.push({primaryExecutable,gameDirectory,directoryName:'Game '+String(i).padStart(4,'0')});plans.push({primaryExecutable,gameDirectory,status:'ready-to-add',appId:1000+i});entries['steam-'+(1000+i)]={key:'steam-'+(1000+i),appId:1000+i,executable:primaryExecutable,staticReady:true,dynamicReady:true};}return {config:{},manualOverrides:{items:{}},resolvedIdentities:{},library:{games},steamPlan:{items:plans},gameMedia:{entries}};}
run('cached-wallpaper-merge-has-linear-path-work',()=>{
 mergeContext.snapshot=mergeFixture(400);vm.runInContext('var pathWork=0;var actualPath=normalizedUiPath;normalizedUiPath=(value)=>{++pathWork;return actualPath(value);};',mergeContext);
 const started=performance.now(),games=mergeContext.mergedGames();mergeMetrics.push({games:400,mediaEntries:400,pathNormalizations:mergeContext.pathWork,elapsedMs:performance.now()-started});
 assert.equal(games.length,400);assert(games.every(game=>game.gameMedia?.appId===game.steam.appId),'Cached media ownership changed');
 assert(mergeContext.pathWork<=400*20,'Merging cached wallpapers still scans the game list for every entry');
});
run('media-merge-keeps-original-first-match-precedence',()=>{
 const state=mergeFixture(3);state.steamPlan.items[0].appId=10;state.steamPlan.items[1].appId=20;state.steamPlan.items[2].appId=10;state.gameMedia.entries={conflict:{key:'conflict',appId:10,executable:state.library.games[1].primaryExecutable}};mergeContext.snapshot=state;
 const games=mergeContext.mergedGames();assert.equal(games[0].gameMedia.key,'conflict');assert.equal(games[1].gameMedia,undefined);assert.equal(games[2].gameMedia,undefined);
});
run('media-merge-normalizes-exact-executable-and-last-record-wins',()=>{
 const state=mergeFixture(1),exe=state.library.games[0].primaryExecutable.toUpperCase().replaceAll('/','\\');state.gameMedia.entries={first:{key:'first',executable:exe},last:{key:'last',executable:exe}};mergeContext.snapshot=state;assert.equal(mergeContext.mergedGames()[0].gameMedia.key,'last');
});
run('unconfirmed-id-never-becomes-a-wallpaper-alias',()=>{
 const state=mergeFixture(1);state.steamPlan.items[0].status='needs-identity-confirmation';state.steamPlan.items[0].steamVerificationStatus='unconfirmed';state.gameMedia.entries={only:{key:'only',appId:1000}};mergeContext.snapshot=state;const games=mergeContext.mergedGames();assert.equal(games.length,2);assert.equal(games[0].gameMedia,undefined);assert.equal(games[1].mediaOnly,true);
});
run('unmatched-media-records-stay-distinct-and-cleared-id-stays-cleared',()=>{
 const state=mergeFixture(1);state.manualOverrides.items[mergeContext.normalizedUiPath(state.library.games[0].primaryExecutable)]={idCleared:true};state.gameMedia.entries={a:{key:'a',appId:1000},b:{key:'b',appId:1000}};mergeContext.snapshot=state;const games=mergeContext.mergedGames();assert.equal(games.length,3);assert.equal(games[0].gameMedia,undefined);assert.equal(games[0].steam.appId,'');
});
run('card-selection-does-not-rebuild-or-copy-the-entire-library',()=>{
 const state=mergeFixture(2);mergeContext.snapshot=state;const game=mergeContext.mergedGames()[0];vm.runInContext('var actualMerged=mergedGames;var selectionMergeReads=0;mergedGames=()=>{++selectionMergeReads;return actualMerged();};',mergeContext);
 assert.equal(mergeContext.isPageSelected(game),true);assert.equal(mergeContext.selectionMergeReads,0);
 mergeContext.selectedGameDirectories=new Set();assert.equal(mergeContext.isPageSelected(game),false);mergeContext.selectedGameDirectories.add(mergeContext.keyFor(game));assert.equal(mergeContext.isPageSelected(game),true);
 mergeContext.activeTab='native-steam';assert.equal(mergeContext.isPageSelected(game),false);mergeContext.activeTab='not-in-steam';mergeContext.selectedGameDirectories=null;
});
const legacyLookupMetrics=[];
const lookupScope=callback=>typeof mergeContext.withLibraryPathLookups==='function'?mergeContext.withLibraryPathLookups(callback):callback();
run('legacy-path-override-and-identity-lookups-have-linear-batch-work',()=>{
 const state=mergeFixture(400);state.gameMedia.entries={};state.manualOverrides.items={};state.resolvedIdentities={};
 for(let i=0;i<400;i++){const exe=state.library.games[i].primaryExecutable;state.manualOverrides.items[exe.toUpperCase().replaceAll('/','\\')+'\\']={name:'Manual '+i};state.resolvedIdentities['legacy-'+i]={executable:exe,formalName:'Resolved '+i};}
 mergeContext.snapshot=state;mergeContext.pathWork=0;const started=performance.now(),games=mergeContext.mergedGames();legacyLookupMetrics.push({games:400,pathNormalizations:mergeContext.pathWork,elapsedMs:performance.now()-started});
 assert(games.every((game,i)=>game.override.name==='Manual '+i&&game.resolvedIdentity.formalName==='Resolved '+i));assert(mergeContext.pathWork<=400*30,'Historical path spelling still makes every lookup scan the inventory');
});
run('override-index-retains-direct-precedence-and-first-falsy-fallback',()=>{
 const key='g:\\fixture\\mixed\\game.exe',legacy='G:/Fixture/Mixed/Game.exe/';const items={[legacy]:{name:'legacy-first'},[key]:{name:'direct'}};mergeContext.snapshot={manualOverrides:{items}};
 assert.equal(lookupScope(()=>mergeContext.manualOverride(key)).name,'direct');delete items[key];assert.equal(lookupScope(()=>mergeContext.manualOverride(key)).name,'legacy-first');items[legacy]=null;items['G:\\FIXTURE\\MIXED\\GAME.EXE']={name:'must-not-win'};assert.equal(lookupScope(()=>mergeContext.manualOverride(key)).name,undefined);
});
run('identity-index-retains-alias-order-and-exact-direct-precedence',()=>{
 const key='g:\\fixture\\mixed\\game.exe';const items={earlier:{executable:key,formalName:'alias-first'},'G:/Fixture/Mixed/Game.exe':{formalName:'stored-later'},[key]:{formalName:'direct'}};mergeContext.snapshot={resolvedIdentities:items};
 assert.equal(lookupScope(()=>mergeContext.resolvedIdentity(key)).formalName,'direct');delete items[key];assert.equal(lookupScope(()=>mergeContext.resolvedIdentity(key)).formalName,'alias-first');
});
run('bucket-index-retains-directory-direct-executable-and-entry-order',()=>{
 const game={gameDirectory:'G:/Fixture/Mixed',primaryExecutable:'G:/Fixture/Mixed/Game.exe'},dir=mergeContext.normalizedUiPath(game.gameDirectory),exe=mergeContext.normalizedUiPath(game.primaryExecutable);
 const buckets={'G:/Fixture/Mixed/Game.exe':{bucket:'unclassified'},'G:/FIXTURE/Mixed/':{bucket:'not-in-steam'},[exe]:{bucket:'non-game'},[dir]:{bucket:'unsupported-user-choice'}};mergeContext.snapshot={config:{manualBuckets:buckets}};
 assert.equal(lookupScope(()=>mergeContext.manualBucket(game)),'auto');delete buckets[dir];assert.equal(lookupScope(()=>mergeContext.manualBucket(game)),'non-game');delete buckets[exe];assert.equal(lookupScope(()=>mergeContext.manualBucket(game)),'unclassified');buckets['G:/Fixture/Mixed/Game.exe'].bucket='';assert.equal(lookupScope(()=>mergeContext.manualBucket(game)),'not-in-steam');
});
run('lookup-scope-releases-indexes-on-error-and-next-in-place-update',()=>{
 const key='g:\\fixture\\mixed\\game.exe',legacy='G:/Fixture/Mixed/Game.exe';const state={manualOverrides:{items:{[legacy]:{name:'old'}}}};mergeContext.snapshot=state;let failed=false;
 try{lookupScope(()=>{assert.equal(mergeContext.manualOverride(key).name,'old');throw Error('fixture-lookup-failure');});}catch(error){failed=error.message==='fixture-lookup-failure';}
 state.manualOverrides.items[legacy]={name:'new'};assert(failed);assert.equal(lookupScope(()=>mergeContext.manualOverride(key)).name,'new');
 const other={manualOverrides:{items:{[legacy]:{name:'nested'}}}};lookupScope(()=>{mergeContext.snapshot=other;assert.equal(lookupScope(()=>mergeContext.manualOverride(key)).name,'nested');mergeContext.snapshot=state;assert.equal(mergeContext.manualOverride(key).name,'new');});
});
run('changed-primary-does-not-inherit-old-directory-plan-or-media',()=>{
 const state=mergeFixture(1),original=state.library.games[0].primaryExecutable;state.library.games[0].primaryExecutable=original.replace('game.exe','Alternate.exe');state.steamPlan.items[0].status='added-to-steam';mergeContext.snapshot=state;const games=mergeContext.mergedGames();assert.equal(games[0].steam.appId,undefined);assert.equal(games[0].gameMedia,undefined);assert.equal(games[1].mediaOnly,true);
});
run('directory-plan-without-an-executable-retains-legacy-display-fallback',()=>{
 const state=mergeFixture(1);delete state.steamPlan.items[0].primaryExecutable;mergeContext.snapshot=state;assert.equal(mergeContext.mergedGames()[0].steam.appId,1000);
});
const report = { legacyLookupMetrics, mergeMetrics, allPassed: reports.every(test => test.passed), caseCount: reports.length, fuzzCombinationCount: 1000, artworkPriorityCombinationCount: 1000, cases: reports, realSteamFilesModified: false };
const build = path.resolve(root, '../../Build');
const output = path.resolve(process.argv[2] || path.join(build, 'CustomSteamLibrary/ui-datachain-summary.json'));
const evidence = path.resolve(root, '../../../Testing/YMCC/Evidence');
assert(output.startsWith(build + path.sep) || output.startsWith(evidence + path.sep), 'Report must stay inside workspace Build or the core Evidence directory');
fs.mkdirSync(path.dirname(output), { recursive: true }); fs.writeFileSync(output, JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2)); if (!report.allPassed) process.exitCode = 1;
