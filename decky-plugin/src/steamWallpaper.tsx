// A read-only consumer attached through the pinned public Decky router API.
import {WallpaperPlayers} from './wallpaperPlayers';
import {installWallpaperRouteFallback} from './wallpaperRouteFallback';
import {WallpaperForeground} from './wallpaperForeground';
import type {MirrorClient} from './mirrorClient';
import {wallpaperEntranceMs,type WallpaperEntrance} from './wallpaperEntrance';
export interface WallpaperSelection {appId:string;name:string;}
export function selectionFromElement(element:any):WallpaperSelection|null {
  // Inspect the focused card only. No global DOM scan, title matching or timer.
  for(let node=element,depth=0;node&&depth<12;node=node.parentElement,depth++){
    const explicit=node.getAttribute?.('data-appid');if(explicit&&/^\d{1,20}$/.test(explicit))return {appId:explicit,name:''};
    const fiberKey=Object.keys(node).find(key=>key.startsWith('__reactFiber$'));
    for(let fiber=fiberKey?node[fiberKey]:null,n=0;fiber&&n<20;fiber=fiber.return,n++){
      const props=fiber.memoizedProps;if(!props||typeof props!=='object')continue;
      const app=props.overview||props.appOverview||props.app;
      const raw=app?.appid??props.appid??props.appId;
      if((typeof raw==='number'&&Number.isSafeInteger(raw)||typeof raw==='string')&&/^\d{1,20}$/.test(String(raw)))return {appId:String(raw),name:typeof app?.display_name==='string'?app.display_name:''};
    }
  }return null;
}
export function wallpaperClasses(modules:any):{home?:string;homeImages?:string;homeRoot?:string;detail?:string;detailImages?:string}{
  // Decky exposes its loaded Webpack exports as a Map; old adapters used a record.
  const exports=modules&&typeof modules.values==='function'?[...modules.values()]:Object.values(modules||{});
  const values:any[]=[];const seen=new Set<any>();for(const item of exports){try{for(const candidate of [item,item?.default])if(candidate&&typeof candidate==='object'&&!seen.has(candidate)){seen.add(candidate);values.push(candidate);}}catch{}}
  const home=values.filter(value=>typeof value.RecentGamesBackgroundContainer==='string'&&typeof value.RecentGamesBackgroundImages==='string');
  const detail=values.filter(value=>typeof value.HeaderBackgroundImage==='string'&&typeof value.ImgSrc==='string'&&typeof value.TopCapsule==='string');
  const safe=(value:any)=>typeof value==='string'&&/^[A-Za-z0-9_-]{1,100}$/.test(value)?value:undefined;
  return {home:home.length===1?safe(home[0].RecentGamesBackgroundContainer):undefined,homeImages:home.length===1?safe(home[0].RecentGamesBackgroundImages):undefined,homeRoot:home.length===1?safe(home[0].RecentGamesContainer):undefined,detail:detail.length===1?safe(detail[0].HeaderBackgroundImage):undefined,detailImages:detail.length===1?safe(detail[0].ImgSrc):undefined};
}
export function installSteamWallpaper({host,react:React,reactDom,ui,routerHook,client,entrance='fade'}:{host:any;react:any;reactDom?:any;ui?:any;routerHook:any;client:MirrorClient;entrance?:WallpaperEntrance}):()=>void {
  // Event-only diagnostics: bounded history, no source URLs/tokens and no timer.
  const traceNow=()=>globalThis.performance?.now?.()??Date.now(),traceStarted=traceNow();
  let traceCount=0;const trace=(stage:string,detail:Record<string,unknown>={})=>{
    if(traceCount>=96)return;
    const ordinal=++traceCount;
    // Reserve the final record for explicit truncation. Silence after the
    // bounded trace is not evidence that playback or background work stopped.
    const record=ordinal===96?{stage:'trace-limit',ordinal,limit:96,suppressedStage:stage}:{stage,ordinal,...detail};
    try{host.console?.info?.('[YMCC:Wallpaper] '+JSON.stringify({...record,sinceInstallMs:Math.round(traceNow()-traceStarted)}));}catch{}
  };
  const capable=typeof routerHook?.addPatch==='function'&&typeof routerHook?.removePatch==='function'&&typeof reactDom?.createPortal==='function';
  trace('install',{router:typeof routerHook?.addPatch==='function',portal:typeof reactDom?.createPortal==='function'});
  if(!capable)return ()=>{};
  let disposed=false,transportAvailable=true,enabled=false,playbackKnown=false,needReady=true,wallpaperDemand=false,sequence=0,current:WallpaperSelection|null=null,currentSeq=0,currentVisible=false,currentOwner:any=null,frame:any=null,contextPending=false,resumeGeneration=0,stateRevision=0,stateOwner:string|null=null;
  const retiredStateOwners=new Set<string>();
  const validEntry=(entry:any)=>entry&&typeof entry.assetId==='string'&&typeof entry.revision==='string'&&['video','image'].includes(entry.kind)&&typeof entry.url==='string'&&/^http:\/\/127\.0\.0\.1:\d+\/asset\/[a-z0-9-]+\.(mp4|webm|jpg|png|webp|bmp|ico)\?token=[a-f0-9]+$/.test(entry.url);
  const classes=wallpaperClasses(ui?.modules);const roots=new Set<any>();
  trace('classes',{home:!!classes.home,homeImages:!!classes.homeImages,detail:!!classes.detail,detailImages:!!classes.detailImages});
  const resets=new Set<()=>void>();const resetPlayers=()=>{for(const reset of resets)reset();};
  const listeners=new Set<()=>void>();const refresh=()=>{for(const listener of listeners)listener();};
  // Wallpaper frames update only their own surface. The Steam page rerenders
  // naturally from Steam props/state; this separate signal is teardown-only.
  const routeListeners=new Set<()=>void>();
  const owners=new Map<any,{visible:()=>boolean;clear:()=>void}>();
  // Space identical, unacknowledged ready retries on existing events only.
  // First selection, changed intent, forced recovery and failed sends stay immediate.
  let readyContextSentAt:number|null=null;
  const report=(selection:WallpaperSelection|null,owner:any,visible:boolean,force=false)=>{
    if(disposed||!selection)return;
    // A forced transport/ready report may only revalidate its own visible route.
    // Unknown geometry is allowed; owners reject only a known hidden route.
    if(visible&&!owners.get(owner)?.visible()){if(currentOwner!==owner)return;visible=false;}
    const same=current?.appId===selection.appId&&currentOwner===owner&&currentVisible===visible;
    if(same&&!force&&!contextPending&&!needReady)return;
    const now=needReady?traceNow():0;
    const sameReadyInFlight=needReady&&readyContextSentAt!==null&&now>=readyContextSentAt&&now-readyContextSentAt<1000;
    if(same&&!force&&!contextPending&&sameReadyInFlight)return;
    // Request sequencing fences incoming replies, not an already verified display.
    // Keep the same selected asset through a recheck, without inventing a new ack.
    const keep=same&&visible&&enabled&&playbackKnown&&transportAvailable&&!!frame?.entry;
    if(!keep){frame=null;if(currentOwner)owners.get(currentOwner)?.clear();}
    current=selection;currentVisible=visible;currentOwner=owner;currentSeq=++sequence;
    contextPending=!client.sendWallpaper('wallpaper.context',{contextSeq:currentSeq,appId:selection.appId,name:selection.name,visible,requestReady:needReady});
    readyContextSentAt=needReady&&!contextPending?now:null;
    trace('context',{appId:selection.appId,seq:currentSeq,visible,sent:!contextPending});refresh();
  };
  const foregrounds=new Map<Document,WallpaperForeground>();
  const foregroundFor=(doc:Document)=>{let state=foregrounds.get(doc);if(!state){state=new WallpaperForeground(doc);foregrounds.set(doc,state);}return state;};
  if(host.document?.defaultView)foregroundFor(host.document);
  function Surface(props:any){
    const players=React.useRef(null);const output=React.useRef(null);const root=React.useRef(null);const owner=React.useRef({}).current;const [,update]=React.useState(0);const [visible,setVisible]=React.useState(false);const admitted=React.useRef(false);const [surface,setSurface]=React.useState(null);const inView=React.useRef(false);const [viewport,setViewport]=React.useState(null);
    React.useEffect(()=>{
      const node=root.current as HTMLElement|null;if(!node||disposed)return;const doc=node.ownerDocument,view=doc.defaultView;if(!view)return;
      const route:Element|null=props.routeRoot??node.closest('[data-ymcc-wallpaper-route]');
      const belongs=(target:any)=>{const marked=target?.closest?.('[data-ymcc-wallpaper-route]');return !!route&&!!target&&route.contains(target)&&(!marked||marked===route);};
      const routeVisible=(target=route)=>{
        if(!target?.isConnected)return false;
        if(props.routeRoot&&target===route&&target.closest('[data-ymcc-wallpaper-route]'))return false;
        // Explicit CSS hiding is a veto, never require dimensions or focus on a
        // particular Steam element: late backgrounds/controller focus still work.
        try{const visibility=view.getComputedStyle(target).visibility;if(visibility==='hidden'||visibility==='collapse')return false;
          for(let element:Element|null=target;element;element=element.parentElement)if((element as HTMLElement).hidden||view.getComputedStyle(element).display==='none')return false;
        }catch{}return true;
      };
      const render=()=>update((n:number)=>n+1),reset=()=>players.current?.stop();listeners.add(render);resets.add(reset);
      owners.set(owner,{visible:()=>admitted.current&&routeVisible(),clear:()=>players.current?.clearCurrent()});
      const selected=()=>{
        if(!routeVisible())return null;
        const focused=doc.activeElement,other=focused?.closest('[data-ymcc-wallpaper-route]');
        if(other&&other!==route&&routeVisible(other))return null;
        const fromRoute=props.appId?{appId:String(props.appId),name:''}:null;
        return fromRoute||selectionFromElement(belongs(focused)?focused:null)||selectionFromElement(route?.querySelector('.gpfocus'))||
          // Steam's background holder exposes its current appid separately from
          // previousAppid/preloadAppid. Use that confirmed native selection when
          // either Home surface arrives before a card has keyboard focus.
          (props.page==='/library/home'&&classes.homeImages?selectionFromElement(route?.querySelector('.'+classes.homeImages)):null);
      };
      const foreground=foregroundFor(doc);
      const updateVisibility=(force=false)=>{const show=foreground.allowed()&&routeVisible();const changed=admitted.current!==show;admitted.current=show;if(!show)players.current?.stop();setVisible(show);if(changed)trace(show?'foreground':'background',{page:props.page});if(!show&&currentOwner===owner&&current)report(current,owner,false,force);if(show)roots.add(owner);else roots.delete(owner);bootstrap();if(show&&currentOwner===owner&&current)report(current,owner,true,force);};
      const offVisibility=foreground.subscribe(updateVisibility);
      const focus=(event:any)=>{if(!belongs(event.target)||!routeVisible())return;foreground.revalidateFocus(event);updateVisibility();const selection=props.appId?{appId:String(props.appId),name:''}:selectionFromElement(event.target);if(selection){report(selection,owner,admitted.current&&foreground.allowed());}};
      doc.addEventListener('focusin',focus,true);
      // HUD/QAM may already own the socket. Acquire wallpaper demand first;
      // no new connection event will replay an initial request on that socket.
      updateVisibility();const initial=selected();
      // A mounted Steam route may still have no foreground admission or card.
      // Record the actual one-time gate values; do not poll or relax playback.
      trace('route-mounted',{page:props.page,documentVisibility:doc.visibilityState,documentFocused:doc.hasFocus(),foregroundAllowed:foreground.allowed(),routeVisible:routeVisible(),selectionFound:!!initial,connected:transportAvailable,playbackKnown,enabled});
      if(initial)report(initial,owner,admitted.current,true);
      let nativeObserver:MutationObserver|null=null;
      if(props.page==='/library/home'&&classes.homeImages){
        let watched:Element|null=null,watchedBackground:Element|null=null,nativeBound=false;
        const watchNativeSelection=()=>{
          const holder=route?.querySelector('.'+classes.homeImages)??null;
          const background=route?.querySelector('.'+classes.home!)??null;
          if(!nativeBound||holder!==watched||background!==watchedBackground){
            nativeBound=true;nativeObserver?.disconnect();watched=holder;watchedBackground=background;
            if(holder)nativeObserver?.observe(holder,{childList:true,subtree:true,attributes:true,attributeFilter:['src','class']});
            if(background&&background!==holder)nativeObserver?.observe(background,{childList:true,subtree:!holder});
            if(route&&route!==holder&&route!==background)nativeObserver?.observe(route,{childList:true,subtree:!holder&&!background});
          }
        };
        nativeObserver=new view.MutationObserver(()=>{if(disposed)return;watchNativeSelection();const selection=selected();if(selection)report(selection,owner,admitted.current&&foreground.allowed());});
        watchNativeSelection();
      }
      return()=>{nativeObserver?.disconnect();admitted.current=false;players.current?.stop();if(currentOwner===owner&&!disposed&&current)client.sendWallpaper('wallpaper.context',{contextSeq:++sequence,appId:current.appId,name:current.name,visible:false});roots.delete(owner);bootstrap();listeners.delete(render);resets.delete(reset);owners.delete(owner);offVisibility();doc.removeEventListener('focusin',focus,true);if(currentOwner===owner){currentOwner=null;current=null;currentVisible=false;frame=null;refresh();}};
    },[props.appId,props.routeRoot]);
    React.useEffect(()=>{
      const marker=root.current as HTMLElement|null,route:Element|null=props.routeRoot??marker?.closest('[data-ymcc-wallpaper-route]')??null;
      const View=marker?.ownerDocument.defaultView;
      if(!marker||!route||!View||!visible||!enabled){setSurface(null);return;}
      const name=props.page==='/library/home'?classes.home:classes.detail;if(!name){trace('background-class-missing',{page:props.page});return;}
      let target:Element|null=null;
      const observer=new View.MutationObserver(()=>{if(!target||!route.contains(target))find();});
      const find=()=>{
        if(target&&route.contains(target))return true;
        const next=route.querySelector('.'+name);target=next;setSurface(next);trace(next?'background-found':'background-waiting',{page:props.page});
        observer.disconnect();
        if(target){
          // Only direct children along the current background's ancestor chain.
          // Game-card text/animation changes inside other branches are ignored.
          for(let parent=target.parentElement;parent;parent=parent.parentElement){
            observer.observe(parent,{childList:true});if(parent===route)break;
          }
        }else{
          // A stalled Steam render may finish much later. Wait for its actual
          // DOM insertion with no timeout or polling, only while this route is visible.
          observer.observe(route,{childList:true,subtree:true});
        }
        return !!target;
      };
      find();route.addEventListener('focusin',find,true);
      return()=>{observer.disconnect();route.removeEventListener('focusin',find,true);};
    },[props.page,props.appId,props.routeRoot,visible,enabled]);
    // Cached Steam routes and scrolled detail headers can stay mounted and
    // focused while their background is outside the viewport. Document focus
    // alone cannot authorize a decoder. Observe only this owned background;
    // no scroll listener, per-frame work or polling is needed.
    React.useEffect(()=>{
      const target=surface as Element|null,view=target?.ownerDocument.defaultView;
      if(!target||!view||!enabled||!visible){inView.current=false;setViewport(null);return;}
      let ended=false;
      const updateVisibility=(show:boolean)=>{
        if(ended)return;
        inView.current=show;
        setViewport((previous:any)=>previous?.target===target&&previous.visible===show?previous:{target,visible:show});
        if(!show)players.current?.stop();
      };
      if(typeof view.IntersectionObserver!=='function'){
        updateVisibility(target.isConnected);
        return()=>{ended=true;inView.current=false;};
      }
      const observer=new view.IntersectionObserver(entries=>{
        const entry=entries.find(value=>value.target===target);
        if(entry)updateVisibility(target.isConnected&&entry.isIntersecting&&entry.intersectionRect.width>0&&entry.intersectionRect.height>0);
      });
      observer.observe(target);
      return()=>{ended=true;inView.current=false;observer.disconnect();};
    },[surface,enabled,visible]);
    const onScreen=!!surface?.isConnected&&viewport?.target===surface&&viewport.visible;
    // frame may belong to the preceding request for this exact owner/selection;
    // report revokes it synchronously whenever that display authority changes.
    const active=!disposed&&enabled&&admitted.current&&currentOwner===owner&&frame?.entry&&visible&&onScreen;
    const displayed=frame;
    React.useEffect(()=>{
      const doc=root.current?.ownerDocument;if(!doc)return;
      if(!players.current)players.current=new WallpaperPlayers(doc,(failed:any)=>{if(!disposed&&enabled&&currentOwner===owner&&currentVisible&&failed.contextSeq===currentSeq&&failed.appId===current?.appId){trace('playback-failed',{appId:failed.appId,seq:failed.contextSeq});client.sendWallpaper('wallpaper.status',{contextSeq:failed.contextSeq,appId:failed.appId,assetId:failed.entry.assetId,bindingRevision:failed.entry.bindingRevision,status:'playback-failed'});}},{entrance,allowed:()=>admitted.current,presented:(shown:any)=>trace('video-ready',{appId:shown.appId,seq:shown.contextSeq})});
      const player=players.current;
      if(!enabled||!visible||!onScreen||disposed){player.stop();return;}
      if(active&&displayed?.entry.kind==='video'&&output.current){player.show(output.current,displayed);}else player.clearCurrent();
    });
    React.useEffect(()=>()=>{players.current?.stop();players.current=null;},[]);
    React.useEffect(()=>{if(!surface||!active)return;const target=surface as HTMLElement;target.setAttribute('data-ymcc-wallpaper-target',props.page);if(displayed?.entry.kind==='image')target.setAttribute('data-ymcc-wallpaper-ready','true');return()=>{target.removeAttribute('data-ymcc-wallpaper-target');target.removeAttribute('data-ymcc-wallpaper-ready');};},[surface,active,props.page,frame?.entry?.kind]);
    const nativeImage=props.page==='/library/home'?classes.homeImages:classes.detailImages;
    const content=React.createElement(React.Fragment,null,active?React.createElement('style',null,`[data-ymcc-wallpaper-target="${props.page}"]{opacity:1!important}[data-ymcc-wallpaper-target="${props.page}"] .${nativeImage}{transition:opacity ${displayed?.entry.kind==='video'?wallpaperEntranceMs(entrance):0}ms ease-out!important}[data-ymcc-wallpaper-target="${props.page}"][data-ymcc-wallpaper-ready="true"] .${nativeImage}{opacity:0!important}@media(prefers-reduced-motion:reduce){[data-ymcc-wallpaper-target="${props.page}"] .${nativeImage}{transition:none!important}}`):null,
      React.createElement('div',{ref:output,'data-ymcc-wallpaper-surface':props.page,style:{position:'absolute',inset:0,pointerEvents:'none',zIndex:0,overflow:'hidden'}},
      active&&frame.entry.kind==='image'?React.createElement('img',{src:frame.entry.url,alt:'',style:{width:'100%',height:'100%',objectFit:frame.entry.playback?.fit||'cover',opacity:frame.opacity??0.64,filter:(frame.blur??0)>0?`blur(${frame.blur}px)`:'none'}}):null));
    return React.createElement(React.Fragment,null,React.createElement('span',{ref:root,style:{display:'none'}}),surface&&active?reactDom.createPortal(content,surface):null);
  }
  const patches:Array<{path:string;patch:any}>=[];
  for(const path of ['/library/home','/library/app/:appid']){
    const patch=(route:any)=>{
      const original=route.children;const Original=original?.type;if(!Original)return route;
      const Wrapped=(props:any)=>{
        const [,updateRoute]=React.useState(0);
        React.useEffect(()=>{const listener=()=>updateRoute((n:number)=>n+1);routeListeners.add(listener);return()=>{routeListeners.delete(listener);};},[]);
        const renderer=props.renderFunc??original.props?.renderFunc;
        // Steam's detail route passes its confirmed overview to renderFunc, including shortcuts.
        const detailRenderer=React.useMemo(()=>typeof renderer==='function'?(overview:any,...args:any[])=>React.createElement(React.Fragment,null,
          React.createElement(Surface,{page:path,appId:overview?.appid}),renderer(overview,...args)):null,[renderer]);
        const detail=path==='/library/app/:appid'&&detailRenderer;
        if(disposed)return React.createElement(Original,{...original.props,...props});
        return React.createElement('div',{'data-ymcc-wallpaper-route':path,style:{position:'relative',width:'100%',height:'100%',isolation:'isolate'}},
          detail?null:React.createElement(Surface,{page:path,appId:props.match?.params?.appid??props.appid??original.props?.match?.params?.appid}),
          React.createElement('div',{style:{position:'relative',zIndex:1,width:'100%',height:'100%'}},React.createElement(Original,{...original.props,...props,...(detail?{renderFunc:detailRenderer}:{})})));
      };
      return {...route,children:React.createElement(Wrapped,original.props)};
    };
    patches.push({path,patch:routerHook.addPatch(path,patch)});
  }
  const offExtra=client.subscribeExtra((message:any)=>{
    if(!message||disposed||!transportAvailable)return;
    if(message.type!=='wallpaper-ready'&&message.type!=='wallpaper')return;
    if(message.type==='wallpaper'&&(message.contextSeq!==currentSeq||message.appId&&message.appId!==current?.appId))return;
    if(message.type==='wallpaper-ready'&&message.contextSeq!==undefined&&message.contextSeq!==currentSeq)return;
    const generation=Number(message.resumeGeneration);
    if(message.type==='wallpaper-ready'&&message.resumeGeneration!==undefined&&(!Number.isSafeInteger(generation)||generation<=resumeGeneration))return;
    const owner=typeof message.stateOwner==='string'&&message.stateOwner?message.stateOwner:null;
    if(owner&&owner!==stateOwner){
      if(retiredStateOwners.has(owner))return;
      if(stateOwner&&!(message.type==='wallpaper-ready'&&needReady&&message.contextSeq===currentSeq)){
        if(!needReady&&current&&currentOwner){needReady=true;trace('ready-recheck',{reason:'host-changed'});report(current,currentOwner,currentVisible,true);}return;
      }
      if(stateOwner){retiredStateOwners.add(stateOwner);if(retiredStateOwners.size>8)retiredStateOwners.delete(retiredStateOwners.values().next().value!);}
      stateOwner=owner;stateRevision=0;resumeGeneration=0;
    }
    if(message.stateRevision!==undefined){
      const revision=Number(message.stateRevision);if(!Number.isSafeInteger(revision)||revision<=0)return;
      if(revision<stateRevision){
        // A recreated YMCC renderer starts its local revision again. Confirm its
        // current intent with a fresh context, rather than waiting forever on
        // a revision belonging to the previous wallpaper host.
        if(message.type==='wallpaper-ready'&&needReady&&message.contextSeq===currentSeq){stateRevision=0;resumeGeneration=0;}
        else{if(!needReady&&current&&currentOwner){needReady=true;report(current,currentOwner,currentVisible,true);}return;}
      }
      stateRevision=revision;
    }
    if(message.type==='wallpaper-ready'){playbackKnown=true;needReady=false;readyContextSentAt=null;trace('ready',{enabled:message.enabled===true});const changed=enabled!==(message.enabled===true);enabled=message.enabled===true;
      const resumed=enabled&&Number.isSafeInteger(generation)&&generation>resumeGeneration;
      if(resumed){resumeGeneration=generation;trace('native-resume',{generation});}if(!enabled||resumed){resetPlayers();frame=null;}
      if((changed||resumed)&&current&&currentOwner)report(current,currentOwner,currentVisible,true);refresh();return;}
    if(message.type!=='wallpaper'||message.contextSeq!==currentSeq||message.appId&&message.appId!==current?.appId)return;
    // Older hosts can miss the initial ready push while passive demand is off.
    // A legacy frame correlated to the current request can bootstrap unknown intent;
    // once OFF is known, a cached frame cannot turn it back on.
    if(message.enabled===true&&!enabled&&(playbackKnown||message.stateRevision!==undefined))return;
    playbackKnown=true;needReady=false;readyContextSentAt=null;enabled=message.enabled===true;if(!enabled)resetPlayers();
    if(!enabled||!message.entry){trace('frame-empty',{appId:current?.appId,seq:currentSeq,enabled,status:typeof message.status==='string'?message.status.slice(0,64):undefined});frame=null;refresh();return;}
    const entry=message.entry;
    if(!validEntry(entry)){trace('frame-rejected',{appId:current?.appId,seq:currentSeq});return;}
    const same=frame?.contextSeq===message.contextSeq&&frame?.entry?.url===entry.url&&frame?.entry?.assetId===entry.assetId&&frame?.entry?.revision===entry.revision&&frame?.entry?.bindingRevision===entry.bindingRevision&&frame?.opacity===message.opacity&&frame?.blur===message.blur&&JSON.stringify(frame?.entry?.playback)===JSON.stringify(entry.playback);
    frame=message;if(!same){trace('frame',{appId:current?.appId,seq:currentSeq,kind:entry.kind});refresh();}
  });
  const offConnection=client.subscribe(state=>{transportAvailable=state.connected;if(!state.connected){playbackKnown=false;needReady=true;readyContextSentAt=null;resetPlayers();frame=null;enabled=false;resumeGeneration=0;stateRevision=0;stateOwner=null;retiredStateOwners.clear();refresh();}else if(current&&currentOwner)report(current,currentOwner,currentVisible,true);},{connectionOnly:true});
  const bootstrap=()=>{
    const wanted=!disposed&&roots.size>0&&host.__YMCC_DECKY_MIRROR__?.wallpaperProtocol===1;
    if(wanted&&!wallpaperDemand){needReady=true;readyContextSentAt=null;}wallpaperDemand=wanted;
    client.setPassiveDemand('wallpaper',wanted);
    // A late bootstrap can grant demand without opening a new socket. Retry
    // only a context that was not accepted; ordinary focus/DOM updates stay idle.
    if(wanted&&contextPending&&current&&currentOwner)report(current,currentOwner,currentVisible,true);
  };
  host.addEventListener('ymcc-decky-bootstrap',bootstrap);bootstrap();
  const disposeFallback=installWallpaperRouteFallback({host,react:React,reactDom,ui,routerHook,homeRootClass:classes.homeRoot,Surface,isDisposed:()=>disposed,trace});
  return()=>{if(disposed)return;disposed=true;disposeFallback();for(const foreground of foregrounds.values())foreground.stop();foregrounds.clear();resetPlayers();frame=null;enabled=false;refresh();for(const listener of routeListeners)listener();routeListeners.clear();offExtra();offConnection();host.removeEventListener('ymcc-decky-bootstrap',bootstrap);for(const {path,patch} of patches)routerHook.removePatch(path,patch);listeners.clear();owners.clear();client.setPassiveDemand('wallpaper',false);};
}

