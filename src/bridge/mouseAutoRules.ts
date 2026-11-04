import {invoke} from './ipc';
export type MouseRuleKind='blacklist'|'whitelist';
export interface MouseAutoRules {blacklist:string[];whitelist:string[]}
async function read(command:string,args:object={}):Promise<MouseAutoRules>{
  const raw=await invoke<MouseAutoRules & {ok?:boolean;error?:string}>(command,args);
  if(raw?.ok===false||!Array.isArray(raw?.blacklist)||!Array.isArray(raw?.whitelist))throw Error(raw?.error||'鼠标名单读取失败');
  return {blacklist:raw.blacklist,whitelist:raw.whitelist};
}
export const getMouseAutoRules=()=>read('mouseMode.rules.get');
export const setMouseAutoRuleList=(kind:MouseRuleKind,values:string[])=>read('mouseMode.rules.set',{[kind]:values});
