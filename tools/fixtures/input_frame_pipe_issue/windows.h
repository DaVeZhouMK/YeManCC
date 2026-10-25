#pragma once
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using DWORD=std::uint32_t;using BOOL=int;using ULONGLONG=unsigned long long;using BYTE=unsigned char;
struct FakeHandle{bool event=false,closed=false;unsigned endpoint=0;};using HANDLE=FakeHandle*;
inline HANDLE const INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(~std::uintptr_t(0));
struct OVERLAPPED{std::uintptr_t Internal=0,InternalHigh=0;DWORD Offset=0,OffsetHigh=0;HANDLE hEvent=nullptr;};
constexpr BOOL TRUE=1,FALSE=0;constexpr DWORD ERROR_SUCCESS=0,ERROR_INVALID_HANDLE=6,ERROR_NOT_ENOUGH_MEMORY=8,ERROR_INVALID_DATA=13,ERROR_GEN_FAILURE=31,ERROR_INVALID_PARAMETER=87,ERROR_BROKEN_PIPE=109,ERROR_BUSY=170,ERROR_PIPE_BUSY=231,ERROR_NO_DATA=232,ERROR_PIPE_NOT_CONNECTED=233,ERROR_MORE_DATA=234,ERROR_PIPE_CONNECTED=535,ERROR_OPERATION_ABORTED=995,ERROR_IO_PENDING=997,ERROR_IO_INCOMPLETE=996,ERROR_SHUTDOWN_IN_PROGRESS=1115,ERROR_ARITHMETIC_OVERFLOW=534;
constexpr DWORD WAIT_OBJECT_0=0,WAIT_TIMEOUT=258,DUPLICATE_SAME_ACCESS=2;constexpr int CSTR_EQUAL=2;
namespace fake {
inline DWORD error=0;inline unsigned nextEndpoint=0,creates=0,closes=0,writes=0,reads=0,connects=0,cancels=0,resets=0,duplicateClosesWhilePending=0;
inline std::vector<std::unique_ptr<FakeHandle>> handles;
struct Operation{HANDLE pipe=nullptr,event=nullptr;DWORD bytes=0;bool pending=false,connect=false;};inline std::map<OVERLAPPED*,Operation> operations;
inline bool pendingConnect=false,completeConnect=false;
inline std::function<void()> connectHook,resetHook,createHook;
inline void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
inline HANDLE allocate(bool event=false,unsigned endpoint=0){handles.push_back(std::make_unique<FakeHandle>());auto h=handles.back().get();h->event=event;h->endpoint=endpoint;return h;}
inline bool livePending(HANDLE h){for(auto& [ov,o]:operations)if(o.pending&&(o.pipe==h||o.event==h))return true;return false;}
inline void clear(){for(auto&[ov,o]:operations)require(!o.pending,"reset loses pending resources");operations.clear();pendingConnect=completeConnect=false;connectHook=resetHook=createHook={};writes=reads=connects=cancels=resets=0;}
}
inline DWORD GetLastError(){return fake::error;}inline void SetLastError(DWORD e){fake::error=e;}
inline HANDLE GetCurrentProcess(){return nullptr;}
inline BOOL CloseHandle(HANDLE h){fake::require(h&&h!=INVALID_HANDLE_VALUE&&!h->closed,"double/invalid close");if(fake::livePending(h))++fake::duplicateClosesWhilePending;h->closed=true;++fake::closes;return TRUE;}
inline BOOL DuplicateHandle(HANDLE,HANDLE source,HANDLE,HANDLE* target,DWORD,BOOL,DWORD){*target=fake::allocate(false,source->endpoint);return TRUE;}
inline int CompareStringOrdinal(const wchar_t* a,int,const wchar_t* b,int,BOOL){std::wstring x=a,y=b;for(auto&c:x)c=std::towupper(c);for(auto&c:y)c=std::towupper(c);return x==y?CSTR_EQUAL:1;}
inline HANDLE CreateEventW(void*,BOOL,BOOL,const wchar_t*){++fake::creates;auto h=fake::allocate(true);if(fake::createHook){auto f=std::move(fake::createHook);fake::createHook={};f();}return h;}
inline BOOL ResetEvent(HANDLE h){fake::require(h&&h->event&&!h->closed&&!fake::livePending(h),"reset active/closed event");++fake::resets;if(fake::resetHook){auto f=std::move(fake::resetHook);fake::resetHook={};f();}return TRUE;}
inline BOOL ConnectNamedPipe(HANDLE h,OVERLAPPED* ov){++fake::connects;fake::operations[ov]={h,ov->hEvent,0,fake::pendingConnect,true};if(fake::connectHook){auto f=std::move(fake::connectHook);fake::connectHook={};f();}if(fake::pendingConnect){SetLastError(ERROR_IO_PENDING);return FALSE;}SetLastError(ERROR_PIPE_CONNECTED);return FALSE;}
inline BOOL WriteFile(HANDLE h,const void*,DWORD n,DWORD* count,OVERLAPPED* ov){fake::require(h&&!h->closed&&!count,"bad overlapped write");++fake::writes;fake::operations[ov]={h,ov->hEvent,n,false,false};return TRUE;}
inline BOOL ReadFile(HANDLE h,void* data,DWORD,DWORD* count,OVERLAPPED* ov){fake::require(h&&!h->closed&&!count,"bad overlapped read");++fake::reads;static constexpr char r[]="{}";std::memcpy(data,r,2);fake::operations[ov]={h,ov->hEvent,2,false,false};return TRUE;}
inline BOOL GetOverlappedResult(HANDLE h,OVERLAPPED* ov,DWORD* n,BOOL wait){fake::require(!wait&&h&&!h->closed,"blocking/closed query");auto it=fake::operations.find(ov);fake::require(it!=fake::operations.end()&&it->second.pipe==h,"query wrong operation owner");auto&o=it->second;if(o.pending&&o.connect&&fake::completeConnect)o.pending=false;if(o.pending){SetLastError(ERROR_IO_INCOMPLETE);return FALSE;}*n=o.bytes;return TRUE;}
inline DWORD WaitForSingleObject(HANDLE h,DWORD){fake::require(h&&!h->closed,"wait on closed");for(auto&[ov,o]:fake::operations)if(o.event==h&&o.pending)return WAIT_TIMEOUT;return WAIT_OBJECT_0;}
inline BOOL CancelIoEx(HANDLE h,OVERLAPPED* ov){auto it=fake::operations.find(ov);fake::require(it!=fake::operations.end()&&it->second.pipe==h&&!h->closed,"cancel wrong owner");++fake::cancels;return TRUE;}
inline BOOL DisconnectNamedPipe(HANDLE h){fake::require(h&&!h->closed,"disconnect closed");return TRUE;}
