// Offline calibration fixtures only. Child shell environment; no global changes.
import fs from 'node:fs';import path from 'node:path';
export function calibrationToolchainPreamble(vcvars) {
 const windows=process.env.SystemRoot||'C:/Windows',sdk='C:/Program Files (x86)/Windows Kits/10';
 const versions=fs.readdirSync(path.join(sdk,'Include')).filter(v=>/^\d+(?:\.\d+){3}$/.test(v)).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}));
 const version=versions.find(v=>['Include/'+v+'/ucrt/crtdbg.h','Include/'+v+'/um/Windows.h','Lib/'+v+'/ucrt/x64/ucrt.lib','Lib/'+v+'/um/x64/kernel32.lib'].every(f=>fs.existsSync(path.join(sdk,f))));
 if(!version||!fs.existsSync(vcvars))throw Error('Complete installed x64 MSVC/Windows SDK required');
 for(const p of [windows,sdk,vcvars])if(/["%\r\n]/.test(p))throw Error('Unsupported toolchain path');
 const include=['ucrt','shared','um','winrt','cppwinrt'].map(f=>path.join(sdk,'Include',version,f)).join(';');
 const libs=['ucrt','um'].map(f=>path.join(sdk,'Lib',version,f,'x64')).join(';');
 return ['@echo off',`call "${vcvars}" >nul`,'if errorlevel 1 exit /b 1',`set "PATH=${path.join(windows,'System32')};${windows};%VCToolsInstallDir%bin\\Hostx64\\x64;%PATH%"`,`set "WindowsSdkDir=${sdk}/"`,`set "WindowsSDKVersion=${version}/"`,`set "INCLUDE=${include};%INCLUDE%"`,`set "LIB=${libs};%LIB%"`].join('\r\n')+'\r\n';
}
