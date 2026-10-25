#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "legion_motion_message_clock.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
thread_local bool fixtureRouteEnabled=true;
static bool inputCaptureLegionControllerMotionRoute() { return fixtureRouteEnabled; }
// Verbatim production wrapper/constant, generated and hash-bound by the runner.
// GetMessageTime and GetTickCount64 are actual Win32 imports, never substituted.
#include "main_message_clock_bridge.h"
namespace {
constexpr UINT messageId=WM_APP+0x351;
constexpr wchar_t className[]=L"YMCC_Legion_Private_Queue_Clock_Selftest";
std::uint64_t checks=0, failures=0, observations=0, serial=0;
std::uint64_t sameQueuePairs=0, sameCutoverPairs=0, sameRejectedPairs=0, nextTickAllowed=0, negativeTimes=0, postDomainFailures=0, freshFailures=0;
std::string scenario;
void expect(bool condition,const char* id) {
    ++checks;
    if (!condition) { ++failures; std::cout << "FAIL {\"case\":\"" << scenario << "\",\"id\":\"" << id << "\"}\n"; }
}
void required(bool condition,const char* operation) {
    if (!condition) throw std::runtime_error(std::string(operation)+" error="+std::to_string(GetLastError()));
}
struct Dispatch { bool called=false; LONG time=0; std::optional<std::uint64_t> receipt; };
thread_local Dispatch* dispatchCapture=nullptr;
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if (message==messageId) {
        if (dispatchCapture) { dispatchCapture->called=true; dispatchCapture->time=GetMessageTime();
            dispatchCapture->receipt=legionMotionMessageReceiptTick(); }
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
struct Sample {
    std::uint64_t sequence=0, before=0, after=0;
    DWORD messageTime=0; LONG signedTime=0, dispatchTime=0;
    bool window=false, removed=false, usedGet=false, dispatched=false;
    std::optional<std::uint64_t> receipt, normalized, dispatchReceipt;
};
struct Request { bool stop=false, remove=true, useGet=false, route=true; unsigned count=1; };
struct State {
    HANDLE ready=nullptr, command=nullptr, done=nullptr, thread=nullptr; DWORD id=0; HWND window=nullptr;
    std::mutex mutex; Request request; std::vector<Sample> result; std::string error;
};
DWORD WINAPI worker(void* argument) {
    auto& state=*static_cast<State*>(argument); bool registered=false;
    try {
        MSG ignored{}; (void)PeekMessageW(&ignored,nullptr,0,0,PM_NOREMOVE);
        WNDCLASSW definition{}; definition.lpfnWndProc=procedure; definition.hInstance=GetModuleHandleW(nullptr);
        definition.lpszClassName=className;
        required(RegisterClassW(&definition)!=0,"RegisterClassW"); registered=true;
        state.window=CreateWindowExW(0,className,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,definition.hInstance,nullptr);
        required(state.window!=nullptr,"CreateWindowExW hidden message-only window");
        required(IsWindowVisible(state.window)==FALSE,"window remains hidden");
        required(SetEvent(state.ready)!=FALSE,"ready event");
        for (;;) {
            required(WaitForSingleObject(state.command,10000)==WAIT_OBJECT_0,"bounded worker command");
            Request request;
            { std::lock_guard<std::mutex> guard(state.mutex); request=state.request; }
            if (request.stop) break;
            fixtureRouteEnabled=request.route;
            std::vector<Sample> result;
            for (unsigned i=0;i<request.count;++i) {
                MSG message{}; Sample sample; sample.before=GetTickCount64();
                sample.removed=request.remove; sample.usedGet=request.useGet;
                if (request.useGet) {
                    MSG preview{};
                    // One exclusive consumer proves presence before GetMessage.
                    required(PeekMessageW(&preview,nullptr,messageId,messageId,PM_NOREMOVE)!=FALSE,"GetMessage precondition");
                    required(GetMessageW(&message,nullptr,messageId,messageId)>0,"GetMessageW");
                } else required(PeekMessageW(&message,nullptr,messageId,messageId,request.remove?PM_REMOVE:PM_NOREMOVE)!=FALSE,"PeekMessageW");
                sample.signedTime=GetMessageTime(); sample.receipt=legionMotionMessageReceiptTick();
                sample.after=GetTickCount64();
                sample.normalized=ymcc::legion_motion_clock::messageReceiptTick(sample.after,message.time,std::numeric_limits<std::uint64_t>::max());
                sample.sequence=static_cast<std::uint64_t>(message.wParam); sample.messageTime=message.time;
                sample.window=message.hwnd!=nullptr;
                required(message.message==messageId,"private message ID");
                required(message.lParam==static_cast<LPARAM>(sample.sequence^0x13579U),"private payload");
                if (sample.window && sample.removed) {
                    Dispatch capture; dispatchCapture=&capture; (void)DispatchMessageW(&message); dispatchCapture=nullptr;
                    sample.dispatched=capture.called; sample.dispatchTime=capture.time; sample.dispatchReceipt=capture.receipt;
                }
                result.push_back(sample);
            }
            { std::lock_guard<std::mutex> guard(state.mutex); state.result=std::move(result); }
            required(SetEvent(state.done)!=FALSE,"done event");
        }
    } catch (const std::exception& error) {
        { std::lock_guard<std::mutex> guard(state.mutex); state.error=error.what(); }
        (void)SetEvent(state.ready); (void)SetEvent(state.done);
    }
    if (state.window) { (void)DestroyWindow(state.window); state.window=nullptr; }
    if (registered) (void)UnregisterClassW(className,GetModuleHandleW(nullptr));
    return 0;
}
class Queue final {
public:
    State state;
    Queue() {
        state.ready=CreateEventW(nullptr,FALSE,FALSE,nullptr); state.command=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        state.done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        required(state.ready && state.command && state.done,"private events");
        state.thread=CreateThread(nullptr,0,worker,&state,0,&state.id);
        required(state.thread!=nullptr,"private thread");
        required(WaitForSingleObject(state.ready,5000)==WAIT_OBJECT_0,"queue readiness"); checkError();
    }
    ~Queue() {
        if (state.thread) {
            { std::lock_guard<std::mutex> guard(state.mutex); state.request.stop=true; }
            (void)SetEvent(state.command);
            if (WaitForSingleObject(state.thread,5000)!=WAIT_OBJECT_0) {
                std::cerr << "FATAL worker bounded cleanup failed\n"; ExitProcess(2);
            }
            CloseHandle(state.thread);
        }
        if (state.ready) CloseHandle(state.ready); if (state.command) CloseHandle(state.command);
        if (state.done) CloseHandle(state.done);
    }
    void checkError() {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.error.empty()) throw std::runtime_error(state.error);
    }
    std::vector<Sample> take(unsigned count,bool remove,bool useGet,bool route=true) {
        { std::lock_guard<std::mutex> guard(state.mutex); state.request={false,remove,useGet,route,count}; state.result.clear(); }
        required(SetEvent(state.command)!=FALSE,"retrieval request");
        required(WaitForSingleObject(state.done,5000)==WAIT_OBJECT_0,"retrieval completion"); checkError();
        std::lock_guard<std::mutex> guard(state.mutex);
        required(state.result.size()==count,"sample count"); return state.result;
    }
};
struct Post { std::uint64_t sequence=0,before=0,after=0; bool window=false; };
Post post(Queue& queue,bool window) {
    Post sent{++serial,GetTickCount64(),0,window};
    const auto w=static_cast<WPARAM>(sent.sequence), l=static_cast<WPARAM>(sent.sequence^0x13579U);
    required(window?PostMessageW(queue.state.window,messageId,w,static_cast<LPARAM>(l))!=FALSE:
        PostThreadMessageW(queue.state.id,messageId,w,static_cast<LPARAM>(l))!=FALSE,"actual PostMessage/PostThreadMessage");
    sent.after=GetTickCount64(); return sent;
}
void optionalJson(const std::optional<std::uint64_t>& value) { if (value) std::cout << *value; else std::cout << "null"; }
void observe(const char* stage,const Post& sent,const Sample& sample,std::uint64_t cutover=0) {
    ++observations; if (sample.signedTime<0) ++negativeTimes;
    const auto bits=static_cast<std::uint32_t>(sample.signedTime);
    // Strict compatibility check: no tolerance, rebasing or retry for green.
    const bool inPostInterval=sample.normalized && *sample.normalized>=sent.before && *sample.normalized<=sent.after;
    if (!inPostInterval) ++postDomainFailures;
    std::cout << "OBS {\"case\":\"" << scenario << "\",\"stage\":\"" << stage
        << "\",\"route\":\"" << (sent.window?"PostMessage":"PostThreadMessage")
        << "\",\"retrieval\":\"" << (sample.usedGet?"GetMessage":sample.removed?"PeekMessage-remove":"PeekMessage-no-remove")
        << "\",\"sequence\":" << sample.sequence << ",\"postBefore64\":" << sent.before << ",\"postAfter64\":" << sent.after
        << ",\"dequeueBefore64\":" << sample.before << ",\"dequeueAfter64\":" << sample.after
        << ",\"msgTime32\":" << sample.messageTime << ",\"getMessageTimeSigned\":" << sample.signedTime
        << ",\"getMessageTimeBits32\":" << bits << ",\"queueMinusPostAfterLow32\":"
        << (static_cast<std::int64_t>(sample.messageTime)-static_cast<std::int64_t>(static_cast<std::uint32_t>(sent.after))) << ",\"receipt\":";
    optionalJson(sample.receipt); std::cout << ",\"domainReceiptNoAgeLimit\":"; optionalJson(sample.normalized);
    std::cout << ",\"dispatchReceipt\":"; optionalJson(sample.dispatchReceipt);
    std::cout << ",\"strictPostInterval\":" << (inPostInterval?"true":"false") << ",\"epochCutover64\":" << cutover
        << ",\"belongsToEpoch\":" << (sample.receipt && ymcc::legion_motion_clock::belongsToEpoch(*sample.receipt,cutover)?"true":"false") << "}\n";
    expect(sample.sequence==sent.sequence,"queue.sequence"); expect(sample.window==sent.window,"queue.destination");
    expect(bits==sample.messageTime,"queue.MSG-time-equals-GetMessageTime-bits");
    expect(sample.after>=sample.before && sample.before>=sent.after,"queue.processing-follows-post");
    if (sample.removed && sent.window) {
        expect(sample.dispatched,"window.actual-dispatch");
        expect(static_cast<std::uint32_t>(sample.dispatchTime)==sample.messageTime,"window.dispatch-keeps-message-time");
        if (sample.receipt && sample.dispatchReceipt) expect(*sample.receipt==*sample.dispatchReceipt,"window.dispatch-bridge-agrees");
    }
}
void fresh(const Post& sent,const Sample& sample) {
    if (!sample.receipt) ++freshFailures;
    expect(sample.receipt.has_value(),"fresh.actual-production-bridge-accepts");
    expect(sample.normalized && *sample.normalized>=sent.before && *sample.normalized<=sent.after,
        "domain.queue-timestamp-within-GetTickCount64-post-interval");
}
void delayed(Queue& queue) {
    for (const bool window:{false,true}) for (const bool useGet:{false,true}) {
        scenario=std::string("delayed-")+(window?"window-":"thread-")+(useGet?"get":"peek");
        const auto sent=post(queue,window);
        const auto preview=queue.take(1,false,false)[0]; observe("peek-before-delay",sent,preview); fresh(sent,preview);
        Sleep(120);
        const auto removed=queue.take(1,true,useGet)[0]; observe("remove-after-delay",sent,removed); fresh(sent,removed);
        expect(removed.messageTime==preview.messageTime,"delay.queue-time-not-dequeue-time");
        expect(removed.before>=preview.after+100,"delay.actual-queue-residence");
        if (preview.receipt && removed.receipt) expect(*preview.receipt==*removed.receipt,"delay.normalized-receipt-stable");
    }
}
void epoch(Queue& queue) {
    for (const bool window:{false,true}) {
        scenario=window?"epoch-window-peek":"epoch-thread-get";
        const auto old=post(queue,window); Sleep(75);
        const auto cutover=GetTickCount64(); Sleep(30); const auto recent=post(queue,window);
        const auto items=queue.take(2,true,!window);
        observe("old-enqueued-before-cutover",old,items[0],cutover); fresh(old,items[0]);
        observe("new-enqueued-after-cutover",recent,items[1],cutover); fresh(recent,items[1]);
        expect(items[0].receipt && !ymcc::legion_motion_clock::belongsToEpoch(*items[0].receipt,cutover),"epoch.old-queued-message-rejected");
        expect(items[1].receipt && ymcc::legion_motion_clock::belongsToEpoch(*items[1].receipt,cutover),"epoch.new-queued-message-allowed");
    }
}
void stale(Queue& queue) {
    for (const bool window:{false,true}) {
        scenario=window?"stale-window-get":"stale-thread-peek";
        const auto sent=post(queue,window); const auto preview=queue.take(1,false,false)[0];
        observe("fresh-peek",sent,preview); fresh(sent,preview);
        Sleep(static_cast<DWORD>(ymcc::kVirtualOutputSourceWindowMaxAgeMs+120));
        const auto removed=queue.take(1,true,window)[0]; observe("expired-removal",sent,removed);
        expect(removed.messageTime==preview.messageTime,"stale.message-retains-original-time");
        expect(removed.before>=sent.after+ymcc::kVirtualOutputSourceWindowMaxAgeMs,"stale.actual-age-exceeds-production-window");
        expect(!removed.receipt,"stale.production-bridge-rejects");
    }
}
void sameMillisecond(Queue& queue) {
    // Fixed observational sample count. No retry-until-pass or fabricated OS time.
    for (unsigned attempt=0;attempt<24;++attempt) {
        const bool window=(attempt&1)!=0; scenario="same-ms-pair-"+std::to_string(attempt);
        const auto old=post(queue,window); const auto cutover=GetTickCount64(); const auto recent=post(queue,window);
        const auto items=queue.take(2,true,(attempt&2)!=0);
        observe("chronologically-before-cutover",old,items[0],cutover);
        observe("chronologically-after-cutover",recent,items[1],cutover);
        if (items[0].messageTime==items[1].messageTime) ++sameQueuePairs;
        if (items[0].receipt && items[1].receipt && *items[0].receipt==cutover && *items[1].receipt==cutover) {
            ++sameCutoverPairs;
            const bool oldAllowed=ymcc::legion_motion_clock::belongsToEpoch(*items[0].receipt,cutover);
            const bool newAllowed=ymcc::legion_motion_clock::belongsToEpoch(*items[1].receipt,cutover);
            if (!oldAllowed && !newAllowed) ++sameRejectedPairs;
            expect(!oldAllowed && !newAllowed,"same-ms.ambiguous-old-and-new-both-rejected");
        }
        // Advance to the next actual OS tick. No message timestamp is rewritten.
        const auto waitingSince=GetTickCount64();
        while (GetTickCount64()<=cutover && GetTickCount64()-waitingSince<2000) Sleep(1);
        required(GetTickCount64()>cutover,"bounded next provably newer OS tick");
        const auto next=post(queue,window); const auto recovered=queue.take(1,true,(attempt&2)!=0)[0];
        observe("next-provably-newer-tick",next,recovered,cutover); fresh(next,recovered);
        const bool allowed=recovered.receipt && *recovered.receipt>cutover &&
            ymcc::legion_motion_clock::belongsToEpoch(*recovered.receipt,cutover);
        if (allowed) ++nextTickAllowed;
        expect(allowed,"same-ms.next-provably-newer-tick-recovers");
    }
    expect(sameRejectedPairs==sameCutoverPairs,"same-ms.every-observed-equality-pair-rejected");
    expect(nextTickAllowed==24,"same-ms.all-next-tick-samples-recover");
}
void signsAndRoute(Queue& queue) {
    scenario="signed-conversion-supplemental-arithmetic";
    expect(sizeof(LONG)==4 && sizeof(DWORD)==4,"signed.actual-platform-width");
    for (const std::uint32_t bits:{0U,0x7fffffffU,0x80000000U,0xffffffffU}) {
        LONG signedValue=0; std::memcpy(&signedValue,&bits,sizeof(bits));
        const auto converted=static_cast<std::uint32_t>(signedValue);
        expect(converted==bits,"signed.cast-preserves-all-32-bits");
        const std::uint64_t now=(std::uint64_t{1}<<32)+bits+1;
        const auto receipt=ymcc::legion_motion_clock::messageReceiptTick(now,converted,1500);
        expect(receipt && *receipt==now-1,"signed.synthetic-negative-bit-pattern-normalization");
        std::cout << "SIGN {\"actualOsMessage\":false,\"bits\":" << bits << ",\"signedValue\":" << signedValue
            << ",\"converted\":" << converted << "}\n";
    }
    expect(!ymcc::legion_motion_clock::belongsToEpoch(123,123),"same-ms.literal-equality-is-rejected");
    expect(ymcc::legion_motion_clock::belongsToEpoch(124,123),"same-ms.literal-next-tick-is-admitted");
    scenario="actual-bridge-route-disabled";
    const auto sent=post(queue,false); const auto sample=queue.take(1,true,true,false)[0];
    observe("route-disabled",sent,sample); expect(!sample.receipt,"route.actual-extracted-bridge-gate");
}
} // namespace
int main() {
    try {
        { Queue queue; delayed(queue); epoch(queue); stale(queue); sameMillisecond(queue); signsAndRoute(queue); }
        std::cout << "SUMMARY {\"checks\":" << checks << ",\"failures\":" << failures << ",\"observations\":" << observations
            << ",\"sameQueueTimePairs\":" << sameQueuePairs << ",\"sameActualCutoverPairs\":" << sameCutoverPairs
            << ",\"sameActualCutoverRejectedPairs\":" << sameRejectedPairs << ",\"nextTickAllowed\":" << nextTickAllowed
            << ",\"negativeOsTimes\":" << negativeTimes << ",\"strictPostDomainFailures\":" << postDomainFailures
            << ",\"freshBridgeFailures\":" << freshFailures
            << ",\"cleanupComplete\":true,\"hardwareIo\":false,\"actualSleepWake\":false,\"rawInput\":false}\n";
        return failures?1:0;
    } catch (const std::exception& error) { std::cerr << "FATAL " << error.what() << '\n'; return 2; }
}