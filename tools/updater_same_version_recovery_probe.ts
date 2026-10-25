// Read-only defect probe. Reports an OPEN requirement, never a successful gate.
import { APP_VERSION } from '../src/version';
let receive:(event:{data:any})=>void=()=>{};const calls:string[]=[];
const saved={phase:'interrupted',stage:'install',version:APP_VERSION,sha256:'F'.repeat(64),operationId:'interrupted-after-executable-replacement',downloadActive:false};
class FixtureEvent extends Event {detail:unknown;constructor(name:string,init:any){super(name);this.detail=init.detail;}}
(globalThis as any).CustomEvent=FixtureEvent;const win=new EventTarget() as any;
win.chrome={webview:{addEventListener(_name:string,handler:typeof receive){receive=handler;},postMessage(request:any){if(typeof request.id!=='number')return;calls.push(request.cmd);queueMicrotask(()=>{if(request.cmd==='app.updateState')receive({data:{id:request.id,result:saved}});else if(request.cmd==='app.checkUpdate')receive({data:{id:request.id,result:{version:APP_VERSION,sha256:saved.sha256}}});else throw Error('FORBIDDEN probe side effect '+request.cmd);});}}};
(globalThis as any).window=win;
const manager=await import('../src/bridge/updateManager');await manager.ensureUpdateManager();
const restored={phase:manager.updateSnapshot.phase,message:manager.updateSnapshot.message,hasInstallInfo:!!manager.updateInfo.value};
await manager.downloadAndInstall();await manager.checkForUpdate(APP_VERSION);
const gap=!restored.hasInstallInfo&&manager.updateSnapshot.phase==='latest'&&!calls.includes('app.installUpdate');
console.log(JSON.stringify({status:gap?'GAP_CONFIRMED':'NOT_REPRODUCED',requirement:'Resume a previously authorized interrupted installation after the executable already reports the target version',currentVersion:APP_VERSION,saved,restored,afterCheck:{phase:manager.updateSnapshot.phase,message:manager.updateSnapshot.message},calls,hardwareIo:false,realFilesWritten:false}));
