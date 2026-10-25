import {installLegacyStoreReads} from './deckyLegacyStore';
import {createStoreRuntime,confirmInstalled,type StoreRuntime} from './deckyStoreRuntime';
import {browsePlugins,versionKey,installType,installLabels,bounded,StoreError,type StorePlugin,type StoreVersion} from './deckyStoreClient';
declare const SP_REACT:any;
const React=SP_REACT;
const STYLE=`
.ymcc-store{box-sizing:border-box;height:100%;padding:54px 34px 22px;color:#dcdedf;background:#18212bed;font-size:16px;overflow:auto}
.ymcc-store *{box-sizing:border-box}.ymcc-store-body{max-width:1080px;margin:auto}.ymcc-store-heading{display:flex;align-items:center;justify-content:space-between;gap:24px;margin-bottom:18px}
.ymcc-store h1{font-size:26px;font-weight:600;margin:0 0 6px}.ymcc-store-note{font-size:14px;color:#b8c2cd;line-height:1.5}.ymcc-store-toolbar{display:flex;align-items:center;gap:14px;margin-bottom:20px}.ymcc-store-search{flex:1;min-width:0}.ymcc-store-sort{width:220px}.ymcc-store-actions{display:flex;gap:10px;align-items:center}.ymcc-store .ymcc-store-actions>*{min-width:110px;width:auto}
.ymcc-store-status{padding:18px 0;line-height:1.6}.ymcc-store-error{color:#f2cba6}.ymcc-store-count{color:#aebbc8;font-size:14px;margin-bottom:6px}.ymcc-store-card{padding:16px 0;border-bottom:1px solid #ffffff16}.ymcc-store-card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:20px}.ymcc-store-card h2{font-size:20px;margin:0 0 5px;font-weight:500}.ymcc-store-description{font-size:14px;line-height:1.55;white-space:pre-wrap;overflow-wrap:anywhere;margin:10px 0;color:#c1cbd5}.ymcc-store-card-controls{display:flex;gap:14px;align-items:center}.ymcc-store-card-controls .ymcc-store-version{width:240px}.ymcc-store-badge{color:#a4cbec;font-size:14px;line-height:1.5}.ymcc-store-modal{padding:24px;max-width:620px;color:#dcdedf;line-height:1.6}.ymcc-store-modal h2{margin:0 0 16px;font-size:23px}.ymcc-store-modal .ymcc-store-actions{margin-top:24px}.ymcc-store-modal .ymcc-store-actions>*{min-width:130px;width:auto}
`;
function useRuntime(runtime:StoreRuntime){const [state,setState]=React.useState(()=>runtime.state());const [status,setStatus]=React.useState(()=>runtime.status());React.useEffect(()=>{const update=()=>setState({...runtime.state()});const changed=()=>setStatus({...runtime.status()});runtime.loader.deckyState.eventBus.addEventListener('update',update);const unsub=runtime.subscribe(changed);update();changed();return()=>{runtime.loader.deckyState.eventBus.removeEventListener('update',update);unsub();};},[runtime]);return {state,status};}
export function StoreCard({plugin,runtime,ui,state}:any){
  const {Focusable,DialogButton,Dropdown}=ui;
  const [selected,setSelected]=React.useState(()=>versionKey(plugin.versions[0]));const [notice,setNotice]=React.useState('');const [busy,setBusy]=React.useState(false);const lock=React.useRef(false),mounted=React.useRef(true);
  React.useEffect(()=>()=>{mounted.current=false;},[]);
  const version=plugin.versions.find((v:StoreVersion)=>versionKey(v)===selected)??plugin.versions[0];
  const installed=state.installedPlugins.find((p:any)=>p.name===plugin.name),type=installType(version,installed),disabled=state.disabledPlugins.some((p:any)=>p.name===plugin.name),frozen=state.frozenPlugins.includes(plugin.name);
  const request=async()=>{if(lock.current)return;lock.current=true;setBusy(true);setNotice('');let uncertain=false;
    try{await bounded(runtime.backend.call('utilities/install_plugin',version.artifact??'https://cdn.tzatzikiweeb.moe/file/steam-deck-homebrew/versions/'+version.hash+'.zip',plugin.name,version.name,version.hash,type),undefined,10000);}
    catch(e){uncertain=e instanceof StoreError&&e.code==='timeout';if(mounted.current)setNotice(uncertain?'安装请求仍在等待 Decky，请先查看确认弹窗，勿重复提交。':'无法发起安装，请稍后重试。');}
    finally{if(!uncertain){lock.current=false;if(mounted.current)setBusy(false);}}
  };
  return <div className="ymcc-store-card" data-plugin={plugin.name}>
    <div className="ymcc-store-card-head"><div><h2>{plugin.name}</h2><div className="ymcc-store-note">{plugin.author||'作者未提供'}</div></div><div className="ymcc-store-badge">{installed?'已安装 '+(installed.version??'未知版本'):'未安装'}{disabled?' · 已禁用':''}{frozen?' · 已冻结':''}</div></div>
    <div className="ymcc-store-description">{plugin.description||'暂无介绍'}</div>
    <Focusable className="ymcc-store-card-controls" flow-children="row" focusable={false}><div className="ymcc-store-version"><Dropdown menuLabel={plugin.name+' 版本'} rgOptions={plugin.versions.map((v:StoreVersion)=>({data:versionKey(v),label:v.name}))} selectedOption={versionKey(version)} onChange={(e:any)=>{setSelected(e.data);setNotice('');}}/></div><DialogButton disabled={busy} onClick={request}>{busy?'等待确认…':installLabels[type]}</DialogButton></Focusable>
    {!!notice&&<div role="alert" className="ymcc-store-note ymcc-store-error">{notice}</div>}
  </div>;
}
export function StorePage({runtime,ui}:any){
  const {Focusable,DialogButton,Dropdown,TextField}=ui;const {state,status}=useRuntime(runtime);
  const [result,setResult]=React.useState(null as any),[error,setError]=React.useState(''),[loading,setLoading]=React.useState(true),[retry,setRetry]=React.useState(0),[search,setSearch]=React.useState(''),[sort,setSort]=React.useState('name:asc');
  React.useEffect(()=>{const controller=new AbortController();setLoading(true);setError('');setResult(null);runtime.catalog(controller.signal).then((data:any)=>{if(!controller.signal.aborted)setResult(data);}).catch((e:any)=>{if(!controller.signal.aborted)setError(e instanceof StoreError?e.message:'无法连接插件商店，请稍后重试。');}).finally(()=>{if(!controller.signal.aborted)setLoading(false);});return()=>controller.abort();},[runtime,retry]);
  const plugins=React.useMemo(()=>browsePlugins(result?.data??[],search,sort),[result,search,sort]);
  const sorts=[['name:asc','名称 A → Z'],['name:desc','名称 Z → A'],['date:desc','最近更新优先'],['date:asc','较早更新优先'],['downloads:desc','下载量 从高到低'],['downloads:asc','下载量 从低到高']].map(([data,label])=>({data,label}));
  return <div className="ymcc-store"><style>{STYLE}</style><div className="ymcc-store-body">
    <div className="ymcc-store-heading"><div><h1>插件商店{result?.config.testing?' · 测试源':result?.config.custom?' · 自定义源':''}</h1><div className="ymcc-store-note">Decky {state.versionInfo?.current??status.loaderVersion??'版本读取中'}{result?.publicAPI?' · 已自动适配商店接口':''}{state.hasLoaderUpdate?' · 有可用更新':''}</div></div><Focusable className="ymcc-store-actions" flow-children="row" focusable={false}><DialogButton onClick={()=>ui.Navigation.Navigate('/decky/settings')}>Decky 设置</DialogButton><DialogButton disabled={loading} onClick={()=>setRetry((v:number)=>v+1)}>{loading?'正在连接…':'刷新目录'}</DialogButton></Focusable></div>
    <Focusable className="ymcc-store-toolbar" flow-children="row" focusable={false}><div className="ymcc-store-search"><TextField label="搜索插件" placeholder="名称、作者或关键词" value={search} onChange={(e:any)=>setSearch(e.target.value)}/></div><div className="ymcc-store-sort"><Dropdown menuLabel="排序方式" rgOptions={sorts} selectedOption={sort} onChange={(e:any)=>setSort(e.data)}/></div></Focusable>
    {loading&&<div className="ymcc-store-status" role="status">正在连接插件商店…</div>}
    {!!error&&<div className="ymcc-store-status" role="alert"><div className="ymcc-store-error">{error}</div><DialogButton onClick={()=>setRetry((v:number)=>v+1)}>重新连接</DialogButton></div>}
    {!!result&&<><div className="ymcc-store-count">{plugins.length} 个插件{result.skipped?' · 已略过 '+result.skipped+' 项无效数据':''}</div>{!plugins.length&&<div className="ymcc-store-status">{search?'没有找到匹配的插件。':'商店当前没有可用插件。'}</div>}{plugins.map((plugin:StorePlugin)=><StoreCard key={plugin.id} plugin={plugin} state={state} runtime={runtime} ui={ui}/>)}</>}
    {!!status.error&&<div className="ymcc-store-note ymcc-store-error">插件更新检查：{status.error}</div>}
  </div></div>;
}
export function SafeInstallModal({runtime,ui,artifact,version,requestId,hash,type,closeModal,onPhase,onClosed}:any){
  const {ModalRoot,Focusable,DialogButton}=ui;const {state}=useRuntime(runtime);const [phase,setPhase]=React.useState('confirm'),[error,setError]=React.useState(''),[progress,setProgress]=React.useState(null as number|null);const locked=React.useRef(false),mounted=React.useRef(true),imported=React.useRef(false),controller=React.useRef(new AbortController());
  React.useEffect(()=>{const listener=(percent:number)=>{if(mounted.current&&Number.isFinite(percent))setProgress(Math.max(0,Math.min(100,percent)));};const importListener=(name:string,loadedVersion:string)=>{if(locked.current&&name===artifact&&loadedVersion===version)imported.current=true;};runtime.backend.addEventListener('loader/plugin_download_info',listener);runtime.backend.addEventListener('loader/import_plugin',importListener);return()=>{mounted.current=false;controller.current.abort();runtime.backend.removeEventListener('loader/plugin_download_info',listener);runtime.backend.removeEventListener('loader/import_plugin',importListener);};},[runtime]);
  const installed=state.installedPlugins.some((p:any)=>p.name===artifact&&p.version===version);
  const close=()=>{onClosed?.();closeModal?.();};
  const cancel=async()=>{if(locked.current)return;if(phase!=='confirm'){close();return;}locked.current=true;setError('');try{await bounded(runtime.backend.call('utilities/cancel_plugin_install',requestId),undefined,10000);close();}catch{if(mounted.current){setError('取消请求未确认，请稍后再试。');locked.current=false;}}};
  const confirm=async()=>{if(locked.current||phase!=='confirm')return;locked.current=true;setPhase('installing');onPhase?.('running');setError('');try{await bounded(runtime.backend.call('utilities/confirm_plugin_install',requestId),controller.current.signal,60000);const verified=await confirmInstalled(runtime,artifact,version,()=>imported.current,controller.current.signal);if(mounted.current){setPhase(verified?'processed':'uncertain');if(!verified)setError('尚未检测到目标版本加载，安装未确认。请查看 Decky 日志和插件列表。');}onPhase?.('settled');void runtime.checkUpdates();}catch(e){if(mounted.current){onPhase?.('settled');setPhase('uncertain');setError(e instanceof StoreError&&e.code==='timeout'?'安装仍未确认，请查看 Decky 插件列表后再操作。':'Decky 安装失败，请查看日志和插件列表后再操作。');}}finally{locked.current=false;}};
  return <ModalRoot onCancel={cancel} closeModal={cancel}><style>{STYLE}</style><div className="ymcc-store-modal"><h2>{installLabels[type]??'安装'} {artifact}</h2><div>版本 {version}</div>{state.disabledPlugins.some((p:any)=>p.name===artifact)&&<div>此插件当前已禁用。</div>}{hash==='False'&&<div className="ymcc-store-error">此安装包没有校验值，请确认来源。</div>}{phase==='confirm'?<p>确认后由 Decky 下载并安装插件。</p>:phase==='installing'?<p role="status">Decky 正在安装{progress===null?'…':'，下载进度 '+Math.round(progress)+'%'}</p>:phase==='processed'?<p role="status">{installed?'已安装并加载此版本。':'Decky 已处理安装，等待插件加载。'}</p>:null}{error&&<p role="alert" className="ymcc-store-error">{error}</p>}
    <Focusable className="ymcc-store-actions" flow-children="row" focusable={false}><DialogButton disabled={phase==='installing'} onClick={cancel}>{phase==='confirm'?'取消':'关闭'}</DialogButton>{phase==='confirm'&&<DialogButton onClick={confirm}>确认{installLabels[type]??'安装'}</DialogButton>}</Focusable>
  </div></ModalRoot>;
}
// Decky 3.2 caches route elements as well as the route map. Keep both references in sync,
// restoring only objects still owned by us. No Steam router or backend globals are replaced. Legacy catalogue reads are adapted separately.
export function installDeckyStoreCompatibility({host,ui,fetcher}:any){
  const loader=host.DeckyPluginLoader,backend=host.DeckyBackend,router=loader?.routerHook;
  if(!loader?.deckyState?.publicState||!backend?.call||!router?.routerState?.publicState||!Array.isArray(router.routes)||typeof ui.ModalRoot!=='function')return ()=>{};
  const reported=loader.deckyState.publicState().versionInfo?.current;
  if(reported&&!/^v?3\.2\./.test(reported))return ()=>{};
  host.__YMCC_DECKY_STORE__?.dispose?.();
  const runtime=createStoreRuntime(host,fetcher??host.fetch.bind(host)),original=router.routerState.publicState().routes.get('/decky/store');
  if(!original)return ()=>runtime.dispose();
  const Page=()=> <StorePage runtime={runtime} ui={ui}/>;
  const stopLegacy=installLegacyStoreReads(host,error=>{const {ModalRoot,DialogButton}=ui;const Message=({closeModal}:any)=><ModalRoot onCancel={closeModal} closeModal={closeModal}><div style={{padding:24,maxWidth:580}}><h2>无法连接插件商店</h2><p>{error instanceof Error?error.message:'商店暂时不可用，请稍后重试。'}</p><DialogButton onClick={closeModal}>关闭</DialogButton></div></ModalRoot>;ui.showModal(<Message/>);});
  const oldCheck=loader.checkPluginUpdates,oldPrompt=loader.addPluginInstallPrompt;
  const prompts=new Map<string,any>(),routeReceipts:any[]=[];let disposed=false;
  const event='loader/add_plugin_install_prompt',eventSet=backend.eventListeners?.get?.(event),boundOriginal=eventSet&&[...eventSet].find((fn:any)=>fn.name==='bound addPluginInstallPrompt');
  if(!boundOriginal){stopLegacy();return ()=>runtime.dispose();}
  function replaceCached(list:any[]){for(const row of list){if(Array.isArray(row)){replaceCached(row);continue;}if(row?.props?.path==='/decky/store'){const before=row.props.children;const after=React.createElement(before.type,{...before.props,children:React.createElement(Page)});row.props.children=after;routeReceipts.push({row,before,after});}}}
  const check=()=>runtime.checkUpdates();
  const prompt=(artifact:string,version:string,requestId:string,hash:string,type:number)=>{if(prompts.has(requestId))return;const record:any={phase:'confirm',handle:null};prompts.set(requestId,record);try{record.handle=ui.showModal(<SafeInstallModal runtime={runtime} ui={ui} artifact={artifact} version={version} requestId={requestId} hash={hash} type={type} onPhase={(phase:string)=>record.phase=phase} onClosed={()=>prompts.delete(requestId)}/>);}catch(e){prompts.delete(requestId);void backend.call('utilities/cancel_plugin_install',requestId).catch(()=>{});throw e;}};
  const dispose=()=>{if(disposed)return;disposed=true;stopLegacy();runtime.dispose();if(loader.checkPluginUpdates===check)loader.checkPluginUpdates=oldCheck;if(loader.addPluginInstallPrompt===prompt)loader.addPluginInstallPrompt=oldPrompt;
    if(router.routerState.publicState().routes.get('/decky/store')?.component===Page)router.addRoute('/decky/store',original.component,original.props);
    for(const receipt of routeReceipts)if(receipt.row.props.children===receipt.after)receipt.row.props.children=receipt.before;
    for(const [id,record] of prompts){record.handle?.Close?.();if(record.phase==='confirm')void backend.call('utilities/cancel_plugin_install',id).catch(()=>{});}prompts.clear();
    backend.removeEventListener(event,prompt);if(!backend.eventListeners.get(event)?.has(boundOriginal))backend.addEventListener(event,boundOriginal);if(host.__YMCC_DECKY_STORE__?.dispose===dispose)delete host.__YMCC_DECKY_STORE__;
    router.routerState.notifyUpdate();
  };
  try{replaceCached(router.routes);router.addRoute('/decky/store',Page,original.props);loader.checkPluginUpdates=check;loader.addPluginInstallPrompt=prompt;backend.removeEventListener(event,boundOriginal);backend.addEventListener(event,prompt);
    host.__YMCC_DECKY_STORE__={protocol:1,status:()=>runtime.status(),dispose};
  }catch(e){dispose();console.warn('[YMCC] Store compatibility not installed');}
  return dispose;
}
