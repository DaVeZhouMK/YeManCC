#!/usr/bin/env python3
"""Pure stored desktop VDF summary, frozen fixture, Release/Checked import audit.

Writes only SteamDesktopBindings-20261015. Never executes the product, Steam,
COM, input APIs, or any live cache/endpoint. The only installed file read is the
explicit built-in template, once, if the frozen known-hash copy does not exist.
"""
from __future__ import annotations
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import traceback
sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
OUT = REPO.parents[2] / 'Build/Validation/SteamDesktopBindings-20261015'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
INPUTS = ['native/steam_desktop_binding_evidence.h','native/steamdeck_mouse_vdf.h',
          'tools/steam_desktop_binding_evidence_selftest.cpp','tools/steam_desktop_binding_evidence_selftest.py','deps/json/json.hpp']
FIXTURE_SHA = '728d8d142b430a805fa1d63f44b701b9bed5b1cb9d2dd5c0fb8bd3a3cb2c1934'
PROFILES = [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]
BANNED = ['user32.dll','win32u.dll','ole32.dll','oleaut32.dll','combase.dll','shell32.dll',
          'winhttp.dll','wininet.dll','ws2_32.dll','steam_api','dinput8.dll','sendinput',
          'keybd_event','mouse_event','getasynckeystate','getkeyboardstate','setwindowshookex',
          'registerrawinputdevices','cocreateinstance','coinitialize','createprocessw']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path = Path(path).resolve()
    if not path.is_relative_to(OUT.resolve()):
        raise AssertionError('write outside authorized validation directory')
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data,str) else data)


def save(path, data):
    write(path,json.dumps(data,ensure_ascii=False,indent=2)+'\n')


def source_audit(sources):
    header=sources[INPUTS[0]].decode('utf-8-sig')
    code=re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',' ',header)
    forbidden=r'\b(?:SendInput|keybd_event|mouse_event|GetAsyncKeyState|GetKeyboardState|GetForegroundWindow|CreateProcess\w*|CreateThread|Sleep|SetTimer|SteamClient|CoCreateInstance|CoInitialize\w*|fopen|ifstream|ofstream|fstream|filesystem|WinHttp\w*|socket|system|popen)\b'
    hits=re.findall(forbidden,code)
    if hits:raise AssertionError(('forbidden side effects in pure header',hits))
    includes=re.findall(r'^\s*#include\s*[<"]([^>"]+)[>"]',header,re.M)
    allowed={'json.hpp','steamdeck_mouse_vdf.h','cstdint','initializer_list','utility','map','set','string_view'}
    if not includes or any(name not in allowed for name in includes):
        raise AssertionError(('unreviewed dependency in pure header',includes))
    if 'Vdf(layoutText).parse()' not in header:raise AssertionError('existing bounded Vdf parser not reused')
    if 'desktopLayout(' in header:raise AssertionError('sensitivity parser must not constrain custom layouts')
    if re.search(r'\bstatic\s+(?!constexpr)',code):raise AssertionError('static mutable state in pure header')
    return {'pass':True,'pureHeader':True,'reusesBoundedVdf':True,'noSensitivityParser':True,
            'liveSteamCalls':0,'OSInputCalls':0,'mutableGlobalState':False}


