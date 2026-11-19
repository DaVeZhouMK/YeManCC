// Read-only inputs for the explicit prehost build. This does not grant a cache
// or attest dynamic compiler/PowerShell backends, DLLs, or a complete library graph.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const win = path.win32;
const INPUT = 'tools/ymcc_sim/global/controller-foreground-latency/prehost-toolchain-inputs.json';
const KIND = 'ymcc-prehost-fixed-toolchain-v1';
const MSVC_VERSION = '14.44.35207';
const SDK_VERSION = '10.0.26100.0';
const POWERSHELL = 'G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe';
const NODE = 'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe';
const FIELDS = ['schemaVersion', 'kind', 'architecture', 'msvcRoot', 'sdkRoot', 'sdkVersion', 'windowsRoot', 'powershell', 'node', 'toolPins'];
const DERIVED = ['compiler', 'linker', 'includePaths', 'libraryPaths', 'cmd', 'msvcBin', 'powershellHome'];
const ROLES = ['compiler', 'linker', 'powershell', 'node'];
const need = (ok, message) => { if (!ok) throw new Error('PREHOST_TOOLCHAIN_' + message); };

function exactFields(value, fields, label) {
  need(value !== null && typeof value === 'object' && !Array.isArray(value), label + '_OBJECT');
  const keys = Object.keys(value);
  need(keys.length === fields.length && fields.every(key => Object.hasOwn(value, key)), label + '_FIELDS');
}

