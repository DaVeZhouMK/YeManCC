// Fake-only Win32/HID boundary. No Windows/SDK includes and no real API calls.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <type_traits>
#include <vector>
using BYTE = unsigned char;
using DWORD = std::uint32_t;
using USHORT = std::uint16_t;
using ULONG = std::uint32_t;
using BOOL = int;
struct FakeResource;
using HANDLE = FakeResource*;
using HDEVINFO = HANDLE;
inline HANDLE const INVALID_HANDLE_VALUE = reinterpret_cast<HANDLE>(std::uintptr_t(-1));
constexpr BOOL TRUE=1, FALSE=0;
constexpr DWORD ERROR_SUCCESS=0, ERROR_ACCESS_DENIED=5, ERROR_INVALID_HANDLE=6,
 ERROR_GEN_FAILURE=31, ERROR_INVALID_PARAMETER=87, ERROR_INSUFFICIENT_BUFFER=122,
 ERROR_NO_MORE_ITEMS=259, ERROR_OPERATION_ABORTED=995, ERROR_IO_INCOMPLETE=996,
 ERROR_IO_PENDING=997, ERROR_NOT_FOUND=1168, ERROR_DEVICE_NOT_CONNECTED=433;
constexpr DWORD FILE_ATTRIBUTE_NORMAL=0x80, FILE_FLAG_OVERLAPPED=0x40000000,
 FILE_FLAG_WRITE_THROUGH=0x80000000, GENERIC_READ=0x80000000, GENERIC_WRITE=0x40000000,
 FILE_SHARE_READ=1, FILE_SHARE_WRITE=2, OPEN_EXISTING=3,
 DIGCF_PRESENT=2, DIGCF_DEVICEINTERFACE=16, WAIT_OBJECT_0=0, WAIT_TIMEOUT=258,
 WAIT_FAILED=0xffffffff;
