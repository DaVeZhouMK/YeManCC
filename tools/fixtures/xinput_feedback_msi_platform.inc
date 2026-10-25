// Controlled Win32/HID boundary. Original source/lease/open/frame/write bodies execute above it.
using HANDLE=void*;using BOOL=int;using USHORT=std::uint16_t;using ULONG=std::uint32_t;
#define INVALID_HANDLE_VALUE ((HANDLE)(std::intptr_t)-1)
static constexpr BOOL TRUE=1,FALSE=0;
static constexpr DWORD ERROR_IO_INCOMPLETE=996,ERROR_IO_PENDING=997,ERROR_OPERATION_ABORTED=995,ERROR_DEVICE_NOT_CONNECTED=1167,ERROR_NOT_ENOUGH_MEMORY=8,ERROR_WRITE_FAULT=29,ERROR_TIMEOUT=1460,ERROR_INVALID_PARAMETER=87;
static constexpr DWORD WAIT_OBJECT_0=0,WAIT_FAILED=0xffffffffu,WAIT_TIMEOUT=258;
static constexpr DWORD GENERIC_READ=0x80000000u,GENERIC_WRITE=0x40000000u,FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,OPEN_EXISTING=3,FILE_ATTRIBUTE_NORMAL=128,FILE_FLAG_OVERLAPPED=0x40000000u;
struct OVERLAPPED{HANDLE hEvent=nullptr;};
struct HIDD_ATTRIBUTES{ULONG Size=0;USHORT VendorID=0,ProductID=0,VersionNumber=0;};
struct HIDP_CAPS{USHORT Usage=0,UsagePage=0,OutputReportByteLength=0;};
using PHIDP_PREPARSED_DATA=void*;static constexpr int HIDP_STATUS_SUCCESS=0x110000;
static json fixtureMsiNativeReports=json::array(),fixtureMsiSdkReports=json::array();
static unsigned fixtureMsiOpens=0,fixtureMsiCloses=0,fixtureMsiCancels=0,fixtureMsiWaits=0,fixtureMsiSdkCalls=0;
static DWORD fixtureMsiLastError=0,fixtureMsiVid=0x0db0,fixtureMsiPid=0x1902,fixtureMsiUsagePage=1,fixtureMsiUsage=5,fixtureMsiReportLength=32;
static bool fixtureMsiAttributes=true,fixtureMsiCaps=true,fixtureMsiAuthority=true,fixtureMsiCancelled=false;
enum class FixtureMsiIo {Sync,Short,Failed,Timeout,Incomplete};static FixtureMsiIo fixtureMsiIo=FixtureMsiIo::Sync;
static std::vector<BYTE> fixtureMsiLastBuffer;static std::uintptr_t fixtureMsiNextHandle=10;
static std::function<void()> fixtureMsiBeforeOpen;
static DWORD GetLastError(){return fixtureMsiLastError;}
static std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
static HANDLE CreateFileW(const wchar_t*,DWORD,DWORD,void*,DWORD,DWORD,void*){++fixtureMsiSdkCalls;++fixtureMsiOpens;if(fixtureMsiBeforeOpen){auto action=std::move(fixtureMsiBeforeOpen);fixtureMsiBeforeOpen={};action();}return (HANDLE)++fixtureMsiNextHandle;}
static BOOL HidD_GetAttributes(HANDLE,HIDD_ATTRIBUTES* a){++fixtureMsiSdkCalls;a->VendorID=(USHORT)fixtureMsiVid;a->ProductID=(USHORT)fixtureMsiPid;return fixtureMsiAttributes;}
static BOOL HidD_GetPreparsedData(HANDLE,PHIDP_PREPARSED_DATA* p){++fixtureMsiSdkCalls;*p=(void*)1;return fixtureMsiCaps;}
static int HidP_GetCaps(PHIDP_PREPARSED_DATA,HIDP_CAPS* c){++fixtureMsiSdkCalls;c->UsagePage=(USHORT)fixtureMsiUsagePage;c->Usage=(USHORT)fixtureMsiUsage;c->OutputReportByteLength=(USHORT)fixtureMsiReportLength;return fixtureMsiCaps?HIDP_STATUS_SUCCESS:0;}
static void HidD_FreePreparsedData(PHIDP_PREPARSED_DATA){++fixtureMsiSdkCalls;}
static HANDLE CreateEventW(void*,BOOL,BOOL,const wchar_t*){++fixtureMsiSdkCalls;return (HANDLE)++fixtureMsiNextHandle;}
static BOOL CloseHandle(HANDLE){++fixtureMsiSdkCalls;++fixtureMsiCloses;return TRUE;}
static BOOL ResetEvent(HANDLE){++fixtureMsiSdkCalls;fixtureMsiCancelled=false;return TRUE;}
static BOOL WriteFile(HANDLE,const void* p,DWORD size,DWORD*,OVERLAPPED*){++fixtureMsiSdkCalls;fixtureMsiLastBuffer.assign((const BYTE*)p,(const BYTE*)p+size);fixtureMsiSdkReports.push_back(fixtureMsiLastBuffer);if(fixtureMsiIo==FixtureMsiIo::Failed){fixtureMsiLastError=5;return FALSE;}if(fixtureMsiIo==FixtureMsiIo::Timeout||fixtureMsiIo==FixtureMsiIo::Incomplete){fixtureMsiLastError=ERROR_IO_PENDING;return FALSE;}return TRUE;}
static BOOL CancelIoEx(HANDLE,OVERLAPPED*){++fixtureMsiSdkCalls;++fixtureMsiCancels;fixtureMsiCancelled=true;return TRUE;}
static BOOL GetOverlappedResult(HANDLE,OVERLAPPED*,DWORD* bytes,BOOL){++fixtureMsiSdkCalls;if(fixtureMsiIo==FixtureMsiIo::Incomplete){fixtureMsiLastError=ERROR_IO_INCOMPLETE;return FALSE;}if(fixtureMsiIo==FixtureMsiIo::Timeout){fixtureMsiLastError=fixtureMsiCancelled?ERROR_OPERATION_ABORTED:ERROR_IO_INCOMPLETE;return FALSE;}*bytes=(DWORD)fixtureMsiLastBuffer.size()-(fixtureMsiIo==FixtureMsiIo::Short?1:0);return TRUE;}
static DWORD WaitForSingleObject(HANDLE,DWORD){++fixtureMsiSdkCalls;++fixtureMsiWaits;return WAIT_TIMEOUT;}
