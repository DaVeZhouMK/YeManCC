#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_server.h"
#include "../native/shared_game_media_playback30_job.h"
#include <iostream>

using namespace ymcc::media;
void expect(bool value,const char* label){if(!value)throw std::runtime_error(label);}

std::string status(const std::string& url) {
    const auto portAt=url.find(':',7),slash=url.find('/',portAt);
    expect(portAt!=std::string::npos&&slash!=std::string::npos,"media URL");
    const auto port=url.substr(portAt+1,slash-portAt-1);
    struct Socket {SOCKET value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}} client;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(static_cast<unsigned short>(std::stoi(port)));
    expect(client.value!=INVALID_SOCKET&&connect(client.value,reinterpret_cast<sockaddr*>(&address),sizeof address)==0,"connect media fixture");
    const DWORD timeout=3000;setsockopt(client.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof timeout);
    const auto request="HEAD "+url.substr(slash)+" HTTP/1.1\r\nHost: 127.0.0.1:"+port+"\r\nConnection: close\r\n\r\n";
    expect(send(client.value,request.data(),static_cast<int>(request.size()),0)==request.size(),"send media fixture");
    std::string result;char buffer[512];
    for(int n;(n=recv(client.value,buffer,sizeof buffer,0))>0&&result.size()<4096;)result.append(buffer,n);
    return result;
}