constexpr int HIDP_STATUS_SUCCESS=0;
struct GUID { int tag=7; };
inline GUID kYeManGuidDeviceInterfaceHid;
struct SP_DEVICE_INTERFACE_DATA { DWORD cbSize=0, index=0; };
struct SP_DEVINFO_DATA { DWORD cbSize=0; };
struct SP_DEVICE_INTERFACE_DETAIL_DATA_W { DWORD cbSize; wchar_t DevicePath[512]; };
struct HIDD_ATTRIBUTES { DWORD Size=0; USHORT VendorID=0, ProductID=0, VersionNumber=0; };
struct HIDP_CAPS { USHORT Usage=0, UsagePage=0, OutputReportByteLength=0; };
struct FakeCandidate {
 std::wstring path=L"\\\\?\\hid#vid_0db0&pid_1902&cmd";
 USHORT vid=0x0db0,pid=0x1902,page=0xfff0,usage=0x40,firmware=411,length=64;
 DWORD openError=0; bool attrs=true,prep=true,caps=true,size=true,detail=true;
};
using PHIDP_PREPARSED_DATA = FakeCandidate*;
struct OVERLAPPED { std::uintptr_t Internal=0, InternalHigh=0; DWORD Offset=0,OffsetHigh=0; HANDLE hEvent=nullptr; };
struct FakeResource { enum Kind { Device, Event, Set } kind; int id=0,candidate=-1; DWORD flags=0; bool closed=false; };
struct FakeContractFailure : std::runtime_error { using std::runtime_error::runtime_error; };
namespace fake {
struct Reply { BOOL ok=TRUE; DWORD bytes=64,error=0; };
enum Start { Immediate, Pending, StartError };
struct Plan { Start start=Immediate; DWORD startError=433,syncCount=64,misleadingAsyncCount=64; bool strictNull=true,cancelOk=true,eventOk=true; DWORD cancelError=ERROR_NOT_FOUND; std::deque<DWORD> waits; std::deque<Reply> results; };
struct Operation { HANDLE handle=nullptr,event=nullptr; OVERLAPPED* ov=nullptr; const BYTE* buffer=nullptr; std::vector<BYTE> snapshot; bool active=false; };
inline std::vector<FakeCandidate> candidates;
inline std::vector<std::unique_ptr<FakeResource>> resources;
inline Plan plan;
inline Operation op;
inline DWORD lastError=0,enumerationError=0,classError=0;
inline int nextId=1,writeCalls=0,resultCalls=0,cancelCalls=0,waitCalls=0,resetCalls=0,
 setDestroyed=0,prepFreed=0,syncBoundaryHits=0,nullCountCalls=0,nonNullCountCalls=0,logs=0;
inline bool trace=true;
inline std::vector<int> openedCandidates;
inline std::vector<DWORD> openFlags,waitBudgets;
inline std::vector<HANDLE> closed,resultsHandles,cancelHandles;
inline std::vector<OVERLAPPED*> resultsOverlapped,cancelOverlapped;
inline HANDLE allocate(FakeResource::Kind kind,int candidate=-1,DWORD flags=0) {
 auto r=std::make_unique<FakeResource>();r->kind=kind;r->id=nextId++;r->candidate=candidate;r->flags=flags;auto h=r.get();resources.push_back(std::move(r));return h;
}
inline void require(bool b,const char* why) { if(!b) throw FakeContractFailure(why); }
inline void reset() {
 require(!op.active,"RESET_WITH_LIVE_IO"); candidates.clear();resources.clear();plan=Plan{};op=Operation{};
 lastError=enumerationError=classError=0;nextId=1;writeCalls=resultCalls=cancelCalls=waitCalls=resetCalls=setDestroyed=prepFreed=syncBoundaryHits=nullCountCalls=nonNullCountCalls=logs=0;trace=true;
 openedCandidates.clear();openFlags.clear();waitBudgets.clear();closed.clear();resultsHandles.clear();resultsOverlapped.clear();cancelHandles.clear();cancelOverlapped.clear();
}
inline FakeCandidate candidate(std::wstring suffix=L"selected",USHORT pid=0x1902,USHORT page=0xfff0,USHORT usage=0x40) {
 FakeCandidate c;c.path=L"\\\\?\\hid#vid_0db0&"+suffix;c.pid=pid;c.page=page;c.usage=usage;return c;
}
inline void checkOp(HANDLE h,OVERLAPPED* ov) {
 require(op.handle==h && op.ov==ov,"WRONG_LATE_REAP_RESOURCE_IDENTITY");
 require(h && !h->closed && op.event && !op.event->closed,"LIVE_IO_RESOURCE_CLOSED");
 require(ov->hEvent==op.event,"LIVE_OVERLAPPED_EVENT_CHANGED");
 require(op.buffer && std::equal(op.snapshot.begin(),op.snapshot.end(),op.buffer),"LIVE_IO_BUFFER_CHANGED");
}
}
inline DWORD GetLastError() { return fake::lastError; }
inline void SetLastError(DWORD error) { fake::lastError=error; }
inline HDEVINFO SetupDiGetClassDevsW(const GUID*,const wchar_t*,HANDLE,DWORD flags) {
 fake::require(flags==(DIGCF_PRESENT|DIGCF_DEVICEINTERFACE),"ENUM_FLAGS_CHANGED");
 if(fake::classError){SetLastError(fake::classError);return INVALID_HANDLE_VALUE;}return fake::allocate(FakeResource::Set);
}
inline BOOL SetupDiEnumDeviceInterfaces(HDEVINFO,void*,const GUID*,DWORD index,SP_DEVICE_INTERFACE_DATA* out) {
 if(index>=fake::candidates.size()){SetLastError(fake::enumerationError?fake::enumerationError:ERROR_NO_MORE_ITEMS);return FALSE;}out->index=index;return TRUE;
}
inline BOOL SetupDiGetDeviceInterfaceDetailW(HDEVINFO,SP_DEVICE_INTERFACE_DATA* iface,SP_DEVICE_INTERFACE_DETAIL_DATA_W* out,DWORD bytes,DWORD* needed,SP_DEVINFO_DATA*) {
 auto& c=fake::candidates.at(iface->index);
 if(!out){if(needed)*needed=c.size?sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W):0;SetLastError(c.size?ERROR_INSUFFICIENT_BUFFER:ERROR_GEN_FAILURE);return FALSE;}
 if(!c.detail){SetLastError(ERROR_GEN_FAILURE);return FALSE;}
 fake::require(bytes>=sizeof(*out),"DETAIL_BUFFER_TOO_SMALL"); fake::require(c.path.size()<512,"FAKE_PATH_TOO_LONG");std::copy(c.path.begin(),c.path.end(),out->DevicePath);out->DevicePath[c.path.size()]=0;return TRUE;
}
inline BOOL SetupDiDestroyDeviceInfoList(HDEVINFO h) {fake::require(h->kind==FakeResource::Set,"NOT_SETUP_SET");h->closed=true;++fake::setDestroyed;return TRUE;}
inline HANDLE CreateFileW(const wchar_t* path,DWORD access,DWORD share,void*,DWORD disposition,DWORD flags,HANDLE) {
 fake::require(access==(GENERIC_READ|GENERIC_WRITE)&&share==(FILE_SHARE_READ|FILE_SHARE_WRITE)&&disposition==OPEN_EXISTING,"OPEN_ACCESS_SHARE_CHANGED");
 for(std::size_t i=0;i<fake::candidates.size();++i)if(fake::candidates[i].path==path){fake::openedCandidates.push_back(int(i));fake::openFlags.push_back(flags);if(fake::candidates[i].openError){SetLastError(fake::candidates[i].openError);return INVALID_HANDLE_VALUE;}return fake::allocate(FakeResource::Device,int(i),flags);}
 throw FakeContractFailure("OPEN_NOT_FROM_ENUMERATED_CANDIDATE");
}
inline BOOL CloseHandle(HANDLE h) {
 fake::require(h && h!=INVALID_HANDLE_VALUE && !h->closed,"DOUBLE_OR_INVALID_CLOSE");
 fake::require(!fake::op.active || (h!=fake::op.handle && h!=fake::op.event),"CLOSED_ACTIVE_DEVICE_OR_EVENT");h->closed=true;fake::closed.push_back(h);return TRUE;
}
inline BOOL HidD_GetAttributes(HANDLE h,HIDD_ATTRIBUTES* out){auto&c=fake::candidates.at(h->candidate);if(!c.attrs)return FALSE;out->VendorID=c.vid;out->ProductID=c.pid;out->VersionNumber=c.firmware;return TRUE;}
inline BOOL HidD_GetPreparsedData(HANDLE h,PHIDP_PREPARSED_DATA* out){auto&c=fake::candidates.at(h->candidate);if(!c.prep)return FALSE;*out=&c;return TRUE;}
inline int HidP_GetCaps(PHIDP_PREPARSED_DATA c,HIDP_CAPS* out){if(!c->caps)return -1;out->UsagePage=c->page;out->Usage=c->usage;out->OutputReportByteLength=c->length;return HIDP_STATUS_SUCCESS;}
inline BOOL HidD_FreePreparsedData(PHIDP_PREPARSED_DATA){++fake::prepFreed;return TRUE;}
inline HANDLE CreateEventW(void*,BOOL manual,BOOL initial,const wchar_t*){fake::require(manual==TRUE&&initial==FALSE,"EVENT_SEMANTICS_CHANGED");if(!fake::plan.eventOk){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return fake::allocate(FakeResource::Event);}
inline BOOL ResetEvent(HANDLE h){fake::require(h&&!h->closed,"RESET_CLOSED_EVENT");fake::require(!fake::op.active,"RESET_ACTIVE_EVENT");++fake::resetCalls;return TRUE;}
inline BOOL WriteFile(HANDLE h,const void* buffer,DWORD bytes,DWORD* count,OVERLAPPED* ov){
 ++fake::writeCalls;fake::require(h&&h!=INVALID_HANDLE_VALUE&&!h->closed,"WRITE_BAD_HANDLE");
 if(!ov){fake::require(count!=nullptr,"SYNC_WRITE_COUNT_NULL");++fake::nonNullCountCalls;if(fake::plan.start==fake::StartError){SetLastError(fake::plan.startError);return FALSE;}*count=fake::plan.syncCount;return TRUE;}
 if(!(h->flags&FILE_FLAG_OVERLAPPED)){++fake::syncBoundaryHits;throw FakeContractFailure("SYNC_HANDLE_BLOCKS_BEFORE_WAIT_BUDGET");}
 if(fake::plan.strictNull)fake::require(count==nullptr,"ASYNC_WRITE_MISLEADING_COUNT_POINTER");
 if(count){++fake::nonNullCountCalls;*count=fake::plan.misleadingAsyncCount;}else ++fake::nullCountCalls;
 fake::require(!fake::op.active,"SECOND_WRITE_WHILE_ACTIVE");
 fake::op={h,ov->hEvent,ov,static_cast<const BYTE*>(buffer),std::vector<BYTE>(static_cast<const BYTE*>(buffer),static_cast<const BYTE*>(buffer)+bytes),false};
 if(fake::plan.start==fake::StartError){SetLastError(fake::plan.startError);return FALSE;}
 fake::op.active=fake::plan.start==fake::Pending || (!fake::plan.results.empty()&&!fake::plan.results.front().ok&&fake::plan.results.front().error==ERROR_IO_INCOMPLETE);
 if(fake::plan.start==fake::Pending){SetLastError(ERROR_IO_PENDING);return FALSE;}return TRUE;
}
inline BOOL GetOverlappedResult(HANDLE h,OVERLAPPED* ov,DWORD* bytes,BOOL wait){
 ++fake::resultCalls;fake::require(wait==FALSE,"UNBOUNDED_COMPLETION_WAIT");fake::checkOp(h,ov);fake::resultsHandles.push_back(h);fake::resultsOverlapped.push_back(ov);
 fake::require(!fake::plan.results.empty(),"UNSCRIPTED_COMPLETION_QUERY");auto r=fake::plan.results.front();fake::plan.results.pop_front();
 if(r.ok){*bytes=r.bytes;fake::op.active=false;return TRUE;}SetLastError(r.error);if(r.error!=ERROR_IO_INCOMPLETE)fake::op.active=false;return FALSE;
}
inline DWORD WaitForSingleObject(HANDLE event,DWORD budget){
 ++fake::waitCalls;fake::waitBudgets.push_back(budget);fake::require(event==fake::op.event&&!event->closed,"WAIT_WRONG_EVENT");fake::require(!fake::plan.waits.empty(),"UNSCRIPTED_OR_EXTRA_WAIT");auto r=fake::plan.waits.front();fake::plan.waits.pop_front();if(r==WAIT_FAILED)SetLastError(ERROR_INVALID_HANDLE);return r;
}
inline BOOL CancelIoEx(HANDLE h,OVERLAPPED* ov){++fake::cancelCalls;fake::checkOp(h,ov);fake::cancelHandles.push_back(h);fake::cancelOverlapped.push_back(ov);if(!fake::plan.cancelOk){SetLastError(fake::plan.cancelError);return FALSE;}return TRUE;}
inline bool msiControllerTraceEnabled(){return fake::trace;}
inline bool ymStartSeamActive(){return false;}
inline std::string W2U(const wchar_t* p){std::string s;while(*p)s.push_back(char(*p++));return s;}
struct FakeLogValue {template<class T> FakeLogValue(const T&){} };
inline void appendNativeLifecycleLog(const char*,std::initializer_list<std::pair<std::string,FakeLogValue>>){++fake::logs;}
