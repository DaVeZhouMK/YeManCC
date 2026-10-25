"""Read-only ownership/cost candidate for actual final gyro.telemetry hostFrame entry.

Only this runner, its matching C++ fixture and --output evidence are created.
Source, Host and existing tools are never written. The full event initializer,
plane lambda, two owned wrappers and event struct are extracted from source.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,re,subprocess

REPO=Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT=Path('G:/YeManCC-Work/Build/Validation/VirtualInputExitAudit-20261014-223258/telemetry-hostframe-candidate')
SAMPLE=Path('G:/YeManCC-Work/Build/Validation/VirtualGamepadPerf-R2-20261014/record-final/run-20261014-220817-44380/real-sample.json')
SHA=lambda b:hashlib.sha256(b).hexdigest()

def close_brace(text,start):
    if text[start]!='{':raise ValueError('Expected opening brace')
    depth=0;i=start;mode='code'
    while i<len(text):
        c=text[i];d=text[i:i+2]
        if mode=='line':
            if c=='\n':mode='code'
        elif mode=='block':
            if d=='*/':mode='code';i+=1
        elif mode in ('string','char'):
            if c=='\\':i+=1
            elif c==('"' if mode=='string' else "'"):mode='code'
        elif d=='//':mode='line';i+=1
        elif d=='/*':mode='block';i+=1
        elif c=='"':mode='string'
        elif c=="'":mode='char'
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if depth==0:return i
        i+=1
    raise ValueError('Unclosed source block')

def function(text,marker):
    starts=[m.start() for m in re.finditer(re.escape(marker),text)]
    candidates=[]
    for start in starts:
        opening=text.find('{',start)
        semicolon=text.find(';',start)
        if opening>=0 and (semicolon<0 or opening<semicolon):
            end=close_brace(text,opening)+1;candidates.append((start,end,text[start:end]))
    if len(candidates)!=1:raise ValueError(f'Expected one function body for {marker}: {len(candidates)}')
    return candidates[0]

def fragments(text):
    cbStart,cbEnd,callback=function(text,'static void inputCaptureOnGamepad(')
    eventStart=callback.index('gamepadSerialPostOwnedEvent("gyro.telemetry", {')
    opening=callback.index('{',eventStart);end=close_brace(callback,opening)+1
    assert callback[end:end+2]==');'
    init=callback[opening:end];call=callback[eventStart:end+2]
    assert init.count('{"hostFrame", hostFrame}')==1
    candidate=init.replace('{"hostFrame", hostFrame}','{"hostFrame", std::move(hostFrame)}',1)
    planeStart=callback.index('const auto buildGamepadMotionPlane = [&]() -> json {')
    planeOpening=callback.index('{',planeStart);planeEnd=close_brace(callback,planeOpening)+1
    assert callback[planeEnd]==';'
    plane=callback[planeStart:planeEnd+1]
    wrappers=[]
    wrapperPositions={}
    for marker in ['static void gamepadSerialPostOwnedEvent(','static void ipc_emit_owned_gyro_telemetry(']:
        start,stop,value=function(text,marker);wrappers.append(value);wrapperPositions[marker]=start
    structStart=text.index('struct GamepadSerialEvent {');structOpening=text.index('{',structStart)
    structEnd=close_brace(text,structOpening)+1;assert text[structEnd]==';'
    eventStruct=text[structStart:structEnd+1]
    tail=callback[end+2:]
    fields=re.findall(r'^\s*\{"([^"]+)",',init,re.M)
    valueReads=[m.start() for m in re.finditer(r'\bhostFrame\.value\(',init)]
    hostEntry=init.index('{"hostFrame", hostFrame}')
    localDecl=callback.index('json hostFrame = json::object();')
    refs=[{'line':text[:cbStart+m.start()].count('\n')+1,'text':callback.splitlines()[callback[:m.start()].count('\n')].strip()} for m in re.finditer(r'\bhostFrame\b',callback)]
    observers={}
    for marker in ['static void inputHostSubmitOutcomeNote(','static void inputCaptureCommonFrameEvidence(',
        'static void steamDeckUiOutputNote(','static void msiUiOutputNote(','static void ps5UiOutputNote(']:
        start,stop,value=function(text,marker);observers[marker]={'line':text[:start].count('\n')+1,'source':value,'sha256':SHA(value.encode())}
    proof={'callbackLine':text[:cbStart].count('\n')+1,'callbackEndLine':text[:cbEnd].count('\n')+1,
        'eventCallLine':text[:cbStart+eventStart].count('\n')+1,'hostFrameEntryLine':text[:cbStart+opening+hostEntry].count('\n')+1,
        'localJsonOwner':callback[localDecl:localDecl+len('json hostFrame = json::object();')],
        'eventFields':fields,'eventFieldCount':len(fields),'hostFrameValueReads':len(valueReads),
        'allValueReadsBeforeHostFrameEntry':all(pos<hostEntry for pos in valueReads),
        'hostFrameEntryIsFinalInitializerEntry':init[hostEntry:].replace('\r\n','\n').strip()=='{"hostFrame", hostFrame}\n        }',
        'noHostFrameIdentifierAfterCall':re.search(r'\bhostFrame\b',tail) is None,
        'tailAfterCall':tail,'hostFrameReferences':refs,
        'beforeInitializerSha256':SHA(init.encode()),'candidateInitializerSha256':SHA(candidate.encode()),
        'candidateOnlyOneEntryChanged':candidate.replace('{"hostFrame", std::move(hostFrame)}','{"hostFrame", hostFrame}',1)==init,
        'planeLambdaSha256':SHA(plane.encode()),'wrapperLocations':{k:text[:v].count('\n')+1 for k,v in wrapperPositions.items()},
        'observers':observers,
        'jsonCppSequencing':'Braced initializer clauses are evaluated in their written order; all nine value() expressions precede the final hostFrame json_ref construction.',
        'aliasAudit':'Earlier source observers consume const json references synchronously; stored state is scalar/string copies. The frame evidence logger copies its proof into an owned log record. No async hostFrame reference/address escapes in these observer bodies.'}
    assert proof['eventFieldCount']==27 and proof['hostFrameValueReads']==9
    assert proof['allValueReadsBeforeHostFrameEntry'] and proof['hostFrameEntryIsFinalInitializerEntry'] and proof['noHostFrameIdentifierAfterCall']
    return {'callback':callback,'before':init,'candidate':candidate,'call':call,'plane':plane,'wrappers':wrappers,'struct':eventStruct,'proof':proof}

def generated(parts):
    methods=''
    for prefix,init in [('before',parts['before']),('candidate',parts['candidate'])]:
        methods+='json '+prefix+'Initializer() {\n'+parts['plane']+'\nreturn '+init+';\n}\n'
        call=parts['call'] if prefix=='before' else parts['call'].replace('{"hostFrame", hostFrame}','{"hostFrame", std::move(hostFrame)}',1)
        methods+='void '+prefix+'Post() {\n'+parts['plane']+'\n'+call+'\n}\n'
        methods+='void '+prefix+'Gated() {\nif (g_hwnd && now - g_inputCaptureLastUi >= 80) {\ng_inputCaptureLastUi = now;\n'+prefix+'Post();\n}\n}\n'
    wrappers='\n\n'.join(value.replace('static void ','void ',1) for value in parts['wrappers'])
    return methods,wrappers

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=DEFAULT_OUTPUT)
    parser.add_argument('--sample',type=Path,default=SAMPLE)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    out=args.output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
    sourcePath=REPO/'native/main.cpp';raw=sourcePath.read_bytes();text=raw.decode('utf-8-sig')
    parts=fragments(text);methods,wrappers=generated(parts)
    harnessPath=REPO/'tools/virtual_telemetry_hostframe_perf_selftest.cpp';harness=harnessPath.read_bytes()
    jsonPath=REPO/'deps/json/json.hpp';jsonRaw=jsonPath.read_bytes();sampleRaw=args.sample.read_bytes()
    (out/'source-main.cpp').write_bytes(raw);(out/'json.hpp').write_bytes(jsonRaw);(out/'fixture.cpp').write_bytes(harness)
    (out/'sample.json').write_bytes(sampleRaw)
    (out/'event-extracted.h').write_text(methods,encoding='utf-8')
    (out/'owned-wrappers-extracted.h').write_text(wrappers,encoding='utf-8')
    (out/'event-struct-extracted.h').write_text(parts['struct'],encoding='utf-8')
    (out/'before-initializer.cpp').write_text(parts['before'],encoding='utf-8')
    (out/'candidate-initializer.cpp').write_text(parts['candidate'],encoding='utf-8')
    (out/'initializer.diff').write_text(''.join(difflib.unified_diff(parts['before'].splitlines(True),parts['candidate'].splitlines(True),fromfile='before/gyro.telemetry-initializer',tofile='candidate/gyro.telemetry-initializer')),encoding='utf-8')
    (out/'ownership-proof.json').write_text(json.dumps(parts['proof'],ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    binding={'sourcePath':str(sourcePath),'sourceSha256AtEntry':SHA(raw),'jsonHeaderSha256':SHA(jsonRaw),
        'harnessSha256':SHA(harness),'runnerSha256':SHA(Path(__file__).read_bytes()),'samplePath':str(args.sample.resolve()),'sampleSha256':SHA(sampleRaw),
        'sampleUse':'R2 saved real G3E scalar/IMU values; hostFrame fixtures follow current source schema, not a claim of real captured hostFrame or full-product telemetry.',
        'candidateOnly':True,'productionWriteSet':[],
        'adapters':'Two wrappers change static free-function declarations to member methods only; bodies exact. Sensor meta, window/exit/cost flags, PostMessage and renderer dispatch are fixture-owned in-memory seams. No Windows message is posted.',
        'extracted':{key:SHA(value.encode()) for key,value in parts.items() if isinstance(value,str)},
        'cppVersion':'C++20','jsonAllocator':'nlohmann::json default std::allocator; counted ordinary/aligned operator new; timing counters disabled in separate timing runs.'}
    (out/'source-binding.json').write_text(json.dumps(binding,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    vcvars=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    profiles=[]
    for profile,options,batches in [('release',['/O2','/DNDEBUG','/MT'],12),('checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'],2)]:
        exe=out/(profile+'.exe')
        command=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*options,str(out/'fixture.cpp'),'/I'+str(out),'/Fo'+str(out/(profile+'.obj')),'/Fe'+str(exe)]
        batch=out/(profile+'-build.bat')
        batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n',encoding='utf-8')
        built=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (out/(profile+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8')
        if built.returncode:raise RuntimeError('Compile failed: '+str(out/(profile+'-build.log')))
        ran=subprocess.run([str(exe),str(out/'sample.json'),str(batches)],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=240)
        (out/(profile+'-run.json')).write_text(ran.stdout,encoding='utf-8');(out/(profile+'-run.stderr')).write_text(ran.stderr,encoding='utf-8')
        if ran.returncode:raise RuntimeError('Fixture failed: '+str(out/(profile+'-run.stderr'))+': exit='+str(ran.returncode)+': '+ran.stderr)
        result=json.loads(ran.stdout);result['profile']=profile
        faults=[]
        for variant in ['before','candidate']:
            warm=subprocess.run([str(exe),str(out/'sample.json'),'0',variant,'-1'],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=15)
            assert warm.returncode==0,warm.stderr
            warmResult=json.loads(warm.stdout);limit=warmResult['eventAllocationCalls']
            faults.append({'variant':variant,'faultIndex':-1,'returnCode':0,'result':warmResult})
            for position in sorted({0,27,limit-1,limit}):
                try:
                    trial=subprocess.run([str(exe),str(out/'sample.json'),'0',variant,str(position)],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=3)
                    row={'variant':variant,'faultIndex':position,'returnCode':trial.returncode,'exitHex':hex(trial.returncode & 0xffffffff),'stdout':trial.stdout,'stderr':trial.stderr}
                except subprocess.TimeoutExpired as error:
                    row={'variant':variant,'faultIndex':position,'returnCode':-999,'exitHex':None,'timeout':True,'stdout':str(error.stdout or ''),'stderr':str(error.stderr or '')}
                if row['returnCode']==0:
                    row['result']=json.loads(row['stdout'])
                    assert row['result']['badAllocCaught']==(position<limit),'Fault trial caught-state inconsistent'
                    assert row['result']['liveAllocationBalance']==0,'Caught failure or complete owned path leaked a fixture allocation'
                faults.append(row)
        result['isolatedAllocationFaults']=faults
        result['oomExceptionParityClaimed']=False
        (out/(profile+'-isolated-faults.json')).write_text(json.dumps(faults,indent=2)+'\n',encoding='utf-8')
        profiles.append(result)
    finalRaw=sourcePath.read_bytes();finalParts=fragments(finalRaw.decode('utf-8-sig'))
    assert all(finalParts[k]==parts[k] for k in ['callback','before','plane','wrappers','struct']),'Bound production fragments drifted during run'
    assert jsonPath.read_bytes()==jsonRaw and harnessPath.read_bytes()==harness,'Bound header or harness drifted'
    result={'pass':True,'runDirectory':str(out),'binding':binding,'ownershipProof':parts['proof'],'profiles':profiles,
        'productionModified':False,'sourceSha256AtEnd':SHA(finalRaw),'wholeSourceStableDuringRun':raw==finalRaw,'boundFragmentsStable':True,
        'scope':'Only isolated extracted C++ fixture runs; actual full initializer plus actual owned message/emitter bodies, default real json.hpp. Windows/UI/device calls are in-memory seams. No Host/native main or device SDK executed.'}
    (out/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (args.output/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'pass':True,'run':str(out),'ownership':{k:parts['proof'][k] for k in ['eventFieldCount','hostFrameValueReads','allValueReadsBeforeHostFrameEntry','noHostFrameIdentifierAfterCall']},
        'profiles':[{'profile':p['profile'],'assertions':p['assertions'],'allocationFaultTrials':len(p['isolatedAllocationFaults']),'allocationFailFastTrials':sum(x['returnCode']!=0 for x in p['isolatedAllocationFaults']),'benchmarks':[{k:b[k] for k in ['name','mode','beforeMedianNs','candidateMedianNs','beforeCallsPerEvent','candidateCallsPerEvent','beforeBytesPerEvent','candidateBytesPerEvent','pairedWins']} for b in p['benchmarks']]} for p in profiles],
        'productionModified':False,'wholeSourceStableDuringRun':raw==finalRaw},ensure_ascii=False,indent=2))

if __name__=='__main__':main()
