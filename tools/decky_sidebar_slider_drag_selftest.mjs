// Exact shipped TSX/queues; strict browser timers, memory-only transport and clock.
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {root,validation} from './fixtures/decky_task_paths.mjs';
const {transformSync}=createRequire(path.join(root,'package.json'))('esbuild');
function load(file,modules={},globals={}){const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(file,'utf8'),{loader:file.endsWith('tsx')?'tsx':'ts',format:'cjs',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment'}).code,{module,exports:module.exports,structuredClone,console,...globals,require:n=>{assert(Object.hasOwn(modules,n),'Unexpected import '+n);return modules[n];}});return module.exports;}
const model=load(path.join(root,'decky-plugin/src/performanceModel.ts')),queue=load(path.join(root,'decky-plugin/src/performanceIntentQueue.ts')),fan=load(path.join(root,'decky-plugin/src/fanChoiceDebounce.ts'));
function harness(real=false){let now=0,id=0,cursor=0,visible=true;let hooks=[],effects=[],cleanups=new Map();const listeners=new Set(),timers=new Map(),mutations=[],requests=[],views=new Map();let activeView='main';
const state={connected:true,busy:false,notice:'',snapshot:{runId:'a'.repeat(32),revision:1,generation:1,ready:true,powerSource:'ac',game:null,actions:{game:false,performance:true,frames:true,fan:true,global:true,monitor:true},performance:{version:1,side:'ac',scope:'global',mode:'auto',sourceRevision:'1',enabled:true,controls:[{key:'tdp',label:'TDP 功耗',kind:'slider',min:5,max:30,step:1,value:20,enabled:true}]},frames:{dedicated:false,pair:{ac:{fps:60,lastFps:60,ceiling:120},dc:{fps:60,lastFps:60,ceiling:60}}},fan:{supported:true,enabled:false,preset:'balanced',canToggle:true,choices:fan.FAN_CHOICES.slice(1)},monitor:{enabled:true},global:{fields:{gyroPreset:{value:'fps',supported:true,choices:[{data:'fps',label:'FPS\ufffd\ufffd\ufffd'}]},speedFactor:{value:'1',supported:false,choices:[{data:'1',label:'1\ufffd\ufffd'}]}}}}};
const react={Fragment:'Fragment',createElement:(type,props,...children)=>({type,props:props||{},children}),useState(initial){const i=cursor++;if(!(i in hooks))hooks[i]=initial;const ownerHooks=hooks;return[ownerHooks[i],v=>{ownerHooks[i]=typeof v==='function'?v(ownerHooks[i]):v;}];},useRef(initial){const i=cursor++;return hooks[i]??={current:initial};},useEffect(fn,deps){const i=cursor++,old=hooks[i];if(!old||!deps||deps.some((v,i)=>v!==old[i])){hooks[i]=deps;effects.push(()=>{cleanups.get(i)?.();const cleanup=fn();if(typeof cleanup==='function')cleanups.set(i,cleanup);else cleanups.delete(i);});}}};
const schedule=function(fn,ms){if(this!==undefined)throw TypeError('Illegal invocation');timers.set(++id,{fn,at:now+ms});return id;},cancel=function(i){if(this!==undefined)throw TypeError('Illegal invocation');timers.delete(i);};
class Client{snapshot(){return state;}subscribe(fn){listeners.add(fn);fn(state);return()=>listeners.delete(fn);}setVisible(){}bootstrapChanged(){}dispose(){}mutate(command,args,expected){assert.equal(expected.revision,state.snapshot.revision,'stale transport receipt');mutations.push({command,args:structuredClone(args),revision:expected.revision});return Promise.resolve();}}
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
const api={state,mutations,requests,timers,clientState:()=>real?realClient.snapshot():structuredClone(state),nodes:render,reply(ok=true,request=mutations.at(-1),fields={}){sockets.at(-1).emit('message',{type:'reply',runId:state.snapshot.runId,id:request.id,ok,result:{saved:true,applied:true},notice:'性能调度已记忆并提交 YMCC',...fields});},find(type,label=''){return render().find(n=>n.type===type&&text(n.props.label).startsWith(label));},publish(){if(real)sockets.at(-1).emit('message',{type:'snapshot',runId:state.snapshot.runId,snapshot:state.snapshot});else for(const f of listeners)f(state);},hide(){visible=false;render();},show,unmount,advance(ms){now+=ms;for(const[i,t]of [...timers])if(t.at<=now){timers.delete(i);t.fn();}},close(){for(const key of views.keys())withView(key,unmount);unmount();plugin.onDismount();}};
function withView(key,fn){if(key===activeView)return fn();const prior={key:activeView,hooks,effects,cleanups,cursor,visible},next=views.get(key)??{hooks:[],effects:[],cleanups:new Map(),cursor:0,visible:false};activeView=key;({hooks,effects,cleanups,cursor,visible}=next);try{return fn();}finally{views.set(key,{hooks,effects,cleanups,cursor,visible});activeView=prior.key;({hooks,effects,cleanups,cursor,visible}=prior);}}
api.view=key=>new Proxy(api,{get(target,prop){const value=Reflect.get(target,prop);return typeof value==='function'?(...args)=>withView(key,()=>value(...args)):value;}});return api;
}
// A bounded invocation of this existing consumer; skipped cases are never PASS.
const speedOnly=process.argv.includes('--speed-only'),skipped=[];
const cases=[];async function check(name,fn){if(speedOnly&&!name.startsWith('speed ack: ')){skipped.push(name);return;}try{await fn();cases.push(name);}catch(error){throw new Error(name+": "+error.message,{cause:error});}}
await check('repeated slider changes cancel a browser timer with its Window receiver intact',()=>{const h=harness(),p=h.find('PerformanceControls').props;p.change('tdp',21);p.change('tdp',22);h.advance(3000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,22);h.close();});
await check('holding a performance slider for ten seconds never commits, release commits one final value against the latest receipt',()=>{const h=harness(),p=h.find('PerformanceControls').props;p.begin();for(let n=21;n<=25;n++)p.change('tdp',n);h.advance(10000);assert.equal(h.mutations.length,0);assert.equal(h.timers.size,0);h.state.snapshot.revision++;h.publish();p.end();h.advance(2999);assert.equal(h.mutations.length,0);h.advance(1);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].revision,2);assert.equal(h.mutations[0].args.patch.tdp,25);h.close();});
await check('switching AC to DC cancels the held performance edit',()=>{const h=harness(),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);h.state.snapshot.performance.side='dc';h.state.snapshot.powerSource='dc';h.publish();p.end();h.advance(10000);assert.equal(h.mutations.length,0);h.close();});
await check('fan drag remains pending while held and sends only the final preset after release',()=>{const h=harness(),p=h.find('SliderField','风扇控制').props;p.onChangeStart();p.onChange(1);p.onChange(2);h.advance(10000);assert.equal(h.mutations.length,0);p.onChangeComplete(2);h.advance(3000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.choice,'balanced');h.close();});
await check('frame float positions round to the five-FPS grid and a newer unrelated receipt is admitted',()=>{const h=harness(),p=h.find('SliderField','锁帧率').props;p.onChangeStart();p.onChange(54.99999999999999);h.state.snapshot.revision++;h.publish();h.find('SliderField','锁帧率').props.onChangeComplete(54.99999999999999);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].revision,2);assert.equal(h.mutations[0].args.value,'55');h.close();});
await check('a frame source change while held never writes the old AC target',()=>{const h=harness(),p=h.find('SliderField','锁帧率').props;p.onChangeStart();p.onChange(55);h.state.snapshot.powerSource='dc';h.state.snapshot.revision++;h.publish();p.onChangeComplete(55);assert.equal(h.mutations.length,0);h.close();});
await check('closing the sidebar cancels a held slider without an automatic replay',()=>{const h=harness(),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);h.hide();p.end();h.advance(10000);assert.equal(h.mutations.length,0);h.close();});
await check('float normalization handles integral, stepped, clamped, zero-range and invalid inputs',()=>{const n=model.normalizeSliderValue;assert.equal(n(24.999999999999996,5,30,1),25);assert.equal(n(54.99999999999999,15,120,5),55);assert.equal(n(0.30000000000000004,0,1,.1),.3);assert.equal(n(500,15,119,5),115);assert.equal(n(-20,5,30,1),5);assert.equal(n(5,5,5,1),5);for(const x of [NaN,Infinity,'25',null])assert.equal(n(x,5,30,1),null);});
await check('the latest native completion handler also rejects a source changed during the original drag',()=>{const h=harness(),p=h.find('SliderField','锁帧率').props;p.onChangeStart();p.onChange(55);h.state.snapshot.powerSource='dc';h.state.snapshot.revision++;h.publish();h.find('SliderField','锁帧率').props.onChangeComplete(55);assert.equal(h.mutations.length,0);h.close();});
await check('stable gyro and speed enum values display correct text with an older corrupted host',()=>{const h=harness();assert.equal(h.find('AnchoredDropdown','陀螺仪预设').props.rgOptions[0].label,'FPS射击');assert.equal(h.find('AnchoredDropdown','游戏变速').props.rgOptions[0].label,'1×');h.close();});
await check('five visual ticks preserve continuous one-watt steps and the four actual fan presets',()=>{const h=harness();assert.equal(h.find('SliderField','锁帧率').props.notchCount,5);assert.equal(h.find('SliderField','锁帧率').props.step,5);assert.equal(h.find('SliderField','风扇控制').props.notchCount,4);const react={Fragment:'Fragment',createElement:(type,props,...children)=>({type,props:props||{},children}),useState:()=>[null,()=>{}],useEffect(){}};const ui=load(process.env.YMCC_PERFORMANCE_CONTROL_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/PerformanceControls.tsx'),{'./performanceModel':model,'./AnchoredDropdown':{AnchoredDropdown:'Dropdown'}},{SP_REACT:react,DFL:{SliderField:'SliderField',PanelSectionRow:'Row',ToggleField:'Toggle'}});let changed;const tree=ui.PerformanceControls({view:h.state.snapshot.performance,disabled:false,draft:{},change:(k,v)=>changed=[k,v]});const visit=n=>Array.isArray(n)?n.flatMap(visit):n&&typeof n==='object'?[n,...visit(n.children)]:[];const slider=visit(tree).find(n=>n.type==='SliderField');assert.equal(slider.props.notchCount,5);assert.equal(slider.props.step,1);assert.equal(slider.props.min,5);assert.equal(slider.props.max,30);slider.props.onChange(24.999999999999996);assert.deepEqual(changed,['tdp',25]);h.close();});

await check('TDP editing and applying preserve the actual product row count and optimistic value',async()=>{const h=harness(),before=h.nodes().filter(n=>n.type==='PanelSectionRow').length,p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,before);assert.equal(h.find('PerformanceControls').props.draft.tdp,25);p.end();h.advance(3000);assert.equal(h.find('PerformanceControls').props.draft.tdp,25);assert.equal(h.find('PerformanceControls').props.status,'正在应用…');assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,before);h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='2';h.publish();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.view.controls[0].value,25);assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,before);h.close();});
await check('unrelated TDP saves keep native toggle identity and use Steam controlled values',()=>{const react={Fragment:'Fragment',createElement:(type,props,...children)=>({type,props:props||{},children}),useState:()=>[null,()=>{}],useEffect(){}};const ui=load(process.env.YMCC_PERFORMANCE_CONTROL_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/PerformanceControls.tsx'),{'./performanceModel':model,'./AnchoredDropdown':{AnchoredDropdown:'Dropdown'}},{SP_REACT:react,DFL:{SliderField:'SliderField',PanelSectionRow:'Row',ToggleField:'Toggle'}});const view={version:1,side:'ac',scope:'global',mode:'auto',sourceRevision:'before',enabled:true,controls:[{key:'saving',kind:'toggle',label:'节能浮动',value:false,enabled:true}]};const flatten=n=>Array.isArray(n)?n.flatMap(flatten):n&&typeof n==='object'?[n,...flatten(n.children)]:[];const render=(v,disabled,draft={})=>flatten(ui.PerformanceControls({view:v,disabled,draft,change(){}})).find(n=>n.type==='Toggle');const a=render(view,false),b=render({...view,sourceRevision:'after'},true),c=render({...view,sourceRevision:'after'},false);assert.equal(a.props.key,b.props.key);assert.equal(a.props.key,c.props.key);assert.equal(a.props.controlled,true);assert.equal(render(view,false,{saving:true}).props.checked,true);assert.equal(render(view,false).props.checked,false);});

await check('actual MirrorClient reply notice and later snapshot never insert or remove product rows',async()=>{const h=harness(true),base=h.nodes().filter(n=>n.type==='PanelSectionRow').length,p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);p.end();h.advance(3000);assert.equal(h.mutations.length,1);assert.equal(h.find('PerformanceControls').props.draft.tdp,25);assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,base);h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.draft.tdp,25);assert.equal(h.find('PerformanceControls').props.status,'等待状态确认…');assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,base);h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='new';h.publish();assert.equal(h.find('PerformanceControls').props.draft.tdp,undefined);assert.equal(h.nodes().filter(n=>n.type==='PanelSectionRow').length,base);assert.equal(h.mutations.length,1);h.close();assert.equal(h.timers.size,0);});

