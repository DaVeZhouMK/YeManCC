#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <thread>
#include "sleep_fact_log_record.h"
#include "log_jsonl_boundary.h"
// Only this Journal's existing kernel/clock leaves are controlled. Other APIs
// remain real; the mutex name is private so this fixture never locks YMCC.
static DWORD createError=0,waitMode=WAIT_OBJECT_0;static ULONGLONG tick=10000;
static HANDLE journalMutex=nullptr;static unsigned journalMutexCreates=0;
static HANDLE fixtureCreateMutexW(LPSECURITY_ATTRIBUTES a,BOOL owner,LPCWSTR name){
    if(std::wstring_view(name?name:L"")==L"Local\\YeManCC.ModelAdaptationLog.v1"){
        ++journalMutexCreates;if(createError){SetLastError(createError);return nullptr;}
        static const auto own=L"Local\\YMCC.JournalFailureFixture."+std::to_wstring(GetCurrentProcessId());
        return journalMutex=::CreateMutexW(a,owner,own.c_str());
    }
    return ::CreateMutexW(a,owner,name);
}
static DWORD fixtureWaitForSingleObject(HANDLE h,DWORD ms){
    if(h==journalMutex&&waitMode!=WAIT_OBJECT_0){if(waitMode==WAIT_FAILED)SetLastError(ERROR_ACCESS_DENIED);return waitMode;}
    return ::WaitForSingleObject(h,ms);
}
static ULONGLONG fixtureGetTickCount64(){return tick;}
#define CreateMutexW fixtureCreateMutexW
#define WaitForSingleObject fixtureWaitForSingleObject
#define GetTickCount64 fixtureGetTickCount64
#include "model_adaptation_log.h"
#undef GetTickCount64
#undef WaitForSingleObject
#undef CreateMutexW
using json=nlohmann::json;namespace fs=std::filesystem;
#ifdef YMCC_JOURNAL_METADATA_FIXTURE
static std::atomic<long long> heapObjects{0};
static thread_local unsigned long long faultCountdown=0,maximumRequest=0,requestedBytes=0;
static thread_local bool faultTriggered=false,throwUnknownAllocation=false;
#endif
static thread_local bool failAllocation=false,trackAllocation=false,blockAllocation=false;
static thread_local unsigned long long allocations=0;
static std::atomic<bool> entered{false},releaseAllocation{false};
void* operator new(size_t size){
    if(trackAllocation){++allocations;
#ifdef YMCC_JOURNAL_METADATA_FIXTURE
        maximumRequest=(std::max)(maximumRequest,static_cast<unsigned long long>(size));requestedBytes+=size;
#endif
    }
#ifdef YMCC_JOURNAL_METADATA_FIXTURE
    if(throwUnknownAllocation){throwUnknownAllocation=false;throw 17;}
    if(faultCountdown&&--faultCountdown==0){faultTriggered=true;throw std::bad_alloc();}
#endif
    if(failAllocation){failAllocation=false;throw std::bad_alloc();}
    if(blockAllocation){entered.store(true);while(!releaseAllocation.load())std::this_thread::yield();}
    if(auto p=std::malloc(size?size:1)){
#ifdef YMCC_JOURNAL_METADATA_FIXTURE
        ++heapObjects;
#endif
        return p;}throw std::bad_alloc();
}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{
#ifdef YMCC_JOURNAL_METADATA_FIXTURE
    if(p)--heapObjects;
#endif
    std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
static json checks=json::array();static void check(const char* n,bool v){checks.push_back({{"name",n},{"pass",v}});std::cerr<<n<<"="<<v<<"\n";}
static std::string bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static json stage(const json& status,const char* name){for(const auto& row:status.at("failureEvidence").at("stages"))if(row.at("stage")==name)return row;return nullptr;}
static unsigned long long observations(const json& status){unsigned long long n=0;for(const auto& row:status.at("failureEvidence").at("stages"))n+=row.at("observations").get<unsigned long long>();return n;}
namespace journalIoPort {
enum class Mode {none,flush,close,read,readPrefix};
static Mode mode=Mode::none;static std::wstring target;
static HANDLE writer=nullptr;static std::array<HANDLE,16> readers{};
static unsigned attrFailAt=0,attrCalls=0,writeFaults=0,closeFaults=0,readFaults=0,active=0,prefixReads=0;
template<class T> static T real(const char* name){
    const auto raw=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);T result=nullptr;
    static_assert(sizeof(result)==sizeof(raw));std::memcpy(&result,&raw,sizeof(result));
    if(!result)std::abort();return result;
}
static bool same(LPCWSTR name) noexcept {
    if(!name||target.empty())return false;
    if(std::wstring_view(name).starts_with(L"\\\\?\\"))name+=4;
    return _wcsicmp(name,target.c_str())==0;
}
static HANDLE WINAPI createFile(LPCWSTR p,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES a,DWORD disposition,DWORD flags,HANDLE templateFile){
    static const auto call=real<decltype(&::CreateFileW)>("CreateFileW");
    const HANDLE h=call(p,access,share,a,disposition,flags,templateFile);
    if(h!=INVALID_HANDLE_VALUE&&same(p)){
        if(access&GENERIC_WRITE){writer=h;++active;}
        else if(access&GENERIC_READ){for(auto& slot:readers)if(!slot){slot=h;++active;break;}}
    }
    return h;
}
static BOOL WINAPI writeFile(HANDLE h,LPCVOID p,DWORD n,LPDWORD done,LPOVERLAPPED o){
    static const auto call=real<decltype(&::WriteFile)>("WriteFile");
    if(h==writer&&mode==Mode::flush){++writeFaults;if(done)*done=0;SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    return call(h,p,n,done,o);
}
static BOOL WINAPI readFile(HANDLE h,LPVOID p,DWORD n,LPDWORD done,LPOVERLAPPED o){
    static const auto call=real<decltype(&::ReadFile)>("ReadFile");bool tracked=false;
    for(const auto slot:readers)tracked|=slot&&h==slot;
    if(tracked&&mode==Mode::readPrefix&&prefixReads==0){++prefixReads;return call(h,p,(std::min)(n,17UL),done,o);}
    if(tracked&&(mode==Mode::read||mode==Mode::readPrefix)&&readFaults==0){++readFaults;if(done)*done=0;SetLastError(ERROR_READ_FAULT);return FALSE;}
    return call(h,p,n,done,o);
}
static BOOL WINAPI closeHandle(HANDLE h){
    static const auto call=real<decltype(&::CloseHandle)>("CloseHandle");
    const bool fail=h==writer&&mode==Mode::close;bool tracked=h==writer;
    if(h==writer)writer=nullptr;
    for(auto& slot:readers)if(slot&&slot==h){slot=nullptr;tracked=true;}
    if(tracked&&active)--active;
    const BOOL result=call(h);
    // The controlled return is separate from fixture resource cleanup. This
    // does not prove resource return after a real kernel close refusal.
    if(fail){++closeFaults;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    return result;
}
static BOOL WINAPI attributes(LPCWSTR p,GET_FILEEX_INFO_LEVELS level,LPVOID info){
    static const auto call=real<decltype(&::GetFileAttributesExW)>("GetFileAttributesExW");
    if(same(p)&&++attrCalls==attrFailAt){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return call(p,level,info);
}
static void arm(const fs::path& path,Mode value,unsigned failAt=0){
    if(active||writer)throw std::runtime_error("previous fixture file still held");
    target=path.wstring();mode=value;attrFailAt=failAt;attrCalls=writeFaults=closeFaults=readFaults=prefixReads=0;
}
static json trace(){return {{"attributeCalls",attrCalls},{"writeFaults",writeFaults},{"closeFaults",closeFaults},{"readFaults",readFaults},{"trackedFileHandles",active}};}
}
// MSVC dllimport leaves resolve through these symbols in this fixture EXE.
// DLL code and all other processes are untouched; forwarding uses kernel32's
// actual exported addresses rather than these fixture import pointers.
extern "C" {
decltype(&::CreateFileW) __imp_CreateFileW=journalIoPort::createFile;
decltype(&::WriteFile) __imp_WriteFile=journalIoPort::writeFile;
decltype(&::ReadFile) __imp_ReadFile=journalIoPort::readFile;
decltype(&::CloseHandle) __imp_CloseHandle=journalIoPort::closeHandle;
decltype(&::GetFileAttributesExW) __imp_GetFileAttributesExW=journalIoPort::attributes;
}

static int journalTailCases(const fs::path& root){
    using ymcc::adaptlog::Journal;using ymcc::adaptlog::RecordResult;
    json reports=json::array();
    auto finish=[&]{unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
        std::cout<<json{{"checks",checks},{"failures",failed},{"reports",reports},{"scope","tail-space-only;no complete module,device or CPU claim"}}.dump(2)<<'\n';return failed?3:0;};
    const json model={{"model","tail-fixture"}},first={{"kind",1}},largeId={{"kind",2}},smallId={{"kind",3}},compact={{"original",true}},large={{"data",std::string(4096,'x')}};
    const std::string label="fixture";
    auto record=[&](Journal& j,const fs::path& p,const json& id,const json& sample){return j.recordResult(p,model,"fixture-tail",id,sample,label);};
    auto counts=[](const json& s)->const json& {return s.at("budgetEvidence").at("observations");};
    const auto sizingPath=root/L"sizing.jsonl";Journal sizing(1000000,100,10000);
    check("journal-tail-size-actual-compact-row",record(sizing,sizingPath,first,compact)==RecordResult::saved);
    const auto original=bytes(sizingPath);const auto rowBytes=original.size();
    const auto p=root/L"tail.jsonl";{std::ofstream out(p,std::ios::binary);out<<original;}
    const auto maximum=rowBytes*2+128;Journal tail(maximum,100,10000);
    check("journal-tail-large-rejection-keeps-original-bytes",record(tail,p,largeId,large)==RecordResult::omitted&&bytes(p)==original&&counts(tail.status())["appendBytesLimit"]==1);
    const auto afterLarge=tail.status();const auto smallResult=record(tail,p,smallId,compact);const auto afterSmall=tail.status();
    reports.push_back({{"case","tail-space"},{"originalBytes",rowBytes},{"maxBytes",maximum},{"afterLarge",afterLarge},{"afterSmall",afterSmall},{"smallResultSaved",smallResult==RecordResult::saved},{"fileBytes",fs::file_size(p)}});
    const bool admitted=smallResult==RecordResult::saved&&afterSmall["committedThisProcess"]==1&&fs::file_size(p)<=maximum&&bytes(p).starts_with(original);
    check("journal-tail-smaller-later-type-can-use-remaining-space",admitted);
    if(!admitted)return finish();
    check("journal-tail-row-rejection-does-not-claim-whole-path-full",afterLarge["budgetFull"]==false&&counts(afterSmall)["cachedFullPath"]==0&&afterSmall["budgetOmissions"]==1&&afterSmall["writeFailures"]==0);
    const auto beforeDuplicate=bytes(p);
    check("journal-tail-existing-key-remains-saved-without-appending",record(tail,p,first,large)==RecordResult::saved&&bytes(p)==beforeDuplicate&&tail.status()["budgetOmissions"]==1);
    auto all=bytes(p);size_t lines=0;for(const char c:all)lines+=c=='\n';
    const auto nl=all.find('\n');const auto retained=json::parse(all.substr(nl+1));
    check("journal-tail-written-row-is-original-new-type-only",lines==2&&retained["uniqueKey"]["identity"]==smallId&&retained["firstSample"]==compact);
    const auto other=root/L"tail-other.jsonl";
    check("journal-tail-other-path-preserves-original-independent-budget",record(tail,other,largeId,compact)==RecordResult::saved&&fs::file_size(other)<=maximum);
    // Row-count exhaustion still has its original no-allocation/no-IO fast path.
    const auto countPath=root/L"count.jsonl";Journal count(1000000,1,10000);
    const bool started=record(count,countPath,first,compact)==RecordResult::saved;
    const bool exhausted=record(count,countPath,smallId,compact)==RecordResult::omitted;
    const auto before=count.status();const auto preserved=bytes(countPath);const auto mutexBefore=journalMutexCreates;
    journalIoPort::attrCalls=0;allocations=0;trackAllocation=true;bool omitted=true;
    for(unsigned i=0;i<50000;++i)omitted&=record(count,countPath,smallId,compact)==RecordResult::omitted;
    trackAllocation=false;const auto repeatedAllocations=allocations;const auto attrs=journalIoPort::attrCalls;const auto after=count.status();
    check("journal-tail-count-full-50000-still-zero-allocation-mutex-and-query",started&&exhausted&&omitted&&repeatedAllocations==0&&journalMutexCreates==mutexBefore&&attrs==0&&before["journalReadScans"]==after["journalReadScans"]&&bytes(countPath)==preserved);
    check("journal-tail-count-full-counters-stay-distinct",counts(after)["recordCountLimit"]==1&&counts(after)["cachedFullPath"]==50000&&counts(after)["appendBytesLimit"]==0&&after["budgetOmissions"]==50001);
    reports.push_back({{"case","count-repeat"},{"allocations",repeatedAllocations},{"attributeCalls",attrs},{"status",after}});
    // Exercise the existing upstream terminal store: omitted is already final.
    ymcc::adaptlog::Signals signals;ymcc::adaptlog::Signal value;value.type.source=99;value.type.code=7;signals.offer(value);
    size_t index=0;ymcc::adaptlog::Signal taken;const bool took=signals.take(index,taken);
    const auto omittedResult=record(tail,p,largeId,large);signals.finish(index,omittedResult!=RecordResult::retry);
    allocations=0;trackAllocation=true;for(unsigned i=0;i<50000;++i)signals.offer(value);trackAllocation=false;const auto signalNew=allocations;
    check("journal-tail-original-omitted-signal-drains-once-for-50000-repeats",took&&omittedResult==RecordResult::omitted&&!signals.pending()&&!signals.take(index,taken)&&signalNew==0);
    ymcc::adaptlog::VirtualFacts facts;const ymcc::adaptlog::Fact fact{model,largeId,large,"fixture-tail"};facts.offer(fact);
    ymcc::adaptlog::Fact takenFact;const bool factTook=facts.take(index,takenFact);facts.finish(index,omittedResult!=RecordResult::retry);
    const auto factMutexBefore=journalMutexCreates;
    for(unsigned i=0;i<50000;++i)facts.offer(fact);
    check("journal-tail-original-omitted-fact-drains-once-for-50000-repeats",factTook&&!facts.pending()&&!facts.take(index,takenFact)&&journalMutexCreates==factMutexBefore);
    // A genuinely observed over-budget file retains its old global latch.
    const auto overPath=root/L"over.jsonl";{std::ofstream out(overPath,std::ios::binary);out<<original;}
    Journal over(1,100,10000);const auto overResult=record(over,overPath,first,compact);const auto overStatus=over.status();
    check("journal-tail-observed-over-budget-file-is-preserved-and-latched",overResult==RecordResult::omitted&&overStatus["budgetFull"]==true&&counts(overStatus)["observedFileOverMaxBytes"]==1&&bytes(overPath)==original);
    return finish();
}

static int journalBudgetCases(const fs::path& root){
    using ymcc::adaptlog::Journal;using ymcc::adaptlog::RecordResult;
    const json model={{"model","fixture"}},identity={{"kind",1}},second={{"kind",2}},sample={{"original",true}};
    const std::string label="fixture";json reports=json::array();
    auto counts=[](Journal& j){return j.status().at("budgetEvidence").at("observations");};
    auto record=[&](Journal& j,const fs::path& p,const json& id,const json& data){return j.recordResult(p,model,"fixture-type",id,data,label);};
    auto finish=[&]{unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
        std::cout<<json{{"checks",checks},{"failures",failed},{"reports",reports},{"scope","budget branches only;not complete history or OS resource failure"}}.dump(2)<<'\n';return failed?3:0;};
    (void)ymcc::sleepfactlog::writerIdentity();
    const auto warm=fixtureCreateMutexW(nullptr,FALSE,L"Local\\YeManCC.ModelAdaptationLog.v1");if(warm)CloseHandle(warm);
    DWORD handlesBefore=0,handlesAfter=0;const bool handlesKnown=GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE;
    Journal empty;const auto initial=empty.status();
    check("journal-budget-optional-observation-exists",initial.contains("budgetEvidence"));
    if(!initial.contains("budgetEvidence"))return finish();
    const json zero={{"cachedFullPath",0},{"observedFileOverMaxBytes",0},{"recordCountLimit",0},{"recordBytesLimit",0},{"appendBytesLimit",0}};
    check("journal-budget-initial-five-zeros-are-not-health-proof",counts(empty)==zero&&initial["budgetEvidence"]["durableHistoryProven"]==false);
    const auto normalFile=root/L"budget-normal.jsonl";
    check("journal-budget-normal-save-and-duplicate-unobserved",record(empty,normalFile,identity,sample)==RecordResult::saved&&record(empty,normalFile,identity,sample)==RecordResult::saved&&counts(empty)==zero&&empty.status()["committedThisProcess"]==1);
    const auto smallFile=root/L"budget-row.jsonl";Journal row(1000000,100,1500);
    const auto rejected=record(row,smallFile,identity,{{"large",std::string(4096,'x')}});
    const auto rowStatus=row.status();
    check("journal-budget-large-record-does-not-latch-full",rejected==RecordResult::omitted&&counts(row)["recordBytesLimit"]==1&&rowStatus["budgetFull"]==false&&!fs::exists(smallFile));
    check("journal-budget-smaller-later-type-still-saves",record(row,smallFile,second,sample)==RecordResult::saved&&row.status()["budgetOmissions"]==1&&counts(row)["recordBytesLimit"]==1);
    reports.push_back({{"case","record-only"},{"status",row.status()}});
    const auto countFile=root/L"budget-count.jsonl";Journal limit(1000000,1,10000);
    const bool first=record(limit,countFile,identity,sample)==RecordResult::saved;const auto original=bytes(countFile);
    check("journal-budget-count-limit-is-distinct",first&&record(limit,countFile,second,sample)==RecordResult::omitted&&counts(limit)["recordCountLimit"]==1&&counts(limit)["appendBytesLimit"]==0&&bytes(countFile)==original);
    const auto prior=limit.status();journalIoPort::arm(countFile,journalIoPort::Mode::none);
    const auto mutexBefore=journalMutexCreates;allocations=0;trackAllocation=true;
    bool allOmitted=true;for(unsigned i=0;i<50000;++i)allOmitted&=record(limit,countFile,second,sample)==RecordResult::omitted;
    trackAllocation=false;const auto repeated=limit.status();
    check("journal-budget-50000-cached-omissions-no-new-or-mutex",allOmitted&&allocations==0&&journalMutexCreates==mutexBefore&&journalIoPort::attrCalls==0&&repeated["journalReadScans"]==prior["journalReadScans"]&&bytes(countFile)==original);
    check("journal-budget-cached-observations-not-reclassified-as-new-count-limit",counts(limit)["cachedFullPath"]==50000&&counts(limit)["recordCountLimit"]==1&&repeated["budgetOmissions"]==50001&&repeated["writeFailures"]==0);
    reports.push_back({{"case","count-and-cached"},{"status",repeated},{"repeatedCppAllocations",allocations},{"repeatedMutexCreates",journalMutexCreates-mutexBefore}});
    const auto oversizedFile=root/L"budget-existing.jsonl";{std::ofstream out(oversizedFile,std::ios::binary);out<<"original retained bytes";}
    Journal existing(1,100,10000);const auto oldBytes=bytes(oversizedFile);
    check("journal-budget-existing-oversize-file-preserved",record(existing,oversizedFile,identity,sample)==RecordResult::omitted&&counts(existing)["observedFileOverMaxBytes"]==1&&existing.status()["journalReadScans"]==0&&bytes(oversizedFile)==oldBytes);
    reports.push_back({{"case","existing-file"},{"status",existing.status()}});
    const auto byteFile=root/L"budget-bytes.jsonl";{std::ofstream out(byteFile,std::ios::binary);out<<original;}
    Journal byteLimit(original.size()+2,100,10000);
    check("journal-budget-append-byte-limit-before-write",record(byteLimit,byteFile,second,sample)==RecordResult::omitted&&counts(byteLimit)["appendBytesLimit"]==1&&counts(byteLimit)["recordCountLimit"]==0&&counts(byteLimit)["recordBytesLimit"]==0&&bytes(byteFile)==original);
    reports.push_back({{"case","append-bytes"},{"status",byteLimit.status()}});
    const auto overlapFile=root/L"budget-overlap.jsonl";{std::ofstream out(overlapFile,std::ios::binary);out<<original;}
    Journal overlap(original.size()+2,1,1);
    check("journal-budget-overlapping-limits-count-one-omission",record(overlap,overlapFile,second,sample)==RecordResult::omitted&&overlap.status()["budgetOmissions"]==1&&counts(overlap)["appendBytesLimit"]==1&&counts(overlap)["recordCountLimit"]==1&&counts(overlap)["recordBytesLimit"]==1&&bytes(overlapFile)==original);
    reports.push_back({{"case","overlap"},{"status",overlap.status()}});
    const auto otherFile=root/L"budget-other-path.jsonl";
    check("journal-budget-cached-latch-keeps-original-path-scope",record(limit,otherFile,second,sample)==RecordResult::saved&&counts(limit)["cachedFullPath"]==50000&&counts(limit)["recordCountLimit"]==1);
    check("journal-budget-own-handle-count-return",handlesKnown&&GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)&&handlesBefore==handlesAfter&&journalIoPort::active==0);
    return finish();
}

static int journalIoCases(const fs::path& root,bool readOnly){
    using ymcc::adaptlog::Journal;using ymcc::adaptlog::RecordResult;namespace port=journalIoPort;
    const json model={{"model","fixture"}},identity={{"kind",1}},sample={{"original",true}};const std::string label="fixture";
    json reports=json::array();DWORD before=0,after=0;const bool beforeKnown=GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE;
    auto record=[&](Journal& j,const fs::path& p){return j.recordResult(p,model,"fixture-type",identity,sample,label);};
    auto finish=[&]{
        port::mode=port::Mode::none;port::target.clear();
        const bool afterKnown=GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE;
        check("journal-io-own-handles-return",beforeKnown&&afterKnown&&before==after&&port::active==0);
        unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
        {std::ofstream out(root/L"io-statuses.json");out<<reports.dump(2)<<'\n';}
        std::cout<<json{{"checks",checks},{"failures",failed},{"reports",reports},{"handlesBefore",before},{"handlesAfter",after},
            {"controlledKernelReturns",true},{"actualMsvcStreamsAndJournal",true},{"closeResourceCleanupIsFixtureOnly",true}}.dump(2)<<'\n';return failed?3:0;
    };
    if(!readOnly){
        for(const auto mode:{port::Mode::flush,port::Mode::close}){
            const auto file=root/(mode==port::Mode::flush?L"flush.jsonl":L"close.jsonl");{std::ofstream create(file);}
            Journal journal;port::arm(file,mode);const auto result=record(journal,file);const auto status=journal.status();
            const auto name=mode==port::Mode::flush?"file-write-flush":"file-close";const auto selected=stage(status,name);const auto trace=port::trace();
            const bool pass=result==RecordResult::retry&&status["writeFailures"]==1&&selected["firstState"]=="retained"&&selected["first"]["codeKnown"]==false&&
                (mode==port::Mode::flush?port::writeFaults>0:port::closeFaults==1)&&port::active==0;
            check(mode==port::Mode::flush?"journal-original-stream-flush-failure-is-unknown-code":"journal-original-stream-close-failure-is-unknown-code",pass);
            reports.push_back({{"stage",name},{"status",status},{"trace",trace}});port::mode=port::Mode::none;port::target.clear();if(!pass)return finish();
        }
        static const char* names[]={"file-exists-query","file-size-query","file-time-query","committed-file-size-query","committed-file-time-query"};
        for(unsigned i=0;i<5;++i){
            const auto file=root/(L"attribute-"+std::to_wstring(i)+L".jsonl");{std::ofstream create(file);}
            Journal journal;port::arm(file,port::Mode::none,i+1);const auto result=record(journal,file);const auto status=journal.status();const auto selected=stage(status,names[i]);
            const auto expected=i<3?RecordResult::retry:RecordResult::saved;
            const bool pass=result==expected&&status["writeFailures"]==(i<3?1:0)&&status["committedThisProcess"]==(i<3?0:1)&&
                selected["firstState"]=="retained"&&selected["first"]["codeKnown"]==true&&selected["first"]["code"]==5&&
                selected["first"]["codeDomain"]=="std-error-code:system"&&port::attrCalls==i+1&&port::active==0;
            check(names[i],pass);reports.push_back({{"stage",names[i]},{"status",status},{"trace",port::trace()}});port::target.clear();if(!pass)return finish();
        }
    }
    const json existing={{"uniqueKey",{{"schema",1},{"model",model},{"category","fixture-type"},{"identity",identity}}}};
    for(const auto mode:{port::Mode::read,port::Mode::readPrefix}){
        const auto file=root/(mode==port::Mode::read?L"read-zero.jsonl":L"read-prefix.jsonl");
        {std::ofstream create(file);create<<existing.dump()<<'\n';}
        const auto original=bytes(file);Journal journal;port::arm(file,mode);const auto result=record(journal,file);const auto status=journal.status();
        port::mode=port::Mode::none;const bool unchanged=bytes(file)==original;const auto first=stage(status,"file-read");
        const bool pass=port::readFaults==1&&result==RecordResult::retry&&first["firstState"]=="retained"&&first["first"]["codeKnown"]==false&&
            status["writeFailures"]==1&&status["committedThisProcess"]==0&&unchanged&&port::active==0;
        check(mode==port::Mode::read?"journal-zero-read-failure-does-not-append":"journal-partial-read-failure-does-not-append",pass);
        reports.push_back({{"stage","file-read"},{"status",status},{"trace",port::trace()},{"bytesUnchanged",unchanged},{"returnedSaved",result==RecordResult::saved}});
        if(!pass)return finish();
        tick+=5001;const auto recovered=record(journal,file);const auto recoveredStatus=journal.status();
        const bool recoveredOk=recovered==RecordResult::saved&&recoveredStatus["committedThisProcess"]==0&&bytes(file)==original&&port::active==0;
        check(mode==port::Mode::read?"journal-zero-read-recovery-does-not-rewrite":"journal-partial-read-recovery-does-not-rewrite",recoveredOk);
        reports.push_back({{"stage","read-recovery"},{"status",recoveredStatus},{"trace",port::trace()}});port::target.clear();if(!recoveredOk)return finish();
    }
    const std::string ordinary=existing.dump();
    const std::array<std::string,6> originals={ordinary+"\n",ordinary,ordinary+"\r\n\r\n",std::string("\n\r\n")+ordinary+"\n",ordinary+"\n{broken",std::string("\n")};
    for(size_t i=0;i<originals.size();++i){
        const auto file=root/(L"ordinary-"+std::to_wstring(i)+L".jsonl");{std::ofstream create(file,std::ios::binary);create<<originals[i];}
        Journal journal;port::arm(file,port::Mode::none);const auto result=record(journal,file);const auto status=journal.status();
        const bool empty=i==5;const bool pass=result==RecordResult::saved&&status["writeFailures"]==0&&observations(status)==0&&status["committedThisProcess"]==(empty?1:0)&&
            (empty||bytes(file)==originals[i])&&port::active==0;
        const auto caseLabel=std::string("journal-complete-read-delimiter-case-")+std::to_string(i);check(caseLabel.c_str(),pass);
        reports.push_back({{"stage","complete-read"},{"status",status},{"trace",port::trace()}});port::target.clear();if(!pass)return finish();
    }
    return finish();
}

#ifdef YMCC_JOURNAL_METADATA_FIXTURE
#include <barrier>
#include "journal-status-control.inc"
static int journalMetadataCases(const fs::path& root){
    using ymcc::adaptlog::Journal;using ymcc::adaptlog::RecordResult;
    using ymcc::adaptlog::JournalStatusControl;using Evidence=ymcc::adaptlog::JournalFailureEvidence;
    json reports=json::array();DWORD baselineHandles=0,finalHandles=0;
    const bool baselineKnown=GetProcessHandleCount(GetCurrentProcess(),&baselineHandles)!=FALSE;
    auto finish=[&]{
        faultCountdown=0;throwUnknownAllocation=false;trackAllocation=false;
        const bool finalKnown=GetProcessHandleCount(GetCurrentProcess(),&finalHandles)!=FALSE;
        check("journal-meta-own-handles-return",baselineKnown&&finalKnown&&baselineHandles==finalHandles);
        unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
        std::cout<<json{{"checks",checks},{"failures",failed},{"reports",reports},{"handlesBefore",baselineHandles},{"handlesAfter",finalHandles},
            {"control","exact actual Journal class;only optional status append removed and class/constructor renamed"},
            {"wholeHeapOrAbiPublicationProven",false}}.dump(2)<<'\n';return failed?3:0;
    };
    Journal actual;JournalStatusControl legacy;
    allocations=0;trackAllocation=true;const json original=legacy.status();trackAllocation=false;const auto originalAllocations=allocations;
    allocations=0;trackAllocation=true;const json full=actual.status();trackAllocation=false;const auto fullAllocations=allocations;
    json stripped=full;stripped.erase("failureEvidence");stripped.erase("budgetEvidence");
    std::cerr<<"originalAllocations="<<originalAllocations<<",fullAllocations="<<fullAllocations<<"\n";
    const bool baselineOk=stripped==original&&fullAllocations>originalAllocations&&fullAllocations<2048;
    check("journal-meta-original-status-is-exact-prefix-control",baselineOk);if(!baselineOk)return finish();
    unsigned optionalFaults=0;bool optionalIntact=true,heapBalanced=true;json failedOrdinals=json::array();
    for(unsigned long long ordinal=originalAllocations+1;ordinal<=fullAllocations;++ordinal){
        const auto heapBefore=heapObjects.load();faultTriggered=false;faultCountdown=ordinal;
        bool threw=false,same=false;
        try {auto observed=actual.status();faultCountdown=0;observed.erase("failureEvidence");observed.erase("budgetEvidence");same=observed==original;}
        catch(...){faultCountdown=0;threw=true;}
        const bool triggered=faultTriggered;optionalFaults+=triggered;
        heapBalanced&=heapObjects.load()==heapBefore;optionalIntact&=triggered&&!threw&&same;
        if((!triggered||threw||!same)&&failedOrdinals.size()<16)failedOrdinals.push_back(ordinal);
    }
    check("journal-meta-every-optional-allocation-fault-keeps-original-status",optionalIntact&&optionalFaults==fullAllocations-originalAllocations);
    check("journal-meta-optional-fault-unwind-returns-cpp-new-objects",heapBalanced);
    reports.push_back({{"case","optional-allocation-faults"},{"originalAllocations",originalAllocations},{"fullAllocations",fullAllocations},
        {"faultsActuallyTriggered",optionalFaults},{"failedOrdinals",failedOrdinals},{"originalStatus",original},{"fullStatus",full},
        {"heapScope","C++ scalar/array new/delete outstanding objects only;not bytes,RSS,CRT or OS heap"}});
    if(!optionalIntact||!heapBalanced)return finish();
    bool legacyFaultPreserved=true;
    for(unsigned long long ordinal=1;ordinal<=originalAllocations;++ordinal){
        bool beforeThrows=false,afterThrows=false;
        faultCountdown=ordinal;try{(void)legacy.status();}catch(const std::bad_alloc&){beforeThrows=true;}faultCountdown=0;
        faultCountdown=ordinal;try{(void)actual.status();}catch(const std::bad_alloc&){afterThrows=true;}faultCountdown=0;
        legacyFaultPreserved&=beforeThrows&&afterThrows;
    }
    const json afterFault=actual.status();json afterStripped=afterFault;afterStripped.erase("failureEvidence");afterStripped.erase("budgetEvidence");
    check("journal-meta-original-allocation-errors-still-propagate-and-unlock",legacyFaultPreserved&&afterStripped==original);
    const json model={{"model","fixture"}},identity={{"kind",51}},sample={{"original",true}};const std::string label="fixture";
    const auto unknownFile=root/L"unknown.jsonl";
    const HANDLE warm=fixtureCreateMutexW(nullptr,FALSE,L"Local\\YeManCC.ModelAdaptationLog.v1");
    if(warm)CloseHandle(warm);
    Journal unknown;throwUnknownAllocation=true;
    const auto unknownResult=unknown.recordResult(unknownFile,model,"fixture-unknown",identity,sample,label);
    throwUnknownAllocation=false;const auto unknownStatus=unknown.status();const auto unknownFirst=stage(unknownStatus,"exception");
    const bool unknownOk=warm&&unknownResult==RecordResult::retry&&unknownStatus["writeFailures"]==1&&
        unknownFirst["firstState"]=="retained"&&unknownFirst["first"]["exceptionKind"]=="unknown-exception"&&
        unknownFirst["first"]["codeKnown"]==false&&unknownFirst["first"]["code"].is_null()&&!fs::exists(unknownFile);
    check("journal-meta-nonstandard-throw-is-unknown-without-api-code",unknownOk);
    const auto recovered=unknown.recordResult(unknownFile,model,"fixture-unknown",identity,sample,label);
    const auto recoveredStatus=unknown.status();
    check("journal-meta-unknown-throw-returns-mutex-and-preserves-first",recovered==RecordResult::saved&&recoveredStatus["committedThisProcess"]==1&&stage(recoveredStatus,"exception")["first"]==unknownFirst["first"]);
    reports.push_back({{"case","unknown-exception"},{"before",unknownStatus},{"after",recoveredStatus},{"origin","fixture custom operator new throws int;not a Win32 API exception or kernel exhaustion"}});
    if(!unknownOk)return finish();
    constexpr unsigned workers=32,iterations=1000,snapshots=128;
    Evidence evidence;std::barrier gate(static_cast<std::ptrdiff_t>(workers+1));
    std::array<std::thread,workers> threads;std::array<unsigned long long,workers> perThreadAllocations{};
    for(unsigned i=0;i<workers;++i)threads[i]=std::thread([&,i]{
        gate.arrive_and_wait();allocations=0;trackAllocation=true;
        for(unsigned k=0;k<iterations;++k)evidence.observe(Evidence::Stage::mutexCreate,Evidence::Domain::win32,8+i);
        trackAllocation=false;perThreadAllocations[i]=allocations;
    });
    gate.arrive_and_wait();bool coherent=true;unsigned publishingSeen=0,retainedSeen=0;unsigned long long firstCode=0;
    for(unsigned k=0;k<snapshots;++k){const auto view=evidence.snapshot();const auto& row=view["stages"][2];
        coherent&=view["snapshotAtomicWithCounters"]==false&&row["observations"].get<unsigned long long>()<=workers*iterations;
        if(row["firstState"]=="publishing"){++publishingSeen;coherent&=!row.contains("first");}
        else if(row["firstState"]=="retained"){
            ++retainedSeen;const auto code=row["first"]["code"].get<unsigned long long>();
            coherent&=code>=8&&code<8+workers&&row["first"]["codeDomain"]=="win32-last-error"&&row["first"]["codeKnown"]==true&&
                row["first"]["waitStatusKnown"]==false&&row["first"]["waitStatus"].is_null()&&row["first"]["exceptionKind"].is_null();
            if(!firstCode)firstCode=code;else coherent&=firstCode==code;
        }else coherent&=row["firstState"]=="not-observed"&&!row.contains("first");
    }
    for(auto& thread:threads)thread.join();const auto settled=evidence.snapshot();const auto& row=settled["stages"][2];
    check("journal-meta-concurrent-snapshots-never-expose-partial-first",coherent&&row["firstState"]=="retained"&&retainedSeen>0);
    bool noAllocation=true;for(const auto count:perThreadAllocations)noAllocation&=count==0;
    check("journal-meta-32000-observations-count-once-and-allocate-zero",noAllocation&&row["observations"]==workers*iterations);
    const auto beforeRepeat=row["first"];evidence.observe(Evidence::Stage::mutexCreate,Evidence::Domain::win32,123456);
    const auto repeated=evidence.snapshot();
    check("journal-meta-concurrent-first-stays-immutable-after-later-code",repeated["stages"][2]["observations"]==workers*iterations+1&&repeated["stages"][2]["first"]==beforeRepeat);
    reports.push_back({{"case","concurrent-publication"},{"workers",workers},{"iterations",iterations},{"snapshots",snapshots},
        {"publishingSnapshotsActuallySeen",publishingSeen},{"retainedSnapshotsActuallySeen",retainedSeen},{"settled",settled},{"afterRepeat",repeated},
        {"inputs","controlled collector values;not real API failure counts"},{"allInterleavingsProven",false}});
    return finish();
}
#endif

#ifdef YMCC_JOURNAL_METADATA_FIXTURE
static int journalMetadataFieldCases(){
    using Evidence=ymcc::adaptlog::JournalFailureEvidence;json reports=json::array();
    for(const auto exception:{Evidence::Exception::allocation,Evidence::Exception::standard,Evidence::Exception::unknown}){
        Evidence evidence;
        for(size_t i=0;i<static_cast<size_t>(Evidence::Stage::count);++i){
            const auto stageValue=static_cast<Evidence::Stage>(i);auto domain=Evidence::Domain::none;std::int64_t code=0;
            bool waitKnown=false;DWORD wait=0;
            if(stageValue==Evidence::Stage::mutexCreate){domain=Evidence::Domain::win32;code=8;}
            if(stageValue==Evidence::Stage::mutexWait){domain=Evidence::Domain::win32;code=5;waitKnown=true;wait=WAIT_FAILED;}
            if(stageValue==Evidence::Stage::exists||stageValue==Evidence::Stage::time){domain=Evidence::Domain::generic;code=13;}
            if(stageValue==Evidence::Stage::size||stageValue==Evidence::Stage::committedSize){domain=Evidence::Domain::system;code=5;}
            if(stageValue==Evidence::Stage::committedTime){domain=Evidence::Domain::other;code=-22;}
            evidence.observe(stageValue,domain,code,waitKnown,wait,stageValue==Evidence::Stage::exception?exception:Evidence::Exception::none);
        }
        allocations=0;trackAllocation=true;const json expected=evidence.snapshot();trackAllocation=false;const auto count=allocations;
        bool allThrow=true,balanced=true;unsigned triggeredCount=0;json bad=json::array();
        for(unsigned long long ordinal=1;ordinal<=count;++ordinal){
            const auto heapBefore=heapObjects.load();faultTriggered=false;faultCountdown=ordinal;bool caught=false;
            try{const auto view=evidence.snapshot();faultCountdown=0;(void)view;}catch(const std::bad_alloc&){faultCountdown=0;caught=true;}
            catch(...){faultCountdown=0;}
            allThrow&=faultTriggered&&caught;triggeredCount+=faultTriggered;balanced&=heapObjects.load()==heapBefore;
            if((!caught||!faultTriggered)&&bad.size()<16)bad.push_back(ordinal);
        }
        const auto actual=evidence.snapshot();const bool unchanged=actual==expected;
        check("journal-meta-retained-fields-all-construction-faults-propagate",count>0&&count<4096&&allThrow&&triggeredCount==count);
        check("journal-meta-retained-fields-unwind-returns-cpp-objects",balanced);
        check("journal-meta-retained-first-descriptors-unchanged-after-faults",unchanged);
        reports.push_back({{"exceptionKind",expected["stages"][12]["first"]["exceptionKind"]},{"allocationPoints",count},
            {"faultsActuallyTriggered",triggeredCount},{"badOrdinals",bad},{"snapshot",expected},{"allStagesPrimed",true},
            {"inputs","controlled collector fields;not actual API-failure census"}});
        if(!allThrow||!balanced||!unchanged)break;
    }
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failed},{"reports",reports},{"noDeviceOrProductProcessActions",true}}.dump(2)<<'\n';return failed?3:0;
}
#endif

