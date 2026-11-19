#pragma once
#include "settings_baseline.h"
#include "update_config_state.h"

// This runs once at an admitted startup, including older-installer and manual
// upgrades. Ordinary writers retain their schema/concurrent-write protections.
namespace ymcc::settingsUpgrade {
using Json = nlohmann::json;
inline constexpr std::size_t kMaximum = 4u * 1024u * 1024u;
inline Json document(const updateconfig::Image& image) {
    if (image.kind == updateconfig::Kind::missing || image.kind == updateconfig::Kind::unavailable ||
        image.info.nFileSizeHigh || image.info.nFileSizeLow > kMaximum) return Json();
    // Match decoded object keys too: duplicate/escaped duplicate fields cannot
    // choose an arbitrary old schema or armed startup value. Bound recursion
    // before constructing a document, like the existing runtime parser.
    struct Invalid {};
    try {
        std::array<std::set<std::string>,65> keys;
        return Json::parse(updateconfig::decode(image.raw),[&](int depth,Json::parse_event_t event,Json& value){
            if(depth<0 || depth>64)throw Invalid{};
            if(event==Json::parse_event_t::object_start)keys[depth].clear();
            if(event==Json::parse_event_t::key &&
                (depth==0 || !keys[depth-1].insert(value.get<std::string>()).second))throw Invalid{};
            return true;
        },false);
    } catch (...) { return Json(); }
}
inline updateconfig::Image readImage(const std::wstring& path, std::size_t maximum) {
    auto image=updateconfig::read(path,maximum);
    if(image.kind!=updateconfig::Kind::missing && image.kind!=updateconfig::Kind::unavailable) {
        // Keep the common handle/path/size guards, but consume this document
        // using the settings contract (64 levels; schemaVersion/baseline).
        // An unrelated legacy root "schema" is not updater-state authority.
        image.value=document(image);
        image.kind=image.value.is_object()?updateconfig::Kind::ready:updateconfig::Kind::damaged;
    }
    return image;
}
inline bool validDefaults(const Json& value) {
    if (!settingsBaseline::supported(value) || !value.contains("schemaVersion") || value["schemaVersion"] != 1 ||
        !value.contains("configurationBaseline")) return false;
    for (const auto* key : {"ui","background","music","gamepad","performanceSchedule","gameCustom",
        "tdp","rtss","cpu","sleep","quickApps","startupDesired","power","tray","autoclose","fan","input","extensions"})
        if (!value.contains(key) || !value[key].is_object()) return false;
    return true;
}
inline bool copyArchive(const std::wstring& from, const std::wstring& to) {
    return CopyFileW(from.c_str(),to.c_str(),TRUE) != FALSE;
}
inline bool compatibleShape(const Json& saved, const Json& defaults, unsigned depth = 0) {
    if (depth > 64) return false;
    if (defaults.is_null()) return true; // Nullable extension/receipt payload.
    if (defaults.is_object()) {
        if (!saved.is_object()) return false;
        for (auto it=defaults.begin();it!=defaults.end();++it) {
            if (depth==0 && (it.key()=="schemaVersion" || it.key()=="configurationBaseline")) continue;
            const auto found=saved.find(it.key());
            if (found!=saved.end() && !compatibleShape(*found,it.value(),depth+1)) return false;
        }
        return true; // Missing keys and unknown extensions remain compatible.
    }
    if (defaults.is_number()) return saved.is_number();
    return defaults.type() == saved.type();
}
inline bool runtimeReadable(const updateconfig::Image& image, const Json& decoded) {
    // Ordinary native/renderer settings readers consume UTF-8 JSON directly.
    // A legacy UTF-16 document is migratable, not already ready for consumers.
    const auto value=settingsBaseline::parseDocument(image.raw);
    return value.is_object() && value==decoded;
}
template<class Writer>
inline Json prepare(bool upgradeLaunch, const std::wstring& primaryPath,
    const std::wstring& defaultsPath, Writer writer,
    bool (*archiveCopy)(const std::wstring&,const std::wstring&) = copyArchive) {
    if (!upgradeLaunch) return {{"status","not-installing"}};
    const auto backupPath = primaryPath + L".bak";
    const auto primary = readImage(primaryPath,kMaximum), backup = readImage(backupPath,kMaximum);
    const auto left = document(primary), right = document(backup);
    if (primary.kind == updateconfig::Kind::missing && backup.kind == updateconfig::Kind::missing)
        return {{"status","fresh"}};

    // Validate shipped defaults BEFORE altering any old entry. Old-data
    // tolerance cannot authorize incomplete/unsupported replacement resources.
    const auto defaults = document(readImage(defaultsPath,kMaximum));
    if (!validDefaults(defaults)) throw std::runtime_error("UPDATE_SETTINGS_DEFAULTS_INVALID");
    const bool leftGood = settingsBaseline::supported(left) && compatibleShape(left,defaults);
    const bool rightGood = settingsBaseline::supported(right) && compatibleShape(right,defaults);
    if (leftGood && runtimeReadable(primary,left) &&
        ((rightGood && runtimeReadable(backup,right)) || backup.kind == updateconfig::Kind::missing))
        return {{"status","compatible"}};
    auto next = leftGood ? left : rightGood ? right : defaults;
    next["schemaVersion"] = 1;
    next["configurationBaseline"] = defaults["configurationBaseline"];
    settingsBaseline::preserveNativeCleanupHistory(next,left,right,true);
    bool discardedForSize=false;
    // Recovery stamps can push an otherwise readable old document over the
    // runtime limit. Prefer the shipped baseline before touching either file.
    if(next.dump().size()>kMaximum) {
        next=defaults;discardedForSize=true;
        settingsBaseline::preserveNativeCleanupHistory(next,left,right,true);
    }
    if (!leftGood || discardedForSize) {
        settingsBaseline::deactivateRecoveredState(next);
        // A compatible backup may still contain an armed boot scenario. After
        // damaged-primary recovery it cannot override the safe-stop defaults.
        auto& extensions=settingsBaseline::recoveryObject(next,"extensions");
        for(const auto* key:{"bootModes","bootModesAppliedBoot","bootModesActiveBoot","bootModesStartupResult"})
            extensions.erase(key);
    }

    // Archival is best-effort under the user's new-data-first upgrade policy.
    // Never follow a leaf reparse point or recursively remove an old directory.
    unsigned archived = 0;
    const auto archive = [&](const std::wstring& path, const updateconfig::Image& before) {
        if (before.kind == updateconfig::Kind::missing || before.kind == updateconfig::Kind::unavailable ||
            before.info.nFileSizeHigh || before.info.nFileSizeLow > kMaximum) return;
        try {
            if (updateconfig::sameImage(before,readImage(path,kMaximum)) &&
                archiveCopy(path,path+L".before-upgrade-"+updateconfig::suffix())) ++archived;
        } catch (...) {} // Best-effort diagnostics cannot veto the new document.
    };
    // Validate the final serialized candidate before any archival/path change.
    if(next.dump().size()>kMaximum || !settingsBaseline::supported(settingsBaseline::parseDocument(next.dump())))
        throw std::runtime_error("UPDATE_SETTINGS_DEFAULTS_INVALID");
    if (!updateconfig::sameImage(primary,readImage(primaryPath,kMaximum)) ||
        !updateconfig::sameImage(backup,readImage(backupPath,kMaximum)))
        throw std::runtime_error("UPDATE_SETTINGS_CHANGED");
    archive(primaryPath,primary); archive(backupPath,backup);
    const auto valid = [](const Json& value) { return settingsBaseline::supported(value); };
    // Each file is atomic and read back; a partial pair remains recognizable
    // on the next admitted launch. Never restore the incompatible old primary.
    updateconfig::save(primaryPath,next,kMaximum,valid,writer,false,readImage);
    updateconfig::save(backupPath,next,kMaximum,valid,writer,false,readImage);
    return {{"status","repaired"},{"source",discardedForSize?"defaults-size-limit":leftGood?"primary":rightGood?"backup":"defaults"},
        {"archived",archived},{"automaticHardwareDeactivated",!leftGood||discardedForSize}};
}
}
