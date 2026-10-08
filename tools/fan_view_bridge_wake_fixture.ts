/** Browser integration boundary only: real FanView + real FanHostLifecycle.
 * Adapter, process launcher, native power responses and settings use fixtures.
 * No device access, real Host launch, EC writes or changes to shared power policy.
 */
import { FanHostLifecycle, resolveFanEntryAction } from '../src/bridge/fanHost';
import { FanApiError, type FanApiAdapter, type FanState, type FanNode, type FanLease } from '../src/bridge/fanApi';
import type { PowerLifecycleState } from '../src/bridge/api';
export { resolveFanEntryAction };
const family = new URL(location.href).searchParams.get('family') || 'ROGAlly';
const curveFamilies = new Set(['ROGAlly', 'ClawA2VM', 'LegionGoTablet']);
const curves = {
  soft: [{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:0},{tempC:70,dutyPercent:20},{tempC:100,dutyPercent:80}],
  balanced: [{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:20},{tempC:70,dutyPercent:45},{tempC:100,dutyPercent:90}],
  aggressive: [{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:30},{tempC:70,dutyPercent:75},{tempC:100,dutyPercent:100}],
};
const fx = {
  holdAt: '', gateReached: '', releaseGate: null as (() => void) | null,
  async waitAt(op: string) {
    if (this.holdAt !== op) return;
    this.gateReached = op;
    await new Promise<void>(resolve => { this.releaseGate = resolve; });
    this.holdAt = '';
  },
  trace: [] as {op:string;generation:number;nodes?:FanNode[]}[],
  native: {generation:0,phase:'ready',inputReady:true,resumeReady:true,hardwareWritesAllowed:true,hibernateAvailable:true} as PowerLifecycleState,
  slowResumeMs: 0, fault: false, throwState: false, enableFailures: 0, heartbeatFailure: false,
  lastControlActive: false, lastNavDuty: 0,
  note(op:string,nodes?:readonly FanNode[]) { this.trace.push({op,generation:this.native.generation,nodes:nodes?.map(n=>({...n}))}); },
};
class Boundary implements FanApiAdapter {
  enabled = true;
  lease: FanLease|null = null;
  nextLease = 0;
  state = 'AwaitingControl';
  terminalFault = false;
  opened = false;
  eventsOpened = false;
  writes = false;
  writesObserved = false;
  closeComplete = true;
  restored = false;
  resumeGeneration = 0;
  private operationEpoch = 0;
  snapshot():FanState {
    return {state:(fx.fault||this.terminalFault)?'FaultLocked':this.state,powerState:this.state==='Suspended'?'Suspended':this.state==='Resuming'?'Resuming':'On',
      protocolVersion:2,hardwareWrites:this.writes,hardwareWritesEnabled:this.writes,
      hardwareWritesObserved:this.writesObserved,unknownState:this.terminalFault,hcCloseCleanupPending:false,
      openCalled:this.opened,openEventsCalled:this.eventsOpened,oemRestoreConfirmed:this.restored,
      hcVirtualCloseReturned:this.closeComplete,hcDeviceManagerStopCompleted:this.closeComplete,
      oemPhysicalOwnershipConfirmed:false,oemOwnershipStatus:'fixture-no-physical-proof',
      lease:this.lease?{...this.lease}:null,leaseGeneration:this.lease?.generation??null,
      controlAccepting:!this.terminalFault&&!fx.fault&&this.opened&&this.eventsOpened&&this.state!=='Resuming',
      resumePhase:this.state,resumePhaseGeneration:this.resumeGeneration,retryAfterMs:250};
  }
  async handshake() { fx.note('handshake');return {ok:true,supported:true,deviceClass:'HandheldCompanion.Devices.'+family,
    fanRoute:curveFamilies.has(family)?'ProfileCurve':'GenericDuty',fanRouteWriteReady:true,
    deviceIdentity:{manufacturer:family==='GPDWin5'?'GPD':'fixture',model:family,product:family}}; }
  async getState() { fx.note('state');if(fx.throwState)throw new Error('HOST_STATE_TIMEOUT');return this.snapshot(); }
  async open() { fx.note('Open');await fx.waitAt('Open');if(fx.fault||this.terminalFault)throw new FanApiError('FAULT_LOCKED',409,'FAULT_LOCKED');this.opened=true;this.closeComplete=false;return this.snapshot(); }
  async openEvents() { fx.note('OpenEvents');this.eventsOpened=true;this.state='AwaitingControl';return this.snapshot(); }
  async acquireControl() { fx.note('acquire');await fx.waitAt('acquire');if(!this.opened||!this.eventsOpened)throw new Error('fixture-HC-session-not-open');this.lease={leaseId:'fixture-'+(++this.nextLease),generation:this.nextLease};return {...this.lease}; }
  async heartbeat(id:string) { fx.note('heartbeat');await fx.waitAt('heartbeat');if(fx.heartbeatFailure||this.lease?.leaseId!==id)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');return {...this.lease}; }
  async enable(nodes:readonly FanNode[],id?:string) {
    if(fx.fault||this.terminalFault)throw new FanApiError('FAULT_LOCKED',409,'FAULT_LOCKED');
    if(fx.enableFailures>0){fx.enableFailures--;fx.note('enable-rejected');throw new FanApiError('POWER_RESUMING',409,'POWER_RESUMING');}
    if(!this.opened||!this.eventsOpened)throw new Error('fixture-write-before-HC-open');
    if(fx.native.phase==='suspending')throw new Error('fixture-write-during-suspending');
    if(this.lease?.leaseId!==id)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');
    fx.note('enable',nodes);this.writes=true;this.writesObserved=true;this.restored=false;this.state='Ready';return this.snapshot();
  }
  async applyPreset(_name:string,id?:string,nodes?:readonly FanNode[]) { return this.enable(nodes??curves.balanced,id); }
  async disable() { this.writes=false;return this.snapshot(); }
  async restoreOem() { if(this.terminalFault)throw new FanApiError('HC_OPERATION_TIMEOUT',504,'HC_OPERATION_TIMEOUT');fx.note('restore');this.writes=false;this.restored=true;return this.snapshot(); }
  async releaseControl() { fx.note('release');this.writes=false;this.restored=true;this.lease=null;this.state='AwaitingControl';return this.snapshot(); }
  async suspend() { this.closed('Suspended');return this.snapshot(); }
  async resume(request:any) {
    const generation=request.generation;fx.note('resume');
    if(generation!==fx.native.generation)throw new Error('fixture-stale-resume-generation');
    if(fx.native.phase==='suspending')throw new Error('fixture-resume-during-suspending');
    if(fx.fault||this.terminalFault)throw new FanApiError('FAULT_LOCKED',409,'FAULT_LOCKED');
    if(this.state==='Resuming'&&this.resumeGeneration===generation)return this.snapshot();
    this.state='Resuming';this.resumeGeneration=generation;const epoch=++this.operationEpoch;
    const complete=()=>{if(generation!==fx.native.generation||epoch!==this.operationEpoch||this.state!=='Resuming')return;fx.note('Open');this.opened=true;fx.note('OpenEvents');this.eventsOpened=true;this.closeComplete=false;this.restored=false;this.state='AwaitingControl';};
    if(fx.slowResumeMs>0)setTimeout(complete,fx.slowResumeMs);else complete();
    return this.snapshot();
  }
  closed(state:string) {this.operationEpoch++;this.state=state;this.opened=false;this.eventsOpened=false;this.writes=false;this.restored=false;this.closeComplete=true;this.lease=null;}
  async close() {fx.note('Close');this.closed('Stopped');this.terminalFault=false;return this.snapshot();}
  async shutdown() {fx.note('shutdown');}
}
const boundary = new Boundary();
export const fanHostLifecycle = new FanHostLifecycle({enabled:true,adapter:boundary,
  launcher:{async start(){fx.note('launch');return {pid:1234,executable:'fixture-FanHost.exe'};},async stop(){fx.note('stop');}},
  heartbeatIntervalMs:5000,resumeWaitDeadlineMs:1500,readNativePowerState:async()=>{fx.note('power-read');return {...fx.native};},
  readNativeActivity:async()=>null,requestManualWake:async(g)=>{
    fx.note('fanManualWake');
    if(g!==fx.native.generation)return {ok:false,generation:fx.native.generation,phase:fx.native.phase,reason:'stale_generation'};
    if(fx.native.phase==='suspending')return {ok:false,generation:g,phase:'suspending',reason:'power_suspending'};
    fx.native={...fx.native,phase:'ready',resumeReady:true,hardwareWritesAllowed:true};
    return {ok:true,generation:g,phase:'ready',admitted:true};
  }});
const setNative=(generation:number,phase:PowerLifecycleState['phase'])=>{fx.native={...fx.native,generation,phase,resumeReady:phase==='ready',hardwareWritesAllowed:phase==='ready'};};
Object.assign(fx,{
  lifecycle:fanHostLifecycle,boundary,curves,
  async sleep(generation:number,notify=true){
    setNative(generation,'suspending');boundary.closed('Suspended');boundary.resumeGeneration=generation;
    if(notify){fanHostLifecycle.setPowerGeneration(generation);fanHostLifecycle.observePowerBoundary('suspending',generation);
      window.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation}}));await fanHostLifecycle.suspend();}
    setNative(generation,'suspended');
  },
  async autoWake(){const g=fx.native.generation;setNative(g,'ready');fanHostLifecycle.observePowerBoundary('resuming',g);fanHostLifecycle.observePowerBoundary('resume-ready',g);await fanHostLifecycle.resume().catch(()=>{fx.note('resume-call-deferred');});},
  setNative,
});
(window as any).__chain=fx;