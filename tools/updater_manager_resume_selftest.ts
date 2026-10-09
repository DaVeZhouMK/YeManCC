/** Real frontend update manager and IPC, with no native process/network/installation. */
import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
// Always exercise a genuinely newer update; a literal 0.0.34 becomes stale at this release.
const fixtureCurrentVersion = APP_VERSION;
const versionParts = APP_VERSION.split('.').map(Number);
const fixtureUpdateVersion = `${versionParts[0]}.${versionParts[1]}.${versionParts[2] + 1}`;
let listener: (event: {data: any}) => void = () => {};
const calls: {id:number;cmd:string;args:any}[] = [];
const sha = 'A'.repeat(64);
let injectedDownloadFailure = false;
let seenResumeBytes = 0;
class FixtureCustomEvent extends Event {
  detail: unknown;
  constructor(type: string, init: {detail?: unknown}) { super(type); this.detail = init.detail; }
}
(globalThis as any).CustomEvent = FixtureCustomEvent;
const win = new EventTarget() as any;
win.chrome = {webview:{
  addEventListener(_type:string, callback:typeof listener) {listener=callback;},
  postMessage(request:{id:number;cmd:string;args:any}) {
    if(typeof request.id !== 'number') return;
    calls.push(request);
    queueMicrotask(() => {
      if(request.cmd === 'app.updateState') listener({data:{id:request.id,result:{
        phase:'downloading',stage:'download',operationId:'old-operation',version:fixtureUpdateVersion,sha256:sha,
        downloadedBytes:4096,totalBytes:8192,resumedBytes:4096,percent:50,
      }}});
      else if(request.cmd === 'app.checkUpdate') listener({data:{id:request.id,result:{version:fixtureUpdateVersion,sha256:sha}}});
      else if(request.cmd === 'app.downloadUpdate') {
        listener({data:{event:'update.progress',data:{operationId:request.args.operationId,phase:'downloading',
          stage:'download',downloadedBytes:6144,totalBytes:8192,resumedBytes:4096,speedBps:2048}}});
        seenResumeBytes = (win.manager?.updateSnapshot.resumedBytes || 0);
        if(injectedDownloadFailure) listener({data:{id:request.id,error:'fixture download failure'}});
        else listener({data:{id:request.id,result:'INERT VERIFIED ZIP PATH'}});
      } else if(request.cmd === 'app.installUpdate') listener({data:{id:request.id,result:true}});
      else throw new Error(`FIXTURE_FORBIDS_NATIVE_COMMAND ${request.cmd}`);
    });
  },
}};
(globalThis as any).window = win;
const manager = await import('../src/bridge/updateManager');
win.manager = manager;
let checks = 0;
function check(label:string,body:()=>void){body();checks++;console.log('PASS '+label);}
await manager.ensureUpdateManager();
check('saved interrupted download remains retryable after manager restart',()=>{
  assert.equal(manager.updateSnapshot.phase,'interrupted');assert.equal(manager.updateInfo.value?.version,fixtureUpdateVersion);
  assert.equal(manager.updateSnapshot.downloadedBytes,4096);assert.equal(manager.updateSnapshot.sha256,sha);
});
for(const phase of ['checking','downloading','validating','installing'] as const){
  manager.updateSnapshot.phase=phase;const prior=calls.length;
  await manager.checkForUpdate(fixtureCurrentVersion);
  check(`busy ${phase} cannot start a competing manifest request`,()=>{
    assert.equal(calls.length,prior);assert.equal(manager.updateSnapshot.phase,phase);
  });
}
manager.updateSnapshot.phase='interrupted';manager.updateSnapshot.stage='download';
await manager.downloadAndInstall();
check('continue passes the same version and SHA to downloader',()=>{
  const call=calls.find(x=>x.cmd==='app.downloadUpdate')!;
  assert.equal(call.args.version,fixtureUpdateVersion);assert.equal(call.args.sha256,sha);
  assert.ok(call.args.url.endsWith(`/v${fixtureUpdateVersion}/YeManCC.zip`));
});
check('accepted native resume progress reaches the UI snapshot',()=>{assert.equal(seenResumeBytes,4096);});
check('installation only follows a successful download IPC',()=>{
  assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,1);
  assert.equal(manager.updateSnapshot.phase,'installing');
});
check('late progress from a previous operation cannot overwrite current bytes',()=>{
  const previous=manager.updateSnapshot.downloadedBytes;
  listener({data:{event:'update.progress',data:{operationId:'different-old-operation',downloadedBytes:1}}});
  assert.equal(manager.updateSnapshot.downloadedBytes,previous);
});
manager.updateSnapshot.phase='interrupted';manager.updateSnapshot.stage='download';injectedDownloadFailure=true;
const installCount=calls.filter(x=>x.cmd==='app.installUpdate').length;
await manager.downloadAndInstall();
check('failed download cannot launch install and leaves a retryable failure',()=>{
  assert.equal(manager.updateSnapshot.phase,'failed');assert.equal(manager.updateSnapshot.stage,'download');
  assert.match(manager.updateSnapshot.error,/fixture download failure/);
  assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,installCount);
});
console.log(`UPDATER_MANAGER_RESUME_SELFTEST_OK checks=${checks} (mock IPC only; no native/network/install)`);
