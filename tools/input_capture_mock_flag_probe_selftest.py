"""Compile actual extracted mock-flag gates, fake-clock concurrency and real Win32 missing-file probes.

The only real probe path is a newly created directory below --output. No production
entry point, SDK, installation directory, registry, sensor, or input device is used.
Candidate mode accepts one saved candidate function; final mode re-extracts main.cpp.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,statistics,subprocess,tempfile

REPO=Path(__file__).resolve().parent.parent
SHA=lambda data:hashlib.sha256(data).hexdigest()

def extract(raw,signature):
    text=raw.decode('utf-8-sig')
    start=text.index(signature+' {')
    # Both audited functions have no column-zero internal closing braces.
    import re
    matched=re.search(r'\r?\n}\r?\n',text[start:])
    if not matched:raise ValueError('Missing function end: '+signature)
    end=start+matched.start()+len(matched.group(0).rstrip('\r\n'))
    return text[start:end].encode('utf-8')

def execute(command,cwd,timeout=60):
    result=subprocess.run(command,cwd=cwd,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
    if result.returncode:raise RuntimeError(f'Command failed ({result.returncode}): {command}\n{result.stdout}\n{result.stderr}')
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before',type=Path,required=True,help='Full raw baseline main.cpp saved before editing')
    parser.add_argument('--candidate-function',type=Path,help='Candidate-only: compile this saved function instead of production')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--iterations',type=int,default=100000)
    parser.add_argument('--bench-rounds',type=int,default=7)
    parser.add_argument('--skip-bench',action='store_true',help='Final correctness/source binding only after a completed candidate benchmark')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    out=args.output/('candidate-' if args.candidate_function else 'current-')
    out=Path(str(out)+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir()
    signature='static bool inputCaptureMockHandshakeFlag()'
    beforeRaw=args.before.read_bytes();currentRaw=(REPO/'native/main.cpp').read_bytes()
    before=extract(beforeRaw,signature)
    current=args.candidate_function.read_bytes() if args.candidate_function else extract(currentRaw,signature)
    helper=extract(currentRaw,'static bool inputCaptureGyroMockSwitchPresent()')
    beforeHelper=extract(beforeRaw,'static bool inputCaptureGyroMockSwitchPresent()')
    aiMarker='    // 仅当带 AI-only 开关时'.encode()
    assert aiMarker in before and aiMarker in current
    assert before[before.index(aiMarker):]==current[current.index(aiMarker):], 'AI branch must remain byte-for-byte unchanged'
    assert helper==beforeHelper,'AI-only switch helper must remain unchanged'
    assert b'compare_exchange_strong' in current and b'ignoredLogged.exchange(true' in current
    assert current.count(b'GetFileAttributesW')==2,'One file call in each branch expected'
    harness=(REPO/'tools/input_capture_mock_flag_probe_selftest.cpp').read_bytes()
    header=REPO/'deps/json/json.hpp'
    binding={'beforePath':str(args.before.resolve()),'sourcePath':str(REPO/'native/main.cpp'),
        'candidatePath':str(args.candidate_function.resolve()) if args.candidate_function else None,
        'candidateOnly':bool(args.candidate_function),'beforeFunctionSha256':SHA(before),'testedFunctionSha256':SHA(current),
        'aiBranchSha256':SHA(before[before.index(aiMarker):]),'aiBranchUnchanged':True,
        'switchHelperSha256':SHA(helper),'switchHelperUnchanged':True,
        'wholeSourceSha256AtEntry':SHA(currentRaw),'wholeSourceEqualityRequired':False,
        'harnessSha256':SHA(harness),'runnerSha256':SHA(Path(__file__).read_bytes()),'jsonHeaderSha256':SHA(header.read_bytes())}
    (out/'source-binding.json').write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8')
    (out/'before-function.cpp').write_bytes(before);(out/'current-function.cpp').write_bytes(current)
    (out/'function.diff').write_text(''.join(difflib.unified_diff(before.decode().splitlines(True),current.decode().splitlines(True),fromfile='before/inputCaptureMockHandshakeFlag',tofile='current/inputCaptureMockHandshakeFlag')),encoding='utf-8')
    extracted=b''
    for name,fn in [('before_impl',before),('current_impl',current)]:
        extracted+=('namespace '+name+' {\n').encode()+helper+b'\n'+fn+b'\n}\n'
    (out/'actual-extracted.h').write_bytes(extracted);(out/'fixture.cpp').write_bytes(harness)
    fixtureRoot=Path(tempfile.mkdtemp(prefix='win32-missing-file-',dir=out))
    flagParent=fixtureRoot/'feature-assets/gyro-motion';flagParent.mkdir(parents=True)
    flag=flagParent/'mock-handshake.flag';assert not flag.exists()
    assert fixtureRoot.resolve().is_relative_to(out.resolve())
    vcvars=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    cases=['product-missing','product-late-flag','product-present','product-transient','product-zero-and-gap',
        'ai-missing','ai-events','product-concurrent-missing','product-concurrent-present','product-overlap-expiry']
    profiles=[]
    for profile,options in [('release',['/O2','/DNDEBUG','/MT']),('checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        exe=out/(profile+'.exe')
        command=['cl','/nologo','/std:c++20','/utf-8','/EHsc',*options,str(out/'fixture.cpp'),'/I'+str(header.parent),'/I'+str(out),'/Fo'+str(out/(profile+'.obj')),'/Fe'+str(exe)]
        batch=out/(profile+'-build.bat')
        batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n',encoding='utf-8')
        built=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (out/(profile+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8')
        if built.returncode:raise RuntimeError('Compilation failed: '+str(out/(profile+'-build.log')))
        results=[]
        for case in cases:
            for variant in ['before','current']:
                ran=execute([str(exe),variant,case,str(fixtureRoot),'0'],out)
                result=json.loads(ran.stdout);results.append(result)
                (out/(profile+'-'+variant+'-'+case+'.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        profiles.append({'profile':profile,'assertions':sum(r['assertions'] for r in results),'cases':results,'pass':all(r['pass'] for r in results)})
    benchmarks=[];rate=[]
    if not args.skip_bench:
        exe=out/'release.exe'
        for roundNo in range(args.bench_rounds):
            order=['before','current'] if roundNo%2==0 else ['current','before']
            for variant in order:
                result=json.loads(execute([str(exe),variant,'real-benchmark',str(fixtureRoot),str(args.iterations)],out).stdout)
                result['round']=roundNo;benchmarks.append(result)
        for variant in ['before','current']:
            rate.append(json.loads(execute([str(exe),variant,'real-rate',str(fixtureRoot),'2400'],out).stdout))
    sourceEnd=(REPO/'native/main.cpp').read_bytes()
    assert extract(sourceEnd,signature)==extract(currentRaw,signature),'Target production function drifted during validation'
    assert extract(sourceEnd,'static bool inputCaptureGyroMockSwitchPresent()')==helper,'Switch helper drifted'
    assert SHA((REPO/'tools/input_capture_mock_flag_probe_selftest.cpp').read_bytes())==binding['harnessSha256']
    assert SHA(header.read_bytes())==binding['jsonHeaderSha256']
    timing={}
    if benchmarks:
        for variant in ['before','current']:
            rows=[r['extra'] for r in benchmarks if r['variant']==variant]
            timing[variant]={'medianNsPerCall':statistics.median(r['nsPerCall'] for r in rows),
                'minNsPerCall':min(r['nsPerCall'] for r in rows),'maxNsPerCall':max(r['nsPerCall'] for r in rows),
                'measuredFileQueries':[r['measuredGetFileAttributesWCalls'] for r in rows],
                'warmupFileQueries':[r['warmupGetFileAttributesWCalls'] for r in rows],
                'operationsPerRound':args.iterations,'rounds':len(rows)}
        timing['medianSpeedup']=timing['before']['medianNsPerCall']/timing['current']['medianNsPerCall']
        timing['medianNsSavedPerCall']=timing['before']['medianNsPerCall']-timing['current']['medianNsPerCall']
    summary={'pass':True,'runDirectory':str(out),'binding':binding,'profiles':profiles,
        'benchmarks':benchmarks,'timingSummary':timing,'realRate':rate,
        'scope':'Actual extracted functions with fake system events and real Win32 GetFileAttributesW against one new temporary directory; no product main, hardware, SDK, registry or installation paths.',
        'targetFunctionStableDuringRun':True,'wholeSourceChangedDuringRun':SHA(sourceEnd)!=SHA(currentRaw),
        'sourceSha256AtEnd':SHA(sourceEnd),'realProbeDirectory':str(fixtureRoot),
        'diagnosticChange':'Persistent product residual flag is discovered at next 1000ms probe (plus caller scheduling/IO); a transient flag appearing and disappearing within the cache window can have no diagnostic. Product mock always false. AI branch byte-for-byte unchanged.'}
    (out/'results.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    (args.output/('candidate-result.json' if args.candidate_function else 'current-result.json')).write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'pass':True,'run':str(out),'profiles':[{'profile':p['profile'],'assertions':p['assertions']} for p in profiles],
        'timingSummary':timing,'realRate':[r['extra'] for r in rate],'targetFunctionStableDuringRun':True,'wholeSourceChangedDuringRun':summary['wholeSourceChangedDuringRun']},indent=2))

if __name__=='__main__':main()
