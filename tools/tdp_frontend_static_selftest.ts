// Real frontend entry points + isolated IPC responders. No commands reach Windows hardware.
import 'vue';
import { hasScheduledTask } from '../src/scheduler';
import './mock-shell';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { setTdp, setTdpCurrentMode, setPowerControlDir, resumeTdpDaemonAfterWake, stopTdpDaemon, primeTdpSession, reconcileTdpPowerSession, invalidateTdpPowerSession, detectPowerMode, detectPowerModeReliable, detectPowerModeStable, setActiveScheme, applyFloatPowerParams, applyManualPowerParams, PW } from '../src/bridge/yeman';
import { resetTdpCapabilityState } from '../src/bridge/tdpCircuitBreaker';
import { tdpRuntimeStore } from '../src/bridge/tdpRuntimeState';
import { tdpDiagnosticDeliveryFailures, markTdpOperatorMode } from '../src/bridge/tdpStaticDiagnostics';
const base=resolve(process.argv[2]);mkdirSync(base,{recursive:true});
const checks:Array<{name:string;pass:boolean}>=[];let scenario='cli',selected=false,floating=false,ready=true,logFails=false,phase='ready';
let calls:Array<{cmd:string;args:any}>=[],rows:Array<{event:string;detail:any}>=[];let resumedGeneration=0;let powerGeneration=1;let holdPower:((value:any)=>void)|null=null;let holdResume:((value:any)=>void)|null=null;let holdSet:((value:any)=>void)|null=null;
(globalThis as any).__mockShellDryRun=true;
(globalThis as any).__mockIpcResponder=(cmd:string,args:any)=>{
 calls.push({cmd,args});
 if(cmd==='tdp.observe'){rows.push(...structuredClone(args.events ?? [args]));if(logFails)throw new Error('fixture-log-delivery-failed');return {ok:true};}
 if(cmd==='tdp.capability'){if(scenario==='capability-throws')throw Error('fixture-capability-not-ready');return {provider:selected?'msi-g3e-wmi':'default',selected,ready,minPL1:selected?8:2,maxPL1:selected?35:200,cpuFamily:selected?'intel-arc-g3-extreme':'other-or-unverified',generation:1,deadlineTick:123456};}
 if(cmd==='power.setActiveScheme'){if(scenario==='activation-old')throw Error('unknown: power.setActiveScheme');if(scenario==='activation-timeout')throw Error('operation observer timed out');return {ok:scenario!=='activation-failed',code:scenario==='activation-failed'?5:0};}
 if(cmd==='power.sourceSnapshot'){if(scenario==='power-held')return new Promise(resolve=>{holdPower=resolve;});return scenario==='power-unknown'?{known:false,acLine:255}:{known:true,acLine:1};}
 if(cmd==='power.lifecycle')return {phase,generation:powerGeneration,hardwareWritesAllowed:phase==='ready',inputReady:phase==='ready'};
 if(cmd==='registry.writePowerBatch'){
  if(scenario==='cpu-lease')throw Error('hardware writes are blocked during power transition');
  if(scenario==='cpu-malformed')return {ok:false,written:0,failed:[]};
  const failed=scenario==='cpu-required'?[{setting:PW.G_FREQ1,code:5}]:scenario==='cpu-optional'?[{setting:PW.G_THROT,code:50}]:[];
  return {ok:failed.length===0,written:args.entries.length-failed.length,failed};
 }
 if(cmd==='registry.read')return args.name==='VendorIdentifier'?'GenuineIntel':null;
 if(cmd==='fs.stat'&&String(args.path).endsWith('float-active')){if(floating)return {modified:Date.now()/1000};throw new Error('fixture-float-disabled');}
 if(cmd==='tdpDaemon.start')throw new Error('fixture-daemon-start-unavailable');
 if(cmd==='tdpDaemon.request'){
  if(args.op==='resume'){if(scenario==='daemon-pending-resume')return new Promise(resolve=>{holdResume=resolve;});if(scenario==='daemon-failed-resume')return {ok:false,rc:6,result:{reinitialized:false,resumeGeneration:powerGeneration}};resumedGeneration=Number(args.args.generation)||1;if(scenario==='daemon-stale-resume')invalidateTdpPowerSession('resuming');if(scenario==='daemon-late-resume')throw new Error('observer timeout after actual completion');return {ok:true,rc:0,result:{reinitialized:true,resumeGeneration:resumedGeneration}};}
  if(args.op==='ping')return {result:{reinitialized:resumedGeneration>0,resumeGeneration:resumedGeneration},ok:scenario.startsWith('daemon')&&scenario!=='daemon-unavailable',rc:scenario.startsWith('daemon')&&scenario!=='daemon-unavailable'?0:7,requestId:'fixture-existing-daemon-id'};
  if(args.op==='set'&&scenario==='daemon-held-set')return new Promise(resolve=>{holdSet=resolve;});
  if(args.op==='set'&&scenario==='daemon-verified-amd')return {ok:true,rc:0,result:{amdObservation:{schemaVersion:1,hardwareCommandReturned:true,readbackMatched:true,verificationScope:'StrixHalo_PM_table_limit_fields_only'}}};
  if(args.op==='set'&&scenario==='daemon-mismatch-amd')return {ok:false,rc:6,error:'amd_tdp_readback_mismatch: requested=31W observed=55W'};
  if(args.op==='set')return {ok:!['daemon-fail','daemon-native-power-block','daemon-unsupported','daemon-helper-notready'].includes(scenario),rc:['daemon-fail','daemon-native-power-block','daemon-unsupported','daemon-helper-notready'].includes(scenario)?7:0,requestId:'fixture-existing-daemon-id',error:scenario==='daemon-fail'?'fixture-original-daemon-error':scenario==='daemon-native-power-block'?'hardware writes are blocked during power transition':scenario==='daemon-unsupported'?'TDP unsupported on this transport':scenario==='daemon-helper-notready'?'hardware_resume_not_ready':undefined};
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
async function reset(name:string){scenario=name;selected=name.startsWith('msi');floating=name.startsWith('daemon');ready=true;phase='ready';logFails=false;resumedGeneration=0;powerGeneration=1;holdResume=null;holdSet=null;holdPower=null;calls=[];rows=[];resetTdpCapabilityState();tdpRuntimeStore.invalidatePower();setPowerControlDir(join(base,name));await Promise.resolve();}
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
 await reset('daemon-unavailable');const fallback=await setTdp('ac',30,{apply:true,save:false,vendor:'intel',source:'autofloat'});await settle();check('daemon-unavailable-before-write-fallback-linked-to-same-request',fallback&&correlated()&&calls.filter(c=>c.cmd==='shell.run').length===1&&rows.some(r=>r.event==='tdp.route-selected'&&r.detail.fallbackReason));
 await reset('daemon-fail');error='';try{await setTdp('ac',30,{apply:true,save:false,vendor:'intel',source:'autofloat'});}catch(e){error=String(e);}await settle();check('daemon-write-failure-never-replayed-or-falsely-applied',error.includes('fixture-original-daemon-error')&&terminals()[0]?.event==='tdp.failed'&&correlated()&&!calls.some(c=>c.cmd==='shell.run')&&tdpRuntimeStore.get().status==='failed');
 for(const source of ['ui','decky','hotkey','schedule','startup','resume','autofloat'] as const){await reset('daemon-hot-'+source);await setTdp('ac',30,{apply:true,save:false,source});await settle();calls=[];rows=[];for(let i=0;i<8;i++)await setTdp('ac',31+i,{apply:true,save:false,source});await settle();check('hot-session-'+source+'-no-process-ping-marker-or-vendor-probe',calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').length===8&&!calls.some(c=>c.cmd==='shell.run'||c.cmd==='power.lifecycle'||c.cmd==='tdpDaemon.start'||c.cmd==='fs.stat'||c.cmd==='registry.read'||c.cmd==='fs.exists'||c.cmd==='tdpDaemon.request'&&c.args.op==='ping'));check('hot-session-'+source+'-bounded-log-ipc-preserves-causal-rows',calls.filter(c=>c.cmd==='tdp.observe').length===16&&rows.filter(r=>r.event==='tdp.completed').length===8&&rows.filter(r=>r.event==='tdp.request').length===8);}

 await reset('daemon-startup');const primed=await primeTdpSession();await settle();check('startup-primes-capability-lifecycle-session-without-hardware-write',primed&&!calls.some(c=>c.cmd==='shell.run'||c.cmd==='tdpDaemon.request'&&c.args.op==='set'));
 await reset('daemon-late-resume');await primeTdpSession();calls=[];rows=[];const late=await resumeTdpDaemonAfterWake(true);await settle();check('late-resume-success-reconciles-generation-without-restart-or-replay',late&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='resume').length===1&&!calls.some(c=>c.cmd==='tdpDaemon.start'||c.cmd==='tdpDaemon.request'&&c.args.op==='quit'));
 await reset('daemon-stale-resume');await primeTdpSession();const oldResume=await resumeTdpDaemonAfterWake(true);await settle();check('new-power-boundary-rejects-old-resume-completion',!oldResume);
 await reset('daemon-power-block');await primeTdpSession();calls=[];rows=[];invalidateTdpPowerSession('suspending');error='';try{await setTdp('ac',30,{apply:true,save:false,source:'autofloat'});}catch(e){error=String(e);}await settle();check('suspend-event-blocks-from-memory-without-poll-or-hardware',!!error&&!calls.some(c=>c.cmd==='power.lifecycle'||c.cmd==='shell.run'||c.cmd==='tdpDaemon.request'));
 await reset('daemon-pending-resume');await primeTdpSession();calls=[];const shared1=resumeTdpDaemonAfterWake(true);await settle();const shared2=resumeTdpDaemonAfterWake(true);await settle();const sharedCount=calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='resume').length;const sharedRelease=holdResume as unknown as ((value:any)=>void);sharedRelease({ok:true,rc:0,result:{reinitialized:true,resumeGeneration:1}});check('same-power-epoch-shares-one-pending-resume',sharedCount===1&&await shared1&&await shared2);
 await reset('daemon-pending-resume');await primeTdpSession();calls=[];const previous=resumeTdpDaemonAfterWake(true);await settle();const oldRelease=holdResume as unknown as ((value:any)=>void);powerGeneration=2;invalidateTdpPowerSession('resuming',2);invalidateTdpPowerSession('ready',2);const next=resumeTdpDaemonAfterWake(true);await settle();const newRelease=holdResume as unknown as ((value:any)=>void);check('new-power-epoch-starts-own-resume-before-old-completion',newRelease!==oldRelease&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='resume').length===2);newRelease({ok:true,rc:0,result:{reinitialized:true,resumeGeneration:2}});const nextOk=await next;oldRelease({ok:true,rc:0,result:{reinitialized:true,resumeGeneration:1}});check('late-old-epoch-cannot-clear-or-publish-new-recovery',nextOk&&!await previous);
 await reset('msi-ok');const msi=await setTdp('ac',30,{apply:true,save:false,source:'decky'});await settle();check('native-MSI-no-helper-or-MSR-replay',msi&&calls.filter(c=>c.cmd==='tdpMsi.set').length===1&&!calls.some(c=>['shell.run','tdpDaemon.start','tdpDaemon.request'].includes(c.cmd))&&terminals()[0].detail.readbackMatched===true);
 await reset('msi-fail');error='';try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch(e){error=String(e);}await settle();check('native-MSI-partial-failure-no-direct-fallback',error.includes('fixture-OEM-partial-unknown')&&terminals()[0]?.event==='tdp.failed'&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('not-ready');ready=false;error='';try{await setTdp('ac',30,{apply:true,save:true,source:'ui'});}catch(e){error=String(e);}await settle();check('early-capability-refusal-still-has-terminal-unclamped-target',!!error&&terminals().length===1&&terminals()[0].detail.clampedW===null&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('sleep-blocked');phase='resuming';error='';try{await setTdp('dc',30,{apply:true,save:false,vendor:'intel',source:'resume'});}catch(e){error=String(e);}await settle();check('existing-lifecycle-gate-rejected-no-extra-write',!!error&&terminals()[0]?.event==='tdp.failed'&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('stale');let staleAccepted=0;const stale=await setTdp('ac',30,{apply:true,save:false,vendor:'intel',source:'ui',onAccepted:()=>{staleAccepted++;}});await settle();check('sleep-generation-old-result-static-stale-not-current-proof',!stale&&staleAccepted===0&&terminals()[0].detail.stale===true&&terminals()[0].detail.ignored===true&&tdpRuntimeStore.get().status!=='applied');
 await reset('log-fails');logFails=true;const failures=tdpDiagnosticDeliveryFailures();const success=await setTdp('ac',30,{apply:true,save:false,vendor:'intel',source:'ui'});await settle();check('log-delivery-failure-never-changes-control-result',success&&tdpDiagnosticDeliveryFailures()>failures&&calls.filter(c=>c.cmd==='shell.run').length===1);
 await reset('operator-marker');markTdpOperatorMode('MSI Center M 智能模式');await settle();check('operator-marker-never-changes-mode-or-hardware',calls.length===1&&calls[0].cmd==='tdp.observe'&&rows[0].event==='tdp.operator-marker');
 await reset('resume-no-owner');await stopTdpDaemon();calls=[];rows=[];const resumed=await resumeTdpDaemonAfterWake(false);await settle();check('manual-resume-no-daemon-owner-explicit-skip',resumed&&rows.some(r=>r.event==='tdp.skipped')&&!calls.some(c=>c.cmd==='tdpDaemon.start'||c.cmd==='shell.run'));
 await reset('power-known');tdpRuntimeStore.powerMode('ac');calls=[];for(let i=0;i<1000;i++)await detectPowerModeReliable();const stableAc=await detectPowerModeStable();check('known-source-event-serves-1000-calls-with-zero-IO',stableAc==='ac'&&calls.length===0);
 tdpRuntimeStore.powerMode('dc');check('source-event-immediately-selects-DC-without-query',await detectPowerMode()==='dc'&&await detectPowerModeReliable()==='dc'&&calls.length===0);
 await reset('power-held');const cold=detectPowerModeReliable();await settle();tdpRuntimeStore.powerMode('dc');(holdPower as unknown as ((value:any)=>void))({known:true,acLine:1});const currentPower=await cold;check('late-startup-source-read-cannot-overwrite-newer-event',currentPower==='dc'&&tdpRuntimeStore.get().mode==='dc'&&calls.filter(c=>c.cmd==='power.sourceSnapshot').length===1);
 await reset('power-unknown');let unknown=true;for(let i=0;i<1000;i++)unknown=unknown&&(await detectPowerModeReliable())===null;check('unknown-source-caches-refusal-without-per-adjustment-probe',unknown&&calls.filter(c=>c.cmd==='power.sourceSnapshot').length===1&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('power-held');const concurrent=Array.from({length:12},()=>detectPowerModeReliable());await settle();const sourceCalls=calls.filter(c=>c.cmd==='power.sourceSnapshot').length;(holdPower as unknown as ((value:any)=>void))({known:true,acLine:1});const answers=await Promise.all(concurrent);check('concurrent-cold-callers-share-one-existing-source-snapshot',sourceCalls===1&&answers.every(side=>side==='ac'));
 await reset('activation-ok');for(let i=0;i<100;i++)await setActiveScheme(PW.YEMAN);check('CPU-policy-reapply-uses-native-without-process-or-source-query',calls.filter(c=>c.cmd==='power.setActiveScheme').length===100&&!calls.some(c=>c.cmd==='shell.run'||c.cmd==='power.sourceSnapshot'||c.cmd==='system.info'));
 await reset('activation-old');await setActiveScheme(PW.YEMAN);check('old-shell-explicit-unknown-retains-single-CLI-activation',calls.filter(c=>c.cmd==='shell.run'&&c.args.program==='powercfg').length===1);
 await reset('activation-failed');error='';try{await setActiveScheme(PW.YEMAN)}catch(e){error=String(e)}check('native-policy-rejection-never-replays-through-CLI',!!error&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('activation-timeout');error='';try{await setActiveScheme(PW.YEMAN)}catch(e){error=String(e)}check('uncertain-policy-activation-never-starts-duplicate-command',!!error&&!calls.some(c=>c.cmd==='shell.run'));
 const cpuPacket={acFreq:2800,dcFreq:1600,acAggr:55,dcAggr:25,acTurbo:true,dcTurbo:false,acMinState:37,dcMinState:20,sides:['ac'] as Array<'ac'|'dc'>,restoreMaxState:false};
 await reset('cpu-ok');const cpuOk=await applyFloatPowerParams(cpuPacket);const cpuCalls=calls.filter(c=>c.cmd==='registry.writePowerBatch'),cpuEntries=cpuCalls[0]?.args.entries;
 check('floating-CPU-single-batch-preserves-all-three-core-class-values',cpuOk&&cpuCalls.length===1&&cpuCalls[0].args.valueName==='ACSettingIndex'&&cpuEntries?.length===11&&[PW.G_FREQ1,PW.G_FREQ2,PW.G_FREQ3].every(setting=>cpuEntries.some((e:any)=>e.setting===setting&&e.value===2800))&&[PW.G_MIN1,PW.G_MIN2,PW.G_MIN3].every(setting=>cpuEntries.some((e:any)=>e.setting===setting&&e.value===37)));
 await reset('cpu-required');error='';try{await applyFloatPowerParams(cpuPacket)}catch(e){error=String(e)}check('floating-required-CPU-failure-cannot-report-applied-or-replay',error.includes(':5')&&calls.filter(c=>c.cmd==='registry.writePowerBatch').length===1&&!calls.some(c=>c.cmd==='shell.run'||c.cmd==='power.setActiveScheme'));
 await reset('cpu-optional');check('floating-optional-unsupported-setting-keeps-original-compatibility',await applyFloatPowerParams(cpuPacket)&&calls.filter(c=>c.cmd==='registry.writePowerBatch').length===1);
 await reset('cpu-lease');error='';try{await applyFloatPowerParams(cpuPacket)}catch(e){error=String(e)}check('floating-CPU-power-lease-refusal-reaches-existing-controller-backoff',error.includes('power transition')&&calls.filter(c=>c.cmd==='registry.writePowerBatch').length===1);
 await reset('cpu-malformed');error='';try{await applyFloatPowerParams(cpuPacket)}catch(e){error=String(e)}check('floating-negative-batch-without-failure-detail-stays-unconfirmed',!!error&&calls.filter(c=>c.cmd==='registry.writePowerBatch').length===1);
 await reset('cpu-required');check('explicit-manual-CPU-retains-original-best-effort-policy',await applyManualPowerParams(cpuPacket));
 for(const side of ['ac','dc'] as const){
  await reset('cpu-lease');error='';try{await applyManualPowerParams({...cpuPacket,sides:[side]});}catch(e){error=String(e);}
  check('manual-CPU-'+side+'-lease-refusal-is-not-success-or-CLI-replay',error.includes('power transition')&&calls.filter(c=>c.cmd==='registry.writePowerBatch').length===1&&!calls.some(c=>c.cmd==='shell.run'||c.cmd==='power.setActiveScheme'));
  scenario='cpu-ok';calls=[];const applied=await applyManualPowerParams({...cpuPacket,sides:[side]});
  check('manual-CPU-'+side+'-recovered-submit-retains-original-values',applied&&calls.filter(c=>c.cmd==='registry.writePowerBatch').length===1&&calls[0].args.valueName===(side==='ac'?'ACSettingIndex':'DCSettingIndex')&&calls[0].args.entries.some((e:any)=>e.setting===PW.G_FREQ1&&e.value===(side==='ac'?2800:1600))&&!calls.some(c=>c.cmd==='power.lifecycle'||c.cmd==='power.sourceSnapshot'||c.cmd==='shell.run'));
 }

 await reset('daemon-missed-ready');await primeTdpSession();powerGeneration=2;invalidateTdpPowerSession('resuming',2);calls=[];
 phase='resuming';check('reconcile-cannot-admit-real-native-transition',!await reconcileTdpPowerSession()&&!calls.some(c=>c.cmd==='tdpDaemon.request'||c.cmd==='shell.run'));
 phase='ready';calls=[];check('missed-ready-native-snapshot-heals-without-hardware-replay',await reconcileTdpPowerSession()&&calls.filter(c=>c.cmd==='power.lifecycle').length===1&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='resume').length===1&&!calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set'||c.cmd==='shell.run'));
 calls=[];await setTdp('ac',30,{apply:true,save:false,source:'ui'});await settle();check('healed-session-accepts-new-user-request-without-per-write-power-probe',calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set')&&!calls.some(c=>c.cmd==='power.lifecycle'||c.cmd==='power.sourceSnapshot'));
 await reset('daemon-all-events-missed');await primeTdpSession();powerGeneration=2;calls=[];
 check('missed-entire-power-boundary-resumes-owned-helper-once-without-replay',await reconcileTdpPowerSession()&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='resume').length===1&&!calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set'||c.cmd==='shell.run'));
 await reset('daemon-native-power-block');error='';try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch(e){error=String(e);}scenario='daemon-recovered';calls=[];
 const powerHealed=await reconcileTdpPowerSession();await setTdp('ac',31,{apply:true,save:false,source:'ui'});await settle();
 check('confirmed-ready-clears-only-power-refusal-backoff-for-new-request',error.includes('power transition')&&powerHealed&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').length===1);
 await reset('daemon-fail');try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch{}scenario='daemon-recovered';calls=[];await reconcileTdpPowerSession();error='';try{await setTdp('ac',31,{apply:true,save:false,source:'ui'});}catch(e){error=String(e);}await settle();
 check('ready-reconciliation-does-not-clear-other-failure-backoff-or-replay',error.includes('backoff')&&!calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set'||c.cmd==='shell.run'));

 await reset('capability-cold');ready=false;for(let i=0;i<3;i++){try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch{}}await settle();
 check('negative-capability-is-cached-with-one-shared-recovery-task',calls.filter(c=>c.cmd==='tdp.capability').length===1&&hasScheduledTask('tdp-session-recovery')&&!calls.some(c=>c.cmd==='shell.run'||c.cmd==='tdpDaemon.request'));
 ready=true;calls=[];const capabilityHealed=await reconcileTdpPowerSession();
 check('negative-capability-fresh-check-recovers-without-hardware-write',capabilityHealed&&calls.filter(c=>c.cmd==='tdp.capability').length===1&&!hasScheduledTask('tdp-session-recovery')&&!calls.some(c=>c.cmd==='shell.run'||c.cmd==='tdpDaemon.request'));
 await reset('capability-throws');for(let i=0;i<3;i++){try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch{}}await settle();
 check('failed-capability-IPC-does-not-reprobe-on-every-adjustment',calls.filter(c=>c.cmd==='tdp.capability').length===1&&hasScheduledTask('tdp-session-recovery'));
 scenario='cli';calls=[];check('failed-capability-IPC-recovers-on-read-only-reconciliation',await reconcileTdpPowerSession()&&calls.filter(c=>c.cmd==='tdp.capability').length===1&&!hasScheduledTask('tdp-session-recovery')&&!calls.some(c=>c.cmd==='shell.run'));
 await reset('daemon-unsupported');try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch{}calls=[];
 const heartbeatUnsupported=await reconcileTdpPowerSession();
 check('heartbeat-does-not-automatically-replay-unsupported-backend',!heartbeatUnsupported&&!hasScheduledTask('tdp-session-recovery')&&!calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set'||c.cmd==='shell.run'));
 scenario='daemon-recovered';calls=[];const reopened=await reconcileTdpPowerSession(true);await setTdp('ac',31,{apply:true,save:false,source:'ui'});await settle();
 check('explicit-reopen-rechecks-capability-before-new-unsupported-retry',reopened&&calls.filter(c=>c.cmd==='tdp.capability').length===1&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').length===1);
 await reset('daemon-unsupported');try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch{}setPowerControlDir(join(base,'restart-fresh'));scenario='daemon-recovered';calls=[];await setTdp('ac',32,{apply:true,save:false,source:'ui'});await settle();
 check('new-runtime-session-clears-old-unsupported-latch-and-recovery-task',calls.filter(c=>c.cmd==='tdp.capability').length===1&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').length===1&&!hasScheduledTask('tdp-session-recovery'));
 await reset('capability-throws');try{await setTdp('ac',30,{apply:true,save:false,source:'ui'});}catch{}await stopTdpDaemon();check('explicit-OFF-clears-recovery-even-with-capability-IPC-failure',!hasScheduledTask('tdp-session-recovery'));
 await reset('daemon-helper-notready');try{await setTdp('dc',19,{apply:true,save:false,source:'ui'});}catch{}scenario='daemon-recovered';calls=[];const helperHealed=await reconcileTdpPowerSession();
 check('helper-not-ready-recovers-handles-without-replaying-old-watts',helperHealed&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='resume').length===1&&!calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set'||c.cmd==='shell.run')&&!hasScheduledTask('tdp-session-recovery'));
 await reset('daemon-pending-resume');await primeTdpSession();powerGeneration=2;invalidateTdpPowerSession('resuming',2);invalidateTdpPowerSession('ready',2);const stoppedRecovery=reconcileTdpPowerSession();await settle();const stoppedRelease=holdResume as unknown as ((value:any)=>void);if(!stoppedRelease)throw Error('fixture recovery did not reach owned resume');await stopTdpDaemon();calls=[];stoppedRelease({ok:true,rc:0,result:{reinitialized:true,resumeGeneration:2}});const stoppedResult=await stoppedRecovery;await settle();
 check('late-recovery-completion-after-explicit-OFF-cannot-revive-session',!stoppedResult&&!hasScheduledTask('tdp-session-recovery')&&!calls.some(c=>c.cmd==='tdpDaemon.start'||c.cmd==='tdpDaemon.request'&&c.args.op==='set'));
 await reset('daemon-invalid-command');let invalidRefusals=0;for(const value of [0,-1,NaN,Infinity,201]){try{await setTdp('ac',value,{apply:true,save:true,source:'autofloat'});}catch{invalidRefusals++;}}await settle();
 check('invalid-floating-command-never-clamps-to-2W-or-writes-config',invalidRefusals===5&&!calls.some(c=>['tdp.capability','shell.run','tdpDaemon.request','tdpMsi.set','fs.write','fs.writeText'].includes(c.cmd)));
 calls=[];await setTdp('ac',2,{apply:true,save:false,source:'autofloat'});await setTdp('ac',30,{apply:true,save:false,source:'autofloat'});await settle();
 check('invalid-command-does-not-latch-failure-or-ban-legitimate-2W',calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').map(c=>c.args.args.watts).join(',')==='2,30');
 await reset('daemon-controller-cancel');const cancelled=await setTdp('ac',2,{apply:true,save:false,source:'autofloat',isCurrent:()=>false});await settle();
 check('revoked-controller-request-skips-before-capability-or-hardware',!cancelled&&!calls.some(c=>['tdp.capability','shell.run','tdpDaemon.request','tdpMsi.set'].includes(c.cmd))&&terminals().length===1&&terminals()[0].event==='tdp.skipped');
 await reset('daemon-held-set');const earlier=setTdp('ac',30,{apply:true,save:false,source:'ui'});await settle();const oldLow=setTdp('ac',2,{apply:true,save:false,source:'autofloat'});await settle();const latest=setTdp('ac',32,{apply:true,save:false,source:'ui'});await settle();const setRelease=holdSet as unknown as ((value:any)=>void);if(!setRelease)throw Error('fixture write did not reach held daemon');scenario='daemon-recovered';setRelease({ok:true,rc:0});const queuedResults=await Promise.all([earlier,oldLow,latest]);await settle();
 check('superseded-queued-low-target-never-reaches-hardware-or-poisons-next',queuedResults[1]===false&&queuedResults[2]===true&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').map(c=>c.args.args.watts).join(',')==='30,32');
 await reset('daemon-invalid-hotkey');let invalidHotkeys=0;for(const value of [0,-1,NaN,Infinity,201]){try{await setTdpCurrentMode(value);}catch{invalidHotkeys++;}}await settle();
 check('invalid-current-mode-command-is-rejected-before-clamp-probes-or-write',invalidHotkeys===5&&!calls.some(c=>['power.sourceSnapshot','tdp.capability','shell.run','tdpDaemon.request','tdpMsi.set','fs.write','fs.writeText'].includes(c.cmd)));
 calls=[];await setTdpCurrentMode(2);await setTdpCurrentMode(30);await settle();
 check('invalid-current-mode-command-keeps-legitimate-low-and-normal-targets-usable',calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').map(c=>c.args.args.watts).join(',')==='2,30');
 await reset('daemon-held-set');const staleSession=setTdp('ac',30,{apply:true,save:false,source:'ui'});await settle();const staleSessionRelease=holdSet as unknown as ((value:any)=>void);if(!staleSessionRelease)throw Error('fixture stale session did not reach held daemon');
 setPowerControlDir(join(base,'reset-during-old-write'));scenario='daemon-recovered';calls=[];staleSessionRelease({ok:false,rc:7,error:'TDP unsupported on this old transport'});const staleSessionResult=await staleSession;await settle();
 const newSessionResult=await setTdp('ac',32,{apply:true,save:false,source:'ui'});await settle();
 check('late-old-session-unsupported-cannot-relatch-new-session-or-block-next-target',!staleSessionResult&&newSessionResult&&calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').map(c=>c.args.args.watts).join(',')==='32'&&!hasScheduledTask('tdp-session-recovery'));
 await reset('daemon-controller-envelope');let envelopeRejected=0;
 for(const value of [2,9,16,30]){try{await setTdp('ac',value,{apply:true,save:true,source:'autofloat',minWatts:10,maxWatts:15});}catch{envelopeRejected++;}}await settle();
 check('controller-envelope-rejects-below-floor-and-above-budget-before-save-or-hardware',envelopeRejected===4&&!calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set'||c.cmd==='shell.run'||c.cmd==='tdpMsi.set'||c.cmd==='fs.write'||c.cmd==='fs.writeText'));
 calls=[];await setTdp('ac',10,{apply:true,save:false,source:'autofloat',minWatts:10,maxWatts:15});await setTdp('ac',15,{apply:true,save:false,source:'autofloat',minWatts:10,maxWatts:15});await settle();
 check('controller-envelope-keeps-valid-floor-and-budget-writes-usable',calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').map(c=>c.args.args.watts).join(',')==='10,15');
 await reset('msi-controller-envelope');let resolvedRejected=false;try{await setTdp('ac',5,{apply:true,save:false,source:'autofloat',minWatts:2,maxWatts:5});}catch{resolvedRejected=true;}await settle();
 check('resolved-device-minimum-cannot-expand-controller-budget-at-real-dispatch',resolvedRejected&&!calls.some(c=>c.cmd==='tdpMsi.set'||c.cmd==='shell.run'||c.cmd==='tdpDaemon.request'&&c.args.op==='set'));
 await reset('daemon-held-set');let currentOwner=true,revokedAccepted=0;
 const revokedAfterDispatch=setTdp('ac',30,{apply:true,save:false,source:'autofloat',isCurrent:()=>currentOwner,onAccepted:()=>{revokedAccepted++;}});await settle();
 const revokedRelease=holdSet as unknown as ((value:any)=>void);if(!revokedRelease)throw Error('revoked write did not reach held daemon');currentOwner=false;scenario='daemon-recovered';revokedRelease({ok:true,rc:0});
 const revokedResult=await revokedAfterDispatch;await settle();
 check('revoked-after-dispatch-ack-cannot-publish-current-accepted-watts',!revokedResult&&revokedAccepted===0&&tdpRuntimeStore.get().status!=='applied');
 const compatibilityBoundaries:string[]=[];
 const compatibilityRealNow=Date.now;let compatibilityNow=compatibilityRealNow();Date.now=()=>compatibilityNow;
 await reset('daemon-verified-amd');const verified=await setTdp('ac',31,{apply:true,save:false,source:'ui'});await settle();
 if(!verified||!calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set'&&c.args.args.verifyTdp===true)||terminals()[0]?.detail.readbackMatched!==true)throw Error('Strix Halo static verification was not consumed');
 compatibilityBoundaries.push('static-request-verification-and-returned-limits-consumed');
 await reset('daemon-mismatch-amd');error='';try{await setTdp('ac',31,{apply:true,save:false,source:'ui'});}catch(e){error=String(e);}await settle();
 if(!error.includes('amd_tdp_readback_mismatch')||terminals()[0]?.event!=='tdp.failed'||calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').length!==1||calls.some(c=>c.cmd==='shell.run'))throw Error('Strix Halo mismatch was hidden or replayed');
 compatibilityBoundaries.push('mismatch-is-explicit-and-no-write-replay');
 const writesBeforeRetry=calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').length;
 let earlyRetry='';try{await setTdp('ac',31,{apply:true,save:false,source:'ui'});}catch(e){earlyRetry=String(e);}await settle();
 if(!earlyRetry.includes('backoff')||calls.filter(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set').length!==writesBeforeRetry)throw Error('Existing TDP failure backoff was bypassed');
 compatibilityNow+=5001; // Existing 5000ms breaker, advanced only in this inert fixture.
 scenario='daemon-verified-amd';const recovered=await setTdp('ac',31,{apply:true,save:false,source:'ui'});await settle();if(!recovered)throw Error('Strix Halo mismatch permanently latched the next request');
 compatibilityBoundaries.push('new-explicit-request-recovers-without-permanent-latch');
 await reset('daemon-float-compat');await setTdp('ac',30,{apply:true,save:false,source:'autofloat'});await settle();
 if(!calls.some(c=>c.cmd==='tdpDaemon.request'&&c.args.op==='set'&&JSON.stringify(c.args.args)==='{"watts":30}')||calls.some(c=>c.cmd==='power.sourceSnapshot'||c.cmd==='registry.read'))throw Error('Strix Halo compatibility added high-frequency read work');
 compatibilityBoundaries.push('float-stays-watts-only-no-extra-read-or-power-probe');Date.now=compatibilityRealNow;
 const report={checks,compatibilityBoundaries,failures:checks.filter(c=>!c.pass).length,scope:'REAL_FRONTEND_UNIFIED_ENTRY_WITH_OFFLINE_IPC_NO_PRODUCT_OR_HARDWARE',realHardwareExecuted:false};writeFileSync(join(base,'report.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));process.exitCode=report.failures?1:0;
}
main().catch(e=>{console.error(e);process.exitCode=1;});
