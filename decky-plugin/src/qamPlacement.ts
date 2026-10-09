// Dedicated, direct-to-controls console tab. Original Decky home is left intact.
export const YMCC_CONSOLE_TAB_ID = 100019;
export function installConsoleTab(dfl: any, host: any, content: any, icon: any): { active: boolean; dispose(): void } {
  const hook=host.__TABS_HOOK_INSTANCE, id=YMCC_CONSOLE_TAB_ID;
  if(typeof dfl.afterPatch!=='function'||typeof hook?.render!=='function'||typeof hook.add!=='function'||typeof hook.removeById!=='function'||
    !Array.isArray(hook.tabs)||!hook.tabs.some((t:any)=>t.id===999)||hook.tabs.some((t:any)=>t.id===id)||
    dfl.QuickAccessTab?.Decky!==999||dfl.QuickAccessTab?.Notifications!==0||typeof WeakRef!=='function')return {active:false,dispose(){}};
  const seen=new WeakSet<any[]>(), receipts:WeakRef<any[]>[]=[];
  let stopped=false,patch:any;
  const remove=(tabs:any[])=>{if(Object.isFrozen(tabs))return;for(let i=tabs.length-1;i>=0;i--)if(String(tabs[i]?.key)===String(id)&&tabs[i]?.decky===true)tabs.splice(i,1);};
  try {
    hook.add({id,title:'YMCC 控制台',content,icon});
    patch=dfl.afterPatch(hook,'render',(args:any[],result:any)=>{
      const tabs=args[0];if(stopped||!Array.isArray(tabs)||Object.isFrozen(tabs))return result;
      const consoles=tabs.filter(t=>String(t?.key)===String(id)&&t?.decky===true);
      if(consoles.length<1||tabs.filter(t=>String(t?.key)==='0').length!==1)return result;
      try {
        if(!seen.has(tabs)){for(let i=receipts.length-1;i>=0;i--)if(!receipts[i].deref())receipts.splice(i,1);
          if(receipts.length===32){dispose();remove(tabs);return result;}
          seen.add(tabs);receipts.push(new WeakRef(tabs));}
        // Native TabsHook may render the same mutable array again; collapse only our own entries.
        const current=consoles[consoles.length-1];remove(tabs);tabs.unshift(current);
      }catch{/* Unknown Steam layout must not break native QAM. */}
      return result;
    });
  }catch{try{hook.removeById(id);}catch{}return {active:false,dispose(){}};}
  function dispose(){if(stopped)return;stopped=true;try{patch.unpatch();}catch{}try{hook.removeById(id);}catch{}
    for(const receipt of receipts){const tabs=receipt.deref();if(tabs)try{remove(tabs);}catch{}}receipts.length=0;
  }
  return {get active(){return !stopped;},dispose};
}
