#pragma once
#include <chrono>
#include <condition_variable>

namespace ymcc::screenpads {
inline constexpr UINT kOwnerExportMessage=WM_APP+183;
inline constexpr auto kOwnerExportWait=std::chrono::milliseconds(200);
struct OwnerExportResult {Json snapshot;const char* reason=nullptr;};
inline Json ownerExportMissingReceipt(const char* reason){
    // Keep the full snapshot absent. The last published phase is atomic and
    // can still be read without contacting an unresponsive owner thread.
    return {{"name","screen-touchpads-export-state.json"},{"present",false},{"missingReason",reason},
        {"lastPublishedDisplayTransition",ownerDisplayExportState()}};
}
struct OwnerExportRequest {
    HWND window=nullptr;
    UINT_PTR ticket=0;
    bool started=false,finished=false;
    std::chrono::steady_clock::time_point deadline{};
    OwnerExportResult result;
};
inline std::mutex ownerExportMutex;
inline std::condition_variable ownerExportReady;
inline std::shared_ptr<OwnerExportRequest> ownerExportPending;
inline UINT_PTR ownerExportTicket=0;
inline bool ownerExportAccepting=false;

inline void openOwnerExport(){
    std::lock_guard lock(ownerExportMutex);
    ownerExportAccepting=true;
}

inline void retireOwnerExport(){
    std::lock_guard lock(ownerExportMutex);
    ownerExportAccepting=false;
    if(!ownerExportPending)return;
    ownerExportPending->result.reason="owner-retired";
    ownerExportPending->finished=true;
    ownerExportPending.reset();
    ownerExportReady.notify_all();
}

inline OwnerExportResult requestOwnerExport(){
    const HWND window=owner.load();
    if(!window||onOwnerThread(window))return {{},"owner-unavailable"};
    auto request=std::make_shared<OwnerExportRequest>();
    std::unique_lock lock(ownerExportMutex);
    if(!ownerExportAccepting||owner.load()!=window)return {{},"owner-retired"};
    if(ownerExportPending)return {{},"snapshot-busy"};
    if(ownerExportTicket==(std::numeric_limits<UINT_PTR>::max)())return {{},"snapshot-ticket-exhausted"};
    request->ticket=++ownerExportTicket;request->window=window;
    request->deadline=std::chrono::steady_clock::now()+kOwnerExportWait;
    ownerExportPending=request;
    if(!PostMessageW(window,kOwnerExportMessage,request->ticket,0)){
        ownerExportPending.reset();return {{},"snapshot-post-failed"};
    }
    if(!ownerExportReady.wait_until(lock,request->deadline,[&]{return request->finished;})){
        if(ownerExportPending==request)ownerExportPending.reset();
        return {{},"snapshot-timeout"};
    }
    return std::move(request->result);
}

template<class Capture> inline bool consumeOwnerExport(HWND window,UINT_PTR ticket,const Capture& capture){
    if(window!=owner.load()||!onOwnerThread(window))return false;
    std::shared_ptr<OwnerExportRequest> request;
    {
        std::lock_guard lock(ownerExportMutex);
        if(!ownerExportPending||ownerExportPending->ticket!=ticket||ownerExportPending->window!=window||ownerExportPending->started||
           std::chrono::steady_clock::now()>=ownerExportPending->deadline)return false;
        request=ownerExportPending;request->started=true;
    }
    const auto epoch=ownerLifecycleEpoch;
    OwnerExportResult result;
    if(!initialized||inputTeardown)result.reason="owner-inactive";
    else {
        try{result.snapshot=capture();}catch(...){result.reason="snapshot-capture-failed";}
        if(epoch!=ownerLifecycleEpoch||window!=owner.load()||!initialized||inputTeardown)result={{},"owner-retired"};
    }
    std::lock_guard lock(ownerExportMutex);
    if(ownerExportPending!=request)return false;
    if(std::chrono::steady_clock::now()>=request->deadline)result={{},"snapshot-timeout"};
    request->result=std::move(result);request->finished=true;
    ownerExportPending.reset();ownerExportReady.notify_all();
    return true;
}
} // namespace ymcc::screenpads
