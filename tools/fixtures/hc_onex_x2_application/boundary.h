#pragma once
// The old fixture is copied unchanged into the evidence snapshot, never edited.
#include "old_mock_win32.h"
#include <Xinput.h>
#include <dbt.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <utility>
#include "json.hpp"
using json = nlohmann::json;
namespace appmock {
inline ULONGLONG now=10000;
inline bool diagnostic=false, postAccepted=true, serialAccepted=true, notificationAccepted=true;
struct Notification {bool active=true;};
inline std::vector<std::unique_ptr<Notification>> notifications;
inline unsigned registrations=0,unregistrations=0;
inline GUID registeredGuid{};
inline unsigned posts=0, refreshes=0;
inline std::function<void()> beforeCompletion,beforeEnumeration,afterEnumeration,afterOwnerOpen,afterRead;
inline std::function<bool()> writePermissionOverride;
inline std::function<void(bool)> afterSourceRevoke;
inline bool hardwareAllowed=true;
inline unsigned permissionChecks=0;
struct Log {std::string event;json fields;};
inline std::vector<Log> logs;
inline std::deque<std::function<void()>> jobs;
inline std::vector<std::string> actions;
inline HDEVNOTIFY WINAPI registerNotification(HANDLE recipient,LPVOID filter,DWORD flags){
    ++registrations;test::check(recipient!=nullptr&&filter&&flags==DEVICE_NOTIFY_WINDOW_HANDLE,"memory device notification recipient and flags");
    auto* f=static_cast<DEV_BROADCAST_DEVICEINTERFACE_W*>(filter);test::check(f->dbcc_size==sizeof(*f)&&f->dbcc_devicetype==DBT_DEVTYP_DEVICEINTERFACE,"device notification interface filter");registeredGuid=f->dbcc_classguid;
    if(!notificationAccepted){mock::w.error=ERROR_ACCESS_DENIED;return nullptr;}
    notifications.push_back(std::make_unique<Notification>());return reinterpret_cast<HDEVNOTIFY>(notifications.back().get());
}
inline BOOL WINAPI unregisterNotification(HDEVNOTIFY handle){
    for(auto& n:notifications)if(reinterpret_cast<HDEVNOTIFY>(n.get())==handle){test::check(n->active,"notification unregister at most once");n->active=false;++unregistrations;return TRUE;}
    test::fail("only memory notification handles may be unregistered");
}
inline int WINAPI compareOrdinal(LPCWCH a,int an,LPCWCH b,int bn,BOOL ignoreCase){
    test::check(a&&b&&an>=0&&bn>=0&&ignoreCase==TRUE,"bounded case-insensitive path comparison");
    const auto lower=[](wchar_t c){return c>=L'A'&&c<=L'Z'?static_cast<wchar_t>(c-L'A'+L'a'):c;};
    for(int i=0;i<(std::min)(an,bn);++i){const auto ac=lower(a[i]),bc=lower(b[i]);if(ac<bc)return CSTR_LESS_THAN;if(ac>bc)return CSTR_GREATER_THAN;}
    return an<bn?CSTR_LESS_THAN:an>bn?CSTR_GREATER_THAN:CSTR_EQUAL;
}
inline SHORT WINAPI asyncKeyState(int key){test::check(key>=0&&key<256,"bounded memory keyboard query");return 0;}
inline HDEVINFO WINAPI classDevs(const GUID* g,PCWSTR e,HWND parent,DWORD flags){
    if(beforeEnumeration)beforeEnumeration();const auto result=mock::getClassDevs(g,e,parent,flags);if(afterEnumeration)afterEnumeration();return result;
}
inline HANDLE WINAPI open(LPCWSTR path,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templateFile){
    const auto result=mock::open(path,access,sharing,security,disposition,flags,templateFile);if(access&&afterOwnerOpen)afterOwnerOpen();return result;
}
inline BOOL WINAPI read(HANDLE h,LPVOID buffer,DWORD size,LPDWORD bytes,LPOVERLAPPED ov){
    const auto result=mock::read(h,buffer,size,bytes,ov);if(afterRead)afterRead();return result;
}
inline ULONGLONG WINAPI ticks(){return now;}
inline BOOL WINAPI result(HANDLE h,LPOVERLAPPED ov,LPDWORD bytes,BOOL wait){
    if(beforeCompletion)beforeCompletion();return mock::result(h,ov,bytes,wait);
}
inline BOOL WINAPI post(HWND,UINT message,WPARAM w,LPARAM l){test::check(message!=0&&w==0&&l==0,"only OEM edge UI notification is mocked");++posts;return postAccepted?TRUE:FALSE;}
}
#undef SetupDiGetClassDevsW
#define SetupDiGetClassDevsW appmock::classDevs
#undef CreateFileW
#define CreateFileW appmock::open
#undef ReadFile
#define ReadFile appmock::read
#undef GetOverlappedResult
#define GetOverlappedResult appmock::result
#define RegisterDeviceNotificationW appmock::registerNotification
#define UnregisterDeviceNotification appmock::unregisterNotification
#define CompareStringOrdinal appmock::compareOrdinal
#define GetAsyncKeyState appmock::asyncKeyState
#define GetTickCount64 appmock::ticks
#define PostMessageW appmock::post
#include "onex_x2_windows.h"
inline void appendNativeLifecycleLog(const char* event,const json& fields){appmock::logs.push_back({event,fields});}
inline bool domainLogEnabled(const char*){return appmock::diagnostic;}
