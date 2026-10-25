#!/usr/bin/env python3
"""R50 present-invalid optional raw exact frozen actual-parser nonregression measurement, once per prepared run.
Only an isolated SDK-free fixture executes. No product/device operations.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,math,os,statistics,subprocess
ROOT=Path(r'G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R50-20261015/optional-raw-validation')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,o):p.write_text(json.dumps(o,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def median(v):return statistics.median(v)
def main(run):
 run=run.resolve();assert run.is_relative_to(ROOT.resolve()) and run.name.startswith('nonregression-ready-')
 manifest=json.loads((run/'manifest.json').read_text(encoding='utf-8'))
 release=json.loads((run/'parent-release.json').read_text(encoding='utf-8'))
 assert release['manifestSha256']==sha(run/'manifest.json') and release['planSha256']==sha(ROOT/'nonregression-plan.json')
 assert release['ownBuildJobsHeldDuringMeasurement'] and release['sourceReviewComplete'] and release['allCorrectnessJobsTerminal']
 assert release['runnerSha256']==sha(Path(__file__))
 for n,expected in manifest['inputs'].items():assert sha((ROOT if n=='PLAN.json' else run)/n)==expected,n
 for p in manifest['profiles']:
  assert sha(run/p['profile']/'ParserTriggerFixture.dll')==p['dllSha256']
  assert sha(ROOT/'candidate-ready.json')==manifest['candidateReadySha256']
 assert not(run/'measurement-start.json').exists(),'No retiming or repeated formal campaign'
 save(run/'measurement-start.json',{'startedAt':datetime.datetime.now().astimezone().isoformat(),'manifestSha256':sha(run/'manifest.json'),'planSha256':sha(ROOT/'nonregression-plan.json'),'parentReleaseSha256':sha(run/'parent-release.json'),'runnerSha256':sha(Path(__file__)),'fullParserWithInertReleaseConstantOnly':True,'productsDevicesSdkExecuted':False})
 env=dict(os.environ);env['DOTNET_CLI_UI_LANGUAGE']='en-US';env['DOTNET_TieredCompilation']='0';env['DOTNET_TieredPGO']='0'
 results=[];metric_count=0;raw_count=0
 for entry in manifest['profiles']:
  profile=entry['profile'];args=['dotnet',str(run/profile/'ParserTriggerFixture.dll'),'--benchmark',str(run/(profile+'-performance.json'))]
  print(profile+' actual parser FORMAL nonregression starting',flush=True)
  with (run/(profile+'-performance-stdout.log')).open('w',encoding='utf-8') as stdout,(run/(profile+'-performance-stderr.log')).open('w',encoding='utf-8') as stderr:
   process=subprocess.Popen(args,stdout=stdout,stderr=stderr,env=env)
   save(run/(profile+'-running-process.json'),{'pid':process.pid,'args':args,'startedAt':datetime.datetime.now().astimezone().isoformat()})
   code=process.wait(timeout=180)
  save(run/(profile+'-terminal-process.json'),{'pid':process.pid,'exitCode':code,'observedTerminal':True});assert code==0,(profile,code)
  result=json.loads((run/(profile+'-performance.json')).read_text(encoding='utf-8'));assert result['executedFullParser'] and len(result['rows'])==8
  for row in result['rows']:
   for key in ['elapsedNs','threadCycles']:
    m=row[key];a=m['baseline'];b=m['candidate'];assert len(a)==len(b)==11 and all(math.isfinite(v) and v>0 for v in a+b)
    paired=median([1-y/x for x,y in zip(a,b)]);ratio=1-median(b)/median(a);passed=paired>=-.05 and ratio>=-.05
    assert abs(paired-m['pairedMedianReduction'])<1e-12 and abs(ratio-m['ratioOfMediansReduction'])<1e-12 and passed==m['pass']
    metric_count+=1;raw_count+=len(a)+len(b)
   assert len(row['baselineAllocatedBytes'])==len(row['candidateAllocatedBytes'])==11
   allocation=all(y<=x for x,y in zip(row['baselineAllocatedBytes'],row['candidateAllocatedBytes']));assert allocation==row['allocationPass']
   row['rowPass']=row['elapsedNs']['pass'] and row['threadCycles']['pass'] and allocation
  results.append({'profile':profile,**result});print(profile+' completed '+str(sum(r['rowPass'] for r in result['rows']))+'/8 nonregression gates',flush=True)
 decision={'executionPass':True,'nonregressionPass':all(r['rowPass'] for p in results for r in p['rows']),'profiles':results,'metricsRecomputed':metric_count,'rawTimingValuesRecomputed':raw_count,'rowGatesPassed':sum(r['rowPass'] for p in results for r in p['rows']),'rowGatesTotal':16,'sourceInputHashesUnchanged':all(sha((ROOT if n=='PLAN.json' else run)/n)==v for n,v in manifest['inputs'].items()),'candidateProductionApplied':False,'noRetiming':True,'scope':'complete pure production parser, not pipe/SDK/whole application CPU'}
 save(run/'decision.json',decision);save(ROOT/'latest-performance.json',{'run':str(run),'executionPass':True,'nonregressionPass':decision['nonregressionPass'],'passedRows':decision['rowGatesPassed'],'totalRows':16,'productionApplied':False});print(json.dumps({'run':str(run),'nonregressionPass':decision['nonregressionPass'],'passedRows':decision['rowGatesPassed']}))
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run',type=Path,required=True);main(parser.parse_args().run)
