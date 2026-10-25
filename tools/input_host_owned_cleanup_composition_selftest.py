#!/usr/bin/env python3
"""Compose actual native cleanup predicates with the actual process pipe owner.

Private kernel HANDLE leaves plus controlled IO; never launches product/Host.
The main wrapper must be present for integrated acceptance. If absent, the
explicit provisional adapter yields only a helpers-level receipt, not PASS.
"""
from pathlib import Path
import argparse, datetime, hashlib, json, os, re, subprocess, sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
CORE=ROOT.parents[2]/'Testing/YMCC/Evidence/VC-Lifecycle-Integration-20261023'
CAPTURE=ROOT.parents[2]/'Build/Validation/VirtualController-Lifecycle-20261021-G3E-XboxSwitch/native-verified'
FIX='tools/fixtures/input_host_owned_cleanup_composition'
RELATIVE=[
 'native/main.cpp','native/virtual_controller_stop_runtime.h','native/input_host_pipe_transport.h','native/input_host_process_retirement.h',
 'deps/json/json.hpp','tools/input_pipe_retirement_memory_selftest.cpp',
 'tools/input_pipe_test_allocation_fault.h','tools/input_host_owned_cleanup_composition_selftest.py',
 FIX+'/suite.cpp.in',FIX+'/README.md']
PROVISIONAL='''static bool inputHostPipeRetirementProvenLocked() {
    auto* owner = ymcc::inputHost::pipeTransport::existingProcessRuntime();
    if (!owner) return true;
    const auto snapshot = owner->snapshot();
    return snapshot.available && snapshot.stopped;
}'''
def sha(b):return hashlib.sha256(b).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def need(b,s):
 if not b:raise RuntimeError(s)
