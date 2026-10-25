/** Actual public start/apply rearm; count generation resolution on one immutable probe, no I/O. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {NoIoHost} from './fixtures/fan_no_io_host';
import type {FanNode,FanState} from '../src/bridge/fanApi';
const families=[['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve']] as const;
const curve:FanNode[]=[0,40,70,100].map(tempC=>({tempC,dutyPercent:75}));
const outcome=<T>(p:Promise<T>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
async function fixture(family:typeof families[number]=families[1]){
 const host=new NoIoHost();host.factoryType=family[1];host.completeResume=true;let generation=6;
 const handshake=host.handshake.bind(host);host.handshake=async()=>({...await handshake(),fanRoute:family[2]});
 let sink:(()=>void)|null=null;
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,resumeWaitDeadlineMs:800,readNativeActivity:async()=>null,
  readNativePowerState:async()=>({generation,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true}),
  onControlDisplay:()=>sink?.(),launcher:{async start(){return{pid:118801,executable:'inert-rearm-host.exe',adopted:true,processCreated:'rearm-birth'};},async stop(){}}});
 function wake(value:number){generation=value;host.generation=value;host.attempt=1;host.powerState='On';life.setPowerGeneration(value);life.observePowerBoundary('resuming',value);life.observePowerBoundary('resume-ready',value);}
 wake(6);await life.start({manualRecovery:true});await life.apply(curve);wake(7);host.stateName='AwaitingControl';host.opened=host.events=true;host.lease=null;
 const owner=life as any,resolve=owner.snapshotGenerationEvidence.bind(owner),originalRead=host.getState.bind(host),originalRearm=owner.runRearmAfterWake.bind(owner);
 const probes:FanState[]=[],counts=new Map<FanState,number>();let active=false,rearmReads=0,transform:(s:FanState,n:number)=>FanState=(s)=>s;
 owner.snapshotGenerationEvidence=(remote:FanState)=>{if(active)counts.set(remote,(counts.get(remote)??0)+1);return resolve(remote);};
 owner.runRearmAfterWake=async(...args:any[])=>{active=true;try{return await originalRearm(...args);}finally{active=false;}};
 host.getState=async()=>{const snapshot=await originalRead();if(!active)return snapshot;const result=transform(snapshot,++rearmReads);probes.push(result);return result;};
 return{host,life,owner,probes,counts,wake,setSink:(f:(()=>void)|null)=>sink=f,transform:(f:typeof transform)=>transform=f,
  async cleanup(){sink=null;owner.snapshotGenerationEvidence=resolve;owner.runRearmAfterWake=originalRearm;host.getState=originalRead;await life.close().catch(()=>{});}};
}
const results:any[]=[];
async function check(name:string,run:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>,family:typeof families[number]=families[1]){const f=await fixture(family);try{results.push({name,ok:true,evidence:await run(f)});}catch(error){results.push({name,ok:false,error:String(error),resolutions:f.probes.map(s=>f.counts.get(s)??0),calls:[...f.host.calls]});}finally{await f.cleanup();}}
for(const family of families)await check(`${family[0]} current completed rearm probe resolves once before sync and once after sync`,async f=>{
 const before=f.host.calls.length;const reply=await f.life.start({manualRecovery:true});assert(reply.writeReady);assert.equal(f.probes.length,1);assert.equal(f.counts.get(f.probes[0]),2,'rearm re-parsed the same post-sync generation evidence');assert.equal(f.host.calls.slice(before).filter(x=>x==='resume').length,0,'admissible current session was rebuilt');await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{probeReads:1,generationResolutions:2,automaticResumeRequests:0,currentManualWrites:1};
},family);
for(const family of families)for(const shape of ['idle','half-open'] as const)await check(`${family[0]} ${shape} current receipt retains original Open/OpenEvents admission`,async f=>{
 f.host.opened=f.host.events=false;f.host.stateName='AwaitingControl';f.transform((s,n)=>n===1?{...s,...(shape==='idle'?{powerOperationOutcome:'await-user'}:{recoveryReadbackRequired:true})}:s);
 const before=f.host.calls.length;await f.life.start({manualRecovery:true});await f.life.apply(curve);const calls=f.host.calls.slice(before);
 assert(calls.includes('open')&&calls.includes('events'),'incomplete current HC session skipped its original Open chain');assert.equal(f.host.calls.filter(x=>x==='enable').length,2);assert.equal(calls.filter(x=>x==='resume').length,0);return{shape,originalOpen:true,currentManualWrites:1};
},family);
for(const shape of ['stale','future','malformed','no-control','missing-open','superseded','unknown','cleanup'] as const)await check(`${shape} first rearm probe never becomes an admitted current session`,async f=>{
 f.transform((s,n)=>{if(n!==1)return s;const result={...s};if(shape==='stale')result.resumePhaseGeneration=result.powerOperationGeneration=6;if(shape==='future')result.resumePhaseGeneration=result.powerOperationGeneration=8;if(shape==='malformed')result.resumePhaseGeneration='invalid';if(shape==='no-control')result.controlAccepting=false;if(shape==='missing-open')result.openEventsCalled=false;if(shape==='superseded')result.powerOperationStatus='superseded';if(shape==='unknown')result.unknownState=true;if(shape==='cleanup')result.hcCloseCleanupPending=true;return result;});
 const before=f.host.calls.length;await outcome(f.life.start({manualRecovery:true}));const calls=f.host.calls.slice(before);
 assert(calls.includes('resume')||f.life.state!=='awaiting-control','untrusted probe silently took the no-op ready path');f.transform(s=>s);await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{shape,rejectedAsNoop:true,currentManualWrites:1};
});
for(const family of families)await check(`${family[0]} an actual display callback changing generation is rechecked after synchronization`,async f=>{
 f.host.stateName='Ready';f.host.lease={leaseId:'current-restored-lease',generation:91};let transitions=0;
 f.setSink(()=>{if(transitions===0){transitions++;f.wake(8);}});
 const before=f.host.calls.filter(x=>x==='enable').length;const reply=await outcome(f.life.start({manualRecovery:true}));
 assert.equal(transitions,1,'real display callback did not run during current snapshot synchronization');
 assert(reply.error,'pre-callback generation evidence was reused to return an admitted stale session');assert.equal(f.host.calls.filter(x=>x==='enable').length,before);
 f.setSink(null);f.host.generation=8;f.host.attempt=1;f.host.stateName='AwaitingControl';f.host.powerState='On';f.host.opened=f.host.events=true;f.host.lease=null;
 await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,before+1);return{realDisplayCallbackRan:true,postCallbackGenerationRechecked:true,currentManualWrites:1};
},family);
console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,hardwareCalls:0,cpuBenchmark:false,scope:'public startup/rearm/manual apply; actual resolver and coordinator with inert Host/native/process; family identities are shared-protocol coverage only'}));
if(results.some(x=>!x.ok))process.exitCode=1;
