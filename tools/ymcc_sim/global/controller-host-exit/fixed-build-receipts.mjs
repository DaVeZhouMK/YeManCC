import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const norm = file => path.win32.resolve(file).toLowerCase();
const parse = bytes => JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, ''));
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const profiles = ['Release', 'Checked'];
const generatedParent = new Set(['environment.hpp', 'actual_pipe_leaves.hpp', 'actual_host_handle_globals.hpp']
 .map(name => 'tools/fixtures/input_host_native_exit_parent/' + name));
export const fixedNativeExitMode = 'current-fixed-native-build-v1';

function readPin(pin, expected = pin?.path) {
 assert(pin && typeof pin.path === 'string' && path.isAbsolute(pin.path) && norm(pin.path) === norm(expected),
  'NATIVE_EXIT_FIXED_PATH');
 const stat = fs.lstatSync(pin.path);
 assert(stat.isFile() && !stat.isSymbolicLink() && norm(fs.realpathSync.native(pin.path)) === norm(expected),
  'NATIVE_EXIT_FIXED_REDIRECTED:' + pin.path);
 const bytes = fs.readFileSync(pin.path);
 assert(bytes.length === pin.bytes && digest(bytes) === pin.sha256, 'NATIVE_EXIT_FIXED_PIN:' + pin.path);
 return bytes;
}

function under(file, root) {
 return norm(file).startsWith(norm(root) + '\\');
}

export function fixedBuildSelection(route, gateRoutes) {
 return profiles.flatMap(profile => (route === 'gates' ? Object.keys(gateRoutes) : [route]).map(variant => ({profile, route: variant})));
}

