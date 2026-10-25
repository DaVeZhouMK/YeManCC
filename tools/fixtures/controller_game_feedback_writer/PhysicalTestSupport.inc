static bool waitUntil(const std::function<bool()>& predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);return predicate();}
static ymcc::game_feedback::Dispatch::Stats dispatchStats(){std::lock_guard<std::mutex> lock(g_gamepadSerialMx);return g_gamepadSerialGameFeedback.snapshot();}
static size_t outputCount(){std::lock_guard<std::mutex> lock(g_feedbackPulseMx);return writes.size();}
static unsigned lastOutputLeft(){std::lock_guard<std::mutex> lock(g_feedbackPulseMx);return writes.empty()?999999u:writes.back()["left"].get<unsigned>();}
static bool pulseActive(){std::lock_guard<std::mutex> lock(g_feedbackPulseMx);return g_feedbackPulseActive;}
static void wakeSerial(){std::lock_guard<std::mutex> lock(g_gamepadSerialMx);g_gamepadSerialCv.notify_one();}
static void stopSerial(){if(!g_gamepadSerialThread.joinable())return;{std::lock_guard<std::mutex> lock(g_gamepadSerialMx);g_gamepadSerialStopping=true;g_gamepadSerialCv.notify_one();}g_gamepadSerialThread.join();}
static void serialReset(bool msi=false){
 stopSerial();reset();fixtureThrowMsiFeedbackLog=false;g_gamepadSerialStarted=false;g_gamepadSerialStopping=false;g_gamepadSerialQ.clear();g_gamepadSerialGameFeedback={};g_gamepadSerialGameInterested=false;g_inputHostEpoch=17;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_inputHostVirtualIntentRevision=1;g_inputHostVirtualIntentWriters=0;g_msiMainSourceRevision=11;g_inputHostGameMotorBindingGeneration=1;g_inputHostGameMotorInbox.bind(17,1,1,1);g_inputCaptureGamepadSnapshot={};sample();
 if(msi){msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;}
}
static InputFrameSubmission prepareFrame(bool update=true,BYTE left=0x12,BYTE right=0xA5,unsigned long long revision=1,bool msi=false){
 const auto source=fixturePublishSource(msi?"msi-dinput":"xinput",true);InputFrameSubmission frameSubmission;frameSubmission.ticket={g_inputHostEpoch,currentPowerGeneration(),g_inputHostVirtualIntentRevision.load(),false};fixtureCopyFrameSource(frameSubmission,source);frameSubmission.physicalSourceAdmitted=true;
 if(update){auto* previous=t_inputFrameSubmission;t_inputFrameSubmission=&frameSubmission;inputHostConsumeGameMotorReplyLocked(ymcc::game_feedback::fromWords({1,revision,left,right,1,revision},0x3f),revision+3);t_inputFrameSubmission=previous;}
 return frameSubmission;
}
static void publishFrame(const InputFrameSubmission& frame,bool accepted=true){GameFeedbackFrameDispatchScope scope(frame);scope.accepted=accepted;}
static void submitFrame(BYTE left=0x12,BYTE right=0xA5,unsigned long long revision=1,bool msi=false){publishFrame(prepareFrame(true,left,right,revision,msi));}
static void waitApplied(unsigned long long count){expect("worker reaches actual physical writer",waitUntil([&]{return dispatchStats().applied>=count;}),true);}
static void finishSerialCase(const std::string& name){stopSerial();finish(name);}
