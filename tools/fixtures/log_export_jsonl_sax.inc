struct AllocationTrace {unsigned long long calls=0,bytes=0;};
static AllocationTrace allocationTrace;static bool allocationMeasure=false;
void* operator new(std::size_t n){if(allocationMeasure){++allocationTrace.calls;allocationTrace.bytes+=n;}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
// ACTUAL_BEFORE_INSPECTOR
// ACTUAL_CURRENT_INSPECTOR
static void saxPut(const fs::path& path,const std::string& value){fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(value.data(),static_cast<std::streamsize>(value.size()));file.close();if(!file)throw std::runtime_error("private seed failed");}
static std::string saxBytes(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static json saxMeasured(bool before,const fs::path& path,AllocationTrace& trace){allocationTrace={};allocationMeasure=true;auto value=before?beforeStagedFileEvidence(path,"native-lifecycle.log"):currentStagedFileEvidence(path,"native-lifecycle.log");allocationMeasure=false;trace=allocationTrace;return value;}
static json saxTrace(const AllocationTrace& t){return {{"calls",t.calls},{"requestedBytes",t.bytes}};}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 2;const fs::path root(argv[1]);if(fs::exists(root))return 3;fs::create_directories(root);const auto file=root/L"native-lifecycle.log";
 saxPut(file,"{}\n");DWORD cold=0,warm=0,beforeHandles=0,afterHandles=0;GetProcessHandleCount(GetCurrentProcess(),&cold);const auto firstHash=sha256File(file.wstring());GetProcessHandleCount(GetCurrentProcess(),&warm);const auto secondHash=sha256File(file.wstring());GetProcessHandleCount(GetCurrentProcess(),&beforeHandles);if(firstHash.size()!=64||firstHash!=secondHash||warm!=beforeHandles)throw std::runtime_error("owned hash warmup unstable");
 const auto line=json{{"timestampUtc","2026-11-17T00:00:00Z"},{"message",std::string(2048,'x')},{"nested",json{{"time","ignored"},{"a",json::array({1,2,3})},{"b",{{"enabled",true},{"number",12.5}}}}}}.dump();std::string data;for(unsigned i=0;i<128;++i)data+=line+'\n';saxPut(file,data);
 AllocationTrace oldTrace,newTrace;const auto old=saxMeasured(true,file,oldTrace),now=saxMeasured(false,file,newTrace);
 check("rich128-entire-inspection-metadata-and-input-bytes-exact",old==now&&now["validJsonRows"]==128&&now["firstTime"]=="2026-11-17T00:00:00Z"&&saxBytes(file)==data);
 check("full-validation-without-DOM-removes-at-least-one-allocation-per-rich-row",oldTrace.calls>=newTrace.calls+128&&oldTrace.bytes>newTrace.bytes);
#ifdef YMCC_JSONL_SAX_OLD_ONLY
 GetProcessHandleCount(GetCurrentProcess(),&afterHandles);unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"oldAllocation",saxTrace(oldTrace)},{"newAllocation",saxTrace(newTrace)},{"handlesBefore",beforeHandles},{"handlesAfter",afterHandles}}.dump(2)<<std::endl;return failed?3:0;
