// Real frontend entry points + isolated IPC responders. No commands reach Windows hardware.
import 'vue';
import './mock-shell';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { setTdp, setPowerControlDir, resumeTdpDaemonAfterWake, stopTdpDaemon } from '../src/bridge/yeman';
import { resetTdpCapabilityState } from '../src/bridge/tdpCircuitBreaker';
import { tdpRuntimeStore } from '../src/bridge/tdpRuntimeState';
import { tdpDiagnosticDeliveryFailures, markTdpOperatorMode } from '../src/bridge/tdpStaticDiagnostics';
const base=resolve(process.argv[2]);mkdirSync(base,{recursive:true});
const checks:Array<{name:string;pass:boolean}>=[];let scenario='cli',selected=false,floating=false,ready=true,logFails=false,phase='ready';
let calls:Array<{cmd:string;args:any}>=[],rows:Array<{event:string;detail:any}>=[];
(globalThis as any).__mockShellDryRun=true;
(globalThis as any).__mockIpcResponder=(cmd:string,args:any)=>{
 calls.push({cmd,args});
 if(cmd==='tdp.observe'){rows.push(structuredClone(args));if(logFails)throw new Error('fixture-log-delivery-failed');return {ok:true};}
 if(cmd==='tdp.capability')return {provider:selected?'msi-g3e-wmi':'default',selected,ready,minPL1:selected?8:2,maxPL1:selected?35:200,cpuFamily:selected?'intel-arc-g3-extreme':'other-or-unverified',generation:1,deadlineTick:123456};
 if(cmd==='power.lifecycle')return {phase,generation:1,hardwareWritesAllowed:phase==='ready',inputReady:phase==='ready'};
 if(cmd==='registry.read')return args.name==='VendorIdentifier'?'GenuineIntel':null;
 if(cmd==='fs.stat'&&String(args.path).endsWith('float-active')){if(floating)return {modified:Date.now()/1000};throw new Error('fixture-float-disabled');}
 if(cmd==='tdpDaemon.start')throw new Error('fixture-daemon-start-unavailable');
 if(cmd==='tdpDaemon.request'){
  if(args.op==='ping')return {ok:scenario.startsWith('daemon')&&scenario!=='daemon-unavailable',rc:scenario.startsWith('daemon')&&scenario!=='daemon-unavailable'?0:7,requestId:'fixture-existing-daemon-id'};
  if(args.op==='set')return {ok:scenario!=='daemon-fail',rc:scenario==='daemon-fail'?7:0,requestId:'fixture-existing-daemon-id',error:scenario==='daemon-fail'?'fixture-original-daemon-error':undefined};
  return {ok:true,rc:0};
 }
 if(cmd==='tdpMsi.set')return {ok:scenario!=='msi-fail',rc:scenario==='msi-fail'?6:0,readbackMatched:scenario!=='msi-fail',error:'fixture-OEM-partial-unknown'};
 if(cmd==='shell.run'){
  if(scenario==='stale')tdpRuntimeStore.invalidatePower();
  return {exitCode:scenario==='cli-fail'?7:0,stdout:'fixture-control-output',stderr:scenario==='cli-fail'?'fixture-first-error':''};
 }
 return undefined;
};
function check(name:string,pass:unknown){checks.push({name,pass:!!pass});}
async function reset(name:string){scenario=name;selected=name.startsWith('msi');floating=name.startsWith('daemon');ready=true;phase='ready';logFails=false;calls=[];rows=[];resetTdpCapabilityState();tdpRuntimeStore.invalidatePower();setPowerControlDir(join(base,name));await Promise.resolve();}
async function settle(){await new Promise(r=>setImmediate(r));}
function terminals(){return rows.filter(r=>['tdp.completed','tdp.failed','tdp.skipped'].includes(r.event));}
function correlated(){const request=rows.find(r=>r.event==='tdp.request');return !!request&&rows.every(r=>r.detail.operationId===request.detail.operationId);}
async function main(){
 await reset('save-only');const saved=await setTdp('ac',30,{apply:false,save:true,source:'ui'});await settle();
 check('save-only-terminal-config-saved-no-helper',saved&&terminals().length===1&&terminals()[0].detail.configSaved===true&&!calls.some(c=>['shell.run','tdpMsi.set','tdpDaemon.start','tdpDaemon.request'].includes(c.cmd)));
 for(const source of ['ui','decky','hotkey','schedule','startup','resume','autofloat'] as const){
  await reset('cli-'+source);const result=await setTdp('dc',30,{apply:true,save:false,vendor:'intel',source});await settle();const control=calls.find(c=>c.cmd==='shell.run');
  check('real-unified-entry-'+source+'-argv-and-causal-id',result&&correlated()&&terminals().length===1&&rows[0].detail.source===source&&JSON.stringify(control?.args.args)===JSON.stringify(['set','30','--vendor','intel'])&&control?.args.tdpObservation.operationId===rows[0].detail.operationId);
 }
 await reset('cli-fail');let error='';try{await setTdp('ac',30,{apply:true,save:false,vendor:'intel',source:'ui'});}catch(e){error=String(e);}await settle();check('failed-CLI-original-error-preserved-terminal',error.includes('fixture-first-error')&&terminals()[0]?.event==='tdp.failed'&&correlated());
 await reset('daemon-ok');const daemon=await setTdp('dc',30,{apply:true,save:false,vendor:'intel',source:'autofloat'});await settle();const sent=calls.find(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set');check('daemon-success-diagnostic-metadata-not-in-hardware-args',daemon&&sent?.args.tdpObservation.operationId===rows[0].detail.operationId&&JSON.stringify(sent.args.args)==='{"watts":30}'&&!calls.some(c=>c.cmd==='shell.run'));
 for(const name of ['daemon-fail','daemon-unavailable']){await reset(name);const result=await setTdp('ac',30,{apply:true,save:false,vendor:'intel',source:'autofloat'});await settle();check(name+'-fallback-linked-to-same-request',result&&correlated()&&calls.filter(c=>c.cmd==='shell.run').length===1&&rows.some(r=>r.event==='tdp.route-selected'&&r.detail.fallbackReason));}
 await reset('msi-ok');const msi=await setTdp('ac',30,{apply:true,save:false,source:'decky'});await settle();check('native-MSI-no-helper-or-MSR-replay',msi&&calls.filter(c=>c.cmd==='tdpMsi.set').length===1&&!calls.some(c=>['shell.run','tdpDaemon.start','tdpDaemon.request'].includes(c.cmd))&&terminals()[0].detail.readbackMatched===true);
 await reset('msi-fail');error='';try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch(e){error=String(e);}await settle();check('native-MSI-partial-failure-no-direct-fallback',error.includes('fixture-OEM-partial-unknown')&&terminals()[0]?.event==='tdp.failed'&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('not-ready');ready=false;error='';try{await setTdp('ac',30,{apply:true,save:true,source:'ui'});}catch(e){error=String(e);}await settle();check('early-capability-refusal-still-has-terminal-unclamped-target',!!error&&terminals().length===1&&terminals()[0].detail.clampedW===null&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('sleep-blocked');phase='resuming';error='';try{await setTdp('dc',30,{apply:true,save:false,vendor:'intel',source:'resume'});}catch(e){error=String(e);}await settle();check('existing-lifecycle-gate-rejected-no-extra-write',!!error&&terminals()[0]?.event==='tdp.failed'&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('stale');const stale=await setTdp('ac',30,{apply:true,save:false,vendor:'intel',source:'ui'});await settle();check('sleep-generation-old-result-static-stale-not-current-proof',stale&&terminals()[0].detail.stale===true&&terminals()[0].detail.ignored===true&&tdpRuntimeStore.get().status!=='applied');
 await reset('log-fails');logFails=true;const failures=tdpDiagnosticDeliveryFailures();const success=await setTdp('ac',30,{apply:true,save:false,vendor:'intel',source:'ui'});await settle();check('log-delivery-failure-never-changes-control-result',success&&tdpDiagnosticDeliveryFailures()>failures&&calls.filter(c=>c.cmd==='shell.run').length===1);
 await reset('operator-marker');markTdpOperatorMode('MSI Center M 智能模式');await settle();check('operator-marker-never-changes-mode-or-hardware',calls.length===1&&calls[0].cmd==='tdp.observe'&&rows[0].event==='tdp.operator-marker');
 await reset('resume-no-owner');await stopTdpDaemon();calls=[];rows=[];const resumed=await resumeTdpDaemonAfterWake(false);await settle();check('manual-resume-no-daemon-owner-explicit-skip',resumed&&rows.some(r=>r.event==='tdp.skipped')&&!calls.some(c=>c.cmd==='tdpDaemon.start'||c.cmd==='shell.run'));
 const report={checks,failures:checks.filter(c=>!c.pass).length,scope:'REAL_FRONTEND_UNIFIED_ENTRY_WITH_OFFLINE_IPC_NO_PRODUCT_OR_HARDWARE',realHardwareExecuted:false};writeFileSync(join(base,'report.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));process.exitCode=report.failures?1:0;
}
main().catch(e=>{console.error(e);process.exitCode=1;});
