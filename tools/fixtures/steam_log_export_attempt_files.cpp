#include <cstdlib>
#include <new>
#include "sleep_export_diagnostics.h"
static bool evidenceTrack=false,evidenceFault=false;static unsigned long long evidenceCount=0,evidenceFailAt=0;
void* operator new(std::size_t n){if(evidenceTrack){++evidenceCount;if(evidenceFailAt&&evidenceCount==evidenceFailAt){evidenceFault=true;throw std::bad_alloc();}}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#if defined(YMCC_CAPTURE_FINALIZATION_ONLY)
#include "log_capture_finalization.inc"
#elif defined(YMCC_ARCHIVE_PROMOTION_ONLY)
#include "log_export_promotion.inc"
#elif defined(YMCC_ARCHIVE_CLEANUP_RETURN_ONLY)
#include "log_export_cleanup_returns.inc"
#elif defined(YMCC_ARCHIVE_CLEANUP_ONLY)
#include "log_export_cleanup.inc"
#elif defined(YMCC_ARCHIVE_HANDLE_CLOSE_ONLY)
#include "log_export_handle_close.inc"
#elif defined(YMCC_ARCHIVE_SNAPSHOT_WORK_ONLY)
#include "log_export_snapshot_work.inc"
#elif defined(YMCC_ARCHIVE_INDEX_WORK_ONLY)
#include "log_export_index_work.inc"
#elif defined(YMCC_ARCHIVE_TIMING_ONLY)
static void appendTimingToTerminal(json& exportTerminal,const TarArchiveRetryResult& packed){
/* ACTUAL_ARCHIVE_TIMING_TERMINAL */
}
static json savedAttemptSnapshots=json::array();
#ifdef YMCC_ARCHIVE_TIMING_RESOURCE_ONLY
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    DWORD cold=0,hash1=0,hash2=0,child1=0,child2=0,before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&cold);
    const auto input=root/L"native-lifecycle.log";put(input,"{\"event\":\"resource-fixture\"}\n");
    const auto h1=sha256File(input.wstring());GetProcessHandleCount(GetCurrentProcess(),&hash1);
    const auto h2=sha256File(input.wstring());GetProcessHandleCount(GetCurrentProcess(),&hash2);
    const auto a=productionRunCapture(L"\"C:\\Windows\\System32\\tar.exe\" --version",10000);GetProcessHandleCount(GetCurrentProcess(),&child1);
    const auto b=productionRunCapture(L"\"C:\\Windows\\System32\\tar.exe\" --version",10000);GetProcessHandleCount(GetCurrentProcess(),&child2);
    check("own-hash-and-two-finite-owned-child-initialization-counts-stabilize",h1.size()==64&&h1==h2&&hash1==hash2&&a.ran&&a.exitCode==0&&b.ran&&b.exitCode==0&&child1==child2);
    GetProcessHandleCount(GetCurrentProcess(),&before);
    ymcc::logexport::SteamLogs absent;absent.root=root/L"own-no-steam";absent.status="not-installed-or-unavailable";absent.source="fixture";
    const auto success=createTarArchiveWithRetry((root/L"success.zip").wstring(),{input.wstring()}, {}, {}, &absent);
    failTarChildren=1;const auto retry=createTarArchiveWithRetry((root/L"retry.zip").wstring(),{input.wstring()}, {}, {}, &absent);
    const auto early=createTarArchiveWithRetry((root/L"missing.zip").wstring(),{(root/L"missing-source.log").wstring()});
    put(root/L"existing.zip","existing-user-bytes");const auto collision=createTarArchiveWithRetry((root/L"existing.zip").wstring(),{input.wstring()});
    GetProcessHandleCount(GetCurrentProcess(),&after);
    check("only-resource-followup-all-four-original-exits-return-to-warm-handle-count",success.ok&&retry.ok&&retry.attempts==2&&!early.ok&&!collision.ok&&collision.attempts==1&&before==after);
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"selection","attempt-files"},{"archivePhase","timing-resource"},{"checks",checks},{"failures",failed},{"coldHandles",cold},{"afterFirstHashHandles",hash1},{"afterSecondHashHandles",hash2},{"afterFirstChildHandles",child1},{"afterSecondChildHandles",child2},{"handlesBefore",before},{"handlesAfter",after},{"tarWrapperCalls",tarCalls},{"resourceWorkloadArchiveCalls",4},{"resourceWorkloadRepeatedBecauseOriginalColdCheckFailed",true},{"passedTimingAssertionsReexecuted",false},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<std::endl;return failed?2:0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 4;}}
#else

