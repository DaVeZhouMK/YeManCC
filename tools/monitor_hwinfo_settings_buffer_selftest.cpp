// Actual before/after acquisition bodies are inserted by the runner. Allocation
// counts cover the complete read+parse call; no hardware or CPU timing probes.
#include <cstdlib>
#include <new>
#include <fstream>
#include <iostream>
#include <functional>
#include "../native/monitor_hwinfo_manager.h"
static thread_local bool tracking=false;
static thread_local std::size_t allocationCalls=0,allocationBytes=0;
void* operator new(std::size_t n){if(void*p=std::malloc(n?n:1)){if(tracking){++allocationCalls;allocationBytes+=n;}return p;}throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using Json=nlohmann::json;
namespace fixture {
const std::optional<std::string>* input=nullptr;unsigned reads=0;bool readError=false;
std::optional<std::string> read(const std::wstring&p,std::size_t limit,bool optional){
 ++reads;ymcc::hwinfo::require(p==L"C:\\YMCC fixture\\PowerControl\\yeman-settings.json"&&limit==16777216&&optional,"Unexpected fixture read contract");
 if(readError)throw ymcc::hwinfo::Error(8,"fixture sharing failure",false,ERROR_SHARING_VIOLATION);
 if(!input||!*input)return {};ymcc::hwinfo::require((**input).size()<=limit,"HWiNFO configuration exceeds its byte budget");return **input;
}
}
namespace before {
using namespace ymcc::hwinfo;
std::optional<std::string> readBytes(const std::wstring&p,std::size_t limit,bool optional){return fixture::read(p,limit,optional);}
// YMCC_BEFORE_ACQUISITION
}
namespace after {
using namespace ymcc::hwinfo;
std::optional<std::string> readBytes(const std::wstring&p,std::size_t limit,bool optional){return fixture::read(p,limit,optional);}
// YMCC_AFTER_ACQUISITION
}
struct Outcome {int code=0,choice=-1;DWORD win32=0;std::string detail;std::size_t calls=0,bytes=0;unsigned reads=0;};
Outcome run(std::function<std::optional<bool>(const std::wstring&)> fn,const std::optional<std::string>&input,bool readError=false){
 fixture::input=&input;fixture::reads=0;fixture::readError=readError;allocationCalls=allocationBytes=0;tracking=true;Outcome out;
 try{const auto value=fn(L"C:\\YMCC fixture\\PowerControl");out.choice=value?(*value?1:0):-1;}catch(const ymcc::hwinfo::Error&e){out.code=e.code;out.win32=e.win32;out.detail=e.what();}catch(const std::exception&e){out.code=-1;out.detail=e.what();}
 tracking=false;out.calls=allocationCalls;out.bytes=allocationBytes;out.reads=fixture::reads;fixture::input=nullptr;return out;
}
int main(int argc,char**argv){if(argc!=2)return 2;Json cases=Json::array();bool ok=true;unsigned serial=0;std::size_t savedBytes=0,savedCalls=0;auto verify=[&](const std::string&name,std::optional<std::string> text,int expectedCode=-999,int expectedChoice=-999,bool requireCopySaving=false,bool failRead=false){
 auto a=run(before::acquisition,text,failRead),b=run(after::acquisition,text,failRead);bool same=a.code==b.code&&a.choice==b.choice&&a.detail==b.detail&&a.win32==b.win32&&a.reads==b.reads&&a.reads==1;
 bool expected=(expectedCode==-999||b.code==expectedCode)&&(expectedChoice==-999||b.choice==expectedChoice);
 // Small documents keep the original exact one-copy check. Large projection
 // documents may add fixed parser bookkeeping, but must still save nearly the
 // complete input length at the 1/16 MiB boundary.
 const bool exactCopySaving=text&&a.calls==b.calls+1&&a.bytes>=b.bytes+text->size()+1&&a.bytes<=b.bytes+text->size()+128;
 bool saving=!requireCopySaving||(text->size()<=4096?exactCopySaving:
     (a.bytes+1024>=b.bytes+text->size()&&a.bytes<=b.bytes+text->size()+128&&b.calls<=a.calls+16));ok=ok&&same&&expected&&saving;
 if(a.bytes>=b.bytes)savedBytes+=a.bytes-b.bytes;if(a.calls>=b.calls)savedCalls+=a.calls-b.calls;
 cases.push_back({{"name",name},{"ok",same&&expected&&saving},{"sameResult",same},{"code",b.code},{"choice",b.choice},{"reads",b.reads},{"inputBytes",text?text->size():0},{"beforeAllocations",a.calls},{"afterAllocations",b.calls},{"beforeAllocatedBytes",a.bytes},{"afterAllocatedBytes",b.bytes},{"oneFullBufferCopyRemoved",requireCopySaving&&saving}});
 if(!(same&&expected&&saving))std::cerr<<name<<" code="<<b.code<<" choice="<<b.choice<<" alloc="<<a.calls<<'/'<<b.calls<<" bytes="<<a.bytes<<'/'<<b.bytes<<'\n';
 };
 verify("missing unified settings",{},0,-1);
 for(const auto&s:{"{}","{\"rtss\":null}","{\"rtss\":{}}","{\"rtss\":{\"monitorAcquisition\":null}}"})verify("optional policy "+std::to_string(serial++),std::string(s),0,-1);
 for(const auto&s:{"[]","null","true","42","\"text\"","{\"rtss\":[]}","{\"rtss\":false}","{\"rtss\":{\"monitorAcquisition\":[]}}","{\"rtss\":{\"monitorAcquisition\":{}}}"})verify("invalid shape "+std::to_string(serial++),std::string(s),8);
 verify("enabled snapshot choice",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}})",0,1);
 verify("disabled snapshot choice",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})",0,0);
 verify("policy disabled retains optional choice",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":false}}})",0,-1);
 for(const Json&version:std::vector<Json>{nullptr,false,0,1,1.0,2,"1"})for(const Json&enabled:std::vector<Json>{nullptr,false,true,0,"true"})for(const Json&choice:std::vector<Json>{nullptr,false,true,1}){
  auto doc=Json{{"rtss",{{"monitorAcquisition",{{"version",version},{"enabled",enabled},{"snapshotPolling",choice}}}}}};verify("choice matrix "+std::to_string(serial++),doc.dump());
 }
 const std::string bom="\xef\xbb\xbf",valid=R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}})";
 verify("UTF8 BOM",bom+valid,0,1);verify("double BOM retains existing parser acceptance",bom+bom+valid,0,1);verify("UTF8 BOM with whitespace",bom+" \r\n"+valid,0,1);
 for(const auto&s:std::vector<std::string>{"",bom,"{" ,valid+" trailing",valid+"{}",std::string("{\0}",3),std::string("{\"x\":\"\xff\"}")})verify("malformed encoding or JSON "+std::to_string(serial++),s,8);
 verify("duplicate key keeps parser last-wins behavior",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false,"snapshotPolling":true}}})",0,1);
 const auto large=[&](std::size_t size,bool withBom){const std::string start=(withBom?bom:std::string{})+"{\"unrelated\":\"",end="\","+valid.substr(1);return start+std::string(size-start.size()-end.size(),'x')+end;};
 verify("large settings without BOM",large(1048576,false),0,1,true);
 verify("large settings with BOM",large(1048576,true),0,1,true);
 verify("exact 16 MiB read budget",large(16777216,false),0,1,true);
 verify("over 16 MiB remains rejected",large(16777217,false),8,-1);
 verify("native read failure remains visible",valid,8,-1,false,true);
 for(unsigned i=0;i<100;++i)verify("repeated event allocation "+std::to_string(i),large(4096,i%2!=0),0,1,true);
 auto unicode=Json{{"界面",{{"名称","掌机监控"},{"path","G:/游戏/空 战"}}},{"rtss",{{"monitorAcquisition",{{"version",1},{"enabled",true},{"snapshotPolling",false}}}}}}.dump();verify("unrelated Unicode content",unicode,0,0,true);
 std::ofstream(argv[1])<<Json({{"ok",ok},{"cases",cases},{"savedAllocationCalls",savedCalls},{"savedAllocatedBytes",savedBytes},{"scope","Actual pre-change/current production acquisition bodies and identical parser. Read stub applies the existing single-read path/budget contract. Global new counts complete call allocations; no hardware IO, CPU percentage, elapsed-time benchmark or permanent collector."}}).dump(2);return ok?0:1;
}