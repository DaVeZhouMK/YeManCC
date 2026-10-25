#!/usr/bin/env python3
"""R22 source-extracted writer ownership candidate; correctness before fixed timing.
Only private fixture files are opened. No product, device, registry, service or input API runs.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,re,subprocess,sys,zipfile
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUTPUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R22-20261015'
ARCHIVE=Path('C:/Users/DaVe/Desktop/日志-侧边栏全开后/YeManCC-logs-20261014-160313--G3E虚拟手柄OK.zip')
ARCHIVE_SHA='e886bcacb06e9250976235f4d1c0dae28c8f8e46902e7a1210f148c4cc01a479'
VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN={'campaigns':2,'rounds':11,'minimumFasterRounds':10,'minimumPairedReduction':.05,'minimumRatioReduction':.05,
      'fewerRequests':True,'fewerRequestedBytes':True,'batchSize':32,'batches':24,'warmupBatches':6,
      'scope':'source-extracted writer PID/dump plus drained-record disposal; excludes actual file I/O and product CPU',
      'decision':'Every preselected affected scenario must pass both profiles and both campaigns. No threshold relaxation.'}
sha=lambda x:hashlib.sha256(x.encode('utf-8') if isinstance(x,str) else x).hexdigest()
def write(p,x):
    p=Path(p).resolve();assert p.is_relative_to(OUTPUT.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(x.encode('utf-8') if isinstance(x,str) else x)
def save(p,x):write(p,json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def blocks(src,owned=False):
    a=src.index('static std::mutex g_inputCaptureSampleQueueMx;');b=src.index(';',src.index('static constexpr size_t kInputCaptureSampleQueueLimit',a))+1
    result={'queueState':src[a:b], 'limit':re.search(r'static constexpr unsigned long long kLogMaxBytes = [^;]+;',src).group(),
            'append':source_function(src,'static void inputCaptureAppend(const json& rec)'),
            'queue':source_function(src,'static void inputCaptureQueueSample(json rec)'),
            'drain':source_function(src,'static void inputCaptureDrainQueuedSamples()')}
    if owned:
        result['impl']='template<class Record>\n'+source_function(src,'static void inputCaptureAppendRecord(Record&& rec)')
        result['owned']=source_function(src,'static void inputCaptureAppendOwned(json&& rec)')
    return result

def transform(src):
    original=source_function(src,'static void inputCaptureAppend(const json& rec)')
    assert original.count('    auto record = rec;')==1
    helper='template<class Record>\n'+original.replace('static void inputCaptureAppend(const json& rec)', 'static void inputCaptureAppendRecord(Record&& rec)',1).replace('    auto record = rec;','    auto record = std::forward<Record>(rec);',1)
    replacement=helper+'''

// Existing lvalue and temporary call sites keep their original copy semantics.
static void inputCaptureAppend(const json& rec) {
    inputCaptureAppendRecord(rec);
}
// The drained queue owns each completed record exclusively; consume it only
// after the unchanged writer-open guards. No producer record is borrowed.
static void inputCaptureAppendOwned(json&& rec) {
    inputCaptureAppendRecord(std::move(rec));
}'''
    drain=source_function(src,'static void inputCaptureDrainQueuedSamples()')
    old='    for (const auto& rec : pending)\r\n        inputCaptureAppend(rec);'
    new='    for (auto& rec : pending)\r\n        inputCaptureAppendOwned(std::move(rec));'
    assert drain.count(old)==1
    out=src.replace(original,replacement,1).replace(drain,drain.replace(old,new,1),1)
    assert out.replace(replacement,original,1).replace(drain.replace(old,new,1),drain,1)==src
    return out

def kernel(body,owned):
    start=body.index('    auto record = ');end=body.index('    const auto line = record.dump();',start)+len('    const auto line = record.dump();')
    signature=('template<class Record>\nstatic std::string writerKernel(Record&& rec)' if owned else 'static std::string writerKernel(const json& rec)')
    return signature+' {\n'+body[start:end]+'\n    return line;\n}\n'

def namespace(parts,name,owned):
    out='namespace '+name+''' {
static std::mutex g_inputCaptureMx;
static std::wstring g_inputCapturePath;
static std::ofstream g_inputCaptureLog;
static uint64_t g_inputCaptureBytes=0;
static uint32_t g_inputCaptureSampleWrites=0;
'''
    order=['queueState','limit']+(['impl'] if owned else [])+['append']+(['owned'] if owned else [])+['queue','drain']
    out+='\n'.join(parts[k] for k in order)
    out+='''
static void fixtureFamilyCall(double gx,double gy,double gz,double ax,double ay,double az) {
    inputCaptureQueueSample({{"t",(unsigned long long)GetTickCount64()},{"kind","controller-family-imu"},
        {"dps",{{"x",gx},{"y",gy},{"z",gz}}},{"g",{{"x",ax},{"y",ay},{"z",az}}}});
}
'''
    out+=kernel(parts['impl'] if owned else parts['append'],owned)+'}\n'
    return out

def corpus(run):
    archive_bytes=ARCHIVE.read_bytes();assert sha(archive_bytes)==ARCHIVE_SHA
    rows=[]
    with zipfile.ZipFile(ARCHIVE) as z:
        for name in ['input-capture.jsonl.prev','input-capture.jsonl']:
            for n,raw in enumerate(z.read(name).splitlines(),1):
                try:v=json.loads(raw)
                except (ValueError,UnicodeDecodeError):continue
                if not isinstance(v,dict) or v.get('kind')!='sample':continue
                rows.append({'name':name+':'+str(n),'payload':v,'rawLineSha256':sha(raw)})
    assert len(rows)==2301
    large=max(rows,key=lambda r:len(json.dumps(r['payload'],ensure_ascii=False)))
    selected=[rows[0],large,rows[-1],{'name':'synthetic-controller-family-imu','payload':{'t':777,'kind':'controller-family-imu','dps':{'x':1.25,'y':-2.5,'z':3.75},'g':{'x':.125,'y':-.25,'z':1.0}}}]
    save(run/'all-samples.json',rows);save(run/'selected.json',selected)
    return {'archive':str(ARCHIVE),'archiveSha256':ARCHIVE_SHA,'allSampleCount':len(rows),'selectedNames':[x['name'] for x in selected], 'selectionRule':'first, maximum serialized payload length, last authorized sample, explicit synthetic family record'}

def compile_verify(run,old,new,main_raw,mode):
    write(run/'production-extracted.h',namespace(old,'oldqueue',False)+namespace(new,'newqueue',True))
    write(run/'fixture.cpp',(REPO/'tools/virtual_sample_writer_perf_selftest.cpp').read_bytes())
    for rel in ['deps/json/json.hpp','native/log_frequency_policy.h']:
        write(run/Path(rel).name,(REPO/rel).read_bytes())
    results=[]
    for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
        bat=run/(name+'-build.bat');write(bat,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\nexit /b %errorlevel%\n')
        p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180)
        write(run/(name+'-build.log'),p.stdout+p.stderr)
        if p.returncode:raise RuntimeError('compile failed '+name+'; '+str(run/(name+'-build.log')))
        result=invoke(run,name,'verify')
        results.append({'profile':name,**result});print(name+': '+str(result['assertions'])+' assertions passed',flush=True)
    save(run/'correctness-result.json',{'pass':True,'profiles':results,'mode':mode,'productionEdited':False,'wholeProcessClaim':False})
    return results

def invoke(run,profile,mode,campaign=None):
    args=[str(run/(profile+'.exe')),mode,str(run),str(run/(profile+'-verify-files'))]
    if campaign is not None:args.append(str(campaign))
    p=subprocess.run(args,cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=300)
    stem=profile+'-'+mode+('' if campaign is None else '-'+str(campaign))
    write(run/(stem+'.stdout.json'),p.stdout);write(run/(stem+'.stderr.log'),p.stderr)
    if p.returncode and not(mode=='benchmark' and p.returncode==2):raise RuntimeError(stem+' failed: '+p.stderr)
    return json.loads(p.stdout)

def prepare():
    run=OUTPUT/('writer-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
    raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig');candidate=transform(src)
    candidate_raw=(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+candidate.encode('utf-8')
    write(run/'baseline-main.cpp',raw);write(run/'candidate-main.cpp',candidate_raw)
    write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(src.splitlines(True),candidate.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')))
    old,new=blocks(src),blocks(candidate,True)
    for k in ['queue','queueState','limit']:assert old[k]==new[k]
    # Every statement in the file writer other than the value-category transfer is identical.
    assert new['impl'].removeprefix('template<class Record>\n').replace('inputCaptureAppendRecord(Record&& rec)','inputCaptureAppend(const json& rec)').replace('std::forward<Record>(rec)','rec')==old['append']
    save(run/'acceptance-plan.json',PLAN);provenance=corpus(run);save(run/'provenance.json',provenance)
    compile_verify(run,old,new,raw,'candidate')
    assert (REPO/'native/main.cpp').read_bytes()==raw,'production drift during candidate proof'
    save(run/'source-binding.json',{'baselineMainSha256':sha(raw),'candidateMainSha256':sha(candidate_raw),'baselineBlocks':{k:sha(v) for k,v in old.items()},'candidateBlocks':{k:sha(v) for k,v in new.items()},'unrelatedProductionBytesUnchanged':True,'mainUnchanged':True})
    save(OUTPUT/'latest-prepare.json',{'pass':True,'run':str(run)});print(json.dumps({'pass':True,'run':str(run),'sourceEdited':False}))

def benchmark(run):
    assert json.loads((run/'acceptance-plan.json').read_text())==PLAN
    binding=json.loads((run/'source-binding.json').read_text())
    assert sha((REPO/'native/main.cpp').read_bytes())==binding['baselineMainSha256'],'production drift before timing'
    results=[]
    for name in ['Release','Checked']:
        for campaign in [1,2]:
            result=invoke(run,name,'benchmark',campaign);results.append({'profile':name,**result})
            print(name+' campaign '+str(campaign)+': '+str(sum(x['pass'] for x in result['rows']))+'/'+str(len(result['rows']))+' passed',flush=True)
    save(run/'benchmark-result.json',{'candidateAccepted':all(x['pass'] for x in results),'profiles':results,'plan':PLAN,'wholeProcessClaim':False,'fileIOTimed':False,'productionEdited':False})
    print(json.dumps({'candidateAccepted':all(x['pass'] for x in results),'run':str(run)}))

def rebase(run):
    binding=json.loads((run/'source-binding.json').read_text())
    old_raw=(run/'baseline-main.cpp').read_bytes();raw=(REPO/'native/main.cpp').read_bytes()
    assert sha(old_raw)==binding['baselineMainSha256']
    old=blocks(old_raw.decode('utf-8-sig'));current=blocks(raw.decode('utf-8-sig'))
    assert old==current,'Bound production writer/queue/drain changed; fresh correctness needed'
    candidate=transform(raw.decode('utf-8-sig'))
    candidate_raw=(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+candidate.encode('utf-8')
    generated=namespace(current,'oldqueue',False)+namespace(blocks(candidate,True),'newqueue',True)
    assert generated.encode('utf-8')==(run/'production-extracted.h').read_bytes(),'Compiled fixture changed'
    history=run/('rebase-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    for name in ['baseline-main.cpp','candidate-main.cpp','candidate-UNAPPLIED.patch','source-binding.json']:
        write(history/name,(run/name).read_bytes())
    save(history/'rebase-proof.json',{'fromMainSha256':sha(old_raw),'toMainSha256':sha(raw),'allBoundBlocksUnchanged':True,'generatedCompiledHeaderUnchanged':True})
    write(history/'unrelated-changes.diff',''.join(difflib.unified_diff(old_raw.decode('utf-8-sig').splitlines(True),raw.decode('utf-8-sig').splitlines(True),fromfile='tested-baseline',tofile='shared-current')))
    write(run/'baseline-main.cpp',raw);write(run/'candidate-main.cpp',candidate_raw)
    write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(raw.decode('utf-8-sig').splitlines(True),candidate.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')))
    binding.update(baselineMainSha256=sha(raw),candidateMainSha256=sha(candidate_raw),rebaseProof=str(history/'rebase-proof.json'))
    save(run/'source-binding.json',binding);print(json.dumps({'rebasePass':True,'boundSourceUnchanged':True,'run':str(run)}))

def final(accepted):
    run=OUTPUT/('current-source-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir(parents=True)
    raw=(REPO/'native/main.cpp').read_bytes();baseline=(accepted/'baseline-main.cpp').read_bytes()
    src=raw.decode('utf-8-sig');before=baseline.decode('utf-8-sig');expected=transform(before)
    assert src==expected,'unexpected production difference; rebase and re-review first'
    assert json.loads((accepted/'benchmark-result.json').read_text())['candidateAccepted']
    provenance=corpus(run);save(run/'provenance.json',provenance)
    compile_verify(run,blocks(before),blocks(src,True),raw,'final-current-source')
    assert (REPO/'native/main.cpp').read_bytes()==raw,'production drift during final proof'
    save(run/'source-binding.json',{'currentMainSha256':sha(raw),'acceptedCandidateMainSha256':sha((accepted/'candidate-main.cpp').read_bytes()),'byteIdentical':raw==(accepted/'candidate-main.cpp').read_bytes(),'acceptedRun':str(accepted),'fixtures':{p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file() and p.suffix in ['.cpp','.h','.hpp','.exe']}})
    save(OUTPUT/'latest-final.json',{'pass':True,'run':str(run)});print(json.dumps({'pass':True,'run':str(run)}))

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--benchmark-run',type=Path);ap.add_argument('--final-run',type=Path);ap.add_argument('--rebase-run',type=Path);args=ap.parse_args()
    if args.benchmark_run:benchmark(args.benchmark_run)
    elif args.final_run:final(args.final_run)
    elif args.rebase_run:rebase(args.rebase_run)
    else:prepare()
