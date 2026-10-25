from pathlib import Path
import hashlib,json,subprocess,sys
REPO=Path(__file__).resolve().parent.parent;TOOLS=REPO/'tools'
if len(sys.argv) not in [2,3]:raise SystemExit('usage: fan_power_http_transport_selftest.py EVIDENCE_ROOT [NATIVE_SOURCE]')
out=Path(sys.argv[1]).resolve();source=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else REPO/'native/main.cpp'
out.mkdir(parents=True,exist_ok=True)
s=source.read_text(encoding='utf-8-sig').replace('\r\n','\n')
def block(start,end):
 a=s.index(start);b=s.index(end,a+len(start));return s[a:b]
parts=[block('static bool isFanLeaseIdValid(', 'static std::string fanApiPathFromObject('),block('static std::string fanHostResponseRequestId(HINTERNET request) {','// 同类入口')]
if 'struct FanBoundaryHttpReply {' in s:parts.append(block('struct FanBoundaryHttpReply {','struct FanBoundaryEvidence {'))
parts.extend([block('struct FanBoundaryEvidence {','static bool fanHostEmergencyPost('),block('static bool fanHostEmergencyPost(','// ── Q1'),block('static bool fanHostEmergencyGetState(','static bool fanHostEmergencySuspend('),block('static bool fanHostEmergencySuspend(','/**\n * Main-process exit'),block('using FanResumeHostReply =','static void fanHostEmergencyResume(unsigned long long generation) {')])
a=s.index('static void fanHostEmergencyResume(unsigned long long generation) {');b=s.index('\nstatic ',a+20);parts.append(s[a:b])
extract='\n'.join(parts);port='WinHttpConnect(session, L"127.0.0.1", 8765, 0)';assert extract.count(port) in [1,2],extract.count(port);replaced=extract.replace(port,'WinHttpConnect(session, L"127.0.0.1", fixturePort, 0)')
(out/'PowerExtract.inc').write_text(replaced,encoding='utf-8');(out/'source-binding.json').write_text(json.dumps({'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'extractSha256':hashlib.sha256(extract.encode()).hexdigest(),'replacements':'Fan loopback port only; source includes actual safe-release classifier, POST/GET, Suspend and full native resume owner/worker; fake identity/token/log sinks, real WinHTTP','files':[str(source)]},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
cmd='@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /wd4189 /wd4505 /MT /I"'+str(REPO/'native')+'" /I"'+str(REPO/'deps/json')+'" /I"'+str(out)+'" "'+str(TOOLS/'fan_power_http_transport_selftest.cpp')+'" /Fo"'+str(out/'fixture.obj')+'" /Fe"'+str(out/'fixture.exe')+'"\n'
(out/'compile.cmd').write_text(cmd,encoding='ascii');p=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace');(out/'compile.txt').write_text(p.stdout,encoding='utf-8');print(p.stdout[-3500:]);
if p.returncode:raise SystemExit(p.returncode)
bind=json.loads((out/'source-binding.json').read_text(encoding='utf-8'));bind['headers']={n:hashlib.sha256((REPO/'native'/n).read_bytes()).hexdigest() for n in ['http_request_deadline.h','fan_http_body_contract.h','fan_resume_evidence.h','fan_resume_owner.h']};(out/'source-binding.json').write_text(json.dumps(bind,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
p=subprocess.run(['node',str(TOOLS/'fan_power_http_transport_selftest.mjs'),str(out)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace');(out/'run.txt').write_text(p.stdout,encoding='utf-8');print(p.stdout);
current=source.read_text(encoding='utf-8-sig').replace('\r\n','\n');assert all(part in current for part in parts),'a bound Fan production function changed during the gate'
assert all(hashlib.sha256((REPO/'native'/n).read_bytes()).hexdigest()==h for n,h in bind['headers'].items()),'a bound Fan header changed during the gate'
sys.exit(p.returncode)
