// Exact shipped TSX/queues; strict browser timers, memory-only transport and clock.
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {root,validation} from './fixtures/decky_task_paths.mjs';
const {transformSync}=createRequire(path.join(root,'package.json'))('esbuild');
function load(file,modules={},globals={}){const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(file,'utf8'),{loader:file.endsWith('tsx')?'tsx':'ts',format:'cjs',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment'}).code,{module,exports:module.exports,structuredClone,console,...globals,require:n=>{assert(Object.hasOwn(modules,n),'Unexpected import '+n);return modules[n];}});return module.exports;}
const model=load(path.join(root,'decky-plugin/src/performanceModel.ts')),queue=load(path.join(root,'decky-plugin/src/performanceIntentQueue.ts')),fan=load(path.join(root,'decky-plugin/src/fanChoiceDebounce.ts'));
function harness(real=false,withEntryFocus=false){let now=0,id=0,cursor=0,visible=true;let hooks=[],effects=[],cleanups=new Map();const listeners=new Set(),timers=new Map(),mutations=[],requests=[],views=new Map();let activeView='main';
const state={connected:true,busy:false,notice:'',snapshot:{runId:'a'.repeat(32),revision:1,generation:1,ready:true,powerSource:'ac',game:null,actions:{game:false,performance:true,frames:true,fan:true,global:true,monitor:true},performance:{version:1,side:'ac',scope:'global',mode:'auto',sourceRevision:'1',enabled:true,controls:[{key:'tdp',label:'TDP 功耗',kind:'slider',min:5,max:30,step:1,value:20,enabled:true}]},frames:{dedicated:false,pair:{ac:{fps:60,lastFps:60,ceiling:120},dc:{fps:60,lastFps:60,ceiling:60}}},fan:{supported:true,enabled:false,preset:'balanced',canToggle:true,choices:fan.FAN_CHOICES.slice(1)},monitor:{enabled:true},global:{fields:{gyroPreset:{value:'fps',supported:true,choices:[{data:'fps',label:'FPS\ufffd\ufffd\ufffd'}]},speedFactor:{value:'1',supported:false,choices:[{data:'1',label:'1\ufffd\ufffd'}]}}}}};
const entryFrames=new Map(),entryListeners=new Map(),entryCalls=[];let nextEntryFrame=0;
const entryHost={requestAnimationFrame(fn){const id=++nextEntryFrame;entryFrames.set(id,fn);return id;},cancelAnimationFrame(id){entryFrames.delete(id);},getComputedStyle(){return {visibility:entryDom.visibility};}};
const entryDoc={defaultView:entryHost,addEventListener(name,fn){const listeners=entryListeners.get(name)??new Set();listeners.add(fn);entryListeners.set(name,listeners);},removeEventListener(name,fn){entryListeners.get(name)?.delete(fn);}};
const entryDom={ownerDocument:entryDoc,isConnected:true,displayed:true,visibility:'visible',getClientRects(){return this.displayed?[{}]:[];},getBoundingClientRect(){return {width:250,height:50};}};
const entryNav={TakeFocus(){entryCalls.push('tdp-row');}};
const attachEntryRefs=(type,props)=>{if(!withEntryFocus)return;if(type==='PerformanceControls'){if(props.entryElementRef)props.entryElementRef.current=entryDom;if(props.entryNavRef)props.entryNavRef.current=entryNav;}if(type==='div'&&props?.className?.startsWith('ymcc-decky-sidebar')&&props.ref)props.ref.current=entryDom;};
const react={Fragment:'Fragment',createElement:(type,props,...children)=>{attachEntryRefs(type,props);return {type,props:props||{},children};},useState(initial){const i=cursor++;if(!(i in hooks))hooks[i]=initial;const ownerHooks=hooks;return[ownerHooks[i],v=>{ownerHooks[i]=typeof v==='function'?v(ownerHooks[i]):v;}];},useRef(initial){const i=cursor++;return hooks[i]??={current:initial};},useEffect(fn,deps){const i=cursor++,old=hooks[i];if(!old||!deps||deps.some((v,i)=>v!==old[i])){hooks[i]=deps;effects.push(()=>{cleanups.get(i)?.();const cleanup=fn();if(typeof cleanup==='function')cleanups.set(i,cleanup);else cleanups.delete(i);});}}};
const schedule=function(fn,ms){if(this!==undefined)throw TypeError('Illegal invocation');timers.set(++id,{fn,at:now+ms});return id;},cancel=function(i){if(this!==undefined)throw TypeError('Illegal invocation');timers.delete(i);};
class Client{setPassiveDemand(){}waitForBootstrap(){return()=>{};}snapshot(){return state;}subscribe(fn){listeners.add(fn);fn(state);return()=>listeners.delete(fn);}setVisible(){}setIntentPending(){}bootstrapChanged(){}dispose(){}mutate(command,args,expected){assert.equal(expected.revision,state.snapshot.revision,'stale transport receipt');mutations.push({command,args:structuredClone(args),revision:expected.revision});return Promise.resolve();}}
const sockets=[];class Socket{constructor(){this.listeners=new Map();this.opened=false;sockets.push(this);}addEventListener(name,fn){const a=this.listeners.get(name)||[];a.push(fn);this.listeners.set(name,a);}emit(name,data){if(name==='open')this.opened=true;for(const fn of this.listeners.get(name)||[])fn(name==='message'?{data:JSON.stringify(data)}:{});}send(raw){const v=JSON.parse(raw);requests.push(v);if(v.command!=='snapshot')mutations.push({command:v.command,args:v.args,revision:v.revision,id:v.id});}close(){this.emit('close');}}
const steamModel=real?load(path.join(root,'decky-plugin/src/steamRunningContext.ts')):null;const nativeClient=real?load(process.env.YMCC_MIRROR_CLIENT_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/mirrorClient.ts'),{'./performanceModel':model,'./steamRunningContext':steamModel}):null;
let realClient;const FixtureClient=real?class extends nativeClient.MirrorClient{constructor(deps){super(deps);realClient=this;}}:Client;
const widgets=Object.fromEntries(['PanelSection','PanelSectionRow','SliderField','DropdownItem','ToggleField','ButtonItem','Focusable'].map(k=>[k,k]));
const modules={'../../src/bridge/losslessProgramState':load(path.join(root,'src/bridge/losslessProgramState.ts')),'./mirrorClient':{MirrorClient:FixtureClient},'../../src/bridge/frameRateModel':{FRAME_RATE_CEILINGS:[0,60,120]},'../../src/bridge/deckyInputOptions':{DECKY_GYRO_CHOICES:[{data:'fps',label:'FPS射击'},{data:'racing',label:'赛车'},{data:'custom',label:'自定义'},{data:'steam',label:'Steam'}]},'./AnchoredDropdown':{AnchoredDropdown:'AnchoredDropdown'},'./fanChoiceDebounce':fan,'./performanceIntentQueue':queue,'./performanceModel':model,'./PerformanceControls':{PerformanceControls:'PerformanceControls'},'./GameProfileControls':{GameProfileControls:'GameProfileControls'},'./steamRunningContext':{observeSteamRunning:()=>({availability:'unavailable'})},'./qamPlacement':{installConsoleTab:()=>({active:false,dispose(){}})},'@decky/api':{useQuickAccessVisible:()=>visible},'./steamHudProbe':{installSteamHudProbe:()=>null},'./steamMonitorHud':{installSteamMonitorHud:()=>null},'./steamWallpaper':{installSteamWallpaper:()=>null},'./deckyStore':{installDeckyStoreCompatibility:()=>null},'./deckyLoaderUpdate':{installLoaderUpdateRow:()=>null}};
const plugin=load(process.env.YMCC_SLIDER_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/index.tsx'),modules,{SP_REACT:react,DFL:widgets,__YMCC_DECKY_BUILD_ID__:'b'.repeat(64),__YMCC_HUD_PROBE_BUILD__:false,window:{__YMCC_DECKY_MIRROR__:{endpoint:'ws://127.0.0.1:1946/mirror',token:'b'.repeat(64),runId:'a'.repeat(32)},addEventListener(){},removeEventListener(){},dispatchEvent(){}},WebSocket:Socket,Event:class{},crypto:{randomUUID:()=> 'test'},setTimeout:schedule,clearTimeout:cancel}).default();
function flatten(n,out=[]){if(Array.isArray(n))n.forEach(x=>flatten(x,out));else if(n&&typeof n==='object'){out.push(n);flatten(n.children,out);}return out;}
const text=n=>Array.isArray(n)?n.map(text).join(''):n&&typeof n==='object'?text(n.children):n==null||n===false?'':String(n);
function render(){cursor=0;const tree=plugin.content.type();while(effects.length)effects.shift()();return flatten(tree);}
function unmount(){for(const cleanup of [...cleanups.values()].reverse())cleanup();cleanups=new Map();hooks=[];effects=[];cursor=0;}
function show(){visible=true;render();if(real){if(!sockets.at(-1).opened)sockets.at(-1).emit('open');sockets.at(-1).emit('message',{type:'snapshot',runId:state.snapshot.runId,snapshot:state.snapshot});render();}}
if(real){render();sockets.at(-1).emit('open');sockets.at(-1).emit('message',{type:'snapshot',runId:state.snapshot.runId,snapshot:state.snapshot});render();}
const api={state,mutations,requests,timers,entry:{dom:entryDom,nav:entryNav,calls:entryCalls,frames:entryFrames,listeners:()=>[...entryListeners.values()].reduce((n,set)=>n+set.size,0),input(name){for(const fn of entryListeners.get(name)??[])fn({type:name});},flush(){for(const [id,fn]of [...entryFrames]){entryFrames.delete(id);fn();}}},clientState:()=>real?realClient.snapshot():structuredClone(state),disconnect(){sockets.at(-1).emit('close');},nodes:render,reply(ok=true,request=mutations.at(-1),fields={}){sockets.at(-1).emit('message',{type:'reply',runId:state.snapshot.runId,id:request.id,ok,result:{saved:true,applied:true},notice:'性能调度已记忆并提交 YMCC',...fields});},find(type,label=''){return render().find(n=>n.type===type&&text(n.props.label).startsWith(label));},publish(){if(real)sockets.at(-1).emit('message',{type:'snapshot',runId:state.snapshot.runId,snapshot:state.snapshot});else for(const f of listeners)f(state);},hide(){visible=false;render();},show,unmount,advance(ms){now+=ms;for(const[i,t]of [...timers])if(t.at<=now){timers.delete(i);t.fn();}},close(){for(const key of views.keys())withView(key,unmount);unmount();plugin.onDismount();}};
function withView(key,fn){if(key===activeView)return fn();const prior={key:activeView,hooks,effects,cleanups,cursor,visible},next=views.get(key)??{hooks:[],effects:[],cleanups:new Map(),cursor:0,visible:false};activeView=key;({hooks,effects,cleanups,cursor,visible}=next);try{return fn();}finally{views.set(key,{hooks,effects,cleanups,cursor,visible});activeView=prior.key;({hooks,effects,cleanups,cursor,visible}=prior);}}
api.view=key=>new Proxy(api,{get(target,prop){const value=Reflect.get(target,prop);return typeof value==='function'?(...args)=>withView(key,()=>value(...args)):value;}});return api;
}
// A bounded invocation of this existing consumer; skipped cases are never PASS.
const speedOnly=process.argv.includes('--speed-only'),backgroundOnly=process.argv.includes('--background-only'),entryOnly=process.argv.includes('--entry-focus-only'),menuOnly=process.argv.includes('--menu-actions-only'),skipped=[];
const cases=[];async function check(name,fn){if(speedOnly&&!name.startsWith('speed ack: ')||backgroundOnly&&!name.startsWith('background intent: ')||entryOnly&&!name.startsWith('entry focus: ')||menuOnly&&!name.startsWith('menu actions: ')){skipped.push(name);return;}try{await fn();cases.push(name);}catch(error){throw new Error(name+": "+error.message,{cause:error});}}
// Whole-sidebar command consumer: stable dropdown source vs unrelated publications.
function allMenusHarness(){const h=controllerMenuHarness(true),s=h.state.snapshot;s.actions.touchpads=true;s.touchpads={persona:'steamdeck',fields:{layout:{value:'single',supported:true,choices:[{data:'off',label:'关闭'},{data:'single',label:'单触摸板'},{data:'dual',label:'双触摸板'}]},singleMode:{value:'mouse',supported:true,choices:[{data:'mouse',label:'鼠标'},{data:'wasd',label:'WASD'}]},specialMask:{value:'1',supported:true,choices:[{data:'1',label:'只开启Steam'},{data:'0',label:'关闭'}]},rearMask:{value:'10',supported:true,choices:[{data:'10',label:'L5/R5'},{data:'0',label:'关闭'}]}}};s.game.fields.speedFactor={value:'1',supported:true,choices:[{data:'1',label:'1×'},{data:'2',label:'2×'}]};s.performance.controls.push({key:'hyperThreadMode',label:'超线程',kind:'choice',value:'default',enabled:true,choices:[{data:'default',label:'默认'},{data:'on',label:'开启'},{data:'off',label:'关闭'}]});h.publish();return h;}
const panelMenus=[['触摸板','touchpad.setField','layout','dual'],['触摸板映射','touchpad.setField','singleMode','wasd'],['专用按键','touchpad.setField','specialMask','0'],['背部按键','touchpad.setField','rearMask','0'],['游戏变速','game.setField','speedFactor','2'],['虚拟手柄','game.setField','padPersona','elite'],['陀螺仪预设','game.setField','gyroPreset','racing'],['锁帧上限','frame.setField','acCeiling','60']];
for(const [label,command,field,value]of panelMenus)await check('menu actions: '+label+' stays open during unrelated snapshots and sends the current intended choice once',async()=>{const h=allMenusHarness();h.find('AnchoredDropdown',label).props.onMenuWillOpen(()=>{});const owned=h.find('AnchoredDropdown',label).props;assert.equal(owned.opened,true);for(let i=0;i<12;i++){h.state.snapshot.revision++;h.state.snapshot.monitor.enabled=!h.state.snapshot.monitor.enabled;h.state.snapshot.performance.sourceRevision='telemetry-'+i;h.publish();assert.equal(h.find('AnchoredDropdown',label).props.opened,true,'Unrelated publication must not retract '+label);}h.find('AnchoredDropdown',label).props.onChange({data:value});assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].command,command);assert.equal(h.mutations[0].args.field,field);assert.equal(h.mutations[0].args.value,value);assert.equal(h.mutations[0].revision,h.state.snapshot.revision);owned.onChange({data:value});assert.equal(h.mutations.length,1);h.reply();await new Promise(r=>setImmediate(r));h.close();assert.equal(h.timers.size,0);});
for(const [label,,field,value]of panelMenus)await check('menu actions: '+label+' old option and cancel callbacks cannot target a reopened menu',async()=>{const h=allMenusHarness();h.find('AnchoredDropdown',label).props.onMenuWillOpen(()=>{});const old=h.find('AnchoredDropdown',label).props;old.onCancel();h.find('AnchoredDropdown',label).props.onMenuWillOpen(()=>{});assert.equal(h.find('AnchoredDropdown',label).props.opened,true);old.onChange({data:value});old.onCancel();assert.equal(h.mutations.length,0,'Stale menu must not submit '+field);assert.equal(h.find('AnchoredDropdown',label).props.opened,true);h.close();assert.equal(h.timers.size,0);});
for(const [boundary,mutate]of [['touchpad value',s=>s.touchpads.fields.layout.value='off'],['touchpad choices',s=>s.touchpads.fields.layout.choices[2].disabled=true],['touchpad persona',s=>s.touchpads.persona='dualsense-edge'],['touchpad permission',s=>s.actions.touchpads=false],['power',s=>s.powerSource='dc'],['generation',s=>s.generation++],['game',s=>s.game.identity='new-game'],['not ready',s=>s.ready=false]])await check('menu actions: real '+boundary+' change revokes touchpad options without a write',()=>{const h=allMenusHarness();h.find('AnchoredDropdown','触摸板').props.onMenuWillOpen(()=>{});const old=h.find('AnchoredDropdown','触摸板').props;mutate(h.state.snapshot);h.state.snapshot.revision++;h.publish();assert.equal(h.find('AnchoredDropdown','触摸板').props.opened,false);old.onChange({data:'dual'});assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);});
await check('menu actions: simply opening a dropdown never cancels an accepted TDP draft',()=>{const h=allMenusHarness();h.find('PerformanceControls').props.change('tdp',25);h.find('AnchoredDropdown','触摸板').props.onMenuWillOpen(()=>{});h.find('AnchoredDropdown','触摸板').props.onCancel();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);h.close();});
function actualPerformanceMenu(h){const props=h.find('PerformanceControls').props,react={Fragment:'Fragment',createElement:(type,props,...children)=>({type,props:props??{},children})};const component=load(path.join(root,'decky-plugin/src/PerformanceControls.tsx'),{'./performanceModel':model,'./AnchoredDropdown':{AnchoredDropdown:'AnchoredDropdown'}},{SP_REACT:react,DFL:{SliderField:'SliderField',PanelSectionRow:'Row',ToggleField:'Toggle',Focusable:'Focusable'}});const flat=n=>Array.isArray(n)?n.flatMap(flat):n&&typeof n==='object'?[n,...flat(n.children)]:[];return flat(component.PerformanceControls(props));}
const performanceMenuCases=[['saving','节能浮动','medium','large','auto'],['coreLow','低功耗核心','bigFirst','littleOnly','auto'],['hyperThreadMode','超线程','default','on','auto'],['powerScheme','电源方案','bal','yeman','manual'],['coreMode','大小核心','bigFirst','littleOnly','manual'],['hyperThreadMode','超线程','default','off','manual']];
function performanceMenuHarness(key,label,value,next,mode){const h=allMenusHarness(),s=h.state.snapshot;s.performance.mode=mode;s.performance.controls=s.performance.controls.filter(c=>c.key!=='hyperThreadMode');s.performance.controls.push({key,label,kind:'choice',value,enabled:true,choices:[{data:value,label:value},{data:next,label:next}]});if(mode==='manual'&&key!=='powerScheme')s.performance.controls.push({key:'powerScheme',label:'电源方案',kind:'choice',value:'yeman',enabled:true,choices:[{data:'yeman',label:'野蛮系统优化电源'},{data:'bal',label:'平衡'}]});h.publish();return h;}
for(const input of performanceMenuCases)await check('menu actions: performance '+input[0]+' '+input[4]+' keeps inline options through unrelated refresh and reaches YMCC once',async()=>{const [key,label,value,next,mode]=input,h=performanceMenuHarness(...input),find=()=>actualPerformanceMenu(h).find(n=>n.type==='AnchoredDropdown'&&n.props.label===label);find().props.onMenuWillOpen();assert.equal(find().props.opened,true);assert.equal(find().props.disabled,false);for(let i=0;i<8;i++){h.state.snapshot.revision++;h.state.snapshot.performance.sourceRevision='noise-'+i;h.state.snapshot.monitor.enabled=!h.state.snapshot.monitor.enabled;h.publish();assert.equal(find().props.opened,true);assert.equal(find().props.disabled,false);}const old=find().props;old.onChange({data:next});old.onChange({data:next});if(key!=='powerScheme')h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].command,'performance.setControls');assert.equal(h.mutations[0].args.patch[key],next);assert.equal(h.mutations[0].revision,h.state.snapshot.revision);h.reply();await new Promise(r=>setImmediate(r));h.close();assert.equal(h.timers.size,0);});
for(const input of performanceMenuCases)await check('menu actions: performance '+input[0]+' '+input[4]+' rejects an old same-field option after cancel and reopen',()=>{const [key,label,value,next]=input,h=performanceMenuHarness(...input),find=()=>actualPerformanceMenu(h).find(n=>n.type==='AnchoredDropdown'&&n.props.label===label);find().props.onMenuWillOpen();const old=find().props;old.onCancel();find().props.onMenuWillOpen();old.onChange({data:next});old.onCancel();assert.equal(find().props.opened,true);h.advance(5000);assert.equal(h.mutations.length,0);h.close();});
for(const change of ['value','choices','enabled','permission','mode','scope','powerScheme'])await check('menu actions: performance selection is revoked by a real '+change+' change',()=>{const h=performanceMenuHarness('coreMode','大小核心','bigFirst','littleOnly','manual'),find=()=>actualPerformanceMenu(h).find(n=>n.type==='AnchoredDropdown'&&n.props.label==='大小核心');find().props.onMenuWillOpen();const old=find().props,s=h.state.snapshot,c=s.performance.controls.find(c=>c.key==='coreMode');if(change==='value')c.value='littleOnly';if(change==='choices')c.choices[1].disabled=true;if(change==='enabled')c.enabled=false;if(change==='permission')s.actions.performance=false;if(change==='mode')s.performance.mode='auto';if(change==='scope')s.performance.scope='game';if(change==='powerScheme')s.performance.controls.find(c=>c.key==='powerScheme').value='bal';s.revision++;h.publish();assert.notEqual(find()?.props.opened,true);old.onChange({data:'littleOnly'});h.advance(5000);assert.equal(h.mutations.length,0);h.close();});
await check('menu actions: each choice menu has a single 45-second deadline and B cancellation never writes',()=>{for(const [label]of panelMenus){const h=allMenusHarness();h.find('AnchoredDropdown',label).props.onMenuWillOpen(()=>{});h.advance(44000);h.state.snapshot.revision++;h.publish();assert.equal(h.find('AnchoredDropdown',label).props.opened,true);h.advance(1000);assert.equal(h.find('AnchoredDropdown',label).props.opened,false);assert.equal(h.mutations.length,0);h.close();}const h=performanceMenuHarness('coreLow','低功耗核心','bigFirst','littleOnly','auto');h.find('PerformanceControls').props.menus.open('coreLow');h.advance(45000);assert.equal(h.find('PerformanceControls').props.menus.field,null);assert.equal(h.mutations.length,0);h.close();});
await check('menu actions: native independent-key standalone menu remains usable and sends only original IDs',async()=>{const h=allMenusHarness();h.state.snapshot.touchpads.persona='disabled';delete h.state.snapshot.touchpads.fields.specialMask;h.state.snapshot.touchpads.fields.standaloneSpecialMode={value:'off',supported:true,choices:[{data:'off',label:'关闭'},{data:'win',label:'Windows'}]};h.publish();h.find('AnchoredDropdown','专用按键').props.onMenuWillOpen(()=>{});h.state.snapshot.revision++;h.publish();assert.equal(h.find('AnchoredDropdown','专用按键').props.opened,true);h.find('AnchoredDropdown','专用按键').props.onChange({data:'win'});assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.field,'standaloneSpecialMode');assert.equal(h.mutations[0].args.value,'win');h.reply();await new Promise(r=>setImmediate(r));h.close();});
await check('menu actions: panel one-shot buttons cannot silently discard an accepted TDP draft',()=>{const h=allMenusHarness();h.find('PerformanceControls').props.change('tdp',25);h.find('ToggleField','监控总开关').props.onChange(false);h.find('AnchoredDropdown','虚拟手柄').props.onMenuWillOpen(()=>{});h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);h.close();});
await check('menu actions: restoring a queued performance choice to its saved value cancels that intent',()=>{const h=performanceMenuHarness('coreLow','低功耗核心','bigFirst','littleOnly','auto'),find=()=>actualPerformanceMenu(h).find(n=>n.type==='AnchoredDropdown'&&n.props.label==='低功耗核心');find().props.onMenuWillOpen();find().props.onChange({data:'littleOnly'});assert.equal(h.find('PerformanceControls').props.draft.coreLow,'littleOnly');find().props.onMenuWillOpen();find().props.onChange({data:'bigFirst'});h.advance(5000);assert.equal(h.mutations.length,0);assert.equal(h.find('PerformanceControls').props.draft.coreLow,undefined);h.close();});
await check('menu actions: power-scheme transaction waits for pending TDP and never mixes or erases intents',async()=>{
 const h=performanceMenuHarness('powerScheme','电源方案','yeman','bal','manual'),find=()=>actualPerformanceMenu(h).find(n=>n.type==='AnchoredDropdown'&&n.props.label==='电源方案');
 h.find('PerformanceControls').props.change('tdp',25);assert.equal(find().props.disabled,true,'Power scheme must wait for pending slider intent');find().props.onMenuWillOpen();assert.equal(find().props.opened,false);find().props.onChange({data:'bal'});assert.equal(h.find('PerformanceControls').props.draft.tdp,25);assert.equal(h.mutations.length,0);
 h.advance(2000);assert.equal(h.mutations.length,1);assert.deepEqual(h.mutations[0].args,{patch:{tdp:25}});h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.performance.controls.find(c=>c.key==='tdp').value=25;h.state.snapshot.performance.sourceRevision='tdp-applied';h.state.snapshot.revision++;h.publish();assert.equal(find().props.disabled,false);
 find().props.onMenuWillOpen();assert.equal(find().props.opened,true);find().props.onChange({data:'bal'});assert.equal(h.mutations.length,2);assert.deepEqual(h.mutations[1].args,{patch:{powerScheme:'bal'}});h.reply();await new Promise(r=>setImmediate(r));h.close();assert.equal(h.timers.size,0);
});
// Exact product entry effect; native TakeFocus/DOM/RAF endpoints are memory-only.
await check('entry focus: opening focuses the editable TDP row once without a write',()=>{const h=harness(true,true);assert.equal(h.entry.frames.size,1);h.entry.flush();assert.deepEqual(h.entry.calls,['tdp-row']);assert.equal(h.mutations.length,0);for(let i=0;i<10;i++){h.state.snapshot.revision++;h.state.snapshot.performance.sourceRevision='refresh-'+i;h.publish();h.nodes();h.entry.flush();}assert.equal(h.entry.calls.length,1);assert.equal(h.mutations.length,0);h.close();assert.equal(h.entry.listeners(),0);assert.equal(h.entry.frames.size,0);});
await check('entry focus: close and reopen grants exactly one fresh TDP focus',()=>{const h=harness(true,true);h.entry.flush();h.hide();assert.equal(h.entry.listeners(),0);h.show();h.entry.flush();assert.equal(h.entry.calls.length,2);h.entry.flush();assert.equal(h.entry.calls.length,2);h.close();assert.equal(h.mutations.length,0);});
for(const event of ['pointerdown','keydown','vgp_onbuttondown'])await check('entry focus: '+event+' before first frame preserves the user focus',()=>{const h=harness(true,true);h.entry.input(event);h.entry.flush();assert.equal(h.entry.calls.length,0);h.state.snapshot.revision++;h.state.snapshot.performance.sourceRevision='after-input';h.publish();h.nodes();h.entry.flush();assert.equal(h.entry.calls.length,0);h.close();assert.equal(h.entry.listeners(),0);});
for(const input of [false,true])await check('entry focus: delayed ready TDP '+(input?'respects prior navigation':'receives one initial focus'),()=>{const h=harness(true,true);h.state.snapshot.performance.controls[0].enabled=false;h.publish();h.nodes();h.entry.flush();assert.equal(h.entry.calls.length,0);if(input)h.entry.input('vgp_onbuttondown');h.state.snapshot.performance.controls[0].enabled=true;h.state.snapshot.performance.sourceRevision='ready-now';h.publish();h.nodes();h.entry.flush();assert.equal(h.entry.calls.length,input?0:1);h.close();});
for(const change of ['disabled','range','permission','view-off','hidden-tab','hidden-style','detached','no-native-ref'])await check('entry focus: '+change+' does not claim a nonexistent editable point',()=>{const h=harness(true,true);if(change==='disabled')h.state.snapshot.performance.controls[0].enabled=false;if(change==='range')h.state.snapshot.performance.controls[0].min=h.state.snapshot.performance.controls[0].max;if(change==='permission')h.state.snapshot.actions.performance=false;if(change==='view-off')h.state.snapshot.performance.enabled=false;if(change==='hidden-tab')h.entry.dom.displayed=false;if(change==='hidden-style')h.entry.dom.visibility='hidden';if(change==='detached')h.entry.dom.isConnected=false;if(change==='no-native-ref')h.entry.nav.TakeFocus=undefined;h.publish();h.nodes();h.entry.flush();assert.equal(h.entry.calls.length,0);assert.equal(h.mutations.length,0);h.close();});
for(const boundary of ['hide','unmount','unload'])await check('entry focus: '+boundary+' fences an already dispatched old frame',()=>{const h=harness(true,true),late=[...h.entry.frames.values()][0];if(boundary==='unload')h.close();else h[boundary]();late();assert.equal(h.entry.calls.length,0);assert.equal(h.entry.frames.size,0);h.close();assert.equal(h.entry.listeners(),0);});
await check('entry focus: an open native menu is never interrupted by initial TDP focus',()=>{const h=harness(true,true);h.find('AnchoredDropdown','锁帧上限').props.onMenuWillOpen(()=>{});h.entry.flush();assert.equal(h.entry.calls.length,0);h.find('AnchoredDropdown','锁帧上限').props.onCancel();h.nodes();h.entry.flush();assert.equal(h.entry.calls.length,0);h.close();assert.equal(h.mutations.length,0);});
await check('entry focus: actual PerformanceControls routes only TDP to its native outer focus ref',()=>{const h=harness(),props=h.find('PerformanceControls').props,rootRef={current:null},navRef={current:null};const react={Fragment:'Fragment',createElement:(type,props,...children)=>({type,props:props??{},children}),useState:()=>[null,()=>{}],useEffect(){}};const ui=load(process.env.YMCC_PERFORMANCE_CONTROL_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/PerformanceControls.tsx'),{'./performanceModel':model,'./AnchoredDropdown':{AnchoredDropdown:'Dropdown'}},{SP_REACT:react,DFL:{SliderField:'SliderField',PanelSectionRow:'Row',ToggleField:'Toggle'}});const view={...props.view,controls:[...props.view.controls,{key:'bias',label:'CPU/GPU',kind:'slider',min:0,max:100,step:5,value:50,enabled:true}]};const nodes=(()=>{const visit=n=>Array.isArray(n)?n.flatMap(visit):n&&typeof n==='object'?[n,...visit(n.children)]:[];return visit(ui.PerformanceControls({...props,view,entryElementRef:rootRef,entryNavRef:navRef}));})();const marked=nodes.filter(n=>n.props['data-ymcc-focus-target']==='tdp');assert.equal(marked.length,1);assert.equal(marked[0].props.ref,rootRef);const sliders=nodes.filter(n=>n.type==='SliderField');assert.equal(sliders[0].props.navRef,navRef);assert.equal(Object.hasOwn(sliders[1].props,'navRef'),false,'Other sliders must retain Steam native refs');assert.equal(h.mutations.length,0);h.close();});
// Same actual entry/client consumer, now closing a view is not cancellation.
for(const boundary of ['hide','unmount'])await check('background intent: '+boundary+' keeps one TDP submission at exactly two seconds',async()=>{
 const h=harness(true),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);p.end();h.advance(500);h[boundary]();h.advance(1499);assert.equal(h.mutations.length,0);h.advance(1);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);
 h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='background';h.publish();assert.equal(h.clientState().busy,false);assert.equal(h.clientState().snapshot,null,'no hidden business stream after settlement');h.advance(60000);assert.equal(h.mutations.length,1);h.close();assert.equal(h.timers.size,0);
});
await check('background intent: closing a held TDP gesture commits its last explicit value without accepting stale callbacks',async()=>{
 const h=harness(true),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);h.hide();p.change('tdp',29);p.end();h.advance(1999);assert.equal(h.mutations.length,0);h.advance(1);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);h.reply();await new Promise(r=>setImmediate(r));h.close();assert.equal(h.timers.size,0);
});
await check('background intent: reopening before submission edits the retained intent rather than duplicating it',async()=>{
 const h=harness(true),p=h.find('PerformanceControls').props;p.change('tdp',25);h.unmount();h.advance(500);h.show();const fresh=h.find('PerformanceControls').props;assert.equal(fresh.draft.tdp,25);fresh.change('tdp',24);h.advance(1999);assert.equal(h.mutations.length,0);h.advance(1);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,24);h.reply();await new Promise(r=>setImmediate(r));h.close();assert.equal(h.timers.size,0);
});
for(const boundary of ['hide','unmount'])await check('background intent: '+boundary+' keeps the fan timer and native reply alive',async()=>{
 const h=harness(true),p=h.find('SliderField','风扇控制').props;p.onChangeStart();p.onChange(2);p.onChangeComplete();h[boundary]();h.advance(1999);assert.equal(h.mutations.length,0);h.advance(1);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].command,'fan.setChoice');assert.equal(h.mutations[0].args.choice,'balanced');h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.clientState().snapshot,null);h.close();assert.equal(h.timers.size,0);
});
for(const kind of ['frame','monitor','power','input','lossless','audio','trainer','profile','touchpad','performanceToggle'])await check('background intent: accepted '+kind+' command receives its native result after unmount',async()=>{
 const h=harness(true);
 if(kind==='frame'){const p=h.find('SliderField','锁帧率').props;p.onChangeStart();p.onChange(55);h.find('SliderField','锁帧率').props.onChangeComplete(55);}
 if(kind==='monitor')h.find('ToggleField','监控总开关').props.onChange(false);
 if(kind==='power'){h.state.snapshot.performance.mode='manual';h.state.snapshot.performance.controls.push({key:'powerScheme',label:'电源',kind:'choice',value:'balanced',enabled:true,choices:[{data:'balanced',label:'平衡'},{data:'ymcc',label:'野蛮'}]});h.publish();h.find('PerformanceControls').props.change('powerScheme','ymcc');}
 if(kind==='input'){h.state.snapshot.global.fields.gyroEnabled={value:'off',supported:true,choices:[{data:'on',label:'开'},{data:'off',label:'关'}]};h.publish();h.find('ToggleField','陀螺仪').props.onChange(true);}
 if(kind==='audio'){h.state.snapshot.actions.audioOptimization=true;h.state.snapshot.audioOptimization={known:true,installed:true,running:true,enabled:false,selected:true,preset:'test',output:'test',reason:''};h.publish();h.find('ToggleField','掌机专用低音强化').props.onChange(true);}
 if(kind==='trainer'||kind==='profile'){h.state.snapshot.game={label:'A',identity:'app:480:pid101:birthA',fields:{profile:{value:'global',sourceRevision:'profile-source',supported:true,choices:[{data:'global',label:'全局'},{data:'dedicated',label:'专属'}]}}};h.state.snapshot.actions.game=true;h.state.snapshot.trainer={available:true,pending:false,notice:''};h.publish();if(kind==='trainer')h.find('ButtonItem','修改器').props.onClick();else h.find('GameProfileControls').props.change('dedicated',h.clientState().snapshot);}
 if(kind==='touchpad'){h.state.snapshot.actions.touchpads=true;h.state.snapshot.touchpads={persona:'steamdeck',fields:{layout:{value:'dual',supported:true,choices:[{data:'dual',label:'双'},{data:'single',label:'单'}]}}};h.publish();h.find('AnchoredDropdown','触摸板').props.onMenuWillOpen(()=>{});h.find('AnchoredDropdown','触摸板').props.onChange({data:'single'});}
 if(kind==='performanceToggle'){h.state.snapshot.performance.controls.push({key:'smt',label:'超线程',kind:'toggle',value:true,enabled:true});h.publish();h.find('PerformanceControls').props.change('smt',false);h.advance(2000);}
 if(kind==='lossless'){h.state.snapshot.game={label:'A',identity:'app:480:pid101:birthA',fields:{losslessScaling:{value:'off',nextValue:'on',supported:true,choices:[{data:'on',label:'开启'},{data:'off',label:'关闭'}]}}};h.state.snapshot.actions.game=true;h.publish();h.find('ButtonItem','小黄鸭').props.onClick();}
 assert.equal(h.mutations.length,1);h.unmount();assert.equal(h.clientState().busy,true);h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.clientState().busy,false);h.advance(60000);assert.equal(h.mutations.length,1);h.close();assert.equal(h.timers.size,0);
});
await check('background intent: idle hidden view keeps no business stream or timer',()=>{const h=harness(true);h.hide();assert.equal(h.clientState().snapshot,null);assert.equal(h.timers.size,0);assert.equal(h.mutations.length,0);h.advance(60000);assert.equal(h.mutations.length,0);h.close();});
await check('background intent: pending hidden fan is canceled when the native fan source changes',()=>{const h=harness(true),p=h.find('SliderField','风扇控制').props;p.onChange(2);h.hide();h.state.snapshot.fan.enabled=true;h.state.snapshot.fan.preset='soft';h.publish();h.advance(60000);assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);});
await check('background intent: stale hidden fan callbacks cannot end the replacement page drag',async()=>{const h=harness(true),old=h.find('SliderField','风扇控制').props;h.hide();h.show();const fresh=h.find('SliderField','风扇控制').props;fresh.onChangeStart();fresh.onChange(2);old.onChange(3);old.onChangeComplete();h.advance(10000);assert.equal(h.mutations.length,0);fresh.onChangeComplete();h.hide();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.choice,'balanced');h.reply();await new Promise(r=>setImmediate(r));h.close();assert.equal(h.timers.size,0);});
await check('background intent: a stale frame release cannot clear the new page frame gesture',async()=>{const h=harness(true),old=h.find('SliderField','锁帧率').props;h.hide();h.show();const fresh=h.find('SliderField','锁帧率').props;fresh.onChangeStart();fresh.onChange(55);old.onChangeComplete(90);assert.equal(h.mutations.length,0);h.find('SliderField','锁帧率').props.onChangeComplete(55);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.value,'55');h.unmount();h.reply();await new Promise(r=>setImmediate(r));h.close();assert.equal(h.timers.size,0);});
for(const boundary of ['generation','game','side','source','disconnect','unload'])await check('background intent: real '+boundary+' invalidates an unsent hidden intent without replay',async()=>{
 const h=harness(true),p=h.find('PerformanceControls').props;p.change('tdp',25);h.hide();
 if(boundary==='generation')h.state.snapshot.generation++;
 if(boundary==='game')h.state.snapshot.game={label:'B',identity:'app:481:pid202:birthB',fields:{}};
 if(boundary==='side')h.state.snapshot.performance.side='dc';
 if(boundary==='source')h.state.snapshot.performance.sourceRevision='external';
 if(boundary==='disconnect')h.disconnect();else if(boundary==='unload')h.close();else h.publish();
 h.advance(60000);assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);
});
await check('background intent: a hidden uncertain write times out once and is never resent',async()=>{
 const h=harness(true),p=h.find('PerformanceControls').props;p.change('tdp',25);h.hide();h.advance(2000);assert.equal(h.mutations.length,1);h.advance(15000);await new Promise(r=>setImmediate(r));h.advance(60000);assert.equal(h.mutations.length,1);h.close();assert.equal(h.timers.size,0);
});
await check('repeated slider changes cancel a browser timer with its Window receiver intact',()=>{const h=harness(),p=h.find('PerformanceControls').props;p.change('tdp',21);p.change('tdp',22);h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,22);h.close();});
await check('holding a performance slider for ten seconds never commits, release commits one final value against the latest receipt',()=>{const h=harness(),p=h.find('PerformanceControls').props;p.begin();for(let n=21;n<=25;n++)p.change('tdp',n);h.advance(10000);assert.equal(h.mutations.length,0);assert.equal(h.timers.size,0);h.state.snapshot.revision++;h.publish();p.end();h.advance(1999);assert.equal(h.mutations.length,0);h.advance(1);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].revision,2);assert.equal(h.mutations[0].args.patch.tdp,25);h.close();});
await check('switching AC to DC cancels the held performance edit',()=>{const h=harness(),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);h.state.snapshot.performance.side='dc';h.state.snapshot.powerSource='dc';h.publish();p.end();h.advance(10000);assert.equal(h.mutations.length,0);h.close();});
await check('fan drag remains pending while held and sends only the final preset after release',()=>{const h=harness(),p=h.find('SliderField','风扇控制').props;p.onChangeStart();p.onChange(1);p.onChange(2);h.advance(10000);assert.equal(h.mutations.length,0);p.onChangeComplete(2);h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.choice,'balanced');h.close();});
await check('frame float positions round to the five-FPS grid and a newer unrelated receipt is admitted',()=>{const h=harness(),p=h.find('SliderField','锁帧率').props;p.onChangeStart();p.onChange(54.99999999999999);h.state.snapshot.revision++;h.publish();h.find('SliderField','锁帧率').props.onChangeComplete(54.99999999999999);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].revision,2);assert.equal(h.mutations[0].args.value,'55');h.close();});
await check('a frame source change while held never writes the old AC target',()=>{const h=harness(),p=h.find('SliderField','锁帧率').props;p.onChangeStart();p.onChange(55);h.state.snapshot.powerSource='dc';h.state.snapshot.revision++;h.publish();p.onChangeComplete(55);assert.equal(h.mutations.length,0);h.close();});
await check('closing the sidebar completes the accepted held value once without replay',()=>{const h=harness(),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);h.hide();p.end();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);h.close();});
await check('float normalization handles integral, stepped, clamped, zero-range and invalid inputs',()=>{const n=model.normalizeSliderValue;assert.equal(n(24.999999999999996,5,30,1),25);assert.equal(n(54.99999999999999,15,120,5),55);assert.equal(n(0.30000000000000004,0,1,.1),.3);assert.equal(n(500,15,119,5),115);assert.equal(n(-20,5,30,1),5);assert.equal(n(5,5,5,1),5);for(const x of [NaN,Infinity,'25',null])assert.equal(n(x,5,30,1),null);});
await check('the latest native completion handler also rejects a source changed during the original drag',()=>{const h=harness(),p=h.find('SliderField','锁帧率').props;p.onChangeStart();p.onChange(55);h.state.snapshot.powerSource='dc';h.state.snapshot.revision++;h.publish();h.find('SliderField','锁帧率').props.onChangeComplete(55);assert.equal(h.mutations.length,0);h.close();});
await check('stable gyro and speed enum values display correct text with an older corrupted host',()=>{const h=harness();assert.equal(h.find('AnchoredDropdown','陀螺仪预设').props.rgOptions[0].label,'FPS射击');assert.equal(h.find('AnchoredDropdown','游戏变速').props.rgOptions[0].label,'1×');h.close();});
await check('five visual ticks preserve continuous one-watt steps and the four actual fan presets',()=>{const h=harness();assert.equal(h.find('SliderField','锁帧率').props.notchCount,5);assert.equal(h.find('SliderField','锁帧率').props.step,5);assert.equal(h.find('SliderField','风扇控制').props.notchCount,4);const react={Fragment:'Fragment',createElement:(type,props,...children)=>({type,props:props||{},children}),useState:()=>[null,()=>{}],useEffect(){}};const ui=load(process.env.YMCC_PERFORMANCE_CONTROL_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/PerformanceControls.tsx'),{'./performanceModel':model,'./AnchoredDropdown':{AnchoredDropdown:'Dropdown'}},{SP_REACT:react,DFL:{SliderField:'SliderField',PanelSectionRow:'Row',ToggleField:'Toggle'}});let changed;const tree=ui.PerformanceControls({view:h.state.snapshot.performance,disabled:false,draft:{},change:(k,v)=>changed=[k,v]});const visit=n=>Array.isArray(n)?n.flatMap(visit):n&&typeof n==='object'?[n,...visit(n.children)]:[];const slider=visit(tree).find(n=>n.type==='SliderField');assert.equal(slider.props.notchCount,5);assert.equal(slider.props.step,1);assert.equal(slider.props.min,5);assert.equal(slider.props.max,30);slider.props.onChange(24.999999999999996);assert.deepEqual(changed,['tdp',25]);h.close();});

await check('TDP editing and applying preserve the actual product row count and optimistic value',async()=>{const h=harness(),before=h.nodes().filter(n=>n.type==='PanelSectionRow').length,p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,before);assert.equal(h.find('PerformanceControls').props.draft.tdp,25);p.end();h.advance(2000);assert.equal(h.find('PerformanceControls').props.draft.tdp,25);assert.equal(h.find('PerformanceControls').props.status,'正在应用…');assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,before);h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='2';h.publish();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.view.controls[0].value,25);assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,before);h.close();});
await check('unrelated TDP saves keep native toggle identity and use Steam controlled values',()=>{const react={Fragment:'Fragment',createElement:(type,props,...children)=>({type,props:props||{},children}),useState:()=>[null,()=>{}],useEffect(){}};const ui=load(process.env.YMCC_PERFORMANCE_CONTROL_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/PerformanceControls.tsx'),{'./performanceModel':model,'./AnchoredDropdown':{AnchoredDropdown:'Dropdown'}},{SP_REACT:react,DFL:{SliderField:'SliderField',PanelSectionRow:'Row',ToggleField:'Toggle'}});const view={version:1,side:'ac',scope:'global',mode:'auto',sourceRevision:'before',enabled:true,controls:[{key:'saving',kind:'toggle',label:'节能浮动',value:false,enabled:true}]};const flatten=n=>Array.isArray(n)?n.flatMap(flatten):n&&typeof n==='object'?[n,...flatten(n.children)]:[];const render=(v,disabled,draft={})=>flatten(ui.PerformanceControls({view:v,disabled,draft,change(){}})).find(n=>n.type==='Toggle');const a=render(view,false),b=render({...view,sourceRevision:'after'},true),c=render({...view,sourceRevision:'after'},false);assert.equal(a.props.key,b.props.key);assert.equal(a.props.key,c.props.key);assert.equal(a.props.controlled,true);assert.equal(render(view,false,{saving:true}).props.checked,true);assert.equal(render(view,false).props.checked,false);});

await check('actual MirrorClient reply notice and later snapshot never insert or remove product rows',async()=>{const h=harness(true),base=h.nodes().filter(n=>n.type==='PanelSectionRow').length,p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);p.end();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.find('PerformanceControls').props.draft.tdp,25);assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,base);h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.draft.tdp,25);assert.equal(h.find('PerformanceControls').props.status,'等待状态确认…');assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,base);h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='new';h.publish();assert.equal(h.find('PerformanceControls').props.draft.tdp,undefined);assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,base);assert.equal(h.mutations.length,1);h.close();assert.equal(h.timers.size,0);});

