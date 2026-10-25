import {supportsOfficialUpdaterConsumer} from './deckyLoaderConsumer';
import {DeckyUpdaterClient} from './deckyUpdaterClient';
import {type UpdaterView} from './deckyUpdaterProtocol';
// Single consumer update path: the existing YMCC package updater owns Decky too.
// Never call Decky updater/do_update or fetch independent releases from this product UI.
declare const SP_REACT:any;
const React=SP_REACT;
export function CompatibleUpdateConfirm({updater,target,ui,closeModal}:any){
  const {ModalRoot,Focusable,DialogButton}=ui;const [notice,setNotice]=React.useState(''),[busy,setBusy]=React.useState(false);const lock=React.useRef(false),mounted=React.useRef(true);
  React.useEffect(()=>()=>{mounted.current=false;},[]);
  const cancel=()=>{if(!lock.current)closeModal?.();};
  const confirm=async()=>{if(lock.current)return;lock.current=true;setBusy(true);setNotice('');try{await updater.request('install',target);closeModal?.();}catch(e){if(mounted.current)setNotice(e instanceof Error?e.message:'安装请求失败，请查看 YMCC。');}finally{lock.current=false;if(mounted.current)setBusy(false);}};
  return <ModalRoot onCancel={cancel} closeModal={cancel}><div style={{padding:24,maxWidth:600}}><h2>安装 YMCC 更新</h2><p>YMCC {target.targetVersion}，Decky 将随本次 YMCC 更新。</p><p>更新包已由 YMCC 校验。安装会关闭并重新打开 YMCC，请先退出正在运行的游戏。</p>{notice&&<p role="alert">{notice}</p>}<Focusable flow-children="row" focusable={false} style={{display:'flex',gap:12}}><DialogButton disabled={busy} onClick={cancel}>取消</DialogButton><DialogButton disabled={busy} onClick={confirm}>{busy?'正在确认…':'确认安装并重启 YMCC'}</DialogButton></Focusable></div></ModalRoot>;
}
export function YmccPackageUpdateRow({host,ui,cache,client}:any){
  const {Field,DialogButton,Focusable}=ui;const updater=React.useMemo(()=>new DeckyUpdaterClient(client,host.crypto.randomUUID()),[client]);const [state,setState]=React.useState(()=>updater.snapshot());const [error,setError]=React.useState('');const mounted=React.useRef(true);
  React.useEffect(()=>{const off=updater.subscribe(()=>setState({...updater.snapshot()}));updater.open();setState({...updater.snapshot()});return()=>{mounted.current=false;off();updater.close();};},[updater]);
  const view=state.view as UpdaterView|null;
  const act=(action:'check'|'download')=>{setError('');void updater.request(action,view??undefined).catch(e=>{if(mounted.current)setError(e.message);});};
  React.useEffect(()=>{if(!view?.canCheck||view.targetVersion||state.pending)return;const key='compat:'+updater.session(),now=Date.now();if((cache.get(key)??0)>now)return;cache.set(key,now+300000);act('check');},[view?.canCheck,view?.targetVersion,updater,state.pending]);
  const install=()=>{if(view?.canInstall)ui.showModal(<CompatibleUpdateConfirm updater={updater} target={{...view}} ui={ui}/>);};
  const busy=state.pending||!!view&&!view.canCheck&&!view.canDownload&&!view.canInstall;
  const label=!view?'等待 YMCC':view.canInstall?'安装并重启 YMCC':view.canDownload?'下载更新':view.phase==='downloading'||view.phase==='validating'?'正在下载并校验':view.phase==='installing'?'正在安装…':state.pending||view.phase==='checking'?'检查中…':'检查更新';
  const description=state.error||error||view?.message||(!state.connected?'等待 YMCC 更新器连接…':'正在读取更新状态…');
  return <Focusable focusable={false} flow-children="column" onCancel={()=>{ui.Navigation.NavigateBack();return true;}}><Field label="YMCC 更新" description={<div role="status"><div>{view?'YMCC '+view.hostVersion+' · ':''}Decky {host.DeckyPluginLoader.deckyState.publicState().versionInfo?.current??''}</div><div>{description}</div><div>Decky 随 YMCC 一起更新。</div>{view?.targetVersion&&<div>可更新至 {view.targetVersion}</div>}{view?.gameRunning&&<div>游戏运行中，可下载，退出游戏后安装。</div>}{view&&['downloading','validating'].includes(view.phase)&&view.percent!==null&&<div>进度 {Math.round(view.percent)}%</div>}</div>} childrenContainerWidth="fixed"><Focusable focusable={false} flow-children="column" style={{display:'flex',flexDirection:'column',gap:8}}><DialogButton disabled={!view||busy} onClick={()=>view?.canInstall?install():act(view?.canDownload?'download':'check')}>{label}</DialogButton>{view&&(view.canDownload||view.canInstall)&&<DialogButton disabled={state.pending||!view.canCheck} onClick={()=>act('check')}>重新检查</DialogButton>}</Focusable></Field></Focusable>;
}
// Suppress the retired field as one native focus subtree. React may reset node properties
// during a check, so keep an owned, reversible predicate for this node's lifetime.
export function suspendUpdaterFocus(node:any){const descriptor=Object.getOwnPropertyDescriptor(node,'GetFocusable'),original=node.GetFocusable;const blocked=()=> 'none';Object.defineProperty(node,'GetFocusable',{value:blocked,writable:true,configurable:true});return()=>{if(node.GetFocusable!==blocked)return;if(descriptor)Object.defineProperty(node,'GetFocusable',descriptor);else delete node.GetFocusable;};}

