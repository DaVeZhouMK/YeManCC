// Exact CancelToken / AsyncRequest consumers with an external owner event.
// Only source-built loopback fixtures are contacted; no Steam or devices.
#define main ymccExistingBootstrapGuardCases
#include "decky_sidebar_bootstrap_selftest.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <memory>

namespace inherited_cancel_test {
using J=nlohmann::json;
struct Event {
    HANDLE value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Event(){if(!value)throw std::runtime_error("fixture-event");}
    ~Event(){if(value)CloseHandle(value);}
    Event(const Event&)=delete;
};
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::shared_ptr<ymcc::deckymirror::CancelToken> token(HANDLE external){
#ifdef YMCC_INHERITED_CANCEL_NEGATIVE
    (void)external;
    return std::make_shared<ymcc::deckymirror::CancelToken>();
#else
    return std::make_shared<ymcc::deckymirror::CancelToken>(external);
#endif
}
J silent(const std::string& kind){
    using namespace ymcc::deckymirror;
    LocalHttpFixture server(kind);Event external,entered,finished;
    auto cancellation=token(external.value);
    std::string reason;std::vector<std::weak_ptr<AsyncState>> pending;
    std::thread worker([&]{
        try{
            ymcc::steamlive::HttpHandle session(WinHttpOpen(L"InheritedCancelFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
            ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));
            const auto deadline=GetTickCount64()+2500;
            AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/inherited",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),cancellation,deadline);
            pending.push_back(request.callbackState());
            if(kind=="http-silent")SetEvent(entered.value);
            request.handshake(kind=="ws-silent");
            if(kind=="ws-silent"){
                AsyncRequest socket(WinHttpWebSocketCompleteUpgrade(request.handle(),0),cancellation,deadline,true);
                pending.push_back(socket.callbackState());SetEvent(entered.value);socket.receiveText(100);
            }
        }catch(const std::exception& error){reason=error.what();}
        SetEvent(finished.value);
    });
    const bool accepted=WaitForSingleObject(server.accepted,1200)==WAIT_OBJECT_0;
    const bool actualWait=WaitForSingleObject(entered.value,1200)==WAIT_OBJECT_0;
    const auto started=GetTickCount64();SetEvent(external.value);
    const bool ownerEventStoppedRead=WaitForSingleObject(finished.value,200)==WAIT_OBJECT_0;
    const auto elapsed=GetTickCount64()-started;
    // Cleanup is separate from the measured owner-event assertion.
    // The negative's independent token is released only AFTER observing failure.
    cancellation->cancel();worker.join();
    const auto closeDeadline=GetTickCount64()+1500;
    const auto released=[&]{return std::all_of(pending.begin(),pending.end(),[](const auto& p){return p.expired();});};
    while(!released()&&GetTickCount64()<closeDeadline)Sleep(10);
    const bool passed=accepted&&actualWait&&ownerEventStoppedRead&&reason=="mirror-bootstrap-canceled"&&released();
    return{{"name",kind},{"passed",passed},{"sourceBuiltSilentPeerAccepted",accepted},{"actualPendingPhaseEntered",actualWait},{"ownerEventStoppedRead",ownerEventStoppedRead},{"stopMs",elapsed},{"deadlineMs",200},{"reason",reason},{"actualCallbackOwnersReleased",released()},{"networkDeadlineMs",2500},{"fallbackCancelOccurredAfterMeasurement",!ownerEventStoppedRead}};
}
}
int main(int argc,char** argv){
    using namespace inherited_cancel_test;using ymcc::deckymirror::CancelToken;
    if(argc!=2)return 3;
    const auto output=std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(output.parent_path());
    WSADATA sockets{};if(WSAStartup(MAKEWORD(2,2),&sockets))return 3;
    J cases=J::array();
    const auto record=[&](const char* name,const auto& body){
        try{J result=body();result["name"]=name;cases.push_back(std::move(result));}
        catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"reason",e.what()}});}
    };
    record("duplicated-event-shares-owner-cancel",[]{
        Event source;auto value=token(source.value);const bool distinct=value->event()!=source.value;
        SetEvent(source.value);const bool signaled=value->canceled();
        return J{{"passed",distinct&&signaled},{"ownsDistinctHandle",distinct},{"sameKernelCancellationObserved",signaled}};
    });
    record("duplicate-survives-source-close",[]{
        Event source;HANDLE control=nullptr;
        require(DuplicateHandle(GetCurrentProcess(),source.value,GetCurrentProcess(),&control,SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,0)!=FALSE,"fixture-control-duplicate");
        auto value=std::make_shared<CancelToken>(source.value);
        CloseHandle(source.value);source.value=nullptr;SetEvent(control);
        const bool live=value->canceled();CloseHandle(control);
        return J{{"passed",live},{"sourceClosedBeforeSignal",true}};
    });
    record("invalid-owner-event-rejected",[]{
        unsigned refused=0;for(HANDLE value:{static_cast<HANDLE>(nullptr),INVALID_HANDLE_VALUE}){
            try{CancelToken invalid(value);}catch(const std::runtime_error& error){if(std::string(error.what())=="mirror-cancel-event-duplicate-failed")++refused;}
        }
        return J{{"passed",refused==2},{"rejected",refused}};
    });
    record("repeat-owner-event-duplicates-release-handles",[]{
        DWORD before=0,after=0;require(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"fixture-handle-count");
        for(unsigned i=0;i<100;++i){Event source;CancelToken value(source.value);value.cancel();require(WaitForSingleObject(source.value,0)==WAIT_OBJECT_0,"duplicate-cancel-not-shared");}
        require(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE,"fixture-handle-count");
        return J{{"passed",before==after},{"before",before},{"after",after},{"iterations",100}};
    });
    record("real-http-read-canceled-by-owner-event",[]{return silent("http-silent");});
    record("real-websocket-read-canceled-by-owner-event",[]{return silent("ws-silent");});
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const J& r){return r.at("passed").get<bool>();});
    const bool ok=static_cast<std::size_t>(passed)==cases.size();
    const J result={{"schema","ymcc.inherited-binding-cancel-actual-consumer.v1"},{"passed",ok},{"cases",cases},{"tests",cases.size()},{"passedCount",passed},{"steamOrDeviceActions",0},{"realProductionCancelTokenAndAsyncRequest",true},{"actualLoopbackIO",true},{"parentRelayThreadsAddedToProduct",0},{"pollersAddedToProduct",0},{"originalUncooperativeCallbackGateResolved",false}};
    std::ofstream out(output,std::ios::binary);out<<result.dump(2)<<'\n';out.close();
    std::cout<<"INHERITED_CANCEL cases="<<cases.size()<<" passed="<<passed<<"\n";WSACleanup();return ok?0:2;
}
