// Test-only FanView presentation fixture. No Host, owner, request or timer mutation.
(window as any).__fanStatusAudit = {
  async set(value: { kind: string; detail?: string }) {
    const owner = () => ({ state: fanHostLifecycle.state, active: fanHostLifecycle.recoveryActive,
      attention: fanHostLifecycle.recoveryNeedsAttention, hasIntent: fanHostLifecycle.hasControlIntent,
      revision: fanHostLifecycle.controlIntentRevision, pid: fanHostLifecycle.processId });
    const ownerBefore = owner();
    connecting.value = false; busy.value = false; closing.value = false;
    recovering.value = false; controlActive.value = false; hardwareControlConfirmed.value = false;
    supported.value = true; controlReady.value = true; hostState.value = 'awaiting-control';
    curveSaveFailure.value = null; statusMessage.value = '风扇控制未开启';
    if (value.kind === 'connecting') { connecting.value = true; busy.value = true; hostState.value = 'handshaking'; }
    else if (value.kind === 'recovering') { recovering.value = true; controlActive.value = true; hostState.value = 'awaiting-control'; }
    else if (value.kind === 'attention') { hostState.value = 'fault-locked'; statusMessage.value = '风扇自动恢复未完成，请点击重试'; }
    else if (value.kind === 'device-gate-long') { supported.value = false; controlReady.value = false; hostState.value = 'unknown'; statusMessage.value = value.detail!; }
    else if (value.kind === 'save-failure') curveSaveFailure.value = value.detail!;
    else if (value.kind === 'message') statusMessage.value = value.detail!;
    else if (value.kind !== 'idle') throw new Error('Unknown fan presentation fixture');
    await nextTick();
    return {kind:value.kind,detail:statusText.value,label:statusLabel.value,buttonText:controlText.value,
      ownerBefore,ownerAfter:owner(),scope:'real FanView presentation refs/computed/template/style; no lifecycle or OEM execution'};
  },
};