def mask(text):
 return re.sub(r'//[^\r\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'',lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text)
def extract(text,name,required=True):
 m=mask(text);hits=list(re.finditer(r'^static\s+bool\s+'+name+r'\s*\([^;]*?\)\s*(?:noexcept\s*)?\{',m,re.M))
 if not hits and not required:return None
 need(len(hits)==1,'Expected unique actual function '+name)
 start=hits[0].start();opening=m.index('{',hits[0].start());depth=0
 for end in range(opening,len(m)):
  depth+=(m[end]=='{')-(m[end]=='}')
  if depth==0:
   text=text[start:end+1];return {'text':text,'sha256':sha(text.encode()),'line':m.count('\n',0,start)+1}
 raise RuntimeError('Unclosed production function '+name)
def execute(args,folder,env,log,timeout):
 p=subprocess.run([str(x) for x in args],cwd=folder,env=env,capture_output=True,encoding='utf-8',errors='replace',timeout=timeout)
 log.write_text(p.stdout+p.stderr,encoding='utf-8');return p

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--out',type=Path)
 parser.add_argument('--toolchain',type=Path,default=CAPTURE)
 parser.add_argument('--require-wrapper',action='store_true')
 args=parser.parse_args()
 out=args.out or CORE/('r'+datetime.datetime.now().strftime('%H%M%S-%f')+'-'+str(os.getpid()))
 need(out.is_absolute() and not out.exists(),'Fresh absolute private output required')
 out=out.resolve();need(not out.is_relative_to(ROOT.resolve()),'Output outside product source required')
 out.mkdir(parents=True)
 receipt={'status':'PREPARING','pass':False,'fullGoalComplete':False,'productionOrHardwareAccepted':False,
  'productsOrHostLaunched':False,'deviceServiceDriverSleepDeploymentOperations':False,'out':str(out)}
 try:
  inputs={r:(ROOT/r).read_bytes() for r in RELATIVE}
  source=out/'source';source.mkdir()
  for relative,b in inputs.items():
   p=source/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  receipt['sources']=[{'relative':r,'path':str(ROOT/r),'sha256':sha(b)} for r,b in inputs.items()]
  maintext=inputs['native/main.cpp'].decode('utf-8-sig')
  stoptext=inputs['native/virtual_controller_stop_runtime.h'].decode('utf-8-sig')
  names=('inputHostOwnedCleanupProvenLocked','inputHostDisabledCleanupProvenLocked')
  parts={name:extract(stoptext,name) for name in names}
  wrapper=extract(maintext,'inputHostPipeRetirementProvenLocked',False)
  primary=extract(maintext,'inputHostPrimaryThreadRetirementProvenLocked')
  declarations=[]
  for name in ('g_inputHostProcess','g_inputHostPrimaryThread','g_inputHostServerPipe','g_inputHostRunId','g_inputHostEpoch'):
   matches=list(re.finditer(r'^static[^\n;{}]*\b'+name+r'\b[^\n;]*;',maintext,re.M))
   need(len(matches)==1,'Actual ownership global required '+name)
   declaration=matches[0].group();declarations.append(declaration)
  actualGlobals='\n'.join(declarations)
  receipt['primaryThreadScope']={'actualGlobalAndHelperCompiled':True,'defaultOwner':'nullptr; no primary ownership transfer in original cases','newPrimaryAcceptance':False,'unchangedOriginalAssertions':95}
  rawSlotMatch=re.findall(r'^static HANDLE g_inputHostUnadoptedPipe\s*=\s*nullptr\s*;',maintext,re.M)
  need(len(rawSlotMatch)==1,'Actual single pre-adoption slot declaration required')
  rawSlot=rawSlotMatch[0]
  receipt['wrapperIntegrated']=wrapper is not None
  receipt['wrapperOrigin']='actual native/main.cpp function' if wrapper else 'explicit provisional read-only fixture adapter; not a product integration proof'
  if args.require_wrapper:need(wrapper is not None,'Actual parent wrapper not yet implemented; integration remains unaccepted')
  if wrapper is None:wrapper={'text':PROVISIONAL,'sha256':sha(PROVISIONAL.encode()),'line':None}
  receipt['functions']={**{n:{k:v for k,v in b.items() if k!='text'} for n,b in parts.items()},'inputHostPipeRetirementProvenLocked':{k:v for k,v in wrapper.items() if k!='text'},'inputHostPrimaryThreadRetirementProvenLocked':{k:v for k,v in primary.items() if k!='text'}}
  save(out/'source-bindings.json',{'functions':{**parts,'inputHostPipeRetirementProvenLocked':wrapper,'inputHostPrimaryThreadRetirementProvenLocked':primary},'actualGlobals':{'text':actualGlobals,'sha256':sha(actualGlobals.encode())}})
  leaf=inputs['tools/input_pipe_retirement_memory_selftest.cpp'].decode('utf-8-sig')
  marker='using json=nlohmann::json;';need(leaf.count(marker)==1,'Existing pipe leaf prefix anchor changed')
  prefix=leaf[:leaf.index(marker)]
  need(prefix.count('#include "input_host_pipe_transport.h"')==1,'Existing leaf must compile actual pipe Runtime')
  template=inputs[FIX+'/suite.cpp.in'].decode('utf-8-sig')
  predicates='\n'.join(parts[n]['text'] for n in names)
  gate='inputHostPipeRetirementProvenLocked()'
  need(parts[names[0]]['text'].count(gate)==1,'Actual Owned predicate must consume pipe retirement once')
  terminal='return snapshot.available && snapshot.stopped;'
  need(wrapper['text'].count(terminal)==1,'Actual wrapper terminal snapshot contract changed')
  rawGuard='if (!CloseHandle(g_inputHostUnadoptedPipe)) return false;'
  need(wrapper['text'].count(rawGuard)==1,'Actual raw slot checked Close guard changed')
  variants=[('current',wrapper['text'],predicates,False),
   ('ignore-pipe-gate',wrapper['text'],predicates.replace(gate,'true',1),True),
   ('pending-only-adapter',wrapper['text'].replace(terminal,'return snapshot.available && snapshot.pending == 0;',1),predicates,True),
   ('historical-failure-is-live-debt',wrapper['text'].replace(terminal,'return snapshot.available && snapshot.stopped && snapshot.closeFailures == 0;',1),predicates,True),
   ('unchecked-raw-slot-close',wrapper['text'].replace(rawGuard,'(void)CloseHandle(g_inputHostUnadoptedPipe);',1),predicates,True)]
  tc=args.toolchain.resolve();capturePath=tc/'build-environment.json';manifestPath=tc/'toolchain-manifest.json'
  capture=read(capturePath);manifest=read(manifestPath)
  env=dict(capture['environment'])
  for key in list(env):
   if key.upper() in ('CL','_CL_','LINK','_LINK_'):del env[key]
  env['INCLUDE']=';'.join(r['frozen'] for r in manifest['roots'] if r['category']=='INCLUDE')
  env['LIB']=';'.join(r['frozen'] for r in manifest['roots'] if r['category']=='LIB');env['LIBPATH']=env['LIB'];env['VSLANG']='1033'
  tools=[]
  for name in ('cl.exe','link.exe','dumpbin.exe'):
   p=Path(capture['tools'][name]['paths'][0]);bound=next(t for t in manifest['tools'] if t['original'].lower()==str(p).lower())
   need(sha(p.read_bytes())==bound['sha256'],'Captured tool identity changed '+str(p));tools.append({'path':str(p),'sha256':bound['sha256']})
  cl=Path(tools[0]['path']);dump=Path(tools[2]['path'])
  receipt['toolchain']={'path':str(tc),'manifestSha256':sha(manifestPath.read_bytes()),'captureSha256':sha(capturePath.read_bytes()),'tools':tools,'python':sys.executable}
  byTool={str(Path(r['frozen']).resolve()).lower():r for r in manifest['files']}
  bySource={str((source/r).resolve()).lower():sha(b) for r,b in inputs.items()}
  closure={};runs=[]
  for profile,flags in [('Release',['/O2','/DNDEBUG']),('Checked',['/Od','/RTC1'])]:
   for variant,w,pr,negative in variants:
    target=out/(profile+'-'+variant);target.mkdir()
    fixture=target/'fixture.cpp'
    generated=template
    for marker,replacement in [('@@PIPE_LEAVES@@',prefix),('@@HOST_GLOBALS@@',actualGlobals),('@@PRIMARY_HELPER@@',primary['text']),('@@RAW_SLOT@@',rawSlot),('@@WRAPPER@@',w),('@@PREDICATES@@',pr)]:
     need(generated.count(marker)==1,'Fixture expansion marker '+marker);generated=generated.replace(marker,replacement)
    fixture.write_text(generated,encoding='utf-8',newline='\n')
    deps=target/'dependencies.json';exe=target/'fixture.exe'
    command=[cl,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/MT',*flags,
     '/I'+str(source/'native'),'/I'+str(source/'deps/json'),'/I'+str(source/'tools'),
     '/sourceDependencies',deps,fixture,'/Fo'+str(target/'fixture.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB']
    save(target/'command.json',[str(x) for x in command]);built=execute(command,target,env,target/'build.log',60)
    need(built.returncode==0,'Actual composed fixture compile failed: '+str(target/'build.log'))
    data=read(deps)['Data'];actual=[data['Source'],*data['Includes']]
    for file in actual:
     p=Path(file).resolve();key=str(p).lower();actualSha=sha(p.read_bytes())
     if p==fixture.resolve():need(actualSha==sha(generated.encode()),'Generated consumer changed')
     elif key in bySource:need(actualSha==bySource[key],'Source snapshot drift')
     else:
      need(key in byTool and actualSha==byTool[key]['sha256'],'Compiler input outside captured authority '+file);closure[key]={'path':str(p),'sha256':actualSha}
    # Linker may localize its prefix, or the decoder may replace it. Match
    # only complete absolute library paths; every path/hash is still checked
    # against the original complete frozen-toolchain manifest below.
    libraries=re.findall(r'^\s+[^\r\n]*?((?:[A-Za-z]:[\\/]|\\\\)[^\r\n]*?\.lib)\s*:\s*$',built.stdout+built.stderr,re.M|re.I)
    need(libraries,'Actual linker library search receipt required')
    for file in libraries:
     p=Path(file.strip()).resolve();key=str(p).lower();need(key in byTool and sha(p.read_bytes())==byTool[key]['sha256'],'Linker input outside authority '+str(p));closure[key]={'path':str(p),'sha256':byTool[key]['sha256']}
    ran=execute([exe],target,env,target/'run.log',12)
    need(ran.stdout.strip(),'No runtime result '+str(target));result=json.loads(ran.stdout);save(target/'result.json',result)
    accepted=(ran.returncode==2 and result['pass'] is False and len(result['failures'])>0) if negative else (ran.returncode==0 and result['pass'] is True)
    if not negative:need(result['negativeOnlyRawSalvage']==0,'Current positive required out-of-owner salvage')
    need(result['oracleLiveBeforeExit']==0 and result['handlesBefore']==result['handlesAfter'],'Private resources not returned before exit')
    imported=execute([dump,'/imports',exe],target,env,target/'imports.txt',15)
    forbidden=('hid.dll','setupapi.dll','dinput8.dll','xinput','steam_api','winmm.dll','user32.dll')
    need(imported.returncode==0 and not any(s in imported.stdout.lower() for s in forbidden),'Unexpected device/product imports')
    row={'profile':profile,'variant':variant,'expectedNegative':negative,'accepted':accepted,'exit':ran.returncode,
     'checks':result['checks'],'failedAssertions':result['failures'],'actualCompilerInputs':len(actual),
     'result':str(target/'result.json'),'binarySha256':sha(exe.read_bytes())}
    runs.append(row);save(out/'PROGRESS.json',runs)
    print(json.dumps({k:v for k,v in row.items() if k not in ('failedAssertions',)}),flush=True)
    need(accepted,'Composition assertion contract rejected '+profile+'/'+variant)
    requiredNegative={
     'ignore-pipe-gate':'zero pending does not retire active lease',
     'pending-only-adapter':'zero pending does not retire active lease',
     'historical-failure-is-live-debt':'historical failure count does not block successfully retired owner',
     'unchecked-raw-slot-close':'raw pre-adoption close failure retains exact sole owner'}
    if negative:need(requiredNegative[variant] in result['failures'],'Compiled control did not reject its required original assertion '+variant)
  receipt['runs']=runs;receipt['boundToolInputs']=list(closure.values())
  receipt['sourceChanged']=[r for r,b in inputs.items() if (ROOT/r).read_bytes()!=b]
  need(not receipt['sourceChanged'],'Source changed during execution; preserve result and rerun after freeze')
  for row in closure.values():need(sha(Path(row['path']).read_bytes())==row['sha256'],'Tool input drift')
  receipt['helperCompositionPassed']=True
  receipt['pass']=receipt['wrapperIntegrated']
  receipt['status']='ACTUAL_WRAPPER_AND_OWNED_PREDICATES_COMPOSED_PASS' if receipt['pass'] else 'HELPER_COMPOSITION_PASS_PARENT_WRAPPER_NOT_INTEGRATED'
  receipt['remaining']=['Primary actual helper/global compiled with null owner; no new primary Close/Wait/adoption/failure acceptance','Full Stop/Clear/Start caller integration not executed here','Host/SDK/endpoint/physical facts are memory-only ports','Arbitrary blocked native resource total destruction UNKNOWN','No hardware or package acceptance']
  save(out/'RESULT.json',receipt);print(json.dumps({'status':receipt['status'],'out':str(out),'integratedPass':receipt['pass']}));return 0 if receipt['pass'] or not args.require_wrapper else 2
 except Exception as e:
  receipt.update(status='FAILED',error=str(e));save(out/'RESULT.json',receipt);print(str(e),file=sys.stderr);return 1
if __name__=='__main__':sys.exit(main())