// Same loaded module, fresh QAM page: old native callbacks must lose authority.
await check('a late slider change from the previous Big Picture page cannot enqueue an edit after reentry',()=>{
 const h=harness(),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);h.hide();h.show();old.change('tdp',29);old.end();h.advance(10000);assert.equal(h.mutations.length,0);assert.equal(Object.keys(h.find('PerformanceControls').props.draft).length,0);h.close();assert.equal(h.timers.size,0);
});
await check('a previous page release cannot end the new page held TDP gesture',()=>{
 const h=harness(),old=h.find('PerformanceControls').props;h.hide();h.show();const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);old.end();h.advance(10000);assert.equal(h.mutations.length,0);fresh.end();h.advance(3000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,24);h.close();
});
await check('unmounted Content callbacks cannot edit the replacement Content in the same loaded plugin',()=>{
 const h=harness(),old=h.find('PerformanceControls').props;h.unmount();h.show();old.begin();old.change('tdp',29);old.end();h.advance(10000);assert.equal(h.mutations.length,0);const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',23);fresh.end();h.advance(3000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,23);h.close();assert.equal(h.timers.size,0);
});
await check('same-module Big Picture reentry uses fresh MirrorClient snapshots and one new write',async()=>{
 const h=harness(true),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);h.hide();await new Promise(r=>setImmediate(r));h.show();old.change('tdp',29);old.end();h.advance(10000);assert.equal(h.mutations.length,0);const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);fresh.end();h.advance(3000);assert.equal(h.mutations.length,1);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=24;h.state.snapshot.performance.sourceRevision='reentered';h.publish();assert.equal(h.find('PerformanceControls').props.draft.tdp,undefined);h.close();assert.equal(h.timers.size,0);
});
await check('an old MirrorClient reply after reentry cannot confirm or replace a new TDP intent',async()=>{
 const h=harness(true),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);old.end();h.advance(3000);const oldRequest=h.mutations[0];h.hide();await new Promise(r=>setImmediate(r));h.show();const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);fresh.end();h.advance(3000);assert.equal(h.mutations.length,2);h.reply(true,oldRequest);await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.draft.tdp,24);assert.equal(h.find('PerformanceControls').props.status,'正在应用…');h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.status,'等待状态确认…');h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=24;h.state.snapshot.performance.sourceRevision='fresh';h.publish();assert.equal(h.find('PerformanceControls').props.draft.tdp,undefined);h.close();assert.equal(h.timers.size,0);
});
await check('reentry after a successful reply cancels its readback wait without replaying the old TDP',async()=>{
 const h=harness(true),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);p.end();h.advance(3000);h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.status,'等待状态确认…');h.hide();await new Promise(r=>setImmediate(r));h.show();h.advance(60000);assert.equal(h.mutations.length,1);assert.equal(Object.keys(h.find('PerformanceControls').props.draft).length,0);h.close();assert.equal(h.timers.size,0);
});
await check('ordinary visible rerenders preserve the active drag authority and final one-watt choice',()=>{
 const h=harness(),p=h.find('PerformanceControls').props;p.begin();p.change('tdp',24);for(let i=0;i<100;i++){h.state.snapshot.revision++;h.publish();h.nodes();}p.change('tdp',25);h.advance(10000);assert.equal(h.mutations.length,0);p.end();h.advance(3000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,25);assert.equal(h.mutations[0].revision,101);h.close();
});


