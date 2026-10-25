#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>
#include "http_request_deadline.h"
#include "fan_http_body_contract.h"
#include "fan_resume_evidence.h"
#include "fan_resume_owner.h"
#include "fan_response_contract.h"
#pragma comment(lib,"winhttp.lib")
using json=nlohmann::json;
INTERNET_PORT fixturePort=0;
std::string fixtureCase;
int authRejected=0,headerFailed=0,leaseArms=0;
std::vector<json> lifecycle;
std::atomic<bool> g_exitRequested{false};
std::atomic<unsigned long long> g_fanResumeRecoveryGeneration{0};
std::atomic<int> g_powerLifecycle{1};
ymcc::fan_resume::RequestOwner g_fanResumeRequestOwner;
std::string g_exitSessionId="fixture-exit";
std::wstring U2W(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring out(static_cast<size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
std::string W2U(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
std::string ascii_lower(std::string value){for(char& ch:value)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));return value;}
std::string fanHostReadSessionToken(){return fixtureCase=="missing-token"?std::string{}:"fixture-"+fixtureCase;}
bool fanHostExactProcessRunning(){return fixtureCase!="wrong-process";}
template<class...Args>void traceLog(Args&&...){}
template<class...Args>void fanHostNoteCredentialUnavailable(Args&&...){}
template<class...Args>void fanHostIdentityNoteUnverified(Args&&...){}
template<class...Args>void fanHostNoteAuthRejected(Args&&...){++authRejected;}
template<class...Args>void fanHostNoteRequestHeaderFailed(Args&&...){++headerFailed;}
std::string fanRequestId(const char*){return "fixture-request";}
std::string fanErrorCodeFromBody(const std::string& b){auto p=json::parse(b,nullptr,false);return p.is_object()&&p.contains("error")&&p["error"].is_object()?p["error"].value("code",std::string{}):std::string{};}
void setHttpTimeouts(HINTERNET h,int ms){WinHttpSetTimeouts(h,ms,ms,ms,ms);}
void appendFanLifecycleLog(const char* name,const json& detail){lifecycle.push_back(json{{"event",name},{"details",detail}});}
unsigned long long currentFanPowerGeneration(){return 1;}
bool fanHostPowerMayResume(){return true;}
bool fanActivityIntentPresent(){return true;}
void fanHostScheduleFanResume(unsigned long long){}
void fanLeaseKeepaliveArm(const std::string&,const char*,ULONGLONG=0){++leaseArms;}
struct FixtureDisplay{ULONGLONG epoch(){return 1;}} g_monitorFanDisplay;
int availabilityQueries=0;
BOOL countedDataAvailable(HINTERNET h,LPDWORD count){++availabilityQueries;return WinHttpQueryDataAvailable(h,count);}
#define WinHttpQueryDataAvailable countedDataAvailable
#include "PowerExtract.inc"
#undef WinHttpQueryDataAvailable
int main(int argc,char** argv){
 if(argc!=4)return 9;fixturePort=static_cast<INTERNET_PORT>(std::stoi(argv[1]));const std::string mode=argv[2];fixtureCase=argv[3];
 const auto started=GetTickCount64();DWORD status=999;std::string body="must-clear";bool ok=false;
 if(mode=="get")ok=fanHostEmergencyGetState(&body,&status);
 else if(mode=="suspend")ok=fanHostEmergencySuspend("fixture",1);
 else if(mode=="resume"||mode=="resume-retry"){
  fanHostEmergencyResume(1);
  if(mode=="resume-retry"){fixtureCase="normal";fanHostEmergencyResume(1);}
  if(!lifecycle.empty())for(const auto& row:lifecycle)if(row["event"]=="fan-host-resume-native-trigger")ok=row["details"].value("terminal",std::string{})=="recovered";
 }
 else ok=fanHostEmergencyPost(mode=="post-safe"?L"/api/close":L"/api/resume","fixture",2,mode=="post-safe",&status,1,"native.power",&body);
 std::cout<<json{{"ok",ok},{"status",status},{"bodyBytes",body.size()},{"bodyParsed",!body.empty()&&!json::parse(body,nullptr,false).is_discarded()},{"elapsedMs",GetTickCount64()-started},{"availabilityQueries",availabilityQueries},{"authRejected",authRejected},{"headerFailed",headerFailed},{"leaseArms",leaseArms},{"lifecycle",lifecycle}}.dump()<<"\n";return 0;
}
