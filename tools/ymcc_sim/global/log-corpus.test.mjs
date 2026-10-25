import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { deflateRawSync } from 'node:zlib';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { indexLogCorpus, buildLogReplayPlan } from './log-corpus.mjs';

// Synthetic fixtures only: no actual logs, application/Host launches or sleep.
const python = process.env.YMCC_SIM_PYTHON ?? path.join(os.homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
const options = { pythonPath: python };
const runFile = promisify(execFile);
const reader = fileURLToPath(new URL('./log-zip-reader.py', import.meta.url));
const stamp = '2026-10-18T14:20:00.000Z';
const sleep = (extra = {}) => ({ time: stamp, event: 'sleep-retry-request', details: { generation: 3, requestError: 0 }, ...extra });
const fan = (extra = {}) => ({ timestampUtc: stamp, eventName: 'api.enable.hc-write-failure', ...extra });
const jsonl = (...records) => records.map(record => JSON.stringify(record)).join('\n') + '\n';
function crc32(data) {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte;
    for (let j = 0; j < 8; j++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function zip(entries) {
  const locals = [], centrals = [];
  let offset = 0;
  for (const entry of entries) {
    const name = Buffer.from(entry.name), localName = Buffer.from(entry.localName ?? entry.name);
    const body = Buffer.isBuffer(entry.data) ? entry.data : Buffer.from(entry.data ?? '');
    const method = entry.method ?? 0, compressed = method === 8 ? deflateRawSync(body) : body;
    const checksum = entry.crc ?? crc32(body), flags = entry.flags ?? 0x800;
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50); local.writeUInt16LE(20, 4); local.writeUInt16LE(flags, 6);
    local.writeUInt16LE(method, 8); local.writeUInt32LE(checksum, 14);
    local.writeUInt32LE(compressed.length, 18); local.writeUInt32LE(body.length, 22); local.writeUInt16LE(localName.length, 26);
    locals.push(local, localName, compressed);
    const central = Buffer.alloc(46);
    central.writeUInt32LE(0x02014b50); central.writeUInt16LE(0x314, 4); central.writeUInt16LE(20, 6);
    central.writeUInt16LE(flags, 8); central.writeUInt16LE(method, 10); central.writeUInt32LE(checksum, 16);
    central.writeUInt32LE(compressed.length, 20); central.writeUInt32LE(body.length, 24);
    central.writeUInt16LE(name.length, 28); central.writeUInt32LE(entry.externalAttr ?? 0, 38); central.writeUInt32LE(offset, 42);
    centrals.push(central, name);
    offset += local.length + localName.length + compressed.length;
  }
  const directory = Buffer.concat(centrals), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, directory, end]);
}
function fixture(t, entries, folder = 'MSI G3E') {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'ymcc-log-corpus-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const dir = path.join(root, folder); fs.mkdirSync(dir);
  const archivePath = path.join(dir, 'fixture.zip');
  fs.writeFileSync(archivePath, zip(entries));
  return { root, archivePath, dir };
}
const hasCode = (result, code) => result.diagnostics.some(d => d.code === code);

test('index reads central metadata, masks excluded names, and binds exact compressed bytes', async t => {
  const f = fixture(t, [{ name: 'logs/sleep-facts.log', data: Buffer.from([0xff]) },
    { name: 'settings/SECRET-session-token-game-title.json', data: 'SECRET-ORIGINAL-CONFIG' }]);
  const result = await indexLogCorpus(f.root, options);
  assert.equal(result.status, 'READY');
  assert.equal(result.summary.archiveCount, 1);
  assert.equal(result.summary.memberCount, 2);
  assert.equal(result.summary.contentRead, false);
  const a = result.archives[0];
  assert.equal(a.summary.contentBytesRead, 0);
  assert.equal(a.sourceArchive.stable, true);
  assert.equal(a.sourceArchive.bytes, fs.statSync(f.archivePath).size);
  assert.equal(a.sourceArchive.sha256, createHash('sha256').update(fs.readFileSync(f.archivePath)).digest('hex'));
  assert.match(a.sourceArchive.mtimeUtc, /Z$/);
  assert.deepEqual(a.operatorLabel, { value: 'MSI G3E', provenance: 'operator-label', detected: false });
  assert.equal(a.members[1].basename, undefined);
  assert.doesNotMatch(JSON.stringify(result), /SECRET|game-title/);
});

test('known observations produce mock schedules with no secrets or inferred presets/commands', async t => {
  const f = fixture(t, [{ name: 'sleep-facts.log', data: jsonl(sleep({ details: { generation: 7, requestError: 5,
    commandLine: 'SECRET-COMMAND', gameName: 'SECRET-GAME', session: 'SECRET-SESSION', token: 'SECRET-TOKEN' } }),
    sleep({ time: '2026-10-18T14:20:00.125Z' })) },
    { name: 'fan-host-fault.log', data: jsonl(fan({ details: { command: 'SECRET-NATIVE', error: 'SECRET-STACK' } })) }]);
  const p = await buildLogReplayPlan(f.archivePath, options);
  assert.equal(p.status, 'READY');
  assert.deepEqual(p.events.filter(e => e.domain === 'sleep').map(e => e.atMs), [0, 125]);
  assert.equal(p.events[0].generation, 7); assert.equal(p.events[0].errorCode, 5);
  assert.deepEqual(p.recommendedAdapterIds, ['sleep.host-forms', 'sleep.host-typeseries', 'sleep.host-rapidwake']);
  assert.ok(p.events.every(e => e.target === 'owned-mock-only'));
  assert.ok(p.scenarios.every(s => s.target === 'owned-mock-only' && s.causalClaims === false && s.schedule.every(e => e.action === 'observe')));
  assert.equal(p.permissions.hardwareWrite, false); assert.equal(p.permissions.systemSleep, false);
  assert.equal(p.events.find(e => e.domain === 'fan').preset, undefined);
  assert.doesNotMatch(JSON.stringify(p), /SECRET|commandLine|gameName|session|token|stack/i);
});

