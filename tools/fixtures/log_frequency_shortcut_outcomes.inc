#include "shortcut_action_protocol.h"
static void offerShortcutRequest(const json& request,const std::string& action) {
    if(request.empty()) { @@REQUEST_REFUSED@@ return; }
    @@REQUEST_ISSUED@@
}
static void offerShortcutResult(const json& feedback) { @@RESULT@@ }
static json completedShortcut(const char* state) {
    ymcc::ShortcutActionMailbox mailbox("logging-owned");
    const auto request=mailbox.issue("monitor.toggle",json::object(),1,7,1000,1000000,[]{return true;});
    if(request.empty()||mailbox.claim(request["id"].get<std::string>(),1001).empty())throw std::runtime_error("actual claim failed");
    const auto feedback=mailbox.complete(request["id"].get<std::string>(),{{"state",state},{"title","fixture title"},{"message","fixture message"}},1002,1000002);
    if(feedback.empty()||mailbox.size())throw std::runtime_error("actual complete failed");return feedback;
}
static unsigned shortcutDetailRows(){g_nativeDetailLog.flush();std::ifstream in(fspath::path(g_testRoot)/L"native-detail.log");unsigned rows=0;std::string line;while(std::getline(in,line))if(!line.empty())++rows;return rows;}
static bool payloadExact(const json& row,const json& payload){for(auto it=payload.begin();it!=payload.end();++it)if(it.key()=="powerGeneration"){if(!row.contains("producerPowerGeneration")||row["producerPowerGeneration"]!=it.value())return false;}else if(!row.contains(it.key())||row[it.key()]!=it.value())return false;return true;}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;
    (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    const auto failed=completedShortcut("failed"),pending=completedShortcut("pending"),applied=completedShortcut("applied"),saved=completedShortcut("saved");
    ymcc::ShortcutActionMailbox full("capacity-owned");json issued;
    for(unsigned i=0;i<16;++i)issued=full.issue("monitor.toggle",json::object(),1,7,1000,1000000,[]{return true;});
    const auto refused=full.issue("monitor.toggle",json::object(),1,7,1000,1000000,[]{return true;});
    const json notIssued={{"actionId","monitor.toggle"},{"status","not-issued"}};
    const json issuedPayload={{"id",issued["id"]},{"actionId","monitor.toggle"},{"status","issued"}};
#ifndef YMCC_SHORTCUT_OUTCOME_CONTINUATION_ONLY
    check(nativeLifecycleEventIsBase("shortcut-action-request")&&nativeLifecycleEventIsBase("shortcut-action-result"),"two-existing-shortcut-outcome-names-registered");
#endif
    offerShortcutRequest(refused,"monitor.toggle");offerShortcutResult(failed);offerShortcutResult(pending);auto rows=readRecoveryRows(root);
    check(rows.size()==3&&payloadExact(rows[0],notIssued)&&payloadExact(rows[1],failed)&&payloadExact(rows[2],pending)&&!g_nativeDetailLog.is_open(),"actual-capacity-refusal-failed-and-pending-outcomes-retained-detail-off");
#ifndef YMCC_SHORTCUT_OUTCOME_OLD_ONLY
#ifndef YMCC_SHORTCUT_OUTCOME_CONTINUATION_ONLY
    check(full.size()==16&&refused.empty()&&!issued.empty(),"actual-sixteen-entry-mailbox-refusal-without-product-actions");
#endif
    offerShortcutRequest(issued,"monitor.toggle");offerShortcutResult(applied);offerShortcutResult(saved);
    check(readRecoveryRows(root).size()==3&&!g_nativeDetailLog.is_open(),"healthy-issued-applied-saved-still-silent-detail-off");
    const auto writes=g_nativeLifecycleFrameWrites;
    ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i){appendNativeLifecycleLog("shortcut-action-request",issuedPayload);appendNativeLifecycleLog("shortcut-action-result",applied);appendNativeLifecycleLog("shortcut-action-result",saved);}const auto armed=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(armed==0&&writes==g_nativeLifecycleFrameWrites,"150000-prebuilt-healthy-offers-detail-off-zero-allocation");
    g_domainLogEnabled[1]=true;offerShortcutRequest(issued,"monitor.toggle");offerShortcutResult(applied);offerShortcutResult(saved);g_domainLogEnabled[1]=false;
    check(shortcutDetailRows()==3&&readRecoveryRows(root).size()==3,"healthy-original-payloads-enter-detail-when-enabled");
    g_domainLogEnabled[1]=true;offerShortcutResult(failed);g_domainLogEnabled[1]=false;
    check(shortcutDetailRows()==3&&readRecoveryRows(root).size()==4,"failed-outcome-remains-resident-with-detail-on");
    for(unsigned i=0;i<3;++i){offerShortcutRequest(refused,"monitor.toggle");offerShortcutResult(failed);offerShortcutResult(pending);}rows=readRecoveryRows(root);bool repeated=rows.size()==13;
    if(repeated)for(unsigned i=4;i<13;++i)repeated=repeated&&payloadExact(rows[i],(i-4)%3==0?notIssued:(i-4)%3==1?failed:pending);
    check(repeated,"every-identical-independent-failure-and-pending-receipt-kept");
    std::vector<std::pair<const char*,json>> unknown;
    unknown.push_back({"shortcut-action-request",{{"actionId","monitor.toggle"},{"id","a"},{"status","future"}}});
    unknown.push_back({"shortcut-action-request",{{"actionId","monitor.toggle"},{"id","a"},{"status",true}}});
    unknown.push_back({"shortcut-action-request",{{"actionId","monitor.toggle"},{"status","issued"}}});
    unknown.push_back({"shortcut-action-request",{{"actionId",false},{"id","a"},{"status","issued"}}});
    for(unsigned i=0;i<6;++i){auto value=applied;if(i==0)value["state"]="future";if(i==1)value["state"]=true;if(i==2)value.erase("origin");if(i==3)value["schemaVersion"]=true;if(i==4)value["id"]="";if(i==5)value["origin"]="future";unknown.push_back({"shortcut-action-result",std::move(value)});}
    const auto priorUnknown=readRecoveryRows(root).size();for(const auto& item:unknown)appendNativeLifecycleLog(item.first,item.second);rows=readRecoveryRows(root);bool unknownKept=rows.size()==priorUnknown+unknown.size();
    if(unknownKept)for(size_t i=0;i<unknown.size();++i)unknownKept=unknownKept&&payloadExact(rows[priorUnknown+i],unknown[i].second);
    check(unknownKept,"ten-unknown-malformed-or-incomplete-outcomes-stay-resident");
    const auto priorGeneration=rows.size();++g_powerGeneration;offerShortcutResult(failed);offerShortcutResult(pending);offerShortcutResult(applied);offerShortcutRequest(issued,"monitor.toggle");
    check(readRecoveryRows(root).size()==priorGeneration+2,"new-generation-retains-failures-without-promoting-healthy-presses");
#ifndef YMCC_SHORTCUT_OUTCOME_CONTINUATION_ONLY
    const auto priorSuffix=readRecoveryRows(root).size();appendNativeLifecycleLog("shortcut-action-result-future",failed);appendNativeLifecycleLog("shortcut-action-request-future",notIssued);
    check(readRecoveryRows(root).size()==priorSuffix,"unknown-event-suffixes-do-not-gain-resident-permission");
#endif
    const auto failureCount=count(Failure::eventWrite),priorFile=readRecoveryRows(root).size();
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);offerShortcutResult(failed);}offerShortcutResult(failed);rows=readRecoveryRows(root);
    check(count(Failure::eventWrite)==failureCount+1&&rows.size()==priorFile+1&&payloadExact(rows.back(),failed),"actual-file-failure-account-and-next-original-failed-offer-retained");
#endif
    closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all-owned-log-handles-returned-before-exit");
    unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"hardwareActions",0},{"productOrSteamActions",0},{"originalProducerStatements",3},{"actualMailbox",true}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
