/** Cold/adopted public startup; inert Host/native/exact process identity, no device I/O. */
import assert from 'node:assert/strict';
import {FanApiError, type FanNode, type FanState, type FanHandshake} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
import {proc, type PowerLifecycleState} from '../src/bridge/api';
// Install an inert process probe before the startup facade snapshots nativeProc.
// The facade keeps this closure; the public api object is restored immediately.
const processProbe = {findExact: async (_path: string) => ({found:false,pid:0})};
const nativeFindExact = proc.findExact;
proc.findExact = path => processProbe.findExact(path);
const {FanHostLifecycle} = await import('../src/bridge/fanHost');
proc.findExact = nativeFindExact;

const curve: FanNode[] = [0, 40, 70, 100].map(tempC => ({tempC, dutyPercent: 75}));
const families = [
  ['ROG', 'HandheldCompanion.Devices.XboxROGAllyX', 'ProfileCurve'],
  ['MSI', 'HandheldCompanion.Devices.MSI.ClawCG3EM', 'ProfileCurve'],
  ['GPD', 'HandheldCompanion.Devices.GPDWin5', 'GenericDuty'],
  ['OneX', 'HandheldCompanion.Devices.OneXPlayerX1Mini', 'GenericDuty'],
  ['Lenovo', 'HandheldCompanion.Devices.Lenovo.LegionGo', 'ProfileCurve'],
] as const;
const denials = [
  ['401', () => new FanApiError('read authentication refused', 401, 'AUTH_REJECTED')],
  ['403', () => new FanApiError('read permission refused', 403, 'API_PERMISSION_DENIED')],
  ['session', () => new FanApiError('read session missing', 409, 'API_SESSION_REQUIRED')],
  ['unsupported', () => new FanApiError('read device unsupported', 409, 'FAN_UNSUPPORTED')],
  ['device-gate', () => new FanApiError('read device gate denied', 409, 'FAN_DEVICE_GATE_DENIED')],
  ['route', () => new FanApiError('read route conflict', 409, 'FAN_ROUTE_CONFLICT')],
  ['openlib', () => new FanApiError('read openlib conflict', 409, 'HC_OPENLIB_CONFLICT')],
  ['owner', () => new FanApiError('read external owner', 409, 'EXTERNAL_FAN_OWNER')],
  ['legacy-owner', () => new Error('EXTERNAL_FAN_OWNER: another controller')],
] as const;
const outcome = <T>(promise: Promise<T>) => promise.then(value => ({value, error: null as unknown}), error => ({value: null, error}));

function fixture(family: typeof families[number] = families[1]) {
  const host = new NoIoHost();
  host.factoryType = family[1];
  const handshake = host.handshake.bind(host);
  let readError: Error | null = null, handshakeError: Error | null = null;
  let starts = 0, stops = 0, stateReads = 0, nativeGeneration = 0;
  let nativeOverride: PowerLifecycleState | null | undefined;
  let nativeReader: (() => Promise<PowerLifecycleState | null>) | null = null;
  const process = {pid: 116901, executable: 'inert-adopted-fan.exe', processCreated: 'inert-birth-1169', adopted: true};
  let hold: {promise: Promise<FanState>, entered: () => void} | null = null;
  host.handshake = async () => {
    if (handshakeError) { host.calls.push('handshake-denied'); throw handshakeError; }
    return {...await handshake(), fanRoute: family[2]};
  };
  host.getState = async () => {
    stateReads++;
    if (hold) { const current = hold; hold = null; host.calls.push('state-held'); current.entered(); return current.promise; }
    if (readError) { host.calls.push('state-denied'); throw readError; }
    host.calls.push('state');
    return host.snapshot();
  };
  const life = new FanHostLifecycle({
    enabled: true, adapter: host, heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800,
    readNativeActivity: async () => null,
    readNativePowerState: async () => nativeReader ? nativeReader() : nativeOverride === undefined ? ({generation: nativeGeneration, phase: 'ready', hardwareWritesAllowed: true, resumeReady: nativeGeneration > 0, inputReady: true, hibernateAvailable: true}) : nativeOverride,
    launcher: {
      async start() { starts++; return process; },
      async stop(owner) { assert.equal(owner.pid, process.pid); assert.equal(owner.processCreated, process.processCreated); stops++; },
    },
  });
  return {
    host, life, calls: host.calls,
    native: (value: PowerLifecycleState | null | undefined) => { nativeOverride = value; },
    nativeReader: (value: (() => Promise<PowerLifecycleState | null>) | null) => { nativeReader = value; },
    readError: (error: Error | null) => { readError = error; },
    handshakeError: (error: Error | null) => { handshakeError = error; },
    counts: () => ({adoptions: starts, stops, stateReads}),
    generation(value: number) {
      nativeGeneration = value; host.generation = value; host.attempt = 1; host.powerState = 'On'; host.completeResume = true;
      life.setPowerGeneration(value); life.observePowerBoundary('resume-ready', value);
    },
    holdState() {
      let resolve!: (value: FanState) => void, reject!: (error: Error) => void, entered!: () => void;
      const ready = new Promise<void>(markEntered => { entered = markEntered; });
      const promise = new Promise<FanState>((resolveRead, rejectRead) => { resolve = resolveRead; reject = rejectRead; });
      hold = {promise, entered};
      return {ready, resolve, reject};
    },
    async cleanup() { nativeReader = null; nativeOverride = undefined; readError = handshakeError = null; await life.close().catch(() => {}); },
  };
}

