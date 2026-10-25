import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';

// No recursion to the raw/witness runners. Only the fake-boundary binary runs;
// real Windows/HidP declarations are checked by compilation, never execution.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sources = ['native/msi_external_gamepad_decode_windows.h',
  'tools/msi_external_gamepad_decode_selftest.cpp', 'tools/msi_external_gamepad_decode_selftest.mjs',
  'native/msi_external_raw_receipts.h'];
const hash = p => createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const source = sources.map(p => ({ path: path.join(root, p), sha256: hash(path.join(root, p)) }));
const header = fs.readFileSync(path.join(root, sources[0]), 'utf8');
assert(!header.includes('msi_external_raw_receipts_windows.h'), 'No dependency on Windows adapter');
assert(!header.includes('queryRawIdentity'), 'No identity query or adapter declaration dependency');
assert(header.includes('#include <hidpi.h>'), 'Explicit HidP declarations');
assert(!/\b(?:CreateFile[AW]?|ReadFile|WriteFile|RegisterRawInputDevices|SendInput|HidD_\w+|HidP_Set\w+|GetRawInputData)\s*\(/.test(header),
  'Decoder has no device read/write/open/registration/injection APIs');
assert.equal([...header.matchAll(/GetRawInputDeviceInfoW\(rawDevice,\s*(\w+)/g)].map(m => m[1]).join(','),
  'RIDI_PREPARSEDDATA,RIDI_PREPARSEDDATA', 'Only selected RAWINPUT device PPD is queried');
const vcvars = process.env.YMCC_MSI_DECODE_VCVARS ||
  'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars), 'MSVC vcvars64.bat required (override YMCC_MSI_DECODE_VCVARS)');
// Some BuildTools installations omit SDK discovery in vcvars. Resolve installed
// SDK headers/libraries explicitly, without installing or running other tools.
const sdkRoot = process.env.YMCC_MSI_DECODE_SDK || 'C:/Program Files (x86)/Windows Kits/10';
const sdkVersion = fs.readdirSync(path.join(sdkRoot, 'Include')).filter(v =>
  fs.existsSync(path.join(sdkRoot, 'Include', v, 'um/Windows.h')) &&
  fs.existsSync(path.join(sdkRoot, 'Lib', v, 'um/x64/kernel32.lib')))
  .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }))[0];
assert(sdkVersion, 'Installed Windows SDK with x64 headers/libraries required');
const sdkInclude = ['ucrt', 'shared', 'um'].map(p => path.join(sdkRoot, 'Include', sdkVersion, p)).join(';');
const sdkLib = ['ucrt', 'um'].map(p => path.join(sdkRoot, 'Lib', sdkVersion, p, 'x64')).join(';');
const retainOutput = Boolean(process.env.YMCC_MSI_DECODE_OUT);
const out = retainOutput ? path.resolve(process.env.YMCC_MSI_DECODE_OUT) :
  fs.mkdtempSync(path.join(os.tmpdir(), 'ymcc-msi-gamepad-decode-'));
fs.mkdirSync(out, { recursive: true });
const quote = text => {
  assert(!/["\r\n%]/.test(text), 'Path must be safe for cmd batch quoting');
  return `"${text}"`;
};
quote(sdkInclude); quote(sdkLib);
const cpp = path.join(root, sources[1]);
const run = (command, args, logName) => {
  const env = { ...process.env, VSLANG: '1033' };
  for (const k of Object.keys(env)) if (/^(CL|_CL_|LINK|_LINK_)$/i.test(k)) delete env[k];
  const p = spawnSync(command, args, { env, cwd: out, encoding: 'utf8', timeout: 120000, windowsHide: true });
  const text = (p.stdout || '') + (p.stderr || '');
  fs.writeFileSync(path.join(out, logName), text);
  assert.ifError(p.error);
  if (p.status !== 0) process.stdout.write(text);
  assert.equal(p.status, 0, `${path.basename(command)} succeeds`);
  return text;
};
try {
  const results = [];
  for (const [name, flags, compileOnly] of [
    ['production-compile', '/c /DYMCC_MSI_GAMEPAD_DECODE_COMPILE_ONLY /O2', true],
    ['production-lean-compile', '/c /DYMCC_MSI_GAMEPAD_DECODE_COMPILE_ONLY /DWIN32_LEAN_AND_MEAN /O2', true],
    ['fake-debug', '/Od /RTC1', false], ['fake-optimized', '/O2', false]]) {
    const exe = path.join(out, `${name}.exe`);
    const batch = path.join(out, `${name}.cmd`);
    fs.writeFileSync(batch, `@echo off\r\ncall ${quote(vcvars)} >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n` +
      `set "INCLUDE=${sdkInclude};%INCLUDE%"\r\nset "LIB=${sdkLib};%LIB%"\r\n` +
      `cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /MT ${flags} /I${quote(path.join(root, 'deps/json'))} ` +
      `${quote(cpp)} /Fo${quote(path.join(out, `${name}.obj`))} /Fe${quote(exe)}\r\nexit /b %errorlevel%\r\n`);
    run('cmd.exe', ['/d', '/v:off', '/c', batch], `${name}-build.log`);
    if (compileOnly) {
      console.log(`${name}: PASS (self-contained header, real Windows/HidP declarations, never executed)`);
      results.push({ name, compileOnly, passed: true });
      continue;
    }
    const text = run(exe, [], `${name}-result.txt`);
    assert(text.includes('realRawInputCalls=0 realHidCalls=0 RESULT=PASS'), 'Actual helper passes all fake boundaries');
    console.log(`${name}: ${text.trim()}`);
    results.push({ name, compileOnly, passed: true, result: text.trim() });
  }
  assert(source.every(p => hash(p.path) === p.sha256), 'Sources unchanged during verification');
  const receipt = { source, productionHelperExercised: true, realRawInputCalls: 0, realHidCalls: 0,
    physicalDeviceVerified: false, samplerIntegrationTested: false, sdkVersion, results };
  fs.writeFileSync(path.join(out, 'source-binding.json'), JSON.stringify(receipt, null, 2) + '\n');
  console.log(JSON.stringify(receipt));
  if (retainOutput) console.log(`evidenceDirectory=${out}`);
} finally {
  if (!retainOutput) {
    assert(path.dirname(out) === path.resolve(os.tmpdir()) && path.basename(out).startsWith('ymcc-msi-gamepad-decode-'),
      'Cleanup stays in this test-owned temporary directory');
    fs.rmSync(out, { recursive: true, force: true });
  }
}
