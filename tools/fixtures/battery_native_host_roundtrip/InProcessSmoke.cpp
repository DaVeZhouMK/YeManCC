#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include "battery_charge_client.h"
using Json=nlohmann::json;
static std::vector<DWORD> children(){
    std::vector<DWORD> result;const auto snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot observe child processes");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snap,&entry))do{if(entry.th32ParentProcessID==GetCurrentProcessId())result.push_back(entry.th32ProcessID);}while(Process32NextW(snap,&entry));
    CloseHandle(snap);std::sort(result.begin(),result.end());return result;
}
static void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){
 try{
    if(argc!=4)throw std::runtime_error("Expected mode, isolated payload root and state root");
    const std::wstring mode=argv[1],power=argv[2],state=argv[3];const auto before=children();
    const auto loaded=[](){return GetModuleHandleW(L"coreclr.dll")!=nullptr;};
    Json report{{"mode",ymcc::batterycharge::utf8Path(mode)},{"processId",GetCurrentProcessId()},{"hardwareOperations",0}};
    if(mode==L"unloaded-stop"){
        need(!loaded(),"CLR was unexpectedly preloaded");report["shutdown"]=ymcc::batterycharge::shutdown(state);
        need(!ymcc::batterycharge::component().loaded()&&!loaded(),"Stopping an unused domain loaded CLR");
    }else if(mode==L"missing"){
        auto r=ymcc::batterycharge::request(power,state,{{"op","hello"}});need(!r.value("ok",true),"Missing DLL was admitted");report["request"]=r;
        need(!loaded(),"Missing DLL loaded CLR");
    }else if(mode==L"mismatch"){
        auto r=ymcc::batterycharge::request(power,state,{{"op","hello"}});need(!r.value("ok",true),"Mismatched contract admitted");
        need(r.value("reason",std::string{}).find("版本不一致")!=std::string::npos,"Mismatch not diagnosed before dispatch");report["request"]=r;
        report["shutdown"]=ymcc::batterycharge::shutdown(state);
    }else{
        const auto identity=ymcc::batterycharge::componentIdentity(power);report["component"]=identity;
        need(ymcc::batterycharge::matchingHost(identity),"Component/native source contract differs");
        need(identity.value("execution",std::string{})=="in-process"&&identity.value("processId",0u)==GetCurrentProcessId(),"Not running in this exact native process");
        need(identity.value("pollingIntervalMs",0)==60000&&loaded(),"Actual CLR/60-second policy not loaded");
        auto r=ymcc::batterycharge::request(power,state,{{"op","hello"}});need(r.value("ok",false),"Actual native request/managed reply failed");report["request"]=r;
        report["shutdown"]=ymcc::batterycharge::shutdown(state);report["repeatShutdown"]=ymcc::batterycharge::shutdown(state);
        auto late=ymcc::batterycharge::request(power,state,{{"op","hello"}});need(!late.value("ok",true),"Late request revived stopped component");report["lateRequest"]=late;
    }
    need(children()==before,"A helper child process was created");
    need(!std::filesystem::exists(std::filesystem::path(state)/L"battery-charge.settings.json")&&!std::filesystem::exists(std::filesystem::path(state)/L"battery-charge.recovery.json"),"Identity-only operation created charge policy/journal");
    report["childProcessesCreated"]=0;report["status"]="PASS";std::cout<<report.dump(2)<<'\n';return 0;
 }catch(const std::exception& error){std::cout<<Json{{"status","FAIL"},{"error",error.what()}}.dump(2)<<'\n';return 1;}
}
