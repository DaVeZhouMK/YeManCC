"""Read-only archive audit; event age is observation, not sensor invalidity."""
from pathlib import Path
import hashlib,json,math,sys,zipfile
workspace=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
float_max=3.4028234663852886e38;results=[]
for archive in sorted((workspace/'日志').rglob('*.zip')):
 with zipfile.ZipFile(archive) as z:
  names=[n for n in z.namelist() if n.replace('\\','/').rsplit('/',1)[-1].lower()=='input-capture.jsonl']
  if len(names)!=1:results.append({'archive':str(archive),'state':'missing-or-ambiguous-member','members':names});continue
  data=z.read(names[0]);stats={'samples':0,'parseFailures':0,'numericRawSamples':0,'rawUnavailable':0,'rawRejectedByNumericContract':0,'zeroGyroTriples':0,'receiptPairSamples':0,'maxGyroReceiptAgeMs':None,'maxAccelReceiptAgeMs':None,'negativeAgePairs':0,'matrixAdmittedSamples':0,'matrixAdmittedWithAgeOver1500Ms':0,'numericRejectionCountFieldsPresent':0};examples=[]
  for line_no,line in enumerate(data.decode('utf-8-sig',errors='replace').splitlines(),1):
   try:row=json.loads(line)
   except (ValueError,TypeError):stats['parseFailures']+=1;continue
   if row.get('kind')!='sample':continue
   stats['samples']+=1;admitted=row.get('matrixAdmission',row.get('testLane',{}).get('matrixAdmission',False));stats['matrixAdmittedSamples']+=bool(admitted)
   raw=row.get('imu',{}).get('raw');keys=['gx','gy','gz','ax','ay','az']
   if not isinstance(raw,dict) or not all(k in raw for k in keys):stats['rawUnavailable']+=1
   else:
    values=[raw[k] for k in keys];valid=all(isinstance(v,(int,float)) and not isinstance(v,bool) and math.isfinite(v) and abs(v)<=float_max for v in values)
    if valid:stats['numericRawSamples']+=1;stats['zeroGyroTriples']+=all(v==0 for v in values[:3])
    else:stats['rawRejectedByNumericContract']+=1
   t=row.get('t');event=row.get('event',{});g=event.get('gyro',{});a=event.get('accel',{});gt=g.get('receiptTick');at=a.get('receiptTick')
   stats['numericRejectionCountFieldsPresent']+=('rejectedNumericSamples' in g and 'rejectedNumericSamples' in a)
   if not all(isinstance(v,(int,float)) and v>0 for v in [t,gt,at]):continue
   ga=t-gt;aa=t-at;stats['receiptPairSamples']+=1;stats['negativeAgePairs']+=(ga<0 or aa<0)
   stats['maxGyroReceiptAgeMs']=max(stats['maxGyroReceiptAgeMs'] or 0,ga);stats['maxAccelReceiptAgeMs']=max(stats['maxAccelReceiptAgeMs'] or 0,aa)
   if admitted and max(ga,aa)>1500:
    stats['matrixAdmittedWithAgeOver1500Ms']+=1
    if len(examples)<3:examples.append({'line':line_no,'t':t,'gyroAgeMs':ga,'accelAgeMs':aa,'gyroSequence':g.get('sequence'),'accelSequence':a.get('sequence'),'sameBinding':event.get('sameBindingGeneration')})
  results.append({'archive':str(archive),'archiveSha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'member':names[0],'memberSha256':hashlib.sha256(data).hexdigest(),'stats':stats,'longGapExamples':examples,'ageClassification':'callback receipt age only; no automatic invalidity, driver-stop or source freshness claim','historicalCause':'not established; old logs lack explicit numeric rejection counters'})
report={'schema':1,'scope':'All current supplied archive samples; numeric representability and reported callback cadence. No hardware or settings changes.','archives':results,'newCadencePolicy':False,'newProductDlls':[],'physicalAcceptance':False}
(out/'LOG-OBSERVATION-AUDIT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'archives':len(results),'samples':sum(r.get('stats',{}).get('samples',0) for r in results),'numericRawSamples':sum(r.get('stats',{}).get('numericRawSamples',0) for r in results),'numericRejected':sum(r.get('stats',{}).get('rawRejectedByNumericContract',0) for r in results),'longAgeSamples':sum(r.get('stats',{}).get('matrixAdmittedWithAgeOver1500Ms',0) for r in results),'rawUnavailable':sum(r.get('stats',{}).get('rawUnavailable',0) for r in results),'parseFailures':sum(r.get('stats',{}).get('parseFailures',0) for r in results),'rejectionCountFieldsObserved':sum(r.get('stats',{}).get('numericRejectionCountFieldsPresent',0) for r in results)},indent=2))
