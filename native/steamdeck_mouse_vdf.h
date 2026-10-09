#pragma once
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

// Steam KeyValues allows repeated "group"/"preset" keys. Keep byte offsets so
// one sensitivity scalar and its save revision can change without reserializing
// unrelated bindings/comments.
namespace ymcc::steamdeck {
struct Node {
    std::string key, value;
    bool object = false;
    size_t valueBegin = 0, valueEnd = 0, close = 0;
    std::vector<Node> children;
};
class Vdf {
    const std::string& text;
    size_t pos = 0, count = 0;
    void skip() {
        for (;;) {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
            if (text.compare(pos, 2, "//") == 0) {
                const auto end = text.find('\n', pos + 2);
                pos = end == std::string::npos ? text.size() : end;
            } else if (text.compare(pos, 2, "/*") == 0) {
                const auto end = text.find("*/", pos + 2);
                if (end == std::string::npos) throw std::runtime_error("invalid-vdf");
                pos = end + 2;
            } else break;
        }
    }
    std::string token(size_t* begin = nullptr, size_t* end = nullptr) {
        skip();
        if (pos >= text.size() || text[pos] == '{' || text[pos] == '}') throw std::runtime_error("invalid-vdf");
        const auto start = pos;
        std::string value;
        if (text[pos] == '"') {
            ++pos;
            bool closed = false;
            while (pos < text.size()) {
                char c = text[pos++];
                if (c == '"') { closed = true; break; }
                if (c == '\\' && pos < text.size()) {
                    const char next = text[pos++];
                    if (next != '\\' && next != '"') value += '\\';
                    c = next;
                }
                value += c;
            }
            if (!closed) throw std::runtime_error("invalid-vdf");
        } else {
            while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos])) &&
                   text[pos] != '{' && text[pos] != '}') value += text[pos++];
        }
        if (begin) *begin = start;
        if (end) *end = pos;
        return value;
    }
    std::vector<Node> nodes(unsigned depth) {
        if (depth > 64) throw std::runtime_error("invalid-vdf");
        std::vector<Node> result;
        for (;;) {
            skip();
            if (pos == text.size()) {
                if (depth != 0) throw std::runtime_error("invalid-vdf");
                return result;
            }
            if (text[pos] == '}') {
                if (depth == 0) throw std::runtime_error("invalid-vdf");
                return result;
            }
            if (++count > 30000) throw std::runtime_error("invalid-vdf");
            Node node;
            node.key = token();
            skip();
            if (pos < text.size() && text[pos] == '{') {
                node.object = true;
                ++pos;
                node.children = nodes(depth + 1);
                node.close = pos++;
            } else node.value = token(&node.valueBegin, &node.valueEnd);
            result.push_back(std::move(node));
        }
    }
