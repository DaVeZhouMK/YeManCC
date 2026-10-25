#!/usr/bin/env python3
"""Execute complete actual Clear/quarantine consumers and actual pipe Runtime.

Only explicit IO/failure and external physical/SDK ports are replaced. Successful
HANDLE leaves use real private Win32 handles. No installation/product is run.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,subprocess,sys,traceback
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT.parents[2]/'Testing/YMCC/Evidence/VC-ClearRetire23'
CAPTURE=ROOT.parents[2]/'Build/Validation/VirtualController-Lifecycle-20261021-G3E-XboxSwitch/native-verified'
FIX='tools/fixtures/input_host_clear_retirement'
SELF='tools/input_host_clear_retirement_selftest.py'
MEMORY='tools/input_pipe_retirement_memory_selftest.cpp'
NATIVE=('inputHostPrimaryThreadRetirementProvenLocked','inputHostPipeOnlyReturnAdmittedLocked','inputHostPipeCloseOwned','inputHostPipeRetirementProvenLocked','inputHostClearLocked',
        'inputHostReconcileQuarantine','inputHostReconcileQuarantineOnLifecycleThread')
STOP=('inputHostOwnedCleanupProvenLocked','inputHostDisabledCleanupProvenLocked')
SOURCES=('native/main.cpp','native/input_host_pipe_transport.h','native/virtual_controller_stop_runtime.h',
         'native/input_host_process_retirement.h','deps/json/json.hpp',MEMORY,
         'tools/input_pipe_test_allocation_fault.h',SELF,FIX+'/suite.cpp.in',FIX+'/README.md')
REQUIRED_NEGATIVE={
 'drop-clear-pipe-gate':'failed original Close keeps old tuple',
 'drop-lifecycle-pipe-gate':'failed original Close prevents physical restore',
 'rearm-before-clear':'all rearm gates run only after Clear closes old tuple'}

def sha(b):return hashlib.sha256(b).hexdigest()
def identity(p):return {'path':str(p),'sha256':sha(p.read_bytes()),'bytes':p.stat().st_size}
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def need(v,msg):
 if not v: raise RuntimeError(msg)
def mask(text):
 return re.sub(r'//[^\r\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'',lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text)
def extract(text,name):
 code=mask(text);hits=list(re.finditer(r'^static\s+(?:bool|void)\s+'+re.escape(name)+r'\s*\([^;]*?\)\s*(?:noexcept\s*)?\{',code,re.M))
 need(len(hits)==1,'expected unique complete production function '+name)
 start=hits[0].start();o=code.index('{',start);depth=0
 for end in range(o,len(code)):
  depth+=(code[end]=='{')-(code[end]=='}')
  if depth==0:
   value=text[start:end+1];return {'text':value,'sha256':sha(value.encode()),'line':code.count('\n',0,start)+1}
 raise RuntimeError('unclosed '+name)
def execute(argv,cwd,env,log,timeout=60):
 p=subprocess.run([str(x) for x in argv],cwd=cwd,env=env,capture_output=True,encoding='utf-8',errors='replace',timeout=timeout)
 log.write_text(p.stdout+p.stderr,encoding='utf-8');return p

def variants(parts):
 clear=parts['inputHostClearLocked']['text'];lifecycle=parts['inputHostReconcileQuarantineOnLifecycleThread']['text']
 gate='|| !pipeRetirementProven';early='if (!inputHostPipeRetirementProvenLocked()) return;'
 need(clear.count(gate)==2,'two actual Clear pipe guards')
 need(lifecycle.count(early)==1,'unique actual lifecycle pipe guard')
 call='    inputHostClearLocked();';wake='    const bool wakeExecutionReturnPreserved=inputHostWakeReturnedLocked(retiredHostEpoch,retiredRunId);'
 need(lifecycle.count(call)==1 and lifecycle.count(wake)==1,'actual Clear/rearm sequence anchors')
 reordered=lifecycle.replace(call,'    // controlled semantic fault: defer old tuple Clear',1).replace(wake,wake+'\n'+call,1)
 return [('current',clear,lifecycle,False),
         ('drop-clear-pipe-gate',clear.replace(gate,'|| false'),lifecycle,True),
         ('drop-lifecycle-pipe-gate',clear,lifecycle.replace(early,'if (false) return;',1),True),
         ('rearm-before-clear',clear,reordered,True)]

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--out',type=Path);ap.add_argument('--toolchain',type=Path,default=CAPTURE)
 args=ap.parse_args()
 requested=args.out or EVIDENCE/('r'+datetime.datetime.now().strftime('%H%M%S-%f')+'-'+str(os.getpid()))
 need(requested.is_absolute(),'fresh absolute output required');out=requested.resolve()
 need(not out.exists() and not out.is_relative_to(ROOT.resolve()) and not out.is_relative_to(args.toolchain.resolve()),'fresh private output outside source/toolchain required')
 out.mkdir(parents=True)
 receipt={'status':'PREPARING','pass':False,'wholeGoalComplete':False,'installedOrHardwareAccepted':False,
          'scope':'Complete actual Clear, wrapper, BUS/lifecycle quarantine, Owned/Disabled plus actual pipe Runtime; explicit external facts and controlled IO leaves.',
          'productSdkDriverServiceDeviceSleepDeploymentExecuted':False,'processExitUsedAsResourceReturn':False,
          'arbitraryOsBlockingBound':'UNKNOWN','out':str(out),'runs':[]}
 try:
  inputs={r:(ROOT/r).read_bytes() for r in SOURCES};src=out/'source'
  for r,b in inputs.items():p=src/r;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  receipt['sources']=[{'relative':r,'original':str(ROOT/r),'snapshot':str(src/r),'sha256':sha(b)} for r,b in inputs.items()]
  main_text=inputs['native/main.cpp'].decode('utf-8-sig');stop_text=inputs['native/virtual_controller_stop_runtime.h'].decode('utf-8-sig')
  parts={n:extract(main_text,n) for n in NATIVE};parts.update({n:extract(stop_text,n) for n in STOP})
  receipt['functions']={n:{k:v for k,v in b.items() if k!='text'} for n,b in parts.items()}
  save(out/'SOURCE-FREEZE.json',{'sources':receipt['sources'],'functions':parts})
  raw=re.findall(r'^static HANDLE g_inputHostUnadoptedPipe\s*=\s*nullptr\s*;',main_text,re.M);need(len(raw)==1,'actual raw-slot declaration')
  leaf=inputs[MEMORY].decode('utf-8-sig');anchor='using json=nlohmann::json;';need(leaf.count(anchor)==1,'existing memory leaves anchor');prefix=leaf[:leaf.index(anchor)]
  need(prefix.count('#include "input_host_pipe_transport.h"')==1,'actual Runtime included once')
  template=inputs[FIX+'/suite.cpp.in'].decode('utf-8-sig')
  tc=args.toolchain.resolve();capPath=tc/'build-environment.json';manPath=tc/'toolchain-manifest.json';cap=read(capPath);man=read(manPath)
  env=dict(cap['environment'])
  for k in list(env):
   if k.upper() in ('CL','_CL_','LINK','_LINK_'):del env[k]
  env['INCLUDE']=';'.join(x['frozen'] for x in man['roots'] if x['category']=='INCLUDE')
  env['LIB']=';'.join(x['frozen'] for x in man['roots'] if x['category']=='LIB');env['LIBPATH']=env['LIB'];env['VSLANG']='1033'
  tools=[]
  for name in ('cl.exe','link.exe','dumpbin.exe'):
   p=Path(cap['tools'][name]['paths'][0]).resolve();bound=next((x for x in man['tools'] if x['original'].lower()==str(p).lower()),None)
   need(bound and sha(p.read_bytes())==bound['sha256'],'captured tool identity changed '+str(p));tools.append(identity(p))
  cl=Path(tools[0]['path']);dump=Path(tools[2]['path'])
  receipt['toolchain']={'capture':identity(capPath),'manifest':identity(manPath),'tools':tools,'python':identity(Path(sys.executable))}
  bytool={str(Path(x['frozen']).resolve()).lower():x for x in man['files']}
  bysource={str((src/r).resolve()).lower():sha(b) for r,b in inputs.items()};closure={}
  all_variants=variants(parts)
  for profile,flags in [('Release',['/O2','/DNDEBUG']),('Checked',['/Od','/RTC1'])]:
   for name,clear,lifecycle,negative in all_variants:
    target=out/(profile+'-'+name);target.mkdir();fixture=target/'fixture.cpp';deps=target/'dependencies.json';exe=target/'fixture.exe'
    replacements={'@@PIPE_LEAVES@@':prefix,'@@PIPE_CLOSE@@':parts['inputHostPipeCloseOwned']['text'],
      '@@PIPE_WRAPPER@@':raw[0]+'\n'+parts['inputHostPipeRetirementProvenLocked']['text'],
      '@@PRIMARY_HELPER@@':parts['inputHostPrimaryThreadRetirementProvenLocked']['text'],
      '@@CLEAR@@':clear,'@@OWNED@@':parts[STOP[0]]['text'],'@@DISABLED@@':parts[STOP[1]]['text'],
      '@@PIPE_ONLY_HELPER@@':parts['inputHostPipeOnlyReturnAdmittedLocked']['text'],
      '@@BUS_QUARANTINE@@':parts['inputHostReconcileQuarantine']['text'],'@@LIFECYCLE_QUARANTINE@@':lifecycle}
    generated=template
    for mark,value in replacements.items():need(generated.count(mark)==1,'fixture marker '+mark);generated=generated.replace(mark,value)
    fixture.write_text(generated,encoding='utf-8',newline='\n')
    cmd=[cl,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/MT',*flags,
         '/I'+str(src/'native'),'/I'+str(src/'deps/json'),'/I'+str(src/'tools'),
         '/sourceDependencies',deps,fixture,'/Fo'+str(target/'fixture.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB']
    save(target/'command.json',[str(x) for x in cmd]);built=execute(cmd,target,env,target/'build.log')
    need(built.returncode==0,'actual production consumer must compile '+str(target/'build.log'))
    data=read(deps)['Data'];actual=[data['Source'],*data['Includes']]
    for file in actual:
     p=Path(file).resolve();key=str(p).lower();actualSha=sha(p.read_bytes())
     if p==fixture.resolve():need(actualSha==sha(generated.encode()),'generated consumer changed')
     elif key in bysource:need(actualSha==bysource[key],'source snapshot changed '+file)
     else:
      need(key in bytool and actualSha==bytool[key]['sha256'],'compiler input outside captured authority '+file);closure[key]=identity(p)
    libs=re.findall(r'^\s*(?:Searching|正在搜索)\s+(.+?\.lib)\s*:',built.stdout+built.stderr,re.M|re.I);need(libs,'actual linker search receipt required')
    for file in libs:
     p=Path(file.strip()).resolve();key=str(p).lower();need(key in bytool and sha(p.read_bytes())==bytool[key]['sha256'],'linker input outside captured authority '+file);closure[key]=identity(p)
    ran=execute([exe],target,env,target/'run.log',20);need(ran.stdout.strip(),'no executed consumer result '+str(target));result=json.loads(ran.stdout);save(target/'RESULT.json',result)
    if negative:
     accepted=ran.returncode==2 and result['pass'] is False and REQUIRED_NEGATIVE[name] in result['failures']
    else:
     accepted=ran.returncode==0 and result['pass'] is True and result['negativeOnlyRawSalvage']==0
    need(result['oracleLiveBeforeExit']==0 and result['handlesBefore']==result['handlesAfter'],'private resources not returned before exit')
    imp=execute([dump,'/imports',exe],target,env,target/'imports.txt',15)
    need(imp.returncode==0 and not any(x in imp.stdout.lower() for x in ('hid.dll','setupapi.dll','xinput','dinput8.dll','steam_api','winmm.dll','user32.dll')),'unexpected product/device import')
    row={'profile':profile,'variant':name,'expectedNegative':negative,'accepted':accepted,'exitCode':ran.returncode,'checks':result['checks'],
         'failedAssertions':result['failures'],'requiredNegative':REQUIRED_NEGATIVE.get(name),
         'privateHandlesBefore':result['handlesBefore'],'privateHandlesAfter':result['handlesAfter'],
         'oracleLiveBeforeExit':result['oracleLiveBeforeExit'],'negativeOnlyRawSalvage':result['negativeOnlyRawSalvage'],
         'compilerInputCount':len(actual),'binary':identity(exe),'result':identity(target/'RESULT.json')}
    receipt['runs'].append(row);save(out/'PROGRESS.json',receipt['runs'])
    print(json.dumps({k:row[k] for k in ('profile','variant','accepted','checks','exitCode')}),flush=True)
    need(accepted,'consumer did not satisfy required original assertion contract '+profile+'/'+name)
  receipt['sourceChanged']=[r for r,b in inputs.items() if (ROOT/r).read_bytes()!=b]
  receipt['boundToolInputs']=list(closure.values())
  for row in [*tools,*closure.values()]:need(sha(Path(row['path']).read_bytes())==row['sha256'],'tool identity drift '+row['path'])
  receipt['executionPassed']=True
  receipt['pass']=not receipt['sourceChanged']
  receipt['status']='ACTUAL_CLEAR_QUARANTINE_PIPE_RETIREMENT_VERIFIED' if receipt['pass'] else 'EXECUTED_FROZEN_CONSUMERS_PASS_LIVE_SOURCE_CHANGED'
  receipt['remaining']=['full Stop/Start/Host/SDK not executed','physical and OEM return only explicit memory facts','arbitrary blocked dependencies bounded total destruction UNKNOWN','final parent whole-main integration and exact source registration separate']
  save(out/'RESULT.json',receipt)
  print(json.dumps({'status':receipt['status'],'out':str(out),'sourceChanged':receipt['sourceChanged']}));return 0 if receipt['pass'] else 2
 except Exception as error:
  receipt.update({'status':'FAILED','pass':False,'error':str(error),'traceback':traceback.format_exc()});save(out/'RESULT.json',receipt);print(str(error),file=sys.stderr);return 1
if __name__=='__main__':sys.exit(main())
