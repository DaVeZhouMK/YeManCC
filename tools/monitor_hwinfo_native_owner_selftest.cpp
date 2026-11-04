// Exercises the real config parser/merger and the complete demand-driven
// transaction using inert operations. This executable is test-only.
#include <fstream>
#include <iostream>
#include "../native/monitor_hwinfo_manager.h"
#include <winioctl.h>
using namespace ymcc::hwinfo;
static std::string bytesFromHex(const std::string&s){std::string out;require(s.size()%2==0,"bad hex");for(std::size_t i=0;i<s.size();i+=2)out.push_back(static_cast<char>(std::stoul(s.substr(i,2),nullptr,16)));return out;}
static std::string hex(const std::string&s){constexpr char h[]="0123456789abcdef";std::string out;for(unsigned char c:s){out+=h[c>>4];out+=h[c&15];}return out;}
struct Fake {
 std::vector<std::string> calls;bool lock=true,healthyValue=false,stopped=true,launched=false,die=false,cancel=false;int failAt=0;unsigned waits=0,released=0;std::vector<std::optional<Proof>> proofs;std::size_t index=0;std::string policy="configured",healthPolicy="confirmed";
 void call(const char*n){calls.push_back(n);if(failAt&&calls.size()==failAt)throw Error(5,"fixture-access-denied",true,5);}
 bool acquire(unsigned){call("lock");return lock;}void release(){calls.push_back("release");++released;}
 bool healthy(){call("healthy");return healthyValue;}void applyAffinity(){call("affinity");}
 std::string healthySensorPolicy(const Request&){return healthPolicy;}
 std::wstring resolve(const std::wstring&){call("resolve");return L"C:\\HWiNFO64\\HWiNFO64.exe";}
 void prepare(const std::wstring&,const Request&){call("preflight");}void stop(){call("stop");stopped=true;}
 void configure(const std::wstring&,const Request&){call("configure");}std::string sensorBudget(const std::wstring&){call("sensor-budget");return policy;}
 void assertStopped(){call("assert-stopped");if(!stopped)throw Error(4,"fixture-producer-appeared");}void launch(const std::wstring&){call("launch");launched=true;}
 bool exited(){call("exit-check");return die;}
 std::optional<Proof> proof(unsigned){call("proof");if(index<proofs.size())return proofs[index++];return {};}
 bool wait(unsigned){call("wait");++waits;return !cancel;}
};
struct JournalFake {
 std::wstring destination=L"C:\\Fixture\\HWiNFO64.INI";std::map<std::string,std::optional<std::string>> files;std::map<std::wstring,SensorState> states;std::vector<std::string> writes;std::vector<std::pair<std::wstring,SensorState>> registryWrites;bool producer=false,failRestore=false,failCommit=false,verifiedValue=true;int failAfter=-1;bool firstFailure=false;
 JournalFake(){for(const auto&g:sensorGroups())states[g]={true,1};}
 std::optional<std::string> read(const std::string&kind){return files[kind];}
 void write(const std::string&kind,const std::string&bytes,const std::optional<std::string>&expected){require(files[kind]==expected,"test observed write conflict");const auto j=SensorJson::parse(bytes);if(failCommit&&kind=="last"&&j.value("phase",std::string())=="committed"){failCommit=false;throw Error(8,"test commit-phase refusal");}files[kind]=bytes;writes.push_back(kind+":"+j.value("phase",std::string("first")));}
 void legacyWrite(const std::wstring&path,const std::string&bytes,bool keep=false){const auto tag=path.substr(destination.size()+20);const auto kind=ascii(tag.substr(0,tag.size()-5));if(keep&&files[kind])return;files[kind]=bytes;writes.push_back(kind+":legacy");}
 bool verified(){return verifiedValue;}bool enabled(){return true;}void stopped(){if(producer)throw Error(4,"test producer appeared");}
 SensorState state(const std::wstring&g){return states[g];}
 void set(const std::wstring&g,SensorState value){if(failRestore&&value.value!=0)throw Error(8,"test restoration refused");states[g]=value;registryWrites.push_back({g,value});if(failAfter>=0&&!firstFailure&&registryWrites.size()==static_cast<size_t>(failAfter)){firstFailure=true;throw Error(8,"test post-write failure");}}
};
// YMCC_LEGACY_SENSOR_BUDGET
static std::string journalBudget(JournalFake&f){
#ifdef YMCC_LEGACY_JOURNAL
 return legacySensorBudget(f,L"C:\\Fixture\\HWiNFO64.exe",f.destination);
#else
 return sensorJournalBudget(f);
#endif
}
static int sensorJournalTests(const char* output){Json checks=Json::array();auto test=[&](const std::string&name,const std::function<void()>&fn){try{fn();checks.push_back({{"name",name},{"ok",true}});}catch(const std::exception&e){checks.push_back({{"name",name},{"ok",false},{"error",e.what()}});}};const auto need=[](bool v){require(v,"sensor ownership contract assertion failed");};
 const auto receipt=[](JournalFake&f,const std::string&phase){auto j=SensorJson::parse(sensorReceipt(f.states));j["note"]="preserve unknown metadata";j["values"]["F0000101_0"]["comment"]="keep";if(!phase.empty())j["phase"]=phase;j["owner"]="YMCC.HWiNFO.SensorBudget";j["iniPath"]=sensorUtf8(f.destination);return j;};
 for(const auto&phase:std::vector<std::string>{"","prepared","rollback-pending"})test("prior-debt-"+phase,[&]{JournalFake f;const auto prior=receipt(f,phase);f.files["last"]=prior.dump();for(auto&[k,v]:f.states)v={true,0};need(journalBudget(f)=="configured");const auto last=SensorJson::parse(*f.files["last"]);need(last.at("phase")=="committed"&&last.at("values").at("F0000101_0").at("Value")==1&&last.at("values").at("F0000101_0").at("comment")=="keep"&&last.at("note")==prior["note"]);need(f.registryWrites.size()==130);need(SensorJson::parse(*f.files["original"])["values"]["F0000101_0"]["Value"]==1);});
 test("foreign-state-preserves-pending-bytes",[&]{JournalFake f;f.files["last"]=receipt(f,"prepared").dump();for(auto&[k,v]:f.states)v={true,0};f.states[L"F0000101_31"]={true,17};const auto files=f.files;need(journalBudget(f).rfind("unconfirmed",0)==0&&f.registryWrites.empty()&&f.files["last"]==files.at("last")&&f.writes.empty());});
 test("missing-first-recovers-only-proven-last",[&]{JournalFake f;f.files["last"]=receipt(f,"committed").dump();for(auto&[k,v]:f.states)v={true,0};f.states[L"F0000302_3"]={true,1};need(journalBudget(f)=="configured");need(SensorJson::parse(*f.files["original"])["values"]["F0000101_0"]["Value"]==1);});
 test("all-disabled-still-retains-pending-responsibility",[&]{JournalFake f;f.files["last"]=receipt(f,"rollback-pending").dump();for(auto&[k,v]:f.states)v={true,0};f.failRestore=true;const auto before=f.files;need(journalBudget(f).rfind("unconfirmed",0)==0&&f.files["last"]==before.at("last")&&f.writes.empty());});
 for(const auto&bad:std::vector<std::string>{"{bad","null","[]","{\"schema\":2}","{\"schema\":1,\"schema\":1}"})test("invalid-receipt-no-mutation-"+bad,[&]{JournalFake f;f.files["last"]=bad;need(journalBudget(f).rfind("unconfirmed",0)==0&&f.writes.empty()&&f.registryWrites.empty()&&f.files["last"]==std::optional<std::string>{bad});});
 for(const auto&key:std::vector<std::string>{"schema","owner","iniPath","phase","values"})test("wrong-journal-field-"+key,[&]{JournalFake f;auto j=receipt(f,"prepared");j[key]=false;f.files["last"]=j.dump();need(journalBudget(f).rfind("unconfirmed",0)==0&&f.writes.empty()&&f.registryWrites.empty());});
 test("write-rollback-debt-survives-retry",[&]{JournalFake f;f.failAfter=2;f.failRestore=true;need(journalBudget(f)=="unconfirmed-rollback-deferred");const auto pending=SensorJson::parse(*f.files["last"]);need(pending.at("phase")=="rollback-pending"&&pending.at("values").at("F0000101_0").at("Value")==1);f.failAfter=-1;f.failRestore=false;need(journalBudget(f)=="configured");need(SensorJson::parse(*f.files["last"])["values"]["F0000101_0"]["Value"]==1);});
 test("phase-commit-failure-does-not-erase-before",[&]{JournalFake f;f.failCommit=true;need(journalBudget(f)=="unconfirmed-rolled-back");need(SensorJson::parse(*f.files["last"])["phase"]=="rolled-back");need(f.states[L"F0000101_0"].value==1);});
 test("corrupt-first-preserved-without-sensor-write",[&]{JournalFake f;f.files["original"]="{bad";need(journalBudget(f).rfind("unconfirmed",0)==0&&f.files["original"]==std::optional<std::string>{"{bad"}&&f.registryWrites.empty());});
 test("healthy-pending-is-readonly-unconfirmed",[&]{JournalFake f;f.files["last"]=receipt(f,"").dump();const auto before=f.files;need(sensorJournalHealth(f).rfind("unconfirmed",0)==0&&f.writes.empty()&&f.registryWrites.empty()&&f.files["last"]==before.at("last"));});
 test("healthy-committed-is-readonly",[&]{JournalFake f;f.files["last"]=receipt(f,"committed").dump();f.files["original"]=sensorReceipt(f.states,L"F0000101_");f.files["cstate-original"]=sensorReceipt(f.states,L"F0000302_");f.files["rtss-original"]=sensorReceipt(f.states,L"F00F5000_");need(sensorJournalHealth(f)=="confirmed"&&f.writes.empty()&&f.registryWrites.empty());});
 test("legacy32-last-and-signed-DWORD",[&]{JournalFake f;auto j=SensorJson::parse(sensorReceipt(f.states,L"F0000101_"));j["values"]["F0000101_0"]["Value"]=-1;const auto valid=parseSensorReceipt(j.dump(),f.destination,"last");need(sensorState(valid["values"]["F0000101_0"]).value==0xffffffffu);});
 test("duplicate-unicode-casefold-rejected",[&]{bool rejected=false;try{JournalFake f;auto text=receipt(f,"committed").dump();text.pop_back();text+=",\"Ä\":1,\"ä\":2}";parseSensorReceipt(text,f.destination,"last");}catch(...){rejected=true;}need(rejected);});
 const auto proof=[](unsigned n){return Proof{n,{1,2,3,4,5,6,7,8,9,10,11}};};Request q;
 test("healthy-data-and-config-debt-code16",[&]{Fake f;f.healthyValue=true;f.healthPolicy="unconfirmed-prior-debt";const auto result=transaction(f,q);need(result.code==16&&result.dataReady&&!f.launched&&std::find(f.calls.begin(),f.calls.end(),"stop")==f.calls.end());const auto wire=asJson(result);need(!wire["ok"].get<bool>()&&wire["dataReady"]==true&&wire["configurationConfirmed"]==false);});
 test("new-producer-with-debt-code16",[&]{Fake f;f.policy="unconfirmed-rollback-deferred";f.proofs={proof(1),proof(2)};const auto result=transaction(f,q);need(result.code==16&&result.dataReady&&f.launched&&f.waits==1);});
 test("unconfirmed-debt-does-not-mask-no-data",[&]{Fake f;f.policy="unconfirmed-rollback-deferred";const auto result=transaction(f,q);need(result.code==7&&!result.dataReady&&f.waits==30);});
 test("confirmed-producer-code0-explicit",[&]{Fake f;f.proofs={proof(1),proof(2)};const auto result=transaction(f,q);need(result.code==0&&result.dataReady&&asJson(result)["configurationConfirmed"]==true);});

 test("elevated-code16-parent-readable-keeps-debt",[&]{struct Parent{int step=0;bool interrupted(){return false;}std::optional<Proof> proof(unsigned){return Proof{static_cast<unsigned long long>(++step),{1,2,3,4,5,6,7,8,9,10,11}};}bool wait(unsigned){return true;}}p;const auto r=confirmElevatedProducer(p,0,16);need(r.code==16&&r.dataReady&&!asJson(r)["configurationConfirmed"].get<bool>());});
 test("elevated-code16-parent-unreadable-not-dataready",[&]{struct Parent{bool interrupted(){return false;}std::optional<Proof> proof(unsigned){return {};}bool wait(unsigned){return true;}}p;const auto r=confirmElevatedProducer(p,0,16);need(r.code==7&&!r.dataReady);});

 for(const auto&mutation:std::vector<std::string>{"bad-image","bad-range","bad-bool","missing-state","nonzero-absent","incomplete-family","foreign-group","foreign-path","unknown-phase","future-schema"})test("receipt-reject-"+mutation,[&]{JournalFake f;auto j=receipt(f,"prepared");if(mutation=="bad-image")j["executableSha256"]="wrong";if(mutation=="bad-range")j["values"]["F0000101_0"]["Value"]=4294967296ULL;if(mutation=="bad-bool")j["values"]["F0000101_0"]["Exists"]=1;if(mutation=="missing-state")j["values"]["F0000101_0"].erase("Value");if(mutation=="nonzero-absent")j["values"]["F0000101_0"]["Exists"]=false;if(mutation=="incomplete-family")j["values"].erase("F0000302_31");if(mutation=="foreign-group")j["values"]["F0000FF0_0"]={{"Exists",true},{"Value",1}};if(mutation=="foreign-path")j["iniPath"]="C:\\Other\\HWiNFO64.INI";if(mutation=="unknown-phase")j["phase"]="done";if(mutation=="future-schema")j["schema"]=2;f.files["last"]=j.dump();need(journalBudget(f).rfind("unconfirmed",0)==0&&f.writes.empty()&&f.registryWrites.empty()&&f.files["last"]==std::optional<std::string>{j.dump()});});

 test("unsupported-new-image-cannot-clear-old-debt",[&]{JournalFake f;f.files["last"]=receipt(f,"prepared").dump();f.verifiedValue=false;const auto raw=f.files["last"];need(journalBudget(f).rfind("unconfirmed",0)==0&&f.files["last"]==raw&&f.registryWrites.empty()&&f.writes.empty());});
 test("absent-original-state-restores-as-absent",[&]{JournalFake f;f.states[L"F0000101_0"]={false,0};f.files["last"]=receipt(f,"rollback-pending").dump();for(auto&[k,v]:f.states)v={true,0};need(journalBudget(f)=="configured");bool removed=false;for(const auto&[g,v]:f.registryWrites)if(g==L"F0000101_0"&&!v.exists)removed=true;need(removed&&!SensorJson::parse(*f.files["original"])["values"]["F0000101_0"]["Exists"].get<bool>());});

 test("healthy-missing-first-keeps-config-unconfirmed",[&]{JournalFake f;f.files["last"]=receipt(f,"committed").dump();need(sensorJournalHealth(f)=="unconfirmed-missing-original"&&f.writes.empty()&&f.registryWrites.empty());for(auto&[g,v]:f.states)v={true,0};need(journalBudget(f)=="already-configured"&&f.registryWrites.empty()&&f.files["original"].has_value()&&SensorJson::parse(*f.files["original"])["values"]["F0000101_0"]["Value"]==1);need(sensorJournalHealth(f)=="confirmed");});
 test("missing-last-does-not-turn-first-into-no-debt",[&]{JournalFake f;f.files["original"]=sensorReceipt(f.states,L"F0000101_");const auto before=f.files["original"];need(sensorJournalHealth(f)=="unconfirmed-missing-last"&&journalBudget(f)=="unconfirmed-missing-last"&&f.registryWrites.empty()&&f.writes.empty()&&f.files["original"]==before);});
 JournalFake sample;const auto sampleResult=journalBudget(sample);Json examples={{"result",sampleResult},{"destination",sensorUtf8(sample.destination)}};for(const auto&[kind,bytes]:sample.files)if(bytes)examples[kind]=SensorJson::parse(*bytes);std::ofstream(std::filesystem::path(output).parent_path()/"INTEROP.json")<<examples.dump(2);
 bool ok=true;for(const auto&c:checks)if(!c["ok"].get<bool>())ok=false;std::ofstream(output)<<Json({{"ok",ok},{"checks",checks},{"scope","actual native journal transaction and recovery transaction; registry/process/store memory ports only; not real HWiNFO"}}).dump(2);return ok?0:1;
}


