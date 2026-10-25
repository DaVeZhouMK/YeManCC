#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <array>
#include <vector>
#include <cstring>
#include <optional>
#include <string>
#include <fstream>
#include <iostream>
#include "json.hpp"
namespace fixture {
struct Slot{bool present=false,viewOkay=true,queryOkay=true;std::vector<unsigned char> bytes;};
inline std::array<Slot,4> slots;inline unsigned opens=0,closes=0,views=0,unmaps=0,sleeps=0;inline unsigned long long now=0;
inline HANDLE open(DWORD rights,BOOL inherit,const wchar_t* name){if(rights!=FILE_MAP_READ||inherit)throw std::runtime_error("unexpected mapping rights");const wchar_t* names[]={L"Global\\HWiNFO_SENS_SM2",L"HWiNFO_SENS_SM2",L"Global\\HWiNFO_SENS_SM",L"HWiNFO_SENS_SM"};for(unsigned i=0;i<4;++i)if(wcscmp(name,names[i])==0){if(!slots[i].present){SetLastError(ERROR_FILE_NOT_FOUND);return nullptr;}++opens;return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(i+1));}throw std::runtime_error("unexpected mapping name");}
inline unsigned index(HANDLE h){const auto n=reinterpret_cast<uintptr_t>(h);if(!n||n>4)throw std::runtime_error("unexpected handle");return (unsigned)n-1;}
inline void* map(HANDLE h,DWORD,DWORD,DWORD,SIZE_T){auto& s=slots[index(h)];if(!s.viewOkay){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}++views;return s.bytes.data();}
inline SIZE_T query(const void* address,PMEMORY_BASIC_INFORMATION info,SIZE_T){for(auto& s:slots)if(s.bytes.data()==address){if(!s.queryOkay)return 0;*info={};info->RegionSize=s.bytes.size();return sizeof(*info);}return 0;}
inline BOOL unmap(const void*){++unmaps;return TRUE;}inline BOOL close(HANDLE){++closes;return TRUE;}inline void sleep(DWORD){++sleeps;}
inline void reset(){slots={};opens=closes=views=unmaps=sleeps=0;}
}
#define OpenFileMappingW fixture::open
#define MapViewOfFile fixture::map
#define VirtualQuery fixture::query
#define UnmapViewOfFile fixture::unmap
#define CloseHandle fixture::close
#define Sleep fixture::sleep
#include "monitor_hwinfo_manager.h"
#include "monitor_hwinfo_recovery.h"
#include "monitor_frame_time.h"
#include "monitor_gpu_selection.h"
#include "monitor_memory_network.h"
#include "monitor_voltage.h"
#include "monitor_text_fold.h"
#include "monitor_low_slots.h"
#include "monitor_parse_reuse.h"
using json=nlohmann::json;
static void monitorTestBarrierPause(int){}
static bool domainLogEnabled(const char*){return false;}
static double sgNowEpoch(){return fixture::now/1000.0;}
// ACTUAL_NATIVE_MONITOR_STRUCTURE
// ACTUAL_NATIVE_MONITOR_PARSER
// ACTUAL_NATIVE_MONITOR_MAPPING_READER
#undef OpenFileMappingW
#undef MapViewOfFile
#undef VirtualQuery
#undef UnmapViewOfFile
#undef CloseHandle
#undef Sleep
static std::vector<BYTE> sample(unsigned long long poll,double watts=17.5){std::vector<BYTE>b(900,0);const auto put=[&](size_t i,auto v){std::memcpy(b.data()+i,&v,sizeof(v));};put(0,uint32_t{0x53695748});put(4,uint32_t{2});put(8,uint32_t{1});put(12,poll);put(20,uint32_t{48});put(24,uint32_t{392});put(28,uint32_t{1});put(32,uint32_t{440});put(36,uint32_t{460});put(40,uint32_t{1});put(44,uint32_t{3000});std::memcpy(b.data()+48+8,"CPU [#0]",8);std::memcpy(b.data()+440+12,"CPU Package Power",17);std::memcpy(b.data()+440+268,"W",1);put(440+284,watts);return b;}
int main(int argc,char**argv){if(argc!=2)return 2;json checks=json::array();bool ok=true;std::string error;const auto check=[&](const char*n,bool pass){checks.push_back({{"name",n},{"ok",pass}});ok=ok&&pass;};try{fixture::now=ymcc::hwinfo::clockMs();const auto fresh=fixture::now/1000ULL;const auto setup=[&](unsigned i,unsigned long long poll,double watts=17.5){fixture::slots[i]={true,true,true,sample(poll,watts)};};const auto collector=[&](){NativeMonitorHw hw;ymcc::monitorparse::Reuse<NativeMonitorHw> reuse;reuse.begin(1);bool available=monitorReadHWiNFO(hw,&reuse);return std::pair{available,hw};};const auto balanced=[&](){return fixture::opens==fixture::closes&&fixture::views==fixture::unmaps;};
fixture::reset();setup(0,fresh);auto a=collector();check("healthy first mapping retains one-open fast path",a.first&&a.second.producerPollTime==fresh&&fixture::opens==1&&balanced());
fixture::reset();setup(0,fresh);fixture::slots[0].bytes[0]=0;setup(1,fresh,22);a=collector();check("collector skips invalid first mapping",a.first&&a.second.packagePower==22&&balanced());
fixture::reset();setup(0,fresh-100,11);setup(1,fresh,22);a=collector();check("collector skips stale first mapping",a.first&&a.second.producerPollTime==fresh&&a.second.packagePower==22&&balanced());
fixture::reset();setup(0,fresh);fixture::slots[0].viewOkay=false;setup(1,fresh,22);a=collector();check("collector continues after map-view failure",a.first&&a.second.packagePower==22&&balanced());
fixture::reset();setup(0,fresh);fixture::slots[0].queryOkay=false;setup(1,fresh,22);a=collector();check("collector continues after mapping-size query failure",a.first&&a.second.packagePower==22&&balanced());
fixture::reset();setup(0,fresh-100);a=collector();check("sole stale mapping remains readable for existing stall recovery",a.first&&a.second.producerPollTime==fresh-100&&balanced());
fixture::reset();a=collector();check("absent mappings remain unavailable",!a.first&&balanced());
fixture::reset();setup(0,fresh-100);setup(1,fresh);auto proof=ymcc::hwinfo::sharedProof();check("manager skips stale first mapping",proof&&proof->poll==fresh&&proof->layout[0]==1&&balanced());
fixture::reset();setup(0,fresh);fixture::slots[0].bytes[0]=0;setup(1,fresh);proof=ymcc::hwinfo::sharedProof();check("manager skips invalid first mapping",proof&&proof->layout[0]==1&&balanced());
fixture::reset();setup(0,fresh);fixture::slots[0].viewOkay=false;setup(1,fresh);proof=ymcc::hwinfo::sharedProof();check("manager continues after map-view failure",proof&&proof->layout[0]==1&&balanced());
fixture::reset();setup(3,fresh);proof=ymcc::hwinfo::sharedProof();check("legacy final mapping can provide current producer proof",proof&&proof->layout[0]==3&&balanced());
fixture::reset();setup(0,fresh-100);proof=ymcc::hwinfo::sharedProof();check("manager never promotes a stale-only producer",!proof&&balanced());
fixture::reset();setup(0,fresh,19);setup(1,fresh,29);a=collector();check("two healthy mappings preserve original source priority",a.first&&a.second.packagePower==19&&fixture::opens==1&&balanced());
fixture::reset();setup(0,fresh,12);ymcc::monitorparse::Reuse<NativeMonitorHw> reuse;reuse.begin(7);NativeMonitorHw switching;bool connected=monitorReadHWiNFO(switching,&reuse);setup(0,fresh-100,13);setup(1,fresh,24);connected=connected&&monitorReadHWiNFO(switching,&reuse);check("source failover cannot reuse the previous mapping values",connected&&switching.packagePower==24&&switching.producerPollTime==fresh&&balanced());
fixture::slots[1].present=false;setup(0,fresh,36);connected=monitorReadHWiNFO(switching,&reuse);check("reconnected original source publishes its own current values",connected&&switching.packagePower==36&&balanced());
fixture::reset();setup(0,fresh);uint32_t differentPeriod=1500;std::memcpy(fixture::slots[0].bytes.data()+44,&differentPeriod,4);setup(1,fresh);proof=ymcc::hwinfo::sharedProof(3000);check("explicit period proof can use a later matching mapping",proof&&proof->layout[0]==1&&balanced());
fixture::reset();setup(0,fresh);fixture::slots[0].bytes[0]=0;setup(1,fresh);fixture::slots[1].bytes[0]=0;a=collector();check("invalid-only mappings never appear connected",!a.first&&balanced());
}catch(const std::exception&e){ok=false;error=e.what();}std::ofstream(argv[1])<<json({{"ok",ok},{"error",error},{"cases",checks},{"liveHardware",false}}).dump(2);return ok?0:3;}
