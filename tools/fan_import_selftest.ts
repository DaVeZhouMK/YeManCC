import { createFanApiAdapter, DisabledFanApiAdapter } from '../src/bridge/fanApi';
import { FAN_IMPORT_ENABLED } from '../src/bridge/fanFeature';
import { FAN_REAL_HOST_ENABLED, FanHostLifecycle, fanHostLifecycle } from '../src/bridge/fanHost';

const adapter = createFanApiAdapter();
if (!FAN_IMPORT_ENABLED || !FAN_REAL_HOST_ENABLED) throw new Error('formal Fan gates are not enabled');
const disabled = new FanHostLifecycle({ enabled: false });
if (fanHostLifecycle.state === 'disabled') throw new Error('formal Fan Host singleton is still disabled');
if (!(adapter instanceof DisabledFanApiAdapter) || adapter.enabled) {
  throw new Error('explicitly disabled Fan adapter is not fail-closed');
}
const handshake = await adapter.handshake();
if (handshake.ok || handshake.supported) throw new Error('disabled adapter reported support');
const state = await adapter.getState();
if (state.hardwareWrites !== false || state.state !== 'Disabled') {
  throw new Error('disabled adapter reported an unsafe state');
}
const gate = await disabled.start();
if (gate.allowed || disabled.state !== 'disabled' || disabled.processId !== null) {
  throw new Error('explicitly disabled lifecycle attempted to start a Host');
}
console.log('fan import selftest: PASS (formal gates on; explicit disabled path remains fail-closed)');
