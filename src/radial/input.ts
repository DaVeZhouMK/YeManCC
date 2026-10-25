/** Lightweight input choices. The output window never imports an input owner. */
import { DECKY_PAD_CHOICES, DECKY_GYRO_CHOICES } from '@/bridge/deckyInputOptions';
export type RadialInputGroup='input.pad'|'input.gyro';
export type RadialInputField='padPersona'|'gyroEnabled'|'gyroPreset';
export const RADIAL_INPUT_OPTIONS={padPersona:DECKY_PAD_CHOICES,gyroEnabled:[{data:'off',label:'关闭'},{data:'on',label:'开启'}],gyroPreset:DECKY_GYRO_CHOICES} as const;
export interface RadialInputChoice {value:string;label:string;enabled:boolean}
export interface RadialInputControl {key:RadialInputField;value:string;enabled:boolean;choices:RadialInputChoice[]}
export interface RadialInputFrame {sourceRevision:string;scope:'unavailable'|'global'|'game';label:string;available:boolean;busy:boolean;pending:boolean;notice:string;controls:RadialInputControl[]}
export interface RadialInputIntent {field:RadialInputField;value:string;sourceRevision:string}
export const emptyRadialInput=():RadialInputFrame=>({sourceRevision:'',scope:'unavailable',label:'输入控制',available:false,busy:false,pending:false,notice:'正在读取输入设置',controls:[]});
export const isRadialInputField=(value:unknown):value is RadialInputField=>value==='padPersona'||value==='gyroEnabled'||value==='gyroPreset';
export function inputFieldsFor(group:RadialInputGroup):RadialInputField[]{return group==='input.pad'?['padPersona']:['gyroEnabled','gyroPreset'];}
export function inputControlId(parent:string,field:RadialInputField,value:string){return '@input/'+encodeURIComponent(parent)+'/'+field+'/'+value;}
export function inputChoicesFor(group:RadialInputGroup,frame?:RadialInputFrame){return inputFieldsFor(group).flatMap(key=>{const control=frame?.controls.find(control=>control.key===key);return control?control.choices.map(choice=>({control,choice})):[]});}
export function normalizeRadialInput(raw:unknown):RadialInputFrame {
 if(!raw||typeof raw!=='object'||Array.isArray(raw))return emptyRadialInput();const value=raw as RadialInputFrame;
 if(typeof value.sourceRevision!=='string'||value.sourceRevision.length>100||!['unavailable','global','game'].includes(value.scope)
  ||typeof value.label!=='string'||value.label.length>120||typeof value.notice!=='string'||value.notice.length>500
  ||!['available','busy','pending'].every(key=>typeof (raw as Record<string,unknown>)[key]==='boolean')||!Array.isArray(value.controls)||value.controls.length!==3)return emptyRadialInput();
 const keys=new Set<string>();
 for(const control of value.controls){
  if(!isRadialInputField(control.key)||keys.has(control.key)||typeof control.enabled!=='boolean'||typeof control.value!=='string'||control.value.length>100||!Array.isArray(control.choices))return emptyRadialInput();keys.add(control.key);
  const allowed=RADIAL_INPUT_OPTIONS[control.key];if(control.choices.length!==allowed.length)return emptyRadialInput();
  for(let n=0;n<allowed.length;n++){const choice=control.choices[n];if(choice.value!==allowed[n].data||typeof choice.enabled!=='boolean'||typeof choice.label!=='string'||choice.label.length>100)return emptyRadialInput();}
 }
 return {...value,controls:value.controls.map(control=>({...control,choices:control.choices.map(choice=>({...choice}))}))};
}

export function inputChoiceLabel(field:RadialInputField,value:string):string {return field==='padPersona'&&value==='disabled'?'关闭':RADIAL_INPUT_OPTIONS[field].find(choice=>choice.data===value)?.label??'不可用';}
