import assert from 'node:assert/strict';
import { createUpdatePredownloadScheduler, type PredownloadPolicy } from '../src/bridge/updatePredownloadScheduler';
import type { UpdateInfo, UpdateProgressState } from '../src/bridge/yeman';
let checks = 0;
function check(label: string, body: () => void) { body(); checks++; console.log('PASS ' + label); }
const sha = 'A'.repeat(64), info: UpdateInfo = { version: '99.0.1', sha256: sha };
function fixture(initial: Partial<PredownloadPolicy> = {}) {
  let now = 1000000000, online = true, gameRunning = false, manifestVersion = info.version;
  let policy: PredownloadPolicy = { enabled: true, bootId: 'boot-A', lastNotifiedBoot: '', failureCount: 0, nextCheckAt: 0, ...initial };
  let current: { info: UpdateInfo | null; progress: UpdateProgressState } = { info: null, progress: { phase: 'idle' } };
  const calls = { refresh: 0, check: 0, download: 0, cache: 0, cancel: 0, notice: 0, ack: 0, schedule: 0, noticeVersions: [] as string[] };
  const timers = new Map<number, { at: number; fn: () => void }>(); let timerId = 0;
  let hasPackage = false, checkFail = false, prepareFail = false, cacheMissing = false, noticeAvailable = true, ackFail = false;
  let pendingCheck: (() => Promise<void>) | null = null, pendingPrepare: (() => Promise<void>) | null = null;
  const scheduler = createUpdatePredownloadScheduler({
    now: () => now, online: () => online, canRun:()=>!gameRunning,
    timer: (fn, ms) => { const id = ++timerId; timers.set(id, { at: now + ms, fn }); return id; },
    clearTimer: id => { timers.delete(id as number); },
    readPolicy: async () => ({ ...policy }), setEnabled: async enabled => ({ ...(policy = { ...policy, enabled }) }),
    saveSchedule: async (failureCount, nextCheckAt) => { calls.schedule++; policy = { ...policy, failureCount, nextCheckAt }; },
    initialize: async () => {}, refresh: async () => { calls.refresh++; }, current: () => current,
    check: async () => {
      calls.check++; await pendingCheck?.();
      if (checkFail) { current.progress = { phase: 'failed', error: 'offline', stage: 'download' }; return; }
      const cached = current.progress.phase === 'downloaded' && current.info?.version === manifestVersion;
      current = { info: { ...info, version:manifestVersion }, progress: { phase: cached ? 'downloaded' : 'available', stage: 'download' } };
    },
    prepare: async cacheOnly => {
      cacheOnly ? calls.cache++ : calls.download++;
      current.progress = { phase: 'downloading', stage: 'download' };
      await pendingPrepare?.();
      if (prepareFail || (cacheOnly && (!hasPackage || cacheMissing))) { current.progress = { phase: 'failed', stage: 'download', error: 'fixture failure' }; return false; }
      hasPackage = true; current.progress = { phase: 'downloaded', stage: 'install' }; return true;
    },
    cancel: async () => { calls.cancel++; },
    notify: async version => { calls.notice++; calls.noticeVersions.push(version); return noticeAvailable; },
    notified: async () => { calls.ack++; if (ackFail) throw Error('disk full'); policy = { ...policy, lastNotifiedBoot: policy.bootId }; return true; },
  });
  return { scheduler, calls, timers, policy: () => policy, current: () => current,
    now:()=>now,
    async advance(ms:number) {
      const end=now+ms; let wakes=0;
      for(;;){const due=[...timers].filter(([,t])=>t.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];if(!due)break;
        assert(++wakes<1000,'scheduler timer storm'); now=due[1].at; timers.delete(due[0]);due[1].fn();
        for(let i=0;i<100;i++)await Promise.resolve();
      }
      now=end;for(let i=0;i<100;i++)await Promise.resolve();return wakes;
    },
    time(ms: number) { now += ms; }, game(value:boolean){gameRunning=value;scheduler.progress();}, manifest(version:string){manifestVersion=version;}, setVersion(version:string){current.info={...info,version};}, offline(value = true) { online = !value; },
    savedReady() { hasPackage = true; current = { info: { ...info }, progress: { phase: 'downloaded', stage: 'install' } }; },
    checkFails(value = true) { checkFail = value; }, prepareFails(value = true) { prepareFail = value; }, missingCache(value = true) { cacheMissing = value; },
    noticeAvailable(value: boolean) { noticeAvailable = value; }, ackFails(value: boolean) { ackFail = value; },
    holdCheck(fn: () => Promise<void>) { pendingCheck = fn; }, holdPrepare(fn: () => Promise<void>) { pendingPrepare = fn; },
  };
}
const idleTimersOnly=process.argv.includes('--idle-timers');
if(!idleTimersOnly){
{
 const f=fixture(); f.scheduler.start(); await f.scheduler.read();
 check('startup is delayed and provider reads perform no download',()=>{ assert.equal(f.calls.check,0);assert.equal(f.calls.download,0);assert.equal(f.timers.size,1); });
 await f.scheduler.run();
 check('new version downloads once and reports ready without any install capability',()=>{assert.equal(f.calls.download,1);assert.equal(f.calls.notice,1);assert.equal(f.calls.ack,1);assert.equal(f.current().progress.phase,'downloaded');});
 for(let i=0;i<5;i++)await f.scheduler.run();
 check('ready package and boot receipt suppress repeated download and notifications',()=>{assert.equal(f.calls.download,1);assert.equal(f.calls.notice,1);assert.equal(f.calls.cache,0);assert.equal(f.timers.size,1);});
 f.time(6*3600000);await f.scheduler.run();
 check('periodic manifest check reuses the same version instead of HTTP redownload',()=>{assert.equal(f.calls.check,2);assert.equal(f.calls.download,1);assert.equal(f.calls.cache,1);});
 f.scheduler.stop();check('disposing clears its only timer',()=>assert.equal(f.timers.size,0));
}
{
 const f=fixture({enabled:false,lastNotifiedBoot:'boot-A'});f.savedReady();f.offline();await f.scheduler.run();
 check('same boot app restart verifies cache offline without another notice or HTTP',()=>{assert.equal(f.calls.cache,1);assert.equal(f.calls.check,0);assert.equal(f.calls.download,0);assert.equal(f.calls.notice,0);});
}
{
 const f=fixture({enabled:false,lastNotifiedBoot:'boot-previous'});f.savedReady();f.offline();await f.scheduler.run();await f.scheduler.run();
 check('next boot emits one ready notice even when automatic downloading is disabled',()=>{assert.equal(f.calls.notice,1);assert.equal(f.calls.ack,1);assert.equal(f.calls.check,0);});
}
{
 const f=fixture();f.noticeAvailable(false);await f.scheduler.run();await f.scheduler.run();
 check('unavailable or disabled monitor does not consume the boot notice',()=>{assert.equal(f.calls.ack,0);assert.equal(f.calls.download,1);});
 f.noticeAvailable(true);await f.scheduler.run();await f.scheduler.run();
 check('monitor recovery delivers and persists exactly one accepted notification',()=>{assert.equal(f.calls.ack,1);assert.equal(f.policy().lastNotifiedBoot,'boot-A');assert.equal(f.calls.notice,3);});
}
{
 const f=fixture();f.ackFails(true);await f.scheduler.run();await f.scheduler.run();f.ackFails(false);await f.scheduler.run();
 check('ack write failure retries persistence without replaying an accepted popup',()=>{assert.equal(f.calls.notice,1);assert.ok(f.calls.ack>=2);assert.equal(f.calls.download,1);});
}
{
 const f=fixture();f.checkFails();await f.scheduler.run();await f.scheduler.run();
 check('manifest failure is backed off instead of a hot request loop',()=>{assert.equal(f.calls.check,1);assert.equal(f.policy().failureCount,1);});
 f.time(60000);await f.scheduler.run();
 check('persistent failure increases the durable retry delay',()=>{assert.equal(f.calls.check,2);assert.equal(f.policy().failureCount,2);});
 f.checkFails(false);f.time(5*60000);await f.scheduler.run();
 check('network repair eventually downloads and clears failure backoff',()=>{assert.equal(f.calls.download,1);assert.equal(f.policy().failureCount,0);});
}
{
 const f=fixture();f.prepareFails();await f.scheduler.run();await f.scheduler.run();
 check('download failure leaves the scheduler retryable without repeated transfers',()=>{assert.equal(f.calls.download,1);assert.equal(f.policy().failureCount,1);});
 f.prepareFails(false);f.time(60000);await f.scheduler.run();check('next bounded round heals a failed download',()=>assert.equal(f.calls.download,2));
}
{
 const f=fixture({nextCheckAt:1000000000+3600000});f.savedReady();f.missingCache();await f.scheduler.run();
 check('missing/corrupt ready cache bypasses old check delay and repairs automatically',()=>{assert.equal(f.calls.cache,1);assert.equal(f.calls.download,1);assert.equal(f.current().progress.phase,'downloaded');});
}
{
 const f=fixture();f.offline();await f.scheduler.run();check('offline startup does not spin native HTTP',()=>assert.equal(f.calls.check,0));
 f.offline(false);f.scheduler.wake(true);await f.scheduler.run();check('online event recovers without an app restart',()=>assert.equal(f.calls.download,1));
}
{
 const f=fixture();let release=()=>{};f.holdCheck(()=>new Promise<void>(r=>release=r));
 const work=f.scheduler.run();await Promise.resolve();await Promise.resolve();await Promise.resolve();await Promise.resolve();await f.scheduler.setEnabled(false);release();await work;
 check('turning off during manifest fetch cannot start a late background transfer',()=>{assert.equal(f.calls.download,0);assert.equal(f.policy().enabled,false);});
}
{
 const f=fixture();let release=()=>{},entered=()=>{};const started=new Promise<void>(r=>entered=r);f.holdPrepare(()=>{entered();return new Promise<void>(r=>release=r);});
 const work=f.scheduler.run();await started;await f.scheduler.run();await f.scheduler.setEnabled(false);release();await work;
 check('off during download cancels only background ownership and cannot notify/install',()=>{assert.equal(f.calls.download,1);assert.equal(f.calls.cancel,1);assert.equal(f.calls.notice,0);});
}
{
 const f=fixture({enabled:false});await f.scheduler.run();check('disabled idle policy performs no checks or downloads',()=>{assert.equal(f.calls.check,0);assert.equal(f.calls.download,0);});
 await f.scheduler.setEnabled(true);await f.scheduler.run();check('enabled setting readback starts one recoverable download',()=>assert.equal(f.calls.download,1));
}
{
 const f=fixture();f.game(true);f.savedReady();await f.scheduler.run();
 check('game blocks startup checks cache hashing downloads and ready popups',()=>{assert.equal(f.calls.check,0);assert.equal(f.calls.cache,0);assert.equal(f.calls.notice,0);});
 assert.equal((await f.scheduler.read()).phase,'paused');f.game(false);f.scheduler.wake(true);await f.scheduler.run();
 check('confirmed game exit recovers preparation and notification',()=>{assert.equal(f.calls.cache,1);assert.equal(f.calls.notice,1);});
}
{
 const f=fixture();let release=()=>{},enter=()=>{};const entered=new Promise<void>(r=>enter=r);f.holdCheck(()=>{enter();return new Promise<void>(r=>release=r);});
 const task=f.scheduler.run();await entered;f.game(true);release();await task;
 check('game entry during a manifest fetch prevents late download admission',()=>assert.equal(f.calls.download,0));
}
{
 const f=fixture();let release=()=>{},enter=()=>{};const entered=new Promise<void>(r=>enter=r);f.holdPrepare(()=>{enter();return new Promise<void>(r=>release=r);});
 const task=f.scheduler.run();await entered;f.game(true);f.prepareFails();release();await task;
 check('a game pause is not counted as download failure or a notification',()=>{assert.equal(f.policy().failureCount,0);assert.equal(f.calls.notice,0);});
 assert.equal((await f.scheduler.read()).phase,'paused');
}
{
 const f=fixture();f.holdPrepare(async()=>{f.setVersion('99.0.3');});await f.scheduler.run();
 check('a skipped-version transfer reports the actual latest completed version',()=>{assert.equal(f.current().info?.version,'99.0.3');});
 assert.equal((await f.scheduler.read()).version,'99.0.3');
}
{
 const f=fixture();f.holdPrepare(async()=>{f.game(true);});await f.scheduler.run();
 check('game entry racing a successful final download cannot announce or reschedule it',()=>{assert.equal(f.calls.notice,0);assert.equal(f.policy().failureCount,0);});
}
{
 const f=fixture();f.savedReady();f.holdPrepare(async()=>{f.game(true);});await f.scheduler.run();
 check('game entry during startup cache proof defers the ready notification',()=>assert.equal(f.calls.notice,0));
}
{
 const f=fixture();f.savedReady();f.manifest('99.0.3');await f.scheduler.run();
 check('fresh startup or post-game manifest supersedes old cache before consuming boot notice',()=>{assert.deepEqual(f.calls.noticeVersions,['99.0.3']);assert.equal(f.calls.download,1);});
}
}
else {
 const interval=6*60*60*1000;
 const snapshots:any[]=[];
 const delay=(f:ReturnType<typeof fixture>)=>Math.min(...[...f.timers.values()].map(t=>t.at-f.now()));
 {
  const f=fixture({enabled:false});f.scheduler.start();await f.advance(15000);const before={...f.calls};
  const wakes=await f.advance(120000);
  check('disabled empty cache owns no timer and repeats no refresh',()=>{assert.equal(f.timers.size,0);assert.equal(wakes,0);assert.deepEqual(f.calls,before);});
  snapshots.push({case:'disabled',wakes,refreshes:f.calls.refresh-before.refresh,timers:f.timers.size});f.scheduler.stop();
 }
 {
  const f=fixture({nextCheckAt:1000000000+interval});f.scheduler.start();await f.advance(15000);const before={...f.calls};
  const wakes=await f.advance(120000);
  check('healthy idle schedules the actual six-hour deadline',()=>{assert.equal(f.timers.size,1);assert.equal(wakes,0);assert.deepEqual(f.calls,before);assert.equal(delay(f),interval-135000);});
  snapshots.push({case:'waiting-six-hours',wakes,refreshes:f.calls.refresh-before.refresh,timers:f.timers.size,nextDelayMs:delay(f)});
  await f.advance(interval-135000);assert.equal(f.calls.check,1);assert.equal(f.calls.download,1);f.scheduler.stop();
 }
 {
  const f=fixture();f.offline();f.scheduler.start();await f.advance(15000);
  check('offline idle stops until an online wake',()=>assert.equal(f.timers.size,0));
  f.offline(false);f.scheduler.wake(true);await f.advance(1000);assert.equal(f.calls.download,1);f.scheduler.stop();
 }
 {
  const f=fixture();f.game(true);f.scheduler.start();await f.advance(15000);
  check('game admission pauses the pump and existing recovery wake resumes it',()=>assert.equal(f.timers.size,0));
  f.game(false);f.scheduler.wake(true);await f.advance(1000);assert.equal(f.calls.download,1);f.scheduler.stop();
 }
 {
  const f=fixture();f.checkFails();f.scheduler.start();await f.advance(15000);
  check('first failure schedules its actual one-minute backoff',()=>{assert.equal(delay(f),60000);assert.equal(f.policy().failureCount,1);});
  await f.advance(59999);assert.equal(f.calls.check,1);await f.advance(1);assert.equal(f.calls.check,2);assert.equal(delay(f),300000);f.scheduler.stop();
 }
 {
  const f=fixture();f.noticeAvailable(false);f.scheduler.start();await f.advance(15000);
  check('declined ready notice retains the original thirty-second retry',()=>assert.equal(delay(f),30000));
  await f.advance(30000);assert.equal(f.calls.notice,2);f.noticeAvailable(true);await f.advance(30000);assert.equal(f.calls.ack,1);assert.equal(delay(f),interval-60000);f.scheduler.stop();
 }
 {
  const f=fixture({enabled:false,lastNotifiedBoot:'boot-A'});f.savedReady();f.offline();f.scheduler.start();await f.advance(15000);
  check('disabled acknowledged cache retains its proof expiry deadline',()=>{assert.equal(f.calls.cache,1);assert.equal(delay(f),interval);});
  const late=[...f.timers.values()];f.scheduler.stop();const before={...f.calls};for(const t of late)t.fn();for(let i=0;i<100;i++)await Promise.resolve();assert.equal(f.timers.size,0);assert.deepEqual(f.calls,before);
 }
 {
  const f=fixture();f.scheduler.start();await f.advance(15000);f.scheduler.invalidateCache();await f.advance(30000);
  check('cache cleanup quiet window schedules only its remaining deadline',()=>assert.equal(delay(f),570000));f.scheduler.stop();
 }
 console.log('UPDATER_IDLE_TIMER_MEASUREMENTS '+JSON.stringify(snapshots));
}
console.log('UPDATER_PREDOWNLOAD_SELFTEST_OK checks='+checks+' (fake clock and isolated IO; no network, install or device changes)');
