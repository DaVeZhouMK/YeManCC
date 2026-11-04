#pragma once
// In-process component boundary. The existing charge/display adapters run inside YeManCC.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include "json.hpp"
#include "battery_host_contract.h"
#include "battery_managed_runtime.h"
#include <atomic>
namespace ymcc::batterycharge {
using Json=nlohmann::json;
inline std::timed_mutex& lock(){static auto* m=new std::timed_mutex;return *m;}
inline std::atomic<bool>& stopping(){static auto* value=new std::atomic<bool>{false};return *value;}
inline managed::Component& component(){static auto* value=new managed::Component;return *value;}
inline void seal() noexcept {stopping().store(true,std::memory_order_release);}
inline bool hasBattery(){SYSTEM_POWER_STATUS s{};return GetSystemPowerStatus(&s)&&s.BatteryFlag!=255&&(s.BatteryFlag&128)==0;}
inline Json failure(const char* reason){return {{"schema",1},{"ok",false},{"supported",false},{"batteryPresent",hasBattery()},{"model",""},{"family",""},{"kind","unsupported"},{"choices",Json::array()},{"currentLimit",nullptr},{"requestedLimit",nullptr},{"managed",false},{"verification","unknown"},{"fence",""},{"reason",reason}};}
// Validate before loading the component; route-specific choices stay authoritative.
inline bool validSetRequest(const Json& args){
    if(!args.is_object()||!args.contains("target")||!args["target"].is_number_integer()||!args.contains("fence")||!args["fence"].is_string())return false;
    const auto& target=args["target"];
    if(target.is_number_unsigned()){if(target.get<unsigned long long>()>100)return false;}
    else {const auto value=target.get<long long>();if(value<0||value>100)return false;}
    const auto& fence=args["fence"].get_ref<const std::string&>();
    if(fence.size()!=64)return false;
    for(const auto ch:fence)if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')||(ch>='A'&&ch<='F')))return false;
    return true;
}
inline void trace(const std::wstring& stateRoot,const char* event,Json detail) noexcept {
    try {
        const auto root=std::filesystem::path(stateRoot);std::filesystem::create_directories(root);
        const auto path=root/L"battery-charge-client.log";
        if(std::filesystem::exists(path)&&std::filesystem::file_size(path)>=256*1024) {
            std::error_code ec;auto previous=path;previous+=L".1";std::filesystem::remove(previous,ec);std::filesystem::rename(path,previous,ec);
        }
        SYSTEMTIME now{};GetLocalTime(&now);char time[40]{};
        std::snprintf(time,sizeof(time),"%04u-%02u-%02uT%02u:%02u:%02u.%03u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);
        std::ofstream stream(path,std::ios::app|std::ios::binary);
        stream<<Json{{"schema",1},{"time",time},{"pid",GetCurrentProcessId()},{"build",__DATE__ " " __TIME__},{"event",event},{"detail",std::move(detail)}}.dump()<<"\n";
    } catch(...) { /* Diagnostics never change transport behavior. */ }
}
inline std::string utf8Path(const std::wstring& value){auto text=std::filesystem::path(value).u8string();return std::string(text.begin(),text.end());}
inline Json runtimeIdentity(const std::wstring& powerRoot){
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    return {{"protocol",kHostProtocol},{"sourceId",kHostSourceId},{"implementation",kHostImplementation},
        {"nativePath",utf8Path(path)},{"powerRoot",utf8Path(powerRoot)},{"componentPath",utf8Path(powerRoot+L"\\battery-host\\YeManBatteryHost.dll")},{"execution","in-process"},{"pollingIntervalMs",60000}};
}
inline bool matchingHost(const Json& hello){
    return hello.is_object()&&hello.value("protocol",0)==kHostProtocol&&hello.value("sourceId","")==kHostSourceId&&hello.value("component","")=="YeManBatteryHost";
}
// Pure component identity. It deliberately never creates controllers or settings.
inline Json componentIdentity(const std::wstring& powerRoot){
    std::lock_guard<std::timed_mutex> serial(lock());
    if(stopping().load(std::memory_order_acquire))throw std::runtime_error("充电组件正在退出");
    component().load(powerRoot);
    return component().call({{"request",{{"op","hello"}}}});
}
inline Json requestCore(const std::wstring& powerRoot,const std::wstring& stateRoot,const Json& input,Json& detail){
    try{
        if(stopping().load(std::memory_order_acquire))return failure("充电组件正在退出");
        detail["runtime"]=runtimeIdentity(powerRoot);detail["stage"]="load-in-process";
        component().load(powerRoot);
        if(stopping().load(std::memory_order_acquire))return failure("充电组件正在退出");
        const auto envelope=[&](Json request){return Json{{"powerRoot",utf8Path(powerRoot)},{"stateRoot",utf8Path(stateRoot)},{"request",std::move(request)}};};
        detail["phase"]="handshake";
        const auto hello=component().call(envelope({{"op","hello"}}));detail["hostIdentity"]=hello;
        if(!matchingHost(hello))return failure("充电组件与主程序版本不一致，请退出 YMCC 后更新完整程序包");
        if(hello.value("execution",std::string{})!="in-process"||hello.value("processId",0u)!=GetCurrentProcessId())return failure("充电组件未绑定当前 YMCC 进程");
        detail["phase"]="operation";detail["stage"]="in-process-dispatch";
        auto bound=input;bound["protocol"]=kHostProtocol;bound["sourceId"]=kHostSourceId;
        auto result=component().call(envelope(std::move(bound)));
        if(result.contains("error"))return failure(result.at("error").get<std::string>().c_str());return result;
    }catch(const std::exception& e){return failure(e.what());}
    catch(...){return failure("充电控制暂不可用，请重新读取");}
}
// Terminal admission is closed before waiting on a running operation. Closing
// never loads a component, recreates a domain or deletes a recovery journal.
inline Json shutdown(const std::wstring& stateRoot){
    seal();
    std::unique_lock<std::timed_mutex> serial(lock(),std::defer_lock);
    if(!serial.try_lock_for(std::chrono::seconds(15))){Json result{{"ok",false},{"reason","充电操作尚未结束，退出恢复未确认"},{"recoveryJournalPreserved",true}};trace(stateRoot,"in-process-stop-retained",result);return result;}
    try{
        if(!component().loaded())return {{"ok",true},{"execution","in-process"},{"loaded",false}};
        const auto result=component().call({{"request",{{"op","shutdown"}}}});
        trace(stateRoot,"in-process-stop",result);return result;
    }catch(const std::exception& e){Json result{{"ok",false},{"reason",e.what()},{"recoveryJournalPreserved",true}};trace(stateRoot,"in-process-stop-failed",result);return result;}
}
inline Json request(const std::wstring& powerRoot,const std::wstring& stateRoot,const Json& input){
    if(stopping().load(std::memory_order_acquire))return failure("充电组件正在退出");
    std::unique_lock<std::timed_mutex> serial(lock(),std::defer_lock);
    if(!serial.try_lock_for(std::chrono::seconds(30)))return failure("充电控制正在处理其他请求，请稍后重试");
    const auto start=GetTickCount64();
    Json detail{{"operation",input.value("op","")}};if(input.contains("target"))detail["target"]=input["target"];
    trace(stateRoot,"request-start",detail);
    auto result=requestCore(powerRoot,stateRoot,input,detail);detail["elapsedMs"]=GetTickCount64()-start;
    for(const auto* key:{"ok","supported","batteryPresent","model","family","currentLimit","requestedLimit","managed","verification","reason"})if(result.contains(key))detail[key]=result[key];
    trace(stateRoot,"request-result",detail);return result;
}
inline void restoreAtStartup(const std::wstring& powerRoot,const std::wstring& stateRoot){
    // No preference/recovery file means zero component loading or hardware work.
    if(!std::filesystem::exists(stateRoot+L"\\battery-charge.settings.json")&&!std::filesystem::exists(stateRoot+L"\\battery-charge.recovery.json"))return;
    std::thread([powerRoot,stateRoot]{request(powerRoot,stateRoot,{{"op","restore"}});}).detach();
}
}
