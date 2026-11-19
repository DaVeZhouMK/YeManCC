// Execute the complete engine and actual mirror/diagnostic sender/IPC with
// declared inert external ports. This does not run installed UI or hardware.
import '../mock-shell';
import assert from 'node:assert/strict';
import { mkdirSync,writeFileSync } from 'node:fs';
import { resolve,join } from 'node:path';
import { InputOwnerRuntime } from '../../src/bridge/inputOwnerRuntime';
import { frontendDiagnosticDeliveryEvidence,readFrontendErrorLog } from '../../src/robust/frontendDiagnostics';

const out=resolve(process.argv[3]);mkdirSync(out,{recursive:true});
const mode=process.argv[4]??'candidate';
assert(['before','candidate','necessary'].includes(mode));
const checks:Array<{name:string;pass:boolean}>=[],traces:any[]=[],ownerCalls:any[]=[],published:any[]=[],business:any[]=[],schedules:any[]=[];
const ports={business,visible:true,visibility:(_value:any)=>{},throwSnapshot:false};
(globalThis as any).__ownerFixturePorts=ports;
const w=(globalThis as any).window,d=(globalThis as any).document;
w.location={hash:'#/fixture'};
d.activeElement=null;d.querySelectorAll=()=>[];
const documentListeners=new Map<string,Set<(e:any)=>void>>();
d.addEventListener=(name:string,fn:(e:any)=>void)=>{if(!documentListeners.has(name))documentListeners.set(name,new Set());documentListeners.get(name)!.add(fn);};
d.removeEventListener=(name:string,fn:(e:any)=>void)=>documentListeners.get(name)?.delete(fn);
let timer=0,raf=0;
w.setTimeout=(_fn:unknown,ms:number)=>{schedules.push(['setTimeout',ms]);return ++timer;};
w.clearTimeout=(id:number)=>schedules.push(['clearTimeout',id]);
(globalThis as any).requestAnimationFrame=(_fn:unknown)=>{schedules.push(['requestAnimationFrame']);return ++raf;};
(globalThis as any).cancelAnimationFrame=(id:number)=>schedules.push(['cancelAnimationFrame',id]);
(globalThis as any).__mockIpcResponder=(cmd:string,args:any)=>{
  if(cmd==='diagnostics.frontendTrace'){traces.push(JSON.parse(args.message));return {ok:true};}
  throw Error('unexpected fixture native port: '+cmd);
};
w.addEventListener('input:owner-trace',(e:any)=>published.push(structuredClone(e.detail)));
let activeOwner:InputOwnerRuntime|undefined,throwStart=false;
for(const method of ['start','stop','summon','sourceInserted','sourceRemoved','semanticAction'] as const){
  const original=(InputOwnerRuntime.prototype as any)[method];
  (InputOwnerRuntime.prototype as any)[method]=function(...args:any[]){
    activeOwner=this;const before=this.snapshot();
    if(method==='start'&&throwStart){ownerCalls.push({method,args,before,threw:true});throw Error('fixture-original-start-throws');}
    const value=original.apply(this,args);ownerCalls.push({method,args,before,returned:structuredClone(value)});return value;
  };
}
const settle=async()=>{await new Promise<void>(r=>setImmediate(r));};
const emit=(name:string,detail?:unknown)=>w.dispatchEvent(new CustomEvent(name,{detail}));
const visibility=(value:string)=>{d.visibilityState=value;for(const fn of documentListeners.get('visibilitychange')??[])fn({type:'visibilitychange'});};
function check(name:string,pass:unknown){checks.push({name,pass:!!pass});}
const isAfter=(q:any)=>q.ownerTransitionSchema===1&&q.phase==='after-call';
const resultFor=(rows:any[],trigger:string,transition:string)=>rows.find(q=>isAfter(q)&&q.trigger===trigger&&q.transition===transition);
async function scenario(name:string,run:()=>void,verify:(rows:any[])=>void){
  const index=traces.length;run();await settle();const rows=traces.slice(index);
  if(mode!=='before')verify(rows);
  business.push(['scenario',name]);
}
try{
 const engine=await import('../../src/gamepad/engine');
 const options={router:{currentRoute:{value:{path:'/fixture'}},push:async()=>{business.push(['router-push']);}},onAction:(label:string)=>business.push(['onAction',label])};
 const errorRingBefore=readFrontendErrorLog().length;
 await scenario('mount',()=>{engine.startGamepad(options as any);},rows=>check('mount-after-call-has-returned-game-consumer',rows.some(q=>q.stage==='engine-start'&&isAfter(q)&&q.owner==='game-consumer'&&q.sourceAdmission===true&&q.sourcePresent===true)));
 await scenario('focus',()=>{emit('ipc:window.hidden');emit('ipc:window.focus');},rows=>{
   const before=rows.find(q=>q.stage==='owner-start'&&q.trigger==='window-focus'),after=resultFor(rows,'window-focus','start');
   check('focus-before-none-and-after-returned-state-are-distinct',before?.phase==='before-call'&&before.owner==='none'&&after?.owner==='game-consumer'&&after.epoch===before.epoch+1);
 });
 if(mode==='necessary'){
   const failed=checks.find(q=>q.name==='focus-before-none-and-after-returned-state-are-distinct'&&!q.pass);
   assert(failed,'original-before must fail the named phase-evidence assertion');
   writeFileSync(join(out,'necessary.json'),JSON.stringify({checks,expectedFailure:failed.name,executedInstalledProduct:false},null,2)+'\n',{flag:'wx'});
   process.exitCode=2;
 }else{
 await scenario('doc-visible',()=>{visibility('hidden');visibility('visible');},rows=>{
   const before=rows.find(q=>q.stage==='owner-start'&&q.trigger==='doc-visible'),after=resultFor(rows,'doc-visible','start');
   check('document-recovery-keeps-precall-and-returned-epoch',before?.phase==='before-call'&&after?.epoch===before.epoch+1);
 });
 await scenario('restart-game',()=>emit('ipc:gamepad.restart'),rows=>check('restart-game-reports-intermediate-stop-and-final-start',resultFor(rows,'gamepad-restart','stop')?.owner==='none'&&resultFor(rows,'gamepad-restart','start')?.owner==='game-consumer'));
 await scenario('summon-restart',()=>{emit('ipc:gamepad.summon');emit('ipc:gamepad.restart');},rows=>check('summon-and-restart-returned-mirror-owner',resultFor(rows,'gamepad-summon','summon')?.owner==='ymcc-frontend'&&resultFor(rows,'gamepad-restart','summon')?.owner==='ymcc-frontend'));
 await scenario('physical-only-blur',()=>emit('ipc:window.blur'),rows=>check('physical-only-blur-does-not-invent-stop-result',rows.length===0));
 await scenario('overlay-blur',()=>{emit('ipc:window.overlayPresentation',{active:true,session:'1'});emit('ipc:window.blur');},rows=>check('overlay-blur-keeps-original-early-return',rows.length===0));
 await scenario('strict-blur',()=>{emit('ipc:window.overlayPresentation',{active:false});emit('input:motion-telemetry',{hostFrame:{hostActive:true,firstFrame:true}});emit('ipc:window.blur');},rows=>check('admitted-target-blur-retains-original-stop',resultFor(rows,'clear-summon-state','stop')?.owner==='none'));
 await scenario('crash-both-events',()=>{emit('ipc:input-host.crash');emit('input-host:crash');},rows=>check('both-original-crash-events-get-returned-state',rows.filter(q=>isAfter(q)&&q.trigger==='input-host-crash'&&q.owner==='none').length===2));
 await scenario('remove-insert',()=>{emit('native-pad-removed');emit('native-pad-added');},rows=>{
   const removed=resultFor(rows,'native-pad-removed','source-removed'),inserted=resultFor(rows,'native-pad-added','source-inserted');
   check('source-removal-and-insertion-do-not-claim-rearm',removed?.sourcePresent===false&&removed.rearm===true&&inserted?.sourcePresent===true&&inserted.rearm===true&&inserted.sourceAdmission===false&&inserted.epoch===removed.epoch);
 });
 await scenario('blocked-start',()=>{emit('ipc:window.hidden');emit('ipc:window.focus');},rows=>{
   const before=rows.find(q=>q.stage==='owner-start'),after=resultFor(rows,'window-focus','start');
   check('returned-start-may-remain-none-with-same-epoch-and-rearm',before?.owner==='none'&&after?.owner==='none'&&after.epoch===before.epoch&&after.rearm===true&&after.sourceAdmission===false);
 });
 await scenario('blocked-restart',()=>emit('ipc:gamepad.restart'),rows=>{
   const stopped=resultFor(rows,'gamepad-restart','stop'),started=resultFor(rows,'gamepad-restart','start');
   check('blocked-restart-keeps-intermediate-and-returned-none',stopped?.owner==='none'&&started?.owner==='none'&&started.epoch===stopped.epoch&&started.rearm===true);
 });
 await scenario('call-throws',()=>{throwStart=true;try{emit('ipc:window.focus');business.push(['unexpected-no-throw']);}catch(e){business.push(['original-throw',(e as Error).message]);}finally{throwStart=false;}},rows=>check('original-call-throw-never-fabricates-after-call-result',rows.some(q=>q.phase==='before-call')&&!rows.some(isAfter)));
 await scenario('diagnostic-snapshot-failure',()=>{ports.throwSnapshot=true;emit('ipc:window.focus');ports.throwSnapshot=false;},rows=>check('diagnostic-snapshot-failure-does-not-block-original-call',rows.length===0&&ownerCalls.at(-1)?.method==='start'&&!ownerCalls.at(-1)?.threw));
 const prior=traces.length;
 for(let i=0;i<10000;i++)emit('ipc:gamepad.state',{connected:(i&1)===0});
 await settle();if(mode!=='before')check('ten-thousand-state-frames-add-no-transition-log',traces.length===prior);
 await scenario('capacity',()=>{
   const webview=w.chrome.webview,post=webview.postMessage,pending:any[]=[];
   webview.postMessage=(q:any)=>pending.push(q);
   for(let i=0;i<80;i++){emit('ipc:window.hidden');emit('ipc:window.focus');}
   const evidence=frontendDiagnosticDeliveryEvidence();
   if(mode!=='before')check('same-original-sender32-capacity-and-no-diagnostic-replay',pending.length===32&&evidence.senderAdmission.inFlightRequests===32&&evidence.senderAdmission.capacityRejectedRequests>0);
   webview.postMessage=post;for(const q of pending)post.call(webview,q);
 },()=>{});
 await scenario('stop',()=>engine.stopGamepad(),rows=>check('engine-stop-before-and-returned-close-state',rows.some(q=>q.stage==='engine-stop'&&q.phase==='before-call')&&resultFor(rows,'engine-stop','stop')?.owner==='none'));
 const delivery=frontendDiagnosticDeliveryEvidence();
 if(mode!=='before'){
   check('all-original-sender-reservations-returned',delivery.senderAdmission.inFlightRequests===0&&delivery.senderAdmission.admittedRequests===delivery.senderAdmission.settledRequests);
   check('state-evidence-does-not-enter-original-persistent-error-ring',readFrontendErrorLog().length===errorRingBefore);
   check('every-current-owner-state-row-has-known-phase-and-fixed-schema',traces.filter(q=>['owner-start','owner-stop','owner-restart','engine-start','engine-stop','owner-result'].includes(q.stage)).every(q=>q.ownerTransitionSchema===1&&['before-call','after-call'].includes(q.phase)));
 }
 const report={checks,failures:checks.filter(q=>!q.pass).length,mode,traces,ownerCalls,published,business,schedules,delivery,executedInstalledProduct:false,scope:'ACTUAL_COMPLETE_ENGINE_MIRROR_SENDER_IPC_WITH_INERT_EXTERNAL_PORTS;NOT_NATIVE_OR_HARDWARE'};
 writeFileSync(join(out,'report.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
 console.log(JSON.stringify({mode,checks:checks.length,failures:report.failures,traces:traces.length,ownerCalls:ownerCalls.length,published:published.length}));
 process.exitCode=report.failures?1:0;
 }
}catch(error){writeFileSync(join(out,'FAIL.json'),JSON.stringify({message:String(error),stack:(error as Error).stack},null,2)+'\n',{flag:'wx'});throw error;}
