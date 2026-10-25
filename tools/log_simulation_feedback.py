"""Read-only presence/field feedback for LOG-SIM-01..05. Never replays an archive.

Only named log members and enumerated evidence fields are retained. No settings,
Steam/game logs, input samples, credentials, commands or user actions are read.
This is evidence coverage, never a product or physical acceptance verdict.
"""
from __future__ import annotations
from pathlib import Path,PurePosixPath
import argparse,collections,datetime,hashlib,json,re,stat,zipfile
from log_lane_audit import canonical_log_name,event_name
MAX_ARCHIVE=128*1024*1024;MAX_MEMBERS=2048;MAX_MEMBER=8*1024*1024;MAX_TOTAL=32*1024*1024;MAX_LINE=64*1024;MAX_ROWS=50000;MAX_EVIDENCE=32
HASH_KEYS={'nativeSha256':'appVersionOrHash','hostDllSha256':'hostSha256OrUnknown','fanBackendDllSha256':'backendSha256OrUnknown','runtimeManifestSha256':'runtimeManifestSha256OrUnknown'}
MEMBERS={'native-lifecycle.log','native-detail.log','fan-lifecycle.log','fan-api.log','fan-host-fault.log','yeman-fan-host-runtime.log','sleep-facts.log','virtual-gamepad.log','gyro-motion.log','input-host-cost.log'}
NODES={
 'LOG-SIM-01':{'title':'版本、机型身份和能力路由','events':{'machine-identity','host.identity','host.instance-started','startup.handshake','api.response','hc.initialization.failure','hc.fan-dispatch.begin','fan.capability.result'},'expectedSoftwareState':'Version tuple and actual model/route are observations; startup or HTTP200 does not prove writes.'},
 'LOG-SIM-02':{'title':'风扇完整控制周期与归还','events':{'api.response','api.request-failure','api.transport-failure','hc.fan-dispatch.begin','hc.fan-dispatch.callback-returned','hc.fan-dispatch.callback-failure','fan.capability.result','restore.success','restore.failure','restore.close-hc-success','restore.deferred-hc-timeout','restore.close-hc-already-retired','power.suspend-preclose-release-decision'},'expectedSoftwareState':'Intent, transport/callback return, cleanup settlement and physical response remain separate.'},
 'LOG-SIM-03':{'title':'真实睡眠、唤醒和恢复','events':{'power-broadcast','kernel-power','kernel-power-506','s0-wake-message','s4-wake-message','fan-host-resume-observe','fan-host-resume-native-trigger','fan-host-resume-attempt','power.transition-accepted','power.transition-complete','power.transition-failure','wake-input-chain-first-frame','wake-input-chain-sustained','sleep-steam-wake-readiness'},'expectedSoftwareState':'Retain per-stream event order and generations; software resume is not a physical sleep/input result.'},
 'LOG-SIM-04':{'title':'风扇与灯光共享设备、切换和冲突','events':{'gamepad-setLed','fan.capability.result','hc.fan-dispatch.begin','hc.fan-dispatch.callback-returned','restore.close-hc-success','api.request-failure'},'expectedSoftwareState':'Coexistence needs same-device/owner correlation; nearby timestamps do not prove common ownership.'},
 'LOG-SIM-05':{'title':'输入、IMU与CPU功耗','events':{'input-host-cost-window','host.runtime-sample','input-host-frame-evidence','input-idle-window','gyro-status-second','sensor-initialised','sensorapi-status','gamepad-controller-wake','gamepad-controller-sleep','wake-input-chain-first-frame','wake-input-chain-sustained'},'expectedSoftwareState':'Source validity and CPU scope stay explicit; no key/button payloads; one process is not whole-product CPU.'},
}
BOOLS={'ok','supported','receiptValid','bodyCompleteObserved','bodyComplete','responseReceived','hardwareCapable','hardwareWritesEnabled','hardwareWritesObserved','mockZeroHardwareEvidence','fanRouteWriteReady','hcCloseCleanupPending','hcVirtualCloseReturned','hcDeviceManagerStopCompleted','hcDeviceManagerStopInvoked','oemRestoreConfirmed','oemPhysicalOwnershipConfirmed','physicalEffectVerified','live','readContextStable','snapshotKnown'}
NUMBERS={'status','generation','powerGeneration','resumePhaseGeneration','epoch','elapsedMs','durationMs','probes','attempts','retryAfterMs','observedTick','sampleSequence','readCompletionAgeMs','observationSchemaVersion','windowMs','procCpuMsPerSec','cpuPercent'}
ENUMS={'path':{'/api/handshake','/api/open','/api/open-events','/api/enable','/api/curve','/api/preset','/api/disable','/api/restore','/api/close','/api/acquire-control','/api/release-control','/api/shutdown','/api/cancel-control-intent','/api/suspend','/api/resume'},'provider':{'hc','HC','backend'},'hostMode':{'real-hc-unavailable','real-hc-authorized','real-hc-read-only','safe-no-hardware','mock-handshake','real-hc-fault-locked'},'snapshotReason':{'state-change','periodic'},'physicalObservation':{'not-observed','unverified','unknown'},'inputSource':{'xinput','msi-dinput','hc','unknown'},'state':{'Ready','Resuming','Suspending','Suspended','Closed','Closing','FaultLocked','Unknown','Disabled'},'hcDeviceManagerLifecycle':{'Started','Stopped','Stopping','Unknown'},'hcManagerCleanupEvidence':{'post-close-no-active-manager-observed'},'errorCode':{'HC_CLOSE_PENDING','FAN_DEVICE_GATE_DENIED','FAN_HTTP_RESULT_UNKNOWN','FAN_API_RESPONSE_INVALID','HC_OPERATION_TIMEOUT','HC_RESUME_SUPERSEDED'}}
SAFE_MEMBERS={'YeManCC-export-manifest.txt',*MEMBERS}
def unique(pairs):
 result={}
 for k,v in pairs:
  if k in result:raise ValueError('duplicate-key')
  result[k]=v
 return result

