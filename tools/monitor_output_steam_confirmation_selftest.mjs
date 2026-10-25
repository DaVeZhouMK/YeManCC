// Production adapter regression. All runtime IO is mocked; never controls Steam/game/hardware.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {build} from 'esbuild';import {pathToFileURL} from 'node:url';
const out=process.env.YMCC_STEAM_SETTINGS_OUT||path.resolve('../../../Build/Tasks/YMCC-Steam-HUD-Probe/steam-confirmation-fix-20261011/frontend-tests');fs.mkdirSync(out,{recursive:true});
const stubs={
 ipc:`export async function invoke(cmd,args,options){const h=globalThis.__monitorOutputFixture;h.calls.push({cmd,args,options});return structuredClone(cmd==='steam.settings.get'?h.read:h.write);}`,
 deckySidebar:`export const deckySidebar={async get(){return globalThis.__monitorOutputFixture.session}};`,
 settingsRepository:`export async function readSettingsSection(){return {}};export async function saveSettingsSection(){}`, 
 yeman:`const h=()=>globalThis.__monitorOutputFixture;export async function rtssRunning(){return true};export async function toggleRtss(){throw Error('unexpected backend restart')};export async function readOverlayLayout(){return h().layout};export async function readRtssZoom(){return 2};export async function setOverlayLayout(value){h().calls.push({cmd:'rtss.layout',args:value});h().layout=value==='off'?'Empty.ovl':({W:'YeManOBS-W-1.ovl',L:'YeManOBS-L-1.ovl',J:'YeManOBS-JJ-1.ovl'})[value];return {mode:'live'}};`,
};
const built=await build({stdin:{loader:'ts',resolveDir:process.cwd(),contents:`export * from './src/bridge/monitorOutput.ts';export * from './src/bridge/monitorHudModel.ts';`},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'memory-IO',setup(b){b.onResolve({filter:/^\.\/(ipc|deckySidebar|settingsRepository|yeman)$/},args=>({path:args.path.slice(2),namespace:'memory'}));b.onLoad({filter:/.*/,namespace:'memory'},args=>({contents:stubs[args.path],loader:'ts'}));}}]});
const bundle=path.join(out,'production-monitorOutput.mjs');fs.writeFileSync(bundle,built.outputFiles[0].text);const m=await import(pathToFileURL(bundle));
const live={ok:true,available:true,pending:false,via:'live',liveConfirmed:true,position:1,detail:4,scale:.8,opacity:1,saturation:1};
let passed=0;function reset(){globalThis.__monitorOutputFixture={calls:[],session:{steamKnown:true,steamRunning:true},read:{...live},write:{...live,position:0,applicationConfirmed:true},layout:'YeManOBS-W-1.ovl'};return globalThis.__monitorOutputFixture;}
function check(label,fn){fn();passed++;console.log('PASS '+label);}
const unregister=m.registerYmccHudOutputAdapter({async available(){return true},async visible(){return false},async state(){return 'hidden'},async setVisible(show){globalThis.__monitorOutputFixture.calls.push({cmd:'ymcc.show',args:show});return {confirmed:true}}});
const prefs=m.normalizeMonitorOutput({enabled:true,output:'ymcc'});
{
 const h=reset();h.read.pending=true;h.read.restartRequired=true;
 const result=await m.monitorOutputCoordinator.apply(prefs);
 check('stale pending intent plus fresh live proof permits real awaited hide',()=>{assert(result.ok);assert.equal(result.applied,'ymcc');const writes=h.calls.filter(c=>c.cmd==='steam.settings.set');assert.equal(writes.length,1);assert.deepEqual(writes[0].args,{monitor:{position:0},awaitApplied:true});assert.equal(writes[0].options.timeoutMs,25000);assert(h.calls.some(c=>c.cmd==='ymcc.show'&&c.args===true));});
}
for(const patch of [{liveConfirmed:false},{via:'file'},{ok:false},{available:false}]){
 const h=reset();Object.assign(h.read,patch);const result=await m.monitorOutputCoordinator.apply(prefs);
 check('unconfirmed Steam preflight never starts writes: '+JSON.stringify(patch),()=>{assert(!result.ok);assert.equal(result.phase,'preflight');assert(h.calls.every(c=>c.cmd==='steam.settings.get'));});
}
for(const patch of [{pending:true,applicationConfirmed:false},{applicationConfirmed:false},{applicationConfirmed:undefined},{position:1},{liveConfirmed:false},{ok:false,reason:'settings-write-failed'},{ok:false,reason:'live-transport-uncertain'}]){
 const h=reset();Object.assign(h.write,patch);const result=await m.monitorOutputCoordinator.apply(prefs);
 check('unconfirmed application receipt blocks all later outputs: '+JSON.stringify(patch),()=>{assert(!result.ok);assert.equal(result.phase,'hide-steam');assert(!h.calls.some(c=>c.cmd==='rtss.layout'||c.cmd==='ymcc.show'));});
}
{
 const h=reset();h.session={steamKnown:true,steamRunning:false};h.read={ok:false,available:false};const result=await m.monitorOutputCoordinator.apply(prefs);
 check('confirmed Steam absence can use desktop carrier without Steam SET',()=>{assert(result.ok);assert(!h.calls.some(c=>c.cmd==='steam.settings.set'));assert(h.calls.some(c=>c.cmd==='ymcc.show'&&c.args));});
 const reject=await m.monitorOutputCoordinator.apply({...prefs,output:'steam'});check('confirmed Steam absence still rejects native Steam output',()=>assert(!reject.ok));
}
{
 const h=reset();h.read.pending=true;const state=await m.readMonitorOutputRuntime();
 check('runtime derives native Steam visibility from actual live position, not pending target',()=>assert.equal(state.applied,'unknown')); // RTSS and Steam are both currently visible.
 h.layout='Empty.ovl';const only=await m.readMonitorOutputRuntime();check('runtime detects one native live output correctly with stale pending',()=>assert.equal(only.applied,'steam'));
 h.read.liveConfirmed=false;const unconfirmed=await m.readMonitorOutputRuntime();check('runtime cannot prove absence from fallback data',()=>assert.equal(unconfirmed.applied,'unknown'));
}
unregister();console.log('production monitor output adapter: '+passed+' cases passed; all IO mocked');fs.writeFileSync(path.join(out,'summary.json'),JSON.stringify({passed,scope:'production adapter + real Steam IPC client; mocked runtime IO',gameAcceptance:false},null,2));
