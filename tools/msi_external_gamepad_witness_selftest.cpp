#include "../native/msi_external_raw_receipts.h"
#include <iostream>
#include <string>
using namespace ymcc::msi::external;
static unsigned cases=0,failures=0,decodes=0;
static void check(bool value,const char* label){++cases;if(!value){++failures;std::cout<<"FAIL "<<label<<"\n";}}
static RawIdentity mainIdentity(){return {true,true,0x0db0,0x1902,1,5,R"(\\?\hid#vid_0db0&pid_1902&mi_00&col01#fixture)"};}
static json decode(const unsigned char* data,unsigned size){++decodes;return {{"state","decoded-fixture"},{"decodedControlsKnown",true},{"decodeComplete",true},{"buttonsDown",json::array({data[size-1]})},{"values",json::array()}};}
static const json& collection(const json& window){return window["collections"][0];}
static const json& witness(const json& window,unsigned n=0){return collection(window)["semanticWitnesses"][n];}
int main(){
    const unsigned char bytes[]={0,7,0,9};const auto identity=mainIdentity();
    check(mainGamepad(identity),"exact MSI DInput main collection is qualified");
    GamepadWitnessBudget budget;check(budget.select(0)==GamepadWitnessBudget::Decision::selected,"uptime zero can select once");
    check(budget.select(0)==GamepadWitnessBudget::Decision::interval,"duplicate timestamp is throttled");
    check(budget.select(499)==GamepadWitnessBudget::Decision::interval,"global budget throttles sub-500ms work");
    check(budget.select(500)==GamepadWitnessBudget::Decision::selected,"next real 500ms candidate is eligible");
    check(budget.select(400)==GamepadWitnessBudget::Decision::clockRollback,"rollback is not a fresh budget");
    RawReceiptCounters counter;counter.begin(100);decodes=0;
    check(counter.observeWithDecoder(1,identity,bytes,2,2,4,101,{true,91,10},decode),"actual counter accepts batched main receipt");
    auto row=counter.flush(110);
    check(decodes==1&&collection(row)["reportReceipts"]==2,"one selected decode does not duplicate original report counts");
    check(witness(row)["controls"]["buttonsDown"][0]==9&&witness(row)["batchReportIndex"]==1&&witness(row)["batchReportCount"]==2,"last actual batch report decoded with exact position");
    check(witness(row)["postedTick"]==91&&witness(row)["queueDelayMs"]==10&&witness(row)["receiptTick"]==101,"queued time is separate from receipt time");
    check(collection(row)["semanticInputDecoded"]==true&&collection(row)["completeWitnesses"]==1,"successful decoder evidence is visible");
    check(collection(row)["reportBytesLogged"]==false&&witness(row)["reportBytesLogged"]==false,"no raw bytes retained in output");
    check(row["steamConsumptionProven"]==false&&row["dimmingImpactProven"]==false,"decoded receipt is not Steam or idle-impact proof");
    counter.observeWithDecoder(1,identity,bytes,2,1,2,112,{},decode);row=counter.flush(113);
    check(decodes==1&&collection(row)["semanticWitnessSuppressed"]==1&&collection(row)["semanticWitnesses"].empty(),"flush cannot reset budget and manufacture a new sample");
    counter.removed(1);counter.observeWithDecoder(1,identity,bytes,2,1,2,114,{},decode);row=counter.flush(115);
    check(decodes==1&&collection(row)["deviceGeneration"]==2,"removal and reused handle cannot bypass budget");
    counter.observeWithDecoder(2,identity,bytes,2,1,2,116,{},decode);row=counter.flush(117);
    check(decodes==1,"another raw handle cannot bypass global budget");
    counter.observeWithDecoder(2,identity,bytes,2,1,2,601,{},decode);row=counter.flush(602);
    check(decodes==2&&witness(row)["postedTick"].is_null()&&witness(row)["queueDelayMs"].is_null(),"missing queue time stays unknown");
    counter.observeWithDecoder(2,identity,bytes,2,1,2,550,{},decode);row=counter.flush(603);
    check(decodes==2&&collection(row)["semanticWitnessClockRollbacks"]==1,"counter serializes suppressed rollback");
    RawReceiptCounters vendor;auto vendorIdentity=identity;vendorIdentity.usagePage=0xfff0;vendorIdentity.usage=0x40;vendorIdentity.path=R"(\\?\hid#vid_0db0&pid_1902&mi_00&col02#fixture)";const auto before=decodes;
    vendor.observeWithDecoder(3,vendorIdentity,bytes,2,1,2,1000,{},decode);row=vendor.flush(1001);
    check(collection(row)["reportReceipts"]==1&&decodes==before&&collection(row)["semanticWitnesses"].empty(),"vendor receipt remains counts-only and never decoded");
    RawReceiptCounters consumer;vendorIdentity.usagePage=12;vendorIdentity.usage=1;vendorIdentity.path=R"(\\?\hid#vid_0db0&pid_1902&mi_01&col03#fixture)";
    consumer.observeWithDecoder(3,vendorIdentity,bytes,2,1,2,1000,{},decode);row=consumer.flush(1001);
    check(decodes==before&&collection(row)["semanticWitnessCandidates"]==0,"consumer payload is never semantically decoded");
    RawReceiptCounters invalid;check(!invalid.observeWithDecoder(1,identity,bytes,2,3,4,1000,{},decode)&&decodes==before,"malformed batch cannot reach decoder");
    auto foreign=identity;foreign.vid=0x045e;check(!invalid.observeWithDecoder(1,foreign,bytes,2,1,2,1000,{},decode)&&decodes==before,"other family cannot reach decoder");
    RawReceiptCounters overflow;overflow.begin(1000);decodes=0;
    for(unsigned i=0;i<8;++i)overflow.observeWithDecoder(1,identity,bytes,2,1,2,1000+500*i,{},decode);
    row=overflow.flush(5000);
    check(decodes==8&&collection(row)["semanticWitnesses"].size()==2,"delayed flush keeps fixed first/latest storage");
    check(collection(row)["semanticWitnessRecordsOverwritten"]==6&&witness(row)["selectionSequence"]==1&&witness(row,1)["selectionSequence"]==8,"replacement loss is explicit with first/latest sequence");
    RawReceiptCounters storm;storm.begin(1000);decodes=0;
    for(unsigned i=0;i<100000;++i)storm.observeWithDecoder(1,identity,bytes,2,1,2,1000+i/100,{},decode);
    row=storm.flush(2000);
    check(decodes==2&&collection(row)["semanticWitnessSelections"]==2&&collection(row)["semanticWitnessSuppressed"]==99998,"100k receipts trigger only two metadata/parse callbacks");
    check(collection(row)["messageReceipts"]==100000&&collection(row)["reportReceipts"]==100000,"sampling preserves existing total receipt counts");
    RawReceiptCounters retired;decodes=0;retired.begin(1000);retired.observeWithDecoder(1,identity,bytes,2,1,2,1000,{},decode);retired.removed(1);retired.observeWithDecoder(1,identity,bytes,2,1,2,1500,{},decode);row=retired.flush(1600);
    check(row["collections"].size()==2&&row["collections"][0]["deviceGeneration"]==1&&row["collections"][1]["deviceGeneration"]==2,"retired and new handle generations retain separate witnesses");
    check(row["collections"][0]["semanticWitnesses"][0]["selectionSequence"]==1&&row["collections"][1]["semanticWitnesses"][0]["selectionSequence"]==2,"sequence survives generations within observer");
    RawReceiptCounters partial;partial.observeWithDecoder(1,identity,bytes,2,1,2,1000,{},[](const unsigned char*,unsigned){return json{{"state","partial"},{"decodedControlsKnown",true},{"decodeComplete",false}};});row=partial.flush(1001);
    check(collection(row)["decodedWitnesses"]==1&&collection(row)["completeWitnesses"]==0,"partial success never becomes complete");
    RawReceiptCounters unavailable;unavailable.observe(1,identity,bytes,2,1,2,1000);row=unavailable.flush(1001);
    check(witness(row)["controls"]["state"]=="decoder-not-connected"&&collection(row)["semanticInputDecoded"]==false,"unconnected decoder remains explicit unknown");
    std::cout<<"cases="<<cases<<" failures="<<failures<<" realRawInputCalls=0 realHidCalls=0 RESULT="<<(failures?"FAIL":"PASS")<<"\n";return failures?1:0;
}
