// Native adapter/export wiring only; fake collector and all platform boundaries.
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
unsigned checks=0,sessions=0,validations=0,stability=0,identities=0,reads=0,hashes=0,writes=0,collections=0,clocks=0;
DWORD error=12345,lastCap=0;bool detailed=true,saveOkay=true,throwCollect=false,throwRead=false,throwWrite=false;
std::wstring lastRead,lastWrite;std::string lastHash,lastBytes;
void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
DWORD getError(){return error;}void setError(DWORD e){error=e;}
ULONGLONG tick(){++clocks;error=777;return 1000;}
}
namespace SleepSteamRecovery {
struct Session {std::wstring root=L"X:/NOT-LIVE";DWORD account=123456,pid=77;ULONGLONG created=88;};
Session session(){++fake::sessions;return {};}
bool validSession(const Session& c){++fake::validations;return c.account==123456;}
bool sameSession(const Session& c){++fake::stability;return c.pid==77&&c.created==88;}
json clientIdentity(const Session& c){++fake::identities;return {{"pid",c.pid},{"identityToken","opaque"}};}
std::vector<std::wstring> exportLogTails(){return {L"existing-tail.log"};}
}
namespace SleepRecoveryApi {
struct Tail {std::string bytes;json meta;};
Tail readTail(const std::wstring& p,DWORD cap){++fake::reads;fake::lastRead=p;fake::lastCap=cap;fake::error=222;if(fake::throwRead)throw std::runtime_error("fake read");return {"abc",{{"available",true},{"truncated",false},{"originalBytes",3},{"reason",""}}};}
}
static std::wstring U2W(const std::string& s){return std::wstring(s.begin(),s.end());}
static std::string inputHostSha256(const std::string& b){++fake::hashes;fake::lastHash=b;fake::error=333;return std::string(64,'a');}
static bool domainLogEnabled(const char* s){fake::check(std::string(s)=="virtual","wrong diagnostic gate");return fake::detailed;}
static std::wstring SG_DIR=L"X:/NOT-LIVE";
static bool sgWriteFileAtomic(const std::wstring& p,const std::string& b){++fake::writes;fake::lastWrite=p;fake::lastBytes=b;fake::error=444;if(fake::throwWrite)throw std::runtime_error("fake write");return fake::saveOkay;}
namespace ymcc::steamDesktopCapture {
template<class Ops>json collect(bool enabled,Ops& ops){
 ++fake::collections;if(fake::throwCollect)throw std::runtime_error("fake collector");
 json j={{"enabled",enabled},{"actualInputProven",false}};
 if(enabled){const auto c=ops.session();fake::check(ops.validSession(c)&&ops.sameSession(c),"native session bridge");j["client"]=ops.clientIdentity(c);const auto r=ops.read(L"X:/NOT-LIVE/read.vdf",33);fake::check(r.available&&!r.truncated&&r.bytes=="abc"&&r.originalBytes==3,"native complete-tail metadata bridge");fake::check(ops.widen("example")==L"example","native UTF path bridge");j["hash"]=ops.hash(r.bytes);j["captureTick"]=ops.now();}
 return j;
}
}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::tick
#include "actual_export.inc"
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
static std::vector<std::wstring> added;static json availability;
static void addFile(const std::wstring& p){added.push_back(p);}
static void attach(){
#include "actual_attach.inc"
}
static void reset(){fake::sessions=fake::validations=fake::stability=fake::identities=fake::reads=fake::hashes=fake::writes=fake::collections=fake::clocks=0;fake::error=12345;fake::detailed=true;fake::saveOkay=true;fake::throwCollect=fake::throwRead=fake::throwWrite=false;fake::lastRead.clear();fake::lastWrite.clear();fake::lastHash.clear();fake::lastBytes.clear();added.clear();availability={{"files",json::array()}};}
int main(){try{
 reset();auto p=exportSteamDesktopBindingEvidence();fake::check(!p.empty()&&fake::writes==1&&fake::reads==1&&fake::lastCap==33&&fake::lastHash=="abc","native exporter forwarding");
 auto j=json::parse(fake::lastBytes);fake::check(j["nativeEvidenceVersion"]==1&&j["exportEntry"]=="logs.exportAll","snapshot version/source marker");
 fake::check(fake::lastBytes.find("123456")==std::string::npos&&fake::lastBytes.find("X:/NOT-LIVE")==std::string::npos&&fake::lastBytes.find("abc")==std::string::npos,"adapter exported account/path/raw bytes");
 fake::check(fake::error==12345&&fake::sessions==1&&fake::validations==1&&fake::stability==1&&fake::identities==1&&fake::clocks==1,"adapter calls/error restoration");
 reset();fake::detailed=false;p=exportSteamDesktopBindingEvidence();j=json::parse(fake::lastBytes);fake::check(!p.empty()&&j["enabled"]==false&&fake::writes==1&&fake::sessions==0&&fake::reads==0&&fake::hashes==0,"closed diagnostics still resolved Steam/config");
 for(unsigned failure:{0u,1u,2u,3u}){reset();fake::saveOkay=failure!=0;fake::throwCollect=failure==1;fake::throwRead=failure==2;fake::throwWrite=failure==3;p=exportSteamDesktopBindingEvidence();fake::check(p.empty()&&fake::error==12345,"failed export returned stale path or altered caller error");}
 reset();attach();fake::check(added.size()==2&&added[0]==L"existing-tail.log"&&added[1].find(L"steam-desktop-bindings.json")!=std::wstring::npos&&availability["files"].empty(),"actual exportAll snippet missing current snapshot/old tails");
 reset();fake::saveOkay=false;attach();fake::check(added==std::vector<std::wstring>{L"existing-tail.log"}&&availability["files"].size()==1&&availability["files"][0]["name"]=="steam-desktop-bindings.json"&&availability["files"][0]["present"]==false,"failed new snapshot copied old file or hid missing status");
 std::cout<<json{{"pass",true},{"checks",fake::checks},{"actualOsInput",false},{"actualSteamRead",false},{"collectorReplaced",true},{"scope","Actual adapter and exportAll attachment; collector tested independently"}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
