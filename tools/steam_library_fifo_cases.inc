// Explicit extension of the original no-restart consumer. Production queue,
// shutdown helper and commit bodies are exact generated source slices.
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <thread>
#include "steam_information_client.h"
#include "steam_client_action_channel.h"

namespace ymcc::steaminfo {static Facts fifoRead(std::uint32_t mask);}
namespace ymcc::steamaction {static Outcome fifoShutdown(DWORD,std::uint64_t,ULONGLONG,
    std::uint64_t,std::uint64_t,std::uint64_t,const std::function<bool()>&);}

namespace LibraryFifoFixture {
using json=nlohmann::json;
static std::mutex g_steamOperationMutex;
static std::atomic<bool> g_taskRunning{false},g_cancelRequested{false},g_shuttingDown{false};
static std::atomic<ULONGLONG> clockTick{1000};
static HWND g_window=nullptr;
static constexpr UINT WM_WORKSPACE_WRITES_DRAINED=WM_APP+0x46;
static int g_artworkJobs=0,g_identitySearchJobs=0;
static std::string scenario;
static unsigned factsReads=0,actionCalls=0,writesAttempted=0,writesCommitted=0,snapshots=0,launchCalls=0,drainedMessages=0,unexpectedPorts=0;
static bool submitted=false,launchFailure=false;
static std::function<void()> beforeLaunchFailure;
static std::vector<json> executions,completions,replies,progress;
static std::mutex replyMutex,gateMutex;
static std::condition_variable gateChanged,threadFinished;
static bool released=false,finished=false;
static std::thread worker;
static std::string jsonStringOr(const json& value,const char* name,const std::string& fallback={}){
    const auto found=value.find(name);return found!=value.end()&&found->is_string()?found->get<std::string>():fallback;
}
static bool jsonBoolOr(const json& value,const char* name,bool fallback=false){
    const auto found=value.find(name);return found!=value.end()&&found->is_boolean()?found->get<bool>():fallback;
}
static json jsonArrayOr(const json& value,const char* name){
    const auto found=value.find(name);return found!=value.end()&&found->is_array()?*found:json::array();
}
static ULONGLONG fifoTickCount(){return clockTick.load();}
static void fifoSleep(DWORD delay){if(delay!=100)throw std::logic_error("unexpected production sleep port");clockTick.fetch_add(delay);}
static BOOL WINAPI fifoPostMessage(HWND window,UINT message,WPARAM,LPARAM){
    if(window!=g_window||message!=WM_WORKSPACE_WRITES_DRAINED)throw std::logic_error("unexpected window publication");
    ++drainedMessages;return TRUE;
}
static ymcc::steaminfo::Facts facts(std::uint32_t mask){
    if(mask!=ymcc::steaminfo::Presence)throw std::logic_error("unexpected facts mask");
    ++factsReads;ymcc::steaminfo::Facts out;out.available=true;out.requested=mask;out.presence=1;
    out.processPid=123;out.processBirth=456;out.processPath="G:/inert/steam.exe";
    out.actionPowerGeneration=7;out.actionPowerSequence=2;out.actionPowerSerial=1;
    if(scenario=="fifo-already-absent")out.presence=0;
    if(submitted){
        if(scenario=="fifo-replacement-after-submit"){out.processPid=124;out.processBirth=789;}
        else if(scenario=="fifo-unknown-after-submit"){out.available=false;out.presence=-1;}
        else out.presence=0;
    }
    if(scenario=="fifo-cancel-during-presence")g_cancelRequested=true;
    return out;
}
static ymcc::steamaction::Outcome shutdown(DWORD pid,std::uint64_t birth,ULONGLONG deadline,
        std::uint64_t generation,std::uint64_t sequence,std::uint64_t serial,const std::function<bool()>& current){
    ++actionCalls;
    if(pid!=123||birth!=456||generation!=7||sequence!=2||serial!=1||deadline<=fifoTickCount()||!current||!current())
        throw std::logic_error("fixed native shutdown lost the original tuple or deadline");
    if(scenario=="fifo-shutdown-unknown")return ymcc::steamaction::Outcome::Unknown;
    submitted=true;return ymcc::steamaction::Outcome::Submitted;
}
static bool hostSteamRunning(){
    const auto value=facts(ymcc::steaminfo::Presence);
    if(!value.available||value.presence<0)throw std::runtime_error("controlled unknown presence");return value.presence==1;
}
static void setSteamTarget(const json&){}
static json committed(const json& target){
    ++writesAttempted;const auto marker=jsonStringOr(target,"fixtureRequest");
    if(marker=="fail")throw std::runtime_error("controlled writer failure");
    if(marker=="throw")throw 42;
    ++writesCommitted;json value={{"committed",true},{"fixtureRequest",marker}};completions.push_back(value);return value;
}
static json commitToAllSteamAccounts(const json& target){return committed(target);}
static json refreshArtworkForAllSteamAccounts(const json& target){return committed(target);}
static fs::path workspacePlanPath(){return L"inert-fifo-no-file-plan.json";}
static std::vector<std::wstring> steamTargetArguments(const json&){throw std::logic_error("unselected single-account port");}
struct ProcessResult {DWORD exitCode=0;};
static ProcessResult runWorker(const std::vector<std::wstring>&){throw std::logic_error("unselected worker disk port");}
static json readSteamAddCompletion(const ProcessResult&,const fs::path&,const json&){throw std::logic_error("unselected completion reader");}
static void attachArtworkPreviews(json& value){value["previewObserved"]=true;}
static json workspaceSnapshot(bool,const json&){++snapshots;return {{"steamPlan",{{"fixtureRequest","stale"}}}};}
static void postWebMessageNoThrow(json message){std::lock_guard lock(replyMutex);replies.push_back(std::move(message));}
static void postPipelineEvent(const std::string&,json message){std::lock_guard lock(replyMutex);progress.push_back(std::move(message));}
struct AsyncResult {json message;bool refreshWatchers=false;};
static bool enqueueWorkspaceResult(std::unique_ptr<AsyncResult> value)noexcept{
    try{postWebMessageNoThrow(std::move(value->message));return true;}catch(...){return false;}
}
static void release(){std::lock_guard lock(gateMutex);released=true;gateChanged.notify_all();}
static void join(){
    if(!worker.joinable())return;
    {std::unique_lock lock(gateMutex);if(!threadFinished.wait_for(lock,std::chrono::seconds(4),[]{return finished;}))throw std::runtime_error("owned FIFO worker failed to finish");}
    worker.join();
}
static void launchHostBackgroundTask(std::function<void()> action){
    ++launchCalls;
    if(launchFailure){if(beforeLaunchFailure)beforeLaunchFailure();throw std::runtime_error("controlled thread creation failure");}
    if(worker.joinable())throw std::logic_error("second simultaneous FIFO worker");
    worker=std::thread([action=std::move(action)]{
        {std::unique_lock lock(gateMutex);if(!gateChanged.wait_for(lock,std::chrono::seconds(3),[]{return released;})){
            finished=true;threadFinished.notify_all();return;}}
        try{action();}catch(...){++unexpectedPorts;}
        {std::lock_guard lock(gateMutex);finished=true;}threadFinished.notify_all();
    });
}
[[noreturn]] static void unselected(){++unexpectedPorts;throw std::logic_error("unselected UI/network/background port");}
static void acceptWorkspaceRefreshProtocol(const json&){unselected();}
static std::optional<json> admitHostDataRootCommand(const std::string&){return {};}
static void appendUiError(const json&){unselected();}
static bool cancelBackgroundJobs(int,const std::string& = {}){unselected();}
static std::string backgroundExecutableKey(const std::string&){unselected();}
static void startArtworkTask(uint64_t,std::string,json){unselected();}
static void startNetworkWakeTask(uint64_t){unselected();}
static bool requestTaskCancellation(){unselected();}
static json startIdentitySearchRequest(const json&){unselected();}
static json executeWorkspaceCommand(const std::string&,const json&);
}
namespace ymcc::steaminfo {static Facts fifoRead(std::uint32_t mask){return LibraryFifoFixture::facts(mask);}}
namespace ymcc::steamaction {static Outcome fifoShutdown(DWORD p,std::uint64_t b,ULONGLONG d,
    std::uint64_t g,std::uint64_t s,std::uint64_t r,const std::function<bool()>& c){return LibraryFifoFixture::shutdown(p,b,d,g,s,r,c);}}

