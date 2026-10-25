// All native, process and filesystem calls below are in-memory mocks. No real
// AppData, games, client processes, services or Steam settings are touched.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'package.json'));
const { build } = require('esbuild');
const mockApi = `
const f=()=>globalThis.__optiFixture;
export const fs={
 exists:async p=>Object.hasOwn(f().files,p),
 readTextFile:async p=>{if(f().onRead)f().onRead(p);if(!Object.hasOwn(f().files,p))throw Error('missing '+p);return f().files[p]},
 mkdir:async p=>{f().directories.push(p);return true},
 writeTextFileAtomic:async(p,s)=>{f().writes.push(p);if(p===f().failWrite)throw Error('denied write');f().files[p]=s}
};
export const proc={running:async names=>{f().processNames.push(names);const r=f().runningChecks?.length?f().runningChecks.shift():f().running;return {OptiscalerClient:r}}};
export const registry={read:async()=>f().appData};
export const app={powerControlDir:async()=> 'C:\\\\Fixture\\\\PowerControl',exeDir:async()=> 'C:\\\\Fixture\\\\YMCC'};
export const shell={run:async(exe,args)=>{f().runs.push({exe,args});return {exitCode:f().exitCode??0,stdout:JSON.stringify(f().result),stderr:''}},execute:async(p,args)=>{f().executions.push({path:p,args});return true}};
`;
const output = await build({
 stdin:{contents:`export * from './src/bridge/optiscalerClientSync';export * from './src/bridge/quickapp';export * from './src/bridge/optiImportPolicy';`,resolveDir:root,sourcefile:'opti-selftest.ts'},
 bundle:true,platform:'node',format:'esm',write:false,
 plugins:[{name:'mock-all-native',setup(b){
  b.onResolve({filter:/^\.\/api$/},()=>({path:'mock-api',namespace:'fixture'}));
  b.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:mockApi,loader:'js'}));
 }}]
});
const m = await import(`data:text/javascript;base64,${Buffer.from(output.outputFiles[0].text).toString('base64')}`);
const appData='C:\\Fixture\\Roaming';
const paths=m.optiClientPaths(appData);
const game='D:\\Games\\Example\\Game.exe';
let passed=0;
function fixture(extra={}){
 globalThis.__optiFixture={files:{},directories:[],writes:[],processNames:[],runs:[],executions:[],running:false,appData,result:{ok:true,version:'0.9.5-pre4/FSR_4.1.1b',backend:'fsr',analysis:{anti_cheat:false}},...extra};
 return globalThis.__optiFixture;
}
async function test(name,fn){await fn();passed++;console.log('PASS '+name)}
const catalog=()=>JSON.parse(globalThis.__optiFixture.files[paths.games]);
await test('real AppData paths, no unexpanded environment token',()=>{
 assert(!paths.games.includes('%')); assert(paths.games.endsWith('OptiscalerClient\\games.json'));
 assert.throws(()=>m.optiClientPaths('%APPDATA%')); assert.throws(()=>m.optiClientPaths('C:\\Root\\..\\Other'));
});
await test('new Manual game matches client schema, no false FG success',()=>{
 const items=m.mergeOptiClientGames([],[{executablePath:game,name:'Example',installed:true,version:'0.9.5-pre4/FSR_4.1.1b'}],()=> 'deadbeef');
 assert.equal(items[0].Platform,8);assert.equal(items[0].AppId,'Manual_deadbeef');
 assert.equal(items[0].ExecutablePath,game);assert.equal(items[0].InstallPath,'D:\\Games\\Example');
 assert.equal(items[0].OptiscalerVersion,'0.9.5-pre4');assert.equal(items[0].Fsr4ExtraVersion,'FSR_4.1.1b');
 assert(!('IsAntiCheatDetected' in items[0]));assert(!('FrameGenerationSettings' in items[0]));
});
await test('merge preserves Steam identity, preferences and unknown fields',()=>{
 const existing={Name:'User Name',InstallPath:'D:\\Games\\Example',ExecutablePath:game,Platform:0,AppId:'123',IsFavorite:true,FrameGenerationSettings:{Route:9},FutureField:{v:1}};
 const before=JSON.stringify(existing);
 const items=m.mergeOptiClientGames([existing,{Name:'Other',ExecutablePath:'E:\\Other.exe'}],[{executablePath:game.toUpperCase(),name:'New',installed:true}],()=> 'unused');
 assert.equal(items.length,2);assert.equal(items[0].Name,'User Name');assert.equal(items[0].Platform,0);assert.equal(items[0].AppId,'123');
 assert.deepEqual(items[0].FrameGenerationSettings,{Route:9});assert.deepEqual(items[0].FutureField,{v:1});assert.equal(JSON.stringify(existing),before);
});
await test('nested EXE fills existing Steam root instead of creating duplicate Manual entry',()=>{
 const nested='D:\\Games\\Example\\Binaries\\Win64\\Game.exe';
 const items=m.mergeOptiClientGames([{Name:'Steam Game',InstallPath:'D:\\Games\\Example',Platform:0,AppId:'123',ExecutablePath:''}],[{executablePath:nested,name:'Game',installed:true}],()=> 'unused');
 assert.equal(items.length,1);assert.equal(items[0].Platform,0);assert.equal(items[0].AppId,'123');assert.equal(items[0].InstallPath,'D:\\Games\\Example');assert.equal(items[0].ExecutablePath,nested);
});
await test('restored original installation does not retain a stale measured version',()=>{
 const items=m.mergeOptiClientGames([{ExecutablePath:game,OptiscalerVersion:'0.9.5-pre4'}],[{executablePath:game,name:'Game',installed:true}],()=> 'unused');
 assert.equal(items[0].IsOptiscalerInstalled,true);assert.equal(items[0].OptiscalerVersion,null);
});
await test('duplicate target identities are never guessed',()=>{
 assert.throws(()=>m.mergeOptiClientGames([{ExecutablePath:game},{ExecutablePath:game.toUpperCase()}],[{executablePath:game,name:'X',installed:true}],()=> 'id'));
});
await test('closed client syncs atomically and backs up original catalog',async()=>{
 const original='[{"Name":"Other","ExecutablePath":"E:\\\\Other.exe","FutureField":7}]';
 const f=fixture({files:{[paths.games]:original}}); const result=await m.syncOptiClientGame(game,true,'0.9.5-pre4/FSR_4.1.1b');
 assert.equal(result.status,'synced');assert.equal(catalog().length,2);assert.equal(f.files[paths.games+'.ymcc.bak'],original);
 assert.deepEqual(JSON.parse(f.files[paths.pending]),[]);assert(f.processNames.every(names=>names[0]==='OptiscalerClient'));
 assert.equal(f.executions.length,0);
});
await test('repeated sync is idempotent and restore preserves game entry',async()=>{
 const f=fixture();await m.syncOptiClientGame(game,true);await m.syncOptiClientGame(game,true);const id=catalog()[0].AppId;
 await m.syncOptiClientGame(game,false);assert.equal(catalog().length,1);assert.equal(catalog()[0].AppId,id);assert.equal(catalog()[0].IsOptiscalerInstalled,false);
});
await test('running client queues only; never overwrites its catalog',async()=>{
 const original='[{"Name":"Other"}]';const f=fixture({running:true,files:{[paths.games]:original}});
 const result=await m.syncOptiClientGame(game,true);assert.equal(result.status,'pending');assert.equal(f.files[paths.games],original);
 assert(!f.writes.includes(paths.games));assert.equal(JSON.parse(f.files[paths.pending]).length,1);assert.equal(f.executions.length,0);
 f.running=false;assert.equal((await m.flushOptiClientGames()).status,'synced');assert.equal(catalog().length,2);
});
await test('restore supersedes a queued install before client opens',async()=>{
 const f=fixture({running:true});await m.syncOptiClientGame(game,true);await m.syncOptiClientGame(game,false);
 assert.equal(JSON.parse(f.files[paths.pending]).length,1);f.running=false;await m.flushOptiClientGames();assert.equal(catalog()[0].IsOptiscalerInstalled,false);
});
await test('client starts between checks: postpone rather than overwrite',async()=>{
 const original='[]';const f=fixture({runningChecks:[false,true],files:{[paths.games]:original}});
 assert.equal((await m.syncOptiClientGame(game,true)).status,'pending');assert.equal(f.files[paths.games],original);assert(!f.writes.includes(paths.games));
});
await test('unknown process state fails closed for catalog writes',async()=>{
 const f=fixture({running:undefined,files:{[paths.games]:'[]'}});
 assert.equal((await m.syncOptiClientGame(game,true)).status,'error');assert.equal(f.files[paths.games],'[]');assert(!f.writes.includes(paths.games));
});
await test('malformed catalog is preserved with pending registration',async()=>{
 const f=fixture({files:{[paths.games]:'{broken'}});assert.equal((await m.syncOptiClientGame(game,true)).status,'error');
 assert.equal(f.files[paths.games],'{broken');assert.equal(JSON.parse(f.files[paths.pending]).length,1);
});
await test('concurrent catalog edits are not overwritten',async()=>{
 let reads=0;const f=fixture({files:{[paths.games]:'[]'},onRead:p=>{if(p===paths.games&&++reads===2)globalThis.__optiFixture.files[p]='[{"Name":"Changed"}]'}});
 assert.equal((await m.syncOptiClientGame(game,true)).status,'error');assert.equal(f.files[paths.games],'[{"Name":"Changed"}]');assert(!f.writes.includes(paths.games));
});
await test('failed atomic replacement retains original and pending queue',async()=>{
 const f=fixture({files:{[paths.games]:'[]'},failWrite:paths.games});assert.equal((await m.syncOptiClientGame(game,true)).status,'error');
 assert.equal(f.files[paths.games],'[]');assert.equal(JSON.parse(f.files[paths.pending]).length,1);
});
await test('BOM catalog and malformed pending record are handled safely',async()=>{
 fixture({files:{[paths.games]:'\uFEFF[]'}});assert.equal((await m.syncOptiClientGame(game,true)).status,'synced');
 const f=fixture({files:{[paths.games]:'[]',[paths.pending]:'{"broken":true}'}});
 assert.equal((await m.syncOptiClientGame(game,true)).status,'error');assert.equal(f.files[paths.pending],'{"broken":true}');assert.equal(f.files[paths.games],'[]');
});
await test('CLI acknowledgement is explicit and install-only',async()=>{
 let f=fixture();assert.equal((await m.oneClickOptiScaler(game,false,'fsr')).ok,true);assert(!f.runs[0].args.includes('--acknowledge-anticheat-risk'));
 f=fixture({result:{ok:true,version:'0.9.5-pre4/FSR_4.1.1b',analysis:{anti_cheat:true},warnings:['risk']}});
 const installed=await m.oneClickOptiScaler(game,false,'xess',{acknowledgeAntiCheatRisk:true});
 assert.equal(installed.antiCheat,true);assert(f.runs[0].args.includes('--acknowledge-anticheat-risk'));assert.equal(installed.clientSyncStatus,'synced');
 f=fixture();await m.oneClickOptiScaler(game,true,'auto',{acknowledgeAntiCheatRisk:true});assert(!f.runs[0].args.includes('--acknowledge-anticheat-risk'));
});
await test('failed install never registers; registration failure keeps install success',async()=>{
 let f=fixture({exitCode:7,result:{ok:false,msgs:['blocked']}});assert.equal((await m.oneClickOptiScaler(game,false,'fsr')).ok,false);assert.equal(f.writes.length,0);
 f=fixture({files:{[paths.games]:'broken'}});const result=await m.oneClickOptiScaler(game,false,'fsr');assert.equal(result.ok,true);assert.equal(result.clientSyncStatus,'error');
});
await test('open-client flushes pending records before issuing open request',async()=>{
 const f=fixture({running:true});await m.syncOptiClientGame(game,true);f.running=false;const notice=await m.openOptiConsole();
 assert.match(notice,/已同步/);assert.equal(catalog().length,1);assert.equal(f.executions.length,1);assert(f.executions[0].path.endsWith('OptiscalerClient.exe'));
});
await test('anti-cheat imports never offer launch; normal imports still can',()=>{
 assert.equal(m.optiMayOfferLaunch(true,false),false);assert.equal(m.optiMayOfferLaunch(false,true),false);assert.equal(m.optiMayOfferLaunch(false,false),true);
 assert.match(m.OPTI_OFFLINE_GUIDE,/不执行反作弊绕过/);assert.match(m.optiAntiCheatWarning('Example'),/不会自动启动/);
});
console.log(`Opti warning/client sync: ${passed} tests passed; all native calls mocked.`);
