#!/usr/bin/env python3
"""Execute frozen complete production sensor owners, explicit memory failures and
owned kernel threads, in Release/Checked. Never starts YMCC or real sensors.
Retention is not arbitrary blocked-COM destruction. No global registration.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = Path('G:/YeManCC-Work/Testing/YMCC/Evidence/VC-SensorOwners23')
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
FIXTURES = 'tools/fixtures/input_capture_timer_retirement'
MUTATIONS = {
    'provider-running-is-stopped': ('msi_legacy_sensor.h',
        'return lock.owns_lock() && !thread_ && !threadId_ && !state_;',
        'return lock.owns_lock() && (!thread_ || WaitForSingleObject(thread_,0)!=WAIT_TIMEOUT);'),
    'provider-unchecked-close': ('msi_legacy_sensor.h',
        'if(!CloseHandle(thread_)) return false;', 'CloseHandle(thread_);'),
    'provider-start-unknown': ('msi_legacy_sensor.h',
        'if(thread_ && WaitForSingleObject(thread_,0)!=WAIT_OBJECT_0) return false;',
        'if(thread_ && WaitForSingleObject(thread_,0)==WAIT_TIMEOUT) return false;'),
    'census-unchecked-close': ('sensorapi_inventory.h',
        'if(!CloseHandle(value.worker->thread)) return;', 'CloseHandle(value.worker->thread);'),
    'census-cancelled-is-stopped': ('sensorapi_inventory.h',
        'return lock.owns_lock() && !value.worker && !value.pending;',
        'return lock.owns_lock() && (!value.worker || value.worker->cancelled.load()) && !value.pending;'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


TOOL_INPUTS = 'tools/ymcc_sim/global/controller-signal-evidence/tool-inputs.json'
TOOL_PROBE = 'tools/ymcc_sim/global/controller-signal-evidence/tool-probe.cpp'


def primary_tools():
    extra = json.loads((ROOT/TOOL_INPUTS).read_text(encoding='utf-8-sig'))
    authority_bytes = (ROOT/extra['nativeAuthority']).read_bytes()
    need(sha(authority_bytes) == extra['nativeAuthoritySha256'], 'SENSOR_NATIVE_TOOL_DECLARATION_CHANGED')
    authority = json.loads(authority_bytes)
    native = authority['native']
    primary = {**extra['primary'], 'python': extra['python'], 'compiler': native['compiler'],
               'linker': native['linker'], 'dumpbin': native['dumpbin'],
               'commandProcessor': native['commandProcessor'], 'vcvars': str(VCVARS)}
    pins = {str(Path(row['path']).resolve()).lower(): row['sha256'] for row in [*authority['files'], *extra['files']]}
    observed = []
    for role, name in primary.items():
        path = Path(name)
        actual = sha(path.read_bytes())
        need(path.is_absolute() and pins.get(str(path.resolve()).lower()) == actual, 'SENSOR_PRIMARY_TOOL_IDENTITY:'+role)
        observed.append({'role': role, 'path': str(path), 'sha256': actual})
    need(Path(sys.executable).resolve() == Path(primary['python']).resolve(), 'SENSOR_PRIMARY_PYTHON_IDENTITY')
    return primary, observed, authority


def execute(argv, directory, env, log, timeout=60):
    start = time.monotonic()
    try:
        result = subprocess.run([str(v) for v in argv], cwd=directory, env=env,
                                capture_output=True, timeout=timeout,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except OSError as error:
        message = 'SENSOR_TOOL_SPAWN_FAILED: '+str(argv[0])+': '+str(error)
        log.write_text(message+'\n', encoding='utf-8')
        raise RuntimeError(message) from error
    text = result.stdout.decode('utf-8', errors='replace') + result.stderr.decode('utf-8', errors='replace')
    log.write_text(text, encoding='utf-8')
    return result.returncode, text, round(time.monotonic()-start, 3)


def compiler_environment(run):
    primary, _, authority = primary_tools()
    need(VCVARS.is_file(), 'Missing vcvars64')
    windows = Path(os.environ.get('SystemRoot', 'C:/Windows'))
    env = {k: v for k, v in os.environ.items() if k.upper() not in ('PATH', 'CL', '_CL_', 'LINK', '_LINK_')}
    env['PATH'] = str(windows/'System32') + os.pathsep + str(windows)
    env['VSLANG'] = '1033'
    script = run/'compiler-environment.cmd'
    sdk_version = Path(authority['native']['include'][1]).parent.name
    script.write_text('@echo off\ncall "' + str(VCVARS) + '" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
                      'set "WindowsSDKVersion=' + sdk_version + '/"\n'
                      'echo VCToolsInstallDir=%VCToolsInstallDir%\necho VCToolsVersion=%VCToolsVersion%\n'
                      'echo WindowsSDKVersion=%WindowsSDKVersion%\necho INCLUDE=%INCLUDE%\necho LIB=%LIB%\necho LIBPATH=%LIBPATH%\n', encoding='utf-8')
    code, text, _ = execute([windows/'System32/cmd.exe', '/d', '/c', script], run, env, run/'compiler-environment.log', 45)
    need(code == 0, 'vcvars64 failed')
    for line in text.splitlines():
        key, sep, value = line.partition('=')
        if sep and key in ('VCToolsInstallDir', 'VCToolsVersion', 'WindowsSDKVersion', 'INCLUDE', 'LIB', 'LIBPATH'):
            env[key] = value
    compiler = Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
    need(compiler.is_file(), 'No x64 compiler')
    need(compiler.resolve() == Path(primary['compiler']).resolve(), 'SENSOR_COMPILER_ENVIRONMENT_IDENTITY')
    need(env.get('WindowsSDKVersion', '').rstrip('\\/') == Path(authority['native']['include'][1]).parent.name,
         'SENSOR_SDK_ENVIRONMENT_IDENTITY')
    env['INCLUDE'] = os.pathsep.join(authority['native']['include'])
    env['LIB'] = os.pathsep.join(authority['native']['lib'])
    env['PATH'] = os.pathsep.join(dict.fromkeys([str(compiler.parent), str(Path(primary['node']).parent),
                      str(Path(primary['git']).parent), str(Path(primary['python']).parent), env['PATH']]))
    return compiler, env



def inventory_regression(run, env, freeze, result):
    # Reuse complete existing query/export/actor fixture and explicit SDK mocks.
    relative = 'tools/sensorapi_inventory_selftest.mjs'
    text = freeze(relative).decode('utf-8-sig')
    for path in ('tools/fixtures/legion_motion_consumer/extract_main.mjs',
                 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
                 'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs',
                 'tools/fixtures/sensorapi_inventory/cases.cpp'):
        freeze(path)
    def once(before, after):
        nonlocal text
        need(text.count(before) == 1, 'Existing inventory runner binding changed: '+before)
        text = text.replace(before, after, 1)
    old_root = "path.join(ws,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/SensorApiInventory-20261018')"
    once('root='+old_root+",control=", 'root=process.env.YMCC_SENSOR_OWNERS_INVENTORY_OUT,control=')
    once("path.join(root,'../SensorApiExplicitPair-20261018/2026-10-18T08-14-45-677Z-35432-baseline')",
         "path.join(ws,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/SensorApiExplicitPair-20261018/2026-10-18T08-14-45-677Z-35432-baseline')")
    once('freeze(fileURLToPath(import.meta.url));', "freeze(path.join(repo,'tools/sensorapi_inventory_selftest.mjs'));")
    once("actor.code.indexOf('if(!shutdown)',", "actor.code.indexOf('if(!shutdown && !inputCaptureSensorStopRequested())',")
    guard = '''
#define NOMINMAX
#include <initguid.h>
#include <windows.h>
#include <sensorsapi.h>
#include <sensors.h>
#include <PortableDeviceTypes.h>
#include <wrl/client.h>
#include <atomic>
static std::atomic<unsigned> sensorOwnersActivations{0};
static HRESULT WINAPI sensorOwnersDenyActivation(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID*){++sensorOwnersActivations;return E_ACCESSDENIED;}
#define CoCreateInstance sensorOwnersDenyActivation
'''
    once("fs.writeFileSync(path.join(out,'test.cpp'),base+'\\n'+cases);",
         "fs.writeFileSync(path.join(out,'test.cpp'),"+json.dumps(guard)+"+base+'\\n'+cases.replace(' const json report=', ' expect(sensorOwnersActivations==0,\"no default SensorManager activation\"); const json report='));")
    adapted = run/'inventory-adapter.mjs'
    adapted.write_text(text, encoding='utf-8')
    primary, _, _ = primary_tools()
    node = primary['node']
    need(Path(node).is_file(), 'Primary Node runtime unavailable for existing inventory regression')
    child_env = dict(env)
    child_env['YMCC_SENSOR_OWNERS_INVENTORY_OUT'] = str(run/'inventory-consumers')
    code, _, seconds = execute([node, '--input-type=module', '--eval', text], ROOT/'tools', child_env, run/'inventory.log', 60)
    row = {'exit': code, 'seconds': seconds, 'adapterSha256': sha(adapted.read_bytes()),
           'adaptations': ['Evidence output root', 'Explicit original historical SDK seed path',
                           'Eval module identity bound to original runner', 'Current actor metadata-tail extraction anchor',
                           'Denied/counted CoCreate fallback leaf'],
           'log': 'inventory.log'}
    result['inventoryConsumers'] = row
    need(code == 0, 'Existing inventory consumers failed; see inventory.log')
    reports = list((run/'inventory-consumers').glob('*/RESULT.json'))
    need(len(reports) == 1, 'Expected one inventory source-bound receipt')
    child = json.loads(reports[0].read_text(encoding='utf-8-sig'))
    need(child['status'] == 'pass' and child['boundProductionCurrent'], 'Inventory source binding failed')
    for source in child['inputs']:
        need(sha(Path(source['path']).read_bytes()) == source['sha256'], 'Inventory source drift: '+source['path'])
    row.update(receipt=str(reports[0]), receiptSha256=sha(reports[0].read_bytes()), configurations=child['configurations'])
    print('Inventory consumers: '+', '.join(c['name']+': '+str(c['report']['checks'])+' checks' for c in child['configurations']), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output-root', type=Path, default=DEFAULT_OUT)
    ap.add_argument('--tool-probe', action='store_true', help='Only git/SDK/compiler/temp execution; no sensor owner acceptance')
    args = ap.parse_args()
    out = args.output_root.resolve()
    run = out/('r-' + dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + str(os.getpid()))
    run.mkdir(parents=True)
    sources = []
    result = {'schemaVersion': 1, 'pass': False, 'status': 'RUNNING', 'run': str(run), 'repo': str(ROOT),
              'scope': 'Complete actual Provider/Census headers and existing Provider algorithm/adapter consumers; leaf OS/COM ports are explicit',
              'sources': sources, 'profiles': [], 'sourceCurrent': False,
              'boundedArbitraryComDestructionProven': False, 'fullTypedCaptureStopIntegrated': False,
              'installedAppDeviceDriverSleepDeployment': False, 'defaultSensorActivationAllowed': False,
              'providerLifetimeRequirement': 'Keep Provider alive while stop returns false; destructor is best effort',
              'classification': {'Provider': 'persistent sampling owner', 'Census': 'one-shot metadata worker per scope, may survive cancellation'},
              'limits': ['No arbitrary COM hang destruction or forced thread termination',
                         'No main.cpp typed Stop integration or full sensor STA shutdown proof',
                         'No real sensors, hardware, installed product, power transition, DSU or global registration']}
    def publish():
        name = 'TOOL-PROBE.json' if args.tool_probe else 'RESULT.json'
        save(run/name, result)
        save(out/name, result)
    publish()
    captured = {}
    def freeze(relative):
        if relative not in captured:
            data = (ROOT/relative).read_bytes()
            captured[relative] = data
            target = run/'source'/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            sources.append({'path': relative, 'sha256': sha(data), 'bytes': len(data)})
        return captured[relative]
    try:
        # Local include closure is compiled directly; no implementation extraction/rewrite.
        pending = ['msi_legacy_sensor.h', 'sensorapi_inventory.h']
        headers = {}
        while pending:
            name = pending.pop()
            if name in headers:
                continue
            headers[name] = freeze('native/' + name)
            for child in re.findall(rb'#include\s+"([^"/]+\.h)"', headers[name]):
                dep = child.decode()
                if (ROOT/'native'/dep).is_file():
                    pending.append(dep)
        for relative in ('tools/input_capture_sensor_owners_selftest.py',
                         FIXTURES+'/sensor_owners_memory.cpp.in', FIXTURES+'/sensor_owners_kernel.cpp.in',
                         'tools/msi_sensor_adapter_selftest.cpp', 'tools/msi_legacy_sensor_selftest.cpp'):
            freeze(relative)
        # Algorithm fixture's additional local include closure.
        extra = 'msi_motion_capabilities.h'
        headers[extra] = freeze('native/' + extra)
        freeze('native/main.cpp')
        main_source = captured['native/main.cpp'].decode('utf-8-sig')
        consumers = []
        for number, line in enumerate(main_source.splitlines(), 1):
            if re.search(r'g_(?:msiSensorApi|hcWindowsSensorApi)Provider\.(?:start|stop|running|stopped)\(|sensorapi_inventory::Census::', line):
                consumers.append({'line': number, 'source': line.strip()})
        result['mainConsumers'] = {'execution': 'source-only callsite inventory; main.cpp not built/executed', 'calls': consumers}
        primary, observed, authority = primary_tools()
        result['primaryToolsBefore'] = observed
        freeze(TOOL_INPUTS); freeze(json.loads((ROOT/TOOL_INPUTS).read_text())['nativeAuthority'])
        compiler, env = compiler_environment(run)
        code, text, _ = execute([primary['git'], 'rev-parse', 'HEAD'], ROOT, env, run/'git-head.log', 10)
        need(code == 0 and re.fullmatch(r'[a-f0-9]{40,64}', text.strip()), 'SENSOR_GIT_HEAD_PROBE_FAILED')
        result['gitHead'] = text.strip()
        code, text, _ = execute([primary['git'], 'status', '--short', '--', 'native/msi_legacy_sensor.h', 'native/sensorapi_inventory.h'], ROOT, env, run/'git-status.log', 10)
        need(code == 0, 'SENSOR_GIT_STATUS_PROBE_FAILED')
        result['gitHeaderStatus'] = text.splitlines()
        result['compiler'] = {'path': str(compiler), 'sha256': sha(compiler.read_bytes()),
                              'vcvarsPath': str(VCVARS), 'vcvarsSha256': sha(VCVARS.read_bytes()),
                              'toolVersion': env.get('VCToolsVersion'), 'sdkVersion': env.get('WindowsSDKVersion')}
        if args.tool_probe:
            result.update(probeOnly=True, productionAcceptance=False)
            need('TEMP' in env and len(env['TEMP']) <= 110 and Path(env['TEMP']).is_dir(), 'SENSOR_SHORT_OWNED_TEMP_REQUIRED')
            folder = Path(env['TEMP']).parent
            cpp, obj, exe = folder/'sp.cpp', folder/'sp.obj', folder/'sp.exe'
            cpp.write_bytes(freeze(TOOL_PROBE))
            code, _, _ = execute([compiler, '/nologo', '/std:c++17', '/EHsc', '/MT', '/O2', cpp, '/Fo'+str(obj), '/Fe'+str(exe)],
                                  folder, env, run/'probe-build.log')
            need(code == 0, 'SENSOR_COMPILER_PROBE_FAILED')
            code, text, _ = execute([exe], folder, env, run/'probe-run.log', 10)
            need(code == 0 and text.strip() == 'PRIMARY_TOOL_PROBE_PASS', 'SENSOR_COMPILER_RUNTIME_PROBE_FAILED')
            node_code, node_text, _ = execute([primary['node'], '-p', 'process.execPath'], folder, env, run/'node-probe.log', 10)
            need(node_code == 0 and Path(node_text.strip()).resolve() == Path(primary['node']).resolve(), 'SENSOR_PRIMARY_NODE_PROBE_FAILED')
            result['primaryToolsAfter'] = primary_tools()[1]
            need(result['primaryToolsBefore'] == result['primaryToolsAfter'], 'SENSOR_PRIMARY_TOOLS_CHANGED')
            result['sourceDrift'] = [name for name, data in captured.items() if (ROOT/name).read_bytes() != data]
            need(not result['sourceDrift'], 'Sensor probe source changed')
            result.update(status='TOOL_PROBE_PASS', sourceCurrent=True, tempPath=env['TEMP'], tempPathLength=len(env['TEMP']),
                          executableSha256=sha(exe.read_bytes()), compilerExecuted=True, privateTempFileCreatedAndDeleted=True)
            publish(); print('SENSOR_TOOL_PROBE_PASS '+str(run/'TOOL-PROBE.json'), flush=True); return 0
        for profile, flags in (('Release', ['/O2', '/MT', '/DNDEBUG']), ('Checked', ['/Od', '/RTC1', '/MTd', '/D_DEBUG'])):
            tests = [('memory', None), ('kernel', None), ('production-api', None), ('adapter', None), ('algorithm', None)]
            tests.extend((name, mutation) for name, mutation in MUTATIONS.items())
            for name, mutation in tests:
                directory = run/profile/name
                directory.mkdir(parents=True)
                local_headers = dict(headers)
                if mutation:
                    file, before, after = mutation
                    data = local_headers[file].decode('utf-8')
                    need(data.count(before) == 1, 'Mutation must bind unique actual source: '+name)
                    local_headers[file] = data.replace(before, after, 1).encode('utf-8')
                for file, data in local_headers.items():
                    (directory/file).write_bytes(data)
                if name == 'production-api':
                    source = b'''#include "sensorapi_inventory.h"\n#include <type_traits>\nusing ymcc::msi_sensor::Provider;\nusing ymcc::sensors::sensorapi_inventory::Census;\nstatic_assert(std::is_same_v<decltype(&Provider::stopped),bool (Provider::*)() const noexcept>);\nstatic_assert(std::is_same_v<decltype(&Census::stopped),bool (*)() noexcept>);\nstatic_assert(std::is_same_v<decltype(&Census::retire),bool (*)() noexcept>);\nint main(){Provider p;return p.stopped()&&Census::stopped()&&Census::retire()?0:1;}\n'''
                elif name in ('adapter', 'algorithm'):
                    path = 'tools/msi_sensor_adapter_selftest.cpp' if name == 'adapter' else 'tools/msi_legacy_sensor_selftest.cpp'
                    source = captured[path].replace(b'"../native/', b'"')
                else:
                    kind = 'kernel' if name == 'kernel' else 'memory'
                    source = captured[FIXTURES+'/sensor_owners_'+kind+'.cpp.in']
                (directory/'test.cpp').write_bytes(source)
                row = {'profile': profile, 'case': name, 'expectedNegative': mutation is not None,
                       'inputSha256': sha(source), 'headers': {n: sha(v) for n, v in local_headers.items()}}
                result['profiles'].append(row)
                argv = [compiler, '/nologo', '/std:c++17', '/utf-8', '/EHsc', '/DNOMINMAX', '/W4', *flags,
                        '/I'+str(directory), directory/'test.cpp', '/Fo'+str(directory/'test.obj'), '/Fe'+str(directory/'test.exe')]
                if name == 'adapter':
                    argv.append('/DYMCC_FAULT_INJECT')
                argv.extend(['/link', 'ole32.lib', 'oleaut32.lib'])
                row['buildExit'], _, row['buildSeconds'] = execute(argv, directory, env, directory/'build.log')
                publish()
                need(row['buildExit'] == 0, 'Build failed: '+profile+'/'+name)
                exe = directory/'test.exe'
                row['exeSha256'] = sha(exe.read_bytes())
                row['exit'], text, row['runSeconds'] = execute([exe], directory, env, directory/'run.log', 35)
                match = re.search(r'checks=(\d+) failures=(\d+)', text)
                if match:
                    row['checks'], row['failures'] = map(int, match.groups())
                if mutation:
                    row['accepted'] = row['exit'] == 1 and row.get('failures', 0) > 0
                else:
                    row['accepted'] = row['exit'] == 0
                publish()
                need(row['accepted'], 'Unexpected result: '+profile+'/'+name)
                print(profile+'/'+name+': '+('EXPECTED_FAIL' if mutation else 'PASS'), flush=True)
        inventory_regression(run, env, freeze, result)
        current = []
        for relative, data in captured.items():
            if (ROOT/relative).read_bytes() != data:
                current.append(relative)
        result['primaryToolsAfter'] = primary_tools()[1]
        need(result['primaryToolsBefore'] == result['primaryToolsAfter'], 'SENSOR_PRIMARY_TOOLS_CHANGED')
        result['sourceDrift'] = current
        need(not current, 'Source changed during validation: '+', '.join(current))
        # Every frozen header copy and mutation is identified separately in the receipt.
        result.update({'pass': True, 'status': 'PASS', 'sourceCurrent': True})
        publish()
        print('Evidence: '+str(run), flush=True)
        return 0
    except Exception as error:
        result.update(status='FAIL', error=repr(error))
        publish()
        print('FAIL '+str(error)+'\nEvidence: '+str(run), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