#if defined(YMCC_JOURNAL_METADATA_FIXTURE) && __has_include("adaptation-status-controls.inc")
#include "adaptation-status-controls.inc"
#include "adaptation-original-stores.inc"
template<class Actual,class Control,class Original,class Prime> static bool storeStatusFaults(const char* name,const char* optionalKey,Actual& actual,Control& control,Original& originalStore,Prime&& prime,json& report){
    prime(actual);prime(control);prime(originalStore);
    allocations=0;trackAllocation=true;const json original=control.status();trackAllocation=false;const auto baseAllocations=allocations;
    allocations=0;trackAllocation=true;const json full=actual.status();trackAllocation=false;const auto fullAllocations=allocations;
    json stripped=full;stripped.erase(optionalKey);const bool calibrated=stripped==original&&full==originalStore.status()&&fullAllocations>baseAllocations&&fullAllocations<8192;
    check("stores-original-status-exact-control",calibrated);
    std::cerr<<"store-case="<<name<<",baseAllocations="<<baseAllocations<<",fullAllocations="<<fullAllocations<<"\n";
    if(!calibrated)return false;
    bool safe=true,balanced=true;unsigned faults=0;
    for(unsigned long long ordinal=baseAllocations+1;ordinal<=fullAllocations;++ordinal){
        std::cerr<<"store-optional-ordinal="<<ordinal<<"\n";
        const auto heapBefore=heapObjects.load();faultTriggered=false;faultCountdown=ordinal;
        bool threw=false,same=false;
        try {auto observed=actual.status();faultCountdown=0;observed.erase(optionalKey);same=observed==original;}
        catch(...){faultCountdown=0;threw=true;}
        faults+=faultTriggered;safe&=faultTriggered&&!threw&&same;balanced&=heapObjects.load()==heapBefore;
    }
    check("stores-optional-single-fault-keeps-base-status",safe&&faults==fullAllocations-baseAllocations);
    check("stores-optional-unwind-returns-cpp-objects",balanced);
    if(!safe||!balanced)return false;
    bool originalErrors=true;
    for(unsigned long long ordinal=1;ordinal<=baseAllocations;++ordinal){
        faultCountdown=ordinal;bool caught=false;try{(void)actual.status();}catch(const std::bad_alloc&){caught=true;}faultCountdown=0;originalErrors&=caught;
    }
    auto after=actual.status();after.erase(optionalKey);check("stores-original-single-fault-propagates-and-unlocks",originalErrors&&after==original);
    report={{"case",name},{"baseAllocations",baseAllocations},{"fullAllocations",fullAllocations},{"optionalFaults",faults},{"baseStatus",original},{"fullStatus",full}};
    return originalErrors;
}
static int adaptationStoreStatusCases(bool baseline,bool factsOnly=false){
    using namespace ymcc::adaptlog;json reports=json::array();
    auto finish=[&]{unsigned failed=0;for(const auto& item:checks)failed+=!item["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"reports",reports},{"singleAllocationFault",true},{"wholeHeapOrRuntimeProof",false}}.dump(2)<<'\n';return failed?3:0;};
    if(!factsOnly){
        Signals actual;SignalsStatusControl control;SignalsOriginal originalStore;json report;
        const bool ok=storeStatusFaults("signals-empty","rejectedTypeEvidence",actual,control,originalStore,[](auto&){},report);
        reports.push_back(std::move(report));if(!ok||baseline)return finish();
    }
    if(!factsOnly){
        Signals actual;SignalsStatusControl control;SignalsOriginal originalStore;json report;
        auto prime=[](auto& store){for(unsigned i=0;i<129;++i){Signal value;value.type.code=i;value.type.canonicalMask=0x8000000000000001ULL;store.offer(value);}};
        const bool ok=storeStatusFaults("signals-capacity","rejectedTypeEvidence",actual,control,originalStore,prime,report);reports.push_back(std::move(report));if(!ok)return finish();
    }
    if(!factsOnly){
        Signals actual;SignalsStatusControl control;SignalsOriginal originalStore;json report;
        auto prime=[](auto& store){entered=false;releaseAllocation=false;std::thread holder([&]{blockAllocation=true;(void)store.status();blockAllocation=false;});while(!entered.load())std::this_thread::yield();
            Signal value;value.type.code=91;value.type.canonicalMask=0x8000000000000001ULL;store.offer(value);releaseAllocation=true;holder.join();};
        const bool ok=storeStatusFaults("signals-busy","rejectedTypeEvidence",actual,control,originalStore,prime,report);reports.push_back(std::move(report));if(!ok)return finish();
    }
    {
        BasicFacts<1> actual;BasicFactsStatusControl<1> control;BasicFactsOriginal<1> originalStore;json report;
        const bool ok=storeStatusFaults("facts-empty","rejectedOfferEvidence",actual,control,originalStore,[](auto&){},report);reports.push_back(std::move(report));if(!ok)return finish();
    }
    {
        BasicFacts<1> actual;BasicFactsStatusControl<1> control;BasicFactsOriginal<1> originalStore;json report;
        auto prime=[](auto& store){store.offer({json{{"model","fixture"}},json{{"type",1}},json{{"first",true}},"first"});
            store.offer({json{{"model","fixture"}},json{{"nested",json::array({1,2,3})}},json{{"privateSample","not-retained"}},"rejected-capacity"});};
        const bool ok=storeStatusFaults("facts-capacity","rejectedOfferEvidence",actual,control,originalStore,prime,report);reports.push_back(std::move(report));if(!ok)return finish();
    }
    {
        BasicFacts<1> actual;BasicFactsStatusControl<1> control;BasicFactsOriginal<1> originalStore;json report;
        auto prime=[](auto& store){entered=false;releaseAllocation=false;std::thread holder([&]{blockAllocation=true;(void)store.status();blockAllocation=false;});while(!entered.load())std::this_thread::yield();
            store.offer({json{{"model","fixture"}},json{{"type",91}},json{{"privateSample","not-retained"}},"rejected-busy"});releaseAllocation=true;holder.join();};
        const bool ok=storeStatusFaults("facts-busy","rejectedOfferEvidence",actual,control,originalStore,prime,report);reports.push_back(std::move(report));if(!ok)return finish();
    }
    for(const bool invalid:{false,true}){
        BasicFacts<1> actual;BasicFactsStatusControl<1> control;BasicFactsOriginal<1> originalStore;json report;
        auto prime=[&](auto& store){store.offer({json{{"model","fixture"}},json{{"type",1}},json::object(),"first"});
            store.offer({json{{"model","fixture"}},json{{"large",invalid?std::string(1,static_cast<char>(0xff)):std::string(4096,'x')}},json::object(),"category-\"\\\n"});};
        const bool ok=storeStatusFaults(invalid?"facts-invalid-utf8":"facts-large-prefix","rejectedOfferEvidence",actual,control,originalStore,prime,report);reports.push_back(std::move(report));if(!ok)return finish();
    }
    return finish();
}
#endif

#if defined(YMCC_JOURNAL_METADATA_FIXTURE) && __has_include("adaptation-status-controls.inc")
static int descriptorPrefixCases(bool baseline){
    using namespace ymcc::adaptlog;json reports=json::array();
    auto finish=[&]{trackAllocation=false;unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"reports",reports},{"trackedScope","status-only operator new request sizes;input/old full encoding prepared before tracking"},{"fullProductCpuOrRssProven",false}}.dump(2)<<'\n';return failed?3:0;};
    const auto measure=[&](const char* label,const json& model,const json& identity,const std::string& category,bool bound){
        BasicFacts<1> actual;BasicFactsOriginal<1> original;
        for(auto* store:{&actual}){store->offer({json{{"first",true}},json{{"kind",1}},json::object(),"first"});store->offer({model,identity,json{{"notRetained",true}},category});}
        original.offer({json{{"first",true}},json{{"kind",1}},json::object(),"first"});original.offer({model,identity,json{{"notRetained",true}},category});
        const auto expected=original.status();allocations=maximumRequest=requestedBytes=0;trackAllocation=true;const json status=actual.status();trackAllocation=false;
        const auto count=allocations,max=maximumRequest,total=requestedBytes;
        bool bounded=max<=8192;
        check(label,!bound||bounded);
        check("descriptor-stream-normal-status-exact-to-old",status==expected);
        const auto& witness=status["rejectedOfferEvidence"]["witnesses"][1];
        reports.push_back({{"case",label},{"inputCategoryBytes",category.size()},{"inputIdentityIsString",identity.is_string()},
            {"statusAllocations",count},{"largestNewRequest",max},{"totalRequestedBytes",total},{"fullStatus",status}});
        if(witness["state"]=="retained"){
            const auto encoded=Json{{"model",model},{"identity",identity},{"category",category}}.dump();const auto n=(std::min)(encoded.size(),size_t{2048});
            std::string expectedHex;constexpr char digits[]="0123456789abcdef";expectedHex.reserve(n*2);
            for(size_t i=0;i<n;++i){const auto b=static_cast<unsigned char>(encoded[i]);expectedHex+=digits[b>>4];expectedHex+=digits[b&15];}
            check("descriptor-stream-exact-length-prefix-completeness",witness["descriptorBytes"]==encoded.size()&&witness["prefixBytes"]==n&&witness["prefixHex"]==expectedHex&&witness["descriptorComplete"]==(n==encoded.size()));
        }else check("descriptor-stream-invalid-utf8-explicit",witness["state"]=="descriptor-serialization-failed");
        return (!bound||bounded)&&status==expected;
    };
    const json model={{"model","fixture"}};
    if(baseline){(void)measure("descriptor-stream-four-megabyte-output-temp-bounded",model,json{{"large",std::string(4*1024*1024,'x')}},"rejected",true);return finish();}
    for(const size_t size:{size_t{1},size_t{2048},size_t{65536},size_t{4*1024*1024}}){
        const auto name="descriptor-stream-identity-size-"+std::to_string(size);if(!measure(name.c_str(),model,json{{"large",std::string(size,'x')}},"rejected",true))return finish();
    }
    if(!measure("descriptor-stream-category-four-megabytes",model,json{{"small",true}},std::string(4*1024*1024,'z'),true))return finish();
    std::string controls;for(unsigned i=0;i<128;++i)controls+=static_cast<char>(i);
    controls+="中文/😀\xe2\x80\xa8\xe2\x80\xa9";
    if(!measure("descriptor-stream-all-ascii-controls-unicode-and-nul",json{{"escaped","\\\"\n"}},json{{"nested",json::array({nullptr,true,1.25,-7,json::binary({0,1,255})})}},controls,true))return finish();
    for(const auto& invalid:std::array<std::string,8>{std::string(1,static_cast<char>(0xff)),"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82","\xf0\x80\x80\xaf",std::string(4096,'x')+"\xff",std::string(4096,'x')+"\xf0\x9f\x98"}){
        if(!measure("descriptor-stream-invalid-category-even-after-prefix",model,json::object(),invalid,true))return finish();
    }
    if(!measure("descriptor-stream-invalid-identity-after-prefix",model,json{{"large",std::string(4096,'x')+"\xff"}},"rejected",true))return finish();
    // Deterministic mixed JSON/category corpus verifies byte identity without
    // changing the sampler,keys or actual default fixture selection.
    unsigned state=0x791341u;bool corpus=true;
    for(unsigned i=0;i<64;++i){state=state*1664525u+1013904223u;std::string category;
        for(unsigned n=0;n<(state&63u);++n){state=state*1664525u+1013904223u;category+=static_cast<char>(state&127u);}
        BasicFacts<1> actual;BasicFactsOriginal<1> original;
        const json identity={{"value",state},{"array",json::array({i,nullptr,false})}};
        actual.offer({model,json{{"first",true}},json::object(),"first"});original.offer({model,json{{"first",true}},json::object(),"first"});
        actual.offer({model,identity,json::object(),category});original.offer({model,identity,json::object(),category});
        corpus&=actual.status()==original.status();
    }
    check("descriptor-stream-deterministic64-exact-old-status",corpus);
    return finish();
}
#endif

