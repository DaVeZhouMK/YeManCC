import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {isDeepStrictEqual} from 'node:util';

// Reconcile both separately written receipts without promoting the raw negative to PASS.
export function bindPsExitArmEvidence(out, arm){
 assert(['full-pipe-core','full-pipe-core-negative'].includes(arm.mode),'PS_EXIT_UNKNOWN_EVIDENCE_ARM');
 const read=name=>{
  const file=path.join(out,arm.mode,name);
  assert(fs.lstatSync(file).isFile()&&path.relative(fs.realpathSync(out),fs.realpathSync(file))===path.join(arm.mode,name),'PS_EXIT_RECEIPT_OUTSIDE_ARM');
  const bytes=fs.readFileSync(file);
  return {value:JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,'')),pin:{path:file,bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')}};
 };
 const native=read('fixture-result.json');
 assert(isDeepStrictEqual(native.value,arm.result),'PS_EXIT_NATIVE_RAW_CHANGED');
 const {checks,assertionTrace,failures,cases}=native.value;
 assert(Number.isSafeInteger(checks)&&checks>0&&assertionTrace.length===checks,'PS_EXIT_ASSERTION_TRACE_INCOMPLETE');
 assert(assertionTrace.every(row=>typeof row.passed==='boolean'),'PS_EXIT_ASSERTION_VERDICT_INVALID');
 assert.deepEqual(assertionTrace.filter(row=>!row.passed).map(row=>row.assertion),failures.map(row=>row.assertion),'PS_EXIT_ASSERTION_TRACE_FAILURE_MISMATCH');
 const managed=read('full-pipe-child-1.json');
 const {nativeObservedProcessExit,nativeObservedExitCode,nativeObserverCloseSucceeded,nativeObserverCloseError,...child}=cases[0].managedReceipt;
 assert(nativeObservedProcessExit===true&&nativeObservedExitCode===0&&nativeObserverCloseSucceeded===true&&nativeObserverCloseError===0,'PS_EXIT_NATIVE_CHILD_NOT_RETURNED');
 assert(isDeepStrictEqual(child,managed.value),'PS_EXIT_MANAGED_RAW_NOT_BOUND_TO_NATIVE');
 assert(child.status==='OBSERVED'&&child.failure===null&&child.finalCleanupOk===true&&child.hasController===false&&child.hasContext===false,'PS_EXIT_MANAGED_CHILD_NOT_RETURNED');
 const power=child.powerOwnershipAfterFinalCleanup;
 const counts=['currentSlots','retiredSlots','currentBorrowers','retiredBorrowers','pendingLocalReturns'];
 const flags=['importInFlight','commandBorrower','closeRefreshBorrower','originalHasBorrowers','originalLocalReturnPending'];
 assert(power?.bound===true&&power.terminal===true&&counts.every(key=>power[key]===0)&&flags.every(key=>power[key]===false),'PS_EXIT_MANAGED_POWER_STILL_OWNED');
 return {mode:arm.mode,native:native.pin,managed:managed.pin,rawStatus:native.value.status,actualAssertions:checks};
}

