#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "source/native/input_host_transport_state.h"
#include "source/native/input_host_transaction_mutex.h"
#include "source/native/msi_steamdeck_release_fence_1726.h"
#include "source/native/ps_endpoint_ownership.h"
using json=nlohmann::json;using USAGE=USHORT;
// Conversion cut: every test ID/command is ASCII; no conversion calls hit Win32.
static std::string W2U(const std::wstring& s){std::string r;for(const auto c:s){if(static_cast<unsigned>(c)>127)throw std::runtime_error("fixture ASCII-only conversion");r.push_back(static_cast<char>(c));}return r;}
static std::wstring U2W(const std::string& s){std::wstring r;for(const unsigned char c:s){if(c>127)throw std::runtime_error("fixture ASCII-only conversion");r.push_back(static_cast<wchar_t>(c));}return r;}
#include "actual-types.inc"
namespace fake {
const std::wstring owned=L"HID\\VID_1234&PID_5678\\OWNED",foreign=L"HID\\VID_7777&PID_7777\\FOREIGN",app=L"G:\\106-private\\YMCC.exe",foreignApp=L"G:\\106-private\\Other.exe";
struct World {bool msi=false,queryOk=true,mutationAck=false,apply=false,appAck=true,appApply=true,pipeOk=false,pipeMalformed=false,exitCodeKnown=true;std::vector<std::wstring> hidden,apps;bool cloak=true;json commands=json::array(),driverMutations=json::array(),pipeRequests=json::array(),logs=json::array(),ports=json::array();unsigned invalidations=0,cycleCalls=0,crashReturnCalls=0;ULONGLONG clock=100;} w;
struct Pipe {std::string bytes;std::size_t pos=0;};
struct Handle {enum Kind {Read,Write,Process,Thread} kind;std::shared_ptr<Pipe> pipe;DWORD exitCode=0;bool exitKnown=true;};
std::map<std::uintptr_t,Handle> handles;std::uintptr_t next=1000;DWORD error=0;
HANDLE add(Handle h){const auto id=++next;handles.emplace(id,std::move(h));return reinterpret_cast<HANDLE>(id);}
Handle& get(HANDLE h){const auto it=handles.find(reinterpret_cast<std::uintptr_t>(h));if(it==handles.end())throw std::runtime_error("fake leaf received unknown handle");return it->second;}
DWORD lastError(){return error;}ULONGLONG tick(){return w.clock;}void sleep(DWORD ms){w.clock+=ms;w.ports.push_back({{"api","Sleep"},{"fakeOnly",true},{"milliseconds",ms}});}
DWORD attributes(LPCWSTR p){w.ports.push_back({{"api","GetFileAttributesW"},{"path",W2U(p)},{"fakeOnly",true}});return FILE_ATTRIBUTE_NORMAL;}
BOOL close(HANDLE h){return handles.erase(reinterpret_cast<std::uintptr_t>(h))?TRUE:FALSE;}
BOOL pipe(PHANDLE read,PHANDLE write,LPSECURITY_ATTRIBUTES,DWORD){auto p=std::make_shared<Pipe>();*read=add({Handle::Read,p});*write=add({Handle::Write,p});return TRUE;}
BOOL handleInfo(HANDLE h,DWORD,DWORD){get(h);return TRUE;}
BOOL peek(HANDLE h,LPVOID,DWORD,LPDWORD,LPDWORD avail,LPDWORD){auto& p=*get(h).pipe;*avail=DWORD(p.bytes.size()-p.pos);return TRUE;}
BOOL read(HANDLE h,LPVOID out,DWORD size,LPDWORD got,LPOVERLAPPED){auto& p=*get(h).pipe;const auto n=(std::min)(std::size_t(size),p.bytes.size()-p.pos);std::memcpy(out,p.bytes.data()+p.pos,n);p.pos+=n;*got=DWORD(n);return TRUE;}
DWORD wait(HANDLE h,DWORD timeout){auto& item=get(h);if(item.kind!=Handle::Process||timeout!=0)throw std::runtime_error("unexpected nonprocess/blocking wait in real CLI");return WAIT_OBJECT_0;}
BOOL exitCode(HANDLE h,LPDWORD out){auto& item=get(h);if(!item.exitKnown)return FALSE;*out=item.exitCode;return TRUE;}
BOOL terminate(HANDLE h,UINT){get(h);w.ports.push_back({{"api","TerminateProcess"},{"fakeOnly",true}});return TRUE;}
std::wstring argPath(const std::wstring& cmd){const auto a=cmd.find(L'"'),z=cmd.rfind(L'"');return a==std::wstring::npos||z<=a?L"":cmd.substr(a+1,z-a-1);}
std::string listed(const std::vector<std::wstring>& paths,const char* prefix){std::string s;for(const auto& p:paths)s+=std::string(prefix)+" \""+W2U(p)+"\"\r\n";return s;}
BOOL create(LPCWSTR,LPWSTR command,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi){
 const std::wstring full=command;const auto quoteEnd=full.find(L'"',1);if(full.empty()||full[0]!=L'"'||quoteEnd==std::wstring::npos)throw std::runtime_error("unbound fake CLI command");const auto cmd=full.substr(quoteEnd+2);const bool mutation=cmd.rfind(L"--dev-unhide",0)==0||cmd.rfind(L"--app-unreg",0)==0||cmd==L"--cloak-off";bool ok=true;std::string output;
 if(mutation){const auto p=argPath(cmd);const bool application=cmd.rfind(L"--app-unreg",0)==0;ok=application?w.appAck:w.mutationAck;const bool apply=application?w.appApply:w.apply;const auto before=w.hidden.size(),appsBefore=w.apps.size();const bool cloakBefore=w.cloak;if(apply){if(cmd.rfind(L"--dev-unhide",0)==0)w.hidden.erase(std::remove(w.hidden.begin(),w.hidden.end(),p),w.hidden.end());else if(application)w.apps.erase(std::remove(w.apps.begin(),w.apps.end(),p),w.apps.end());else w.cloak=false;}
  w.driverMutations.push_back({{"command",W2U(cmd)},{"path",W2U(p)},{"ownedPath",p==owned},{"foreignPath",p==foreign},{"driverLeafReached",true},{"ack",ok},{"configuredApply",apply},{"hiddenDelta",int(w.hidden.size())-int(before)},{"applicationsDelta",int(w.apps.size())-int(appsBefore)},{"cloakBefore",cloakBefore},{"cloakAfter",w.cloak},{"fakeDriverOnly",true}});
 }else{ok=w.queryOk;if(ok){if(cmd==L"--dev-list")output=listed(w.hidden,"--dev-hide");else if(cmd==L"--app-list")output=listed(w.apps,"--app-reg");else if(cmd==L"--cloak-state")output=w.cloak?"--cloak-on":"--cloak-off";else if(cmd==L"--inv-state")output="--inv-off";else throw std::runtime_error("unexpected query command");}}
 w.commands.push_back({{"command",W2U(cmd)},{"mutation",mutation},{"driverReached",true},{"exitCode",ok?0:5},{"exitCodeKnown",!mutation||w.exitCodeKnown}});auto p=get(si->hStdOutput).pipe;p->bytes=output;pi->hProcess=add({Handle::Process,p,ok?0U:5U,!mutation||w.exitCodeKnown});pi->hThread=add({Handle::Thread,p});return TRUE;
}
}
#define GetLastError fake::lastError
#define GetTickCount64 fake::tick
#define GetFileAttributesW fake::attributes
#define CloseHandle fake::close
#define CreatePipe fake::pipe
#define SetHandleInformation fake::handleInfo
#define PeekNamedPipe fake::peek
#define ReadFile fake::read
#define WaitForSingleObject fake::wait
#define GetExitCodeProcess fake::exitCode
#define TerminateProcess fake::terminate
#define Sleep fake::sleep
// Unsupported OS/device/persistence entry points fail compilation if a future
// extracted consumer reaches them. The fixture output file below uses only CRT
// streams; standard-library headers were parsed before these fences.
#define CreateFileW forbidden_real_CreateFileW
#define CreateFileA forbidden_real_CreateFileA
#define WriteFile forbidden_real_WriteFile
#define DeviceIoControl forbidden_real_DeviceIoControl
#define CreateProcessW forbidden_real_CreateProcessW
#define CreateProcessA forbidden_real_CreateProcessA
#define ShellExecuteW forbidden_real_ShellExecuteW
#define ShellExecuteExW forbidden_real_ShellExecuteExW
#define GetEnvironmentVariableW forbidden_real_GetEnvironmentVariableW
#define GetEnvironmentVariableA forbidden_real_GetEnvironmentVariableA
#define SHGetKnownFolderPath forbidden_real_SHGetKnownFolderPath
#define SHGetFolderPathW forbidden_real_SHGetFolderPathW
#define RegOpenKeyExW forbidden_real_RegOpenKeyExW
#define LoadLibraryW forbidden_real_LoadLibraryW
#define LoadLibraryA forbidden_real_LoadLibraryA
#define GetProcAddress forbidden_real_GetProcAddress

