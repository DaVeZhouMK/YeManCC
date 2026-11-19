"""Compile the actual backend manifest block against isolated fixture files/hash seam."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]

def running_native(root,out):
 import os,time
 if any(out.iterdir()):raise RuntimeError('fresh running-native output required')
 native=(root/'native/main.cpp').read_text('utf-8-sig')
 begin=native.index('        // REQUIRED ordinary running-build evidence: compiled literal, not disk metadata.')
 end=native.index('        // REQUIRED ordinary snapshot: no source refresh/query or callback I/O.',begin)
 original=native[begin:end]
 assert original.count('ymcc::buildidentity::marker()')==1 and 'exportArgs' not in original
 def put(path,content):
  with path.open('x',encoding='utf-8',newline='\n') as f:f.write(content)
 preambleScript='import {calibrationToolchainPreamble} from '+json.dumps((root/'tools/fixtures/hc_reviewed_calibration/toolchain.mjs').as_uri())+';process.stdout.write(calibrationToolchainPreamble("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat"));'
 preamble=subprocess.run(['node','--input-type=module','-e',preambleScript],capture_output=True,text=True,encoding='utf-8',check=True,timeout=10000).stdout
 cpp=r"""
#include <windows.h>
#include <sstream>
#include <iostream>
#include <string>
#include "json.hpp"
static unsigned birthQueries=0;
static BOOL WINAPI countedGetProcessTimes(HANDLE h,LPFILETIME a,LPFILETIME b,LPFILETIME c,LPFILETIME d){++birthQueries;return ::GetProcessTimes(h,a,b,c,d);}
#define GetProcessTimes countedGetProcessTimes
#include "sleep_fact_log_record.h"
#undef GetProcessTimes
#include "runtime_build_identity.h"
using json=nlohmann::json;
static const std::string fakeDiskMarker=std::string(ymcc::buildidentity::prefix)+std::string(64,'f');
static json observe(){
 json availability={{"detailEnabled",false},{"runningNativeBuild",{{"markerKnown",true},{"compiledMarker",fakeDiskMarker},{"writerPid",0}}}};
#include "RunningNativeIdentity.inc"
 return availability;
}
int main(){
 json checks=json::array();unsigned failures=0;
 const auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});if(!ok)++failures;};
 const auto first=observe(),second=observe();const auto& value=second.at("runningNativeBuild");
 check("ordinary-export-works-with-detail-off",first.at("detailEnabled")==false&&second.at("detailEnabled")==false&&value.at("detailLogsRequired")==false);
 check("current-executing-pid-overwrites-spoof",value.at("writerPid")==GetCurrentProcessId()&&value.at("writerPid")!=0);
 FILETIME a{},b{},c{},d{};const BOOL known=::GetProcessTimes(GetCurrentProcess(),&a,&b,&c,&d);
 const auto birth=(static_cast<unsigned long long>(a.dwHighDateTime)<<32)|a.dwLowDateTime;
 check("current-process-birth-matches-original-writer",known?value.at("writerProcessCreation100ns")==std::to_string(birth):value.at("writerProcessCreation100ns").is_null());
 check("cached-writer-birth-not-requeried-per-export",birthQueries==1);
 check("running-marker-is-executing-literal-not-disk",ymcc::buildidentity::available?
    value.at("markerKnown")==true&&value.at("compiledMarker")==std::string(ymcc::buildidentity::marker())&&value.at("compiledMarker")!=fakeDiskMarker:
    value.at("markerKnown")==false&&value.at("compiledMarker").is_null());
 check("diagnostic-grants-no-power-action",value.at("powerActionAuthorized")==false);
 check("scope-excludes-disk-and-historical-writers",value.at("evidenceScope")=="executing-main-compiled-marker-at-export;not-disk-identity-or-historical-writers");
 check("same-process-exports-keep-same-identity",first.at("runningNativeBuild")==second.at("runningNativeBuild"));
 std::cout<<json({{"checks",checks},{"failures",failures},{"markerAvailable",ymcc::buildidentity::available},{"snapshot",value},{"realSystemSleep",false}}).dump()<<'\n';
 return failures?1:0;
}
"""
 rows=[]
 for mode in ['marked','unmarked','disk-substitution-negative']:
  directory=out/mode;directory.mkdir()
  prefix='#pragma once\n'+('#define YMCC_NATIVE_BUILD_MARKER "YMCC-NATIVE-BUILD:v1:'+('b'*64)+'"\n' if mode!='unmarked' else '')
  put(directory/'BuildMarker.h',prefix)
  block=original if mode!='disk-substitution-negative' else original.replace('ymcc::buildidentity::marker()','fakeDiskMarker.c_str()')
  put(directory/'RunningNativeIdentity.inc',block);put(directory/'Fixture.cpp',cpp)
  cmd=preamble+'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W4 /WX /sourceDependencies "'+str(directory/'dependencies.json')+'" /FI"'+str(directory/'BuildMarker.h')+'" /I"'+str(directory)+'" /I"'+str(root/'native')+'" /I"'+str(root/'deps/json')+'" "'+str(directory/'Fixture.cpp')+'" /Fo"'+str(directory/'Fixture.obj')+'" /Fe"'+str(directory/'Fixture.exe')+'" /link /VERBOSE:LIB\n'
  put(directory/'compile.cmd',cmd)
  compiled=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(directory/'compile.cmd')],cwd=directory,capture_output=True,timeout=60000)
  (directory/'compile.log').write_bytes(compiled.stdout+compiled.stderr)
  if compiled.returncode:raise RuntimeError('running native compile failed '+mode+': '+str(compiled.returncode))
  executed=subprocess.run([str(directory/'Fixture.exe')],cwd=directory,capture_output=True,text=True,encoding='utf-8',timeout=5000)
  put(directory/'stdout.log',executed.stdout);put(directory/'stderr.log',executed.stderr)
  result=json.loads(executed.stdout)
  if mode=='disk-substitution-negative':
   assert executed.returncode==1 and result['failures']==1
   assert [x['name'] for x in result['checks'] if not x['pass']]==['running-marker-is-executing-literal-not-disk']
  else:assert executed.returncode==0 and result['failures']==0
  put(directory/'result.json',json.dumps(result,indent=2)+'\n')
  rows.append(dict(mode=mode,compileExit=compiled.returncode,runExit=executed.returncode,checks=len(result['checks']),failures=result['failures'],expectedFaultCaught=mode=='disk-substitution-negative',blockSha256=hashlib.sha256(block.encode()).hexdigest()))
 result=dict(status='PASS_RUNNING_NATIVE_EXPORT_SCOPE',componentScope='native-running',scenarios=rows,originalBlockSha256=hashlib.sha256(original.encode()).hexdigest(),nativeSourceSha256=hashlib.sha256((root/'native/main.cpp').read_bytes()).hexdigest(),sourceBeginUtf8=len(native[:begin].encode()),sourceEndUtf8=len(native[:end].encode()),oldBackendScopeExecuted=False,realSystemSleep=False,applicationControl=False)
 put(out/'result.json',json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(status=result['status'],scenarios=len(rows),checks=sum(x['checks'] for x in rows))))
 return 0

def main():
 p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);p.add_argument('--component-scope',choices=['backend','native-running'],default='backend');a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
 if a.component_scope=='native-running':return running_native(ROOT,out)
 data=(ROOT/'native/main.cpp').read_bytes();s=data.decode('utf-8-sig').replace('\r\n','\n');start=s.index('                    // Export-only disk identity. The autonomous backend');end=s.index('                    const std::wstring hostPayloadManifest =',start);block=s[start:end]
 (out/'BackendIdentity.inc').write_text(block,encoding='utf-8')
 runtimeStart=s.index('                    // The runtime manifest is distinct');runtimeEnd=s.index('                    manifest << "hostRuntimeIdentitySource="',runtimeStart);runtimeBlock=s[runtimeStart:runtimeEnd]
 (out/'RuntimeIdentity.inc').write_text(runtimeBlock,encoding='utf-8')
 cpp=r'''
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include "json.hpp"
namespace fspath=std::filesystem;static unsigned hashes=0;
static std::string sha256File(const std::wstring& path){++hashes;std::ifstream file{fspath::path(path)};char c='?';file.get(c);return std::string(64,c);}
static std::string observe(const fspath::path& root){const std::wstring POWER_CONTROL_DIR=root.wstring();std::ostringstream manifest;
#include "BackendIdentity.inc"
const std::wstring hcAssembly=(root/L"runtime"/L"HandheldCompanion.dll").wstring();
#include "RuntimeIdentity.inc"
return manifest.str();}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;const fspath::path base=argv[1];fspath::create_directories(base);
nlohmann::json checks=nlohmann::json::array();int failed=0;auto check=[&](const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});if(!pass)++failed;};
auto missing=observe(base/L"missing");check("absent-backend-explicitly-missing",missing.find("fanBackendDllPresent=0\n")!=std::string::npos&&missing.find("fanBackendDllSha256=missing\n")!=std::string::npos);check("absent-backend-is-not-hashed",hashes==0);
auto dir=base/L"directory"/L"fan-host-v2"/L"YeManFanBackend.dll";fspath::create_directories(dir);auto directory=observe(base/L"directory");check("directory-is-not-a-backend-file",directory==missing&&hashes==0);
auto file=base/L"present"/L"fan-host-v2"/L"YeManFanBackend.dll";fspath::create_directories(file.parent_path());{std::ofstream f(file,std::ios::binary);f<<'a';}
auto present=observe(base/L"present");check("present-backend-hashed-once",hashes==1&&present.find("fanBackendDllPresent=1\n")!=std::string::npos&&present.find(std::string(64,'a'))!=std::string::npos);
check("disk-evidence-scope-explicit",present.find("export-time-disk;not-loaded-module-proof;not-atomic-with-logs")!=std::string::npos);
{std::ofstream f(file,std::ios::binary);f<<'b';}auto next=observe(base/L"present");check("next-export-reads-current-file-without-cache",hashes==2&&next.find(std::string(64,'b'))!=std::string::npos);
std::ifstream verify(file,std::ios::binary);char c=0;verify.get(c);check("export-does-not-change-bytes",c=='b');
check("missing-runtime-explicit-not-host-payload",missing.find("runtimeManifestSha256=missing\n")!=std::string::npos);
auto runtime=base/L"runtime-present"/L"runtime"/L"HandheldCompanion.runtime.json";fspath::create_directories(runtime.parent_path());{std::ofstream f(runtime,std::ios::binary);f<<'d';}auto runtimeOnly=observe(base/L"runtime-present");check("runtime-neighbor-independent-of-backend",runtimeOnly.find("fanBackendDllSha256=missing\n")!=std::string::npos&&runtimeOnly.find("runtimeManifestSha256="+std::string(64,'d'))!=std::string::npos);
auto payload=base/L"payload-only"/L"fan-host-v2"/L"YeManFanHost.payload.json";fspath::create_directories(payload.parent_path());{std::ofstream f(payload,std::ios::binary);f<<'f';}auto payloadOnly=observe(base/L"payload-only");check("host-payload-never-substitutes-runtime-manifest",payloadOnly.find("runtimeManifestSha256=missing\n")!=std::string::npos);
std::ifstream runtimeVerify(runtime,std::ios::binary);char runtimeByte=0;runtimeVerify.get(runtimeByte);check("runtime-export-preserves-file-bytes",runtimeByte=='d');
std::cout<<nlohmann::json({{"checks",checks},{"failures",failed},{"scope","actual export statements; isolated files; deterministic hash seam; no installed file or product"}}).dump(2)<<'\n';return failed?1:0;}
'''
 (out/'Fixture.cpp').write_text(cpp,encoding='utf-8')
 preambleScript='import {calibrationToolchainPreamble} from '+json.dumps((ROOT/'tools/fixtures/hc_reviewed_calibration/toolchain.mjs').as_uri())+';process.stdout.write(calibrationToolchainPreamble("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat"));'
 preamble=subprocess.run(['node','--input-type=module','-e',preambleScript],capture_output=True,text=True,encoding='utf-8',check=True).stdout
 cmd=preamble+'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W4 /WX /I"'+str(out)+'" /I"'+str(ROOT/'deps/json')+'" "'+str(out/'Fixture.cpp')+'" /Fo"'+str(out/'Fixture.obj')+'" /Fe"'+str(out/'Fixture.exe')+'"\n'
 (out/'compile.cmd').write_text(cmd,encoding='ascii');r=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr)
 if r.returncode:print((r.stdout+r.stderr).decode('utf-8',errors='replace'));return r.returncode
 r=subprocess.run([str(out/'Fixture.exe'),str(out/'files')],capture_output=True,text=True,encoding='utf-8');result=json.loads(r.stdout);result['blockSha256']=hashlib.sha256(block.encode()).hexdigest();result['runtimeBlockSha256']=hashlib.sha256(runtimeBlock.encode()).hexdigest();result['nativeSourceSha256']=hashlib.sha256(data).hexdigest()
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps({'checks':len(result['checks']),'failures':result['failures'],'blockSha256':result['blockSha256']}));return r.returncode
if __name__=='__main__':raise SystemExit(main())
