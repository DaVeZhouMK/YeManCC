#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "deps/json/json.hpp"
#include <functional>
#include "tools/fixtures/process_inventory_ports.h"
using json=nlohmann::json;
#include "handlers.inc"
static unsigned checks=0;
static void require(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
static void returned(){require(osfacts::s.handles.empty(),"owned fake handles leaked");require(osfacts::s.violations==0,"fake API contract violated");}
static void reset(std::vector<osfacts::Row> rows={{900,L"unrelated.exe"}}){returned();osfacts::s=osfacts::State{};osfacts::s.rows=std::move(rows);ymcc::processInventory::invalidate();}
static bool unknown(const json& names){try{proc_after::handler(json{{"names",names}});return false;}catch(const std::runtime_error& e){return std::string(e.what())=="PROCESS_INVENTORY_UNAVAILABLE";}}
static void before(){
 reset({{100,L"RTSS.exe"}});require(proc_before::handler(json{{"names",{"RTSS"}}}).at("RTSS")==true,"before known present control");
 reset();require(proc_before::handler(json{{"names",{"RTSS"}}}).at("RTSS")==false,"before known absent control");
 osfacts::s.snapshotFail=true;bool rejected=false;json value;
 try{value=proc_before::handler(json{{"names",{"RTSS"}}});}catch(...){rejected=true;}
 std::cout<<"BEFORE_UNKNOWN_REPORTED_ABSENT result="<<value.dump()<<" snapshotFail="<<osfacts::s.snapshotFail<<"\n";
 require(rejected,"BEFORE_UNKNOWN_REPORTED_ABSENT: failed native census must not confirm process absence");
}
static void after(){
 reset({{100,L"RTSS.Exe"},{101,L"RTSSHooksLoader64.exe"},{102,L"EncoderServer64.exe"},{103,L"中文.exe"}});
 auto result=proc_after::handler(json{{"names",{"RTSS","rtss","RTSSHooksLoader*","EncoderServer*","missing","RTSS.exe","中文",""}}});
 for(const auto* name:{"RTSS","rtss","RTSSHooksLoader*","EncoderServer*","中文"})require(result.at(name)==true,"same image/stem/prefix/case rules preserved");
 for(const auto* name:{"missing","RTSS.exe",""})require(result.at(name)==false,"absent/full extension/empty matching rules preserved");
 require(osfacts::s.queryCalls==1&&osfacts::s.snapshots==0,"one real owner basic query, zero consumer Toolhelp");returned();
 result=proc_after::handler(json{{"names",{"*","RT*","RTSS*"}}});require(result.at("*")==true&&result.at("RT*")==true&&result.at("RTSS*")==true,"prefix wildcard rules preserved");
 result=proc_after::handler(json{{"names",{"RTSS","RTSS",7,nullptr}}});require(result.size()==1&&result.at("RTSS")==true,"duplicate and nonstring input ignored as before");
 reset();result=proc_after::handler(json::object());require(result.empty()&&osfacts::s.queryCalls==0&&osfacts::s.snapshots==0,"empty request needs no discovery");
 result=proc_after::handler(json{{"names",json::array()}});require(result.empty()&&osfacts::s.queryCalls==0,"empty names need no discovery");
 result=proc_after::handler(json{{"names",{"RTSS"}}});require(result.at("RTSS")==false&&osfacts::s.queryCalls==1,"complete absence is confirmed false");
 osfacts::s.rows={{100,L"RTSS.exe"}};result=proc_after::handler(json{{"names",{"RTSS"}}});require(result.at("RTSS")==true&&osfacts::s.queryCalls==2,"request freshness0 observes a newly started instance");
 osfacts::s.rows={{900,L"other.exe"}};result=proc_after::handler(json{{"names",{"RTSS"}}});require(result.at("RTSS")==false&&osfacts::s.queryCalls==3,"request freshness0 observes process exit");
 osfacts::s.mode=osfacts::QueryMode::failed;require(unknown({"RTSS"}),"failed fresh query rejects, cannot reuse stopped");
 reset({{100,L"RTSS.exe"}});proc_after::handler(json{{"names",{"RTSS"}}});osfacts::s.mode=osfacts::QueryMode::malformed;require(unknown({"RTSS"}),"malformed query rejects, cannot reuse running");
 reset();osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.snapshotFail=true;require(unknown({"RTSS"}),"unavailable fallback snapshot rejects");returned();
 reset({{100,L"RTSS.exe"},{900,L"other.exe"}});osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.partial=true;require(unknown({"RTSS"}),"partial table rejects even an early name match");returned();
 reset({{100,L"RTSS.exe"}});osfacts::s.mode=osfacts::QueryMode::unsupported;result=proc_after::handler(json{{"names",{"RTSS","missing"}}});require(result.at("RTSS")==true&&result.at("missing")==false&&osfacts::s.snapshots==1,"complete owner Toolhelp fallback retained");returned();
 std::cout<<json{{"passed",true},{"checks",checks},{"actualNativeHandler",true},{"actualOwner",true},{"realProcessesOrSettings",false}}.dump()<<"\n";
}
int main(int argc,char**argv){try{proc_before::registration();proc_after::registration();if(argc!=2)throw std::runtime_error("selection required");if(!strcmp(argv[1],"--before"))before();else if(!strcmp(argv[1],"--after"))after();else throw std::runtime_error("unknown selection");return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
