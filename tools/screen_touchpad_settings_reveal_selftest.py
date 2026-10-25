"""Run actual configuration/auto-hide consumers with in-memory OS boundaries."""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,sys,datetime
ROOT=pathlib.Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def body(text,name,kind='function'):
 pattern=(r'^struct '+re.escape(name)+r'\s*\{' if kind=='struct' else r'^inline [^\n{]+\b'+re.escape(name)+r'\([^\n]*\)(?:\s*noexcept)?\s*\{')
 matches=list(re.finditer(pattern,text,re.M))
 if len(matches)!=1:raise ValueError(f'{name}: expected one definition, got {len(matches)}')
 start=matches[0].start();opening=text.index('{',start);depth=0;state='code';i=opening
 while i<len(text):
  c=text[i];n=text[i+1:i+2]
  if state=='line':
   if c=='\n':state='code'
  elif state=='block':
   if c=='*' and n=='/':state='code';i+=1
  elif state in ('"',"'"):
   if c=='\\':i+=1
   elif c==state:state='code'
  elif c=='/' and n=='/':state='line';i+=1
  elif c=='/' and n=='*':state='block';i+=1
  elif c in ('"',"'"):state=c
  elif c=='{':depth+=1
  elif c=='}':
   depth-=1
   if not depth:
    end=i+1
    if kind=='struct':end=text.index(';',end)+1
    return text[start:end],text[:start].count('\n')+1
  i+=1
 raise ValueError(f'unbalanced body: {name}')
