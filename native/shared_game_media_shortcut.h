#pragma once
// Display identity only: parse the active account's file, never write Steam data.
#include "shared_game_media.h"
#include <map>
namespace ymcc::media {
class ShortcutVdf {
    std::string bytes_; size_t at_=0, nodes_=0;
    uint64_t number(size_t count) { if(at_+count>bytes_.size())throw std::runtime_error("media-shortcut-truncated");uint64_t value=0;for(size_t i=0;i<count;i++)value|=uint64_t(static_cast<unsigned char>(bytes_[at_++]))<<(8*i);return value; }
    std::string text(){const auto end=bytes_.find('\0',at_);if(end==std::string::npos||end-at_>32768)throw std::runtime_error("media-shortcut-string");auto value=bytes_.substr(at_,end-at_);at_=end+1;return value;}
    Json object(int depth){if(depth>16)throw std::runtime_error("media-shortcut-depth");Json value=Json::object();while(at_<bytes_.size()){
        const auto type=number(1);if(type==8)return value;if(++nodes_>100000)throw std::runtime_error("media-shortcut-size");auto key=text();for(auto& c:key)c=static_cast<char>(tolower(static_cast<unsigned char>(c)));if(value.contains(key))throw std::runtime_error("media-shortcut-duplicate");
        if(type==0)value[key]=object(depth+1);else if(type==1)value[key]=text();else if(type==2||type==3)value[key]=number(4);else if(type==7)value[key]=number(8);else throw std::runtime_error("media-shortcut-type");
    }throw std::runtime_error("media-shortcut-unterminated");}
public:
    explicit ShortcutVdf(std::string bytes):bytes_(std::move(bytes)){if(bytes_.size()>16*1024*1024)throw std::runtime_error("media-shortcut-size");}
    Json parse(){auto value=object(0);if(at_!=bytes_.size())throw std::runtime_error("media-shortcut-tail");return value;}
};
inline uint32_t shortcutAppId(const std::string& id){
    if(id.empty()||id.size()>20||!std::all_of(id.begin(),id.end(),[](char c){return c>='0'&&c<='9';}))throw std::runtime_error("media-shortcut-id");
    const auto value=std::stoull(id);uint64_t app=value;
    if(value>UINT32_MAX){if((value&0xffffffffull)!=0x02000000ull)throw std::runtime_error("media-shortcut-gameid");app=value>>32;}
    if(app<0x80000000ull||app>UINT32_MAX)throw std::runtime_error("media-shortcut-id");return static_cast<uint32_t>(app);
}
inline Json readShortcutItems(const fs::path& file){
    std::error_code ec;const auto size=fs::file_size(file,ec);if(ec||size>16*1024*1024)throw std::runtime_error("media-shortcuts-unavailable");
    const auto before=fs::last_write_time(file);std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("media-shortcuts-unavailable");
    std::string bytes((std::istreambuf_iterator<char>(stream)),{});if(bytes.size()!=size||fs::last_write_time(file)!=before)throw std::runtime_error("media-shortcuts-changed");
    auto root=ShortcutVdf(std::move(bytes)).parse();if(!root.contains("shortcuts")||!root["shortcuts"].is_object())throw std::runtime_error("media-shortcuts-invalid");return root["shortcuts"];
}
inline Json describeShortcut(const Json& item){
    if(!item.is_object()||!item.contains("appid")||!item["appid"].is_number_unsigned())throw std::runtime_error("media-shortcut-id");
    const auto id=shortcutAppId(std::to_string(item["appid"].get<uint64_t>()));auto exe=stringField(item,"exe");
    if(exe.size()>1&&exe.front()=='"'&&exe.back()=='"')exe=exe.substr(1,exe.size()-2);
    const auto path=fs::path(wide(exe));if(!path.is_absolute()||normalized(utf8(path.extension().wstring()))!=".exe")throw std::runtime_error("media-shortcut-executable-unconfirmed");
    return {{"executable",exe},{"name",stringField(item,"appname")},{"shortcutAppId",std::to_string(id)},{"lastPlayed",(std::min)(uint64_t(now()/1000),item.value("lastplaytime",uint64_t{0}))*1000}};
}
inline Json shortcutIdentity(const fs::path& file,const std::string& gameId){
    const auto wanted=shortcutAppId(gameId);const auto items=readShortcutItems(file);Json found=Json::object();
    for(const auto& item:items){if(!item.is_object()||!item.contains("appid")||!item["appid"].is_number_unsigned()||item["appid"].get<uint64_t>()!=wanted)continue;
        if(!found.empty())throw std::runtime_error("media-shortcut-ambiguous");found=describeShortcut(item);
    }return found;
}
inline Json shortcutInventory(const fs::path& file){
    const auto items=readShortcutItems(file);std::map<uint32_t,Json> found;std::set<uint32_t> seen,ambiguous;
    for(const auto& item:items){
        if(!item.is_object()||!item.contains("appid")||!item["appid"].is_number_unsigned())continue;
        try{const auto id=shortcutAppId(std::to_string(item["appid"].get<uint64_t>()));if(!seen.insert(id).second){ambiguous.insert(id);found.erase(id);continue;}
            if(!ambiguous.count(id))found[id]=describeShortcut(item);
        }catch(const std::exception&){} // A URL/tool shortcut does not hide other valid EXEs.
    }
    Json result=Json::array();for(const auto& [id,item]:found)if(!ambiguous.count(id))result.push_back(item);return result;
}
inline Json shortcutInventoryForAccount(const fs::path& file,uint32_t account,const std::function<uint32_t()>& activeAccount){
    if(!account||activeAccount()!=account)return Json::array();auto rows=shortcutInventory(file);if(activeAccount()!=account)return Json::array();for(auto& row:rows)row["steamAccount"]=std::to_string(account);return rows;
}
// Runtime diagnostics preserve the original admission rule: a failed read never
// becomes a guessed empty inventory or a successful identity. No Steam writes.
inline Json observeShortcutInventory(const fs::path& file,uint32_t account,const std::function<uint32_t()>& activeAccount) {
    Json result={{"available",false},{"reason","waiting-steam-account"},{"rows",Json::array()},
        {"evidence",{{"file",utf8(file.wstring())},{"account",std::to_string(account)},{"sourceReadOnly",true}}}};
    auto& evidence=result["evidence"];
    const auto changed=[&]{if(!account||activeAccount()!=account){result["rows"]=Json::array();result["available"]=false;result["reason"]=account?"account-changed":"waiting-steam-account";return true;}return false;};
    if(changed())return result;
    std::error_code error;const auto status=fs::status(file,error);
    if(error||!fs::exists(status)) {
        evidence["filePresent"]=false;evidence["fileStatusErrorCode"]=error.value();
        result["reason"]=!error||error==std::errc::no_such_file_or_directory?"shortcut-file-missing":"shortcut-file-status-failed";
        changed();return result;
    }
    evidence["filePresent"]=true;evidence["regularFile"]=fs::is_regular_file(status);
    if(!fs::is_regular_file(status)){result["reason"]="shortcut-file-not-regular";changed();return result;}
    const auto bytes=fs::file_size(file,error);evidence["fileSizeErrorCode"]=error.value();
    if(error){result["reason"]="shortcut-file-status-failed";changed();return result;}
    evidence["fileBytes"]=bytes;
    if(bytes>16*1024*1024){result["reason"]="shortcut-file-too-large";changed();return result;}
    try {
        auto rows=shortcutInventoryForAccount(file,account,activeAccount);
        if(changed())return result;
        result["rows"]=std::move(rows);result["available"]=true;result["reason"]="ready";
    }catch(const std::exception& exception){
        const std::string detail=exception.what();
        const bool tag=detail.size()<=100&&detail.starts_with("media-shortcut")&&std::all_of(detail.begin(),detail.end(),[](unsigned char c){return (c>='a'&&c<='z')||c=='-';});
        evidence["readErrorTag"]=tag?detail:"shortcut-read-exception";
        result["reason"]=detail=="media-shortcuts-changed"?"shortcut-file-changed":detail=="media-shortcuts-unavailable"?"shortcut-file-read-failed":"shortcut-vdf-invalid";
        if(detail=="media-shortcuts-unavailable"){
            // This is a separate after-failure probe, not a claim about an earlier
            // open. Distinguishes a still-present access/sharing error for support.
            HANDLE probe=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            const auto code=probe==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;
            if(probe!=INVALID_HANDLE_VALUE)CloseHandle(probe);
            evidence["afterFailureReadProbe"]=true;evidence["afterFailureReadErrorCode"]=code;
        }
        changed();
    }
    return result;
}
} // namespace ymcc::media