await check('a previous page begin cannot cancel the replacement page released TDP deadline',()=>{
 const h=harness(),old=h.find('PerformanceControls').props;h.hide();h.show();const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);fresh.end();old.begin();h.advance(3000);assert.equal(h.mutations.length,1);assert.equal(h.mutations[0].args.patch.tdp,24);h.close();
});
await check('an old failure reply cannot roll back a replacement page TDP intent',async()=>{
 const h=harness(true),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);old.end();h.advance(3000);const oldRequest=h.mutations[0];h.hide();await new Promise(r=>setImmediate(r));h.show();const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',24);fresh.end();h.advance(3000);h.reply(false,oldRequest);await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.draft.tdp,24);assert.equal(h.find('PerformanceControls').props.status,'正在应用…');h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=24;h.state.snapshot.performance.sourceRevision='replacement';h.publish();h.close();assert.equal(h.timers.size,0);
});
await check('Content unmount and replacement use the actual MirrorClient without stale writes',async()=>{
 const h=harness(true),old=h.find('PerformanceControls').props;old.begin();old.change('tdp',25);h.unmount();await new Promise(r=>setImmediate(r));h.show();old.begin();old.change('tdp',29);old.end();h.advance(10000);assert.equal(h.mutations.length,0);const fresh=h.find('PerformanceControls').props;fresh.begin();fresh.change('tdp',23);fresh.end();h.advance(3000);assert.equal(h.mutations.length,1);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=23;h.state.snapshot.performance.sourceRevision='remounted';h.publish();h.close();assert.equal(h.timers.size,0);
});
await check('thirty same-module page replacements leave no timer or duplicate performance request',async()=>{
 const h=harness(true);for(let n=0;n<30;n++){h.unmount();await new Promise(r=>setImmediate(r));h.show();const p=h.find('PerformanceControls').props,value=n%2?24:25;p.begin();p.change('tdp',value);p.end();h.advance(3000);assert.equal(h.mutations.length,n+1);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=value;h.state.snapshot.performance.sourceRevision=String(n+10);h.publish();h.nodes();assert.equal(Object.keys(h.find('PerformanceControls').props.draft).length,0);assert.equal(h.timers.size,0);}h.close();assert.equal(h.timers.size,0);
});


