#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
using DWORD=unsigned long;
using HANDLE=void*;
using BOOL=int;
namespace fspath=std::filesystem;
constexpr DWORD ERROR_SUCCESS=0,STILL_ACTIVE=259,WAIT_OBJECT_0=0,WAIT_TIMEOUT=258;
constexpr DWORD INVALID_FILE_ATTRIBUTES=0xffffffff,STARTF_USESHOWWINDOW=1,SW_HIDE=0,CREATE_NO_WINDOW=0x08000000,EXTENDED_STARTUPINFO_PRESENT=0x80000;
constexpr BOOL TRUE=1,FALSE=0;
struct STARTUPINFOW {std::size_t cb=0;DWORD dwFlags=0;unsigned short wShowWindow=0;};
struct PROCESS_INFORMATION {HANDLE hProcess=nullptr,hThread=nullptr;};
struct HidHidePhysicalIdentity {std::string deviceInstanceId;};
struct HidHideVisibilitySnapshot {
    std::string hiddenRaw,applicationsRaw,cloakRaw,inverseRaw;
    std::vector<std::wstring> hidden,applications;
    bool valid=false,cloaking=false;
};
static std::string trim_ascii(std::string text){const auto begin=text.find_first_not_of(" \r\n\t");return begin==std::string::npos?std::string():text.substr(begin,text.find_last_not_of(" \r\n\t")-begin+1);}
static std::wstring U2W(const std::string& text){return std::wstring(text.begin(),text.end());}
static std::string W2U(const std::wstring& text){return std::string(text.begin(),text.end());}
static bool seamActive=false;
static bool ymStartSeamActive(){return seamActive;}
static void ymStartSeamTrace(const char*){}
static std::string reply;
static bool runOk=true;
static DWORD runExit=0;
static std::vector<std::wstring> commands;
static bool hidHideRun(const std::wstring& command,std::string* output=nullptr,DWORD* exitCode=nullptr){
    commands.push_back(command);
    if(exitCode)*exitCode=runExit;
    if(output){
        if(command==L"--dev-list")*output="--dev-hide \"HID\\GAME\"\n";
        else if(command==L"--app-list")*output="--app-reg \"C:\\YMCC\\YeManCC.exe\"\n";
        else if(command==L"--cloak-state")*output="--cloak-on\n";
        else if(command==L"--inv-state")*output="--inv-off\n";
        else *output=reply;
    }
    return runOk;
}
@@PATH_PARSER@@
@@LEGACY_SNAPSHOT@@
@@SNAPSHOT@@
namespace ymcc::nativeForeground {enum class Profile {Msi,Rog,Gpd,Unsupported};}
namespace ymcc::input {
struct OneShotHelperDiagnostic {
    bool prepare(){return false;}
    void started(){}
    STARTUPINFOW* startup(){return nullptr;}
};
}
struct NativeForegroundRefreshTarget {HidHidePhysicalIdentity source;bool xusb=false;};
@@LEASE@@
static MsiNativeForegroundLease g_msiNativeForegroundLease;
static std::atomic<bool> g_msiNativeForegroundCyclePending{false};
static std::atomic<std::uint64_t> g_msiNativeForegroundEdge{7};
static HANDLE childHandle=reinterpret_cast<HANDLE>(11),threadHandle=reinterpret_cast<HANDLE>(12);
static std::vector<DWORD> waits;
static std::vector<HANDLE> closed;
static unsigned launches=0,settles=0,receipts=0,launchFlags=0;
static bool launchOk=true,exitQueryOk=true;
static DWORD waitResult=WAIT_TIMEOUT,childExit=0;
static DWORD GetFileAttributesW(const wchar_t*){return 0;}
static DWORD GetLastError(){return 5;}
static DWORD GetCurrentProcessId(){return 42;}
static DWORD WaitForSingleObject(HANDLE handle,DWORD timeout){if(handle!=childHandle)throw std::runtime_error("foreign handle");waits.push_back(timeout);return waitResult;}
static BOOL GetExitCodeProcess(HANDLE handle,DWORD* code){if(handle!=childHandle)throw std::runtime_error("foreign exit query");*code=childExit;return exitQueryOk?TRUE:FALSE;}
static BOOL CloseHandle(HANDLE handle){closed.push_back(handle);return TRUE;}
static std::wstring inputHostExecutablePath(){return L"C:\\YMCC\\input-host\\YeManInputHost.exe";}
static std::string sha256File(const std::wstring&){return "fixture-image-identity";}
namespace ymcc::processEfficiency {
static bool createProcess(const wchar_t*,wchar_t*,void*,void*,BOOL,DWORD flags,void*,const wchar_t*,STARTUPINFOW*,PROCESS_INFORMATION* process){
    ++launches;launchFlags=flags;if(!launchOk)return false;process->hProcess=childHandle;process->hThread=threadHandle;return true;
}
}
static void msiNativeForegroundSettleHelperDiagnosticLocked(DWORD){++settles;}
static void msiNativeForegroundReceipt(const char*,const char*){++receipts;}
@@CYCLE@@
@@POLL@@
int main(int argc,char** argv){
    unsigned cases=0;
    std::vector<std::string> failures;
    const auto check=[&](const std::string& name,bool ok){++cases;if(!ok)failures.push_back(name);};
    const auto query=[&](const std::string& output,bool expected){
        reply=output;commands.clear();HidHideVisibilitySnapshot snapshot;snapshot.valid=true;snapshot.hidden={L"STALE"};
        const bool ok=msiNativeForegroundSnapshot(snapshot);
        check("snapshot-result:"+output,ok==expected);
        check("one-query-without-apply",commands==std::vector<std::wstring>{L"--dev-list --app-list --cloak-state --inv-state --cancel"});
        check("no-partial-publication",expected?snapshot.valid:!snapshot.valid&&snapshot.hidden.empty());
        return snapshot;
    };
    auto snapshot=query("--dev-hide \"HID\\GAME\"\r\n--app-reg \"C:\\YMCC\\YeManCC.exe\"\r\n--cloak-on\r\n--inv-off\r\n",true);
    check("full-fields-preserved",snapshot.hidden==std::vector<std::wstring>{L"HID\\GAME"}&&snapshot.applications==std::vector<std::wstring>{L"C:\\YMCC\\YeManCC.exe"}&&snapshot.cloaking&&snapshot.inverseRaw=="--inv-off");
    snapshot=query(" \r\n--cloak-off\r\n--inv-on\r\n",true);
    check("empty-lists-cloak-and-inverse-preserved",snapshot.hidden.empty()&&snapshot.applications.empty()&&!snapshot.cloaking&&snapshot.inverseRaw=="--inv-on");
    query("--dev-hide \"HID\\ONE\"\n--dev-hide \"HID\\TWO\"\n--app-reg \"C:\\A B\\reader.exe\"\n--app-reg \"C:\\second.exe\"\n--cloak-on\n--inv-off\n",true);
    for(const std::string& invalid:{"","--cloak-on\n","--inv-off\n","--inv-off\n--cloak-on\n","--cloak-on\n--cloak-off\n--inv-off\n","--cloak-on\n--inv-off\n--inv-on\n","--cloak-on\n--inv-unknown\n","--cloak-on\n--inv-off\nERROR\n","--app-reg \"C:\\reader.exe\"\n--dev-hide \"HID\\GAME\"\n--cloak-on\n--inv-off\n","--dev-hide \"\"\n--cloak-on\n--inv-off\n","--dev-hide \"HID\\GAME\n--cloak-on\n--inv-off\n","--dev-hide \"HID\"trailing\"\n--cloak-on\n--inv-off\n","--cloak-on\n--app-reg \"C:\\reader.exe\"\n--inv-off\n","--cloak-on\n--inv-off\n--dev-unhide \"HID\\GAME\"\n"})query(invalid,false);
    std::string embedded="--cloak-on\n--inv-off";embedded.push_back('\0');embedded+="\n";query(embedded,false);
    runOk=false;query("--cloak-on\n--inv-off\n",false);runOk=true;
    runExit=5;query("--cloak-on\n--inv-off\n",false);runExit=0;
    commands.clear();seamActive=true;snapshot.valid=true;
    check("start-seam-no-cli",!msiNativeForegroundSnapshot(snapshot)&&commands.empty()&&!snapshot.valid);seamActive=false;
    const auto reset=[&]{g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundLease.root="USB\\VID_0DB0&PID_1901\\PINNED";g_msiNativeForegroundLease.locations={L"PINNED-LOCATION"};g_msiNativeForegroundCyclePending=false;waits.clear();closed.clear();launches=settles=receipts=launchFlags=0;launchOk=exitQueryOk=true;waitResult=WAIT_TIMEOUT;childExit=0;};
    reset();
    check("running-helper-returns-pending",msiNativeForegroundCycleLocked(false)==1);
    check("no-lock-held-wait",waits==std::vector<DWORD>{0});
    check("same-helper-retained",g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.cycleProcess==childHandle&&!g_msiNativeForegroundLease.cycleAccepted&&closed==std::vector<HANDLE>{threadHandle});
    check("hidden-process-launch",launches==1&&(launchFlags&CREATE_NO_WINDOW));
    const auto oldRoot=g_msiNativeForegroundLease.root;
    HidHidePhysicalIdentity game;game.deviceInstanceId="HID\\SECOND";
    check("pending-does-not-relaunch-or-relabel",msiNativeForegroundCycleLocked(true,&game)==1&&launches==1&&!g_msiNativeForegroundLease.cycleForRestore&&g_msiNativeForegroundLease.helperGameTarget.empty()&&g_msiNativeForegroundLease.root==oldRoot);
    check("pending-poll-is-nonblocking",msiNativeForegroundPollCycleLocked()==-1&&waits.back()==0&&g_msiNativeForegroundCyclePending);
    waitResult=WAIT_OBJECT_0;
    check("same-helper-completes-on-existing-maintenance",msiNativeForegroundPollCycleLocked()==0&&!g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.cycleProcess==nullptr&&g_msiNativeForegroundLease.cycleAccepted&&settles==1&&receipts==1);
    check("child-handle-closed-once",std::count(closed.begin(),closed.end(),childHandle)==1);
    check("idle-maintenance-no-wait",msiNativeForegroundPollCycleLocked()==-1&&waits.size()==3);
    for(DWORD failure:{DWORD(6),DWORD(8)}){
        reset();msiNativeForegroundCycleLocked(true);waitResult=WAIT_OBJECT_0;childExit=failure;
        check("failed-helper-not-success",msiNativeForegroundPollCycleLocked()==2&&!g_msiNativeForegroundCyclePending&&!g_msiNativeForegroundLease.cycleAccepted&&g_msiNativeForegroundLease.helperExitCode==failure);
    }
    reset();waitResult=WAIT_OBJECT_0;
    check("already-complete-helper",msiNativeForegroundCycleLocked(false)==0&&!g_msiNativeForegroundCyclePending&&settles==1&&waits==std::vector<DWORD>{0});
    reset();launchOk=false;
    check("launch-failure-has-no-wait",msiNativeForegroundCycleLocked(false)==2&&waits.empty()&&!g_msiNativeForegroundCyclePending);
    reset();g_msiNativeForegroundLease.locations.clear();
    check("missing-location-no-launch",msiNativeForegroundCycleLocked(false)==2&&launches==0);
    reset();g_msiNativeForegroundLease.profile=ymcc::nativeForeground::Profile::Rog;
    check("rog-root-rejected",msiNativeForegroundCycleLocked(false)==2&&launches==0);
    reset();g_msiNativeForegroundLease.profile=ymcc::nativeForeground::Profile::Gpd;
    check("gpd-root-rejected",msiNativeForegroundCycleLocked(false)==2&&launches==0);
    reset();g_msiNativeForegroundLease.profile=ymcc::nativeForeground::Profile::Rog;
    check("game-interface-keeps-same-async-owner",msiNativeForegroundCycleLocked(false,&game)==1&&g_msiNativeForegroundLease.helperGameTarget==game.deviceInstanceId&&waits==std::vector<DWORD>{0});
    const json report={{"passed",failures.empty()},{"cases",cases},{"failures",failures},{"hardwareAccess",false},{"realProcessesLaunched",false}};
    if(argc>1){std::ofstream file(argv[1]);file<<report.dump(2)<<"\n";}
    std::cout<<report.dump()<<"\n";
    return failures.empty()?0:1;
}
