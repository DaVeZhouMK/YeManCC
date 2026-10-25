import {task,root,validation} from './fixtures/decky_task_paths.mjs';
// Real Windows native broker/socket, production Decky client; no Steam, settings or hardware.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawn} from 'node:child_process';
import {createRequire} from 'node:module';
import {SocketAdapter,awaitState,bounded,deferred} from './fixtures/decky_native_transport.mjs';
const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
function load(file,dependencies={}){const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,file),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,Date,require:name=>{if(name in dependencies)return dependencies[name];throw Error('Unexpected import '+name);}});return module.exports;}
const context=load('decky-plugin/src/steamRunningContext.ts');
const {MirrorClient}=load('decky-plugin/src/mirrorClient.ts',{'./steamRunningContext':context});
const child=spawn(path.join(task,'Build/decky-sidebar-broker-test.exe'),['--serve-relay-fixture'],{windowsHide:true,stdio:['pipe','pipe','pipe']});
let buffer='',stderr='',client,previous=false;
const bootstrap=deferred(),summary=deferred(),requests=[],sockets=[],frames=[],timers=new Set(),cases=[];
const terminal=new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',(code,signal)=>resolve({code,signal}));});
const write=value=>child.stdin.write(JSON.stringify(value)+'\n');
child.stderr.on('data',value=>{stderr+=String(value);});
child.stdout.on('data',data=>{
  buffer+=String(data);
  for(;;){const end=buffer.indexOf('\n');if(end<0)break;const line=buffer.slice(0,end).trim();buffer=buffer.slice(end+1);if(!line)continue;
    try{const event=JSON.parse(line);
      if(event.endpoint)bootstrap.resolve(event);
      else if(event.type==='peer'&&event.connected)write({connection:event.connection,message:{type:'hud-ready',protocol:1,runId:event.runId}});
      else if(event.type==='request')requests.push(event.request);
      else if(event.endpointReleased)summary.resolve(event);
    }catch(e){summary.reject(e);}
  }
});
async function waitFor(predicate,label){await bounded((async()=>{while(!predicate())await new Promise(resolve=>setTimeout(resolve,10));})(),label);}
try{
  const boot=await bounded(bootstrap.promise,'native bootstrap');boot.hudProtocol=1;
  client=new MirrorClient({bootstrap:()=>boot,clientId:'hud-loopback-inert',socket:url=>{const s=new SocketAdapter(url);sockets.push(s);return s;},
    schedule:(fn,delay)=>{const id=setTimeout(()=>{timers.delete(id);fn();},delay);timers.add(id);return id;},cancel:id=>{clearTimeout(id);timers.delete(id);}});
  client.subscribeExtra(frame=>frames.push(frame));
  client.subscribe(state=>{if(state.connected&&!previous)client.sendHud('hud.status',{capable:true,game:null,shown:false,hiddenConfirmed:true,session:null,operation:0,reason:'Loopback only; no Steam window'});previous=state.connected;});
  client.setPassiveVisible(true);await awaitState(client,state=>state.connected);
  await waitFor(()=>frames.length===1&&requests.some(r=>r.command==='hud.status'),'HUD ready/status');
  assert.equal(client.snapshot().snapshot,null);assert.equal(timers.size,0);assert.equal(sockets.length,1);
  assert.equal(requests.some(r=>r.command==='snapshot'&&r.subscribe!==false),false);cases.push('Hidden HUD authenticates and reports status to native broker with no business subscription or idle transport timer');
  client.setVisible(true);await waitFor(()=>requests.some(r=>r.command==='snapshot'&&r.subscribe!==false),'sidebar request');
  assert.equal(client.snapshot().connected,true);assert.equal(client.snapshot().snapshot,null);assert.equal(client.sendHud('hud.status',{capable:true,game:null,shown:false,hiddenConfirmed:true,session:null,operation:0,reason:'Delayed snapshot'}),true);
  assert.equal(timers.size,0);cases.push('Delayed native business reply cannot block HUD status on the shared authenticated socket');
  client.setVisible(false);assert.equal(sockets[0].closed,false);assert.equal(client.snapshot().connected,true);
  await waitFor(()=>requests.filter(r=>r.command==='snapshot'&&r.subscribe===false).length>=2,'sidebar pause');cases.push('Hiding the console sends a subscription pause and preserves the native HUD socket');
  sockets[0].close();await awaitState(client,state=>!state.connected);await awaitState(client,state=>state.connected);
  await waitFor(()=>frames.length===2&&requests.filter(r=>r.command==='hud.status').length>=3,'recovered native HUD');
  assert.equal(sockets.length,2);assert.equal(client.snapshot().snapshot,null);assert.equal(timers.size,0);
  assert.equal(sockets[1].sent.some(r=>r.command==='snapshot'&&r.subscribe!==false),false);cases.push('Actual native loopback disconnect recovers HUD while the console stays hidden and restores status without business replay');
  await assert.rejects(client.mutate('monitor.setEnabled',{enabled:true}));assert.equal(requests.some(r=>!['hud.status','snapshot'].includes(r.command)),false);cases.push('Background recovery sends no monitor/fan/game command and cannot mutate hidden business controls');
  client.dispose();await waitFor(()=>sockets.every(s=>s.closed),'closed sockets');assert.equal(timers.size,0);
  child.stdin.write('stop\n');const final=await bounded(summary.promise,'broker cleanup');const exit=await bounded(terminal,'broker exit');
  assert.equal(exit.code,0);assert.equal(stderr,'');assert.equal(final.activeAfterStop,false);cases.push('Plugin unload clears every retry/deadline, releases native socket and leaves broker stopped');
  const report={passed:cases.length,cases,actualWindowsNativeBroker:true,actualLoopbackSocket:true,actualSteam:false,realConfigWrites:0,hardwareWrites:0};
  fs.writeFileSync(path.join(validation,'HUD-TRANSPORT-LOOPBACK.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{client?.dispose();for(const socket of sockets)socket.close();if(child.exitCode===null){child.stdin.write('stop\n');await bounded(terminal,'final broker exit').catch(()=>child.kill());}}
