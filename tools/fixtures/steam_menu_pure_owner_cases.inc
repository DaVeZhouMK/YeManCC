static int menuPureOwnerFixture(bool firstOnly,const std::string& selected={}) {
 Json cases=Json::array();unsigned checks=0;
 auto require=[&](bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);};
 auto fresh=[] {ymcc::steamsession::IdentitySnapshot f;f.session={L"P:\\YMCC-identity-fixture",processIdentityPid,42,processIdentityBirth};f.presence=1;f.registeredPid=processIdentityPid;f.rootReadError=f.registeredPidReadError=f.accountReadError=ERROR_SUCCESS;f.processEpoch=f.accountEpoch=f.revision=1;return f;};
 bool owner=true;unsigned admissions=0,ownerChecks=0;
 auto reset=[&]{processIdentityProbe=dispatchProbe=true;admissionProbe=true;
   processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;
   dispatchRead=0;dispatchExitOnUpgrade=dispatchSendFails=false;dispatchReplyHook={};dispatchUpgradeHook={};
   dispatchReplyValue=R"({"ok":true,"mutated":false})";g_exitRequested=false;g_aiFanMockSession.isolated=false;
   g_steamSettingsObserver.onRead={};g_steamSettingsObserver.publish(fresh());ymcc::processInventory::invalidate();
   owner=true;admissions=ownerChecks=0;};
 auto current=[&]{++ownerChecks;return owner;};
 auto admit=[&]{++admissions;return true;};
 auto invoke=[&](const char* operation){return steamLiveRequest(42,{{"operation",operation}},admit,current);};
 auto test=[&](const char* name,const auto& body){if(selected=="pre-dispatch"&&std::string(name)!="owner-ended-during-upgrade"&&std::string(name)!="owner-ended-by-final-admit")return;reset();try{body();
   require(processIdentityOpens==processIdentityCloses&&dispatchHttpOpen==dispatchHttpClose&&processIdentityUnexpected==0,"original-owned-resources-returned");
   cases.push_back({{"name",name},{"passed",true}});
  }catch(const std::exception& error){cases.push_back({{"name",name},{"passed",false},{"assertion",error.what()}});}};
 test("stale-owner-no-reader",[&]{owner=false;const auto opens=processIdentityOpens,ports=admissionPortReads.load(),sends=dispatchSends;
   const auto r=invoke("menu.button");
   require(!r.value("ok",true)&&r.value("reason",std::string{})=="live-action-cancelled","stale-feature-owner-rejected-before-discovery");
   require(processIdentityOpens==opens&&admissionPortReads.load()==ports&&dispatchSends==sends&&admissions==0,"stale-owner-no-reader-or-final-admission");});
 if(!firstOnly){
 test("throwing-owner-no-reader",[&]{const auto opens=processIdentityOpens,sends=dispatchSends;
   const auto r=steamLiveRequest(42,{{"operation","menu.button"}},admit,[]()->bool{throw std::runtime_error("expired");});
   require(!r.value("ok",true)&&r.value("reason",std::string{})=="live-action-cancelled"&&processIdentityOpens==opens&&dispatchSends==sends&&admissions==0,"throwing-feature-owner-fails-closed");});
 test("owner-ended-during-upgrade",[&]{const auto sends=dispatchSends;dispatchUpgradeHook=[&]{owner=false;};const auto r=invoke("menu.button");
   require(!r.value("ok",true)&&(!r.contains("requestDispatched")||r["requestDispatched"]==Json(false))&&dispatchSends==sends&&admissions==0,"pure-owner-current-at-upgrade-before-final-admit");});
 test("owner-ended-by-final-admit",[&]{const auto sends=dispatchSends;const auto r=steamLiveRequest(42,{{"operation","menu.button"}},[&]{++admissions;owner=false;return true;},current);
   require(!r.value("ok",true)&&(!r.contains("requestDispatched")||r["requestDispatched"]==Json(false))&&dispatchSends==sends&&admissions==1,"owner-rechecked-after-one-final-admit");});
 test("late-read-cannot-acknowledge",[&]{dispatchReplyHook=[&]{owner=false;};const auto sends=dispatchSends;const auto r=invoke("settings.get");
   require(!r.value("ok",true)&&!r.value("mutated",true)&&r.value("reason",std::string{})=="live-action-cancelled"&&dispatchSends==sends+1&&admissions==1,"late-read-cannot-publish-current-result");});
 test("late-write-remains-uncertain",[&]{dispatchReplyHook=[&]{owner=false;};const auto sends=dispatchSends;const auto r=invoke("settings.set");
   require(!r.value("ok",true)&&r.value("mutated",false)&&r.value("reason",std::string{})=="live-action-cancelled"&&dispatchSends==sends+1&&admissions==1,"sent-write-owner-loss-keeps-uncertainty-no-replay");});
 test("healthy-owner-single-admit",[&]{const auto sends=dispatchSends;const auto r=invoke("menu.button");
   require(r.value("ok",false)&&dispatchSends==sends+1&&admissions==1&&ownerChecks>1,"healthy-pure-owner-keeps-one-original-dispatch");});
 test("legacy-admit-single-call",[&]{const auto sends=dispatchSends;const auto r=steamLiveRequest(42,{{"operation","menu.button"}},admit);
   require(r.value("ok",false)&&dispatchSends==sends+1&&admissions==1&&ownerChecks==0,"legacy-side-effect-admit-is-not-repeated");});
 }
 unsigned failed=0;for(const auto& c:cases)if(!c.value("passed",false))++failed;
 processIdentityProbe=dispatchProbe=false;admissionProbe=false;dispatchUpgradeHook={};dispatchReplyHook={};g_exitRequested=false;g_aiFanMockSession.isolated=false;
 std::cout<<Json{{"selection",firstOnly?"menu-pure-owner-before":selected.empty()?"menu-pure-owner":"menu-pure-owner/pre-dispatch"},{"cases",cases},{"checks",checks},{"failedCases",failed},
   {"actualMainWrapper",true},{"osAndNetworkLeavesControlled",true},{"realSteam",false},{"defaultGroupExecuted",false},{"newCanonicalIds",0}}.dump()<<"\n";
 return failed?1:0;
}
