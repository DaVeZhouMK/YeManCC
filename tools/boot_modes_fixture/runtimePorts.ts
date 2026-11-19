import { setGamingHomeStartup, type GamingHomeSelection } from '../../src/bridge/gamingHomePolicy';
import { globalShortcutContainer, prepareGlobalShortcutEdit } from '../../src/bridge/bootModeSharedShortcuts';
import { normalizeScreenTouchpadSummon, screenTouchpadDefaults } from '../../src/bridge/screenTouchpadDefaults';
import { computeInputConfigHash } from '../../src/bridge/inputConfigHash';
import { equalSettingsDocuments } from '../../src/bridge/settingsBaseline';
import { captureBootPreferenceWrite, type BootPreferenceWrite } from '../../src/bridge/bootModeGlobalPreferences';
/** In-memory native owners. Never imports production IO or writes user settings. */
export const copy = <T>(value: T): T => structuredClone(value);
export const calls: Array<{op:string;value?:any}> = [];
export const faults: Record<string,number> = {};
export const hooks: Record<string,undefined|((...args:any[])=>any)> = {};
const ipcListeners = new Map<string,Set<(data:any)=>unknown>>();
let lastGamepadState:any;
export const ipcEvents: Array<{op:string;event:string}> = [];
export function on(event:string,listener:(data:any)=>unknown){let entries=ipcListeners.get(event);if(!entries)ipcListeners.set(event,entries=new Set());entries.add(listener);if(event==='gamepad.state'&&lastGamepadState!==undefined)queueMicrotask(()=>{if(entries!.has(listener))listener(copy(lastGamepadState));});ipcEvents.push({op:'subscribe',event});return()=>{entries!.delete(listener);ipcEvents.push({op:'unsubscribe',event});};}
export function ipcListenerCount(event:string){return ipcListeners.get(event)?.size??0;}
export async function emitMemoryIpc(event:string,data:any={}){if(event==='gamepad.state')lastGamepadState=copy(data);ipcEvents.push({op:'emit',event});await Promise.all([...ipcListeners.get(event)??[]].map(listener=>listener(copy(data))));await new Promise<void>(resolve=>setImmediate(resolve));}
export let generation = 1;
export let settings: any;
export let state: any;
export function record(op:string,value?:any) { calls.push({op,value:copy(value ?? null)}); if(faults[op]) { faults[op]--; throw new Error('injected:'+op); } }
export function reset() {
 lastGamepadState=undefined; calls.length=0;ipcEvents.length=0;for(const key of Object.keys(hooks))delete hooks[key]; for(const key of Object.keys(faults)) delete faults[key]; generation++;
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
 settings={gamepad:{mouseBackend:'joyxoff',mouseMode:'on'},extensions:{unrelated:{keep:true}},input:{revision:7,bootModeShortcuts:null,outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false},ownership:{owner:'global'},gyroMotion:copy(gyroDefaults),buttonMapping:{rules:{schemaVersion:2,closure:'UNENCLOSED',shortcutRules:[]}}},performanceSchedule:{configured:true,enabled:true,controls:{ac:copy(bank),dc:copy(bank)}},rtss:{frameRates:[60,120],templateDefaults:false},fan:{preset:'balanced',nodes:[]},startupDesired:{fanControl:false},sleep:{enabled:false},ui:{deckyEnabled:false},quickApps:{apps:[{id:'app-1',name:'Fixture application',path:'C:/fixture/app.exe'}]},gameProfiles:{'C:/games/do-not-edit.exe':{input:{persona:'dualsense-edge'},sentinel:['never','mutate']}},games:[{path:'C:/games/do-not-edit.exe',profile:{sentinel:17}}]};
 state={mouse:{ok:true,stateKnown:true,mode:'on',on:true,backend:'joyxoff',provider:'ymcc',controlAllowed:true},xbox:false,steam:false,fx:false,addon:false,charge:{ok:true,supported:true,kind:'supported',managed:false,choices:[60,80,100],currentLimit:100,fence:'fixture-fence'},monitor:{ymccEnabled:false,rtssEnabled:false,ymccTemplate:'A'},ui:copy(uiDefaults),touch:{},hibernate:{enabled:false,enabledKnown:true,supported:true,supportedKnown:true},timeouts:{acScreen:10,dcScreen:5,acSleep:30,dcSleep:15},acButton:0,dcButton:0,recovery:{ok:true,enabled:false},appearance:{themes:[]},fanReady:false,fanIntent:false};state.fanFeature=copy(settings.fan);state.deviceForm=3;state.earlyCommand='';state.homeApp='fixture-original-home';state.joyStartup=null;state.legacyDesktop=false;state.mouseAutoOn=false;state.steamUi={known:true,running:false,mode:'stopped',pid:0,birth:'0'};state.boundPersona=null;state.buildPersona=null;state.mouseReads=0;state.nativeGamepadFlags={enabled:true,bDoubleMinimize:true,startDoubleF7:false,tdpShortcut:true,fpsShortcut:true,killGame:true,openKeyboard:true,returnDesktop:true,mouseToggle:true,mouseBackend:'joyxoff'};
 settings.input.startupObservedBoot='fixture-windows-boot-1';
}
reset();
export async function loadSettings(){await hooks.settingsRead?.();return copy(settings);}
export async function saveSettingsSection(section:string,value:any){return updateSettings(draft=>{draft[section]={...draft[section],...copy(value)};});}
export async function updateSettings(update:any){record('settings.update');const draft=copy(settings);await update(draft);await hooks.settingsWrite?.(copy(draft));if(draft.extensions?.bootModesStartupResult&&faults['startup.result.write']){faults['startup.result.write']--;throw Error('injected:startup.result.write');}settings=JSON.parse(JSON.stringify(draft));return copy(settings);}
export async function updateSettingsForInputReceipt(identity:{revision:number;hash:string},update:any,isCurrent:(settings?:any)=>boolean=()=>true,_options:{origin?:string}={}){
 const {revision,hash}=identity,epoch=generation;
 const matches=()=>settings.input.revision===revision&&computeInputConfigHash(settings.input)===hash;
 if(!isCurrent(copy(settings))||!matches())return false;
 const draft=copy(settings);if(!update(draft))return false;
 record('settings.update');await hooks.settingsWrite?.(copy(draft));await hooks.inputReceiptCommit?.();
 if(epoch!==generation||!isCurrent(copy(settings))||!matches())return false;
 if(draft.extensions?.bootModesStartupResult&&faults['startup.result.write']){faults['startup.result.write']--;throw Error('injected:startup.result.write');}
 settings=JSON.parse(JSON.stringify(draft));return true;
}
export function getSettingsRecoveryState(){return {status:'ready',persisted:true,message:'',...hooks.recoveryState};}
export function getSettingsGeneration(){return generation;}
export function assertSettingsGeneration(expected:number){if(expected!==generation)throw new Error('stale generation');}
export function invalidate(){generation++;}
export async function compareAndSwapInputSettings(expected:number,patch:any,isCurrent?:(current?:any)=>boolean,options:{globalShortcuts?:boolean}={}){record('input.cas',{expected,patch});await hooks.inputCas?.();if(isCurrent&&!isCurrent(copy(settings)))return {ok:false,reason:'cancelled',currentRevision:settings.input.revision,value:copy(settings.input)};if(faults['input.stale']){faults['input.stale']--;return {ok:false,reason:'revision-conflict'};}if(expected!==settings.input.revision)return {ok:false,reason:'revision-conflict',value:copy(settings.input),...(options.globalShortcuts?{globalShortcutRules:globalShortcutContainer(settings)}:{})};const globalPatch=prepareGlobalShortcutEdit(patch);settings.input={...settings.input,...copy(globalPatch),...(globalPatch.ownership?{ownership:{...settings.input.ownership,...copy(globalPatch.ownership)}}:{}),revision:expected+1};if(faults['input.invalidate']){faults['input.invalidate']--;generation++;}return {ok:true,value:copy(settings.input),...(options.globalShortcuts?{globalShortcutRules:globalShortcutContainer(settings)}:{})};}
export async function loadPerformanceSchedule(){return typeof state.performanceLoad==='function'?state.performanceLoad():copy(settings.performanceSchedule);}
export const snapshotPerformanceSchedule=(value:any)=>typeof state.performanceSnapshot==='function'?state.performanceSnapshot(value):copy(value);
export const continuousControlsFromSchedule=(config:any)=>typeof state.performanceControls==='function'?state.performanceControls(config):copy(config.controls);
export async function restorePerformanceScheduleIfConfigured(){record('performance.restore');return state.performanceOwnership??'auto';}
export async function loadUiSettings(){return copy(state.ui);}
export const getUiSettings=()=>copy(state.ui);
export async function setUiSettings(patch:any){record('ui.set',patch);state.ui={...state.ui,...copy(patch)};}
export function applyTheme(theme:any){record('theme.apply',theme);}
// Mirrors native mouseModeSteamDeckBlocked(): effective game override and still-bound/build personas win over the new global default.
export function nativeMouseSteamBlocked(){const target=settings.input.gameOverride?.outputTarget??settings.input.outputTarget;return target?.persona==='steamdeck'||state.boundPersona==='steamdeck'||state.buildPersona==='steamdeck';}
function nativeSteamMouseState(operation:boolean){return {ok:!operation,backend:state.mouse.backend,provider:'steam',on:false,stateKnown:false,controlAllowed:false,mappingVerified:false,verification:'unobserved',reason:'steamdeck_managed',error:operation?'SteamDeck 虚拟手柄启用时鼠标由 Steam 管理，不能开启 YMCC 模拟鼠标。':''};}
export async function getMouseModeState(){state.mouseReads++;await hooks.mouseRead?.();return nativeMouseSteamBlocked()?{...nativeSteamMouseState(false),mode:state.mouse.mode}:copy(state.mouse);}
function bootMouseExpired(context?:any,current=settings){
 if(!context)return null;
 const modeKeys=['bootModes','bootModesAppliedBoot','bootModesActiveBoot','bootModesStartupResult','bootModesClaimWriteId'];
 const project=(extensions:any)=>Object.fromEntries(modeKeys.filter(key=>extensions?.[key]!==undefined).map(key=>[key,extensions[key]]));
 if(context.input.revision===current.input.revision&&computeInputConfigHash(context.input)===computeInputConfigHash(current.input)&&equalSettingsDocuments(project(context.extensions),project(current.extensions)))return null;
 return {ok:false,on:false,stateKnown:false,controlAllowed:true,reason:'boot_request_expired',superseded:true};
}
function bootSteamStartupExpired(context:any,enabled:boolean){
 if(!context)return null;
 const expired=bootMouseExpired(context);
 const wanted=context?.extensions?.bootModes,latest=settings.extensions?.bootModes,p=wanted?.preparation;
 const mode=wanted&&Object.hasOwn(wanted,'boot')?wanted.boot:'global';
 const sceneEnabled=wanted&&Object.hasOwn(wanted,'bootEnabled')?wanted.bootEnabled:true;
 if(wanted?.schema!==1||typeof sceneEnabled!=='boolean'||!['global','xbox','steam','xbox-steam'].includes(mode)||
    enabled!==(sceneEnabled&&['steam','xbox-steam'].includes(mode)))return {reason:'boot_request_expired'};
 if(!expired)return null;
 if(!wanted||!latest||!p||wanted.schema!==1||latest.schema!==1||
    !Number.isSafeInteger(wanted.revision)||wanted.revision<0||!Number.isSafeInteger(latest.revision)||latest.revision<wanted.revision||
    !Number.isSafeInteger(p.revision)||p.revision<0||p.revision>wanted.revision||
    !['global','xbox','steam','xbox-steam'].includes(wanted.boot)||typeof wanted.bootEnabled!=='boolean'||typeof wanted.current!=='string'||
    latest.boot!==wanted.boot||latest.bootEnabled!==wanted.bootEnabled||latest.current!==wanted.current||p.schema!==1||
    p.mode!==wanted.boot||p.enabled!==wanted.bootEnabled||p.status!=='preparing'||p.steps?.steam?.state!=='waiting'||
    !equalSettingsDocuments(latest.preparation,p)||enabled!==(wanted.bootEnabled&&['steam','xbox-steam'].includes(wanted.boot)))return expired;
 const projected=copy(settings);projected.extensions.bootModes=copy(wanted);return bootMouseExpired(context,projected);
}

