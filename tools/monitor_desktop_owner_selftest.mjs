// Production desktop owner/router/source/protocol/coordinator. Inert native API
// and virtual timers, NOT WebView/native pixels/physical mouse acceptance.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),out=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/desktop-output-20261011');fs.mkdirSync(out,{recursive:true});
const b=await build({stdin:{resolveDir:root,loader:'ts',contents:`export * from './src/bridge/desktopHudOwner.ts';export * from './src/bridge/monitorHudCarrierRouter.ts';export * from './src/bridge/monitorHudSampleSource.ts';export * from './src/bridge/desktopHudProtocol.ts';export * from './src/bridge/monitorOutputCoordinator.ts';export * from './src/bridge/monitorHudModel.ts'`},bundle:true,format:'esm',platform:'node',write:false});
const file=path.join(out,'desktop-owner-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);const m=await import('file:///'+file.replaceAll('\\','/')+'?'+Date.now());const cases=[];
const check=(n,f)=>{f();cases.push(n)},flush=async()=>{for(let i=0;i<20;i++)await Promise.resolve();};
function harness(options={}){let now=10000,id=0,listener=null,users=0;const timers=new Map(),calls=[],packets=[];const target={pid:100,processCreated:'123456789'};
const owner=m.createDesktopHudOwner({target:()=>target,nonce:()=> 'desktop-session-000001',now:()=>now,sample:()=>m.hudFromUniformSnapshot(null,now),acquire:()=>{users++;return()=>{users--}},invoke:async(cmd,args)=>{calls.push([cmd,structuredClone(args)]);if(cmd==='desktopHud.status')return {available:true,id:0};if(cmd==='desktopHud.create'){if(options.delayedCreate)return options.delayedCreate;if(options.earlyReady)listener('desktopHud.ready',{id:7,nonce:args.nonce});return {id:7,nonce:args.nonce}}if(cmd==='desktopHud.frame'){packets.push(args.frame);if(options.autoPaint)queueMicrotask(()=>listener('desktopHud.painted',{id:7,nonce:args.nonce,seq:args.frame.seq}));return options.rejectFrame!==true}if(cmd==='desktopHud.close'){if(options.failClose)throw Error('lost close');return true}throw Error('Unexpected '+cmd)},observe:cb=>{listener=cb;return()=>{listener=null}},setTimer:(fn,ms)=>{timers.set(++id,{fn,at:now+ms});return id},clearTimer:k=>timers.delete(k)});
return {owner,calls,packets,timers,users:()=>users,send:(name,value={})=>listener?.(name,{id:7,nonce:'desktop-session-000001',...value}),advance(ms){const end=now+ms;while(timers.size){const [k,t]=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];if(t.at>end)break;now=t.at;timers.delete(k);t.fn()}now=end}};}
{
 const h=harness({earlyReady:true,autoPaint:true});assert(await h.owner.available());const p=h.owner.setVisible(true,m.normalizeMonitorOutput());await flush();assert.equal((await p).confirmed,true);
 check('Early renderer-ready before create receipt is reconciled without lost handshake',()=>{assert.equal(h.users(),1);assert.equal(h.packets.length,1);assert(h.owner.visible());assert.equal(h.packets[0].data.source,'ymcc-uniform')});
 h.owner.updateAppearance(m.normalizeMonitorOutput({output:'rtss',enabled:false,ymcc:{opacity:.35}}));h.advance(1000);await flush();
 check('Desktop appearance uses the shared next 1Hz frame without independent hardware reads or output-draft changes',()=>{assert.equal(h.packets.length,2);assert.equal(h.packets[1].preferences.output,'ymcc');assert(h.packets[1].preferences.enabled);assert.equal(h.packets[1].preferences.ymcc.opacity,.35)});
 h.send('desktopHud.ready');await flush();check('Duplicate ready event cannot allocate another data lease or tick',()=>{assert.equal(h.users(),1);assert.equal(h.packets.length,2);assert.equal(h.timers.size,1)});
 const closed=await h.owner.setVisible(false,m.normalizeMonitorOutput());check('Synchronous confirmed native close releases only owned carrier, sample demand and timer',()=>{assert(closed.confirmed);assert.equal(h.users(),0);assert.equal(h.timers.size,0);assert(!h.owner.visible())});await h.owner.dispose();
}
{
 const h=harness();await h.owner.available();const p=h.owner.setVisible(true,m.normalizeMonitorOutput());await flush();h.send('desktopHud.painted',{nonce:'wrong-nonce',seq:1});h.send('desktopHud.ready',{id:99});
 check('Another HWND nonce/id cannot acknowledge or retarget the owner',()=>{assert(h.owner.inspect().opening);assert.equal(h.users(),0)});
 h.send('desktopHud.ready');await flush();h.send('desktopHud.painted',{seq:1});assert((await p).confirmed);
 h.send('desktopHud.expired');await flush();check('Native lease expiration revokes the display and releases source even while parent is otherwise healthy',()=>{assert(!h.owner.visible());assert.equal(h.users(),0);assert.equal(h.timers.size,0)});await h.owner.dispose();
}
{
 const h=harness({earlyReady:true,autoPaint:true,failClose:true});await h.owner.available();assert((await h.owner.setVisible(true,m.normalizeMonitorOutput())).confirmed);assert(!(await h.owner.setVisible(false,m.normalizeMonitorOutput())).confirmed);
 check('Lost native close ACK retains uncertainty instead of enabling another carrier',()=>assert(h.owner.visible()));await h.owner.dispose();
}
{
 let resolve;const h=harness({delayedCreate:new Promise(r=>resolve=r)});await h.owner.available();const p=h.owner.setVisible(true,m.normalizeMonitorOutput());await flush();const disposing=h.owner.dispose();resolve({id:17,nonce:'desktop-session-000001'});await flush();await disposing;assert(!(await p).confirmed);
 check('Late HWND creation after disposal is closed by exact returned id/nonce',()=>{assert(h.calls.some(([c,a])=>c==='desktopHud.close'&&a.hudId===17));assert.equal(h.users(),0)});
}
{
 const h=harness();await h.owner.available();const p=h.owner.setVisible(true,m.normalizeMonitorOutput());await flush();h.advance(12000);await flush();assert(!(await p).confirmed);
 check('Creation timeout closes own native object, does not collect data indefinitely',()=>{assert.equal(h.users(),0);assert.equal(h.timers.size,0);assert(h.calls.some(([c])=>c==='desktopHud.close'))});await h.owner.dispose();
}
{
 let samples=0,tdps=0,demands=0,reads=0,cb=null,tcb=null;const source=m.createHudSampleSource({subscribeSnapshot:fn=>{samples++;cb=fn;return()=>samples--},subscribeTdp:fn=>{tdps++;tcb=fn;return()=>tdps--},readOnce:async()=>{reads++;return null},acquire:()=>{demands++;return()=>demands--},now:()=>10000});
 const a=source.acquire(),b=source.acquire();await flush();
 check('Steam and desktop source consumers share one uniform read, one sensor demand and one TDP binding',()=>assert.deepEqual([samples,tdps,demands,reads,source.users()],[1,1,1,1,2]));
 cb({top:{ts:9900,hwDown:false,tdpW:28.4},fps:{ts:9900,fps:123},hwDown:false});tcb({mode:'dc',status:'applied',watts:18,proof:'write-ack',revision:1,confirmedAt:9000});
 check('Shared source retains real AC/DC applied TDP distinct from package power',()=>{assert.equal(source.sample().values.tdpLimit,18);assert.equal(source.sample().values.cpuPower,28.4)});
 a();a();assert.equal(demands,1);b();check('Last display cleanup frees shared demand and excludes stale source values',()=>{assert.deepEqual([samples,tdps,demands,source.users()],[0,0,0,0]);assert.equal(source.sample().values.tdpLimit,null)});
}
{
 const calls=[],state={steam:true,desktop:true,shown:'steam',deny:false};const port=name=>({available:async()=>state[name],visible:()=>state.shown===name,setVisible:async(show)=>{calls.push([name,show]);if(!show&&name==='steam'&&state.deny)return {confirmed:false};state.shown=show?name:null;return {confirmed:true}}});
 const router=m.createHudCarrierRouter(port('steam'),port('desktop'));
 assert((await router.setVisible(true,m.normalizeMonitorOutput())).confirmed);check('Steam ready is preferred and all old carriers close before native show',()=>assert.deepEqual(calls,[['steam',false],['desktop',false],['steam',true]]));
 state.steam=false;calls.length=0;assert((await router.setVisible(true,m.normalizeMonitorOutput())).confirmed);check('Unavailable Steam game binding falls back to desktop with the same exclusive output',()=>assert.deepEqual(calls,[['steam',false],['desktop',false],['desktop',true]]));
 state.deny=true;state.steam=true;calls.length=0;assert(!(await router.setVisible(true,m.normalizeMonitorOutput())).confirmed);check('Unconfirmed old Steam closure prevents both Steam and desktop show',()=>assert.deepEqual(calls,[['steam',false]]));
}
{
 const calls=[];const coordinator=m.createMonitorOutputCoordinator({capabilities:async()=>({steam:false,steamAbsent:true,ymcc:true,ymccOwned:true}),ensureRtss:async()=>calls.push('backend'),setSteam:async()=>{throw Error('Absent Steam must not be written')},setRtss:async()=>{calls.push('rtss-hide');return {confirmed:true}},setYmcc:async show=>{calls.push(show?'ymcc-show':'ymcc-hide');return {confirmed:true}}});
 assert((await coordinator.apply(m.normalizeMonitorOutput())).ok);check('Confirmed absent Steam no longer blocks standalone YMCC / never writes an absent Steam process',()=>assert.deepEqual(calls,['backend','rtss-hide','ymcc-hide','ymcc-show']));
 assert(!(await coordinator.apply(m.normalizeMonitorOutput({output:'steam'}))).ok);check('Native Steam output cannot be enabled when process is confirmed absent',()=>assert.equal(calls.length,4));
 const fail=m.createMonitorOutputCoordinator({capabilities:async()=>({steam:true,ymcc:false,ymccOwned:true}),ensureRtss:async()=>{},setSteam:async()=>({confirmed:true}),setRtss:async()=>({confirmed:true}),setYmcc:async()=>({confirmed:false})});
 assert(!(await fail.apply(m.normalizeMonitorOutput({output:'rtss'}))).ok);cases.push('Disconnected/not-ready owned YMCC must still confirm hidden before RTSS may show');
}
{
 const frame={type:'desktop-hud',protocol:1,hudId:7,nonce:'desktop-session-000001',seq:1,expiresAt:16000,show:true,preferences:m.normalizeMonitorOutput(),data:m.hudFromUniformSnapshot(null,10000)};
 check('Desktop renderer admits current native identity/lease and rejects fixture/old/source-spoof packets',()=>{assert(m.admitDesktopHudFrame(frame,7,frame.nonce,10000,0));for(const patch of [{data:m.makeHudFixture()},{seq:0},{nonce:'other'},{expiresAt:10000},{protocol:2}])assert.equal(m.admitDesktopHudFrame({...frame,...patch},7,frame.nonce,10000,0),null)});
}