type Evidence = {name: string, ok: boolean, evidence?: unknown, error?: string, calls?: string[]};
const results: Evidence[] = [];
async function check(name: string, work: (f: ReturnType<typeof fixture>) => Promise<unknown>, family: typeof families[number] = families[1]) {
  const f = fixture(family);
  try { results.push({name, ok: true, evidence: await work(f)}); }
  catch (error) { results.push({name, ok: false, error: String(error), calls: [...f.calls]}); }
  finally { await f.cleanup(); }
}

for (const family of families) for (const action of ['start', 'apply'] as const) for (const [kind, make] of denials) {
  await check(`${family[0]} ${action} adopted read ${kind}: reject without follow-up, corrected manual remains reachable`, async f => {
    const error = make(); f.readError(error);
    const reply = await outcome(action === 'start' ? f.life.start({manualRecovery: true}) : f.life.apply(curve));
    const deniedIndex = f.calls.indexOf('state-denied');
    const after = f.calls.slice(deniedIndex + 1);
    const beforeCorrection = {...f.counts(), replyRejected: reply.error === error, afterDenied: after};
    assert(deniedIndex >= 0, 'adoption must actually read the existing Host');
    assert.equal(reply.error, error, 'adoption turned a declared refusal into startup success or another error');
    assert.deepEqual(after, [], 'adoption performed more work after a declared state refusal');
    assert.equal(f.counts().stops, 0, 'adopted ownership has no exact exit proof');
    assert.equal(f.life.processId, 116901, 'retain the exact adopted process');
    f.readError(null);
    await f.life.apply(curve);
    assert.equal(f.calls.filter(c => c === 'enable').length, 1, 'one corrected manual request writes exactly once');
    assert.equal(f.life.processId, 116901);
    assert.equal(f.counts().stops, 0);
    return {family: family[0], beforeCorrection, manualAfterCorrection: true, exactOwnerRetained: true};
  }, family);
}
for (const error of [new Error('HTTP request timed out'), new FanApiError('unknown read receipt', 0, 'FAN_HTTP_RESULT_UNKNOWN')]) {
  await check(`adopted ordinary read uncertainty stays distinct: ${error.message}`, async f => {
    f.readError(error);
    const reply = await outcome(f.life.start());
    assert.equal(reply.error, null);
    assert.equal(reply.value?.allowed, true);
    assert.equal(f.life.state, 'suspended', 'retain conservative adopted unreadable-session admission');
    assert(!f.life.recoveryNeedsAttention);
    assert(!f.calls.some(c => ['open', 'events', 'acquire', 'enable'].includes(c)));
    return {ordinaryUncertaintyNotPermissionDenial: true, stateReads: f.counts().stateReads};
  });
}
for (const [kind, make] of denials) await check(`adopted handshake ${kind}: no state query follows a declared refusal`, async f => {
  const error = make(); f.handshakeError(error);
  const reply = await outcome(f.life.start({manualRecovery: true}));
  assert.equal(reply.error, error);
  assert.equal(f.counts().stateReads, 0, 'a refused handshake cannot be reconciled through another endpoint');
  assert.equal(f.counts().stops, 0);
  f.handshakeError(null); await f.life.apply(curve);
  assert.equal(f.calls.filter(c => c === 'enable').length, 1);
  return {stateReadsAfterDeniedHandshake: 0, exactOwnerRetained: true, manualAfterCorrection: true};
});
for (const [kind, make] of denials) await check(`positive generation adopted ${kind}: retained intent parks and one corrected manual request works`, async f => {
  f.generation(6); f.life.rememberDesiredCurve(curve);
  const error = make(); f.readError(error);
  const reply = await outcome(f.life.start({manualRecovery: true}));
  assert.equal(reply.error, error);
  assert(f.life.recoveryNeedsAttention);
  assert(!f.life.recoveryActive);
  assert(f.life.hasControlIntent);
  f.readError(null); await f.life.apply(curve);
  assert.equal(f.calls.filter(c => c === 'enable').length, 1);
  assert.equal(f.counts().stops, 0);
  return {positiveGeneration: 6, needsAttentionEnded: !f.life.recoveryNeedsAttention, correctedManual: true};
});
for (const newer of ['curve', 'off', 'sleep', 'wake'] as const) await check(`adopted stale state rejection after ${newer} cannot park a newer owner`, async f => {
  f.generation(6); f.life.rememberDesiredCurve(curve);
  const held = f.holdState(), old = outcome(f.life.start({manualRecovery: true}));
  await held.ready;
  let next: ReturnType<typeof outcome> | null = null;
  if (newer === 'curve') next = outcome(f.life.apply(curve));
  else if (newer === 'off') next = outcome(f.life.disable());
  else if (newer === 'sleep') { f.life.setPowerGeneration(7); f.life.observePowerBoundary('suspending', 7); }
  else f.life.observePowerBoundary('resuming', 6);
  const epoch = (f.life as unknown as {heartbeatEpoch: number}).heartbeatEpoch;
  held.reject(new FanApiError('old adoption read refused', 401, 'AUTH_REJECTED'));
  await old;
  if (next) assert.equal((await next).error, null);
  assert(!f.life.recoveryNeedsAttention);
  assert.equal(f.counts().stops, 0);
  if (newer === 'curve') assert.equal(f.calls.filter(c => c === 'enable').length, 1);
  else if (newer === 'off') assert(!f.life.hasControlIntent);
  else if (newer === 'sleep') {
    assert.equal(f.life.state, 'suspended');
    assert(!f.calls.some(c => ['open', 'events', 'acquire', 'enable'].includes(c)));
  }
  if (newer === 'wake') {
    assert.equal((f.life as unknown as {heartbeatEpoch: number}).heartbeatEpoch, epoch);
    f.life.observePowerBoundary('resume-ready', 6);
    await f.life.apply(curve);
    assert.equal(f.calls.filter(c => c === 'enable').length, 1);
  }
  return {newer, oldRefusalDidNotParkNewOwner: true};
});
// FAN1181: a follow-up read may refuse even when the initial handshake only failed transiently.
for (const [kind, make] of denials.filter(([kind]) => ['401','403','owner'].includes(kind))) await check(`adopted follow-up ${kind} overrides an uncertain startup result`, async f => {
  const original = new FanApiError('handshake callback failed', 500, 'HOST_ERROR'), refusal = make();
  f.handshakeError(original); f.readError(refusal);
  const reply = await outcome(f.life.start({manualRecovery:true}));
  assert.equal(reply.error, refusal, 'a declared follow-up refusal was swallowed into an uncertain original error');
  assert.equal(f.counts().stateReads, 1); assert.equal(f.counts().stops, 0);
  f.handshakeError(null); f.readError(null); await f.life.start({manualRecovery:true}); await f.life.apply(curve);
  assert.equal(f.calls.filter(c => c === 'enable').length, 1);
  return {declaredReadRefusalPreserved:true,manualAfterCorrection:true};
});
await check('adopted follow-up ordinary timeout retains the uncertain original startup error', async f => {
  const original = new FanApiError('handshake callback failed', 500, 'HOST_ERROR');
  f.handshakeError(original); f.readError(new Error('HTTP request timed out'));
  const reply = await outcome(f.life.start({manualRecovery:true}));
  assert.equal(reply.error, original); assert.equal(f.counts().stateReads, 1); assert.equal(f.counts().stops, 0);
  return {ordinaryReadUncertaintyUnchanged:true};
});
// FAN1182: successful adopted snapshots are bound to the actual read boundary as well.
function resident(f: ReturnType<typeof fixture>, suspended = false): FanState {
  f.generation(6); f.life.rememberDesiredCurve(curve);
  f.host.stateName = suspended ? 'Suspended' : 'Ready'; f.host.powerState = suspended ? 'Suspended' : 'On';
  f.host.opened = f.host.events = !suspended;
  f.host.lease = suspended ? null : {leaseId:'old-resident-lease',generation:900};
  return f.host.snapshot();
}
for (const family of families) for (const path of ['primary','fallback'] as const) for (const shape of ['old-ready-same-generation','old-suspended-new-generation'] as const) await check(`${family[0]} ${path} ${shape} cannot replace the new wake boundary`, async f => {
  const oldState = resident(f,shape==='old-suspended-new-generation');
  if(path==='fallback')f.handshakeError(new FanApiError('uncertain handshake',500,'HOST_ERROR'));
  const held=f.holdState(), old=outcome(f.life.start({manualRecovery:true}));await held.ready;
  const generation=shape==='old-ready-same-generation'?6:7;
  if(generation===7)f.generation(7);
  f.life.observePowerBoundary('resuming',generation);f.life.observePowerBoundary('resume-ready',generation);
  f.host.stateName='AwaitingControl';f.host.powerState='On';f.host.opened=f.host.events=false;f.host.lease=null;
  const before=f.life.coordinatorSnapshot,epoch=(f.life as unknown as {heartbeatEpoch:number}).heartbeatEpoch;
  held.resolve(oldState);await old;
  assert.equal(f.life.currentLease,null,'old Ready snapshot revived its retired resident lease');
  assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,before.wakeReadyGeneration,'old Suspended snapshot erased the current wake-ready fact');
  assert.equal((f.life as unknown as {heartbeatEpoch:number}).heartbeatEpoch,epoch,'read-only old completion changed the current epoch');
  assert.equal(f.counts().stateReads,1,'a retired adopted read must not start another fallback query');
  assert.equal(f.counts().stops,0);assert(!f.calls.some(c=>['open','events','acquire','enable'].includes(c)));
  f.handshakeError(null);await f.life.start({manualRecovery:true});await f.life.apply(curve);
  assert.equal(f.calls.filter(c=>c==='enable').length,1);assert.equal(f.counts().stops,0);
  return {generation,epoch,oldSnapshotIgnored:true,manualWriteAfterCurrentWake:1};
},family);
for(const path of ['primary','fallback'] as const)await check(`current ${path} resident snapshot still supplies its exact lease`,async f=>{
  const snapshot=resident(f);const original=new FanApiError('uncertain handshake',500,'HOST_ERROR');if(path==='fallback')f.handshakeError(original);
  const held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;held.resolve(snapshot);const reply=await pending;
  assert.equal(reply.error,path==='fallback'?original:null);assert.equal(f.life.currentLease?.leaseId,'old-resident-lease');assert.equal(f.counts().stateReads,1);assert.equal(f.counts().stops,0);
  return {currentSnapshotPreserved:true};
});
await check('same-boundary newer curve reuses valid pending startup instead of duplicating adoption',async f=>{
  const snapshot=resident(f),held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;
  const latest=curve.map(n=>({...n,dutyPercent:65}));let nodes:readonly FanNode[]|null=null;const enable=f.host.enable.bind(f.host);f.host.enable=async(...args:any[])=>{nodes=args[0];return enable();};
  const apply=outcome(f.life.apply(latest));held.resolve(snapshot);assert.equal((await pending).error,null);assert.equal((await apply).error,null);assert.deepEqual(nodes,latest);assert.equal(f.counts().adoptions,1);assert.equal(f.calls.filter(c=>c==='enable').length,1);
  return {adoptions:1,latestCurveWrites:1};
});
for(const boundary of ['session','process'] as const)await check(`replaced ${boundary} cannot inherit or abort an old adopted success`,async f=>{
  const snapshot=resident(f),held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;
  const owner=f.life as unknown as {sessionGeneration:number;process:Record<string,unknown>|null};const original=owner.process;
  if(boundary==='session')owner.sessionGeneration++;else owner.process={...original,adopted:false};
  try{held.resolve(snapshot);await pending;assert.equal(f.life.currentLease,null,'old owner leaked a lease across the new boundary');assert.equal(f.counts().stops,0,'old read stopped a newer owned wrapper');assert.equal(f.counts().stateReads,1,'retired read made an extra fallback query');return{simulatedBoundaryOnly:true,oldReceiptIgnored:true};}
  finally{owner.process=original;}
});
for(const action of ['apply','preset'] as const)await check(`first direct ${action} refreshes an unconfirmed adopted handshake after wake`,async f=>{
  const snapshot=resident(f,true);f.handshakeError(new FanApiError('uncertain handshake',500,'HOST_ERROR'));
  const held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;
  f.generation(7);f.life.observePowerBoundary('resuming',7);f.life.observePowerBoundary('resume-ready',7);
  f.host.stateName='AwaitingControl';f.host.powerState='On';f.host.opened=f.host.events=false;f.host.lease=null;
  held.resolve(snapshot);await pending;f.handshakeError(null);
  const reply=await outcome(action==='apply'?f.life.apply(curve):f.life.applyPreset('balanced',curve));assert.equal(reply.error,null,'an unconfirmed old handshake blocked a fresh explicit control');
  assert(f.life.controlReady);assert.equal(f.calls.filter(c=>c==='enable').length+(f.calls.filter(c=>c==='preset').length),1);assert.equal(f.counts().stops,0);
  return {freshHandshake:true,manualOperationCount:1};
});
for(const action of ['start-apply','apply','preset'] as const)await check(`current Suspended fallback with no confirmed handshake permits one fresh ${action}`,async f=>{
  const snapshot=resident(f,true),original=new FanApiError('first handshake uncertain',500,'HOST_ERROR');f.handshakeError(original);
  const held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;held.resolve(snapshot);assert.equal((await pending).error,original);
  f.handshakeError(null);if(action==='start-apply')assert((await f.life.start({manualRecovery:true})).writeReady);
  const reply=await outcome(action==='preset'?f.life.applyPreset('balanced',curve):f.life.apply(curve));assert.equal(reply.error,null);
  assert(f.life.controlReady);assert.equal(f.calls.filter(c=>c==='enable').length+f.calls.filter(c=>c==='preset').length,1);assert.equal(f.counts().stops,0);assert.equal(f.calls.filter(c=>c==='close').length,0);
  return {freshManualHandshake:true,writes:1,forcedStops:0};
});
for(const boundary of ['suspending','writes-denied','unavailable'] as const)await check(`manual adopted Suspended stays device-closed when native is ${boundary}`,async f=>{
  const snapshot=resident(f,true),original=new FanApiError('first handshake uncertain',500,'HOST_ERROR');f.handshakeError(original);
  const held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;held.resolve(snapshot);await pending;f.handshakeError(null);
  f.native(boundary==='unavailable'?null:{generation:6,phase:boundary==='suspending'?'suspending':'ready',hardwareWritesAllowed:false,resumeReady:boundary!=='suspending',inputReady:true,hibernateAvailable:true});
  const before=f.calls.length;await outcome(f.life.start({manualRecovery:true}));
  assert(!f.calls.slice(before).some(c=>['resume','open','events','acquire','enable','preset','close'].includes(c)),'denied native boundary caused a device operation');
  f.native(undefined);await f.life.start({manualRecovery:true});await f.life.apply(curve);assert.equal(f.calls.filter(c=>c==='enable').length,1);
  return {nativeBoundaryPreserved:true,correctedManualWrites:1};
});
await check('ordinary adopted Suspended remains observation-only',async f=>{
  const snapshot=resident(f,true),held=f.holdState(),pending=outcome(f.life.start());await held.ready;held.resolve(snapshot);assert.equal((await pending).error,null);
  assert(!f.calls.some(c=>['resume','open','events','acquire','enable','preset','close'].includes(c)));return{ordinaryAdoptionNoDeviceWork:true};
});
await check('read-only handshake cannot trigger adopted HC rearm',async f=>{
  const snapshot=resident(f,true),handshake=f.host.handshake.bind(f.host);f.host.handshake=async()=>({...await handshake(),fanRouteWriteReady:false,hardwareWritesEnabled:false});
  const held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;held.resolve(snapshot);const reply=await pending;assert.equal(reply.error,null);assert.equal(reply.value?.writeReady,false);
  assert(!f.calls.some(c=>['resume','open','events','acquire','enable','preset','close'].includes(c)));return{capabilityDenialPreserved:true};
});
await check('adopted Suspended native confirmation crossed by a newer wake cannot rearm the old request',async f=>{
  const snapshot=resident(f,true),held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;
  let resolve!: (v:PowerLifecycleState)=>void,entered!:()=>void;const ready=new Promise<void>(r=>entered=r),native=new Promise<PowerLifecycleState>(r=>resolve=r);
  f.nativeReader(async()=>{entered();return native;});held.resolve(snapshot);
  let timer:ReturnType<typeof setTimeout>|undefined;
  try{
    await Promise.race([ready,new Promise<never>((_,reject)=>timer=setTimeout(()=>reject(new Error('native-confirmation test boundary was not reached')),700))]);if(timer)clearTimeout(timer);
    f.generation(7);f.life.observePowerBoundary('resuming',7);f.life.observePowerBoundary('resume-ready',7);f.host.stateName='AwaitingControl';f.host.powerState='On';f.host.opened=f.host.events=false;f.host.lease=null;
    resolve({generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true});await pending;
    assert(!f.calls.some(c=>['resume','open','events','acquire','enable','preset','close'].includes(c)),'old native reply initiated device work for another wake');assert.equal(f.life.currentLease,null);assert.equal(f.counts().stops,0);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,7);
    f.nativeReader(null);await f.life.start({manualRecovery:true});await f.life.apply(curve);assert.equal(f.calls.filter(c=>c==='enable').length,1);
    return{oldNativeReplyIgnored:true,currentManualWrites:1};
  }finally{if(timer)clearTimeout(timer);resolve({generation:7,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true});f.nativeReader(null);await pending;}
});
// FAN1183: the pre-shortcut resident read is also owned by its wake/session boundary.
for (const family of families) for (const boundary of ['same-generation-wake','new-generation'] as const)
  for (const shape of ['ready','fault','cleanup-pending','timeout','denial'] as const)
  await check(`${family[0]} retained ${boundary} ${shape} retires before shortcut or cleanup`, async f => {
    f.generation(6); await f.life.apply(curve);
    const snapshot = f.host.snapshot();
    if (shape === 'fault') snapshot.state = 'FaultLocked';
    if (shape === 'cleanup-pending') snapshot.hcCloseCleanupPending = true;
    const reads = f.counts().stateReads, adoptions = f.counts().adoptions;
    const held = f.holdState(), pending = outcome(f.life.start({manualRecovery:true})); await held.ready;
    const generation = boundary === 'new-generation' ? 7 : 6;
    if (generation === 7) f.generation(7);
    f.life.observePowerBoundary('resuming',generation); f.life.observePowerBoundary('resume-ready',generation);
    f.host.stateName='AwaitingControl'; f.host.powerState='On'; f.host.opened=f.host.events=false; f.host.lease=null;
    const callBoundary = f.calls.length, epoch = (f.life as unknown as {heartbeatEpoch:number}).heartbeatEpoch;
    if (shape === 'timeout') held.reject(new Error('HTTP request timed out'));
    else if (shape === 'denial') held.reject(new FanApiError('stale auth refusal',401,'AUTH_REJECTED'));
    else held.resolve(snapshot);
    const reply = await pending;
    assert.match(String(reply.error),/FAN_CONTROL_INTENT_SUPERSEDED:/,'retired resident reply was accepted as current startup result');
    assert.deepEqual(f.calls.slice(callBoundary),[],'retired resident completion continued device/startup work');
    assert.equal(f.counts().stateReads,reads+1,'retired resident reply triggered follow-up reads');
    assert.equal(f.counts().adoptions,adoptions,'retired resident reply repeated launcher adoption');
    assert.equal(f.counts().stops,0,'retired resident reply stopped the current owner');
    assert.equal(f.life.currentLease,null,'retired resident reply revived a lease');
    assert.equal((f.life as unknown as {heartbeatEpoch:number}).heartbeatEpoch,epoch,'retired resident result changed the current epoch');
    assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,generation);
    await f.life.apply(curve);
    assert.equal(f.calls.filter(c=>c==='enable').length,2,'one current manual request must write once after retiring the read');
    return {generation,shape,oldResultRetired:true,currentManualWrites:1};
  },family);
