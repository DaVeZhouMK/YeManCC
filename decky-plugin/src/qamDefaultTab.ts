// Select the console when the user opens QAM. No menu/window is opened during installation.
// Uses Steam's existing selector with open=false; its toggle still owns opening and closing.
export function installConsoleDefault(dfl:any,host:any,id:number,owns:()=>boolean):{active:boolean;dispose():void} {
  const inactive={active:false,dispose(){}};
  if(typeof dfl.beforePatch!=='function'||dfl.SideMenu?.QuickAccess!==2||dfl.SideMenu?.None!==0||dfl.QuickAccessTab?.Decky!==999)return inactive;
  const stores=dfl.Router?.WindowStore, primary=stores?.GamepadUIMainWindowInstance??stores?.SteamUIWindows?.[0];
  const prototype=primary?.MenuStore&&Object.getPrototypeOf(primary.MenuStore);
  const toggle=Object.getOwnPropertyDescriptor(prototype??{},'ToggleSideMenu');
  if(!prototype||typeof toggle?.value!=='function'||!toggle.writable||!toggle.configurable||
    !['GetOpenSideMenu','GetQuickAccessTab','OpenQuickAccessMenu','OpenSideMenu','CloseSideMenus'].every(k=>typeof prototype[k]==='function'))return inactive;
  // Verify the currently exposed Steam route rather than trusting a method name alone.
  const source=String(toggle.value);
  if(!['GetOpenSideMenu','OpenSideMenu','CloseSideMenus'].every(k=>source.includes(k)))return inactive;
  let stopped=false;const patches:any[]=[];
  const checkable=(menu:any)=>{
    try{return !stopped&&Object.getPrototypeOf(menu)===prototype&&menu.m_Instance?.MenuStore===menu&&
      menu.m_Instance?.IsGamepadUIWindow?.()===true&&menu.m_Instance?.IsAnyVRWindow?.()===false&&
      !(menu.m_cSuppressRequests>0)&&typeof menu.OpenQuickAccessMenu==='function';}catch{return false;}
  };
  const eligible=(menu:any)=>{try{return checkable(menu)&&owns();}catch{return false;}};
  const navigationMenu=()=>{
    let windowInstance:any;
    try{windowInstance=host.SteamUIStore?.GetFocusedWindowInstance?.();}catch{}
    // Same fallback used by the pinned Decky Navigation implementation.
    windowInstance??=dfl.Router?.WindowStore?.GamepadUIMainWindowInstance??dfl.Router?.WindowStore?.SteamUIWindows?.[0];
    return windowInstance?.MenuStore;
  };
  const select=(menu:any)=>{if(menu.GetQuickAccessTab()!==id)menu.OpenQuickAccessMenu(id,false);};
  try{
    patches.push(dfl.beforePatch(prototype,'ToggleSideMenu',function consoleDefaultToggle(this:any,args:any[]){
      try{
        if(args[0]!==dfl.SideMenu.QuickAccess||!eligible(this)||this.GetOpenSideMenu()===dfl.SideMenu.QuickAccess)return;
        // undefined means toggle; false explicitly asks to close. Neither changes the original operation.
        if(args[1]!==undefined&&args[1]!==true)return;
        select(this);
      }catch{/* Preserve Steam's normal toggle when selection cannot be confirmed. */}
    }));
    const navigation=dfl.Navigation;
    for(const name of ['OpenSideMenu','OpenQuickAccessMenu']){
      const descriptor=Object.getOwnPropertyDescriptor(navigation??{},name);
      if(typeof descriptor?.value!=='function'||!descriptor.writable||!descriptor.configurable)continue;
      patches.push(dfl.beforePatch(navigation,name,function consoleDefaultNavigation(args:any[]){
        try{
          const menu=navigationMenu();if(!eligible(menu))return;
          if(name==='OpenSideMenu'){
            if(args[0]===dfl.SideMenu.QuickAccess&&menu.GetOpenSideMenu()!==dfl.SideMenu.QuickAccess)select(menu);
          }else if(args[0]===undefined||args[0]===dfl.QuickAccessTab.Decky){
            // The explicit native tab-click path calls MenuStore directly and remains untouched.
            args[0]=id;
          }
        }catch{/* Unknown focused windows keep the original navigation. */}
      }));
    }
  }catch{dispose();return inactive;}
  // Observe the real MenuStore transition once, below Toggle and Navigation.
  // Checking a connection does not require owning the console or selecting it.
  // A missing afterPatch keeps the original selection-only behavior intact.
  const open=Object.getOwnPropertyDescriptor(prototype,'OpenSideMenu');
  if(typeof dfl.afterPatch==='function'&&typeof open?.value==='function'&&open.writable&&open.configurable){
    const observation:any[]=[],opening=new WeakMap<object,boolean>();
    try{
      observation.push(dfl.beforePatch(prototype,'OpenSideMenu',function consoleCheckBefore(this:any,args:any[]){
        try{if(args[0]===dfl.SideMenu.QuickAccess)opening.set(this,checkable(this)&&this.GetOpenSideMenu()!==dfl.SideMenu.QuickAccess);}catch{}
      }));
      observation.push(dfl.afterPatch(prototype,'OpenSideMenu',function consoleCheckAfter(this:any,args:any[],result:any){
        try{
          if(args[0]!==dfl.SideMenu.QuickAccess)return result;
          const opened=opening.get(this);opening.delete(this);
          // Consuming the receipt before dispatch coalesces nested calls. A new
          // call replaces an unfinished receipt if an earlier Steam call threw.
          if(opened&&checkable(this)&&this.GetOpenSideMenu()===dfl.SideMenu.QuickAccess&&
            typeof host.dispatchEvent==='function'&&typeof host.Event==='function')
            host.dispatchEvent(new host.Event('ymcc-decky-connection-check'));
        }catch{/* Observation must never change Steam's action or its return value. */}
        return result;
      }));
      patches.push(...observation);
    }catch{for(const patch of observation.reverse())try{patch.unpatch();}catch{}}
  }
  function dispose(){if(stopped)return;stopped=true;for(const patch of patches.reverse())try{patch.unpatch();}catch{}patches.length=0;}
  return {get active(){return !stopped&&owns();},dispose};
}