test('domain filter uses producer type and ignores invalid encoding in excluded input', async t => {
  const f = fixture(t, [{ name: 'native-lifecycle.log', data: Buffer.from([0xff]) },
    { name: 'yeman-fan-host20261018.log', data: '2026-10-18 22:20:53.100 +08:00 [ERR] SECRET-MESSAGE\n' },
    { name: 'sleep-facts.log', data: jsonl(sleep()) }]);
  const p = await buildLogReplayPlan(f.archivePath, { ...options, domains: ['fan'] });
  assert.equal(p.status, 'READY'); assert.equal(p.events.length, 1);
  assert.equal(p.events[0].event, 'fan-host.error-observed');
  assert.equal(p.events[0].time, '2026-10-18 22:20:53.100+08:00');
  assert.deepEqual(p.recommendedAdapterIds, []);
  assert.equal(p.summary.completeMemberCount, 1); assert.equal(p.summary.selectedMemberCount, 1);
  assert.doesNotMatch(JSON.stringify(p), /SECRET-MESSAGE/);
});

test('default event budget gives fan and sleep a share after dense input history', async t => {
  const dense = jsonl(...Array.from({ length: 30 }, () => ({ time: stamp, event: 'input-host-started' })));
  const f = fixture(t, [{ name: 'native-lifecycle.log.1', data: dense },
    { name: 'fan-host-fault.log', data: jsonl(fan()) }, { name: 'sleep-facts.log', data: jsonl(sleep()) }]);
  const p = await buildLogReplayPlan(f.archivePath, { ...options, limits: { maxEvents: 6 } });
  assert.equal(p.status, 'INCOMPLETE'); assert.ok(hasCode(p, 'MEMBER_EVENT_BUDGET'));
  assert.deepEqual([...new Set(p.events.map(e => e.domain))].sort(), ['fan', 'input', 'sleep']);
  assert.ok(p.events.length <= 6);
});

test('unknown format/event/phase/error code are explicit and cannot escape through fields', async t => {
  const f = fixture(t, [{ name: 'sleep-facts.log', data: 'SECRET-RAW-LINE\n' + jsonl(
    { time: stamp, event: 'SECRET-EVENT' }, sleep({ phase: 'SECRET-PHASE', errorCode: 'SECRET-CODE' })) }]);
  const p = await buildLogReplayPlan(f.archivePath, options);
  assert.equal(p.status, 'INCOMPLETE'); assert.equal(p.events.length, 1);
  assert.ok(p.diagnostics.some(d => d.status === 'UNSUPPORTED'));
  assert.doesNotMatch(JSON.stringify(p), /SECRET/);
});

test('preset is preserved only as an explicitly present known fan preset', async t => {
  const f = fixture(t, [{ name: 'fan-host.jsonl', data: jsonl(fan({ preset: 'balanced', errorCode: '0x80070005' }), fan()) }]);
  const p = await buildLogReplayPlan(f.archivePath, options);
  assert.equal(p.events[0].preset, 'balanced'); assert.equal(p.events[0].errorCode, '0x80070005');
  assert.equal(p.events[1].preset, undefined); assert.deepEqual(p.recommendedAdapterIds, []);
});

test('time regression never produces negative or fabricated schedule offsets', async t => {
  const f = fixture(t, [{ name: 'sleep-facts.log', data: jsonl(sleep(),
    sleep({ time: '2026-10-18T14:19:00Z' }), sleep({ time: '2026-10-18T14:19:30Z' }),
    sleep({ time: '2026-10-18T14:21:00Z' }), sleep({ time: 'SECRET-TIME' })) }]);
  const p = await buildLogReplayPlan(f.archivePath, options);
  assert.equal(p.status, 'INCOMPLETE'); assert.deepEqual(p.events.map(e => e.atMs), [0, null, null, 60000, null]);
  assert.equal(p.scenarios[0].timingStatus, 'UNSUPPORTED'); assert.doesNotMatch(JSON.stringify(p), /SECRET/);
});

