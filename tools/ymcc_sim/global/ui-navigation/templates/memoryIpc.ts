import {screenTouchpadDefaults,validateScreenTouchpadPatch,screenTouchpadProfile} from '@/bridge/screenTouchpads';
// Opt-in single-owner window-width boundary; all other scenes retain browser mode.
const nativeWidthSeed=new URLSearchParams(location.search).get('nativeWidth')==='1';
export const isNativeRuntime=nativeWidthSeed;
const widthHoldSeed=new URLSearchParams(location.search).get('widthHold');
let heldWidthReply:(()=>void)|undefined;
const editorWidthFixture={owner:'',events:[] as {owner:string;enabled:boolean;ok:boolean}[],waiting:false,holdConsumed:false,releaseReply(){heldWidthReply?.();heldWidthReply=undefined;}};
if(nativeWidthSeed)(window as any).__editorWidthFixture=editorWidthFixture;
const handlers=new Map<string,Set<Function>>(),files=new Map<string,string>();
const gyroPresetFixture={armed:false,conflictNext:false,holdNext:false,pending:false,writes:[] as any[],releases:[] as (()=>void)[],last:null as any,release(){this.releases.shift()?.();}};
(window as any).__gyroPresetFixture=gyroPresetFixture;
let stamp=1;
const fixture=()=> (window as any).__fixture;
const screenTouchpadProfiles:Record<string,any>={};
const screenTouchpadMemory={profiles:screenTouchpadProfiles,writes:[] as any[],failNext:false,holdNext:false,release:undefined as undefined|(()=>void)};
(window as any).__screenTouchpadMemory=screenTouchpadMemory;
function readScreenTouchpadMemory(persona:string){
 const config=screenTouchpadProfiles[persona]??=(structuredClone(screenTouchpadDefaults(screenTouchpadProfile(persona))));
 return {...config,persona,ok:true,available:true,steamDeckAvailable:persona==='steamdeck',ps5Available:persona==='dualsense-edge',visible:config.masterEnabled!==false&&config.enabled,reason:''};
}

// Explicit, per-document legacy inputs. Ordinary navigation scenes keep their original data.
const shortcutSeed=new URLSearchParams(location.search).get('shortcutSeed');
const shortcutSettingsPath='G:\\YMCC-H5-Memory\\yeman-settings.json';
const steamMouseSeed=new URLSearchParams(location.search).get('steamMouseSeed');
if(steamMouseSeed==='standard'||steamMouseSeed==='legacy-steam')files.set(shortcutSettingsPath,JSON.stringify({input:{outputTarget:{persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false},gyroMotion:{enabled:false}},rtss:{monitorOutput:{enabled:true,output:'ymcc'}}}));
const shortcutNativeState:Record<string,boolean>={enabled:true,bDoubleMinimize:true,tdpShortcut:true,fpsShortcut:true,killGame:true,openKeyboard:true,returnDesktop:true,mouseToggle:true,startDoubleF7:shortcutSeed!=='empty'};
if(shortcutSeed){
 if(!['empty','legacy-flag-only','legacy-default','legacy-custom'].includes(shortcutSeed))throw Error('Unknown shortcut memory seed: '+shortcutSeed);
 const retired={id:'native-startDoubleF7',origin:'native-default',defaultKey:'startDoubleF7',enabled:true,actionId:'keyboard.numeric',inputs:[{source:'controller',code:'start'}],trigger:'double',holdMs:500,doubleWindowMs:500,intervalMs:150,params:{key:'F7'}};
 // Retain the old defaultKey even on the custom record: origin decides retirement.
 const rules=shortcutSeed==='legacy-default'?[retired]:shortcutSeed==='legacy-custom'?[retired,{...retired,id:'custom-start-f7',origin:'custom'}]:[];
 const settings={input:{outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true},gyroMotion:{enabled:true},buttonMapping:{rules:{schemaVersion:2,closure:'UNENCLOSED',shortcutRules:rules}}},rtss:{monitorOutput:{enabled:true,output:'ymcc'}}};
 files.set(shortcutSettingsPath,JSON.stringify(settings));
 (window as any).__shortcutFixture={seed:shortcutSeed,initial:structuredClone(settings),read:()=>JSON.parse(files.get(shortcutSettingsPath)!)};
}

