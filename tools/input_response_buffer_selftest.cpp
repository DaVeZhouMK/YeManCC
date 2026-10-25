// R46 current declaration + exact scanner/DOM expression, actual R45 Runtime.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <realtimeapiset.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
#include "input_host_pipe_transport.h"
using json=nlohmann::json;
namespace transport=ymcc::inputHost::pipeTransport;
namespace metric {inline bool enabled=false;inline std::uint64_t requests=0,bytes=0;}
void* operator new(std::size_t n){if(metric::enabled){++metric::requests;metric::bytes+=n;}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static std::string g_inputHostRunId="r46-run",g_inputHostStartupNonce="r46-nonce",g_inputHostHostInstanceId="H";
static unsigned long long g_inputHostEpoch=17,g_inputHostConfigRevision=5;
static constexpr const char* kInputHostTargetId="virtual-gamepad",*kInputHostOwnerIdentity="r46-owner",*kInputHostProtocolConfigHash="r46-config",*kInputHostLockedCoreSha256="r46-core",*kInputHostLockedCoreMvid="r46-mvid";
static unsigned long long inputHostProtocolPowerGenerationLocked(){return 19;}
static const char* inputHostBoundOrCurrentPersona(){return "steamdeck";}
static const char* inputHostBoundOrCurrentProfileIdentity(){return "r46-profile";}
#include "actual-scanner.inc"
static unsigned checks=0,pairOrdinal=0;
static void require(bool b,const char* why){if(!b)throw std::runtime_error(std::string(why)+" error="+std::to_string(GetLastError()));}
static void check(bool b,const char* why){++checks;require(b,why);}
static json receipt(){return {{"protocolVersion",1},{"command","SUBMIT_FRAME"},{"requestId","r46-request"},{"sequence",41},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},{"powerGeneration",inputHostProtocolPowerGenerationLocked()},{"targetId",kInputHostTargetId},{"ownerIdentity",kInputHostOwnerIdentity},{"persona",inputHostBoundOrCurrentPersona()},{"profileIdentity",inputHostBoundOrCurrentProfileIdentity()},{"startupNonce",g_inputHostStartupNonce},{"configRevision",g_inputHostConfigRevision},{"configHash",kInputHostProtocolConfigHash},{"assetVerified",true},{"lockedCoreSha256",kInputHostLockedCoreSha256},{"lockedCoreMvid",kInputHostLockedCoreMvid},{"hostInstanceId",g_inputHostHostInstanceId},{"ok",true},{"status","frame-accepted"},{"submitElapsedUs",7},{"firstFrame",false}};}
static std::string payload(std::size_t size){auto j=receipt();j["padding"]="";auto s=j.dump();require(s.size()<=size,"fixture target size too small");j["padding"]=std::string(size-s.size(),'x');s=j.dump();require(s.size()==size,"fixture target size mismatch");return s;}
struct Outcome {bool accepted=false,fast=false;DWORD count=0,error=0;unsigned long long submit=0;bool first=false;bool operator==(const Outcome&)const=default;};
// This receipt seam is deliberately bounded: it executes the actual scanner
// and actual DOM parse expression. It is not the whole production tuple/lifecycle.
template<class Work> __declspec(noinline) static Outcome baselineReceive(Work& work,bool poison){
#include "baseline-declaration.inc"
#include "receive-body.inc"
}
template<class Work> __declspec(noinline) static Outcome candidateReceive(Work& work,bool poison){
#include "candidate-declaration.inc"
#include "receive-body.inc"
}
struct Scripted {
 std::string body;bool fast=true,readOk=true;DWORD error=0;bool writeOk=true;
 std::array<unsigned char,64> request{};
 bool io(bool write,void* p,DWORD n,DWORD* transferred,DWORD* errorOut){if(transferred)*transferred=0;if(errorOut)*errorOut=0;if(write){if(!writeOk){*errorOut=ERROR_BROKEN_PIPE;return false;}return true;}if(!readOk){if(errorOut)*errorOut=error;return false;}require(body.size()<=n,"script payload exceeds response capacity");if(!body.empty())std::memcpy(p,body.data(),body.size());if(transferred)*transferred=static_cast<DWORD>(body.size());return true;}
 void peerRoundtrip(){}
};
static void scriptedCorrectness(){std::vector<Scripted> cases;for(auto size:{768u,1024u,12288u,16384u})for(bool fast:{false,true})cases.push_back({payload(size),fast});for(auto s:{"", "{", "[]", "null", "false", "{\"ok\":true}", "{\"requestId\":\"r46-request\",\"ok\":false}", "{\"requestId\":\"r46-request\",\"ok\":true}junk"})for(bool fast:{false,true})cases.push_back({s,fast});for(DWORD error:{WAIT_TIMEOUT,ERROR_INVALID_HANDLE,ERROR_MORE_DATA,ERROR_NO_DATA,ERROR_INVALID_DATA}){Scripted s;s.readOk=false;s.error=error;cases.push_back(s);}for(unsigned n=0;n<300;++n){auto j=receipt();j["padding"]=std::string(n%97,static_cast<char>('a'+n%26));if(n%5==0)j.erase("requestId");if(n%7==0)j["sequence"]=40;if(n%11==0)j["ok"]=false;if(n%13==0)j["status"]=false;cases.push_back({j.dump(),n%2==0});}
 for(auto& c:cases){auto b=baselineReceive(c,true),a=candidateReceive(c,true);check(a==b,"declaration candidate changed controlled receipt outcome");if(!c.readOk)check(!a.accepted&&a.error==c.error&&a.count==0,"failed read consumed stale local bytes");}
 // Access beyond the exact valid prefix would cross into PAGE_NOACCESS. The
 // production scanner receives the same [begin,end) contract in both variants.
 SYSTEM_INFO info{};GetSystemInfo(&info);const SIZE_T page=info.dwPageSize;auto* memory=static_cast<char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));check(memory!=nullptr,"guard pages allocate");DWORD previous=0;check(VirtualProtect(memory+page,page,PAGE_NOACCESS,&previous)!=FALSE,"guard page protect");auto full=payload(1024);for(std::size_t n:{0u,1u,2u,7u,128u,1024u}){const char* begin=memory+page-n;if(n)std::memcpy(const_cast<char*>(begin),full.data(),n);InputHostFastReceipt out;(void)c7aReceiptFast::scanSubmitFrameReceipt(begin,begin+n,"frame-accepted","r46-request",41,out);auto parsed=json::parse(std::string(begin,begin+n),nullptr,false);(void)parsed;++checks;}VirtualFree(memory,0,MEM_RELEASE);}
