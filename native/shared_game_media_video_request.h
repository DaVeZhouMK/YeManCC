#pragma once
#include "shared_game_media_download_queue.h"
#include "custom-steam-library/custom_steam_library_video_catalog.h"

namespace ymcc::media {
// An explicit official trailer choice authorizes media for this AppID. It
// does not verify the local EXE or repair/promote a legacy identity cache.
inline void requireVideoIdentity(const Store& store,const Json& entry,const Json& request){
    const auto app=entry.value("appId",int64_t{0});
    if(app<=0||app>2147483647||!entry.value("identityVerified",false)||
       !safeKey(stringField(entry,"key"))||stringField(entry,"key")!="steam-"+std::to_string(app))
        throw std::runtime_error("请先确认此游戏的 Steam 身份，再搜索动态壁纸");
    const auto exe=stringField(entry,"executable"),requestedExe=stringField(request,"executable");
    if((request.contains("appId")&&request.value("appId",int64_t{0})!=app)||
       (!requestedExe.empty()&&normalized(requestedExe)!=normalized(exe)))
        throw std::runtime_error("动态壁纸游戏绑定已变化，请重新搜索");
    const auto identity=store.identity(exe);
    if(identity.value("verified",false)&&identity.value("appId",int64_t{0})!=app)
        throw std::runtime_error("游戏身份已更新，请刷新游戏后重新搜索动态壁纸");
}

inline Json selectVideoForDownload(const fs::path& root,const Json& request,
                                  const std::function<bool()>& cancelled={}){
    // Store and Queue use this same recursive cross-process mutex. No worker
    // or competing editor may see a half-admitted choice during this request.
    Lock lock(root);Store store(root);downloads::Queue queue(root);
    auto entry=store.resolve(request);requireVideoIdentity(store,entry,request);
    const auto key=entry["key"].get<std::string>();
    const auto catalog=read(root/L"cache"/L"jobs"/L"media-video-search"/wide(key+".json"));
    auto authorization=entry;
    const auto searchedRevision=catalog.value("bindingRevision",uint64_t{0});
    const auto revision=entry.value("bindingRevision",uint64_t{0});
    const bool retry=searchedRevision<UINT64_MAX&&revision==searchedRevision+1;
    if(retry)authorization["bindingRevision"]=searchedRevision;
    auto selected=custom_steam_library::video_catalog::selection(authorization,catalog,request,now());
    // This receipt permits only an exact retry after a Store/Queue write or
    // IPC acknowledgement failure. It never accepts another intervening edit.
    selected["selectionToken"]=stringField(catalog,"searchToken");
    if(retry&&entry.value("selectedVideo",Json::object())!=selected)
        throw std::runtime_error("壁纸设置已更新，请重新搜索后选择");
    auto document=queue.document();
    const auto jobs=document.value("jobs",Json::object());
    if(!jobs.contains(key)&&jobs.size()>=10000)
        throw std::runtime_error("media-download-queue-full");
    if(downloads::scopedManual(document)&&
       std::find(document["manualTargets"].begin(),document["manualTargets"].end(),key)==document["manualTargets"].end()&&
       document["manualTargets"].size()>=24)
        throw std::runtime_error("media-download-selection-limit");
    if(cancelled&&cancelled())throw std::runtime_error("media-download-cancelled");
    if(!retry){
        entry["action"]="select-video";entry["slot"]="dynamic";entry["selectedVideo"]=selected;
        entry=store.edit(entry);
    }else if(jobs.contains(key)&&jobs[key].value("requestBindingRevision",uint64_t{0})==revision&&
             jobs[key].value("selectedVideo",Json::object())==selected){
        // A lost response must not restart a completed transfer or reset its
        // backoff, manual deadline, cancellation, or worker ownership.
        const bool accepted=document.value("manualTargetRevisions",Json::object()).value(key,uint64_t{0})==revision;
        if(accepted&&stringField(document,"manualState")=="cancelled")
            throw std::runtime_error("下载请求已取消，请重新搜索后选择");
        if(accepted||stringField(jobs[key],"state")=="done")return entry;
    }
    // AppID-only admission is already used for official Steam media. Keep
    // the EXE on its existing Store binding; automatic inventory still uses
    // strict EXE identity verification in Queue::enqueue.
    Json target={{"appId",entry["appId"]},{"verified",true},{"name",stringField(entry,"name")},
                 {"sourceTier",3},{"catalogSource","manual-video-selection"}};
    queue.enqueue(Json::array({target}));
    const auto admitted=queue.document().value("jobs",Json::object()).value(key,Json::object());
    if(admitted.value("requestBindingRevision",uint64_t{0})!=entry.value("bindingRevision",uint64_t{0})||
       admitted.value("selectedVideo",Json::object())!=selected)
        throw std::runtime_error("media-download-target-unavailable");
    queue.requestSupplement(true,key);
    return entry;
}
} // namespace ymcc::media