// Project the original controller exit path into the private full-pipe test.
export function projectPsExitReceipt({text,extract,replace,sha,paired=false}){
 const receipts=[];
 const query='if (inputHostRequestLocked("QUERY_TARGET_OWNERSHIP",json::object(),"target-ownership",2000,&receipt)';
 const receiptCapture=extract(text,'psWakeCaptureEndpointOwnership').code;
 const guardedQuery=query+' && current()) {';
 const currentReceipt=receiptCapture.includes(guardedQuery);
 if(receiptCapture.split(currentReceipt?guardedQuery:query+') {').length!==2)throw Error('PS_EXIT_RECEIPT_ADMISSION_SOURCE_DRIFT');
 function transform(name,edit){
  const original=extract(text,name).code,after=edit(original);
  text=replace(text,original,after,name+' explicit PS exit port');
  receipts.push({symbol:name,beforeSha256:sha(original),afterSha256:sha(after)});
 }
 function port(body,from,to='(void)0;'){return replace(body,from,to,'PS exit external leaf '+from);}
 function removeBetween(body,start,end){
  const i=body.indexOf(start),j=body.indexOf(end);
  if(i<0||j<=i||body.lastIndexOf(start)!==i||body.lastIndexOf(end)!==j)throw Error('PS_EXIT_SCOPE_DRIFT:'+start);
  return body.slice(0,i)+body.slice(j);
 }
 transform('beginAsyncExit',body=>{
  for(const leaf of ['steamControllerCursorShutdown();','cpuUvTrialStop("exit");','g_optiCatalogStartup.cancel();',
   'GameSteamContext::cancelCompletionNotice();','g_steamTrackerCompletionDispatcher->retire();',
   'g_deckySidebarRuntime.requestExitFence();','focusClearSession();','ymcc::screenpads::quiesceForOwner();',
   'steamOriginalRecoveryRequestStop();','ymcc::batterycharge::seal();','recoverySnapSetExitIntent();'])body=port(body,leaf);
  body=port(body,'if (!hwnd || !IsWindow(hwnd)) return;','if (!hwnd) return;');
  body=removeBetween(body,'    if (g_ctrl) g_ctrl->put_IsVisible(FALSE);','    (void)tryStartExitCleanupWorker(hwnd);');
  const timer=body.indexOf('    if(!SetTimer(hwnd,EXIT_CLEANUP_WATCHDOG_TIMER_ID');
  if(timer<0||body.indexOf('    if(!SetTimer(hwnd,EXIT_CLEANUP_WATCHDOG_TIMER_ID',timer+1)>=0)throw Error('PS_EXIT_WATCHDOG_SCOPE_DRIFT');
  return port(body.slice(0,timer)+'}', '(void)tryStartExitCleanupWorker(hwnd);','fixturePsExitWorkerRequested=true;');
 });
 transform('exitCleanupThreadProc',body=>{
  for(const leaf of ['g_optiCatalogStartup.stop();','g_autoCloseWait.requestStop();','recoverySnapSetFanHandoffPending(true);',
   'fanLeaseKeepaliveShutdown();','recoverySnapSetFanHandoffPending(false);','steamMenuGameShutdown();',
   'physicalFeedbackExitCleanup(true);','oemVendorStackRestore("app-exit");','msiNativeForegroundExitCleanup();',
   'externalControllerPriorityShutdown();','physicalFeedbackExitCleanup(false);','g_steamCommandCoordinator.stop();',
   'g_steamSettingsObserver.finishFactReadsForExit();'])body=port(body,leaf);
  body=port(body,'const auto fanExitHandoff = fanHostCleanupForAppExit();','(void)0;');
  body=removeBetween(body,'    appendFanLifecycleLog("fan-exit-handoff-result",','    // DaVe');
  body=port(body,'{"fanHandoffFirst", true},','{"fanHandoffFirst", nullptr},');
  body=removeBetween(body,'    oemBackMappingSetEnabled(false);','    g_exitCleanupStage.store("input-local-return"');
  body=removeBetween(body,'    g_exitCleanupStage.store("media-downloads-return"','    g_exitCleanupStage.store("shared-power-transport-stop"');
  body=port(body,'{"recoveryWorkerStopped",steamOriginalRecoveryStopped()}','{"recoveryWorkerStopped",nullptr}');
  return port(body,'postExitReadyOnce(hwnd, 0);','(void)hwnd;');
 });
 if(paired){
  if(currentReceipt)transform('psWakeCaptureEndpointOwnership',body=>port(body,guardedQuery,
   query+' && (fixturePsExitSkipPostProbeAdmission || current())) {'));
  transform('inputHostPerformPsWakeRearm',body=>{
   body=port(body,'if (!probeOwnerCurrent()) {','if (!fixturePsExitSkipPostProbeAdmission && !probeOwnerCurrent()) {');
   const pause='if (probeAdmissionPaused()) {\n        ULONGLONG delay = 0;';
   return port(body,pause,pause.replace('if (','if (!fixturePsExitSkipPostProbeAdmission && '));
  });
 }
 if(currentReceipt)text='#define YMCC_FIXTURE_PS_EXIT_RECEIPT_RECHECK 1\n'+text;
 return {text,receipts};
}
