import ast, pathlib, os, sys, json, hashlib
_config=json.loads(pathlib.Path(os.environ['YMCC_SLEEP_PY_CONFIG']).read_text(encoding='utf-8-sig'))
_selected=pathlib.Path(sys.argv[0]).name
if _selected not in _config['scripts']:
    raise RuntimeError('S4_COMMAND_SCRIPT_NOT_REVIEWED')
_command=_config['scripts'][_selected]
os.environ['YMCC_FAN_GLOBAL_BOUNDARY']=_command['nodeBoundaryConfig']
_base=pathlib.Path(_config['sharedPythonGuard'])
_bytes=_base.read_bytes()
if hashlib.sha256(_bytes).hexdigest()!=_config['sharedPythonGuardSha256']:
    raise RuntimeError('S4_SHARED_PYTHON_GUARD_CHANGED')
_tree=ast.parse(_bytes.decode('utf-8-sig'),filename=str(_base))
_replacement=ast.parse('''
executable, argv, cwd, environ=args
formatter=sys.modules['subprocess'].list2cmdline
child=['python','tools/sleep_wake_resume_selftest.py','--output',str(_root)]
candidates=[_command['nodeCommand'],_command['batch']]+([child] if _selected=='sleep_recovery_api_selftest.py' else [])
matches=[row for row in candidates if argv==formatter(row)] if isinstance(argv,str) else []
cmd=list(argv) if isinstance(argv,(list,tuple)) else matches[0] if len(matches)==1 else [str(argv)]
if pathlib.Path(cwd or pathlib.Path.cwd()).resolve()!=_project or cmd not in candidates:
    _reject(event,cmd)
script_sha=None
if cmd==_command['nodeCommand']:
    active=environ if environ is not None else os.environ
    if executable not in (None,'node') or active.get('NODE_OPTIONS')!=_command['nodeOptions'] or active.get('YMCC_FAN_GLOBAL_BOUNDARY')!=_command['nodeBoundaryConfig'] or pathlib.Path(active.get('PATH','').split(';')[0])!=pathlib.Path(_command['nodeExecutable']).parent:
        _reject(event,'S4_CALIBRATION_ENVIRONMENT_CHANGED')
elif cmd==_command['batch']:
    data=pathlib.Path(cmd[3]).read_text(encoding='ascii')
    if [line for line in data.splitlines() if line.strip()]!=_command['lines']:
        _reject(event,'S4_EXACT_BATCH_CHANGED')
    script_sha=hashlib.sha256(pathlib.Path(cmd[3]).read_bytes()).hexdigest()
_record['commands'].append({'executable':executable,'argv':cmd,'cwd':str(cwd or pathlib.Path.cwd()),'scriptSha256':script_sha})
''').body
_count=0
class _Compose(ast.NodeTransformer):
    def visit_If(self,node):
        global _count
        self.generic_visit(node)
        if ast.unparse(node.test)=="event == 'subprocess.Popen'":
            node.body=_replacement
            _count+=1
        return node
_tree=_Compose().visit(_tree)
if _count!=1:
    raise RuntimeError('S4_SHARED_COMMAND_BRANCH_NOT_UNIQUE')
exec(compile(ast.fix_missing_locations(_tree),str(_base)+'[exact-S4-command-context]','exec'),globals())
_record['currentOriginalScript']=_selected
_record['sharedPythonCommandReview']={'sha256':hashlib.sha256(_bytes).hexdigest(),'replacedBranches':_count,'filesystemNetworkAndExternalPortRulesUnchanged':True}
