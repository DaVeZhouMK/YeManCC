static json classifierDetailRows(const fspath::path& root){std::ifstream in(root/L"native-detail.log");json rows=json::array();std::string line;while(std::getline(in,line))if(!line.empty())rows.push_back(json::parse(line));return rows;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();
 const char* event="msi-dinput-feedback";const json failed={{"managed",true},{"ok",false},{"writeError",5}};
 const json healthy={{"managed",true},{"ok",true}},unmanaged={{"managed",false},{"ok",false},{"attempted",false},{"policy","no-write-when-msi-dinput-unmanaged"}};
 appendNativeLifecycleLog(event,failed);closeStreams();DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
 const auto finish=[&]{closeStreams();if(!GetProcessHandleCount(GetCurrentProcess(),&after))return 3;check(before==after,"all owned logging stream handles returned");unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"phase","msi-feedback-classification"},{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"resident",readRecoveryRows(root)},{"detail",classifierDetailRows(root)},{"msiDeviceOrFeedbackLeaseExecuted",false},{"productOrSleepActions",0}}.dump(2)<<'\n';return failures?2:0;};
 check(!nativeLifecycleRecordIsStreaming(event,failed)&&readRecoveryRows(root).size()==1,"typed managed failure retains its original resident receipt");
 check(nativeLifecycleRecordIsStreaming(event,healthy)&&nativeLifecycleRecordIsStreaming(event,unmanaged),"typed healthy and unmanaged samples keep original streaming classification");
 const json missing={{"ok",false},{"marker","missing-managed"}};appendNativeLifecycleLog(event,missing);closeStreams();
 check(readRecoveryRows(root).size()==2&&readRecoveryRows(root).back().at("marker")=="missing-managed","missing managed flag remains resident with detail OFF");
#ifdef YMCC_MSI_CLASSIFIER_OLD_ONLY
 return finish();
#else
 const std::array<json,10> malformed={json::object(),json{{"managed",nullptr},{"ok",false}},json{{"managed",1},{"ok",false}},json{{"managed","false"},{"ok",false}},json{{"managed",true}},json{{"managed",true},{"ok",nullptr}},json{{"managed",true},{"ok",1}},json{{"managed",false}},json{{"managed",false},{"ok","false"}},json{{"managed",json::array()},{"ok",true}}};
 bool unknownResident=true;for(const auto& row:malformed)unknownResident=unknownResident&&!nativeLifecycleRecordIsStreaming(event,row);
 check(unknownResident,"all ten missing or nonboolean flag shapes are unknown resident evidence");
 check(!nativeLifecycleRecordIsStreaming(event,json())&&!nativeLifecycleRecordIsStreaming(event,json::array()),"nonobject classifier inputs do not claim healthy streaming state");
 const auto prior=readRecoveryRows(root).size();for(const auto& row:malformed)appendNativeLifecycleLog(event,row);closeStreams();auto resident=readRecoveryRows(root);
 check(resident.size()==prior+malformed.size(),"actual original wrapper writes every malformed object with detail OFF");
 check(resident.at(prior+2).at("managed")==1&&resident.at(prior+6).at("ok")==1&&resident.at(prior+8).at("ok")=="false","unknown recorded flag values are not rewritten into booleans or success");
 check(!fspath::exists(root/L"native-detail.log"),"detail OFF creates no detailed file for malformed resident receipts");
 const auto hotBefore=resident.size();ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i){appendNativeLifecycleLog(event,healthy);appendNativeLifecycleLog(event,unmanaged);}const auto normalArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(normalArm==0&&readRecoveryRows(root).size()==hotBefore,"two hundred thousand typed normal OFF offers allocate no C++ heap and add no resident rows");
 ymcc::testing::allocationFault::failAfter(0);bool direct=true;for(unsigned i=0;i<10000;++i)for(const auto& row:malformed)direct=direct&&!nativeLifecycleRecordIsStreaming(event,row);const auto unknownArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(direct&&unknownArm==0,"one hundred thousand malformed classifier decisions allocate no C++ heap");
 g_domainLogEnabled[1]=true;appendNativeLifecycleLog(event,healthy);appendNativeLifecycleLog(event,unmanaged);closeStreams();auto detailed=classifierDetailRows(root);
 check(detailed.size()==2&&detailed.at(0).at("managed")==true&&detailed.at(0).at("ok")==true&&detailed.at(1).at("managed")==false&&detailed.at(1).at("ok")==false,"detail ON retains the original two typed normal samples in detail only");
 const auto beforeUnknownOn=readRecoveryRows(root).size();appendNativeLifecycleLog(event,missing);appendNativeLifecycleLog(event,failed);closeStreams();
 check(readRecoveryRows(root).size()==beforeUnknownOn+2&&classifierDetailRows(root).size()==2,"detail ON does not move unknown or managed failure receipts away from resident lane");
 const auto beforeNonobject=readRecoveryRows(root).size();appendNativeLifecycleLog(event,json::array());closeStreams();
 check(readRecoveryRows(root).size()==beforeNonobject+1&&readRecoveryRows(root).back().at("event")==event&&!readRecoveryRows(root).back().contains("managed"),"original wrapper normalization of nonobjects stays unchanged while unknown event remains resident");
 const auto errors=ymcc::logging::nativeLogFailureEvidence().snapshot();bool noErrors=true;for(const auto& bucket:errors["buckets"])noErrors=noErrors&&bucket["observedFailures"]==0;
 check(noErrors,"unknown flag types are classified without inventing Native preparation or file failures");
 ++g_powerGeneration;check(!nativeLifecycleRecordIsStreaming(event,missing)&&nativeLifecycleRecordIsStreaming(event,healthy),"power change preserves typed normal and unknown lane decisions without a new memo");
 return finish();
#endif
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
