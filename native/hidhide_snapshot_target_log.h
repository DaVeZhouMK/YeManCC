#pragma once
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include "json.hpp"

namespace ymcc::logging {
inline constexpr char hidHideSnapshotTargetCopiesField[] = "targetInventoryCopiesV1";
inline constexpr std::array hidHideSnapshotTargetListFields{
    "physicalTargets", "gameInputTargets", "excludedNonGameTargets"};
inline constexpr std::size_t hidHideSnapshotTargetCopiesLimit = 64;

inline std::optional<std::string> hidHideSnapshotCompactLine(const char* event, const nlohmann::json& detail) noexcept {
    if (!event || event[0] != 'h' || std::strcmp(event,"hidhide.visibility-snapshot") != 0) return std::nullopt;
    try {
        using Json = nlohmann::json;
        const auto inventory = detail.find("targetInventory");
        if (detail.contains(hidHideSnapshotTargetCopiesField) || inventory == detail.end() ||
            !inventory->is_array() || inventory->empty() || inventory->size() > hidHideSnapshotTargetCopiesLimit) return std::nullopt;
        std::array<std::string_view,hidHideSnapshotTargetCopiesLimit> instances{};
        for (size_t i=0;i<inventory->size();++i) {
            const auto instance=(*inventory)[i].find("instance");
            if (instance!=(*inventory)[i].end() && instance->is_string()) instances[i]=instance->get_ref<const std::string&>();
        }
        Json copies=Json::object();
        std::size_t savings=0;
        for (const auto* field:hidHideSnapshotTargetListFields) {
            const auto list=detail.find(field);
            if(list==detail.end() || !list->is_array() || list->empty() || list->size()>hidHideSnapshotTargetCopiesLimit)continue;
            const bool physical=field==hidHideSnapshotTargetListFields.front();
            Json packed=Json::array();
            std::size_t listSavings=0;
            for(const auto& entry:*list){
                const Json* value=&entry;
                if(physical){
                    const auto instance=entry.find("instance"),hidden=entry.find("hidden");
                    if(!entry.is_object() || instance==entry.end() || hidden==entry.end() || !hidden->is_boolean())break;
                    value=&*instance;
                }
                if(!value->is_string())break;
                const std::string_view instance=value->get_ref<const std::string&>();
                std::size_t index=instances.size();
                if(instance.size()>=8){
                    for(std::size_t i=0;i<inventory->size();++i)if(instances[i]==instance){
                        if(index!=instances.size()){index=instances.size();break;}
                        index=i;
                    }
                }
                if(index==instances.size()){packed.push_back(entry);continue;}
                // Both indices are at most two digits; even unescaped JSON strings
                // save at least their UTF-8 length after replacing the quotes.
                listSavings+=instance.size();
                if(physical){auto target=entry;target["instance"]=index;packed.push_back(std::move(target));}
                else packed.push_back(index);
            }
            if(packed.size()==list->size() && listSavings){savings+=listSavings;copies[field]=std::move(packed);}
        }
        // Leave the line unchanged unless the conservative byte estimate covers
        // the enclosing key, colon, braces and comma.
        if(savings<=sizeof(hidHideSnapshotTargetCopiesField)+5)return std::nullopt;
        auto wire=detail;
        for(auto field=copies.begin();field!=copies.end();++field)wire.erase(field.key());
        wire.emplace(hidHideSnapshotTargetCopiesField,std::move(copies));
        return wire.dump();
    }catch(...){return std::nullopt;}
}
}
