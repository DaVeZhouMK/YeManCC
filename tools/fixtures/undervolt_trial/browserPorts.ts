const listeners=new Map<string,Set<(value:any)=>void>>();
export const isNativeRuntime=true;
export const on=(event:string,fn:(v:any)=>void)=>{let set=listeners.get(event);if(!set)listeners.set(event,set=new Set());set.add(fn);return()=>set!.delete(fn);};
export const emitNativeEvent=async()=>{};
const state={ok:true,token:0,pending:false,remaining:0,stage:'idle',error:'',saved:null as any,excluded:new URLSearchParams(location.search).has('rog')};
const seeded=new URLSearchParams(location.search).get('saved');
if(seeded)state.saved={schema:1,enabled:seeded!=='off',value:-4,vendor:'amd',unit:'co_step',identity:{family:'fixture',vendor:'amd'}};
let timer:ReturnType<typeof setInterval>|undefined;
export const stats={probe:0,hardware:[] as number[],saves:0,failNext:false};
function emit(){for(const fn of listeners.get('cpu.undervolt.state')??[])fn({...state});}
export async function invoke(command:string,args:any={}){
 if(!command.startsWith('cpu.undervolt.'))return null;
 if(command.endsWith('.state'))return {...state};
 if(command.endsWith('.begin')||command.endsWith('.cancel')){if(timer)clearInterval(timer);state.token++;state.pending=false;state.stage=command.endsWith('.cancel')?'cancelled':'applying';return {...state};}
 if(command.endsWith('.arm')){
  if(args.token!==state.token)return {ok:false,error:'stale'};
  state.pending=true;state.remaining=30;state.stage='pending';const candidate=structuredClone(args.preference);
  timer=setInterval(()=>{state.remaining--;if(state.remaining<=0){clearInterval(timer);state.pending=false;state.stage='saved';state.saved=candidate;stats.saves++;}emit();},1000);emit();return {...state};
 }
 throw Error('Unexpected IPC '+command);
}
export const capability:any={supported:true,available:true,vendor:'amd',unit:'co_step',offset:{min:-60,max:0,step:1},scope:{kind:'amd_all_core',planes:[]},identity:{vendor:'amd',family:'fixture'},readbackCapability:false};
export async function probeUndervolt(){stats.probe++;return capability;}
export async function setUndervolt(value:number){stats.hardware.push(value);if(stats.failNext){stats.failNext=false;return {ok:false,accepted:false,applicationState:'unknown',applied:null,msg:'fixture failure'};}return {ok:true,accepted:true,applicationState:'unknown',applied:null,msg:'',receipt:{accepted:true,vendor:'amd',unit:'co_step',requested:{value,scope:'amd_all_core',planes:[]}}};}
(window as any).__uvPorts={stats,state};
