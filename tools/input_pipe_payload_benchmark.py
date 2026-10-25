#!/usr/bin/env python3
"""R47 compile-only by default. Explicit released ready run performs one formal campaign set."""
from pathlib import Path
import argparse,datetime,hashlib,json,math,os,statistics,subprocess,sys
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R47-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def current_inputs():return {'baseline.h':(OUT/'baseline.h').read_bytes(),'candidate.h':(OUT/'candidate.h').read_bytes(),'PLAN.json':(OUT/'PLAN.json').read_bytes(),'fixture.cpp':(REPO/'tools/input_pipe_payload_benchmark.cpp').read_bytes(),'json.hpp':(REPO/'deps/json/json.hpp').read_bytes()}
def check_correctness(candidate):
 readyPath=OUT/'payload-candidate/candidate-ready.json'
 ready=json.loads(readyPath.read_text(encoding='utf-8-sig'))
 assert ready['pass'] and ready['allJobsTerminal'] is True
 assert ready['baselineSha256']==sha((OUT/'baseline.h').read_bytes())
 assert ready['candidateSha256']==sha(candidate)
 root=readyPath.parent.resolve()
 for relative,expected in ready['artifactHashes'].items():
  artifact=(root/relative).resolve()
  assert artifact.is_relative_to(root),'artifact escaped candidate directory'
  assert sha(artifact.read_bytes())==expected,('correctness artifact changed',relative)
 assert sha((REPO/'native/input_host_pipe_transport.h').read_bytes())==ready['baselineSha256'],'production baseline changed'
 return {'candidateReadySha256':sha(readyPath.read_bytes()),'run':ready['run'],'artifactsVerified':len(ready['artifactHashes']),'allJobsTerminal':True}
def compile_ready():
 inputs=current_inputs();correctness=check_correctness(inputs['candidate.h']);run=OUT/('benchmark-ready-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 for name,b in inputs.items():(run/name).write_bytes(b)
 (run/'baseline_transport.h').write_bytes(inputs['baseline.h']);(run/'candidate_transport.h').write_bytes(inputs['candidate.h']);(run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes());profiles=[]
 for profile,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(run/(profile+'.exe'))];batch=run/(profile+'-build.bat');batch.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8');p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(run/(profile+'-build.log')).write_text(p.stdout+p.stderr,encoding='utf-8');assert p.returncode==0,(profile,'compile',str(run));assert 'warning C' not in p.stdout+p.stderr,('build warning',profile,str(run));profiles.append({'profile':profile,'flags':flags,'exeSha256':sha((run/(profile+'.exe')).read_bytes())});print(profile+' benchmark compiled; NOT measured',flush=True)
 manifest={'compiledAt':datetime.datetime.now().astimezone().isoformat(),'inputs':{n:sha(b) for n,b in inputs.items()},'profiles':profiles,'runnerSha256':sha(Path(__file__).read_bytes()),'correctness':correctness,'formalMeasurementReleased':False,'productionEdited':False};save(run/'manifest.json',manifest);save(OUT/'latest-benchmark-ready.json',{'run':str(run),'manifestSha256':sha((run/'manifest.json').read_bytes()),'measurementExecuted':False});print(json.dumps({'compiled':True,'run':str(run),'measured':False}))