int wmain(int argc,wchar_t** argv) {
    if(argc!=4&&argc!=7)return 2;
    try {
        const fs::path root=fs::absolute(argv[1]),high=argv[2],low=argv[3];
        expect(!fs::exists(root),"fresh private test root required");
        fs::create_directories(root);
        Store store(root);
        auto request=Json{{"appId",101},{"slot","dynamic"}};
        request["bindingRevision"]=store.registerEntry(request)["bindingRevision"];
        const auto first=store.publish(request,high,true);
        const auto original=first["asset"];
        const auto originalId=stringField(original,"id");
        const Json claim={{"key",first["key"]},{"id",original["id"]},{"revision",original["revision"]},{"bindingRevision",first["bindingRevision"]}};
        const auto sourceCadence=frameProbe::inspectMp4(high),outputCadence=frameProbe::inspectMp4(low);
        expect(sourceCadence.exceeds30()&&outputCadence&&!outputCadence.exceeds30(),"test source/output cadence");
        expect(original["cadence"]["schema"]==frameProbe::cadenceSchema&&original["cadence"]["exceeds30"]==true,"current original cadence recorded at publish");
        expect(store.playback30Source(claim)==original,"exact original identity resolves");
        expect(Store::playback30Claim(first)==claim,"high cadence creates path-free claim with local queue version");
        auto lowFrameRate=first;lowFrameRate["asset"]["cadence"]=cadenceRecord(outputCadence);
        expect(Store::playback30Claim(lowFrameRate).empty(),"verified 30 FPS original does not enqueue");
        expect(Store::originalWallpaperNeedsPlayback30(first)&&!Store::originalWallpaperNeedsPlayback30(lowFrameRate),
               "only the high-cadence original needs optimization");
        for(int schema=1;schema<frameProbe::cadenceSchema;++schema) {
            lowFrameRate["asset"]["cadence"]["schema"]=schema;
            expect(Store::playback30Claim(lowFrameRate)==claim,"old source cadence can be rechecked on demand");
            expect(Store::wallpaperPlaybackStamp(lowFrameRate)==originalId,"rechecking cadence does not change the playable source");
        }
        for(const Json& cadence:{Json::object(),Json{{"schema",frameProbe::cadenceSchema},{"exceeds30",0}},Json{{"schema","bad"},{"exceeds30",false}}}) {
            auto unknown=first;unknown["asset"]["cadence"]=cadence;
            expect(Store::playback30Claim(unknown)==claim&&Store::wallpaperPlaybackStamp(unknown)==originalId,
                   "an existing original remains the playback source while its cadence is unknown");
        }
        auto webm=first;webm["asset"]["extension"]="webm";webm["asset"].erase("cadence");
        expect(Store::playback30Claim(webm)==claim&&Store::wallpaperPlaybackStamp(webm)==originalId,
               "unknown WebM cadence only requests optional work while keeping the original playable");
        auto unsupported=webm;unsupported["asset"]["extension"]="mkv";
        expect(Store::playback30Claim(unsupported).empty()&&Store::wallpaperPlaybackStamp(unsupported)==originalId,
               "an unsupported container stays playable without starting a worker");
        expect(store.playback30Selection(first).empty()&&Store::wallpaperPlaybackStamp(first)==originalId&&
               Store::wallpaperPlaybackStamp(lowFrameRate)==originalId,"original remains selected before prepare or after cadence-only validation");
        const auto token=randomId();const auto staged=store.playback30Staging(token);
        fs::create_directories(staged.parent_path());
        fs::copy_file(high,staged);
        bool refused=false;
        try{store.publishPlayback30(claim,token,sourceCadence.samples,sourceCadence,[]{});}catch(...){refused=true;}
        expect(refused&&store.playback30Selection(store.resolve(request)).empty(),"wrong cadence never published");
        fs::copy_file(low,staged,fs::copy_options::overwrite_existing);
        refused=false;
        try{store.publishPlayback30(claim,token,outputCadence.samples,sourceCadence,[&]{throw std::runtime_error("cancelled");});}catch(...){refused=true;}
        expect(refused&&fs::exists(staged)&&store.playback30Selection(store.resolve(request)).empty(),"cancellation before commit");
        expect(store.publishPlayback30(claim,token,outputCadence.samples,sourceCadence,[]{}),"eligible verified derivative published");
        expect(!fs::exists(staged),"publication consumes staged file");
        const auto current=store.resolve(request),variant=store.playback30Selection(current);
        expect(!variant.empty()&&current["asset"]["id"]==original["id"]&&variant["id"]!=original["id"],"selected view does not replace editor asset");
        const auto key=current["key"].get<std::string>();
        expect(!store.snapshot()["entries"][key].contains("wallpaperPlaybackStamp"),"editor snapshots stay unchanged");
        expect(Store::wallpaperPlaybackStamp(first)==originalId&&store.snapshot(true)["entries"][key]["wallpaperPlaybackStamp"]==variant["id"],"same binding publishes a playback-only hint");
        auto legacyVariant=current;
        for(int schema=1;schema<frameProbe::cadenceSchema;++schema) {
            legacyVariant["asset"]["playback30"]["asset"]["cadence"]["schema"]=schema;
            expect(store.playback30Selection(legacyVariant)["id"]==variant["id"],"reuse verified derivative on source probe upgrade");
        }
        legacyVariant["asset"]["playback30"]["asset"]["cadence"]["schema"]=frameProbe::cadenceSchema+1;
        expect(store.playback30Selection(legacyVariant).empty(),"unknown derivative schema falls back");
        expect(current["bindingRevision"]==first["bindingRevision"],"derivative metadata does not make editor stale");
        auto manual=current;manual["selectedVideo"]={{"preferredHeight",1080}};
        expect(store.playback30Selection(manual).empty()&&Store::playback30Claim(manual).empty()&&
               !Store::originalWallpaperNeedsPlayback30(manual),"manual 1080p bypass");
        Server server;
        const auto originalUrl=server.expose(store,current),reducedUrl=server.expose(store,current,&variant);
        expect(!originalUrl.empty()&&!reducedUrl.empty()&&originalUrl!=reducedUrl,"separate original and reduced URLs");
        server.reconcile(first); // same binding, before derivative publication
        expect(server.expose(store,first)==originalUrl&&status(originalUrl).starts_with("HTTP/1.1 200")&&status(reducedUrl).starts_with("HTTP/1.1 200"),"late original-only read keeps both URLs active");
        const auto derivativeFile=store.assetPath(variant);
        fs::last_write_time(derivativeFile,fs::file_time_type::clock::now()-std::chrono::hours(25));
        store.collectOrphans();expect(fs::exists(derivativeFile),"referenced derivative retained during cleanup");
        fs::resize_file(derivativeFile,1);
        expect(store.playback30Selection(store.resolve(request)).empty(),"truncated derivative falls back");
        auto replacement=request;replacement["bindingRevision"]=current["bindingRevision"];
        const auto replaced=store.publish(replacement,high,true);
        fs::copy_file(low,staged);
        expect(replaced["asset"]["id"]!=original["id"]&&store.playback30Source(claim).empty(),"replacement fences old claim");
        expect(!store.publishPlayback30(claim,token,outputCadence.samples,sourceCadence,[]{}),"late old worker cannot replace new source");
        expect(store.playback30Selection(store.resolve(request)).empty(),"replacement initially plays its original");
        fs::last_write_time(derivativeFile,fs::file_time_type::clock::now()-std::chrono::hours(25));
        store.collectOrphans();expect(!fs::exists(derivativeFile),"unreferenced aged derivative collected");
        const auto newOriginal=replaced["asset"];
        const Json newClaim={{"key",replaced["key"]},{"id",newOriginal["id"]},{"revision",newOriginal["revision"]}};
        const auto jobToken=randomId();
        const auto started=GetTickCount64();
        const auto check=[&]{if(GetTickCount64()-started>30000)throw std::runtime_error("fixture deadline");};
        expect(playback30::prepare(store,newClaim,jobToken,check)==playback30::Outcome::ready,"real job finished and published");
        const auto newer=store.playback30Selection(store.resolve(request));
        expect(!newer.empty()&&!fs::exists(store.playback30Staging(jobToken)),"real hardware output selected and staging retired");
        expect(playback30::prepare(store,newClaim,jobToken,check)==playback30::Outcome::ready&&
               store.playback30Selection(store.resolve(request))==newer,"repeat claim reuses same derivative");
        const auto beforeFailure=store.resolve(request);
        const auto liveSource=server.expose(store,beforeFailure),liveVariant=server.expose(store,beforeFailure,&newer);
        const auto fallback=store.edit({{"key",beforeFailure["key"]},{"bindingRevision",beforeFailure["bindingRevision"]},
            {"slot","dynamic"},{"action","playback-failed"},{"assetId",newer["id"]}});
        expect(fallback.value("dynamicReady",false)&&fallback["asset"]["id"]==newOriginal["id"]&&
            Store::playback30Claim(fallback).empty()&&store.playback30Source(newClaim).empty(),"rejected derivative leaves source playable without re-encoding loop");
        expect(Store::wallpaperPlaybackStamp(fallback)==stringField(newOriginal,"id"),"rejected derivative switches the playback hint back to original");
        server.reconcile(fallback);
        expect(server.expose(store,beforeFailure,&newer).empty()&&status(liveVariant).starts_with("HTTP/1.1 404")&&
            status(liveSource).starts_with("HTTP/1.1 200"),"failed derivative fenced without revoking source URL");
        for(const int fps:{25,30,60}){
            hardwareVideo::Vp9Timeline t;
            for(unsigned i=0;i<450;++i)t.observe(((static_cast<LONGLONG>(i)*1000+fps/2)/fps)*10000);
            expect(t.cadence.fastWindow==(fps>30),"rounded WebM PTS use a window tolerance, not nominal FPS");
        }
        hardwareVideo::Vp9Timeline lateBurst;
        for(unsigned i=0;i<100;++i)lateBurst.observe(i*400000LL);
        expect(!lateBurst.cadence.fastWindow,"slow PTS prefix alone is not high cadence");
        lateBurst.observe(99*400000LL+170000);
        expect(lateBurst.cadence.fastWindow,"a late rendered burst still requests conversion");
        refused=false;
        try{lateBurst.observe(lateBurst.last);}catch(...){refused=true;}
        expect(refused,"duplicate decoded PTS cannot authorize a derivative");
        if(argc==7){
            for(int i=4;i<7;++i){
                const fs::path input=fs::absolute(argv[i]);
                const bool highWebm=i==4,lowWebm=i==5;
                expect(frameProbe::opaqueVp9Webm(input)==(i!=6),"VP9 metadata eligibility");
                Json item={{"appId",100+i},{"slot","dynamic"}};
                item["bindingRevision"]=store.registerEntry(item)["bindingRevision"];
                const auto seeded=store.publish(item,input,true);
                const auto webmClaim=Store::playback30Claim(seeded);
                const auto webmId=seeded["asset"]["id"];
                expect(!webmClaim.empty()&&seeded["asset"]["extension"]=="webm","WebM on-demand claim");
                const auto originalWebmUrl=server.expose(store,seeded);
                expect(status(originalWebmUrl).starts_with("HTTP/1.1 200"),"original WebM served while pending");
                const auto webmToken=randomId();
                const auto webmStage=store.playback30Staging(webmToken);
                if(highWebm){
                    bool cancelled=false;
                    try{playback30::prepare(store,webmClaim,webmToken,[&]{
                        if(fs::exists(webmStage))throw std::runtime_error("fixture WebM cancel");
                    });}catch(const std::exception& e){cancelled=std::string_view(e.what())=="fixture WebM cancel";}
                    expect(cancelled&&!fs::exists(webmStage)&&store.playback30Selection(store.resolve(item)).empty(),
                           "cancelled WebM encoder retires its staging and leaves the original");
                }
                const auto started=GetTickCount64();
                const auto webmOutcome=playback30::prepare(store,webmClaim,webmToken,[&]{
                    if(!highWebm)expect(!fs::exists(webmStage),"no staging for low or unsupported WebM");
                    if(GetTickCount64()-started>20000)throw std::runtime_error("fixture WebM deadline");
                });
                const auto row=store.resolve(item),derived=store.playback30Selection(row);
                expect(webmOutcome==(highWebm?playback30::Outcome::ready:lowWebm?playback30::Outcome::original:
                    playback30::Outcome::unsupported)&&row["asset"]["id"]==webmId&&!fs::exists(webmStage),"WebM job outcome preserves original");
                if(highWebm){
                    expect(row["asset"]["cadence"]["samples"]==960&&row["asset"]["cadence"]["exceeds30"]==true&&
                           derived["cadence"]["samples"]==480&&derived["extension"]=="mp4","high WebM measured and converted at 30 FPS");
                    expect(status(server.expose(store,row,&derived)).starts_with("HTTP/1.1 200"),"WebM derivative served");
                    const auto variantPath=store.assetPath(derived);
                    std::cout<<"webm-derived="<<utf8(variantPath.wstring())<<'\n';
                    const auto withheld=variantPath.wstring()+L".fixture-withheld";
                    fs::rename(variantPath,withheld);
                    expect(store.playback30Selection(store.resolve(item)).empty()&&status(originalWebmUrl).starts_with("HTTP/1.1 200"),
                           "missing MP4 derivative leaves playable WebM original");
                    fs::rename(withheld,variantPath);

                    Json retryItem={{"appId",109},{"slot","dynamic"}};
                    retryItem["bindingRevision"]=store.registerEntry(retryItem)["bindingRevision"];
                    const auto retrySeed=store.publish(retryItem,input,true);
                    const auto retryClaim=Store::playback30Claim(retrySeed);
                    const auto retryUrl=server.expose(store,retrySeed);
                    const auto retryToken=randomId();const auto retryStage=store.playback30Staging(retryToken);
                    bool interruptedReuse=false;
                    const auto retryStarted=GetTickCount64();
                    const auto recovered=playback30::prepare(store,retryClaim,retryToken,[&]{
                        if(GetTickCount64()-retryStarted>20000)throw std::runtime_error("fixture WebM recovery deadline");
                        if(!interruptedReuse&&fs::exists(retryStage)){
                            interruptedReuse=true;
                            throw std::runtime_error("fixture transient WebM writer failure");
                        }
                    });
                    const auto retryResult=store.resolve(retryItem);
                    expect(interruptedReuse&&recovered==playback30::Outcome::ready&&!fs::exists(retryStage)&&
                           retryResult["asset"]["id"]==retrySeed["asset"]["id"]&&
                           store.playback30Selection(retryResult)["cadence"]["samples"]==480&&
                           status(retryUrl).starts_with("HTTP/1.1 200"),"failed VP9 reuse can recover without replacing the original");
                    std::cout<<"PASS VP9 reuse interruption and fresh-decoder recovery\n";
                }else{
                    expect(derived.empty()&&status(originalWebmUrl).starts_with("HTTP/1.1 200"),"low or unsupported WebM kept playable");
                    if(lowWebm)expect(row["asset"]["cadence"]["samples"]==39&&row["asset"]["cadence"]["exceeds30"]==false&&
                         Store::playback30Claim(row).empty(),"proved 25 FPS gets no rescan or upscale");
                }
            }
            std::cout<<"PASS VP9 WebM: 60-to-30 hardware, 25-FPS passthrough, unsupported passthrough, cancellation and missing variant fallback\n";
        }
        std::cout<<"PASS playback30 store and HTTP: cadence, cancellation, output, identity, 1080P choice, dual URLs, fallback, replacement, cleanup\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
