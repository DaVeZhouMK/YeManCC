#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "json.hpp"
struct MemoryApi {
    alignas(8) std::array<unsigned char,1024> data{};
    DWORD error=44; unsigned opens=0, maps=0, unmaps=0, closes=0, sequenceReads=0;
    bool denyOpen=false, denyView=false;
    DWORD openAccess=0, viewAccess=0; BOOL inherit=TRUE; SIZE_T requestedBytes=0;
    std::wstring openedName;
} api;
static DWORD FakeGetLastError(){return api.error;}
static void FakeSetLastError(DWORD value){api.error=value;}
static HANDLE FakeOpenFileMappingW(DWORD access,BOOL inherit,LPCWSTR name){
    ++api.opens;api.openAccess=access;api.inherit=inherit;api.openedName=name;
    if(api.denyOpen){api.error=ERROR_ACCESS_DENIED;return nullptr;}return reinterpret_cast<HANDLE>(1);
}
static LPVOID FakeMapViewOfFile(HANDLE,DWORD access,DWORD,DWORD,SIZE_T bytes){
    ++api.maps;api.viewAccess=access;api.requestedBytes=bytes;
    if(api.denyView){api.error=ERROR_ACCESS_DENIED;return nullptr;}return api.data.data();
}
static BOOL FakeUnmapViewOfFile(LPCVOID){++api.unmaps;return TRUE;}
static BOOL FakeCloseHandle(HANDLE){++api.closes;return TRUE;}
static BOOL FakeQueryPerformanceCounter(LARGE_INTEGER* value){value->QuadPart=1600;return TRUE;}
static BOOL FakeQueryPerformanceFrequency(LARGE_INTEGER* value){value->QuadPart=1000;return TRUE;}
#undef MemoryBarrier
#define MemoryBarrier() std::atomic_thread_fence(std::memory_order_seq_cst)
#define GetLastError FakeGetLastError
#define SetLastError FakeSetLastError
#define OpenFileMappingW FakeOpenFileMappingW
#define MapViewOfFile FakeMapViewOfFile
#define UnmapViewOfFile FakeUnmapViewOfFile
#define CloseHandle FakeCloseHandle
#define QueryPerformanceCounter FakeQueryPerformanceCounter
#define QueryPerformanceFrequency FakeQueryPerformanceFrequency
#include "msi_host_stage_evidence.h"
using namespace ymcc::msi::hostStageEvidence;
static unsigned checks=0,cases=0;
static void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
static void put(Bytes& b,std::size_t at,std::uint64_t value){for(unsigned i=0;i<8;++i)b[at+i]=static_cast<unsigned char>(value>>(i*8));}
static void txt(Bytes& b,std::size_t at,std::size_t data,const std::string& value){put(b,at,value.size());std::memcpy(b.data()+data,value.data(),value.size());}
static Expected expected(){return {111,222,42,7,"0123456789abcdef0123456789abcdef","req-42","run-7","SUBMIT_FRAME"};}
static Bytes valid(){Bytes b{};put(b,0,magic);put(b,8,1);put(b,16,capacity);put(b,24,2);put(b,32,222);put(b,40,111);put(b,48,1000);put(b,56,6);put(b,64,1500);put(b,72,1000);put(b,80,1100);put(b,88,1200);put(b,96,1300);put(b,104,1500);put(b,144,42);put(b,152,7);put(b,160,3);put(b,168,512);put(b,176,15);put(b,184,41);txt(b,192,200,"0123456789abcdef0123456789abcdef");txt(b,264,272,"req-42");txt(b,336,344,"run-7");txt(b,408,416,"SUBMIT_FRAME");return b;}
struct SyntheticReader {
    Bytes bytes=valid(); std::vector<std::uint64_t> sequences; unsigned reads=0,copies=0,barriers=0;
    std::uint64_t sequence(){return reads<sequences.size()?sequences[reads++]:u64(bytes,24);}
    void barrier(){++barriers;}
    void copy(Bytes& out){++copies;out=bytes;}
};
static void reset(){api=MemoryApi{};api.data=valid();}
int main(int argc,char** argv){
 try{
#ifdef YMCC_FAULT_INJECT
    reset();auto r=read(expected());++cases;
    check(r["observed"]==false&&r["reason"]=="fault-build-no-live-mapping","fault build fails closed");
    check(api.opens==0&&api.maps==0&&api.closes==0,"fault build invokes no mapping API");
#else
    auto e=expected();auto b=valid();auto r=decode(b,e,1600,1000);++cases;
    check(r["observed"]==true&&r["currentRequestMatchesNativeRequest"]==true,"coherent exact request matched");
    check(r["phase"]=="submit-frame-handler"&&r["phaseAgeMsAtSnapshot"]==100.0,"stage clock decoded");
    check(r["parsedMetadataIsAdmissionProof"]==false&&r["writeReturnIsNativeReceiptProof"]==false&&r["steamConsumption"]=="unobserved","observations cannot become acceptance");
    check(r["submitScope"]=="TrySubmitFrame-including-encoding-backend-and-host-cost-window-path","handler scope honest");
    ++cases;check(r["extensionVersion"]==0&&r["extensionObserved"]==false,"old v1 has no extension evidence");
    for(const auto* key:{"backendBegin","backendEnd","costBegin","costEnd"})check(r["stagesQpc"][key].is_null(),"old v1 extension stamps unknown");
    for(const auto marker:{0ULL,2ULL,~0ULL}){
        auto bad=b;put(bad,448,marker);put(bad,456,1500);put(bad,464,1500);put(bad,472,1500);put(bad,480,1500);
        ++cases;auto row=decode(bad,e,1600,1000);check(row["observed"]==true&&row["extensionObserved"]==false,"legacy phase remains usable without recognizing invalid extension");
        for(const auto* key:{"backendBegin","backendEnd","costBegin","costEnd"})check(row["stagesQpc"][key].is_null(),"unmarked/unsupported stamps cannot pretend backend evidence");
        for(auto phase=12ULL;phase<=15;++phase){put(bad,56,phase);++cases;check(decode(bad,e,1600,1000)["observed"]==false,"new phase needs explicit supported extension");}
    }
    auto ext=b;put(ext,448,1);
    ++cases;r=decode(ext,e,1600,1000);check(r["extensionObserved"]==true&&r["stagesQpc"]["backendBegin"].is_null(),"supported extension zero stamp is unknown");
    for(auto phase=12ULL;phase<=15;++phase){
        auto at=456+8*(phase-12);put(ext,at,1501+10*(phase-12));put(ext,56,phase);put(ext,64,u64(ext,at));
        ++cases;r=decode(ext,e,1600,1000);check(r["observed"]==true&&r["extensionObserved"]==true&&r["phaseCode"]==phase,"each extension phase decoded");
        check(r["backendEndIsConsumerProof"]==false&&r["costReturnIsAckProof"]==false,"extension return is not consumer/ACK proof");
    }
    for(const auto mutation:{0,1,2,3,4}){auto bad=ext;
        if(mutation==0)put(bad,456,0); // end without begin
        if(mutation==1)put(bad,464,1499); // before submit/begin
        if(mutation==2)put(bad,472,1510); // before backend return
        if(mutation==3)put(bad,480,1601); // future cost return
        if(mutation==4)put(bad,104,0); // no enclosing submit
        ++cases;check(decode(bad,e,1600,1000)["observed"]==false,"invalid extension cannot assert a new phase");
        put(bad,56,7);++cases;auto row=decode(bad,e,1600,1000);check(row["observed"]==true&&row["extensionObserved"]==false&&row["stagesQpc"]["backendBegin"].is_null(),"invalid stamps stripped even in legacy phase");
    }
    {auto rejected=b;put(rejected,448,1);put(rejected,56,15);put(rejected,472,1500);put(rejected,480,1500);++cases;r=decode(rejected,e,1600,1000);check(r["extensionObserved"]==true&&r["stagesQpc"]["backendBegin"].is_null(),"profile rejection may cost-log without backend");}
    for(const auto phase:{0ULL,16ULL,~0ULL}){auto bad=b;put(bad,56,phase);++cases;check(decode(bad,e,1600,1000)["observed"]==false,"outside phase contract rejected");}
    for(const auto at:{0U,8U,16U}){auto bad=b;put(bad,at,999);++cases;check(decode(bad,e,1600,1000)["observed"]==false,"format drift rejected");}
    for(const auto seq:{0ULL,3ULL}){auto bad=b;put(bad,24,seq);++cases;check(decode(bad,e,1600,1000)["observed"]==false,"unpublished/odd data rejected");}
    for(const auto at:{32U,40U}){auto bad=b;put(bad,at,999);++cases;check(decode(bad,e,1600,1000)["observed"]==false,"wrong process tuple rejected");}
    {auto wrong=e;wrong.hostInstance[0]='f';++cases;check(decode(b,wrong,1600,1000)["observed"]==false,"host instance mismatch rejected");}
    {auto wrong=e;wrong.hostInstance="not-a-guid";++cases;check(decode(b,wrong,1600,1000)["observed"]==false,"unbound instance rejected");}
    for(const auto phase:{0ULL,12ULL}){auto bad=b;put(bad,56,phase);++cases;check(decode(bad,e,1600,1000)["observed"]==false,"invalid stage rejected");}
    for(auto wrong:std::vector<Expected>{Expected{111,222,43,7,e.hostInstance,e.requestId,e.runId,e.command},Expected{111,222,42,8,e.hostInstance,e.requestId,e.runId,e.command},Expected{111,222,42,7,e.hostInstance,"other",e.runId,e.command},Expected{111,222,42,7,e.hostInstance,e.requestId,"other",e.command},Expected{111,222,42,7,e.hostInstance,e.requestId,e.runId,"other"}}){++cases;r=decode(b,wrong,1600,1000);check(r["observed"]==true&&r["currentRequestMatchesNativeRequest"]==false,"metadata cannot match a different Native request");}
    {auto idle=b;put(idle,56,2);put(idle,144,0);put(idle,152,0);put(idle,176,1);put(idle,264,0);put(idle,336,0);put(idle,408,0);++cases;r=decode(idle,e,1600,1000);check(r["observed"]==true&&r["requestIdentityKnown"]==false&&r["currentRequestMatchesNativeRequest"]==false,"idle read never claims the submitted request was handled");check(r["lastResponseWriteReturnedSequence"]==41,"last write context survives read wait");}
    for(const auto at:{264U,336U,408U}){auto bad=b;put(bad,at,~0ULL);++cases;r=decode(bad,e,1600,1000);check(r["observed"]==true&&r["requestIdentityKnown"]==false,"invalid text remains unknown rather than truncated identity");}
    {auto bad=b;bad[272]=0xFF;++cases;r=decode(bad,e,1600,1000);check(r["currentRequestMatchesNativeRequest"]==false,"non-ASCII request cannot match");}
    for(const auto freq:{0ULL,2000ULL}){auto bad=b;put(bad,48,freq);++cases;r=decode(bad,e,1600,1000);check(r["clockComparable"]==false&&!r.contains("phaseAgeMsAtSnapshot"),"frequency mismatch is not a usable age");}
    {++cases;r=decode(b,e,1400,1000);check(r["clockComparable"]==false&&!r.contains("phaseAgeMsAtSnapshot"),"future timestamp cannot become negative age");}
    {SyntheticReader sr;sr.sequences={2,2};Bytes out{};unsigned attempts=0;++cases;check(stableCopy(sr,out,attempts)&&attempts==1&&sr.copies==1,"one coherent copy accepted");}
    {SyntheticReader sr;sr.sequences={3,3,3};Bytes out{};unsigned attempts=0;++cases;check(!stableCopy(sr,out,attempts)&&attempts==3&&sr.copies==0,"busy publication has three immediate attempts only");}
    {SyntheticReader sr;sr.sequences={2,4,2,4,2,4};Bytes out{};unsigned attempts=0;++cases;check(!stableCopy(sr,out,attempts)&&attempts==3&&sr.copies==3,"changed publication rejected after bounded retries");}
    {SyntheticReader sr;sr.sequences={4,4,4,4,4,4};Bytes out{};unsigned attempts=0;++cases;check(!stableCopy(sr,out,attempts),"copied publication must equal bracketing sequence");}
    reset();++cases;r=read(e);check(r["observed"]==true&&r["copyAttempts"]==1,"actual mapping adapter decoded through fake APIs");
    check(api.openAccess==FILE_MAP_READ&&api.viewAccess==FILE_MAP_READ&&api.inherit==FALSE&&api.requestedBytes==1024,"mapping adapter requests read-only exact bound");
    check(api.openedName==L"Local\\YeManCC-MSI-HostStage-v1-111-222-0123456789abcdef0123456789abcdef","exact mapping identity");
    check(api.opens==1&&api.maps==1&&api.unmaps==1&&api.closes==1&&api.error==44,"all handles/views balanced and original LastError retained");
    reset();api.denyOpen=true;++cases;r=read(e);check(r["observed"]==false&&r["win32Error"]==ERROR_ACCESS_DENIED&&api.maps==0&&api.closes==0&&api.error==44,"open denial not absence or controller failure");
    reset();api.denyView=true;++cases;r=read(e);check(r["observed"]==false&&api.closes==1&&api.unmaps==0&&api.error==44,"view denial closes mapping");
    reset();auto unbound=e;unbound.hostPid=0;++cases;r=read(unbound);check(r["observed"]==false&&api.opens==0,"unknown identity cannot open mapping");
    reset();put(api.data,8,99);++cases;r=read(e);check(r["observed"]==false&&api.unmaps==1&&api.closes==1,"malformed mapped data releases resources");
    if(argc>=3){
        std::ifstream file(argv[1],std::ios::binary);Bytes wire{};file.read(reinterpret_cast<char*>(wire.data()),wire.size());
        check(file.gcount()==capacity&&file.peek()==std::char_traits<char>::eof(),"actual C# fixture is exactly1024 bytes");
        std::ifstream metadata(argv[2]);Json m;metadata>>m;
        Expected fromProducer;fromProducer.parentPid=m.at("parentPid");fromProducer.hostPid=m.at("hostPid");fromProducer.sequence=m.at("requestSequence");fromProducer.epoch=m.at("epoch");fromProducer.hostInstance=m.at("hostInstanceId");fromProducer.requestId=m.at("requestId");fromProducer.runId=m.at("runId");fromProducer.command=m.at("command");
        ++cases;r=decode(wire,fromProducer,u64(wire,64)+u64(wire,48),u64(wire,48));
        check(r["observed"]==true&&r["currentRequestMatchesNativeRequest"]==true&&r["phase"]=="submit-frame-handler","Native consumes actual C# producer bytes");
        if(argc==4){std::ifstream extensionFile(argv[3],std::ios::binary);Bytes extensionWire{};extensionFile.read(reinterpret_cast<char*>(extensionWire.data()),extensionWire.size());
            ++cases;check(extensionFile.gcount()==capacity&&extensionFile.peek()==std::char_traits<char>::eof(),"extension wire exact capacity");
            r=decode(extensionWire,fromProducer,u64(extensionWire,64)+u64(extensionWire,48),u64(extensionWire,48));
            check(r["observed"]==true&&r["phase"]=="cost-window-returned"&&r["extensionObserved"]==true,"Native consumes actual C# extension wire");
            check(r["stagesQpc"]["backendBegin"]==1006&&r["stagesQpc"]["backendEnd"]==1007&&r["stagesQpc"]["costBegin"]==1008&&r["stagesQpc"]["costEnd"]==1009,"four actual producer QPC offsets agree");}
    }
#endif
    std::cout<<Json{{"passed",true},{"cases",cases},{"assertions",checks},{"realMappingAPICalls",0},{"productExecuted",false},{"scope","actual Native decoder and mapping adapter with fake Win32 boundaries; optional actual C# fake-sink fixture"}}.dump()<<'\n';return 0;
 }catch(const std::exception& ex){std::cout<<Json{{"passed",false},{"cases",cases},{"assertions",checks},{"failure",ex.what()}}.dump()<<'\n';return 1;}
}

