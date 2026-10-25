#pragma once
// Read-only shared library resource lookup. No Steam/network/process acquisition.
#include "custom_steam_library_paths.h"
#include "json.hpp"
#include <fstream>
#include <iterator>

namespace custom_steam_library::logos {
using json = nlohmann::json;
inline std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) return {};
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
inline std::string utf8(const std::wstring& value) {
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    if (count) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
inline std::string text(const json& value, const char* key) {
    if (!value.is_object()) return {};
    const auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
inline std::wstring pathKey(const fs::path& path) {
    if (path.empty()) return {};
    std::error_code error;
    auto result = fs::weakly_canonical(path, error);
    if (error) result = path.lexically_normal();
    auto key = result.wstring();
    std::replace(key.begin(), key.end(), L'/', L'\\');
    if (!key.empty()) CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
    return key;
}
struct Reader {
    size_t budget = 24 * 1024 * 1024;
    json read(const fs::path& path) {
        std::error_code ec;
        auto bytes = fs::file_size(ioPath(path), ec);
        if (ec || bytes > 4 * 1024 * 1024 || bytes > budget) return json::object();
        budget -= static_cast<size_t>(bytes);
        std::ifstream stream(ioPath(path), std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(stream)), {});
        if (!stream || data.size() != bytes) return json::object();
        if (data.starts_with("\xef\xbb\xbf")) data.erase(0, 3);
        auto parsed = json::parse(data, nullptr, false);
        return parsed.is_discarded() ? json::object() : parsed;
    }
};
inline bool matches(const json& item, const std::wstring& wanted) {
    for (const auto* key : {"primaryExecutable", "exe", "nativeSteamExecutable"}) {
        const auto value = text(item, key);
        if (!value.empty() && pathKey(fs::path(wide(value))) == wanted) return true;
    }
    return false;
}
inline std::vector<fs::path> directories(const fs::path& root) {
    std::vector<fs::path> result;
    std::error_code ec;
    for (fs::directory_iterator it(ioPath(root), ec), end; !ec && it != end && result.size() < 512; it.increment(ec)) {
        if (it->is_directory(ec) && !ec) result.push_back(it->path());
        ec.clear();
    }
    std::sort(result.begin(), result.end());
    return result;
}
inline std::string encode(const std::vector<unsigned char>& bytes) {
    constexpr auto alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        unsigned bits = unsigned(bytes[i]) << 16;
        if (i + 1 < bytes.size()) bits |= unsigned(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size()) bits |= bytes[i + 2];
        result += alphabet[(bits >> 18) & 63]; result += alphabet[(bits >> 12) & 63];
        result += i + 1 < bytes.size() ? alphabet[(bits >> 6) & 63] : '=';
        result += i + 2 < bytes.size() ? alphabet[bits & 63] : '=';
    }
    return result;
}
inline json raster(const fs::path& source) {
    if (!source.is_absolute()) return nullptr;
    const auto ext = lower(source.extension().wstring());
    if (ext != L".png" && ext != L".jpg" && ext != L".jpeg" && ext != L".webp") return nullptr;
    std::error_code ec;
    const auto size = fs::file_size(ioPath(source), ec);
    if (ec || size < 12 || size > 2 * 1024 * 1024) return nullptr;
    const auto before = fs::last_write_time(ioPath(source), ec);
    if (ec) return nullptr;
    std::ifstream stream(ioPath(source), std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!stream || stream.gcount() != size || fs::last_write_time(ioPath(source), ec) != before || ec) return nullptr;
    const bool png = bytes[0] == 137 && bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71 && bytes[4] == 13 && bytes[5] == 10 && bytes[6] == 26 && bytes[7] == 10;
    const bool jpg = bytes[0] == 255 && bytes[1] == 216 && bytes[2] == 255;
    const bool webp = std::string(bytes.begin(), bytes.begin() + 4) == "RIFF" && std::string(bytes.begin() + 8, bytes.begin() + 12) == "WEBP";
    if (!png && !jpg && !webp) return nullptr;
    return {{"image", std::string("data:image/") + (png ? "png" : jpg ? "jpeg" : "webp") + ";base64," + encode(bytes)},
            {"revision", utf8(pathKey(source)) + "|" + std::to_string(size) + "|" + std::to_string(before.time_since_epoch().count())}};
}
inline json asset(const json& value) {
    for (const auto* key : {"portableFile", "file", "path"}) {
        const auto path = text(value, key);
        if (!path.empty()) { auto result = raster(fs::path(wide(path))); if (!result.is_null()) return result; }
    }
    return nullptr;
}
inline json manifestLogo(const json& manifest, const std::wstring& wanted) {
    if (!matches(manifest, wanted)) return nullptr;
    const auto it = manifest.find("artwork");
    if (it == manifest.end() || !it->is_array()) return nullptr;
    for (const auto& artwork : *it) if (text(artwork, "type") == "logo" && artwork.value("ok", json(false)) == true) {
        auto result = asset(artwork); if (!result.is_null()) return result;
    }
    return nullptr;
}
inline json resolveAtRoot(const fs::path& root, const std::string& executable) {
    const fs::path target(wide(executable));
    if (executable.size() > 32768 || !target.is_absolute() || lower(target.extension().wstring()) != L".exe") return nullptr;
    const auto wanted = pathKey(target);
    Reader reader;
    const auto manual = reader.read(root / L"config" / L"manual-overrides.json");
    const auto items = manual.find("items");
    if (items != manual.end() && items->is_object()) for (const auto& [key, value] : items->items()) {
        if (pathKey(fs::path(wide(key))) != wanted || !value.is_object()) continue;
        const auto protection = value.find("artworkProtection");
        if (protection != value.end() && text(*protection, "logo") == "deleted") return nullptr;
        const auto logo = value.find("logo");
        if (logo != value.end()) { auto result = asset(*logo); if (!result.is_null()) return result; }
    }
    // Reuse the library's exact EXE -> artwork index. Never fuzzy-match titles.
    const auto plan = reader.read(root / L"state" / L"steam-add-plan.json");
    const auto entries = plan.find("items");
    if (entries != plan.end() && entries->is_array()) for (const auto& item : *entries) {
        if (!matches(item, wanted)) continue;
        const auto preview = item.find("artworkPreview");
        if (preview != item.end() && preview->is_object()) {
            const auto logo = preview->find("logo");
            if (logo != preview->end()) { auto result = asset(*logo); if (!result.is_null()) return result; }
        }
        const auto manifestPath = text(item, "artworkManifest");
        if (!manifestPath.empty()) { auto result = manifestLogo(reader.read(fs::path(wide(manifestPath))), wanted); if (!result.is_null()) return result; }
        const auto art = item.find("artwork");
        if (art != item.end() && art->is_object()) {
            const auto logo = art->find("logo");
            if (logo != art->end() && logo->is_object()) {
                const auto files = logo->find("files");
                if (files != logo->end() && files->is_array()) for (const auto& file : *files) if (file.is_string()) {
                    const fs::path original(wide(file.get<std::string>()));
                    const auto stem = original.stem().wstring();
                    const auto account = original.parent_path().parent_path().parent_path().filename();
                    if (stem.ends_with(L"_logo") && !account.empty()) {
                        const auto cached = root / L"cache" / L"existing-steam-artwork" / (account.wstring() + L"_" + stem.substr(0, stem.size() - 5));
                        for (const auto* ext : {L".png", L".webp", L".jpg", L".jpeg"}) { auto result = raster(cached / (std::wstring(L"logo") + ext)); if (!result.is_null()) return result; }
                    }
                    auto result = raster(original); if (!result.is_null()) return result;
                }
            }
        }
    }
    // Portable manifests also cover Steam-native and non-Steam games not in the add plan.
    for (const auto& directory : directories(root / L"artwork")) {
        if (!reader.budget) break;
        auto result = manifestLogo(reader.read(directory / L"manifest.json"), wanted);
        if (!result.is_null()) return result;
    }
    return nullptr;
}
inline json resolve(const fs::path& appRoot, const std::string& executable) {
    try { return resolveAtRoot(defaultDataRoot(appRoot), executable); } catch (...) { return nullptr; }
}
} // namespace custom_steam_library::logos