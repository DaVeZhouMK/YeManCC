// Dedicated, direct-to-controls console tab. Original Decky home is left intact.
export const YMCC_CONSOLE_TAB_ID = 100019;
export function installConsoleTab(dfl: any, host: any, content: any, icon: any): { active: boolean; dispose(): void } {
  const hook=host.__TABS_HOOK_INSTANCE, id=YMCC_CONSOLE_TAB_ID;
  const nativeRender=hook && Object.getPrototypeOf(hook)?.render;
  if(typeof dfl.afterPatch!=='function'||typeof dfl.beforePatch!=='function'||typeof nativeRender!=='function'||
    typeof hook?.render!=='function'||typeof hook.add!=='function'||typeof hook.removeById!=='function'||
    !Array.isArray(hook.tabs)||!hook.tabs.some((t:any)=>t.id===999)||hook.tabs.some((t:any)=>t.id===id)||
    dfl.QuickAccessTab?.Decky!==999||dfl.QuickAccessTab?.Notifications!==0||typeof WeakRef!=='function')return {active:false,dispose(){}};
  const registration={id,title:'YMCC 控制台',content,icon};
  const owns=()=>hook.tabs.includes(registration);
  const isConsole=(tab:any)=>String(tab?.key)===String(id)&&tab?.decky===true;
  const panelContent=(tab:any)=>tab?.panel?.props?.children?.props?.children;
  const seen=new WeakSet<any[]>(), receipts:WeakRef<any[]>[]=[];
  let stopped=false,patch:any,before:any;
  const remove=(tabs:any[])=>{if(Object.isFrozen(tabs))return;for(let i=tabs.length-1;i>=0;i--)if(isConsole(tabs[i])&&panelContent(tabs[i])===content)tabs.splice(i,1);};
  try {
    hook.add(registration);
    before=dfl.beforePatch(hook,'render',(args:any[])=>{
      const tabs=args[0];if(stopped||!owns()||!Array.isArray(tabs)||Object.isFrozen(tabs))return;
      const consoles=tabs.filter(isConsole);
      if(consoles.length===1&&panelContent(consoles[0])===content)return;
      if(!consoles.length&&!tabs.some(t=>String(t?.key)==='999'&&t?.decky===true))return;
      // Native TabsHook's same-count fast path otherwise keeps an old provider and
      // Content closure. Build ONLY our replacement with the CURRENT native class;
      // a scratch array is never mounted and foreign/native home entries stay intact.
      const scratch:any[]=[];nativeRender.call(hook,scratch,args[1]);
      const current=scratch.find(isConsole);if(!current||panelContent(current)!==content)return;
      let position=tabs.findIndex(isConsole);if(position<0)position=tabs.length;
      for(let i=tabs.length-1;i>=0;i--)if(isConsole(tabs[i]))tabs.splice(i,1);
      tabs.splice(Math.min(position,tabs.length),0,current);
    });
    patch=dfl.afterPatch(hook,'render',(args:any[],result:any)=>{
      const tabs=args[0];if(stopped||!owns()||!Array.isArray(tabs)||Object.isFrozen(tabs))return result;
      const consoles=tabs.filter(t=>isConsole(t)&&panelContent(t)===content);
      if(consoles.length<1||tabs.filter(t=>String(t?.key)==='0').length!==1)return result;
      try {
        if(!seen.has(tabs)){for(let i=receipts.length-1;i>=0;i--)if(!receipts[i].deref())receipts.splice(i,1);
          if(receipts.length===32){dispose();remove(tabs);return result;}
          seen.add(tabs);receipts.push(new WeakRef(tabs));}
        const current=consoles[consoles.length-1];remove(tabs);tabs.unshift(current);
      }catch{/* Unknown Steam layout must not break native QAM. */}
      return result;
    });
  }catch{try{patch?.unpatch();before?.unpatch();if(owns())hook.removeById(id);}catch{}return {active:false,dispose(){}};}
  function dispose(){if(stopped)return;stopped=true;try{patch?.unpatch();before?.unpatch();}catch{}
    // A late previous onDismount must not unregister a newer module with our id.
    try{if(owns())hook.removeById(id);}catch{}
    for(const receipt of receipts){const tabs=receipt.deref();if(tabs)try{remove(tabs);}catch{}}receipts.length=0;
  }
  return {get active(){return !stopped&&owns();},dispose};
}