def audit_archive(path:Path,expected:dict|None=None):
 before=path.stat()
 if before.st_size>MAX_ARCHIVE:raise ValueError('ARCHIVE_SIZE_LIMIT')
 with path.open('rb') as stream:sha=hashlib.file_digest(stream,'sha256').hexdigest()
 result={'schemaVersion':1,'scope':'ordinary-log-field-presence-only;not-device-acceptance','readOnly':True,'commandsExecuted':0,'logArchivePath':str(path.resolve()),'logArchiveSha256':sha,'timeZoneOffset':None,'modelEvidence':[],'versionTuple':{v:'unknown' for v in HASH_KEYS.values()},'versionTupleScope':'export-manifest-disk;not-loaded-module-proof;not-atomic-with-logs','expectedVersionTuple':dict(expected or {}),'diagnostics':[],'nodes':[]}
 captures={k:[] for k in NODES};counts=collections.Counter();eventsSeen=collections.defaultdict(set);timeRanges=collections.defaultdict(dict);manifest={};budgets={'bytes':0,'rows':0};seen=set();correlation=collections.defaultdict(set)
 def issue(code,member=None,line=None):
  if len(result['diagnostics'])<64:result['diagnostics'].append({'code':code,**({'member':member} if member else {}),**({'line':line} if line else {})})
 with zipfile.ZipFile(path) as z:
  infos=z.infolist()
  if len(infos)>MAX_MEMBERS:raise ValueError('MEMBER_COUNT_LIMIT')
  selected=[]
  for info in infos:
   normalized=info.filename.replace('\\','/');parts=PurePosixPath(normalized).parts
   if normalized.startswith('/') or any(p in ('..','.') or ':' in p for p in parts) or info.flag_bits&1:raise ValueError('UNSAFE_MEMBER')
   mode=(info.external_attr>>16)&0xf000
   if mode not in (0,stat.S_IFREG,stat.S_IFDIR):raise ValueError('NON_REGULAR_MEMBER')
   if not parts or info.is_dir():continue
   name=parts[-1];base=canonical_log_name(name)
   # Only root log members; never import similarly named Steam or nested files.
   if len(parts)!=1 or base not in SAFE_MEMBERS:continue
   if name in seen:raise ValueError('DUPLICATE_EVIDENCE_MEMBER')
   seen.add(name)
   if info.file_size>MAX_MEMBER or info.file_size>max(1,info.compress_size)*200:issue('MEMBER_READ_LIMIT',name);continue
   selected.append((name,base,info))
  priority={name:i for i,name in enumerate(['YeManCC-export-manifest.txt','native-lifecycle.log','fan-host-fault.log','yeman-fan-host-runtime.log','fan-lifecycle.log','fan-api.log','sleep-facts.log','virtual-gamepad.log','input-host-cost.log','gyro-motion.log','native-detail.log'])}
  selected.sort(key=lambda item:(priority.get(item[1],99),item[0]!=item[1],item[0]))
  for name,base,info in selected:
   if budgets['rows']>=MAX_ROWS:issue('TOTAL_ROW_LIMIT');break
   if budgets['bytes']+info.file_size>MAX_TOTAL:issue('TOTAL_READ_LIMIT');break
   budgets['bytes']+=info.file_size
   try:
    with z.open(info) as stream:
     if name=='YeManCC-export-manifest.txt':
      for raw in stream:
       if len(raw)>MAX_LINE:issue('MANIFEST_LINE_LIMIT',name);continue
       line=raw.decode('utf-8-sig',errors='strict').strip();key,sep,value=line.partition('=')
       if sep and key in {*HASH_KEYS,'hostPayloadManifestSha256','timeZoneBiasMinutes','appVersion','processId'}:
        if key in manifest:issue('DUPLICATE_MANIFEST_FIELD',name)
        else:manifest[key]=value
      continue
     for line_no in range(1,MAX_ROWS+1):
      raw=stream.readline(MAX_LINE+1)
      if not raw:break
      budgets['rows']+=1
      if budgets['rows']>MAX_ROWS:issue('TOTAL_ROW_LIMIT');break
      if len(raw)>MAX_LINE:issue('LINE_SIZE_LIMIT',name,line_no);break
      try:r=json.loads(raw,object_pairs_hook=unique,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('nonfinite')))
      except (ValueError,UnicodeError):issue('PARTIAL_OR_INVALID_RECORD',name,line_no);continue
      if not isinstance(r,dict):continue
      event=event_name(r)
      costWindow=base=='input-host-cost.log' and type(r.get('windowMs')) in (int,float)
      if costWindow:event='input-host-cost-window'
      nodes=[k for k,v in NODES.items() if event in v['events']]
      if not nodes:continue
      details=r.get('details');details=details if isinstance(details,dict) else {}
      if event in {'api.response','api.request-failure','api.transport-failure'}:
       route=details.get('path')
       if route=='/api/handshake':nodes=['LOG-SIM-01']
       elif route in {'/api/open','/api/open-events','/api/enable','/api/curve','/api/preset','/api/disable','/api/restore','/api/close','/api/acquire-control','/api/release-control','/api/shutdown','/api/cancel-control-intent'}:nodes=['LOG-SIM-02']
       elif route in {'/api/suspend','/api/resume'}:nodes=['LOG-SIM-02','LOG-SIM-03']
       else:continue  # Periodic state/heartbeat is not an action boundary.
      if event=='machine-identity':
       machine={'member':name,'line':line_no,'fieldScope':'native-record-only;not-automatically-bound-to-a-Host-cohort','values':{}}
       writer=r.get('writerPid',r.get('pid'))
       if type(writer) is int and 0<writer<2**32:machine['writerProcessId']=writer
       machine['matchesExportNativePid']=str(writer)==manifest.get('processId') if manifest.get('processId') else None
       machine['processBirthBindingProven']=False
       for field in ['manufacturer','systemProduct','product','deviceClass','family']:
        v=r.get(field)
        if isinstance(v,str) and 0<len(v)<=128 and not any(ord(c)<32 for c in v):machine['values'][field]=v
       if machine['values'] and len(result['modelEvidence'])<16:result['modelEvidence'].append(machine)
      item={'member':name,'line':line_no,'event':event,'fields':{}}
      if costWindow:item.update({'eventProvenance':'schema-derived-cost-row-discriminator;not-literal-product-event','cpuScope':'one-InputHost-process-only;not-logger-or-whole-product','processCpuRateKnown':type(r.get('procCpuMsPerSec')) in (int,float) and r['procCpuMsPerSec']>=0 and r['windowMs']>0})
      if event=='host.runtime-sample':item['cpuScope']='Host-runtime-counter-as-recorded;units-require-producer-contract'
      if event=='input-idle-window' and isinstance(r.get('nativeProcessCost'),dict):
       cpu=r['nativeProcessCost'];item['cpuScope']='whole-native-process-only;not-logger-or-whole-product'
       item['nativeProcessCost']={k:v for k,v in cpu.items() if k in ('cpuMsPerSec','intervalMs') and type(v) in (int,float) and 0<=v<=2**53-1}
      writer=r.get('writerPid',r.get('processId',r.get('pid') if base.startswith('native-') or base=='fan-lifecycle.log' else None))
      if type(writer) is int and 0<writer<2**32:item['writerProcessId']=writer
      if costWindow and 'writerProcessId' not in item and isinstance(r.get('usbipTransport'),dict):
       hp=r['usbipTransport'].get('hostPid')
       if type(hp) is int and 0<hp<2**32:item['writerProcessId']=hp
      subject=r.get('hostPid')
      if type(subject) is int and 0<subject<2**32:item['observedHostProcessId']=subject
      if costWindow and type(r.get('t')) is int and 946684800000<=r['t']<=4102444800000:item['time']=datetime.datetime.fromtimestamp(r['t']/1000,datetime.timezone.utc).isoformat(timespec='milliseconds')
      for key in ('time','timestampUtc','timestampLocal','atUtc'):
       v=r.get(key)
       if isinstance(v,str) and re.fullmatch(r'\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d{1,7})?(?:Z|[+-]\d{2}:\d{2})?',v):item['time']=v;break
      # Named containers are separate evidence, never merged into one synthetic fact.
      containers=[('record',r),('details',details)]
      for label,obj in list(containers):
       for child in ['response','receipt','source']:
        if isinstance(obj.get(child),dict):containers.append((label+'.'+child,obj[child]))
      for label,obj in containers:
       safe={}
       for key in BOOLS:
        if type(obj.get(key)) is bool:safe[key]=obj[key]
       for key in NUMBERS:
        value=obj.get(key)
        if type(value) in (int,float) and 0<=value<=2**53-1:safe[key]=value
       for key,allowed in ENUMS.items():
        value=obj.get(key)
        if isinstance(value,str) and value in allowed:safe[key]=value
       if safe:item['fields'][label]=safe
       if event=='api.transport-failure' and isinstance(obj.get('error'),str):
        match=re.search(r'WinHTTP ([0-9]{1,6}), phase=(send|headers|body)\)',obj['error'])
        if match:item['transportObservation']={'winHttpError':int(match[1]),'phase':match[2],'scope':'literal-error-grammar-only;not-write-outcome'}
       # Preserve opaque non-secret owner correlation without exposing session/credential strings.
       owner=[obj[k] for k in ('hostInstanceId','ScopeId','scopeId') if isinstance(obj.get(k),str) and 0<len(obj[k])<=128 and obj[k].strip().lower() not in {'unknown','not-observed','none','[redacted]','unverified'}]
       if owner:item.setdefault('ownerCorrelationSha256',[]).append(hashlib.sha256(json.dumps(owner).encode()).hexdigest())
      for node in nodes:
       counts[node]+=1;eventsSeen[node].add(event)
       if 'time' in item:
        interval=timeRanges[node].setdefault(name,{'first':item['time'],'last':item['time']});interval['last']=item['time']
       for digest in item.get('ownerCorrelationSha256',[]):correlation[node].add(digest)
       if len(captures[node])>=MAX_EVIDENCE:captures[node].pop(MAX_EVIDENCE//2)
       captures[node].append(item)
   except (OSError,ValueError,UnicodeError,zipfile.BadZipFile):issue('MEMBER_INSPECTION_FAILED',name)
 for field,label in HASH_KEYS.items():
  if re.fullmatch('[a-fA-F0-9]{64}',manifest.get(field,'')):result['versionTuple'][label]=manifest[field].lower()
 if re.fullmatch('[a-fA-F0-9]{64}',manifest.get('hostPayloadManifestSha256','')):result['exportHostPayloadManifestSha256']=manifest['hostPayloadManifestSha256'].lower()
 result['versionTuple']['appVersion']=manifest.get('appVersion') if re.fullmatch(r'\d+(?:\.\d+){1,3}',manifest.get('appVersion','')) else 'unknown'
 try:
  bias=int(manifest['timeZoneBiasMinutes']);offset=-bias
  if not -840<=offset<=840:raise ValueError()
  result['timeZoneOffset']=f'{"+" if offset>=0 else "-"}{abs(offset)//60:02d}:{abs(offset)%60:02d}'
 except (KeyError,ValueError):issue('TIMEZONE_NOT_OBSERVED')
 mismatch=[k for k,v in (expected or {}).items() if k in result['versionTuple'] and v!=result['versionTuple'][k]]
 for node,spec in NODES.items():
  fields={k for item in captures[node] for values in item['fields'].values() for k in values}
  gaps=[]
  if node=='LOG-SIM-01':
   gaps=[k for k,v in result['versionTuple'].items() if v=='unknown']
   if not result['modelEvidence']:gaps.append('machine-identity')
  if node=='LOG-SIM-02' and counts[node] and not correlation[node]:gaps.append('control-owner-correlation')
  if node=='LOG-SIM-03' and counts[node] and 'wake-input-chain-first-frame' not in eventsSeen[node]:gaps.append('retained-first-effective-input-after-wake')
  if node=='LOG-SIM-04':
   events=eventsSeen[node]
   if 'gamepad-setLed' not in events or not events.intersection({'fan.capability.result','hc.fan-dispatch.begin','hc.fan-dispatch.callback-returned'}):gaps.append('fan-and-lighting-observations-both-required')
   if not correlation[node]:gaps.append('shared-device-owner-correlation')
  if node=='LOG-SIM-05':gaps.append('comparable-whole-product-and-WebView-CPU-set-not-established-by-event-presence')
  if result['diagnostics']:gaps.append('inspection-limited;absence-is-not-proof')
  classification='VERSION_MISMATCH' if mismatch else 'NOT_OBSERVED' if not counts[node] else 'LOG_FIELD_GAP' if gaps else 'PHYSICAL_OBSERVATION_REQUIRED'
  result['nodes'].append({'nodeId':node,'versionTuple':dict(result['versionTuple']),'modelEvidence':list(result['modelEvidence']),'logArchivePath':result['logArchivePath'],'logArchiveSha256':result['logArchiveSha256'],'title':spec['title'],'classification':classification,'recordCount':counts[node],'recordCountScope':'selected-inspected-rows-only;not-complete-history',
   'redactedEvidenceLocations':[{'member':e['member'],'line':e['line']} for e in captures[node]],'observedSequence':captures[node],
   'sequenceScope':'inspected-member-file-order-only;not-cross-file-causal-order;bounded-first16-and-last16',
   'timeRangeWithTimezone':{'offset':result['timeZoneOffset'],'offsetAppliesTo':'unqualified-local-times-only;explicit-offsets-retained','perMember':timeRanges[node],'crossMemberAtomic':False},'expectedSoftwareState':spec['expectedSoftwareState'],
   'difference':{'missingOrUnproven':gaps,'versionMismatches':mismatch},'reproducibility':'not-executed;archive-observation-only',
   'sourceRepairRoute':'logging task for field gaps; original domain owner for confirmed behavior','simulatorRegressionRoute':'existing tools/ymcc_sim; handoff case IDs; no archive command execution'})
 after=path.stat()
 with path.open('rb') as stream:stable=hashlib.file_digest(stream,'sha256').hexdigest()==sha
 result['archiveStable']=stable and before.st_size==after.st_size and before.st_mtime_ns==after.st_mtime_ns
 result['status']='EVIDENCE_COVERAGE_ONLY' if result['archiveStable'] and not result['diagnostics'] else 'INCOMPLETE_EVIDENCE_COVERAGE'
 result['physicalAcceptance']=False;result['sourceBytesRead']=budgets['bytes'];return result

def main():
 p=argparse.ArgumentParser();p.add_argument('--zip',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
 def sha_arg(value):
  if not re.fullmatch('[a-fA-F0-9]{64}',value):raise argparse.ArgumentTypeError('Expected exactly64 hexadecimal SHA256 characters')
  return value.lower()
 expected_names={'native':'appVersionOrHash','host':'hostSha256OrUnknown','backend':'backendSha256OrUnknown','runtime-manifest':'runtimeManifestSha256OrUnknown'}
 for label in expected_names:p.add_argument('--expected-'+label+'-sha256',type=sha_arg)
 a=p.parse_args();expected={key:getattr(a,'expected_'+label.replace('-','_')+'_sha256') for label,key in expected_names.items() if getattr(a,'expected_'+label.replace('-','_')+'_sha256') is not None}
 result=audit_archive(a.zip,expected);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({'status':result['status'],'nodes':len(result['nodes']),'classifications':{n['nodeId']:n['classification'] for n in result['nodes']},'archiveStable':result['archiveStable']}))
if __name__=='__main__':main()