// Same loaded module, fresh QAM page: old native callbacks must lose authority.
await check('a late slider change from the previous Big Picture page cannot enqueue an edit after reentry',()=>{
 const h=harness(),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);h.hide();h.show();old.change('tdp',29);old.end();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);h.close();assert.equal(h.timers.size,0);
});
await check('a previous page release cannot end the new page held TDP gesture',()=>{
 const h=harness(),old=h.find('PerformanceControls').props;h.hide();h.show();const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);old.end();h.advance(10000);assert.equal(h.mutations.length,0);fresh.end();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,24);h.close();
});
await check('unmounted Content callbacks cannot edit the replacement Content in the same loaded plugin',()=>{
 const h=harness(),old=h.find('PerformanceControls').props;h.unmount();h.show();old.begin();old.change('tdp',29);old.end();h.advance(10000);assert.equal(h.mutations.length,0);const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',23);fresh.end();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,23);h.close();assert.equal(h.timers.size,0);
});
await check('same-module Big Picture reentry uses fresh MirrorClient snapshots and one new write',async()=>{
 const h=harness(true),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);h.hide();await new Promise(r=>setImmediate(r));h.show();old.change('tdp',29);old.end();h.advance(1000);assert.equal(h.mutations.length,0);const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);fresh.end();h.advance(2000);assert.equal(h.mutations.length,1);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=24;h.state.snapshot.performance.sourceRevision='reentered';h.publish();assert.equal(h.find('PerformanceControls').props.draft.tdp,undefined);h.close();assert.equal(h.timers.size,0);
});
await check('an old MirrorClient reply after reentry cannot confirm or replace a new TDP intent',async()=>{
 const h=harness(true),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);old.end();h.advance(2000);const oldRequest=h.mutations[0];h.hide();h.show();assert.equal(h.find('PerformanceControls').props.draft.tdp,25);assert.equal(h.mutations.length,1);h.reply(true,oldRequest);await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='old-confirmed';h.publish();const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);fresh.end();h.advance(2000);assert.equal(h.mutations.length,2);h.reply(true,oldRequest);await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.draft.tdp,24);assert.equal(h.find('PerformanceControls').props.status,'正在应用…');h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=24;h.state.snapshot.performance.sourceRevision='fresh';h.publish();assert.equal(h.find('PerformanceControls').props.draft.tdp,undefined);h.close();assert.equal(h.timers.size,0);
});
await check('reentry after a successful reply cancels its readback wait without replaying the old TDP',async()=>{
 const h=harness(true),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);p.end();h.advance(2000);h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.status,'等待状态确认…');h.hide();await new Promise(r=>setImmediate(r));h.show();h.advance(60000);assert.equal(h.mutations.length,1);assert.equal(Object.keys(h.find('PerformanceControls').props.draft).length,0);h.close();assert.equal(h.timers.size,0);
});
await check('ordinary visible rerenders preserve the active drag authority and final one-watt choice',()=>{
 const h=harness(),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',24);for(let i=0;i<100;i++){h.state.snapshot.revision++;h.publish();h.nodes();}p.change('tdp',25);h.advance(10000);assert.equal(h.mutations.length,0);p.end();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);assert.equal(h.mutations[0].revision,101);h.close();
});


