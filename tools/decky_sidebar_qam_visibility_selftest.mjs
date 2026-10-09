import { task } from './fixtures/decky_task_paths.mjs';
// Exact cached upstream hooks + production plugin/client, inert React and sockets. Not actual Steam or DFL layout.
import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import crypto from 'node:crypto';
import {createRequire} from 'node:module';import {execFileSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..');
const archive='G:/YeManCC-Work/Archives/Steam-QAM-YMCC-Wishlist-20261009';
const require=createRequire(path.join(root,'package.json')),{build,transformSync}=require('esbuild');
const baseline=process.argv.includes('--baseline');
const legacyBytes=execFileSync('tar',['-xOf',path.join(archive,'downloads/decky--ui--4.12.1.tgz'),'package/dist/custom-hooks/useQuickAccessVisible.js']);
const modernFile=path.join(archive,'sources/SteamDeckHomebrew--decky-loader/decky-loader-75563316f9119ee7e36be7f43885be65e877fad0/frontend/src/components/QuickAccessVisibleState.tsx');
const legacyCode=transformSync(legacyBytes.toString(),{loader:'js',format:'cjs'}).code;
const modernCode=transformSync(fs.readFileSync(modernFile,'utf8'),{loader:'tsx',format:'cjs',jsxFactory:'SP_REACT.createElement'}).code;
const plugins=baseline?[]:(await import('./decky_sidebar_decky_api_build.mjs')).deckyApiPlugins();
const bundle=await build({entryPoints:[path.join(root,'decky-plugin/src/index.tsx')],bundle:true,write:false,format:'cjs',platform:'browser',target:'es2020',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment',plugins});
const code=bundle.outputFiles[0].text;
function harness({navPresent=false,documentHidden=false,visible=false,apiMissing=false,apiOneOnly=false}={}){
  const slots=[],effects=[],contexts=[],events=new Map(),sockets=[],timers=new Map(),connectCalls=[],errors=[];
  let cursor=0,timerId=0,navCalls=0,documentListeners=0;
  const React={
    createElement:(type,props,...children)=>({type,props:props??{},children}),Fragment:'Fragment',
    createContext:value=>{const context={value,Provider:'Provider'};contexts.push(context);return context;},
    useContext:context=>{cursor++;return context.value;},
    useRef:initial=>{const index=cursor++;if(!(index in slots))slots[index]={current:initial};return slots[index];},
    useState:initial=>{const index=cursor++;if(!(index in slots))slots[index]=typeof initial==='function'?initial():initial;return [slots[index],value=>{slots[index]=typeof value==='function'?value(slots[index]):value;}];},
    useEffect:(callback,deps)=>{const index=cursor++,old=effects[index];if(!old||deps.length!==old.deps.length||deps.some((value,i)=>!Object.is(value,old.deps[i])))effects[index]={callback,deps,cleanup:old?.cleanup,pending:true};}
  };
  const document={hidden:documentHidden};
  const quickWindow={document,addEventListener:()=>{documentListeners++;},removeEventListener:()=>{documentListeners--;}};
  const evaluate=(source,extra)=>{const module={exports:{}};vm.runInNewContext(source,{module,exports:module.exports,SP_REACT:React,console:{error:value=>errors.push(String(value)),warn:value=>errors.push(String(value)),log:()=>{}},...extra});return module.exports;};
  const legacy=evaluate(legacyCode,{require:name=>{if(name==='react')return React;if(name==='../utils')return {getGamepadNavigationTrees:()=>{navCalls++;return navPresent?[{id:'QuickAccess-NA',m_Root:{m_element:{ownerDocument:{defaultView:quickWindow}}}}]:[];}};throw Error('Unexpected legacy import '+name);}});
  const modern=evaluate(modernCode,{require:name=>{assert.equal(name,'react');return React;}});
  assert.equal(contexts.length,1);contexts[0].value=visible;
  const window={__YMCC_DECKY_MIRROR__:{endpoint:'ws://127.0.0.1:12345/mirror',token:'UI-TEST-ONLY',runId:'run-1'},addEventListener:(name,fn)=>events.set(name,fn),removeEventListener:(name,fn)=>{if(events.get(name)===fn)events.delete(name);}};
  if(!apiMissing)window.__DECKY_SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED_deckyLoaderAPIInit={connect:(version,name)=>{connectCalls.push({version,name});if(apiOneOnly&&version===2)throw Error('Version2 unavailable');return {_version:apiOneOnly?1:2,useQuickAccessVisible:apiOneOnly?undefined:modern.useQuickAccessVisible};}};
  class Socket{
    listeners=new Map();sent=[];closed=false;
    constructor(url){this.url=url;sockets.push(this);}
    addEventListener(name,fn){this.listeners.set(name,fn);}removeEventListener(name,fn){if(this.listeners.get(name)===fn)this.listeners.delete(name);}
    send(value){this.sent.push(JSON.parse(value));}close(){if(this.closed)return;this.closed=true;this.listeners.get('close')?.();}
    emit(name,value){this.listeners.get(name)?.({data:JSON.stringify(value)});}
  }
  const DFL={PanelSection:'PanelSection',PanelSectionRow:'PanelSectionRow',SliderField:'SliderField',DropdownItem:'DropdownItem',ToggleField:'ToggleField',Router:{},useQuickAccessVisible:legacy.useQuickAccessVisible};
  const module={exports:{}};
  let plugin,error;
  try{vm.runInNewContext(code,{module,exports:module.exports,SP_REACT:React,DFL,window,WebSocket:Socket,crypto:{randomUUID:()=> 'ui-client-test'},structuredClone,console:{warn:value=>errors.push(String(value)),error:value=>errors.push(String(value)),log:()=>{}},setTimeout:(action,delay)=>{const id=++timerId;timers.set(id,{action,delay});return id;},clearTimeout:id=>timers.delete(id)});plugin=module.exports.default();}catch(failure){error=failure;}
  function render(){assert.ok(plugin,error?.message);cursor=0;const tree=plugin.content.type();for(const effect of effects){if(effect?.pending){effect.cleanup?.();effect.pending=false;effect.cleanup=effect.callback();}}return tree;}
  function unmount(){for(const effect of effects)effect?.cleanup?.();plugin?.onDismount();}
  return {render,unmount,plugin,error,sockets,timers,events,connectCalls,errors,setVisible:value=>contexts[0].value=value,bootstrapEvent:()=>events.get('ymcc-decky-bootstrap')?.(),navCalls:()=>navCalls,documentListeners:()=>documentListeners,fireDelay:delay=>{for(const [id,timer] of [...timers])if(timer.delay===delay){timers.delete(id);timer.action();}},seed:()=>{const socket=sockets.at(-1);socket.emit('open',{});socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:{powerSource:'ac',runId:'run-1',generation:1,revision:1,ready:true,game:{label:'Test',identity:'fixture-exe',fields:{acMode:{value:'default',choices:[{data:'default',label:'Global'},{data:'performance',label:'Performance'}],supported:true},padPersona:{value:'follow',choices:[{data:'follow',label:'Follow'},{data:'elite',label:'Xbox'}],supported:true}}},fan:{enabled:false,supported:true,preset:'balanced',choices:[{data:'soft',label:'Soft'}],canToggle:true},actions:{fan:true,game:true}}});}};
}
const cases=[];async function check(name,fn){await fn();cases.push(name);}
if(baseline){
  const h=harness({visible:false,navPresent:false});h.render();
  assert.equal(h.sockets.length,1);assert.ok(h.navCalls()>0);h.unmount();assert.equal(h.timers.size,0);
  const report={project:'YMCC 控制台',stage:'Mainline19',phase:'baseline',loaderContextVisible:false,legacyNavigationWindowAvailable:false,observedProductionClientSocketAttempts:1,actualLegacyNpmHookExecuted:true,actualProductionPluginClientExecuted:true,actualSteam:false,realNetworkSockets:0,realConfigWrites:0,hardwareWrites:0,note:'Deprecated DFL hook defaults to visible when QuickAccess navigation window is missing. This is an integration contract failure, not proof that the real Steam window is missing on this PC.'};
  fs.writeFileSync(path.join(task,'validation/QAM-VISIBILITY14-BASELINE.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));process.exit(0);
}
await check('loader hidden + missing navigation window creates no production mirror socket or timer',()=>{const h=harness();h.render();assert.equal(h.sockets.length,0);assert.equal(h.timers.size,0);assert.equal(h.navCalls(),0);assert.equal(h.documentListeners(),0);assert.deepEqual(h.connectCalls,[{version:2,name:'ymcc-sidebar'}]);h.unmount();});
await check('visible loader context opens the original production client once, without deprecated navigation lookup',()=>{const h=harness({visible:true});h.render();assert.equal(h.sockets.length,1);assert.equal(h.navCalls(),0);h.render();assert.equal(h.sockets.length,1);assert.equal(h.connectCalls.length,1);h.unmount();assert.equal(h.timers.size,0);});
await check('hiding loader context closes original socket and cancels every existing deadline',()=>{const h=harness({visible:true});h.render();h.setVisible(false);h.render();assert.equal(h.sockets[0].closed,true);assert.equal(h.timers.size,0);h.unmount();});
await check('hide cancels pending original reconnect; reopen uses the same SDK connection and a new socket',()=>{const h=harness({visible:true});h.render();h.sockets[0].close();assert.equal(h.timers.size,1);h.setVisible(false);h.render();assert.equal(h.timers.size,0);h.setVisible(true);h.render();assert.equal(h.sockets.length,2);assert.equal(h.connectCalls.length,1);h.unmount();});
await check('visible document cannot override hidden modern loader context',()=>{const h=harness({visible:false,navPresent:true,documentHidden:false});h.render();assert.equal(h.sockets.length,0);assert.equal(h.navCalls(),0);h.unmount();});
await check('hidden bootstrap event does not create a socket or new SDK connection',()=>{const h=harness();h.render();h.bootstrapEvent();assert.equal(h.sockets.length,0);assert.equal(h.connectCalls.length,1);h.unmount();});
await check('onDismount releases bootstrap listeners and all current production-client timers/sockets',()=>{const h=harness({visible:true});h.render();h.unmount();assert.equal(h.events.size,0);assert.equal(h.timers.size,0);assert.ok(h.sockets.every(socket=>socket.closed));});
await check('missing actual SDK init rejects module before creating a mirror listener/socket',()=>{const h=harness({apiMissing:true});assert.match(h.error?.message??'',/Failed to connect/);assert.equal(h.events.size,0);assert.equal(h.sockets.length,0);assert.equal(h.timers.size,0);});
await check('old API without context hook fails closed before listener/client admission instead of legacy fallback',()=>{const h=harness({apiOneOnly:true});assert.match(h.error?.message??'',/API.*2|QAM.*hook|可见性/);assert.equal(h.events.size,0);assert.equal(h.sockets.length,0);assert.equal(h.timers.size,0);assert.deepEqual(h.connectCalls.map(call=>call.version),[2,1]);});
function widgets(tree,result=[]){if(Array.isArray(tree))for(const child of tree)widgets(child,result);else if(tree&&typeof tree==='object'){if(tree.type==='SliderField'||tree.type==='DropdownItem'||tree.type?.name==='AnchoredDropdown')result.push(tree);widgets(tree.children,result);}return result;}
await check('native slider selection creates no modal lease or extra socket and sends original field intent',()=>{const h=harness({visible:true});h.render();h.seed();const control=widgets(h.render())[0];assert.equal(control.type,'SliderField');assert.equal(control.props.onMenuWillOpen,undefined);control.props.onChange(1);assert.equal(h.sockets[0].sent.at(-1).command,'snapshot');h.fireDelay(3000);const request=h.sockets[0].sent.at(-1);assert.equal(request.command,'game.setField');assert.equal(request.args.value,'performance');assert.equal(h.sockets.length,1);assert.equal([...h.timers.values()].some(t=>t.delay===45000),false);h.unmount();});
await check('hiding QAM always closes native slider client with no retained modal deadline',()=>{const h=harness({visible:true});h.render();h.seed();widgets(h.render());h.setVisible(false);h.render();assert.equal(h.sockets[0].closed,true);assert.equal(h.timers.size,0);h.fireDelay(45000);assert.equal(h.timers.size,0);h.unmount();});
await check('late hidden slider callback cannot mutate a hidden client or reconnect it',async()=>{const h=harness({visible:true});h.render();h.seed();const control=widgets(h.render())[0],count=h.sockets[0].sent.length;h.setVisible(false);h.render();control.props.onChange(1);for(let i=0;i<10;i++)await Promise.resolve();assert.equal(h.sockets[0].sent.length,count);assert.equal(h.sockets.length,1);assert.equal(h.timers.size,0);h.unmount();});
await check('inline hand dropdown cannot retain a hidden QAM socket or execute a stale option',async()=>{const h=harness({visible:true});h.render();h.seed();const control=widgets(h.render()).find(w=>w.type?.name==='AnchoredDropdown');control.props.onMenuWillOpen(()=>{});h.render();h.setVisible(false);h.render();h.render();assert.equal(h.sockets[0].closed,true);assert.equal(h.timers.size,0);const count=h.sockets[0].sent.length;control.props.onChange({data:'elite'});for(let i=0;i<10;i++)await Promise.resolve();assert.equal(h.sockets[0].sent.length,count);h.unmount();});
await check('inline hand cancel and bounded deadline release their QAM lease without polling',()=>{for(const cancel of [true,false]){const h=harness({visible:true});h.render();h.seed();const control=widgets(h.render()).find(w=>w.type?.name==='AnchoredDropdown');control.props.onMenuWillOpen(()=>{});h.render();if(cancel)control.props.onCancel();else h.fireDelay(45000);h.render();h.setVisible(false);h.render();assert.equal(h.sockets[0].closed,true);assert.equal(h.timers.size,0);h.unmount();}});
const report={project:'YMCC 控制台',stage:'Mainline19',cases:cases.length,results:cases,exactCachedApiVersion:'1.1.3',exactCachedUiVersion:'4.12.1',actualLoaderContextHookExecuted:true,actualProductionBundledPluginClientExecuted:true,inertReact:true,inertSocketAndClock:true,actualSteam:false,actualDflWidgets:false,layoutMeasured:false,realConfigWrites:0,physicalHardwareWrites:0,newRuntimePolling:0,normalSdkConnectsPerModule:1,deprecatedDocumentVisibilityListeners:0,legacyHookSha256:crypto.createHash('sha256').update(legacyBytes).digest('hex')};
fs.writeFileSync(path.join(task,'validation/QAM-VISIBILITY14-FIXED.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
