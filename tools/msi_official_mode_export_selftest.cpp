// Actual native export adapter and attachment, with inert platform boundaries.
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
unsigned checks=0,rootCalls=0,frees=0,dateCalls=0,reads=0,hashes=0,writes=0,collections=0,clocks=0;
DWORD error=12345,lastCap=0;bool detailed=true,msi=true,saveOkay=true,throwCollect=false,throwRead=false,throwWrite=false;
bool calendarOkay=true,previousOkay=true,readAvailable=true,readTruncated=false;
int rootFailure=0;SYSTEMTIME today{2026,10,4,15,23,59,58,999};
std::vector<std::string> expectedDates{"2026-10-15","2026-10-14"};
GUID data{1,0,0,{}},program{2,0,0,{}},local{3,0,0,{}};
std::wstring lastRead,lastWrite;std::string lastHash,lastBytes,reason;std::uint64_t originalBytes=3;
void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
DWORD getError(){return error;}void setError(DWORD v){error=v;}
ULONGLONG tick(){++clocks;error=777;return 1000;}
HRESULT knownFolder(REFKNOWNFOLDERID id,DWORD flags,HANDLE token,PWSTR* out){
 ++rootCalls;check(flags==KF_FLAG_DONT_VERIFY&&token==nullptr,"known-folder flags/token");
 const std::wstring value=id.Data1==1?L"X:\\DATA":id.Data1==2?L"X:\\PROGRAM":L"X:\\LOCAL";
 *out=new wchar_t[value.size()+1];std::copy(value.begin(),value.end(),*out);(*out)[value.size()]=0;error=111;
 return rootFailure==static_cast<int>(id.Data1)?E_FAIL:S_OK;
}
void freePath(void* value){++frees;delete[] static_cast<wchar_t*>(value);error=112;}
void localTime(LPSYSTEMTIME out){++dateCalls;*out=today;error=113;}
BOOL toFile(const SYSTEMTIME* s,FILETIME* f){return calendarOkay?::SystemTimeToFileTime(s,f):FALSE;}
BOOL fromFile(const FILETIME* f,SYSTEMTIME* s){return previousOkay?::FileTimeToSystemTime(f,s):FALSE;}
}
namespace ymcc::msi::officialLogEvidence {
struct Root {std::string label;std::wstring path;};
struct ReadResult {std::string bytes;bool available=false,truncated=false;std::uint64_t originalBytes=0;std::string reason;};
template<class Ops>json collect(bool enabled,Ops& ops){
 ++fake::collections;if(fake::throwCollect)throw std::runtime_error("fake collector");
 json out={{"enabled",enabled},{"proofs",{{"injector",false}}}};
 if(enabled){
  auto roots=ops.roots();fake::check(roots.size()==3&&roots[0].label=="program-data"&&roots[0].path==L"X:\\DATA"&&roots[1].label=="program-files-x86"&&roots[1].path==L"X:\\PROGRAM"&&roots[2].label=="local-app-data"&&roots[2].path==L"X:\\LOCAL","known root adapter");
  fake::check(ops.dates()==fake::expectedDates,"local today/yesterday adapter");
  auto r=ops.read(L"X:\\NOT-LIVE\\MODE.log",65536);fake::check(r.bytes=="abc"&&r.available&&r.originalBytes==3&&!r.truncated&&r.reason.empty(),"tail bridge");
  out["digest"]=ops.hash(r.bytes);out["tick"]=ops.now();
 }
 return out;
}
}
namespace SleepRecoveryApi {
struct Tail {std::string bytes;json meta;};
Tail readTail(const std::wstring& path,DWORD cap){++fake::reads;fake::lastRead=path;fake::lastCap=cap;fake::error=222;if(fake::throwRead)throw std::runtime_error("fake read");return {"abc",{{"available",fake::readAvailable},{"truncated",fake::readTruncated},{"originalBytes",fake::originalBytes},{"reason",fake::reason}}};}
}
static bool domainLogEnabled(const char* name){fake::check(std::string(name)=="virtual","detail domain");return fake::detailed;}
static bool msiClawKnownBoardAuthority(){return fake::msi;}
static std::string inputHostSha256(const std::string& bytes){++fake::hashes;fake::lastHash=bytes;fake::error=333;return std::string(64,'a');}
static std::wstring SG_DIR=L"X:\\NOT-LIVE";
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& bytes){++fake::writes;fake::lastWrite=path;fake::lastBytes=bytes;fake::error=444;if(fake::throwWrite)throw std::runtime_error("fake write");return fake::saveOkay;}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::tick
#define SHGetKnownFolderPath fake::knownFolder
#define CoTaskMemFree fake::freePath
#define GetLocalTime fake::localTime
#define SystemTimeToFileTime fake::toFile
#define FileTimeToSystemTime fake::fromFile
#define FOLDERID_ProgramData fake::data
#define FOLDERID_ProgramFilesX86 fake::program
#define FOLDERID_LocalAppData fake::local
#include "actual_export.inc"
static std::vector<std::wstring> added;static json availability;
static void addFile(const std::wstring& path){added.push_back(path);}
static void attach(){
#include "actual_attach.inc"
}
static void reset(){
 fake::rootCalls=fake::frees=fake::dateCalls=fake::reads=fake::hashes=fake::writes=fake::collections=fake::clocks=0;
 fake::error=12345;fake::detailed=fake::msi=fake::saveOkay=fake::calendarOkay=fake::previousOkay=fake::readAvailable=true;
 fake::throwCollect=fake::throwRead=fake::throwWrite=fake::readTruncated=false;fake::rootFailure=0;fake::reason.clear();fake::originalBytes=3;
 fake::lastRead.clear();fake::lastWrite.clear();fake::lastHash.clear();fake::lastBytes.clear();added.clear();availability={{"files",json::array()}};
 fake::today={2026,10,4,15,23,59,58,999};fake::expectedDates={"2026-10-15","2026-10-14"};
}
int main(){try{
 reset();auto path=exportMsiOfficialModeLogEvidence();auto j=json::parse(fake::lastBytes);
 fake::check(!path.empty()&&fake::writes==1&&fake::reads==1&&fake::lastCap==65536&&fake::lastHash=="abc","native export forwarding");
 fake::check(fake::rootCalls==3&&fake::frees==3&&fake::dateCalls==1&&fake::clocks==1&&fake::collections==1&&fake::error==12345,"bounded adapter/error restoration");
 fake::check(j["nativeEvidenceVersion"]==1&&j["exportEntry"]=="logs.exportAll"&&j["knownMsiBoard"]==true&&j["detailEnabled"]==true&&j["gateReason"]=="enabled","identity markers");
 fake::check(path==SG_DIR+L"\\steam-input-log-tails\\msi-official-mode-evidence.json","new evidence path");
 fake::check(fake::lastBytes.find("NOT-LIVE")==std::string::npos&&fake::lastBytes.find("abc")==std::string::npos,"raw bytes/path leak");
 for(unsigned gate=0;gate<3;++gate){reset();fake::detailed=gate==0;fake::msi=gate==1;path=exportMsiOfficialModeLogEvidence();j=json::parse(fake::lastBytes);
  fake::check(!path.empty()&&j["enabled"]==false&&fake::rootCalls==0&&fake::dateCalls==0&&fake::reads==0&&fake::hashes==0&&fake::clocks==0,"disabled/non-MSI resolves external evidence");
  fake::check(j["gateReason"]==(fake::detailed?"unsupported-machine":"virtual-detail-disabled")&&fake::error==12345,"disabled reason/caller error");}
 for(unsigned failure=0;failure<4;++failure){reset();fake::saveOkay=failure!=0;fake::throwCollect=failure==1;fake::throwRead=failure==2;fake::throwWrite=failure==3;
  fake::check(exportMsiOfficialModeLogEvidence().empty()&&fake::error==12345,"exception/save failure returned stale path or altered error");}
 reset();fake::rootFailure=2;MsiOfficialModeLogEvidenceOps ops;auto roots=ops.roots();fake::check(roots[1].path.empty()&&roots[0].path==L"X:\\DATA"&&roots[2].path==L"X:\\LOCAL"&&fake::frees==3,"failed folder HRESULT or allocation cleanup");
 reset();fake::readAvailable=false;fake::readTruncated=true;fake::originalBytes=999;fake::reason="read-denied";auto r=ops.read(L"X:\\NONE",37);fake::check(!r.available&&r.truncated&&r.originalBytes==999&&r.reason=="read-denied"&&fake::lastCap==37,"failure tail metadata bridge");
 struct DateCase {WORD y,m,d;std::vector<std::string> expected;};
 const std::vector<DateCase> dates{{2026,1,1,{"2026-01-01","2025-12-31"}},{2024,3,1,{"2024-03-01","2024-02-29"}},{2025,3,1,{"2025-03-01","2025-02-28"}},{2026,11,1,{"2026-11-01","2026-10-31"}},{2026,10,15,{"2026-10-15","2026-10-14"}}};
 for(const auto& d:dates){reset();fake::today={d.y,d.m,0,d.d,0,0,0,0};fake::check(ops.dates()==d.expected,"civil calendar rollover");}
 reset();fake::calendarOkay=false;fake::check(ops.dates()==std::vector<std::string>{"2026-10-15"},"first date conversion failure");
 reset();fake::previousOkay=false;fake::check(ops.dates()==std::vector<std::string>{"2026-10-15"},"previous date conversion failure");
 reset();attach();fake::check(added.size()==1&&added[0].find(L"msi-official-mode-evidence.json")!=std::wstring::npos&&availability["files"].empty(),"current successful attachment");
 reset();fake::saveOkay=false;attach();fake::check(added.empty()&&availability["files"].size()==1&&availability["files"][0]["present"]==false&&availability["files"][0]["missingReason"]=="current-msi-mode-snapshot-not-saved","stale attachment or missing failure");
 std::cout<<json{{"pass",true},{"checks",fake::checks},{"actualOemRead",false},{"actualInput",false},{"collectorReplaced",true}}.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
