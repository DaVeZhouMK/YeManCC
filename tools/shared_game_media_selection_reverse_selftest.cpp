#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "shared_game_media_download_supervisor.h"
#include "custom-steam-library/custom_steam_library_video_catalog.h"
#include "shared_game_media_video_request.h"
#include "custom-steam-library/custom_steam_library_network_policy.h"
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

 // The reported game has an old EXE identity cache, yet its current media
 // entry and official candidate catalog already agree on this Steam AppID.
 const std::string nobuExe="D:/Game/NOBU16/NOBU16.exe";
 const auto identityFile=[](const fs::path& root,const std::string& exe){auto segment=normalized(exe);for(auto& c:segment)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_')c='_';return root/L"cache"/L"identity"/wide(segment+".json");};
 const auto makeRequest=[&](const fs::path& root){
   Store media(root);auto file=media.registerEntry({{"executable",nobuExe}});
   auto bound=media.registerEntry({{"appId",1336980},{"verified",true},{"executable",nobuExe},{"name","NOBUNAGA'S AMBITION: Awakening"}});
   const auto sample=root/L"old.jpg";{std::ofstream out(sample,std::ios::binary);out.write("\xff\xd8\xff\xe0",4);}bound=media.publish(bound,sample,true);
   bound["action"]="mode";bound["mode"]="dynamic";bound=media.edit(bound);
   atomicWrite(identityFile(root,nobuExe),{{"executable",nobuExe},{"steamAppId",1336980},{"formalName","NOBUNAGA'S AMBITION: Awakening"},{"manifestPath","missing-old-manifest.json"}});
   Json result={{"entryKey",bound["key"]},{"appId",1336980},{"bindingRevision",bound["bindingRevision"]},{"createdAt",now()},{"searchToken","nobu-official-catalog"},{"candidates",Json::array({{{"id","0"},{"url","https://video.akamai.steamstatic.com/store_trailers/1336980/580738/761be36be4793f63f54cb34deb2e8252ef00c304/1750674186/hls_264_master.m3u8?t=1687407694"},{"title","Overview Trailer"}}})}};
   atomicWrite(root/L"cache"/L"jobs"/L"media-video-search"/L"steam-1336980.json",result);
   return Json{{"key",file["key"]},{"appId",1336980},{"executable",nobuExe},{"bindingRevision",bound["bindingRevision"]},{"searchToken",result["searchToken"]},{"candidateId","0"},{"preferredHeight",720}};
 };
 const auto nobuRoot=base/L"nobu";auto nobu=makeRequest(nobuRoot);Store nobuStore(nobuRoot);Queue nobuQueue(nobuRoot);
 nobuQueue.enqueue(Json::array({{{"appId",1336980},{"verified",true},{"executable",nobuExe}}}));
 check(nobuQueue.document()["jobs"].empty(),"automatic EXE admission still rejects the legacy identity cache");
 const auto beforeIdentity=read(identityFile(nobuRoot,nobuExe)),beforeEntry=nobuStore.resolve(nobu);
 nobuQueue.configure(false,"game-running");auto chosen=selectVideoForDownload(nobuRoot,nobu);auto admitted=nobuQueue.document();
 check(chosen["key"]=="steam-1336980"&&chosen["bindingRevision"]==beforeEntry.value("bindingRevision",uint64_t{0})+1,"legacy EXE alias selects exact canonical Nobu entry once");
 check(admitted["jobs"].contains("steam-1336980")&&admitted["jobs"]["steam-1336980"]["selectedVideo"]==chosen["selectedVideo"],"accepted choice exists in the durable queue before the success receipt");
 check(!admitted["jobs"]["steam-1336980"].contains("executable"),"explicit media AppID does not manufacture EXE verification");
 check(read(identityFile(nobuRoot,nobuExe))==beforeIdentity&&!nobuStore.identity(nobuExe).value("verified",false),"legacy identity cache remains unchanged and unverified");
 check(chosen["assets"]["static"]==beforeEntry["assets"]["static"]&&chosen.value("staticReady",false),"old wallpaper remains available until the selected download publishes");
 check(scopedManual(admitted)&&admitted["manualTargets"]==Json::array({"steam-1336980"})&&!admitted.value("enabled",true),"explicit candidate runs once without enabling automatic downloads");
 const auto firstChoice=chosen;auto duplicate=selectVideoForDownload(nobuRoot,nobu);
 check(duplicate["bindingRevision"]==firstChoice["bindingRevision"]&&nobuQueue.document()==admitted,"lost acknowledgement retry neither edits nor extends/restarts queue ownership");
 const auto nobuSession=nobuQueue.start();auto nobuClaim=nobuQueue.claim(nobuSession);auto running=nobuQueue.document();selectVideoForDownload(nobuRoot,nobu);
 check(!nobuSession.empty()&&nobuClaim.value("appId",0)==1336980&&nobuQueue.document()==running,"duplicate click preserves an active selected worker and its session");
 nobuQueue.result(nobuSession,"steam-1336980",true);nobuQueue.end(nobuSession,"completed");const auto done=nobuQueue.document();selectVideoForDownload(nobuRoot,nobu);
 check(nobuQueue.document()==done&&!manualRun(done),"duplicate acknowledgement cannot restart completed scoped work");
 auto intervening=nobuStore.resolve(nobu);intervening["action"]="playback";intervening["start"]=0;intervening["end"]=30;nobuStore.edit(intervening);
 check(rejected([&]{selectVideoForDownload(nobuRoot,nobu);}),"an intervening real edit fences even an otherwise identical retry");
 for(const auto* fault:{"token","revision","appid","executable","candidate","cancel","identity-conflict","offline"}){
   const auto root=base/wide(std::string("selection-")+fault);auto request=makeRequest(root);Store media(root);Queue q(root);const auto original=media.resolve(request);
   if(std::string_view(fault)=="token")request["searchToken"]="wrong";
   if(std::string_view(fault)=="revision")request["bindingRevision"]=0;
   if(std::string_view(fault)=="appid")request["appId"]=570;
   if(std::string_view(fault)=="executable")request["executable"]="D:/other.exe";
   if(std::string_view(fault)=="candidate")request["candidateId"]="missing";
   if(std::string_view(fault)=="identity-conflict"||std::string_view(fault)=="offline"){
     Json match={{"appId",570},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"}};
     if(std::string_view(fault)=="offline")match["source"]="local-install-metadata-offline";
     atomicWrite(identityFile(root,nobuExe),{{"executable",nobuExe},{"match",match}});
   }
   if(std::string_view(fault)=="offline"){
     auto selected=selectVideoForDownload(root,request);check(selected.value("appId",0)==1336980&&!media.identity(nobuExe).value("verified",false),"offline guess is ignored rather than promoted or allowed to redirect explicit media");
   }else{
     check(rejected([&]{selectVideoForDownload(root,request,[&]{return std::string_view(fault)=="cancel";});}),"invalid token/revision/AppID/EXE/candidate/cancellation/confirmed conflict is rejected");
     check(media.resolve(request.value("appId",0)==570?Json{{"appId",1336980}}:Json{{"key","steam-1336980"}})==original&&q.document()["jobs"].empty(),"rejected selection cannot mutate the saved wallpaper or queue");
   }
 }

 const auto cancelledRoot=base/L"cancelled-receipt";auto cancelledRequest=makeRequest(cancelledRoot);Queue cancelledQueue(cancelledRoot);selectVideoForDownload(cancelledRoot,cancelledRequest);cancelledQueue.cancelManual();const auto cancelledState=cancelledQueue.document();
 check(rejected([&]{selectVideoForDownload(cancelledRoot,cancelledRequest);})&&cancelledQueue.document()==cancelledState,"a cancelled accepted request cannot return a false queued receipt or restart through retry");
 const auto supersededRoot=base/L"superseded-worker";auto oldRequest=makeRequest(supersededRoot);Store supersededStore(supersededRoot);Queue supersededQueue(supersededRoot);supersededQueue.configure(false,"");
 auto oldChoice=selectVideoForDownload(supersededRoot,oldRequest);const auto oldSession=supersededQueue.start();auto oldTarget=supersededQueue.claim(oldSession);auto oldLease=supersededStore.begin(oldTarget,supersededQueue.document().value("manualRequestId",std::string{}));
 check(oldLease.value("acquired",false),"old selected worker owns a real Store lease before concurrent selection");
 const auto supersededCatalog=supersededRoot/L"cache"/L"jobs"/L"media-video-search"/L"steam-1336980.json";auto nextCatalog=read(supersededCatalog);nextCatalog["bindingRevision"]=oldChoice["bindingRevision"];nextCatalog["searchToken"]="next-catalog";atomicWrite(supersededCatalog,nextCatalog);
 auto nextRequest=oldRequest;nextRequest["bindingRevision"]=oldChoice["bindingRevision"];nextRequest["searchToken"]="next-catalog";auto newChoice=selectVideoForDownload(supersededRoot,nextRequest);
 const auto lateVideo=supersededRoot/L"late.mp4";{std::ofstream out(lateVideo,std::ios::binary);out.write("\0\0\0\x18" "ftypisom",12);}
 check(rejected([&]{supersededStore.publish(oldLease,lateVideo,false);}),"a superseded download cannot publish into the newer selected binding");
 supersededStore.fail(oldLease);check(supersededStore.resolve(nextRequest)["bindingRevision"]==newChoice["bindingRevision"],"late old failure cannot replace the newer choice");
 supersededQueue.result(oldSession,"steam-1336980",true);
 check(supersededQueue.document()["jobs"]["steam-1336980"]["state"]=="pending"&&supersededQueue.document()["active"].empty(),"old worker success cannot complete or erase the replacement queued request");
 supersededQueue.end(oldSession,"completed");check(manualRun(supersededQueue.document()),"old worker exit preserves the replacement manual request");
 const auto nextSession=supersededQueue.start();auto nextTarget=supersededQueue.claim(nextSession);
 check(!nextSession.empty()&&nextTarget["requestBindingRevision"]==newChoice["bindingRevision"],"replacement worker claims the actual latest selection revision");
 auto newLease=supersededStore.begin(nextTarget,supersededQueue.document().value("manualRequestId",std::string{}));check(newLease.value("acquired",false),"replacement request reacquires the lease without waiting for an obsolete timeout");
 for(const auto* fault:{"queue-full","manual-limit"}){
   const auto root=base/wide(fault);auto request=makeRequest(root);Store media(root);Queue q(root);const auto original=media.resolve(request);auto doc=q.document();
   if(std::string_view(fault)=="queue-full")for(int i=1;i<=10000;++i)doc["jobs"]["steam-"+std::to_string(i)]={{"appId",i},{"verified",true},{"state","pending"}};
   else {Json targets=Json::array();for(int i=1;i<=24;++i)targets.push_back({{"appId",i},{"verified",true}});q.enqueue(targets);for(int i=1;i<=24;++i)q.requestSupplement(true,"steam-"+std::to_string(i));doc=q.document();}
   atomicWrite(root/L"config"/L"game-media-downloads.json",doc);
   check(rejected([&]{selectVideoForDownload(root,request);})&&media.resolve(request)==original,"known queue/manual capacity failure is checked before editing the wallpaper");
 }
 for(const auto* blocked:{L"game-media-downloads.json",L"game-media-download-progress.json"}){
   const auto root=base/(std::wstring(L"write-recovery-")+blocked);auto request=makeRequest(root);Store media(root);Queue q(root);q.configure(false,"");const auto original=media.resolve(request);
   HANDLE block=CreateFileW((root/L"config"/blocked).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);check(block!=INVALID_HANDLE_VALUE,"fault injection owns an isolated queue file without delete sharing");
   const bool failed=rejected([&]{selectVideoForDownload(root,request);});CloseHandle(block);
   check(failed&&media.resolve(request).value("bindingRevision",uint64_t{0})==original.value("bindingRevision",uint64_t{0})+1,"Store-success/queue-write-failure preserves the exact durable selection receipt");
   auto recovered=selectVideoForDownload(root,request);auto doc=q.document();
   check(recovered["bindingRevision"]==media.resolve(request)["bindingRevision"]&&doc["jobs"].contains("steam-1336980")&&scopedManual(doc),"same candidate recovers queue/index/progress write failure without another edit");
 }
 Json target={{"appId",570},{"verified",true},{"sourceTier",3},{"name","chosen"}};
 Json other={{"appId",620},{"verified",true},{"sourceTier",1},{"catalogSource","steam-native"},{"name","other installed game"}};
 Queue retired(base/L"retired");retired.enqueue(Json::array({{{"appId",620},{"verified",true},{"catalogSource","popular"}}}));check(retired.document()["jobs"].empty(),"retired popular inventory cannot create background jobs");
 Queue queue(base/L"scoped");queue.enqueue(Json::array({target,other}));queue.configure(false,"game-running");queue.requestCatalog();queue.requestSupplement(true,"steam-570");
 const auto initial=queue.document();check(scopedManual(initial),"candidate creates scoped one-time request");
 check(!initial.contains("inventoryRequestId")&&initial.value("inventoryAwaitUntil",int64_t{0})==0,"known selected target does not start full inventory scan or ten second wait");
 check(queue.snapshot().value("manualTotal",0)==1&&queue.snapshot().value("manualCompleted",-1)==0,"scoped progress counts the chosen target only");
 const auto deadline=initial["manualUntil"];const auto request=initial["manualRequestId"];queue.requestSupplement(true,"steam-570");check(queue.document()["manualUntil"]==deadline&&queue.document()["manualRequestId"]==request,"duplicate selected request is inert and does not extend ownership");
 const auto token=queue.start();check(!token.empty(),"explicit selected target runs with auto off and game soft pause");check(!queue.catalogDue(token),"selected video skips unrelated popular-catalog network work");
 const auto claimed=queue.claim(token);check(claimed.value("appId",0)==570,"chosen video runs before unrelated installed backlog");check(queue.claim(token).is_null(),"unrelated backlog never fills scoped lanes");
 queue.result(token,"steam-570",true);check(manualTargetsComplete(queue.document()),"one chosen completion ends the scoped batch");check(queue.snapshot().value("manualCompleted",0)==1,"scoped completion count is returned to both UIs");queue.end(token,"completed");
 check(!manualRun(queue.snapshot())&&queue.snapshot()["enabled"]==false,"one selected completion restores automatic-off state");check(queue.document()["jobs"]["steam-620"]["state"]=="pending","unrelated installed backlog stays untouched");check(queue.delay()==-1,"auto-off selected completion has no parked restart timer");
 Queue race(base/L"late");race.enqueue(Json::array({target,other}));race.configure(false,"");race.requestSupplement(true,"steam-570");const auto first=race.start();race.claim(first);race.result(first,"steam-570",true);
 race.requestSupplement(true,"steam-620");check(race.document()["manualUntil"]>now(),"second choice joins bounded request without extending deadline");race.end(first,"completed");check(manualRun(race.snapshot()),"old worker cannot clear a newly accepted choice at completion boundary");
 const auto second=race.start();check(race.claim(second).value("appId",0)==620,"late selected target gets its own next claim");race.result(second,"steam-620",true);race.end(second,"completed");check(!manualRun(race.snapshot()),"last selected completion releases scoped request");
 Queue battery(base/L"battery");battery.enqueue(Json::array({target}));battery.configure(false,"on-battery");battery.requestSupplement(true,"steam-570");check(battery.start().empty(),"scoped video selection preserves power protection");
 Queue bulk(base/L"bulk");bulk.enqueue(Json::array({target,other}));bulk.configure(false,"");bulk.requestSupplement(true);bulk.inventoryReady(bulk.snapshot().value("inventoryRequestId",std::string{}));bulk.requestSupplement(true,"steam-570");check(!scopedManual(bulk.document()),"selection within an explicit bulk request preserves bulk scope");const auto b=bulk.start();check(bulk.claim(b).value("appId",0)==570,"selection is preferred within a bulk request");bulk.result(b,"steam-570",true);check(bulk.claim(b).value("appId",0)==620,"bulk request retains other intended work");bulk.result(b,"steam-620",true);bulk.catalogResult(b,true);bulk.end(b,"completed");check(!manualRun(bulk.snapshot()),"bulk completion clears ownership even after accepting a selected priority");
 Store store(base/L"store");auto saved=store.registerEntry(target);saved["action"]="select-video";saved["selectedVideo"]={{"appId",570},{"url",url}};saved=store.edit(saved);
 const auto sample=base/L"sample.mp4";{std::ofstream out(sample,std::ios::binary);out.write("\0\0\0\x18" "ftypisom",12);}saved=store.publish(saved,sample,true);check(!saved.contains("selectedVideo"),"local imported video clears an obsolete online selection");
 saved["action"]="select-video";saved["selectedVideo"]={{"appId",570},{"url",url}};saved=store.edit(saved);saved["action"]="delete";saved["slot"]="dynamic";saved=store.edit(saved);check(!saved.contains("selectedVideo"),"deleting dynamic slot clears an obsolete choice");

 // A cached wallpaper is a fallback, not a receipt for a replacement choice.
 Store replacement(base/L"cached-choice");auto cached=replacement.registerEntry(target);
 const auto poster=base/L"cached.jpg";{std::ofstream out(poster,std::ios::binary);out.write("\xff\xd8\xff\xe0",4);}
 cached=replacement.publish(cached,poster,true);cached=replacement.publish(cached,sample,true);
 const auto oldAsset=cached["assets"]["dynamic"];
 cached["action"]="select-video";cached["selectedVideo"]={{"appId",570},{"url",url}};cached=replacement.edit(cached);
 auto downloading=replacement.begin(target);check(downloading.value("acquired",false),"replacement choice acquires work while retaining an existing wallpaper");
 downloading["reason"]="media-network-request-failed";auto failedChoice=replacement.fail(downloading);
 atomicWrite(base/L"cached-choice-failure.json",failedChoice);
 check(failedChoice.value("dynamicReady",false)&&failedChoice["assets"]["dynamic"]==oldAsset,"network failure preserves the old playable wallpaper");
 check(!complete(failedChoice,now()),"old playable wallpaper cannot complete a failed replacement choice");
 check(!replacement.begin(target).value("acquired",false),"failed replacement honors Store retry backoff");
 Queue replacementQueue(replacement.root());replacementQueue.enqueue(Json::array({target}));
 check(replacementQueue.document()["jobs"].contains("steam-570"),"failed cached replacement remains eligible for durable queue admission");
 auto durable=read(replacement.root()/L"config"/L"game-media.json");durable["entries"]["steam-570"]["retryAt"]=0;atomicWrite(replacement.root()/L"config"/L"game-media.json",durable);
 auto retried=replacement.begin(target);check(retried.value("acquired",false)&&retried["selectedVideo"]["url"]==url,"next admitted retry reacquires the exact selected video");
 auto published=replacement.publish(retried,sample,false);replacement.finish(published);
 const auto finished=replacement.resolve(target);
 check(finished["assets"]["dynamic"]!=oldAsset&&complete(finished,now()),"only successful replacement publication completes cached choice");
 check(!replacement.begin(target).value("acquired",false),"published replacement does not keep downloading on subsequent scans");
 // Older failed records consumed refreshRequested before reporting failure.
 durable=read(replacement.root()/L"config"/L"game-media.json");auto &legacyFailed=durable["entries"]["steam-570"];
 legacyFailed["refreshRequested"]=false;legacyFailed["taskSlots"]=Json::array({"dynamic"});legacyFailed["status"]="error";legacyFailed["retryAt"]=now()+60000;
 atomicWrite(replacement.root()/L"config"/L"game-media.json",durable);
 const auto recoveredChoice=replacement.resolve(target);check(!complete(recoveredChoice,now()),"previous-version failed choice with outstanding slot is not a completed default asset");
 check(!replacement.begin(target).value("acquired",false),"previous-version failed choice retains its retry deadline");
 durable["entries"]["steam-570"]["retryAt"]=0;atomicWrite(replacement.root()/L"config"/L"game-media.json",durable);
 auto recoveredLease=replacement.begin(target);check(recoveredLease.value("acquired",false),"previous-version failed choice recovers on its next eligible retry");
 replacement.finish(recoveredLease);check(!complete(replacement.resolve(target),now()),"interrupted replacement remains pending across lease release");

 const auto localVideo=[](const std::string& host){return host=="video.st.dl.eccdnx.com";};
 check(custom_steam_library::network_policy::acceleratedUrl(url,localVideo)=="https://video.st.dl.eccdnx.com/store_trailers/570/a/master.m3u8","enabled modern video route preserves exact AppID and selected trailer path");
 check(custom_steam_library::network_policy::acceleratedUrl(url,[](const std::string&){return false;})==url,"without an enabled local alias the canonical video URL remains authoritative");
 check(custom_steam_library::network_policy::acceleratedUrl(url,[](const std::string& host){return host=="steamvideo-a.akamaihd.net";})==url,"legacy video alias never rewrites modern AppID trailer paths into false 404s");
 check(custom_steam_library::network_policy::acceleratedUrl("https://store.steampowered.com/api/appdetails?appids=570",[](const std::string& host){return host=="steamstore-a.akamaihd.net";})=="https://steamstore-a.akamaihd.net/api/appdetails?appids=570","metadata and media reuse the existing explicitly enabled Steam acceleration policy");
 const auto legacy=base/L"legacy";fs::create_directories(legacy);fs::copy_file(fs::absolute(argv[0]),legacy/L"CustomSteamLibrary.exe");atomicWrite(legacy/L"package-manifest.json",{{"backgroundMediaProtocol",1},{"backgroundCatalogProtocol",1},{"backgroundIdentityProtocol",1},{"backgroundVideoQualityPolicy",2},{"backgroundManualProtocol",1}});Queue oldQueue(legacy);oldQueue.enqueue(Json::array({target}));oldQueue.requestSupplement(true,"steam-570");
 {Controller owner(legacy,legacy/L"CustomSteamLibrary.exe",[](const Json&){},Limits{5000,10000,20000,50,0},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};}});owner.background({{"enabled",false},{"pauseReason",""}});const auto start=GetTickCount64();while(GetTickCount64()-start<3000&&oldQueue.snapshot().value("reason",std::string{})!="download-video-selection-worker-upgrade-required")Sleep(20);check(oldQueue.snapshot().value("reason",std::string{})=="download-video-selection-worker-upgrade-required","old manual-capable child is not allowed to expand a scoped selection to full batch");check(oldQueue.snapshot().value("workerPid",DWORD{0})==0,"incompatible child remains unlaunched");}
 std::cout<<Json({{"ok",true},{"checks",checks},{"scope","production catalog selection, durable Queue/Controller and real isolated child refusal; no live cache, Steam, input or network"}}).dump()<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 2;}}
