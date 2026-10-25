'use strict';
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto'), Module = require('node:module'), { fileURLToPath } = require('node:url');
const nativeRead = fs.readFileSync.bind(fs), nativeWrite = fs.writeFileSync.bind(fs), nativeMkdir = fs.mkdirSync.bind(fs), asyncRead = fs.promises.readFile.bind(fs.promises), originalLoad = Module._load;
const configFile = process.env.YMCC_LIBRARY_OBSERVER_CONFIG;
if (!configFile || !path.isAbsolute(configFile)) throw Error('LIBRARY_OBSERVER_CONFIG_REQUIRED');
const cfg = JSON.parse(nativeRead(configFile, 'utf8')), root = fs.realpathSync(cfg.caseRoot);
const digest = data => crypto.createHash('sha256').update(data).digest('hex');
const key = file => fs.realpathSync(file).toLowerCase();
const allowed = new Map(cfg.reads.map(row => [key(row.path), row]));
const trace = { sourceReads: [], toolLoads: [], writes: [], forbidden: [], producer: cfg.producer, inputConfig: configFile, physicalIo: false };
const within = file => { const r = path.relative(root, path.resolve(file)); return r === '' || (!path.isAbsolute(r) && r !== '..' && !r.startsWith('..' + path.sep)); };
function filePath(value) { return value instanceof URL ? fileURLToPath(value) : typeof value === 'string' ? path.resolve(value) : null; }
function deny(kind, value) { trace.forbidden.push({ kind, value: typeof value === 'string' ? value : null }); throw Error('LIBRARY_OBSERVER_FORBIDDEN_' + kind); }
function checkedRead(file, kind) {
  const full = filePath(file); if (!full) return;
  const row = allowed.get(key(full)); if (!row) deny('READ', full);
  const actual = digest(nativeRead(full)); if (actual !== row.sha256) deny('READ_IDENTITY', full);
  trace[kind].push({ path: full, realPath: fs.realpathSync(full), sha256: actual, role: row.role });
}
function checkedWrite(file) {
  const full = filePath(file); if (!full || !within(full)) deny('WRITE', full);
  let cursor = full; while (!fs.existsSync(cursor)) cursor = path.dirname(cursor);
  if (!within(fs.realpathSync(cursor))) deny('WRITE_REDIRECTION', full);
  trace.writes.push(full);
}
fs.readFileSync = function(file, ...args) { checkedRead(file, 'sourceReads'); return nativeRead(file, ...args); };
fs.promises.readFile = async function(file, ...args) { checkedRead(file, 'sourceReads'); return asyncRead(file, ...args); };
fs.writeFileSync = function(file, ...args) { checkedWrite(file); return nativeWrite(file, ...args); };
fs.mkdirSync = function(file, ...args) { checkedWrite(file); return nativeMkdir(file, ...args); };
for (const name of ['appendFileSync','unlinkSync','rmSync','rmdirSync']) { const original = fs[name].bind(fs); fs[name] = function(file, ...args) { checkedWrite(file); return original(file, ...args); }; }
for (const name of ['writeFile','appendFile','mkdir','unlink','rm','rmdir','rename','copyFile']) if (fs.promises[name]) fs.promises[name] = async () => deny('UNREVIEWED_ASYNC_FS', name);
Module._load = function(name, parent, isMain) { const resolved = Module._resolveFilename(name, parent, isMain); if (typeof resolved === 'string' && path.isAbsolute(resolved)) checkedRead(resolved, 'toolLoads'); return originalLoad.apply(this, arguments); };
for (const name of ['node:http','node:https','node:net','node:tls','node:dgram']) { const value = require(name); for (const member of ['request','get','connect','createConnection','createServer','createSocket']) if (typeof value[member] === 'function') value[member] = () => deny('NETWORK', name + ':' + member); }
const processes = require('node:child_process'); for (const name of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork']) processes[name] = () => deny('NESTED_PROCESS', name);
Module.syncBuiltinESMExports();
process.on('exit', () => { nativeMkdir(path.dirname(cfg.trace), { recursive: true }); nativeWrite(cfg.trace, JSON.stringify(trace, null, 2) + '\n'); });