for (const [name, entries, code] of [
  ['path traversal', [{ name: '../sleep-facts.log', data: jsonl(sleep()) }], 'UNSAFE_MEMBER_PATH'],
  ['Windows traversal', [{ name: 'safe\\..\\sleep-facts.log' }], 'UNSAFE_MEMBER_PATH'],
  ['alternate data stream', [{ name: 'sleep-facts.log:secret' }], 'UNSAFE_MEMBER_PATH'],
  ['normalized duplicate', [{ name: 'a/sleep-facts.log' }, { name: 'A/sleep-facts.log' }], 'DUPLICATE_MEMBER'],
  ['symlink member', [{ name: 'sleep-facts.log', externalAttr: (0o120777 << 16) >>> 0 }], 'NON_REGULAR_MEMBER'],
  ['encryption', [{ name: 'sleep-facts.log', flags: 1 }], 'ENCRYPTED_MEMBER'],
  ['compression bomb', [{ name: 'sleep-facts.log', data: 'X'.repeat(100000), method: 8 }], 'COMPRESSION_RATIO_LIMIT'],
  ['unsupported compression', [{ name: 'sleep-facts.log', method: 12 }], 'UNSUPPORTED_COMPRESSION'],
  ['huge line', [{ name: 'sleep-facts.log', data: 'X'.repeat(65537) }], 'LINE_TOO_LONG'],
  ['bad encoding', [{ name: 'sleep-facts.log', data: Buffer.from([0xff, 10]) }], 'INVALID_UTF8'],
  ['bad CRC', [{ name: 'sleep-facts.log', data: jsonl(sleep()), crc: 0 }], 'MEMBER_INTEGRITY_FAILED'],
  ['local name mismatch', [{ name: 'sleep-facts.log', localName: 'other-file.log', data: jsonl(sleep()) }], 'MEMBER_INTEGRITY_FAILED'],
  ['duplicate JSON keys', [{ name: 'sleep-facts.log', data: '{"event":"sleep-retry-request","event":"SECRET"}\n' }], 'DUPLICATE_JSON_KEY'],
]) test(`rejects ${name} without a usable schedule`, async t => {
  const f = fixture(t, entries);
  const p = await buildLogReplayPlan(f.archivePath, options);
  assert.equal(p.status, 'FAIL'); assert.ok(hasCode(p, code), JSON.stringify(p.diagnostics));
  assert.deepEqual(p.events, []); assert.deepEqual(p.scenarios, []);
});

test('member/read/line/archive/central/scan budgets are explicit', async t => {
  const f = fixture(t, [{ name: 'sleep-facts.log', data: jsonl(...Array.from({ length: 6 }, () => sleep())) }]);
  for (const [limits, code] of [[{ maxMemberReadBytes: 10 }, 'READ_BYTE_LIMIT'],
    [{ maxTotalReadBytes: 10 }, 'READ_BYTE_LIMIT'], [{ maxLinesPerMember: 1 }, 'LINE_COUNT_LIMIT'],
    [{ maxArchiveBytes: 10 }, 'ARCHIVE_SIZE_LIMIT'], [{ maxCentralDirectoryBytes: 1 }, 'CENTRAL_DIRECTORY_LIMIT']]) {
    const p = await buildLogReplayPlan(f.archivePath, { ...options, limits });
    assert.notEqual(p.status, 'READY'); assert.ok(hasCode(p, code), JSON.stringify(p.diagnostics));
  }
  fs.writeFileSync(path.join(f.dir, 'second.zip'), fs.readFileSync(f.archivePath));
  const corpus = await indexLogCorpus(f.root, { ...options, limits: { maxArchives: 1 } });
  assert.equal(corpus.status, 'INCOMPLETE'); assert.ok(hasCode(corpus, 'ARCHIVE_COUNT_LIMIT'));
});

test('invalid/ZIP64/spoofed central directory is rejected before content reads', async t => {
  const f = fixture(t, [{ name: 'sleep-facts.log' }, { name: 'fan-host.log' }]);
  const valid = fs.readFileSync(f.archivePath);
  for (const bytes of [Buffer.from('not zip'), (() => { const b = Buffer.from(valid); b.writeUInt16LE(65535, b.length - 12); return b; })(),
    (() => { const b = Buffer.from(valid); b.writeUInt16LE(1, b.length - 14); b.writeUInt16LE(1, b.length - 12); return b; })()]) {
    fs.writeFileSync(f.archivePath, bytes);
    const corpus = await indexLogCorpus(f.root, options);
    assert.equal(corpus.status, 'FAIL'); assert.equal(corpus.archives[0].summary.contentBytesRead, 0);
  }
});

test('runtime must be an explicit absolute executable or fixed Codex candidate', async t => {
  const f = fixture(t, [{ name: 'sleep-facts.log', data: jsonl(sleep()) }]);
  for (const pythonPath of ['python.exe', 'C:/Users/fixture/AppData/Local/Microsoft/WindowsApps/python.exe', 'Z:/missing/python.exe']) {
    const p = await buildLogReplayPlan(f.archivePath, { pythonPath });
    assert.equal(p.status, 'BLOCKED'); assert.ok(hasCode(p, 'PYTHON_RUNTIME_UNAVAILABLE'));
  }
  for (const invalid of [{ domains: ['SECRET-DOMAIN'] }, { limits: { maxEvents: 20001 } }, { limits: [] }]) {
    const p = await buildLogReplayPlan(f.archivePath, { ...options, ...invalid });
    assert.equal(p.status, 'FAIL'); assert.ok(hasCode(p, 'INVALID_OPTIONS'));
  }
});

