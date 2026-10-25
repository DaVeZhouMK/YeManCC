/** V2 checks actual typed-worker ownership. V1 same-process WinHTTP natural
 * library-zero is a separate diagnostic and is never promoted by this parser.
 * Memory validation is not source/tool/disk acceptance. */
import assert from 'node:assert/strict';
import {TYPED_ASYNC_ORDERED_IDS} from './typed-async-parent.mjs';
const KINDS=['http-silent','ws-silent'],STOPS=['shutdown','fatal','client-harvest'];
const ROWS=STOPS.flatMap(stop=>KINDS.map(kind=>stop+'.'+kind));
const fail=label=>'TYPED_OWNED_ASYNC_'+label;
const integer=(value,label,minimum=0)=>assert(Number.isSafeInteger(value)&&value>=minimum,label);
const bool=(value,label)=>assert.equal(typeof value,'boolean',label);
const key=value=>value.replaceAll('\\','/').toLowerCase();
const samePath=(actual,expected,label)=>assert.equal(key(actual),key(expected),label);
const destroyed=s=>s?.fullyDestroyed===true&&s.childExited===true&&['handles','pendingIo','activeCalls','jobActiveProcesses','ownerHandles','ownerPendingIo','readerHandles','readerPendingIo','clientHandles','processId'].every(name=>s[name]===0);
function control(row,kind,mode,{executablePath,observations,nonces,parentIdentity}){
  assert.equal(row.kind,kind);assert.equal(row.retentionNegative,mode==='retain');
  assert.equal(row.deadlineMs,mode==='renew'?700:1500);assert.equal(row.callbackCloseDeadlineMs,1500);
  integer(row.elapsedMs,fail('DIRECT_ELAPSED'));bool(row.directConsumerPassed,fail('DIRECT_COMPONENT_VERDICT'));bool(row.passed,fail('DIRECT_VERDICT'));
  const body=row.accepted===true&&row.reason==='mirror-bootstrap-canceled'&&row.elapsedMs<row.deadlineMs&&row.callbackOwnersReleased===true&&row.freshUncanceledToken===true&&row.lateOldCallbacksDidNotAffectActualNewRequest===true&&row.atExit.registrations>0&&row.atExit.handleClosingCallbacks===row.atExit.registrations&&row.atExit.callbacksAfterOffStarted>0&&(mode!=='retain'||row.originalClosedPredicateWhileRetained===false);
  assert.equal(row.directConsumerPassed,body,fail('DIRECT_BODY_REPORT'));
  const p=row.ownership;assert.equal(p?.schema,'ymcc.owned-async-lifetime.v2');assert.match(p.nonce,/^[a-f0-9]{32}$/);assert(!nonces.has(p.nonce),fail('REUSED_NONCE'));nonces.add(p.nonce);
  assert.equal(key(p.parent.path),key(executablePath));assert.equal(key(p.child.path),key(executablePath));
  for(const identity of [p.parent,p.child]){integer(identity.pid,fail('PID'),1);assert.match(identity.birth,/^[1-9][0-9]{1,19}$/);integer(identity.session,fail('WINDOWS_SESSION'));}
  assert.notEqual(p.parent.pid,p.child.pid);assert.equal(p.parent.session,p.child.session);
  if(parentIdentity.value)assert.deepEqual(p.parent,parentIdentity.value,fail('PARENT_IDENTITY_CHANGED'));else parentIdentity.value=p.parent;
  for(const field of ['startedAt','offAt','callbackObservedAt','callbackReleasedAt','replyObservedAt','exitObservedAt','cleanupFinishedAt'])integer(p[field],fail('CLOCK:'+field));
  assert(p.startedAt<=p.offAt&&p.offAt<=p.callbackObservedAt&&p.callbackObservedAt<=p.replyObservedAt&&p.replyObservedAt<=p.exitObservedAt&&p.exitObservedAt<=p.cleanupFinishedAt,fail('CLOCK_ORDER'));
  assert.equal(p.callbackReleaseObserved,row.callbackOwnersReleased,fail('CALLBACK_RELEASE_OBSERVATION'));
  assert.equal(p.callbackReleasedAt,row.callbackOwnersReleased?p.callbackObservedAt:0,fail('CALLBACK_RELEASE_TIME'));
  assert.equal(p.resourceDeadlineMs,1500,fail('RESOURCE_DEADLINE_UNCHANGED'));
  for(const field of ['aliveWhileCallbackProofRead','naturalExit','zeroOwnedSubtree','strictParentResourcesEqual','withinOriginalOffDeadline'])bool(p[field],fail('OWNERSHIP_BOOLEAN:'+field));
  for(const field of ['parentHandlesBefore','parentHandlesAfter','parentThreadsBefore','parentThreadsAfter'])integer(p[field],fail('PARENT_COUNTER:'+field));
  const zero=p.afterOwnerClose?.fullyDestroyed===true&&p.afterOwnerClose.childExited===true&&p.afterOwnerClose.cleanupError===0&&p.afterOwnerClose.jobTerminationAttempted===false&&['handles','activeCalls','pendingIo','jobActiveProcesses'].every(name=>p.afterOwnerClose[name]===0);
  assert.equal(p.zeroOwnedSubtree,zero,fail('ZERO_SUBTREE_REPORT'));
  assert.equal(p.strictParentResourcesEqual,p.parentHandlesBefore===p.parentHandlesAfter&&p.parentThreadsBefore===p.parentThreadsAfter,fail('STRICT_PARENT_REPORT'));
  assert.equal(p.withinOriginalOffDeadline,p.cleanupFinishedAt-p.offAt<1500,fail('STRICT_DEADLINE_REPORT'));
  const observed=observations.get(p.nonce);assert(observed,fail('MISSING_CHILD_RAW'));
  assert.equal(observed.schema,'ymcc.owned-async-result.v2');assert.equal(observed.id,p.nonce);assert.deepEqual(observed.identity,p.child,fail('CHILD_RAW_IDENTITY'));
  for(const field of ['offAt','callbackReleasedAt','callbackObservedAt'])assert.equal(observed[field],p[field],fail('CHILD_RAW_CLOCK:'+field));
  const {ownership,directConsumerPassed,...bodyReported}=row;void ownership;void directConsumerPassed;
  bodyReported.passed=row.directConsumerPassed;assert.deepEqual(bodyReported,observed.row,fail('CHILD_RAW_BODY_CHANGED'));
  const okay=body&&p.aliveWhileCallbackProofRead&&p.naturalExit&&p.exitCode===0&&p.afterOwnerClose.exitCode===0&&zero&&p.strictParentResourcesEqual&&p.withinOriginalOffDeadline&&p.exitObservedAt-p.offAt<1500;
  assert.equal(row.passed,okay,fail('CONTROL_REPORTED_VERDICT'));assert(okay,fail('CONTROL_FAILED:'+row.id));
  return row;
}
function peerCalibration(rows){
  assert.deepEqual(rows.map(r=>r.index),[0,1]);
  for(const [i,row]of rows.entries()){
    for(const field of ['beforeHandles','afterHandles'])integer(row[field],fail('PEER_COUNTER'));
    assert.equal(row.client,'raw owned loopback socket; no WinHTTP');assert(Array.isArray(row.before)&&Array.isArray(row.after));
    assert.equal(row.before.length,row.beforeHandles);assert.equal(row.after.length,row.afterHandles);
    if(i===1){assert.equal(row.beforeHandles,row.afterHandles,fail('PEER_CALIBRATION_UNSTABLE'));continue;}
    const identity=h=>[h.handle,h.type,h.access].join(':');const previous=new Set(row.before.map(identity)),after=new Set(row.after.map(identity));
    assert(row.before.every(h=>after.has(identity(h))),fail('PEER_LOST_BASELINE_RESOURCE'));
    const added=row.after.filter(h=>!previous.has(identity(h))),delta=row.afterHandles-row.beforeHandles;
    assert.equal(added.length,delta);assert(delta===0||delta===11,fail('UNKNOWN_PEER_COLD_DELTA'));
    if(delta===11){assert.equal(added.filter(h=>h.type==='EtwRegistration').length,9);assert.equal(added.filter(h=>h.type==='Event').length,1);const files=added.filter(h=>h.type==='File');assert.equal(files.length,1);assert.equal(files[0].access,0x100001);assert.equal(files[0].objectName,'\\Device\\KsecDD');}
  }
}
export function inspectOwnedAsyncNative(raw,{executablePath,observations}={}){
  assert.equal(typeof executablePath,'string',fail('EXECUTABLE_AUTHORITY_REQUIRED'));assert(observations instanceof Map,fail('CHILD_RAW_AUTHORITY_REQUIRED'));
  assert.equal(raw.schema,'ymcc.typed-owned-async-request.v2');assert.equal(raw.productCallbackMode,'typed-only; legacy macro absent');
  for(const name of ['fullPersistentAcceptance','officialLoaderExecuted','physicalSleepPerformed','externalNetwork'])assert.equal(raw[name],false);
  assert.equal(raw.realSteamActions,0);assert.equal(raw.noStopPolicyInterposition,true);assert.equal(raw.nativeBudgetMs,20000);assert.equal(raw.originalNativeBudgetMs,20000);
  assert.equal(raw.sameProcessLibraryNaturalZeroClaim,'NOT_ASSERTED_BY_TYPED_ISOLATION_SCOPE');
  integer(raw.elapsedMs,fail('NATIVE_ELAPSED'));assert(raw.elapsedMs<20000,fail('NATIVE20S'));assert(!raw.error,fail('NATIVE_ERROR'));
  assert.deepEqual(raw.originalTypedRows.map(r=>r.id),ROWS,fail('TYPED_MEMBERSHIP'));
  const failures = [], ordered = [];
  for (const [index, row] of raw.originalTypedRows.entries()) {
    assert(!row.error, fail('ROW_ERROR')); assert.equal(row.kind, KINDS[index % 2]); assert.equal(row.stop, STOPS[Math.floor(index / 2)]);
    assert.equal(row.deadlineMs, 1500); assert.equal(row.callbackCloseDeadlineMs, 1500); assert.equal(row.nativeBudgetMs, 20000);
    assert.deepEqual(row.originalAssertions.map(a => a.id), TYPED_ASYNC_ORDERED_IDS, fail('ORDERED_12:') + row.id);
    const a = row.originalAssertions, t = row.afterStopAsync, actual = a.map(x => x.actual);
    integer(actual[0], fail('LOADER_PID'), 1); bool(actual[1], fail('LOADER_HANDLE'));
    assert.equal(typeof actual[2], 'string'); samePath(actual[2], executablePath, fail('LOADER_IDENTITY'));
    for (const i of [3,5,7,8,9,11]) bool(actual[i], fail('ACTUAL_BOOLEAN'));
    integer(actual[4], fail('SHUTDOWN_MS')); integer(actual[10], fail('RUNTIME_PID'));
    assert.equal(actual[4], row.shutdownMs); assert.equal(actual[5], t.bindingReturned);
    assert.equal(actual[6], t.reason); assert.equal(actual[7], row.ownedLoaderExitObserved);
    assert.equal(actual[8], t.callbackOwnersReleasedObserved);
    const checks = [actual[0] > 0, actual[1], key(actual[2]) === key(executablePath), actual[3],
      row.shutdownMs < 1500, t.bindingReturned === true, t.reason === 'mirror-bootstrap-canceled',
      row.ownedLoaderExitObserved === true, t.callbackOwnersReleasedObserved === true,
      actual[9] === false, actual[10] === 0, actual[11] === true];
    a.forEach((condition, i) => {
      assert.equal(condition.passed, checks[i], fail('RECONSTRUCTED_CONDITION:') + row.id + '/' + condition.id);
      assert.equal(condition.status, checks[i] ? 'PASS' : 'FAIL', fail('CONDITION_STATUS'));
    });
    const originalPredicate = checks.every(Boolean);
    const independent = row.committedRefresh === true && row.beforeOff.pendingIo > 0 && row.beforeOff.processId > 0 &&
      row.beforeOff.jobActiveProcesses > 0 && row.activeAsync.weakCount === (row.kind === 'ws-silent' ? 2 : 1) &&
      row.activeAsync.registrations > 0 && destroyed(row.atExit) && checks[11] &&
      row.latePlansBefore === row.latePlansAfter && row.lateCreatesBefore === row.lateCreatesAfter &&
      row.cefUnaffected === true && row.workerExitObserved === true && t.unexpectedError === 0 &&
      (row.stop !== 'fatal' || row.faultBeforeLate === 'decky-render-failed');
    assert.equal(row.currentResourceAndIdentityChecks, independent, fail('RESOURCE_REPORT'));
    assert(independent, fail('RESOURCE_OR_IDENTITY_FAILED'));
    assert.equal(row.originalPredicate, originalPredicate, fail('ORIGINAL_PREDICATE'));
    assert.equal(row.passed, originalPredicate && independent, fail('TYPED_ROW_VERDICT'));
    integer(row.workerJobForceCalls, fail('JOB_FORCE_COUNT'));
    if (row.passed) {
      assert.equal(t.weakLive, 0); assert.equal(t.handleClosingCallbacks, t.registrations);
      assert(t.callbacksAfterOffStarted > 0, fail('REAL_CALLBACK_OBSERVATION_REQUIRED'));
    }
    const failedOriginalAssertions = checks.flatMap((okay, i) => okay ? [] : [TYPED_ASYNC_ORDERED_IDS[i]]);
    if (!row.passed) failures.push({id:row.id, failedOriginalAssertions,
      workerJobForceCalls:row.workerJobForceCalls, afterStopAsync:t});
    ordered.push({id:row.id, conditions:a.map((condition, i) => ({ordinal:i+1, id:condition.id, passed:checks[i], actual:condition.actual}))});
  }

  peerCalibration(raw.parentPeerCalibration);
  assert.deepEqual(raw.coldContextCalibration.map(r=>r.index),[0,1]);assert.equal(raw.coldContextCalibration[1].beforeHandles,raw.coldContextCalibration[1].afterHandles);
  const coldDelta=raw.coldContextCalibration[0].afterHandles-raw.coldContextCalibration[0].beforeHandles;assert(coldDelta===0||coldDelta===2,fail('UNKNOWN_CONTEXT_COLD_DELTA'));
  assert.deepEqual(raw.ownedCalibration.map(r=>r.index),[0,1]);
  const nonces=new Set(),parentIdentity={value:null},options={executablePath,observations,nonces,parentIdentity};
  let calibration=true;
  for(const row of raw.ownedCalibration){
    for(const field of ['beforeHandles','afterHandles'])integer(row[field],fail('CALIBRATION_COUNTER'));
    assert.deepEqual(row.probes.map(r=>r.id),KINDS.map(k=>'current.direct-real-async-cancel.'+k));
    row.probes.forEach((probe,i)=>control(probe,KINDS[i],'normal',options));calibration=calibration&&row.beforeHandles===row.afterHandles;
  }
  assert.deepEqual(raw.directAsyncRequestConsumers.map(r=>r.id),KINDS.map(k=>'current.direct-real-async-cancel.'+k));
  assert.deepEqual(raw.negativeControls.map(r=>r.id),KINDS.flatMap(k=>['negative.old-cancel-and-late-callback-isolation.'+k,'negative.retained-callback-owner.'+k]));
  raw.directAsyncRequestConsumers.forEach((row,i)=>control(row,KINDS[i],'normal',options));
  raw.negativeControls.forEach((row,i)=>control(row,KINDS[Math.floor(i/2)],i%2?'retain':'renew',options));
  assert.equal(nonces.size,10);assert.equal(observations.size,10,fail('EXTRA_OR_MISSING_CHILD_RAW'));
  for(const field of ['parentHandlesBefore','parentHandlesAfter','parentThreadsBefore','parentThreadsAfter'])integer(raw[field],fail('TOTAL_PARENT_COUNTER'));
  const strictHandles=raw.parentHandlesBefore===raw.parentHandlesAfter,strictThreads=raw.parentThreadsBefore===raw.parentThreadsAfter;
  assert.equal(raw.strictParentHandlesEqual,strictHandles,fail('TOTAL_HANDLE_REPORT'));assert.equal(raw.strictParentThreadsEqual,strictThreads,fail('TOTAL_THREAD_REPORT'));
  assert.equal(raw.ownedCalibrationPassed,calibration,fail('OWNED_CALIBRATION_REPORT'));
  assert.equal(raw.coldContextCalibration[1].afterHandles,raw.ownedCalibration[0].beforeHandles);assert.equal(raw.ownedCalibration[1].afterHandles,raw.parentHandlesBefore);
  for(const row of raw.originalTypedRows){assert.equal(row.parentObservedBefore,raw.parentHandlesBefore,fail('TYPED_PARENT_DRIFT'));assert.equal(row.parentObservedAfter,raw.parentHandlesBefore,fail('TYPED_PARENT_DRIFT'));}
  const passed=failures.length===0&&calibration&&strictHandles&&strictThreads;assert.equal(raw.passed,passed,fail('NATIVE_VERDICT'));
  return {passed,ordered,summary:{typedRows:6,typedPassed:6-failures.length,typedFailed:failures.length,originalPredicatesPerRow:12,directActualAsyncConsumers:2,negativeControls:4,ownedDirectChildren:10,ownedCalibrationPassed:calibration,strictParentHandlesEqual:strictHandles,strictParentThreadsEqual:strictThreads,failures,fullPersistentAcceptance:false,sameProcessLibraryNaturalZeroClaim:'NOT_ASSERTED_BY_TYPED_ISOLATION_SCOPE'}};
}
export function validateOwnedAsyncNative(raw,options){const result=inspectOwnedAsyncNative(raw,options);assert.equal(result.passed,true,fail('OWNED_LIFECYCLE_NOT_PASS'));return result.summary;}
