// Existing production lease + exact launch failures, using only copied inert EXEs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_guard.h"
#include "steam_cssloader_launch.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <json.hpp>
static std::atomic<unsigned> launchAttempts{0};
static DWORD failureCode=ERROR_PRIVILEGE_NOT_HELD;
static std::string failureStage="create-process-with-token";
namespace ymcc::steamcsslaunch {
inline deckylaunch::Result fixtureCreate(const std::wstring&,std::wstring&,wchar_t*,const std::wstring&,STARTUPINFOW&,PROCESS_INFORMATION&) {
 ++launchAttempts;return {false,failureCode,failureStage,"ymcc-current-token"};
}}
#define create fixtureCreate
#include "steam_cssloader_use.h"
#undef create
#include "decky_sidebar_diagnostics.h"
using namespace ymcc;
static steamcsseditor::State noEditor(){return steamcsseditor::State::Absent;}
struct Child { HANDLE handle=nullptr;~Child(){if(handle){TerminateProcess(handle,0);WaitForSingleObject(handle,1000);CloseHandle(handle);}} };
static void start(Child& c,const std::filesystem::path& exe){auto cmd=L"\""+exe.wstring()+L"\" --dummy";STARTUPINFOW s{sizeof(s)};PROCESS_INFORMATION p{};if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&s,&p))throw std::runtime_error("fixture child");CloseHandle(p.hThread);c.handle=p.hProcess;}
static std::wstring self(){wchar_t file[32768];GetModuleFileNameW(nullptr,file,32768);return file;}
static std::string utf8(const std::wstring& v){const auto n=WideCharToMultiByte(CP_UTF8,0,v.data(),int(v.size()),nullptr,0,nullptr,nullptr);std::string b(n,'\0');WideCharToMultiByte(CP_UTF8,0,v.data(),int(v.size()),b.data(),n,nullptr,nullptr);return b;}
static void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
int wmain(int argc,wchar_t** argv){if(argc==2&&std::wstring(argv[1])==L"--dummy"){Sleep(15000);return 0;}try{
 if(argc!=2)throw std::runtime_error("isolated output required");auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
 const auto root=out/(L"fixture-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root);
 const auto steamPath=root/L"steam.exe",cssPath=root/L"CssLoader-Standalone-Headless.exe",script=root/L"steam-appearance.ps1",link=root/L"link.txt";
 for(const auto& file:{steamPath,cssPath})std::filesystem::copy_file(self(),file);
 {std::ofstream f(script);f<<"exit 0\n";}{std::ofstream f(link);f<<utf8(cssPath.wstring());}
 Child steam;start(steam,steamPath);FILETIME b{},e{},k{},u{};GetProcessTimes(steam.handle,&b,&e,&k,&u);
 steamsession::Session session{root.wstring(),GetProcessId(steam.handle),0,(ULONGLONG(b.dwHighDateTime)<<32)|b.dwLowDateTime};
 nlohmann::json records=nlohmann::json::array();
 for(const auto& item:std::vector<std::pair<std::string,DWORD>>{{"shell-not-available",ERROR_NOT_FOUND},{"create-process-with-token",ERROR_PRIVILEGE_NOT_HELD},{"child-token-verification",ERROR_ACCESS_DENIED}}){
  failureStage=item.first;failureCode=item.second;launchAttempts=0;std::atomic<unsigned> failures{0},starts{0},secondary{0};steamcssuse::Cleanup detail;
  Child css;start(css,cssPath);steamcssuse::Runtime runtime(false,35821,noEditor);
  runtime.initialize(script.wstring(),link.wstring(),3000,30,[&](const auto& stage,const auto&,const auto&,const auto& result){if(stage=="idle-helper-failed"){detail=result;++failures;}if(stage=="idle-helper-start")++starts;if(stage=="secondary-cleanup-complete")++secondary;},100);
  deckylaunch::Handle done;done.value=runtime.confirmAndIdle(session);check(done.value==nullptr,"initial helper launch failure rejects the automatic lease");
  check(failures==1,"failed initial helper reports its exact error synchronously");Sleep(100);runtime.shutdown();
  check(failures==1&&starts==0&&launchAttempts==1&&secondary==1,"failure is one launch attempt and one secondary cleanup without retry loop");
  check(detail.error==failureCode&&detail.launchStage==failureStage&&detail.launchMode=="ymcc-current-token","original launch stage and OS code survive catch");
  check(WaitForSingleObject(css.handle,0)==WAIT_OBJECT_0,"only fixture backend is cleaned after failed helper");
  records.push_back({{"stage",detail.launchStage},{"error",detail.error},{"launchMode",detail.launchMode},{"attempts",launchAttempts.load()}});
 }
 nlohmann::json loaders=nlohmann::json::array();for(unsigned i=0;i<20;++i)loaders.push_back({{"pid",i+1},{"birth","100"},{"token","DO_NOT_EXPORT"}});
 auto log=deckydiag::record("steam-css-use",{{"policyVersion",2},{"steamPid",123},{"steamBirth","100"},{"remaining",1},{"unverified",2},{"verified",3},{"minimumHoldMs",30000},{"launchStage","create-process-with-token"},{"launchMode","ymcc-current-token"},{"error",1314},{"helperExitKnown",true},{"helperExitCode",1},{"helperResultKnown",true},{"helperPhase",steamcssuse::helperPhaseName(5)},{"helperReason",steamcssuse::helperFailureName(6)},{"helperFailureCode",6},{"helperHResult",0x80131501u},{"backendReady",true},{"configuredMatchesBackend",true},{"domConfirmed",false},{"minimumWindowConfirmed",false},{"backendExitConfirmed",false},{"backendProcessCount",2},{"editorProtected",true},{"editorStateUnknown",false},{"loaders",loaders},{"token","DO_NOT_EXPORT"},{"endpoint","DO_NOT_EXPORT"},{"backendPath","DO_NOT_EXPORT"}});
 check(log["remaining"]==1&&log["minimumHoldMs"]==30000&&log["helperExitCode"]==1,"cleanup counts/hold and helper exit survive diagnostic projection");
 check(log["helperResultKnown"]==true&&log["helperPhase"]=="dom-confirmation"&&log["helperReason"]=="dom-unconfirmed"&&log["helperFailureCode"]==6&&log["helperHResult"]==0x80131501u,"structured helper failure stage, reason and HRESULT survive the real diagnostic filter");
 check(log["backendReady"]==true&&log["configuredMatchesBackend"]==true&&log["domConfirmed"]==false&&log["minimumWindowConfirmed"]==false&&log["backendExitConfirmed"]==false&&log["backendProcessCount"]==2,"backend readiness stays distinct from DOM, hold and exit confirmations in exported logs");
 check(log["editorProtected"]==true&&log["editorStateUnknown"]==false,"Desktop preservation is retained in the actual exported diagnostic projection");
 check(log["loaders"].size()==16&&log["loaderIdentitiesTruncated"]==true&&log.dump().find("DO_NOT_EXPORT")==std::string::npos,"diagnostics stay bounded and strip unrelated sensitive fields");
 auto other=deckydiag::record("mirror-peer",{{"remaining",1},{"loaders",loaders}});check(!other.contains("remaining")&&!other.contains("loaders"),"CSS diagnostic extension cannot widen unrelated event payloads");
 nlohmann::json result{{"ok",true},{"injectedFailures",records},{"diagnosticProjection",log},{"realSteamOperations",0},{"realCssOperations",0},{"fixtureOnlyProcesses",true}};
 std::ofstream f(out/L"failure-evidence.json");f<<result.dump(2);std::cout<<result.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
