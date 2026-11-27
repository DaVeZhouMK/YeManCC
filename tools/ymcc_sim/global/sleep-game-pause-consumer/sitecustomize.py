"""Reference the frozen consumer-command composition; only batch configuration differs."""
import pathlib,os,json,hashlib,contextlib
_game_config=json.loads(pathlib.Path(os.environ['YMCC_SLEEP_PY_CONFIG']).read_text(encoding='utf-8-sig'))
_composition=pathlib.Path(_game_config['sharedConsumerGuard'])
_composition_bytes=_composition.read_bytes()
if hashlib.sha256(_composition_bytes).hexdigest()!=_game_config['sharedConsumerGuardSha256']:
    raise RuntimeError('GAME_PAUSE_SHARED_COMMAND_COMPOSITION_CHANGED')
exec(compile(_composition_bytes.decode('utf-8-sig'),str(_composition)+'[game-pause-exact-command-configuration]','exec'),globals())
_record['gamePauseSharedComposition']={'source':str(_composition),'sha256':hashlib.sha256(_composition_bytes).hexdigest(),'sharedRulesUnmodified':True,'onlyOriginalBatchLinesConfigured':True}

# Only the exact generated owned C++ receives a reviewed product context include.
# All inherited audit/command/path rules remain active and unchanged.
_game_context=_game_config['observerContext']
_game_header=pathlib.Path(_game_context['header']['path']).resolve()
if _game_header.parent!=_root or hashlib.sha256(_game_header.read_bytes()).hexdigest()!=_game_context['header']['sha256']:
    raise RuntimeError('GAME_MSI_HEADER_NOT_EXACT')
_game_script=(_project/_game_context['originalScript']['path']).resolve()
if pathlib.Path(sys.argv[0]).resolve()!=_game_script or hashlib.sha256(_game_script.read_bytes()).hexdigest()!=_game_context['originalScript']['sha256']:
    raise RuntimeError('GAME_MSI_ORIGINAL_GENERATOR_CHANGED')
def _game_transform(data):
    anchor=_game_context['insertion']['anchor']; include=_game_context['insertion']['include']
    if not isinstance(data,str) or data.count(anchor)!=1 or include in data or 'gameObserverContext' in _record:
        raise RuntimeError('GAME_MSI_ORIGINAL_CPP_ANCHOR_NOT_EXACT')
    generated=data
    for edit in _game_context['insertion'].get('scaffoldingReplacements',[]):
        if generated.count(edit['before'])!=1 or edit['after'] in generated:
            raise RuntimeError('GAME_MSI_SCAFFOLDING_NOT_EXACT')
        generated=generated.replace(edit['before'],edit['after'],1)
    generated=generated.replace(anchor,include+anchor,1)
    proof={'headerSha256':_game_context['header']['sha256'],'reviewSha256':_game_context['reviewSha256'],'originalScriptSha256':_game_context['originalScript']['sha256'],'originalTextSha256':hashlib.sha256(data.encode('utf-8')).hexdigest(),'generatedTextSha256':hashlib.sha256(generated.encode('utf-8')).hexdigest(),'insertionOffset':data.index(anchor),'include':include,'originalGeneratorAndDefinitionsUnchanged':True}
    return generated,proof

_game_original_open=pathlib.Path.open
@contextlib.contextmanager
def _game_generated_cpp(path):
    proof=None
    with _game_original_open(path,'x',encoding='utf-8') as output:
        class Writer:
            def write(self,data):
                nonlocal proof
                if proof is not None:
                    raise RuntimeError('GAME_MSI_ORIGINAL_CPP_MULTIPLE_WRITES')
                generated,proof=_game_transform(data)
                output.write(generated)
                return len(data)
        yield Writer()
    if proof is None:
        raise RuntimeError('GAME_MSI_ORIGINAL_CPP_NOT_WRITTEN')
    proof['generatedFileSha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    _record['gameObserverContext']=proof

def _game_open(self,*args,**kwargs):
    if self.name=='sleep_game_pause_native_selftest.cpp' and self.resolve().parent==_root and args==('x',) and kwargs=={'encoding':'utf-8'}:
        return _game_generated_cpp(self)
    return _game_original_open(self,*args,**kwargs)
pathlib.Path.open=_game_open

def _game_prepare_only(event,args):
    if event=='subprocess.Popen':
        _reject(event,'GAME_PAUSE_PREPARATION_MUST_NOT_LAUNCH')
sys.addaudithook(_game_prepare_only)
