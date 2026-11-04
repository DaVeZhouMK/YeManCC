/** FAN-943: parameterize the production common lifecycle across the frozen HC route domain.
 * These are software contracts using inert adapters, NOT physical device tests. */
import { readFileSync } from 'node:fs';
import { FanHostLifecycle, resolveFanHostConfig, type FanHostLauncher } from '../src/bridge/fanHost';
import { FanApiError, HttpFanApiAdapter, type FanState, type FanNode } from '../src/bridge/fanApi';
import { hasRetiredSourceFanResources, hasSafeSourceFanRelease, resolveFanSnapshotGeneration, isColdUnopenedFanHost, isCompleteAdmissibleHcSession } from '../src/bridge/fanSessionEvidence';
import { NoIoHost } from './fixtures/fan_no_io_host';
import { http, type PowerLifecycleState, type HttpResponse } from '../src/bridge/api';
let assertions = 0;
function check(value: unknown, message: string): asserts value { ++assertions; if (!value) throw new Error(message); }
const curve: FanNode[] = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 70, dutyPercent: 65 }, { tempC: 100, dutyPercent: 90 }];
const launcher: FanHostLauncher = { start: async () => ({ pid: 943, executable: 'no-io.exe' }), stop: async () => {} };
const native = (generation: number): PowerLifecycleState => ({ generation, phase: 'ready', resumeReady: generation > 1,
  hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true });
const matrix = readFileSync('FanLab/evidence/HC-DEVICE-MATRIX-BATCH03-20260817.md', 'utf8');
const families = [...matrix.matchAll(/^\| `([^`]+)` .*\| `SourceMappedFan` \|$/gm)].map(m => m[1]);
const unsupported = [...matrix.matchAll(/^\| `([^`]+)` .*\| `UnsupportedNoFanCapability` \|$/gm)].map(m => m[1]);
function make(host: NoIoHost, read: () => Promise<PowerLifecycleState | null>) { return new FanHostLifecycle({ enabled: true, launcher,
  adapter: host, heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800, readNativePowerState: read }); }
const current = (generation: number): FanState => ({ state: 'Ready', resumePhase: 'Ready', powerState: 'On', hardwareWrites: false,
  resumePhaseGeneration: generation, powerOperationGeneration: generation, powerOperationAttempt: 1, powerOperationStatus: 'completed',
  fanCapabilitySupported: true, controlAccepting: true, openCalled: true, openEventsCalled: true, unknownState: false, hcCloseCleanupPending: false });
