// Actual native Fan HTTP response stage and observer; no network/Host/HC/device.
#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>
#include "fan_response_contract.h"
using json=nlohmann::json;using DWORD=unsigned long;using ULONGLONG=unsigned long long;
unsigned checks=0,headerIdCalls=0,headerNameCopies=0,observerCalls=0,directParses=0,fallbackParses=0;
unsigned credentialCopyCalls=0;size_t credentialBytesCopied=0;
std::string FixtureCredentialCopy(const json& value){++credentialCopyCalls;credentialBytesCopied+=value.get_ref<const std::string&>().size();return value.get<std::string>();}
size_t loweredRawHeaderBytes=0;std::vector<std::string> failures;json trace=json::array();
std::string W2U(const std::wstring& value){std::string out;for(wchar_t c:value)out.push_back(static_cast<char>(c));return out;}
#include "AsciiExtract.inc"
#include "LoopbackExtract.inc"
std::string FixtureLowerHeaders(const std::string& value){loweredRawHeaderBytes+=value.size();return ascii_lower(value);}
std::string FixtureLowerName(const std::string& value){++headerNameCopies;return ascii_lower(value);}
#include "HeaderIdExtract.inc"
std::string FixtureHeaderId(const std::string& value){++headerIdCalls;return fanHeaderRequestIdFromRawHeaders(value);}
struct Monitor{ULONGLONG current=1;ULONGLONG epoch()const{return current;}void observe(ULONGLONG epoch,DWORD status,const json& value,long long){if(epoch==current)trace.push_back(json{{"event","display"},{"status",status},{"value",value.is_discarded()?json():value}});}}g_monitorFanDisplay;
double sgNowEpoch(){return 1700000000.;}
std::string fanApiPathFromObject(const std::wstring& value){auto s=W2U(value);const auto at=s.find('?');return at==std::string::npos?s:s.substr(0,at);}
std::string fanErrorCodeFromBody(const std::string& body){const auto v=json::parse(body,nullptr,false);return v.is_object()&&v.contains("error")&&v["error"].is_object()?v["error"].value("code",std::string{}):std::string{};}
std::string fanRequestId(const char*){return "fixture-native-id";}
void fanHostNoteAuthRejected(const char*,const std::string& method,const std::string& path,DWORD status,const std::string& credential,bool present,const char*,const std::string& requestId,const std::string& error,const std::string&,const std::string&,bool added){trace.push_back(json{{"event","auth"},{"method",method},{"path",path},{"status",status},{"present",present},{"credentialEmpty",credential.empty()},{"fixtureCredential",credential},{"requestId",requestId},{"error",error},{"added",added}});}
#include "LeaseParserExtract.inc"
void fanLeaseKeepaliveArm(const std::string& lease,const char* source,ULONGLONG epoch){trace.push_back(json{{"event","arm"},{"lease",lease},{"source",source},{"epoch",epoch}});}
void fanLeaseKeepaliveDisarm(const char* source){trace.push_back(json{{"event","disarm"},{"source",source}});}
void fanActivityObserve(const std::string& path,const std::string& request,DWORD status,const std::string& body,const json* supplied,ULONGLONG epoch){const auto value=supplied?*supplied:json::parse(body,nullptr,false);trace.push_back(json{{"event","activity"},{"path",path},{"status",status},{"supplied",supplied!=nullptr},{"accepted",ymcc::fan_response::accepted(path,status,value,parseFanLeaseIdText(request))},{"epoch",epoch}});}
json FixtureDirectParse(const std::string& body){++directParses;return json::parse(body,nullptr,false);}
json FixtureFallbackParse(const std::string& body){++fallbackParses;return json::parse(body,nullptr,false);}
#include "ObserverExtract.inc"
template<class...Args>void FixtureObserve(Args&&...args){++observerCalls;fanLeaseKeepaliveObserveHttp(std::forward<Args>(args)...);}
const std::string lease="fixture-lease-0001";
struct Input{const wchar_t* host=L"127.0.0.1";unsigned short port=8765;std::string method="POST";std::wstring path=L"/api/heartbeat";std::string body=json{{"leaseId",lease}}.dump();DWORD status=200;bool complete=true;ULONGLONG epoch=1;json headers={{"Content-Type","application/json"},{"X-Test","fixture"},{"X-YeMan-Fan-Session",std::string(64,'f')}};std::string rawHeaders="HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-YeMan-Fan-Request-Id: 0123456789ABCDEF0123456789ABCDEF\r\n\r\n";std::string response=json{{"ok",true},{"lease",{{"leaseId",lease},{"generation",1}}}}.dump();};
json run(const Input& in){const auto host=in.host;struct Url{unsigned short nPort;}uc{in.port};const auto& method=in.method;const auto& objectPath=in.path;const auto& body=in.body;const auto& hdrs=in.headers;const auto statusCode=in.status;const auto& rawResponseHeaders=in.rawHeaders;const auto& respBody=in.response;const bool fanBodyComplete=in.complete;const auto monitorFanEpoch=in.epoch;
#include "TargetExtract.inc"
#include "ResponseStageExtract.inc"
}
void expect(bool value,const char* message){++checks;if(!value)failures.push_back(message);}
unsigned events(const char* name){return static_cast<unsigned>(std::count_if(trace.begin(),trace.end(),[&](const json& x){return x.value("event",std::string{})==name;}));}
int main(int argc,char** argv){if(argc!=2)return 2;const std::string scenario=argv[1];Input in;json result;unsigned requestCount=1;std::string expectedId="0123456789abcdef0123456789abcdef";
try{
 if(scenario.rfind("nonfan-",0)==0){
  if(scenario=="nonfan-local-port")in.port=27060;else{in.host=L"store.steampowered.com";in.port=scenario=="nonfan-remote-port8765"?8765:443;}
  if(scenario=="nonfan-denied"){in.status=401;in.response=R"({"ok":false,"error":{"code":"AUTH_REJECTED"}})";}
  if(scenario=="nonfan-burst"){requestCount=100;in.rawHeaders+="X-Long: "+std::string(4096,'x')+"\r\n";}
  for(unsigned i=0;i<requestCount;++i)result=run(in);
  expect(trace.empty(),"non-Fan request changed Fan state/auth/display");expect(headerIdCalls==0,"non-Fan request still scanned/lowercased all response headers");expect(headerNameCopies==0,"non-Fan request still copied request header names");expect(observerCalls==0,"non-Fan request still entered Fan-only observer");expect(directParses==0&&fallbackParses==0,"non-Fan request did Fan JSON work");expect(!result.contains("bodyComplete"),"non-Fan response contract changed");
 }else{
  if(scenario=="fan-localhost")in.host=L"LOCALHOST";
  if(scenario=="fan-ipv6")in.host=L"::1";
  if(scenario=="fan-get"){in.method="GET";in.path=L"/api/state";in.response=R"({"ok":true,"state":{"state":"Ready"}})";}
  if(scenario=="fan-head"){in.method="HEAD";in.response.clear();}
  if(scenario=="fan-cased-header")in.headers={{"x-yEmAn-FaN-sEsSiOn","fixture-token"}};
  if(scenario=="fan-no-header")in.headers=json::object();
  if(scenario=="fan-typed-header")in.headers={{"X-YeMan-Fan-Session",7}};
  if(scenario=="fan-no-header"||scenario=="fan-typed-header"||scenario=="fan-denied401"||scenario=="fan-denied403"||scenario=="fan-denied-stale-epoch"||scenario=="fan-denied-token-rotation"||scenario=="fan-denied-incomplete"){
   in.status=scenario=="fan-denied403"?403:401;in.response=R"({"ok":false,"error":{"code":"AUTH_REJECTED"}})";
  }
  if(scenario=="fan-incomplete"||scenario=="fan-close-incomplete")in.complete=false;
  if(scenario=="fan-close-incomplete")in.path=L"/api/close";
  if(scenario=="fan-denied-incomplete")in.complete=false;
  if(scenario=="fan-denied-token-rotation")in.headers["X-YeMan-Fan-Session"]=std::string(64,'a');
  if(scenario=="fan-invalid-json")in.response="{not-json}";
  if(scenario=="fan-oversized")in.response=json{{"padding",std::string(65537,'x')}}.dump();
  if(scenario=="fan-stale-epoch"||scenario=="fan-denied-stale-epoch")g_monitorFanDisplay.current=2;
  if(scenario.rfind("header-",0)==0){
   in.status=401;in.response=R"({"ok":false,"error":{"code":"AUTH_REJECTED"}})";
   const std::string id="0123456789ABCDEF0123456789ABCDEF",name="X-YeMan-Fan-Request-Id: ";
   if(scenario=="header-name-in-value"){in.rawHeaders="HTTP/1.1 401\r\nX-Note: "+name+id+"\r\n\r\n";expectedId.clear();}
   if(scenario=="header-name-prefix"){in.rawHeaders="HTTP/1.1 401\r\nNot-"+name+id+"\r\n\r\n";expectedId.clear();}
   if(scenario=="header-reason-phrase"){in.rawHeaders="HTTP/1.1 401 "+name+id+"\r\n\r\n";expectedId.clear();}
   if(scenario=="header-trailing-letter"){in.rawHeaders=name+id+"Z\r\n";expectedId.clear();}
   if(scenario=="header-trailing-comma"){in.rawHeaders=name+id+",other\r\n";expectedId.clear();}
   if(scenario=="header-body-spoof"){in.rawHeaders="HTTP/1.1 401\r\n\r\n"+name+id;expectedId.clear();}
   if(scenario=="header-short"){in.rawHeaders=name+id.substr(0,31)+"\r\n";expectedId.clear();}
   if(scenario=="header-long"){in.rawHeaders=name+id+"A\r\n";expectedId.clear();}
   if(scenario=="header-ows")in.rawHeaders=name+"\t "+id+" \t\r\n\r\n";
   if(scenario=="header-mixed-case")in.rawHeaders="x-yEmAn-fAn-rEqUeSt-iD: "+id+"\r\n";
   if(scenario=="header-lf")in.rawHeaders="HTTP/1.1 401\n"+name+id+"\n\n";
   if(scenario=="header-no-final-newline")in.rawHeaders=name+id;
   if(scenario=="header-empty-value"){in.rawHeaders=name+"\r\nOther: "+id+"\r\n";expectedId.clear();}
   if(scenario=="header-missing-then-manual"){in.rawHeaders="HTTP/1.1 401\r\nOther: none\r\n\r\n";expectedId.clear();}
  }
  if(scenario=="fan-long-headers")in.rawHeaders="HTTP/1.1 200 OK\r\nX-Long: "+std::string(8192,'a')+"\r\nX-YeMan-Fan-Request-Id: 0123456789ABCDEF0123456789ABCDEF\r\n\r\n";
  if(scenario=="fan-success-burst")requestCount=100;
  for(unsigned i=0;i<requestCount;++i)result=run(in);expect(observerCalls==requestCount&&headerIdCalls==requestCount,"Fan receipt/provenance work was skipped");expect(result.contains("bodyComplete")&&result["bodyComplete"]==in.complete,"Fan completeness marker changed");
  const bool denied=in.status==401||in.status==403;expect(events("auth")==static_cast<unsigned>(denied),"Fan authentication denial accounting changed");
  if(denied){const auto auth=std::find_if(trace.begin(),trace.end(),[](const json& x){return x["event"]=="auth";});expect(auth!=trace.end()&&(*auth)["requestId"]==expectedId,"Fan request ID was lost");expect(events("arm")==0,"denied receipt armed renewal");}
  if(scenario=="fan-incomplete"){expect(events("arm")==0&&events("display")==0,"incomplete body became successful receipt");expect(directParses==0&&fallbackParses==0,"incomplete receipt reparsed downstream");}
  if(scenario=="fan-close-incomplete")expect(events("disarm")==1&&events("arm")==0,"incomplete explicit Close lost its disarm boundary");
  if(scenario=="fan-stale-epoch")expect(events("arm")==0&&events("activity")==0&&events("display")==0,"stale epoch recreated control");
  if(scenario=="fan-normal"||scenario=="fan-localhost"||scenario=="fan-ipv6"||scenario=="fan-cased-header"||scenario=="fan-success-burst")expect(events("arm")==requestCount,"valid current heartbeat became unreachable");
  if(!denied)expect(credentialCopyCalls==0&&credentialBytesCopied==0,"successful/non-auth receipt materialized an unused credential copy");
  if(denied){const auto auth=std::find_if(trace.begin(),trace.end(),[](const json& x){return x["event"]=="auth";});std::string expectedCredential;for(const auto& item:in.headers.items())if(ascii_lower(item.key())=="x-yeman-fan-session"&&item.value().is_string())expectedCredential=item.value().get<std::string>();expect(auth!=trace.end()&&(*auth)["fixtureCredential"]==expectedCredential,"rejection evidence lost the exact credential used by this request");}
  if(scenario=="fan-denied-token-rotation"){Input next=in;next.headers["X-YeMan-Fan-Session"]=std::string(64,'b');run(next);expect(events("auth")==2&&credentialCopyCalls==2&&credentialBytesCopied==128,"new rejection token was cached or not materialized");const auto auth=std::find_if(trace.rbegin(),trace.rend(),[](const json& x){return x["event"]=="auth";});expect(auth!=trace.rend()&&(*auth)["fixtureCredential"]==std::string(64,'b'),"second rejection reused old credential bytes");requestCount++;}
  if(scenario=="fan-long-headers")expect(loweredRawHeaderBytes==0,"valid Fan request still copied/lowercased the entire raw header block");
  if(scenario=="header-missing-then-manual"){Input next;const auto response=run(next);expect(response["status"]==200&&events("arm")==1,"missing diagnostic ID blocked the next current manual receipt");requestCount++;}
 }
 expect(result.value("status",0u)==in.status&&result.value("headers",std::string{})==in.rawHeaders&&result.value("body",std::string{})==in.response,"general HTTP response fields changed");
 std::cout<<json{{"ok",failures.empty()},{"scenario",scenario},{"checks",checks},{"failures",failures},{"control",{{"response",result},{"fanTrace",trace},{"requests",requestCount}}},{"work",{{"headerIdCalls",headerIdCalls},{"credentialCopyCalls",credentialCopyCalls},{"credentialBytesCopied",credentialBytesCopied},{"headerNameCopies",headerNameCopies},{"loweredRawHeaderBytes",loweredRawHeaderBytes},{"observerCalls",observerCalls},{"directParses",directParses},{"fallbackParses",fallbackParses}}},{"hardwareCalls",0},{"cpuBenchmark",false}}.dump()<<'\n';return failures.empty()?0:1;
}catch(const std::exception& ex){std::cout<<json{{"ok",false},{"fixtureError",ex.what()},{"scenario",scenario}}.dump()<<'\n';return 2;}}