export function validateFixedBuildGraphs(result, ctx, output, route, selection, expectedLaunches) {
 const fixed = result.fixedNativeBuild, tools = ctx.tools, compiler = result.compiler;
 assert(fixed && fixed.profileSha256 === tools.profileSha256 && fixed.scenarioBinariesLaunched === expectedLaunches &&
  compiler?.vcvarsExecuted === false && compiler.sdkVersion === tools.sdkVersion &&
  !Object.hasOwn(compiler, 'vcvarsSha256'), 'NATIVE_EXIT_FIXED_PROFILE_OR_LAUNCHES');
 const root = route === 'gates' ? result.run : output;
 assert(path.isAbsolute(root) && (route !== 'gates' || norm(path.dirname(root)) === norm(output)), 'NATIVE_EXIT_FIXED_ROUTE_ROOT');
 const environment = fixed.environment, windows = path.dirname(path.dirname(tools.primary.commandProcessor));
 const expectedEnv = {SystemRoot: windows, windir: windows, ComSpec: tools.primary.commandProcessor,
  TEMP: path.join(root, 'temp'), TMP: path.join(root, 'temp'), VSLANG: '1033', WindowsSDKVersion: tools.sdkVersion,
  PATH: [path.dirname(tools.primary.compiler), path.join(windows, 'System32'), windows].join(';'),
  INCLUDE: tools.native.include.join(';'), LIB: tools.native.lib.join(';')};
 assert.deepEqual(Object.keys(environment ?? {}).sort(), Object.keys(expectedEnv).sort(), 'NATIVE_EXIT_FIXED_ENV_FIELDS');
 for (const [key, value] of Object.entries(expectedEnv)) {
  assert.equal(environment[key].toLowerCase(), value.toLowerCase(), 'NATIVE_EXIT_FIXED_ENV:' + key);
 }
 const admitted = new Map(tools.files.map(pin => [norm(pin.path), pin.sha256]));
 const toolRoles = ['commandProcessor', 'compiler', 'dumpbin', 'linker'];
 assert.deepEqual(Object.keys(fixed.tools ?? {}).sort(), toolRoles, 'NATIVE_EXIT_FIXED_TOOL_ROLES');
 for (const role of toolRoles) {
  const pin = fixed.tools[role], expected = tools.primary[role];
  assert(norm(pin.path) === norm(expected) && admitted.get(norm(expected)) === pin.sha256,
   'NATIVE_EXIT_FIXED_TOOL:' + role);
 }
 assert(norm(compiler.path) === norm(fixed.tools.compiler.path) && compiler.bytes === fixed.tools.compiler.bytes &&
  compiler.sha256 === fixed.tools.compiler.sha256, 'NATIVE_EXIT_FIXED_COMPILER');
 assert(Array.isArray(fixed.graphs) && fixed.graphs.length === selection.length, 'NATIVE_EXIT_FIXED_GRAPH_COUNT');
 const graphs = new Map(fixed.graphs.map(pin => [norm(pin.path), pin]));
 assert.equal(graphs.size, selection.length, 'NATIVE_EXIT_FIXED_DUPLICATE_GRAPH');
 const snapshot = path.join(root, 'source');
 const observedInputs = new Map();
 function verifyInput(pin) {
  const key = norm(pin.path);
  const prior = observedInputs.get(key);
  if (prior) assert.equal(pin.bytes + ':' + pin.sha256, prior, 'NATIVE_EXIT_FIXED_OBSERVED_INPUT_CHANGED');
  else { readPin(pin); observedInputs.set(key, pin.bytes + ':' + pin.sha256); }
 }
 return selection.map(expected => {
  const folder = path.join(root, route === 'gates' ? expected.profile + '-' + expected.route : expected.profile);
  const graphFile = path.join(folder, 'FIXED-BUILD-GRAPH.json');
  const graph = parse(readPin(graphs.get(norm(graphFile)), graphFile));
  assert(graph.profile === expected.profile && graph.route === expected.route, 'NATIVE_EXIT_FIXED_GRAPH_ID');
  const binaryPath = path.join(folder, {helper: 'suite.exe', parent: 'fixture.exe', gates: 'gates.exe'}[route]);
  const tuPath = route === 'parent' ? path.join(root, 'fixture.cpp') : path.join(folder, route === 'helper' ? 'suite.cpp' : 'gates.cpp');
  const depsPath = path.join(folder, 'deps.json');
  const deps = parse(readPin(graph.dependencies, depsPath)).Data;
  const args = parse(readPin(graph.command, path.join(folder, 'COMPILE-ARGS.json')));
  const link = args.indexOf('/link');
  assert(norm(args[0]) === norm(tools.primary.compiler) && args.some(arg => norm(arg) === norm(tuPath)) &&
   args.some(arg => arg.toLowerCase() === ('/Fe' + binaryPath).toLowerCase()) &&
   args.indexOf('/sourceDependencies') > 0 && args.indexOf('/sourceDependencies') < link &&
   norm(args[args.indexOf('/sourceDependencies') + 1]) === norm(depsPath) &&
   link > 0 && args.slice(link + 1).includes('/VERBOSE:LIB'), 'NATIVE_EXIT_FIXED_COMMAND');
  assert(norm(deps.Source) === norm(tuPath), 'NATIVE_EXIT_FIXED_TU');
  readPin(graph.translationUnit, tuPath);
  readPin(graph.binary, binaryPath);
  const rawLog = readPin(graph.buildLog, path.join(folder, 'build.log')).toString('utf8');
  const found = rawLog.split(/\r?\n/).map(line => line.trim())
   .filter(line => line.startsWith('Searching ') && line !== 'Searching libraries');
  const searched = [...new Set(found.map(line => {
   const match = /^Searching (.+\.lib):$/i.exec(line);
   assert(match && path.isAbsolute(match[1]), 'NATIVE_EXIT_FIXED_LINK_LOG');
   return norm(match[1]);
  }))];
  assert(searched.length && rawLog.includes('Finished searching libraries'), 'NATIVE_EXIT_FIXED_LINK_LOG');
  assert.deepEqual(graph.linkSearchLibraries.map(pin => norm(pin.path)), searched, 'NATIVE_EXIT_FIXED_LINK_ROWS');
  assert.deepEqual(graph.headers.map(pin => norm(pin.path)), deps.Includes.map(norm), 'NATIVE_EXIT_FIXED_INCLUDE_ROWS');
  for (const pin of graph.linkSearchLibraries) {
   assert(tools.native.lib.some(dir => under(pin.path, dir)) && admitted.get(norm(pin.path)) === pin.sha256,
    'NATIVE_EXIT_FIXED_UNDECLARED_LIBRARY:' + pin.path);
   verifyInput(pin);
  }
  const seenLocal = new Set();
  for (const pin of graph.headers) {
   if (under(pin.path, snapshot)) {
    const relative = path.win32.relative(snapshot, pin.path).replaceAll('\\', '/').toLowerCase();
    assert(ctx.sources[relative] === pin.sha256 || (route === 'parent' && generatedParent.has(relative)),
     'NATIVE_EXIT_FIXED_UNDECLARED_LOCAL_HEADER:' + pin.path);
    seenLocal.add(relative);
   } else assert(tools.native.include.some(dir => under(pin.path, dir)) && admitted.get(norm(pin.path)) === pin.sha256,
    'NATIVE_EXIT_FIXED_UNDECLARED_HEADER:' + pin.path);
   verifyInput(pin);
  }
  const required = route === 'helper' ? ['native/input_host_exit_retirement.h', 'native/input_host_process_retirement.h'] :
   route === 'parent' ? ['native/input_host_exit_owner_runtime.h', ...generatedParent] : ['native/exit_cleanup_wait.h'];
  assert([...required, 'deps/json/json.hpp'].every(name => seenLocal.has(name)), 'NATIVE_EXIT_FIXED_LOCAL_HEADER_SET');
  if (expectedLaunches) {
   const row = result.profiles.find(p => route === 'gates' ? p.profile === expected.profile && p.route === expected.route : p.name === expected.profile);
   assert(row && graph.binary.sha256 === (row.binarySha256 ?? row.executableSha256) && (route !== 'gates' ||
    (row.cppSha256 === graph.translationUnit.sha256 && row.sourceDependenciesSha256 === graph.dependencies.sha256)),
   'NATIVE_EXIT_FIXED_EXECUTED_BINARY');
  }
  return {...expected, includes: graph.headers.length, libraries: graph.linkSearchLibraries.length};
 });
}
