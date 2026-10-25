// R39 SYNTHETIC actual-source proof. All Windows names below are deterministic stand-ins.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
struct Counts {uint64_t requests=0,bytes=0,alignedRequests=0,alignedBytes=0;};
static bool allocationCounting=false;static Counts allocationCounts;
void* operator new(std::size_t n){if(allocationCounting){++allocationCounts.requests;allocationCounts.bytes+=n;}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){if(allocationCounting){++allocationCounts.alignedRequests;allocationCounts.alignedBytes+=n;}if(void* p=_aligned_malloc(n?n:1,static_cast<std::size_t>(a)))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}
#include "json.hpp"
using json=nlohmann::json;
#include "actual-command.h"
// Pure diagnostic Session is actual current source; expose state for oracle inspection only.
#define private public
#include "actual-session.h"
#undef private
using DWORD=uint32_t;using ULONGLONG=unsigned long long;using BOOL=int;using HANDLE=void*;
constexpr BOOL TRUE=1,FALSE=0;
constexpr DWORD ERROR_SUCCESS=0,ERROR_INVALID_HANDLE=6,ERROR_NOT_ENOUGH_MEMORY=8,ERROR_BROKEN_PIPE=109,ERROR_IO_PENDING=997,ERROR_IO_INCOMPLETE=996,ERROR_OPERATION_ABORTED=995,ERROR_PIPE_CONNECTED=535,ERROR_NOT_FOUND=1168,ERROR_ACCESS_DENIED=5;
constexpr DWORD WAIT_OBJECT_0=0,WAIT_TIMEOUT=258,WAIT_FAILED=0xffffffffU;
struct OVERLAPPED{uintptr_t Internal=0,InternalHigh=0;DWORD Offset=0,OffsetHigh=0;HANDLE hEvent=nullptr;};
enum class IoCase {Sync,PendingSuccess,PendingFailure,StartFailure,EventFailure,TimeoutPending,TimeoutComplete,FailedPending,FailedCancelRejected,FailedSecondWait,FailedCleanupClearsError,ConnectedAlready};
struct Config {
 std::string name="synthetic",command="SUBMIT_FRAME",persona="steamdeck",buildPersona,boundPersona;
 std::string runId="run-synthetic",nonce="nonce-synthetic",requestId="01234567-89AB-4CDE-8123-0123456789AB",sessionId="r39-session",metadataOverride;
 unsigned long long sequence=40,boundPower=29,power=31,now=1000;
 bool prepared=true,validProcess=true,validPipeName=true,serverPresent=true,connected=true,knownBoard=true,detail=true,explicitRequest=true,entrySeam=false,aliasId=false;
 bool nullId=false,nullSequence=false,exitFence=false,powerReady=true;
 int phase=3; // 0 closed,1 pending,2 offered,3 active; Session transitions remain real.
 IoCase connectCase=IoCase::Sync,writeCase=IoCase::Sync,readCase=IoCase::Sync;
 std::string customWire;bool useCustomWire=false;bool requireFreshSource=true;
};
struct Operation {int kind=0;bool pending=false;bool issued=false;const void* buffer=nullptr;size_t size=0;unsigned waits=0;};
struct Model {
 bool trace=true,entrySeam=false,serverAvailable=true,knownBoard=true,detailEnabled=true,explicitRequest=true;
 std::string requestId,sessionId;uint64_t now=1000;DWORD last=0;
 IoCase cases[3]{IoCase::Sync,IoCase::Sync,IoCase::Sync};
 std::vector<std::string> actions;std::vector<std::pair<std::string,std::string>> notes;
 std::array<Operation,3> ops{};size_t opCount=0;int active=-1;unsigned closePending=0,ownerEndPending=0;
 const void* sourcePointer=nullptr;const void* ownerPointer=nullptr;size_t ownerSize=0;
 bool writePointerOwned=true;uint64_t emittedChecksum=0;size_t emittedLength=0;
 std::string sent;std::string response="{\"ok\":true,\"status\":\"frame-accepted\"}";
 void action(const char* s){if(trace)actions.emplace_back(s);}
 void action(const std::string& s){if(trace)actions.push_back(s);}
 void note(const char* a,const std::string& b){if(trace)notes.emplace_back(a,b);}
 void ownerEnter(const void* p,size_t n){ownerPointer=p;ownerSize=n;action("request-owner-enter");}
 void ownerExit(){for(size_t k=0;k<opCount;++k){const auto& o=ops[k];if(o.pending&&o.kind==0&&o.buffer==ownerPointer)++ownerEndPending;}action("request-owner-scope-exit-before-destruction");ownerPointer=nullptr;}
 void reset(const Config& c,bool detailed){*this=Model{};trace=detailed;requestId=c.requestId;sessionId=c.sessionId;now=c.now;entrySeam=c.entrySeam;serverAvailable=c.serverPresent;knownBoard=c.knownBoard;detailEnabled=c.detail;explicitRequest=c.explicitRequest;cases[0]=c.writeCase;cases[1]=c.readCase;cases[2]=c.connectCase;}
};
static Model model;
static DWORD GetLastError(){return model.last;}
static DWORD GetCurrentProcessId(){return 1234;}
static ULONGLONG GetTickCount64(){return model.now;}
static int nextKind=0;
static HANDLE CreateEventW(void*,BOOL,BOOL,const wchar_t*){model.action("event-create");if(model.cases[nextKind]==IoCase::EventFailure){model.last=ERROR_NOT_ENOUGH_MEMORY;return nullptr;}return reinterpret_cast<HANDLE>(uintptr_t(0x1001));}
static void FillRead(Operation& op,DWORD* bytes){auto n=(std::min)(op.size,model.response.size());if(op.buffer&&n)std::memcpy(const_cast<void*>(op.buffer),model.response.data(),n);*bytes=static_cast<DWORD>(n);}
static BOOL Start(int kind,void* buffer,DWORD size,DWORD* bytes){
 nextKind=kind;model.action(kind==0?"write":kind==1?"read":"connect");
 Operation op;op.kind=kind;op.buffer=buffer;op.size=size;op.issued=true;
 if(kind==0){model.writePointerOwned&=buffer==model.ownerPointer&&size==model.ownerSize;model.emittedLength=size;const auto* p=static_cast<const unsigned char*>(buffer);uint64_t h=1469598103934665603ULL;if(model.trace){for(DWORD n=0;n<size;++n){h^=p[n];h*=1099511628211ULL;}}else if(size){h^=p[0];h*=1099511628211ULL;h^=p[size-1];}model.emittedChecksum=h;if(model.trace)model.sent.assign(static_cast<const char*>(buffer),size);}
 auto c=model.cases[kind];
 if(c==IoCase::StartFailure){model.last=ERROR_BROKEN_PIPE;return FALSE;}
 if(c==IoCase::ConnectedAlready&&kind==2){model.last=ERROR_PIPE_CONNECTED;return FALSE;}
 op.pending=c!=IoCase::Sync;model.ops[model.opCount++]=op;model.active=static_cast<int>(model.opCount)-1;
 if(!op.pending){if(kind==1)FillRead(model.ops[model.opCount-1],bytes);else *bytes=size;return TRUE;}
 model.last=ERROR_IO_PENDING;return FALSE;
}
static BOOL WriteFile(HANDLE,const void* p,DWORD n,DWORD* b,OVERLAPPED*){return Start(0,const_cast<void*>(p),n,b);}
static BOOL ReadFile(HANDLE,void* p,DWORD n,DWORD* b,OVERLAPPED*){return Start(1,p,n,b);}
static BOOL ConnectNamedPipe(HANDLE,OVERLAPPED*){DWORD b=0;return Start(2,nullptr,0,&b);}
static DWORD WaitForSingleObject(HANDLE,DWORD duration){
 if(model.trace)model.action("wait:"+std::to_string(duration));if(model.active<0)return WAIT_FAILED;auto& o=model.ops[static_cast<size_t>(model.active)];auto c=model.cases[o.kind];++o.waits;
 if(o.waits==1){if(c==IoCase::PendingSuccess||c==IoCase::PendingFailure){o.pending=false;return WAIT_OBJECT_0;}
  if(c==IoCase::FailedPending||c==IoCase::FailedCancelRejected||c==IoCase::FailedSecondWait||c==IoCase::FailedCleanupClearsError){model.last=ERROR_INVALID_HANDLE;return WAIT_FAILED;}return WAIT_TIMEOUT;}
 if(c==IoCase::TimeoutPending||c==IoCase::FailedPending||c==IoCase::FailedCancelRejected)return WAIT_TIMEOUT;
 if(c==IoCase::FailedSecondWait){model.last=ERROR_ACCESS_DENIED;return WAIT_FAILED;}
 o.pending=false;if(c==IoCase::FailedCleanupClearsError)model.last=ERROR_SUCCESS;return WAIT_OBJECT_0;
}
static BOOL CancelIoEx(HANDLE,OVERLAPPED*){model.action("cancel");auto& o=model.ops[static_cast<size_t>(model.active)];auto c=model.cases[o.kind];if(c==IoCase::TimeoutComplete){o.pending=false;model.last=ERROR_NOT_FOUND;return FALSE;}if(c==IoCase::FailedCancelRejected){model.last=ERROR_ACCESS_DENIED;return FALSE;}return TRUE;}
static BOOL GetOverlappedResult(HANDLE,OVERLAPPED*,DWORD* b,BOOL){model.action("terminal-result");auto& o=model.ops[static_cast<size_t>(model.active)];auto c=model.cases[o.kind];if(o.pending){model.last=ERROR_IO_INCOMPLETE;return FALSE;}if(c==IoCase::PendingFailure){model.last=ERROR_OPERATION_ABORTED;return FALSE;}if(o.kind==1)FillRead(o,b);else *b=static_cast<DWORD>(o.size);return TRUE;}
static BOOL CloseHandle(HANDLE){model.action("event-close");if(model.active>=0&&model.ops[static_cast<size_t>(model.active)].pending)++model.closePending;nextKind=nextKind==2?0:nextKind+1;return TRUE;}
static uint64_t proofObjectsConstructed=0,diagnosticStateCalls=0;
struct ResultTrace {
 ResultTrace(){++proofObjectsConstructed;}
 std::string wireAfterExtension,requestId,response,sourceAfter;json session,lifecycle;
};
struct Result {
 Result()=default;Result(bool){early=true;}
 bool early=false,connected=false,written=false,read=false,wirePointerMatchesSource=false;
 DWORD error=0,responseBytes=0;uint64_t sequence=0;
 std::unique_ptr<ResultTrace> trace;
};
#include "source-bound.h"
struct OutcomeTrace {
 OutcomeTrace(){++proofObjectsConstructed;}
 std::vector<std::string> actions;std::vector<std::pair<std::string,std::string>> notes;
 json operations;std::string sent,sourceAfter,exception;json finalSession,responseOutput;
};
struct Outcome {
 Result r;std::unique_ptr<OutcomeTrace> trace;bool exceptionThrown=false;
 uint64_t checksum=0;size_t size=0;
 unsigned pendingAtClose=0,pendingAtOwnerScopeExit=0;bool owned=true;
};
static const ResultTrace& ReadResultTrace(const Result& r){static const ResultTrace empty;return r.trace?*r.trace:empty;}
static uint64_t sink=0;
#define DEFINE_RUN(NS) \
static Outcome Run_##NS(const Config& c,const json& frame,bool trace,bool reset=true){ \
 if(reset){model.reset(c,trace);NS::Reset(c);}else{model.opCount=0;model.active=-1;model.closePending=0;model.ownerEndPending=0;model.writePointerOwned=true;model.emittedLength=0;model.emittedChecksum=0;model.trace=trace;} \
 Outcome out;if(trace)out.trace=std::make_unique<OutcomeTrace>(); \
 std::string wire,id=c.requestId;unsigned long long seq=c.sequence+1;json response=trace?json{{"sentinel",true}}:json{}; \
 auto execute=[&](){ \
  if(c.prepared)wire=c.useCustomWire?c.customWire:NS::Build(frame,id,seq); \
  model.sourcePointer=wire.data(); \
  const std::string* requestId=c.nullId?nullptr:(c.aliasId?&wire:&id); \
  return NS::Execute(c.command.c_str(),frame,"frame-accepted",1000,&response,c.prepared?&wire:nullptr,requestId,c.nullSequence?nullptr:&seq); \
 }; \
 try {if constexpr(NS::HasDiagnostic){const std::string metadata=NS::metadataFor(c);NS::MsiVirtualReportFrameMetadataScope metadataScope(metadata);out.r=execute();}else out.r=execute();} \
 catch(const std::exception& e){out.exceptionThrown=true;if(trace)out.trace->exception=e.what();}catch(...){out.exceptionThrown=true;if(trace)out.trace->exception="nonstandard-exception";} \
 out.checksum=model.emittedChecksum;out.size=model.emittedLength;out.pendingAtClose=model.closePending;out.pendingAtOwnerScopeExit=model.ownerEndPending;out.owned=model.writePointerOwned; \
 if(trace){auto& t=*out.trace;t.actions=model.actions;t.notes=model.notes;t.sent=model.sent;t.sourceAfter=wire;t.finalSession=NS::diagnosticState();t.responseOutput=response;for(size_t k=0;k<model.opCount;++k){const auto& op=model.ops[k];t.operations.push_back({op.kind,op.pending,op.issued,op.size,op.waits});}} \
 sink^=out.checksum+out.size+out.r.sequence+out.r.error;return out; \
}
DEFINE_RUN(plain_baseline) DEFINE_RUN(plain_candidate)
DEFINE_RUN(diag_baseline) DEFINE_RUN(diag_candidate)
DEFINE_RUN(novis_baseline) DEFINE_RUN(novis_candidate)
using RunFn=Outcome(*)(const Config&,const json&,bool,bool);
static RunFn funcs[3][2]={{Run_plain_baseline,Run_plain_candidate},{Run_diag_baseline,Run_diag_candidate},{Run_novis_baseline,Run_novis_candidate}};
static void ResetVariant(int mode,int variant,const Config& c){
 model.reset(c,false);
 if(mode==0){if(variant)plain_candidate::Reset(c);else plain_baseline::Reset(c);}
 else if(mode==1){if(variant)diag_candidate::Reset(c);else diag_baseline::Reset(c);}
 else{if(variant)novis_candidate::Reset(c);else novis_baseline::Reset(c);}
}
static FocusFrameContent Content(unsigned i){
 FocusFrameContent c;if(!i)return c;
 c.buttons=static_cast<int>((i*7919U)&0xffff);c.backButtons=static_cast<int>(i&3);c.screenButtons=i&3;
 c.lxShort=static_cast<short>(i*997U);c.lyShort=static_cast<short>(i*1237U);c.rxShort=static_cast<short>(i*37U);c.ryShort=static_cast<short>(i*17U);
 c.lt=static_cast<float>(i%256)/255.0f;c.rt=1.0f-c.lt;c.ltRaw=c.lt;c.rtRaw=c.rt;c.rawLtByte=static_cast<unsigned char>(i);c.rawRtByte=static_cast<unsigned char>(255-i);
 c.imuAdmitted=(i%3)==0;c.gyroX=-0.0;c.gyroY=123.456;c.gyroZ=-1000.01;c.accelX=0.01;c.accelY=-1;c.accelZ=1.00000001;c.qw=0.999999;c.qx=0.01;c.qy=-0.02;c.qz=1e-12;
 c.touchpads.present=(i%2)==0;c.touchpads.left={true,true,-32768,32767,65535};c.touchpads.right={false,false,17,-99,12};return c;
}
static json Semantic(const Outcome& o){
 const auto& t=*o.trace;const auto& r=ReadResultTrace(o.r);
 return {{"early",o.r.early},{"connected",o.r.connected},{"written",o.r.written},{"read",o.r.read},{"error",o.r.error},{"bytes",o.r.responseBytes},{"sequence",o.r.sequence},{"id",r.requestId},{"response",r.response},{"responseOutput",t.responseOutput},{"wire",r.wireAfterExtension},{"lifecycleEvidence",r.lifecycle},{"session",t.finalSession},{"actions",t.actions},{"notes",t.notes},{"operations",t.operations},{"sent",t.sent},{"checksum",o.checksum},{"size",o.size},{"eventClosedPending",o.pendingAtClose},{"ownerScopeExitedPending",o.pendingAtOwnerScopeExit},{"owned",o.owned},{"exception",t.exception}};
}
static uint64_t checks=0,cases=0;static unsigned pendingNegatives=0;static std::vector<json> examples;
static void Require(bool b,const std::string& message){++checks;if(!b)throw std::runtime_error(message);}
static void Compare(const Config& c,const json& frame,int mode){
 auto a=funcs[mode][0](c,frame,true,true);auto b=funcs[mode][1](c,frame,true,true);++cases;
 Require(Semantic(a)==Semantic(b),"variant mismatch: "+c.name+" mode="+std::to_string(mode));
 Require(a.owned&&b.owned,"issued pointer is not final local owner");
 if(a.r.early)Require(a.trace->sourceAfter==b.trace->sourceAfter,"early guard consumed wire");
 if(!a.trace->exception.empty())Require(a.trace->exception==b.trace->exception,"exception differs");
 if(a.pendingAtOwnerScopeExit){++pendingNegatives;Require(a.pendingAtOwnerScopeExit==b.pendingAtOwnerScopeExit,"existing R32 gap changed");}
 if(!a.r.early&&!c.useCustomWire&&c.prepared&&a.trace->exception.empty()){
  Require(!a.trace->sourceAfter.empty(),"baseline caller unexpectedly consumed");
  Require(ReadResultTrace(a.r).requestId==(c.nullId?std::string{}:c.requestId),"distinct request ID changed");
  Require(a.r.sequence==(c.nullSequence?0:c.sequence+1),"sequence changed");
  if(mode==0)Require(b.r.wirePointerMatchesSource,"long ordinary prepared buffer not transferred");
 }
 if(examples.size()<12&&c.name!="random")examples.push_back({{"name",c.name},{"mode",mode},{"result",Semantic(a)},{"baselineCallerSize",a.trace->sourceAfter.size()},{"candidateCallerSize",b.trace->sourceAfter.size()}});
}
// Explicit suffix seam, not production receipt validation: protected suffix hash binds real code.
// This model checks that equal issued bytes/id/sequence feed equal representative tuple outcomes.
static json ReceiptModel(const Outcome& o,const json& receipt,const Config& c){
 if(!o.r.read)return {{"accepted",false},{"phase","transport"},{"nextSequence",c.sequence}};
 const bool accepted=receipt.is_object()&&receipt.value("ok",false)&&receipt.value("status",std::string{})=="frame-accepted"&&receipt.value("requestId",std::string{})==ReadResultTrace(o.r).requestId&&receipt.value("sequence",0ULL)==o.r.sequence;
 return {{"accepted",accepted},{"phase","representative-tuple-model"},{"nextSequence",accepted?o.r.sequence:c.sequence}};
}
static void Verify(const std::string& directory,const std::string& profile){
 json frame=focusIsolationAssembleFrame(Content(0));Config c;
 for(int mode=0;mode<3;++mode){
  Compare(c,frame,mode);
  for(int guard=0;guard<5;++guard){Config t;t.name="guard";if(guard==0)t.entrySeam=true;if(guard==1)t.validProcess=false;if(guard==2)t.validPipeName=false;if(guard==3)t.runId.clear();if(guard==4)t.nonce.clear();Compare(t,frame,mode);}
  for(int phase=0;phase<4;++phase)for(int gate=0;gate<6;++gate){Config t;t.name="diagnostic-matrix";t.phase=phase;if(gate==1)t.knownBoard=false;if(gate==2)t.detail=false;if(gate==3)t.exitFence=true;if(gate==4)t.powerReady=false;if(gate==5)t.requireFreshSource=false;Compare(t,frame,mode);}
  for(const auto& metadata:std::vector<std::string>{"{", "null","[]","{\"schemaVersion\":1,\"schemaVersion\":2}","{\"diagnosticSessionId\":\"expired\"}","{\"a\":\"\\ud800\"}"}){Config t;t.name="metadata-shape";t.metadataOverride=metadata;Compare(t,frame,mode);}
  {Config t;t.name="expired";t.now=20000;Compare(t,frame,mode);}
  for(int id=0;id<3;++id){Config t;t.name="optional-id-sequence";t.nullId=id!=1;t.nullSequence=id!=0;Compare(t,frame,mode);}
  for(const auto& wire:std::vector<std::string>{"","{}","x",std::string(15,'a'),std::string(16,'b'),std::string(4096,'x'),std::string("a\0b}",4),"{\"msiReportChainSource\":{}}","{\"escaped\":\"\\\"\\\\\\n\\u0000\"}"}){Config t;t.name="raw-wire-shape";t.useCustomWire=true;t.customWire=wire;Compare(t,frame,mode);}
  for(const char* command:{"SUBMIT_FRAME","HELLO","PREPARE_TARGET","RELEASE_TARGET","RECREATE_TARGET","QUIESCE","STATUS","UNRECOGNIZED"}){Config t;t.name="json-command";t.prepared=false;t.command=command;t.phase=1;Compare(t,frame,mode);}
  for(int kind=0;kind<3;++kind)for(int fault=0;fault<12;++fault){Config t;t.name="pipe-action";t.connected=kind!=2;auto f=static_cast<IoCase>(fault);if(kind==0)t.writeCase=f;else if(kind==1)t.readCase=f;else t.connectCase=f;Compare(t,frame,mode);}
  {Config t;t.name="server-unavailable";t.serverPresent=false;Compare(t,frame,mode);}
  for(unsigned n=0;n<256;++n){Config t;t.name="random";auto f=focusIsolationAssembleFrame(Content(n));t.persona=std::array<std::string,6>{"steamdeck","xbox360","dualsense","elite","dualsense-edge","unknown"}[n%6];t.phase=n%4;
   if(n%7==0)t.buildPersona="elite";if(n%11==0)t.boundPersona="dualsense";t.boundPower=n%5==0?0:29;t.sequence=40+n;
   if(n%13==0){t.requestId=std::string("id\0",3)+"\"\\\n"+"\xE4\xB8\xAD\xF0\x9F\x8E\xAE"+std::string(n,'x');t.runId=t.requestId;}
   if(n%17==0)f["extension"]=std::string(1024,'x');Compare(t,f,mode);
  }
  for(const auto& value:std::vector<json>{nullptr,3,"bad",json::array(),json{{"buttons","bad"}},json{{"lt",std::numeric_limits<double>::infinity()}}}){Config t;t.name="builder-shape-fallback";try{(void)plain_baseline::Build(value,t.requestId,41);}catch(...){t.prepared=false;}Compare(t,value,mode);}
 }
 // Outside-contract ID alias: retain exact copy order and expose non-equivalence.
 c.aliasId=true;c.name="OUTSIDE-CONTRACT-id-alias";auto a=Run_plain_baseline(c,frame,true,true);auto b=Run_plain_candidate(c,frame,true,true);
 Require(ReadResultTrace(a.r).requestId!=ReadResultTrace(b.r).requestId,"ID alias counterexample missing");
 json alias={{"idAliasOutsideContract",true},{"baselineIdBytes",ReadResultTrace(a.r).requestId.size()},{"candidateIdBytes",ReadResultTrace(b.r).requestId.size()},{"commandCStringAliasOutsideContract",true},{"commandAliasReason","move consumes string; SSO/moved-from c_str may change; production literals are independent; no dangling-pointer test"}};
 c=Config{};a=Run_plain_baseline(c,frame,true,true);b=Run_plain_candidate(c,frame,true,true);
 for(int mutation=0;mutation<5;++mutation){json receipt={{"ok",true},{"status","frame-accepted"},{"requestId",c.requestId},{"sequence",41}};if(mutation==1)receipt["ok"]=false;if(mutation==2)receipt["status"]="other";if(mutation==3)receipt["requestId"]="other";if(mutation==4)receipt["sequence"]=42;Require(ReceiptModel(a,receipt,c)==ReceiptModel(b,receipt,c),"tuple model differs");}
 Require(pendingNegatives>0,"R32 negative controls absent");
 // Measurement instrumentation self-check only; no timing or new production behavior model.
 for(int mode=0;mode<3;++mode)for(int variant=0;variant<2;++variant){const auto objects=proofObjectsConstructed,queries=diagnosticStateCalls;auto lean=funcs[mode][variant](c,frame,false,true);Require(!lean.trace&&!lean.r.trace&&!lean.exceptionThrown,"lean call created proof storage or threw");Require(proofObjectsConstructed==objects&&diagnosticStateCalls==queries,"lean call constructed semantic snapshots");}

 json result={{"status","PASS"},{"profile",profile},{"cases",cases},{"checks",checks},{"syntheticOnly",true},{"performanceExecuted",false},{"existingR32PendingNegativeCases",pendingNegatives},{"alias",alias},{"executed","actual builder/cache/assembler/envelope/identity helpers/Session/diagnostic metadata-scope/AppendOptional/ExtendRequest/contiguous preparation/current R37 helper bodies"},{"seamed","entry observation/adoption RAII static; Win32 actions deterministic standins; representative receipt tuple model only; full suffix protected exact source"},{"ownerTrace","owner scope marker precedes actual requestText destructor; not kernel completion or exact allocator free; diagnostic swap may release original moved capacity before issue"},{"oomParityClaim",false},{"leanProofStorageSelfCheck",true},{"examples",examples}};
 std::ofstream(directory+"/"+profile+"-correctness.json")<<result.dump(2)<<'\n';std::cout<<profile<<" PASS "<<cases<<" cases "<<checks<<" checks\n";
}
struct Scenario {std::string name;Config config;int mode=0;bool affected=true;std::array<FocusFrameContent,16> content;std::array<json,16> frames;};
static std::vector<Scenario> Scenarios(){
 std::vector<Scenario> all;
 for(const char* n:{"prepared-neutral","prepared-moving","prepared-imu-touch","prepared-unicode-long","prepared-diagnostic-active","json-legacy","json-prepare-diagnostic","prepared-diagnostic-closed","prepared-empty-sso","guard-rejected"}){
  Scenario s;s.name=n;s.config.name=n;s.affected=all.size()<5;
  if(s.name=="prepared-diagnostic-active")s.mode=1;
  if(s.name=="json-legacy")s.config.prepared=false;
  if(s.name=="json-prepare-diagnostic"){s.mode=1;s.config.prepared=false;s.config.command="PREPARE_TARGET";s.config.phase=1;}
  if(s.name=="prepared-diagnostic-closed"){s.mode=1;s.config.phase=0;}
  if(s.name=="prepared-empty-sso"){s.config.useCustomWire=true;s.config.customWire="{}";}
  if(s.name=="guard-rejected")s.config.validProcess=false;
  if(s.name=="prepared-unicode-long"){s.config.requestId="id\"\\\n\xE4\xB8\xAD\xF0\x9F\x8E\xAE"+std::string(1024,'x');s.config.runId=std::string("run\0",4)+s.config.requestId;}
  for(unsigned i=0;i<16;++i){s.content[i]=Content(s.name=="prepared-neutral"?0:s.name=="prepared-imu-touch"?(i+1)*6:i+1);s.frames[i]=focusIsolationAssembleFrame(s.content[i]);}
  all.push_back(std::move(s));
 }
 return all;
}
struct Sample {double nanoseconds=0;Counts alloc;};
static Sample Measure(const Scenario& s,int variant,int scope,unsigned batches,bool count){
 ResetVariant(s.mode,variant,s.config);auto fn=funcs[s.mode][variant];
 allocationCounts={};allocationCounting=count;const auto start=std::chrono::steady_clock::now();
 for(unsigned b=0;b<batches;++b)for(size_t i=0;i<16;++i){
  // Session transitions/context set once above, outside measured steady frames.
  // PREPARE control uses the actual repeated OfferPrepare path, not per-frame reinitialization.
  if(scope==1){json frame=focusIsolationAssembleFrame(s.content[i]);auto out=fn(s.config,frame,false,false);if(out.exceptionThrown)throw std::runtime_error("unexpected timed-path exception");sink+=out.size;}
  else{auto out=fn(s.config,s.frames[i],false,false);if(out.exceptionThrown)throw std::runtime_error("unexpected timed-path exception");sink+=out.size;}
 }
 const auto end=std::chrono::steady_clock::now();allocationCounting=false;
 return {std::chrono::duration<double,std::nano>(end-start).count()/(batches*16),allocationCounts};
}
static json Allocation(const Sample& s,unsigned batches){const double n=batches*16.0;return {{"requests",s.alloc.requests/n},{"bytes",s.alloc.bytes/n},{"alignedRequests",s.alloc.alignedRequests/n},{"alignedBytes",s.alloc.alignedBytes/n}};}
static double Median(std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];}
static void Benchmark(const std::string& directory,const std::string& profile,int campaign){
 std::ifstream permission(directory+"/performance-permission.json");if(!permission)throw std::runtime_error("Performance HELD: parent permission receipt absent");
 auto all=Scenarios();json rows=json::array();bool pass=true;
 for(const auto& s:all)for(int scope=0;scope<2;++scope){
  (void)Measure(s,0,scope,12,false);(void)Measure(s,1,scope,12,false);
  auto aa=Measure(s,0,scope,8,true),ab=Measure(s,1,scope,8,true);
  json pairs=json::array();std::vector<double> reductions,baselineSamples,candidateSamples;unsigned wins=0;
  for(int pair=0;pair<11;++pair){Sample a,b;bool candidateFirst=((pair+campaign+scope)%2)!=0;
   if(candidateFirst){b=Measure(s,1,scope,64,false);a=Measure(s,0,scope,64,false);}else{a=Measure(s,0,scope,64,false);b=Measure(s,1,scope,64,false);}
   baselineSamples.push_back(a.nanoseconds);candidateSamples.push_back(b.nanoseconds);const double reduction=1-b.nanoseconds/a.nanoseconds;reductions.push_back(reduction);wins+=b.nanoseconds<a.nanoseconds;
   pairs.push_back({{"pair",pair+1},{"candidateFirst",candidateFirst},{"baselineNsPerOp",a.nanoseconds},{"candidateNsPerOp",b.nanoseconds},{"pairedReduction",reduction}});
  }
  auto ba=Allocation(aa,8),ca=Allocation(ab,8);bool alloc=true;for(const char* key:{"requests","bytes","alignedRequests","alignedBytes"})alloc&=ca[key].get<double>()<=ba[key].get<double>();
  const double baselineMedian=Median(baselineSamples),candidateMedian=Median(candidateSamples);
  const double paired=Median(reductions),ratio=1-candidateMedian/baselineMedian;
  const bool cpu=s.affected?(wins>=10&&paired>=.05&&ratio>=.05):(paired>=-.05&&ratio>=-.05);
  pass&=alloc&&cpu;rows.push_back({{"workload",s.name},{"scope",scope},{"affected",s.affected},{"fasterPairs",wins},{"medianPairedReduction",paired},{"ratioOfMediansReduction",ratio},{"baselineMedianNs",baselineMedian},{"candidateMedianNs",candidateMedian},{"baselineAlloc",ba},{"candidateAlloc",ca},{"allocationGate",alloc},{"cpuGate",cpu},{"pass",alloc&&cpu},{"pairs",pairs}});
 }
 json result={{"profile",profile},{"campaign",campaign},{"status",pass?"PASS":"REJECTED"},{"rows",rows},{"sink",sink},{"syntheticOnly",true}};
 std::ofstream(directory+"/"+profile+"-campaign-"+std::to_string(campaign)+".json")<<result.dump(2)<<'\n';std::cout<<profile<<" campaign "<<campaign<<" "<<(pass?"PASS":"REJECTED")<<" rows="<<rows.size()<<'\n';
}
int main(int argc,char** argv){
 try{if(argc<4)throw std::runtime_error("usage: fixture verify|benchmark evidence-directory profile [campaign]");
  if(std::string(argv[1])=="verify")Verify(argv[2],argv[3]);
  else if(std::string(argv[1])=="benchmark"&&argc==5)Benchmark(argv[2],argv[3],std::stoi(argv[4]));
  else throw std::runtime_error("invalid arguments");return 0;
 }catch(const std::exception& e){allocationCounting=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
