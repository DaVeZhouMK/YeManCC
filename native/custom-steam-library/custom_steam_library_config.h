// Shared policy for the two portable user-config families. No I/O on construction.
#pragma once
#include <array>
#include <limits>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include "json.hpp"
#include "custom_steam_library_paths.h"

namespace custom_steam_library::config {
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline bool library(const fs::path& path) { return path.filename() == L"library-config.json"; }
inline size_t limit(const fs::path& path) { return library(path) ? 4u << 20 : 1u << 20; }
inline bool incompatible(const Json& value) {
    return value.is_object() && value.contains("schemaVersion") &&
        value["schemaVersion"].is_number() && value["schemaVersion"] > 1;
}
inline Json defaults(bool isLibrary) {
    if (!isLibrary) return {{"schemaVersion",1},{"items",Json::object()}};
    return {{"schemaVersion",1},{"enabled",false},{"automaticScrapingEnabled",true},
        {"scanMode","event-driven-no-polling"},
        {"automaticTriggers",{"app-start","steam-launch","library-page-open","directory-change-while-app-running"}},
        {"roots",Json::array()},{"trainerRoots",Json::array()},{"manualGameDirectories",Json::array()},
        {"excludedGameDirectories",Json::array()},{"scanRootsExplicitlyCleared",false},
        {"manualPrimary",Json::object()},{"manualBuckets",Json::object()},
        {"firstRunDefaultScan",{{"status","pending"},{"candidateSearchDepth",2},{"drivePolicy","fixed-only"},{"autoAddToSteam",false}}}};
}
inline bool text(const Json& value) {
    return value.is_string() && value.get_ref<const std::string&>().find('\0') == std::string::npos;
}
inline bool structurallyValid(const Json& value, bool isLibrary) {
    if (!value.is_object() || incompatible(value)) return false;
    if (!isLibrary) return !value.contains("items") || value["items"].is_object();
    for (const auto* key : {"roots","trainerRoots","manualGameDirectories","excludedGameDirectories"})
        if (value.contains(key) && !value[key].is_array()) return false;
    for (const auto* key : {"manualPrimary","manualBuckets","firstRunDefaultScan"})
        if (value.contains(key) && !value[key].is_object()) return false;
    return !value.contains("scanRootsExplicitlyCleared") || value["scanRootsExplicitlyCleared"].is_boolean();
}
inline Json normalize(Json value, bool isLibrary, bool* repaired = nullptr) {
    bool bad = false;
    const auto fallback = defaults(isLibrary);
    if (!value.is_object() || incompatible(value)) { if (repaired) *repaired = true; return fallback; }
    auto field = [&](Json& object, const char* key, const Json& replacement, const auto& valid) {
        const auto it = object.find(key);
        if (it == object.end()) object[key] = replacement;
        else if (!valid(*it)) { object[key] = replacement; bad = true; }
    };
    auto strings = [&](Json& object, std::initializer_list<const char*> keys) {
        for (const auto* key : keys) if (object.contains(key) && !text(object[key])) { object.erase(key); bad = true; }
    };
    auto booleans = [&](Json& object, std::initializer_list<const char*> keys) {
        for (const auto* key : keys) if (object.contains(key) && !object[key].is_boolean()) { object.erase(key); bad = true; }
    };
    if (value.contains("schemaVersion") && (!value["schemaVersion"].is_number_integer() || value["schemaVersion"] != 1)) bad = true;
    value["schemaVersion"] = 1;
    if (isLibrary) {
        for (const auto* key : {"enabled","automaticScrapingEnabled","scanRootsExplicitlyCleared"})
            field(value,key,fallback[key],[](const Json& x){return x.is_boolean();});
        for (const auto* key : {"roots","trainerRoots","manualGameDirectories","excludedGameDirectories"}) {
            field(value,key,Json::array(),[](const Json& x){return x.is_array();});
            Json list = Json::array();
            for (auto entry : value[key]) {
                if (text(entry)) { list.push_back(std::move(entry)); continue; }
                if ((std::string(key)=="manualGameDirectories" || std::string(key)=="excludedGameDirectories") && entry.is_object()) {
                    if (entry.contains("path") && text(entry["path"])) { list.push_back(std::move(entry)); continue; }
                }
                bad = true;
            }
            value[key] = std::move(list);
        }
        for (const auto* key : {"manualPrimary","manualBuckets"}) {
            field(value,key,Json::object(),[](const Json& x){return x.is_object();});
            for (auto& [_, entry] : value[key].items()) {
                if (!entry.is_object()) { entry = Json::object(); bad = true; }
                strings(entry,{"exe","path","bucket","name","source"});
            }
        }
        field(value,"firstRunDefaultScan",fallback["firstRunDefaultScan"],[](const Json& x){return x.is_object();});
        auto& first = value["firstRunDefaultScan"];
        field(first,"status","pending",[](const Json& x){return text(x) && (x=="pending" || x=="running" || x=="completed" || x=="skipped-existing-roots");});
        field(first,"candidateSearchDepth",2,[](const Json& x){return x.is_number_integer() && x>=0 && x<=16;});
        field(first,"drivePolicy","fixed-only",[](const Json& x){return text(x);});
        field(first,"autoAddToSteam",false,[](const Json& x){return x.is_boolean() && x==false;});
        value["scanMode"] = fallback["scanMode"];
        value["automaticTriggers"] = fallback["automaticTriggers"];
    } else {
        field(value,"items",Json::object(),[](const Json& x){return x.is_object();});
        for (auto& [_, entry] : value["items"].items()) {
            if (!entry.is_object()) { entry = Json::object(); bad = true; }
            bool brokenIdentity = false;
            for (const auto* key : {"steamId","storefrontSteamId","igdbId"}) {
                if (!entry.contains(key)) continue;
                const auto& id=entry[key];
                const bool valid=id.is_null() ||
                    (id.is_number_integer() && id>=0 && id<=((std::string(key)=="igdbId")?(std::numeric_limits<int64_t>::max)():(std::numeric_limits<int>::max)())) ||
                    (text(id) && !id.get_ref<const std::string&>().empty() && id.get_ref<const std::string&>().find_first_not_of("0123456789")==std::string::npos);
                if (!valid) { entry.erase(key); bad=true; brokenIdentity=true; }
            }
            strings(entry,{"name","formalName","source","identityStatus","automaticIdentityLastStatus","lastError"});
            if(entry.contains("idCleared") && !entry["idCleared"].is_boolean()) { entry["idCleared"]=true; brokenIdentity=true; bad=true; }
            booleans(entry,{"idCleared","retryable","automaticIdentityAttempted"});
            if (brokenIdentity) for (const auto* key : {"identityStatus","verified","steamOwnership","downloadConfirmed","committed"}) entry.erase(key);
            for (const auto* key : {"automaticIdentityRetryAfterAt","automaticIdentityAttemptedAt","automaticIdentityCompletedAt","automaticIdentityAttempts"})
                if (entry.contains(key) && (!entry[key].is_number_integer() || entry[key]<0 || entry[key]>(std::numeric_limits<int64_t>::max)())) { entry.erase(key); bad=true; }
            for (const auto* key : {"cover","long","wallpaper","artworkProtection"}) {
                if (!entry.contains(key)) continue;
                if (!entry[key].is_object()) { entry.erase(key); bad=true; continue; }
                strings(entry[key],{"file","portableFile","source","provider","cover","long","wallpaper"});
            }
        }
    }
    if (repaired) *repaired = bad;
    return value;
}
// Fill only structurally damaged fields from a recovery document. Valid siblings
// and extension fields in the primary remain the user's most recent data.
inline Json recover(const Json& primary, const Json& backup, bool isLibrary) {
    if (!primary.is_object()) return normalize(backup,isLibrary);
    auto merged = backup;
    for(const auto& [key,value]:primary.items())merged[key]=value;
    const auto& fields = isLibrary ? defaults(true) : defaults(false);
    for (const auto& [key, expected] : fields.items()) {
        if (!primary.contains(key)) { if (backup.contains(key)) merged[key]=backup[key]; continue; }
        const auto& actual=primary[key];
        if ((expected.is_object() && !actual.is_object()) || (expected.is_array() && !actual.is_array()) ||
            (expected.is_boolean() && !actual.is_boolean())) if (backup.contains(key)) merged[key]=backup[key];
    }
    return normalize(std::move(merged),isLibrary);
}
inline Json delta(const Json& current, const Json& baseline, const Json& submitted) {
    if (baseline == submitted) return current;
    if (baseline.is_object() && submitted.is_object() && current.is_object()) {
        auto result=current;
        for (const auto& [key,value] : baseline.items()) if (!submitted.contains(key)) {
            if (current.contains(key) && current[key]!=value) throw std::runtime_error("Configuration field changed during deletion; retry");
            result.erase(key);
        }
        for (const auto& [key,value] : submitted.items()) {
            if (!baseline.contains(key)) {
                if (current.contains(key) && current[key]!=value) throw std::runtime_error("Configuration field was added concurrently; retry");
                result[key]=value;
            } else if (baseline[key]!=value) result[key]=delta(current.contains(key)?current[key]:Json(nullptr),baseline[key],value);
        }
        return result;
    }
    if (current!=baseline && current!=submitted) throw std::runtime_error("Configuration field changed during submission; retry");
    return submitted;
}
struct Read {
    bool exists=false, oversized=false, unreadable=false;
    std::string bytes;
    BY_HANDLE_FILE_INFORMATION info{};
    std::optional<Json> document;
};
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct ReadError : std::runtime_error { using std::runtime_error::runtime_error; };
inline Read readBytes(const fs::path& path, size_t maximum=0) {
    Read result;
    Handle file{CreateFileW(ioPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND) return result;
        throw ReadError("Cannot read user configuration (system error "+std::to_string(error)+")");
    }
    result.exists=true;
    if(!GetFileInformationByHandle(file.value,&result.info) || (result.info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
        throw ReadError("Cannot inspect user configuration file");
    const uint64_t size=(uint64_t(result.info.nFileSizeHigh)<<32)|result.info.nFileSizeLow;
    if(size>(maximum?maximum:limit(path))) {result.oversized=true;return result;}
    result.bytes.resize(static_cast<size_t>(size));size_t offset=0;
    while(offset<result.bytes.size()) {DWORD count=0;if(!ReadFile(file.value,result.bytes.data()+offset,static_cast<DWORD>(result.bytes.size()-offset),&count,nullptr)||!count)throw ReadError("Incomplete configuration read");offset+=count;}
    return result;
}
inline Read read(const fs::path& path, size_t maximum=0) {
    auto result=readBytes(path,maximum);
    if(!result.exists||result.oversized)return result;
    struct TooDeep {};
    try {
        result.document=Json::parse(result.bytes,[](int depth,Json::parse_event_t,Json&){if(depth>64)throw TooDeep{};return true;});
    } catch(const Json::parse_error&) {} catch(const TooDeep&) {}
    return result;
}
inline Read readRuntime(const fs::path& path,size_t maximum=0) {
    try{return read(path,maximum);}catch(const ReadError&){Read result;result.unreadable=true;return result;}
}
inline bool same(const Read& a,const Read& b) {
    if(a.unreadable!=b.unreadable || a.exists!=b.exists || a.oversized!=b.oversized) return false;
    if(!a.oversized) return a.bytes==b.bytes;
    return a.info.nFileIndexHigh==b.info.nFileIndexHigh && a.info.nFileIndexLow==b.info.nFileIndexLow &&
        a.info.nFileSizeHigh==b.info.nFileSizeHigh && a.info.nFileSizeLow==b.info.nFileSizeLow &&
        CompareFileTime(&a.info.ftLastWriteTime,&b.info.ftLastWriteTime)==0;
}
struct Observation { fs::path path; Read source; Json view; };
// Bounded to these two families, per caller thread; no cross-root cache authority.
inline thread_local std::array<std::optional<Observation>,2> observations;
inline const Observation* observed(const fs::path& path) {
    const auto& item=observations[library(path)?0:1];
    return item && item->path==path.lexically_normal() ? &*item : nullptr;
}
inline Json remember(const fs::path& path, Read source, Json view) {
    observations[library(path)?0:1]=Observation{path.lexically_normal(),std::move(source),view};
    return view;
}
inline Json prepare(const fs::path& path,const Read& source,const Json& submitted,const Json* baseline=nullptr) {
    if(!submitted.is_object())throw std::runtime_error("Submitted configuration must be an object");
    if(incompatible(submitted) || (source.document && incompatible(*source.document)))
        throw std::runtime_error("Newer configuration schema is read-only in this version; original preserved");
    const auto* observation=observed(path);
    auto latest=source.document?normalize(*source.document,library(path)):defaults(library(path));
    if(observation && same(source,observation->source) && (!source.document || !structurallyValid(*source.document,library(path)))) latest=observation->view;
    if(observation && !same(source,observation->source) && (!source.document || !structurallyValid(*source.document,library(path))))
        throw std::runtime_error("Configuration became missing/corrupt during editing; retry");
    auto value=normalize(submitted,library(path));
    if(baseline) value=delta(latest,*baseline,value);
    else if(observation) value=delta(latest,observation->view,value);
    const auto text=value.dump(2)+'\n';
    if(text.size()>limit(path)) throw std::runtime_error("Configuration exceeds its read size limit");
    return value;
}
// Copy oversized originals with bounded memory and checked durable output. The
// main file is still untouched if preservation fails, including access denial.
inline void archive(const fs::path& source,const fs::path& destination) {
    fs::create_directories(ioPath(destination.parent_path()));
    Handle input{CreateFileW(ioPath(source).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(input.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot preserve original configuration");
    const auto stage=fs::path(destination.wstring()+L".preserve-"+std::to_wstring(GetCurrentProcessId()));
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove(ioPath(path),error);}} cleanup{stage};
    {
        Handle output{CreateFileW(ioPath(stage).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
        if(output.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create original configuration archive");
        std::array<char,64u<<10> buffer{};
        for(;;) {
            DWORD count=0;
            if(!ReadFile(input.value,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr))throw std::runtime_error("Cannot read original configuration archive");
            if(!count)break;
            DWORD offset=0;
            while(offset<count) {
                DWORD wrote=0;
                if(!WriteFile(output.value,buffer.data()+offset,count-offset,&wrote,nullptr)||!wrote)throw std::runtime_error("Cannot write original configuration archive");
                offset+=wrote;
            }
        }
        if(!FlushFileBuffers(output.value))throw std::runtime_error("Cannot flush original configuration archive");
    }
    if(!MoveFileExW(ioPath(stage).c_str(),ioPath(destination).c_str(),MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot activate original configuration archive");
}
}
