// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Real Runtime + production WinHTTP cancel consumer. Own source-built children
// and a silent loopback fixture only; no installed Steam/Loader/YMCC is queried.
#define main ymccBootstrapGuardSuite
#include "decky_sidebar_bootstrap_selftest.cpp"
#undef main
#include "../native/decky_sidebar_runtime.h"
#include <filesystem>
#include <fstream>
int main(int argc,char** argv){
    if(argc==1){Sleep(30000);return 0;} // Runtime launches only this inert image.
    const std::filesystem::path output=std::filesystem::absolute(argv[1]);
    wchar_t image[32768];GetModuleFileNameW(nullptr,image,32768);
    if(output.parent_path()!=std::filesystem::path(image).parent_path())return 2;
    std::filesystem::create_directories(output);WSADATA sockets{};
    if(WSAStartup(MAKEWORD(2,2),&sockets))return 3;
    nlohmann::json results=nlohmann::json::array();
    try{for(const auto& kind:{std::string("http-silent"),std::string("ws-silent")}){
        LocalHttpFixture server(kind);ymcc::deckymirror::CancelSignal cancellation;
        ymcc::deckysidebar::Runtime runtime;std::atomic<bool> block{false},returned{false};
        std::string failure;std::vector<std::weak_ptr<ymcc::deckymirror::AsyncState>> pending;
        ymcc::deckysidebar::Config config;config.executable=image;config.home=(output/kind/L"home").wstring();
        std::filesystem::create_directories(config.home);config.preflight=[]{return std::string{};};
        config.beginPending=[&]{cancellation.renew();};config.cancelPending=[&]{cancellation.cancel();};
        config.refreshBinding=[&]()->std::string{
            if(!block)return {};
            try{
                using namespace ymcc::deckymirror;
                ymcc::steamlive::HttpHandle session(WinHttpOpen(L"RuntimeCancelFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
                ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));
                const auto token=cancellation.current();const auto deadline=GetTickCount64()+7000;
                AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/runtime",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),token,deadline);
                pending.push_back(request.callbackState());request.handshake(kind=="ws-silent");
                if(kind=="ws-silent"){
                    AsyncRequest socket(WinHttpWebSocketCompleteUpgrade(request.handle(),0),token,deadline,true);
                    pending.push_back(socket.callbackState());socket.receiveText(100);
                }
            }catch(const std::exception& error){failure=error.what();}
            returned=true;return failure;
        };
        runtime.initialize(std::move(config));runtime.observeSteam(1,true);runtime.setEnabled(true);
        const auto startDeadline=GetTickCount64()+4000;
        while(!runtime.snapshot().pid&&GetTickCount64()<startDeadline)Sleep(5);
        const auto pid=runtime.snapshot().pid;if(!pid)throw std::runtime_error("inert child did not start");
        HANDLE child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!child)throw std::runtime_error("owned child handle unavailable");
        wchar_t observed[32768];DWORD chars=32768;
        if(!QueryFullProcessImageNameW(child,0,observed,&chars)||_wcsicmp(observed,image))throw std::runtime_error("child identity mismatch");
        block=true;runtime.requestBindingRefresh(2);
        if(WaitForSingleObject(server.accepted,3000)!=WAIT_OBJECT_0)throw std::runtime_error("production request did not reach silent fixture");
        Sleep(40);const auto stopAt=GetTickCount64();runtime.shutdown();const auto elapsed=GetTickCount64()-stopAt;
        const bool childExited=WaitForSingleObject(child,1000)==WAIT_OBJECT_0;CloseHandle(child);
        const auto closed=[&]{return std::all_of(pending.begin(),pending.end(),[](const auto& state){return state.expired();});};
        const auto closeDeadline=GetTickCount64()+1500;while(!closed()&&GetTickCount64()<closeDeadline)Sleep(5);
        const bool ok=elapsed<1500&&returned&&failure=="mirror-bootstrap-canceled"&&childExited&&closed()&&!runtime.snapshot().enabled&&runtime.snapshot().pid==0;
        runtime.requestBindingRefresh(3);runtime.requestTransportRecovery();runtime.requestLaunchAdmissionRefresh();runtime.shutdown();
        const bool stayedOff=!runtime.snapshot().enabled&&runtime.snapshot().pid==0;
        results.push_back({{"kind",kind},{"ok",ok&&stayedOff},{"shutdownMs",elapsed},{"bindingReturned",returned.load()},{"bindingReason",failure},{"ownedChildExitObserved",childExited},{"requestCallbackOwnersReleased",closed()},{"offSurvivedLateRequests",stayedOff},{"deadlineMs",1500}});
        if(!ok||!stayedOff)throw std::runtime_error("real network cancellation lifecycle did not finish within bound");
    }
    const nlohmann::json result={{"ok",true},{"cases",results},{"realSteamActions",0},{"officialLoaderExecuted",false},{"scope","Actual Runtime plus production AsyncRequest/CancelSignal and silent TCP/WebSocket peers; owned inert child exit, callback-buffer release and OFF late-request checks"}};
    std::ofstream(output.parent_path()/L"binding-cancel-results.json")<<result.dump(2);std::cout<<result.dump()<<'\n';WSACleanup();return 0;
    }catch(const std::exception& error){std::ofstream(output.parent_path()/L"binding-cancel-results.json")<<nlohmann::json{{"ok",false},{"cases",results},{"error",error.what()}}.dump(2);std::cerr<<error.what()<<'\n';WSACleanup();return 1;}
}
