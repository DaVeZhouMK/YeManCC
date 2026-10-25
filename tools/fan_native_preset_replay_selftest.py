from pathlib import Path
import argparse,hashlib,json,subprocess,sys,runpy

def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--identity-header',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[1];out=a.output.resolve();source=a.native.resolve()
    if out.exists():raise RuntimeError('Output must be new')
    out.mkdir(parents=True);raw=source.read_bytes();text=raw.decode('utf-8-sig');extract=runpy.run_path(str(root/'tools/fan_msi_renderer_mode_delivery_selftest.py'))['extract']
    signatures={'ActivityExtract.inc':'static void fanActivityObserve(const std::string& path,','SnapshotExtract.inc':'static json fanActivitySnapshotJson() {'};parts={name:extract(text[text.rindex(sig):],sig)for name,sig in signatures.items()}
    for name,value in parts.items():(out/name).write_bytes(value.encode('utf-8'))
    identity=(a.identity_header or root/'native/fan_activity_identity.h').resolve();identityRaw=identity.read_bytes();identityText=identityRaw.decode('utf-8-sig');identityMetered=identityText
    identitySwaps=[('it->get<std::string>()','FixtureActivityIdentityTextCopy(*it)'),('it->get_ref<const std::string&>()','FixtureActivityIdentityTextReference(*it)'),('selected ? *selected : std::string{}','selected ? FixtureActivityIdentityOwnedCopy(*selected) : std::string{}')]
    used=[]
    for old,new in identitySwaps:
        if old in identityMetered:assert identityMetered.count(old)==1;identityMetered=identityMetered.replace(old,new);used.append((old,new))
    restored=identityMetered
    for old,new in reversed(used):restored=restored.replace(new,old)
    assert restored==identityText
    (out/'fan_activity_identity.h').write_bytes(identityMetered.encode('utf-8'))
    fixture=root/'tools/fan_native_preset_replay_selftest.cpp';(out/'fixture.cpp').write_bytes(fixture.read_bytes());batch='\n'.join(['@echo off',r'set "PATH=%SystemRoot%\System32;%PATH%"',r'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul','if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /MT /I"'+str(root/'native')+'" /I"'+str(root/'deps/json')+'" /I"'+str(out)+'" "'+str(out/'fixture.cpp')+'" /Fo"'+str(out/'fixture.obj')+'" /Fe"'+str(out/'fixture.exe')+'"','']);(out/'compile.cmd').write_text(batch,encoding='ascii')
    with(out/'compile.log').open('w',encoding='utf-8')as log:r=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],cwd=out,stdout=log,stderr=subprocess.STDOUT)
    if r.returncode:print((out/'compile.log').read_text(encoding='utf-8',errors='replace'));return 2
    results=[]
    for case in ['named','explicit','missing','invalid','stale','failed','idle-cost','identity-cost','identity-change','revoke']:
        r=subprocess.run([str(out/'fixture.exe'),case],capture_output=True,text=True,encoding='utf-8');value=json.loads(r.stdout)
        if r.returncode not in(0,1)or'fixtureError'in value:raise RuntimeError(case+' fixture failure '+r.stdout+r.stderr)
        results.append(value)
    latest=source.read_bytes().decode('utf-8-sig')
    for name,sig in signatures.items():assert extract(latest[latest.rindex(sig):],sig)==parts[name],name+' changed'
    assert identity.read_bytes()==identityRaw,'identity header drift'
    result={'identityHeaderSha256':hashlib.sha256(identityRaw).hexdigest(),'identityMeteringRestorationVerified':True,'ok':all(x['ok']for x in results),'cases':len(results),'checks':sum(x['checks']for x in results),'failedCases':sum(not x['ok']for x in results),'results':results,'sourceSha256':hashlib.sha256(raw).hexdigest(),'fixtureSha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'rawExtracts':[{ 'name':name,'sha256':hashlib.sha256(value.encode()).hexdigest()}for name,value in parts.items()],'scope':'actual native Fan activity observer and snapshot; inert persistence/boot/package/diagnostic sinks, no network/Host/HC/device'};(out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({'ok':result['ok'],'checks':result['checks'],'cases':result['cases'],'failedCases':result['failedCases']}));return 0 if result['ok']else 1
if __name__=='__main__':
    try:sys.exit(main())
    except Exception as error:print(str(error),file=sys.stderr);sys.exit(2)
