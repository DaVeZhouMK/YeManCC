#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shldisp.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
namespace forbidden {
ULONGLONG tick();HANDLE open(DWORD,BOOL,LPCWSTR);DWORD error();HRESULT initialize(LPVOID,DWORD);
BOOL peek(LPMSG,HWND,UINT,UINT,UINT);BOOL signal(HANDLE);DWORD wait(HANDLE,DWORD);
HRESULT create(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID*);void uninitialize();BOOL close(HANDLE);
LPWSTR* parse(LPCWSTR,int*);HLOCAL free(HLOCAL);
}
#define GetTickCount64 forbidden::tick
#define OpenEventW forbidden::open
#define GetLastError forbidden::error
#define CoInitializeEx forbidden::initialize
#define PeekMessageW forbidden::peek
#define SetEvent forbidden::signal
#define WaitForSingleObject forbidden::wait
#define CoCreateInstance forbidden::create
#define CoUninitialize forbidden::uninitialize
#define CloseHandle forbidden::close
#define CommandLineToArgvW forbidden::parse
#define LocalFree forbidden::free
#include "../native/task_view_worker.h"
using json=nlohmann::json;
namespace worker=ymcc::taskView::worker;
namespace {
unsigned checks=0,cases=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct FakeOps {
    unsigned scenario=0;DWORD lastError=ERROR_ACCESS_DENIED;
    ULONGLONG clock=100;unsigned leaseMode=0,powerEpoch=1;bool callerCancelled=false;DWORD lastWaitBudget=0;
    ULONGLONG now()const noexcept{return clock;}
    unsigned live=0,initializes=0,uninitializes=0,queues=0,signals=0,waits=0,shells=0;
    std::vector<std::string> events;
    HANDLE openReady(const wchar_t*)noexcept{events.push_back("open-ready");if(scenario==1)return nullptr;++live;return reinterpret_cast<HANDLE>(1);}
    HANDLE openExecute(const wchar_t*)noexcept{events.push_back("open-executeNameTest");if(scenario==2)return nullptr;++live;return reinterpret_cast<HANDLE>(2);}
    DWORD error()noexcept{return lastError;}
    HRESULT initialize()noexcept{++initializes;events.push_back("initialize");return scenario==3?RPC_E_CHANGED_MODE:scenario==4?S_FALSE:S_OK;}
    void initializeQueue()noexcept{++queues;events.push_back("queue");}
    bool signalReady(HANDLE)noexcept{++signals;events.push_back("readyNameTest");return scenario!=5;}
    DWORD waitExecute(HANDLE)noexcept{if(leaseMode==1)clock=4501;if(leaseMode==2){clock=150;powerEpoch=2;}if(leaseMode==3){clock=150;callerCancelled=true;}++waits;events.push_back("wait-executeNameTest");return scenario==6?WAIT_TIMEOUT:scenario==7?WAIT_FAILED:scenario==8?WAIT_ABANDONED_0:WAIT_OBJECT_0;}
    HRESULT switchWindows()noexcept{++shells;events.push_back("shell");return scenario==9?E_ACCESSDENIED:scenario==10?E_UNEXPECTED:scenario==11?S_FALSE:S_OK;}
    DWORD waitExecute(HANDLE h,DWORD budget)noexcept{lastWaitBudget=budget;return waitExecute(h);}
    HRESULT switchWindows(ULONGLONG)noexcept{return switchWindows();}
    void uninitialize()noexcept{++uninitializes;events.push_back("uninitialize");}
    void close(HANDLE)noexcept{--live;events.push_back("close");}
};
constexpr wchar_t readyNameTest[]=L"Local\\YeManTaskView-123-456-7-ready",executeNameTest[]=L"Local\\YeManTaskView-123-456-7-execute";

template<class Ops> HRESULT runWithDeadline(Ops& ops, ULONGLONG deadline) {
    if constexpr(requires { worker::run(ops,readyNameTest,executeNameTest,deadline); })
        return worker::run(ops,readyNameTest,executeNameTest,deadline);
    else return worker::run(ops,readyNameTest,executeNameTest);
}
void leaseWindow(){
    auto observations=json::array();unsigned expiredShells=999;
    for(unsigned mode=1;mode<=3;++mode){
        FakeOps ops;ops.leaseMode=mode;
        const auto hr=runWithDeadline(ops,4500);
        observations.push_back({{"case",mode==1?"execute-consumed-after-original-deadline":mode==2?"power-invalidated-before-worker-runs":"caller-cancelled-before-worker-runs"},
            {"absoluteDeadline",4500},{"clockAfterExecuteWait",ops.clock},{"powerEpoch",ops.powerEpoch},
            {"callerCancelled",ops.callerCancelled},{"shellCalls",ops.shells},{"liveHandles",ops.live},
            {"hresult",static_cast<unsigned long>(hr)},
            {"status",mode==1?(ops.shells?"FAIL-STILL-EXECUTES":"DEADLINE-REJECTED"):"OPEN-NO-CROSS-PROCESS-REVOCATION"}});
        check(ops.live==0,"delayed worker leaked handles");
        if(mode==1)expiredShells=ops.shells;
    }
    std::cerr<<"LEASE_OBSERVATIONS "<<observations.dump()<<'\n';
    check(expiredShells==0,"worker called Shell after the original absolute deadline");
    ++cases;
}

void argumentMatrix(){
    for(const auto text:{L"",L"0",L"-1",L"1a",L" 1",L"1 ",L"18446744073709551616",L"999999999999999999999"}){
        ULONGLONG value=0;check(!worker::parseDeadline(text,value),"bad deadline admitted");++cases;
    }
    for(const auto text:{L"1",L"0001",L"4500",L"18446744073709551615"}){
        ULONGLONG value=0;check(worker::parseDeadline(text,value)&&value!=0,"valid absolute deadline rejected");++cases;
    }
    {FakeOps ops;ops.clock=4500;check(worker::run(ops,readyNameTest,executeNameTest,4500)==HRESULT_FROM_WIN32(ERROR_TIMEOUT)&&ops.events.empty(),"expired worker did IO before rejecting deadline");++cases;}
    {FakeOps ops;ops.clock=4499;check(SUCCEEDED(worker::run(ops,readyNameTest,executeNameTest,4500))&&ops.shells==1&&ops.lastWaitBudget==1,"worker minted a fresh relative wait instead of remaining original deadline");++cases;}
    check(worker::validEventPair(readyNameTest,executeNameTest),"valid pair rejected");
    const std::vector<std::pair<std::wstring,std::wstring>> invalid={
        {L"",L""},{readyNameTest,readyNameTest},{executeNameTest,readyNameTest},{readyNameTest,L"Local\\YeManTaskView-123-456-8-execute"},
        {L"Global\\YeManTaskView-123-456-7-ready",executeNameTest},{L"Local\\YeManTaskView-123-456-ready",executeNameTest},
        {L"Local\\YeManTaskView-123-456-7-8-ready",executeNameTest},{L"Local\\YeManTaskView-a-456-7-ready",executeNameTest},
        {L"Local\\YeManTaskView--456-7-ready",executeNameTest},{L"Local\\YeManTaskView-123-456--ready",executeNameTest},
        {L"Local\\YeManTaskView-123-456-7-ready ",executeNameTest},{std::wstring(1000,L'1'),executeNameTest}};
    for(const auto& pair:invalid){check(!worker::validEventPair(pair.first,pair.second),"invalid event pair admitted");++cases;}
    const wchar_t* valid[]={L"G:\\folder with spaces\\YeManCC.exe",worker::mode,L"4500",readyNameTest,executeNameTest};
    for(int argc:{0,1,2,3,4,5}){
        FakeOps ops;int exit=99;const bool handled=worker::tryRunArgs(argc,valid,ops,exit);
        check(handled==(argc>=2),"worker invocation fell through into normal startup");
        check(argc==5?exit==0&&ops.shells==1:ops.events.empty(),"malformed arguments reached worker operations");++cases;
    }
    for(const auto* arg:{L"--dev",L"--minimized",L"--battery-runtime-identity",L"--ymcc-task-view-worker=1",L"--ymcc-task-view-worker-extra"}){
        const wchar_t* argv[]={L"YeManCC.exe",arg,L"4500",readyNameTest,executeNameTest};FakeOps ops;int exit=99;
        const bool handled=worker::tryRunArgs(5,argv,ops,exit);
        const bool reserved=std::wstring_view(arg).starts_with(worker::mode);
        check(handled==reserved,"normal launch or reserved malformed mode misclassified");
        check(ops.events.empty(),"invalid mode performed any IO");if(reserved)check(exit==static_cast<int>(E_INVALIDARG),"reserved mode launched full application");++cases;
    }
}
void ioMatrix(){
    for(unsigned repeat=0;repeat<200;++repeat)for(unsigned scenario=0;scenario<12;++scenario){
        FakeOps ops;ops.scenario=scenario;
        if(repeat%2==0)ops.lastError=0;
        const auto result=worker::run(ops,readyNameTest,executeNameTest,4500);
        const bool success=scenario==0||scenario==4||scenario==11;
        check(SUCCEEDED(result)==success,"HRESULT outcome changed");check(ops.live==0,"event handles leaked");
        check(ops.shells==unsigned(scenario==0||scenario==4||scenario>=9),"Shell called before executeNameTest or called more than once");
        check(ops.uninitializes==unsigned(scenario!=1&&scenario!=2&&scenario!=3),"COM initialization not balanced");
        if(ops.shells){auto q=std::find(ops.events.begin(),ops.events.end(),"queue"),r=std::find(ops.events.begin(),ops.events.end(),"readyNameTest"),w=std::find(ops.events.begin(),ops.events.end(),"wait-executeNameTest"),s=std::find(ops.events.begin(),ops.events.end(),"shell");check(q<r&&r<w&&w<s,"worker order not queue/readyNameTest/executeNameTest/Shell");}
        if(scenario==6)check(result==HRESULT_FROM_WIN32(ERROR_TIMEOUT),"timeout code lost");++cases;
    }
}
}
int main(int argc,char** argv){try{const bool directed=argc==2&&std::string(argv[1])=="--lease-window";if(!directed){argumentMatrix();ioMatrix();}leaseWindow();std::cout<<json{{"pass",true},{"checks",checks},{"cases",cases},{"actualOSApisInvoked",false},{"powerAndCancellationLeaseProtected",false},{"scope","Actual same-executable worker templates, argument parsing and all I/O outcomes replaced with memory fixtures"}}.dump()<<'\n';return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
