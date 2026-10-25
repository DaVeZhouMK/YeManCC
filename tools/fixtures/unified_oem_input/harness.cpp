static unsigned checks=0,selections=0,pressCycles=0;
static void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
static void setIdentity(YmccFamily family,const std::string& cls){g_machineIdentity.family=family;g_machineIdentity.deviceClass=cls;g_machineIdentity.resolved=true;}
static void clearKeys(){g_oemKbDown=0;g_oemKbDownRaw.store(0);g_oemMouseLeft=g_oemMouseX2=false;oemBackHookRecompute();}
static void checkAliasConsumers(){
 for(const auto family:families)for(const auto& row:aliases){
  testContainerReady=testUsbReady=false;g_rog={};
  const std::string canonical(row.canonical);
  const int expected=oemChordProfileIdFor(family,canonical);
  const auto oldProfile=g_oemBackProfileId.load();const auto oldEpoch=g_oemShortcutEdgeEpoch.load();
  setIdentity(family,row.alias);oemBackMappingRefreshGate(true,"steamdeck",true);
  check(g_oemBackProfileId.load()==expected,"full gate consumes unified canonical profile");
  check(g_oemShortcutEdgeEpoch.load()==oldEpoch+(oldProfile==expected?0u:1u),"full gate keeps profile-change epoch");
  check(g_oemHooksWanted.load()==(oemChordProfileFor(expected).count>0),"full gate uses original hook capability");
  check(g_oemBackMappingEnabled.load()==(family!=YmccFamily::ValveSteamDeck),"physical Deck mapping exclusion retained");
  const auto selected=oemBoundCatalogForFamily(family,row.alias),original=oemCatalogForFamily(family,canonical);
  check(selected.keys==original.keys&&selected.count==original.count,"original static key table is consumed by reference");
  const auto actualQuery=actualOemKeysGet();const auto actualPressed=gamepadOemPressedKeys(~uint64_t(0));
  setIdentity(family,canonical);
  const auto canonicalQuery=actualOemKeysGet();const auto canonicalPressed=gamepadOemPressedKeys(~uint64_t(0));
  check(actualQuery==canonicalQuery,"query aliases retain complete canonical capture predicates and routes");
  check(actualPressed==canonicalPressed,"pressed enumeration aliases retain canonical key IDs");
  json expectedPressed=json::array();for(int i=0;i<original.count;++i){uint64_t bit=0;if(nativeKeyboardShortcutMaskBit(original.keys[i].keyId,bit)&&bit>=(1ull<<20))expectedPressed.push_back(original.keys[i].keyId);}
  check(actualPressed==expectedPressed,"pressed enumeration reaches every original canonical key");
  check(actualQuery.at("keys").size()==static_cast<size_t>(original.count),"query executes every selected key");
  const auto& profile=oemChordProfileFor(expected);
  for(int index=0;index<profile.count;++index){
   const auto& rule=profile.rules[index];uint64_t wantMask=0;int wantState=0,wantPhysical=0;
   oemChordEvalRules(expected,rule.keys,rule.mouseChord,true,&wantMask,&wantState,&wantPhysical,nullptr,nullptr);
   g_gpdBackIfaceReady.store(true);g_gpdBackIfaceGen.store(71);
   g_oemKbDown=rule.keys;g_oemKbDownRaw.store(0);g_oemMouseLeft=g_oemMouseX2=rule.mouseChord;
   oemBackHookRecompute();
   check(g_oemKeyMask.load()==wantMask&&g_oemBackStateKb.load()==wantState,"actual hook publishes selected original rule");
   clearKeys();check(g_oemKeyMask.load()==0&&g_oemBackStateKb.load()==0,"actual key-up clears published state");++pressCycles;
  }
  ++selections;
 }
}
static void checkFreshGatesAndReturn(){
 setIdentity(YmccFamily::Gpd,"HandheldCompanion.Devices.GPDWin5");oemBackMappingRefreshGate(true,"steamdeck",true);
 for(const bool ready:{true,false,true,false}){
  g_gpdBackIfaceReady.store(ready);g_gpdBackIfaceGen.fetch_add(1);g_oemKbDown=kVkF14;g_oemKbDownRaw.store(0);g_oemMouseLeft=g_oemMouseX2=false;
  oemBackHookRecompute();
  check(g_oemBackStateKb.load()==(ready?1:0),"same cached model must recheck live GPD interface");
  check(g_gpdChordSnapPhysical.load()==1&&g_gpdChordPubGen.load()==g_gpdBackIfaceGen.load(),"physical diagnostic and publication generation retained");
  clearKeys();check(g_oemKeyMask.load()==0,"GPD refusal and acceptance both release");
 }
 setIdentity(YmccFamily::AsusRogAllyClassic,"hc.device.ROGAlly");g_rog.inputPath=L"memory-only-identity";
 for(const bool ready:{false,true,false,true}){
  testContainerReady=ready;testUsbReady=false;oemBackMappingRefreshGate(true,"steamdeck",true);
  check(g_oemRawPathActive.load()==ready&&g_oemHooksWanted.load()!=ready,"same cached model rechecks raw identity ownership");
 }
 testContainerReady=false;testUsbReady=true;oemBackMappingRefreshGate(true,"steamdeck",true);
 check(g_oemRawPathActive.load(),"existing USB-instance identity fallback retained");
 testUsbReady=false;oemBackMappingRefreshGate(true,"steamdeck",true);check(!g_oemRawPathActive.load()&&g_oemHooksWanted.load(),"lost identity falls back to original hook route");
 setIdentity(YmccFamily::Gpd,"HandheldCompanion.Devices.GPDWin5");g_rog={};oemBackMappingRefreshGate(true,"steamdeck",true);g_gpdBackIfaceReady.store(true);
 g_oemKbDown=kVkF14;g_oemKbDownRaw.store(kVkF15);g_oemMouseLeft=g_oemMouseX2=false;oemBackHookRecompute();
 oemRawPathReset("fixture-source-removed");
 check(g_oemKbDown==kVkF14&&g_oemKbDownRaw.load()==0,"raw source return preserves hook lane");
 check(g_oemBackStateKb.load()==1&&g_oemKeyMask.load()==kOemBitL4,"raw source return recomputes surviving hook input");
 clearKeys();
 for(const char* persona:{"disabled","xbox360","steamdeck","elite","dualsense-edge"})for(const bool target:{false,true}){
  oemBackMappingRefreshGate(target,persona,false);
  const std::string value(persona);const bool want=target&&(value=="steamdeck"||value=="elite"||value=="dualsense-edge");
  check(g_oemBackMappingEnabled.load()==want,"persona/target gate remains live and backMapping compatibility field does not control it");
 }
 const std::string deck="hc.device.SteamDeck";setIdentity(YmccFamily::ValveSteamDeck,deck);
 const auto query=actualOemKeysGet();check(query.at("keys").size()==2,"Deck retains two declared keys");
 for(const auto& key:query.at("keys"))check(key.at("captureImplemented")==false&&key.at("captureRoute")=="physical-neptune-reader-unimplemented","registered Deck alias must not invent a physical reader");
 for(const auto family:families)for(const char* cls:{"","unknown","HandheldCompanion.Devices.AYANEOFlip1SFuture"}){
  setIdentity(family,cls);oemBackMappingRefreshGate(false,"disabled",false);
  check(g_oemBackProfileId.load()==oemChordProfileIdFor(family,std::string(cls)),"unknown and reviewed extensions retain original family fallback");
  const auto original=oemCatalogForFamily(family,std::string(cls));check(actualOemKeysGet().at("keys").size()==static_cast<size_t>(original.count),"extension query fallback retained");
 }
}
int main(){try{
 checkAliasConsumers();checkFreshGatesAndReturn();
 std::cout<<json{{"status","PASS"},{"checks",checks},{"selections",selections},{"pressReleaseCycles",pressCycles},{"completeProductionGate",true},{"productionQuery",true},{"productionHook",true},{"hardwareIo",false}}.dump()<<'\n';return 0;
}catch(const std::exception& error){std::cout<<json{{"status","FAIL"},{"checks",checks},{"error",error.what()}}.dump()<<'\n';return 1;}}
