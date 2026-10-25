from pathlib import Path
import argparse,hashlib,json,subprocess,sys

def main():
 p=argparse.ArgumentParser();p.add_argument('--header',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1];out=a.output.resolve();header=a.header.resolve()
 if out.exists():raise RuntimeError('Output must be new')
 out.mkdir(parents=True);raw=header.read_bytes();text=raw.decode('utf-8-sig');metered=text;swaps=[]
 for old,new in [('return id;','return FixtureLeaseCopy(id);'),('id ? *id : std::string{}','id ? FixtureLeaseCopy(*id) : std::string{}'),('#include "../deps/json/json.hpp"','#include "json.hpp"')]:
  if old in metered:
   assert metered.count(old)==1,old;metered=metered.replace(old,new);swaps.append((old,new))
 restored=metered
 for old,new in reversed(swaps):restored=restored.replace(new,old)
 assert restored==text
 (out/'LeaseHeaderExtract.h').write_bytes(metered.encode('utf-8'));fixture=root/'tools/fan_lease_receipt_borrow_selftest.cpp';(out/'fixture.cpp').write_bytes(fixture.read_bytes())
 borrowed='inline const std::string* keepaliveLeaseReference('in text
 batch='\n'.join(['@echo off',r'set "PATH=%SystemRoot%\System32;%PATH%"',r'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul','if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /MT /DFAN_HAS_BORROWED_LEASE='+('1'if borrowed else'0')+' /I"'+str(root/'deps/json')+'" /I"'+str(out)+'" "'+str(out/'fixture.cpp')+'" /Fo"'+str(out/'fixture.obj')+'" /Fe"'+str(out/'fixture.exe')+'"'])
 (out/'compile.cmd').write_text(batch,encoding='ascii')
 with(out/'compile.log').open('w',encoding='utf-8')as log:r=subprocess.run(['cmd','/d','/c',str(out/'compile.cmd')],cwd=out,stdout=log,stderr=subprocess.STDOUT)
 if r.returncode:print((out/'compile.log').read_text(encoding='utf-8',errors='replace'));return 2
 r=subprocess.run([str(out/'fixture.exe')],capture_output=True,text=True,encoding='utf-8');value=json.loads(r.stdout);(out/'output.txt').write_text(r.stdout+r.stderr,encoding='utf-8')
 assert r.returncode in(0,1)and header.read_bytes()==raw
 value.update({'headerSha256':hashlib.sha256(raw).hexdigest(),'fixtureSha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'meteringRestorationVerified':True,'borrowedApiChecksIncluded':borrowed});(out/'result.json').write_text(json.dumps(value,indent=2),encoding='utf-8');print(json.dumps({k:value[k]for k in ['ok','checks','failures','validationTextCopies','validationCopiedBytes']}));return r.returncode
if __name__=='__main__':sys.exit(main())
