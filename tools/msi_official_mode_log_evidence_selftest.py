#!/usr/bin/env python3
"""Compile the production pure-Ops header, gate imports, run only memory fixtures.

Release/Checked use frozen exact source copies and no real log/known-folder,
Windows input, OEM, Steam, or product calls. All writes stay in the dated
validation directory. The fixture digest is a deterministic fake; hash-argument
identity is tested, cryptographic implementation belongs to the parent adapter.
"""
from __future__ import annotations
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
OUT = REPO.parents[2] / 'Build/Validation/MsiOfficialModeLogEvidence-20261015'
HEADER = 'native/msi_official_mode_log_evidence.h'
FIXTURE = 'tools/msi_official_mode_log_evidence_selftest.cpp'
RUNNER = 'tools/msi_official_mode_log_evidence_selftest.py'
INPUTS = [HEADER, FIXTURE, RUNNER, 'deps/json/json.hpp']
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PROFILES = [('Release', ['/O2', '/DNDEBUG', '/MT']), ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/MTd'])]
BANNED = ['user32.dll', 'win32u.dll', 'ole32.dll', 'oleaut32.dll', 'combase.dll', 'shell32.dll',
          'advapi32.dll', 'winhttp.dll', 'wininet.dll', 'ws2_32.dll', 'setupapi.dll', 'cfgmgr32.dll',
          'hid.dll', 'winusb.dll', 'dinput8.dll', 'xinput', 'steam_api', 'msi.dll',
          'sendinput', 'keybd_event', 'mouse_event', 'getasynckeystate', 'getkeyboardstate',
          'setwindowshookex', 'registerrawinputdevices', 'cocreateinstance', 'coinitialize',
          'createprocess', 'shellexecute']
DIRECT_SYMBOLS = ['sendinput', 'getasynckeystate', 'cocreateinstance', 'createfilew', 'findfirstfile',
                  'deviceiocontrol', 'createthread', 'steamclient', 'xinputgetstate', 'loadlibrary', 'getprocaddress']
PARENT_UI_METADATA = {
    'method': '[Reflection.AssemblyName]::GetAssemblyName (metadata only, no code load)',
    'providedBy': 'parent task', 'performedByRunner': False,
    'simpleName': 'MSI Center M', 'version': '3.0.2605.2101',
    'exeSha256': 'E5A434D2F25564E3719BCCE2CB7717876ED97CAA30DBDCF96DA2875594C2823A',
    'installedG3EVersionMatched': False,
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path = Path(path).resolve()
    if not path.is_relative_to(OUT.resolve()):
        raise AssertionError('write outside authorized validation directory')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)


def save(path, value):
    write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def source_audit(sources):
    header = sources[HEADER].decode('utf-8-sig')
    fixture = sources[FIXTURE].decode('utf-8-sig')
    strip = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
    code = re.sub(strip, ' ', header)
    forbidden = r'\b(?:windows|Win32|SendInput|keybd_event|mouse_event|GetAsyncKeyState|GetKeyboardState|CreateProcess\w*|LoadLibrary\w*|GetProcAddress|CreateThread|Sleep|SetTimer|SteamClient|CoCreateInstance|CoInitialize\w*|CreateFile\w*|ReadFile|FindFirstFile\w*|FindNextFile\w*|DeviceIoControl|fopen|ifstream|ofstream|fstream|filesystem|thread|async|WinHttp\w*|socket|system|popen)\b'
    hits = re.findall(forbidden, code)
    if hits:
        raise AssertionError(('forbidden dependencies/side effects in pure header', hits))
    includes = re.findall(r'^\s*#include\s*[<"]([^>"]+)[>"]', header, re.M)
    allowed = {'json.hpp', 'algorithm', 'array', 'cstdint', 'initializer_list', 'string', 'string_view', 'utility', 'vector'}
    if not includes or any(name not in allowed for name in includes):
        raise AssertionError(('unreviewed header include', includes))
    fixture_includes = re.findall(r'^\s*#include\s*[<"]([^>"]+)[>"]', fixture, re.M)
    if set(fixture_includes) != {'../native/msi_official_mode_log_evidence.h', 'functional', 'iostream', 'set', 'stdexcept'}:
        raise AssertionError(('unreviewed fixture include', fixture_includes))
    fixture_code = re.sub(strip, ' ', fixture)
    if re.search(forbidden, fixture_code):
        raise AssertionError('platform/file/input call in memory fixture')
    if 'template<class Ops> json collect(bool enabled, Ops& ops)' not in header:
        raise AssertionError('production contract missing')
    if re.search(r'\bstatic\s+(?!constexpr)', code):
        raise AssertionError('unexpected static state')
    methods = sorted(set(re.findall(r'\bops\.(\w+)\s*\(', code)))
    if methods != ['dates', 'hash', 'now', 'read', 'roots']:
        raise AssertionError(('unexpected Ops capability', methods))
    return {'pass': True, 'pureOpsHeader': True, 'productionHeaderDirectlyIncluded': True,
            'platformHeaders': [], 'opsMethods': methods, 'noDirectoryEnumeration': True,
            'noThreads': True, 'includes': includes, 'fixtureIncludes': fixture_includes}


def build_and_run(run, name, flags):
    exe = run / (name + '.exe')
    imports_path = run / (name + '-imports.txt')
    symbols_path = run / (name + '-symbols.txt')
    args = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', '/permissive-', *flags,
            str(run / 'source' / FIXTURE), '/I' + str(run / 'source/deps/json'),
            '/Fo' + str(run / (name + '.obj')), '/Fd' + str(run / (name + '-compiler.pdb')),
            '/Fe' + str(exe), '/link', '/INCREMENTAL:NO', '/NODEFAULTLIB:user32.lib',
            '/NODEFAULTLIB:ole32.lib', '/NODEFAULTLIB:oleaut32.lib', '/NODEFAULTLIB:shell32.lib', '/NODEFAULTLIB:advapi32.lib']
    batch = '@echo off\ncall "' + str(VCVARS) + '" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
    batch += subprocess.list2cmdline(args) + '\nif errorlevel 1 exit /b %errorlevel%\n'
    batch += 'dumpbin /imports "' + str(exe) + '" > "' + str(imports_path) + '"\nif errorlevel 1 exit /b %errorlevel%\n'
    batch += 'dumpbin /symbols "' + str(run / (name + '.obj')) + '" > "' + str(symbols_path) + '"\nexit /b %errorlevel%\n'
    command = run / (name + '.cmd')
    write(command, batch)
    temp = run / 'temp'
    temp.mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update(TMP=str(temp), TEMP=str(temp), PYTHONDONTWRITEBYTECODE='1')
    process = subprocess.run(['cmd.exe', '/d', '/c', str(command)], cwd=run, env=env,
                             capture_output=True, encoding='utf-8', errors='replace', timeout=120)
    write(run / (name + '-compile.log'), process.stdout + process.stderr)
    profile = {'profile': name, 'flags': flags, 'pass': False, 'compileExitCode': process.returncode, 'executableRun': False}
    if process.returncode:
        profile['error'] = 'compilation or import dump failed; see compile log'
        return profile
    imports = imports_path.read_text(encoding='utf-8', errors='replace').lower()
    symbols = symbols_path.read_text(encoding='utf-8', errors='replace').lower()
    banned = [term for term in BANNED if term in imports]
    references = [term for term in DIRECT_SYMBOLS if term in symbols]
    dlls = re.findall(r'(?m)^\s+([\w.-]+\.dll)\s*$', imports)
    unexpected_dlls = sorted(set(dlls) - {'kernel32.dll'})
    audit = {'pass': not banned and not references and not unexpected_dlls,
             'unexpectedDlls': unexpected_dlls, 'allowedRuntimeDlls': ['kernel32.dll'],
             'crtDynamicLoaderImports': [term for term in ['loadlibraryexw', 'getprocaddress'] if term in imports], 'forbiddenImports': banned,
             'forbiddenObjectReferences': references, 'forbiddenImportTerms': BANNED,
             'dlls': re.findall(r'(?m)^\s+([\w.-]+\.dll)\s*$', imports),
             'exeSha256': sha(exe.read_bytes()), 'importsSha256': sha(imports_path.read_bytes())}
    save(run / (name + '-import-audit.json'), audit)
    profile['importAudit'] = audit
    if not audit['pass']:
        profile['error'] = 'forbidden imports/object references: fixture not run'
        return profile
    process = subprocess.run([str(exe)], cwd=run, env=env, capture_output=True,
                             encoding='utf-8', errors='replace', timeout=40)
    write(run / (name + '-result.json'), process.stdout)
    write(run / (name + '-stderr.txt'), process.stderr)
    profile.update(executableRun=True, testExitCode=process.returncode)
    try:
        profile['test'] = json.loads(process.stdout)
        profile['pass'] = process.returncode == 0 and profile['test']['pass'] is True
    except (ValueError, TypeError, KeyError):
        profile['error'] = 'invalid fixture output'
    return profile


