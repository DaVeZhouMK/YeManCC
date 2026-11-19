#if defined(YMCC_STEAM_MONITOR_LIVE_READ_ONLY)
#include "fixtures/steam_monitor_live_read_cases.inc"
#else
#if defined(YMCC_STEAM_MODULE_PROFILE_PREPARATION_ONLY)
#include "fixtures/steam_settings_module_profile_preparation/fixture.inc"
#else
#if defined(YMCC_STEAM_SETTINGS_OBSERVATION_ONLY)
#if defined(YMCC_STEAM_SHARED_PRESENCE_ONLY)
#error Observation and presence compile selections are mutually exclusive
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include "deps/json/json.hpp"
#include "native/steam_monitor_settings.h"
using json = nlohmann::json;

namespace observed_os {
inline const std::wstring steamKey=L"Software\\Valve\\Steam";
struct State {
    std::wstring root=L"Q:/MiXeD/Steam\\";
    DWORD account=42, registeredPid=900, producerPid=900;
    ULONGLONG birth=100;
    int presence=1;
    LSTATUS rootError=ERROR_SUCCESS,accountError=ERROR_SUCCESS,pidError=ERROR_SUCCESS;
    unsigned rootReads=0,accountReads=0,pidReads=0,violations=0;
};
inline State s;
inline LSTATUS WINAPI regGetValue(HKEY key,LPCWSTR subkey,LPCWSTR name,DWORD flags,
    LPDWORD type,PVOID output,LPDWORD size) {
    if(key!=HKEY_CURRENT_USER||!subkey||!name||!size){++s.violations;return ERROR_INVALID_PARAMETER;}
    const std::wstring sub=subkey,value=name;
    if(sub==steamKey&&value==L"SteamPath"&&flags==RRF_RT_REG_SZ){
        ++s.rootReads;if(s.rootError!=ERROR_SUCCESS)return s.rootError;
        const auto required=static_cast<DWORD>((s.root.size()+1)*sizeof(wchar_t));
        if(!output||*size<required){*size=required;return ERROR_MORE_DATA;}
        std::copy(s.root.c_str(),s.root.c_str()+s.root.size()+1,static_cast<wchar_t*>(output));
        *size=required;if(type)*type=REG_SZ;return ERROR_SUCCESS;
    }
    if(sub==steamKey+L"\\ActiveProcess"&&flags==RRF_RT_REG_DWORD&&(value==L"ActiveUser"||value==L"pid")){
        if(value==L"ActiveUser"){++s.accountReads;if(s.accountError!=ERROR_SUCCESS)return s.accountError;}
        else {++s.pidReads;if(s.pidError!=ERROR_SUCCESS)return s.pidError;}
        if(!output||*size<sizeof(DWORD))return ERROR_MORE_DATA;
        *static_cast<DWORD*>(output)=value==L"ActiveUser"?s.account:s.registeredPid;
        *size=sizeof(DWORD);if(type)*type=REG_DWORD;return ERROR_SUCCESS;
    }
    ++s.violations;return ERROR_INVALID_PARAMETER;
}
inline unsigned discoveryReads(){return s.rootReads+s.accountReads+s.pidReads;}
}
// Windows declarations precede the one OS substitution. The real Observer is
// never started. Its existing fixture adapter observes terminal session facts;
// GetCurrentProcess()/zero-timeout wait refer only to this fixture itself.
#define RegGetValueW observed_os::regGetValue
#include "native/steam_session_observer.fixture.h"
#undef RegGetValueW
static ymcc::steamsession::Observer g_steamSettingsObserver;

namespace memoryfs {
using file_time_type=std::int64_t;
struct File {std::string text;file_time_type modified=0;bool statError=false,readError=false;bool operator==(const File&)const=default;};
struct Stats {unsigned regular=0,directories=0,directoryChecks=0,steps=0,times=0,reads=0;};
inline std::map<std::wstring,File> files;
inline std::map<std::wstring,std::vector<std::pair<std::wstring,bool>>> directories;
inline bool unknownDirectory=false;
inline Stats stats;
inline std::wstring key(std::wstring value){
    std::wstring result;
    for(auto c:value){if(c==L'/')c=L'\\';c=static_cast<wchar_t>(std::towlower(c));
        if(c==L'\\'&&!result.empty()&&result.back()==L'\\')continue;result.push_back(c);}
    while(!result.empty()&&result.back()==L'\\')result.pop_back();return result;
}
class path {
    std::wstring text_;
public:
    path()=default;path(const std::wstring& value):text_(value){}
    std::wstring wstring()const{return text_;}
    path parent_path()const{const auto at=text_.find_last_of(L"\\/");return path(at==std::wstring::npos?L"":text_.substr(0,at));}
    path filename()const{const auto at=text_.find_last_of(L"\\/");return path(at==std::wstring::npos?text_:text_.substr(at+1));}
};
inline bool is_regular_file(const std::wstring& name,std::error_code& error){
    ++stats.regular;error.clear();const auto found=files.find(key(name));
    if(found==files.end())return false;
    if(found->second.statError){error=std::make_error_code(std::errc::permission_denied);return false;}
    return true;
}
inline file_time_type last_write_time(const std::wstring& name,std::error_code& error){
    ++stats.times;error.clear();const auto found=files.find(key(name));
    if(found==files.end()){error=std::make_error_code(std::errc::no_such_file_or_directory);return {};}
    return found->second.modified;
}
struct directory_entry {
    std::wstring name;bool directory=true;
    bool is_directory(std::error_code& error)const{++stats.directoryChecks;error.clear();return directory;}
    memoryfs::path path()const{return memoryfs::path(name);}
};
class directory_iterator {
    std::vector<directory_entry> entries_;std::size_t index_=0;
    bool ended()const{return index_>=entries_.size();}
public:
    directory_iterator()=default;
    directory_iterator(const std::wstring& directory,std::error_code& error){
        ++stats.directories;error.clear();
        if(unknownDirectory){error=std::make_error_code(std::errc::permission_denied);return;}
        const auto found=directories.find(key(directory));
        if(found==directories.end()){error=std::make_error_code(std::errc::no_such_file_or_directory);return;}
        for(const auto& entry:found->second)entries_.push_back({entry.first,entry.second});
    }
    bool operator!=(const directory_iterator& other)const{return ended()!=other.ended()||(!ended()&&index_!=other.index_);}
    const directory_entry* operator->()const{return &entries_.at(index_);}
    directory_iterator& increment(std::error_code& error){++stats.steps;error.clear();++index_;return *this;}
};
inline std::string read(const std::wstring& name){
    ++stats.reads;const auto found=files.find(key(name));
    if(found==files.end()||found->second.readError)throw std::runtime_error("memory-vdf-read-failure");
    return found->second.text;
}
}
namespace fspath=memoryfs;
static json prefs=json::object();
static struct {bool isolated=false;} g_aiFanMockSession;
static bool useFreshReadControl=false;
static std::wstring sofRegSZ(HKEY key,const wchar_t* subkey,const wchar_t* name){
    wchar_t data[32768]{};DWORD size=sizeof(data);
    return observed_os::regGetValue(key,subkey,name,RRF_RT_REG_SZ,nullptr,data,&size)==ERROR_SUCCESS?std::wstring(data):std::wstring{};
}
static bool sofRegDWORD(HKEY key,const wchar_t* subkey,const wchar_t* name,DWORD* value){
    DWORD size=sizeof(*value);*value=0;
    return observed_os::regGetValue(key,subkey,name,RRF_RT_REG_DWORD,nullptr,value,&size)==ERROR_SUCCESS;
}
static json ymSettingsRead(){return prefs;}
static std::string sgReadFile(const std::wstring& path){return memoryfs::read(path);}
#include "observed-settings-functions.inc"
static bool steamSettingsObservedLocalConfig(std::wstring* path,std::string* account,DWORD* registeredPid=nullptr){
    return useFreshReadControl?fresh_read_control::steamSettingsObservedLocalConfig(path,account,registeredPid):
        actual_observed::steamSettingsObservedLocalConfig(path,account,registeredPid);
}
#include "steam-monitor-state.inc"

