from pathlib import Path
import hashlib,json,subprocess,sys
REPO=Path(__file__).resolve().parent.parent;TOOLS=REPO/'tools'
if len(sys.argv) not in [2,3,4,5]:raise SystemExit('usage: fan_power_http_transport_selftest.py EVIDENCE_ROOT [NATIVE_SOURCE] [--shared-power-only|--shared-owner-only|--resume-existing-only|--compile-only]')
extra=sys.argv[2:];case=extra.pop() if len(extra)>1 and extra[-2] in ['--request-lifetime-only','--resume-existing-only'] else None;selection=next((v for v in extra if v.startswith('--')),None);positional=[v for v in extra if not v.startswith('--')]
if len(positional)>1 or any(v.startswith('--') and v not in ['--shared-power-only','--shared-owner-only','--resume-existing-only','--request-lifetime-only','--compile-only'] for v in extra) or sum(v.startswith('--') for v in extra)>1:raise SystemExit('invalid explicit power transport selection')
if case is not None:
 allowed={'--request-lifetime-only':{'business-scheduler-zero-action','business-terminal','business-awaiting-old-superseded','business-unknown','business-other-request','business-running-global-ready','business-duplicate-key'},'--resume-existing-only':{'resume-truncated-then-valid-observation','resume-post-only-truncated-then-ready','failed-observation-next-request-still-reachable','unknown500-no-post-repeat'}}
 if not case or case not in allowed.get(selection,set()):raise SystemExit('invalid exact existing Fan case')
out=Path(sys.argv[1]).resolve();source=Path(positional[0]).resolve() if positional else REPO/'native/main.cpp'
out.mkdir(parents=True,exist_ok=True)
s=source.read_text(encoding='utf-8-sig').replace('\r\n','\n')
def block(start,end):
 a=s.index(start);b=s.index(end,a+len(start));return s[a:b]
generated=subprocess.run(['node',str(TOOLS/'fixtures/fan_power_http_transport/extract.mjs'),str(REPO),str(out),str(source)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
if generated.returncode:print(generated.stdout);raise SystemExit(generated.returncode)
binding=json.loads((out/'SOURCE-BINDING.json').read_text(encoding='utf-8'));parts=[s[item['begin']:item['end']] for item in binding['segments']]
(out/'source-binding.json').write_text(json.dumps({'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'generatedBinding':binding,'files':[str(source)]},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
cmd='@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /wd4189 /wd4505 /MT /I"'+str(REPO/'native')+'" /I"'+str(REPO/'deps/json')+'" /I"'+str(out)+'" "'+str(TOOLS/'fan_power_http_transport_selftest.cpp')+'" /Fo"'+str(out/'fixture.obj')+'" /Fe"'+str(out/'fixture.exe')+'"\n'
(out/'compile.cmd').write_text(cmd,encoding='ascii');p=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace');(out/'compile.txt').write_text(p.stdout,encoding='utf-8');print(p.stdout[-3500:]);
if p.returncode:raise SystemExit(p.returncode)
bind=json.loads((out/'source-binding.json').read_text(encoding='utf-8'));bind['headers']={n:hashlib.sha256((REPO/'native'/n).read_bytes()).hexdigest() for n in ['http_request_deadline.h','fan_http_body_contract.h','fan_physical_wake.h','fan_response_contract.h','fan_resume_business.h','fan_resume_evidence.h','fan_resume_owner.h','sleep_power_dispatch.h','sleep_power_remote_transport.h','sleep_power_state.h','sleep_power_observation.h','sleep_power_revocation.h']};(out/'source-binding.json').write_text(json.dumps(bind,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if selection=='--compile-only':
 current=source.read_text(encoding='utf-8-sig').replace('\r\n','\n');assert all(part in current for part in parts),'a bound Fan production function changed during compilation'
 print('compile-only: original Native fixture compiled; no scenario producer executed');raise SystemExit(0)
p=subprocess.run(['node',str(TOOLS/'fan_power_http_transport_selftest.mjs'),str(out)]+([selection] if selection else [])+([case] if case else []),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace');(out/'run.txt').write_text(p.stdout,encoding='utf-8');print(p.stdout);
current=source.read_text(encoding='utf-8-sig').replace('\r\n','\n');assert all(part in current for part in parts),'a bound Fan production function changed during the gate'
assert all(hashlib.sha256((REPO/'native'/n).read_bytes()).hexdigest()==h for n,h in bind['headers'].items()),'a bound Fan header changed during the gate'
sys.exit(p.returncode)
