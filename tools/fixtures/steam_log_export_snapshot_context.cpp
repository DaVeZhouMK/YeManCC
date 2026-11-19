#include <cstdlib>
#include <new>
static bool contextTrack=false,contextFaultTriggered=false;static unsigned long long contextAllocations=0,contextFailAt=0;
void* operator new(std::size_t size){if(contextTrack){++contextAllocations;if(contextFailAt&&contextAllocations==contextFailAt){contextFaultTriggered=true;throw std::bad_alloc();}}
    if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    const auto input=root/L"native-lifecycle.log";put(input,"{\"event\":\"original\"}\n");
    json observed=json::array();snapshotAttemptObserver=[&](const wchar_t* directory){std::ifstream f(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json",std::ios::binary);json row;f>>row;observed.push_back(std::move(row));};
    const json operation={{"schema",1},{"id","37-42-3"},{"writerPid",37},{"writerProcessCreation100ns","9007199254740993"},
        {"ordinal","3"},{"startedTickMs","42"},{"scope","admitted-ordinary-log-export;diagnostic-correlation-only"}};
#ifdef YMCC_SNAPSHOT_CONTEXT_OLD_CONTROL
    const auto first=createTarArchiveWithRetry((root/L"first.zip").wstring(),{input.wstring()});
#else
    const auto first=createTarArchiveWithRetry((root/L"first.zip").wstring(),{input.wstring()},{},{},nullptr,&operation);
#endif
    const bool contextPresent=observed.size()==1&&observed[0].contains("archiveAttemptContext");
    check("actual staged snapshot retains original export context",first.ok&&contextPresent);
    auto finish=[&]{unsigned failures=0;for(const auto& q:checks)failures+=!q["pass"].get<bool>();
        std::cout<<json{{"selection","snapshot-context"},{"checks",checks},{"failures",failures},{"observed",observed},{"tarCalls",tarCalls},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;};
#ifdef YMCC_SNAPSHOT_CONTEXT_OLD_CONTROL
    return finish();
#else
    if(!contextPresent)return finish();const auto original=observed[0]["archiveAttemptContext"];
    check("snapshot operation values and 64bit birth remain exact",original["exportOperation"]==operation&&original["attempt"]==1&&original["maxAttempts"]==2);
    failTarChildren=1;const auto before=observed.size();const auto retry=createTarArchiveWithRetry((root/L"retry.zip").wstring(),{input.wstring()},{},{},nullptr,&operation);
    check("original two attempts retain distinct staging attempt numbers",retry.ok&&retry.attempts==2&&observed.size()==before+2&&observed[before]["archiveAttemptContext"]["attempt"]==1&&observed.back()["archiveAttemptContext"]["attempt"]==2);
    check("both retry receipts preserve the same admitted operation",observed[before]["archiveAttemptContext"]["exportOperation"]==operation&&observed.back()["archiveAttemptContext"]["exportOperation"]==operation);
    const auto legacy=createTarArchiveWithRetry((root/L"legacy.zip").wstring(),{input.wstring()});
    check("legacy archive caller keeps absent context unknown",legacy.ok&&!observed.back().contains("archiveAttemptContext")&&legacy.snapshotAttemptContextEvidenceBuildFailures==0);
    auto huge=operation;huge["id"]=std::string(100000,'x');const auto rejected=createTarArchiveWithRetry((root/L"context-refused.zip").wstring(),{input.wstring()},{},{},nullptr,&huge);
    check("unsupported context does not abort a valid archive or become copy failure",rejected.ok&&rejected.snapshotAttemptContextEvidenceBuildFailures==1&&rejected.snapshotCopyFailureObservations==0&&!observed.back().contains("archiveAttemptContext"));
    json extra=operation;extra["privatePayload"]=std::string(100000,'p');json target=json::object();
    check("context copies only fixed operation fields",retainArchiveSnapshotAttemptContext(target,extra,2)&&target["archiveAttemptContext"]["exportOperation"]==operation);
    json measured=json::object();contextAllocations=0;contextFailAt=0;contextTrack=true;const bool measuredOk=retainArchiveSnapshotAttemptContext(measured,operation,1);contextTrack=false;const auto allocations=contextAllocations;
    check("bounded context allocation census succeeds",measuredOk&&allocations>0&&allocations<1024);
    bool faults=true;for(unsigned long long n=1;n<=allocations;++n){json row=json::object();contextAllocations=0;contextFailAt=n;contextFaultTriggered=false;contextTrack=true;
        const bool ok=retainArchiveSnapshotAttemptContext(row,operation,1);contextTrack=false;contextFailAt=0;faults&=contextFaultTriggered&&!ok&&!row.contains("archiveAttemptContext");}
    check("every single context allocation failure returns without partial context",faults);
    json unchanged={{"kept",true}};const auto old=unchanged;
    check("invalid attempt leaves receipt untouched",!retainArchiveSnapshotAttemptContext(unchanged,operation,0)&&!retainArchiveSnapshotAttemptContext(unchanged,operation,3)&&unchanged==old);
    bool clean=true;for(const auto& file:fspath::directory_iterator(root)){const auto name=file.path().filename().wstring();if(name.find(L".staging-")!=std::wstring::npos||name.find(L".attempt-")!=std::wstring::npos)clean=false;}
    check("all staging and attempt temporaries are returned",clean&&fspath::exists(input));
    snapshotAttemptObserver={};return finish();
#endif
}
