import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/');
const out=path.resolve(process.argv[2]||'../../Build/Validation/GestureSummon-20261018/idle').replaceAll('\\','/');
fs.mkdirSync(out,{recursive:true});
const recipe=['@echo off','set "PATH=C:\\Windows\\System32;C:\\Windows;%PATH%"','call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat"','if errorlevel 1 exit /b 1',`cd /d "${out}"`,`cl /nologo /std:c++20 /utf-8 /EHsc /MT /bigobj "${root}/tools/gesture_summon_idle_selftest.cpp" /I"${root}/native" /I"${root}/deps/json" /I"%WindowsSdkDir%Include\\%WindowsSDKVersion%cppwinrt" /Fe:idle-test.exe /link user32.lib gdi32.lib ole32.lib`,'if errorlevel 1 exit /b 1','idle-test.exe > result.json','exit /b %errorlevel%'].join('\r\n');
fs.writeFileSync(out+'/run.cmd',recipe);
const run=spawnSync('cmd.exe',['/d','/c',out+'/run.cmd'],{encoding:'utf8',windowsHide:true});
fs.writeFileSync(out+'/build.log',(run.stdout||'')+(run.stderr||''));
if(!fs.existsSync(out+'/result.json')){console.error(run.stdout,run.stderr);process.exit(run.status||1)}
const report=JSON.parse(fs.readFileSync(out+'/result.json','utf8'));
const ok=run.status===0&&report.ok;console.log(JSON.stringify(report,null,2));process.exit(ok?0:1);