int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    DWORD coldHandles=0,hashFirstHandles=0,hashSecondHandles=0,handlesBefore=0,handlesAfter=0;
    GetProcessHandleCount(GetCurrentProcess(),&coldHandles);
    const auto warmupFile=root/L"private-hash-warmup.txt";put(warmupFile,"owned-hash-initialization");
    const auto firstHash=sha256File(warmupFile.wstring());GetProcessHandleCount(GetCurrentProcess(),&hashFirstHandles);
    const auto secondHash=sha256File(warmupFile.wstring());GetProcessHandleCount(GetCurrentProcess(),&hashSecondHandles);
    if(firstHash.size()!=64||firstHash!=secondHash||hashFirstHandles!=hashSecondHandles)throw std::runtime_error("own hash warmup unstable");
    const auto warmChild1=productionRunCapture(L"\"C:\\Windows\\System32\\tar.exe\" --version",10000);
    const auto warmChild2=productionRunCapture(L"\"C:\\Windows\\System32\\tar.exe\" --version",10000);
    if(!warmChild1.ran||warmChild1.exitCode||!warmChild2.ran||warmChild2.exitCode)throw std::runtime_error("own child warmup failed");
    GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const auto input=root/L"native-lifecycle.log";put(input,"{\"event\":\"fixture-original\"}\n");
    ymcc::logexport::SteamLogs absent;absent.root=root/L"own-no-steam";absent.status="not-installed-or-unavailable";absent.source="fixture";
    snapshotAttemptObserver=[&](const wchar_t* directory){
        std::ifstream stream(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");json value;stream>>value;savedAttemptSnapshots.push_back(value);Sleep(80);
    };
    const auto success=createTarArchiveWithRetry((root/L"success.zip").wstring(),{input.wstring()}, {}, {}, &absent);
    const auto timing=archiveAttemptTimingEvidence(success);
    check("actual-archive-has-bounded-per-attempt-step-timing",success.ok&&success.attempts==1&&timing.is_object()&&timing["attemptsObserved"]==1);
#ifndef YMCC_ARCHIVE_TIMING_OLD_ONLY
    const auto& first=timing["attempts"][0];const auto& phases=first["completedStepElapsedMs"];
    check("successful-attempt-records-seven-entered-steps-with-no-retry-delay",first["attemptBodyReturned"]==true&&phases.size()==7&&!phases.contains("retryDelay")&&phases.contains("snapshotEvidenceAndIndex")&&phases["tarProcess"].get<unsigned long long>()>=32);
    const auto& pre=savedAttemptSnapshots[0]["archiveAttemptTiming"]["attempts"][0];
    check("pre-snapshot-receipt-keeps-only-completed-two-steps-and-null-total",pre["attemptBodyReturned"]==false&&pre["totalElapsedMs"].is_null()&&pre["completedStepElapsedMs"].size()==2&&!pre["completedStepElapsedMs"].contains("tarProcess"));
    json terminal={{"ok",success.ok}};appendTimingToTerminal(terminal,success);
    check("actual-optional-terminal-block-keeps-final-attempt-timing",terminal["archiveAttemptTiming"]==timing&&terminal["ok"]==true);
    evidenceTrack=true;evidenceCount=0;evidenceFailAt=0;auto measured=archiveAttemptTimingEvidence(success);evidenceTrack=false;
    const auto census=evidenceCount;bool safe=census>0&&census<1024&&measured==timing;
    for(unsigned long long i=1;i<=census;++i){evidenceCount=0;evidenceFailAt=i;evidenceFault=false;evidenceTrack=true;auto omitted=archiveAttemptTimingEvidence(success);evidenceTrack=false;evidenceFailAt=0;safe=safe&&evidenceFault&&omitted.is_null();}
    check("each-optional-timing-construction-allocation-failure-returns-null",safe);
    json terminalProbe={{"ok",true}};evidenceCount=0;evidenceFailAt=0;evidenceTrack=true;appendTimingToTerminal(terminalProbe,success);evidenceTrack=false;const auto terminalCensus=evidenceCount;
    bool safePublish=terminalCensus>census&&terminalCensus<1024;
    for(unsigned long long i=1;i<=terminalCensus;++i){json item={{"ok",true}};evidenceCount=0;evidenceFailAt=i;evidenceFault=false;evidenceTrack=true;bool escaped=false;try{appendTimingToTerminal(item,success);}catch(...){escaped=true;}evidenceTrack=false;evidenceFailAt=0;safePublish=safePublish&&!escaped&&evidenceFault&&item["ok"]==true;}
    check("each-optional-terminal-publication-allocation-failure-keeps-original-result",safePublish);
    const auto callsBefore=tarCalls;failTarChildren=1;
    const auto retry=createTarArchiveWithRetry((root/L"retry.zip").wstring(),{input.wstring()}, {}, {}, &absent);const auto retryTiming=archiveAttemptTimingEvidence(retry);
    check("original-two-attempt-limit-and-retry-delay-are-measured-separately",retry.ok&&retry.attempts==2&&tarCalls==callsBefore+2&&retryTiming["attempts"].size()==2&&retryTiming["attempts"][0]["completedStepElapsedMs"]["retryDelay"].get<unsigned long long>()>=32&&!retryTiming["attempts"][1]["completedStepElapsedMs"].contains("retryDelay"));
    const auto& secondPre=savedAttemptSnapshots.back()["archiveAttemptTiming"]["attempts"];
    check("second-snapshot-retains-first-final-timing-with-current-partial-timing",secondPre.size()==2&&secondPre[0]==retryTiming["attempts"][0]&&secondPre[1]["attemptBodyReturned"]==false&&secondPre[1]["totalElapsedMs"].is_null());
    snapshotAttemptObserver={};const auto beforeEarly=tarCalls;
    const auto early=createTarArchiveWithRetry((root/L"missing.zip").wstring(),{(root/L"missing-source.log").wstring()});const auto earlyTiming=archiveAttemptTimingEvidence(early);
    bool earlyScopes=true;for(const auto& row:earlyTiming["attempts"]){const auto& done=row["completedStepElapsedMs"];earlyScopes=earlyScopes&&done.contains("sourceSnapshot")&&done.contains("attemptCleanup")&&!done.contains("snapshotEvidenceAndIndex")&&!done.contains("commandPreparation")&&!done.contains("tarProcess");}
    check("early-copy-failure-does-not-invent-zero-index-command-or-tar-steps",!early.ok&&early.attempts==2&&tarCalls==beforeEarly&&earlyScopes);
    const auto collision=root/L"existing.zip";put(collision,"existing-user-archive");const auto refused=createTarArchiveWithRetry(collision.wstring(),{input.wstring()});const auto refusedTiming=archiveAttemptTimingEvidence(refused);
    std::ifstream original(collision);std::string kept;std::getline(original,kept);original.close();
    check("real-promotion-collision-keeps-one-timing-and-existing-final-bytes",!refused.ok&&refused.attempts==1&&refusedTiming["attempts"].size()==1&&kept=="existing-user-archive");
    bool totals=true;for(const auto* value:{&timing,&retryTiming,&earlyTiming,&refusedTiming})for(const auto& row:(*value)["attempts"]){unsigned long long sum=0;for(const auto& part:row["completedStepElapsedMs"].items())sum+=part.value().get<unsigned long long>();totals=totals&&row["attemptBodyReturned"]==true&&sum<=row["totalElapsedMs"].get<unsigned long long>();}
    check("sequential-completed-step-times-fit-each-original-attempt-total",totals);
    const TarArchiveRetryResult unstarted;const auto none=archiveAttemptTimingEvidence(unstarted);
    check("unstarted-archive-has-no-attempts-or-fabricated-zero-phases",none["attemptsObserved"]==0&&none["attempts"].empty());
    std::ifstream retained(input);std::string retainedBytes;std::getline(retained,retainedBytes);retained.close();
    check("logging-keeps-original-source-bytes-and-no-adjacent-attempt-zip",retainedBytes=="{\"event\":\"fixture-original\"}"&&std::none_of(fspath::directory_iterator(root),fspath::directory_iterator{},[](const auto& item){return item.path().filename().wstring().find(L".zip.attempt-")!=std::wstring::npos;}));
#endif
    GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);check("all-owned-source-output-and-child-handles-return",handlesBefore==handlesAfter);
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    json result={{"coldHandlesBeforeHash",coldHandles},{"afterFirstHashHandles",hashFirstHandles},{"afterSecondHashHandles",hashSecondHandles},{"hashWarmupSucceeded",true},{"selection","attempt-files"},{"archivePhase","timing"},{"checks",checks},{"failures",failed},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"successTiming",timing},{"hardwareActions",0},{"productOrSteamActions",0}};
#ifndef YMCC_ARCHIVE_TIMING_OLD_ONLY
    result["retryTiming"]=retryTiming;result["earlyTiming"]=earlyTiming;result["refusedTiming"]=refusedTiming;result["snapshotObservations"]=savedAttemptSnapshots;result["terminal"]=terminal;result["timingStateBytes"]=sizeof(success.attemptTiming);result["optionalAllocationCensus"]=census;result["terminalAllocationCensus"]=terminalCensus;
