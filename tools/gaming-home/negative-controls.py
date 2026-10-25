"""Compile actual controller regression mutations. Only inert test executables run."""
import json,pathlib,subprocess,sys,zipfile,importlib.util
root=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
source=(root/'native/GamingHome/flow.h').read_text(encoding='utf-8-sig')
mutations={
 'duplicate-activation':('    if (!p.lock()) return Result::Busy;','    (void)p.lock();'),
 'desktop-launch':('    if (!s.active) return Result::OutsideFse;','    /* broken: desktop admission omitted */'),
 'missing-cancellation':('            if (s.cancelled || !s.active) return Result::Cancelled;','            /* broken: cancellation omitted */'),
 'duplicate-ymcc':('    if (!s.ymcc) {','    if (true) {'),
 'window-desktop-style':('visible && sameSession && ownedImage && sdlClass && appWindow && !resizable &&','visible && sameSession && ownedImage && sdlClass && appWindow &&'),
}
expected={
 'duplicate-activation':'duplicate activation',
 'desktop-launch':'desktop entry',
 'missing-cancellation':'cancel during YMCC wait',
 'duplicate-ymcc':'cold once through original BAT',
 'window-desktop-style':'resizable Steam desktop rejected',
}
rows=[]
for name,(before,after) in mutations.items():
    if source.count(before)!=1:raise RuntimeError('mutation anchor '+name)
    d=out/name;d.mkdir(exist_ok=True);(d/'flow.h').write_text(source.replace(before,after,1),encoding='utf-8')
    cmd='\r\n'.join([
        '@echo off', 'setlocal',
        'call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul',
        'if errorlevel 1 exit /b 2',
        'set "SDK=C:\\Program Files (x86)\\Windows Kits\\10"',
        'set "INCLUDE=%SDK%\\Include\\10.0.26100.0\\ucrt;%SDK%\\Include\\10.0.26100.0\\shared;%SDK%\\Include\\10.0.26100.0\\um;%INCLUDE%"',
        'set "LIB=%SDK%\\Lib\\10.0.26100.0\\ucrt\\x64;%SDK%\\Lib\\10.0.26100.0\\um\\x64;%LIB%"',
        f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"{d}" "{root / "tools/gaming-home/flow-selftest.cpp"}" /Fo"{d / "test.obj"}" /Fe"{d / "test.exe"}" /link /SUBSYSTEM:CONSOLE /MACHINE:x64',
        'if errorlevel 1 exit /b 2',f'"{d / "test.exe"}"','exit /b %errorlevel%', ''])
    (d/'build.cmd').write_bytes(cmd.encode('utf-8'))
    run=subprocess.run(['cmd.exe','/d','/c',str(d/'build.cmd')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=45)
    text=run.stdout.decode('utf-8',errors='replace');(d/'output.log').write_text(text,encoding='utf-8')
    if run.returncode!=1 or expected[name] not in text:raise RuntimeError(f'negative {name} did not fail at expected business assertion: {run.returncode} {text}')
    rows.append({'name':name,'compiled':True,'expectedExit':1,'assertion':expected[name],'detected':True})
result={'status':'PASS','compiledNegativeControls':rows,'productExecuted':False,'windowsSettingsChanged':False}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
