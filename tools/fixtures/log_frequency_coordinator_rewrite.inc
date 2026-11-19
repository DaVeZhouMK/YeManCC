static std::atomic<bool> g_fanLogEnabled{true};
static constexpr size_t COORDINATOR_LOG_MAX_LINES=300,COORDINATOR_LOG_MAX_BYTES=256*1024;
enum class ReadFault{none,failure,exception};
static ReadFault readFault=ReadFault::none;
struct CoordinatorInput:std::ifstream {
 struct Buffer:std::streambuf {
  std::streambuf* original=nullptr;size_t consumed=0;
  int_type underflow()override{if(consumed>=10)throw std::runtime_error("owned-read-fault");return original->sgetc();}
  int_type uflow()override{const auto value=underflow();if(!traits_type::eq_int_type(value,traits_type::eof())){original->sbumpc();++consumed;}return value;}
 } buffer;
 bool attached=false;
 CoordinatorInput(const std::wstring& path,std::ios::openmode mode):std::ifstream(path,mode){if(is_open()&&readFault!=ReadFault::none){buffer.original=std::ifstream::rdbuf();static_cast<std::istream&>(*this).rdbuf(&buffer);attached=true;if(readFault==ReadFault::exception)exceptions(std::ios::badbit);}}
 ~CoordinatorInput(){exceptions(std::ios::goodbit);if(attached)static_cast<std::istream&>(*this).rdbuf(buffer.original);}
};
struct CoordinatorOutput:FailureOutput {
 CoordinatorOutput(const std::wstring& path,std::ios::openmode mode){open(path,mode);if(is_open()&&outputFault!=OutputFault::none&&outputFault!=OutputFault::openException){static_cast<std::ostream&>(*this).rdbuf(&buffer);attached=true;}}
};
static bool failTemporaryIdentityOnce=false;
static bool failTemporaryCloseOnce=false,shortTemporaryWrite=false,failPromotion=false,failTemporaryCleanup=false,collideTemporaryCreate=false;
static HANDLE observedTemporary=INVALID_HANDLE_VALUE;
static std::wstring observedTemporaryPath;
static unsigned promotionCalls=0,temporaryCreateCalls=0,temporaryWriteCalls=0,temporaryCloseCalls=0,temporaryCleanupCalls=0;
[[maybe_unused]] static HANDLE coordinatorCreatePort(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD attributes,HANDLE templateFile){
 if(disposition==CREATE_NEW){++temporaryCreateCalls;observedTemporaryPath=path;}
 if(disposition==CREATE_NEW&&outputFault==OutputFault::openException)throw std::runtime_error("owned-temporary-create-exception");
 if(disposition==CREATE_NEW&&collideTemporaryCreate){collideTemporaryCreate=false;HANDLE foreign=::CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(foreign==INVALID_HANDLE_VALUE)throw std::runtime_error("owned-collision-setup-failed");DWORD written=0;const char bytes[]="fixture-foreign-temporary";if(!::WriteFile(foreign,bytes,sizeof(bytes)-1,&written,nullptr)||written!=sizeof(bytes)-1){::CloseHandle(foreign);throw std::runtime_error("owned-collision-write-failed");}::CloseHandle(foreign);}
 observedTemporary=::CreateFileW(path,access,share,security,disposition,attributes,templateFile);return observedTemporary;
}
[[maybe_unused]] static BOOL coordinatorWritePort(HANDLE file,LPCVOID bytes,DWORD length,LPDWORD written,LPOVERLAPPED overlapped){
 ++temporaryWriteCalls;
 if(outputFault==OutputFault::writeException)throw std::runtime_error("owned-temporary-write-exception");
 if(outputFault==OutputFault::writeFailure){*written=0;SetLastError(ERROR_WRITE_FAULT);return FALSE;}
 if(shortTemporaryWrite){shortTemporaryWrite=false;const DWORD partial=length?length-1:0;return ::WriteFile(file,bytes,partial,written,overlapped);}
 return ::WriteFile(file,bytes,length,written,overlapped);
}
[[maybe_unused]] static BOOL coordinatorClosePort(HANDLE file){++temporaryCloseCalls;if(failTemporaryCloseOnce){failTemporaryCloseOnce=false;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::CloseHandle(file);}
[[maybe_unused]] static BOOL coordinatorMovePort(LPCWSTR source,LPCWSTR target,DWORD flags){++promotionCalls;if(failPromotion){failPromotion=false;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::MoveFileExW(source,target,flags);}
[[maybe_unused]] static BOOL coordinatorDispositionPort(HANDLE file,FILE_INFO_BY_HANDLE_CLASS kind,LPVOID info,DWORD size){++temporaryCleanupCalls;if(failTemporaryCleanup){failTemporaryCleanup=false;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::SetFileInformationByHandle(file,kind,info,size);}

[[maybe_unused]] static BOOL coordinatorInformationPort(HANDLE file,LPBY_HANDLE_FILE_INFORMATION info){if(failTemporaryIdentityOnce){failTemporaryIdentityOnce=false;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::GetFileInformationByHandle(file,info);}

@@ACTUAL_CLASSIFIER@@
@@ACTUAL_TRANSACTION@@
@@ACTUAL_WRITER@@
static void exportFailureAvailability(json& availability){
@@ACTUAL_EXPORT@@
}
static json exportFailureAvailability(){json value;exportFailureAvailability(value);return value;}
static json checks=json::array();
static json oldFailureSnapshots=json::object();
static void check(bool pass,const char* name){checks.push_back({{"name",name},{"pass",pass}});}
using Evidence=ymcc::logging::CoordinatorLogFailureEvidence;
[[maybe_unused]] static unsigned long long count(Evidence::Stage kind){return ymcc::logging::coordinatorLogFailureEvidence().snapshot()["buckets"][size_t(kind)]["observedFailures"].get<unsigned long long>();}
static void put(const fspath::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
static std::string read(const fspath::path& path){std::ifstream f(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
static void initializeErrorResources(const fspath::path& root){const auto p=root/L"fixture-initialization";put(p,"owned");bool denied=false;try{fspath::create_directories(p/L"blocked");}catch(const fspath::filesystem_error&){denied=true;}if(!denied)throw std::runtime_error("owned error initialization failed");}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);fixtureRoot=root.wstring();initializeErrorResources(root);
 (void)ymcc::sleepfactlog::writerIdentity();(void)ymcc::logging::coordinatorLogFailureEvidence().snapshot();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
#ifdef YMCC_COORDINATOR_WRITER_IDENTITY_ONLY
 const auto identityPath=root/L"hardware-coordinator.log";const json identityPayload={{"accepted",false},{"pid",999},{"writerPid",888},{"writerProcessCreation100ns","subject-only"}};
 writeCoordinatorDiagnosticLog("coordinator.writer-identity",identityPayload);const auto identityRow=json::parse(read(identityPath));
 check(identityRow["event"]=="coordinator.writer-identity"&&identityRow["details"]==identityPayload&&identityRow["pid"]==GetCurrentProcessId(),"original-coordinator-payload-and-root-pid-preserved");
 check(identityRow.size()==7&&identityRow["writerEnvelopeSchema"]==1&&identityRow["writerPid"]==ymcc::sleepfactlog::writerIdentity().pid&&identityRow["writerProcessCreation100ns"]==ymcc::sleepfactlog::writerCreation100nsJson(),"versioned-coordinator-writer-is-cached-native-not-payload-subject");
 GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-file-and-blocker-handles-returned");unsigned identityFailures=0;for(const auto& row:checks)identityFailures+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",identityFailures},{"row",identityRow},{"handlesBefore",before},{"handlesAfter",after}}.dump(2)<<'\n';return identityFailures?2:0;
#endif
 const auto path=root/L"hardware-coordinator.log";const std::string original="{\"event\":\"retained-one\",\"details\":{\"accepted\":false}}\n{\"event\":\"retained-two\"}\n";put(path,original);
 HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(held==INVALID_HANDLE_VALUE)throw std::runtime_error("owned read-sharing blocker failed");
 bool readDenied=false;{std::ifstream probe(path,std::ios::binary);readDenied=!probe.is_open();}
 HANDLE writable=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);const bool writeAllowed=writable!=INVALID_HANDLE_VALUE;if(writeAllowed)CloseHandle(writable);
 writeCoordinatorDiagnosticLog("coordinator.owned-history",{{"accepted",false}});CloseHandle(held);held=nullptr;
 check(readDenied&&writeAllowed,"real-sharing-blocker-denies-read-and-allows-write");check(read(path)==original,"unreadable-existing-history-is-not-truncated");
#ifdef YMCC_COORDINATOR_OLD_ONLY
 writeCoordinatorDiagnosticLog("api.old-over-budget",{{"large",std::string(COORDINATOR_LOG_MAX_BYTES,'x')}});oldFailureSnapshots["overBudgetAfter"]=read(path);check(read(path)==original,"old-over-budget-new-row-keeps-history");
 put(path,original);outputFault=OutputFault::writeFailure;writeCoordinatorDiagnosticLog("api.old-write-failure",{{"accepted",false}});outputFault=OutputFault::none;oldFailureSnapshots["writeFailureAfter"]=read(path);check(read(path)==original,"old-failed-output-write-keeps-history");
#endif
#ifndef YMCC_COORDINATOR_OLD_ONLY
 check(count(Evidence::Stage::sourceOpenOrPresenceFailure)==1,"source-open-or-presence-failure-has-distinct-bucket");
 const auto nativeBefore=ymcc::logging::nativeLogFailureEvidence().snapshot(),deckyBefore=ymcc::logging::deckyLogFailureEvidence().snapshot(),fanBefore=ymcc::logging::fanLifecycleLogFailureEvidence().snapshot(),detailBefore=ymcc::logging::fanDetailLogFailureEvidence().snapshot();
 const std::string event="api.owned-gate";const json payload={{"accepted",false},{"pid",999},{"writerPid",888},{"writerProcessCreation100ns","subject-only"}};const auto gateBefore=ymcc::logging::coordinatorLogFailureEvidence().snapshot();const auto rotationsBefore=rotations;const auto commitGateBefore=ymcc::logging::coordinatorRewriteCommitEvidence().snapshot();const auto temporaryCreatesBeforeGate=temporaryCreateCalls;g_fanLogEnabled=false;
 ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)writeCoordinatorDiagnosticLog(event,payload);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);g_fanLogEnabled=true;
 check(arm==0&&temporaryCreateCalls==temporaryCreatesBeforeGate&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()==commitGateBefore&&rotations==rotationsBefore&&read(path)==original&&ymcc::logging::coordinatorLogFailureEvidence().snapshot()==gateBefore,"50000-disabled-offers-no-allocation-file-work-or-counters");
 writeCoordinatorDiagnosticLog("unselected.owned",payload);check(temporaryCreateCalls==temporaryCreatesBeforeGate&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()==commitGateBefore&&rotations==rotationsBefore&&read(path)==original&&ymcc::logging::coordinatorLogFailureEvidence().snapshot()==gateBefore,"unselected-prefix-stays-outside-original-writer");
 fspath::remove(path);writeCoordinatorDiagnosticLog("lifecycle.owned-first",payload);const auto first=json::parse(read(path));check((first.size()==4||first.size()==7)&&first["event"]=="lifecycle.owned-first"&&first["details"]==payload&&first["pid"]==GetCurrentProcessId(),"observed-absent-first-file-keeps-original-four-field-row");
 check(first.size()==7&&first["writerEnvelopeSchema"]==1&&first["writerPid"]==ymcc::sleepfactlog::writerIdentity().pid&&first["writerProcessCreation100ns"]==ymcc::sleepfactlog::writerCreation100nsJson()&&first["details"]==payload,"versioned-coordinator-writer-is-cached-native-not-payload-subject");
 std::string many;for(unsigned i=0;i<350;++i)many+="line-"+std::to_string(i)+"\r\n";put(path,many);writeCoordinatorDiagnosticLog("api.retention-lines",payload);const auto retained=read(path);size_t lineCount=0;for(char ch:retained)lineCount+=ch=='\n';check(lineCount==300&&retained.starts_with("line-51\n")&&retained.find('\r')==std::string::npos,"original-300-line-retention-and-crlf-normalization-preserved");
 std::string large;for(unsigned i=0;i<290;++i)large+=std::string(1024,'a')+"\n";put(path,large);writeCoordinatorDiagnosticLog("api.retention-bytes",payload);const auto bounded=read(path);check(!bounded.empty()&&bounded.size()<=COORDINATOR_LOG_MAX_BYTES&&bounded.ends_with("\n")&&bounded.find("api.retention-bytes")!=std::string::npos,"original-256-kib-whole-line-retention-preserved");
 put(path,original);readFault=ReadFault::failure;writeCoordinatorDiagnosticLog("api.read-false",payload);readFault=ReadFault::none;check(read(path)==original&&count(Evidence::Stage::sourceReadFailure)==1,"partial-source-read-false-preserves-all-original-bytes");
 readFault=ReadFault::exception;writeCoordinatorDiagnosticLog("api.read-exception",payload);readFault=ReadFault::none;check(read(path)==original&&count(Evidence::Stage::sourceReadException)==1,"partial-source-read-exception-preserves-all-original-bytes");
 held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(held==INVALID_HANDLE_VALUE)throw std::runtime_error("owned write-sharing blocker failed");writeCoordinatorDiagnosticLog("api.output-open-false",payload);CloseHandle(held);held=nullptr;
 check(read(path)==original&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()["buckets"][3]["observedFailures"]==1,"real-output-replacement-denial-keeps-source-and-distinct-commit-stage");
 outputFault=OutputFault::openException;held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(held==INVALID_HANDLE_VALUE)throw std::runtime_error("owned open-exception blocker failed");writeCoordinatorDiagnosticLog("api.output-open-exception",payload);CloseHandle(held);held=nullptr;outputFault=OutputFault::none;
 check(read(path)==original&&count(Evidence::Stage::rewriteException)==1,"temporary-create-port-exception-is-contained");
 outputFault=OutputFault::writeFailure;writeCoordinatorDiagnosticLog("api.output-write-false",payload);outputFault=OutputFault::none;check(read(path)==original&&count(Evidence::Stage::rewriteWriteFailure)==1,"failed-temporary-write-keeps-history-and-separate-summary");
 put(path,original);outputFault=OutputFault::writeException;writeCoordinatorDiagnosticLog("api.output-write-exception",payload);outputFault=OutputFault::none;check(read(path)==original&&count(Evidence::Stage::rewriteException)==2,"temporary-write-port-exception-keeps-original-bytes");
 put(path,original);writeCoordinatorDiagnosticLog("api.bad-json",{{"bad",std::string(1,static_cast<char>(0xff))}});check(read(path)==original&&count(Evidence::Stage::preparationException)==1,"actual-json-dump-failure-never-reaches-truncation");
 fixtureRoot=(root/L"fixture-initialization"/L"blocked").wstring();writeCoordinatorDiagnosticLog("api.directory-error",payload);fixtureRoot=root.wstring();check(count(Evidence::Stage::preparationException)==2,"actual-directory-preparation-exception-is-contained");
 const auto evidence=ymcc::logging::coordinatorLogFailureEvidence().snapshot();for(unsigned i=0;i<100;++i)writeCoordinatorDiagnosticLog(event,payload);check(ymcc::logging::coordinatorLogFailureEvidence().snapshot()==evidence,"100-healthy-rewrites-do-not-increment-failure-counters");
 check(ymcc::logging::nativeLogFailureEvidence().snapshot()==nativeBefore&&ymcc::logging::deckyLogFailureEvidence().snapshot()==deckyBefore&&ymcc::logging::fanLifecycleLogFailureEvidence().snapshot()==fanBefore&&ymcc::logging::fanDetailLogFailureEvidence().snapshot()==detailBefore,"coordinator-storage-independent-of-four-original-recorders");
 Evidence local;const std::string longEvent=std::string("a\n")+std::string(100,'x');local.record(Evidence::Stage::sourceReadFailure,longEvent.c_str());ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)local.record(Evidence::Stage::sourceReadFailure,"later");const auto recorderArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);const auto prefix=local.snapshot()["buckets"][2];check(recorderArm==0&&prefix["observedFailures"]==50001&&prefix["firstEvent"].get<std::string>().size()==95&&prefix["firstEventAsciiSanitized"]==true&&prefix["firstEventTruncated"]==true,"50000-fixed-record-calls-allocation-free-with-bounded-first-prefix");
 const auto availability=exportFailureAvailability();check(availability["coordinatorLogFailureEvidence"]["stats"]==evidence&&availability["coordinatorLogFailureEvidence"]["writerPid"]==GetCurrentProcessId()&&availability["coordinatorLogFailureEvidence"]["writerProcessCreation100ns"].is_string(),"actual-optional-export-retains-exact-seven-stage-stats-and-writer");
 json unavailable={{"existing",true}};ymcc::testing::allocationFault::failAfter(0);exportFailureAvailability(unavailable);ymcc::testing::allocationFault::failAfter(-1);check(unavailable["existing"]==true&&!unavailable.contains("coordinatorRewriteCommitEvidence"),"optional-export-allocation-failure-keeps-existing-result");
 put(path,original);const auto createsBeforeBudget=temporaryCreateCalls;writeCoordinatorDiagnosticLog("api.over-budget",{{"large",std::string(COORDINATOR_LOG_MAX_BYTES,'x')}});
 check(read(path)==original&&temporaryCreateCalls==createsBeforeBudget&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()["buckets"][5]["observedFailures"]==1,"over-budget-new-row-keeps-history-and-never-creates-temporary");
 put(path,original);const auto closeBefore=temporaryCloseCalls,promotionBeforeClose=promotionCalls;failTemporaryCloseOnce=true;writeCoordinatorDiagnosticLog("api.temporary-close-false",payload);
 check(read(path)==original&&temporaryCloseCalls==closeBefore+2&&promotionCalls==promotionBeforeClose&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()["buckets"][2]["observedFailures"]==1,"failed-temporary-close-prevents-promotion-and-returns-handle-on-cleanup");
 put(path,original);collideTemporaryCreate=true;const auto deletesBeforeCollision=temporaryCleanupCalls;writeCoordinatorDiagnosticLog("api.temporary-collision",payload);
 check(read(path)==original&&read(observedTemporaryPath)=="fixture-foreign-temporary"&&temporaryCleanupCalls==deletesBeforeCollision&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()["buckets"][0]["observedFailures"]==1,"create-new-collision-never-overwrites-or-deletes-preexisting-temporary");
 if(!::DeleteFileW(observedTemporaryPath.c_str()))throw std::runtime_error("owned collision cleanup failed");
 put(path,original);const auto movesBeforeShort=promotionCalls;shortTemporaryWrite=true;writeCoordinatorDiagnosticLog("api.temporary-short-write",payload);
 check(read(path)==original&&promotionCalls==movesBeforeShort&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()["buckets"][1]["observedFailures"]==2,"actual-short-temporary-write-never-promotes-partial-bytes");
 put(path,original);failPromotion=true;const auto promotionsBeforeFailure=promotionCalls;writeCoordinatorDiagnosticLog("api.temporary-promotion-false",payload);
 check(read(path)==original&&promotionCalls==promotionsBeforeFailure+1&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()["buckets"][3]["observedFailures"]==2,"one-failed-promotion-preserves-original-without-retry");
 put(path,original);outputFault=OutputFault::writeFailure;failTemporaryCleanup=true;writeCoordinatorDiagnosticLog("api.temporary-cleanup-false",payload);outputFault=OutputFault::none;
 check(read(path)==original&&fspath::exists(observedTemporaryPath)&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()["buckets"][4]["observedFailures"]==1,"cleanup-refusal-keeps-original-and-exposes-owned-temporary-debt");
 const auto debtCreates=temporaryCreateCalls;writeCoordinatorDiagnosticLog("api.owned-cleanup-recovered",payload);
 check(read(path).starts_with(original)&&read(path).find("api.owned-cleanup-recovered")!=std::string::npos&&temporaryCreateCalls==debtCreates+1,"next-call-reclaims-same-identity-debt-before-one-new-commit");
 put(path,original);outputFault=OutputFault::writeFailure;failTemporaryCleanup=true;writeCoordinatorDiagnosticLog("api.pending-identity-change",payload);outputFault=OutputFault::none;
 HANDLE oldDebt=::CreateFileW(observedTemporaryPath.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(oldDebt==INVALID_HANDLE_VALUE)throw std::runtime_error("owned debt identity handle failed");
 const auto retainedDebt=root/L"fixture-retained-old-debt";if(!::MoveFileExW(observedTemporaryPath.c_str(),retainedDebt.c_str(),0)){::CloseHandle(oldDebt);throw std::runtime_error("owned debt replacement setup failed");}put(observedTemporaryPath,"fixture-foreign-replacement");const auto blockedCreates=temporaryCreateCalls;
 writeCoordinatorDiagnosticLog("api.foreign-debt-refused",payload);check(read(path)==original&&read(observedTemporaryPath)=="fixture-foreign-replacement"&&temporaryCreateCalls==blockedCreates,"changed-debt-file-identity-is-never-deleted-or-followed-by-another-temp");
 ::CloseHandle(oldDebt);if(!::DeleteFileW(observedTemporaryPath.c_str()))throw std::runtime_error("owned foreign cleanup failed");if(!::DeleteFileW(retainedDebt.c_str()))throw std::runtime_error("owned retained debt cleanup failed");

 put(path,original);failTemporaryIdentityOnce=true;failPromotion=true;writeCoordinatorDiagnosticLog("api.unknown-debt-identity",payload);const auto unknownCreates=temporaryCreateCalls;
 writeCoordinatorDiagnosticLog("api.unknown-debt-refused",payload);check(read(path)==original&&fspath::exists(observedTemporaryPath)&&temporaryCreateCalls==unknownCreates,"unknown-debt-identity-never-authorizes-deletion-or-new-temporary");
 if(!::DeleteFileW(observedTemporaryPath.c_str()))throw std::runtime_error("owned unknown-debt cleanup failed");
 put(path,original);const auto commitBefore=ymcc::logging::coordinatorRewriteCommitEvidence().snapshot();writeCoordinatorDiagnosticLog("api.commit-recovered",payload);const auto recovered=read(path);
 check(recovered.starts_with(original)&&recovered.find("api.commit-recovered")!=std::string::npos&&ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()==commitBefore,"next-original-healthy-call-commits-after-observed-missing-debt-without-replay");
 size_t remainingTemporary=0;for(const auto& item:fspath::directory_iterator(root))if(item.path().filename().wstring().find(L"hardware-coordinator.log.rewrite-")==0)++remainingTemporary;
 check(remainingTemporary==0,"all-own-stage-temporaries-removed-or-promoted-after-explicit-test-debt-cleanup");
 const auto commitAvailability=exportFailureAvailability();check(commitAvailability["coordinatorRewriteCommitEvidence"]["stats"]==ymcc::logging::coordinatorRewriteCommitEvidence().snapshot()&&commitAvailability["coordinatorRewriteCommitEvidence"]["stats"]["explicitFlushPerformed"]==false,"actual-commit-export-keeps-independent-failure-marginals-without-flush-proof");

#endif
 GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-file-and-blocker-handles-returned");unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"availability",exportFailureAvailability()},{"handlesBefore",before},{"handlesAfter",after},{"realReadSharingDenied",readDenied},{"realWriteSharingAllowed",writeAllowed},{"sourceBefore",original},{"oldFailureSnapshots",oldFailureSnapshots},{"onlyOwnFiles",true},{"rotationMemoryPort",true}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
