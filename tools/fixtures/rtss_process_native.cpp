#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "deps/json/json.hpp"
#include <functional>
#include <set>
#include "tools/fixtures/process_inventory_ports.h"
namespace affinityfacts {
inline std::set<UINT_PTR> writable;
inline std::map<DWORD,std::pair<DWORD_PTR,DWORD_PTR>> masks;
inline bool readFail=false,setFail=false,readbackFail=false,mismatch=false;
inline unsigned reads=0,sets=0,applied=0;
inline std::function<void()> afterRead,afterSet;
inline HANDLE WINAPI open(DWORD access,BOOL inherit,DWORD pid){
 const DWORD read=PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE;
 if(access!=read&&access!=(read|PROCESS_SET_INFORMATION)){++osfacts::s.violations;return nullptr;}
 auto handle=osfacts::open(read,inherit,pid);if(handle&&(access&PROCESS_SET_INFORMATION))writable.insert(reinterpret_cast<UINT_PTR>(handle));return handle;
}
inline BOOL WINAPI close(HANDLE handle){writable.erase(reinterpret_cast<UINT_PTR>(handle));return osfacts::close(handle);}
inline BOOL WINAPI get(HANDLE handle,PDWORD_PTR current,PDWORD_PTR system){
 ++reads;auto* value=osfacts::lookup(handle);if(!value)return FALSE;
 if(readFail||(readbackFail&&applied)){osfacts::s.error=ERROR_ACCESS_DENIED;return FALSE;}
 auto at=masks.find(value->row.pid);const auto pair=at==masks.end()?std::pair<DWORD_PTR,DWORD_PTR>{0xFF,0xFF}:at->second;
 *current=mismatch&&applied?0x1:pair.first;*system=pair.second;
 if(afterRead)afterRead();return TRUE;
}
inline BOOL WINAPI set(HANDLE handle,DWORD_PTR mask){
 ++sets;auto* value=osfacts::lookup(handle);if(!value)return FALSE;
 if(!writable.contains(reinterpret_cast<UINT_PTR>(handle))||mask!=0xA0){++osfacts::s.violations;return FALSE;}
 if(setFail){osfacts::s.error=ERROR_ACCESS_DENIED;return FALSE;}
 const auto old=masks.find(value->row.pid);const auto system=old==masks.end()?DWORD_PTR{0xFF}:old->second.second;masks[value->row.pid]={mask,system};++applied;
 if(afterSet)afterSet();return TRUE;
}
inline void clear(){writable.clear();masks.clear();readFail=setFail=readbackFail=mismatch=false;reads=sets=applied=0;afterRead={};afterSet={};}
}
#undef OpenProcess
#undef CloseHandle
#define OpenProcess affinityfacts::open
#define CloseHandle affinityfacts::close
#define GetProcessAffinityMask affinityfacts::get
#define SetProcessAffinityMask affinityfacts::set
#ifndef GetProcessAffinityMask
#error No real affinity queries allowed
#endif
#ifndef SetProcessAffinityMask
#error No real affinity writes allowed
#endif
#include "native/rtss_process_runtime.h"
using json=nlohmann::json;
static std::mutex g_gameCorePolicyMutex;
static std::atomic<DWORD> g_gameCorePolicyPid{0};
static std::atomic<bool> g_exitRequested{false};
static unsigned long long testPowerGeneration=7;
static bool testHardwareAllowed=true;
static unsigned long long currentPowerGeneration(){return testPowerGeneration;}
static bool hardwareWriteAllowed(){return testHardwareAllowed;}
static std::map<std::string,std::function<json(const json&)>> handlers;
static void ipc_on(const char* name,std::function<json(const json&)> fn){handlers.emplace(name,std::move(fn));}
#include "handlers.inc"
static const std::wstring rtssPath=L"Q:\\FixtureOnly\\RTSS\\RTSS.exe";
static unsigned checks=0;
static void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static void returned(){require(osfacts::s.handles.empty(),"fake process HANDLE leak");require(affinityfacts::writable.empty(),"writable fake HANDLE leak");require(osfacts::s.violations==0,"fake API contract violated");}
static void reset(std::vector<osfacts::Row> rows={{100,L"RTSS.exe"}}){returned();osfacts::s=osfacts::State{};osfacts::s.rows=std::move(rows);for(auto& row:osfacts::s.rows)if(row.path==osfacts::expectedPath)row.path=rtssPath;affinityfacts::clear();ymcc::processInventory::invalidate();testPowerGeneration=7;testHardwareAllowed=true;g_gameCorePolicyPid=0;g_exitRequested=false;}
static json args(DWORD pid=100,ULONGLONG birth=100,const std::wstring& path=rtssPath){return {{"pid",pid},{"processCreated",std::to_string(birth)},{"path",W2U(path)},{"_nativeRtssPowerGeneration",7},{"_nativeRtssDeadlineTick",2000}};}
static json write(const json& value){return handlers.at("rtss.setAffinityExact")(value);}
static void tests(){
 reset();auto seen=handlers.at("proc.identities")({{"names",{"RTSS"}}});require(seen.at("known")==true&&seen.at("processes").size()==1,"real identity RPC returns one live matching process");
 require(seen["processes"][0]["pid"]==100&&seen["processes"][0]["processCreated"]=="100","identity PID/birth serialized exactly");returned();
 osfacts::s.rows[0].birth=134380833279081855ULL;seen=handlers.at("proc.identities")({{"names",{"RTSS.exe"}}});require(seen["processes"][0]["processCreated"]=="134380833279081855","birth above JS integer limit stays decimal string");
 reset();seen=handlers.at("proc.identities")({{"names",json::array()}});require(seen.at("known")==true&&seen["processes"].empty()&&osfacts::s.queryCalls==0,"empty list needs no topology query");
 reset({{100,L"unrelated.exe"}});seen=handlers.at("proc.identities")({{"names",{"RTSS"}}});require(seen.at("known")==true&&seen["processes"].empty(),"complete unmatched census is known empty");
 reset();osfacts::s.rows[0].session=8;seen=handlers.at("proc.identities")({{"names",{"RTSS"}}});require(seen.at("known")==true&&seen["processes"].empty(),"foreign session is not a current-session identity");
 for(int guard=0;guard<8;++guard){reset();auto& row=osfacts::s.rows[0];if(guard==0)row.sessionFail=true;if(guard==1)row.openFail=true;if(guard==2)row.alive=false;if(guard==3)row.pathFail=true;if(guard==4)row.birthFail=true;if(guard==5)row.heldPid=777;if(guard==6)row.heldSession=8;if(guard==7)row.birth=0;seen=handlers.at("proc.identities")({{"names",{"RTSS"}}});require(seen["known"]==false&&seen["processes"].empty(),"unverifiable identity must not become absence/partial authority");returned();}
 reset();osfacts::s.rows[0].path=LR"(Q:\Other\other.exe)";seen=handlers.at("proc.identities")({{"names",{"RTSS"}}});require(seen["known"]==true&&seen["processes"].empty(),"reused PID image mismatches stale name hint");
 reset();osfacts::s.mode=osfacts::QueryMode::failed;seen=handlers.at("proc.identities")({{"names",{"RTSS"}}});require(seen["known"]==false&&seen["processes"].empty(),"failed owner census is unknown");
 reset({{100,L"RTSS.exe"},{101,L"RTSS.exe"}});osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.partial=true;seen=handlers.at("proc.identities")({{"names",{"RTSS"}}});require(seen["known"]==false&&seen["processes"].empty(),"partial old-system table cannot export an early identity");returned();
 reset();auto result=write(args());require(result["ok"]==true&&result["matched"]==true&&result["applied"]==true&&affinityfacts::applied==1&&affinityfacts::sets==1,"exact identity receives original fixed0xA0 once with readback");returned();
 for(int guard=0;guard<10;++guard){reset();auto& row=osfacts::s.rows[0];if(guard==0)row.birth=101;if(guard==1)row.path=LR"(Q:\Different\RTSS.exe)";if(guard==2)row.heldPid=777;if(guard==3)row.heldSession=8;if(guard==4)row.alive=false;if(guard==5)row.openFail=true;if(guard==6)row.pathFail=true;if(guard==7)row.birthFail=true;if(guard==8)row.heldSessionFail=true;if(guard==9)osfacts::s.ownSessionFail=true;result=write(args());require(result["ok"]==false&&result["applied"]==false&&affinityfacts::sets==0,"held PID/birth/path/session/liveness must gate write");returned();}
 reset();result=write(args(100,100,LR"(Q:\Foo\game.exe)"));require(result["ok"]==false&&affinityfacts::sets==0,"unrelated image never receives RTSS fixed affinity");
 for(int gate=0;gate<4;++gate){reset();if(gate==0)testHardwareAllowed=false;if(gate==1)testPowerGeneration=8;if(gate==2)g_gameCorePolicyPid=100;if(gate==3)g_exitRequested=true;result=write(args());require(result["ok"]==false&&affinityfacts::sets==0,"hardware/power/lease/exit gate refuses writes");returned();}
 reset();affinityfacts::afterRead=[]{testHardwareAllowed=false;};result=write(args());require(result["ok"]==false&&affinityfacts::sets==0,"revocation during mask read rechecked before commit");returned();
 reset();osfacts::s.tick=2000;result=write(args());require(result["ok"]==false&&affinityfacts::sets==0,"expired queued request cannot apply to a still-live process");returned();
 reset();affinityfacts::afterRead=[]{osfacts::s.tick=2000;};result=write(args());require(result["ok"]==false&&affinityfacts::sets==0,"queue budget exhausted during read is rechecked before write");returned();
 reset();affinityfacts::masks[100]={0x1F,0x1F};result=write(args());require(result["ok"]==false&&affinityfacts::sets==0,"unsupported CPU bits do not authorize requested mask");
 reset();affinityfacts::readFail=true;result=write(args());require(result["ok"]==false&&affinityfacts::sets==0,"mask query failure performs no write");
 reset();affinityfacts::setFail=true;result=write(args());require(result["ok"]==false&&result["applied"]==false&&affinityfacts::sets==1&&affinityfacts::applied==0,"denied set is not falsely confirmed");
 reset();affinityfacts::readbackFail=true;result=write(args());require(result["ok"]==false&&result["applied"]==true&&affinityfacts::sets==1,"missing receipt reports applied-but-unconfirmed without rollback");
 reset();affinityfacts::mismatch=true;result=write(args());require(result["ok"]==false&&result["applied"]==true&&affinityfacts::sets==1,"external readback mismatch never triggers second write");
 reset();affinityfacts::afterSet=[]{testHardwareAllowed=false;};result=write(args());require(result["ok"]==true&&result["applied"]==true&&affinityfacts::sets==1,"allowed committed write finishes receipt after revocation");returned();
 for(const auto& leaf:{L"EncoderServer64.exe",L"RTSSHooksLoader64.exe"}){reset();const std::wstring path=LR"(Q:\FixtureOnly\)"+std::wstring(leaf);osfacts::s.rows[0].path=path;result=write(args(100,100,path));require(result["ok"]==true&&affinityfacts::sets==1,"original selected family leaf remains supported");returned();}
 reset();const auto beforeQueries=osfacts::s.queryCalls;result=write(args());require(result["ok"]==true&&osfacts::s.queryCalls==beforeQueries,"exact write revalidates held identity without another global census");returned();
 std::cout<<json{{"passed",true},{"checks",checks},{"actualRuntimeHeader",true},{"actualRpcHandlers",true},{"realProcessOrAffinityOperations",false}}.dump()<<"\n";
}
int main(){try{registerHandlers();tests();return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}}
