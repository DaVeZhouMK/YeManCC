#pragma once
#include "shared_game_media.h"
#include "shared_game_media_hardware_video.h"

namespace ymcc::media::playback30 {
enum class Outcome : int { ready, original, superseded, unsupported };

inline Outcome prepare(Store& store,const Json& claim,const std::string& token,
                       const std::function<void()>& check,const std::function<void()>& checkCommit={}) {
    check();
    const auto original=store.playback30Source(claim);
    if(original.empty())return Outcome::superseded;
    if(!store.readyPlayback30(original).empty())return Outcome::ready;
    const auto input=custom_steam_library::ioPath(store.assetPath(original));
    const auto stamp=fs::last_write_time(input);
    const auto inputBytes=fs::file_size(input);
    if(!inputBytes||inputBytes!=original.value("bytes",uint64_t{0}))return Outcome::unsupported;
    const auto current=[&]{
        if(checkCommit)checkCommit();else check();
        if(fs::last_write_time(input)!=stamp||fs::file_size(input)!=inputBytes)
            throw std::runtime_error("media-playback30-source-file-changed");
    };
    const bool webm=stringField(original,"extension")=="webm";
    frameProbe::Cadence cadence;
    UINT firstAdapter=0;
    if(webm){
        if(!frameProbe::opaqueVp9Webm(input))return Outcome::unsupported;
        for(;firstAdapter<2&&!cadence;++firstAdapter){
            current();
            try {
                hardwareVideo::transcode(input.c_str(),nullptr,firstAdapter,check,&cadence);
            }catch(const std::exception&) { current(); }
            if(cadence)break;
        }
    }else cadence=frameProbe::inspectMp4(input);
    current();
    if(!cadence)return Outcome::unsupported;
    if(!cadence.exceeds30())
        return store.recordPlayback30Cadence(claim,cadence,current)?Outcome::original:Outcome::superseded;

    const auto staging=custom_steam_library::ioPath(store.playback30Staging(token));
    capacity::require(store.root(),1,true,capacity::maxAssetBytes);
    fs::create_directories(staging.parent_path());
    // The supervisor owns this random token, including forced-stop cleanup.
    // Refuse to overwrite a path if its previous owner was not retired.
    if(fs::exists(staging))throw std::runtime_error("media-playback30-staging-conflict");
    struct Cleanup {
        const fs::path& path;
        ~Cleanup(){std::error_code ec;fs::remove(path,ec);}
    } cleanup{staging};
    unsigned frames=0;
    for(UINT adapter=firstAdapter;adapter<2&&!frames;++adapter) {
        current();
        try {
            frames=hardwareVideo::transcode(input.c_str(),staging.c_str(),adapter,check,webm?&cadence:nullptr);
        }catch(const std::exception&) {
            // A policy withdrawal is not a codec incompatibility and cannot
            // authorize another adapter. No software encoder is attempted.
            current();
            fs::remove(staging);
        }
    }
    if(!frames)return Outcome::unsupported;
    current();
    return store.publishPlayback30(claim,token,frames,cadence,current)?Outcome::ready:Outcome::superseded;
}
} // namespace ymcc::media::playback30
