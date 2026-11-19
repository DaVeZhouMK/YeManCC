"""Optional complete gamepad.state publication consumer for the existing wire test.

Physical samples and device getter facts are in-memory ports. The status function,
publication gate, payload assembly, emitter and wire conversion use extracted source.
"""
from pathlib import Path
import datetime,hashlib,importlib.util,json,os,re,subprocess,sys

def run(args,repo):
    if not (args.prepare_only or args.correctness_only):
        raise ValueError('--state-publication requires --prepare-only or --correctness-only')
    spec=importlib.util.spec_from_file_location('state_publication_extract',repo/'tools/virtual_gyro_envelope_perf_selftest.py')
    extract_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract_module)
    def extract(raw,pattern):return extract_module.extract(raw,pattern)['bytes'].decode('utf-8').rstrip('\r\n')
    source=(args.state_source or repo/'native/main.cpp').resolve();raw=source.read_bytes()
    baseline=args.baseline.read_bytes() if args.baseline else raw
    live=(repo/'native/main.cpp').read_bytes()
    patterns={
        'read':rb'^static void gamepadReadStateUnlocked\(bool publishUi\) \{',
        'resolver':rb'^static ComPtr<ICoreWebView2> ipc_view_for_child\(int childId\) \{',
        'generated':rb'^static void ipc_broadcast_controller_snapshot_generated\(',
        'broadcast':rb'^static void ipc_broadcast_controller_snapshot\(',
        'owned':rb'^static void ipc_emit_owned_gamepad_state\(json data\) \{',
        'wide':rb'^static std::string W2U\(const wchar_t\* w, int len = -1\) \{',
        'wide-overload':rb'^static std::string W2U\(const std::wstring& w\) \{',
        'snapshot':rb'^static const json& virtualOutputRuntimeSnapshot\(',
        'formatter':rb'^static json inputHostStartProtectionJson\(',
    }
    parts={name:extract(raw,pattern) for name,pattern in patterns.items()}
    for name,pattern in patterns.items():
        if name!='read':
            if parts[name]!=extract(baseline,pattern) or parts[name]!=extract(live,pattern):
                raise ValueError('Status/emitter dependency drift: '+name)
    stamp=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).strftime('%Y%m%d-%H%M%S-%f')
    out=args.out.resolve()/stamp;out.mkdir(parents=True,exist_ok=False)
    def put(name,data):
        with (out/name).open('xb') as f:f.write(data.encode('utf-8') if isinstance(data,str) else data)
    def pin(path):
        path=Path(path).resolve();b=path.read_bytes();return dict(path=str(path),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    node=Path(sys.executable).parent.parent/'node/bin/node.exe'
    # Use the configured runtime beside the bundled Python on Windows when present.
    if not node.is_file():node=Path(os.environ.get('YMCC_NODE','node'))
    command=[str(node),str(repo/'tools/virtual_output_transition_status_selftest.mjs'),'--prepare-only','--fixed-only','--profiles','Release','--output',str(out/'canonical')]
    start=datetime.datetime.now(datetime.timezone.utc)
    process=subprocess.run(command,cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=40,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    put('canonical-generate.stdout',process.stdout);put('canonical-generate.stderr',process.stderr)
    put('GENERATION.json',json.dumps(dict(args=command,exitCode=process.returncode,startedUtc=start.isoformat(),endedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat()),indent=2)+'\n')
    if process.returncode:raise RuntimeError('Original status generator failed: '+str(out))
    canonical=(out/'canonical/Release-fixed.cpp').read_bytes().decode('utf-8')
    anchor='static ULONGLONG mockTick=10000;'
    wall='''
#ifdef UI_COST_REAL_WALL
#define UI_SYSTEM_WALL_NOW std::chrono::system_clock::now
#else
static std::chrono::system_clock::time_point fixtureWallNow(){return std::chrono::system_clock::time_point(std::chrono::milliseconds(1794816000000ull+mockTick));}
#define UI_SYSTEM_WALL_NOW fixtureWallNow
#endif
'''
    if canonical.count(anchor)!=1 or canonical.count('std::chrono::system_clock::now()')!=1:
        raise ValueError('Original clock port drift')
    observed=canonical.replace(anchor,anchor+wall,1).replace('std::chrono::system_clock::now()','UI_SYSTEM_WALL_NOW()',1)
    if observed.replace(wall,'',1).replace('UI_SYSTEM_WALL_NOW()','std::chrono::system_clock::now()',1)!=canonical:
        raise ValueError('Whole original status consumer port inverse failed')
    put('Canonical.observed.cpp',observed)
    ports_path=repo/'tools/fixtures/controller_snapshot_state_publication_ports.inc'
    template_path=repo/'tools/fixtures/controller_snapshot_state_publication.cpp'
    ports=ports_path.read_bytes().decode('utf-8');wide=parts['wide']+'\n'+parts['wide-overload']
    if ports.count('@@YMCC_W2U@@')!=1:raise ValueError('W2U port placeholder drift')
    ports=ports.replace('@@YMCC_W2U@@',wide)
    wire=(repo/'tools/controller_snapshot_wire_fixture.inc').read_bytes().decode('utf-8')
    emitter='\n'.join(('template<class MakeUtf8>\r\n' if name=='generated' else '')+parts[name] for name in ['resolver','generated','broadcast','owned'])
    resolver='static ComPtr<ICoreWebView2> ipc_view_for_child(int childId) {'
    if emitter.count(resolver)!=1:raise ValueError('Resolver definition drift')
    emitter=emitter.replace(resolver,resolver+'\n    if(observing) resolutions.push_back(childId);',1)
    generated={}
    for name,whole in [('baseline',extract(baseline,patterns['read'])),('candidate',parts['read']),('oldControl',extract(baseline,patterns['read']))]:
        anchor='    if (!publishUi || !g_view) return;'
        if whole.count(anchor)!=1:raise ValueError('UI publication boundary drift')
        tail=whole[whole.index(anchor):]
        wrapper='static void publication(bool publishUi, bool live, const XINPUT_GAMEPAD& pad, const std::string& inputSource, const ymcc::VirtualOutputSourceWindow& statusSourceWindow, unsigned admittedMask) {\n'+tail
        generated[name]='namespace '+name+' {\n'+wire+'\n'+ports+'\n#ifndef UI_COST_REAL_WALL\n#define MultiByteToWideChar observedConvert\n#endif\n'+emitter+'\n#ifndef UI_COST_REAL_WALL\n#undef MultiByteToWideChar\n#endif\n'+wrapper+'\n}\n'
        put(name+'.inc',generated[name])
    control='state["connected"] = live;'
    if generated['candidate'].count(control)!=1:raise ValueError('Connected field mutation anchor drift')
    put('negative.inc',generated['candidate'].replace('namespace candidate {','namespace negative {',1).replace(control,'state["connected"] = false;',1))
    put('Probe.cpp',template_path.read_bytes())
    put('SOURCE-BINDING.json',json.dumps(dict(source=pin(source),baseline=pin(args.baseline) if args.baseline else pin(source),generator=pin(repo/'tools/virtual_output_transition_status_selftest.mjs'),ports=pin(ports_path),template=pin(template_path),runner=pin(Path(__file__)),wholeOriginalStatusConsumerPortInverse=True,completePublicationAndEmitters=True,physicalReadExecuted=False,newPermanentIds=0),indent=2)+'\n')
    if args.prepare_only:
        print(json.dumps(dict(run=str(out),preparedOnly=True,nativeExecuted=False)));return
    results=[]
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    for profile,flags in [('Release','/O2 /DNDEBUG /MT /GF /permissive-'),('Checked','/Od /RTC1 /MT /GF /permissive-')]:
        exe=out/(profile+'.exe')
        cmd='@echo off\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /std:c++20 /EHsc /W4 /WX /wd4505 /utf-8 '+flags+' /I"'+str(out/'canonical/frozen-source/native')+'" /I"'+str(out/'canonical/frozen-source/deps/json')+'" "'+str(out/'Probe.cpp')+'" /Fe:"'+str(exe)+'" /Fo:"'+str(out/(profile+'.obj'))+'"\n'
        put(profile+'.cmd',cmd)
        p=subprocess.run(['cmd.exe','/d','/c',str(out/(profile+'.cmd'))],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));put(profile+'-compile.log',p.stdout)
        if p.returncode:raise RuntimeError(profile+' compile failed: '+str(out))
        rows={}
        for selection,expected in [('canonical',0),('semantics',0),('negative-field',1),('unknown',2)]:
            p=subprocess.run([str(exe),selection],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=45,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));put(profile+'-'+selection+'.stdout',p.stdout);put(profile+'-'+selection+'.stderr',p.stderr)
            if p.returncode!=expected:raise RuntimeError(profile+'/'+selection+' exit '+str(p.returncode))
            rows[selection]=dict(exitCode=p.returncode,stdout=pin(out/(profile+'-'+selection+'.stdout')),stderr=pin(out/(profile+'-'+selection+'.stderr')))
        results.append(dict(profile=profile,selections=rows))
    put('RESULT.json',json.dumps(dict(passAll=True,results=results,scope='Optional complete publication software consumer; no physical read, process CPU or installed acceptance'),indent=2)+'\n')
    print(json.dumps(dict(run=str(out),correctnessPassed=True,performanceRun=False)))