#if defined(YMCC_JOURNAL_METADATA_FIXTURE) && __has_include("adaptation-status-controls.inc")
static int descriptorUtf8Cases(){
    using namespace ymcc::adaptlog;json cases=json::array();bool pass=true;unsigned valid=0,invalid=0;unsigned state=0x1756b21u;
    std::vector<std::string> values;values.reserve(600);
    for(unsigned byte=0;byte<256;++byte)values.push_back(std::string(1,static_cast<char>(byte)));
    const std::array<std::string,16> boundaries={"\xc2\x80","\xdf\xbf","\xe0\xa0\x80","\xe0\xbf\xbf","\xed\x9f\xbf","\xee\x80\x80","\xef\xbf\xbf","\xf0\x90\x80\x80","\xf4\x8f\xbf\xbf","\xed\xa0\x80","\xf4\x90\x80\x80","\xc1\xbf","\xe0\x9f\xbf","\xf0\x8f\xbf\xbf","\xf5\x80\x80\x80","\xe2\x28\xa1"};
    values.insert(values.end(),boundaries.begin(),boundaries.end());
    for(unsigned i=0;i<256;++i){std::string value;const auto count=i%33;
        for(unsigned n=0;n<count;++n){state=state*1664525u+1013904223u;value+=static_cast<char>(state>>24);}
        values.push_back(std::move(value));
    }
    const json model={{"model","fixture"}},first={{"first",true}},identity={{"candidate",true}};
    for(size_t i=0;i<values.size();++i){
        BasicFacts<1> actual;BasicFactsOriginal<1> original;
        actual.offer({model,first,json::object(),"first"});original.offer({model,first,json::object(),"first"});
        actual.offer({model,identity,json::object(),values[i]});original.offer({model,identity,json::object(),values[i]});
        const auto old=original.status();const auto candidate=actual.status();const bool same=old==candidate;pass&=same;
        const auto stateName=old["rejectedOfferEvidence"]["witnesses"][1]["state"].get<std::string>();valid+=stateName=="retained";invalid+=stateName=="descriptor-serialization-failed";
        if(!same&&cases.size()<8)cases.push_back({{"ordinal",i},{"bytes",values[i].size()},{"old",old},{"candidate",candidate}});
    }
    check("descriptor-stream-528-byte-utf8-cases-exact-old",pass&&values.size()==528&&valid>0&&invalid>0);
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failed},{"categoryCases",values.size()},{"oldValidCases",valid},{"oldInvalidCases",invalid},{"mismatches",cases},{"scope","single-byte256,boundaries16,deterministic256;not all Unicode strings"}}.dump(2)<<'\n';return failed?3:0;
}
#endif

