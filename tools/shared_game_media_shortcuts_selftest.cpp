#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_download_worker.h"
#include <iostream>
using namespace ymcc::media;
using namespace ymcc::media::downloads;
static int checks=0;
static void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
static Json verified(int id){return {{"appId",id},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"}};}
static void shortcuts(const fs::path& file,const Json& rows){std::string bytes;auto text=[&](const std::string& s){bytes+=s;bytes+=char(0);};bytes+=char(0);text("shortcuts");int n=0;for(const auto& row:rows){bytes+=char(0);text(std::to_string(n++));bytes+=char(2);text("appid");auto id=row["appid"].get<uint32_t>();for(int i=0;i<4;i++)bytes+=char(id>>(8*i));for(const auto* field:{"appname","exe"}){bytes+=char(1);text(field);text(row[field]);}bytes+=char(8);}bytes+=char(8);bytes+=char(8);fs::create_directories(file.parent_path());std::ofstream(file,std::ios::binary).write(bytes.data(),bytes.size());}
static Json row(const fs::path& exe,int n){return {{"appId",0},{"executable",utf8(exe.wstring())},{"name","candidate only"},{"steamAccount","17"},{"shortcutAppId",std::to_string(0x90000000u+n)},{"catalogSource","steam-shortcut"},{"sourceTier",2},{"verified",false}};}
int wmain(int argc,wchar_t** argv){try{
 if(argc==4&&std::wstring_view(argv[2])==L"--steam-comprehensive-identity-only"){
  const auto exe=fs::path(argv[1]),output=fs::path(argv[3]);const auto stem=utf8(exe.stem().wstring());
  fs::create_directories(output);std::ofstream(output.parent_path()/L"helpers.jsonl",std::ios::app)<<Json({{"pid",GetCurrentProcessId()},{"mode",utf8(argv[2])},{"exe",utf8(exe.wstring())}}).dump()<<"\n";
  if(stem=="hang")Sleep(30000);
  const auto manifest=output/L"artwork"/L"fixture"/L"manifest.json";
  atomicWrite(manifest,{{"exe",stem=="mismatch"?"C:/different.exe":utf8(exe.wstring())},{"match",stem=="unconfirmed"?Json::object():verified(stem.starts_with("fixture-")?std::stoi(stem.substr(8)):777)}});return 0;
 }
 if(argc!=2)throw std::runtime_error("fresh fixture root required");const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("fixture root exists");fs::create_directories(root);
 const auto bin=root/L"bin";fs::create_directories(bin);fs::copy_file(fs::absolute(argv[0]),bin/L"SteamArtworkLab.exe");
 const auto data=root/L"data",vdf=root/L"shortcuts.vdf";Json raw=Json::array(),scan=Json::array();
 for(int n=0;n<5;n++){const auto exe=root/(L"fixture-"+std::to_wstring(100+n)+L".exe");std::ofstream(exe)<<"fixture, never executed";raw.push_back({{"appid",0x90000000u+n},{"appname","candidate "+std::to_string(n)},{"exe",utf8(exe.wstring())}});if(n<4)scan.push_back({{"primaryExecutable",utf8(exe.wstring())},{"directoryName","local duplicate"}});}
 shortcuts(vdf,raw);atomicWrite(data/L"state"/L"library-scan.json",{{"games",scan}});Store store(data);Queue q(data);
 auto inventory=store.inventory(shortcutInventoryForAccount(vdf,17,[]{return 17u;}));check(inventory.size()==5,"shortcut-only and overlapping library files join exactly once");
 for(const auto& target:inventory){check(stringField(target,"catalogSource")=="steam-shortcut","dedupe preserves added-Steam priority");check(stringField(target,"steamAccount")=="17","stable account evidence survives inventory");check(!target.value("verified",true)&&target.value("appId",-1)==0,"display names/shortcut IDs never verify store identity");}
 q.configure(true,"");q.enqueue(inventory);check(q.document()["jobs"].size()==5,"unknown added shortcuts enter production background queue");check(q.snapshot()["sourceCounts"]["identifying"]==5,"status exposes identities awaiting resolver");
 const auto before=q.snapshot().value("statusRevision",uint64_t{0});for(int n=0;n<30;n++)q.enqueue(inventory);check(q.document()["jobs"].size()==5,"repeat inventory cannot duplicate pending files");check(q.snapshot().value("statusRevision",uint64_t{0})==before,"repeat inventory causes no redundant durable write");
 auto spoof=inventory[0];spoof["appId"]=0x90000000u;spoof["verified"]=true;q.enqueue(Json::array({spoof}));check(q.document()["jobs"].size()==5,"spoofed shortcut AppID is replaced by pending exact-file identity");
 auto plain=inventory[0];plain["catalogSource"]="ymcc-confirmed";plain["executable"]=utf8((root/L"not-confirmed.exe").wstring());q.enqueue(Json::array({plain}));check(q.document()["jobs"].size()==5,"unconfirmed generic scan cannot bypass shortcut admission");
 Json order={{"popular",{{"sourceTier",0},{"chartRank",1}}},{"native",{{"sourceTier",1}}},{"shortcut",{{"sourceTier",2},{"catalogSource","steam-shortcut"}}},{"confirmed",{{"sourceTier",2}}},{"other",{{"sourceTier",3}}}};
 for(const auto* key:{"popular","native","shortcut","confirmed","other"}){check(nextKey(order,now())==key,"priority: popular, native, added shortcuts, confirmed, remembered");order.erase(key);}
 const auto token=q.start();auto claimed=q.claim(token);check(!claimed.is_null()&&claimed.value("identityPending",false),"production claim selects a pending file");check(q.claim(token).is_null(),"at most one identity helper can be active");q.end(token,"deferred");
 // Complete/manual slots let the real worker exercise identification, promotion,
 // de-duplication and exit without accessing any external metadata or media.
 for(int id=100;id<105;id++){auto e=store.edit({{"appId",id},{"bindingRevision",1},{"slot","static"},{"action","delete"}});store.edit({{"key",e["key"]},{"bindingRevision",e["bindingRevision"]},{"slot","dynamic"},{"action","delete"}});}
 IdentityPorts ports;ports.appRoot=bin;ports.current=[&](const Json& t){const auto found=shortcutIdentity(vdf,stringField(t,"shortcutAppId"));return stringField(t,"steamAccount")=="17"&&!found.empty()&&normalized(stringField(found,"executable"))==normalized(stringField(t,"executable"));};
 const auto runToken=q.start();check(!runToken.empty(),"identity queue starts despite no initial store AppIDs");check(Worker(data,runToken,GetCurrentProcess(),{},ports).run()==0,"real worker completes file identification without renderer help");
 check(q.document()["jobs"].empty(),"identity placeholders retire after protected cached output is reused");check(q.snapshot().value("workerPid",DWORD{1})==0&&q.snapshot().value("remaining",size_t{1})==0,"all identities complete and worker exits");
 for(const auto& target:inventory){const auto id=store.identity(stringField(target,"executable"));check(id.value("verified",false),"each exact-file resolver result is remembered for later media admission");check(store.resolve({{"executable",target["executable"]}}).value("key",std::string{})=="steam-"+std::to_string(id.value("appId",0)),"unknown selected file immediately reuses an existing cached canonical entry");}
 const auto count=read(data/L"config"/L"game-media.json")["entries"].size();check(count==5,"manual slots remain unchanged and no guessed media binding appears");
 auto failing=[&](const char* name,const char* expected,IdentityPorts use){const auto exe=root/(wide(name)+L".exe");std::ofstream(exe)<<"not executable";auto target=row(exe,8);const auto fresh=root/wide(name);bool rejected=false;try{identifyShortcut(fresh,target,use,[]{});}catch(const std::exception& e){rejected=std::string(e.what()).find(expected)!=std::string::npos;}check(rejected,expected);check(Store(fresh).identity(utf8(exe.wstring())).empty(),"failed/changed identity never binds the file");check(!fs::exists(fresh/L"cache"/L"jobs"/L"game-media-identity")||std::all_of(fs::directory_iterator(fresh/L"cache"/L"jobs"/L"game-media-identity"),fs::directory_iterator{},[](const auto& p){return !p.is_directory();}),"temporary resolver output is cleaned");};
 ports.current=[](const Json&){return true;};failing("mismatch","target-mismatch",ports);failing("unconfirmed","identity-unconfirmed",ports);
 auto timeout=ports;timeout.timeoutMs=150;auto started=GetTickCount64();failing("hang","identity-timeout",timeout);check(GetTickCount64()-started<4000,"hung resolver is killed within bounded timeout");
 auto stale=ports;stale.current=[](const Json&){return false;};failing("stale-account","no-longer-current",stale);
 auto changing=ports;int calls=0;changing.current=[&](const Json&){return ++calls<3;};failing("changed-after-output","no-longer-current",changing);
 auto cancel=root/L"cancel.exe";std::ofstream(cancel)<<"fixture";int beats=0;bool cancelled=false;try{identifyShortcut(root/L"cancel",row(cancel,9),ports,[&]{if(++beats>1)throw std::runtime_error("media-download-cancelled");});}catch(const std::exception&e){cancelled=std::string(e.what())=="media-download-cancelled";}check(cancelled,"policy cancellation propagates before identity publication");
 // A verified exact file now becomes one canonical store task. The old
 // popular/native priority survives aliases and later generic confirmations.
 const auto queueRoot=root/L"promotion";Store promoted(queueRoot);auto target=row(root/L"fixture-100.exe",0);const auto manifest=root/L"verified.json";atomicWrite(manifest,{{"exe",target["executable"]},{"match",verified(100)}});promoted.rememberIdentity(target["executable"],manifest,now());Queue pq(queueRoot);pq.configure(true,"");pq.enqueue(Json::array({{{"appId",100},{"verified",true},{"sourceTier",2},{"catalogSource","ymcc-confirmed"}},target}));check(pq.document()["jobs"].size()==1&&stringField(pq.document()["jobs"]["steam-100"],"catalogSource")=="steam-shortcut","verified alias upgrades confirmed task to added-Steam priority exactly once");pq.enqueueCatalog(Json::array({{{"appId",100},{"verified",true},{"sourceTier",0},{"catalogSource","popular"},{"chartRank",1}}}));pq.enqueueCatalog(Json::array());check(stringField(pq.document()["jobs"]["steam-100"],"catalogSource")=="steam-shortcut","chart refresh restores remembered shortcut priority");
 std::cout<<Json({{"ok",true},{"checks",checks},{"scope","production inventory/Queue/Worker/real identity subprocess; synthetic exact-file resolver results, no live cache/Steam writes or network"}}).dump()<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
