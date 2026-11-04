#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
using BYTE=unsigned char;using DWORD=uint32_t;using USHORT=uint16_t;using ULONG=uint32_t;
using BOOL=int;using ULONGLONG=unsigned long long;
struct FakeHandle { DWORD wait=258,bytes=0,error=996;bool ok=false,closed=false;int kind=0; };
using HANDLE=FakeHandle*;using HDEVINFO=HANDLE;
static HANDLE const INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
struct OVERLAPPED { HANDLE hEvent=nullptr; };
struct GUID{};static const GUID kYeManGuidDeviceInterfaceHid{};
struct SP_DEVICE_INTERFACE_DATA { DWORD cbSize=0; };
struct SP_DEVINFO_DATA { DWORD cbSize=0; };
struct SP_DEVICE_INTERFACE_DETAIL_DATA_W { DWORD cbSize;wchar_t DevicePath[256]; };
struct HIDD_ATTRIBUTES { DWORD Size=0;USHORT VendorID=0,ProductID=0,VersionNumber=0; };
struct HIDP_CAPS { USHORT Usage=0,UsagePage=0,InputReportByteLength=0,OutputReportByteLength=0; };
using PHIDP_PREPARSED_DATA=HIDP_CAPS*;
static constexpr BOOL FALSE=0,TRUE=1;
static constexpr DWORD WAIT_OBJECT_0=0,WAIT_TIMEOUT=258,WAIT_FAILED=0xffffffffu;
static constexpr DWORD ERROR_IO_INCOMPLETE=996,ERROR_OPERATION_ABORTED=995,ERROR_IO_PENDING=997,ERROR_SUCCESS=0;
static constexpr DWORD DIGCF_PRESENT=2,DIGCF_DEVICEINTERFACE=16,FILE_SHARE_READ=1,FILE_SHARE_WRITE=2;
static constexpr DWORD OPEN_EXISTING=3,FILE_ATTRIBUTE_NORMAL=128,FILE_FLAG_OVERLAPPED=0x40000000;
static constexpr DWORD GENERIC_READ=0x80000000,GENERIC_WRITE=0x40000000,HIDP_STATUS_SUCCESS=0x110000;
static ULONGLONG fixtureTick=10000;static DWORD fixtureError=0;
static int fixtureCancels=0,fixtureWaits=0,fixtureResults=0,fixtureCloses=0;
static int fixtureParses=0,fixtureOpened=0,fixtureBindAttempts=0,fixtureInit=0,fixtureStartRead=0;
static int fixtureProbeCloses=0,fixtureAttrQueries=0,fixtureCapsQueries=0,fixtureEventCreates=0;
static std::array<FakeHandle,3> fixtureHandles{};
static FakeHandle fixtureSet{},fixtureProbe{};
static std::vector<json> fixtureLogs;
static int fixtureProducerRevokes=0;static std::array<int,4> fixtureSiblingCloses{};
static void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static ULONGLONG GetTickCount64(){return fixtureTick;}
static DWORD GetLastError(){return fixtureError;}
static DWORD WaitForSingleObject(HANDLE h,DWORD timeout){require(timeout==0,"blocking OS wait forbidden");require(h&&!h->closed,"wait on closed event");++fixtureWaits;return h->wait;}
static BOOL GetOverlappedResult(HANDLE h,OVERLAPPED* ov,DWORD* bytes,BOOL wait){require(!wait,"blocking overlapped result forbidden");require(h!=INVALID_HANDLE_VALUE&&!h->closed,"result on closed device");require(ov&&ov->hEvent&&!ov->hEvent->closed,"result event invalid");++fixtureResults;const auto& e=*ov->hEvent;*bytes=e.bytes;fixtureError=e.error;return e.ok;}
static BOOL CancelIoEx(HANDLE h,OVERLAPPED*){require(h!=INVALID_HANDLE_VALUE&&h&&!h->closed,"cancel on invalid device");++fixtureCancels;return TRUE;}
static BOOL CloseHandle(HANDLE);
static void ZeroMemory(void* p,size_t n){std::memset(p,0,n);}
static BOOL ResetEvent(HANDLE h){require(h&&!h->closed,"reset invalid event");h->wait=WAIT_TIMEOUT;h->ok=false;h->error=ERROR_IO_INCOMPLETE;return TRUE;}
struct FixtureCaps { USHORT vid=0x1a86,pid=0xfe00,page=0xff00,usage=1,input=64,output=64;bool attrsOk=true,prepOk=true,capsOk=true; };
static FixtureCaps fixtureProbeCaps{},fixtureOpenCaps{};
static bool fixtureEnumerate=true,fixtureOpenOk=true;static DWORD fixtureInterfaceCount=1;
static std::wstring fixturePath=L"\\\\?\\hid#vid_1a86&pid_fe00&mi_02#fixture";
static HDEVINFO SetupDiGetClassDevsW(const GUID*,const wchar_t*,void*,DWORD){++fixtureBindAttempts;return fixtureEnumerate?&fixtureSet:INVALID_HANDLE_VALUE;}
static BOOL SetupDiEnumDeviceInterfaces(HDEVINFO,void*,const GUID*,DWORD i,SP_DEVICE_INTERFACE_DATA*){return i<fixtureInterfaceCount;}
static BOOL SetupDiGetDeviceInterfaceDetailW(HDEVINFO,SP_DEVICE_INTERFACE_DATA*,SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail,DWORD,DWORD* bytes,SP_DEVINFO_DATA*){if(bytes)*bytes=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);if(!detail)return FALSE;require(fixturePath.size()<256,"fixture path too long");std::copy(fixturePath.begin(),fixturePath.end(),detail->DevicePath);detail->DevicePath[fixturePath.size()]=0;return TRUE;}
static BOOL SetupDiDestroyDeviceInfoList(HDEVINFO){return TRUE;}
static HANDLE CreateFileW(const wchar_t*,DWORD access,DWORD,void*,DWORD,DWORD,void*){if(!access){fixtureProbe={};fixtureProbe.kind=1;return &fixtureProbe;}if(!fixtureOpenOk)return INVALID_HANDLE_VALUE;++fixtureOpened;fixtureHandles[0]={};return &fixtureHandles[0];}
static HANDLE CreateEventW(void*,BOOL,BOOL,const wchar_t*){require(fixtureEventCreates<2,"more than two I/O events");const size_t i=static_cast<size_t>(++fixtureEventCreates);fixtureHandles[i]={};return &fixtureHandles[i];}
static BOOL HidD_GetAttributes(HANDLE h,HIDD_ATTRIBUTES* a){++fixtureAttrQueries;const auto& c=h==&fixtureProbe?fixtureProbeCaps:fixtureOpenCaps;a->VendorID=c.vid;a->ProductID=c.pid;return c.attrsOk;}
static BOOL HidD_GetPreparsedData(HANDLE h,PHIDP_PREPARSED_DATA* p){const auto& c=h==&fixtureProbe?fixtureProbeCaps:fixtureOpenCaps;static HIDP_CAPS caps;caps={c.usage,c.page,c.input,c.output};*p=c.prepOk?&caps:nullptr;return c.prepOk;}
static ULONG HidP_GetCaps(PHIDP_PREPARSED_DATA p,HIDP_CAPS* out){++fixtureCapsQueries;*out=*p;const auto& c=fixtureCapsQueries==1?fixtureProbeCaps:fixtureOpenCaps;return c.capsOk?HIDP_STATUS_SUCCESS:0;}
static BOOL HidD_FreePreparsedData(PHIDP_PREPARSED_DATA){return TRUE;}
static std::string W2U(const std::wstring&){return "fixture-hid";}
enum class FixtureIo { Sync,Pending,Failure,Short };
static FixtureIo fixtureReadMode=FixtureIo::Pending,fixtureWriteMode=FixtureIo::Sync;
static std::vector<BYTE> fixtureReadBytes;
struct FixtureWrite { ULONGLONG at;std::vector<BYTE> bytes; };
static std::vector<FixtureWrite> fixtureWrites;
static BOOL WriteFile(HANDLE h,const void* p,DWORD n,DWORD* bytes,OVERLAPPED* ov){require(h&&!h->closed&&ov&&ov->hEvent&&!ov->hEvent->closed,"write ownership invalid");fixtureWrites.push_back({fixtureTick,{static_cast<const BYTE*>(p),static_cast<const BYTE*>(p)+n}});*bytes=fixtureWriteMode==FixtureIo::Short?n-1:n;if(fixtureWriteMode==FixtureIo::Pending){fixtureError=ERROR_IO_PENDING;return FALSE;}if(fixtureWriteMode==FixtureIo::Failure){fixtureError=5;return FALSE;}return TRUE;}
static BOOL ReadFile(HANDLE h,void* p,DWORD n,DWORD* bytes,OVERLAPPED* ov){require(h&&!h->closed&&ov&&ov->hEvent&&!ov->hEvent->closed,"read ownership invalid");++fixtureStartRead;if(fixtureReadMode==FixtureIo::Pending){fixtureError=ERROR_IO_PENDING;return FALSE;}if(fixtureReadMode==FixtureIo::Failure){fixtureError=5;return FALSE;}*bytes=fixtureReadMode==FixtureIo::Short?n-1:n;std::memset(p,0,n);std::memcpy(p,fixtureReadBytes.data(),(std::min)(fixtureReadBytes.size(),static_cast<size_t>(*bytes)));return TRUE;}
static void appendNativeLifecycleLog(const char* event,const json& detail){if(std::string(event)=="oem-producer-edge"&&detail.value("status","")=="cancelled")++fixtureProducerRevokes;auto item=detail;item["event"]=event;fixtureLogs.push_back(std::move(item));}
struct FixtureSibling {bool enabled=true;};static FixtureSibling g_legionBack,g_legionSBack,g_gpdBack,g_rogOemKey;
static std::atomic<bool> g_gpdBackEnabledSnap{true};static std::atomic<int> g_oemBackState{0};static std::atomic<uint64_t> g_oemKeyMaskHid{0};
static void legionBackClose(){++fixtureSiblingCloses[0];}static void legionSBackClose(){++fixtureSiblingCloses[1];}static void gpdBackClose(){++fixtureSiblingCloses[2];}static void rogOemKeyClose(){++fixtureSiblingCloses[3];}
// Non-OneX context and the unrelated UI notification are controlled external sinks.
static uint64_t nativeOemKeyboardActionMask(){++fixtureParses;return 0;}
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{1};
static void* g_hwnd=nullptr;static constexpr unsigned WM_OEM_PRODUCER_EDGE=1;
static BOOL PostMessageW(void*,unsigned,uintptr_t,intptr_t){return TRUE;}
