#!/usr/bin/env python3
from __future__ import annotations
import argparse,datetime,difflib,hashlib,json,re,subprocess
from pathlib import Path
REPO=Path(__file__).resolve().parents[1]; WORKSPACE=REPO.parents[2]; SOURCE=REPO/'native/main.cpp'; JSON_HPP=REPO/'deps/json/json.hpp'; HARNESS=REPO/'tools/virtual_testlane_live_perf_selftest.cpp'; OUT=WORKSPACE/'Build/Validation/VirtualInputPerf-R19-20261015/testlane-local'; EXPECTED='2c745ba4badccf691b1468193600e66c01f4b239b8f137dd37427f0396ee3d75'
H=lambda b:hashlib.sha256(b).hexdigest()
def close(t,o):
 d=0;q=None;e=False
 for i in range(o,len(t)):
  c=t[i]
  if q:
   if e:e=False
   elif c=='\\':e=True
   elif c==q:q=None
  elif c=='"':q='"'
  elif c=="'":q="'"
  elif c=='{':d+=1
  elif c=='}':
   d-=1
   if d==0:return i
 raise ValueError('brace')
def split_top(t):
 out=[];a=0;d=0;q=None;e=False
 for i,c in enumerate(t):
  if q:
   if e:e=False
   elif c=='\\':e=True
   elif c==q:q=None
  elif c in '"\'':q=c
  elif c in '{([':d+=1
  elif c in '})]':d-=1
  elif c==',' and d==0:out.append(t[a:i].strip());a=i+1
 out.append(t[a:].strip());return [x for x in out if x]
def extract(text):
 marker='        json testLaneLiveObj = {';s=text.index(marker);o=text.index('{',s);e=close(text,o);block=text[s:e+2];entries=split_top(text[o+1:e]);assert len(entries)==9
 pairs=[]
 for x in entries:
  m=re.match(r'\{"([^"]+)",\s*(.*)\}\s*$',x,re.S);assert m,x;pairs.append((m.group(1),m.group(2)))
 hs=text.rfind('static const char* inputCaptureMotionProofId(',0,s);ho=text.index('{',hs);he=close(text,ho);return {'block':block,'entries':pairs,'helper':text[hs:he+1],'line':text[:s].count('\n')+1}
def candidate(entries):
 return '\n'.join(['        json testLaneLiveObj = json::object();']+[f'        testLaneLiveObj["{k}"] = {v};' for k,v in entries])
def header(parts):
 helper=parts['helper'].replace('inputCaptureMotionProofId','inputCaptureMotionProofIdInstrumented',1); helper=re.sub(r'\{\r?\n','{\n    ++g_proofCalls;\n',helper,count=1)
 base=parts['block'];cand=candidate(parts['entries'])
 def fn(name,body):
  return f'''static NOINLINE json {name}(const Input& in) {{
    bind(in);
    bool motionTriggered=in.motionTriggered;
    bool pairProven=in.pairProven;
    bool calibrationPairProven=in.calibrationPairProven;
    bool sameProvider=in.sameProvider;
    bool selectorBound=in.selectorBound;
    bool hcMatrixAdmitted=in.hcMatrixAdmitted;
{body.replace('inputCaptureMotionProofId(', 'inputCaptureMotionProofIdInstrumented(')}
    return testLaneLiveObj;
}}
'''
 return f'''#include "json.hpp"
using json = nlohmann::json;
#define NOINLINE __declspec(noinline)
#include <bit>
#include <cstdint>
#include <string>
#include <type_traits>
struct TestConfig {{ bool motionTriggerResolved=false, aimingSightsTriggerResolved=false, standaloneTestLane=false; }};
struct Selector {{ std::string selectorId; }};
struct Input {{ bool motionTriggerResolved=false, aimingSightsTriggerResolved=false, motionTriggered=false, pairProven=false, calibrationPairProven=false, calibrationOnlyProcess=false, sameProvider=false, selectorBound=false, gmProcessed=false, hcMatrixAdmitted=false, standaloneTestLane=false; std::string selectorId; }};
static thread_local TestConfig g_realStickTest; static thread_local Selector g_hcMotionSelector; static thread_local bool g_gmProcessedThisTick=false; static thread_local unsigned long long g_proofCalls=0;
static void bind(const Input& in) {{ g_realStickTest.motionTriggerResolved=in.motionTriggerResolved; g_realStickTest.aimingSightsTriggerResolved=in.aimingSightsTriggerResolved; g_realStickTest.standaloneTestLane=in.standaloneTestLane; g_hcMotionSelector.selectorId=in.selectorId; g_gmProcessedThisTick=in.gmProcessed; }}
{helper}
{fn('baselineLane',base)}
{fn('candidateLane',cand)}
'''
def prepare():
 raw=SOURCE.read_bytes();assert H(raw)==EXPECTED;parts=extract(raw.decode('utf-8'));new=candidate(parts['entries']);old=parts['block'];assert raw.decode('utf-8').replace(old,new,1).count(new)==1
 run=OUT/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir();(run/'generated.hpp').write_text(header(parts),encoding='utf-8');(run/'fixture.cpp').write_bytes(HARNESS.read_bytes());(run/'json.hpp').write_bytes(JSON_HPP.read_bytes());(run/'source-main.cpp').write_bytes(raw);(run/'testlane.before.cpp').write_text(old,encoding='utf-8');(run/'testlane.candidate.cpp').write_text(new,encoding='utf-8');(run/'proof-helper.cpp').write_text(parts['helper'],encoding='utf-8');(run/'sample.json').write_text('{"imu":{"gx":0.1}}\n',encoding='utf-8');(run/'binding.json').write_text(json.dumps({'sourceSha256':H(raw),'line':parts['line'],'blockSha256':H(old.encode()),'candidateSha256':H(new.encode()),'helperSha256':H(parts['helper'].encode()),'keys':[k for k,_ in parts['entries']],'productionWriteSet':[]},indent=2)+'\n');return run
