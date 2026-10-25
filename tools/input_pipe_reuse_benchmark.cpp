// R45 actual Runtime + same-process private message-pipe full round trips.
// Both headers are frozen, unmodified copies except their namespace token.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <realtimeapiset.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
namespace metric {
inline bool allocationEnabled=false,eventEnabled=false;
inline std::uint64_t requests=0,bytes=0,eventCreates=0,eventResets=0;
inline HANDLE event(LPSECURITY_ATTRIBUTES a,BOOL b,BOOL c,LPCWSTR d){if(eventEnabled)++eventCreates;return CreateEventW(a,b,c,d);}
inline BOOL reset(HANDLE h){if(eventEnabled)++eventResets;return ResetEvent(h);}
}
void* operator new(std::size_t n){if(metric::allocationEnabled){++metric::requests;metric::bytes+=n;}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#define CreateEventW metric::event
#define ResetEvent metric::reset
#define pipeTransport r45Baseline
#include "baseline_transport.h"
#undef pipeTransport
#define pipeTransport r45Candidate
#include "candidate_transport.h"
#undef pipeTransport
#undef CreateEventW
#undef ResetEvent
using json=nlohmann::json;
namespace base=ymcc::inputHost::r45Baseline;
namespace candidate=ymcc::inputHost::r45Candidate;
static unsigned pairOrdinal=0;
static volatile std::uint64_t sink=0;
static void require(bool b,const char* why){if(!b)throw std::runtime_error(std::string(why)+" error="+std::to_string(GetLastError()));}
struct NativeApi {
 using Runtime=base::Runtime;
 static base::Result write(Runtime& o,HANDLE h,void* p,DWORD n){return o.run(h,base::Kind::Write,p,n,1000);}
 static base::Result read(Runtime& o,HANDLE h,void* p,DWORD n){return o.run(h,base::Kind::Read,p,n,1000);}
 static base::Result connect(Runtime& o,HANDLE h){return o.run(h,base::Kind::Connect,nullptr,0,1000);}
};
struct CandidateApi {
 using Runtime=candidate::Runtime;
 static candidate::Result write(Runtime& o,HANDLE h,void* p,DWORD n){return o.run(h,candidate::Kind::Write,p,n,1000);}
 static candidate::Result read(Runtime& o,HANDLE h,void* p,DWORD n){return o.run(h,candidate::Kind::Read,p,n,1000);}
 static candidate::Result connect(Runtime& o,HANDLE h){return o.run(h,candidate::Kind::Connect,nullptr,0,1000);}
};
struct Peer {
 HANDLE pipe=INVALID_HANDLE_VALUE,event=nullptr;
 DWORD transfer(bool write,void* p,DWORD n){
  require(ResetEvent(event)!=FALSE,"peer ResetEvent");OVERLAPPED ov{};ov.hEvent=event;
  const BOOL issued=write?WriteFile(pipe,p,n,nullptr,&ov):ReadFile(pipe,p,n,nullptr,&ov);DWORD error=issued?0:GetLastError();
  if(!issued&&error==ERROR_IO_PENDING){
   const DWORD wait=WaitForSingleObject(event,2000);
   if(wait!=WAIT_OBJECT_0){CancelIoEx(pipe,&ov);if(WaitForSingleObject(event,2000)!=WAIT_OBJECT_0)std::abort();}
  }else require(issued!=FALSE,"peer issue");
  DWORD got=0;require(GetOverlappedResult(pipe,&ov,&got,FALSE)!=FALSE,"peer result");return got;
 }
 ~Peer(){if(event)CloseHandle(event);if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);}
};
template<class Api> struct RoundTrip {
 typename Api::Runtime* owner=new typename Api::Runtime();
 HANDLE server=INVALID_HANDLE_VALUE;Peer peer;
 std::vector<unsigned char> request,response,nativeRead,peerRead;
 DWORD requestBytes,responseBytes;
 RoundTrip(DWORD req,DWORD res):request(req,0x61),response(res,0x39),nativeRead(16384,0xA7),peerRead(16384,0xA7),requestBytes(req),responseBytes(res){
  const std::wstring name=L"\\\\.\\pipe\\YMCC-R45-perf-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++pairOrdinal);
  server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,16384,16384,0,nullptr);require(server!=INVALID_HANDLE_VALUE,"private server");DWORD error=0;require(owner->adopt(server,name,error),"private owner adopt");
  peer.pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);require(peer.pipe!=INVALID_HANDLE_VALUE,"private client");DWORD mode=PIPE_READMODE_MESSAGE;require(SetNamedPipeHandleState(peer.pipe,&mode,nullptr,nullptr)!=FALSE,"peer message mode");peer.event=CreateEventW(nullptr,TRUE,FALSE,nullptr);require(peer.event!=nullptr,"peer event");ULONG pid=0;require(GetNamedPipeClientProcessId(server,&pid)!=FALSE&&pid==GetCurrentProcessId(),"client peer PID");pid=0;require(GetNamedPipeServerProcessId(peer.pipe,&pid)!=FALSE&&pid==GetCurrentProcessId(),"server peer PID");require(Api::connect(*owner,server).ok,"private connect");
 }
 std::uint64_t one(){
  auto write=Api::write(*owner,server,request.data(),requestBytes);require(write.ok&&write.byteCountKnown&&write.transferred==requestBytes,"native request write");require(peer.transfer(false,peerRead.data(),16384)==requestBytes,"peer request read");require(peer.transfer(true,response.data(),responseBytes)==responseBytes,"peer response write");auto read=Api::read(*owner,server,nativeRead.data(),16384);require(read.ok&&read.byteCountKnown&&read.transferred==responseBytes,"native response read");return read.transferred+write.transferred+nativeRead[0]+nativeRead[responseBytes-1]+peerRead[0]+peerRead[requestBytes-1];
 }
 void verify(){require(std::equal(request.begin(),request.end(),peerRead.begin()),"full request parity");require(std::equal(response.begin(),response.end(),nativeRead.begin()),"full response parity");auto s=owner->snapshot();require(s.available&&s.pending==0&&s.payloadBytes==0&&s.admitted==s.reaped,"roundtrip ownership/accounting");}
 ~RoundTrip(){owner->close(server);owner->sealAndReap();}
};
template<class Api> struct Rejected {
 typename Api::Runtime* owner=new typename Api::Runtime();bool oversized;
 volatile DWORD size;
 std::array<unsigned char,1> data{};
 explicit Rejected(bool over):oversized(over),size(over?16385:1){if(!over)owner->sealAndReap();}
 std::uint64_t one(){auto r=Api::read(*owner,nullptr,data.data(),size);require(!r.ok&&!r.admitted&&r.error==(oversized?ERROR_INVALID_PARAMETER:ERROR_SHUTDOWN_IN_PROGRESS),"guard control result");return r.error;}
};
struct Sample {double elapsedNs=0,cycles=0;};
static LARGE_INTEGER frequency{};
template<class Work> static Sample timing(Work& work,unsigned iterations){ULONG64 beforeCycles=0,afterCycles=0;LARGE_INTEGER before{},after{};require(QueryThreadCycleTime(GetCurrentThread(),&beforeCycles)!=FALSE,"cycle start");require(QueryPerformanceCounter(&before)!=FALSE,"QPC start");std::uint64_t sum=0;for(unsigned i=0;i<iterations;++i)sum+=work.one();require(QueryPerformanceCounter(&after)!=FALSE,"QPC end");require(QueryThreadCycleTime(GetCurrentThread(),&afterCycles)!=FALSE,"cycle end");sink=sum;return {static_cast<double>(after.QuadPart-before.QuadPart)*1e9/static_cast<double>(frequency.QuadPart)/iterations,static_cast<double>(afterCycles-beforeCycles)/iterations};}
template<class Work> static json allocation(Work& work,unsigned iterations){metric::requests=metric::bytes=metric::eventCreates=metric::eventResets=0;metric::allocationEnabled=true;metric::eventEnabled=true;std::uint64_t sum=0;for(unsigned i=0;i<iterations;++i)sum+=work.one();metric::allocationEnabled=false;metric::eventEnabled=false;sink=sum;return {{"iterations",iterations},{"allocationRequests",metric::requests},{"requestedBytes",metric::bytes},{"eventCreates",metric::eventCreates},{"eventResets",metric::eventResets}};}
static double median(std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];}
static json compare(const std::vector<double>& b,const std::vector<double>& c,bool affected){std::vector<double> paired;unsigned faster=0;for(std::size_t i=0;i<b.size();++i){require(b[i]>0&&c[i]>0,"nonpositive timing/cycles");paired.push_back(1-c[i]/b[i]);if(c[i]<b[i])++faster;}const double reduction=median(paired),ratio=1-median(c)/median(b);bool pass=affected?(faster>=10&&reduction>=.05&&ratio>=.05):(reduction>=-.05&&ratio>=-.05);return {{"baseline",b},{"candidate",c},{"baselineMedian",median(b)},{"candidateMedian",median(c)},{"pairedMedianReduction",reduction},{"ratioOfMediansReduction",ratio},{"fasterRounds",faster},{"pass",pass}};}
template<class A,class B> static json row(const char* name,A& a,B& b,bool affected,unsigned campaign,unsigned scenario,unsigned iterations){
 for(unsigned i=0;i<128;++i){sink=a.one();sink=b.one();}
 std::vector<double> an,bn,ac,bc;
 for(unsigned round=0;round<11;++round){Sample x,y;if((round+campaign+scenario)%2==0){x=timing(a,iterations);y=timing(b,iterations);}else{y=timing(b,iterations);x=timing(a,iterations);}an.push_back(x.elapsedNs);bn.push_back(y.elapsedNs);ac.push_back(x.cycles);bc.push_back(y.cycles);}
 auto allocA=allocation(a,32),allocB=allocation(b,32);const bool allocPass=allocB.at("allocationRequests").template get<std::uint64_t>()<=allocA.at("allocationRequests").template get<std::uint64_t>()&&allocB.at("requestedBytes").template get<std::uint64_t>()<=allocA.at("requestedBytes").template get<std::uint64_t>()&&allocB.at("eventCreates").template get<std::uint64_t>()<=allocA.at("eventCreates").template get<std::uint64_t>();const bool eventPass=!affected||allocB.at("eventCreates").template get<std::uint64_t>()<allocA.at("eventCreates").template get<std::uint64_t>();auto elapsed=compare(an,bn,affected),cycles=compare(ac,bc,affected);const bool pass=elapsed.at("pass").get<bool>()&&cycles.at("pass").get<bool>()&&allocPass&&eventPass;std::cerr<<"campaign="<<campaign<<" scenario="<<name<<" pass="<<(pass?"true":"false")<<'\n';return {{"scenario",name},{"affected",affected},{"campaign",campaign},{"iterationsPerSample",iterations},{"elapsedNsPerOperation",elapsed},{"threadCyclesPerOperation",cycles},{"baselineAllocation",allocA},{"candidateAllocation",allocB},{"allocationPass",allocPass},{"eventCreatePass",eventPass},{"pass",pass}};
}
int main(){HANDLE mutex=nullptr;DWORD_PTR oldAffinity=0;try{
 mutex=CreateMutexW(nullptr,FALSE,L"Local\\YMCC-VirtualInputPerf-R45-measurement");require(mutex!=nullptr,"measurement mutex");const auto lock=WaitForSingleObject(mutex,1000);require(lock==WAIT_OBJECT_0||lock==WAIT_ABANDONED,"measurement mutex busy");DWORD_PTR allowed=0,system=0;DWORD_PTR selected=0;if(GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system)){const DWORD cpu=GetCurrentProcessorNumber();const DWORD_PTR current=cpu<sizeof(DWORD_PTR)*8?(static_cast<DWORD_PTR>(1)<<cpu):0;selected=(current&allowed)?current:(allowed&(~allowed+1));if(selected)oldAffinity=SetThreadAffinityMask(GetCurrentThread(),selected);}require(QueryPerformanceFrequency(&frequency)!=FALSE&&frequency.QuadPart>0,"QPC frequency");DWORD before=0,after=0;require(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial handle count");json rows=json::array();bool passed=true;
 {
  RoundTrip<NativeApi> a64(64,64);RoundTrip<CandidateApi> b64(64,64);RoundTrip<NativeApi> a4096(4096,1024);RoundTrip<CandidateApi> b4096(4096,1024);RoundTrip<NativeApi> a16384(16384,16384);RoundTrip<CandidateApi> b16384(16384,16384);Rejected<NativeApi> ao(true),as(false);Rejected<CandidateApi> bo(true),bs(false);
  for(unsigned campaign=0;campaign<2;++campaign){rows.push_back(row("private-roundtrip-64",a64,b64,true,campaign,0,512));rows.push_back(row("private-roundtrip-4096",a4096,b4096,true,campaign,1,512));rows.push_back(row("private-roundtrip-16384",a16384,b16384,true,campaign,2,512));rows.push_back(row("oversize-rejected",ao,bo,false,campaign,3,32768));rows.push_back(row("sealed-rejected",as,bs,false,campaign,4,32768));}
  a64.verify();b64.verify();a4096.verify();b4096.verify();a16384.verify();b16384.verify();
 }
 for(const auto& r:rows)passed=passed&&r.at("pass").get<bool>();require(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"benchmark pipe/event handle leak");if(oldAffinity)SetThreadAffinityMask(GetCurrentThread(),oldAffinity);ReleaseMutex(mutex);CloseHandle(mutex);std::cout<<json({{"executionPass",true},{"performancePass",passed},{"rows",rows},{"privatePipePairs",pairOrdinal},{"processHandlesBefore",before},{"processHandlesAfter",after},{"threadAffinityApplied",oldAffinity!=0},{"selectedAffinityMask",static_cast<std::uint64_t>(selected)},{"priorityChanged",false},{"cyclesAreRawNotConvertedToTime",true},{"timingsIncludeSameThreadPeerAndRuntime",true},{"fullHostProtocolOrDevicesExecuted",false},{"productionCandidateApplied",false}}).dump(2)<<'\n';return 0;
 }catch(const std::exception& e){if(oldAffinity)SetThreadAffinityMask(GetCurrentThread(),oldAffinity);if(mutex){ReleaseMutex(mutex);CloseHandle(mutex);}std::cerr<<e.what()<<'\n';return 1;}}
