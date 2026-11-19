// Complete engine and real serial queue;only external UI/bridge ports are inert.
import '../mock-shell';
import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';import {resolve,join} from 'node:path';
import {InputOwnerRuntime} from '../../src/bridge/inputOwnerRuntime';
import {enqueueGamepadTask,gamepadSerialPending} from '../../src/gamepad/serial';
import {frontendDiagnosticDeliveryEvidence} from '../../src/robust/frontendDiagnostics';
const out=resolve(process.argv[3]);mkdirSync(out,{recursive:true});const mode=process.argv[4]??'candidate';assert(['before','candidate','necessary'].includes(mode));
const checks:any[]=[],traces:any[]=[],business:any[]=[],schedules:any[]=[],observations:any[]=[],cases:any[]=[];
const ports:any={visible:true,business,visibility:(_v:any)=>{},throwSnapshot:false};(globalThis as any).__ownerFixturePorts=ports;
const w:any=(globalThis as any).window,d:any=(globalThis as any).document;w.location={hash:'#/fixture'};d.querySelectorAll=()=>[];d.querySelector=()=>null;d.activeElement=null;
let timer=0;w.setTimeout=(_fn:unknown,ms:number)=>{schedules.push(['timeout',ms]);return ++timer;};w.clearTimeout=(id:number)=>schedules.push(['clear',id]);
(globalThis as any).requestAnimationFrame=()=>++timer;(globalThis as any).cancelAnimationFrame=(id:number)=>schedules.push(['cancel',id]);
w.addEventListener('gp:page-command',(e:any)=>business.push(['page-command',e.detail.action]));
const originalVisible=Object.getOwnPropertyDescriptor(d,'visibilityState');let visibility='visible',sequence:string[]=[];
Object.defineProperty(d,'visibilityState',{configurable:true,get(){const value=sequence.length?sequence.shift()!:visibility;observations.push(['visibility',value]);return value;},set(v:string){visibility=v;}});
const originalSnapshot=InputOwnerRuntime.prototype.snapshot;let owner:InputOwnerRuntime|undefined,throwSnapshot=false,snapshots=0;
InputOwnerRuntime.prototype.snapshot=function(){owner=this;++snapshots;if(throwSnapshot)throw Error('controlled original snapshot');return originalSnapshot.call(this);};
let semanticCalls=0;const originalSemantic=InputOwnerRuntime.prototype.semanticAction;InputOwnerRuntime.prototype.semanticAction=function(){++semanticCalls;return originalSemantic.call(this);};
(globalThis as any).__mockIpcResponder=(cmd:string,args:any)=>{if(cmd==='diagnostics.frontendTrace'){traces.push(JSON.parse(args.message));return {ok:true};}throw Error('external IPC refused '+cmd);};
const errors:any[]=[],consoleError=console.error;console.error=(...args:any[])=>errors.push(args.map(x=>x instanceof Error?x.message:String(x)));
const emit=(name:string,detail?:unknown)=>w.dispatchEvent(new CustomEvent(name,{detail}));const overlay=(session:string)=>emit('ipc:window.overlayPresentation',{active:true,session});
const settle=()=>new Promise<void>(r=>setImmediate(r));const flush=async()=>{await enqueueGamepadTask(()=>{});await settle();};
const check=(name:string,value:unknown)=>checks.push({name,pass:!!value});
let uiSeq=0;
const expected=(values:any[])=>({schema:1,scope:'original-short-circuit-at-execution;null=not-evaluated',overlaySessionMatch:values[0],epochMatch:values[1],loopAllowed:values[2],childOwned:values[3]});
async function queuedCase(name:string,mutate:()=>void,guard:any[]|null,gate?:string,dispatch=true,seq?:number){
 await flush();overlay('700');emit('ipc:gamepad.input-owner',{owner:''});emit('ipc:gamepad.testmode',false);visibility='visible';sequence=[];ports.visible=true;
 await settle();let release!:()=>void;const blocked=new Promise<void>(r=>release=r);const entered=enqueueGamepadTask(async()=>{business.push(['barrier',name]);await blocked;});
 await settle();const ti=traces.length,bi=business.length,mi=semanticCalls,si=snapshots;const usedSeq=seq??++uiSeq;
 emit('ipc:gamepad.ui-input',{action:'slider-increase',uiSeq:usedSeq,provenance:{overlaySession:'700',session:'serial'}});
 check(name+' enqueues one original semantic action before execution',semanticCalls===mi+1&&gamepadSerialPending()===2);
 mutate();release();await entered;await flush();
 const rows=traces.slice(ti);const drop=rows.find(r=>r.stage==='exec-drop');const actions=business.slice(bi).filter(r=>r[0]==='page-command');
 check(name+' original dispatch/refusal result',actions.length===(dispatch?1:0));
 if(guard){check(name+' emits existing exec-drop once',rows.filter(r=>r.stage==='exec-drop').length===1);if(mode==='candidate')check(name+' original short-circuit values captured',JSON.stringify(drop.execGuard)===JSON.stringify(expected(guard)));}
 else if(gate){check(name+' keeps legacy second guard reason',drop?.entryGate===gate&&!drop.execGuard);}
 else check(name+' does not invent guard refusal',!drop);
 cases.push({name,rows,actions,snapshotCalls:snapshots-si,guardExpected:guard,pendingAfter:gamepadSerialPending()});return drop;
}
try{
 const engine=await import('../../src/gamepad/engine');engine.startGamepad({router:{currentRoute:{value:{path:'/fixture'}},push:async()=>business.push(['router'])},onAction:(s:string)=>business.push(['onAction',s])} as any);await settle();
 const first=await queuedCase('overlay-first',()=>overlay('701'),[false,null,null,null],undefined,false);
 if(mode==='necessary'){
  check('necessary original guard fields missing',!!first?.execGuard);
  const failed=checks.filter(q=>!q.pass);assert.equal(failed.length,1);assert.equal(failed[0].name,'necessary original guard fields missing');
  writeFileSync(join(out,'necessary.json'),JSON.stringify({checks,expectedFailure:failed[0].name,firstDrop:first,realSerialQueue:true},null,2)+'\n',{flag:'wx'});process.exitCode=2;
 }else{
  await queuedCase('epoch-second',()=>owner!.stop('blur'),[true,false,null,null],undefined,false);
  await queuedCase('loop-third',()=>{visibility='hidden';},[true,true,false,null],undefined,false);
  await queuedCase('child-fourth',()=>emit('ipc:gamepad.input-owner',{owner:'custom-steam-library'}),[true,true,true,true],undefined,false);
  await queuedCase('pass-through',()=>{},null);
  const later=await queuedCase('loop-recheck-changes',()=>{sequence=['hidden','visible','visible'];},[true,true,false,null],undefined,false);
  check('legacy post-decision loop value may be true after original loop refusal',later.loopAllowed===true&&later.epochMatch===true&&later.childOwned===false);
  await queuedCase('testmode-second-guard',()=>emit('ipc:gamepad.testmode',true),null,'engine-gone-at-exec',false);
  await queuedCase('dedup-first-accepted',()=>{},null,undefined,true,88888);
  await queuedCase('dedup-second-refused',()=>{},null,undefined,false,88888);
  check('original dedup route remains distinct from execution guard',cases.at(-1).rows.some((r:any)=>r.stage==='exec-dedup'));
  // Original snapshot exception occurs before all guard operands and is caught by the real detached serial lane.
  await flush();overlay('700');emit('ipc:gamepad.testmode',false);let release!:()=>void;const barrier=enqueueGamepadTask(()=>new Promise<void>(r=>release=r));await settle();const begin=traces.length;
  emit('ipc:gamepad.ui-input',{action:'slider-increase',uiSeq:++uiSeq,provenance:{overlaySession:'700'}});throwSnapshot=true;release();await barrier;await flush();throwSnapshot=false;
  check('snapshot exception preserves real serial error handling without fabricated guard',errors.some(r=>r.includes('controlled original snapshot'))&&!traces.slice(begin).some(r=>r.execGuard));
  // Original passing route has no per-action trace. Reuse one input,uiSeq0 disables old dedup.
  emit('ipc:gamepad.input-owner',{owner:''});visibility='visible';sequence=[];overlay('700');await settle();const beforeTrace=traces.length,delivery=frontendDiagnosticDeliveryEvidence(),actionStart=business.length;
  const event=new CustomEvent('ipc:gamepad.ui-input',{detail:{action:'slider-increase',uiSeq:0,provenance:{overlaySession:'700'}}});
  for(let i=0;i<5000;++i)w.dispatchEvent(event);await flush();
  check('5000 actual serial passing actions add no trace or diagnostic requests',traces.length===beforeTrace&&JSON.stringify(frontendDiagnosticDeliveryEvidence().senderAdmission)===JSON.stringify(delivery.senderAdmission)&&business.slice(actionStart).filter(r=>r[0]==='page-command').length===5000);
  check('real serial lane retires every task without extra timers',gamepadSerialPending()===0);
  check('no shell or registry action reached through inert bridge',!(globalThis as any).__mockShellCalls.length&&!(globalThis as any).__mockRegistryWrites.length);
  engine.stopGamepad();await settle();const report={checks,failures:checks.filter(q=>!q.pass).length,traces,cases,business,schedules,observations,errors,semanticCalls,snapshotCalls:snapshots,delivery:frontendDiagnosticDeliveryEvidence(),actualSerialQueue:true,installedProductExecuted:false};
  writeFileSync(join(out,'report.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});assert.equal(report.failures,0);
 }
}catch(e){writeFileSync(join(out,'failure.txt'),String((e as Error).stack??e)+'\n',{flag:'wx'});throw e;}finally{console.error=consoleError;if(originalVisible)Object.defineProperty(d,'visibilityState',originalVisible);}
