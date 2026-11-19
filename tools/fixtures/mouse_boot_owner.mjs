import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { extract } from './legion_motion_consumer/extract_main.mjs';

const hash = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function bootMouseOwnerSelection() {
  const selected = [];
  for (const mode of ['global', 'xbox', 'steam', 'xbox-steam']) {
    for (const scenario of ['stale-dispatch', 'stale-read', 'backend-read'])
      for (const change of ['revision', 'hash', 'mode', 'result', 'observed', 'read-failure']) selected.push([scenario, change, mode]);
    for (const change of ['ack', 'unrelated', 'none']) selected.push(['current', change, mode]);
    selected.push(['already-submitted', 'revision', mode], ['retired-idle', 'revision', mode], ['retry', 'revision', mode]);
    for (const scenario of ['stale-dispatch', 'stale-read', 'backend-read']) selected.push([scenario, 'claim', mode]);
    selected.push(['current', 'claim-current', mode]);
  }
  return selected;
}
export function prepareBootMouseOwner(root, out) {
  const main = fs.readFileSync(path.join(root, 'native/main.cpp'), 'utf8');
  const fixture = fs.readFileSync(path.join(root, 'tools/mouse_mode_control_fixture.cpp'), 'utf8');
  const marker = fixture.indexOf('// The lower provider regression');
  assert(marker > 0, 'Missing original inert provider terminal');
  let ports = fixture.slice(0, marker);
  const read = 'static Result observe(const std::wstring&){', gamebar = 'static HRESULT gamebarMouseGet(bool& value){';
  const write = ' ++writes;if(enabled)++joyStarts;else ++joyStops;';
  for (const anchor of [read, gamebar, write]) assert.equal(ports.split(anchor).length, 2, anchor);
  ports = ports.replace(read, read + 'fixtureProviderRead();').replace(gamebar, gamebar + 'fixtureProviderRead();')
    .replace(write, ' fixtureSubmitted(enabled);\n' + write);
  const guards = ['ymSettingsBootWriteMatches', 'mouseModeBootOwnerCheck', 'mouseModeRequestExpired', 'mouseModeSaveBootIntent'];
  const providers = ['ymccIsRogFamily', 'mouseModeSelectedBackend', 'mouseModeSteamDeckBlocked', 'mouseModeSteamDeckState',
    'mouseModeGamebarAbsent', 'mouseModeGamebarModelRejection', 'mouseModeStateLocked', 'mouseModeGetState',
    'mouseModeSetBackend', 'mouseModeApplyState', 'mousePolicySet', 'mousePolicyGet', 'mousePolicyCycle'];
  const fragments = [...guards, ...providers].map(name => extract(main, name));
  const registrations = ['mouseMode.setPolicy', 'mouseMode.setBackend', 'mouseMode.set'].map(name => {
    const start = main.indexOf('    ipc_on("' + name + '",');
    assert(start > 0, name);
    const end = main.indexOf('\n    });', start);
    assert(end > start, name);
    return main.slice(start, end + '\n    });'.length);
  }).join('\n');
  const serial = 'struct GamepadSerialEvent {struct ConfigurationWrite {explicit ConfigurationWrite(bool){}};};\n';
  const source = fs.readFileSync(path.join(root, 'tools/mouse_mode_boot_owner_fixture.cpp'), 'utf8')
    .replace('// ORIGINAL_PORTS', ports + serial + '\n#include "settings_baseline.h"\n')
    .replace('// ORIGINAL_GUARD', guards.map(name => extract(main, name).code).join('\n'))
    .replace('// ORIGINAL_PROVIDER', providers.map(name => extract(main, name).code).join('\n'))
    .replace('// ORIGINAL_HANDLERS', registrations);
  fs.mkdirSync(out, { recursive: true });
  const file = path.join(out, 'probe.cpp');
  fs.writeFileSync(file, source, { flag: 'wx' });
  return { file, sourceSha256: hash(file), fragments: fragments.map(({ code, ...identity }) => identity) };
}
export function runBootMouseOwner(root, out, compile) {
  const prepared = prepareBootMouseOwner(root, out), exe = path.join(out, 'probe.exe');
  const selected = bootMouseOwnerSelection();
  fs.writeFileSync(path.join(out, 'selection.json'), JSON.stringify(selected, null, 2), { flag: 'wx' });
  compile(out, prepared.file, exe, false);
  const rows = [];
  for (let i = 0; i < selected.length; i++) {
    const [scenario, change, mode] = selected[i], raw = path.join(out, String(i + 1).padStart(3, '0') + '.json');
    const args = [scenario, change, mode, path.join(root, 'PowerControl/settings-defaults-v1.json'), raw];
    const start = performance.now(), run = spawnSync(exe, args, { cwd: out, windowsHide: true, encoding: 'utf8', timeout: 20000 });
    for (const stream of ['stdout', 'stderr']) fs.writeFileSync(raw + '.' + stream + '.log', run[stream] || '', { flag: 'wx' });
    fs.writeFileSync(raw + '.process.json', JSON.stringify({ program: exe, args, exitCode: run.status,
      elapsedMs: performance.now() - start, error: run.error?.message || null }, null, 2), { flag: 'wx' });
    assert(!run.error && !run.signal, 'Boot mouse consumer must finish naturally');
    const result = JSON.parse(fs.readFileSync(raw, 'utf8'));
    assert.equal(run.status, 0, JSON.stringify(result));
    assert.equal(result.status, 'PASS');
    assert.deepEqual([result.scenario, result.change, result.mode], selected[i]);
    assert.equal(result.workerJoined, true);
    assert.equal(result.realProviderOperations, 0);
    rows.push({ scenario, change, mode, result, raw: { path: raw, bytes: fs.statSync(raw).size, sha256: hash(raw) } });
  }
  return { status: 'PASS', actualScenarios: rows.length, selected, prepared, executableSha256: hash(exe), rows,
    scope: 'Original IPC/boot guard/policy/provider; memory settings and OS leaves. No installed app or real provider operation.' };
}