namespace ymcc::processEfficiency {static BOOL createProcess(LPCWSTR a,LPWSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD f,LPVOID g,LPCWSTR h,LPSTARTUPINFOW i,LPPROCESS_INFORMATION j){return fake::create(a,b,c,d,e,f,g,h,i,j);}}
static ULONGLONG g_inputHostSwitchRecoveryStartDeadline=0;
static HANDLE g_inputHostProcess=nullptr;
static bool g_inputHostTransportFault=false,g_inputHostPipeRetired=false,g_inputHostHello=false,g_inputHostPipeConnected=false,g_inputHostPrepared=false,g_inputHostPrepareMayHaveReachedHost=false,g_inputHostNeutralized=false,g_inputHostActive=false,g_inputHostReleaseQuarantined=false;
static std::wstring g_inputHostPipeName;static std::string g_inputHostRunId,g_inputHostStartupNonce;static std::uint64_t g_inputHostEpoch=1;
static bool g_xinputPhysicalSourceLocked=true;static DWORD g_xinputConnectedMask=1,g_xinputPhysicalSourceMask=1;
static ymcc::msi::sdRelease1726::Session g_msiSteamDeckReleaseFence,g_msiSteamDeckReleaseDebt;
static ymcc::psEndpointOwnership::State g_psEndpointOwnership;
using InputHostMutex=ymcc::inputHost::TransactionMutex<>;static InputHostMutex g_inputHostMx;
static HidHideVisibilityJournal g_hidHideVisibilityJournal;static HidHideCarriedResponsibility g_hidHideCarriedResponsibility;static std::mutex g_hidHideCarriedMx;
static const std::wstring POWER_CONTROL_DIR=L"G:\\106-private";
static bool msiClawKnownBoardAuthority(){return fake::w.msi;}
static bool ymStartSeamActive(){return false;}
static void ymStartSeamTrace(const std::string&){throw std::runtime_error("restore seam shortcut must never run");}
static std::wstring hidHideCliPath(){return L"G:\\106-private\\HidHideCLI.exe";}
static std::wstring hidHideCurrentModulePath(){return fake::app;}
static std::wstring inputHostExecutablePath(){return L"G:\\106-private\\YeManInputHost.exe";}
static void appendNativeLifecycleLog(const char* name,json row){fake::w.logs.push_back({{"name",name},{"row",std::move(row)},{"journalActiveAtSink",g_hidHideVisibilityJournal.active},{"journalTransactionAtSink",g_hidHideVisibilityJournal.transactionId}});}
static void hidHideInvalidateRawInputIdentityCache(){++fake::w.invalidations;}
// Deliberately cut physical cycle, logger facts and separate MSI disk callback.
// Their real implementations could reach a device or installed AppData. These
// ports are recorded; no tested case needs a cycle to prove configuration return.
static bool hidHideCycleInstance(const std::wstring& p,const char* reason){++fake::w.cycleCalls;fake::w.ports.push_back({{"api","physical-cycle"},{"path",W2U(p)},{"phase",reason},{"fakeOnly",true}});return false;}
static int hidHideVisibilitySnapshotFactsLocked(){return 0;}
static void inputCaptureLogVisibilitySnapshotRows(const HidHideVisibilitySnapshot&,bool,int,const char*,bool){}
static void msiCrashRecoveryAfterOwnedRestore(const HidHideVisibilitySnapshot&,bool){++fake::w.crashReturnCalls;}
static bool inputHostRequestLocked(const char* command,const json&,const char*,DWORD,json* response){fake::w.pipeRequests.push_back({{"command",command},{"fakeOnly",true}});if(!fake::w.pipeOk)return false;json payload={{"ok",true},{"cloakOn",fake::w.cloak},{"hidden",json::array()},{"applications",json::array()}};for(const auto& p:fake::w.hidden)payload["hidden"].push_back(W2U(p));for(const auto& p:fake::w.apps)payload["applications"].push_back(W2U(p));if(fake::w.pipeMalformed)payload["hidden"]="bad-type";*response={{"detail",payload.dump()}};return true;}
static bool hidHideCaptureVisibilitySnapshotViaPipe(HidHideVisibilitySnapshot&);
static bool hidHideCaptureVisibilitySnapshotViaCli(HidHideVisibilitySnapshot&);
// Focused local-return gate only: the durable settlement boundary is an
// explicit external stub. No Session/storage/CM algorithm executes here.
static bool inputHostTargetReturnAfterPhysicalRestoreLocked(
 const HidHideVisibilitySnapshot&,bool,const std::vector<std::wstring>&){return true;}