await check('a previous page begin cannot cancel the replacement page released TDP deadline',()=>{
 const h=harness(),old=h.find('PerformanceControls').props;h.hide();h.show();const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);fresh.end();old.begin();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,24);h.close();
});
await check('an old failure reply cannot roll back a replacement page TDP intent',async()=>{
 const h=harness(true),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);old.end();h.advance(2000);const oldRequest=h.mutations[0];h.hide();h.show();h.reply(false,oldRequest);await new Promise(r=>setImmediate(r));const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);fresh.end();h.advance(2000);h.reply(false,oldRequest);await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.draft.tdp,24);assert.equal(h.find('PerformanceControls').props.status,'正在应用…');h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=24;h.state.snapshot.performance.sourceRevision='replacement';h.publish();h.close();assert.equal(h.timers.size,0);
});
await check('Content unmount and replacement use the actual MirrorClient without stale writes',async()=>{
 const h=harness(true),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);h.unmount();h.show();old.begin();old.change('tdp',29);old.end();h.advance(2000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='old-confirmed';h.publish();const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',23);fresh.end();h.advance(2000);assert.equal(h.mutations.length,2);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=23;h.state.snapshot.performance.sourceRevision='remounted';h.publish();h.close();assert.equal(h.timers.size,0);
});
await check('thirty same-module page replacements leave no timer or duplicate performance request',async()=>{
 const h=harness(true);for(let n=0;n<30;n++){h.unmount();await new Promise(r=>setImmediate(r));h.show();const p=h.find('PerformanceControls').props,value=n%2?24:25;p.begin();p.change('tdp',value);p.end();h.advance(2000);assert.equal(h.mutations.length,n+1);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=value;h.state.snapshot.performance.sourceRevision=String(n+10);h.publish();h.nodes();assert.equal(Object.keys(h.find('PerformanceControls').props.draft).length,0);assert.equal(h.timers.size,0);}h.close();assert.equal(h.timers.size,0);
});