namespace observed_settings_fixture {
namespace session=ymcc::steamsession;
static json cases=json::array();static json* currentCase=nullptr;
static void require(bool ok,const char* name){
    (*currentCase)["assertions"].push_back({{"name",name},{"passed",ok}});
    if(!ok)throw std::runtime_error(name);
}
static std::wstring configPath(const std::wstring& root,DWORD account){return root+L"\\userdata\\"+std::to_wstring(account)+L"\\config\\localconfig.vdf";}
static const std::string disk=R"VDF("UserLocalConfigStore" { "system" { "EnableGameOverlay" "1" "InGameOverlayShowFPSCorner" "1" "InGameOverlayShowFPSScaling" "0.8" } "other" { "keep" "KEEP" } })VDF";
static void reset(){
    g_steamSettingsObserver.stop(); // Never started: no event, worker or target handle exists.
    observed_os::s=observed_os::State{};memoryfs::files.clear();memoryfs::directories.clear();
    memoryfs::stats={};memoryfs::unknownDirectory=false;prefs=json::object();g_aiFanMockSession.isolated=false;useFreshReadControl=false;
    const auto root=observed_os::s.root,userdata=root+L"\\userdata";
    memoryfs::directories[memoryfs::key(userdata)]={{userdata+L"\\42",true},{userdata+L"\\43",true},{userdata+L"\\not-a-directory",false}};
    memoryfs::files[memoryfs::key(configPath(root,42))]={disk,10};
    memoryfs::files[memoryfs::key(configPath(root,43))]={ymcc::steamsettings::patch(disk,{{"position",2}}),20};
}
static std::shared_ptr<const session::IdentitySnapshot> publish(){
    session::IdentitySnapshot facts;std::wstring raw;LSTATUS rootError=ERROR_NOT_READY;
    const auto normalized=session::steamRoot(observed_os::steamKey,&rootError,&raw);
    facts.session={normalized,observed_os::s.producerPid,observed_os::s.account,observed_os::s.producerPid?observed_os::s.birth:0};
    facts.registryRoot=raw;facts.presence=observed_os::s.presence;facts.registeredPid=observed_os::s.registeredPid;
    facts.rootReadError=rootError;facts.registeredPidReadError=observed_os::s.pidError;facts.accountReadError=observed_os::s.accountError;
    facts.windowsSession=7;facts.windowsSessionKnown=true;facts.windowsSessionError=ERROR_SUCCESS;
    session::IdentityPublicationFixture::publish(g_steamSettingsObserver,facts);
    return g_steamSettingsObserver.identitySnapshot();
}
static void saveLive(){
    const json values={{"position",5},{"detail",2},{"scale",0.6},{"saturation",0.3},{"opacity",0.2}};
    prefs["steamMonitor"]={{"account","42"},{"path",W2U(configPath(observed_os::s.root,42))},
        {"pending",false},{"desired",{{"position",5}}},{"via","live"},{"reason",""},
        {"restartRequired",false},{"livePid",900},{"liveValues",values},{"futureNote","KEEP"}};
}
static json state(){
    const auto saved=prefs;const auto files=memoryfs::files;const auto directories=memoryfs::directories;
    const auto result=steamMonitorStateLocked();
    require(prefs==saved&&memoryfs::files==files&&memoryfs::directories==directories,"state-preserves-original-saved-json-and-memory-disk");
    require(!result.value("liveConfirmed",false)&&!result.value("applicationConfirmed",false),"display-never-grants-live-or-application-confirmation");
    return result;
}
static std::pair<std::wstring,std::string> selected(){
    std::wstring path;std::string account;
    require(actual_observed::steamSettingsObservedLocalConfig(&path,&account),"actual-memory-wrapper-selected-config");
    return {path,account};
}
static void run(const char* name,const std::function<void()>& body){
    json result={{"name",name},{"assertions",json::array()}};currentCase=&result;
    try{reset();body();}catch(const std::exception& error){result["error"]=error.what();}catch(...){result["error"]="unknown-exception";}
    result["assertions"].push_back({{"name","only-declared-registry-port-calls"},{"passed",observed_os::s.violations==0}});
    result["observed"]={{"rootReads",observed_os::s.rootReads},{"activeUserReads",observed_os::s.accountReads},
        {"pidReads",observed_os::s.pidReads},{"filesystem",{{"regularFileChecks",memoryfs::stats.regular},
        {"directoryEnumerations",memoryfs::stats.directories},{"directoryChecks",memoryfs::stats.directoryChecks},
        {"directorySteps",memoryfs::stats.steps},{"timeReads",memoryfs::stats.times},{"vdfReads",memoryfs::stats.reads}}}};
    result["passed"]=!result.contains("error")&&std::all_of(result["assertions"].begin(),result["assertions"].end(),[](const json& item){return item.at("passed").get<bool>();});
    cases.push_back(std::move(result));currentCase=nullptr;
}
static void repeatedDisplay(bool negative){
    publish();saveLive();useFreshReadControl=negative;
    const auto before=observed_os::discoveryReads();const auto original=prefs;const auto files=memoryfs::files;
    const auto rootBefore=observed_os::s.rootReads,accountBefore=observed_os::s.accountReads,pidBefore=observed_os::s.pidReads;
    const auto first=state();require(first.at("position")==5,"warm-display-keeps-saved-live-values");
    const unsigned repeats=negative?1:1001;
    for(unsigned i=0;i<repeats;++i){const auto again=steamMonitorStateLocked();
        require(again.at("position")==5&&!again.value("liveConfirmed",false),"repeated-real-state-keeps-display-only-values");}
    require(observed_os::discoveryReads()==before,"memory-settings-consumer-must-not-reread-registry");
    require(observed_os::s.rootReads==rootBefore,"repeated-state-SteamPath-discovery-zero-increment");
    require(observed_os::s.accountReads==accountBefore,"repeated-state-ActiveUser-discovery-zero-increment");
    require(observed_os::s.pidReads==pidBefore,"repeated-state-registeredPid-discovery-zero-increment");
    require(prefs==original&&memoryfs::files==files,"repeated-state-preserves-saved-json-and-memory-disk");
    require(memoryfs::stats.reads==repeats+1,"filesystem-reads-are-counted-separately-not-called-zero-io");
}
static void positives(){
    run("observed/root-normalized-and-raw-spelling-one-read",[]{
        const auto receipt=publish();require(receipt&&receipt->session.root==L"q:\\mixed\\steam","actual-steamRoot-normalization-unchanged");
        require(receipt->registryRoot==observed_os::s.root&&observed_os::s.rootReads==1,"raw-root-retained-from-one-SteamPath-read");
    });
    run("observed/root-failure-clears-raw",[]{
        observed_os::s.rootError=ERROR_ACCESS_DENIED;std::wstring raw=L"stale";LSTATUS error=ERROR_SUCCESS;
        require(session::steamRoot(observed_os::steamKey,&error,&raw).empty()&&raw.empty()&&error==ERROR_ACCESS_DENIED&&observed_os::s.rootReads==1,"failed-root-read-clears-raw-and-normalized-output");
    });
    run("observed/saved-path-and-live-values-exact",[]{
        publish();saveLive();const auto reads=observed_os::discoveryReads();const auto target=selected();const auto display=state();
        require(target.first==configPath(observed_os::s.root,42)&&target.second=="42","raw-root-keeps-exact-saved-path-spelling");
        require(display.at("position")==5&&display.at("scale")==0.6&&display.at("via")=="live","real-state-keeps-saved-live-values");
        require(observed_os::discoveryReads()==reads,"display-does-not-discover-registry-root-or-account");
    });
    run("observed/offline-retains-candidate-without-live-confirmation",[]{
        observed_os::s.presence=0;observed_os::s.producerPid=0;const auto receipt=publish();saveLive();
        require(receipt->registryAccountCandidateKnown()&&!receipt->currentRegistryAccountKnown(),"offline-candidate-is-not-login-or-current-account-proof");
        const auto display=state();require(display.at("position")==5&&selected().second=="42","offline-display-keeps-candidate-and-last-saved-values");
    });
    run("observed/zero-account-latest-fallback-not-login-proof",[]{
        observed_os::s.account=0;const auto receipt=publish();const auto target=selected();
        require(!receipt->registryAccountCandidateKnown()&&!receipt->currentRegistryAccountKnown(),"zero-account-is-unknown-not-login-proof");
        require(target.second=="43"&&state().at("position")==2,"zero-account-preserves-original-latest-file-fallback");
    });
    run("observed/account-read-error-latest-fallback",[]{
        observed_os::s.accountError=ERROR_ACCESS_DENIED;observed_os::s.pidError=ERROR_ACCESS_DENIED;const auto receipt=publish();
        DWORD captured=777;std::wstring path;std::string account;
        require(actual_observed::steamSettingsObservedLocalConfig(&path,&account,&captured)&&captured==0,"unknown-published-PID-clears-caller-output");
        require(!receipt->registryAccountCandidateKnown()&&selected().second=="43"&&state().at("position")==2,"unreadable-active-account-falls-back-without-signing-account");
    });
    run("observed/missing-active-file-latest-fallback",[]{
        memoryfs::files.erase(memoryfs::key(configPath(observed_os::s.root,42)));publish();
        require(selected().second=="43"&&state().at("position")==2,"missing-active-file-keeps-original-filesystem-selection");
    });
    run("observed/no-root-receipt-rejects-without-private-discovery",[]{
        observed_os::s.root.clear();publish();const auto reads=observed_os::discoveryReads();
        DWORD captured=777;std::wstring path;std::string account;
        require(!actual_observed::steamSettingsObservedLocalConfig(&path,&account,&captured)&&captured==0,"invalid-root-receipt-clears-caller-PID");
        require(state().at("reason")=="localconfig-not-found"&&observed_os::discoveryReads()==reads&&memoryfs::stats.reads==0,"missing-root-does-not-trigger-private-registry-or-file-read");
    });
    run("observed/no-receipt-rejects-without-private-discovery",[]{
        require(!g_steamSettingsObserver.identitySnapshot(),"real-unstarted-observer-has-no-receipt");
        DWORD captured=777;std::wstring path;std::string account;
        require(!actual_observed::steamSettingsObservedLocalConfig(&path,&account,&captured)&&captured==0,"missing-receipt-clears-caller-PID");
        require(state().at("reason")=="localconfig-not-found"&&observed_os::discoveryReads()==0&&memoryfs::stats.reads==0,"no-receipt-does-not-discover-registry-or-read-files");
    });
    run("observed/unknown-userdata-directory",[]{
        observed_os::s.account=0;memoryfs::unknownDirectory=true;publish();
        require(state().at("reason")=="localconfig-not-found"&&memoryfs::stats.directories==1&&memoryfs::stats.reads==0,"unknown-directory-is-not-an-available-config");
    });
    run("observed/empty-userdata-directory",[]{
        observed_os::s.account=0;memoryfs::directories[memoryfs::key(observed_os::s.root+L"\\userdata")].clear();publish();
        require(state().at("reason")=="localconfig-not-found"&&memoryfs::stats.reads==0,"empty-directory-does-not-fabricate-account-or-config");
    });
    run("observed/producer-replacement-consumes-new-facts",[]{
        publish();saveLive();prefs["steamMonitor"]["pending"]=true;prefs["steamMonitor"]["via"]="deferred";prefs["steamMonitor"]["restartRequired"]=true;
        DWORD captured=0;std::wstring path;std::string account;
        require(actual_observed::steamSettingsObservedLocalConfig(&path,&account,&captured)&&captured==900&&account=="42","captured-PID-belongs-to-first-publication");
        observed_os::s.account=43;observed_os::s.registeredPid=901;observed_os::s.producerPid=901;publish();
        require(actual_observed::steamSettingsObservedLocalConfig(&path,&account,&captured)&&captured==901&&account=="43","captured-PID-and-account-follow-new-publication-together");
        require(selected().second=="43"&&state().at("reason")=="steam-account-changed","replacement-publication-replaces-display-selection");
    });
    run("observed/old-receipt-remains-immutable",[]{
        const auto old=publish();const auto revision=old->revision;const auto root=old->registryRoot;
        observed_os::s.account=43;const auto next=publish();
        require(next->revision>revision&&old->revision==revision&&old->session.account==42&&old->registryRoot==root&&next->session.account==43,"retained-publication-does-not-mutate-on-replacement");
    });
    run("observed/case-only-root-publication-keeps-epochs",[]{
        const auto old=publish();observed_os::s.root=L"q:/mIxEd/sTEAM\\";const auto next=publish();
        require(next->registryRoot==observed_os::s.root&&next->registryRoot!=old->registryRoot&&next->revision>old->revision,"new-publication-updates-raw-spelling");
        require(next->processEpoch==old->processEpoch&&next->accountEpoch==old->accountEpoch&&g_steamSettingsObserver.sameIdentityEpoch(old),"case-only-raw-root-does-not-create-process-or-account-epoch");
        require(selected().first==configPath(observed_os::s.root,42),"new-selector-uses-new-raw-root-spelling");
    });
    run("observed/old-selector-and-selectorAt-equivalent",[]{
        for(unsigned variation=0;variation<4;++variation){
            observed_os::s.account=variation==1?0:42;observed_os::s.accountError=variation==2?ERROR_ACCESS_DENIED:ERROR_SUCCESS;
            if(variation==3)memoryfs::files.erase(memoryfs::key(configPath(observed_os::s.root,42)));
            std::wstring before,after;std::string beforeAccount,afterAccount;
            const bool old=before_selector::sofFindLocalConfig(&before,&beforeAccount);
            const DWORD candidate=observed_os::s.accountError==ERROR_SUCCESS?observed_os::s.account:0;
            const bool current=sofFindLocalConfigAt(observed_os::s.root,candidate,&after,&afterAccount);
            require(old==current&&before==after&&beforeAccount==afterAccount,"old-selector-and-At-match-on-identical-memory-filesystem-inputs");
        }
    });
    run("observed/repeated-display-zero-discovery-1001",[]{repeatedDisplay(false);});
    run("observed/vdf-read-error-preserves-original",[]{
        publish();saveLive();memoryfs::files.at(memoryfs::key(configPath(observed_os::s.root,42))).readError=true;
        require(state().at("reason")=="memory-vdf-read-failure","real-state-catches-memory-file-read-failure");
    });
    run("observed/invalid-saved-section-rejected-before-filesystem",[]{
        publish();prefs["steamMonitor"]="broken";
        require(state().at("reason")=="invalid-steam-monitor-state"&&memoryfs::stats.reads==0&&memoryfs::stats.regular==0,"actual-saved-validator-runs-before-display-target-lookup");
    });
}
static int emit(const std::string& selection,int rejection=0){
    const auto failures=std::count_if(cases.begin(),cases.end(),[](const json& item){return !item.at("passed").get<bool>();});
    json result={{"selection",selection},{"actualConsumer",true},{"actualOwner",true},{"realWindowsOrProcesses",false},
        {"selfProcessTerminalWaitOnly",true},{"observerStarted",false},{"realRegistryOrFilesystem",false},
        {"cases",cases},{"caseCount",cases.size()},{"failedCases",failures}};
    if(rejection)result["selectionError"]="exactly-one-observed-settings-selection-required";
    std::cout<<result.dump()<<'\n';return rejection?rejection:(failures?1:0);
}
}
int main(int argc,char** argv){
    using namespace observed_settings_fixture;
    if(argc!=2)return emit("rejected",2);const std::string selection=argv[1];
    if(selection=="--observed-settings-fresh-read-control"){
        run("observed/fresh-read-control-repeated-display",[]{repeatedDisplay(true);});return emit(selection);
    }
    if(selection!="--observed-settings-only")return emit("rejected",2);
    positives();return emit(selection);
}
#else
#if defined(YMCC_STEAM_SHARED_PRESENCE_ONLY)
// Explicit source-prepared selection of the existing Steam settings consumer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "deps/json/json.hpp"
#include "tools/fixtures/process_inventory_ports.h"
#include "native/monitor_identity.h"
#include "sof-steam-presence.inc"
#include <functional>

