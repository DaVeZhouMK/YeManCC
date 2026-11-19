import assert from 'node:assert/strict';
import {installUpdaterFixtureEnvironment} from './lib/updater_fixture_environment';
import { APP_VERSION } from '../src/version';
const mode=process.argv[2]||'same-version',sha='A'.repeat(64),parts=APP_VERSION.split('.').map(Number);
const future=`${parts[0]}.${parts[1]}.${parts[2]+1}`;let receive:(event:{data:any})=>void=()=>{};
let saved:any={phase:'interrupted',stage:'install',version:APP_VERSION,sha256:sha,operationId:'authorized-previous-install',repairAuthorized:true,installerActive:false};
if(mode==='forged')saved.repairAuthorized=false;
if(mode==='active'||mode==='completed')Object.assign(saved,{phase:'installing',installerActive:true});
const hintMode=['active-hint','active-revision-hint','repair-hint','hint-read-fails','hint-unknown-read','hint-completed'].includes(mode);
if(hintMode)Object.assign(saved,{version:future,phase:mode==='repair-hint'?'interrupted':'installing',installerActive:mode!=='repair-hint',repairAuthorized:mode==='repair-hint'});
let heldCheck:(()=>void)|undefined;
const lateCheckMode=mode.startsWith('late-old-check-');
const oldInstallMode=/^old-(failure|interrupted)-(same-manifest|new-version|new-sha|check-fails)$/.test(mode);
if(oldInstallMode)Object.assign(saved,{version:future,phase:mode.startsWith('old-interrupted-')?'interrupted':'failed',installerActive:false,repairAuthorized:false,error:'old install failure'});
if(lateCheckMode)Object.assign(saved,{version:future,phase:'failed',installerActive:false,repairAuthorized:false,error:'old install failure'});
if(mode==='older')saved.version='0.0.1';
const calls:any[]=[];let installs=0,downloads=0;
class FixtureEvent extends Event{detail:unknown;constructor(name:string,init:any){super(name);this.detail=init.detail;}}
(globalThis as any).CustomEvent=FixtureEvent;const win=new EventTarget() as any;
win.chrome={webview:{addEventListener(_name:string,handler:typeof receive){receive=handler;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{
 let result:any;if(req.cmd==='app.updateState'){if(saved.readFails){receive({data:{id:req.id,error:'fixture native observation unavailable'}});return;}result={...saved};}
 else if(req.cmd==='app.checkUpdate'){
  if(lateCheckMode){heldCheck=()=>receive({data:mode.endsWith('error')?{id:req.id,error:'old delayed manifest error'}:{id:req.id,result:{version:future,sha256:sha}}});return;}
  if(mode==='old-failure-check-fails'){receive({data:{id:req.id,error:'fixture manifest offline'}});return;}
  result={version:oldInstallMode?(mode.endsWith('new-version')?`${parts[0]}.${parts[1]}.${parts[2]+2}`:future):mode==='successor'?future:APP_VERSION,sha256:mode.endsWith('new-sha')?'B'.repeat(64):sha};
 }
 else if(req.cmd==='app.installUpdate'){installs++;if(mode==='start-failed'){receive({data:{id:req.id,error:'UPDATE_INSTALLER_START_FAILED: helper did not acquire installation ownership'}});return;}if(mode==='missing-cache'&&installs===1){receive({data:{id:req.id,error:'No update package downloaded'}});return;}result=true;}
 else if(req.cmd==='app.downloadUpdate'){assert.equal(req.args?.background===true,oldInstallMode);downloads++;result=true;}
 else throw Error('FORBIDDEN '+req.cmd);
 receive({data:{id:req.id,result}});
 });}}};installUpdaterFixtureEnvironment(win, event => receive(event));
