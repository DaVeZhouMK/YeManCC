/** Lightweight fan state for the output window. Device ownership stays in YMCC. */
export const RADIAL_FAN_CHOICES = [
  {value:'off',label:'关闭'}, {value:'soft',label:'轻柔'},
  {value:'balanced',label:'均衡'}, {value:'aggressive',label:'强劲'},
] as const;
export type RadialFanChoice = typeof RADIAL_FAN_CHOICES[number]['value'];
export interface RadialFanFrame {
  sourceRevision:string; available:boolean; busy:boolean; choice:RadialFanChoice;
  active:boolean; pending:boolean; confirmed:boolean; notice:string;
}
export interface RadialFanIntent { choice:RadialFanChoice; sourceRevision:string; }
export const emptyRadialFan = ():RadialFanFrame => ({sourceRevision:'',available:false,busy:false,choice:'off',active:false,pending:false,confirmed:false,notice:'正在读取风扇状态'});
export function isRadialFanChoice(value:unknown):value is RadialFanChoice {
  return RADIAL_FAN_CHOICES.some(choice=>choice.value===value);
}
export function normalizeRadialFan(raw:unknown):RadialFanFrame {
  if(!raw||typeof raw!=='object'||Array.isArray(raw))return emptyRadialFan();
  const value=raw as RadialFanFrame;
  if(typeof value.sourceRevision!=='string'||value.sourceRevision.length>100||!isRadialFanChoice(value.choice)
    ||!['available','busy','active','pending','confirmed'].every(key=>typeof (raw as Record<string,unknown>)[key]==='boolean')
    ||typeof value.notice!=='string'||value.notice.length>500||value.active===(value.choice==='off')
    ||value.confirmed&&(!value.active||value.pending))return emptyRadialFan();
  return {sourceRevision:value.sourceRevision,available:value.available,busy:value.busy,choice:value.choice,
    active:value.active,pending:value.pending,confirmed:value.confirmed,notice:value.notice};
}
export const fanControlId=(parent:string,choice:RadialFanChoice)=>'@fan/'+encodeURIComponent(parent)+'/'+choice;
export function fanChoiceText(frame:RadialFanFrame,choice:RadialFanChoice):string {
  if(frame.choice!==choice)return '';
  return frame.pending?'等待确认':frame.confirmed?'已确认':'已选择';
}