def main():
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run = OUT / ('run-' + stamp + '-' + str(os.getpid()))
    run.mkdir(parents=True, exist_ok=False)
    result = {'pass': False, 'run': str(run), 'profiles': [], 'productExecuted': False,
              'liveLogReads': 0, 'actualWindowsInput': False, 'OEMCalls': 0, 'SteamCalls': 0,
              'writesOutsideEvidenceByRunner': False, 'parentUiStaticMetadata': PARENT_UI_METADATA,
              'adapterHashImplementationTested': False,
              'singleIOHardTimeoutTested': False, 'scope': 'production pure Ops header and memory fixtures only'}
    try:
        sources = {name: (REPO / name).read_bytes() for name in INPUTS}
        for name, data in sources.items():
            write(run / 'source' / name, data)
        result['sources'] = [{'path': name, 'sha256': sha(data), 'bytes': len(data)} for name, data in sources.items()]
        result['sourceAudit'] = source_audit(sources)
        save(run / 'source-audit.json', result['sourceAudit'])
        if not VCVARS.is_file():
            raise FileNotFoundError(str(VCVARS))
        for name, flags in PROFILES:
            try:
                profile = build_and_run(run, name, flags)
            except Exception as error:
                profile = {'profile': name, 'pass': False, 'error': str(error)}
                write(run / (name + '-runner-error.txt'), traceback.format_exc())
            result['profiles'].append(profile)
            print(name + ': ' + ('PASS' if profile['pass'] else 'FAIL') +
                  ', checks=' + str(profile.get('test', {}).get('checks', 0)), flush=True)
        result['sourcesUnchanged'] = all((REPO / name).read_bytes() == data for name, data in sources.items())
        result['pass'] = result['sourcesUnchanged'] and all(p['pass'] for p in result['profiles']) and len(result['profiles']) == 2
    except Exception as error:
        result['error'] = str(error)
        write(run / 'runner-error.txt', traceback.format_exc())
    save(run / 'result.json', result)
    save(OUT / 'latest.json', {'run': str(run), 'pass': result['pass']})
    print('Evidence: ' + str(run), flush=True)
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
