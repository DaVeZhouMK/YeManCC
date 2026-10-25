#include "custom_steam_library_test_runtime.h"
#include <iostream>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void fixtureText(const fs::path& path, const std::string& text) { writeAtomic(path, std::vector<unsigned char>(text.begin(),text.end())); }
static std::vector<unsigned char> fixturePng(const fs::path& path, int width, int height) {
    (void)artworkInspectionGdiPlusToken(); fs::create_directories(path.parent_path());
    Gdiplus::Bitmap image(width,height,PixelFormat32bppARGB); Gdiplus::Graphics canvas(&image);
    canvas.Clear(Gdiplus::Color(255,35,60,100));
    require(image.Save(path.c_str(), &pngEncoderClsid(), nullptr)==Gdiplus::Ok,"Could not create PNG fixture");
    return readBinaryFile(path);
}
struct ArtworkFixture {
    fs::path root,data,exe,output; json manual=json::object();
    std::map<std::string,std::vector<unsigned char>> images;
    std::vector<std::string> requests;
    std::wstring title=L"Fixture Game";
    std::string caption;
    bool withIgdbId=true, landscapeOnly=false, autoFormatImages=false, libraryAssets=false;
    bool igdbImages=true, baiduImages=true, steamImages=true, failIdentity=false, unrelated=false, failAll=false;
    bool networkOutage=false, malformedBaiduSearch=false;
    std::optional<std::vector<unsigned char>> imageResponseOverride;
    std::optional<json> libraryAssetsOverride;
    ArtworkFixture(const fs::path& p):root(p),data(p/L"data"),exe(p/L"Fixture Game"/L"FixtureGame.exe"),output(p/L"output") {
        fixtureText(exe,"not executable; isolated fixture");
        images["tall"]=fixturePng(p/L"fixtures"/L"tall.png",600,900);
        images["long"]=fixturePng(p/L"fixtures"/L"long.png",1196,559);
        images["hero"]=fixturePng(p/L"fixtures"/L"hero.png",1920,620);
        images["logo"]=fixturePng(p/L"fixtures"/L"logo.png",960,540);
        images["icon"]=fixturePng(p/L"fixtures"/L"icon.png",512,512);
    }
    std::string url(const std::string& type,bool baidu=false) const { return std::string(baidu?"https://fixture.baidu.invalid/":"https://images.igdb.com/igdb/image/upload/t_original/")+toUtf8(root.filename().wstring())+"-"+type+".png"+(baidu&&autoFormatImages?"?fmt=auto":""); }
    json game() const {
        json g={{"id",7},{"name",toUtf8(title)},{"game_type",0}};
        if(igdbImages) {
            g["cover_expanded"]={{"url",url("tall")},{"width",600},{"height",900}};
            g["artworks_expanded"]=json::array({{{"url",url("hero")},{"width",1920},{"height",620}}});
            g["screenshots_expanded"]=json::array({{{"url",url("long")},{"width",1196},{"height",559}}});
        }
        return g;
    }
    HttpResponse response(const json& value) const { const auto text=value.dump();return HttpResponse{200,{},std::vector<unsigned char>(text.begin(),text.end())}; }
    void install() {
        SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());
        writeJsonAtomic(data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(exe),manual}}}});
        artworkTestTransport=[this](const std::string& provider,const std::string&,const std::string& request,const std::vector<unsigned char>&)->HttpResponse {
            requests.push_back(provider+":"+request);
            if(failAll) throw std::runtime_error("simulated offline");
            if(networkOutage) throw HttpTransportError("simulated network outage",ERROR_WINHTTP_TIMEOUT,true);
            if(request.find("/api/public/search/autocomplete")!=std::string::npos)
                return response({{"success",true},{"data",json::array({{{"id",9022},{"name",toUtf8(title)},{"types",{"steam"}}}})}});
            if(request.find("/api/public/search/assets")!=std::string::npos)
                return response({{"success",true},{"data",{{"assets",json::array({{{"id",1},{"url","https://cdn2.steamgriddb.com/icon/fixture.png"},{"thumb","https://cdn2.steamgriddb.com/icon_thumb/fixture.png"},{"width",512},{"height",512}}})}}}});
            if(request.find("/api/igdb/game/")!=std::string::npos) {
                if(failIdentity) throw std::runtime_error("IGDB identity unavailable");
                return response({{"data",game()}});
            }
            if(request.find("/api/igdb/search")!=std::string::npos) return response({{"data",igdbImages?json::array({game()}):json::array()}});
            if(request.find("IStoreBrowseService/GetItems")!=std::string::npos) {
                if(libraryAssetsOverride)return response(*libraryAssetsOverride);
                if(!libraryAssets)return response({{"response",{{"store_items",json::array()}}}});
                json assets={{"asset_url_format","steam/apps/77/${FILENAME}?t=123"},
                    {"library_capsule","hash-cover/library_capsule.jpg"},{"library_capsule_2x","hash-cover/library_capsule_2x.jpg"},
                    {"header","hash-header/header.jpg"},{"library_hero","hash-hero/library_hero.jpg"}};
                return response({{"response",{{"store_items",json::array({{{"appid",77},{"success",1},{"assets",assets}}})}}}});
            }
            if(request.find("api/appdetails")!=std::string::npos) return response({{"77",{{"success",true},{"data",{{"type","game"},{"name","Fixture Game"},{"steam_appid",77}}}}}});
            if(request.find("image.baidu.com/search/acjson")!=std::string::npos) {
                if(malformedBaiduSearch) {const std::string text="<html>temporary gateway failure</html>";return HttpResponse{200,{},std::vector<unsigned char>(text.begin(),text.end())};}
                json candidates=json::array();
                if(baiduImages) for(const auto& type:{"tall","long","hero"}) {
                    if(landscapeOnly && std::string(type)=="tall") continue;
                    const auto info=*inspectImage(images[type]);
                    candidates.push_back({{"objURL",url(type,true)},{"width",info.width},{"height",info.height},{"fromPageTitle",unrelated?"Unrelated Racing Game":(caption.empty()?toUtf8(title):caption)}});
                }
                return response({{"data",candidates}});
            }
            if(imageResponseOverride) return HttpResponse{200,{},*imageResponseOverride};
            if(autoFormatImages&&request.find("fmt=auto")!=std::string::npos)
                return HttpResponse{200,{},std::vector<unsigned char>{'R','I','F','F',0,0,0,0,'W','E','B','P'}};
            const bool isSteam=provider.starts_with("steam");
            if((isSteam&&!steamImages)||(!isSteam&&!baiduImages&&provider=="baidu-image")) return HttpResponse{404};
            if(request.find("cdn2.steamgriddb.com/icon/")!=std::string::npos) return HttpResponse{200,{},images["icon"]};
            for(const auto& type:{"tall","long","hero","logo"}) {
                if(request.find(std::string("-")+type+".png")!=std::string::npos ||
                    (isSteam&&((std::string(type)=="tall"&&(request.find("600x900")!=std::string::npos||request.find("library_capsule")!=std::string::npos))||
                    (std::string(type)=="long"&&request.find("header.jpg")!=std::string::npos)||
                    (std::string(type)=="hero"&&request.find("library_hero")!=std::string::npos)||
                    (std::string(type)=="logo"&&request.find("logo")!=std::string::npos)))) {
                    return HttpResponse{200,{},images[type]};
                }
            }
            return HttpResponse{404};
        };
    }
    json run(bool steam=false,bool metadata=false,bool steamOnly=false) {
        install(); std::vector<std::wstring> args={toWide(toUtf8((root/L"bin"/L"SteamArtworkLab.exe").wstring())),exe.wstring(),output.wstring(),L"--name",title,L"--platform",L"pc"};
        if(steam) args.insert(args.end(),{L"--steam-id",L"77"});
        else { args.push_back(L"--igdb-artwork-only"); if(withIgdbId) args.insert(args.end(),{L"--igdb-id",L"7"}); }
        if(metadata) args.push_back(L"--metadata-only");
        if(steamOnly) args.push_back(L"--steam-artwork-only");
        std::vector<wchar_t*> argv;for(auto& arg:args)argv.push_back(arg.data());
        workerMainNotUsedBySelftest(static_cast<int>(argv.size()),argv.data());
        require(fs::is_regular_file(durable()),"Worker did not publish visual-only manifest");
        if (metadata) {
            for (const auto& entry : fs::recursive_directory_iterator(output)) {
                if (entry.path().filename() != L"manifest.json") continue;
                const auto result = loadJsonDocument(entry.path());
                if (jsonBoolSafe(result, "metadataOnly", false)) return result;
            }
            throw std::runtime_error("No metadata-only worker result");
        }
        return loadJsonDocument(durable());
    }
    json slot(const json& m,const char* type) const { for(const auto& a:m.at("artwork"))if(jsonStringOr(a,"type")==type)return a;return json::object(); }
    fs::path durable() const {return data/L"artwork"/toWide(gameDataId(persistentGameDirectoryForExecutable(data,exe)))/L"manifest.json";}
};
int wmain(int argc,wchar_t** argv) {
    try {
        require(argc==2,"Pass a new Build fixture directory");const auto root=fs::absolute(argv[1]);
        const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        require(pathWithin(root,build)&&!fs::exists(root),"Unsafe/reused fixture root");fs::create_directories(root);
        const auto priorRoot=custom_steam_library::configuredDataRoot();json tests=json::array();
        auto test=[&](const char* name,const std::function<void(const fs::path&)>& action){
            const auto p=root/(L"c"+std::to_wstring(tests.size()));fs::create_directories(p);
            try{action(p);tests.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){tests.push_back({{"name",name},{"passed",false},{"error",e.what()}});}
            artworkTestTransport={};selftestSteamRunning=false;selftestMoveFault={};
        };
        test("igdb-artwork-without-steamid-is-automatically-applied",[](const auto& p){ArtworkFixture f(p);const auto m=f.run();require(m["successCount"]>=3,"IGDB images were not applied");require(positiveJsonInt(m["match"],"appId")==0,"Artwork became a Steam identity");require(fs::is_regular_file(f.durable()),"No durable manifest");});
        test("steamgriddb-icon-is-optional-automatic-artwork",[](const auto& p){ArtworkFixture f(p);const auto m=f.run(true);const auto icon=f.slot(m,"icon");require(icon["ok"]&&icon["provider"]=="steamgriddb","SteamGridDB icon was not automatically applied");});
        test("manual-cover-does-not-block-auto-long-and-wallpaper",[](const auto& p){ArtworkFixture f(p);f.manual["cover"]={{"file",toUtf8((p/L"fixtures"/L"tall.png").wstring())}};const auto m=f.run();require(f.slot(m,"tall")["provider"]=="manual-override","Manual cover replaced");require(f.slot(m,"long")["ok"]&&f.slot(m,"hero")["ok"],"Other slots were blocked");});
        test("manual-wallpaper-does-not-block-auto-cover",[](const auto& p){ArtworkFixture f(p);f.manual["wallpaper"]={{"file",toUtf8((p/L"fixtures"/L"hero.png").wstring())}};const auto m=f.run();require(f.slot(m,"hero")["provider"]=="manual-override"&&f.slot(m,"tall")["ok"],"Manual wallpaper blocked cover");});
        test("manual-deleted-slot-is-not-recreated-even-with-stale-file",[](const auto& p){ArtworkFixture f(p);f.manual={{"cover",{{"file",toUtf8((p/L"fixtures"/L"tall.png").wstring())}}},{"artworkProtection",{{"cover","deleted"}}}};const auto m=f.run();require(!f.slot(m,"tall")["ok"].template get<bool>(),"Deleted cover recreated");require(f.slot(m,"hero")["ok"],"Deletion blocked other slots");});
        test("missing-manual-file-allows-automatic-fallback",[](const auto& p){ArtworkFixture f(p);f.manual={{"cover",{{"file",toUtf8((p/L"missing.png").wstring())}}},{"artworkProtection",{{"cover","manual"}}}};const auto m=f.run();require(f.slot(m,"tall")["ok"]&&f.slot(m,"tall")["provider"]!="manual-override","Missing file blocked fallback");});
        test("automatic-igdb-to-baidu-fallback-with-no-steamid",[](const auto& p){ArtworkFixture f(p);f.igdbImages=false;const auto m=f.run();for(const auto* type:{"tall","long","hero"})require(f.slot(m,type)["provider"]=="baidu-image"&&f.slot(m,type)["ok"],"Baidu slot not applied");});
        test("baidu-still-works-when-igdb-identity-fails",[](const auto& p){ArtworkFixture f(p);f.igdbImages=false;f.failIdentity=true;const auto m=f.run();require(m["successCount"]>=3,"Failed identity prevented visual fallback");require(positiveJsonInt(m["match"],"appId")==0&&positiveJsonInt(m["match"],"igdbId")==0,"Search hit granted identity");});
        test("japanese-old-game-with-neither-steamid-nor-igdb-id-gets-baidu-images",[](const auto& p){ArtworkFixture f(p);f.title=L"維新の嵐 幕末志士伝";f.exe=p/L"Ishin2"/L"ISHIN2.EXE";fixtureText(f.exe,"isolated old-game fixture");f.withIgdbId=false;f.igdbImages=false;f.caption="維新の嵐幕末志士伝攻略・画像集";f.manual["name"]=toUtf8(f.title);const auto m=f.run();for(const auto* type:{"tall","long","hero"})require(f.slot(m,type)["provider"]=="baidu-image"&&f.slot(m,type)["ok"],"Japanese no-ID fallback missing");require(positiveJsonInt(m["match"],"appId")==0&&positiveJsonInt(m["match"],"igdbId")==0,"Artwork invented an identity");require(m["match"]["formalName"]==toUtf8(f.title),"Fallback lost readable title");});
        test("japanese-title-applies-chinese-caption-artwork-without-identity",[](const auto& p){ArtworkFixture f(p);f.title=L"維新の嵐 幕末志士伝";f.withIgdbId=false;f.igdbImages=false;f.caption="pc版 维新之岚 幕末志士传汉化测试";const auto m=f.run();require(m["successCount"]>=3,"Chinese captions for Japanese title were discarded");require(positiveJsonInt(m["match"],"appId")==0&&positiveJsonInt(m["match"],"igdbId")==0,"Caption normalization granted identity");});
        test("automatic-baidu-download-uses-the-working-manual-jpeg-format",[](const auto& p){ArtworkFixture f(p);f.withIgdbId=false;f.igdbImages=false;f.autoFormatImages=true;const auto m=f.run();require(m["successCount"]>=3,"Automatic format negotiation differed from manual download");bool jpeg=false;for(const auto& request:f.requests){require(request.find("fmt=auto")==std::string::npos,"Unsupported image format requested");jpeg=jpeg||request.find("fmt=jpeg")!=std::string::npos;}require(jpeg,"No decoder-compatible format requested");});
        test("download-format-normalization-preserves-other-url-parameters",[](const auto&){require(supportedArtworkRequestUrl("https://img2.baidu.com/it/u=1&fmt=auto&f=JPEG?w=889&h=500")=="https://img2.baidu.com/it/u=1&fmt=jpeg&f=JPEG?w=889&h=500","Baidu nested query not normalized");require(supportedArtworkRequestUrl("https://example.invalid/fmt=auto.png?signature=fmt=auto")=="https://example.invalid/fmt=auto.png?signature=fmt=auto","Non-format/signed parameter changed");const std::wstring accept=kSupportedArtworkAccept;require(accept.find(L"webp")==std::wstring::npos&&accept.find(L"avif")==std::wstring::npos,"Decoder advertises unsupported formats");});
        test("landscape-only-old-game-gets-tagged-portrait-last-resort",[](const auto& p){ArtworkFixture f(p);f.title=L"維新の嵐 幕末志士伝";f.withIgdbId=false;f.igdbImages=false;f.landscapeOnly=true;f.caption="pc版 维新之岚 幕末志士传汉化测试";auto m=f.run();const auto cover=f.slot(m,"tall");require(cover["ok"]&&cover["width"]==600&&cover["height"]==900,"Landscape-only cover fallback missing");require(cover["sourceRole"]=="automatic-landscape-cover-fallback"&&cover["lowQualityOnlyCandidate"],"Derived cover masquerades as original/preferred cover");require(m["successCount"]>=3,"Backgrounds not applied with adapted cover");const auto candidate=ArtworkCandidate{jsonStringOr(cover,"url"),"baidu-image","automatic-landscape-cover-fallback","",false,jsonStringOr(cover,"url"),100,0,true};const auto cached=loadArtworkUrlCache(f.data,candidate,artworkSpecs().front());require(cached&&cached->quality.band==ArtworkQualityBand::UniqueFallback,"URL cache promoted adapted cover quality");});
        test("derived-cover-survives-offline-and-upgrades-to-real-portrait",[](const auto& p){ArtworkFixture f(p);f.withIgdbId=false;f.igdbImages=false;f.landscapeOnly=true;auto m=f.run();const auto before=readBinaryFile(f.slot(m,"tall")["portableFile"].template get<std::string>());f.failAll=true;m=f.run();require(f.slot(m,"tall")["lowQualityOnlyCandidate"]&&readBinaryFile(f.slot(m,"tall")["portableFile"].template get<std::string>())==before,"Offline retry lost/misclassified derived cover");f.failAll=false;f.landscapeOnly=false;m=f.run();require(f.slot(m,"tall")["sourceRole"]=="automatic-search-fallback"&&!f.slot(m,"tall")["lowQualityOnlyCandidate"].template get<bool>(),"Derived cover blocked a real portrait upgrade");});
        test("proper-baidu-portrait-wins-over-landscape-adaptation",[](const auto& p){ArtworkFixture f(p);f.withIgdbId=false;f.igdbImages=false;const auto m=f.run();require(f.slot(m,"tall")["sourceRole"]=="automatic-search-fallback"&&!f.slot(m,"tall")["lowQualityOnlyCandidate"].template get<bool>(),"A screenshot replaced proper portrait artwork");});
        test("actual-ishin-baidu-captions-match-full-title-not-unrelated-results",[](const auto&){const std::string query="維新の嵐 幕末志士伝";for(const auto* caption:{"pc版 维新之岚 幕末志士传汉化测试","《维新之岚 · 幕末志士传》简体中文汉化版","《维新之岚幕末志士传》关于汉化事件的相关澄清说明"})require(automaticArtworkSearchTitleRelevant(query,caption),"Actual Baidu caption discarded");for(const auto* caption:{"土星ss《维新之岚》试玩","維新の嵐","《幕末替身传说》热血战斗新番推荐","幕末尽忠报国烈士传miburo"})require(!automaticArtworkSearchTitleRelevant(query,caption),"Wrong original game/movie/anime was accepted");});
        test("no-id-artwork-prefers-pe-product-name-over-ishin2-alias",[](const auto& p){const auto exe=p/L"Ishin2"/L"ISHIN2.EXE";const auto rounds=buildNameRounds(exe,{{"ProductName","維新の嵐 幕末志士伝"},{"FileDescription","ISHIN2"}},false,"","");require(artworkFallbackTitle("",rounds,{"Ishin2","ISHIN2"},exe)=="維新の嵐 幕末志士伝","PE name ignored for artwork");require(artworkFallbackTitle("My Manual Name",rounds,{"Ishin2"},exe)=="My Manual Name","Manual name priority lost");});
        test("highlighted-japanese-web-caption-is-artwork-relevant",[](const auto&){require(automaticArtworkSearchTitleRelevant("維新の嵐 幕末志士伝","<strong>維新の嵐</strong>幕末志士伝の画像"),"Highlight/compact title rejected");require(!automaticArtworkSearchTitleRelevant("維新の嵐 幕末志士伝","無関係のレースゲーム画像"),"Unrelated CJK image accepted");require(!automaticArtworkSearchTitleRelevant("Ishin2","Ishin20 pictures"),"Numeric title substring false positive");});
        test("no-id-fallback-with-empty-igdb-does-not-search-steam",[](const auto& p){ArtworkFixture f(p);f.withIgdbId=false;f.igdbImages=false;const auto m=f.run();require(m["successCount"]>=3,"Empty IGDB prevented Baidu");for(const auto& request:f.requests)require(!request.starts_with("steam"),"Visual fallback searched/reverified Steam identity");});
        test("hero-slot-uses-the-same-horizontal-search-rule-as-wallpaper",[](const auto&){require(artworkSearchMinimumWidth("hero")==artworkSearchMinimumWidth("wallpaper")&&artworkSearchMinimumHeight("hero")==artworkSearchMinimumHeight("wallpaper"),"Hero was treated as portrait");});
        test("unrelated-baidu-images-are-not-auto-applied",[](const auto& p){ArtworkFixture f(p);f.igdbImages=false;f.unrelated=true;const auto m=f.run();require(m["successCount"]==0,"Unrelated fallback images applied");});
        test("existing-slots-survive-offline-retry-and-count-is-recomputed",[](const auto& p){ArtworkFixture f(p);auto m=f.run();const auto before=readBinaryFile(f.slot(m,"tall")["portableFile"].template get<std::string>());m["successCount"]=0;m["required"]={{"tall",false},{"hero",false}};writeJsonAtomic(f.durable(),m);f.failAll=true;m=f.run();require(m["successCount"]>=3,"Preserved slots not counted");require(readBinaryFile(f.slot(m,"tall")["portableFile"].template get<std::string>())==before,"Retry replaced good image");});
        test("partial-visual-manifest-with-null-identity-fields-is-readable",[](const auto& p){ArtworkFixture f(p);auto m=f.run();m["ok"]=false;m["successCount"]=0;m["match"]={{"contentRelation",nullptr},{"identityStatus",nullptr},{"appId",nullptr},{"igdbId",nullptr}};writeJsonAtomic(f.durable(),m);const auto read=portableArtworkManifest(f.data,{{"gameDirectory",toUtf8(f.exe.parent_path().wstring())}},f.exe);require(read.has_value(),"Partial visual manifest hidden");require((*read)["successCount"]>=3,"Stale manifest count not repaired");});
        test("steam-images-have-priority-over-igdb-and-baidu",[](const auto& p){ArtworkFixture f(p);const auto m=f.run(true);for(const auto* type:{"tall","long","hero"})require(jsonStringOr(f.slot(m,type),"provider").starts_with("steam"),"Steam priority lost");});
        test("steam-image-failure-falls-through-to-igdb",[](const auto& p){ArtworkFixture f(p);f.steamImages=false;const auto m=f.run(true);for(const auto* type:{"tall","long","hero"})require(f.slot(m,type)["provider"]=="playnite-igdb"&&f.slot(m,type)["ok"],"Steam failure did not reach IGDB");});
        test("usable-provider-tiers-outweigh-pixel-score",[](const auto&){DownloadedArtworkCandidate a,b;a.candidate.provider="steam-cdn";b.candidate.provider="playnite-igdb";a.quality.band=ArtworkQualityBand::Usable;b.quality.band=ArtworkQualityBand::Preferred;require(betterArtworkCandidate(a,b),"IGDB replaced usable Steam");a.candidate.provider="playnite-igdb";b.candidate.provider="baidu-image";require(betterArtworkCandidate(a,b),"Baidu replaced usable IGDB");});
        test("automatic-candidate-filter-rejects-bad-title-and-shape",[](const auto&){const auto spec=artworkSpecs().front();std::vector<ArtworkCandidate> output;json hits={{"candidates",json::array({{{"url","https://fixture.invalid/a.png"},{"provider","baidu-image"},{"title","Unrelated Racing Game"},{"width",600},{"height",900}},{{"url","https://fixture.invalid/b.png"},{"provider","baidu-image"},{"title","Fixture Game"},{"width",30},{"height",30}}})}};require(appendAutomaticArtworkSearchCandidates(hits,"Fixture Game",spec,output)==0,"Bad automatic candidate accepted");});
        test("deleted-slot-is-settled-without-claiming-image-availability",[](const auto& p){ArtworkFixture f(p);auto m=f.run();const auto state=artworkReadiness(m,{{"artworkProtection",{{"cover","deleted"}}}});require(!state["tall"].template get<bool>()&&state["automaticSlotsComplete"].template get<bool>(),"Deleted slot repeatedly scheduled");});
        test("nullable-visual-identity-does-not-break-whole-steam-plan",[](const auto& p){ArtworkFixture f(p);auto m=f.run();m["ok"]=false;m["successCount"]=0;m["metadata"]=nullptr;m["match"]={{"contentRelation",nullptr},{"identityStatus",nullptr},{"appId",nullptr},{"igdbId",nullptr},{"formalName",nullptr},{"displayName",nullptr},{"primaryProvider",nullptr}};writeJsonAtomic(f.durable(),m);const auto scan=f.data/L"state"/L"library-scan.json";writeJsonAtomic(scan,{{"games",json::array({{{"gameDirectory",toUtf8(f.exe.parent_path().wstring())},{"primaryExecutable",toUtf8(f.exe.wstring())},{"status","ready"},{"contentType","game"}}})}});SteamShortcutTarget target{p/L"Steam"/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf","19627"};fs::create_directories(target.path.parent_path());const auto plan=buildSteamLibraryAddPlan(f.data,scan,target,f.data/L"state"/L"plan.json");require(plan["items"].size()==1,"Visual-only item lost from plan");require(plan["items"][0]["status"]=="waiting-steam-verification","Visual images granted identity or were dropped");require(plan["items"][0]["artwork"]["minimumComplete"].template get<bool>(),"Readable images not ready");});
        test("metadata-only-retry-keeps-final-image-count-and-durable-bytes",[](const auto& p){ArtworkFixture f(p);auto m=f.run();const auto before=readBinaryFile(f.durable());m=f.run(false,true);require(readBinaryFile(f.durable())==before,"Identity-only pass rewrote durable artwork");require(m["successCount"]>=3&&m["required"]["tall"].template get<bool>(),"Identity-only result reset artwork count");});
        test("explicit-deletion-is-not-reported-as-download-failure",[](const auto& p){ArtworkFixture f(p);f.manual={{"artworkProtection",{{"cover","deleted"},{"wallpaper","deleted"}}}};const auto m=f.run();require(m["ok"].template get<bool>()&&!m["required"]["tall"].template get<bool>(),"Deleted slot considered failure");});

        test("japanese-power-up-edition-matches-chinese-full-title",[](const auto&){const std::string q="提督の決断Ⅳ パワーアップキット";for(const auto* text:{"提督的决断4威力加强版","提督之决断Ⅳ威力增强版","《提督的决断4威力加强版》截图"})require(automaticArtworkSearchTitleRelevant(q,text),"Localized edition caption rejected");for(const auto* text:{"提督的决断4","提督的决断3威力加强版","第14回mmd杯本选提督的决断","提督的决断40威力加强版"})require(!automaticArtworkSearchTitleRelevant(q,text),"Wrong sequel/base game image accepted");});
        test("small-four-three-matched-images-fill-all-three-slots-without-id",[](const auto& p){ArtworkFixture f(p);f.title=L"提督の決断Ⅳ パワーアップキット";f.caption="提督的决断4威力加强版";f.withIgdbId=false;f.igdbImages=false;f.landscapeOnly=true;for(const auto* type:{"long","hero"})f.images[type]=fixturePng(p/L"fixtures"/(toWide(type)+L"-small.png"),550,412);auto m=f.run();require(m["successCount"]>=3,"Small 4:3 image did not fill slots");for(const auto* type:{"tall","long","hero"}){const auto a=f.slot(m,type);require(a["ok"]&&a["lowQualityOnlyCandidate"],"Adapted screenshot not marked fallback");require(jsonStringOr(a,"sourceRole").find("fallback")!=std::string::npos,"Adapted source provenance lost");}require(positiveJsonInt(m["match"],"appId")==0&&positiveJsonInt(m["match"],"igdbId")==0,"Visual fallback invented game identity");f.failAll=true;m=f.run();require(m["successCount"]>=3&&f.slot(m,"hero")["lowQualityOnlyCandidate"],"Offline retry lost adapted background");});
        test("small-four-three-base-game-cannot-fill-power-up-edition",[](const auto& p){ArtworkFixture f(p);f.title=L"提督の決断Ⅳ パワーアップキット";f.caption="提督的决断4";f.withIgdbId=false;f.igdbImages=false;f.landscapeOnly=true;for(const auto* type:{"long","hero"})f.images[type]=fixturePng(p/L"fixtures"/(toWide(type)+L"-small.png"),550,412);const auto m=f.run();require(m["successCount"]==0,"Base-game images applied to different edition");});


        test("verified-identity-cache-promotes-old-visual-artwork-plan",[](const auto& p){ArtworkFixture f(p);auto m=f.run();m["match"]={{"primaryProvider","artwork-fallback"},{"identityStatus","unverified"},{"appId",nullptr}};writeJsonAtomic(f.durable(),m);const auto before=readBinaryFile(f.durable());json match={{"appId",4012810},{"storefrontAppId",4012810},{"formalName","STEINS;GATE RE:BOOT"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"primaryProvider","steam"},{"resolverVersion",STEAM_RESOLVER_VERSION}};const auto cache=f.data/L"cache"/L"identity"/L"fixture.json";writeJsonAtomic(cache,{{"executable",toUtf8(f.exe.wstring())},{"match",match}});const auto scan=f.data/L"state"/L"library-scan.json";writeJsonAtomic(scan,{{"games",json::array({{{"gameDirectory",toUtf8(f.exe.parent_path().wstring())},{"primaryExecutable",toUtf8(f.exe.wstring())},{"status","ready"},{"contentType","game"}}})}});SteamShortcutTarget target{p/L"Steam"/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf","19627"};fs::create_directories(target.path.parent_path());auto plan=buildSteamLibraryAddPlan(f.data,scan,target,f.data/L"state"/L"plan.json");require(plan["items"][0]["status"]=="ready-to-add"&&plan["items"][0]["steamStoreAppId"]==4012810,"Cached verified identity blocked by visual manifest");require(plan["items"][0]["artwork"]["minimumComplete"].template get<bool>()&&readBinaryFile(f.durable())==before,"Identity promotion rewrote artwork");match["steamVerificationStatus"]="network-pending";writeJsonAtomic(cache,{{"executable",toUtf8(f.exe.wstring())},{"match",match}});plan=buildSteamLibraryAddPlan(f.data,scan,target,f.data/L"state"/L"plan.json");require(plan["items"][0]["status"]=="waiting-steam-verification","Unverified cache promoted plan");});
        test("legacy-identity-index-loads-only-matching-verified-source",[](const auto& p){ArtworkFixture f(p);f.install();json match={{"appId",4012810},{"formalName","STEINS;GATE RE:BOOT"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"primaryProvider","steam"},{"resolverVersion",STEAM_RESOLVER_VERSION}};const auto source=p/L"identity"/L"manifest.json";const auto cache=f.data/L"cache"/L"identity"/L"old.json";writeJsonAtomic(source,{{"exe",toUtf8(f.exe.wstring())},{"match",match}});writeJsonAtomic(cache,{{"executable",toUtf8(f.exe.wstring())},{"steamAppId",4012810},{"manifestPath",toUtf8(source.wstring())}});require(cachedVerifiedSteamIdentity(f.data,f.exe).has_value(),"Existing legacy identity index not migrated");writeJsonAtomic(source,{{"exe",toUtf8((p/L"Other"/L"Other.exe").wstring())},{"match",match}});require(!cachedVerifiedSteamIdentity(f.data,f.exe).has_value(),"Foreign executable identity accepted");});
        test("manual-cleared-id-and-unknown-cache-version-remain-pending",[](const auto& p){ArtworkFixture f(p);f.run();json match={{"appId",4012810},{"formalName","STEINS;GATE RE:BOOT"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"primaryProvider","steam"},{"resolverVersion","unknown-future"}};const auto cache=f.data/L"cache"/L"identity"/L"fixture.json";writeJsonAtomic(cache,{{"executable",toUtf8(f.exe.wstring())},{"match",match}});require(!cachedVerifiedSteamIdentity(f.data,f.exe).has_value(),"Unknown resolver cache accepted");match["resolverVersion"]=STEAM_RESOLVER_VERSION;writeJsonAtomic(cache,{{"executable",toUtf8(f.exe.wstring())},{"match",match}});writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(f.exe),{{"idCleared",true}}}}}});const auto scan=f.data/L"state"/L"library-scan.json";writeJsonAtomic(scan,{{"games",json::array({{{"gameDirectory",toUtf8(f.exe.parent_path().wstring())},{"primaryExecutable",toUtf8(f.exe.wstring())},{"status","ready"}}})}});SteamShortcutTarget target{p/L"Steam"/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf","19627"};fs::create_directories(target.path.parent_path());const auto plan=buildSteamLibraryAddPlan(f.data,scan,target,f.data/L"state"/L"plan.json");require(plan["items"][0]["status"]=="waiting-steam-verification","Manual clear undone by cached identity");});

        test("deduplicated-manual-bin-root-retains-former-root-artwork",[](const auto& p){ArtworkFixture f(p);auto m=f.run();const auto nested=f.exe.parent_path()/L"bin";auto game=json{{"gameDirectory",toUtf8(nested.wstring())},{"primaryExecutable",toUtf8(f.exe.wstring())},{"alternateGameDirectories",json::array({toUtf8(f.exe.parent_path().wstring())})}};const auto found=portableArtworkManifest(f.data,game,f.exe);require(found&&(*found)["successCount"]>=3,"Deduplication stranded root artwork");m["exe"]=toUtf8((p/L"Foreign"/L"Game.exe").wstring());writeJsonAtomic(f.durable(),m);require(!portableArtworkManifest(f.data,game,f.exe).has_value(),"Foreign EXE artwork reused through directory alias");});
        test("modern-hashed-steam-library-cover-is-applied-automatically",[](const auto& p){ArtworkFixture f(p);f.libraryAssets=true;const auto m=f.run(true);const auto cover=f.slot(m,"tall");require(cover["ok"]&&cover["provider"]=="steam-store"&&jsonStringOr(cover,"url").find("/77/hash-cover/library_capsule")!=std::string::npos,"Modern Steam library cover not applied");require(jsonStringOr(cover,"sourceRole").starts_with("library-assets-"),"Hashed-cover provenance lost");require(std::none_of(f.requests.begin(),f.requests.end(),[](const auto& r){return r.find("/library_600x900")!=std::string::npos;}),"Known obsolete fixed covers still downloaded");const auto count=std::count_if(f.requests.begin(),f.requests.end(),[](const auto& r){return r.find("IStoreBrowseService/GetItems")!=std::string::npos;});require(count==1,"Asset discovery repeated for every slot");});
        test("manual-cover-search-reuses-real-library-capsule-addresses",[](const auto& p){ArtworkFixture f(p);f.libraryAssets=true;f.install();g_workerDataRoot=f.data;const auto result=steamIdArtworkSearch("77","cover");require(result["candidates"].size()==2,"Manual cover search returned missing/fake covers");for(const auto& c:result["candidates"])require(jsonStringOr(c,"url").find("/77/hash-cover/library_capsule")!=std::string::npos,"Manual gallery contains guessed cover URL");require(result["hashedStoreAssets"].template get<bool>(),"Modern hashed assets not detected");});
        test("library-assets-format-rejects-wrong-appid-path-traversal-and-unsupported-format",[](const auto&){json assets={{"asset_url_format","steam/apps/77/${FILENAME}?t=123"},{"library_capsule","hash/library_capsule.jpg"}};require(!steamLibraryAssetUrl(assets,77,"library_capsule").empty(),"Valid Steam hash rejected");require(steamLibraryAssetUrl(assets,78,"library_capsule").empty(),"Foreign AppID accepted");for(const auto* bad:{"../other/library.jpg","https://evil.invalid/image.jpg","hash/library.webp","hash/image.jpg?redirect=bad"}){assets["library_capsule"]=bad;require(steamLibraryAssetUrl(assets,77,"library_capsule").empty(),"Unsafe or unsupported asset filename accepted");}});
        test("wrong-storebrowse-appid-is-not-artwork-evidence",[](const auto& p){ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return f.response({{"response",{{"store_items",json::array({{{"appid",78},{"success",1},{"assets",{{"asset_url_format","steam/apps/78/${FILENAME}"},{"library_capsule","hash/library_capsule.jpg"}}}}})}}}});};json attempts=json::array();require(fetchSteamLibraryAssets(77,attempts).empty(),"Foreign StoreBrowse asset selected");});
        test("cached-official-library-assets-are-available-offline",[](const auto& p){ArtworkFixture f(p);f.libraryAssets=true;f.install();g_workerDataRoot=f.data;json attempts=json::array();auto assets=fetchSteamLibraryAssets(77,attempts);require(!assets.empty(),"Official asset index not cached");g_steamLibraryAssetsSession.clear();f.failAll=true;attempts=json::array();assets=fetchSteamLibraryAssets(77,attempts);require(!steamLibraryAssetUrl(assets,77,"library_capsule").empty()&&attempts.size()==1&&attempts[0]["cacheHit"],"Offline official asset cache not reused");});
        test("library-assets-network-fault-falls-back-without-throwing-or-requerying-each-slot",[](const auto& p){ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;f.failAll=true;json attempts=json::array();require(fetchSteamLibraryAssets(77,attempts).empty(),"Offline discovery invented assets");const auto count=f.requests.size();require(fetchSteamLibraryAssets(77,attempts).empty()&&f.requests.size()==count,"Offline endpoint requested again for another slot");});
        test("manual-long-search-includes-authoritative-steam-header",[](const auto& p){
            ArtworkFixture f(p);f.libraryAssets=true;f.install();g_workerDataRoot=f.data;
            const auto result=steamIdArtworkSearch("77","long");bool header=false;
            for(const auto& candidate:result["candidates"]){
                if(jsonStringOr(candidate,"sourceRole")=="library-assets-header"){
                    header=true;require(candidate["width"]==460&&candidate["height"]==215,"Header claimed wallpaper dimensions");
                }
                require(jsonStringOr(candidate,"sourceRole")!="store-screenshot","Long cover was replaced with store screenshots");
            }
            require(header,"Manual long search lost Steam header field");
        });
        test("header-only-library-assets-cache-is-reused",[](const auto& p){
            ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;
            const json assets={{"asset_url_format","steam/apps/77/${FILENAME}?t=1"},{"header","hash/header.jpg"}};
            writeJsonAtomic(f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json",{{"steamAppId",77},{"fetchedAt",unixTimeMs()},{"assets",assets}});
            g_steamLibraryAssetsSession.clear();json attempts=json::array();const auto result=fetchSteamLibraryAssets(77,attempts);
            require(result==assets&&f.requests.empty(),"Header-only cache was discarded because portrait is missing");
        });
        test("manual-long-and-wallpaper-have-distinct-steam-candidates",[](const auto& p){
            ArtworkFixture f(p);f.libraryAssets=true;f.install();g_workerDataRoot=f.data;
            const auto longResult=steamIdArtworkSearch("77","long"),heroResult=steamIdArtworkSearch("77","wallpaper");
            require(longResult["candidates"][0]["sourceRole"]=="library-assets-header","Header not preferred for long");
            require(heroResult["candidates"][0]["sourceRole"]=="library-assets-library_hero","Hero not preferred for wallpaper");
        });

        test("host-steam-artwork-only-still-collects-optional-sgdb-icon",[](const auto& p){ArtworkFixture f(p);const auto m=f.run(true,false,true);require(f.slot(m,"icon")["ok"]&&f.slot(m,"icon")["provider"]=="steamgriddb","Host automatic second stage skipped SGDB icon");});
        test("icon-does-not-consume-library-logo-fields",[](const auto& p){
            ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;
            const json assets={{"asset_url_format","steam/apps/77/${FILENAME}"},{"library_logo","logo.png"}};
            writeJsonAtomic(f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json",{{"steamAppId",77},{"fetchedAt",unixTimeMs()},{"assets",assets}});
            g_steamLibraryAssetsSession.clear();ResolvedGame game;game.appId=77;game.baseGameAppId=77;
            const auto candidates=artworkUrls(game,artworkSpecs().back());require(candidates.empty(),"Logo downloaded into icon slot");
        });
        test("official-community-icon-is-bound-to-exact-appid",[](const auto& p){
            ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;
            const json assets={{"community_icon",std::string(40,'a')}};
            writeJsonAtomic(f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json",{{"steamAppId",77},{"fetchedAt",unixTimeMs()},{"assets",assets}});
            g_steamLibraryAssetsSession.clear();ResolvedGame game;game.appId=77;game.baseGameAppId=77;
            const auto candidates=artworkUrls(game,artworkSpecs().back());require(candidates.size()==1&&candidates[0].url=="https://cdn.cloudflare.steamstatic.com/steamcommunity/public/images/apps/77/"+std::string(40,'a')+".jpg","Official icon identity or hash path wrong");
            require(f.requests.empty(),"Icon-only metadata cache was discarded");
        });
        test("community-icon-hash-cannot-inject-a-path",[](const auto& p){
            ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;
            const json assets={{"community_icon","../../other/app.jpg"},{"asset_url_format","steam/apps/77/${FILENAME}"},{"header","header.jpg"}};
            writeJsonAtomic(f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json",{{"steamAppId",77},{"fetchedAt",unixTimeMs()},{"assets",assets}});
            g_steamLibraryAssetsSession.clear();ResolvedGame game;game.appId=77;game.baseGameAppId=77;
            require(artworkUrls(game,artworkSpecs().back()).empty(),"Malformed hash escaped verified AppID icon URL");
        });
        test("native-32px-steam-icon-is-low-quality-fallback",[](const auto& p){const auto bytes=fixturePng(p/L"icon.png",32,32);const auto image=*inspectImage(bytes);const auto quality=assessArtworkQuality("icon",image,false);require(quality.band==ArtworkQualityBand::UniqueFallback,"Small official icon was rejected or falsely rated as high quality");});

        test("network-pipeline-discovery-outage-persists-retry-and-recovers-without-steamid",[](const auto& p){
            ArtworkFixture f(p);f.title=L"維新の嵐 幕末志士伝";f.withIgdbId=false;f.igdbImages=false;f.networkOutage=true;
            auto m=f.run();
            require(m["failure"]["kind"]=="network-unavailable"&&m["failure"]["retryWhenNetworkReturns"]==true,"Full worker mislabeled discovery outage as permanent missing artwork");
            require(m.value("networkRetry",json{}).is_object()&&!m["failure"]["artworkNetworkFailures"].empty(),"Full worker failed to persist the artwork retry job");
            f.networkOutage=false;m=f.run();
            require(m["ok"]==true&&m["required"]["tall"]==true&&m["required"]["hero"]==true,"Recovered connection did not apply required artwork");
            require(m.value("networkRetry",json{}).is_null(),"Recovered worker retained an active network retry job");
            require(positiveJsonInt(m["match"],"steamAppId")==0&&f.slot(m,"tall")["provider"]=="baidu-image","Recovery falsely required or fabricated a Steam ID");
        });
        test("network-pipeline-malformed-baidu-response-persists-retry-job",[](const auto& p){
            ArtworkFixture f(p);f.withIgdbId=false;f.igdbImages=false;f.malformedBaiduSearch=true;
            const auto m=f.run();
            require(m["failure"]["kind"]=="network-unavailable"&&m.value("networkRetry",json{}).is_object(),"HTTP200 gateway page never reached the full worker retry job");
        });
        for(const auto* kind:{"empty","html","xml-error","truncated-png","truncated-jpeg"}){
            const auto label=std::string("network-pipeline-invalid-image-")+kind+"-recovers";
            test(label.c_str(),[=](const auto& p){
                ArtworkFixture f(p);
                std::vector<unsigned char> bad;
                if(std::string(kind)=="html"){const std::string body=" \r\n<!DOCTYPE HTML><html>temporary gateway error</html>";bad.assign(body.begin(),body.end());}
                else if(std::string(kind)=="xml-error"){const std::string body="<?xml version=\"1.0\"?><Error><Code>SlowDown</Code></Error>";bad.assign(body.begin(),body.end());}
                else if(std::string(kind)=="truncated-png")bad={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a,0,0,0,13,'I','H','D','R'};
                else if(std::string(kind)=="truncated-jpeg")bad={0xff,0xd8,0xff,0xe0,0,16,'J','F','I','F'};
                f.imageResponseOverride=bad;
                auto m=f.run();
                require(m["failure"]["kind"]=="network-unavailable"&&m.value("networkRetry",json{}).is_object(),"HTTP200 invalid image bytes became permanent missing artwork");
                for(const auto& artwork:m.at("artwork"))require(!artwork.value("ok",false)&&jsonStringOr(artwork,"file").empty(),"Invalid image was published in the durable manifest");
                for(const auto& entry:fs::recursive_directory_iterator(f.data)){
                    const auto ext=asciiLower(toUtf8(entry.path().extension().wstring()));
                    require(ext!=".png"&&ext!=".jpg"&&ext!=".jpeg","Invalid image bytes reached a durable artwork or URL cache");
                }
                f.imageResponseOverride.reset();m=f.run();
                require(m["ok"]==true&&f.slot(m,"tall")["ok"]&&f.slot(m,"hero")["ok"],"Healthy image response did not recover the worker");
                require(m.value("networkRetry",json{}).is_null(),"Recovered image retained its active network retry job");
                for(const auto* type:{"tall","hero"})require(inspectImage(readBinaryFile(fs::path(toWide(f.slot(m,type).at("file").template get<std::string>())))).has_value(),"Recovered durable artwork does not decode");
                require(positiveJsonInt(m["match"],"steamAppId")==0,"Image repair fabricated Steam identity");
            });
        }
        test("network-pipeline-unsupported-format-is-not-connectivity-loss",[](const auto& p){
            ArtworkFixture f(p);f.imageResponseOverride=std::vector<unsigned char>{'R','I','F','F',0,0,0,0,'W','E','B','P'};
            const auto m=f.run();
            require(m["failure"]["kind"]=="artwork-incomplete"&&m.value("networkRetry",json{}).is_null(),"Unsupported format was incorrectly classified as a network outage");
        });
        test("network-pipeline-unsupported-svg-is-not-connectivity-loss",[](const auto& p){
            ArtworkFixture f(p);const std::string svg="<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" width=\"600\" height=\"900\"/>";
            f.imageResponseOverride=std::vector<unsigned char>(svg.begin(),svg.end());
            const auto m=f.run();
            require(m["failure"]["kind"]=="artwork-incomplete"&&m.value("networkRetry",json{}).is_null(),"Unsupported SVG was incorrectly classified as a gateway error");
        });
        test("network-pipeline-invalid-image-preserves-manual-cover",[](const auto& p){
            ArtworkFixture f(p);const auto manual=p/L"fixtures"/L"tall.png";
            f.manual["cover"]={{"file",toUtf8(manual.wstring())}};
            const auto prior=readBinaryFile(manual);
            const std::string html="<html>temporary gateway failure</html>";
            f.imageResponseOverride=std::vector<unsigned char>(html.begin(),html.end());
            const auto m=f.run();
            require(f.slot(m,"tall")["ok"]&&f.slot(m,"tall")["provider"]=="manual-override"&&readBinaryFile(manual)==prior,"Invalid automatic download replaced manual artwork");
            require(m.value("networkRetry",json{}).is_object(),"Other missing slots lost network recovery");
        });
        test("network-pipeline-healthy-empty-artwork-does-not-enqueue-outage",[](const auto& p){
            ArtworkFixture f(p);f.withIgdbId=false;f.igdbImages=false;f.baiduImages=false;
            const auto m=f.run();
            require(m["failure"]["kind"]=="artwork-incomplete"&&m.value("networkRetry",json{}).is_null(),"Definitive absence entered an endless network retry loop");
        });


        const json goodAssets={{"asset_url_format","steam/apps/77/${FILENAME}"},{"library_capsule","known/library_capsule.jpg"}};
        auto assetEnvelope=[](const json& items){return json{{"response",{{"store_items",items}}}};};
        const json goodItem={{"item_type",0},{"id",77},{"appid",77},{"success",1},{"assets",goodAssets}};
        std::vector<std::pair<std::string,json>> malformedAssets={
            {"null-root",nullptr},{"array-root",json::array()},
            {"missing-envelope",json::object()},{"null-envelope",{{"response",nullptr}}},
            {"missing-inventory",{{"response",json::object()}}},
            {"invalid-inventory",assetEnvelope(json::object())},
            {"invalid-row",assetEnvelope(json::array({nullptr}))}};
        for(const auto* defect:{"foreign-app","conflicting-id","missing-success","invalid-success","missing-assets","invalid-assets","unsafe-assets","duplicate-app","wrong-item-type","zero-result"}) {
            auto item=goodItem;
            if(std::string(defect)=="foreign-app")item["appid"]=78,item["id"]=78;
            if(std::string(defect)=="conflicting-id")item["id"]=78;
            if(std::string(defect)=="missing-success")item.erase("success");
            if(std::string(defect)=="wrong-item-type")item["item_type"]=1;
            if(std::string(defect)=="zero-result")item["success"]=0;
            if(std::string(defect)=="invalid-success")item["success"]="1";
            if(std::string(defect)=="missing-assets")item.erase("assets");
            if(std::string(defect)=="invalid-assets")item["assets"]=json::array();
            if(std::string(defect)=="unsafe-assets")item["assets"]["asset_url_format"]="steam/apps/78/${FILENAME}";
            malformedAssets.push_back({defect,assetEnvelope(std::string(defect)=="duplicate-app"?json::array({item,item}):json::array({item}))});
        }
        for(const auto& [defect,body]:malformedAssets) {
            const auto name="network-storebrowse-malformed-"+defect;
            test(name.c_str(),[&](const auto& p){
                ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;f.libraryAssetsOverride=body;
                const auto cache=f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json";
                writeJsonAtomic(cache,{{"schemaVersion",1},{"steamAppId",77},{"fetchedAt",unixTimeMs()-25ull*60*60*1000},{"assets",goodAssets}});
                const auto original=readBinaryFile(cache);json attempts=json::array();
                const auto result=fetchSteamLibraryAssets(77,attempts);
                require(result==goodAssets,"Unusable HTTP 200 lost the verified stale asset index");
                require(readBinaryFile(cache)==original,"Unusable HTTP 200 overwrote/refreshed the positive cache");
                require(networkVerificationPending(attempts),"Malformed asset index became definitive absence");
                require(std::any_of(attempts.begin(),attempts.end(),[](const json& row){return row.value("cacheHit",false)&&row.value("stale",false);}),"Stale asset reuse was not disclosed");
                json failures=json::array();appendArtworkDiscoveryNetworkFailure("tall","steam-library-assets",attempts,failures);
                require(!failures.empty(),"Malformed discovery did not reach artwork retry accounting");
                const auto count=f.requests.size();require(fetchSteamLibraryAssets(77,attempts)==goodAssets&&f.requests.size()==count,"One worker queried the broken inventory again for another slot");
            });
        }
        for(const int resultCode:{2,3,10,16,20,38,84}) {
            const auto name="network-storebrowse-transient-result-"+std::to_string(resultCode);
            test(name.c_str(),[&](const auto& p){
                ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;
                f.libraryAssetsOverride=assetEnvelope(json::array({{{"item_type",0},{"id",77},{"appid",0},{"success",resultCode}}}));
                writeJsonAtomic(f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json",{{"steamAppId",77},{"fetchedAt",unixTimeMs()-25ull*60*60*1000},{"assets",goodAssets}});
                json attempts=json::array();require(fetchSteamLibraryAssets(77,attempts)==goodAssets,"Transient API result discarded the stale asset index");
                require(networkVerificationPending(attempts),"Transient API result did not preserve recovery evidence");
            });
        }
        test("network-storebrowse-rate-limit-persists-across-appids-and-worker-memory",[&](const auto& p){
            ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;
            f.libraryAssetsOverride=assetEnvelope(json::array({{{"item_type",0},{"id",77},{"appid",0},{"success",84}}}));
            json attempts=json::array();require(fetchSteamLibraryAssets(77,attempts).empty(),"Rate limit invented artwork");
            require(networkVerificationPending(attempts),"HTTP 200 rate limit became definitive absence");
            const auto count=f.requests.size();g_metadataBackoffs.clear();g_steamLibraryAssetsSession.clear();
            f.libraryAssetsOverride=assetEnvelope(json::array());attempts=json::array();fetchSteamLibraryAssets(78,attempts);
            require(f.requests.size()==count,"API result 84 did not persist a provider wait for another app");
            require(networkVerificationPending(attempts),"Reloaded provider wait lost retry state");
        });
        for(const bool expired:{false,true}) {
            const auto name=std::string("network-storebrowse-no-usable-cache-")+(expired?"expired":"missing");
            test(name.c_str(),[&](const auto& p){
                ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;f.libraryAssetsOverride=json::object();
                const auto cache=f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json";
                if(expired)writeJsonAtomic(cache,{{"steamAppId",77},{"fetchedAt",unixTimeMs()-31ull*24*60*60*1000},{"assets",goodAssets}});
                const auto prior=expired?readBinaryFile(cache):std::vector<unsigned char>{};json attempts=json::array();
                require(fetchSteamLibraryAssets(77,attempts).empty(),"Missing/expired index granted stale authority");
                require(networkVerificationPending(attempts),"Malformed response without cache lost retry state");
                require(expired?readBinaryFile(cache)==prior:!fs::exists(cache),"Invalid inventory altered the durable cache");
            });
        }
        for(const auto* control:{"empty-inventory","empty-assets","definitive-denial"}) {
            const auto name=std::string("network-storebrowse-healthy-")+control;
            test(name.c_str(),[&](const auto& p){
                ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;
                auto item=goodItem;item["assets"]=json::object();
                if(std::string(control)=="definitive-denial")item={{"item_type",0},{"id",77},{"appid",0},{"success",15}};
                f.libraryAssetsOverride=assetEnvelope(std::string(control)=="empty-inventory"?json::array():json::array({item}));
                json attempts=json::array();require(fetchSteamLibraryAssets(77,attempts).empty(),"Definitive absence invented assets");
                require(!networkVerificationPending(attempts),"Definitive absence entered an endless network retry loop");
                require(!fs::exists(f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json"),"Empty inventory was persisted as a positive cache");
            });
        }
        test("network-storebrowse-healthy-ancillary-assets",[&](const auto& p){
            ArtworkFixture f(p);f.install();g_workerDataRoot=f.data;auto item=goodItem;
            item["assets"]={{"asset_url_format","steam/apps/77/${FILENAME}"},{"small_capsule","hash/capsule_231x87.jpg"},{"last_modified",123}};
            f.libraryAssetsOverride=assetEnvelope(json::array({item}));json attempts=json::array();
            require(!hasUsableSteamLibraryAssets(fetchSteamLibraryAssets(77,attempts),77),"Store thumbnail became library artwork");
            require(!networkVerificationPending(attempts),"Ancillary assets alone were mislabeled as a network outage");
            require(!fs::exists(f.data/L"cache"/L"steam-library-assets"/L"77-english-us.json"),"Unusable index polluted the positive artwork cache");
        });
        test("network-storebrowse-invalid-inventory-reaches-full-worker-retry",[&](const auto& p){
            ArtworkFixture f(p);f.libraryAssetsOverride=json::object();f.steamImages=false;f.igdbImages=false;f.baiduImages=false;
            const auto m=f.run(true,false,true);
            require(m.value("networkRetry",json{}).is_object(),"Malformed Steam discovery stopped automatic image recovery");
            const auto jobs=loadNetworkRetryQueue(f.data);
            require(!jobs["jobs"].empty(),"Full worker did not persist the recovery job");
        });

        SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",priorRoot.empty()?nullptr:priorRoot.c_str());
        size_t failures=0;for(const auto& t:tests)if(!t["passed"].get<bool>())++failures;
        json report={{"allPassed",failures==0},{"caseCount",tests.size()},{"failedCount",failures},{"cases",tests},{"network","fixture-only"},{"realSteamFilesModified",false}};
        writeJsonAtomic(root/L"summary.json",report);std::cout<<report.dump(2)<<"\n";return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
