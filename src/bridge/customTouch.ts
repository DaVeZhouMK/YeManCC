import {invoke,isNativeRuntime} from './ipc';
import {CONTROLLER_SHORTCUT_ACTIONS} from './controllerShortcutRules';
import type {KeyboardOutputKey} from './controllerKeyboardSequence';
export interface CustomTouchKey {id:string;label:string;keys:KeyboardOutputKey[]}
export interface CustomTouchConfig {version:1;enabled:boolean;gamepad:boolean;template:'steamdeck'|'ps5'|'xbox';buttonScale:number;stickScale:number;keys:CustomTouchKey[];positions:Record<string,Record<string,{x:number;y:number}>>}
export interface CustomTouchState {ok:boolean;scope:'global';config:CustomTouchConfig;editing:boolean;outputReady:boolean;settingsSaved:boolean;reason?:string;error?:number}
export function customTouchDefaults():CustomTouchConfig{return{version:1,enabled:true,gamepad:false,template:'steamdeck',buttonScale:100,stickScale:100,keys:[{id:'key-1',label:'Ins',keys:[{actionId:'keyboard.rightFunction',code:'Insert'}]}],positions:{}}}
export function validCustomTouch(value:CustomTouchConfig):boolean{
  if(!value||value.version!==1||typeof value.enabled!=='boolean'||typeof value.gamepad!=='boolean'||!['steamdeck','ps5','xbox'].includes(value.template)||!Array.isArray(value.keys)||!value.positions||Array.isArray(value.positions))return false;
  if(![value.buttonScale,value.stickScale].every(n=>Number.isInteger(n)&&n>=50&&n<=150&&n%5===0))return false;
  const ids=new Set<string>();
  for(const row of value.keys){if(!row||typeof row.id!=='string'||!row.id.startsWith('key-')||row.id.length>100||ids.has(row.id)||typeof row.label!=='string'||new TextEncoder().encode(row.label).length>256||!Array.isArray(row.keys)||!row.keys.length||row.keys.length>4)return false;ids.add(row.id);
    const codes=new Set<string>();for(const key of row.keys){const action=CONTROLLER_SHORTCUT_ACTIONS.find(item=>item.id===key.actionId);if(!action?.parameters?.find(p=>p.id==='key')?.options?.some(option=>option.value===key.code))return false;codes.add(key.code);if(key.actionId==='keyboard.symbol')codes.add('ShiftLeft');}if(codes.size>4)return false;}
  return Object.entries(value.positions).every(([resolution,group])=>resolution.length<=40&&group&&typeof group==='object'&&!Array.isArray(group)&&Object.entries(group).every(([id,p])=>id.length<=100&&p&&Object.keys(p).length===2&&[p.x,p.y].every(n=>Number.isFinite(n)&&n>=0&&n<=1)));
}
let preview=customTouchDefaults(),previewEditing=false;
function memoryState():CustomTouchState{return {ok:true,scope:'global',config:structuredClone(preview),editing:previewEditing,outputReady:false,settingsSaved:false}}
export function customTouchGet():Promise<CustomTouchState>{return isNativeRuntime?invoke('customTouch.get',{}):Promise.resolve(memoryState())}
export function customTouchSet(config:CustomTouchConfig,expected:CustomTouchConfig):Promise<CustomTouchState>{
  if(!validCustomTouch(config))return Promise.reject(new Error('自定义触控设置无效'));
  if(isNativeRuntime)return invoke('customTouch.set',{config:structuredClone(config),expected:structuredClone(expected)});
  if(JSON.stringify(preview)!==JSON.stringify(expected)||previewEditing)return Promise.reject(new Error('配置已变化，请重新打开页面'));
  preview=structuredClone(config);return Promise.resolve({...memoryState(),settingsSaved:true});
}
export function customTouchEdit(action:'begin'|'cancel'):Promise<CustomTouchState>{
  if(isNativeRuntime)return invoke('customTouch.edit',{action});
  previewEditing=action==='begin';return Promise.resolve(memoryState());
}
