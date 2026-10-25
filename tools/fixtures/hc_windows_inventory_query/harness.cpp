// Boundary fakes only. Actual native query, observer, IPC and export code is inserted.
#include <thread>
#include <sstream>
// @@BASE_FIXTURE@@
struct Machine {std::string deviceClass="fixture-model";};static Machine g_machineIdentity;
using DWORD=std::uint32_t;
static DWORD fixtureLastError=71;
static bool fixtureWriteOk=true;
static unsigned fixtureWrites=0;
static std::map<std::wstring,std::string> fixtureFiles;
static std::map<std::string,std::function<json(const json&)>> fixtureHandlers;
static DWORD GetLastError(){return fixtureLastError;}
static void SetLastError(DWORD value){fixtureLastError=value;}
static std::wstring yemancc_data_dir(){return L"fixture";}
static std::string nativeLifecycleTimestamp(){return "fixture-time";}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& body){++fixtureWrites;fixtureLastError=99;if(!fixtureWriteOk)return false;fixtureFiles[path]=body;return true;}
static void ipc_on(const char* command,std::function<json(const json&)> handler){fixtureHandlers[command]=std::move(handler);}
// @@TICKET_FUNCTION@@
// @@QUERY_FUNCTION@@
// Probe behavior is composed separately by hc_windows_candidate_probe_selftest.
static json inputCaptureWindowsSourceProbeStatus(){return {{"scope","fixture-probe-not-run"}};}
static json inputCaptureWindowsSourceEligibilityStatus(){return {{"scope","qualification-fixture-not-run"}};}
// @@EXPORT_FUNCTION@@
static void registerActualIpc(){
// @@IPC_BLOCK@@
}
static std::vector<std::wstring> packageActualExport(json& availability){std::vector<std::wstring> paths;const auto addFile=[&](const std::wstring& path){paths.push_back(path);};
// @@PACKAGE_BLOCK@@
return paths;}
static std::string actualManifest(){std::ostringstream manifest;
// @@MANIFEST_BLOCK@@
return manifest.str();}
static void prepare(){fixtureMetaHook={};reset();fixtureLastError=71;fixtureWriteOk=true;fixtureWrites=0;fixtureFiles.clear();fixtureHandlers.clear();}
static void observe(){inputCaptureObserveWindowsInventory(ticket());}
int main(int argc,char** argv){try{
 const int baseResult=inventoryBaselineMain(argc,argv);check(baseResult==0,"query/baseline original inventory assertions pass");prepare();
 auto data=inputCaptureWindowsSensorStatus();check(data["recorded"]==false&&data["observationState"]=="not-enumerated"&&data["lastObservation"].is_null()&&data["observationAgeMs"].is_null(),"query/unobserved metadata stays unknown rather than an empty device list");
 check(fixtureQueries==0&&fixtureEvents.empty()&&g_inputCaptureSink->samples==0,"query/read does not enumerate, log, or publish samples");
 observe();const auto published=fixtureQueries;data=inputCaptureWindowsSensorStatus();
 check(data["recorded"]==true&&data["metadataContextCurrent"]==true&&data["enumerationComplete"]==true&&data["candidateGroupsFromCurrentContext"].size()==2,"query/current complete cached metadata is exposed without another enumeration");
 check(data["selectedMetadataMatchesObservation"]==true&&data["lastObservation"]["selectedAtEnd"]==data["selectedAtQuery"],"query/current selected identity is compared with original observation");
 check(data["queryDoesNotEnumerate"]==true&&data["sensorObjectsOpenedByQuery"]==0&&data["selectionSupported"]==false&&data["selectionWritten"]==false&&data["samplesPublished"]==false,"query/API declares readonly metadata boundary");
 check(data["metadataAvailabilityCurrentProven"]==false&&data["sampleFreshnessProven"]==false&&data["hostSubmissionProven"]==false&&data["gameConsumptionProven"]==false,"query/metadata context never becomes sample, Host or game acceptance");
 for(unsigned i=0;i<12;++i)(void)inputCaptureWindowsSensorStatus();check(fixtureQueries==published,"query/repeated reads reuse observation without invoking discovery");
 fixtureTick+=43;data=inputCaptureWindowsSensorStatus();check(data["observationAgeMs"]==43,"query/observation age is explicit");fixtureTick=999;data=inputCaptureWindowsSensorStatus();check(data["metadataContextCurrent"]==false&&data["observationAgeMs"].is_null()&&data["reason"]=="clock-regressed","query/regressed clock never makes history current");
 for(const std::string change:{"binding","power","owner","inactive","power-unknown"}){
  prepare();observe();if(change=="binding")g_inputCaptureSink->activeBindingGeneration=74;if(change=="power")fixturePower=8;if(change=="owner")g_inputCaptureSink=std::make_shared<Sink>();if(change=="inactive")g_inputCaptureSink->activeBindingGeneration=0;if(change=="power-unknown")fixturePower=0;data=inputCaptureWindowsSensorStatus();
  check(data["recorded"]==true&&data["metadataContextCurrent"]==false&&data["candidateGroupsFromCurrentContext"].is_null(),"query/retired observation cannot supply current-context candidates: "+change);
  check(data["observationBindingGeneration"]==73&&data["observationPowerGeneration"]==7&&data["lastObservation"]["bindingGeneration"]==73,"query/history keeps its original scope: "+change);
 }
 prepare();observe();fixtureMeta.gyroDeviceId="new-id-without-binding-change";data=inputCaptureWindowsSensorStatus();check(data["metadataContextCurrent"]==false&&data["reason"]=="selected-metadata-changed"&&data["selectedMetadataMatchesObservation"]==false&&data["candidateGroupsFromCurrentContext"].is_null(),"query/selected metadata change cannot reuse former identity as current evidence");
 for(const std::string change:{"owner","power","binding"}){
  prepare();observe();fixtureMetaHook=[change]{if(change=="owner")g_inputCaptureSink=std::make_shared<Sink>();if(change=="power")fixturePower=8;if(change=="binding")g_inputCaptureSink->activeBindingGeneration=74;};data=inputCaptureWindowsSensorStatus();fixtureMetaHook={};
  check(data["metadataContextCurrent"]==false&&data["contextStableAtCheck"]==false&&data["candidateGroupsFromCurrentContext"].is_null(),"query/context change during read is rejected: "+change);
 }
 prepare();Sink reused;auto first=std::shared_ptr<Sink>(&reused,[](auto*){});g_inputCaptureSink=first;observe();const auto retained=ticket();auto second=std::shared_ptr<Sink>(&reused,[](auto*){});g_inputCaptureSink=second;data=inputCaptureWindowsSensorStatus();check(data["metadataContextCurrent"]==false&&data["reason"]=="owner-changed","query/same counter address with different shared ownership cannot reuse cache");
 prepare();std::weak_ptr<Sink> old=g_inputCaptureSink;observe();g_inputCaptureSink.reset();check(old.expired(),"query/cache never retains original sensor sink");
 prepare();fixtureEnumerate=[](auto,auto,const auto&){return inv::Listing{};};observe();data=inputCaptureWindowsSensorStatus();check(data["metadataContextCurrent"]==true&&data["lastObservation"]["kinds"][0]["enumerated"]==0&&data["candidateGroupsFromCurrentContext"].empty(),"query/successful empty enumeration is known zero in original complete observation");
 prepare();fixtureEnumerate=[](auto,auto,const auto&)->inv::Listing{throw inv::Error{inv::State::Failed,-7};};observe();data=inputCaptureWindowsSensorStatus();check(data["metadataContextCurrent"]==true&&data["observationState"]=="failed"&&data["enumerationComplete"]==false&&data["lastObservation"]["kinds"][0]["enumerated"].is_null()&&data["candidateGroupsFromCurrentContext"].is_null(),"query/current failed observation remains failure and unknown counts");
 prepare();fixtureEnumerate=[](auto kind,auto,const auto&){if(kind==inv::Kind::Accel)throw inv::Error{inv::State::Failed,-8};return listing({device(g1)});};observe();data=inputCaptureWindowsSensorStatus();check(data["observationState"]=="partial"&&data["candidateGroupsFromCurrentContext"].is_null()&&data["lastObservation"]["kinds"][1]["enumerated"].is_null(),"query/partial observations never nominate complete current candidate groups");
 prepare();fixtureEnumerate=[](auto,auto,const auto&){fixtureTick+=1500;throw inv::Error{inv::State::TimedOut,-9};return inv::Listing{};};observe();data=inputCaptureWindowsSensorStatus();check(data["observationState"]=="timed-out"&&data["enumerationComplete"]==false&&data["candidateGroupsFromCurrentContext"].is_null(),"query/timeout remains explicit cached outcome");
 prepare();fixtureLogFailures=1;observe();data=inputCaptureWindowsSensorStatus();check(data["recorded"]==true&&data["observationState"]=="complete","query/completed metadata remains readable when domain logging fails");
 prepare();observe();registerActualIpc();const auto queries=fixtureQueries;data=fixtureHandlers.at("gyro.windowsSensors.get")(json::object());check(data["metadataContextCurrent"]==true&&fixtureQueries==queries,"query/actual registered IPC delegates to readonly cached query");
 prepare();observe();const auto beforeExport=fixtureQueries;auto path=exportWindowsSensorEvidence();check(!path.empty()&&fixtureWrites==1&&fixtureLastError==71,"query/actual export writes current snapshot and preserves caller error state");data=json::parse(fixtureFiles.at(path));check(data["exportEntry"]=="logs.exportAll"&&data["readOnly"]==true&&data["nativeEvidenceVersion"]==2&&data["lastObservation"]["kinds"][0]["enumerated"]==2,"query/export contains real cached observation and explicit provenance");check(fixtureQueries==beforeExport,"query/export never performs sensor discovery");
 json availability={{"files",json::array()}};auto paths=packageActualExport(availability);check(paths.size()==1&&availability["files"].empty(),"query/actual log package adds successfully exported evidence");
 fixtureWriteOk=false;availability["files"]=json::array();paths=packageActualExport(availability);check(paths.empty()&&availability["files"].size()==1&&availability["files"][0]["present"]==false&&fixtureFiles.count(path)==1,"query/failed export never reuses an older evidence file");check(fixtureLastError==71,"query/failed export restores caller error state");
 fixtureMetaHook=[]{fixtureLastError=101;throw std::runtime_error("query boundary failure");};check(exportWindowsSensorEvidence().empty()&&fixtureLastError==71,"query/export catches query failure and preserves caller error");fixtureMetaHook={};
 check(actualManifest().find("metadataAndExplicitRawProbeEvidence=true;queryDoesNotEnumerate=true")!=std::string::npos,"query/actual manifest states readonly metadata scope");
 prepare();inv::ObservationStore<json> concurrent;const auto key=ticket();inv::Snapshot complete;complete.state=inv::State::Complete;complete.sourceCurrent=true;complete.finished=1000;std::atomic<bool> readerStarted{false},done{false};std::thread writer([&]{concurrent.publish(key,complete,{{"iteration",1}});while(!readerStarted.load())std::this_thread::yield();for(unsigned i=2;i<=200;++i)concurrent.publish(key,complete,{{"iteration",i}});done=true;});unsigned reads=0;bool coherent=true;do{const auto view=concurrent.inspect(key,1001);if(view.lastObservation){++reads;coherent=coherent&&view.metadataContextCurrent&&(*view.lastObservation)["iteration"]==view.serial;readerStarted=true;}}while(!done.load());writer.join();check(reads>0&&coherent,"query/cache publication and query copies are coherent under concurrent access");
 const auto result=json{{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"actualNativeObserver",true},{"actualNativeQuery",true},{"actualIpcHandler",true},{"actualExportFunction",true},{"actualPackageEvidenceBlock",true},{"actualManifestBlock",true},{"wholeNativeThreadProcExecuted",false},{"normalAppStarted",false},{"hardwareIo",false},{"filesystemWrites","mock atomic file boundary only"},{"newProductDlls",json::array()}};std::ofstream(argv[1])<<result.dump(2);std::cout<<checks<<" checks / "<<failures.size()<<" failures\n";return failures.empty()?0:1;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