{
 let listener,resolve,reads=0;const pending=new Promise(r=>resolve=r);
 const source=m.createHudSampleSource({subscribeSnapshot:cb=>{listener=cb;return()=>{}},subscribeTdp:()=>()=>{},readOnce:()=>{reads++;return pending},acquire:()=>()=>{},now:()=>10000});
 const stop=source.acquire();listener({top:{ts:9900,hwDown:false,tdpW:19},fps:{ts:9900,pid:2077,processCreated:'123456789',fps:120},hwDown:false});resolve(null);await flush();
 check('Late one-shot source read cannot overwrite a newer passive uniform event',()=>{assert.equal(source.sample().values.fps,120);assert.equal(reads,1)});stop();
}
{
 let closed=null;const deps={target:()=>null,nonce:()=> 'recovery-session-00000001',now:()=>10000,sample:()=>m.hudFromUniformSnapshot(null),acquire:()=>()=>{},observe:()=>()=>{},setTimer:()=>0,clearTimer:()=>{},invoke:async(cmd,args)=>{if(cmd==='desktopHud.status')return {available:true,id:41,nonce:'previous-main-source-00001'};if(cmd==='desktopHud.close'){closed=args;return true}throw Error('Unexpected')}};
 const owner=m.createDesktopHudOwner(deps);assert(!(await owner.available()));check('Reloaded owner reports pre-existing native HUD as uncertain, not absent',()=>assert(owner.visible()));
 assert((await owner.setVisible(false,m.normalizeMonitorOutput())).confirmed);check('Reloaded main source closes exact native descriptor before another route can open',()=>assert.deepEqual(closed,{hudId:41,nonce:'previous-main-source-00001'}));await owner.dispose();
}
{
 const port={available:async()=>true,visible:()=>true,state:()=> 'unknown',setVisible:async()=>({confirmed:false})};const hidden={available:async()=>false,visible:()=>false,state:()=> 'hidden',setVisible:async()=>({confirmed:true})};
 const router=m.createHudCarrierRouter(port,hidden);check('Potential visibility is not misreported as confirmed YMCC output in UI state',()=>assert.equal(router.state(),'unknown'));
}
const report={passed:cases.length,cases,physicalMousePassVerified:false,actualNativeWindow:false,scope:'Exact production desktop owner, dual carrier routing, shared sample source and data admission with inert native/IO/timers. Not native/game/CPU acceptance.'};fs.writeFileSync(path.join(out,'DESKTOP-OWNER-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