// Explicit memory-boundary leaf facts, never execution of the native class.
// Browser time, authoritative state injection, event delivery and reply delivery
// are independent. Only the opt-in global/locked seeds use these controls.
const virtualStartSeed=new URLSearchParams(location.search).get('virtualStartSeed');
const holdInitialVirtualFeed=new URLSearchParams(location.search).get('virtualStartInitialFeed')==='hold';
const baseGamepadState={connected:true,virtualConnected:true,deviceName:'Xbox Controller',physicalName:'Handheld Controller',deviceSource:'xinput',battery:82,capabilities:{vibration:true,lighting:true,gyro:true}};
let lastGamepadState:any=baseGamepadState;
type VirtualReplyTrace={id:number;cmd:string;request:any;requestId:number;atMs:number;status:'pending'|'held'|'fulfilled';response?:any;finishedAtMs?:number};
const heldVirtualReplies=new Map<number,()=>void>();
let virtualStartShortcutRuntime:any={active:false};
const virtualStartFixture=virtualStartSeed?{
 armed:false, rejectNextWrite:false, holdNextStart:false, holdNextClear:false, holdNextFeed:false, sameTargetClearSucceeds:false, heldFeeds:[] as any[], accepted:0, rejected:0,
 backendNowMs:0, nextReplyId:1, feeds:[] as {atMs:number;state:any}[], replies:[] as VirtualReplyTrace[],
 facts:[] as {kind:string;atMs:number;state?:any}[],
 state:{locked:virtualStartSeed==='locked',requestId:1,phase:virtualStartSeed==='locked'?'pending':'idle',persona:'steamdeck',remainingMs:virtualStartSeed==='locked'?30000:0,elapsedMs:0,maxMs:30000,settleMs:10000,expiresAtUnixMs:virtualStartSeed==='locked'?Date.now()+30000:0},
 read:()=>JSON.parse(files.get(shortcutSettingsPath)!),
 produceState(patch:any){
  const previous={...this.state};Object.assign(this.state,patch);
  // This opt-in memory producer supplies explicit deadline leaf facts. It is
  // not an implementation or execution test of the native admission class.
  let budget=Math.max(0,30000-Math.max(0,this.state.elapsedMs));
  if(this.state.remainingMs>0)budget=Math.min(budget,this.state.remainingMs);
  if(['success','failure','cancelled'].includes(this.state.phase))budget=Math.min(budget,10000);
  let expiry=Date.now()+budget;
  if(previous.locked&&previous.requestId===this.state.requestId&&previous.expiresAtUnixMs>0)expiry=Math.min(expiry,previous.expiresAtUnixMs);
  this.state.expiresAtUnixMs=this.state.locked?expiry:0;
 },
 capturePayload(){return {...baseGamepadState,virtualOutput:{startProtection:structuredClone(this.state)}};},
 deliverPayload(payload:any){
  // Delivery never mutates authoritative state or restamps an old payload.
  const copy=structuredClone(payload),state=copy.virtualOutput?.startProtection;
  this.feeds.push({atMs:this.backendNowMs,state});emitNativeEvent('gamepad.state',copy);
 },
 releaseFeed(index=0){const [payload]=this.heldFeeds.splice(index,1);if(payload)this.deliverPayload(payload);},
 publish(patch:any){
  this.produceState(patch);const payload=this.capturePayload();
  if(this.holdNextFeed){this.holdNextFeed=false;this.heldFeeds.push(payload);this.facts.push({kind:'producer-payload-held-with-original-deadline',atMs:this.backendNowMs,state:structuredClone(this.state)});}
  else this.deliverPayload(payload);
 },
 injectStopTerminal(outcome:'success'|'failure',requestId=this.state.requestId,elapsedMs=0){
  // Explicit endpoint receipt leaf only: no stop/cleanup/recovery work runs.
  if(!['success','failure'].includes(outcome)||requestId!==this.state.requestId||this.state.persona!=='disabled'||!this.state.locked||this.state.phase!=='pending'||!Number.isFinite(elapsedMs)||elapsedMs<0)
   throw Error('Stop terminal must belong to the current pending OFF request');
  this.publish({locked:true,phase:outcome,remainingMs:Math.min(10000,Math.max(0,30000-elapsedMs)),elapsedMs});
  this.facts.push({kind:'injected-stop-'+outcome,atMs:this.backendNowMs,state:structuredClone(this.state)});
 },
 advanceBackend(ms:number){
  if(!Number.isFinite(ms)||ms<0)throw Error('Invalid explicit backend clock step');
  this.backendNowMs+=ms;this.facts.push({kind:'backend-clock-step',atMs:this.backendNowMs});
 },
 expireWithoutFeed(){
  // Inject an authoritative expiry fact. Do not call publish or claim that the
  // real native timer ran; native class admission/clock tests live elsewhere.
  Object.assign(this.state,{locked:false,phase:this.state.phase==='pending'?'expired':this.state.phase,remainingMs:0,elapsedMs:0,expiresAtUnixMs:0});
  this.facts.push({kind:'injected-authoritative-expiry-without-feed',atMs:this.backendNowMs,state:structuredClone(this.state)});
 },
 beginReply(cmd:string,request:any){
  const row:VirtualReplyTrace={id:this.nextReplyId++,cmd,request:structuredClone(request),requestId:this.state.requestId,atMs:this.backendNowMs,status:'pending'};
  this.replies.push(row);return row;
 },
 finishReply<T>(row:VirtualReplyTrace,response:T):T{
  row.status='fulfilled';row.finishedAtMs=this.backendNowMs;row.response=structuredClone(response);return response;
 },
 holdReply(row:VirtualReplyTrace){
  row.status='held';return new Promise<void>(resolve=>heldVirtualReplies.set(row.id,()=>{row.status='pending';resolve();}));
 },
 releaseReply(id:number){const release=heldVirtualReplies.get(id);heldVirtualReplies.delete(id);release?.();},
 releaseWrite(){const row=this.replies.find(row=>row.cmd==='settings.write'&&row.status==='held');if(row)this.releaseReply(row.id);},
 releaseClear(){const row=this.replies.find(row=>row.cmd==='input.shortcutRuntime.clear'&&row.status==='held');if(row)this.releaseReply(row.id);},
}:null;
if(virtualStartFixture){
 if(!['global','locked'].includes(virtualStartSeed!))throw Error('Unknown virtual start seed');
 files.set(shortcutSettingsPath,JSON.stringify({input:{revision:10,outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true},gyroMotion:{enabled:true}},rtss:{monitorOutput:{enabled:true,output:'ymcc'}}}));
 (window as any).__virtualStartFixture=virtualStartFixture;
 const initialPayload=virtualStartFixture.capturePayload();
 if(holdInitialVirtualFeed){virtualStartFixture.heldFeeds.push(initialPayload);lastGamepadState={...baseGamepadState};}
 else lastGamepadState=initialPayload;
}
export const getLastGamepadState=()=>lastGamepadState;
export const setLogSink=()=>{};
export function on(event:string,cb:Function){
 if(!handlers.has(event))handlers.set(event,new Set());handlers.get(event)!.add(cb);
 let active=true,replay=event==='gamepad.state'&&!!virtualStartFixture;
 if(replay)queueMicrotask(()=>{if(active&&replay)cb(lastGamepadState)});
 const supersede=()=>{replay=false;};
 if(replay){const original=cb;handlers.get(event)!.delete(cb);cb=(data:any)=>{supersede();original(data)};handlers.get(event)!.add(cb);}
 return()=>{active=false;handlers.get(event)?.delete(cb)};
}
export function emitNativeEvent(event:string,data:any={}){if(event==='gamepad.state')lastGamepadState=data;if(event==='input.shortcutRuntime'&&virtualStartFixture)virtualStartShortcutRuntime=data;for(const cb of handlers.get(event)||[])cb(data);}
export class IpcRemoteError extends Error {}
const updateCacheFixture={clears:0,hold:false,release:null as null|(()=>void),next:{ok:true,busy:false,bytesFreed:4096,message:'更新缓存已清理'}};
(window as any).__updateCacheFixture=updateCacheFixture;
// Explicit charging transport port for the real settings route; no hardware or alternate policy.
const batteryChargeFixture={calls:[] as {cmd:string;args:any}[],revision:0,state:{schema:1,ok:true,supported:true,batteryPresent:true,model:'ClawCG3EM',family:'MSI',kind:'firmware-percent',choices:[50,55,60,65,70,75,80,85,90,95,100],currentLimit:100 as number|null,requestedLimit:null as number|null,managed:false,verification:'readback',fence:'a'.repeat(64),reason:''}};
(window as any).__batteryChargeFixture=batteryChargeFixture;
// Independent read-only native power observation. Window/event delivery never
// edits this state: missed commit and late-response regressions control both sides.
const uiPowerFixture={state:{phase:'ready',generation:1,hardwareWritesAllowed:true,inputReady:true,resumeReady:false,hibernateAvailable:true},calls:0,holdNext:false,failNext:false,pending:[] as {value:any;resolve:(value:any)=>void}[],release(){const item=this.pending.shift();item?.resolve(item.value);}};
(window as any).__uiPowerFixture=uiPowerFixture;
// Explicit Xbox fullscreen LS boundary. The real quickapp owner, XML parser,
// top switch and gamepad engine execute; OS/file/process actions are memory only.
const losslessSeed=new URLSearchParams(location.search).get('trainerMenu')==='lossless-xbox';
const losslessFile='Q:\\LsMemory\\Lossless Scaling\\Settings.xml';
const losslessExe='C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe';
const losslessMemory={running:false,stamp:'731',writes:[] as string[],launches:0,closes:0,focus:[] as any[],
 read:()=>files.get(losslessFile)||''};
