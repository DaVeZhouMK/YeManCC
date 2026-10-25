#!/usr/bin/env python3
"""Actual Admission/Start/typed Stop/StopLease races; only private kernel objects."""
from pathlib import Path
import argparse,datetime as dt,hashlib,importlib.util,json,os,re,sys
ROOT=Path(__file__).resolve().parents[1]
TEMPLATE='tools/fixtures/input_capture_timer_retirement/stop_receipt_fixture.cpp.in'
BASE='tools/fixtures/input_capture_timer_retirement/stop_wait_kernel_fixture.cpp.in'
AUTH='tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
SOURCES=('native/main.cpp','native/input_capture_completion.h','native/input_capture_startup_wait.h','deps/json/json.hpp',
 'tools/input_capture_stop_receipt_selftest.py',TEMPLATE,BASE,AUTH,
 'tools/input_capture_worker_retirement_selftest.py','tools/input_timer_evidence_adapter_selftest.py')
sha=lambda b:hashlib.sha256(b).hexdigest()
def need(v,m):
 if not v:raise RuntimeError(m)
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output-root',required=True,type=Path);ap.add_argument('--profile',choices=['Release','Checked']);args=ap.parse_args()
 out=args.output_root.resolve();run=out/('r'+dt.datetime.now().strftime('%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 receipt={'schemaVersion':1,'pass':False,'evidenceComplete':False,'status':'RUNNING','run':str(run),'profiles':[],
  'wholeLifecycleAcceptance':False,'actualRetireResourcesBodyExecuted':False,'actualStartWorkerBodyExecuted':False,
  'arbitraryBlockedParentBoundedDestruction':'UNKNOWN','processExitUsedAsCleanup':False}
 def publish():save(run/'RESULT.json',receipt);save(out/'RESULT.json',receipt)
 try:
  source={n:(ROOT/n).read_bytes() for n in SOURCES};snap=run/'source'
  for n,b in source.items():p=snap/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  receipt['sourceHashes']={n:sha(b) for n,b in source.items()}
  spec=importlib.util.spec_from_file_location('receipt_snapshot_ret',snap/'tools/input_capture_worker_retirement_selftest.py');ret=importlib.util.module_from_spec(spec);spec.loader.exec_module(ret);ret.verify_freeze(source,receipt)
  text=source['native/main.cpp'].decode('utf-8-sig')
  funcs={n:ret.function(text,n)['text'] for n in ['inputCaptureStart','inputCaptureStop','inputCaptureResetStartupStop','inputCaptureRequestStop','nativeLifecycleEventIsBase']}
  receipt['actualFunctions']={n:sha(v.encode()) for n,v in funcs.items()}
  base=source[BASE].decode();prefix=base[:base.index('static void verifyLog(')]
  prefix=ret.bind_stop(prefix,text,funcs['inputCaptureStop'],funcs['nativeLifecycleEventIsBase'])
  # Replace only the explicitly fake notification leaf with the actual helper.
  fake=ret.function(prefix,'inputCaptureRequestStop')['text'];prefix=prefix.replace(fake,'static void inputCaptureRequestStop() noexcept;')
  declaration=re.findall(r'^static[^;\n]*\bg_inputCaptureStartupWait\b[^;]*;',text,re.M);need(len(declaration)==1,'Unique real startup owner')
  helpers=declaration[0]+'\n'+funcs['inputCaptureRequestStop']+'\n'+funcs['inputCaptureResetStartupStop']
  cpp_text=source[TEMPLATE].decode().replace('@KERNEL_PREFIX@',prefix).replace('@STARTUP_HELPERS@',helpers).replace('@PRODUCTION_START@',funcs['inputCaptureStart'])
  need(not re.search(r'@[A-Z_]+@',cpp_text),'Unexpanded receipt source')
  compiler,env=ret.helper.compiler_environment(run);authority=json.loads(source[AUTH]);need(Path(authority['native']['compiler']).resolve()==compiler.resolve(),'Compiler declaration mismatch')
  env['INCLUDE']=';'.join(authority['native']['include']);env['LIB']=';'.join(authority['native']['lib'])
  for profile,flags in [('Release',['/O2','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
   if args.profile and args.profile!=profile:continue
   for route,compiled in [('current',cpp_text),('negative-without-admission',cpp_text.replace('    if(!g_captureAdmission.beginStart())return;','',1))]:
    folder=run/(profile+'-'+route);folder.mkdir();cpp=folder/'receipt.cpp';exe=folder/'receipt.exe';cpp.write_text(compiled,encoding='utf-8')
    built=ret.helper.execute([compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4',*flags,
      '/I'+str(snap/'native'),'/I'+str(snap/'deps/json'),cpp,'/Fo'+str(folder/'receipt.obj'),'/Fe'+str(exe),'/link','kernel32.lib','/INCREMENTAL:NO'],folder,env,folder/'build.log',90)
    need(built.returncode==0,'Receipt consumer compile failed: '+str(folder/'build.log'))
    proc=ret.helper.execute([exe]+(['negative'] if route!='current' else []),folder,env,folder/'run.log',30)
    value=json.loads(proc.stdout.strip().splitlines()[-1])
    need(all(c['starterJoined'] and c['workerJoined'] and c['ownedHandlesRemaining']==0 and not c['contextRetained'] and not c['processExitUsedAsCleanup'] for c in value['cases']),'Receipt consumer lost owned resources')
    if route=='current':need(proc.returncode==0 and value['pass'] and len(value['cases'])==4,'Actual Admission/Start/Stop assertions failed: '+str(folder/'run.log'))
    else:need(proc.returncode==1 and not value['pass'] and 'CHECK_FAILED Stop cannot settle while actual Start owns its reservation' in proc.stderr,'Removing actual Start admission must reproduce false settled receipt')
    receipt['profiles'].append({'profile':profile,'route':route,'checks':value['checks'],'failures':value['failures'],'cases':value['cases'],'lease':value['lease'],
     'expectedFailureCaught':route!='current','sourceSha256':sha(cpp.read_bytes()),'executableSha256':sha(exe.read_bytes())});publish()
    print(profile+' '+route+': '+str(value['checks'])+' checks; failures='+str(value['failures']),flush=True)
  need(all((ROOT/n).read_bytes()==b for n,b in source.items()),'Receipt source changed during test')
  receipt.update(pass_=True,status='PASS',sourceStable=True,evidenceComplete=True);receipt['pass']=receipt.pop('pass_');publish();return 0
 except Exception as error:receipt.update(status='FAIL',error=str(error));publish();print(str(error),file=sys.stderr);return 1
if __name__=='__main__':raise SystemExit(main())
