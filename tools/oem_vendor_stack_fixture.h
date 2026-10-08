#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>
#include <algorithm>
#include <iostream>
#include "json.hpp"
using json=nlohmann::json;
/* FAMILY_ENUM */
static constexpr uint64_t kOemBitCc=1ull<<22,kOemBitAc=1ull<<23,kOemBitLib=1ull<<24,kOemBitClaw=1ull<<25,kOemBitQs=1ull<<26,kOemBitZotac=1ull<<47,kOemBitDots=1ull<<48,kOemBitLegionFrontL=1ull<<61,kOemBitLegionFrontR=1ull<<62;
static struct {YmccFamily family=YmccFamily::AsusRogAlly;std::string deviceClass="HandheldCompanion.Devices.ROGAlly";} g_machineIdentity;
static std::atomic<bool> g_exitRequested{false};
static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
static std::wstring U2W(const std::string& s){return std::wstring(s.begin(),s.end());}
static const std::wstring POWER_CONTROL_DIR=L"memory";
static std::vector<json> logs;
static void appendNativeLifecycleLog(const char*,json x){logs.push_back(x);}
struct Service{DWORD type=SERVICE_AUTO_START,state=SERVICE_RUNNING;bool deny=false,stopFail=false,startFail=false,stuck=false,configFail=false,queryFail=false;ULONGLONG settleAt=0;DWORD settleState=SERVICE_RUNNING;DWORD stopDelay=0;};
struct Task {bool enabled=true,running=true,deny=false,setFail=false,stuck=false;};
static std::map<std::wstring,Service> services;
static std::map<std::wstring,Task> tasks;
static std::vector<std::wstring> processes,stoppedProcesses;
static bool processFail=false,scmFail=false,queueFail=false,journalFail=false,journalExists=false;
static std::string journal;
static std::vector<std::function<void()>> jobs;
static std::function<void()> onPersist;
static int mutationCount=0,stops=0,starts=0,taskMutations=0,taskStarts=0,processCalls=0;
static ULONGLONG tick=1;
static SC_HANDLE fakeScm(LPCWSTR,LPCWSTR,DWORD){if(scmFail){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return reinterpret_cast<SC_HANDLE>(1);}
static SC_HANDLE fakeOpen(SC_HANDLE,LPCWSTR name,DWORD){auto i=services.find(name);if(i==services.end()){SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);return nullptr;}if(i->second.deny){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return reinterpret_cast<SC_HANDLE>(&i->second);}
static BOOL fakeClose(SC_HANDLE){return TRUE;}
static BOOL fakeConfigQuery(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW p,DWORD,DWORD* n){auto& v=*reinterpret_cast<Service*>(h);*n=sizeof(QUERY_SERVICE_CONFIGW);if(v.queryFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(!p){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}p->dwStartType=v.type;return TRUE;}
static BOOL fakeStatus(SC_HANDLE h,SC_STATUS_TYPE,LPBYTE p,DWORD,DWORD*){auto& v=*reinterpret_cast<Service*>(h);if(v.queryFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(v.settleAt&&tick>=v.settleAt){v.state=v.settleState;v.settleAt=0;}reinterpret_cast<SERVICE_STATUS_PROCESS*>(p)->dwCurrentState=v.state;return TRUE;}
static BOOL fakeChange(SC_HANDLE h,DWORD,DWORD t,DWORD,LPCWSTR,LPCWSTR,LPDWORD,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR){auto& v=*reinterpret_cast<Service*>(h);if(v.configFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}v.type=t;++mutationCount;return TRUE;}
static BOOL fakeControl(SC_HANDLE h,DWORD control,LPSERVICE_STATUS){auto& v=*reinterpret_cast<Service*>(h);++stops;if(v.stopFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(!v.stuck){if(v.stopDelay){v.state=SERVICE_STOP_PENDING;v.settleAt=tick+v.stopDelay;v.settleState=SERVICE_STOPPED;}else v.state=control==SERVICE_CONTROL_PAUSE?SERVICE_PAUSED:SERVICE_STOPPED;}return TRUE;}
static BOOL fakeStart(SC_HANDLE h,DWORD,LPCWSTR*){auto& v=*reinterpret_cast<Service*>(h);++starts;if(v.startFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}v.state=SERVICE_RUNNING;return TRUE;}
static DWORD fakeAttrs(LPCWSTR){if(journalExists)return FILE_ATTRIBUTE_NORMAL;SetLastError(ERROR_FILE_NOT_FOUND);return INVALID_FILE_ATTRIBUTES;}
static bool sgWriteFileAtomic(const std::wstring&,const std::string& s){if(journalFail)return false;journal=s;journalExists=true;if(onPersist){auto f=std::move(onPersist);onPersist={};f();}return true;}
static std::string sgReadFile(const std::wstring&){return journal;}
static bool gamepadSerialSubmit(std::function<void()> f){if(queueFail || g_exitRequested)return false;jobs.push_back(f);return true;}
#define OpenSCManagerW fakeScm
#define OpenServiceW fakeOpen
#define CloseServiceHandle fakeClose
#define QueryServiceConfigW fakeConfigQuery
#define QueryServiceStatusEx fakeStatus
#define ChangeServiceConfigW fakeChange
#define ControlService fakeControl
#define StartServiceW fakeStart
#define GetFileAttributesW fakeAttrs
#define GetTickCount64() tick
#define Sleep(ms) (tick += (ms))
static void oemVendorStackRestore(const char*);
