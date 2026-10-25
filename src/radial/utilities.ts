/** Allowlisted lightweight utility choices; no settings/process owner in output. */
export type RadialUtilityGroup='monitor.templates'|'steam.controls';
export const UTILITY_OPTIONS={
 'monitor.templates':[{value:'A',label:'模板 A'},{value:'B',label:'模板 B'}],
 'steam.controls':[{value:'bigpicture',label:'Steam 大屏'},{value:'start',label:'开启 Steam'},{value:'stop',label:'关闭 Steam'}],
} as const;
export const isUtilityGroup=(value:unknown):value is RadialUtilityGroup=>value==='monitor.templates'||value==='steam.controls';
export interface RadialUtilityChoice {value:string;label:string;enabled:boolean}
export interface RadialUtilityState {sourceRevision:string;available:boolean;busy:boolean;pending:boolean;selected:string;notice:string;choices:RadialUtilityChoice[]}
export type RadialUtilities=Record<RadialUtilityGroup,RadialUtilityState>;
export interface RadialUtilityIntent {choice:string;sourceRevision:string}
export function emptyUtility(group:RadialUtilityGroup):RadialUtilityState{return {sourceRevision:'',available:false,busy:false,pending:false,selected:'',notice:'正在读取当前状态',choices:UTILITY_OPTIONS[group].map(choice=>({...choice,enabled:false}))};}
export const emptyRadialUtilities=():RadialUtilities=>({'monitor.templates':emptyUtility('monitor.templates'),'steam.controls':emptyUtility('steam.controls')});
export const utilityControlId=(parent:string,choice:string)=>'@utility/'+encodeURIComponent(parent)+'/'+choice;
export function normalizeRadialUtilities(raw:unknown):RadialUtilities {
 const result=emptyRadialUtilities();if(!raw||typeof raw!=='object'||Array.isArray(raw))return result;
 for(const group of Object.keys(UTILITY_OPTIONS) as RadialUtilityGroup[]){const value=(raw as RadialUtilities)[group],allowed=UTILITY_OPTIONS[group];
  if(!value||typeof value.sourceRevision!=='string'||value.sourceRevision.length>100||typeof value.notice!=='string'||value.notice.length>500||typeof value.selected!=='string'||value.selected!==''&&!allowed.some(choice=>choice.value===value.selected)
   ||!['available','busy','pending'].every(key=>typeof (value as unknown as Record<string,unknown>)[key]==='boolean')||!Array.isArray(value.choices)||value.choices.length!==allowed.length)continue;
  if(value.choices.some((choice,index)=>choice.value!==allowed[index].value||choice.label!==allowed[index].label||typeof choice.enabled!=='boolean'))continue;
  result[group]={sourceRevision:value.sourceRevision,available:value.available,busy:value.busy,pending:value.pending,selected:value.selected,notice:value.notice,choices:value.choices.map(choice=>({...choice}))};
 }
 return result;
}
