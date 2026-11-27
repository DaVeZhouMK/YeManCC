// Added parameters of the ORIGINAL input_frame_pipe_issue fixture, no new ID.
// Wire tuple/parser acceptance is outside this extracted transport harness.
// fixtureValidatedReceiptEntry consumes only the actual r6 post-validation block.
static void runRequestSharedPowerCases(Checks& c){
 using namespace ymcc::inputHost::pipeTransport;
 using Scope=ymcc::inputHost::sharedPower::Scope;
 using Use=ymcc::inputHost::sharedPower::Use;
 const auto scope=[](){return Scope::create(&readSharedSystemPower,g_sharedPowerRevocations,true,Use::Lifecycle);};
 const auto revoke=[](){++fixtureNotification;g_sharedPowerRevocations.revoke();};
 const auto run=[&](const std::string& name,const std::function<void()>& body){c.run(("request-shared-power/"+name).c_str(),[&]{
  fixtureBusinessConsumers=0;body();
  c.check(!fake::waitMismatch&&fake::waitSteps.empty(),"original wait interleaves consumed exactly");
  if(g_inputHostServerPipe){inputHostPipeCloseOwned(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}
  fake::completeConnect=true;processRuntime().maintenance();
  c.check(processRuntime().stopped(),"actual Runtime has returned original/duplicate/event/operation resources");
  c.check(fake::duplicateClosesWhilePending==0,"no issuing duplicate/event closed while pending");
 });};
 run("pre-signalled-no-dispatch",[&]{
  auto old=scope();c.check(bool(old),"create original public scope");InputHostRequestPowerScope outer(old);
  BorrowedCancellationOwner mirror;
  auto link=old?old->linkCancellationEvent(mirror.event):nullptr;
  c.check(link&&WaitForSingleObject(mirror.event,0)==WAIT_TIMEOUT,"linked borrowed event is not signalled during capture");
  revoke();json reply;
  c.check(WaitForSingleObject(mirror.event,0)==WAIT_OBJECT_0,"original public revocation signals the shared event object");
  link.reset();
  c.check(WaitForSingleObject(mirror.event,0)==WAIT_OBJECT_0,"closing the link duplicate leaves the borrowed event usable");
  const auto before=processRuntime().snapshot().admitted;
  const bool accepted=preparedRequest("HIDHIDE_HIDE",&reply,{{"deviceInstanceId","fixture-owned"}});
  c.check(!accepted&&reply.value("status","")=="power-context-retired","actual public request admission rejects pre-signalled scope");
  c.check(fake::connects+fake::writes+fake::reads==0&&commandSequence==0&&faultBranches==0&&processRuntime().snapshot().admitted==before,"no Connect, issue, sequence burn or transport fault");
  c.check(fixtureOfferCalls==0,"retired scope does not enter the remote offer port");
 });
 for(const bool ready:{true,false})run(ready?"physical-forward-ready":"physical-forward-resuming",[&]{
  g_powerLifecycle=ready?PowerLifecycle::Ready:PowerLifecycle::Resuming;json reply;
  const bool accepted=preparedRequest("HIDHIDE_CLOAK",&reply,{{"cloak",true}});
  c.check(accepted==ready&&fake::writes==(ready?1u:0u),"payload-aware physical forward requires actual powerAdmitted Ready");
  c.check(faultBranches==0&&commandSequence==(ready?1u:0u),"derived hardware gate is admission, not a fabricated transport failure");
 });
 run("remote-offer-unavailable-no-dispatch",[&]{
  for(const auto mode:{FixtureOffer::NoTransport,FixtureOffer::Rejected}){
   fixtureOffer=mode;json reply;
   c.check(!preparedRequest("HIDHIDE_CLOAK",&reply,{{"cloak",true}})&&
    reply.value("status","")=="power-context-retired"&&
    reply.value("detail","")=="authenticated-Host-power-offer-unavailable"&&
    !reply.value("deviceWriteAttempted",true),"actual remote offer failure is not a dispatch or a Host ACK");
  }
  c.check(fixtureOfferCalls==1&&fake::connects==0&&fake::writes==0&&fake::reads==0&&
   commandSequence==0&&faultBranches==0,"missing and rejecting remote port leave original transport untouched");
 });
 for(const auto payload:{json::object(),json{{"cloak","false"}},json{{"cloak",nullptr}}})run("cloak-invalid-"+payload.dump(),[&]{
  g_powerLifecycle=PowerLifecycle::Resuming;json reply;
  c.check(!preparedRequest("HIDHIDE_CLOAK",&reply,payload)&&fake::writes==0&&faultBranches==0,"only explicit boolean false gains release-only exception");
 });
 struct Cleanup{const char* name;const char* command;bool cloakFalse;};
 static constexpr Cleanup cleanup[]={
  {"cloak-false","HIDHIDE_CLOAK",true},{"cycle-poll","HIDHIDE_CYCLE_POLL",false},
  {"keepalive","KEEPALIVE",false},{"quiesce","QUIESCE",false},
  {"release","RELEASE_TARGET",false},{"shutdown","SHUTDOWN",false},{"neutral","SUBMIT_NEUTRAL",false}};
 for(const auto& row:cleanup)run(std::string("cleanup/")+row.name,[&]{
  auto old=scope();InputHostRequestPowerScope outer(old,true);revoke();g_powerLifecycle=PowerLifecycle::Resuming;
  t_inputHostDeferredPhysicalHideWrite=reinterpret_cast<const InputHostDeferredPhysicalHide*>(std::uintptr_t(1));
  g_inputHostFrameReplyBinding="fixture-old-binding";g_inputHostCompactFrameRequest=true;g_inputHostGameMotorBindingGeneration=1;
  json reply;const bool accepted=preparedRequest(row.command,&reply,row.cloakFalse?json{{"cloak",false}}:json::object());
  c.check(accepted&&fake::writes==1&&fake::reads==1&&commandSequence==1&&faultBranches==0,"actual admission/RAII/issue fragments preserve cleanup under stale outer forward scope");
  c.check(t_inputHostRequestPower==old&&t_inputHostRequestHardwareRequired,"actual request RAII restores predecessor scope and hardware role");
  const bool retires=std::strcmp(row.command,"QUIESCE")==0||std::strcmp(row.command,"RELEASE_TARGET")==0||std::strcmp(row.command,"SHUTDOWN")==0;
  c.check(fixtureOfferCalls==0&&fixtureForgottenTargets==(retires?1u:0u)&&
   g_inputHostGameMotorBindingGeneration==(retires?0u:1u)&&
   g_inputHostFrameReplyBinding.empty()==retires&&g_inputHostCompactFrameRequest!=retires,
   "cleanup neither mints a remote offer nor revokes bindings for a non-retiring command");
  t_inputHostDeferredPhysicalHideWrite=nullptr;
 });
 for(const bool pending:{true,false})run(pending?"revoked-pending-connect-retirement":"revoked-terminal-connect-retirement",[&]{
  auto old=scope();InputHostRequestPowerScope outer(old);
  auto sample=makeSample();InputFrameSubmission frame{};frame.ticket={g_inputHostEpoch,power,0,false};
  frame.sample=inputHostFrameSubmissionSample(&sample);frame.sourcePower=sample.sharedPower;frame.powerScope=old;frame.physicalSourceAdmitted=true;
  InputFrameSubmissionScope frameScope(&frame);
  fake::pendingConnect=pending;fake::connectFailure=pending?ERROR_SUCCESS:ERROR_BROKEN_PIPE;fake::connectHook=revoke;
  json reply;const bool accepted=preparedRequest("SUBMIT_FRAME",&reply);
  const auto retained=processRuntime().snapshot();
  c.check(!accepted&&fake::connects==1&&fake::writes==0&&fake::reads==0&&frame.rejected,"actual failed issued Connect revokes original frame without a following write");
  c.check(faultBranches==1&&g_inputHostTransportFault&&!g_inputHostServerPipe&&!g_inputHostPipeConnected&&retained.activeLeases==0,"actual failure-retirement fragment remains reachable after frame rejection");
  c.check(retained.pending==(pending?1u:0u),"pending IO retains exact operation; terminal failure returns it");
  logs.push_back({{"domain","request-connect-retirement"},{"row",{{"pending",retained.pending},{"retainedLeases",retained.retainedLeases},{"fault",g_inputHostTransportFault},{"sequence",commandSequence}}}});
  fake::completeConnect=true;processRuntime().maintenance();c.check(processRuntime().stopped(),"later terminal Connect proof reaps original owned resources");
 });
 run("write-revoked-read-drains-to-final-current",[&]{
  auto old=scope();InputHostRequestPowerScope outer(old);fake::writeHook=revoke;json reply;
  const bool accepted=preparedRequest("HIDHIDE_HIDE",&reply,{{"deviceInstanceId","fixture-owned"}});
  c.check(!accepted&&fake::writes==1&&fake::reads==1&&commandSequence==1&&faultBranches==0,"issued write still drains read, actual final-current block syncs wire only");
  c.check(reply.value("status","")=="power-context-retired"&&reply.value("wireSequenceSynchronized",false),"actual receipt helper records late wire sync without business success");
 });
 struct ReceiptVariant{const char* name;const char* status;const char* detail;bool consumed;};
 static constexpr ReceiptVariant receipts[]={
  {"success","fixture-ok","",true},{"business-rejected","operation-failed","device-busy",true},
  {"asset-mismatch","asset-mismatch","",false},{"sequence-stale-or-gap","protocol-error","sequence-stale-or-gap",false},
  {"tuple-mismatch","protocol-error","tuple-mismatch",false},{"hello-required","protocol-error","hello-required",false},
  {"hello-already-received","protocol-error","hello-already-received",false},
  {"startup-nonce-mismatch","protocol-error","startup-nonce-mismatch",false},
  {"request-id-content-mismatch","protocol-error","request-id-content-mismatch",false},
  {"other-protocol-error","protocol-error","other-rejection-after-gate",true}};
 for(const auto& row:receipts)for(const bool advance:{true,false})run(std::string("post-validation/")+row.name+(advance?"/advance":"/probe"),[&]{
  auto old=scope();InputHostRequestPowerScope outer(old);revoke();json reply;
  const json response={{"status",row.status},{"detail",row.detail}};
  const bool accepted=fixtureValidatedReceiptEntry("HELLO",response,advance,9,&reply);
  c.check(!accepted&&fixtureBusinessConsumers==0,"actual r6 pre-business block rejects stale success/rejection before successor consumers");
  c.check(commandSequence==(advance&&row.consumed?9u:0u)&&reply.value("wireSequenceSynchronized",!row.consumed)==row.consumed,"actual original sequence-gate helper distinguishes consumed and pre-gate rejection");
  c.check(faultBranches==0&&!g_inputHostTransportFault,"scope retirement alone does not claim a broken transport");
 });
 run("post-validation/current-control",[&]{
  auto current=scope();InputHostRequestPowerScope outer(current);json reply;
  c.check(fixtureValidatedReceiptEntry("HELLO",{{"status","fixture-ok"}},true,9,&reply)&&fixtureBusinessConsumers==1&&commandSequence==0,"current receipt crosses actual pre-business guard; downstream consumers are excluded");
 });
}

// Additional explicit parameters of the original test, excluded from defaults.
static void runLocalPublicVetoCases(Checks& c){
 const auto revoke=[](){++fixtureNotification;g_sharedPowerRevocations.revoke();};
 for(const bool legacy:{false,true})for(unsigned stage=0;stage<3;++stage)for(const bool ui:{false,true}){
  const std::string name="public-power-veto/"+std::string(legacy?"legacy/":"prepared/")+(ui?"ui-neutral/":"capture/")+std::to_string(stage);
  c.run(name.c_str(),[&]{
   auto sample=makeSample();auto original=ui?nullptr:&sample;
   std::shared_ptr<ymcc::inputHost::sharedPower::Scope> old;
   if(stage==0){old=ymcc::inputHost::sharedPower::Scope::create(&readSharedSystemPower,g_sharedPowerRevocations,true,ymcc::inputHost::sharedPower::Use::Input);}
   InputHostRequestPowerScope outer(old);
   const auto originalProcess=g_inputHostProcess;const auto originalEpoch=g_inputHostEpoch;
   auto r=submit(original,true,ui,[&]{
    if(stage==0)revoke();
    else if(stage==1)fake::connectHook=revoke;
    else{g_inputHostServerPipe=inputHostCreateServerPipeLocked();g_inputHostPipeConnected=true;fake::createHook=revoke;}
   },legacy);
   c.check(!r.sent&&r.contextRejected&&r.hostSubmission=="frame-skipped-context-retired","actual caller and final result classify local public veto, including UI without TLS");
   c.check(fake::writes==0&&fake::reads==0&&commandSequence==0,"public veto cannot issue bytes or consume sequence");
   c.check(faultBranches==0&&!g_inputHostFaultHoldsStart&&!g_inputHostTransportFault,"local pre-issue veto cannot create backend hold or transport fault");
   c.check(g_inputHostProcess==originalProcess&&g_inputHostEpoch==originalEpoch&&g_inputHostPrepared&&g_inputHostNeutralized,"veto preserves exact target and session");
   t_inputHostRequestPower.reset();t_inputHostRequestHardwareRequired=false;
   auto fresh=makeSample();fresh.readAttemptSequence=10;
   const auto next=submit(ui?nullptr:&fresh,true,ui,{},legacy);
   c.check(next.sent&&fake::writes==1&&commandSequence==1&&!g_inputHostFaultHoldsStart,"next current scope resumes original target without rearm");
   c.check(g_inputHostProcess==originalProcess&&g_inputHostEpoch==originalEpoch&&newHosts==0,"no replacement Host or target created for recovery");
  });
 }
 for(const bool legacy:{false,true})c.run((std::string("public-power-veto/real-backend/")+(legacy?"legacy":"prepared")).c_str(),[&]{
  auto s=makeSample();const auto r=submit(&s,true,false,{},legacy,true);
  c.check(!r.sent&&!r.contextRejected&&g_inputHostFaultHoldsStart&&faultBranches==1,"actual backend hold still applies to a real backend rejection");
 });

 for(const bool legacy:{false,true})for(const bool ui:{false,true})c.run((std::string("public-power-veto/late-ack/")+(legacy?"legacy/":"prepared/")+(ui?"ui-neutral":"capture")).c_str(),[&]{
  auto s=makeSample();fake::writeHook=revoke;const auto host=g_inputHostProcess;const auto epoch=g_inputHostEpoch;
  const auto r=submit(ui?nullptr:&s,true,ui,{},legacy);
  c.check(!r.sent&&r.contextRejected&&fake::writes==1&&fake::reads==1&&commandSequence==1,"old ACK is actually drained and sequence synchronized before refusing current success");
  c.check(faultBranches==0&&!g_inputHostFaultHoldsStart&&!g_inputHostTransportFault,"post-wire retirement is local rejection even without capture TLS");
  auto fresh=makeSample();fresh.readAttemptSequence=10;const auto next=submit(ui?nullptr:&fresh,true,ui,{},legacy);
  c.check(next.sent&&commandSequence==2&&g_inputHostProcess==host&&g_inputHostEpoch==epoch&&newHosts==0,"fresh scope resumes same Host after drained old ACK, no command replay");
 });
 for(const bool ui:{false,true})c.run((std::string("public-power-veto/connect-fault-and-revocation/")+(ui?"ui-neutral":"capture")).c_str(),[&]{
  auto s=makeSample();fake::connectHook=revoke;fake::connectFailure=ERROR_BROKEN_PIPE;
  const auto r=submit(ui?nullptr:&s,true,ui);
  c.check(!r.sent&&g_inputHostTransportFault&&!g_inputHostServerPipe&&fake::writes==0,"real failed Connect still retires transport after simultaneous public revocation");
  c.check(r.transportFaultHandled&&!g_inputHostNeutralized&&r.hostSubmission=="not-admitted-or-failed","local context flag cannot mask real transport fault in handling or final output");
 });
 c.run("public-power-veto/existing-hold",[&]{
  g_inputHostFaultHoldsStart=true;auto s=makeSample();const auto r=submit(&s,true);
  c.check(!r.sent&&fake::writes==0&&g_inputHostFaultHoldsStart,"local classification never clears an existing backend hold");
 });
 c.run("public-power-veto/non-frame-ticket",[&]{
  auto scope=ymcc::inputHost::sharedPower::Scope::create(&readSharedSystemPower,g_sharedPowerRevocations,true,ymcc::inputHost::sharedPower::Use::Lifecycle);
  InputHostRequestPowerScope outer(scope);InputFrameSubmission frame;revoke();json receipt;
  c.check(!preparedRequest("HELLO",&receipt,json::object(),&frame)&&!frame.rejected&&fake::writes==0,"HELLO veto cannot mark a SUBMIT_FRAME local ticket");
 });
}
