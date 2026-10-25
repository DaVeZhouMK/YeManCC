// Actual Windows RTSS adapter with unique, synthetic named mappings.
// Only read-operation counters are injected into the frozen/current header.
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
static std::uint64_t testOpens=0,testMaps=0,testUnmaps=0,testScans=0,testSlots=0,testProcesses=0;
static void ymccTestFullScan(){++testScans;}
static void ymccTestScannedSlot(){++testSlots;}
static HANDLE ymccTestOpenMapping(DWORD access,BOOL inherit,LPCWSTR name){++testOpens;return ::OpenFileMappingW(access,inherit,name);}
static LPVOID ymccTestMap(HANDLE h,DWORD access,DWORD high,DWORD low,SIZE_T n){++testMaps;return ::MapViewOfFile(h,access,high,low,n);}
static BOOL ymccTestUnmap(LPCVOID p){++testUnmaps;return ::UnmapViewOfFile(p);}
static HANDLE ymccTestOpenProcess(DWORD access,BOOL inherit,DWORD pid){++testProcesses;return ::OpenProcess(access,inherit,pid);}
#define OpenFileMappingW ymccTestOpenMapping
#define MapViewOfFile ymccTestMap
#define UnmapViewOfFile ymccTestUnmap
#define OpenProcess ymccTestOpenProcess
#include "rtss-under-test.h"
#undef OpenFileMappingW
#undef MapViewOfFile
#undef UnmapViewOfFile
#undef OpenProcess
#include "frame-output-under-test.h"
using namespace ymcc::monitorframe;
static void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static std::wstring uniqueName(){static unsigned serial=0;return L"Local\\YMCC_RTSS_PROBE_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(++serial);}
struct Fixture{
 std::wstring name;HANDLE map=nullptr;unsigned char* view=nullptr;std::size_t bytes=0;bool already=false;
 Fixture(std::uint32_t count=1,std::wstring selectedName=L""):name(selectedName.empty()?uniqueName():selectedName){
  bytes=sizeof(RtssHeader)+std::size_t(count)*sizeof(RtssAppPrefix);
  map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(bytes),name.c_str());already=GetLastError()==ERROR_ALREADY_EXISTS;require(map!=nullptr,"fixture mapping create failed");
  view=static_cast<unsigned char*>(MapViewOfFile(map,FILE_MAP_WRITE,0,0,bytes));require(view!=nullptr,"fixture view failed");std::memset(view,0,bytes);
  RtssHeader h{0x52545353,0x20015,sizeof(RtssAppPrefix),sizeof(RtssHeader),count,0,0,0,0,0};std::memcpy(view,&h,sizeof(h));
 }
 RtssHeader& header(){return *reinterpret_cast<RtssHeader*>(view);}
 RtssAppPrefix& app(std::uint32_t index=0){return *reinterpret_cast<RtssAppPrefix*>(view+header().appOffset+std::size_t(index)*header().appEntrySize);}
 void frame(unsigned n=1,std::uint32_t index=0){auto&a=app(index);a.pid=GetCurrentProcessId();a.time0=10000+n*1000;a.time1=a.time0+1000;a.frames=120;a.statFrames=n*120;a.frameBufferPos=n%1024;a.frameTimeUs=8000+n;}
 void close(){if(view)UnmapViewOfFile(view);view=nullptr;if(map)CloseHandle(map);map=nullptr;}
 ~Fixture(){close();}
};
static bool exists(const std::wstring& name){auto h=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(!h)return false;CloseHandle(h);return true;}
static void resetCounts(){testOpens=testMaps=testUnmaps=testScans=testSlots=testProcesses=0;}
static json counts(){return{{"mappingOpens",testOpens},{"maps",testMaps},{"unmaps",testUnmaps},{"tableScans",testScans},{"slotPidReads",testSlots},{"processOpens",testProcesses}};}

