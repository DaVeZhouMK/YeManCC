#pragma once
#include "json.hpp"
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>

namespace ymcc::bootPreferences {
using Json = nlohmann::json;
enum class WriteOrigin { Ordinary, BootMode, Runtime };
inline WriteOrigin readOrigin(const Json& args, WriteOrigin fallback = WriteOrigin::Ordinary) {
    if (!args.is_object()) throw std::invalid_argument("Invalid preference request");
    const auto origin=args.find("origin");
    if (origin==args.end()) return fallback;
    if (*origin=="ordinary") return WriteOrigin::Ordinary;
    if (*origin=="boot-mode") return WriteOrigin::BootMode;
    if (*origin=="runtime") return WriteOrigin::Runtime;
    throw std::invalid_argument("Invalid preference origin");
}
inline bool rememberValue(Json& target, const Json& next) {
    if (target==next) return false;
    target=next;return true;
}
inline bool safeKey(const std::string& key) {
    return key != "__proto__" && key != "constructor" && key != "prototype";
}
inline bool delta(Json& target, const Json& before, const Json& next) {
    if (before == next) return false;
    if (before.is_object() && next.is_object()) {
        bool changed = false;
        if (!target.is_object()) target = Json::object();
        for (auto it = before.begin(); it != before.end(); ++it) {
            if (safeKey(it.key()) && !next.contains(it.key())) changed = target.erase(it.key()) > 0 || changed;
        }
        for (auto it = next.begin(); it != next.end(); ++it) {
            if (!safeKey(it.key())) continue;
            const auto prior = before.find(it.key());
            if (prior != before.end() && *prior == *it) continue;
            auto existing = target.find(it.key());
            Json value = existing == target.end() ? Json() : *existing;
            if (delta(value, prior == before.end() ? Json() : *prior, *it)) {
                target[it.key()] = std::move(value); changed = true;
            }
        }
        return changed;
    }
    if (target == next) return false;
    target = next;
    return true;
}
/** Called inside the original settings lock, only for explicit ordinary saves.
 * Runtime/startup restoration never recaptures its effective scene as global. */
inline bool rememberSection(Json& document, const std::string& section, const Json& next, const Json& intent = Json()) {
    if (section == "sleep") return true; // Shared sleep settings never depend on a mode document.
    auto extensions = document.find("extensions");
    if (extensions == document.end() || !extensions->is_object()) return true;
    auto mode = extensions->find("bootModes");
    if (mode == extensions->end() || !mode->is_object() || mode->value("schema", Json()) != 1) return true;
    if (!mode->contains("global") || !(*mode)["global"].is_object()) return false;
    const Json before = document.value(section, Json::object());
    Json global = (*mode)["global"];
    bool changed = false;
    if (section == "screenTouchpads") {
        const auto oldProfiles = before.value("profiles", Json::object());
        const auto profiles = next.value("profiles", Json::object());
        if (intent.is_object()) {
            for (auto persona=intent.begin();persona!=intent.end();++persona) {
                if (!safeKey(persona.key()) || !persona->is_object()) continue;
                const auto prior=oldProfiles.value(persona.key(),Json::object()), saved=profiles.value(persona.key(),Json::object());
                for (auto field=persona->begin();field!=persona->end();++field) {
                    if (!safeKey(field.key()) || !saved.contains(field.key())) continue;
                    const auto old=prior.value(field.key(),Json());
                    changed=rememberValue(global["touch"]["profiles"][persona.key()][field.key()],saved[field.key()]) || changed;
                }
            }
        } else if (oldProfiles != profiles) changed = delta(global["touch"]["profiles"], oldProfiles, profiles);
    } else if (section=="extensions") {
        const auto old=before.value("deckySidebar",Json::object()), value=next.value("deckySidebar",Json::object());
        const bool explicitEnabled=intent.is_object() && intent.contains("deckySidebar") && intent["deckySidebar"].is_object() && intent["deckySidebar"].contains("enabled");
        if (value.contains("enabled") && (explicitEnabled || !intent.is_object() && old.value("enabled",Json())!=value["enabled"]))
            changed=rememberValue(global["steamDecky"]["enabled"],value["enabled"]);
    } else return true;
    if (!changed) return true;
    const auto revision = mode->find("revision");
    if (revision == mode->end() || !revision->is_number_integer() || *revision < 0 || *revision >= 9007199254740991LL) return false;
    (*mode)["global"] = std::move(global);
    (*mode)["revision"] = revision->get<long long>() + 1;
    return true;
}
/** Remember an acknowledged ordinary charge preference in the original lock. */
inline bool rememberCharge(Json& document, bool enabled, int limit) {
    auto extensions=document.find("extensions");
    if(extensions==document.end() || !extensions->is_object() || !extensions->contains("bootModes"))return true;
    auto& mode=(*extensions)["bootModes"];
    if(!mode.is_object() || mode.value("schema",Json())!=1 || !mode.contains("global") || !mode["global"].is_object() || limit<0 || limit>100)return false;
    auto global=mode["global"];
    auto charge=global.value("charge",Json::object());
    if(!charge.is_object())return false;
    charge["enabled"]=enabled;charge["limit"]=limit;
    if(global.value("charge",Json())==charge)return true;
    const auto revision=mode.find("revision");
    if(revision==mode.end() || !revision->is_number_integer() || *revision<0 || *revision>=9007199254740991LL)return false;
    global["charge"]=std::move(charge);mode["global"]=std::move(global);mode["revision"]=revision->get<long long>()+1;
    return true;
}
/** Stage one known patch from the document held by the existing file lock. */
inline bool rememberDisplay(Json& document, const Json& patch) {
    if(!patch.is_object() || patch.empty())return false;
    for(const auto& [key,value]:patch.items()) {
        if(key=="brightness") {if(!value.is_number_integer() || value<0 || value>100)return false;}
        else if(key=="resolution" || key=="hz") {if(!value.is_string() || value.get_ref<const std::string&>().empty() || value.get_ref<const std::string&>().size()>32)return false;}
        else return false;
    }
    auto extensions=document.find("extensions");
    if(extensions==document.end())return true;
    if(!extensions->is_object())return false;
    if(!extensions->contains("bootModes"))return true;
    auto& mode=(*extensions)["bootModes"];
    if(!mode.is_object() || mode.value("schema",Json())!=1 || !mode.contains("global") || !mode["global"].is_object())return false;
    auto global=mode["global"],display=global.value("display",Json::object());
    if(!display.is_object())return false;
    display.update(patch);
    if(global.value("display",Json())==display)return true;
    const auto revision=mode.find("revision");
    if(revision==mode.end() || !revision->is_number_integer() || *revision<0 || *revision>=9007199254740991LL)return false;
    global["display"]=std::move(display);mode["global"]=std::move(global);mode["revision"]=revision->get<long long>()+1;
    return true;
}
/** Stage one known patch from the document held by the existing file lock. */
inline bool prepareSection(Json& prepared, Json& document, const std::string& section, const Json& patch, WriteOrigin origin) {
    const auto prior=document.find(section);
    if (!patch.is_object() || (prior!=document.end() && !prior->is_object())) return false;
    prepared=prior==document.end()?Json::object():*prior;
    prepared.update(patch);
    if (origin!=WriteOrigin::Ordinary) return true;
    if (!rememberSection(document,section,prepared,patch)) return false;
    // extensions owns bootModes itself. Carry the same-transaction projection
    // into the prepared section instead of restoring its earlier copy.
    if (section=="extensions" && document["extensions"].contains("bootModes"))
        prepared["bootModes"]=document["extensions"]["bootModes"];
    return true;
}
}
