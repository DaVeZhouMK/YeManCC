// Public catalogue compatibility only. Never changes the reported Loader version or installs a binary.
export interface StoreVersion {name:string;hash:string;artifact?:string;created?:string;downloads?:number}
export interface StorePlugin {id:string;name:string;author:string;description:string;tags:string[];versions:StoreVersion[];downloads:number;updated:string}
export interface StoreConfig {url:string;version:string;testing:boolean;custom:boolean}
export interface StoreResult<T> {data:T;publicAPI:boolean;checkedAt:number;skipped:number}
export class StoreError extends Error {constructor(public code:string,message:string){super(message);this.name='StoreError';}}
export const STORE_URL='https://plugins.deckbrew.xyz/plugins';
const SHA=/^[a-f0-9]{64}$/i;
const object=(v:any):v is Record<string,any>=>!!v&&typeof v==='object'&&!Array.isArray(v);
const text=(v:any,max=12000)=>typeof v==='string'?v.slice(0,max):'';
// Catalogue labels are later sent to installation APIs; never truncate two
// different identities into one name or accept a path/control-character name.
export const validStorePluginName=(v:unknown):v is string=>typeof v==='string'&&v.length>0&&v.length<=160&&v.trim()===v&&!/[\\/\x00-\x1f\x7f]/.test(v)&&v!=='.'&&v!=='..';
export const versionKey=(v:StoreVersion)=>v.name+'|'+v.hash;
export function normalizeVersion(v:any):StoreVersion|null {
  if(!object(v)||!text(v.name,160)||!SHA.test(v.hash))return null;
  let artifact:string|undefined;
  if(v.artifact!=null){try{const u=new URL(v.artifact);if(u.protocol!=='https:'||u.username||u.password)return null;artifact=u.href;}catch{return null;}}
  return {name:v.name.slice(0,160),hash:v.hash.toLowerCase(),...(artifact?{artifact}:{}),created:text(v.created,80),downloads:Number.isFinite(v.downloads)?Math.max(0,v.downloads):0};
}
export function normalizeCatalog(raw:any):{data:StorePlugin[];skipped:number}{
  if(!Array.isArray(raw)||raw.length>10000)throw new StoreError('schema','商店返回的数据格式已变化，请更新 YMCC 后重试。');
  const data:StorePlugin[]=[],seen=new Set<string>();let skipped=0;
  for(const row of raw){
    if(!object(row)||!validStorePluginName(row.name)||!Array.isArray(row.versions)||row.versions.length>512||seen.has(row.name)){skipped++;continue;}
    if(row.visible===false)continue;
    const keys=new Set<string>(),versions:StoreVersion[]=[];
    for(const source of row.versions){const v=normalizeVersion(source);if(v&&!keys.has(versionKey(v))){keys.add(versionKey(v));versions.push(v);}}
    if(!versions.length){skipped++;continue;}
    seen.add(row.name);data.push({id:row.name,name:row.name.slice(0,160),author:text(row.author,200),description:text(row.description),tags:Array.isArray(row.tags)?row.tags.filter((x:any)=>typeof x==='string').slice(0,100):[],versions,downloads:Number.isFinite(row.downloads)?Math.max(0,row.downloads):0,updated:text(row.updated??row.created,80)});
  }
  if(raw.length&&!data.length)throw new StoreError('schema','商店没有可用的插件版本，请稍后重试或更新 YMCC。');
  return {data,skipped};
}
export function normalizeVersions(raw:any):{data:Map<string,StoreVersion>;skipped:number}{
  if(!object(raw)||Object.keys(raw).length>10000)throw new StoreError('schema','商店更新目录格式已变化，请更新 YMCC 后重试。');
  const data=new Map<string,StoreVersion>();let skipped=0;
  for(const [name,row]of Object.entries(raw)){const v=Array.isArray(row)&&normalizeVersion({name:row[0],hash:row[1],artifact:row[2]});if(validStorePluginName(name)&&v)data.set(name,v);else skipped++;}
  if(skipped)throw new StoreError('schema','商店更新目录不完整，已保留上次检查结果。');
  return {data,skipped};
}
function semver(v:string){const m=/^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/.exec(v);if(!m)return null;const pre=m[4]?.split('.')??[];if(pre.some(x=>/^\d+$/.test(x)&&x.length>1&&x[0]==='0'))return null;return {core:m.slice(1,4),pre};}
const integerCompare=(a:string,b:string)=>a.length-b.length||(a<b?-1:a>b?1:0);
export function compareVersions(a:string,b:string):number|null {const x=semver(a),y=semver(b);if(!x||!y)return null;for(let i=0;i<3;i++){const c=integerCompare(x.core[i],y.core[i]);if(c)return Math.sign(c);}if(!x.pre.length||!y.pre.length)return x.pre.length===y.pre.length?0:x.pre.length?-1:1;for(let i=0;i<Math.max(x.pre.length,y.pre.length);i++){const p=x.pre[i],q=y.pre[i];if(p===undefined)return -1;if(q===undefined)return 1;if(p===q)continue;const pn=/^\d+$/.test(p),qn=/^\d+$/.test(q);return pn&&qn?Math.sign(integerCompare(p,q)):pn?-1:qn?1:p<q?-1:1;}return 0;}
// Matches Decky's install type contract, including a locally installed version absent from the store.
export function installType(selected:StoreVersion,installed?:{version?:string}){if(!installed)return 0;if(selected.name===installed.version)return 1;const cmp=compareVersions(selected.name,installed.version??'');return cmp===null||cmp===0?4:cmp>0?2:3;}
export const installLabels=['安装','重新安装','更新','降级','覆盖安装'];
const searchText=(s:string)=>s.normalize('NFKC').toLocaleLowerCase().replace(/\s+/g,'');
export function browsePlugins(plugins:StorePlugin[],search:string,sort:string){const q=searchText(search);const list=plugins.filter(p=>!q||[p.name,p.author,p.description,...p.tags].some(x=>searchText(x).includes(q)));const [key,dir]=sort.split(':');const sign=dir==='desc'?-1:1;return list.sort((a,b)=>{const cmp=key==='downloads'?a.downloads-b.downloads:key==='date'?(Date.parse(a.updated)||0)-(Date.parse(b.updated)||0):a.name.localeCompare(b.name,'en',{sensitivity:'base',numeric:true});return sign*cmp||a.name.localeCompare(b.name,'en');});}
export function storeEndpoint(config:StoreConfig,versions=false){let u:URL;try{u=new URL(config.url);}catch{throw new StoreError('config','自定义商店地址无效，请在 Decky 设置中修改。');}if(!['https:','http:'].includes(u.protocol)||u.username||u.password)throw new StoreError('config','商店地址无效。');u.hash='';if(versions)u.pathname=u.pathname.replace(/\/$/,'')+'/versions';return u;}
function official(u:URL,custom:boolean){return !custom&&u.protocol==='https:'&&['plugins.deckbrew.xyz','testing.deckbrew.xyz'].includes(u.host)&&['/plugins','/plugins/versions'].includes(u.pathname);}
export function bounded<T>(promise:Promise<T>,signal?:AbortSignal,timeout=10000):Promise<T>{return new Promise((resolve,reject)=>{let settled=false;const finish=(fn:any,v:any)=>{if(settled)return;settled=true;clearTimeout(timer);signal?.removeEventListener('abort',cancel);fn(v);};const cancel=()=>finish(reject,new StoreError('cancelled','请求已取消。'));const timer=setTimeout(()=>finish(reject,new StoreError('timeout','连接商店超时，请检查网络后重试。')),timeout);signal?.addEventListener('abort',cancel,{once:true});if(signal?.aborted)cancel();promise.then(v=>finish(resolve,v),e=>finish(reject,e));});}
export class DeckyStoreClient {
  private negotiated=new Map<string,number>();
  constructor(private fetcher:typeof fetch,private timeout=10000,private now=()=>Date.now()){}
  async load<T>(config:StoreConfig,versions:boolean,signal?:AbortSignal):Promise<StoreResult<T>>{
    if(!config.version)throw new StoreError('version','无法读取 Decky 版本，请稍后重试。');
    const url=storeEndpoint(config,versions),key=url.origin+'|'+config.version;
    const controller=new AbortController(),abort=()=>controller.abort();signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)abort();
    const limit=4*1024*1024;
    const read=async(r:Response)=>{if(Number(r.headers.get('content-length')||0)>limit)throw new StoreError('size','商店响应过大，请稍后重试。');if(!r.body)return '';const reader=r.body.getReader();const chunks:Uint8Array[]=[];let bytes=0;try{while(true){const v=await reader.read();if(v.done)break;bytes+=v.value.byteLength;if(bytes>limit)throw new StoreError('size','商店响应过大，请稍后重试。');chunks.push(v.value);}const all=new Uint8Array(bytes);let offset=0;for(const c of chunks){all.set(c,offset);offset+=c.length;}return new TextDecoder().decode(all);}finally{void reader.cancel().catch(()=>{});reader.releaseLock();}};
    const perform=async()=>{
      let publicAPI=official(url,config.custom)&&(this.negotiated.get(key)??0)>this.now();
      const request=()=>this.fetcher(url.href,{method:'GET',headers:publicAPI?{Accept:'application/json'}:{Accept:'application/json','X-Decky-Version':config.version},signal:controller.signal,credentials:'omit',cache:'no-store'});
      let response=await request(),body=await read(response);
      // The official public directory is available to clients without a Loader version. Do not bypass custom-store requirements.
      if(!publicAPI&&response.status===426&&official(url,config.custom)&&body.trim()==='Unsupported loader version, please upgrade.'){
        publicAPI=true;response=await request();body=await read(response);
      }
      if(!response.ok)throw new StoreError('http-'+response.status,response.status===426?'商店要求更新 Decky，请通过 YMCC 更新兼容版本。':response.status===429?'商店请求过于频繁，请稍后手动重试。':'商店暂时无法连接（HTTP '+response.status+'），请稍后重试。');
      if(!/application\/(?:[\w.+-]+\+)?json\b/i.test(response.headers.get('content-type')??''))throw new StoreError('content','商店返回了网页或验证页面，请检查网络后重试。');
      let raw:any;try{raw=JSON.parse(body);}catch{throw new StoreError('json','商店返回的数据不完整，请重试。');}
      const normalized=versions?normalizeVersions(raw):normalizeCatalog(raw);
      if(publicAPI)this.negotiated.set(key,this.now()+300000);
      return {...normalized,data:normalized.data as T,publicAPI,checkedAt:this.now()};
    };
    try{return await bounded(perform(),controller.signal,this.timeout);}catch(e){if(e instanceof StoreError)throw e;throw new StoreError(signal?.aborted?'cancelled':'network',signal?.aborted?'请求已取消。':'无法连接插件商店，请检查网络或代理后重试。');}
    finally{controller.abort();signal?.removeEventListener('abort',abort);}
  }
}
