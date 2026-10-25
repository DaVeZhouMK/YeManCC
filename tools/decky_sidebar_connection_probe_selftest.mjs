import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {load} from './fixtures/decky_game_memory_store.mjs';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
import {root,validation} from './fixtures/decky_task_paths.mjs';
const context=load(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'));
const {ReadOnlyMirrorRelay}=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':context});
const runId='a'.repeat(32),replies=[],tests=[];let reads=0,writes=0;
const relay=new ReadOnlyMirrorRelay({read:async()=>({generation:1,ready:true,game:null,provenance:{kind:'ymcc-native',pid:123},fan:{supported:false,enabled:false,preset:'balanced',choices:[]}}),observe:()=>()=>{},reply:async(connection,message)=>{replies.push(message);return true;},execute:async()=>{writes++;throw Error('No writes in connection test');}});
const read=relay.deps.read;relay.deps.read=()=>{reads++;return read();};
async function check(name,fn){await fn();tests.push(name);}
const request=extra=>relay.request({connection:'1',runId,request:{command:'snapshot',runId,clientId:'probe-client',...extra}});
await check('ordinary snapshot may reuse a cache, fresh probe forces a native read and correlated reply',async()=>{await request({});const first=replies.at(-1).snapshot.revision;assert.equal(reads,1);await request({});assert.equal(reads,1);await request({probeId:'probe:probe-client:1'});assert.equal(reads,2);assert.equal(replies.at(-1).probeId,'probe:probe-client:1');assert.ok(replies.at(-1).snapshot.revision>first);assert.equal(replies.at(-1).snapshot.provenance.pid,123);assert.equal(writes,0);});
await check('a second test has a distinct correlation and does not acknowledge an older request',async()=>{await request({probeId:'probe:probe-client:2'});assert.equal(reads,3);assert.equal(replies.at(-1).probeId,'probe:probe-client:2');assert.equal(writes,0);});
await check('invalid probe identifiers never read state or send a fabricated confirmation',async()=>{for(const probeId of ['',123,'probe:../path:1','x'.repeat(512)]){const before=[reads,replies.length];await request({probeId});assert.deepEqual([reads,replies.length],before);}});
await check('pause is not a test and ends demand',async()=>{const before=reads;await request({subscribe:false,probeId:'probe:probe-client:3'});assert.equal(reads,before);});
relay.dispose();
const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),runtime=fs.readFileSync(path.join(root,'native/decky_sidebar_runtime.h'),'utf8');
const start=main.indexOf('static void deckySidebarInitialize()'),block=main.slice(start,main.indexOf('static bool sofApply',start));
await check('existing Steam restoration is confined to enabled Decky and validates identity/mode',()=>{assert.match(block,/current.pid!=session.pid\|\|current.created!=session.created\|\|current.root!=session.root/);assert.match(block,/if \(state\.value\("known", false\) && state\.value\("running", false\)\) \{/);assert.match(block,/if \(mode == "bigpicture"\) \{[\s\S]*?if \(!state\.contains\("deckyFrontendLoaded"\) \|\| !state\["deckyFrontendLoaded"\]\.is_boolean\(\)\)\s*return \{false, "decky-context-state-pending", true\};[\s\S]*?if \(state\["deckyFrontendLoaded"\]\.get<bool>\(\)\)\s*return \{false, "decky-context-already-loaded", false\};\s*return \{true\};/);assert.match(block,/if \(mode == "desktop"\) return \{false/);assert.match(runtime,/!state_.enabled/);assert.ok(!block.includes('steam-restart-observation-only'));});
await check('watch recovery wakes the owning worker without joining itself or granting Steam startup',()=>{const method=runtime.slice(runtime.indexOf('bool requestTransportRecovery()'),runtime.indexOf('// Transport-only demand from'));assert.ok(!method.includes('lock(lifecycleMutex_)'));assert.ok(method.includes('transportRecoveryRequested_=true'));assert.match(main,/bootstrapStage.*failed.*requestTransportRecovery/);assert.ok(!method.includes('startOwned('));});
await check('startup attachment returns initialization without opening business writes and ready reattaches the same peer',async()=>{
 const host=await createMirrorHostFixture({invoke:async(command)=>command==='deckySidebar.mirrorAttach'?{connected:true,connection:'1',runId,owner:'YMCC-native',pid:123}:true});
 try {const flush=async()=>{for(let i=0;i<200;i++)await Promise.resolve();};await flush();assert.equal(host.counts.attaches,1);assert.equal(host.replies.at(-1).message.snapshot.ready,false);assert.match(host.replies.at(-1).message.snapshot.notice,/正在初始化/);
  for(const key of ['power','schedule','capabilities','saves','speedApply','frameApplies','touchpadSaves','monitorSaves'])assert.equal(host.counts[key],0,key+' stays untouched during initialization');
  host.ready();await flush();assert.equal(host.counts.attaches,2);assert.equal(host.replies.at(-1).message.snapshot.ready,true);assert.deepEqual(host.replies.at(-1).message.snapshot.provenance,{kind:'ymcc-native',pid:123});
 }finally{host.stop();}
});
fs.writeFileSync(path.join(validation,'CONNECTION-PROBE-SELFTEST.json'),JSON.stringify({passed:tests.length,tests,realHardware:false,productionRelay:true},null,2));console.log('Connection probe PASS '+tests.length);
