// Pure fake fixtures only. Import does not run the export audit or touch devices.
import assert from 'node:assert/strict';
import { parseManifest, localEpoch, jsonRows, manifestWindow, ownedNativeRows, bindObserverRows,
  endpointRole, parsePnpReceipt, buildRuns, parseSteam, steamRunEvidence, observerSnapshots, producerWindowEvidence } from './msi_persona_export_evidence_20261012.mjs';
let tests = 0;
function test(name, fn) { fn(); tests++; console.log(`PASS ${name}`); }
const bias = -480;
const t = s => localEpoch(`2026-10-12T${s}`, bias);
const row = (data, line = 1, file = 'fake/native-lifecycle.log') => ({ data, line, file });
const manifest = parseManifest('processId=9844\ntimeZoneBiasMinutes=-480\nexportedAtLocal=2026-10-12T13:10:32');
const start = row({ event: 'start', pid: 9844, time: '2026-10-12T13:06:02.978' });
const window = manifestWindow(manifest, [start]);
const pnp = 'pnp-primary-roots=1;pnp-xbox-companion-roots=0;pnp-hid-children=1;pnp-steamdeck-roots=0;pnp-steamdeck-hid-children=0;pnp-started=true;' +
  'pnp-primary-root-ids=SWD\\HIDMAESTRO_VID_045E_PID_0B00&IG_00\\session[root\\HIDMaestro];pnp-xbox-companion-root-ids=;' +
  'pnp-hid-ids=HID\\HIDMAESTRO_VID_045E_PID_0B00&IG_00\\child[HID\\VID_045E&PID_0B00|HID\\HIDMaestro];pnp-steamdeck-root-ids=;pnp-steamdeck-hid-ids=';
const started = row({ event: 'input-host-started', pid: 9844, time: '2026-10-12T13:10:05.518', runId: 'elite-run', epoch: 4,
  persona: 'elite', builtProfile: 'xbox-elite-v2', vidPid: '045E:0B00' }, 2);
const prepared = row({ event: 'input-host-command-result', pid: 9844, time: '2026-10-12T13:10:05.509', runId: 'elite-run', epoch: 4,
  command: 'PREPARE_TARGET', ok: true, targetPnpAdmitted: true, targetPnpDetail: pnp, hostInstanceId: 'fresh-host' }, 3);
const fixtureRun = () => buildRuns([start, started, prepared], window)[0];
const steamBlock = (time, p, index, id = '045e 0b00') => `[2026-10-12 ${time}] Local Device Found\n  type: ${id}\n  path: ${p}\n  serial_number: - 0\n` +
  `[2026-10-12 ${time}] Product: Controller\n[2026-10-12 ${time}] !! Steam controller device opened for index ${index}.\n`;
const steam = text => parseSteam(text, 'fake/controller.txt', bias);