static int sensorInteropTests(const char*input,const char*output){Json samples;std::ifstream(input)>>samples;Json checks=Json::array();bool ok=true;for(const auto&sample:samples){bool passed=false;std::string error;try{JournalFake f;f.destination=sensorWide(sample.at("destination"));f.files["last"]=sample.at("raw").get<std::string>();const auto parsed=parseSensorReceipt(*f.files["last"],f.destination,"last");for(auto&[k,v]:f.states)v={true,0};if(!sensorDebtPending(parsed))f.states[L"F0000302_3"]={true,1};const auto result=sensorJournalBudget(f);const auto committed=SensorJson::parse(*f.files["last"]);passed=result=="configured"&&committed["phase"]=="committed"&&committed["note"]=="ps note preserved"&&committed["values"]["F0000101_0"]["comment"]=="ps node preserved";if(sensorDebtPending(parsed))passed=passed&&sensorState(committed["values"]["F0000101_0"]).value==0xffffffffu;else passed=passed&&sensorState(SensorJson::parse(*f.files["original"])["values"]["F0000101_0"]).value==0xffffffffu;}catch(const std::exception&e){error=e.what();}checks.push_back({{"phase",sample.at("phase")},{"ok",passed},{"error",error}});ok=ok&&passed;}std::ofstream(output)<<Json({{"ok",ok},{"checks",checks},{"scope","actual PS-emitted phase records consumed by native journal with fake registry/process/file ports"}}).dump(2);return ok?0:1;}
// Root-only selection: real owned filesystem -> original acquisition -> managedIni
// -> Operations::planned. It never acquires the owner or controls a producer.
static int rootAcquisitionTests(const char* output,const char* selection=nullptr){
 const auto evidence=fs::absolute(fs::path(output)).parent_path(),owned=evidence/L"owned-root-files";
 if(fs::exists(owned))throw std::runtime_error("root fixture output already exists");fs::create_directories(owned);
 Json checks=Json::array();
 auto check=[&](const std::string&name,bool ok,const std::string&state="",const std::string&error=""){checks.push_back({{"name",name},{"ok",ok},{"state",state},{"error",error}});if(!ok)std::cerr<<name<<": "<<error<<'\n';};
 auto write=[](const fs::path&p,const std::string&s){std::ofstream f(p,std::ios::binary);f.write(s.data(),s.size());if(!f)throw std::runtime_error("owned write failed");};
 auto read=[](const fs::path&p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
 const std::string yes=R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}})";
 const std::string no=R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})";
 const std::string existing="[Settings]\r\nSensorInterval=1500\r\nSnapshotPolling=1\r\nUserKeep=unchanged\r\n[Foreign]\r\nProof=original\r\n";
 unsigned serial=0;
 auto exercise=[&](const std::string&name,const fs::path&root,const std::optional<std::string>&original,std::optional<bool>expected,const char*expectedState,bool verifyOriginal=true){
  bool readOk=false,planOk=false,untouched=false;std::string state,error;
  try{
#ifdef YMCC_ROOT_READER_STATE
   AcquisitionRead observed;auto actual=acquisition(root.wstring(),&observed);state=observed.state;readOk=actual==expected&&state==expectedState;
#else
   auto actual=acquisition(root.wstring());state="baseline-reader";readOk=actual==expected;
#endif
  }catch(const std::exception&e){error=e.what();}
  try{Operations ops(root.wstring());Request request;request.powerControl=root.wstring();
   const auto planned=decodeIni(ops.planned(request,std::optional<std::string>{existing}));std::map<std::wstring,std::wstring> values;for(const auto&entry:entries(planned))values[entry.identity]=entry.value;
   planOk=values[L"settings\x1fsnapshotpolling"]==(expected&&!*expected?L"0":L"1")&&values[L"settings\x1fsensorinterval"]==L"1500"&&values[L"settings\x1fuserkeep"]==L"unchanged"&&values[L"foreign\x1fproof"]==L"original"&&values[L"settings\x1fpresentmon"]==L"1"&&values[L"settings\x1fnvclockmeasuring"]==L"0"&&values[L"settings\x1fusbc_pd"]==L"0";
   const auto fresh=ops.planned(request,std::optional<std::string>{});const bool hasSnapshot=fresh.find("SnapshotPolling=")!=std::string::npos;planOk=planOk&&(hasSnapshot==expected.has_value());
  }catch(const std::exception&e){if(!error.empty())error+="; ";error+=e.what();}
  if(!verifyOriginal)untouched=true;else if(original)untouched=read(root/L"yeman-settings.json")==*original;else untouched=!fs::exists(root/L"yeman-settings.json")||fs::is_directory(root/L"yeman-settings.json");
  check(name,readOk&&planOk&&untouched,state,error);
 };
 auto run=[&](const std::string&name,const std::optional<std::string>&bytes,std::optional<bool>expected={},const char*state="default:corrupt"){
  auto root=owned/std::to_wstring(++serial);fs::create_directory(root);if(bytes)write(root/L"yeman-settings.json",*bytes);exercise(name,root,bytes,expected,state);return root;
 };
 auto finish=[&](){bool ok=true;for(const auto&c:checks)ok=ok&&c["ok"].get<bool>();Json result={{"ok",ok},{"checks",checks},{"rootOnly",true},{"scope","Original native consumer selection; real owned files and original acquisition/managedIni/planned; no producer/process/registry/transaction control"}};std::ofstream(output)<<result.dump(2)<<'\n';std::cout<<result.dump(2)<<'\n';return ok?0:1;};


