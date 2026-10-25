"""Actual current Native frame-object construction candidate. Product source is read-only.
No entrypoint, pipe, SDK, crypto, physical source, registry or installed process use.
"""
from pathlib import Path
import argparse,importlib.util,json,hashlib,datetime,subprocess,re,difflib
REPO=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('wire_extract',REPO/'tools/virtual_wire_number_perf_selftest.py');ex=importlib.util.module_from_spec(spec);spec.loader.exec_module(ex)
H=lambda b:hashlib.sha256(b).hexdigest()

def candidate(body):
    marker='    json frame{'
    start=body.index(marker);end=body.index('    if (c.touchpads.present)',start)
    replacement="""    // Build each owned object once; initializer-list temporaries otherwise
    // repeatedly allocate/copy nested JSON values before the final frame exists.
    json frame = json::object();
    frame["backButtons"] = c.backButtons;
    frame["buttons"] = c.buttons;
    json imu = json::object();
    imu["admitted"] = c.imuAdmitted;
    json gyro = json::object();
    gyro["x"] = c.imuAdmitted ? c.gyroX : 0.0;
    gyro["y"] = c.imuAdmitted ? c.gyroY : 0.0;
    gyro["z"] = c.imuAdmitted ? c.gyroZ : 0.0;
    imu["gyroDps"] = std::move(gyro);
    json accel = json::object();
    accel["x"] = c.imuAdmitted ? c.accelX : 0.0;
    accel["y"] = c.imuAdmitted ? c.accelY : 0.0;
    accel["z"] = c.imuAdmitted ? c.accelZ : 0.0;
    imu["accelG"] = std::move(accel);
    json quaternion = json::object();
    quaternion["w"] = c.imuAdmitted ? c.qw : 1.0;
    quaternion["x"] = c.imuAdmitted ? c.qx : 0.0;
    quaternion["y"] = c.imuAdmitted ? c.qy : 0.0;
    quaternion["z"] = c.imuAdmitted ? c.qz : 0.0;
    imu["quaternion"] = std::move(quaternion);
    frame["imu"] = std::move(imu);
    frame["lt"] = c.lt;
    frame["ltRaw"] = c.ltRaw;
    frame["lx"] = inputHostNormalizeAxis(c.lxShort);
    frame["ly"] = inputHostNormalizeAxis(c.lyShort);
    json raw = json::object();
    raw["bLeftTrigger"] = c.rawLtByte;
    raw["bRightTrigger"] = c.rawRtByte;
    raw["buttons"] = c.buttons;
    raw["sThumbLX"] = c.lxShort;
    raw["sThumbLY"] = c.lyShort;
    raw["sThumbRX"] = c.rxShort;
    raw["sThumbRY"] = c.ryShort;
    frame["raw"] = std::move(raw);
    frame["rt"] = c.rt;
    frame["rtRaw"] = c.rtRaw;
    frame["rx"] = inputHostNormalizeAxis(c.rxShort);
    frame["ry"] = inputHostNormalizeAxis(c.ryShort);
"""
    return body[:start]+replacement+body[end:]

def candidate_snapshot(body):
    assert 'auto pad=[](const Contact& p)->Json' in body
    return """inline Json snapshotJson(const Snapshot& s) {
    auto pad=[](const Contact& p)->Json {
        Json value = Json::object();
        value["active"] = p.down;
        value["click"] = p.click;
        value["x"] = p.down ? p.x : 0;
        value["y"] = p.down ? p.y : 0;
        value["pressure"] = p.down ? p.pressure : 0;
        return value;
    };
    Json value = Json::object();
    value["left"] = pad(s.left);
    value["right"] = pad(s.right);
    return value;
}"""

