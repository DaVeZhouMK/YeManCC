#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <iostream>
#include "json.hpp"
#include "steam_monitor_settings.h"
using json=nlohmann::json;
static int passed=0,writes=0,backups=0,mouseWrites=0,liveCalls=0;
static bool running=false,writeOk=true,copyOk=true,poolOk=true,storeOk=true;
static std::string account="42",liveMode="ok",disk;
static std::wstring path=LR"(R:\memory-only\42\config\localconfig.vdf)";
static json prefs=json::object(),client=json::object();
static std::vector<std::function<void()>> jobs;
static std::vector<std::string> order;
static std::function<void()> liveHook;
static struct {bool isolated=false;} g_aiFanMockSession;
static std::mutex g_sofMx,g_sdmMx;
static std::atomic<bool> g_sofQueued{false},g_sofPending{false},g_sofEnabled{false},g_sdmEventPending{false},g_sdmRefreshRequested{false},g_sdmApplyRequested{false};
static std::atomic<int> g_sofDesired{-1};
static std::atomic<unsigned long long> g_sofRequestSerial{0};
static json g_sofReceipt={{"via","none"}};
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
static bool sofRegDWORD(HKEY,const wchar_t*,const wchar_t*,DWORD* pid){*pid=777;return true;}
static bool sofFindLocalConfig(std::wstring* out,std::string* user){if(out)*out=path;if(user)*user=account;return !account.empty();}
static bool sofSteamRunning(){return running;}
static std::string sgReadFile(const std::wstring& file){if(file!=path)throw std::runtime_error("unexpected-file-read");return disk;}
static BOOL memoryCopy(const wchar_t*,const wchar_t*,BOOL){++backups;return copyOk;}
#define CopyFileW memoryCopy
static bool sgWriteFileAtomic(const std::wstring& file,const std::string& text){if(file!=path || running)throw std::runtime_error("unsafe-file-write");++writes;order.push_back("localconfig");if(writeOk)disk=text;return writeOk;}
static json ymSettingsSection(const char* name){return prefs.value(name,json::object());}
static bool ymSettingsWriteSection(const char* name,const json& value){if(!storeOk)return false;prefs[name]=value;return true;}
static bool ymSettingsPatchSection(const char* name,const json& value){auto merged=ymSettingsSection(name);merged.update(value);return ymSettingsWriteSection(name,merged);}
static void ipc_emit(const char*,const json&){}
static void appendNativeLifecycleLog(const char*,const json&){}
static bool poolSubmit(std::function<void()> job){if(!poolOk)return false;jobs.push_back(std::move(job));return true;}
static json steamOverlayFixStateLocked(){auto result=g_sofReceipt;result["pending"]=g_sofPending.load();return result;}
static json steamOverlayFixGet(){return steamOverlayFixStateLocked();}
static int sofCurrentValue(const std::string& text,bool*){const auto roots=ymcc::steamdeck::Vdf(text).parse();const auto& sys=ymcc::steamsettings::system(roots);auto value=ymcc::steamdeck::scalar(sys.children,"EnableGameOverlay");return value.empty()?1:std::stoi(value);}
static json steamLiveRequest(DWORD user,json request){
    ++liveCalls;if(std::to_string(user)!=account)return {{"ok",false},{"reason","steam-account-changed"},{"mutated",false}};
    if(liveMode!="ok")return {{"ok",false},{"reason",liveMode},{"mutated",liveMode=="live-transport-uncertain"}};
    if(request["operation"]=="settings.set"){
        client.update(request["values"]);order.push_back("live-client");
        if(liveHook){auto hook=std::move(liveHook);liveHook={};hook();}
        return {{"ok",true},{"values",request["values"]}};
    }
    json values=json::object();for(const auto& key:request["keys"])values[key.get<std::string>()]=client[key.get<std::string>()];
    return {{"ok",true},{"values",values}};
}
static bool steamLiveCanDefer(const json& result){return !result.value("mutated",false)&&(result.value("reason",std::string())=="live-debug-unavailable"||result.value("reason",std::string())=="steam-session-starting");}
static void sdmApplyLocked(){if(g_sdmEventPending.exchange(false)){++mouseWrites;order.push_back("mouse-layout");}}
static json sdmStateLocked(bool=false){return {{"ok",true},{"pending",g_sdmEventPending.load()}};}
static json steamDeckMouseGet(){return sdmStateLocked();}
static void steamSettingsQueue(const char*);
static json steamDeckMouseSet(const json& args){if(!args.contains("percent")||!args["percent"].is_number_integer())return {{"ok",false}};g_sdmEventPending=true;steamSettingsQueue("mouse-user");return sdmStateLocked();}
//__PRODUCTION_OVERLAY_KICK__
#include "steam_settings_runtime.h"
static void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);++passed;std::cout<<"PASS "<<text<<'\n';}
static void flush(){while(!jobs.empty()){auto next=std::move(jobs);jobs.clear();for(auto& job:next)job();}}
static void reset(){
    prefs=json::object();jobs.clear();order.clear();liveHook={};running=false;writeOk=copyOk=poolOk=storeOk=true;
    account="42";path=LR"(R:\memory-only\42\config\localconfig.vdf)";liveMode="ok";
    disk=R"VDF("UserLocalConfigStore" {
        "Software" { "system" { "EnableGameOverlay" "SHADOW" } }
        // unrelated monitor-specific preferences stay byte-identical
        "system" { "EnableGameOverlay" "1" "InGameOverlayShowFPSCorner" "1" "InGameOverlayShowFPSScaling" "0.800000011920928955" "keep" "KEEP" }
        "other" { "future" "sentinel" }
    })VDF";
    writes=backups=mouseWrites=liveCalls=0;g_aiFanMockSession.isolated=false;
    g_sofPending=false;g_sofEnabled=false;g_sofQueued=false;g_sofDesired=-1;g_sofRequestSerial=0;
    g_steamSettingsQueued=false;g_steamSettingsSerial=0;g_steamClientPending=false;g_sdmEventPending=false;g_sdmRefreshRequested=false;g_sdmApplyRequested=false;
    g_steamOverlayAccount.clear();g_steamOverlayPath.clear();g_steamOverlayPinnedSerial=0;
    client=ymcc::steamsettings::clientPatch(ymcc::steamsettings::read(disk),1);
}
int main(){try{
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
    auto values=ymcc::steamsettings::read(disk);check(values["position"]==5&&values["opacity"]==.7&&values["scale"]==1.4&&sofCurrentValue(disk,nullptr)==0,"batch preserves every field and overlay change");
    check(!prefs["steamMonitor"]["pending"].get<bool>()&&!g_sofPending&&g_sofReceipt["via"]=="file","only confirmed file readback clears queued intent");
    reset();running=true;steamMonitorSet({{"position",6},{"detail",4}});g_sofEnabled=true;steamOverlayFixKick("setting");flush();
    check(writes==0&&backups==0&&liveCalls==1,"live monitor+overlay share one Steam-context batch, no VDF writes");
    check(client["overlay_fps_counter_corner"]==6&&client["enable_overlay"]==false,"live unified batch uses Steam's exact settings names");
    check(steamMonitorGet()["position"]==6,"confirmed live receipt/get does not snap back to stale file content");
    reset();running=true;liveMode="live-debug-unavailable";steamMonitorSet({{"position",2},{"scale",.2}});g_sofEnabled=true;steamOverlayFixKick("setting");flush();
    check(writes==0&&g_sofPending&&g_steamClientPending&&steamMonitorGet()["restartRequired"]==true,"unsafe live editing defers both settings and explicitly requires restart");
    running=false;steamSettingsQueue("steam-session");flush();
    check(writes==1&&sofCurrentValue(disk,nullptr)==0&&ymcc::steamsettings::read(disk)["scale"]==.2,"Steam stop event safely commits deferred monitor+overlay once");
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
    check(client["enable_overlay"]==true&&!g_sofPending,"new overlay intent during IO follows up through same lane and wins");
    reset();auto overlayAck=steamSettingsSet({{"overlayOffFix",true}});
    check(overlayAck["accepted"]==true && prefs["sleep"]["steamOverlayOffFix"]==true && writes==0,"sleep overlay adapter persists parameter through public unified entry before applying");
    flush();check(sofCurrentValue(disk,nullptr)==0,"unified overlay entry preserves existing inverted off-fix semantics");
    check(steamSettingsSet({{"monitor",{{"position",1}}},{"mousePercent",100}})["ok"]==false,"ambiguous multi-operation IPC rejected before any mutation");
    std::cout<<"steam settings production batch: "<<passed<<" cases passed; all IO in memory\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
