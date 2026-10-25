'use strict';
// Observation only: retain compiler output/metafile and actual child completion.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const Module = require('node:module'), cp = require('node:child_process');
const originalRead = fs.readFileSync.bind(fs);
const configFile = process.env.YMCC_LIGHTING_OBSERVATION;
if (!configFile || !path.isAbsolute(configFile)) throw Error('LIGHTING_OBSERVER_CONFIG_REQUIRED');
const config = JSON.parse(originalRead(configFile, 'utf8'));
for (const key of ['repo', 'trace', 'evidenceRoot', 'esbuildBinary']) {
  if (!path.isAbsolute(config[key] || '')) throw Error('LIGHTING_OBSERVER_ABSOLUTE_PATH_REQUIRED');
}
fs.mkdirSync(config.trace, {recursive:true});
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const inside = (root, file) => { const rel = path.relative(root, file); return rel === '' || (!rel.startsWith('..' + path.sep) && rel !== '..' && !path.isAbsolute(rel)); };
const report = {schemaVersion:1, pid:process.pid, ppid:process.ppid, entry:path.resolve(process.argv[1]),
  argv:process.argv.slice(1), executable:fs.realpathSync(process.execPath), startedUtc:new Date().toISOString(),
  sources:[], tools:[], generatedInputs:[], children:[], builds:[], drift:[], observationErrors:[], terminal:null};