#else
 const auto richAllocation=json{{"old",saxTrace(oldTrace)},{"current",saxTrace(newTrace)},{"rows",128},{"lineBytes",line.size()}};
 std::vector<std::pair<std::string,std::string>> shapes={
  {"empty",""},{"blank-lines","\n\r\n \n"},{"nonobjects","[]\nnull\n17\ntrue\n\"time\""},{"no-newline","{\"time\":\"last\"}"},
  {"bad-tail","{}\n{broken"},{"bad-middle","{broken\n{}\n"},{"bad-utf8",std::string("{\"x\":\"\xff\"}")},{"nul",std::string("{}\0{}",5)},
  {"bom",std::string("\xef\xbb\xbf{}\n\xef\xbb\xbf[]")},{"unicode","{\"timestampUtc\":\"中文🎮\\u2028\"}\n"},{"numeric-overflow","{\"n\":1e999}"},{"trailing-token","{}junk\n{}"},
  {"priority","{\"timestampUtc\":\"zulu\",\"timeWithOffset\":\"offset\",\"time\":\"legacy\"}\n{\"timestampLocal\":\"local\",\"atUtc\":\"utc\"}"},
  {"empty-priority","{\"timestampUtc\":\"\",\"time\":\"must-not-fallback\"}"},{"nested-only","{\"x\":{\"timestampUtc\":\"nested\"},\"a\":[{\"time\":\"nested\"}]}"},
  {"root-array","[{\"timestampUtc\":\"nested\"}],0"},{"escaped-key","{\"timestamp\\u0055tc\":\"escaped\",\"time\":\"lower\"}"},
  {"nul-key","{\"timestampUtc\\u0000\":\"ignored\",\"time\":\"t\\u0000v\"}"},{"late-error","{\"time\":\"seen\",\"z\":[1,]}"},
  {"line65536","{\"s\":\""+std::string(65528,'x')+"\"}"},{"line65537","{\"s\":\""+std::string(65529,'x')+"\"}"},
  {"tail-window",std::string(8*1024*1024,'X')+"\n{\"time\":\"retained\"}\n{"}};
 const std::vector<std::string> vals={"\"first\"","\"last\"","\"\"","null","false","7","{}","[]","{\"time\":\"nested\"}"};
 for(const char* key:{"timestampUtc","timeWithOffset","timestampLocal","atUtc","time"})for(const auto& a:vals)for(const auto& b:vals){const std::string quoted=json(key).dump();shapes.push_back({"duplicate-"+std::string(key),"{"+quoted+":"+a+","+quoted+":"+b+",\"other\":{\"time\":\"nested\"}}"});}
 bool exact=true,bytesKept=true;json evidence=json::array();unsigned number=0;
 for(const auto& shape:shapes){const auto path=root/L"cases"/(std::to_wstring(number++)+L".jsonl");saxPut(path,shape.second);const auto expected=beforeStagedFileEvidence(path,"native-lifecycle.log"),actual=currentStagedFileEvidence(path,"native-lifecycle.log");exact=exact&&expected==actual;bytesKept=bytesKept&&saxBytes(path)==shape.second;evidence.push_back({{"case",shape.first},{"bytes",shape.second.size()},{"exact",expected==actual},{"result",actual}});}
 check("427-complete-metadata-differentials-cover-duplicates-types-syntax-and-boundaries",exact&&shapes.size()==427);
 check("all427-input-files-are-byte-exact-after-both-independent-inspections",bytesKept);
 check("empty-high-priority-string-suppresses-lower-fallback",evidence[13]["result"]["firstTime"]=="not-observed");
 check("nested-time-is-ignored-and-late-syntax-error-is-validated",evidence[14]["result"]["firstTime"]=="not-observed"&&evidence[18]["result"]["invalidJsonRows"]==1&&evidence[18]["result"]["validJsonRows"]==0);
 check("escaped-keys-and-embedded-NUL-values-keep-original-meaning",evidence[16]["result"]["firstTime"]=="escaped"&&evidence[17]["result"]["firstTime"]==std::string("t\0v",3));
 check("row-limit-and-leading-tail-boundary-are-preserved",evidence[19]["result"]["validJsonRows"]==1&&evidence[20]["result"]["oversizeRowsNotParsed"]==1&&evidence[21]["result"]["inspectionScope"]=="retained-tail-only"&&evidence[21]["result"]["lastTime"]=="retained");
 saxPut(file,"{}\n[]\nnull\n");const auto smallOld=saxMeasured(true,file,oldTrace),smallNow=saxMeasured(false,file,newTrace);check("short-primitive-rows-add-no-allocation-or-metadata-change",smallOld==smallNow&&newTrace.calls<=oldTrace.calls&&newTrace.bytes<=oldTrace.bytes);
 const auto missing=root/L"missing.jsonl";check("missing-file-keeps-original-typed-metadata",beforeStagedFileEvidence(missing,"native-lifecycle.log")==currentStagedFileEvidence(missing,"native-lifecycle.log"));
 const auto opaque=root/L"opaque.bin";saxPut(opaque,"opaque\n");check("opaque-final-byte-path-is-exact",beforeStagedFileEvidence(opaque,"snapshot.json")==currentStagedFileEvidence(opaque,"snapshot.json"));
 saxPut(file,"{\"time\":\"first\",\"x\":1}\n{\"time\":\"last\",\"x\":2}\n");const auto beforeHash=sha256File(file.wstring());std::vector<std::pair<std::wstring,std::wstring>> entries={{root.wstring(),L"native-lifecycle.log"}};bool indexed=writeAiLogIndex(root/L"ai",entries,sha256File);
 std::ifstream index(root/L"ai"/L"ai-log-index.jsonl");std::vector<json> frames;for(std::string value;std::getline(index,value);)frames.push_back(json::parse(value));index.close();
 check("actual-index-retains-full-hash-size-and-independent-inspection",indexed&&frames.size()==4&&frames[1]["sha256"]==beforeHash&&frames[2]["validJsonRows"]==2&&frames[2]["firstTime"]=="first"&&frames[2]["lastTime"]=="last"&&sha256File(file.wstring())==beforeHash);
 saxPut(file,"{\"time\":\"changed\"}\n");const auto changed=currentStagedFileEvidence(file,"native-lifecycle.log");check("second-inspection-observes-mutation-instead-of-reusing-metadata",changed["firstTime"]=="changed"&&changed["validJsonRows"]==1);saxPut(file,"{\"time\":\"first\",\"x\":1}\n{\"time\":\"last\",\"x\":2}\n");
 GetProcessHandleCount(GetCurrentProcess(),&afterHandles);check("owned-inspector-and-hash-handles-return-to-warm-count",beforeHandles==afterHandles);
 unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"richAllocation",richAllocation},{"formatEvidence",evidence},{"coldHandles",cold},{"firstHashHandles",warm},{"handlesBefore",beforeHandles},{"handlesAfter",afterHandles},{"actualBeforeAndCurrentBodies",true},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<std::endl;return failed?3:0;
#endif
}catch(const std::exception& error){allocationMeasure=false;std::cerr<<error.what()<<std::endl;return 4;}}
