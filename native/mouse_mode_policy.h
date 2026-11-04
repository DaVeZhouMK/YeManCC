#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>
#include "json.hpp"
namespace ymcc::mousePolicy {
using Json=nlohmann::json;
enum class Mode { Off, Auto, On };
inline constexpr int ForegroundTransientGraceMs=250;
inline const char* name(Mode m){return m==Mode::Auto?"auto":m==Mode::On?"on":"off";}
inline Mode mode(const std::string& s){return s=="auto"?Mode::Auto:s=="on"?Mode::On:Mode::Off;}
struct Context {
 std::uintptr_t window=0;std::uint32_t pid=0;std::uint64_t created=0;bool desktop=false,ymcc=false,steam=false;
 std::string foregroundImage,foregroundClass;
 bool identityKnown=false,classificationKnown=false,game=false,emulator=false,gameLibrary=false;
 std::uint64_t rulesEpoch=0;std::uint32_t gameTargetPid=0;
 std::string applicationCategory,gameObservation;
 int customRule=0;bool customRulesKnown=true;std::uint64_t customRulesEpoch=0;
 bool operator==(const Context&)const=default;
};
struct Decision {bool on=false;const char* scene="unknown";const char* reason="foreground_unconfirmed";};
inline Decision decide(Mode m,const Context& c){
 if(c.steam)return {false,"steam","steamdeck_managed"};
 if(m==Mode::Off)return {false,"off","manual_off"};
 if(m==Mode::On)return {true,"manual","manual_on"};
 if(c.ymcc)return {false,"ymcc","ymcc_foreground_paused"};
 if(!c.window)return {false,"unknown","foreground_unconfirmed"};
 if(!c.customRulesKnown)return {false,"unknown","mouse_rules_unavailable"};
 if(c.customRule<0)return {false,"blacklist","mouse_blacklist_foreground"};
 if(c.customRule>0)return {true,"desktop","mouse_whitelist_foreground"};
 if(c.emulator)return {false,"emulator","emulator_foreground"};
 if(c.gameLibrary)return {false,"game-library","game_library_foreground"};
 if(c.game)return {false,"game","game_foreground"};
 if(c.desktop)return {true,"desktop","desktop_foreground"};
 // Ordinary foreground apps use default allow. A pending/missing shared game
 // observation is diagnostic state, not an exclusion rule for mouse input.
 if(c.identityKnown&&!c.foregroundImage.empty())return {true,"desktop","non_game_foreground"};
 return {false,"unknown","foreground_classification_unconfirmed"};
}
struct Deps {
 std::function<Context()> observe;
 std::function<Json(bool,const std::function<bool()>&)> apply;
 // A release-only boundary also works during SD transition, never enables Steam.
 std::function<Json()> release;
 std::function<void(const Json&)> publish;
 // Reuse a confirmed state while its provider and effective decision are unchanged.
 // Explicit requests, Steam handoffs and failed/unknown reads still use IO.
 std::function<bool(const Json&)> retainConfirmedState;
};
class Runtime {
 Deps deps;std::mutex mx;std::condition_variable cv;std::thread worker;
 std::atomic<bool> stopping{false};std::atomic<Mode> selected{Mode::Off};std::atomic<std::uint64_t> revision{1};
 std::atomic<std::uint64_t> backendEpoch{1};
 std::atomic<bool> forced{false};bool dirty=true;std::uint64_t settled=0,sequence=0;Json last=Json::object(),history=Json::array();
 static std::uint64_t wall(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
 static bool confirmedReceipt(const Json& value){
  return value.value("ok",false)&&value.value("stateKnown",false)&&
    value.contains("on")&&value["on"].is_boolean()&&value.value("controlAllowed",false);
 }
 void run(){
  Context previous{};Mode priorMode=Mode::Off;bool first=true;Json confirmedState=Json::object();std::uint64_t confirmedStateEpoch=0;
  std::chrono::steady_clock::time_point unknownSince{};
  while(!stopping.load()){
   {std::unique_lock lock(mx);cv.wait_for(lock,std::chrono::seconds(1),[&]{return dirty||stopping.load();});dirty=false;}
   if(stopping.load())break;
   const auto rev=revision.load();const Mode m=selected.load();const auto context=deps.observe();
   // A foreground HWND can temporarily be NULL during activation. Retain only
   // the last admitted state for a bounded transition; this never enables from
   // an unknown foreground. Real windows, explicit OFF and SD bypass the hold.
   if(m==Mode::Auto&&!context.window&&!context.steam&&!first){
    const auto now=std::chrono::steady_clock::now();
    if(unknownSince==std::chrono::steady_clock::time_point{})unknownSince=now;
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-unknownSince).count();
    if(elapsed<ForegroundTransientGraceMs){
     std::unique_lock lock(mx);cv.wait_for(lock,std::chrono::milliseconds(ForegroundTransientGraceMs-elapsed),[&]{return stopping.load()||revision.load()!=rev;});
     dirty=true;continue;
    }
   }else unknownSince={};
   // A foreground read replaces cached evidence. Reconcile a changed or unknown
   // provider state; the read itself never becomes periodic backend probing.
   if(backendEpoch.load()!=confirmedStateEpoch){
    std::lock_guard lock(mx);confirmedStateEpoch=backendEpoch.load();
    const bool changed=!confirmedState.empty()&&(!confirmedReceipt(last)||
      confirmedState.value("on",false)!=last.value("on",false)||
      confirmedState.value("backend",std::string{})!=last.value("backend",std::string{}));
    confirmedState=confirmedReceipt(last)?last:Json::object();
    if(changed)first=true;
   }
   const bool refresh=forced.exchange(false);
   if(!refresh&&!first&&context==previous&&m==priorMode){std::lock_guard lock(mx);settled=rev;cv.notify_all();continue;}
   first=false;previous=context;priorMode=m;
   // Only enable after the same desktop identity remains stable. Off is immediate.
   const auto d=decide(m,context);
   // Window and diagnostic changes do not require another provider command
   // when the admitted ON/OFF decision and provider are unchanged.
   if(!refresh&&!context.steam&&!confirmedState.empty()&&
      confirmedState.value("on",false)==d.on&&
      deps.retainConfirmedState&&deps.retainConfirmedState(confirmedState)){
    if(revision.load()!=rev){first=true;wake();continue;}
    auto retained=confirmedState;retained["backendReadbackFresh"]=false;
    if(!publish(retained,m,context,rev,d,confirmedStateEpoch,false)){first=true;wake();}continue;
   }
   confirmedState=Json::object();
   if(d.on&&m==Mode::Auto){std::unique_lock lock(mx);if(cv.wait_for(lock,std::chrono::milliseconds(180),[&]{return stopping.load()||revision.load()!=rev;})){first=true;continue;}}
   auto valid=[this,rev,m,context]{return !stopping.load()&&revision.load()==rev&&selected.load()==m&&(m==Mode::On?!deps.observe().steam:deps.observe()==context)&&decide(m,context).on;};
   if(d.on&&!valid()){first=true;wake();continue;}
   Json result;const auto readEpoch=backendEpoch.load();
   try {result=context.steam?deps.release():deps.apply(d.on,valid);}catch(const std::exception& e){result={{"ok",false},{"on",false},{"stateKnown",false},{"error",e.what()}};}
   // Re-observe after bounded provider IO. A late enable is withdrawn immediately.
   if(d.on&&!valid()){
    try{const auto withdrawn=deps.release();if(!withdrawn.value("ok",false)){result=withdrawn;result["ok"]=false;result["reason"]="stale_enable_cleanup_failed";publish(result,m,context,rev,d,readEpoch);}}catch(...){}
    previous={};first=true;wake();continue;
   }
   if(revision.load()!=rev){first=true;wake();continue;}
   const auto publishedEpoch=publish(result,m,context,rev,d,readEpoch);
   if(!publishedEpoch){first=true;wake();continue;}
   // Synchronize the generation for every settled provider result, including
   // ON, Steam and failures: unchanged contexts must never become a retry timer.
   {std::lock_guard lock(mx);if(backendEpoch.load()==publishedEpoch){
    confirmedStateEpoch=publishedEpoch;
    confirmedState=!context.steam&&confirmedReceipt(result)?last:Json::object();
   }}
  }
 }
 std::uint64_t publish(Json result,Mode m,const Context& context,std::uint64_t rev,const Decision& d,std::uint64_t expectedEpoch,bool fresh=true){
  if(fresh){result["backendReadbackFresh"]=true;result["backendObservedAt"]=wall();}
  result["mode"]=name(m);result["scene"]=d.scene;result["policyReason"]=d.reason;result["requestedOn"]=d.on;result["foregroundPid"]=context.pid;
  result["foregroundCreated"]=std::to_string(context.created);result["foregroundWindow"]=std::to_string(context.window);result["pending"]=false;
  result["foregroundImage"]=context.foregroundImage;result["foregroundClass"]=context.foregroundClass;
  result["foregroundTransientGraceMs"]=ForegroundTransientGraceMs;
  result["foregroundCategory"]=context.applicationCategory;result["gameObservation"]=context.gameObservation;
  result["classificationRulesEpoch"]=context.rulesEpoch;result["gameTargetPid"]=context.gameTargetPid;
  result["observedAt"]=wall();
  if(context.steam){result["provider"]="steam";result["controlAllowed"]=false;result["stateKnown"]=false;result["verification"]="unobserved";result["mappingVerified"]=false;}
  std::uint64_t publishedEpoch=0;
  {std::lock_guard lock(mx);if(backendEpoch.load()!=expectedEpoch)return 0;
   if(revision.load()!=rev||selected.load()!=m)return 0;
   if(fresh)backendEpoch.fetch_add(1);publishedEpoch=backendEpoch.load();result["sequence"]=++sequence;last=result;settled=rev;history.push_back({{"at",result["observedAt"]},{"mode",name(m)},{"scene",d.scene},{"reason",d.reason},{"ok",result.value("ok",false)},{"on",result.value("on",false)},{"sequence",sequence}});if(history.size()>64)history.erase(history.begin());cv.notify_all();}
  if(deps.publish)deps.publish(result);
  return publishedEpoch;
 }
public:
 ~Runtime(){stop();}
 void start(Deps value,Mode initial){if(worker.joinable())return;deps=std::move(value);selected.store(initial);stopping.store(false);worker=std::thread([this]{run();});}
 void wake(){revision.fetch_add(1);{std::lock_guard lock(mx);dirty=true;}cv.notify_all();}
 // Admission is short and can share the settings transaction; waiting never
 // holds that external lock or delays a newer mode request.
 std::uint64_t request(Mode m){
  std::uint64_t wanted=0;
  {std::lock_guard lock(mx);selected.store(m);forced.store(true);wanted=revision.fetch_add(1)+1;dirty=true;}
  cv.notify_all();return wanted;
 }
 Json wait(Mode m,std::uint64_t wanted){
  std::unique_lock lock(mx);
  cv.wait_for(lock,std::chrono::seconds(7),[&]{return settled>=wanted||stopping.load();});
  auto out=last;if(out.empty())out={{"ok",true},{"on",false},{"stateKnown",false}};
  // A superseded caller receives the current authority, never its old mode
  // relabelled onto a newer backend receipt.
  out["mode"]=name(selected.load());out["pending"]=settled<revision.load();
  if(selected.load()!=m)out["superseded"]=true;
  return out;
 }
 Json set(Mode m){return wait(m,request(m));}
 Json refresh(){
  Mode m;std::uint64_t wanted=0;
  {std::lock_guard lock(mx);m=selected.load();forced.store(true);wanted=revision.fetch_add(1)+1;dirty=true;}
  cv.notify_all();return wait(m,wanted);
 }
 // The caller holds the provider operation mutex across its read and this
 // admission. An older in-flight worker result is fenced by backendEpoch;
 // a queued old event keeps a lower sequence than this foreground read.
 Json acceptBackendReadback(Json observed){
  std::lock_guard lock(mx);
  for(const auto* key:{"mode","scene","policyReason","requestedOn","pending","observedAt","foregroundPid","foregroundCreated","foregroundWindow","foregroundImage","foregroundClass","foregroundTransientGraceMs","foregroundCategory","gameObservation","classificationRulesEpoch","gameTargetPid"})
   if(last.contains(key))observed[key]=last[key];
  if(last.contains("ok")&&!last.value("ok",true)){
   observed["ok"]=false;for(const auto* key:{"error","reason"})if(last.contains(key))observed[key]=last[key];
  }
  observed["mode"]=name(selected.load());observed["pending"]=settled<revision.load();
  observed["backendReadbackFresh"]=true;observed["backendObservedAt"]=wall();observed["sequence"]=++sequence;
  backendEpoch.fetch_add(1);last=observed;return observed;
 }
 Json state(){std::lock_guard lock(mx);auto out=last;out["mode"]=name(selected.load());out["pending"]=settled<revision.load();return out;}
 Json records(){std::lock_guard lock(mx);return history;}
 Mode current()const{return selected.load();}
 void stop(){stopping.store(true);cv.notify_all();if(worker.joinable())worker.join();}
};
}
