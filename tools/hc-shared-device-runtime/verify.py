"""Exercise the real shared resolver and HC callbacks without starting a device Host."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--scratch',required=True)
parser.add_argument('--omit',action='append',default=[])
args=parser.parse_args()
repo=Path(__file__).resolve().parents[2]
scratch=Path(args.scratch).resolve()
if scratch.exists(): raise RuntimeError('Use a new scratch directory')
scratch.mkdir(parents=True)
offline=scratch/'offline-packages';offline.mkdir()
def run(name,command):
    result=subprocess.run(command,cwd=repo,capture_output=True,text=True,encoding='utf-8',errors='replace')
    (scratch/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode: raise RuntimeError(name+' failed: '+str(scratch/(name+'.log')))
    print(name+': passed',flush=True)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def build(name,project):
    run(name,['dotnet','build',str(project),'-c','Release',
      '-p:BaseIntermediateOutputPath='+str(scratch/name/'obj')+'/',
      '-p:OutputPath='+str(scratch/name/'bin')+'/',
      '--source',str(offline),'--nologo','-v:q'])
    return scratch/name/'bin'
probe=build('resolver-build',repo/'tools/hc-shared-device-runtime/SharedDeviceRuntimeProbe.csproj')
probe_args=['dotnet',str(probe/'SharedDeviceRuntimeProbe.dll'),str(repo),str(scratch/'fixture'),str(scratch/'resolver-result.json')]
if args.omit:
    (scratch/'additional-omissions.json').write_text(json.dumps(args.omit),encoding='utf-8')
    probe_args.append(str(scratch/'additional-omissions.json'))
run('resolver-checks',probe_args)
profile=json.loads((repo/'deps/ymcc-device-core-runtime/profile.json').read_text(encoding='utf-8-sig'))
runtime=repo/'PowerControl/handheldcompanion-runtime'/profile['runtimeId']
fixture=scratch/'fixture/PowerControl/handheldcompanion-runtime'/profile['runtimeId']
callback=scratch/'callback-project';callback.mkdir()
original=repo/'tools/hc-devicecore-trim/probes/CallbackPathProbe'
adapter=(original/'PinnedHcAdapter.cs').read_text(encoding='utf-8-sig')
anchor='            if (!File.Exists(path))\n                throw new FileNotFoundException("HC dependency is not in its specified directory.", path);'
replacement='            if (!File.Exists(path))\n            {\n                Assembly shared = YeMan.SharedRuntime.DeviceCoreSharedDependencies.Resolve(this, name, directory)\n                    ?? throw new FileNotFoundException("HC dependency is not in its specified directory.", path);\n                BlockNativeImports(shared);\n                return shared;\n            }'
if adapter.count(anchor)!=1:raise RuntimeError('Original callback loader changed; review the single fallback transformation')
(callback/'PinnedHcAdapter.cs').write_text(adapter.replace(anchor,replacement),encoding='utf-8')
shutil.copyfile(original/'Program.cs',callback/'Program.cs')
shutil.copyfile(repo/'FanLab/DeviceCoreSharedDependencies.cs',callback/'DeviceCoreSharedDependencies.cs')
shutil.copyfile(repo/'FanLab/RuntimeDependencyContract.cs',callback/'RuntimeDependencyContract.cs')
shutil.copyfile(original/'CallbackPathProbe.csproj',callback/'CallbackPathProbe.csproj')
callback_bin=build('callback-build',callback/'CallbackPathProbe.csproj')
hc_hash=sha(runtime/'HandheldCompanion.dll')
run('callback-baseline',['dotnet',str(callback_bin/'CallbackPathProbe.dll'),str(runtime/'HandheldCompanion.dll'),hc_hash,'-',str(scratch/'callback-baseline.json')])
run('callback-shared',['dotnet',str(callback_bin/'CallbackPathProbe.dll'),str(fixture/'HandheldCompanion.dll'),hc_hash,str(scratch/'callback-baseline.json'),str(scratch/'callback-shared.json')])
snapshot=scratch/'consumer-source'
source_files={}
for sub in ['FanLab/real-host','FanLab/fan-backend','FanLab/LightSetter']:
    for file in (repo/sub).rglob('*'):
        if not file.is_file() or any(part in ('bin','obj','.git') for part in file.relative_to(repo/sub).parts):continue
        relative=file.relative_to(repo)
        target=snapshot/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(file,target)
        source_files[relative.as_posix()]=sha(file)
# Copy actual linked sources from the consumer projects, including the load
# contract and generated capability catalogs, rather than a stale hand list.
import xml.etree.ElementTree as ET
for relative in list(source_files):
    if not relative.endswith('.csproj'):continue
    project=repo/relative
    for node in ET.parse(project).iter():
        include=node.get('Include')
        if node.tag.rsplit('}',1)[-1]!='Compile' or include is None:continue
        if any(token in include for token in ['*','?','$']):raise RuntimeError('Review dynamic linked compile input: '+include)
        source=(project.parent/include.replace(chr(92),'/')).resolve()
        if not source.is_relative_to(repo) or not source.is_file():raise RuntimeError('Invalid linked compile input: '+include)
        linked=source.relative_to(repo).as_posix();target=snapshot/linked
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        source_files[linked]=sha(source)
# These are compile-only checks: no Host/Setter executable is started.
for name,relative in [('fanhost-compile','FanLab/real-host/YeManFanHost.csproj'),('setter-compile','FanLab/LightSetter/YeManLightSetter.csproj')]:
    run(name,['dotnet','build',str(snapshot/relative),'-c','Release','--source',str(offline),'--nologo','-v:q'])
result={'status':'passed','sourceHashes':source_files,
  'probe':json.loads((scratch/'resolver-result.json').read_text(encoding='utf-8-sig')),
  'callback':json.loads((scratch/'callback-shared.json').read_text(encoding='utf-8-sig')),
  'actualConsumersCompile':True,'hostOrSetterExecuted':False,'physicalDeviceVerified':False,
  'callbackFixtureChange':'Only missing-local-copy loader fallback delegates to the exact production shared resolver; original callback execution and memory transport unchanged.'}
(scratch/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'status':'passed','checks':result['probe']['checks'],'callbackChecks':result['callback']['count'],'removedCopyBytes':result['probe']['removedCopyBytes']}))
