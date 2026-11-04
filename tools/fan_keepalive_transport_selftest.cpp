// FAN981: exact production Fan keepalive function, real WinHTTP and loopback server.
// Credential/process/display/native mode hooks are fixtures; no FanHost/HC/device.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <utility>
#include "http_request_deadline.h"
#include "fan_response_contract.h"
// Meter only real parent handle setup/retirement; request and callbacks stay real WinHTTP.
int sessionSetups=0,connectionSetups=0,parentCloses=0;
HINTERNET MeterSession(LPCWSTR agent,DWORD access,LPCWSTR proxy,LPCWSTR bypass,DWORD flags){++sessionSetups;return ::WinHttpOpen(agent,access,proxy,bypass,flags);}
HINTERNET MeterConnection(HINTERNET session,LPCWSTR host,INTERNET_PORT port,DWORD reserved){++connectionSetups;return ::WinHttpConnect(session,host,port,reserved);}
BOOL MeterParentClose(HINTERNET value){++parentCloses;return ::WinHttpCloseHandle(value);}
#define WinHttpOpen MeterSession
#define WinHttpConnect MeterConnection
#define WinHttpCloseHandle MeterParentClose
#include "fan_keepalive_transport.h"
#undef WinHttpOpen
#undef WinHttpConnect
#undef WinHttpCloseHandle
#pragma comment(lib,"winhttp.lib")
using json=nlohmann::json;
INTERNET_PORT fixturePort=0;
std::string fixtureCase;
int authRejected=0,headerFailed=0,displayObserved=0,credentialReads=0,identityReads=0;
std::uint64_t fixtureOwnerRevision=1;
struct FixtureSelection {std::uint64_t revision()const{return fixtureOwnerRevision;}};
FixtureSelection& fanKeepaliveProcessSelection(){static FixtureSelection selection;return selection;}
std::atomic<bool> g_fanLeaseKeepaliveStop{false},g_exitRequested{false};
struct FixtureMonitor {ULONGLONG epoch(){return 1;}void observe(ULONGLONG,DWORD,const json&,long long){++displayObserved;}} g_monitorFanDisplay;
std::string fanHostReadSessionToken(){++credentialReads;return fixtureCase=="missing-token"?std::string{}:"fixture-token-"+fixtureCase;}
bool fanHostExactProcessRunning(){++identityReads;return fixtureCase!="wrong-identity";}
bool fanHostExactProcessRunningForKeepalive(){return fanHostExactProcessRunning();}
void fanHostNoteCredentialUnavailable(const char*){}
void fanHostIdentityNoteUnverified(const char*){}
void setHttpTimeouts(HINTERNET h,DWORD n){WinHttpSetTimeouts(h,static_cast<int>(n),static_cast<int>(n),static_cast<int>(n),static_cast<int>(n));}
std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
void fanLeaseKeepaliveAddMsiMode(json&){}
std::string fanHostResponseRequestId(HINTERNET){return "fixture-request";}
double sgNowEpoch(){return 1700000000.;}
std::string fanErrorCodeFromBody(const std::string& s){auto p=json::parse(s,nullptr,false);if(!p.is_object()||!p.contains("error")||!p["error"].is_object())return {};return p["error"].value("code",std::string{});}
std::string fanRequestId(const char*){return "fixture-request";}
template<class...Args>void fanHostNoteAuthRejected(Args&&...){++authRejected;}
template<class...Args>void fanHostNoteRequestHeaderFailed(Args&&...){++headerFailed;}
#include "KeepaliveExtract.inc"
int main(int argc,char** argv){
 if(argc!=3)return 9;fixturePort=static_cast<INTERNET_PORT>(std::stoi(argv[1]));fixtureCase=argv[2];
 const auto began=GetTickCount64();std::thread cancel;
 if(fixtureCase=="cancel-headers"||fixtureCase=="cancel-body"||fixtureCase=="exit-body"){
  cancel=std::thread([]{Sleep(80);if(fixtureCase=="exit-body")g_exitRequested=true;else g_fanLeaseKeepaliveStop=true;});
 }
 std::string lease="must-be-cleared",code="must-be-cleared";DWORD status=999;
 bool ok=fanLeaseKeepalivePostHeartbeat("fixture-lease-0001",&lease,&status,&code);
 const std::string scenario=fixtureCase;int acceptedCalls=ok?1:0;bool lastCallOk=ok;bool middleRefusalClearedLease=false;
 if(scenario=="session-reuse"||scenario=="owner-replaced"||scenario=="retry-after-denial"||scenario=="idle-reset"||scenario=="missing-token-after-warm"||scenario=="identity-after-warm"){
  for(int i=1;i<3;i++){
   if(scenario=="owner-replaced"&&i==1)++fixtureOwnerRevision;
   if(scenario=="idle-reset"&&i==1)ymcc::fan_http::currentThreadTransport().reset();
   if(scenario=="retry-after-denial")fixtureCase=i==1?"denied":"normal";
   if(scenario=="missing-token-after-warm")fixtureCase=i==1?"missing-token":"normal";
   if(scenario=="identity-after-warm")fixtureCase=i==1?"wrong-identity":"normal";
   lastCallOk=fanLeaseKeepalivePostHeartbeat("fixture-lease-0001",&lease,&status,&code);
   if(i==1)middleRefusalClearedLease=!lastCallOk&&lease.empty();
   if(lastCallOk)++acceptedCalls;
  }
 }
 if(cancel.joinable())cancel.join();
 const auto elapsed=GetTickCount64()-began;
 bool retryOk=false;
 if(fixtureCase=="slow-body"){fixtureCase="normal";retryOk=fanLeaseKeepalivePostHeartbeat("fixture-lease-0001",&lease,&status,&code);}
 std::cout<<json{{"ok",ok},{"elapsed",elapsed},{"lease",lease},{"code",code},{"status",status},{"authRejected",authRejected},{"headerFailed",headerFailed},{"displayObserved",displayObserved},{"retryOk",retryOk},{"acceptedCalls",acceptedCalls},{"lastCallOk",lastCallOk},{"credentialReads",credentialReads},{"identityReads",identityReads},{"sessionSetups",sessionSetups},{"connectionSetups",connectionSetups},{"parentCloses",parentCloses},{"middleRefusalClearedLease",middleRefusalClearedLease}}.dump()<<"\n";
 return 0;
}
