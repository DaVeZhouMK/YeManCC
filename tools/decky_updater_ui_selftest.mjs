// Actual native-control TSX logic. Inert hooks/transport only; this creates no UI screenshots.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),{build}=require('esbuild');
const originalSingleEntry='single update entry delegates only to actual YMCC package component';
let outputArgument=null,selectedCase=null,surfaceVariant=null;
for(let i=2;i<process.argv.length;i++){
 const arg=process.argv[i];
 if(arg.startsWith('--surface-variant=')){assert(surfaceVariant===null,'UPDATER_UI_VARIANT_DUPLICATE');surfaceVariant=arg.slice('--surface-variant='.length);assert(['route-read-work','route-read-lifecycle','cleanup'].includes(surfaceVariant),'UPDATER_UI_VARIANT_UNKNOWN');continue;}
 if(arg==='--case'||arg.startsWith('--case=')){
  assert(selectedCase===null,'UPDATER_UI_SELECTOR_DUPLICATE');const value=arg==='--case'?process.argv[++i]:arg.slice('--case='.length);
  assert(typeof value==='string'&&value.length>0,'UPDATER_UI_SELECTOR_EMPTY');assert(value===originalSingleEntry,'UPDATER_UI_SELECTOR_UNKNOWN');selectedCase=value;
 }else{assert(typeof arg==='string'&&arg.length>0&&!arg.startsWith('-')&&outputArgument===null,'UPDATER_UI_ARGUMENT_UNKNOWN');outputArgument=arg;}
}
assert(surfaceVariant===null||selectedCase===originalSingleEntry,'UPDATER_UI_VARIANT_CASE_CONFLICT');
const out=path.resolve(outputArgument||'work/decky-updater-ui');fs.mkdirSync(out,{recursive:true});
const rawPath=path.join(out,'UPDATER-UI-COMPONENTS.json');assert(!fs.existsSync(rawPath),'UPDATER_UI_RAW_ALREADY_EXISTS');
const omitted=[],surfaceCleanupObservations=[],surfaceReadWorkObservations=[],surfaceReadLifecycleObservations=[];
function hooks(){const state=[],deps=[],cleanup=[];let index=0,effects=[];const same=(a,b)=>a&&b&&a.length===b.length&&a.every((x,i)=>Object.is(x,b[i]));const react={createElement:(type,props,...children)=>({type,props:{...props,children:children.length===1?children[0]:children}}),Fragment:'fragment',useState:initial=>{const k=index++;if(!(k in state))state[k]=typeof initial==='function'?initial():initial;return [state[k],v=>{state[k]=typeof v==='function'?v(state[k]):v;}];},useRef:v=>{const k=index++;return state[k]??(state[k]={current:v});},useMemo:(fn,d)=>{const k=index++;if(!same(deps[k],d)){deps[k]=d;state[k]=fn();}return state[k];},useEffect:(fn,d)=>{const k=index++;if(!same(deps[k],d)){deps[k]=d;effects.push(()=>{cleanup[k]?.();cleanup[k]=fn();});}}};return {react,render(fn){index=0;effects=[];const tree=fn();effects.forEach(fn=>fn());return tree;},dispose(){cleanup.forEach(fn=>fn?.());}};}
const backing=hooks();globalThis.SP_REACT=backing.react;const result=await build({entryPoints:[process.env.YMCC_LOADER_UPDATE_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/deckyLoaderUpdate.tsx')],bundle:true,write:false,platform:'node',format:'esm',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment'}),m=await import('data:text/javascript;base64,'+Buffer.from(result.outputFiles[0].contents).toString('base64'));
const clientId='aabbccdd-1122-3344-5566-778899aabbcc',hash='a'.repeat(64),view=(extra={})=>({protocol:1,revision:1,hostVersion:'0.0.35',phase:'available',targetVersion:'0.0.36',targetHash:hash,percent:0,canCheck:true,canDownload:true,canInstall:false,gameRunning:false,message:'发现新版本',...extra});
const flatten=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(20).flatMap(flatten)],text=n=>Array.isArray(n)?n.map(text).join(''):n&&typeof n==='object'?text(n.props?.description)+text(n.props?.children):n==null||n===false?'':String(n),rows=[];
function harness(supported=true){const h=hooks();Object.assign(backing.react,h.react);const extra=new Set(),conn=new Set(),sent=[],modals=[],demand=new Set();const client={updaterSupported:()=>supported,updaterRunId:()=> 'b'.repeat(32),setPassiveDemand:(k,v)=>v?demand.add(k):demand.delete(k),subscribe(fn){conn.add(fn);fn({connected:true});return()=>conn.delete(fn);},subscribeExtra(fn){extra.add(fn);return()=>extra.delete(fn);},sendUpdater(command,data){assert(demand.has('updater'));sent.push({command,...data});return true;}};let backs=0;const ui={Field:'Field',DialogButton:'DialogButton',Focusable:'Focusable',ModalRoot:'ModalRoot',Navigation:{NavigateBack(){backs++;}},showModal:element=>modals.push(element)};const host={crypto:{randomUUID:()=>clientId},fetch(){throw Error('No direct upstream update fetch allowed');},DeckyPluginLoader:{deckyState:{publicState:()=>({versionInfo:{current:'v3.3.0'}})}}};const props={host,ui,cache:new Map(),client};const emit=value=>{for(const fn of extra)fn(value);};return {h,ui,props,sent,modals,demand,emit,backs:()=>backs,render:()=>flatten(h.render(()=>m.YmccPackageUpdateRow(props))),close(){h.dispose();},button(label){return this.render().find(n=>n.type==='DialogButton'&&text(n)===label);}};}
async function check(name,fn){if(selectedCase!==null&&name!==selectedCase){omitted.push(name);return;}try{await fn();rows.push({name,status:'passed'});}catch(e){rows.push({name,status:'failed',error:e.stack});console.error(name,e);}}
const ack=(h,id=1)=>h.emit({type:'ymcc-updater-result',clientId,id,ok:true,message:'已接收'}),tick=()=>new Promise(r=>setImmediate(r));
await check('old native host is unavailable instead of claiming version checking is a working update',()=>{const h=harness(false);h.render();const nodes=h.render();assert(text(nodes).includes('新版 YMCC'));assert(nodes.find(n=>n.type==='DialogButton').props.disabled);assert.equal(h.sent.length,0);h.close();});
await check('available package button requests download through YMCC without installing',async()=>{const h=harness();h.render();h.emit({type:'ymcc-updater',view:view()});h.button('下载更新').props.onClick();assert.equal(h.sent.filter(p=>p.action==='download').length,1);assert.equal(h.sent.at(-1).version,'0.0.36');assert.equal(h.sent.at(-1).sha256,hash);assert.equal(h.sent.filter(p=>p.action==='install').length,0);ack(h);await tick();h.close();});
await check('installation requires the real confirmation modal and its explicit confirm action',async()=>{const h=harness();h.render();const v=view({phase:'downloaded',canDownload:false,canInstall:true});h.emit({type:'ymcc-updater',view:v});h.button('安装并重启 YMCC').props.onClick();assert.equal(h.modals.length,1);assert.equal(h.sent.filter(p=>p.action==='install').length,0);const modal=h.modals[0],mh=hooks();Object.assign(backing.react,mh.react);let closed=false;const nodes=flatten(mh.render(()=>m.CompatibleUpdateConfirm({...modal.props,closeModal:()=>closed=true})));const confirm=nodes.find(n=>n.type==='DialogButton'&&text(n)==='确认安装并重启 YMCC');const pending=confirm.props.onClick();assert.equal(h.sent.filter(p=>p.action==='install').length,1);ack(h);await pending;assert(closed);mh.dispose();h.close();});
await check('native modal cancel sends no update request',()=>{const h=harness();h.render();const v=view({phase:'downloaded',canDownload:false,canInstall:true});h.emit({type:'ymcc-updater',view:v});h.button('安装并重启 YMCC').props.onClick();const modal=h.modals[0],mh=hooks();Object.assign(backing.react,mh.react);let closed=false;const nodes=flatten(mh.render(()=>m.CompatibleUpdateConfirm({...modal.props,closeModal:()=>closed=true})));nodes.find(n=>n.type==='ModalRoot').props.onCancel();assert(closed);assert.equal(h.sent.filter(p=>p.command==='updater.request').length,0);mh.dispose();h.close();});
await check('a changed package cannot be installed by an old open confirmation',async()=>{const h=harness();h.render();h.emit({type:'ymcc-updater',view:view({phase:'downloaded',canDownload:false,canInstall:true})});h.button('安装并重启 YMCC').props.onClick();const modal=h.modals[0];h.emit({type:'ymcc-updater',view:view({revision:2,phase:'downloaded',canDownload:false,canInstall:true,targetVersion:'0.0.37'})});const mh=hooks();Object.assign(backing.react,mh.react);const render=()=>flatten(mh.render(()=>m.CompatibleUpdateConfirm(modal.props)));await render().find(n=>n.type==='DialogButton'&&text(n)==='确认安装并重启 YMCC').props.onClick();assert(text(render()).includes('更新包已改变'));assert.equal(h.sent.filter(p=>p.action==='install').length,0);mh.dispose();h.close();});
await check('game-running state exposes download and an explicit installation warning',()=>{const h=harness();h.render();h.emit({type:'ymcc-updater',view:view({gameRunning:true})});assert(h.button('下载更新'));assert(!h.button('安装并重启 YMCC'));assert(text(h.render()).includes('退出游戏后安装'));h.close();});
await check('automatic check uses YMCC once per session cache rather than querying upstream repeatedly',async()=>{const h=harness();h.render();h.emit({type:'ymcc-updater',view:view({phase:'idle',targetVersion:null,targetHash:null,canDownload:false})});h.render();assert.equal(h.sent.filter(p=>p.action==='check').length,1);ack(h);await tick();h.emit({type:'ymcc-updater',view:view({revision:2,phase:'latest',targetVersion:null,targetHash:null,canDownload:false})});h.render();h.render();assert.equal(h.sent.filter(p=>p.action==='check').length,1);h.close();});
await check('row B returns through the existing Steam navigation action',()=>{const h=harness();const node=h.render().find(n=>n.type==='Focusable');assert.equal(node.props.onCancel(),true);assert.equal(h.backs(),1);h.close();});

