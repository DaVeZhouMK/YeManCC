#!/usr/bin/env python3
"""Execute actual primary-thread ownership and full native Clear/quarantine consumers.

No product process or device is run. A private live Win32 thread supplies the
creation-thread HANDLE; process/endpoint/visibility facts are explicit ports.
"""
from pathlib import Path
import argparse,datetime,hashlib,importlib.util,json,os,re,sys,traceback
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT.parents[2]/'Testing/YMCC/Evidence/VC-PrimaryThread23'
SELF='tools/input_host_primary_thread_selftest.py'
FIX='tools/fixtures/input_host_primary_thread/suite.cpp.in'
START='native/virtual_controller_start_runtime.h'
BEFORE=BASE/'before'/START
BEFORE_MANIFEST=BASE/'BEFORE.json'
HELPER='inputHostPrimaryThreadRetirementProvenLocked'
sha=lambda b:hashlib.sha256(b).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def need(v,s):
 if not v:raise RuntimeError(s)
def ident(p):return {'path':str(p),'sha256':sha(p.read_bytes()),'bytes':p.stat().st_size}
def adoption(text,current):
 anchor='    g_inputHostPrimaryThread = pi.hThread;' if current else '    CloseHandle(pi.hThread);'
 start=text.index(anchor,text.index('static bool inputHostStart('));end=text.index('    json helloReceipt;',start)
 return text[start:end]
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path)
 ap.add_argument('--toolchain',type=Path)
 ap.add_argument('--runtime_sourceClosure',type=Path)
 ap.add_argument('--source-identity-manifest',type=Path)
 ap.add_argument('--source-identity-sha256')
 a=ap.parse_args()
 out=a.out or BASE/('r'+datetime.datetime.now().strftime('%H%M%S-%f')+'-'+str(os.getpid()))
 need(out.is_absolute() and not out.exists() and not out.resolve().is_relative_to(ROOT.resolve()),'fresh absolute output outside source required');out.mkdir(parents=True)
 result={'status':'PREPARING','pass':False,'wholeGoalComplete':False,'hardwareAccepted':False,'runs':[],
 'scope':'Actual ownership publication fragment, checked primary Close, full Clear/BUS/lifecycle quarantine/Owned predicates, actual pipe Runtime; private real Win32 thread and join witness; explicit process/SDK/physical facts',
 'completeStartOrSdkOrProductExecuted':False,'processExitUsedAsCleanup':False,'arbitraryOsBlockingBound':'UNKNOWN','out':str(out)}
 try:
  spec=importlib.util.spec_from_file_location('actual_clear_primary',ROOT/'tools/input_host_clear_retirement_selftest.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  names=list(dict.fromkeys([*m.SOURCES,START,FIX,SELF]));inputs={n:(ROOT/n).read_bytes() for n in names};src=out/'source'
  for n,b in inputs.items():p=src/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  bindingArgs=[a.runtime_sourceClosure,a.source_identity_manifest,a.source_identity_sha256]
  need(all(bindingArgs) or not any(bindingArgs),'identity and runtime closure arguments must be supplied together')
  if all(bindingArgs):
   need(a.runtime_sourceClosure.is_absolute() and a.source_identity_manifest.is_absolute(),'absolute identity paths required')
   need(sha(a.source_identity_manifest.read_bytes())==a.source_identity_sha256,'approved identity manifest changed')
   approval=read(a.source_identity_manifest);runtimeClosure=read(a.runtime_sourceClosure)
   need(approval.get('mainSha256')==sha(inputs['native/main.cpp']) and approval.get('sourceBindings',{}).get('native/main.cpp')==approval['mainSha256'],'Production main differs explicitly approved identity')
   need(all(runtimeClosure.get(n)==sha(b) for n,b in inputs.items()),'runtime closure must bind all thirteen actual inputs')
   need(all(runtimeClosure.get(n)==h for n,h in approval['sourceBindings'].items()),'approved source bindings differ runtime closure')
   need(all(sha((ROOT/n).read_bytes())==h for n,h in runtimeClosure.items()),'runtime closure changed before execution')
   result.update(identityMode='approved-manifest-and-runtime-source-closure',approvedIdentityManifest=ident(a.source_identity_manifest),runtimeSourceClosureIdentity=ident(a.runtime_sourceClosure),runtime_sourceClosure=runtimeClosure)
  original=BEFORE.read_bytes();pin=next(r['sha256'] for r in read(BEFORE_MANIFEST)['inputs'] if r['path']==START);need(sha(original)==pin,'original Start baseline changed')
  (out/'historical-start.h').write_bytes(original)
  rawMain=inputs['native/main.cpp'].decode('utf-8-sig');maintext=rawMain.replace('\r\n','\n');stop=inputs['native/virtual_controller_stop_runtime.h'].decode('utf-8-sig').replace('\r\n','\n');starttext=inputs[START].decode('utf-8-sig').replace('\r\n','\n')
  nativeNames=(*m.NATIVE,'nativeLifecycleEventIsBase')
  parts={n:m.extract(maintext,n) for n in nativeNames};parts.update({n:m.extract(stop,n) for n in m.STOP})
  created=adoption(starttext,True);historical=adoption(original.decode('utf-8-sig').replace('\r\n','\n'),False)
  declaration=re.findall(r'^static HANDLE g_inputHostPrimaryThread\s*=\s*nullptr\s*;',maintext,re.M);need(len(declaration)==1,'one actual primary-thread owner declaration required')
  template=inputs[m.FIX+'/suite.cpp.in'].decode('utf-8-sig');common=template[:template.index('int main(){try{')]
  need(common.count('static HANDLE g_inputHostPrimaryThread=nullptr;')==1,'explicit fixture primary owner shape');common=common.replace('static HANDLE g_inputHostPrimaryThread=nullptr;',declaration[0],1)
  logger='static void appendNativeLifecycleLog(const char* event,const json& data){ports.logs.push_back({{"event",event},{"data",data}});}'
  need(common.count(logger)==1,'named memory logger leaf');common=common.replace(logger,logger.replace('ports.logs.push_back','if(primaryLoggerThrows)throw std::runtime_error("injected-log-failure");ports.logs.push_back'),1)
  new=inputs[FIX].decode('utf-8-sig');need(new.count('static bool primaryLoggerThrows=false;')==1,'logger leaf declaration');new=new.replace('static bool primaryLoggerThrows=false;\n','',1);new='static bool primaryLoggerThrows=false;\n'+new
  leaf=inputs[m.MEMORY].decode('utf-8-sig');prefix=leaf[:leaf.index('using json=nlohmann::json;')];raw=re.findall(r'^static HANDLE g_inputHostUnadoptedPipe\s*=\s*nullptr\s*;',maintext,re.M);need(len(raw)==1,'actual raw-slot owner declaration')
  save(out/'SOURCE-FREEZE.json',{'sourceHashes':{n:sha(b) for n,b in inputs.items()},'functions':parts,'rawActualMainFunctionHashes':{n:m.extract(rawMain,n)['sha256'] for n in nativeNames},'normalizationPolicy':'CRLF-to-LF-only;raw-file-and-raw-function-pins-retained','ownerDeclaration':declaration[0],'actualAdoption':created,'historicalStart':ident(BEFORE),'historicalManifest':ident(BEFORE_MANIFEST),'historicalAdoption':historical,
   'rawFunctionHashes':{**{n:m.extract(rawMain,n)['sha256'] for n in nativeNames},**{n:m.extract(inputs['native/virtual_controller_stop_runtime.h'].decode('utf-8-sig'),n)['sha256'] for n in m.STOP},'inputHostStart':m.extract(inputs[START].decode('utf-8-sig'),'inputHostStart')['sha256']},
   'historicalRawStartFunctionSha256':m.extract(original.decode('utf-8-sig'),'inputHostStart')['sha256']})
  h=parts[HELPER]['text'];clear=parts['inputHostClearLocked']['text'];life=parts['inputHostReconcileQuarantineOnLifecycleThread']['text'];owned=parts[m.STOP[0]]['text'];bus=parts['inputHostReconcileQuarantine']['text']
  conditional='if (receipt.closed) g_inputHostPrimaryThread = nullptr;';clearGate='|| !primaryThreadRetirementProven';lifeGate='    if (!inputHostPrimaryThreadRetirementProvenLocked("quarantine")) return;'
  busGate='''    if (g_inputHostPrimaryThread) {
        inputHostQueueLifecycleWork("quarantine-endpoint-absent");
        return;
    }'''
  need(h.count(conditional)==1 and clear.count(clearGate)==2 and life.count(lifeGate)==1 and owned.count('!g_inputHostPrimaryThread &&')==1 and bus.count(busGate)==1,'primary production gate shapes')
  variants=[('current',created,h,clear,life,owned,bus,None),
   ('historical-start-loses-owner',historical,h,clear,life,owned,bus,'failed creation-thread Close retains sole owner'),
   ('drop-owner-on-failed-Close',created,h.replace(conditional,'g_inputHostPrimaryThread = nullptr;'),clear,life,owned,bus,'failed creation-thread Close retains sole owner'),
   ('omit-owned-primary-duty',created,h,clear,life,owned.replace('!g_inputHostPrimaryThread &&','true &&'),bus,'Owned independently rejects a retained primary handle'),
   ('omit-Clear-primary-duty',created,h,clear.replace(clearGate,'|| false'),life,owned,bus,'Clear preserves old tuple while primary handle remains'),
   ('omit-lifecycle-primary-duty',created,h,clear,life.replace(lifeGate,'    // specified negative: bypass primary close'),owned,bus,'proven endpoint absence does not overtake primary handle retirement'),
   ('omit-BUS-primary-continuation',created,h,clear,life,owned,bus.replace(busGate,'    // specified negative: skip local handle debt queue'),'source-free primary debt queues existing lifecycle cleanup')]
  tc=a.toolchain or m.CAPTURE;cap=read(tc/'build-environment.json');man=read(tc/'toolchain-manifest.json');env=dict(cap['environment'])
  for k in list(env):
   if k.upper() in ('CL','_CL_','LINK','_LINK_'):del env[k]
  env['INCLUDE']=';'.join(r['frozen'] for r in man['roots'] if r['category']=='INCLUDE');env['LIB']=';'.join(r['frozen'] for r in man['roots'] if r['category']=='LIB');env['LIBPATH']=env['LIB'];env['VSLANG']='1033'
  cl=Path(cap['tools']['cl.exe']['paths'][0]);dump=Path(cap['tools']['dumpbin.exe']['paths'][0]);tools=[]
  for name in ('cl.exe','link.exe','dumpbin.exe'):
   p=Path(cap['tools'][name]['paths'][0]);row=next(r for r in man['tools'] if r['name']==name);need(sha(p.read_bytes())==row['sha256'],'captured tool identity');tools.append(ident(p))
  result['toolchain']={'manifest':ident(tc/'toolchain-manifest.json'),'capture':ident(tc/'build-environment.json'),'tools':tools,'python':ident(Path(sys.executable))}
  bytool={str(Path(r['frozen']).resolve()).lower():r for r in man['files']};bysource={str((src/n).resolve()).lower():sha(b) for n,b in inputs.items()};closure={}
  for profile,flags in [('Release',['/O2','/DNDEBUG']),('Checked',['/Od','/RTC1'])]:
   for name,adoptCode,helper,c,l,o,b,expected in variants:
    target=out/(profile+'-'+name);target.mkdir();generated=common
    replacements={'@@PIPE_LEAVES@@':prefix,'@@PIPE_CLOSE@@':parts['inputHostPipeCloseOwned']['text'],'@@PIPE_WRAPPER@@':raw[0]+'\n'+parts['inputHostPipeRetirementProvenLocked']['text'],
     '@@PRIMARY_HELPER@@':helper+'\n'+parts['nativeLifecycleEventIsBase']['text'],'@@CLEAR@@':c,'@@OWNED@@':o,'@@DISABLED@@':parts[m.STOP[1]]['text'],'@@PIPE_ONLY_HELPER@@':parts['inputHostPipeOnlyReturnAdmittedLocked']['text'],
     '@@BUS_QUARANTINE@@':b,'@@LIFECYCLE_QUARANTINE@@':l}
    for marker,value in replacements.items():need(generated.count(marker)==1,'common marker '+marker);generated=generated.replace(marker,value)
    fixtureText=new.replace('@@COMMON@@',generated).replace('@@ADOPT_FUNCTION@@','static void publishCreatedHandles(PROCESS_INFORMATION& pi){\n'+adoptCode+'\n}')
    need('@@' not in fixtureText,'unexpanded fixture marker');fixture=target/'fixture.cpp';fixture.write_text(fixtureText,encoding='utf-8',newline='\n');exe=target/'fixture.exe';dep=target/'dependencies.json'
    argv=[cl,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/MT',*flags,'/I'+str(src/'native'),'/I'+str(src/'deps/json'),'/I'+str(src/'tools'),'/sourceDependencies',dep,fixture,'/Fo'+str(target/'fixture.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB']
    save(target/'command.json',[str(x) for x in argv]);built=m.execute(argv,target,env,target/'build.log',60);need(built.returncode==0,'compile failed '+str(target/'build.log'))
    data=read(dep)['Data']
    for file in [data['Source'],*data['Includes']]:
     p=Path(file).resolve();key=str(p).lower();actual=sha(p.read_bytes())
     if p==fixture.resolve():need(actual==sha(fixtureText.encode()),'generated consumer changed')
     elif key in bysource:need(actual==bysource[key],'snapshot include changed')
     else:need(key in bytool and bytool[key]['sha256']==actual,'undeclared compiler dependency '+str(p));closure[str(p)]=actual
    ran=m.execute([exe],target,env,target/'run.log',30);runtime=json.loads(ran.stdout);save(target/'RESULT.json',runtime)
    passed=ran.returncode==2 and runtime['pass'] is False and expected in runtime['failures'] if expected else ran.returncode==0 and runtime['pass'] is True
    need(runtime['privateThreadsJoinedBeforeExit']==5 and not runtime['primaryOwnerRemaining'] and runtime['pipeOracleLiveBeforeExit']==0 and runtime['handlesBefore']==runtime['handlesAfter'],'private owner not settled before process exit')
    if not expected:need(runtime['negativeOnlyPrimarySalvage']==0,'current requires oracle salvage')
    imp=m.execute([dump,'/imports',exe],target,env,target/'imports.txt',15);need(imp.returncode==0 and not any(x in imp.stdout.lower() for x in ('hid.dll','setupapi.dll','dinput8.dll','xinput','steam_api','winmm.dll','user32.dll')),'unexpected real input/device imports')
    result['runs'].append({'profile':profile,'variant':name,'expectedFailure':expected,'accepted':passed,'exitCode':ran.returncode,'runtime':runtime,'executable':ident(exe),'fixture':ident(fixture)})
    need(passed,'primary consumer assertion failure '+str(target/'run.log'))
  final=(ROOT/'native/main.cpp').read_bytes().decode('utf-8-sig').replace('\r\n','\n')
  currentParts={n:m.extract(final,n) for n in nativeNames};mainConsumersStable=all(currentParts[n]['sha256']==parts[n]['sha256'] for n in nativeNames)
  otherDrift=[n for n,b in inputs.items() if n!='native/main.cpp' and (ROOT/n).read_bytes()!=b]
  result.update(consumerInputsStable=mainConsumersStable and not otherDrift,wholeMainUnchanged=sha((ROOT/'native/main.cpp').read_bytes())==sha(inputs['native/main.cpp']),sourceChanges=otherDrift,
   mainSha256AtStart=sha(inputs['native/main.cpp']),mainSha256AtEnd=sha((ROOT/'native/main.cpp').read_bytes()),actualFunctionHashes={n:p['sha256'] for n,p in parts.items()},toolDependencyClosure=closure)
  if all(bindingArgs):
   result['runtimeClosureStable']=all(sha((ROOT/n).read_bytes())==h for n,h in runtimeClosure.items()) and sha(a.runtime_sourceClosure.read_bytes())==result['runtimeSourceClosureIdentity']['sha256'] and sha(a.source_identity_manifest.read_bytes())==a.source_identity_sha256
   need(result['runtimeClosureStable'],'runtime closure or approved identity changed during execution')
  result['pass']=result['consumerInputsStable'] and all(r['accepted'] for r in result['runs']);result['status']='PASS_CURRENT_CONSUMERS_WITH_PRIVATE_KERNEL_OWNERS' if result['pass'] else 'FAIL_SOURCE_DRIFT'
 except Exception as e:result['error']=str(e);result['traceback']=traceback.format_exc();result['status']='FAIL'
 result['finishedAt']=datetime.datetime.now().astimezone().isoformat();save(out/'RESULT.json',result)
 print(json.dumps({k:result.get(k) for k in ['pass','status','error','consumerInputsStable','wholeMainUnchanged']},ensure_ascii=False));print(str(out));return 0 if result['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
