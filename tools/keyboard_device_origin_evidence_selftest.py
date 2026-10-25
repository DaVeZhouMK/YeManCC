#!/usr/bin/env python3
"""Build/run only the synthetic keyboard origin fixture; audit both PE imports."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
OUT = Path('G:/YeManCC-Work/Build/Validation/KeyboardDeviceOriginEvidence-20261015/core')
VC = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
INPUTS = ('native/keyboard_device_origin_evidence.h',
          'tools/keyboard_device_origin_evidence_selftest.cpp',
          'tools/keyboard_device_origin_evidence_selftest.py', 'deps/json/json.hpp')
FORBIDDEN = ('cfgmgr32.dll', 'setupapi.dll', 'user32.dll', 'win32u.dll', 'hid.dll',
             'cm_get_', 'cm_locate_', 'cm_set_', 'sendinput', 'getasynckeystate',
             'registerrawinputdevices', 'getrawinputdata', 'setwindowshookex',
             'deviceiocontrol', 'createprocess', 'shellexecute')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def main():
    run = OUT / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + str(os.getpid()))
    run.mkdir(parents=True)
    sources = {name: (REPO / name).read_bytes() for name in INPUTS}
    for name, content in sources.items():
        filename = 'fixture.cpp' if name.endswith('_selftest.cpp') else Path(name).name
        (run / filename).write_bytes(content)
    save(run / 'source-binding.json', {'inputs': {name: sha(data) for name, data in sources.items()},
                                     'repo': str(REPO), 'scope': 'inert fixture only'})
    profiles = []
    for name, flags in [('Release', ['/O2', '/DNDEBUG', '/MT']),
                        ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd'])]:
        exe = run / (name + '.exe')
        args = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', *flags,
                str(run / 'fixture.cpp'), '/I' + str(run), '/Fo' + str(run / (name + '.obj')),
                '/Fd' + str(run / (name + '.pdb')), '/Fe' + str(exe)]
        imports_path = run / (name + '-imports.txt')
        batch = run / (name + '-build.cmd')
        batch.write_text('@echo off\nset VSLANG=1033\ncall "' + str(VC) + '" >nul\n'
                         'if errorlevel 1 exit /b %errorlevel%\n' + subprocess.list2cmdline(args) + '\n'
                         'if errorlevel 1 exit /b %errorlevel%\n'
                         'dumpbin /imports "' + str(exe) + '" > "' + str(imports_path) + '"\n'
                         'exit /b %errorlevel%\n', encoding='utf-8')
        built = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=run, capture_output=True,
                               encoding='utf-8', errors='replace', timeout=120)
        (run / (name + '-build.log')).write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode:
            raise RuntimeError(f'{name} compile failed in {run}\n{built.stdout}{built.stderr}')
        imports = imports_path.read_text(encoding='utf-8', errors='replace').lower()
        found = [value for value in FORBIDDEN if value in imports]
        save(run / (name + '-import-audit.json'), {'pass': not found, 'forbiddenFound': found,
                                                'checked': FORBIDDEN, 'binarySha256': sha(exe.read_bytes())})
        if found:
            raise RuntimeError(f'{name}: forbidden imports: {found}')
        tested = subprocess.run([str(exe)], cwd=run, capture_output=True, encoding='utf-8',
                                errors='replace', timeout=30)
        (run / (name + '-result.json')).write_text(tested.stdout, encoding='utf-8')
        (run / (name + '-stderr.txt')).write_text(tested.stderr, encoding='utf-8')
        if tested.returncode:
            raise RuntimeError(f'{name} failed in {run}\n{tested.stdout}{tested.stderr}')
        result = json.loads(tested.stdout)
        if not result['pass']:
            raise RuntimeError(f'{name} reported failure in {run}')
        profiles.append({'profile': name, **result, 'forbiddenImportsAbsent': True})
        print(f'{name}: {result["cases"]} meaningful cases, {result["checks"]} checks; import audit passed', flush=True)
    changed = [name for name, data in sources.items() if (REPO / name).read_bytes() != data]
    if changed:
        raise RuntimeError(f'Tested source drifted: {changed}')
    result = {'pass': True, 'run': str(run), 'profiles': profiles,
              'sourceHashes': {name: sha(data) for name, data in sources.items()},
              'artifacts': {path.name: sha(path.read_bytes()) for path in sorted(run.iterdir()) if path.is_file()},
              'sourceStable': True, 'actualOSInput': False, 'actualCMQueries': False,
              'productExecuted': False, 'helperExecuted': False}
    save(run / 'result.json', result)
    save(OUT / 'latest.json', result)
    print(json.dumps({'pass': True, 'run': str(run), 'sourceHashes': result['sourceHashes']}))


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