namespace shared_presence_fixture {
using json = nlohmann::json;
namespace inventory = ymcc::processInventory;
using Mode = osfacts::QueryMode;
static json cases = json::array();
static json* activeCase = nullptr;

static void require(bool condition, const char* name) {
    (*activeCase)["assertions"].push_back({{"name", name}, {"passed", condition}});
    if (!condition) throw std::runtime_error(name);
}
static osfacts::Row row(const wchar_t* name, DWORD pid = 100) {
    osfacts::Row value{pid}; value.name = name; return value;
}
static void reset(std::vector<osfacts::Row> rows, Mode mode = Mode::complete) {
    // Never erase evidence of an unreturned handle from a preceding case.
    require(osfacts::s.handles.empty(), "previous-case-returned-all-virtual-handles");
    osfacts::s = osfacts::State{};
    osfacts::s.rows = std::move(rows); osfacts::s.mode = mode;
    inventory::invalidate(); // The global real Store, not a fixture-local Store.
}
static std::shared_ptr<const inventory::Snapshot> warm(bool present) {
    const auto receipt = inventory::read(inventory::resolveQuery(), inventory::currentTime, 250);
    require(receipt && receipt->status == inventory::Status::Complete, "warm-shared-table-is-complete");
    require(std::any_of(receipt->rows.begin(), receipt->rows.end(), [](const auto& item) {
        return nativeIdentityNormalizeName(item.name) == L"steam";
    }) == present, "warm-shared-table-has-requested-presence");
    require(osfacts::s.queryCalls == 1 && osfacts::s.snapshots == 0,
        "warm-uses-one-native-query-and-no-private-snapshot");
    return receipt;
}
static std::shared_ptr<const inventory::Snapshot> observe(bool expected,
    inventory::Status expectedStatus, unsigned expectedSnapshots = 0, bool control = false) {
    const auto queries = osfacts::s.queryCalls, snapshots = osfacts::s.snapshots;
    const bool actual = control ? cached_control::sofSteamRunning() : actual_presence::sofSteamRunning();
    require(actual == expected, control ? "fresh-absence-must-not-hide-steam-start" : "real-sofSteamRunning-presence-result");
    require(osfacts::s.queryCalls == queries + 1, "fresh-zero-age-does-not-reuse-warm-table");
    require(osfacts::s.snapshots == snapshots + expectedSnapshots,
        expectedSnapshots ? "only-central-owner-toolhelp-fallback" : "native-support-means-zero-private-toolhelp-snapshots");
    const auto queriesAfter = osfacts::s.queryCalls, snapshotsAfter = osfacts::s.snapshots;
    const auto receipt = inventory::read(inventory::resolveQuery(), inventory::currentTime, 250);
    require(receipt && receipt->status == expectedStatus, "shared-owner-receipt-status");
    require(osfacts::s.queryCalls == queriesAfter && osfacts::s.snapshots == snapshotsAfter,
        "other-read250-consumer-reuses-helper-table-without-query");
    return receipt;
}
static void runCase(const std::string& name, const std::function<void()>& body) {
    json result = {{"name", name}, {"assertions", json::array()}};
    activeCase = &result;
    try { body(); }
    catch (const std::exception& error) { result["error"] = error.what(); }
    catch (...) { result["error"] = "unknown-exception"; }
    const auto resource = [&](const char* label, bool passed) {
        result["assertions"].push_back({{"name", label}, {"passed", passed}});
    };
    resource("all-virtual-handles-returned", osfacts::s.handles.empty());
    resource("zero-process-opens", osfacts::s.opens == 0);
    resource("zero-session-or-wait-authority-probes", osfacts::s.sessionCalls == 0 && osfacts::s.waits == 0);
    resource("zero-virtual-os-violations", osfacts::s.violations == 0);
    resource("all-successful-central-snapshots-closed", osfacts::s.closes ==
        (osfacts::s.snapshotFail ? 0u : osfacts::s.snapshots));
    result["observed"] = {{"queries", osfacts::s.queryCalls}, {"snapshots", osfacts::s.snapshots},
        {"opens", osfacts::s.opens}, {"closes", osfacts::s.closes},
        {"handlesRemaining", osfacts::s.handles.size()}, {"violations", osfacts::s.violations}};
    result["passed"] = !result.contains("error") && std::all_of(result["assertions"].begin(),
        result["assertions"].end(), [](const json& item) { return item.at("passed").get<bool>(); });
    cases.push_back(std::move(result)); activeCase = nullptr;
}
static void warmAbsentThenStart(bool control) {
    reset({row(L"Other.exe")});
    const auto old = warm(false);
    ++osfacts::s.tick; osfacts::s.rows = {row(L"Steam.Exe")};
    const auto fresh = observe(true, inventory::Status::Complete, 0, control);
    require(fresh->generation > old->generation && fresh->observedAt == osfacts::s.tick,
        "start-publishes-new-shared-generation-at-plus-one-ms");
    require(old->rows.size() == 1 && old->rows.front().name == L"Other.exe",
        "earlier-owning-receipt-remains-immutable");
}
static void positiveCases() {
    for (const auto& name : std::vector<std::wstring>{L"steam.exe", L"STEAM.EXE", L"StEaM.ExE",
        L"C:\\Program Files (x86)\\Steam\\Steam.Exe", L"Q:/Steam/STEAM.exe", L"steam"}) {
        const std::string label = "complete-steam-name-variant-" + std::to_string(cases.size() + 1);
        runCase(label, [&] { reset({row(L"Other.exe"), row(name.c_str(), 101)});
            const auto receipt = observe(true, inventory::Status::Complete);
            require(receipt->rows.size() == 2, "complete-native-table-retains-all-rows"); });
    }
    runCase("complete-other-process-is-absent", [] {
        reset({row(L"Other.exe")}); observe(false, inventory::Status::Complete);
    });
    runCase("webhelper-and-lookalikes-do-not-count-as-steam", [] {
        reset({row(L"steamwebhelper.exe"), row(L"C:\\Steam\\steamwebhelper.Exe", 101),
            row(L"steamservice.exe", 102), row(L"notsteam.exe", 103), row(L"steam.exe.bak", 104)});
        observe(false, inventory::Status::Complete);
    });
    runCase("native-success-zero-returned-bytes-is-rejected", [] {
        reset({});
        const auto receipt = observe(true, inventory::Status::Failed);
        require(receipt->basicStatus == inventory::Status::Failed && receipt->error == ERROR_INVALID_DATA &&
            receipt->rows.empty(), "zero-byte-native-success-is-failed-invalid-data-not-complete-empty");
    });
    const std::vector<std::pair<const char*, Mode>> failures = {
        {"malformed", Mode::malformed}, {"failed", Mode::failed}, {"allocation-failure", Mode::allocationFailure}};
    for (const auto& failure : failures) runCase(std::string("native-") + failure.first + "-is-conservative", [&] {
        reset({row(L"Other.exe")}, failure.second);
        const auto receipt = observe(true, inventory::Status::Failed);
        require(receipt->rows.empty(), "failed-native-table-does-not-prove-absence");
    });
    runCase("unsupported-central-toolhelp-complete-absent", [] {
        reset({row(L"Other.exe")}, Mode::unsupported);
        const auto receipt = observe(false, inventory::Status::Complete, 1);
        require(receipt->basicStatus == inventory::Status::Unavailable && receipt->rows.size() == 1,
            "complete-fallback-belongs-to-real-owner");
    });
    runCase("unsupported-central-toolhelp-complete-steam", [] {
        reset({row(L"Other.exe"), row(L"StEaM.Exe", 101)}, Mode::unsupported);
        observe(true, inventory::Status::Complete, 1);
    });
    runCase("unsupported-central-toolhelp-complete-empty-is-absent", [] {
        reset({}, Mode::unsupported);
        const auto receipt = observe(false, inventory::Status::Complete, 1);
        require(receipt->rows.empty() && receipt->error == ERROR_SUCCESS,
            "only-complete-empty-toolhelp-receipt-proves-absence");
    });
    for (const bool steamPrefix : {false, true}) runCase(steamPrefix ?
        "unsupported-partial-steam-prefix-remains-failed" : "unsupported-partial-other-prefix-cannot-prove-absence", [&] {
        reset({row(steamPrefix ? L"Steam.exe" : L"Other.exe"), row(L"Other2.exe", 101)}, Mode::unsupported);
        osfacts::s.partial = true;
        const auto receipt = observe(true, inventory::Status::Failed, 1);
        require(receipt->rows.empty() && receipt->rejectionHints.size() == 1 && receipt->error == ERROR_ACCESS_DENIED,
            "partial-prefix-is-rejection-only-not-complete-authority");
    });
    runCase("unsupported-central-first-failure-is-conservative", [] {
        reset({row(L"Other.exe")}, Mode::unsupported); osfacts::s.firstFail = true;
        const auto receipt = observe(true, inventory::Status::Failed, 1);
        require(receipt->error == ERROR_ACCESS_DENIED && receipt->rows.empty(), "first-failure-does-not-prove-absence");
    });
    runCase("unsupported-central-snapshot-failure-is-conservative", [] {
        reset({row(L"Other.exe")}, Mode::unsupported); osfacts::s.snapshotFail = true;
        const auto receipt = observe(true, inventory::Status::Failed, 1);
        require(receipt->error == ERROR_ACCESS_DENIED && receipt->rows.empty(), "snapshot-failure-does-not-prove-absence");
    });
    runCase("warm-absent-plus-one-ms-steam-start-is-fresh", [] { warmAbsentThenStart(false); });
    runCase("warm-present-plus-one-ms-steam-exit-is-fresh", [] {
        reset({row(L"Steam.exe")}); const auto old = warm(true);
        ++osfacts::s.tick; osfacts::s.rows = {row(L"Other.exe")};
        const auto fresh = observe(false, inventory::Status::Complete);
        require(fresh->generation > old->generation && old->rows.front().name == L"Steam.exe",
            "exit-replaces-cache-and-retains-earlier-owning-receipt");
    });
    for (const bool present : {false, true}) runCase(present ?
        "warm-present-plus-one-ms-zero-byte-success-is-rejected" :
        "warm-absent-plus-one-ms-zero-byte-success-is-rejected", [&] {
        reset({row(present ? L"Steam.exe" : L"Other.exe")}); const auto old = warm(present);
        ++osfacts::s.tick; osfacts::s.rows.clear();
        const auto fresh = observe(true, inventory::Status::Failed);
        require(fresh->generation > old->generation && fresh->error == ERROR_INVALID_DATA && fresh->rows.empty(),
            "zero-byte-native-success-cannot-reuse-warm-absence-or-become-empty-complete");
    });
    for (const bool present : {false, true}) {
        for (const auto& failure : failures) runCase(std::string(present ? "warm-present-" : "warm-absent-") +
            "plus-one-ms-" + failure.first, [&] {
            reset({row(present ? L"Steam.exe" : L"Other.exe")}); const auto old = warm(present);
            ++osfacts::s.tick; osfacts::s.mode = failure.second;
            const auto fresh = observe(true, inventory::Status::Failed);
            require(fresh->generation > old->generation && fresh->rows.empty() && old->status == inventory::Status::Complete,
                "fresh-failure-publishes-no-stale-success-or-absence");
        });
    }
}
static int emit(const char* selection, int rejection = 0) {
    const auto failed = std::count_if(cases.begin(), cases.end(), [](const json& item) {
        return !item.at("passed").get<bool>();
    });
    json output = {{"selection", selection}, {"actualConsumer", true}, {"actualOwner", true},
        {"realWindowsOrProcesses", false}, {"cases", cases}, {"caseCount", cases.size()}, {"failedCases", failed}};
    if (rejection) output["selectionError"] = "exactly-one-known-presence-selection-required";
    std::cout << output.dump() << '\n';
    return rejection ? rejection : (failed ? 1 : 0);
}
} // namespace shared_presence_fixture