if(losslessSeed){
 files.set(losslessFile,'<Settings><Unknown>retained</Unknown>\n  <GameProfiles>\n    <Profile><Title>Other</Title><Path>C:/Games/Other.exe</Path><AutoScale>true</AutoScale></Profile>\n  </GameProfiles>\n</Settings>');
 (window as any).__losslessMemory=losslessMemory;
}
export async function invoke(cmd:string,args:any={}){
 if(cmd==='lossless.watch')return true;
 if(cmd==='lossless.detect')return {known:true,running:losslessSeed&&losslessMemory.running,readable:false,pid:losslessMemory.running?731:0,processCreated:losslessMemory.stamp,path:losslessExe,button:'',status:'',reason:'UIA unavailable',configRevision:0};
 if(losslessSeed){
  if(cmd==='lossless.ensureRunning'){const started=!losslessMemory.running;if(started){losslessMemory.running=true;losslessMemory.launches++;}return {ok:true,pid:731,processCreated:losslessMemory.stamp,path:losslessExe,started,reused:!started,reason:'',win32Error:0};}
  if(cmd==='lossless.ensureScaling')return {ok:false,enabled:null,invoked:false,pending:false,cancelled:false,reason:'no-focus-automation-unavailable'};

  if(cmd==='fs.exists'&&(args.path===losslessExe||String(args.path).startsWith('Q:\\LsMemory')))return args.path===losslessExe||files.has(args.path);
  if(cmd==='fs.readTextFile'&&String(args.path).startsWith('Q:\\LsMemory'))return files.get(args.path)||'';
  if(cmd==='fs.writeTextFileAtomic'&&String(args.path).startsWith('Q:\\LsMemory')){
   if(args.path===losslessFile&&losslessMemory.running)throw Error('LS_CONFIG_REPLACED_WHILE_RUNNING');
   losslessMemory.writes.push(args.path);files.set(args.path,args.content);return true;
  }
  if(cmd==='proc.findExact'&&args.path===losslessExe)return {found:losslessMemory.running,pid:losslessMemory.running?731:0};
  if(cmd==='process.identity'&&args.pid===731)return {valid:losslessMemory.running,pid:731,processCreated:losslessMemory.stamp,path:losslessExe};
  if(cmd==='shell.execute'&&args.program==='cmd'&&args.args?.includes(losslessExe)){losslessMemory.running=true;losslessMemory.launches++;return true;}
  if(cmd==='game.losslessReturnTarget'){losslessMemory.focus.push(structuredClone(args));return {ok:true};}
  if(cmd==='shell.run'&&args.args?.includes('Write-Output $env:LOCALAPPDATA'))return {exitCode:0,stdout:'Q:\\LsMemory',stderr:''};
  if(cmd==='shell.run'&&args.args?.includes('-EncodedCommand')){
   const script=new TextDecoder('utf-16le').decode(Uint8Array.from(atob(args.args.at(-1)),c=>c.charCodeAt(0)));
   if(script.includes('UIAutomationClient'))return {exitCode:1,stdout:'',stderr:'Xbox fullscreen UIAutomation unavailable'};
   if(script.includes('YMCC_LOSSLESS_CLOSE')){losslessMemory.running=false;losslessMemory.closes++;return {exitCode:0,stdout:'',stderr:''};}
   throw Error('UNEXPECTED_LOSSLESS_TEST_SHELL');
  }
 }

 if(cmd==='power.lifecycle'){
  const port=uiPowerFixture;port.calls++;if(port.failNext){port.failNext=false;throw Error('memory native power read failed');}
  const value=structuredClone(port.state);if(port.holdNext){port.holdNext=false;return await new Promise(resolve=>port.pending.push({value,resolve}));}return value;
 }
 if(cmd.startsWith('battery.charge.')){
  const b=batteryChargeFixture;b.calls.push({cmd,args:structuredClone(args)});
  if(cmd==='battery.charge.get'||cmd==='battery.charge.refresh')return structuredClone(b.state);
  if(cmd==='battery.charge.set'){
   if(!b.state.supported||args.fence!==b.state.fence||!b.state.choices.includes(args.target))throw Error('Invalid charging memory request');
   Object.assign(b.state,{currentLimit:args.target,requestedLimit:args.target,managed:true,fence:(++b.revision).toString(16).padStart(64,'0')});
   return structuredClone(b.state);
  }
  throw Error('Unexpected charging memory operation '+cmd);
 }
 if(cmd==='app.updateState')return {phase:'idle',downloadActive:false};
 if(cmd==='app.retireUpdateDownload')return {retired:true,active:false,activeOperation:'',background:false};
 if(cmd==='app.clearUpdateCache'){
  updateCacheFixture.clears++;
  if(updateCacheFixture.hold)await new Promise<void>(resolve=>updateCacheFixture.release=resolve);
  return {...updateCacheFixture.next};
 }
 (window as any).__previewCalls??=[];(window as any).__previewCalls.push({cmd,args});
 if(cmd==='screenTouchpads.get')return readScreenTouchpadMemory(args.persona);
 if(cmd==='screenTouchpads.set'){
  if(screenTouchpadMemory.holdNext){screenTouchpadMemory.holdNext=false;await new Promise<void>(resolve=>screenTouchpadMemory.release=resolve);}
  if(screenTouchpadMemory.failNext){screenTouchpadMemory.failNext=false;return {...readScreenTouchpadMemory(args.persona),ok:false,settingsSaved:false,reason:'settings-write-failed'};}
  if(!['disabled','steamdeck','dualsense-edge','elite'].includes(args.persona)||!validateScreenTouchpadPatch(args.config))throw Error('Invalid screen touchpad memory request');
  readScreenTouchpadMemory(args.persona);Object.assign(screenTouchpadProfiles[args.persona],structuredClone(args.config));
  if(args.config.layout!==undefined)screenTouchpadProfiles[args.persona].enabled=args.config.layout!=='off';
  screenTouchpadMemory.writes.push(structuredClone(args));
  const value={...readScreenTouchpadMemory(args.persona),settingsSaved:true};emitNativeEvent('screenTouchpads.updated',value);return value;
 }

 if(cmd==='window.controllerEditorWidth'&&nativeWidthSeed){
  const owner=String(args.owner||''),enabled=args.enabled===true;
  const ok=!!owner&&(enabled?(!editorWidthFixture.owner||editorWidthFixture.owner===owner):editorWidthFixture.owner===owner);
  if(ok)editorWidthFixture.owner=enabled?owner:'';
  editorWidthFixture.events.push({owner,enabled,ok});
  const receipt={ok,active:!!editorWidthFixture.owner,width:editorWidthFixture.owner?1160:580,baseWidth:580,maxWidth:1160};
  if(ok&&enabled&&!editorWidthFixture.holdConsumed&&((widthHoldSeed==='parent'&&owner.startsWith('radial-editor-'))||(widthHoldSeed==='recorder'&&owner.startsWith('controller-editor-')))){
   editorWidthFixture.holdConsumed=true;editorWidthFixture.waiting=true;
   await new Promise<void>(resolve=>{heldWidthReply=()=>{editorWidthFixture.waiting=false;resolve();};});
  }
  return receipt;
 }
 // A bounded slow-save fixture exercises real Vue busy/disabled focus loss.
 if(cmd==='settings.write'&&fixture()?.scrollSaveDelay)await new Promise(resolve=>setTimeout(resolve,fixture().scrollSaveDelay));
 if(cmd==='radial.testing.configure'){if(typeof args.enabled!=='boolean'||Object.keys(args).length!==1)throw Error('Invalid testing contract');fixture().radialTestingEnabled=args.enabled;return {ok:true,enabled:args.enabled,available:args.enabled};}
 if(cmd==='radial.configure')return {ok:!!fixture().radialTestingEnabled,enabled:!!fixture().radialTestingEnabled&&args.enabled};
 if(cmd==='radial.status')return fixture()?.radialSourceStatus||{fullScreenInstalled:true,rawRegistered:true,rawObservedDevices:0,rawState:'registered-awaiting-touch-report'};
 if(cmd==='window.gestureSummonStatus')return {enabled:true,available:true};
 if(cmd==='logs.domainGetEnabled')return {gyro:!!fixture().loggingEnabled,virtual:!!fixture().loggingEnabled};
 if(cmd==='logs.domainSetEnabled'){fixture().loggingEnabled=args.enabled;return {ok:true,gyro:!!args.enabled,virtual:!!args.enabled};}
 if(cmd==='logs.deleteCurrent'){
  const before=!!fixture().loggingEnabled;fixture().logTrace=['capture','pause','ack'];fixture().loggingEnabled=false;
  await new Promise(resolve=>setTimeout(resolve,160));
  fixture().logTrace.push('clear','restore');fixture().loggingEnabled=before;
  return {protocol:'ymcc.current-diagnostic-logs.v1',ok:true,busy:false,captured:true,pauseAcknowledged:true,deleteAttempted:true,restored:true,originalGates:{gyro:before,virtual:before},restoredGates:{gyro:before,virtual:before},files:fixture().currentLogFiles.map(name=>({name,state:'deleted',error:''})),pauseError:'',deleteError:'',restoreError:''};
 }
 if(cmd==='logs.exportAll')return {ok:true,path:'G:/YMCC-H5-Memory/Logs.zip',count:16};
 if(cmd==='background.get')return fixture().backgroundState||{enabled:false,url:''};
 if(cmd==='dialog.openFile')return fixture().selectedBackground||null;
 if(cmd==='background.install'){fixture().backgroundState=fixture().selectedBackgroundState||{enabled:true,kind:'image',file:args.source,url:''};return fixture().backgroundState;}
 if(cmd==='background.clear'){fixture().backgroundState={enabled:false,url:''};return fixture().backgroundState;}
 if(cmd==='settings.location')return {directory:'G:\\YMCC-H5-Memory',file:'G:\\YMCC-H5-Memory\\yeman-settings.json',backup:'G:\\YMCC-H5-Memory\\yeman-settings.json.bak'};
 if(cmd==='settings.read')return {stamp:String(stamp),unchanged:false,content:files.get(args.path)||JSON.stringify({input:{buttonMapping:{rules:{schemaVersion:2,shortcutRules:fixture().navigationShortcuts||[]}},outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true},gyroMotion:{enabled:true}},rtss:{monitorOutput:{enabled:true,output:'ymcc'}}})};
 if(cmd==='settings.write'){
  if(gyroPresetFixture.armed&&typeof args.expectedInputRevision==='number'){
   const next=JSON.parse(args.content),current=JSON.parse(files.get(args.path)||args.content);
   if(gyroPresetFixture.conflictNext){gyroPresetFixture.conflictNext=false;current.input.revision=args.expectedInputRevision+1;files.set(args.path,JSON.stringify(current));stamp++;gyroPresetFixture.writes.push({kind:'conflict',expected:args.expectedInputRevision,actual:current.input.revision});return {ok:false,reason:'revision-conflict',content:JSON.stringify(current)};}
   if(gyroPresetFixture.holdNext){gyroPresetFixture.holdNext=false;gyroPresetFixture.pending=true;await new Promise<void>(resolve=>gyroPresetFixture.releases.push(()=>{gyroPresetFixture.pending=false;resolve();}));}
   files.set(args.path,args.content);stamp++;gyroPresetFixture.last=structuredClone(next);gyroPresetFixture.writes.push({kind:'committed',revision:next.input.revision,preset:next.input.gyroMotion?.preset});return {ok:true,content:args.content};
  }
  if(virtualStartFixture?.armed){
   const next=JSON.parse(args.content),current=virtualStartFixture.read();
   const reply=virtualStartFixture.beginReply(cmd,args);
   const target=next.input?.outputTarget,tuple=(value:any)=>JSON.stringify([value?.persona,value?.buttonMappingEnabled,value?.gyroEnabled]);
   const changed=tuple(target)!==tuple(current.input?.outputTarget);
   if(virtualStartFixture.rejectNextWrite||(changed&&virtualStartFixture.state.locked)){
    virtualStartFixture.rejectNextWrite=false;virtualStartFixture.rejected++;
    // An external blocker observed after an unlocked request must have a new
    // identity. Native cannot revive a completed request; keep an active id.
    if(!virtualStartFixture.state.locked)virtualStartFixture.state.requestId++;
    virtualStartFixture.produceState({locked:true,phase:'pending',remainingMs:30000,elapsedMs:0});
    reply.requestId=virtualStartFixture.state.requestId;
    return virtualStartFixture.finishReply(reply,{ok:false,reason:'virtual-start-protection-active',content:JSON.stringify(current),startProtection:{...virtualStartFixture.state}});
   }
   files.set(args.path,args.content);stamp++;
   if(changed){
    // Every explicit ON/switch/OFF intent gets a new pending admission. A
    // settings commit is not proof that the requested start or stop finished.
    virtualStartFixture.accepted++;
    virtualStartFixture.publish({locked:true,phase:'pending',requestId:virtualStartFixture.state.requestId+1,persona:target.persona,remainingMs:30000,elapsedMs:0});
    reply.requestId=virtualStartFixture.state.requestId;
    if(virtualStartFixture.holdNextStart){virtualStartFixture.holdNextStart=false;await virtualStartFixture.holdReply(reply);}
   }
   // Preserve the original fixture contract: a held write returns the actual
   // current document, including any newer confirmed document.
   return virtualStartFixture.finishReply(reply,{ok:true,content:files.get(args.path)});
  }
  files.set(args.path,args.content);stamp++;return {ok:true,content:args.content};
 }
 if(cmd==='input.shortcutRuntime.clear'&&virtualStartFixture?.armed){
  const reply=virtualStartFixture.beginReply(cmd,args);
  // Capture this reply before holding it, so a late old protection payload can
  // really arrive after a newer request. The private trace retains both facts.
  // Explicit leaf: native same-target clear can succeed while admission is
  // locked. Legacy callers get true; opt-in callers also observe protection.
  const response=virtualStartFixture.state.locked&&!virtualStartFixture.sameTargetClearSucceeds
   ?{ok:false,reason:'virtual-start-protection-active',startProtection:{...virtualStartFixture.state}}
   :args.observeProtection===true?{ok:true,startProtection:{...virtualStartFixture.state}}:true;
  if(virtualStartFixture.holdNextClear){virtualStartFixture.holdNextClear=false;await virtualStartFixture.holdReply(reply);}
  return virtualStartFixture.finishReply(reply,response);
 }
 if(cmd==='settings.reset'){files.clear();stamp++;return {ok:true,backups:[],protectedDataRecovered:false};}
 if(cmd==='app.dataDir'||cmd==='app.exeDir')return 'memory://YMCC';
 if(cmd==='app.pid')return 123;
 if(cmd==='app.version')return '2026.10';
 if(cmd.startsWith('dialog.'))return null;
 if(cmd==='fs.readTextFile')return files.get(args.path)||'{}';
 if(cmd==='fs.writeTextFile'||cmd==='fs.writeTextFileAtomic'){files.set(args.path,args.content);return true;}
 // Optional read-only CE capability probe for trainer-popup layout cases.
 const trainerMenuSeed=new URLSearchParams(location.search).get('trainerMenu');
 if(trainerMenuSeed && cmd==='app.powerControlDir')return 'memory://YMCC/PowerControl';
 if(trainerMenuSeed && cmd==='fs.exists' && /[\\/]MemoryScan[\\/]MemoryScanHost\.exe$/i.test(String(args.path)))return trainerMenuSeed!=='no-scan';
 if(trainerMenuSeed && cmd==='shell.hidden' && /[\\/]MemoryScan[\\/]MemoryScanHost\.exe$/i.test(String(args.program)) && args.args?.[0]==='--probe'){
  if(trainerMenuSeed==='delayed-scan')await new Promise(resolve=>setTimeout(resolve,180));
  files.set(String(args.args[1]),JSON.stringify({engine:'cheat-engine',available:true}));
  return {ok:true,pid:9876};
 }
 if(cmd==='fs.exists')return true;
 if(cmd==='fs.readDir')return [];
 if(cmd==='fs.getFileIcon')return null;
 if(cmd==='fs.stat')return {exists:true,size:1024,isFile:true,isDirectory:false};
 if(cmd==='http.get'||cmd==='http.request')return {status:200,body:'{}'};
 if(cmd==='shell.exec'||cmd==='shell.execute'||cmd.startsWith('proc.'))return {exitCode:0,stdout:'',stderr:''};
 // Known in-memory mouse readback; unknown/Steam ownership remains explicitly overridable.
 // The production control requires both stateKnown and controlAllowed before it is focusable.
 if(cmd.startsWith('mouseMode.')){
  const f=fixture();const sd=f?.mousePersona==='steamdeck';
  const base={ok:true,backend:'joyxoff',provider:sd?'steam':'ymcc',mode:'off',scene:'ymcc',on:false,controlAllowed:!sd,stateKnown:!sd,mappingVerified:false,verification:'backend-state',joyxoffOn:false,gamebarOn:false,joyxoffAvailable:true,gamebarAvailable:true,...f?.mouseModeState};
  if(sd)Object.assign(base,{provider:'steam',on:false,controlAllowed:false,stateKnown:false,verification:'unobserved'});
  if(cmd==='mouseMode.get')return base;
  if(cmd==='mouseMode.history')return [];
  if(cmd==='mouseMode.releaseJoyxoff')return {ok:true,closed:base.on,stateKnown:true};
  if(sd)return {...base,ok:false,reason:'steamdeck_managed',error:'Steam 管理，仅显示状态'};
  if(cmd==='mouseMode.set'||cmd==='mouseMode.toggle'||cmd==='mouseMode.setPolicy'){
   if(base.provider==='steam'){const on=cmd==='mouseMode.set'?args.on:cmd==='mouseMode.setPolicy'?args.mode!=='off':!base.on;const state={...base,on,mode:on?'auto':'off',scene:'steam',verification:'component-readback'};if(f){f.mouseModeState=state;f.mouseWrites=(f.mouseWrites??0)+1;}return state;}
   const mode=cmd==='mouseMode.setPolicy'?args.mode:cmd==='mouseMode.set'?(args.on?'on':'off'):base.mode==='off'?'auto':base.mode==='auto'?'on':'off';
   const state={...base,mode,on:mode==='on',scene:mode==='auto'?'ymcc':mode==='on'?'manual':'off',verification:mode==='on'?'process-liveness':'backend-state'};
   if(f){f.mouseModeState=state;f.mouseWrites=(f.mouseWrites??0)+1;}return state;
  }
  if(cmd==='mouseMode.setBackend'){
   if(args.backend==='steam')return {...base,ok:false,reason:'steam-desktop-mouse-retired'};
   if(f?.mouseBackendThrow)throw new Error('IPC command timed out: mouseMode.setBackend');
   const state={...base,backend:args.backend,provider:'ymcc',on:false,mode:'off',scene:'ymcc',controlAllowed:true,stateKnown:true};
   if(f){f.mouseModeState=state;f.mouseWrites=(f.mouseWrites??0)+1;}return state;
  }
 }

 if((cmd==='steam.settings.get'||cmd==='steam.settings.set')&&(args.scope==='mouse'||'mousePercent' in args)){const f=fixture();if(cmd==='steam.settings.get'&&f?.steamMouseHoldRead){f.steamMouseReadAttempts=(f.steamMouseReadAttempts??0)+1;await new Promise<void>(resolve=>{f.steamMouseReleaseRead=resolve;});}if('mousePercent' in args&&f){f.steamMousePercent=args.mousePercent;f.mouseSensitivityWrites=(f.mouseSensitivityWrites??0)+1;}return {ok:true,available:true,percent:f?.steamMousePercent??100,pending:false,steamRunning:true,liveAvailable:true,appliedLive:true};}
 if(cmd==='steam.settings.get'||cmd==='steam.settings.set')return {ok:true,available:true,pending:false,position:1,detail:2,scale:100,saturation:100,opacity:80};
 if(cmd==='display.getModes')return {current:'1280x800',modes:[{width:1280,height:720,refreshRate:60},{width:1280,height:800,refreshRate:60}]};
 if(cmd==='input.shortcutRuntime.get')return virtualStartFixture?virtualStartShortcutRuntime:{active:false};
 if(cmd==='steam.startup.get')return {known:true,enabled:false,conflict:false};
 if(cmd==='steam.startup.set')return {ok:true,enabled:args.enabled===true};
 if(cmd==='deckySidebar.get')return fixture()?.deckyState||{enabled:false,pid:0,revision:1,phase:'disabled',steamKnown:true,steamRunning:true};
 if(cmd==='deckySidebar.repairConnection')return {ok:true,requestDispatched:true,mutated:true};
 if(cmd==='steam.ui.launch')return {ok:true,dispatched:true};
 if(cmd==='steam.uiState')return fixture()?.steamUiState||{running:true,mode:'bigpicture',known:true,pid:100,birth:'456',reason:''};
 if(cmd==='steam.ui.showBigPicture')return {ok:true};
 if(cmd==='steamGameLink.get'||cmd==='steam.gameLink'){
  if(fixture()?.steamGameLinkError)throw Error('Memory Steam link read failed');
  const value={schemaVersion:1,phase:'observed',reason:'memory-fixture',readOnly:true,linked:false,contactConnected:false,transportConnected:true,active:true,fresh:true,repairAuthorized:false,gameInputVerified:false,continueButtonVerified:false,steamWrites:0,gameLaunches:0,focusWrites:0,deviceWrites:0,sleepWrites:0,...fixture()?.steamGameLink};
  if(fixture()?.steamGameLinkHold)return new Promise(resolve=>{(window as any).__releaseSteamGameLink=()=>resolve(value);});
  return value;
 }
 if(cmd==='game.detect'||cmd==='gameproc.detect'||cmd==='game.status'||cmd==='gameproc.status')return fixture()?.game||null;
 if(cmd.includes('gamepad.state'))return getLastGamepadState();
 if(cmd==='shortcut.recording.set')return {active:args.active===true,reason:'simulator'};
 // Explicit read-only status port; ordinary scenes keep unknown capability facts.
 if(cmd==='oem.keys.status')return structuredClone(fixture()?.oemInputUsage??null);
 if(cmd==='oem.keys.get'&&new URLSearchParams(location.search).get('radialRecorder')==='1')return {familyId:'simulator',supported:true,keys:[{keyId:'back1',label:'背键 1',backIndex:1,triggerCapability:'full',captureImplemented:true}]};
 if(cmd==='oem.keys.get')return {keys:[],devices:[],source:'preview',ready:true};
 if(shortcutSeed&&cmd==='summon.get')return {...shortcutNativeState};
 if(shortcutSeed&&cmd==='summon.set'){Object.assign(shortcutNativeState,args);return {...shortcutNativeState};}
 if(steamMouseSeed&&cmd==='summon.get')return {enabled:true,bDoubleMinimize:true,tdpShortcut:true,fpsShortcut:true,killGame:true,openKeyboard:true,returnDesktop:true,mouseToggle:true,mouseBackend:steamMouseSeed==='legacy-steam'?'steam':fixture()?.mouseModeState?.backend??'joyxoff'};
 if(cmd==='summon.get')return {enabled:true,bDoubleMinimize:true,tdpShortcut:true,fpsShortcut:true,killGame:true,openKeyboard:true,returnDesktop:true,mouseToggle:true};
 if(cmd==='gamepad.feedbackCapabilities')return {ledAvailable:true,brightnessAvailable:true,vibrationAvailable:true,familySupportsLighting:true,lightingBackendImplemented:true,physicalPrimarySlot:0,supportsSecondColor:true,ledEffects:['solid','breathing','rainbow'],ledPresets:[],vibrationReason:'ok',ledReason:'ok'};
 if(cmd==='sys.info')return {totalMemoryGB:32,memoryGB:32,totalMemoryBytes:34359738368,batteryPresent:true,processor:'AMD Ryzen Z2 Extreme',cpu:'AMD Ryzen Z2 Extreme',systemManufacturer:'LENOVO',systemModel:'Legion Go 2',hasBattery:true};
 if(cmd==='power.activeScheme')return '1cb8b882-a900-4b9f-9bac-99d151e64441';
 if(cmd==='power.hibernateState')return {ok:true,enabled:true,supported:true,known:true,available:true,hibernateEnabled:true,enabledKnown:true,supportedKnown:true,source:'registry'};
 if(cmd==='sleepGuard.get')return {enabled:true,pauseGameOnSleep:true,retryOnEntryFailure:true,retryOnNonUserWake:true,autoWakeRecovery:true,wakePasswordEnabled:false};
 if(cmd==='registry.read')return args.name==='DeviceForm'?46:0;
 if(cmd==='shell.run')return {exitCode:0,stdout:'',stderr:''};
 if(cmd==='gamepad.getCapabilities')return {vibration:true,lighting:true,gyro:true};
 if(cmd.includes('power.source'))return {mode:'ac',ac:true,hasBattery:true,percent:82};
 if(cmd==='os.totalMemoryGB')return 32;
 if(cmd.includes('hibernate')&&/state|State|read|get/.test(cmd))return {supported:true,enabled:true,known:true,percent:40};
 if(cmd==='os.activeScheme')return '381b4222-f694-41f0-9685-ff5bb260df2e';
 if(cmd.includes('wakePassword'))return {ok:true,known:true,enabled:!!args.enabled,mixed:false,ac:Number(!!args.enabled),dc:Number(!!args.enabled),win32Error:0};
 if(false)return {ok:true,known:true,enabled:false,mixed:false};
 if(cmd==='display.modes')return [{width:1280,height:720,refreshRate:60},{width:1280,height:800,refreshRate:60}];
 if(cmd==='display.topology')return {displays:[{id:'1',primary:true,width:1280,height:800}],primary:'1'};
 if(/\.readDir$|\.list$|\.devices$/.test(cmd))return [];
 if(/\.exists$|\.set$|\.write$|\.enabled$/.test(cmd))return true;
 if(/\.get$|\.state$|\.status$/.test(cmd))return {ok:true,enabled:false,known:true,supported:true,installed:true};
 return {};
}
