/** YMCC-owned model. Polar selection/hysteresis references StarPie MIT 248b7236.
 * See docs/third-party/StarPie-MIT.txt. No WPF/StarPie process is required. */
export type RadialAction = 'ymcc.show' | 'tdp' | 'performance' | 'performance.control' | 'fan' | 'fan.control' | 'input.pad' | 'input.gyro' | 'input.control' | 'monitor.templates' | 'steam.controls' | 'utility.control' | 'page.schedule' | 'page.fan' | 'page.monitor' | 'page.settings' | 'page.radial' | 'windows.thisPc' | 'windows.browser' | 'windows.taskView' | 'submenu' | 'none';
export interface RadialItem { id: string; label: string; action: RadialAction; enabled: boolean; children?: RadialItem[] }
export interface RadialSettings {
  schemaVersion: 1; revision: number; enabled: boolean; monitorHold: boolean; holdMs: number;
  fullScreenHold: boolean; feedback: boolean; pages: { id: string; name: string; items: RadialItem[] }[];
}
export const ACTIONS: { value: RadialAction; label: string }[] = [
  {value:'ymcc.show',label:'呼出 YMCC'}, {value:'tdp',label:'TDP 转盘'}, {value:'performance',label:'性能调度控件'},
  {value:'monitor.templates',label:'监控模板 A/B'}, {value:'steam.controls',label:'Steam 菜单'}, {value:'input.pad',label:'虚拟手柄'}, {value:'input.gyro',label:'陀螺仪控制'}, {value:'fan',label:'风扇档位'}, {value:'submenu',label:'二级菜单'}, {value:'page.schedule',label:'性能调度'}, {value:'page.fan',label:'风扇控制'},
  {value:'page.monitor',label:'监控设置'}, {value:'page.settings',label:'YMCC 设置'}, {value:'page.radial',label:'轮盘设置'},
  {value:'windows.thisPc',label:'我的电脑'}, {value:'windows.browser',label:'默认浏览器'}, {value:'windows.taskView',label:'切换程序'}, {value:'none',label:'留空'},
];
const item=(id:string,label:string,action:RadialAction,children?:RadialItem[]):RadialItem=>({id,label,action,enabled:true,...(children?{children}:{})});
export function defaultRadialSettings(): RadialSettings {
  return {schemaVersion:1,revision:0,enabled:false,monitorHold:false,holdMs:650,fullScreenHold:false,feedback:false,pages:[{id:'main',name:'常用',items:[
    item('ymcc','呼出 YMCC','ymcc.show'), item('windows','Windows','submenu',[
      item('thisPc','我的电脑','windows.thisPc'),item('browser','浏览器','windows.browser'),item('taskView','切换程序','windows.taskView')]),
    item('monitor','监控','monitor.templates'),item('virtualPad','虚拟手柄','input.pad'),item('gyro','陀螺仪','input.gyro'),
    item('pages','性能调度','performance'),
    item('fanControls','风扇','fan'),item('steam','Steam','steam.controls'),
  ]}]};
}
export function normalizeRadialSettings(raw:unknown):RadialSettings {
  const base=defaultRadialSettings(); if(!raw||typeof raw!=='object')return base;
  const r=raw as Partial<RadialSettings>; const ids=new Set<string>();
  const normalizeItem=(v:unknown,child=false):RadialItem|null=>{
    if(!v||typeof v!=='object')return null;const p=v as RadialItem;
    if(typeof p.id!=='string'||!p.id||p.id.length>100||ids.has(p.id)||!ACTIONS.some(a=>a.value===p.action))return null;
    ids.add(p.id); const children=!child&&Array.isArray(p.children)?p.children.slice(0,3).map(v=>normalizeItem(v,true)).filter((v):v is RadialItem=>!!v):undefined;
    return {id:p.id,label:typeof p.label==='string'?p.label.slice(0,20):'未命名',action:child&&(p.action==='submenu'||p.action==='performance'||p.action==='fan'||p.action==='input.pad'||p.action==='input.gyro'||p.action==='monitor.templates'||p.action==='steam.controls')?'none':p.action,enabled:p.enabled!==false,...(children?{children}:{})};
  };
  const pages=Array.isArray(r.pages)?r.pages.slice(0,4).map((p,i)=>({id:typeof p.id==='string'?p.id.slice(0,80):`page-${i}`,name:typeof p.name==='string'?p.name.slice(0,16):`菜单 ${i+1}`,items:Array.isArray(p.items)?p.items.slice(0,8).map(v=>normalizeItem(v)).filter((v):v is RadialItem=>!!v):[]})).filter(p=>p.items.length===8):[];
  return {...base,revision:Number.isSafeInteger(r.revision)&&r.revision!>=0?r.revision!:0,enabled:r.enabled===true,monitorHold:r.monitorHold===true,
    holdMs:typeof r.holdMs==='number'&&Number.isFinite(r.holdMs)?Math.max(400,Math.min(1500,Math.round(r.holdMs))):650,fullScreenHold:r.fullScreenHold===true,feedback:r.feedback===true,pages:pages.length?pages:base.pages};
}
export function findRadialItem(settings:RadialSettings,id:string):RadialItem|undefined {
  for(const page of settings.pages)for(const entry of page.items){if(entry.id===id)return entry;const child=entry.children?.find(c=>c.id===id);if(child)return child;}return undefined;
}
export interface TdpDraft { available:boolean; min:number; max:number; value:number; label:string; reason?:string }
export const emptyTdp=():TdpDraft=>({available:false,min:0,max:0,value:0,label:'读取中',reason:'正在读取当前性能策略'});
