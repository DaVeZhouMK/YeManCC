export function inspectGpdStopReceipt(row, selection) {
  const settled = selection === 'settled-first-on';
  const terminal = row?.terminal;
  const stopReason = terminal === 'off' ? 'virtual-target-disabled' : terminal === 'exit' ? (settled ? 'app-exit' : 'shutdown') : null;
  const host = row?.hostStart;
  const fail = issue => ({accepted: false, issue});
  if (!stopReason || row.name !== (settled ? 'GPD/first-on-settled-full-cycle/' : 'GPD/first-on-expired-full-cycle/') + terminal ||
      !Number.isSafeInteger(host?.pid) || host.pid <= 0 || !Number.isSafeInteger(host.epoch) || host.epoch <= 0 ||
      typeof host.runId !== 'string' || !host.runId.trim() || host.runId === 'UNBOUND') return fail('invalid-terminal-owner');

  const requests = [], results = [];
  // An idempotent OFF can emit another Stop with an unbound tuple. It is not
  // evidence for the Host which owned the target.
  for (const [index, event] of (Array.isArray(row.nativeEvents) ? row.nativeEvents : []).entries()) {
    const detail = event?.detail;
    if (detail?.hostPid !== host.pid || detail.epoch !== host.epoch || detail.runId !== host.runId) continue;
    if (event.event === 'input-host-stop-request') requests.push({index, detail});
    if (event.event === 'input-host-stop-result') results.push({index, detail});
  }
  if (requests.length !== 1 || results.length !== 1 || requests[0].index >= results[0].index) return fail('missing-duplicate-or-unordered-owned-stop');
  const request = requests[0].detail, result = results[0].detail;
  if (request.stopReason !== stopReason || result.stopReason !== stopReason ||
      request.hadProcess !== true || request.targetMayExist !== true || result.targetMayExist !== true) return fail('wrong-stop-reason-or-target');
  if (result.hostExitConfirmed !== true || result.localReleaseAck !== true || result.quarantined !== false) return fail('local-host-release-unconfirmed');
  if (result.releaseDisposition !== 'local-dispose-complete/external-release-unproven' ||
      result.physicalReturnObserved !== 'UNKNOWN' || result.externalAccess !== 'unverified') return fail('external-return-scope-mismatch');
  const managed = row.managedReceipt;
  const finalized = managed?.stateAfterFinalCleanup?.tuple;
  if (managed?.processId !== host.pid || finalized?.Epoch !== host.epoch ||
      finalized?.RunId !== host.runId) return fail('managed-release-owner-mismatch');
  if (managed.nativeObserverCloseSucceeded !== true) return fail('native-observer-close-unconfirmed');
  return {accepted: true, stopReason, hostPid: host.pid, epoch: host.epoch, runId: host.runId,
    requestIndex: requests[0].index, resultIndex: results[0].index,
    hostExitConfirmed: true, localReleaseAck: true, physicalReturnObserved: result.physicalReturnObserved,
    externalAccess: result.externalAccess};
}
