#define _WIN32_WINNT 0x0A00
#define NOMINMAX
#include "../native/sensor_choice.h"
#include "../native/sensor_sta_wait.h"
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
using namespace ymcc::sensors;
struct Sensor {
    int id=0;Sensor(std::nullptr_t=nullptr){}explicit Sensor(int n):id(n){}explicit operator bool()const{return id!=0;}
};
struct AsyncState {std::atomic<winrt::Windows::Foundation::AsyncStatus> status{winrt::Windows::Foundation::AsyncStatus::Started};std::atomic<unsigned> cancels{0},results{0};};
struct Operation {
    std::shared_ptr<AsyncState> state=std::make_shared<AsyncState>();
    auto Status()const{return state->status.load();}
    void Cancel(){++state->cancels;state->status=winrt::Windows::Foundation::AsyncStatus::Canceled;}
    int GetResults(){++state->results;if(Status()!=winrt::Windows::Foundation::AsyncStatus::Completed)throw winrt::hresult_error(E_FAIL);return 42;}
};
static unsigned messages=0;
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l){if(message==WM_APP+1){++messages;return 0;}return DefWindowProcW(window,message,w,l);}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    unsigned count=0,failed=0;auto check=[&](const char* name,bool ok){++count;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    ChoiceReceipt receipt;unsigned enumerates=0,opens=0;
    auto selected=chooseAvailable<Sensor>([]{return Sensor(1);},[&]{++enumerates;return std::vector<Candidate>{{"other",true}};},[&](const auto&){++opens;return Sensor(2);},receipt);
    check("successful 258V/default path never re-enumerates or replaces sensor",selected.id==1&&receipt.usedDefault&&enumerates==0&&opens==0);
    receipt=ChoiceReceipt{};opens=0;
    selected=chooseAvailable<Sensor>([]{return Sensor{};},[]{return std::vector<Candidate>{{"disabled",false},{"enabled",true}};},[&](const std::string& id){++opens;return Sensor(id=="enabled"?2:3);},receipt);
    check("no default can select a unique enabled nondefault WinRT sensor",selected.id==2&&!receipt.usedDefault&&receipt.opened==1&&opens==1);
    receipt=ChoiceReceipt{};
    selected=chooseAvailable<Sensor>([]{return Sensor{};},[]{return std::vector<Candidate>{};},[](const auto&){return Sensor(1);},receipt);
    check("G3E typed-enumeration empty remains absent for separate COM fallback",!selected&&receipt.enumerated==0);
    receipt=ChoiceReceipt{};
    selected=chooseAvailable<Sensor>([]{return Sensor{};},[]{return std::vector<Candidate>{{"one",true},{"two",true}};},[](const auto&){return Sensor(1);},receipt);
    check("multiple valid sensors do not silently choose the first provider",!selected&&receipt.ambiguous&&receipt.opened==2);
    receipt=ChoiceReceipt{};
    selected=chooseAvailable<Sensor>([]{throw 1;return Sensor{};},[]{return std::vector<Candidate>{{"one",true}};},[](const auto&){return Sensor(9);},receipt);
    check("default failure can still use an enabled readable typed sensor",selected.id==9&&receipt.errors==1);
    receipt=ChoiceReceipt{};
    selected=chooseAvailable<Sensor>([]{return Sensor{};},[]{throw 1;return std::vector<Candidate>{};},[](const auto&){return Sensor(1);},receipt);
    check("enumeration failure returns absence, not invented sensor",!selected&&receipt.errors==1);
    receipt=ChoiceReceipt{};
    selected=chooseAvailable<Sensor>([]{return Sensor{};},[]{return std::vector<Candidate>{{"denied",true},{"working",true}};},[](const std::string& id){if(id=="denied")throw 1;return Sensor(7);},receipt);
    check("unreadable candidate cannot displace the unique readable candidate",selected.id==7&&receipt.opened==1&&receipt.errors==1);
    WNDCLASSW klass{};klass.hInstance=GetModuleHandleW(nullptr);klass.lpfnWndProc=procedure;klass.lpszClassName=L"YmccSensorSTAMessagePumpSelftest";
    RegisterClassW(&klass);HWND window=CreateWindowW(klass.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,klass.hInstance,nullptr);
    check("message-only STA fixture creates no interactive window",window!=nullptr);
    {
        Operation operation;PostMessageW(window,WM_APP+1,0,0);
        std::thread complete([state=operation.state]{Sleep(40);state->status=winrt::Windows::Foundation::AsyncStatus::Completed;});
        const int result=getStaResults(operation,GetTickCount64()+1000,[]{return false;});complete.join();
        check("actual bounded STA wait only gets results after completion",result==42&&operation.state->results==1&&operation.state->cancels==0);
        check("actual bounded wait dispatches STA messages rather than blocking get",messages==1);
    }
    {
        Operation operation;const auto started=GetTickCount64();bool timeout=false;
        try{(void)getStaResults(operation,started+35,[]{return false;});}catch(const winrt::hresult_error& error){timeout=error.code()==HRESULT_FROM_WIN32(ERROR_TIMEOUT);}
        check("hung async operation times out and is cancelled within budget",timeout&&operation.state->cancels==1&&operation.state->results==0&&GetTickCount64()-started<500);
    }
    {
        Operation operation;bool cancelled=false;
        try{(void)getStaResults(operation,GetTickCount64()+1000,[]{return true;});}catch(const winrt::hresult_error& error){cancelled=error.code()==HRESULT_FROM_WIN32(ERROR_CANCELLED);}
        check("stop/suspend cancellation cannot create a late typed sensor",cancelled&&operation.state->cancels==1&&operation.state->results==0);
    }
    {
        Operation operation;operation.state->status=winrt::Windows::Foundation::AsyncStatus::Error;bool error=false;
        try{(void)getStaResults(operation,GetTickCount64()+1000,[]{return false;});}catch(const winrt::hresult_error&){error=true;}
        check("async error propagates to typed selection failure",error&&operation.state->results==1);
    }
    DestroyWindow(window);UnregisterClassW(klass.lpszClassName,klass.hInstance);CoUninitialize();
    std::cout<<"cases="<<count<<" failed="<<failed<<"\n";return failed?3:0;
}
