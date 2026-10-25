"""Offline actual-source equivalence and costs for lazy UI-only motion JSON.

Original per-tick scalar capture points are kept. Existing final 80ms gate remains
in production. The fixture executes extracted builder/snapshot code, not devices.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,subprocess
from virtual_sample_metadata_perf_selftest import matching_brace
REPO=Path(__file__).resolve().parent.parent
SHA=lambda b:hashlib.sha256(b).hexdigest()
MARKER='    json gamepadMotionPlane = json::object();'
END='    MotionSensorFrame motionSensor;'
NEW='    bool motionPlaneSteeringYaw = false;'


def plane_block(text):
    start=text.index(MARKER if MARKER in text else NEW)
    return text[start:text.index(END,start)].rstrip()


def proposed(text):
    old=plane_block(text)
    assert MARKER in old
    init_start=old.index('        gamepadMotionPlane = {')
    op=old.index('{',init_start)
    init=old[op:matching_brace(old,op)+1]
    new='''    bool motionPlaneSteeringYaw = false;
    float motionPlaneGyroMultiplier = 0.0f;
    float motionPlaneAccelMultiplier = 0.0f;
    if (gamepadMotionPlaneProven) {
        // Capture the original scalar inputs at the same pre-Host point.
        // Only the UI diagnostic JSON allocation moves to its existing 80ms gate.
        TickPreHostSubScope mpScope(kTickCostPreHostMotionPlane, &g_tickPreHostSub.motionPlaneUs);
        motionPlaneSteeringYaw = inputCaptureHcSteeringAxisResolvesYaw();
        motionPlaneGyroMultiplier = (float)g_realStickTest.gyroMultiplier;
        motionPlaneAccelMultiplier = (float)g_realStickTest.accelerometerMultiplier;
    }
    const auto buildGamepadMotionPlane = [&]() -> json {
        if (!gamepadMotionPlaneProven) return json::object();
        const bool steeringYaw = motionPlaneSteeringYaw;
        const float gm = motionPlaneGyroMultiplier;
        const float am = motionPlaneAccelMultiplier;
        // HC SetupMotion's yaw/roll order and every numeric expression stay unchanged.
        return '''+init+''';
    };'''
    assert text.count(old)==1
    result=text.replace(old,new)
    consumer='{"gamepadMotionPlane", gamepadMotionPlane}'
    assert result.count(consumer)==1
    return result.replace(consumer,'{"gamepadMotionPlane", buildGamepadMotionPlane()}')


def callback(text):
    a=text.index('static void inputCaptureOnGamepad(bool live, const XINPUT_GAMEPAD& pad',text.index('static bool inputCaptureMockHandshakeFlag() {'))
    op=text.index('{',a)
    return text[a:matching_brace(text,op)+1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--before',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--candidate',action='store_true')
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    out=args.output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));out.mkdir()
    before=args.before.read_text(encoding='utf-8-sig')
    current=proposed(before) if args.candidate else (REPO/'native/main.cpp').read_text(encoding='utf-8-sig')
    expected=proposed(before)
    checks={'callback-only-scalar-capture-lazy-builder-and-consumer':callback(expected)==callback(current),
        'sole-plane-consumer-in-original-ui-gate':current.count('{"gamepadMotionPlane", buildGamepadMotionPlane()}')==1,
        'original-ui-gate-byte-identical':current.count('if (g_hwnd && now - g_inputCaptureLastUi >= 80) {')==before.count('if (g_hwnd && now - g_inputCaptureLastUi >= 80) {')==1,
        'original-proven-predicate-byte-identical':before[before.index('    const bool gamepadMotionPlaneProven ='):before.index(MARKER)]==current[current.index('    const bool gamepadMotionPlaneProven ='):current.index(NEW)]}
    assert all(checks.values()),checks
    (out/'candidate-main.cpp').write_text(current,encoding='utf-8')
    (out/'motion-plane.diff').write_text(''.join(difflib.unified_diff(callback(before).splitlines(True),callback(current).splitlines(True),fromfile='before/callback',tofile='current/callback')),encoding='utf-8')
    blocks={k:plane_block(v) for k,v in [('before',before),('current',current)]}
    generated=''
    gate='if (g_hwnd && now - g_inputCaptureLastUi >= 80) {'
    for k,block in blocks.items():
        consumer='gamepadMotionPlane' if k=='before' else 'buildGamepadMotionPlane()'
        generated+='namespace '+k+'Fixture {\njson tick(Context& c,bool mutateAfterSnapshot) {\n'+ALIASES+'\n'+block+'\n'
        generated+='    if(mutateAfterSnapshot){c.config.gyroMultiplier=37.;c.config.accelerometerMultiplier=-29.;c.config.yaw=!c.config.yaw;}\n'
        generated+='    '+gate+'\n        g_inputCaptureLastUi = now;\n        return json{{"gamepadMotionPlane",'+consumer+'}};\n    }\n    return nullptr;\n}\n}\n'
    (out/'plane-extracted.h').write_text(generated,encoding='utf-8')
    harness=(REPO/'tools/virtual_motion_plane_perf_selftest.cpp').read_bytes();(out/'fixture.cpp').write_bytes(harness)
    binding={'beforePath':str(args.before.resolve()),'currentPath':str(REPO/'native/main.cpp'),
        'beforeFileSha256':SHA(args.before.read_bytes()),'currentTextSha256':SHA(current.encode()),'candidateOnly':args.candidate,
        'callbackBeforeSha256':SHA(callback(before).encode()),'callbackCurrentSha256':SHA(callback(current).encode()),
        'blocks':{k:SHA(v.encode()) for k,v in blocks.items()},'jsonHeaderSha256':SHA((REPO/'deps/json/json.hpp').read_bytes()),
        'harnessSha256':SHA(harness),'checks':checks,
        'timingScope':'Only exact builder/snapshot, gate and plane-field assembly/destruction; fake scalar surroundings and cost-scope. No Host/device/whole telemetry/full BUS executed.'}
    (out/'source-binding.json').write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8')
    vcvars=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    results=[]
    for name,opts in [('release',['/O2','/DNDEBUG','/MT']),('checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        exe=out/(name+'.exe')
        cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc',*opts,str(out/'fixture.cpp'),'/I'+str(REPO/'deps/json'),'/I'+str(out),'/Fo'+str(out/(name+'.obj')),'/Fe'+str(exe)]
        batch=out/(name+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n',encoding='utf-8')
        build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,capture_output=True,encoding='utf-8',errors='replace',timeout=120)
        (out/(name+'-build.log')).write_text(build.stdout+build.stderr,encoding='utf-8')
        if build.returncode:raise RuntimeError('Build failed: '+str(batch))
        ran=subprocess.run([str(exe)],cwd=out,capture_output=True,encoding='utf-8',errors='replace',timeout=90)
        (out/(name+'-result.json')).write_text(ran.stdout,encoding='utf-8');(out/(name+'-stderr.txt')).write_text(ran.stderr,encoding='utf-8')
        if ran.returncode:raise RuntimeError('Fixture failed: '+ran.stderr)
        result=json.loads(ran.stdout);result['profile']=name;results.append(result)
    assert SHA((REPO/'tools/virtual_motion_plane_perf_selftest.cpp').read_bytes())==binding['harnessSha256']
    assert args.candidate or callback((REPO/'native/main.cpp').read_text(encoding='utf-8-sig'))==callback(current)
    receipt={'pass':True,'run':str(out),'binding':binding,'profiles':results,'wholeProcessCpuOrPowerMeasured':False}
    (args.output/'result.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'pass':True,'run':str(out),'profiles':[{'profile':r['profile'],'assertions':r['assertions'],'benchmarks':r['benchmarks']} for r in results]},indent=2))

ALIASES=r'''
    Active active(c);
    const bool gamepadMotionPlaneProven=c.proven;
    const auto& g_realStickTest=c.config;
    const double cgx=c.cgx,cgy=c.cgy,cgz=c.cgz,gravityX=c.gravityX,gravityY=c.gravityY,gravityZ=c.gravityZ;
    const double linearAccelX=c.linearAccelX,linearAccelY=c.linearAccelY,linearAccelZ=c.linearAccelZ;
    const double orientationW=c.orientationW,orientationX=c.orientationX,orientationY=c.orientationY,orientationZ=c.orientationZ;
    const float psx=c.psx,psy=c.psy,wsx=c.wsx,wsy=c.wsy;
    const std::uintptr_t g_hwnd=c.window;
    const unsigned long long now=c.now;
    auto& g_inputCaptureLastUi=c.lastUi;
'''
if __name__=='__main__':main()
