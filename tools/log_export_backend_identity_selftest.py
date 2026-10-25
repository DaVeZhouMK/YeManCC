"""Compile the actual backend manifest block against isolated fixture files/hash seam."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
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
