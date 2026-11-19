// Actual IPC policy and dispatch arms are generated from the fixed main.
// Only task scheduling/response delivery ports below are controlled.
inline int g_asyncMode=2;
inline struct {int family=0;} g_machineIdentity;
inline const char* ymccFamilyName(int){return "fixture";}
inline std::atomic<bool> g_traceOn{false};
using IpcFn=std::function<json(const json&)>;
inline std::map<std::string,IpcFn> watchCommands;
inline std::vector<std::function<void()>> pendingPool;
inline std::vector<json> completedIpc;
inline std::shared_ptr<int> g_ipcResultMailbox;
inline unsigned asyncCompletions=0,syncCompletions=0,configuredCalls=0;
inline bool poolSubmit(std::function<void()> job){pendingPool.push_back(std::move(job));return true;}
inline void fanHttpCapturePowerAdmission(json&){throw std::runtime_error("unselected-http-path");}
inline void appendNativeLifecycleLog(const char*,const json&){throw std::runtime_error("unselected-http-log");}
template<class... Args> inline void traceLog(const char*,Args...){ }
inline void ipc_post_json(int,const json& value){++syncCompletions;completedIpc.push_back(value);}
inline void ipc_complete_async(const std::shared_ptr<int>&,unsigned long long,int,json value,std::string){++asyncCompletions;completedIpc.push_back(std::move(value));}
#include "watch-async-policy.inc"
inline void dispatchOriginalWatch(bool active,bool detailed){
    const std::string cmd="lossless.watch";
    json args={{"active",active},{"detailed",detailed}},resp={{"id",configuredCalls+1}};
    const unsigned long long generation=1;const int targetChildId=0;
    const bool traceRenderHandshake=false;
    watchCommands[cmd]=[](const json& a){++configuredCalls;return watchEntry(a);};
    const auto it=watchCommands.find(cmd);
#include "watch-dispatch-arms.inc"
}
inline void drainNewestPoolFirst(){
    while(!pendingPool.empty()){
        auto work=std::move(pendingPool.back());pendingPool.pop_back();
        std::thread worker(std::move(work));worker.join();
    }
}
inline void beginWatchDispatch(){
    endpointDiscoveries=endpointEmits=commonEmits=0;endpointReceiptKnown=false;g_exitRequested=false;commonRetryRequested=false;
    configuredCalls=asyncCompletions=syncCompletions=0;pendingPool.clear();completedIpc.clear();watchCommands.clear();
}
inline void endWatchDispatch(){
    drainNewestPoolFirst();endpoint->stop();endpoint.reset();watchEntry={};watchCommands.clear();g_sharedPowerProjectionPending=false;
}
