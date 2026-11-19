"""Owned small native producer. No shell, product launch or old-source input."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,time

plan_path,root=Path(sys.argv[1]),Path(sys.argv[2])
plan=json.loads(plan_path.read_text(encoding='utf-8'))
end=plan['deadline']/1000-3
started=time.time();records=[];reports=[];headers={};libraries={}
execution=root/'Execution'

def pin(p):
    p=Path(p);b=p.read_bytes()
    return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())

def save(p,v):
    with Path(p).open('x',encoding='utf-8',newline='') as out:
        json.dump(v,out,indent=2)

def key(p):return os.path.normcase(os.path.realpath(p))

declared={key(x['path']):x for x in plan['sources']+plan['toolArtifacts']}

def invoke(args,label,expected):
    begin=time.time();left=end-begin
    assert left>.1,'Shared execution budget exhausted'
    out=execution/(label+'.stdout.log');err=execution/(label+'.stderr.log')
    timeout=max(.01,left-.05);timed_out=False;code=None;spawn_error=None;pid=None
    try:
        with out.open('xb') as stdout,err.open('xb') as stderr:
            child=subprocess.Popen(args,cwd=execution,env=plan['environment'],stdout=stdout,stderr=stderr,creationflags=0x08000000)
            pid=child.pid
            try:code=child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                # The owning raw guard cancels/collects this whole subtree on the
                # same deadline. Do not kill a compiler root and orphan children.
                timed_out=True
    except Exception as exc:spawn_error=repr(exc)
    rec=dict(pid=pid,args=args,exitCode=code,expectedExitCode=expected,timeoutMs=timeout*1000,timedOut=timed_out,
             signal=None,spawnError=spawn_error,elapsedMs=(time.time()-begin)*1000,stdout=pin(out),stderr=pin(err))
    record=execution/(label+'.PROCESS.json');save(record,rec);records.append(pin(record))
    assert not timed_out and not spawn_error and code==expected,'Unexpected process terminal: '+label
    return out

try:
    for label,flags,expected in [('Before',['/MT','/O2','/DNDEBUG','/DBASELINE_RUN'],101),
                                 ('Release',['/MT','/O2','/DNDEBUG'],0),
                                 ('Checked',['/MTd','/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2'],0)]:
        exe=execution/(label+'.exe');deps=execution/(label+'.deps.json')
        args=[plan['tools']['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc',*flags,
              '/I'+str(root),'/I'+plan['jsonInclude'],str(root/'Fixture.cpp'),'/Fo'+str(execution/(label+'.obj')),
              '/Fe'+str(exe),'/sourceDependencies',str(deps),'/link','/INCREMENTAL:NO','/VERBOSE:LIB','/ENTRY:fixtureEntry','kernel32.lib']
        log=invoke(args,label+'-build',0)
        graph=json.loads(deps.read_text(encoding='utf-8-sig'))['Data']
        assert key(graph['Source'])==key(root/'Fixture.cpp')
        actual_libs=[]
        for match in re.finditer(r'Searching ([A-Za-z]:\\[^\r\n]*?\.lib):',log.read_text(encoding='utf-8',errors='replace'),re.I):
            file=Path(match.group(1).strip())
            if file.is_file():actual_libs.append(str(file.resolve()))
        assert graph['Includes'] and actual_libs,'Actual compile/link inputs missing'
        for file in graph['Includes']+actual_libs:
            previous=declared.get(key(file));assert previous,'Undeclared actual input: '+file
            now=pin(file)
            assert now['sha256']==previous['sha256'] and now['bytes']==previous['bytes'],'Actual input drift: '+file
            (libraries if file in actual_libs else headers)[key(file)]=now
        raw=invoke([str(exe)],label+'-verify',expected)
        rows=[json.loads(line) for line in raw.read_text(encoding='utf-8').splitlines() if line.strip()]
        if expected:
            assert rows==[dict(phase='before-crt',status='av-observed',exception='0xc0000005',faultAddress='0x10',
                              onlyThisFixtureTerminated=True,workerBodyEntered=False,crtStarted=False,threadIsBootstrap=False)],'Exact old pre-CRT thread failure missing'
        else:
            assert rows==[dict(phase='before-crt',status='worker-entered',crtStarted=False),
                          dict(phase='main',status='pass',checks=22,failures=0,realPortAndCallerFragments=True,wholeOneXStart=False,hardwareIo=False)],'Exact new phase/assertion set missing'
        reports.append(dict(name=label,exitCode=expected,rows=rows,executable=pin(exe),dependencies=pin(deps),raw=pin(raw)))
        print(label+': EXPECTED_TERMINAL',flush=True)
    for previous in plan['sources']:
        now=pin(previous['path'])
        assert now['sha256']==previous['sha256'] and now['bytes']==previous['bytes'],'Closing source drift'
    result=dict(status='PASS_SELECTED_SCOPE',passed=True)
except Exception as error:
    result=dict(status='FAIL',passed=False,error=repr(error))
result.update(dict(elapsedMs=(time.time()-started)*1000,records=records,reports=reports,headers=list(headers.values()),libraries=list(libraries.values()),
                   canonicalTestId=plan['canonicalTestId'],selectedCase=plan['selectedCase'],fullOneXStart=False,hardwareIo=False))
save(root/'worker-result.json',result)
print(json.dumps({k:result.get(k) for k in ['status','passed','elapsedMs','error']}),flush=True)
sys.exit(0 if result['passed'] else 1)