test('manifest numeric hash suffix and BOM survive', () => assert.equal(parseManifest('\uFEFFnativeSha256=F\nhostDllSha256=H').hostDllSha256, 'H'));
test('UTC conversion uses manifest Windows bias, not host timezone', () => assert.equal(t('13:10:05'), Date.parse('2026-10-12T05:10:05Z')));
test('invalid local timestamp rejected', () => assert(Number.isNaN(localEpoch('yesterday', bias))));
test('invalid/missing timezone rejected', () => assert.throws(() => manifestWindow({ ...manifest, timeZoneBiasMinutes: undefined }, [start])));
test('partially retained JSONL tails do not crash or invent rows', () => assert.equal(jsonRows('{}\n{"bad":\n[]\nnull', 'fake').length, 1));
test('manifest native PID excludes cumulative older process', () => assert.equal(ownedNativeRows([start, row({ ...start.data, pid: 7896 })], window).length, 1));
test('nativePid supports metadata rows without process pid', () => assert.equal(ownedNativeRows([row({ event: 'msi-dinput-enumeration', nativePid: 9844, time: start.data.time })], window).length, 1));
test('unowned native rows are not silently promoted by timestamp', () => assert.equal(ownedNativeRows([row({ event: 'input-idle-window', time: start.data.time })], window).length, 0));
test('native rows after export are excluded', () => assert.equal(ownedNativeRows([row({ ...start.data, time: '2026-10-12T13:11:00' })], window).length, 0));
const observerStart = row({ event: 'start', parentPid: 9844, parentCreationTime: 1234, observerPid: 15788, utcEpochMs: t('13:07:55') }, 1, 'fake/visibility.log');
test('observer pid field is USB product id, never native process ownership', () => {
  const device = row({ event: 'directinput-device', observerPid: 15788, pid: 6402, utcEpochMs: t('13:08:00') });
  assert.equal(bindObserverRows([observerStart, device], window)[0].rows.length, 2);
});
test('observer session belongs to manifest parent identity', () => assert.equal(bindObserverRows([row({ ...observerStart.data, parentPid: 7896 })], window).length, 0));
test('observer parent creation identity required', () => assert.equal(bindObserverRows([row({ ...observerStart.data, parentCreationTime: undefined })], window).length, 0));
test('observer PID reuse cannot pull next parent session into current window', () => {
  const newStart = row({ ...observerStart.data, parentPid: 16908, utcEpochMs: t('13:09:00') });
  const laterDevice = row({ event: 'directinput-device', observerPid: 15788, pid: 9844, utcEpochMs: t('13:09:01') });
  assert.equal(bindObserverRows([observerStart, newStart, laterDevice], window)[0].rows.length, 1);
});
const msi = { hidVid: 0x0db0, hidPid: 0x1902, pnpInstanceId: 'HID\\VID_0DB0&PID_1902&MI_00&COL01\\main' };
test('MSI main gamepad capability is distinct from auxiliary devices', () => assert.equal(endpointRole({ ...msi, hidUsagePage: 1, hidUsage: 5 }), 'msi-physical-main-gamepad'));
test('same USB parent keyboard metadata is not gamepad input', () => assert.equal(endpointRole({ ...msi, hidUsagePage: 1, hidUsage: 6 }), 'msi-physical-keyboard-metadata'));
test('same USB parent mouse metadata is not proof of emitted clicks', () => assert.equal(endpointRole({ ...msi, hidUsagePage: 1, hidUsage: 2 }), 'msi-physical-mouse-metadata'));
test('consumer collection is auxiliary, not an extra controller', () => assert.equal(endpointRole({ ...msi, hidUsagePage: 12, hidUsage: 1 }), 'msi-physical-consumer-auxiliary'));
test('vendor page FF F0 is auxiliary, not a main gamepad', () => assert.equal(endpointRole({ ...msi, hidUsagePage: 0xfff0, hidUsage: 64 }), 'msi-physical-vendor-auxiliary'));
test('unreadable main path does not imply delivered input', () => assert.equal(endpointRole({ pnpInstanceId: msi.pnpInstanceId }), 'msi-main-path-caps-not-observed'));
test('known virtual HID child matched by instance, not VID/PID alone', () => {
  assert.equal(endpointRole({ pnpInstanceId: 'HID\\HIDCLASS\\one', hidVid: 0x054c, hidPid: 0x0df2 }, ['HID\\HIDCLASS\\one']), 'virtual-target-interface');
  assert.equal(endpointRole({ pnpInstanceId: 'HID\\VID_054C&PID_0DF2\\other', hidVid: 0x054c, hidPid: 0x0df2 }), 'unattributed-or-other-device');
});
test('ROG/GPD/unrelated endpoint remains unclassified by MSI rules', () => assert.equal(endpointRole({ hidVid: 0x0b05, hidPid: 0x1abe, hidUsagePage: 1, hidUsage: 5 }), 'unattributed-or-other-device'));
test('PnP primary root and HID child are one target topology, not two target instances', () => {
  const o = parsePnpReceipt(pnp); assert.equal(o.primaryRoots.length, 1); assert.equal(o.hidChildren.length, 1); assert.equal(o.companionRoots.length, 0);
});
test('missing PnP counts stay unknown, never zero', () => assert.equal(parsePnpReceipt('').counts['pnp-primary-roots'], null));
test('legitimate Xbox companion root is preserved separately', () => {
  const o = parsePnpReceipt(pnp.replace('pnp-xbox-companion-roots=0', 'pnp-xbox-companion-roots=1').replace('pnp-xbox-companion-root-ids=;', 'pnp-xbox-companion-root-ids=SWD\\XUSB\\one[root\\HIDMaestroXUSB];'));
  assert.equal(o.primaryRoots.length, 1); assert.equal(o.companionRoots.length, 1);
});
test('dual log sinks do not produce two host runs', () => assert.equal(buildRuns([start, started, row(started.data, 20, 'fake/virtual-gamepad.log'), prepared], window).length, 1));
test('PREPARE receipt must have same runID and epoch', () => assert.equal(buildRuns([start, started, row({ ...prepared.data, epoch: 3 })], window)[0].prepare, null));
test('successful ACK without PnP admission is not a target proof', () => assert.equal(buildRuns([start, started, row({ ...prepared.data, targetPnpAdmitted: false })], window)[0].prepare, null));
test('same-second Steam open before millisecond start ACK is retained with uncertainty', () => {
  const e = steamRunEvidence(steam(steamBlock('13:10:05', 'sdl://17', 1)), fixtureRun()); assert.deepEqual(e.targetIdentityOpenIndices, [1]);
  assert.match(e.temporalScope, /subsecond-order-not-observed/);
});
test('discovery totals cannot be converted to simultaneous controller count', () => {
  const repeat = '[2026-10-12 13:10:06] Local Device Found\n type: 0db0 1902\n path: sdl://16\n';
  const e = steamRunEvidence(steam(repeat.repeat(4)), fixtureRun()); assert.equal(e.discoveries.length, 4); assert.equal(e.targetIdentityOpenIndices.length, 0);
});
test('Elite second index is a Steam view, not a second driver root', () => {
  const e = steamRunEvidence(steam(steamBlock('13:10:05', 'sdl://17', 1) + steamBlock('13:10:06', 'sdl://18', 2) +
    '[2026-10-12 13:10:06] Queueing activation for controller: 1 app: 769\n[2026-10-12 13:10:06] Queueing activation for controller: 2 app: 769\n'), fixtureRun());
  assert.deepEqual(e.targetIdentityOpenIndices, [1, 2]); assert.equal(e.sameSecondActivationEvidence.length, 1);
  assert.equal(e.simultaneousInputDelivery, 'not-observed'); assert.equal(e.sdlAliasToPhysicalInterfaceMapping, 'not-observed');
});
test('Steam opens of MSI physical/auxiliary VID cannot become Elite target opens', () => {
  assert.equal(steamRunEvidence(steam(steamBlock('13:10:06', 'sdl://16', 2, '0db0 1902')), fixtureRun()).targetIdentityOpenIndices.length, 0);
});
test('open without a discovery is unassociated, not copied from prior index', () => {
  const r = steam(steamBlock('13:10:05', 'sdl://17', 1) + '[2026-10-12 13:10:06] !! Steam controller device opened for index 2.\n');
  assert.equal(r.filter(x => x.kind === 'opened')[1].discovery, null);
});
test('malformed discovery cannot steal following discovery metadata', () => {
  const r = steam('[2026-10-12 13:10:05] Local Device Found\n' + steamBlock('13:10:06', 'sdl://17', 1)); assert.equal(r[0].vidPid, null);
});
test('unindexed Steam close is kept uncertain rather than retiring arbitrary index', () => {
  const e = steamRunEvidence(steam(steamBlock('13:10:05', 'sdl://17', 1) + '[2026-10-12 13:10:07] Controller device closed after hid_read failure\n'), fixtureRun());
  assert.equal(e.unattributedCloses[0].index, null); assert.equal(e.simultaneousInputDelivery, 'not-observed');
});
test('adjacent PS5 epochs do not merge into simultaneous duplicate PS5 targets', () => {
  const a = row({ ...started.data, runId: 'ps5-a', epoch: 2, persona: 'dualsense-edge', vidPid: '054C:0DF2', time: '2026-10-12T13:09:13.046' });
  const b = row({ ...a.data, runId: 'ps5-b', epoch: 3, time: '2026-10-12T13:09:51.361' });
  const release = row({ event: 'input-host-release-receipt', pid: 9844, runId: 'ps5-a', epoch: 2, time: '2026-10-12T13:09:44.200', ok: true });
  const runs = buildRuns([start, a, b, release], window); assert.equal(runs.length, 2); assert(runs[0].end < runs[1].begin);
});
test('inventory timestamps keep snapshots separate and auxiliary count out of gamepad count', () => {
  const fakeDevices = [row({ event: 'directinput-device', observerPid: 15788, inventoryId: 130, utcEpochMs: t('13:10:08'), ...msi,
    hidUsagePage: 0xfff0, hidUsage: 64, interfacePath: 'vendor', buttonsDownCount: 0, decodedContentChanged: false }),
    row({ event: 'directinput-device', observerPid: 15788, inventoryId: 130, utcEpochMs: t('13:10:08'), ...msi,
      hidUsagePage: 12, hidUsage: 1, interfacePath: 'consumer', buttonsDownCount: 0 }),
    row({ event: 'directinput-device', observerPid: 15788, inventoryId: 131, utcEpochMs: t('13:10:09'), ...msi,
      hidUsagePage: 1, hidUsage: 5, interfacePath: 'main', buttonsDownCount: 0 })];
  const s = observerSnapshots([{ observerPid: 15788, rows: fakeDevices }], fixtureRun());
  assert.equal(s.length, 2); assert.equal(s[0].auxiliaryDeviceCount, 2); assert.equal(s[0].gamepadRoleDeviceCount, 0); assert.equal(s[0].nonNeutralButtonDeviceCount, 0);
});
test('virtual composite mouse metadata is not counted as a gamepad', () => {
  const run = fixtureRun(); run.prepare.observation.hidChildren.push({ instance: 'HID\\VIRTUAL_MOUSE', hardwareIds: [] });
  const device = row({ event: 'directinput-device', observerPid: 15788, inventoryId: 130, utcEpochMs: t('13:10:08'),
    pnpInstanceId: 'HID\\VIRTUAL_MOUSE', interfacePath: 'virtual-mouse', hidUsagePage: 1, hidUsage: 2 });
  const s = observerSnapshots([{ observerPid: 15788, rows: [device] }], run)[0]; assert.equal(s.gamepadRoleDeviceCount, 0);
});
test('unpid producer aggregates bind only by exact current Host tuple, not timestamp', () => {
  const data = { event: 'input-idle-window', time: '2026-10-12T13:10:08', hostRunId: 'elite-run', hostEpoch: 4,
    hostAcceptedContent: { meaningfulFrames: 54 } };
  const rows = [row(data), row({ ...data, hostRunId: 'old-run' }), row({ ...data, hostEpoch: 3 }), row({ ...data, pid: 7896 })];
  const out = producerWindowEvidence(rows, fixtureRun(), window); assert.equal(out.length, 1);
  assert.match(out[0].scope, /meaningful-frame-count-not-press-count/);
});
console.log(JSON.stringify({ result: 'PASS', fakeTests: tests, productExecuted: false, hardwareObserved: false, claim: 'parser/policy-regression-only;hardware-resolution-pending' }));

