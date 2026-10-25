// Contract exercise of the exact production React probe, using inert hooks/DOM/Steam APIs.
// This is NOT actual Chromium rendering, physical input, game pixels or a native window.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),{build}=require('esbuild');
const dir=path.resolve(process.argv[2]||'');if(!dir.startsWith('G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Steam-HUD-Probe\\native-ready-'))throw Error('Owned task directory required');
const b=await build({entryPoints:[path.join(root,'decky-plugin/src/steamHudProbe.tsx')],bundle:true,platform:'node',format:'cjs',target:'es2020',write:false});const file=path.join(dir,'native-probe-react.cjs');fs.writeFileSync(file,b.outputFiles[0].contents);const {installSteamHudProbe}=require(file);
const flags={NotFocusable:512,TransparentParentWindow:4194304,ForceBrowserVisible:16777216,NoTaskbarIcon:8,NoWindowShadow:16384,NoRoundedCorners:262144,Hidden:2,ApplyBrowserScaleToDimensions:4096},kinds={Notification:1,Overlay:0,Tooltip:2,StandaloneContextMenu:4};
const binding={appId:4131894616,gameId:'17746352246272032768',browserGameId:'4131894616',overlayTitle:'SP Overlay: 28868/2228232/0',overlayPid:28868,overlayBrowserId:2228232,ownerBrowserId:9001,overlayBrowserType:0,overlayNativeAppId:4699480};
const cases=[];function test(name,fn){fn();cases.push(name);console.log('PASS '+name);}
function target(){const listeners=new Map();return {listeners,addEventListener(k,f){if(!listeners.has(k))listeners.set(k,new Set());listeners.get(k).add(f);},removeEventListener(k,f){listeners.get(k)?.delete(f);if(listeners.get(k)?.size===0)listeners.delete(k);}};}
function fixture(){
 const timers=new Map(),childTimers=new Map(),calls=[],created=[],globalComponents=new Map(),cells=new Map(),pending=[];let current=null,index=0,context=null,nextTimer=0,uid=0;
 const environment=(mode,title)=>{const requests=new Map([[0,0],[1,0],[2,0],[3,0],[4,0]]),o={...target(),closed:false,document:{...target(),title},SteamClient:{Browser:{GetBrowserID:()=>binding.ownerBrowserId}}};
  const instance={MainRunningAppID:mode===4?binding.appId:null,MainRunningApp:mode===4?{gameid:binding.gameId}:null,BrowserWindow:o,IsGamepadUIOverlayWindow:()=>mode===4,
    params:{browserInfo:{m_unPID:binding.overlayPid,m_nBrowserID:binding.overlayBrowserId,m_eBrowserType:binding.overlayBrowserType,m_unAppID:binding.overlayNativeAppId,m_gameID:binding.browserGameId}},LocationPathName:'/apprunning'};
  const store={m_Instance:instance,m_mapCompositionStateRequests:requests,GetName:()=>title,GetCompositionState:()=>requests.get(1)>0?1:0,
   AddMinimumCompositionStateRequest(e){requests.set(e,requests.get(e)+1);},ChangeMinimumCompositionStateRequest(){},RemoveMinimumCompositionStateRequest(e){requests.set(e,requests.get(e)-1);}};
  return {id:title,mode:{mode},CompositionStateStore:store,owner:o};};
 const envs=[environment(1,'Steam 大屏幕模式'),environment(4,binding.overlayTitle)];
 const equal=(a,b)=>a?.length===b?.length&&a.every((v,i)=>v===b[i]);
 function cell(make){const key=current.key+'#'+index++;if(!cells.has(key))cells.set(key,make());return cells.get(key);}
 const React={
  createElement(type,props,...children){if(typeof type==='function')return invoke(type,{...props,children});return {type,props:props??{},children};},
  useContext:()=>context.mode,
  useState(initial){const c=cell(()=>({value:typeof initial==='function'?initial():initial}));return [c.value,v=>{c.value=typeof v==='function'?v(c.value):v;}];},
  useRef(value){return cell(()=>({current:value}));},
  useMemo(fn,deps){const c=cell(()=>({value:fn(),deps}));if(!equal(c.deps,deps)){c.value=fn();c.deps=deps;}return c.value;},
  useEffect(fn,deps){const c=cell(()=>({deps:undefined,cleanup:undefined,owner:context.id}));if(!equal(c.deps,deps)){c.deps=deps;pending.push(()=>{c.cleanup?.();c.cleanup=fn();});}}
 };
 function invoke(fn,props){const old=current,idx=index;current={key:context.id+'::'+fn.name};index=0;try{return fn(props);}finally{current=old;index=idx;}}
 function flush(){for(let safety=0;pending.length;safety++){if(safety>500)throw Error('inert effect loop');pending.shift()();}}
 function render(){for(const env of envs){context=env;for(const Root of globalComponents.values())invoke(Root,{});}context=null;flush();}
 function remove(){for(const c of cells.values())if(c.cleanup){const fn=c.cleanup;c.cleanup=undefined;fn();}cells.clear();globalComponents.clear();}
 function getter(){return context.CompositionStateStore;}
 function modeGetter(){return React.useContext(modeContext);}
 const modeContext={};
 function compositionHook(e,owner){const s=context.CompositionStateStore;s.ChangeMinimumCompositionStateRequest;React.useEffect(()=>{s.AddMinimumCompositionStateRequest(e,owner);return()=>s.RemoveMinimumCompositionStateRequest(e,owner);},[s,e,owner]);}
 function flagFactory(e){return e===kinds.Notification?flags.NotFocusable|flags.TransparentParentWindow|flags.ForceBrowserVisible|flags.NoTaskbarIcon|flags.NoWindowShadow|flags.NoRoundedCorners:0;}
 function nativeHook(name,params){
  const pair=React.useMemo(()=>{
    const child={...target(),closed:false,setTimeout(fn,ms){childTimers.set(++nextTimer,{fn,ms});return nextTimer;},clearTimeout(id){childTimers.delete(id);}};
    const st=()=>({setProperty(){}}),element={style:st()},doc={...target(),title:name,hidden:true,body:{style:st()},documentElement:{style:st()},querySelector:()=>element.hud??null};child.document=doc;element.ownerDocument={defaultView:child};
    child.SteamClient={Window:{SetKeyFocus(v){calls.push(['focus',name,v]);},SetHideOnClose(v){calls.push(['hide-on-close',name,v]);},ShowWindow(){assert.ok(element.hud,'Portal must commit before child ShowWindow');calls.push(['show',name]);doc.hidden=false;},Close(){calls.push(['close',name]);child.closed=true;}},Overlay:{SetWindowAlpha(v){calls.push(['alpha',name,v]);},SetWindowPinned(v){calls.push(['pin',name,v]);}}};
    child.getComputedStyle=()=>element.hud?.paint??{};created.push({name,params,child,element});return {popup:child,element,popupObj:{Close:()=>child.SteamClient.Window.Close()}};
  },[]);
  const behavior={bNoInitialShow:params.bNoInitialShow,bNoFocusOnShow:params.bNoFocusOnShow};
  React.useEffect(()=>{pair.popup.SteamClient.Window.SetHideOnClose(false);return()=>pair.popupObj.Close();},[]);
  return behavior?pair:null;
 }
 const paintText=node=>typeof node==='string'||typeof node==='number'?String(node):Array.isArray(node)?node.map(paintText).join('\n'):node?.children?.map(paintText).join('\n')??'';
 const reactDom={createPortal(node,element){element.hud={innerText:paintText(node),paint:{backgroundColor:node.props.style.background,color:node.props.style.color,opacity:String(node.props.style.opacity),pointerEvents:node.props.style.pointerEvents},getBoundingClientRect:()=>({toJSON:()=>({x:0,y:0,width:200,height:186})}),node};return {portal:true};}};
 const host={...target(),crypto:{randomUUID:()=>String(++uid)},setTimeout(fn,ms){timers.set(++nextTimer,{fn,ms});return nextTimer;},clearTimeout(id){timers.delete(id);},SteamClient:{Overlay:{SetOverlayState(){throw Error('must not call shared native state directly');}}}};
 const ui={modules:new Map([[1,{compositionHook}],[2,{getter,modeGetter,enum:{Full:1,Overlay:4,Headless:0}}],[3,{nativeHook}],[4,{flagFactory,flags,kinds}]]),EUIComposition:{Notification:1}};
 const routerHook={addGlobalComponent(name,Root){globalComponents.set(name,Root);},removeGlobalComponent(){remove();}};
 const dispose=installSteamHudProbe({host,react:React,reactDom,ui,routerHook});
 return {host,api:host.__YMCC_STEAM_HUD_PROBE__,envs,created,calls,timers,childTimers,render,dispose,globalComponents};
}
function config(){return {kind:'integration-test',runId:'hud-probe-native-react',expiresAt:Date.now()+30000,scope:'overlay-only',carrier:'native-popup',popupPin:'creation',gameBinding:{...binding}};}
test('native capabilities resolve inside exact production probe with renamed fake exports',()=>{const f=fixture();assert.equal(f.api.diagnostics().capabilities.nativePopup.available,true);f.dispose();});
test('native start creates one GAME child, never creates a main-window child or changes original binding',()=>{const f=fixture();assert.equal(f.api.start(config()),true);f.render();assert.equal(f.created.length,1);assert.equal('owner_window' in f.created[0].params,false);assert.equal(f.created[0].params.target_browser.m_nBrowserID,binding.overlayBrowserId);assert.equal(f.envs[0].CompositionStateStore.m_mapCompositionStateRequests.get(1),0);assert.equal(f.envs[1].CompositionStateStore.m_mapCompositionStateRequests.get(1),1);assert.equal(f.api.diagnostics().nativeChildren.length,1);assert.equal(f.created[0].element.hud.innerText.includes('实时数据未连接'),true);f.dispose();});
test('actual production TSX uses corner panel in OWNED child with no double root inset',()=>{const f=fixture();f.api.start(config());f.render();const n=f.created[0].element.hud.node;assert.equal(n.props['data-hud-layout'],'corner');assert.equal(n.props.style.top,0);assert.equal(n.props.style.position,'relative');f.dispose();});
test('fixture updates + four alpha changes rerender SAME native child, source is labelled',()=>{const f=fixture(),cfg=config();f.api.start(cfg);f.render();assert.equal(f.api.update({runId:cfg.runId,seq:1,collectedAt:Date.now(),source:'fixture',fps:60,cpuPowerW:18,cpuTempC:67,gpuUsage:86}),true);for(const a of [0,0.35,0.65,0.9]){assert.equal(f.api.setAppearance(cfg.runId,{backgroundAlpha:a,textAlpha:1,layout:'corner'}),true);f.render();const h=f.created[0].element.hud;assert.ok(h.innerText.includes('示例数据'));assert.equal(h.paint.backgroundColor,'rgba(16,22,31,'+a+')');assert.equal(h.paint.color,'rgba(241,245,250,1)');assert.equal(h.paint.opacity,'1');}assert.equal(f.created.length,1);assert.equal(f.calls.filter(c=>c[0]==='show').length,1);assert.equal(f.api.diagnostics().view.data.seq,1);f.dispose();});
test('native child probe rejects unimplemented uniform provenance and unsupported bar layout',()=>{const f=fixture(),cfg=config();f.api.start(cfg);f.render();assert.equal(f.api.update({runId:cfg.runId,seq:1,collectedAt:Date.now(),source:'ymcc-uniform',fps:60,cpuPowerW:18,cpuTempC:67,gpuUsage:86}),false);assert.equal(f.api.setAppearance(cfg.runId,{backgroundAlpha:0.65,textAlpha:1,layout:'bar'}),false);f.dispose();});
test('explicit stop cleans original native hook + independent child timer/listeners + composition',()=>{const f=fixture(),cfg=config();f.api.start(cfg);f.render();assert.equal(f.api.stop('hud-probe-unowned'),false);assert.equal(f.api.stop(cfg.runId),true);assert.equal(f.created[0].child.closed,true);assert.equal(f.created[0].child.listeners.size,0);assert.equal(f.envs[1].owner.listeners.size,0);assert.equal(f.childTimers.size,0);assert.equal(f.timers.size,0);assert.equal(f.api.diagnostics().nativeChildren.length,0);assert.equal(f.api.diagnostics().counts.listeners,0);assert.equal(f.envs[1].CompositionStateStore.m_mapCompositionStateRequests.get(1),0);assert.equal(f.calls.filter(c=>c[0]==='close').length,1);f.dispose();});
test('changed game denies initial carrier and update fails closed without touching main popup',()=>{const f=fixture(),cfg=config();f.envs[1].CompositionStateStore.m_Instance.MainRunningAppID=1;f.api.start(cfg);f.render();assert.equal(f.created.length,0);assert.equal(f.api.update({runId:cfg.runId,seq:1,collectedAt:Date.now(),source:'fixture',fps:60,cpuPowerW:18,cpuTempC:67,gpuUsage:86}),false);assert.equal(f.timers.size,0);f.dispose();});
test('legacy root path still has no native child, independent alpha and one composition lease',()=>{const f=fixture(),c=config();delete c.carrier;delete c.popupPin;delete c.gameBinding;f.api.start(c);f.render();assert.equal(f.created.length,0);assert.equal(f.envs[1].CompositionStateStore.m_mapCompositionStateRequests.get(1),1);f.dispose();assert.equal(f.timers.size,0);});
const report={pass:true,tests:cases.length,cases,evidence:'exact production component with inert React hooks + fake native child, not browser/native/game pixels',actualSteam:false,desktopInput:false,desktopCapture:false,fullscreenVerified:false};fs.writeFileSync(path.join(dir,'NATIVE-PROBE-REACT-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({pass:true,tests:cases.length,report:path.join(dir,'NATIVE-PROBE-REACT-SELFTEST.json')}));