await check('mounting an inactive QAM page cannot clear the active page authenticated snapshot',()=>{
 const h=harness(true);assert(h.find('PerformanceControls'));h.view('inactive').hide();assert(h.find('PerformanceControls'),'active performance controls must survive an inactive page mount');h.close();assert.equal(h.timers.size,0);
});
await check('hiding a different QAM page cannot cancel the active page held TDP gesture',async()=>{
 const h=harness(true),other=h.view('other');other.show();const p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);other.hide();assert.equal(h.find('PerformanceControls').props.draft.tdp,25);h.advance(10000);assert.equal(h.mutations.length,0);p.end();h.advance(2000);assert.equal(h.mutations.length,1);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='other-hidden';h.publish();h.close();assert.equal(h.timers.size,0);
});
await check('unmounting another QAM page cannot reject the active page in-flight native write',async()=>{
 const h=harness(true),other=h.view('other');other.show();const p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);p.end();h.advance(2000);other.unmount();assert.equal(h.find('PerformanceControls').props.status,'正在应用…');h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.status,'等待状态确认…');h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='other-unmounted';h.publish();assert.equal(h.mutations.length,1);h.close();assert.equal(h.timers.size,0);
});
await check('separate QAM pages never inherit each other optimistic TDP draft',()=>{
 const h=harness(true),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);const other=h.view('other');other.show();assert.equal(other.find('PerformanceControls').props.draft.tdp,undefined);assert.equal(h.find('PerformanceControls').props.draft.tdp,25);h.close();assert.equal(h.timers.size,0);
});
await check('eighty inactive page replacements preserve one active snapshot and zero unsolicited writes',()=>{
 const h=harness(true);for(let i=0;i<80;i++){const other=h.view('inactive-'+i);other.hide();other.unmount();assert(h.find('PerformanceControls'));assert.equal(h.mutations.length,0);assert.equal(h.timers.size,0);}h.close();assert.equal(h.timers.size,0);
});

