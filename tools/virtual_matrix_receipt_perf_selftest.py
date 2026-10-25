#!/usr/bin/env python3
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,re,subprocess,sys,zipfile
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[1];OUTPUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R21-20261015';VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
ARCHIVE=Path('C:/Users/DaVe/Desktop/日志-侧边栏全开后/YeManCC-logs-20261014-160313--G3E虚拟手柄OK.zip');ARCHIVE_SHA='e886bcacb06e9250976235f4d1c0dae28c8f8e46902e7a1210f148c4cc01a479'
PLAN={'campaigns':2,'rounds':11,'minimumFasterRounds':10,'minimumPairedReduction':.05,'minimumRatioReduction':.05,'fewerRequests':True,'fewerBytes':True,'batchSize':32,'batches':24,'warmupBatches':6,'scopes':['receipt','hc-object','receipt-dump','hc-dump'],'decision':'Every selected MSI G3E scenario and scope must pass in both profiles and campaigns.'}
sha=lambda b:hashlib.sha256(b.encode('utf-8') if isinstance(b,str) else b).hexdigest()
# Use the established balanced-brace source helpers without importing a tool as a package.
helper_path=REPO/'tools/virtual_sample_tree_perf_selftest.py';ns={'__file__':str(helper_path),'__name__':'r21_helpers'};exec(helper_path.read_text(encoding='utf-8'),ns)
match_brace,source_function=ns['match_brace'],ns['source_function']
def write(p,d):p=Path(p);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(d.encode('utf-8') if isinstance(d,str) else d)
def save(p,v):write(p,json.dumps(v,ensure_ascii=False,indent=2)+'\n')
def function_block(text,signature):return source_function(text,signature)
def hc_block(text):
    start=text.index('        json hcCandidateObj = {');close=match_brace(text,text.index('{',start));assert text[close+1]==';';return text[start:close+2]
def transform_receipt(src):
    # Transform only the two JSON initializer sites inside the current receipt function.
    a=src.index('        return json{{');close=match_brace(src,src.index('{',a));old_component=src[a:close+2]
    new_component='''        json componentObj = json::object();
        componentObj["id"] = id;
        componentObj["sourceSha256"] = digest;
        componentObj["sourceScope"] = legacy ? "hc-1.3.3.1-resource-json" : baseScope;
        componentObj["provider"] = family;
        componentObj["legacyFieldOverride"] = legacy;
        componentObj["validationScope"] = "declared-source-recipe;physical-axis-not-claimed";
        return componentObj;'''
    out=src.replace(old_component,new_component,1)
    a=out.index('    return {{"gyro",component(true)');close=match_brace(out,out.index('{',a));old_outer=out[a:close+2]
    new_outer='''    json result = json::object();
    result["gyro"] = component(true);
    result["accel"] = component(false);
    result["admissionIndependent"] = true;
    return result;'''
    out=out.replace(old_outer,new_outer,1)
    assert out!=src
    return out,old_component,old_outer
def make_header(receipt,receipt_candidate,hc, hc_candidate):
    baseline=receipt
    candidate=receipt_candidate.replace('static json inputCaptureCandidateMatrixReceipt(', 'static json candidateMatrixReceipt(',1)
    text='''#include "msi_motion_capabilities.h"
'''+'''struct MachineIdentity { std::string deviceClass,product,gyroMatrixId,accelMatrixId; };
struct HcMotionSelectorReceipt { bool resolved=false; std::string matrixSourceSha256; };
struct InputCaptureSensorMeta { std::string gyroFamily,accelFamily; bool gyroHcLegacyFields=false,accelHcLegacyFields=false; };
struct Input { MachineIdentity machine; HcMotionSelectorReceipt selector; InputCaptureSensorMeta meta; json expected; };
static thread_local MachineIdentity g_machineIdentity;
static thread_local HcMotionSelectorReceipt g_hcMotionSelector;
'''+baseline+'\n'+candidate+'\n'
    b_hc=hc
    c_hc=hc_candidate.replace('inputCaptureCandidateMatrixReceipt(sensorMeta)','candidateMatrixReceipt(sensorMeta)')
    for name,block in [('baseline',b_hc),('candidate',c_hc)]:
        text+='static NOINLINE json '+name+'Hc(const Input& v) {\n'
        text+='    const auto& g_hcMotionSelector=v.selector; const auto& sensorMeta=v.meta;\n'
        text+='    const double hcCandidateGx=1.25,hcCandidateGy=-2.5,hcCandidateGz=3.75,hcCandidateAx=-4.5,hcCandidateAy=5.5,hcCandidateAz=-6.5;\n'
        text+=block+'\n    return hcCandidateObj;\n}\n'
    return text
def build_cases(run):
    rows=[]
    with zipfile.ZipFile(ARCHIVE) as z:
        assert sha(ARCHIVE.read_bytes())==ARCHIVE_SHA
        refs=[]
        for name in ('input-capture.jsonl.prev','input-capture.jsonl'):
            for line_no,line in enumerate(z.read(name).splitlines(),1):
                try:r=json.loads(line)
                except (ValueError,UnicodeDecodeError):continue
                if r.get('kind')!='sample':continue
                receipt=r['hcMatrixCandidate']['recipeReceipt']
                rows.append({'name':f'{name}:{line_no}','case':{'deviceClass':'HandheldCompanion.Devices.ClawCG3EM','product':'MS-1T91','gyroMatrixId':'hc-06c0b954:msi-claw-sub:gyro:x-y-neg-z','accelMatrixId':'hc-06c0b954:msi-claw-a1m:accel:neg-x-neg-z-y','matrixSourceSha256':'hc-06c0b954:msi-claw-sub:gyro:x-y-neg-z','meta':{'gyroFamily':receipt['gyro']['provider'],'accelFamily':receipt['accel']['provider'],'gyroHcLegacyFields':receipt['gyro']['legacyFieldOverride'],'accelHcLegacyFields':receipt['accel']['legacyFieldOverride']},'expected':receipt}})
    assert len(rows)==2301
    save(run/'all-cases.json',rows)
    selected=[rows[0],rows[-1],max(rows,key=lambda x:len(json.dumps(x))),next(x for x in rows if x['case']['meta']['gyroHcLegacyFields'] or x['case']['meta']['accelHcLegacyFields'])]
    save(run/'selected-cases.json',selected)
    return {'archive':str(ARCHIVE),'archiveSha256':sha(ARCHIVE.read_bytes()),'sampleCount':len(rows),'selectedCount':len(selected),'activeEntries':['input-capture.jsonl.prev','input-capture.jsonl'],'hardwareEvidenceOnly':True}
