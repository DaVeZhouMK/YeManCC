import '../mock-shell';
import assert from 'node:assert/strict';
import { mkdirSync,writeFileSync } from 'node:fs';
import { resolve,join } from 'node:path';
import { InputOwnerRuntime } from '../../src/bridge/inputOwnerRuntime';
import { frontendDiagnosticDeliveryEvidence,readFrontendErrorLog } from '../../src/robust/frontendDiagnostics';
const out=resolve(process.argv[3]);mkdirSync(out,{recursive:true});const mode=process.argv[4]??'candidate';assert(['before','candidate','necessary'].includes(mode));
const checks:any[]=[],business:any[]=[],traces:any[]=[],ownerCalls:any[]=[],schedules:any[]=[],getterReads:any[]=[];
const ports={business,visible:true,visibility:(_v:any)=>{},throwSnapshot:false};(globalThis as any).__ownerFixturePorts=ports;
const w=(globalThis as any).window,d=(globalThis as any).document;w.location={hash:'#/fixture'};d.activeElement=null;d.querySelectorAll=()=>[];
let timer=0;w.setTimeout=(_fn:unknown,ms:number)=>{schedules.push(['setTimeout',ms]);return ++timer;};w.clearTimeout=(n:number)=>schedules.push(['clearTimeout',n]);
(globalThis as any).requestAnimationFrame=()=>++timer;(globalThis as any).cancelAnimationFrame=(n:number)=>schedules.push(['cancelAnimationFrame',n]);
(globalThis as any).__mockIpcResponder=(cmd:string,args:any)=>{if(cmd==='diagnostics.frontendTrace'){traces.push(JSON.parse(args.message));return {ok:true};}throw Error('refused external command '+cmd);};
w.addEventListener('gp:page-command',(e:any)=>business.push(['page-command',e.detail.action]));
const originalSemantic=InputOwnerRuntime.prototype.semanticAction;
InputOwnerRuntime.prototype.semanticAction=function(...args:any[]){const value=(originalSemantic as any).apply(this,args);ownerCalls.push(structuredClone(value));return value;};
const emit=(name:string,detail?:unknown)=>w.dispatchEvent(new CustomEvent(name,{detail}));
const overlay=(active:boolean,session?:string)=>emit('ipc:window.overlayPresentation',{active,session});
const settle=()=>new Promise<void>(r=>setImmediate(r));
const check=(name:string,value:unknown)=>{checks.push({name,pass:!!value});};
const snapshot=()=>frontendDiagnosticDeliveryEvidence() as any;
const keys=['activePass','activeReject','inactivePass','inactiveReject'] as const;
const counters=()=>snapshot().overlaySessionEntry?.counts;
let uiSeq=0;
function send(provenance:any,action:string|undefined='slider-increase'){emit('ipc:gamepad.ui-input',{action,uiSeq:++uiSeq,provenance});}
async function step(name:string,active:boolean,session:string|undefined,provenance:any,bucket:typeof keys[number],shouldQueue:boolean,action:string|undefined='slider-increase'){
 overlay(active,session);const counts=counters(),before=ownerCalls.length;send(provenance,action);await settle();
 check(name+' preserves original admission mirror call',ownerCalls.length-before===(shouldQueue?1:0));
 if(mode!=='before')check(name+' counts only original completed comparison',keys.every(k=>counters()[k]===counts[k]+(k===bucket?1:0)));
}
try{
 const engine=await import('../../src/gamepad/engine');const errorRing=readFrontendErrorLog().length;
 // This comparison passes before engine readiness; no semantic action is allowed.
 const initial=counters();send({});await settle();check('engine-not-ready still refuses action after session gate',ownerCalls.length===0);
 if(mode!=='before')check('session pass before engine readiness is not action success',counters()?.inactivePass===(initial?.inactivePass??0)+1);
 const options={router:{currentRoute:{value:{path:'/fixture'}},push:async()=>business.push(['router-push'])},onAction:(s:string)=>business.push(['onAction',s])};engine.startGamepad(options as any);await settle();
 if(mode==='necessary'){
  overlay(true,'100');send({overlaySession:'stale'});await settle();
  const value=counters();check('old original rejects the stale session without semantic action',ownerCalls.length===0);
  check('necessary original session refusal is reported by a fixed counter',value?.activeReject===1);
  writeFileSync(join(out,'necessary.json'),JSON.stringify({checks,expectedMissingObservation:'activeReject',oldFailed:checks.some(q=>!q.pass)},null,2)+'\n',{flag:'wx'});process.exitCode=2;
 }else{
  await step('inactive untagged',false,undefined,{},'inactivePass',true);
  await step('inactive string tag',false,undefined,{overlaySession:'old'},'inactiveReject',false);
  await step('inactive numeric tag follows old typeof rule',false,undefined,{overlaySession:100},'inactivePass',true);
  await step('active exact session',true,'100',{overlaySession:'100'},'activePass',true);
  await step('active different session',true,'100',{overlaySession:'101'},'activeReject',false);
  await step('active missing session',true,'100',{},'activeReject',false);
  await step('active numeric session is not string equality',true,'100',{overlaySession:100},'activeReject',false);
  let reads=0;const getter={get overlaySession(){++reads;return '100';}};
  await step('getter read once',true,'100',getter,'activePass',true);getterReads.push(['one-read',reads]);check('provenance overlaySession getter count remains one',reads===1);
  overlay(true,'100');const beforeFlip=counters(),beforeCalls=ownerCalls.length,beforeBusiness=business.length;
  send({get overlaySession(){getterReads.push(['reentrant']);overlay(false);return '100';}});await settle();
  check('reentrant getter keeps original entry pass and later execution refusal',ownerCalls.length===beforeCalls+1&&business.length===beforeBusiness+1);
  if(mode!=='before')check('counter uses original active decision before getter reentry',counters().activePass===beforeFlip.activePass+1&&counters().inactivePass===beforeFlip.inactivePass);
  overlay(true,'100');const beforeThrow=counters(),beforeThrown=ownerCalls.length;let caught=false;
  try{send({get overlaySession(){getterReads.push(['throw']);throw Error('original-provenance-getter');}});}catch{caught=true;}
  check('original getter exception remains exceptional before gate completion',caught&&ownerCalls.length===beforeThrown);
  if(mode!=='before')check('throwing comparison does not fabricate a completed count',JSON.stringify(counters())===JSON.stringify(beforeThrow));
  const beforeMissing=counters();emit('ipc:gamepad.ui-input',{uiSeq:++uiSeq,provenance:{overlaySession:'100'}});await settle();
  if(mode!=='before')check('missing action still records only session comparison',counters().activePass===beforeMissing.activePass+1);
  // No event/JSON/sender/timer work is introduced for repeated refused input.
  overlay(true,'100');await settle();const pre=snapshot(),traceCount=traces.length,ownerCount=ownerCalls.length,businessCount=business.length,scheduleCount=schedules.length;
  const event=new CustomEvent('ipc:gamepad.ui-input',{detail:{action:'slider-increase',uiSeq:900000,provenance:{overlaySession:'stale'}}});
  let jsonCalls=0;const stringify=JSON.stringify;JSON.stringify=((...args:any[])=>{++jsonCalls;return (stringify as any)(...args);}) as any;
  for(let i=0;i<100000;++i)w.dispatchEvent(event);JSON.stringify=stringify;await settle();const post=snapshot();
  check('100000 refusals add no JSON tracing sender business scheduler or mirror calls',jsonCalls===0&&traces.length===traceCount&&ownerCalls.length===ownerCount&&business.length===businessCount&&schedules.length===scheduleCount&&JSON.stringify(post.senderAdmission)===JSON.stringify(pre.senderAdmission));
  if(mode!=='before')check('storm contributes exactly fixed activeReject count',post.overlaySessionEntry.counts.activeReject===pre.overlaySessionEntry.counts.activeReject+100000);
  check('no frontend error-ring entry or shell/registry action is introduced',readFrontendErrorLog().length===errorRing&&(globalThis as any).__mockShellCalls.length===0&&(globalThis as any).__mockRegistryWrites.length===0);
  if(mode!=='before'){
   const copy=snapshot().overlaySessionEntry;const original=copy.counts.activePass;copy.counts.activePass=-999;
   check('export owns scalar values and never exposes counter storage',counters().activePass===original);
   check('export keeps fixed schema scope and JS-safe clamp bound',post.overlaySessionEntry.schema===1&&post.overlaySessionEntry.counterLimit===Number.MAX_SAFE_INTEGER&&post.overlaySessionEntry.scope==='completed-renderer-native-ui-input-session-gate;module-lifetime');
  }
  engine.stopGamepad();await settle();
  const evidence=snapshot(),failures=checks.filter(q=>!q.pass).length;
  writeFileSync(join(out,'report.json'),JSON.stringify({checks,failures,evidence,business,traces,ownerCalls,schedules,getterReads,storm:100000,installedProductExecuted:false},null,2)+'\n',{flag:'wx'});assert.equal(failures,0);
 }
}catch(error){writeFileSync(join(out,'failure.txt'),String((error as Error).stack??error)+'\n',{flag:'wx'});throw error;}