struct Peer {HANDLE h=INVALID_HANDLE_VALUE,event=nullptr;DWORD transfer(bool write,void* p,DWORD n){require(ResetEvent(event)!=FALSE,"peer reset");OVERLAPPED ov{};ov.hEvent=event;const BOOL issued=write?WriteFile(h,p,n,nullptr,&ov):ReadFile(h,p,n,nullptr,&ov);const DWORD e=issued?0:GetLastError();if(!issued&&e==ERROR_IO_PENDING){const DWORD w=WaitForSingleObject(event,2000);if(w!=WAIT_OBJECT_0){CancelIoEx(h,&ov);if(WaitForSingleObject(event,2000)!=WAIT_OBJECT_0)std::abort();throw std::runtime_error("peer timed batch invalid");}}else require(issued!=FALSE,"peer issue");DWORD got=0;require(GetOverlappedResult(h,&ov,&got,FALSE)!=FALSE,"peer query");return got;}~Peer(){if(event)CloseHandle(event);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Real {
 transport::Runtime* owner=new transport::Runtime();HANDLE server=INVALID_HANDLE_VALUE;Peer peer;
 std::string body;bool fast;std::array<unsigned char,64> request{};std::array<unsigned char,64> peerRead{};
 Real(unsigned size,bool useFast):body(payload(size)),fast(useFast){const std::wstring name=L"\\\\.\\pipe\\YMCC-R46-response-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++pairOrdinal);server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,16384,16384,0,nullptr);require(server!=INVALID_HANDLE_VALUE,"private server");DWORD e=99;require(owner->adopt(server,name,e),"private adopt");peer.h=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);require(peer.h!=INVALID_HANDLE_VALUE,"private client");DWORD mode=PIPE_READMODE_MESSAGE;require(SetNamedPipeHandleState(peer.h,&mode,nullptr,nullptr)!=FALSE,"peer mode");peer.event=CreateEventW(nullptr,TRUE,FALSE,nullptr);require(peer.event!=nullptr,"peer event");ULONG pid=0;require(GetNamedPipeClientProcessId(server,&pid)!=FALSE&&pid==GetCurrentProcessId(),"client peer PID");pid=0;require(GetNamedPipeServerProcessId(peer.h,&pid)!=FALSE&&pid==GetCurrentProcessId(),"server peer PID");require(owner->run(server,transport::Kind::Connect,nullptr,0,1000).ok,"private connect");}
 bool io(bool write,void* data,DWORD n,DWORD* transferred,DWORD* error){auto r=owner->run(server,write?transport::Kind::Write:transport::Kind::Read,data,n,1000);if(transferred)*transferred=r.transferred;if(error)*error=r.error;return r.ok;}
 void peerRoundtrip(){require(peer.transfer(false,peerRead.data(),64)==64,"peer request count");require(peerRead==request,"peer request bytes");require(peer.transfer(true,body.data(),static_cast<DWORD>(body.size()))==body.size(),"peer response count");}
 ~Real(){owner->close(server);owner->sealAndReap();}
};
template<bool Candidate> struct TimedWork {Real& real;std::uint64_t one(){auto r=Candidate?candidateReceive(real,false):baselineReceive(real,false);require(r.accepted&&r.count==real.body.size()&&r.fast==real.fast&&r.submit==7&&!r.first,"timed receipt invalid");return r.count+r.submit+static_cast<unsigned>(r.fast);}};
struct Guard {bool sealed=false;volatile bool rejected=true;std::uint64_t one(){if(rejected)return sealed?ERROR_SHUTDOWN_IN_PROGRESS:ERROR_INVALID_PARAMETER;return 0;}};
static volatile std::uint64_t sink=0;static LARGE_INTEGER frequency{};
struct Sample {double ns=0,cycles=0;};
template<class W> static Sample timing(W& work,unsigned n){ULONG64 c0=0,c1=0;LARGE_INTEGER t0{},t1{};require(QueryThreadCycleTime(GetCurrentThread(),&c0)!=FALSE,"cycles begin");require(QueryPerformanceCounter(&t0)!=FALSE,"QPC begin");std::uint64_t sum=0;for(unsigned i=0;i<n;++i)sum+=work.one();require(QueryPerformanceCounter(&t1)!=FALSE,"QPC end");require(QueryThreadCycleTime(GetCurrentThread(),&c1)!=FALSE,"cycles end");sink=sum;return {static_cast<double>(t1.QuadPart-t0.QuadPart)*1e9/static_cast<double>(frequency.QuadPart)/n,static_cast<double>(c1-c0)/n};}
template<class W> static json allocation(W& work){metric::requests=metric::bytes=0;metric::enabled=true;std::uint64_t sum=0;for(unsigned i=0;i<32;++i)sum+=work.one();metric::enabled=false;sink=sum;return {{"iterations",32},{"requests",metric::requests},{"bytes",metric::bytes}};}
static double median(std::vector<double> x){std::sort(x.begin(),x.end());return x[x.size()/2];}
static json comparison(const std::vector<double>& a,const std::vector<double>& b,bool affected){std::vector<double> ratios;unsigned faster=0;for(std::size_t i=0;i<a.size();++i){require(a[i]>0&&b[i]>0,"invalid timing");ratios.push_back(1-b[i]/a[i]);faster+=b[i]<a[i];}const double paired=median(ratios),ratio=1-median(b)/median(a);return {{"baseline",a},{"candidate",b},{"pairedMedianReduction",paired},{"ratioOfMediansReduction",ratio},{"fasterPairs",faster},{"pass",affected?(paired>=.05&&ratio>=.05&&faster>=10):(paired>=-.05&&ratio>=-.05)}};}
template<class A,class B> static json row(const char* name,A& a,B& b,bool affected,unsigned campaign,unsigned scenario){for(unsigned i=0;i<128;++i){sink=a.one();sink=b.one();}const unsigned n=affected?512:32768;std::vector<double> an,bn,ac,bc;for(unsigned r=0;r<11;++r){Sample x,y;if((r+campaign+scenario)%2){y=timing(b,n);x=timing(a,n);}else{x=timing(a,n);y=timing(b,n);}an.push_back(x.ns);bn.push_back(y.ns);ac.push_back(x.cycles);bc.push_back(y.cycles);}auto aa=allocation(a),ba=allocation(b);const bool ap=ba.at("requests").template get<std::uint64_t>()<=aa.at("requests").template get<std::uint64_t>()&&ba.at("bytes").template get<std::uint64_t>()<=aa.at("bytes").template get<std::uint64_t>()&&(!affected||ba.at("requests").template get<std::uint64_t>()<aa.at("requests").template get<std::uint64_t>());auto elapsed=comparison(an,bn,affected),cycles=comparison(ac,bc,affected);bool pass=elapsed.at("pass").template get<bool>()&&cycles.at("pass").template get<bool>()&&ap;std::cerr<<name<<" campaign="<<campaign<<" pass="<<pass<<'\n';return {{"scenario",name},{"campaign",campaign},{"affected",affected},{"iterationsPerSample",n},{"elapsedNsPerRoundTrip",elapsed},{"rawThreadCyclesPerRoundTrip",cycles},{"baselineAllocation",aa},{"candidateAllocation",ba},{"allocationPass",ap},{"pass",pass}};}
static json correctness(){DWORD before=0,after=0;require(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial handles");scriptedCorrectness();for(unsigned size:{1024u,12288u})for(bool fast:{false,true}){Real p(size,fast);auto a=baselineReceive(p,true),b=candidateReceive(p,true);check(a==b&&a.accepted&&a.fast==fast&&a.count==size,"actual pipe receipt parity");}require(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&before==after,"correctness handle leak");return {{"pass",true},{"checks",checks},{"privatePipePairs",pairOrdinal},{"processHandlesBefore",before},{"processHandlesAfter",after},{"sameSourceScannerAndDOMExpressionExecuted",true},{"prefixGuardPageCases",6},{"unusedBufferTailPoisonedInCorrectness",true},{"realHostProtocolOrDeviceExecuted",false}};}
static json benchmark(){require(QueryPerformanceFrequency(&frequency)!=FALSE,"QPC frequency");HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\YMCC-VirtualInputPerf-R46-measurement");require(mutex!=nullptr,"measurement mutex");const auto held=WaitForSingleObject(mutex,1000);require(held==WAIT_OBJECT_0||held==WAIT_ABANDONED,"measurement busy");DWORD_PTR affinity=0,oldAffinity=0,allowed=0,system=0;if(GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system)){DWORD cpu=GetCurrentProcessorNumber();const auto mask=cpu<sizeof(DWORD_PTR)*8?(static_cast<DWORD_PTR>(1)<<cpu):0;affinity=mask&allowed?mask:(allowed&(~allowed+1));if(affinity)oldAffinity=SetThreadAffinityMask(GetCurrentThread(),affinity);}DWORD before=0,after=0;require(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"before handles");json rows=json::array();{
 Real a1024(1024,true),b1024(1024,true),a12288(12288,true),b12288(12288,true),adom(1024,false),bdom(1024,false);TimedWork<false> x1{a1024},x2{a12288},x3{adom};TimedWork<true> y1{b1024},y2{b12288},y3{bdom};Guard ga{false},gb{false},sa{true},sb{true};for(unsigned c=0;c<2;++c){rows.push_back(row("fast-receipt-1024",x1,y1,true,c,0));rows.push_back(row("fast-receipt-12288",x2,y2,true,c,1));rows.push_back(row("dom-receipt-1024",x3,y3,true,c,2));rows.push_back(row("before-receive-guard-rejected",ga,gb,false,c,3));rows.push_back(row("before-receive-sealed-rejected",sa,sb,false,c,4));}}
 require(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&before==after,"benchmark handles leaked");if(oldAffinity)SetThreadAffinityMask(GetCurrentThread(),oldAffinity);ReleaseMutex(mutex);CloseHandle(mutex);bool pass=true;for(auto& r:rows)pass=pass&&r.at("pass").get<bool>();return {{"executionPass",true},{"performancePass",pass},{"rows",rows},{"privatePipePairs",pairOrdinal},{"processHandlesBefore",before},{"processHandlesAfter",after},{"affinityApplied",oldAffinity!=0},{"priorityChanged",false},{"fullRequestStateMachineOrHostProtocolExecuted",false},{"productionCandidateApplied",false}};}
int main(int argc,char** argv){try{const bool timed=argc==2&&std::string(argv[1])=="--benchmark";std::cout<<(timed?benchmark():correctness()).dump(2)<<'\n';return 0;}catch(const std::exception& e){metric::enabled=false;std::cerr<<e.what()<<'\n';return 1;}}