await check('mounting an inactive QAM page cannot clear the active page authenticated snapshot',()=>{
 const h=harness(true);assert(h.find('PerformanceControls'));h.view('inactive').hide();assert(h.find('PerformanceControls'),'active performance controls must survive an inactive page mount');h.close();assert.equal(h.timers.size,0);
});
await check('hiding a different QAM page cannot cancel the active page held TDP gesture',async()=>{
 const h=harness(true),other=h.view('other');other.show();const p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);other.hide();assert.equal(h.find('PerformanceControls').props.draft.tdp,25);h.advance(10000);assert.equal(h.mutations.length,0);p.end();h.advance(3000);assert.equal(h.mutations.length,1);h.reply();await new Promise(r=>setImmediate(r));h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='other-hidden';h.publish();h.close();assert.equal(h.timers.size,0);
});
await check('unmounting another QAM page cannot reject the active page in-flight native write',async()=>{
 const h=harness(true),other=h.view('other');other.show();const p=h.find('PerformanceControls').props;p.begin();p.change('tdp',25);p.end();h.advance(3000);other.unmount();assert.equal(h.find('PerformanceControls').props.status,'正在应用…');h.reply();await new Promise(r=>setImmediate(r));assert.equal(h.find('PerformanceControls').props.status,'等待状态确认…');h.state.snapshot.revision++;h.state.snapshot.performance.controls[0].value=25;h.state.snapshot.performance.sourceRevision='other-unmounted';h.publish();assert.equal(h.mutations.length,1);h.close();assert.equal(h.timers.size,0);
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
 assert.equal(h.clientState().busy,false);assert.equal(h.clientState().connected,false);assert.equal(h.timers.size,0);
 const sent=h.requests.length,closed=h.clientState();
 for(const timer of pendingCallbacks)timer.fn();
 h.reply(true,request,{notice:'hidden late success'});stale.onChange({data:'4'});stale.onMenuWillOpen(()=>{});
 h.advance(30000);await settleSpeed();
 assert.deepEqual(h.clientState(),closed);assert.equal(h.requests.length,sent);assert.equal(h.mutations.length,1);assert.equal(h.timers.size,0);
});
for(const action of ['unmount','dispose'])await speedCheck(action+' cancels pending speed and fences all late work',async h=>{
 const request=chooseSpeed(h),stale=h.find('AnchoredDropdown','游戏变速').props,pendingCallbacks=[...h.timers.values()];
 if(action==='dispose')h.close();else h.unmount();
 await settleSpeed();assert.equal(h.timers.size,0);assert.equal(h.clientState().connected,false);assert.equal(h.clientState().busy,false);
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
fs.writeFileSync(path.join(validation,speedOnly?'SPEED-ACK-REGRESSION.json':'SLIDER-DRAG-REGRESSION.json'),JSON.stringify({status:'passed',scope:speedOnly?'speed-ack':'all',executedThisRun:true,cases:cases.length,results:cases,skippedCases:skipped.length,productionTsx:true,strictBrowserTimers:true,virtualClock:true,realHardwareWrites:0},null,2)+'\n');console.log((speedOnly?'Speed acknowledgment':'Slider drag')+' regression PASS '+cases.length);