def compile_run(run,profile):
 opts=['/O2','/DNDEBUG','/MT'] if profile=='release' else ['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'];vc=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat';exe=run/(profile+'.exe');cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*opts,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(exe)];bat=run/(profile+'-build.bat');bat.write_text('@echo off\ncall "'+vc+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n',encoding='utf-8');r=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180);(run/(profile+'-build.log')).write_text(r.stdout+r.stderr,encoding='utf-8');assert r.returncode==0,(profile,r.stdout+r.stderr);return exe
def invoke(exe,run,*args):
 r=subprocess.run([str(exe),str(run/'sample.json'),*args],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=900);assert r.returncode==0,r.stderr;return json.loads(r.stdout)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--phase',choices=['correctness','benchmark'],default='correctness');ap.add_argument('--run-dir',type=Path);args=ap.parse_args();OUT.mkdir(parents=True,exist_ok=True)
 if args.phase=='correctness':
  run=prepare();profiles={}
  for prof in ('release','checked'):
   exe=compile_run(run,prof);profiles[prof]=invoke(exe,run,'correctness');(run/(prof+'-correctness.json')).write_text(json.dumps(profiles[prof],indent=2)+'\n')
  result={'status':'READY_FOR_CAMPAIGNS','pass':True,'runDirectory':str(run),'profiles':profiles,'sourceSha256':H(SOURCE.read_bytes()),'productionModified':False,'wholeProcessClaim':False};(run/'correctness-result.json').write_text(json.dumps(result,indent=2)+'\n');(OUT/'latest-correctness.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return
 run=args.run_dir or Path(json.loads((OUT/'latest-correctness.json').read_text())['runDirectory']);bind=json.loads((run/'binding.json').read_text());assert H(SOURCE.read_bytes())==bind['sourceSha256'];profiles={};overall=True
 for prof in ('release','checked'):
  exe=run/(prof+'.exe');rows=[]
  for campaign in (0,1):
   v=invoke(exe,run,'benchmark',str(campaign));rows.append(v);(run/(prof+f'-campaign-{campaign}.json')).write_text(json.dumps(v,indent=2)+'\n');overall=overall and all(x['pass'] for x in v['rows'])
  profiles[prof]=rows
 result={'status':'ACCEPTED' if overall else 'REJECTED_BY_FIXED_CRITERIA','pass':overall,'runDirectory':str(run),'profiles':profiles,'sourceSha256':H(SOURCE.read_bytes()),'productionModified':False,'wholeProcessClaim':False};(run/'result.json').write_text(json.dumps(result,indent=2)+'\n');(OUT/'latest-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
