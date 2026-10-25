#include "custom_steam_library_test_runtime.h"
#include <iostream>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void mustFail(const std::function<void()>& f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,"Expected controlled failure");}
static json record(int requested=620,int canonical=620,const std::string& type="game",const std::string& name="Portal 2"){
    return {{std::to_string(requested),{{"success",true},{"data",{{"steam_appid",canonical},{"name",name},{"type",type}}}}}};
}
static HttpResponse payload(const json& doc){const auto t=doc.dump();return {200,{},std::vector<unsigned char>(t.begin(),t.end())};}
static std::vector<unsigned char> png(const fs::path& path,int width=600,int height=900){
    (void)artworkInspectionGdiPlusToken();fs::create_directories(path.parent_path());Gdiplus::Bitmap image(width,height,PixelFormat32bppARGB);Gdiplus::Graphics g(&image);g.Clear(Gdiplus::Color(255,30,60,90));check(image.Save(path.c_str(),&pngEncoderClsid(),nullptr)==Gdiplus::Ok,"Fixture PNG save failed");return readBinaryFile(path);
}
int wmain(int argc,wchar_t** argv){try{
    if (argc==3 && std::wstring(argv[1])==L"--backoff-fixture-action") {
        const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        const auto file=fs::absolute(argv[2]);
        check(canonicalPathKey(file).starts_with(canonicalPathKey(build)+"\\"),"Action must stay in Build");
        const auto action=loadJsonDocument(file,64u<<10);
        g_workerDataRoot=fs::absolute(fs::path(toWide(action.at("dataRoot").get<std::string>())));
        check(canonicalPathKey(g_workerDataRoot).starts_with(canonicalPathKey(build)+"\\"),"Unsafe action data root");
        const auto url=action.at("url").get<std::string>(),operation=action.at("operation").get<std::string>();
        if(operation=="write")rememberMetadataBackoff(url,action.at("status").get<DWORD>(),action.at("delayMs").get<DWORD>());
        else if(operation=="clear")clearMetadataBackoff(url,action.at("requestStartedAt").get<uint64_t>());
        else check(operation=="read","Unknown backoff fixture action");
        writeJsonAtomic(file.wstring()+L".result.json",loadMetadataBackoff(url));
        return 0;
    }
    check(argc==2||argc==3,"Pass fresh Build fixture root and optional Steam evidence root");const auto root=fs::absolute(argv[1]);
    const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
    check(canonicalPathKey(root).starts_with(canonicalPathKey(build)+"\\")&&!fs::exists(root),"Unsafe/reused fixture root");fs::create_directories(root);
    // Avoid live DNS / local transparent accelerator probes, including on a
    // machine with Steam accelerator Hosts mappings. Actual policy is tested
    // with a deterministic direct route and a fail-closed raw HTTP backend.
    g_steamAcceleratorStatus.checked=true;g_steamAcceleratorStatus.state="fixture-direct";
    for(const auto* host:{"store.steampowered.com","steamstore-a.akamaihd.net","api.steampowered.com","shared.steamstatic.com","cdn.cloudflare.steamstatic.com"})g_steamAcceleratorStatus.localHostCache[host]=false;
    json cases=json::array();
    auto test=[&](const char* name,const std::function<void(const fs::path&)>& action){const auto p=root/std::to_wstring(cases.size());fs::create_directories(p);g_workerDataRoot=p/L"data";g_fastMetadataMode=false;g_identityNetworkDeadline=0;g_metadataBackoffs.clear();artworkTestTransport={};selftestMoveFault={};
        SetEnvironmentVariableW(L"YEMAN_SYSTEM_BLACKLIST_PATH",(p/L"system-blacklist.txt").c_str());SetEnvironmentVariableW(L"YEMAN_PLAYER_BLACKLIST_PATH",(p/L"player-blacklist.txt").c_str());
        try{action(p);cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});}artworkTestTransport={};selftestMoveFault={};};
    test("steam-exact-valid-appdetails",[](const auto&){auto r=steamDetailsRecord(record(),620);check(r.requestedAppId==620&&r.canonicalAppId==620&&r.type=="game","Valid Steam record rejected");});
    for(const auto& pair:std::vector<std::pair<std::string,json>>{{"null",nullptr},{"array",json::array()},{"string","error"},{"number",42},{"empty-object",json::object()},{"unrelated-app",record(570,570,"game","Dota 2")},{"failed-app",{{"620",{{"success",false}}}}},{"data-null",{{"620",{{"success",true},{"data",nullptr}}}}},{"envelope-array",{{"620",json::array()}}}}){
        const auto name="steam-rejects-"+pair.first;test(name.c_str(),[&](const auto&){mustFail([&]{steamDetailsRecord(pair.second,620);});});}
    for(const auto& bad:std::vector<json>{nullptr,"true",1,0,json::array(),json::object()})test("steam-success-type-is-strict",[&](const auto&){auto r=record();r["620"]["success"]=bad;mustFail([&]{steamDetailsRecord(r,620);});});
    for(const auto& bad:std::vector<json>{nullptr,"620",0,-1,620.5,18446744073709551615ull,json::object()})test("steam-canonical-id-type-and-range",[&](const auto&){auto r=record();r["620"]["data"]["steam_appid"]=bad;mustFail([&]{steamDetailsRecord(r,620);});});
    test("steam-missing-id-not-filled-from-request",[](const auto&){auto r=record();r["620"]["data"].erase("steam_appid");mustFail([&]{steamDetailsRecord(r,620);});});
    for(const auto* key:{"name","type"})for(const auto& bad:std::vector<json>{nullptr,12,json::array(),""})test("steam-title-and-content-type-are-strict",[&](const auto&){auto r=record();r["620"]["data"][key]=bad;mustFail([&]{steamDetailsRecord(r,620);});});
    for(const auto* type:{"dlc","demo","tool","music","soundtrack"})test("steam-related-content-is-not-coerced-to-game",[&](const auto&){auto r=steamDetailsRecord(record(620,620,type),620);check(r.type==type&&r.type!="game","Related content became base game");});
    test("steam-alias-requires-independent-canonical-query",[](const auto&){int requests=0;artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){++requests;return payload(url.find("appids=621")!=std::string::npos?record(621,621):record(620,621));};json log=json::array();auto r=fetchSteamDetails(620,log);check(requests==2&&r.canonicalAppId==621&&r.requestedAppId==620&&fs::exists(steamDetailsCachePath(621))&&!fs::exists(steamDetailsCachePath(620)),"Alias not verified independently");});
    test("steam-alias-canonical-title-disagreement-rejected",[](const auto&){artworkTestTransport=[](const auto&,const auto&,const auto& url,const auto&){return payload(url.find("appids=621")!=std::string::npos?record(621,621,"game","Other Game"):record(620,621));};json log=json::array();mustFail([&]{fetchSteamDetails(620,log);});check(!fs::exists(steamDetailsCachePath(621)),"Unverified alias cached");});
    test("steam-alias-cycle-rejected",[](const auto&){artworkTestTransport=[](const auto&,const auto&,const auto& url,const auto&){return payload(url.find("appids=621")!=std::string::npos?record(621,620):record(620,621));};json log=json::array();mustFail([&]{fetchSteamDetails(620,log);});});
    test("steam-alias-failed-canonical-query-rejected",[](const auto&){artworkTestTransport=[](const auto&,const auto&,const auto& url,const auto&){return url.find("appids=621")!=std::string::npos?HttpResponse{404}:payload(record(620,621));};json log=json::array();mustFail([&]{fetchSteamDetails(620,log);});});
    test("steam-cache-invalid-canonical-is-cache-miss",[](const auto&){writeJsonAtomic(steamDetailsCachePath(620),{{"schemaVersion",1},{"steamAppId",620},{"fetchedAt",unixTimeMs()},{"data",record(620,621)["620"]["data"]}});uint64_t at=0;check(!loadSteamDetailsCache(620,at),"Mismatched cache accepted");});
    test("steam-cache-corrupt-json-is-cache-miss",[](const auto&){const auto p=steamDetailsCachePath(620);writeAtomic(p,{'{'});uint64_t at=0;check(!loadSteamDetailsCache(620,at),"Broken JSON cache accepted");});
    test("steam-cache-null-fetched-at-is-cache-miss",[](const auto&){writeJsonAtomic(steamDetailsCachePath(620),{{"schemaVersion",1},{"steamAppId",620},{"fetchedAt",nullptr},{"data",record()["620"]["data"]}});uint64_t at=0;check(!loadSteamDetailsCache(620,at),"Invalid timestamp accepted");});
    test("steam-cache-future-timestamp-is-not-used",[](const auto&){writeJsonAtomic(steamDetailsCachePath(620),{{"schemaVersion",1},{"steamAppId",620},{"fetchedAt",unixTimeMs()+60000},{"data",record()["620"]["data"]}});int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(record());};json log=json::array();fetchSteamDetails(620,log);check(calls==1,"Future cache granted hit");});
    test("steam-cache-valid-hit-does-not-contact-network",[](const auto&){saveSteamDetailsCache(steamDetailsRecord(record(),620));json log=json::array();auto r=fetchSteamDetails(620,log);check(r.canonicalAppId==620&&log.size()==1&&log[0]["cacheHit"]==true,"Valid cache not used");});
    auto sequence=[&](const std::vector<HttpResponse>& responses,json& log,size_t max=1000){size_t index=0;artworkTestTransport=[&,index](const auto&,const auto&,const auto&,const auto&)mutable{const auto n=(std::min)(index++,responses.size()-1);return responses[n];};return httpRequestWithRetry("fixture","GET","https://fixture.invalid/test",L"",{},max,false,"",&log);};
    test("network-http-429-defers-before-success-response",[&](const auto&){json log=json::array();auto r=sequence({HttpResponse{429},payload(record())},log);check(r.status==429&&log.size()==1&&log[0].value("retryDeferred",false)&&networkVerificationPending(log),"429 did not hand recovery to scheduling");});
    test("http-empty-503-then-success",[&](const auto&){json log=json::array();auto r=sequence({HttpResponse{503},payload(record())},log);check(r.status==200&&log.size()==2,"503 not retried");});
    test("http-persistent-503-is-bounded",[&](const auto&){json log=json::array();auto r=sequence({HttpResponse{503}},log);check(r.status==503&&log.size()==HTTP_MAX_ATTEMPTS,"503 retry unbounded");});
    test("http-404-is-not-retried",[&](const auto&){json log=json::array();auto r=sequence({HttpResponse{404}},log);check(r.status==404&&log.size()==1,"404 needlessly retried");});
    test("network-http-retry-after-respected-and-capped",[&](const auto&){json log=json::array();auto response=HttpResponse{429};response.retryAfterMs=2000;auto r=sequence({response,payload(record())},log);check(r.status==429&&log.size()==1&&log[0].value("retryDeferred",false)&&log[0]["retryAfterMs"].get<int>()>=2000,"Retry-After ignored");check(parseRetryAfterMs(L"9999999999999999999999999")==HTTP_MAX_RETRY_AFTER_MS&&parseRetryAfterMs(L"bad")==0&&parseRetryAfterMs(L"-2")==0&&parseRetryAfterMs(L"2")==2000,"Retry-After parse invalid");});
    test("http-transport-timeout-retry-and-success",[](const auto&){int count=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){if(++count==1)throw HttpTransportError("fixture timeout",ERROR_WINHTTP_TIMEOUT,true);return payload(record());};json log=json::array();auto r=httpRequestWithRetry("fixture","GET","https://fixture.invalid/test",L"",{},1000,false,"",&log);check(r.status==200&&count==2&&log[0]["win32Error"]==ERROR_WINHTTP_TIMEOUT,"Transport retry bypassed");});
    test("http-invalid-data-fails-without-retry",[](const auto&){int count=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&)->HttpResponse{++count;throw HttpTransportError("fixture invalid",ERROR_INVALID_DATA,false);};json log=json::array();mustFail([&]{httpRequestWithRetry("fixture","GET","https://fixture.invalid/test",L"",{},1000,false,"",&log);});check(count==1,"Permanent transport fault retried");});
    test("http-content-length-over-limit-rejected",[&](const auto&){json log=json::array();auto r=HttpResponse{200};r.expectedBytes=1001;mustFail([&]{sequence({r},log);});check(log.size()==1,"Oversized declaration retried");});
    test("http-actual-body-over-limit-rejected",[&](const auto&){json log=json::array();auto r=HttpResponse{200};r.body.resize(1001);mustFail([&]{sequence({r},log);});});
    test("http-200-html-is-not-json",[](const auto&){artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&){std::string s="<html>Steam error</html>";return HttpResponse{200,{},std::vector<unsigned char>(s.begin(),s.end())};};json log=json::array();mustFail([&]{getJson("https://fixture.invalid/test","fixture",&log);});});
    test("http-200-empty-json-is-not-success",[](const auto&){artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&){return HttpResponse{200};};json log=json::array();mustFail([&]{getJson("https://fixture.invalid/test","fixture",&log);});});
    test("fixture-raw-http-is-forbidden",[](const auto&){mustFail([]{httpRequest("GET","https://store.steampowered.com/",L"",{},1000,false,"",GetTickCount64()+1000);});});
    test("image-real-png-decodes",[](const auto& p){auto b=png(p/L"valid.png");auto i=inspectImage(b);check(i&&i->width==600&&i->height==900,"Valid PNG rejected");});
    test("image-header-only-png-rejected",[](const auto& p){auto b=png(p/L"valid.png");b.resize(24);check(!inspectImage(b),"Header-only fake image accepted");});
    test("image-truncated-png-rejected",[](const auto& p){auto b=png(p/L"valid.png");b.resize(45);check(!inspectImage(b),"Truncated image accepted");});
    test("image-html-with-jpg-extension-rejected",[](const auto&){std::string s="<html>200 OK</html>";check(!inspectImage(std::vector<unsigned char>(s.begin(),s.end())),"HTML accepted as artwork");});
    test("image-decompression-bomb-header-rejected",[](const auto& p){auto b=png(p/L"valid.png");b[16]=0x7f;b[17]=0xff;b[18]=0xff;b[19]=0xff;check(!inspectImage(b),"Huge dimensions accepted");});
    test("image-minimal-jpeg-sof-is-not-decodable",[](const auto&){std::vector<unsigned char> b={0xff,0xd8,0xff,0xc0,0,17,8,3,0x84,2,0x58,3,1,0x11,0,2,0x11,0,3,0x11,0,0xff,0xd9};check(!inspectImage(b),"SOF without pixels accepted");});
    test("image-real-hero-ratio-and-resolution",[](const auto& p){auto b=png(p/L"hero.png",3840,1240);auto i=inspectImage(b);check(i&&i->width==3840&&i->height==1240,"Steam official full hero shape rejected");});
    test("atomic-write-transient-denial-keeps-and-retries",[](const auto& p){const auto target=p/L"config.json";writeAtomic(target,{'o','l','d'});int attempts=0;selftestMoveFault=[&](const auto&,const auto& to){return to==target.wstring()&&++attempts<=2?ERROR_ACCESS_DENIED:0;};writeAtomic(target,{'n','e','w'});check(attempts==3&&readBinaryFile(target)==std::vector<unsigned char>({'n','e','w'}),"Worker atomic retry failed");});
    test("atomic-write-disk-fault-keeps-original",[](const auto& p){const auto target=p/L"config.json";writeAtomic(target,{'o','l','d'});selftestMoveFault=[&](const auto&,const auto& to){return to==target.wstring()?ERROR_DISK_FULL:0;};mustFail([&]{writeAtomic(target,{'n','e','w'});});check(readBinaryFile(target)==std::vector<unsigned char>({'o','l','d'}),"Original lost on disk fault");for(const auto& e:fs::directory_iterator(p))check(e.path().filename().wstring().find(L".yeman-tmp-")==std::wstring::npos,"Staging file leaked");});
    test("steam-200-external-redirect-cannot-grant-identity",[](const auto&){artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&){auto r=payload(record());r.finalUrl="https://untrusted.invalid/api/appdetails?appids=620";return r;};json log=json::array();mustFail([&]{fetchSteamDetails(620,log);});check(!fs::exists(steamDetailsCachePath(620)),"External redirect cached as Steam authority");});
    test("steam-final-url-rejects-downgrade-and-host-lookalikes",[](const auto&){for(const auto* url:{"http://store.steampowered.com/api/appdetails","https://store.steampowered.com.untrusted.invalid/api/appdetails","https://store.steampowered.com/error","https://untrusted.invalid/api/appdetails"})check(!trustedSteamMetadataFinalUrl("https://store.steampowered.com/api/appdetails?appids=620",url),"Untrusted URL accepted");check(trustedSteamMetadataFinalUrl("https://store.steampowered.com/api/appdetails?appids=620","https://store.steampowered.com/api/appdetails?appids=620"),"Official Steam URL rejected");});
    test("random-malformed-appdetails-1000-combinations-never-verify",[](const auto&){const std::vector<json> bad={nullptr,0,"",json::array(),json::object()};for(int i=0;i<1000;++i){auto r=record();if(i%3==0)r["620"]["success"]=bad[(i/3)%bad.size()];else if(i%3==1)r["620"]["data"]["steam_appid"]=bad[(i/3)%bad.size()];else r["620"]["data"]["type"]=bad[(i/3)%bad.size()];try{steamDetailsRecord(r,620);}catch(...){continue;}throw std::runtime_error("Malformed combination accepted at i="+std::to_string(i)+" value="+r.dump());}});
    test("atomic-write-and-read-beyond-max-path",[](const auto& p){auto dir=p;for(int i=0;i<8;++i)dir/=L"long-artwork-metadata-fixture-012345";const auto target=dir/L"manifest.json";check(target.wstring().size()>300,"Long-path fixture too short");writeJsonAtomic(target,{{"ok",true},{"id",620}});const auto b=readBinaryFile(target);check(json::parse(b.begin(),b.end())["id"]==620,"Long-path JSON did not roundtrip");writeJsonAtomic(target,{{"ok",true},{"id",570}});const auto replaced=readBinaryFile(target);check(json::parse(replaced.begin(),replaced.end())["id"]==570,"Long-path replacement failed");});
    test("io-path-supports-long-unc-without-changing-persisted-path",[](const auto&){const auto original=fs::path(L"\\\\server\\share")/std::wstring(260,L'a')/L"image.png";const auto result=custom_steam_library::ioPath(original).wstring();check(result.starts_with(L"\\\\?\\UNC\\server\\share\\"),"UNC IO prefix malformed");check(!original.wstring().starts_with(L"\\\\?\\"),"Original persisted path was mutated");});
    if(argc==3){const auto evidence=fs::path(argv[2]);for(const int id:{620,570})test("live-captured-steam-appdetails-parses",[&](const auto&){const auto b=readBinaryFile(evidence/(L"app-"+std::to_wstring(id)+L".json"));auto r=steamDetailsRecord(json::parse(b.begin(),b.end()),id);check(r.canonicalAppId==id&&r.type=="game","Real Steam sample rejected");});test("live-captured-steam-invalid-app-is-rejected",[&](const auto&){const auto b=readBinaryFile(evidence/L"app-999999999.json");mustFail([&]{steamDetailsRecord(json::parse(b.begin(),b.end()),999999999);});});}

    test("builtin-steam-tool-rejected-before-network-or-cache",[](const auto&){
        int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&)->HttpResponse{++calls;return payload(record(228980,228980,"game","Steamworks Common Redistributables"));};
        json attempts=json::array();mustFail([&]{fetchSteamDetails(228980,attempts);});check(calls==0&&attempts.empty(),"Excluded tool started network");
    });
    test("builtin-steam-tool-forged-game-record-rejected",[](const auto&){
        mustFail([&]{steamDetailsRecord(record(228980,228980,"game","Renamed Tool"),228980);});
        mustFail([&]{steamDetailsRecord(record(620,228980,"game","Renamed Tool"),620);});
        mustFail([&]{steamDetailsRecord(record(620,620,"game","Steamworks Common Redistributables"),620);});
    });
    test("builtin-steam-tool-old-verified-identity-is-denied",[](const auto&){
        json match={{"appId",228980},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion",STEAM_RESOLVER_VERSION}};
        check(!custom_steam_library::verifiedSteamIdentityMatch(match),"Cached tool is verified game");
        match["appId"]=620;check(custom_steam_library::verifiedSteamIdentityMatch(match),"Real cached game rejected");
    });
    test("compact-official-search-verifies-exact-id-without-html",[](const auto&){
        int compact=0,html=0,details=0;
        artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){
            if(url.find("/api/storesearch/")!=std::string::npos){++compact;return payload({{"items",json::array({{{"id",620},{"name","Portal 2"}}})}});}
            if(url.find("/api/appdetails")!=std::string::npos){++details;return payload(record());}
            ++html;return payload(json::object());
        };
        json rounds=json::array();const auto r=resolveSteamGame({{1,"manual-name","Portal 2",{}}},{"Portal 2"},rounds);
        check(r&&r->appId==620&&r->steamVerificationStatus=="steam-verified","Exact compact candidate failed verification");
        check(compact==1&&html==0&&details==1,"Exact compact query wasted HTML or repeated details requests");
    });
    test("compact-candidate-still-rejects-tools-and-false-appdetails",[](const auto&){
        artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){
            if(url.find("storesearch")!=std::string::npos)return payload({{"items",json::array({{{"id",620},{"name","Portal 2"}}})}});
            return payload(record(620,620,"tool"));
        };
        json rounds=json::array();const auto r=resolveSteamGame({{1,"manual-name","Portal 2",{}}},{"Portal 2"},rounds);
        check(!r||!r->confident||r->appId==0,"Search candidate fabricated a game identity");
    });
    test("compact-endpoint-failure-keeps-html-fallback-open",[](const auto&){
        int html=0;artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){
            if(url.find("storesearch")!=std::string::npos)throw std::runtime_error("compact timeout");
            if(url.find("search/results")!=std::string::npos){++html;return payload({{"success",1},{"results_html",R"(<a href="https://store.steampowered.com/app/620/" data-ds-appid="620"><span class="title">Portal 2</span></a>)"}});}
            return payload(record());
        };
        json rounds=json::array();const auto r=resolveSteamGame({{1,"manual-name","Portal 2",{}}},{"Portal 2"},rounds);
        check(html>0&&r&&r->appId==620,"Compact outage disabled working HTML fallback");
    });
    test("steam-search-cache-reuses-inventory-not-identity",[](const auto&){
        int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array({{{"id",620},{"name","Portal 2"}}})}});};
        const std::string url="https://store.steampowered.com/api/storesearch/?term=Portal%202&l=english&cc=US";
        json attempts=json::array();const auto first=steamCandidateSearchJson(url,&attempts);const auto second=steamCandidateSearchJson(url,&attempts);
        check(calls==1&&first==second,"Warm compact search repeated network");
        check(attempts.back().value("cacheHit",false)&&!attempts.back().value("identityAuthority",true),"Candidate cache claimed identity authority");
    });
    test("steam-search-cache-is-keyed-by-language-country-and-query",[](const auto&){
        int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array()}});};
        for(const auto* suffix:{"term=Portal&l=english&cc=US","term=Portal&l=japanese&cc=JP","term=Portal%202&l=english&cc=US"})steamCandidateSearchJson(std::string("https://store.steampowered.com/api/storesearch/?")+suffix,nullptr);
        check(calls==3,"Locale or sequel shared a search cache entry");
    });
    test("steam-search-cache-future-time-and-wrong-url-are-rejected",[](const auto&){
        int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array()}});};
        const std::string url="https://store.steampowered.com/api/storesearch/?term=Portal";
        const auto path=g_workerDataRoot/L"cache"/L"steam-search"/toWide(sha256(std::vector<unsigned char>(url.begin(),url.end()))+".json");
        for(const auto& bad:std::vector<json>{
            {{"schemaVersion",1},{"url",url},{"fetchedAt",unixTimeMs()+60000},{"data",{{"items",json::array()}}}},
            {{"schemaVersion",1},{"url","other query"},{"fetchedAt",unixTimeMs()},{"data",{{"items",json::array()}}}},
            {{"schemaVersion",1},{"url",url},{"fetchedAt",unixTimeMs()},{"data",{{"items","bad shape"}}}}}){
            writeJsonAtomic(path,bad);steamCandidateSearchJson(url,nullptr);
        }
        check(calls==3,"Malformed or mismatched cache suppressed fresh query");
    });
    test("steam-search-network-errors-are-never-negative-cached",[](const auto&){
        int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&)->HttpResponse{++calls;throw std::runtime_error("offline");};
        for(int i=0;i<2;++i)mustFail([&]{steamCandidateSearchJson("https://store.steampowered.com/api/storesearch/?term=Portal",nullptr);});
        check(calls>=2,"Transient failure became permanent search absence");
    });
    test("local-config-title-cannot-fabricate-verified-steam-identity",[](const auto& p){
        const auto exe=p/L"Portal 2"/L"portal2.exe",data=p/L"data",out=p/L"jobs";
        writeAtomic(exe,std::vector<unsigned char>{'f','i','x','t','u','r','e'});
        const std::string ini="AppId=620 # Portal 2\n";writeAtomic(exe.parent_path()/L"steam_emu.ini",std::vector<unsigned char>(ini.begin(),ini.end()));
        SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());
        artworkTestTransport=[](const auto&,const auto&,const auto& url,const auto&){
            if(url.find("appdetails")!=std::string::npos)return payload(record(620,620,"tool"));
            if(url.find("storesearch")!=std::string::npos)return payload({{"items",json::array()}});
            if(url.find("search/results")!=std::string::npos)return payload({{"success",1},{"results_html",""}});
            return payload(json::object());
        };
        std::vector<std::wstring> args={L"SteamArtworkLab.exe",exe.wstring(),out.wstring(),L"--metadata-only",L"--steam-comprehensive-identity-only"};
        std::vector<wchar_t*> argv;for(auto& a:args)argv.push_back(a.data());const int code=workerMainNotUsedBySelftest(static_cast<int>(argv.size()),argv.data());
        check(code!=0,"Inline local title bypassed official tool rejection");
        if(fs::exists(out))for(const auto& f:fs::recursive_directory_iterator(out))if(f.path().filename()==L"manifest.json"){
            const auto m=loadJsonDocument(f.path());const auto match=m.value("match",json::object());
            check(jsonStringOr(match,"steamVerificationStatus")!="steam-verified","Rejected local ID was published as verified");
        }
    });
    test("known-library-root-stops-unrelated-parent-search-rounds",[](const auto& p){
        const auto game=p/L"UnrelatedParent"/L"Portal 2",exe=game/L"bin"/L"win64"/L"portal2.exe";
        const auto rounds=buildNameRounds(exe,{},true,{}, {}, {},game);
        bool title=false;for(const auto& round:rounds){
            title=title||round.value=="Portal 2";
            check(round.value!="UnrelatedParent","Parent library polluted identity evidence");
        }check(title,"Boundary removed actual game title");
    });
    test("unknown-library-root-keeps-diagnostic-parent-fallback",[](const auto& p){
        const auto exe=p/L"LibraryParent"/L"Portal 2"/L"bin"/L"portal2.exe";
        const auto rounds=buildNameRounds(exe,{},true,{},{});bool title=false;
        for(const auto& round:rounds)title=title||round.value=="Portal 2";
        check(title,"Unknown scan root suppressed nearby real title");
    });
    test("legacy-local-title-identity-is-rejected-without-discarding-official-cache",[](const auto&){
        json match={{"appId",620},{"storefrontAppId",620},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion",STEAM_RESOLVER_VERSION},{"source","local-install-metadata-offline"}};
        check(!custom_steam_library::verifiedSteamIdentityMatch(match),"Local title inherited official verification");
        match["source"]="local-steam-appid";check(custom_steam_library::verifiedSteamIdentityMatch(match),"Officially checked local AppID was discarded");
    });
    test("optional-icon-deadline-restores-required-slot-budget",[](const auto&){
        check(g_optionalArtworkDeadline==0,"Test started inside optional scope");
        {ScopedOptionalArtworkBudget optional(true);const auto end=g_optionalArtworkDeadline;
         check(end>GetTickCount64()&&end<=GetTickCount64()+8000,"Optional deadline not bounded");
         {ScopedOptionalArtworkBudget nested(true);check(g_optionalArtworkDeadline<=end,"Nested lookup extended icon budget");}}
        check(g_optionalArtworkDeadline==0,"Optional timeout leaked into required artwork");
    });
    test("expired-optional-icon-budget-never-launches-another-request",[](const auto&){
        int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array()}});};
        {ScopedOptionalArtworkBudget optional(true);g_optionalArtworkDeadline=GetTickCount64()-1;
         mustFail([&]{getJson("https://www.steamgriddb.com/api/public/search/autocomplete?term=Portal","steamgriddb");});}
        check(calls==0&&g_optionalArtworkDeadline==0,"Expired optional slot still launched IO");
    });
    test("identical-official-titles-with-distinct-appids-require-confirmation",[](const auto&){
        artworkTestTransport=[](const auto&,const auto&,const auto& url,const auto&){
            if(url.find("storesearch")!=std::string::npos)return payload({{"items",json::array({{{"id",620},{"name","Portal 2"}},{{"id",621},{"name","Portal 2"}}})}});
            return payload(url.find("appids=621")!=std::string::npos?record(621,621):record());
        };
        json log=json::array();const auto result=resolveSteamGame({{1,"manual-name","Portal 2",{}}},{"Portal 2"},log);
        check(result&&!result->confident&&result->identityStatus=="needs-confirmation","Identical names silently chose one distinct official AppID");
    });
    test("canonical-aliases-of-the-same-game-are-not-false-ambiguity",[](const auto&){
        artworkTestTransport=[](const auto&,const auto&,const auto& url,const auto&){
            if(url.find("storesearch")!=std::string::npos)return payload({{"items",json::array({{{"id",620},{"name","Portal 2"}},{{"id",621},{"name","Portal 2"}}})}});
            return payload(url.find("appids=621")!=std::string::npos?record(621,620):record());
        };
        json log=json::array();const auto result=resolveSteamGame({{1,"manual-name","Portal 2",{}}},{"Portal 2"},log);
        check(result&&result->confident&&result->appId==620,"Verified canonical alias was treated as another game");
    });

    for (const auto& input : std::vector<std::pair<std::string,std::string>>{
        {"Dota 2 [FitGirl Repack]","Dota 2"},{"ELDEN RING [DODI Repack]","ELDEN RING"},
        {"Portal 2-Goldberg","Portal 2"},{"Magicraft.v1.2.14-P2P","Magicraft"},{"Little.Fighter.2.Remastered.v2.15a-P2P","Little.Fighter.2.Remastered"},{"Cyberpunk 2077 v1.2.3-GOG","Cyberpunk 2077"},
        {"Game 2 Deluxe-CODEX","Game 2 Deluxe"},{"Grand Theft Auto V","Grand Theft Auto V"},
        {"FINAL FANTASY XVI","FINAL FANTASY XVI"},{"GOG Galaxy","GOG Galaxy"}}) {
        const auto name="installed-title-packaging-"+input.first;
        test(name.c_str(),[&](const auto&){check(installedDirectoryTitle(input.first)==input.second,"Packaging cleanup changed product identity");});
    }
    test("generic-launcher-exe-is-not-a-provider-game-title",[](const auto& p){
        const auto game=p/L"Grand Theft Auto V Legacy";
        for(const auto* filename:{L"Game.exe",L"Game-Win64-Shipping.exe",L"Launcher.exe"}){
            const auto rounds=buildNameRounds(game/filename,{},true,{},{},{},game);
            for(const auto& round:rounds)check(!round.source.starts_with("exe-"),"Generic launcher became another game's identity");
        }
    });
    for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"BC","BC Kings"},{"PZ","PZ Gravity"},{"APEX","Apex"}}){
        const auto name="ambiguous-short-alias-needs-confirmation-"+item.first;
        test(name.c_str(),[&](const auto&){
            artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){
                if(url.find("storesearch")!=std::string::npos)return payload({{"items",json::array({{{"id",620},{"name",item.second}}})}});
                if(url.find("search/results")!=std::string::npos)return payload({{"success",1},{"results_html",""}});
                return payload(record(620,620,"game",item.second));
            };
            json log=json::array();auto result=resolveSteamGame({{1,"parent-directory-1",item.first,{}}},{item.first},log);
            check(!result||(!result->confident&&result->identityStatus=="needs-confirmation"),"Abbreviation attached an unrelated AppID");
        });
    }
    test("short-official-uppercase-title-still-accepted",[](const auto&){
        artworkTestTransport=[](const auto&,const auto&,const auto& url,const auto&){
            if(url.find("storesearch")!=std::string::npos)return payload({{"items",json::array({{{"id",620},{"name","DOOM"}}})}});
            return payload(record(620,620,"game","DOOM"));};
        json log=json::array();auto result=resolveSteamGame({{1,"parent-directory-1","DOOM",{}}},{"DOOM"},log);
        check(result&&result->confident,"Exact official uppercase title was treated as unsupported acronym");
    });
    test("old-short-alias-cache-cannot-reintroduce-wrong-identity",[](const auto&){
        for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"PZ","PZ Gravity"},{"APEX","Apex"},{"Game","Game"}}){
            json match={{"appId",620},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},
                {"resolverVersion",STEAM_RESOLVER_VERSION},{"source","parent-directory-1"},{"query",pair.first},{"formalName",pair.second}};
            check(!custom_steam_library::verifiedSteamIdentityMatch(match),"Old bad match bypassed new discovery policy");
        }
    });
    test("known-software-is-excluded-despite-appdetails-game-type",[](const auto&){
        for(const auto* name:{"Wallpaper Engine","Crosshair X","OBS Studio","Soundpad"}){
            check(custom_steam_library::builtinExcludedSteamTool({{"name",name},{"type","game"}}),"Legacy endpoint mislabeled software as a game");
        }
        check(!custom_steam_library::builtinExcludedSteamTool({{"name","Wallpaper Engine Adventure"}}),"Software filter became a broad substring ban");
    });
    auto iconTransport=[](int& calls,const std::string& title="Portal 2",int width=128){
        artworkTestTransport=[&calls,title,width](const auto&,const auto&,const auto& url,const auto&){
            ++calls;
            if(url.find("autocomplete")!=std::string::npos)return payload({{"success",true},{"data",json::array({{{"id",99},{"name",title}}})}});
            return payload({{"success",true},{"data",{{"assets",json::array({{{"id",7},{"url","https://cdn2.steamgriddb.com/icon/test.png"},{"width",width},{"height",width}}})}}}});
        };
    };
    test("sgdb-substring-sequel-does-not-download-wrong-icon",[&](const auto&){int calls=0;iconTransport(calls,"Portal 2");const auto r=steamGridDbIconSearch("Portal");check(r["candidates"].empty()&&calls==1,"Sequel icon accepted for base game");});
    test("sgdb-valid-128px-icon-is-not-discarded",[&](const auto&){int calls=0;iconTransport(calls);const auto r=steamGridDbIconSearch("Portal 2");check(r["candidates"].size()==1&&calls==2,"Valid small icon rejected");});
    test("sgdb-positive-cache-reuses-exact-title-only",[&](const auto&){int calls=0;iconTransport(calls);steamGridDbIconSearch("Portal 2");const auto r=steamGridDbIconSearch("Portal 2");check(calls==2&&r["candidates"].size()==1,"Exact positive icon cache missed");steamGridDbIconSearch("Portal");check(calls==3,"Cache leaked into another query");});
    test("sgdb-confirmed-absence-is-short-negative-cached",[&](const auto&){int calls=0;iconTransport(calls,"Portal 3");steamGridDbIconSearch("Portal 2");steamGridDbIconSearch("Portal 2");check(calls==1,"Confirmed absence retried each scan");});
    test("sgdb-network-failure-never-becomes-no-icon-cache",[](const auto&){int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&)->HttpResponse{++calls;throw std::runtime_error("offline");};for(int i=0;i<2;++i)mustFail([]{steamGridDbIconSearch("Portal 2");});check(calls>=2,"Offline permanently hid icon");});
    test("sgdb-duplicate-exact-titles-abstain",[](const auto&){int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"success",true},{"data",json::array({{{"id",1},{"name","Portal"}},{{"id",2},{"name","Portal"}}})}});};const auto r=steamGridDbIconSearch("Portal");check(r["candidates"].empty()&&calls==1,"Ambiguous exact SGDB games picked arbitrary icon");});
    for(const auto* fault:{"future-time","expired","wrong-query","bad-url","overflow-width","malformed-json"}){
        const auto name=std::string("sgdb-cache-recovers-")+fault;
        test(name.c_str(),[&,fault](const auto&){
            int calls=0;iconTransport(calls);steamGridDbIconSearch("Portal 2");
            const auto file=fs::directory_iterator(g_workerDataRoot/L"cache"/L"steamgriddb-icons")->path();auto data=loadJsonDocument(file);
            const std::string kind=fault;
            if(kind=="future-time")data["fetchedAt"]=unixTimeMs()+60000;
            if(kind=="expired")data["fetchedAt"]=unixTimeMs()-13ull*60*60*1000;
            if(kind=="wrong-query")data["query"]="Other Game";
            if(kind=="bad-url")data["candidates"][0]["url"]="https://cdn2.steamgriddb.com.evil.invalid/icon/test.png";
            if(kind=="overflow-width")data["candidates"][0]["width"]=18446744073709551615ull;
            writeJsonAtomic(file,data);
            if(kind=="malformed-json")writeAtomic(file,std::vector<unsigned char>{'{','b','a','d'});
            const auto r=steamGridDbIconSearch("Portal 2");check(calls==4&&r["candidates"].size()==1,"Damaged icon cache suppressed correct fresh lookup");
        });
    }
    test("sgdb-locked-cache-cannot-fail-valid-candidates",[&](const auto&){int calls=0;iconTransport(calls);selftestMoveFault=[](const auto&,const auto& to){return to.find(L"steamgriddb-icons")!=std::wstring::npos?DWORD{ERROR_ACCESS_DENIED}:DWORD{0};};const auto r=steamGridDbIconSearch("Portal 2");check(r["candidates"].size()==1,"Cache write failure failed optional discovery");});
    test("nested-third-party-budget-reserves-official-icon-download",[](const auto&){ScopedOptionalArtworkBudget slot(true);const auto slotEnd=g_optionalArtworkDeadline;{ScopedOptionalArtworkBudget discovery(true,3000);check(g_optionalArtworkDeadline<=GetTickCount64()+3000&&g_optionalArtworkDeadline<slotEnd,"SGDB consumed entire icon budget");}check(g_optionalArtworkDeadline==slotEnd,"Discovery deadline leaked into official download");});

    test("search-requests-preserve-real-word-boundaries",[](const auto&){check(searchQueryKey("MoonHarvest")!=searchQueryKey("Moon Harvest"),"Joined and spaced search terms deduplicated");check(searchQueryKey("Ｍｏｏｎ Harvest")==searchQueryKey("Moon Harvest"),"Width/case normalization drift");});
    test("humanized-installed-name-retains-a-search-round",[](const auto& p){const auto rounds=buildNameRounds(p/L"MoonHarvest"/L"MoonHarvest.exe",{},true,{},{});bool joined=false,spaced=false;for(const auto& r:rounds)if(r.skipReason.empty()){joined=joined||r.value=="MoonHarvest";spaced=spaced||r.value=="Moon Harvest";}check(joined&&spaced,"Actual alternative search query was lost before resolver");});
    test("local-appid-corroborated-by-official-bilingual-title-component",[](const auto& p){const auto exe=p/L"MoonHarvest.exe";const std::vector<LocalSteamAppIdEvidence> e={{620,p/L"steam_appid.txt","plain-steam-appid",1.0,{},{},false}};const std::vector<NameRound> r={{1,"pe-product-name","MoonHarvest",{}}};const auto result=assessLocalSteamAppIdIdentity(620,e,exe,r,"月光牧场 / Moon Harvest");check(result["accepted"]==true&&result["bestInstalledAliasScore"]==1.0,"Exact official English component rejected");});
    for(const auto* title:{"月光牧场 / Moon Harvest 2","月光牧场 / Moon Harvest Soundtrack","Other Game / Moon Harvest"}){
        const auto name=std::string("official-alias-does-not-erase-sequel-addon-or-same-language-subtitle-")+title;
        test(name.c_str(),[=](const auto& p){const std::vector<LocalSteamAppIdEvidence> e={{620,p/L"steam_appid.txt","plain-steam-appid",1.0,{},{},false}};const auto a=assessLocalSteamAppIdIdentity(620,e,p/L"MoonHarvest.exe",{{1,"pe-product-name","MoonHarvest",{}}},title);check(a["accepted"]==false,"Alias matching accepted a different title or addon");});
    }
    test("bilingual-alias-does-not-override-conflicting-local-appids",[](const auto& p){const std::vector<LocalSteamAppIdEvidence> e={{620,p/L"a.ini","appid",1.0,{},{},false},{621,p/L"b.ini","appid",1.0,{},{},false}};const auto a=assessLocalSteamAppIdIdentity(620,e,p/L"MoonHarvest.exe",{{1,"pe-product-name","MoonHarvest",{}}},"月光牧场 / Moon Harvest");check(a["accepted"]==false,"Name agreement overrode conflicting IDs");});
    auto fixtureExe=[](const fs::path& path){writeAtomic(path,std::vector<unsigned char>{'f','i','x','t','u','r','e'});};
    test("ymcc-built-in-blacklist-is-shared-and-applies-to-real-leaf-only",[](const auto&){const auto rules=libraryExecutableExclusions();for(const auto* exe:{L"Steam.EXE",L"LosslessScaling.exe",L"UnityCrashHandler64.exe",L"qemu-system-x86_64.exe"})check(libraryExecutableExclusion(fs::path(L"G:/Games")/exe,rules).has_value(),"Shared builtin rule missed");check(!libraryExecutableExclusion(L"G:/SteamGames/SteamQuest.exe",rules),"Directory substring became executable blacklist");});
    test("system-and-player-wildcards-bom-comments-are-obeyed",[](const auto& p){const std::string system="\xEF\xBB\xBF" "Blocked*.EXE # comment\n",player="MyTool?.exe\n";writeAtomic(p/L"system-blacklist.txt",std::vector<unsigned char>(system.begin(),system.end()));writeAtomic(p/L"player-blacklist.txt",std::vector<unsigned char>(player.begin(),player.end()));const auto rules=libraryExecutableExclusions();check(libraryExecutableExclusion(L"G:/BlockedSupport.exe",rules)&&libraryExecutableExclusion(L"G:/MyTool1.exe",rules),"Rule parsing differs from YMCC");check(!libraryExecutableExclusion(L"G:/MyTool10.exe",rules),"Question mark wildcard broadened");});
    test("blacklisted-exe-is-filtered-before-ranking-and-retains-audit",[&](const auto& p){const auto directory=p/L"RealGame";fixtureExe(directory/L"Steam.exe");fixtureExe(directory/L"RealGame.exe");const auto game=scanOneGameDirectory(directory,5,defaultLibraryConfig(),{});check(game["primaryExecutable"]==toUtf8((directory/L"RealGame.exe").wstring()),"Blacklisted helper became primary");check(game["excludedExecutables"].size()==1,"Excluded candidate has no audit");for(const auto& c:game["candidates"])check(c["path"]!=toUtf8((directory/L"Steam.exe").wstring()),"Blacklisted executable reached visible candidates");});
    test("manual-primary-cannot-bypass-explicit-blacklist",[&](const auto& p){const auto d=p/L"Install";fixtureExe(d/L"Steam.exe");auto c=defaultLibraryConfig();c["manualPrimary"][canonicalPathKey(d)]={{"exe",toUtf8((d/L"Steam.exe").wstring())}};const auto g=scanOneGameDirectory(d,5,c,{});check(g["status"]=="unrecognized-tool"&&g["primaryExecutable"].is_null(),"Manual primary bypassed requested blacklist exclusion");});
    test("blocked-direct-identification-never-contacts-providers",[&](const auto& p){const auto exe=p/L"Install"/L"Steam.exe";fixtureExe(exe);int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(record());};SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",g_workerDataRoot.c_str());std::vector<std::wstring> args={L"SteamArtworkLab.exe",exe.wstring(),(p/L"jobs").wstring(),L"--metadata-only",L"--steam-comprehensive-identity-only"};std::vector<wchar_t*> argv;for(auto& a:args)argv.push_back(a.data());check(workerMainNotUsedBySelftest(static_cast<int>(argv.size()),argv.data())!=0&&calls==0,"Blocked target inherited a parent identity or contacted Steam");});
    test("anti-cheat-installer-is-a-helper-not-a-game",[](const auto&){check(definitePidBlacklistRole(classifyLibraryExecutable(L"G:/Game/EAAntiCheat/EAAntiCheat.Installer.exe")),"Anti-cheat classified as main game");});
    test("arbitrarily-named-collection-splits-independent-child-installs",[&](const auto& p){const auto root=p/L"Library",collection=root/L"My Download Shelf";fixtureExe(collection/L"AlphaQuest"/L"AlphaQuest.exe");fixtureExe(collection/L"BetaIsland"/L"BetaIsland.exe");auto config=defaultLibraryConfig();config["roots"]=json::array({toUtf8(root.wstring())});const auto scan=scanLibraryRoots({root},{},5,config);check(scan["games"].size()==2&&scan["collectionContainers"].size()==1,"Unknown collection collapsed into one game");for(const auto& g:scan["games"])check(g["gameDirectory"]!=toUtf8(collection.wstring()),"Collection root became a game");});
    test("collection-inference-preserves-an-installation-with-own-main-exe",[&](const auto& p){const auto d=p/L"AlphaQuest";fixtureExe(d/L"AlphaQuest.exe");fixtureExe(d/L"Mods"/L"Mods.exe");fixtureExe(d/L"Bonus"/L"Bonus.exe");const auto c=defaultLibraryConfig();const auto parent=scanOneGameDirectory(d,5,c,{});check(!independentCollectionGames(d,parent,5,c,{}),"Mods or bundled tools split a normal installation");});
    test("win32-win64-layout-is-one-installation-not-a-collection",[&](const auto& p){const auto d=p/L"GameInstall";fixtureExe(d/L"Win32"/L"Game.exe");fixtureExe(d/L"Win64"/L"Game.exe");const auto c=defaultLibraryConfig();const auto parent=scanOneGameDirectory(d,5,c,{});check(!independentCollectionGames(d,parent,5,c,{}),"Binary architecture folders became distinct games");});
    test("manual-collection-primary-remains-an-explicit-directory-choice",[&](const auto& p){const auto d=p/L"DownloadShelf";fixtureExe(d/L"AlphaQuest"/L"AlphaQuest.exe");fixtureExe(d/L"BetaIsland"/L"BetaIsland.exe");auto c=defaultLibraryConfig();c["manualPrimary"][canonicalPathKey(d)]={{"exe",toUtf8((d/L"AlphaQuest"/L"AlphaQuest.exe").wstring())}};check(!independentCollectionGames(d,scanOneGameDirectory(d,5,c,{}),5,c,{}),"Explicit directory selection was silently split");});
    test("old-collapsed-state-refines-artwork-root-to-the-owning-install",[&](const auto& p){const auto root=p/L"Library",d=root/L"DownloadShelf",exe=d/L"AlphaQuest"/L"AlphaQuest.exe";fixtureExe(exe);fixtureExe(d/L"BetaIsland"/L"BetaIsland.exe");auto c=defaultLibraryConfig();c["roots"]=json::array({toUtf8(root.wstring())});writeJsonAtomic(g_workerDataRoot/L"config"/L"library-config.json",c);writeJsonAtomic(g_workerDataRoot/L"state"/L"library-scan.json",{{"games",json::array({{{"gameDirectory",toUtf8(d.wstring())},{"primaryExecutable",toUtf8(exe.wstring())}}})}});check(canonicalPathKey(persistentGameDirectoryForExecutable(g_workerDataRoot,exe))==canonicalPathKey(exe.parent_path()),"Collapsed directory still shares artwork storage between games");});
    test("native-appmanifest-evidence-is-bound-to-selected-primary",[&](const auto& p){const auto steam=p/L"SteamFixture",d=steam/L"steamapps"/L"common"/L"AlphaQuest",exe=d/L"AlphaQuest.exe";fixtureExe(exe);fixtureExe(d/L"Bonus"/L"Bonus.exe");const std::string acf="\"AppState\" { \"appid\" \"620\" \"name\" \"Alpha Quest\" \"installdir\" \"AlphaQuest\" }";writeAtomic(steam/L"steamapps"/L"appmanifest_620.acf",std::vector<unsigned char>(acf.begin(),acf.end()));const auto evidence=nativeSteamAppManifestEvidence(exe);check(evidence&&evidence->appId==620&&evidence->targetMatchesExecutable,"Actual primary ignored native installation metadata");check(!nativeSteamAppManifestEvidence(d/L"Bonus"/L"Bonus.exe"),"Bundled auxiliary game inherited parent Steam AppID");check(nativeSteamGameBoundaryForExecutable(exe)==d,"Native folder boundary escaped to Steam root");});

    test("crash-dump-runtime-cannot-replace-an-excluded-primary",[](const auto&){check(classifyLibraryExecutable(L"G:/Game/createdump.exe")=="crash-handler","Dump runtime is a candidate game");});
    test("blacklisted-native-application-cannot-resurface-through-another-exe",[&](const auto& p){const auto steam=p/L"SteamFixture",d=steam/L"steamapps"/L"common"/L"BlockedApp";fixtureExe(d/L"BlockedApp.exe");fixtureExe(d/L"DifferentProduct.exe");const std::string rule="BlockedApp.exe\n",acf="\"AppState\" { \"appid\" \"620\" \"name\" \"Blocked App\" \"installdir\" \"BlockedApp\" }";writeAtomic(p/L"system-blacklist.txt",std::vector<unsigned char>(rule.begin(),rule.end()));writeAtomic(steam/L"steamapps"/L"appmanifest_620.acf",std::vector<unsigned char>(acf.begin(),acf.end()));const auto scan=scanLibraryRoots({steam/L"steamapps"/L"common"},{},5,defaultLibraryConfig());check(scan["games"].empty(),"Blocked software resurrected under fallback executable");});

    test("versioned-install-title-corroborates-exact-steam-candidate",[](const auto& p){const auto evidence=buildPathEvidence(p/L"MoonHarvest.v1.2.14-P2P"/L"MoonHarvest.exe");check(scorePathEvidence(evidence,"Moon Harvest")>=0.99,"Search cleaning did not reach independent directory verification");check(scorePathEvidence(evidence,"Moon Harvest 2")<0.94,"Cleaned directory erased sequel mismatch");});

    test("refining-collection-retains-old-ownership-alias-only-for-exact-exe",[&](const auto& p){const auto root=p/L"Library",d=root/L"DownloadShelf",exe=d/L"AlphaQuest"/L"AlphaQuest.exe";fixtureExe(exe);fixtureExe(d/L"BetaIsland"/L"BetaIsland.exe");writeJsonAtomic(g_workerDataRoot/L"state"/L"library-scan.json",{{"games",json::array({{{"gameDirectory",toUtf8(d.wstring())},{"primaryExecutable",toUtf8(exe.wstring())}}})}});const auto scan=scanLibraryRoots({root},{},5,defaultLibraryConfig());size_t matched=0;for(const auto& game:scan["games"]){if(game["primaryExecutable"]==toUtf8(exe.wstring())){check(game["alternateGameDirectories"].size()==1&&game["alternateGameDirectories"][0]==toUtf8(d.wstring()),"Exact legacy owner alias lost");++matched;}else check(!game.contains("alternateGameDirectories"),"Sibling inherited collapsed collection ownership");}check(matched==1,"Original primary EXE disappeared");});

    auto searchUrl=[](const std::string& query){return "https://store.steampowered.com/api/storesearch/?term="+query+"&l=english&cc=us";};
    auto searchCache=[](const std::string& url){return g_workerDataRoot/L"cache"/L"steam-search"/toWide(sha256(std::vector<unsigned char>(url.begin(),url.end()))+".json");};
    const auto candidates=json{{"items",json::array({{{"id",620},{"name","Portal 2"},{"type","app"}}})}};
    auto seedSearch=[&](const std::string& url,const json& data,uint64_t age){const auto at=unixTimeMs()-age;writeJsonAtomic(searchCache(url),{{"schemaVersion",1},{"url",url},{"fetchedAt",at},{"data",data}});return at;};
    test("weak-mixed-search-success-and-verification-timeout-remains-retryable",[](const auto&){const auto log=json::array({{{"attempt",1},{"freshSession",true},{"status",200}},{{"attempt",1},{"freshSession",true},{"retryable",true},{"win32Error",ERROR_WINHTTP_TIMEOUT}}});check(networkVerificationPending(log),"Partial network success hid verification timeout");const auto f=describeResolutionFailure(log,json::array(),false);check(f["kind"]=="network-unavailable"&&f["retryWhenNetworkReturns"]==true&&f["successfulResponses"]==1,"Incomplete search became permanent unrecognized");});
    test("weak-permanent-transport-error-is-not-network-retry",[](const auto&){const auto log=json::array({{{"attempt",1},{"freshSession",true},{"retryable",false},{"win32Error",ERROR_INVALID_DATA}}});check(!networkVerificationPending(log)&&describeResolutionFailure(log,json::array(),true)["kind"]=="manual-id-rejected","Permanent invalid data became pending network");});
    test("weak-definitive-404-stays-nonretryable",[](const auto&){const auto log=json::array({{{"attempt",1},{"freshSession",true},{"status",404}}});check(!networkVerificationPending(log)&&!describeResolutionFailure(log,json::array(),false)["retryWhenNetworkReturns"].get<bool>(),"404 retried as outage");});
    test("weak-shared-identity-budget-prevents-new-endpoint-request",[](const auto&){int calls=0;g_fastMetadataMode=true;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(record());};g_identityNetworkDeadline=GetTickCount64()-1;json log=json::array();mustFail([&]{getJson("https://store.steampowered.com/api/storesearch/?term=A","steam",&log);});mustFail([&]{getJson("https://api.igdb.com/v4/games","igdb",&log);});check(calls==0&&log.size()==2&&networkVerificationPending(log),"Per-endpoint call reset job budget");});
    test("weak-nested-budgets-never-extend-and-restore",[](const auto&){const auto end=GetTickCount64()+1000;g_identityNetworkDeadline=end;{ScopedIdentityNetworkBudget scope(true,50000);check(g_identityNetworkDeadline==end&&effectiveHttpDeadline(end+20000)==end,"Nested identity budget extended");{ScopedOptionalArtworkBudget optional(true,100);check(effectiveHttpDeadline(end)<=end,"Optional budget extended identity");}}check(g_identityNetworkDeadline==end&&g_optionalArtworkDeadline==0,"Budget leaked outside scope");});
    test("weak-endpoint-key-ignores-query-keeps-service-port-and-path",[&](const auto&){check(metadataEndpointKey(searchUrl("First"))==metadataEndpointKey(searchUrl("Second")),"Query leaked into limiter key");check(metadataEndpointKey(searchUrl("First"))!=metadataEndpointKey("https://store.steampowered.com/api/appdetails?appids=620"),"Independent endpoint was blocked");check(metadataEndpointKey("https://example.invalid:444/api/test?a=1")!=metadataEndpointKey("https://example.invalid:443/api/test?a=1"),"Ports lost identity");});
    test("weak-retry-after-preserves-server-delay-beyond-inline-cap",[](const auto&){check(parseRetryAfterMs(L"120")==30000&&parseRetryAfterMs(L"120",HTTP_MAX_PROVIDER_COOLDOWN_MS)==120000,"Server delay truncated to inline sleep cap");});
    test("weak-automatic-429-defers-once-and-shares-across-queries",[&](const auto&){g_fastMetadataMode=true;int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;auto r=HttpResponse{429};r.serverRetryAfterMs=120000;return r;};json log=json::array();mustFail([&]{getJson(searchUrl("First"),"steam",&log);});mustFail([&]{getJson(searchUrl("Second"),"steam",&log);});check(calls==1&&log.back().value("skippedNetwork",false)&&log.front()["retryAfterMs"]==120000,"Rate limit retried per game/query");check(networkVerificationPending(log),"Backoff became no match");});
    test("weak-endpoint-backoff-survives-a-new-worker-memory",[&](const auto&){g_fastMetadataMode=true;rememberMetadataBackoff(searchUrl("First"),429,60000);g_metadataBackoffs.clear();int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(candidates);};json log=json::array();mustFail([&]{getJson(searchUrl("Other"),"steam",&log);});check(calls==0&&log.back()["skippedNetwork"]==true,"Other worker ignored persisted cooldown");});
    test("weak-search-rate-limit-does-not-block-appdetails-or-igdb",[&](const auto&){g_fastMetadataMode=true;rememberMetadataBackoff(searchUrl("A"),429,60000);int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(record());};json log=json::array();fetchSteamDetails(620,log);getJson("https://api.igdb.com/v4/games","igdb",&log);check(calls==2,"Search limiter blocked independent evidence");});
    test("weak-valid-details-cache-bypasses-network-cooldown",[](const auto&){g_fastMetadataMode=true;saveSteamDetailsCache(steamDetailsRecord(record(),620));rememberMetadataBackoff("https://store.steampowered.com/api/appdetails?appids=570",429,60000);int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return HttpResponse{503};};json log=json::array();check(fetchSteamDetails(620,log).canonicalAppId==620&&calls==0,"Valid cache waited on unhealthy endpoint");});
    test("weak-expired-cooldown-allows-recovery-request",[&](const auto&){g_fastMetadataMode=true;const auto url=searchUrl("A"),endpoint=metadataEndpointKey(url);writeJsonAtomic(metadataBackoffCachePath(endpoint),{{"schemaVersion",1},{"endpoint",endpoint},{"status",429},{"observedAt",unixTimeMs()-40000},{"retryAt",unixTimeMs()-1000}});int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(candidates);};check(getJson(url).contains("items")&&calls==1,"Expired throttle never recovered");});
    test("weak-future-or-wrong-endpoint-backoff-is-rejected",[&](const auto&){const auto url=searchUrl("A"),endpoint=metadataEndpointKey(url);const auto path=metadataBackoffCachePath(endpoint);for(const auto& fake:json::array({{{"schemaVersion",1},{"endpoint",endpoint},{"status",429},{"observedAt",unixTimeMs()+60000},{"retryAt",unixTimeMs()+120000}},{{"schemaVersion",1},{"endpoint","https://other.invalid/x"},{"status",429},{"observedAt",unixTimeMs()},{"retryAt",unixTimeMs()+60000}},{{"schemaVersion",1},{"endpoint",endpoint},{"status",404},{"observedAt",unixTimeMs()},{"retryAt",unixTimeMs()+60000}}})){writeJsonAtomic(path,fake);check(loadMetadataBackoff(url).empty(),"Invalid scheduling data suppressed network");}});
    test("weak-late-success-cannot-clear-newer-rate-limit",[&](const auto&){const auto started=unixTimeMs()-1;rememberMetadataBackoff(searchUrl("A"),429,60000);clearMetadataBackoff(searchUrl("B"),started);check(!loadMetadataBackoff(searchUrl("C")).empty(),"Late success erased another worker rate limit");});
    test("weak-longer-cooldown-is-not-shortened",[&](const auto&){rememberMetadataBackoff(searchUrl("A"),429,120000);const auto before=loadMetadataBackoff(searchUrl("A"));rememberMetadataBackoff(searchUrl("B"),503,15000);check(loadMetadataBackoff(searchUrl("C"))["retryAt"]==before["retryAt"],"Short failure overwrote server Retry-After");});
    test("weak-readonly-backoff-still-throttles-in-process",[&](const auto&){g_fastMetadataMode=true;selftestMoveFault=[](const auto&,const auto& to){return to.find(L"network-backoff")!=std::wstring::npos?DWORD{ERROR_ACCESS_DENIED}:DWORD{0};};rememberMetadataBackoff(searchUrl("A"),429,60000);check(!loadMetadataBackoff(searchUrl("B")).empty(),"Read-only data root lost in-memory scheduling");});
    test("weak-explicit-manual-request-can-probe-recovery",[&](const auto&){rememberMetadataBackoff(searchUrl("A"),0,15000);int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(candidates);};getJson(searchUrl("A"));check(calls==1,"Automatic limiter prevented explicit user retry");});
    test("weak-timeout-failure-shares-short-backoff",[&](const auto&){g_fastMetadataMode=true;int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&)->HttpResponse{++calls;throw HttpTransportError("offline",ERROR_WINHTTP_CANNOT_CONNECT,true);};json log=json::array();mustFail([&]{getJson(searchUrl("A"),"steam",&log);});const auto after=calls;mustFail([&]{getJson(searchUrl("B"),"steam",&log);});check(after==2&&calls==after&&log.back()["skippedNetwork"]==true,"Offline loop retried every keyword");});
    test("weak-positive-stale-search-survives-timeout-without-refreshing-age",[&](const auto&){const auto url=searchUrl("Portal2");const auto at=seedSearch(url,candidates,7ull*60*60*1000);artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&)->HttpResponse{throw HttpTransportError("timeout",ERROR_WINHTTP_TIMEOUT,true);};json log=json::array();const auto r=steamCandidateSearchJson(url,&log);check(r==candidates&&log.back()["stale"]==true&&!log.back()["identityAuthority"].get<bool>(),"Known positive inventory was discarded");check(loadJsonDocument(searchCache(url))["fetchedAt"]==at,"Outage extended stale cache age");});
    test("weak-stale-search-does-not-grant-steam-identity",[&](const auto&){const auto url=searchUrl("Portal2");seedSearch(url,candidates,7ull*60*60*1000);artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&)->HttpResponse{throw HttpTransportError("offline",ERROR_WINHTTP_TIMEOUT,true);};json log=json::array();check(steamCandidateSearchJson(url,&log)==candidates,"No candidate fallback");mustFail([&]{fetchSteamDetails(620,log);});check(!fs::exists(steamDetailsCachePath(620)),"Stale inventory became formal identity");});
    test("weak-empty-search-cannot-use-stale-negative-fallback",[&](const auto&){const auto url=searchUrl("None");seedSearch(url,{{"items",json::array()}},7ull*60*60*1000);artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&)->HttpResponse{throw HttpTransportError("offline",ERROR_WINHTTP_TIMEOUT,true);};json log=json::array();mustFail([&]{steamCandidateSearchJson(url,&log);});});
    test("weak-too-old-search-inventory-is-rejected",[&](const auto&){const auto url=searchUrl("Portal2");seedSearch(url,candidates,8ull*24*60*60*1000);artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&)->HttpResponse{throw HttpTransportError("offline",ERROR_WINHTTP_TIMEOUT,true);};json log=json::array();mustFail([&]{steamCandidateSearchJson(url,&log);});});
    test("weak-stale-search-cannot-hide-permanent-http-error",[&](const auto&){const auto url=searchUrl("Portal2");seedSearch(url,candidates,7ull*60*60*1000);artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&){return HttpResponse{404};};json log=json::array();mustFail([&]{steamCandidateSearchJson(url,&log);});});
    test("weak-stale-search-keeps-invalid-json-visible",[&](const auto&){const auto url=searchUrl("Portal2");seedSearch(url,candidates,7ull*60*60*1000);const auto before=readBinaryFile(searchCache(url));artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&){const std::string body="<html>error</html>";return HttpResponse{200,{},std::vector<unsigned char>(body.begin(),body.end())};};json log=json::array();check(steamCandidateSearchJson(url,&log)==candidates,"Prior search progress was lost");check(networkVerificationPending(log)&&before==readBinaryFile(searchCache(url)),"Malformed live response was hidden or old success redated");mustFail([&]{fetchSteamDetails(620,log);});check(!fs::exists(steamDetailsCachePath(620)),"Stale candidates bypassed formal identity validation");});
    test("weak-identical-successful-refresh-updates-search-age",[&](const auto&){const auto url=searchUrl("Portal2");const auto old=seedSearch(url,candidates,7ull*60*60*1000);artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(candidates);};json log=json::array();check(steamCandidateSearchJson(url,&log)==candidates&&loadJsonDocument(searchCache(url))["fetchedAt"].get<uint64_t>()>old,"Identical actual network success remained stale");});
    test("weak-429-stale-fallback-keeps-next-query-throttled",[&](const auto&){g_fastMetadataMode=true;const auto url=searchUrl("Portal2");seedSearch(url,candidates,7ull*60*60*1000);int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return HttpResponse{429};};json log=json::array();check(steamCandidateSearchJson(url,&log)==candidates,"429 discarded usable inventory");mustFail([&]{steamCandidateSearchJson(searchUrl("NewQuery"),&log);});check(calls==1&&!loadMetadataBackoff(url).empty(),"Stale result erased rate limit");});

    const std::string igdbSearch="https://api2.playnite.link/api/igdb/search",igdbDetails="https://api2.playnite.link/api/igdb/game/7";
    const auto igdbBody=json{{"SearchTerm","Test Game"}},igdbInventory=json{{"Data",json::array({{{"id",7},{"name","Test Game"}}})}},igdbRecord=json{{"Data",{{"id",7},{"name","Test Game"},{"game_type",0}}}};
    auto seedIgdb=[&](const std::string& url,const json& body,const json& data,uint64_t age){writeJsonAtomic(igdbResponseCachePath(url,body),{{"schemaVersion",1},{"url",url},{"requestBody",body},{"fetchedAt",unixTimeMs()-age},{"data",data}});};
    test("weak-igdb-search-cache-preserves-exact-post-body",[&](const auto&){int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(igdbInventory);};json log=json::array();postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log);postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log);check(calls==1&&log.back()["cacheHit"]==true,"Successful search repeated on retry");postCachedIgdbJson(igdbSearch,{{"SearchTerm","Test Game 2"}},"playnite-igdb",&log);check(calls==2,"Other sequel reused query cache");});
    test("weak-igdb-detail-cache-bound-to-exact-numeric-id",[&](const auto&){int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(igdbRecord);};json log=json::array();getCachedIgdbJson(igdbDetails,"playnite-igdb",&log);getCachedIgdbJson(igdbDetails,"playnite-igdb",&log);check(calls==1&&!log.back()["steamIdentityAuthority"].get<bool>(),"Fresh detail cache was not reused safely");mustFail([&]{getCachedIgdbJson("https://api2.playnite.link/api/igdb/game/8","playnite-igdb",&log);});check(calls==2,"Other ID reused detail cache");check(!fs::exists(igdbResponseCachePath("https://api2.playnite.link/api/igdb/game/8",nullptr)),"Mismatched detail ID was cached");});
    test("weak-igdb-api-error-is-never-cached-as-absence",[&](const auto&){int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"Error","Unavailable"},{"Data",json::array()}});};json log=json::array();for(int n=0;n<2;++n)mustFail([&]{postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log);});check(calls==2&&networkVerificationPending(log)&&!fs::exists(igdbResponseCachePath(igdbSearch,igdbBody)),"Provider error became negative cache or lost retry evidence");});
    test("weak-igdb-positive-stale-search-never-refreshes-age",[&](const auto&){seedIgdb(igdbSearch,igdbBody,igdbInventory,7ull*60*60*1000);const auto path=igdbResponseCachePath(igdbSearch,igdbBody);const auto before=loadJsonDocument(path)["fetchedAt"];artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&)->HttpResponse{throw HttpTransportError("offline",ERROR_WINHTTP_TIMEOUT,true);};json log=json::array();check(postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log)==igdbInventory&&log.back()["stale"]==true,"No positive fallback");check(loadJsonDocument(path)["fetchedAt"]==before,"Outage extended IGDB cache age");});
    test("weak-expired-igdb-detail-cannot-grant-formal-metadata",[&](const auto&){seedIgdb(igdbDetails,nullptr,igdbRecord,13ull*60*60*1000);artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&)->HttpResponse{throw HttpTransportError("offline",ERROR_WINHTTP_TIMEOUT,true);};json log=json::array();mustFail([&]{getCachedIgdbJson(igdbDetails,"playnite-igdb",&log);});});
    test("weak-empty-igdb-search-has-short-negative-lifetime",[&](const auto&){seedIgdb(igdbSearch,igdbBody,{{"Data",json::array()}},6ull*60*1000);artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&)->HttpResponse{throw HttpTransportError("offline",ERROR_WINHTTP_TIMEOUT,true);};json log=json::array();mustFail([&]{postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log);});});
    test("weak-igdb-future-cache-cannot-suppress-live-query",[&](const auto&){const auto path=igdbResponseCachePath(igdbSearch,igdbBody);writeJsonAtomic(path,{{"schemaVersion",1},{"url",igdbSearch},{"requestBody",igdbBody},{"fetchedAt",unixTimeMs()+60000},{"data",igdbInventory}});int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(igdbInventory);};json log=json::array();postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log);check(calls==1,"Future IGDB cache became authority");});
    test("weak-igdb-cache-write-failure-keeps-success",[&](const auto&){selftestMoveFault=[](const auto&,const auto& to){return to.find(L"igdb-responses")!=std::wstring::npos?DWORD{ERROR_ACCESS_DENIED}:DWORD{0};};artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(igdbRecord);};json log=json::array();check(getCachedIgdbJson(igdbDetails,"playnite-igdb",&log)==igdbRecord,"Read-only cache failed successful lookup");});
    test("weak-igdb-cache-cannot-bypass-content-type-verification",[&](const auto&){auto wrong=igdbRecord;wrong["Data"]["game_type"]=1;seedIgdb(igdbDetails,nullptr,wrong,1000);json log=json::array();mustFail([&]{verifyIgdbId(7,"pc",log);});});
    test("weak-fresh-igdb-cache-works-after-network-budget-exhaustion",[&](const auto&){seedIgdb(igdbSearch,igdbBody,igdbInventory,1000);g_identityNetworkDeadline=GetTickCount64()-1;json log=json::array();check(postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log)==igdbInventory,"Deadline discarded completed query progress");});

    test("weak-html-rate-limit-cannot-disable-next-compact-query",[](const auto&){g_fastMetadataMode=true;ScopedIdentityNetworkBudget budget(true);int html=0,compact=0;artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){if(url.find("search/results")!=std::string::npos){++html;return HttpResponse{429};}if(url.find("storesearch")!=std::string::npos){++compact;return payload({{"items",url.find("term=Portal%202&")!=std::string::npos?json::array({{{"id",620},{"name","Portal 2"}}}):json::array()}});}return payload(record());};json log=json::array();const auto match=resolveSteamGame({{1,"parent-directory-1","UnhelpfulReleaseName",{}},{2,"pe-product-name","Portal 2",{}}},{"Portal 2"},log);check(match&&match->steamVerificationStatus=="steam-verified"&&match->appId==620,"Broken HTML route stopped healthy exact compact evidence");check(html==1&&compact>1,"Throttled HTML was repeated or later compact skipped");});

    test("weak-parent-native-game-name-cannot-identify-an-independent-bundled-exe",[&](const auto& p){const auto steam=p/L"SteamFixture",parent=steam/L"steamapps"/L"common"/L"AlphaQuest",child=parent/L"BetaIsland",exe=child/L"BetaIsland.exe";fixtureExe(parent/L"AlphaQuest.exe");fixtureExe(exe);const std::string acf="\"AppState\" { \"appid\" \"620\" \"name\" \"Alpha Quest\" \"installdir\" \"AlphaQuest\" }";writeAtomic(steam/L"steamapps"/L"appmanifest_620.acf",std::vector<unsigned char>(acf.begin(),acf.end()));const auto boundary=persistentGameDirectoryForExecutable(g_workerDataRoot,exe);check(boundary==child,"Bundled game retained outer native directory");const auto evidence=buildPathEvidence(exe,boundary);check(scorePathEvidence(evidence,"Alpha Quest")<0.94,"Parent title leaked into independent path evidence");const auto rounds=buildNameRounds(exe,{},true,{},{},{},boundary);for(const auto& r:rounds)check(r.value.find("AlphaQuest")==std::string::npos,"Parent name still entered online queries");check(!nativeSteamAppManifestEvidence(exe),"Bundled EXE inherited outer AppID");});
    test("weak-path-evidence-stops-at-known-game-root",[](const auto& p){const auto root=p/L"UnrelatedParentGame"/L"ActualGame";const auto evidence=buildPathEvidence(root/L"bin"/L"ActualGame.exe",root);for(const auto& v:evidence)check(v.find("UnrelatedParentGame")==std::string::npos,"Identity score used unrelated parent outside game boundary");check(scorePathEvidence(evidence,"ActualGame")>=0.99,"True game root evidence was lost");});

    test("round6-igdb-live-mismatched-detail-id-cannot-verify",[&](const auto&){auto swapped=igdbRecord;swapped["Data"]["id"]=8;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(swapped);};json log=json::array();mustFail([&]{verifyIgdbId(7,"pc",log);});check(!fs::exists(igdbResponseCachePath(igdbDetails,nullptr)),"Mismatched identity was cached");});
    test("round6-igdb-live-detail-without-id-cannot-verify",[&](const auto&){auto missing=igdbRecord;missing["Data"].erase("id");artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(missing);};json log=json::array();mustFail([&]{verifyIgdbId(7,"pc",log);});});
    test("round6-igdb-detail-changed-to-dlc-cannot-use-base-game-search-type",[&](const auto&){auto changed=igdbRecord;changed["Data"]["game_type"]=1;artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){return payload(url.find("/search")!=std::string::npos?igdbInventory:changed);};json log=json::array();const auto result=resolveIgdbGame({{1,"manual-name","Test Game",{}}},{"Test Game"},"pc",log);check(!result||!result->confident,"Base-game search type overrode authoritative DLC detail");});
    test("round6-later-shorter-failure-survives-an-earlier-success",[&](const auto&){const auto url=searchUrl("A"),endpoint=metadataEndpointKey(url);const auto now=unixTimeMs();writeJsonAtomic(metadataBackoffCachePath(endpoint),{{"schemaVersion",1},{"endpoint",endpoint},{"status",429},{"observedAt",now-5000},{"retryAt",now+120000}});rememberMetadataBackoff(url,503,15000);clearMetadataBackoff(url,now-1000);check(!loadMetadataBackoff(url).empty(),"Newer failure was erased because its delay was shorter");});
    test("round6-steam-error-envelope-is-not-negative-cached",[&](const auto&){int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array()},{"success",false},{"error","temporarily unavailable"}});};const auto url=searchUrl("TransientError");json log=json::array();mustFail([&]{steamCandidateSearchJson(url,&log);});check(!fs::exists(searchCache(url)),"HTTP200 provider error became five-minute no-results");});

    test("round6-igdb-decimal-detail-id-is-not-truncated",[&](const auto&){auto wrong=igdbRecord;wrong["Data"]["id"]=7.25;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(wrong);};json log=json::array();mustFail([&]{verifyIgdbId(7,"pc",log);});});
    test("round6-igdb-negative-detail-id-is-rejected",[&](const auto&){auto wrong=igdbRecord;wrong["Data"]["id"]=-7;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(wrong);};json log=json::array();mustFail([&]{verifyIgdbId(7,"pc",log);});});
    test("round6-igdb-valid-fresh-detail-still-verifies",[&](const auto&){seedIgdb(igdbDetails,nullptr,igdbRecord,1000);json log=json::array();const auto r=verifyIgdbId(7,"pc",log);check(r.igdbId==7&&r.confident&&r.metadataVerificationStatus=="metadata-verified","Correct cached detail regressed");});
    test("round6-latest-failure-observation-is-persisted-for-other-workers",[&](const auto&){const auto url=searchUrl("A"),endpoint=metadataEndpointKey(url);const auto now=unixTimeMs();writeJsonAtomic(metadataBackoffCachePath(endpoint),{{"schemaVersion",1},{"endpoint",endpoint},{"status",429},{"observedAt",now-5000},{"retryAt",now+120000}});rememberMetadataBackoff(url,503,15000);g_metadataBackoffs.clear();const auto combined=loadMetadataBackoff(url);check(combined["observedAt"].get<uint64_t>()>=now&&combined["retryAt"].get<uint64_t>()>=now+120000,"Cross-process state forgot newest observation or longest delay");});
    test("round6-steam-error-envelope-stays-retryable-after-200",[&](const auto&){artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&){return payload({{"items",json::array()},{"success",false}});};json log=json::array();mustFail([&]{steamCandidateSearchJson(searchUrl("A"),&log);});check(networkVerificationPending(log),"HTTP200 error became definitive no-game");});
    test("round6-cached-steam-error-envelope-is-not-a-cache-hit",[&](const auto&){const auto url=searchUrl("A");seedSearch(url,{{"items",json::array()},{"success",false}},1000);int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(candidates);};json log=json::array();check(steamCandidateSearchJson(url,&log)==candidates&&calls==1,"Previously written provider error blocked recovery");});
    test("round6-valid-empty-steam-search-retains-short-cache",[&](const auto&){const auto url=searchUrl("NoMatch");const auto empty=json{{"items",json::array()}};int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(empty);};json log=json::array();check(steamCandidateSearchJson(url,&log)==empty&&steamCandidateSearchJson(url,&log)==empty&&calls==1,"Real no-result inventory lost its short cache");});

    // Production JSON consumers must preserve progress and retryability even
    // when a weak connection/proxy returns HTTP 200 with an unusable payload.
    auto rawResponse=[](const std::string& text){return HttpResponse{200,{},std::vector<unsigned char>(text.begin(),text.end())};};
    for(const auto& bad:std::vector<std::pair<std::string,std::string>>{
        {"truncated","{\"items\":["},{"html","<!doctype html><title>Bad gateway</title>"},{"empty",""}}){
        const auto label="network-json-steam-"+bad.first;
        test(label.c_str(),[&](const auto&){
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return rawResponse(bad.second);};
            json log=json::array();mustFail([&]{steamCandidateSearchJson(searchUrl("A"),&log);});
            check(networkVerificationPending(log),"HTTP200 malformed Steam JSON became a permanent no-game result");
            check(!fs::exists(searchCache(searchUrl("A"))),"Malformed JSON poisoned candidate cache");
        });
        const auto postLabel="network-json-igdb-post-"+bad.first;
        test(postLabel.c_str(),[&](const auto&){
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return rawResponse(bad.second);};
            json log=json::array();mustFail([&]{postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log);});
            check(networkVerificationPending(log),"HTTP200 malformed IGDB POST lost its retry evidence");
            check(!fs::exists(igdbResponseCachePath(igdbSearch,igdbBody)),"Malformed POST polluted search cache");
        });
    }
    for(const auto& bad:std::vector<std::pair<std::string,json>>{
        {"null",nullptr},{"array",json::array()},{"error",{{"items",json::array()},{"success",false}}},
        {"missing-items",json::object()},{"gateway",{{"error","temporarily unavailable"}}}}){
        const auto label="network-stale-steam-envelope-"+bad.first;
        test(label.c_str(),[&](const auto&){
            const auto url=searchUrl("A");seedSearch(url,candidates,7ull*60*60*1000);
            const auto before=readBinaryFile(searchCache(url));
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(bad.second);};
            json log=json::array();check(steamCandidateSearchJson(url,&log)==candidates,"Invalid Steam envelope discarded previous successful search");
            check(networkVerificationPending(log),"Stale candidates erased current endpoint failure");
            check(before==readBinaryFile(searchCache(url)),"Stale search was redated or replaced by provider error");
            check(!fs::exists(steamDetailsCachePath(620)),"Search fallback granted formal Steam identity");
        });
    }
    test("network-stale-steam-malformed-json-keeps-original-cache",[&](const auto&){
        const auto url=searchUrl("A");seedSearch(url,candidates,7ull*60*60*1000);const auto before=readBinaryFile(searchCache(url));
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return rawResponse("{\"items\":[");};
        json log=json::array();check(steamCandidateSearchJson(url,&log)==candidates,"Malformed JSON bypassed stale positive fallback");
        check(networkVerificationPending(log)&&before==readBinaryFile(searchCache(url)),"Fallback lost failure or rewrote successful inventory");
    });
    for(const auto& bad:std::vector<std::pair<std::string,json>>{
        {"null",nullptr},{"array",json::array()},{"missing-data",json::object()},
        {"error",{{"Error","temporarily unavailable"},{"Data",json::array()}}},{"wrong-data",{{"Data",json::object()}}}}){
        const auto label="network-igdb-search-envelope-"+bad.first;
        test(label.c_str(),[&](const auto&){
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(bad.second);};
            json log=json::array();mustFail([&]{postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log);});
            check(networkVerificationPending(log),"Invalid IGDB inventory became a definitive empty search");
            check(!fs::exists(igdbResponseCachePath(igdbSearch,igdbBody)),"Invalid IGDB search was cached");
        });
        const auto staleLabel="network-stale-igdb-envelope-"+bad.first;
        test(staleLabel.c_str(),[&](const auto&){
            seedIgdb(igdbSearch,igdbBody,igdbInventory,7ull*60*60*1000);
            const auto path=igdbResponseCachePath(igdbSearch,igdbBody);
            const auto bytes=readBinaryFile(path);
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(bad.second);};
            json log=json::array();check(postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log)==igdbInventory,"IGDB provider error discarded previous successful search");
            check(networkVerificationPending(log)&&bytes==readBinaryFile(path),"IGDB fallback lost error or redated old cache");
        });
    }
    test("network-stale-igdb-malformed-json-keeps-original-cache",[&](const auto&){
        seedIgdb(igdbSearch,igdbBody,igdbInventory,7ull*60*60*1000);const auto path=igdbResponseCachePath(igdbSearch,igdbBody);
        const auto before=readBinaryFile(path);
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return rawResponse("<html>gateway</html>");};
        json log=json::array();check(postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log)==igdbInventory,"Malformed POST skipped stale-positive fallback");
        check(before==readBinaryFile(path)&&networkVerificationPending(log),"Malformed POST changed old success or erased failure");
    });
    test("network-malformed-igdb-details-stay-pending-without-extending-expired-cache",[&](const auto&){
        seedIgdb(igdbDetails,nullptr,igdbRecord,13ull*60*60*1000);const auto path=igdbResponseCachePath(igdbDetails,nullptr);
        const auto before=readBinaryFile(path);
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return rawResponse("{\"Data\":");};
        json log=json::array();mustFail([&]{getCachedIgdbJson(igdbDetails,"playnite-igdb",&log);});
        check(networkVerificationPending(log)&&before==readBinaryFile(path),"Invalid detail was made permanent or extended old identity authority");
    });
    test("network-steam-semantic-failure-cools-search-only-and-manual-recovery-works",[&](const auto&){
        g_fastMetadataMode=true;int calls=0;
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return rawResponse("<html>offline</html>");};
        json log=json::array();mustFail([&]{steamCandidateSearchJson(searchUrl("A"),&log);});
        mustFail([&]{steamCandidateSearchJson(searchUrl("B"),&log);});
        check(calls==1&&!loadMetadataBackoff(searchUrl("C")).empty(),"Automatic malformed requests hammered a broken search endpoint");
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(record());};
        check(fetchSteamDetails(620,log).canonicalAppId==620&&calls==2,"Search semantic cooldown disabled healthy AppDetails");
        g_fastMetadataMode=false;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(candidates);};
        check(steamCandidateSearchJson(searchUrl("B"),&log)==candidates&&calls==3,"Manual recovery could not probe the recovered endpoint");
    });
    test("network-igdb-semantic-cooldown-suppresses-next-query",[&](const auto&){
        g_fastMetadataMode=true;int calls=0;
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"Error","gateway failure"}});};
        json log=json::array();mustFail([&]{postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log);});
        mustFail([&]{postCachedIgdbJson(igdbSearch,{{"Search","B"}},"playnite-igdb",&log);});
        check(calls==1&&networkVerificationPending(log),"Invalid IGDB envelope was retried for every title");
    });
    for(bool fast:{false,true})for(DWORD status:{DWORD{429},DWORD{503}}){
        const auto label="network-long-retry-after-"+std::to_string(status)+(fast?"-auto":"-manual");
        test(label.c_str(),[&](const auto&){
            g_fastMetadataMode=fast;int calls=0;
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;HttpResponse r;r.status=status;r.retryAfterMs=30000;r.serverRetryAfterMs=120000;return r;};
            json log=json::array();mustFail([&]{getJson(searchUrl("A"),"steam",&log);});
            check(calls==1,"Long server Retry-After was shortened into immediate repeated requests");
            check(log.size()==1&&log[0].value("retryDeferred",false)&&log[0].value("retryAfterMs",DWORD{0})>=120000,"Long Retry-After was not handed back to scheduling");
            check(!loadMetadataBackoff(searchUrl("B")).empty(),"Long provider cooldown was not retained for automatic workers");
        });
    }
    // Provider waits apply to user-triggered requests too. A second worker
    // reloads the cooldown, while a different API remains usable.
    for(bool fast:{false,true})for(const auto& provider:std::vector<std::string>{"steam","playnite-igdb"})
    for(const auto [status,after]:std::vector<std::pair<DWORD,DWORD>>{{429,0},{429,1000},{429,120000},{503,1000},{503,120000}}){
        const auto label="network-metadata-provider-wait-"+provider+"-"+std::to_string(status)+"-"+std::to_string(after)+(fast?"-auto":"-manual");
        test(label.c_str(),[&](const auto&){
            g_fastMetadataMode=fast;int calls=0;
            const auto url=provider=="steam"?searchUrl("A"):igdbSearch;
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){
                ++calls;HttpResponse r;r.status=calls==1?status:200;
                if(calls==1){r.retryAfterMs=(std::min)(after,DWORD{30000});r.serverRetryAfterMs=after;}return r;
            };
            json log=json::array();
            const auto method=provider=="steam"?"GET":"POST";
            const std::vector<unsigned char> body=provider=="steam"?std::vector<unsigned char>{}:std::vector<unsigned char>{'{','}'};
            const auto response=httpRequestWithRetry(provider,method,url,L"",body,MAX_METADATA_BYTES,false,{},&log);
            check(response.status==status&&calls==1,"Metadata provider wait retried inside the worker");
            check(log.size()==1&&log[0].value("retryDeferred",false)&&networkVerificationPending(log),"Metadata provider wait lost its scheduling evidence");
            const auto cooldown=loadMetadataBackoff(url);
            check(!cooldown.empty(),"Provider wait was not persisted");
            const auto minimum=status==429?(std::max)(DWORD{30000},after):after;
            check(cooldown["retryAt"].get<uint64_t>()>=unixTimeMs()+minimum-500,"Provider wait was shortened");
            g_metadataBackoffs.clear();
            const auto nextUrl=provider=="steam"?searchUrl("B"):url;
            mustFail([&]{httpRequestWithRetry(provider,method,nextUrl,L"",body,MAX_METADATA_BYTES,false,{},&log);});
            check(calls==1&&log.back().value("skippedNetwork",false),"Manual or automatic metadata bypassed persisted provider wait");
            // Switching mode must not bypass a server's recovery time either.
            g_fastMetadataMode=!fast;
            mustFail([&]{httpRequestWithRetry(provider,method,nextUrl,L"",body,MAX_METADATA_BYTES,false,{},&log);});
            check(calls==1,"Changing request mode bypassed provider wait");
            httpRequestWithRetry(provider,"GET",provider=="steam"?"https://store.steampowered.com/api/appdetails?appids=620":"https://api2.playnite.link/api/igdb/game/1",L"",{},MAX_METADATA_BYTES,false,{},&log);
            check(calls==2,"Provider wait blocked an unrelated API");
        });
    }
    test("network-metadata-expired-wait-allows-manual-request",[&](const auto&){
        const auto url=searchUrl("A");rememberMetadataBackoff(url,429,1000);
        auto expired=loadMetadataBackoff(url);expired["observedAt"]=unixTimeMs()-2000;expired["retryAt"]=unixTimeMs()-1000;
        writeJsonAtomic(metadataBackoffCachePath(metadataEndpointKey(url)),expired);g_metadataBackoffs.clear();
        int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array()}});};
        json log=json::array();check(steamCandidateSearchJson(url,&log)["items"].empty()&&calls==1,"Expired wait prevented a healthy manual request");
        check(!networkVerificationPending(log),"Expired wait was retained as an active failure");
    });
    for(bool fast:{false,true})for(bool image:{false,true})for(DWORD code:{DWORD{ERROR_WINHTTP_TIMEOUT},DWORD{ERROR_WINHTTP_CANNOT_CONNECT},DWORD{ERROR_WINHTTP_NAME_NOT_RESOLVED},DWORD{ERROR_WINHTTP_CONNECTION_ERROR},DWORD{ERROR_WINHTTP_INCORRECT_HANDLE_STATE}}){
        const auto label="network-terminal-session-"+std::to_string(code)+(image?"-image":"-metadata")+(fast?"-auto":"-manual");
        test(label.c_str(),[&](const auto&){
            resetReusableHttpSession();acquireReusableHttpSession();
            const auto generation=g_reusableHttpSession.generation;
            g_fastMetadataMode=fast;int calls=0;
            ScopedOptionalArtworkBudget budget(true,1000); // One attempt, no real HTTP or retry sleeps.
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&)->HttpResponse{++calls;throw HttpTransportError("fixture final transport failure",code,true);};
            json log=json::array();
            mustFail([&]{httpGet("https://images.igdb.com/failing.png",L"",image?MAX_IMAGE_BYTES:MAX_METADATA_BYTES,image,"playnite-igdb",{},&log);});
            const bool discarded=g_reusableHttpSession.handle.value==nullptr;
            acquireReusableHttpSession();const bool rebuilt=g_reusableHttpSession.generation==generation+1;
            resetReusableHttpSession();
            check(calls==1&&networkVerificationPending(log),"Final transport failure lost its retry evidence");
            check(discarded&&rebuilt,"Final failed transport session leaked into the next request");
        });
    }
    test("network-provider-wait-merge-keeps-manual-transport-recovery",[&](const auto&){
        const auto url=searchUrl("A");rememberMetadataBackoff(url,503,120000,true);
        const auto providerUntil=loadMetadataBackoff(url).at("retryAt").get<uint64_t>();
        rememberMetadataBackoff(url,0,180000);const auto merged=loadMetadataBackoff(url);
        check(merged.at("providerRetryAt").get<uint64_t>()==providerUntil&&merged.at("retryAt").get<uint64_t>()>providerUntil,"Automatic fault changed the provider-directed recovery time");
        int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array()}});};
        json log=json::array();mustFail([&]{getJson(url,"steam",&log);});check(calls==0,"A later transport fault erased the server wait");
        auto recovered=merged;recovered["providerRetryAt"]=unixTimeMs()-1;writeJsonAtomic(metadataBackoffCachePath(metadataEndpointKey(url)),recovered);g_metadataBackoffs.clear();
        check(getJson(url,"steam",&log)["items"].empty()&&calls==1,"Expired provider wait inherited a longer automatic cooldown");
    });
    for(DWORD status:{DWORD{429},DWORD{503}}){
        const auto label="network-legacy-provider-wait-"+std::to_string(status);
        test(label.c_str(),[&](const auto&){
            const auto url=searchUrl("A");rememberMetadataBackoff(url,status,120000);
            auto legacy=loadMetadataBackoff(url);legacy.erase("providerRetryAt");writeJsonAtomic(metadataBackoffCachePath(metadataEndpointKey(url)),legacy);g_metadataBackoffs.clear();
            int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array()}});};
            json log=json::array();mustFail([&]{getJson(url,"steam",&log);});check(calls==0&&networkVerificationPending(log),"Legacy provider wait was bypassed by a manual request");
        });
    }
    for(const auto& invalid:std::vector<json>{"forever",unixTimeMs()+HTTP_MAX_PROVIDER_COOLDOWN_MS+100000}){
        test("network-invalid-provider-wait-cannot-disable-api",[&](const auto&){
            const auto url=searchUrl("A");rememberMetadataBackoff(url,429,30000);
            auto broken=loadMetadataBackoff(url);broken["providerRetryAt"]=invalid;writeJsonAtomic(metadataBackoffCachePath(metadataEndpointKey(url)),broken);g_metadataBackoffs.clear();
            int calls=0;artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload({{"items",json::array()}});};
            json log=json::array();check(getJson(url,"steam",&log)["items"].empty()&&calls==1,"Malformed optional scheduling field permanently disabled manual recovery");
        });
    }
    test("network-success-keeps-reusable-session",[&](const auto&){
        resetReusableHttpSession();acquireReusableHttpSession();const auto generation=g_reusableHttpSession.generation;
        artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&){return payload({{"items",json::array()}});};
        json log=json::array();httpGet(searchUrl("A"),L"",MAX_METADATA_BYTES,false,"steam",{},&log);
        check(g_reusableHttpSession.handle.value!=nullptr&&g_reusableHttpSession.generation==generation,"Healthy request unnecessarily discarded its connection pool");resetReusableHttpSession();
    });
    for(bool fast:{false,true})for(const auto [status,after]:std::vector<std::pair<DWORD,DWORD>>{{429,0},{429,1000},{429,120000},{503,1000},{503,120000}}){
        const auto label="network-image-cooldown-"+std::to_string(status)+"-"+std::to_string(after)+(fast?"-auto":"-manual");
        test(label.c_str(),[&](const auto&){
            g_fastMetadataMode=fast;int calls=0;
            const std::string url="https://images.igdb.com/igdb/image/upload/t_original/limited.png";
            artworkTestTransport=[&](const auto&,const auto&,const auto& request,const auto&){
                ++calls;HttpResponse r;r.status=request.find("limited.png")!=std::string::npos?status:200;
                if(r.status!=200){r.retryAfterMs=(std::min)(after,DWORD{30000});r.serverRetryAfterMs=after;}return r;
            };
            json log=json::array();const auto response=httpGet(url,L"",MAX_IMAGE_BYTES,true,"playnite-igdb",{},&log);
            check(response.status==status&&calls==1,"Rate-limited image consumed repeated requests inside one worker");
            check(networkVerificationPending(log)&&log[0].value("retryDeferred",false),"Image Retry-After lost its deferred failure evidence");
            const auto cooldown=loadMetadataBackoff(url);
            check(!cooldown.empty()&&cooldown["retryAt"].get<uint64_t>()>=unixTimeMs()+(after>30000?after-1000:0),"Image cooldown lost the provider recovery time");
            g_metadataBackoffs.clear(); // The next worker must honor the persisted record too.
            mustFail([&]{httpGet(url+"?cache=2",L"",MAX_IMAGE_BYTES,true,"playnite-igdb",{},&log);});
            check(calls==1&&log.back().value("skippedNetwork",false),"Another image request ignored the disk cooldown");
            httpGet("https://images.igdb.com/igdb/image/upload/t_original/healthy.png",L"",MAX_IMAGE_BYTES,true,"playnite-igdb",{},&log);
            check(calls==2,"A single image cooldown blocked unrelated artwork");
        });
    }
    test("network-image-definitive-404-is-not-network-outage",[&](const auto&){
        int calls=0;const std::string url="https://images.igdb.com/missing.png";
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return HttpResponse{404};};
        json log=json::array();for(int i=0;i<2;++i)httpGet(url,L"",MAX_IMAGE_BYTES,true,"playnite-igdb",{},&log);
        check(calls==2&&loadMetadataBackoff(url).empty()&&!networkVerificationPending(log),"Missing artwork was promoted to an endless network retry");
    });
    test("network-healthy-empty-search-is-still-definitive",[&](const auto&){
        g_fastMetadataMode=true;
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload({{"items",json::array()}});};
        json log=json::array();check(steamCandidateSearchJson(searchUrl("None"),&log)["items"].empty(),"Valid empty Steam result was rejected");
        check(!networkVerificationPending(log)&&loadMetadataBackoff(searchUrl("None")).empty(),"Valid empty search entered endless network retry");
    });



    const ArtworkSpec networkCover{"tall","p",600,900,{}};
    const ArtworkSpec networkHero{"hero","_hero",1920,620,{}};
    for(const auto& provider:std::vector<std::string>{"playnite-igdb","baidu-image"}){
        const auto label="network-discovery-timeout-"+provider;
        test(label.c_str(),[&](const auto&){
            artworkTestTransport=[](const auto&,const auto&,const auto&,const auto&)->HttpResponse{throw HttpTransportError("fixture timeout",ERROR_WINHTTP_TIMEOUT,true);};
            std::vector<ArtworkCandidate> images;json failures=json::array();
            const auto attempt=appendAutomaticArtworkFallback("Test Game",networkCover,provider,images,failures);
            check(images.empty()&&!attempt["ok"].get<bool>()&&networkVerificationPending(attempt),"Discovery exception discarded raw failure receipts");
            check(!failures.empty()&&failures[0]["stage"]=="artwork-discovery","Discovery timeout never reached the artwork retry queue");
        });
        const auto malformed="network-discovery-malformed-"+provider;
        test(malformed.c_str(),[&](const auto&){
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return rawResponse("<html>gateway failed</html>");};
            std::vector<ArtworkCandidate> images;json failures=json::array();
            const auto attempt=appendAutomaticArtworkFallback("Test Game",networkCover,provider,images,failures);
            check(images.empty()&&networkVerificationPending(attempt)&&!failures.empty(),"Malformed discovery became permanent missing artwork");
        });
        const auto empty="network-discovery-healthy-empty-"+provider;
        test(empty.c_str(),[&](const auto&){
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(provider=="playnite-igdb"?json{{"Data",json::array()}}:json{{"data",json::array()}});};
            std::vector<ArtworkCandidate> images;json failures=json::array();
            const auto attempt=appendAutomaticArtworkFallback("Test Game",networkCover,provider,images,failures);
            check(images.empty()&&failures.empty()&&!networkVerificationPending(attempt),"Healthy empty discovery was mislabeled as a network outage");
        });
    }
    test("network-discovery-partial-igdb-detail-failure-reaches-retry-queue",[&](const auto&){
        artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&)->HttpResponse{
            if(url==igdbSearch)return payload(igdbInventory);
            throw HttpTransportError("detail timeout",ERROR_WINHTTP_TIMEOUT,true);
        };
        std::vector<ArtworkCandidate> images;json failures=json::array();
        const auto attempt=appendAutomaticArtworkFallback("Test Game",networkHero,"playnite-igdb",images,failures);
        check(images.empty()&&networkVerificationPending(attempt)&&!failures.empty(),"Successful search hid failed background-detail lookup");
    });
    test("network-discovery-baidu-invalid-envelope-reaches-retry-queue",[&](const auto&){
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload({{"error","service unavailable"}});};
        std::vector<ArtworkCandidate> images;json failures=json::array();
        appendAutomaticArtworkFallback("Test Game",networkCover,"baidu-image",images,failures);
        check(images.empty()&&!failures.empty(),"Baidu error envelope became definitive no-artwork");
    });
    test("network-discovery-japanese-title-can-use-baidu-without-steam-id",[&](const auto&){
        const std::string title="維新の嵐 幕末志士伝";
        artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload({{"data",json::array({{{"objURL","https://example.com/cover.jpg"},{"thumbURL","https://example.com/preview.jpg"},{"width",600},{"height",900},{"fromPageTitle",title}}})}});};
        std::vector<ArtworkCandidate> images;json failures=json::array();
        const auto attempt=appendAutomaticArtworkFallback(title,networkCover,"baidu-image",images,failures);
        check(images.size()==1&&images[0].provider=="baidu-image"&&attempt["ok"]==true&&failures.empty(),"Healthy Japanese artwork still required a Steam ID");
    });
    test("network-discovery-igdb-failure-does-not-block-healthy-baidu-candidate",[&](const auto&){
        artworkTestTransport=[&](const auto& provider,const auto&,const auto&,const auto&)->HttpResponse{
            if(provider=="playnite-igdb")throw HttpTransportError("IGDB timeout",ERROR_WINHTTP_TIMEOUT,true);
            return payload({{"data",json::array({{{"objURL","https://example.com/cover.jpg"},{"width",600},{"height",900},{"fromPageTitle","Test Game"}}})}});
        };
        std::vector<ArtworkCandidate> images;json failures=json::array();
        appendAutomaticArtworkFallback("Test Game",networkCover,"playnite-igdb",images,failures);
        const auto attempt=appendAutomaticArtworkFallback("Test Game",networkCover,"baidu-image",images,failures);
        check(!failures.empty()&&images.size()==1&&attempt["ok"]==true,"First provider failure prevented automatic fallback progress");
    });
    test("network-discovery-mismatched-igdb-details-stay-nonretryable",[&](const auto&){
        artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){return payload(url==igdbSearch?igdbInventory:json{{"Data",{{"id",8},{"name","Wrong Game"}}}});};
        std::vector<ArtworkCandidate> images;json failures=json::array();
        const auto attempt=appendAutomaticArtworkFallback("Test Game",networkHero,"playnite-igdb",images,failures);
        check(images.empty()&&failures.empty()&&!networkVerificationPending(attempt),"Mismatched detail identity became a retryable network failure");
    });

    wchar_t networkCaptured[32768]{};
    if(GetEnvironmentVariableW(L"YMCC_LIBRARY_NETWORK_CASES",networkCaptured,32768)>0){
        const auto capture=fs::absolute(fs::path(networkCaptured));
        check(canonicalPathKey(capture).starts_with(canonicalPathKey(build)+"\\"),"Network captures must stay in Build");
        test("network-captured-steam-search-and-details-bind-requested-app",[&](const auto&){
            const auto inventory=loadJsonDocument(capture/L"storesearch.body"),details=loadJsonDocument(capture/L"appdetails.body");
            artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){return payload(url.find("storesearch")!=std::string::npos?inventory:details);};
            json log=json::array();check(steamCandidateSearchJson(searchUrl("Portal%202"),&log)==inventory,"Actual StoreSearch envelope rejected");
            check(fetchSteamDetails(620,log).canonicalAppId==620,"Actual AppDetails failed strict binding");
        });
        test("network-captured-igdb-lowercase-data-survives-cache-roundtrip",[&](const auto&){
            const auto inventory=loadJsonDocument(capture/L"igdb-search-production.body");int calls=0;
            artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(inventory);};
            json log=json::array();check(postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log)==inventory,"Actual lower-case IGDB envelope rejected");
            check(postCachedIgdbJson(igdbSearch,igdbBody,"playnite-igdb",&log)==inventory&&calls==1,"Actual IGDB response did not reuse successful cache");
        });
    }

    wchar_t captured[32768]{};
    if(GetEnvironmentVariableW(L"YMCC_STEAM_DATA_CASES",captured,32768)>0){
        const auto source=fs::absolute(fs::path(captured));check(canonicalPathKey(source).starts_with(canonicalPathKey(build)+"\\"),"Steam capture must stay in Build");
        test("round6-real-steam-search-and-details-select-the-base-game",[&](const auto&){const auto inventory=loadJsonDocument(source/L"portal2-search.json"),detail=loadJsonDocument(source/L"portal2.json");artworkTestTransport=[&](const auto&,const auto&,const auto& url,const auto&){return payload(url.find("storesearch")!=std::string::npos?inventory:detail);};json log=json::array();const auto r=resolveSteamGame({{1,"manual-name","Portal 2",{}}},{"Portal 2"},log);check(r&&r->appId==620&&r->steamVerificationStatus=="steam-verified","Actual base-game response failed identity arbitration");});
        test("round6-real-steam-soundtrack-cannot-be-a-base-game",[&](const auto&){const auto detail=loadJsonDocument(source/L"portal2-soundtrack.json");artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){return payload(detail);};json log=json::array();mustFail([&]{verifySteamAppId(323180,log);});});
        test("round6-real-steam-redistributable-stays-excluded-before-network",[&](const auto&){int calls=0;const auto detail=loadJsonDocument(source/L"steam-common-redist.json");artworkTestTransport=[&](const auto&,const auto&,const auto&,const auto&){++calls;return payload(detail);};json log=json::array();mustFail([&]{fetchSteamDetails(228980,log);});check(calls==0,"Real system tool entered metadata queue");});
    }
    size_t failures=0;for(const auto& t:cases)if(!t["passed"].get<bool>())++failures;json report={{"allPassed",failures==0},{"caseCount",cases.size()},{"failedCount",failures},{"cases",cases},{"realSteamFilesModified",false},{"liveNetworkUsedByFixtures",false}};selftestMoveFault={};writeJsonAtomic(root/L"summary.json",report);std::cout<<report.dump(2)<<"\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
