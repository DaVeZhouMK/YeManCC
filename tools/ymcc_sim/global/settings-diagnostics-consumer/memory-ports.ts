// Reuse the existing navigation memory settings/IPC terminal, with targeted reply control.
import * as original from '../ui-navigation/templates/memoryIpc';
export const isNativeRuntime=original.isNativeRuntime;export const on=original.on,emitNativeEvent=original.emitNativeEvent,setLogSink=original.setLogSink,getLastGamepadState=original.getLastGamepadState,IpcRemoteError=original.IpcRemoteError;
const w=window as any;w.__fixture??={};Object.assign(w.__fixture,{loggingEnabled:false,navigationShortcuts:[],currentLogFiles:[],side:'ac'});
type Plan={kind:'resolve'|'reject'|'hold';value?:any;message?:string};
const plans=new Map<string,Plan[]>(),pending=new Map<number,()=>void>();let serial=0;const calls:any[]=[];
const gates={gyro:false,virtual:false};
function receipt(){return{protocol:'ymcc.current-diagnostic-logs.v1',ok:true,busy:false,captured:true,pauseAcknowledged:true,deleteAttempted:true,restored:true,originalGates:{...gates},restoredGates:{...gates},files:w.__fixture.currentLogFiles.map((name:string)=>({name,state:'deleted',error:''})),pauseError:'',deleteError:'',restoreError:''};}
function defaults(cmd:string,args:any){if(cmd==='logs.domainGetEnabled')return{...gates};if(cmd==='logs.domainSetEnabled')return{ok:true,gyro:gates.gyro,virtual:!!args.enabled};if(cmd==='logs.exportAll')return{ok:true,path:'memory://owned/diagnostics.zip',count:16};if(cmd==='logs.deleteCurrent')return receipt();}
w.__diagnosticsPorts={calls,gates,plan(cmd:string,plan:Plan){const q=plans.get(cmd)||[];q.push(plan);plans.set(cmd,q);},release(cmd:string){for(const row of calls.filter((r:any)=>r.cmd===cmd&&r.state==='held')){const fn=pending.get(row.id);pending.delete(row.id);fn?.();}},inspect(){return{calls:structuredClone(calls),gates:{...gates},pending:pending.size};}};
export async function invoke(cmd:string,args:any={},options:any={}){const row={id:++serial,cmd,args:structuredClone(args),timeoutMs:options?.timeoutMs??null,state:'pending',reply:undefined as any};calls.push(row);
 try{let value:any;if(cmd.startsWith('logs.')){if(!['logs.domainGetEnabled','logs.domainSetEnabled','logs.exportAll','logs.deleteCurrent'].includes(cmd))throw Error('UNDECLARED_DIAGNOSTIC_TERMINAL:'+cmd);const p=plans.get(cmd)?.shift();if(p?.kind==='hold'){row.state='held';await new Promise<void>(resolve=>pending.set(row.id,resolve));}if(p?.kind==='reject')throw Error(p.message||'fixture transport denied');value=p&&'value'in p?p.value:defaults(cmd,args);if(cmd==='logs.domainSetEnabled'&&value?.ok===true&&typeof value.virtual==='boolean')gates.virtual=value.virtual;if(cmd==='logs.deleteCurrent'&&value?.restored===true)Object.assign(gates,value.restoredGates);}
 else if(cmd==='fs.exists')value=false; // Keep optional feature/capture assets absent; never enable development gates.
 else{value=await original.invoke(cmd,args);}
 row.reply=structuredClone(value);row.state='fulfilled';return value;
 }catch(e){row.state='rejected';row.reply=String(e instanceof Error?e.message:e);throw e;}}
