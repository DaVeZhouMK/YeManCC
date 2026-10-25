// Live transport smoke: permitted only on a host without a battery; sends GET only.
#include "../native/battery_charge_client.h"
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
int wmain(int argc,wchar_t** argv){
    // Copied under a temporary battery-host directory to test early helper exit.
    if(argc>1&&std::wstring(argv[1])==L"--parent")return 73;
    using Json=ymcc::batterycharge::Json;
    if(argc==4&&std::wstring(argv[1])==L"--expect-early-exit"){
        auto result=ymcc::batterycharge::request(argv[2],argv[3],{{"op","get"}});
        if(result.value("ok",true)||result.value("reason","").find("73")==std::string::npos)throw std::runtime_error("early helper exit not diagnosed");
        std::cout<<"BATTERY_HELPER_EARLY_EXIT_OK: code 73 returned without waiting for pipe timeout\n";return 0;
    }
    const auto fence=std::string(64,'A');
    for(const Json& target:std::vector<Json>{-1,101,80.5,"80",true,nullptr,Json::array(),Json::object(),Json::parse("18446744073709551615")})
        if(ymcc::batterycharge::validSetRequest({{"target",target},{"fence",fence}}))throw std::runtime_error("invalid target accepted");
    for(const Json& bad:std::vector<Json>{nullptr,Json::array(),{{"target",80}},{{"fence",fence}},{{"target",80},{"fence",std::string(64,'Z')}},{{"target",80},{"fence",std::string(63,'A')}}})
        if(ymcc::batterycharge::validSetRequest(bad))throw std::runtime_error("invalid request accepted");
    if(!ymcc::batterycharge::validSetRequest({{"target",80},{"fence",fence}}))throw std::runtime_error("valid target rejected");
    std::cout<<"BATTERY_INPUT_VALIDATION_OK: invalid types, ranges, overflow and fences rejected\n";
    if(argc==2&&std::wstring(argv[1])==L"--input-only")return 0;
    const bool expectOld=argc==4&&std::wstring(argv[1])==L"--expect-contract-mismatch";
    if(argc!=3&&!expectOld)return 2;
    SYSTEM_POWER_STATUS power{};
    if(!GetSystemPowerStatus(&power)||power.BatteryFlag==255||(power.BatteryFlag&128)==0){std::cout<<"SKIP: negative transport smoke requires a battery-less host\n";return 3;}
    for(const auto& value:std::vector<std::wstring>{L"C:\\folder with space\\",L"C:\\quote\"part\\",L"plain"}){
        auto command=L"fixture "+ymcc::batterycharge::quoted(value);int count=0;auto parts=CommandLineToArgvW(command.c_str(),&count);
        bool good=parts&&count==2&&parts[1]==value;if(parts)LocalFree(parts);if(!good)throw std::runtime_error("argument quoting failed");
    }
    if(expectOld){
        // Real old helper, not a mocked response. This fixture is restricted to a battery-less desktop.
        auto result=ymcc::batterycharge::request(argv[2],argv[3],{{"op","set"},{"target",80},{"fence",fence}});
        if(result.value("ok",true)||result.value("reason","").find("版本不一致")==std::string::npos)throw std::runtime_error("old helper not diagnosed before set");
        if(std::filesystem::exists(std::wstring(argv[3])+L"\\battery-charge.settings.json")||std::filesystem::exists(std::wstring(argv[3])+L"\\battery-charge.recovery.json"))throw std::runtime_error("old helper received a mutation");
        std::cout<<"BATTERY_REAL_OLD_HELPER_REJECTED: no SET sent\n";return 0;
    }
    auto result=ymcc::batterycharge::request(argv[1],argv[2],{{"op","get"}});
    std::cout<<result.dump()<<"\n";
    if(result.value("model","").empty()||result.value("supported",true)||result.value("batteryPresent",true)||result.value("schema",0)!=1)throw std::runtime_error("host did not return the unsupported desktop identity");
    if(std::filesystem::exists(std::wstring(argv[2])+L"\\battery-charge.settings.json")||std::filesystem::exists(std::wstring(argv[2])+L"\\battery-charge.recovery.json"))throw std::runtime_error("read created mutable battery state");
    {
        const auto pipeName=L"\\\\.\\pipe\\YMCC.BatteryCharge."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(ymcc::batterycharge::parentStamp());
        ymcc::batterycharge::Handle pipe;const auto until=GetTickCount64()+3000;
        do{pipe.h=CreateFileW(pipeName.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(pipe.h!=INVALID_HANDLE_VALUE)break;Sleep(20);}while(GetTickCount64()<until);
        if(pipe.h==INVALID_HANDLE_VALUE)throw std::runtime_error("raw old-client fixture connection failed");
        Json detail=Json::object();auto response=ymcc::batterycharge::exchange(pipe.h,{{"op","set"},{"target",80},{"fence",fence}},until,detail);
        if(response.value("error","").find("版本不一致")==std::string::npos)throw std::runtime_error("host accepted an unbound client");
        std::cout<<"BATTERY_REAL_OLD_CLIENT_REJECTED: new host rejected SET before dispatch\n";
    }
    const auto log=std::filesystem::path(argv[2])/L"battery-charge-client.log";
    std::ifstream diagnostics(log);std::string line;bool resultLogged=false;
    while(std::getline(diagnostics,line)){auto row=Json::parse(line);if(row.value("event","")=="request-result"&&row.at("detail").value("model","")==result.value("model",""))resultLogged=true;}
    if(!resultLogged)throw std::runtime_error("transport result not recorded for export");
    std::cout<<"BATTERY_NATIVE_PIPE_OK: real helper GET, quoted paths, no battery writes or saved state\n";
}
