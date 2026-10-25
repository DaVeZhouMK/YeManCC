/** Optional browser bridge for the registered four-mode real-file consumer. */
const call=async(operation:string,...args:any[])=>{
  if(typeof (window as any).__bootFileBackend!=='function')throw Error('No isolated file backend');
  (window as any).__bootFileCalls??=[];(window as any).__bootFileCalls.push(operation);
  const result=await (window as any).__bootFileBackend(operation,args);
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
