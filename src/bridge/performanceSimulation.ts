/** TS draft session. Reads current production settings; only confirmed Save uses the CAS writer. */
import {getContinuousPerformanceSnapshot,updateContinuousPerformanceControls} from './performanceSchedule';
import {patchContinuousBank,defaultContinuousBank,normalizeHyperThreadMode,type ContinuousPerformanceSnapshot,type ContinuousPerformanceIdentity,type ContinuousSidePatch,type ControlSide} from './performanceControls';
import {frameRateSetting} from './frameRateModel';
import {detectPerformanceTemplate} from './yeman';

const copy=<T>(value:T):T=>JSON.parse(JSON.stringify(value));
const stableContext=(value:ContinuousPerformanceIdentity)=>{const {revision,generation,...rest}=value;return JSON.stringify(rest);};
const ownedValue=(value:ContinuousPerformanceSnapshot)=>JSON.stringify({bank:value.bank,frame:value.side?value.frameRates[value.side]:null,smt:value.hyperThreadMode,scheme:value.powerScheme});
export function createPerformanceSimulationSession(){
 const documents=new Map<ControlSide,{base:ContinuousPerformanceSnapshot;draft:ContinuousPerformanceSnapshot;version:number}>();
 const listeners=new Set<()=>void>();let side:ControlSide|null=null,alive=true,saving=false;
 const publish=()=>{for(const listener of listeners)listener();};
 function document(){const value=side?documents.get(side):null;if(!value)throw Error('正式参数尚未载入');return value;}
 function snapshot(){const value=document(),result=copy(value.draft);result.identity={...result.identity,revision:value.base.identity.revision+'/ts-draft/'+value.version};return result;}
 async function read(requested?:ControlSide){
  if(!alive)throw Error('模拟窗口已关闭');
  if(!side||requested&&!documents.has(requested)){
   const source=await getContinuousPerformanceSnapshot('effective',requested);
   if(!alive||!source.side||!source.bank)throw Error('正在确认供电状态，请稍后重试');
   if(!documents.has(source.side))documents.set(source.side,{base:copy(source),draft:copy(source),version:0});
   side=source.side;
  }else if(requested)side=requested;
  return snapshot();
 }
 return {
  read,
  subscribe(listener:()=>void){listeners.add(listener);return()=>listeners.delete(listener);},
  current(){return side&&documents.has(side)?snapshot():null;},
  dirty(){return side&&documents.has(side)?ownedValue(document().base)!==ownedValue(document().draft):false;},
  async commit(identity:ContinuousPerformanceIdentity,patch:ContinuousSidePatch){
   const current=snapshot();if(JSON.stringify(current.identity)!==JSON.stringify(identity))throw Error('编辑目标已变化，请重新调节');
   if(patch.powerScheme!==undefined)throw Error('系统电源方案请在性能调度页面单独选择');
   const value=document(),selected=side!;value.draft.controls[selected]=patchContinuousBank(value.draft.controls[selected],patch,selected,value.draft.powerScheme);value.draft.bank=copy(value.draft.controls[selected]);
   if(patch.frameRate)value.draft.frameRates[selected]=frameRateSetting(patch.frameRate);
   if(patch.hyperThreadMode!==undefined)value.draft.hyperThreadMode=normalizeHyperThreadMode(patch.hyperThreadMode);
   value.version++;publish();return snapshot();
  },
  async restore(){
   const source=await getContinuousPerformanceSnapshot('effective',side??undefined);if(!alive||!source.side||!source.bank)throw Error('无法读取当前正式参数');
   side=source.side;documents.set(side,{base:copy(source),draft:copy(source),version:(documents.get(side)?.version??0)+1});publish();return snapshot();
  },
  async reset(){
   const value=document(),selected=side!,template=await detectPerformanceTemplate();if(!alive)throw Error('模拟窗口已关闭');
   const bank=defaultContinuousBank(selected,template);bank.tdpCeiling=Math.min(bank.tdpCeiling,value.draft.hardwareMaximum);bank.tdp=Math.max(value.draft.hardwareMinimum,Math.min(bank.tdp,bank.tdpCeiling));
   value.draft.controls[selected]=bank;value.draft.bank=copy(bank);value.draft.hyperThreadMode='default';value.draft.frameRates[selected]=frameRateSetting(selected==='dc'?60:template==='desktop'?200:120);value.version++;publish();return snapshot();
  },
  async save(){
   if(saving)throw Error('正在保存，请稍候');saving=true;
   try{const value=document(),selected=side!,current=await getContinuousPerformanceSnapshot('effective',selected);
   if(!alive)throw Error('模拟窗口已关闭');
   if(stableContext(current.identity)!==stableContext(value.base.identity)||ownedValue(current)!==ownedValue(value.base))throw Error('正式供电、游戏或设置已变化，请恢复当前设置后重新调节');
   const bank=value.draft.controls[selected],patch:ContinuousSidePatch={mode:bank.mode,tdp:bank.tdp,tdpCeiling:bank.tdpCeiling,frameRate:copy(value.draft.frameRates[selected])};
   if(bank.mode!=='manual'||current.powerScheme==='yeman'){patch.auto=copy(bank.auto);patch.manual=copy(bank.manual);patch.hyperThreadMode=value.draft.hyperThreadMode;}
   const saved=await updateContinuousPerformanceControls(current.identity,patch,()=>alive);
   if(alive){documents.set(selected,{base:copy(saved),draft:copy(saved),version:value.version+1});publish();}
   return saved;}finally{saving=false;}
  },
  dispose(){alive=false;listeners.clear();documents.clear();}
 };
}