// Frame ceiling menus share the real Content subscription and strict MirrorClient
// revision checks. Only unrelated refreshes may retain an open selection.
for(const side of ['ac','dc'])await check(side+' frame ceiling menu survives unrelated live snapshots and commits once against the latest actual client revision',async()=>{
 const h=harness(true);h.state.snapshot.powerSource=side;h.publish();
 const closed=h.find('AnchoredDropdown','锁帧上限');closed.props.onMenuWillOpen(()=>{});
 assert.equal(h.find('AnchoredDropdown','锁帧上限').props.opened,true);
 for(let revision=2;revision<=21;revision++){
  h.state.snapshot.revision=revision;h.state.snapshot.monitor.enabled=revision%2===0;
  h.state.snapshot.performance.sourceRevision=String(revision);h.state.snapshot.performance.controls[0].value=20+revision%3;
  h.publish();assert.equal(h.find('AnchoredDropdown','锁帧上限').props.opened,true,'unrelated revision must not retract the frame menu');
 }
 assert.equal(h.mutations.length,0,'background refresh does not choose or write');
 const value=side==='ac'?'60':'120';h.find('AnchoredDropdown','锁帧上限').props.onChange({data:value});
 assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].command,'frame.setField');
 assert.equal(h.mutations[0].args.field,side+'Ceiling');assert.equal(h.mutations[0].args.value,value);assert.equal(h.mutations[0].revision,21);
 h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('AnchoredDropdown','锁帧上限').props.opened,false);
 h.close();assert.equal(h.timers.size,0);
});
for(const [reason,change]of [
 ['frame limit changed',s=>s.snapshot.frames.pair.ac.ceiling=60],
 ['frame FPS changed',s=>s.snapshot.frames.pair.ac.fps=30],
 ['frame dedicated source changed',s=>s.snapshot.frames.dedicated=true],
 ['power source changed',s=>s.snapshot.powerSource='dc'],
 ['power generation changed',s=>s.snapshot.generation++],
 ['session changed',s=>s.snapshot.runId='c'.repeat(32)],
 ['game changed',s=>s.snapshot.game={identity:'new-game',label:'Game',fields:{}}],
 ['permission revoked',s=>s.snapshot.actions.frames=false],
 ['frame data missing',s=>delete s.snapshot.frames],
 ['not ready',s=>s.snapshot.ready=false],
 ['transport disconnected',s=>s.connected=false],
])await check('frame menu still cancels without writing when '+reason,()=>{
 const h=harness();h.find('AnchoredDropdown','锁帧上限').props.onMenuWillOpen(()=>{});
 const stale=h.find('AnchoredDropdown','锁帧上限');assert.equal(stale.props.opened,true);
 h.state.snapshot.revision++;change(h.state);h.publish();
 assert.ok(!h.find('AnchoredDropdown','锁帧上限')?.props.opened);
 stale.props.onChange({data:'60'});assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);
});
await check('retained frame menu keeps its original timeout and explicit cancel and hide release it',()=>{
 const h=harness();h.find('AnchoredDropdown','锁帧上限').props.onMenuWillOpen(()=>{});
 h.advance(44000);h.state.snapshot.revision++;h.publish();
 assert.equal(h.find('AnchoredDropdown','锁帧上限').props.opened,true);
 h.advance(1000);assert.equal(h.find('AnchoredDropdown','锁帧上限').props.opened,false);
 h.find('AnchoredDropdown','锁帧上限').props.onMenuWillOpen(()=>{});h.find('AnchoredDropdown','锁帧上限').props.onCancel();
 assert.equal(h.find('AnchoredDropdown','锁帧上限').props.opened,false);
 h.find('AnchoredDropdown','锁帧上限').props.onMenuWillOpen(()=>{});h.hide();h.show();
 assert.equal(h.find('AnchoredDropdown','锁帧上限').props.opened,false);assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);
});


