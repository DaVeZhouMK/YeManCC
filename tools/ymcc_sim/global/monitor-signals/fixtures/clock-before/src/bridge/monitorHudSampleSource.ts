// One existing YMCC collection demand and one passive snapshot/TDP binding,
// shared by any number of display carriers. No timers or secondary HW readers.
import type { HudData } from './monitorHudModel';
import { hudFromUniformSnapshot } from './monitorHudModel';
import type { MonitorSnapshotView, TdpRuntimeSnapshot } from './monitorData';
export interface HudSampleSourceDeps {
  subscribeSnapshot(listener:(value:MonitorSnapshotView|null)=>void):()=>void;
  subscribeTdp(listener:(value:TdpRuntimeSnapshot)=>void):()=>void;
  subscribeGameLogo?(listener:(image:string)=>void):()=>void;
  readOnce():Promise<MonitorSnapshotView|null>;acquire():()=>void;now():number;
}
export function createHudSampleSource(deps:HudSampleSourceDeps){
  let users=0,epoch=0,revision=0,snapshot:MonitorSnapshotView|null=null,tdp:TdpRuntimeSnapshot|null=null;
  let off:(()=>void)|null=null,offTdp:(()=>void)|null=null,release:(()=>void)|null=null;
  let logoEnabled=false,logo='',offLogo:(()=>void)|null=null;
  // Steam and desktop carriers consume the same heartbeat in sequence. Cache
  // only the pure projection for that exact admitted input/time; freshness is
  // still recomputed when the clock advances, so this cannot prolong data.
  let cachedSnapshot:MonitorSnapshotView|null=null,cachedTdp:TdpRuntimeSnapshot|null=null,cachedAt=-1,cachedLogo='',cachedLogoEnabled=false,cachedData:HudData|null=null;
  function syncLogo(){
    if(users>0&&logoEnabled&&!offLogo&&deps.subscribeGameLogo)offLogo=deps.subscribeGameLogo(image=>{logo=image;});
    if((users===0||!logoEnabled)&&offLogo){offLogo();offLogo=null;logo='';}
  }
  function cleanup() {
    const pending = [offLogo, off, offTdp, release]; offLogo = null; logo = ''; off = offTdp = release = null; snapshot = null; tdp = null;
    cachedSnapshot = null; cachedTdp = null; cachedAt = -1; cachedLogo = ''; cachedLogoEnabled = false; cachedData = null;
    for (const close of pending) { try { close?.(); } catch { /* finish all owned releases */ } }
  }
  return {
    sample:():HudData=>{
      const now=deps.now();
      if(cachedData!==null&&cachedSnapshot===snapshot&&cachedTdp===tdp&&cachedAt===now&&cachedLogo===logo&&cachedLogoEnabled===logoEnabled)return cachedData;
      cachedSnapshot=snapshot;cachedTdp=tdp;cachedAt=now;cachedLogo=logo;cachedLogoEnabled=logoEnabled;
      cachedData={...hudFromUniformSnapshot(snapshot,now,tdp),...(logoEnabled&&logo?{gameLogo:logo}:{})};
      return cachedData;
    },
    setGameLogoEnabled(enabled:boolean){logoEnabled=enabled;syncLogo();},
    acquire(){
      if(users++===0){
        const own=++epoch;
        try{
          off=deps.subscribeSnapshot(value=>{if(own===epoch){++revision;snapshot=value;}});
          offTdp=deps.subscribeTdp(value=>{if(own===epoch)tdp=value;});
          release=deps.acquire();syncLogo();
          const atRead=revision;
          void deps.readOnce().then(value=>{if(own===epoch&&revision===atRead)snapshot=value;}).catch(()=>{});
        }catch(error){users=0;++epoch;cleanup();throw error;}
      }
      const own=epoch;let stopped=false;return()=>{if(stopped)return;stopped=true;if(own!==epoch)return;if(--users===0){++epoch;cleanup();}};
    },
    users:()=>users,
  };
}
