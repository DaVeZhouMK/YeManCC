// All native, process and filesystem calls below are in-memory mocks. No real
// AppData, games, client processes, services or Steam settings are touched.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import crypto from 'node:crypto';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'package.json'));
const { build } = require('esbuild');
const configStorm=process.argv.includes('--config-storm');
const evidence=process.env.YMCC_OPTI_CONFIG_OUT?path.resolve(process.env.YMCC_OPTI_CONFIG_OUT):null;
assert.ok(!configStorm||evidence,'--config-storm needs an owned evidence directory');
assert.ok(configStorm||!process.env.YMCC_OPTI_CONFIG_SOURCE,'private source requires --config-storm');
const sourcePath=path.resolve(process.env.YMCC_OPTI_CONFIG_SOURCE||path.join(root,'src/bridge/optiscalerClientSync.ts'));
const pin=p=>{const b=fs.readFileSync(p);return {path:path.resolve(p),bytes:b.length,sha256:crypto.createHash('sha256').update(b).digest('hex')}};
const inputs=evidence?[sourcePath,path.join(root,'tools/optiscaler_client_sync_selftest.mjs'),path.join(root,'src/bridge/api.ts'),require.resolve('esbuild'),require.resolve('@esbuild/win32-x64/esbuild.exe'),process.execPath].map(pin):[];
let stormRunning=false;const receipts=[],originalNames=[],compiledInputs=[];

