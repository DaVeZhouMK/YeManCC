#pragma once
#include "power_source_state.h"
// In-process component boundary. The existing charge/display adapters run inside YeManCC.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>
#include "json.hpp"
#include "battery_host_contract.h"
#include "battery_managed_runtime.h"
#include <atomic>
#include <functional>
#include "sleep_power_state.h" // Parent-owned ABI: coherent=1, unknown=0.
namespace ymcc::batterycharge {
using Json=nlohmann::json;
inline std::timed_mutex& lock(){static auto* m=new std::timed_mutex;return *m;}
inline std::atomic<bool>& stopping(){static auto* value=new std::atomic<bool>{false};return *value;}
inline managed::Component& component(){static auto* value=new managed::Component;return *value;}
using SystemPowerReader=int(__cdecl*)(ymcc::sleeppower::SharedPowerV1*,std::uint32_t);
inline std::atomic<SystemPowerReader> systemPowerReader{nullptr};
inline bool bindSystemPowerReader(SystemPowerReader reader) noexcept {
    if(!reader||stopping().load(std::memory_order_acquire))return false;
    SystemPowerReader expected=nullptr;
    return systemPowerReader.compare_exchange_strong(expected,reader,std::memory_order_acq_rel)||expected==reader;
}
inline void notifySystemPower() noexcept {
    // No Component lookup/load, battery mutex, JSON, OS query or device work.
    if(!stopping().load(std::memory_order_acquire))managed::notifySystemPower();
}
inline void seal() noexcept {stopping().store(true,std::memory_order_release);managed::sealSystemPowerNotifications();}
inline bool hasBattery(){const auto state=ymcc::powerSource::snapshot();return state.batteryKnown&&state.hasBattery;}
// Same-process ABI: passive receipt only. No OS sampling or managed callback under
// the owner lock; the component stops using this function before process teardown.
inline std::uint64_t __cdecl nativePowerReader() noexcept {
    const auto state=ymcc::powerSource::snapshot();
    const auto percent=state.readable&&state.batteryKnown&&state.hasBattery&&state.status.BatteryLifePercent<=100?state.status.BatteryLifePercent:255u;
    return (state.readable?1ull:0ull)|(state.known?2ull:0ull)|(state.batteryKnown?4ull:0ull)|
        (state.hasBattery?8ull:0ull)|(state.ac==1?16ull:0ull)|(std::uint64_t(percent)<<8);
}
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
inline const char* readSourceTag(const Json& args){
    if(!args.is_object())return nullptr;
    const auto key=args.find("readSource");
    if(key==args.end()||!key->is_string())return nullptr;
    const auto& value=key->get_ref<const std::string&>();
    for(const char* tag:{"settings-edge","settings-poll","settings-retry","boot-capture","boot-preflight"})
        if(value==tag)return tag;
    return nullptr;
}
inline void compactRequestResultIdentity(Json& detail){
    const auto runtime=detail.find("runtime"),host=detail.find("hostIdentity");
    if(runtime==detail.end()||host==detail.end()||!runtime->is_object()||!host->is_object()||
        detail.contains("hostIdentitySharesRuntimeV1"))return;
    const auto execution=runtime->find("execution");
    if(execution==runtime->end()||*execution!="in-process")return;
    struct Field {const char* host;const char* runtime;bool integer;};
    constexpr Field fields[]={{"assemblyPath","componentPath",false},{"processPath","nativePath",false},
        {"execution","execution",false},{"implementation","implementation",false},{"sourceId","sourceId",false},
        {"protocol","protocol",true},{"pollingIntervalMs","pollingIntervalMs",true}};
    for(const auto& field:fields){
        const auto actual=host->find(field.host),copy=runtime->find(field.runtime);
        if(actual==host->end()||copy==runtime->end())return;
        if(field.integer){
            if(!actual->is_number_integer()||!copy->is_number_integer()||*actual<1||*copy<1||
                *actual>9007199254740991ULL||*copy>9007199254740991ULL)return;
        }else if(!actual->is_string()||!copy->is_string())return;
        if(*actual!=*copy)return;
    }
    detail.emplace("hostIdentitySharesRuntimeV1",true);
    for(const auto& field:fields)host->erase(field.host);
}
inline void trace(const std::wstring& stateRoot,const char* event,Json detail) noexcept {
    try {
        if(std::string_view(event)=="request-result")compactRequestResultIdentity(detail);
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
inline bool currentRequest(const std::function<bool()>& isCurrent) noexcept {
    try{return !isCurrent || isCurrent();}catch(...){return false;}
}
inline int __cdecl nativeRequestOwnerReader(std::uintptr_t context) noexcept {
    try{const auto* owner=reinterpret_cast<const std::function<bool()>*>(context);return !stopping().load(std::memory_order_acquire)&&owner&&currentRequest(*owner)?1:0;}catch(...){return 0;}
}
inline Json requestCore(const std::wstring& powerRoot,const std::wstring& stateRoot,const Json& input,Json& detail,const std::function<bool()>& isCurrent={}){
    try{
        if(stopping().load(std::memory_order_acquire))return failure("充电组件正在退出");
        if(!currentRequest(isCurrent))return failure("BOOT_CHARGE_REQUEST_EXPIRED");
        detail["runtime"]=runtimeIdentity(powerRoot);detail["stage"]="load-in-process";
        component().load(powerRoot);
        if(stopping().load(std::memory_order_acquire))return failure("充电组件正在退出");
        if(!currentRequest(isCurrent))return failure("BOOT_CHARGE_REQUEST_EXPIRED");
        const auto publicReader=systemPowerReader.load(std::memory_order_acquire);
        if(!publicReader)return failure("公共睡眠状态读取接口尚未绑定");
        const auto envelope=[&](Json request){Json value{{"powerRoot",utf8Path(powerRoot)},{"stateRoot",utf8Path(stateRoot)},{"nativePowerReader",std::to_string(reinterpret_cast<std::uintptr_t>(&nativePowerReader))},{"nativeSystemPowerReader",std::to_string(reinterpret_cast<std::uintptr_t>(publicReader))},{"request",std::move(request)}};
            if(isCurrent)value["nativeRequestOwner"]={{"reader",std::to_string(reinterpret_cast<std::uintptr_t>(&nativeRequestOwnerReader))},{"context",std::to_string(reinterpret_cast<std::uintptr_t>(&isCurrent))}};return value;};
        detail["phase"]="handshake";
        const auto hello=component().call(envelope({{"op","hello"}}));detail["hostIdentity"]=hello;
        if(!currentRequest(isCurrent))return failure("BOOT_CHARGE_REQUEST_EXPIRED");
        if(!matchingHost(hello))return failure("充电组件与主程序版本不一致，请退出 YMCC 后更新完整程序包");
        if(hello.value("execution",std::string{})!="in-process"||hello.value("processId",0u)!=GetCurrentProcessId())return failure("充电组件未绑定当前 YMCC 进程");
        detail["phase"]="operation";detail["stage"]="in-process-dispatch";
        auto bound=input;bound["protocol"]=kHostProtocol;bound["sourceId"]=kHostSourceId;
        auto result=component().call(envelope(std::move(bound)));
        if(stopping().load(std::memory_order_acquire)||!currentRequest(isCurrent))return failure("BOOT_CHARGE_REQUEST_EXPIRED");
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
inline Json request(const std::wstring& powerRoot,const std::wstring& stateRoot,const Json& input,const std::function<bool()>& isCurrent={},const char* readSource=nullptr){
    if(stopping().load(std::memory_order_acquire))return failure("充电组件正在退出");
    if(!currentRequest(isCurrent))return failure("BOOT_CHARGE_REQUEST_EXPIRED");
    std::unique_lock<std::timed_mutex> serial(lock(),std::defer_lock);
    if(!serial.try_lock_for(std::chrono::seconds(30)))return failure("充电控制正在处理其他请求，请稍后重试");
    if(!currentRequest(isCurrent))return failure("BOOT_CHARGE_REQUEST_EXPIRED");
    const auto start=GetTickCount64();
    Json detail{{"operation",input.value("op","")}};if(input.contains("target"))detail["target"]=input["target"];
    if(readSource&&detail["operation"]=="get")detail["readSource"]=readSource;
    trace(stateRoot,"request-start",detail);
    auto result=requestCore(powerRoot,stateRoot,input,detail,isCurrent);detail["elapsedMs"]=GetTickCount64()-start;
    for(const auto* key:{"ok","supported","batteryPresent","model","family","currentLimit","requestedLimit","managed","verification","reason"})if(result.contains(key))detail[key]=result[key];
    trace(stateRoot,"request-result",std::move(detail));return currentRequest(isCurrent)?result:failure("BOOT_CHARGE_REQUEST_EXPIRED");
}
inline void restoreAtStartup(const std::wstring& powerRoot,const std::wstring& stateRoot){
    // No preference/recovery file means zero component loading or hardware work.
    if(!std::filesystem::exists(stateRoot+L"\\battery-charge.settings.json")&&!std::filesystem::exists(stateRoot+L"\\battery-charge.recovery.json"))return;
    std::thread([powerRoot,stateRoot]{request(powerRoot,stateRoot,{{"op","restore"}});}).detach();
}
}
