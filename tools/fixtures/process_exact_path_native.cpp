#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "deps/json/json.hpp"
#include <functional>
#include "tools/fixtures/process_inventory_ports.h"
#include "tools/fixtures/process_exact_path_ports.h"
#define main processStatusOriginalMain
#include "tools/fixtures/steam_process_status_native.cpp"
#undef main
namespace exactcases {
static unsigned checks=0;
static void need(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
static void returned(){need(osfacts::s.handles.empty(),"exact path owned fake handles leaked");need(osfacts::s.violations==0,"exact API contract violated");}
static void reset(std::vector<osfacts::Row> rows={{900,L"unrelated.exe"}}){returned();osfacts::s=osfacts::State{};osfacts::s.rows=std::move(rows);ymcc::processInventory::invalidate();exactfacts::resetFiles();}
static json args(const std::wstring& path=exactfacts::wanted){return{{"path",W2U(path)}};}
static osfacts::Row row(DWORD pid=100){osfacts::Row value{pid,L"different-name.exe"};value.path=exactfacts::wanted;return value;}
static bool unknown(){try{(void)exact_after::handler(args());return false;}catch(const std::runtime_error& error){return std::string(error.what())=="PROCESS_INVENTORY_UNAVAILABLE";}}
static int before(){
 reset({row()});const auto present=exact_before::handler(args());need(present.at("found")==true&&present.at("pid")==100,"original exact present control");returned();need(osfacts::s.snapshots==1&&osfacts::s.queryCalls==0,"original owns separate census");
 reset();need(exact_before::handler(json::object()).at("found")==false&&osfacts::s.snapshots==0,"original empty path no census");
 osfacts::s.snapshotFail=true;bool failed=false;json value;try{value=exact_before::handler(args());}catch(...){failed=true;}
 std::cout<<"EXACT_BEFORE_UNKNOWN "<<value.dump()<<std::endl;
 need(failed,"EXACT_BEFORE_UNKNOWN_REPORTED_ABSENT: failed private census cannot confirm exact process absence");return 0;
}
static int after(){
 reset({row()});auto value=exact_after::handler(args());need(value==json({{"found",true},{"pid",100}}),"complete exact path result shape and PID");need(osfacts::s.queryCalls==1&&osfacts::s.snapshots==0,"exact request consumes only shared NT owner census");returned();
 value=exact_after::handler(args(L"q:\\fixtureonly\\steam\\STEAM.EXE"));need(value.at("found")==true,"original case insensitive final path");
 value=exact_after::handler(args(L"Q:\\FixtureOnly\\Alias\\link.exe"));need(value.at("found")==true,"canonical alias cannot be name filtered");
 value=exact_after::handler(args(L"Q:\\FixtureOnly\\Elsewhere\\steam.exe"));need(value.at("found")==false,"same name other path refused");returned();
 reset({row()});need(exact_after::handler(json::object())==json({{"found",false},{"pid",0}})&&osfacts::s.queryCalls==0,"empty exact path preserves zero discovery");
 bool malformed=false;try{(void)exact_after::handler(json{{"path",42}});}catch(...){malformed=true;}need(malformed&&osfacts::s.queryCalls==0,"bad path type stays error before discovery");
 reset({row()});need(exact_after::handler(args()).at("found")==true,"fresh present control");osfacts::s.rows={{900,L"unrelated.exe"}};exactfacts::resetFiles();need(exact_after::handler(args()).at("found")==false&&osfacts::s.queryCalls==2,"same tick maxAge0 observes exit");osfacts::s.rows={row(101)};exactfacts::resetFiles();need(exact_after::handler(args()).at("pid")==101&&osfacts::s.queryCalls==3,"same tick maxAge0 sees new PID");returned();
 osfacts::s.mode=osfacts::QueryMode::failed;const auto images=exactfacts::imageOpens;need(unknown()&&exactfacts::imageOpens==images,"fresh failure cannot reuse prior presence or report absence");returned();
 for(const auto mode:{osfacts::QueryMode::malformed,osfacts::QueryMode::allocationFailure}){reset({row()});osfacts::s.mode=mode;need(unknown()&&exactfacts::imageOpens==0,"bad receipt refuses before path enumeration");returned();}
 reset({row()});osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.snapshotFail=true;need(unknown(),"failed owner fallback is unknown");returned();
 reset({row()});osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.firstFail=true;need(unknown(),"failed first owner row is unknown");returned();
 reset({row(),{900,L"other.exe"}});osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.partial=true;need(unknown()&&exactfacts::imageOpens==0,"partial owner prefix cannot mint exact identity or absence");returned();
 reset({row()});osfacts::s.mode=osfacts::QueryMode::unsupported;value=exact_after::handler(args());need(value.at("found")==true&&osfacts::s.snapshots==1,"complete owner Toolhelp fallback remains valid");returned();
 reset({row()});osfacts::s.rows[0].openFail=true;need(exact_after::handler(args()).at("found")==false,"original live image open failure behavior preserved");returned();
 reset({row()});osfacts::s.rows[0].pathFail=true;need(exact_after::handler(args()).at("found")==false,"original unavailable live image behavior preserved");returned();
 reset({row(100),row(101)});value=exact_after::handler(args());need(value.at("found")==true&&(value.at("pid")==100||value.at("pid")==101),"multiple matches return a valid live path PID without order promise");returned();
 std::cout<<json{{"schema","ymcc.exact-path-public-inventory-consumer.v1"},{"passed",true},{"checks",checks},{"actualOwner",true},{"completeOriginalLambdaAndPathHelpers",true},{"realProcessesOrFiles",false},{"wholeMain",false},{"sourceReady",false}}.dump()<<std::endl;return 0;
}
}
int main(int argc,char** argv){try{exact_before::registration();exact_after::registration();if(argc!=2)throw std::runtime_error("exact selector required");if(!strcmp(argv[1],"--exact-before"))return exactcases::before();if(!strcmp(argv[1],"--exact-after"))return exactcases::after();return processStatusOriginalMain(argc,argv);}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}}