// Speed acknowledgment extends the same shipped TSX / real MirrorClient fixture.
// Minimal entry: node tools/decky_sidebar_slider_drag_selftest.mjs --speed-only
// The memory transport supplies replies; the product still owns the pending lease.
const settleSpeed=()=>new Promise(resolve=>setImmediate(resolve));
async function speedCheck(name,fn){await check('speed ack: '+name,async()=>{
 const h=harness(true);
 try{
  h.state.snapshot.game={identity:'speed-ack-game',label:'Speed acknowledgment game',fields:{speedFactor:{value:'1',supported:true,choices:['1','2','4'].map(data=>({data,label:data+'×'}))}}};
  h.state.snapshot.actions.game=true;h.state.snapshot.revision++;h.publish();
  assert.equal(h.clientState().connected,true);assert.equal(h.find('AnchoredDropdown','游戏变速').props.disabled,false);
  await fn(h);
 }finally{h.close();assert.equal(h.timers.size,0,'speed fixture must release every owned timer');}
});}
function chooseSpeed(h,value='2'){
 const before=h.mutations.length,closed=h.find('AnchoredDropdown','游戏变速');
 assert.equal(closed.props.disabled,false);closed.props.onMenuWillOpen(()=>{});
 const opened=h.find('AnchoredDropdown','游戏变速');assert.equal(opened.props.opened,true);
 opened.props.onChange({data:value});
 assert.equal(h.mutations.length,before+1,'one user speed selection sends exactly one mutation');
 const request=h.mutations.at(-1);
 assert.equal(request.command,'game.setField');assert.deepEqual(request.args,{field:'speedFactor',value});
 assert.equal(request.revision,h.state.snapshot.revision);return request;
}
function speedWaiting(h,value='1'){
 const dropdown=h.find('AnchoredDropdown','游戏变速'),state=h.clientState();
 assert.equal(dropdown.props.opened,false,'speed pending menu must close immediately');
 assert.equal(dropdown.props.disabled,true,'speed pending dropdown must stay disabled');
 assert(h.find('AnchoredDropdown','游戏变速 · 等待确认'),'speed pending label must include 等待确认');
 assert.equal(dropdown.props.selectedOption,value,'unacknowledged speed must retain the confirmed value');
 assert.equal(state.snapshot.game.fields.speedFactor.value,value);
 assert.equal(state.busy,true,'speed request must stay busy until its own reply or deadline');
 assert.match(state.notice,/游戏变速正在处理.*等待执行结果.*2 秒/);
 assert(h.nodes().some(n=>n.props.role==='status'&&n.children.includes(state.notice)),'actual TSX must display the speed busy notice');
}
await speedCheck('selection closes and disables immediately; only its matching reply releases the dropdown',async h=>{
 const request=chooseSpeed(h),reads=h.requests.length;speedWaiting(h);
 h.find('AnchoredDropdown','游戏变速').props.onChange({data:'4'});
 h.find('AnchoredDropdown','游戏变速').props.onMenuWillOpen(()=>{});
 assert.equal(h.mutations.length,1,'pending callbacks cannot resend speed');
 h.reply(true,{id:'unrelated-request'},{notice:'unrelated success'});await settleSpeed();speedWaiting(h);
 h.state.snapshot.revision++;h.state.snapshot.monitor.enabled=false;h.publish();speedWaiting(h);
 h.advance(1999);await settleSpeed();speedWaiting(h);
 assert.equal(h.requests.length,reads,'no timer-driven replay or snapshot polling before timeout');
 h.reply(true,request,{notice:'游戏变速已确认'});await settleSpeed();
 const done=h.find('AnchoredDropdown','游戏变速');assert.equal(done.props.opened,false);assert.equal(done.props.disabled,false);
 assert.equal(done.props.selectedOption,'1','reply alone must not invent a new snapshot value');
 assert.equal(h.find('AnchoredDropdown','游戏变速 · 等待确认'),undefined);
 assert.equal(h.clientState().busy,false);assert.equal(h.clientState().notice,'游戏变速已确认');assert.equal(h.timers.size,0);
 h.state.snapshot.revision++;h.state.snapshot.game.fields.speedFactor.value='2';h.publish();
 assert.equal(h.find('AnchoredDropdown','游戏变速').props.selectedOption,'2');
 h.advance(4000);await settleSpeed();assert.equal(h.requests.length,reads);assert.equal(h.mutations.length,1);
});
await speedCheck('1999 ms remains locked; exactly 2000 ms becomes unconfirmed and rereads only snapshot',async h=>{
 const request=chooseSpeed(h),reads=h.requests.filter(r=>r.command==='snapshot').length;
 h.advance(1999);await settleSpeed();speedWaiting(h);assert.equal(h.requests.filter(r=>r.command==='snapshot').length,reads);
 h.advance(1);await settleSpeed();
 assert.equal(h.clientState().busy,false,'speed must leave busy at exactly 2000 ms without a reply');
 assert.equal(h.clientState().snapshot,null,'unconfirmed speed must demand a fresh snapshot');
 assert.match(h.clientState().notice,/游戏变速结果未确认（等待已达 2 秒）.*重新读取状态.*不自动重试/);
 assert.equal(h.find('AnchoredDropdown','游戏变速').props.disabled,true);
 assert.equal(h.find('AnchoredDropdown','游戏变速 · 等待确认'),undefined);
 const readsAfter=h.requests.filter(r=>r.command==='snapshot');assert.equal(readsAfter.length,reads+1);
 assert.equal(readsAfter.at(-1).runId,h.state.snapshot.runId);
 assert.equal(readsAfter.at(-1).subscribe,undefined);assert.equal(h.mutations.length,1);
 const timedOut=h.clientState();h.reply(true,request,{notice:'late old speed success'});await settleSpeed();
 assert.deepEqual(h.clientState(),timedOut,'late timed-out reply cannot claim success');
 h.state.snapshot.revision++;h.publish();
 assert.equal(h.find('AnchoredDropdown','游戏变速').props.disabled,false);
 assert.equal(h.find('AnchoredDropdown','游戏变速').props.selectedOption,'1');
 const afterRead=h.requests.length;h.advance(4000);await settleSpeed();
 assert.equal(h.requests.length,afterRead);assert.equal(h.mutations.length,1);assert.equal(h.timers.size,0);
});
await speedCheck('late old replies cannot unlock or overwrite a newer speed request',async h=>{
 const old=chooseSpeed(h);h.advance(2000);await settleSpeed();
 assert.equal(h.clientState().busy,false);h.state.snapshot.revision++;h.publish();
 const current=chooseSpeed(h,'4'),pendingTimers=[...h.timers.entries()],sent=h.requests.length;
 assert.notEqual(current.id,old.id);speedWaiting(h);h.advance(1000);
 for(const ok of [true,false]){
  h.reply(ok,old,{notice:'late old speed reply',error:'late old speed error'});await settleSpeed();speedWaiting(h);
  assert.deepEqual([...h.timers.entries()],pendingTimers,'old reply cannot cancel or replace the new deadline');
 }
 assert.equal(h.requests.length,sent);assert.equal(h.mutations.length,2);
 h.advance(999);await settleSpeed();speedWaiting(h);
 h.reply(true,current,{notice:'new speed confirmed'});await settleSpeed();
 assert.equal(h.clientState().busy,false);assert.equal(h.clientState().notice,'new speed confirmed');
 assert.equal(h.find('AnchoredDropdown','游戏变速').props.disabled,false);
 h.state.snapshot.revision++;h.state.snapshot.game.fields.speedFactor.value='4';h.publish();
 const confirmed=h.clientState();h.reply(true,old,{notice:'obsolete overwrite'});await settleSpeed();
 assert.deepEqual(h.clientState(),confirmed);assert.equal(h.find('AnchoredDropdown','游戏变速').props.selectedOption,'4');
 assert.equal(h.mutations.length,2);assert.equal(h.timers.size,0);
});
await speedCheck('matching failure releases the dropdown while retaining the old speed and error',async h=>{
 const request=chooseSpeed(h);speedWaiting(h);
 h.reply(false,request,{error:'游戏变速执行失败',notice:'must not claim success'});await settleSpeed();
 assert.equal(h.clientState().busy,false);assert.equal(h.clientState().notice,'游戏变速执行失败');
 assert.equal(h.find('AnchoredDropdown','游戏变速').props.disabled,false);
 assert.equal(h.find('AnchoredDropdown','游戏变速').props.selectedOption,'1');
 assert.equal(h.find('AnchoredDropdown','游戏变速 · 等待确认'),undefined);
 h.advance(4000);assert.equal(h.mutations.length,1);assert.equal(h.timers.size,0);
});
await speedCheck('hidden pending speed is bounded by two seconds and late callbacks cannot revive it',async h=>{
 const request=chooseSpeed(h),stale=h.find('AnchoredDropdown','游戏变速').props,pendingCallbacks=[...h.timers.values()];
 h.hide();h.advance(1999);await settleSpeed();speedWaiting(h);
 h.advance(1);await settleSpeed();h.nodes();h.nodes();
 assert.equal(h.clientState().busy,false);assert.equal(h.clientState().connected,true);assert.equal(h.clientState().snapshot,null);assert.equal(h.timers.size,0);
 const sent=h.requests.length,closed=h.clientState();
 for(const timer of pendingCallbacks)timer.fn();
 h.reply(true,request,{notice:'hidden late success'});stale.onChange({data:'4'});stale.onMenuWillOpen(()=>{});
 h.advance(30000);await settleSpeed();
 assert.deepEqual(h.clientState(),closed);assert.equal(h.requests.length,sent);assert.equal(h.mutations.length,1);assert.equal(h.timers.size,0);
});
for(const action of ['unmount','dispose'])await speedCheck(action+' preserves accepted work until settlement or real unload and fences late work',async h=>{
 const request=chooseSpeed(h),stale=h.find('AnchoredDropdown','游戏变速').props,pendingCallbacks=[...h.timers.values()];
 if(action==='dispose')h.close();else {h.unmount();assert.equal(h.clientState().busy,true);h.reply(true,request);}
 await settleSpeed();assert.equal(h.timers.size,0);assert.equal(h.clientState().connected,action==='unmount');assert.equal(h.clientState().busy,false);
 const sent=h.requests.length,closed=h.clientState();
 for(const timer of pendingCallbacks)timer.fn();
 h.reply(true,request,{notice:'disposed late success'});stale.onChange({data:'4'});stale.onMenuWillOpen(()=>{});
 h.advance(30000);await settleSpeed();
 assert.deepEqual(h.clientState(),closed);assert.equal(h.requests.length,sent);assert.equal(h.mutations.length,1);assert.equal(h.timers.size,0);
});


