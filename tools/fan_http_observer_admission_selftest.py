from pathlib import Path
import argparse,hashlib,json,subprocess,sys,runpy

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--native',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];out=args.output.resolve();source=args.native.resolve()
    if out.exists():raise RuntimeError('Output must be new')
    out.mkdir(parents=True);raw=source.read_bytes();text=raw.decode('utf-8-sig');extract=runpy.run_path(str(root/'tools/fan_msi_renderer_mode_delivery_selftest.py'))['extract']
    sigs={'AsciiExtract.inc':'static std::string ascii_lower(std::string s) {','LoopbackExtract.inc':'static bool isLoopbackHost(const wchar_t* host) {','HeaderIdExtract.inc':'static std::string fanHeaderRequestIdFromRawHeaders(const std::string& rawHeaders) {','ObserverExtract.inc':'static void fanLeaseKeepaliveObserveHttp(const std::string& method,'}
    def get_source(text,sig):return extract(text[text.rindex(sig):],sig)
    parts={name:get_source(text,sig)for name,sig in sigs.items()};leaseStart=text.index('static bool isFanLeaseIdValid(const std::string& id) {');leaseEnd=text.index('static std::string fanApiPathFromObject(',leaseStart);parts['LeaseParserExtract.inc']=text[leaseStart:leaseEnd];originals=parts.copy()
    def swap(name,old,new):
        assert parts[name].count(old)==1,(name,old);parts[name]=parts[name].replace(old,new)
        return old,new
    swaps=[('ObserverExtract.inc',*swap('ObserverExtract.inc','json::parse(responseBody, nullptr, false)','FixtureFallbackParse(responseBody)'))]
    if 'ascii_lower(rawHeaders)'in parts['HeaderIdExtract.inc']:swaps.append(('HeaderIdExtract.inc',*swap('HeaderIdExtract.inc','ascii_lower(rawHeaders)','FixtureLowerHeaders(rawHeaders)')))
    reg=text.index('ipc_on("http.request",');targetStart=text.index('        const bool loopbackHost = isLoopbackHost(host);',reg);targetEnd=text.index('\n',text.index('const bool monitorFanRequest =',targetStart))
    parts['TargetExtract.inc']=text[targetStart:targetEnd];originals['TargetExtract.inc']=parts['TargetExtract.inc']
    end=text.index('        return response;',reg)+len('        return response;');begin=text.index('        const std::string emptyFanResponse;',reg)
    marker='        // Fan-only receipt preparation uses the same target as its observer.';guard=text.find(marker,reg,begin)
    if guard>=0:begin=guard
    parts['ResponseStageExtract.inc']=text[begin:end];originals['ResponseStageExtract.inc']=parts['ResponseStageExtract.inc']
    for old,new in [('ascii_lower(key)','FixtureLowerName(key)'),('json::parse(respBody, nullptr, false)','FixtureDirectParse(respBody)'),('fanHeaderRequestIdFromRawHeaders(rawResponseHeaders)','FixtureHeaderId(rawResponseHeaders)'),('fanLeaseKeepaliveObserveHttp(method, host','FixtureObserve(method, host'),('value.get<std::string>()','FixtureCredentialCopy(value)')]:swaps.append(('ResponseStageExtract.inc',*swap('ResponseStageExtract.inc',old,new)))
    for name,part in parts.items():
        restored=part
        for target,old,new in reversed(swaps):
            if target==name:restored=restored.replace(new,old)
        assert restored==originals[name],name+' metering restoration failed';(out/name).write_bytes(part.encode('utf-8'))
    fixture=root/'tools/fan_http_observer_admission_selftest.cpp';(out/'fixture.cpp').write_bytes(fixture.read_bytes())
    batch='\n'.join(['@echo off',r'set "PATH=%SystemRoot%\System32;%PATH%"',r'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul','if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /MT /I"'+str(root/'native')+'" /I"'+str(out)+'" "'+str(out/'fixture.cpp')+'" /Fo"'+str(out/'fixture.obj')+'" /Fe"'+str(out/'fixture.exe')+'"',''])
    (out/'compile.cmd').write_text(batch,encoding='ascii')
    with(out/'compile.log').open('w',encoding='utf-8')as log:compiled=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],cwd=out,stdout=log,stderr=subprocess.STDOUT)
    if compiled.returncode:print((out/'compile.log').read_text(encoding='utf-8',errors='replace'));return 2
    cases=['nonfan-remote','nonfan-local-port','nonfan-remote-port8765','nonfan-denied','nonfan-burst','fan-normal','fan-localhost','fan-ipv6','fan-cased-header','fan-get','fan-head','fan-no-header','fan-typed-header','fan-denied401','fan-denied403','fan-denied-stale-epoch','fan-incomplete','fan-close-incomplete','fan-invalid-json','fan-oversized','fan-stale-epoch','header-name-in-value','header-name-prefix','header-reason-phrase','header-trailing-letter','header-trailing-comma','header-body-spoof','header-short','header-long','header-ows','header-mixed-case','header-lf','header-no-final-newline','header-empty-value','fan-long-headers','header-missing-then-manual','fan-success-burst','fan-denied-token-rotation','fan-denied-incomplete'];results=[]
    for case in cases:
        run=subprocess.run([str(out/'fixture.exe'),case],capture_output=True,text=True,encoding='utf-8');value=json.loads(run.stdout)
        if run.returncode not in(0,1)or'fixtureError'in value:raise RuntimeError(case+' fixture failure '+run.stdout+run.stderr)
        results.append(value);(out/(case+'.json')).write_text(json.dumps(value,indent=2),encoding='utf-8')
    latest=source.read_bytes().decode('utf-8-sig')
    for name,sig in sigs.items():assert get_source(latest,sig)==originals[name],name+' source drift'
    assert originals['ResponseStageExtract.inc']in latest and originals['TargetExtract.inc']in latest and originals['LeaseParserExtract.inc']in latest,'response stage/target/parser drift'
    result={'ok':all(x['ok']for x in results),'cases':len(results),'checks':sum(x['checks']for x in results),'failedCases':sum(not x['ok']for x in results),'results':results,'sourceSha256':hashlib.sha256(raw).hexdigest(),'fixtureSha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'rawExtracts':[{ 'name':name,'sha256':hashlib.sha256(value.encode()).hexdigest()}for name,value in originals.items()],'scope':'actual reg_http response-stage/target, actual Fan receipt observer/header ID/loopback; fake consumed network result and activity/auth/display sinks; no transport/Host/HC/hardware/CPU benchmark'}
    (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({'ok':result['ok'],'cases':result['cases'],'checks':result['checks'],'failedCases':result['failedCases'],'failed':[{ 'scenario':x['scenario'],'failures':x['failures']}for x in results if not x['ok']]}));return 0 if result['ok']else 1
if __name__=='__main__':
    try:sys.exit(main())
    except Exception as error:print(str(error),file=sys.stderr);sys.exit(2)
