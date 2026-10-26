#pragma once
// Thread-safe, process-local manual-operation receipts. No I/O, timers or workers.
#include "json.hpp"
#include <cstddef>
#include <list>
#include <mutex>
#include <string>

namespace ymcc { namespace staticwake {
using Json = nlohmann::json;
class OperationReceipts {
    struct Receipt {
        std::string requestId,action;
        bool completed=false,ok=false;
    };
    mutable std::mutex mutex_;
    // Completing a record moves it to the end, ordering eviction by completion.
    std::list<Receipt> receipts_;
public:
    static constexpr std::size_t capacity=16;
    // IDs are case-sensitive, 1..96 bytes, and only ASCII [A-Za-z0-9_-].
    static bool validRequestId(const std::string& id) {
        if(id.empty()||id.size()>96)return false;
        for(const unsigned char c:id)
            if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
        return true;
    }
    static bool validAction(const std::string& action) {return action=="scan"||action=="restore";}
    // Admit exactly one running record. Invalid inputs and any retained ID reject.
    // At capacity, evict only the oldest completed record; all-running rejects.
    // Replay protection lasts while retained; callers must generate unique IDs.
    bool begin(const std::string& id,const std::string& action) {
        if(!validRequestId(id)||!validAction(action))return false;
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& r:receipts_)if(r.requestId==id)return false;
        if(receipts_.size()==capacity) {
            auto oldest=receipts_.begin();
            while(oldest!=receipts_.end()&&!oldest->completed)++oldest;
            if(oldest==receipts_.end())return false;
            receipts_.erase(oldest);
        }
        receipts_.push_back({id,action,false,false});return true;
    }
    // Only the matching running (ID, action) can finish, including ok=false.
    // Return false without changes for invalid, absent, mismatched or completed.
    bool complete(const std::string& id,const std::string& action,bool ok) {
        if(!validRequestId(id)||!validAction(action))return false;
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto it=receipts_.begin();it!=receipts_.end();++it)if(it->requestId==id) {
            if(it->completed||it->action!=action)return false;
            it->completed=true;it->ok=ok;
            receipts_.splice(receipts_.end(),receipts_,it);return true;
        }
        return false;
    }
    // Snapshot: {requestId, action, state}, adding boolean ok ONLY if completed.
    // Absent/evicted IDs return unknown with action="" and no ok.
    // Invalid IDs also return requestId="", without copying/echoing the input.
    // Lookup never admits, completes, refreshes or evicts a record.
    Json get(const std::string& id) const {
        if(!validRequestId(id))return {{"requestId",""},{"action",""},{"state","unknown"}};
        Json result={{"requestId",id},{"action",""},{"state","unknown"}};
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& r:receipts_)if(r.requestId==id) {
            result["action"]=r.action;result["state"]=r.completed?"completed":"running";
            if(r.completed)result["ok"]=r.ok;
            break;
        }
        return result;
    }
};
}} // namespace ymcc::staticwake
