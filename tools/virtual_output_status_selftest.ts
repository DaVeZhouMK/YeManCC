import { virtualOutputPresentation, type VirtualOutputRuntimeState } from '../src/bridge/virtualOutputStatus';
let checks = 0;
function expect(value: boolean, reason: string) { if (!value) throw new Error(reason); ++checks; }
const active: VirtualOutputRuntimeState = {
  state: 'active', reason: 'current-session-frame-accepted', requestedPersona: 'steamdeck',
  boundPersona: 'steamdeck', hostAlive: true, prepared: true, neutralized: true,
  frameAccepted: true, sourceLive: true, suppressionState: 'verified',
};
for (const persona of ['steamdeck', 'dualsense-edge', 'elite']) {
  const running = { ...active, requestedPersona: persona, boundPersona: persona };
  expect(virtualOutputPresentation(running, true, persona).text === '虚拟手柄-成功开启', 'current persona running');
  for (const key of ['hostAlive', 'prepared', 'neutralized', 'frameAccepted', 'sourceLive'] as const) {
    expect(virtualOutputPresentation({ ...running, [key]: false }, true, persona).tone === (key === 'sourceLive' || key === 'frameAccepted' ? 'pending' : 'warning'), `missing ${key} cannot be green`);
  }
  expect(virtualOutputPresentation({ ...running, suppressionState: 'degraded' }, true, persona).text === '虚拟手柄-已开启，隔离异常', 'known suppression anomaly');
  expect(virtualOutputPresentation({ ...running, suppressionState: 'unknown' }, true, persona).tone === 'on', 'local output success does not invent a suppression failure');
  expect(virtualOutputPresentation({ ...running, boundPersona: 'other' }, true, persona).tone === 'warning', 'actual persona mismatch');
  expect(virtualOutputPresentation(running, true, persona, true).text === '虚拟手柄-正在切换', 'busy transition');
  expect(virtualOutputPresentation(running, false, persona).text === '虚拟手柄-未开启', 'disabled config');
}
expect(virtualOutputPresentation(null, true, 'steamdeck').tone === 'pending', 'saved config alone not success');
expect(virtualOutputPresentation(active, true, 'elite').text === '虚拟手柄-正在切换', 'old persona receipt not new success');
expect(virtualOutputPresentation(active, true, 'disabled').text === '虚拟手柄-未开启', 'local input mode');
for (const reason of ['backend-fault-hold','physical-fallback','transport-fault','host-not-alive','persona-mismatch','target-not-admitted','suppression-degraded','physical-source-unavailable']) {
  const view=virtualOutputPresentation({ ...active, state:'suspected-failed', reason }, true,'steamdeck');
  expect(view.tone === (reason === 'physical-source-unavailable' ? 'pending' : 'warning') && view.detail.length>0, reason);
}
for (const state of ['starting','recovering','unknown','future-status']) {
  expect(virtualOutputPresentation({ ...active,state },true,'steamdeck').tone==='pending', 'transitional '+state);
}
// Steam presence is independent of backend frame acceptance. Both current HID
// personas must warn even when the host accepted frames, without changing Xbox.
for (const persona of ['steamdeck', 'dualsense-edge']) {
  const running = { ...active, requestedPersona: persona, boundPersona: persona };
  for (const state of ['active', 'suspected-failed', 'starting', 'recovering', 'unknown', 'future-status']) {
    const view = virtualOutputPresentation({ ...running, state, steamRunning: false }, true, persona);
    expect(view.text === '虚拟手柄-Steam未开启实际无效' && view.tone === 'warning', `${persona} needs Steam (${state})`);
    expect(view.detail.includes(persona === 'steamdeck' ? 'SteamDeck' : 'PS5'), 'mode-specific actionable detail');
  }
  for (const steamRunning of [true, null, undefined]) {
    expect(virtualOutputPresentation({ ...running, steamRunning }, true, persona).tone === 'on', 'Steam running or unknown does not invent missing Steam');
    expect(virtualOutputPresentation({ ...running, state: 'suspected-failed', steamRunning }, true, persona).text === '虚拟手柄-失败', 'backend failure still reported when Steam absent not proven');
  }
  expect(virtualOutputPresentation({ ...running, steamRunning: false }, false, persona).tone === 'off', 'disabled Steam modes stay off');
  expect(virtualOutputPresentation({ ...running, steamRunning: false }, true, persona, true).tone === 'pending', 'manual transition stays pending until selected mode confirmed');
  expect(virtualOutputPresentation({ requestedPersona: persona, steamRunning: false }, true, persona).tone === 'warning', 'confirmed absent Steam does not require a full backend receipt');
}
const xbox = { ...active, requestedPersona: 'elite', boundPersona: 'elite', steamRunning: false };
expect(virtualOutputPresentation(xbox, true, 'elite').tone === 'on', 'Xbox does not require Steam');
expect(virtualOutputPresentation({ ...xbox, state: 'suspected-failed' }, true, 'elite').text === '虚拟手柄-失败', 'Xbox backend failure not mislabeled Steam');
expect(virtualOutputPresentation({ ...active, steamRunning: false }, true, 'disabled').tone === 'off', 'disabled persona does not require Steam');
expect(virtualOutputPresentation(null, true, 'dualsense-edge').tone === 'pending', 'no telemetry does not prove Steam stopped');
// Backend transactions may outlive a settings write, including switches between
// generations of the same persona. Pending ends only with backend evidence.
const transientReasons = ['transport-fault', 'host-not-alive', 'persona-mismatch',
  'target-not-admitted', 'suppression-degraded', 'physical-source-unavailable'];
