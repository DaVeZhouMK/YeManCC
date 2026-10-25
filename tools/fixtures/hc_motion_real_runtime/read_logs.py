# Read only the three user-supplied evidence archives; never extract or execute.
import sys,json,hashlib,zipfile,math
from pathlib import Path
root=Path(sys.argv[1])/'日志'
targets=[('MSI-G3E','微星G3E','*184736*.zip','HandheldCompanion.Devices.ClawCG3EM'),
         ('ROG-Xbox-2025','ROG-Xbox-2025','*.zip','HandheldCompanion.Devices.XboxROGAllyX'),
         ('GPD-Win5','Win5','*.zip','HandheldCompanion.Devices.GPDWin5')]
output=[]
for family,sub,pattern,device in targets:
 matches=sorted((root/sub).glob(pattern))
 if len(matches)!=1:raise RuntimeError(f'Expected one fixed archive for {family}, found {len(matches)}')
 archive=matches[0];frames=[];stats={'samples':0,'admittedSamples':0,'orientationNullSamples':0,'usableRawVectors':0}
 with zipfile.ZipFile(archive) as z:
  for line in z.read('input-capture.jsonl').decode('utf-8-sig').splitlines():
   try:d=json.loads(line)
   except json.JSONDecodeError:continue
   if d.get('kind')!='sample':continue
   stats['samples']+=1
   if d.get('matrixAdmission') is True:stats['admittedSamples']+=1
   imu=d.get('imu',{});q=imu.get('orientation',{})
   if any(v is None for v in q.values()):stats['orientationNullSamples']+=1
   raw=imu.get('raw');dt=d.get('dtMs')
   if not isinstance(raw,dict) or not isinstance(dt,(float,int)) or not math.isfinite(dt) or dt<=0:continue
   if not all(isinstance(raw.get(k),(float,int)) and math.isfinite(raw[k]) for k in ['gx','gy','gz','ax','ay','az']):continue
   candidate=d.get('hcMatrixCandidate',{});receipt=candidate.get('recipeReceipt',{})
   if not isinstance(candidate.get('gyro'),dict) or not isinstance(candidate.get('accel'),dict):continue
   ids={sensor:receipt.get(sensor,{}).get('id') for sensor in ['gyro','accel']}
   values=[candidate['gyro'].get(k) for k in ['gx','gy','gz']]+[candidate['accel'].get(k) for k in ['x','y','z']]
   if not all(ids.values()) or not all(isinstance(v,(float,int)) and math.isfinite(v) for v in values):continue
   frames.append({'raw':raw,'candidate':values,'matrixIds':ids,'deltaSeconds':dt/1000,'matrixAdmissionInOriginalLog':d.get('matrixAdmission') is True})
 stats['usableRawVectors']=len(frames)
 output.append({'family':family,'deviceClass':device,'archive':str(archive),'archiveSha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
                'member':'input-capture.jsonl','stats':stats,'frames':frames,
                'boundary':'Raw vectors only. Original calibration/filter state is not reconstructed; original admission remains reported separately.'})
json.dump(output,sys.stdout,ensure_ascii=False)