for (const family of families) for (const boundary of ['session','process'] as const)
  await check(`${family[0]} retained read cannot clean a replaced ${boundary}`, async f => {
    f.generation(6); await f.life.apply(curve); const snapshot = {...f.host.snapshot(),state:'FaultLocked'};
    const held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true})); await held.ready;
    const internal=f.life as unknown as {sessionGeneration:number;process:Record<string,unknown>};
    const session=internal.sessionGeneration,process=internal.process,readCount=f.counts().stateReads,mark=f.calls.length;
    if(boundary==='session')internal.sessionGeneration++;else internal.process={...process,adopted:false};
    try {
      held.resolve(snapshot);const reply=await pending;
      assert.match(String(reply.error),/FAN_CONTROL_INTENT_SUPERSEDED:/);
      assert.deepEqual(f.calls.slice(mark),[],'retired retained read operated on a new owner');
      assert.equal(f.counts().stops,0);assert.equal(f.counts().stateReads,readCount);
      return {simulatedBoundaryOnly:true,noNewOwnerCleanup:true};
    } finally { internal.sessionGeneration=session;internal.process=process;held.resolve(snapshot);await pending; }
  },family);
for(const phase of ['first-find','wait','final-find'] as const)for(const boundary of ['same-generation-wake','new-generation'] as const)
  await check(`retained ${phase} completion crossing ${boundary} stops obsolete process observation`,async f=>{
    f.generation(6);await f.life.apply(curve);f.readError(new Error('HTTP request timed out'));
    const original=processProbe.findExact,originalTimeout=globalThis.setTimeout;
    let queryCount=0,retired=false,release!:()=>void,entered!:()=>void;
    const ready=new Promise<void>(resolve=>entered=resolve),gate=new Promise<void>(resolve=>release=resolve);
    let releaseWait:(()=>void)|undefined;
    processProbe.findExact=async()=>{
      queryCount++;
      if(phase==='first-find'&&queryCount===1||phase==='final-find'&&queryCount===2){entered();await gate;return{found:false,pid:0};}
      return{found:phase==='wait'&&!retired,pid:phase==='wait'&&!retired?116901:0};
    };
    if(phase==='wait')globalThis.setTimeout=((callback:(...args:any[])=>void,ms?:number,...args:any[])=>{
      if(ms===100){releaseWait=()=>callback(...args);entered();return 0 as any;}
      return originalTimeout(callback,ms,...args);
    }) as typeof setTimeout;
    const pending=outcome(f.life.start({manualRecovery:true}));
    let watchdog:ReturnType<typeof setTimeout>|undefined;
    try{
      await Promise.race([ready,new Promise<never>((_,reject)=>watchdog=originalTimeout(()=>reject(new Error('process observation boundary not reached')),700))]);if(watchdog)clearTimeout(watchdog);
      const generation=boundary==='new-generation'?7:6;if(generation===7)f.generation(7);
      f.life.observePowerBoundary('resuming',generation);f.life.observePowerBoundary('resume-ready',generation);
      const queries=queryCount,adoptions=f.counts().adoptions,mark=f.calls.length;retired=true;release();releaseWait?.();
      const reply=await pending;assert.match(String(reply.error),/FAN_CONTROL_INTENT_SUPERSEDED:/);
      assert.equal(queryCount,queries,'obsolete process-observation loop queried again');
      assert.equal(f.counts().adoptions,adoptions);assert.equal(f.counts().stops,0);assert.deepEqual(f.calls.slice(mark),[]);
      processProbe.findExact=original;globalThis.setTimeout=originalTimeout;f.readError(null);
      f.host.stateName='AwaitingControl';f.host.powerState='On';f.host.opened=f.host.events=false;f.host.lease=null;
      await f.life.apply(curve);assert.equal(f.calls.filter(c=>c==='enable').length,2);
      return {phase,boundary,processQueriesBeforeRetirement:queries,extraQueriesAfterRetirement:0,currentManualWrites:1};
    }finally{if(watchdog)clearTimeout(watchdog);release();releaseWait?.();processProbe.findExact=original;globalThis.setTimeout=originalTimeout;f.readError(null);await pending;}
  });