int wmain(int argc,wchar_t** argv){
    if(argc<2||argc>3)return 2;const fs::path root=argv[1];fs::create_directories(root);
    if(argc==3&&std::wstring_view(argv[2])==L"--budget-tail")return journalTailCases(root);
    if(argc==3&&std::wstring_view(argv[2])==L"--budget")return journalBudgetCases(root);
    if(argc==3&&(std::wstring_view(argv[2])==L"--io"||std::wstring_view(argv[2])==L"--read"))return journalIoCases(root,std::wstring_view(argv[2])==L"--read");
#ifdef YMCC_JOURNAL_METADATA_FIXTURE
#if __has_include("adaptation-status-controls.inc")
    if(argc==3&&std::wstring_view(argv[2])==L"--prefix-faults")return adaptationStoreStatusCases(false,true);
    if(argc==3&&std::wstring_view(argv[2])==L"--prefix-utf8")return descriptorUtf8Cases();
    if(argc==3&&(std::wstring_view(argv[2])==L"--prefix"||std::wstring_view(argv[2])==L"--prefix-control"))return descriptorPrefixCases(std::wstring_view(argv[2])==L"--prefix-control");
    if(argc==3&&(std::wstring_view(argv[2])==L"--stores"||std::wstring_view(argv[2])==L"--stores-control"))return adaptationStoreStatusCases(std::wstring_view(argv[2])==L"--stores-control");
#endif
    if(argc==3&&std::wstring_view(argv[2])==L"--meta-fields")return journalMetadataFieldCases();
    if(argc==3&&std::wstring_view(argv[2])==L"--meta")return journalMetadataCases(root);
#endif
    const bool control=argc==3&&std::wstring_view(argv[2])==L"--control";
    using ymcc::adaptlog::Journal;using ymcc::adaptlog::RecordResult;
    const json model={{"model","fixture"}},identity={{"kind",1}},sample={{"original",true}};const std::string localTime="fixture";
    const auto file=root/L"journal.jsonl";
    auto record=[&](Journal& j,const fs::path& p,const json& id,const json& s){return j.recordResult(p,model,"fixture-type",id,s,localTime);};
    Journal create;createError=ERROR_NOT_ENOUGH_MEMORY;
    check("journal-create-failure-has-stage-evidence",record(create,file,identity,sample)==RecordResult::retry&&create.status().contains("failureEvidence"));
    if(control||!create.status().contains("failureEvidence")){
        const bool pass=checks[0]["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",pass?0:1}}.dump()<<'\n';return pass?0:3;
    }
    auto status=create.status();auto first=stage(status,"named-mutex-create");
    check("journal-original-create-error8-and-legacy-counter-retained",first["first"]["code"]==8&&first["first"]["codeDomain"]=="win32-last-error"&&first["observations"]==1&&status["writeFailures"]==1);
    allocations=0;trackAllocation=true;for(unsigned i=0;i<50000;++i)(void)record(create,file,identity,sample);trackAllocation=false;
    status=create.status();
    check("journal-backoff-50000-no-allocation-or-extra-failure",allocations==0&&stage(status,"retry-backoff")["observations"]==50000&&status["writeFailures"]==1&&stage(status,"retry-backoff")["first"]["codeKnown"]==false);
    tick+=5001;createError=ERROR_OUTOFMEMORY;(void)record(create,file,identity,sample);status=create.status();createError=0;
    check("journal-first-error-not-overwritten-by-later14",stage(status,"named-mutex-create")["observations"]==2&&stage(status,"named-mutex-create")["first"]==first["first"]&&status["writeFailures"]==2);
    Journal timeout;waitMode=WAIT_TIMEOUT;const auto timeoutResult=record(timeout,file,identity,sample);status=timeout.status();
    check("journal-timeout-is-defer-with-no-fabricated-api-error",timeoutResult==RecordResult::retry&&status["writeFailures"]==0&&stage(status,"named-mutex-wait")["first"]["waitStatus"]==258&&stage(status,"named-mutex-wait")["first"]["codeKnown"]==false);
    Journal waitFailed;waitMode=WAIT_FAILED;const auto waitResult=record(waitFailed,file,identity,sample);status=waitFailed.status();waitMode=WAIT_OBJECT_0;
    check("journal-failed-wait-keeps5-before-handle-close",waitResult==RecordResult::retry&&status["writeFailures"]==0&&stage(status,"named-mutex-wait")["first"]["code"]==5&&stage(status,"named-mutex-wait")["first"]["waitStatus"]==WAIT_FAILED);
    Journal missing;const auto missingResult=record(missing,root/L"missing"/L"child.jsonl",identity,sample);status=missing.status();
    check("journal-real-missing-parent-stream-error-remains-code-unknown",missingResult==RecordResult::retry&&status["writeFailures"]==1&&stage(status,"file-open-append")["firstState"]=="retained"&&stage(status,"file-open-append")["first"]["codeKnown"]==false);
    {std::ofstream f(file);f<<"{}\n";}
    HANDLE deny=CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Journal blocked;const auto blockedResult=record(blocked,file,identity,sample);status=blocked.status();if(deny!=INVALID_HANDLE_VALUE)CloseHandle(deny);
    check("journal-real-exclusive-file-read-refusal-has-original-stage",deny!=INVALID_HANDLE_VALUE&&blockedResult==RecordResult::retry&&status["writeFailures"]==1&&stage(status,"file-open-read")["firstState"]=="retained"&&stage(status,"file-open-read")["first"]["codeKnown"]==false);
    Journal allocation;const auto allocationFile=root/L"allocation.jsonl";failAllocation=true;const auto allocationResult=record(allocation,allocationFile,identity,sample);status=allocation.status();
    check("journal-real-allocation-exception-keeps-unknown-code",allocationResult==RecordResult::retry&&stage(status,"exception")["first"]["exceptionKind"]=="allocation-exception"&&stage(status,"exception")["first"]["codeKnown"]==false&&status["writeFailures"]==1);
    Journal malformed;json bad={{"invalid",std::string(1,static_cast<char>(0xff))}};const auto malformedResult=record(malformed,root/L"malformed.jsonl",identity,bad);status=malformed.status();
    check("journal-actual-json-exception-is-standard-not-last-error",malformedResult==RecordResult::retry&&stage(status,"exception")["first"]["exceptionKind"]=="standard-exception"&&stage(status,"exception")["first"]["codeKnown"]==false);
    Journal limit(8192,256,16);const auto limitResult=record(limit,root/L"limit.jsonl",identity,sample);status=limit.status();
    check("journal-budget-omission-is-not-failure-or-file-write",limitResult==RecordResult::omitted&&status["budgetOmissions"]==1&&status["writeFailures"]==0&&observations(status)==0&&!fs::exists(root/L"limit.jsonl"));
    Journal normal;const auto normalFile=root/L"normal.jsonl";const auto saved=record(normal,normalFile,identity,sample);const auto before=bytes(normalFile);const auto duplicate=record(normal,normalFile,identity,sample);status=normal.status();
    check("journal-original-save-and-exact-duplicate-preserved",saved==RecordResult::saved&&duplicate==RecordResult::saved&&status["committedThisProcess"]==1&&status["writeFailures"]==0&&observations(status)==0&&bytes(normalFile)==before);
    Journal busy;entered=false;releaseAllocation=false;
    std::thread holder([&]{blockAllocation=true;(void)busy.status();blockAllocation=false;});while(!entered.load())std::this_thread::yield();
    allocations=0;trackAllocation=true;const auto busyResult=record(busy,file,identity,sample);trackAllocation=false;releaseAllocation=true;holder.join();status=busy.status();
    check("journal-local-lock-busy-is-allocation-free-defer",busyResult==RecordResult::retry&&allocations==0&&status["writeFailures"]==0&&stage(status,"local-lock-busy")["observations"]==1);
    check("journal-fixed15-nonatomic-first-stage-reports-not-history",status["failureEvidence"]["maximumStages"]==15&&status["failureEvidence"]["stages"].size()==15&&status["failureEvidence"]["snapshotAtomicWithCounters"]==false&&status["failureEvidence"]["allErrorKindsRetained"]==false);
    DWORD baseline=0,finalHandles=0;const bool baselineOk=GetProcessHandleCount(GetCurrentProcess(),&baseline)!=FALSE;
    {Journal returned;for(unsigned i=0;i<20;++i)(void)record(returned,normalFile,identity,sample);}
    const bool finalOk=GetProcessHandleCount(GetCurrentProcess(),&finalHandles)!=FALSE;
    check("journal-original-mutex-and-file-handles-return",baselineOk&&finalOk&&baseline==finalHandles);
    {std::ofstream out(root/L"actual-status.json");out<<status.dump(2)<<'\n';}
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failed},{"handlesBefore",baseline},{"handlesAfter",finalHandles},{"clockAndFailedKernelLeavesControlled",true},{"streamIoAndJsonFailuresActual",true},{"productOrDeviceActions",false}}.dump(2)<<'\n';return failed?3:0;
}
