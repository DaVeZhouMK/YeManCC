"""Actual current Native frame-wire iterator candidate. Product source is read-only.
No entrypoint, pipe, SDK, crypto, physical source, registry or installed process use.
"""
from pathlib import Path
import argparse,importlib.util,json,hashlib,datetime,subprocess,re,difflib
REPO=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('wire_extract',REPO/'tools/virtual_wire_number_perf_selftest.py');ex=importlib.util.module_from_spec(spec);spec.loader.exec_module(ex)
H=lambda b:hashlib.sha256(b).hexdigest()

def candidate(body):
    old='''    auto field = [&](const char* name) {
        key(name);
        c6aAppendJsonValue(out, frame.at(name));
    };'''
    new='''    // Fields below are requested in the default JSON object's sorted order.
    // Keep at() as the fallback so malformed roots/missing fields retain the
    // original exception and lookup order. No field values or iterators escape.
    auto currentField = frame.cbegin();
    const auto fieldEnd = frame.cend();
    auto findOrderedField = [&](const char* name) -> const json* {
        if (!frame.is_object()) return nullptr;
        while (currentField != fieldEnd && currentField.key().compare(name) < 0)
            ++currentField;
        return currentField != fieldEnd && currentField.key() == name
            ? &currentField.value() : nullptr;
    };
    auto field = [&](const char* name) {
        key(name);
        const json* value = findOrderedField(name);
        c6aAppendJsonValue(out, value ? *value : frame.at(name));
    };'''
    assert body.count(old)==1
    body=body.replace(old,new)
    for name in ('screenButtons','touchpads'):
        old=f'if (frame.contains("{name}")) field("{name}");'
        new=f'if (findOrderedField("{name}")) field("{name}");'
        assert body.count(old)==1
        body=body.replace(old,new)
    return body