for(const family of families)await check(`${family[0]} valid retained read preserves its gate without new startup`,async f=>{
  f.generation(6);await f.life.apply(curve);const snapshot=f.host.snapshot(),before=f.counts();
  const held=f.holdState(),pending=outcome(f.life.start({manualRecovery:true}));await held.ready;held.resolve(snapshot);const reply=await pending;
  assert.equal(reply.error,null);assert.equal(reply.value?.writeReady,true);assert.equal(f.counts().adoptions,before.adoptions);assert.equal(f.counts().stateReads,before.stateReads+1);assert.equal(f.calls.filter(c=>c==='enable').length,1);
  return{sameBoundaryGatePreserved:true};
},family);
await check('new curve shares a valid retained probe within the same boundary',async f=>{
  f.generation(6);await f.life.apply(curve);const snapshot=f.host.snapshot(),before=f.counts(),held=f.holdState();
  const pending=outcome(f.life.start({manualRecovery:true}));await held.ready;
  const latest=curve.map(n=>({...n,dutyPercent:65}));let written:readonly FanNode[]|null=null;const enable=f.host.enable.bind(f.host);f.host.enable=async(...args:any[])=>{written=args[0];return enable();};
  const apply=outcome(f.life.apply(latest));held.resolve(snapshot);assert.equal((await pending).error,null);assert.equal((await apply).error,null);assert.deepEqual(written,latest);assert.equal(f.counts().adoptions,before.adoptions);assert.equal(f.calls.filter(c=>c==='enable').length,2);
  return{validNewerCurveShared:true};
});
await check('current retained timeout keeps the original bounded owner recovery',async f=>{
  f.generation(6);await f.life.apply(curve);const before=f.counts(),snapshot=f.host.snapshot(),held=f.holdState();
  const original=processProbe.findExact;let queries=0;processProbe.findExact=async()=>{queries++;return{found:false,pid:0};};
  try{const pending=outcome(f.life.start({manualRecovery:true}));await held.ready;held.reject(new Error('HTTP request timed out'));assert.equal((await pending).error,null);assert.equal(queries,2);assert.equal(f.counts().adoptions,before.adoptions+1);assert.equal(f.counts().stops,0);return{currentTimeoutStillRecovers:true};}
  finally{processProbe.findExact=original;held.resolve(snapshot);}
});
await check('current retained authentication refusal stays a refusal and corrected manual works',async f=>{
  f.generation(6);await f.life.apply(curve);const error=new FanApiError('current auth refusal',401,'AUTH_REJECTED');f.readError(error);
  const reply=await outcome(f.life.start({manualRecovery:true}));assert.equal(reply.error,error);f.readError(null);await f.life.apply(curve);assert.equal(f.calls.filter(c=>c==='enable').length,2);return{currentRefusalPreserved:true};
});
// FAN1184: handshake receipts and retry waits retain their exact startup boundary.
function heldStartupHandshake(f: ReturnType<typeof fixture>, stage: 'first'|'retry') {
  let resolve!: (reply: FanHandshake) => void, reject!: (error: Error) => void, entered!: () => void, attempts = 0;
  const ready = new Promise<void>((r) => entered = r), promise = new Promise<FanHandshake>((r, j) => {
    resolve = r;
    reject = j;
  });
  const original = f.host.handshake.bind(f.host), host = f.host as typeof f.host & {setSessionToken?: (token: string) => void};
  const reply: FanHandshake = {
    ok: true,
    supported: true,
    hardwareWritesEnabled: true,
    fanRouteWriteReady: true,
    deviceClass: f.host.factoryType,
    fanRoute: families.find((x) => x[1] === f.host.factoryType)[2],
    deviceIdentity: { fixtureReceipt: "current" }
  };
  if (stage === "retry") host.setSessionToken = () => {
    f.calls.push("token-bind");
  };
  host.handshake = async () => {
    attempts++;
    if (stage === "retry" && attempts === 1) {
      f.calls.push("handshake-transient");
      throw new Error("temporary startup token race");
    }
    f.calls.push("handshake-held");
    entered();
    return promise;
  };
  return { ready, reply, resolve, reject, attempts: () => attempts, restore() {
    host.handshake = original;
    delete host.setSessionToken;
  } };
}
for (const family of families) for (const stage of (["first", "retry"] as const)) for (const boundary of ["same-generation-wake", "new-generation"])
  for (const shape of ["success", "denial", "error"]) await check(`${family[0]} ${stage} handshake ${shape} crossing ${boundary} cannot publish or continue`, async (f) => {
    f.generation(6);
    let identities = 0;
    f.life.setDeviceIdentitySink(() => identities++);
    const held = heldStartupHandshake(f, stage), pending2 = outcome(f.life.start({ manualRecovery: true }));
    await held.ready;
    const generation = boundary === "new-generation" ? 7 : 6;
    if (generation === 7) f.generation(7);
    f.life.observePowerBoundary("resuming", generation);
    f.life.observePowerBoundary("resume-ready", generation);
    const mark = f.calls.length, reads = f.counts().stateReads, epoch = (f.life as unknown as {heartbeatEpoch: number}).heartbeatEpoch;
    if (shape === "error") held.reject(new Error("late handshake timeout"));
    else held.resolve(shape === "denial" ? { ...held.reply, supported: false } : held.reply);
    const reply = await pending2;
    assert.match(String(reply.error), /FAN_CONTROL_INTENT_SUPERSEDED:/, "old handshake was accepted or classified as current");
    assert.equal(identities, 0, "old handshake published device identity");
    assert.deepEqual(f.calls.slice(mark), [], "old handshake continued token/read/control work");
    assert.equal(f.counts().stateReads, reads);
    assert.equal(f.counts().stops, 0);
    assert.equal(f.life.controlReady, false, "old handshake changed writeReady");
    assert.equal((f.life as unknown as {heartbeatEpoch: number}).heartbeatEpoch, epoch);
    held.restore();
    await f.life.apply(curve);
    assert.equal(f.calls.filter((c) => c === "enable").length, 1);
    return { stage, boundary, shape, retiredBeforePublication: true, currentManualWrites: 1 };
  }, family);
