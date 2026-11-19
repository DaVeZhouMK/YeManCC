@@ACTUAL_COMMAND_WRAPPER@@
static json commandDetailRows(const fspath::path& root){std::ifstream in(root/L"native-detail.log");json rows=json::array();std::string line;while(std::getline(in,line))if(!line.empty())rows.push_back(json::parse(line));return rows;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2&&argc!=3)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();
 const char* event="input-host-command-result";const json healthy={{"command","SUBMIT_FRAME"},{"ok",true}},failed={{"command","SUBMIT_FRAME"},{"ok",false}},other={{"command","HELLO"},{"ok",true}};
 inputHostCommandResultLog(failed);closeStreams();DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;unsigned replayRows=0,replayFailures=0;
 const auto finish=[&]{closeStreams();if(!GetProcessHandleCount(GetCurrentProcess(),&after))return 3;check(before==after,"all owned command logging stream handles returned");unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"phase","command-result-boolean"},{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"resident",readRecoveryRows(root)},{"detail",commandDetailRows(root)},{"fieldReplayRows",replayRows},{"fieldReplaySubmitFrameFailures",replayFailures},{"hostCommandsDeviceOrSleepExecuted",false}}.dump(2)<<'\n';return failures?2:0;};
 check(nativeLifecycleRecordIsStreaming(event,healthy)&&!nativeLifecycleRecordIsStreaming(event,failed),"typed successful frame stays detailed and typed failed frame stays resident");
 check(!nativeLifecycleRecordIsStreaming(event,other)&&!nativeLifecycleRecordIsStreaming(event,json{{"command","SUBMIT_FRAME"}}),"other command receipts and missing ok retain their original resident classification");
 const json malformed={{"command","SUBMIT_FRAME"},{"ok",1},{"marker","numeric-ok"}};inputHostCommandResultLog(malformed);closeStreams();
 check(readRecoveryRows(root).size()==2&&readRecoveryRows(root).back().at("ok")==1,"actual command logger retains nonboolean ok instead of losing row in classifier catch");
#ifdef YMCC_COMMAND_BOOLEAN_OLD_ONLY
 return finish();
#else
 const std::array<json,5> values={nullptr,1,"false",json::array({1}),json{{"value",false}}};bool classified=true;
 for(const auto& value:values)classified=classified&&!nativeLifecycleRecordIsStreaming(event,json{{"command","SUBMIT_FRAME"},{"ok",value}});
 check(classified,"all five nonboolean ok kinds are classified as resident unknown without throw");
 const auto start=readRecoveryRows(root).size();std::array<json,5> payloads;for(size_t i=0;i<values.size();++i){payloads[i]={{"command","SUBMIT_FRAME"},{"ok",values[i]}};inputHostCommandResultLog(payloads[i]);}closeStreams();auto rows=readRecoveryRows(root);
 bool exact=rows.size()==start+values.size();for(size_t i=0;i<values.size();++i)exact=exact&&rows.at(start+i).at("ok")==values[i];
 check(exact,"original command wrapper and file writer preserve all five bad values unchanged");
 check(count(Failure::preparationException)==0,"bad ok values no longer masquerade as Native preparation exceptions");
 inputHostCommandResultLog(json{{"command","SUBMIT_FRAME"}});inputHostCommandResultLog(json{{"command","HELLO"},{"ok",json::array()}});closeStreams();
 check(readRecoveryRows(root).size()==rows.size()+2,"missing ok and other-command bad ok keep original short circuit behavior");
 const auto normalRows=readRecoveryRows(root).size();ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<100000;++i)inputHostCommandResultLog(healthy);const auto normalArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(normalArm==0&&readRecoveryRows(root).size()==normalRows&&!fspath::exists(root/L"native-detail.log"),"one hundred thousand successful frame logger calls stay silent and allocate no C++ heap with detail OFF");
 ymcc::testing::allocationFault::failAfter(0);bool direct=true;for(unsigned i=0;i<20000;++i)for(const auto& row:payloads)direct=direct&&!nativeLifecycleRecordIsStreaming(event,row);const auto classifierArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(direct&&classifierArm==0,"one hundred thousand bad boolean classifier decisions allocate no C++ heap");
 g_domainLogEnabled[1]=true;inputHostCommandResultLog(healthy);closeStreams();auto details=commandDetailRows(root);
 check(details.size()==1&&details.at(0).at("command")=="SUBMIT_FRAME"&&details.at(0).at("ok")==true,"detail ON keeps successful SUBMIT_FRAME in the original detailed file");
 const auto onRows=readRecoveryRows(root).size();inputHostCommandResultLog(malformed);inputHostCommandResultLog(failed);closeStreams();
 check(readRecoveryRows(root).size()==onRows+2&&commandDetailRows(root).size()==1,"detail ON does not move unknown or failed frame results away from resident lane");
 const auto beforeText=count(Failure::preparationException),beforeTextRows=readRecoveryRows(root).size();inputHostCommandResultLog(json{{"command",false},{"ok",true}});closeStreams();
 check(count(Failure::preparationException)==beforeText+1&&readRecoveryRows(root).size()==beforeTextRows,"original malformed command text contract remains its separate preparation exception");
 const auto normalized=readRecoveryRows(root).size();inputHostCommandResultLog(json::array());closeStreams();
 check(readRecoveryRows(root).size()==normalized+1&&!readRecoveryRows(root).back().contains("command"),"original command wrapper nonobject normalization stays unchanged");
 if(argc==3){const fspath::path replay(argv[2]);if(fspath::file_size(replay)>512*1024)throw std::runtime_error("owned replay too large");std::ifstream in(replay);json data;in>>data;if(!data.is_array()||data.empty()||data.size()>512)throw std::runtime_error("invalid owned replay");bool exactReplay=true;
  for(const auto& row:data){const auto command=row.find("command"),ok=row.find("ok");const bool known=row.is_object()&&command!=row.end()&&command->is_string()&&ok!=row.end()&&ok->is_boolean();exactReplay=exactReplay&&known;if(!known)continue;const bool frame=command->get_ref<const std::string&>()=="SUBMIT_FRAME",success=ok->get<bool>();exactReplay=exactReplay&&(nativeLifecycleRecordIsStreaming(event,row)==(frame&&success));++replayRows;if(frame&&!success)++replayFailures;}
  check(exactReplay&&replayRows==data.size(),"all supplied original exported command records preserve typed lane decisions");}
 ++g_powerGeneration;check(nativeLifecycleRecordIsStreaming(event,healthy)&&!nativeLifecycleRecordIsStreaming(event,malformed),"power change grants no new normal or unknown classification state");
 return finish();
#endif
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