export function LoaderUpdateRow(props:any){return <YmccPackageUpdateRow {...props}/>;}

function focusNode(element:any){for(let fiber=element?.[Object.keys(element).find(k=>k.startsWith('__reactFiber'))??''],depth=0;fiber&&depth<60;depth++,fiber=fiber.return){const node=fiber.memoizedProps?.node;if(node?.Element===element)return node;}return null;}
function updaterField(doc:Document,fieldClass:string,owned:HTMLElement|null){
  // Match the pinned Decky component's identity, never a translated word elsewhere in Steam.
  for(const button of Array.from(doc.querySelectorAll('button'))){let fiber=(button as any)[Object.keys(button).find(k=>k.startsWith('__reactFiber'))??''];for(let depth=0;fiber&&depth<28;depth++,fiber=fiber.return){if(fiber.type?.name==='UpdaterSettings'){const field=button.closest('.'+fieldClass) as HTMLElement|null;if(!field||field.closest('[data-ymcc-loader-update]'))break;const rect=(field===owned?field.parentElement:field)?.getBoundingClientRect();if(!rect||rect.width<1||rect.height<1)break;const node=focusNode(field);if(!node?.Parent||!node.SetProperties)break;let parentFiber=(field as any)[Object.keys(field).find(k=>k.startsWith('__reactFiber'))??''];for(let d=0;parentFiber&&d<40;d++,parentFiber=parentFiber.return){if(parentFiber.tag===10&&parentFiber.memoizedProps?.value===node.Parent)return {field,node,parent:node.Parent,provider:parentFiber.type};}break;}}}return null;
}
export function installLoaderUpdateRow({host,ui,reactDom,routerHook,client}:any){
  if(!client||!reactDom?.createPortal||!routerHook?.addGlobalComponent||!ui.gamepadDialogClasses?.Field||!ui.Field||!host.DeckyPluginLoader?.deckyState?.publicState)return ()=>{};
  const cache=new Map(),owners=new Set<()=>void>();let disposed=false;
  // Consumer UI adapter for the verified official 3.3 settings Field.
  // Preserve Decky versionInfo/updatable state; never change its updater capability.
  function Surface(){const anchor=React.useRef(null as HTMLElement|null),[target,setTarget]=React.useState(null as any);
    React.useEffect(()=>{const doc=anchor.current?.ownerDocument,view=doc?.defaultView;if(!doc||!view)return;let field:HTMLElement|null=null,mount:HTMLElement|null=null,before='',restoreFocus:(()=>void)|null=null;
      const release=()=>{if(field?.style.display==='none')field.style.display=before;restoreFocus?.();mount?.remove();field=null;mount=null;restoreFocus=null;};
      const scan=()=>{if(disposed)return;const updating=host.DeckyPluginLoader.deckyState.publicState().isLoaderUpdating;const supported=supportsOfficialUpdaterConsumer(host.DeckyPluginLoader.deckyState.publicState().versionInfo?.current);const found=!updating&&supported&&host.location.pathname.startsWith('/routes/decky/settings')?updaterField(doc,ui.gamepadDialogClasses.Field,field):null;if(found?.field===field&&mount?.isConnected)return;release();if(found){field=found.field;watchVisibility(field);restoreFocus=suspendUpdaterFocus(found.node);before=field.style.display;mount=doc.createElement('div');mount.setAttribute('data-ymcc-loader-update','true');field.after(mount);field.style.display='none';setTarget({...found,mount});}else setTarget(null);};
      // Steam retains settings pages and changes ancestor classes during route/size transitions.
      // Observe only those ancestors; no document-wide style observer or idle polling.
      const visibilityObserver=new view.MutationObserver(scan);
      const watchVisibility=(element:HTMLElement)=>{visibilityObserver.disconnect();for(let e=element.parentElement,depth=0;e&&depth<20;depth++,e=e.parentElement)visibilityObserver.observe(e,{attributes:true,attributeFilter:['class','style','hidden','aria-hidden']});};
      const observer=new view.MutationObserver(scan);observer.observe(doc.body,{childList:true,subtree:true});owners.add(release);scan();return()=>{observer.disconnect();visibilityObserver.disconnect();owners.delete(release);release();};
    },[]);
    const ParentProvider=target?.provider;return <><span ref={anchor} style={{display:'none'}}/>{target&&reactDom.createPortal(<ParentProvider value={target.parent}><LoaderUpdateRow key="ymcc-loader-update" host={host} ui={ui} cache={cache} client={client}/></ParentProvider>,target.mount)}</>;
  }
  routerHook.addGlobalComponent('YMCC.DeckyLoaderUpdate',Surface);
  return()=>{if(disposed)return;disposed=true;routerHook.removeGlobalComponent('YMCC.DeckyLoaderUpdate');for(const release of owners)release();owners.clear();cache.clear();};
}
