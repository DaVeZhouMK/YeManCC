#pragma once

#include <fstream>
#include <optional>
#include <string_view>
#include "steam_log_export.h"
#include "json.hpp"

namespace ymcc::logexport {
class SteamTailAliases {
    using Json = nlohmann::json;
    using Entries = std::vector<std::pair<std::wstring,std::wstring>>;
    static constexpr unsigned long long limit = 2 * 1024 * 1024;
    static constexpr std::wstring_view manifest = L"steam-input-export-manifest.json";
    struct Pair { std::wstring_view tail, source; std::string_view tailUtf8, sourceUtf8; };
    static constexpr std::array pairs{
        Pair{L"steam-gameprocess.log", L"gameprocess_log.txt", "steam-gameprocess.log", "gameprocess_log.txt"},
        Pair{L"steam-controller.log", L"controller.txt", "steam-controller.log", "controller.txt"},
        Pair{L"steam-controller-ui.log", L"controller_ui.txt", "steam-controller-ui.log", "controller_ui.txt"},
        Pair{L"steam-gameoverlay-renderer.log", L"gameoverlay_renderer.txt", "steam-gameoverlay-renderer.log", "gameoverlay_renderer.txt"},
        Pair{L"steam-gameoverlay-ui.log", L"gameoverlay_ui.txt", "steam-gameoverlay-ui.log", "gameoverlay_ui.txt"},
        Pair{L"steam-gameoverlay-renderer.previous.log", L"gameoverlay_renderer.previous.txt", "steam-gameoverlay-renderer.previous.log", "gameoverlay_renderer.previous.txt"},
        Pair{L"steam-gameoverlay-ui.previous.log", L"gameoverlay_ui.previous.txt", "steam-gameoverlay-ui.previous.log", "gameoverlay_ui.previous.txt"},
    };
    const std::vector<std::wstring>* sources_;
    std::array<std::optional<size_t>,pairs.size()> staged_{};
    std::optional<size_t> manifest_;
    bool invalid_ = false;

public:
    explicit SteamTailAliases(const std::vector<std::wstring>* sources) : sources_(sources) {}

    void observe(const std::wstring& source, const std::wstring& name, size_t ordinal) noexcept {
        if(!sources_ || invalid_ || std::find(sources_->begin(),sources_->end(),source)==sources_->end()) return;
        const auto keep=[&](std::optional<size_t>& slot){ if(slot)invalid_=true;else slot=ordinal; };
        if(name==manifest) keep(manifest_);
        else for(size_t i=0;i<pairs.size();++i) if(name==pairs[i].tail){keep(staged_[i]);break;}
    }

    template<class Digest>
    void omitIdentical(Entries& staged, const fs::path& steamFolder, const Json& copyRows, Json& snapshot, Digest&& digest) const noexcept {
        if(invalid_ || !manifest_) return;
        try {
            const auto path=[&](size_t i){return fs::path(staged.at(i).first)/staged.at(i).second;};
            const auto unique=[&](std::wstring_view name){return std::count_if(staged.begin(),staged.end(),[&](const auto& entry){return entry.second==name;})==1;};
            if(!unique(manifest))return;
            std::error_code ec;
            const auto manifestSize=fs::file_size(path(*manifest_),ec);
            if(ec || manifestSize>64*1024)return;
            std::ifstream in(winPath(path(*manifest_)).c_str(),std::ios::binary);
            std::string raw(static_cast<size_t>(manifestSize),'\0');
            if(!in || !in.read(raw.data(),static_cast<std::streamsize>(raw.size())) || in.peek()!=std::char_traits<char>::eof() || in.bad())return;
            bool manifestKeysOk=true;
            std::vector<std::vector<std::string>> keys;
            const auto parsed=Json::parse(raw,[&](int depth,Json::parse_event_t event,Json& node){
                if(depth>8)throw 0;
                if(event==Json::parse_event_t::object_start)keys.emplace_back();
                else if(event==Json::parse_event_t::object_end)keys.pop_back();
                else if(event==Json::parse_event_t::key){auto& current=keys.back();const auto& k=node.get_ref<const std::string&>();
                    if(std::find(current.begin(),current.end(),k)!=current.end())manifestKeysOk=false;
                    else current.push_back(k);}
                return true;
            },false);
            const auto integerIs=[](const Json& object,const char* key,unsigned long long value){
                const auto it=object.find(key);return it!=object.end() && it->is_number_unsigned() && *it==value;
            };
            if(!manifestKeysOk || !parsed.is_object() || !integerIs(parsed,"schema",1) || parsed.value("scope","")!="steam" ||
                    !integerIs(parsed,"maxBytesPerFile",limit) || !parsed.contains("files") || !parsed["files"].is_array() || parsed["files"].size()>pairs.size())return;
            const auto findUnique=[](const Json& rows,const char* key,std::string_view value)->const Json*{
                const Json* match=nullptr;
                for(const auto& row:rows)if(row.is_object()){
                    const auto it=row.find(key);
                    if(it!=row.end() && it->is_string() && it->get_ref<const std::string&>()==value){if(match)return nullptr;match=&row;}
                }
                return match;
            };
            Json aliases=Json::array();std::array<size_t,pairs.size()> remove{};size_t count=0;
            for(size_t i=0;i<pairs.size();++i)if(staged_[i]){
                const auto& pair=pairs[i];
                if(!unique(pair.tail))continue;
                const auto* tail=findUnique(parsed["files"],"file",pair.tailUtf8);
                if(!tail || tail->value("sourceFile","")!=pair.sourceUtf8 || !tail->value("available",false) ||
                        !tail->value("saved",false) || !tail->contains("truncated") || (*tail)["truncated"]!=false || !integerIs(*tail,"limitBytes",limit))continue;
                const auto left=path(*staged_[i]);const auto bytes=fs::file_size(left,ec);
                if(ec || bytes>limit || !integerIs(*tail,"copiedBytes",bytes) || !integerIs(*tail,"originalBytes",bytes))continue;
                const auto canonical="steam/logs/"+std::string(pair.sourceUtf8);
                const auto* copied=findUnique(copyRows,"entry",canonical);
                if(!copied || !copied->value("copied",false))continue;
                const auto right=steamFolder/L"logs"/pair.source;
                if(fs::file_size(right,ec)!=bytes || ec)continue;
                auto sha=digest.matchingDigest(winPath(left),winPath(right),bytes,canonical);if(sha.size()!=64)continue;
                aliases.push_back({{"tail",pair.tailUtf8},{"canonical",canonical},{"bytes",bytes},{"sha256",std::move(sha)}});
                remove[count++]=*staged_[i];
            }
            if(!count)return;
            // Emit the mapping before excluding any member; a failed comparison keeps both.
            snapshot["steamTailAliases"]={{"schema",1},{"entries",std::move(aliases)}};
            std::sort(remove.begin(),remove.begin()+count,std::greater<>{});
            for(size_t i=0;i<count;++i)staged.erase(staged.begin()+remove[i]);
        }catch(...){} // Optional optimization; independent tails remain available on failure.
    }
};
} // namespace ymcc::logexport