#endif
    std::cout<<result.dump(2)<<std::endl;return failed?2:0;
}catch(const std::exception& error){evidenceTrack=false;evidenceFailAt=0;std::cerr<<error.what()<<std::endl;return 4;}}


#endif
#else
int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--bounded-child"){Sleep(30000);return 0;}
    if(argc==2&&std::wstring(argv[1])==L"--archive-child-failure"){
        constexpr char marker[]="owned archive failure\n";DWORD written=0;
        return WriteFile(GetStdHandle(STD_ERROR_HANDLE),marker,sizeof(marker)-1,&written,nullptr)&&written==sizeof(marker)-1?17:18;
    }
    if(argc==2&&std::wstring(argv[1])==L"--archive-empty-child-failure")return 17;
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    auto finish=[&]{unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"selection","attempt-files"},{"checks",checks},{"failures",failures},{"tarCalls",tarCalls},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;};
#ifdef YMCC_ATTEMPT_BUDGET_OLD_CONTROL
    check("first query has10000ms budget while cooldown remains60000ms",ymcc::sleepexport::timeoutMs==10000);return finish();
#endif
#ifdef YMCC_ATTEMPT_FILES_OLD_CONTROL
    if(root.filename()==L"budget-control"){check("first report query budget is10000ms",ymcc::sleepexport::timeoutMs==10000);return finish();}
