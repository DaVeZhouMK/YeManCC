"""Production log-boundary regression with fake DI/HID and one-shot bad_alloc."""
from pathlib import Path
import argparse,datetime,hashlib,json,subprocess,sys
from log_frequency_selftest import ROOT,extract

SIGS=['static json msiDinputEnumerationReceipt(', 'static void msiDinputEnumerationHidMetadata(', 'static bool msiDinputOpen() {']

def parts(raw):
 source=raw.decode('utf-8-sig').replace('\r\n','\n');result=[]
 for sig in SIGS:
  start=source.index(sig);head=source[start:source.index('{',start)+1];result.append(extract(source,head))
 function=result[2];start=function.index('    struct Ctx {');ctx=extract(function,'    struct Ctx {')+';'
 start=function.index('[](const DIDEVICEINSTANCEW* inst, VOID* p) -> BOOL {');callback=extract(function,'[](const DIDEVICEINSTANCEW* inst, VOID* p) -> BOOL {')
 command_start=source.index('static void inputHostCommandResultLog(');command_head=source[command_start:source.index('{',command_start)+1]
 return result[:2]+[ctx,'static auto callback='+callback+';',extract(source,command_head)]

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--baseline',type=Path,required=True);parser.add_argument('--output-dir',type=Path,required=True);args=parser.parse_args()
 out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);run=out/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir()
 before=args.baseline.read_bytes();now=(ROOT/'native/main.cpp').read_bytes();blocks={'before':parts(before),'after':parts(now)}
 header=''.join('\nnamespace '+key+' {\n'+'\n'.join(body)+'\n}\n' for key,body in blocks.items());(run/'production-extracted.h').write_text(header,encoding='utf-8')
 inputs={'tools/log_control_independence_selftest.cpp':'fixture.cpp','native/msi_physical_name_evidence.h':'msi_physical_name_evidence.h','deps/json/json.hpp':'json.hpp'}
 hashes={}
 for name,target in inputs.items():
  data=(ROOT/name).read_bytes();(run/target).write_bytes(data);hashes[name]=hashlib.sha256(data).hexdigest()
 (run/'before-main.cpp').write_bytes(before);(run/'current-main.cpp').write_bytes(now)
 profiles=[];vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
 for profile,flags in [('Release',['/O2','/MT','/DNDEBUG']),('Checked',['/Od','/RTC1','/MTd','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2'])]:
  exe=run/(profile+'.exe');command=['cl','/nologo','/std:c++20','/utf-8','/EHsc',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(exe)]
  batch=run/(profile+'.cmd');batch.write_text('@echo off\ncall "'+str(vc)+'" >nul\n'+subprocess.list2cmdline(command)+'\n',encoding='utf-8')
  built=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace');(run/(profile+'-compile.log')).write_text(built.stdout+built.stderr,encoding='utf-8')
  if built.returncode:print(built.stdout+built.stderr);return built.returncode
  result=subprocess.run([str(exe)],cwd=run,capture_output=True,text=True,errors='replace');(run/(profile+'-result.json')).write_text(result.stdout,encoding='utf-8')
  report=json.loads(result.stdout);profiles.append({'profile':profile,'checks':len(report['checks']),'failures':report['failures']})
 current=(ROOT/'native/main.cpp').read_bytes();stable=parts(current)==blocks['after'] and all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h for n,h in hashes.items())
 summary={'profiles':profiles,'failures':sum(p['failures'] for p in profiles)+(not stable),'boundSourceAndDependenciesStable':stable,'wholeMainStable':current==now,'sourceSha256':hashlib.sha256(now).hexdigest(),'baselineSha256':hashlib.sha256(before).hexdigest(),'dependencies':hashes,'runDirectory':str(run),'productLaunched':False,'hardwareCalls':0}
 (out/'report.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');print(json.dumps(summary,indent=2));return 3 if summary['failures'] else 0
if __name__=='__main__':raise SystemExit(main())
