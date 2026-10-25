#!/usr/bin/env python3
"""Offline R35 cost attribution from the single authorized MSI G3E capture.
Reads ZIP members as data; never extracts files or invokes product/device APIs.
"""
from pathlib import Path
from collections import defaultdict
import argparse,datetime,hashlib,json,statistics,zipfile
ROOT=Path(__file__).resolve().parents[1]
ARCHIVE=Path(r'C:/Users/DaVe/Desktop/日志-侧边栏全开后/YeManCC-logs-20261014-160313--G3E虚拟手柄OK.zip')
EXPECTED='e886bcacb06e9250976235f4d1c0dae28c8f8e46902e7a1210f148c4cc01a479'
OUT=ROOT.parents[2]/'Build/Validation/VirtualInputPerf-R35-20261015'
def digest(data):return hashlib.sha256(data).hexdigest()
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def stats(values):
 if not values:return {'n':0}
 return {'n':len(values),'mean':sum(values)/len(values),'median':statistics.median(values),'min':min(values),'max':max(values)}
def aggregate(rows,key):
 selected=[x[key] for x in rows if isinstance(x.get(key),dict) and isinstance(x[key].get('n'),int) and x[key]['n']>0]
 n=sum(x['n'] for x in selected)
 return {'activeWindows':len(selected),'samples':n,'weightedRoundedMeanUs':sum(x.get('meanUs',0)*x['n'] for x in selected)/n if n else None,'maxUs':max((x.get('maxUs',0) for x in selected),default=None)}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=OUT);a=p.parse_args();out=a.output.resolve()
 if not out.is_relative_to(OUT.resolve()):raise ValueError('Output must remain under R35 evidence')
 raw=ARCHIVE.read_bytes()
 if digest(raw)!=EXPECTED:raise ValueError('Authorized archive hash differs')
 out.mkdir(parents=True,exist_ok=True);native=defaultdict(list);host=defaultdict(list);members=[];pidcost=0;legacy=[]
 with zipfile.ZipFile(ARCHIVE) as z:
  manifest=z.read('YeManCC-export-manifest.txt').decode('utf-8-sig',errors='replace')
  wanted={'exportedAtLocal','processId','appVersion','domainLogsVirtual','nativeSha256','hostSha256','hostExeSha256','hostDllSha256','nativeBuildId','hostBuildId'}
  identity={k:v for line in manifest.splitlines() if '=' in line for k,v in [line.split('=',1)] if k in wanted or k.lower().endswith('sourcehash')}
  for info in z.infolist():
   if not (info.filename.startswith('virtual-gamepad.log') or info.filename=='input-host-cost.log'):continue
   content=z.read(info);members.append({'member':info.filename,'bytes':len(content),'sha256':digest(content)})
   for number,line in enumerate(content.decode('utf-8-sig',errors='replace').splitlines(),1):
    try:row=json.loads(line)
    except ValueError:continue
    if not isinstance(row,dict):continue
    ref={'member':info.filename,'line':number,'time':row.get('time',row.get('t'))}
    if row.get('event')=='input-capture-tick-cost':
     native[str(row.get('writerPid','unknown'))].append({'row':row,'ref':ref})
     st=row.get('stages',{});el=st.get('tickPreambleUs',{});cy=st.get('preambleCpuUs',{})
     if el.get('n',0)>0 and el.get('n')==cy.get('n') and cy.get('meanUs',0)>el.get('meanUs',0):legacy.append({'writerPid':row.get('writerPid'),'ref':ref,'sameReportedN':el['n'],'elapsedMeanUs':el.get('meanUs'),'legacyCycleEstimateMeanUs':cy.get('meanUs')})
    if row.get('event')=='input-idle-window' and isinstance(row.get('nativeProcessCost'),dict):pidcost+=1
    if info.filename=='input-host-cost.log' and 'frames' in row:
     u=row.get('usbipTransport') if isinstance(row.get('usbipTransport'),dict) else {};tuplekey=tuple(str(u.get(k,'unknown')) for k in ['hostPid','hostInstanceId','runId','epoch','powerGeneration','persona'])
     host[tuplekey].append({'row':row,'ref':ref})
 summaries=[]
 for pid,entries in sorted(native.items()):
  rows=[e['row'] for e in entries];stagekeys=sorted({k for x in rows for k in x.get('stages',{})});readkeys=sorted({k for x in rows for k in x.get('gpRead',{})});resolvekeys=sorted({k for x in rows for k in x.get('gpResolve',{})})
  reads={}
  for source in readkeys:
   values=[x.get('gpRead',{}).get(source,{}) for x in rows];n=sum(x.get('n',0) for x in values)
   reads[source]={'calls':n,'weightedRoundedMeanUs':sum(x.get('meanUs',0)*x.get('n',0) for x in values)/n if n else None,'slotApiCalls':sum(x.get('slotReads',0) for x in values),'discoveryPasses':sum(x.get('discoveryPasses',0) for x in values),'dirtyPasses':sum(x.get('discoveryDirtyPasses',0) for x in values),'purposes':{f'{api}.{purpose}':sum(x.get(api,{}).get(purpose,0) for x in values) for api in ['canonical','secret'] for purpose in ['discovery','guard','realtime']}}
  resolves={}
  for source in resolvekeys:
   values=[x.get('gpResolve',{}).get(source,{}) for x in rows];n=sum(x.get('n',0) for x in values)
   resolves[source]={'calls':n,'cacheHits':sum(x.get('cacheHits',0) for x in values),'cacheMisses':sum(x.get('cacheMisses',0) for x in values),'weightedRoundedMeanUs':sum(x.get('meanUs',0)*x.get('n',0) for x in values)/n if n else None}
  chronological=sorted(entries,key=lambda e:str(e['ref']['time']))
  summaries.append({'writerPid':pid,'windows':len(rows),'range':[chronological[0]['ref']['time'],chronological[-1]['ref']['time']],'ticksPerSecond':stats([x['ticksPerSecond'] for x in rows if isinstance(x.get('ticksPerSecond'),(int,float))]),'stages':{k:aggregate([x.get('stages',{}) for x in rows],k) for k in stagekeys},'physicalReads':reads,'sourceResolve':resolves,'cadenceEligibleWindows':sum(105<=x.get('ticksPerSecond',0)<=135 and x.get('skipped',{}).get('busySkip',0)==0 for x in rows),'cadenceEligibilityIsNotSteadyStateOrPersonaProof':True})
 hosts=[]
 for key,entries in sorted(host.items()):
  rows=[e['row'] for e in entries];hosts.append({'embeddedTuple':dict(zip(['hostPid','hostInstanceId','runId','epoch','powerGeneration','persona'],key)),'rows':len(rows),'frames':stats([x['frames'] for x in rows]),'rawProcessCpuMsPerSec':stats([x['procCpuMsPerSec'] for x in rows if isinstance(x.get('procCpuMsPerSec'),(int,float))]),'allocationBytesPerSec':stats([x['allocBytesPerSec'] for x in rows if isinstance(x.get('allocBytesPerSec'),(int,float))]),'cpuObservationStateCounts':{s:sum(x.get('cpuObservation',{}).get('state','legacy-unclassified')==s for x in rows) for s in sorted({x.get('cpuObservation',{}).get('state','legacy-unclassified') for x in rows})},'stages':{field:stats([x.get(field,{}).get('mean') for x in rows if isinstance(x.get(field,{}).get('mean'),(int,float))]) for field in ['jsonParseUs','hidSubmitUs','pipeReadUs']},'tupleUnknown':any(v=='unknown' for v in key)})
 result={'schemaVersion':1,'createdAt':datetime.datetime.now().astimezone().isoformat(),'archive':str(ARCHIVE),'archiveSha256':EXPECTED,'onlyAuthorizedArchive':True,'members':members,'exportIdentity':identity,'nativeTickWindows':sum(len(x) for x in native.values()),'nativeByPid':summaries,'nativeProcessCostWindows':pidcost,'hostCostWindows':sum(len(x) for x in host.values()),'hostByEmbeddedTuple':hosts,'legacyCycleEstimateExceedsElapsedAtEqualReportedN':{'windows':len(legacy),'examples':legacy[:8],'meaning':'Consistency warning for calibration/population/aggregation; not proof of actual CPU time or source blame'},'currentSource':{n:digest((ROOT/n).read_bytes()) for n in ['native/main.cpp','InputHost/Program.cs']},'logMatchesCurrentSourceProven':False,'limits':['Archived binary identity is not current source identity; these are prioritization observations, not current optimization acceptance.','Stage means are rounded and counters are not an atomic snapshot; no subtraction/summation into exclusive CPU.','gpRead covers physical read, while tickPreamble also covers pumps/reconcile/UI setup; neither is whole Native process CPU.','Cycles-to-us are estimates; an estimate above wall duration is not positive proof of CPU consumption.','No Native process CPU windows were synthesized when missing.','Host CPU states absent from legacy records remain legacy-unclassified.','No device, product, SDK, service, driver or registry operation; no performance candidate measured.']}
 save(out/'cost-frontier.json',result);print(json.dumps({'nativeTickWindows':result['nativeTickWindows'],'nativePids':len(summaries),'nativeProcessCostWindows':pidcost,'hostCostWindows':result['hostCostWindows'],'legacyCycleVsElapsedWarnings':len(legacy),'source':result['currentSource'],'result':str(out/'cost-frontier.json')},ensure_ascii=False))
if __name__=='__main__':main()
