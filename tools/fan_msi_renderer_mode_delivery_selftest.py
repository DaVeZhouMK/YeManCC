from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys

def extract(text,signature):
    start=text.index(signature);i=text.index('{',start)+1;depth=1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[start:i]

def main():
    parser=argparse.ArgumentParser(description='Exact Fan mode/Arm/Tick/renderer hook; inert transport and native power, no HC/device')
    parser.add_argument('--native',required=True,type=Path);parser.add_argument('--output',required=True,type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];out=args.output.resolve();source=args.native.resolve()
    if out.exists():raise RuntimeError('Output must be new')
    out.mkdir(parents=True);raw=source.read_bytes();text=raw.decode('utf-8-sig')
    signatures={'ArmExtract.inc':'static void fanLeaseKeepaliveArm(const std::string& leaseId, const char* source, ULONGLONG receiptEpoch = 0) {',
      'DisarmExtract.inc':'static void fanLeaseKeepaliveDisarm(const char* reason, const std::string* expectedLease) {',
      'NoteExtract.inc':'static void fanHostNoteMsiModeWrite(unsigned long long generation) noexcept {',
      'ModeExtract.inc':'static void fanLeaseKeepaliveAddMsiMode(json& payload) {',
      'TickExtract.inc':'static void fanLeaseKeepaliveTick() {'}
    parts={name:extract(text,sig)for name,sig in signatures.items()}
    rawArm=parts['ArmExtract.inc'];oldArmGuard='std::lock_guard<std::mutex> lock(g_fanLeaseKeepaliveMx);';newArmGuard='FixtureArmLock lock(g_fanLeaseKeepaliveMx);'
    assert rawArm.count(oldArmGuard)==2,'Arm owner/notification lock shape changed'
    parts['ArmExtract.inc']=rawArm.replace(oldArmGuard,newArmGuard,1)
    assert parts['ArmExtract.inc'].replace(newArmGuard,oldArmGuard,1)==rawArm,'Arm unlock metering changed control logic'
    receiptHeader=(root/'native/fan_response_contract.h').read_bytes()
    hostRaw=(root/'FanLab/real-host/Program.cs').read_bytes();hostText=hostRaw.decode('utf-8-sig')
    heartbeat=extract(hostText,'public FanLease Heartbeat(')
    ttlValues=re.findall(r'ExpiresAtMonoMs = MonoMs\(\) \+ (\d+)',heartbeat)
    assert ttlValues==['15000'],'review Host TTL before interpreting native clock proof'
    leaseTtl=int(ttlValues[0])
    optional='static void fanLeaseKeepaliveAddRendererMode(std::string& body, const json& headers, ULONGLONG receiptEpoch) noexcept {'
    parts['RendererModeExtract.inc']=extract(text,optional)if optional in text else ''
    rawRendererMode=parts['RendererModeExtract.inc']
    builderSignature='static std::string fanLeaseKeepaliveRendererModeBody(const std::string& body, ULONGLONG revision) {'
    rawBuilder=extract(text,builderSignature)if builderSignature in text else ''
    parts['RendererModeBodyExtract.inc']=rawBuilder
    parts['FixtureConfig.inc']='#define FAN_FIXTURE_CANONICAL_BUILDER '+('1'if rawBuilder else '0')+'\n'+'#define FAN_FIXTURE_LEASE_TTL_MS '+str(leaseTtl)+'ULL\n'
    if rawRendererMode:
        parse='json::parse(body, nullptr, false)';dump='payload.dump()'
        assert rawRendererMode.count(parse)==1 and rawRendererMode.count(dump)==1,'optional metadata operation shape changed'
        metered=rawRendererMode.replace(parse,'FixtureRendererParse(body)',1).replace(dump,'FixtureRendererDump(payload)',1)
        builderCall='fanLeaseKeepaliveRendererModeBody(body, revision)'
        if rawBuilder:
            assert metered.count(builderCall)==1,'canonical metadata call shape changed'
            metered=metered.replace(builderCall,'FixtureRendererCanonicalBody(body, revision)',1)
        restored=metered.replace('FixtureRendererParse(body)',parse,1).replace('FixtureRendererDump(payload)',dump,1)
        if rawBuilder:restored=restored.replace('FixtureRendererCanonicalBody(body, revision)',builderCall,1)
        assert restored==rawRendererMode
        parts['RendererModeExtract.inc']=metered

    post=extract(text,'static bool fanLeaseKeepalivePostHeartbeat(')
    a=post.index('json payload = json::object();');z=post.index('headerAdded =',a)
    send='transport.send(payload.dump());';assert post.count(send)==1
    parts['NativePayloadExtract.inc']=post[a:z].strip()+'\n'+send
    start=text.index('ipc_on("http.request",');sendAt=text.index('        request.send(body);',start);marker='        // Fan-only mode metadata rides this existing request; no new heartbeat.'
    a=text.find(marker,start,sendAt);a=sendAt if a<0 else a
    parts['RendererSendExtract.inc']=text[a:sendAt+len('        request.send(body);')]
    for name,part in parts.items():(out/name).write_bytes(part.encode('utf-8'))
    fixture=root/'tools/fan_msi_renderer_mode_delivery_selftest.cpp';(out/'fixture.cpp').write_bytes(fixture.read_bytes())
    batch='@echo off\nset "PATH=%SystemRoot%\\System32;%PATH%"\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\n'
    batch+='cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /MT /I"'+str(root/'native')+'" /I"'+str(out)+'" "'+str(out/'fixture.cpp')+'" /Fo"'+str(out/'fixture.obj')+'" /Fe"'+str(out/'fixture.exe')+'"\n'
    (out/'compile.cmd').write_text(batch,encoding='ascii')
    with(out/'compile.log').open('w',encoding='utf-8')as log:
        compiled=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],cwd=out,stdout=log,stderr=subprocess.STDOUT)
    if compiled.returncode:print((out/'compile.log').read_text(encoding='utf-8',errors='replace'));return compiled.returncode
    cases=['lease-clock-ownership','lease-clock-enable','lease-clock-preset','lease-clock-mixed','lease-clock-evidence','lease-clock-refused-boundaries','renderer-starvation','duplicate-for-deferred','native-backup','zero-edge','multiple-edges','authorization','lease-boundary','malformed','existing-field','stale-power','stale-epoch','off','sleep','large-body','routing','cold-generation','header-casing','stop-flags','metadata-failure','during-epoch','during-power','during-lease','during-off','canonical-body','fallback-bodies','canonical-invalid','canonical-build-failure','fallback-during-epoch','fallback-during-power','fallback-during-lease','fallback-during-off'];results=[]
    for case in cases:
        run=subprocess.run([str(out/'fixture.exe'),case],capture_output=True,text=True,encoding='utf-8');value=json.loads(run.stdout)
        if run.returncode not in(0,1)or'fixtureError'in value:raise RuntimeError(case+' fixture failure: '+run.stdout+run.stderr)
        results.append(value);(out/(case+'.json')).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
    for name,sig in signatures.items():assert extract(source.read_bytes().decode('utf-8-sig'),sig)==(rawArm if name=='ArmExtract.inc' else parts[name]),name+' changed during verification'
    assert (root/'native/fan_response_contract.h').read_bytes()==receiptHeader,'receipt header changed during verification'
    assert (root/'FanLab/real-host/Program.cs').read_bytes()==hostRaw,'Host TTL source changed during verification'
    latest=source.read_bytes().decode('utf-8-sig')
    assert (extract(latest,optional)if optional in latest else '')==rawRendererMode,'renderer metadata helper changed during verification'
    assert (extract(latest,builderSignature)if builderSignature in latest else '')==rawBuilder,'canonical builder changed during verification'
    latestStart=latest.index('ipc_on("http.request",');latestSend=latest.index('        request.send(body);',latestStart)
    latestBegin=latest.find(marker,latestStart,latestSend);latestBegin=latestSend if latestBegin<0 else latestBegin
    assert latest[latestBegin:latestSend+len('        request.send(body);')]==parts['RendererSendExtract.inc'],'actual renderer hook changed during verification'

    result={'ok':all(x['ok']for x in results),'cases':len(results),'checks':sum(x['checks']for x in results),'failedCases':sum(not x['ok']for x in results),'results':results,
      'sourceSha256':hashlib.sha256(raw).hexdigest(),'receiptHeaderSha256':hashlib.sha256(receiptHeader).hexdigest(),'hostTtlSourceSha256':hashlib.sha256(hostRaw).hexdigest(),'leaseTtlMs':leaseTtl,'rawArmSha256':hashlib.sha256(rawArm.encode()).hexdigest(),'fixtureSha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'rawRendererModeSha256':hashlib.sha256(rawRendererMode.encode()).hexdigest(),'rawCanonicalBuilderSha256':hashlib.sha256(rawBuilder.encode()).hexdigest(),'canonicalBuilderPresent':bool(rawBuilder),'metadataOperationMetering':'parse/dump/canonical-build wrappers, identity restoration asserted; deterministic exceptions/boundaries only in fixture','extracts':[{ 'name':name,'sha256':hashlib.sha256((out/name).read_bytes()).hexdigest()}for name in parts],
      'scope':'exact production Fan Arm/Disarm/Note/Add/Tick and exact renderer-send hook; native payload assembly/send fragments; Fake transport/auth receiver/power/events, no native app/Host/HC/device/CPU benchmark'}
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({'ok':result['ok'],'checks':result['checks'],'cases':result['cases'],'failedCases':result['failedCases'],'failed':[{ 'scenario':x['scenario'],'failures':x['failures']}for x in results if not x['ok']]}));return 0 if result['ok']else 1
if __name__=='__main__':
    try:sys.exit(main())
    except Exception as error:print(str(error),file=sys.stderr);sys.exit(2)
