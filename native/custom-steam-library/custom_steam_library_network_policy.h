#pragma once
#include <algorithm>
#include <string>
namespace custom_steam_library::network_policy {
// Shared with the existing artwork weak-network transport. No resident work.
inline constexpr int resolveMs=15000,connectMs=25000,ioMs=45000,maxAttempts=4;
inline constexpr int localResolveMs=3000,localConnectMs=5000,localIoMs=15000,maxRetryAfterMs=30000;
inline bool retryableHttp(unsigned code){return code==408||code==425||code==429||code==500||code==502||code==503||code==504;}
template<class Local> std::string acceleratedUrl(const std::string& url,Local local){
    if(!url.starts_with("https://"))return url;const auto end=url.find('/',8);
    const auto host=url.substr(8,end==std::string::npos?std::string::npos:end-8);
    auto lower=host;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(c>='A'&&c<='Z'?c+32:c);});
    if(local(lower))return url;const auto path=end==std::string::npos?std::string{"/"}:url.substr(end);
    const auto replace=[&](const std::string& alias){return local(alias)?"https://"+alias+path:url;};
    if(lower=="store.steampowered.com")return replace("steamstore-a.akamaihd.net");
    if(lower=="video.steamstatic.com"||lower=="video.akamai.steamstatic.com"||lower=="video.cloudflare.steamstatic.com"||lower=="video.fastly.steamstatic.com"){
        // The old steamvideo alias serves /steam/apps, not modern AppID paths.
        if(path.starts_with("/store_trailers/"))return replace("video.st.dl.eccdnx.com");
        return replace("steamvideo-a.akamaihd.net");
    }
    if(lower=="shared.steamstatic.com"||lower=="shared.cloudflare.steamstatic.com"||lower=="shared.akamai.steamstatic.com"||lower=="cdn.cloudflare.steamstatic.com"){
        auto result=replace("steamcdn-a.akamaihd.net");const std::string prefix="https://steamcdn-a.akamaihd.net/store_item_assets/steam/apps/";
        if(result.starts_with(prefix))result="https://steamcdn-a.akamaihd.net/steam/apps/"+result.substr(prefix.size());return result;
    }
    return url;
}
} // namespace custom_steam_library::network_policy
