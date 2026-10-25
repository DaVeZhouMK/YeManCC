#!/usr/bin/env python3
"""R23 bounded fingerprint allocation candidate. Prepare/prove first, time separately.
Full metadata delta uses actual production builders and receipt recipe; leaves and
persona snapshot are fixture data, not a hardware reconstruction.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_metadata_perf_selftest import extract
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUTPUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R23-20261015'
VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN={'campaigns':2,'rounds':11,'minimumFasterRounds':10,'minimumPairedReduction':.05,'minimumRatioReduction':.05,'fewerRequests':True,'fewerRequestedBytes':True,'batchSize':32,'batches':24,'warmupBatches':6,'shapes':['synthetic-CG3EM-shape','synthetic-Unicode-shape'],'modes':['stable','selector-alternating','config-alternating','both-alternating'],'scopes':['fingerprints','full-metadata-delta'],'decision':'All rows in both profiles and campaigns must pass; no threshold relaxation. No production edits before acceptance.'}
sha=lambda x:hashlib.sha256(x.encode('utf-8') if isinstance(x,str) else x).hexdigest()
def write(p,x):
 p=Path(p).resolve();assert p.is_relative_to(OUTPUT.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(x.encode('utf-8') if isinstance(x,str) else x)
def save(p,x):write(p,json.dumps(x,ensure_ascii=False,indent=2)+'\n')
FIELDS=['dmiManufacturer','dmiProduct','dmiSystemProduct','selectorId','deviceClass','gyroMatrixId','accelMatrixId','matrixSourceSha256']
def transform(src):
 a=src.index('        const std::string selectorFp = ');b=src.index(';',a)+1;old=src[a:b]
 new='        std::string selectorFp;\n        selectorFp.reserve(34u +\n'
 new+=' +\n'.join('            '+' + '.join('g_hcMotionSelector.'+field+'.size()' for field in FIELDS[i:i+2]) for i in range(0,8,2))+');\n'
 for field in FIELDS:new+='        selectorFp += g_hcMotionSelector.'+field+"; selectorFp += '\\x1f';\n"
 new+='''        selectorFp += g_hcMotionSelector.resolved ? "1" : "0";
        selectorFp += '\\x1f';
        selectorFp += sensorMeta.gyroHcLegacyFields ? "legacy-gyro" : "base-gyro";
        selectorFp += '\\x1f';
        selectorFp += sensorMeta.accelHcLegacyFields ? "legacy-accel" : "base-accel";'''
 if '\r\n' in old:new=new.replace('\n','\r\n')
 marker='        std::string testLaneCfgFp;'
 reserve='''
        testLaneCfgFp.reserve(12u + sampleConfiguredPersona.size() +
            g_realStickTest.motionMode.size() + g_realStickTest.motionTrigger.size() +
            g_realStickTest.targetStick.size() + gyroProvider.size() + accelProvider.size());'''
 if '\r\n' in src[src.index(marker):src.index(marker)+len(marker)+2]:reserve=reserve.replace('\n','\r\n')
 assert src.count(marker)==1
 out=src.replace(old,new,1).replace(marker,marker+reserve,1)
 assert out.replace(new,old,1).replace(marker+reserve,marker,1)==src
 return out

def fp_block(delta):
 a=delta.index('        '+('const std::string selectorFp' if 'const std::string selectorFp' in delta else 'std::string selectorFp'))
 b=delta.index('        if (selectorFp != ',a)
 c=delta.index('        std::string testLaneCfgFp;',b);d=delta.index('        if (testLaneCfgFp != ',c)
 return delta[a:b]+delta[c:d]
ALIASES='''    Active active(ctx);
    const auto& g_hcMotionSelector=ctx.selector;
    const auto& g_realStickTest=ctx.config;
    const auto& sensorMeta=ctx.meta;
    const auto& gyroProvider=ctx.gyroProvider;
    const auto& accelProvider=ctx.accelProvider;
'''
def header(src,candidate):
 old,new=extract(src,True),extract(candidate,True)
 for key in ['selector','config','personaSnapshot']:assert old[key]==new[key]
 receipt=source_function(src,'static json inputCaptureCandidateMatrixReceipt(')
 receipt=receipt.replace('static json inputCaptureCandidateMatrixReceipt(','static json productionReceipt(',1)
 a=receipt.index('{')+1
 receipt=receipt[:a]+'''\n    const auto& g_machineIdentity=activeContext->machine;
    const auto& g_hcMotionSelector=activeContext->selector;
'''+receipt[a:]
 out='#include "msi_motion_capabilities.h"\n'+receipt+'\nstatic json inputCaptureCandidateMatrixReceipt(const Meta& meta) { ++receiptReads; return productionReceipt(meta); }\n'
 out=out.replace('const InputCaptureSensorMeta& meta','const Meta& meta')
 for name,parts in [('baseline',old),('candidate',new)]:
  delta=parts['delta'].replace('static std::string g_lastSampleSelectorFp;','auto& g_lastSampleSelectorFp=state.selector;').replace('static std::string g_lastSampleTestLaneCfgFp;','auto& g_lastSampleTestLaneCfgFp=state.config;')
  out+='static NOINLINE json '+name+'Delta(const Context& ctx,State& state) {\n'+ALIASES+'    json rec=json::object();\n'+parts['personaSnapshot']+'\n'+parts['selector']+'\n'+parts['config']+'\n'+delta+'\n    return rec;\n}\n'
  out+='static NOINLINE std::pair<std::string,std::string> '+name+'Fingerprints(const Context& ctx) {\n'+ALIASES+parts['personaSnapshot']+'\n'+fp_block(parts['delta'])+'\n    return {std::move(selectorFp),std::move(testLaneCfgFp)};\n}\n'
 return out,old,new

def invoke(run,name,mode,campaign=None):
 args=[str(run/(name+'.exe')),mode]
 if campaign is not None:args.append(str(campaign))
 p=subprocess.run(args,cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=300)
 stem=name+'-'+mode+('' if campaign is None else '-'+str(campaign));write(run/(stem+'.stdout.json'),p.stdout);write(run/(stem+'.stderr.log'),p.stderr)
 if p.returncode and not(mode=='benchmark' and p.returncode==2):raise RuntimeError(stem+': '+p.stderr)
 return json.loads(p.stdout)

def build_verify(run,src,candidate):
 gen,old,new=header(src,candidate);write(run/'generated.hpp',gen)
 write(run/'fixture.cpp',(REPO/'tools/virtual_metadata_fingerprint_perf_selftest.cpp').read_bytes())
 for rel in ['deps/json/json.hpp','native/msi_motion_capabilities.h']:write(run/Path(rel).name,(REPO/rel).read_bytes())
 profiles=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  bat=run/(name+'-build.bat');write(bat,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\nexit /b %errorlevel%\n')
  p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);write(run/(name+'-build.log'),p.stdout+p.stderr)
  if p.returncode:raise RuntimeError('compile failed '+name+': '+str(run/(name+'-build.log')))
  result=invoke(run,name,'verify');profiles.append({'profile':name,**result});print(name+': '+str(result['assertions'])+' assertions passed',flush=True)
 save(run/'correctness-result.json',{'pass':True,'profiles':profiles,'hardwareOrProductExecuted':False,'wholeProcessClaim':False,'fixtureLeavesNotFieldReconstruction':True})
 return old,new

def prepare():
 run=OUTPUT/('fingerprints-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig');candidate=transform(src);candidate_raw=(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+candidate.encode('utf-8')
 write(run/'baseline-main.cpp',raw);write(run/'candidate-main.cpp',candidate_raw);write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(src.splitlines(True),candidate.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')));save(run/'acceptance-plan.json',PLAN)
 old,new=build_verify(run,src,candidate)
 assert raw==(REPO/'native/main.cpp').read_bytes(),'shared source drift during proof'
 save(run/'source-binding.json',{'baselineMainSha256':sha(raw),'candidateMainSha256':sha(candidate_raw),'baselineBlocks':{k:sha(v) for k,v in old.items()},'candidateBlocks':{k:sha(v) for k,v in new.items()},'outsideTwoFingerprintConstructionSpansUnchanged':True,'productionEdited':False})
 save(OUTPUT/'latest-prepare.json',{'pass':True,'run':str(run)});print(json.dumps({'pass':True,'run':str(run),'productionEdited':False}))

def benchmark(run):
 assert json.loads((run/'acceptance-plan.json').read_text())==PLAN
 binding=json.loads((run/'source-binding.json').read_text());assert sha((REPO/'native/main.cpp').read_bytes())==binding['baselineMainSha256']
 profiles=[]
 for name in ['Release','Checked']:
  for c in [1,2]:
   result=invoke(run,name,'benchmark',c);profiles.append({'profile':name,**result});print(name+' campaign '+str(c)+': '+str(sum(x['pass'] for x in result['rows']))+'/'+str(len(result['rows']))+' passed',flush=True)
 result={'candidateAccepted':all(p['pass'] for p in profiles),'profiles':profiles,'plan':PLAN,'productionEdited':False,'wholeProcessClaim':False};save(run/'benchmark-result.json',result);print(json.dumps({'candidateAccepted':result['candidateAccepted'],'run':str(run)}))

def final(accepted):
 run=OUTPUT/('current-source-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();base=(accepted/'baseline-main.cpp').read_bytes()
 assert raw==(accepted/'candidate-main.cpp').read_bytes() and raw.decode('utf-8-sig')==transform(base.decode('utf-8-sig'))
 assert json.loads((accepted/'benchmark-result.json').read_text())['candidateAccepted']
 build_verify(run,base.decode('utf-8-sig'),raw.decode('utf-8-sig'))
 assert raw==(REPO/'native/main.cpp').read_bytes()
 save(run/'source-binding.json',{'currentSourceSha256':sha(raw),'acceptedCandidateSourceSha256':sha((accepted/'candidate-main.cpp').read_bytes()),'exactCandidateIdentity':True,'acceptedRun':str(accepted)})
 save(OUTPUT/'latest-final.json',{'pass':True,'run':str(run)});print(json.dumps({'pass':True,'run':str(run)}))

if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--benchmark-run',type=Path);ap.add_argument('--final-run',type=Path);args=ap.parse_args()
 if args.benchmark_run:benchmark(args.benchmark_run)
 elif args.final_run:final(args.final_run)
 else:prepare()
