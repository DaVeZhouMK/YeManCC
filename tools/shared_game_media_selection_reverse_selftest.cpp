#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "shared_game_media_download_supervisor.h"
#include "custom-steam-library/custom_steam_library_video_catalog.h"
#include <iostream>
using namespace ymcc::media;using namespace ymcc::media::downloads;
static int checks=0;static void check(bool result,const char* label){++checks;if(!result)throw std::runtime_error(label);}
static bool rejected(const std::function<void()>& fn){try{fn();return false;}catch(const std::exception&){return true;}}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("fresh evidence root required");const auto base=fs::absolute(argv[1]);if(fs::exists(base))throw std::runtime_error("fresh root required");fs::create_directories(base);
 const std::string url="https://video.steamstatic.com/store_trailers/570/a/master.m3u8";
 Json entry={{"key","steam-570"},{"appId",570},{"identityVerified",true},{"bindingRevision",9}};
 Json catalog={{"entryKey","steam-570"},{"appId",570},{"bindingRevision",9},{"createdAt",now()},{"searchToken","proof"},{"candidates",Json::array({{{"id","0"},{"url",url},{"title","chosen"}}})}};
 Json args={{"bindingRevision",9},{"searchToken","proof"},{"candidateId","0"}};
 const auto select=[&](const Json& e,const Json& c,const Json& a){return custom_steam_library::video_catalog::selection(e,c,a,now());};
 check(select(entry,catalog,args)["url"]==url,"same game/revision/catalog authorizes chosen video");
 check(select(entry,catalog,args).value("preferredHeight",0)==720,"manual candidate defaults to 720p");
 auto hd=args;hd["preferredHeight"]=1080;check(select(entry,catalog,hd).value("preferredHeight",0)==1080,"manual full HD preference survives the authenticated candidate selection");
 hd["preferredHeight"]=2160;check(rejected([&]{select(entry,catalog,hd);}),"unsupported requested quality is explicit rather than silently changing profile");
 auto direct=catalog;direct["candidates"][0]["url"]="https://video.steamstatic.com/movie480.mp4";direct["candidates"][0]["sources"]={{"480","https://video.steamstatic.com/movie480.mp4"},{"720","https://video.steamstatic.com/movie720.mp4"},{"max","https://video.steamstatic.com/movie_max.mp4"}};
 check(select(entry,direct,args)["url"]=="https://video.steamstatic.com/movie720.mp4","manual default direct source chooses 720p");hd=args;hd["preferredHeight"]=1080;check(select(entry,direct,hd)["url"]=="https://video.steamstatic.com/movie_max.mp4","explicit manual full HD chooses the catalog max source");

 auto changed=entry;changed["bindingRevision"]=10;check(rejected([&]{select(changed,catalog,args);}),"external wallpaper edit fences an old search result");
 auto old=args;old["bindingRevision"]=8;check(rejected([&]{select(entry,catalog,old);}),"client cannot select with an older entry revision");
 old=args;old.erase("bindingRevision");check(rejected([&]{select(entry,catalog,old);}),"unversioned candidate is not accepted");
 old=catalog;old["entryKey"]="steam-620";check(rejected([&]{select(entry,old,args);}),"catalog cannot move to another game");
 old=catalog;old["appId"]=620;check(rejected([&]{select(entry,old,args);}),"store identity must still match");
 old=args;old["searchToken"]="stale";check(rejected([&]{select(entry,catalog,old);}),"replaced search token rejected");
 old=catalog;old["createdAt"]=now()-600001;check(rejected([&]{select(entry,old,args);}),"expired catalog rejected");
 old=catalog;old["createdAt"]=now()+300000;check(rejected([&]{select(entry,old,args);}),"clock rollback cannot extend catalog validity");
 old=catalog;old["candidates"][0]["url"]="https://example.invalid/not-official.mp4";check(rejected([&]{select(entry,old,args);}),"candidate source is rechecked before persistence");
 old=args;old["candidateId"]="absent";check(rejected([&]{select(entry,catalog,old);}),"unknown candidate rejected");
 Json target={{"appId",570},{"verified",true},{"sourceTier",3},{"name","chosen"}};
 Json other={{"appId",620},{"verified",true},{"sourceTier",0},{"chartRank",1},{"catalogSource","popular"},{"name","popular"}};
 Queue queue(base/L"scoped");queue.enqueue(Json::array({target,other}));queue.configure(false,"game-running");queue.requestCatalog();queue.requestSupplement(true,"steam-570");
 const auto initial=queue.document();check(scopedManual(initial),"candidate creates scoped one-time request");
 check(!initial.contains("inventoryRequestId")&&initial.value("inventoryAwaitUntil",int64_t{0})==0,"known selected target does not start full inventory scan or ten second wait");
 check(queue.snapshot().value("manualTotal",0)==1&&queue.snapshot().value("manualCompleted",-1)==0,"scoped progress counts the chosen target only");
 const auto deadline=initial["manualUntil"];const auto request=initial["manualRequestId"];queue.requestSupplement(true,"steam-570");check(queue.document()["manualUntil"]==deadline&&queue.document()["manualRequestId"]==request,"duplicate selected request is inert and does not extend ownership");
 const auto token=queue.start();check(!token.empty(),"explicit selected target runs with auto off and game soft pause");check(!queue.catalogDue(token),"selected video skips unrelated popular-catalog network work");
 const auto claimed=queue.claim(token);check(claimed.value("appId",0)==570,"chosen video runs before popular backlog");check(queue.claim(token).is_null(),"unrelated backlog never fills scoped lanes");
 queue.result(token,"steam-570",true);check(manualTargetsComplete(queue.document()),"one chosen completion ends the scoped batch");check(queue.snapshot().value("manualCompleted",0)==1,"scoped completion count is returned to both UIs");queue.end(token,"completed");
 check(!manualRun(queue.snapshot())&&queue.snapshot()["enabled"]==false,"one selected completion restores automatic-off state");check(queue.document()["jobs"]["steam-620"]["state"]=="pending","popular backlog stays untouched");check(queue.delay()==-1,"auto-off selected completion has no parked restart timer");
 Queue race(base/L"late");race.enqueue(Json::array({target,other}));race.configure(false,"");race.requestSupplement(true,"steam-570");const auto first=race.start();race.claim(first);race.result(first,"steam-570",true);
 race.requestSupplement(true,"steam-620");check(race.document()["manualUntil"]>now(),"second choice joins bounded request without extending deadline");race.end(first,"completed");check(manualRun(race.snapshot()),"old worker cannot clear a newly accepted choice at completion boundary");
 const auto second=race.start();check(race.claim(second).value("appId",0)==620,"late selected target gets its own next claim");race.result(second,"steam-620",true);race.end(second,"completed");check(!manualRun(race.snapshot()),"last selected completion releases scoped request");
 Queue battery(base/L"battery");battery.enqueue(Json::array({target}));battery.configure(false,"on-battery");battery.requestSupplement(true,"steam-570");check(battery.start().empty(),"scoped video selection preserves power protection");
 Queue bulk(base/L"bulk");bulk.enqueue(Json::array({target,other}));bulk.configure(false,"");bulk.requestSupplement(true);bulk.inventoryReady(bulk.snapshot().value("inventoryRequestId",std::string{}));bulk.requestSupplement(true,"steam-570");check(!scopedManual(bulk.document()),"selection within an explicit bulk request preserves bulk scope");const auto b=bulk.start();check(bulk.claim(b).value("appId",0)==570,"selection is preferred within a bulk request");bulk.result(b,"steam-570",true);check(bulk.claim(b).value("appId",0)==620,"bulk request retains other intended work");bulk.result(b,"steam-620",true);bulk.catalogResult(b,true);bulk.end(b,"completed");check(!manualRun(bulk.snapshot()),"bulk completion clears ownership even after accepting a selected priority");
 Store store(base/L"store");auto saved=store.registerEntry(target);saved["action"]="select-video";saved["selectedVideo"]={{"appId",570},{"url",url}};saved=store.edit(saved);
 const auto sample=base/L"sample.mp4";{std::ofstream out(sample,std::ios::binary);out.write("\0\0\0\x18" "ftypisom",12);}saved=store.publish(saved,sample,true);check(!saved.contains("selectedVideo"),"local imported video clears an obsolete online selection");
 saved["action"]="select-video";saved["selectedVideo"]={{"appId",570},{"url",url}};saved=store.edit(saved);saved["action"]="delete";saved["slot"]="dynamic";saved=store.edit(saved);check(!saved.contains("selectedVideo"),"deleting dynamic slot clears an obsolete choice");
 const auto legacy=base/L"legacy";fs::create_directories(legacy);fs::copy_file(fs::absolute(argv[0]),legacy/L"CustomSteamLibrary.exe");atomicWrite(legacy/L"package-manifest.json",{{"backgroundMediaProtocol",1},{"backgroundCatalogProtocol",1},{"backgroundIdentityProtocol",1},{"backgroundVideoQualityPolicy",2},{"backgroundManualProtocol",1}});Queue oldQueue(legacy);oldQueue.enqueue(Json::array({target}));oldQueue.requestSupplement(true,"steam-570");
 {Controller owner(legacy,legacy/L"CustomSteamLibrary.exe",[](const Json&){},Limits{5000,10000,20000,50,0},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};}});owner.background({{"enabled",false},{"pauseReason",""}});const auto start=GetTickCount64();while(GetTickCount64()-start<3000&&oldQueue.snapshot().value("reason",std::string{})!="download-video-selection-worker-upgrade-required")Sleep(20);check(oldQueue.snapshot().value("reason",std::string{})=="download-video-selection-worker-upgrade-required","old manual-capable child is not allowed to expand a scoped selection to full batch");check(oldQueue.snapshot().value("workerPid",DWORD{0})==0,"incompatible child remains unlaunched");}
 std::cout<<Json({{"ok",true},{"checks",checks},{"scope","production catalog selection, durable Queue/Controller and real isolated child refusal; no live cache, Steam, input or network"}}).dump()<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 2;}}
