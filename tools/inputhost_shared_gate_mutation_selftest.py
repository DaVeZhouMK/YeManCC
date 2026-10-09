"""Mutate a private copied candidate; run only read-only gate/packager checks."""
import argparse,datetime,json,shutil,subprocess
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('candidate',type=Path);ap.add_argument('output',type=Path);args=ap.parse_args()
source=args.candidate.resolve();out=args.output.resolve();assert not out.exists();out.mkdir(parents=True)
root=out/'fixture';shutil.copytree(source,root)
tools=Path(__file__).resolve().parent
workspace=tools.parents[2]
hc=root/'PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902'
vg=root/'PowerControl/feature-assets/virtual-gamepad'
a=hc/'Newtonsoft.Json.dll';b=hc/'Nefarius.Utilities.DeviceManagement.dll'
a_bytes=a.read_bytes();b_bytes=b.read_bytes();rows=[]
def run(case,expected):
 for kind in ('gate','packaging'):
  if kind=='gate':
   cmd=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(tools/'opt-inheritance-gate.ps1'),'-Mode','Gate','-Stage','package','-BuildManifest',str(workspace/'Build/Validation/opt-gate/BUILD__build.json'),'-WorkspaceRoot',str(workspace),'-AppDirOverride',str(root/'YeManCC'),'-WebDirOverride',str(root/'YeManCC'),'-PcDirOverride',str(vg),'-Out',str(out/f'{case}-gate.json')]
  else:cmd=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(tools/'inputhost_shared_packaging_selftest.ps1'),'-CandidateRoot',str(root)]
  before=datetime.datetime.now().astimezone().isoformat();result=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
  log=out/f'{case}-{kind}.log';log.write_bytes(result.stdout)
  valid=(result.returncode==0)==expected
  rows.append({'case':case,'kind':kind,'command':cmd,'exitCode':result.returncode,'expectedPass':expected,'assertionPass':valid,'log':str(log),'startedAt':before,'completedAt':datetime.datetime.now().astimezone().isoformat(),'deviceAccessed':False})
  if not valid:raise AssertionError((case,kind,result.returncode,result.stdout[-1200:]))
def safe(p):assert p.resolve().is_relative_to(root.resolve())
try:
 run('positive',True)
 safe(a);a.unlink();run('missing',False);a.write_bytes(a_bytes)
 safe(a);a.write_bytes(a_bytes[:-1]+bytes([a_bytes[-1]^1]));run('tampered',False);a.write_bytes(a_bytes)
 safe(a);safe(b);a.write_bytes(b_bytes);b.write_bytes(a_bytes);run('swapped-name-pins',False);a.write_bytes(a_bytes);b.write_bytes(b_bytes)
 dup=vg/a.name;safe(dup);dup.write_bytes(a_bytes);run('local-duplicate',False);dup.unlink()
 renamed=hc/(a.name+'.wrong-name');safe(renamed);a.rename(renamed);run('wrong-name',False);renamed.rename(a)
 run('restored-positive',True)
finally:
 (out/'result.json').write_text(json.dumps({'cases':rows,'hardwareWrites':False,'targetCodeExecuted':False,'allAssertionsPass':all(r['assertionPass'] for r in rows)},ensure_ascii=False,indent=2),encoding='utf-8')
print('SHARED_GATE_PACKAGING_SELFTEST',len(rows),'checks PASS_NOT_DEVICE_PASS')
