"""Private native build: outputs only, no native application execution."""
import argparse, datetime, hashlib, json, os, pathlib, re, shutil, subprocess, sys
P = pathlib.Path
NOW = lambda: datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p): return hashlib.sha256(P(p).read_bytes()).hexdigest()
def read(p): return json.loads(P(p).read_text(encoding="utf-8-sig"))
def save(p, x):
    p=P(p);p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
def child(root, rel):
    p=(P(root)/rel).resolve();p.relative_to(P(root).resolve());return p

def snapshot(a):
    root=P(a.project_root).resolve();work=P(a.workspace).resolve();dest=work/'source'
    if dest.exists(): raise RuntimeError('FRESH_NATIVE_SOURCE_REQUIRED')
    paths=set();suffix={'.h','.hpp','.inl','.inc','.cpp','.c','.rc','.ico','.manifest','.bat','.def','.cs','.csproj','.json','.js','.mjs'}
    for folder in ['native','capabilities','deps/json','deps/webview2/build/native/include','deps/webview2/build/native/x64']:
        for p in (root/folder).rglob('*'):
            if p.is_file() and not any(x in {'bin','obj','node_modules','.git'} for x in p.relative_to(root).parts) and (p.suffix.lower() in suffix or p.suffix.lower()=='.lib'): paths.add(p)
    paths.add(root/'tools/battery-charge-contract.mjs');paths.add(root/'tools/controller_visibility_probe.cpp')
    project=root/'native/BatteryHost/YeManBatteryHost.csproj'
    for include in re.findall(r'<Compile\s+Include="([^"]+)"',project.read_text(encoding='utf-8-sig')):
        p=(project.parent/include.replace('\\','/')).resolve();p.relative_to(root);paths.add(p)
    rows=[]
    for p in sorted(paths):
        if p.is_symlink() or getattr(p.stat(),'st_file_attributes',0)&0x400: raise RuntimeError('SOURCE_REPARSE')
        rel=p.relative_to(root).as_posix();data=p.read_bytes();to=child(dest,rel);to.parent.mkdir(parents=True,exist_ok=True);to.write_bytes(data);rows.append({'path':rel,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
    for row in rows:
        if sha(root/row['path'])!=row['sha256']: raise RuntimeError('SOURCE_CHANGED_DURING_SNAPSHOT:'+row['path'])
    record={'schemaVersion':1,'kind':'ymcc-lighting-native-source-v1','createdUtc':NOW(),'sourceOrigin':str(root),'sourceRoot':str(dest),'files':rows,'nativeExecuted':False,'hardwareIo':False}
    save(work/'native-source.json',record);print(json.dumps({'status':'SOURCE_READY','files':len(rows),'source':str(dest)}),flush=True)

def review_build(a):
    work=P(a.workspace).resolve();manifest=read(work/'native-source.json');root=P(manifest['sourceRoot'])
    scripts=[root/'native/build_native.bat',root/'native/rtss/build_bridge.bat',root/'tools/battery-charge-contract.mjs']
    for file in scripts:
        row=next((r for r in manifest['files'] if r['path']==file.relative_to(root).as_posix()),None)
        if row is None or sha(file)!=row['sha256']:raise RuntimeError('BUILD_REVIEW_SOURCE_CHANGED')
    main=scripts[0].read_text(encoding='utf-8-sig');bridge=scripts[1].read_text(encoding='utf-8-sig');commands=[]
    forbidden=re.compile(r'^\s*(copy|move|del|rmdir|rd|start|powershell|pwsh|curl|wget|robocopy|xcopy|reg|schtasks)\b',re.I)
    for file in scripts[:2]:
        for line in file.read_text(encoding='utf-8-sig').splitlines():
            line=line.strip()
            if not line or line.lower().startswith('rem ') or line.startswith(':') or line.startswith('@echo'):continue
            if forbidden.match(line):raise RuntimeError('BUILD_REVIEW_FORBIDDEN_ACTION:'+line)
            if 'call ' in line.lower():
                if not ('vcvars' in line and 'Microsoft Visual Studio' in line) and line.lower()!='call rtss\\build_bridge.bat':raise RuntimeError('BUILD_REVIEW_UNEXPECTED_CALL:'+line)
            if re.match(r'^(node|rc|cl|mt\.exe|"%VCToolsInstallDir%)',line,re.I):commands.append({'script':str(file),'command':line})
    if 'node "%~dp0..\\tools\\battery-charge-contract.mjs" --check' not in main:raise RuntimeError('BUILD_REVIEW_CHECK_ONLY_REQUIRED')
    if any(re.search(r'(?i)(YeManCC|YeManRecoveryService)\.exe[" ]*$',l) for l in main.splitlines() if re.match(r'^\s*(call|start) ',l,re.I)):raise RuntimeError('BUILD_REVIEW_PRODUCT_EXECUTION')
    if 'set "OUTDIR=%WORKSPACE_ROOT%\\Build\\App\\Native"' not in main or 'Build\\App\\Native' not in bridge:raise RuntimeError('BUILD_REVIEW_OUTPUT_ROOT')
    result={'schemaVersion':1,'status':'PASS','sourceRoot':str(root),'workspaceRoot':str(work),'reviewedScripts':[{'path':str(f),'sha256':sha(f)} for f in scripts],'compileAndLinkCommands':commands,'outputRoot':str(work/'Build/App/Native'),'temporaryRoot':str(work/'temp'),'nativeExecuted':False,'hardwareIo':False,'sourceCheck':'battery-charge-contract.mjs --check only','postBuild':'echo only'}
    save(work/'build-path-review.json',result);print(json.dumps({'status':'BUILD_PATH_REVIEWED','commands':len(commands)}),flush=True)

def build(a):
    work=P(a.workspace).resolve();record=read(work/'native-source.json');root=P(record['sourceRoot'])
    for row in record['files']:
        if sha(root/row['path'])!=row['sha256']:raise RuntimeError('NATIVE_SNAPSHOT_CHANGED')
    if not (work/'build-path-review.json').is_file():raise RuntimeError('BUILD_PATH_REVIEW_REQUIRED')
    review=read(work/'build-path-review.json')
    if review.get('status')!='PASS' or review['sourceRoot']!=str(root):raise RuntimeError('BUILD_PATH_REVIEW_REQUIRED')
    for row in review['reviewedScripts']:
        if sha(row['path'])!=row['sha256']:raise RuntimeError('BUILD_PATH_REVIEW_STALE')
    if (work/'native-build.log').exists() or (work/'native-candidate.json').exists():raise RuntimeError('FRESH_BUILD_EVIDENCE_REQUIRED')
    (work/'temp').mkdir(exist_ok=True)
    env={k.upper():v for k,v in os.environ.items()};env['TEMP']=str(work/'temp');env['TMP']=str(work/'temp');env.pop('_CL_',None);env['YEMAN_WORKSPACE_ROOT']=str(work);env['VSLANG']='1033';env['CL']='/showIncludes /Bv /Z7 /ZH:SHA_256 /FC';env['LINK']='/DEBUG:FULL /PDBALTPATH:%_PDB% /OPT:REF /OPT:ICF /INCREMENTAL:NO'
    node=P(a.node or shutil.which('node') or '')
    if not node.is_file():raise RuntimeError('EXPLICIT_NODE_REQUIRED')
    env['PATH']=';'.join([str(node.parent),str(P(sys.executable).parent),'C:/Windows/System32','C:/Windows','C:/Program Files/dotnet'])
    env.pop('__VSCMD_PREINIT_PATH',None);env['VSCMD_SKIP_SENDTELEMETRY']='1'
    args=['cmd.exe','/d','/c',str(root/'native/build_native.bat')];start=NOW()
    with (work/'native-build.log').open('wb') as log:
        proc=subprocess.Popen(args,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=0x08000000)
        save(work/'native-build-process.json',{'pid':proc.pid,'state':'running','args':args,'startedUtc':start});print(json.dumps({'status':'NATIVE_BUILD_RUNNING','pid':proc.pid}),flush=True);
        timed_out=False
        try:rc=proc.wait(timeout=420)
        except subprocess.TimeoutExpired:
            timed_out=True
            subprocess.run(['C:/Windows/System32/taskkill.exe','/PID',str(proc.pid),'/T','/F'],capture_output=True,timeout=30,creationflags=0x08000000)
            rc=proc.wait(timeout=30)
    state={'pid':proc.pid,'state':'terminal','exitCode':rc,'naturalExit':not timed_out,'timedOut':timed_out,'args':args,'startedUtc':start,'finishedUtc':NOW()};save(work/'native-build-process.json',state)
    if timed_out or rc:raise RuntimeError('PRIVATE_NATIVE_BUILD_FAILED:'+str(rc))
    changes=[r['path'] for r in record['files'] if sha(root/r['path'])!=r['sha256']]
    if changes:raise RuntimeError('NATIVE_SOURCE_CHANGED_DURING_BUILD:'+','.join(changes))
    exe=work/'Build/App/Native/YeManCC.exe';pdb=exe.with_suffix('.pdb')
    if not exe.is_file() or not pdb.is_file():raise RuntimeError('NATIVE_PE_PDB_OUTPUT_MISSING')
    text=(work/'native-build.log').read_text(encoding='utf-8',errors='replace');includes=[]
    for line in text.splitlines():
        m=re.search(r'([A-Za-z]:\\[^\r\n]+)$',line)
        if m:
            p=P(m.group(1).strip())
            if p.is_file():includes.append(str(p.resolve()))
    include_rows=[{'path':p,'sha256':sha(p)} for p in sorted(set(includes))]
    receipt={'schemaVersion':1,'kind':'ymcc-lighting-native-candidate-v1','status':'BUILT_PENDING_PDB_CONSUMER','workspaceRoot':str(work),'sourceManifest':{'path':str(work/'native-source.json'),'sha256':sha(work/'native-source.json')},'sourceRoot':str(root),'mainSha256':sha(root/'native/main.cpp'),'buildScriptSha256':sha(root/'native/build_native.bat'),'process':state,'binary':{'path':str(exe),'sha256':sha(exe),'bytes':exe.stat().st_size},'pdb':{'path':str(pdb),'sha256':sha(pdb)},'includeFiles':include_rows,'sourceChanges':changes,'compilerOptionsAdded':env['CL'],'linkOptionsAdded':env['LINK'],'nativeExecuted':False,'hardwareIo':False,'fullProductAcceptance':False}
    save(work/'native-candidate.json',receipt);print(json.dumps({'status':'NATIVE_BUILT','sha256':receipt['binary']['sha256'],'includeFiles':len(include_rows)}),flush=True)


def toolchain(root):
    vs=P('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC')
    msvc=sorted((p for p in vs.iterdir() if (p/'bin/Hostx64/x64/cl.exe').is_file()),reverse=True)[0]
    sdk=P('C:/Program Files (x86)/Windows Kits/10');ver=sorted(p.name for p in (sdk/'Include').iterdir() if (p/'ucrt').is_dir())[-1]
    env={k.upper():v for k,v in os.environ.items()};env['PATH']=str(msvc/'bin/Hostx64/x64')+';C:/Windows/System32;C:/Windows';env.pop('CL',None);env.pop('_CL_',None);env.pop('LINK',None)
    env['INCLUDE']=';'.join(map(str,[msvc/'include',sdk/'Include'/ver/'ucrt',sdk/'Include'/ver/'shared',sdk/'Include'/ver/'um',root/'deps/json']))
    env['LIB']=';'.join(map(str,[msvc/'lib/x64',sdk/'Lib'/ver/'ucrt/x64',sdk/'Lib'/ver/'um/x64']))
    return msvc,env

def compile_worker(root,out,name,cpp,extra=None):
    out.mkdir(parents=True,exist_ok=True);src=out/(name+'.cpp');src.write_text(cpp,encoding='utf-8');exe=out/(name+'.exe');msvc,env=toolchain(root)
    args=[str(msvc/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++20','/EHsc','/W4','/WX','/utf-8','/MT','/O2',str(src),'/Fo'+str(out/(name+'.obj')),'/Fe'+str(exe)]+(extra or [])
    proc=subprocess.run(args,cwd=out,env=env,capture_output=True,timeout=120,creationflags=0x08000000);(out/(name+'-compile.log')).write_bytes(proc.stdout+proc.stderr)
    if proc.returncode:raise RuntimeError(name.upper()+'_COMPILE_FAILED')
    return exe

DIA_CPP=r'''
#include <windows.h>
#include <dia2.h>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;
std::string text(const wchar_t* p){if(!p)return {};int n=WideCharToMultiByte(CP_UTF8,0,p,-1,nullptr,0,nullptr,nullptr);std::string s(static_cast<size_t>(n),0);WideCharToMultiByte(CP_UTF8,0,p,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;}
void need(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(std::string(what)+":"+std::to_string(hr));}
int wmain(int argc,wchar_t**argv){try{if(argc!=3)return 3;need(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM");HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!dll)throw std::runtime_error("DIA DLL");
using Factory=HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*);auto entry=reinterpret_cast<Factory>(GetProcAddress(dll,"DllGetClassObject"));if(!entry)throw std::runtime_error("DIA factory");IClassFactory* factory=nullptr;need(entry(__uuidof(DiaSource),IID_IClassFactory,reinterpret_cast<void**>(&factory)),"factory");IDiaDataSource* source=nullptr;need(factory->CreateInstance(nullptr,__uuidof(IDiaDataSource),reinterpret_cast<void**>(&source)),"source");factory->Release();need(source->loadDataFromPdb(argv[2]),"load PDB");IDiaSession* session=nullptr;need(source->openSession(&session),"session");IDiaSymbol* global=nullptr;need(session->get_globalScope(&global),"global");GUID guid{};DWORD age=0;need(global->get_guid(&guid),"guid");need(global->get_age(&age),"age");wchar_t guidText[64]{};StringFromGUID2(guid,guidText,64);IDiaEnumSymbols* modules=nullptr;need(global->findChildren(SymTagCompiland,nullptr,nsNone,&modules),"modules");json rows=json::array();IDiaSymbol* module=nullptr;ULONG modulesFetched=0;while(modules->Next(1,&module,&modulesFetched)==S_OK&&modulesFetched){IDiaEnumSourceFiles* files=nullptr;if(session->findFile(module,nullptr,nsNone,&files)!=S_OK){module->Release();continue;}IDiaSourceFile* file=nullptr;ULONG fetched=0;
while(files->Next(1,&file,&fetched)==S_OK&&fetched){BSTR name=nullptr;DWORD type=0,n=0;need(file->get_fileName(&name),"filename");std::vector<BYTE> sum(256);n=256;need(file->get_checksum(n,&n,sum.data()),"checksum");sum.resize(n);need(file->get_checksumType(&type),"checksum type");std::string hex;const char* h="0123456789abcdef";for(auto b:sum){hex.push_back(h[b>>4]);hex.push_back(h[b&15]);}rows.push_back(json{{"path",text(name)},{"checksumType",type},{"checksum",hex}});SysFreeString(name);file->Release();}
files->Release();module->Release();}modules->Release();global->Release();session->Release();source->Release();std::cout<<json{{"guid",text(guidText)},{"age",age},{"sourceFiles",rows},{"nativeExecuted",false}}.dump();CoUninitialize();return 0;
}catch(const std::exception&e){std::cerr<<e.what();return 2;}}
'''

def pdb_proof(a):
    import struct,uuid
    work=P(a.workspace).resolve();native=read(work/'native-candidate.json');root=P(native['sourceRoot']);out=work/'pdb-proof'
    if out.exists():raise RuntimeError('FRESH_PDB_PROOF_REQUIRED')
    dia=P('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/DIA SDK');exe=compile_worker(root,out,'dia-source',DIA_CPP,['/I'+str(dia/'include'),'ole32.lib','oleaut32.lib'])
    args=[str(exe),str(dia/'bin/amd64/msdia140.dll'),native['pdb']['path']];proc=subprocess.run(args,capture_output=True,timeout=90,creationflags=0x08000000);(out/'run.log').write_bytes(proc.stdout+proc.stderr)
    if proc.returncode:raise RuntimeError('DIA_PDB_READER_FAILED')
    data=json.loads(proc.stdout.decode('utf-8'));b=P(native['binary']['path']).read_bytes();pe=struct.unpack_from('<I',b,0x3c)[0];sections=struct.unpack_from('<H',b,pe+6)[0];op=pe+24;size=struct.unpack_from('<H',b,pe+20)[0];magic=struct.unpack_from('<H',b,op)[0];dd=op+(112 if magic==0x20b else 96);rva,sz=struct.unpack_from('<II',b,dd+6*8)
    def offset(addr):
        for i in range(sections):
            at=op+size+i*40;vs,va,rs,rp=struct.unpack_from('<IIII',b,at+8)
            if va<=addr<va+max(vs,rs):return rp+addr-va
        raise RuntimeError('DEBUG_RVA')
    cv=[]
    for i in range(sz//28):
        e=struct.unpack_from('<IIHHIIII',b,offset(rva)+i*28)
        if e[4]==2:
            raw=b[e[7]:e[7]+e[5]]
            if raw[:4]==b'RSDS':cv.append({'guid':str(uuid.UUID(bytes_le=raw[4:20])),'age':struct.unpack_from('<I',raw,20)[0]})
    if len(cv)!=1 or cv[0]['guid']!=data['guid'].strip('{}').lower() or cv[0]['age']!=data['age']:raise RuntimeError('NATIVE_PE_PDB_MISMATCH')
    sourceManifest=read(work/'native-source.json');maprows={str((root/r['path']).resolve()).lower():r for r in sourceManifest['files']};verified=[]
    for row in data['sourceFiles']:
        p=P(row['path']);key=str(p.resolve()).lower()
        if key in maprows:
            ref=maprows[key]
            if row['checksumType']==0 and row['checksum']=='':
                verified.append({'path':ref['path'],'sha256':ref['sha256'],'checksumType':0,'compilerChecksumUnavailable':True})
            elif row['checksumType']!=3 or row['checksum']!=ref['sha256'] or sha(p)!=ref['sha256']:
                raise RuntimeError('NATIVE_PDB_SOURCE_MISMATCH:'+str(p))
            else: verified.append({'path':ref['path'],'sha256':ref['sha256'],'checksumType':3})
    required=['native/main.cpp','native/oem_lighting_operations.h','native/lighting_device_core_binding.h','capabilities/hc-device-catalog.generated.h']
    if not all(any(r['path']==f for r in verified) for f in required):raise RuntimeError('NATIVE_PDB_LIGHTING_SOURCE_MISSING')
    requiredChecked=[f for f in required if any(r['path']==f and r.get('checksumType')==3 for r in verified)]
    if len(requiredChecked)!=len(required): raise RuntimeError('NATIVE_PDB_LIGHTING_SOURCE_CHECKSUM_MISSING')
    result={'schemaVersion':1,'kind':'ymcc-lighting-native-pdb-v1','status':'PASS','nativeSha256':native['binary']['sha256'],'pdbSha256':native['pdb']['sha256'],'pair':cv[0],'pairVerified':True,'verifiedSourceDocuments':verified,'totalPdbSourceFiles':len(data['sourceFiles']),'nativeSourceManifestSha256':native['sourceManifest']['sha256'],'reader':{'path':str(exe),'sha256':sha(exe),'diaSha256':sha(dia/'bin/amd64/msdia140.dll')},'nativeExecuted':False,'hardwareIo':False}
    save(out/'result.json',result);save(out/'dia-raw.json',data);print(json.dumps({'status':'PDB_PASS','sourceDocuments':len(verified)}),flush=True)

CONSUMER_HEAD=r'''
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;using BYTE=unsigned char;constexpr unsigned INVALID_FILE_ATTRIBUTES=~0u;
struct{std::string deviceClass;}g_machineIdentity;
std::wstring kLightSetterExe=L"D:\\YMCC Fixture\\YeManLightSetter.exe",kLightSetterHcRuntimeRoot=L"D:\\YMCC Fixture\\HC",POWER_CONTROL_DIR=L"D:\\YMCC Fixture\\PowerControl";
unsigned GetFileAttributesW(const wchar_t*){return 0;}
std::wstring U2W(const std::string&s){return std::wstring(s.begin(),s.end());}
'''
CONSUMER_END=r'''
int main(int argc,char**argv){if(argc!=2)return 3;std::ifstream input(argv[1]);json cases;input>>cases;json rows=json::array();bool all=true;
for(const auto& c:cases){g_machineIdentity.deviceClass=c.at("model");auto command=ymccLightSetterApply(true,"solid",10,20,30,40,50,60,75,50).at("command").get<std::string>();bool params=command.find(" --action color --level solid")!=std::string::npos&&command.find(" --brightness 50")!=std::string::npos&&command.find(" --r 10 --g 20 --b 30")!=std::string::npos&&command.find(" --hc-profile ymcc-device-core-v1")!=std::string::npos;
auto got=parseReply(c.at("raw"),c.at("exitCode"),c.at("timedOut"));bool pass=params&&got.value("ok",false)==c.at("expectedOk").get<bool>();if(!c.at("expectedReason").is_null())pass=pass&&got.value("reason",std::string())==c.at("expectedReason").get<std::string>();rows.push_back(json{{"id",c.at("id")},{"passed",pass},{"parametersMatched",params},{"actualReceipt",got}});all=all&&pass;}
std::cout<<json{{"status",all?"PASS":"FAIL"},{"cases",rows},{"hardwareIo",false},{"nativeCandidateExecuted",false}}.dump();return all?0:2;}
'''

def consumer(a):
    work=P(a.workspace).resolve();native=read(work/'native-candidate.json');root=P(native['sourceRoot']);out=work/'native-consumer'
    if out.exists():raise RuntimeError('FRESH_CONSUMER_EVIDENCE_REQUIRED')
    out.mkdir();helper=P(a.helper_candidate).resolve();approval=read(helper/'CANDIDATE-ACCEPTANCE.json');signalPath=next((helper/'global').glob('*/cases/lighting.signal-consumer/lighting/result.json'));signal=read(signalPath);behavior=signal['compatibility']['payloadCandidate']['behavior']
    if behavior['actualSha256']!=approval['helperSha256'] or not behavior['passed']:raise RuntimeError('ACTUAL_HELPER_EVIDENCE_REQUIRED')
    source=(root/'native/main.cpp').read_text(encoding='utf-8-sig');begin=source.index('static json ymccLightSetterApply(');end=source.index('    // 单一实参只取一次 str()',begin);ps=source.index('    bool hasJson = false',begin);pe=source.index('\n}',ps);prefix=source[begin:end];parser=source[ps:pe]
    header=root/'native/oem_lighting_operations.h';catalog=re.search(r'kCatalogSha256\[\]\s*=\s*"([a-f0-9]{64})"',header.read_text()).group(1)
    managed=P(a.helper_sources)/'capabilities/HcLightingCapabilityCatalog.generated.cs';managed_catalog=re.search(r'CatalogSha256\s*=\s*"([a-f0-9]{64})"',managed.read_text()).group(1)
    if managed_catalog!=catalog:raise RuntimeError('NATIVE_HELPER_CATALOG_MISMATCH')
    core_header=(root/'native/lighting_device_core_binding.h').read_text();core_pin=re.search(r'kIndependentDeviceCoreSha256(?:\[\])?\s*=\s*"([a-f0-9]{64})"',core_header)
    if not core_pin or core_pin.group(1)!=approval['coreSha256']:raise RuntimeError('NATIVE_HELPER_CORE_MISMATCH')
    traces=[r for r in behavior['ayaneoObservations'] if r.get('phase')=='color-with-brightness']
    if len(traces)!=2:raise RuntimeError('ACTUAL_TWO_PROTOCOL_TRACES_REQUIRED')
    cases=[]
    for trace in traces:
        model=trace['model'];reply={'ok':True,'result':{'deviceClass':model,'capabilityId':'hc.device.'+model.rsplit('.',1)[1],'capabilityOperation':'lighting.color.solid','capabilityCatalogSha256':catalog,'action':'color','note':'ok','ready':True,'opened':True,'backend':'ymcc-hc-device-core','hcProfile':'ymcc-device-core-v1','physicalEffectVerified':False,'queuedWriteManaged':False,'queuedWritesCompleted':False,'queuedWriteTasks':0,'queuedWriteFailures':0}}
        for mode in ['accepted','wrong-catalog','missing-catalog','wrong-operation','wrong-model','wrong-action','child-false','effect-false','exit-failed','timed-out','malformed']:
            row=json.loads(json.dumps(reply));exit_code=0;timed=False;raw=None;reason=None
            if mode=='wrong-catalog':row['result']['capabilityCatalogSha256']='0'*64;reason='light-setter-catalog-mismatch'
            if mode=='missing-catalog':del row['result']['capabilityCatalogSha256'];reason='light-setter-catalog-mismatch'
            if mode=='wrong-operation':row['result']['capabilityOperation']='lighting.brightness';reason='light-setter-operation-mismatch'
            if mode=='wrong-model':row['result']['deviceClass']='HandheldCompanion.Devices.ClawA1M';row['result']['capabilityId']='hc.device.ClawA1M';reason='light-setter-identity-mismatch'
            if mode=='wrong-action':row['result']['action']='off';reason='light-setter-operation-mismatch'
            if mode=='child-false':row['ok']=False;reason='light-setter-result-not-ok'
            if mode=='effect-false':row['result']['note']='setLedColor-false';reason='setLedColor-false'
            if mode=='exit-failed':exit_code=1;reason='light-setter-exit-1'
            if mode=='timed-out':timed=True;reason='light-setter-timeout'
            if mode=='malformed':raw='not-json';reason='light-setter-no-json'
            cases.append({'id':model+':'+mode,'model':model,'raw':raw or json.dumps(row),'exitCode':exit_code,'timedOut':timed,'expectedOk':mode=='accepted','expectedReason':reason,'inputKind':'actual frozen-source action trace plus source-derived Run envelope','actualTraceWrites':trace['writes']})
    includes='#include '+json.dumps(str(header).replace('\\','/'))+'\n#include '+json.dumps(str(root/'native/lighting_device_core_binding.h').replace('\\','/'))+'\n'
    cpp=includes+CONSUMER_HEAD+prefix+'\nconst auto value=cmd.str();std::string ascii;for(wchar_t ch:value){if(ch<0||ch>127)throw std::runtime_error("non-ASCII fixture");ascii.push_back(static_cast<char>(ch));}return json{{"command",ascii}};\n}\n'
    cpp+='static json parseReply(const std::string& childOut,unsigned exitCode,bool timedOut){\nconst bool brightnessSupported=true,outputReadTimedOut=false,outputTruncated=false,outputReadError=false,enabled=true,presetAction=false;const std::string action="color",mode="solid",presetId="";\n'+parser+'\n}\n'+CONSUMER_END
    exe=compile_worker(root,out,'consumer',cpp);save(out/'inputs.json',cases)
    proc=subprocess.run([str(exe),str(out/'inputs.json')],cwd=out,capture_output=True,timeout=30,creationflags=0x08000000);(out/'run.log').write_bytes(proc.stdout+proc.stderr);actual=json.loads(proc.stdout.decode())
    result={'schemaVersion':1,'kind':'ymcc-lighting-native-consumer-v1','status':actual['status'],'exitCode':proc.returncode,'nativeBinarySha256':native['binary']['sha256'],'nativeSourceManifestSha256':native['sourceManifest']['sha256'],'nativeMainSha256':native['mainSha256'],'catalogSha256':catalog,'helperSha256':approval['helperSha256'],'coreSha256':approval['coreSha256'],'nativeBuildReceipt':{'path':str(work/'native-candidate.json'),'sha256':sha(work/'native-candidate.json')},'actualHelperBehavior':{'path':str(signalPath),'sha256':sha(signalPath)},'inputs':{'path':str(out/'inputs.json'),'sha256':sha(out/'inputs.json')},'compiledConsumer':{'path':str(exe),'sha256':sha(exe)},'fixtureSource':{'path':str(out/'consumer.cpp'),'sha256':sha(out/'consumer.cpp')},'extractedCodeSha256':{'command':hashlib.sha256(prefix.encode()).hexdigest(),'parser':hashlib.sha256(parser.encode()).hexdigest()},'cases':actual['cases'],'processApiLinked':False,'nativeCandidateExecuted':False,'hardwareIo':False,'physicalAcceptance':False}
    save(out/'result.json',result);print(json.dumps({'status':result['status'],'cases':len(result['cases'])}),flush=True)
    if proc.returncode or actual['status']!='PASS':raise RuntimeError('ACTUAL_NATIVE_CONSUMER_FAILED')
p=argparse.ArgumentParser();p.add_argument('mode',choices=['snapshot','review','build','pdb','consumer']);p.add_argument('--workspace',required=True);p.add_argument('--project-root');p.add_argument('--node');p.add_argument('--helper-candidate');p.add_argument('--helper-sources');a=p.parse_args()
try:{'snapshot':snapshot,'review':review_build,'build':build,'pdb':pdb_proof,'consumer':consumer}[a.mode](a)
except Exception as e:print(type(e).__name__+': '+str(e),file=sys.stderr);raise SystemExit(2)
