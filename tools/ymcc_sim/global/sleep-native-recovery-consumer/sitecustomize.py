"""Audit boundary inherited by the two reviewed maintained Python consumers (frozen within each run)."""
import sys, os, json, pathlib, hashlib, atexit
_cfg = json.loads(pathlib.Path(os.environ['YMCC_SLEEP_PY_CONFIG']).read_text(encoding='utf-8-sig'))
_root = pathlib.Path(_cfg['root']).resolve(); _project = pathlib.Path(_cfg['project']).resolve()
_python = pathlib.Path(_cfg['python']).resolve(); _pythonroot = _python.parent
_record = {'schemaVersion':1, 'pid':os.getpid(), 'python':str(pathlib.Path(sys.executable).resolve()), 'commands':[], 'reads':{}, 'denied':[], 'naturalPythonExit':False}
_busy = False
_record['stdoutEncoding']=sys.stdout.encoding
_record['stderrEncoding']=sys.stderr.encoding
_original_exception_hook=sys.excepthook
def _exception_hook(kind,value,tb):
    _record['uncaughtException']={'type':kind.__name__,'message':str(value)}
    _original_exception_hook(kind,value,tb)
sys.excepthook=_exception_hook
if pathlib.Path(sys.executable).resolve() != _python:
    os._exit(94)
def _inside(base, target):
    try: target.relative_to(base); return True
    except ValueError: return False
def _path(value):
    if not isinstance(value, (str,bytes,os.PathLike)): return None
    return pathlib.Path(os.fsdecode(value)).resolve()
def _reject(event, value):
    _record['denied'].append({'event':event, 'value':str(value)})
    raise PermissionError('YMCC_SLEEP_PY_EXTERNAL_BOUNDARY:' + event)
def _audit(event, args):
    global _busy
    if _busy: return
    _busy = True
    try:
        if event == 'open':
            target = _path(args[0])
            if target is None: return
            mode = args[1] if isinstance(args[1],str) else ''
            flags = args[2] if isinstance(args[2],int) else 0
            writing = any(c in mode for c in 'wax+') or flags & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND)
            if writing and not _inside(_root,target): _reject(event,target)
            if not writing:
                if not any(_inside(b,target) for b in [_root,_project,_pythonroot]): _reject(event,target)
                if target.is_file(): _record['reads'][str(target)] = hashlib.sha256(target.read_bytes()).hexdigest()
        elif event in ['os.mkdir','os.remove','os.rmdir','os.rename','os.replace']:
            for value in args[:2] if event in ['os.rename','os.replace'] else args[:1]:
                target = _path(value)
                if target is not None and not _inside(_root,target): _reject(event,target)
        elif event == 'subprocess.Popen':
            executable, argv, cwd, environ = args
            candidates = [['python','tools/sleep_wake_resume_selftest.py','--output',str(_root)], ['cmd.exe','/d','/c',str(_root/'run-wake-recovery-selftest.cmd')], ['cmd.exe','/d','/c',str(_root/'run-sleep-recovery-api.cmd')]]
            if isinstance(argv,(list,tuple)): cmd=list(argv)
            else:
                formatter=sys.modules['subprocess'].list2cmdline
                matches=[row for row in candidates if formatter(row)==argv]
                cmd=matches[0] if len(matches)==1 else [str(argv)]
            allowed = len(cmd)==4 and cmd[0]=='python' and cmd[1]=='tools/sleep_wake_resume_selftest.py' and cmd[2]=='--output' and _path(cmd[3])==_root
            if not allowed:
                allowed = len(cmd)==4 and cmd[0]=='cmd.exe' and cmd[1:3]==['/d','/c'] and _inside(_root,_path(cmd[3])) and pathlib.Path(cmd[3]).suffix.lower()=='.cmd'
                if allowed:
                    batch=pathlib.Path(cmd[3]).read_text(encoding='ascii')
                    allowed=any(name in batch for name in ['sleep_wake_resume_native_selftest','sleep_recovery_api_selftest']) and _cfg['vcvars64'] in batch and 'if errorlevel 1 exit /b 1' in batch
            if not allowed: _reject(event,cmd)
            _record['commands'].append({'executable':executable,'argv':cmd,'cwd':str(cwd or pathlib.Path.cwd()),'scriptSha256':hashlib.sha256(pathlib.Path(cmd[3]).read_bytes()).hexdigest() if cmd[0]=='cmd.exe' else None})
        elif event.startswith(('socket.','winreg.')) or event in ['os.system','os.startfile','ctypes.dlopen']:
            _reject(event,str(args)[:300])
    finally: _busy = False
sys.addaudithook(_audit)
def _save():
    global _busy
    _busy=True
    _record['naturalPythonExit']=True
    with open(_root / ('python-audit-'+str(os.getpid())+'.json'),'w',encoding='utf-8') as stream:
        json.dump(_record,stream,indent=2);stream.write('\n')
atexit.register(_save)
