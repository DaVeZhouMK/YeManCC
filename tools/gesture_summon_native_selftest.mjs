import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
const root=process.cwd().replaceAll('\\','/');
const out=path.resolve(process.argv[2]||'../../Build/Validation/GestureSummon-20261018/native-tests').replaceAll('\\','/');
fs.mkdirSync(out,{recursive:true});
const recipe=[
 '@echo off',
 'set "PATH=C:\\Windows\\System32;C:\\Windows;%PATH%"',
 'call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat"',
 'if errorlevel 1 exit /b 1',
 `cd /d "${out}"`,
 `cl /nologo /std:c++20 /utf-8 /EHsc /MT /bigobj "${root}/tools/gesture_summon_selftest.cpp" /I"${root}/native" /I"%WindowsSdkDir%Include\\%WindowsSDKVersion%cppwinrt" /Fe:gesture-test.exe /link user32.lib gdi32.lib ole32.lib`,
 'if errorlevel 1 exit /b 1',
 'gesture-test.exe > result.json',
 'exit /b %errorlevel%',
].join('\r\n');
fs.writeFileSync(out+'/run.cmd',recipe);
const run=spawnSync('cmd.exe',['/d','/c',out+'/run.cmd'],{encoding:'utf8',windowsHide:true});
fs.writeFileSync(out+'/build.log',(run.stdout||'')+(run.stderr||''));
assert.equal(run.status,0,run.stdout+'\n'+run.stderr);
const report=JSON.parse(fs.readFileSync(out+'/result.json','utf8'));
assert.equal(report.failures,0);console.log('GESTURE_NATIVE_OK '+JSON.stringify(report));