#ifdef YMCC_ROOT_IO_PORTS
 if(selection&&std::string(selection)=="io-boundaries"){
  using namespace ymccRootIoProbe;
  const auto root=owned/L"io-root";fs::create_directory(root);const auto path=root/L"yeman-settings.json";
  auto reset=[&](Mode mode){probe={};probe.mode=mode;probe.root=path.wstring();};
  for(const auto mode:{Mode::DenyOpen,Mode::ReadError,Mode::ShortRead,Mode::ChangedMetadata}){
   write(path,no);reset(mode);exercise("injected-read-fault-"+std::to_string(static_cast<int>(mode)),root,no,{},"default:read-fault");
   check("fault-uses-exactly-three-explicit-reads-"+std::to_string(static_cast<int>(mode)),probe.rootOpens==3&&probe.injected==3);reset(Mode::None);
   exercise("explicit-fault-release-retry-"+std::to_string(static_cast<int>(mode)),root,no,false,"configured");
  }
  for(const auto mode:{Mode::AttemptTruncate,Mode::AttemptReplace}){
   write(path,no);reset(mode);probe.replacement=(root/L"replacement.json").wstring();write(probe.replacement,yes);
   exercise("open-handle-preserves-preimage-"+std::to_string(static_cast<int>(mode)),root,no,false,"configured");
   check("actual-concurrent-mutation-denied-"+std::to_string(static_cast<int>(mode)),probe.rootOpens==3&&probe.injected==3&&probe.deniedMutations==3);reset(Mode::None);
   if(mode==Mode::AttemptReplace){check("explicit-replace-after-reader-close",MoveFileExW((root/L"replacement.json").c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE);}
   else write(path,yes);
   exercise("explicit-mutated-file-retry-"+std::to_string(static_cast<int>(mode)),root,yes,true,"configured");
  }
  write(path,"{bad");reset(Mode::None);for(unsigned i=0;i<3;++i)exercise("same-bad-bytes-explicit-"+std::to_string(i),root,std::string("{bad"),{},"default:corrupt");
  check("bad-root-no-internal-retry-loop",probe.rootOpens==9&&probe.primaryReads==9&&probe.allReads==18&&probe.injected==0);
  write(path,yes);exercise("bad-bytes-repaired-next-explicit-call",root,yes,true,"configured");
  reset(Mode::None);return finish();
 }
#endif
 if(selection){
  if(std::string(selection)=="decoded-duplicate")run("selected-discarded-decoded-duplicate",R"({"unknown":[{"a":1,"\u0061":2}],"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})");
  else if(std::string(selection)=="future-root")run("selected-future-root",R"({"schemaVersion":2,"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})",{},"default:future");
  else throw std::runtime_error("unknown root selection");return finish();
 }
 run("missing",{}, {},"default:missing");run("valid-true",yes,true,"configured");run("valid-false",no,false,"configured");
 run("bom",std::string("\xef\xbb\xbf")+yes,true,"configured");
 for(const auto&s:std::vector<std::string>{"{}",R"({"rtss":null})",R"({"rtss":{}})",R"({"rtss":{"monitorAcquisition":null}})"})run("absent-"+std::to_string(serial),s,{},"default:absent");
 run("disabled",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":false}}})",{},"default:disabled");
 run("schema-and-baseline",R"({"schemaVersion":1,"configurationBaseline":{"id":"ymcc-settings","revision":1,"legacyImported":true},"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})",false,"configured");
 run("legacy-zero",R"({"schemaVersion":0})",{},"default:absent");
 for(const auto&s:std::vector<std::string>{"","{","null","[]","true","123",R"("text")",R"({"schemaVersion":"1"})",R"({"schemaVersion":1.0})",R"({"schemaVersion":-1})",R"({"configurationBaseline":[]})",R"({"configurationBaseline":{}})",R"({"configurationBaseline":{"id":"ymcc-settings","revision":1.0,"legacyImported":true}})",R"({"configurationBaseline":{"id":"ymcc-settings","revision":1,"legacyImported":false}})",R"({"rtss":false})",R"({"rtss":[]})",R"({"rtss":{"monitorAcquisition":[]}})",R"({"rtss":{"monitorAcquisition":false}})",R"({"rtss":{"monitorAcquisition":{}}})",R"({"rtss":{"monitorAcquisition":{"version":1.0,"enabled":false}}})",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":0}}})",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true}}})",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":false,"snapshotPolling":0}}})",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":"true"}}})",R"({"a":1,"a":2})",R"({"rtss":{},"rt\u0073s":{}})",R"({"unknown":{"a":1,"\u0061":2}})",R"({"unknown":[{"x":1,"x":2}]})",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false,"discard":{"q":1,"q":2}}}})",R"({"unknown":"\ud800"})",R"({"unknown":1e999})","{} trailing"})run("corrupt-"+std::to_string(serial),s);
 for(const auto&s:std::vector<std::string>{R"({"schemaVersion":2})",R"({"configurationBaseline":{"id":"ymcc-settings","revision":2,"legacyImported":true}})",R"({"configurationBaseline":{"id":"foreign","revision":1,"legacyImported":true}})",R"({"rtss":{"monitorAcquisition":{"version":2,"enabled":true,"snapshotPolling":true}}})"})run("future-"+std::to_string(serial),s,{},"default:future");
 run("unknown-case-distinct",R"({"unknown":{"A":1,"a":2}})",{},"default:absent");
 run("large-valid",std::string(5000,' ')+yes,true,"configured");
 run("future-root-cannot-authorize-override",R"({"schemaVersion":2,"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})",{},"default:future");
 run("future-baseline-cannot-authorize-override",R"({"configurationBaseline":{"id":"ymcc-settings","revision":2,"legacyImported":true},"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})",{},"default:future");
 run("unknown-duplicate-cannot-authorize-override",R"({"unknown":{"a":1,"\u0061":2},"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})");
 run("large-unknown-duplicate-cannot-authorize-override",std::string(5000,' ')+R"({"unknown":[{"a":1,"\u0061":2}],"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}})");
 run("known-decoded-duplicate-cannot-authorize-override",R"({"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false,"snapshot\u0050olling":true}}})");

 run("large-discarded-duplicate",std::string(5000,' ')+R"({"unknown":{"a":1,"\u0061":2}})");
 run("invalid-utf8",std::string("{\"x\":\"")+char(0xff)+"\"}",{},"default:encoding");
 run("utf16",std::string("\xff\xfe{\0}\0",6),{},"default:encoding");
 run("embedded-zero",std::string("{}\0",3));
 const auto nested=[&](unsigned count){return std::string("{\"unknown\":")+std::string(count,'[')+"0"+std::string(count,']')+"}";};
 run("depth64",nested(63),{},"default:absent");run("depth65",nested(64),{},"default:depth");
 run("exact-four-MiB",std::string(4194304-yes.size(),' ')+yes,true,"configured");run("over-four-MiB",std::string(4194305-yes.size(),' ')+yes,{},"default:oversize");
 auto retry=run("truncated-before-retry","{",{},"default:corrupt");write(retry/L"yeman-settings.json",no);exercise("truncated-explicit-retry",retry,no,false,"configured");
 auto locked=owned/std::to_wstring(++serial);fs::create_directory(locked);write(locked/L"yeman-settings.json",no);
 {Handle hold(CreateFileW((locked/L"yeman-settings.json").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr));if(!hold)throw std::runtime_error("owned lock failed");exercise("locked-read",locked,no,{},"default:read-fault",false);}
 check("lock-source-byte-proof-after-release",read(locked/L"yeman-settings.json")==no);
 exercise("lock-explicit-retry",locked,no,false,"configured");
 auto directory=owned/std::to_wstring(++serial);fs::create_directory(directory);fs::create_directory(directory/L"yeman-settings.json");exercise("root-is-directory",directory,{}, {},"default:read-fault");
 auto parentFile=owned/L"parent-file";write(parentFile,"inert");exercise("parent-is-file",parentFile,{}, {},"default:read-fault");
 auto parentMissing=owned/L"missing-parent";exercise("parent-is-missing",parentMissing,{}, {},"default:missing");
 // An owned junction is sufficient to verify reparse refusal without symlink privilege.
 auto junction=[&](const fs::path&link,const fs::path&target){fs::create_directory(link);std::wstring substitute=L"\\??\\"+fs::absolute(target).wstring(),print=fs::absolute(target).wstring();std::vector<unsigned char> bytes(16+(substitute.size()+print.size()+2)*2);auto put16=[&](std::size_t at,WORD v){std::memcpy(bytes.data()+at,&v,2);};DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;std::memcpy(bytes.data(),&tag,4);put16(4,static_cast<WORD>(bytes.size()-8));put16(8,0);put16(10,static_cast<WORD>(substitute.size()*2));put16(12,static_cast<WORD>((substitute.size()+1)*2));put16(14,static_cast<WORD>(print.size()*2));std::memcpy(bytes.data()+16,substitute.c_str(),(substitute.size()+1)*2);std::memcpy(bytes.data()+16+(substitute.size()+1)*2,print.c_str(),(print.size()+1)*2);Handle h(CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));DWORD got=0;if(!h||!DeviceIoControl(h.h,FSCTL_SET_REPARSE_POINT,bytes.data(),static_cast<DWORD>(bytes.size()),nullptr,0,&got,nullptr))throw std::runtime_error("owned junction failed");};
 auto target=owned/L"junction-target";fs::create_directory(target);write(target/L"yeman-settings.json",no);
 auto link=owned/L"parent-junction";junction(link,target);exercise("reparse-parent",link,no,{},"default:read-fault");
 auto fileLink=owned/L"file-junction";fs::create_directory(fileLink);junction(fileLink/L"yeman-settings.json",target);exercise("reparse-leaf",fileLink,{}, {},"default:read-fault");check("junction-target-untouched",read(target/L"yeman-settings.json")==no);
 auto debts=owned/L"debt-sentinels";fs::create_directory(debts);write(debts/L"yeman-settings.json","{");const std::vector<std::wstring> names={L"HWiNFO64.INI",L"HWiNFO64.INI.ymcc-original",L"HWiNFO64.INI.ymcc-last",L"sensor-registry-receipt.json"};for(const auto&n:names)write(debts/n,"original-sentinel");
 exercise("bad-root-does-not-clear-debt",debts,std::string("{"),{},"default:corrupt");bool same=true;for(const auto&n:names)same=same&&read(debts/n)=="original-sentinel";check("all-INI-and-receipt-bytes-preserved",same);
 for(const auto&bad:std::vector<std::string>{"bad", "[Settings]\nSensorInterval=1500\nSensorInterval=2000\n"}){bool refused=false;try{Operations ops(debts.wstring());Request q;(void)ops.planned(q,std::optional<std::string>{bad});}catch(const Error&){refused=true;}check("corrupt-existing-INI-still-refused-"+std::to_string(checks.size()),refused);}
 return finish();
}

