import {DeckyStoreClient,bounded} from './deckyStoreClient';
// Some 3.2 settings actions hold a lexical reference to getPluginList. Adapt only
// their versioned, official, read-only catalogue fetches; all other fetches pass through.
export function installLegacyStoreReads(host:any,onError:(error:unknown)=>void){
  const original=host.fetch;if(typeof original!=='function')return ()=>{};const backend=host.DeckyBackend,loader=host.DeckyPluginLoader;
  if(!backend?.call||!loader?.deckyState?.publicState)return ()=>{};
  const client=new DeckyStoreClient((...args:any[])=>original.apply(host,args));
  let disposed=false;
  const replacement=async(input:any,init:any={})=>{
    let url:URL,headers:Headers;
    try{url=new URL(typeof input==='string'?input:input?.url??String(input));headers=new Headers(init.headers??input?.headers);}catch{return original.call(host,input,init);}
    const version=headers.get('X-Decky-Version'),current=loader.deckyState.publicState().versionInfo?.current,method=String(init.method??input?.method??'GET').toUpperCase();
    if(disposed||method!=='GET'||!/^v?3\.2\./.test(current??'')||version!==current||url.protocol!=='https:'||!['plugins.deckbrew.xyz','testing.deckbrew.xyz'].includes(url.host)||!['/plugins','/plugins/versions'].includes(url.pathname))return original.call(host,input,init);
    try{const store=Number(await bounded(backend.call('utilities/settings/get','store',0),init.signal??input?.signal,5000));
      // Explicit custom stores retain their own version requirements, even on an official hostname.
      if(store===2||store===1&&url.host!=='testing.deckbrew.xyz'||store!==1&&url.host!=='plugins.deckbrew.xyz')return original.call(host,input,init);
      const versions=url.pathname.endsWith('/versions'),base=new URL(url.href);if(versions)base.pathname='/plugins';
      const result=await client.load<any>({url:base.href,version:current,testing:store===1,custom:false},versions,init.signal??input?.signal);
      const data=versions?Object.fromEntries([...result.data].map(([name,v]:any)=>[name,[v.name,v.hash,...(v.artifact?[v.artifact]:[])]])):result.data;
      return new Response(JSON.stringify(data),{status:200,headers:{'Content-Type':'application/json'}});
    }catch(e){if(!disposed&&!(init.signal??input?.signal)?.aborted){try{onError(e);}catch{}}throw e;}
  };
  host.fetch=replacement;
  return()=>{if(disposed)return;disposed=true;if(host.fetch===replacement)host.fetch=original;};
}
