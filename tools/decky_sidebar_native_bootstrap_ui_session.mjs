// Real Steam session through exact production native bootstrap. Only the business/device boundary is memory-only.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawn} from 'node:child_process';
import {root,task} from './fixtures/decky_task_paths.mjs';import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const loader=path.join(root,'PowerControl/decky/PluginLoader_noconsole.exe');
if(hash(fs.readFileSync(loader))!=='1d8e06921ced35b0349e677207953d90e548ea254079150d99ced4d360381824')throw Error('Locked real loader mismatch');
const directory=path.join(task,'Build/RealSteamUI','native-'+Date.now()),home=path.join(directory,'home');fs.mkdirSync(home,{recursive:true});
fs.cpSync(path.join(root,'PowerControl/decky/plugins'),path.join(home,'plugins'),{recursive:true});
const stopPath=path.join(directory,'stop-request'),refreshPath=path.join(directory,'refresh-request');
const script=fs.readFileSync(path.join(home,'plugins/ymcc-sidebar/dist/index.js'));
const sessionName=process.env.YMCC_DECKY_UI_RUN_NAME||'MAINLINE16-NATIVE-UI-RUN';if(!/^[A-Z0-9-]+$/.test(sessionName))throw Error('UI evidence name rejected');
const seconds=Number(process.env.YMCC_DECKY_UI_SECONDS||180);if(!Number.isSafeInteger(seconds)||seconds<30||seconds>600)throw Error('UI duration rejected');
const report={project:'YMCC Decky 侧边栏',stage:sessionName,startedUtc:new Date().toISOString(),sourceRoot:root,home,stopRequestPath:stopPath,refreshRequestPath:refreshPath,actualSteam:true,productionNativeRuntime:true,productionNativeBroker:true,productionNativeBootstrap:true,javascriptCredentialInjection:false,fixtureBusinessData:true,physicalHardwareWrites:0,realBusinessConfigWrites:0,productYmccExeExecuted:false,servedProductionScriptBytes:script.length,servedProductionScriptSha256:hash(script),events:[],credentialsRecorded:false};
const out=path.join(task,'validation/'+sessionName+'.json'),persist=()=>fs.writeFileSync(out,JSON.stringify(report,null,2)+'\n');persist();
const child=spawn(path.join(task,'Build/decky-sidebar-ui-controller.exe'),[loader,home,'native-bootstrap'],{windowsHide:true,stdio:['pipe','pipe','pipe']});
let host,buffer='',id=0,stopping=false,deadline,watcher,lastRefresh='';const attaches=new Map();
const terminal=new Promise(resolve=>{child.once('error',error=>{process.exitCode=1;report.failure=error.message;persist();resolve({code:null,signal:null,error:error.message});});child.once('exit',(code,signal)=>{report.controllerTerminal={code,signal};report.hostCounts=host?.counts;persist();console.log(JSON.stringify({event:'native-controller-exit',code,signal}));resolve({code,signal});});});
child.on('error',error=>{report.failure=error.message;persist();void stop();});
child.stderr.on('data',value=>{report.stderr=(report.stderr||'')+String(value).slice(0,2000);persist();});
function send(value){if(!child.stdin.destroyed)child.stdin.write(JSON.stringify(value)+'\n');}
child.stdout.on('data',value=>{buffer+=value;for(;;){const end=buffer.indexOf('\n');if(end<0)break;const line=buffer.slice(0,end);buffer=buffer.slice(end+1);if(!line.trim())continue;
 try{const event=JSON.parse(line);
  if(event.type==='request'){if(event.request?.command!=='snapshot'){report.mutationRequests??=[];report.mutationRequests.push({command:event.request?.command,args:event.request?.args,revision:event.request?.revision});}host?.emit('deckySidebar.mirrorRequest',event);setTimeout(()=>{report.hostCounts=host?.counts;persist();},100);}
  else if(event.type==='peer')host?.emit('deckySidebar.mirrorPeer',event);
  else if(event.type==='attach'){const waiter=attaches.get(event.id);if(waiter){attaches.delete(event.id);clearTimeout(waiter.timer);waiter.resolve(event);}}
  else{report.events.push(event);if(event.type==='runtime')host?.emit('deckySidebar.updated',event);persist();console.log(JSON.stringify(event));}
 }catch(error){report.bridgeError=error.message;persist();}
}});
async function stop(){if(stopping)return;stopping=true;clearTimeout(deadline);watcher?.close();host?.stop();for(const waiter of attaches.values()){clearTimeout(waiter.timer);waiter.reject(Error('Owned UI session closed'));}attaches.clear();if(!child.stdin.destroyed)child.stdin.write('quit\n');await terminal;report.finishedUtc=new Date().toISOString();report.hostCounts=host?.counts;persist();}
host=await createMirrorHostFixture({invoke:async(command,args)=>{
 if(command==='deckySidebar.mirrorReply'){send({type:'reply',...args});return true;}
 if(command==='deckySidebar.mirrorAttach'){const next='attach-'+(++id);return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{attaches.delete(next);reject(Error('Actual native attach deadline'));},7000);attaches.set(next,{resolve,reject,timer});send({type:'attach',id:next});});}
 throw Error('Unexpected physical IPC:'+command);
}});
if(report.failure){host.stop();throw Error(report.failure);}
host.state.game.title='测试 · 内存档位，不操作硬件';host.ready();
watcher=fs.watch(directory,()=>{
 if(fs.existsSync(stopPath)){void stop();return;}
 if(fs.existsSync(refreshPath)){const value=fs.readFileSync(refreshPath,'utf8').trim();if(value!==lastRefresh){lastRefresh=value;const generation=Number(value);if(Number.isSafeInteger(generation)&&generation>0)send({type:'refresh',generation});}}
});
deadline=setTimeout(()=>{report.deadlineReached=true;void stop();},seconds*1000);
process.stdin.on('data',data=>{if(String(data).includes('quit'))void stop();});
