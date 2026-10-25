// Actual binding/Inbox/frame/writer controls; independent of the41 pipe cases.
static unsigned xboxProtocolChecks=0;
static void xboxNeed(bool ok,const char* label){++xboxProtocolChecks;if(!ok)throw std::runtime_error(label);}
int wmain(int argc,wchar_t**argv){
 try{xboxNeed(argc==2,"XBOX_PROTOCOL_CONTROL_ROOT_REQUIRED");const std::filesystem::path root(argv[1]);json records=json::array();
 {
  combinedReset(false);steamProtocolBind("elite",3);const auto before=xboxProtocolChecks;
  xboxNeed(g_inputHostGameMotorInbox.snapshot().bound,"XBOX_PROFILE3_BOUND");
  const auto command=PhysicalMemory::nativeBindGameFeedbackFrameCommand(steamProtocolFrame(4));
  const auto receipt=PhysicalMemory::nativeWriteBoundGameFeedbackCommand(command);
  xboxNeed(receipt.ok,"XBOX_ACTUAL_XINPUT_WRITER_ACCEPTED");
  xboxNeed(PhysicalMemory::lastOutputLeft()==0x1212u&&combinedRight()==0xA5A5u,"XBOX_INDEPENDENT_MOTORS_PRESERVED");
  PhysicalMemory::physicalFeedbackFinish(receipt.pulseGeneration);PhysicalMemory::stopSerial();
  records.push_back({{"id","xbox-profile3-same-frame-original-writer"},{"passed",true},{"checks",xboxProtocolChecks-before}});
 }
 {
  combinedReset(false);steamProtocolBind("dualsense-edge",3);const auto before=xboxProtocolChecks;
  xboxNeed(!g_inputHostGameMotorInbox.snapshot().bound,"XBOX_HOST_PROTOCOL_MUST_MATCH_EXACT_TARGET");
  xboxNeed(g_inputHostGameMotorBindingGeneration.load()==0,"XBOX_MISMATCH_CANNOT_PUBLISH_BINDING");
  records.push_back({{"id","host-protocol3-cannot-bind-sony-profile1"},{"passed",true},{"checks",xboxProtocolChecks-before}});
 }
 {
  combinedReset(false);steamProtocolBind("elite",3);const auto before=xboxProtocolChecks;auto frame=steamProtocolFrame(3);
  xboxNeed(g_inputHostGameMotorInbox.snapshot().received==0,"STEAM_COMPAT_CANNOT_ENTER_XBOX_INBOX");
  xboxNeed(g_inputHostGameMotorInbox.snapshot().rejected==1&&!frame.gameMotorReply.present,"STEAM_COMPAT_REJECTED_BEFORE_XBOX_FRAME");
  records.push_back({{"id","steam-compatibility-cannot-enter-xbox-inbox"},{"passed",true},{"checks",xboxProtocolChecks-before}});
 }
 {
  const auto before=xboxProtocolChecks;
  for(const auto* persona:{"dualsense-edge","steamdeck"}){combinedReset(false);steamProtocolBind(persona,std::string(persona)=="steamdeck"?2:1);auto frame=steamProtocolFrame(4);xboxNeed(g_inputHostGameMotorInbox.snapshot().received==0&&!frame.gameMotorReply.present,"XBOX_COMPAT_CANNOT_ENTER_OTHER_PROTOCOL");}
  records.push_back({{"id","xbox-compatibility-cannot-enter-sony-or-steam-inbox"},{"passed",true},{"checks",xboxProtocolChecks-before}});
 }
 PhysicalMemory::stopSerial();const json result={{"status","PASS"},{"passed",true},{"cases",records},{"checks",xboxProtocolChecks},{"additionalXboxProtocolControlsOnly",true},{"actualNativeBindingAndInbox",true},{"actualSameFrameWriterWithMemoryEndpoints",true},{"original41PipeCasesExecuted",false},{"managedHostStarts",0},{"realSdkLoaded",false},{"realHardwareIo",false},{"physicalAcceptance",false}};
 std::ofstream(root/"result.json")<<result.dump(2);std::cout<<json{{"status","PASS"},{"cases",records.size()},{"checks",xboxProtocolChecks}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){PhysicalMemory::stopSerial();std::cout<<json{{"status","FAIL"},{"error",e.what()},{"checks",xboxProtocolChecks}}.dump()<<'\n';return 1;}
}
