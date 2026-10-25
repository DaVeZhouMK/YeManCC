#!/usr/bin/env python3
"""R20 source-bound base record optimization: offline correctness before timing.
No product/device/service/driver/registry API executes.
"""
from __future__ import annotations
from pathlib import Path
import argparse, datetime, difflib, hashlib, json, os, re, subprocess, sys, zipfile
sys.dont_write_bytecode = True
from virtual_sample_tree_perf_selftest import pairs, match_brace, source_function
from virtual_sample_queue_perf_selftest import DEFAULT_ZIP
REPO=Path(__file__).resolve().parents[1]
OUTPUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R20-20261015'
VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN={'campaigns':2,'rounds':11,'minimumFasterRounds':10,'minimumPairedReduction':.05,'minimumRatioReduction':.05,
      'fewerAllocationRequests':True,'fewerRequestedBytes':True,'batchSize':32,'measurementBatches':24,'warmupBatches':6,
      'scopes':['record-assembly','record-dump','writer-copy-dump'],
      'decision':'Every row in both profiles and campaigns must pass. No threshold relaxation.'}
sha=lambda data:hashlib.sha256(data.encode('utf-8') if isinstance(data,str) else data).hexdigest()
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data.encode('utf-8') if isinstance(data,str) else data)
def save(path,obj):write(path,json.dumps(obj,ensure_ascii=False,indent=2)+'\n')
def blocks(text):
    callback=source_function(text,'static void inputCaptureOnGamepad(')
    start=callback.index('    if (wantFullSample) {')
    full=callback[start:match_brace(callback,callback.index('{',start))+1]
    a=full.index('        rec = ');b=full.index('        // Transfer completed nested diagnostic trees',a)
    record=full[a:b].rstrip()
    transfers=re.search(r'        rec\["imu"\] = std::move\(imuObj\);[\s\S]*?        rec\["testLane"\] = std::move\(testLaneLiveObj\);',full).group()
    a=full.index('        if (!(gyroSequence != 0 || accelSequence != 0)) {')
    inactive=full[a:match_brace(full,full.index('{',a))+1]
    logger=source_function(text,'static void inputCaptureAppend(const json& rec)')
    a=logger.index('    auto record = rec;');b=logger.index('    const auto line = record.dump();',a)+len('    const auto line = record.dump();')
    return {'callback':callback,'producer':full,'record':record,'transfers':transfers,'inactive':inactive,
            'logger':logger,'writer':logger[a:b],
            'queue':source_function(text,'static void inputCaptureQueueSample(json rec)'),
            'drain':source_function(text,'static void inputCaptureDrainQueuedSamples()')}
def transform(old):
    parsed=pairs(old[old.index('{'):old.rindex('}')+1]);assert len(parsed)==26,len(parsed)
    expressions=[];keys=[];children=[];rows=['        rec = json::object();']
    for key,val in parsed:
        keys.append(json.loads(key))
        if val.startswith('{'):
            name='sample'+json.loads(key)[0].upper()+json.loads(key)[1:]+'Obj';children.append(name)
            rows.append('        json '+name+' = json::object();')
            for ck,cv in pairs(val):
                expressions.append((ck,cv));rows.append('        '+name+'['+ck+'] = '+cv+';')
            val='std::move('+name+')'
        else:expressions.append((key,val))
        rows.append('        rec['+key+'] = '+val+';')
    assert len(expressions)==30
    return '\n'.join(rows),{'topLevelKeys':keys,'topLevelCount':len(keys),'scalarLeafCount':len(expressions),
        'scalarExpressionsInOrder':expressions,'movedChildren':children}
def source_types(text,bound):
    callback=bound['callback'];out=[]
    for needle in ['const ULONGLONG now =','const float motionDeltaSeconds =','const XINPUT_GAMEPAD canonicalPad =',
                   'float psx =','float ox =','bool steady =','UINT64 gyroSequence =']:
        a=callback.index(needle);out.append(callback[a:callback.index(';',a)+1])
    for needle in ['static void* g_gm =','static unsigned long long g_inputCaptureBusTickGlobal =']:
        a=text.index(needle);out.append(text[a:text.index(';',a)+1])
    out.append(callback[:callback.index('{')])
    for typ,names in [('struct RealStickTestConfig',['standaloneTestLane','motionTriggerResolved','aimingSightsTriggerResolved']),('struct HcMotionSelectorReceipt',['resolved'])]:
        body=source_function(text,typ)
        for name in names:out.append(re.search(r'bool '+name+r'\s*=\s*[^;]+;',body).group())
    return out