export async function setMouseModeEnabled(on:boolean,bootContext?:any){record('mouse.enable',on);await hooks.mouseWrite?.(copy(bootContext),on);const expired=bootMouseExpired(bootContext);if(expired)return expired;if(nativeMouseSteamBlocked())return nativeSteamMouseState(true);state.mouse.on=on;state.mouse.mode=on?'on':'off';state.mouse.provider='ymcc';settings.gamepad.mouseMode=state.mouse.mode;if(on&&faults['mouse.start.partial']){faults['mouse.start.partial']--;throw new Error('injected:mouse.start.partial');}return copy(state.mouse);}
export async function setMouseModePolicy(mode:'on'|'off'|'auto',bootContext?:any){record('mouse.policy',mode);await hooks.mouseWrite?.(copy(bootContext),mode);const expired=bootMouseExpired(bootContext);if(expired)return expired;if(nativeMouseSteamBlocked())return nativeSteamMouseState(true);state.mouse.mode=mode;state.mouse.on=mode==='auto'?state.mouseAutoOn:mode==='on';state.mouse.provider='ymcc';settings.gamepad.mouseMode=mode;return copy(state.mouse);}
export async function setMouseBackend(backend:string,bootContext?:any){record('mouse.backend',backend);await hooks.mouseBackend?.(copy(bootContext),backend);const expired=bootMouseExpired(bootContext);if(expired)return expired;if(nativeMouseSteamBlocked())return nativeSteamMouseState(true);if(!['joyxoff','gamebar'].includes(backend))return {ok:false,backend:state.mouse.backend,reason:'invalid_backend'};state.mouse.backend=backend;state.nativeGamepadFlags.mouseBackend=backend;settings.gamepad.mouseBackend=backend;return copy(state.mouse);}
// Native summon.set updates the live g_summon shortcut cache as well as the saved gamepad section.
export async function summonGet(){return copy(state.nativeGamepadFlags);}
export async function summonSet(flags:any){record('summon.set',flags);await hooks.summonSet?.();for(const [key,value]of Object.entries(flags)){if(key in state.nativeGamepadFlags&&key!=='mouseBackend')state.nativeGamepadFlags[key]=value===true;if(key==='mouseBackend'&&['joyxoff','gamebar'].includes(String(value))){state.nativeGamepadFlags.mouseBackend=value;state.mouse.backend=value;}}settings.gamepad={...settings.gamepad,...copy(state.nativeGamepadFlags)};const result=copy(state.nativeGamepadFlags);if(faults['summon.ack-mismatch']){faults['summon.ack-mismatch']--;const key=Object.keys(flags).find(k=>typeof flags[k]==='boolean');if(key)result[key]=!flags[key];}return result;}
export const getFanFeatureSettings=()=>copy(settings.fan);
export const DEFAULT_FAN_PRESET_CURVES={soft:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:70}],balanced:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:90}],aggressive:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:100}]};
export const fanHostLifecycle={get hasControlIntent(){return state.fanIntent;},get controlReady(){return state.fanReady;},get controlCancellationRevision(){return state.fanCancellationRevision??0;},get controlIntentRevision(){return state.fanIntentRevision??0;},async start(){record('fan.start');await hooks.fanStart?.();state.fanReady=true;return {allowed:true,writeReady:true};},async disable(){record('fan.disable');state.fanCancellationRevision=(state.fanCancellationRevision??0)+1;state.fanIntentRevision=(state.fanIntentRevision??0)+1;state.fanIntent=false;state.fanReady=false;},async close(){record('fan.close');state.fanCancellationRevision=(state.fanCancellationRevision??0)+1;state.fanIntentRevision=(state.fanIntentRevision??0)+1;state.fanIntent=false;state.fanReady=false;},async applyPreset(preset:any,nodes:any){record('fan.preset',{preset,nodes});state.fanIntentRevision=(state.fanIntentRevision??0)+1;state.fanIntent=true;}};
export const batteryCharge={async get(){await hooks.chargeRead?.();return copy(state.charge);},async set(fence:any,limit:any,options:{origin?:string}={}){record('charge.set',{fence,limit});(state.preferenceOrigins??=[]).push({part:'charge',origin:options.origin});state.charge.managed=true;state.charge.requestedLimit=limit;state.charge.currentLimit=limit;return hooks.chargeResult?hooks.chargeResult():{...copy(state.charge),ok:true,verification:'readback'};},async release(options:{origin?:string}={}){record('charge.release');(state.preferenceOrigins??=[]).push({part:'charge',origin:options.origin});state.charge.managed=false;state.charge.requestedLimit=100;state.charge.currentLimit=100;return hooks.chargeResult?hooks.chargeResult():{...copy(state.charge),ok:true,verification:'unmanaged'};}};
export const steamUi={subscribe:(handler:()=>void)=>on('steam.uiStateChanged',handler),async read(){return copy(state.steamUi);},async switchMode(_identity:any,mode:string,options:any){record('steam.switchMode',{mode,userInitiated:options?.userInitiated===true});if(options?.userInitiated!==true)throw Error('Steam switch requires manual intent');state.steamUi.mode=mode;return copy(state.steamUi);},async showBigPicture(_identity:any,options:any){record('steam.showBigPicture',{userInitiated:options?.userInitiated===true});if(options?.userInitiated!==true)throw Error('Steam show requires manual intent');return copy(state.steamUi);}};
export async function launchSteam(options?:{userInitiated:true;isCurrent?:()=>boolean}){const current=options?.isCurrent?.();record('steam.launch',{userInitiated:options?.userInitiated===true,hasCurrentGuard:typeof options?.isCurrent==='function',currentAtDispatch:current,currentMode:settings.extensions.bootModes?.current});if(options?.userInitiated!==true||current!==true)throw Error('Steam launch requires fresh manual intent and current generation');state.steamUi={known:true,running:true,mode:'desktop',pid:4321,birth:'fixture-steam-birth'};}
export const STEAM_APPEARANCE_THEMES=[];
export const steamAppearance={async read(){await hooks.appearanceRead?.();return copy(state.appearance);},async apply(_before:any,states:any,options?:any){record('appearance.apply',states);calls[calls.length-1].origin=options?.origin;return {ok:true};}};
export async function readMonitorOutputPreferences(){return copy(state.monitor);}
export async function saveMonitorOutputPreferences(value:any){record('monitor.save',value);await hooks.monitorSave?.();state.monitor=copy(value);settings.rtss.monitorOutput=copy(value);}
export function onMonitorOutputApplied(listener:(preferences:any,result:any,intent:any)=>unknown){return on('fixture.monitor-applied',value=>listener(value.preferences,value.result,value.intent));}
function monitorReceiptOwner(value:any){return {boot:value.input.startupObservedBoot,writeId:value.extensions?.bootModesClaimWriteId??null};}
export async function emitMonitorOutputApplied(preferences:any,result:any,intent:any={startupReceiptOwner:monitorReceiptOwner(settings)}){await emitMemoryIpc('fixture.monitor-applied',{preferences,result,intent});}
export const monitorOutputCoordinator={async apply(value:any,options:any={}){record('monitor.apply',value);state.monitorApplyOptions=copy(options);const result=faults['monitor.pending']?(faults['monitor.pending']--,{ok:false,applied:'unknown',reason:'fixture monitor not confirmed'}):{ok:true,applied:value.enabled?value.output:'off',reason:'fixture owner confirmed'};void emitMonitorOutputApplied(value,result,{startupReceiptOwner:monitorReceiptOwner(options.bootContext??settings)});return result;}};
export async function screenTouchpadsGet(persona:string){return copy(state.touch[persona]??{persona,layout:'off',enabled:false,ok:true,available:true});}
export async function screenTouchpadsSet(patch:any,persona:string){record('touch.set',{patch,persona});state.touch[persona]={...state.touch[persona],...copy(patch)};return {ok:true,settingsSaved:true};}
export async function screenTouchpadsSaveProfiles(profiles:any,expectedProfiles:any,bootContext?:any){
 record('touch.saveProfiles',{profiles,expectedProfiles,bootContext});
 const context=copy(bootContext),nativeResult=await hooks.touchWrite?.(copy(context));
 if(nativeResult&&nativeResult.settingsSaved!==true)return nativeResult;
 if(context&&(bootMouseExpired(context)||context.boot!==settings.input.startupObservedBoot||context.mode!==settings.extensions?.bootModes?.current||context.revision!==settings.extensions?.bootModes?.revision))
  return {ok:false,settingsSaved:false,reason:'boot-mode-context-changed'};
 const staged=copy(state.touch);
 const keys=new Set(Object.keys(screenTouchpadDefaults('disabled')));
 for(const [persona,patch]of Object.entries(profiles)){
  const previous=normalizeScreenTouchpadSummon(await screenTouchpadsGet(persona));
  staged[persona]={...Object.fromEntries(Object.entries(previous).filter(([key])=>keys.has(key))),...copy(patch as any)};
 }
 state.touch=staged;return {ok:true,settingsSaved:true,reason:'saved',profiles:copy(staged)};
}
export async function setTrayResident(enabled:boolean){record('tray.set',enabled);}
export const shell={async execute(path:string,args:any,bootContext?:any){record('app.launch',{path,args});await hooks.shellExecute?.(path,copy(bootContext));if(bootMouseExpired(bootContext))return false;(state.softwareEffects??=[]).push({op:'launch',path});return true;},async open(path:string){record('app.open',path);},async run(path:string,args:any[],timeout:any){record('app.run',{path,args,timeout});return {exitCode:state.shellExit??0,stdout:'',stderr:''};}};
export const display={async getModes(){await hooks.displayRead?.();return copy(state.displayModes??{current:'',modes:[]});},async setMode(mode:any,options:{origin?:string}={}){record('display.set',mode);(state.preferenceOrigins??=[]).push({part:'display',origin:options.origin});if(state.displayModes)state.displayModes.current=mode.id;return hooks.displayResult?hooks.displayResult():{...copy(mode),ok:true};}};
export const gamepadFeedback={}; export const wakePassword={async set(enabled:boolean,options:{origin?:string}={}){record('wakePassword.set',enabled);(state.preferenceOrigins??=[]).push({part:'wake-password',origin:options.origin});const preferenceSaved=!faults['wakePassword.save'];if(!preferenceSaved)faults['wakePassword.save']--;return {ok:true,known:true,enabled,preferenceSaved};}};
export const getPolicyGame=()=>null;
export const isGameStatusCurrent=()=>true;
export function onContinuousPerformanceApplied(listener:any,onInvalidated?:()=>void){
 const stop=on('performance.applied',raw=>listener(raw.signature,raw.context));
 const invalidated=on('performance.invalidated',()=>onInvalidated?.());
 return()=>{stop();invalidated();};
}
export const getLosslessGameState=async()=>({});
export async function setLosslessScalingEnabled(...args:any[]){record('lossless.set',args.slice(0,2));return {confirmed:true};}
export async function getAutomaticSleepRecovery(){return copy(state.recovery);}
export async function setAutomaticSleepRecovery(enabled:boolean,options:{origin?:string}={}){record('recovery.set',enabled);(state.preferenceOrigins??=[]).push({part:'recovery',origin:options.origin});state.recovery.enabled=enabled;return {ok:true,enabled};}
export async function readGamingHomeConfiguration(){await hooks.gamingHomeRead?.();return {deviceForm:state.deviceForm,startupValue:state.xbox?1:0,startupEnabled:state.xbox,homeApp:state.homeApp};}
export async function readBootSteamStartup(){
 await hooks.steamStartupRead?.();
 if(faults['preparation.steam.read']){faults['preparation.steam.read']--;throw Error('injected:preparation.steam.read');}
 const conflict=!!state.earlyCommand&&state.earlyCommand!=='"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
 const service=state.earlyService??{known:true,installed:true,automatic:true,running:true};
 return {known:true,enabled:state.steam,conflict,command:state.steam?'recognized':conflict?'conflict':'empty-or-absent',
  file:state.earlyFile??{known:true,exists:!!state.earlyCommand,bytes:new TextEncoder().encode(state.earlyCommand?state.earlyCommand+'\r\n':'').length,canonical:state.steam&&!conflict,encoding:state.earlyCommand?'utf-8':'unknown'},
  service:{known:service.known===true,installed:service.installed===true,automatic:service.automatic===true,running:service.running===true}};
}
export async function writeGamingHomeStartup(enabled:boolean,repairHandheld=enabled,selection?:GamingHomeSelection,requireOwner?:()=>Promise<unknown>){
 record('startup.xbox',enabled);
 if(repairHandheld)record('startup.handheld',46);
 if(selection!==undefined)record('startup.home',selection==='xbox'?'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App':'');
 // Real policy, memory registry terminal. Mode tests cannot pass by mocking the omitted home write.
 await setGamingHomeStartup({
  read:async(_root,_key,name)=>name==='DeviceForm'?state.deviceForm:name==='StartupToGamingHome'?(state.xbox?1:0):state.homeApp,
  write:async(_root,_key,name,value)=>{if(requireOwner)await requireOwner();if(name==='DeviceForm')state.deviceForm=value;else if(name==='StartupToGamingHome')state.xbox=value===1;else state.homeApp=value;return true;},
  remove:async()=>{throw Error('unexpected default-policy rollback');},saveBackup:async()=>{await hooks.gamingHomeBackup?.();},
 },enabled,repairHandheld,selection);
}
export async function steamMasterOn(){return state.steam;}
export async function steamMasterSet(enabled:boolean,repairCommand=false,options:BootPreferenceWrite={}){
 const submitted=captureBootPreferenceWrite(options);
 if(submitted.origin!==undefined&&!['ordinary','boot-mode','runtime'].includes(submitted.origin))throw Error('Invalid preference origin');
 if(submitted.origin==='boot-mode'&&!submitted.bootContext)throw Error('BOOT_STEAM_STARTUP_CONTEXT_REQUIRED');
 record('startup.steam',enabled);state.steamStartupOptions=copy(submitted);
 if(submitted.bootContext)await hooks.steamStartupOwnerRead?.();
 if(bootSteamStartupExpired(submitted.bootContext,enabled))throw Error('BOOT_STEAM_STARTUP_REQUEST_EXPIRED');
 await hooks.steamStartupWrite?.(copy(submitted));
 if(submitted.bootContext)await hooks.steamStartupOwnerRead?.();
 if(bootSteamStartupExpired(submitted.bootContext,enabled))throw Error('BOOT_STEAM_STARTUP_REQUEST_EXPIRED');
 state.steam=enabled;state.earlyCommand=enabled?'"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"':'';
 if(repairCommand)record('startup.commandRepair',true);
}
export async function steamRunning(){return false;}
export async function steamAddonExists(_id:string){await hooks.frameStartupRead?.();return state.addon;}
export async function steamAddonSet(id:string,enabled:boolean){record('addon.set',{id,enabled});if(!enabled)await hooks.legacyStartupMutation?.('frameStartup');state.addon=enabled;}
export async function fxExists(_options?:{strict?:boolean}){await hooks.audioStartupRead?.();return state.fx;}
export async function fxSet(enabled:boolean){record('fx.set',enabled);if(!enabled)await hooks.legacyStartupMutation?.('audioStartup');state.fx=enabled;}
export async function readHibernateState(){await hooks.hibernateRead?.();return copy(state.hibernate);}
export async function readHibernateSize(){return state.hibernateSize??40;}
export async function readSleepTimeouts(){return copy(state.timeouts);}
export async function setHibernate(enabled:boolean,options?:any){record('hibernate.set',enabled);state.sleepOrigins.push({part:'hibernate',origin:options?.origin});state.hibernate.enabled=enabled;return {exitCode:0,saved:true,applied:true};}
export async function setHibernateSize(size:number,options?:any){record('hibernate.size',size);state.sleepOrigins.push({part:'size',origin:options?.origin});state.hibernateSize=size;return {saved:true,applied:true};}
export async function setSleepTimeout(kind:string,ac:boolean,minutes:number,options?:any){record('sleep.timeout',{kind,ac,minutes});state.sleepOrigins.push({part:'timeout',origin:options?.origin});state.timeouts[(ac?'ac':'dc')+kind[0].toUpperCase()+kind.slice(1)]=minutes;return {saved:true,applied:true};}
export async function getPowerBtnIdx(ac:boolean){return ac?state.acButton:state.dcButton;}
export async function setPowerBtnIdx(ac:boolean,index:number,options?:any){record('sleep.button',{ac,index});state.sleepOrigins.push({part:'button',origin:options?.origin});state[ac?'acButton':'dcButton']=index;return {saved:true,applied:false};}
export async function sleepGuardGet(){return copy(settings.sleep);}
export async function sleepGuardSetConfig(config:any,options:{origin?:string}={}){record('sleep.config',config);(state.sleepOrigins??=[]).push({part:'guard',origin:options.origin});settings.sleep=copy(config);}
export async function getTdpExecutionCapability(){await hooks.tdpCapabilityRead?.();return {supported:true};}
export async function detectCoreArchitecture(){await hooks.cpuArchitectureRead?.();return {heterogeneous:true};}
async function retireLegacyStartup(args:any){
 const {step,bootContext}=args;
 await hooks.legacyStartupMutation?.(step);
 if(!bootContext||bootMouseExpired(bootContext))throw Error('BOOT_LEGACY_STARTUP_REQUEST_EXPIRED');
 if(step==='audioStartup'){const removed=state.fx;record('fx.set',false);state.fx=false;return {removed};}
 if(step==='frameStartup'){const removed=state.addon;record('addon.set',{id:'steamscale',enabled:false});state.addon=false;return {removed};}
 if(step==='legacyMouse'){
  const value=state.joyStartup;
  if(typeof value!=='string'||!/(?:^|[\\/])joyxoff\.exe(?:["\s]|$)/i.test(value.replace(/^"/,'')))return {removed:false};
  record('joy.startup',false);state.joyStartup=null;return {removed:true};
 }
 throw Error('INVALID_BOOT_LEGACY_STARTUP_TARGET');
}
export async function invoke(op:string,args:any){record('ipc.'+op,args);if(op==='bootMode.legacyStartup.retire')return retireLegacyStartup(args);if(op==='steam.startup.get')return 'nativeStartupReply' in state?copy(state.nativeStartupReply):readBootSteamStartup();if(op==='input.shortcutRuntime.get'){await hooks.shortcutRead?.();const target=settings.input.outputTarget,motion=settings.input.gyroMotion;return copy(state.shortcutRuntime??{active:false,persona:target.persona,virtualEnabled:target.persona!=='disabled'&&(target.buttonMappingEnabled===true||target.gyroEnabled===true),gyroEnabled:target.gyroEnabled===true&&motion.enabled===true});}if(op==='display.setMode')return {...copy(args),ok:true};if(op==='display.getModes')return copy(state.displayModes??{current:'',modes:[]});if(op==='fxsound.setEnabled'){if(args?.enabled!==true)throw Error('boot must only request FxSound ON');await hooks.fxSet?.(copy(args.bootContext));if(bootMouseExpired(args.bootContext))return {ok:false,reason:'boot_request_expired',state:{known:false}};(state.softwareEffects??=[]).push({op:'fx'});await hooks.fxCompleted?.();return {ok:true,copied:false,reused:true,repaired:false,reason:'静默启动',state:{known:true,installed:true,running:true,enabled:true,selected:true,preset:'移动端通用音频优化',output:'fixture FxSound',reason:''}};}if(op==='input.gameOverride.session')return hooks.nativeSession?.() ?? 'fixture-native-session-1';if(op==='bootMode.startup.identity')return hooks.bootIdentity ? hooks.bootIdentity() : 'fixture-windows-boot-1';throw new Error('Unexpected native operation: '+op);}
export const bootPlatform={async read(){return state.platform?copy(state.platform):{known:true,supported:true,active:state.xbox};},async set(_before:any,active:boolean){record('platform.set',active);state.xbox=active;return {ok:true,known:true,supported:true,active};}};
export async function readBootDisplay(){return {brightness:50,resolution:'1280x800',hz:'60'};}
export async function applyBootBrightness(percent:number){record('brightness.set',percent);return hooks.brightnessResult?hooks.brightnessResult():{saved:true,applied:true};}
export async function savePerformanceSchedule(config:any){record('performance.save',config);settings.performanceSchedule=copy(config);return copy(config);}
/** Compatibility port: retain the existing three terminal operations and counters. */
export async function applyPerformanceConfiguration(config:any,frameRates:any,options?:{withReceipt:true}){
 await savePerformanceSchedule(config);
 await updateSettings((draft:any)=>{draft.rtss={...draft.rtss,frameRates:copy(frameRates),templateDefaults:false};});
 const ownership=await restorePerformanceScheduleIfConfigured(),scope=state.performanceScope??(ownership==='none'?'none':'global');
 return options?.withReceipt?{saved:true,ownership,scope,applied:ownership!=='none'&&scope!=='none'}:ownership;
}
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
export const proc={async findExact(path:string){record('process.find',path);await hooks.processRead?.(path);return {found:false};}};
export const fs={async exists(path:string){record('file.exists',path);await hooks.fileRead?.(path);return true;}};
export async function getSleepPowerPlanOptimizationEnabled(){return false;}
export async function setStaticWakeAutoScanEnabled(value:boolean,options:BootPreferenceWrite={}){record('sleep.staticWake',value);(state.sleepOrigins??=[]).push({part:'staticWake',origin:options.origin});if(hooks.sleepStaticWrite)await hooks.sleepStaticWrite(value,options);else settings.sleep={...settings.sleep,staticWakeAutoScanEnabled:value};}
export async function setSleepPowerPlanOptimizationEnabled(value:boolean,options:{origin?:string}={}){record('sleep.plan',value);(state.sleepOrigins??=[]).push({part:'plan',origin:options.origin});return true;}
export async function reactivateCurrentScheme(){record('sleep.reactivate');return true;}
export function captureSleepPowerWrite(){return {powerAdmission:{expectedPowerGeneration:1,expectedPowerNotificationSequence:0}};}
export const deckySidebar={async get(){return await hooks.deckyGet?.()??{enabled:false,phase:'off'};},async setEnabled(enabled:boolean,options:{origin?:string}={}){record('decky.set',enabled);(state.preferenceOrigins??=[]).push({part:'decky',origin:options.origin});return {enabled,phase:enabled?'loader-started':'off',mirrorConnected:enabled,powerReady:true};}};


