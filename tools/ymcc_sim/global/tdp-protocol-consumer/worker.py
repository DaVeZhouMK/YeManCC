import argparse,ast,ctypes,hashlib,importlib.util,io,json,marshal,os,pathlib,runpy,shutil,subprocess,sys,time,traceback

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--test',required=True,type=pathlib.Path);parser.add_argument('--root',required=True,type=pathlib.Path);args=parser.parse_args()
 root=args.root.resolve();test=args.test.resolve();helper=(root/'PowerControl/pawnio/YeManTdpCtl.py').resolve()
 if sys.flags.optimize or not sys.flags.isolated or not sys.flags.no_site or not sys.dont_write_bytecode or pathlib.Path(sys.pycache_prefix or '').resolve()!=(root/'ignored-cache').resolve() or not test.is_file() or not helper.is_file() or not test.is_relative_to(root):raise RuntimeError('TDP_PROTOCOL_INPUT_OR_ASSERT_MODE_INVALID')
 scratch=root/'scratch';scratch.mkdir();os.environ['TEMP']=str(scratch);os.environ['TMP']=str(scratch)
 expected=['check_families','check_amd_routes','check_intel_encoding','check_intel_single_targets','check_daemon_client_allowlist']
 original=test.read_text(encoding='utf-8-sig');tree=ast.parse(original);assert_lines={node.lineno for node in ast.walk(tree) if isinstance(node,ast.Assert)}
 observation={'forbidden':[],'fileWrites':0,'groups':{},'assertLineVisits':{},'executedHelperFunctions':[]}
 state={'active':True,'helperMainInvoked':False};compiled={};prefix=pathlib.Path(sys.base_prefix).resolve();helper_functions=set();test_name=str(test);helper_name=str(helper)
 def audit(event,values):
  if not state['active']:return
  if event=='exec' and values and getattr(values[0],'co_filename',None) in (str(test),str(helper)):
   compiled[values[0].co_filename]=marshal.dumps(values[0])
  if event in ('ctypes.dlopen','ctypes.dlsym','ctypes.call_function','subprocess.Popen','os.system','os.spawn','os.posix_spawn','os.exec','os.startfile','socket.connect','socket.__new__','os.symlink','os.link') or event.startswith('winreg.'):
   observation['forbidden'].append(event);raise RuntimeError('TDP_PROTOCOL_FORBIDDEN_ACTION:'+event)
  if event in ('os.remove','os.rmdir','os.mkdir','os.rename','os.replace','os.chmod','shutil.copyfile'):
   targets=values[:2] if event in ('os.rename','os.replace','shutil.copyfile') else values[:1]
   for value in targets:
    if isinstance(value,(str,bytes,os.PathLike)) and not pathlib.Path(os.fsdecode(value)).resolve().is_relative_to(root):
     observation['forbidden'].append('mutate-outside');raise RuntimeError('TDP_PROTOCOL_FILE_OUTSIDE_CASE')
  if event=='open' and values and isinstance(values[0],(str,bytes,os.PathLike)):
   target=pathlib.Path(os.fsdecode(values[0])).resolve();mode=values[1] or '';flags=values[2] or 0
   writing=any(char in str(mode) for char in 'wax+') or bool(flags&(os.O_WRONLY|os.O_RDWR|os.O_CREAT|os.O_TRUNC|os.O_APPEND))
   okay=target.is_relative_to(root) if writing else target.is_relative_to(root) or target.is_relative_to(prefix)
   if not okay:observation['forbidden'].append('open-outside');raise RuntimeError('TDP_PROTOCOL_FILE_OUTSIDE_CASE')
   if writing:observation['fileWrites']+=1
 def trace(frame,event,arg):
  name=frame.f_code.co_filename
  if name==helper_name and event=='call':
   helper_functions.add(frame.f_code.co_name)
   if frame.f_code.co_name in ('main','daemon_run','ensure_pawnio','_ensure_lock'):
    state['helperMainInvoked']=True;observation['forbidden'].append('PRODUCT_OR_HARDWARE_ENTRY');raise RuntimeError('TDP_PROTOCOL_PRODUCT_MAIN_FORBIDDEN')
  if name!=test_name:return None
  function=frame.f_code.co_name
  if function in expected:
   group=observation['groups'].setdefault(function,{'calls':0,'returns':0})
   if event=='call':group['calls']+=1
   if event=='return':group['returns']+=1
  if event=='line' and frame.f_lineno in assert_lines:
   key=str(frame.f_lineno);observation['assertLineVisits'][key]=observation['assertLineVisits'].get(key,0)+1
  return trace
 sys.addaudithook(audit);capture=io.StringIO();old_stdout=sys.stdout;old_stderr=sys.stderr;sys.stdout=capture;sys.stderr=capture
 failure=None;exit_code=0
 try:
  sys.settrace(trace);runpy.run_path(test_name,run_name='__main__')
 except BaseException as error:
  exit_code=1;failure={'type':type(error).__name__,'message':str(error),'function':None,'line':None,'sourceLine':None}
  tb=error.__traceback__
  while tb:
   if tb.tb_frame.f_code.co_filename==test_name:
    failure.update(function=tb.tb_frame.f_code.co_name,line=tb.tb_lineno,sourceLine=original.splitlines()[tb.tb_lineno-1].strip())
   tb=tb.tb_next
 finally:
  sys.settrace(None);state['active']=False;sys.stdout=old_stdout;sys.stderr=old_stderr
 observation['executedHelperFunctions']=sorted(helper_functions)
 compiled_code=[];code_dir=root/'executed-code';code_dir.mkdir()
 for source,code in compiled.items():
  target=code_dir/(('original-test' if source==str(test) else 'original-helper')+'.marshal');target.write_bytes(code)
  compiled_code.append({'source':source,'sourceSha256':hashlib.sha256(pathlib.Path(source).read_bytes()).hexdigest(),'path':str(target),'sha256':hashlib.sha256(code).hexdigest()})
 runtime=[]
 for module in list(sys.modules.values()):
  for attribute in ('__file__','__cached__'):
   file=getattr(module,attribute,None)
   if file:
    file=pathlib.Path(file).resolve()
    if file.is_file() and file.is_relative_to(prefix):runtime.append({'path':str(file),'sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
 runtime=list({entry['path']:entry for entry in runtime}.values());runtime.sort(key=lambda entry:entry['path'])
 result={'schemaVersion':1,'kind':'tdp-original-python-protocol','exitCode':exit_code,'assertOptimization':sys.flags.optimize,'isolated':bool(sys.flags.isolated),'noSite':bool(sys.flags.no_site),'noBytecodeWrite':sys.dont_write_bytecode,'pycachePrefix':sys.pycache_prefix,'failure':failure,'observation':observation,'stdout':capture.getvalue(),'runtimeInputs':runtime,'originalSourceUnchanged':True,'helperMainInvoked':state['helperMainInvoked'],'compiledCode':compiled_code,'physicalHardwareAccepted':False}
 (root/'original-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(capture.getvalue(),end='');print(json.dumps({'kind':result['kind'],'groups':list(observation['groups']),'failure':failure,'forbidden':observation['forbidden']},ensure_ascii=False))
 return exit_code
if __name__=='__main__':sys.exit(main())
