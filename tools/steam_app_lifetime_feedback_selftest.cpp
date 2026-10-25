#include "../native/steam_app_lifetime_feedback.h"
#include <iostream>
using namespace ymcc::steamlink;
static int count=0;
static void expect(bool value,const char* message){++count;if(!value)throw std::runtime_error(message);}
static LifetimeJson base(){return {{"ok",true},{"mutated",false},{"nativeAppLifetimeFeedback",{{"implementation","steam-app-lifetime-feedback-v1"},{"source","steam-game-sessions-app-lifetime-subscription"},{"available",true},{"observerInstalled",true},{"events",LifetimeJson::array()}}}};}
static LifetimeJson event(std::uint32_t app,const char* instance,bool running,unsigned seq){return {{"sequence",seq},{"unAppID",app},{"nInstanceID",instance},{"bRunning",running},{"receivedAtUnixMs",1},{"receivedAtPerformanceMs",2}};}
int main(){try{
    {auto x=base();auto r=compareNativeAppLifetime(x,123);expect(r["available"]==true,"empty observer available");expect(r["eventRelation"]=="no-events","no event relation");}
    {auto x=base();x["nativeAppLifetimeFeedback"]["events"]=LifetimeJson::array({event(123,"77",true,1)});auto r=compareNativeAppLifetime(x,123);expect(r["available"]==true,"running event available");expect(r["originalAppRunningKnown"]==true&&r["originalAppRunning"]==true,"running event state");expect(r["instanceRelation"]=="single-steam-instance-id","single instance");expect(r["lastOriginalSteamInstanceID"]=="77","instance preserved");}
    {auto x=base();x["nativeAppLifetimeFeedback"]["events"]=LifetimeJson::array({event(123,"77",true,1),event(123,"88",false,2)});auto r=compareNativeAppLifetime(x,123);expect(r["originalAppRunning"]==false,"latest stopped");expect(r["instanceRelation"]=="multiple-steam-instance-ids","multiple instances");}
    {auto x=base();x["nativeAppLifetimeFeedback"]["events"]=LifetimeJson::array({event(999,"8",true,1)});auto r=compareNativeAppLifetime(x,123);expect(r["eventRelation"]=="other-app-only","other event isolated");expect(r["originalAppRunningKnown"]==false,"other event not borrowed");}
    {auto x=base();x["nativeAppLifetimeFeedback"]["events"]=LifetimeJson::array({{{"unAppID",123},{"nInstanceID","7"},{"bRunning",true}}});auto r=compareNativeAppLifetime(x,123);expect(r["available"]==false,"missing sequence rejected");}
    {auto x=base();x["mutated"]=true;auto r=compareNativeAppLifetime(x,123);expect(r["available"]==false,"mutated read rejected");}
    {auto x=base();auto& f=x["nativeAppLifetimeFeedback"];f["observerInstanceId"]="watch-a";f["observerCreatedAtUnixMs"]=1234;f["diagnosticCountersAvailable"]=true;f["rawCallbacksEntered"]=5;f["accountUnavailableCallbacks"]=2;f["accountMismatchCallbacks"]=1;f["schemaInvalidEvents"]=2;f["nativeSubscriptionHealth"]="healthy";auto r=compareNativeAppLifetime(x,123);
        expect(r["schemaInvalidEvents"]==2,"actual schema rejection count projected");expect(r["rawCallbacksEntered"]==5&&r["accountUnavailableCallbacks"]==2&&r["accountMismatchCallbacks"]==1,"callback entry/drop counters projected");
        expect(r["observerInstanceId"]=="watch-a"&&r["observerCreatedAtUnixMs"]==1234,"local watch identity retained");expect(r["diagnosticCountersAvailable"]==true,"complete counters recognized");
        expect(r["nativeSubscriptionHealth"]=="unknown"&&r["originalAppRunningKnown"]==false&&r["gameInputVerified"]==false&&r["continueVerified"]==false,"counters and claimed health cannot promote native health or game state");}
    {auto x=base();auto r=compareNativeAppLifetime(x,123);expect(r["schemaInvalidEvents"].is_null()&&r["rawCallbacksEntered"].is_null()&&r["diagnosticCountersAvailable"]==false,"legacy missing diagnostics remain unknown");}
    {auto x=base();auto& f=x["nativeAppLifetimeFeedback"];f["diagnosticCountersAvailable"]=true;f["rawCallbacksEntered"]=-1;f["accountUnavailableCallbacks"]=0;f["accountMismatchCallbacks"]=0;f["schemaInvalidEvents"]=0;f["events"]=LifetimeJson::array({event(123,"77",true,1)});auto r=compareNativeAppLifetime(x,123);
        expect(r["diagnosticCountersAvailable"]==false&&r["rawCallbacksEntered"].is_null(),"invalid diagnostics remain unknown");expect(r["available"]==true&&r["originalAppRunningKnown"]==true&&r["originalAppRunning"]==true,"invalid diagnostics do not discard a valid lifecycle event");}
    std::cout<<"steam-app-lifetime-comparison assertions="<<count<<" passed; no Steam/OS/device actions\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}


