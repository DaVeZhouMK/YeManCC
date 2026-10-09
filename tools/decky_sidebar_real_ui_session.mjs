// Real Steam + locked mainline Loader/plugin, exact production mirror host with memory-only owners.
// This fixture never launches the product YMCC EXE, writes real configs or invokes physical hardware.
import fs from 'node:fs';import path from 'node:path';import {spawn} from 'node:child_process';
import {root,task} from './fixtures/decky_task_paths.mjs';import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
import {targets,connect} from './fixtures/decky_cdp.mjs';
const directory=path.join(task,'Build/RealSteamUI','run-'+Date.now()),home=path.join(directory,'home');fs.mkdirSync(home,{recursive:true});
fs.cpSync(path.join(root,'PowerControl/decky/plugins'),path.join(home,'plugins'),{recursive:true});
const report={project:'YMCC Decky 侧边栏',stage:'Mainline23 real UI',startedUtc:new Date().toISOString(),sourceRoot:root,home,physicalHardwareWrites:0,realBusinessConfigWrites:0,actualSteam:true,actualProductYmcc:false,fixtureData:true,controllerEvents:[],pluginBytes:fs.readdirSync(path.join(root,'PowerControl/decky/plugins/ymcc-sidebar')).filter(name=>name!=='dist').reduce((total,name)=>total+fs.statSync(path.join(root,'PowerControl/decky/plugins/ymcc-sidebar',name)).size,fs.statSync(path.join(root,'PowerControl/decky/plugins/ymcc-sidebar/dist/index.js')).size)};
const sessionName=process.env.YMCC_DECKY_UI_RUN_NAME||'MAINLINE15-REAL-UI-RUN';if(!/^[A-Z0-9-]+$/.test(sessionName))throw Error('UI evidence name rejected');
const out=path.join(task,'validation/'+sessionName+'.json');
const persist=()=>fs.writeFileSync(out,JSON.stringify(report,null,2));persist();
const broker=spawn(path.join(task,'Build/decky-sidebar-broker-test.exe'),['--serve-relay-fixture'],{windowsHide:true,stdio:['pipe','pipe','pipe']});
const controller=spawn(path.join(task,'Build/decky-sidebar-ui-controller.exe'),[path.join(root,'PowerControl/decky/PluginLoader_noconsole.exe'),home],{windowsHide:true,stdio:['pipe','pipe','pipe']});
let host,boot,b1='',c1='',stopped=false;let resolveBoot,rejectBoot;
const bootReady=new Promise((resolve,reject)=>{resolveBoot=resolve;rejectBoot=reject;});
for(const [child,name] of [[broker,'broker'],[controller,'controller']]){child.once('error',rejectBoot);child.stderr.on('data',data=>{report[name+'Errors']=(report[name+'Errors']||'')+String(data).slice(0,1000);persist();});child.on('exit',(code,signal)=>{report[name+'Terminal']={code,signal};persist();console.log(JSON.stringify({event:name+'-exit',code,signal}));});}
broker.stdout.on('data',data=>{b1+=data;for(;;){const i=b1.indexOf('\n');if(i<0)break;const line=b1.slice(0,i);b1=b1.slice(i+1);try{const event=JSON.parse(line);if(event.endpoint){boot=event;resolveBoot(event);}else if(event.type==='peer')host?.emit('deckySidebar.mirrorPeer',event);else if(event.type==='request'){if(event.request?.command!=='snapshot'){report.mutationRequests??=[];report.mutationRequests.push({command:event.request?.command,args:event.request?.args,at:Date.now()});}host?.emit('deckySidebar.mirrorRequest',event);setTimeout(()=>{report.hostCounts=host?.counts;persist();},100);}}catch{}}});
controller.stdout.on('data',data=>{c1+=data;for(;;){const i=c1.indexOf('\n');if(i<0)break;const line=c1.slice(0,i);c1=c1.slice(i+1);try{const event=JSON.parse(line);report.controllerEvents.push(event);persist();console.log(JSON.stringify(event));}catch{}}});
let deadline,watcher;
const stopPath=path.join(directory,'stop-request');
const statePath=path.join(directory,'memory-state-request');let lastState='';
report.memoryStateRequestPath=statePath;
function applyMemoryState(){
 if(!host||!fs.existsSync(statePath))return;const text=fs.readFileSync(statePath,'utf8');if(text===lastState)return;lastState=text;
 const change=JSON.parse(text);if(Object.keys(change).some(key=>!['powerSource','game'].includes(key)) || change.powerSource!==undefined&&!['ac','dc',null].includes(change.powerSource)||change.game!==undefined&&change.game!==null)throw Error('Memory fixture state request rejected');
 if(change.powerSource!==undefined){host.state.powerSource=change.powerSource;host.powerChanged();}
 if(change.game===null){host.state.game=null;host.gameChanged();}
 report.memoryFixtureState=change;persist();
}
watcher=fs.watch(directory,()=>{if(fs.existsSync(stopPath))void stop();else applyMemoryState();});
async function stop(){if(stopped)return;stopped=true;clearTimeout(deadline);watcher?.close();host?.stop();controller.stdin.write('quit\n');broker.stdin.write('stop\n');report.finishedUtc=new Date().toISOString();report.hostCounts=host?.counts;persist();setTimeout(()=>{process.exitCode=0;},500);}
process.stdin.setEncoding('utf8');process.stdin.on('data',data=>{if(data.includes('quit'))void stop();});
try{
 await Promise.race([bootReady,new Promise((_,reject)=>setTimeout(()=>reject(Error('Native broker boot deadline')),12000))]);
 host=await createMirrorHostFixture({invoke:async(command,args)=>{if(command==='deckySidebar.mirrorReply'){broker.stdin.write(JSON.stringify(args)+'\n');return true;}if(command==='deckySidebar.mirrorAttach')return {active:true,runId:boot.runId,revision:0};throw Error('Unexpected physical IPC request: '+command);}});
 host.state.game.title='测试 · 仅内存数据，不操作硬件';host.ready();
 // Test-only bootstrap must follow Loader's initial shared-context reload.
 // No constant credential reinjection or resident retry: finite startup window.
 await new Promise(resolve=>setTimeout(resolve,2000));
 let context;
 for(let attempt=0;attempt<30;attempt++){
  context=(await targets()).find(t=>t.title==='SharedJSContext');
  if(context){let probe;try{probe=await connect(context.webSocketDebuggerUrl);if(await probe.evaluate("typeof DFL !== 'undefined' && typeof DeckyPluginLoader !== 'undefined'"))break;}catch{}finally{probe?.close();}}
  if(attempt===29)throw Error('Pinned Loader UI readiness deadline');
  await new Promise(resolve=>setTimeout(resolve,200));
 }
 if(!context)throw Error('Steam shared context missing');const cdp=await connect(context.webSocketDebuggerUrl);
 await cdp.evaluate(`Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:Object.freeze(${JSON.stringify(boot)}),configurable:true,writable:false});window.dispatchEvent(new Event('ymcc-decky-bootstrap'));true`);cdp.close();report.bootstrapInjected=true;report.stopRequestPath=stopPath;report.servedProductionScriptBytes=fs.statSync(path.join(home,'plugins/ymcc-sidebar/dist/index.js')).size;report.servedProductionScriptSha256=(await import('node:crypto')).createHash('sha256').update(fs.readFileSync(path.join(home,'plugins/ymcc-sidebar/dist/index.js'))).digest('hex');persist();console.log(JSON.stringify({event:'memory-host-ready',mainlineSource:true,hardware:false}));
 const seconds=Number(process.env.YMCC_DECKY_UI_SECONDS||180);if(!Number.isSafeInteger(seconds)||seconds<30||seconds>600)throw Error('UI duration rejected');
 deadline=setTimeout(()=>{report.deadlineReached=true;void stop();},seconds*1000);
}catch(error){report.failure=error.message;persist();console.error(error.message);await stop();process.exitCode=1;}
