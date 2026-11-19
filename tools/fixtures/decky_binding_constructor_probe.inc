#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <array>
#include <cstdlib>
#include <new>
#include <cstdint>
#include <string>
namespace ymcc::deckybinding::fixture::publicationprobe {
inline std::atomic<unsigned> packets{0},copies{0},allocations{0},contentFailures{0};
inline std::atomic<std::size_t> bytes{0};
inline thread_local bool armed=false;
inline thread_local std::uintptr_t payloadAddress=0;
inline thread_local unsigned localAllocations=0;
inline thread_local std::size_t localBytes=0;
template<class Json> inline void begin(const Json& value){
    const auto& detail=value.at("detail");
    if(!detail.contains("index")||!detail.contains("payload"))return;
    const auto& payload=detail.at("payload").template get_ref<const std::string&>();
    payloadAddress=reinterpret_cast<std::uintptr_t>(payload.data());
    localAllocations=0;localBytes=0;armed=true;
}
template<class Packet> inline void end(const Packet& packet){
    if(!armed)return;armed=false;
    allocations+=localAllocations;bytes+=localBytes;++packets;
    const auto& payload=packet.value.at("detail").at("payload").template get_ref<const std::string&>();
    if(reinterpret_cast<std::uintptr_t>(payload.data())!=payloadAddress)++copies;
    bool valid=payload.size()==8192;for(const auto value:payload)if(value!='d')valid=false;
    if(!valid)++contentFailures;
}
}
namespace ymcc::deckybinding::fixture::detailprobe {
inline std::atomic<unsigned> packets{0},copies{0},payloadPackets{0},payloadCopies{0},allocations{0},contentFailures{0},initialPackets{0},watchPackets{0};
inline std::atomic<std::size_t> bytes{0};
inline thread_local bool armed=false,hasPayload=false;
inline thread_local unsigned phase=0,localAllocations=0;
inline thread_local std::size_t localBytes=0;
inline thread_local std::uintptr_t objectAddress=0,payloadAddress=0;
template<class Json> inline void begin(const Json& envelope,unsigned stage){
    using Object=typename Json::object_t;const auto& detail=envelope.at("detail");
    objectAddress=reinterpret_cast<std::uintptr_t>(detail.template get_ptr<const Object*>());
    hasPayload=detail.contains("index")&&detail.contains("payload");
    if(hasPayload)payloadAddress=reinterpret_cast<std::uintptr_t>(detail.at("payload").template get_ref<const std::string&>().data());
    phase=stage;localAllocations=0;localBytes=0;armed=true;
}
template<class Json> inline void end(const Json& detail){
    if(!armed)return;armed=false;using Object=typename Json::object_t;
    ++packets;if(phase==1)++initialPackets;else if(phase==2)++watchPackets;
    allocations+=localAllocations;bytes+=localBytes;
    if(reinterpret_cast<std::uintptr_t>(detail.template get_ptr<const Object*>())!=objectAddress)++copies;
    if(hasPayload){
        ++payloadPackets;const auto& payload=detail.at("payload").template get_ref<const std::string&>();
        if(reinterpret_cast<std::uintptr_t>(payload.data())!=payloadAddress)++payloadCopies;
        bool valid=payload.size()==8192;for(const auto value:payload)if(value!='d')valid=false;
        if(!valid)++contentFailures;
    }
}
}
namespace ymcc::deckybinding::fixture::diagnosticprobe {
inline std::atomic<unsigned> packets{0},copies{0},allocations{0},payloadPackets{0},payloadCopies{0},contentFailures{0};
inline std::atomic<std::size_t> bytes{0};
inline thread_local bool armed=false,hasPayload=false;
inline thread_local unsigned localAllocations=0;
inline thread_local std::size_t localBytes=0;
inline thread_local std::uintptr_t objectAddress=0,payloadAddress=0;
template<class Json> inline void begin(const Json& detail){
    using Object=typename Json::object_t;objectAddress=reinterpret_cast<std::uintptr_t>(detail.template get_ptr<const Object*>());
    hasPayload=detail.contains("index")&&detail.contains("payload");
    if(hasPayload)payloadAddress=reinterpret_cast<std::uintptr_t>(detail.at("payload").template get_ref<const std::string&>().data());
    localAllocations=0;localBytes=0;armed=true;
}
template<class Json> inline void end(const Json& detail){
    if(!armed)return;armed=false;using Object=typename Json::object_t;
    ++packets;allocations+=localAllocations;bytes+=localBytes;
    if(reinterpret_cast<std::uintptr_t>(detail.template get_ptr<const Object*>())!=objectAddress)++copies;
    if(hasPayload){
        ++payloadPackets;const auto& payload=detail.at("payload").template get_ref<const std::string&>();
        if(reinterpret_cast<std::uintptr_t>(payload.data())!=payloadAddress)++payloadCopies;
        bool valid=payload.size()==8192;for(const auto value:payload)if(value!='d')valid=false;
        if(!valid)++contentFailures;
    }
}
}
namespace ymcc::deckybinding::fixture::constructionprobe {
inline std::atomic<unsigned> budget{0},entries{0},failures{0};
inline std::atomic<std::size_t> failedBytes{0};
inline thread_local bool failNext=false;
inline thread_local unsigned skipAllocations=0;
inline std::atomic<unsigned> resourceStarts{0};
inline thread_local bool resourceSendActive=false,resourceWaitActive=false;
inline std::atomic<unsigned> resourceIoMode{0},resourceIoFailures{0},resourceIoActualError{0},resourceIoSetupError{0};
inline std::atomic<bool> resourceIoArmed{false},resourceIoCopyReleased{true};
struct ResourceApiScope{
    bool& active;
    explicit ResourceApiScope(bool& value) noexcept:active(value){active=true;}
    ~ResourceApiScope() noexcept{active=false;}
    void finish() noexcept{active=false;}
};
inline thread_local bool resourceObservationActive=false;
inline std::atomic<bool> resourceProtectRequested{false},resourceObservationCopied{false},resourceObservationValid{false},resourceObservationFixtureReleased{true},resourceObservationTrackedValid{false};
inline std::atomic<unsigned> resourceObservationDuplicates{0},resourceObservationProtected{0},resourceObservationCloseCalls{0},resourceObservationCloseFailures{0},resourceObservationCloseError{0},resourceObservationSetupError{0},resourceObservationPid{0};
inline std::atomic<unsigned long long> resourceObservationBirth{0};
inline std::atomic<HANDLE> resourceObservationTracked{nullptr};
inline unsigned long long resourceBirth(const FILETIME& value) noexcept{return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
inline void resourceObservationBegin() noexcept{resourceObservationActive=true;}
inline void resourceObservationEnd(HANDLE observed,DWORD expectedPid,bool copied) noexcept{
    resourceObservationActive=false;resourceObservationCopied=copied;
    FILETIME birth{},exit{},kernel{},user{};const DWORD pid=observed?GetProcessId(observed):0;
    resourceObservationValid=observed&&pid==expectedPid&&GetProcessTimes(observed,&birth,&exit,&kernel,&user);
    resourceObservationPid=pid;resourceObservationBirth=resourceBirth(birth);
    if(!resourceProtectRequested||!copied||!observed)return;
    if(IsDebuggerPresent()){resourceObservationSetupError=ERROR_NOT_SUPPORTED;return;}
    if(!SetHandleInformation(observed,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE)){resourceObservationSetupError=GetLastError();return;}
    resourceObservationTracked=observed;resourceObservationFixtureReleased=false;++resourceObservationProtected;
}
inline bool releaseResourceObservation() noexcept{
    const HANDLE observed=resourceObservationTracked.load();if(!observed)return true;
    DWORD flags=0;FILETIME birth{},exit{},kernel{},user{};
    const bool identity=GetHandleInformation(observed,&flags)&&(flags&HANDLE_FLAG_PROTECT_FROM_CLOSE)&&GetProcessId(observed)==resourceObservationPid&&GetProcessTimes(observed,&birth,&exit,&kernel,&user)&&resourceBirth(birth)==resourceObservationBirth;
    resourceObservationTrackedValid=identity;if(!identity)return false;
    if(!SetHandleInformation(observed,HANDLE_FLAG_PROTECT_FROM_CLOSE,0))return false;
    resourceObservationTracked=nullptr;const bool closed=::CloseHandle(observed)!=FALSE;resourceObservationFixtureReleased=closed;return closed;
}
inline BOOL WINAPI closeResourceObservation(HANDLE observed) noexcept{
    const BOOL closed=::CloseHandle(observed);
    if(observed&&observed==resourceObservationTracked.load()){
        ++resourceObservationCloseCalls;if(!closed){++resourceObservationCloseFailures;resourceObservationCloseError=GetLastError();}
    }
    return closed;
}
inline std::atomic<unsigned> resourceDiagnosticBudget{0},resourceErrorBudget{0},resourceDiagnosticHits{0},resourceErrorHits{0},resourceCleanupCalls{0};
inline std::atomic<unsigned> firstCleanupOwnerHandles{0},firstCleanupOwnerIo{0},firstCleanupReaderHandles{0},firstCleanupReaderIo{0},firstDiagnosticOwnerHandles{0},firstDiagnosticReaderHandles{0},firstErrorOwnerHandles{0},firstErrorReaderHandles{0};
inline std::atomic<bool> firstCleanupChildClosed{false},firstCleanupZero{false};
inline bool resourceFault(std::atomic<unsigned>& faultBudget) noexcept{
    auto remaining=faultBudget.load();while(remaining&&!faultBudget.compare_exchange_weak(remaining,remaining-1)){}
    return remaining!=0;
}
template<class OwnerStatus,class ReaderStatus> inline void resourceDiagnostic(const OwnerStatus& owner,const ReaderStatus& reader) noexcept{
    if(!resourceFault(resourceDiagnosticBudget))return;
    ++resourceDiagnosticHits;firstDiagnosticOwnerHandles=owner.handles;firstDiagnosticReaderHandles=reader.handles;failNext=true;
}
template<class OwnerStatus,class ReaderStatus> inline void resourceErrorResult(const OwnerStatus& owner,const ReaderStatus& reader) noexcept{
    if(!resourceFault(resourceErrorBudget))return;
    ++resourceErrorHits;firstErrorOwnerHandles=owner.handles;firstErrorReaderHandles=reader.handles;failNext=true;
}
template<class OwnerStatus,class ReaderStatus> inline void resourceCleanup(const OwnerStatus& owner,const ReaderStatus& reader,HANDLE child,bool destroyed) noexcept{
    if(resourceCleanupCalls.fetch_add(1)!=0)return;
    firstCleanupOwnerHandles=owner.handles;firstCleanupOwnerIo=owner.pendingIo;firstCleanupReaderHandles=reader.handles;firstCleanupReaderIo=reader.pendingIo;
    firstCleanupChildClosed=child==nullptr;firstCleanupZero=destroyed;
}
inline std::atomic<unsigned> preparationStage{0},syntheticFailures{0},writeSuccesses{0};
inline std::atomic<unsigned> apiFailures{0},apiFailureCode{0},ownerReturns{0},firstOwnerCode{0},firstOwnerError{0};
inline std::atomic<bool> firstOwnerFullyDestroyed{false},firstOwnerIoPending{false};
inline std::atomic<unsigned> injectedCreationError{ERROR_NOT_ENOUGH_MEMORY},postFailureCreates{0};
inline std::array<std::atomic<unsigned>,4> postCreateKinds{},postCreateErrors{};
inline thread_local bool prepareArmed=false,trackCreations=false;
inline thread_local bool handoffArmed=false,readerEventArmed=false;
inline std::atomic<unsigned> clientReturns{0},firstClientCode{0},firstClientError{0},cancelInjections{0};
template<class Result> inline void clientReturned(const Result& result) noexcept{
    const auto stage=preparationStage.load();if(stage<13||stage>17||clientReturns.fetch_add(1)!=0)return;
    firstClientCode=static_cast<unsigned>(result.code);firstClientError=result.error;
}
template<class Owner> inline void beforeChild(Owner& owner) noexcept{
    if(preparationStage.load()!=17)return;
    auto current=budget.load();while(current&&!budget.compare_exchange_weak(current,current-1)){}
    if(current){++cancelInjections;owner.requestCancel();SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
}
inline void beforeAttach() noexcept{if(preparationStage.load()==15)readerEventArmed=true;}
template<class Result> inline void ownerReturned(const Result& result) noexcept{
    const auto stage=preparationStage.load();if(stage!=6&&stage!=8&&(stage<9||stage>12))return;
    trackCreations=false;
    if(ownerReturns.fetch_add(1)!=0)return;
    firstOwnerCode=static_cast<unsigned>(result.code);firstOwnerError=result.error;firstOwnerFullyDestroyed=result.fullyDestroyed;firstOwnerIoPending=result.ioPending;
}
inline thread_local bool eventArmed=false,pipeArmed=false;
inline void preparation(unsigned location){
    const auto requested=preparationStage.load();
    if(requested!=location&&!(location==18&&requested==19))return;
    ++entries;auto current=budget.load();
    while(current&&!budget.compare_exchange_weak(current,current-1)){}
    if(!current)return;
    if(location==4||location==5){++syntheticFailures;throw std::bad_alloc();}
    skipAllocations=requested==19?1u:0u;failNext=true;
}
inline void enter() noexcept{
    if(preparationStage.load()>=13&&preparationStage.load()<=17){handoffArmed=true;return;}
    if(preparationStage.load()>=9&&preparationStage.load()<=12){prepareArmed=true;return;}
    if(preparationStage.load()==7){eventArmed=true;return;}
    if(preparationStage.load()==8){pipeArmed=true;return;}
    if(preparationStage.load())return;
    ++entries;auto current=budget.load();
    while(current&& !budget.compare_exchange_weak(current,current-1)){}
    if(current)failNext=true;
}
}
void* operator new(std::size_t size){
    namespace diagnostic=ymcc::deckybinding::fixture::diagnosticprobe;
    if(diagnostic::armed){++diagnostic::localAllocations;diagnostic::localBytes+=size;}
    namespace detail=ymcc::deckybinding::fixture::detailprobe;
    if(detail::armed){++detail::localAllocations;detail::localBytes+=size;}
    namespace publication=ymcc::deckybinding::fixture::publicationprobe;
    if(publication::armed){++publication::localAllocations;publication::localBytes+=size;}
    namespace probe=ymcc::deckybinding::fixture::constructionprobe;
    if(probe::failNext&&probe::skipAllocations)--probe::skipAllocations;
    else if(probe::failNext){probe::failNext=false;++probe::failures;probe::failedBytes=size;throw std::bad_alloc();}
    if(void* block=std::malloc(size?size:1))return block;
    throw std::bad_alloc();
}
void operator delete(void* block) noexcept{std::free(block);}
void operator delete(void* block,std::size_t) noexcept{std::free(block);}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void* block) noexcept{::operator delete(block);}
void operator delete[](void* block,std::size_t) noexcept{::operator delete(block);}
namespace ymcc::deckybinding::fixture::resourceprobe {
inline bool consume() noexcept{
    auto current=constructionprobe::budget.load();
    while(current&&!constructionprobe::budget.compare_exchange_weak(current,current-1)){}
    return current!=0;
}
inline void postCreate(unsigned kind,DWORD error) noexcept{
    if(!constructionprobe::trackCreations)return;
    const auto index=constructionprobe::postFailureCreates.fetch_add(1);
    if(index<4){constructionprobe::postCreateKinds[index]=kind;constructionprobe::postCreateErrors[index]=error;}
}
inline bool failCreation(unsigned stage) noexcept{
    if(!constructionprobe::prepareArmed||constructionprobe::preparationStage.load()!=stage||!consume())return false;
    constructionprobe::prepareArmed=false;constructionprobe::trackCreations=true;++constructionprobe::apiFailures;
    constructionprobe::apiFailureCode=constructionprobe::injectedCreationError.load();SetLastError(constructionprobe::injectedCreationError.load());return true;
}
inline HANDLE WINAPI createEvent(LPSECURITY_ATTRIBUTES security,BOOL manual,BOOL initial,LPCWSTR name){
    if(constructionprobe::readerEventArmed&&manual&&!initial&&!name&&!security&&consume()){
        constructionprobe::readerEventArmed=false;++constructionprobe::apiFailures;constructionprobe::apiFailureCode=ERROR_NOT_ENOUGH_MEMORY;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;
    }
    if(!name&&!initial&&((!manual&&!security&&failCreation(9))||(manual&&security&&failCreation(10))))return nullptr;
    if(constructionprobe::eventArmed&&manual&&!initial&&!name&&!security&&consume()){
        constructionprobe::eventArmed=false;++constructionprobe::apiFailures;constructionprobe::apiFailureCode=ERROR_NOT_ENOUGH_MEMORY;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;
    }
    const auto result=::CreateEventW(security,manual,initial,name);const DWORD error=GetLastError();postCreate(manual?2u:1u,error);return result;
}
inline HANDLE WINAPI createPipe(LPCWSTR name,DWORD openMode,DWORD pipeMode,DWORD maximum,DWORD output,DWORD input,DWORD timeout,LPSECURITY_ATTRIBUTES security){
    if(constructionprobe::pipeArmed&&consume()){
        constructionprobe::pipeArmed=false;++constructionprobe::apiFailures;constructionprobe::apiFailureCode=ERROR_ACCESS_DENIED;
        SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;
    }
    return ::CreateNamedPipeW(name,openMode,pipeMode,maximum,output,input,timeout,security);
}
}
namespace ymcc::deckybinding::fixture::resourceprobe {
inline HANDLE WINAPI createJob(LPSECURITY_ATTRIBUTES security,LPCWSTR name){
    if(!name&&!security&&failCreation(11))return nullptr;
    const auto result=::CreateJobObjectW(security,name);const DWORD error=GetLastError();postCreate(3,error);return result;
}
inline HANDLE WINAPI createPort(HANDLE file,HANDLE existing,ULONG_PTR key,DWORD threads){
    if(file==INVALID_HANDLE_VALUE&&!existing&&failCreation(12))return nullptr;
    const auto result=::CreateIoCompletionPort(file,existing,key,threads);const DWORD error=GetLastError();postCreate(4,error);return result;
}
}
namespace ymcc::deckybinding::fixture::resourceprobe {
inline BOOL WINAPI duplicate(HANDLE sourceProcess,HANDLE source,HANDLE targetProcess,LPHANDLE target,DWORD access,BOOL inherit,DWORD options){
    const auto stage=constructionprobe::preparationStage.load();
    if(constructionprobe::resourceObservationActive&&access==(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE))++constructionprobe::resourceObservationDuplicates;
    const bool child=(stage==13||stage==16)&&access==(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE);
    const bool pipe=stage==14&&access==(GENERIC_READ|SYNCHRONIZE);
    if(constructionprobe::handoffArmed&&(child||pipe)&&consume()){
        constructionprobe::handoffArmed=false;++constructionprobe::apiFailures;constructionprobe::apiFailureCode=stage==16?ERROR_ACCESS_DENIED:ERROR_NOT_ENOUGH_MEMORY;
        if(target)*target=nullptr;SetLastError(constructionprobe::apiFailureCode.load());return FALSE;
    }
    return ::DuplicateHandle(sourceProcess,source,targetProcess,target,access,inherit,options);
}
}
namespace ymcc::deckybinding::fixture::resourceprobe {
inline BOOL WINAPI writeResource(HANDLE pipe,LPCVOID bytes,DWORD size,LPDWORD written,LPOVERLAPPED overlapped){
    const auto mode=constructionprobe::resourceIoMode.load();
    if(constructionprobe::resourceSendActive&&(mode==1||mode==2)&&constructionprobe::resourceIoArmed.exchange(false)){
        HANDLE fault=INVALID_HANDLE_VALUE;
        if(mode==1&&!::DuplicateHandle(::GetCurrentProcess(),pipe,::GetCurrentProcess(),&fault,GENERIC_READ|SYNCHRONIZE,FALSE,0)){
            constructionprobe::resourceIoSetupError=::GetLastError();return FALSE;
        }
        const BOOL result=::WriteFile(fault,bytes,size,written,overlapped);const DWORD error=::GetLastError();
        if(mode==1)constructionprobe::resourceIoCopyReleased=::CloseHandle(fault)!=FALSE;
        if(!result){++constructionprobe::resourceIoFailures;constructionprobe::resourceIoActualError=error;++constructionprobe::apiFailures;constructionprobe::apiFailureCode=error;}
        ::SetLastError(error);return result;
    }
    return ::WriteFile(pipe,bytes,size,written,overlapped);
}
inline DWORD WINAPI waitResource(DWORD count,const HANDLE* handles,BOOL all,DWORD timeout){
    if(constructionprobe::resourceWaitActive&&constructionprobe::resourceIoMode==3&&constructionprobe::resourceIoArmed.exchange(false)){
        const HANDLE invalid=INVALID_HANDLE_VALUE;
        const DWORD result=::WaitForMultipleObjects(1,&invalid,FALSE,0),error=::GetLastError();
        if(result==WAIT_FAILED){++constructionprobe::resourceIoFailures;constructionprobe::resourceIoActualError=error;++constructionprobe::apiFailures;constructionprobe::apiFailureCode=error;}
        ::SetLastError(error);return result;
    }
    return ::WaitForMultipleObjects(count,handles,all,timeout);
}
}
#define WriteFile ymcc::deckybinding::fixture::resourceprobe::writeResource
#define WaitForMultipleObjects ymcc::deckybinding::fixture::resourceprobe::waitResource
#define CloseHandle ymcc::deckybinding::fixture::constructionprobe::closeResourceObservation
#define DuplicateHandle ymcc::deckybinding::fixture::resourceprobe::duplicate
#define CreateJobObjectW ymcc::deckybinding::fixture::resourceprobe::createJob
#define CreateIoCompletionPort ymcc::deckybinding::fixture::resourceprobe::createPort
#define CreateEventW ymcc::deckybinding::fixture::resourceprobe::createEvent
#define CreateNamedPipeW ymcc::deckybinding::fixture::resourceprobe::createPipe

