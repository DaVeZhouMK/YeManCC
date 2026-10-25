import {validUpdaterView,updateTargetKey,type UpdaterView,type UpdaterAction} from './deckyUpdaterProtocol';
interface Port {setPassiveDemand(name:string,enabled:boolean):void;subscribe(fn:(state:{connected:boolean})=>void,options:{connectionOnly:true}):()=>void;subscribeExtra(fn:(value:unknown)=>void):()=>void;updaterSupported():boolean;updaterRunId():string|undefined;sendUpdater(command:'updater.status'|'updater.request',data:Record<string,unknown>):boolean;}
export interface UpdaterClientState {connected:boolean;supported:boolean;view:UpdaterView|null;pending:boolean;error:string;}
// A settings-page remount is not a new authenticated update client. Retain the
// monotonic receipt identity for this MirrorClient without keeping it alive.
interface UpdaterPortSession {clientId:string;sequence:number;owner:DeckyUpdaterClient|null;}
const portSessions=new WeakMap<Port,UpdaterPortSession>();
export class DeckyUpdaterClient {
  private opened=false;private sessionState:UpdaterPortSession;private state:UpdaterClientState={connected:false,supported:false,view:null,pending:false,error:''};private listeners=new Set<()=>void>();private stopExtra:(()=>void)|null=null;private stopConnection:(()=>void)|null=null;private firstTimer:any=null;private pending:{id:number;resolve:()=>void;reject:(e:Error)=>void;timer:any}|null=null;
  constructor(private port:Port,private clientId:string,private timeout=8000){const prior=portSessions.get(port);this.sessionState=prior??{clientId,sequence:0,owner:null};if(!prior)portSessions.set(port,this.sessionState);this.clientId=this.sessionState.clientId;}
  snapshot(){return this.state;}
  subscribe(fn:()=>void){this.listeners.add(fn);return()=>{this.listeners.delete(fn);};}
  private publish(patch:Partial<UpdaterClientState>){this.state={...this.state,...patch};for(const fn of this.listeners)fn();}
  private cancelFirst(){if(this.firstTimer!==null)clearTimeout(this.firstTimer);this.firstTimer=null;}
  private failPending(message:string){const p=this.pending;if(!p)return;this.pending=null;clearTimeout(p.timer);p.reject(Error(message));}
  session(){return this.port.updaterRunId();}
  open(){if(this.opened)return;this.sessionState.owner?.close();this.sessionState.owner=this;this.opened=true;this.stopExtra=this.port.subscribeExtra(value=>this.receive(value));this.port.setPassiveDemand('updater',true);this.stopConnection=this.port.subscribe(state=>{
    this.cancelFirst();if(!state.connected){this.failPending('更新连接已断开，结果未确认，请查看 YMCC 状态。');this.publish({connected:false,supported:false,view:null,pending:false,error:''});return;}
    const supported=this.port.updaterSupported();this.publish({connected:true,supported,view:null,error:supported?'':'请重新打开支持兼容更新的新版 YMCC。'});if(!supported)return;
    this.firstTimer=setTimeout(()=>{this.firstTimer=null;this.publish({view:null,error:'YMCC 更新器未响应，请重新打开 YMCC 后重试。'});},this.timeout);
    if(!this.port.sendUpdater('updater.status',{subscribe:true})){this.cancelFirst();this.publish({error:'无法连接 YMCC 更新器。'});}
  },{connectionOnly:true});}
  private receive(raw:any){if(!this.opened)return;
    if(raw?.type==='ymcc-updater-ready'&&raw.protocol===1&&this.state.connected&&this.state.supported){this.failPending('YMCC 更新器已恢复，请查看实际状态后重新确认。');this.cancelFirst();this.publish({view:null,pending:false,error:''});this.firstTimer=setTimeout(()=>{this.firstTimer=null;this.publish({view:null,error:'YMCC 更新器未响应，请重新打开 YMCC 后重试。'});},this.timeout);this.port.sendUpdater('updater.status',{subscribe:true});return;}
    if(raw?.type==='ymcc-updater'){
      if(!validUpdaterView(raw.view)){this.cancelFirst();this.publish({view:null,error:'更新状态格式不匹配，已停用安装，请更新 YMCC。'});return;}
      if(this.state.view&&raw.view.revision<this.state.view.revision)return;this.cancelFirst();this.publish({view:raw.view,error:''});return;
    }
    const p=this.pending;if(raw?.type!=='ymcc-updater-result'||!p||raw.clientId!==this.clientId||raw.id!==p.id)return;
    if(typeof raw.ok!=='boolean'||typeof raw.message!=='string'||raw.message.length>600){this.failPending('更新回执格式不匹配，结果未确认。');this.publish({pending:false,view:null,error:'更新回执异常，请在 YMCC 查看实际状态。'});return;}
    this.pending=null;clearTimeout(p.timer);this.publish({pending:false,error:raw.ok?'':raw.message});raw.ok?p.resolve():p.reject(Error(raw.message));
  }
  request(action:UpdaterAction,expected?:UpdaterView):Promise<void>{
    const v=this.state.view;if(!this.opened||!this.state.connected||!this.state.supported||!v||this.pending)return Promise.reject(Error('更新器当前不可用，请稍后重试。'));
    const allowed=action==='check'?v.canCheck:action==='download'?v.canDownload:v.canInstall;if(!allowed)return Promise.reject(Error('当前状态不能执行此更新操作。'));
    if(action!=='check'&&(!expected||updateTargetKey(expected)!==updateTargetKey(v)))return Promise.reject(Error('更新包已改变，请重新确认版本。'));
    const id=++this.sessionState.sequence;
    return new Promise<void>((resolve,reject)=>{const timer=setTimeout(()=>{if(this.pending?.id!==id)return;this.pending=null;this.publish({pending:false,view:null,error:'更新请求未确认，请在 YMCC 查看实际状态。'});reject(Error('更新请求未确认；不自动重放。'));this.port.sendUpdater('updater.status',{subscribe:true});},this.timeout);this.pending={id,resolve,reject,timer};this.publish({pending:true,error:''});
      if(!this.port.sendUpdater('updater.request',{id,clientId:this.clientId,action,...(action==='check'?{}:{version:v.targetVersion,sha256:v.targetHash})})){this.failPending('更新连接不可用，请重试。');this.publish({pending:false,error:'更新连接不可用，请重试。'});}
    });
  }
  close(){if(!this.opened)return;this.opened=false;if(this.sessionState.owner===this)this.sessionState.owner=null;this.port.sendUpdater('updater.status',{subscribe:false});this.stopExtra?.();this.stopExtra=null;this.stopConnection?.();this.stopConnection=null;this.cancelFirst();this.failPending('更新页面已关闭，已接收的操作请在 YMCC 查看。');this.port.setPassiveDemand('updater',false);this.publish({connected:false,supported:false,view:null,pending:false});this.listeners.clear();}
}
