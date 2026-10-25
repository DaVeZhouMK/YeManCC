/** One-shot shortcut execution coordinator. Display consumers never call these ports. */
import type { ShortcutFeedbackState } from './shortcutActionFeedback';
export const UI_SHORTCUT_ACTIONS = ['os.switchProgram','monitor.toggle','display.frameRateControl','fan.switchPreset','app.launchQuickApp'] as const;
export type UiShortcutAction = typeof UI_SHORTCUT_ACTIONS[number];
export interface ShortcutActionRequest {
  schemaVersion: 1; id: string; actionId: UiShortcutAction; params: Record<string, string>;
  issuedAtUnixMs: number; expiresAtUnixMs: number; powerGeneration: number; inputRevision: number;
}
export interface ShortcutActionOutcome {
  state: ShortcutFeedbackState; title: string; message: string;
  value?: { text: string; number?: number; unit?: string };
}
export interface ShortcutExecutionContext {
  request: ShortcutActionRequest;
  checkpoint(): void;
  verify(): Promise<void>;
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
  return {schemaVersion:1,id:p.id,actionId:p.actionId!,params,issuedAtUnixMs:p.issuedAtUnixMs!,expiresAtUnixMs:p.expiresAtUnixMs!,powerGeneration:p.powerGeneration!,inputRevision:p.inputRevision!};
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
  claim(request:ShortcutActionRequest):Promise<boolean>;
  execute:Record<UiShortcutAction,(params:Record<string,string>,context:ShortcutExecutionContext)=>Promise<ShortcutActionOutcome>>;
  complete(request:ShortcutActionRequest,outcome:ShortcutActionOutcome):Promise<void>;
  clock?:()=>number;
  /** Capture target identity at receipt, before the serialized queue can wait. */
  capture?:(request:ShortcutActionRequest)=>()=>boolean;
}) {
  const clock=deps.clock??Date.now,seen=new Map<string,number>();
  let ended=false,version=0,pending=0,queue:Promise<void>=Promise.resolve();
  function enqueue(raw:unknown): boolean {
    const request=readShortcutActionRequest(raw,clock());if(!request||ended)return false;
    for(const[id,expires]of seen)if(expires<=clock())seen.delete(id);
    if(seen.has(request.id))return false;
    if(pending>=16){void deps.complete(request,shortcutActionFailure(request.actionId,new Error('SHORTCUT_BUSY'))).catch(()=>{});return false;}
    seen.set(request.id,request.expiresAtUnixMs);while(seen.size>128)seen.delete(seen.keys().next().value!);
    const captured=version,isCurrent=deps.capture?.(request)??(()=>true);++pending;
    const checkpoint=()=>{if(ended||captured!==version||!isCurrent()||request.expiresAtUnixMs<=clock())throw Error('SHORTCUT_SOURCE_CHANGED')};
    const verify=async()=>{checkpoint();if(!await deps.claim(request))throw Error('SHORTCUT_SOURCE_CHANGED');checkpoint()};
    const run=queue.then(async()=>{
      // A result transport failure cannot undo an action or justify a second result.
      try {await verify();const outcome=await deps.execute[request.actionId](request.params,{request,checkpoint,verify});checkpoint();await deps.complete(request,outcome).catch(()=>{});}
      catch(error){if(!ended&&captured===version&&request.expiresAtUnixMs>clock())await deps.complete(request,shortcutActionFailure(request.actionId,error)).catch(()=>{});}
      finally {--pending;}
    });queue=run.catch(()=>{});return true;
  }
  return {enqueue,invalidate(){++version},stop(){ended=true;++version;seen.clear()},idle(){return queue},get pending(){return pending}};
}
