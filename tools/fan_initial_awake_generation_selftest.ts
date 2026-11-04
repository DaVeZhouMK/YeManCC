/** FAN-943: real bridge reproduction; all adapters are inert, no HC/native/device I/O. */
import { NoIoHost } from './fixtures/fan_no_io_host';
import { FanHostLifecycle, type FanHostLauncher } from '../src/bridge/fanHost';
import { FanApiError, type FanApiAdapter, type FanState, type FanLease, type FanNode } from '../src/bridge/fanApi';
import type { PowerLifecycleState } from '../src/bridge/api';

let assertions = 0;
function check(value: unknown, message: string): asserts value {
  assertions += 1;
  if (!value) throw new Error(message);
}
const curve: FanNode[] = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 100, dutyPercent: 90 }];
/** A replacement Host is a fresh process/session; do not carry the predecessor's closeCalled bit. */
function launcherFor(host: NoIoHost, counts={starts:0,stops:0}): FanHostLauncher {
  let starts = 0;
  return {
    start: async () => {
      starts += 1; counts.starts += 1;
      if (starts > 1) {
        host.closed = false;
        host.opened = false;
        host.events = false;
        host.lease = null;
        host.stateName = 'Stopped';
        host.powerState = 'Unknown';
        host.generation = 0;
        host.attempt = 0;
      }
      return { pid: 943 + starts, executable: 'no-io-fixture.exe' };
    },
    stop: async () => { counts.stops += 1; },
  };
}
const coldNative = (generation = 1, resumeReady = false): PowerLifecycleState => ({ generation, phase: 'ready', resumeReady,
  hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true });

function make(host: NoIoHost, read: () => Promise<PowerLifecycleState | null>, counts?:{starts:number;stops:number}) { return new FanHostLifecycle({ enabled: true,
  launcher: launcherFor(host,counts), adapter: host, heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800, readNativePowerState: read }); }
async function reject(work: Promise<unknown>, code: string) { await work.then(() => { throw new Error(`expected ${code}`); },
  error => check(error instanceof FanApiError && error.errorCode === code, `preserve ${code}: ${String(error)}`)); }
