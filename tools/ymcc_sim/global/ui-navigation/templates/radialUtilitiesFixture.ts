// Actual utility state machine and wheel; process/settings IO stays in this private memory fixture.
import {createApp,h,ref} from 'vue';import RadialWheel from '@/components/RadialWheel.vue';
import {defaultRadialSettings} from '@/radial/model';import {createRadialUtilityActions} from '@/radial/utilityActions';
import {emptyRadialUtilities,isUtilityGroup,type RadialUtilityGroup,type RadialUtilityIntent} from '@/radial/utilities';
import {normalizeMonitorOutput,switchMonitorHudTemplate} from '@/bridge/monitorHudModel';import {createSteamUi} from '@/bridge/steamUi';
export function mountRadialUtilitiesWheel(){
 const node=document.createElement('div');node.style.cssText='position:fixed;inset:0;background:#0b111a;z-index:2400;display:grid;place-items:center';document.body.append(node);
 const copy=(value:any)=>JSON.parse(JSON.stringify(value)),items=defaultRadialSettings().pages[0].items;
 const s:any={prefs:normalizeMonitorOutput({enabled:false,output:'ymcc',ymccTemplate:'A',ymccTemplateRevision:10,ymcc:{scale:1.3,dragLocked:true},ymccTemplates:{A:{scale:1.3},B:{scale:.7}}}),steam:{known:true,running:false,mode:'stopped',pid:0,birth:'0',reason:''},launchPending:false,clock:1000};
 let alive=true,epoch=0,revision=0,lastGroup:RadialUtilityGroup|null=null;const utilities=ref(emptyRadialUtilities()),pending=ref(false),status=ref(''),wheel=ref<any>(null),events:any[]=[],calls:any[]=[],regions=ref<number[][][]>([]);
 const api=createSteamUi({running:async()=>s.steam.running,invoke:async(command,args)=>{if(command==='steam.uiState')return copy(s.steam);calls.push({command,args:copy(args)});return {ok:true}}});
 const adapter=createRadialUtilityActions({alive:()=>alive,power:async()=>({generation:1,phase:'ready',inputReady:true} as any),now:()=>s.clock,nextRevision:()=>String(++revision),
  monitor:{read:async()=>copy(s.prefs),busy:()=>false,save:async(template,version,admit)=>{if(!admit(copy(s.prefs)))throw Error('SOURCE_CHANGED');s.prefs=switchMonitorHudTemplate(s.prefs,template,version);calls.push({command:'template',template})}},
  steam:{read:force=>api.read(force?undefined:{automatic:true}),launchPending:()=>s.launchPending,launch:async admit=>{if(!admit())throw Error('CANCELED');s.launchPending=true;calls.push({command:'launch'})},switchMode:state=>api.switchMode(state,'bigpicture',{userInitiated:true}),shutdown:state=>api.shutdown(state,{userInitiated:true})},
 });
 const refresh=async(group:RadialUtilityGroup)=>{const next=await adapter.read(group,()=>{if(!alive)throw Error('fixture ended')});if(!pending.value&&lastGroup===group&&next.sourceRevision!==utilities.value[group].sourceRevision)status.value=next.notice;utilities.value={...utilities.value,[group]:next}};
 async function execute(parentId:string,intent:RadialUtilityIntent){if(pending.value)return;const group=items.find(item=>item.id===parentId)?.action;if(!isUtilityGroup(group))throw Error('Wrong fixture group');const ticket=epoch;lastGroup=group;pending.value=true;events.push({parentId,intent:copy(intent)});
  try{const result=await adapter.apply(group,intent,{generation:1,checkpoint:()=>{if(!alive||epoch!==ticket)throw Error('fixture ended')}});utilities.value={...utilities.value,[group]:result.frame};status.value=result.notice;}catch(error){status.value=(error as Error).message;}finally{pending.value=false;if(alive)await refresh(group);}}
 const app=createApp({setup:()=>()=>h(RadialWheel,{ref:wheel,items,utilities:utilities.value,pending:pending.value,status:status.value,onUtility:execute,onRegions:(value:number[][][])=>regions.value=value,onClose:()=>events.push({close:true})})});app.mount(node);void Promise.all([refresh('monitor.templates'),refresh('steam.controls')]);
 (window as any).__radialUtilities={get api(){return wheel.value},events,calls,read:()=>({utilities:copy(utilities.value),pending:pending.value,status:status.value,regions:copy(regions.value),prefs:copy(s.prefs),items:copy(items)}),
  setSteam:async(mode:string,pid=123,birth='456')=>{s.steam=mode==='stopped'?{known:true,running:false,mode:'stopped',pid:0,birth:'0',reason:''}:mode==='unavailable'?{known:false,running:false,mode:'unknown',pid:0,birth:'0',reason:'io'}:{known:true,running:true,mode,pid,birth,reason:''};await refresh('steam.controls')},
  changeMonitor:async()=>{s.prefs.ymcc.scale=1.7;await refresh('monitor.templates')},newMenu:async()=>{epoch++;adapter.resetSession();await refresh('steam.controls')},releaseLaunch:async()=>{s.launchPending=false;await refresh('steam.controls')},
  unmount:()=>{alive=false;epoch++;adapter.stop();app.unmount();node.remove()}};
}