for (const persona of ['steamdeck', 'dualsense-edge', 'elite']) {
  const running = { ...active, requestedPersona: persona, boundPersona: persona };
  for (const reason of transientReasons) {
    const receipt = { ...running, state: 'suspected-failed', reason, switchPending: true };
    const pending = virtualOutputPresentation(receipt, true, persona);
    expect(pending.tone === 'pending', persona + ' pending ' + reason);
    expect(virtualOutputPresentation({ ...receipt, switchPending: false }, true, persona).tone === (reason === 'physical-source-unavailable' ? 'pending' : 'warning'),
      persona + ' settled failure remains visible ' + reason);
  }
  for (const key of ['hostAlive', 'prepared', 'neutralized', 'frameAccepted', 'sourceLive'] as const) {
    const receipt = { ...running, [key]: false, switchPending: true };
    expect(virtualOutputPresentation(receipt, true, persona).tone === 'pending', 'partial receipt while switching ' + key);
    expect(virtualOutputPresentation({ ...receipt, switchPending: false }, true, persona).tone === (key === 'sourceLive' || key === 'frameAccepted' ? 'pending' : 'warning'),
      'partial receipt after transaction ends ' + key);
  }
  expect(virtualOutputPresentation({ ...running, switchPending: true }, true, persona).tone === 'pending',
    'same persona receipt cannot settle a new generation');
  expect(virtualOutputPresentation({ ...running, state: 'unknown', switchPending: true }, true, persona).tone === 'pending',
    'pending before a host receipt exists');
  for (const steamRunning of [true, false, null, undefined]) {
    expect(virtualOutputPresentation({ ...running, switchPending: true, steamRunning }, true, persona).tone === 'pending',
      'backend switch precedes independent Steam observation');
    expect(virtualOutputPresentation({ ...running, state: 'recovering', switchPending: true, steamRunning }, true, persona).text === '虚拟手柄-正在恢复',
      'backend recovery stays recovery during a switch');
  }
  for (const reason of ['backend-fault-hold', 'physical-fallback']) {
    const view = virtualOutputPresentation({ ...running, state: 'suspected-failed', reason, switchPending: true }, true, persona);
    expect(view.text === '虚拟手柄-失败' && view.tone === 'warning' && view.detail.length > 0,
      'current terminal verdict wins over contradictory pending ' + reason);
  }
  expect(virtualOutputPresentation({ ...running, physicalReturnPending: true, physicalReturnFailed: true }, true, persona).tone === 'on',
    'stale OFF-only return facts cannot mask an enabled receipt');
  expect(virtualOutputPresentation({ ...running, state: 'suspected-failed', reason: 'transport-fault', physicalReturnFailed: true }, true, persona).text === '虚拟手柄-失败',
    'OFF-only return failure does not replace enabled backend failures');
}
// An old target fault is not the result of a newly selected target or local save.
const oldFault = { ...active, state: 'suspected-failed', reason: 'backend-fault-hold', switchPending: true };
expect(virtualOutputPresentation(oldFault, true, 'elite').text === '虚拟手柄-正在切换', 'old target terminal receipt stays stale');
expect(virtualOutputPresentation(oldFault, true, 'steamdeck', true).text === '虚拟手柄-正在切换', 'local save still awaits its own receipt');
for (const persona of ['steamdeck', 'dualsense-edge']) {
  const running = { ...active, requestedPersona: persona, boundPersona: persona, steamRunning: false };
  expect(virtualOutputPresentation({ ...running, switchPending: false }, true, persona).text === '虚拟手柄-Steam未开启实际无效',
    'Steam stopped regains its independent meaning when switch settles');
}