test('Windows stat/fstat ctime disagreement is harmless; changed mtime invalidates identity', async t => {
  const f = fixture(t, [{ name: 'sleep-facts.log', data: jsonl(sleep()) }]);
  const script = `import json,os,runpy,sys,types
m=runpy.run_path(sys.argv[1],run_name="fixture_reader")
a=types.SimpleNamespace(st_dev=1,st_ino=2,st_size=3,st_mtime_ns=4,st_ctime_ns=5)
b=types.SimpleNamespace(st_dev=1,st_ino=2,st_size=3,st_mtime_ns=4,st_ctime_ns=999)
assert m["snapshot_signature"](a)==m["snapshot_signature"](b)
b.st_mtime_ns=6
assert m["snapshot_signature"](a)!=m["snapshot_signature"](b)
Reader=m["Reader"]; original=Reader.fingerprint
def changed(self,stream,initial):
 original(self,stream,initial)
 os.utime(sys.argv[2],ns=(initial.st_atime_ns,initial.st_mtime_ns+1000000000))
Reader.fingerprint=changed
print(json.dumps(Reader(m["CAPS"],[]).run(sys.argv[2],"replay")))`;
  const { stdout } = await runFile(python, ['-I', '-B', '-c', script, reader, f.archivePath], { windowsHide: true, shell: false, timeout: 10000 });
  const p = JSON.parse(stdout);
  assert.equal(p.status, 'INCOMPLETE'); assert.ok(hasCode(p, 'ARCHIVE_CHANGED'));
  assert.equal(p.sourceArchive.stable, false); assert.equal(p.sourceArchive.sha256, null);
  assert.deepEqual(p.events, []);
});


test('only a complete literal unavailable handshake recommends the reviewed fan gate recipe', async t => {
  const evidence = { hostMode: 'real-hc-unavailable', hardwareCapable: false, hardwareWritesEnabled: false, hardwareWritesObserved: false };
  const f = fixture(t, [{ name: 'fan-host.jsonl', data: jsonl(
    fan({ eventName: 'startup.handshake', details: evidence }),
    fan({ eventName: 'startup.handshake', details: { ...evidence, hardwareCapable: true } }),
    fan({ eventName: 'listener.ready', details: evidence })) }]);
  const p = await buildLogReplayPlan(f.archivePath, { ...options, domains: ['fan'] });
  assert.equal(p.status, 'READY'); assert.deepEqual(p.recommendedAdapterIds, ['fan.hc-init-unavailable']);
  assert.equal(p.events[0].hostMode, 'real-hc-unavailable');
  assert.equal(p.events[0].errorCode, undefined);
  assert.deepEqual(p.adapterRecommendations[0].evidenceEventIds, [p.events[0].id]);
  assert.equal(p.adapterRecommendations[0].mappedErrorCode, 'FAN_DEVICE_GATE_DENIED');
  assert.equal(p.adapterRecommendations[0].mappedErrorCodeProvenance, 'source-mapped-not-archive-literal');
  assert.equal(p.adapterRecommendations[0].rootCauseProven, false);
  assert.equal(p.permissions.hardwareWrite, false);
});

test('partial, cross-record, string booleans and generic fan errors cannot recommend a gate recipe', async t => {
  const f = fixture(t, [{ name: 'fan-host.jsonl', data: jsonl(
    fan({ eventName: 'startup.handshake', details: { hostMode: 'real-hc-unavailable', hardwareCapable: false } }),
    fan({ eventName: 'startup.handshake', details: { hardwareWritesEnabled: false, hardwareWritesObserved: false } }),
    fan({ eventName: 'startup.handshake', details: { hostMode: 'real-hc-unavailable', hardwareCapable: 'false', hardwareWritesEnabled: false, hardwareWritesObserved: false } }),
    fan({ details: { hostMode: 'real-hc-unavailable', hardwareCapable: false, hardwareWritesEnabled: false, hardwareWritesObserved: false } })) }]);
  const p = await buildLogReplayPlan(f.archivePath, options);
  assert.equal(p.status, 'INCOMPLETE'); assert.deepEqual(p.recommendedAdapterIds, []);
  assert.deepEqual(p.adapterRecommendations, []);
});


test('an exact event/read/line budget at EOF is a complete member', async t => {
  const data = jsonl(sleep());
  const f = fixture(t, [{ name: 'sleep-facts.log', data }]);
  const p = await buildLogReplayPlan(f.archivePath, { ...options, limits: { maxEvents: 1, maxLinesPerMember: 1, maxMemberReadBytes: Buffer.byteLength(data) } });
  assert.equal(p.status, 'READY'); assert.equal(p.summary.completeMemberCount, 1);
  assert.equal(p.members[0].integrity, 'CRC_VERIFIED');
});

test('OEM observations preserve literal booleans and settled phase without granting write authority', async t => {
  const details = { phase: 'settled-1500ms', origin: 'wmi-click', sourceGeneration: 2, code: 41, sequence: 3,
    actionDispatchAccepted: true, defaultActionSuppressionVerified: false, hardwareWriteIssued: false, readOnly: true,
    configured: true, requestPowerGeneration: 1, secretToken: 'NEVER_RETURN', officialStack: { secret: 'NEVER_RETURN' } };
  const f = fixture(t, [{ name: 'native-lifecycle.log', data: jsonl({time: stamp, event: 'msi-oem-suppression-observation', details}) }]);
  const p = await buildLogReplayPlan(f.archivePath, {...options, domains: ['input']});
  assert.equal(p.status, 'READY'); assert.equal(p.events.length, 1);
  const e = p.events[0];
  assert.equal(e.phase, 'settled-1500ms'); assert.equal(e.origin, 'wmi-click');
  assert.equal(e.actionDispatchAccepted, true); assert.equal(e.defaultActionSuppressionVerified, false);
  assert.equal(e.hardwareWriteIssued, false); assert.equal(e.requestPowerGeneration, 1);
  assert.equal(e.signalPolicy, 'diagnostic-observation-only');
  assert.equal(p.permissions.hardwareWrite, false); assert.deepEqual(p.recommendedAdapterIds, []);
  assert(!JSON.stringify(p).includes('NEVER_RETURN'));
});

