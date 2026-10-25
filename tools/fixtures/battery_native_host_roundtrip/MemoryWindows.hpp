#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using HANDLE=std::intptr_t;using DWORD=std::uint32_t;using ULONGLONG=unsigned long long;
constexpr HANDLE INVALID_HANDLE_VALUE=-1;constexpr bool TRUE=true,FALSE=false;
constexpr DWORD GENERIC_READ=0x80000000u,GENERIC_WRITE=0x40000000u,OPEN_EXISTING=3,FILE_FLAG_OVERLAPPED=0x40000000u,STARTF_USESHOWWINDOW=1,SW_HIDE=0,CREATE_NO_WINDOW=0x08000000,ERROR_PIPE_BUSY=231,ERROR_FILE_NOT_FOUND=2,ERROR_ACCESS_DENIED=5,ERROR_BROKEN_PIPE=109,ERROR_IO_PENDING=997,WAIT_OBJECT_0=0;
struct FILETIME{DWORD dwLowDateTime=0,dwHighDateTime=0;};
struct SYSTEM_POWER_STATUS{unsigned char BatteryFlag=128;};
struct SYSTEMTIME{unsigned short wYear=2026,wMonth=10,wDayOfWeek=1,wDay=19,wHour=0,wMinute=0,wSecond=0,wMilliseconds=0;};
struct OVERLAPPED{HANDLE hEvent=0;};
struct STARTUPINFOW{DWORD cb=0,dwFlags=0,wShowWindow=0;};struct PROCESS_INFORMATION{HANDLE hProcess=INVALID_HANDLE_VALUE,hThread=INVALID_HANDLE_VALUE;DWORD dwProcessId=0;};
namespace memory {
using Json=nlohmann::json;
struct Pipe{std::string request,response;std::size_t offset=0;bool completed=false;};
struct Pending{DWORD count=0;bool success=true;};
inline std::map<HANDLE,Pipe> pipes;inline std::map<HANDLE,Pending> events;inline std::vector<Json> trace,exchanges;
inline ULONGLONG now=1000;inline DWORD lastError=0;inline int nextHandle=1,connects=0,closedPipes=0,closedEvents=0,launchAttempts=0,rpcSequence=0;
inline bool armWriteFailure=false;
inline std::string scenario;inline std::string executablePath;inline bool initialized=false;
inline void ensure(){if(initialized)return;initialized=true;scenario=std::getenv("YMCC_NATIVE_CASE")?std::getenv("YMCC_NATIVE_CASE"):"";executablePath=std::getenv("YMCC_NATIVE_EXE")?std::getenv("YMCC_NATIVE_EXE"):"owned-native-probe.exe";}
inline void event(const char* name,Json detail=Json::object()){ensure();detail["event"]=name;trace.push_back(std::move(detail));}
inline void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
inline Json rpc(Json request){request["sequence"]=++rpcSequence;std::cout<<"YMCC_NATIVE_RPC "<<request.dump()<<std::endl;std::string response;if(!std::getline(std::cin,response))throw std::runtime_error("Owned memory endpoint broker disconnected");auto result=Json::parse(response);need(result.value("ok",false),"Owned actual Host endpoint failed");return result;}
inline Json host(const std::string& raw){return rpc({{"command","pipe.exchange"},{"raw",raw}});}
inline Json hostControl(const char* command,Json args){return rpc({{"command",command},{"args",args}});}
inline void response(Pipe& p){
 const auto parsed=Json::parse(p.request);const auto op=parsed.value("op","");auto actual=host(p.request);std::string wire=actual.at("raw").get<std::string>();exchanges.push_back({{"request",parsed},{"actualHostRaw",wire},{"actualHostStats",actual.at("stats")}});
 if(op=="hello"&&(scenario=="original-bad-hello"||scenario=="fault-original-bad-hello")){auto hello=Json::parse(wire);hello["protocol"]=999;wire=hello.dump()+"\n";}
 if(op=="hello"&&scenario=="bad-component"){auto hello=Json::parse(wire);hello["component"]="different-component";wire=hello.dump()+"\n";}
 if(op=="hello"&&scenario=="handshake-eof")wire.clear();
 if(op=="set"&&scenario=="truncated-after-apply")wire=wire.substr(0,wire.size()/2);
 if(op=="set"&&scenario=="eof-after-apply")wire.clear();
 p.response=std::move(wire);p.completed=true;event("actual-host-response",{{"op",op},{"deliveredBytes",p.response.size()}});
}
inline void finish(const char* error=nullptr){ensure();const auto dest=std::getenv("YMCC_NATIVE_PORT_RECEIPT");if(!dest)return;Json r={{"scenario",scenario},{"actualHostExchanges",exchanges},{"trace",trace},{"openMemoryPipes",pipes.size()},{"openMemoryEvents",events.size()},{"closedMemoryPipes",closedPipes},{"closedMemoryEvents",closedEvents},{"launchAttempts",launchAttempts},{"error",error?Json(error):Json(nullptr)},{"realHostMain",false},{"realNamedPipe",false},{"realHardware",false}};std::ofstream f(dest);f<<r.dump(2);}
}
inline HANDLE CreateEventW(void*,bool manual,bool initial,const wchar_t*){memory::need(manual&&!initial,"original transfer event flags");const auto h=memory::nextHandle++;memory::events[h]={};return h;}
inline bool CloseHandle(HANDLE h){if(memory::pipes.erase(h)){++memory::closedPipes;memory::event("close-memory-pipe",{{"handle",h}});return true;}if(memory::events.erase(h)){++memory::closedEvents;return true;}throw std::runtime_error("Close outside owned memory handle set");}
inline HANDLE CreateFileW(const wchar_t* name,DWORD access,DWORD,void*,DWORD disposition,DWORD flags,void*){
 memory::ensure();memory::need(std::wstring(name).starts_with(L"\\\\.\\pipe\\YMCC.BatteryCharge.")&&access==(GENERIC_READ|GENERIC_WRITE)&&disposition==OPEN_EXISTING&&flags==FILE_FLAG_OVERLAPPED,"native pipe name and flags");++memory::connects;
 if(memory::scenario=="reconnect-timeout"&&memory::connects>1){memory::lastError=ERROR_PIPE_BUSY;return INVALID_HANDLE_VALUE;}
 const auto h=memory::nextHandle++;memory::pipes[h]={};memory::event("open-memory-pipe",{{"handle",h},{"connection",memory::connects}});return h;
}
inline bool WriteFile(HANDLE h,void* data,DWORD size,DWORD* count,OVERLAPPED* ov){
 auto it=memory::pipes.find(h);memory::need(it!=memory::pipes.end()&&memory::events.contains(ov->hEvent),"owned pipe/event for write");auto& p=it->second;
 if(memory::scenario=="partial-operation-write"&&memory::armWriteFailure&&memory::connects%2==0&&p.request.size()>=9){*count=0;memory::lastError=ERROR_BROKEN_PIPE;return false;}
 const bool fragmented=memory::scenario=="fragmented-pending"||memory::scenario=="partial-operation-write";
 const DWORD n=(std::min)(size,fragmented?DWORD(3):size);p.request.append(static_cast<const char*>(data),n);*count=n;
 if(!p.completed&&p.request.find('\n')!=std::string::npos)memory::response(p);
 if(memory::scenario=="fragmented-pending"){memory::events[ov->hEvent]={n,true};*count=0;memory::lastError=ERROR_IO_PENDING;return false;}return true;
}
inline bool ReadFile(HANDLE h,void* data,DWORD size,DWORD* count,OVERLAPPED* ov){
 auto it=memory::pipes.find(h);memory::need(it!=memory::pipes.end()&&memory::events.contains(ov->hEvent),"owned pipe/event for read");auto& p=it->second;const DWORD n=static_cast<DWORD>((std::min)(std::size_t(size),p.response.size()-p.offset));const DWORD piece=memory::scenario=="fragmented-pending"?(std::min)(n,DWORD(7)):n;std::memcpy(data,p.response.data()+p.offset,piece);p.offset+=piece;*count=piece;
 if(memory::scenario=="fragmented-pending"){memory::events[ov->hEvent]={piece,true};*count=0;memory::lastError=ERROR_IO_PENDING;return false;}return true;
}
inline DWORD GetLastError(){return memory::lastError;}
inline ULONGLONG GetTickCount64(){return memory::now;}
inline void Sleep(DWORD ms){memory::now+=ms;}
inline DWORD WaitForSingleObject(HANDLE h,DWORD timeout){memory::need(memory::events.contains(h),"only owned event may be waited");memory::event("memory-wait",{{"timeout",timeout}});return WAIT_OBJECT_0;}
inline bool CancelIoEx(HANDLE,OVERLAPPED*){memory::event("memory-cancel");return true;}
inline bool GetOverlappedResult(HANDLE,OVERLAPPED* ov,DWORD* count,bool wait){memory::need(memory::events.contains(ov->hEvent),"completion before event close");*count=memory::events[ov->hEvent].count;memory::event("memory-complete",{{"bytes",*count},{"wait",wait}});return memory::events[ov->hEvent].success;}
inline DWORD GetCurrentProcessId(){return 4242;}
inline HANDLE GetCurrentProcess(){return 7000;}
inline bool GetProcessTimes(HANDLE,FILETIME* c,FILETIME*,FILETIME*,FILETIME*){c->dwLowDateTime=123456;return true;}
inline bool GetSystemPowerStatus(SYSTEM_POWER_STATUS* p){p->BatteryFlag=128;return true;}
inline void GetLocalTime(SYSTEMTIME* p){*p=SYSTEMTIME{};}
inline DWORD GetModuleFileNameW(void*,wchar_t* data,DWORD size){memory::ensure();const auto path=std::filesystem::path(memory::executablePath).wstring();memory::need(path.size()+1<size,"fixture executable path bounded");std::wmemcpy(data,path.c_str(),path.size()+1);return static_cast<DWORD>(path.size());}
inline bool GetExitCodeProcess(HANDLE,DWORD*){throw std::runtime_error("Unexpected real child status path");}
