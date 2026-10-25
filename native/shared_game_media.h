#pragma once
// One durable media owner shared by the main renderer and library editor.
// All writes use a cross-process mutex and replace one versioned document.
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <functional>
#include <chrono>
#include <cmath>
#include <thread>
#include <mutex>
#include <sstream>
#include <set>
#include <map>
#include <stdexcept>
#include <string>
#include "json.hpp"
#include "custom-steam-library/custom_steam_library_paths.h"
#include "shared_game_media_capacity.h"
#include "custom-steam-library/custom_steam_library_identity_cache.h"
#pragma comment(lib, "bcrypt.lib")
namespace ymcc::media {
using Json = nlohmann::json;
inline constexpr int videoQualityPolicy=3;
inline int preferredVideoHeight(const Json& entry){
    const auto selected=entry.value("selectedVideo",Json::object());
    const auto height=selected.value("preferredHeight",720);
    return height==480||height==720||height==1080?height:720;
}
inline bool needsVideoQualityUpgrade(const Json& e,int64_t time){
    return e.value("dynamicReady",false)&&!e.value("deleted",false)&&
        !e.value("manualSlots",Json::object()).value("dynamic",false)&&!e.value("deletedSlots",Json::object()).value("dynamic",false)&&
        e.value("videoUnavailableUntil",int64_t{0})<=time&&
        e.value("assets",Json::object()).value("dynamic",Json::object()).value("qualityPolicy",0)<videoQualityPolicy&&
        (e.value("assets",Json::object()).value("dynamic",Json::object()).value("selectedHeight",0)<=0||
         e.value("assets",Json::object()).value("dynamic",Json::object()).value("selectedHeight",0)>preferredVideoHeight(e));
}
inline std::string stringField(const Json& object,const char* key,const std::string& fallback={}){if(!object.is_object())return fallback;const auto item=object.find(key);return item!=object.end()&&item->is_string()?item->get<std::string>():fallback;}
namespace fs = std::filesystem;
inline int64_t now() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
inline std::wstring wide(const std::string& v) { if(v.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,v.data(),static_cast<int>(v.size()),nullptr,0);if(n<=0)throw std::runtime_error("media-invalid-utf8");std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,v.data(),static_cast<int>(v.size()),r.data(),n);return r; }
inline std::string utf8(const std::wstring& v) { if(v.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,v.data(),static_cast<int>(v.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,v.data(),static_cast<int>(v.size()),r.data(),n,nullptr,nullptr);return r; }
inline std::string normalized(const std::string& v) { auto p=wide(v);std::replace(p.begin(),p.end(),L'/',L'\\');for(auto& c:p)c=static_cast<wchar_t>(towlower(c));return utf8(p); }
inline std::string digestKey(const std::string& s) { uint64_t n=1469598103934665603ull;for(unsigned char c:s){n^=c;n*=1099511628211ull;}std::ostringstream o;o<<std::hex<<n;return o.str(); }
inline bool safeKey(const std::string& s) { return !s.empty()&&s.size()<=100&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';}); }
inline Json read(const fs::path& p) { std::error_code ec;if(!fs::exists(p,ec))return Json::object();auto size=fs::file_size(p,ec);if(ec||size>16*1024*1024)throw std::runtime_error("media-index-unreadable");std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("media-index-unreadable");Json j;in>>j;if(!j.is_object())throw std::runtime_error("media-index-invalid");return j; }
struct Lock { HANDLE value=nullptr;explicit Lock(const fs::path& root){auto name=L"Local\\YMCC-SharedMedia-"+wide(digestKey(normalized(utf8(fs::absolute(root).wstring()))));value=CreateMutexW(nullptr,FALSE,name.c_str());if(!value)throw std::runtime_error("media-lock-unavailable");auto result=WaitForSingleObject(value,10000);if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED){CloseHandle(value);value=nullptr;throw std::runtime_error("media-owner-busy");}}~Lock(){if(value){ReleaseMutex(value);CloseHandle(value);}}Lock(const Lock&)=delete; };
inline void atomicWrite(const fs::path& p,const Json& j) { fs::create_directories(p.parent_path());auto tmp=p;tmp+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());{std::ofstream o(tmp,std::ios::binary|std::ios::trunc);o<<j.dump(2);o.flush();if(!o)throw std::runtime_error("media-write-failed");}if(!MoveFileExW(tmp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code e;fs::remove(tmp,e);throw std::runtime_error("media-publish-failed");} }
inline std::string randomId() { unsigned char b[16];if(BCryptGenRandom(nullptr,b,sizeof b,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("media-entropy-unavailable");std::string r;for(auto c:b){r+="0123456789abcdef"[c>>4];r+="0123456789abcdef"[c&15];}return r; }
inline std::string mediaKind(const fs::path& p) { std::ifstream in(p,std::ios::binary);unsigned char b[16]{};in.read(reinterpret_cast<char*>(b),sizeof b);auto n=in.gcount();if(n>=8&&b[4]=='f'&&b[5]=='t'&&b[6]=='y'&&b[7]=='p')return "mp4";if(n>=4&&b[0]==0x1a&&b[1]==0x45&&b[2]==0xdf&&b[3]==0xa3)return "webm";if(n>=8&&b[0]==0x89&&b[1]=='P'&&b[2]=='N'&&b[3]=='G')return "png";if(n>=3&&b[0]==0xff&&b[1]==0xd8&&b[2]==0xff)return "jpg";if(n>=12&&b[0]=='R'&&b[1]=='I'&&b[2]=='F'&&b[3]=='F'&&b[8]=='W'&&b[9]=='E'&&b[10]=='B'&&b[11]=='P')return "webp";if(n>=2&&b[0]=='B'&&b[1]=='M')return "bmp";if(n>=4&&b[0]==0&&b[1]==0&&b[2]==1&&b[3]==0)return "ico";throw std::runtime_error("media-unsupported-container"); }
inline std::string processBirthToken(DWORD pid){HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return {};FILETIME created{},exit{},kernel{},user{};const bool ok=GetProcessTimes(process,&created,&exit,&kernel,&user)!=FALSE;CloseHandle(process);if(!ok)return {};ULARGE_INTEGER value{};value.LowPart=created.dwLowDateTime;value.HighPart=created.dwHighDateTime;return std::to_string(value.QuadPart);}
inline bool leaseOwnerGone(const Json& entry){const auto pid=entry.value("leasePid",DWORD{0});if(!pid)return false;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)return GetLastError()==ERROR_INVALID_PARAMETER;const bool exited=WaitForSingleObject(process,0)==WAIT_OBJECT_0;CloseHandle(process);if(exited)return true;const auto birth=processBirthToken(pid);return !birth.empty()&&!entry.value("leaseBirth",std::string{}).empty()&&birth!=entry.value("leaseBirth",std::string{});}
inline Json identityWorkerArguments(const std::string& executable,const fs::path& output){
    if(executable.empty()||!output.is_absolute())throw std::runtime_error("media-identity-arguments-invalid");
    return Json::array({executable,"--steam-identity-only",utf8(output.wstring())});
}
class Store {
    fs::path root_;
    fs::path index()const{return root_/L"config"/L"game-media.json";}
    static std::string keyFor(const Json& a){
        const auto key=a.value("key",std::string{});
        if(!key.empty()){if(!safeKey(key))throw std::runtime_error("media-invalid-key");return key;}
        const auto id=a.value("appId",int64_t{0});
        if(id>0&&id<=2147483647)return "steam-"+std::to_string(id);
        const auto exe=a.value("executable",std::string{});
        if(!exe.empty())return "exe-"+digestKey(normalized(exe));
        throw std::runtime_error("media-identity-required");
    }
    static Json normalizeEntry(Json e){
        if(!e.contains("assets"))e["assets"]=Json::object();
        if(e.contains("asset")&&e["asset"].is_object()&&e["assets"].empty()){
            const auto slot=e["asset"].value("kind",std::string{})=="video"?"dynamic":"static";
            e["assets"][slot]=e["asset"];
            if(e.value("manual",false))e["manualSlots"][slot]=true;
        }
        if(!e.contains("manualSlots"))e["manualSlots"]=Json::object();
        if(!e.contains("deletedSlots"))e["deletedSlots"]=Json::object();
        const Json defaultPlayback={{"start",8},{"end",38},{"fit","cover"}};
        if(!e.contains("playback"))e["playback"]=defaultPlayback;
        if(!e.contains("playbackExplicit"))e["playbackExplicit"]=e["playback"]!=defaultPlayback;
        // Older editor builds marked a trim as manual video ownership, even
        // before a video existed. Retain that trim but allow initial collection.
        if(e.value("playbackExplicit",false)&&!e["assets"].contains("dynamic")&&!e["deletedSlots"].value("dynamic",false))e["manualSlots"].erase("dynamic");
        if(!e.contains("invalidSlots"))e["invalidSlots"]=Json::object();
        const auto usable=[&](const std::string& slot){return e["assets"].contains(slot)&&!e["deletedSlots"].value(slot,false)&&e["invalidSlots"].value(slot,std::string{})!=e["assets"][slot].value("id",std::string{});};
        const auto mode=e.value("modeExplicit",false)?e.value("wallpaperMode",std::string{"static"}):(e["assets"].contains("dynamic")?std::string{"dynamic"}:std::string{"static"});e["wallpaperMode"]=mode=="static"?"static":"dynamic";
        const auto chosen=e["wallpaperMode"].get<std::string>();
        if(usable(chosen))e["asset"]=e["assets"][chosen];
        else if(chosen=="dynamic"&&usable("static"))e["asset"]=e["assets"]["static"];
        else e.erase("asset");
        e["ready"]=e.contains("asset")&&!e.value("deleted",false);
        e["staticReady"]=usable("static");
        e["dynamicReady"]=usable("dynamic");
        e["manual"]=e["manualSlots"].value(chosen,false);
        return e;
    }
    static void releasePreparation(Json& e){e["taskId"]="";e["leaseUntil"]=0;e["taskSlots"]=Json::array();}
    static std::string canonicalKey(const Json& j,const Json& args){auto key=keyFor(args);for(int n=0;n<4;n++){if(!j.contains("aliases")||!j["aliases"].contains(key))return key;const auto next=j["aliases"][key].get<std::string>();if(!safeKey(next)||next==key)throw std::runtime_error("media-alias-invalid");key=next;}throw std::runtime_error("media-alias-cycle");}
    Json document()const{
        auto j=read(index());if(!j.contains("entries"))j["entries"]=Json::object();
        if(!j["entries"].is_object()||j.value("schemaVersion",1)!=1)throw std::runtime_error("media-schema-unsupported");
        for(auto& e:j["entries"])e=normalizeEntry(e);
        return j;
    }
    void save(Json& j){
        j.erase("_mediaChanged");j["schemaVersion"]=1;j["revision"]=j.value("revision",uint64_t{0})+1;j["updatedAt"]=now();atomicWrite(index(),j);
        for(const auto* role:{L"main",L"library"}){
            const auto name=L"Local\\YMCC-MediaChanged-"+wide(digestKey(normalized(utf8(fs::absolute(root_).wstring()))))+L"-"+role;
            HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(event){SetEvent(event);CloseHandle(event);}
        }
    }
    Json seed(Json& j,const Json& a){
        const auto key=canonicalKey(j,a);
        const auto exe=normalized(a.value("executable",std::string{}));const auto appId=a.value("appId",int64_t{0});
        if(appId>0&&!exe.empty()&&a.value("verified",false))for(auto it=j["entries"].begin();it!=j["entries"].end();){
            if(it.key()==key||it.value().value("appId",int64_t{0})>0||normalized(it.value().value("executable",std::string{}))!=exe){++it;continue;}
            const auto oldKey=it.key();auto old=normalizeEntry(it.value());
            if(!j["entries"].contains(key)){old["key"]=key;old["appId"]=appId;old["bindingRevision"]=old.value("bindingRevision",uint64_t{0})+1;j["entries"][key]=old;}
            else {
                auto current=normalizeEntry(j["entries"][key]);bool bindingChanged=false;
                for(const auto* slot:{"static","dynamic"}){
                    if(old["assets"].contains(slot)&&(!current["assets"].contains(slot)||old["manualSlots"].value(slot,false)&&!current["manualSlots"].value(slot,false))){
                        current["assets"][slot]=old["assets"][slot];current["manualSlots"][slot]=old["manualSlots"].value(slot,false);
                        if(old["invalidSlots"].contains(slot))current["invalidSlots"][slot]=old["invalidSlots"][slot];else current["invalidSlots"].erase(slot);
                        bindingChanged=true;
                    }
                    if(old["deletedSlots"].value(slot,false)&&!current["manualSlots"].value(slot,false)&&!current["deletedSlots"].value(slot,false)){current["deletedSlots"][slot]=true;bindingChanged=true;}
                }
                if(old.value("modeExplicit",false)&&!current.value("modeExplicit",false)){current["wallpaperMode"]=old["wallpaperMode"];current["modeExplicit"]=true;bindingChanged=true;}
                if(old.value("playbackExplicit",false)&&!current.value("playbackExplicit",false)){current["playback"]=old["playback"];current["playbackExplicit"]=true;bindingChanged=true;}
                // Metadata-only aliases preserve a download already owned by another
                // request. Real edits supersede it, without leaving a stale 10m lease.
                if(bindingChanged){
                    current["bindingRevision"]=std::max(current.value("bindingRevision",uint64_t{0}),old.value("bindingRevision",uint64_t{0}))+1;
                    releasePreparation(current);current["retryAt"]=0;
                    current=normalizeEntry(current);current["status"]=current.value("ready",false)?"ready":"missing";
                }
                j["entries"][key]=normalizeEntry(current);
            }
            j["aliases"][oldKey]=key;j["_mediaChanged"]=true;it=j["entries"].erase(it);
        }
        if(!j["entries"].contains(key))j["entries"][key]={{"key",key},{"appId",a.value("appId",int64_t{0})},{"name",a.value("name",std::string{})},{"executable",a.value("executable",std::string{})},{"bucket","unclassified"},{"bindingRevision",1},{"status","missing"},{"manual",false},{"deleted",false},{"updatedAt",now()},{"assets",Json::object()},{"wallpaperMode","static"},{"modeExplicit",false}};
        if(appId>0&&!exe.empty()&&a.value("verified",false)){
            const auto fileKey=keyFor(Json{{"executable",a.value("executable",std::string{})}});
            if(j.contains("aliases")&&j["aliases"].contains(fileKey)&&canonicalKey(j,Json{{"executable",a.value("executable",std::string{})}})!=key)throw std::runtime_error("media-identity-binding-conflict");
            if(fileKey!=key&&(!j.contains("aliases")||!j["aliases"].contains(fileKey))){j["aliases"][fileKey]=key;j["_mediaChanged"]=true;}
        }
        auto e=normalizeEntry(j["entries"][key]);
        if(a.value("verified",false)&&appId>0&&!e.value("identityVerified",false)){e["identityVerified"]=true;j["_mediaChanged"]=true;}
        for(const auto* field:{"executable","gameDirectory"})if(e.value(field,std::string{}).empty()&&!a.value(field,std::string{}).empty()){e[field]=a.value(field,std::string{});j["_mediaChanged"]=true;}
        return e;
    }
    static std::string canonicalDirectoryId(const fs::path& path){
        std::error_code ec;auto full=fs::weakly_canonical(path,ec);if(ec)full=fs::absolute(path);
        auto text=utf8(full.wstring());std::replace(text.begin(),text.end(),'/','\\');
        for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        BCRYPT_ALG_HANDLE algorithm=nullptr;unsigned char bytes[32]{};
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("media-hash-unavailable");
        const auto result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(text.data()),static_cast<ULONG>(text.size()),bytes,sizeof bytes);
        BCryptCloseAlgorithmProvider(algorithm,0);if(result<0)throw std::runtime_error("media-hash-failed");
        std::string id;for(auto b:bytes){id+="0123456789abcdef"[b>>4];id+="0123456789abcdef"[b&15];}return id;
    }
    Json reference(const fs::path& path)const{
        std::error_code ec;auto relative=fs::relative(path,root_,ec);if(ec||relative.empty()||relative.is_absolute())return Json::object();
        for(const auto& part:relative)if(part==L"..")return Json::object();
        if(*relative.begin()!=L"artwork")return Json::object();
        auto bytes=fs::file_size(path,ec);if(ec||!bytes||bytes>30ull*1024*1024)return Json::object();
        const auto ext=mediaKind(path);if(ext=="mp4"||ext=="webm")return Json::object();
        const auto stamp=fs::last_write_time(path,ec).time_since_epoch().count();if(ec)return Json::object();
        const auto id="library-"+digestKey(utf8(relative.generic_wstring())+std::to_string(stamp)+std::to_string(bytes));
        return {{"id",id},{"revision",id},{"extension",ext},{"kind","image"},{"mime",ext=="jpg"?"image/jpeg":ext=="png"?"image/png":"image/"+ext},{"bytes",bytes},{"storage",utf8(relative.generic_wstring())}};
    }
    Json localStatic(const std::string& executable,const std::string& gameDirectory={})const{
        if(executable.empty()&&gameDirectory.empty())return Json::object();
        auto overrides=read(root_/L"config"/L"manual-overrides.json");const auto wanted=normalized(executable);
        if(overrides.contains("items")&&overrides["items"].is_object())for(auto it=overrides["items"].begin();it!=overrides["items"].end();++it){
            if(normalized(it.key())!=wanted)continue;const auto& item=it.value();
            const auto policy=item.value("artworkProtection",Json::object()).value("wallpaper",std::string{});
            if(policy=="deleted")return {{"deleted",true}};
            if(item.contains("wallpaper")){const auto& w=item["wallpaper"];const auto file=w.is_string()?w.get<std::string>():w.value("portableFile",w.value("file",std::string{}));
                auto asset=reference(fs::path(wide(file)));if(!asset.empty())return {{"asset",asset},{"manual",true}};}
        }
        std::vector<std::string> directories;if(!gameDirectory.empty())directories.push_back(gameDirectory);
        else {const auto state=read(root_/L"state"/L"library-scan.json");for(const auto& game:state.value("games",Json::array()))if(normalized(game.value("primaryExecutable",std::string{}))==wanted){const auto directory=game.value("gameDirectory",std::string{});if(!directory.empty())directories.push_back(directory);}}
        for(const auto& directory:directories){
            const auto manifest=read(root_/L"artwork"/wide(canonicalDirectoryId(fs::path(wide(directory))))/L"manifest.json");
            for(const auto& item:manifest.value("artwork",Json::array())){
                const auto type=item.value("type",std::string{});if(type!="hero"&&type!="wallpaper")continue;
                const auto file=item.value("portableFile",item.value("file",std::string{}));if(file.empty())continue;
                const auto asset=reference(fs::path(wide(file)));if(!asset.empty())return {{"asset",asset},{"manual",false}};
            }
        }
        return Json::object();
    }
    bool adoptStatic(Json& entry)const{
        const auto local=localStatic(entry.value("executable",std::string{}),entry.value("gameDirectory",std::string{}));if(local.empty())return false;
        if(local.value("deleted",false)){if(entry["deletedSlots"].value("static",false))return false;entry["deletedSlots"]["static"]=true;entry["assets"].erase("static");return true;}
        if(!local.contains("asset"))return false;if(entry["deletedSlots"].value("static",false)){if(!local.value("manual",false))return false;entry["deletedSlots"]["static"]=false;}
        if(entry["assets"].contains("static")&&entry["assets"]["static"]==local["asset"])return false;
        if(entry["manualSlots"].value("static",false)&&!local.value("manual",false))return false;
        entry["assets"]["static"]=local["asset"];entry["manualSlots"]["static"]=local.value("manual",false);return true;
    }
public:
    explicit Store(fs::path root):root_(std::move(root)){}
    const fs::path& root()const{return root_;}
    Json snapshot()const{Lock lock(root_);auto j=document();for(auto& e:j["entries"])if(e.value("status",std::string{})=="pending"&&leaseOwnerGone(e)){e["leaseUntil"]=0;e["leaseAbandoned"]=true;}return j;}
    Json resolve(const Json& a)const{Lock lock(root_);auto j=document();return resolveDocument(j,a);}
    Json resolveMany(const Json& targets)const{
        if(!targets.is_array()||targets.size()>4)throw std::runtime_error("media-prewarm-target-limit");
        if(targets.empty())return Json::array();Lock lock(root_);auto j=document();Json entries=Json::array();
        for(const auto& target:targets){if(!target.is_object())throw std::runtime_error("media-prewarm-target-invalid");entries.push_back(resolveDocument(j,target));}return entries;
    }
private:
    Json resolveDocument(const Json& j,const Json& a)const{
        auto key=canonicalKey(j,a);
        if(!j["entries"].contains(key)){
            const auto exe=normalized(a.value("executable",std::string{}));std::string found;
            if(!exe.empty())for(auto it=j["entries"].begin();it!=j["entries"].end();++it)if(normalized(it.value().value("executable",std::string{}))==exe){if(!found.empty())return {{"key",key},{"status","ambiguous"},{"bindingRevision",0}};found=it.key();}
            if(!found.empty())key=found;
        }
        if(!j["entries"].contains(key))return {{"key",key},{"status","missing"},{"bindingRevision",0},{"ready",false}};
        auto e=normalizeEntry(j["entries"][key]);std::error_code ec;
        if(e.contains("asset"))e["ready"]=fs::is_regular_file(assetPath(e["asset"]),ec)&&!ec;
        for(const auto* slot:{"static","dynamic"}){const bool exists=e[std::string(slot)+"Ready"].get<bool>()&&fs::is_regular_file(assetPath(e["assets"][slot]),ec)&&!ec;e[std::string(slot)+"Ready"]=exists;ec.clear();}
        if(!e.value("ready",false)&&!e.value("deleted",false)&&e.value("wallpaperMode",std::string{})=="dynamic"&&e.value("staticReady",false)){e["asset"]=e["assets"]["static"];e["ready"]=true;}
        return e;
    }
public:
    Json registerEntry(const Json& a){
        Lock lock(root_);auto j=document();const auto key=canonicalKey(j,a);const bool exists=j["entries"].contains(key);auto e=seed(j,a);
        const bool changed=adoptStatic(e);if(changed){e["bindingRevision"]=e.value("bindingRevision",uint64_t{0})+1;releasePreparation(e);}
        e=normalizeEntry(e);if(!exists||changed||j.value("_mediaChanged",false)){j["entries"][key]=e;save(j);}return e;
    }
    Json begin(const Json& a,const std::string& manualRequest={}){
        Lock lock(root_);auto j=document();const auto originalKey=keyFor(a);const bool existed=j["entries"].contains(canonicalKey(j,a));auto e=seed(j,a);const bool adopted=adoptStatic(e);if(adopted){e["bindingRevision"]=e.value("bindingRevision",uint64_t{0})+1;releasePreparation(e);}e=normalizeEntry(e);
        const bool retryOnce=safeKey(manualRequest)&&e.value("lastManualRetry",std::string{})!=manualRequest;
        Json slots=Json::array();for(const auto* slot:{"dynamic","static"}){
            bool exists=e.value(std::string(slot)+"Ready",false);std::error_code ec;if(exists)exists=fs::is_regular_file(assetPath(e["assets"][slot]),ec)&&!ec;
            const bool refresh=(e.value("refreshRequested",false)&&e.value("refreshSlot",std::string{"dynamic"})==slot)||(std::string(slot)=="dynamic"&&a.value("videoQualityPolicy",0)>=videoQualityPolicy&&needsVideoQualityUpgrade(e,now()));
            if((!exists||refresh)&&!e["deletedSlots"].value(slot,false)&&(!e["manualSlots"].value(slot,false)||refresh)&&!(e.value(std::string(slot)=="dynamic"?"videoUnavailableUntil":"staticUnavailableUntil",int64_t{0})>now()&&!refresh&&!retryOnce))slots.push_back(slot);
        }
        const auto returnExisting=[&](){if(!existed||adopted||j.value("_mediaChanged",false)){j["entries"][e["key"].get<std::string>()]=e;save(j);}e["acquired"]=false;return e;};
        if(e.value("deleted",false)||slots.empty())return returnExisting();
        if((e.value("leaseUntil",int64_t{0})>now()&&!leaseOwnerGone(e))||(e.value("retryAt",int64_t{0})>now()&&!retryOnce))return returnExisting();
        if(retryOnce){e["lastManualRetry"]=manualRequest;e["retryAt"]=0;e["attempts"]=0;e["error"]="";}
        e["demandConsumer"]=a.value("demandConsumer",std::string{"library"});e["taskId"]=randomId();e["taskSlots"]=slots;e["leasePid"]=GetCurrentProcessId();e["leaseBirth"]=processBirthToken(GetCurrentProcessId());e["leaseUntil"]=now()+600000;e["status"]="pending";e["refreshRequested"]=false;e["updatedAt"]=now();
        if(e.value("name",std::string{}).empty())e["name"]=a.value("name",std::string{});
        if(e.value("executable",std::string{}).empty())e["executable"]=a.value("executable",std::string{});
        j["entries"][e["key"].get<std::string>()]=e;save(j);e["acquired"]=true;return e;
    }
    fs::path assetPath(const Json& asset)const{
        const auto id=asset.value("id",std::string{}),ext=asset.value("extension",std::string{});
        if(!safeKey(id)||(ext!="mp4"&&ext!="webm"&&ext!="jpg"&&ext!="png"&&ext!="webp"&&ext!="bmp"&&ext!="ico"))throw std::runtime_error("media-invalid-asset");
        const auto storage=asset.value("storage",std::string{});if(storage.empty())return root_/L"artwork"/L"video"/wide(id+"."+ext);
        const auto relative=fs::path(wide(storage));if(relative.is_absolute()||relative.empty()||*relative.begin()!=L"artwork")throw std::runtime_error("media-invalid-storage");
        for(const auto& part:relative)if(part==L"..")throw std::runtime_error("media-invalid-storage");
        const auto path=fs::weakly_canonical(root_/relative);const auto base=normalized(utf8(fs::weakly_canonical(root_).wstring()))+"\\";
        if(normalized(utf8(path.wstring())).rfind(base,0)!=0)throw std::runtime_error("media-storage-escape");return path;
    }
    Json publish(const Json& a,const fs::path& source,bool manual){
        const auto ext=mediaKind(source);std::error_code ec;const auto bytes=fs::file_size(source,ec);if(ec||bytes==0||bytes>capacity::maxAssetBytes)throw std::runtime_error("media-size-invalid");
        const auto slot=ext=="mp4"||ext=="webm"?"dynamic":"static";
        if(a.contains("slot")&&a.value("slot",std::string{})!=slot)throw std::runtime_error("media-slot-container-mismatch");
        Lock lock(root_);auto j=document();auto e=seed(j,a);const auto expected=a.value("bindingRevision",uint64_t{0});
        if(expected!=e.value("bindingRevision",uint64_t{0})||(!manual&&(e["manualSlots"].value(slot,false)||e["deletedSlots"].value(slot,false)||e.value("taskId",std::string{})!=a.value("taskId",std::string{}))))throw std::runtime_error("media-stale-publication");
        const auto id=randomId();Json asset={{"id",id},{"revision",id},{"extension",ext},{"kind",std::string(slot)=="dynamic"?"video":"image"},{"mime",ext=="mp4"?"video/mp4":ext=="webm"?"video/webm":ext=="jpg"?"image/jpeg":"image/"+ext},{"bytes",bytes}};
        if(!manual&&std::string(slot)=="dynamic"&&a.value("videoQualityPolicy",0)>=videoQualityPolicy){asset["qualityPolicy"]=videoQualityPolicy;asset["targetHeight"]=preferredVideoHeight(e);if(a.value("selectedHeight",0)>0)asset["selectedHeight"]=a["selectedHeight"];}
        capacity::require(root_,bytes,!manual);
        const auto path=assetPath(asset);fs::create_directories(path.parent_path());fs::copy_file(source,path,fs::copy_options::none);
        e["assets"][slot]=asset;e["invalidSlots"].erase(slot);e["manualSlots"][slot]=manual;e["deletedSlots"][slot]=false;e["bindingRevision"]=expected+1;e["manual"]=e["manualSlots"].value(e["wallpaperMode"].get<std::string>(),false);e["deleted"]=false;
        if(e.contains("taskSlots")){Json remaining=Json::array();for(const auto& value:e["taskSlots"])if(value!=slot)remaining.push_back(value);e["taskSlots"]=remaining;}
        if(manual){releasePreparation(e);if(std::string(slot)=="dynamic")e.erase("selectedVideo");}
        e["status"]="ready";e["retryAt"]=0;e["error"]="";e["updatedAt"]=now();e["source"]=manual?"manual":"steam";if(!manual&&a.value("verified",false))e["identityVerified"]=true;
        if(!manual&&!a.value("name",std::string{}).empty())e["name"]=a.value("name",std::string{});
        if(!manual&&a.contains("legacySourceFingerprint"))e["legacySourceFingerprint"]=a["legacySourceFingerprint"];
        e=normalizeEntry(e);j["entries"][e["key"].get<std::string>()]=e;
        try{save(j);}catch(...){fs::remove(path,ec);throw;}return e;
    }
    Json importLegacy(const Json& a,const fs::path& source,const std::function<bool()>& admitted){
        auto current=resolve(a);if(current.value("dynamicReady",false)||current.value("deleted",false)||current.value("manualSlots",Json::object()).value("dynamic",false)||current.value("deletedSlots",Json::object()).value("dynamic",false))return current;
        const auto fingerprint=digestKey(normalized(utf8(fs::absolute(source).wstring()))+":"+std::to_string(fs::file_size(source))+":"+std::to_string(fs::last_write_time(source).time_since_epoch().count()));
        if(current.value("legacySourceFingerprint",std::string{})==fingerprint)return current;
        const bool explicitLease=!a.value("taskId",std::string{}).empty();
        const bool ownLease=current.value("leasePid",DWORD{0})==GetCurrentProcessId()&&!leaseOwnerGone(current)&&current.value("leaseUntil",int64_t{0})>now()&&!current.value("taskId",std::string{}).empty();
        const bool borrowed=explicitLease||ownLease;auto lease=borrowed?current:begin(a);
        if(borrowed&&(lease.value("leasePid",DWORD{0})!=GetCurrentProcessId()||leaseOwnerGone(lease)||explicitLease&&(lease.value("taskId",std::string{})!=a.value("taskId",std::string{})||lease.value("bindingRevision",uint64_t{0})!=a.value("bindingRevision",uint64_t{0}))))throw std::runtime_error("media-stale-legacy-lease");
        if(!borrowed&&!lease.value("acquired",false))return current;
        try{if(!admitted()){if(!borrowed)finish(lease);return resolve(a);}auto publication=lease;publication["verified"]=a.value("verified",false);publication["legacySourceFingerprint"]=fingerprint;auto entry=publish(publication,source,false);if(!explicitLease)finish(entry);return entry;}
        catch(...){if(!borrowed)fail(lease);throw;}
    }
    Json finish(const Json& a){
        Lock lock(root_);auto j=document();const auto key=canonicalKey(j,a);if(!j["entries"].contains(key))return Json::object();auto e=seed(j,a);
        if(e.value("taskId",std::string{})!=a.value("taskId",std::string{})||e.value("bindingRevision",uint64_t{0})!=a.value("bindingRevision",uint64_t{0}))return e;
        if(a.value("videoUnavailable",false))e["videoUnavailableUntil"]=now()+24ll*60*60*1000;
        if(a.value("staticUnavailable",false))e["staticUnavailableUntil"]=now()+24ll*60*60*1000;
        e["leaseUntil"]=0;e["taskSlots"]=Json::array();e["retryAt"]=0;e["error"]="";e["attempts"]=0;e["status"]=e.contains("asset")?"ready":"missing";j["entries"][key]=e;save(j);return e;
    }
    Json fail(const Json& a){
        Lock lock(root_);auto j=document();const auto key=canonicalKey(j,a);if(!j["entries"].contains(key))return Json::object();auto e=seed(j,a);
        if(e.value("taskId",std::string{})!=a.value("taskId",std::string{})||e.value("bindingRevision",uint64_t{0})!=a.value("bindingRevision",uint64_t{0}))return e;
        const auto attempts=std::min(8,e.value("attempts",0)+1);e["status"]="error";e["attempts"]=attempts;e["leaseUntil"]=0;e["retryAt"]=now()+(e.value("demandConsumer",std::string{})=="background"?20000ll:std::min<int64_t>(3600000,30000ll*(1ll<<attempts)));e["error"]=a.value("reason",std::string{"prepare-failed"}).substr(0,160);j["entries"][key]=e;save(j);return e;
    }
    Json edit(const Json& a){
        Lock lock(root_);auto j=document();auto e=seed(j,a);if(a.value("bindingRevision",uint64_t{0})!=e.value("bindingRevision",uint64_t{0}))throw std::runtime_error("media-edit-conflict");
        const auto action=a.value("action",std::string{});const auto slot=a.value("slot",e.value("wallpaperMode",std::string{"dynamic"}));if(slot!="static"&&slot!="dynamic")throw std::runtime_error("media-slot-invalid");
        if(action=="delete"){if(slot=="dynamic")e.erase("selectedVideo");e["assets"].erase(slot);e["deletedSlots"][slot]=true;e["manualSlots"][slot]=true;e["status"]="deleted";}
        else if(action=="select-video"){const auto selected=a.value("selectedVideo",Json::object());if(selected.value("appId",int64_t{0})!=e.value("appId",int64_t{0})||stringField(selected,"url").empty())throw std::runtime_error("media-video-choice-invalid");e["selectedVideo"]=selected;e["deletedSlots"]["dynamic"]=false;e["manualSlots"]["dynamic"]=false;e["refreshRequested"]=true;e["refreshSlot"]="dynamic";e["videoUnavailableUntil"]=0;e["retryAt"]=0;e["deleted"]=false;e["status"]="missing";}
        else if(action=="redownload"){e["deletedSlots"][slot]=false;e["manualSlots"][slot]=false;e["manual"]=false;e["deleted"]=false;e["refreshRequested"]=true;e["refreshSlot"]=slot;e["videoUnavailableUntil"]=0;e["staticUnavailableUntil"]=0;e["invalidSlots"].erase(slot);e["retryAt"]=0;e["status"]="missing";}
        else if(action=="playback-failed"){
            const auto assetId=a.value("assetId",std::string{});if(!e["assets"].contains(slot)||assetId.empty()||e["assets"][slot].value("id",std::string{})!=assetId)throw std::runtime_error("media-stale-playback");
            if(e["invalidSlots"].value(slot,std::string{})==assetId)return e;
            e["invalidSlots"][slot]=assetId;e["status"]="error";e["error"]="playback-failed";const auto attempts=std::min(8,e.value("attempts",0)+1);e["attempts"]=attempts;e["retryAt"]=now()+std::min<int64_t>(3600000,30000ll*(1ll<<attempts));
        }
        else if(action=="rename"){const auto name=a.value("name",std::string{});if(name.empty()||name.size()>512)throw std::runtime_error("media-invalid-name");e["name"]=name;}
        else if(action=="mode"){const auto mode=a.value("mode",std::string{});if(mode!="static"&&mode!="dynamic")throw std::runtime_error("media-mode-invalid");e["wallpaperMode"]=mode;e["modeExplicit"]=true;}
        else if(action=="playback"){const double start=a.value("start",0.0),end=a.value("end",0.0);const auto fit=a.value("fit",std::string{"cover"});if(!std::isfinite(start)||!std::isfinite(end)||start<0||start>86400||end<0||end>86400||(end>0&&end<=start)||(fit!="cover"&&fit!="contain"))throw std::runtime_error("media-playback-range-invalid");e["playback"]={{"start",start},{"end",end},{"fit",fit}};e["playbackExplicit"]=true;}
        else throw std::runtime_error("media-edit-action-invalid");
        e["bindingRevision"]=e.value("bindingRevision",uint64_t{0})+1;releasePreparation(e);e["updatedAt"]=now();e=normalizeEntry(e);j["entries"][e["key"].get<std::string>()]=e;save(j);return e;
    }
    Json rememberIdentity(const std::string& executable,const fs::path& manifestPath,int64_t startedAt){
        const auto manifest=read(manifestPath);if(normalized(manifest.value("exe",std::string{}))!=normalized(executable))throw std::runtime_error("media-identity-target-mismatch");
        const auto match=manifest.value("match",Json::object());if(!custom_steam_library::verifiedSteamIdentityMatch(match))return identity(executable);
        auto segment=normalized(executable);for(auto& c:segment)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_')c='_';
        if(segment.size()>230)throw std::runtime_error("media-identity-path-too-long");
        HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\YeManCustomSteamLibraryDataTransaction");if(!mutex)throw std::runtime_error("media-identity-lock-failed");
        const auto waited=WaitForSingleObject(mutex,10000);if(waited!=WAIT_OBJECT_0&&waited!=WAIT_ABANDONED){CloseHandle(mutex);throw std::runtime_error("media-identity-owner-busy");}
        try{const auto cache=root_/L"cache"/L"identity"/wide(segment+".json");Json previous=Json::object();try{previous=read(cache);}catch(const std::exception&){}
            if(normalized(stringField(previous,"executable"))==normalized(executable)&&(previous.contains("updatedAt")&&previous["updatedAt"].is_number_integer()?previous["updatedAt"].get<int64_t>():0)>startedAt&&custom_steam_library::verifiedSteamIdentityMatch(previous.value("match",Json::object()))){ReleaseMutex(mutex);CloseHandle(mutex);return identity(executable);}
            const auto name=stringField(match,"formalName",stringField(match,"displayName"));Json value={{"executable",executable},{"manifestPath",utf8(manifestPath.wstring())},{"updatedAt",now()},{"match",match},{"metadata",manifest.value("metadata",Json::object())},{"formalName",name},{"steamAppId",match.value("appId",int64_t{0})}};atomicWrite(cache,value);ReleaseMutex(mutex);CloseHandle(mutex);
        }catch(...){ReleaseMutex(mutex);CloseHandle(mutex);throw;}return identity(executable);
    }
    Json identity(const std::string& executable)const{
        if(executable.empty())return Json::object();auto segment=normalized(executable);for(auto& c:segment)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_')c='_';if(segment.size()>230)return Json::object();
        try{const auto j=read(root_/L"cache"/L"identity"/wide(segment+".json"));if(normalized(stringField(j,"executable"))!=normalized(executable))return Json::object();const auto match=j.value("match",Json::object());if(!custom_steam_library::verifiedSteamIdentityMatch(match))return Json::object();
            return {{"appId",match.value("appId",int64_t{0})},{"name",stringField(j,"formalName",stringField(match,"formalName",stringField(match,"displayName")))},{"verified",true},{"source","library-identity"}};
        }catch(const std::exception&){return Json::object();} // Cache damage is a miss; a verified worker result may repair it.

    }
    Json collectOrphans()const{
        Lock lock(root_);const auto j=document();std::set<std::wstring> used;
        for(const auto& e:j["entries"])for(const auto& a:e["assets"])if(a.is_object())used.insert(assetPath(a).filename().wstring());
        const auto directory=root_/L"artwork"/L"video";std::error_code ec;size_t examined=0,removed=0;
        for(fs::directory_iterator it(directory,ec),end;!ec&&it!=end&&examined<10000;it.increment(ec),++examined){
            if(it->is_symlink(ec)||!it->is_regular_file(ec)||used.count(it->path().filename().wstring()))continue;
            const auto extension=it->path().extension().wstring();if(extension!=L".mp4"&&extension!=L".webm"&&extension!=L".jpg"&&extension!=L".png"&&extension!=L".webp"&&extension!=L".bmp"&&extension!=L".ico")continue;
            const auto id=utf8(it->path().stem().wstring());if(id.size()!=32||!safeKey(id))continue;
            const auto age=fs::file_time_type::clock::now()-it->last_write_time(ec);if(ec||age<std::chrono::hours(24))continue;
            if(fs::remove(it->path(),ec))++removed;ec.clear();
        }return {{"examined",examined},{"removed",removed},{"temporaryRemoved",capacity::collectTemporary(root_)}};
    }
    Json inventory(const Json& shortcuts=Json::array())const{
        const auto state=read(root_/L"state"/L"library-scan.json"),config=read(root_/L"config"/L"library-config.json");Json result=Json::array();
        auto games=state.value("games",Json::array());if(!games.is_array())games=Json::array();
        if(shortcuts.is_array())for(const auto& item:shortcuts)if(item.is_object()&&item.contains("executable"))games.push_back({{"primaryExecutable",item["executable"]},{"directoryName",stringField(item,"name")}});
        std::map<std::string,Json> added;
        if(shortcuts.is_array())for(const auto& item:shortcuts)if(item.is_object()&&!stringField(item,"executable").empty())added[normalized(stringField(item,"executable"))]=item;
        std::set<std::string> seen;
        static const wchar_t* excludedStems[]={
#include "game_detection_blacklist_entries.h"
        };
        for(const auto& game:games){
            if(!game.is_object())continue;const auto exe=stringField(game,"primaryExecutable");if(exe.empty()||!seen.insert(normalized(exe)).second)continue;
            if(stringField(game,"contentType")=="non-game"||stringField(game,"status")=="unrecognized-tool"||custom_steam_library::builtinExcludedSteamTool(game))continue;
            auto stem=wide(normalized(utf8(fs::path(wide(exe)).stem().wstring())));bool excluded=false;
            for(const auto* raw:excludedStems){std::wstring pattern(raw);if(pattern.ends_with(L"*")){pattern.pop_back();if(stem.starts_with(pattern)){excluded=true;break;}}else if(stem==pattern){excluded=true;break;}}
            if(excluded)continue;const auto directory=stringField(game,"gameDirectory");
            if(config.contains("manualBuckets")&&config["manualBuckets"].is_object())for(auto it=config["manualBuckets"].begin();it!=config["manualBuckets"].end();++it){const auto key=normalized(it.key());if((key==normalized(exe)||!directory.empty()&&key==normalized(directory))&&it.value().is_object()&&stringField(it.value(),"bucket")=="non-game"){excluded=true;break;}}
            if(excluded)continue;auto id=identity(exe);if(!id.value("verified",false))id={{"appId",0},{"name",stringField(game,"primaryProductName",stringField(game,"directoryName"))},{"verified",false}};
            id["executable"]=exe;id["gameDirectory"]=directory;id["sourceTier"]=2;id["catalogSource"]="ymcc-confirmed";
            if(const auto found=added.find(normalized(exe));found!=added.end()){id["catalogSource"]="steam-shortcut";for(const auto* field:{"shortcutAppId","steamAccount","lastPlayed"})if(found->second.contains(field))id[field]=found->second[field];}
            result.push_back(id);
        }return result;
    }

};
inline std::wstring inventoryEventName(const fs::path& root){return L"Local\\YMCC-MediaInventoryChanged-"+wide(digestKey(normalized(utf8(fs::absolute(root).wstring()))))+L"-main";}
inline bool signalInventoryChanged(const fs::path& root){HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,inventoryEventName(root).c_str());if(!event)return false;const bool signaled=SetEvent(event)!=FALSE;CloseHandle(event);return signaled;}
inline bool publishInventoryFile(const fs::path& root,const fs::path& file){
    const auto name=normalized(utf8(file.filename().wstring()));if(name!="library-scan.json"&&name!="steam-import-plan.json"&&name!="steam-add-plan.json"&&name!="library-steam-add-plan.json"&&name!="last-steam-add-all-accounts.json"&&name!="library-config.json"&&name!="manual-overrides.json")return false;
    const auto base=normalized(utf8(fs::absolute(root).lexically_normal().wstring()))+"\\";const auto target=normalized(utf8(fs::absolute(file).lexically_normal().wstring()));
    if(target!=base+"state\\library-scan.json"&&target!=base+"state\\steam-import-plan.json"&&target!=base+"state\\steam-add-plan.json"&&target!=base+"state\\library-steam-add-plan.json"&&target!=base+"state\\last-steam-add-all-accounts.json"&&target!=base+"config\\library-config.json"&&target!=base+"config\\manual-overrides.json")return false;
    return signalInventoryChanged(root);
}
class Watcher {
    HANDLE changed_=nullptr,stop_=nullptr,inventory_=nullptr;std::thread worker_;
public:
    Watcher(const fs::path& root,std::function<void()> changed,const std::wstring& role=L"main",std::function<void()> inventoryChanged={}){
        auto name=L"Local\\YMCC-MediaChanged-"+wide(digestKey(normalized(utf8(fs::absolute(root).wstring()))))+L"-"+role;
        changed_=CreateEventW(nullptr,FALSE,FALSE,name.c_str());stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(inventoryChanged)inventory_=CreateEventW(nullptr,FALSE,FALSE,inventoryEventName(root).c_str());
        if(!changed_||!stop_||(inventoryChanged&&!inventory_)){if(changed_)CloseHandle(changed_);if(stop_)CloseHandle(stop_);if(inventory_)CloseHandle(inventory_);throw std::runtime_error("media-watch-unavailable");}
        worker_=std::thread([this,changed,inventoryChanged]{HANDLE events[]={stop_,changed_,inventory_};const DWORD count=inventory_?3:2;
            for(;;){auto r=WaitForMultipleObjects(count,events,FALSE,INFINITE);if(r==WAIT_OBJECT_0+1){try{changed();}catch(...){}}else if(inventory_&&r==WAIT_OBJECT_0+2){try{inventoryChanged();}catch(...){}}else return;}
        });
    }
    ~Watcher(){SetEvent(stop_);if(worker_.joinable())worker_.join();CloseHandle(changed_);CloseHandle(stop_);if(inventory_)CloseHandle(inventory_);}
};
} // namespace ymcc::media
