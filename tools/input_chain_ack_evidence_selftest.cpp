#include "../native/input_chain_ack_evidence.h"
#include <iostream>
#include <vector>
#include <functional>
#include <type_traits>
using namespace ymcc::inputChainAck;
static_assert(std::is_trivially_copyable<View>::value,"Per-frame view copies pointers/scalars only");
static_assert(noexcept(project(View{})),"Diagnostic projection must not throw into sender");
int main() {
    Json cases=Json::array();bool passed=true;
    auto check=[&](const char* name,bool condition){cases.push_back({{"name",name},{"passed",condition}});passed=passed&&condition;};
    std::string run="bound-run",host="bound-host",request="frame-request",parsedHost=host;
    View v;v.nativeAccepted=true;v.requestBindingCaptured=true;v.fastReceiptUsed=true;v.protocolPowerGeneration=1;v.hostEpoch=5;v.requestedSequence=7;v.runId=&run;v.boundHostInstanceId=&host;v.preparedRequestId=&request;v.parsedFastHostInstanceId=&parsedHost;
    auto good=project(v);check("fast-validated-request-and-parsed-host",good.value("tupleVerified",false)&&good["protocolPowerGeneration"]==1&&good["hostEpoch"]==5&&good["wireSequence"]==7&&good["identityOrigin"]=="strict-fast-validator-equality-and-parsed-host");
    check("same-local-ACK-never-means-Steam-or-game-consumption",good["gameInputUnverified"]==true&&good["steamConsumptionProven"]==false&&!good.contains("gameInputVerified"));
    check("protocol-bound-one-is-not-current-power-three",good["protocolPowerGeneration"]==1&&good["protocolGenerationScope"]=="input-host-bound-protocol;not-current-shared-power"&&!good.contains("powerGeneration"));
    auto unknown=[&](const char* name,const View& view){auto j=project(view);check(name,j.is_null()||!j.value("tupleVerified",false));};
    auto bad=v;bad.nativeAccepted=false;unknown("ACK-object-cannot-promote-native-rejection",bad);check("native-rejection-preserved",project(bad)["nativeAccepted"]==false);
    bad=v;bad.requestBindingCaptured=false;unknown("missing-before-request-binding",bad);
    bad=v;bad.protocolPowerGeneration=0;unknown("current-power-cannot-fill-missing-protocol-binding",bad);
    bad=v;bad.hostEpoch=0;unknown("missing-host-epoch",bad);
    bad=v;bad.requestedSequence=0;unknown("missing-wire-sequence",bad);
    bad=v;bad.runId=nullptr;unknown("missing-run-id-pointer",bad);
    bad=v;bad.boundHostInstanceId=nullptr;unknown("missing-bound-host-pointer",bad);
    bad=v;bad.parsedFastHostInstanceId=nullptr;unknown("missing-parsed-fast-host",bad);
    bad=v;bad.preparedRequestId=nullptr;unknown("missing-prepared-request-id",bad);
    std::string foreign="foreign-host";bad=v;bad.parsedFastHostInstanceId=&foreign;unknown("foreign-fast-ACK-host-cannot-borrow-current-host",bad);
    std::string empty;bad=v;bad.runId=&empty;unknown("empty-run-id",bad);
    bad=v;bad.boundHostInstanceId=&empty;unknown("empty-bound-host-id",bad);
    Json reply={{"protocolVersion",1},{"command","SUBMIT_FRAME"},{"requestId",request},{"sequence",7},{"runId",run},{"epoch",5},{"powerGeneration",1},{"hostInstanceId",host},{"ok",true},{"status","frame-accepted"}};
    View dom=v;dom.fastReceiptUsed=false;dom.parsedDomReceipt=&reply;auto parsed=project(dom);check("DOM-accepted-projection-uses-parsed-reply",parsed.value("tupleVerified",false)&&parsed["identityOrigin"]=="existing-validator-and-parsed-dom-reply");
    auto legacy=dom;legacy.preparedRequestId=nullptr;check("legacy-DOM-request-id-already-validated-by-native-path",project(legacy).value("tupleVerified",false)&&project(legacy)["requestId"]==request);
    for(const auto* key:{"protocolVersion","command","requestId","sequence","runId","epoch","powerGeneration","hostInstanceId","ok","status"}) {
        auto x=reply;x.erase(key);auto view=dom;view.parsedDomReceipt=&x;auto j=project(view);const std::string name=std::string("DOM-missing-")+key;check(name.c_str(),!j.value("tupleVerified",false));
    }
    const std::vector<std::pair<const char*,Json>> wrong={{"protocolVersion",2},{"command","OTHER"},{"requestId","foreign-request"},{"sequence",8},{"runId","foreign-run"},{"epoch",6},{"powerGeneration",2},{"hostInstanceId","foreign-host"},{"ok",false},{"status","frame-rejected"}};
    for(const auto& mutation:wrong){auto x=reply;x[mutation.first]=mutation.second;auto view=dom;view.parsedDomReceipt=&x;auto j=project(view);const std::string name=std::string("DOM-wrong-")+mutation.first;check(name.c_str(),!j.value("tupleVerified",false)&&j["nativeAccepted"]==true);}
    for(const auto* key:{"protocolVersion","sequence","epoch","powerGeneration"})for(const auto& value:std::vector<Json>{Json(-1),Json(1.0),Json("1"),Json(true),Json(nullptr)}){auto x=reply;x[key]=value;auto view=dom;view.parsedDomReceipt=&x;auto j=project(view);check("DOM-numeric-types-negative-and-aliases-stay-unknown",!j.value("tupleVerified",false));}
    for(const auto& value:std::vector<Json>{Json(1),Json("true"),Json(nullptr)}){auto x=reply;x["ok"]=value;auto view=dom;view.parsedDomReceipt=&x;unknown("DOM-ok-requires-literal-boolean",view);}
    bad=dom;bad.parsedDomReceipt=nullptr;unknown("DOM-object-missing",bad);Json array=Json::array();bad=dom;bad.parsedDomReceipt=&array;unknown("DOM-array-is-not-ACK-object",bad);
    auto spoof=reply;spoof["gameInputVerified"]=true;spoof["steamConsumptionProven"]=true;dom.parsedDomReceipt=&spoof;auto unpromoted=project(dom);check("foreign-ACK-gameplay-flags-never-promoted",unpromoted["gameInputUnverified"]==true&&unpromoted["steamConsumptionProven"]==false&&!unpromoted.contains("gameInputVerified"));
    std::cout<<Json{{"suite","actual-ACK-projection-production-header"},{"passed",passed},{"cases",cases},{"caseCount",cases.size()},{"actualInputVerified",false},{"deviceOperations",0}}.dump(2)<<std::endl;return passed?0:1;
}
