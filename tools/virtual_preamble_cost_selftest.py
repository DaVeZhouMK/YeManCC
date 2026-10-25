#!/usr/bin/env python3
"""R36 actual-source elapsed-scope correctness. Offline clock/last-error models only."""
from pathlib import Path
import datetime,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R36-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(data):return hashlib.sha256(data).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
    raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig')
    markers=['enum TickCostStage','static inline bool tickCostEnabled(','static inline void tickCostAccumulate(','static inline unsigned long long tickCostElapsedUs(','class TickPreambleElapsedScope']
    parts={m:source_function(src,m) for m in markers}
    names=re.search(r'static const char\* const kTickCostStageNames\[kTickCostStageCount\] = \{[\s\S]*?\};',src).group()
    declarations='\n'.join(re.search(r'static std::atomic<unsigned long long> '+name+r'\[kTickCostStageCount\];',src).group() for name in ['g_tickCostSumUs','g_tickCostMaxUs','g_tickCostCount'])
    actual=parts[markers[0]]+';\n'+names+'\n'+declarations+'\n'+'\n'.join(parts[m]+(';' if m.startswith('class ') else '') for m in markers[1:])
    # Only the qualified clock type is renamed to a stand-in; helper bodies are otherwise intact.
    generated=actual.replace('std::chrono::steady_clock','FixtureClock')
    run=OUT/('scope-selftest-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
    (run/'actual-scope-original.hpp').write_bytes(actual.encode('utf-8'))
    (run/'actual-scope.hpp').write_bytes(generated.encode('utf-8'))
    (run/'fixture.cpp').write_bytes((REPO/'tools/virtual_preamble_cost_selftest.cpp').read_bytes())
    profiles=[]
    for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
        batch=run/(name+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8')
        p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180)
        (run/(name+'-build.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        if p.returncode:raise RuntimeError('Build '+name+' failed: '+str(run))
        p=subprocess.run([str(run/(name+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=60)
        (run/(name+'-result.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-stderr.log')).write_text(p.stderr,encoding='utf-8')
        if p.returncode:raise RuntimeError(name+' failed: '+p.stderr)
        result=json.loads(p.stdout);assert result['pass'];profiles.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks',flush=True)
    now=(REPO/'native/main.cpp').read_bytes();current=now.decode('utf-8-sig')
    assert all(source_function(current,m)==f for m,f in parts.items()),'Measured source scope changed'
    manifest=json.loads((OUT/'production-patch-manifest.json').read_text(encoding='utf-8-sig'));before=(OUT/'production-before-main.cpp').read_bytes().decode('utf-8-sig');candidate=(OUT/'candidate-main.cpp').read_bytes().decode('utf-8-sig')
    controlled=['static void inputCaptureBusTick()','static void inputCaptureOnGamepad(','static void emitTickCostWindow(']
    for sig in controlled:assert source_function(current,sig)==source_function(candidate,sig),'instrumented function changed: '+sig
    for row in manifest['protectedFunctions']:assert sha(source_function(current,row['signature']).encode('utf-8'))==row['sha256'],row['signature']
    reverse=candidate
    for change in reversed(manifest['patchChanges']):
        sig=change['scope'];target=reverse if sig is None else source_function(reverse,sig)
        assert target.count(change['after'])==1
        updated=target.replace(change['after'],change['before'],1)
        reverse=updated if sig is None else reverse.replace(target,updated,1)
    assert reverse==before,'old control bytes not recovered exactly'
    binding={'pass':True,'sourceSha256AtStart':sha(raw),'sourceSha256AtEnd':sha(now),'wholeSourceStable':raw==now,'actualMeasuredScopesStable':True,'actualHelperHashes':{m:sha(f.encode('utf-8')) for m,f in parts.items()},'oldControlBytesExactReversal':True,'protectedFunctionsVerified':len(manifest['protectedFunctions']),'oldStageIndicesRetained':18,'appendedStages':6,'clockAndLastErrorAreModels':True,'realProductOrInputCalls':False,'wholeProcessCpuOrHardwareAcceptance':False,'artifacts':{p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file()}}
    save(run/'source-binding.json',binding);save(run/'result.json',{'pass':True,'profiles':profiles,'binding':binding});save(OUT/'latest-selftest.json',{'pass':True,'run':str(run),'profiles':profiles,'sourceSha256':sha(now)})
    print(json.dumps({'pass':True,'run':str(run),'sourceSha256':sha(now)}))
if __name__=='__main__':main()