for (const stage of (["first", "retry"] as const)) for (const boundary of ["session", "process", "off", "close"]) for (const shape of ["success", "denial", "error"])
  await check(`${stage} handshake ${shape} cannot cross ${boundary}`, async (f) => {
    f.generation(6);
    let identities = 0;
    f.life.setDeviceIdentitySink(() => identities++);
    const held = heldStartupHandshake(f, stage), pending2 = outcome(f.life.start({ manualRecovery: true }));
    await held.ready;
    const internal = f.life as unknown as {sessionGeneration: number; process: Record<string, unknown>};
    const session = internal.sessionGeneration, process2 = internal.process;
    let end;
    if (boundary === "session") internal.sessionGeneration++;
    else if (boundary === "process") internal.process = { ...process2, adopted: false };
    else end = outcome(boundary === "off" ? f.life.disable() : f.life.close());
    const reads = f.counts().stateReads, mark = f.calls.length;
    try {
      if (shape === "error") held.reject(new Error("late handshake error"));
      else held.resolve(shape === "denial" ? { ...held.reply, supported: false } : held.reply);
      const reply = await pending2;
      await end;
      assert.match(String(reply.error), /FAN_CONTROL_INTENT_SUPERSEDED:/);
      assert.equal(identities, 0, "retired handshake published identity");
      assert.equal(f.counts().stateReads, reads + (boundary === "off" ? 1 : 0), "old handshake added reads beyond the current OFF reconciliation");
      if (boundary === "session" || boundary === "process") {
        assert.deepEqual(f.calls.slice(mark), []);
        assert.equal(f.counts().stops, 0);
        internal.sessionGeneration = session;
        internal.process = process2;
      }
      held.restore();
      await f.life.apply(curve);
      assert.equal(f.calls.filter((c) => c === "enable").length, 1);
      return { stage, boundary, shape, identitySeamOnly: boundary === "session" || boundary === "process", currentManualWrites: 1 };
    } finally {
      if (boundary === "session" || boundary === "process") {
        internal.sessionGeneration = session;
        internal.process = process2;
      }
      held.restore();
      held.resolve(held.reply);
      await pending2;
      await end;
    }
  });
