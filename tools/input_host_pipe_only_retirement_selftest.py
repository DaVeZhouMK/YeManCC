#!/usr/bin/env python3
"""Actual pipe-only no-PREPARE retirement with preserved failing main negative."""
from pathlib import Path
import argparse,datetime,hashlib,importlib.util,json,os,re,subprocess,sys,traceback
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT.parents[2]/'Testing/YMCC/Evidence/VC-PipeOnly23'
BEFORE=BASE/'r095830-669940-62244/source/native/main.cpp'
BEFORE_SHA='5a2e5b4ee51f53641d224af2f51ca4d018ab585538ba6df876b14a2cac96c72c'
HELPER='inputHostPipeOnlyReturnAdmittedLocked'
FIX='tools/fixtures/input_host_clear_retirement/pipe_only_case.cpp.in'
SELF='tools/input_host_pipe_only_retirement_selftest.py'
def sha(b):return hashlib.sha256(b).hexdigest()
def ident(p):return {'path':str(p),'sha256':sha(p.read_bytes()),'bytes':p.stat().st_size}
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def need(v,s):
 if not v:raise RuntimeError(s)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path);args=ap.parse_args()
 out=args.out or BASE/('r'+datetime.datetime.now().strftime('%H%M%S-%f')+'-'+str(os.getpid()))
 need(out.is_absolute() and not out.exists() and not out.resolve().is_relative_to(ROOT.resolve()),'fresh absolute private output required');out.mkdir(parents=True);out=out.resolve()
 receipt={'status':'PREPARING','pass':False,'sourcesStable':False,'beforeSha256':BEFORE_SHA,'runs':[],
 'hardwareAccepted':False,'fullHostLifecycleAccepted':False,'productDriverServiceDeviceSleepDeploymentExecuted':False}
 try:
  runner=ROOT/'tools/input_host_clear_retirement_selftest.py';spec=importlib.util.spec_from_file_location('actual_clear_consumer',runner);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  files=list(dict.fromkeys([*m.SOURCES,FIX,SELF]));inputs={r:(ROOT/r).read_bytes() for r in files};src=out/'source'
  for r,b in inputs.items():p=src/r;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  old=BEFORE.read_bytes();need(sha(old)==BEFORE_SHA,'fixed original failing native source changed');(out/'old-main.cpp').write_bytes(old)
  sourceText={'current':inputs['native/main.cpp'].decode('utf-8-sig'),'historical':old.decode('utf-8-sig')};stop=inputs['native/virtual_controller_stop_runtime.h'].decode('utf-8-sig')
  parts={}
  for kind,text in sourceText.items():
   names=[n for n in m.NATIVE if n not in (HELPER,'inputHostPrimaryThreadRetirementProvenLocked')] if kind=='historical' else m.NATIVE
   parts[kind]={n:m.extract(text,n) for n in names};parts[kind].update({n:m.extract(stop,n) for n in m.STOP})
  receipt['sources']=[{'relative':r,'original':str(ROOT/r),'snapshot':str(src/r),'sha256':sha(b)} for r,b in inputs.items()]
  receipt['functions']={k:{n:{i:v for i,v in p.items() if i!='text'} for n,p in ps.items()} for k,ps in parts.items()}
  save(out/'SOURCE-FREEZE.json',{'sources':receipt['sources'],'actualFunctions':parts,'oldMain':ident(out/'old-main.cpp')})
  oldSuite=inputs[m.FIX+'/suite.cpp.in'].decode('utf-8-sig');need(oldSuite.count('int main(){try{')==1,'old fixture common boundary')
  common=oldSuite[:oldSuite.index('int main(){try{')];new=inputs[FIX].decode('utf-8-sig')
  leaf=inputs[m.MEMORY].decode('utf-8-sig');prefix=leaf[:leaf.index('using json=nlohmann::json;')]
  variants=[('current','current',None,False),('historical-old-main','historical',None,True),
   ('omit-PREPARE-may-duty','current',('!g_inputHostPrepareMayHaveReachedHost &&','true &&'),True),
   ('omit-history-duty','current',('inputHostTargetReturnHistorySettledLocked()','true'),True),
   ('omit-pipe-duty','current',('&& inputHostPipeRetirementProvenLocked()',''),True)]
  required={'historical-old-main':'pipe-only paid local resources must clear tuple without physical source',
   'omit-PREPARE-may-duty':'independent known-or-unknown duty vetoes pipe-only admission',
   'omit-history-duty':'independent known-or-unknown duty vetoes pipe-only admission',
   'omit-pipe-duty':'active lease blocks pipe-only admission even with no pending IO'}
  cap=read(m.CAPTURE/'build-environment.json');man=read(m.CAPTURE/'toolchain-manifest.json');env=dict(cap['environment'])
  for k in list(env):
   if k.upper() in ('CL','_CL_','LINK','_LINK_'):del env[k]
  env['INCLUDE']=';'.join(r['frozen'] for r in man['roots'] if r['category']=='INCLUDE');env['LIB']=';'.join(r['frozen'] for r in man['roots'] if r['category']=='LIB');env['LIBPATH']=env['LIB'];env['VSLANG']='1033'
  tools=[]
  for name in ('cl.exe','link.exe','dumpbin.exe'):
   p=Path(cap['tools'][name]['paths'][0]).resolve();bound=next(x for x in man['tools'] if x['name']==name);need(sha(p.read_bytes())==bound['sha256'],'captured tool changed '+name);tools.append(ident(p))
  cl=Path(tools[0]['path']);dump=Path(tools[2]['path']);bytool={str(Path(r['frozen']).resolve()).lower():r for r in man['files']}
  bysource={str((src/r).resolve()).lower():sha(b) for r,b in inputs.items()};closure={}
  receipt['toolchain']={'capture':ident(m.CAPTURE/'build-environment.json'),'manifest':ident(m.CAPTURE/'toolchain-manifest.json'),'tools':tools,'python':ident(Path(sys.executable))}
  for profile,flags in [('Release',['/O2','/DNDEBUG']),('Checked',['/Od','/RTC1'])]:
   for name,kind,mutation,negative in variants:
    target=out/(profile+'-'+name);target.mkdir();ps=parts[kind];text=sourceText[kind]
    raw=re.findall(r'^static HANDLE g_inputHostUnadoptedPipe\s*=\s*nullptr\s*;',text,re.M);need(len(raw)==1,'raw slot source declaration')
    helper=ps[HELPER]['text'] if kind=='current' else ''
    if mutation:
     before,after=mutation;need(helper.count(before)==1,'unique semantic mutation '+name);helper=helper.replace(before,after,1)
    replacements={'@@PIPE_LEAVES@@':prefix,'@@PIPE_CLOSE@@':ps['inputHostPipeCloseOwned']['text'],
     '@@PIPE_WRAPPER@@':raw[0]+'\n'+ps['inputHostPipeRetirementProvenLocked']['text'],
     '@@PRIMARY_HELPER@@':ps.get('inputHostPrimaryThreadRetirementProvenLocked',{}).get('text',''),
     '@@CLEAR@@':ps['inputHostClearLocked']['text'],'@@OWNED@@':ps[m.STOP[0]]['text'],'@@DISABLED@@':ps[m.STOP[1]]['text'],
     '@@PIPE_ONLY_HELPER@@':helper,'@@BUS_QUARANTINE@@':ps['inputHostReconcileQuarantine']['text'],
     '@@LIFECYCLE_QUARANTINE@@':ps['inputHostReconcileQuarantineOnLifecycleThread']['text']}
    generated=common
    for mark,value in replacements.items():need(generated.count(mark)==1,'source marker '+mark);generated=generated.replace(mark,value)
    need(new.count('@@BASE_COMMON@@')==1,'new scenario marker');generated='#define HAS_PIPE_ONLY_HELPER '+str(int(kind=='current'))+'\n'+new.replace('@@BASE_COMMON@@',generated)
    fixture=target/'fixture.cpp';fixture.write_text(generated,encoding='utf-8',newline='\n');deps=target/'dependencies.json';exe=target/'fixture.exe'
    cmd=[cl,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/MT',*flags,
      '/I'+str(src/'native'),'/I'+str(src/'deps/json'),'/I'+str(src/'tools'),'/sourceDependencies',deps,fixture,
      '/Fo'+str(target/'fixture.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB']
    save(target/'command.json',[str(x) for x in cmd]);built=m.execute(cmd,target,env,target/'build.log',60);need(built.returncode==0,'compile failed '+str(target/'build.log'))
    dep=read(deps)['Data'];observed=[dep['Source'],*dep['Includes']]
    for file in observed:
     p=Path(file).resolve();k=str(p).lower();h=sha(p.read_bytes())
     if p==fixture:need(h==sha(generated.encode()),'generated input changed')
     elif k in bysource:need(h==bysource[k],'source snapshot changed')
     else:need(k in bytool and h==bytool[k]['sha256'],'unbound compiler input '+file);closure[k]=ident(p)
    libs=re.findall(r'^\s*(?:Searching|正在搜索)\s+(.+?\.lib)\s*:',built.stdout+built.stderr,re.M|re.I);need(libs,'library evidence absent')
    for file in libs:
     p=Path(file.strip()).resolve();k=str(p).lower();need(k in bytool and sha(p.read_bytes())==bytool[k]['sha256'],'unbound library '+file);closure[k]=ident(p)
    ran=m.execute([exe],target,env,target/'run.log',20);need(ran.stdout.strip(),'runtime result missing');result=json.loads(ran.stdout);save(target/'RESULT.json',result)
    accepted=ran.returncode==2 and result['pass'] is False and required[name] in result['failures'] if negative else ran.returncode==0 and result['pass'] is True
    need(result['oracleLiveBeforeExit']==0 and result['handlesBefore']==result['handlesAfter'],'owned handles not returned before exit')
    imports=m.execute([dump,'/imports',exe],target,env,target/'imports.txt',15);need(imports.returncode==0 and not any(x in imports.stdout.lower() for x in ('hid.dll','setupapi.dll','dinput8.dll','xinput','steam_api','winmm.dll','user32.dll')),'unexpected device imports')
    row={'profile':profile,'variant':name,'expectedNegative':negative,'accepted':accepted,'checks':result['checks'],
     'failureCount':len(result['failures']),'requiredFailure':required.get(name),'failedAssertions':result['failures'],
     'result':ident(target/'RESULT.json'),'binary':ident(exe),'privateHandlesBefore':result['handlesBefore'],
     'privateHandlesAfter':result['handlesAfter'],'oracleLiveBeforeExit':result['oracleLiveBeforeExit'],'compilerInputCount':len(observed)}
    receipt['runs'].append(row);save(out/'PROGRESS.json',receipt['runs']);print(json.dumps({k:row[k] for k in ('profile','variant','accepted','checks','failureCount')}),flush=True)
    need(accepted,'specific semantic assertion contract failed '+profile+'/'+name)
  receipt['changed']=[r for r,b in inputs.items() if (ROOT/r).read_bytes()!=b];receipt['sourcesStable']=not receipt['changed'];receipt['boundToolInputs']=list(closure.values())
  for t in tools+list(closure.values()):need(sha(Path(t['path']).read_bytes())==t['sha256'],'tool changed during execution')
  receipt['pass']=receipt['sourcesStable'];receipt['executionPassed']=True
  receipt['status']='PIPE_ONLY_RECOVERY_AND_INDEPENDENT_VETO_CONSUMERS_VERIFIED' if receipt['pass'] else 'FROZEN_EXECUTION_VERIFIED_LIVE_SOURCES_CHANGED'
  receipt['limits']=['external Host/SDK/physical/history are memory facts','queue port followed by explicit actual lifecycle invocation; not real app scheduler','historical old main lacks new helper so compiled historical variant executes only original two recovery cases','no product/driver or true sleep acceptance']
  save(out/'RESULT.json',receipt);print(json.dumps({'status':receipt['status'],'out':str(out),'sourcesStable':receipt['sourcesStable']}));return 0 if receipt['pass'] else 2
 except Exception as error:
  receipt.update({'status':'FAILED','error':str(error),'traceback':traceback.format_exc()});save(out/'RESULT.json',receipt);print(str(error),file=sys.stderr);return 1
if __name__=='__main__':sys.exit(main())