const external = { enabled: true, state: 'active', reason: 'verified', externalPresent: true,
  builtinHidden: true, virtualSuspended: true };
for (const [enabled, persona] of [[false, 'steamdeck'], [true, 'disabled'], [false, 'disabled']] as const) {
  const off = { ...oldFault, requestedPersona: 'old-mode', steamRunning: false };
  for (const physicalReturnFailed of [false, true]) {
    const view = virtualOutputPresentation({ ...off, physicalReturnPending: true, physicalReturnFailed }, enabled, persona, true, external);
    expect(view.text === '本机手柄-正在恢复' && view.tone === 'pending' && view.detail.length > 0,
      'active OFF return wins over stale failure, busy, mismatch, switch and external priority');
  }
  const failed = virtualOutputPresentation({ ...off, physicalReturnPending: false, physicalReturnFailed: true }, enabled, persona, true, external);
  expect(failed.text === '本机手柄-恢复未确认' && failed.tone === 'warning' && failed.detail.length > 0,
    'unconfirmed OFF return remains visible after bounded work ends');
  for (const state of ['recovering', 'starting', 'suspected-failed', 'unknown', 'external-priority']) {
    expect(virtualOutputPresentation({ ...off, state, physicalReturnPending: false, physicalReturnFailed: false }, enabled, persona).text === '虚拟手柄-未开启',
      'unrelated legacy states do not invent physical return debt ' + state);
  }
  expect(virtualOutputPresentation({ physicalReturnPending: true }, enabled, persona).text === '本机手柄-正在恢复',
    'OFF recovery needs no virtual host receipt');
  expect(virtualOutputPresentation({ physicalReturnFailed: true }, enabled, persona).text === '本机手柄-恢复未确认',
    'OFF failure needs no virtual host receipt');
  expect(virtualOutputPresentation({ physicalReturnPending: false, physicalReturnFailed: false }, enabled, persona).detail === '当前未请求虚拟手柄。',
    'ordinary OFF detail remains unchanged');
  expect(virtualOutputPresentation(null, enabled, persona, false, external).detail === '外接手柄直通，本机物理手柄已隐藏；虚拟手柄保持未开启。',
    'ordinary OFF external priority detail remains unchanged');
}
// Replaying settled unconfirmed receipts cannot silently turn OFF. A later real
// matching event clears failure in the producer; the view adds no deadline.
const returnDebt: VirtualOutputRuntimeState = { state: 'recovering', reason: 'physical-return-pending',
  requestedPersona: 'disabled', physicalReturnPending: true, physicalReturnFailed: false };
expect(virtualOutputPresentation(returnDebt, false, 'disabled').text === '本机手柄-正在恢复', 'physical return begins');
returnDebt.state = 'suspected-failed'; returnDebt.reason = 'physical-return-unconfirmed';
returnDebt.physicalReturnPending = false; returnDebt.physicalReturnFailed = true;
for (let replay = 0; replay < 3; ++replay) {
  expect(virtualOutputPresentation(returnDebt, false, 'disabled').text === '本机手柄-恢复未确认', 'return failure persists ' + replay);
}
returnDebt.state = 'disabled'; returnDebt.reason = 'not-requested'; returnDebt.physicalReturnFailed = false;
expect(virtualOutputPresentation(returnDebt, false, 'disabled').text === '虚拟手柄-未开启', 'matching physical event clears return failure');

