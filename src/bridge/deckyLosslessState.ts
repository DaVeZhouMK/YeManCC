// Decky observation/confirmation budgets only. No process, settings or hardware ownership.
export const DECKY_LOSSLESS_READ_MS = 6000;
export const DECKY_LOSSLESS_ACTION_MS = 40000;
export type DeckyLosslessDiagnostic = (event: string, detail: {elapsedMs?:number;ok?:boolean;stage?:string;completed?:boolean;applied?:boolean;pending?:boolean}) => void;
export function deckyLosslessText(value: string): string { return value.replace(/小黄鸭/g,'LosslessScaling'); }
// One timed-out reader may be bypassed by an explicit refresh. Keep at most
// two unresolved reads, and never turn snapshot traffic into background polling.
export class DeckyLosslessObservation<T> {
  private key = ''; private serial = 0; private dirty = true; private ended = false;
  private document: T | null = null; private notice = '';
  private task: {serial:number;key:string;expired:boolean;timer:ReturnType<typeof setTimeout>} | null = null;
  private readers = new Set<NonNullable<DeckyLosslessObservation<T>['task']>>();
  constructor(private changed:()=>void,private diagnostic:DeckyLosslessDiagnostic = ()=>{}) {}
  refresh():void { if(this.ended)return; this.serial++; this.dirty=true; }
  demand():void { if(!this.task)this.refresh(); }
  private reset():void {
    if(this.task)clearTimeout(this.task.timer);
    this.task=null;this.document=null;this.notice='';this.refresh();
  }
  view(key:string,read:()=>Promise<T>):{document:T|null;pending:boolean;notice:string} {
    if(this.ended)return {document:null,pending:false,notice:''};
    if(key!==this.key){this.key=key;this.reset();}
    if(this.dirty&&!this.task){
      if(this.readers.size>=2){
        this.notice='LosslessScaling 状态读取仍未结束；等待已有读取释放后可刷新，不自动重试';
      }else{
        this.dirty=false;this.notice='LosslessScaling 状态读取中；其他设置仍可操作';
        const started=Date.now(),owned={serial:this.serial,key,expired:false,timer:null as unknown as ReturnType<typeof setTimeout>};
        this.task=owned;this.readers.add(owned);this.diagnostic('lossless-read-start',{});
        owned.timer=setTimeout(()=>{
          if(this.task!==owned||this.ended)return;
          owned.expired=true;this.task=null;
          this.notice='LosslessScaling 状态回读超时；保留上次状态，可重新打开侧栏刷新';
          this.diagnostic('lossless-read-timeout',{elapsedMs:Date.now()-started,ok:false});this.changed();
        },DECKY_LOSSLESS_READ_MS);
        let readConfirmed=false;
        Promise.resolve().then(()=>{
          if(this.ended||this.task!==owned||owned.serial!==this.serial)return null;
          return read();
        }).then(document=>{
          if(this.ended||owned.expired||this.task!==owned||owned.key!==this.key||owned.serial!==this.serial)return;
          this.document=document;this.notice='';readConfirmed=document!==null;
        },()=>{
          if(!this.ended&&!owned.expired&&this.task===owned&&owned.key===this.key&&owned.serial===this.serial)
            this.notice='LosslessScaling 状态暂不可读；保留上次状态，其他设置仍可操作';
        }).finally(()=>{
          clearTimeout(owned.timer);this.readers.delete(owned);
          const current=this.task===owned;if(current)this.task=null;
          if(this.ended)return;
          this.diagnostic('lossless-read-finished',{elapsedMs:Date.now()-started,ok:readConfirmed,stage:owned.expired?'late':'settled'});
          if(current||this.dirty&&!this.task)this.changed();
        });
      }
    }
    return {document:this.document,pending:!!this.task,notice:this.notice};
  }
  clear():void {if(this.ended)return;this.key='';this.reset();}
  dispose():void {
    this.ended=true;this.serial++;
    for(const reader of this.readers)clearTimeout(reader.timer);
    this.readers.clear();this.task=null;this.document=null;
  }
}
// Expiry revokes all later checkpoints, while the original action keeps its own
// lock until its in-flight await settles. A deadline never repeats the action.
export async function waitForDeckyLosslessAction<T>(run:(checkpoint:()=>void)=>Promise<T>,checkpoint:()=>void,
 diagnostic:DeckyLosslessDiagnostic=()=>{},replyAfterMs?:number):Promise<{pending:true;expired?:boolean}|{pending:false;result:T}> {
  let current=true,timer:ReturnType<typeof setTimeout>,replyTimer:ReturnType<typeof setTimeout>|undefined;const started=Date.now();
  const check=()=>{if(!current)throw new Error('LOSSLESS_CONFIRMATION_EXPIRED');checkpoint();};
  diagnostic('lossless-action-start',{});
  const work=Promise.resolve().then(()=>run(check)).then(result=>({pending:false as const,result}));
  const deadline=new Promise<{pending:true;expired?:boolean}>(resolve=>{timer=setTimeout(()=>{
    current=false;diagnostic('lossless-action-timeout',{elapsedMs:Date.now()-started,ok:false});resolve({pending:true,expired:true});
  },DECKY_LOSSLESS_ACTION_MS);});
  const reply=replyAfterMs===undefined?null:new Promise<{pending:true}>(resolve=>{
    replyTimer=setTimeout(()=>resolve({pending:true}),Math.min(DECKY_LOSSLESS_ACTION_MS,Math.max(1,replyAfterMs)));
  });
  const finish=()=>{clearTimeout(timer!);if(replyTimer)clearTimeout(replyTimer);};
  // A soft reply releases the client wait, not the actual owner lock or deadline.
  // Observe late rejection without an unhandled promise or false confirmation.
  void work.then(outcome=>{
    finish();const result=outcome.result,receipt=result&&typeof result==='object'?result as Record<string,unknown>:null;
    const applied=typeof receipt?.applied==='boolean'?receipt.applied:undefined;
    const pending=typeof receipt?.pending==='boolean'?receipt.pending:undefined;
    diagnostic('lossless-action-finished',{elapsedMs:Date.now()-started,ok:current&&applied!==false,completed:current,
      ...(applied===undefined?{}:{applied}),...(pending===undefined?{}:{pending}),stage:!current?'late':applied===false?'partial':'settled'});
  },()=>{finish();diagnostic('lossless-action-finished',{elapsedMs:Date.now()-started,ok:false,completed:false,stage:current?'failed':'late'});});
  return await Promise.race(reply?[work,deadline,reply]:[work,deadline]);
}
