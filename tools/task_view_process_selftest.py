#!/usr/bin/env python3
"""Real WindowsOps process/job sidecar: only this fixture's bounded inert children.

Writes only Build/Validation/TaskView-Api-20261015/process-fixture. Builds frozen
inputs, audits PE imports before execution, then launches each fixture suspended
inside a kill-on-close guardian job. Never launches a production helper or invokes
COM, User32, foreground APIs, input, Steam, or device services. This is process
lifetime evidence and is not acceptance of the Task View API or actual UI.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes as wt
import datetime
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import traceback

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
OUT = REPO.parents[2] / 'Build/Validation/TaskView-Api-20261015/process-fixture'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
CPP = 'tools/task_view_process_selftest.cpp'
RUNNER = 'tools/task_view_process_selftest.py'
HEADER = 'native/task_view_dispatch.h'
INPUTS = [CPP, RUNNER, HEADER, 'deps/json/json.hpp']
PROFILES = [('Release', ['/O2', '/DNDEBUG', '/MT']),
            ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/MTd'])]
FORBIDDEN = ['user32.dll', 'win32u.dll', 'ole32.dll', 'oleaut32.dll', 'combase.dll',
             'shell32.dll', 'shlwapi.dll', 'sendinput', 'keybd_event', 'mouse_event',
             'allowsetforegroundwindow', 'setforegroundwindow', 'setwindowshook',
             'getforegroundwindow', 'cocreateinstance', 'coinitialize', 'windowswitcher',
             'setupapi.dll', 'cfgmgr32.dll', 'hid.dll', 'dinput8.dll', 'gameinput.dll']
REQUIRED = ['CreateProcessW', 'CreateJobObjectW', 'SetInformationJobObject',
            'AssignProcessToJobObject', 'ResumeThread', 'WaitForSingleObject',
            'TerminateProcess', 'GetExitCodeProcess', 'DuplicateHandle', 'GetProcessHandleCount']
WAIT_OBJECT_0, WAIT_TIMEOUT, INFINITE = 0, 258, 0xFFFFFFFF
CANCELLED = 0xE0000042


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path = Path(path).resolve()
    if not path.is_relative_to(OUT.resolve()):
        raise AssertionError('write outside authorized evidence: ' + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)


def save(path, value):
    write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')


class StartupInfo(ctypes.Structure):
    _fields_ = [('cb', wt.DWORD), ('lpReserved', wt.LPWSTR), ('lpDesktop', wt.LPWSTR),
                ('lpTitle', wt.LPWSTR), ('dwX', wt.DWORD), ('dwY', wt.DWORD),
                ('dwXSize', wt.DWORD), ('dwYSize', wt.DWORD), ('dwXCountChars', wt.DWORD),
                ('dwYCountChars', wt.DWORD), ('dwFillAttribute', wt.DWORD), ('dwFlags', wt.DWORD),
                ('wShowWindow', wt.WORD), ('cbReserved2', wt.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', wt.HANDLE), ('hStdOutput', wt.HANDLE), ('hStdError', wt.HANDLE)]


class ProcessInfo(ctypes.Structure):
    _fields_ = [('hProcess', wt.HANDLE), ('hThread', wt.HANDLE),
                ('dwProcessId', wt.DWORD), ('dwThreadId', wt.DWORD)]


class BasicLimits(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong),
                ('PerJobUserTimeLimit', ctypes.c_longlong), ('LimitFlags', wt.DWORD),
                ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t),
                ('ActiveProcessLimit', wt.DWORD), ('Affinity', ctypes.c_size_t),
                ('PriorityClass', wt.DWORD), ('SchedulingClass', wt.DWORD)]


class IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ulonglong) for name in
                ['ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount',
                 'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount']]


class ExtendedLimits(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', BasicLimits), ('IoInfo', IoCounters),
                ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


class Accounting(ctypes.Structure):
    _fields_ = [(name, ctypes.c_longlong) for name in
                ['TotalUserTime', 'TotalKernelTime', 'ThisPeriodTotalUserTime', 'ThisPeriodTotalKernelTime']]
    _fields_ += [(name, wt.DWORD) for name in
                 ['TotalPageFaultCount', 'TotalProcesses', 'ActiveProcesses', 'TotalTerminatedProcesses']]


class PidList(ctypes.Structure):
    _fields_ = [('NumberOfAssignedProcesses', wt.DWORD), ('NumberOfProcessIdsInList', wt.DWORD),
                ('ProcessIdList', ctypes.c_size_t * 32)]


K = ctypes.WinDLL('kernel32', use_last_error=True)
SIGNATURES = {
    'CreateJobObjectW': (wt.HANDLE, [ctypes.c_void_p, wt.LPCWSTR]),
    'SetInformationJobObject': (wt.BOOL, [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD]),
    'QueryInformationJobObject': (wt.BOOL, [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p]),
    'CreateProcessW': (wt.BOOL, [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL,
                               wt.DWORD, ctypes.c_void_p, wt.LPCWSTR, ctypes.POINTER(StartupInfo), ctypes.POINTER(ProcessInfo)]),
    'AssignProcessToJobObject': (wt.BOOL, [wt.HANDLE, wt.HANDLE]),
    'ResumeThread': (wt.DWORD, [wt.HANDLE]),
    'WaitForSingleObject': (wt.DWORD, [wt.HANDLE, wt.DWORD]),
    'GetExitCodeProcess': (wt.BOOL, [wt.HANDLE, ctypes.POINTER(wt.DWORD)]),
    'TerminateProcess': (wt.BOOL, [wt.HANDLE, wt.UINT]),
    'TerminateJobObject': (wt.BOOL, [wt.HANDLE, wt.UINT]),
    'CloseHandle': (wt.BOOL, [wt.HANDLE]),
}
for name, (result, arguments) in SIGNATURES.items():
    function = getattr(K, name)
    function.restype, function.argtypes = result, arguments


def checked(value, label):
    if not value:
        raise OSError(ctypes.get_last_error(), label)
    return value


def accounting(job):
    info = Accounting()
    checked(K.QueryInformationJobObject(job, 1, ctypes.byref(info), ctypes.sizeof(info), None),
            'QueryInformationJobObject accounting')
    return {'totalProcesses': info.TotalProcesses, 'activeProcesses': info.ActiveProcesses,
            'totalTerminatedProcesses': info.TotalTerminatedProcesses}


def job_pids(job):
    info = PidList()
    checked(K.QueryInformationJobObject(job, 3, ctypes.byref(info), ctypes.sizeof(info), None),
            'QueryInformationJobObject PID list')
    return list(info.ProcessIdList[:info.NumberOfProcessIdsInList])


def guarded_fixture(exe, run, name):
    """Kernel32-only launcher; primary thread cannot run before guardian assignment.

    The guardian covers the fixture parent and its nested jobs. The fixture's own
    RAII protects each child; this outer guard also covers abrupt fixture exit or
    a bounded runner timeout. No existing/unrelated PID is opened or terminated.
    """
    exe = exe.resolve()
    if not exe.is_relative_to(run.resolve()) or exe.name != name + '.exe':
        raise AssertionError('refusing non-fixture executable')
    result = {'fixturePid': None, 'guardianAssignedBeforeResume': False, 'runnerTimeout': False,
              'runnerTerminationAttempted': False, 'launchHandlesClosed': False}
    job, process, thread = None, None, None
    started = time.monotonic()
    try:
        job = checked(K.CreateJobObjectW(None, None), 'CreateJobObjectW guardian')
        limits = ExtendedLimits()
        limits.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        checked(K.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)),
                'SetInformationJobObject guardian')
        stdin_path = run / (name + '-stdin.empty')
        write(stdin_path, b'')
        with stdin_path.open('rb') as child_in, (run / (name + '-stdout.jsonl')).open('wb') as child_out, \
                (run / (name + '-stderr.txt')).open('wb') as child_error:
            descriptors = [msvcrt.get_osfhandle(item.fileno()) for item in [child_in, child_out, child_error]]
            for handle in descriptors:
                os.set_handle_inheritable(handle, True)
            try:
                startup = StartupInfo()
                startup.cb, startup.dwFlags, startup.wShowWindow = ctypes.sizeof(startup), 0x101, 0
                startup.hStdInput, startup.hStdOutput, startup.hStdError = descriptors
                pi = ProcessInfo()
                command = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(exe)]))
                checked(K.CreateProcessW(str(exe), command, None, None, True, 0x08000004,
                                        None, str(run), ctypes.byref(startup), ctypes.byref(pi)),
                        'CreateProcessW only audited fixture executable')
                process, thread = pi.hProcess, pi.hThread
                result['fixturePid'] = pi.dwProcessId
                save(run / (name + '-launch.json'), result)
                checked(K.AssignProcessToJobObject(job, process), 'AssignProcessToJobObject guardian')
                result['guardianAssignedBeforeResume'] = True
                if K.ResumeThread(thread) == INFINITE:
                    raise OSError(ctypes.get_last_error(), 'ResumeThread fixture')
                checked(K.CloseHandle(thread), 'CloseHandle fixture thread')
                thread = None
            finally:
                for handle in descriptors:
                    os.set_handle_inheritable(handle, False)
            waited = K.WaitForSingleObject(process, 20000)
            if waited != WAIT_OBJECT_0:
                result['runnerTimeout'] = waited == WAIT_TIMEOUT
                raise RuntimeError('fixture wait did not complete within 20 seconds: ' + str(waited))
            code = wt.DWORD()
            checked(K.GetExitCodeProcess(process, ctypes.byref(code)), 'GetExitCodeProcess fixture')
            result['exitCode'] = code.value
            # Only this run's guardian job is observed; there is no system scan.
            until = time.monotonic() + 1.0
            while accounting(job)['activeProcesses'] and time.monotonic() < until:
                time.sleep(0.01)
            result['guardianBeforeClose'] = accounting(job)
            result['guardianPidsBeforeClose'] = job_pids(job)
            if result['guardianBeforeClose']['activeProcesses']:
                raise RuntimeError('fixture left active processes in guardian job')
    except Exception as error:
        result['error'] = str(error)
        result['traceback'] = traceback.format_exc()
    finally:
        # All termination targets are handles created by this exact invocation.
        if job:
            try:
                result['guardianBeforeCleanup'] = accounting(job)
                result['guardianPidsBeforeCleanup'] = job_pids(job)
                if result['guardianBeforeCleanup']['activeProcesses']:
                    result['runnerTerminationAttempted'] = True
                    checked(K.TerminateJobObject(job, CANCELLED), 'TerminateJobObject guardian')
            except Exception as error:
                result['cleanupError'] = str(error)
        if process and K.WaitForSingleObject(process, 0) != WAIT_OBJECT_0:
            result['runnerTerminationAttempted'] = True
            if not K.TerminateProcess(process, CANCELLED) and K.WaitForSingleObject(process, 0) != WAIT_OBJECT_0:
                result['cleanupError'] = 'could not terminate owned fixture parent'
        if process:
            result['parentExitVerified'] = K.WaitForSingleObject(process, 3000) == WAIT_OBJECT_0
        if job:
            try:
                until = time.monotonic() + 3.0
                while accounting(job)['activeProcesses'] and time.monotonic() < until:
                    time.sleep(0.01)
                result['guardianAfterCleanup'] = accounting(job)
                result['allOwnedProcessesExited'] = result['guardianAfterCleanup']['activeProcesses'] == 0
            except Exception as error:
                result['cleanupError'] = str(error)
        close_okay = True
        for handle in [thread, process, job]:
            if handle:
                close_okay = bool(K.CloseHandle(handle)) and close_okay
        result['launchHandlesClosed'] = close_okay
        result['elapsedMs'] = round((time.monotonic() - started) * 1000)
        save(run / (name + '-launch.json'), result)
    return result


def source_audit(sources):
    cpp = sources[CPP].decode('utf-8-sig')
    header = sources[HEADER].decode('utf-8-sig')
    if 'struct GuardOps : tv::WindowsOps' not in cpp:
        raise AssertionError('fixture must derive from real WindowsOps')
    foreground = re.search(r'bool allowForeground\(DWORD childPid\) noexcept \{(.*?)\n    \}', cpp, re.S)
    if not foreground:
        raise AssertionError('foreground suppression entry missing')
    body = re.sub(r'//[^\n]*', '', foreground.group(1))
    if 'return true;' not in body or 'WindowsOps::allowForeground' in body:
        raise AssertionError('real foreground entry might be called')
    if 'CREATE_SUSPENDED|CREATE_NO_WINDOW' not in re.sub(r'\s+', '', header):
        raise AssertionError('production start is no longer suspended/windowless; review required')
    for token in ['CoCreateInstance(', 'CoInitializeEx(', 'SendInput(', 'OpenProcess(',
                  'AllowSetForegroundWindow(', 'SetForegroundWindow(', 'SetWindowsHookEx(']:
        if token in cpp:
            raise AssertionError('forbidden fixture source token: ' + token)
    return {'pass': True, 'usesRealWindowsOps': True, 'foregroundEntryAlwaysFalse': True,
            'productionStartSuspendedAndNoWindow': True, 'actualUiOrApiAcceptance': False}


def compile_profile(run, name, flags):
    exe = run / (name + '.exe')
    args = ['cl.exe', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', '/Bv',
            '/Gy', '/Zc:inline', *flags, str(run / 'source' / CPP),
            '/I' + str(run / 'source/deps/json'), '/Fo' + str(run / (name + '.obj')),
            '/Fd' + str(run / (name + '-compiler.pdb')), '/Fe' + str(exe), '/link',
            '/SUBSYSTEM:WINDOWS', '/MACHINE:X64', '/INCREMENTAL:NO', '/OPT:REF',
            '/NODEFAULTLIB:user32.lib', '/NODEFAULTLIB:ole32.lib', '/NODEFAULTLIB:oleaut32.lib',
            '/NODEFAULTLIB:shell32.lib']
    command = '@echo off\ncall "' + str(VCVARS) + '" >nul\nif errorlevel 1 exit /b 1\n'
    command += subprocess.list2cmdline(args) + '\nif errorlevel 1 exit /b 1\n'
    command += 'dumpbin /imports "' + str(exe) + '" > "' + str(run / (name + '-imports.txt')) + '"\nif errorlevel 1 exit /b 1\n'
    command += 'dumpbin /headers "' + str(exe) + '" > "' + str(run / (name + '-headers.txt')) + '"\nexit /b %errorlevel%\n'
    batch = run / (name + '-build.cmd')
    write(batch, command)
    temp = run / 'temp'
    temp.mkdir(exist_ok=True)
    env = dict(os.environ, TMP=str(temp), TEMP=str(temp), PYTHONDONTWRITEBYTECODE='1')
    build = subprocess.Popen(['cmd.exe', '/d', '/c', str(batch)], cwd=run, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    save(run / (name + '-build-launch.json'), {'buildLauncherPid': build.pid, 'scope': 'build tools only; never a termination target'})
    output, _ = build.communicate(timeout=60)
    write(run / (name + '-compile.log'), output)
    profile = {'profile': name, 'pass': False, 'buildLauncherPid': build.pid, 'compileExitCode': build.returncode,
               'fixtureExecuted': False}
    if build.returncode:
        raise RuntimeError(name + ' compile/import dump failed; see ' + str(run / (name + '-compile.log')))
    imports = (run / (name + '-imports.txt')).read_text(encoding='utf-8', errors='replace')
    lowered = imports.lower()
    banned = [token for token in FORBIDDEN if token in lowered]
    missing = [token for token in REQUIRED if token.lower() not in lowered]
    dlls = re.findall(r'(?m)^\s+([\w.-]+\.dll)\s*$', lowered)
    audit = {'pass': not banned and not missing and set(dlls) == {'kernel32.dll'},
             'forbiddenMatches': banned, 'requiredImportsMissing': missing, 'importedDlls': dlls,
             'exeSha256': sha(exe.read_bytes())}
    save(run / (name + '-import-audit.json'), audit)
    profile['importAudit'] = audit
    if not audit['pass']:
        raise RuntimeError(name + ' forbidden/unexpected imports; fixture was not executed')
    launch = guarded_fixture(exe, run, name)
    profile.update(fixtureExecuted=bool(launch.get('fixturePid')), launch=launch)
    events = []
    output_file = run / (name + '-stdout.jsonl')
    if output_file.exists():
        for line in output_file.read_text(encoding='utf-8').splitlines():
            if line.strip():
                events.append(json.loads(line))
    reports = [event for event in events if event.get('event') == 'result']
    created = [event['pid'] for event in events if event.get('event') == 'child-created']
    cleanups = [event for event in events if event.get('event') == 'guard-cleanup']
    profile['allCreatedChildPids'] = created
    profile['test'] = reports[0] if len(reports) == 1 else {'pass': False, 'error': 'missing fixture result'}
    proof = profile['test']
    rows = proof.get('cases', [])
    pids_match = (len(set(created)) == len(created) and sorted(created) == sorted(row.get('pid') for row in rows)
                  and sorted(created) == sorted(event['pid'] for event in cleanups)
                  and all(event['clean'] and event['exitVerified'] for event in cleanups))
    guardian_pid_snapshots_match = all(set(row.get('guardianActivePidsAfterCreate', [])) == {launch.get('fixturePid'), row.get('pid')} for row in rows if 'guardianActivePidsAfterCreate' in row)
    profile['pidAccountingMatches'] = pids_match
    profile['guardianActivePidSnapshotsMatch'] = guardian_pid_snapshots_match
    profile['guardianTotalProcessCounterIsInformational'] = True
    profile['pass'] = (proof.get('pass') is True and launch.get('exitCode') == 0 and
                       not launch.get('error') and not launch.get('cleanupError') and
                       launch.get('guardianAssignedBeforeResume') and launch.get('allOwnedProcessesExited') and
                       launch.get('launchHandlesClosed') and not launch.get('runnerTerminationAttempted') and
                       pids_match and guardian_pid_snapshots_match)
    save(run / (name + '-result.json'), profile)
    return profile


def main():
    if len(sys.argv) != 1:
        raise SystemExit('This fixed-scope selftest accepts no arguments.')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run = OUT / ('run-' + stamp + '-' + str(os.getpid()))
    run.mkdir(parents=True, exist_ok=False)
    result = {'pass': False, 'run': str(run), 'runnerPid': os.getpid(), 'profiles': [],
              'scope': 'Create/wait/terminate only these fixture parents and their same-executable inert children',
              'productionHelperExecuted': False, 'comInvoked': False, 'inputInvoked': False,
              'foregroundApiInvoked': False, 'taskViewApiAcceptance': False}
    try:
        if not VCVARS.is_file():
            raise FileNotFoundError(VCVARS)
        sources = {name: (REPO / name).read_bytes() for name in INPUTS}
        result['sourceSha256'] = {name: sha(data) for name, data in sources.items()}
        for name, data in sources.items():
            write(run / 'source' / name, data)
        result['sourceAudit'] = source_audit(sources)
        save(run / 'source-audit.json', result['sourceAudit'])
        for name, flags in PROFILES:
            profile = compile_profile(run, name, flags)
            result['profiles'].append(profile)
            print(name + ': ' + ('PASS' if profile['pass'] else 'FAIL') +
                  ', parent=' + str(profile['launch'].get('fixturePid')) +
                  ', children=' + ','.join(map(str, profile['allCreatedChildPids'])), flush=True)
            if not profile['pass']:
                raise RuntimeError('Stop after failed cleanup/result; inspect evidence before any more launches')
        current = {name: (REPO / name).read_bytes() for name in INPUTS}
        result['filesChangedDuringRun'] = [name for name in INPUTS if current[name] != sources[name]]
        result['sourceStableThroughRun'] = not result['filesChangedDuringRun']
        result['pass'] = result['sourceStableThroughRun'] and all(row['pass'] for row in result['profiles'])
    except Exception as error:
        result['error'] = str(error)
        write(run / 'runner-error.txt', traceback.format_exc())
    save(run / 'result.json', result)
    save(OUT / 'latest.json', {'run': str(run), 'pass': result['pass']})
    rows = []
    for profile in result['profiles']:
        rows.append('| ' + profile['profile'] + ' | ' + str(profile['launch'].get('fixturePid')) + ' | ' +
                    ', '.join(map(str, profile.get('allCreatedChildPids', []))) + ' | ' +
                    ('PASS' if profile['pass'] else 'FAIL') + ' |')
    report = '# Real WindowsOps inert process sidecar\n\n'
    report += ('PASS' if result['pass'] else 'FAIL') + '. Scope: only self-created fixture processes. '
    report += 'No production helper, COM, WindowSwitcher, User32, input, foreground change, Steam, or device service was executed. '
    report += 'This is process/job/timeout cleanup evidence and is not Task View API/UI acceptance.\n\n'
    report += '| Build | Fixture parent PID | All inert child PIDs | Result |\n| --- | --- | --- | --- |\n' + '\n'.join(rows) + '\n\n'
    report += 'Each fixture parent was created suspended and assigned to an outer kill-on-close job before resume. '
    report += 'Only the fixture itself is an executable launch target. The production WindowsOps methods create suspended inert children, '
    report += 'assign a private job, resume, wait, and terminate the exact owned child. allowForeground reports an in-memory accepted grant; it never calls User32. '
    report += 'Tracking wrappers delegate to the real WindowsOps; duplicate process handles independently prove child exit.\n\n'
    report += 'Both builds test a 200 ms timeout, success on the same Dispatcher after that timeout, preservation of 0x80070005, '
    report += 'and real child cleanup during a deliberate parent-scope C++ exception. Release additionally tests the production 3000 ms call cap '
    report += 'with a 4500 ms request deadline. The only child wait mode sleeps for 10000 ms, bounded independently of all cleanup. '
    report += 'Per-case original handle closures and extra observer closures are checked. The GUI subsystem prevents implicit console-host creation. The cold first CreateProcess records its handle-count change and self-only metadata separately; all explicit handles must close. Subsequent requests, the exception case, and the suite must return exactly to their handle-count baseline. '
    report += 'Outer job accounting must show zero active processes before closing; actual live PID snapshots at child creation must contain only the parent and that child. Cumulative nested-job TotalProcesses counters are informational, not treated as distinct PID counts.\n\n'
    report += 'Build tools are separate from the process-operation target scope. Direct build cmd launcher PIDs are recorded in result.json; '
    report += 'this test never targets a compiler, existing process, or unrelated PID for termination.\n\n'
    report += 'See result.json, *-result.json, *-stdout.jsonl, *-launch.json, *-imports.txt, *-import-audit.json, '
    report += '*-compile.log, and frozen source/ inputs with SHA256. Imports must be kernel32.dll only, and are audited before running. '
    report += 'The production header and all inputs must remain unchanged throughout the run.\n'
    if result.get('error'):
        report += '\nError: ' + result['error'] + '\n'
    write(run / 'REPORT.md', report)
    print('Evidence: ' + str(run), flush=True)
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())