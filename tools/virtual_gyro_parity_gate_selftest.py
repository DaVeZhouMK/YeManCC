"""Actual owned-gyro diagnostic gate regression; no live app/device actions.
Rejects missing-reference parity errors while retaining dump validation, target writes,
gate reads, exception points and cost-scope lifetime. Reuses a frozen inert R15 fixture.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,importlib.util,subprocess,shutil
P=Path(__file__).resolve().parents[1];E=Path(r"G:/YeManCC-Work/Build/Validation/VirtualInputParityGate-20261015")
F=Path(r"G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R15-20261015/gyro-envelope-local/run-20261015-061152")
H=lambda b:hashlib.sha256(b).hexdigest()
def save(path, data):path.write_text(json.dumps(data,indent=2)+"\n",encoding="utf-8")
def replacement(old):
    a=b'    if (tickCostEnabled()) {\r\n        g_c17aParityChecks.fetch_add(1, std::memory_order_relaxed);\r\n        if (m.dump() != referenceWire) {\r\n            g_c17aParityFailures.fetch_add(1, std::memory_order_relaxed);\r\n            appendNativeLifecycleLog("c17a-gyro-wire-parity-mismatch", {});\r\n        }\r\n    }'
    b=b'    if (tickCostEnabled()) {\r\n        if (referenceWire.empty()) {\r\n            // The gate reopened without a reference. Preserve dump validation\r\n            // and its exception point, but there is no parity comparison to count.\r\n            (void)m.dump();\r\n        } else {\r\n            g_c17aParityChecks.fetch_add(1, std::memory_order_relaxed);\r\n            if (m.dump() != referenceWire) {\r\n                g_c17aParityFailures.fetch_add(1, std::memory_order_relaxed);\r\n                appendNativeLifecycleLog("c17a-gyro-wire-parity-mismatch", {});\r\n            }\r\n        }\r\n    }'
    assert old.count(a)==1,"baseline gate block drift"
    return old.replace(a,b),a,b

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--final',action='store_true');args=ap.parse_args()
    E.mkdir(parents=True,exist_ok=True);run=E/(('final-' if args.final else 'candidate-')+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir()
    spec=importlib.util.spec_from_file_location('frozen_r15_extractor',F/'runner.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
    before=(F/'baseline-main.cpp').read_bytes();baselineBound=r.bind(before);old=baselineBound['owned-emitter']['bytes'];candidate,a,b=replacement(old)
    current=(P/'native/main.cpp').read_bytes();bound=r.bind(current)
    for name,piece in bound.items():
        expected=candidate if name=='owned-emitter' and args.final else baselineBound[name]['bytes']
        assert piece['bytes']==expected,name+' drift'
    compiled=bound['owned-emitter']['bytes'] if args.final else candidate
    shared=['diagnostic-gate','cost-name-hash','ipc-cost-scope','utf8-to-utf16','target-resolver','target-post']
    header=b'// Actual production bodies with inert bindings; observation hooks are test-only.\n'+b''.join(bound[k]['bytes'] for k in shared)
    for label,body in [('baseline_emit',old),('candidate_emit',compiled)]:
        header+=body.replace(b'ipc_emit_owned_gyro_telemetry',label.encode(),1)
    needle=b'    json m = {{"event", std::move(ev)}, {"data", std::move(data)}};'
    for label,body in [('baseline_observed',old),('candidate_observed',compiled)]:
        assert body.count(needle)==1
        instrumented=body.replace(needle,needle+b'\r\n    observeLocals(ev, data, m);',1)
        header+=instrumented.replace(b'ipc_emit_owned_gyro_telemetry',label.encode(),1)
    (run/'production-extracted.h').write_bytes(header)
    (run/'baseline-main.cpp').write_bytes(before);(run/'source-main.cpp').write_bytes(current)
    (run/'actual-or-candidate-function.cpp').write_bytes(compiled)
    snapshots={}
    for rel,target in [('deps/json/json.hpp','json.hpp'),('tools/virtual_gyro_parity_gate_selftest.cpp','fixture.cpp'),('tools/virtual_gyro_parity_gate_selftest.py','runner.py')]:
        data=(P/rel).read_bytes();snapshots[rel]={'sha256':H(data),'bytes':len(data)};(run/target).write_bytes(data)
    manifest={'mode':'final' if args.final else 'candidate','currentMainSha256':H(current),'baselineMainSha256':H(before),'baselineFunctionSha256':H(old),'candidateFunctionSha256':H(candidate),'compiledFunctionSha256':H(compiled),'inputs':snapshots,'productionBindings':{k:H(v['bytes']) for k,v in bound.items()},'generatedHeaderSha256':H(header),'scope':'Only availability guard in owned gyro parity diagnostic; always dump at the old parity gate; no broadcast/control change','oldGateBlock':a.decode(),'newGateBlock':b.decode(),'productionEditedByRunner':False}
    save(run/'manifest.json',manifest)
    profiles=[];vcvars=Path(r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    for name,options in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        exe=run/(name+'.exe');command=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*options,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(exe)]
        batch=run/(name+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n',encoding='utf-8',newline='\r\n')
        built=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (run/(name+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8');assert built.returncode==0,name+' compile failed'
        output=subprocess.run([str(exe),'0'],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (run/(name+'-result.json')).write_text(output.stdout,encoding='utf-8');(run/(name+'-stderr.log')).write_text(output.stderr,encoding='utf-8');assert output.returncode==0,output.stderr
        result=json.loads(output.stdout);assert result['correctness']['pass'] and result['correctness']['cases']==2829
        profiles.append({'name':name,'assertions':result['assertions'],'cases':result['correctness']['cases'],'resultSha256':H(output.stdout.encode()),'executableSha256':H(exe.read_bytes()),'correctness':result['correctness']})
    after=(P/'native/main.cpp').read_bytes();assert all(r.bind(after)[n]['bytes']==x['bytes'] for n,x in bound.items())
    for rel,row in snapshots.items():assert H((P/rel).read_bytes())==row['sha256'],rel
    result={'pass':True,'mode':manifest['mode'],'runDirectory':str(run),'manifest':manifest,'profiles':profiles,'wholeMainStable':current==after,'bindingsStable':True,'noPerformanceClaim':True,'ctrlCauseProven':False,'productionEditedByRunner':False}
    save(run/'result.json',result);save(E/('final-result.json' if args.final else 'candidate-result.json'),result)
    save(run/'artifact-hashes.json',{str(f.relative_to(run)):{'sha256':H(f.read_bytes()),'bytes':f.stat().st_size} for f in sorted(run.rglob('*')) if f.is_file()})
    print(json.dumps({'pass':True,'mode':manifest['mode'],'run':str(run),'profiles':profiles},indent=2))
if __name__=='__main__':main()
