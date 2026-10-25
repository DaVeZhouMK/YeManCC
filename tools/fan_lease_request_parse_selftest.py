from pathlib import Path
import argparse,hashlib,json,subprocess,sys

def main():
 p=argparse.ArgumentParser();p.add_argument('--native',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--wire-bodies',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[1];out=a.output.resolve();source=a.native.resolve()
 if out.exists():raise RuntimeError('Output must be new')
 out.mkdir(parents=True);raw=source.read_bytes();text=raw.decode('utf-8-sig');start=text.index('static bool isFanLeaseIdValid(const std::string& id) {');end=text.index('static std::string fanApiPathFromObject(',start);part=text[start:end]
 swaps=[('json::parse(text)','FixtureLeaseParse(text)'),('text.substr(prefixLength, quote - prefixLength)','FixtureLeaseSlice(text, prefixLength, quote - prefixLength)')];metered=part;used=[]
 for old,new in swaps:
  if old in metered:assert metered.count(old)==1;metered=metered.replace(old,new);used.append((old,new))
 restored=metered
 for old,new in reversed(used):restored=restored.replace(new,old)
 assert restored==part
 (out/'LeaseParserExtract.inc').write_bytes(metered.encode('utf-8'));(out/'original-extract.txt').write_bytes(part.encode('utf-8'));fixture=root/'tools/fan_lease_request_parse_selftest.cpp';(out/'fixture.cpp').write_bytes(fixture.read_bytes())
 batch='\n'.join(['@echo off',r'set "PATH=%SystemRoot%\System32;%PATH%"',r'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul','if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /MT /I"'+str(root/'native')+'" /I"'+str(out)+'" "'+str(out/'fixture.cpp')+'" /Fo"'+str(out/'fixture.obj')+'" /Fe"'+str(out/'fixture.exe')+'"'])
 (out/'compile.cmd').write_text(batch,encoding='ascii')
 with(out/'compile.log').open('w',encoding='utf-8')as log:r=subprocess.run(['cmd','/d','/c',str(out/'compile.cmd')],cwd=out,stdout=log,stderr=subprocess.STDOUT)
 if r.returncode:print((out/'compile.log').read_text(encoding='utf-8',errors='replace'));return 2
 r=subprocess.run([str(out/'fixture.exe')]+([str(a.wire_bodies.resolve())]if a.wire_bodies else[]),capture_output=True,text=True,encoding='utf-8');v=json.loads(r.stdout);assert r.returncode in(0,1);latest=source.read_bytes().decode('utf-8-sig');assert part in latest,'owned parser drift';v.update({'sourceSha256':hashlib.sha256(raw).hexdigest(),'fixtureSha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'extractSha256':hashlib.sha256(part.encode()).hexdigest(),'meteringRestorationVerified':True});(out/'result.json').write_text(json.dumps(v,indent=2),encoding='utf-8');print(json.dumps({'ok':v['ok'],'checks':v['checks'],'cases':len(v['cases']),'failures':v['failures'][:5],'normalRequestGenericParses':v['normalRequestGenericParses']}));return r.returncode
if __name__=='__main__':sys.exit(main())
