#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <functional>
#include <iostream>
#include <thread>
#include "http_request_deadline.h"
#include "fan_response_contract.h"
#if __has_include("fan_http_body_contract.h")
#include "fan_http_body_contract.h"
#endif
#pragma comment(lib,"winhttp.lib")
using json=nlohmann::json;
INTERNET_PORT fixturePort=0;
int displayObserved=0,activityAccepted=0,armed=0,disarmed=0,authRejected=0,headerConversions=0;
std::atomic<bool> g_poolCancel{false};
std::function<json(const json&)> fixtureHandler;
void ipc_on(const std::string&,std::function<json(const json&)> handler){fixtureHandler=std::move(handler);}
std::wstring U2W(const std::string& s){if(s.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring r(static_cast<size_t>(n),0);MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n);return r;}
std::string W2U(const std::wstring& s){if(s.rfind(L"HTTP/",0)==0)headerConversions++;if(s.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(static_cast<size_t>(n),0);WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::string ascii_lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');return s;}
bool isSteamHostName(const wchar_t*){return false;}
bool isLoopbackHost(const wchar_t* host){return host&&(wcscmp(host,L"127.0.0.1")==0||wcscmp(host,L"localhost")==0);}
bool is_http_token(const std::string& s){return !s.empty()&&s.find_first_of(" \r\n\t")==std::string::npos;}
bool has_header_injection_chars(const std::string& s){return s.find_first_of("\r\n")!=std::string::npos;}
void setHttpTimeouts(HINTERNET h,DWORD n){WinHttpSetTimeouts(h,static_cast<int>(n),static_cast<int>(n),static_cast<int>(n),static_cast<int>(n));}
struct AiMock{bool enabled=false;}g_aiFanMockSession;
struct MockHost{DWORD livePid(){return 0;}}g_aiFanMockHost;
bool fanHostOwnsLoopbackPort(DWORD,int){return false;}
namespace ymcc::monitorfan {inline bool isMutation(const std::string& p){return p!="/api/state"&&p!="/api/handshake";}}
struct Monitor {ULONGLONG invalidate(){return 1;}ULONGLONG epoch(){return 1;}void observe(ULONGLONG,DWORD,const json&,long long){displayObserved++;}}g_monitorFanDisplay;
double sgNowEpoch(){return 1700000000.;}
std::string fanHeaderRequestIdFromRawHeaders(const std::string&){return "fixture-request";}
std::string fanErrorCodeFromBody(const std::string& s){const auto j=json::parse(s,nullptr,false);return j.is_object()&&j.contains("error")&&j["error"].is_object()?j["error"].value("code",std::string{}):std::string{};}
std::string fanRequestId(const char*){return "fixture-request";}
template<class...Args>void fanHostNoteAuthRejected(Args&&...){authRejected++;}
std::string parseFanLeaseIdText(const std::string& s){const auto j=json::parse(s,nullptr,false);return j.is_object()?j.value("leaseId",std::string{}):std::string{};}
bool isFanLeaseIdValid(const std::string& s){return ymcc::fan_response::leaseIdValid(s);}
void fanLeaseKeepaliveArm(const std::string&,const char*,ULONGLONG){armed++;}
void fanLeaseKeepaliveDisarm(const char*){disarmed++;}
void fanActivityObserve(const std::string& path,const std::string&,DWORD status,const std::string& body,const json* supplied,ULONGLONG){json fallback;const json* value=supplied;if(!value){fallback=json::parse(body,nullptr,false);value=&fallback;}if(ymcc::fan_response::accepted(path,status,*value))activityAccepted++;}
static std::string fanApiPathFromObject(const std::wstring& p){auto s=W2U(p);const auto q=s.find('?');if(q!=std::string::npos)s.resize(q);return s;}
static void fanLeaseKeepaliveObserveHttp(const std::string&,const wchar_t*,unsigned short,const std::wstring&,const std::string&,DWORD,const std::string&,bool,const std::string&,const std::string&,const json*,ULONGLONG);
#include "HttpExtract.inc"
int main(int argc,char** argv){if(argc!=4)return 9;const auto actualPort=static_cast<INTERNET_PORT>(std::stoi(argv[1]));const std::string kind=argv[2],mode=argv[3];fixturePort=mode=="nonfan"?1:actualPort;reg_http();std::thread stop;if(kind=="cancel-body")stop=std::thread([]{Sleep(80);g_poolCancel=true;});const auto begin=GetTickCount64();json result;try{result=fixtureHandler({{"url","http://127.0.0.1:"+std::to_string(actualPort)+"/api/"+(mode=="close"?"close":mode=="get"?"state":"enable")+"?kind="+kind},{"method",mode=="head"?"HEAD":mode=="get"?"GET":"POST"},{"headers",{{"X-YeMan-Fan-Session","fixture-session"}}},{"body",R"({"leaseId":"fixture-lease-0001","nodes":[{"tempC":0,"dutyPercent":100},{"tempC":100,"dutyPercent":100}]})"},{"timeoutMs",600}});}catch(const std::exception& e){result={{"threw",true},{"error",e.what()}};}if(stop.joinable())stop.join();result["elapsed"]=GetTickCount64()-begin;result["headerConversions"]=headerConversions;result["displayObserved"]=displayObserved;result["activityAccepted"]=activityAccepted;result["armed"]=armed;result["disarmed"]=disarmed;result["authRejected"]=authRejected;std::cout<<result.dump()<<"\n";}