def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--output',type=Path,default=Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R8-20261015/ordered-wire'));a.add_argument('--final',action='store_true');args=a.parse_args()
    root=args.output;root.mkdir(parents=True,exist_ok=True);run=root/('final-' if args.final else 'candidate-')/datetime.datetime.now().strftime('%Y%m%d-%H%M%S');run.mkdir(parents=True)
    raw=(REPO/'native/main.cpp').read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n');header=(REPO/'deps/json/json.hpp').read_bytes()
    _,_,current=ex.extract(text,'static std::string inputHostBuildSubmitFrameText(')
    if args.final:
        before=(root/'before-emitter.cpp').read_text(encoding='utf-8');after=candidate(before);assert current==after
    else:
        before=current;after=candidate(before)
        target=root/'before-emitter.cpp'
        if target.exists():assert target.read_text(encoding='utf-8')==before
        else:target.write_text(before,encoding='utf-8')
    shared=[]
    for marker,cls in [('class C6aNumberTextCache',True),('static C6aNumberTextCache& c6aNumberTextCache()',False),('static void c6aAppendEscaped(std::string& out, const std::string& value)',False),('static void c6aAppendEscaped(std::string& out, const char* value)',False),('static void c6aAppendUInt(',False),('static void c6aAppendInt(',False),('static void c6aAppendJsonValue(',False)]:
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
    for marker,cls in [('struct Contact {',True),('struct Snapshot {',True),('inline Json snapshotJson(',False)]:padParts.append(ex.extract(padText,marker,cls)[2])
    data='namespace ymcc::screenpads { using Json=json;\n'+'\n'.join(padParts)+'\n}\n'+content+'\n'+normalize+'\n'+assemble
    context='''static unsigned long long g_inputHostConfigRevision=17,g_inputHostEpoch=23;
static std::string g_inputHostRunId="fixture-run-\\\"-\\n",g_inputHostStartupNonce="fixture-nonce";
static const char* inputHostBoundOrCurrentPersona(){return "steamdeck";}
static const char* inputHostBoundOrCurrentProfileIdentity(){return "fixture-profile";}
static unsigned long long inputHostProtocolPowerGenerationLocked(){return 29;}
'''
    (run/'context.hpp').write_text('\n'.join(constants)+'\n'+context+oracle+'\n'+data,encoding='utf-8')
    for name,emitter in [('before',before),('candidate',after)]:
        (run/(name+'.hpp')).write_text('#define private public\nnamespace '+name+' {\n'+'\n'.join(f['source'] for f in shared)+'\n'+emitter+'\n}\n#undef private\n',encoding='utf-8')
    (run/'json.hpp').write_bytes(header)
    fixture=(REPO/'tools/virtual_wire_order_perf_selftest.cpp').read_bytes();(run/'fixture.cpp').write_bytes(fixture)
    manifest={'mode':'final' if args.final else 'candidate','mainPath':str(REPO/'native/main.cpp'),'mainSha256':H(raw),'jsonHeaderSha256':H(header),'screenTouchpadsSha256':H(padRaw),'beforeEmitterSha256':H(before.encode()),'candidateEmitterSha256':H(after.encode()),'currentEmitterSha256':H(current.encode()),'sharedFragments':[{k:v for k,v in x.items() if k!='source'} for x in shared],'assemblerSha256':H(assemble.encode()),'contentSha256':H(content.encode()),'padDataSha256':H('\n'.join(padParts).encode()),'oracleSha256':H(oracle.encode()),'fixtureSha256':H(fixture),'runnerSha256':H(Path(__file__).read_bytes()),'stubs':'Only inert identity getters; actual emitter/cache/append/assembler/pad data bodies copied; no Host, OS, crypto, SDK, device or entrypoint execution'}
    (run/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (run/'emitter.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='before',tofile='candidate')),encoding='utf-8')
    vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');profiles=[]
    for profile,opts in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        exe=run/(profile+'.exe');cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*opts,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(exe)]
        bat=run/(profile+'-build.bat');bat.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n',encoding='utf-8')
        built=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120);(run/(profile+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8');assert built.returncode==0,str(run/(profile+'-build.log'))
        print(profile+': actual-source fixture compiled',flush=True)
        ran=subprocess.run([str(exe),'verify' if args.final or profile=='Checked' else 'benchmark'],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(run/(profile+'-result.json')).write_text(ran.stdout,encoding='utf-8');(run/(profile+'-stderr.log')).write_text(ran.stderr,encoding='utf-8');assert ran.returncode==0,(run,ran.returncode,ran.stderr)
        result=json.loads(ran.stdout);assert result['pass'];profiles.append({'profile':profile,**result});print(profile+': '+str(result['assertions'])+' assertions',flush=True)
        # Fresh process per deterministic fault site; prevent stale cache state.
        if profile=='Release':
            faultRows=[]
            for variant in ('before','candidate'):
                for scenario in ('full','missing'):
                    for point in range(12):
                        child=subprocess.run([str(exe),'fault',variant,scenario,str(point)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=20)
                        assert child.returncode==0,(variant,scenario,point,child.stderr)
                        value=json.loads(child.stdout);faultRows.append(value)
            for scenario in ('full','missing'):
                for point in range(12):
                    x=next(v for v in faultRows if v['variant']=='before' and v['scenario']==scenario and v['point']==point);y=next(v for v in faultRows if v['variant']=='candidate' and v['scenario']==scenario and v['point']==point)
                    assert {k:v for k,v in x.items() if k!='variant'}=={k:v for k,v in y.items() if k!='variant'},(x,y)
            (run/'fault-results.json').write_text(json.dumps({'pass':True,'childProcesses':48,'perPointOutcomesIdentical':True,'rows':faultRows},indent=2)+'\n',encoding='utf-8')
    manifest['sourceStableDuringRun']=H((REPO/'native/main.cpp').read_bytes())==H(raw) and H((REPO/'deps/json/json.hpp').read_bytes())==H(header) and H((REPO/'native/screen_touchpads.h').read_bytes())==H(padRaw);assert manifest['sourceStableDuringRun'];result={'pass':True,'runDirectory':str(run),'manifest':manifest,'profiles':profiles,'faultChildren':48,'productionModifiedByRunner':False}
    (run/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8');(run/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');(root/('final-result.json' if args.final else 'candidate-result.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(run)
if __name__=='__main__':main()
