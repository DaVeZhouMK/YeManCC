import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
const mode=process.argv[2]||'same-version',sha='A'.repeat(64),parts=APP_VERSION.split('.').map(Number);
const future=`${parts[0]}.${parts[1]}.${parts[2]+1}`;let receive:(event:{data:any})=>void=()=>{};
let saved:any={phase:'interrupted',stage:'install',version:APP_VERSION,sha256:sha,operationId:'authorized-previous-install',repairAuthorized:true,installerActive:false};
if(mode==='forged')saved.repairAuthorized=false;
if(mode==='active'||mode==='completed')Object.assign(saved,{phase:'installing',installerActive:true});
if(mode==='older')saved.version='0.0.1';
const calls:any[]=[];let installs=0,downloads=0;
class FixtureEvent extends Event{detail:unknown;constructor(name:string,init:any){super(name);this.detail=init.detail;}}
(globalThis as any).CustomEvent=FixtureEvent;const win=new EventTarget() as any;
win.chrome={webview:{addEventListener(_name:string,handler:typeof receive){receive=handler;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{
 let result:any;if(req.cmd==='app.updateState')result={...saved};
 else if(req.cmd==='app.checkUpdate')result={version:mode==='successor'?future:APP_VERSION,sha256:sha};
 else if(req.cmd==='app.installUpdate'){installs++;if(mode==='start-failed'){receive({data:{id:req.id,error:'UPDATE_INSTALLER_START_FAILED: helper did not acquire installation ownership'}});return;}if(mode==='missing-cache'&&installs===1){receive({data:{id:req.id,error:'No update package downloaded'}});return;}result=true;}
 else if(req.cmd==='app.downloadUpdate'){assert.equal(req.args?.background===true,false);downloads++;result=true;}
 else throw Error('FORBIDDEN '+req.cmd);
 receive({data:{id:req.id,result}});
 });}}};(globalThis as any).window=win;
const m=await import('../src/bridge/updateManager');await m.ensureUpdateManager();let checks=0;
function check(fn:()=>void){fn();checks++;}
if(mode==='forged'||mode==='older'){
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