const zero = (): FanState => ({ ...current(0), powerState: 'Unknown', powerOperationAttempt: 0, powerOperationStatus: undefined });
async function main() {
  check(families.length === 70 && unsupported.length === 10, 'frozen route domain stays 70 mapped / 10 unsupported');
  const source:FanState={...current(1),state:'Stopped',hardwareWrites:false,hardwareWritesEnabled:false,hardwareWritesObserved:true,
    openCalled:false,openEventsCalled:false,controlAccepting:false,lease:null,sourceFanMode:true,sourceFanOwnerEntered:true,
    sourceFanClosePending:false,sourceFanClosed:true,sourceFanAutomaticReturned:true,sourceFanGeneration:7,
    hcVirtualCloseReturned:false,hcDeviceManagerStopCompleted:false,oemRestoreConfirmed:false,oemPhysicalOwnershipConfirmed:false,
    recoveryClaimEvidence:{sourceFanGeneration:7,oemReleaseActionKind:'proven-by-source-auto-register-and-terminal-close',oemReleaseActionClass:'release-action-proven'}};
  check(hasSafeSourceFanRelease(source,7),'source return consumes automatic readback plus exact terminal closure without fake HC flags');
  check(!hasSafeSourceFanRelease(source,8),'source return cannot retire a successor generation');
  for(const delta of [{sourceFanClosePending:true},{sourceFanClosed:false},{sourceFanGeneration:8},
    {sourceFanAutomaticReturned:false},{hardwareWrites:true},{hardwareWritesEnabled:true},{openCalled:true},{openEventsCalled:true},
    {hcCloseCleanupPending:true},{unknownState:true},{sourceFanMode:false},
    {recoveryClaimEvidence:{sourceFanGeneration:7,oemReleaseActionKind:'proven-by-scope-receipts',oemReleaseActionClass:'release-action-proven'}}])
    check(!hasSafeSourceFanRelease({...source,...delta}),'source incomplete or mismatched evidence refuses release');
  check(hasRetiredSourceFanResources({...source,unknownState:true,sourceFanAutomaticReturned:false}), 'source retired resources can retain unproven release debt');
  check(!hasSafeSourceFanRelease({...source,unknownState:true,sourceFanAutomaticReturned:false}), 'resource retirement cannot substitute for release');
  const noWrite={...source,hardwareWritesObserved:false,sourceFanAutomaticReturned:false};
  check(hasSafeSourceFanRelease(noWrite),'source no-write retirement does not invent an automatic-mode write');
  for(const bad of [undefined,null,'true',1]) check(!hasSafeSourceFanRelease({...source,sourceFanClosed:bad} as FanState),'missing or malformed source closure fails closed');
  {
    const host=new NoIoHost(),life=make(host,async()=>native(1));
    await life.start();await life.apply(curve);
    const close=host.close.bind(host);host.close=async()=>{await close();return {...source,sourceFanGeneration:7};};
    (life as any).sourceSessionGeneration=7;
    await life.close();check(host.calls.includes('shutdown'),'actual bridge close consumes honest source return');
  }
  {
    const host=new NoIoHost(),life=make(host,async()=>native(1));
    await life.start();await life.apply(curve);
    const close=host.close.bind(host);host.close=async()=>{await close();return source;};
    (life as any).sourceSessionGeneration=8;
    let rejected=false;try{await life.close();}catch(error){rejected=String(error).includes('SOURCE_FAN_GENERATION_MISMATCH');}
    check(rejected&&!host.calls.includes('shutdown'),'actual bridge refuses old-generation source close before shutdown');
  }
  let transitions = 0;
  for (const family of families) {
    const host = new NoIoHost(); host.factoryType = family === 'OneXPlayerApex'
      ? 'HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex' : `HandheldCompanion.Devices.${family}`;
    let power: PowerLifecycleState | null = null; const life = make(host, async () => power);
    await life.start(); await life.apply(curve); power = native(1);
    await life.start(); await life.applyPreset('aggressive', curve); await life.disable(); await life.start(); await life.apply(curve);
    check(host.calls.filter(c => c === 'enable').length === 2 && host.calls.filter(c => c === 'preset').length === 1, `${family}: every submitted awake control effective`);
    check(!host.calls.includes('resume'), `${family}: no invented initial sleep/resume`);
    // The actual production classifier is the common entrance used by waits, adoption and rearm.
    const classify = (remote: FanState) => (life as any).classifyControlAdmission(remote, true);
    for (const [name, delta] of [
      ['explicit-false', { controlAccepting: false }], ['future', { resumePhaseGeneration: 2, powerOperationGeneration: 2 }],
      ['conflicting', { resumePhaseGeneration: 1, powerOperationGeneration: 2 }],
      ['pending', { powerOperationStatus: 'pending' }], ['executing', { powerOperationStatus: 'executing' }],
      ['failed', { powerOperationStatus: 'failed' }], ['superseded', { powerOperationStatus: 'superseded' }],
      ['not-open', { openCalled: false }], ['no-events', { openEventsCalled: false }],
      ['cleanup', { hcCloseCleanupPending: true }], ['unknown', { unknownState: true }],
      ['resuming-power', { powerState: 'Resuming' }], ['suspended-power', { powerState: 'Suspended' }],
    ] as Array<[string, Partial<FanState>]>) {
      check(classify({ ...current(1), ...delta }) !== 'accepting', `${family}: ${name} cannot pass Ready`); ++transitions;
    }
    const originalState = host.getState.bind(host);
    host.getState = async () => ({ ...await originalState(), controlAccepting: false });
    const reattachWrites = host.calls.filter(c => ['enable', 'acquire', 'preset'].includes(c)).length;
    check(!await life.reattachExistingControlSession(), `${family}: cached lease cannot bypass explicit Host denial during reattach`);
    check(host.calls.filter(c => ['enable', 'acquire', 'preset'].includes(c)).length === reattachWrites, `${family}: blocked reattach has no ownership/control writes`);
    host.getState = async () => { throw new FanApiError('denied read', 401, 'API_SESSION_REQUIRED'); };
    const callbacksBeforeDenial = host.calls.filter(c => ['resume', 'close', 'open', 'events', 'acquire', 'enable', 'preset'].includes(c)).length;
    await life.start({ manualRecovery: true }).then(() => { throw new Error('authorization refusal was swallowed'); },
      error => check(error instanceof FanApiError && error.errorCode === 'API_SESSION_REQUIRED', `${family}: state read denial preserves its original code`));
    check(host.calls.filter(c => ['resume', 'close', 'open', 'events', 'acquire', 'enable', 'preset'].includes(c)).length === callbacksBeforeDenial,
      `${family}: denied state read is not Host absence and triggers no replacement/close/resume`);
    host.getState = originalState;
    // Late snapshots cannot mutate current flags or lease, including a future/contradictory snapshot.
    const before = { opened: (life as any).opened, events: (life as any).eventsOpened, lease: life.currentLease?.leaseId };
    for (const delta of [{ resumePhaseGeneration: 0, powerOperationGeneration: 0 },
      { resumePhaseGeneration: 2, powerOperationGeneration: 2 }, { resumePhaseGeneration: 1, powerOperationGeneration: 2 }]) {
      (life as any).syncRemoteSessionState({ ...current(1), ...delta, openCalled: false, openEventsCalled: false, lease: null, leaseGeneration: null });
      check((life as any).opened === before.opened && (life as any).eventsOpened === before.events && life.currentLease?.leaseId === before.lease,
        `${family}: stale/future/conflicting snapshot cannot poison live session`);
    }
    // A release/expiry may remove lease during Restore; the saved cleanup token must remain valid.
    const restore = host.restoreOem.bind(host); host.restoreOem = async () => { const result = await restore(); host.lease = null; return { ...result, lease: null, leaseGeneration: null }; };
    await life.disable(); check(host.calls.includes('release'), `${family}: release follows Restore even if lease was cleared`);
    await life.close();

    const shell = new NoIoHost(); shell.factoryType = host.factoryType;
    let shellPower: PowerLifecycleState | null = null; const shellLife = make(shell, async () => shellPower);
    await shellLife.start(); shellPower = native(1);
    await shellLife.start({ manualRecovery: true }); await shellLife.apply(curve);
    check(!shell.calls.includes('resume') && shell.calls.filter(c => c === 'open').length === 1
      && shell.calls.filter(c => c === 'events').length === 1 && shell.calls.filter(c => c === 'enable').length === 1,
      `${family}: handshake-only cold Host enters the original Open/OpenEvents chain, not phantom Resume`);
    await shellLife.close();
    // FAN-944: the app has already slept, but this Host did not exist until the
    // first manual enable. A new Host has no resume receipt for that prior sleep.
    for (const shellState of ['AwaitingControl', 'Stopped']) {
      const afterWake = new NoIoHost(); afterWake.factoryType = host.factoryType; afterWake.stateName = shellState;
      if (shellState === 'Stopped') {
        const open = afterWake.open.bind(afterWake), events = afterWake.openEvents.bind(afterWake);
        afterWake.open = async () => { afterWake.stateName = 'Open'; return open(); };
        afterWake.openEvents = async () => { afterWake.stateName = 'AwaitingControl'; return events(); };
      }
      const afterWakeLife = make(afterWake, async () => native(2));
      afterWakeLife.observePowerBoundary('suspending', 2);
      afterWakeLife.observePowerBoundary('resuming', 2);
      afterWakeLife.observePowerBoundary('resume-ready', 2);
      await afterWakeLife.start({ manualRecovery: true }); await afterWakeLife.apply(curve);
      check(!afterWake.calls.includes('resume') && afterWake.calls.filter(c => c === 'open').length === 1
        && afterWake.calls.filter(c => c === 'events').length === 1 && afterWake.calls.filter(c => c === 'acquire').length === 1
        && afterWake.calls.filter(c => c === 'enable').length === 1,
        `${family}/${shellState}: first Host after prior sleep must Open once without inventing a resume receipt`);
      await afterWakeLife.start(); await afterWakeLife.applyPreset('soft', curve);
      check(!afterWake.calls.includes('resume') && afterWake.calls.filter(c => c === 'preset').length === 1,
        `${family}/${shellState}: the next manual adjustment remains usable after first-enable admission`);
      await afterWakeLife.close();
    }
    // A fresh Host opened AFTER a real wake still has zero power receipts. Its
    // own Open/OpenEvents bind this exact session; a later preset must not call phantom Resume.
    const fresh = new NoIoHost(); fresh.factoryType = host.factoryType;
    const freshLife = make(fresh, async () => native(2)); freshLife.setPowerGeneration(2);
    await freshLife.start(); await freshLife.apply(curve); await freshLife.start(); await freshLife.applyPreset('soft', curve);
    check(!fresh.calls.includes('resume') && fresh.calls.filter(c => c === 'preset').length === 1, `${family}: post-wake replacement has its own completed Open proof`);
    check(await freshLife.reattachExistingControlSession(), `${family}: active session reattach uses the same evidence`);
    // Positive old receipts may not borrow the new Open binding.
    check((freshLife as any).classifyControlAdmission(current(1), true) !== 'accepting', `${family}: positive old receipt stays denied`);
    freshLife.observePowerBoundary('suspending', 2); freshLife.observePowerBoundary('resuming', 2); freshLife.observePowerBoundary('resume-ready', 2);
    check((freshLife as any).classifyControlAdmission(zero(), true) !== 'accepting', `${family}: a real boundary revokes the explicit-open binding`);
    await freshLife.close();
  }
  for (const family of unsupported) {
    const host = new NoIoHost(); const original = host.handshake.bind(host); host.handshake = async () => ({ ...await original(), supported: false, fanRouteWriteReady: false, deviceClass: `HandheldCompanion.Devices.${family}` });
    const life = make(host, async () => null); await life.start().catch(() => undefined);
    check(!host.calls.some(c => ['open', 'events', 'acquire', 'enable', 'preset'].includes(c)), `${family}: unsupported handshake has no control callback`);
    await life.close();
  }
  // A later same-generation curve owns a fresh rearm context, not the
  // cancelled earlier request's waiter. There is still only one resume owner.
  {
    const host = new NoIoHost(); const life = make(host, async () => native(1));
    await life.start(); host.generation = 1; host.attempt = 1; host.stateName = 'Resuming';
    host.opened = host.events = false;
    life.setPowerGeneration(1); life.observePowerBoundary('resuming', 1); life.observePowerBoundary('resume-ready', 1);
    const old = (life as any).rearmAfterWakeForControlAdmission('old-intent', true);
    const oldResult = old.then(() => 'resolved', () => 'cancelled');
    for (let spins = 0; spins < 30 && !(life as any).resumeWaitTask; ++spins) await Promise.resolve();
    check(!!(life as any).resumeWaitTask, 'old same-generation rearm waiter actually exists');
    // Use the production staging entry to retire the earlier rearm waiter.
    life.stagePendingControlCurve(curve);
    const latestRearm = (life as any).rearmAfterWakeForControlAdmission('new-intent', true);
    host.stateName = 'Ready'; host.opened = host.events = true;
    await latestRearm;
    await life.apply(curve);
    check(await oldResult === 'cancelled', 'old intent cannot claim the new curve');
    check(host.calls.filter(c => c === 'enable').length === 1, 'latest curve submitted exactly once');
    check(host.calls.filter(c => c === 'resume').length <= 1, 'same-generation rearm never drives parallel resume owners');
    await life.close();
  }
  // A zero receipt is insufficient unless the entire never-opened contract is present.
  {
    const empty = new NoIoHost().snapshot();
    check(isColdUnopenedFanHost(empty), 'current unopened AwaitingControl shell is eligible only for original Open');
    check(isColdUnopenedFanHost({ ...empty, state: 'Stopped', resumePhase: 'Stopped', controlAccepting: false }),
      'production Stopped shell may enter Open without claiming it already accepts control');
    for (const delta of [
      { openCalled: true }, { openEventsCalled: true }, { closeCalled: true }, { closeCalled: undefined },
      { hardwareWritesObserved: true }, { hardwareWritesObserved: undefined }, { hardwareWritesEnabled: true },
      { lease: { leaseId: 'other-owner', generation: 1 } }, { lease: undefined },
      { unknownState: true }, { hcCloseCleanupPending: true }, { fanCapabilitySupported: false },
      { controlAccepting: false }, { powerOperationAttempt: 1 }, { powerOperationStatus: 'failed' },
      { powerOperationGeneration: 2 }, { resumePhaseGeneration: 2 }, { powerState: 'Suspended' },
      { state: 'Ready', resumePhase: 'Ready' }, { state: 'Stopped', resumePhase: 'Stopped', controlAccepting: true },
    ]) check(!isColdUnopenedFanHost({ ...empty, ...delta }), 'dirty/closed/leased/stale/unsupported shell must not bypass real recovery');
    for (const nativeDelta of [{ generation: 3 }, { phase: 'suspended' }, { phase: 'suspending' }, { hardwareWritesAllowed: false }]) {
      const blocked = new NoIoHost();
      const blockedLife = make(blocked, async () => ({ ...native(2), ...nativeDelta } as PowerLifecycleState));
      blockedLife.setPowerGeneration(2); blockedLife.observePowerBoundary('resuming', 2); blockedLife.observePowerBoundary('resume-ready', 2);
      try { await blockedLife.start({ manualRecovery: true }); } catch {}
      check(!blocked.calls.some(c => ['open', 'events', 'acquire', 'enable'].includes(c)),
        'non-current/sleeping/write-blocked native boundary does not admit cold Host writes');
      await blockedLife.close();
    }
  }
  for (let local = 0; local <= 71; ++local) for (let actual = 0; actual <= 71; ++actual) {
    const result = resolveFanSnapshotGeneration(current(actual), { powerGeneration: local, initialAwakeGeneration: null, explicitOpenGeneration: null });
    check((result.freshness === 'current') === (local === actual), 'receipt generations match exactly, not >=');
  }
  for (const value of [-1, 0.5, NaN, Infinity, '1', null]) {
    check(resolveFanSnapshotGeneration({ ...current(1), resumePhaseGeneration: value }, { powerGeneration: 1, initialAwakeGeneration: 1, explicitOpenGeneration: 1 }).freshness === 'invalid', 'malformed supplied generation cannot become missing legacy evidence');
  }
  check(isCompleteAdmissibleHcSession(zero()), 'zero itself can be a complete opened session, not a resume receipt');
  const actualSourceChains=(globalThis as {ymccSourceFanBridgeReceipts?:Array<{deviceId:string;handshake:Record<string,unknown>;lease:Record<string,unknown>;frames:Record<string,{ok:true;state:FanState}>}>}).ymccSourceFanBridgeReceipts;
  if(actualSourceChains) {
    check(actualSourceChains.length===9,'actual engine transcript covers all nine source descriptors');
    for(const chain of actualSourceChains) {
      const original=http.request,requests:string[]=[],token='0123456789abcdef'.repeat(4);
      let remote=chain.frames.cold,closed=false;
      http.request=async(url,options):Promise<HttpResponse>=>{
        const endpoint=new URL(url).pathname;requests.push(endpoint);
        const body=typeof options?.body==='string'?JSON.parse(options.body):options?.body;
        let reply:unknown;
        if(endpoint==='/api/handshake') reply=chain.handshake;
        else if(endpoint==='/api/state') reply=remote;
        else if(endpoint==='/api/open') reply=remote=chain.frames.opened;
        else if(endpoint==='/api/open-events') reply=remote=chain.frames.events;
        else if(endpoint==='/api/acquire-control') {remote=chain.frames.acquired;reply=chain.lease;}
        else if(endpoint==='/api/enable') {
          check(JSON.stringify(body?.nodes)===JSON.stringify(chain.frames.armed.state.activeCurve),'actual source curve request matches original engine fixture input '+chain.deviceId);
          check(body?.leaseId===(chain.lease.lease as {leaseId:string}).leaseId,'actual source enable keeps original exact lease '+chain.deviceId);
          reply=remote=chain.frames.armed;
        } else if(endpoint==='/api/restore') reply=remote=chain.frames.restored;
        else if(endpoint==='/api/release-control') reply=remote=chain.frames.released;
        else if(endpoint==='/api/close') {closed=true;reply=remote=chain.frames.closed;}
        else if(endpoint==='/api/shutdown') reply={ok:true};
        else throw new Error('UNEXPECTED_SOURCE_FIXTURE_ENDPOINT '+endpoint);
        return {status:200,headers:'Content-Type: application/json\r\n',body:JSON.stringify(reply)};
      };
      const config={...resolveFanHostConfig('C:\\source-fixture'),sessionToken:token};
      const adapter=new HttpFanApiAdapter(config.baseUrl,token);
      const life=new FanHostLifecycle({enabled:true,config,adapter,launcher,heartbeatIntervalMs:0,resumeWaitDeadlineMs:800,readNativePowerState:async()=>native(1)});
      try {
        await life.start();await life.apply(chain.frames.armed.state.activeCurve as FanNode[]);
        remote=chain.frames.active;
        await life.disable();
        check(requests.includes('/api/restore')&&requests.includes('/api/release-control'),'actual source restore/release consumed by original bridge '+chain.deviceId);
        check(remote.state.sourceFanClosed===true&&remote.state.oemRestoreConfirmed===false,'source software return remains distinct from HC callback '+chain.deviceId);
        await life.close();check(closed&&requests.includes('/api/shutdown'),'actual source bridge close reaches exact process shutdown boundary '+chain.deviceId);
        check(!requests.includes('/api/resume'),'cold source route uses actual Open chain without invented wake '+chain.deviceId);
      } finally {
        if(!closed) await life.close().catch(()=>{});
        http.request=original;
      }
      const power=(chain as typeof chain&{power:{handshake:Record<string,unknown>;lease:Record<string,unknown>;wakeLease:Record<string,unknown>;frames:Record<string,{ok:true;state:FanState}>}}).power;
      check(!!power,'actual source power receipt chain present '+chain.deviceId);
      const powerRequests:string[]=[];let powerRemote=power.frames.cold,woke=false,powerClosed=false;
      let nativePower=native(1);
      http.request=async(url,options):Promise<HttpResponse>=>{
        const endpoint=new URL(url).pathname;powerRequests.push(endpoint);
        const body=typeof options?.body==='string'?JSON.parse(options.body):options?.body;
        let reply:unknown;
        if(endpoint==='/api/handshake')reply=power.handshake;
        else if(endpoint==='/api/state')reply=powerRemote;
        else if(endpoint==='/api/open')reply=powerRemote=power.frames.opened;
        else if(endpoint==='/api/open-events')reply=powerRemote=power.frames.events;
        else if(endpoint==='/api/acquire-control'){powerRemote=woke?power.frames.reacquired:power.frames.acquired;reply=woke?power.wakeLease:power.lease;}
        else if(endpoint==='/api/enable'){
          const expected=woke?power.wakeLease:power.lease;
          check(body?.leaseId===(expected.lease as {leaseId:string}).leaseId,'source wake enable uses exact current lease '+chain.deviceId);
          reply=powerRemote=woke?power.frames.rearmed:power.frames.armed;
        } else if(endpoint==='/api/close'){powerClosed=true;reply=powerRemote=power.frames.closed;}
        else if(endpoint==='/api/shutdown')reply={ok:true};
        else throw new Error('UNEXPECTED_SOURCE_POWER_ENDPOINT '+endpoint);
        return{status:200,headers:'Content-Type: application/json\r\n',body:JSON.stringify(reply)};
      };
      const powerLife=new FanHostLifecycle({enabled:true,config,adapter:new HttpFanApiAdapter(config.baseUrl,token),launcher,
        heartbeatIntervalMs:0,resumeWaitDeadlineMs:800,readNativePowerState:async()=>nativePower});
      try{
        await powerLife.start();await powerLife.apply(power.frames.armed.state.activeCurve as FanNode[]);
        powerRemote=power.frames.active;
        powerLife.setPowerGeneration(2);powerLife.observePowerBoundary('suspending',2);
        nativePower={...native(2),phase:'suspending',hardwareWritesAllowed:false,resumeReady:false};
        await powerLife.suspend();powerRemote=power.frames.suspended;
        check(!powerRequests.includes('/api/suspend'),'source bridge suspend retains original zero-message boundary '+chain.deviceId);
        woke=true;powerRemote=power.frames.resumed;nativePower=native(3);
        powerLife.setPowerGeneration(3);powerLife.observePowerBoundary('resuming',3);powerLife.observePowerBoundary('resume-ready',3);
        await powerLife.resume();
        check(powerRequests.filter(x=>x==='/api/acquire-control').length===2&&powerRequests.filter(x=>x==='/api/enable').length===2,
          'source wake reacquires once and submits retained curve once '+chain.deviceId);
        check(!powerRequests.includes('/api/resume')&&powerRequests.filter(x=>x==='/api/open').length===1,
          'source already-ready real Host wake is consumed without invented second rebuild '+chain.deviceId);
        check(powerLife.currentLease?.leaseId===(power.wakeLease.lease as {leaseId:string}).leaseId,
          'source bridge adopts actual post-wake lease '+chain.deviceId);
        await powerLife.close();check(powerClosed,'source post-wake generation closes through real bridge '+chain.deviceId);
      }finally{
        if(!powerClosed)await powerLife.close().catch(()=>{});
        http.request=original;
      }
    }
  }
  console.log(JSON.stringify({ ok: true, assertions, mappedFamilies: families.length, unsupportedFamilies: unsupported.length, transitionCases: transitions,
    hardwareCalls: 0, HCAssemblyLoaded: false, scope: 'software lifecycle only; no physical control or OEM ownership claim' }));
}
void main();
