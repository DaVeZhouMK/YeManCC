import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
const arg=process.argv[2]||'stale-check';
const p=APP_VERSION.split('.').map(Number),version=p[0]+'.'+p[1]+'.'+(p[2]+1),sha='D'.repeat(64);
let receive:(event:{data:any})=>void=()=>{},checks=0;
const calls:string[]=[];
const saved=arg==='stale-check'?{phase:'checking',stage:'download',version,sha256:sha}:arg==='missing-install-info'?{phase:'installing',stage:'install'}:{phase:'validating',stage:'download',version,sha256:'corrupt'};
class FixtureEvent extends Event{detail:unknown;constructor(type:string,init:{detail?:unknown}){super(type);this.detail=init.detail;}}
(globalThis as any).CustomEvent=FixtureEvent;
const win=new EventTarget() as any;win.chrome={webview:{addEventListener(_name:string,fn:typeof receive){receive=fn;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req.cmd);queueMicrotask(()=>{if(req.cmd==='app.updateState')receive({data:{id:req.id,result:saved}});else if(req.cmd==='app.checkUpdate')receive({data:{id:req.id,result:{version,sha256:sha}}});else throw Error('FORBIDDEN '+req.cmd);});}}};
(globalThis as any).window=win;
const m=await import('../src/bridge/updateManager');await m.ensureUpdateManager();
assert.ok(!['checking','downloading','validating','installing'].includes(m.updateSnapshot.phase));checks++;
await m.checkForUpdate(APP_VERSION);assert.equal(m.updateSnapshot.phase,'available');assert.equal(m.updateInfo.value?.version,version);assert.equal(calls.filter(x=>x==='app.checkUpdate').length,1);checks++;
console.log('UPDATER_CORRUPT_STATE_SELFTEST_OK case='+arg+' checks='+checks+' (mock IPC only)');