int main(int argc,char**argv){if(argc==3&&std::string(argv[1])=="--root-acquisition")return rootAcquisitionTests(argv[2]);if(argc==4&&std::string(argv[1])=="--root-acquisition")return rootAcquisitionTests(argv[2],argv[3]);if(argc==4&&std::string(argv[1])=="--journal-interop")return sensorInteropTests(argv[2],argv[3]);if(argc==3&&std::string(argv[1])=="--sensor-journal")return sensorJournalTests(argv[2]);if(argc!=4)return 2;Json inputs;std::ifstream(argv[1])>>inputs;Json plans=Json::array(),checks=Json::array();auto check=[&](const char*n,bool ok){checks.push_back({{"name",n},{"ok",ok}});if(!ok)std::cerr<<n<<'\n';};
 for(const auto&c:inputs){try{const auto source=c.contains("sourceHex")?decodeIni(bytesFromHex(c["sourceHex"])):managedIni(c.contains("snapshotPolling")?std::optional<bool>{c["snapshotPolling"].get<bool>()}:std::optional<bool>{});const auto target=c.contains("destinationHex")?std::optional<Ini>{decodeIni(bytesFromHex(c["destinationHex"]))}:std::optional<Ini>{};const auto plan=mergeIni(source,target,c.value("period",0U));plans.push_back({{"name",c["name"]},{"code",0},{"hex",hex(plan)}});}catch(const Error&e){plans.push_back({{"name",c["name"]},{"code",e.code},{"error",e.what()}});}}
 Request q;q.powerControl=L"C:\\YMCC\\PowerControl";Proof p{100,{1,2,3,4,5,6,7,8,9,10,11}},next=p;next.poll=101;
 {Fake o;o.healthyValue=true;auto r=transaction(o,q);check("healthy start has no config writes, stop, launch or proof loop",r.code==0&&o.calls==std::vector<std::string>{"lock","healthy","affinity","release"}&&o.released==1);}
 {Fake o;o.healthyValue=true;auto requested=q;requested.period=1500;auto r=transaction(o,requested);check("explicit mismatching period on healthy start is deferred using general health without writes",r.code==12&&r.dataReady&&o.calls==std::vector<std::string>{"lock","healthy","release"});}
 {Fake o;o.lock=false;auto r=transaction(o,q);check("busy lock performs no process or config work",r.code==11&&o.calls==std::vector<std::string>{"lock"}&&o.released==0);}
 {Fake o;o.proofs={p,next};auto r=transaction(o,q);check("recovery validates before stop, configures after stop, then confirms advancing producer",r.code==0&&o.calls==std::vector<std::string>{"lock","healthy","resolve","preflight","stop","configure","sensor-budget","assert-stopped","launch","exit-check","proof","wait","exit-check","proof","release"}&&o.waits==1&&o.released==1);}
 {Fake o;o.failAt=4;auto r=transaction(o,q);check("bad preflight preserves running upstream and releases recovery lock",r.code==5&&!o.launched&&std::find(o.calls.begin(),o.calls.end(),"stop")==o.calls.end()&&o.released==1&&r.needsElevation);}
 {Fake o;o.failAt=5;auto r=transaction(o,q);check("stop failure never configures or launches",r.code==5&&std::find(o.calls.begin(),o.calls.end(),"configure")==o.calls.end()&&!o.launched&&o.released==1);}
 {Fake o;o.failAt=6;auto r=transaction(o,q);check("configuration failure does not launch and returns elevation need after releasing lock",r.code==5&&!o.launched&&r.needsElevation&&o.released==1);}
 {Fake o;o.stopped=false;auto r=transaction(o,q);check("complete empty proof loop is bounded at 30 observations",r.code==7&&o.waits==30&&o.index==0&&o.released==1);}
 {Fake o;o.proofs.assign(30,p);auto r=transaction(o,q);check("stalled proof cannot be declared recovered",r.code==14&&o.waits==30&&o.released==1);}
 {Fake o;auto requested=q;requested.period=2000;auto r=transaction(o,requested);check("unconfirmed requested period returns its explicit code",r.code==13&&o.waits==30);}
 {Fake o;o.die=true;auto r=transaction(o,q);check("started process exit prevents false confirmation",r.code==6&&o.waits==0&&o.released==1);}
 {Fake o;o.cancel=true;auto r=transaction(o,q);check("application exit interrupts observation without terminating new producer",r.code==15&&o.launched&&o.waits==1&&o.released==1);}
 {Fake o;auto changed=next;changed.layout[3]=55;o.proofs={p,changed};auto r=transaction(o,q);check("different layout cannot count as continuous production",r.code==14&&o.waits==30);}
 {Fake o;o.proofs={p,std::nullopt,next};auto r=transaction(o,q);check("unreadable gap requires fresh consecutive producer proof",r.code==14&&o.waits==30);}
 {Fake o;o.proofs={p,p,next};auto r=transaction(o,q);check("same producer second does not count as progress",r.code==0&&o.waits==2);}
 {Fake o;auto requested=q;requested.restart=true;o.healthyValue=true;o.proofs={p,next};auto r=transaction(o,requested);check("explicit restart actually restarts healthy producer",r.code==0&&o.launched&&std::find(o.calls.begin(),o.calls.end(),"healthy")==o.calls.end());}
 check("only fixed unused groups are allowed",sensorGroups().size()==65&&sensorGroupAllowed(L"F0000101_31")&&sensorGroupAllowed(L"F0000302_0")&&sensorGroupAllowed(L"F00F5000_0")&&!sensorGroupAllowed(L"F0000FF0_0")&&!sensorGroupAllowed(L"F0000101_32")&&!sensorGroupAllowed(L"F0000101_01")&&!sensorGroupAllowed(L"..\\Enabled"));
 {int code=-1;check("ordinary app command line is not captured",!tryCommandLine(L"YMCC.exe --dev http://127.0.0.1",q.powerControl,code));check("malformed private mode exits before normal app startup",tryCommandLine(L"YMCC.exe --hwinfo-manage restart --period 999",q.powerControl,code)&&code==8);check("duplicate private flag is rejected",tryCommandLine(L"YMCC.exe --hwinfo-manage restart --period 1000 --period 2000",q.powerControl,code)&&code==8);}
 {auto request=q;request.executable=L"C:\\portable & edition!\\HWiNFO64.exe";request.powerControl=L"C:\\YMCC with spaces\\PowerControl\\";request.elevated=true;const auto cmd=quoteArgument(L"C:\\YMCC\\YeManCC.exe")+L" "+arguments(request);int n=0;auto args=CommandLineToArgvW(cmd.c_str(),&n);bool ok=args&&n==10&&std::wstring(args[6])==request.powerControl&&std::wstring(args[9])==request.executable;if(args)LocalFree(args);check("same-image elevation arguments roundtrip Windows quoting",ok);}
 bool ok=true;for(const auto&c:checks)ok=ok&&c["ok"].get<bool>();std::ofstream(argv[2])<<plans.dump(2)<<'\n';std::ofstream(argv[3])<<Json({{"ok",ok},{"checks",checks},{"scope","Production native INI and recovery state machine with inert operations; no hardware start/stop or CPU timing."}}).dump(2)<<'\n';return ok?0:1;
}
