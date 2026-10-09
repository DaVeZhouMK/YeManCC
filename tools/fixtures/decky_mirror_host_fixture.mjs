// Exact production host/relay/adapters. Only the external IPC/device/store IO
// boundaries are inert; business save/load/lock/normalization stay original.
import fs from 'node:fs';import path from 'node:path';import {root,load,fixture,gamePolicyKey,detectedGameName,base} from './decky_game_memory_store.mjs';
const keyModule=load(fs.readFileSync(path.join(root,'src/bridge/gamePolicyHysteresis.ts'),'utf8'));
const steamModule=load(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'));
const relayModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':steamModule});
const gameModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyGameActions.ts'),'utf8'),{'./gamePolicyHysteresis':keyModule});
const speedModule=load(fs.readFileSync(path.join(root,'src/bridge/deckySpeedActions.ts'),'utf8'),{'./deckyGameActions':gameModule});
const frameModel=load(fs.readFileSync(path.join(root,'src/bridge/frameRateModel.ts'),'utf8'));
const screenModule=load(fs.readFileSync(path.join(root,'src/bridge/screenTouchpads.ts'),'utf8'),{'./ipc':{isNativeRuntime:false}});
const frameModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyFrameActions.ts'),'utf8'),{'./frameRateModel':frameModel,'./deckyGameActions':gameModule});
const touchModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyTouchpadActions.ts'),'utf8'),{'./screenTouchpads':screenModule});
const fanModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyFanActions.ts'),'utf8'));
export async function createMirrorHostFixture({initial={version:1,entries:{},rtss:{}},warm=true,invoke:externalInvoke}={}){
 const memory=fixture(initial);if(warm)await memory.store.loadGameCustomConfig();
 const counts={power:0,schedule:0,capabilities:0,saves:0,joyxoff:0,fanSaves:0,watchers:0,attaches:0,speedApply:0,speedClear:0,frameApplies:0,globalFrameSaves:0,touchpadSaves:0};
 const state={globalFrames:frameModel.frameRatePair({}),input:{outputTarget:{persona:'steamdeck',buttonMappingEnabled:true}},touchpads:{...screenModule.screenTouchpadDefaults('steamdeck'),ok:true,available:true,visible:false,persona:'steamdeck',steamDeckAvailable:true,ps5Available:false},touchError:false,frameApplyResult:true,speed:null,speedResult:null,gyroDefaults:{padPersona:'steamdeck',virtualPad:true,enabled:false,preset:'fps',source:'inert-original-global-gyro'},powerSource:'ac',lossless:{settingsPath:'memory-only-LS.xml',xml:'memory-LS-xml',profile:null,reason:'当前 EXE 不在LosslessScaling 插帧生成的列表中'},game:{pid:101,name:'game.exe',title:'Fixture Game (Vulkan)',path:'C:/Games/Game.exe',processCreated:'123456789',ts:0},power:{generation:1,phase:'ready',hardwareWritesAllowed:true,inputReady:true,resumeReady:true},caps:{detected:true,heterogeneous:true,smtAvailable:true,efficiencyClasses:[0,1],logical:8,physical:4,smtLogical:8,smtPhysical:4,source:'inert-fixture'},fan:{enabled:false,preset:'balanced'},schedule:{version:2,configured:true,enabled:true,active:{ac:'balanced',dc:'eco'},profiles:{ac:{},dc:{}}}};
 const modes=['eco','balanced','medium','performance','elite','extreme'];for(const side of ['ac','dc'])for(const [i,mode] of modes.entries())state.schedule.profiles[side][mode]={...base,tdpMax:side==='ac'?25+i*3:8+i*2,fpsTarget:side==='ac'?60:30};
 const events=new Map(),gameListeners=new Set(),scheduleListeners=new Set(),fanListeners=new Set(),replies=[];
 let handle=null;
 const on=(event,callback)=>{let set=events.get(event);if(!set)events.set(event,set=new Set());set.add(callback);return()=>{set.delete(callback);if(!set.size)events.delete(event);};};
 const emit=(event,message)=>{for(const callback of [...events.get(event)??[]])callback(message);};
 const invoke=async(command,args,options)=>{if(command==='deckySidebar.mirrorAttach')counts.attaches++;if(command==='deckySidebar.mirrorReply')replies.push(structuredClone(args));if(externalInvoke)return externalInvoke(command,args,options);if(command==='deckySidebar.mirrorAttach')return {connected:false};if(command==='deckySidebar.mirrorReply')return true;throw new Error('Unexpected production IPC: '+command);};
 const modules={
 './deckySpeedActions':speedModule,
 './deckyFrameActions':frameModule,'./deckyTouchpadActions':touchModule,
 './settingsRepository':{readSettingsSection:async()=>structuredClone(state.input)},
 './screenTouchpads':{...screenModule,screenTouchpadsGet:async persona=>({...structuredClone(state.touchpads),persona}),screenTouchpadsSet:async(patch,persona)=>{counts.touchpadSaves++;if(state.touchError)return {...state.touchpads,ok:false};state.touchpads={...state.touchpads,...structuredClone(patch),persona};emit('screenTouchpads.updated',state.touchpads);return structuredClone(state.touchpads);}},
 './frameRateLimits':{...frameModel,loadGlobalFrameRates:async()=>structuredClone(state.globalFrames),saveGlobalFrameRate:async(side,value)=>{counts.globalFrameSaves++;state.globalFrames[side]=frameModel.frameRateSetting(value,state.globalFrames[side]);emit('test.frameRates',state.globalFrames);return structuredClone(state.globalFrames);},applyIndependentFrameRates:async current=>{if(!current())return false;counts.frameApplies++;return state.frameApplyResult;},onFrameRatesChanged:callback=>on('test.frameRates',callback)},
 './speedhack':{SPEED_PRESETS:[0.5,0.8,1,1.2,1.5,2,4,8],isMinecraftTarget:game=>/minecraft|java/i.test(game.name),getGameSpeedState:()=>state.speed?structuredClone(state.speed):null,onGameSpeedStateChanged:()=>()=>{},applyGameSpeed:async(pid,factor,target)=>{counts.speedApply++;if(state.speedResult)return state.speedResult;state.speed={pid,processCreated:target.processCreated,factor,enabled:true};return {ok:true,msgs:[]};},clearGameSpeed:async()=>{counts.speedClear++;if(state.speedResult)return state.speedResult;state.speed=null;return {ok:true,msgs:[]};}},
 './deckyGyroDefaults':{readGameMirrorGyroDefaults:async()=>structuredClone(state.gyroDefaults)},
 './powerSource':{powerSourceMode:{get value(){return state.powerSource;}},refreshPowerSourceSnapshot:async()=>{}},
 './losslessScaling':{readLosslessScaling:async()=>structuredClone(state.lossless),losslessField:document=>({value:document?.profile?.enabled?'on':'off',supported:!!document?.profile,choices:[{data:'off',label:'关闭'},{data:'on',label:'开启'}]}),setLosslessFromMirror:()=>{throw Error('No real LS control in fixture');},onLosslessScalingChanged:()=>()=>{}},
 'vue':{watch:()=>{counts.watchers++;return()=>counts.watchers--;}},'./ipc':{on,invoke},'./api':{powerLifecycle:{get:async()=>{counts.power++;return structuredClone(state.power);}}},
 './gamePolicyTarget':{getPolicyGame:()=>state.game,gamePolicyKey,subscribePolicyGameStatus:callback=>{gameListeners.add(callback);callback(state.game);return()=>gameListeners.delete(callback);}},
 './gamedetect':{detectedGameName},'./quickActionLock':memory.lock,'./gameproc':{closeJoyxoffIfRunning:async()=>{counts.joyxoff++;return false;}},
 './performanceSchedule':{getGameCustomConfig:memory.store.getGameCustomConfig,loadGameCustomConfig:memory.store.loadGameCustomConfig,saveGameCustomConfig:async config=>{counts.saves++;await memory.store.saveGameCustomConfig(config);},resolveGameCustomProfiles:memory.store.resolveGameCustomProfiles,loadPerformanceSchedule:async()=>{counts.schedule++;return structuredClone(state.schedule);},onGameCustomConfigChanged:memory.store.onGameCustomConfigChanged,onPerformanceScheduleChanged:callback=>{scheduleListeners.add(callback);return()=>scheduleListeners.delete(callback);}},
 './gameCorePolicy':{detectGameCorePolicy:async()=>{counts.capabilities++;return state.caps?structuredClone(state.caps):null;}},'./deckyGameActions':gameModule,'./deckyFanActions':fanModule,'./deckyMirrorRelay':relayModule,
 './deckyFanDisplay':{observeFanMirrorDisplay:()=>{},isFanMirrorUiBusy:()=>false,onFanMirrorUiGate:callback=>{fanListeners.add(callback);return()=>fanListeners.delete(callback);}},
 './fanFeature':{FAN_IMPORT_ENABLED:true,FAN_FORCE_PREVIEW:false,fanFeatureEnabled:{value:true},fanControlActive:{get value(){return state.fan.enabled;}},getFanFeatureSettings:()=>({preset:state.fan.preset}),getFanPresetCurve:()=>[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:100}],saveFanCurve:async()=>{counts.fanSaves++;throw new Error('Game host fixture must not save Fan');},setFanControlActive:()=>{throw new Error('Game host fixture must not write Fan');},setFanNavigationDuty:()=>{},recordFanHandshake:async()=>{}},
 './fanHost':{fanHostLifecycle:{controlReady:true}},
 };
 const source=fs.readFileSync(path.join(root,'src/bridge/deckyMirrorHost.ts'),'utf8');
 const start=()=>{if(handle)throw new Error('Already running host fixture');handle=load(source,modules).startDeckyMirrorHost();return handle;};
 const stop=()=>{handle?.stop();handle=null;};
 const result={memory,state,counts,replies,emit,start,stop,ready:()=>{if(!handle)throw new Error('No host');handle.ready();},
  gameChanged:()=>{for(const callback of [...gameListeners])callback(state.game);},scheduleChanged:()=>{for(const callback of [...scheduleListeners])callback(structuredClone(state.schedule));},powerChanged:(pending=false)=>emit(pending?'power.pending':'power.resumed',structuredClone(state.power)),
  observerStats:()=>({ipc:[...events.values()].reduce((n,set)=>n+set.size,0),game:gameListeners.size,schedule:scheduleListeners.size,fan:fanListeners.size,watchers:counts.watchers})};
 start();return result;
}