(globalThis as any).window=win;
const m=await import('../src/bridge/updateManager');await m.ensureUpdateManager();let checks=0;
function check(fn:()=>void){fn();checks++;}
if(lateCheckMode){
 const checking=m.checkForUpdate(APP_VERSION,true);
 for(let attempt=0;attempt<100&&!heldCheck;attempt++)await Promise.resolve();assert(heldCheck,'manifest must actually be in flight');
 const pending={version:mode.includes('new-version')?`${parts[0]}.${parts[1]}.${parts[2]+2}`:future,sha256:'B'.repeat(64)};
 await m.restoreBackgroundTarget(pending);const newOperation=m.updateSnapshot.operationId;
 heldCheck();const checked=await checking;
 check(()=>{assert.equal(m.updateInfo.value?.version,pending.version);assert.equal(m.updateInfo.value?.sha256,pending.sha256);assert.equal(m.updateSnapshot.operationId,newOperation);assert.equal(m.updateSnapshot.phase,'available');assert.equal(m.updateSnapshot.stage,'download');assert.equal(m.updateSnapshot.error,'');assert.equal(installs,0);assert.equal(downloads,0);});
 assert.equal(checked,false);
}else if(hintMode){
 const pending={version:mode==='active-revision-hint'?future:`${parts[0]}.${parts[1]}.${parts[2]+2}`,sha256:'B'.repeat(64)};
 const oldOperation=m.updateSnapshot.operationId;
 await m.restoreBackgroundTarget(pending);
 check(()=>{assert.equal(m.updateInfo.value?.version,future);assert.equal(m.updateSnapshot.operationId,oldOperation);assert.equal(m.updateSnapshot.installerActive,mode!=='repair-hint');assert.equal(m.updateSnapshot.repairAuthorized,mode==='repair-hint');assert.equal(downloads,0);assert.equal(installs,0);});
 const beforeCheck=calls.filter(x=>x.cmd==='app.checkUpdate').length;await m.checkForUpdate(APP_VERSION,true);
 check(()=>assert.equal(calls.filter(x=>x.cmd==='app.checkUpdate').length,beforeCheck));
 if(mode==='hint-read-fails'){
  saved.readFails=true;await assert.rejects(m.refreshAdoptedUpdate(),/unavailable/);
  check(()=>{assert.equal(m.updateSnapshot.installerActive,true);assert.equal(m.updateSnapshot.operationId,oldOperation);assert.equal(m.updateInfo.value?.version,future);});
 }else if(mode!=='repair-hint'){
  if(mode==='hint-unknown-read'){
   const {installerActive,repairAuthorized,...unknown}=saved;saved={...unknown,phase:'failed'};
   await m.refreshAdoptedUpdate();check(()=>{assert.equal(m.updateSnapshot.installerActive,true);assert.equal(m.updateInfo.value?.sha256,sha);assert.equal(downloads,0);assert.equal(installs,0);});
  }
  if(mode!=='hint-completed'){
   await m.refreshAdoptedUpdate();check(()=>{assert.equal(m.updateSnapshot.installerActive,true);assert.equal(m.updateInfo.value?.sha256,sha);});
  }
  saved={...saved,installerActive:false,repairAuthorized:false,phase:'completed'};await m.refreshAdoptedUpdate();
  check(()=>{assert.equal(m.updateSnapshot.installerActive,false);assert.equal(m.updateInfo.value?.version,pending.version);assert.equal(m.updateInfo.value?.sha256,pending.sha256);assert.equal(m.updateSnapshot.phase,'available');assert.equal(downloads,0);assert.equal(installs,0);});
 }
}else if(mode==='old-failure-check-fails'){
 check(()=>assert.equal(m.updateSnapshot.error,'old install failure'));
 assert.equal(await m.checkForUpdate(APP_VERSION,true),false);
 check(()=>{assert.equal(m.updateInfo.value?.version,future);assert.equal(m.updateSnapshot.phase,'failed');assert.equal(m.updateSnapshot.stage,'install');assert.match(m.updateSnapshot.error,/manifest offline/);assert.equal(installs,0);assert.equal(downloads,0);});
}else if(mode==='old-failure-same-manifest'){
 await m.checkForUpdate(APP_VERSION,true);check(()=>{assert.equal(m.updateInfo.value?.version,future);assert.equal(m.updateSnapshot.phase,'failed');assert.equal(m.updateSnapshot.stage,'install');assert.equal(m.updateSnapshot.error,'old install failure');assert.equal(installs,0);assert.equal(downloads,0);});
}else if(oldInstallMode){
 await m.checkForUpdate(APP_VERSION,true);
 check(()=>{assert.equal(m.updateSnapshot.phase,'available');assert.equal(m.updateSnapshot.stage,'download');assert.equal(m.updateInfo.value?.version,mode.endsWith('new-version')?`${parts[0]}.${parts[1]}.${parts[2]+2}`:future);assert.equal(m.updateInfo.value?.sha256,mode.endsWith('new-sha')?'B'.repeat(64):sha);assert.equal(installs,0);assert.equal(downloads,0);});
 assert.equal(await m.prepareUpdate({background:true}),true);
 check(()=>{assert.equal(m.updateSnapshot.phase,'downloaded');assert.equal(downloads,1);assert.equal(installs,0);});
}else if(mode==='forged'||mode==='older'){
 check(()=>{assert.equal(m.updateInfo.value,null);assert.equal(m.updateSnapshot.phase,'idle');});
 await m.downloadAndInstall();check(()=>assert.equal(installs,0));
}else if(mode==='active'||mode==='completed'){
 check(()=>{assert.equal(m.updateSnapshot.phase,'installing');assert.equal(m.updateSnapshot.installerActive,true);});
 await m.downloadAndInstall();check(()=>assert.equal(installs,0));
 saved={...saved,installerActive:false,phase:mode==='completed'?'completed':'interrupted',repairAuthorized:mode!=='completed'};
 await m.refreshAdoptedUpdate();check(()=>assert.equal(m.updateSnapshot.phase,saved.phase));
 if(mode==='active'){await m.downloadAndInstall();check(()=>assert.equal(installs,1));}
 else{await m.checkForUpdate(APP_VERSION);check(()=>assert.equal(m.updateSnapshot.phase,'latest'));}
}else{
 check(()=>{assert.equal(m.updateInfo.value?.version,APP_VERSION);assert.equal(m.updateSnapshot.phase,'interrupted');});
 await m.checkForUpdate(APP_VERSION);
 check(()=>{assert.equal(m.updateInfo.value?.version,mode==='successor'?future:APP_VERSION);assert.equal(m.updateSnapshot.phase,mode==='successor'?'available':'interrupted');});
 check(()=>assert.equal(m.updateSnapshot.repairAuthorized,true));
 await m.downloadAndInstall();check(()=>assert.equal(installs,mode==='missing-cache'?2:1));
 if(mode==='missing-cache')check(()=>assert.equal(downloads,1));
 if(mode==='start-failed')check(()=>{assert.equal(downloads,0);assert.equal(m.updateSnapshot.phase,'failed');});
}
console.log(JSON.stringify({status:'PASS',mode,checks,calls:calls.map(x=>x.cmd),actualInstallation:false}));