def header(before,after):
    text=''
    for label,bound in [('baselineRecord',before),('candidateRecord',after)]:
        text+='static NOINLINE json '+label+'(Input& v) {\n'
        text+='''    const ULONGLONG now=v.now;
    const unsigned long long g_inputCaptureBusTickGlobal=v.busTick;
    const float motionDeltaSeconds=v.motionDeltaSeconds;
    const bool live=v.live;
    const DWORD primarySlot=v.primarySlot, connectedMask=v.connectedMask;
    const std::string& inputSource=v.inputSource;
    const XINPUT_GAMEPAD canonicalPad=v.canonicalPad;
    const auto& g_realStickTest=v.config;
    const auto& g_hcMotionSelector=v.selector;
    const std::string& matrixIdentity=v.matrixIdentity;
    const bool hcMatrixAdmitted=v.hcMatrixAdmitted;
    const float ox=v.ox,oy=v.oy,oz=v.oz,conf=v.conf,psx=v.psx,psy=v.psy,wsx=v.wsx,wsy=v.wsy;
    const bool steady=v.steady;
    void* g_gm=v.gm;
    const UINT64 gyroSequence=v.gyroSequence,accelSequence=v.accelSequence;
    json& imuObj=v.imu; json& hcCandidateObj=v.hc;
    json& eventObj=v.event; json& testLaneLiveObj=v.test;
    json rec;
'''
        text+=bound['record']+'\n'+bound['transfers']+'\n'
        text+='    for(auto it=v.extras.begin();it!=v.extras.end();++it) rec[it.key()]=std::move(it.value());\n'
        text+=bound['inactive']+'\n    return rec;\n}\n'
    text+='static NOINLINE std::string writerDump(const json& rec,DWORD fixturePid) {\n'
    text+=before['writer'].replace('GetCurrentProcessId()','fixturePid')+'\n    return line;\n}\n'
    return text
def corpus(run,archive):
    raw=archive.read_bytes();records=[];refs=[];entries=[]
    with zipfile.ZipFile(archive) as z:
        for name in sorted(z.namelist()):
            if Path(name).name not in ('input-capture.jsonl','input-capture.jsonl.prev'):continue
            content=z.read(name);entries.append({'entry':name,'sha256':sha(content),'bytes':len(content)})
            for line,data in enumerate(content.splitlines(),1):
                try:value=json.loads(data)
                except (ValueError,UnicodeDecodeError):continue
                if isinstance(value,dict) and value.get('kind')=='sample':
                    records.append(value);refs.append({'entry':name,'line':line,'sha256':sha(data)})
    assert len(records)==2301, len(records)
    active=[i for i,v in enumerate(records) if 'gx' in v.get('imu',{})]
    inactive=next(i for i,v in enumerate(records) if v.get('imu',{}).get('imuActive') is False)
    largest=max(active,key=lambda i:len(json.dumps(records[i])))
    selected=[('active-real-early',active[0]),('active-real-late',active[-1]),('active-real-delta',largest),('inactive-real',inactive)]
    save(run/'samples.json',[{'name':name,'sample':records[i],'source':refs[i]} for name,i in selected]);save(run/'all-samples.json',records);save(run/'sample-references.json',refs)
    return {'archive':str(archive),'archiveSha256':sha(raw),'entries':entries,'samples':len(records),'active':len(active),
            'inactive':len(records)-len(active),'fieldHardwareEvidenceOnly':True}
def invoke(exe,run,mode,campaign=None):
    args=[str(exe),mode,str(run/'samples.json'),str(run/'all-samples.json')]
    if campaign is not None:args.append(str(campaign))
    proc=subprocess.run(args,cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=240)
    label=exe.stem+'-'+mode+('' if campaign is None else '-'+str(campaign))
    write(run/(label+'.stdout.json'),proc.stdout);write(run/(label+'.stderr.txt'),proc.stderr)
    assert proc.returncode==0,(label,proc.returncode,proc.stderr)
    return json.loads(proc.stdout)
