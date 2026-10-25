import {compareVersions} from '../../decky-plugin/src/deckyStoreClient';
import {validUpdateTarget,type UpdaterView,type UpdaterRequest} from '../../decky-plugin/src/deckyUpdaterProtocol';
import type {MirrorPeer,MirrorRequest} from './deckyMirrorRelay';
interface UpdateState {phase:string;version?:string;sha256?:string;percent?:number;message?:string;error?:string;downloadActive?:boolean;operationId?:string;}
export interface DeckyUpdaterDeps {version:string;read:()=>{state:UpdateState;target:{version:string;sha256:string}|null};ready:()=>boolean;gameRunning:()=>boolean;powerReady:()=>boolean;check:()=>Promise<void>;download:()=>Promise<boolean>;install:()=>Promise<void>;validateInstall:()=>Promise<void>;observe:(changed:()=>void)=>()=>void;reply:(connection:string,message:unknown)=>Promise<boolean>;}
// Own only request admission and message receipts. Download/install ownership stays in updateManager.
export function createDeckyUpdaterService(deps:DeckyUpdaterDeps){
  let peer:MirrorPeer|null=null,watching=false,disposed=false,revision=0,task:Promise<void>|null=null,lastWire='',notice='';
  let stopObserve:(()=>void)|null=null,clientRun='';const clients=new Map<string,{highest:number;receipts:Map<number,{signature:string;result:Promise<void>;response?:Record<string,unknown>}>}>();
  const matches=(p:MirrorPeer)=>!!peer&&p.runId===peer.runId&&p.connection===peer.connection;
  const target=()=>{const t=deps.read().target;return t&&validUpdateTarget(t.version,t.sha256)&&compareVersions(t.version,deps.version)===1?{version:t.version,sha256:t.sha256.toLowerCase()}:null;};
  function view():UpdaterView {const {state}=deps.read(),t=target(),ready=!disposed&&deps.ready(),gameRunning=deps.gameRunning(),power=deps.powerReady();const busy=!!task||['checking','downloading','validating','installing'].includes(state.phase)||state.downloadActive===true;return {protocol:1,revision:Math.max(1,revision),hostVersion:deps.version,phase:state.phase,targetVersion:t?.version??null,targetHash:t?.sha256??null,percent:Number.isFinite(state.percent)?Math.max(0,Math.min(100,state.percent!)):null,canCheck:ready&&!busy,canDownload:ready&&!busy&&!!t&&['available','failed','interrupted'].includes(state.phase),canInstall:ready&&!busy&&!gameRunning&&power&&!!t&&state.phase==='downloaded'&&state.version===t.version&&state.sha256?.toLowerCase()===t.sha256,gameRunning,message:(notice||state.error||state.message||'通过 YMCC 检查更新').slice(0,600)};}
  async function send(message:Record<string,unknown>,owner=peer){if(!owner||disposed||!matches(owner))return;try{await deps.reply(owner.connection,{...message,runId:owner.runId});}catch{/* The transport is optional. Never restart or replay an update here. */}}
  function publish(force=false){if(!peer||!watching||disposed)return;const next=view(),key=JSON.stringify({...next,revision:0});if(!force&&key===lastWire)return;lastWire=key;next.revision=++revision;void send({type:'ymcc-updater',view:next});}
  function stopWatching(){watching=false;stopObserve?.();stopObserve=null;lastWire='';}
  function connected(next:MirrorPeer){if(disposed)return;if(next.connected===false){if(matches(next)){stopWatching();peer=null;}return;}if(!/^[a-f0-9]{32}$/.test(next.runId)||!/^\d{1,20}$/.test(next.connection))return;const fresh=!matches(next);if(clientRun!==next.runId){clients.clear();clientRun=next.runId;}if(peer&&!matches(next))stopWatching();peer={connection:next.connection,runId:next.runId};if(fresh)void send({type:'ymcc-updater-ready',protocol:1});}
  const rejected=(id:unknown,code:string,message:string,owner:MirrorPeer|null,clientId:string)=>send({type:'ymcc-updater-result',id,clientId,ok:false,code,message},owner);
  function request(event:MirrorRequest){if(disposed||!matches(event)||event.request.runId!==peer!.runId)return;const r=event.request,owner={...peer!};
    if(r.command==='updater.status'){if(r.subscribe===false){stopWatching();return;}if(!watching){watching=true;stopObserve=deps.observe(()=>{notice='';publish();});}publish(true);return;}
    if(r.command!=='updater.request')return;
    if(typeof r.clientId!=='string'||!/^[a-f0-9-]{16,64}$/i.test(r.clientId)||!Number.isSafeInteger(r.id)||Number(r.id)<1)return;
    const id=Number(r.id),clientId=r.clientId,signature=JSON.stringify([r.action,r.version,r.sha256]);let client=clients.get(clientId);
    if(!client){if(clients.size>=8){void rejected(id,'busy','更新请求过多，请重开页面。',owner,clientId);return;}client={highest:0,receipts:new Map()};clients.set(clientId,client);}
    const existing=client.receipts.get(id);if(existing){if(existing.signature!==signature)void rejected(id,'conflict','更新请求标识重复，请重新确认。',owner,clientId);else if(existing.response)void send(existing.response,owner);else void existing.result.then(()=>{if(existing.response)void send(existing.response,owner);});return;}
    if(id<=client.highest){void rejected(id,'stale','旧更新请求已失效，请重新确认。',owner,clientId);return;}client.highest=id;
    const respond=async(message:Record<string,unknown>)=>{const record=client!.receipts.get(id);const response={type:'ymcc-updater-result',id,clientId,...message};if(record)record.response=response;await send(response,owner);};
    const deny=(code:string,message:string)=>respond({ok:false,code,message});
    const execute=async()=>{
      const v=view(),action=r.action as UpdaterRequest['action'];
      if(!['check','download','install'].includes(action)){await deny('action','不支持的更新操作。');return;}
      if(action==='check'?!v.canCheck:action==='download'?!v.canDownload:!v.canInstall){await deny('unavailable',action==='install'&&v.gameRunning?'游戏运行中，请退出游戏后安装。':'当前状态不能执行此更新操作，请刷新后重试。');return;}
      if(action!=='check'&&(!validUpdateTarget(r.version,r.sha256)||r.version!==v.targetVersion||String(r.sha256).toLowerCase()!==v.targetHash)){await deny('target','更新包已改变，请重新检查并确认版本。');return;}
      notice=action==='check'?'正在检查兼容更新':action==='download'?'正在下载兼容更新':'正在核验安装条件';
      const operation=Promise.resolve().then(async()=>{if(disposed||!matches(owner)||!deps.ready())throw Error('连接已改变，请重新确认更新。');if(action==='install')await deps.validateInstall();if(disposed||!matches(owner)||!deps.ready())throw Error('连接已改变，请重新确认更新。');if(action==='install'&&(deps.gameRunning()||!deps.powerReady()))throw Error('当前不能安装，请退出游戏并等待电源恢复完成。');const current=target();if(action!=='check'&&(!current||current.version!==r.version||current.sha256!==String(r.sha256).toLowerCase()))throw Error('更新包已改变，请重新确认。');if(action==='check')await deps.check();else if(action==='download')await deps.download();else await deps.install();});
      void operation.catch(()=>{});task=operation;publish();await respond({ok:true,code:'accepted',message:action==='check'?'正在检查兼容更新':action==='download'?'已交给 YMCC 下载并校验':'已交给 YMCC 核验安装条件'});
      try{await operation;notice='';}catch(e){notice=e instanceof Error?e.message.slice(0,600):'更新操作失败，请查看 YMCC 更新状态。';}
      finally{if(task===operation)task=null;publish(true);}
    };
    // Cache the receipt before asynchronous work: button repeats never start another install.
    const receipt=Promise.resolve().then(execute);client.receipts.set(id,{signature,result:receipt});while(client.receipts.size>32)client.receipts.delete(client.receipts.keys().next().value!);
    void receipt.catch(()=>{});
  }
  return {connected,request,view,changed:()=>publish(),dispose(){if(disposed)return;disposed=true;stopWatching();peer=null;clients.clear();}};
}