// Retired startup entries are configuration only; removing them never stops a current mouse owner.
export const registry={
 async read(hive:string,key:string,name:string){record('registry.read',{hive,key,name});if(hive==='HKCU'&&key==='Software\\Microsoft\\GameBar'&&name==='UseNexusForGameBarEnabled'){await hooks.gameBarRead?.();return 'gameBarControllerWindows' in state?state.gameBarControllerWindows:null;}return name==='Joyxoff'?state.joyStartup:name==='ACSettingIndex'||name==='DCSettingIndex'?state.brightnessRaw??null:null;},
 async write(hive:string,key:string,name:string,value:number){record('registry.write',{hive,key,name,value});if(hive!=='HKCU'||key!=='Software\\Microsoft\\GameBar'||name!=='UseNexusForGameBarEnabled'||![0,1].includes(value))throw Error('Unexpected memory registry write');await hooks.gameBarWrite?.();if(state.gameBarWriteRejected)return false;if(!state.gameBarNoReadback)state.gameBarControllerWindows=value;return true;}
};
export const PW={YEMAN:'1cb8b882-a900-4b9f-9bac-99d151e64441'};
export async function detectPowerModeReliable(){return 'powerMode' in state?state.powerMode:'ac';}
export async function getActiveScheme(){return state.activeScheme??PW.YEMAN;}
export async function setAcValueIndex(subGroup:string,setting:string,value:string){record('brightness.power.write',{mode:'ac',subGroup,setting,value});if(!state.brightnessWriteCode&&!state.brightnessWriteNoReadback)state.brightnessRaw=Number(value);return {exitCode:state.brightnessWriteCode??0,stdout:'',stderr:''};}
export async function setDcValueIndex(subGroup:string,setting:string,value:string){record('brightness.power.write',{mode:'dc',subGroup,setting,value});if(!state.brightnessWriteCode&&!state.brightnessWriteNoReadback)state.brightnessRaw=Number(value);return {exitCode:state.brightnessWriteCode??0,stdout:'',stderr:''};}
export async function joySet(enabled:boolean){record('joy.startup',enabled);if(!enabled)await hooks.legacyStartupMutation?.('legacyMouse');state.joyStartup=enabled?'C:\\SOFT\\Joyxoff\\Joyxoff.exe':null;}
export async function deleteTask(name:string){record('legacy.desktop.remove',name);state.legacyDesktop=false;return true;}

// Actual desktop startup preference code owns task intent; only the OS edge is memory.
export async function taskExists(name:string){record('desktop.task.read',name);return name==='桌面模式-开机设置为桌面模式'&&state.legacyDesktop===true;}
export async function toggleTask(name:string,enabled:boolean){record('desktop.task.set',{name,enabled});if(name!=='桌面模式-开机设置为桌面模式')throw Error('Unexpected desktop task');state.legacyDesktop=enabled;return enabled;}
