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
  const eligible=(menu:any)=>{
    try{return !stopped&&owns()&&Object.getPrototypeOf(menu)===prototype&&menu.m_Instance?.MenuStore===menu&&
      menu.m_Instance?.IsGamepadUIWindow?.()===true&&menu.m_Instance?.IsAnyVRWindow?.()===false&&
      !(menu.m_cSuppressRequests>0)&&typeof menu.OpenQuickAccessMenu==='function';}catch{return false;}
  };
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
  function dispose(){if(stopped)return;stopped=true;for(const patch of patches.reverse())try{patch.unpatch();}catch{}patches.length=0;}
  return {get active(){return !stopped&&owns();},dispose};
}