test('producer-specific fields never coerce strings to proof or leak into unrelated events', async t => {
  const f = fixture(t, [{ name: 'native-lifecycle.log', data: jsonl(
    {time: stamp, event: 'msi-oem-suppression-observation', details: {phase: 'immediate', actionDispatchAccepted: 'true', hardwareWriteIssued: 1, sequence: true}},
    {time: stamp, event: 'oem-producer-edge', details: {actionDispatchAccepted: true, hardwareWriteIssued: true}}) }]);
  const p = await buildLogReplayPlan(f.archivePath, {...options, domains: ['input']});
  assert.equal(p.status, 'INCOMPLETE'); assert(hasCode(p, 'FIELD_UNSUPPORTED'));
  for(const e of p.events) { assert.equal(e.actionDispatchAccepted, undefined); assert.equal(e.hardwareWriteIssued, undefined); }
  assert.equal(p.events[0].sequence, undefined);
});

test('config and DirectInput observations retain separate success and bounded-backoff fields', async t => {
  const f = fixture(t, [{ name: 'native-lifecycle.log', data: jsonl(
    {time: stamp, event: 'msi-claw-gamepad-mode', details: {phase: 'config-returned', modeWrite: false, externalInputBlocked: 'unverified', virtualTargetCreated: false}},
    {time: stamp, event: 'msi-dinput-open-failed', details: {openAttempts: 2, episodeOpens: 2, episodeBudget: 12, bindCount: 0, nextBackoffMs: 240000}}) }]);
  const p = await buildLogReplayPlan(f.archivePath, {...options, domains: ['input']});
  assert.equal(p.status, 'READY'); const [config, retry] = p.events;
  assert.equal(config.modeWrite, false); assert.equal(config.externalInputBlocked, 'unverified'); assert.equal(config.virtualTargetCreated, false);
  assert.equal(retry.nextBackoffMs, 240000); assert.equal(retry.episodeBudget, 12); assert.equal(retry.bindCount, 0);
  assert.equal(p.summary.executed, false);
});

test('actual native top-level producer fields survive; conflicting nested detail is never combined', async t => {
  const f = fixture(t, [{ name: 'native-lifecycle.log', data: jsonl(
    {time: stamp, event: 'msi-oem-suppression-observation', phase: 'immediate', code: 41, actionDispatchAccepted: true,
      defaultActionSuppressionVerified: false, hardwareWriteIssued: false, details: {defaultActionSuppressionVerified: true, readOnly: true}},
    {time: stamp, event: 'msi-dinput-open-failed', openAttempts: 2, episodeBudget: 12, bindCount: 0, nextBackoffMs: 240000}) }]);
  const p = await buildLogReplayPlan(f.archivePath, {...options, domains: ['input']});
  assert.equal(p.status, 'READY'); const [oem, backoff] = p.events;
  assert.equal(oem.signalFieldContainer, 'record'); assert.equal(oem.actionDispatchAccepted, true);
  assert.equal(oem.defaultActionSuppressionVerified, false); assert.equal(oem.hardwareWriteIssued, false);
  assert.equal(oem.readOnly, undefined); assert.equal(backoff.nextBackoffMs, 240000);
});

test('all known Host handshake modes preserve literal protocol and receipt fields, including retained fault debt',async t=>{
  const f=fixture(t,[{name:'fan-host.jsonl',data:jsonl(
    fan({eventName:'startup.handshake',details:{hostMode:'real-hc-authorized',ok:true,supported:true,protocolVersion:2,hardwareCapable:true,fanRoute:'GenericDuty',fanRouteWriteReady:true}}),
    fan({eventName:'startup.handshake',details:{hostMode:'real-hc-fault-locked',ok:false,supported:false,protocolVersion:2,hardwareCapable:false,hardwareWritesEnabled:true,hardwareWritesObserved:true,fanRoute:null}}))}]);
  const p=await buildLogReplayPlan(f.archivePath,{...options,domains:['fan']});assert.equal(p.status,'READY');
  const[ready,debt]=p.events;assert.equal(ready.supported,true);assert.equal(ready.fanRoute,'GenericDuty');assert.equal(ready.protocolVersion,2);
  assert.equal(debt.hostMode,'real-hc-fault-locked');assert.equal(debt.hardwareCapable,false);assert.equal(debt.hardwareWritesObserved,true);assert.equal(debt.ok,false);assert.equal(debt.fanRoute,null);
  assert.deepEqual(p.recommendedAdapterIds,[]);
});

