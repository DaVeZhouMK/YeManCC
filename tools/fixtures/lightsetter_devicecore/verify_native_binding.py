"""Compile and execute the actual shipping header; never run the real host."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--scratch', required=True)
args = parser.parse_args()
fixture = pathlib.Path(__file__).resolve().parent
repo = fixture.parents[2]
scratch = pathlib.Path(args.scratch).resolve() / 'native-binding'
scratch.mkdir(parents=True, exist_ok=True)
vs = pathlib.Path(os.environ['VCToolsInstallDir']).parent if os.environ.get('VCToolsInstallDir') else pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC')
versions = sorted((p for p in vs.iterdir() if (p / 'bin/Hostx64/x64/cl.exe').is_file()), reverse=True)
if not versions:
    raise RuntimeError('MSVC compiler unavailable')
msvc = versions[0]
sdk = pathlib.Path('C:/Program Files (x86)/Windows Kits/10')
sdk_versions = sorted((p for p in (sdk / 'Include').iterdir() if (p / 'ucrt').is_dir() and (p / 'um').is_dir()), reverse=True)
if not sdk_versions:
    raise RuntimeError('Windows CRT headers unavailable')
sdk_version = sdk_versions[0].name
bin_dir = msvc / 'bin/Hostx64/x64'
env = os.environ.copy()
env['PATH'] = str(bin_dir) + os.pathsep + env.get('PATH', '')
env['INCLUDE'] = ';'.join(map(str, [msvc / 'include', sdk / 'Include' / sdk_version / 'ucrt',
                                   sdk / 'Include' / sdk_version / 'shared', sdk / 'Include' / sdk_version / 'um']))
env['LIB'] = ';'.join(map(str, [msvc / 'lib/x64', sdk / 'Lib' / sdk_version / 'ucrt/x64', sdk / 'Lib' / sdk_version / 'um/x64']))
exe = scratch / 'lighting-binding-test.exe'
command = [str(bin_dir / 'cl.exe'), '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/utf-8', '/MT', '/O2',
           str(fixture / 'native_binding_test.cpp'), '/Fo' + str(scratch / 'lighting-binding-test.obj'), '/Fe' + str(exe)]
built = subprocess.run(command, cwd=scratch, env=env, capture_output=True, timeout=60)
(scratch / 'compile.log').write_bytes(built.stdout + built.stderr)
if built.returncode:
    raise RuntimeError('native header compile failed: ' + str(scratch / 'compile.log'))
ran = subprocess.run([str(exe)], cwd=scratch, capture_output=True, text=True, encoding='utf-8', timeout=10, check=True)
report = json.loads(ran.stdout)
assert report['ok'] and report['routeCases'] >= 14
header = repo / 'native/lighting_device_core_binding.h'
report.update({'header': str(header), 'headerSha256': hashlib.sha256(header.read_bytes()).hexdigest(),
               'compiler': str(bin_dir / 'cl.exe'), 'compileExitCode': built.returncode,
               'testExecutable': str(exe), 'testExecutableSha256': hashlib.sha256(exe.read_bytes()).hexdigest()})
(scratch / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
