#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <cstring>
#include "json.hpp"
using json=nlohmann::json;
#include "msi_foundation_process_fixture.h"
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
struct Facts{std::wstring display=L"MSI Test",binary=L"C:\\MSI\\controller.exe";DWORD type=SERVICE_DEMAND_START,state=SERVICE_RUNNING,pid=42;bool denied=false,configDenied=false,statusDenied=false,invalidSize=false;};
static std::map<std::wstring,Facts> services;
static std::vector<std::wstring> enumeration;
static bool scmDenied=false,enumDenied=false,enumNeverCompletes=false;
static unsigned enumChunk=1024,enumCalls=0,opens=0,closes=0;
static bool unsafeAccess=false;static ULONGLONG mono=555;
static SC_HANDLE fakeScm(LPCWSTR,LPCWSTR,DWORD rights){if(rights&~(SC_MANAGER_CONNECT|SC_MANAGER_ENUMERATE_SERVICE))unsafeAccess=true;if(scmDenied){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}++opens;return reinterpret_cast<SC_HANDLE>(1);}
static SC_HANDLE fakeOpen(SC_HANDLE,LPCWSTR name,DWORD rights){if(rights!=(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS))unsafeAccess=true;auto i=services.find(name);if(i==services.end()){SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);return nullptr;}if(i->second.denied){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}++opens;return reinterpret_cast<SC_HANDLE>(&i->second);}
static BOOL fakeClose(SC_HANDLE){++closes;return TRUE;}
static BOOL fakeStatus(SC_HANDLE h,SC_STATUS_TYPE,LPBYTE data,DWORD,DWORD* bytes){auto& f=*reinterpret_cast<Facts*>(h);if(f.statusDenied){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}auto* s=reinterpret_cast<SERVICE_STATUS_PROCESS*>(data);s->dwCurrentState=f.state;s->dwProcessId=f.pid;s->dwServiceType=SERVICE_WIN32_OWN_PROCESS;*bytes=sizeof(*s);return TRUE;}
static BOOL fakeConfig(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW data,DWORD size,DWORD* bytes){auto& f=*reinterpret_cast<Facts*>(h);if(f.configDenied){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}*bytes=f.invalidSize?2*1024*1024:sizeof(*data)+64*sizeof(wchar_t);if(!data||size<*bytes){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}data->dwStartType=f.type;data->lpBinaryPathName=f.binary.data();data->lpDisplayName=f.display.data();auto* deps=reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(data)+sizeof(*data));const wchar_t values[]=L"hid_dependency\0base_service\0";memcpy(deps,values,sizeof(values));data->lpDependencies=deps;return TRUE;}
static BOOL fakeEnum(SC_HANDLE,SC_ENUM_TYPE,DWORD type,DWORD state,LPBYTE data,DWORD size,LPDWORD needed,LPDWORD count,LPDWORD resume,LPCWSTR){++enumCalls;if(type!=SERVICE_WIN32||state!=SERVICE_STATE_ALL)unsafeAccess=true;if(enumDenied){SetLastError(ERROR_ACCESS_DENIED);*count=0;return FALSE;}if(enumNeverCompletes){*count=0;*needed=1024;*resume=enumCalls;SetLastError(ERROR_MORE_DATA);return FALSE;}const size_t from=*resume;const size_t num=std::min<size_t>({enumeration.size()-from,enumChunk,size/sizeof(ENUM_SERVICE_STATUS_PROCESSW)});auto* rows=reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(data);for(size_t n=0;n<num;++n){auto& key=enumeration[from+n];rows[n].lpServiceName=key.data();rows[n].lpDisplayName=services[key].display.data();}*count=static_cast<DWORD>(num);*resume=static_cast<DWORD>(from+num);*needed=0;if(*resume<enumeration.size()){SetLastError(ERROR_MORE_DATA);return FALSE;}*resume=0;return TRUE;}
#define OpenSCManagerW fakeScm
#define OpenServiceW fakeOpen
#define CloseServiceHandle fakeClose
#define QueryServiceStatusEx fakeStatus
#define QueryServiceConfigW fakeConfig
#define EnumServicesStatusExW fakeEnum
#define GetTickCount64() mono
/* PRODUCTION_SERVICE_OBSERVATION */
static int cases=0,failures=0;
static void check(bool pass,const char* text){++cases;if(!pass)++failures;std::cout<<(pass?"PASS ":"FAIL ")<<text<<"\n";}
static void reset(){FoundationMemory::reset();services.clear();enumeration.clear();scmDenied=enumDenied=enumNeverCompletes=unsafeAccess=false;enumChunk=1024;enumCalls=opens=closes=0;}
int main(){
 reset();services[L"MSI Foundation Service"]={};FoundationMemory::add(42,4,100);FoundationMemory::add(100,42,200);auto p=msiShieldReadProtectedService();check(p["readbackVerified"]==true,"protected service actually queried");check(p["serviceState"]==SERVICE_RUNNING&&p["startType"]==SERVICE_DEMAND_START,"actual status and startup type retained");check(p["binaryPath"]=="C:\\MSI\\controller.exe"&&p["processId"]==42,"service process and binary path recorded");check(p["dependencies"].size()==2,"dependency MULTI_SZ recorded");check(p["controllerOnlyRoleVerified"]==false&&p["mutationPolicy"]=="preserve-unverified-dependency","readback never proves a controller-only role");check(!unsafeAccess&&opens==closes,"query-only service/SCM rights and balanced handles");check(services.begin()->second.state==SERVICE_RUNNING&&services.begin()->second.type==SERVICE_DEMAND_START,"no service mutation");
 check(p["foundationProcessObservation"]["complete"]==true&&p["foundationProcessObservation"]["children"].size()==1,"protected observation invokes actual Foundation helper through memory process boundaries");check(p["foundationProcessObservation"]["observationPhase"]=="after-existing-process-isolation","protected observation marks existing after worker phase");check(FoundationMemory::opens==FoundationMemory::closes&&!FoundationMemory::unsafeRights,"new process helper uses only fake query boundaries and releases handles");
 reset();p=msiShieldReadProtectedService();check(p["existsKnown"]==true&&p["exists"]==false,"missing is positively distinguished from unreadable");
 reset();services[L"MSI Foundation Service"].denied=true;p=msiShieldReadProtectedService();check(p["existsKnown"]==false&&p["win32Error"]==ERROR_ACCESS_DENIED,"denied is not absence");
 reset();scmDenied=true;p=msiShieldReadProtectedService();check(p["readbackVerified"]==false&&p["existsKnown"]==false,"SCM access denial is not verification");
 reset();services[L"MSI Foundation Service"].statusDenied=true;p=msiShieldReadProtectedService();check(p["configRead"]==true&&p["stateRead"]==false&&p["readbackVerified"]==false,"partial config/status failures retained");
 reset();services[L"MSI Foundation Service"].configDenied=true;p=msiShieldReadProtectedService();check(p["stateRead"]==true&&p["configRead"]==false&&p["readbackVerified"]==false,"config denial is not startup readback");
 reset();services[L"MSI Foundation Service"].invalidSize=true;p=msiShieldReadProtectedService();check(p["configRead"]==false&&p["configWin32Error"]==ERROR_INVALID_DATA,"oversized config bounded");
 reset();services[L"MSI Foundation Service"].state=SERVICE_STOPPED;p=msiShieldReadProtectedService();check(p["serviceState"]==SERVICE_STOPPED,"stopped protected service is not reported running");
 reset();FoundationMemory::add(42,4,100);FoundationMemory::add(100,42,200);services[L"MSI Foundation Service"]={};services[L"MSI_Unknown"]={};services[L"MicrosoftInput"].display=L"Microsoft Input";services[L"ArmouryCrate"].display=L"ASUS Service";enumeration={L"MSI Foundation Service",L"MSI_Unknown",L"MicrosoftInput",L"ArmouryCrate"};auto inv=msiShieldReadServiceInventory();check(inv["complete"]==true&&inv["candidateCount"]==2,"only MSI service candidates recorded");check(inv["discoveryAuthorizesMutation"]==false&&inv["controllerOnlyServiceIdentified"]==false,"discovery never grants stop authority");check(inv["observedAtMonoMs"]==mono&&inv["services"][0]["candidateOnly"]==true,"inventory timestamp and candidate-only provenance");check(!unsafeAccess&&opens==closes,"inventory uses enumeration/query rights only and balances handles");check(services[L"MSI_Unknown"].state==SERVICE_RUNNING,"unknown candidate unchanged");
 check(FoundationMemory::processOpens==2&&FoundationMemory::snapshots==2&&inv["services"][0]["foundationProcessObservation"]["observationPhase"]=="before-existing-process-isolation"&&!inv["services"][1].contains("foundationProcessObservation"),"inventory observes exact Foundation once before isolation, unknown candidate has no process calls");
 p=msiShieldReadProtectedService();check(FoundationMemory::processOpens==4&&FoundationMemory::snapshots==4&&p["foundationProcessObservation"]["observationPhase"]=="after-existing-process-isolation","before and after invoke one bounded helper each on existing worker phases");
 FoundationMemory::processes[100]->alive=false;p=msiShieldReadProtectedService();check(inv["services"][0]["foundationProcessObservation"]["children"].size()==1&&p["foundationProcessObservation"]["children"].empty(),"before retains child relation even when an existing isolation action precedes after observation");
 enumChunk=1;enumCalls=0;unsigned phaseCalls=FoundationMemory::processOpens;inv=msiShieldReadServiceInventory();check(FoundationMemory::processOpens==phaseCalls+1,"pagination invokes exact Foundation process observation once");check(inv["complete"]==true&&enumCalls==4&&inv["candidateCount"]==2,"resume pagination completes without drops");enumeration.push_back(L"MSI Foundation Service");phaseCalls=FoundationMemory::processOpens;inv=msiShieldReadServiceInventory();check(inv["candidateCount"]==2&&FoundationMemory::processOpens==phaseCalls+1,"duplicate Foundation entries do not invoke another process snapshot");
 reset();services[L"MSI_Unknown"].display=L"MSI Foundation Service";enumeration={L"MSI_Unknown"};inv=msiShieldReadServiceInventory();check(FoundationMemory::processOpens==0&&FoundationMemory::snapshots==0&&!inv["services"][0].contains("foundationProcessObservation"),"Foundation display name on a different SCM key grants no process observation");
 reset();enumNeverCompletes=true;inv=msiShieldReadServiceInventory();check(inv["complete"]==false&&inv["truncated"]==true&&enumCalls==8,"nonterminating enumeration capped at eight pages");
 reset();for(unsigned n=0;n<40;++n){auto name=L"MSI_"+std::to_wstring(n);services[name]={};enumeration.push_back(name);}inv=msiShieldReadServiceInventory();check(inv["complete"]==false&&inv["truncated"]==true&&inv["candidateCount"]==32,"candidate output bounded at 32 with explicit truncation");
 reset();enumDenied=true;inv=msiShieldReadServiceInventory();check(inv["complete"]==false&&inv["win32Error"]==ERROR_ACCESS_DENIED,"enumeration failure is not empty success");
 reset();scmDenied=true;inv=msiShieldReadServiceInventory();check(inv["complete"]==false&&inv["win32Error"]==ERROR_ACCESS_DENIED,"SCM inventory failure explicit");
 check(msiShieldServiceCandidate(L"msi_test",L""),"MSI candidate case-insensitive");check(!msiShieldServiceCandidate(L"msiserver",L"Windows Installer"),"Windows Installer not mistaken for MSI OEM");check(!msiShieldServiceCandidate(L"MicrosoftService",L"Microsoft Service"),"Microsoft prefix not mistaken for MSI OEM");check(msiShieldServiceCandidate(L"UnknownExactName",L"Micro-Star Controller"),"display publisher candidate remains observational only");
 std::cout<<"cases="<<cases<<" failures="<<failures<<" liveServiceCalls=0 liveProcessCalls=0 RESULT="<<(failures?"FAIL":"PASS")<<"\n";return failures?1:0;
}