const gpuCapability = (extra = {}) => ({time:stamp,event:'machine-capability',schema:1,capability:'gpu.dedicatedMemory',ok:true,available:true,status:'available',provider:'rog-apu',verification:'verified',capacityRead:true,dedicatedBytes:4294967296,selected:'mib:4096',options:[{id:'auto',raw:0,capacityMiB:null,removeOverride:false},{id:'mib:4096',raw:260,capacityMiB:4096,removeOverride:false}],...extra});
test('actual flattened GPU capability producer routes only reviewed scalars and registered workflow',async t=>{
 const f=fixture(t,[{name:'native-lifecycle.log',data:jsonl(gpuCapability({pnpId:'NEVER_RETURN',driverVersion:'NEVER_RETURN',processor:'NEVER_RETURN',reason:'NEVER_RETURN',bootId:'NEVER_RETURN',readDiagnostics:{secret:'NEVER_RETURN'},options:[{id:'auto',raw:0,capacityMiB:null,removeOverride:false,secret:'NEVER_RETURN'},{id:'mib:4096',raw:260,capacityMiB:4096,removeOverride:false}]}))}]);
 const p=await buildLogReplayPlan(f.archivePath,{...options,domains:['performance']});assert.equal(p.status,'READY');assert.equal(p.events.length,1);const e=p.events[0];assert.equal(e.domain,'performance');assert.equal(e.provider,'rog-apu');assert.equal(e.dedicatedBytes,4294967296);assert.equal(e.options.length,2);assert.equal(e.options[0].secret,undefined);assert.doesNotMatch(JSON.stringify(p),/NEVER_RETURN/);assert.deepEqual(p.recommendedAdapterIds,['performance.gpu-dedicated-memory-workflow']);assert.equal(p.adapterRecommendations[0].rootCauseProven,false);assert.equal(p.permissions.hardwareWrite,false);
});
test('GPU domain selection shares native producer but never leaks or mixes other capability/record fields',async t=>{
 const f=fixture(t,[{name:'native-lifecycle.log',data:jsonl({time:stamp,event:'oem-producer-edge'},gpuCapability({details:{provider:'rog-vram',verification:'not_effective'}}),{time:stamp,event:'machine-capability',capability:'other.capability',provider:'rog-apu'})}]);
 const p=await buildLogReplayPlan(f.archivePath,{...options,domains:['performance']});assert.equal(p.status,'READY');assert.equal(p.events.length,1);assert.equal(p.events[0].provider,'rog-apu');assert.equal(p.events[0].verification,'verified');
 const inputOnly=await buildLogReplayPlan(f.archivePath,{...options,domains:['input']});assert(inputOnly.events.every(e=>e.domain==='input'));assert.equal(inputOnly.recommendedAdapterIds.length,0);
});
test('GPU whitelist rejects coercion/array enums, unknown option IDs and partially invalid options as a whole',async t=>{
 const f=fixture(t,[{name:'native-lifecycle.log',data:jsonl(gpuCapability({provider:['rog-apu'],verification:['verified'],dedicatedBytes:'4294967296',capacityRead:1,selected:'NEVER_RETURN',options:[{id:'auto',raw:0,capacityMiB:null,removeOverride:false},{id:'NEVER_RETURN',raw:1,capacityMiB:1,removeOverride:false}]}))}]);
 const p=await buildLogReplayPlan(f.archivePath,{...options,domains:['performance']});assert.equal(p.status,'INCOMPLETE');const e=p.events[0];for(const key of ['provider','verification','dedicatedBytes','capacityRead','selected','options'])assert.equal(e[key],undefined);assert.doesNotMatch(JSON.stringify(p),/NEVER_RETURN/);assert(hasCode(p,'FIELD_UNSUPPORTED'));
});
test('GPU options never accept duplicate ids, boolean numeric capacities or missing capacity field',async t=>{
 for(const invalid of [[{id:'auto',raw:0,capacityMiB:null,removeOverride:false},{id:'auto',raw:1,capacityMiB:1,removeOverride:false}],[{id:'auto',raw:false,capacityMiB:null,removeOverride:false},{id:'mib:4',raw:4,capacityMiB:4,removeOverride:false}],[{id:'auto',raw:0,removeOverride:false},{id:'mib:4',raw:4,capacityMiB:4,removeOverride:false}]]){
  const f=fixture(t,[{name:'native-lifecycle.log',data:jsonl(gpuCapability({options:invalid}))}]);const p=await buildLogReplayPlan(f.archivePath,{...options,domains:['performance']});assert.equal(p.status,'INCOMPLETE');assert.equal(p.events[0].options,undefined);
 }
});

test('dense input cannot starve GPU observations inside the same native log member',async t=>{
 const f=fixture(t,[{name:'native-lifecycle.log',data:jsonl(...Array.from({length:20},()=>({time:stamp,event:'input-host-started'})),gpuCapability())}]);const p=await buildLogReplayPlan(f.archivePath,{...options,domains:['input','performance'],limits:{maxEvents:4}});assert.equal(p.status,'INCOMPLETE');assert.deepEqual(p.events.map(e=>e.domain),['input','input','performance']);assert(hasCode(p,'MEMBER_EVENT_BUDGET'));assert.equal(p.summary.completeMemberCount,1);
});

test('real signal-check CLI consumes filtered GPU archive values without granting hardware/save or complete acceptance',async t=>{
 const f=fixture(t,[{name:'native-lifecycle.log',data:jsonl(gpuCapability({pnpId:'NEVER_RETURN',bootId:'NEVER_RETURN'}))}]);
 const cli=fileURLToPath(new URL('./cli.mjs',import.meta.url)),out=path.join(f.root,'cli-evidence');const called=await runFile(process.execPath,[cli,'--mode','signal-check','--domain','performance','--archive',f.archivePath,'--python',python,'--out-dir',out],{windowsHide:true,timeout:30000,maxBuffer:1024*1024}).then(value=>({...value,code:0}),error=>({code:error.code,stdout:error.stdout,stderr:error.stderr}));
 assert.equal(called.code,10,called.stderr);const resultPath=called.stdout.match(/^RESULT (.+)$/m)?.[1].trim();assert(resultPath);const r=JSON.parse(fs.readFileSync(resultPath));assert.equal(r.status,'INCOMPLETE');assert.equal(r.sourceArchive.stable,true);assert.equal(r.results[0].result.summary.actualMemoryReads,1);assert.equal(r.results[0].result.summary.parsedPresentation,1);assert.equal(r.results[0].result.evidence[0].state.status,'已生效');assert.equal(r.results[0].result.evidence[0].feedback.bindingVerified,false);assert.equal(r.results[0].result.evidence[0].feedback.saveInvoked,false);assert.equal(r.fullProductAcceptance,false);assert.doesNotMatch(JSON.stringify(r),/NEVER_RETURN/);
});

