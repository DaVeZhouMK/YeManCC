"""Run only extracted path-probe functions, with fake owners or inert Python children.
Requires Python 3.11 to decode the pinned archive. Never starts Loader or Steam.
"""
import argparse, hashlib, importlib.util, json, marshal, os, pathlib, shutil, struct, subprocess, sys, types, unittest.mock, zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('repack', ROOT / 'tools/patch-decky-loader.py')
repack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repack)

def archive(path):
    prefix, cookie, entries = repack.read_archive(path.read_bytes())
    entry = next(e for e in entries if e['kind'] == b'z')
    _, modules = repack.read_pyz(zlib.decompress(entry['data']) if entry['compressed'] else entry['data'])
    return prefix, entries, dict((name, (kind, payload)) for name, kind, payload in modules)

def helper(modules):
    return marshal.loads(zlib.decompress(modules['decky_loader.helpers'][1]))

def function(code, name):
    return next(c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == name)

class Log:
    def __init__(self): self.warnings = []
    def warning(self, value): self.warnings.append(value)

class Runner:
    STARTUPINFO = subprocess.STARTUPINFO
    STARTF_USESHOWWINDOW = subprocess.STARTF_USESHOWWINDOW
    SW_HIDE = subprocess.SW_HIDE
    CREATE_NO_WINDOW = subprocess.CREATE_NO_WINDOW
    def __init__(self, stdout=b' C:/one \n\n C:/two \n', failure=None):
        self.calls = []; self.stdout = stdout; self.failure = failure
    def run(self, args, **kwargs):
        self.calls.append((args, kwargs))
        if self.failure: raise self.failure
        return types.SimpleNamespace(stdout=self.stdout, check_returncode=lambda: None)

def invoke(code, runner, platform='windows', resolved='C:/Python311/python.exe'):
    flags = types.SimpleNamespace(ON_LINUX=platform=='linux', ON_WINDOWS=platform=='windows',
        localplatform=types.SimpleNamespace(_get_user_id=lambda: 1000))
    log = Log()
    fn = types.FunctionType(code, {'subprocess':runner, 'localplatform':flags, 'logger':log, 'os':os})
    with unittest.mock.patch.object(shutil, 'which', return_value=resolved):
        result = fn()
    return result, log

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--before',type=pathlib.Path,required=True)
    parser.add_argument('--candidate',type=pathlib.Path,required=True); parser.add_argument('--out',type=pathlib.Path,required=True)
    args = parser.parse_args(); checks=[]
    def check(name, condition):
        if not condition: raise AssertionError(name)
        checks.append(name)
    bprefix, bentries, bmods = archive(args.before)
    aprefix, aentries, amods = archive(args.candidate)
    before, after = helper(bmods), helper(amods)
    check('PE bootloader remains GUI and byte-identical', bprefix==aprefix)
    check('only the reviewed static router and async boot entries change outside PYZ', [b['name'] for b,a in zip(bentries,aentries) if b['kind']!=b'z' and b['data']!=a['data']]==[r'decky_loader\static\chunk-BFRQAfGY.js',r'decky_loader\static\index.js'])
    check('only helpers PYZ module changes', [n for n in bmods if bmods[n]!=amods[n]]==['decky_loader.helpers'])
    old = function(before,'get_system_pythonpaths'); new = function(after,'get_system_pythonpaths')
    oldinject = function(before,'create_inject_script'); newinject = function(after,'create_inject_script')
    restored = after.replace(co_consts=tuple(old if c is new else oldinject if c is newinject else c for c in after.co_consts))
    check('only the optional probe and reviewed injection function change in helpers', restored==before)
    generated = repack.generated_script(newinject)
    check('no-Steam-restart behavior remains in packaged injection', 'RestartJSContext' not in generated and 'SteamClient' not in generated)
    runner=Runner();result,_=invoke(old,runner)
    check('original packaged code reproduces an unhardened Python subprocess', result==['C:/one','C:/two'] and 'creationflags' not in runner.calls[0][1] and 'startupinfo' not in runner.calls[0][1])
    runner=Runner();result,_=invoke(new,runner);options=runner.calls[0][1]
    check('Windows retains discovered paths and pins resolved executable', result==['C:/one','C:/two'] and runner.calls[0][0][0]=='C:/Python311/python.exe')
    check('Windows child explicitly has no console and starts hidden', options['creationflags']==subprocess.CREATE_NO_WINDOW and options['startupinfo'].dwFlags&subprocess.STARTF_USESHOWWINDOW and options['startupinfo'].wShowWindow==subprocess.SW_HIDE)
    check('optional Windows probe has bounded wait', options['timeout']==5)
    runner=Runner();result,_=invoke(new,runner,resolved=None)
    check('no installed Python returns optional empty paths without spawning', result==[] and not runner.calls)
    for alias in [r'C:\Users\Fixture\AppData\Local\Microsoft\WindowsApps\python.exe',r'C:/USERS/Fixture/AppData/Local/MICROSOFT/WINDOWSAPPS/Python.exe']:
        runner=Runner();result,_=invoke(new,runner,resolved=alias)
        check('Windows Store or manager alias cannot activate a GUI: '+alias, result==[] and not runner.calls)
    for label,error in [('timeout',subprocess.TimeoutExpired('python',5)),('missing',FileNotFoundError()),('nonzero',subprocess.CalledProcessError(1,'python'))]:
        runner=Runner(failure=error);result,log=invoke(new,runner)
        check('optional '+label+' cannot abort Loader initialization', result==[] and len(log.warnings)==1)
    for platform in ['linux','mac']:
        left,right=Runner(),Runner();oldpaths,_=invoke(old,left,platform);newpaths,_=invoke(new,right,platform)
        check(platform+' command, env, user and returned paths are preserved', oldpaths==newpaths and left.calls==right.calls)
    # Exercise the exact flags captured from the packaged function using an inert child.
    # No old visible child is executed; no Steam/Loader, business IPC or device writes.
    cmd='import ctypes,json; from ctypes import wintypes; k=ctypes.WinDLL("kernel32"); k.GetConsoleWindow.restype=wintypes.HWND; print(json.dumps({"consoleWindow":bool(k.GetConsoleWindow())}))'
    actual=subprocess.run([sys.executable,'-c',cmd],**options)
    actual.check_returncode();observation=json.loads(actual.stdout)
    check('real Windows console interpreter has no console HWND with packaged probe flags', observation['consoleWindow'] is False)
    report={'status':'passed','checks':len(checks),'cases':checks,'realChild':observation,'loaderStarted':False,'steamTouched':False,'settingsWrites':0,'beforeSha256':hashlib.sha256(args.before.read_bytes()).hexdigest(),'candidateSha256':hashlib.sha256(args.candidate.read_bytes()).hexdigest()}
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':'passed','checks':len(checks),'consoleWindow':observation['consoleWindow']}))

if __name__=='__main__': main()