def measure(run):
 run=run.resolve();assert run.is_relative_to(OUT.resolve()) and run.name.startswith('benchmark-ready-');manifest=json.loads((run/'manifest.json').read_text(encoding='utf-8'));assert not(run/'measurement-start.json').exists(),'formal run was already started; no retiming'
 release_file=run/'parent-release.json'
 assert release_file.is_file(),'Missing reviewed parent-release.json; no measurement may start'
 release=json.loads(release_file.read_text(encoding='utf-8'));assert release['manifestSha256']==sha((run/'manifest.json').read_bytes()) and release['planSha256']==manifest['inputs']['PLAN.json'],'release identity mismatch'
 assert release['allCorrectnessAndSealProbeJobsTerminal'] is True and release['ownBuildJobsHeldUntilMeasurementEnds'] is True,'release does not hold build work'
 inputs=current_inputs();assert all(sha(b)==manifest['inputs'][n] for n,b in inputs.items()),'current input differs from ready';assert sha(Path(__file__).read_bytes())==manifest['runnerSha256'];correctness=check_correctness(inputs['candidate.h']);assert correctness==manifest['correctness']
 for profile in manifest['profiles']:assert sha((run/(profile['profile']+'.exe')).read_bytes())==profile['exeSha256']
 save(run/'measurement-start.json',{'startedAt':datetime.datetime.now().astimezone().isoformat(),'manifestSha256':sha((run/'manifest.json').read_bytes()),'parentReleasedAfterAllOwnBuilds':True,'standaloneNoProductExecution':True,'noRetiming':True,'planSha256':manifest['inputs']['PLAN.json']})
 results=[];verifiedMetrics=0
 for entry in manifest['profiles']:
  profile=entry['profile'];print(profile+' FORMAL measurement starting; all build work held',flush=True)
  with (run/(profile+'-result.json')).open('w',encoding='utf-8') as stdout,(run/(profile+'-progress.log')).open('w',encoding='utf-8') as stderr:
   p=subprocess.run([str(run/(profile+'.exe'))],cwd=run,stdout=stdout,stderr=stderr,timeout=180)
  assert p.returncode==0,(profile,'execution failed',str(run));result=json.loads((run/(profile+'-result.json')).read_text(encoding='utf-8'));assert result['executionPass'] and len(result['rows'])==10
  for row in result['rows']:
   for metric in ['elapsedNsPerOperation','threadCyclesPerOperation']:
    value=row[metric];b=value['baseline'];c=value['candidate'];assert len(b)==len(c)==11 and all(math.isfinite(x) and x>0 for x in b+c)
    paired=statistics.median([1-y/x for x,y in zip(b,c)]);ratio=1-statistics.median(c)/statistics.median(b);faster=sum(y<x for x,y in zip(b,c));passed=(faster>=10 and paired>=.05 and ratio>=.05) if row['affected'] else (paired>=-.05 and ratio>=-.05)
    assert abs(paired-value['pairedMedianReduction'])<1e-12 and abs(ratio-value['ratioOfMediansReduction'])<1e-12 and faster==value['fasterRounds'] and passed==value['pass'];verifiedMetrics+=1
   a=row['baselineAllocation'];c=row['candidateAllocation'];assert a['iterations']==c['iterations']==32
   alloc=all(c[k]<=a[k] for k in ['allocationRequests','requestedBytes','eventCreates']);events=c['eventCreates']<=a['eventCreates'];assert alloc==row['allocationPass'] and events==row['eventCreatePass'];assert row['pass']==(alloc and events and row['elapsedNsPerOperation']['pass'] and row['threadCyclesPerOperation']['pass'])
  assert result['performancePass']==all(row['pass'] for row in result['rows']);results.append({'profile':profile,**result});print(profile+' FORMAL completed: '+str(sum(r['pass'] for r in result['rows']))+'/10 row gates passed',flush=True)
 overall=all(r['performancePass'] for r in results);decision={'executionPass':True,'performanceGatePass':overall,'candidateMayBeConsideredForAdoption':overall,'profiles':results,'metricRowsRecomputed':verifiedMetrics,'totalRowGates':20,'passedRowGates':sum(row['pass'] for r in results for row in r['rows']),'productionCandidateApplied':False,'noRetimingOrThresholdRelaxation':True,'scope':'actual isolated Runtime+same-thread-peer roundtrip, not whole-app CPU or real Host/device','inputsStillMatch':all(sha(b)==manifest['inputs'][n] for n,b in current_inputs().items())};save(run/'decision.json',decision);save(OUT/'latest-performance.json',{'run':str(run),'executionPass':True,'performanceGatePass':overall,'passedRowGates':decision['passedRowGates'],'totalRowGates':20,'productionApplied':False});print(json.dumps({'run':str(run),'executionPass':True,'performanceGatePass':overall,'passedRows':decision['passedRowGates'],'productionApplied':False}))
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--measure-ready-run',type=Path);args=parser.parse_args();measure(args.measure_ready_run) if args.measure_ready_run else compile_ready()