// Ordinary fan-api ZIP input: producer payloads come from the unchanged product
// fixture. The synthetic native envelope is not native/handheld execution.
let actualFanApiProducer;
async function producerRecords() {
 if(!actualFanApiProducer)actualFanApiProducer=(await import('./fan-api-log-consumer/fixture.mjs')).generateFanApiFixture();
 return structuredClone((await actualFanApiProducer).records);
}
const nativeFanEnvelope=records=>records.map(row=>({time:new Date(Date.parse(stamp)+row.atMs).toISOString(),event:row.event,details:row.details}));
async function fanApiArchive(t,change=x=>x) {
 const records=change(await producerRecords());
 const f=fixture(t,[{name:'logs/fan-api.log',data:jsonl(...nativeFanEnvelope(records))}]);
 return {...f,records,plan:await buildLogReplayPlan(f.archivePath,{...options,domains:['fan']})};
}
test('ordinary fan-api ZIP connects actual schema2 projections with separate archive/reconstructed identities',async t=>{
 const f=await fanApiArchive(t),p=f.plan;
 assert.equal(p.status,'READY',JSON.stringify(p.diagnostics));
 assert.equal(p.members[0].format,'fan-api');assert.equal(p.members[0].integrity,'CRC_VERIFIED');
 assert.deepEqual(p.events.map(row=>row.event),['startup.handshake','api.transport-failure']);
 assert(p.events.every(row=>row.memberId==='member-0001'&&row.provenance==='source-derived-fixture'&&row.sourceProjection==='ordinary-log-allowlisted-record'));
 assert.deepEqual(p.events.map(row=>row.line),[1,2]);assert.equal(p.sourceHashes.length,4);
 assert.equal(p.fanApiIngestion.originalArchiveIdentity.sha256,createHash('sha256').update(fs.readFileSync(f.archivePath)).digest('hex'));
 assert.notEqual(p.fanApiIngestion.batches[0].projectedArrayIdentity.sha256,p.sourceArchive.sha256);
 assert.equal(p.fanApiIngestion.originalMemberByteHashesAvailable,false);
 assert.equal(p.fanApiIngestion.originalUserIntentEstablished,false);
});
test('ordinary fan-api projections execute the existing actual fan lifecycle and keep 12029 rejected without control writes',async t=>{
 const {replaySignals}=await import('./signal-replay.mjs'),f=await fanApiArchive(t);
 const r=await replaySignals({repoRoot:process.cwd(),plan:f.plan,domains:['fan']});
 assert.equal(r.status,'PASS',JSON.stringify(r.diagnostics));
 assert.equal(r.results[0].result.summary.curveWrites,0);assert.equal(r.results[0].result.summary.explicitRequests,0);
 assert.equal(r.hardwareValidated,false);assert.equal(r.planSourceHashes.length,4);
 assert(r.results[0].result.evidence.some(row=>row.feedback.status==='rejected'));
});
for(const schema of [1,undefined])test('ordinary ZIP rejects unsupported/missing fan-api schema '+schema+' before any consumer',async t=>{
 const f=await fanApiArchive(t,records=>{if(schema===undefined)delete records[0].details.observationSchemaVersion;else records[0].details.observationSchemaVersion=schema;return records;});
 assert.equal(f.plan.status,'INCOMPLETE');assert.deepEqual(f.plan.events,[]);
 const {replaySignals}=await import('./signal-replay.mjs');let calls=0;
 const result=await replaySignals({repoRoot:process.cwd(),plan:f.plan,domains:['fan'],loadConsumer:()=>{calls++;}});
 assert.equal(calls,0);assert.equal(result.status,'BLOCKED');
});
test('ordinary ZIP retains root response contradiction as rejection and erases the entire rejected batch',async t=>{
 const f=await fanApiArchive(t,records=>{records[0].details.receipt.ok=false;records[0].details.ok=false;return records;});
 assert.equal(f.plan.status,'FAIL');assert.deepEqual(f.plan.events,[]);
 assert(hasCode(f.plan,'FAN_API_ROOT_RECEIPT_RESPONSE_CONTRADICTION'));
});
test('ordinary ZIP request-failure never becomes a transport error based on its text',async t=>{
 const f=await fanApiArchive(t,records=>{records[1].event='api.request-failure';return records;});
 assert.equal(f.plan.status,'INCOMPLETE');assert.deepEqual(f.plan.events.map(row=>row.event),['startup.handshake']);
 assert(hasCode(f.plan,'FAN_API_RESPONSE_REQUEST_FAILURE_NOT_TRANSPORT'));
});
test('ordinary fan-api ZIP drops unknown free text, credentials, paths, IDs and unsupported endpoints',async t=>{
 const f=await fanApiArchive(t,records=>{for(const row of records){row.details.session='NEVER_RETURN_SESSION';row.details.pathHint='NEVER_RETURN_PATH';row.details.credential='NEVER_RETURN_KEY';}
 records[1].details.error='NEVER_RETURN_ERROR';return records;});
 assert.equal(f.plan.status,'INCOMPLETE');assert.doesNotMatch(JSON.stringify(f.plan),/NEVER_RETURN/);
 assert.deepEqual(f.plan.events.map(row=>row.event),['startup.handshake']);
});
test('ordinary fan-api private device identity cannot be silently discarded to approve a profile-class claim',async t=>{
 const f=await fanApiArchive(t,records=>{records[0].details.response.deviceClass='HandheldCompanion.Devices.GPDWin5';records[0].details.response.deviceIdentity={serial:'NEVER_RETURN'};return records;});
 assert.equal(f.plan.status,'INCOMPLETE');assert(hasCode(f.plan,'FAN_API_PRIVATE_IDENTITY_NOT_REPLAYABLE'));
 assert.doesNotMatch(JSON.stringify(f.plan),/NEVER_RETURN/);assert(!f.plan.events.some(row=>row.event==='startup.handshake'));
});
test('ordinary fan-api duplicate JSON key stays a ZIP failure rather than a normalized successful record',async t=>{
 const records=nativeFanEnvelope(await producerRecords()),first=JSON.stringify(records[0]).replace('"details":{','"details":{"observationSchemaVersion":1,');
 const f=fixture(t,[{name:'fan-api.log',data:first+'\n'}]),p=await buildLogReplayPlan(f.archivePath,{...options,domains:['fan']});
 assert.equal(p.status,'FAIL');assert(hasCode(p,'DUPLICATE_JSON_KEY'));assert.deepEqual(p.events,[]);
});
test('ordinary fan-api source-line time reversal is rejected rather than sorted into a valid timeline',async t=>{
 const f=await fanApiArchive(t,records=>{records[0].atMs=1000;records[1].atMs=0;return records;});
 assert.equal(f.plan.status,'FAIL');assert(hasCode(f.plan,'TIME_ORDER_UNSUPPORTED'));assert.deepEqual(f.plan.events,[]);
});
test('ordinary fan-api schema wrong boolean type is not coerced into a successful receipt',async t=>{
 const f=await fanApiArchive(t,records=>{records[0].details.receipt.ok='false';return records;});
 assert.equal(f.plan.status,'FAIL');assert.deepEqual(f.plan.events,[]);
});
test('ordinary fan-api malformed CRC cannot contribute projected events',async t=>{
 const records=nativeFanEnvelope(await producerRecords());const f=fixture(t,[{name:'fan-api.log',data:jsonl(...records),crc:123}]);
 const p=await buildLogReplayPlan(f.archivePath,{...options,domains:['fan']});assert.equal(p.status,'FAIL');assert.deepEqual(p.events,[]);
});
test('actual signal-check CLI executes the ordinary schema2 ZIP ingestion and original software consumer',async t=>{
 const f=await fanApiArchive(t),out=path.join(f.root,'fan-cli-evidence'),cli=fileURLToPath(new URL('./cli.mjs',import.meta.url));
 const called=await runFile(process.execPath,[cli,'--mode','signal-check','--domain','fan','--archive',f.archivePath,'--python',python,'--out-dir',out],{windowsHide:true,timeout:30000,maxBuffer:1024*1024}).then(value=>({...value,code:0}),error=>({code:error.code,stdout:error.stdout,stderr:error.stderr}));
 assert.equal(called.code,0,called.stderr);const file=called.stdout.match(/^RESULT (.+)$/m)?.[1].trim();assert(file);
 const r=JSON.parse(fs.readFileSync(file,'utf8'));assert.equal(r.status,'PASS');assert.equal(r.results[0].result.summary.curveWrites,0);assert.equal(r.fullProductAcceptance,false);
});

