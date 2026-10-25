#include "../native/steam_native_focus_feedback.h"
#include <iostream>
#include <stdexcept>
using namespace ymcc::steamlink;
static unsigned count=0;
static void expect(bool value){++count;if(!value)throw std::runtime_error("native focus comparison failed");}
static FocusJson tuple(unsigned app=123,unsigned pid=456){return {{"appid",app},{"pid",pid},{"windowid",0},{"exeName",nullptr}};}
static FocusJson good(){return {{"ok",true},{"mutated",false},{"nativeFocusFeedback",{
    {"implementation","steam-native-focus-feedback-v1"},{"source","steam-native-focus-subscription"},
    {"available",true},{"callbackReceived",true},{"ownSubscriptionUnregistered",true},
    {"focused",tuple()},{"focusable",FocusJson::array({tuple()})}}}};}
int main(){try{
    auto r=compareNativeFocus(good(),123,456);
    expect(r["available"]==true);expect(r["focusedRelation"]=="same-original-pid-and-appid");
    expect(r["focusableRelation"]=="contains-original-pid-and-appid");
    for(const auto* k:{"fullGameIdCarried","processBirthCarried","nativeCacheFreshnessKnown","originalInstanceReattached","gameInputVerified","continueVerified"})expect(r[k]==false);
    for(const auto* k:{"available","callbackReceived","ownSubscriptionUnregistered"})for(const auto& v:{FocusJson(false),FocusJson("true"),FocusJson(1),FocusJson(nullptr)}){
        auto x=good();x["nativeFocusFeedback"][k]=v;expect(compareNativeFocus(x,123,456)["available"]==false);}
    for(const auto* k:{"implementation","source"})for(const auto& v:{FocusJson("unknown"),FocusJson(1),FocusJson(nullptr)}){
        auto x=good();x["nativeFocusFeedback"][k]=v;expect(compareNativeFocus(x,123,456)["available"]==false);}
    for(const auto* k:{"appid","pid","windowid"})for(const auto& v:{FocusJson(-1),FocusJson(1.5),FocusJson("123"),FocusJson(4294967296ULL),FocusJson(nullptr)}){
        auto x=good();x["nativeFocusFeedback"]["focused"][k]=v;expect(compareNativeFocus(x,123,456)["available"]==false);
        x=good();x["nativeFocusFeedback"]["focusable"][0][k]=v;expect(compareNativeFocus(x,123,456)["available"]==false);}
    for(const auto& v:{FocusJson(123),FocusJson::array(),FocusJson(std::string(641,'x'))}){auto x=good();x["nativeFocusFeedback"]["focused"]["exeName"]=v;expect(compareNativeFocus(x,123,456)["available"]==false);}
    for(const auto* k:{"ok","mutated"}){auto x=good();x.erase(k);expect(compareNativeFocus(x,123,456)["available"]==false);}
    {auto x=good();x["mutated"]=true;expect(compareNativeFocus(x,123,456)["available"]==false);}
    {auto x=good();x["nativeFocusFeedback"]["focusable"]=FocusJson::array();expect(compareNativeFocus(x,123,456)["focusableRelation"]=="original-tuple-absent");}
    for(unsigned n:{128,129}){auto x=good();auto& a=x["nativeFocusFeedback"]["focusable"];a=FocusJson::array();for(unsigned i=0;i<n;++i)a.push_back(tuple());expect(compareNativeFocus(x,123,456)["available"]==(n==128));}
    for(const auto& row:{tuple(123,789),tuple(789,456),tuple(789,789)}){auto x=good();x["nativeFocusFeedback"]["focused"]=row;x["nativeFocusFeedback"]["focusable"]=FocusJson::array({row});auto y=compareNativeFocus(x,123,456);expect(y["available"]==true);expect(y["focusedRelation"]!="same-original-pid-and-appid");expect(y["focusableRelation"]!="contains-original-pid-and-appid");}
    {auto x=good();x["nativeFocusFeedback"]["focusable"]=FocusJson::array({tuple(123,789),tuple(789,456)});expect(compareNativeFocus(x,123,456)["focusableRelation"]=="separate-app-and-pid-candidates");}
    {auto x=good();x["nativeFocusFeedback"]["focused"]=tuple(0xffffffff,0xffffffff);x["nativeFocusFeedback"]["focusable"]=FocusJson::array({tuple(0xffffffff,0xffffffff)});expect(compareNativeFocus(x,0xffffffff,0xffffffff)["available"]==true);}
    expect(compareNativeFocus(good(),0,456)["available"]==false);expect(compareNativeFocus(good(),123,0)["available"]==false);
    expect(compareNativeFocus(FocusJson(nullptr),123,456)["available"]==false);
    std::cout<<"native-focus-comparison assertions="<<count<<" passed; no OS/Steam actions\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
