"""Reference the frozen consumer-command composition; only batch configuration differs."""
import pathlib,os,json,hashlib
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
_game_original_write_text=pathlib.Path.write_text
def _game_write_text(self,data,*args,**kwargs):
    if self.resolve()!=_root/'sleep_game_pause_native_selftest.cpp':
        return _game_original_write_text(self,data,*args,**kwargs)
    anchor=_game_context['insertion']['anchor']; include=_game_context['insertion']['include']
    if not isinstance(data,str) or data.count(anchor)!=1 or include in data or 'gameObserverContext' in _record:
        raise RuntimeError('GAME_MSI_ORIGINAL_CPP_ANCHOR_NOT_EXACT')
    if kwargs.get('encoding')!='utf-8' or args or set(kwargs)!=set(['encoding']):
        raise RuntimeError('GAME_MSI_ORIGINAL_CPP_WRITE_SHAPE_CHANGED')
    generated=data.replace(anchor,include+anchor,1)
    result=_game_original_write_text(self,generated,**kwargs)
    _record['gameObserverContext']={'headerSha256':_game_context['header']['sha256'],'reviewSha256':_game_context['reviewSha256'],'originalScriptSha256':_game_context['originalScript']['sha256'],'originalTextSha256':hashlib.sha256(data.encode('utf-8')).hexdigest(),'generatedTextSha256':hashlib.sha256(generated.encode('utf-8')).hexdigest(),'generatedFileSha256':hashlib.sha256(self.read_bytes()).hexdigest(),'insertionOffset':data.index(anchor),'include':include,'originalGeneratorAndDefinitionsUnchanged':True}
    return result
pathlib.Path.write_text=_game_write_text