async function main() {
  // Original initial-awake reproduction: native's boot generation comes from
  // GetTickCount64()+1, so its numeric value cannot manufacture a sleep cycle.
  const initialScenarios:string[]=[];
  for(const family of ['XboxROGAllyX','OneXPlayerX1Mini'])for(const generation of [1,16860,37594165]){
    const name=`${family}: boot ${generation} Host-zero first adjustment`,host=new NoIoHost(),counts={starts:0,stops:0};host.factoryType='HandheldCompanion.Devices.'+family;
    let native:PowerLifecycleState|null=null;const life=make(host,async()=>native,counts),before=assertions;
    const writtenCurves:FanNode[][]=[],enable=host.enable.bind(host);host.enable=async(nodes?:readonly FanNode[])=>{writtenCurves.push(nodes?.map(node=>({...node}))??[]);return enable();};
    try{
      await life.start();await life.apply(curve);
      check(host.calls.filter(c=>c==='enable').length===1,name+': first enable succeeded before native observation');
      native=coldNative(generation);const first=await life.start({manualRecovery:true});check(first.allowed,name+': first manual native adoption is allowed');
      const adjusted=curve.map(node=>({...node,dutyPercent:100}));await life.apply(adjusted);
      check(host.calls.filter(c=>c==='enable').length===2&&JSON.stringify(writtenCurves[1])===JSON.stringify(adjusted),name+': first manual 100 percent curve really reaches second Enable without another start');
      await life.applyPreset('aggressive',adjusted);
      check(!host.calls.includes('resume'),name+': initial awake Host-zero must not fabricate resume');
      check(host.calls.filter(c=>c==='preset').length===1,name+': next 100 percent preset really submitted');
      check(life.controlReady,name+': next preset remains usable');
      await life.start({manualRecovery:true});check(life.controlReady&&!host.calls.includes('resume'),name+': repeated same boot observation retains its established baseline');
      const token=life.currentLease?.leaseId;await life.disable();await life.start({manualRecovery:true});await life.apply(curve);
      check(life.currentLease?.leaseId!==token,name+': disable then re-enable uses fresh lease');
      check(host.calls.filter(c=>c==='enable').length===3,name+': re-enable actually submitted');
      check(host.calls.filter(c=>c==='open').length===1,name+': reuse current open session without duplicate Open');
      check(!host.calls.includes('resume'),name+': no phantom resume after disabling');
      check(counts.starts===1&&counts.stops===0&&!host.calls.includes('close'),name+': no Close or process replacement before cleanup');
      check(host.generation===0&&host.attempt===0,name+': boot baseline never invents a Host resume receipt');
      life.observePowerBoundary('suspending',generation);life.observePowerBoundary('resuming',generation);life.observePowerBoundary('resume-ready',generation);
      check((life as any).classifyControlAdmission(host.snapshot(),true)!=='accepting',name+': real sleep at this generation revokes Host-zero baseline');
      life.setPowerGeneration(generation+1);life.observePowerBoundary('resuming',generation+1);life.observePowerBoundary('resume-ready',generation+1);
      check((life as any).classifyControlAdmission(host.snapshot(),true)!=='accepting',name+': new generation cannot borrow initial Open evidence');
      initialScenarios.push(name);console.log('PASS '+name+' assertions='+(assertions-before));
    }finally{await life.close().catch(()=>{});}
    const coldName=`${family}: boot ${generation} cold initial-native adoption`,cold=new NoIoHost(),coldCounts={starts:0,stops:0};cold.factoryType=host.factoryType;
    const coldLife=make(cold,async()=>coldNative(generation),coldCounts),coldBefore=assertions;
    try{
      const first=await coldLife.start({manualRecovery:true});check(first.allowed,coldName+': first start independently allowed');await coldLife.apply(curve);
      check(['open','events','acquire','enable'].every(call=>cold.calls.filter(value=>value===call).length===1),coldName+': first action reaches original Open Events acquire and Enable once');
      check(!cold.calls.includes('resume')&&!cold.calls.includes('close')&&coldCounts.starts===1&&coldCounts.stops===0,coldName+': no fictitious resume Close or replacement');
      check(cold.generation===0&&cold.attempt===0,coldName+': actual Host receipt remains zero');
      initialScenarios.push(coldName);console.log('PASS '+coldName+' assertions='+(assertions-coldBefore));
    }finally{await coldLife.close().catch(()=>{});}
  }
  // A one-shot ready/committed notification is still a real wake fact when
  // both sleep and resuming were missed. Withdraw only the initial baseline;
  // an actual current-generation Open binding must survive these late edges.
  {
    const name='XboxROGAllyX: ready-only wake revokes initial baseline but preserves explicit Open',host=new NoIoHost();host.factoryType='HandheldCompanion.Devices.XboxROGAllyX';
    let power:PowerLifecycleState|null=null;const life=make(host,async()=>power),before=assertions;
    try{
      await life.start();await life.apply(curve);power=coldNative(16860);await life.start({manualRecovery:true});
      check((life as any).initialAwakePowerGeneration===16860,name+': real initial baseline was established before the notification');
      life.observePowerBoundary('resume-ready',16860);life.observePowerBoundary('resumed',16860);
      check((life as any).initialAwakePowerGeneration===null&&(life as any).observedPowerTransition===true,name+': missed earlier events cannot retain initial-awake permission');
      check((life as any).classifyControlAdmission(host.snapshot(),true)!=='accepting',name+': old Host-zero cannot bypass a real ready-only wake');
    }finally{await life.close().catch(()=>{});}
    const fresh=new NoIoHost(),freshLife=make(fresh,async()=>coldNative(16860,true));fresh.factoryType=host.factoryType;
    try{
      freshLife.setPowerGeneration(16860);freshLife.observePowerBoundary('resume-ready',16860);await freshLife.start({manualRecovery:true});await freshLife.apply(curve);
      const binding=(freshLife as any).explicitOpenBinding;check(!!binding,name+': actual current Open established explicit proof');
      freshLife.observePowerBoundary('resume-ready',16860);freshLife.observePowerBoundary('resumed',16860);
      check((freshLife as any).explicitOpenBinding===binding&&(freshLife as any).classifyControlAdmission(fresh.snapshot(),true)==='accepting',name+': late completion retains actual Open proof');
      initialScenarios.push(name);console.log('PASS '+name+' assertions='+(assertions-before));
    }finally{await freshLife.close().catch(()=>{});}
  }
  for(const generation of [1,16860,37594165])for(const denial of ['ready-no-write','unknown','missing-resume-ready'] as const){
    const name=`XboxROGAllyX: boot ${generation} rejects ${denial} baseline`,host=new NoIoHost(),before=assertions;
    const observed=denial==='unknown'?null:(denial==='missing-resume-ready'?{...coldNative(generation),resumeReady:undefined}:{...coldNative(generation),hardwareWritesAllowed:false}) as PowerLifecycleState,life=make(host,async()=>observed);
    (life as any).synchronizeObservedPowerSnapshot(observed);life.setPowerGeneration(generation);
    check((life as any).initialAwakePowerGeneration===null,name+': no unverified boot baseline is created');
    host.opened=host.events=true;host.stateName='Ready';check((life as any).classifyControlAdmission(host.snapshot(),true)!=='accepting',name+': Host zero stays denied without native evidence');
    check(host.calls.length===0,name+': observation does not issue control writes');
    initialScenarios.push(name);console.log('PASS '+name+' assertions='+(assertions-before));
  }

  // Genuine sleep/new generation revokes the baseline, not just the old lease.
  const wake = new NoIoHost(); let wakeNative = coldNative(); const wakeLife = make(wake, async () => wakeNative);
  await wakeLife.start({ manualRecovery: true }); await wakeLife.apply(curve);
  wakeLife.setPowerGeneration(2); wakeLife.observePowerBoundary('suspending', 2); await wakeLife.suspend();
  wakeLife.observePowerBoundary('resuming', 2); wakeLife.observePowerBoundary('resume-ready', 2);
  wake.completeResume = true; wakeNative = coldNative(2, true);
  const before = wake.calls.filter(c => c === 'enable').length;
  await wakeLife.start(); await wakeLife.apply(curve);
  check(wake.calls.includes('resume'), 'real sleep requires existing resume owner');
  check(wake.generation === 2 && wake.attempt === 1, 'resume receipt belongs to generation 2');
  check(wake.calls.filter(c => c === 'enable').length === before + 1, 'real wake writes once after admission');
  await wakeLife.close();

  // A new successor with a current awake boundary has no Host-side resume receipt to
  // borrow. Manual control must use the original Open/OpenEvents chain. A prior/foreign
  // power receipt remains a hard negative and must not be borrowed by the successor.
  for (const mode of ['wake-fact', 'generation-2', 'observed-sleep', 'real-old-receipt', 'contradictory-zero'] as const) {
    const old = new NoIoHost(); old.opened = old.events = true; old.stateName = 'Ready';
    let staleNative: PowerLifecycleState | null = null; const staleCounts={starts:0,stops:0};
    const staleLife = make(old, async () => staleNative, staleCounts);
    await staleLife.start();
    staleNative = coldNative(mode === 'generation-2' ? 2 : 1, mode === 'wake-fact');
    if (mode === 'real-old-receipt') { old.attempt = 1; }
    if (mode === 'contradictory-zero') { const original = old.snapshot.bind(old); old.snapshot = () => ({ ...original(), powerOperationGeneration: 2 }); }
    if (mode === 'observed-sleep') { staleLife.observePowerBoundary('suspending', 1); staleLife.observePowerBoundary('resuming', 1); staleLife.observePowerBoundary('resume-ready', 1); }
    let manualError: unknown = null;
    try {
      await staleLife.start({ manualRecovery: true });
      await staleLife.apply(curve);
    } catch (error) {
      manualError = error;
    }
    const attemptedWrite = old.calls.includes('acquire') || old.calls.includes('enable');
    if (mode === 'real-old-receipt' || mode === 'contradictory-zero') {
      check(manualError instanceof FanApiError && manualError.errorCode === 'HC_RESUME_RESULT_EXPIRED',
        `${mode}: foreign/old resume receipt remains rejected`);
      check(!attemptedWrite, `${mode}: rejected receipt must not write`);
    } else if(mode==='generation-2') {
      // No power edge was observed and resumeReady is explicitly false. The
      // first native boot value 2 is an initial baseline, not a sleeping Host.
      check(manualError===null&&old.calls.filter(call=>call==='acquire').length===1&&old.calls.filter(call=>call==='enable').length===1,
        `${mode}: initial boot adoption submits acquire and Enable on the existing HC session`);
      check(!old.calls.some(call=>['open','events','resume','close'].includes(call))&&staleCounts.starts===1&&staleCounts.stops===0,
        `${mode}: no phantom resume Close reopen or process replacement`);
      check(old.generation===0&&old.attempt===0,`${mode}: initial boot baseline preserves the Host zero receipt`);
    } else {
      check(manualError === null && old.calls.includes('open') && old.calls.includes('events')
        && old.calls.includes('acquire') && old.calls.includes('enable'),
        `${mode}: fresh successor must reach original Open/OpenEvents/acquire/enable`);
    }
    await staleLife.close();
  }
  // Expiry and real terminal resume errors must survive; never become a 15s Ready deadline.
  for (const code of ['HC_RESUME_RESULT_EXPIRED', 'API_SESSION_REQUIRED', 'FAN_ROUTE_CONFLICT']) {
    const blocked = new NoIoHost(); blocked.resumeFailure = new FanApiError(code, code === 'API_SESSION_REQUIRED' ? 401 : 409, code);
    let blockedNative: PowerLifecycleState | null = null;
    const blockedLife = make(blocked, async () => blockedNative);
    // These are errors from an EXISTING HC session, unlike a never-opened cold
    // Host that has no earlier power operation and must enter its original Open.
    await blockedLife.start(); await blockedLife.apply(curve);
    const writeCount = blocked.calls.filter(c => c === 'enable' || c === 'acquire').length;
    blockedNative = coldNative(2, true);
    const now = performance.now(); await reject(blockedLife.start({ manualRecovery: true }), code);
    check(performance.now() - now < 500, `${code}: no bounded recovery wait`);
    check(blocked.calls.filter(c => c === 'enable' || c === 'acquire').length === writeCount, `${code}: no additional write`);
    await blockedLife.close();
  }
  console.log(JSON.stringify({ ok: true, assertions, initialScenarios, fixture: 'no HC/native/device I/O', cases: 'boot generations 1/16860/37594165 with Host receipt zero; first manual preset and cold adoption; real wake, stale/contradictory receipt, terminal resume errors' }));
}
void main();
