from pathlib import Path
import argparse,ast,datetime,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
base=Path(__file__).resolve().parent
detected=next((p for p in base.parents if (p/'native/main.cpp').is_file()),None)
parser=argparse.ArgumentParser();parser.add_argument('--repo',type=Path,default=detected);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--negative-preimage',type=Path);options=parser.parse_args()
assert options.repo,'Cannot discover repository; pass --repo only for a relocated support runner'
repo=options.repo.resolve();out=options.out.resolve();out.mkdir(parents=True,exist_ok=True)
def sha(b):return hashlib.sha256(b).hexdigest()
def write(path,text):
 with path.open('x',encoding='utf-8',newline='') as f:f.write(text)
def main():
 run=out/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir()
 raw=((options.negative_preimage/'main.cpp.before') if options.negative_preimage else (repo/'native/main.cpp')).read_bytes();source=raw.decode('utf-8-sig').replace('\r\n','\n')
 parser=(repo/'tools/virtual_sample_tree_perf_selftest.py').read_bytes();module=ast.parse(parser.decode('utf-8-sig'));nodes=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name in ['clean','match_brace','source_function']]
 scope={'re':re};exec(compile(ast.Module(body=nodes,type_ignores=[]),'<source-parser>','exec'),scope);extract=scope['source_function']
 capture='static bool taskViewCapturePower('
 signatures=['static bool taskViewContextCurrent(','static json nativeTouchTaskViewButton(','static bool ipc_cmd_async(']+([capture] if capture in source else [])
 bodies={s:extract(source,s) for s in signatures}
 surface=(repo/'native/game_overlay_surface.h').read_bytes()
 main_presented=extract(surface.decode('utf-8-sig').replace('\r\n','\n'),'static bool gameOverlayMainPresented(')
 guardStart='        if(cmd=="window.touchTaskViewButton"&&sourceView!=g_view.Get())'
 guard=source[source.index(guardStart):source.index('        // The main window\'s render handshake',source.index(guardStart))]
 registration='    ipc_on("window.touchTaskViewButton", [](const json& args) -> json {return nativeTouchTaskViewButton(args);});'
 assert source.count(registration)==1
 body=bodies['static json nativeTouchTaskViewButton(']
 assert all(x not in body for x in ['SendInput','InjectTouchInput','nativeSendTaskView','shell.taskView'])
 assert source.index(guardStart,source.index('static void ipc_dispatch('))<source.index('        if (cmd == "shell.taskView")',source.index('static void ipc_dispatch('))
 header=((options.negative_preimage/'task_view_touch_activation.h.before') if options.negative_preimage else (repo/'native/task_view_touch_activation.h')).read_bytes();j=(repo/'deps/json/json.hpp').read_bytes()
 sleep_headers={name:(repo/'native'/name).read_bytes() for name in ['sleep_power_state.h','sleep_power_observation.h']}
 for name,data in [('task_view_touch_activation.h',header),('json.hpp',j),*sleep_headers.items()]:
  with (run/name).open('xb') as f:f.write(data)
 template=(base/'task_view_touch_native_fixture.cpp.in').read_bytes();fixture=template.decode('utf-8').replace('__MAIN_PRESENTED__',main_presented).replace('__CONTEXT__',bodies.get(capture,'')+'\n'+bodies[signatures[0]]).replace('__ENDPOINT__',body).replace('__ASYNC__',bodies[signatures[2]]).replace('__SOURCE_GUARD__',guard).replace('__REGISTRATION__',registration)
 write(run/'fixture.cpp',fixture)
 sdk=Path('C:/Program Files (x86)/Windows Kits/10');version='10.0.26100.0';vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
 includes=';'.join(str(sdk/'Include'/version/x) for x in ['ucrt','shared','um','winrt','cppwinrt']);libs=';'.join(str(sdk/'Lib'/version/x/'x64') for x in ['ucrt','um'])
 profiles=[]
 for name,flags in ([('LegacyOverlayControl',['/O2','/DNDEBUG','/MT','/DTASKVIEW_ONLY_OVERLAY_CONTROL'])] if options.negative_preimage else [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]):
  args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/sourceDependencies',str(run/(name+'-dependencies.json')),*flags,str(run/'fixture.cpp'),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  batch='@echo off\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\nset "INCLUDE='+includes+';%INCLUDE%"\nset "LIB='+libs+';%LIB%"\n"%SystemRoot%\\System32\\where.exe" cl.exe > "'+str(run/(name+'-compiler.txt'))+'"\n'+subprocess.list2cmdline(args)+'\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /imports "'+str(run/(name+'.exe'))+'" > "'+str(run/(name+'-imports.txt'))+'"\nexit /b %errorlevel%\n'
  write(run/(name+'.cmd'),batch)
  p=subprocess.run(['cmd.exe','/d','/c',str(run/(name+'.cmd'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=120)
  write(run/(name+'-compile.log'),p.stdout+p.stderr)
  assert p.returncode==0,p.stdout+p.stderr
  imports=(run/(name+'-imports.txt')).read_text(encoding='utf-8',errors='replace').lower();assert not any(n in imports for n in ['user32.dll','ole32.dll','shell32.dll','sendinput','injectsyntheticpointerinput','createsyntheticpointerdevice','createsyntheticpointerdevice','createprocessw'])
  p=subprocess.run([str(run/(name+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=30)
  write(run/(name+'-result.json'),p.stdout);write(run/(name+'-stderr.txt'),p.stderr)
  if options.negative_preimage:
   assert p.returncode==1 and 'overlay touch must use display+panel' in p.stderr,'Original endpoint did not expose the expected overlay refusal'
   proof={'pass':True,'expectedOriginalRejection':True,'actualExitCode':p.returncode,'stderr':p.stderr,'run':str(run),'actualOSInput':False}
   write(out/'NEGATIVE-RESULT.json',json.dumps(proof,indent=2));print(json.dumps(proof));return
  assert p.returncode==0,p.stderr
  result=json.loads(p.stdout);assert result['pass'];profiles.append({'profile':name,**result})
 current=(repo/'native/main.cpp').read_text(encoding='utf-8-sig')
 assert all(extract(current,s)==b for s,b in bodies.items()) and guard in current and registration in current
 assert (repo/'native/task_view_touch_activation.h').read_bytes()==header
 assert all((repo/'native'/name).read_bytes()==data for name,data in sleep_headers.items())
 assert extract((repo/'native/game_overlay_surface.h').read_text(encoding='utf-8-sig'),'static bool gameOverlayMainPresented(')==main_presented
 result={'pass':True,'profiles':profiles,'run':str(run),'functionHashes':{s:sha(b.encode()) for s,b in {**bodies,'static bool gameOverlayMainPresented(':main_presented}.items()},'guardHash':sha(guard.encode()),'registrationHash':sha(registration.encode()),'headerSha256':sha(header),'inputPins':{'mainObservedSha256':sha(raw),'parserSha256':sha(parser),'templateSha256':sha(template),'jsonSha256':sha(j),'gameOverlaySurfaceSha256':sha(surface),**{name:sha(data) for name,data in sleep_headers.items()}},'actualOSInput':False,'fullNative':False}
 write(run/'result.json',json.dumps(result,indent=2));write(out/'NATIVE-RESULT.json',json.dumps(result,indent=2));print(json.dumps(result))
if __name__=='__main__':main()
