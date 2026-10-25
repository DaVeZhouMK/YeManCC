#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Pure MSI-only planning. No Windows/device API, descendant enumeration, writes,
// HidHide support claim, or ownership mutation. Caller supplies resolved facts.
namespace ymcc::msi::auxiliaryCloak {
struct SourceIdentity {
    bool valid=false, ancestryParsed=false;
    std::string instanceId, baseInstanceId, physicalParentId, sourceToken;
    unsigned vid=0, pid=0;
    std::uint64_t powerGeneration=0, bindGeneration=0;
};
struct Record {
    std::string instanceId, baseInstanceId, physicalParentId, miCol;
    unsigned vid=0, pid=0, usagePage=0, usage=0;
    bool ancestryParsed=false, capsKnown=false, virtualIdentity=false;
};
struct Facts {
    bool authoritativeMsiFamily=false, virtualEnabled=false;
    bool uniqueManagedSource=false, inventoryReadComplete=false;
    // Taken before/after inventory, not an atomic simultaneous snapshot. Caller
    // must use monotonic generations/tokens to detect intervening ownership ABA.
    SourceIdentity before, after;
    std::vector<Record> inventory;
};
struct Plan {
    std::vector<std::string> targetIds;
    std::string reason;
    bool generationReadsAtomicTogether=false;
};
inline std::string folded(const std::string& text) {
    std::string result=text;
    for(char& c:result) if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
    return result;
}
inline bool cleanIdentity(const std::string& value) noexcept {
    if(value.empty()||value.size()>512)return false;
    for(const auto* sentinel:{"UNKNOWN","UNRESOLVED","NULL","NONE","N/A"}){
        std::size_t i=0;
        for(;i<value.size()&&sentinel[i];++i){
            char c=value[i];if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
            if(c!=sentinel[i])break;
        }
        if(i==value.size()&&sentinel[i]=='\0')return false;
    }
    for(const unsigned char c:value)if(c<=32||c>=127)return false;
    return true;
}
inline bool hasPrefixIdentity(const std::string& value,const std::string& prefix) {
    const auto id=folded(value);
    return cleanIdentity(value)&&id.compare(0,prefix.size(),prefix)==0&&id.size()>prefix.size();
}
inline bool physicalRoot(const std::string& value) {
    return hasPrefixIdentity(value,"USB\\VID_0DB0&PID_1902\\");
}
inline std::string collection(const std::string& value) {
    const auto id=folded(value);
    const std::string prefix="HID\\VID_0DB0&PID_1902&";
    if(!cleanIdentity(value)||id.compare(0,prefix.size(),prefix)!=0)return {};
    const auto slash=id.find('\\',prefix.size());
    if(slash==std::string::npos||slash+1==id.size())return {};
    return id.substr(prefix.size(),slash-prefix.size());
}
inline bool sameSource(const SourceIdentity& a,const SourceIdentity& b) noexcept {
    // Exact before/after strings; even case-only drift fails closed. Inventory
    // identity matching below follows Windows case-insensitive identity rules.
    return a.valid==b.valid&&a.ancestryParsed==b.ancestryParsed&&
        a.instanceId==b.instanceId&&a.baseInstanceId==b.baseInstanceId&&
        a.physicalParentId==b.physicalParentId&&a.sourceToken==b.sourceToken&&
        a.vid==b.vid&&a.pid==b.pid&&a.powerGeneration==b.powerGeneration&&a.bindGeneration==b.bindGeneration;
}
inline Plan makePlan(const Facts& facts) {
    const auto reject=[](const char* reason){return Plan{{},reason,false};};
    if(!facts.authoritativeMsiFamily)return reject("not-authoritative-msi-family");
    if(!facts.virtualEnabled)return reject("virtual-disabled");
    if(!facts.uniqueManagedSource)return reject("managed-source-not-unique");
    if(!facts.inventoryReadComplete)return reject("inventory-incomplete");
    const auto& source=facts.before;
    if(!source.valid||!facts.after.valid||!source.ancestryParsed||!facts.after.ancestryParsed||
        !cleanIdentity(source.sourceToken)||!source.powerGeneration||!source.bindGeneration||
        !cleanIdentity(source.instanceId)||!physicalRoot(source.baseInstanceId)||
        !physicalRoot(source.physicalParentId))return reject("source-identity-unknown");
    if(source.vid!=0x0DB0||source.pid!=0x1902||collection(source.instanceId)!="MI_00&COL01")
        return reject("source-outside-exact-1902-main-collection");
    if(!sameSource(source,facts.after))return reject("source-power-bind-or-identity-changed");
    const auto root=folded(source.baseInstanceId),parent=folded(source.physicalParentId);
    // physicalParentId is the resolved common physical USB parent/ancestor,
    // not each MI interface's different immediate parent. It must be the same
    // exact physical root; matching VID/PID or a shared hub alone is insufficient.
    if(root!=parent)return reject("source-parent-base-conflict");
    std::vector<std::string> seen;
    for(const auto& record:facts.inventory){
        if(!cleanIdentity(record.instanceId))return reject("inventory-identity-unknown");
        const auto id=folded(record.instanceId);
        if(std::find(seen.begin(),seen.end(),id)!=seen.end())return reject("duplicate-or-case-alias-identity");
        seen.push_back(id);
    }
    std::string vendor,consumer;
    unsigned mains=0;
    for(const auto& record:facts.inventory){
        const auto mi=collection(record.instanceId);
        const bool relevant=mi=="MI_00&COL01"||mi=="MI_00&COL02"||mi=="MI_01&COL03";
        if(!relevant)continue; // keyboard/mouse/sensor/other vendor collections never selected.
        if(record.vid!=0x0DB0||record.pid!=0x1902)return reject("record-vid-pid-identity-conflict");
        if(record.virtualIdentity)return reject("virtual-record-rejected");
        if(!record.ancestryParsed||!physicalRoot(record.baseInstanceId)||!physicalRoot(record.physicalParentId))
            return reject("record-ancestry-unknown");
        if(folded(record.baseInstanceId)!=root||folded(record.physicalParentId)!=parent)continue;
        if(folded(record.miCol)!=mi)return reject("record-mi-col-identity-conflict");
        if(!record.capsKnown)return reject("record-caps-unknown");
        // Bound only the two selected auxiliaries to the CLI instance-ID limit.
        // Source/interface tokens retain the existing independent 512-byte gate.
        if(mi!="MI_00&COL01"&&record.instanceId.size()>200)
            return reject("auxiliary-instance-id-exceeds-200");
        if(mi=="MI_00&COL01"){
            if(record.usagePage!=1||record.usage!=5)return reject("main-caps-unexpected");
            ++mains;
            if(folded(record.instanceId)!=folded(source.instanceId))return reject("same-root-main-source-ambiguous");
        }else if(mi=="MI_00&COL02"){
            if(record.usagePage!=0xFFF0||record.usage!=0x40)return reject("vendor-caps-unexpected");
            if(!vendor.empty())return reject("vendor-collection-not-unique");
            vendor=record.instanceId;
        }else{
            if(record.usagePage!=0x0C||record.usage!=1)return reject("consumer-caps-unexpected");
            if(!consumer.empty())return reject("consumer-collection-not-unique");
            consumer=record.instanceId;
        }
    }
    if(mains!=1)return reject("managed-main-record-missing-or-not-unique");
    if(vendor.empty()||consumer.empty())return reject("both-exact-auxiliary-collections-required");
    return Plan{{vendor,consumer},"two-exact-same-source-auxiliary-collections",false};
}
inline bool containsIdentity(const std::vector<std::string>& ids,const std::string& wanted) {
    const auto key=folded(wanted);
    for(const auto& id:ids)if(folded(id)==key)return true;
    return false;
}
// A target plan is not ownership. Apply ONLY to these new auxiliary IDs before
// entering an existing loop; existing main/root behavior remains the caller's.
inline std::vector<std::string> journalEligibleTargets(const Plan& plan,
    const std::vector<std::string>& beforeHidden,const std::vector<std::string>& journalOwned,
    const std::vector<std::string>& carriedOwned) {
    std::vector<std::string> result;
    if(plan.reason!="two-exact-same-source-auxiliary-collections"||plan.targetIds.size()!=2)return result;
    for(const auto& id:plan.targetIds){
        if(!containsIdentity(beforeHidden,id)||containsIdentity(journalOwned,id)||containsIdentity(carriedOwned,id))
            result.push_back(id);
    }
    return result;
}
} // namespace ymcc::msi::auxiliaryCloak