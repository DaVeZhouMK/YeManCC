import assert from 'node:assert/strict';
import test from 'node:test';
import {inspectGpdStopReceipt} from './gpd_stop_receipt.mjs';

function sample(terminal = 'off') {
  const stopReason = terminal === 'off' ? 'virtual-target-disabled' : 'shutdown';
  const owner = {hostPid: 112, epoch: 2, runId: 'owned-run', stopReason, targetMayExist: true};
  return {name: 'GPD/first-on-expired-full-cycle/' + terminal, terminal,
    hostStart: {pid: owner.hostPid, epoch: owner.epoch, runId: owner.runId},
    managedReceipt: {processId: owner.hostPid, nativeObserverCloseSucceeded: true,
      stateAfterFinalCleanup: {tuple: {Epoch: owner.epoch, RunId: owner.runId}}},
    nativeEvents: [
      {event: 'input-host-stop-request', detail: {...owner, hadProcess: true}},
      {event: 'input-host-stop-result', detail: {...owner, hostExitConfirmed: true, localReleaseAck: true,
        quarantined: false, visibilityRestored: true, releaseDisposition: 'local-dispose-complete/external-release-unproven',
        physicalReturnObserved: 'UNKNOWN', externalAccess: 'unverified'}},
    ]};
}

test('OFF and exit prove local release only and can coexist with an unbound repeat', () => {
  for (const terminal of ['off', 'exit']) {
    const row = sample(terminal);
    row.nativeEvents.push({event: 'input-host-stop-result', detail: {hostPid: 0, epoch: 0, runId: 'UNBOUND'}});
    const receipt = inspectGpdStopReceipt(row);
    assert.equal(receipt.accepted, true);
    assert.equal(receipt.physicalReturnObserved, 'UNKNOWN');
    assert.equal(receipt.externalAccess, 'unverified');
    assert.equal(receipt.resultIndex, 1);
  }
});

test('a wrong PID, epoch or run cannot substitute for the old owner', () => {
  for (const [key, value] of [['hostPid', 113], ['epoch', 3], ['runId', 'later-run']]) {
    const row = sample();
    row.nativeEvents[1].detail[key] = value;
    assert.equal(inspectGpdStopReceipt(row).issue, 'missing-duplicate-or-unordered-owned-stop', key);
  }
});

test('missing, duplicate or reversed owned Stop events are rejected', () => {
  for (const alter of [events => events.shift(), events => events.push(events[1]), events => events.reverse()]) {
    const row = sample();
    alter(row.nativeEvents);
    assert.equal(inspectGpdStopReceipt(row).issue, 'missing-duplicate-or-unordered-owned-stop');
  }
});

test('a Stop for another intent or without the old target is not the selected terminal', () => {
  for (const result of [{stopReason: 'persona-transition'}, {targetMayExist: false}]) {
    const row = sample('exit');
    Object.assign(row.nativeEvents[1].detail, result);
    assert.equal(inspectGpdStopReceipt(row).issue, 'wrong-stop-reason-or-target');
  }
});

test('missing ACK, exit or quarantine settlement cannot prove local release', () => {
  for (const result of [{localReleaseAck: false}, {hostExitConfirmed: false}, {quarantined: true}]) {
    const row = sample();
    Object.assign(row.nativeEvents[1].detail, result);
    assert.equal(inspectGpdStopReceipt(row).issue, 'local-host-release-unconfirmed');
  }
});

test('a visibility restore or a local ACK cannot upgrade external return', () => {
  for (const result of [{physicalReturnObserved: 'PROVEN'}, {physicalReturnObserved: undefined},
    {externalAccess: 'verified'}, {releaseDisposition: 'local-dispose-complete/external-release-proven'}]) {
    const row = sample();
    Object.assign(row.nativeEvents[1].detail, result);
    assert.equal(inspectGpdStopReceipt(row).issue, 'external-return-scope-mismatch');
  }
});

test('the finalized managed receipt must belong to the stopped owner for OFF and exit', () => {
  for (const terminal of ['off', 'exit']) {
    const changes = [
      managed => { managed.processId += 1; },
      managed => { managed.stateAfterFinalCleanup.tuple.Epoch += 1; },
      managed => { managed.stateAfterFinalCleanup.tuple.RunId = 'later-run'; },
      managed => { delete managed.stateAfterFinalCleanup.tuple; },
    ];
    for (const change of changes) {
      const row = sample(terminal);
      change(row.managedReceipt);
      assert.equal(inspectGpdStopReceipt(row).issue, 'managed-release-owner-mismatch');
    }
  }
});

test('a same-owner managed receipt cannot hide a failed or missing observer close', () => {
  for (const terminal of ['off', 'exit']) {
    for (const close of [false, undefined]) {
      const row = sample(terminal);
      row.managedReceipt.nativeObserverCloseSucceeded = close;
      assert.equal(inspectGpdStopReceipt(row).issue, 'native-observer-close-unconfirmed');
    }
  }
});