def invoke(exe,run,mode,campaign=None):
    args=[str(exe),mode,str(run/'selected-cases.json'),str(run/'all-cases.json')]
    if campaign is not None:args.append(str(campaign))
    p=subprocess.run(args,cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=300)
    stem=exe.stem+'-'+mode+('' if campaign is None else '-'+str(campaign));write(run/(stem+'.stdout.json'),p.stdout);write(run/(stem+'.stderr.log'),p.stderr)
    if p.returncode!=0:raise RuntimeError(stem+': '+p.stderr)
    return json.loads(p.stdout)
def prepare():
    run=OUTPUT/('receipt-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
    main=REPO/'native/main.cpp';raw=main.read_bytes();source=raw.decode('utf-8-sig');receipt=function_block(source,'static json inputCaptureCandidateMatrixReceipt(');hc=hc_block(source);candidate_receipt,old_component,old_outer=transform_receipt(receipt);candidate=source.replace(receipt,candidate_receipt,1)
    assert source.count(receipt)==1 and candidate.replace(candidate_receipt,receipt,1)==source
    candidate_hc=hc.replace('inputCaptureCandidateMatrixReceipt(sensorMeta)','candidateMatrixReceipt(sensorMeta)')
    write(run/'baseline-main.cpp',raw);write(run/'candidate-main.cpp',(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+candidate.encode('utf-8'));write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(source.splitlines(True),candidate.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')))
    write(run/'receipt.baseline.cpp',receipt);write(run/'receipt.candidate.cpp',candidate_receipt);write(run/'hc.baseline.cpp',hc);write(run/'hc.candidate.cpp',candidate_hc);write(run/'generated.hpp',make_header(receipt,candidate_receipt,hc,candidate_hc));write(run/'baseline-hc.inl',hc);write(run/'candidate-hc.inl',candidate_hc);write(run/'fixture.cpp',(REPO/'tools/virtual_matrix_receipt_perf_selftest.cpp').read_bytes());write(run/'json.hpp',(REPO/'deps/json/json.hpp').read_bytes());write(run/'msi_motion_capabilities.h',(REPO/'native/msi_motion_capabilities.h').read_bytes());save(run/'acceptance-plan.json',PLAN);provenance=build_cases(run);save(run/'provenance.json',provenance)
    profiles=[]
    for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))];bat=run/(name+'-build.bat');write(bat,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n');p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);write(run/(name+'-build.log'),p.stdout+p.stderr)
        if p.returncode!=0:raise RuntimeError('compile '+name+' failed')
        r=invoke(run/(name+'.exe'),run,'verify');profiles.append({'profile':name,**r});print(name+': '+str(r['assertions'])+' assertions passed',flush=True)
    save(run/'source-binding.json',{'baselineMainSha256':sha(raw),'candidateMainSha256':sha((run/'candidate-main.cpp').read_bytes()),'receiptBaselineSha256':sha(receipt),'receiptCandidateSha256':sha(candidate_receipt),'hcBaselineSha256':sha(hc),'hcCandidateSha256':sha(candidate_hc),'currentMainUnchanged':sha(main.read_bytes())==sha(raw),'onlyReceiptCallChanged':candidate_hc.replace('candidateMatrixReceipt','inputCaptureCandidateMatrixReceipt')==hc,'sourceTokenScope':'receipt function only'})
    save(run/'correctness-result.json',{'pass':True,'profiles':profiles,'provenance':provenance,'formalTimingExecuted':False,'productionEdited':False});save(OUTPUT/'latest-prepare.json',{'run':str(run),'pass':True});print(json.dumps({'pass':True,'run':str(run),'sampleCount':provenance['sampleCount'],'formalTimingExecuted':False}))
def benchmark(run):
    run=Path(run);assert json.loads((run/'acceptance-plan.json').read_text())==PLAN;assert json.loads((run/'source-binding.json').read_text())['currentMainUnchanged'];allresults=[]
    for profile in ('Release','Checked'):
        for c in (1,2):
            r=invoke(run/(profile+'.exe'),run,'benchmark',c);allresults.append({'profile':profile,**r});print(profile+' campaign '+str(c)+': '+str(sum(x['pass'] for x in r['rows']))+'/'+str(len(r['rows']))+' rows passed',flush=True)
    accepted=all(x['pass'] for r in allresults for x in r['rows']);save(run/'benchmark-result.json',{'candidateAccepted':accepted,'profiles':allresults,'plan':PLAN,'productionEdited':False,'wholeProcessClaim':False});save(OUTPUT/'result.json',{'candidateAccepted':accepted,'run':str(run),'profiles':allresults});print(json.dumps({'candidateAccepted':accepted,'run':str(run)}))
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--benchmark-run',type=Path);a=ap.parse_args();benchmark(a.benchmark_run) if a.benchmark_run else prepare()
