import { readonly, shallowReactive } from 'vue';
/** UI seam only. A later provider must reuse the existing update manager's
 * download/validation ownership and must never install as a side effect. */
export interface UpdatePredownloadSnapshot {
  enabled:boolean;
  phase:'idle'|'waiting'|'downloading'|'ready'|'paused'|'failed';
  version?:string;
  percent?:number;
  message?:string;
}
export interface UpdatePredownloadProvider {
  read():Promise<UpdatePredownloadSnapshot>;
  setEnabled(enabled:boolean):Promise<UpdatePredownloadSnapshot>;
  subscribe?(listener:(snapshot:UpdatePredownloadSnapshot)=>void):()=>void;
}
const state=shallowReactive({available:false,busy:false,enabled:false,phase:'unavailable' as UpdatePredownloadSnapshot['phase']|'unavailable',version:'',percent:0,message:'待接入'});
export const updatePredownloadState=readonly(state);
let provider:UpdatePredownloadProvider|null=null,epoch=0,statusRevision=0;
function accept(snapshot:UpdatePredownloadSnapshot){
 if(!snapshot || typeof snapshot.enabled!=='boolean'||!['idle','waiting','downloading','ready','paused','failed'].includes(snapshot.phase)||
    (snapshot.version!==undefined&&typeof snapshot.version!=='string')||
    (snapshot.message!==undefined&&typeof snapshot.message!=='string')||
    (snapshot.percent!==undefined&&(typeof snapshot.percent!=='number'||!Number.isFinite(snapshot.percent))))throw new Error('后台预下载状态无效');
 Object.assign(state,{enabled:snapshot.enabled,phase:snapshot.phase,version:snapshot.version||'',percent:Math.max(0,Math.min(100,Number(snapshot.percent)||0)),message:snapshot.message||''});
}
/** Registration performs no scheduling itself. The returned cleanup invalidates
 * late receipts so an old provider cannot overwrite a replacement's state. */
export function registerUpdatePredownloadProvider(next:UpdatePredownloadProvider):()=>void {
 if(provider)throw new Error('后台预下载接口已有实现');
 provider=next;const ticket=++epoch;state.available=true;state.busy=true;state.message='正在读取';
 const readingRevision=statusRevision;
 let stop:(()=>void)|undefined;
 try{stop=next.subscribe?.(snapshot=>{if(ticket===epoch){statusRevision++;accept(snapshot);}});}
 catch(error){epoch++;provider=null;Object.assign(state,{available:false,busy:false,enabled:false,phase:'unavailable',version:'',percent:0,message:'待接入'});throw error;}
 void Promise.resolve().then(()=>next.read()).then(snapshot=>{if(ticket===epoch && readingRevision===statusRevision)accept(snapshot);}).catch(error=>{if(ticket===epoch && readingRevision===statusRevision){state.phase='failed';state.message=String(error instanceof Error?error.message:error);}}).finally(()=>{if(ticket===epoch)state.busy=false;});
 return()=>{if(ticket!==epoch)return;epoch++;provider=null;try{stop?.();}finally{Object.assign(state,{available:false,busy:false,enabled:false,phase:'unavailable',version:'',percent:0,message:'待接入'});}};
}
export async function setUpdatePredownloadEnabled(enabled:boolean):Promise<void> {
 const current=provider,ticket=epoch;if(!current||!state.available)throw new Error('后台预下载尚未接入');if(state.busy)return;
 state.busy=true;const writingRevision=statusRevision;
 try{const result=await current.setEnabled(enabled);if(ticket===epoch && writingRevision===statusRevision)accept(result);}
 catch(error){if(ticket===epoch && writingRevision===statusRevision){state.phase='failed';state.message=String(error instanceof Error?error.message:error);}throw error;}
 finally{if(ticket===epoch)state.busy=false;}
}
