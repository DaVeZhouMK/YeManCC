"""Current-source mode of the original sample-tree consumer; no new test ID."""
from pathlib import Path
import datetime, hashlib, json, os, re, subprocess

def run_current(repo, output, before_path, helpers):
    repo=Path(repo).resolve(); output=Path(output).resolve(); before_path=Path(before_path).resolve()
    evidence=repo.parents[2]/'Testing/YMCC/Evidence'
    if not output.is_relative_to(evidence.resolve()): raise ValueError('Output must stay in development Evidence')
    if output.exists(): raise FileExistsError(output)
    output.mkdir(parents=True)
    sha=lambda b:hashlib.sha256(b).hexdigest()
    save=lambda name,value:(output/name).write_bytes((json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode())
    write=lambda name,value:(output/name).open('xb').write(value.encode() if isinstance(value,str) else value)
    source=repo/'native/main.cpp'; old_raw=before_path.read_bytes(); new_raw=source.read_bytes()
    old=old_raw.decode('utf-8-sig'); new=new_raw.decode('utf-8-sig')
    function=helpers['source_function']; brace=helpers['match_brace']
    old_fn=function(old,'static void inputCaptureOnGamepad('); new_fn=function(new,'static void inputCaptureOnGamepad(')
    start='        json imuObj = json::object();'; finish='        imuObj["orientation"] = std::move(imuObj_orientation);'
    if old_fn.count(start)!=1 or old_fn.count(finish)!=1: raise ValueError('Unique original IMU builder required')
    begin=old_fn.index(start); end=old_fn.index(finish,begin)+len(finish); old_build=old_fn[begin:end]
    gate='        if (gyroSequence != 0 || accelSequence != 0) {'
    new_start=new_fn.index('        json imuObj;'); gate_start=new_fn.index(gate,new_start)
    new_end=brace(new_fn,new_fn.index('{',gate_start))+1; new_build=new_fn[new_start:new_end]
    actual_body=new_fn[new_fn.index('{',gate_start)+1:new_end-1].strip('\r\n')
    actual_body='\n'.join(line for line in actual_body.splitlines() if line.strip())
    restored='\n'.join(line[4:] if line.startswith('    ') else line for line in actual_body.splitlines())
    restored=restored.replace('        imuObj = json::object();',start,1)
    if restored!=old_build.replace('\r\n','\n'): raise ValueError('Active branch must retain every original expression in order')
    if new_fn.replace(new_build,old_build,1)!=old_fn: raise ValueError('Unrelated callback changes rejected')
    if new.replace(new_fn,old_fn,1)!=old: raise ValueError('Unrelated main changes rejected')
    inactive_start=old_fn.index('        if (!(gyroSequence != 0 || accelSequence != 0)) {')
    inactive_end=brace(old_fn,old_fn.index('{',inactive_start))+1
    override=old_fn[inactive_start:inactive_end]
    transfer='        rec["imu"] = std::move(imuObj);'
    if old_fn.count(transfer)!=1 or new_fn.count(override)!=1: raise ValueError('Original final transfer/override required')
    protected=['static void inputCaptureAppendRecord(', 'static void inputCaptureAppend(const json& rec)',
      'static void inputCaptureAppendOwned(', 'static void inputCaptureQueueSample(', 'static void inputCaptureDrainQueuedSamples(',
      'static void CALLBACK inputCaptureTimerEventProc(', 'static void inputCaptureBusTick(', 'static DWORD WINAPI inputCaptureThreadProc(']
    protected_rows=[]
    for name in protected:
        a=function(old,name); b=function(new,name)
        if a!=b: raise ValueError('Protected consumer changed: '+name)
        protected_rows.append({'signature':name,'sha256':sha(a.encode())})
    original_cpp=(repo/'tools/virtual_sample_tree_perf_selftest.cpp').read_text(encoding='utf-8-sig')
    prefix=original_cpp[:original_cpp.index('struct RecordInput {')]
    shared=prefix+'\n'+function(original_cpp,'static bool exact(')+'\n'+function(original_cpp,'static void exactAndDump(')
    oracle=function(original_cpp,'static Trees oracle(')
    names=sorted(set(re.findall(r'\bv\.(\w+)',oracle)))
    groups=['double gx =','bool gyro =','bool accel =','UINT64 gyroTimestamp100ns =','double gyroTimeOfDayMs =',
      'ULONGLONG gyroReceiptTick =','UINT64 gyroSequence =','UINT64 gyroBindingGeneration =','double mgx =','double cgx =',
      'float gravityX =','float linearAccelX =','float orientationW =','const bool sameBindingGeneration =',
      'const UINT64 activeBindingGeneration=','const bool currentBindingGeneration=']
    declarations=[]
    for group in groups:
        pos=old_fn.index(group); declarations.append(old_fn[pos:old_fn.index(';',pos)+1])
    types=helpers['scalar_types'](declarations,names)
    fields='\n'.join('    '+t.removeprefix('const ')+' '+n+'{};' for n,t in types.items())
    header='struct Scalars {\n'+fields+'\n};\nstruct Trees {json imu;json event;};\n'+oracle+'\n'
    for label,body in [('before',old_build),('after',new_build)]:
        header+='static NOINLINE json '+label+'Record(const Scalars& values,const json& opaque) {\n'+helpers['aliases'](types)+'\n'
        if label=='after': header+='#ifdef YMCC_IMU_GATE_NEGATIVE\n'+body.replace('gyroSequence != 0 || accelSequence != 0','gyroSequence != 0 && accelSequence != 0',1)+'\n#else\n'+body+'\n#endif\n'
        else: header+=body+'\n'
        header+='    json rec=opaque;\n'+transfer+'\n'+override+'\n    return rec;\n}\n'
    for typename,label in [('double','Double'),('float','Float'),('UINT64','Unsigned')]:
        matching=[n for n,t in types.items() if t.removeprefix('const ')==typename or typename=='UINT64' and t.removeprefix('const ')=='ULONGLONG']
        header+='static void fill'+label+'(Scalars& v,'+typename+' x){'+''.join('v.'+n+'=x;' for n in matching)+'}\n'
    template=repo/'tools/fixtures/virtual_sample_tree/idle_current.cpp.in'
    write('shared-original.hpp',shared);write('current-extracted.hpp',header);write('fixture.cpp',template.read_bytes());write('main.before.cpp',old_raw);write('main.candidate.cpp',new_raw)
    plan_path=output.parent/'PLAN.json'; plan=json.loads(plan_path.read_text(encoding='utf-8-sig'))
    expected={'campaigns':2,'pairsPerCampaign':11,'iterationsPerArm':1024,'inactiveMinimumMedianCycleReductionPercent':5,
      'inactiveFasterPairsAtLeast':9,'inactiveAllocationsMustDecrease':True,'inactiveRequestedBytesMustDecrease':True,
      'activeMaximumMedianCycleRegressionPercent':5,'activeAllocationRequestsAndBytesMayNotIncrease':True,'retryOnMeasuredFailure':False}
    if plan['acceptance']['cpu']!=expected: raise ValueError('Predetermined plan differs')
    write('acceptance-plan.json',json.dumps(expected))
    files=[source,repo/'deps/json/json.hpp',repo/'tools/virtual_sample_tree_perf_selftest.cpp',repo/'tools/virtual_sample_tree_perf_selftest.py',Path(__file__),template]
    inputs=[{'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())} for p in files]
    save('SOURCE-IDENTITY.json',{'atUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'before':{'path':str(before_path),'sha256':sha(old_raw)},'currentSha256':sha(new_raw),'activeBodyExact':True,'onlyImuBuildChanged':True,'protected':protected_rows,'declarations':declarations,'types':types,'inputs':inputs,'modelBoundaries':['Opaque unchanged record fields are fixture data; other production branches are verified byte-identical, not re-executed.','Native JSON builders and existing final transfer/override are extracted from the actual full callback.','Source-only sample-log scope; no physical/Host/device calls or whole-process CPU acceptance.']})
    env=os.environ.copy(); env['VSLANG']='1033'
    for key in list(env):
        if key.upper() in {'PATH','INCLUDE','LIB','LIBPATH'} or key.upper().startswith(('VSCMD','__VSCMD','VCTOOLS','VCINSTALL','VSINSTALL','WINDOWSSDK','UNIVERSALCRT','UCRTVERSION')):
            env.pop(key,None)
    env['PATH']='C:\\Windows\\System32;C:\\Windows;C:\\Windows\\System32\\Wbem;C:\\Windows\\System32\\WindowsPowerShell\\v1.0'
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    sdk=Path('C:/Program Files (x86)/Windows Kits/10');version='10.0.26100.0'
    setup='@echo off\r\ncall "'+str(vc)+'" >nul\r\nif errorlevel 1 exit /b 1\r\nset "PATH=C:\\Windows\\System32;%VCToolsInstallDir%bin\\Hostx64\\x64;%PATH%"\r\n'
    setup+='set "INCLUDE='+ ';'.join(str(sdk/'Include'/version/f) for f in ['ucrt','shared','um','winrt','cppwinrt'])+';%INCLUDE%"\r\n'
    setup+='set "LIB='+ ';'.join(str(sdk/'Lib'/version/f/'x64') for f in ['ucrt','um'])+';%LIB%"\r\n'
    write('environment.cmd',setup+'set PATH\r\nset INCLUDE\r\nset LIB\r\nset VCToolsInstallDir\r\nset WindowsSdkDir\r\nset WindowsSDKVersion\r\nexit /b 0\r\n')
    def invoke(exe,args,name,timeout=90,allowed=(0,)):
        began=datetime.datetime.now(datetime.timezone.utc).isoformat()
        p=subprocess.run([str(Path(exe)),*map(str,args)],cwd=output,env=env,capture_output=True,timeout=timeout)
        write(name+'.stdout.log',p.stdout);write(name+'.stderr.log',p.stderr)
        save(name+'.PROCESS.json',{'startedAtUtc':began,'endedAtUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'exitCode':p.returncode,'exe':str(exe),'args':list(map(str,args))})
        if p.returncode not in allowed: raise RuntimeError(name+' rejected, preserved logs: '+str(output))
        return p
    prepared=invoke('C:/Windows/System32/cmd.exe',['/d','/c',output/'environment.cmd'],'environment',30)
    for line in prepared.stdout.decode('utf-8',errors='replace').splitlines():
        if '=' not in line:continue
        key,value=line.split('=',1)
        if key and not key.startswith('='):
            existing=next((k for k in env if k.lower()==key.lower()),key);env[existing]=value
    reports=[]
    tools_root=next(value for key,value in env.items() if key.lower()=='vctoolsinstalldir')
    compiler=Path(tools_root)/'bin/Hostx64/x64/cl.exe'
    linker=compiler.with_name('link.exe')
    if not compiler.is_file() or not linker.is_file():raise FileNotFoundError('Selected MSVC x64 compiler/linker')
    save('TOOLCHAIN-IDENTITY.json',{'compiler':{'path':str(compiler),'sha256':sha(compiler.read_bytes())},'linker':{'path':str(linker),'sha256':sha(linker.read_bytes())},'windowsSdkVersion':version,'sdkIncludes':[str(sdk/'Include'/version/f) for f in ['ucrt','shared','um','winrt','cppwinrt']],'childPathLength':len(next(v for k,v in env.items() if k.lower()=='path')),'environmentScope':'Owned compiler children only; finite bootstrap PATH, selected vcvars plus exactSDK10.0.26100.0 include/lib; no global changes'})
    for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']),('SemanticNegative',['/O2','/DNDEBUG','/MT','/DYMCC_IMU_GATE_NEGATIVE'])]:
        executable=output/(name+'.exe');args=['/nologo','/std:c++20','/EHsc','/utf-8','/W4',*flags,'/I'+str(output),'/I'+str(repo/'deps/json'),str(output/'fixture.cpp'),'/Fo'+str(output/(name+'.obj')),'/Fe'+str(executable),'/link','/INCREMENTAL:NO']
        invoke(compiler,args,name+'-build',90)
        p=invoke(executable,['verify'],name+'-verify',60,allowed=(1,) if name=='SemanticNegative' else (0,))
        report=json.loads(p.stdout.decode('utf-8'))
        if name=='SemanticNegative':
            if report.get('failure')!='final IMU record types/values/bits':raise RuntimeError('Wrong semantic-negative failure')
        elif not report.get('pass'):raise RuntimeError('Correctness not accepted')
        reports.append({'configuration':name,'exeSha256':sha(executable.read_bytes()),'exitCode':p.returncode,'report':report})
        print(name+': '+('EXPECTED SEMANTIC FAILURE' if name=='SemanticNegative' else 'PASS'),flush=True)
    if any(sha(Path(p['path']).read_bytes())!=p['sha256'] for p in inputs):raise RuntimeError('Source drift before measurement')
    p=invoke(output/'Release.exe',['benchmark'], 'Release-benchmark',180,allowed=(0,2))
    timing=json.loads(p.stdout.decode('utf-8'))
    if any(sha(Path(p['path']).read_bytes())!=p['sha256'] for p in inputs):raise RuntimeError('Source drift during measurement')
    save('RESULT.json',{'atUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'ADOPTABLE_SCOPE_ONLY' if timing['accepted'] else 'REJECTED_CPU_CRITERIA','correctness':reports,'timing':timing,'inputs':inputs,'sourceUnchangedDuringRun':True,'wholeProcessCpuAcceptance':False,'devicesOrApplicationsControlled':False})
    print(json.dumps({'status':'ADOPTABLE_SCOPE_ONLY' if timing['accepted'] else 'REJECTED_CPU_CRITERIA','output':str(output),'timingAccepted':timing['accepted']}),flush=True)
    if not timing['accepted']:raise SystemExit(2)
