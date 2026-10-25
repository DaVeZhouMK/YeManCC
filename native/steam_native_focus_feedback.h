#pragma once
#include "json.hpp"
#include <cstdint>
#include <string>

// Native focus tuples have neither full GameID nor process birth. Comparisons
// here describe the callback only; they never grant running/recovery authority.
namespace ymcc::steamlink {
using FocusJson = nlohmann::json;
inline bool focusUInt(const FocusJson& row,const char* name,std::uint32_t& out) {
    if(!row.is_object()||!row.contains(name))return false;
    const auto& value=row[name];
    if(!value.is_number_integer()&&!value.is_number_unsigned())return false;
    if(value.is_number_integer()&&!value.is_number_unsigned()&&value.get<std::int64_t>()<0)return false;
    const auto n=value.get<std::uint64_t>();if(n>UINT32_MAX)return false;
    out=static_cast<std::uint32_t>(n);return true;
}
inline bool focusTupleValid(const FocusJson& row) {
    std::uint32_t app=0,pid=0,window=0;
    if(!focusUInt(row,"appid",app)||!focusUInt(row,"pid",pid)||!focusUInt(row,"windowid",window))return false;
    return !row.contains("exeName")||row["exeName"].is_null()||
        (row["exeName"].is_string()&&row["exeName"].get<std::string>().size()<=640); // UTF-8: <=160 UTF-16 chars from JS.

}
inline FocusJson compareNativeFocus(const FocusJson& read,std::uint32_t appid,std::uint32_t pid) {
    FocusJson result={{"implementation","steam-native-focus-feedback-v1"},{"authority","diagnostic-only-no-admission"},
        {"available",false},{"focusedRelation","unknown"},{"focusableRelation","unknown"},
        {"fullGameIdCarried",false},{"processBirthCarried",false},{"nativeCacheFreshnessKnown",false},
        {"originalInstanceReattached",false},{"gameInputVerified",false},{"continueVerified",false}};
    const auto yes=[](const FocusJson& x,const char* key){return x.is_object()&&x.contains(key)&&x[key].is_boolean()&&x[key].get<bool>();};
    if(!appid||!pid||!yes(read,"ok")||!read.contains("mutated")||!read["mutated"].is_boolean()||yes(read,"mutated")||
        !read.contains("nativeFocusFeedback")||!read["nativeFocusFeedback"].is_object())return result;
    const auto& f=read["nativeFocusFeedback"];
    const auto text=[](const FocusJson& x,const char* key,const char* expected){return x.contains(key)&&x[key].is_string()&&x[key].get<std::string>()==expected;};
    // Copy the receipt even if unavailable, so timeout/schema/cleanup failure is
    // visible. A missing callback never falls back to a UI focus-store value.
    result["receipt"]=f;
    if(!yes(f,"available")||!yes(f,"callbackReceived")||!yes(f,"ownSubscriptionUnregistered")||
        !text(f,"implementation","steam-native-focus-feedback-v1")||
        !text(f,"source","steam-native-focus-subscription")||
        !f.contains("focused")||!focusTupleValid(f["focused"])||!f.contains("focusable")||
        !f["focusable"].is_array()||f["focusable"].size()>128)return result;
    for(const auto& row:f["focusable"])if(!focusTupleValid(row))return result;
    result["available"]=true;
    std::uint32_t focusApp=0,focusPid=0;focusUInt(f["focused"],"appid",focusApp);focusUInt(f["focused"],"pid",focusPid);
    result["focusedRelation"]=focusApp==appid&&focusPid==pid?"same-original-pid-and-appid":
        focusApp==appid?"same-app-different-pid":focusPid==pid?"same-pid-different-app":"different-tuple";
    bool exact=false,sameApp=false,samePid=false;
    for(const auto& row:f["focusable"]){std::uint32_t a=0,p=0;focusUInt(row,"appid",a);focusUInt(row,"pid",p);
        exact|=a==appid&&p==pid;sameApp|=a==appid;samePid|=p==pid;}
    result["focusableRelation"]=exact?"contains-original-pid-and-appid":sameApp&&samePid?"separate-app-and-pid-candidates":
        sameApp?"same-app-different-pid":samePid?"same-pid-different-app":"original-tuple-absent";
    return result;
}
} // namespace ymcc::steamlink