public:
    explicit Vdf(const std::string& input) : text(input) {}
    std::vector<Node> parse() {
        if (text.size() > 2 * 1024 * 1024) throw std::runtime_error("invalid-vdf");
        if (text.compare(0, 3, "\xef\xbb\xbf") == 0) pos = 3;
        return nodes(0);
    }
};
inline const Node* unique(const std::vector<Node>& nodes, const std::string& key) {
    const Node* result = nullptr;
    for (const auto& node : nodes) if (node.key == key) {
        if (result) throw std::runtime_error("ambiguous-vdf");
        result = &node;
    }
    return result;
}
inline std::string scalar(const std::vector<Node>& nodes, const std::string& key) {
    const auto* node = unique(nodes, key);
    if (!node || node->object) return {};
    return node->value;
}
// Read only Steam-declared library roots. Modern object and legacy scalar
// formats are accepted; no disk/drive scanning or guessed install locations.
inline std::vector<std::string> libraryPaths(const std::string& text) {
    if (text.empty()) return {};
    const auto roots = Vdf(text).parse();
    const Node* folders = nullptr;
    for (const auto& node : roots) {
        std::string key = node.key;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key != "libraryfolders") continue;
        if (folders) throw std::runtime_error("ambiguous-steam-libraries");
        folders = &node;
    }
    if (!folders || !folders->object) throw std::runtime_error("invalid-steam-libraries");
    std::vector<std::string> result;
    for (const auto& folder : folders->children) {
        if (folder.key.empty() || !std::all_of(folder.key.begin(), folder.key.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) continue;
        const auto path = folder.object ? scalar(folder.children, "path") : folder.value;
        if (path.empty()) throw std::runtime_error("invalid-steam-libraries");
        result.push_back(path);
        if (result.size() > 64) throw std::runtime_error("ambiguous-steam-libraries");
    }
    return result;
}
inline int percent(const std::string& value) {
    if (value.empty() || value.size() > 5 ||
        !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::runtime_error("invalid-sensitivity");
    const int result = std::stoi(value);
    if (result < 1 || result > 10000) throw std::runtime_error("invalid-sensitivity");
    return result;
}
struct Layout {
    int sensitivity = 100;
    std::string group;
    bool explicitValue = false;
    size_t begin = 0, end = 0, settingsClose = 0;
    long long revision = -1;
    size_t revisionBegin = 0, revisionEnd = 0;
};
inline Layout desktopLayout(const std::string& text) {
    const auto roots = Vdf(text).parse();
    const auto* mappings = unique(roots, "controller_mappings");
    if (!mappings || !mappings->object || scalar(mappings->children, "controller_type") != "controller_neptune")
        throw std::runtime_error("not-neptune-desktop");
    // Only the default desktop action set, never gamepad action sets/layers.
    const Node* preset = nullptr;
    for (const auto& node : mappings->children) if (node.key == "preset" && node.object &&
        scalar(node.children, "id") == "0" && scalar(node.children, "name") == "Default") {
        if (preset) throw std::runtime_error("ambiguous-desktop-preset");
        preset = &node;
    }
    if (!preset) throw std::runtime_error("desktop-preset-not-found");
    const auto* bindings = unique(preset->children, "group_source_bindings");
    if (!bindings || !bindings->object) throw std::runtime_error("desktop-binding-not-found");
    Layout result;
    // User-provided Steam saves (100% -> 137%) also increment this top-level
    // layout revision. Mirror that metadata without touching other bindings.
    if (const auto* revision = unique(mappings->children, "revision")) {
        if (revision->object || revision->value.empty() || revision->value.size() > 10 ||
            !std::all_of(revision->value.begin(), revision->value.end(), [](char c) { return c >= '0' && c <= '9'; }))
            throw std::runtime_error("invalid-layout-revision");
        result.revision = std::stoll(revision->value);
        if (result.revision > 2147483647LL) throw std::runtime_error("invalid-layout-revision");
        result.revisionBegin = revision->valueBegin;
        result.revisionEnd = revision->valueEnd;
    }
    for (const auto& binding : bindings->children) if (!binding.object && binding.value == "right_joystick active") {
        if (!result.group.empty()) throw std::runtime_error("ambiguous-right-stick");
        result.group = binding.key;
    }
    if (result.group.empty()) throw std::runtime_error("right-stick-not-found");
    // A shared group would also alter another action set. Defer custom layouts
    // rather than silently affecting their gamepad/mode-shift bindings.
    const auto* shifts = unique(preset->children, "group_source_modeshifts");
    if (shifts && (!shifts->object || !shifts->children.empty()))
        throw std::runtime_error("desktop-modeshift-not-supported");
    for (const auto& node : mappings->children) if (&node != preset && node.key == "preset" && node.object) {
        const auto* other = unique(node.children, "group_source_bindings");
        if (other && other->object) for (const auto& binding : other->children)
            if (binding.key == result.group && !binding.object && binding.value.ends_with(" active"))
                throw std::runtime_error("shared-desktop-group");
    }

    const Node* group = nullptr;
    for (const auto& node : mappings->children) if (node.key == "group" && node.object &&
        scalar(node.children, "id") == result.group) {
        if (group) throw std::runtime_error("ambiguous-right-stick");
        group = &node;
    }
    if (!group || scalar(group->children, "mode") != "joystick_mouse")
        throw std::runtime_error("right-stick-not-mouse");
    const auto* settings = unique(group->children, "settings");
    if (!settings || !settings->object) throw std::runtime_error("stick-settings-not-found");
    result.settingsClose = settings->close;
    const auto* sensitivity = unique(settings->children, "sensitivity");
    if (sensitivity) {
        if (sensitivity->object) throw std::runtime_error("invalid-sensitivity");
        result.sensitivity = percent(sensitivity->value);
        result.explicitValue = true;
        result.begin = sensitivity->valueBegin;
        result.end = sensitivity->valueEnd;
    }
    return result;
}
inline bool desktopAutosave(const std::string& text) {
    const auto roots = Vdf(text).parse();
    const auto* config = unique(roots, "controller_config");
    if (!config || !config->object) return false;
    const auto* desktop = unique(config->children, "413080");
    // External templates/workshop refs need device evidence; never guess one.
    return desktop && desktop->object && scalar(desktop->children, "autosave") == "1" &&
        !unique(desktop->children, "template") && !unique(desktop->children, "workshop");
}
inline std::string replaceSensitivity(const std::string& text, int value) {
    if (value < 1 || value > 300) throw std::runtime_error("invalid-sensitivity");
    const auto layout = desktopLayout(text);
    if (layout.sensitivity == value) return text; // No-op must not dirty metadata.
    if (layout.revision >= 2147483647LL) throw std::runtime_error("layout-revision-overflow");
    struct Edit { size_t begin, end; std::string value; };
    std::vector<Edit> edits;
    if (layout.explicitValue)
        edits.push_back({layout.begin, layout.end, "\"" + std::to_string(value) + "\""});
    else {
        const std::string newline = text.find("\r\n") == std::string::npos ? "\n" : "\r\n";
        edits.push_back({layout.settingsClose, layout.settingsClose, "\t\"sensitivity\"\t\t\"" +
            std::to_string(value) + "\"" + newline + "\t\t"});
    }
    if (layout.revision >= 0)
        edits.push_back({layout.revisionBegin, layout.revisionEnd, "\"" + std::to_string(layout.revision + 1) + "\""});
    // Apply from high offsets to low so metadata may precede OR follow groups.
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
    std::string output = text;
    for (const auto& edit : edits) output.replace(edit.begin, edit.end - edit.begin, edit.value);
    return output;
}
} // namespace ymcc::steamdeck