#include "actual-helpers.inc"
#include "actual-cli.inc"
#include "actual-snapshot.inc"
#include "actual-carried.inc"
#include "actual-sweep.inc"
// Always generated verbatim from the requested production main snapshot.
#include "actual-restore.inc"
namespace suite {
json cases=json::array(),failures=json::array();unsigned assertions=0;std::string active;
void check(bool value,const char* text){++assertions;if(!value)failures.push_back({{"case",active},{"assertion",text}});}
json paths(const std::vector<std::wstring>& p){json j=json::array();for(const auto& s:p)j.push_back(W2U(s));return j;}
json journal(){const auto& j=g_hidHideVisibilityJournal;return {{"active",j.active},{"transactionId",j.transactionId},{"hiddenByUs",paths(j.hiddenByUs)},{"applicationAdded",j.applicationAdded},{"ownedApplications",paths(j.ownedApplications)},{"cloakChanged",j.cloakChanged}};}
void reset(){if(!fake::handles.empty())throw std::runtime_error("leaked fake Win32 handles");fake::w={};g_hidHideVisibilityJournal={};g_hidHideCarriedResponsibility={};g_msiSteamDeckReleaseFence.clear();g_msiSteamDeckReleaseDebt.clear();g_inputHostProcess=nullptr;g_inputHostTransportFault=false;g_inputHostPipeRetired=false;g_inputHostHello=false;g_inputHostPipeConnected=false;g_inputHostPrepared=g_inputHostPrepareMayHaveReachedHost=g_inputHostNeutralized=g_inputHostActive=g_inputHostReleaseQuarantined=false;g_inputHostRunId="run106";g_inputHostEpoch=1;g_inputHostStartupNonce="nonce106";g_xinputPhysicalSourceLocked=true;g_xinputConnectedMask=1;g_xinputPhysicalSourceMask=1;g_hidHideVisibilityJournal.active=true;g_hidHideVisibilityJournal.transactionId="owned106";g_hidHideVisibilityJournal.hiddenByUs={fake::owned};g_hidHideVisibilityJournal.physical.deviceInstanceId=W2U(fake::owned);fake::w.hidden={fake::owned,fake::foreign};}
void scenario(const char* name,const std::function<void()>& setup,bool expectRestored,bool expectJournal,const char* expectConfig,const char* expectAck,bool noMutations=false){active=name;const auto previous=assertions;const auto failed=failures.size();json result;try{reset();setup();const auto before=journal();bool restored=false;{std::lock_guard<InputHostMutex> owner(g_inputHostMx);restored=hidHideRestorePhysicalTransaction(name);}json row;for(const auto& item:fake::w.logs)if(item["name"]=="hidhide.p-hid-restore"&&!item["row"].contains("action"))row=item["row"];check(restored==expectRestored,"consumer return matches exact owned-configuration duty");check(g_hidHideVisibilityJournal.active==expectJournal,"journal retained until exact owned return is proven");check(row.is_object(),"actual restore emitted its own lifecycle receipt");check(row.value("configReadback",std::string("none"))==expectConfig,"actual configuration readback remains distinct from command result");check(row.value("commandResult",std::string("none"))==expectAck,"actual command acceptance classification retained");check(row.value("physicalReturnObserved",std::string("UNKNOWN"))=="UNKNOWN"&&row.value("externalAccess",std::string("unverified"))=="unverified","no game-consumer or external physical visibility proof is fabricated");check(fake::handles.empty(),"all fake process/pipe handles released");check(fake::w.invalidations==1,"actual consumer entry reached without restore seam");if(noMutations)check(fake::w.driverMutations.empty()&&fake::w.commands.empty(),"empty-owned no-source OFF issues no driver mutation or CLI command");if(fake::w.msi){check(std::none_of(fake::w.driverMutations.begin(),fake::w.driverMutations.end(),[](const json& m){return m.value("foreignPath",false)||m.value("command",std::string{})=="--cloak-off";}),"existing MSI path retains exact-owned scope and resident cloak");check(std::find(fake::w.hidden.begin(),fake::w.hidden.end(),fake::foreign)!=fake::w.hidden.end(),"MSI return does not consume foreign hidden path");}result={{"name",name},{"expectedRestored",expectRestored},{"actualRestored",restored},{"journalBefore",before},{"journalAfter",journal()},{"journalCleared",(before["active"]==true||row.value("unhideAttempted",false))&&!g_hidHideVisibilityJournal.active},{"carriedAfterActive",g_hidHideCarriedResponsibility.active},{"actualReceipt",row},{"driverMutationsReached",fake::w.driverMutations},{"commandsReached",fake::w.commands},{"fakePipeRequests",fake::w.pipeRequests},{"driverHiddenAfter",paths(fake::w.hidden)},{"driverApplicationsAfter",paths(fake::w.apps)},{"driverCloakAfter",fake::w.cloak},{"cycleLeafCalls",fake::w.cycleCalls},{"separateMsiCrashCallbackCalls",fake::w.crashReturnCalls},{"lifecycleLogs",fake::w.logs}};}catch(const std::exception& e){check(false,(std::string("fixture-exception:")+e.what()).c_str());result={{"name",name},{"exception",e.what()}};}result["assertions"]=assertions-previous;result["failures"]=failures.size()-failed;cases.push_back(std::move(result));}
}
int main(int argc,char** argv){if(argc!=2)return 2;using namespace suite;
 scenario("nonMSI-cli-false-owned-still-hidden",[]{},false,true,"still-hidden","cli-fallback-failed");
 scenario("nonMSI-readback-unknown-cli-rejected",[]{fake::w.queryOk=false;},false,true,"UNKNOWN","cli-fallback-failed");
 scenario("nonMSI-host-ack-unknown-malformed-pipe",[]{g_inputHostProcess=reinterpret_cast<HANDLE>(0x700000);g_inputHostHello=true;g_inputHostPipeConnected=true;fake::w.queryOk=false;fake::w.pipeOk=true;fake::w.pipeMalformed=true;},false,true,"UNKNOWN","UNKNOWN");
 scenario("empty-owned-no-source-normal-OFF",[]{g_hidHideVisibilityJournal={};fake::w.hidden.clear();fake::w.cloak=false;g_xinputPhysicalSourceLocked=false;g_xinputConnectedMask=0;g_xinputPhysicalSourceMask=0;},true,false,"none","none",true);
 scenario("MSI-cli-false-owned-still-hidden",[]{fake::w.msi=true;},false,true,"still-hidden","cli-fallback-failed");
 scenario("MSI-readback-unknown-cli-rejected",[]{fake::w.msi=true;fake::w.queryOk=false;},false,true,"UNKNOWN","cli-fallback-failed");
 scenario("MSI-exact-path-and-application-return",[]{fake::w.msi=true;fake::w.mutationAck=true;fake::w.apply=true;fake::w.apps={fake::app,fake::foreignApp};g_hidHideVisibilityJournal.applicationAdded=true;g_hidHideVisibilityJournal.ownedApplications={fake::app};g_hidHideVisibilityJournal.cloakChanged=true;},true,false,"not-hidden","cli-fallback-accepted");
 scenario("MSI-application-still-registered",[]{fake::w.msi=true;fake::w.mutationAck=true;fake::w.apply=true;fake::w.apps={fake::app,fake::foreignApp};fake::w.appAck=false;fake::w.appApply=false;g_hidHideVisibilityJournal.applicationAdded=true;g_hidHideVisibilityJournal.ownedApplications={fake::app};},false,true,"not-hidden","cli-fallback-failed");
 scenario("nonMSI-CLI-ACK-is-not-config-return",[]{fake::w.mutationAck=true;},false,true,"still-hidden","cli-fallback-accepted");
 scenario("nonMSI-negative-ACK-config-removal-observed",[]{fake::w.apply=true;},true,false,"not-hidden","cli-fallback-failed");
 scenario("nonMSI-carried-duty-reattached-not-erased",[]{g_hidHideVisibilityJournal={};g_hidHideCarriedResponsibility.active=true;g_hidHideCarriedResponsibility.transactionId="old106";g_hidHideCarriedResponsibility.ownedTargets={fake::owned};g_hidHideCarriedResponsibility.physical.deviceInstanceId=W2U(fake::owned);},false,true,"still-hidden","cli-fallback-failed");
 json report={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"failures",failures},{"cases",cases},{"actualConsumer","verbatim extracted hidHideRestorePhysicalTransaction plus real sweep, CLI and snapshot parsers"},{"realDeviceApisCalled",0},{"realProcessesSpawned",0},{"installedAppDataAccessed",false},{"realDriverCalls",0},{"fakeDriverMutationsAreObservationsOnly",true},{"physicalPersistenceAlgorithmExecuted",false},{"physicalSettlementBoundary","inputHostTargetReturnAfterPhysicalRestoreLocked: explicit external return-true stub; not durable settlement validation"}};std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<report.dump(2)<<"\n";std::cout<<json({{"pass",failures.empty()},{"cases",cases.size()},{"assertions",assertions},{"failures",failures}}).dump()<<"\n";return failures.empty()?0:1;}