const mockApi = `
import {fs as actualFs} from 'fixture-real-api';
const f=()=>globalThis.__optiFixture;
export const fs={
 exists:async p=>{f().io.push({api:'exists',path:p});if(p===f().existsDenied)throw Error('ACCESS_DENIED');return Object.hasOwn(f().files,p)},
 readTextFile:async(p,maxBytes,options)=>{f().io.push({api:'read',path:p,maxBytes,bounded:options?.bounded===true});if(f().onRead)f().onRead(p);return actualFs.readTextFile(p,maxBytes,options)},
 mkdir:async p=>{f().directories.push(p);if(p===f().mkdirDenied)return false;return true},
 writeTextFileAtomic:async(p,s)=>{f().writes.push(p);if(p===f().failWrite)throw Error('denied write');return actualFs.writeTextFileAtomic(p,s)}
};
export const losslessRuntime=new Proxy(Object.create(null),{get(){throw Error('Lossless control is outside this opti-client fixture')}});
export const proc={running:async names=>{f().processNames.push(names);const r=f().runningChecks?.length?f().runningChecks.shift():f().running;return {OptiscalerClient:r}}};
export const registry={read:async()=>f().appData};
export const app={powerControlDir:async()=> 'C:\\\\Fixture\\\\PowerControl',exeDir:async()=> 'C:\\\\Fixture\\\\YMCC'};
export const shell={run:async(exe,args)=>{f().runs.push({exe,args});return {exitCode:f().exitCode??0,stdout:JSON.stringify(f().result),stderr:''}},execute:async(p,args)=>{f().executions.push({path:p,args});return true}};
`;
const output = await build({
 stdin:{contents:configStorm?`export * from ${JSON.stringify(sourcePath)};`:`export * from './src/bridge/optiscalerClientSync';export * from './src/bridge/quickapp';export * from './src/bridge/optiImportPolicy';`,resolveDir:root,sourcefile:'opti-selftest.ts'},
 bundle:true,platform:'node',format:'esm',write:false,metafile:true,
 plugins:[{name:'mock-all-native',setup(b){
  b.onLoad({filter:/\.[cm]?[jt]sx?$/,namespace:'file'},args=>{
   const bytes=fs.readFileSync(args.path);compiledInputs.push({path:args.path,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')});
   const ext=path.extname(args.path).slice(1);return {contents:bytes,loader:['ts','tsx','jsx'].includes(ext)?ext:'js'};
  });
  b.onResolve({filter:/^fixture-real-api$/},()=>({path:path.join(root,'src/bridge/api.ts')}));
  b.onResolve({filter:/^\.\/ipc$/},args=>args.importer===path.join(root,'src/bridge/api.ts')?({path:'fake-config-ipc',namespace:'config-ipc'}):undefined);
  b.onLoad({filter:/.*/,namespace:'config-ipc'},()=>({contents:'export const invoke=(cmd,args)=>globalThis.__optiFixture.invoke(cmd,args);',loader:'js'}));
  b.onResolve({filter:/^\.\/api$/},()=>({path:'mock-api',namespace:'fixture'}));
  b.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:mockApi,loader:'js'}));
 }}]
});
const m = await import(`data:text/javascript;base64,${Buffer.from(output.outputFiles[0].text).toString('base64')}`);
const appData='C:\\Fixture\\Roaming';
const paths=m.optiClientPaths(appData);
const game='D:\\Games\\Example\\Game.exe';
let passed=0;const fixtureInstances=[];
function fixture(extra={}){
 globalThis.__optiFixture={files:{},directories:[],writes:[],processNames:[],runs:[],executions:[],io:[],running:false,appData,result:{ok:true,version:'0.9.5-pre4/FSR_4.1.1b',backend:'fsr',analysis:{anti_cheat:false}},...extra};
 const current=globalThis.__optiFixture;
 current.invoke=async(cmd,args)=>{
  const p=args.path;assert.ok([paths.games,paths.pending,paths.games+'.ymcc.bak'].includes(p),'Unowned config path '+p);
  const e={api:cmd,path:p};current.io.push(e);
  try{
   if(cmd==='fs.readTextRange'||cmd==='fs.readTextFile'){
    if(p===current.readDenied)throw Error('SHARING_VIOLATION');
    if(!Object.hasOwn(current.files,p))throw Error('FILE_NOT_FOUND');
    let text=current.files[p];if(p===current.mismatchReadback&&current.writes.includes(p))text=text+' ';
    e.bytes=Buffer.byteLength(text);e.sha256=crypto.createHash('sha256').update(text).digest('hex');
    if(cmd==='fs.readTextRange'){
     const receipt={content:Buffer.from(text).subarray(0,args.maxBytes).toString('utf8'),offset:0,size:e.bytes};
     if(current.badReceipt==='size')receipt.size=-1;if(current.badReceipt==='offset')receipt.offset=1;if(current.badReceipt==='short')receipt.size++;
     return receipt;
    }
    return text;
   }
   assert.equal(cmd,'fs.writeTextFileAtomic');e.bytes=Buffer.byteLength(args.content);e.sha256=crypto.createHash('sha256').update(args.content).digest('hex');
   if(current.onWrite)current.onWrite(p,args.content);
   if(p===current.rejectAtomic){e.ok=false;return false;}
   if(p===current.silentWrite||p===current.dropClear&&args.content.trim()==='[]'){e.silentDrop=true;e.ok=true;return true;}
   current.files[p]=args.content;e.ok=true;if(current.afterWrite)current.afterWrite(p,args.content);return true;
  }catch(error){e.ok=false;e.error=String(error);throw error;}
 };
 fixtureInstances.push(current);
 return globalThis.__optiFixture;
}
async function test(name,fn){
 if(configStorm&&!stormRunning)return;
 if(configStorm&&process.env.YMCC_OPTI_CONFIG_CASE&&name!==process.env.YMCC_OPTI_CONFIG_CASE)return;
 if(!configStorm){await fn();passed++;originalNames.push(name);console.log('PASS '+name);return;}
 const start=fixtureInstances.length;let error='';try{await fn();}catch(failure){error=String(failure);}
 const pass=!error;receipts.push({name,pass,error,fixtures:fixtureInstances.slice(start).map(f=>({io:f.io,writes:f.writes,directories:f.directories,processNames:f.processNames,runs:f.runs,executions:f.executions,files:Object.entries(f.files).map(([p,s])=>({path:p,bytes:Buffer.byteLength(s),sha256:crypto.createHash('sha256').update(s).digest('hex')}))}))});
 if(pass)passed++;console.log((pass?'PASS ':'FAIL ')+name+(error?' : '+error:''));
}
const catalog=()=>JSON.parse(globalThis.__optiFixture.files[paths.games]);
if(configStorm){
 stormRunning=true;
 const queued={executablePath:game,name:'Game',installed:true,version:'1.0/Extra'};
 const pending=JSON.stringify([queued]);
 const original=JSON.stringify([{Name:'User Name',ExecutablePath:game,InstallPath:'D:/Games/Example',Platform:0,AppId:'123',IsFavorite:true,Settings:{route:9},FutureField:{keep:true},ExternalOwnedDebt:{owner:'outside',original:'KEEP'}},{Name:'Other',ExecutablePath:'E:/Other.exe',Settings:{preserve:7}}]);
 const noActions=f=>{assert.equal(f.runs.length,0);assert.equal(f.executions.length,0);};
 const keep=(f,p,bytes)=>{assert.equal(f.files[p],bytes);assert.ok(!f.writes.includes(p));noActions(f);};
 await test('missing defaults remain empty and explicit valid sync has verified queue backup and catalog receipts',async()=>{const f=fixture();assert.equal((await m.flushOptiClientGames()).status,'idle');assert.equal(f.writes.length,0);assert.equal((await m.syncOptiClientGame(game,true)).status,'synced');assert.equal(catalog().length,1);assert.equal(f.files[paths.pending].trim(),'[]');assert.ok(f.io.some(e=>e.api==='read'&&e.path===paths.pending));assert.ok(f.io.filter(e=>e.api==='read').every(e=>e.bounded&&e.maxBytes===16*1024*1024));noActions(f);});
 const pendingBad=[['empty',''],['truncated','[{'],['wrong root','{}'],['null root','null'],['wrong row','[null]'],['wrong bool',JSON.stringify([{...queued,installed:'true'}])],['duplicate keys','[{"executablePath":'+JSON.stringify(game)+',"name":"Game","installed":false,"installed":true}]'],['escaped duplicate keys','[{"executablePath":'+JSON.stringify(game)+',"name":"Game","installed":true,"\\u006eame":"spoof"}]'],['future envelope',JSON.stringify({schema:999,updates:[queued]})],['duplicate identity',JSON.stringify([queued,{...queued,executablePath:game.toUpperCase()}])]];
 for(const [name,bad]of pendingBad)await test('pending '+name+' is preserved without catalog authority',async()=>{const f=fixture({files:{[paths.pending]:bad,[paths.games]:original}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.pending,bad);keep(f,paths.games,original);});
 const catalogBad=[['empty',''],['truncated','[{'],['wrong root','{}'],['null root','null'],['wrong row','[1]'],['path type',JSON.stringify([{ExecutablePath:false,InstallPath:'D:/Games/Example'}])],['install path type',JSON.stringify([{ExecutablePath:game,InstallPath:42}])],['platform type',JSON.stringify([{ExecutablePath:game,Platform:'8'}])],['appId type',JSON.stringify([{ExecutablePath:game,AppId:{id:'123'}}])],['install state type',JSON.stringify([{ExecutablePath:game,IsOptiscalerInstalled:'true'}])],['duplicate keys','[{"ExecutablePath":'+JSON.stringify(game)+',"Name":"Keep","Name":"Spoof"}]'],['future envelope',JSON.stringify({schema:999,games:[]})]];
 for(const [name,bad]of catalogBad)await test('catalog '+name+' retains original and valid pending debt',async()=>{const f=fixture({files:{[paths.games]:bad,[paths.pending]:pending}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,bad);assert.equal(f.files[paths.pending],pending);});
 await test('catalog and pending UTF8 byte limit is enforced rather than character count',async()=>{const big=JSON.stringify([{...queued,Note:'界'.repeat(5600000)}]);for(const p of [paths.pending,paths.games]){const f=fixture({files:{[paths.pending]:pending,[paths.games]:original,[p]:big}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,p,big);}});
 await test('pending and catalog row limits reject without trimming debt or preferences',async()=>{for(const [p,big]of [[paths.pending,JSON.stringify(Array(1001).fill(queued))],[paths.games,JSON.stringify(Array.from({length:20001},(_,n)=>({Name:'Game'+n})))]]){const f=fixture({files:{[paths.pending]:pending,[paths.games]:original,[p]:big}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,p,big);}});
 await test('known catalog merge preserves preference unknown fields and external owned debt',async()=>{const f=fixture({files:{[paths.games]:original}});assert.equal((await m.syncOptiClientGame(game,true,'1.0/Extra')).status,'synced');const list=catalog();assert.equal(list[0].Name,'User Name');assert.equal(list[0].AppId,'123');assert.equal(list[0].IsFavorite,true);assert.deepEqual(list[0].Settings,{route:9});assert.deepEqual(list[0].FutureField,{keep:true});assert.deepEqual(list[0].ExternalOwnedDebt,{owner:'outside',original:'KEEP'});assert.deepEqual(list[1],JSON.parse(original)[1]);assert.equal(f.files[paths.games+'.ymcc.bak'],original);assert.ok(f.io.some(e=>e.api==='read'&&e.path===paths.games+'.ymcc.bak'));noActions(f);});
 await test('unknown pending metadata remains unconfirmed and is preserved across selected-game edits',async()=>{const unknown={...queued,UserNote:'KEEP',ExternalOwnedDebt:{owner:'outside',original:7}},bytes=JSON.stringify([unknown]),f=fixture({files:{[paths.pending]:bytes,[paths.games]:original},running:true});const result=await m.syncOptiClientGame(game,false);assert.equal(result.status,'pending');const actual=JSON.parse(f.files[paths.pending])[0];assert.equal(actual.UserNote,'KEEP');assert.deepEqual(actual.ExternalOwnedDebt,unknown.ExternalOwnedDebt);f.running=false;assert.equal((await m.flushOptiClientGames()).status,'pending');assert.ok(JSON.parse(f.files[paths.pending])[0].ExternalOwnedDebt);keep(f,paths.games,original);});
 await test('future pending metadata never authorizes catalog publication or debt clearing',async()=>{const bytes=JSON.stringify([{...queued,schemaVersion:999}]),f=fixture({files:{[paths.pending]:bytes,[paths.games]:original}});assert.equal((await m.flushOptiClientGames()).status,'pending');keep(f,paths.pending,bytes);keep(f,paths.games,original);});
 for(const target of ['queue','catalog'])await test(target+' sharing denial differs from missing and released IO can retry',async()=>{const p=target==='queue'?paths.pending:paths.games,f=fixture({files:{[paths.pending]:pending,[paths.games]:original},readDenied:p});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,original);f.readDenied='';assert.equal((await m.flushOptiClientGames()).status,'synced');noActions(f);});
 await test('registry and process unknown cannot authorize catalog changes',async()=>{for(const options of [{appData:null},{running:undefined}]){const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},...options});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,original);assert.equal(f.files[paths.pending],pending);}});
 for(const hook of ['silentWrite','rejectAtomic','failWrite'])await test('pending '+hook+' is not acknowledged as saved and explicit retry heals',async()=>{const f=fixture({files:{[paths.games]:original},[hook]:paths.pending});assert.equal((await m.syncOptiClientGame(game,true)).status,'error');keep(f,paths.games,original);f[hook]='';assert.equal((await m.syncOptiClientGame(game,true)).status,'synced');noActions(f);});
 await test('pending readback denial returns error before any catalog mutation and retry recovers',async()=>{const f=fixture({files:{[paths.games]:original},afterWrite:p=>{if(p===paths.pending)globalThis.__optiFixture.readDenied=p;}});assert.equal((await m.syncOptiClientGame(game,true)).status,'error');keep(f,paths.games,original);f.afterWrite=null;f.readDenied='';assert.equal((await m.flushOptiClientGames()).status,'synced');});
 await test('silent backup acknowledgement cannot authorize catalog publication',async()=>{const backup=paths.games+'.ymcc.bak',f=fixture({files:{[paths.pending]:pending,[paths.games]:original,[backup]:'[]'},silentWrite:backup});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,original);assert.equal(f.files[paths.pending],pending);f.silentWrite='';assert.equal((await m.flushOptiClientGames()).status,'synced');});
 await test('unreadable or corrupt backup is preserved instead of overwritten',async()=>{const backup=paths.games+'.ymcc.bak';for(const extra of [{files:{[backup]:'{bad'}},{files:{[backup]:'[]'},readDenied:backup}]){const f=fixture({...extra,files:{[paths.pending]:pending,[paths.games]:original,...extra.files}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,original);keep(f,backup,extra.files[backup]);}});
 for(const hook of ['silentWrite','rejectAtomic','failWrite'])await test('catalog '+hook+' retains catalog plus debt and can explicitly retry',async()=>{const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},[hook]:paths.games});assert.equal((await m.flushOptiClientGames()).status,'error');assert.equal(f.files[paths.games],original);assert.equal(f.files[paths.pending],pending);f[hook]='';assert.equal((await m.flushOptiClientGames()).status,'synced');noActions(f);});
 await test('pending clear silent failure never returns synced and retained debt can retry',async()=>{const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},dropClear:paths.pending});assert.equal((await m.flushOptiClientGames()).status,'error');assert.equal(f.files[paths.pending],pending);f.dropClear='';assert.equal((await m.flushOptiClientGames()).status,'synced');});
 await test('pending clear rejects and then retries without losing known return debt',async()=>{const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},onWrite:(p,text)=>{if(p===paths.pending&&text.trim()==='[]')throw Error('CLEAR_DENIED');}});assert.equal((await m.flushOptiClientGames()).status,'error');assert.equal(f.files[paths.pending],pending);f.onWrite=null;assert.equal((await m.flushOptiClientGames()).status,'synced');});
 await test('catalog postpublish readback mismatch is not a synced receipt and retry recovers',async()=>{const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},mismatchReadback:paths.games});assert.equal((await m.flushOptiClientGames()).status,'error');assert.equal(f.files[paths.pending],pending);f.mismatchReadback='';assert.equal((await m.flushOptiClientGames()).status,'synced');});
 await test('external pending change before queue publication is never replaced',async()=>{let reads=0;const external=JSON.stringify([{...queued,installed:false,ExternalOwnedDebt:{keep:true}}]),f=fixture({files:{[paths.pending]:pending,[paths.games]:original},onRead:p=>{if(p===paths.pending&&++reads===2)globalThis.__optiFixture.files[p]=external;}});assert.equal((await m.syncOptiClientGame(game,true)).status,'error');keep(f,paths.pending,external);keep(f,paths.games,original);});
 await test('external catalog change during backup is rejected before original publication',async()=>{const external=JSON.stringify([{Name:'External',ExecutablePath:'E:/Other.exe'}]),f=fixture({files:{[paths.pending]:pending,[paths.games]:original},afterWrite:p=>{if(p===paths.games+'.ymcc.bak')globalThis.__optiFixture.files[paths.games]=external;}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,external);assert.equal(f.files[paths.pending],pending);});
 await test('external pending append during catalog publication is never cleared',async()=>{const external=JSON.stringify([queued,{...queued,executablePath:'E:/Other.exe',name:'Other',ExternalOwnedDebt:{keep:true}}]),f=fixture({files:{[paths.pending]:pending,[paths.games]:original},afterWrite:p=>{if(p===paths.games)globalThis.__optiFixture.files[paths.pending]=external;}});assert.equal((await m.flushOptiClientGames()).status,'error');assert.equal(f.files[paths.pending],external);assert.ok(!f.writes.includes(paths.pending));noActions(f);});
 await test('late clear acknowledgement with external queue content is rejected without erasing it',async()=>{const external=JSON.stringify([{...queued,installed:false}]),f=fixture({files:{[paths.pending]:pending,[paths.games]:original},afterWrite:(p,text)=>{if(p===paths.pending&&text.trim()==='[]')globalThis.__optiFixture.files[p]=external;}});assert.equal((await m.flushOptiClientGames()).status,'error');assert.equal(f.files[paths.pending],external);});
 await test('process restart consumes only persisted valid catalog and pending receipt',async()=>{const f=fixture({running:true});assert.equal((await m.syncOptiClientGame(game,true)).status,'pending');const saved={...f.files},restarted=fixture({files:saved});assert.equal((await m.flushOptiClientGames()).status,'synced');assert.equal(catalog().length,1);assert.equal(restarted.files[paths.pending].trim(),'[]');noActions(restarted);});
 await test('unsafe target identity and duplicate game rows cannot grant a catalog update',async()=>{for(const target of ['../Game.exe','C:/Games/../Other.exe']){const f=fixture({files:{[paths.games]:original}});assert.equal((await m.syncOptiClientGame(target,true)).status,'error');keep(f,paths.games,original);}const dup=JSON.stringify([{ExecutablePath:game},{ExecutablePath:game.toUpperCase()}]),f=fixture({files:{[paths.pending]:pending,[paths.games]:dup}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,dup);});
 await test('older measured queue version is not retained by an explicit unknown-version update',async()=>{const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},running:true});assert.equal((await m.syncOptiClientGame(game,true)).status,'pending');assert.ok(!('version'in JSON.parse(f.files[paths.pending])[0]));f.running=false;assert.equal((await m.flushOptiClientGames()).status,'synced');assert.equal(catalog()[0].OptiscalerVersion,null);});
 await test('nested duplicate preference keys and unsafe numeric extensions preserve the original',async()=>{for(const extension of ['{"a":1,"a":2}','{"a":1e400}','{"a":9007199254740993}']){const bytes='[{"ExecutablePath":'+JSON.stringify(game)+',"Settings":'+extension+'}]',f=fixture({files:{[paths.pending]:pending,[paths.games]:bytes}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,bytes);assert.equal(f.files[paths.pending],pending);}});
 await test('overdeep catalog is unknown rather than stripped to selected defaults',async()=>{const bytes=JSON.stringify([{ExecutablePath:game,Settings:JSON.parse('['.repeat(65)+'1'+']'.repeat(65))}]),f=fixture({files:{[paths.pending]:pending,[paths.games]:bytes}});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,bytes);});
 await test('invalid range receipts cannot be consumed as current complete configurations',async()=>{for(const badReceipt of ['size','offset','short']){const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},badReceipt});assert.equal((await m.flushOptiClientGames()).status,'error');keep(f,paths.games,original);assert.equal(f.files[paths.pending],pending);}});
 await test('directory denial is a recoverable failure and does not claim pending saved',async()=>{const f=fixture({files:{[paths.games]:original},mkdirDenied:paths.queueDirectory});assert.equal((await m.syncOptiClientGame(game,true)).status,'error');assert.ok(!Object.hasOwn(f.files,paths.pending));keep(f,paths.games,original);f.mkdirDenied='';assert.equal((await m.syncOptiClientGame(game,true)).status,'synced');});
 await test('client starting after backup retains queued debt and can be retried after exit',async()=>{const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},afterWrite:p=>{if(p===paths.games+'.ymcc.bak')globalThis.__optiFixture.running=true;}});assert.equal((await m.flushOptiClientGames()).status,'pending');keep(f,paths.games,original);assert.equal(f.files[paths.pending],pending);f.afterWrite=null;f.running=false;assert.equal((await m.flushOptiClientGames()).status,'synced');});
 await test('clear readback denial does not claim synced and a new explicit valid action recovers',async()=>{const f=fixture({files:{[paths.pending]:pending,[paths.games]:original},afterWrite:(p,text)=>{if(p===paths.pending&&text.trim()==='[]')globalThis.__optiFixture.readDenied=p;}});assert.equal((await m.flushOptiClientGames()).status,'error');assert.equal(catalog()[0].IsOptiscalerInstalled,true);f.afterWrite=null;f.readDenied='';assert.equal((await m.syncOptiClientGame(game,true)).status,'synced');noActions(f);});
 stormRunning=false;
}
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
console.log(`${configStorm?"Opti config storm":"Opti warning/client sync"}: ${passed} tests passed; all native calls mocked.`);

