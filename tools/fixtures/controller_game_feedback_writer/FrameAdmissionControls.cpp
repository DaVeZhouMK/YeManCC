// Additional controlled-environment admission checks. Original13/354 remains separate.
static unsigned fixtureAdmissionChecks=0;
static void fixtureAdmissionNeed(bool ok,const char* label){++fixtureAdmissionChecks;if(!ok)throw std::runtime_error(label);}
static InputFrameSubmission fixtureAdmissionFrame(bool neutral){
 const auto now=GetTickCount64(),power=currentPowerGeneration();
 InputFrameSubmission frame;
 frame.ticket={g_inputHostEpoch,power,g_inputHostVirtualIntentRevision.load(),neutral};
 frame.sample={{1,now,now,power,g_xinputTopologyEpoch.load(),true},g_xinputSlotIdentityEpoch.load(),0,true,true,false,0};
 frame.physicalSourceAdmitted=true;return frame;
}
int wmain(int argc,wchar_t**argv){
 try{
  fixtureAdmissionNeed(argc==2,"ADMISSION_CONTROL_EVIDENCE_ROOT_REQUIRED");const std::filesystem::path root(argv[1]);
  // This is a non-owning truthy environmental token, never a started process or API handle.
  g_inputHostProcess=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1));
  g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;
  g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_inputHostVirtualIntentWriters=0;
  const auto beforeSequence=g_inputHostSequence;json records=json::array();
  struct Setting{const char* id;bool enabled,target,neutral,allowed;};
  for(const auto setting:std::array<Setting,6>{{
   {"baseline-on",true,true,false,true},{"off-current",false,true,false,false},
   {"target-disabled-current",true,false,false,false},{"off-neutral",false,true,true,false},
   {"target-disabled-neutral",true,false,true,false},{"restored-on-neutral",true,true,true,true}
  }}){
   const auto before=fixtureAdmissionChecks;
   g_realStickTest.enabled=setting.enabled;g_realStickTest.virtualTargetEnabled=setting.target;
   const auto current=inputHostFrameSubmissionCurrent(true,false);
   fixtureAdmissionNeed(current.ready==setting.allowed,"ACTUAL_CURRENT_RESPECTS_ENABLED_AND_TARGET");
   auto frame=fixtureAdmissionFrame(setting.neutral);const bool mayIssue=inputHostFrameSubmissionMayIssue(&frame);
   fixtureAdmissionNeed(mayIssue==setting.allowed,"ACTUAL_MAY_ISSUE_RESPECTS_ENABLED_AND_TARGET");
   fixtureAdmissionNeed(frame.rejected==!setting.allowed,"ACTUAL_MAY_ISSUE_REJECTION_RECORDED");
   if(!setting.allowed)fixtureAdmissionNeed(frame.reason==ymcc::input::frameSubmission::Verdict::PowerUnavailable,"ACTUAL_MAY_ISSUE_POWER_UNAVAILABLE_REASON");
   auto requestFrame=fixtureAdmissionFrame(setting.neutral);auto* previous=t_inputFrameSubmission;t_inputFrameSubmission=&requestFrame;
   json response;const bool requestCurrent=inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"fixture-admission-control");t_inputFrameSubmission=previous;
   fixtureAdmissionNeed(requestCurrent==setting.allowed,"ACTUAL_REQUEST_CURRENT_RESPECTS_ENABLED_AND_TARGET");
   fixtureAdmissionNeed(requestFrame.rejected==!setting.allowed,"ACTUAL_REQUEST_CURRENT_REJECTION_RECORDED");
   if(!setting.allowed){fixtureAdmissionNeed(response.value("status",std::string{})=="frame-context-retired","ACTUAL_REQUEST_CURRENT_REFUSAL_STATUS");fixtureAdmissionNeed(response["deviceWriteAttempted"]==false,"ACTUAL_REQUEST_CURRENT_NO_DEVICE_WRITE");}
   records.push_back({{"id",setting.id},{"passed",true},{"checks",fixtureAdmissionChecks-before},{"enabled",setting.enabled},{"virtualTargetEnabled",setting.target},{"neutral",setting.neutral},{"currentReady",current.ready},{"mayIssue",mayIssue},{"requestCurrent",requestCurrent},{"reason",ymcc::input::frameSubmission::name(frame.reason)}});
  }
  g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=true;g_inputHostProcess=nullptr;
  fixtureAdmissionNeed(g_inputHostSequence==beforeSequence,"CONTROL_DOES_NOT_ISSUE_A_PIPE_REQUEST");
  fixtureAdmissionNeed(!PhysicalMemory::g_gamepadSerialThread.joinable(),"CONTROL_DOES_NOT_START_SERIAL_WORKER");
  fixtureAdmissionNeed(PhysicalMemory::outputCount()==0,"CONTROL_DOES_NOT_WRITE_MEMORY_HARDWARE");
  const json result={{"status","PASS"},{"passed",true},{"cases",records},{"checks",fixtureAdmissionChecks},{"additionalControlsOnly",true},{"original13Case354Executed",false},{"actualCurrentMayIssueRequestCurrent",true},{"managedHostStarts",0},{"realSdkLoaded",false},{"realHardwareIo",false},{"physicalAcceptance",false}};
  std::ofstream(root/"result.json")<<result.dump(2);std::cout<<json{{"status","PASS"},{"cases",records.size()},{"checks",fixtureAdmissionChecks}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cout<<json{{"status","FAIL"},{"error",e.what()},{"checks",fixtureAdmissionChecks}}.dump()<<'\n';return 1;}
}
