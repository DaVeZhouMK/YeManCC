'''Compile finite callback ownership proof using current production helpers.
No production startup, pipe, SDK, timer registration or hardware operation.
'''
from pathlib import Path
import argparse,datetime,hashlib,json,subprocess
from virtual_sample_metadata_perf_selftest import matching_brace
ROOT=Path(__file__).resolve().parent.parent
SHA=lambda b:hashlib.sha256(b).hexdigest()
def block(text,marker):
 assert text.count(marker)==1,marker
 start=text.index(marker);end=matching_brace(text,text.index('{',start))+1
 return text[start:end]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--before',type=Path,required=True);a=p.parse_args();run=a.output/datetime.datetime.now().strftime('%Y%m%d-%H%M%S');run.mkdir(parents=True)
 raw=(ROOT/'native/main.cpp').read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n')
 markers=['static inline unsigned long long tickCostEpochUs(std::chrono::steady_clock::time_point point)','static inline callbackCost::CycleSample tickCostThreadCycleSample()','static inline void tickCostRecordPreamble(','static json tickCostCallbackWindowJson()']
 parts=[block(text,m) for m in markers]
 before=a.before.read_text(encoding='utf-8-sig').replace('\r\n','\n')
 accumulate=block(text,'static inline void tickCostAccumulate(')
 globals=text[text.index('namespace callbackCost ='):text.index('// 跳过原因计数',text.index('namespace callbackCost ='))]
 state='''namespace callbackCost = ymcc::input::callbackCost;
static std::atomic<bool> costGate{true};
enum TickCostStage{kTickCostPreamble,kTickCostPreambleCpu,kCount};
static std::atomic<unsigned long long> g_tickCostSumUs[kCount],g_tickCostMaxUs[kCount],g_tickCostCount[kCount];
static inline bool tickCostEnabled(){return costGate.load(std::memory_order_acquire);}
static inline unsigned long long tickCostCyclesToUs(unsigned long long value){return value/4;}
'''+globals.replace('namespace callbackCost = ymcc::input::callbackCost;\n','')+'\n'+accumulate
 oldStart=before[before.index('    if (tickCostEnabled()) {',before.index('static void CALLBACK inputCaptureTimerEventProc(')):before.index('    // QPC 相位步进',before.index('static void CALLBACK inputCaptureTimerEventProc('))]
 oldEnd=block(before, '    if (tickCostEnabled()) {\n        const unsigned long long cbStart =')
 newStart=text[text.index('    const callbackCost::Scope tickCostScope',text.index('static void CALLBACK inputCaptureTimerEventProc(')):text.index('    // QPC 相位步进',text.index('static void CALLBACK inputCaptureTimerEventProc('))]
 newEnd='    tickCostRecordPreamble(frameStarted);'
 prefix='static std::atomic<unsigned long long> g_tickCostCallbackStartUs{0},g_tickCostCallbackStartCycles{0};\nstatic volatile unsigned long long benchmarkSink=0;\nstatic inline unsigned long long tickCostEpochUs(){return tickCostEpochUs(std::chrono::steady_clock::now());}\nstatic inline unsigned long long tickCostThreadCycles(){unsigned long long c=0;return QueryThreadCycleTime(GetCurrentThread(),&c)?c:0;}\n'
 bench=prefix
 for name,start,end in [('baselineTick',oldStart,oldEnd),('currentTick',newStart,newEnd)]:
  bench+='static __declspec(noinline) void '+name+'(){\n    const auto tickCostStarted=std::chrono::steady_clock::now();\n'+start+'\n    const auto frameStarted=std::chrono::steady_clock::now();\n'+end+'\n    benchmarkSink=tickCostEpochUs(tickCostStarted)+tickCostEpochUs(frameStarted);\n}\n'
 (run/'benchmark_helpers.hpp').write_text(bench,encoding='utf-8')
 (run/'helper_state.hpp').write_text(state,encoding='utf-8');(run/'actual_helpers.hpp').write_text('\n'.join(parts),encoding='utf-8')
 inputs=['native/main.cpp','native/input_callback_cost.h','deps/json/json.hpp','tools/virtual_callback_cost_selftest.cpp','tools/virtual_callback_cost_selftest.py','tools/virtual_sample_metadata_perf_selftest.py']
 rows=[{'path':x,'sha256':SHA((ROOT/x).read_bytes())} for x in inputs]
 for src,name in [('native/input_callback_cost.h','input_callback_cost.h'),('deps/json/json.hpp','json.hpp'),('tools/virtual_callback_cost_selftest.cpp','fixture.cpp')]: (run/name).write_bytes((ROOT/src).read_bytes())
 vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat');profiles=[]
 for profile,options in [('Release',['/O2','/DNDEBUG']),('Checked',['/Od','/RTC1'])]:
  exe=run/(profile+'.exe');cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*options,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(exe)]
  bat=run/(profile+'-build.bat');bat.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n',encoding='utf-8')
  built=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120);(run/(profile+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8');assert built.returncode==0,str(run/(profile+'-build.log'))
  print(profile+' compiled',flush=True)
  result=subprocess.run([str(exe),'benchmark' if profile=='Release' else 'verify'],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60);(run/(profile+'-stderr.log')).write_text(result.stderr,encoding='utf-8');assert result.returncode==0,result.stderr
  data=json.loads(result.stdout);assert data['pass'];(run/(profile+'-result.json')).write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8');profiles.append({'profile':profile,**data});print(profile+' '+str(data['checks'])+' checks',flush=True)
 stable=all(SHA((ROOT/r['path']).read_bytes())==r['sha256'] for r in rows);assert stable
 manifest={'sourceFiles':rows,'beforeMainSha256':SHA(a.before.read_bytes()),'actualAccumulateSha256':SHA(accumulate.encode()),'benchmarkEndpointFragmentsSha256':SHA(bench.encode()),'sourceStableDuringRun':stable,'actualHelperFragmentHashes':[{'marker':m,'sha256':SHA(s.encode())} for m,s in zip(markers,parts)],'scope':'Windows cycle APIs only in benchmark; diagnostic recorder OS providers replaced in correctness fixture; no production Main or control calls'}
 data={'pass':True,'runDirectory':str(run),'manifest':manifest,'profiles':profiles,'hardwareRun':False,'wholeProcessCpuProof':False}
 (run/'result.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8');(a.output/'final-result.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8');print(run)
if __name__=='__main__':main()