namespace LibraryFifoFixture {
#define GetTickCount64 fifoTickCount
#define Sleep fifoSleep
#define PostMessageW fifoPostMessage
#define read fifoRead
#define shutdown fifoShutdown
#include "production-library-fifo.h"
#undef shutdown
#undef read
#undef PostMessageW
#undef Sleep
#undef GetTickCount64
#include "production-library-no-restart.h"
static json executeWorkspaceCommand(const std::string& command,const json& args){
    executions.push_back({{"command",command},{"arguments",args}});
    if(command=="commit"||command=="refreshSteamArtwork")return productionLibraryCommand(command,args);
    if(command=="closeSteam"){closeSteamForShortcutCommit(args);return {{"closed",true}};}
    unselected();
}
static json arguments(const std::string& marker="one"){
    return {{"selectionMode","all-steam-accounts"},{"selectedGameDirectories",json::array({"fixture-b","fixture-a"})},
        {"forceCloseSteam",true},{"fixtureRequest",marker},{"steamClient",{{"pid",uint32_t{123}},{"birth","456"},
            {"powerGeneration","7"},{"powerSequence","2"},{"powerSerial","1"}}}};
}
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void reset(const std::string& selected){
    need(!worker.joinable(),"previous worker still owned");scenario=selected;clockTick=1000;
    g_taskRunning=false;g_cancelRequested=false;g_shuttingDown=false;g_closeAfterWrites=false;g_acceptedWrites=0;
    factsReads=actionCalls=writesAttempted=writesCommitted=snapshots=launchCalls=drainedMessages=unexpectedPorts=0;
    submitted=false;launchFailure=false;released=finished=false;beforeLaunchFailure={};executions.clear();completions.clear();replies.clear();progress.clear();
    need(g_commandQueue.empty()&&g_commitWaiters.empty(),"prior queue owner not returned");
}
static void assertReplies(const std::vector<std::pair<uint64_t,bool>>& expected){
    need(replies.size()==expected.size(),"reply count lost or duplicated");
    for(const auto& [id,ok]:expected){auto found=std::find_if(replies.begin(),replies.end(),[&](const auto& r){return r.at("id")==id;});
        need(found!=replies.end()&&found->at("ok")==ok,"waiter result/id mismatch");
        if(ok&&found->contains("result")&&found->at("result").contains("steamPlan"))need(found->at("result").at("steamPlan").value("fixtureRequest",std::string{})!="stale","queue returned stale completion");}
}
static const std::vector<std::string> names={"fifo-current-dedupe","fifo-expired-dedupe-no-renewal","fifo-ui-deadline-overwritten", "fifo-close-retains-write",
    "fifo-shutdown-unknown","fifo-replacement-after-submit","fifo-unknown-after-submit","fifo-cancel-during-presence",
    "fifo-failure-continues","fifo-nonstd-failure-continues","fifo-launch-failure-returns-waiters","fifo-already-absent"};
static json run(const std::string& selected){
    reset(selected);std::string error;
    try{
        if(selected=="fifo-launch-failure-returns-waiters"){
            launchFailure=true;beforeLaunchFailure=[]{beginCommand(102,"commit",arguments());};beginCommand(101,"commit",arguments());
            need(!g_taskRunning&&g_acceptedWrites==0&&g_commitWaiters.empty()&&g_commandQueue.empty(),"thread creation failure retained admission");
            assertReplies({{101,false},{102,false}});need(executions.empty()&&actionCalls==0&&writesAttempted==0,"failed launch executed business");
            launchFailure=false;beforeLaunchFailure={};beginCommand(103,"commit",arguments("next"));release();join();
            assertReplies({{101,false},{102,false},{103,true}});need(actionCalls==1&&writesCommitted==1,"next request did not recover after failed launch");
        }else if(selected=="fifo-failure-continues"||selected=="fifo-nonstd-failure-continues"){
            beginCommand(101,"commit",arguments(selected=="fifo-failure-continues"?"fail":"throw"));beginCommand(102,"commit",arguments("next"));
            need(g_acceptedWrites==2&&g_commandQueue.size()==2,"different requests merged or lost");release();join();
            assertReplies({{101,false},{102,true}});need(writesAttempted==2&&writesCommitted==1&&executions.size()==2,"failure blocked FIFO successor");
        }else{
            auto args=arguments();if(selected=="fifo-ui-deadline-overwritten")args["_nativeSteamDeadlineTick"]=(std::numeric_limits<uint64_t>::max)();
            beginCommand(101,"commit",args);
            {std::lock_guard lock(g_commandQueueMutex);need(g_commandQueue.size()==1&&g_acceptedWrites==1&&g_commandQueue.front().arguments.at("_nativeSteamDeadlineTick")==uint64_t{6000},"native deadline not captured before FIFO");}
            bool duplicate=selected=="fifo-current-dedupe"||selected=="fifo-expired-dedupe-no-renewal";
            if(duplicate){clockTick=2000;std::reverse(args["selectedGameDirectories"].begin(),args["selectedGameDirectories"].end());beginCommand(102,"commit",args);
                std::lock_guard lock(g_commandQueueMutex);need(g_commandQueue.size()==1&&g_commitWaiters.size()==1&&g_commitWaiters.begin()->second.size()==2&&g_acceptedWrites==1,
                    "duplicate obtained another writer or lost a waiter");need(g_commandQueue.front().arguments.at("_nativeSteamDeadlineTick")==uint64_t{6000},"duplicate renewed original deadline");}
            if(selected=="fifo-close-retains-write"){g_closeAfterWrites=true;beginCommand(102,"commit",arguments("late"));need(g_acceptedWrites==1&&!g_cancelRequested,"close dropped/cancelled an accepted write");}
            if(selected=="fifo-expired-dedupe-no-renewal"||selected=="fifo-ui-deadline-overwritten")clockTick=6500;
            release();join();
            const bool success=selected=="fifo-current-dedupe"||selected=="fifo-close-retains-write"||selected=="fifo-already-absent";
            if(duplicate)assertReplies({{101,success},{102,success}});
            else if(selected=="fifo-close-retains-write")assertReplies({{101,true},{102,false}});
            else assertReplies({{101,success}});
            need(executions.size()==1&&writesCommitted==(success?1u:0u)&&writesAttempted==(success?1u:0u),"refused command reached writer or current request lost");
            if(selected=="fifo-expired-dedupe-no-renewal"||selected=="fifo-ui-deadline-overwritten"||selected=="fifo-cancel-during-presence"||selected=="fifo-already-absent")need(actionCalls==0,"expired/cancelled/absent request submitted shutdown");
            else need(actionCalls==1,"fixed shutdown was replayed or omitted");
            if(selected=="fifo-close-retains-write")need(drainedMessages==1,"close did not receive original drained notification");
        }
        need(!worker.joinable()&&!g_taskRunning&&g_commandQueue.empty()&&g_commitWaiters.empty()&&g_acceptedWrites==0,"FIFO ownership not returned");
        need(unexpectedPorts==0,"unexpected UI/network/worker path or exception escaped");
    }catch(const std::exception& e){error=e.what();release();try{join();}catch(...){}}
    return {{"name",selected},{"ok",error.empty()},{"error",error},{"executions",executions},{"replies",replies},{"progress",progress},
        {"factsReads",factsReads},{"shutdownCalls",actionCalls},{"writesAttempted",writesAttempted},{"writesCommitted",writesCommitted},
        {"launchCalls",launchCalls},{"drainedMessages",drainedMessages},{"acceptedWritesFinal",g_acceptedWrites.load()},
        {"queueEmpty",g_commandQueue.empty()},{"waitersEmpty",g_commitWaiters.empty()},{"workerJoined",!worker.joinable()},{"unexpectedPorts",unexpectedPorts},
        {"clockIsControlled",true},{"factsAndShutdownAndDiskAreControlled",true},{"actualSteamOrDiskWrite",false}};
}
static int main(int argc,char** argv){
    if(argc!=4||std::string(argv[2])!="--fifo-case"||std::string(argv[3]).empty()||std::find(names.begin(),names.end(),argv[3])==names.end())return 2;
    const auto out=fs::absolute(argv[1]);if(fs::exists(out))return 2;fs::create_directories(out.parent_path());if(!fs::create_directory(out))return 2;
    const auto row=run(argv[3]);const json receipt={{"ok",row.at("ok")},{"checks",1},{"cases",json::array({row})},
        {"selected",argv[3]},{"defaultMatrixExecuted",false},{"newCanonicalIds",0},{"realSteamOperations",0},{"defaultMatrixCount",24}};
    std::ofstream stream(out/"result.json",std::ios::binary);stream<<receipt.dump(2)<<'\n';stream.close();if(!stream)return 1;
    std::cout<<receipt.dump()<<'\n';return row.at("ok")==true?0:1;
}
}