def run():
 ap=argparse.ArgumentParser();ap.add_argument('output',type=pathlib.Path);ap.add_argument('--negative',action='store_true');ap.add_argument('--negative-abort',action='store_true');ap.add_argument('--negative-missing',action='store_true');args=ap.parse_args()
 out=args.output.resolve();out.mkdir(parents=True,exist_ok=False);frozen=out/'frozen';frozen.mkdir()
 receipt={'ok':False,'negative':args.negative,'negativeAbort':args.negative_abort,'negativeMissing':args.negative_missing,'scope':'Actual configure/button sync+refresh/permission/auto-hide consumers; HWND, Surface, artwork, release and clock boundaries mocked','realWindowsCreated':False,'productStarted':False,'inputInjected':False,'sources':[],'segments':[],'startedAt':datetime.datetime.now().isoformat()}
 try:
  files=['native/screen_touchpads.h','native/screen_button_overlay.h','native/screen_touchpad_auto_hide.h','deps/json/json.hpp','tools/screen_touchpad_settings_reveal_selftest.cpp','tools/screen_touchpad_settings_reveal_selftest.py']
  for rel in files:
   src=ROOT/rel;dst=frozen/src.name;shutil.copyfile(src,dst)
   if digest(src)!=digest(dst):raise RuntimeError(f'source changed during copy: {rel}')
   receipt['sources'].append({'path':str(src),'frozen':str(dst),'sha256':digest(dst)})
  texts={p.name:p.read_text(encoding='utf-8-sig') for p in frozen.iterdir() if p.suffix=='.h'}
  def segment(source,name,kind='function'):
   s,line=body(texts[source],name,kind);receipt['segments'].append({'source':source,'name':name,'line':line,'sha256':hashlib.sha256(s.encode()).hexdigest()});return s
  h='screen_touchpads.h';bh='screen_button_overlay.h'
  types=[]
  for name in ['Mode','SummonPosition','StandaloneSpecialMode','Layout','TouchFeedbackMode']:
   m=re.search(r'^enum class '+name+r' \{[^\n]+',texts[h],re.M)
   if not m:raise ValueError(name)
   types.append(m.group())
  (frozen/'types.inc').write_text('\n'.join(types),encoding='utf-8')
  for filename,content in [
   ('config.inc',segment(h,'Config','struct')),
   ('auto_hide_state.inc',segment(h,'AutoHideState','struct')),
   ('selectors.inc','\n'.join(segment(h,n) for n in ['effectiveSummonPosition','effectiveSpecialMask','effectiveRearMask','standaloneSpecialProfile'])),
   ('button_permitted.inc',segment(bh,'buttonPermitted')),
   ('button_visibility.inc',segment(bh,'buttonVisibilityState')),
   ('recovery_state.inc',segment(bh,'ButtonInputRecovery','struct')),
   ('buttons_refresh.inc',segment(bh,'buttonsRefresh')),
   ('buttons_need_refresh.inc',segment(bh,'buttonsNeedRefresh'))]:
   (frozen/filename).write_text(content+'\n',encoding='utf-8')
  sync=segment(bh,'buttonNativeInputSync')
  if args.negative_abort:
   sync=sync.replace('if(failure==E_ABORT && attempt==0)', 'if(false && attempt==0)')
  (frozen/'button_sync.inc').write_text(sync+'\n',encoding='utf-8')
  cfg=segment(h,'configure')
  if args.negative_missing:
   cfg=cfg.replace('(revealFromSettings && buttonsNeedRefresh(cfg))', 'false')
  if args.negative:
   old='revealFromSettings || reset ||'
   if cfg.count(old)!=1:raise RuntimeError('negative operand not unique')
   cfg=cfg.replace(old,'reset ||');receipt['negativeMutation']='Remove only revealFromSettings operand; restore original same-config behavior'
  (frozen/'configure.inc').write_text(cfg+'\n',encoding='utf-8')
  main=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig');line='if(profile==ymcc::screenpads::activeProfile.load()) ymcc::screenpads::configure(next,profile,true);'
  receipt['settingsIpcWired']=main.count(line)==1
  receipt['inactiveProfileClearsRuntimeVisibility']='if(!current) {j["buttonVisibility"]=nullptr;' in texts[h]
  if not receipt['settingsIpcWired'] or not receipt['inactiveProfileClearsRuntimeVisibility']:raise RuntimeError('IPC/profile boundary assertion failed')
  vcvars=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
  exe=out/'settings-reveal.exe';report=out/'replay.json';build=out/'build.cmd'
  build.write_text(f'@echo off\nset "PATH=C:\\Windows\\System32;C:\\Windows;%PATH%"\ncall "{vcvars}" 10.0.26100.0 >nul\nif errorlevel 1 exit /b 10\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE /I"{frozen}" "{frozen / "screen_touchpad_settings_reveal_selftest.cpp"}" /Fo"{out / "fixture.obj"}" /Fe"{exe}" /link /INCREMENTAL:NO\nif errorlevel 1 exit /b 11\ndumpbin /nologo /imports "{exe}" > "{out / "imports.txt"}"\n',encoding='ascii')
  with (out/'compile.log').open('w',encoding='utf-8') as log:
   result=subprocess.run(['cmd','/d','/c',str(build)],stdout=log,stderr=subprocess.STDOUT)
  receipt['compileExitCode']=result.returncode
  if result.returncode:raise RuntimeError('compile failed; see compile.log')
  imports=(out/'imports.txt').read_text(errors='replace')
  receipt['noWindowsUiInputOrWinRTImports']=not re.search(r'\b(?:USER32|GDI32|DINPUT8|COMBASE|WINDOWSAPP|XINPUT[^\s]*)\.dll\b',imports,re.I)
  if not receipt['noWindowsUiInputOrWinRTImports']:raise RuntimeError('unexpected input/UI dependency')
  result=subprocess.run([str(exe),str(report)],capture_output=True,text=True);(out/'run.log').write_text(result.stdout+result.stderr,encoding='utf-8')
  receipt['runExitCode']=result.returncode;r=json.loads(report.read_text());receipt['checks']=r['checks'];receipt['failures']=[c['name'] for c in r['cases'] if not c['ok']]
  expected=[n+'-explicit-same-selection-reveals-and-restarts' for n in ['standalone-Steam','standalone-PS5','Steam-Deck','PS5','Xbox']]
  if args.negative_abort:expected=['sync-E_ABORT-single-rebind-restores-both-keys','attach-E_ABORT-single-rebind-restores-both-keys','repeated-E_ABORT-stops-after-two-attempts']
  if args.negative_missing:expected=['same-settings-repair-hidden-missing-button','same-settings-repair-unexpected-hidden-artwork','same-settings-recover-after-bounded-retry-exhaustion']
  if args.negative or args.negative_abort or args.negative_missing:receipt['negativeDetected']=result.returncode==1 and sorted(receipt['failures'])==sorted(expected);receipt['ok']=receipt['negativeDetected']
  else:receipt['ok']=result.returncode==0 and r['ok']
  receipt['sourcesUnchanged']=all(digest(pathlib.Path(s['path']))==s['sha256']==digest(pathlib.Path(s['frozen'])) for s in receipt['sources'])
  receipt['ok']=receipt['ok'] and receipt['sourcesUnchanged'];receipt['exeSha256']=digest(exe)
  receipt['limitations']=['No live WinRT, OS hit testing, input injection, Steam or hardware output.','Button sync and refresh, configuration and auto-hide execute production functions; pad layout/artwork/release and WinRT Surface are mocks.','Field ZIP proves E_ABORT and missing one key with inputTeardown=false; it does not identify which internal dispatcher callback caused the abort.']
 except Exception as e:receipt['error']=str(e)
 finally:
  receipt['finishedAt']=datetime.datetime.now().isoformat();(out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps({k:receipt[k] for k in ['ok','checks','failures','error'] if k in receipt},ensure_ascii=False));return 0 if receipt['ok'] else 1
if __name__=='__main__':sys.exit(run())
