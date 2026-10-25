import { monitorFileSystemDemand } from './monitorNativeDemand';
import { HUD_FRESH_FAMILIES } from './monitorHudFreshness';
// One existing YMCC collection demand and one passive snapshot/TDP binding,
// shared by any number of display carriers. No timers or secondary HW readers.
import type { HudData, HudNativeDemand } from './monitorHudModel';
import { hudFromUniformSnapshot } from './monitorHudModel';
import type { MonitorSnapshotView, TdpRuntimeSnapshot } from './monitorData';
export interface HudSampleSourceDeps {
  subscribeSnapshot(listener:(value:MonitorSnapshotView|null)=>void):()=>void;
  subscribeTdp(listener:(value:TdpRuntimeSnapshot)=>void):()=>void;
  cpuName?(): string;
  subscribeGameLogo?(listener:(image:string)=>void):()=>void;
  readOnce():Promise<MonitorSnapshotView|null>;acquire(need:HudNativeDemand):()=>void;now():number;
  // Capability supplied by the existing sole publisher, never Object.freeze.
  canReuseSnapshot?(value:MonitorSnapshotView|null):boolean;
}
export function createHudSampleSource(deps:HudSampleSourceDeps){
  let users=0,epoch=0,revision=0,snapshot:MonitorSnapshotView|null=null,tdp:TdpRuntimeSnapshot|null=null;
  let off:(()=>void)|null=null,offTdp:(()=>void)|null=null,release:(()=>void)|null=null;
  let demand:HudNativeDemand={top:true,fps:true};
  let logoEnabled=false,logo='',offLogo:(()=>void)|null=null,clockEnabled=true;
  // Both carriers share one pure projection while the admitted inputs remain
  // immutable and before their earliest real deadline. No proof is renewed.
  let cachedUntil=-Infinity,cachedReusable=false;
  let cachedTdpStatus:TdpRuntimeSnapshot['status']|undefined,cachedTdpProof:TdpRuntimeSnapshot['proof']|undefined;
  let cachedTdpMode:TdpRuntimeSnapshot['mode']|undefined,cachedTdpWatts:number|null|undefined;
  let cachedSnapshot:MonitorSnapshotView|null=null,cachedTdp:TdpRuntimeSnapshot|null=null,cachedAt=-1,cachedLogo='',cachedLogoEnabled=false,cachedClockEnabled=true,cachedCpuName='',cachedData:HudData|null=null;
  function syncLogo(){
    if(users>0&&logoEnabled&&!offLogo&&deps.subscribeGameLogo)offLogo=deps.subscribeGameLogo(image=>{logo=image;});
    if((users===0||!logoEnabled)&&offLogo){offLogo();offLogo=null;logo='';}
  }
  function cleanup() {
    const pending = [offLogo, off, offTdp, release]; offLogo = null; logo = ''; off = offTdp = release = null; snapshot = null; tdp = null;
    cachedUntil=-Infinity;cachedReusable=false;
    cachedSnapshot = null; cachedTdp = null; cachedAt = -1; cachedLogo = ''; cachedLogoEnabled = false; cachedClockEnabled = false; cachedData = null;
    for (const close of pending) { try { close?.(); } catch { /* finish all owned releases */ } }
  }
  return {
    sample:():HudData=>{
      const now=deps.now(),cpuName=deps.cpuName?.()??'';
      if(cachedData!==null&&cachedCpuName===cpuName&&cachedSnapshot===snapshot&&cachedTdp===tdp&&cachedTdpStatus===tdp?.status&&cachedTdpProof===tdp?.proof&&cachedTdpMode===tdp?.mode&&cachedTdpWatts===tdp?.watts&&
        (cachedAt===now||(cachedReusable&&Number.isFinite(now)&&now>=cachedAt&&now<cachedUntil))&&cachedLogo===logo&&cachedLogoEnabled===logoEnabled&&cachedClockEnabled===clockEnabled)return cachedData;
      cachedTdpStatus=tdp?.status;cachedTdpProof=tdp?.proof;cachedTdpMode=tdp?.mode;cachedTdpWatts=tdp?.watts;
      cachedCpuName=cpuName;cachedSnapshot=snapshot;cachedTdp=tdp;cachedAt=now;cachedLogo=logo;cachedLogoEnabled=logoEnabled;cachedClockEnabled=clockEnabled;
      cachedData={...hudFromUniformSnapshot(snapshot,now,tdp,clockEnabled,cpuName),...(logoEnabled&&logo?{gameLogo:logo}:{})};
      cachedReusable=Number.isFinite(now)&&deps.canReuseSnapshot?.(snapshot)===true;
      cachedUntil=clockEnabled?now-now%60000+60000:Infinity;
      if(cachedReusable){
        for(const family of HUD_FRESH_FAMILIES){const end=cachedData.freshUntil?.[family];if(typeof end==='number'&&end>now&&end<cachedUntil)cachedUntil=end;}
        // The result is shared beyond one call. Freeze only our projection and
        // the source's already-owned arrays; never freeze an untrusted caller.
        Object.freeze(cachedData.values);Object.freeze(cachedData.freshUntil);
        Object.freeze(cachedData.coreUsage);Object.freeze(cachedData.coreLabels);
        Object.freeze(cachedData.coreClocks);Object.freeze(cachedData.frameTimes);Object.freeze(cachedData);
      }
      return cachedData;
    },
    setDemand(next:HudNativeDemand){
      const wanted={top:next.top===true,fps:next.fps===true,...(next.fileSystem===undefined?{}:{fileSystem:next.fileSystem===true})};
      if(wanted.top===demand.top&&wanted.fps===demand.fps&&monitorFileSystemDemand(wanted)===monitorFileSystemDemand(demand))return;
      if(users>0){
        // The facade replaces this owner's token synchronously. Acquire first
        // so the retired disposer cannot create a transient zero-demand gap.
        const replacement=deps.acquire(wanted),previous=release;
        release=replacement;demand=wanted;
        previous?.();
      }else demand=wanted;
    },
    setClockEnabled(enabled:boolean){clockEnabled=enabled;},
    setGameLogoEnabled(enabled:boolean){logoEnabled=enabled;syncLogo();},
    acquire(){
      if(users++===0){
        const own=++epoch;
        try{
          off=deps.subscribeSnapshot(value=>{if(own===epoch){++revision;snapshot=value;}});
          offTdp=deps.subscribeTdp(value=>{if(own===epoch)tdp=value;});
          release=deps.acquire(demand);syncLogo();
          const atRead=revision;
          void deps.readOnce().then(value=>{if(own===epoch&&revision===atRead)snapshot=value;}).catch(()=>{});
        }catch(error){users=0;++epoch;cleanup();throw error;}
      }
      const own=epoch;let stopped=false;return()=>{if(stopped)return;stopped=true;if(own!==epoch)return;if(--users===0){++epoch;cleanup();}};
    },
    users:()=>users,
  };
}
