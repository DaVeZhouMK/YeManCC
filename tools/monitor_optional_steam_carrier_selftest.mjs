// Transactional optional routing tests. Synthetic acknowledgements, no app/game IO.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_ROUTING_OUT||path.join(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/global-followup-20261011/tests'));fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const b=await build({stdin:{contents:"export * from './src/bridge/monitorHudCarrierRouter.ts';export * from './src/bridge/monitorOutputCoordinator.ts';export * from './src/bridge/monitorHudModel.ts';",resolveDir:root,loader:'ts'},bundle:true,platform:'node',format:'esm',write:false});
const file=path.join(out,'optional-carrier-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);const m=await import('file:///'+file.replaceAll('\\','/'));
const prefs=m.normalizeMonitorOutput(),optional=m.normalizeMonitorOutput({ymccCarrier:'steam-preferred'}),cases=[];
const check=(name,fn)=>{fn();cases.push(name)};
function harness(options={}){
 const calls=[],states={steam:options.steamState??'hidden',desktop:options.desktopState??'hidden'};let available=options.steamAvailable??true;
 const ports={steam:{available:async()=>{calls.push('probe-steam');if(options.throwSteamProbe)throw Error('Steam probe unavailable');return available},visible:()=>states.steam==='shown',state:()=>states.steam,async setVisible(show,p){calls.push('steam:'+show);if(show&&options.failSteamShow){states.steam='unknown';return {confirmed:false,reason:'create failed'}}if(!show&&options.failSteamClose){states.steam='unknown';return {confirmed:false,reason:'close unknown'}}states.steam=show?'shown':'hidden';if(!show&&options.bindingLost)available=false;return {confirmed:true}}},desktop:{available:async()=>{calls.push('probe-desktop');return options.desktopAvailable!==false},visible:()=>states.desktop==='shown',state:()=>states.desktop,async setVisible(show,p){calls.push('desktop:'+show);if(show&&options.failDesktopShow)return {confirmed:false,reason:'desktop paint failed'};states.desktop=show?'shown':'hidden';return {confirmed:true}}}};
 return {calls,states,router:m.createHudCarrierRouter(ports.steam,ports.desktop,'desktop-global'),ports,setAvailable(v){available=v}};
}
check('Old/invalid preferences default to the desktop; optional Steam remains internal while two public choices are shown',()=>{assert.equal(prefs.ymccCarrier,'desktop');assert.equal(m.normalizeMonitorOutput({ymccCarrier:'bad'}).ymccCarrier,'desktop');assert.equal(optional.ymccCarrier,'steam-preferred');assert.deepEqual(m.MONITOR_OUTPUTS.map(v=>v.value),['desktop','rtss'])});
{
 const h=harness({throwSteamProbe:true,steamState:'unknown'});assert.equal(await h.router.kind(prefs),'desktop');assert((await h.router.setVisible(true,prefs)).confirmed);
 check('Default global route never probes, reads state or writes an unknown Steam carrier',()=>{assert(h.calls.every(s=>!s.includes('steam')));assert.equal(h.router.active(),'desktop');assert.equal(h.router.state(),'shown')});
}
for(const setup of [{steamAvailable:false,steamState:'unknown'},{throwSteamProbe:true},{steamState:'unknown'}]){
 const h=harness(setup);assert.equal(await h.router.kind(optional),'desktop');assert((await h.router.setVisible(true,optional)).confirmed);
 check('Explicit preference falls back without needing an absent/failing/unconfirmed Steam popup: '+JSON.stringify(setup),()=>{assert(!h.calls.includes('steam:false'));assert(!h.calls.includes('steam:true'));assert.equal(h.router.active(),'desktop')});
}
{
 const h=harness({desktopState:'shown'});assert.equal(await h.router.kind(optional),'steam');assert((await h.router.setVisible(true,optional)).confirmed);
 check('Ready opt-in Steam closes previous carriers before show; one confirmed final display',()=>{assert.deepEqual(h.calls.filter(s=>s.includes(':')),['steam:false','desktop:false','steam:true']);assert.deepEqual(h.states,{steam:'shown',desktop:'hidden'});assert.equal(h.router.active(),'steam')});
 assert((await h.router.setVisible(true,prefs)).confirmed);
 check('Switch back to default desktop reclaims owned Steam before displaying desktop',()=>{assert.deepEqual(h.calls.filter(s=>s.includes(':')).slice(-3),['steam:false','desktop:false','desktop:true']);assert.deepEqual(h.states,{steam:'hidden',desktop:'shown'})});
}
{
 const h=harness({bindingLost:true,desktopState:'shown'});assert((await h.router.setVisible(true,optional)).confirmed);
 check('Binding lost during migration opens desktop without requiring a new game identity',()=>{assert(!h.calls.includes('steam:true'));assert.equal(h.router.active(),'desktop');assert.equal(h.states.desktop,'shown')});
}
{
 const h=harness({failSteamShow:true});const ack=await h.router.setVisible(true,optional);assert(ack.confirmed);
 check('Failed Steam create is reclaimed with real hide ACK before restoring global desktop',()=>{assert.deepEqual(h.calls.filter(s=>s.includes(':')),['steam:false','desktop:false','steam:true','steam:false','desktop:true']);assert(ack.reason.includes('恢复全局桌面'));assert.deepEqual(h.states,{steam:'hidden',desktop:'shown'})});
 const coordinator=m.createMonitorOutputCoordinator({capabilities:async()=>({steam:true,ymcc:true,ymccOwned:true}),ensureRtss:async()=>{},setSteam:async()=>({confirmed:true}),setRtss:async()=>({confirmed:true}),setYmcc:(show,p)=>h.router.setVisible(show,p)});
 const result=await coordinator.apply(optional);check('User-visible coordinator result retains fallback reason and actual confirmed display',()=>{assert(result.ok);assert.equal(result.applied,'ymcc');assert(result.reason.includes('恢复全局桌面'))});
}
{
 const h=harness({failSteamClose:true,desktopState:'shown'});const ack=await h.router.setVisible(true,optional);
 check('Unknown old Steam close does not close the currently visible desktop or open another popup',()=>{assert(!ack.confirmed);assert(!h.calls.includes('desktop:false'));assert(!h.calls.includes('steam:true'));assert.equal(h.states.desktop,'shown');assert.equal(h.router.state(),'unknown')});
}
{
 const h=harness({failSteamShow:true});let closeCount=0;const original=h.ports.steam.setVisible;h.ports.steam.setVisible=async(show,p)=>{if(!show&&++closeCount===2){h.calls.push('steam:false');h.states.steam='unknown';return {confirmed:false,reason:'close ACK lost'}}return original(show,p)};
 const ack=await h.router.setVisible(true,optional);
 check('Lost reclaim ACK after create failure cannot be promoted into safe desktop fallback',()=>{assert(!ack.confirmed);assert(!h.calls.includes('desktop:true'));assert.equal(h.router.state(),'unknown');assert(ack.reason.includes('未叠开桌面'))});
}
{
 const h=harness({failSteamShow:true,failDesktopShow:true});const ack=await h.router.setVisible(true,optional);
 check('Failed desktop paint after Steam failure is not reported as successful output',()=>{assert(!ack.confirmed);assert.equal(ack.reason,'desktop paint failed');assert.equal(h.router.active(),null)});
}
{
 const h=harness();const draft=structuredClone(optional),one=h.router.setVisible(true,draft);draft.ymccCarrier='desktop';const two=h.router.setVisible(true,prefs);await Promise.all([one,two]);
 check('Queued migrations snapshot preference intents and finish with exactly the requested last carrier',()=>{assert(h.calls.includes('steam:true'));assert.equal(h.router.active(),'desktop');assert.deepEqual(h.states,{steam:'hidden',desktop:'shown'})});
 assert((await h.router.setVisible(false,prefs)).confirmed);check('Master off reclaims owned output and leaves no shown carrier',()=>assert.deepEqual(h.states,{steam:'hidden',desktop:'hidden'}));
}
const report={passed:cases.length,cases,scope:'Exact production optional router/coordinator/model using synthetic carrier ACKs; not installed Steam/desktop pixel acceptance. Default desktop behavior and sole telemetry layer retained.'};fs.writeFileSync(path.join(out,'OPTIONAL-STEAM-CARRIER-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
