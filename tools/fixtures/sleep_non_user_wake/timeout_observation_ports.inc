// Only Event Log/pool/window transports are controlled; native matching,
// event time parsing, lifetime checks, classification and sender stay original.
#include <winevt.h>
#include <memory>
#include <set>
#include <functional>
#include "sleep_residency_evidence.h"
#include "sleep_power_observation.h"
static std::vector<std::string> timeoutSourceEvents;
static std::vector<std::function<void()>> timeoutSourceWork;
static std::set<EVT_HANDLE> timeoutSourceHandles;
static unsigned timeoutSourceQueries=0,timeoutSourcePosts=0;
static bool timeoutSourceQueryAllowed=true,timeoutSourcePoolAllowed=true,timeoutSourceRenderAllowed=true;
static std::function<void()> timeoutSourceAfterRender;
static void resetTimeoutSourcePorts(){
 if(!timeoutSourceHandles.empty()||!timeoutSourceWork.empty())throw std::runtime_error("previous timeout source not returned");
 timeoutSourceEvents.clear();timeoutSourceQueries=timeoutSourcePosts=0;
 timeoutSourceQueryAllowed=timeoutSourcePoolAllowed=timeoutSourceRenderAllowed=true;
 timeoutSourceAfterRender=nullptr;
}
static bool poolTrySubmitExisting(std::function<void()> work) noexcept {
 if(!timeoutSourcePoolAllowed)return false;timeoutSourceWork.push_back(std::move(work));return true;
}
static EVT_HANDLE WINAPI timeoutSourceQuery(EVT_HANDLE session,LPCWSTR path,LPCWSTR query,DWORD flags){
 if(session||std::wstring(path)!=L"System"||std::wstring(query)!=L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Power'] and (EventID=506 or EventID=507 or EventID=107)]]"||
    flags!=(EvtQueryChannelPath|EvtQueryReverseDirection))throw std::runtime_error("unadmitted timeout query");
 ++timeoutSourceQueries;if(!timeoutSourceQueryAllowed){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
 const auto handle=reinterpret_cast<EVT_HANDLE>(101);timeoutSourceHandles.insert(handle);return handle;
}
static BOOL WINAPI timeoutSourceNext(EVT_HANDLE query,DWORD maximum,PEVT_HANDLE events,DWORD wait,DWORD flags,PDWORD count){
 if(!timeoutSourceHandles.count(query)||maximum!=8||wait||flags)throw std::runtime_error("unadmitted timeout event batch");
 *count=static_cast<DWORD>((std::min)(size_t{8},timeoutSourceEvents.size()));
 for(DWORD i=0;i<*count;++i){events[i]=reinterpret_cast<EVT_HANDLE>(102+static_cast<uintptr_t>(i));timeoutSourceHandles.insert(events[i]);}
 if(!*count){SetLastError(ERROR_NO_MORE_ITEMS);return FALSE;}return TRUE;
}
static BOOL WINAPI timeoutSourceRender(EVT_HANDLE context,EVT_HANDLE event,DWORD flags,DWORD bytes,PVOID buffer,PDWORD used,PDWORD properties){
 if(context||!timeoutSourceHandles.count(event)||flags!=EvtRenderEventXml)throw std::runtime_error("unadmitted timeout render");
 if(!timeoutSourceRenderAllowed){SetLastError(ERROR_INVALID_DATA);return FALSE;}
 const auto index=reinterpret_cast<uintptr_t>(event)-102;const auto& text=timeoutSourceEvents.at(index);
 const std::wstring wide(text.begin(),text.end());*used=static_cast<DWORD>((wide.size()+1)*sizeof(wchar_t));*properties=0;
 if(bytes<*used){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
 memcpy(buffer,wide.c_str(),*used);const auto hook=std::move(timeoutSourceAfterRender);timeoutSourceAfterRender=nullptr;if(hook)hook();return TRUE;
}
static BOOL WINAPI timeoutSourceClose(EVT_HANDLE handle){
 if(timeoutSourceHandles.erase(handle)!=1)throw std::runtime_error("foreign or duplicate timeout event close");return TRUE;
}
static BOOL timeoutSourcePost(HWND window,UINT message,WPARAM timer,LPARAM value){
 if(window!=g_hwnd||message!=WM_TIMER||timer!=SG_SLEEP_TIMEOUT_TIMER_ID||value)throw std::runtime_error("unadmitted timeout completion post");
 ++timeoutSourcePosts;return TRUE;
}
static void drainTimeoutSource(){
 while(!timeoutSourceWork.empty()){auto work=std::move(timeoutSourceWork.front());timeoutSourceWork.erase(timeoutSourceWork.begin());work();}
 if(!timeoutSourceHandles.empty())throw std::runtime_error("timeout event handles retained after source return");
}

static std::string timeoutXml(int id,int reason,ULONGLONG at,bool success=false){
 ULARGE_INTEGER number{};number.QuadPart=at;const FILETIME ft{number.LowPart,number.HighPart};SYSTEMTIME st{};
 if(!FileTimeToSystemTime(&ft,&st))throw std::runtime_error("fixture event time invalid");
 char stamp[64]{};std::snprintf(stamp,sizeof(stamp),"%04u-%02u-%02uT%02u:%02u:%02u.%07lluZ",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,at%10000000ULL);
 return "<Event><System><EventID>"+std::to_string(id)+"</EventID><TimeCreated SystemTime=\""+stamp+"\"/></System><EventData><Data Name=\"Reason\">"+std::to_string(reason)+"</Data>"+
  (success?"<Data Name=\"SleepEntered\">true</Data><Data Name=\"DurationInUs\">1000000</Data><Data Name=\"DripsResidencyInUs\">1000</Data>":"")+"</EventData></Event>";
}
