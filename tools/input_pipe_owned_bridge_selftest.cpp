// R42 owned bridge tests. Legacy helpers run ONLY against deterministic stubs.
// Real named pipes have unique names and same-process peers, never product pipes.
#include "input_pipe_owned_bridge.h"
#include "json.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using json=nlohmann::json;
namespace own=ymcc::testing::pipeOwnership;
namespace win=ymcc::testing::pipeWin32;
namespace bridge=ymcc::testing::pipeBridge;
static unsigned checks=0,pipeOrdinal=0;
static json cases=json::array();
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static json asJson(const bridge::Result& r){return {{"ok",r.ok},{"error",r.error},{"transferred",r.transferred},{"byteCountKnown",r.byteCountKnown},{"admitted",r.admitted},{"terminalAtReturn",r.terminalAtReturn},{"retired",r.retired},{"operationId",r.operationId},{"ticket",r.ticket}};}
struct Observation {own::Poll state=own::Poll::Pending;DWORD error=ERROR_IO_INCOMPLETE,bytes=0;};
struct Script {
    bool immediate=true;DWORD initialError=ERROR_SUCCESS,immediateBytes=32;
    DWORD firstWait=WAIT_OBJECT_0,firstWaitError=ERROR_INVALID_HANDLE;
    BOOL firstQueryOk=TRUE;DWORD firstQueryError=ERROR_SUCCESS,firstQueryBytes=32;
    std::vector<Observation> observations;
    unsigned waits=0,queries=0,cancels=0,issued=0,created=0,destroyed=0;
    bool throwCreate=false,throwBadAlloc=false;DWORD eventError=ERROR_SUCCESS;unsigned char content=0x5A;
};
namespace legacy {
static Script* script=nullptr;
static DWORD last=0;
static HANDLE stubCreateEventW(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR){last=script->eventError;return script->eventError?nullptr:reinterpret_cast<HANDLE>(1);}
static DWORD stubGetLastError(){return last;}
static BOOL api(void* data,DWORD size,DWORD* n){
    ++script->issued;last=script->initialError;
    if(n)*n=script->immediateBytes;
    if(data&&script->initialError!=ERROR_IO_PENDING&&script->immediateBytes)
        std::memset(data,script->content,std::min(size,script->immediateBytes));
    return script->immediate?TRUE:FALSE;
}
static BOOL stubReadFile(HANDLE,void* data,DWORD size,DWORD* n,OVERLAPPED*){return api(data,size,n);}
static BOOL stubWriteFile(HANDLE,const void*,DWORD size,DWORD* n,OVERLAPPED*){return api(nullptr,size,n);}
static BOOL stubConnectNamedPipe(HANDLE,OVERLAPPED*){return api(nullptr,0,nullptr);}
static DWORD stubWaitForSingleObject(HANDLE,DWORD){++script->waits;last=script->waits==1?script->firstWaitError:ERROR_INVALID_PARAMETER;return script->waits==1?script->firstWait:WAIT_OBJECT_0;}
static BOOL stubCancelIoEx(HANDLE,OVERLAPPED*){++script->cancels;last=ERROR_ACCESS_DENIED;return FALSE;}
static BOOL stubGetOverlappedResult(HANDLE,OVERLAPPED*,DWORD* n,BOOL){++script->queries;*n=script->firstQueryBytes;last=script->firstQueryError;return script->firstQueryOk;}
static BOOL stubCloseHandle(HANDLE){last=ERROR_INVALID_FUNCTION;return TRUE;}
#define CreateEventW stubCreateEventW
#define GetLastError stubGetLastError
#define ReadFile stubReadFile
#define WriteFile stubWriteFile
#define ConnectNamedPipe stubConnectNamedPipe
#define WaitForSingleObject stubWaitForSingleObject
#define CancelIoEx stubCancelIoEx
#define GetOverlappedResult stubGetOverlappedResult
#define CloseHandle stubCloseHandle
#include "legacy_helpers.inc"
#undef CreateEventW
#undef GetLastError
#undef ReadFile
#undef WriteFile
#undef ConnectNamedPipe
#undef WaitForSingleObject
#undef CancelIoEx
#undef GetOverlappedResult
#undef CloseHandle
} // namespace legacy
class FakeBackend {
    struct Record {own::Kind kind;const own::OverlappedStorage* identity;unsigned char* data;std::size_t size;win::Summary summary;std::size_t next=0;unsigned waits=0;};
    std::shared_ptr<Script> script_;
    std::map<std::uint64_t,Record> records_;
    std::map<std::uint64_t,win::Summary> history_;
    std::uint64_t next_=0;
    void completed(Record& r,DWORD error,DWORD n)noexcept{
        r.summary.terminal=true;r.summary.completionError=error;
        const auto bytes=win::classifyBytes(r.kind,error==ERROR_SUCCESS,error,n,static_cast<DWORD>(r.size));
        r.summary.byteCountApplicable=bytes.applicable;r.summary.byteCountKnown=bytes.known;r.summary.bytes=bytes.count;
        if(r.kind==own::Kind::Read&&bytes.known&&bytes.count)std::memset(r.data,script_->content,bytes.count);
    }
public:
    explicit FakeBackend(std::shared_ptr<Script> script):script_(std::move(script)){}
    std::uint64_t create(own::Kind kind,const own::OverlappedStorage* identity,unsigned char* data,std::size_t size){
        if(script_->eventError)throw win::ApiError("CreateEventW",script_->eventError);
        if(script_->throwBadAlloc)throw std::bad_alloc();
        if(script_->throwCreate)throw std::runtime_error("deliberate pre-issue create failure");
        Record r{kind,identity,data,size,{},0};r.summary.byteCountApplicable=kind!=own::Kind::Connect;
        records_.emplace(++next_,std::move(r));++script_->created;return next_;
    }
    own::Issue issue(std::uint64_t id)noexcept{
        auto& r=records_.at(id);r.summary.issued=true;r.summary.initialError=script_->initialError;++script_->issued;
        if(!script_->immediate&&script_->initialError==ERROR_IO_PENDING){r.summary.pendingAtIssue=true;return own::Issue::Pending;}
        const DWORD error=script_->immediate||(r.kind==own::Kind::Connect&&script_->initialError==ERROR_PIPE_CONNECTED)?ERROR_SUCCESS:script_->initialError;
        completed(r,error,script_->immediateBytes);return error==ERROR_SUCCESS?own::Issue::Completed:own::Issue::Rejected;
    }
    void cancel(std::uint64_t id)noexcept{auto& r=records_.at(id);++r.summary.cancels;++script_->cancels;SetLastError(ERROR_ACCESS_DENIED);}
    DWORD waitEvent(std::uint64_t id,DWORD)noexcept{auto& r=records_.at(id);++r.waits;++script_->waits;SetLastError(r.waits==1?script_->firstWaitError:ERROR_INVALID_PARAMETER);return r.waits==1?script_->firstWait:WAIT_OBJECT_0;}
    own::Poll poll(std::uint64_t id)noexcept{
        auto& r=records_.at(id);if(r.summary.terminal)return own::Poll::Terminal;
        ++r.summary.polls;Observation o;
        if(!script_->observations.empty()){
            o=script_->observations[std::min(r.next,script_->observations.size()-1)];
            if(r.next<script_->observations.size())++r.next;
        }
        r.summary.lastPollError=o.error;
        if(o.state==own::Poll::Terminal)completed(r,o.error,o.bytes);
        return o.state;
    }
    win::Summary summary(std::uint64_t id)const{const auto it=records_.find(id);return it==records_.end()?history_.at(id):it->second.summary;}
    void destroy(std::uint64_t id,const own::OverlappedStorage* identity,const unsigned char* data,std::size_t size)noexcept{
        const auto it=records_.find(id);if(it==records_.end())std::terminate();
        auto& r=it->second;if((r.summary.issued&&!r.summary.terminal)||r.identity!=identity||r.data!=data||r.size!=size)std::terminate();
        r.summary.destroyed=true;history_[id]=r.summary;records_.erase(it);++script_->destroyed;
    }
};
static Script pending(DWORD wait=WAIT_OBJECT_0){Script s;s.immediate=false;s.initialError=ERROR_IO_PENDING;s.immediateBytes=0;s.firstWait=wait;return s;}
static void parityCase(const char* name,own::Kind kind,Script script){
    Script old=script;legacy::script=&old;std::vector<unsigned char> baseline(64,0xA7),caller(64,0xA7);DWORD oldBytes=99,oldError=99;
    const bool oldOk=kind==own::Kind::Connect?legacy::inputHostWaitClientLocked(nullptr,0,&oldError):
        legacy::inputHostPipeIo(nullptr,kind==own::Kind::Write,baseline.data(),64,0,&oldBytes,&oldError);
    if(kind==own::Kind::Connect)oldBytes=0;
    auto state=std::make_shared<Script>(script);auto backend=std::make_shared<FakeBackend>(state);
    auto budget=std::make_shared<own::Budget>(4,256);auto hub=std::make_shared<bridge::RetirementHub<FakeBackend>>();
    bridge::Result result;
    {bridge::Endpoint<FakeBackend> endpoint(backend,budget,hub);result=endpoint.run(kind,kind==own::Kind::Connect?nullptr:caller.data(),kind==own::Kind::Connect?0:64,0,0);}
    check(result.ok==oldOk&&result.error==oldError&&result.transferred==oldBytes,"legacy result parity mismatch");
    if(result.error==WAIT_TIMEOUT||script.firstWait==WAIT_FAILED)check(caller==std::vector<unsigned char>(64,0xA7),"late completion copied into expired request buffer");
    const auto frozen=asJson(result);
    if(result.retired){state->observations.push_back({own::Poll::Terminal,ERROR_OPERATION_ABORTED,0});(void)hub->sweep();}
    check(asJson(result)==frozen,"maintenance rewrote a returned result");
    check(budget->count==0&&budget->bytes==0&&hub->tracked()==0,"parity case retained terminal resources");
    check(state->created==state->destroyed,"fake operations leaked");
    cases.push_back({{"case",name},{"kind",static_cast<int>(kind)},{"deterministicLegacyComparison",true},{"result",frozen},{"legacyCancels",old.cancels},{"ownedCancels",state->cancels}});
}
static void parityMatrix(){
    for(const auto kind:{own::Kind::Read,own::Kind::Write,own::Kind::Connect}){
        Script eventFail;eventFail.eventError=ERROR_NOT_ENOUGH_MEMORY;eventFail.immediateBytes=0;parityCase("create-event-error-preserved",kind,eventFail);
        Script immediate;if(kind==own::Kind::Connect)immediate.immediateBytes=0;parityCase("immediate-success",kind,immediate);
        Script denied;denied.immediate=false;denied.initialError=ERROR_ACCESS_DENIED;denied.immediateBytes=0;parityCase("immediate-error",kind,denied);
        auto success=pending();success.observations={{own::Poll::Terminal,ERROR_SUCCESS,32}};parityCase("pending-first-wait-success",kind,success);
        auto aborted=pending();aborted.firstQueryOk=FALSE;aborted.firstQueryError=ERROR_OPERATION_ABORTED;aborted.firstQueryBytes=0;aborted.observations={{own::Poll::Terminal,ERROR_OPERATION_ABORTED,0}};parityCase("signaled-aborted",kind,aborted);
        auto timeout=pending(WAIT_TIMEOUT);timeout.observations={{own::Poll::Terminal,ERROR_SUCCESS,32}};parityCase("timeout-late-success-stays-timeout",kind,timeout);
        auto retire=pending(WAIT_TIMEOUT);retire.observations={{own::Poll::Pending,ERROR_IO_INCOMPLETE,0}};parityCase("timeout-still-pending",kind,retire);
        auto failed=pending(WAIT_FAILED);failed.observations={{own::Poll::Terminal,ERROR_OPERATION_ABORTED,0}};parityCase("first-wait-error-survives-cleanup",kind,failed);
        auto incomplete=pending();incomplete.firstQueryOk=FALSE;incomplete.firstQueryError=ERROR_IO_INCOMPLETE;incomplete.firstQueryBytes=0;incomplete.observations={{own::Poll::Pending,ERROR_IO_INCOMPLETE,0}};parityCase("signaled-query-incomplete-retained",kind,incomplete);
        Script noData;noData.immediate=false;noData.initialError=ERROR_NO_DATA;noData.immediateBytes=0;parityCase("immediate-no-data-232",kind,noData);
        auto broken=pending();broken.firstQueryOk=FALSE;broken.firstQueryError=ERROR_BROKEN_PIPE;broken.firstQueryBytes=0;broken.observations={{own::Poll::Terminal,ERROR_BROKEN_PIPE,0}};parityCase("signaled-peer-broken",kind,broken);
        auto unknown=pending();unknown.firstQueryOk=FALSE;unknown.firstQueryError=ERROR_GEN_FAILURE;unknown.firstQueryBytes=0;unknown.observations={{own::Poll::Unavailable,ERROR_GEN_FAILURE,0}};parityCase("unknown-query-retained",kind,unknown);
    }
    Script connected;connected.immediate=false;connected.initialError=ERROR_PIPE_CONNECTED;connected.immediateBytes=0;parityCase("already-connected-normalized",own::Kind::Connect,connected);
    Script partial;partial.immediate=false;partial.initialError=ERROR_MORE_DATA;partial.immediateBytes=32;parityCase("immediate-partial-message",own::Kind::Read,partial);
    auto partialPending=pending();partialPending.firstQueryOk=FALSE;partialPending.firstQueryError=ERROR_MORE_DATA;partialPending.firstQueryBytes=32;partialPending.observations={{own::Poll::Terminal,ERROR_MORE_DATA,32}};parityCase("pending-partial-message",own::Kind::Read,partialPending);
}
static void retentionAndCaps(){
    auto budget=std::make_shared<own::Budget>(2,128);auto hub=std::make_shared<bridge::RetirementHub<FakeBackend,2>>();
    auto s=std::make_shared<Script>(pending(WAIT_TIMEOUT));s->observations={{own::Poll::Unavailable,ERROR_INVALID_HANDLE,0}};
    std::weak_ptr<FakeBackend> weak;std::vector<unsigned char> caller(64,0xB8);bridge::Result r1,r2;
    {auto backend=std::make_shared<FakeBackend>(s);weak=backend;bridge::Endpoint<FakeBackend,2> endpoint(backend,budget,hub);r1=endpoint.run(own::Kind::Read,caller.data(),64,0,0);r2=endpoint.run(own::Kind::Read,caller.data(),64,0,0);}
    check(!weak.expired()&&budget->count==2&&budget->bytes==128&&hub->tracked()==2,"last caller lost unresolved ownership");
    check(r1.retired&&r2.retired&&r2.ticket>r1.ticket&&r1.error==WAIT_TIMEOUT&&r2.error==WAIT_TIMEOUT,"retirement tickets or per-operation first waits invalid");
    auto fresh=std::make_shared<Script>();auto freshBackend=std::make_shared<FakeBackend>(fresh);
    {bridge::Endpoint<FakeBackend,2> next(freshBackend,budget,hub);auto full=next.run(own::Kind::Read,caller.data(),64,0,0);check(!full.admitted&&full.error==ERROR_NOT_ENOUGH_MEMORY&&fresh->issued==0,"cross-generation budget failed to reject before issue");}
    for(unsigned i=0;i<1000;++i){auto sweep=hub->sweep();check(sweep.unavailable==2&&budget->count==2&&s->destroyed==0,"unknown query or elapsed sweeps released unresolved state");}
    s->observations.push_back({own::Poll::Terminal,ERROR_SUCCESS,64});const auto drained=hub->sweep();
    check(drained.terminal==2&&budget->count==0&&hub->tracked()==0&&weak.expired(),"maintenance failed to reap old backend");
    check(caller==std::vector<unsigned char>(64,0xB8),"late old-generation read touched caller");
    {bridge::Endpoint<FakeBackend,2> next(freshBackend,budget,hub);auto good=next.run(own::Kind::Read,caller.data(),64,0,0);check(good.ok&&good.ticket>r2.ticket,"recovered admission or monotonic ticket failed");}
    auto small=std::make_shared<own::Budget>(4,8);auto smallHub=std::make_shared<bridge::RetirementHub<FakeBackend>>();
    {bridge::Endpoint<FakeBackend> endpoint(freshBackend,small,smallHub);const auto count=fresh->issued;auto full=endpoint.run(own::Kind::Read,caller.data(),64,0,0);check(!full.admitted&&fresh->issued==count,"logical byte cap allowed issue");}
    // Separately saturate the retirement table while count/byte budget has room.
    auto wide=std::make_shared<own::Budget>(8,1024);auto oneHub=std::make_shared<bridge::RetirementHub<FakeBackend,1>>();
    auto held=std::make_shared<Script>(pending(WAIT_TIMEOUT));held->observations={{own::Poll::Pending,ERROR_IO_INCOMPLETE,0}};auto heldBackend=std::make_shared<FakeBackend>(held);
    {bridge::Endpoint<FakeBackend,1> a(heldBackend,wide,oneHub);auto first=a.run(own::Kind::Read,caller.data(),64,0,0);check(first.retired,"one-slot setup did not retire");
     bridge::Endpoint<FakeBackend,1> b(freshBackend,wide,oneHub);const auto issued=fresh->issued;auto rejected=b.run(own::Kind::Read,caller.data(),64,0,0);check(!rejected.admitted&&fresh->issued==issued&&wide->count==1,"retirement admission happened after issue");}
    held->observations.push_back({own::Poll::Terminal,ERROR_OPERATION_ABORTED,0});(void)oneHub->sweep();check(wide->count==0,"one-slot retained after completion");
    cases.push_back({{"case","retirement-across-endpoint-lifetimes-and-epochs"},{"actualWin32",false},{"unknownRetentionSweeps",1000},{"countByteAndRetirementCapsVerified",true},{"lateResultAndCallerBufferImmutable",true}});
}
static void wrappersAndPreissueFailure(){
    auto state=std::make_shared<Script>();auto backend=std::make_shared<FakeBackend>(state);auto budget=std::make_shared<own::Budget>(4,256);auto hub=std::make_shared<bridge::RetirementHub<FakeBackend>>();bridge::Endpoint<FakeBackend> endpoint(backend,budget,hub);std::vector<unsigned char> data(64);
    check(endpoint.io(false,data.data(),64,0,nullptr,nullptr),"null optional I/O outputs rejected");check(endpoint.connect(0,nullptr),"null optional connect output rejected");
    DWORD n=777,error=777;check(endpoint.io(false,data.data(),64,0,&n,&error)&&n==32&&error==0,"wrapper outputs not populated");
    const auto issued=state->issued;auto invalid=endpoint.run(own::Kind::Read,nullptr,64,0,0);check(invalid.error==ERROR_INVALID_PARAMETER&&!invalid.admitted&&state->issued==issued,"invalid buffer was issued");
    state->eventError=ERROR_ACCESS_DENIED;n=777;error=777;check(!endpoint.io(false,data.data(),64,0,&n,&error)&&n==0&&error==ERROR_ACCESS_DENIED&&budget->count==0,"typed pre-issue OS error was lost");state->eventError=ERROR_SUCCESS;
    state->throwBadAlloc=true;n=777;error=777;check(!endpoint.io(false,data.data(),64,0,&n,&error)&&n==0&&error==ERROR_NOT_ENOUGH_MEMORY&&budget->count==0,"allocation failure was not bounded before issue");state->throwBadAlloc=false;
    state->throwCreate=true;n=777;error=777;bool caught=false;try{(void)endpoint.io(false,data.data(),64,0,&n,&error);}catch(const std::runtime_error&){caught=true;}
    check(caught&&n==0&&error==0&&budget->count==0&&hub->tracked()==0&&state->issued==issued,"pre-issue exception lost budget/output initialization");
    cases.push_back({{"case","optional-outputs-and-preissue-throw"},{"actualWin32",false},{"preissueExceptionsStillPropagate",true},{"typedWin32CreationErrorAndBadAllocMapped",true}});
}
static auto handles=std::make_shared<win::HandleStats>();
static auto realBudget=std::make_shared<own::Budget>(4,4*1024*1024);
using RealHub=bridge::RetirementHub<win::Backend>;
using RealEndpoint=bridge::Endpoint<win::Backend>;
struct Pair {
    std::wstring name;win::Handle server,client;
    Pair(){name=L"\\\\.\\pipe\\YMCC-R42-bridge-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++pipeOrdinal);
        server=win::Handle(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr));check(server.valid(),"private server creation failed");}
    void open(){client=win::Handle(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));check(client.valid(),"private client open failed");DWORD mode=PIPE_READMODE_MESSAGE;check(SetNamedPipeHandleState(client.get(),&mode,nullptr,nullptr)!=FALSE,"private message mode failed");ULONG pid=0;check(GetNamedPipeClientProcessId(server.get(),&pid)!=FALSE&&pid==GetCurrentProcessId(),"private client PID mismatch");pid=0;check(GetNamedPipeServerProcessId(client.get(),&pid)!=FALSE&&pid==GetCurrentProcessId(),"private server PID mismatch");}
    void connect(){open();auto backend=std::make_shared<win::Backend>(server.get(),handles);auto hub=std::make_shared<RealHub>();RealEndpoint endpoint(backend,realBudget,hub);const auto result=endpoint.run(own::Kind::Connect,nullptr,0,2000);check(result.ok&&backend->summary(result.operationId).initialError==ERROR_PIPE_CONNECTED,"real connect normalization failed");}
};
template<class Hub> static void drain(const std::shared_ptr<Hub>& hub){const auto deadline=GetTickCount64()+2000;while(hub->tracked()&&GetTickCount64()<deadline){(void)hub->sweep();if(hub->tracked())Sleep(1);}check(hub->tracked()==0,"private retired operation did not drain");}
static bridge::Result realIo(HANDLE pipe,own::Kind kind,std::vector<unsigned char>& data){auto backend=std::make_shared<win::Backend>(pipe,handles);auto hub=std::make_shared<RealHub>();RealEndpoint endpoint(backend,realBudget,hub);auto result=endpoint.run(kind,data.data(),static_cast<DWORD>(data.size()),2000);drain(hub);return result;}
static void realNormalAndPartial(){
    Pair pair;pair.connect();std::vector<unsigned char> full(128,0x6C),first(64,0xA7),tail(64,0xA7);
    check(realIo(pair.client.get(),own::Kind::Write,full).ok,"real buffered write failed");auto partial=realIo(pair.server.get(),own::Kind::Read,first);check(!partial.ok&&partial.error==ERROR_MORE_DATA&&partial.byteCountKnown&&partial.transferred==64&&first==std::vector<unsigned char>(64,0x6C),"real partial read semantics failed");auto remaining=realIo(pair.server.get(),own::Kind::Read,tail);check(remaining.ok&&remaining.transferred==64&&tail==first,"real partial tail failed");
    cases.push_back({{"case","real-buffered-write-partial-read-tail"},{"actualWin32",true},{"partial",asJson(partial)},{"tail",asJson(remaining)}});
}
static void realTimeouts(){
    {Pair pair;auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);auto hub=std::make_shared<RealHub>();RealEndpoint endpoint(backend,realBudget,hub);auto result=endpoint.run(own::Kind::Connect,nullptr,0,0);check(!result.ok&&result.error==WAIT_TIMEOUT,"real connect timeout became completion result");drain(hub);cases.push_back({{"case","real-connect-timeout-cancel"},{"actualWin32",true},{"result",asJson(result)},{"completionError",backend->summary(result.operationId).completionError}});}
    {Pair pair;pair.connect();auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);auto hub=std::make_shared<RealHub>();RealEndpoint endpoint(backend,realBudget,hub);std::vector<unsigned char> data(64,0xA7);auto result=endpoint.run(own::Kind::Read,data.data(),64,0);check(!result.ok&&result.error==WAIT_TIMEOUT&&data==std::vector<unsigned char>(64,0xA7),"real read timeout changed result or caller");drain(hub);check(backend->summary(result.operationId).completionError==ERROR_OPERATION_ABORTED,"real canceled read lacked aborted completion");cases.push_back({{"case","real-read-timeout-cancel"},{"actualWin32",true},{"result",asJson(result)}});}
    {Pair pair;pair.connect();pair.client.reset();std::vector<unsigned char> data(64,0xA7);auto result=realIo(pair.server.get(),own::Kind::Read,data);check(!result.ok&&result.error==ERROR_BROKEN_PIPE,"real peer close status failed");cases.push_back({{"case","real-peer-close"},{"actualWin32",true},{"result",asJson(result)}});}
}
// The following two tests deliberately suppress cancellation in the adapter.
// I/O, buffers, event, duplicate handle, and result queries are real Win32.
// This exercises a reachable retirement bridge without claiming a kernel fault
// or a genuinely failed/stuck CancelIoEx was induced.
class SuppressedCancelBackend:public win::Backend {
public:
    std::shared_ptr<unsigned> suppressed;
    SuppressedCancelBackend(HANDLE h,std::shared_ptr<unsigned> n):win::Backend(h,handles),suppressed(std::move(n)){}
    void cancel(std::uint64_t)noexcept{++*suppressed;}
};
static void realRetirement(bool write){
    Pair pair;pair.connect();using B=SuppressedCancelBackend;auto hub=std::make_shared<bridge::RetirementHub<B>>();auto suppressed=std::make_shared<unsigned>(0);std::weak_ptr<B> weak;const std::size_t size=write?1024*1024:64;
    std::vector<unsigned char> caller(size,write?0x39:0xA7);bridge::Result result;
    {auto backend=std::make_shared<B>(pair.server.get(),suppressed);weak=backend;bridge::Endpoint<B> endpoint(backend,realBudget,hub);result=endpoint.run(write?own::Kind::Write:own::Kind::Read,caller.data(),static_cast<DWORD>(caller.size()),0,0);}
    check(result.error==WAIT_TIMEOUT&&result.retired&&*suppressed==1&&!weak.expired(),"real deliberate deferred cancellation failed to retain");
    const auto frozen=asJson(result);pair.server.reset();
    for(unsigned i=0;i<8;++i){auto sweep=hub->sweep();check(sweep.pending==1&&realBudget->count==1,"real pending operation retired without completion");}
    std::vector<unsigned char> peer(size,write?0:0x5B);
    if(write)std::fill(caller.begin(),caller.end(),static_cast<unsigned char>(0xA7));
    const auto peerResult=realIo(pair.client.get(),write?own::Kind::Read:own::Kind::Write,peer);check(peerResult.ok&&peerResult.transferred==size,"real private retirement peer I/O failed");drain(hub);
    check(weak.expired()&&realBudget->count==0&&realBudget->bytes==0,"real retired backend or payload leaked");
    check(asJson(result)==frozen&&caller==std::vector<unsigned char>(size,0xA7),"late real completion changed returned result/caller buffer");
    if(write)check(peer==std::vector<unsigned char>(size,0x39),"pending write depended on modified caller source");
    cases.push_back({{"case",write?"real-owned-write-after-last-caller-release":"real-owned-read-after-last-caller-release"},{"actualWin32",true},{"cancellationDeliberatelySuppressed",true},{"realKernelCancellationFailureInduced",false},{"result",frozen},{"payloadBytesVerified",size},{"completionReapedByHub",true}});
}
int main(){try{DWORD before=0,after=0;check(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial handle count failed");parityMatrix();retentionAndCaps();wrappersAndPreissueFailure();realNormalAndPartial();realTimeouts();realRetirement(false);realRetirement(true);check(realBudget->count==0&&realBudget->bytes==0&&handles->live==0&&handles->closeFailures==0,"real accounting not empty");check(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"process handles leaked");std::cout<<json({{"pass",true},{"checks",checks},{"cases",cases},{"privatePipePairs",pipeOrdinal},{"processHandlesBefore",before},{"processHandlesAfter",after},{"adapterHandlesCreated",handles->created},{"adapterHandlesClosed",handles->closed},{"adapterCloseFailures",handles->closeFailures},{"logicalCountAfter",realBudget->count},{"logicalPayloadBytesAfter",realBudget->bytes},{"legacyOnlyExecutedWithStubs",true},{"sameProcessPrivatePipesOnly",true},{"productionIntegrated",false},{"CPUOrCtrlCauseClaim",false},{"kernelUncancelableFaultForced",false}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<" LastError="<<GetLastError()<<'\n';return 1;}}
