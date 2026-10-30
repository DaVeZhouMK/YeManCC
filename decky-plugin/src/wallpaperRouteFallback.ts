// Public Decky global mount for the native Home before the normal route patch mounts.
export function installWallpaperRouteFallback({host,react:React,reactDom,ui,routerHook,homeRootClass,Surface,isDisposed,trace}:any):()=>void {
  const name='YMCCSteamWallpaperRouteFallback',route='[data-ymcc-wallpaper-route]';
  const cleanups=new Set<(clear?:boolean)=>void>();let stopped=false;
  const disposed=()=>stopped||isDisposed();
  const note=(stage:string)=>{try{trace(stage,{page:'/library/home'});}catch{}};
  if(disposed()||typeof routerHook?.addGlobalComponent!=='function'||typeof routerHook?.removeGlobalComponent!=='function'||
    typeof reactDom?.createPortal!=='function'||typeof ui?.findSP!=='function'||!/^[A-Za-z0-9_-]{1,100}$/.test(homeRootClass||''))return()=>{};
  function Global(){
    const anchor=React.useRef(null),selected=React.useRef(null),[root,setRoot]=React.useState(null);
    const portal=React.useMemo(()=>root?reactDom.createPortal(React.createElement('div',{
      'data-ymcc-wallpaper-fallback-route':'/library/home',style:{display:'contents'}
    },React.createElement(Surface,{page:'/library/home',routeRoot:root})),root):null,[root]);
    React.useEffect(()=>{
      const doc=anchor.current?.ownerDocument,view=doc?.defaultView;
      if(!doc||!view||disposed())return;
      const Observer=view.MutationObserver||host?.MutationObserver;
      if(typeof Observer!=='function')return;
      let dead=false,wide=false,unknownDiscovered=false,unlisten:any,scopes:any[]=[];
      const publish=(next:any)=>{
        if(selected.current===next)return;
        selected.current=next;setRoot((prior:any)=>prior===next?prior:next);
        note(next?'fallback-mounted':'fallback-cleared');
      };
      const shown=(element:any)=>{
        if(!element?.isConnected)return false;
        try{for(let node=element;node;node=node.parentElement){
          const css=view.getComputedStyle(node);
          if(node.hidden||css.display==='none'||css.visibility==='hidden'||css.visibility==='collapse')return false;
        }}catch{return false;}return true;
      };
      const observer=new Observer(check);
      function bind(roots:any[],wait:boolean){
        const next:any[]=[];
        if(wait){const body=doc.body||doc.documentElement;if(body)next.push(body);}
        else for(const root of roots)for(let node=root;node;node=node.parentNode)if(!next.includes(node))next.push(node);
        if(wide===wait&&scopes.length===next.length&&scopes.every((node,index)=>node===next[index]))return;
        observer.disconnect();wide=wait;scopes=next;
        for(const node of scopes)observer.observe(node,{childList:true,subtree:wait});
      }
      function clear(clearPortal=true){
        if(dead)return;dead=true;observer.disconnect();scopes=[];
        try{unlisten?.();}catch{}unlisten=undefined;
        doc.removeEventListener('focusin',check,true);doc.removeEventListener('visibilitychange',check);
        view.removeEventListener('focus',check);view.removeEventListener('pageshow',check);
        cleanups.delete(clear);if(clearPortal)publish(null);
      }
      function check(){
        if(dead)return;if(disposed()){clear();return;}
        let pathname:any;
        try{pathname=ui.Router?.WindowStore?.GamepadUIMainWindowInstance?.m_locationPathname;}catch{}
        const home=pathname==='/library/home';
        if(typeof pathname==='string'&&pathname.length>0&&!home){publish(null);bind([],false);return;}
        let eligible=false;try{eligible=ui.findSP()?.document===doc;}catch{}
        if(!eligible){publish(null);bind([],false);return;}
        if(!home&&unknownDiscovered){
          const current=selected.current;
          if(current&&(!shown(current)||current.closest(route)))publish(null);
          bind(selected.current?[selected.current]:[],false);return;
        }
        if(!home)unknownDiscovered=true;
        const homes:any[]=Array.from(doc.getElementsByClassName(homeRootClass));
        const visible=homes.filter(shown),normal:any[]=Array.from(doc.querySelectorAll(route)).filter(shown);
        const nativeHomeRoot=visible.length===1?visible[0]:null;
        publish(nativeHomeRoot&&!nativeHomeRoot.closest(route)&&normal.length===0?nativeHomeRoot:null);
        // Visible roots only need direct changes; hidden cached routes do not end the insertion wait.
        // A visible normal route owns playback and also ends the broad insertion wait.
        bind(visible.length?visible:normal.slice(0,1),home&&visible.length===0&&normal.length===0);
      }
      cleanups.add(clear);
      doc.addEventListener('focusin',check,true);doc.addEventListener('visibilitychange',check);
      view.addEventListener('focus',check);view.addEventListener('pageshow',check);
      try{
        const history=ui.Router?.WindowStore?.GamepadUIMainWindowInstance?.m_history;
        if(typeof history?.listen==='function'){
          const off=history.listen(check);if(typeof off==='function')unlisten=off;
        }
      }catch{}
      check();return()=>clear(false);
    },[]);
    return React.createElement('span',{ref:anchor,style:{display:'contents'}},portal);
  }
  routerHook.addGlobalComponent(name,Global);note('fallback-installed');
  return()=>{
    if(stopped)return;stopped=true;
    for(const clear of [...cleanups])clear();
    routerHook.removeGlobalComponent(name);
  };
}
