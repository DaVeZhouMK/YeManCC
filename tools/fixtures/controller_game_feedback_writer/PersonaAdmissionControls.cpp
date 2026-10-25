// Independent persona-mismatch controls; old13/354 and old6/46 stay separate.
static unsigned fixturePersonaChecks=0;
static void fixturePersonaNeed(bool ok,const char* label){++fixturePersonaChecks;if(!ok)throw std::runtime_error(label);}
int wmain(int argc,wchar_t**argv){
 try{
  fixturePersonaNeed(argc==2,"PERSONA_CONTROL_EVIDENCE_ROOT_REQUIRED");const std::filesystem::path root(argv[1]);
  g_inputHostProcess=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1));
  g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=true;
  g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;
  g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_inputHostVirtualIntentWriters=0;
  const auto beforeSequence=g_inputHostSequence;json records=json::array();
  const auto check=[&](const char* id,bool allowed,bool neutral){
   const auto start=fixturePersonaChecks;
   fixturePersonaNeed(g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire)==allowed,"ACTUAL_REFRESH_DERIVES_PERSONA_MATCH");
   fixturePersonaNeed(inputHostFrameSubmissionCurrent(true,false).ready==allowed,"ACTUAL_CURRENT_REJECTS_PERSONA_MISMATCH");
   auto frame=fixtureAdmissionFrame(neutral);const auto mayIssue=inputHostFrameSubmissionMayIssue(&frame);
   fixturePersonaNeed(mayIssue==allowed,"ACTUAL_MAY_ISSUE_REJECTS_PERSONA_MISMATCH");
   fixturePersonaNeed(frame.rejected==!allowed,"ACTUAL_PERSONA_MAY_ISSUE_REJECTION_RECORDED");
   auto requestFrame=fixtureAdmissionFrame(neutral);auto* previous=t_inputFrameSubmission;t_inputFrameSubmission=&requestFrame;
   json response;const auto requestCurrent=inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"persona-admission-control");t_inputFrameSubmission=previous;
   fixturePersonaNeed(requestCurrent==allowed,"ACTUAL_REQUEST_CURRENT_REJECTS_PERSONA_MISMATCH");
   fixturePersonaNeed(requestFrame.rejected==!allowed,"ACTUAL_PERSONA_REQUEST_REJECTION_RECORDED");
   if(!allowed){fixturePersonaNeed(frame.reason==ymcc::input::frameSubmission::Verdict::PowerUnavailable,"ACTUAL_PERSONA_UNAVAILABLE_REASON");fixturePersonaNeed(response.value("status",std::string{})=="frame-context-retired"&&response["deviceWriteAttempted"]==false,"ACTUAL_PERSONA_REFUSAL_NO_DEVICE_WRITE");}
   records.push_back({{"id",id},{"passed",true},{"checks",fixturePersonaChecks-start},{"requestedPersona",g_realStickTest.virtualTargetPersona},{"boundPersona",g_inputHostBoundPersona},{"neutral",neutral},{"atomicMatch",g_inputHostRequestedPersonaMatchesBound.load()},{"currentReady",inputHostFrameSubmissionCurrent(true,false).ready},{"mayIssue",mayIssue},{"requestCurrent",requestCurrent}});
  };
  fixtureSetMatchingPersona("dualsense-edge");check("actual-set-matching-baseline",true,false);
  fixtureSetRequestedPersona("xbox-elite-v2");check("requested-not-bound-refused",false,false);
  check("requested-not-bound-neutral-refused",false,true);
  inputHostSetBoundPersona("xbox-elite-v2");check("actual-set-restores-matching",true,false);
  inputHostClearBoundPersona();check("actual-clear-revokes-match",false,false);
  inputHostSetBoundPersona("dualsense-edge");check("bound-still-differs-from-request",false,false);
  fixtureSetRequestedPersona("dualsense-edge");check("actual-refresh-restores-match",true,true);
  g_inputHostProcess=nullptr;
  fixturePersonaNeed(g_inputHostSequence==beforeSequence,"PERSONA_CONTROL_HAS_NO_PIPE_REQUEST");
  fixturePersonaNeed(!PhysicalMemory::g_gamepadSerialThread.joinable(),"PERSONA_CONTROL_HAS_NO_SERIAL_WORKER");
  fixturePersonaNeed(PhysicalMemory::outputCount()==0,"PERSONA_CONTROL_HAS_NO_MEMORY_HARDWARE_WRITE");
  const json result={{"status","PASS"},{"passed",true},{"cases",records},{"checks",fixturePersonaChecks},{"additionalPersonaControlsOnly",true},{"old6Case46Executed",false},{"original13Case354Executed",false},{"actualRefreshSetClearAndCurrentMayIssueRequestCurrent",true},{"managedHostStarts",0},{"realSdkLoaded",false},{"realHardwareIo",false},{"physicalAcceptance",false}};
  std::ofstream(root/"result.json")<<result.dump(2);std::cout<<json{{"status","PASS"},{"cases",records.size()},{"checks",fixturePersonaChecks}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cout<<json{{"status","FAIL"},{"error",e.what()},{"checks",fixturePersonaChecks}}.dump()<<'\n';return 1;}
}