if(evidence){
 const after=inputs.map(p=>pin(p.path));const changedInputs=after.filter((p,i)=>p.sha256!==inputs[i].sha256);const failures=receipts.filter(r=>!r.pass).length;
 const compiledAfter=compiledInputs.map(p=>pin(p.path));const changedCompiledInputs=compiledAfter.filter((p,i)=>p.sha256!==compiledInputs[i].sha256);
 const result={pass:failures===0&&(configStorm?receipts.length:originalNames.length)>0&&changedInputs.length===0&&changedCompiledInputs.length===0,selection:configStorm?'original optiscaler-client consumer --config-storm':'original optiscaler-client default consumer',cases:configStorm?receipts.length:originalNames.length,failures,receipts,originalNames,source:pin(sourcePath),inputsBefore:inputs,inputsAfter:after,changedInputs,compiledInputsBefore:compiledInputs,compiledInputsAfter:compiledAfter,changedCompiledInputs,actualBundleInputs:output.metafile.inputs,realFilesystemConfigAccess:0,realRegistryAccess:0,realProcessActions:0,realGameActions:0,scope:'actual owner/API receipt decoders under inert fs/registry/process/app/IPC only'};
 fs.mkdirSync(evidence,{recursive:true});fs.writeFileSync(path.join(evidence,'RESULT.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({pass:result.pass,cases:result.cases,failures,changedInputs}));process.exitCode=result.pass?0:1;
}