for (const boundary of ["same-generation-wake", "new-generation", "session", "process", "off", "close"])
  await check(`handshake retry delay crossed by ${boundary} cannot send a second handshake`, async (f) => {
    f.generation(6);
    let identities = 0, attempts = 0, entered, release;
    f.life.setDeviceIdentitySink(() => identities++);
    const host = f.host, originalHandshake = host.handshake.bind(host), originalTimeout = globalThis.setTimeout;
    const ready = new Promise((resolve) => entered = resolve);
    host.setSessionToken = () => {
      f.calls.push("token-bind");
    };
    host.handshake = async () => {
      attempts++;
      if (attempts === 1) {
        f.calls.push("handshake-transient");
        throw new Error("temporary startup token race");
      }
      return { ...await originalHandshake(), deviceIdentity: { fixtureReceipt: "retry-delay" } };
    };
    globalThis.setTimeout = (callback, ms, ...args) => {
      if (ms === 50) {
        release = () => callback(...args);
        entered();
        return 0;
      }
      return originalTimeout(callback, ms, ...args);
    };
    const pending2 = outcome(f.life.start({ manualRecovery: true }));
    await ready;
    const internal = f.life as unknown as {sessionGeneration: number; process: Record<string, unknown>};
    const session = internal.sessionGeneration, process2 = internal.process;
    let end;
    if (boundary === "session") internal.sessionGeneration++;
    else if (boundary === "process") internal.process = { ...process2, adopted: false };
    else if (boundary === "off" || boundary === "close") end = outcome(boundary === "off" ? f.life.disable() : f.life.close());
    else {
      const generation = boundary === "new-generation" ? 7 : 6;
      if (generation === 7) f.generation(7);
      f.life.observePowerBoundary("resuming", generation);
      f.life.observePowerBoundary("resume-ready", generation);
    }
    try {
      release();
      const reply = await pending2;
      await end;
      assert.match(String(reply.error), /FAN_CONTROL_INTENT_SUPERSEDED:/);
      assert.equal(attempts, 1, "retired delay sent another handshake");
      assert.equal(identities, 0);
      return { boundary, extraHandshakeAfterRetirement: 0 };
    } finally {
      globalThis.setTimeout = originalTimeout;
      host.handshake = originalHandshake;
      delete host.setSessionToken;
      if (boundary === "session" || boundary === "process") {
        internal.sessionGeneration = session;
        internal.process = process2;
      }
      release?.();
      await pending2;
      await end;
    }
  });
