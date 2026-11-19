static std::atomic<bool> g_fanLogEnabled{true};
static std::wstring g_fanLogFile;
@@ACTUAL_WRITER@@
static void exportFailureAvailability(json& availability){
@@ACTUAL_EXPORT@@
}
static json exportFailureAvailability(){json value;exportFailureAvailability(value);return value;}
static void initializeFanDetailFixtureErrorResources(const fspath::path& root){
 const auto marker=root/L"fixture-error-resource-initialization";{std::ofstream f(marker);f<<"owned";}
 bool rejected=false;try{fspath::create_directories(marker/L"nested");}catch(const fspath::filesystem_error&){rejected=true;}
 if(!rejected)throw std::runtime_error("owned filesystem-error initialization did not reject");
}
static json checks=json::array();
static void check(bool pass,const char* name){checks.push_back({{"name",name},{"pass",pass}});}
using Evidence=ymcc::logging::FanDetailLogFailureEvidence;
static unsigned long long count(Evidence::Kind kind){return ymcc::logging::fanDetailLogFailureEvidence().snapshot()["buckets"][size_t(kind)]["observedFailures"].get<unsigned long long>();}
static json readRows(const fspath::path& path){std::ifstream in(path);json rows=json::array();std::string line;while(std::getline(in,line))if(!line.empty())rows.push_back(json::parse(line));return rows;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);fixtureRoot=root.wstring();g_fanLogFile=(root/L"fan-api.log").wstring();
 initializeFanDetailFixtureErrorResources(root);
 (void)ymcc::sleepfactlog::writerIdentity();(void)ymcc::logging::fanDetailLogFailureEvidence().snapshot();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 const json payload={{"caller","owned-fan-detail"},{"accepted",false},{"pid",999},{"writerPid",888},{"writerEnvelopeSchema",42},{"writerProcessCreation100ns","subject-only"}};const std::string event="api.owned-detail";
 const auto initial=ymcc::logging::fanDetailLogFailureEvidence().snapshot();
 writeFanDiagnosticLog(event,payload);auto rows=readRows(root/L"fan-api.log");
 check(rows.size()==1&&rows[0]["event"]==event&&rows[0]["details"]==payload&&rows[0]["time"].is_string()&&(rows[0].size()==3||rows[0].size()==6),"original-detailed-row-and-false-payload-preserved");
 check(rows.size()==1&&rows[0].size()==6&&rows[0]["writerEnvelopeSchema"]==1&&rows[0]["writerPid"]==ymcc::sleepfactlog::writerIdentity().pid&&rows[0]["writerProcessCreation100ns"]==ymcc::sleepfactlog::writerCreation100nsJson()&&rows[0]["details"]==payload,"versioned-fan-detail-writer-is-cached-native-not-payload-subject");
 fspath::create_directories(root/L"directory-target");g_fanLogFile=(root/L"directory-target").wstring();writeFanDiagnosticLog("api.open-false",payload);g_fanLogFile=(root/L"fan-api.log").wstring();
 check(count(Evidence::openFailure)==1,"actual-directory-target-open-failure-is-observed");