// Keep the original presentation assertions above, then exercise actual R3 receipts.
const legacyChecks = checks;
function expectPresentation(actual: ReturnType<typeof virtualOutputPresentation>, expected: ReturnType<typeof virtualOutputPresentation>, reason: string): void {
  expect(actual.text === expected.text && actual.detail === expected.detail && actual.tone === expected.tone, reason);
}
const inspectionPresentation: ReturnType<typeof virtualOutputPresentation> = {
  text: '虚拟手柄-检查中', tone: 'pending',
  detail: '本次切换检查尚未结束；等待当前设备与输入链路的最终核验结果。',
};
const successfulPresentation: ReturnType<typeof virtualOutputPresentation> = {
  text: '虚拟手柄-成功开启', tone: 'on',
  detail: '虚拟设备已创建，后端已接收当前手柄输入。Steam 中的设备数量和背键编辑仍以实际表现为准。',
};
for (const persona of ['steamdeck', 'dualsense-edge', 'elite']) {
  const running: VirtualOutputRuntimeState = { ...active, requestedPersona: persona, boundPersona: persona };
  const protection: NonNullable<VirtualOutputRuntimeState['startProtection']> = {
    locked: true, requestId: 81, phase: 'verifying', persona,
    remainingMs: 22000, elapsedMs: 8000, maxMs: 30000, settleMs: 10000,
    verificationPending: true, verificationRemainingMs: 10000, candidateOutcome: 'success',
  };
  const inspecting: VirtualOutputRuntimeState = { ...running, startProtection: protection };
  expectPresentation(virtualOutputPresentation(inspecting, true, persona), inspectionPresentation,
    persona + ' usable candidate remains under inspection, not final success');
  expectPresentation(virtualOutputPresentation({ ...running, startProtection: { ...protection, verificationPending: false } }, true, persona), inspectionPresentation,
    persona + ' verifying phase alone identifies the locked inspection');
  expectPresentation(virtualOutputPresentation({ ...running, startProtection: { ...protection, phase: 'pending' } }, true, persona), inspectionPresentation,
    persona + ' verificationPending alone identifies the locked inspection');
  const recovered: VirtualOutputRuntimeState = { ...running, startProtection: { ...protection, candidateOutcome: 'failure' } };
  expectPresentation(virtualOutputPresentation(recovered, true, persona), inspectionPresentation,
    persona + ' recovered actual source still awaits final despite candidate failure');
  expectPresentation(virtualOutputPresentation({ ...recovered, startProtection: {
    ...recovered.startProtection!, locked: false, phase: 'success', remainingMs: 0,
    verificationPending: false, verificationRemainingMs: 0,
  } }, true, persona), successfulPresentation, persona + ' native final success releases inspection presentation');
  expectPresentation(virtualOutputPresentation(running, true, persona), successfulPresentation,
    persona + ' legacy active receipt without protection keeps exact success presentation');
  expectPresentation(virtualOutputPresentation({ ...running, startProtection: { ...protection, locked: false } }, true, persona), successfulPresentation,
    persona + ' unlocked protection cannot keep active output under inspection');
  expectPresentation(virtualOutputPresentation({ ...inspecting, sourceLive: false }, true, persona),
    virtualOutputPresentation({ ...running, sourceLive: false }, true, persona), persona + ' incomplete active receipt keeps its existing warning');
  expectPresentation(virtualOutputPresentation(inspecting, true, persona, true),
    virtualOutputPresentation(running, true, persona, true), persona + ' busy switch keeps its existing waiting presentation');
  expectPresentation(virtualOutputPresentation({ ...inspecting, switchPending: true }, true, persona),
    virtualOutputPresentation({ ...running, switchPending: true }, true, persona), persona + ' backend switch keeps its existing waiting presentation');
  for (const reason of ['backend-fault-hold', 'physical-fallback']) {
    expectPresentation(virtualOutputPresentation({ ...inspecting, state: 'suspected-failed', reason, switchPending: true }, true, persona),
      virtualOutputPresentation({ ...running, state: 'suspected-failed', reason, switchPending: true }, true, persona),
      persona + ' inspection cannot mask existing terminal warning ' + reason);
  }
}

console.log(`VIRTUAL_OUTPUT_PRESENTATION_PASS checks=${checks} hardwareOperations=0 legacyChecks=${legacyChecks} inspectionChecks=${checks - legacyChecks}`);
