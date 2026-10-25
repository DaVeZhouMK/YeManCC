"""Run existing HC pure construction and passive SDL probes with the production shared resolver."""
import argparse,hashlib,json,subprocess
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--runtime',required=True)
parser.add_argument('--scratch',required=True)
parser.add_argument('--token-map')
args=parser.parse_args()
repo=Path(__file__).resolve().parents[2]
root=Path(args.runtime).resolve();scratch=Path(args.scratch).resolve()
if scratch.exists():raise RuntimeError('Use a new scratch directory')
scratch.mkdir(parents=True);offline=scratch/'offline-packages';offline.mkdir()
changes=[]
token_map=json.loads(Path(args.token_map).read_text(encoding='utf-8-sig')) if args.token_map else None
if token_map:
    if token_map['candidateSha256']!=hashlib.sha256((root/'HandheldCompanion.dll').read_bytes()).hexdigest():raise RuntimeError('Token-map candidate SHA mismatch')
    proof=Path(token_map['independentILProof'])
    if hashlib.sha256(proof.read_bytes()).hexdigest()!=token_map['independentILProofSha256']:raise RuntimeError('Independent IL proof changed')
    data=json.loads(proof.read_text(encoding='utf-8-sig'))
    if data['status']!='passed' or data['results'][0]['afterSha256']!=token_map['candidateSha256']:raise RuntimeError('Independent IL equality proof not applicable')
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
    if name=='SdlTouchpadProbe' and token_map:
        old='Check(pads.MetadataToken==0x06002847&&fingers.MetadataToken==0x06002848&&init.MetadataToken==0x0600284f,"Reviewed method tokens changed");'
        rows={r['name']:r for r in token_map['methods']}
        new='Check(pads.MetadataToken=='+hex(rows['GetTouchpads']['candidateToken'])+'&&fingers.MetadataToken=='+hex(rows['GetTouchpadFingers']['candidateToken'])+'&&init.MetadataToken=='+hex(rows['InitializeInputOutput']['candidateToken'])+',"Reviewed method tokens changed");'
        text=replace_once(text,old,new)
        changes.append({'probe':name,'additionalChange':'Only fixed method-token values rebased for ILLink table compaction; three original signatures and tokens checked, candidate full retained IL equality independently pinned. All behavioral assertions, dispatch checks, zero/sentinel and native-denial operations unchanged.','tokenMap':token_map})
    (project/'Program.cs').write_text(text,encoding='utf-8')
    (project/(name+'.csproj')).write_bytes((source/(name+'.csproj')).read_bytes())
    helper=repo/'FanLab/DeviceCoreSharedDependencies.cs'
    (project/helper.name).write_bytes(helper.read_bytes())
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