function controllerMenuHarness(withGame=false){
 const h=harness(true),s=h.state.snapshot;
 s.global.fields={padPersona:{value:'steamdeck',supported:true,choices:[{data:'disabled',label:'关闭虚拟手柄'},{data:'steamdeck',label:'SteamDeck'},{data:'elite',label:'Xbox'}]},gyroEnabled:{value:'on',supported:true,choices:[{data:'on',label:'开启'},{data:'off',label:'关闭'}]},gyroPreset:{value:'fps',supported:true,choices:[{data:'fps',label:'FPS射击'},{data:'racing',label:'赛车'}]}};
 s.virtualController={text:'虚拟手柄-成功开启',detail:'实际原生运行回执',tone:'on',locked:false};
 if(withGame){s.game={identity:'game-A',label:'游戏',fields:{...structuredClone(s.global.fields),profile:{value:'global',supported:true,sourceRevision:'profile-A',choices:[{data:'global',label:'全局配置'},{data:'dedicated',label:'游戏专属'}]}}};s.actions.game=true;}
 s.revision++;h.publish();return h;
}
for(const [field,label,value]of [['padPersona','虚拟手柄','elite'],['gyroPreset','陀螺仪预设','racing']]){
 for(const withGame of [false,true])await check(label+' stays open across unrelated snapshots and submits once using the newest real MirrorClient receipt '+withGame,async()=>{
  const h=controllerMenuHarness(withGame);h.find('AnchoredDropdown',label).props.onMenuWillOpen(()=>{});
  assert.equal(h.find('AnchoredDropdown',label).props.opened,true);
  for(let i=0;i<8;i++){h.state.snapshot.revision++;h.state.snapshot.monitor.enabled=!h.state.snapshot.monitor.enabled;h.publish();assert.equal(h.find('AnchoredDropdown',label).props.opened,true);}
  h.find('AnchoredDropdown',label).props.onChange({data:value});assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].revision,h.state.snapshot.revision);
  assert.equal(h.mutations[0].command,withGame?'game.setField':'global.setField');assert.equal(h.mutations[0].args.field,field);
  h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('AnchoredDropdown',label).props.opened,false);h.close();assert.equal(h.timers.size,0);
 });
 await check(label+' keeps its original timeout despite refresh and closes without writing',()=>{
  const h=controllerMenuHarness();h.find('AnchoredDropdown',label).props.onMenuWillOpen(()=>{});h.advance(44000);h.state.snapshot.revision++;h.publish();assert.equal(h.find('AnchoredDropdown',label).props.opened,true);h.advance(1000);assert.equal(h.find('AnchoredDropdown',label).props.opened,false);assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);
 });
 for(const [cause,change]of [
  ['value',s=>s.game.fields[field].value=value],
  ['choices',s=>s.game.fields[field].choices[0].disabled=true],
  ['support',s=>s.game.fields[field].supported=false],
  ['profile',s=>s.game.fields.profile.sourceRevision='profile-B'],
  ['power',s=>s.powerSource='dc'],
  ['game',s=>s.game.identity='game-B'],
  ['startup lock',s=>s.virtualController.locked=true],
 ])await check(label+' cancels on a real '+cause+' change and ignores its old menu callback',()=>{
  const h=controllerMenuHarness(true);h.find('AnchoredDropdown',label).props.onMenuWillOpen(()=>{});const old=h.find('AnchoredDropdown',label).props;
  change(h.state.snapshot);h.state.snapshot.revision++;h.publish();assert.equal(h.find('AnchoredDropdown',label).props.opened,false);old.onChange({data:value});assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);
 });
}
await check('controller status appears only beside the controller and startup protection disables its trigger',()=>{
 const h=controllerMenuHarness(),status=()=>h.nodes().filter(n=>n.props.className==='ymcc-controller-status');assert.equal(status().length,1);
 h.state.snapshot.virtualController={text:'虚拟手柄-设备创建中',detail:'等待原生启动确认',tone:'pending',locked:true};h.state.snapshot.revision++;h.publish();
 assert.equal(status().length,1);assert.equal(h.find('AnchoredDropdown','虚拟手柄').props.disabled,true);
 assert.equal(h.find('AnchoredDropdown','虚拟手柄').props.onMenuWillOpen(()=>{}),false);assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);
});

