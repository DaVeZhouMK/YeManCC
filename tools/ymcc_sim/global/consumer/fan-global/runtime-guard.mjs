/** Child-only safety boundary. Original tests/product expressions stay intact. */
import fs from 'node:fs';
import fsPromises from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { syncBuiltinESMExports } from 'node:module';
import childProcess from 'node:child_process';
import http from 'node:http';
import https from 'node:https';
import net from 'node:net';
import tls from 'node:tls';
import dgram from 'node:dgram';
import workerThreads from 'node:worker_threads';

const original = { readFileSync: fs.readFileSync.bind(fs), writeFileSync: fs.writeFileSync.bind(fs),
  realpathSync: fs.realpathSync.native.bind(fs.realpathSync), existsSync: fs.existsSync.bind(fs) };
const configurationPath = process.env.YMCC_FAN_GLOBAL_BOUNDARY;
if (!configurationPath) throw new Error('FAN_GLOBAL_BOUNDARY_CONFIGURATION_REQUIRED');
const config = JSON.parse(original.readFileSync(configurationPath, 'utf8'));
const normalize = input => path.resolve(input).toLowerCase();
const root = normalize(original.realpathSync(config.caseRoot));
const resolve = input => {
  let value = input instanceof URL ? fileURLToPath(input) : Buffer.isBuffer(input) ? input.toString() : input;
  if (typeof value !== 'string') return null;
  value = path.resolve(value); let parent = value;
  while (!original.existsSync(parent)) { const next = path.dirname(parent); if (next === parent) return normalize(value); parent = next; }
  return normalize(path.join(original.realpathSync(parent), path.relative(parent, value)));
};
const inside = value => value === root || value?.startsWith(root + path.sep);
const allowedReads = new Set(config.readFiles.map(resolve));
const reads = new Set(), denied = [], descriptors = new Map();
let readCount = 0, writeCount = 0, explicitExitCalled = false;
function deny(operation) { denied.push({ operation, code: 'FAN_GLOBAL_FORBIDDEN_IO' }); throw new Error('FAN_GLOBAL_FORBIDDEN_IO:' + operation); }
function checkRead(input, operation) {
  if (typeof input === 'number') { if (!descriptors.has(input)) return deny(operation + ':unknown-fd'); return; }
  const value = resolve(input);
  if (!value || (!inside(value) && !allowedReads.has(value))) return deny(operation);
  readCount++; if (!inside(value)) reads.add(value);
}
function checkWrite(input, operation) {
  if (typeof input === 'number') { if (descriptors.get(input) !== 'write') return deny(operation + ':unknown-write-fd'); return; }
  if (!inside(resolve(input))) return deny(operation);
  writeCount++;
}
function writing(flags) {
  return typeof flags === 'number' ? Boolean(flags & (fs.constants.O_WRONLY | fs.constants.O_RDWR | fs.constants.O_CREAT | fs.constants.O_TRUNC | fs.constants.O_APPEND))
    : /[wa+]/.test(String(flags ?? 'r'));
}
for (const name of ['readFileSync', 'readFile', 'existsSync', 'statSync', 'lstatSync', 'stat', 'lstat', 'accessSync', 'access', 'readdirSync', 'readdir', 'realpathSync', 'realpath']) {
  const implementation = fs[name];
  fs[name] = function (...args) { checkRead(args[0], 'fs.' + name); return implementation.apply(this, args); };
}
for (const name of ['writeFileSync', 'writeFile', 'appendFileSync', 'appendFile', 'mkdirSync', 'mkdir', 'mkdtempSync', 'mkdtemp', 'unlinkSync', 'unlink', 'rmSync', 'rm', 'rmdirSync', 'rmdir', 'chmodSync', 'chmod', 'truncateSync', 'truncate']) {
  const implementation = fs[name];
  fs[name] = function (...args) { checkWrite(args[0], 'fs.' + name); return implementation.apply(this, args); };
}
for (const name of ['copyFileSync', 'copyFile', 'cpSync', 'cp']) {
  const implementation = fs[name];
  fs[name] = function (...args) { checkRead(args[0], 'fs.' + name + ':source'); checkWrite(args[1], 'fs.' + name + ':destination'); return implementation.apply(this, args); };
}
for (const name of ['renameSync', 'rename']) {
  const implementation = fs[name];
  fs[name] = function (...args) { checkWrite(args[0], 'fs.' + name + ':source'); checkWrite(args[1], 'fs.' + name + ':destination'); return implementation.apply(this, args); };
}
for (const name of ['symlinkSync', 'symlink', 'linkSync', 'link']) fs[name] = () => deny('fs.' + name);
for (const [name, checker] of [['createReadStream', checkRead], ['createWriteStream', checkWrite]]) {
  const implementation = fs[name];
  fs[name] = function (...args) { checker(args[0], 'fs.' + name); return implementation.apply(this, args); };
}
fs.realpathSync.native = function (...args) { checkRead(args[0], 'fs.realpathSync.native'); return original.realpathSync(...args); };
const promiseOpen = fsPromises.open;
fsPromises.open = async function (file, flags, ...rest) { (writing(flags) ? checkWrite : checkRead)(file, 'fsp.open'); return promiseOpen.call(this, file, flags, ...rest); };
const openSync = fs.openSync;
fs.openSync = function (file, flags, ...rest) {
  const write = writing(flags); (write ? checkWrite : checkRead)(file, 'fs.openSync');
  const fd = openSync.call(this, file, flags, ...rest); descriptors.set(fd, write ? 'write' : 'read'); return fd;
};
for (const name of ['readFile', 'stat', 'lstat', 'access', 'readdir', 'realpath']) {
  const implementation = fsPromises[name];
  fsPromises[name] = async function (...args) { checkRead(args[0], 'fsp.' + name); return implementation.apply(this, args); };
}
for (const name of ['writeFile', 'appendFile', 'mkdir', 'mkdtemp', 'unlink', 'rm', 'rmdir', 'chmod', 'truncate']) {
  const implementation = fsPromises[name];
  fsPromises[name] = async function (...args) { checkWrite(args[0], 'fsp.' + name); return implementation.apply(this, args); };
}
for (const name of ['copyFile', 'cp']) {
  const implementation = fsPromises[name];
  fsPromises[name] = async function (...args) { checkRead(args[0], 'fsp.' + name); checkWrite(args[1], 'fsp.' + name); return implementation.apply(this, args); };
}
for (const name of ['symlink', 'link', 'rename']) fsPromises[name] = async () => deny('fsp.' + name);
for (const name of ['spawn', 'spawnSync', 'exec', 'execSync', 'execFile', 'execFileSync', 'fork']) childProcess[name] = () => deny('child_process.' + name);
for (const [module, names] of [[http, ['request', 'get', 'createServer']], [https, ['request', 'get', 'createServer']], [net, ['connect', 'createConnection', 'createServer']], [tls, ['connect', 'createServer']], [dgram, ['createSocket']]])
  for (const name of names) module[name] = () => deny('network.' + name);
workerThreads.Worker = class { constructor() { deny('worker_threads.Worker'); } };
process.dlopen = () => deny('process.dlopen');
globalThis.fetch = async () => deny('fetch');
globalThis.WebSocket = class { constructor() { deny('WebSocket'); } };
const exit = process.exit.bind(process);
process.exit = code => { explicitExitCalled = true; return exit(code); };
syncBuiltinESMExports();
process.on('exit', code => {
  original.writeFileSync(config.receiptPath, JSON.stringify({ schemaVersion: 1, kind: 'fan-global-runtime-boundary',
    status: denied.length || code !== 0 ? 'FAIL' : 'PASS', processId: process.pid, exitCode: code, explicitExitCalled,
    denied, sourceReads: [...reads].sort(), allowedReadCount: readCount, allowedWriteCount: writeCount,
    boundaries: { realNetwork: false, realChildProcess: false, nativeLibraryLoad: false, writesOutsideEvidence: false,
      realHostMain: false, hardwareAccess: false, operatingSystemSleep: false } }, null, 2));
});

