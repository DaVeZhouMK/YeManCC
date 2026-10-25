#pragma once
#include "oem_gamepad_mode_contract.h"
#include "json.hpp"
#include <limits>
#include <stdexcept>

namespace ymcc::oem::gamepad {
using JournalJson=nlohmann::json;
inline JournalJson encodeRecord(const Record& r) {
    auto value=[](const Value& v){return JournalJson{{"exists",v.exists},{"type",v.type},{"bytes",v.bytes}};};
    JournalJson entries=JournalJson::array();
    for(const auto& e:r.entries) {
        // The current contract intentionally uses ASCII registry addresses.
        const auto ascii=[](const std::wstring& w) {
            std::string result;for(wchar_t c:w) {
                if(c<32 || c>126) throw std::runtime_error("non-ASCII registry address");
                result.push_back(static_cast<char>(c));
            }
            return result;
        };
        const auto key=ascii(e.patch.address.subkey),name=ascii(e.patch.address.name);
        entries.push_back({{"view",int(e.patch.address.view)},{"subkey",key},{"name",name},
            {"desired",value(e.patch.desired)},{"original",value(e.original)},
            {"mayHaveWritten",e.mayHaveWritten}});
    }
    return {{"schema",r.schema},{"vendor",int(r.vendor)},{"intent",int(r.intent)},
        {"binding",r.binding},{"pending",r.pending},
        {"owner",{{"processNonce",r.owner.processNonce},{"session",r.owner.session},
                  {"hostEpoch",r.owner.hostEpoch},{"powerGeneration",r.owner.powerGeneration},
                  {"intentRevision",r.owner.intentRevision}}},{"entries",entries}};
}
inline JournalRead decodeRecord(std::string_view text) {
    JournalRead out;
    if(text.size()>65536 || text.empty()) return out;
    try {
        // Reject duplicate field names instead of accepting last-value-wins.
        std::vector<std::vector<std::string>> keys;
        auto callback=[&](int depth,JournalJson::parse_event_t event,JournalJson& parsed) {
            if(depth>8) throw std::runtime_error("record too deeply nested");
            if(event==JournalJson::parse_event_t::object_start) keys.emplace_back();
            if(event==JournalJson::parse_event_t::key) {
                const auto key=parsed.get<std::string>();
                for(const auto& prior:keys.back()) if(prior==key) throw std::runtime_error("duplicate key");
                keys.back().push_back(key);
            }
            if(event==JournalJson::parse_event_t::object_end) keys.pop_back();
            return true;
        };
        const auto j=JournalJson::parse(text.begin(),text.end(),callback);
        const auto number=[](const JournalJson& v) -> uint64_t {
            if(!v.is_number_unsigned() && (!v.is_number_integer() || v.get<int64_t>()<0))
                throw std::runtime_error("not unsigned integer");
            return v.get<uint64_t>();
        };
        const auto str=[](const JournalJson& v,size_t max)->std::string {
            if(!v.is_string()) throw std::runtime_error("not string");
            auto s=v.get<std::string>();if(s.size()>max) throw std::runtime_error("string too long");return s;
        };
        const auto boolean=[](const JournalJson& v)->bool {
            if(!v.is_boolean()) throw std::runtime_error("not boolean");return v.get<bool>();
        };
        const auto val=[&](const JournalJson& v)->Value {
            if(!v.is_object() || v.size()!=3) throw std::runtime_error("invalid value");
            Value r;r.exists=boolean(v.at("exists"));const auto type=number(v.at("type"));
            if(type>std::numeric_limits<uint32_t>::max()) throw std::runtime_error("invalid type");
            r.type=uint32_t(type);const auto& bytes=v.at("bytes");
            if(!bytes.is_array() || bytes.size()>1024) throw std::runtime_error("invalid bytes");
            for(const auto& byte:bytes) {const auto n=number(byte);if(n>255) throw std::runtime_error("invalid byte");r.bytes.push_back(uint8_t(n));}
            return r;
        };
        if(!j.is_object() || j.size()!=7) return out;
        Record r;
        if(number(j.at("schema"))!=contractVersion) return out;
        r.schema=contractVersion;
        const auto vendor=number(j.at("vendor")),intent=number(j.at("intent"));
        if(vendor>uint64_t(Vendor::Lenovo) || intent>uint64_t(Intent::VirtualCapture)) return out;
        r.vendor=Vendor(vendor);r.intent=Intent(intent);r.binding=str(j.at("binding"),512);
        r.pending=boolean(j.at("pending"));const auto& o=j.at("owner");
        if(!o.is_object() || o.size()!=5) return out;
        r.owner={str(o.at("processNonce"),64),number(o.at("session")),number(o.at("hostEpoch")),
            number(o.at("powerGeneration")),number(o.at("intentRevision"))};
        if(!validOwner(r.owner)) return out;
        const auto& entries=j.at("entries");if(!entries.is_array() || entries.size()>16 || entries.empty()) return out;
        for(const auto& e:entries) {
            if(!e.is_object() || e.size()!=6) return out;
            const auto view=number(e.at("view"));if(view>uint64_t(RegistryView::View64)) return out;
            const auto key=str(e.at("subkey"),256),name=str(e.at("name"),128);
            for(char c:key+name) if(uint8_t(c)<32 || uint8_t(c)>126) return out;
            Entry entry;entry.patch.address={RegistryView(view),std::wstring(key.begin(),key.end()),std::wstring(name.begin(),name.end())};
            entry.patch.desired=val(e.at("desired"));entry.original=val(e.at("original"));
            entry.mayHaveWritten=boolean(e.at("mayHaveWritten"));r.entries.push_back(std::move(entry));
        }
        out.state=JournalState::Record;out.record=std::move(r);
    } catch(...) {out.state=JournalState::Invalid;}
    return out;
}
} // namespace ymcc::oem::gamepad




