#pragma once
#include <array>
#include <cstring>
#include "json.hpp"

namespace ymcc::logging {
inline constexpr char oemKeyCatalogField[] = "oemKeyCatalogV1";
inline constexpr std::array oemKeyCatalogColumns{"keyId", "label", "backIndex",
    "triggerCapability", "physicalForm", "captureImplemented"};
inline constexpr std::size_t oemKeyCatalogLimit = 32;

inline void compactOemKeyCatalog(const char* event, nlohmann::json& detail) noexcept {
    if (!event || event[0] != 'o' || std::strcmp(event, "oem-key-usage-query") != 0) return;
    try {
        const auto schema = detail.find("schemaVersion"), keys = detail.find("keys");
        if (schema == detail.end() || !schema->is_number_integer() || *schema != 1 ||
            keys == detail.end() || !keys->is_array() || keys->empty() || keys->size() > oemKeyCatalogLimit ||
            detail.contains(oemKeyCatalogField)) return;

        using Json = nlohmann::json;
        Json packed = Json::array();
        for (const auto& key : *keys) {
            if (!key.is_object() || key.size() != oemKeyCatalogColumns.size()) return;
            std::array<const Json*, oemKeyCatalogColumns.size()> values{};
            for (std::size_t i = 0; i < values.size(); ++i) {
                const auto field = key.find(oemKeyCatalogColumns[i]);
                if (field == key.end()) return;
                values[i] = &*field;
            }
            if (!values[0]->is_string() || !values[1]->is_string() || !values[2]->is_number_integer() ||
                *values[2] < 0 || *values[2] > 255 || !values[3]->is_string() || !values[4]->is_string() ||
                !values[5]->is_boolean()) return;
            packed.push_back(Json::array({*values[0], *values[1], *values[2], *values[3], *values[4], *values[5]}));
        }
        // Keep the original catalog unless the complete encoded copy fits its schema.
        if (detail.emplace(oemKeyCatalogField, std::move(packed)).second) detail.erase(keys);
    } catch (...) {} // Compaction failure must not discard the original receipt.
}
}