struct ChildFixture{
 HANDLE event=nullptr,process=nullptr;DWORD pid=0;std::uint64_t birth=0;
 ChildFixture(){
  const auto eventName=uniqueName()+L"_exit";event=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());require(event!=nullptr,"child fixture event failed");
  wchar_t exe[32768]{};require(GetModuleFileNameW(nullptr,exe,32768)>0,"child executable unavailable");std::wstring command=L"\""+std::wstring(exe)+L"\" --fixture-child \""+eventName+L"\"";std::vector<wchar_t> args(command.begin(),command.end());args.push_back(0);STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
  if(!CreateProcessW(exe,args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){CloseHandle(event);event=nullptr;throw std::runtime_error("child fixture create failed");}
  CloseHandle(pi.hThread);process=pi.hProcess;pid=pi.dwProcessId;FILETIME c{},e{},k{},u{};
  if(!GetProcessTimes(process,&c,&e,&k,&u)){SetEvent(event);WaitForSingleObject(process,3000);CloseHandle(process);CloseHandle(event);event=process=nullptr;throw std::runtime_error("child fixture birth failed");}
  birth=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
 }
 bool finish(){if(event)SetEvent(event);const bool exited=process&&WaitForSingleObject(process,3000)==WAIT_OBJECT_0;if(process)CloseHandle(process);if(event)CloseHandle(event);process=event=nullptr;return exited;}
 ~ChildFixture(){finish();}
};
int main(int argc,char**argv){if(argc==3&&std::string(argv[1])=="--fixture-child"){const std::wstring name(argv[2],argv[2]+std::strlen(argv[2]));auto h=OpenEventW(SYNCHRONIZE,FALSE,name.c_str());if(!h)return 3;const auto wait=WaitForSingleObject(h,20000);CloseHandle(h);return wait==WAIT_OBJECT_0?0:4;}if(argc!=3)return 2;const bool after=std::string(argv[2])=="after";json checks=json::array(),work=json::object();
 auto check=[&](const char* name,bool passed){checks.push_back({{"name",name},{"ok",passed}});if(!passed)throw std::runtime_error(name);};
 try{
  FILETIME c{},e{},k{},u{};require(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)!=FALSE,"current fixture process birth unavailable");const auto birth=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;const auto pid=GetCurrentProcessId();constexpr std::uint64_t epoch=1700000000000ULL;
  {
   Fixture f(1024);RtssReader reader(f.name.c_str());resetCounts();for(unsigned i=0;i<20;++i)check("missing hooked slot never fabricates a frame",!reader.sample(pid,birth,10000+i*100,epoch+i*100).valid);
   work["missingSlot20"]=counts();check("negative-slot lookup has bounded scan work",testScans==(after?2:20)&&testSlots==1024*(after?2:20));check("negative-slot misses reuse one process and mapping lease",testOpens==1&&testMaps==1&&testProcesses==1);
  }
  {
   Fixture f;RtssReader reader(f.name.c_str());resetCounts();reader.sample(pid,birth,10000,epoch);f.frame(1);auto early=reader.sample(pid,birth,10500,epoch+500);check("a newly hooked slot starts with warmup instead of a fabricated observation",!early.valid);
   if(after){check("missing-slot retry deadline prevents a repeated full scan",testScans==1);check("new slot is discovered at the existing one-second boundary",!reader.sample(pid,birth,11000,epoch+1000).valid);f.frame(2);check("next genuine marker after discovery is admitted",reader.sample(pid,birth,11100,epoch+1100).valid);}else{f.frame(2);check("frozen baseline accepts a new marker after immediate slot discovery",reader.sample(pid,birth,10600,epoch+600).valid);}
  }
  {
   Fixture f(2);RtssReader reader(f.name.c_str());resetCounts();reader.sample(pid,birth,10000,epoch);f.frame(1);f.header().appCount=1;
   check("a layout change bypasses an obsolete negative-search deadline",!reader.sample(pid,birth,10200,epoch+200).valid&&testScans==2);f.frame(2);check("layout rediscovery still needs real marker advancement",reader.sample(pid,birth,10300,epoch+300).valid);
  }
  {
   Fixture f;f.frame(1);RtssReader reader(f.name.c_str());resetCounts();check("positive cached slot has initial warmup",!reader.sample(pid,birth,10000,epoch).valid);
   for(unsigned i=1;i<=100;++i){f.frame(i+1);const auto sampled=reader.sample(pid,birth,10000+i*100,epoch+i*100);require(sampled.valid&&sampled.fps==120,"positive cached frame/FPS lost");}
   work["positive100"]=counts();check("continuously advancing producer never remaps or rescans",testScans==1&&testOpens==1&&testMaps==1&&testUnmaps==0&&testProcesses==1);
   const auto before=testUnmaps;check("unchanged marker does not renew observation",!reader.sample(pid,birth,20100,epoch+10100).valid);check("one transient duplicate does not close a healthy mapping",testUnmaps==before);
  }
  {
   Fixture f;f.frame(1);const auto name=f.name;RtssReader reader(name.c_str());resetCounts();reader.sample(pid,birth,10000,epoch);f.frame(2);require(reader.sample(pid,birth,10100,epoch+100).valid,"stale setup frame failed");f.close();check("fixture writer close leaves mapping pinned by reader until revalidation",exists(name));
   for(unsigned t=200;t<5100;t+=100)require(!reader.sample(pid,birth,10000+t,epoch+t).valid,"frozen mapping invented an observation");
   check("last valid marker keeps only its bounded mapping window",testUnmaps==0);
   check("stale recheck itself cannot create a frame",!reader.sample(pid,birth,15100,epoch+5100).valid);
   work["closedProducer"]=counts();check("unresponsive mapping reference is released at its proof deadline",after?!exists(name):exists(name));
   Fixture replacement(1,name);check("producer replacement no longer inherits the reader-pinned old mapping",replacement.already==!after);replacement.frame(20);
   const auto first=reader.sample(pid,birth,16100,epoch+6100);check("new mapping requires a warmup read",after?!first.valid:first.valid);replacement.frame(21);check("a genuinely advancing replacement resumes samples",reader.sample(pid,birth,16200,epoch+6200).valid);
  }
  {
   Fixture f;f.header().busy=1;const auto name=f.name;RtssReader reader(name.c_str());resetCounts();check("producer busy flag blocks initial observation",!reader.sample(pid,birth,10000,epoch).valid);f.close();check("busy frozen producer remains invalid",!reader.sample(pid,birth,15000,epoch+5000).valid);check("permanently busy retired mapping cannot remain pinned indefinitely",after?!exists(name):exists(name));
  }
  {
   const auto name=uniqueName();RtssReader reader(name.c_str());resetCounts();for(unsigned i=0;i<20;++i)require(!reader.sample(pid,birth,10000+i*100,epoch+i*100).valid,"absent mapping fabricated a frame");work["absentMapping20"]=counts();check("mapping absence retries only once per second",testOpens==2&&testMaps==0&&testScans==0&&testProcesses==1);
  }
  {
   Fixture f;f.frame(1);RtssReader reader(f.name.c_str());resetCounts();reader.sample(pid,birth,10000,epoch);f.frame(2);check("invalid birth cannot adopt another process identity",!reader.sample(pid,birth+1,10100,epoch+100).valid);check("restoring exact process lifetime starts warmup",!reader.sample(pid,birth,10200,epoch+200).valid);f.frame(3);check("valid identity recovers without fabricated bridging",reader.sample(pid,birth,10300,epoch+300).valid);
  }
  {
   Fixture f(2);f.frame(1,0);RtssReader reader(f.name.c_str());resetCounts();reader.sample(pid,birth,10000,epoch);f.frame(2,0);require(reader.sample(pid,birth,10100,epoch+100).valid,"slot move setup failed");f.app(0).pid=0;f.frame(5,1);check("cached-slot PID change initiates warmup in the moved slot",!reader.sample(pid,birth,10200,epoch+200).valid);f.frame(6,1);check("moved slot with new real marker is admitted",reader.sample(pid,birth,10300,epoch+300).valid);
  }

  for(unsigned kind=0;kind<3;++kind){
   Fixture f(2);RtssReader reader(f.name.c_str());resetCounts();reader.sample(pid,birth,10000,epoch);
   if(kind==0){f.header().version=0x20014;f.frame(1);}else if(kind==1){f.header().appOffset+=4;f.header().appCount=1;f.frame(1);}else{f.header().appEntrySize+=4;f.header().appCount=1;f.frame(1);}
   check("supported header geometry/version change triggers immediate safe rediscovery",!reader.sample(pid,birth,10200,epoch+200).valid&&testScans==2);f.frame(2);check("new supported layout admits only a subsequent genuine marker",reader.sample(pid,birth,10300,epoch+300).valid);
  }
  {
   Fixture f;f.frame(1);RtssReader reader(f.name.c_str());resetCounts();reader.sample(pid,birth,10000,epoch);f.frame(2);require(reader.sample(pid,birth,10100,epoch+100).valid,"invalid-duration setup failed");
   for(unsigned i=2;i<51;++i){f.frame(i+1);f.app().frameTimeUs=0;require(!reader.sample(pid,birth,10000+i*100,epoch+i*100).valid,"invalid duration admitted");}
   require(!reader.sample(pid,birth,15100,epoch+5100).valid,"invalid duration admitted at reprobe");check("changing invalid durations cannot indefinitely renew a mapped proof",after?testUnmaps==1:testUnmaps==0);
  }
  {
   Output output;output.bind(77,99,epoch);output.hardware(epoch/1000+1,120,85,45,20,"fixture",epoch+1000,8.3,3000);
   auto initial=output.packet(epoch+1000);check("unchanged output layer retains genuine HWiNFO 1% LOW",!initial.is_null()&&initial["fps1"]==85&&initial["fps1Source"]=="hwinfo-presented-1percent-low");
   require(output.observeRtss({true,8.1,125,epoch+1100}),"RTSS packet integration setup failed");auto mixed=output.packet(epoch+1200);
   check("real RTSS frame source can coexist with independent hardware LOW",mixed["fpsSource"]=="rtss"&&mixed["fps1"]==85&&mixed["frameTimeSource"]=="rtss-current-frame"&&mixed["frametime"]==8.1);
   for(unsigned i=0;i<100;++i){auto v=output.packet(epoch+1300+i);require(v["fpsObservedAt"]==epoch+1100&&v["fps1ObservedAt"]==epoch+1000,"packet read renewed measurement proof");}
   check("repeated packet consumption keeps original RTSS and LOW deadlines",output.packet(epoch+5900)["fpsValidUntil"]==epoch+6100&&output.packet(epoch+5900)["fps1ValidUntil"]==epoch+6000);
   auto lowExpired=output.packet(epoch+6000);check("expired hardware LOW becomes unavailable even while RTSS FPS is still fresh",!lowExpired.is_null()&&lowExpired["fps1"]==-1&&lowExpired["fpsSource"]=="rtss");
   output.hardware(epoch/1000+4,119,87,48,21,"fixture",epoch+4000,8.4,3000);auto fallback=output.packet(epoch+6100);
   check("RTSS source expiry retains newly observed HWiNFO fallback instead of a blank FPS family",!fallback.is_null()&&fallback["fpsSource"]=="hwinfo-presented"&&fallback["fps1"]==87&&fallback["frameTimeSource"]=="hwinfo-presented-avg");
   check("unadvanced fallback data eventually expires without self-renewal",output.packet(epoch+9000).is_null());
  }

  {
   ChildFixture child;Fixture f(1024);RtssReader reader(f.name.c_str());resetCounts();require(!reader.sample(child.pid,child.birth,10000,epoch).valid,"child missing slot fabricated data");require(child.finish(),"child fixture did not exit");
   check("real process termination is visible before a missing-slot retry deadline",reader.terminated());
   check("process termination releases mapping immediately without waiting for source grace",!reader.sample(child.pid,child.birth,10100,epoch+100).valid&&testUnmaps==1&&testScans==1);
   reader.reset();check("explicit reset also closes the terminated-process lease",!reader.terminated()&&!reader.alive());
  }
  bool ok=true;for(const auto&v:checks)ok=ok&&v["ok"].get<bool>();json result={{"ok",ok},{"side",after?"after":"before"},{"checks",checks.size()},{"cases",checks},{"work",work},{"realRtssTouched",false},{"hardwareReads",false},{"cpuPercentageMeasured",false},{"scope","Actual Windows RTSS reader on unique synthetic named mappings and the fixture process's exact creation time; counters only, not CPU timing."}};std::ofstream(argv[1])<<result.dump(2);std::cout<<json({{"ok",ok},{"side",after?"after":"before"},{"checks",checks.size()},{"work",work}}).dump(2);return ok?0:1;
 }catch(const std::exception& ex){std::ofstream(argv[1])<<json({{"ok",false},{"side",after?"after":"before"},{"error",ex.what()},{"cases",checks},{"work",work}}).dump(2);std::cerr<<ex.what();return 1;}
}
