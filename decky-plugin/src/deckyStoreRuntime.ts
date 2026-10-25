import {DeckyStoreClient,STORE_URL,StoreError,bounded,compareVersions,type StoreConfig,type StoreVersion} from './deckyStoreClient';
export function createStoreRuntime(host:any,fetcher:typeof fetch){
  const loader=host.DeckyPluginLoader,backend=host.DeckyBackend,client=new DeckyStoreClient(fetcher);
  const controller=new AbortController(),listeners=new Set<()=>void>();let disposed=false,checking:Promise<Map<string,StoreVersion>>|null=null;
  let status={error:'',publicAPI:false,loaderVersion:'',checkedAt:0};
  const publish=(patch:Partial<typeof status>)=>{if(disposed)return;status={...status,...patch};for(const fn of listeners)fn();};
  const state=()=>loader.deckyState.publicState();
  const signature=()=>JSON.stringify({plugins:state().installedPlugins.map((p:any)=>[p.name,p.version]),frozen:state().frozenPlugins});
  async function config(signal?:AbortSignal):Promise<StoreConfig>{
    const version=state().versionInfo?.current??(await bounded(loader.updateVersion(),signal,5000))?.current;
    const store=await bounded(backend.call('utilities/settings/get','store',0),signal,5000);
    const custom=+store===2,testing=+store===1;
    const url=custom?await bounded<string>(backend.call('utilities/settings/get','store-url',STORE_URL),signal,5000):testing?'https://testing.deckbrew.xyz/plugins':STORE_URL;
    if(typeof version!=='string'||!version)throw new StoreError('version','无法读取 Decky 版本，请稍后重试。');
    return {url,version,testing,custom};
  }
  async function catalog(signal?:AbortSignal){const source=await config(signal);const result=await client.load<any>(source,false,signal);publish({error:'',publicAPI:result.publicAPI,loaderVersion:source.version,checkedAt:result.checkedAt});return {...result,config:source};}
  function checkUpdates():Promise<Map<string,StoreVersion>>{
    if(checking)return checking;
    checking=(async()=>{
      try{const identity=signature(),source=await config(controller.signal);const result=await client.load<Map<string,StoreVersion>>(source,true,controller.signal);
        if(disposed||identity!==signature()||JSON.stringify(source)!==JSON.stringify(await config(controller.signal)))return state().updates??new Map();
        const updates=new Map<string,StoreVersion>(),s=state();
        for(const p of s.installedPlugins){if(s.frozenPlugins.includes(p.name))continue;const remote=result.data.get(p.name);if(remote&&compareVersions(remote.name,p.version??'0.0.0')===1)updates.set(p.name,remote);}
        loader.deckyState.setUpdates(updates);publish({error:'',publicAPI:result.publicAPI,loaderVersion:source.version,checkedAt:result.checkedAt});return updates;
      }catch(e){if(!disposed)publish({error:e instanceof StoreError?e.message:'检查插件更新失败，请稍后重试。'});return state().updates??new Map();}
      finally{checking=null;}
    })();return checking;
  }
  return {loader,backend,state,config,catalog,checkUpdates,status:()=>status,subscribe(fn:()=>void){listeners.add(fn);return()=>listeners.delete(fn);},dispose(){disposed=true;controller.abort();listeners.clear();}};
}
export type StoreRuntime=ReturnType<typeof createStoreRuntime>;

// A backend acknowledgement alone is not installation success: some 3.2 backends return
// normally after a failed download/hash check. Require a fresh import plus matching live state.
export async function confirmInstalled(runtime:StoreRuntime,name:string,version:string,imported:()=>boolean,signal?:AbortSignal,timeout=4000){
  const matches=()=>imported()&&runtime.state().installedPlugins.some((p:any)=>p.name===name&&p.version===version);
  if(matches())return true;
  return new Promise<boolean>(resolve=>{let done=false;const finish=(value:boolean)=>{if(done)return;done=true;clearTimeout(timer);signal?.removeEventListener('abort',cancel);runtime.loader.deckyState.eventBus.removeEventListener('update',update);resolve(value);};const update=()=>{if(matches())finish(true);},cancel=()=>finish(false),timer=setTimeout(()=>finish(false),timeout);runtime.loader.deckyState.eventBus.addEventListener('update',update);signal?.addEventListener('abort',cancel,{once:true});if(signal?.aborted)cancel();else update();});
}
