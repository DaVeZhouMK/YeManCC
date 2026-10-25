// Pure production protocol/relay/extension regression; NO native/Steam/hardware.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const out=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/product-output-20261010');fs.mkdirSync(out,{recursive:true});
const b=await build({stdin:{resolveDir:root,loader:'ts',contents:`export * from './src/bridge/steamHudRelay.ts';export * from './src/bridge/monitorHudProtocol.ts';export * from './src/bridge/monitorHudModel.ts';export * from './src/bridge/deckyMirrorExtensions.ts'`},bundle:true,platform:'node',format:'esm',write:false});
const file=path.join(out,'relay-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);const m=await import('file:///'+file.replaceAll('\\','/')+'?'+Date.now());
const cases=[],check=(name,fn)=>{fn();cases.push(name)},runId='a'.repeat(32),peer={runId,connection:'1',connected:true};
const game={appId:1091500,gameId:'1091500',browserGameId:'1091500',overlayTitle:'SP Overlay: 77/88/3',overlayPid:77,overlayBrowserId:88,ownerBrowserId:99,overlayBrowserType:3,overlayNativeAppId:1091500};
const status={capable:true,game,shown:false,hiddenConfirmed:true,session:null,operation:0,reason:''};
function harness(){let now=10000,id=0,acquired=0,reads=0;const timers=new Map(),frames=[];const relay=m.createSteamHudRelay({now:()=>now,session:()=> 'product-session-00000001',reply:async(connection,frame)=>{if(frame.type==='hud')frames.push(structuredClone(frame));return true},sample:()=>{reads++;return m.hudFromUniformSnapshot({top:{ts:now,hwDown:false,tdpW:27,tempC:63,freqMhz:4300},fps:{ts:now,fps:123},hwDown:false},now)},acquire:()=>{acquired++;return()=>{acquired--}},setTimer:(fn,ms)=>{const k=++id;timers.set(k,{fn,at:now+ms});return k},clearTimer:k=>timers.delete(k)});
return {relay,timers,frames,counts:()=>({acquired,reads}),hello(s=status,p=peer){relay.request({...p,request:{command:'hud.status',runId:p.runId,status:s}})},ack(ok=true,override={}){const f=frames.at(-1);relay.request({...peer,request:{command:'hud.ack',runId,status:{...status,session:f.session,operation:f.operation,shown:f.show,hiddenConfirmed:!f.show,...override},ok}})},advance(ms){const end=now+ms;while(timers.size){const [k,t]=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];if(t.at>end)break;timers.delete(k);now=t.at;t.fn()}now=end},now:()=>now};}
{
 const h=harness();check('No peer => no acquisition/timer, unknown existing native-window state preserved',()=>{assert(!h.relay.available());assert(h.relay.visible());assert.deepEqual(h.counts(),{acquired:0,reads:0});assert.equal(h.timers.size,0)});
 assert.equal((await h.relay.setVisible(false,m.normalizeMonitorOutput())).confirmed,false);h.relay.noteSteamPresence(false);assert.equal((await h.relay.setVisible(false,m.normalizeMonitorOutput())).confirmed,true);h.relay.noteSteamPresence(true);
 assert.equal((await h.relay.setVisible(true,m.normalizeMonitorOutput())).confirmed,false);
 h.relay.connected(peer);h.hello({...status,game:null});check('Steam process/transport alone is not a game-bound overlay capability',()=>assert(!h.relay.available()));
 h.hello();check('Capability needs authenticated peer and an exact validated game identity',()=>assert(h.relay.available()));
 const pending=h.relay.setVisible(true,m.normalizeMonitorOutput());assert.equal(h.frames.length,1);assert(!h.relay.visible());
 check('Show starts one shared source/lease, but sending a packet is NOT a visible ACK',()=>{assert.equal(h.counts().acquired,1);assert.equal(h.frames[0].data.source,'ymcc-uniform');assert.equal(h.timers.size,2)});
 h.ack();assert.equal((await pending).confirmed,true);check('Correct native ready receipt confirms formal output',()=>assert(h.relay.visible()));
 const f=h.frames.at(-1);h.hello({...status,shown:false,hiddenConfirmed:false,session:f.session,operation:f.operation});
 check('Passive not-shown without closed proof cannot clear a visible/uncertain carrier',()=>assert(h.relay.visible()));
 h.advance(5000);check('Transport lease/data tick at 1000ms and one source remains shared',()=>{assert.equal(h.frames.length,6);assert.equal(h.counts().acquired,1);assert.equal(h.timers.size,1)});
 h.relay.updateAppearance(m.normalizeMonitorOutput({output:'rtss',enabled:false,ymcc:{opacity:.4,layout:'bar'}}));h.advance(1000);
 check('Saved output drafts do not silently change active carrier; appearance updates share next tick',()=>{assert.equal(h.frames.at(-1).show,true);assert.equal(h.frames.at(-1).preferences.output,'ymcc');assert.equal(h.frames.at(-1).preferences.enabled,true);assert.equal(h.frames.at(-1).preferences.ymcc.opacity,.4);assert.equal(h.counts().acquired,1)});
 const hide=h.relay.setVisible(false,m.normalizeMonitorOutput());h.ack();assert.equal((await hide).confirmed,true);
 check('Confirmed hide releases source and both operation/transport timers',()=>{assert.equal(h.counts().acquired,0);assert.equal(h.timers.size,0);assert(!h.relay.visible())});h.relay.dispose();
}
{
 const h=harness();h.relay.connected(peer);h.hello();const show=h.relay.setVisible(true,m.normalizeMonitorOutput());
 h.ack(true,{operation:999});check('Wrong/late operation receipt cannot resolve a new operation',()=>assert(h.relay.inspect().pending));
 h.ack(true,{game:{...game,gameId:'42'}});assert.equal((await show).confirmed,false);
 check('Another game receipt cannot confirm the requested game output',()=>assert(!h.relay.inspect().confirmed));h.relay.dispose();
}
{
 const h=harness();h.relay.connected(peer);h.hello();const p=h.relay.setVisible(true,m.normalizeMonitorOutput());h.ack();await p;
 const hide=h.relay.setVisible(false,m.normalizeMonitorOutput());h.ack(true,{shown:false,hiddenConfirmed:false});assert.equal((await hide).confirmed,false);
 check('Hide ACK without actual closed-window proof stays uncertain and prevents overlap',()=>assert(h.relay.visible()));h.relay.dispose();
}
{
 const h=harness();h.relay.connected(peer);h.hello();const p=h.relay.setVisible(true,m.normalizeMonitorOutput());h.advance(12000);assert.equal((await p).confirmed,false);
 check('Show timeout revokes intent and releases source without claiming absence',()=>{assert(h.relay.visible());assert.equal(h.counts().acquired,0);assert.equal(h.timers.size,0);assert.equal(h.frames.at(-1).show,false)});h.relay.dispose();
}
{
 const h=harness();h.relay.connected(peer);h.hello();const p=h.relay.setVisible(true,m.normalizeMonitorOutput());h.ack();await p;h.relay.connected({...peer,connected:false});
 check('Disconnect revokes sample/timers; last visible native child remains uncertain',()=>{assert(!h.relay.available());assert(h.relay.visible());assert.equal(h.counts().acquired,0);assert.equal(h.timers.size,0)});
 assert.equal((await h.relay.setVisible(false,m.normalizeMonitorOutput())).confirmed,false);
 h.relay.connected({...peer,connection:'2'});h.hello(status,{...peer,connection:'2'});
 check('New authenticated peer can prove old children closed before allowing another output',()=>assert(!h.relay.visible()));h.relay.dispose();
}
{
 const h=harness();h.relay.connected(peer);h.hello();const p=h.relay.setVisible(true,m.normalizeMonitorOutput());h.ack();await p;
 h.hello({...status,game:{...game,gameId:'42'},hiddenConfirmed:true});
 check('Game change releases old source instead of continuing to route data across owners',()=>{assert.equal(h.counts().acquired,0);assert(!h.relay.inspect().wanted)});h.relay.dispose();
}
{
 const data=m.hudFromUniformSnapshot(null,10000),frame={type:'hud',protocol:1,runId,session:'product-session-00000001',seq:1,operation:1,show:true,expiresAt:16000,game,preferences:m.normalizeMonitorOutput(),data};
 check('Formal wire accepts only bounded actual-uniform shape with current run/lease/sequence',()=>assert(m.admitHudFrame(frame,runId,10000,0)));
 for(const [name,patch]of [['fixture',{data:m.makeHudFixture()}],['expired',{expiresAt:10000}],['far-future',{expiresAt:18000}],['wrong-run',{runId:'b'.repeat(32)}],['unknown-protocol',{protocol:2}],['wrong-output',{preferences:m.normalizeMonitorOutput({output:'rtss'})}]])check('Reject '+name+' packet',()=>assert.equal(m.admitHudFrame({...frame,...patch},runId,10000,0),null));
 check('Reject duplicate/late data sequence',()=>assert.equal(m.admitHudFrame(frame,runId,10000,1),null));
 const copy=m.admitHudFrame({...frame,data:{...data,coreUsage:Array(1000).fill(20)}},runId,10000,0);assert.equal(copy.data.coreUsage.length,128);
 check('Admission bounds core arrays without a second data collector',()=>assert.equal(copy.data.source,'ymcc-uniform'));
 check('Game binding rejects desktop fallback, wrong title and native identity zeros',()=>{assert(!m.validHudGameBinding({...game,overlayTitle:'Steam 大屏幕模式'}));assert(!m.validHudGameBinding({...game,overlayPid:0}));assert(m.validHudGameBinding(game))});
 const positions=Array.from({length:6},(_,i)=>m.hudPlacement(1920,1080,286,320,i+1));
 check('Six positions retain natural board size with correct top/bottom/center edges',()=>{assert.equal(positions[0].left,12);assert.equal(positions[4].left,817);assert.equal(positions[4].top,12);assert.equal(positions[1].left,1622);assert.equal(positions[2].top,748);assert.equal(positions[3].top,748);assert.equal(positions[5].top,748);assert(positions.every(p=>p.width===286&&p.height===320))});
}
{
 let peers=0,requests=0;m.dispatchDeckyMirrorPeer(peer);const off=m.registerDeckyMirrorExtension({prefix:'hud.',connected:()=>peers++,request:()=>requests++});
 assert.equal(peers,1);assert(m.dispatchDeckyMirrorRequest({...peer,request:{runId,command:'hud.status'}}));assert.equal(requests,1);
 check('HUD extension shares one authenticated transport and cannot enter business mutation relay',()=>{assert(!m.dispatchDeckyMirrorRequest({...peer,request:{runId,command:'fan.setPreset'}}));assert.throws(()=>m.registerDeckyMirrorExtension({prefix:'hud.',connected(){},request(){}}));off();assert(m.dispatchDeckyMirrorRequest({...peer,request:{runId,command:'hud.status'}}))});
}
const report={suite:'Formal YMCC HUD relay / admission / lifecycle',passed:cases.length,cases,hardwareTouched:false,gameValidation:false,scope:'Production pure TS with virtual clock and inert authenticated-peer/sample/transport APIs; no actual HWND or physical mouse test.'};fs.writeFileSync(path.join(out,'PRODUCT-RELAY-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
