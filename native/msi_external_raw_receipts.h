#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <json.hpp>
// Independent-observer evidence only; never a production control permission.
namespace ymcc::msi::external {
using nlohmann::json;
inline std::string lowerAscii(std::string value) { for(auto& c:value)if(c>='A'&&c<='Z')c=char(c+'a'-'A');return value; }
inline uint64_t receiptHash(const unsigned char* data,size_t bytes) { uint64_t value=1469598103934665603ull;while(bytes--){value^=*data++;value*=1099511628211ull;}return value; }
struct Comparison { bool havePrior=false,changed=false;uint64_t series=0,priorInventory=0,gapMs=0; };
class ControlComparisons {
    struct Prior {std::string content;uint64_t inventory=0,tick=0,series=0;};
    std::map<std::string,Prior> states;uint64_t nextSeries=0;
public:
    Comparison observe(const std::string& key,const json& loggedControls,uint64_t inventory,uint64_t tick) {
        Comparison result;auto found=states.find(key);
        if(found!=states.end()) {
            const auto& p=found->second;result.priorInventory=p.inventory;
            result.gapMs=tick>=p.tick?tick-p.tick:0;
            result.havePrior=inventory>0&&p.inventory>0&&inventory==p.inventory+1&&tick>=p.tick&&result.gapMs<=3000;
            if(result.havePrior){result.series=p.series;result.changed=p.content!=loggedControls.dump();}
        }
        if(!result.havePrior)result.series=++nextSeries;
        if(states.size()>=256&&found==states.end())states.clear();
        states[key]={loggedControls.dump(),inventory,tick,result.series};return result;
    }
};
struct QueueTime {bool known=false;uint64_t postedTick=0,delayMs=0;};
inline QueueTime resolveQueueTime(uint64_t receiptTick,uint32_t postedTick32){
    const uint64_t delay=uint32_t(uint32_t(receiptTick)-postedTick32);
    // Diagnostic session ceiling only; not a hardware freshness allowance.
    if(delay>900000||receiptTick<delay)return {};
    return {true,receiptTick-delay,delay};
}
struct RawIdentity {bool known=false,hid=false;unsigned vid=0,pid=0,usagePage=0,usage=0;std::string path;};
inline const char* rawRole(const RawIdentity& d) {
    if(!d.known||!d.hid||d.vid!=0x0db0)return "unqualified";
    const auto p=lowerAscii(d.path);
    if(d.pid==0x1901&&p.starts_with("\\\\?\\hid#vid_0db0&pid_1901&")&&d.usagePage==1&&(d.usage==4||d.usage==5))return "msi-xinput-game-collection";
    if(d.pid==0x1901&&p.starts_with("\\\\?\\hid#vid_0db0&pid_1901&mi_01#")&&d.usagePage==0xffa0&&d.usage==1)return "msi-xinput-vendor-collection";
    if(d.pid==0x1901&&p.starts_with("\\\\?\\hid#vid_0db0&pid_1901&mi_02&col03#")&&d.usagePage==12&&d.usage==1)return "msi-xinput-consumer-collection";
    if(d.pid!=0x1902)return "unqualified";
    if(p.starts_with("\\\\?\\hid#vid_0db0&pid_1902&mi_00&col01#")&&d.usagePage==1&&(d.usage==4||d.usage==5))return "msi-dinput-main-collection";
    if(p.starts_with("\\\\?\\hid#vid_0db0&pid_1902&mi_00&col02#")&&d.usagePage==0xfff0&&d.usage==0x40)return "msi-vendor-collection";
    if(p.starts_with("\\\\?\\hid#vid_0db0&pid_1902&mi_01&col03#")&&d.usagePage==12&&d.usage==1)return "msi-consumer-collection";
    return "unqualified";
}
inline bool rawShape(unsigned size,unsigned count,size_t available) {
    return size>0&&size<=4096&&count>0&&count<=1024&&uint64_t(size)*count<=65536&&uint64_t(size)*count<=available;
}
// One global budget for this observer, retained across flush/removal/rebinding.
// Selection only; it owns no device, timer, thread or production input state.
class GamepadWitnessBudget {
    bool haveSelection=false;
    uint64_t lastSelection=0;
public:
    enum class Decision { selected, interval, clockRollback };
    Decision select(uint64_t now) {
        if(haveSelection&&now<lastSelection)return Decision::clockRollback;
        if(haveSelection&&now-lastSelection<500)return Decision::interval;
        haveSelection=true;lastSelection=now;return Decision::selected;
    }
};
inline bool mainGamepad(const RawIdentity& identity) {
    const std::string_view role=rawRole(identity);
    return role=="msi-dinput-main-collection"||role=="msi-xinput-game-collection";
}
class RawReceiptCounters {
    struct Row {RawIdentity identity;uint64_t handle=0,generation=0,messages=0,reports=0,changed=0,firstTick=0,lastTick=0;
        unsigned lastSize=0;uint64_t hash=0;bool prior=false;
        uint64_t queueKnown=0,queueUnknown=0,firstQueuedTick=0,lastQueuedTick=0,maxQueueDelay=0,delayedMessages=0;
        std::array<json,2> witnesses{};unsigned witnessCount=0;
        uint64_t witnessCandidates=0,witnessSelected=0,witnessSuppressed=0,witnessRollbacks=0,
            witnessOverwritten=0,decodedWitnesses=0,completeWitnesses=0;};
    std::map<uint64_t,Row> devices;std::vector<Row> retired;uint64_t nextGeneration=0,windowStart=0;
    uint64_t messages=0,accepted=0,unqualified=0,malformed=0,readErrors=0,limited=0,pumpLimits=0;
    GamepadWitnessBudget witnessBudget;
    uint64_t nextWitness=0;
void retire(const Row& row){if(!row.messages)return;if(retired.size()<32)retired.push_back(row);else ++limited;}
public:
    void begin(uint64_t now){windowStart=now;}
    void error(){++readErrors;}void invalid(){++malformed;}void pumpLimited(){++pumpLimits;}
    void removed(uint64_t handle){auto row=devices.find(handle);if(row!=devices.end()){retire(row->second);devices.erase(row);}}
    bool observe(uint64_t handle,const RawIdentity& identity,const unsigned char* data,unsigned size,unsigned count,size_t available,uint64_t now,QueueTime queue={}) {
        return observeWithDecoder(handle,identity,data,size,count,available,now,queue,
            [](const unsigned char*,unsigned){return json{{"state","decoder-not-connected"},{"decodedControlsKnown",false},{"decodeComplete",false}};});
    }
    template<class Decoder>
    bool observeWithDecoder(uint64_t handle,const RawIdentity& identity,const unsigned char* data,unsigned size,unsigned count,size_t available,uint64_t now,QueueTime queue,Decoder&& decode) {
        ++messages;
        if(!rawShape(size,count,available)||!data){++malformed;return false;}
        const std::string_view role=rawRole(identity);
        if(!handle||role=="unqualified"){++unqualified;removed(handle);return false;}
        auto found=devices.find(handle);
        if(found==devices.end()&&devices.size()>=32){++limited;return false;}
        if(found==devices.end())found=devices.emplace(handle,Row{}).first;
        auto& row=found->second;
        const bool sameIdentity=row.identity.known&&lowerAscii(row.identity.path)==lowerAscii(identity.path)&&row.identity.vid==identity.vid&&row.identity.pid==identity.pid&&row.identity.usagePage==identity.usagePage&&row.identity.usage==identity.usage;
        if(!sameIdentity){retire(row);row={};row.handle=handle;row.identity=identity;row.generation=++nextGeneration;}
        if(!row.firstTick)row.firstTick=now;
        ++row.messages;row.reports+=count;++accepted;
        if(queue.known){if(!row.queueKnown)row.firstQueuedTick=queue.postedTick;row.firstQueuedTick=std::min(row.firstQueuedTick,queue.postedTick);row.lastQueuedTick=std::max(row.lastQueuedTick,queue.postedTick);++row.queueKnown;row.maxQueueDelay=std::max(row.maxQueueDelay,queue.delayMs);if(queue.delayMs>3000)++row.delayedMessages;}else ++row.queueUnknown;
        if(!row.lastTick||now<row.lastTick||now-row.lastTick>3000)row.prior=false;
        for(unsigned i=0;i<count;++i){const auto hash=receiptHash(data+size_t(i)*size,size);if(row.prior&&(row.lastSize!=size||row.hash!=hash))++row.changed;row.hash=hash;row.lastSize=size;row.prior=true;}
        const bool witnessEligible=role=="msi-dinput-main-collection"||role=="msi-xinput-game-collection";
        if(witnessEligible) {
            ++row.witnessCandidates;
            const auto decision=witnessBudget.select(now);
            if(decision!=GamepadWitnessBudget::Decision::selected) {
                ++row.witnessSuppressed;
                if(decision==GamepadWitnessBudget::Decision::clockRollback)++row.witnessRollbacks;
            } else {
                // A batched message can contain many reports. Decode only its
                // last report; other reports remain counted, never called neutral.
                const auto* selected=data+size_t(count-1)*size;
                auto controls=decode(selected,size);
                if(!controls.is_object())controls={{"state","decoder-result-invalid"},{"decodedControlsKnown",false},{"decodeComplete",false}};
                ++row.witnessSelected;
                if(controls.value("decodedControlsKnown",false))++row.decodedWitnesses;
                if(controls.value("decodeComplete",false))++row.completeWitnesses;
                json witness={{"version",1},{"selectionSequence",++nextWitness},{"receiptTick",now},
                    {"queueTimestampKnown",queue.known},{"postedTick",queue.known?json(queue.postedTick):json(nullptr)},
                    {"queueDelayMs",queue.known?json(queue.delayMs):json(nullptr)},
                    {"hardwareSampleTimeKnown",false},{"batchReportIndex",count-1},{"batchReportCount",count},
                    {"reportSize",size},{"reportId",selected[0]},{"payloadReceiptHash",row.hash},
                    {"controls",std::move(controls)},{"reportBytesLogged",false}};
                if(row.witnessCount<row.witnesses.size())row.witnesses[row.witnessCount++]=std::move(witness);
                else {row.witnesses.back()=std::move(witness);++row.witnessOverwritten;}
            }
        }
        row.lastTick=now;return true;
    }
    json flush(uint64_t now) {
        json rows=json::array();const auto append=[&](const Row& row){
            const auto handle=row.handle;
            json witnesses=json::array();
            for(unsigned i=0;i<row.witnessCount;++i)witnesses.push_back(row.witnesses[i]);
            if(row.messages)rows.push_back({{"interfacePath",row.identity.path},{"rawDeviceHandle",handle},{"deviceGeneration",row.generation},
                {"vid",row.identity.vid},{"pid",row.identity.pid},{"usagePage",row.identity.usagePage},{"usage",row.identity.usage},
                {"collectionRole",rawRole(row.identity)},{"messageReceipts",row.messages},{"reportReceipts",row.reports},
                {"changedPayloadReports",row.changed},{"firstReceiptTick",row.firstTick},{"lastReceiptTick",row.lastTick},
                {"reportBytesLogged",false},{"semanticInputDecoded",row.decodedWitnesses>0},
                {"semanticWitnessVersion",1},{"semanticWitnesses",std::move(witnesses)},
                {"semanticWitnessCandidates",row.witnessCandidates},{"semanticWitnessSelections",row.witnessSelected},
                {"semanticWitnessSuppressed",row.witnessSuppressed},{"semanticWitnessClockRollbacks",row.witnessRollbacks},
                {"semanticWitnessRecordsOverwritten",row.witnessOverwritten},{"decodedWitnesses",row.decodedWitnesses},
                {"completeWitnesses",row.completeWitnesses},
                {"queueTimestampsKnown",row.queueKnown},{"queueTimestampsUnknown",row.queueUnknown},{"firstQueuedTick",row.firstQueuedTick},{"lastQueuedTick",row.lastQueuedTick},
                {"maxQueueDelayMs",row.maxQueueDelay},{"delayedMessages",row.delayedMessages}});
        };
        for(const auto& row:retired)append(row);retired.clear();
        for(auto& [handle,row]:devices){
            append(row);row.messages=row.reports=row.changed=row.firstTick=0;
            row.queueKnown=row.queueUnknown=row.firstQueuedTick=row.lastQueuedTick=row.maxQueueDelay=row.delayedMessages=0;
            for(auto& witness:row.witnesses)witness=nullptr;
            row.witnessCount=0;row.witnessCandidates=row.witnessSelected=row.witnessSuppressed=row.witnessRollbacks=
                row.witnessOverwritten=row.decodedWitnesses=row.completeWitnesses=0;
        }
        json result={{"event","msi-rawinput-receipt-window"},{"windowStartTick",windowStart},{"windowEndTick",now},{"collections",rows},
            {"hidMessagesExamined",messages},{"acceptedMessages",accepted},{"unqualifiedMessages",unqualified},{"malformedMessages",malformed},
            {"readErrors",readErrors},{"deviceLimitDrops",limited},{"messagePumpLimitHits",pumpLimits},
            {"queueTimestampScope","MSG.time resolved against receipt uptime; not hardware-generation time"},
            {"semanticWitnessVersion",1},{"semanticWitnessMaxPerSecond",2},
            {"semanticWitnessScope","selected main gamepad reports only; at most one per 500ms across all devices/generations; first/latest two retained per collection/window; no raw bytes; unsampled, unsupported or missing controls remain unknown"},
            {"writesHardware",false},{"steamConsumptionProven",false},{"dimmingImpactProven",false},
            {"scope","actual WM_INPUT receipts in independent observer only; payload changes alone may be heartbeat/counter; sampled controls do not prove operator-idle state, Steam delivery or filter leakage; zero receipts do not prove suppression"}};
        messages=accepted=unqualified=malformed=readErrors=limited=pumpLimits=0;windowStart=now;
        for(auto i=devices.begin();i!=devices.end();)if(now>=i->second.lastTick&&now-i->second.lastTick>3000)i=devices.erase(i);else ++i;
        return result;
    }
};
}
