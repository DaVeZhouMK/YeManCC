#pragma once
#include "json.hpp"
#include "steamdeck_mouse_vdf.h"
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <map>
#include <set>
#include <string_view>

// Bytes in, bounded stored-layout evidence out. No resolution, IO, Steam, input,
// editor/cache load, or active-preset inference belongs in this header.
namespace ymcc::steamdeck::bindingevidence {
using Json = nlohmann::json;
inline constexpr std::size_t matchLimit = 32, groupLimit = 64, presetLimit = 16, referenceLimit = 64;
namespace detail {
inline bool space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return s;
}
inline bool number(std::string_view s, std::uint32_t& value) {
    if (s.empty() || s.size()>10) return false;
    std::uint64_t n=0;
    for (const char c:s) {
        if (c<'0' || c>'9') return false;
        n=n*10+static_cast<unsigned>(c-'0');
        if (n>UINT32_MAX) return false;
    }
    value=static_cast<std::uint32_t>(n); return true;
}
inline const Node* field(const std::vector<Node>& nodes,const char* key,std::size_t& count) {
    const Node* found=nullptr; count=0;
    for (const auto& n:nodes) if (n.key==key) { found=&n; ++count; }
    return found;
}
inline const char* known(std::string_view s,std::initializer_list<const char*> names) {
    for (const char* n:names) if (s==n) return n;
    return "unknown";
}
inline const char* inputName(std::string_view s) {
    return known(s,{"left_bumper","right_bumper","button_a","button_b","button_x","button_y",
        "button_escape","button_menu","button_back_left","button_back_right","button_back_left_upper",
        "button_back_right_upper","button_back_left_lower","button_back_right_lower","click","touch",
        "position","edge","up","down","left","right","trigger","soft_pull","full_pull"});
}
inline const char* activatorName(std::string_view s) {
    return known(s,{"Full_Press","Soft_Press","Long_Press","Double_Press","Release_Press",
        "Start_Press","Chorded_Press","Analog"});
}
inline const char* sourceName(std::string_view s) {
    return known(s,{"switch","button_diamond","left_trackpad","right_trackpad","joystick",
        "right_joystick","left_joystick","left_trigger","right_trigger","dpad","gyro",
        "left_grip","right_grip","touchscreen"});
}
// Exactly one literal key token. Labels, comma lists, sequences, escapes yielding
// additional tokens, and macros are not interpreted or exported.
inline const char* modifier(std::string_view binding,bool& nonModifierKey) {
    nonModifierKey=false; binding=trim(binding);
    constexpr std::string_view command="key_press";
    if (binding.size()<=command.size() || binding.substr(0,command.size())!=command ||
        !space(binding[command.size()])) return nullptr;
    auto key=trim(binding.substr(command.size()));
    if (key.empty() || key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789")!=std::string_view::npos)
        return nullptr;
    if (key=="LEFT_CONTROL") return "leftCtrl";
    if (key=="RIGHT_CONTROL") return "rightCtrl";
    if (key=="LEFT_ALT") return "leftAlt";
    if (key=="RIGHT_ALT") return "rightAlt";
    nonModifierKey=(key.size()==1) || std::string_view(known(key,{"RETURN","ESCAPE","SPACE","TAB",
        "BACKSPACE","DELETE","INSERT","HOME","END","PAGEUP","PAGEDOWN","UP","DOWN","LEFT","RIGHT",
        "LEFT_SHIFT","RIGHT_SHIFT","LEFT_WINDOWS","RIGHT_WINDOWS"}))!="unknown";
    return nullptr;
}
struct Entry { const Node* node; std::uint32_t id; };
} // namespace detail

// Stable integration contract, schema 1. Names are fixed whitelists, IDs are
// uint32, and raw binding/configuration strings never enter the JSON result.
inline Json summarize(const std::string& layoutText) {
    Json out={{"schema",1},{"implementation","steam-desktop-binding-evidence-v1"},
        {"ok",false},{"status","invalid-vdf"},{"controllerType","unknown"},{"storedLayoutOnly",true},
        {"active",false},{"activePresetKnown",false},{"producer",false},{"absenceProven",false},
        {"coverageComplete",false},{"coverageScope","stored-literal-ctrl-alt-only"},{"modeShiftPresent",false},{"inputBytes",layoutText.size()},
        {"limits",{{"matches",matchLimit},{"groups",groupLimit},{"presets",presetLimit},{"presetReferences",referenceLimit}}},
        {"groups",Json::array()},{"presets",Json::array()},{"matches",Json::array()},{"counters",Json::object()}};
    for (const char* k:{"parseFailures","inputTooLarge","ambiguousRoots","unsupportedRoots","unsupportedControllerTypes",
        "unsupportedFormatVersions","groupsSeen","groupsOmitted","presetsSeen","presetsOmitted","bindingsSeen",
        "literalModifierBindings","matchesOmitted","presetReferencesSeen","presetReferencesOmitted",
        "invalidIds","duplicateIdFields","duplicateGroupIds","ambiguousGroups","duplicatePresetIds",
        "ambiguousPresets","ambiguousContainers","malformedStructures","unsupportedInputNames",
        "unsupportedActivatorNames","unsupportedBindingCommands","nonModifierKeyPressBindings",
        "unsupportedBindingFields","unsupportedSourceNames","unsupportedReferenceStates",
        "ambiguousPresetReferences","missingGroupReferences","ambiguousGroupReferences",
        "omittedGroupReferences","modeShiftEntries","unsupportedModeShifts","disabledActivatorEntries"})
        out["counters"][k]=0;
    auto count=[&](const char* k,std::size_t n=1) {
        auto& v=out["counters"][k]; v=v.get<std::size_t>()+n;
    };
    bool complete=true;
    auto unsupported=[&](const char* k,std::size_t n=1) { count(k,n); complete=false; };
    if (layoutText.size()>2*1024*1024) { out["status"]="input-too-large"; count("inputTooLarge"); return out; }
    std::vector<Node> roots;
    try { roots=Vdf(layoutText).parse(); }
    catch (const std::exception&) { count("parseFailures"); return out; } // Never echo parser/input text.
    if (roots.size()!=1 || roots[0].key!="controller_mappings" || !roots[0].object) {
        out["status"]=roots.size()>1?"ambiguous-root":"unsupported-root";
        count(roots.size()>1?"ambiguousRoots":"unsupportedRoots"); return out;
    }
    const auto& body=roots[0].children;
    std::size_t n=0;
    const auto* type=detail::field(body,"controller_type",n);
    if (n!=1 || type->object || type->value!="controller_neptune") {
        out["status"]="unsupported-controller-type"; out["controllerType"]="unsupported";
        count("unsupportedControllerTypes"); return out;
    }
    out["controllerType"]="controller_neptune";
    const auto* version=detail::field(body,"version",n);
    if (n!=1 || version->object || version->value!="3") { out["status"]="unsupported-format-version"; count("unsupportedFormatVersions"); return out; }
    auto container=[&](const std::vector<Node>& nodes,const char* key)->const Node* {
        std::size_t size=0; const auto* found=detail::field(nodes,key,size);
        if (size>1) { unsupported("ambiguousContainers"); return nullptr; }
        if (found && !found->object) { unsupported("malformedStructures"); return nullptr; }
        return found;
    };
    auto shifts=[&](const std::vector<Node>& nodes) {
        bool present=false; std::size_t containers=0;
        for (const auto& node:nodes) if (node.key=="group_source_modeshifts") {
            present=true; ++containers; out["modeShiftPresent"]=true;
            if (!node.object) unsupported("malformedStructures");
            else { count("modeShiftEntries",node.children.size());
                if (!node.children.empty()) unsupported("unsupportedModeShifts",node.children.size()); }
        }
        if (containers>1) unsupported("ambiguousContainers");
        return present;
    };
    std::vector<detail::Entry> groups,presets;
    std::map<std::uint32_t,std::size_t> groupCounts,presetCounts;
    for (const auto& node:body) {
        const bool isGroup=node.key=="group",isPreset=node.key=="preset";
        if (!isGroup && !isPreset) continue;
        count(isGroup?"groupsSeen":"presetsSeen");
        if (isPreset && node.object) shifts(node.children); // Includes omitted presets.
        auto omit=[&] { count(isGroup?"groupsOmitted":"presetsOmitted"); complete=false; };
        if (!node.object) { unsupported("malformedStructures"); omit(); continue; }
        std::size_t ids=0; const auto* id=detail::field(node.children,"id",ids);
        std::uint32_t numeric=0;
        if (ids!=1 || id->object || !detail::number(id->value,numeric)) {
            unsupported(ids>1?"duplicateIdFields":"invalidIds"); omit(); continue;
        }
        (isGroup?groups:presets).push_back({&node,numeric});
        ++(isGroup?groupCounts:presetCounts)[numeric];
    }
    for (const auto& c:groupCounts) if (c.second>1) unsupported("duplicateGroupIds");
    for (const auto& c:presetCounts) if (c.second>1) unsupported("duplicatePresetIds");
    std::set<std::uint32_t> storedGroups;
    for (const auto& entry:groups) {
        if (groupCounts[entry.id]>1) { count("groupsOmitted"); count("ambiguousGroups"); continue; }
        if (out["groups"].size()>=groupLimit) { unsupported("groupsOmitted"); continue; }
        storedGroups.insert(entry.id); out["groups"].push_back({{"id",entry.id}});
        const auto* inputs=container(entry.node->children,"inputs");
        if (!inputs) continue;
        for (const auto& input:inputs->children) {
            if (!input.object) { unsupported("malformedStructures"); continue; }
            const char* inputName=detail::inputName(input.key);
            if (std::string_view(inputName)=="unknown") unsupported("unsupportedInputNames");
            for (const auto& d:input.children) if (d.key=="disabled_activators") {
                if (d.object) count("disabledActivatorEntries",d.children.size());
                else unsupported("malformedStructures");
            }
            const auto* activators=container(input.children,"activators");
            if (!activators) { unsupported("malformedStructures"); continue; }
            for (const auto& activator:activators->children) {
                if (!activator.object) { unsupported("malformedStructures"); continue; }
                const char* name=detail::activatorName(activator.key);
                if (std::string_view(name)=="unknown") unsupported("unsupportedActivatorNames");
                const auto* bindings=container(activator.children,"bindings");
                if (!bindings) { unsupported("malformedStructures"); continue; }
                for (const auto& binding:bindings->children) {
                    if (binding.key!="binding" || binding.object) { unsupported("unsupportedBindingFields"); continue; }
                    count("bindingsSeen"); bool nonModifier=false;
                    const char* modifier=detail::modifier(binding.value,nonModifier);
                    if (!modifier) {
                        if (nonModifier) count("nonModifierKeyPressBindings");
                        else unsupported("unsupportedBindingCommands");
                        continue;
                    }
                    count("literalModifierBindings");
                    if (out["matches"].size()>=matchLimit) { unsupported("matchesOmitted"); continue; }
                    out["matches"].push_back({{"groupId",entry.id},{"input",inputName},
                        {"activator",name},{"modifier",modifier}});
                }
            }
        }
    }
    std::size_t references=0;
    for (const auto& entry:presets) {
        if (presetCounts[entry.id]>1) { count("presetsOmitted"); count("ambiguousPresets"); continue; }
        if (out["presets"].size()>=presetLimit) { unsupported("presetsOmitted"); continue; }
        bool modeShift=false;
        for (const auto& f:entry.node->children) if (f.key=="group_source_modeshifts") modeShift=true;
        Json preset={{"id",entry.id},{"modeShiftPresent",modeShift},{"groupSourceBindings",Json::array()}};
        const auto* refs=container(entry.node->children,"group_source_bindings");
        if (refs) {
            std::map<std::uint32_t,std::size_t> refCounts;
            for (const auto& ref:refs->children) { std::uint32_t id=0;
                if (detail::number(ref.key,id)) ++refCounts[id]; }
            for (const auto& ref:refs->children) {
                count("presetReferencesSeen"); std::uint32_t id=0;
                if (!detail::number(ref.key,id) || ref.object) {
                    unsupported("invalidIds"); count("presetReferencesOmitted"); continue;
                }
                if (refCounts[id]>1) { unsupported("ambiguousPresetReferences"); count("presetReferencesOmitted"); continue; }
                if (references>=referenceLimit) { unsupported("presetReferencesOmitted"); continue; }
                const auto raw=detail::trim(ref.value); const auto split=raw.find_first_of(" \t\r\n");
                const char* source=detail::sourceName(raw.substr(0,split));
                const auto tail=split==std::string_view::npos?std::string_view{}:detail::trim(raw.substr(split));
                const char* state=detail::known(tail,{"active","inactive"});
                if (std::string_view(source)=="unknown") unsupported("unsupportedSourceNames");
                if (std::string_view(state)=="unknown") unsupported("unsupportedReferenceStates");
                const char* relation="stored-group";
                if (!groupCounts.count(id)) { relation="missing"; unsupported("missingGroupReferences"); }
                else if (groupCounts[id]>1) { relation="ambiguous"; unsupported("ambiguousGroupReferences"); }
                else if (!storedGroups.count(id)) { relation="omitted"; unsupported("omittedGroupReferences"); }
                preset["groupSourceBindings"].push_back({{"groupId",id},{"source",source},
                    {"storedState",state},{"groupStatus",relation}});
                ++references;
            }
        }
        out["presets"].push_back(std::move(preset));
    }
    out["ok"]=true; out["coverageComplete"]=complete; out["status"]=complete?"summarized":"partial";
    return out;
}
} // namespace ymcc::steamdeck::bindingevidence
