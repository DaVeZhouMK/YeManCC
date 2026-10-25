// Direct actual native protocol-binding/compatibility controls, separate from25 pipe cases.
static unsigned steamProtocolChecks=0;
static void steamNeed(bool ok,const char* label){++steamProtocolChecks;if(!ok)throw std::runtime_error(label);}
static void steamProtocolBind(const char* persona,unsigned protocol){
 fixturePersona=persona;fixtureSetMatchingPersona(persona);InputHostGameMotorDomKeys keys;keys.seen=0xc0u;
 inputHostObserveGameMotorBindingLocked(json{{"gameFeedbackBindingGeneration",std::uint64_t{1}},{"gameFeedbackProtocol",std::uint64_t{protocol}}},keys);
}
static InputFrameSubmission steamProtocolFrame(unsigned compatibility){
 auto frame=PhysicalMemory::prepareFrame(false);auto* previous=t_inputFrameSubmission;t_inputFrameSubmission=&frame;
 inputHostConsumeGameMotorReplyLocked(ymcc::game_feedback::fromWords({1,1,0x12,0xA5,compatibility,1},0x3f),4);t_inputFrameSubmission=previous;return frame;
}
int wmain(int argc,wchar_t**argv){
 try{steamNeed(argc==2,"STEAM_PROTOCOL_CONTROL_ROOT_REQUIRED");const std::filesystem::path root(argv[1]);json records=json::array();
 {
  combinedReset(false);steamProtocolBind("steamdeck",2);const auto before=steamProtocolChecks;
  steamNeed(g_inputHostGameMotorInbox.snapshot().bound,"STEAM_PROFILE_TOKEN_BOUND");
  const auto command=PhysicalMemory::nativeBindGameFeedbackFrameCommand(steamProtocolFrame(3));
  const auto receipt=PhysicalMemory::nativeWriteBoundGameFeedbackCommand(command);
  steamNeed(receipt.ok,"STEAM_XINPUT_ACTUAL_WRITER_ACCEPTED");
  steamNeed(PhysicalMemory::lastOutputLeft()==0x1212u&&combinedRight()==0xA5A5u,"STEAM_INDEPENDENT_LEFT_RIGHT_PRESERVED");
  PhysicalMemory::physicalFeedbackFinish(receipt.pulseGeneration);PhysicalMemory::stopSerial();
  records.push_back({{"id","steam-profile2-bound-and-same-frame-writer"},{"passed",true},{"checks",steamProtocolChecks-before}});
 }
 {
  combinedReset(false);steamProtocolBind("dualsense-edge",2);const auto before=steamProtocolChecks;
  steamNeed(!g_inputHostGameMotorInbox.snapshot().bound,"HOST_PROTOCOL_MUST_MATCH_CATALOG_PROFILE");
  steamNeed(g_inputHostGameMotorBindingGeneration.load()==0,"MISMATCH_DOES_NOT_PUBLISH_BINDING");
  records.push_back({{"id","host-protocol2-cannot-bind-sony-profile1"},{"passed",true},{"checks",steamProtocolChecks-before}});
 }
 {
  combinedReset(false);steamProtocolBind("steamdeck",2);const auto before=steamProtocolChecks;
  auto frame=steamProtocolFrame(1);steamNeed(g_inputHostGameMotorInbox.snapshot().received==0,"SONY_COMPAT_CANNOT_ENTER_STEAM_INBOX");
  steamNeed(g_inputHostGameMotorInbox.snapshot().rejected==1&&!frame.gameMotorReply.present,"WRONG_COMPAT_REJECTED_BEFORE_FRAME_ATTACHMENT");
  records.push_back({{"id","sony-compatibility-cannot-enter-steam-inbox"},{"passed",true},{"checks",steamProtocolChecks-before}});
 }
 {
  combinedReset(false);steamProtocolBind("dualsense-edge",1);const auto before=steamProtocolChecks;
  auto frame=steamProtocolFrame(3);steamNeed(g_inputHostGameMotorInbox.snapshot().received==0&&!frame.gameMotorReply.present,"STEAM_COMPAT_CANNOT_ENTER_SONY_INBOX");
  records.push_back({{"id","steam-compatibility-cannot-enter-sony-inbox"},{"passed",true},{"checks",steamProtocolChecks-before}});
 }
 PhysicalMemory::stopSerial();const json result={{"status","PASS"},{"passed",true},{"cases",records},{"checks",steamProtocolChecks},{"additionalSteamProtocolControlsOnly",true},{"actualNativeBindingAndInbox",true},{"actualSameFrameWriterWithMemoryEndpoints",true},{"original25PipeCasesExecuted",false},{"managedHostStarts",0},{"realSdkLoaded",false},{"realHardwareIo",false},{"physicalAcceptance",false}};
 std::ofstream(root/"result.json")<<result.dump(2);std::cout<<json{{"status","PASS"},{"cases",records.size()},{"checks",steamProtocolChecks}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){PhysicalMemory::stopSerial();std::cout<<json{{"status","FAIL"},{"error",e.what()},{"checks",steamProtocolChecks}}.dump()<<'\n';return 1;}
}
