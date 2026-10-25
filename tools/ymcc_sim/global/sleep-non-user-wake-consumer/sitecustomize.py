"""Consumer-specific command adapter around the pinned shared Python audit rules."""
import ast, pathlib, os, json, hashlib
_config = json.loads(pathlib.Path(os.environ['YMCC_SLEEP_PY_CONFIG']).read_text(encoding='utf-8-sig'))
_basefile = pathlib.Path(_config['sharedPythonGuard'])
_basebytes = _basefile.read_bytes()
if hashlib.sha256(_basebytes).hexdigest() != _config['sharedPythonGuardSha256']:
    raise RuntimeError('NONUSER_SHARED_GUARD_SOURCE_CHANGED')
_tree = ast.parse(_basebytes.decode('utf-8-sig'),filename=str(_basefile))
_replacement = ast.parse('''
executable, argv, cwd, environ = args
expected=['cmd.exe','/d','/c',str(_root/'run-native-selftest.cmd')]
node_expected=_cfg.get('nodeCommand')

formatter=sys.modules['subprocess'].list2cmdline
cmd=list(argv) if isinstance(argv,(list,tuple)) else expected if argv==formatter(expected) else node_expected if node_expected and argv==formatter(node_expected) else [str(argv)]
if pathlib.Path(cwd or pathlib.Path.cwd()).resolve()!=_project:
    _reject(event,cmd)
if node_expected and cmd == node_expected:
    active_env=environ if environ is not None else os.environ
    if executable not in (None,'node') or active_env.get('NODE_OPTIONS')!=_cfg['nodeOptions'] or active_env.get('YMCC_FAN_GLOBAL_BOUNDARY')!=_cfg['nodeBoundaryConfig']:
        _reject(event,'CALIBRATION_NODE_ENVIRONMENT_CHANGED')
    if pathlib.Path(active_env.get('PATH','').split(';')[0]) != pathlib.Path(_cfg['nodeExecutable']).parent:
        _reject(event,'CALIBRATION_NODE_NOT_PINNED')
    script_sha=None
else:
    if cmd != expected:
        _reject(event,cmd)
    batch=pathlib.Path(cmd[3]).read_text(encoding='ascii')
    lines=[line for line in batch.splitlines() if line.strip()]
    if lines != _cfg['consumerBatchLines']:
        _reject(event,'ORIGINAL_BATCH_SHAPE_CHANGED')
    script_sha=hashlib.sha256(pathlib.Path(cmd[3]).read_bytes()).hexdigest()
_record['commands'].append({'executable':executable,'argv':cmd,'cwd':str(cwd or pathlib.Path.cwd()),'scriptSha256':script_sha})
''').body
_count=0
class _CommandAdapter(ast.NodeTransformer):
    def visit_If(self,node):
        global _count
        self.generic_visit(node)
        test=node.test
        if isinstance(test,ast.Compare) and isinstance(test.left,ast.Name) and test.left.id=='event' and len(test.comparators)==1 and isinstance(test.comparators[0],ast.Constant) and test.comparators[0].value=='subprocess.Popen':
            node.body=_replacement;_count+=1
        return node
_tree=_CommandAdapter().visit(_tree)
if _count!=1: raise RuntimeError('NONUSER_SHARED_COMMAND_BRANCH_NOT_UNIQUE')
ast.fix_missing_locations(_tree)
_derived=ast.unparse(_tree)
exec(compile(_tree,str(_basefile)+'[non-user-command-adapter]','exec'),globals())
_record['guardComposition']={'sharedSource':str(_basefile),'sharedSha256':hashlib.sha256(_basebytes).hexdigest(),'consumerCommandAdapter':str(pathlib.Path(__file__).resolve()),'derivedAstSha256':hashlib.sha256(_derived.encode()).hexdigest(),'sharedFilesystemNetworkRegistryRulesUnchanged':True,'onlySubprocessBranchSpecialized':True}
_record['bootstrapImportedPythonModules']={str(pathlib.Path(module.__file__).resolve()):hashlib.sha256(pathlib.Path(module.__file__).read_bytes()).hexdigest() for module in tuple(sys.modules.values()) if getattr(module,'__file__',None) and pathlib.Path(module.__file__).is_file() and _inside(_pythonroot,pathlib.Path(module.__file__).resolve())}
