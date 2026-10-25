import { FanHostLifecycle, type FanHostProcess } from '../src/bridge/fanHost';
import type { FanApiAdapter } from '../src/bridge/fanApi';

type Context = { action: string; intentRevision: number; generation: number; hostPid: number; hostProcess?: FanHostProcess | null };
type Probe = {
  process: FanHostProcess | null; lastExitedProcess: FanHostProcess | null;
  stateValue: string; powerGeneration: number; controlIntentRevisionValue: number; enabled: boolean;
  admissionContext(action: string, revision: number): Context;
  adoptConfirmedExitForManualControl(context: Context, successor: FanHostProcess | null): boolean;
};
const results: Array<{name:string;passed:boolean}> = [];
function fixture() {
  const life = new FanHostLifecycle({ enabled:true, adapter:{enabled:true} as FanApiAdapter,
    launcher:{async start(){throw new Error('unexpected launcher call');},async stop(){throw new Error('unexpected stop');}},
    heartbeatIntervalMs:0,readNativeActivity:async()=>null,readNativePowerState:async()=>null });
  const p=life as unknown as Probe;
  const old:FanHostProcess={pid:51,executable:'fixture-host.exe',processCreated:'exact-fixture-birth'};
  p.process=old;p.powerGeneration=7;p.controlIntentRevisionValue=2;
  const context=p.admissionContext('apply',2);
  p.lastExitedProcess=old;p.process=null;p.stateValue='stopped';
  return {p,context,old};
}
function check(name:string,test:(f:ReturnType<typeof fixture>)=>boolean){
  const f=fixture();const passed=test(f);results.push({name,passed});console.log((passed?'PASS ':'FAIL ')+name);
}
check('exact confirmed predecessor permits latest manual request to continue from stopped',({p,context})=>p.adoptConfirmedExitForManualControl(context,null)&&context.hostPid===0&&context.hostProcess===null);
check('same PID/path/birth in a reconstructed object is not the captured process owner',({p,context,old})=>{context.hostProcess={...old};return !p.adoptConfirmedExitForManualControl(context,null)&&context.hostPid===51;});
check('a requested exit without completion is not authority',({p,context})=>{p.lastExitedProcess=null;return !p.adoptConfirmedExitForManualControl(context,null)&&context.hostPid===51;});
check('exit receipt for another exact owner is refused',({p,context,old})=>{p.lastExitedProcess={...old};return !p.adoptConfirmedExitForManualControl(context,null);});
check('old curve intent cannot inherit the new request',({p,context})=>{context.intentRevision=1;return !p.adoptConfirmedExitForManualControl(context,null);});
check('new power generation invalidates the transfer',({p,context})=>{p.powerGeneration=8;return !p.adoptConfirmedExitForManualControl(context,null);});
check('automatic resume does not acquire manual transfer authority',({p,context})=>{context.action='resume';return !p.adoptConfirmedExitForManualControl(context,null);});
check('disabled lifecycle refuses manual transfer',({p,context})=>{p.enabled=false;return !p.adoptConfirmedExitForManualControl(context,null);});
check('missing process with a non-stopped phase is not completed retirement',({p,context})=>{p.stateValue='ready';return !p.adoptConfirmedExitForManualControl(context,null);});
check('an unrelated successor cannot be substituted',({p,context})=>!p.adoptConfirmedExitForManualControl(context,{pid:77,executable:'other-host.exe'}));
check('exact current successor can be adopted after predecessor exit',({p,context})=>{const next={pid:77,executable:'fixture-successor.exe'};p.process=next;p.stateValue='awaiting-control';return p.adoptConfirmedExitForManualControl(context,next)&&context.hostProcess===next&&context.hostPid===77;});
check('PID mismatch is refused even with the correct owner object',({p,context})=>{context.hostPid=52;return !p.adoptConfirmedExitForManualControl(context,null);});
check('an origin-free context cannot borrow a previous exit receipt',({p,context})=>{context.hostProcess=null;context.hostPid=0;return !p.adoptConfirmedExitForManualControl(context,null);});
check('explicit preset uses the same confirmed exit rule',({p,context})=>{context.action='apply-preset';return p.adoptConfirmedExitForManualControl(context,null);});
const report={ok:results.every(r=>r.passed),checks:results.length,hardwareCalls:0,scope:'actual bridge transfer predicate; controlled in-memory contexts/process objects; no Host launch or hardware; end-to-end behavior tested separately',results};
console.log(JSON.stringify(report));process.exitCode=report.ok?0:1;
