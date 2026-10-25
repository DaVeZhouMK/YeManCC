import path from 'node:path';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createHash} from 'node:crypto';
// Required OS locations only. No inherited compiler, Node, Python or fixture
// override can choose another source, toolchain, import path or output folder.
const WINDOWS_INPUTS = Object.freeze(['SystemRoot', 'windir', 'SystemDrive', 'ComSpec', 'OS',
  'PROCESSOR_ARCHITECTURE', 'PROCESSOR_ARCHITEW6432', 'NUMBER_OF_PROCESSORS',
  'ProgramFiles', 'ProgramFiles(x86)', 'ProgramW6432', 'CommonProgramFiles',
  'CommonProgramFiles(x86)', 'CommonProgramW6432', 'ProgramData', 'LOCALAPPDATA',
  'APPDATA', 'USERPROFILE', 'USERNAME', 'HOMEDRIVE', 'HOMEPATH', 'PATHEXT']);
export function consumerEnvironment({ambient, tools, out, nodeExecutable}) {
  const primary = tools.primary;
  assert(primary?.git && primary?.node && primary?.compiler, 'PRIMARY_TOOLS_REQUIRED');
  assert.equal(path.resolve(nodeExecutable).toLowerCase(), path.resolve(primary.node).toLowerCase(), 'PRIMARY_NODE_PATH_MISMATCH');
  for (const [role, file] of Object.entries(primary)) {
    const row = tools.files.find(r => path.resolve(r.path).toLowerCase() === path.resolve(file).toLowerCase());
    assert(row && createHash('sha256').update(fs.readFileSync(file)).digest('hex') === row.sha256, 'PRIMARY_TOOL_HASH_MISMATCH:' + role);
  }
  const entries = new Map(Object.entries(ambient).map(([key, value]) => [key.toLowerCase(), value]));
  const env = {};
  for (const key of WINDOWS_INPUTS) if (entries.has(key.toLowerCase())) env[key] = entries.get(key.toLowerCase());
  const windows = env.SystemRoot || 'C:/Windows';
  return Object.assign(env, {
    TEMP: path.join(out, 'tool-temp'), TMP: path.join(out, 'tool-temp'),
    YMCC_MSI_RAW_OUT: path.join(out, 'consumers'), YMCC_PYTHON_PATH: primary.python,
    YMCC_GIT_PATH: primary.git, YMCC_NODE_PATH: primary.node, YMCC_CL_PATH: primary.compiler,
    PYTHONNOUSERSITE: '1', PYTHONIOENCODING: 'utf-8', PYTHONDONTWRITEBYTECODE: '1',
    VSCMD_SKIP_SENDTELEMETRY: '1',
    PATH: [...new Set([path.dirname(primary.node), path.dirname(primary.python), path.dirname(primary.git),
      path.dirname(primary.compiler), path.join(windows, 'System32'), windows])].join(';'),
  });
}