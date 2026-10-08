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
    expect(virtualOutputPresentation({ ...running, [key]: false }, true, persona).tone === 'warning', `missing ${key} cannot be green`);
  }
  expect(virtualOutputPresentation({ ...running, suppressionState: 'degraded' }, true, persona).text === '虚拟手柄-疑似失败', 'known suppression anomaly');
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
  expect(view.text==='虚拟手柄-疑似失败' && view.tone==='warning' && view.detail.length>0, reason);
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
    expect(virtualOutputPresentation({ ...running, state: 'suspected-failed', steamRunning }, true, persona).text === '虚拟手柄-疑似失败', 'backend failure still reported when Steam absent not proven');
  }
  expect(virtualOutputPresentation({ ...running, steamRunning: false }, false, persona).tone === 'off', 'disabled Steam modes stay off');
  expect(virtualOutputPresentation({ ...running, steamRunning: false }, true, persona, true).tone === 'pending', 'manual transition stays pending until selected mode confirmed');
  expect(virtualOutputPresentation({ requestedPersona: persona, steamRunning: false }, true, persona).tone === 'warning', 'confirmed absent Steam does not require a full backend receipt');
}
const xbox = { ...active, requestedPersona: 'elite', boundPersona: 'elite', steamRunning: false };
expect(virtualOutputPresentation(xbox, true, 'elite').tone === 'on', 'Xbox does not require Steam');
expect(virtualOutputPresentation({ ...xbox, state: 'suspected-failed' }, true, 'elite').text === '虚拟手柄-疑似失败', 'Xbox backend failure not mislabeled Steam');
expect(virtualOutputPresentation({ ...active, steamRunning: false }, true, 'disabled').tone === 'off', 'disabled persona does not require Steam');
expect(virtualOutputPresentation(null, true, 'dualsense-edge').tone === 'pending', 'no telemetry does not prove Steam stopped');
console.log(`VIRTUAL_OUTPUT_PRESENTATION_PASS checks=${checks} hardwareOperations=0`);
