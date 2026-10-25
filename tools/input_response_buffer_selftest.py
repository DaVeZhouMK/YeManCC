#!/usr/bin/env python3
"""R46 source-bound response allocation candidate; correctness before one formal measurement."""
from pathlib import Path
import argparse,datetime,hashlib,json,math,os,statistics,subprocess,sys
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R46-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def sources():
 result={n:(OUT/n).read_bytes() for n in ['PLAN.json','baseline-declaration.inc','candidate-declaration.inc','receive-body.inc','actual-scanner.inc','request-baseline.cpp','request-candidate.cpp']}
 result['input_host_pipe_transport.h']=(REPO/'native/input_host_pipe_transport.h').read_bytes();result['fixture.cpp']=(REPO/'tools/input_response_buffer_selftest.cpp').read_bytes();result['json.hpp']=(REPO/'deps/json/json.hpp').read_bytes();return result
def prepare():
 inputs=sources();run=OUT/('ready-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 for name,b in inputs.items():(run/name).write_bytes(b)
 (run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes());profiles=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/FAs',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fa'+str(run/(name+'.asm')),'/Fe'+str(run/(name+'.exe'))];batch=run/(name+'-build.bat');batch.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8');p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);log=p.stdout+p.stderr;(run/(name+'-build.log')).write_text(log,encoding='utf-8');assert p.returncode==0,('build',name,str(run));assert 'warning C' not in log and 'error C' not in log,('compile gate',name,str(run))
  p=subprocess.run([str(run/(name+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=45);(run/(name+'-correctness.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-correctness-stderr.log')).write_text(p.stderr,encoding='utf-8');assert p.returncode==0,('correctness',name,p.stderr,str(run));j=json.loads(p.stdout);assert j['pass'];profiles.append({'profile':name,'flags':flags,'exeSha256':sha((run/(name+'.exe')).read_bytes()),'correctnessSha256':sha((run/(name+'-correctness.json')).read_bytes()),'checks':j['checks']});print(name+' correctness '+str(j['checks'])+' checks; formal benchmark NOT run',flush=True)
 assert all(sources()[n]==b for n,b in inputs.items()),'source drift'
 save(run/'manifest.json',{'preparedAt':datetime.datetime.now().astimezone().isoformat(),'inputs':{n:sha(b) for n,b in inputs.items()},'runnerSha256':sha(Path(__file__).read_bytes()),'profiles':profiles,'productionCandidateApplied':False,'measurementExecuted':False});save(OUT/'latest-ready.json',{'pass':True,'run':str(run),'manifestSha256':sha((run/'manifest.json').read_bytes()),'productionApplied':False});print(json.dumps({'pass':True,'ready':str(run),'formalMeasurementExecuted':False}))
def measure(run):
 run=run.resolve();assert run.is_relative_to(OUT.resolve()) and run.name.startswith('ready-');manifest=json.loads((run/'manifest.json').read_text(encoding='utf-8'));assert not(run/'measurement-start.json').exists(),'No retiming';releaseFile=run/'parent-release.json';assert releaseFile.is_file(),'Missing checked parent release';release=json.loads(releaseFile.read_text(encoding='utf-8'));assert release['manifestSha256']==sha((run/'manifest.json').read_bytes()) and release['ownBuildsHeldDuringMeasurement'] and release['correctnessAndIndependentSourceReviewComplete']
 assert sha(Path(__file__).read_bytes())==manifest['runnerSha256'];current=sources();assert all(sha(b)==manifest['inputs'][n] for n,b in current.items()),'input drift before measurement'
 for entry in manifest['profiles']:
  n=entry['profile'];assert sha((run/(n+'.exe')).read_bytes())==entry['exeSha256'];assert sha((run/(n+'-correctness.json')).read_bytes())==entry['correctnessSha256'];assert (run/(n+'-build.log')).read_text(encoding='utf-8').strip()=='fixture.cpp'
 save(run/'measurement-start.json',{'startedAt':datetime.datetime.now().astimezone().isoformat(),'releaseSha256':sha(releaseFile.read_bytes()),'manifestSha256':sha((run/'manifest.json').read_bytes()),'noRetiming':True});results=[];metricRows=0
 for entry in manifest['profiles']:
  n=entry['profile'];print(n+' single FORMAL measurement starting',flush=True)
  with (run/(n+'-performance.json')).open('w',encoding='utf-8') as stdout,(run/(n+'-progress.log')).open('w',encoding='utf-8') as stderr:p=subprocess.run([str(run/(n+'.exe')),'--benchmark'],cwd=run,stdout=stdout,stderr=stderr,timeout=180)
  assert p.returncode==0,('formal execution',n,str(run));j=json.loads((run/(n+'-performance.json')).read_text(encoding='utf-8'));assert j['executionPass'] and len(j['rows'])==10
  for row in j['rows']:
   for metric in ['elapsedNsPerRoundTrip','rawThreadCyclesPerRoundTrip']:
    value=row[metric];b=value['baseline'];c=value['candidate'];assert len(b)==len(c)==11 and all(math.isfinite(x) and x>0 for x in b+c);paired=statistics.median(1-y/x for x,y in zip(b,c));ratio=1-statistics.median(c)/statistics.median(b);fast=sum(y<x for x,y in zip(b,c));passed=(paired>=.05 and ratio>=.05 and fast>=10) if row['affected'] else (paired>=-.05 and ratio>=-.05);assert abs(paired-value['pairedMedianReduction'])<1e-12 and abs(ratio-value['ratioOfMediansReduction'])<1e-12 and fast==value['fasterPairs'] and passed==value['pass'];metricRows+=1
   a=row['baselineAllocation'];c=row['candidateAllocation'];ap=c['requests']<=a['requests'] and c['bytes']<=a['bytes'] and (not row['affected'] or c['requests']<a['requests']);assert ap==row['allocationPass'];assert row['pass']==(ap and row['elapsedNsPerRoundTrip']['pass'] and row['rawThreadCyclesPerRoundTrip']['pass'])
  assert j['performancePass']==all(r['pass'] for r in j['rows']);results.append({'profile':n,**j});print(n+': '+str(sum(r['pass'] for r in j['rows']))+'/10 gates passed',flush=True)
 result={'executionPass':True,'performanceGatePass':all(r['performancePass'] for r in results),'passedRows':sum(row['pass'] for r in results for row in r['rows']),'totalRows':20,'metricRowsRecomputed':metricRows,'profiles':results,'inputsStillMatch':all(sha(b)==manifest['inputs'][n] for n,b in sources().items()),'productionApplied':False,'noRetimingOrThresholdChange':True};save(run/'decision.json',result);save(OUT/'latest-performance.json',{'run':str(run),'executionPass':True,'performanceGatePass':result['performanceGatePass'],'passedRows':result['passedRows'],'productionApplied':False});print(json.dumps({'executionPass':True,'performanceGatePass':result['performanceGatePass'],'passedRows':result['passedRows'],'run':str(run)}))
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--measure-ready-run',type=Path);args=parser.parse_args();measure(args.measure_ready_run) if args.measure_ready_run else prepare()
