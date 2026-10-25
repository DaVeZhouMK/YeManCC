#!/usr/bin/env python3
"""R37 actual pipe helper error-capture candidate; isolated deterministic API-result model."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R37-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(data):return hashlib.sha256(data).hexdigest()
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def candidate(body):
    nl='\r\n' if '\r\n' in body else '\n'
    first='            } else {'+nl+'                CancelIoEx(pipe, &ov);'
    second='                error = wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError();'
    assert body.count(first)==body.count(second)==1
    result=body.replace(first,'            } else {'+nl+'                const DWORD waitError = wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError();'+nl+'                CancelIoEx(pipe, &ov);',1).replace(second,'                error = waitError;',1)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--verify-current',action='store_true');options=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);raw=(REPO/'native/main.cpp').read_bytes();text=raw.decode('utf-8-sig')
    signatures=['static bool inputHostPipeIo(','static bool inputHostWaitClientLocked(']
    if options.verify_current:
        patch=json.loads((OUT/'production-patch-manifest.json').read_text(encoding='utf-8-sig'))
        bodies=[next(c['before'] for c in patch['changes'] if c['signature']==sig) for sig in signatures]
        proposed=[candidate(body) for body in bodies]
        assert all(source_function(text,sig)==body for sig,body in zip(signatures,proposed)), 'Current helper no longer matches the reviewed R37 candidate'
    else:
        bodies=[source_function(text,s) for s in signatures]
        if any('const DWORD waitError =' in body for body in bodies):parser.error('R37 is already applied; use --verify-current to validate current production against the retained baseline')
        proposed=[candidate(s) for s in bodies]
    run=OUT/('wait-error-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir()
    (run/'actual-main.snapshot.cpp').write_bytes(raw);after=text;baseline=text
    if options.verify_current:
        for old,new in zip(bodies,proposed):assert baseline.count(new)==1;baseline=baseline.replace(new,old,1)
    else:
        for old,new in zip(bodies,proposed):assert after.count(old)==1;after=after.replace(old,new,1)
    (run/'baseline-main.cpp').write_bytes(baseline.encode('utf-8-sig'))
    (run/'candidate-main.cpp').write_bytes(after.encode('utf-8-sig'))
    header='namespace baseline {\n'+'\n\n'.join(bodies)+'\n}\nnamespace candidate {\n'+'\n\n'.join(proposed)+'\n}\n'
    (run/'actual-helpers.hpp').write_bytes(header.encode('utf-8'));(run/'fixture.cpp').write_bytes((REPO/'tools/input_pipe_wait_error_selftest.cpp').read_bytes());(run/'json.hpp').write_bytes((REPO/'deps/json/json.hpp').read_bytes());(run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes())
    profiles=[]
    for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
        batch=run/(name+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8')
        p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(run/(name+'-build.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        if p.returncode:raise RuntimeError(name+' compile failed: '+str(run))
        p=subprocess.run([str(run/(name+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=60);(run/(name+'-result.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-stderr.log')).write_text(p.stderr,encoding='utf-8')
        if p.returncode:raise RuntimeError(name+' model failed: '+p.stderr)
        result=json.loads(p.stdout);assert result['pass'];profiles.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks',flush=True)
    now=(REPO/'native/main.cpp').read_bytes();current=now.decode('utf-8-sig');assert all(source_function(current,s)==body for s,body in zip(signatures,proposed if options.verify_current else bodies)),'native helper drift'
    binding={'pass':True,'mode':'verify-current' if options.verify_current else 'unapplied-candidate','sourceMainSha256':sha(raw),'currentMainSha256':sha(now),'wholeMainStable':raw==now,'actualHelperBodiesStable':True,'helperBindings':[{'signature':sig,'beforeSha256':sha(old.encode('utf-8')),'candidateSha256':sha(new.encode('utf-8'))} for sig,old,new in zip(signatures,bodies,proposed)],'exactCandidateProgramSha256':sha(after.encode('utf-8-sig')),'onlyErrorCaptureTimingChanged':True,'resourceCancellationAndPendingCloseNotFixed':True,'productionEdited':False,'realPipeOrKernelFaultReproduced':False,'artifacts':{p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file()}}
    save(run/'source-binding.json',binding);save(run/'result.json',{'pass':True,'profiles':profiles,'binding':binding,'productionEdited':False});save(OUT/('latest-current-selftest.json' if options.verify_current else 'latest-selftest.json'),{'pass':True,'run':str(run),'profiles':[{'profile':p['profile'],'checks':p['checks'],'pairedCases':p['pairedCases']} for p in profiles],'sourceMainSha256':sha(raw),'productionEdited':False})
    print(json.dumps({'pass':True,'run':str(run),'sourceSha256':sha(raw),'productionEdited':False}))
if __name__=='__main__':main()