await check('single update entry delegates only to actual YMCC package component',()=>{
 if(surfaceVariant!=='route-read-work'&&surfaceVariant!=='route-read-lifecycle'){const h=harness();const nodes=flatten(h.h.render(()=>m.LoaderUpdateRow(h.props)));assert.equal(nodes.filter(n=>n.type===m.YmccPackageUpdateRow).length,1);assert(!text(nodes).includes('Decky 自身更新'));assert.equal(m.NativeDeckyConfirm,undefined);h.close();}
 for(const variant of (surfaceVariant==='route-read-work'?['route-read-work']:surfaceVariant==='route-read-lifecycle'?['route-read-lifecycle']:surfaceVariant==='cleanup'?['module-unload','router-error','effect-unmount','initial-scan-error','observer-disconnect-error']:['module-unload','router-error','effect-unmount','initial-scan-error','observer-disconnect-error','route-read-work','route-read-lifecycle'])){
  const h=hooks(),observers=[],mounts=[],registered=new Map();let reads=0,queries=0,routeRemovals=0,failRead=variant==='initial-scan-error',passed=false,updating=false,loaderVersion='v3.3.1';
  const parent={parentElement:null,getBoundingClientRect:()=>({width:300,height:40})};
  const field={style:{display:''},parentElement:parent,closest:()=>null,getBoundingClientRect:()=>({width:300,height:40}),after(node){node.isConnected=true;}};
  const originalFocus=()=> 'auto',node={Element:field,Parent:{},SetProperties(){},GetFocusable:originalFocus};
  field.__reactFiber_fixture={memoizedProps:{node},return:{tag:10,memoizedProps:{value:node.Parent},type:'OwnedProvider'}};
  const button={closest:selector=>selector==='.native-field'?field:null,__reactFiber_fixture:{type:{name:'UpdaterSettings'}}};
  class Observer{
   constructor(callback){this.callback=callback;this.active=false;this.targets=[];this.disconnectCalls=0;observers.push(this);}
   observe(target){this.active=true;this.targets.push(target);}
   disconnect(){this.active=false;this.disconnectCalls++;if(variant==='observer-disconnect-error'&&this.targets.includes(doc.body))throw Error('inert observer disconnect failure after release');}
  }
  const doc={body:{},defaultView:{MutationObserver:Observer},querySelectorAll:()=>{queries++;return [button];},createElement(){const mount={isConnected:false,setAttribute(){},remove(){this.isConnected=false;}};mounts.push(mount);return mount;}};
  const originalCreate=h.react.createElement;
  h.react.createElement=(type,props,...children)=>{if(type==='span'&&props?.ref)props.ref.current={ownerDocument:doc};return originalCreate(type,props,...children);};
  Object.assign(backing.react,h.react);
  const host={location:{pathname:'/routes/decky/settings'},DeckyPluginLoader:{deckyState:{publicState(){reads++;if(failRead)throw Error('inert initial scan failure');return{isLoaderUpdating:updating,versionInfo:{current:loaderVersion}};}}}};
  const routerHook={addGlobalComponent(name,Component){registered.set(name,Component);},removeGlobalComponent(name){routeRemovals++;if(variant==='router-error')throw Error('inert router removal failure');registered.delete(name);}};
  const stop=m.installLoaderUpdateRow({host,ui:{gamepadDialogClasses:{Field:'native-field'},Field:'Field'},reactDom:{createPortal(){}},routerHook,client:{}});
  try{
   const render=()=>h.render(()=>registered.get('YMCC.DeckyLoaderUpdate')());
   if(variant==='route-read-lifecycle'){
    const original=host.DeckyPluginLoader.deckyState.publicState;
    const navigateDuringRead=()=>{const value=original();host.location.pathname='/library/home';return value;};
    host.DeckyPluginLoader.deckyState.publicState=navigateDuringRead;render();
    assert.equal(field.style.display,'','late route exit must preserve original native updater Field');assert.equal(node.GetFocusable,originalFocus);assert.equal(mounts.filter(x=>x.isConnected).length,0,'late route exit must not mount the replacement');assert.equal(reads,1);
    const globalObserver=observers.find(observer=>observer.targets.includes(doc.body));assert(globalObserver?.active);
    host.DeckyPluginLoader.deckyState.publicState=original;host.location.pathname='/routes/decky/settings';globalObserver.callback();assert.equal(mounts.filter(x=>x.isConnected).length,1);
    host.DeckyPluginLoader.deckyState.publicState=navigateDuringRead;globalObserver.callback();assert.equal(mounts.filter(x=>x.isConnected).length,0);assert.equal(field.style.display,'');assert.equal(node.GetFocusable,originalFocus);
    host.DeckyPluginLoader.deckyState.publicState=original;host.location.pathname='/routes/decky/settings';globalObserver.callback();assert.equal(mounts.filter(x=>x.isConnected).length,1);
    host.DeckyPluginLoader.deckyState.publicState=()=>{const value=original();value.versionInfo={get current(){host.location.pathname='/library/home';return 'v3.3.1';}};return value;};
    globalObserver.callback();assert.equal(mounts.filter(x=>x.isConnected).length,0);assert.equal(field.style.display,'');assert.equal(node.GetFocusable,originalFocus);
    const ended=reads;for(let n=0;n<1000;n++)globalObserver.callback();assert.equal(reads,ended,'late route exit returns to zero off-route reads');stop();
    surfaceReadLifecycleObservations.push({initialReadExitRejected:true,liveFieldRestoredAfterReadExit:true,versionGetterExitRejected:true,offRouteCallbacks:1000,offRouteReads:0,activeObservers:observers.filter(x=>x.active).length,liveMounts:mounts.filter(x=>x.isConnected).length,fieldDisplay:field.style.display,focusRestored:node.GetFocusable===originalFocus});
   }else
   if(variant==='route-read-work'){
    host.location.pathname='/library/home';
    render();assert.equal(reads,0,'outside settings must not read official state');assert.equal(queries,0);assert.equal(field.style.display,'');assert.equal(node.GetFocusable,originalFocus);assert.equal(mounts.length,0);
    const globalObserver=observers.find(observer=>observer.targets.includes(doc.body));assert(globalObserver?.active);
    for(let n=0;n<1000;n++)globalObserver.callback();
    assert.equal(reads,0);assert.equal(queries,0);assert.equal(mounts.length,0);const outsideHealthy={callbacks:1000,reads,queries,mounts:mounts.length};
    failRead=true;for(let n=0;n<1000;n++)assert.doesNotThrow(()=>globalObserver.callback());failRead=false;
    assert.equal(reads,0,'a broken official getter is irrelevant outside its settings surface');const outsideBroken={callbacks:1000,reads,queries};
    host.location.pathname='/routes/decky/settings';globalObserver.callback();
    assert.equal(reads,1,'one settings scan must consume one coherent official snapshot');assert.equal(queries,1);assert.equal(field.style.display,'none');assert.equal(node.GetFocusable(),'none');assert.equal(mounts.filter(x=>x.isConnected).length,1);
    const before=reads;for(let n=0;n<1000;n++)globalObserver.callback();
    assert.equal(reads-before,1000,'settings mutations use one official snapshot each');assert.equal(mounts.length,1);const inside={callbacks:1000,reads:reads-before,queries:queries-1,mounts:mounts.length};
    updating=true;globalObserver.callback();assert.equal(mounts.filter(x=>x.isConnected).length,0);assert.equal(field.style.display,'');assert.equal(node.GetFocusable,originalFocus);
    updating=false;globalObserver.callback();assert.equal(mounts.filter(x=>x.isConnected).length,1);
    loaderVersion='v4.0.0';globalObserver.callback();assert.equal(mounts.filter(x=>x.isConnected).length,0);assert.equal(field.style.display,'');assert.equal(node.GetFocusable,originalFocus);
    loaderVersion='v3.3.1';globalObserver.callback();assert.equal(mounts.filter(x=>x.isConnected).length,1);
    const enteredReads=reads;host.location.pathname='/library/home';globalObserver.callback();assert.equal(reads,enteredReads);assert.equal(mounts.filter(x=>x.isConnected).length,0);assert.equal(field.style.display,'');assert.equal(node.GetFocusable,originalFocus);
    const exitQueries=queries;for(let n=0;n<1000;n++)globalObserver.callback();assert.equal(reads,enteredReads);assert.equal(queries,exitQueries);
    stop();const retiredReads=reads;for(const observer of observers)observer.callback();assert.equal(reads,retiredReads);assert.equal(observers.filter(x=>x.active).length,0);
    surfaceReadWorkObservations.push({outsideHealthy,outsideBroken,inside,routeExitReads:0,routeExitStormCallbacks:1000,retiredCallbacksQuiet:true,restoredDisplay:field.style.display,restoredFocus:node.GetFocusable===originalFocus,networkRequests:0,newTimers:0});
   }else
   if(variant==='initial-scan-error'){
    assert.throws(render,/inert initial scan failure/);assert.equal(observers.filter(x=>x.active).length,0,'Failed first scan must release both owned observers');failRead=false;
    const before=reads;for(const observer of observers)observer.callback();assert.equal(reads,before,'Failed initial surface cannot read again');stop();
   }else{
    render();assert.equal(observers.filter(x=>x.active).length,2);assert.equal(field.style.display,'none');assert.equal(node.GetFocusable(),'none');assert.equal(mounts.filter(x=>x.isConnected).length,1);
    if(variant==='effect-unmount')h.dispose();
    else if(variant==='router-error')assert.throws(stop,/inert router removal failure/);
    else if(variant==='observer-disconnect-error')assert.throws(stop,/inert observer disconnect failure after release/);
    else stop();
    assert.equal(observers.filter(x=>x.active).length,0,'Every surface observer must detach before teardown returns');
    assert.equal(field.style.display,'');assert.equal(node.GetFocusable,originalFocus);assert.equal(mounts.filter(x=>x.isConnected).length,0);
    const beforeReads=reads,beforeMounts=mounts.length;
    for(let n=0;n<100;n++)for(const observer of observers)observer.callback();
    assert.equal(reads,beforeReads,'Retired surface callbacks must do no source reads');assert.equal(mounts.length,beforeMounts,'Retired surface callbacks must not rebuild an update row');
    if(variant==='effect-unmount')stop();
   }
   const beforeRemovalCount=routeRemovals;stop();assert.equal(routeRemovals,beforeRemovalCount);passed=true;
  }finally{
   surfaceCleanupObservations.push({variant,passed,activeObservers:observers.filter(x=>x.active).length,liveMounts:mounts.filter(x=>x.isConnected).length,focusRestored:node.GetFocusable===originalFocus,fieldDisplay:field.style.display,routeRemovals,registeredComponents:registered.size,sourceReads:reads,plannedLateCallbackAttempts:variant==='initial-scan-error'||variant==='route-read-work'||variant==='route-read-lifecycle'?observers.length:observers.length*100,observerFailureInjectedAfterRelease:variant==='observer-disconnect-error'});
   try{h.dispose();}catch{}try{stop();}catch{}
  }
 }
});
await check('real update Field explains that Decky follows YMCC',()=>{const h=harness();h.render();h.emit({type:'ymcc-updater',view:view()});const nodes=h.render();assert.equal(nodes.filter(n=>n.type==='Field').length,1);assert(nodes.some(n=>n.type==='Field'&&n.props.label==='YMCC 更新'));assert(text(nodes).includes('Decky 随 YMCC 一起更新'));assert(!text(nodes).includes('自身更新'));h.close();});
const report={surfaceVariant,surfaceReadWorkObservations,surfaceReadLifecycleObservations,status:rows.every(x=>x.status==='passed')?'passed':'failed',cases:rows.length,results:rows,selectedCase,omitted,surfaceCleanupObservations,actualProductionTsx:true,nativeWidgetsRendered:false,syntheticScreenshots:false,currentSteamActions:0};fs.writeFileSync(rawPath,JSON.stringify(report,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({status:report.status,cases:rows.length,failed:rows.filter(r=>r.status==='failed').map(r=>r.name)}));if(report.status!=='passed')process.exitCode=1;