for(const [field,value]of [['tone','invented'],['locked','yes'],['text','x'.repeat(513)],['detail','x'.repeat(2049)]])await check('actual client rejects malformed bounded virtual-controller '+field+' data before any edit',()=>{
 const h=controllerMenuHarness();h.state.snapshot.virtualController[field]=value;h.state.snapshot.revision++;h.publish();assert.equal(h.clientState().connected,false);assert.equal(h.mutations.length,0);h.close();assert.equal(h.timers.size,0);
});

for(const withGame of [false,true])await check('explicit controller reselection reaches YMCC once even when the saved persona already matches '+withGame,async()=>{
 const h=controllerMenuHarness(withGame);h.find('AnchoredDropdown','虚拟手柄').props.onMenuWillOpen(()=>{});h.find('AnchoredDropdown','虚拟手柄').props.onChange({data:'steamdeck'});
 assert.equal(h.mutations.length,1,'explicit selection is an intent to the original YMCC input owner, not a client no-op');assert.equal(h.mutations[0].args.value,'steamdeck');h.reply();await new Promise(r=>setImmediate(r));h.close();assert.equal(h.timers.size,0);
});
fs.writeFileSync(path.join(validation,speedOnly?'SPEED-ACK-REGRESSION.json':backgroundOnly?'BACKGROUND-INTENT-REGRESSION.json':entryOnly?'ENTRY-FOCUS-REGRESSION.json':menuOnly?'MENU-ACTIONS-REGRESSION.json':'SLIDER-DRAG-REGRESSION.json'),JSON.stringify({status:'passed',scope:speedOnly?'speed-ack':backgroundOnly?'background-intent':entryOnly?'entry-focus':menuOnly?'menu-actions':'all',executedThisRun:true,cases:cases.length,results:cases,skippedCases:skipped.length,productionTsx:true,strictBrowserTimers:true,virtualClock:true,realHardwareWrites:0},null,2)+'\n');console.log((speedOnly?'Speed acknowledgment':'Slider drag')+' regression PASS '+cases.length);
