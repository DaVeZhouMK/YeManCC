// One read of production sleep.inspect through an already enabled local Steam
// context. No rescope operation, setters, game launch, listeners or device IO.
import fs from 'node:fs';import path from 'node:path';
const [output]=process.argv.slice(2);if(!output||!path.isAbsolute(output))throw Error('absolute evidence output required');
const contexts=await (await fetch('http://127.0.0.1:8080/json/list',{signal:AbortSignal.timeout(5000)})).json();
const target=contexts.find(t=>t.title==='SharedJSContext');
if(!target||!/^ws:\/\/127\.0\.0\.1:8080\/devtools\/page\/[A-F0-9]+$/i.test(target.webSocketDebuggerUrl))throw Error('verified existing local Steam context unavailable');
const source=fs.readFileSync(new URL('../native/steam_live.js',import.meta.url),'utf8').trim();
const expression=`(async()=>{const account=window.settingsStore?.m_CMInterface?.steamid?.GetAccountID?.();if(!Number.isInteger(account))return {ok:false,reason:'account-unavailable'};return (${source})({operation:'sleep.inspect',account,appids:[]});})()`;
const socket=new WebSocket(target.webSocketDebuggerUrl);let timer;
const bounded=p=>Promise.race([p,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('read-only Steam deadline')),10000)})]);
try {
 await bounded(new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',()=>reject(Error('socket error')),{once:true})}));clearTimeout(timer);
 const result=await bounded(new Promise((resolve,reject)=>{socket.addEventListener('message',event=>{const r=JSON.parse(event.data);if(r.id!==1)return;r.error?reject(Error('CDP error')):resolve(r.result)});socket.addEventListener('close',()=>reject(Error('context closed')),{once:true});socket.send(JSON.stringify({id:1,method:'Runtime.evaluate',params:{expression,returnByValue:true,awaitPromise:true,timeout:5000}}));}));
 if(result.exceptionDetails)throw Error('Steam read expression failed');
 const snapshot=result.result?.value;if(!snapshot||typeof snapshot!=='object')throw Error('no snapshot');
 const evidence={kind:'production-sleep.inspect-readonly-existing-local-context',atUtc:new Date().toISOString(),result:snapshot,operations:{steamRescope:0,steamSettingsWrites:0,gameLaunches:0,controllerWrites:0,fanWrites:0,powerChanges:0},limits:'Reference host only, not the supplied symptom handheld; real continue button/input not verified.'};
 fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify(evidence,null,2));
 console.log(JSON.stringify({output,ok:snapshot.ok,runningAppIDs:snapshot.runningAppIDs,overviews:snapshot.overviews?.map(v=>({appid:v.appid,key:v.overviewKey,local:v.localDisplayStatus,selected:v.selectedDisplayStatus,mostAvailable:v.mostAvailableDisplayStatus,inRunningList:v.inRunningList,aliasMismatch:v.runningAliasMismatch}))}));
}finally{clearTimeout(timer);socket.close()}
