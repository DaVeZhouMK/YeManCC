/** Actual original TDP test in a CommonJS VM; only reviewed builtins are supplied.
 * FS terminal stays under this case's fresh scratch path. Product IPC responders
 * and assertions are the unchanged original script. No product process starts.
 */
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import vm from 'node:vm';
import { performance } from 'node:perf_hooks';
import { webcrypto } from 'node:crypto';

const inside = (root, target) => {
  const relative = path.relative(root, target);
  return relative === '' || (relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative));
};
export function createIoBoundary(scratch) {
  if (!path.isAbsolute(scratch) || !fs.statSync(scratch).isDirectory()) throw Error('TDP_FIXTURE_SCRATCH_REQUIRED');
  const realRoot = fs.realpathSync(scratch);
  const observation = { reads: 0, writes: 0, virtualizedCalls: 0, denied: [], processAttempts: 0, networkAttempts: 0, required: [] };
  function guard(target) {
    if (typeof target !== 'string') throw Error('TDP_FIXTURE_PATH_STRING_REQUIRED');
    const requested = path.resolve(target);
    const virtualRoot = 'C:\\SOFT\\YeMan\\PowerControl';
    const absolute = inside(virtualRoot, requested) ? path.join(scratch, 'default-install', path.relative(virtualRoot, requested)) : requested;
    if (absolute !== requested) observation.virtualizedCalls++;
    if (!inside(path.resolve(scratch), absolute)) { observation.denied.push('OUTSIDE_SCRATCH'); throw Error('TDP_FIXTURE_PATH_OUTSIDE_SCRATCH'); }
    let parent = absolute;
    while (!fs.existsSync(parent) && parent !== path.dirname(parent)) parent = path.dirname(parent);
    if (!inside(realRoot, fs.realpathSync(parent))) { observation.denied.push('REPARSE_ESCAPE'); throw Error('TDP_FIXTURE_REPARSE_ESCAPE'); }
    return absolute;
  }
  const names = ['readFileSync', 'writeFileSync', 'mkdirSync', 'existsSync', 'readdirSync', 'statSync', 'mkdtempSync', 'rmSync'];
  const safeFs = Object.fromEntries(names.map(name => [name, (file, ...args) => {
    file = guard(file);
    if (name === 'rmSync' && path.resolve(file) === path.resolve(scratch)) throw Error('TDP_SCHEDULE_ROOT_DELETE_FORBIDDEN');
    if (['writeFileSync','mkdirSync','mkdtempSync','rmSync'].includes(name)) observation.writes++; else observation.reads++;
    return fs[name](file, ...args);
  }]));
  const blockedProcess = new Proxy({}, { get(_object, name) {
    if (typeof name !== 'string') return undefined;
    return () => { observation.processAttempts++; throw Error('TDP_FIXTURE_PROCESS_FORBIDDEN'); };
  } });
  function requireReviewed(specifier) {
    observation.required.push(specifier);
    if (['node:fs', 'fs'].includes(specifier)) return safeFs;
    if (['node:path', 'path'].includes(specifier)) return path;
    if (['node:os', 'os'].includes(specifier)) return { tmpdir: () => path.join(scratch, 'tmp'), platform: os.platform, arch: os.arch };
    if (['node:child_process', 'child_process'].includes(specifier)) return blockedProcess;
    throw Error('TDP_FIXTURE_UNREVIEWED_REQUIRE:' + specifier);
  }
  return { observation, requireReviewed, guard, safeFs };
}
export async function executeOriginalBundle(bundlePath, scratch, { timeoutMs = 30000 } = {}) {
  if (!Number.isInteger(timeoutMs) || timeoutMs < 50 || timeoutMs > 30000) throw Error('TDP_VM_TIMEOUT_INVALID');
  if (fs.existsSync(scratch)) throw Error('TDP_FIXTURE_FRESH_SCRATCH_REQUIRED');
  fs.mkdirSync(scratch, { recursive: true });
  fs.mkdirSync(path.join(scratch,'tmp'));
  const boundary = createIoBoundary(scratch), logs = [], handles = new Map();
  let exitResolve, processExitObserved = false;
  const originalExit = new Promise(resolve => { exitResolve = resolve; });
  const fakeProcess = { exit(code) { processExitObserved = true; fakeProcess.exitCode = Number.isInteger(code) ? code : 2; exitResolve(); }, argv: [process.execPath, bundlePath, scratch], env: { NODE_ENV: 'production' },
    versions: { node: process.versions.node }, version: process.version, platform: process.platform,
    cwd: () => scratch, pid: process.pid, exitCode: 0, nextTick: queueMicrotask };
  const clear = handle => {
    const kind = handles.get(handle);
    if (kind === 'immediate') clearImmediate(handle); else if (kind === 'interval') clearInterval(handle); else clearTimeout(handle);
    handles.delete(handle);
  };
  const once = (kind, fn, milliseconds, ...args) => {
    let handle;
    const callback = () => { handles.delete(handle); fn(...args); };
    handle = kind === 'immediate' ? setImmediate(callback) : setTimeout(callback, milliseconds);
    handles.set(handle, kind); return handle;
  };
  const module = { exports: {} };
  const context = vm.createContext({ module, exports: module.exports, require: boundary.requireReviewed, process: fakeProcess,
    Buffer, URL, URLSearchParams, TextEncoder, TextDecoder, AbortController, performance, crypto: webcrypto,
    structuredClone, queueMicrotask,
    setImmediate: (fn, ...args) => once('immediate', fn, 0, ...args), clearImmediate: clear,
    setTimeout: (fn, ms, ...args) => once('timeout', fn, ms, ...args), clearTimeout: clear,
    setInterval: (fn, ms, ...args) => { const handle = setInterval(fn, ms, ...args); handles.set(handle, 'interval'); return handle; }, clearInterval: clear,
    console: Object.fromEntries(['log','warn','error'].map(name => [name, (...values) => logs.push(values.map(String).join(' '))])),
    fetch() { boundary.observation.networkAttempts++; throw Error('TDP_FIXTURE_NETWORK_FORBIDDEN'); },
  }, { codeGeneration: { strings: false, wasm: false } });
  context.global = context;
  let timer, thrown = null;
  try {
    const completion = new vm.Script(fs.readFileSync(bundlePath, 'utf8'), { filename: bundlePath }).runInContext(context, { timeout: timeoutMs });
    await Promise.race([originalExit, new Promise((_resolve, reject) => { timer = setTimeout(() => reject(Error('TDP_ORIGINAL_TIMEOUT')), timeoutMs); })]);
  } catch (error) { thrown = String(error.message ?? error); }
  finally { clearTimeout(timer); for (const handle of [...handles.keys()]) clear(handle); }
  const reportPath = path.join(scratch, 'report.json');
  let report = null;
  if (fs.existsSync(reportPath)) report = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
  return { exitCode: fakeProcess.exitCode, processExitObserved, thrown, reportPath, report, boundary: boundary.observation, logs,
    execution: 'Original unchanged void-main suite signaled its own process.exit after cleanup in the reviewed VM; product IPC is the original fixture, not an OS process.' };
}
