from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys

def extract(text,signature):
    start=text.index(signature);begin=text.index('{',start);depth=1;end=begin+1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    return text[start:end]

def main():
    parser=argparse.ArgumentParser(description='Exact Fan keepalive tick with real JSON and inert transport/clock/log boundaries; no hardware or CPU timing')
    parser.add_argument('--native',required=True,type=Path);parser.add_argument('--output',required=True,type=Path);parser.add_argument('--mode',choices=['eager','guarded'],required=True);args=parser.parse_args()
    project=Path(__file__).resolve().parents[1];source=args.native.resolve();out=args.output.resolve()
    if out.exists():raise RuntimeError('Output must be new; existing evidence will not be cleared')
    out.mkdir(parents=True);raw=source.read_bytes();text=raw.decode('utf-8-sig')
    original=extract(text,'static void fanLeaseKeepaliveTick() {')
    patterns=[(r'json detail = (\{\s*\{"status", static_cast<int>\(status\)\},\s*\{"leasePresent", true\},\s*\{"source", "native.ready"\}\s*\});','CountSuccessDetail'),
              (r'json detail = (\{\s*\{"status", static_cast<int>\(status\)\},\s*\{"errorCode", errorCode\},\s*\{"rejected", rejected\}\s*\});','CountFailureDetail')]
    metered=original;replacements=[]
    for pattern,counter in patterns:
        matches=list(re.finditer(pattern,metered));assert len(matches)==1,counter+' payload shape drift; review fixture'
        match=matches[0];replacement='json detail = '+counter+'('+match.group(1)+');'
        metered=metered[:match.start()]+replacement+metered[match.end():];replacements.append((replacement,match.group(0)))
    leaseCopy='leaseId = g_fanLeaseId;'
    assert metered.count(leaseCopy)==1
    metered=metered.replace(leaseCopy,'FixtureLeaseCopy(leaseId, g_fanLeaseId);',1)
    restored=metered.replace('FixtureLeaseCopy(leaseId, g_fanLeaseId);',leaseCopy,1)
    for replacement,before in replacements:
        assert restored.count(replacement)==1;restored=restored.replace(replacement,before,1)
    assert restored==original,'metering changed production control code'
    (out/'TickExtract.inc').write_bytes(metered.encode('utf-8'))
    fixture=Path(__file__).with_suffix('.cpp');fixture_copy=out/'fixture.cpp';fixture_copy.write_bytes(fixture.read_bytes())
    compile_cmd=out/'compile.cmd'
    compile_cmd.write_text('@echo off\nset "PATH=%SystemRoot%\\System32;%PATH%"\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /MT /I"'+str(project/'native')+'" /I"'+str(out)+'" "'+str(fixture_copy)+'" /Fo"'+str(out/'fixture.obj')+'" /Fe"'+str(out/'fixture.exe')+'"\n',encoding='ascii')
    with (out/'compile.log').open('w',encoding='utf-8') as log:compiled=subprocess.run(['cmd.exe','/d','/c',str(compile_cmd)],cwd=out,stdout=log,stderr=subprocess.STDOUT)
    if compiled.returncode:print((out/'compile.log').read_text(encoding='utf-8',errors='replace'));return compiled.returncode
    cases=['steady','transitions','auth401','auth403','rejected409','stale-owner','off-after-post','early-gates','throttle-and-inflight','renderer-grace','payload-failure-before-exchange','transient-miss-throttle','busy-admission','busy-auth-refused','busy-lifecycle-refused','busy-off-inflight','busy-new-owner-inflight','busy-sleep-boundary','busy-renderer-grace','post-exception-retry','post-allocation-exception','lease-copy-exception','throw-after-owner-change','throw-after-off','reentrant-post-exception'];results=[]
    for case in cases:
        run=subprocess.run([str(out/'fixture.exe'),case,args.mode],capture_output=True,text=True,encoding='utf-8')
        (out/(case+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
        if run.returncode not in (0,1):raise RuntimeError('fixture execution failed: '+run.stdout+run.stderr)
        value=json.loads(run.stdout);assert value['ok']==(run.returncode==0);results.append(value);print(('PASS 'if value['ok']else'FAIL ')+case+' success-detail-materializations='+str(value['successDetailsConstructed']))
    # Bind the tested region even if another task edited an unrelated part of main.cpp.
    assert extract(source.read_bytes().decode('utf-8-sig'),'static void fanLeaseKeepaliveTick() {')==original,'tick changed during checks'
    worker=extract(text,'static void fanLeaseKeepaliveThreadProc() {');ensure=extract(text,'static void fanLeaseKeepaliveEnsureThread() {')
    usages=[m.start() for m in re.finditer(r'\bfanLeaseKeepaliveTick\b',text)];assert len(usages)==2 and 'fanLeaseKeepaliveTick();' in worker,'single-worker call-site assumption changed'
    assert 'std::lock_guard<std::mutex> lock(g_fanLeaseKeepaliveMx);' in ensure and 'g_fanLeaseKeepaliveThread.joinable()' in ensure
    sha=lambda b:hashlib.sha256(b).hexdigest()
    result={'ok':all(x['ok']for x in results),'compiled':True,'cases':len(results),'checks':sum(r['checks'] for r in results),'mode':args.mode,'sourceSha256AtExtraction':sha(raw),'tickSha256':sha(original.encode()),'meteredSha256':sha(metered.encode()),'fixtureSha256':sha(fixture.read_bytes()),'responseHeaderSha256':sha((project/'native/fan_response_contract.h').read_bytes()),'singleProductionTickCaller':True,'workerSha256':sha(worker.encode()),'threadCreationSha256':sha(ensure.encode()),'scope':'exact production tick body and real nlohmann JSON/receipt header; payload initializers and lease copy have reversible count/fault wrappers; clock/transport/log/power are deterministic boundaries; sequential worker scenarios; no native transport, Host, HC, devices or CPU timing','hardwareCalls':0,'cpuBenchmark':False,'results':results}
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({k:result[k] for k in ['ok','cases','checks','mode','tickSha256','singleProductionTickCaller']}));return 0 if result['ok']else 1

if __name__=='__main__':
    try:sys.exit(main())
    except Exception as error:print(str(error),file=sys.stderr);sys.exit(1)