def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--output',type=Path,default=Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R8-20261015/frame-object'));a.add_argument('--final',action='store_true');args=a.parse_args()
    root=args.output;root.mkdir(parents=True,exist_ok=True);run=root/('final-' if args.final else 'candidate-')/datetime.datetime.now().strftime('%Y%m%d-%H%M%S');run.mkdir(parents=True)
    raw=(REPO/'native/main.cpp').read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n');header=(REPO/'deps/json/json.hpp').read_bytes()
    _,_,current=ex.extract(text,'static json focusIsolationAssembleFrame(')
    if args.final:
        before=(root/'before-assembler.cpp').read_text(encoding='utf-8');after=candidate(before);assert current==after
    else:
        before=current;after=candidate(before)
        target=root/'before-assembler.cpp'
        if target.exists():assert target.read_text(encoding='utf-8')==before
        else:target.write_text(before,encoding='utf-8')
    shared=[]
    for marker,cls in [('class C6aNumberTextCache',True),('static C6aNumberTextCache& c6aNumberTextCache()',False),('static void c6aAppendEscaped(std::string& out, const std::string& value)',False),('static void c6aAppendEscaped(std::string& out, const char* value)',False),('static void c6aAppendUInt(',False),('static void c6aAppendInt(',False),('static void c6aAppendJsonValue(',False),('static std::string inputHostBuildSubmitFrameText(',False)]:
        start,end,body=ex.extract(text,marker,cls);shared.append({'marker':marker,'line':text[:start].count('\n')+1,'sha256':H(body.encode()),'source':body})
    constants=[]
    for name in ['kInputHostProtocolConfigHash','kInputHostTargetId','kInputHostOwnerIdentity']:
        m=re.search(r'static constexpr const char\* '+name+r'\s*=.*?;',text,re.S);assert m;constants.append(m.group())
    _,_,parity=ex.extract(text,'static void inputHostVerifySubmitFrameParity(')
    oracle='''static std::string wireOracle(const json& frame,const std::string& requestId,unsigned long long sequence,
unsigned long long sampleSequence,unsigned long long monotonicTick,const std::string& normalizedInputHash,const char* motionPairProofId) {
'''+parity[parity.index('    json reference = json{'):parity.index('    const std::string referenceText = reference.dump();')]+'    return reference.dump();\n}'
    _,_,content=ex.extract(text,'struct FocusFrameContent',True)
    _,_,assemble=ex.extract(text,'static json focusIsolationAssembleFrame(')
    _,_,normalize=ex.extract(text,'static constexpr float inputHostNormalizeAxis(')
    padRaw=(REPO/'native/screen_touchpads.h').read_bytes();padText=padRaw.decode('utf-8-sig').replace('\r\n','\n');padParts=[]
    for marker,cls in [('struct Contact {',True),('struct Snapshot {',True)]:padParts.append(ex.extract(padText,marker,cls)[2])
    _,_,snapshotCurrent=ex.extract(padText,'inline Json snapshotJson(')
    saved=root/'before-snapshot-json.cpp'
    if args.final:
        snapshotBefore=saved.read_text(encoding='utf-8');snapshotAfter=candidate_snapshot(snapshotBefore);assert snapshotCurrent==snapshotAfter
    else:
        snapshotBefore=snapshotCurrent;snapshotAfter=candidate_snapshot(snapshotBefore)
        if saved.exists():assert saved.read_text(encoding='utf-8')==snapshotBefore
        else:saved.write_text(snapshotBefore,encoding='utf-8')
    padParts.append(snapshotBefore)
    snapshotBridge=snapshotAfter.replace('snapshotJson(', 'candidateSnapshotJson(',1)
    data='namespace ymcc::screenpads { using Json=json;\n'+'\n'.join(padParts)+'\n'+snapshotBridge+'\n}\n'+content+'\n'+normalize
    context='''static unsigned long long g_inputHostConfigRevision=17,g_inputHostEpoch=23;
static std::string g_inputHostRunId="fixture-run-\\\"-\\n",g_inputHostStartupNonce="fixture-nonce";
static const char* inputHostBoundOrCurrentPersona(){return "steamdeck";}
static const char* inputHostBoundOrCurrentProfileIdentity(){return "fixture-profile";}
static unsigned long long inputHostProtocolPowerGenerationLocked(){return 29;}
'''
    (run/'context.hpp').write_text('\n'.join(constants)+'\n'+context+oracle+'\n'+data,encoding='utf-8')
    for name,emitter in [('before',before),('candidate',after)]:
        (run/(name+'.hpp')).write_text('#define private public\nnamespace '+name+' {\n'+'\n'.join(f['source'] for f in shared)+'\n'+(emitter.replace('ymcc::screenpads::snapshotJson(', 'ymcc::screenpads::candidateSnapshotJson(') if name=='candidate' else emitter)+'\n}\n#undef private\n',encoding='utf-8')
    (run/'json.hpp').write_bytes(header)
    fixture=(REPO/'tools/virtual_frame_object_perf_selftest.cpp').read_bytes();(run/'fixture.cpp').write_bytes(fixture)
    manifest={'mode':'final' if args.final else 'candidate','mainPath':str(REPO/'native/main.cpp'),'mainSha256':H(raw),'jsonHeaderSha256':H(header),'screenTouchpadsSha256':H(padRaw),'beforeAssemblerSha256':H(before.encode()),'candidateAssemblerSha256':H(after.encode()),'currentAssemblerSha256':H(current.encode()),'sharedFragments':[{k:v for k,v in x.items() if k!='source'} for x in shared],'assemblerSha256':H(assemble.encode()),'contentSha256':H(content.encode()),'padDataSha256':H('\n'.join(padParts).encode()),'beforeSnapshotSha256':H(snapshotBefore.encode()),'candidateSnapshotSha256':H(snapshotAfter.encode()),'currentSnapshotSha256':H(snapshotCurrent.encode()),'snapshotFunctionSelectionSeam':'Only candidate fixture assembler call renamed to candidateSnapshotJson for A/B isolation; real helper bodies copied','oracleSha256':H(oracle.encode()),'fixtureSha256':H(fixture),'runnerSha256':H(Path(__file__).read_bytes()),'stubs':'Only inert identity getters; actual emitter/cache/append/assembler/pad data bodies copied; no Host, OS, crypto, SDK, device or entrypoint execution'}
    (run/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (run/'snapshot.patch').write_text(''.join(difflib.unified_diff(snapshotBefore.splitlines(True),snapshotAfter.splitlines(True),fromfile='before-snapshot',tofile='candidate-snapshot')),encoding='utf-8')
    (run/'assembler.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='before',tofile='candidate')),encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');profiles=[]
    for profile,opts in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        exe=run/(profile+'.exe');cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*opts,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(exe)]
        bat=run/(profile+'-build.bat');bat.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n',encoding='utf-8')
        built=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120);(run/(profile+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8');assert built.returncode==0,str(run/(profile+'-build.log'))
        print(profile+': actual-source fixture compiled',flush=True)
        ran=subprocess.run([str(exe),'verify' if args.final or profile=='Checked' else 'benchmark'],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(run/(profile+'-result.json')).write_text(ran.stdout,encoding='utf-8');(run/(profile+'-stderr.log')).write_text(ran.stderr,encoding='utf-8');assert ran.returncode==0,(run,ran.returncode,ran.stderr)
        result=json.loads(ran.stdout);assert result['pass'];profiles.append({'profile':profile,**result});print(profile+': '+str(result['assertions'])+' assertions',flush=True)
        if profile=='Release':
            childRows=[]
            for variant in ('before','candidate'):
                discovery=subprocess.run([str(exe),'fault-sweep',variant],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=20)
                assert discovery.returncode==0,(variant,discovery.stderr)
                count=json.loads(discovery.stdout)['allocationSites'];rows=[]
                for point in range(count):
                    child=subprocess.run([str(exe),'fault-sweep',variant,str(point)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=20)
                    if child.returncode==86 and 'TERMINATE during injected construction allocation' in child.stderr:
                        data={'point':point,'terminated':True,'cleanupUnobservable':True,'frameConstructedBeforeTerminate':'frameConstructed=true' in child.stderr,'stderr':child.stderr.strip()}
                    else:
                        assert child.returncode==0,(variant,point,child.returncode,child.stderr)
                        data={'point':point,**json.loads(child.stdout)};assert data['pass']
                    rows.append(data)
                childRows.append({'variant':variant,'allocationSites':count,'rows':rows,'terminatedSites':sum(bool(x.get('terminated')) for x in rows),'verifiedCleanupSites':sum(bool(x.get('liveAllocationBalanceRetained')) for x in rows)})
            (run/'fault-results.json').write_text(json.dumps({'pass':True,'variants':childRows,'scope':'Each allocation point in a separate fresh process. Terminated sites explicitly lack cleanup proof; nonterminated sites check live-allocation balance and retry frame bytes. Counts intentionally differ.'},indent=2)+'\n',encoding='utf-8')
    manifest['sourceStableDuringRun']=H((REPO/'native/main.cpp').read_bytes())==H(raw) and H((REPO/'deps/json/json.hpp').read_bytes())==H(header) and H((REPO/'native/screen_touchpads.h').read_bytes())==H(padRaw);assert manifest['sourceStableDuringRun'];result={'pass':True,'runDirectory':str(run),'manifest':manifest,'profiles':profiles,'faultSweepVariants':2,'productionModifiedByRunner':False}
    (run/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8');(run/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');(root/('final-result.json' if args.final else 'candidate-result.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(run)
if __name__=='__main__':main()
