#!/usr/bin/env python3
"""R26 frame receipt JSON construction: prepare read-only, verify, then time.
Only the initial nine-field diagnostic JSON initializer is a production candidate.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,subprocess,sys,zipfile
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function,match_brace,pairs
REPO=Path(__file__).resolve().parents[1];OUTPUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R26-20261015'
ARCHIVE=Path('C:/Users/DaVe/Desktop/日志-侧边栏全开后/YeManCC-logs-20261014-160313--G3E虚拟手柄OK.zip');ARCHIVE_SHA='e886bcacb06e9250976235f4d1c0dae28c8f8e46902e7a1210f148c4cc01a479'
VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN={'campaigns':2,'rounds':11,'minimumFasterRounds':10,'minimumPairedReduction':.05,'minimumRatioReduction':.05,'fewerRequests':True,'fewerRequestedBytes':True,'batchSize':32,'batches':24,'warmupBatches':6,'scenarios':['archived-first-receipt','archived-last-receipt','synthetic-Unicode-receipt'],'scopes':['receipt-initial','receipt-updated','frameProof-move','frameProof-dump'],'decision':'Every affected row in both profiles and campaigns must pass before production changes. Diagnostic gate false is correctness-only and unchanged.'}
sha=lambda x:hashlib.sha256(x.encode('utf-8') if isinstance(x,str) else x).hexdigest()
def write(p,x):
 p=Path(p).resolve();assert p.is_relative_to(OUTPUT.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(x.encode('utf-8') if isinstance(x,str) else x)
def save(p,x):write(p,json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def parts(src):
 fn=source_function(src,'static json inputHostSubmitPad(')
 a=fn.index('            if(msiTargetIntentEvidenceEnabled()) {');e=match_brace(fn,fn.index('{',a))+1;gate=fn[a:e]
 ia=gate.index('commonFrameReceipt={{');ib=match_brace(gate,gate.index('{',ia))+2;initial=gate[ia:ib]
 a=fn.index('            if(!commonFrameReceipt.is_null()) {');e=match_brace(fn,fn.index('{',a))+1;update=fn[a:e]
 a=fn.index('    json frameProof = json::object();');e=fn.index('    json result = json::object();',a);proof=fn[a:e]
 helper=source_function(src,'static const char* inputCaptureMotionProofId(')
 return {'gate':gate,'initial':initial,'update':update,'proof':proof,'helper':helper,'submit':fn}
def transformed_initial(initial):
 a=initial.index('{');expr=initial[a:-1];fields=pairs(expr);assert len(fields)==9
 result='commonFrameReceipt=json::object();'
 for k,v in fields:result+='\n                    commonFrameReceipt['+k+'] = '+v+';'
 if '\r\n' in initial:result=result.replace('\n','\r\n')
 return result

def transform(src):
 p=parts(src);new=transformed_initial(p['initial']);assert src.count(p['initial'])==1
 out=src.replace(p['initial'],new,1);assert out.replace(new,p['initial'],1)==src
 return out

def header(src,candidate):
 # The candidate no longer contains the old initializer marker, so bind other
 # blocks from the baseline and substitute the single measured candidate span.
 p=parts(src);gateNew=p['gate'].replace(p['initial'],transformed_initial(p['initial']),1)
 assert candidate==src.replace(p['initial'],transformed_initial(p['initial']),1)
 out='''struct Input {
    std::string run="fixture-run",instance="fixture-host",persona="steamdeck",selectorId="fixture-selector";
    unsigned long long epoch=2,command=4,beforeSample=1,afterSample=1,tick=777;
    bool enabled=true,accepted=true,effUseMotion=false,standalone=false,throwPersona=false;
    json expected;
};
static thread_local const Input* active;
static thread_local unsigned long long g_inputHostEpoch=0,g_inputHostSequence=0,g_inputHostSampleSequence=0,g_commonFrameEvidenceFailures=0;
static thread_local std::string g_inputHostRunId,g_inputHostHostInstanceId;
struct Config { bool standaloneTestLane=false; }; struct Selector { std::string selectorId; };
static thread_local Config g_realStickTest; static thread_local Selector g_hcMotionSelector;
static thread_local unsigned g_personaCalls=0,g_proofCalls=0,g_gateCalls=0;
static bool msiTargetIntentEvidenceEnabled(){++g_gateCalls;return active->enabled;}
static std::string inputHostPersona(){++g_personaCalls;if(active->throwPersona)throw std::runtime_error("fixture-persona-failed");return active->persona;}
static unsigned long long GetTickCount64(){return active->tick;}
static void bind(const Input& v){active=&v;g_inputHostRunId=v.run;g_inputHostHostInstanceId=v.instance;g_inputHostEpoch=v.epoch;g_inputHostSequence=v.command;g_inputHostSampleSequence=v.beforeSample;g_realStickTest.standaloneTestLane=v.standalone;g_hcMotionSelector.selectorId=v.selectorId;g_personaCalls=g_proofCalls=g_gateCalls=0;g_commonFrameEvidenceFailures=0;}
'''
 helper=p['helper'];a=helper.index('{')+1;helper=helper[:a]+'\n    ++g_proofCalls;'+helper[a:];out+=helper+'\n'
 for name,gate in [('baseline',p['gate']),('candidate',gateNew)]:
  out+='static NOINLINE json '+name+'(const Input& in,int scope) {\n    bind(in);\n    const bool accepted=in.accepted,effUseMotion=in.effUseMotion;\n    json commonFrameReceipt=nullptr;\n'+gate+'\n    if(scope==0)return commonFrameReceipt;\n    g_inputHostSampleSequence=in.afterSample;\n'+p['update']+'\n    if(scope==1)return commonFrameReceipt;\n'+p['proof']+'\n    return frameProof;\n}\n'
 return out,p

def corpus(run):
 raw=ARCHIVE.read_bytes();assert sha(raw)==ARCHIVE_SHA;rows=[]
 with zipfile.ZipFile(ARCHIVE) as z:
  for name in ['virtual-gamepad.log.1','virtual-gamepad.log','native-lifecycle.log.1','native-lifecycle.log']:
   if name not in z.namelist():continue
   for ln,line in enumerate(z.read(name).splitlines(),1):
    try:r=json.loads(line)
    except (ValueError,UnicodeDecodeError):continue
    proof=r.get('frameProof');receipt=proof.get('hostReceipt') if isinstance(proof,dict) else None
    if isinstance(receipt,dict):rows.append({'file':name,'line':ln,'rawLineSha256':sha(line),'receipt':receipt})
 assert len(rows)==918
 save(run/'receipts.json',rows);save(run/'provenance.json',{'archive':str(ARCHIVE),'archiveSha256':ARCHIVE_SHA,'receiptRows':len(rows),'scope':'Observed post-request receipts; pre-request sample uses the same observed value as a fixture seam, no original pre-request sample or live mutex/Host is reconstructed.'})

def prepare():
 run=OUTPUT/('frame-receipt-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig');candidate=transform(src);newraw=(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+candidate.encode('utf-8')
 gen,p=header(src,candidate)
 write(run/'baseline-main.cpp',raw);write(run/'candidate-main.cpp',newraw);write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(src.splitlines(True),candidate.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')))
 write(run/'generated.hpp',gen);write(run/'fixture.cpp',(REPO/'tools/virtual_frame_receipt_perf_selftest.cpp').read_bytes());write(run/'json.hpp',(REPO/'deps/json/json.hpp').read_bytes());corpus(run);save(run/'acceptance-plan.json',PLAN)
 save(run/'source-binding.json',{'baselineMainSha256':sha(raw),'candidateMainSha256':sha(newraw),'baselineBlocks':{k:sha(v) for k,v in p.items()},'generatedSha256':sha(gen),'outsideInitializerBytesUnchanged':True,'productionEdited':False})
 save(OUTPUT/'latest-prepare.json',{'run':str(run),'prepared':True,'correctnessExecuted':False});print(json.dumps({'run':str(run),'prepared':True,'correctnessExecuted':False}))

def invoke(run,profile,mode,campaign=None):
 args=[str(run/(profile+'.exe')),mode,str(run/'receipts.json')]
 if campaign is not None:args.append(str(campaign))
 p=subprocess.run(args,cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=300)
 stem=profile+'-'+mode+('' if campaign is None else '-'+str(campaign));write(run/(stem+'.stdout.json'),p.stdout);write(run/(stem+'.stderr.log'),p.stderr)
 if p.returncode and not(mode=='benchmark' and p.returncode==2):raise RuntimeError(stem+': '+p.stderr)
 return json.loads(p.stdout)

def verify(run):
 profiles=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  bat=run/(name+'-build.bat');write(bat,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\nexit /b %errorlevel%\n')
  p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);write(run/(name+'-build.log'),p.stdout+p.stderr)
  if p.returncode:raise RuntimeError('compile '+name+' failed: '+str(run/(name+'-build.log')))
  result=invoke(run,name,'verify');profiles.append({'profile':name,**result});print(name+': '+str(result['assertions'])+' assertions passed',flush=True)
 save(run/'correctness-result.json',{'pass':True,'profiles':profiles,'wholeProcessClaim':False,'hardwareOrProductExecuted':False})

def benchmark(run):
 assert json.loads((run/'acceptance-plan.json').read_text())==PLAN and json.loads((run/'correctness-result.json').read_text())['pass']
 binding=json.loads((run/'source-binding.json').read_text());assert sha((REPO/'native/main.cpp').read_bytes())==binding['baselineMainSha256']
 results=[]
 for profile in ['Release','Checked']:
  for c in [1,2]:
   r=invoke(run,profile,'benchmark',c);results.append({'profile':profile,**r});print(profile+' campaign '+str(c)+': '+str(sum(x['pass'] for x in r['rows']))+'/'+str(len(r['rows']))+' passed',flush=True)
 out={'candidateAccepted':all(r['pass'] for r in results),'profiles':results,'plan':PLAN,'productionEdited':False,'wholeProcessClaim':False};save(run/'benchmark-result.json',out);print(json.dumps({'candidateAccepted':out['candidateAccepted'],'run':str(run)}))

def final(accepted):
 run=OUTPUT/('current-source-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();base=(accepted/'baseline-main.cpp').read_bytes()
 assert json.loads((accepted/'benchmark-result.json').read_text())['candidateAccepted']
 assert raw==(accepted/'candidate-main.cpp').read_bytes() and raw.decode('utf-8-sig')==transform(base.decode('utf-8-sig'))
 gen,_=header(base.decode('utf-8-sig'),raw.decode('utf-8-sig'))
 assert gen.encode('utf-8')==(accepted/'generated.hpp').read_bytes()
 write(run/'generated.hpp',gen);write(run/'fixture.cpp',(REPO/'tools/virtual_frame_receipt_perf_selftest.cpp').read_bytes());write(run/'json.hpp',(REPO/'deps/json/json.hpp').read_bytes());corpus(run)
 assert (run/'fixture.cpp').read_bytes()==(accepted/'fixture.cpp').read_bytes()
 verify(run)
 assert raw==(REPO/'native/main.cpp').read_bytes()
 save(run/'source-binding.json',{'currentMainSha256':sha(raw),'acceptedMainSha256':sha((accepted/'candidate-main.cpp').read_bytes()),'exactCandidateByteIdentity':True,'compiledBlocksIdenticalToMeasured':True,'acceptedRun':str(accepted)})
 save(OUTPUT/'latest-final.json',{'pass':True,'run':str(run)});print(json.dumps({'pass':True,'run':str(run)}))

if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--verify-run',type=Path);ap.add_argument('--benchmark-run',type=Path);ap.add_argument('--final-run',type=Path);a=ap.parse_args()
 if a.verify_run:verify(a.verify_run)
 elif a.benchmark_run:benchmark(a.benchmark_run)
 elif a.final_run:final(a.final_run)
 else:prepare()
