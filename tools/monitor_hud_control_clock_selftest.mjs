// Production control deadlines, intentionally NEVER executing browser timers.
// Native calls/events and unified source are inert. No desktop/game/CPU access.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const out=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/desktop-output-20261011/control-clock');fs.mkdirSync(out,{recursive:true});
const b=await build({stdin:{resolveDir:root,loader:'ts',contents:"export * from './src/bridge/monitorHudClock.ts';export * from './src/bridge/steamHudRelay.ts';export * from './src/bridge/desktopHudOwner.ts';export * from './src/bridge/monitorHudCarrierRouter.ts';export * from './src/bridge/monitorHudSampleSource.ts';export * from './src/bridge/monitorHudModel.ts'"},bundle:true,format:'esm',platform:'node',write:false});
const file=path.join(out,'control-clock-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);
const m=await import('file:///'+file.replaceAll('\\','/')+'?'+Date.now());const cases=[];
const check=(name,f)=>{f();cases.push(name)},flush=async()=>{for(let i=0;i<40;i++)await Promise.resolve();};
const deferred=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b});return {promise,resolve,reject};};
function clockHarness(options={}) {
 let now=10000,nextNonce=0,nextTimer=0,pulses=0;const timers=new Map(),calls=[],failures=[];
 const clock=m.createHudClock({now:()=>now,nonce:()=> 'native-control-clock-'+String(++nextNonce).padStart(8,'0'),
  setTimer:(fn,ms)=>{timers.set(++nextTimer,{fn,ms});return nextTimer},clearTimer:id=>timers.delete(id),
  invoke:async(command,args)=>{calls.push([command,{...args}]);if(command==='monitor.hudPulse.set'&&args.enabled){if(options.start)return options.start.promise;return options.failStart!==true;}if(command==='monitor.hudPulse.ack'){if(options.ack)return options.ack.promise;return options.failAck!==true;}return true;},
  pulse:snapshot=>{pulses++;options.pulse?.(snapshot)},failed:reason=>{failures.push(reason);options.failed?.(reason)}});
 return {clock,calls,timers,failures,setAckFailure:value=>options.failAck=value,now:()=>now,pulses:()=>pulses,setNow:n=>now=n,
  async tick(ms=1000){now+=ms;const nonce=clock.inspect().nonce;const accepted=clock.accept({nonce,snapshot:null});await flush();return accepted;}};
}
{
 const h=clockHarness(),a=h.clock.acquire(),b=h.clock.acquire();await flush();
 check('Sample and pending-control leases share ONE native clock enable',()=>{assert.equal(h.clock.inspect().users,2);assert.equal(h.calls.filter(([c,a])=>a.enabled===true).length,1)});
 a();a();check('First/idempotent release cannot disable the other clock consumer',()=>{assert.equal(h.clock.inspect().users,1);assert(!h.calls.some(([c,a])=>a.enabled===false))});
 await h.tick();b();check('Last lease clears native clock, ACK/fallback state and rejects old ticks',()=>{assert.equal(h.clock.inspect().users,0);assert.equal(h.timers.size,0);assert.equal(h.calls.filter(([c,a])=>a.enabled===false).length,1);assert(!h.clock.accept({nonce:'native-control-clock-00000001',snapshot:null}))});
 h.clock.dispose();assert.throws(()=>h.clock.acquire());
}
{
 const start=deferred(),h=clockHarness({start}),release=h.clock.acquire();await flush();await h.tick();
 check('Nonce-bound tick proves a running clock when enable receipt is missing',()=>{assert(h.clock.inspect().enabled);assert.equal(h.failures.length,0)});
 release();start.resolve(true);await flush();check('Late enable receipt for released clock is explicitly revoked, not adopted',()=>{assert.equal(h.clock.inspect().users,0);assert(h.calls.filter(([c,a])=>a.enabled===false).length>=2);assert.equal(h.timers.size,0)});
}
{
 const start=deferred(),h=clockHarness({start}),old=h.clock.acquire(),oldNonce=h.clock.inspect().nonce;old();const live=h.clock.acquire(),liveNonce=h.clock.inspect().nonce;
 start.resolve(true);await flush();check('Old-generation enable/disable receipts cannot replace the new nonce',()=>{assert.notEqual(oldNonce,liveNonce);assert.equal(h.clock.inspect().nonce,liveNonce);assert(!h.clock.accept({nonce:oldNonce,snapshot:null}))});live();
}
{
 const h=clockHarness({failStart:true}),release=h.clock.acquire();await flush();
 check('Native enable failure fails once and cannot silently join a failed generation',()=>{assert.equal(h.failures.length,1);assert(h.clock.inspect().failed);assert.throws(()=>h.clock.acquire());assert.equal(h.timers.size,0)});release();
}
{
 const start=deferred(),h=clockHarness({start}),release=h.clock.acquire();await flush();assert(!await h.tick(3000));
 check('First native tick cannot revive startup beyond its deadline with browser timers suppressed',()=>{assert(h.clock.inspect().failed);assert.equal(h.failures.length,1)});release();start.resolve(true);await flush();
}
{
 const ack=deferred(),h=clockHarness({ack}),release=h.clock.acquire();await flush();await h.tick();await h.tick();await h.tick();assert(!await h.tick());
 check('Missing native ACK expires from native events, with at most one outstanding ACK',()=>{assert.equal(h.calls.filter(([c])=>c==='monitor.hudPulse.ack').length,1);assert.equal(h.failures.length,1);assert(h.clock.inspect().failed);assert.equal(h.timers.size,0)});
 ack.resolve(true);await flush();check('Late ACK cannot revive a failed clock',()=>assert(h.clock.inspect().failed));release();
}
{
 const h=clockHarness({failAck:true}),release=h.clock.acquire();await flush();await h.tick();check('Negative renewal reply fails the clock without requiring a browser timer',()=>assert.equal(h.failures.length,1));release();
}
const game={appId:1091500,gameId:'1091500',browserGameId:'1091500',overlayTitle:'SP Overlay: 77/88/3',overlayPid:77,overlayBrowserId:88,ownerBrowserId:99,overlayBrowserType:3,overlayNativeAppId:1091500};
const peer={connection:'1',runId:'a'.repeat(32),connected:true};
function relayHarness(options={}) {
 let relay,demands=0,reads=0;const frames=[];
 const h=clockHarness({pulse:()=>relay.advance(),failed:reason=>relay.clockUnavailable(reason),...options});
 const source=m.createHudSampleSource({now:h.now,subscribeSnapshot:()=>()=>{},subscribeTdp:()=>()=>{},readOnce:async()=>{reads++;return null},acquire:()=>{demands++;const release=h.clock.acquire();return()=>{demands--;release()}}});
 relay=m.createSteamHudRelay({externalClock:true,acquireClock:h.clock.acquire,now:h.now,session:()=> 'native-control-steam-000001',sample:source.sample,acquire:source.acquire,
  reply:async(c,f)=>{if(f.type==='hud')frames.push(f);return true},setTimer:(fn,ms)=>{const id=Symbol();h.timers.set(id,{fn,ms});return id},clearTimer:id=>h.timers.delete(id)});
 relay.connected(peer);relay.request({...peer,request:{command:'hud.status',runId:peer.runId,status:{capable:true,game,shown:options.previouslyShown===true,hiddenConfirmed:options.previouslyShown!==true,session:null,operation:0,reason:''}}});
 const ack=(frame,shown,hiddenConfirmed=!shown)=>relay.request({...peer,request:{command:'hud.ack',runId:peer.runId,ok:true,status:{capable:true,game,shown,hiddenConfirmed,session:frame.session,operation:frame.operation,reason:''}}});
 return {...h,relay,frames,ack,demands:()=>demands,reads:()=>reads};
}
{
 const h=relayHarness({previouslyShown:true}),p=h.relay.setVisible(false,m.normalizeMonitorOutput());await flush();
 check('Hide-only Steam operation owns a native clock WITHOUT a sample/hardware lease',()=>{assert.equal(h.demands(),0);assert.equal(h.reads(),0);assert.equal(h.clock.inspect().users,1)});
 for(let i=0;i<12;i++)await h.tick();assert(!(await p).confirmed);
 check('Steam hide deadline expires with every browser callback suppressed and retains unknown state',()=>{assert(!h.relay.inspect().pending);assert.equal(h.relay.state(),'unknown');assert.equal(h.clock.inspect().users,0);assert.equal(h.timers.size,0);assert(h.frames.every(f=>!f.show))});h.relay.dispose();
}
{
 const h=relayHarness(),p=h.relay.setVisible(true,m.normalizeMonitorOutput());await flush();const first=h.frames[0];
 check('Show control and data share native tick; no second unified sensor demand is created',()=>{assert.equal(h.clock.inspect().users,2);assert.equal(h.demands(),1);assert.equal(h.reads(),1);assert.equal(h.calls.filter(([c,a])=>a.enabled===true).length,1)});
 for(let i=0;i<12;i++)await h.tick();assert(!(await p).confirmed);h.ack(first,true);
 check('Missing Steam show ACK revokes intent/data; late show ACK cannot claim visibility',()=>{assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,0);assert(!h.relay.inspect().confirmed);assert.equal(h.relay.state(),'unknown');assert.equal(h.frames.at(-1).show,false);assert.equal(h.timers.size,0)});h.relay.dispose();
}
{
 const h=relayHarness(),p=h.relay.setVisible(true,m.normalizeMonitorOutput());await flush();h.ack(h.frames[0],true);assert((await p).confirmed);
 check('Successful show releases only control clock while the single data lease keeps ticking',()=>{assert.equal(h.clock.inspect().users,1);assert.equal(h.demands(),1);assert.equal(h.timers.size,0)});
 const hiding=h.relay.setVisible(false,m.normalizeMonitorOutput());await flush();h.ack(h.frames.at(-1),false);assert((await hiding).confirmed);
 check('Confirmed Steam hide cleans the last clock and sample consumer',()=>{assert.equal(h.clock.inspect().users,0);assert.equal(h.demands(),0);assert.equal(h.timers.size,0)});h.relay.dispose();
}
{
 const h=relayHarness({previouslyShown:true});let desktopShows=0;const desktop={available:()=>true,visible:()=>false,setVisible:async show=>{if(show)desktopShows++;return {confirmed:true}}};
 const router=m.createHudCarrierRouter(h.relay,desktop),p=router.setVisible(true,m.normalizeMonitorOutput());await flush();for(let i=0;i<12;i++)await h.tick();assert(!(await p).confirmed);
 check('Unconfirmed Steam hide deadline cannot enable another carrier',()=>assert.equal(desktopShows,0));h.relay.dispose();
}
{
 const h=relayHarness({failStart:true}),p=h.relay.setVisible(true,m.normalizeMonitorOutput());await flush();assert(!(await p).confirmed);
 check('Clock enable failure fails a show promptly and releases data/control leases',()=>{assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,0);assert.equal(h.timers.size,0);assert(!h.relay.inspect().pending)});h.relay.dispose();
}
{
 const h=relayHarness(),p=h.relay.setVisible(true,m.normalizeMonitorOutput());await flush();h.relay.connected({...peer,connected:false});assert(!(await p).confirmed);
 check('Disconnect frees both pending control and data clock references',()=>{assert.equal(h.clock.inspect().users,0);assert.equal(h.demands(),0);assert.equal(h.timers.size,0)});h.relay.dispose();
}
function desktopHarness(options={}) {
 let owner,listener,demands=0;const nativeCalls=[];
 const h=clockHarness({pulse:()=>owner.advance(),failed:reason=>owner.clockUnavailable(reason),failStart:options.failStart});
 owner=m.createDesktopHudOwner({externalClock:true,acquireClock:h.clock.acquire,now:h.now,target:()=>({pid:100,processCreated:'123456789'}),nonce:()=> 'native-control-desktop-000001',sample:()=>m.hudFromUniformSnapshot(null,h.now()),acquire:()=>{demands++;const release=h.clock.acquire();return()=>{demands--;release()};},
  changed:()=>options.changed?.(owner),observe:cb=>{listener=cb;return()=>{listener=null}},setTimer:(fn,ms)=>{const id=Symbol();h.timers.set(id,{fn,ms});return id},clearTimer:id=>h.timers.delete(id),
  invoke:async(command,args)=>{nativeCalls.push([command,args]);if(command==='desktopHud.status')return options.status?.promise??{available:true,id:0};if(command==='desktopHud.create'){if(options.create)return options.create.promise;if(options.ready)listener('desktopHud.ready',{id:7,nonce:args.nonce});return {id:7,nonce:args.nonce};}if(command==='desktopHud.frame'){if(options.painted)queueMicrotask(()=>listener?.('desktopHud.painted',{id:7,nonce:args.nonce,seq:args.frame.seq}));return true;}if(command==='desktopHud.close')return options.close?.promise??true;throw Error('Unexpected '+command);}});
 return {...h,owner,nativeCalls,demands:()=>demands,send:(name,args={})=>listener?.(name,{id:7,nonce:'native-control-desktop-000001',...args})};
}
{
 const h=desktopHarness();assert(await h.owner.available());const p=h.owner.setVisible(true,m.normalizeMonitorOutput());await flush();
 check('Desktop create/ready handshake holds clock before it can acquire monitoring data',()=>{assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,1);assert(h.owner.inspect().opening)});
 for(let i=0;i<12;i++)await h.tick();assert(!(await p).confirmed);
 check('Missing desktop ready expires and closes exact native object with no browser timer',()=>{assert(h.nativeCalls.some(([c,a])=>c==='desktopHud.close'&&a.hudId===7));assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,0);assert.equal(h.timers.size,0);assert.equal(h.owner.state(),'hidden')});await h.owner.dispose();
}
{
 const create=deferred(),h=desktopHarness({create});assert(await h.owner.available());const p=h.owner.setVisible(true,m.normalizeMonitorOutput());await flush();for(let i=0;i<5;i++)await h.tick();assert(!(await p).confirmed);
 check('Missing native create RPC expires separately and retains in-flight creation uncertainty',()=>{assert(h.owner.inspect().creating);assert.equal(h.owner.state(),'unknown');assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,0)});
 create.resolve({id:23,nonce:'native-control-desktop-000001'});await flush();
 check('Late create receipt after native-clock timeout is reclaimed by exact id/nonce',()=>{assert(h.nativeCalls.some(([c,a])=>c==='desktopHud.close'&&a.hudId===23&&a.nonce==='native-control-desktop-000001'));assert.equal(h.owner.state(),'hidden');assert(!h.owner.inspect().creating);assert.equal(h.timers.size,0)});await h.owner.dispose();
}
{
 const close=deferred(),h=desktopHarness({ready:true,painted:true,close});assert(await h.owner.available());assert((await h.owner.setVisible(true,m.normalizeMonitorOutput())).confirmed);
 const p=h.owner.setVisible(false,m.normalizeMonitorOutput());await flush();const framesBeforeClose=h.nativeCalls.filter(([c])=>c==='desktopHud.frame').length;for(let i=0;i<5;i++)await h.tick();assert(!(await p).confirmed);h.send('desktopHud.painted',{seq:1});
 check('A closing desktop cannot renew frames or accept a late painted event',()=>{assert.equal(h.nativeCalls.filter(([c])=>c==='desktopHud.frame').length,framesBeforeClose);assert.equal(h.owner.state(),'unknown')});
 check('Missing desktop close RPC expires without allowing false hidden/other output',()=>{assert.equal(h.owner.state(),'unknown');assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,0);assert.equal(h.timers.size,0)});
 close.resolve(true);await flush();check('Late exact close proof can safely clear prior uncertainty',()=>assert.equal(h.owner.state(),'hidden'));await h.owner.dispose();
}
{
 const status=deferred(),h=desktopHarness({status}),p=h.owner.available();await flush();for(let i=0;i<5;i++)await h.tick();assert.equal(await p,false);
 check('Missing native availability RPC also has a native-driven deadline without sensor acquisition',()=>{assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,0);assert.equal(h.owner.state(),'unknown');assert.equal(h.timers.size,0)});status.resolve({available:true,id:0});await flush();await h.owner.dispose();
}
{
 const h=desktopHarness({ready:true,painted:true});assert(await h.owner.available());assert((await h.owner.setVisible(true,m.normalizeMonitorOutput())).confirmed);
 for(let i=0;i<5;i++)await h.tick();
 check('Steady desktop frame ACKs do not multiply native clocks or leave control timers behind',()=>{assert.equal(h.demands(),1);assert.equal(h.clock.inspect().users,1);assert.equal(h.timers.size,0);assert.equal(h.owner.inspect().pendingCalls,0)});await h.owner.dispose();
 check('Owner disposal frees the final data/control clock references',()=>{assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,0);assert.equal(h.timers.size,0)});
}
{
 let invalidations=0;const h=desktopHarness({failStart:true,changed:owner=>{invalidations++;void owner.available();}});
 assert.equal(await h.owner.available(),false);await flush();
 check('Unavailable native clock does not self-invalidate availability into a retry/CPU loop',()=>{assert.equal(invalidations,0);assert.equal(h.calls.filter(([c,a])=>a.enabled===true).length,1);assert.equal(h.clock.inspect().users,0);assert.equal(h.timers.size,0)});await h.owner.dispose();
}
{
 const h=desktopHarness({ready:true,painted:true});assert(await h.owner.available());assert((await h.owner.setVisible(true,m.normalizeMonitorOutput())).confirmed);
 h.setAckFailure(true);await h.tick();
 check('Native clock renewal failure revokes steady desktop data and reclaims its exact window',()=>{assert.equal(h.demands(),0);assert.equal(h.clock.inspect().users,0);assert.equal(h.timers.size,0);assert.equal(h.owner.state(),'hidden');assert(h.nativeCalls.some(([c,a])=>c==='desktopHud.close'&&a.hudId===7))});await h.owner.dispose();
}
const host=fs.readFileSync('src/bridge/monitorHudHost.ts','utf8');
check('Host separates hardware demand from shared clock and retains late desktop IPC replies',()=>{assert(host.includes('acquireClock:clock.acquire'));assert(host.includes('source.users()>0'));assert(!host.includes('clockNonce'));assert(!/invoke:\(cmd,args\)=>invoke\(cmd,args as Record<string,unknown>,\{timeoutMs:5000\}\)/.test(host))});
const report={passed:cases.length,cases,browserTimerCallbacksExecuted:0,cpuMeasured:false,hardwareTouched:false,actualNativeWindow:false,gameValidation:false,scope:'Production refcount clock + relay/desktop/router/sample functions with inert IPC and native pulses. Does not prove real minimized main-WebView, game mouse passthrough, or installation.'};
fs.writeFileSync(path.join(out,'CONTROL-CLOCK-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
