from pathlib import Path
import sys, ctypes, ctypes.wintypes as W, hashlib, json, os, secrets, subprocess, time, traceback

E = Path(sys.argv[1]).resolve()
OLD = E.parent.parent / 'RawDeviceName'
REPO = Path('G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC')
PY = Path('G:/YeManCC-Work/Environment/Toolchains/python-embed/3.13.17/python.exe')
PW = Path('G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe')
HELPER = REPO / 'tools/ymcc_sim/global/host-process.ps1'

def now(): return time.time() * 1000
def need(c, message):
    if not c: raise RuntimeError(message)
def put(p, value):
    p = Path(p)
    need(p.resolve().is_relative_to(E), 'WRITE_OUTSIDE_EXCLUSIVE_ROOT: ' + str(p))
    p.parent.mkdir(parents=True, exist_ok=True)
    with p.open('x', encoding='utf-8', newline='\n') as f:
        f.write(value if isinstance(value, str) else json.dumps(value, ensure_ascii=False, indent=2) + '\n')
        f.flush(); os.fsync(f.fileno())
def load(p): return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def key(p): return str(Path(p).resolve()).casefold()
def pin(p):
    p = Path(p).resolve()
    data = p.read_bytes()
    return dict(path=str(p), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
def unique_pins(pins):
    result = {}
    for p in pins:
        k = key(p['path'])
        if k in result: need(result[k]['sha256'] == p['sha256'] and result[k]['bytes'] == p['bytes'], 'CONFLICTING_PIN: ' + k)
        else: result[k] = p
    return list(result.values())
def verify(pins):
    actual = []
    for p in pins:
        a = pin(p['path'])
        need(a['bytes'] == p['bytes'] and a['sha256'] == p['sha256'], 'DECLARED_INPUT_DRIFT: ' + p['path'])
        actual.append(a)
    return actual
def deadline(): return float(sys.argv[2])
def host_ticks():
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.GetCurrentProcess.restype = ctypes.c_void_p
    k.GetProcessTimes.argtypes = [ctypes.c_void_p, *([ctypes.POINTER(W.FILETIME)] * 4)]
    b, e, kt, ut = (W.FILETIME() for _ in range(4))
    need(k.GetProcessTimes(k.GetCurrentProcess(), ctypes.byref(b), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut)), 'OWNER_TIMES')
    return (b.dwHighDateTime << 32 | b.dwLowDateTime) + 504911232000000000

def owned(label, program, args, environment, sources, tools, maxms, expected=0, compile=False):
    begin = now()
    end = min(deadline(), begin + maxms + 10000)
    need(end - begin >= 12000, 'NO_USEFUL_REMAINING_BOUNDED_TIME')
    raw = E / ('Raw-' + label); raw.mkdir()
    driver = E / 'Drivers' / label; driver.mkdir(parents=True)
    put(raw / 'arguments.json', list(map(str, args)))
    request = dict(schemaVersion=1, kind='ymcc-raw-program-v1', cancelNonce=secrets.token_hex(32), cwd=str(E), arguments=pin(raw/'arguments.json'), environment=environment,
        sources=unique_pins(sources), tools=unique_pins([*tools, pin(program), pin(PW)]), maxOutputBytes=16777216)
    need(any(key(x['path']) == key(HELPER) for x in request['sources']), 'HOST_PROCESS_NOT_DECLARED')
    if compile:
        vctip = Path(program).parent / 'vctip.exe'
        request['toolCleanupPolicy'] = dict(kind='MSVC_VCTIP_AFTER_ROOT_EXIT', maximumProcesses=1, program=pin(vctip))
    put(raw / 'request.json', request)
    command = [str(PW), '-NoProfile', '-NonInteractive', '-File', str(HELPER), '-RawProgram', '-ProgramPath', str(program), '-ArgumentsPath', str(raw/'arguments.json'), '-RawDirectory', str(raw), '-RawRequestPath', str(raw/'request.json'), '-RawRequestSha256', pin(raw/'request.json')['sha256'], '-OwnerProcessId', str(os.getpid()), '-OwnerStartTicks', str(host_ticks()), '-DeadlineEpochMs', str(int(end)), '-CleanupReserveMs', '10000']
    error = None; code = None; t = None
    try:
        with (driver/'guard.stdout.log').open('xb') as out, (driver/'guard.stderr.log').open('xb') as err:
            p = subprocess.Popen(command, cwd=E, env=environment, stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
            put(driver/'launch.json', dict(pid=p.pid, ownerPid=os.getpid(), startedEpochMs=begin, deadlineEpochMs=end, arguments=command))
            code = p.wait(timeout=max(1, (end-now())/1000 + 5))
        t = load(raw/'terminal.json')
        need(t['collectionComplete'] and t['logsClosedAndFlushed'] and t['declaredPinsUnchanged'], 'RAW_COLLECTION_OR_PINS_FAILED: ' + str(t.get('error')))
        j = t['job']; b = t['business']
        need(b and b['atomicJobAdmission'] and t['resumedEpochMs'] and (raw/'admission.json').is_file() and (raw/'started.json').is_file(), 'ACTUAL_ADMISSION_MISSING')
        need(j['activeProcessesFinal'] == 0 and j['zeroObservedBeforeDispose'] and j['disposed'] and j['cleanupError'] is None, 'JOB_ZERO_OR_DISPOSAL_FAILED')
        need(t['businessExitCode'] == expected and t['reason'] in ['NATURAL','DECLARED_TOOL_CLEANUP'] and not t['timedOut'] and not t['cancelled'], 'PRODUCER_EXIT: ' + json.dumps(dict(helperExit=code,reason=t['reason'],businessExit=t['businessExitCode'],error=t['error'])))
        need(t['business']['arguments'] == list(map(str,args)) and t['business']['environment'] == environment, 'ACTUAL_ARGUMENT_OR_ENVIRONMENT_MISMATCH')
    except Exception:
        error = traceback.format_exc()
    put(raw/'INTAKE.json', dict(status='PRODUCER_EXIT_AND_ACTUAL_JOB_ZERO' if not error else 'FAILED_OR_INCOMPLETE', error=error, helperExit=code, terminal=pin(raw/'terminal.json') if (raw/'terminal.json').is_file() else None, startedEpochMs=begin, finishedEpochMs=now(), deadlineEpochMs=end, expectedBusinessExit=expected))
    if error: raise RuntimeError(error)
    return (raw/'stdout.raw').read_text(encoding='utf-8-sig'), (raw/'stderr.raw').read_text(encoding='utf-8-sig'), t
