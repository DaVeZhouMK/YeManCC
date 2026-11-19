// Optional case of controller_oem_recording_native_selftest.mjs. Only packet
// arrival, serial admission, notification and OS keyboard IO are memory leaves.
static void vendorPacket(BYTE id,bool down,size_t bytes=65,uint64_t generation=0){
 std::vector<BYTE> packet(bytes,0);const size_t offset=bytes==64?0:1;
 packet[offset]=kOnexBackBtnCmd;packet[offset+1]=kOnexBackFrame;
 packet[offset+6]=id;packet[offset+12]=down?1:0;packet[bytes-2]=kOnexBackFrame;
 onexBackParse(packet.data(),packet.size(),generation?generation:g_onexBack.actionGeneration);
}
static void vendorNeutral(){nativeKeyboardShortcutsEvaluate(0,g_curPad,GetTickCount64());}
static void vendorRule(uint64_t mask,const std::string& id="vendor-action"){
 NativeKeyboardShortcutRule rule;rule.requiredMask=mask;rule.outputId=id;rule.virtualKey='K';
 const auto resolve=[](const std::string& action,const std::string& code,unsigned short& vk,bool& shift){vk='K';shift=false;return action=="keyboard.key"&&code=="KeyK";};
 check(ymcc::keyboardSequence::parse(json{{"key","KeyK"}},"keyboard.key",resolve,rule.keyboardPlan),"actual sequence plan parses "+id);
 g_nativeKeyboardShortcuts={rule};
}
static void vendorBegin(){
 if(g_onexBack.actionGeneration)oemProducerCancel(OemProducerId::OneX,g_onexBack.actionGeneration);
 g_onexBack={};g_onexBack.desc=onexCapDescriptorFor(g_machineIdentity.deviceClass);
 g_onexBack.actionGeneration=oemProducerBegin(OemProducerId::OneX);
 g_oemKeyMaskHid=0;g_oemBackState=0;displayMask=0;
}
static void vendorExpectPair(const std::string& label){
 const auto jobs=actionJobs.size();check(jobs==1,"first real vendor edge admits exactly one action "+label);
 actionDrain();if(jobs)waitReleased();
 {std::lock_guard<std::mutex> lock(actionTraceMx);check(actionTrace==std::vector<std::pair<WORD,bool>>{{'K',false},{'K',true}},"real worker emits one matching down/up "+label);actionTrace.clear();}
}
static void vendorExpectSilent(const std::string& label){
 check(actionJobs.empty(),"no admitted action "+label);actionDrain();check(traceSize()==0,"no keyboard output "+label);clearTrace();
}
int main(){
 registerIpc();nativeShortcutActionRegisterIpc();physicalAuthority=dinputAuthority=0;wmi=false;eligible=true;custom=false;displayMask=0;
 unsigned modelCount=0,operationCount=0;json coverage=json::array();
 for(const auto& model:selectedActionModels){
  const auto descriptor=onexCapDescriptorFor(model);if(!descriptor.valid)continue;
  ++modelCount;g_machineIdentity.family=YmccFamily::OneXPlayer;g_machineIdentity.deviceClass=model;
  const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,model);g_oemBackProfileId=binding.profileId;
  const std::array<std::pair<BYTE,uint64_t>,4> buttons={{{kOnexBackL4Id,kOemBitL4},{kOnexBackR4Id,kOemBitR4},{kOnexBackSpecialId,descriptor.role21Orange?kOemBitOrange:kOemBitSpecial},{kOnexBackKeyboardId,kOemBitKeyboard}}};
  for(const auto [id,bit]:buttons){
   ++operationCount;const std::string label=model+" button="+std::to_string(id);
   bool catalogued=false;for(int n=0;n<binding.catalog.count;++n){uint64_t declared=0;const auto& key=binding.catalog.keys[n];if(nativeKeyboardShortcutMaskBit(key.keyId,declared)&&declared==bit&&oemKeyCaptureImplemented(g_machineIdentity.family,model,key.keyId))catalogued=true;}
   check(catalogued,"actual catalog admits tested vendor operation "+label);
   // The rule has already seen neutral, but the newly observed button ID gains
   // authority only on this first validated DOWN. No synthetic keyboard edge.
   vendorBegin();vendorRule(bit);vendorNeutral();check(!g_nativeKeyboardShortcuts[0].needsNeutral,"rule neutral established before source event "+label);
   vendorPacket(id,true);vendorPacket(id,true);vendorPacket(id,false);nativeOemProducerDrain();
   check(g_oemProducerEdgeCount==0,"actual FIFO fully drained "+label);vendorExpectPair(label+" first authority");
   // A later arrival of the source must not erase an already established rule
   // neutral. Rule edits themselves retain their independent neutral fence.
   oemProducerCancel(OemProducerId::OneX,g_onexBack.actionGeneration);g_onexBack.actionGeneration=0;
   vendorRule(bit);vendorNeutral();vendorBegin();vendorPacket(id,true,64);vendorPacket(id,false,64);nativeOemProducerDrain();vendorExpectPair(label+" late generation legacy64");
   // Notification failure retains the same actual receipt for the ordinary UI
   // drain. It must not turn a valid first press into a lost one.
   vendorBegin();vendorRule(bit);vendorNeutral();fixturePostAccepts=false;
   vendorPacket(id,true);vendorPacket(id,false);check(g_oemProducerEdgeCount==2,"post failure retains both packet edges "+label);
   fixturePostAccepts=true;nativeOemProducerDrain();vendorExpectPair(label+" retained post failure");
   // A new rule observing a held button never borrows neutral from an older rule.
   vendorBegin();g_nativeKeyboardShortcuts.clear();vendorPacket(id,true);nativeOemProducerDrain();
   vendorRule(bit);vendorNeutral();vendorPacket(id,true);nativeOemProducerDrain();vendorExpectSilent(label+" held across new rule");
   vendorPacket(id,false);nativeOemProducerDrain();vendorPacket(id,true);vendorPacket(id,false);nativeOemProducerDrain();vendorExpectPair(label+" actual release after new rule");
   // A first UP observes neutral only; it is not an action.
   vendorBegin();vendorRule(bit);vendorNeutral();vendorPacket(id,false);nativeOemProducerDrain();vendorExpectSilent(label+" first up");
   vendorPacket(id,true);vendorPacket(id,false);nativeOemProducerDrain();vendorExpectPair(label+" press after actual up");
   // Cancellation before FIFO drain removes old ownership, and late packets
   // cannot resurrect the cancelled generation.
   vendorBegin();vendorRule(bit);vendorNeutral();const auto stale=g_onexBack.actionGeneration;
   vendorPacket(id,true);oemProducerCancel(OemProducerId::OneX,stale);nativeOemProducerDrain();vendorPacket(id,true,65,stale);nativeOemProducerDrain();vendorExpectSilent(label+" cancelled source and stale packet");
   // Also cancel after evaluator admission: the real serial context guard must
   // reject the old generation before the keyboard worker can execute it.
   vendorBegin();vendorRule(bit);vendorNeutral();vendorPacket(id,false);nativeOemProducerDrain();vendorPacket(id,true);nativeOemProducerDrain();
   check(actionJobs.size()==1,"current source admitted before cancellation "+label);
   oemProducerCancel(OemProducerId::OneX,g_onexBack.actionGeneration);actionDrain();check(traceSize()==0,"cancelled generation cannot reach keyboard leaf "+label);
   // Rule epoch and recording ownership retain the same cancellation boundary.
   for(const bool recording:{false,true}){
    vendorBegin();vendorRule(bit);vendorNeutral();vendorPacket(id,false);nativeOemProducerDrain();vendorPacket(id,true);
    if(recording)g_shortcutRecordingActive=true;else g_oemShortcutEdgeEpoch.fetch_add(1);
    nativeOemProducerDrain();vendorExpectSilent(label+(recording?" recording owner":" stale rule epoch"));g_shortcutRecordingActive=false;
   }
   // Invalid input cannot manufacture source authority or keyboard output.
   vendorBegin();vendorRule(bit);vendorNeutral();vendorPacket(0x7f,true);
   BYTE malformed[65]={};onexBackParse(malformed,sizeof(malformed),g_onexBack.actionGeneration);nativeOemProducerDrain();
   check(oemProducerAuthorityMask()==0,"malformed and unknown-ID reports have no authority "+label);vendorExpectSilent(label+" invalid reports");
  }
  // Snapshot sources keep their original first-held suppression. This is a
  // semantic regression check on the same evaluator, not a GPD hardware claim.
  oemProducerCancel(OemProducerId::OneX,g_onexBack.actionGeneration);g_oemKeyMaskHid=0;g_nativeKeyboardShortcuts.clear();
  const auto snapshot=oemProducerBegin(OemProducerId::Gpd);vendorRule(kOemBitL4);vendorNeutral();
  oemProducerPublish(OemProducerId::Gpd,snapshot,kOemBitL4);nativeOemProducerDrain();vendorExpectSilent(model+" snapshot first held remains disarmed");
  oemProducerPublish(OemProducerId::Gpd,snapshot,0);nativeOemProducerDrain();oemProducerPublish(OemProducerId::Gpd,snapshot,kOemBitL4);nativeOemProducerDrain();oemProducerPublish(OemProducerId::Gpd,snapshot,0);nativeOemProducerDrain();vendorExpectPair(model+" snapshot actual release then press");
  oemProducerCancel(OemProducerId::Gpd,snapshot);
  coverage.push_back({{"model",model},{"vendorOperations",buttons.size()},{"role21",descriptor.role21Orange?"orange":"special"}});
 }
 check(modelCount==6&&operationCount==24,"six exact legacy vendor descriptors and 24 physical operations retained");
 check(fixtureReinitRequests==0,"button scenarios never request hardware initialization");
 nativeKeyboardSequenceStop();
 json report={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"selectedCases","onex-vendor-actions"},{"models",modelCount},{"operationEntries",operationCount},{"modelCoverage",coverage},{"actualPacketParser",true},{"actualProducerStateAndFifo",true},{"actualAuthorityAndGeneration",true},{"actualNativeEvaluator",true},{"actualDispatcherAndKeyboardWorker",true},{"ruleInputBoundary","native rule structure; existing editor/settings parser cases separate"},{"sourceBindingBoundary","actual event producer generation; OS device open/read and init excluded"},{"contextBoundary","legacy OneX/GPD always-current branch; X2 power/topology excluded"},{"osKeyboardLeaf","memory only"},{"virtualOutput","disabled"},{"hardwareIo",false},{"physicalActionSuccessProven",false}};
 std::ofstream("RESULT.json")<<report.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}