function absolute(value, label) {
  need(typeof value === 'string' && /^[a-z]:[\\/]/i.test(value), label + '_ABSOLUTE_LOCAL_PATH');
  need(!/[\x00-\x1f\x7f<>"|?*;%]/.test(value), label + '_PATH_CHARACTER');
  const parts = value.slice(3).split(/[\\/]/);
  if (parts.at(-1) === '') parts.pop();
  need(parts.every(part => part && part !== '.' && part !== '..' && !/[. :]$|:/.test(part)
    && !/^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part)), label + '_PATH_ALIAS');
  return win.normalize(value);
}

const samePath = (left, right) => win.normalize(left).toLowerCase() === win.normalize(right).toLowerCase();
const inside = (root, file) => {
  const relative = win.relative(root, file);
  return relative === '' || (!win.isAbsolute(relative) && relative !== '..' && !relative.startsWith('..\\'));
};

// lstat catches dangling links as well as live links/junctions. Check every
// existing ancestor and its native realpath, including a not-yet-created root.
function diskPath(value, label, type = 'directory', allowMissing = false) {
  const full = absolute(value, label);
  const root = win.parse(full).root;
  const parts = full.slice(root.length).split('\\').filter(Boolean);
  let current = root;
  let result;
  for (let index = 0; index <= parts.length; index++) {
    if (index) current = win.join(current, parts[index - 1]);
    try { result = fs.lstatSync(current); }
    catch (error) {
      if (allowMissing && error.code === 'ENOENT') return null;
      throw error;
    }
    need(!result.isSymbolicLink(), label + '_REDIRECTION:' + current);
    const real = fs.realpathSync.native(current).replace(/^\\\\\?\\(?=[a-z]:\\)/i, '');
    need(samePath(real, current), label + '_REDIRECTION:' + current);
    if (index < parts.length || type === 'directory') need(result.isDirectory(), label + '_DIRECTORY:' + current);
    else need(result.isFile(), label + '_FILE:' + current);
  }
  return result;
}

function readFile(value, label) {
  const before = diskPath(value, label, 'file');
  const bytes = fs.readFileSync(value);
  const after = diskPath(value, label, 'file');
  need(bytes.length === before.size && ['dev', 'ino', 'size', 'mtimeMs', 'ctimeMs'].every(key => before[key] === after[key]), label + '_CHANGED_DURING_READ');
  return bytes;
}

function declaration(value) {
  exactFields(value, FIELDS, 'INPUT');
  need(value.schemaVersion === 1 && value.kind === KIND, 'SCHEMA');
  need(value.architecture === 'x64', 'ARCHITECTURE');
  const msvcRoot = absolute(value.msvcRoot, 'MSVC');
  const sdkRoot = absolute(value.sdkRoot, 'SDK');
  need(msvcRoot.toLowerCase().endsWith('\\vc\\tools\\msvc\\' + MSVC_VERSION), 'MSVC_VERSION');
  need(sdkRoot.toLowerCase().endsWith('\\windows kits\\10'), 'SDK_ROOT');
  need(value.sdkVersion === SDK_VERSION, 'SDK_VERSION');
  need(samePath(absolute(value.windowsRoot, 'WINDOWS'), 'C:\\Windows'), 'WINDOWS_ROOT');
  need(samePath(absolute(value.powershell, 'POWERSHELL'), POWERSHELL), 'POWERSHELL_PATH');
  need(samePath(absolute(value.node, 'NODE'), NODE), 'NODE_PATH');
  const expected = {
    compiler: win.join(msvcRoot, 'bin/Hostx64/x64/cl.exe'),
    linker: win.join(msvcRoot, 'bin/Hostx64/x64/link.exe'),
    powershell: value.powershell,
    node: value.node,
  };
  need(Array.isArray(value.toolPins) && value.toolPins.length === ROLES.length, 'TOOL_PIN_COUNT');
  const seen = new Set();
  for (const pin of value.toolPins) {
    exactFields(pin, ['role', 'path', 'sha256'], 'TOOL_PIN');
    need(ROLES.includes(pin.role) && !seen.has(pin.role), 'TOOL_PIN_ROLE');
    seen.add(pin.role);
    need(samePath(absolute(pin.path, 'TOOL_PIN'), expected[pin.role]), 'TOOL_PIN_PATH:' + pin.role);
    need(typeof pin.sha256 === 'string' && /^[0-9a-f]{64}$/.test(pin.sha256), 'TOOL_PIN_SHA256:' + pin.role);
  }
  need(ROLES.every(role => seen.has(role)), 'TOOL_PIN_ROLES');
  return value;
}

function derive(value) {
  const msvcBin = win.join(value.msvcRoot, 'bin/Hostx64/x64');
  return {
    ...value,
    compiler: win.join(msvcBin, 'cl.exe'),
    linker: win.join(msvcBin, 'link.exe'),
    includePaths: [...['ucrt', 'shared', 'um', 'winrt', 'cppwinrt'].map(name => win.join(value.sdkRoot, 'Include', value.sdkVersion, name)), win.join(value.msvcRoot, 'include')],
    libraryPaths: [...['ucrt', 'um'].map(name => win.join(value.sdkRoot, 'Lib', value.sdkVersion, name, 'x64')), win.join(value.msvcRoot, 'lib/x64')],
    cmd: win.join(value.windowsRoot, 'System32/cmd.exe'),
    msvcBin,
    powershellHome: win.dirname(value.powershell),
  };
}

/** Synchronously read the fixed declaration and verify its four actual files. */
export function resolvePreHostToolchain(repoRoot) {
  const repo = absolute(repoRoot, 'REPO');
  diskPath(repo, 'REPO');
  const input = win.join(repo, INPUT);
  const config = declaration(JSON.parse(readFile(input, 'INPUT').toString('utf8').replace(/^\uFEFF/, '')));
  const build = derive(config);
  for (const pin of config.toolPins) {
    const actual = crypto.createHash('sha256').update(readFile(pin.path, 'TOOL:' + pin.role)).digest('hex');
    need(actual === pin.sha256, 'TOOL_SHA256_MISMATCH:' + pin.role);
  }
  for (const directory of [build.msvcRoot, build.sdkRoot, build.windowsRoot, build.msvcBin, build.powershellHome,
    win.join(build.windowsRoot, 'System32'), win.join(build.powershellHome, 'Modules'), ...build.includePaths, ...build.libraryPaths]) {
    diskPath(directory, 'BUILD_DIRECTORY');
  }
  for (const file of [build.cmd, win.join(build.sdkRoot, 'Include', build.sdkVersion, 'um/Windows.h'), win.join(build.sdkRoot, 'Lib', build.sdkVersion, 'um/x64/kernel32.lib')]) {
    diskPath(file, 'BUILD_SENTINEL', 'file');
  }
  return build;
}

function resolvedBuild(value) {
  exactFields(value, [...FIELDS, ...DERIVED], 'BUILD');
  const config = declaration(Object.fromEntries(FIELDS.map(key => [key, value[key]])));
  const expected = derive(config);
  for (const key of DERIVED) {
    if (Array.isArray(expected[key])) {
      need(Array.isArray(value[key]) && value[key].length === expected[key].length, 'BUILD_PATH_COUNT:' + key);
      value[key].forEach((item, index) => need(samePath(absolute(item, 'BUILD_PATH'), expected[key][index]), 'BUILD_PATH:' + key));
    } else need(samePath(absolute(value[key], 'BUILD_PATH'), expected[key]), 'BUILD_PATH:' + key);
  }
  return expected;
}

/** Return only strings; inspect existing paths, but create nothing or inherit nothing. */
export function preHostChildEnvironment(build, privateRoot, repoRoot) {
  const fixed = resolvedBuild(build);
  const repo = absolute(repoRoot, 'REPO');
  diskPath(repo, 'REPO');
  const base = absolute(privateRoot, 'PRIVATE_ROOT');
  const core = win.resolve(repo, '../../../Testing/YMCC');
  need(['Evidence', 'Workspace'].some(name => inside(win.join(core, name), base)), 'PRIVATE_ROOT_OUTSIDE_PROJECT_CORE');
  diskPath(base, 'PRIVATE_ROOT', 'directory', true);
  const profile = win.join(base, 'profile');
  const privatePaths = {
    TEMP: win.join(base, 'temp'), TMP: win.join(base, 'temp'), USERPROFILE: profile,
    APPDATA: win.join(profile, 'AppData/Roaming'), LOCALAPPDATA: win.join(profile, 'AppData/Local'),
    YEMAN_POWER_CONTROL_DIR: win.join(base, 'power-control'),
  };
  for (const directory of new Set(Object.values(privatePaths))) diskPath(directory, 'PRIVATE_DIRECTORY', 'directory', true);
  const windowsRoot = absolute(fixed.windowsRoot, 'WINDOWS');
  return {
    SystemRoot: windowsRoot, WINDIR: windowsRoot, SystemDrive: win.parse(windowsRoot).root.slice(0, 2), COMSPEC: fixed.cmd,
    PATHEXT: '.COM;.EXE;.BAT;.CMD',
    PATH: [fixed.msvcBin, fixed.powershellHome, win.join(windowsRoot, 'System32'), windowsRoot].join(';'),
    INCLUDE: fixed.includePaths.join(';'), LIB: fixed.libraryPaths.join(';'), LIBPATH: fixed.libraryPaths.join(';'), VSLANG: '1033',
    PSModulePath: win.join(fixed.powershellHome, 'Modules'),
    POWERSHELL_TELEMETRY_OPTOUT: '1',
    VCToolsInstallDir: absolute(fixed.msvcRoot, 'MSVC') + '\\', VCToolsVersion: MSVC_VERSION,
    WindowsSdkDir: absolute(fixed.sdkRoot, 'SDK') + '\\', WindowsSDKVersion: fixed.sdkVersion + '\\',
    ...privatePaths,
  };
}
