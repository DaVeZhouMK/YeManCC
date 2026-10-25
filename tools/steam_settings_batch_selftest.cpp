#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <iostream>
#include "json.hpp"
#include "sleep_steam_preferences.h"
#include "steam_monitor_settings.h"
using json=nlohmann::json;
static int passed=0,writes=0,backups=0,mouseWrites=0,liveCalls=0,liveSetCalls=0,liveGetCalls=0,storeWrites=0;
static bool running=false,writeOk=true,copyOk=true,poolOk=true,storeOk=true;
static std::string account="42",liveMode="ok",liveSetMode="ok",disk;
static DWORD activePid=777;
static json getPatch=json::object();
static std::wstring path=LR"(R:\memory-only\42\config\localconfig.vdf)";
static json prefs=json::object(),client=json::object();
static std::vector<std::function<void()>> jobs;
static std::vector<std::string> order;
static std::function<void()> liveHook,liveGetHook;
static struct {bool isolated=false;} g_aiFanMockSession;
static std::mutex g_sofMx,g_sdmMx;
static std::atomic<bool> g_sofQueued{false},g_sofPending{false},g_sofEnabled{false},g_sdmEventPending{false},g_sdmRefreshRequested{false},g_sdmApplyRequested{false};
static std::atomic<int> g_sofDesired{-1};
static std::atomic<unsigned long long> g_sofRequestSerial{0};
static json g_sofReceipt={{"via","none"}};
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
static bool sofRegDWORD(HKEY,const wchar_t*,const wchar_t*,DWORD* pid){*pid=activePid;return true;}
static bool sofFindLocalConfig(std::wstring* out,std::string* user){if(out)*out=path;if(user)*user=account;return !account.empty();}
static bool sofSteamRunning(){return running;}
static std::string sgReadFile(const std::wstring& file){if(file!=path)throw std::runtime_error("unexpected-file-read");return disk;}
static BOOL memoryCopy(const wchar_t*,const wchar_t*,BOOL){++backups;return copyOk;}
#define CopyFileW memoryCopy
static bool sgWriteFileAtomic(const std::wstring& file,const std::string& text){if(file!=path || running)throw std::runtime_error("unsafe-file-write");++writes;order.push_back("localconfig");if(writeOk)disk=text;return writeOk;}
static json ymSettingsSection(const char* name){return prefs.value(name,json::object());}
static bool ymSettingsWriteSection(const char* name,const json& value){++storeWrites;if(!storeOk)return false;prefs[name]=value;return true;}
static bool ymSettingsPatchSection(const char* name,const json& value){auto merged=ymSettingsSection(name);merged.update(value);return ymSettingsWriteSection(name,merged);}
static void ipc_emit(const char*,const json&){}
static void appendNativeLifecycleLog(const char*,const json&){}
static bool poolSubmit(std::function<void()> job){if(!poolOk)return false;jobs.push_back(std::move(job));return true;}
static json steamOverlayFixStateLocked(){return ymcc::sleepprefs::retiredOverlayReply();}
static json steamOverlayFixGet(){return steamOverlayFixStateLocked();}
static int sofCurrentValue(const std::string& text,bool*){const auto roots=ymcc::steamdeck::Vdf(text).parse();const auto& sys=ymcc::steamsettings::system(roots);auto value=ymcc::steamdeck::scalar(sys.children,"EnableGameOverlay");return value.empty()?1:std::stoi(value);}
static json steamLiveRequest(DWORD user,json request){
    ++liveCalls;request["operation"]=="settings.set"?++liveSetCalls:++liveGetCalls;
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
static void steamSettingsQueue(const char*);
static json steamDeckMouseSet(const json& args){if(!args.contains("percent")||!args["percent"].is_number_integer())return {{"ok",false}};g_sdmEventPending=true;steamSettingsQueue("mouse-user");return sdmStateLocked();}
//__PRODUCTION_OVERLAY_KICK__
#include "steam_settings_runtime.h"
static void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);++passed;std::cout<<"PASS "<<text<<'\n';}
static void flush(){while(!jobs.empty()){auto next=std::move(jobs);jobs.clear();for(auto& job:next)job();}}
static void reset(){
    prefs=json::object();jobs.clear();order.clear();liveHook={};liveGetHook={};getPatch=json::object();activePid=777;running=false;writeOk=copyOk=poolOk=storeOk=true;
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
    std::cout<<"steam settings production batch: "<<passed<<" cases passed; all IO in memory\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
