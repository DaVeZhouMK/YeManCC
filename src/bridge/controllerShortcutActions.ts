/** One-shot shortcut execution coordinator. Display consumers never call these ports. */
import type { ShortcutFeedbackState } from './shortcutActionFeedback';
import type { SharedPowerWriteScope } from './powerLifecycleState';
export const UI_SHORTCUT_ACTIONS = ['os.switchProgram','monitor.toggle','display.frameRateControl','fan.switchPreset','app.launchQuickApp'] as const;
export type UiShortcutAction = typeof UI_SHORTCUT_ACTIONS[number];
export interface ShortcutActionRequest {
  schemaVersion: 1; id: string; actionId: UiShortcutAction; params: Record<string, string>;
  issuedAtUnixMs: number; expiresAtUnixMs: number; powerGeneration: number; inputRevision: number;
  powerScope?: SharedPowerWriteScope;
}
export interface ShortcutActionOutcome {
  state: ShortcutFeedbackState; title: string; message: string;
  value?: { text: string; number?: number; unit?: string };
}
export interface ShortcutExecutionContext {
  request: ShortcutActionRequest;
  checkpoint(): void;
  verify(): Promise<void>;
  read<T>(load: (signal: AbortSignal) => Promise<T>): Promise<T>;
}
export const SHORTCUT_FRAME_RATES = [15,20,25,30,40,45,50,60,72,90,120,144,165,200,240,300] as const;
export function parseUiShortcutParameters(actionId: string, raw: unknown): Record<string,string> | null {
  if (!UI_SHORTCUT_ACTIONS.includes(actionId as UiShortcutAction) || !raw || typeof raw !== 'object' || Array.isArray(raw)) return null;
  const p = raw as Record<string, unknown>;
  if(actionId==='os.switchProgram')return {};
  if(actionId==='monitor.toggle')return ['toggle','show','hide'].includes(String(p.operation??'toggle')) && (p.operation===undefined||typeof p.operation==='string') ? {operation:String(p.operation??'toggle')} : null;
  if(actionId==='display.frameRateControl') {
    const operation=p.operation??'toggle',fps=p.fps??'60';
    if(typeof operation!=='string'||!['toggle','set'].includes(operation)||typeof fps!=='string'||!SHORTCUT_FRAME_RATES.some(v=>String(v)===fps))return null;
    return {operation,fps};
  }
  if(actionId==='fan.switchPreset')return ['next','soft','balanced','aggressive'].includes(String(p.preset??'next')) && (p.preset===undefined||typeof p.preset==='string') ? {preset:String(p.preset??'next')} : null;
  return typeof p.appPath==='string'&&p.appPath.length>0&&p.appPath.length<=2048&&!/[\u0000-\u001f]/.test(p.appPath)?{appPath:p.appPath}:null;
}
export function readShortcutActionRequest(raw: unknown, now=Date.now()): ShortcutActionRequest|null {
  if(!raw||typeof raw!=='object'||Array.isArray(raw))return null;
  const p=raw as Partial<ShortcutActionRequest>,params=parseUiShortcutParameters(String(p.actionId),p.params);
  if(p.schemaVersion!==1||typeof p.id!=='string'||!p.id||p.id.length>128||!params||!Number.isSafeInteger(p.issuedAtUnixMs)||!Number.isSafeInteger(p.expiresAtUnixMs)||!Number.isSafeInteger(p.powerGeneration)||!Number.isSafeInteger(p.inputRevision)||p.powerGeneration!<0||p.inputRevision!<0)return null;
  if(p.issuedAtUnixMs!>now+1000||p.expiresAtUnixMs!<=now||p.expiresAtUnixMs!<=p.issuedAtUnixMs!||p.expiresAtUnixMs!-p.issuedAtUnixMs!>30000)return null;
  let powerScope: SharedPowerWriteScope | undefined;
  if(p.powerScope!==undefined){
    const scope=p.powerScope;
    if(!Array.isArray(scope)||scope.length!==3||scope[0]!==p.powerGeneration||scope[0]<=0||
       !Number.isSafeInteger(scope[1])||scope[1]<0||!Number.isSafeInteger(scope[2])||scope[2]<0)return null;
    powerScope=[scope[0],scope[1],scope[2]];
  }
  return {schemaVersion:1,id:p.id,actionId:p.actionId!,params,issuedAtUnixMs:p.issuedAtUnixMs!,expiresAtUnixMs:p.expiresAtUnixMs!,powerGeneration:p.powerGeneration!,inputRevision:p.inputRevision!,powerScope};
}
const titles: Record<UiShortcutAction,string>={'os.switchProgram':'切换程序','monitor.toggle':'监控显示','display.frameRateControl':'锁帧控制','fan.switchPreset':'风扇预设','app.launchQuickApp':'快捷应用'};
export function shortcutActionFailure(actionId: UiShortcutAction, error: unknown): ShortcutActionOutcome {
  const code=error instanceof Error?error.message:String(error);
  const message=code==='SHORTCUT_APP_REMOVED'?'快捷应用已移除或路径已更改，请重新选择。':
    code==='SHORTCUT_BUSY'?'正在处理其他操作，请稍后重试。':
    code==='SHORTCUT_POWER_CHANGED'?'电源状态已变化，请稍后重试。':
    code==='SHORTCUT_SOURCE_CHANGED'?'配置或输入来源已变化，请重新触发。':
    code==='SHORTCUT_FAN_NOT_READY'?'风扇功能尚未就绪，请稍后重试。':
    code==='SHORTCUT_NO_APPS'?'尚无可启动的快捷应用，请先在快捷应用中添加。':
    titles[actionId]+'未确认完成，请检查当前状态后重试。';
  return {state:'failed',title:titles[actionId],message};
}
export function createControllerShortcutActions(deps: {
  claim(request:ShortcutActionRequest,signal:AbortSignal):Promise<boolean>;
  execute:Record<UiShortcutAction,(params:Record<string,string>,context:ShortcutExecutionContext)=>Promise<ShortcutActionOutcome>>;
  complete(request:ShortcutActionRequest,outcome:ShortcutActionOutcome,signal?:AbortSignal):Promise<void>;
  clock?:()=>number;
  /** Capture target identity at receipt, before the serialized queue can wait. */
  capture?:(request:ShortcutActionRequest)=>()=>boolean;
}) {
  const clock=deps.clock??Date.now,seen=new Map<string,number>();
  const pending=new Map<ShortcutActionRequest,AbortController>();
  let ended=false,queue:Promise<void>=Promise.resolve();
  function invalidate(retire:(request:ShortcutActionRequest)=>boolean=()=>true){
    for(const [request,controller] of [...pending])if(retire(request))controller.abort();
  }
  function enqueue(raw:unknown): boolean {
    const request=readShortcutActionRequest(raw,clock());if(!request||ended)return false;
    for(const[id,expires]of seen)if(expires<=clock())seen.delete(id);
    if(seen.has(request.id))return false;
    if(pending.size>=16){void deps.complete(request,shortcutActionFailure(request.actionId,new Error('SHORTCUT_BUSY'))).catch(()=>{});return false;}
    seen.set(request.id,request.expiresAtUnixMs);while(seen.size>128)seen.delete(seen.keys().next().value!);
    const controller=new AbortController(),captured=controller.signal,isCurrent=deps.capture?.(request)??(()=>true);
    pending.set(request,controller);
    const checkScope=()=>{if(ended||captured.aborted||request.expiresAtUnixMs<=clock())throw Error('SHORTCUT_SOURCE_CHANGED')};
    const checkpoint=()=>{checkScope();if(!isCurrent())throw Error('SHORTCUT_SOURCE_CHANGED')};
    const waitForReply=async<T>(load:(signal:AbortSignal)=>Promise<T>,check=checkpoint):Promise<T>=>{
      check();
      let abort!:()=>void;
      const revoked=new Promise<never>((_,reject)=>{abort=()=>reject(Error('SHORTCUT_SOURCE_CHANGED'));});
      captured.addEventListener('abort',abort,{once:true});
      try{const value=await Promise.race([revoked,Promise.resolve().then(()=>{check();return load(captured);})]);check();return value;}
      finally{captured.removeEventListener('abort',abort);}
    };
    // Claim only checks the Native ticket; retiring its response does not replay an action.
    const verify=async()=>{if(!await waitForReply(signal=>deps.claim(request,signal)))throw Error('SHORTCUT_SOURCE_CHANGED');};
    // Execution has settled; retiring the feedback observer does not undo or resend it.
    const complete=(outcome:ShortcutActionOutcome)=>waitForReply(signal=>deps.complete(request,outcome,signal),checkScope).catch(()=>{});
    const run=queue.then(async()=>{
      try {await verify();const outcome=await deps.execute[request.actionId](request.params,{request,checkpoint,verify,read:waitForReply});checkpoint();await complete(outcome);}
      catch(error){if(!ended&&!captured.aborted&&request.expiresAtUnixMs>clock())await complete(shortcutActionFailure(request.actionId,error));}
      finally {pending.delete(request);}
    });queue=run.catch(()=>{});return true;
  }
  return {enqueue,invalidate,stop(){ended=true;invalidate();seen.clear()},idle(){return queue},get pending(){return pending.size}};
}