#ifndef YMCC_FAN_DETAIL_FAILURE_OLD_ONLY
 const auto nativeBefore=ymcc::logging::nativeLogFailureEvidence().snapshot();const auto deckyBefore=ymcc::logging::deckyLogFailureEvidence().snapshot();const auto fanBefore=ymcc::logging::fanLifecycleLogFailureEvidence().snapshot();
 const auto gateBefore=ymcc::logging::fanDetailLogFailureEvidence().snapshot();const auto rotationsBefore=rotations;g_fanLogEnabled=false;
 ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)writeFanDiagnosticLog(event,payload);const auto countdown=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);g_fanLogEnabled=true;
 check(countdown==0&&rotations==rotationsBefore&&ymcc::logging::fanDetailLogFailureEvidence().snapshot()==gateBefore,"50000-disabled-offers-no-allocation-io-or-failure-counter");
 g_fanLogFile.clear();writeFanDiagnosticLog(event,payload);g_fanLogFile=(root/L"fan-api.log").wstring();check(ymcc::logging::fanDetailLogFailureEvidence().snapshot()==gateBefore,"empty-path-does-not-become-a-file-failure");
 {std::ofstream marker(root/L"blocked-parent");marker<<"owned";}g_fanLogFile=(root/L"blocked-parent"/L"fan-api.log").wstring();writeFanDiagnosticLog("api.directory-preparation",payload);g_fanLogFile=(root/L"fan-api.log").wstring();
 check(count(Evidence::preparationException)==1,"actual-directory-preparation-exception-is-contained");
 writeFanDiagnosticLog("api.invalid-utf8",{{"bad",std::string(1,static_cast<char>(0xff))}});check(count(Evidence::preparationException)==2,"actual-json-dump-exception-is-preparation-not-write");
 g_fanLogFile=(root/L"directory-target").wstring();outputFault=OutputFault::openException;writeFanDiagnosticLog("api.open-exception",payload);outputFault=OutputFault::none;g_fanLogFile=(root/L"fan-api.log").wstring();
 check(count(Evidence::openException)==1,"actual-stream-open-exception-is-contained");
 outputFault=OutputFault::writeFailure;writeFanDiagnosticLog("api.write-false",payload);outputFault=OutputFault::none;check(count(Evidence::writeFailure)==1,"original-buffered-write-false-is-observed");
 outputFault=OutputFault::writeException;writeFanDiagnosticLog("api.write-exception",payload);outputFault=OutputFault::none;check(count(Evidence::writeException)==1,"original-stream-write-exception-is-contained");
 auto evidence=ymcc::logging::fanDetailLogFailureEvidence().snapshot();check(evidence["explicitFlushPerformed"]==false&&evidence["openBoundaryRepairPerformed"]==false&&count(Evidence::flushFailure)==0&&count(Evidence::flushException)==0,"destructor-flush-and-boundary-repair-are-not-invented");
 for(unsigned i=0;i<100;++i)writeFanDiagnosticLog(event,payload);check(ymcc::logging::fanDetailLogFailureEvidence().snapshot()==evidence,"100-successful-offers-do-not-increment-failure-counters");
 check(ymcc::logging::nativeLogFailureEvidence().snapshot()==nativeBefore&&ymcc::logging::deckyLogFailureEvidence().snapshot()==deckyBefore&&ymcc::logging::fanLifecycleLogFailureEvidence().snapshot()==fanBefore,"detail-storage-is-independent-of-native-decky-and-fan-lifecycle");
 Evidence local;const std::string longEvent=std::string("a\n")+std::string(100,'x');local.record(Evidence::writeFailure,longEvent.c_str());ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)local.record(Evidence::writeFailure,"later");const auto recorderArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 const auto prefix=local.snapshot()["buckets"][Evidence::writeFailure];check(recorderArm==0&&prefix["observedFailures"]==50001&&prefix["firstEvent"].get<std::string>().size()==95&&prefix["firstEventAsciiSanitized"]==true&&prefix["firstEventTruncated"]==true,"50000-record-calls-allocation-free-with-bounded-first-prefix");
 const auto availability=exportFailureAvailability();check(availability["fanDetailLogFailureEvidence"]["stats"]==evidence&&availability["fanDetailLogFailureEvidence"]["writerPid"]==GetCurrentProcessId()&&availability["fanDetailLogFailureEvidence"]["writerProcessCreation100ns"].is_string(),"actual-optional-export-publishes-exact-stats-and-current-writer");
 json unavailable={{"existing",true}};ymcc::testing::allocationFault::failAfter(0);exportFailureAvailability(unavailable);ymcc::testing::allocationFault::failAfter(-1);check(unavailable["existing"]==true&&!unavailable.contains("fanDetailLogFailureEvidence"),"optional-export-allocation-failure-keeps-original-result");
#endif
 GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-file-handles-returned");unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
 std::cout<<json{{"checks",checks},{"failures",failed},{"availability",exportFailureAvailability()},{"handlesBefore",before},{"handlesAfter",after},{"onlyOwnFiles",true},{"originalWriterWithStreamTypePort",true},{"rotationIsMemoryPort",true}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& e){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<e.what()<<'\n';return 3;}}
