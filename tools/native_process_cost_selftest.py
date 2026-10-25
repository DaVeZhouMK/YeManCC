"""Isolated CPU/memory diagnostic state and exact Windows adapter proof.

Only fixture binaries execute. The real adapter test reads this fixture process.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,subprocess
from virtual_sample_metadata_perf_selftest import matching_brace
ROOT=Path(__file__).resolve().parent.parent
SHA=lambda b:hashlib.sha256(b).hexdigest()
def adapter(text):
 marker='static json inputNativeProcessCostSnapshot(bool enabled) noexcept {'
 start=text.index(marker);opening=text.index('{',start)
 return text[start:matching_brace(text,opening)+1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate-adapter',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output/('candidate-' if a.candidate_adapter else 'final-')/datetime.datetime.now().strftime('%Y%m%d-%H%M%S');out.mkdir(parents=True)
 raw=(ROOT/'native/main.cpp').read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n');fn=a.candidate_adapter.read_text(encoding='utf-8-sig').strip() if a.candidate_adapter else adapter(text)
 header=(ROOT/'native/native_process_cost.h').read_bytes();harness=(ROOT/'tools/native_process_cost_selftest.cpp').read_bytes()
 (out/'adapter-extracted.h').write_text(fn+'\n',encoding='utf-8');(out/'fixture.cpp').write_bytes(harness);(out/'native_process_cost.h').write_bytes(header)
 binding={'sourceSha256':SHA(raw),'adapterSha256':SHA(fn.encode()),'headerSha256':SHA(header),'harnessSha256':SHA(harness),'candidateOnly':bool(a.candidate_adapter),'productionAdapterInOriginal1HzGate':False}
 if not a.candidate_adapter:
  marker='static void inputHostDisplayIdleAuditTick() {';s=text.index(marker);op=text.index('{',s);idle=text[s:matching_brace(text,op)+1]
  assert idle.count('inputNativeProcessCostSnapshot(true)')==1
  assert idle.index('if(!enabled)inputNativeProcessCostSnapshot(false);')<idle.index('if(!enabled || (lastAudit && now-lastAudit<1000))return;')
  assert idle.index('if(!enabled || (lastAudit && now-lastAudit<1000))return;')<idle.index('inputNativeProcessCostSnapshot(true)')<idle.index('appendDomainLog("virtual","input-idle-window",row);')
  binding['productionAdapterInOriginal1HzGate']=True
 results=[];vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
 for profile,opts in [('release',['/O2','/DNDEBUG','/MT']),('checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  exe=out/(profile+'.exe');cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc',*opts,str(out/'fixture.cpp'),'/I'+str(out),'/I'+str(ROOT/'deps/json'),'/Fo'+str(out/(profile+'.obj')),'/Fe'+str(exe),'/link','psapi.lib']
  batch=out/(profile+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n',encoding='utf-8')
  ran=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120);(out/(profile+'-build.log')).write_text(ran.stdout+ran.stderr,encoding='utf-8')
  if ran.returncode:raise RuntimeError('Compilation failed: '+str(batch))
  ran=subprocess.run([str(exe)],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45);(out/(profile+'-result.json')).write_text(ran.stdout,encoding='utf-8');(out/(profile+'-stderr.txt')).write_text(ran.stderr,encoding='utf-8')
  if ran.returncode:raise RuntimeError('Fixture failed: '+ran.stderr)
  row=json.loads(ran.stdout);row['profile']=profile;results.append(row)
 if not a.candidate_adapter:assert adapter((ROOT/'native/main.cpp').read_text(encoding='utf-8-sig'))==fn
 assert SHA((ROOT/'native/native_process_cost.h').read_bytes())==binding['headerSha256']
 receipt={'pass':True,'run':str(out),'binding':binding,'profiles':results,'scope':'Fake OS result/failure/state transitions and fixture own-process read only; no product or device execution','currentSourceWholeSha256':SHA((ROOT/'native/main.cpp').read_bytes())}
 (a.output/('candidate-result.json' if a.candidate_adapter else 'result.json')).write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps({'pass':True,'candidateOnly':bool(a.candidate_adapter),'profiles':[{'profile':r['profile'],'checks':r['checks'],'realOwnProcessReadUs':r['realOwnProcessRead']['oneObservedReadUs']} for r in results]},indent=2))
if __name__=='__main__':main()
