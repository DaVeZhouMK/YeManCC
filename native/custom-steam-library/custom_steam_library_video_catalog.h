#pragma once
#include "json.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace custom_steam_library::video_catalog {
using Json=nlohmann::json;
inline std::string text(const Json& j,const char* key){const auto it=j.find(key);return it!=j.end()&&it->is_string()?it->get<std::string>():std::string{};}
inline bool allowed(const std::string& url){return url.size()<=8192&&std::regex_match(url,[]()->const std::regex&{static const std::regex pattern(R"(https://(?:[a-zA-Z0-9-]+\.)*(?:steamstatic\.com|steampowered\.com|steamusercontent\.com|akamaihd\.net|eccdnx\.com|queniuqe\.com)/[^\s<>"\\]+)",std::regex::icase);return pattern;}());}
inline Json candidates(int appId,const Json& data){
 if(appId<=0||!data.is_object()||data.value("steam_appid",0)!=appId||text(data,"type")!="game")throw std::runtime_error("media-video-identity-unconfirmed");
 Json result={{"appId",appId},{"query",text(data,"name")},{"candidates",Json::array()}};std::set<std::string> seen;
 const auto movies=data.value("movies",Json::array());if(!movies.is_array())throw std::runtime_error("media-video-catalog-invalid");
 for(const auto& movie:movies){if(!movie.is_object())continue;std::string url;Json sources=Json::object();
  for(const auto* quality:{"720","480","1080","max"})for(const auto* format:{"mp4","webm"}){const auto value=text(movie.value(format,Json::object()),quality);if(!sources.contains(quality)&&allowed(value))sources[quality]=value;}
  for(const auto* key:{"hls_h264","hlsManifest"})if(allowed(text(movie,key))){url=text(movie,key);break;}
  if(url.empty())for(const auto* quality:{"720","480","1080","max"}){for(const auto* format:{"mp4","webm"}){const auto values=movie.value(format,Json::object());const auto value=text(values,quality);if(allowed(value)){url=value;break;}}if(!url.empty())break;}
  if(url.empty()||!seen.insert(url).second)continue;
  std::string poster;for(const auto* key:{"thumbnail","thumbnail_1920","thumbnail_1080"})if(allowed(text(movie,key))){poster=text(movie,key);break;}
  if(poster.empty()&&allowed(text(data,"header_image")))poster=text(data,"header_image");
  const auto index=result["candidates"].size();result["candidates"].push_back({{"sources",sources},{"hls",url.find(".m3u8")!=std::string::npos},{"id",std::to_string(index)},{"appId",appId},{"url",url},{"previewUrl",poster},{"title",text(movie,"name").empty()?"Steam 预告片 "+std::to_string(index+1):text(movie,"name")},{"provider","steam"},{"quality",url.find(".m3u8")!=std::string::npos?"720p 优先 · 可选 1080p":"按所选清晰度下载"}});
  if(result["candidates"].size()>=24)break;
 }
 return result;
}
// A catalog authorizes only the exact entry revision that was searched.
inline Json selection(const Json& entry,const Json& catalog,const Json& request,int64_t time){
 const auto revision=entry.value("bindingRevision",uint64_t{0});
 const auto created=catalog.value("createdAt",int64_t{0});
 if(!entry.value("identityVerified",false)||text(entry,"key").empty()||text(catalog,"entryKey")!=text(entry,"key")||
    catalog.value("appId",int64_t{0})!=entry.value("appId",int64_t{0})||
    text(request,"searchToken").empty()||text(catalog,"searchToken")!=text(request,"searchToken")||
    created<=0||created>time||time-created>600000)throw std::runtime_error("候选已过期，请重新搜索动态壁纸");
 if(!catalog.contains("bindingRevision")||!request.contains("bindingRevision")||catalog.value("bindingRevision",uint64_t{0})!=revision||request.value("bindingRevision",uint64_t{0})!=revision)
   throw std::runtime_error("壁纸设置已更新，请重新搜索后选择");
 const auto id=text(request,"candidateId");
 const int preferred=request.value("preferredHeight",720);
 if(preferred!=480&&preferred!=720&&preferred!=1080)throw std::runtime_error("media-video-quality-invalid");
 for(const auto& candidate:catalog.value("candidates",Json::array()))if(text(candidate,"id")==id&&allowed(text(candidate,"url"))){
   std::string url=text(candidate,"url");
   if(url.find(".m3u8")==std::string::npos){
     const auto sources=candidate.value("sources",Json::object());
     const std::vector<std::string> qualities=preferred==1080?std::vector<std::string>{"1080","max","720","480"}:preferred==480?std::vector<std::string>{"480","720","1080","max"}:std::vector<std::string>{"720","480","1080","max"};
     for(const auto& quality:qualities)if(allowed(text(sources,quality.c_str()))){url=text(sources,quality.c_str());break;}
   }
   return {{"appId",entry["appId"]},{"url",url},{"title",text(candidate,"title")},{"preferredHeight",preferred}};
 }
 throw std::runtime_error("动态壁纸候选不可用，请重新搜索");
}
} // namespace custom_steam_library::video_catalog
