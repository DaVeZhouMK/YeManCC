/** Optional browser bridge for the registered four-mode real-file consumer. */
import { nextTick } from 'vue';
const pending=new Set<Promise<unknown>>();
// Synchronize original gamepad actions with the actual isolated backend, rather
// than making every following locator discover completion on a later retry.
(window as any).__bootFileAfterIO=async()=>{
  let timer:ReturnType<typeof setTimeout>|undefined;
  try{
    await Promise.race([
      (async()=>{do{await Promise.allSettled([...pending]);await nextTick();}while(pending.size);})(),
      new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Isolated file UI operation exceeded its original 2500ms action budget')),2500);}),
    ]);
  }finally{clearTimeout(timer);}
};
const call=async(operation:string,...args:any[])=>{
  if(typeof (window as any).__bootFileBackend!=='function')throw Error('No isolated file backend');
  (window as any).__bootFileCalls??=[];(window as any).__bootFileCalls.push(operation);
  const request=Promise.resolve((window as any).__bootFileBackend(operation,args));pending.add(request);
  let result:any;try{result=await request;}finally{pending.delete(request);}
  if(operation==='loadBootModes')(window as any).__bootFileDocument=result.document;
  if(operation==='chooseBootMode'||operation==='saveBootModes')(window as any).__bootFileDocument=result;
  return result;
};
export const loadBootModes=(options:any={})=>call('loadBootModes',options);
export const chooseBootMode=(...args:any[])=>call('chooseBootMode',...args);
export const saveBootModes=(...args:any[])=>call('saveBootModes',...args);
export const readBootModeSupport=(...args:any[])=>call('readBootModeSupport',...args);
export const bootModeConnections=(...args:any[])=>call('bootModeConnections',...args);
export const addBootSharedApplication=(...args:any[])=>call('addBootSharedApplication',...args);
export const applyBootMode=()=>{throw Error('UI must never hot-apply the boot scene');};
