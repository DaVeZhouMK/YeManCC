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
const normalizedPathSource=source.split(/\r?\n/).find(line=>line.startsWith('function normalizedUiPath('));
assert(normalizedPathSource,'Missing production path normalization');
vm.runInContext(normalizedPathSource,context);
const mediaSupportStart=source.indexOf('function selectMediaDownloadDisplay('),mediaSupportEnd=source.indexOf('let mediaManualBusy=',mediaSupportStart);
assert(mediaSupportStart>=0&&mediaSupportEnd>mediaSupportStart,'Missing production media read support');
vm.runInContext(source.slice(mediaSupportStart,mediaSupportEnd),context);
const preserveMediaStart=source.indexOf('function preserveNewerEditorMedia('),preserveMediaEnd=source.indexOf('\nfunction ',preserveMediaStart+15);
assert(preserveMediaStart>=0&&preserveMediaEnd>preserveMediaStart,'Missing production media revision support');
vm.runInContext(source.slice(preserveMediaStart,preserveMediaEnd),context);
vm.runInContext(source.slice(begin, end), context);
const normalize = (value, prior = previous) => context.normalizeWorkspaceSnapshot(value, prior);
const reports = [];
const manualIdentityFeedback=[];
function manualIdentityFeedbackFixture() {
  let listener, refreshes=0;
  const notices=[];
  const fixture=vm.createContext({
    window:{chrome:{webview:{addEventListener(type,callback){assert.equal(type,'message');listener=callback;}}}},
    isWorkspaceRecord:context.isWorkspaceRecord,manualIdentifyGenerations:new Map(),
    snapshot:{},editDraft:null,selectedActionGame:null,libraryNotice:{},
    scheduleCardOperationState(){},scheduleSilentRefresh(){++refreshes;},
    showNotice(message,error){notices.push({message:String(message),error:Boolean(error)});}
  });
  vm.runInContext(normalizedPathSource,fixture);
  const matchBegin=source.indexOf('function manualIdentifyEventMatches('),matchEnd=source.indexOf('async function searchGame(',matchBegin);
  assert(matchBegin>=0&&matchEnd>matchBegin);vm.runInContext(source.slice(matchBegin,matchEnd),fixture);
  const eventBegin=source.indexOf("window.chrome.webview.addEventListener('message', event => {"),eventEnd=source.indexOf('\n});',eventBegin);
  assert(eventBegin>=0&&eventEnd>eventBegin);vm.runInContext(source.slice(eventBegin,eventEnd+5),fixture);
  return {emit(message){listener({data:structuredClone(message)});},notices,get refreshes(){return refreshes;}};
}

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
run('malformed-bridge-message-does-not-access-array-shape', () => {
  assert(source.includes('isWorkspaceRecord(event.data) ? event.data : {}'));
  const messages=[
    ['conflict','needs-confirmation','后台返回的身份与本次选择不一致，已保留当前 ID。',/本次选择不一致/,true],
    ['ready','ready',null,/Steam 身份已确认/,false],
    ['metadata','metadata-verified',null,/游戏资料已确认.*Steam 身份仍待确认/,false],
    ['pending','pending-network',null,/网络补齐暂未完成/,false],
    ['unconfirmed','needs-confirmation',null,/身份尚未确认/,true],
    ['missing-status','',null,/身份尚未确认/,true]
  ];
  const base={kind:'event',executable:'G:/Fixture/Feedback/game.exe',generation:10};
  for(const [variant,identityStatus,error,expected,errorStyle] of messages){
    const fixture=manualIdentityFeedbackFixture();fixture.emit({...base,event:'manual-identify-started'});
    fixture.emit({...base,event:'manual-identify-complete',identityStatus,error});
    const actual=fixture.notices.at(-1),passed=Boolean(actual&&expected.test(actual.message)&&actual.error===errorStyle&&fixture.refreshes===1);
    manualIdentityFeedback.push({variant,actual,refreshes:fixture.refreshes,passed});
  }
  const generations=manualIdentityFeedbackFixture();for(const data of [null,7,[],false])generations.emit(data);
  generations.emit({...base,event:'manual-identify-started'});generations.emit({...base,event:'manual-identify-complete',identityStatus:'ready'});
  generations.emit({...base,generation:11,event:'manual-identify-started'});generations.emit({...base,generation:11,event:'manual-identify-complete',identityStatus:'ready'});
  const secondCompleted=generations.refreshes===2&&/Steam 身份已确认/.test(generations.notices.at(-1)?.message||'');
  const count=generations.notices.length,refreshes=generations.refreshes;
  for(const event of ['manual-identify-started','manual-identify-complete','manual-identify-failed'])generations.emit({...base,event,identityStatus:'ready',error:'stale'});
  const oldIgnored=generations.notices.length===count&&generations.refreshes===refreshes;
  generations.emit({...base,generation:12,event:'manual-identify-complete',identityStatus:'ready'});
  const terminalWithoutStart=generations.refreshes===refreshes+1;
  generations.emit({...base,executable:'G:/Fixture/Other/game.exe',generation:1,event:'manual-identify-complete',identityStatus:'metadata-verified'});
  const independentExe=generations.refreshes===refreshes+2&&/游戏资料已确认/.test(generations.notices.at(-1)?.message||'');
  manualIdentityFeedback.push({variant:'successive-generations',secondCompleted,oldIgnored,terminalWithoutStart,independentExe,passed:secondCompleted&&oldIgnored&&terminalWithoutStart&&independentExe});
  assert(manualIdentityFeedback.every(row=>row.passed),'Manual identity feedback misreported completion or ignored the current generation');
});
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
mergeContext.isBuiltinExcludedSteamTool=context.isBuiltinExcludedSteamTool;
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
 const state=mergeFixture(1);state.steamPlan.items[0].status='needs-identity-confirmation';state.steamPlan.items[0].steamVerificationStatus='unconfirmed';state.gameMedia.entries={only:{key:'only',appId:1000}};mergeContext.snapshot=state;const games=mergeContext.mergedGames();assert.equal(games.length,1);assert.equal(games[0].gameMedia,undefined);assert.equal(state.gameMedia.entries.only.appId,1000);
});
run('unmatched-media-records-stay-distinct-and-cleared-id-stays-cleared',()=>{
 const state=mergeFixture(1);state.manualOverrides.items[mergeContext.normalizedUiPath(state.library.games[0].primaryExecutable)]={idCleared:true};state.gameMedia.entries={a:{key:'a',appId:1000},b:{key:'b',appId:1000}};mergeContext.snapshot=state;const games=mergeContext.mergedGames();assert.equal(games.length,1);assert.equal(games[0].gameMedia,undefined);assert.equal(games[0].steam.appId,'');assert.equal(Object.keys(state.gameMedia.entries).length,2,'Unmatched cache records were destroyed');
 const native=mergeFixture(0);native.gameMedia.entries={
  native:{key:'steam-1687950',appId:1687950,name:'Persona 5 Royal',identityVerified:true,source:'steam',gameDirectory:'D:/soft/steam/steamapps/common/P5R'},
  tool:{key:'steam-228980',appId:228980,name:'Steamworks Common Redistributables',identityVerified:true,gameDirectory:'D:/soft/steam/steamapps/common/Steamworks Shared'},
  deleted:{key:'removed',appId:620,name:'Removed',deleted:true},
  custom:{key:'custom',appId:1000,name:'Custom game',identityVerified:true,source:'steam',executable:'D:/GAME/Custom/game.exe',gameDirectory:'D:/GAME/Custom'},
  unknown:{key:'unknown',appId:1001,name:'Unconfirmed',identityVerified:false,source:'steam',gameDirectory:'D:/soft/steam/steamapps/common/Unknown'}
 };native.library.games=[{gameDirectory:'D:/soft/steam/steamapps/common/P5R',directoryName:'P5R',steamNative:true,nativeSteamAppId:1687950}];mergeContext.snapshot=native;const beforeCache=JSON.stringify(native.gameMedia);const visible=mergeContext.mergedGames();
 assert.equal(visible.length,1,'A wallpaper cache record became an installation card');
 assert.equal(mergeContext.isNativeSteamGame(visible[0]),true,'Scanned native Steam game disappeared');
 assert.equal(JSON.stringify(native.gameMedia),beforeCache,'Library projection changed persisted cache');
 assert.equal(visible.some(game=>game.mediaOnly),false,'Unmatched cache still generates headless cards');
 assert.equal(mergeContext.gameDisplayName({directoryName:'The Legend of Zelda Tears of the kingdom',primaryExecutable:'D:/GAME/Zelda/totk_xlate.exe',primaryProductName:'totk_xlate'}),'The Legend of Zelda Tears of the kingdom');
 assert.equal(mergeContext.gameDisplayName({directoryName:'Collection 12345',primaryExecutable:'D:/GAME/Collection 12345/Sifu.exe',primaryProductName:'Sifu'}),'Sifu','A valid PE title was replaced by an unrelated directory name');
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
 const state=mergeFixture(1),original=state.library.games[0].primaryExecutable;state.library.games[0].primaryExecutable=original.replace('game.exe','Alternate.exe');state.steamPlan.items[0].status='added-to-steam';mergeContext.snapshot=state;const games=mergeContext.mergedGames();assert.equal(games[0].steam.appId,undefined);assert.equal(games[0].gameMedia,undefined);assert.equal(games.length,1);assert.equal(state.gameMedia.entries['steam-1000'].executable,original);
 // The same original merge consumer must not borrow identity metadata
 // across a protected Steam AppID even when the executable is unchanged.
 for(const kind of ['conflicting-cache','metadata-only-cache','same-id-cache','manual-title','native-conflicting-cache']){
  const bound=mergeFixture(1),exe=bound.library.games[0].primaryExecutable,key=mergeContext.normalizedUiPath(exe),row=bound.steamPlan.items[0];
  Object.assign(row,{steamStoreAppId:1000,storefrontAppId:1000,identityStatus:'verified',steamVerificationStatus:'steam-verified',formalName:'Bound identity'});
  bound.resolvedIdentities[key]={steamAppId:kind==='same-id-cache'?1000:2000,appId:kind==='same-id-cache'?1000:2000,formalName:kind==='same-id-cache'?'Same identity refreshed title':'Foreign cached title'};
  if(kind==='metadata-only-cache')bound.resolvedIdentities[key]={igdbId:77,formalName:'Auxiliary candidate title'};
  if(kind==='manual-title')bound.manualOverrides.items[key]={name:'User title'};
  if(kind==='native-conflicting-cache'){bound.library.games[0].steamNative=true;bound.library.games[0].nativeSteamAppId=1000;row.steamNative=true;row.nativeSteamAppId=1000;row.status='already-in-steam';}
  mergeContext.snapshot=bound;const cached=JSON.stringify(bound.resolvedIdentities),game=mergeContext.mergedGames()[0];
  assert.equal(mergeContext.confirmedMediaAppId(game),'1000');assert.equal(mergeContext.gameDisplayName(game),kind==='same-id-cache'?'Same identity refreshed title':kind==='manual-title'?'User title':'Bound identity',kind);
  assert.equal(JSON.stringify(bound.resolvedIdentities),cached,'Raw cache must remain available for explicit review');
 }
});
run('directory-plan-without-an-executable-retains-legacy-display-fallback',()=>{
 const state=mergeFixture(1);delete state.steamPlan.items[0].primaryExecutable;mergeContext.snapshot=state;assert.equal(mergeContext.mergedGames()[0].steam.appId,1000);
});
const report = { manualIdentityFeedback, legacyLookupMetrics, mergeMetrics, allPassed: reports.every(test => test.passed), caseCount: reports.length, fuzzCombinationCount: 1000, artworkPriorityCombinationCount: 1000, cases: reports, realSteamFilesModified: false };
const build = path.resolve(root, '../../Build');
const output = path.resolve(process.argv[2] || path.join(build, 'CustomSteamLibrary/ui-datachain-summary.json'));
const evidence = path.resolve(root, '../../../Testing/YMCC/Evidence');
assert(output.startsWith(build + path.sep) || output.startsWith(evidence + path.sep), 'Report must stay inside workspace Build or the core Evidence directory');
fs.mkdirSync(path.dirname(output), { recursive: true }); fs.writeFileSync(output, JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2)); if (!report.allPassed) process.exitCode = 1;
