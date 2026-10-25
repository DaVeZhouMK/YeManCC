#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_download_supervisor.h"
#include "../native/shared_game_media_download_worker.h"
#include <iostream>
using namespace ymcc::media;using namespace ymcc::media::downloads;
static int checks=0;static void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
static Json target(int id,int64_t played=0,int tier=1,const char* source="steam-native"){return{{"appId",id},{"name","Fixture "+std::to_string(id)},{"verified",true},{"lastPlayed",played},{"sourceTier",tier},{"catalogSource",source}};}
static std::string claimKey(Queue& q,const std::string& token){auto row=q.claim(token);check(row.is_object(),"expected queued claim");return row.value("key",std::string{});}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("fresh isolated root required");const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("fresh root required");fs::create_directories(root);
 check(Limits{}.recheck==20000,"recovery is 20 seconds");check(Limits{}.startup==30000,"startup remains protected for 30 seconds");
 {
  Queue q(root/L"order");Json targets=Json::array();for(int id=1;id<=12;++id)targets.push_back(target(id,id*10,id%2?1:2,id%2?"steam-native":"ymcc-confirmed"));targets.push_back(target(570,99999,0,"popular"));q.enqueue(targets);
  check(!q.document()["jobs"].contains("steam-570"),"popular feed cannot enqueue work");check(q.document()["maxParallel"]==10,"ten slots are saved");auto token=q.start();Json claims=Json::array();
  for(int n=0;n<10;++n){auto row=q.claim(token);check(row.value("appId",0)==12-n,"recent order ignores native/confirmed source");claims.push_back(row);}
  check(q.snapshot()["activeCount"]==10,"ten independent items admitted");check(q.claim(token).is_null(),"eleventh claim cannot exceed cap");q.result(token,claims[0]["key"],true);q.enqueue(Json::array({target(99,9999,2,"ymcc-confirmed")}));check(q.claim(token).value("appId",0)==99,"new recent game takes freed slot without stopping other work");check(q.snapshot()["activeCount"]==10,"other active transfers preserved");q.end(token,"completed");
 }
 {
  Queue q(root/L"retire");atomicWrite(q.root()/L"config"/L"game-media-downloads.json",{{"enabled",true},{"maxParallel",5},{"catalogPending",true},{"catalogStatus","fetching"},{"jobs",{{"steam-570",target(570,999,0,"popular")},{"steam-730",{{"appId",730},{"verified",true},{"catalogSource","popular"},{"sourceTier",0},{"localSourceTier",2},{"localCatalogSource","ymcc-confirmed"}}},{"steam-620",{{"appId",620},{"verified",true},{"catalogSource","popular"},{"sourceTier",0},{"localSourceTier",1},{"localCatalogSource","steam-native"}}},{"steam-1",target(1,100)}}}});
  q.configure(true,"");auto d=q.document();check(d["jobs"].size()==2,"old popular-only and cache-only jobs removed");check(d["jobs"]["steam-620"]["catalogSource"]=="steam-native","real local overlap retained");check(d["maxParallel"]==10&&!d["catalogPending"].get<bool>(),"legacy cap and catalog migrated");auto stamp=fs::last_write_time(q.root()/L"config"/L"game-media-downloads.json");q.requestCatalog();q.enqueueCatalog(Json::array({target(730,999,0,"popular")}));check(fs::last_write_time(q.root()/L"config"/L"game-media-downloads.json")==stamp,"legacy feed entry points cause no write or discovery");check(q.snapshot().value("catalogStatus",std::string{})=="disabled","new UI sees retired feed");
 }
 {
  Queue q(root/L"retries");q.enqueue(Json::array({target(101,100)}));auto token=q.start();auto key=claimKey(q,token);q.result(token,key,false,"media-http-503");auto d=q.document();check(d["jobs"][key]["retryAt"].get<int64_t>()>=now()+19000&&d["jobs"][key]["retryAt"].get<int64_t>()<=now()+21000,"first transient failure retries near 20sec");
  for(int n=1;n<3;++n){q.update(token,[&](Json& j){j["jobs"][key]["retryAt"]=0;});claimKey(q,token);q.result(token,key,false,"media-http-503");}
  d=q.document();check(d["jobs"][key].value("autoStopped",false),"third failed attempt stops only that game");check(d["jobs"][key]["state"]=="unavailable","exhausted game is a skipped outcome");check(q.snapshot()["remaining"]==0&&q.snapshot()["autoStopped"]==1,"skipped game is not retained work");check(q.delay()<0,"no wake timer for exhausted tail");q.end(token,"completed");q.enqueue(Json::array({target(101,200)}));check(q.document()["jobs"][key].value("autoStopped",false),"automatic rescan does not reset budget");check(Queue(q.root()).document()["jobs"][key].value("autoStopped",false),"restart preserves exhausted state");q.enqueue(Json::array({target(202,300)}));token=q.start();check(!token.empty()&&q.claim(token).value("appId",0)==202,"later game starts despite failed tail");q.result(token,"steam-202",true);q.end(token,"completed");
  q.requestSupplement(true,key);token=q.start();check(!token.empty(),"explicit retry rearms skipped target");auto row=q.claim(token);check(row.value("appId",0)==101&&row.value("attempts",-1)==0&&!row.value("autoStopped",true),"manual target gets a fresh finite budget");q.end(token,"completed");
 }
 {
  Queue q(root/L"budget");q.enqueue(Json::array({target(301,100)}));auto token=q.start();std::string key;
  for(int n=0;n<2;++n){if(n)q.update(token,[&](Json& j){j["jobs"][key]["retryAt"]=0;});key=claimKey(q,token);q.update(token,[&](Json& j){j["active"][key]["taskStartedAt"]=now()-automaticAttemptMs;});q.result(token,key,false,"media-task-time-budget");}
  auto job=q.document()["jobs"][key];check(job.value("autoStopped",false)&&job["attempts"]==2,"cumulative time budget stops before third attempt");check(job["automaticElapsedMs"]==automaticGameMs,"six minutes is the cumulative active budget");check(q.delay()<0,"time budget exhaustion leaves no retry timer");q.end(token,"completed");
 }
 {
  Queue q(root/L"isolated-watchdog");q.enqueue(Json::array({target(401,200),target(402,100)}));auto token=q.start();auto failed=claimKey(q,token),healthy=claimKey(q,token);q.recover(token,"worker-stalled",failed);auto d=q.document();check(d["jobs"][failed]["attempts"]==1,"watchdog charges failed lane");check(d["jobs"][healthy]["attempts"]==0&&d["jobs"][healthy]["state"]=="pending","healthy interrupted lane retains budget");
 }
 {
  Queue q(root/L"no-material");q.enqueue(Json::array({target(501,100)}));auto token=q.start();auto key=claimKey(q,token);q.result(token,key,true,"no-dynamic-video");check(q.document()["jobs"][key].value("autoStopped",false),"confirmed no-material game stops automatic attempts");check(q.delay()<0,"no daily rescan for absent video");q.enqueue(Json::array({target(501,999)}));check(q.document()["jobs"][key].value("autoStopped",false),"recency does not undo no-material stop");q.end(token,"completed");
 }
 {
  Queue q(root/L"timestamps");q.enqueue(Json::array({target(601,999999)}));q.enqueue(Json::array({target(601,12)}));check(q.document()["jobs"]["steam-601"]["lastPlayed"]==12,"fresh Steam play timestamp replaces legacy viewed timestamp");
  Store store(root/L"store-backoff");auto a=target(701);a["demandConsumer"]="background";auto lease=store.begin(a);check(lease.value("acquired",false),"test lease acquired");lease["reason"]="media-http-503";auto failed=store.fail(lease);check(failed.value("retryAt",int64_t{0})>=now()+19000&&failed.value("retryAt",int64_t{0})<=now()+21000,"store and queue share the 20sec background retry");
 }
 Json result={{"ok",true},{"checks",checks},{"scope","Production Queue/Store; recent scheduling, migration, ten claims, finite item budgets, future arrivals, manual rearm and watchdog isolation; no real Steam/device/network changes"}};atomicWrite(root/L"result.json",result);std::cout<<result.dump()<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}}
