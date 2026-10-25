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
 bool operator==(const Context&)const=default;
};
struct Decision {bool on=false;const char* scene="unknown";const char* reason="foreground_unconfirmed";};
inline Decision decide(Mode m,const Context& c){
 if(c.steam)return {false,"steam","steamdeck_managed"};
 if(m==Mode::Off)return {false,"off","manual_off"};
 if(m==Mode::On)return {true,"manual","manual_on"};
 if(c.ymcc)return {false,"ymcc","ymcc_foreground_paused"};
 if(!c.window)return {false,"unknown","foreground_unconfirmed"};
 if(c.desktop)return {true,"desktop","desktop_foreground"};
 return {false,"external","non_desktop_foreground"};
}
struct Deps {
 std::function<Context()> observe;
 std::function<Json(bool,const std::function<bool()>&)> apply;
 // A release-only boundary also works during SD transition, never enables Steam.
 std::function<Json()> release;
 std::function<void(const Json&)> publish;
};
class Runtime {
 Deps deps;std::mutex mx;std::condition_variable cv;std::thread worker;
 std::atomic<bool> stopping{false};std::atomic<Mode> selected{Mode::Off};std::atomic<std::uint64_t> revision{1};
 std::atomic<bool> forced{false};bool dirty=true;std::uint64_t settled=0,sequence=0;Json last=Json::object(),history=Json::array();
 static std::uint64_t wall(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
 void run(){
  Context previous{};Mode priorMode=Mode::Off;bool first=true;
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
   if(!forced.exchange(false)&&!first&&context==previous&&m==priorMode){std::lock_guard lock(mx);settled=rev;cv.notify_all();continue;}
   first=false;previous=context;priorMode=m;
   // Only enable after the same desktop identity remains stable. Off is immediate.
   const auto d=decide(m,context);
   if(d.on&&m==Mode::Auto){std::unique_lock lock(mx);if(cv.wait_for(lock,std::chrono::milliseconds(180),[&]{return stopping.load()||revision.load()!=rev;})){first=true;continue;}}
   auto valid=[this,rev,m,context]{return !stopping.load()&&revision.load()==rev&&selected.load()==m&&(m==Mode::On?!deps.observe().steam:deps.observe()==context)&&decide(m,context).on;};
   if(d.on&&!valid()){first=true;wake();continue;}
   Json result;
   try {result=context.steam?deps.release():deps.apply(d.on,valid);}catch(const std::exception& e){result={{"ok",false},{"on",false},{"stateKnown",false},{"error",e.what()}};}
   // Re-observe after bounded provider IO. A late enable is withdrawn immediately.
   if(d.on&&!valid()){
    try{const auto withdrawn=deps.release();if(!withdrawn.value("ok",false)){result=withdrawn;result["ok"]=false;result["reason"]="stale_enable_cleanup_failed";publish(result,m,context,rev,d);}}catch(...){}
    previous={};first=true;wake();continue;
   }
   if(revision.load()!=rev){first=true;wake();continue;}
   publish(result,m,context,rev,d);
  }
 }
 void publish(Json result,Mode m,const Context& context,std::uint64_t rev,const Decision& d){
  result["mode"]=name(m);result["scene"]=d.scene;result["policyReason"]=d.reason;result["requestedOn"]=d.on;result["foregroundPid"]=context.pid;
  result["foregroundCreated"]=std::to_string(context.created);result["foregroundWindow"]=std::to_string(context.window);result["pending"]=false;
  result["foregroundImage"]=context.foregroundImage;result["foregroundClass"]=context.foregroundClass;
  result["foregroundTransientGraceMs"]=ForegroundTransientGraceMs;
  result["observedAt"]=wall();result["sequence"]=++sequence;
  if(context.steam){result["provider"]="steam";result["controlAllowed"]=false;result["stateKnown"]=false;result["verification"]="unobserved";result["mappingVerified"]=false;}
  {std::lock_guard lock(mx);last=result;settled=rev;history.push_back({{"at",result["observedAt"]},{"mode",name(m)},{"scene",d.scene},{"reason",d.reason},{"ok",result.value("ok",false)},{"on",result.value("on",false)},{"sequence",sequence}});if(history.size()>64)history.erase(history.begin());cv.notify_all();}
  if(deps.publish)deps.publish(result);
 }
public:
 ~Runtime(){stop();}
 void start(Deps value,Mode initial){if(worker.joinable())return;deps=std::move(value);selected.store(initial);stopping.store(false);worker=std::thread([this]{run();});}
 void wake(){revision.fetch_add(1);{std::lock_guard lock(mx);dirty=true;}cv.notify_all();}
 Json set(Mode m){
  std::uint64_t wanted=0;
  {std::lock_guard lock(mx);selected.store(m);forced.store(true);wanted=revision.fetch_add(1)+1;dirty=true;}
  cv.notify_all();std::unique_lock lock(mx);
  cv.wait_for(lock,std::chrono::seconds(7),[&]{return settled>=wanted||stopping.load();});
  auto out=last;if(out.empty())out={{"ok",true},{"on",false},{"stateKnown",false}};
  // A superseded caller receives the current authority, never its old mode
  // relabelled onto a newer backend receipt.
  out["mode"]=name(selected.load());out["pending"]=settled<revision.load();
  if(selected.load()!=m)out["superseded"]=true;
  return out;
 }
 Json state(){std::lock_guard lock(mx);auto out=last;out["mode"]=name(selected.load());out["pending"]=settled<revision.load();return out;}
 Json records(){std::lock_guard lock(mx);return history;}
 Mode current()const{return selected.load();}
 void stop(){stopping.store(true);cv.notify_all();if(worker.joinable())worker.join();}
};
}