int main(int argc, char** argv) {
    using namespace shared_presence_fixture;
    if (argc != 2) return emit("rejected", 2);
    const std::string selection = argv[1];
    if (selection == "--shared-presence-cached-control") {
        runCase("warm-absent-plus-one-ms-steam-start-is-fresh", [] { warmAbsentThenStart(true); });
        return emit(selection.c_str());
    }
    if (selection != "--shared-presence-only") return emit("rejected", 2);
    positiveCases(); return emit(selection.c_str());
}
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <memory>
#include <iostream>
#include "json.hpp"
#include "sleep_steam_preferences.h"
#include "steam_monitor_settings.h"
using json=nlohmann::json;
static int passed=0,writes=0,backups=0,mouseWrites=0,liveCalls=0,liveSetCalls=0,liveGetCalls=0,storeWrites=0;
static bool running=false,writeOk=true,copyOk=true,poolOk=true,storeOk=true;
static std::string account="42",liveMode="ok",liveSetMode="ok",disk;
static DWORD activePid=777;
static std::atomic<bool> g_exitRequested{false};
static std::string startupBoot="fixture-boot-1";
static const std::string& bootModeStartupIdentity(){return startupBoot;}
static json getPatch=json::object(),replyOverride=nullptr;
static bool overrideGet=false,overrideSet=false,throwGet=false,throwSet=false;
static std::vector<json> storePreimages,monitorEvents;
static std::wstring path=LR"(R:\memory-only\42\config\localconfig.vdf)";
static json prefs=json::object(),client=json::object();
static std::vector<std::function<void()>> jobs;
static std::vector<std::string> order;
static std::function<void()> liveHook,liveGetHook;
static std::function<void()> copyHook,writeHook;
static std::function<void(const char*,const json&)> storeHook;
static bool readFault=false;
static struct {bool isolated=false;} g_aiFanMockSession;
static std::mutex g_sofMx,g_sdmMx;
static std::atomic<bool> g_sofQueued{false},g_sofPending{false},g_sofEnabled{false},g_sdmEventPending{false},g_sdmRefreshRequested{false},g_sdmApplyRequested{false};
static std::atomic<int> g_sofDesired{-1};
static std::atomic<unsigned long long> g_sofRequestSerial{0};
static json g_sofReceipt={{"via","none"}};
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
static unsigned registryReads=0,privateConfigReads=0,sharedConfigReads=0,presenceReads=0;
static std::function<void()> beforeSectionPrepareHook;
static std::function<void()> writeGateHook,presenceHook,sharedConfigHook;
static unsigned writeGateRepeats=1;
static bool sofRegDWORD(HKEY,const wchar_t*,const wchar_t* name,DWORD* value){++registryReads;*value=std::wstring(name)==L"ActiveUser"?static_cast<DWORD>(std::stoul(account.empty()?"0":account)):activePid;return true;}
static bool sofFindLocalConfig(std::wstring* out,std::string* user){++privateConfigReads;if(out)*out=path;if(user)*user=account;return !account.empty();}
#if !defined(YMCC_STEAM_MONITOR_WRITE_SHARED_ONLY) && !defined(YMCC_STEAM_OVERLAY_STARTUP_SHARED_ONLY)
static bool steamSettingsObservedLocalConfig(std::wstring* out,std::string* user,DWORD* registeredPid=nullptr){if(registeredPid){*registeredPid=0;sofRegDWORD(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"pid",registeredPid);}return sofFindLocalConfig(out,user);}
#endif
// The preserved default suite already controls registry/file/process leaves.
// This memory port projects those same leaves; the directed case above uses
// the real Observer publication and tests actual epoch/birth invalidation.
#if defined(YMCC_STEAM_MONITOR_WRITE_SHARED_ONLY) || defined(YMCC_STEAM_OVERLAY_STARTUP_SHARED_ONLY)
#include "steam_session_observer.fixture.h"
static ymcc::steamsession::Observer g_steamSettingsObserver;
static ULONGLONG writeBirth=100;
static int writePresence=-2;
static DWORD writeRegistered=777,writePid=777;
static bool sharedConfigForeign=false;
static LSTATUS writeAccountError=ERROR_SUCCESS,writeRegisteredError=ERROR_SUCCESS,writeRootError=ERROR_SUCCESS;
static void publishWriteIdentity(){
    ymcc::steamsession::IdentitySnapshot f;f.session={L"R:\\memory-only",writePid,static_cast<DWORD>(std::stoul(account.empty()?"0":account)),writeBirth};
    f.registryRoot=L"R:\\memory-only";f.presence=writePresence==-2?(running?1:0):writePresence;f.registeredPid=writeRegistered;
    f.rootReadError=writeRootError;f.accountReadError=writeAccountError;f.registeredPidReadError=writeRegisteredError;
    f.windowsSessionKnown=true;f.windowsSession=7;f.windowsSessionError=ERROR_SUCCESS;
    ymcc::steamsession::IdentityPublicationFixture::publish(g_steamSettingsObserver,f);
}
static bool sofFindLocalConfigAt(const std::wstring&,DWORD candidate,std::wstring* out,std::string* user){
    ++sharedConfigReads;if(out)*out=sharedConfigForeign?LR"(R:/memory-only/43/config/localconfig.vdf)":path;if(user)*user=sharedConfigForeign?"43":account;
    if(sharedConfigHook){auto hook=std::move(sharedConfigHook);sharedConfigHook={};hook();}
    return !account.empty();
}
static bool steamSettingsObservedLocalConfig(std::wstring* pathOut, std::string* accountOut, DWORD* registeredPid = nullptr) {
    if (registeredPid) *registeredPid = 0;
    const auto identity = g_steamSettingsObserver.identitySnapshot();
    if (!identity || identity->rootReadError != ERROR_SUCCESS || identity->registryRoot.empty()) {
        g_steamSettingsObserver.requestIdentityRefresh();
        return false;
    }
    if (registeredPid && identity->registeredPidReadError == ERROR_SUCCESS) *registeredPid = identity->registeredPid;
    const auto candidate = identity->accountReadError == ERROR_SUCCESS ? identity->session.account : DWORD{0};
    return sofFindLocalConfigAt(identity->registryRoot, candidate, pathOut, accountOut);
}
#else
struct LegacyMonitorSession {std::wstring root;DWORD pid=0,account=0;ULONGLONG created=0;bool operator==(const LegacyMonitorSession&)const=default;};
struct LegacyMonitorFacts {LegacyMonitorSession session;std::wstring registryRoot;int presence=0;LSTATUS rootReadError=0,accountReadError=0,registeredPidReadError=0;DWORD registeredPid=0;unsigned long long processEpoch=1,accountEpoch=1;};
struct LegacyMonitorIdentityPort {
    std::shared_ptr<const LegacyMonitorFacts> identitySnapshot()const{
        auto value=std::make_shared<LegacyMonitorFacts>();value->registryRoot=path;value->presence=running?1:0;value->registeredPid=activePid;
        value->session={path,activePid,static_cast<DWORD>(std::stoul(account.empty()?"0":account)),1};return value;
    }
    bool sameIdentityEpoch(const std::shared_ptr<const LegacyMonitorFacts>& expected)const{
        const auto now=identitySnapshot();return expected&&now->session.root==expected->session.root&&now->session.pid==expected->session.pid&&
            now->session.account==expected->session.account&&now->session.created==expected->session.created&&now->presence==expected->presence;
    }
    void requestIdentityRefresh()const{}
};
static LegacyMonitorIdentityPort g_steamSettingsObserver;
static bool sofFindLocalConfigAt(const std::wstring&,DWORD,std::wstring* out,std::string* user){return sofFindLocalConfig(out,user);}
#endif
static bool sofSteamRunning(){++presenceReads;if(presenceHook){auto hook=std::move(presenceHook);presenceHook={};hook();}return running;}
static std::string sgReadFile(const std::wstring& file){if(file!=path)throw std::runtime_error("unexpected-file-read");if(readFault)throw std::runtime_error("controlled-temporary-read-failure");return disk;}
static BOOL memoryCopy(const wchar_t*,const wchar_t*,BOOL){++backups;if(copyHook){auto hook=std::move(copyHook);copyHook={};hook();}return copyOk;}
#define CopyFileW memoryCopy
static bool sgWriteFileAtomic(const std::wstring& file,const std::string& text){if(file!=path || running)throw std::runtime_error("unsafe-file-write");++writes;order.push_back("localconfig");if(writeOk)disk=text;if(writeHook){auto hook=std::move(writeHook);writeHook={};hook();}return writeOk;}
static json ymSettingsRead(){return prefs;}
static json ymSettingsSection(const char* name){const auto it=prefs.find(name);return it!=prefs.end()&&it->is_object()?*it:json::object();}
static json ymSettingsField(const char* name,const char* field){return ymSettingsSection(name).value(field,json());}
static bool ymSettingsWriteSection(const char* name,const json& value,const std::function<bool(json&,json&)>& prepare={}){json prepared=value;if(prepare&&beforeSectionPrepareHook){auto hook=std::move(beforeSectionPrepareHook);beforeSectionPrepareHook={};hook();}if(prepare&&!prepare(prepared,prefs))return false;++storeWrites;if(!storeOk)return false;storePreimages.push_back(prefs);prefs[name]=prepared;if(storeHook)storeHook(name,prepared);return true;}
static bool ymSettingsPatchSection(const char* name,const json& value){auto merged=ymSettingsSection(name);merged.update(value);return ymSettingsWriteSection(name,merged);}
static void ipc_emit(const char* name,const json& value){if(std::string(name)=="steam.settings.updated")monitorEvents.push_back(value);}
static void appendNativeLifecycleLog(const char*,const json&){}
static bool poolSubmit(std::function<void()> job){if(!poolOk)return false;jobs.push_back(std::move(job));return true;}
static json steamOverlayFixStateLocked(){return ymcc::sleepprefs::retiredOverlayReply();}
static json steamOverlayFixGet(){return steamOverlayFixStateLocked();}
static int sofCurrentValue(const std::string& text,bool*){const auto roots=ymcc::steamdeck::Vdf(text).parse();const auto& sys=ymcc::steamsettings::system(roots);auto value=ymcc::steamdeck::scalar(sys.children,"EnableGameOverlay");return value.empty()?1:std::stoi(value);}
static std::function<void()> beforeSetPureOwnerHook;static unsigned setControlledDiscovery=0;static bool setPureOwnerSupplied=false;
static json steamLiveRequest(DWORD user,json request,const std::function<bool()>& admit={},const std::function<bool()>& currentOwner={}){
    const bool isSet=request.value("operation",std::string{})=="settings.set";
    if(isSet){setPureOwnerSupplied=bool(currentOwner);if(beforeSetPureOwnerHook){auto hook=std::move(beforeSetPureOwnerHook);beforeSetPureOwnerHook={};hook();}}
    if(currentOwner&&!currentOwner())return {{"ok",false},{"reason","controlled-request-cancelled"},{"mutated",false}};
    if(isSet)++setControlledDiscovery; // Controlled discovery, after pure owner and before final action admission.
    if(writeGateHook){auto hook=std::move(writeGateHook);writeGateHook={};hook();}
    for(unsigned i=0;i<writeGateRepeats;++i)if(admit&&!admit())return {{"ok",false},{"reason","controlled-request-cancelled"},{"mutated",false}};
    ++liveCalls;request["operation"]=="settings.set"?++liveSetCalls:++liveGetCalls;
    if((request["operation"]=="settings.set"&&throwSet)||(request["operation"]=="settings.get"&&throwGet))throw json::type_error::create(302,"controlled malformed transport receipt",&request);
    if((request["operation"]=="settings.set"&&overrideSet)||(request["operation"]=="settings.get"&&overrideGet))return replyOverride;
    if(std::to_string(user)!=account)return {{"ok",false},{"reason","steam-account-changed"},{"mutated",false}};
    if(liveMode!="ok")return {{"ok",false},{"reason",liveMode},{"mutated",liveMode=="live-transport-uncertain"}};
    if(request["operation"]=="settings.set"){
        if(liveSetMode!="ok")return {{"ok",false},{"reason",liveSetMode},{"mutated",liveSetMode=="live-transport-uncertain"}};
        client.update(request["values"]);order.push_back("live-client");
        if(liveHook){auto hook=std::move(liveHook);liveHook={};hook();}
        return {{"ok",true},{"values",request["values"]}};
    }
    json values=json::object();for(const auto& key:request["keys"])values[key.get<std::string>()]=client[key.get<std::string>()];
    values.update(getPatch);
    if(liveGetHook){auto hook=std::move(liveGetHook);liveGetHook={};hook();}
    return {{"ok",true},{"values",values}};
}
static bool steamLiveCanDefer(const json& result){return !result.value("mutated",false)&&(result.value("reason",std::string())=="live-debug-unavailable"||result.value("reason",std::string())=="steam-session-starting");}
static void sdmApplyLocked(){if(g_sdmEventPending.exchange(false)){++mouseWrites;order.push_back("mouse-layout");}}
static json sdmStateLocked(bool=false){return {{"ok",true},{"pending",g_sdmEventPending.load()}};}
static json steamDeckMouseGet(){return sdmStateLocked();}
static void steamSettingsQueue(const char*,bool=false);
// Default settings tests never request module preparation; fail if the leaf is reached.
static const char* nativeSteamPrepareSessionModuleProfile() noexcept { std::terminate(); }
static json steamDeckMouseSet(const json& args){if(!args.contains("percent")||!args["percent"].is_number_integer())return {{"ok",false}};g_sdmEventPending=true;steamSettingsQueue("mouse-user");return sdmStateLocked();}
//__PRODUCTION_OVERLAY_KICK__
#include "steam_settings_runtime.h"
#include "steam_overlay_startup.h"
static void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);++passed;std::cout<<"PASS "<<text<<'\n';}
static void flush(){while(!jobs.empty()){auto next=std::move(jobs);jobs.clear();for(auto& job:next)job();}}
static void reset(){
    prefs=json::object();jobs.clear();order.clear();liveHook={};liveGetHook={};copyHook={};writeHook={};storeHook={};readFault=false;getPatch=json::object();storePreimages.clear();monitorEvents.clear();overrideGet=overrideSet=throwGet=throwSet=false;replyOverride=nullptr;activePid=777;running=false;writeOk=copyOk=poolOk=storeOk=true;
    account="42";path=LR"(R:\memory-only\42\config\localconfig.vdf)";liveMode=liveSetMode="ok";
    disk=R"VDF("UserLocalConfigStore" {
        "Software" { "system" { "EnableGameOverlay" "SHADOW" } }
        // unrelated monitor-specific preferences stay byte-identical
        "system" { "EnableGameOverlay" "1" "InGameOverlayShowFPSCorner" "1" "InGameOverlayShowFPSScaling" "0.800000011920928955" "keep" "KEEP" }
        "other" { "future" "sentinel" }
    })VDF";
    writes=backups=mouseWrites=liveCalls=liveSetCalls=liveGetCalls=storeWrites=0;g_aiFanMockSession.isolated=false;
    g_sofPending=false;g_sofEnabled=false;g_sofQueued=false;g_sofDesired=-1;g_sofRequestSerial=0;
    g_steamSettingsQueued=false;g_steamSettingsSerial=0;g_steamClientPending=false;g_sdmEventPending=false;g_sdmRefreshRequested=false;g_sdmApplyRequested=false;
    g_steamOverlayAccount.clear();g_steamOverlayPath.clear();g_steamOverlayPinnedSerial=0;
    client=ymcc::steamsettings::clientPatch(ymcc::steamsettings::read(disk),1);
    g_exitRequested=false;startupBoot="fixture-boot-1";
    g_steamOverlayStartupPending=false;g_steamSettingsStartupConsumer=nullptr;
}
static int startupOverlayCases(){
    const auto enable=[] {prefs["startupDesired"]={{"steamOverlayOnBoot",true},{"fanControl",true},{"unrelated","KEEP"}};disk=ymcc::steamsettings::patch(disk,json::object(),0);};
    reset();disk=ymcc::steamsettings::patch(disk,json::object(),0);steamOverlayStartupAttempt(false);check(writes==1&&liveCalls==0&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==true,"absent startup configuration defaults ON and enables Steam once");
    reset();prefs["startupDesired"]={{"fanControl",true}};disk=ymcc::steamsettings::patch(disk,json::object(),0);steamOverlayStartupAttempt(false);check(writes==1&&prefs["startupDesired"]["fanControl"]==true,"missing overlay field in existing startup settings defaults ON without changing fan preference");
    reset();prefs["startupDesired"]="broken";steamOverlayStartupAttempt(false);check(writes==0&&storeWrites==0,"malformed startup section is not treated as missing configuration");
    for(const auto& value:std::vector<json>{false,"true",1,nullptr}){reset();prefs["startupDesired"]["steamOverlayOnBoot"]=value;steamOverlayStartupAttempt(false);check(writes==0&&liveCalls==0&&storeWrites==0,"false or malformed startup preference never authorizes writes");}
    reset();enable();startupBoot.clear();steamOverlayStartupAttempt(false);check(writes==0&&storeWrites==0,"unknown boot identity never fabricates once-per-boot authority");
    reset();enable();const auto before=disk;steamOverlayStartupAttempt(true);check(writes==1&&backups==1&&ymcc::steamsettings::overlayValue(disk)==1&&liveCalls==0,"pre-Steam startup enables only the scoped overlay configuration with backup");
    check(prefs["startupDesired"]["fanControl"]==true&&prefs["startupDesired"]["unrelated"]=="KEEP"&&disk.find("SHADOW")!=std::string::npos&&ymcc::steamsettings::read(disk)==ymcc::steamsettings::read(before),"startup preserves fan preference, unrelated VDF and performance monitor values");
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==true&&prefs["startupDesired"]["steamOverlayAppliedBoot"]==startupBoot,"startup result records verified file value and durable boot claim");
    disk=ymcc::steamsettings::patch(disk,json::object(),0);steamOverlayStartupAttempt(false);check(writes==1&&liveCalls==0&&ymcc::steamsettings::overlayValue(disk)==0,"reopen, wake or lost-reply same-boot calls cannot replay the setting");
    startupBoot="fixture-boot-2";steamOverlayStartupAttempt(true);check(writes==2&&ymcc::steamsettings::overlayValue(disk)==1,"a new Windows boot permits exactly one fresh application");
    reset();enable();storeOk=false;steamOverlayStartupAttempt(false);check(writes==0&&liveCalls==0,"failed durable boot claim prevents Steam mutation");
    reset();enable();copyOk=false;steamOverlayStartupAttempt(false);check(writes==0&&ymcc::steamsettings::overlayValue(disk)==0&&prefs["startupDesired"]["steamOverlayStartupResult"]["reason"]=="backup-failed","backup failure preserves the disabled overlay and reports failure");
    reset();enable();writeOk=false;steamOverlayStartupAttempt(false);check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false&&ymcc::steamsettings::overlayValue(disk)==0,"failed native file write is never confirmed");
    reset();enable();running=true;steamOverlayStartupAttempt(true);check(liveCalls==0&&storeWrites==0,"pre-launch callback never mutates an already running Steam or consumes its boot claim");
    steamOverlayStartupAttempt(false);check(liveSetCalls==1&&writes==0&&backups==0&&client["enable_overlay"]==true&&prefs["startupDesired"]["steamOverlayStartupResult"]["via"]=="live","an earlier Steam uses its own validated setting interface without file writes");
    steamOverlayStartupAttempt(false);check(liveSetCalls==1&&jobs.empty(),"successful live startup never queues a second attempt or polling");
    reset();enable();running=true;liveMode="live-debug-unavailable";steamOverlayStartupAttempt(false);steamOverlayStartupAttempt(false);check(liveSetCalls==1&&writes==0&&jobs.empty()&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false,"unavailable live interface keeps explicit failure without fallback file writes or retries");
    reset();enable();running=true;liveHook=[] {account="43";};steamOverlayStartupAttempt(false);check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false&&writes==0,"account changes after live dispatch cannot produce a successful startup receipt");
    reset();enable();g_exitRequested=true;steamOverlayStartupInitialize();steamOverlayStartupAttempt(false);check(jobs.empty()&&storeWrites==0&&writes==0,"exit blocks all startup work");
    reset();enable();g_aiFanMockSession.isolated=true;steamOverlayStartupInitialize();steamOverlayStartupAttempt(false);check(jobs.empty()&&storeWrites==0&&writes==0,"isolated sessions cannot apply real startup settings");
    reset();enable();running=true;steamOverlayStartupInitialize();check(jobs.size()==1&&liveCalls==0,"running Steam startup is submitted to the existing worker pool");flush();check(liveSetCalls==1&&jobs.empty(),"startup worker finishes with no resident timer or deferred demand");
    reset();enable();running=true;getPatch={{"enable_overlay",false}};steamOverlayStartupAttempt(false);
    check(liveSetCalls==1&&liveGetCalls==1&&writes==0&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false,"SET success cannot hide independent live readback still OFF");
    for(const auto& bad:std::vector<json>{"true",1,nullptr}){
        reset();enable();running=true;getPatch={{"enable_overlay",bad}};steamOverlayStartupAttempt(false);
        check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false&&writes==0,"malformed independent overlay readback is never accepted");
    }
    reset();enable();running=true;getPatch={{"unexpected",true}};steamOverlayStartupAttempt(false);
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false,"extra live readback keys cannot sign startup success");
    reset();enable();running=true;throwGet=true;steamOverlayStartupAttempt(false);running=false;steamOverlayStartupBeforeSteam();
    check(writes==0&&liveSetCalls==1&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false,"lost readback after SET does not authorize a second file mutation");
    reset();enable();running=true;liveGetHook=[] {account="43";};steamOverlayStartupAttempt(false);
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false,"account change during independent readback revokes success");
    reset();enable();running=true;liveGetHook=[] {activePid=888;};steamOverlayStartupAttempt(false);
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false,"Steam PID change during independent readback revokes success");
    reset();enable();running=true;steamOverlayStartupAttempt(false);
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["liveConfirmed"]==true&&prefs["startupDesired"]["steamOverlayStartupResult"]["persisted"]==false&&prefs["startupDesired"]["steamOverlayStartupResult"]["gameOverlayVerified"]==false,"live setting confirmation is distinct from disk persistence and game injection");
    reset();enable();disk=ymcc::steamsettings::patch(disk,json::object(),1);running=true;steamOverlayStartupAttempt(false);
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["persisted"]==true&&writes==0,"already persisted live value is confirmed without a native file write");
    reset();enable();running=true;liveMode="live-debug-unavailable";steamOverlayStartupAttempt(false);steamOverlayStartupAttempt(false);
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["safeFileRetry"]==true&&liveSetCalls==1&&jobs.empty(),"explicit unmutated unavailable Live result preserves only a pre-launch opportunity");
    steamOverlayStartupBeforeSteam();check(writes==0&&backups==0,"safe fallback remains blocked until Steam actually exits");
    running=false;steamOverlayStartupBeforeSteam();
    check(writes==1&&backups==1&&prefs["startupDesired"]["steamOverlayStartupResult"]["fileFallback"]==true&&prefs["startupDesired"]["steamOverlayStartupResult"]["persisted"]==true,"closed Steam gets one backed-up fallback after an unmutated unavailable Live attempt");
    disk=ymcc::steamsettings::patch(disk,json::object(),0);steamOverlayStartupBeforeSteam();
    check(writes==1&&ymcc::steamsettings::overlayValue(disk)==0,"consumed fallback cannot override a later user change in the same boot");
    for(const auto& value:std::vector<json>{true,nullptr,"false"}){
        reset();enable();running=true;overrideSet=true;replyOverride={{"ok",false},{"reason","live-debug-unavailable"},{"mutated",value}};steamOverlayStartupAttempt(false);running=false;steamOverlayStartupBeforeSteam();
        check(writes==0,"mutated, unknown or malformed mutation status never authorizes file fallback");
    }
    reset();enable();running=true;overrideSet=true;replyOverride={{"ok",false},{"reason","live-debug-unavailable"}};steamOverlayStartupAttempt(false);running=false;steamOverlayStartupBeforeSteam();
    check(writes==0,"omitted mutation status never authorizes file fallback");
    for(const auto* why:{"steam-account-changed","steam-session-changed","live-transport-uncertain","unknown-failure"}){
        reset();enable();running=true;liveMode=why;steamOverlayStartupAttempt(false);running=false;steamOverlayStartupBeforeSteam();
        check(writes==0,"identity or unknown transport failures never authorize file fallback");
    }
    reset();enable();running=true;liveMode="live-debug-unavailable";steamOverlayStartupAttempt(false);running=false;account="43";steamOverlayStartupBeforeSteam();
    check(writes==0&&prefs["startupDesired"]["steamOverlayStartupResult"]["reason"]=="steam-account-changed","safe fallback cannot migrate to another Steam account");
    reset();enable();running=true;liveMode="live-debug-unavailable";steamOverlayStartupAttempt(false);running=false;path=LR"(R:\different\42\config\localconfig.vdf)";steamOverlayStartupBeforeSteam();
    check(writes==0,"safe fallback cannot migrate to another Steam installation");
    reset();enable();running=true;liveMode="live-debug-unavailable";steamOverlayStartupAttempt(false);running=false;prefs["startupDesired"]["steamOverlayOnBoot"]=false;steamOverlayStartupBeforeSteam();
    check(writes==0,"explicit OFF cancels the remaining safe fallback");
    reset();enable();running=true;liveMode="live-debug-unavailable";steamOverlayStartupAttempt(false);running=false;copyOk=false;steamOverlayStartupBeforeSteam();copyOk=true;steamOverlayStartupBeforeSteam();
    check(writes==0&&backups==1,"failed fallback backup is terminal and never loops");
    reset();enable();copyHook=[] {running=true;};steamOverlayStartupAttempt(true);
    check(writes==0&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false,"Steam appearing after backup blocks file commit");
    reset();enable();copyHook=[] {disk+="\n// concurrent owner";};steamOverlayStartupAttempt(true);
    check(writes==0&&disk.find("concurrent owner")!=std::string::npos,"external VDF change after backup is preserved instead of overwritten");
    reset();enable();copyHook=[] {prefs["startupDesired"]["steamOverlayOnBoot"]=false;};steamOverlayStartupAttempt(true);
    check(writes==0,"OFF during backup revokes the pending file commit");
    reset();enable();writeHook=[] {running=true;};steamOverlayStartupAttempt(true);
    check(writes==1&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false&&prefs["startupDesired"]["steamOverlayStartupResult"]["reason"]=="startup-context-changed","Steam appearing at commit cannot receive a false offline success");
    reset();enable();writeHook=[] {disk+="\n// later writer";};steamOverlayStartupAttempt(true);
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false,"whole VDF readback must match the committed output, not only the overlay key");
    reset();enable();writeHook=[] {disk=ymcc::steamsettings::patch(disk,json::object(),0);};steamOverlayStartupAttempt(true);
    check(prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false&&writes==1,"Steam-style exit overwrite back to OFF is reported without replay or rollback");
    reset();enable();disk=ymcc::steamsettings::patch(disk,json::object(),1);steamOverlayStartupAttempt(true);
    check(writes==0&&backups==0&&prefs["startupDesired"]["steamOverlayStartupResult"]["persisted"]==true,"already enabled offline configuration needs no backup or rewrite");
    std::cout<<"STARTUP_OVERLAY_PASSED "<<passed<<'\n';return 0;
}
#ifdef YMCC_STEAM_MONITOR_WRITE_SHARED_ONLY
#include "fixtures/steam_monitor_write_shared_cases.inc"
#endif
#ifdef YMCC_STEAM_OVERLAY_STARTUP_SHARED_ONLY
#include "fixtures/steam_overlay_startup_identity_cases.inc"
#endif
int main(int argc,char** argv){try{
#ifdef YMCC_STEAM_OVERLAY_STARTUP_SHARED_ONLY
    return startupOverlaySharedFixture(argc,argv);
#endif
#ifdef YMCC_STEAM_MONITOR_WRITE_SHARED_ONLY
    return monitorWriteSharedFixture(argc,argv);
#endif
    if(argc==2&&std::string(argv[1])=="--startup-overlay-only")return startupOverlayCases();
    const bool sectionOnly=argc==2&&std::string(argv[1])=="--section-only";
    if(!sectionOnly){
    reset();const auto original=disk;
    auto read=steamSettingsGet({{"scope","monitor"}});check(read["available"]==true&&read["scale"].get<double>()>.8,"get uses Steam value without rounding or writing");
    check(writes==0&&jobs.empty()&&prefs.empty(),"opening monitor page never queues or persists a change");
    for(const auto& invalid:std::vector<json>{json::object(),{{"position",7}},{{"detail",0}},{{"scale",1.5}},{{"opacity",-1}},{{"saturation",.35}},{{"scale","1.0"}},{{"injectedPath","anything"}}})check(steamMonitorSet(invalid)["ok"]==false,"invalid/non-Steam parameters rejected before IO");
    check(writes==0&&prefs.empty(),"rejected values do not persist partial intent");
    auto transformed=ymcc::steamsettings::patch(original,{{"position",5},{"detail",4},{"opacity",.7}},0);
    check(sofCurrentValue(transformed,nullptr)==0&&transformed.find("\"EnableGameOverlay\" \"SHADOW\"")!=std::string::npos,"scoped transform changes only root system, not shadow key");
    check(transformed.find("\"keep\" \"KEEP\"")!=std::string::npos&&transformed.find("\"future\" \"sentinel\"")!=std::string::npos,"unknown fields/comments and unrelated blocks preserved");
    check(ymcc::steamsettings::overlayValue(transformed)==0,"overlay readback is scoped to root system despite shadow key");
    for(const auto& invalid:std::vector<std::string>{R"VDF("UserLocalConfigStore" {"system" {"InGameOverlayShowFPSCorner" "1" "InGameOverlayShowFPSCorner" "2"}})VDF",R"VDF("UserLocalConfigStore" {"system" {"InGameOverlayShowFPSCorner" "BAD"}})VDF",R"VDF("UserLocalConfigStore" {"system" {"InGameOverlayShowFPSScaling" "100"}})VDF",R"VDF("wrongRoot" {"system" {}})VDF"}){
        bool rejected=false;try{(void)ymcc::steamsettings::read(invalid);}catch(...){rejected=true;}check(rejected,"malformed/ambiguous/out-of-range VDF never authorizes unsafe edits");
    }
    for(const int position:{0,1,5,2,3,6,4})check(ymcc::steamsettings::read(ymcc::steamsettings::patch(original,{{"position",position}}))["position"]==position,"all seven native Steam position enums round-trip");
    for(const int detail:{1,2,3,4})check(ymcc::steamsettings::read(ymcc::steamsettings::patch(original,{{"detail",detail}}))["detail"]==detail,"all four native Steam detail enums round-trip");
    steamMonitorSet({{"position",5}});steamMonitorSet({{"scale",1.4},{"opacity",.7}});g_sofEnabled=true;steamOverlayFixKick("setting");steamSettingsSet({{"mousePercent",170}});
    check(jobs.size()==1,"monitor/overlay/mouse and rapid slider changes admit just one common worker");flush();
    check(writes==1&&backups==1&&mouseWrites==1,"one combined localconfig backup/write plus separate native mouse layout work");
    check(order==std::vector<std::string>{"localconfig","mouse-layout"},"all Steam writes serialized on shared lane");
    auto values=ymcc::steamsettings::read(disk);check(values["position"]==5&&values["opacity"]==.7&&values["scale"]==1.4&&sofCurrentValue(disk,nullptr)==1,"monitor batch preserves every field without abandoned overlay change");
    check(!prefs["steamMonitor"]["pending"].get<bool>()&&!g_sofPending,"confirmed monitor readback clears intent; retired overlay stays off");
    reset();running=true;steamMonitorSet({{"position",6},{"detail",4}});g_sofEnabled=true;steamOverlayFixKick("setting");flush();
    check(writes==0&&backups==0&&liveSetCalls==1&&liveGetCalls==1,"live monitor+overlay share one Steam-context batch, no VDF writes");
    check(client["overlay_fps_counter_corner"]==6&&client["enable_overlay"]==true,"live monitor changes preserve Steam overlay after retirement");
    check(steamMonitorGet()["position"]==6,"confirmed live receipt/get does not snap back to stale file content");
    reset();running=true;liveMode="live-debug-unavailable";steamMonitorSet({{"position",2},{"scale",.2}});g_sofEnabled=true;steamOverlayFixKick("setting");flush();
    check(writes==0&&!g_sofPending&&g_steamClientPending&&steamMonitorGet()["restartRequired"]==true,"unsafe live monitor editing defers without reviving obsolete overlay");
    running=false;steamSettingsQueue("steam-session");flush();
    check(writes==1&&sofCurrentValue(disk,nullptr)==1&&ymcc::steamsettings::read(disk)["scale"]==.2,"Steam stop commits deferred monitor once and preserves overlay");
    reset();running=true;liveMode="live-debug-unavailable";steamMonitorSet({{"position",3}});flush();account="43";path=LR"(R:\memory-only\43\config\localconfig.vdf)";running=false;steamSettingsQueue("steam-session");flush();
    check(writes==0&&prefs["steamMonitor"]["reason"]=="steam-account-changed","account switch never applies previous user's saved monitor intent");
    reset();running=true;liveMode="live-transport-uncertain";steamMonitorSet({{"opacity",0}});g_sofEnabled=true;steamOverlayFixKick("setting");flush();
    check(writes==0&&!g_sofPending&&!g_steamClientPending&&!prefs["steamMonitor"]["pending"].get<bool>(),"possibly mutated live call is not automatically replayed or file-overwritten");
    running=false;steamSettingsQueue("steam-session");flush();check(writes==0,"uncertain result stays non-replayable after restart");
    reset();copyOk=false;steamMonitorSet({{"position",5}});flush();check(writes==0&&prefs["steamMonitor"]["reason"]=="backup-failed","backup failure prevents original-file overwrite");
    reset();writeOk=false;steamMonitorSet({{"position",5}});flush();check(disk==original&&prefs["steamMonitor"]["reason"]=="write-failed","atomic-write failure reports failure and preserves original");
    reset();g_aiFanMockSession.isolated=true;check(steamMonitorSet({{"position",5}})["ok"]==false&&jobs.empty()&&writes==0,"isolated sessions reject all ambient Steam modifications");
    reset();poolOk=false;steamMonitorSet({{"position",5}});check(prefs["steamMonitor"]["pending"]==true&&jobs.empty(),"full queue retains explicit pending intent instead of claiming applied");poolOk=true;steamSettingsQueue("manual-retry");flush();check(writes==1,"retained request can be retried without duplicate or lost fields");
    reset();running=true;g_sofEnabled=true;liveHook=[] {g_sofEnabled=false;steamOverlayFixKick("during-live");};steamOverlayFixKick("initial");flush();
    check(client["enable_overlay"]==true&&!g_sofPending,"deprecated overlay kick cannot queue work or change Steam overlay");
    reset();auto overlayAck=steamSettingsSet({{"overlayOffFix",true}});
    check(overlayAck["accepted"]==false&&overlayAck["deprecated"]==true&&prefs.empty()&&writes==0&&jobs.empty(),"retired unified overlay setter rejects true without persisting or queuing");
    check(steamSettingsSet({{"overlayOffFix",false}})["deprecated"]==true&&storeWrites==0,"retired setter cannot restore overlay on a false request either");
    flush();check(sofCurrentValue(disk,nullptr)==1&&liveCalls==0,"obsolete overlay IPC causes no file/CDP mutation");
    reset();g_sofPending=true;g_sofDesired=0;g_sofEnabled=true;steamMonitorSet({{"position",2}});flush();
    check(!g_sofPending&&!g_sofEnabled&&sofCurrentValue(disk,nullptr)==1,"stale retired background intent is fenced from ordinary monitor work");
    reset();running=true;client["enable_overlay"]=false;
    auto retiredOff=steamSettingsSet({{"overlayOffFix",false}});steamOverlayFixKick("old-restore-request");flush();
    check(retiredOff["deprecated"]==true&&client["enable_overlay"]==false&&liveCalls==0&&writes==0&&jobs.empty(),"retirement never restores a previously disabled Steam overlay");
    check(steamSettingsSet({{"monitor",{{"position",1}}},{"mousePercent",100}})["ok"]==false,"ambiguous multi-operation IPC rejected before any mutation");

    reset();running=true;
    prefs["steamMonitor"]={{"account",account},{"path",W2U(path)},{"pending",true},{"desired",{{"position",0}}},{"reason","steam-session-starting"},{"via","deferred"},{"restartRequired",true}};
    auto pendingBefore=prefs;auto fresh=steamMonitorGet();
    check(fresh["ok"]==true&&fresh["liveConfirmed"]==true&&fresh["via"]=="live"&&fresh["position"]==1&&fresh["pending"]==true,"stale pending hidden intent does not replace live visible position");
    check(prefs==pendingBefore&&storeWrites==0&&liveSetCalls==0&&liveGetCalls==1&&writes==0&&jobs.empty(),"GET is read-only: never replays, clears or persists old pending intent");
    auto awaited=steamSettingsSet({{"monitor",{{"position",0}}},{"awaitApplied",true}});
    check(awaited["ok"]==true&&awaited["applicationConfirmed"]==true&&awaited["liveConfirmed"]==true&&awaited["position"]==0&&!awaited["pending"].get<bool>(),"awaited switch returns application ACK only after durable receipt AND fresh live readback");
    check(liveSetCalls==1&&liveGetCalls==2&&jobs.empty()&&writes==0&&backups==0,"awaited live switch uses exactly one SET and no Steam VDF edit / new worker");
    reset();running=true;auto queueAck=steamSettingsSet({{"monitor",{{"position",0}}}});
    check(queueAck["pending"]==true&&!queueAck.value("applicationConfirmed",false)&&queueAck["position"]==1&&liveSetCalls==0&&jobs.size()==1,"ordinary monitor setter stays coalesced; queue ACK is not application ACK");
    flush();check(liveSetCalls==1,"ordinary queue still performs exactly one eventual SET");
    for(const auto& reason:std::vector<std::string>{"live-debug-unavailable","live-timeout","live-transport-uncertain","steam-account-changed"}){
        reset();running=true;client["overlay_fps_counter_corner"]=0;liveSetMode=reason;
        auto failed=steamSettingsSet({{"monitor",{{"position",0}}},{"awaitApplied",true}});
        check(failed["ok"]==false&&!failed.value("applicationConfirmed",false)&&failed["reason"]==reason&&failed["liveConfirmed"]==true,"matching GET cannot turn failed or uncertain SET into application success");
        check(liveSetCalls==1&&writes==0&&backups==0,"failed/uncertain awaited SET does not append VDF writes or replay");
    }
    reset();running=true;liveHook=[] {storeOk=false;};
    auto undurable=steamSettingsSet({{"monitor",{{"position",0}}},{"awaitApplied",true}});
    check(undurable["ok"]==false&&undurable["reason"]=="settings-write-failed"&&!undurable.value("applicationConfirmed",false)&&client["overlay_fps_counter_corner"]==0,"failed durable acknowledgement cannot be signed by successful Steam SET and GET");
    for(const auto& patch:std::vector<json>{{{"overlay_fps_counter_corner",nullptr}},{{"overlay_fps_counter_corner",7}},{{"overlay_fps_counter_corner",.5}},{{"overlay_fps_counter_scale_factor",2}},{{"overlay_fps_counter_detail_level",0}},{{"overlay_fps_counter_bgopacity","1"}}}){
        reset();running=true;getPatch=patch;auto bad=steamMonitorGet();
        check(bad["ok"]==false&&bad["available"]==false&&bad["liveConfirmed"]==false,"missing/type/enum/range-invalid readback never falls back to disk proof");
    }
    reset();running=true;getPatch={{"overlay_fps_counter_corner",1}};
    auto mismatch=steamSettingsSet({{"monitor",{{"position",0}}},{"awaitApplied",true}});
    check(!mismatch.value("applicationConfirmed",false)&&mismatch["ok"]==false,"successful SET receipt with mismatching fresh GET remains unconfirmed");
    reset();running=true;liveGetHook=[] {account="43";};auto swapped=steamMonitorGet();
    check(swapped["ok"]==false&&!swapped.value("liveConfirmed",false)&&swapped["reason"]=="steam-account-changed","account change during GET cannot sign another user's state");
    reset();running=true;liveGetHook=[] {activePid=778;};auto restarted=steamMonitorGet();
    check(restarted["ok"]==false&&!restarted.value("liveConfirmed",false)&&restarted["reason"]=="steam-session-changed","Steam process change during GET invalidates live proof");
    reset();running=true;liveHook=[] {account="43";};auto swapSet=steamSettingsSet({{"monitor",{{"position",0}}},{"awaitApplied",true}});
    check(swapSet["ok"]==false&&!swapSet.value("applicationConfirmed",false)&&swapSet["reason"]=="steam-account-changed","account change between request and readback rejects awaited application");
    reset();running=true;g_aiFanMockSession.isolated=true;auto isolatedRead=steamMonitorGet();
    check(isolatedRead["ok"]==false&&liveCalls==0,"isolated GET does not access ambient Steam");
    reset();running=true;liveMode="live-debug-unavailable";auto unreadable=steamMonitorGet();
    check(unreadable["ok"]==false&&unreadable["available"]==false&&!unreadable.value("liveConfirmed",false),"live transport unavailable cannot sign disk fallback as current state");
    for(const auto& invalid:std::vector<json>{{{"monitor",{{"position",0}}},{"awaitApplied","true"}},{{"monitor",{{"position",0}}},{"awaitApplied",true},{"unknown",1}},{{"mousePercent",100},{"awaitApplied",true}},{{"awaitApplied",true}}}){
        reset();auto rejected=steamSettingsSet(invalid);
        check(rejected["ok"]==false&&prefs.empty()&&jobs.empty()&&liveCalls==0,"awaited entry rejects malformed/extra/other-scope parameters before mutation");
    }

    }
    // The persisted steamMonitor document is untrusted independently of root JSON.
    // Native Steam/VDF and durable-store ports remain controlled by this consumer.
    int sectionFailures=0,sectionScenarios=0;
    const auto sectionCase=[&](const std::string& name,const std::function<void()>& fn){
        ++sectionScenarios;try{fn();}catch(const std::exception& e){++sectionFailures;std::cerr<<"FAIL section "<<name<<": "<<e.what()<<'\n';}
    };
    const auto seed=[](){return json{{"pending",true},{"account",account},{"path",W2U(path)},{"desired",{{"position",2}}},{"reason",""},{"restartRequired",false}};};
    std::vector<json> broken={nullptr,true,7,"bad",json::array(),json{{"pending",true}},json{{"pending",false},{"via","live"},{"liveConfirmed",true}}};
    reset();const auto validSeed=seed();
    for(const auto& [key,value]:std::vector<std::pair<std::string,json>>{
        {"pending","true"},{"pending",1},{"pending",nullptr},{"account",42},{"account",""},{"account","4294967296"},{"account","42x"},{"path",false},{"path",""},
        {"reason",true},{"restartRequired","false"},{"via",true},{"via","invented"},{"desired",nullptr},{"desired",json::array()},{"desired",json::object()},
        {"desired",{{"position",true}}},{"desired",{{"position",7}}},{"desired",{{"scale",1e200}}},{"desired",{{"ok",true},{"pending",false},{"position",0}}},
        {"livePid",-1},{"livePid",1.5},{"livePid",4294967296ULL},{"liveValues","bad"},{"liveValues",{{"ok",true},{"position",2}}},
        {"schemaVersion",2},{"schemaVersion","1"}}){auto item=validSeed;item[key]=value;broken.push_back(item);}
    auto badLive=validSeed;badLive["pending"]=false;badLive["via"]="live";badLive["livePid"]=777;badLive["liveValues"]=ymcc::steamsettings::read(disk);badLive["liveValues"]["position"]="0";broken.push_back(badLive);
    auto incompleteLive=validSeed;incompleteLive["pending"]=false;incompleteLive["via"]="live";broken.push_back(incompleteLive);
    for(size_t n=0;n<broken.size();++n)sectionCase("bad-persistent-"+std::to_string(n),[&,n]{
        reset();running=true;prefs["steamMonitor"]=broken[n];const auto before=prefs;const auto vdf=disk;
        const auto state=steamSettingsGet({{"scope","monitor"}});
        check(state["ok"]==false&&state["pending"]==false&&!state.value("liveConfirmed",false)&&!state.value("applicationConfirmed",false),"bad section read is safe and never signs live/application confirmation");
        check(prefs==before&&storeWrites==0&&liveCalls==0&&writes==0&&backups==0,"bad section GET preserves exact original without a Steam query or write");
        g_steamClientPending=true;steamSettingsQueue("section-retry");flush();steamSettingsQueue("section-retry-again");flush();
        check(!g_steamClientPending&&jobs.empty()&&prefs==before&&storeWrites==0&&liveCalls==0&&disk==vdf,"bad pending is disarmed across retries without replaying or rewriting the original");
        const auto recovered=steamSettingsSet({{"monitor",{{"position",4}}},{"awaitApplied",true}});
        check(recovered["ok"]==true&&recovered["applicationConfirmed"]==true&&recovered["liveConfirmed"]==true&&recovered["position"]==4,"explicit valid save recovers monitor and obtains a new independent live receipt");
        check(!storePreimages.empty()&&storePreimages.front()==before&&prefs["steamMonitor"]["desired"]==json{{"position",4}}&&liveSetCalls==1&&disk==vdf&&writes==0,"valid recovery preserves original preimage through store port and does not merge damaged desired");
        const auto reread=steamMonitorGet();check(reread["ok"]==true&&reread["liveConfirmed"]==true&&reread["pending"]==false&&reread["position"]==4,"recovered persisted receipt survives readback");
    });
    for(const auto& reply:std::vector<json>{nullptr,true,json::array(),json{{"ok","true"}},json{{"ok",false},{"reason",true}},json{{"ok",false},{"reason","live-debug-unavailable"},{"mutated","false"}},json{{"ok",true},{"values","wrong"}}}){
        sectionCase("malformed-live-GET",[&]{reset();running=true;overrideGet=true;replyOverride=reply;const auto before=prefs;const auto state=steamMonitorGet();check(state["ok"]==false&&state["pending"]==false&&!state.value("liveConfirmed",false)&&prefs==before&&storeWrites==0&&writes==0,"malformed live GET receipt returns unknown without throwing or changing persistent state");});
        sectionCase("malformed-live-SET",[&]{reset();running=true;overrideSet=true;replyOverride=reply;const auto state=steamMonitorSet({{"position",4}},true);check(state["ok"]==false&&state["pending"]==false&&!state.value("liveConfirmed",false)&&!state.value("applicationConfirmed",false),"malformed live SET cannot be confirmed by a later valid GET");check(!g_steamClientPending&&prefs["steamMonitor"]["pending"]==false&&writes==0&&backups==0,"invalid SET receipt is non-replayable and never falls back to Steam file writes");overrideSet=false;steamSettingsQueue("section-receipt-retry");flush();check(liveSetCalls==1&&writes==0,"retry after invalid SET receipt does not replay uncertain work");const auto good=steamMonitorSet({{"position",4}},true);check(good["applicationConfirmed"]==true&&good["liveConfirmed"]==true,"explicit valid save also recovers after bad live receipt");});
    }
    sectionCase("store-refusal-preserves-bad-original",[&]{reset();running=true;prefs["steamMonitor"]=broken[7];const auto original=prefs;storeOk=false;const auto denied=steamMonitorSet({{"position",4}},true);check(denied["ok"]==false&&prefs==original&&storePreimages.empty()&&liveCalls==0&&jobs.empty(),"store preservation/write refusal blocks valid recovery before a Steam SET");});
    sectionCase("valid-deferred-legacy-section",[&]{reset();running=true;prefs["steamMonitor"]=seed();prefs["steamMonitor"]["via"]="deferred";prefs["steamMonitor"]["restartRequired"]=true;const auto state=steamMonitorGet();check(state["ok"]==true&&state["pending"]==true&&state["liveConfirmed"]==true,"valid legacy pending remains queryable; no blanket removal of deferred monitor settings");check(liveSetCalls==0&&storeWrites==0,"valid pending GET stays read only");});

    for(const bool setter:{false,true})sectionCase(setter?"throwing-live-SET":"throwing-live-GET",[&]{reset();running=true;throwSet=setter;throwGet=!setter;const auto state=setter?steamMonitorSet({{"position",4}},true):steamMonitorGet();check(state["ok"]==false&&state["pending"]==false&&!state.value("liveConfirmed",false)&&!state.value("applicationConfirmed",false),"throwing live transport is contained without a fabricated confirmation");check(writes==0&&backups==0&&!g_steamClientPending,"transport exception causes no Steam file fallback or automatic replay");});
    sectionCase("valid-live-cache-and-unknown-extension",[&]{reset();running=true;const auto applied=steamMonitorSet({{"position",4}},true);check(applied["applicationConfirmed"]==true,"well formed live receipt is accepted");prefs["steamMonitor"]["futureNote"]={{"preserve","owned elsewhere"}};const auto state=steamMonitorGet();check(state["liveConfirmed"]==true&&state["position"]==4,"unknown harmless metadata does not invalidate a complete bounded receipt");});
    for(const auto& args:std::vector<json>{nullptr,true,json::array(),json{{"scope",nullptr}},json{{"scope",3}}})sectionCase("malformed-public-scope",[&]{reset();const auto state=steamSettingsGet(args);check(state["ok"]==false&&!state.value("pending",false)&&liveCalls==0&&storeWrites==0,"bad get schema is rejected before settings or Steam mutation");});

    for(const DWORD nextPid:{DWORD{778},DWORD{0}})sectionCase("same-account-process-change-after-durable-SET",[&]{
        reset();running=true;bool changed=false;
        storeHook=[&](const char* name,const json& value){if(std::string(name)=="steamMonitor"&&value.value("via",std::string())=="live"&&!value.value("pending",true)){activePid=nextPid;changed=true;storeHook={};}};
        const auto reply=steamMonitorSet({{"position",4}},true);
        check(changed&&reply["ok"]==false&&!reply.value("applicationConfirmed",false)&&reply["reason"]=="steam-session-changed","SET receipt cannot confirm a different process even when a fresh GET matches its values");
        check(liveSetCalls==1&&writes==0&&backups==0&&!g_steamClientPending,"session change after durable receipt never replays SET or falls back to a file write");
        activePid=779;const auto repaired=steamMonitorSet({{"position",3}},true);check(repaired["applicationConfirmed"]==true&&repaired["liveConfirmed"]==true,"explicit request in the new stable process can confirm independently");
    });
    sectionCase("temporary-read-fault-before-explicit-save",[&]{reset();running=true;readFault=true;const auto reply=steamMonitorSet({{"position",4}},true);check(reply["ok"]==false&&prefs.empty()&&storeWrites==0&&liveCalls==0&&jobs.empty(),"temporary localconfig read failure cannot persist intent or manufacture an apply");readFault=false;const auto recovered=steamMonitorSet({{"position",4}},true);check(recovered["applicationConfirmed"]==true,"explicit valid save recovers after a temporary read error");});
    std::cout<<"steamMonitor section scenarios: "<<sectionScenarios<<" failed: "<<sectionFailures<<'\n';if(sectionFailures)return 1;
    std::cout<<"steam settings production batch: "<<passed<<" cases passed; all IO in memory\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

#endif // YMCC_STEAM_SHARED_PRESENCE_ONLY

#endif // YMCC_STEAM_SETTINGS_OBSERVATION_ONLY

#endif // YMCC_STEAM_MODULE_PROFILE_PREPARATION_ONLY

#endif // YMCC_STEAM_MONITOR_LIVE_READ_ONLY