for (const stage of (["first", "retry"] as const)) for (const newerCurve of [false, true]) await check(`valid ${stage} handshake ${newerCurve ? "shares newer curve" : "preserves identity and gate"}`, async (f) => {
  f.generation(6);
  let identities = 0;
  f.life.setDeviceIdentitySink(() => identities++);
  const held = heldStartupHandshake(f, stage), pending2 = outcome(f.life.start({ manualRecovery: true }));
  await held.ready;
  const latest = curve.map((n) => ({ ...n, dutyPercent: 65 }));
  let written = null;
  const enable = f.host.enable.bind(f.host);
  f.host.enable = async (...args) => {
    written = args[0];
    return enable();
  };
  const apply = newerCurve ? outcome(f.life.apply(latest)) : void 0;
  held.resolve(held.reply);
  const reply = await pending2;
  assert.equal(reply.error, null);
  assert.equal(reply.value?.writeReady, true);
  assert.equal(identities, 1);
  if (apply) {
    assert.equal((await apply).error, null);
    assert.deepEqual(written, latest);
    assert.equal(f.calls.filter((c) => c === "enable").length, 1);
  }
  assert.equal(held.attempts(), stage === "first" ? 1 : 2);
  held.restore();
  return { stage, newerCurve, currentHandshakePreserved: true };
});
for (const stage of (["first", "retry"] as const)) await check(`current ${stage} handshake device denial stays denied and next manual can retry`, async (f) => {
  f.generation(6);
  let identities = 0;
  f.life.setDeviceIdentitySink(() => identities++);
  const held = heldStartupHandshake(f, stage), pending2 = outcome(f.life.start({ manualRecovery: true }));
  await held.ready;
  held.resolve({ ...held.reply, supported: false });
  const reply = await pending2;
  assert(reply.error instanceof FanApiError);
  assert.equal(reply.error.errorCode, "FAN_DEVICE_GATE_DENIED");
  assert.equal(identities, 0);
  assert.equal(f.counts().stateReads, 0);
  held.restore();
  await f.life.apply(curve);
  assert.equal(f.calls.filter((c) => c === "enable").length, 1);
  return { currentRefusalPreserved: true };
});
await check("current read-only handshake cannot establish hardware write permission", async (f) => {
  f.generation(6);
  const held = heldStartupHandshake(f, "first"), pending2 = outcome(f.life.start({ manualRecovery: true }));
  await held.ready;
  held.resolve({ ...held.reply, fanRouteWriteReady: false, hardwareWritesEnabled: false });
  const reply = await pending2;
  assert.equal(reply.error, null);
  assert.equal(reply.value?.writeReady, false);
  assert.equal(f.life.controlReady, false);
  assert(!f.calls.some((c) => ["open", "events", "acquire", "enable", "preset"].includes(c)));
  held.restore();
  return { readOnlyPreserved: true };
});
for (const boundary of ['off','close'] as const) await check(`launcher returned after ${boundary} cannot bind or send its first handshake`,async f=>{
  f.generation(6);let entered!:()=>void,release!:()=>void;
  const ready=new Promise<void>(r=>entered=r),gate=new Promise<void>(r=>release=r);
  const owner=f.life as unknown as {launcher:{start:(...args:any[])=>Promise<any>}},original=owner.launcher.start;
  owner.launcher.start=async(...args:any[])=>{const process=await original(...args);entered();await gate;return process;};
  const pending=outcome(f.life.start({manualRecovery:true}));await ready;const end=outcome(boundary==='off'?f.life.disable():f.life.close());
  try{release();const reply=await pending;await end;assert.match(String(reply.error),/FAN_CONTROL_INTENT_SUPERSEDED:/);assert.equal(f.calls.filter(c=>c==='handshake').length,0,'cancelled launch continuation sent a handshake');owner.launcher.start=original;await f.life.apply(curve);assert.equal(f.calls.filter(c=>c==='enable').length,1);return{boundary,cancelledLaunchNoHandshake:true,currentManualWrites:1};}
  finally{release();owner.launcher.start=original;await pending;await end;}
});
console.log(JSON.stringify({ok: results.every(r => r.ok), checks: results.length, results, hardwareCalls: 0,
  scope: 'public startup/adoption/manual apply; family labels exercise shared bridge only; inert adapters/native/process; no hardware or CPU benchmark'}));
if (results.some(r => !r.ok)) process.exitCode = 1;
