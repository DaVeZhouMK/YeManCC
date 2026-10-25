import { normalizeContinuousControls } from '@/bridge/performanceControls';
import { patchContinuousBank } from '@/bridge/performanceControls';
import { frameRateSetting } from '@/bridge/frameRateModel';
const copy = value => JSON.parse(JSON.stringify(value));
const fixture = () => window.__fixture;
export function gameProfileInputRevision(profile) { return JSON.stringify([profile?.padPersona,profile?.gyroOverride,profile?.gyroPreset,profile?.inputOverrides]); }
const configListeners = new Set();
export const onGameCustomConfigChanged = cb => {configListeners.add(cb);return()=>configListeners.delete(cb)};
async function publish() { const config=await loadGameCustomConfig();for(const cb of [...configListeners])cb(config);for(const cb of fixture().listeners)cb(); }
window.__profileChanged = publish;
const key=game=>(game?.path||game?.name||'').split(/[\\/]/).pop().toLowerCase();
const profile=()=>fixture().profileDeleted?undefined:{...copy(fixture().profileInput),displayName:fixture().profileInput.displayName||'Cyberpunk 2077',controls:copy(fixture().dedicatedControls),ac:{},dc:{}};
export async function getContinuousPerformanceSnapshot(scope='global', editSide) {
 const f=fixture(),side=editSide||f.side,entry=profile(),owner=scope==='effective'?(entry&&entry.enabled!==false?'dedicated':'global'):scope;
 const controls=owner==='dedicated'?f.dedicatedControls:f.controls,frames=owner==='dedicated'?f.dedicatedFrames:f.frames,bank=copy(controls[side]);
 return {side,actualSide:f.side,gameName:entry?.displayName,bank,controls:copy(controls),frameRates:copy(frames),coreGroups:f.hybrid?[{label:'大核',physical:3,logical:6},{label:'小核',physical:5,logical:10}]:[],hybrid:f.hybrid,smtAvailable:true,hyperThreadMode:f.smt,configured:true,hardwareMinimum:2,hardwareMaximum:150,identity:{scope:owner,followEffective:scope==='effective',side,actualSide:f.side,exe:key(f.game),pid:f.game?.pid||0,processCreated:f.game?.processCreated||'',generation:1,revision:JSON.stringify([bank,frames[side],f.smt,owner==='dedicated'?entry?.enabled:true])}};
}
export async function updateContinuousPerformanceControls(identity,patch) {
 const f=fixture(),current=await getContinuousPerformanceSnapshot(identity.followEffective?'effective':identity.scope,identity.side);
 if(identity.revision!==current.identity.revision||identity.scope!==current.identity.scope)throw Error('当前编辑对象已变化');
 const controls=identity.scope==='dedicated'?f.dedicatedControls:f.controls,frames=identity.scope==='dedicated'?f.dedicatedFrames:f.frames;
 controls[identity.side]=patchContinuousBank(controls[identity.side],patch,identity.side);
 if(patch.frameRate)frames[identity.side]=frameRateSetting(patch.frameRate);if(patch.hyperThreadMode)f.smt=patch.hyperThreadMode;
 f.writes.push(copy({scope:identity.scope,side:identity.side,patch}));
 if(identity.scope==='dedicated')await publish();else for(const cb of f.listeners)cb();
 return getContinuousPerformanceSnapshot(identity.followEffective?'effective':identity.scope,identity.side);
}
export const onPerformanceScheduleChanged=cb=>{fixture().listeners.add(cb);return()=>fixture().listeners.delete(cb)};
export const getGameCustomConfig=()=>({version:1,entries:profile()?{[key(fixture().game)]:profile()}:{},rtss:fixture().profileDeleted?{}:{[key(fixture().game)]:copy(fixture().customRtss||{})}});
export const loadGameCustomConfig=async()=>getGameCustomConfig();
// Full document reads/writes remain fixture-owned memory, matching the newer
// radial consumer contract without touching product configuration files.
export async function saveGameCustomConfig(config){
 const f=fixture(),exe=key(f.game),next=config.entries?.[exe];
 if(!config||config.version!==1||!config.entries)throw Error('Invalid fixture game configuration');
 f.profileDeleted=!next;
 if(next){f.profileInput=copy(next);if(next.controls)f.dedicatedControls=copy(next.controls);}
 f.customRtss=copy(config.rtss?.[exe]||{});
 f.writes.push(copy({scope:'dedicated-document',config}));await publish();
}
export const loadPerformanceSchedule=async()=>copy(fixture().schedule||{configured:true,controls:fixture().controls,profiles:{ac:{performance:{}},dc:{performance:{}}},active:{ac:'performance',dc:'performance'}});
// Explicit memory scheduling storage for the real BootModes consumer. Hardware is outside this UI fixture.
export async function savePerformanceSchedule(config) {
 const f=fixture();f.schedule=copy(config);
 if(config.controls)f.controls=normalizeContinuousControls(config.controls);
 f.writes.push(copy({scope:'global-schedule-document',config}));
 for(const cb of f.listeners)cb();
}
export const restorePerformanceScheduleIfConfigured=async()=>profile()?.enabled!==false&&!fixture().profileDeleted?'dedicated':'global';
function verify(expected){const f=fixture();if(expected.exe!==key(f.game)||expected.pid!==f.game?.pid||expected.processCreated!==f.game?.processCreated)throw Error('当前游戏已变化');}
export async function patchGameCustomProfile(expected,patch,seed,options={}){
 verify(expected);const f=fixture(),current=profile();if(!current&&!seed)throw Error('请先创建专属配置');
 if(options.expectedAbsent&&current)throw Error('专属配置已变化');
 if(options.expectedEnabled!==undefined&&current?.enabled!==options.expectedEnabled)throw Error('专属状态已变化');
 if(options.expectedInputRevision!==undefined&&gameProfileInputRevision(current)!==options.expectedInputRevision)throw Error('专属参数已变化');
 f.profileDeleted=false;Object.assign(f.profileInput,copy(patch));if(seed&&!current){f.dedicatedControls=copy(seed.controls);Object.assign(f.profileInput,seed,patch)}
 f.writes.push(copy({scope:'dedicated-input',patch}));await publish();
}
export async function removeGameCustomProfile(expected,revision){verify(expected);const config=await loadGameCustomConfig();if(JSON.stringify([config.entries[expected.exe],config.rtss[expected.exe]])!==revision)throw Error('专属配置已变化，请重新确认');fixture().profileDeleted=true;fixture().writes.push({scope:'remove-profile',exe:expected.exe});await publish();}
export const clearGameInputRestoreSnapshot=async()=>{};

// Memory-only ports newly imported by the real boot/simulation UI. Windows power writes remain outside this harness.
export const snapshotPerformanceSchedule=config=>copy(config);
export async function stopPerformanceScheduleAndUseWindowsBalanced(){
 const current=await loadPerformanceSchedule();await savePerformanceSchedule({...current,suspended:true,enabled:false});
 fixture().writes.push({scope:'windows-balanced-memory-port'});
}
