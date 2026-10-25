// Pure offline checks: production request parsing and DLL preflight only.
// Do not call handle/runTrial, attach a debugger, load the DLL, or open Steam.
#define NOMINMAX
#include <windows.h>
#include <set>
#include <limits>
#include <filesystem>
#include <fstream>
#include "vendor/steam_native_app_state_win32.h"
#include "running-publish-trial.h"
namespace ymcc::steamOriginalRecovery {
using J=nlohmann::json;namespace rw=ymcc::steamrecordwin;namespace tr=running_trial;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::wstring utf16(const std::string& s){need(!s.empty()&&s.size()<32768&&s.find('\0')==std::string::npos,"utf8-text");const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);need(n>0,"utf8");std::wstring w(static_cast<std::size_t>(n),L'\0');need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n)==n,"utf8");return w;}
#define YMCC_STEAM_ORIGINAL_DRIVER_VALIDATION_ONLY
#include "driver_port.inc"
#undef YMCC_STEAM_ORIGINAL_DRIVER_VALIDATION_ONLY
}

#include <iostream>
static unsigned unexpectedTrialProcessOpens=0;
static HANDLE WINAPI offlineForbiddenOpenProcess(DWORD,BOOL,DWORD){++unexpectedTrialProcessOpens;return nullptr;}
#define OpenProcess offlineForbiddenOpenProcess
#include "running-publish-trial.cpp"
#undef OpenProcess
namespace eng=ymcc::steamOriginalRecovery;
using Json=nlohmann::json;
namespace {
Json request(std::uint64_t gameId=10307630032478011392ULL,std::uint32_t appid=2399932135u,const std::string& steamPath="E:\\GAME\\STEAM\\steam.exe"){
 Json value={{"account64","76561198000000001"},{"gameId",std::to_string(gameId)},{"appid",appid},
  {"nonce","01234567-89ab-4cde-8123-456789abcdef"},{"sourceThreadId",901},{"sourceThreadBirth","134370372977050101"},{"noPublish",false},
  {"healthyProof",{{"pinNonce","11234567-89ab-4cde-8123-456789abcdef"},{"healthyFlags",8196},{"ownerPin",std::string(64,'1')},{"recordPin",std::string(64,'2')},{"uiObjectPin",std::string(64,'3')}}}};
 unsigned pid=101;
 for(const auto* role:{"steam","game","ymcc","cef"}){value["processes"][role]={{"pid",pid++},{"birthFileTime","134370372977050100"},{"session",1},{"path",std::string(role)=="steam"?steamPath:"F:\\OfflineFixture\\"+std::string(role)+".exe"}};}
 return value;
}
void validate(const Json& value){eng::validateRequest(value);}
void expect(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void rejects(const Json& value){bool refused=false;try{validate(value);}catch(...){refused=true;}expect(refused,"bad request accepted");}
template<std::size_t N> void put(wchar_t (&dest)[N],const wchar_t* value){const auto count=std::wcslen(value);expect(count<N,"fixture path overflow");std::copy_n(value,count+1,dest);}
running_trial::Packet packet(){running_trial::Packet p{};p.gameId=10307630032478011392ULL;p.account=76561198000000001ULL;p.steamPid=GetCurrentProcessId();p.gamePid=102;p.session=p.ymcc.session=p.cef.session=1;put(p.steamPath,L"E:\\GAME\\STEAM\\steam.exe");put(p.gamePath,L"F:\\OfflineFixture\\game.exe");put(p.ymcc.path,L"F:\\OfflineFixture\\ymcc.exe");put(p.cef.path,L"F:\\OfflineFixture\\cef.exe");return p;}
}
int main(int argc,char** argv){
 if(argc!=2)return 64;
 Json cases=Json::array();unsigned passed=0;
 const auto test=[&](const char* name,const auto& action){try{action();cases.push_back({{"name",name},{"passed",true}});++passed;}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"reason",e.what()}});}};
 test("TMNT-full-type2-on-E-drive",[]{validate(request());});
 test("different-shortcut-and-Steam-directory",[]{validate(request(10260559608980439040ULL,2388972698u,"F:\\Applications\\Steam\\Steam.exe"));});
 test("vendor-type2-AppID-lower-bound",[]{validate(request((1ULL<<32)|(2u<<24),1));});
 test("vendor-type2-AppID-upper-bound",[]{validate(request((0xffffffffULL<<32)|(2u<<24),0xffffffffu));});
 test("Unicode-Steam-directory",[]{validate(request(10307630032478011392ULL,2399932135u,"F:\\应用 程序\\Steam\\steam.exe"));});
 test("numeric-GameID-refused",[]{auto r=request();r["gameId"]=10307630032478011392ULL;rejects(r);});
 test("uint64-overflow-refused",[]{auto r=request();r["gameId"]="18446744073709551616";rejects(r);});
 test("signed-GameID-refused",[]{auto r=request();r["gameId"]="-10307630032478011392";rejects(r);});
 test("empty-GameID-refused",[]{auto r=request();r["gameId"]="";rejects(r);});
 test("missing-GameID-refused",[]{auto r=request();r.erase("gameId");rejects(r);});
 test("native-type0-refused",[]{auto r=request();r["gameId"]="730";rejects(r);});
 test("type1-refused",[]{auto r=request();r["gameId"]=std::to_string((2399932135ULL<<32)|(1u<<24));rejects(r);});
 test("type3-refused",[]{auto r=request();r["gameId"]=std::to_string((2399932135ULL<<32)|(3u<<24));rejects(r);});
 test("type2-low24-bits-refused",[]{auto r=request();r["gameId"]=std::to_string(10307630032478011393ULL);rejects(r);});
 test("zero-shortcut-AppID-refused",[]{auto r=request();r["gameId"]=std::to_string(2u<<24);r["appid"]=0;rejects(r);});
 test("mismatched-AppID-refused",[]{auto r=request();r["appid"]=2388972698u;rejects(r);});
 test("missing-AppID-refused",[]{auto r=request();r.erase("appid");rejects(r);});
 test("string-AppID-refused",[]{auto r=request();r["appid"]="2399932135";rejects(r);});
 test("negative-AppID-refused",[]{auto r=request();r["appid"]=-1;rejects(r);});
 test("uint32-AppID-overflow-refused",[]{auto r=request();r["appid"]=0x100000000ULL;rejects(r);});
 test("relative-Steam-path-refused",[]{auto r=request();r["processes"]["steam"]["path"]="Steam\\steam.exe";rejects(r);});
 test("wrong-Steam-image-refused",[]{auto r=request();r["processes"]["steam"]["path"]="E:\\GAME\\STEAM\\other.exe";rejects(r);});
 test("path-traversal-refused",[]{auto r=request();r["processes"]["steam"]["path"]="E:\\GAME\\..\\Steam\\steam.exe";rejects(r);});
 test("path-embedded-NUL-refused",[]{auto r=request();r["processes"]["steam"]["path"]=std::string("E:\\Steam\\steam.exe\0other",24);rejects(r);});
 test("overlong-Steam-path-refused",[]{auto r=request();r["processes"]["steam"]["path"]="E:\\"+std::string(510,'x')+"\\steam.exe";rejects(r);});
 test("duplicate-process-PID-refused",[]{auto r=request();r["processes"]["game"]["pid"]=r["processes"]["steam"]["pid"];rejects(r);});
 test("changed-Windows-session-refused",[]{auto r=request();r["processes"]["game"]["session"]=2;rejects(r);});
 test("zero-birth-refused",[]{auto r=request();r["processes"]["steam"]["birthFileTime"]="0";rejects(r);});
 test("no-publish-mode-still-refused",[]{auto r=request();r["noPublish"]=true;rejects(r);});
 test("missing-health-proof-refused",[]{auto r=request();r.erase("healthyProof");rejects(r);});
 test("health-pin-shape-bound-without-claiming-health",[]{auto r=request();running_trial::Packet p{};eng::bindExternalHealthyProof(p,r);expect(validHealthyProof(p.healthyProof),"proof shape changed");});
 test("wrong-healthy-flags-refused",[]{auto r=request();r["healthyProof"]["healthyFlags"]=4;running_trial::Packet p{};bool no=false;try{eng::bindExternalHealthyProof(p,r);}catch(...){no=true;}expect(no,"lost flags accepted as healthy");});
 test("malformed-owner-pin-refused",[]{auto r=request();r["healthyProof"]["ownerPin"]="old-health-fingerprint";running_trial::Packet p{};bool no=false;try{eng::bindExternalHealthyProof(p,r);}catch(...){no=true;}expect(no,"bad owner pin accepted");});
 test("module-path-from-bound-Steam-directory",[]{expect(running_trial::steamModulePath(L"E:\\GAME\\STEAM\\steam.exe",L"steamclient64.dll")==L"E:\\GAME\\STEAM\\steamclient64.dll","wrong client directory");expect(running_trial::steamModulePath(L"F:\\Applications\\Steam\\steam.exe",L"SteamUI.dll")==L"F:\\Applications\\Steam\\SteamUI.dll","wrong UI directory");});
 test("module-path-refuses-unbound-module",[]{expect(running_trial::steamModulePath(L"E:\\Steam\\steam.exe",L"other.dll").empty(),"arbitrary module accepted");});
 test("ABI3-path-layout",[]{auto p=packet();expect(p.version==3&&p.size==sizeof(p),"bad ABI");expect(p.qualificationRoute==running_trial::QualificationRoute::SemiStatic&&offsetof(running_trial::Packet,qualificationRoute)==16,"route layout/default changed");expect(running_trial::validPacketPaths(p),"valid bound paths rejected");expect(offsetof(running_trial::Packet,steamPath)==offsetof(running_trial::Packet,gamePath)+1040,"bad packet layout");});
 test("wire-legacy-route-overrides-static-default",[]{auto r=request();r["recoveryRoute"]="legacy-dynamic";validate(r);auto p=packet();eng::bindQualificationRoute(p,r);expect(p.qualificationRoute==running_trial::QualificationRoute::LegacyDynamic,"legacy driver inherited static Packet default");});
 test("wire-explicit-semi-static-route",[]{auto r=request();r["recoveryRoute"]="semi-static";validate(r);auto p=packet();p.qualificationRoute=running_trial::QualificationRoute::LegacyDynamic;eng::bindQualificationRoute(p,r);expect(p.qualificationRoute==running_trial::QualificationRoute::SemiStatic,"static route was not bound");});
 test("wire-missing-route-keeps-semi-static-compatibility",[]{auto p=packet();eng::bindQualificationRoute(p,request());expect(p.qualificationRoute==running_trial::QualificationRoute::SemiStatic,"missing route changed compatibility");});
 test("wire-unknown-route-refused",[]{auto r=request();r["recoveryRoute"]="other";rejects(r);});
 test("wire-numeric-route-refused",[]{auto r=request();r["recoveryRoute"]=1;rejects(r);});
 test("DLL-ABI2-refused-before-process-open",[]{auto p=packet();p.version=2;const auto opens=unexpectedTrialProcessOpens;expect(RunningTrialControl(&p)==99,"ABI2 accepted");expect(unexpectedTrialProcessOpens==opens,"ABI2 opened process before rejection");});
 test("prepare-ABI2-refused-before-process-open",[]{auto p=packet();eng::bindExternalHealthyProof(p,request());p.version=2;const auto opens=unexpectedTrialProcessOpens;expect(prepare(p)==100,"prepare accepted ABI2");expect(unexpectedTrialProcessOpens==opens,"prepare ABI2 opened process");});
 test("DLL-unknown-qualification-route-refused-before-process-open",[]{for(const auto invalid:{0u,3u,0xffffffffu}){auto p=packet();eng::bindExternalHealthyProof(p,request());p.qualificationRoute=static_cast<running_trial::QualificationRoute>(invalid);const auto opens=unexpectedTrialProcessOpens;const bool attempted=preparationAttempted.load();expect(RunningTrialControl(&p)==99,"unknown control route accepted");expect(unexpectedTrialProcessOpens==opens&&preparationAttempted.load()==attempted,"unknown route touched process or attempt");}});
 test("prepare-unknown-qualification-route-refused-before-process-open",[]{for(const auto invalid:{0u,3u,0xffffffffu}){auto p=packet();eng::bindExternalHealthyProof(p,request());p.qualificationRoute=static_cast<running_trial::QualificationRoute>(invalid);const auto opens=unexpectedTrialProcessOpens;const bool attempted=preparationAttempted.load();expect(prepare(p)==100,"unknown prepare route accepted");expect(unexpectedTrialProcessOpens==opens&&preparationAttempted.load()==attempted,"unknown prepare route touched process or attempt");}});
 test("cleanup-identity-reset-preserves-route-and-status",[]{
  const auto saved=bound;const auto opens=unexpectedTrialProcessOpens;
  for(const auto route:{running_trial::QualificationRoute::LegacyDynamic,running_trial::QualificationRoute::SemiStatic}){
   bound=packet();bound.qualificationRoute=route;clearBoundIdentity();expect(bound.qualificationRoute==route&&bound.gameId==0&&bound.account==0,"cleanup reset lost route or kept identity");
   auto p=packet();p.op=running_trial::Op::Status;p.qualificationRoute=route;expect(RunningTrialControl(&p)==0&&p.qualificationRoute==route,"same-route status rejected after identity reset");
   p.qualificationRoute=route==running_trial::QualificationRoute::LegacyDynamic?running_trial::QualificationRoute::SemiStatic:running_trial::QualificationRoute::LegacyDynamic;
   expect(RunningTrialControl(&p)==99,"post-cleanup route switch accepted");
  }bound=saved;expect(unexpectedTrialProcessOpens==opens,"cleanup route check opened a process");
 });
 test("DLL-ABI1-refused-before-process-open",[]{auto p=packet();p.version=1;expect(RunningTrialControl(&p)==99,"ABI1 accepted");});
 test("DLL-wrong-packet-size-refused",[]{auto p=packet();--p.size;expect(RunningTrialControl(&p)==99,"bad packet size accepted");});
 test("DLL-invalid-type2-refused-before-process-open",[]{auto p=packet();++p.gameId;expect(RunningTrialControl(&p)==100,"invalid GameID accepted");});
 test("DLL-zero-shortcut-AppID-refused",[]{auto p=packet();p.gameId=2u<<24;expect(RunningTrialControl(&p)==100,"zero AppID accepted");});
 test("DLL-unterminated-Steam-path-refused",[]{auto p=packet();std::fill(std::begin(p.steamPath),std::end(p.steamPath),L'x');expect(RunningTrialControl(&p)==100,"unbounded Steam path accepted");});
 test("DLL-unterminated-game-path-refused",[]{auto p=packet();std::fill(std::begin(p.gamePath),std::end(p.gamePath),L'x');expect(RunningTrialControl(&p)==100,"unbounded game path accepted");});
 test("DLL-wrong-Steam-image-refused",[]{auto p=packet();put(p.steamPath,L"E:\\Steam\\other.exe");expect(RunningTrialControl(&p)==100,"wrong Steam image accepted");});
 test("DLL-missing-healthy-pins-still-refused",[]{auto p=packet();expect(RunningTrialControl(&p)==108,"missing healthy proof accepted");});
 test("owner-record-UI-pins-remain-bound-to-full-GameID",[]{
  bound=packet();bound.steamBirth=134370372977050100ULL;client.base=0x180000000ULL;ui.base=0x190000000ULL;rc::detail::Selection s{};s.engine=0x200000000ULL;s.user=0x210000000ULL;const std::uint64_t record=0x220000000ULL,address=0x230000020ULL;
  const auto dummy=request();eng::bindExternalHealthyProof(bound,dummy);
  const auto setPin=[](char* out,const std::string& pin){std::copy(pin.begin(),pin.end(),out);out[64]=0;};
  setPin(bound.healthyProof.ownerPin,pinValues({client.base,bound.steamBirth,s.engine,s.user,bound.account}));
  setPin(bound.healthyProof.recordPin,pinValues({bound.steamBirth,s.user,record,bound.gameId}));
  setPin(bound.healthyProof.uiObjectPin,pinValues({bound.steamBirth,ui.base,address-0x20,bound.gameId}));
  expect(matchesSavedHealthy(s,record,address),"exact local synthetic binding rejected");
  expect(!matchesSavedHealthy(s,record+0x330,address),"different record accepted");
  expect(!matchesSavedHealthy(s,record,address+0x80),"different UI object accepted");
  const auto old=bound.gameId;bound.gameId=10260559608980439040ULL;expect(!matchesSavedHealthy(s,record,address),"old-game pins reused");bound.gameId=old;
  ++bound.steamBirth;expect(!matchesSavedHealthy(s,record,address),"different Steam owner accepted");bound={};
 });
 test("product-local-diskHash-preserves-SHA256",[&]{const auto file=std::filesystem::path(argv[1]).parent_path()/L"sha256-fixture.txt";{std::ofstream out(file,std::ios::binary);out<<"abc";}expect(diskHash(file.wstring())=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","disk hash mismatch");expect(diskHash((file.wstring()+L".missing")).empty(),"missing file hash accepted");});
 test("startup-without-host-deadline-retains-30s",[]{expect(eng::mutationDeadlineForRequest(request(),1000)==31000,"startup limit changed");});
 test("short-host-deadline-caps-packet",[]{auto r=request();r["mutationDeadlineTick"]="1250";expect(eng::mutationDeadlineForRequest(r,1000)==1250,"host limit not applied");});
 test("long-host-deadline-cannot-extend-30s",[]{auto r=request();r["mutationDeadlineTick"]="90000";expect(eng::mutationDeadlineForRequest(r,1000)==31000,"local 30s cap extended");});
 test("equal-host-and-local-deadline",[]{auto r=request();r["mutationDeadlineTick"]="31000";expect(eng::mutationDeadlineForRequest(r,1000)==31000,"equal cap rejected");});
 test("setup-time-does-not-refresh-host-deadline",[]{auto r=request();r["mutationDeadlineTick"]="1250";expect(eng::mutationDeadlineForRequest(r,1000)==1250&&eng::mutationDeadlineForRequest(r,1249)==1250,"host cap refreshed during setup");});
 test("host-deadline-expired-during-setup-refused",[]{auto r=request();r["mutationDeadlineTick"]="1250";bool no=false;try{(void)eng::mutationDeadlineForRequest(r,1250);}catch(...){no=true;}expect(no,"setup expiry accepted");});
 test("request-expired-host-deadline-refused",[]{auto r=request();r["mutationDeadlineTick"]="1";rejects(r);});
 test("request-future-host-deadline-accepted",[]{auto r=request();r["mutationDeadlineTick"]=std::to_string(GetTickCount64()+30000);validate(r);});
 test("numeric-host-deadline-refused",[]{auto r=request();r["mutationDeadlineTick"]=90000;rejects(r);});
 test("null-host-deadline-refused",[]{auto r=request();r["mutationDeadlineTick"]=nullptr;rejects(r);});
 test("zero-host-deadline-refused",[]{auto r=request();r["mutationDeadlineTick"]="0";rejects(r);});
 test("negative-host-deadline-refused",[]{auto r=request();r["mutationDeadlineTick"]="-1";rejects(r);});
 test("empty-host-deadline-refused",[]{auto r=request();r["mutationDeadlineTick"]="";rejects(r);});
 test("overflow-host-deadline-refused",[]{auto r=request();r["mutationDeadlineTick"]="18446744073709551616";rejects(r);});
 test("non-decimal-host-deadline-refused",[]{auto r=request();r["mutationDeadlineTick"]="90000ms";rejects(r);});
 test("Arm-one-tick-before-deadline-allowed",[]{eng::requireMutationDeadline(1250,1249);});
 test("Arm-at-deadline-refused",[]{bool no=false;try{eng::requireMutationDeadline(1250,1250);}catch(...){no=true;}expect(no,"Arm accepted at exclusive deadline");});
 test("Arm-after-deadline-refused",[]{bool no=false;try{eng::requireMutationDeadline(1250,1251);}catch(...){no=true;}expect(no,"late Arm accepted");});
 test("Arm-zero-deadline-refused",[]{bool no=false;try{eng::requireMutationDeadline(0,0);}catch(...){no=true;}expect(no,"zero deadline accepted");});
 test("tick-addition-overflow-refused",[]{bool no=false;try{(void)eng::mutationDeadlineForRequest(request(),(std::numeric_limits<std::uint64_t>::max)()-100);}catch(...){no=true;}expect(no,"local deadline wrapped");});
 test("no-trial-process-open",[]{expect(unexpectedTrialProcessOpens==0,"trial opened a process during offline validation");});
 const Json result={{"schema","ymcc.generic-trial-offline.v1"},{"tests",cases.size()},{"passed",passed},{"success",passed==cases.size()},{"abi",running_trial::abi},{"packetBytes",sizeof(running_trial::Packet)},{"steamPathOffset",offsetof(running_trial::Packet,steamPath)},{"cases",cases},{"trialProcessOpens",unexpectedTrialProcessOpens},{"debugAttaches",0},{"compiledDllLoaded",false},{"liveRecoveryVerified",false},{"extraProductExecutables",0}};
 {std::ofstream out(argv[1],std::ios::binary);out<<result.dump(2)<<'\n';}
 std::cout<<result.dump()<<'\n';return passed==cases.size()?0:1;
}