const sets = {sources:new Map(), tools:new Map(), generatedInputs:new Map()};
const reportPath = path.join(config.trace, `node-${process.pid}.json`);
function persist() {
  for (const key of Object.keys(sets)) report[key] = [...sets[key].values()].sort((a,b) => a.path.localeCompare(b.path));
  fs.writeFileSync(reportPath, JSON.stringify(report, null, 2) + '\n');
}
function record(input, supplied) {
  if (typeof input !== 'string' || !path.isAbsolute(input)) return;
  const file = fs.realpathSync(input);
  if (!fs.statSync(file).isFile()) return;
  const bytes = supplied && Buffer.isBuffer(supplied) ? supplied : originalRead(file);
  const nodeModule = file.split(path.sep).includes('node_modules');
  const group = inside(config.evidenceRoot, file) ? 'generatedInputs' : inside(config.repo, file) && !nodeModule ? 'sources' : 'tools';
  const row = {path:file, sha256:sha(bytes)};
  const prior = sets[group].get(file);
  if (prior && prior.sha256 !== row.sha256) report.drift.push({path:file, before:prior.sha256, after:row.sha256});
  sets[group].set(file, row);
}
fs.readFileSync = function(file, ...args) {
  const data = originalRead(file, ...args);
  try { record(file, Buffer.isBuffer(data) ? data : undefined); } catch(error) { report.observationErrors.push(String(error)); }
  return data;
};
record(report.executable); record(report.entry); record(__filename);
const browserTypes = new WeakSet();
function endCompilerInput(child, row, method) {
  if (row.shutdownMethod) return;
  row.shutdownMethod=method; child.ref(); child.stdout?.ref?.(); child.stderr?.ref?.();
  child.stdin?.ref?.(); child.stdin?.end(); persist();
}
async function closeOwnedCompilerServices() {
  await Promise.all([...services].map(([child,row]) => new Promise((resolve,reject) => {
    if (row.terminal) { resolve(); return; }
    const done = error => { clearTimeout(timer); child.removeListener('close',closed); child.removeListener('error',failed); error ? reject(error) : resolve(); };
    const closed = () => done();
    const failed = error => done(error);
    const timer = setTimeout(() => done(Error('LIGHTING_COMPILER_EOF_CLOSE_TIMEOUT')),5000);
    child.once('close',closed); child.once('error',failed);
    try { endCompilerInput(child,row,'stdin-eof-at-browser-close'); } catch(error) { done(error); }
  })));
}
function observeBrowserClose(actual) {
  const chromium=actual?.chromium;
  if (!chromium || typeof chromium.launch!=='function' || browserTypes.has(chromium)) return actual;
  browserTypes.add(chromium);
  const launch=chromium.launch;
  chromium.launch=async function(...args) {
    const browser=await launch.apply(this,args), close=browser.close;
    browser.close=async function(...closeArgs) {
      const result=await close.apply(this,closeArgs);
      // This is reached by the original fixture's finally, including uncaught assertions.
      // Await real compiler EOF/close before the original exception resumes propagation.
      await closeOwnedCompilerServices();
      return result;
    };
    return browser;
  };
  return actual;
}
const originalLoad = Module._load, wrapped = new WeakMap();
Module._load = function(name, parent, isMain) {
  let resolved;
  try { resolved = Module._resolveFilename(name, parent, isMain); if (path.isAbsolute(resolved)) record(resolved); }
  catch(error) { if (resolved && path.isAbsolute(resolved)) report.observationErrors.push(String(error)); }
  const actual = originalLoad.apply(this, arguments);
  if (name === 'playwright' || name === 'playwright-core') return observeBrowserClose(actual);
  if (name !== 'esbuild') return actual;
  if (wrapped.has(actual)) return wrapped.get(actual);
  const observed = {...actual, async build(options) {
    const built = await actual.build({...options, metafile:true});
    const index = report.builds.length, prefix = `build-${process.pid}-${index}`;
    const inputs = Object.keys(built.metafile.inputs).map(name => {
      if (name.startsWith('<') || name.startsWith('mock:')) return {name, virtual:true};
      const logical = path.resolve(options.absWorkingDir || process.cwd(), name);
      // .vue loaders supply transformed bytes; actual source reads are recorded above.
      if (!name.endsWith('.vue')) record(logical);
      return {name, path:logical, transformed:name.endsWith('.vue')};
    });
    const outputs = built.outputFiles.map((file, i) => {
      const target = path.join(config.trace, `${prefix}-output-${i}.js`);
      fs.writeFileSync(target, file.contents);
      return {path:target, sha256:sha(file.contents), bytes:file.contents.length};
    });
    const metafile = path.join(config.trace, `${prefix}-metafile.json`);
    fs.writeFileSync(metafile, JSON.stringify(built.metafile, null, 2) + '\n');
    report.builds.push({esbuildVersion:actual.version, inputs, outputs, metafile, metafileSha256:sha(originalRead(metafile))});
    persist();
    return built;
  }};
  wrapped.set(actual, observed);
  return observed;
};
function begin(kind, executable, args, options) {
  const file = path.isAbsolute(executable) ? fs.realpathSync(executable) : executable;
  if (path.isAbsolute(file)) record(file);
  const row = {kind, executable:file, args:Array.isArray(args) ? args.slice() : [], cwd:options?.cwd || process.cwd(),
    startedUtc:new Date().toISOString(), finishedUtc:null, pid:null, exitCode:null, signal:null,
    launchError:null, timedOut:false, outputTruncated:false, killRequested:false, terminal:false};
  report.children.push(row); persist(); return row;
}
const originalSpawn = cp.spawn, originalSpawnSync = cp.spawnSync;
const services = new Map();
cp.spawn = function(executable, args, options) {
  const row = begin('spawn', executable, args, options);
  let child;
  try { child = originalSpawn.apply(this, arguments); }
  catch(error) { row.launchError=error.code || String(error); row.finishedUtc=new Date().toISOString(); persist(); throw error; }
  row.pid = child.pid || null;
  if (path.isAbsolute(row.executable) && row.executable === fs.realpathSync(config.esbuildBinary)) services.set(child, row);
  const kill = child.kill;
  child.kill = function(...values) { row.killRequested=true; persist(); return kill.apply(this, values); };
  child.once('error', error => { row.launchError=error.code || String(error); persist(); });
  child.once('close', (code, signal) => {
    row.exitCode=code; row.signal=signal; row.finishedUtc=new Date().toISOString(); row.terminal=true;
    services.delete(child); persist();
  });
  persist(); return child;
};
cp.spawnSync = function(executable, args, options) {
  const row = begin('spawnSync', executable, args, options);
  const result = originalSpawnSync.apply(this, arguments);
  row.pid=result.pid || null; row.exitCode=result.status; row.signal=result.signal;
  row.launchError=result.error?.code || null; row.timedOut=row.launchError === 'ETIMEDOUT';
  row.outputTruncated=row.launchError === 'ENOBUFS'; row.terminal=Number.isInteger(result.status);
  row.finishedUtc=new Date().toISOString();
  for (const stream of ['stdout', 'stderr']) if (result[stream] != null) {
    const target=path.join(config.trace, `child-${process.pid}-${report.children.length}-${stream}.log`);
    fs.writeFileSync(target, result[stream]); row[stream+'File']=target;
  }
  persist(); return result;
};
Module.syncBuiltinESMExports();
// esbuild unrefs its service. Once the original program is otherwise finished,
// close its input normally and retain handles until its real close event arrives.
// No kill, synthetic exit code, modified component, or assertion interception.
process.on('beforeExit', () => {
  for (const [child,row] of services) if (!row.shutdownMethod) {
    endCompilerInput(child,row,'stdin-eof-at-beforeExit');
  }
});
process.on('exit', code => {
  report.terminal={event:'exit',exitCode:code,finishedUtc:new Date().toISOString()}; persist();
});
persist();
