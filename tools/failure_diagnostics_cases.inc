static int failureDiagnosticsFixture(bool oldSourceNegative,const std::string& selectedCase={}) {
 namespace sf=ymcc::steamfailure;
 Json cases=Json::array();
 auto require=[](bool value,const char* assertion){if(!value)throw std::runtime_error(assertion);};
 auto test=[&](const char* name,const auto& body){if(!selectedCase.empty()&&selectedCase!=name)return;try{body();cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"assertion",e.what()}});}};
 processIdentityProbe=dispatchProbe=failureDiagnosticsProbe=true;
 processIdentityOpens=processIdentityCloses=processIdentityUnexpected=0;
 dispatchSends=dispatchHttpOpen=dispatchHttpClose=0;
 failureImageOpens=failureImageCloses=failureSnapshotOpens=listenerFixtureCloses=0;
 failureExternalCalls=0;
 const ymcc::steamsession::Session identity{L"P:\\YMCC-identity-fixture",processIdentityPid,42,processIdentityBirth};
 auto reset=[&]{processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;
   dispatchRead=0;dispatchSendFails=dispatchExitOnUpgrade=false;dispatchReplyHook={};
   failureCloseQueryError=ERROR_INVALID_OPERATION;failureCloseStatus=1001;failureCloseQueryCalls=0;failureCloseReason.clear();
   failurePacket.clear();failurePacketOffset=0;failureReceiveError=0;failureHttpStatus=0;
   failureHttpQueryDenied=false;failureHttpError=ERROR_WINHTTP_CANNOT_CONNECT;
   failureHttpSendAt=failureHttpReceiveAt=0;failureSocketClose=false;failureFinalAdmissions=0;
   dispatchReplyValue=R"({"ok":true,"mutated":false})";ymcc::processInventory::invalidate();};
 auto handles=[&]{require(processIdentityOpens==processIdentityCloses,"process-handles-zero");
   require(dispatchHttpOpen==dispatchHttpClose,"http-socket-handles-zero");
   require(failureImageOpens==failureImageCloses,"image-handles-zero");
   require(failureSnapshotOpens==listenerFixtureCloses,"snapshot-handles-zero");
   require(processIdentityUnexpected==0&&failureExternalCalls==0,"no-real-os-or-network-fallback");};
 auto invoke=[&](bool read=false){const auto before=dispatchSends;
   auto result=run(identity.root,{{"operation",read?"ui.mode.get":"settings.set"}},[&]{++failureFinalAdmissions;return true;},read?4000:0,[]{return true;},&identity);
   handles();require(dispatchSends-before<=1,"no-replay");return result;};
 auto check=[&](const Json& result,const char* reason,bool mutated,bool sent,const char* domain,const char* stage,std::int64_t code){
   require(!result.value("ok",true)&&result.value("reason",std::string{})==reason,"original-reason-retained");
   require(result.value("mutated",!mutated)==mutated&&result.value("requestDispatched",!sent)==sent,"original-mutation-dispatch-retained");
   require(!result.contains("uncertain"),"no-new-business-uncertain-field");
   require(result.contains("steamFailure"),"failure-diagnostics-retained-by-actual-run");
   const auto& f=result["steamFailure"];
   require(f.value("domain",std::string{})==domain&&f.value("sourceStage",std::string{})==stage,"diagnostic-domain-stage");
   require(f.value("codeKnown",false)&&f["code"]==code,"diagnostic-numeric-code");};
 reset();
 if(oldSourceNegative){
   test("old-source-drops-cdp-failure",[&]{failurePacket=R"({"id":1,"error":{"code":-32000,"message":"synthetic is not a function"}})";
     const auto result=invoke();require(result.value("reason",std::string{})=="live-transport-uncertain"&&result.value("mutated",false)&&result.value("requestDispatched",false),"old-business-shape-control");
     require(result.contains("steamFailure"),"failure-diagnostics-retained-by-actual-run");});
 }else{
   test("cdp-protocol-write",[&]{reset();failurePacket=R"({"id":1,"error":{"code":-32000,"message":"synthetic is not a function"}})";
     auto r=invoke();check(r,"live-transport-uncertain",true,true,"cdp-protocol","evaluate",-32000);
     require(r["steamFailure"]["signatures"]==Json::array({"not-a-function"}),"protocol-signature");require(failureFinalAdmissions==1,"single-final-admit");});
   test("cdp-protocol-bounded-read",[&]{reset();failurePacket=R"({"id":1,"error":{"code":-32000,"message":"timed out"}})";
     auto r=invoke(true);check(r,"live-api-error",false,true,"cdp-protocol","evaluate",-32000);});
   test("cdp-javascript-exception",[&]{reset();failurePacket=R"({"id":1,"result":{"exceptionDetails":{"text":"Uncaught cannot read properties of undefined","lineNumber":0,"columnNumber":7,"exception":{"className":"TypeError","description":"TypeError: synthetic is not a function"}}}})";
     const auto r=invoke();require(r.value("reason",std::string{})=="live-transport-uncertain"&&r.value("mutated",false)&&r.value("requestDispatched",false),"exception-original-business-shape");
     const auto& f=r.at("steamFailure");require(f["domain"]=="javascript"&&f["exceptionClass"]=="TypeError"&&f["line"]==0&&f["column"]==7&&!f.value("codeKnown",true),"exception-bounded-shape");
     require(f["signatures"]==Json::array({"undefined-property","not-a-function"}),"exception-message-signatures-only");});
   test("script-caught-receipt-normalized",[&]{reset();dispatchReplyValue=R"({"ok":false,"reason":"live-api-error","stage":"preflight","mutated":false,"steamFailure":{"schema":1,"domain":"javascript","sourceStage":"evaluate","codeKnown":false,"exceptionClassKnown":true,"exceptionClass":"TypeError","message":"SYNTHETIC_SECRET","stack":"SYNTHETIC_SECRET","account":"SYNTHETIC_SECRET"}})";
     auto r=invoke();require(r["reason"]=="live-api-error"&&!r.value("mutated",true)&&!r.contains("requestDispatched"),"remote-value-business-shape-preserved");
     sf::FailureContext context;auto projected=sf::project(r,context);
     require(projected["steamFailure"]["exceptionClass"]=="TypeError"&&projected.dump().find("SYNTHETIC_SECRET")==std::string::npos,"remote-value-normalize-removes-injected-extras");});
   test("winhttp-websocket-send",[&]{reset();dispatchSendFails=true;auto r=invoke();check(r,"live-transport-uncertain",true,true,"winhttp","send",ERROR_CONNECTION_ABORTED);});
   test("winhttp-websocket-receive",[&]{reset();failureReceiveError=ERROR_WINHTTP_TIMEOUT;auto r=invoke();check(r,"live-transport-uncertain",true,true,"winhttp","receive",ERROR_WINHTTP_TIMEOUT);
     const auto policy=ymcc::steamrequest::classify("input.gamepadLease");
     require(policy.known&&policy.lightGamepadLease&&policy.shortRead&&!policy.setter&&!policy.runningLinkRead,"light-policy-is-known-bounded-and-distinct");
     const Json request={{"operation","input.gamepadLease"},{"account",identity.account},{"owner",std::string(32,'a')},{"serial",1},{"enabled",true},{"expiresAt",1},{"lightSessionProcess",{{"pid",identity.pid}}}};
     auto light=[&](const Json& value,const std::function<bool()>& admit,const std::function<bool()>& current){
       const auto before=dispatchSends;auto result=run(identity.root,value,admit,500,current,&identity);
       handles();require(dispatchSends-before<=1,"light-never-replays");return result;
     };
     reset();const auto sendsBefore=dispatchSends;unsigned finalCalls=0;
     r=light(request,[&]{++finalCalls;return true;},[]{return false;});
     require(r.value("reason",std::string{})=="live-action-cancelled"&&finalCalls==0&&dispatchSends==sendsBefore,"revoked-light-owner-before-discovery");
     reset();r=light(request,[&]{++finalCalls;return false;},[]{return true;});
     require(r.value("reason",std::string{})=="live-action-cancelled"&&finalCalls==1&&dispatchSends==sendsBefore,"light-final-admission-is-mandatory");
     reset();bool current=true;r=light(request,[&]{current=false;return true;},[&]{return current;});
     require(r.value("reason",std::string{})=="live-action-cancelled"&&dispatchSends==sendsBefore,"light-rechecks-owner-after-final-admission");
     reset();auto mismatch=request;mismatch["lightSessionProcess"]["pid"]=identity.pid+1;
     r=light(mismatch,[]{return true;},[]{return true;});
     require(r.value("reason",std::string{})=="steam-session-changed"&&dispatchSends==sendsBefore,"light-cannot-use-another-steam-process");
     for(const bool enabled:{true,false}){
       reset();auto value=request;value["enabled"]=enabled;value["serial"]=enabled?2:3;
       std::lock_guard unrelated(transportMutex);r=light(value,[]{return true;},[]{return true;});
       require(r.value("ok",false),"light-renew-and-release-do-not-queue-behind-unrelated-owner");
       const auto packet=Json::parse(dispatchLastMessage);
       require(packet["params"]["expression"].get<std::string>()==std::string(kLightGamepadScript)+"("+value.dump()+")","light-uses-actual-small-generated-script-with-original-owner-and-serial");
     }
     reset();failureReceiveError=ERROR_WINHTTP_TIMEOUT;r=light(request,[]{return true;},[]{return true;});
     check(r,"live-transport-uncertain",true,true,"winhttp","receive",ERROR_WINHTTP_TIMEOUT);
     reset();current=true;dispatchReplyHook=[&]{current=false;};r=light(request,[]{return true;},[&]{return current;});
     require(!r.value("ok",true)&&r.value("mutated",false)&&r.value("requestDispatched",false)&&r.value("reason",std::string{})=="live-transport-uncertain","late-light-reply-cannot-acknowledge-revoked-owner");
   });
   test("websocket-close-without-code",[&]{reset();failureSocketClose=true;auto r=invoke();require(r["reason"]=="live-transport-uncertain"&&r.value("mutated",false)&&r.value("requestDispatched",false),"close-original-business-shape");
     require(r["steamFailure"]["domain"]=="websocket-close"&&!r["steamFailure"].value("codeKnown",true)&&!r["steamFailure"].contains("code"),"close-does-not-invent-zero");
     reset();failureSocketClose=true;failureCloseQueryError=NO_ERROR;failureCloseStatus=1001;
     failureCloseReason="SYNTHETIC_CLOSE_PRIVATE_REASON_TOKEN";r=invoke();
     require(r["steamFailure"].value("codeKnown",false)&&r["steamFailure"].value("code",0)==1001,"actual-close-code");
     check(r,"live-transport-uncertain",true,true,"websocket-close","receive",1001);
     require(failureCloseQueryCalls==1,"successful-close-single-query");
     require(r.dump().find(failureCloseReason)==std::string::npos&&r["steamFailure"].value("textInspectedBytes",0)==0&&r["steamFailure"]["signatures"].empty(),"close-reason-never-retained-or-interpreted");
     require(failureFinalAdmissions==1,"close-single-final-admit");
     reset();failureSocketClose=true;failureCloseQueryError=ERROR_ACCESS_DENIED;failureCloseStatus=1001;r=invoke();
     require(r["reason"]=="live-transport-uncertain"&&r.value("mutated",false)&&r.value("requestDispatched",false)&&failureFinalAdmissions==1,"query-error-original-business-shape");
     require(failureCloseQueryCalls==1&&r["steamFailure"]["domain"]=="websocket-close"&&!r["steamFailure"].value("codeKnown",true)&&!r["steamFailure"].contains("code"),"query-error-keeps-unknown-not-zero-or-winhttp-code");
     reset();failureSocketClose=true;failureReceiveError=ERROR_WINHTTP_TIMEOUT;failureCloseQueryError=NO_ERROR;r=invoke();
     check(r,"live-transport-uncertain",true,true,"winhttp","receive",ERROR_WINHTTP_TIMEOUT);
     require(failureCloseQueryCalls==0&&failureFinalAdmissions==1,"receive-error-never-queries-close-status");});
   test("http-upgrade-status",[&]{reset();failureHttpStatus=503;auto r=invoke();check(r,"live-transport-failed",false,false,"http","upgrade",503);require(failureFinalAdmissions==0,"status-before-final-admit");});
   test("http-query-zero-last-error",[&]{reset();failureHttpQueryDenied=true;failureHttpError=0;auto r=invoke();check(r,"live-transport-failed",false,false,"winhttp","upgrade",0);});
   test("http-upgrade-send",[&]{reset();failureHttpSendAt=4;auto r=invoke();check(r,"live-transport-failed",false,false,"winhttp","upgrade",ERROR_WINHTTP_CANNOT_CONNECT);});
   test("http-upgrade-receive",[&]{reset();failureHttpReceiveAt=4;auto r=invoke();check(r,"live-transport-failed",false,false,"winhttp","upgrade",ERROR_WINHTTP_CANNOT_CONNECT);});
   test("http-discovery-send",[&]{reset();failureHttpSendAt=3;auto r=invoke();check(r,"live-context-unavailable",false,false,"winhttp","discovery",ERROR_WINHTTP_CANNOT_CONNECT);});
   test("http-discovery-receive",[&]{reset();failureHttpReceiveAt=3;auto r=invoke();check(r,"live-context-unavailable",false,false,"winhttp","discovery",ERROR_WINHTTP_CANNOT_CONNECT);});
   test("large-packet-three-prefix-bounds",[&]{reset();const std::string secret="SYNTHETIC_PRIVATE_TOKEN_URL_ACCOUNT_STACK";
     std::string huge="is not a function "+secret+std::string(80000,'X');
     Json packet={{"id",1},{"error",{{"code",-32000},{"message",huge},{"data",{{"url",secret},{"token",secret}}}}},
       {"result",{{"exceptionDetails",{{"text",huge},{"exception",{{"className","TypeError"},{"description",huge},{"stackTrace",secret},{"account",secret}}}}}}}};
     failurePacket=packet.dump();auto r=invoke();auto p=sf::project(r,{});auto dumped=p.dump();
     require(p["steamFailure"]["textInspectedBytes"]==3072&&p["steamFailure"]["textTruncated"]==true,"three-times-1024-byte-ceiling");
     require(dumped.size()<sf::recordLimit&&dumped.find(secret)==std::string::npos,"large-packet-no-private-body");
     require(p["steamFailure"]["signatures"]==Json::array({"not-a-function"}),"three-field-signatures-deduplicated");});
   test("signature-after-prefix-not-scanned",[&]{const auto f=sf::cdp({{"error",{{"message",std::string(1024,'x')+"is not a function"}}}});
     require(f["signatures"].empty()&&f["textInspectedBytes"]==1024&&f["textTruncated"]==true,"no-scan-after-1024");});
   test("missing-code-versus-zero",[&]{auto missing=sf::cdp({{"error",Json::object()}}),zero=sf::cdp({{"error",{{"code",0}}}});
     require(!missing.value("codeKnown",true)&&!missing.contains("code")&&zero.value("codeKnown",false)&&zero["code"]==0,"missing-not-explicit-zero");});
   test("numeric-types-and-cdp-boundaries",[&]{for(const auto& bad:std::vector<Json>{"-32000",true,nullptr,1.5,std::numeric_limits<double>::quiet_NaN(),std::int64_t(INT32_MIN)-1,std::uint64_t(INT32_MAX)+1,UINT64_MAX}){
       auto f=sf::cdp({{"error",{{"code",bad}}}});require(!f.value("codeKnown",true)&&!f.contains("code"),"invalid-cdp-code-unknown");}
     for(const auto valid:{INT32_MIN,INT32_MAX,0})require(sf::cdp({{"error",{{"code",valid}}}})["code"]==valid,"cdp-int32-edge");});
   test("native-code-negative-or-overflow-unknown",[&]{for(const auto& bad:std::vector<Json>{-1,std::uint64_t(UINT32_MAX)+1,"0",false,std::numeric_limits<double>::quiet_NaN()}){
       auto f=sf::normalize({{"schema",1},{"domain","winhttp"},{"codeKnown",true},{"code",bad}});require(!f.value("codeKnown",true),"native-code-strict-uint32");}
     require(sf::normalize(sf::native(sf::Domain::WinHttp,UINT32_MAX,sf::SourceStage::Send))["code"]==UINT32_MAX,"native-uint32-edge");});
   test("malicious-unknown-types-no-text-output",[&]{const std::string secret="SYNTHETIC_PRIVATE_TOKEN_URL_ACCOUNT_STACK";
     Json receipt={{"ok","false"},{"mutated",0},{"uncertain",Json::array()},{"requestDispatched",nullptr},{"account",secret},{"token",secret},{"reason",secret},{"stack",secret},
       {"steamFailure",{{"schema",1},{"domain",secret},{"sourceStage",secret},{"exceptionClassKnown",true},{"exceptionClass",secret},{"codeKnown",true},{"code",secret},{"url",secret},{"stack",secret},{"signatures",Json::array({secret,"not-a-function","not-a-function",7,nullptr,"timeout","access-denied"})}}}};
     auto p=sf::project(receipt,{});require(!p.contains("ok")&&!p.contains("mutated")&&!p.contains("uncertain")&&!p.contains("requestDispatched"),"business-bool-types-not-coerced");
     require(p["steamFailure"]["domain"]=="unknown"&&!p["steamFailure"].value("codeKnown",true)&&!p["steamFailure"].value("exceptionClassKnown",true),"unknown-tag-not-forwarded");
     require(p["steamFailure"]["signatures"]==Json::array({"not-a-function","timeout"})&&p.dump().find(secret)==std::string::npos,"first-six-input-signatures-only-and-dedup");});
   test("signature-output-cap",[&]{auto f=sf::cdp({{"error",{{"message","is not a function cannot read properties of undefined cannot read properties of null is not defined execution context was destroyed cannot find context target closed connection closed timeout access denied"}}}});
     require(f["signatures"].size()==sf::signatureLimit,"maximum-six-signatures");});
   test("line-column-types-and-range",[&]{for(const auto& bad:std::vector<Json>{-1,std::uint64_t(INT32_MAX)+1,1.5,"0",true,std::numeric_limits<double>::quiet_NaN()}){
     auto f=sf::cdp({{"result",{{"exceptionDetails",{{"lineNumber",bad},{"columnNumber",bad}}}}}});require(!f.value("lineKnown",true)&&!f.value("columnKnown",true),"position-invalid-unknown");}});
   test("project-fixed-record-max-context",[&]{sf::FailureContext c;c.actionId=c.recoveryEpisode=c.generation=c.powerGeneration=c.steamBirth=UINT64_MAX;c.steamPid=UINT32_MAX;c.phase=sf::Phase::Finished;c.operation=sf::Operation::RebuildDocument;
     auto r=sf::project({{"ok",false},{"mutated",true},{"uncertain",true},{"requestDispatched",true},{"steamFailure",sf::native(sf::Domain::WinHttp,UINT32_MAX,sf::SourceStage::Receive)}},c);
     require(r.dump().size()<2048&&r["actionId"]==std::to_string(UINT64_MAX)&&r["uncertain"]==true,"bounded-full-record-and-original-bools");});
   test("nonobject-and-unknown-schema",[&]{for(const auto& input:std::vector<Json>{nullptr,42,"private",Json::array(),Json{{"schema",2},{"domain","javascript"},{"codeKnown",true},{"code",0}}}){
     auto f=sf::normalize(input);require(!f.value("codeKnown",true)&&f.dump().size()<2048,"nonobject-or-schema-unknown");}});
   test("paired-script-embedded-full-mode-light",[&]{for(const auto* script:{kScript,kUiModeScript,kLightGamepadScript}){
     require(std::string_view(script).find("const name = error?.name;")!=std::string_view::npos&&std::string_view(script).find("...(steamFailure ? { steamFailure } : {})")!=std::string_view::npos,"paired-generated-catch-present");}});
 }
 try{handles();}catch(const std::exception& e){cases.push_back({{"name","final-resource-accounting"},{"passed",false},{"assertion",e.what()}});}
 unsigned failed=0;for(const auto& item:cases)if(!item.value("passed",false))++failed;
 const Json result={{"selection",oldSourceNegative?"failure-diagnostics-old-source-negative-only":"failure-diagnostics-only"},
   {"caseCount",cases.size()},{"failedCases",failed},{"cases",cases},{"actualRun",true},{"realSteam",false},{"networkFallbackCalls",failureExternalCalls},
   {"processHandlesRemaining",processIdentityOpens-processIdentityCloses},{"httpHandlesRemaining",dispatchHttpOpen-dispatchHttpClose},
   {"imageHandlesRemaining",failureImageOpens-failureImageCloses},{"snapshotHandlesRemaining",failureSnapshotOpens-listenerFixtureCloses},
   {"sendAttempts",dispatchSends},{"newCanonicalIds",0},{"defaultGroupExecuted",false}};
 if(cases.empty())return 2;
 std::cout<<result.dump()<<"\n";
 dispatchProbe=processIdentityProbe=failureDiagnosticsProbe=false;
 return failed?1:0;
}