def prepare():
    run=OUTPUT/('base-record-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
    path=REPO/'native/main.cpp';raw=path.read_bytes();source=raw.decode('utf-8-sig');before=blocks(source)
    new,inventory=transform(before['record']);newline='\r\n' if '\r\n' in before['record'] else '\n';new=new.replace('\n',newline)
    assert source.count(before['record'])==1;candidate=source.replace(before['record'],new,1);after=blocks(candidate)
    assert before['producer'].replace(before['record'],new,1)==after['producer']
    for key in before:
        if key not in ('callback','producer','record'):assert before[key]==after[key],key
    assert before['callback'].replace(before['record'],new,1)==after['callback']
    assert candidate.replace(new,before['record'],1)==source
    candidateRaw=(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+candidate.encode('utf-8')
    write(run/'baseline-main.cpp',raw);write(run/'candidate-main.cpp',candidateRaw)
    write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(source.splitlines(True),candidate.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')))
    write(run/'generated.hpp',header(before,after));write(run/'json.hpp',(REPO/'deps/json/json.hpp').read_bytes())
    write(run/'fixture.cpp',(REPO/'tools/virtual_sample_record_base_perf_selftest.cpp').read_bytes())
    for key,value in before.items():write(run/('bound-'+key+'.cpp'),value)
    save(run/'field-inventory.json',inventory);save(run/'type-declarations.json',source_types(source,before));save(run/'acceptance-plan.json',PLAN)
    provenance=corpus(run,DEFAULT_ZIP);save(run/'provenance.json',provenance);results=[]
    for profile,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(run/(profile+'.exe'))]
        bat=run/(profile+'-build.bat');write(bat,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n')
        proc=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180)
        write(run/(profile+'-build.log'),proc.stdout+proc.stderr);assert proc.returncode==0,str(run/(profile+'-build.log'))
        r=invoke(run/(profile+'.exe'),run,'verify');results.append({'profile':profile,**r});print(profile+': '+str(r['assertions'])+' assertions passed',flush=True)
    assert path.read_bytes()==raw,'Shared production changed: review before timing'
    binding={'baselineMainSha256':sha(raw),'candidateMainSha256':sha(candidateRaw),'boundBlocks':{k:sha(v) for k,v in before.items()},
             'inputHashes':{p:sha((REPO/p).read_bytes()) for p in ('tools/virtual_sample_record_base_perf_selftest.py','tools/virtual_sample_record_base_perf_selftest.cpp','deps/json/json.hpp')},
             'artifactHashes':{p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file() and p.suffix not in ('.obj',)},
             'exactReverseToBaseline':True,'outsideCandidateBytesUnchanged':True,
             'baselineStandaloneLF':raw.count(b'\n')-raw.count(b'\r\n'),'candidateStandaloneLF':candidateRaw.count(b'\n')-candidateRaw.count(b'\r\n')}
    save(run/'source-binding.json',binding);save(run/'correctness-result.json',{'pass':True,'profiles':results,'provenance':provenance,'productionEdited':False,'formalTimingExecuted':False})
    save(OUTPUT/'latest-prepare.json',{'run':str(run),'pass':True});print(json.dumps({'pass':True,'run':str(run),'samples':provenance['samples'],'formalTimingExecuted':False},ensure_ascii=False),flush=True)
def benchmark(run):
    binding=json.loads((run/'source-binding.json').read_text())
    for name,expected in binding['artifactHashes'].items():assert sha((run/name).read_bytes())==expected,name
    assert sha((REPO/'native/main.cpp').read_bytes())==binding['baselineMainSha256'],'shared production drift'
    assert json.loads((run/'acceptance-plan.json').read_text())==PLAN;results=[]
    for profile in ('Release','Checked'):
        for campaign in (1,2):
            r=invoke(run/(profile+'.exe'),run,'benchmark',campaign);results.append({'profile':profile,**r})
            print(profile+' campaign '+str(campaign)+': '+str(sum(v['pass'] for v in r['rows']))+'/'+str(len(r['rows']))+' rows passed',flush=True)
    accepted=all(row['pass'] for r in results for row in r['rows'])
    save(run/'benchmark-result.json',{'candidateAccepted':accepted,'profiles':results,'plan':PLAN,'wholeProcessClaim':False,'productionEdited':False})
    save(OUTPUT/'result.json',{'candidateAccepted':accepted,'run':str(run),'profiles':results});print(json.dumps({'candidateAccepted':accepted,'run':str(run)}),flush=True)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--benchmark-run',type=Path);args=parser.parse_args()
    if args.benchmark_run:benchmark(args.benchmark_run.resolve())
    else:prepare()
if __name__=='__main__':main()