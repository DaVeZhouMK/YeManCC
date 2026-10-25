/** Public recovery start with held initial/admission/loop state reads; inert Host/native/process. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanState,type FanNode} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const families=[['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve']] as const;
const stages=['rearm-probe','admission-probe','wait-loop'] as const;
const curve:FanNode[]=[0,40,70,100].map(tempC=>({tempC,dutyPercent:75}));
function held<T>(){let resolve!:(v:T)=>void,reject!:(e:Error)=>void,enter!:()=>void;return{promise:new Promise<T>((r,j)=>{resolve=r;reject=j;}),ready:new Promise<void>(r=>enter=r),enter:()=>enter(),resolve:(v:T)=>resolve(v),reject:(e:Error)=>reject(e)};}
const outcome=<T>(p:Promise<T>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
async function fixture(family:typeof families[number]=families[1]){
 const host=new NoIoHost();host.factoryType=family[1];host.completeResume=true;host.generation=6;host.attempt=1;host.powerState='On';let generation=6;
 const handshake=host.handshake.bind(host);host.handshake=async()=>({...await handshake(),fanRoute:family[2]});
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,resumeWaitDeadlineMs:1200,readNativeActivity:async()=>null,readNativePowerState:async()=>({generation,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true}),onControlDisplay:()=>{},launcher:{async start(){return{pid:118901,executable:'inert-probe-host.exe',adopted:true,processCreated:'probe-birth'};},async stop(){}}});
 function wake(value:number){generation=value;host.generation=value;host.attempt=1;host.powerState='On';life.setPowerGeneration(value);life.observePowerBoundary('resuming',value);life.observePowerBoundary('resume-ready',value);}
 await life.start({manualRecovery:true});await life.apply(curve);wake(7);host.stateName='Suspended';host.powerState='Suspended';host.opened=host.events=false;host.lease=null;
 const owner=life as any,rearm=owner.runRearmAfterWake.bind(owner),read=host.getState.bind(host),sync=owner.syncRemoteSessionState.bind(owner),finish=owner.finishResumeWait.bind(owner);
 const timeouts:Array<number|undefined>=[],outcomes:any[]=[];let churnAdvance:(()=>void)|null=null;
 let inRearm=false,ordinal=0,staleAdoptions=0,heldState:FanState|null=null,wait:ReturnType<typeof held<FanState>>|null=null,target=0,holdAfter=0,heldErrors=0;
 owner.finishResumeWait=(...args:any[])=>{outcomes.push(args[1]);return finish(...args);};
 owner.runRearmAfterWake=async(...args:any[])=>{inRearm=true;try{return await rearm(...args);}finally{inRearm=false;}};
 owner.syncRemoteSessionState=(s:FanState,...args:any[])=>{if(s===heldState)staleAdoptions++;return sync(s,...args);};
 host.getState=async(timeoutMs?:number)=>{if(!inRearm)return read();ordinal++;timeouts.push(timeoutMs);
  if(wait&&ordinal===target){const current=wait;wait=null;current.enter();return current.promise;}
  if(holdAfter&&ordinal>=holdAfter){heldErrors++;churnAdvance?.();life.observePowerBoundary('resuming',generation);life.observePowerBoundary('resume-ready',generation);return{...host.snapshot(),state:'Ready',resumePhase:'Ready',powerState:'On',openCalled:true,openEventsCalled:true};}
  if(target===3&&ordinal===2)return{...host.snapshot(),state:'Resuming',resumePhase:'Resuming',powerState:'Resuming',powerOperationStatus:'executing',controlAccepting:false};
  return read();};
 return{host,life,owner,wake,timeouts,outcomes,counts:()=>({rearmReads:ordinal,staleAdoptions,heldErrors}),
  prepare(stage:typeof stages[number]){target=stages.indexOf(stage)+1;wait=held<FanState>();heldState={...host.snapshot(),state:'Ready',resumePhase:'Ready',powerState:'On',controlAccepting:true,openCalled:true,openEventsCalled:true,hardwareWritesEnabled:true,lease:{leaseId:'old-read-lease',generation:77},leaseGeneration:77};return{wait,snapshot:heldState};},
  current(){host.stateName='AwaitingControl';host.powerState='On';host.opened=host.events=true;host.lease=null;},
  endlessFrom(n:number,advance?:(()=>void)){holdAfter=n;churnAdvance=advance??null;},
  async cleanup(){owner.runRearmAfterWake=rearm;owner.syncRemoteSessionState=sync;owner.finishResumeWait=finish;host.getState=read;await life.close().catch(()=>{});}};
}
const results:any[]=[];
async function check(name:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>,family:typeof families[number]=families[1]){const f=await fixture(family);try{results.push({name,ok:true,evidence:await work(f)});}catch(error){results.push({name,ok:false,error:String(error),counts:f.counts(),calls:[...f.host.calls]});}finally{await f.cleanup();}}
for(const family of families)for(const stage of stages)for(const shape of ['success','denial','timeout'] as const)await check(`${family[0]} ${stage} old ${shape} across one same-generation wake stays one accepted manual start`,async f=>{
 const{wait,snapshot}=f.prepare(stage),pending=outcome(f.life.start({manualRecovery:true}));await Promise.race([wait.ready,pending]);
 f.wake(7);f.current();if(shape==='success')wait.resolve(snapshot);else wait.reject(shape==='denial'?new FanApiError('retired recovery auth rejection',401,'AUTH_REJECTED'):new Error('retired recovery read timeout'));
 const reply=await pending;assert.equal(reply.error,null,'same-boundary control intent required another user click after a retired state read');assert.equal(f.counts().staleAdoptions,0,'old recovery state was synchronized');assert.notEqual(f.life.currentLease?.leaseId,'old-read-lease');
 await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{stage,shape,oldReadNotAdopted:true,initialManualRequestCompleted:true,currentManualWrites:1,reads:f.counts().rearmReads};
},family);
for(const stage of stages)for(const shape of ['success','denial'] as const)await check(`${stage} duplicate resume-ready preserves current ${shape}`,async f=>{
 const{wait,snapshot}=f.prepare(stage),pending=outcome(f.life.start({manualRecovery:true}));await Promise.race([wait.ready,pending]);f.life.observePowerBoundary('resume-ready',7);
 const error=new FanApiError('current read authentication rejected',401,'AUTH_REJECTED');if(shape==='success')wait.resolve(snapshot);else wait.reject(error);const reply=await pending;
 if(shape==='denial'){assert(reply.error,'current credential refusal became success');assert.equal(f.counts().staleAdoptions,0);}else{assert.equal(reply.error,null);assert.equal(f.counts().staleAdoptions,1);assert.equal(f.life.currentLease?.leaseId,'old-read-lease');}
 f.current();await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{stage,shape,currentBoundaryPreserved:true,currentManualWrites:1};
});
for(const stage of stages)for(const boundary of ['off','close','new-generation','new-curve','session','same-pid-object'] as const)await check(`${stage} old success cannot cross ${boundary}`,async f=>{
 const{wait,snapshot}=f.prepare(stage),pending=outcome(f.life.start({manualRecovery:true}));await Promise.race([wait.ready,pending]);const session=f.owner.sessionGeneration,process=f.owner.process;
 let after:ReturnType<typeof outcome>|undefined;
 if(boundary==='off'||boundary==='close')after=outcome(boundary==='off'?f.life.disable():f.life.close());else if(boundary==='new-generation'){f.wake(8);f.current();}else if(boundary==='new-curve')f.life.stagePendingControlCurve(curve.map(n=>({...n,dutyPercent:65})));else if(boundary==='session')f.owner.sessionGeneration++;else f.owner.process={...process,processCreated:'probe-other-birth'};
 try{wait.resolve(snapshot);const reply=await pending;await after;assert(reply.error,'retired recovery request was accepted');assert.equal(f.counts().staleAdoptions,0,'old recovery reply crossed the owner/cancellation boundary');if(stage==='wait-loop'){assert.equal(f.outcomes.length,1,'started waiter lost its one terminal outcome');assert(['cancelled','superseded'].includes(f.outcomes[0].reason),'retired waiter reported a false success');}
 if(boundary==='session'||boundary==='same-pid-object'){f.owner.sessionGeneration=session;f.owner.process=process;}f.current();await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{stage,boundary,identitySeamOnly:boundary==='session'||boundary==='same-pid-object',currentManualWrites:1};}
 finally{if(boundary==='session'||boundary==='same-pid-object'){f.owner.sessionGeneration=session;f.owner.process=process;}wait.resolve(snapshot);await pending;await after;}
});
for(const stage of stages)await check(`${stage} repeated wake read retirement is finite and cannot extend the original deadline`,async f=>{
 const descriptor=Object.getOwnPropertyDescriptor(globalThis,'performance')!,now={value:0};
 Object.defineProperty(globalThis,'performance',{value:{now:()=>now.value},configurable:true});
 const{wait,snapshot}=f.prepare(stage),pending=outcome(f.life.start({manualRecovery:true}));await Promise.race([wait.ready,pending]);f.wake(7);f.current();f.endlessFrom(stages.indexOf(stage)+2,()=>now.value+=1200);
 try{wait.resolve(snapshot);const reply=await pending;assert(reply.error,'two retired reads were accepted as a current session');assert.equal(f.counts().heldErrors,1,'probe refreshed repeatedly beyond its bound');assert.equal(f.counts().rearmReads,stages.indexOf(stage)+2,'deadline retirement started another unbudgeted GET');assert.equal(f.counts().staleAdoptions,0);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert(f.life.hasControlIntent,'budget exhaustion erased the user intent');
  const timeouts=f.timeouts.slice(stages.indexOf(stage));assert(timeouts.length===2&&timeouts[0]!>0&&timeouts[1]!>0&&timeouts[1]!<=timeouts[0]!,'refresh reset or enlarged its read timeout');
  if(stage==='wait-loop')assert.equal(f.outcomes.length,1,'deadline waiter lost its terminal outcome');
  f.endlessFrom(0);Object.defineProperty(globalThis,'performance',descriptor);f.current();await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{stage,refreshAttempts:1,probeTimeouts:timeouts,virtualClockAdvancedMs:now.value,currentManualWrites:1};
 }finally{f.endlessFrom(0);Object.defineProperty(globalThis,'performance',descriptor);wait.resolve(snapshot);await pending;}
});
for(const stage of stages)await check(`${stage} refresh spends only the remaining original read budget`,async f=>{
 const descriptor=Object.getOwnPropertyDescriptor(globalThis,'performance')!,clock={now:0};Object.defineProperty(globalThis,'performance',{value:{now:()=>clock.now},configurable:true});
 const{wait,snapshot}=f.prepare(stage),pending=outcome(f.life.start({manualRecovery:true}));await Promise.race([wait.ready,pending]);clock.now=200;f.wake(7);f.current();
 try{wait.resolve(snapshot);const reply=await pending;assert.equal(reply.error,null);const budgets=f.timeouts.slice(stages.indexOf(stage));assert.equal(budgets.length,2);assert.equal(budgets[1],budgets[0]!-200,'read refresh reset its timeout instead of spending the remaining budget');assert.equal(f.counts().staleAdoptions,0);Object.defineProperty(globalThis,'performance',descriptor);await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{stage,probeTimeouts:budgets,virtualFirstReadCostMs:200,currentManualWrites:1};}
 finally{Object.defineProperty(globalThis,'performance',descriptor);wait.resolve(snapshot);await pending;}
});
await check('initial rearm cannot keep probing through many cheap repeated wake edges',async f=>{
 const descriptor=Object.getOwnPropertyDescriptor(globalThis,'performance')!,clock={now:0};Object.defineProperty(globalThis,'performance',{value:{now:()=>clock.now},configurable:true});
 const{wait,snapshot}=f.prepare('rearm-probe'),pending=outcome(f.life.start({manualRecovery:true}));await Promise.race([wait.ready,pending]);f.wake(7);f.current();f.endlessFrom(2,()=>clock.now+=50);
 try{wait.resolve(snapshot);const reply=await pending;assert(reply.error);assert.equal(f.counts().rearmReads,2,'unsettled wake probe was retried more than once');assert.equal(f.counts().heldErrors,1);assert.equal(clock.now,50,'the probe spent the whole budget on fast repeating wake edges');assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert(f.life.recoveryActive,'exhausted probe lost the existing recovery owner');f.endlessFrom(0);Object.defineProperty(globalThis,'performance',descriptor);await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{reads:2,refreshes:1,virtualElapsedMs:50,retainedRecovery:true,currentManualWrites:1};}
 finally{f.endlessFrom(0);Object.defineProperty(globalThis,'performance',descriptor);wait.resolve(snapshot);await pending;}
});
console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,hardwareCalls:0,cpuBenchmark:false,scope:'public start/observePowerBoundary/disable/close/apply and real recovery wait; inert state/native/process; object/session replacement are injected seams, no hardware or timing benchmark'}));
if(results.some(x=>!x.ok))process.exitCode=1;