test('unreviewed profile class is omitted without claiming a model while generic handshake software remains consumable',async t=>{
 const f=await fanApiArchive(t,records=>{records[0].details.response.deviceClass='NEVER_RETURN_PRIVATE_CLASS';return records;});
 assert.equal(f.plan.status,'READY');assert.equal(f.plan.fanApiIngestion.modelIdentityEstablished,false);
 assert.equal(f.plan.events[0].deviceClass,undefined);assert.doesNotMatch(JSON.stringify(f.plan),/NEVER_RETURN/);
});

test('unsupported old fan-api schema stays rejected while other producer evidence remains usable and incomplete',async t=>{
 const records=await producerRecords();delete records[0].details.observationSchemaVersion;
 const f=fixture(t,[{name:'fan-api.log',data:jsonl(...nativeFanEnvelope([records[0]]))},{name:'sleep-facts.log',data:jsonl(sleep())}]);
 const p=await buildLogReplayPlan(f.archivePath,{...options,domains:['fan','sleep']});
 assert.equal(p.status,'INCOMPLETE');assert(p.events.some(row=>row.domain==='sleep'));assert(!p.events.some(row=>row.domain==='fan'));
 assert.equal(p.fanApiIngestion.batches[0].normalizedScope.status,'REJECTED');
 assert(hasCode(p,'FAN_API_OBSERVATION_VERSION_MISSING'));
});
