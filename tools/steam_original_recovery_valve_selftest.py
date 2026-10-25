"""Run the production valve reader against the preserved pre-optimization reader.
Called by steam_original_recovery_authority_selftest.py. Controlled ports only;
no application process opens, sleep, Steam calls or runtime state changes.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,subprocess

def run(output):
    repo=Path(__file__).resolve().parents[1]
    out=Path(output).resolve()
    if out==repo or repo in out.parents or out in repo.parents:raise RuntimeError('fresh external output required')
    if out.exists() and any(out.iterdir()):raise RuntimeError('output not fresh')
    out.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('valve_source_extractor',repo/'tools/steam_original_recovery_authority_selftest.py')
    extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)
    files=repo/'tools/fixtures/steam_original_recovery_valve_guard'
    main=(repo/'native/main.cpp').read_text(encoding='utf-8-sig')
    before=(files/'reader-before.cpp.in').read_text(encoding='utf-8').rstrip('\n')
    before_sha=hashlib.sha256(before.encode()).hexdigest()
    if before_sha!='d665d4bfffda678cf6060fbed55f4d4bed3fc9229968cae1a41c3028726ff6d7':raise RuntimeError('preserved production reader changed')
    helper_names=['sgBaseName','sgNamePatternMatches','sgNameExcludedBy','nativeValveTargetStillValid']
    functions={name:extract.function(main,name) for name in helper_names+['nativeValveReadCurrentTarget']}
    helpers='\n'.join(functions[n] for n in helper_names)
    after=functions['nativeValveReadCurrentTarget']
    ports=(files/'ports.cpp.in').read_text(encoding='utf-8');cases=(files/'cases.cpp.in').read_text(encoding='utf-8')
    identity=(repo/'native/monitor_identity.h').read_bytes();(out/'monitor_identity.h').write_bytes(identity)
    env={k:v for k,v in os.environ.items() if k.upper() not in ('CL','_CL_','LINK','_LINK_')}
    locked_env=r'''@echo off
setlocal
set "VCTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVERSION=10.0.26100.0"
set "INCLUDE=%VCTOOLS%\include;%SDK%\Include\%SDKVERSION%\um;%SDK%\Include\%SDKVERSION%\shared;%SDK%\Include\%SDKVERSION%\ucrt"
set "LIB=%VCTOOLS%\lib\x64;%SDK%\Lib\%SDKVERSION%\um\x64;%SDK%\Lib\%SDKVERSION%\ucrt\x64"
set "PATH=%SystemRoot%\System32;%VCTOOLS%\bin\Hostx64\x64;%PATH%"
'''
    results=[]
    variants=[('positive',None),('skip-required-blacklist',('if (!savedPathWhitelisted) userExcludes = sgExcludes();','if (false) userExcludes = sgExcludes();')),('skip-whitelist-refresh',('whitelist = sgGameWhitelist();','whitelist = {};')),('skip-custom-config',('if (snapshot.game.customConfigured) configuredGameExes = nativeConfiguredGameExes();','if (false) configuredGameExes = nativeConfiguredGameExes();'))]
    for name,mutation in variants:
        current=after
        if mutation:
            if current.count(mutation[0])!=1:raise RuntimeError('negative source mutation not unique:'+name)
            current=current.replace(*mutation)
        code=ports+helpers+'\n'+before.replace('nativeValveReadCurrentTarget(','beforeRead(')+'\n'+current.replace('nativeValveReadCurrentTarget(','afterRead(')+'\n'+cases
        case=out/name;case.mkdir();(case/'fixture.cpp').write_text(code,encoding='utf-8')
        script=case/'Build.cmd';script.write_text(locked_env+f'cd /d "{case}"\ncl /nologo /std:c++20 /O2 /EHsc /MT /utf-8 fixture.cpp /I"{out}" /Fe:fixture.exe\nexit /b %errorlevel%\n',encoding='ascii')
        compiled=subprocess.run(['cmd.exe','/d','/c',str(script)],capture_output=True,env=env)
        (case/'compile.stdout').write_bytes(compiled.stdout);(case/'compile.stderr').write_bytes(compiled.stderr)
        item={'name':name,'compileReturnCode':compiled.returncode,'codeSha256':hashlib.sha256(code.encode()).hexdigest()}
        if compiled.returncode==0:
            process=subprocess.run([str(case/'fixture.exe')],capture_output=True,env=env)
            (case/'run.stdout').write_bytes(process.stdout);(case/'run.stderr').write_bytes(process.stderr)
            item['returnCode']=process.returncode
            if name=='positive' and process.returncode==0:item['result']=json.loads(process.stdout)
            item['passed']=process.returncode==(0 if name=='positive' else 1)
        else:item['passed']=False
        results.append(item)
    pinned={name:hashlib.sha256(value.encode()).hexdigest() for name,value in functions.items()}
    now=(repo/'native/main.cpp').read_text(encoding='utf-8-sig')
    unchanged=all(extract.function(now,name)==value for name,value in functions.items()) and (repo/'native/monitor_identity.h').read_bytes()==identity
    result={'schema':'ymcc.production-valve-unused-rule-differential.v1','passed':all(x['passed'] for x in results) and unchanged,'variants':results,'productionFunctions':pinned,'preservedBeforeFunctionSha256':before_sha,'productionFunctionsUnchangedDuringRun':unchanged,'controlledPorts':['process identity/session/memory','rule loader entry calls','disk-stamp and selected target snapshot'],'realWindowsProcessCalls':0,'sleepActions':0,'ruleLoadCallsAreNotPhysicalDiskIo':True,'fieldSpeedImprovementProven':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    result=run(args.output);print(json.dumps(result));raise SystemExit(0 if result['passed'] else 1)
