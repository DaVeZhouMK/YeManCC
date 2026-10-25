#pragma once
#include "process_efficiency.h"
// IPC transport only. Battery adapters and their recovery lifetime live in the separate host.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include "json.hpp"
#include "battery_host_contract.h"
namespace ymcc::batterycharge {
using Json=nlohmann::json;
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;void reset(){if(h!=INVALID_HANDLE_VALUE&&h)CloseHandle(h);h=INVALID_HANDLE_VALUE;}~Handle(){reset();}};
inline std::mutex& lock(){static auto* m=new std::mutex;return *m;}
inline bool hasBattery(){SYSTEM_POWER_STATUS s{};return GetSystemPowerStatus(&s)&&s.BatteryFlag!=255&&(s.BatteryFlag&128)==0;}
inline Json failure(const char* reason){return {{"schema",1},{"ok",false},{"supported",false},{"batteryPresent",hasBattery()},{"model",""},{"family",""},{"kind","unsupported"},{"choices",Json::array()},{"currentLimit",nullptr},{"requestedLimit",nullptr},{"managed",false},{"verification","unknown"},{"fence",""},{"reason",reason}};}
// Validate JSON types and bounds before starting a helper. Route-specific choices remain authoritative in the host.
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
inline unsigned long long parentStamp(){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))return 0;return (static_cast<unsigned long long>(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
inline std::wstring quoted(const std::wstring& value){
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t ch:value){
        if(ch==L'\\'){++slashes;continue;}
        out.append(ch==L'"'?slashes*2+1:slashes,L'\\');slashes=0;out+=ch;
    }
    out.append(slashes*2,L'\\');out+=L'"';return out;
}
inline bool transfer(HANDLE pipe,bool write,void* data,DWORD size,DWORD& count,ULONGLONG deadline){
    Handle event;event.h=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event.h)return false;
    OVERLAPPED ov{};ov.hEvent=event.h;count=0;
    const bool done=write?!!WriteFile(pipe,data,size,&count,&ov):!!ReadFile(pipe,data,size,&count,&ov);
    if(done)return true;
    if(GetLastError()!=ERROR_IO_PENDING)return false;
    const auto now=GetTickCount64();const DWORD remaining=deadline>now?static_cast<DWORD>(deadline-now):0;
    if(WaitForSingleObject(event.h,remaining)!=WAIT_OBJECT_0){CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&count,TRUE);return false;}
    return !!GetOverlappedResult(pipe,&ov,&count,FALSE);
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
        {"nativePath",utf8Path(path)},{"powerRoot",utf8Path(powerRoot)},{"hostPath",utf8Path(powerRoot+L"\\battery-host\\YeManBatteryHost.exe")}};
}
inline bool matchingHost(const Json& hello){
    return hello.is_object()&&hello.value("protocol",0)==kHostProtocol&&hello.value("sourceId","")==kHostSourceId&&hello.value("component","")=="YeManBatteryHost";
}
inline Json exchange(HANDLE pipe,const Json& input,ULONGLONG deadline,Json& detail){
    detail["stage"]="write-request";
    auto text=input.dump()+"\n";if(text.size()>4096)throw std::runtime_error("充电设置参数过大");
    DWORD sent=0;
    while(sent<text.size()){DWORD n=0;if(!transfer(pipe,true,text.data()+sent,static_cast<DWORD>(text.size()-sent),n,deadline)||!n)throw std::runtime_error("充电请求结果未知，请重新读取，避免重复设置");sent+=n;}
    detail["stage"]="read-response";
    std::string reply;char buffer[2048];
    while(reply.size()<16384){DWORD n=0;if(!transfer(pipe,false,buffer,sizeof(buffer),n,deadline)||!n)throw std::runtime_error("充电设置结果未确认，请重新读取");reply.append(buffer,n);if(reply.find('\n')!=std::string::npos)break;}
    if(reply.size()>=16384)throw std::runtime_error("充电状态响应异常");
    detail["stage"]="parse-response";return Json::parse(reply);
}
inline Json requestCore(const std::wstring& powerRoot,const std::wstring& stateRoot,const Json& input,Json& detail){
    try{
        const auto requestDeadline=GetTickCount64()+30000;
        detail["runtime"]=runtimeIdentity(powerRoot);
        detail["stage"]="parent-identity";
        const auto stamp=parentStamp();if(!stamp)return failure("无法确认充电控制进程身份");
        const auto suffix=std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(stamp);
        const auto pipeName=L"\\\\.\\pipe\\YMCC.BatteryCharge."+suffix;
        Handle pipe,child;detail["stage"]="connect";
        auto connect=[&]{pipe.h=CreateFileW(pipeName.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);return pipe.h!=INVALID_HANDLE_VALUE;};
        if(!connect()){
            const auto error=GetLastError();detail["win32"]=error;
            if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PIPE_BUSY)return failure("充电控制通道不可用");
            if(error==ERROR_FILE_NOT_FOUND){
                detail["stage"]="launch";
                const auto exe=powerRoot+L"\\battery-host\\YeManBatteryHost.exe";
                if(!std::filesystem::is_regular_file(exe))return failure("缺少充电控制组件，请更新完整程序包");
                std::wstring command=quoted(exe)+L" --parent "+std::to_wstring(GetCurrentProcessId())+L" --parent-start "+std::to_wstring(stamp)+L" --power-root "+quoted(powerRoot)+L" --state-dir "+quoted(stateRoot);
                STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
                if(!ymcc::processEfficiency::createProcess(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,powerRoot.c_str(),&si,&pi)){detail["win32"]=GetLastError();return failure("充电控制组件无法启动");}
                child.h=pi.hProcess;CloseHandle(pi.hThread);detail["hostPid"]=pi.dwProcessId;detail.erase("win32");
                trace(stateRoot,"host-launched",detail);
            }
            detail["stage"]="await-pipe";const auto until=GetTickCount64()+12000;
            while(GetTickCount64()<until){
                if(connect())break;
                if(child.h!=INVALID_HANDLE_VALUE&&WaitForSingleObject(child.h,0)==WAIT_OBJECT_0){
                    DWORD code=0;GetExitCodeProcess(child.h,&code);detail["exitCode"]=code;
                    if(code==3)return failure("另一充电控制实例仍在退出，请稍后重新读取");
                    const auto reason="充电控制组件已退出（代码 "+std::to_string(code)+"），请导出日志";
                    return failure(reason.c_str());
                }
                Sleep(50);
            }
            if(pipe.h==INVALID_HANDLE_VALUE)return failure("充电控制组件未响应或已有其他实例，请稍后重试");
        }
        // An old helper ignores new request fields. Ask a read-only hello first;
        // never send a user mutation until the actual running assembly matches.
        detail["phase"]="handshake";
        const auto hello=exchange(pipe.h,{{"op","hello"}},(std::min)(requestDeadline,GetTickCount64()+5000),detail);
        detail["hostIdentity"]=hello;
        if(!matchingHost(hello))return failure("充电组件与主程序版本不一致，请退出 YMCC 后同时更新 YeManCC 和 PowerControl 文件夹");
        pipe.reset();detail["phase"]="operation";detail["stage"]="reconnect";
        const auto reconnectUntil=(std::min)(requestDeadline,GetTickCount64()+2000);
        while(!connect()&&GetTickCount64()<reconnectUntil)Sleep(20);
        if(pipe.h==INVALID_HANDLE_VALUE)return failure("充电控制通道未恢复，请重新读取");
        auto bound=input;bound["protocol"]=kHostProtocol;bound["sourceId"]=kHostSourceId;
        auto result=exchange(pipe.h,bound,(std::min)(requestDeadline,GetTickCount64()+18000),detail);
        if(result.contains("error"))return failure(result.at("error").get<std::string>().c_str());return result;
    }catch(const std::exception& e){return failure(e.what());}
    catch(...){return failure("充电控制暂不可用，请重新读取");}
}
inline Json request(const std::wstring& powerRoot,const std::wstring& stateRoot,const Json& input){
    std::lock_guard<std::mutex> serial(lock());const auto start=GetTickCount64();
    Json detail{{"operation",input.value("op","")}};if(input.contains("target"))detail["target"]=input["target"];
    trace(stateRoot,"request-start",detail);
    auto result=requestCore(powerRoot,stateRoot,input,detail);detail["elapsedMs"]=GetTickCount64()-start;
    for(const auto* key:{"ok","supported","batteryPresent","model","family","currentLimit","requestedLimit","managed","verification","reason"})if(result.contains(key))detail[key]=result[key];
    trace(stateRoot,"request-result",detail);return result;
}
inline void restoreAtStartup(const std::wstring& powerRoot,const std::wstring& stateRoot){
    // No preference/recovery file means zero hardware work at startup.
    if(!std::filesystem::exists(stateRoot+L"\\battery-charge.settings.json")&&!std::filesystem::exists(stateRoot+L"\\battery-charge.recovery.json"))return;
    std::thread([powerRoot,stateRoot]{request(powerRoot,stateRoot,{{"op","restore"}});}).detach();
}
}
