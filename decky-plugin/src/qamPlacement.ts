import { installConsoleDefault } from './qamDefaultTab';
// Dedicated, direct-to-controls console tab. Original Decky home is left intact.
export const YMCC_CONSOLE_TAB_ID = 100019;
export function installConsoleTab(dfl: any, host: any, content: any, icon: any): { active: boolean; dispose(): void } {
  const hook=host.__TABS_HOOK_INSTANCE, id=YMCC_CONSOLE_TAB_ID;
  const nativeRender=hook && Object.getPrototypeOf(hook)?.render;
  if(typeof dfl.afterPatch!=='function'||typeof dfl.beforePatch!=='function'||typeof nativeRender!=='function'||
    typeof hook?.render!=='function'||typeof hook.add!=='function'||typeof hook.removeById!=='function'||
    !Array.isArray(hook.tabs)||!hook.tabs.some((t:any)=>t.id===999)||hook.tabs.some((t:any)=>t.id===id)||
    dfl.QuickAccessTab?.Decky!==999||dfl.QuickAccessTab?.Notifications!==0||typeof WeakRef!=='function')return {active:false,dispose(){}};
  const registration={id,title:'',content,icon};
  const owns=()=>hook.tabs.includes(registration);
  const isConsole=(tab:any)=>String(tab?.key)===String(id)&&tab?.decky===true;
  const providerProps=(tab:any)=>tab?.panel?.props?.children?.props;
  const panelContent=(tab:any)=>providerProps(tab)?.children;
  const isNativeTab=(tab:any)=>tab?.decky===true&&Number.isSafeInteger(tab.key)&&providerProps(tab)?.tab===tab;
  const matches=(tab:any,entry:any)=>entry&&tab.key===entry.id&&tab.title===entry.title&&tab.tab===entry.icon&&panelContent(tab)===entry.content;
  const seen=new WeakSet<any[]>(), receipts:WeakRef<any[]>[]=[];
  let stopped=false,patch:any,before:any;
  const isCurrent=()=>!stopped&&host.__TABS_HOOK_INSTANCE===hook&&owns()&&
    before?.hasUnpatched===false&&patch?.hasUnpatched===false;
  let defaultSelection:{active:boolean;dispose():void}|undefined;
  const remove=(tabs:any[])=>{if(Object.isFrozen(tabs))return;for(let i=tabs.length-1;i>=0;i--)if(isConsole(tabs[i])&&panelContent(tabs[i])===content)tabs.splice(i,1);};
  try {
    hook.add(registration);
    before=dfl.beforePatch(hook,'render',(args:any[])=>{
      const tabs=args[0];if(!isCurrent()||!Array.isArray(tabs)||Object.isFrozen(tabs))return;
      const entries=hook.tabs;
      let next=0,ownCount=0,cached=0,dirty=false;
      for(const tab of tabs){
        if(!tab?.decky)continue;
        if(!isNativeTab(tab))return; // Unknown Loader shape: leave the native render in charge.
        ++cached;
        if(tab.key===id){if(++ownCount!==1||!matches(tab,registration))dirty=true;}
        else {if(entries[next]===registration)++next;if(!matches(tab,entries[next++]))dirty=true;}
      }
      if(cached===0||(!dirty&&ownCount===1&&cached===entries.length))return;

      // The native renderer compares counts only and appends all registrations
      // on mismatch. Restore its cache before that branch can duplicate panels.
      const byId=new Map<number,any>();
      for(const entry of entries){
        if(!Number.isSafeInteger(entry?.id)||byId.has(entry.id))return;
        byId.set(entry.id,entry);
      }
      const keep=new Map<number,any>();
      for(const tab of tabs){
        if(!tab?.decky||keep.has(tab.key)||!matches(tab,byId.get(tab.key)))continue;
        keep.set(tab.key,tab);
      }
      let fresh:any[]=[];
      if(keep.size!==entries.length){
        nativeRender.call(hook,fresh,args[1]); // A scratch array is never mounted.
        if(fresh.length!==entries.length||!fresh.every((tab,i)=>isNativeTab(tab)&&matches(tab,entries[i])))return;
      }
      const repaired:any[]=[];
      let own:any;
      entries.forEach((entry:any,i:number)=>{
        const tab=keep.get(entry.id)??fresh[i];
        if(entry===registration)own=tab;else repaired.push(tab);
      });
      // Keep Steam's own entries and their order. Existing healthy Decky tabs
      // retain the provider's live visibility setter instead of remounting.
      let slot=0,placedOwn=false;
      for(let i=0;i<tabs.length;){
        if(!tabs[i]?.decky){++i;continue;}
        if(tabs[i].key===id){
          if(!placedOwn){tabs[i++]=own;placedOwn=true;}else tabs.splice(i,1);
        }else if(slot<repaired.length)tabs[i++]=repaired[slot++];else tabs.splice(i,1);
      }
      while(slot<repaired.length)tabs.push(repaired[slot++]);
      if(!placedOwn)tabs.push(own);
    });
    patch=dfl.afterPatch(hook,'render',(args:any[],result:any)=>{
      const tabs=args[0];if(!isCurrent()||!Array.isArray(tabs)||Object.isFrozen(tabs))return result;
      const consoles=tabs.filter(t=>isConsole(t)&&panelContent(t)===content);
      if(consoles.length<1||tabs.filter(t=>String(t?.key)==='0').length!==1)return result;
      try {
        if(!seen.has(tabs)){for(let i=receipts.length-1;i>=0;i--)if(!receipts[i].deref())receipts.splice(i,1);
          if(receipts.length===32){
            // Retire only the oldest array receipt, never the live console.
            // Forget its seen marker so a later render can register it again.
            const oldest=receipts[0].deref();
            if(oldest){remove(oldest);seen.delete(oldest);}
            receipts.shift();
          }
          seen.add(tabs);receipts.push(new WeakRef(tabs));}
        const current=consoles[consoles.length-1];
        if(consoles.length!==1||tabs[0]!==current){remove(tabs);tabs.unshift(current);}
      }catch{/* Unknown Steam layout must not break native QAM. */}
      return result;
    });
    defaultSelection=installConsoleDefault(dfl,host,id,isCurrent);
  }catch{try{defaultSelection?.dispose();}catch{}unpatchRender();try{if(owns())hook.removeById(id);}catch{}return {active:false,dispose(){}};}
  function unpatchRender(){for(const item of [patch,before])try{if(item&&!item.hasUnpatched)item.unpatch();}catch{}}
  function dispose(){if(stopped)return;stopped=true;defaultSelection?.dispose();unpatchRender();
    // A late previous onDismount must not unregister a newer module with our id.
    try{if(owns())hook.removeById(id);}catch{}
    for(const receipt of receipts){const tabs=receipt.deref();if(tabs)try{remove(tabs);}catch{}}receipts.length=0;
  }
  return {get active(){return isCurrent();},dispose};
}
