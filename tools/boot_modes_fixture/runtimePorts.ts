/** In-memory native owners. Never imports production IO or writes user settings. */
export const copy = <T>(value: T): T => structuredClone(value);
export const calls: Array<{op:string;value?:any}> = [];
export const faults: Record<string,number> = {};
export const hooks: Record<string,undefined|(()=>void|Promise<void>)> = {};
const ipcListeners = new Map<string,Set<(data:any)=>unknown>>();
export const ipcEvents: Array<{op:string;event:string}> = [];
export function on(event:string,listener:(data:any)=>unknown){let entries=ipcListeners.get(event);if(!entries)ipcListeners.set(event,entries=new Set());entries.add(listener);ipcEvents.push({op:'subscribe',event});return()=>{entries!.delete(listener);ipcEvents.push({op:'unsubscribe',event});};}
export function ipcListenerCount(event:string){return ipcListeners.get(event)?.size??0;}
export async function emitMemoryIpc(event:string,data:any={}){ipcEvents.push({op:'emit',event});await Promise.all([...ipcListeners.get(event)??[]].map(listener=>listener(copy(data))));await new Promise<void>(resolve=>setImmediate(resolve));}
export let generation = 1;
export let settings: any;
export let state: any;
export function record(op:string,value?:any) { calls.push({op,value:copy(value ?? null)}); if(faults[op]) { faults[op]--; throw new Error('injected:'+op); } }
export function reset() {
 calls.length=0;ipcEvents.length=0;for(const key of Object.keys(hooks))delete hooks[key]; for(const key of Object.keys(faults)) delete faults[key]; generation++;
 const gyroDefaults = {
  schemaVersion:1,revision:0,closure:'UNENCLOSED',enabled:false,provider:null,unit:null,calibrationId:null,
  outputMode:'disabled',motionInput:'local-space',motionMode:'off',motionTrigger:null,steeringAxis:'roll',
  gyroMultiplier:1,accelerometerMultiplier:1,gyroThreshold:2000,motionSensitivityX:1.5,motionSensitivityY:1.5,
  gyroWeight:1.8,velocityMode:'default',velocityScale:1,innerDeadzone:1,outerDeadzone:0,deadzone:1,antiDeadzone:20,
  outputShape:'default',invertHorizontal:false,invertVertical:false,aimingSightsMultiplier:0.3,aimingSightsTrigger:'',
  preset:'fps',outputStick:'right',outputAxis:'xy',calibration:null,autoCalibrate:true,virtualPadLink:true,
 };
 const uiDefaults = {
  theme:'blue-black',dynamicBackgroundEnabled:false,steamDynamicWallpaperEnabled:true,steamWallpaperBackgroundDownload:true,
  steamWallpaperDownloadCpuThreshold:15,backgroundOpacity:0.64,backgroundBlur:0,videoBatteryPause:true,scheduleMonitor:true,
  steamChartAutoRefresh:'none',windowPlacement:'right',gestureSummonEnabled:false,trainerAutoPrepare:true,radialTestingEnabled:false,fxSoundAutoRepair:true,
 };
 const bank = {mode:'auto',tdp:15,tdpCeiling:30,auto:{bias:50,saving:'small',coreSplit:15,coreLow:'bigFirst',coreHigh:'bigFirst'},manual:{frequencyMHz:4000,unlimited:false,aggressiveness:60,turbo:true,coreMode:'bigFirst'}};
 settings={gamepad:{mouseBackend:'joyxoff',mouseMode:'on'},extensions:{unrelated:{keep:true}},input:{revision:7,outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false},ownership:{owner:'global'},gyroMotion:copy(gyroDefaults),buttonMapping:{rules:{schemaVersion:2,closure:'UNENCLOSED',shortcutRules:[]}}},performanceSchedule:{configured:true,enabled:true,controls:{ac:copy(bank),dc:copy(bank)}},rtss:{frameRates:[60,120],templateDefaults:false},fan:{preset:'balanced',nodes:[]},startupDesired:{fanControl:false},sleep:{enabled:false},ui:{deckyEnabled:false},quickApps:{apps:[{id:'app-1',name:'Fixture application',path:'C:/fixture/app.exe'}]},gameProfiles:{'C:/games/do-not-edit.exe':{input:{persona:'dualsense-edge'},sentinel:['never','mutate']}},games:[{path:'C:/games/do-not-edit.exe',profile:{sentinel:17}}]};
 state={mouse:{ok:true,stateKnown:true,mode:'on',on:true,backend:'joyxoff',provider:'ymcc',controlAllowed:true},xbox:false,steam:false,fx:false,addon:false,charge:{supported:true,kind:'supported',managed:false,choices:[60,80,100],currentLimit:100,fence:'fixture-fence'},monitor:{ymccEnabled:false,rtssEnabled:false,ymccTemplate:'A'},ui:copy(uiDefaults),touch:{},hibernate:{enabled:false,enabledKnown:true,supported:true,supportedKnown:true},timeouts:{acScreen:10,dcScreen:5,acSleep:30,dcSleep:15},acButton:0,dcButton:0,recovery:{ok:true,enabled:false},appearance:{themes:[]},fanReady:false,fanIntent:false};state.fanFeature=copy(settings.fan);state.deviceForm=3;state.earlyCommand='';state.homeApp='fixture-original-home';state.joyStartup=null;state.legacyDesktop=false;state.mouseAutoOn=false;state.steamUi={known:true,running:false,mode:'stopped',pid:0,birth:'0'};state.boundPersona=null;state.buildPersona=null;state.mouseReads=0;state.nativeGamepadFlags={enabled:true,bDoubleMinimize:true,startDoubleF7:false,tdpShortcut:true,fpsShortcut:true,killGame:true,openKeyboard:true,returnDesktop:true,mouseToggle:true,mouseBackend:'joyxoff'};
 settings.input.startupObservedBoot='fixture-windows-boot-1';
}
reset();
export async function loadSettings(){await hooks.settingsRead?.();return copy(settings);}
export async function updateSettings(update:any){record('settings.update');const draft=copy(settings);await update(draft);if(draft.extensions?.bootModesStartupResult&&faults['startup.result.write']){faults['startup.result.write']--;throw Error('injected:startup.result.write');}settings=draft;return copy(settings);}
export function getSettingsGeneration(){return generation;}
export function assertSettingsGeneration(expected:number){if(expected!==generation)throw new Error('stale generation');}
export function invalidate(){generation++;}
export async function compareAndSwapInputSettings(expected:number,patch:any,isCurrent?:()=>boolean){record('input.cas',{expected,patch});await hooks.inputCas?.();if(isCurrent&&!isCurrent())return {ok:false,reason:'cancelled',currentRevision:settings.input.revision,value:copy(settings.input)};if(faults['input.stale']){faults['input.stale']--;return {ok:false,reason:'revision-conflict'};}if(expected!==settings.input.revision)return {ok:false,reason:'revision-conflict'};settings.input={...settings.input,...copy(patch),revision:expected+1};if(faults['input.invalidate']){faults['input.invalidate']--;generation++;}return {ok:true,value:copy(settings.input)};}
export async function loadPerformanceSchedule(){return copy(settings.performanceSchedule);}
export const snapshotPerformanceSchedule=(value:any)=>copy(value);
export const continuousControlsFromSchedule=(config:any)=>copy(config.controls);
export async function restorePerformanceScheduleIfConfigured(){record('performance.restore');}
export async function loadUiSettings(){return copy(state.ui);}
export const getUiSettings=()=>copy(state.ui);
export async function setUiSettings(patch:any){record('ui.set',patch);state.ui={...state.ui,...copy(patch)};}
export function applyTheme(theme:any){record('theme.apply',theme);}
// Mirrors native mouseModeSteamDeckBlocked(): effective game override and still-bound/build personas win over the new global default.
export function nativeMouseSteamBlocked(){const target=settings.input.gameOverride?.outputTarget??settings.input.outputTarget;return target?.persona==='steamdeck'||state.boundPersona==='steamdeck'||state.buildPersona==='steamdeck';}
function nativeSteamMouseState(operation:boolean){return {ok:!operation,backend:state.mouse.backend,provider:'steam',on:false,stateKnown:false,controlAllowed:false,mappingVerified:false,verification:'unobserved',reason:'steamdeck_managed',error:operation?'SteamDeck 虚拟手柄启用时鼠标由 Steam 管理，不能开启 YMCC 模拟鼠标。':''};}
export async function getMouseModeState(){state.mouseReads++;await hooks.mouseRead?.();return nativeMouseSteamBlocked()?{...nativeSteamMouseState(false),mode:state.mouse.mode}:copy(state.mouse);}
export async function setMouseModeEnabled(on:boolean){record('mouse.enable',on);await hooks.mouseWrite?.();if(nativeMouseSteamBlocked())return nativeSteamMouseState(true);state.mouse.on=on;state.mouse.mode=on?'on':'off';state.mouse.provider='ymcc';settings.gamepad.mouseMode=state.mouse.mode;if(on&&faults['mouse.start.partial']){faults['mouse.start.partial']--;throw new Error('injected:mouse.start.partial');}return copy(state.mouse);}
export async function setMouseModePolicy(mode:'on'|'off'|'auto'){record('mouse.policy',mode);await hooks.mouseWrite?.();if(nativeMouseSteamBlocked())return nativeSteamMouseState(true);state.mouse.mode=mode;state.mouse.on=mode==='auto'?state.mouseAutoOn:mode==='on';state.mouse.provider='ymcc';settings.gamepad.mouseMode=mode;return copy(state.mouse);}
export async function setMouseBackend(backend:string){record('mouse.backend',backend);await hooks.mouseBackend?.();if(nativeMouseSteamBlocked())return nativeSteamMouseState(true);if(!['joyxoff','gamebar'].includes(backend))return {ok:false,backend:state.mouse.backend,reason:'invalid_backend'};state.mouse.backend=backend;state.nativeGamepadFlags.mouseBackend=backend;settings.gamepad.mouseBackend=backend;return copy(state.mouse);}
// Native summon.set updates the live g_summon shortcut cache as well as the saved gamepad section.
export async function summonGet(){return copy(state.nativeGamepadFlags);}
export async function summonSet(flags:any){record('summon.set',flags);await hooks.summonSet?.();for(const [key,value]of Object.entries(flags)){if(key in state.nativeGamepadFlags&&key!=='mouseBackend')state.nativeGamepadFlags[key]=value===true;if(key==='mouseBackend'&&['joyxoff','gamebar'].includes(String(value))){state.nativeGamepadFlags.mouseBackend=value;state.mouse.backend=value;}}settings.gamepad={...settings.gamepad,...copy(state.nativeGamepadFlags)};const result=copy(state.nativeGamepadFlags);if(faults['summon.ack-mismatch']){faults['summon.ack-mismatch']--;const key=Object.keys(flags).find(k=>typeof flags[k]==='boolean');if(key)result[key]=!flags[key];}return result;}
export const getFanFeatureSettings=()=>copy(settings.fan);
export const DEFAULT_FAN_PRESET_CURVES={soft:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:70}],balanced:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:90}],aggressive:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:100}]};
export const fanHostLifecycle={get hasControlIntent(){return state.fanIntent;},get controlReady(){return state.fanReady;},async start(){record('fan.start');state.fanIntent=true;state.fanReady=true;return {allowed:true,writeReady:true};},async close(){record('fan.close');state.fanIntent=false;state.fanReady=false;},async applyPreset(preset:any,nodes:any){record('fan.preset',{preset,nodes});}};
export const batteryCharge={async get(){await hooks.chargeRead?.();return copy(state.charge);},async set(fence:any,limit:any){record('charge.set',{fence,limit});state.charge.managed=true;state.charge.requestedLimit=limit;return {ok:true};},async release(){record('charge.release');state.charge.managed=false;return {ok:true};}};
export const steamUi={subscribe:(handler:()=>void)=>on('steam.uiStateChanged',handler),async read(){return copy(state.steamUi);},async switchMode(_identity:any,mode:string,options:any){record('steam.switchMode',{mode,userInitiated:options?.userInitiated===true});if(options?.userInitiated!==true)throw Error('Steam switch requires manual intent');state.steamUi.mode=mode;return copy(state.steamUi);},async showBigPicture(_identity:any,options:any){record('steam.showBigPicture',{userInitiated:options?.userInitiated===true});if(options?.userInitiated!==true)throw Error('Steam show requires manual intent');return copy(state.steamUi);}};
export async function launchSteam(options?:{userInitiated:true;isCurrent?:()=>boolean}){const current=options?.isCurrent?.();record('steam.launch',{userInitiated:options?.userInitiated===true,hasCurrentGuard:typeof options?.isCurrent==='function',currentAtDispatch:current,currentMode:settings.extensions.bootModes?.current});if(options?.userInitiated!==true||current!==true)throw Error('Steam launch requires fresh manual intent and current generation');state.steamUi={known:true,running:true,mode:'desktop',pid:4321,birth:'fixture-steam-birth'};}
export const STEAM_APPEARANCE_THEMES=[];
export const steamAppearance={async read(){return copy(state.appearance);},async apply(_before:any,states:any){record('appearance.apply',states);return {ok:true};}};
export async function readMonitorOutputPreferences(){return copy(state.monitor);}
export async function saveMonitorOutputPreferences(value:any){record('monitor.save',value);state.monitor=copy(value);}
export const monitorOutputCoordinator={async apply(value:any){record('monitor.apply',value);if(faults['monitor.pending']){faults['monitor.pending']--;return {ok:false,reason:'fixture monitor not confirmed'};}return {ok:true};}};
export async function screenTouchpadsGet(persona:string){return copy(state.touch[persona]??{persona,layout:'off',enabled:false,ok:true,available:true});}
export async function screenTouchpadsSet(patch:any,persona:string){record('touch.set',{patch,persona});state.touch[persona]={...state.touch[persona],...copy(patch)};return {ok:true,settingsSaved:true};}
export async function setTrayResident(enabled:boolean){record('tray.set',enabled);}
export const shell={async execute(path:string,args:any){record('app.launch',{path,args});return true;},async open(path:string){record('app.open',path);}};
export const display={async getModes(){return {modes:[]};},async setMode(mode:any){record('display.set',mode);}};
export const gamepadFeedback={}; export const wakePassword={async set(enabled:boolean){record('wakePassword.set',enabled);return {ok:true,known:true,enabled};}};
export const getPolicyGame=()=>null;
export const getLosslessGameState=async()=>({});
export async function setLosslessScalingEnabled(...args:any[]){record('lossless.set',args.slice(0,2));return {confirmed:true};}
export async function getAutomaticSleepRecovery(){return copy(state.recovery);}
export async function setAutomaticSleepRecovery(enabled:boolean){record('recovery.set',enabled);state.recovery.enabled=enabled;}
export async function readGamingHomeConfiguration(){return {startupEnabled:state.xbox,homeApp:'fixture-xbox'};}
export async function writeGamingHomeStartup(enabled:boolean,repairHandheld=enabled){record('startup.xbox',enabled);state.xbox=enabled;if(repairHandheld){record('startup.handheld',46);state.deviceForm=46;}}
export async function steamMasterOn(){return state.steam;}
export async function steamMasterSet(enabled:boolean,repairCommand=false){record('startup.steam',enabled);state.steam=enabled;state.earlyCommand=enabled?'"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"':'';if(repairCommand)record('startup.commandRepair',true);}
export async function steamRunning(){return false;}
export async function steamAddonExists(_id:string){return state.addon;}
export async function steamAddonSet(id:string,enabled:boolean){record('addon.set',{id,enabled});state.addon=enabled;}
export async function fxExists(){return state.fx;}
export async function fxSet(enabled:boolean){record('fx.set',enabled);state.fx=enabled;}
export async function readHibernateState(){await hooks.hibernateRead?.();return copy(state.hibernate);}
export async function readHibernateSize(){return 40;}
export async function readSleepTimeouts(){return copy(state.timeouts);}
export async function setHibernate(enabled:boolean){record('hibernate.set',enabled);state.hibernate.enabled=enabled;return {exitCode:0};}
export async function setHibernateSize(size:number){record('hibernate.size',size);}
export async function setSleepTimeout(kind:string,ac:boolean,minutes:number){record('sleep.timeout',{kind,ac,minutes});}
export async function getPowerBtnIdx(ac:boolean){return ac?state.acButton:state.dcButton;}
export async function setPowerBtnIdx(ac:boolean,index:number){record('sleep.button',{ac,index});state[ac?'acButton':'dcButton']=index;}
export async function sleepGuardGet(){return copy(settings.sleep);}
export async function sleepGuardSetConfig(config:any){record('sleep.config',config);settings.sleep=copy(config);}
export async function getTdpExecutionCapability(){await hooks.tdpCapabilityRead?.();return {supported:true};}
export async function detectCoreArchitecture(){await hooks.cpuArchitectureRead?.();return {heterogeneous:true};}
export async function invoke(op:string,args:any){record('ipc.'+op,args);if(op==='fxsound.setEnabled'){if(args?.enabled!==true)throw Error('boot must only request FxSound ON');return {ok:true,copied:false,reused:true,repaired:false,reason:'静默启动',state:{known:true,installed:true,running:true,enabled:true,selected:true,preset:'移动端通用音频优化',output:'fixture FxSound',reason:''}};}if(op==='input.gameOverride.session')return hooks.nativeSession?.() ?? 'fixture-native-session-1';if(op==='bootMode.startup.identity')return hooks.bootIdentity ? hooks.bootIdentity() : 'fixture-windows-boot-1';throw new Error('Unexpected native operation: '+op);}
export const bootPlatform={async read(){return state.platform?copy(state.platform):{known:true,supported:true,active:state.xbox};},async set(_before:any,active:boolean){record('platform.set',active);state.xbox=active;return {ok:true,known:true,supported:true,active};}};
export async function readBootDisplay(){return {brightness:50,resolution:'1280x800',hz:'60'};}
export async function applyBootBrightness(percent:number){record('brightness.set',percent);}
export async function savePerformanceSchedule(config:any){record('performance.save',config);settings.performanceSchedule=copy(config);return copy(config);}
export async function saveFanPresetSelection(preset:string,expected:any){
 record('fan.save',preset);
 const fingerprint=(value:any)=>JSON.stringify([value.preset,value.nodes,value.presetCurves]);
 if(fingerprint(settings.fan)!==fingerprint(expected))throw new Error('SHORTCUT_SOURCE_CHANGED');
 const current=copy(settings.fan),table=copy(current.presetCurves||{});
 if(!(current.preset in table)&&Array.isArray(current.nodes))table[current.preset]=copy(current.nodes);
 if(!(preset in table))table[preset]=copy(DEFAULT_FAN_PRESET_CURVES[preset as keyof typeof DEFAULT_FAN_PRESET_CURVES]);
 settings.fan={...current,preset,nodes:copy(table[preset]),presetCurves:table};state.fanFeature=copy(settings.fan);
}
export function getFanPresetCurve(preset:string){record('fan.curve',preset);return copy(state.fanFeature.presetCurves?.[preset]??DEFAULT_FAN_PRESET_CURVES[preset as keyof typeof DEFAULT_FAN_PRESET_CURVES]);}
export { screenTouchpadDefaults } from '../../src/bridge/screenTouchpadDefaults';
export const STEAM_ADDONS=[{key:'steamscale',exe:'C:/fixture/LosslessScaling.exe'}];
export const proc={async findExact(path:string){record('process.find',path);return {found:false};}};
export const fs={async exists(path:string){record('file.exists',path);await hooks.fileRead?.();return true;}};
export async function getSleepPowerPlanOptimizationEnabled(){return false;}
export async function setSleepPowerPlanOptimizationEnabled(value:boolean){record('sleep.plan',value);return true;}
export async function reactivateCurrentScheme(){record('sleep.reactivate');return true;}
export const deckySidebar={async get(){return {enabled:false,phase:'off'};},async setEnabled(enabled:boolean){record('decky.set',enabled);return {enabled,phase:enabled?'loader-started':'off',mirrorConnected:enabled,powerReady:true};}};


// Retired startup entries are configuration only; removing them never stops a current mouse owner.
export const registry={async read(hive:string,key:string,name:string){record('registry.read',{hive,key,name});return name==='Joyxoff'?state.joyStartup:null;}};
export async function joySet(enabled:boolean){record('joy.startup',enabled);state.joyStartup=enabled?'C:\\SOFT\\Joyxoff\\Joyxoff.exe':null;}
export async function deleteTask(name:string){record('legacy.desktop.remove',name);state.legacyDesktop=false;return true;}

// Actual desktop startup preference code owns task intent; only the OS edge is memory.
export async function taskExists(name:string){record('desktop.task.read',name);return name==='桌面模式-开机设置为桌面模式'&&state.legacyDesktop===true;}
export async function toggleTask(name:string,enabled:boolean){record('desktop.task.set',{name,enabled});if(name!=='桌面模式-开机设置为桌面模式')throw Error('Unexpected desktop task');state.legacyDesktop=enabled;return enabled;}
