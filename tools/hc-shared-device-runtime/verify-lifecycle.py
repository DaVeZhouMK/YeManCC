"""Run existing HC pure construction and passive SDL probes with the production shared resolver."""
import argparse,hashlib,json,subprocess
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--runtime',required=True)
parser.add_argument('--scratch',required=True)
args=parser.parse_args()
repo=Path(__file__).resolve().parents[2]
root=Path(args.runtime).resolve();scratch=Path(args.scratch).resolve()
if scratch.exists():raise RuntimeError('Use a new scratch directory')
scratch.mkdir(parents=True);offline=scratch/'offline-packages';offline.mkdir()
changes=[]
def sha(b):return hashlib.sha256(b).hexdigest()
def command(name,cmd):
    p=subprocess.run(cmd,cwd=repo,capture_output=True,text=True,encoding='utf-8',errors='replace')
    (scratch/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
    if p.returncode:raise RuntimeError(name+' failed; '+str(scratch/(name+'.log')))
    print(name+': passed',flush=True)
def replace_once(source,old,new):
    if source.count(old)!=1:raise RuntimeError('Probe loader changed: '+old)
    return source.replace(old,new)
for name in ['ModernHeadlessProbe','SdlTouchpadProbe','SdlTimerProbe']:
    source=repo/'tools/hc-devicecore-trim/probes'/name
    project=scratch/(name+'-source');project.mkdir()
    original=(source/'Program.cs').read_text(encoding='utf-8-sig');text=original
    if name=='SdlTouchpadProbe':
        text=replace_once(text,'var a=LoadFromAssemblyPath(Path.Combine(root,name+".dll"));',
            'string path=Path.Combine(root,name+".dll");var a=File.Exists(path)?LoadFromAssemblyPath(path):YeMan.SharedRuntime.DeviceCoreSharedDependencies.Resolve(this,requested,root)??throw new FileNotFoundException("Missing dependency",path);')
    else:
        guard='if(!File.Exists(path))throw new FileNotFoundException("'+('Missing frozen dependency' if name=='ModernHeadlessProbe' else 'Missing frozen metadata dependency')+'",path);'
        block='BlockNative' if name=='ModernHeadlessProbe' else 'Block'
        new='if(!File.Exists(path)){var shared=YeMan.SharedRuntime.DeviceCoreSharedDependencies.Resolve(this,requested,root)??throw new FileNotFoundException("Missing dependency",path);'+block+'(shared);loads.Add(new{Requested=requested.FullName,Resolved=shared.FullName,Path=shared.Location});return shared;}'
        text=replace_once(text,guard,new)
    (project/'Program.cs').write_text(text,encoding='utf-8')
    (project/(name+'.csproj')).write_bytes((source/(name+'.csproj')).read_bytes())
    helper=repo/'FanLab/DeviceCoreSharedDependencies.cs'
    (project/helper.name).write_bytes(helper.read_bytes())
    contract=repo/'FanLab/RuntimeDependencyContract.cs'
    (project/contract.name).write_bytes(contract.read_bytes())
    changes.append({'probe':name,'originalSha256':sha(original.encode()),'fixtureSha256':sha(text.encode()),
       'change':'Only custom ALC missing-local-copy loader branch delegates to exact production shared resolver; assertions and operations unchanged.'})
    out=scratch/(name+'-build')
    command(name+'-build',['dotnet','build',str(project/(name+'.csproj')),'-p:BaseIntermediateOutputPath='+str(out/'obj')+'/',
      '-p:OutputPath='+str(out/'bin')+'/','--source',str(offline),'--nologo','-v:q'])
    if name=='ModernHeadlessProbe':
        command('modern-pure-constructions',['dotnet',str(out/'bin'/(name+'.dll')),str(root),'538ea1f68f629a04',str(scratch/'modern-pure-constructions.json')])
    else:
        hc_hash=sha((root/'HandheldCompanion.dll').read_bytes())
        modes=['candidate-zero','candidate-nonzero'] if name=='SdlTouchpadProbe' else ['candidate']
        for mode in modes:
            command(name+'-'+mode,['dotnet',str(out/'bin'/(name+'.dll')),str(root),hc_hash,mode,str(scratch/(name+'-'+mode+'.json'))])
results={p.stem:json.loads(p.read_text(encoding='utf-8-sig')) for p in scratch.glob('*.json')}
(scratch/'verification.json').write_text(json.dumps({'status':'passed','runtime':str(root),'probeLoaderTransformations':changes,
 'results':results,'hostOrSetterExecuted':False,'physicalDeviceVerified':False},indent=2)+'\n',encoding='utf-8')
print('All existing pure construction/passive probes passed with production shared loading.')
