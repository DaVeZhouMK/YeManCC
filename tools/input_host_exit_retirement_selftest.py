#!/usr/bin/env python3
"""Actual exit-retirement header, private children and explicit failure ports."""
from __future__ import annotations
import sys
sys.dont_write_bytecode=True
import argparse,datetime as dt,hashlib,importlib.util,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('native_parent_tools',ROOT/'tools/input_host_native_exit_parent_selftest.py')
parent=importlib.util.module_from_spec(spec);spec.loader.exec_module(parent)
helper=parent.helper
sha=lambda b:hashlib.sha256(b).hexdigest()
def need(v,m):
    if not v:raise RuntimeError(m)
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,required=True);args=parser.parse_args()
    out=args.out.resolve();need(not out.exists(),'Fresh evidence required');out.mkdir(parents=True)
    files=[Path(__file__).resolve(),ROOT/'tools/input_host_native_exit_parent_selftest.py',parent.PROCESS,parent.process.HELPER,
      ROOT/'tools/fixtures/input_host_exit_retirement/suite.cpp.in',ROOT/'native/input_host_exit_retirement.h',ROOT/'native/input_host_process_retirement.h',ROOT/'deps/json/json.hpp']
    inputs={p:p.read_bytes() for p in files};receipt={'pass':False,'parentStatus':'INCOMPLETE','realHostOrSdkExecuted':False,'productionModified':False,'inputs':[{'path':str(p),'sha256':sha(v)} for p,v in inputs.items()]}
    try:
        source=out/'source';source.mkdir()
        for p,v in inputs.items():
            dest=source/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(v)
        compiler,env=helper.compiler_environment(out);receipt['compiler']={'path':str(compiler),'sha256':sha(compiler.read_bytes()),'vcvarsSha256':sha(helper.VCVARS.read_bytes())};receipt['profiles']=[]
        fixture=source/'tools/fixtures/input_host_exit_retirement/suite.cpp.in'
        for profile,flags in [('Release',['/MT','/O2','/DNDEBUG']),('Checked',['/MTd','/Od','/D_DEBUG'])]:
            target=out/profile;target.mkdir();exe=target/'suite.exe';cpp=target/'suite.cpp';cpp.write_bytes(fixture.read_bytes())
            command=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/permissive-',*flags,'/I'+str(source/'native'),'/I'+str(source/'deps/json'),cpp,'/Fo'+str(target/'suite.obj'),'/Fe'+str(exe)]
            save(target/'command.json',[str(v) for v in command]);built=helper.execute(command,target,env,target/'build.log',60);need(built.returncode==0,'Build failed '+str(target/'build.log'))
            ran=helper.execute([exe],target,env,target/'run.log',30);need(ran.stdout.strip(),'No execution receipt')
            result=json.loads(ran.stdout);save(target/'result.json',result);need(ran.returncode==0 and result['failures']==0,'Header contract failed '+str(target/'result.json'))
            imported=helper.execute([compiler.parent/'dumpbin.exe','/imports',exe],target,env,target/'imports.txt',15);need(imported.returncode==0 and not any(x in imported.stdout.lower() for x in helper.FORBIDDEN),'Unexpected device/UI import')
            receipt['profiles'].append({'name':profile,'result':result,'binarySha256':sha(exe.read_bytes())})
        receipt['sourceStable']=all(p.read_bytes()==v for p,v in inputs.items());need(receipt['sourceStable'],'Source changed during run')
        receipt['pass']=True;receipt['scope']='Actual exit-retirement algorithm/WindowsPort; real private process/creation-thread exit, join and checked close. Explicit force-accepted-without-exit/unknown wait/identity/failure ports remain labeled; no real Host/SDK, pipe Runtime or complete parent acceptance.'
    except Exception as exc:receipt['error']=str(exc);save(out/'RESULT.json',receipt);raise
    save(out/'RESULT.json',receipt);print(json.dumps({'out':str(out),'pass':True,'profiles':[{'name':p['name'],'cases':len(p['result']['rows'])} for p in receipt['profiles']],'parentStatus':'INCOMPLETE'}))
if __name__=='__main__':main()
