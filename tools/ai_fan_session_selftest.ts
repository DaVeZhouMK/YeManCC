import assert from 'node:assert/strict';
import { parseAiFanMockSession, aiFanMockArguments, aiFanMockHandshakeAllowed, aiFanMockStateAllowed, parseAiFanRequest, aiFanRuntimeEvidence, type AiFanMockSession, type AiFanRequest } from '../src/bridge/aiFanMock';
import { startAiFanService, type AiFanServiceDeps } from '../src/bridge/aiFanService';
import type { FanHandshake, FanState } from '../src/bridge/fanApi';
let checks = 0;
function check(value: unknown, message: string) { checks++; assert.ok(value, message); }
function rejects(fn: () => unknown, message: string) { checks++; assert.throws(fn, undefined, message); }
const id='11111111-2222-3333-4444-555555555555';
const dir='C:\\test\\YeManCC\\ai-fan-sessions\\'+id+'\\fan-host';
const session: AiFanMockSession={schemaVersion:1,enabled:true,mode:'mock-handshake',sessionId:id,parentPid:1234,stateDirectory:dir,requestPath:dir+'\\ai-request.json',responsePath:dir+'\\ai-response.json',hostPid:0,hostCreationTime100ns:'0'};
check(parseAiFanMockSession({schemaVersion:1,enabled:false})===null,'normal launch disabled');
check(parseAiFanMockSession(session)?.parentPid===1234,'valid native session');
for(const bad of [null,{},true,{schemaVersion:2,enabled:false},{schemaVersion:1,enabled:'true'}]) rejects(()=>parseAiFanMockSession(bad),'invalid session rejected');
for(const [key,value] of Object.entries({sessionId:'../a',parentPid:0,mode:'real',stateDirectory:'C:\\test\\real-fan-host',requestPath:'C:\\other.json',responsePath:'C:\\other.json',hostPid:-1,hostCreationTime100ns:'NaN'})) rejects(()=>parseAiFanMockSession({...session,[key]:value}),'session field '+key);
for(let i=0;i<1000;i++){
 rejects(()=>parseAiFanMockSession({...session,parentPid:i+0.5}),'non-integral parent');
 rejects(()=>parseAiFanMockSession({...session,stateDirectory:'C:\\test\\..\\ai-fan-sessions\\'+id+'\\fan-host',requestPath:'C:\\x',responsePath:'C:\\y'}),'traversal');
}
const args=aiFanMockArguments(session,dir+'\\YeManFanHost.session');
check(args.includes('--mock-handshake')&&args.includes('--mock-zero-hardware-evidence'),'explicit mock/evidence args');
check(!args.some(x=>/real-backend|allow-hardware-writes|authorization|confirm/.test(x)),'no real write args');
rejects(()=>aiFanMockArguments(session,'C:\\real\\YeManFanHost.session'),'real token forbidden');
const handshake: FanHandshake={ok:true,supported:true,protocolVersion:2,hostMode:'mock-handshake',mockZeroHardwareEvidence:true,fanRouteWriteReady:true,hardwareWritesEnabled:false,hardwareWritesObserved:false};
check(aiFanMockHandshakeAllowed(handshake),'explicit zero-write handshake');
for(const key of ['ok','supported','protocolVersion','hostMode','mockZeroHardwareEvidence','fanRouteWriteReady','hardwareWritesEnabled','hardwareWritesObserved']){const x={...handshake} as Record<string,unknown>;delete x[key];check(!aiFanMockHandshakeAllowed(x as unknown as FanHandshake),'missing handshake '+key);}
check(!aiFanMockHandshakeAllowed({...handshake,hardwareWritesEnabled:true}),'physical writes refuse');
const safe: FanState={state:'Ready',hostMode:'mock-handshake',protocolVersion:'2',mockZeroHardwareEvidence:true,hardwareWrites:false,hardwareWritesEnabled:false,hardwareWritesObserved:false,mockControlEnabled:false,mockControlSequence:0};
check(aiFanMockStateAllowed(safe),'zero-write state');
for(const k of ['hostMode','protocolVersion','hardwareWritesEnabled','hardwareWritesObserved','mockZeroHardwareEvidence']){const x={...safe};delete x[k];check(!aiFanMockStateAllowed(x),'missing state '+k);}
const secretState={...safe,lease:{leaseId:'secret',owner:'YeManCC',generation:3},sessionToken:'secret',confirmationToken:'secret',authorization:'secret',nested:{secret:'secret'},mockControlSequence:5};
const evidence=aiFanRuntimeEvidence(secretState);
check(!JSON.stringify(evidence).includes('secret'),'runtime evidence secret whitelist');check(evidence.leaseHeld===true,'public lease bool retained');
function request(sequence:number, action:AiFanRequest['action']='status'): AiFanRequest{return {schemaVersion:1,sessionId:id,parentPid:1234,requestId:'aaaaaaaa-bbbb-cccc-dddd-'+sequence.toString(16).padStart(12,'0'),sequence,action};}
for(let i=1;i<=1000;i++){const r=request(i);check(parseAiFanRequest(r,session).sequence===i,'valid request');rejects(()=>parseAiFanRequest({...r,sessionId:'wrong'},session),'different session');rejects(()=>parseAiFanRequest({...r,action:'execute',command:'cmd'},session),'arbitrary action');}
rejects(()=>parseAiFanRequest({...request(1),preset:'balanced'},session),'preset forbidden on status');
rejects(()=>parseAiFanRequest({...request(1),sequence:Infinity},session),'sequence not finite');
const flush=async()=>{for(let i=0;i<25;i++)await Promise.resolve();};
function fixture(enabled=true){
 let text='';let state={...safe};let pid=0;let high=0;const seen=new Set<string>();let token=0;
 const calls:string[]=[];const responses:Record<string,any>[]=[];const timers=new Map<number,()=>void>();let reads=0,claims=0,writes=0;
 const deps:AiFanServiceDeps={
  session:async()=>enabled?{...session,hostPid:pid,hostCreationTime100ns:pid?'133000000000000000':'0'}:{schemaVersion:1,enabled:false},
  read:async()=>{reads++;if(!text)throw Error('not-found');return text;},
  write:async(_p,x)=>{writes++;responses.push(JSON.parse(x));},
  claim:async r=>{claims++;if(r.sequence<=high||seen.has(r.requestId))return false;high=r.sequence;seen.add(r.requestId);return true;},
  setTimer:f=>{const k=token++;timers.set(k,f);return k;},clearTimer:k=>{timers.delete(k as number);},now:()=>new Date(0).toISOString(),
 };
 const life={phase:'ready' as const,start:async()=>{calls.push('start');pid=4567;return {allowed:true,writeReady:true};},getState:async()=>{calls.push('state');return structuredClone(state);},apply:async()=>{calls.push('apply');state={...safe,mockControlEnabled:true,mockControlSequence:Number(state.mockControlSequence)+1,lease:{leaseId:'secret',owner:'YeManCC',generation:1}};return state;},disable:async()=>{calls.push('disable');state={...safe,state:'AwaitingControl'};return state;},getAiMockCloseReceipt:()=>({...safe,state:'Stopped',mockCloseCompleted:true,leaseHeld:false}),close:async()=>{calls.push('close');pid=0;state={...safe,state:'Stopped'};}};
 return {deps,life,calls,responses,timers,get reads(){return reads},get claims(){return claims},get writes(){return writes},set text(x:string){text=x},set state(x:FanState){state=x},get pid(){return pid},async tick(){const first=timers.entries().next().value;if(first){timers.delete(first[0]);first[1]();}await flush();}};
}
const inert=fixture(false);const stopInert=await startAiFanService({lifecycle:inert.life,preset:()=>[],deps:inert.deps});await flush();check(inert.timers.size===0&&inert.reads===0&&inert.claims===0&&inert.calls.length===0,'normal zero timers/files/controls');stopInert();
const f=fixture();const opts={lifecycle:f.life,preset:()=>[{tempC:40,dutyPercent:20}],deps:f.deps};let stop=await startAiFanService(opts);await flush();
check(f.timers.size===1,'single idle timer');
f.text=JSON.stringify(request(1,'probe'));await f.tick();check(f.responses.at(-1)?.ok===true&&f.pid===4567,'probe runtime readback');
f.text=JSON.stringify({...request(2,'on'),preset:'balanced'});await f.tick();check(f.responses.at(-1)?.ok===true&&f.responses.at(-1)?.evidence.mockControlEnabled===true,'on real lifecycle apply');check(!JSON.stringify(f.responses).includes('secret'),'all responses no credentials');
const applies=f.calls.filter(x=>x==='apply').length;await f.tick();check(f.calls.filter(x=>x==='apply').length===applies,'same text no action');
f.text=JSON.stringify({...request(2,'on'),preset:'soft'});await f.tick();check(f.calls.filter(x=>x==='apply').length===applies,'same sequence altered body no action');
stop();check(f.timers.size===0,'dispose timer even handle0');stop=await startAiFanService(opts);await flush();check(f.calls.filter(x=>x==='apply').length===applies,'native claim prevents renderer remount replay');
f.text=JSON.stringify(request(3,'off'));await f.tick();check(f.responses.at(-1)?.ok===true&&f.calls.includes('disable'),'off uses lifecycle');
f.text=JSON.stringify(request(4,'on'));await f.tick();check(f.responses.at(-1)?.ok===true,'reenable after off');
f.text=JSON.stringify(request(5,'close'));await f.tick();check(f.responses.at(-1)?.ok===true&&f.pid===0&&f.calls.includes('close')&&f.responses.at(-1)?.evidence.closeReceipt.mockCloseCompleted===true&&f.responses.at(-1)?.evidence.childExited===true,'close uses lifecycle with receipt and actual exited child');
f.text=JSON.stringify(request(6,'status'));await f.tick();check(f.responses.at(-1)?.ok===true&&f.responses.at(-1)?.evidence.runtimeVerified===false,'stopped status no fake runtime');
f.text='{"command":"cmd","token":"secret"}';await f.tick();check(f.responses.at(-1)?.ok===false&&!JSON.stringify(f.responses.at(-1)).includes('secret'),'malformed request safely rejected');stop();
const bad=fixture();bad.state={...safe,hardwareWritesEnabled:true};bad.text=JSON.stringify(request(1,'on'));const stopBad=await startAiFanService({lifecycle:bad.life,preset:()=>[],deps:bad.deps});await flush();check(!bad.calls.includes('apply')&&bad.responses.at(-1)?.ok===false,'bad runtime not admitted to apply');stopBad();
const late=fixture();let release:((s:unknown)=>void)|null=null;late.deps.session=()=>new Promise(resolve=>{release=resolve});const startLate=startAiFanService({lifecycle:late.life,preset:()=>[],deps:late.deps});await flush();check(release!==null,'initial session request pending');(release as unknown as (s:unknown)=>void)({schemaVersion:1,enabled:false});await startLate;check(late.timers.size===0,'disabled late response stays inert');
console.log('AI fan contract/service selftest PASS '+checks+' checks; no process/hardware mutations');