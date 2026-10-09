/** Current production CyclePort contract: native coordinator -> InputHost -> pinned
 * Nefarius ToUsbPnPDevice/CyclePort. The retired native hub IOCTL is NOT restored.
 * Static source + adversarial mutations only; never invokes a device or driver.
 * Preserves StopRumble/reentry/enumerator/base-container/error/restore invariants.
 */
import assert from 'node:assert/strict';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { createHash } from 'node:crypto';
const root = process.cwd();
const native = readFileSync(resolve(root, 'native/main.cpp'), 'utf8');
const host = readFileSync(resolve(root, 'InputHost/Program.cs'), 'utf8');
const project = readFileSync(resolve(root, 'InputHost/YeManInputHost.csproj'), 'utf8');
function functionBody(source: string, marker: string): string {
  const start = source.indexOf(marker);
  assert(start >= 0, `missing function ${marker}`);
  const open = source.indexOf('{', start);
  let depth = 0, quote = '', line = false, block = false;
  for (let i = open; i < source.length; i++) {
    const ch = source[i], next = source[i + 1];
    if (line) { if (ch === '\n') line = false; continue; }
    if (block) { if (ch === '*' && next === '/') { block = false; i++; } continue; }
    if (quote) { if (ch === '\\') i++; else if (ch === quote) quote = ''; continue; }
    if (ch === '/' && next === '/') { line = true; i++; continue; }
    if (ch === '/' && next === '*') { block = true; i++; continue; }
    if (ch === '"' || ch === "'") { quote = ch; continue; }
    if (ch === '{') depth++;
    if (ch === '}' && --depth === 0) return source.slice(open, i + 1);
  }
  throw new Error(`unterminated function ${marker}`);
}
function verify(n: string, h: string, p: string): string[] {
  const assertions: string[] = [];
  const check = (yes: boolean, label: string) => { assert(yes, label); assertions.push(label); };
  const cycle = functionBody(n, 'static bool hidHideCycleInstance(');
  const restore = functionBody(n, 'static void inputHostRestoreVisibilityViaPipeLocked() {');
  const stop = functionBody(n, 'static void inputHostStopLocked(const char* reason) {');
  const finalRestore = functionBody(n, 'static bool hidHideRestorePhysicalTransaction(const char* reason) {');
  const hs = h.indexOf('case "HIDHIDE_CYCLE":');
  check(hs >= 0, 'Host synchronous cycle command exists');
  const handler = h.slice(hs, h.indexOf('default:', hs));
  check(!/static constexpr ULONG kIoctlUsbHubCyclePort\s*=/.test(n), 'retired native hub IOCTL is not reintroduced');
  check(cycle.includes('XINPUT_VIBRATION zero') && cycle.includes('ymccXInputSetState14') &&
    cycle.indexOf('ymccXInputSetState14') < cycle.indexOf('inputHostRequestLocked("HIDHIDE_CYCLE"'), 'C1 StopRumble precedes cycle');
  check(cycle.includes('g_hidHideCycleInFlight') && cycle.includes('cycle-in-flight'), 'C2 reentry guarded');
  check(cycle.includes('CM_DRP_ENUMERATOR_NAME') && cycle.includes('unsupported-enumerator') &&
    cycle.includes('if (hcUsbLike)'), 'C3 USB/HID gate before side effects');
  check(cycle.includes('inputHostRequestLocked("HIDHIDE_CYCLE"') &&
    cycle.includes('{{"path", W2U(instanceId)}}') && cycle.includes('"hidhide-cycle", cycleTimeoutMs, &cycleResponse'), 'cycle pipe path/ACK/budget bound');
  check(cycle.includes('cycleResponse.contains("detail")') && cycle.includes('detail == "true"'), 'ACK success comes from detail, not nonexistent payload');
  check(!cycle.includes('hidHideCyclePortHub(') && !cycle.includes('hidHideRunPnputil('), 'single Nefarius backend; no revived native or CLI fallback');
  check(handler.includes('FindPhysicalDevice(envelope.HidHidePath)') && handler.includes('device.ToUsbPnPDevice()') &&
    handler.includes('usb.CyclePort()'), 'C4 real PnP target converted to USB parent by pinned SDK');
  check(handler.includes('"false-empty-path"') && handler.includes('"false-device-not-found"') &&
    handler.includes('catch (Exception ex)') && handler.includes('"false:" + ex.GetType().Name') &&
    handler.indexOf('usb.CyclePort()') < handler.indexOf('"true"'), 'C5 void-return and exception semantics retained');
  check(p.includes('<Reference Include="Nefarius.Utilities.DeviceManagement">') &&
    h.includes('using Nefarius.Utilities.DeviceManagement.Extensions;'), 'real SDK dependency is referenced');
  check(restore.includes('baseContainerDeviceInstanceId') && restore.includes('hidHideCycleInstance(U2W(base), "pre-release")'), 'restore uses physical USB base, not virtual/persona path');
  check(restore.indexOf('"HIDHIDE_UNHIDE"') < restore.indexOf('hidHideCycleInstance(U2W(base)'), 'Unhide before CyclePort while Host alive');
  check(stop.indexOf('inputHostRestoreVisibilityViaPipeLocked()') < stop.indexOf('inputHostRequestLocked("RELEASE_TARGET"') &&
    stop.indexOf('inputHostRequestLocked("RELEASE_TARGET"') < stop.indexOf('inputHostRequestLocked("SHUTDOWN"'), 'pre-release unhide/cycle precedes release and shutdown');
  check(restore.includes('!base.starts_with("USB\\\\VID_045E")') && finalRestore.includes('journal.preReleaseCycleAttempted') &&
    finalRestore.includes('journal.preReleaseCycleOk'), 'xusb exclusion and no duplicate post-release cycle preserved');
  check(n.includes('hidHideApplyPhysicalTransaction(g_inputHostRunId, /*powerCycle=*/false)') &&
    n.includes('inputHostQueueLifecycleWork("late-hide-after-source-return")') &&
    n.includes('physical-source-returned-after-skip'), 'late source hide still skips power cycle');
  check(h.includes('case "HIDHIDE_CYCLE_BEGIN":') && h.includes('case "HIDHIDE_CYCLE_POLL":') &&
    n.includes('inputHostRequestLocked("HIDHIDE_CYCLE_BEGIN"') && n.includes('inputHostRequestLocked("HIDHIDE_CYCLE_POLL"'), 'async close refresh uses existing real SDK worker');
  return assertions;
}
const assertions = verify(native, host, project);
const mutations = [
  { name: 'lost-real-SDK-call', n: native, h: host.replaceAll('usb.CyclePort();', '/* missing CyclePort */'), p: project },
  { name: 'wrong-ACK-field', n: native.replaceAll('cycleResponse.contains("detail")', 'cycleResponse.contains("payload")'), h: host, p: project },
  { name: 'lost-USB-parent-conversion', n: native, h: host.replaceAll('device.ToUsbPnPDevice()', 'device.WrongParent()'), p: project },
  { name: 'lost-pre-release-cycle', n: native.replaceAll('hidHideCycleInstance(U2W(base), "pre-release")', 'true'), h: host, p: project },
  { name: 'lost-exception-failure', n: native, h: host.replaceAll('"false:" + ex.GetType().Name', '"true"'), p: project },
  { name: 'late-hide-reintroduces-power-cycle', n: native.replaceAll('/*powerCycle=*/false', '/*powerCycle=*/true'), h: host, p: project },
];
for (const mutation of mutations) assert.throws(() => verify(mutation.n, mutation.h, mutation.p), mutation.name);
const evidencePath = resolve(root, '../../Build/Validation/HC-SIZE-ORDER-20261007/hidhide-cycle-current-contract.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify({ schema: 'HIDHIDE_CURRENT_NEFARIUS_CYCLE_CONTRACT_V2', date: '2026-10-07', status: 'SOURCE_CONTRACT_PASS_NOT_DEVICE_PASS',
  systemMutation: false, deviceAccessed: false, assertions, rejectedMutations: mutations.map(m => m.name),
  sourceHashes: { native: createHash('sha256').update(native).digest('hex'), host: createHash('sha256').update(host).digest('hex') },
  supersedes: 'retired native hub implementation checks, not HC lifecycle assertions',
}, null, 2));
console.log(`HidHide current CyclePort contract: PASS (${assertions.length} checks, ${mutations.length} adversarial mutations rejected)`);
