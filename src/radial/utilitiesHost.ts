import { powerLifecycle } from '@/bridge/api';
import { readMonitorOutputPreferences,saveMonitorHudTemplate,onMonitorOutputPreferencesChanged,onMonitorOutputApplying } from '@/bridge/monitorOutput';
import { steamUi } from '@/bridge/steamUi';
import { launchSteam,steamLaunchPending } from '@/bridge/yeman';
import { createRadialUtilityActions } from './utilityActions';
import type { RadialUtilityGroup } from './utilities';
export function createLocalRadialUtilityActions(alive:()=>boolean){
 let monitorBusy=false;
 const actions=createRadialUtilityActions({alive,power:()=>powerLifecycle.get(),nextRevision:()=>crypto.randomUUID(),now:Date.now,
  monitor:{read:readMonitorOutputPreferences,save:saveMonitorHudTemplate,busy:()=>monitorBusy},
  steam:{read:force=>steamUi.read(force?undefined:{automatic:true}),launch:admit=>launchSteam({userInitiated:true,isCurrent:admit}),launchPending:steamLaunchPending,switchMode:state=>steamUi.switchMode(state,'bigpicture',{userInitiated:true}),shutdown:state=>steamUi.shutdown(state,{userInitiated:true})},
 });
 return {...actions,observe(listener:(group:RadialUtilityGroup)=>void){const stops=[onMonitorOutputPreferencesChanged(()=>listener('monitor.templates')),onMonitorOutputApplying(busy=>{monitorBusy=busy;listener('monitor.templates')}),steamUi.subscribe(()=>listener('steam.controls'))];return ()=>stops.forEach(stop=>stop());}};
}
