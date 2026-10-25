/** FAN958: real bridge + real HTTP + mock Host supplied by the C# harness.
 * Only native power/process and hardware are fixtures. No production service starts. */
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { HttpFanApiAdapter, type FanNode } from '../src/bridge/fanApi';
import { http, type PowerLifecycleState } from '../src/bridge/api';
const base=process.argv[2];const mode=process.argv[3]??'allowed';const fanGen=mode.startsWith('empty-')?3:2;const requests:string[]=[];
const timeline:any[]=[];let stage='handshake';let observe:()=>object=()=>({});const record=(event:string,details:object={})=>timeline.push({sequence:timeline.length+1,event,stage,at:performance.now(),native:{...native},...observe(),...details});
const curve:FanNode[]=[{tempC:0,dutyPercent:20},{tempC:44,dutyPercent:35},{tempC:78,dutyPercent:60},{tempC:100,dutyPercent:85}];
http.request=async(url:string,options:any)=>{const path=new URL(url).pathname;requests.push(path);const request= requests.length;record('request-issued',{request,path});const reply=await fetch(url,{method:options?.method??'GET',headers:options?.headers,body:options?.body,signal:AbortSignal.timeout(options?.timeoutMs??5000)});const body=await reply.text();record('response-received',{request,path,status:reply.status});return {status:reply.status,headers:[...reply.headers].map(([k,v])=>k+': '+v).join('\r\n'),body};};
let native:PowerLifecycleState={generation:fanGen,phase:'suspended',resumeReady:false,hardwareWritesAllowed:false,inputReady:true,hibernateAvailable:true};
const lifecycle=new FanHostLifecycle({enabled:true,adapter:new HttpFanApiAdapter(base,'a'.repeat(64)),
 launcher:{async start(){return {pid:88888,executable:'fixture-host.exe'};},async stop(){}},
 readNativeActivity:async()=>null,readNativePowerState:async()=>{record('native-read');return({...native});},heartbeatIntervalMs:60000,resumeWaitDeadlineMs:1200,
 requestManualWake:async generation=>{const receipt={ok:native.phase==='ready',generation,phase:native.phase};record('manual-wake-receipt',{receipt});return receipt;}});
observe=()=>({localState:lifecycle.state,coordinator:lifecycle.coordinatorSnapshot});
// Existing UI owner at the actual native sleep boundary, process identity is
// a fixture while HTTP session/engine and subsequent operations are real.
const handshake=await new HttpFanApiAdapter(base,'a'.repeat(64)).handshake();
(lifecycle as any).writeReady=handshake.supported===true&&handshake.fanRouteWriteReady===true;
(lifecycle as any).process={pid:88888,executable:'fixture-host.exe'};
stage='observed-sleep';lifecycle.setPowerGeneration(fanGen);lifecycle.observePowerBoundary('suspending',fanGen);record('sleep-observed');
(lifecycle as any).desiredCurve=curve;(lifecycle as any).resumeCurve=curve;(lifecycle as any).stateValue='suspended';
let ok=false,error='';let writeStart=0;
try{
 stage='off';record('disable-called');await lifecycle.disable();record('disable-completed');
 writeStart=requests.length;
 native={...native,generation:mode==='stale-native'?1:fanGen,phase:mode==='sleep'?'suspended':'ready',resumeReady:mode!=='sleep',hardwareWritesAllowed:mode!=='sleep'};
 record('fixture-native-updated');lifecycle.observePowerBoundary('resuming',fanGen);lifecycle.observePowerBoundary('resume-ready',fanGen);record('client-wake-notice');
 stage='manual-start';if(mode!=='empty-apply')await lifecycle.start({manualRecovery:true,reason:'off-then-manual-on'});record('start-completed');
 stage='apply';await lifecycle.apply(curve);
 const state=await lifecycle.getState();
 ok=lifecycle.state==='ready'&&state.hardwareWritesEnabled===true&&state.controlAccepting===true;
 if(mode==='sleep'||mode==='stale-native'){ok=false;error='NATIVE_REFUSAL_BYPASSED';}record('control-completed',{ok,error});
}catch(e){error=String(e);record('control-refused',{error});if(mode==='sleep'||mode==='stale-native')ok=error.includes('FAN_MANUAL_WAKE_NOT_READY')&&!requests.slice(writeStart).some(p=>['/api/open','/api/open-events','/api/enable','/api/acquire-control'].includes(p));if(mode==='empty-apply')ok=error.includes('Fan Host 当前不可控制: suspended')&&!requests.slice(writeStart).some(p=>['/api/open','/api/open-events','/api/enable','/api/acquire-control'].includes(p));}
finally{stage='cleanup';await lifecycle.close().catch(()=>{});record('cleanup-completed');}
console.log(JSON.stringify({ok,error,mode,timelineSchemaVersion:1,timeline,scope:'production bridge + HTTP adapter + mock Host; native power/process fixtures; no HC/device proof'}));
if(!ok)process.exitCode=1;