#endif
    const auto input=root/L"native-lifecycle.log";put(input,"{\"event\":\"original\"}\n");bool extraZip=false;json observed=json::array();
    snapshotAttemptObserver=[&](const wchar_t* directory){
        for(const auto& p:fspath::directory_iterator(root))if(p.path().filename().wstring().find(L".zip.attempt-")!=std::wstring::npos)extraZip=true;
        std::ifstream stream(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");json value;stream>>value;observed.push_back(value);};
    const auto first=createTarArchiveWithRetry((root/L"one.zip").wstring(),{input.wstring()});
    check("packing exposes no second ZIP beside the final destination",first.ok&&!extraZip);
#ifdef YMCC_ATTEMPT_FILES_OLD_CONTROL
    return finish();
#else
    check("successful single attempt keeps original budget and cleanup",first.ok&&first.attempts==1&&first.cleanupOk&&tarInvocations.back().budget==300000);
    HANDLE blocker=INVALID_HANDLE_VALUE;fspath::path blockedOutput;bool locked=false;unsigned completed=0;
    tarCompletionObserver=[&](HANDLE output,const wchar_t*){
        if(++completed!=1)return;wchar_t path[32768]{};const auto n=GetFinalPathNameByHandleW(output,path,32768,FILE_NAME_NORMALIZED);
        if(n&&n<32768){blockedOutput=path;blocker=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);locked=blocker!=INVALID_HANDLE_VALUE;}};
    snapshotAttemptObserver=[&](const wchar_t* directory){
        if(blocker!=INVALID_HANDLE_VALUE){CloseHandle(blocker);blocker=INVALID_HANDLE_VALUE;}
        for(const auto& p:fspath::directory_iterator(root))if(p.path().filename().wstring().find(L".zip.attempt-")!=std::wstring::npos)extraZip=true;
        std::ifstream stream(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");json value;stream>>value;observed.push_back(value);};
    const auto retryStarted=GetTickCount64();
    const auto retry=createTarArchiveWithRetry((root/L"retry.zip").wstring(),{input.wstring()});
    const auto retryElapsed=GetTickCount64()-retryStarted;
    if(blocker!=INVALID_HANDLE_VALUE){CloseHandle(blocker);blocker=INVALID_HANDLE_VALUE;}
    tarCompletionObserver={};snapshotAttemptObserver={};
    check("actual sharing refusal exercises only the original two attempts",locked&&retry.ok&&retry.attempts==2&&completed==2);
    const auto previous=archivePreviousAttemptEvidence(retry);
    check("previous promotion failure retains exact typed error",previous.is_object()&&previous["attempt"]==1&&previous["reason"]=="promote-failed"&&(previous["win32Error"]==ERROR_SHARING_VIOLATION||previous["win32Error"]==ERROR_ACCESS_DENIED));
    check("cleanup refusal stays separate from final archive success",!retry.cleanupOk&&previous["cleanupOk"]==false&&previous["temporaryRemoveError"].get<unsigned>()!=0&&previous["stagingRemoveError"].get<unsigned>()!=0);
    check("second staged snapshot carries the original failed attempt",observed.size()==3&&observed.back()["previousArchiveAttempt"]==previous);
    check("retry leaves no additional ZIP in the destination directory",!extraZip&&fspath::exists(root/L"retry.zip")&&blockedOutput.extension()==L".tmp");
    evidenceTrack=true;evidenceCount=0;evidenceFailAt=0;auto measured=archivePreviousAttemptEvidence(retry);evidenceTrack=false;const auto count=evidenceCount;
    check("previous attempt witness has bounded successful allocation census",measured==previous&&count>0&&count<512);
    bool safe=true;for(unsigned long long i=1;i<=count;++i){evidenceCount=0;evidenceFailAt=i;evidenceFault=false;evidenceTrack=true;auto value=archivePreviousAttemptEvidence(retry);evidenceTrack=false;evidenceFailAt=0;safe&=evidenceFault&&value.is_null();}
    check("every single witness allocation failure remains optional",safe&&archivePreviousAttemptEvidence(first).is_null());
    if(!blockedOutput.empty()){std::error_code ec;fspath::remove_all(blockedOutput.parent_path(),ec);check("fixture releases its held reader and exact failed-attempt directory",!ec&&!fspath::exists(blockedOutput));}
    const auto collision=root/L"existing.zip";put(collision,"existing-user-bytes");const auto refused=createTarArchiveWithRetry(collision.wstring(),{input.wstring()});
    std::ifstream original(collision);std::string kept;std::getline(original,kept);original.close();
    check("existing completed user archive is never replaced or retried",!refused.ok&&refused.attempts==1&&kept=="existing-user-bytes");
    wchar_t self[32768]{};const auto length=GetModuleFileNameW(nullptr,self,32768);
    const auto childCommand=L"\""+std::wstring(self,length)+L"\"";
    const auto started=GetTickCount64();
    const auto child=productionRunCapture(childCommand+L" --bounded-child",ymcc::sleepexport::timeoutMs);
    check("actual owned child receives the new10000ms deadline",child.ran&&child.exitCode==ERROR_TIMEOUT&&std::string(child.completion)=="deadline"&&GetTickCount64()-started>=10000&&GetTickCount64()-started<15000);
    const auto controlDir=root/L"child-failure";fspath::create_directory(controlDir);
    const auto finalZip=controlDir/L"recovered.zip";
    const auto beforeChildRetry=tarCalls;
    failTarChildren=1;forcedArchiveChildCommand=childCommand+L" --archive-child-failure";
    const auto recovered=createTarArchiveWithRetry(finalZip.wstring(),{input.wstring()});
    forcedArchiveChildCommand.clear();
    const auto recoveredTiming=archiveAttemptTimingEvidence(recovered);
    const auto& attempts=recoveredTiming.at("attempts");
    const auto& failure=attempts.at(0).at("archiveChildFailure");
    check("real archive child failure retains the original stderr length through retry",
        recovered.ok&&recovered.attempts==2&&tarCalls==beforeChildRetry+2&&failure==json({
            {"schema",1},{"backend","tar"},{"completion","child-exit"},{"ran",true},{"captureFinalizationEntered",true},
            {"wrapperExitCode",17},{"win32Error",0},{"captureChannel","stderr"},{"retainedOutputBytes",22}})&&
        !attempts.at(1).contains("archiveChildFailure"));
    check("real archive child retry leaves only its promoted zip",std::distance(fspath::directory_iterator(controlDir),fspath::directory_iterator())==1&&fspath::is_regular_file(finalZip));
    const auto empty=productionRunCapture(childCommand+L" --archive-empty-child-failure");
    const auto unstarted=productionRunCapture(L"\""+(controlDir/L"absent.exe").wstring()+L"\"");
    json emptyAttempt=json::object(),unstartedAttempt=json::object();
    ymcc::logexport::ArchiveChildFailure emptyEvidence,unstartedEvidence;
    emptyEvidence.observe(true,empty);emptyEvidence.write(emptyAttempt);
    unstartedEvidence.observe(false,unstarted);unstartedEvidence.write(unstartedAttempt);
    const auto& emptyRecord=emptyAttempt.at("archiveChildFailure");
    const auto& unstartedRecord=unstartedAttempt.at("archiveChildFailure");
    check("real empty capture differs from unstarted capture",emptyRecord.at("retainedOutputBytes")==0&&
        emptyRecord.at("captureFinalizationEntered")==true&&emptyRecord.at("captureChannel")=="stdout-and-stderr"&&
        unstartedRecord.at("retainedOutputBytes").is_null()&&unstartedRecord.at("wrapperExitCode").is_null()&&
        unstartedRecord.at("completion")=="setup-create-process");
    const auto carryDir=root/L"child-failure-carry";fspath::create_directory(carryDir);
    const auto failedZip=carryDir/L"failed.zip";
    failTarChildren=2;
    snapshotAttemptObserver=[&](const wchar_t*){
        forcedArchiveChildCommand=childCommand+(failTarChildren==2?L" --archive-child-failure":L" --archive-empty-child-failure");
    };
    const auto failedStarted=GetTickCount64();
    const auto failed=createTarArchiveWithRetry(failedZip.wstring(),{input.wstring()});
    const auto failedElapsed=GetTickCount64()-failedStarted;
    snapshotAttemptObserver={};forcedArchiveChildCommand.clear();
    check("two independently failed children leave no final ZIP",!failed.ok&&failed.attempts==2&&!fspath::exists(failedZip));
    const json operation={{"schema",1},{"scope","admitted-ordinary-log-export;diagnostic-correlation-only"},
        {"id","7012-1000-1"},{"writerPid",7012},{"writerProcessCreation100ns","134394066260992255"},
        {"ordinal","1"},{"startedTickMs","1000"}};
    json failedRow={{"ok",failed.ok},{"cleanupOk",failed.cleanupOk},{"attempts",failed.attempts},
        {"bodyReturned",true},{"exportElapsedMs",failedElapsed},{"archiveElapsedMs",failedElapsed},
        {"terminalStage","archive-return"},{"exportMode","full"},{"phaseTimingBuildFailures",0u},
        {"win32Error",failed.win32Error},{"completion",failed.completion},{"reason",failed.reason},
        {"exportOperation",operation},{"archiveAttemptTiming",archiveAttemptTimingEvidence(failed)}};
    auto successor=operation;successor["id"]="7012-2000-2";successor["ordinal"]="2";successor["startedTickMs"]="2000";
    const json failedReturn={{"ok",false}};
    const auto carryTerminal=[&](const json& row){
        json slot,availability={{"exportOperation",successor}};
        ymcc::logexport::retainPriorOrdinaryTerminal(slot,row,&failedReturn);
        ymcc::logexport::addPriorOrdinaryTerminal(availability,slot);
        return availability;
    };
    const auto availability=carryTerminal(failedRow);
    const auto& compact=availability.at("priorOrdinaryExportTerminal").at("archiveAttemptSteps");
    const auto& failures=compact.at("childFailures");
    check("preceding terminal carries only two exact unsuccessful child records",
        failures.size()==2&&failures[0].at("attempt")==1&&failures[1].at("attempt")==2&&
        failures[0].at("archiveChildFailure")==failedRow["archiveAttemptTiming"]["attempts"][0]["archiveChildFailure"]&&
        failures[1].at("archiveChildFailure")==failedRow["archiveAttemptTiming"]["attempts"][1]["archiveChildFailure"]&&
        failures[0]["archiveChildFailure"]["retainedOutputBytes"]==22&&failures[1]["archiveChildFailure"]["retainedOutputBytes"]==0);
    auto bad=failedRow;
    bad["archiveAttemptTiming"]["attempts"][0]["archiveChildFailure"]["wrapperExitCode"]="unexpected-text";
    const auto bounded=carryTerminal(bad).at("priorOrdinaryExportTerminal").at("archiveAttemptSteps");
    check("invalid child fields do not discard valid attempt timing or the other failure",
        bounded.at("attempts")==compact.at("attempts")&&bounded.at("childFailures").size()==1&&bounded["childFailures"][0]==failures[1]);
    const auto availabilityPath=carryDir/L"log-export-availability.json";
    put(availabilityPath,availability.dump());
    const auto next=createTarArchiveWithRetry((carryDir/L"next.zip").wstring(),{input.wstring(),availabilityPath.wstring()});
    check("next ordinary archive includes preceding evidence input and is separately successful",next.ok&&next.attempts==1);
    put(carryDir/L"reference.json",failedRow.dump());
    auto retryRow=failedRow;
    retryRow.update(json{{"ok",retry.ok},{"cleanupOk",retry.cleanupOk},{"attempts",retry.attempts},
        {"archiveElapsedMs",retryElapsed},{"exportElapsedMs",retryElapsed},{"reason",retry.reason},
        {"win32Error",retry.win32Error},{"completion",retry.completion},
        {"archiveAttemptTiming",archiveAttemptTimingEvidence(retry)}});
    json retrySlot,residueAvailability={{"exportOperation",successor}};
    const json successfulBody={{"ok",true}};
    ymcc::logexport::retainPriorOrdinaryTerminal(retrySlot,retryRow,&successfulBody);
    ymcc::logexport::addPriorOrdinaryTerminal(residueAvailability,retrySlot);
    const auto& residueSteps=residueAvailability.at("priorOrdinaryExportTerminal").at("archiveAttemptSteps");
    const auto& residue=residueSteps.at("failedTemporaryLookups");
    const auto& originalLookup=retryRow.at("archiveAttemptTiming").at("attempts").at(0).at("failedTemporaryAfterCleanup");
    check("preceding carry retains failed cleanup metadata only from actual blocked attempt",
        residue.size()==1&&residue[0].at("attempt")==1&&residue[0].at("failedTemporaryAfterCleanup")==originalLookup&&
        originalLookup.at("state")=="regular-file-metadata"&&originalLookup.at("bytes")>0&&
        residue[0].at("failedCleanupErrors").at(0).at("errorValue")==previous.at("temporaryRemoveError")&&
        residue[0].at("failedCleanupErrors").at(1).at("errorValue")==previous.at("stagingRemoveError"));
    auto invalidLookup=retryRow;
    invalidLookup["archiveAttemptTiming"]["attempts"][0]["failedTemporaryAfterCleanup"]["bytes"]=true;
    ymcc::logexport::retainPriorOrdinaryTerminal(retrySlot,invalidLookup,&successfulBody);
    const auto& withoutLookup=retrySlot.at("archiveAttemptSteps");
    check("invalid metadata does not lose preceding return or timings",
        retrySlot.at("terminal").at("ok")==true&&withoutLookup.at("attempts")==residueSteps.at("attempts")&&
        !withoutLookup.contains("failedTemporaryLookups"));
    const auto residueDir=root/L"temporary-lookup-carry";fspath::create_directory(residueDir);
    const auto residuePath=residueDir/L"log-export-availability.json";
    put(residuePath,residueAvailability.dump());
    const auto residueZip=residueDir/L"next.zip";
    const auto residueResult=createTarArchiveWithRetry(residueZip.wstring(),{input.wstring(),residuePath.wstring()});
    check("next ordinary export publishes one archive with preceding lookup receipt",residueResult.ok&&residueResult.cleanupOk&&
        residueResult.attempts==1&&fspath::is_regular_file(residueZip)&&!fspath::exists(residueDir/L"archive-output.tmp"));
    put(residueDir/L"reference.json",retryRow.dump());
    return finish();
#endif
}

#endif
