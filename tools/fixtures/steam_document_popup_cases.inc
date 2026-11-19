// Directed extension of the original document consumer. Only WinHTTP reply,
// process identity and listener inventory leaves are controlled. Product
// targets / expression / evaluate / validateReceipt / readStyles stay intact.
static std::string documentPopupReply(HINTERNET h){
 auto socket=documentPopupSockets.find(h);
 if(socket==documentPopupSockets.end()){++documentPopupUnexpected;throw std::runtime_error("fixture-socket-unmapped");}
 const auto path=socket->second.path;const auto id=socket->second.replyId;
 if(documentPopupReceiveHook)documentPopupReceiveHook(path,id);
 const auto leaf=documentPopupLeaves.find(path);
 if(leaf==documentPopupLeaves.end()){++documentPopupUnexpected;throw std::runtime_error("fixture-path-unmapped");}
 const auto& v=leaf->second;
 const auto href=id==2&&!v.finalHref.empty()?v.finalHref:v.href;
 const auto origin=id==2&&!v.finalOrigin.empty()?v.finalOrigin:v.origin;
 const auto key=id==2&&!v.finalKey.empty()?v.finalKey:v.key;
 const Json value{{"sourceOrigin",origin},{"sourceHref",href},{"documentKey",key},{"protocol",4},{"ready",true},{"styles",1},{"values",Json::array({"1"})},{"fileCounts",Json::array({1})}};
 return Json{{"id",id},{"result",{{"result",{{"type","object"},{"value",value}}}}}}.dump();
}
static int documentPopupFixture(int argc,char**argv){
 if((argc!=3&&argc!=5)||!argv[2][0]||std::string(argv[2]).starts_with("--")||
    (argc==5&&(std::string(argv[3])!="--case"||(std::string(argv[4])!="popup-three"&&std::string(argv[4])!="process-witness"&&std::string(argv[4])!="process-witness-control"&&std::string(argv[4])!="process-witness-tail"&&std::string(argv[4])!="process-witness-tail-control"&&std::string(argv[4])!="target-source-diagnostics"&&std::string(argv[4])!="bpm-blank"&&std::string(argv[4])!="bpm-blank-source-boundaries"&&std::string(argv[4])!="bpm-live-href"&&std::string(argv[4])!="bpm-live-href-control"&&std::string(argv[4])!="bpm-live-auxiliary-href"))))return 2;
 using namespace ymcc::steamdocuments;using ymcc::steamsession::Session;
 const bool bpmHref=argc==5&&(std::string(argv[4])=="bpm-live-href"||std::string(argv[4])=="bpm-live-href-control");const bool bpmControl=argc==5&&std::string(argv[4])=="bpm-live-href-control";const bool bpmAux=argc==5&&std::string(argv[4])=="bpm-live-auxiliary-href";
 const bool witnessTail=argc==5&&(std::string(argv[4])=="process-witness-tail"||std::string(argv[4])=="process-witness-tail-control");const bool tailControl=argc==5&&std::string(argv[4])=="process-witness-tail-control";
 const bool witness=argc==5&&(std::string(argv[4])=="process-witness"||std::string(argv[4])=="process-witness-control");const bool witnessControl=argc==5&&std::string(argv[4])=="process-witness-control";
 const bool single=argc==5;const bool targetDiagnostics=single&&std::string(argv[4])=="target-source-diagnostics";const bool bpmSource=single&&std::string(argv[4])=="bpm-blank-source-boundaries";const bool bpmBlank=single&&(std::string(argv[4])=="bpm-blank"||bpmSource);
 const auto out=CreateFileA(argv[2],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(out==INVALID_HANDLE_VALUE)return 2;
 Json raw{{"selection","document-popup-only"},{"defaultDocumentGroupExecuted",false},{"caseIds",Json::array()},{"cases",Json::array()},{"realSteamOperations",0},{"realNetworkOperations",0},{"realCssOperations",0}};
 Json* active=nullptr;unsigned checks=0;bool failed=false;
 auto check=[&](bool passed,const std::string& name){++checks;(*active)["checks"].push_back({{"name",name},{"passed",passed}});if(!passed)throw std::runtime_error(name);};
 processIdentityProbe=dispatchProbe=documentProbe=documentPopupMapping=true;admissionProbe=true;
 Session expected{L"P:\\YMCC-identity-fixture",processIdentityPid,0,processIdentityBirth};
 StylePlan plan;plan.sp=plan.mainMenu=plan.quickAccess=Properties{{"--ymcc-file-0000000000000000"},{"--ymcc-file-0000000000000000"}};
 const std::string sp="https://steamloopback.host/index.html?browserType=3",menu="about:blank?browserviewpopup=1&requestid=1&parentpopup=2",quick="about:blank?browserviewpopup=1&requestid=12345678901234567890&parentpopup=99999999999999999999";
 auto pages=[&](bool auxiliaries=true){Json tabs=Json::array();for(const auto& item:std::vector<std::tuple<std::string,std::string,std::string>>{{"main","Steam Big Picture",sp},{"menu","MainMenu_uid1",menu},{"quick","QuickAccess_uid2",quick}}){const auto&[id,title,url]=item;if(!auxiliaries&&id!="main")continue;tabs.push_back({{"id",id},{"type","page"},{"title",title},{"url",url},{"webSocketDebuggerUrl","ws://127.0.0.1:8080/devtools/page/"+id}});}return tabs;};
 auto setup=[&]{
  documentFinalTargetsRead=false;
  documentChangedTargets="[]";
  documentChildOpen=documentChildTimes=documentChildImage=true;documentChildWait=WAIT_TIMEOUT;documentParentOverride=0;documentTcp6Denied=false;
  processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;
  dispatchOwner=true;dispatchRead=documentTargetReads=0;dispatchExitOnUpgrade=dispatchSendFails=documentReplaced=documentTargetsChanged=false;dispatchReplyHook={};dispatchUpgradeHook={};modeDiscoveryHook={};
  documentPopupNext=documentPopupUnexpected=0;documentPopupRequests.clear();documentPopupSockets.clear();documentPopupActive.clear();documentPopupLeaves.clear();documentPopupReceiveHook={};
  // This is the controlled remote leaf state, independent of expected receipt values.
  documentPopupLeaves["/devtools/page/main"]={sp,"https://steamloopback.host","1700000000.125"};
  documentPopupLeaves["/devtools/page/menu"]={menu,"https://steamloopback.host","1700000001.25"};
  documentPopupLeaves["/devtools/page/quick"]={quick,"https://steamloopback.host","1700000002.5"};
  documentTargets=pages().dump();ymcc::processInventory::invalidate();
 };
 auto attempt=[&](DWORD budget=4000){auto receipt=readStyles(expected,plan,[&]{return dispatchOwner;},budget);(*active)["receipt"]=receipt;return receipt;};
 auto refused=[&](const Json& r,const char* reason){check(!r.value("ok",true)&&!r.value("complete",true)&&!r.value("mutated",true)&&r.at("documents").empty(),"reject-without-partial-success");check(r.value("reason",std::string{})==reason,std::string("reason:")+reason);};
 auto runCase=[&](const std::string& id,const std::function<void()>& body){
  if(failed||(tailControl&&id!="held-tail-owner-child-exit")||(witnessControl&&id!="held-three")||(bpmAux&&!id.starts_with("bpm-aux-live:"))||(bpmControl&&id!="bpm-three")||(bpmSource&&id!="bpm-auxiliary-not-sp"&&id!="bpm-bare-sp-still-rejected"&&id!="bpm-legacy-menu-sp-still-rejected"&&!id.starts_with("bpm-reject:")))return;setup();if(witnessTail||witness||bpmBlank||bpmHref||bpmAux)dispatchSends=0;raw["caseIds"].push_back(id);raw["cases"].push_back({{"id",id},{"checks",Json::array()},{"passed",false}});active=&raw["cases"].back();
  const auto pOpen=processIdentityOpens,pClose=processIdentityCloses,hOpen=dispatchHttpOpen,hClose=dispatchHttpClose;
  try{body();check(processIdentityOpens-pOpen==processIdentityCloses-pClose,"process-handles-balanced");check(dispatchHttpOpen-hOpen==dispatchHttpClose-hClose&&documentPopupActive.empty(),"http-and-distinct-sockets-balanced");check(documentPopupUnexpected==0&&processIdentityUnexpected==0,"only-controlled-leaf-handles");(*active)["passed"]=true;}
  catch(const std::exception& error){(*active)["error"]=error.what();failed=true;}
  (*active)["handles"]={{"processOpen",processIdentityOpens-pOpen},{"processClose",processIdentityCloses-pClose},{"httpOpen",dispatchHttpOpen-hOpen},{"httpClose",dispatchHttpClose-hClose},{"activeDistinct",documentPopupActive.size()},{"unexpected",documentPopupUnexpected}};
 };
 if(witnessTail){
  for(bool parent:{false,true})runCase(parent?"held-tail-owner-parent-exit":"held-tail-owner-child-exit",[&,parent]{unsigned finalCalls=0;const auto r=readStyles(expected,plan,[&]{if(documentFinalTargetsRead&&++finalCalls==2){if(parent)processIdentityAlive=false;else documentChildWait=WAIT_OBJECT_0;}return true;});(*active)["receipt"]=r;check(finalCalls==2,"controlled-original-final-owner-callback");check(!r.value("ok",true)&&r.at("documents").empty(),"tail-exit-must-not-publish-documents");check(dispatchSends==6,"same-original-final-evaluation-prefix");});
  runCase("held-parent-image-changes",[&]{documentPopupReceiveHook=[](const auto&,unsigned){processIdentityPath=false;};const auto r=attempt();check(!r.value("ok",true)&&r.at("documents").empty(),"fresh-held-parent-image-rejects-change");check(dispatchSends==1,"parent-path-change-stops-prefix");});
 }else if(witness){
  runCase("held-three",[&]{const auto before=processIdentityOpens;const auto r=attempt();check(r.value("ok",false)&&r.value("complete",false)&&r.at("documents").size()==3,"held-complete-three");check(processIdentityOpens-before==2,"exactly-root-and-child-held-open");check(dispatchSends==6,"original-six-evaluations");});
  runCase("held-open-denied",[&]{documentChildOpen=false;refused(attempt(),"document-read-cancelled");check(dispatchSends==0,"no-evaluate-on-child-open-denial");});
  runCase("held-times-denied",[&]{documentChildTimes=false;refused(attempt(),"document-read-cancelled");});
  runCase("held-image-foreign",[&]{documentChildImage=false;refused(attempt(),"document-read-cancelled");});
  runCase("held-wait-unknown",[&]{documentChildWait=WAIT_FAILED;refused(attempt(),"document-read-cancelled");});
  runCase("held-child-exits",[&]{documentPopupReceiveHook=[](const auto&,unsigned){documentChildWait=WAIT_OBJECT_0;};refused(attempt(),"document-read-cancelled");check(dispatchSends==1,"exit-stops-original-send-prefix");});
  runCase("held-parent-exits",[&]{documentPopupReceiveHook=[](const auto&,unsigned){processIdentityAlive=false;};refused(attempt(),"document-read-cancelled");});
  runCase("held-image-changes",[&]{documentPopupReceiveHook=[](const auto&,unsigned){documentChildImage=false;};const auto r=attempt();check(!r.value("ok",true)&&r.at("documents").empty(),"fresh-child-image-rejects-path-change");check(dispatchSends==1,"path-change-stops-prefix");});
  runCase("held-parent-changes",[&]{documentPopupReceiveHook=[](const auto&,unsigned){documentParentOverride=424241;};const auto r=attempt();check(!r.value("ok",true)&&r.at("documents").empty(),"fresh-topology-rejects-parent-change");check(dispatchSends==1,"parent-change-stops-prefix");});
  runCase("held-sibling-table-fails",[&]{documentPopupReceiveHook=[](const auto&,unsigned){documentTcp6Denied=true;};const auto r=attempt();check(!r.value("ok",true)&&r.at("documents").empty(),"complete-both-families-still-required");check(dispatchSends==1,"sibling-table-stops-prefix");});
  runCase("held-owner-revoked",[&]{documentPopupReceiveHook=[](const auto&,unsigned){dispatchOwner=false;};refused(attempt(),"document-read-cancelled");});
  runCase("held-owner-throws",[&]{const auto r=readStyles(expected,plan,[&]()->bool{throw std::runtime_error("owner-fault");});check(!r.value("ok",true)&&r.at("documents").empty(),"owner-exception-keeps-unknown");});
  runCase("held-deadline",[&]{documentPopupReceiveHook=[](const auto&,unsigned){Sleep(15);};refused(attempt(1),"document-read-cancelled");});
 }else if(bpmBlank||bpmHref||bpmAux){
  const std::string bpm="about:blank?createflags=6292754&minwidth=853&minheight=534&pid=0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad";
  const auto useBpm=[&]{auto tabs=pages();tabs[0]["url"]=bpm;documentTargets=tabs.dump();documentPopupLeaves["/devtools/page/main"].href=sp;};
  runCase("bpm-three",[&]{useBpm();const auto r=attempt();check(r.value("ok",false)&&r.value("complete",false)&&r.at("documents").size()==3,"bpm-three-original-receipts-complete");for(const auto& d:r.at("documents"))if(d.at("kind")=="SP")check(d.at("sourceUrl")==sp&&d.at("value").at("sourceHref")==sp&&d.at("value").at("sourceOrigin")=="https://steamloopback.host","bpm-exact-real-receipt-source");check(dispatchSends==6,"bpm-no-extra-evaluation-or-reader");});
  runCase("bpm-origin-external",[&]{useBpm();documentPopupLeaves["/devtools/page/main"].origin="https://evil.invalid";refused(attempt(),"document-receipt-invalid");});
  runCase("bpm-href-unequal",[&]{useBpm();documentPopupLeaves["/devtools/page/main"].href="https://evil.invalid/index.html";refused(attempt(),"document-receipt-invalid");});
  runCase("bpm-document-replaced",[&]{useBpm();documentPopupLeaves["/devtools/page/main"].finalKey="1700009999.5";refused(attempt(),"document-replaced");});
  runCase("bpm-final-origin",[&]{useBpm();documentPopupLeaves["/devtools/page/main"].finalOrigin="null";refused(attempt(),"document-replaced");});
  runCase("bpm-final-href",[&]{useBpm();documentPopupLeaves["/devtools/page/main"].finalHref="https://steamloopback.host/routes/library/home";refused(attempt(),"document-replaced");});
  runCase("bpm-final-targets",[&]{useBpm();documentTargetsChanged=true;refused(attempt(),"document-targets-changed");});
  runCase("bpm-owner-revoked",[&]{useBpm();documentPopupReceiveHook=[](const auto& path,unsigned id){if(path=="/devtools/page/main"&&id==1)dispatchOwner=false;};refused(attempt(),"document-read-cancelled");});
  runCase("bpm-process-exited",[&]{useBpm();documentPopupReceiveHook=[](const auto& path,unsigned id){if(path=="/devtools/page/main"&&id==1)processIdentityAlive=false;};refused(attempt(),"document-read-cancelled");});
  runCase("bpm-budget-expired",[&]{useBpm();documentPopupReceiveHook=[](const auto&,unsigned){Sleep(15);};refused(attempt(1),"document-read-cancelled");});
  if(bpmHref||bpmAux){
   runCase("bpm-live-route-href",[&]{useBpm();const std::string href="https://steamloopback.host/routes/library/home";documentPopupLeaves["/devtools/page/main"].href=href;const auto r=attempt();check(r.value("ok",false)&&r.value("complete",false),"live-route-source-accepted");for(const auto& d:r.at("documents"))if(d.at("kind")=="SP")check(d.at("sourceUrl")==href&&d.at("value").at("sourceHref")==href,"public-source-is-live-href");check(dispatchSends==6,"same-three-read-and-final-rounds");});
   for(const auto& href:std::vector<std::string>{bpm,"about:blank","https://steamloopback.host.evil.invalid/index.html","https://steamloopback.host/index.html.evil","https://steamloopback.host/","https://steamloopback.host/routes",std::string(4097,'x')})runCase("bpm-live-source-reject:"+href.substr(0,100),[&,href]{useBpm();documentPopupLeaves["/devtools/page/main"].href=href;refused(attempt(),"document-receipt-invalid");});
   runCase("bpm-live-menu-source-foreign-rejected",[&]{useBpm();documentPopupLeaves["/devtools/page/menu"].href="https://evil.invalid/index.html";refused(attempt(),"document-receipt-invalid");});
   runCase("bpm-aux-live:navigated-menu-and-qam",[&]{useBpm();documentPopupLeaves["/devtools/page/menu"].href=sp;documentPopupLeaves["/devtools/page/quick"].href="https://steamloopback.host/routes/apprunning";const auto r=attempt();check(r.value("ok",false)&&r.value("complete",false),"both-popup-locators-have-trusted-live-steam-sources");for(const auto& d:r.at("documents"))check(d.at("sourceUrl")==d.at("value").at("sourceHref"),"public-source-equals-observed-href");check(dispatchSends==6,"original-two-rounds-only");});
   runCase("bpm-aux-live:bare-popup-steam-href",[&]{useBpm();auto tabs=Json::parse(documentTargets);tabs[1]["url"]="about:blank";documentTargets=tabs.dump();documentPopupLeaves["/devtools/page/menu"].href=sp;const auto r=attempt();check(r.value("ok",false),"bare-existing-popup-can-have-steam-live-source");});
   runCase("bpm-aux-live:other-blank-not-source",[&]{useBpm();documentPopupLeaves["/devtools/page/menu"].href=quick;refused(attempt(),"document-receipt-invalid");});
   runCase("bpm-aux-live:foreign-source",[&]{useBpm();documentPopupLeaves["/devtools/page/quick"].href="https://steamloopback.host.evil.invalid/routes/apprunning";refused(attempt(),"document-receipt-invalid");});
   runCase("bpm-aux-live:final-href-changed",[&]{useBpm();documentPopupLeaves["/devtools/page/quick"].href="https://steamloopback.host/routes/apprunning";documentPopupLeaves["/devtools/page/quick"].finalHref=sp;refused(attempt(),"document-replaced");});
   runCase("bpm-aux-live:final-origin-null",[&]{useBpm();documentPopupLeaves["/devtools/page/quick"].href="https://steamloopback.host/routes/apprunning";documentPopupLeaves["/devtools/page/quick"].finalOrigin="null";refused(attempt(),"document-replaced");});
   runCase("bpm-live-final-source-becomes-locator",[&]{useBpm();documentPopupLeaves["/devtools/page/main"].finalHref=bpm;refused(attempt(),"document-replaced");});
  }
  if(!bpmHref){
  runCase("bpm-auxiliary-not-sp",[&]{auto tabs=pages();tabs[1]["url"]=bpm;documentTargets=tabs.dump();refused(attempt(),"document-source-unconfirmed");check(dispatchSends==0,"bpm-candidate-is-sp-only");});
  runCase("bpm-bare-sp-still-rejected",[&]{auto tabs=pages();tabs[0]["url"]="about:blank";documentTargets=tabs.dump();refused(attempt(),"document-source-unconfirmed");});
  runCase("bpm-legacy-menu-sp-still-rejected",[&]{auto tabs=pages();tabs[0]["url"]=menu;documentTargets=tabs.dump();refused(attempt(),"document-source-unconfirmed");});
  const std::vector<std::pair<std::string,std::string>> invalidBpm={{"empty-flags","about:blank?createflags=&minwidth=853&minheight=534&pid=0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad"},{"negative-width","about:blank?createflags=6292754&minwidth=-1&minheight=534&pid=0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad"},{"long-height","about:blank?createflags=6292754&minwidth=853&minheight=12345678901&pid=0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad"},{"encoded-number","about:blank?createflags=6292754&minwidth=%38&minheight=534&pid=0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad"},{"foreign-pid","about:blank?createflags=6292754&minwidth=853&minheight=534&pid=42&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad"},{"browser-zero","about:blank?createflags=6292754&minwidth=853&minheight=534&pid=0&browser=0&browserType=3&useragent=Valve%20Steam%20Gamepad"},{"wrong-type","about:blank?createflags=6292754&minwidth=853&minheight=534&pid=0&browser=-1&browserType=4&useragent=Valve%20Steam%20Gamepad"},{"wrong-agent","about:blank?createflags=6292754&minwidth=853&minheight=534&pid=0&browser=-1&browserType=3&useragent=Other"},{"fragment","about:blank?createflags=6292754&minwidth=853&minheight=534&pid=0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad#x"},{"extra","about:blank?createflags=6292754&minwidth=853&minheight=534&pid=0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad&extra=1"},{"duplicate","about:blank?createflags=6292754&minwidth=853&minheight=534&pid=0&browser=-1&browserType=3&useragent=Valve%20Steam%20Gamepad&browserType=3"}};
  for(const auto&[label,url]:invalidBpm)runCase("bpm-reject:"+label,[&,url]{auto tabs=pages();tabs[0]["url"]=url;documentTargets=tabs.dump();refused(attempt(),"document-source-unconfirmed");check(dispatchSends==0,"bpm-invalid-shape-before-evaluate");});
  } // Original candidate-shape group remains independently selectable.
 }else if(targetDiagnostics){
  const auto sourceCheck=[&](const Json& r,const char* kind,const char* form,const char* phase){
   refused(r,"document-source-unconfirmed");const auto& facts=r.at("targetSource");check(facts.size()==7&&facts.at("schema")==1,"fixed-seven-field-source-facts");
   check(facts.at("kind")==kind&&facts.at("urlClass")==form&&facts.at("phase")==phase,"exact-rejected-role-form-and-stage");
   check(facts.at("hasQuery").is_boolean()&&facts.at("hasFragment").is_boolean()&&facts.at("urlLength").is_number_unsigned(),"bounded-typed-facts");
   check(facts.dump().find("SECRET")==std::string::npos&&facts.dump().find("evil.invalid")==std::string::npos,"source-text-never-retained");
  };
  const auto rejectedCase=[&](const char* id,unsigned index,const std::string& url,const char* form){runCase(id,[&,index,url,form]{auto tabs=pages();tabs[index]["url"]=url;documentTargets=tabs.dump();const auto r=attempt();sourceCheck(r,index==0?"SP":index==1?"MainMenu":"QuickAccess",form,"discovery");check(dispatchSends==0,"source-rejected-before-evaluate");check(r.at("targetSource").at("urlLength")== (std::min<std::size_t>)(url.size(),4097),"length-clamped-to4097");});};
  rejectedCase("target-source-sp-blank",0,"about:blank","blank-exact");
  rejectedCase("target-source-menu-variant",1,menu+"#SECRET","blank-variant");
  rejectedCase("target-source-quick-other",2,"https://evil.invalid/SECRET?x#y","other");
  rejectedCase("target-source-steam-lookalike",0,"https://steamloopback.host.evil.invalid/SECRET","steam-variant");
  rejectedCase("target-source-empty",1,"","empty");
  rejectedCase("target-source-long",2,std::string(5000,'S'),"other");
  runCase("target-source-final",[&]{documentPopupReceiveHook=[&](const auto& path,unsigned id){if(path=="/devtools/page/quick"&&id==2){auto tabs=pages();tabs[2]["url"]="https://evil.invalid/SECRET";documentTargets=tabs.dump();}};const auto r=attempt();sourceCheck(r,"QuickAccess","other","final-targets");check(dispatchSends==6,"only-original-three-initial-and-three-final-evaluations");});
  runCase("target-source-success-quiet",[&]{const auto r=attempt();check(r.value("ok",false)&&r.at("documents").size()==3&&!r.contains("targetSource"),"successful-read-does-not-carry-old-failure");});
 }else runCase("popup-three",[&]{const auto r=attempt();check(r.value("ok",false)&&r.value("complete",false)&&r.at("documents").size()==3,"complete-three-typed-documents");check(r.value("sequential",false)&&!r.value("mutated",true),"sequential-read-only");std::set<std::string> kinds,hrefs,keys;
  for(const auto& d:r.at("documents")){const auto id=d.at("id").get<std::string>();const auto wanted=id=="main"?sp:id=="menu"?menu:quick;const auto kind=id=="main"?"SP":id=="menu"?"MainMenu":"QuickAccess";kinds.insert(d.at("kind").get<std::string>());hrefs.insert(d.at("sourceUrl").get<std::string>());keys.insert(d.at("value").at("documentKey").get<std::string>());check(d.at("kind")==kind&&d.at("sourceUrl")==wanted&&d.at("value").at("sourceHref")==wanted,"typed-source-and-receipt:"+id);check(d.at("value").at("sourceOrigin")=="https://steamloopback.host"&&d.at("value").at("protocol")==4,"validated-origin-and-protocol:"+id);}
  check(kinds.size()==3&&hrefs.size()==3&&keys.size()==3&&documentPopupSockets.size()==3,"independent-target-href-timeOrigin-and-socket");check(dispatchSends==6,"three-style-and-three-final-evaluations");const auto expression=Json::parse(documentExpression).at("params").at("expression").get<std::string>();check(expression.find("getComputedStyle(e)")!=std::string::npos&&expression.find("performance.timeOrigin")!=std::string::npos&&expression.find("sourceHref:location.href")!=std::string::npos,"actual-fixed-product-expression-sent");
 });
 if(!single&&!targetDiagnostics){
  runCase("bare-blank-compatible",[&]{auto tabs=pages();for(unsigned i=1;i<3;++i)tabs[i]["url"]="about:blank";documentTargets=tabs.dump();documentPopupLeaves["/devtools/page/menu"].href=documentPopupLeaves["/devtools/page/quick"].href="about:blank";const auto r=attempt();check(r.value("ok",false)&&r.value("complete",false)&&r.at("documents").size()==3,"bare-blank-both-auxiliaries");for(const auto& d:r.at("documents"))if(d.at("id")!="main")check(d.at("sourceUrl")=="about:blank"&&d.at("value").at("sourceHref")=="about:blank","bare-blank-exact-receipt");});
  const std::vector<std::pair<std::string,std::string>> invalid{
   {"external","https://evil.invalid/index.html"},{"lookalike","about:blank.evil?browserviewpopup=1&requestid=1&parentpopup=2"},{"blank-slash","about:blank/?browserviewpopup=1&requestid=1&parentpopup=2"},{"case","About:blank?browserviewpopup=1&requestid=1&parentpopup=2"},
   {"extra",menu+"&extra=1"},{"fragment",menu+"#fragment"},{"empty-fragment",menu+"#"},{"missing-parent","about:blank?browserviewpopup=1&requestid=1"},{"missing-request","about:blank?browserviewpopup=1&parentpopup=2"},{"missing-popup","about:blank?requestid=1&parentpopup=2"},
   {"duplicate-request",menu+"&requestid=3"},{"duplicate-parent",menu+"&parentpopup=3"},{"duplicate-popup","about:blank?browserviewpopup=1&browserviewpopup=1&requestid=1&parentpopup=2"},
   {"encoded-digit","about:blank?browserviewpopup=1&requestid=%31&parentpopup=2"},{"encoded-key","about:blank?browserviewpopup=1&request%69d=1&parentpopup=2"},{"encoded-separator","about:blank?browserviewpopup=1%26requestid=1&parentpopup=2"},
   {"request-21","about:blank?browserviewpopup=1&requestid=123456789012345678901&parentpopup=2"},{"parent-21","about:blank?browserviewpopup=1&requestid=1&parentpopup=123456789012345678901"},
   {"swapped","about:blank?browserviewpopup=1&parentpopup=2&requestid=1"},{"popup-last","about:blank?requestid=1&parentpopup=2&browserviewpopup=1"},{"empty-request","about:blank?browserviewpopup=1&requestid=&parentpopup=2"},{"empty-parent","about:blank?browserviewpopup=1&requestid=1&parentpopup="},
   {"plus","about:blank?browserviewpopup=1&requestid=+1&parentpopup=2"},{"minus","about:blank?browserviewpopup=1&requestid=-1&parentpopup=2"},{"space",menu+" "},{"unicode-digit","about:blank?browserviewpopup=1&requestid=１&parentpopup=2"},{"popup-value","about:blank?browserviewpopup=01&requestid=1&parentpopup=2"}};
  for(const auto&[label,url]:invalid)for(unsigned index:{1u,2u})runCase(std::string(index==1?"menu-reject:":"quick-reject:")+label,[&,index,url]{auto tabs=pages();tabs[index]["url"]=url;documentTargets=tabs.dump();const auto before=dispatchSends;refused(attempt(),"document-source-unconfirmed");check(dispatchSends==before,"source-rejected-before-any-evaluate");});
  for(const auto& url:std::vector<std::string>{"about:blank",menu})runCase(url=="about:blank"?"bigpicture-bare-reject":"bigpicture-query-reject",[&,url]{auto tabs=pages();tabs[0]["url"]=url;documentTargets=tabs.dump();refused(attempt(),"document-source-unconfirmed");});
  runCase("popup-origin-external",[&]{documentPopupLeaves["/devtools/page/menu"].origin="https://evil.invalid";refused(attempt(),"document-receipt-invalid");});
  runCase("popup-href-unequal",[&]{documentPopupLeaves["/devtools/page/menu"].href=quick;refused(attempt(),"document-receipt-invalid");});
  runCase("popup-documentKey-replaced",[&]{documentPopupLeaves["/devtools/page/menu"].finalKey="1700000100.5";refused(attempt(),"document-replaced");});
  runCase("popup-final-href-unequal",[&]{documentPopupLeaves["/devtools/page/menu"].finalHref=quick;refused(attempt(),"document-replaced");});
  runCase("popup-final-origin-external",[&]{documentPopupLeaves["/devtools/page/menu"].finalOrigin="https://evil.invalid";refused(attempt(),"document-replaced");});
  runCase("popup-final-targets-changed",[&]{documentTargetsChanged=true;refused(attempt(),"document-targets-changed");});
  runCase("popup-final-targets-refreshed",[&]{
   documentTargetsChanged=true;documentChangedTargets=pages(false).dump();dispatchSends=0;
   refused(attempt(),"document-targets-refreshed");check(dispatchSends==6,"no-old-batch-or-extra-evaluation");
  });
  runCase("popup-final-targets-refreshed-owner-revoked",[&]{
   documentTargetsChanged=true;documentChangedTargets=pages(false).dump();dispatchSends=0;
   const auto result=readStyles(expected,plan,[&]{return !documentFinalTargetsRead;});
   (*active)["receipt"]=result;check(documentFinalTargetsRead,"owner-revoked-at-final-list");
   refused(result,"document-read-cancelled");check(dispatchSends==6,"old-batch-not-reused-on-revocation");
  });
  runCase("popup-final-target-refresh-authority-boundaries",[&]{
   ymcc::steamlive::Endpoint endpoint;endpoint.host=L"127.0.0.1";endpoint.port=8080;
   const auto initial=targets(pages(),endpoint),final=targets(pages(false),endpoint);
   check(std::string_view(finalTargetSetFailure(initial,final,[]()->bool{throw std::runtime_error("owner unavailable");},GetTickCount64()+1000))=="document-read-cancelled","failed-owner-lookup-cannot-authorize-recheck");
   check(std::string_view(finalTargetSetFailure(initial,final,[]{return true;},GetTickCount64()))=="document-read-cancelled","deadline-cannot-authorize-recheck");
   check(std::string_view(finalTargetSetFailure(initial,{},[]()->bool{throw std::runtime_error("unexpected owner lookup");},GetTickCount64()+1000))=="document-targets-changed","no-extra-owner-lookup-when-main-is-gone");
  });
  runCase("popup-owner-revoked",[&]{documentPopupReceiveHook=[](const auto& path,unsigned id){if(path=="/devtools/page/menu"&&id==1)dispatchOwner=false;};refused(attempt(),"document-read-cancelled");});
  runCase("popup-process-exited",[&]{documentPopupReceiveHook=[](const auto& path,unsigned id){if(path=="/devtools/page/menu"&&id==1)processIdentityAlive=false;};refused(attempt(),"document-read-cancelled");});
  runCase("popup-budget-expired",[&]{documentPopupReceiveHook=[](const auto&,unsigned){Sleep(15);};refused(attempt(1),"document-read-cancelled");});
  runCase("auxiliary-uncreated-partial",[&]{documentTargets=pages(false).dump();const auto r=attempt();check(r.value("ok",false)&&!r.value("complete",true)&&r.at("documents").size()==1&&r.at("documents")[0].at("kind")=="SP","uncreated-auxiliary-stays-partial");check(documentPopupSockets.size()==1,"no-auxiliary-socket-created");});
 }
 raw["ok"]=!failed;raw["checkCount"]=checks;raw["sourceReady"]=false;raw["mainlineImported"]=false;
 const auto bytes=raw.dump(2);DWORD written=0;const bool saved=WriteFile(out,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(out);const bool closed=::CloseHandle(out)!=FALSE;
 std::cout<<"DOCUMENT_POPUP_SELECTED "<<Json{{"ok",!failed},{"cases",raw["cases"].size()},{"checks",checks},{"saved",saved&&closed}}.dump()<<'\n';
 return saved&&closed?(failed?1:0):3;
}