def build_and_run(run,name,flags,fixture):
    exe=run/(name+'.exe');imports=run/(name+'-imports.txt');symbols=run/(name+'-symbols.txt')
    args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/permissive-',*flags,
          str(run/'source/tools/steam_desktop_binding_evidence_selftest.cpp'),
          '/I'+str(run/'source/deps/json'),'/Fo'+str(run/(name+'.obj')),
          '/Fd'+str(run/(name+'-compiler.pdb')),'/Fe'+str(exe),'/link','/INCREMENTAL:NO',
          '/NODEFAULTLIB:user32.lib','/NODEFAULTLIB:ole32.lib','/NODEFAULTLIB:oleaut32.lib','/NODEFAULTLIB:shell32.lib']
    batch='@echo off\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
    batch+=subprocess.list2cmdline(args)+'\nif errorlevel 1 exit /b %errorlevel%\n'
    batch+='dumpbin /imports "'+str(exe)+'" > "'+str(imports)+'"\nif errorlevel 1 exit /b %errorlevel%\n'
    batch+='dumpbin /symbols "'+str(run/(name+'.obj'))+'" > "'+str(symbols)+'"\nexit /b %errorlevel%\n'
    bat=run/(name+'.cmd');write(bat,batch)
    temp=run/'temp';temp.mkdir(exist_ok=True)
    env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp),PYTHONDONTWRITEBYTECODE='1')
    p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,env=env,capture_output=True,encoding='utf-8',errors='replace',timeout=120)
    write(run/(name+'-compile.log'),p.stdout+p.stderr)
    result={'profile':name,'flags':flags,'pass':False,'compileExitCode':p.returncode,'executableRun':False}
    if p.returncode:result['error']='compile/import dump failed';return result
    imp=imports.read_text(errors='replace').lower();sym=symbols.read_text(errors='replace').lower()
    banned=[term for term in BANNED if term in imp]
    direct=[term for term in ['__imp_sendinput','__imp_getasynckeystate','__imp_cocreateinstance','steamclient'] if term in sym]
    audit={'pass':not banned and not direct,'forbiddenImports':banned,'forbiddenObjectReferences':direct,
           'dlls':re.findall(r'(?m)^\s+([\w.-]+\.dll)\s*$',imp),'exeSha256':sha(exe.read_bytes()),
           'importsSha256':sha(imports.read_bytes()),'user32Imported':'user32.dll' in imp}
    save(run/(name+'-import-audit.json'),audit);result['importAudit']=audit
    if not audit['pass']:result['error']='forbidden import; binary not run';return result
    p=subprocess.run([str(exe),str(fixture)],cwd=run,env=env,capture_output=True,encoding='utf-8',errors='replace',timeout=40)
    write(run/(name+'-result.json'),p.stdout);write(run/(name+'-stderr.txt'),p.stderr)
    result.update(executableRun=True,testExitCode=p.returncode)
    try:result['test']=json.loads(p.stdout)
    except ValueError:result['error']='invalid fixture output';return result
    result['pass']=p.returncode==0 and result['test']['pass'] is True
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--template',type=Path,default=Path('D:/Game/Steam/controller_base/desktop_neptune.vdf'))
    options=parser.parse_args()
    run=OUT/('run-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()))
    run.mkdir(parents=True,exist_ok=False)
    result={'pass':False,'run':str(run),'profiles':[],'liveSteamCalls':0,'productExecuted':False,'fieldProducer':'unknown','productionIntegrationChanged':False}
    try:
        sources={name:(REPO/name).read_bytes() for name in INPUTS}
        result['sourceAudit']=source_audit(sources)
        result['sources']=[{'path':name,'sha256':sha(data),'bytes':len(data)} for name,data in sources.items()]
        for name,data in sources.items():write(run/'source'/name,data)
        fixture=OUT/'frozen/desktop_neptune.vdf'
        data=fixture.read_bytes() if fixture.exists() else options.template.read_bytes()
        if sha(data)!=FIXTURE_SHA:raise AssertionError('built-in desktop fixture does not match the frozen evidence hash')
        if not fixture.exists():write(fixture,data)
        write(run/'fixture/desktop_neptune.vdf',data)
        result['fixture']={'frozenPath':str(fixture),'sourcePathOnFirstFreeze':str(options.template),'sha256':sha(data),
                           'bytes':len(data),'formatVersion':3,'revision':36,'installedPackageVersionContext':'1791918634',
                           'packageContextSource':'previous static report; no live version query; no publisher archive reauthentication',
                           'fieldActiveLayoutProven':False}
        save(OUT/'frozen/receipt.json',result['fixture'])
        for name,flags in PROFILES:
            row=build_and_run(run,name,flags,run/'fixture/desktop_neptune.vdf');result['profiles'].append(row)
            print(name+': '+('PASS' if row['pass'] else 'FAIL')+', '+str(row.get('test',{}).get('checks',0))+' checks',flush=True)
            if not row['pass']:break
        result['sourcesUnchanged']=all((REPO/name).read_bytes()==data for name,data in sources.items())
        result['pass']=len(result['profiles'])==2 and all(p['pass'] for p in result['profiles']) and result['sourcesUnchanged']
    except Exception as exc:
        result['error']=str(exc);write(run/'error.txt',traceback.format_exc())
    save(run/'result.json',result);save(OUT/'latest.json',{'run':str(run),'pass':result['pass']})
    print('Evidence: '+str(run),flush=True)
    return 0 if result['pass'] else 1


if __name__=='__main__':raise SystemExit(main())
