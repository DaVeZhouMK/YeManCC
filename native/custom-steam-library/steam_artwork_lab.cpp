#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <gdiplus.h>
#include <objidl.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "json.hpp"
#include "custom_steam_library_paths.h"
#include "custom_steam_library_inventory.h"
#include "custom_steam_library_identity_cache.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

static bool pathWithin(const fs::path& child, const fs::path& parent);

// Artwork lookup is a background task and defaults to a weak-network profile.
// A worker reuses its WinHTTP session so sequential Steam CDN candidates can
// share DNS/TCP/TLS state. A transport failure still discards that session
// before the next retry, preventing a poisoned connection from being reused.
static constexpr DWORD HTTP_RESOLVE_TIMEOUT_MS = 15000;
static constexpr DWORD HTTP_CONNECT_TIMEOUT_MS = 25000;
static constexpr DWORD HTTP_TIMEOUT_MS = 45000;
static constexpr ULONGLONG HTTP_METADATA_BUDGET_MS = 180000;
static constexpr ULONGLONG HTTP_IMAGE_BUDGET_MS = 300000;
// The local Steamcommunity 302 forwarder is normally very fast, but a
// half-open listener must not consume the same five-minute budget as a real
// CDN download.  When an enabled Steam alias is available, probe it briefly
// and then fall back to the original URL.
static constexpr DWORD HTTP_ACCELERATOR_RESOLVE_TIMEOUT_MS = 3000;
static constexpr DWORD HTTP_ACCELERATOR_CONNECT_TIMEOUT_MS = 5000;
static constexpr DWORD HTTP_ACCELERATOR_TIMEOUT_MS = 15000;
static constexpr ULONGLONG HTTP_ACCELERATOR_METADATA_BUDGET_MS = 30000;
static constexpr ULONGLONG HTTP_ACCELERATOR_IMAGE_BUDGET_MS = 90000;
static constexpr DWORD DNS_DIRECT_FALLBACK_TIMEOUT_MS = 1200;
static constexpr DWORD HTTP_MAX_RETRY_AFTER_MS = 30000;
static constexpr size_t MAX_METADATA_BYTES = 8u << 20;
static constexpr size_t MAX_IMAGE_BYTES = 25u << 20;
// Scheme C: one initial request plus three bounded immediate retries.
// Persistent network failures are still handed to the event-driven retry queue.
static constexpr int HTTP_MAX_ATTEMPTS = 4;
static constexpr const char* STEAM_RESOLVER_VERSION = "steam-official-fuzzy-global-v4";
static constexpr const char* CUSTOM_STEAM_LIBRARY_FEATURE_NAME = "Custom Steam Library";

static bool isCustomSteamLibraryFeatureName(const std::string& value) {
    // Accept the old label so backups made before the rename remain restorable.
    return value == CUSTOM_STEAM_LIBRARY_FEATURE_NAME || value == "Steam大屏";
}

static std::map<std::string, int> g_injectedNetworkFailures;
static std::map<std::string, int> g_injectedArtworkNetworkFailures;
static std::map<std::string, int> g_configuredArtworkNetworkFailures;
// Background identity requests use a short fail-fast budget. A dead Steam
// endpoint must not occupy the serial queue for several minutes; the host
// will retry the item in the next automatic pass or when the network wakes.
static bool g_fastMetadataMode = false;

// The worker is launched as a separate process for each game, so the useful
// repeat-scrape cache must live below the portable data root rather than only
// in process memory.
static fs::path g_workerDataRoot;
static constexpr uint64_t STEAM_APPDETAILS_CACHE_TTL_MS = 12ull * 60ull * 60ull * 1000ull;
static constexpr uint64_t ARTWORK_URL_CACHE_TTL_MS = 7ull * 24ull * 60ull * 60ull * 1000ull;
static constexpr uint64_t STEAM_ROUTE_MEMORY_TTL_MS = 6ull * 60ull * 60ull * 1000ull;
static constexpr uint64_t ARTWORK_NEGATIVE_CACHE_TTL_MS = 10ull * 60ull * 1000ull;
static bool g_steamRouteMemoryLoaded = false;
static bool g_steamHashedStoreAssetPreferDirect = false;
static uint64_t g_steamRouteMemoryUpdatedAt = 0;

static bool isHashedSteamStoreAssetUrl(const std::string& url);
static bool preferDirectSteamStoreAssetRoute(const std::string& url);
static void rememberDirectSteamStoreAssetRoute(const std::string& url);
static uint64_t steamRouteMemoryUpdatedAt();

static uint64_t unixTimeMs();

static std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

static std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

static std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::wstring unicodeLower(std::wstring value) {
    if (value.empty()) return value;
    const int needed = LCMapStringEx(
        LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
    if (needed <= 0) return value;
    std::wstring out(static_cast<size_t>(needed), L'\0');
    if (LCMapStringEx(
        LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING,
        value.data(), static_cast<int>(value.size()), out.data(), needed, nullptr, nullptr, 0) <= 0) {
        return value;
    }
    return out;
}

// Windows game folders frequently contain full-width Latin characters and
// spaces copied from Chinese storefronts or forum posts.  Fold only the
// reversible full-width ASCII range; do not transliterate Chinese, because
// Steam's localized search can use the original Han text.
static std::wstring normalizeSearchWidth(std::wstring value) {
    for (auto& ch : value) {
        if (ch >= 0xFF01 && ch <= 0xFF5E) ch = static_cast<wchar_t>(ch - 0xFEE0);
        else if (ch == 0x3000) ch = L' ';
    }
    return value;
}

static bool unicodeAlphaNumeric(wchar_t ch) {
    WORD kind = 0;
    return GetStringTypeW(CT_CTYPE1, &ch, 1, &kind) != 0 &&
        (kind & (C1_ALPHA | C1_DIGIT)) != 0;
}

static std::string trim(std::string value) {
    auto blank = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!value.empty() && blank(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && blank(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

static std::string collapseSpaces(const std::string& value) {
    std::string out;
    bool pendingSpace = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            pendingSpace = !out.empty();
        } else {
            if (pendingSpace) out.push_back(' ');
            out.push_back(static_cast<char>(ch));
            pendingSpace = false;
        }
    }
    return trim(out);
}

static std::string humanizeTitle(const std::string& value) {
    const auto normalized = toUtf8(normalizeSearchWidth(toWide(value)));
    std::string out;
    for (size_t i = 0; i < normalized.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(normalized[i]);
        if (ch == '_' || ch == '.' || ch == '-') {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            continue;
        }
        if (i > 0) {
            unsigned char prev = static_cast<unsigned char>(normalized[i - 1]);
            const bool letterDigit = std::isalpha(prev) && std::isdigit(ch);
            const bool digitLetter = std::isdigit(prev) && std::isalpha(ch);
            const bool camel = std::islower(prev) && std::isupper(ch);
            if ((letterDigit || digitLetter || camel) && !out.empty() && out.back() != ' ') out.push_back(' ');
        }
        out.push_back(static_cast<char>(ch));
    }
    return collapseSpaces(out);
}

static std::string normalizeTitle(const std::string& value) {
    std::wstring out;
    for (wchar_t ch : unicodeLower(normalizeSearchWidth(toWide(value)))) {
        if (unicodeAlphaNumeric(ch)) out.push_back(ch);
    }
    return toUtf8(out);
}

static std::string searchQueryKey(const std::string& value) {
    return normalizeTitle(collapseSpaces(value));
}

static std::vector<std::string> titleTokens(const std::string& value) {
    static const std::unordered_set<std::string> noise = {
        "the", "a", "an", "of", "and", "for", "with", "game", "edition"
    };
    std::vector<std::string> tokens;
    std::wstring current;
    auto append = [&]() {
        if (current.empty()) return;
        const auto token = toUtf8(current);
        if (!noise.count(token)) tokens.push_back(token);
        current.clear();
    };
    for (wchar_t ch : unicodeLower(toWide(humanizeTitle(value)))) {
        if (unicodeAlphaNumeric(ch)) current.push_back(ch);
        else append();
    }
    append();
    return tokens;
}

static std::string romanNumber(const std::string& raw) {
    const auto value = asciiLower(raw);
    static const std::map<std::string, std::string> values = {
        {"i", "1"}, {"ii", "2"}, {"iii", "3"}, {"iv", "4"}, {"v", "5"},
        {"vi", "6"}, {"vii", "7"}, {"viii", "8"}, {"ix", "9"}, {"x", "10"}
    };
    const auto found = values.find(value);
    return found == values.end() ? std::string{} : found->second;
}

static std::string scanTitleAcronym(const std::string& value) {
    std::vector<std::string> words;
    std::string current;
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) current.push_back(static_cast<char>(std::tolower(ch)));
        else if (!current.empty()) {
            words.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) words.push_back(current);
    std::string out;
    for (const auto& word : words) {
        const auto roman = romanNumber(word);
        if (!roman.empty()) out += roman;
        else if (!word.empty()) out.push_back(word.front());
    }
    return out;
}

static std::set<int> titleOrdinalSet(const std::string& value) {
    std::set<int> ordinals;
    for (const auto& token : titleTokens(value)) {
        const auto roman = romanNumber(token);
        const auto numeric = !roman.empty() ? roman : token;
        if (numeric.empty() || !std::all_of(numeric.begin(), numeric.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) continue;
        try {
            const auto number = std::stoi(numeric);
            if (number > 0) ordinals.insert(number);
        } catch (...) {}
    }
    return ordinals;
}

static bool noisyDirectory(const std::string& value);
static bool containsSteamRelatedContentMarker(const std::string& text);

static double scoreTitle(const std::string& query, const std::string& candidate) {
    const auto queryKey = normalizeTitle(query);
    const auto candidateKey = normalizeTitle(candidate);
    if (queryKey.empty() || candidateKey.empty()) return 0.0;
    if (queryKey == candidateKey) return 1.0;

    const auto queryList = titleTokens(query);
    const auto candidateList = titleTokens(candidate);
    std::set<std::string> queryTokens(queryList.begin(), queryList.end());
    std::set<std::string> candidateTokens(candidateList.begin(), candidateList.end());
    if (queryTokens.empty() || candidateTokens.empty()) return 0.0;
    size_t intersection = 0;
    for (const auto& token : queryTokens) if (candidateTokens.count(token)) ++intersection;
    const double coverage = static_cast<double>(intersection) / queryTokens.size();
    const double precision = static_cast<double>(intersection) / candidateTokens.size();
    double score = coverage * 0.68 + precision * 0.32;
    if (candidateKey.starts_with(queryKey) || queryKey.starts_with(candidateKey)) score += 0.08;

    if (containsSteamRelatedContentMarker(candidate) &&
        !containsSteamRelatedContentMarker(query)) score -= 0.30;
    const auto queryOrdinals = titleOrdinalSet(query);
    const auto candidateOrdinals = titleOrdinalSet(candidate);
    if (queryOrdinals != candidateOrdinals && (!queryOrdinals.empty() || !candidateOrdinals.empty())) {
        score = (std::min)(score, 0.72);
    }
    return std::clamp(score, 0.0, 0.99);
}

static double scoreInstalledTitle(const std::string& installed, const std::string& formal) {
    double score = scoreTitle(installed, formal);
    const auto installedKey = normalizeTitle(installed);
    const auto formalKey = normalizeTitle(formal);
    const auto installedAcronym = scanTitleAcronym(installed);
    const auto formalAcronym = scanTitleAcronym(formal);
    if ((installedKey.size() >= 3 && installedKey == formalAcronym) ||
        (formalKey.size() >= 3 && formalKey == installedAcronym)) {
        score = (std::max)(score, 0.965);
    }
    return score;
}

static std::vector<std::string> buildPathEvidence(const fs::path& exePath) {
    std::vector<std::string> evidence;
    std::set<std::string> seen;
    auto add = [&](const std::string& raw) {
        const auto value = collapseSpaces(raw);
        const auto key = normalizeTitle(value);
        if (key.size() >= 2 && seen.insert(key).second) evidence.push_back(value);
    };
    // EXE stem is already an independent query round. Path evidence is kept
    // directory-only so a misleading executable name cannot reinforce itself.
    auto current = exePath.parent_path();
    for (int depth = 0; !current.empty() && depth < 7; ++depth) {
        const auto name = toUtf8(current.filename().wstring());
        if (!name.empty() && !noisyDirectory(name)) {
            add(name);
            add(humanizeTitle(name));
        }
        current = current.parent_path();
    }
    return evidence;
}

static double scorePathEvidence(const std::vector<std::string>& evidence, const std::string& networkName) {
    double best = 0.0;
    for (const auto& value : evidence) best = (std::max)(best, scoreTitle(value, networkName));
    return best;
}

static std::string stripAddonSuffix(const std::string& value) {
    const auto humanized = humanizeTitle(value);
    const auto lower = asciiLower(humanized);
    static const std::array<std::string, 10> markers = {
        " nation pack", " country pack", " dlc", " expansion", " soundtrack",
        " season pass", " bonus content", " content pack", " skin pack", " map pack"
    };
    // Total War's standalone campaign installs often use names such as
    // "Rise of the Republic" while the executable identifies the base game.
    // Keep the base title as an additional search round.
    static const std::array<std::string, 2> editionMarkers = {
        " rise of the ", " fall of the "
    };
    size_t cut = std::string::npos;
    for (const auto& marker : markers) {
        const auto position = lower.find(marker);
        if (position != std::string::npos) cut = (std::min)(cut, position);
    }
    for (const auto& marker : editionMarkers) {
        const auto position = lower.find(marker);
        if (position != std::string::npos) cut = (std::min)(cut, position);
    }
    if (cut == std::string::npos) return {};
    const auto base = trim(humanized.substr(0, cut));
    return normalizeTitle(base).size() >= 3 ? base : std::string{};
}

// Chinese repacks and localized folder names often append a packaging label
// that is not part of the Steam product title.  Treat these as additional
// search-only variants.  The original evidence remains untouched, so the
// label can never silently rename an installed game.
static std::string stripSearchPackagingSuffix(const std::string& value) {
    std::wstring current = unicodeLower(normalizeSearchWidth(toWide(humanizeTitle(value))));
    auto trimWide = [](std::wstring text) {
        const auto first = text.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return std::wstring{};
        const auto last = text.find_last_not_of(L" \t\r\n");
        return text.substr(first, last - first + 1);
    };
    static const std::array<std::wstring, 18> suffixes = {
        L"[简体中文]", L"[繁体中文]", L"[中文汉化]", L"【简体中文】", L"【繁体中文】", L"【中文汉化】",
        L"(简体中文)", L"(繁体中文)", L"(中文汉化)", L"（简体中文）", L"（繁体中文）", L"（中文汉化）",
        L"中文版", L"简体中文", L"繁体中文", L"中文汉化", L"汉化版", L"汉化"
    };
    bool changed = true;
    while (changed && !current.empty()) {
        changed = false;
        for (const auto& suffix : suffixes) {
            if (current.size() < suffix.size() ||
                current.compare(current.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            current = trimWide(current.substr(0, current.size() - suffix.size()));
            changed = true;
            break;
        }
    }
    const auto result = toUtf8(current);
    return normalizeTitle(result).size() >= 3 ? result : std::string{};
}

static bool containsSteamRelatedContentMarker(const std::string& text) {
    const auto lower = asciiLower(humanizeTitle(text));
    static const std::array<const char*, 20> englishMarkers = {
        "dlc", "soundtrack", "season pass", "nation pack", "country pack",
        "expansion", "demo", "redmod", "bonus content", "dedicated server",
        "benchmark", "sdk", "tool", "artbook", "supporter", "upgrade",
        "early access pack", "content pack", "skin pack", "map pack"
    };
    for (const auto* marker : englishMarkers) {
        if (lower.find(marker) != std::string::npos) return true;
    }
    const auto wide = unicodeLower(normalizeSearchWidth(toWide(text)));
    static const std::array<const wchar_t*, 14> chineseMarkers = {
        L"资料片", L"扩展包", L"原声", L"季票", L"国家包", L"内容包", L"皮肤包",
        L"地图包", L"演示", L"试玩", L"专用服务器", L"工具", L"艺术设定集", L"升级包"
    };
    for (const auto* marker : chineseMarkers) {
        if (wide.find(marker) != std::wstring::npos) return true;
    }
    return false;
}

static json runSteamSearchQuerySelfTest() {
    struct Case { const char* name; bool passed; json detail; };
    std::vector<Case> cases;
    const auto addCase = [&](const char* name, bool passed, json detail) {
        cases.push_back({name, passed, std::move(detail)});
    };
    const auto fullWidth = std::string("Ｃｙｂｅｒｐｕｎｋ　２０７７");
    addCase("full-width-ascii-and-space-fold",
        searchQueryKey(fullWidth) == searchQueryKey("Cyberpunk 2077"),
        {{"input", fullWidth}, {"normalized", searchQueryKey(fullWidth)}});
    const auto simplified = stripSearchPackagingSuffix("赛博朋克 2077 简体中文");
    addCase("simplified-chinese-packaging-suffix",
        !simplified.empty() && searchQueryKey(simplified) == searchQueryKey("赛博朋克 2077"),
        {{"stripped", simplified}});
    const auto bracketed = stripSearchPackagingSuffix("Cyberpunk 2077 [中文汉化]");
    addCase("bracketed-chinese-packaging-suffix",
        !bracketed.empty() && searchQueryKey(bracketed) == searchQueryKey("Cyberpunk 2077"),
        {{"stripped", bracketed}});
    addCase("chinese-related-content-marker",
        containsSteamRelatedContentMarker("赛博朋克 2077 原声带"),
        {{"text", "赛博朋克 2077 原声带"}});
    bool allPassed = true;
    json resultCases = json::array();
    for (const auto& item : cases) {
        allPassed = allPassed && item.passed;
        resultCases.push_back({{"name", item.name}, {"passed", item.passed}, {"detail", item.detail}});
    }
    return {{"resolverVersion", STEAM_RESOLVER_VERSION}, {"allPassed", allPassed}, {"cases", resultCases}};
}

static std::vector<std::string> jsonStringArray(const json& value, const std::string& nestedName = {}) {
    std::vector<std::string> result;
    if (!value.is_array()) return result;
    for (const auto& item : value) {
        if (item.is_string()) result.push_back(item.get<std::string>());
        else if (item.is_object() && !nestedName.empty()) {
            const auto text = item.value(nestedName, std::string{});
            if (!text.empty()) result.push_back(text);
        }
    }
    return result;
}

static bool platformMatches(const std::vector<std::string>& platforms, const std::string& hint) {
    const auto wanted = asciiLower(trim(hint));
    if (wanted.empty() || wanted == "any" || wanted == "all") return true;
    for (const auto& platform : platforms) {
        const auto value = asciiLower(platform);
        if ((wanted == "pc" || wanted == "windows" || wanted == "pc-windows") &&
            (value.find("microsoft windows") != std::string::npos || value == "windows")) return true;
        if (wanted == "linux" && value.find("linux") != std::string::npos) return true;
        if ((wanted == "mac" || wanted == "macos") && value.find("mac") != std::string::npos) return true;
        if ((wanted == "playstation" || wanted == "ps4" || wanted == "ps5") &&
            value.find("playstation") != std::string::npos) return true;
        if ((wanted == "ps2" || wanted == "playstation2" || wanted == "playstation-2") &&
            value.find("playstation 2") != std::string::npos) return true;
        if ((wanted == "ps3" || wanted == "playstation3" || wanted == "playstation-3") &&
            value.find("playstation 3") != std::string::npos) return true;
        if ((wanted == "psp" || wanted == "playstation-portable") &&
            (value.find("playstation portable") != std::string::npos || value == "psp")) return true;
        if (wanted == "xbox" && value.find("xbox") != std::string::npos) return true;
        if ((wanted == "switch" || wanted == "nintendo") &&
            (value.find("switch") != std::string::npos || value.find("nintendo") != std::string::npos)) return true;
        if (value.find(wanted) != std::string::npos) return true;
    }
    return false;
}

static bool isSteamHost(const std::wstring& host) {
    auto value = asciiLower(toUtf8(host));
    auto endsWith = [&](const std::string& suffix) {
        return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return value == "steampowered.com" || endsWith(".steampowered.com") ||
           value == "steamstatic.com" || endsWith(".steamstatic.com") ||
           value == "steamusercontent.com" || endsWith(".steamusercontent.com") ||
           value == "akamaihd.net" || endsWith(".akamaihd.net") ||
           value == "eccdnx.com" || endsWith(".eccdnx.com");
}

struct SteamAcceleratorStatus {
    bool checked = false;
    bool listener80 = false;
    bool listener443 = false;
    bool storeLocal = false;
    bool storeAliasLocal = false;
    bool apiLocal = false;
    bool cdnLocal = false;
    bool cdnAliasLocal = false;
    bool videoAliasLocal = false;
    bool localRequestSucceeded = false;
    bool localRequestFailed = false;
    std::string state = "unknown";
    std::string reason;
    std::map<std::string, bool> localHostCache;
};

static SteamAcceleratorStatus g_steamAcceleratorStatus;

static bool ensureWinsock() {
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
}

static bool localTcpListenerReachable(USHORT port) {
    if (!ensureWinsock()) return false;
    const SOCKET socketHandle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) return false;

    u_long nonBlocking = 1;
    ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const int result = ::connect(socketHandle,
        reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == 0) {
        closesocket(socketHandle);
        return true;
    }
    const int connectError = WSAGetLastError();
    if (connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS) {
        closesocket(socketHandle);
        return false;
    }

    fd_set writable{};
    FD_ZERO(&writable);
    FD_SET(socketHandle, &writable);
    timeval timeout{};
    timeout.tv_usec = 150000;
    const int selected = select(0, nullptr, &writable, nullptr, &timeout);
    int socketError = WSAETIMEDOUT;
    int socketErrorBytes = sizeof(socketError);
    if (selected > 0) {
        getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char*>(&socketError), &socketErrorBytes);
    }
    closesocket(socketHandle);
    return selected > 0 && socketError == 0;
}

static bool hostResolvesToLoopback(const std::string& host) {
#ifdef CUSTOM_STEAM_LIBRARY_TEST_RUNTIME
    return false;
#endif
    auto cached = g_steamAcceleratorStatus.localHostCache.find(host);
    if (cached != g_steamAcceleratorStatus.localHostCache.end()) return cached->second;
    if (!ensureWinsock()) return false;

    ADDRINFOW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    PADDRINFOW results = nullptr;
    const auto wideHost = toWide(host);
    const int error = GetAddrInfoW(wideHost.c_str(), nullptr, &hints, &results);
    bool loopback = false;
    if (error == 0) {
        for (auto* item = results; item; item = item->ai_next) {
            if (item->ai_family == AF_INET && item->ai_addrlen >= sizeof(sockaddr_in)) {
                const auto* address = reinterpret_cast<const sockaddr_in*>(item->ai_addr);
                if (ntohl(address->sin_addr.s_addr) == INADDR_LOOPBACK) {
                    loopback = true;
                    break;
                }
            } else if (item->ai_family == AF_INET6 && item->ai_addrlen >= sizeof(sockaddr_in6)) {
                const auto* address = reinterpret_cast<const sockaddr_in6*>(item->ai_addr);
                if (IN6_IS_ADDR_LOOPBACK(&address->sin6_addr)) {
                    loopback = true;
                    break;
                }
            }
        }
    }
    if (results) FreeAddrInfoW(results);
    g_steamAcceleratorStatus.localHostCache[host] = loopback;
    return loopback;
}

static void refreshSteamAcceleratorStatus() {
#ifdef CUSTOM_STEAM_LIBRARY_TEST_RUNTIME
    g_steamAcceleratorStatus.checked = true;
    g_steamAcceleratorStatus.state = "isolated-test-direct";
    return;
#endif
    if (g_steamAcceleratorStatus.checked) return;
    g_steamAcceleratorStatus.checked = true;
    g_steamAcceleratorStatus.listener80 = localTcpListenerReachable(80);
    g_steamAcceleratorStatus.listener443 = localTcpListenerReachable(443);
    g_steamAcceleratorStatus.storeLocal = hostResolvesToLoopback("store.steampowered.com");
    g_steamAcceleratorStatus.storeAliasLocal = hostResolvesToLoopback("steamstore-a.akamaihd.net");
    g_steamAcceleratorStatus.apiLocal = hostResolvesToLoopback("api.steampowered.com");
    g_steamAcceleratorStatus.cdnLocal =
        hostResolvesToLoopback("shared.steamstatic.com") ||
        hostResolvesToLoopback("shared.cloudflare.steamstatic.com") ||
        hostResolvesToLoopback("cdn.cloudflare.steamstatic.com");
    g_steamAcceleratorStatus.cdnAliasLocal = hostResolvesToLoopback("steamcdn-a.akamaihd.net");
    g_steamAcceleratorStatus.videoAliasLocal = hostResolvesToLoopback("steamvideo-a.akamaihd.net");

    const int localHostCount = static_cast<int>(g_steamAcceleratorStatus.storeLocal ||
        g_steamAcceleratorStatus.storeAliasLocal) +
        static_cast<int>(g_steamAcceleratorStatus.apiLocal) +
        static_cast<int>(g_steamAcceleratorStatus.cdnLocal ||
            g_steamAcceleratorStatus.cdnAliasLocal) +
        static_cast<int>(g_steamAcceleratorStatus.videoAliasLocal);
    if (localHostCount > 0 && g_steamAcceleratorStatus.listener443) {
        g_steamAcceleratorStatus.state = localHostCount >= 3 ? "active" : "partial";
        g_steamAcceleratorStatus.reason = "Steam direct/alias host resolution and local TLS listener agree";
    } else if (localHostCount > 0) {
        g_steamAcceleratorStatus.state = "blocked";
        g_steamAcceleratorStatus.reason = "Steam hosts resolve to loopback but local 443 is unavailable";
    } else if (g_steamAcceleratorStatus.listener80 || g_steamAcceleratorStatus.listener443) {
        g_steamAcceleratorStatus.state = "listener-only";
        g_steamAcceleratorStatus.reason = "local 80/443 listens, but Steam metadata/CDN hosts resolve externally";
    } else {
        g_steamAcceleratorStatus.state = "inactive";
        g_steamAcceleratorStatus.reason = "no usable local Steam transparent route detected";
    }
}

static bool steamRouteIsLocal(const std::string& host, USHORT port) {
    refreshSteamAcceleratorStatus();
    if (!hostResolvesToLoopback(host)) return false;
    return port == 80 ? g_steamAcceleratorStatus.listener80 : g_steamAcceleratorStatus.listener443;
}

static bool isLocalSteamRoute(const std::string& route) {
    return route == "transparent-local" || route == "transparent-local-unavailable";
}

static std::string steamRouteForUrl(const std::string& url) {
    const auto wideUrl = toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) return "direct";
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    if (!isSteamHost(host)) return "direct";
    const auto hostText = toUtf8(host);
    if (steamRouteIsLocal(hostText, parts.nPort)) return "transparent-local";
    if (hostResolvesToLoopback(hostText)) return "transparent-local-unavailable";
    return "direct";
}

struct DirectDnsAnswer {
    std::string address;
    std::string resolver;
};

struct SteamDirectIpFallback {
    std::string requestUrl;
    std::string originalHost;
    std::string address;
    std::string resolver;
};

static uint16_t dnsReadU16(const std::vector<unsigned char>& bytes, size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) | bytes[offset + 1]);
}

static bool dnsSkipName(const std::vector<unsigned char>& bytes, size_t& offset) {
    while (offset < bytes.size()) {
        const unsigned char length = bytes[offset++];
        if (length == 0) return true;
        if ((length & 0xc0) == 0xc0) {
            if (offset >= bytes.size()) return false;
            ++offset;
            return true;
        }
        if ((length & 0xc0) != 0 || length > 63 || offset + length > bytes.size()) return false;
        offset += length;
    }
    return false;
}

static bool dnsAppendName(std::vector<unsigned char>& request, const std::string& host) {
    size_t start = 0;
    while (start < host.size()) {
        const size_t end = host.find('.', start);
        const size_t length = (end == std::string::npos ? host.size() : end) - start;
        if (length == 0 || length > 63) return false;
        request.push_back(static_cast<unsigned char>(length));
        request.insert(request.end(), host.begin() + static_cast<std::ptrdiff_t>(start),
            host.begin() + static_cast<std::ptrdiff_t>(start + length));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    request.push_back(0);
    return true;
}

static std::vector<std::string> configuredDnsServers() {
    ULONG bytes = 0;
    const DWORD initial = GetNetworkParams(nullptr, &bytes);
    if (initial != ERROR_BUFFER_OVERFLOW || bytes < sizeof(FIXED_INFO)) return {};
    std::vector<unsigned char> storage(bytes);
    auto* info = reinterpret_cast<FIXED_INFO*>(storage.data());
    if (GetNetworkParams(info, &bytes) != NO_ERROR) return {};

    std::vector<std::string> servers;
    for (IP_ADDR_STRING* item = &info->DnsServerList; item; item = item->Next) {
        const std::string address = trim(item->IpAddress.String);
        in_addr parsed{};
        if (address.empty() || InetPtonA(AF_INET, address.c_str(), &parsed) != 1) continue;
        if (std::find(servers.begin(), servers.end(), address) == servers.end()) servers.push_back(address);
    }
    return servers;
}

static std::optional<DirectDnsAnswer> queryConfiguredDnsA(const std::string& host) {
    if (!ensureWinsock() || host.empty()) return std::nullopt;
    const auto servers = configuredDnsServers();
    if (servers.empty()) return std::nullopt;

    static std::atomic<uint16_t> sequence{1};
    const uint16_t transaction = static_cast<uint16_t>(
        (GetTickCount64() & 0xffffu) ^ sequence.fetch_add(1, std::memory_order_relaxed));
    std::vector<unsigned char> request = {
        static_cast<unsigned char>(transaction >> 8), static_cast<unsigned char>(transaction & 0xff),
        0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    if (!dnsAppendName(request, host)) return std::nullopt;
    request.insert(request.end(), {0x00, 0x01, 0x00, 0x01});

    for (const auto& server : servers) {
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(53);
        if (InetPtonA(AF_INET, server.c_str(), &destination.sin_addr) != 1) continue;
        const SOCKET socketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET) continue;
        const int timeout = static_cast<int>(DNS_DIRECT_FALLBACK_TIMEOUT_MS);
        setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        const int sent = sendto(socketHandle, reinterpret_cast<const char*>(request.data()),
            static_cast<int>(request.size()), 0, reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
        if (sent != static_cast<int>(request.size())) {
            closesocket(socketHandle);
            continue;
        }

        std::array<unsigned char, 2048> responseBuffer{};
        sockaddr_in responseSource{};
        int responseSourceLength = sizeof(responseSource);
        const int received = recvfrom(socketHandle, reinterpret_cast<char*>(responseBuffer.data()),
            static_cast<int>(responseBuffer.size()), 0, reinterpret_cast<sockaddr*>(&responseSource), &responseSourceLength);
        closesocket(socketHandle);
        if (received < 12) continue;

        std::vector<unsigned char> response(responseBuffer.begin(), responseBuffer.begin() + received);
        if (dnsReadU16(response, 0) != transaction || (response[2] & 0x80) == 0 || (response[3] & 0x0f) != 0) continue;
        const uint16_t questions = dnsReadU16(response, 4);
        const uint16_t answers = dnsReadU16(response, 6);
        size_t offset = 12;
        bool malformed = false;
        for (uint16_t question = 0; question < questions; ++question) {
            if (!dnsSkipName(response, offset) || offset + 4 > response.size()) {
                malformed = true;
                break;
            }
            offset += 4;
        }
        if (malformed) continue;
        for (uint16_t answer = 0; answer < answers; ++answer) {
            if (!dnsSkipName(response, offset) || offset + 10 > response.size()) {
                malformed = true;
                break;
            }
            const uint16_t type = dnsReadU16(response, offset);
            const uint16_t klass = dnsReadU16(response, offset + 2);
            const uint16_t dataLength = dnsReadU16(response, offset + 8);
            offset += 10;
            if (offset + dataLength > response.size()) {
                malformed = true;
                break;
            }
            if (type == 1 && klass == 1 && dataLength == 4) {
                in_addr address{};
                std::memcpy(&address, response.data() + offset, sizeof(address));
                const uint32_t value = ntohl(address.s_addr);
                if ((value >> 24) != 0 && (value >> 24) != 127 && (value >> 24) < 224) {
                    char text[INET_ADDRSTRLEN]{};
                    if (InetNtopA(AF_INET, &address, text, static_cast<DWORD>(sizeof(text)))) {
                        return DirectDnsAnswer{text, server};
                    }
                }
            }
            offset += dataLength;
        }
        if (malformed) continue;
    }
    return std::nullopt;
}

static std::string replaceHttpsHost(const std::string& url,
                                    const std::string& expectedHost,
                                    const std::string& replacementHost) {
    const auto lowerUrl = asciiLower(url);
    const auto lowerPrefix = "https://" + asciiLower(expectedHost);
    if (lowerUrl.rfind(lowerPrefix, 0) != 0) return {};
    const auto boundary = lowerPrefix.size();
    if (url.size() > boundary && url[boundary] != '/' && url[boundary] != '?' && url[boundary] != '#') return {};
    return "https://" + replacementHost + url.substr(boundary);
}

static std::optional<SteamDirectIpFallback> directSteamIpFallbackForUrl(const std::string& url) {
    const auto wideUrl = toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        return std::nullopt;
    }
    const std::string host = toUtf8(std::wstring(parts.lpszHostName, parts.dwHostNameLength));
    if (!isSteamHost(toWide(host)) || !isLocalSteamRoute(steamRouteForUrl(url))) return std::nullopt;
    const auto answer = queryConfiguredDnsA(host);
    if (!answer) return std::nullopt;
    const auto rewritten = replaceHttpsHost(url, host, answer->address);
    if (rewritten.empty()) return std::nullopt;
    return SteamDirectIpFallback{rewritten, host, answer->address, answer->resolver};
}

// Steamcommunity 302 exposes several enabled CDN/store aliases in Hosts.
// Those aliases are the same transparent interface used by the main app's
// WebView video path.  Use them only after confirming that the alias really
// resolves to loopback; otherwise keep the original URL and use the normal
// Internet route.
static std::string steamAcceleratedRequestUrl(const std::string& url) {
    // Hashed store_item_assets URLs are the modern Steam asset route. Some
    // local alias forwarders return a false 404 for them while the canonical
    // shared.steamstatic.com URL succeeds. Once observed, remember that
    // route class and avoid paying the alias miss on the next worker run.
    if (preferDirectSteamStoreAssetRoute(url)) return url;
    if (isLocalSteamRoute(steamRouteForUrl(url))) return url;

    if (g_steamAcceleratorStatus.storeAliasLocal) {
        if (const auto rewritten = replaceHttpsHost(url, "store.steampowered.com",
                "steamstore-a.akamaihd.net"); !rewritten.empty()) return rewritten;
    }

    if (!g_steamAcceleratorStatus.cdnAliasLocal) return url;
    const std::array<std::string, 4> cdnHosts = {
        "shared.steamstatic.com",
        "shared.cloudflare.steamstatic.com",
        "shared.akamai.steamstatic.com",
        "cdn.cloudflare.steamstatic.com"
    };
    for (const auto& host : cdnHosts) {
        const auto rewritten = replaceHttpsHost(url, host, "steamcdn-a.akamaihd.net");
        if (rewritten.empty()) continue;
        const std::string sourcePrefix = "/store_item_assets/steam/apps/";
        const std::string oldPrefix = "https://steamcdn-a.akamaihd.net" + sourcePrefix;
        if (rewritten.rfind(oldPrefix, 0) == 0) {
            return "https://steamcdn-a.akamaihd.net/steam/apps/" +
                rewritten.substr(oldPrefix.size());
        }
        return rewritten;
    }
    return url;
}

static void markSteamRouteResult(const std::string& route, bool success) {
    if (route != "transparent-local") return;
    refreshSteamAcceleratorStatus();
    if (success) {
        g_steamAcceleratorStatus.localRequestSucceeded = true;
        g_steamAcceleratorStatus.localRequestFailed = false;
        if (g_steamAcceleratorStatus.state == "blocked") {
            g_steamAcceleratorStatus.state = "active";
            g_steamAcceleratorStatus.reason = "local Steam route recovered after a transient failure";
        }
    } else {
        g_steamAcceleratorStatus.localRequestFailed = true;
        g_steamAcceleratorStatus.state = "blocked";
        g_steamAcceleratorStatus.reason = "a request through the local Steam route failed or timed out";
    }
}

static json steamAcceleratorDiagnostics() {
    refreshSteamAcceleratorStatus();
    return {
        {"state", g_steamAcceleratorStatus.state},
        {"reason", g_steamAcceleratorStatus.reason},
        {"listener80", g_steamAcceleratorStatus.listener80},
        {"listener443", g_steamAcceleratorStatus.listener443},
        {"storeHostLocal", g_steamAcceleratorStatus.storeLocal},
        {"storeAliasHostLocal", g_steamAcceleratorStatus.storeAliasLocal},
        {"apiHostLocal", g_steamAcceleratorStatus.apiLocal},
        {"cdnHostLocal", g_steamAcceleratorStatus.cdnLocal},
        {"cdnAliasHostLocal", g_steamAcceleratorStatus.cdnAliasLocal},
        {"videoAliasHostLocal", g_steamAcceleratorStatus.videoAliasLocal},
        {"localRequestSucceeded", g_steamAcceleratorStatus.localRequestSucceeded},
        {"localRequestFailed", g_steamAcceleratorStatus.localRequestFailed},
        {"hashedStoreAssetPreferDirect", g_steamHashedStoreAssetPreferDirect},
        {"routeMemoryUpdatedAt", steamRouteMemoryUpdatedAt() > 0
            ? json(steamRouteMemoryUpdatedAt()) : json(nullptr)},
        {"routeMemoryTtlMs", STEAM_ROUTE_MEMORY_TTL_MS},
        {"legacyFixedAssetNegativeCacheTtlMs", ARTWORK_NEGATIVE_CACHE_TTL_MS},
        {"interface", "transparent-host-forwarding"},
        {"proxyMode", "not-http-connect"},
        {"requestPolicy", "use-enabled-steamstore/steamcdn-aliases; preserve-original-url-fallback"}
    };
}

static DWORD remainingHttpBudget(ULONGLONG deadline, DWORD maximum) {
    const auto now = GetTickCount64();
    if (now >= deadline) return 1;
    return static_cast<DWORD>((std::min)(deadline - now, static_cast<ULONGLONG>(maximum)));
}

static void setHttpTimeouts(HINTERNET handle, ULONGLONG deadline, bool acceleratorRoute = false) {
    DWORD resolveTimeout = remainingHttpBudget(deadline,
        acceleratorRoute ? HTTP_ACCELERATOR_RESOLVE_TIMEOUT_MS : HTTP_RESOLVE_TIMEOUT_MS);
    DWORD connectTimeout = remainingHttpBudget(deadline,
        acceleratorRoute ? HTTP_ACCELERATOR_CONNECT_TIMEOUT_MS : HTTP_CONNECT_TIMEOUT_MS);
    DWORD sendTimeout = remainingHttpBudget(deadline,
        acceleratorRoute ? HTTP_ACCELERATOR_TIMEOUT_MS : HTTP_TIMEOUT_MS);
    DWORD receiveTimeout = remainingHttpBudget(deadline,
        acceleratorRoute ? HTTP_ACCELERATOR_TIMEOUT_MS : HTTP_TIMEOUT_MS);
    WinHttpSetTimeouts(handle, resolveTimeout, connectTimeout, sendTimeout, receiveTimeout);
}

struct HttpResponse {
    DWORD status = 0;
    std::string headers;
    std::vector<unsigned char> body;
    uint64_t expectedBytes = 0;
    DWORD retryAfterMs = 0;
    std::string networkRoute = "direct";
    std::string finalUrl;
};

#ifdef CUSTOM_STEAM_LIBRARY_TEST_RUNTIME
// Test-only transport: no fixture is ever allowed to contact a live provider.
static std::function<HttpResponse(const std::string&, const std::string&, const std::string&, const std::vector<unsigned char>&)> artworkTestTransport;
#endif

class HttpTransportError : public std::runtime_error {
public:
    HttpTransportError(std::string message, DWORD code, bool retryable)
        : std::runtime_error(std::move(message)), code_(code), retryable_(retryable) {}
    DWORD code() const { return code_; }
    bool retryable() const { return retryable_; }
private:
    DWORD code_;
    bool retryable_;
};

struct InternetHandle {
    HINTERNET value = nullptr;
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET h) : value(h) {}
    ~InternetHandle() { reset(); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    operator HINTERNET() const { return value; }

    void reset() {
        if (value) WinHttpCloseHandle(value);
        value = nullptr;
    }
};

static bool transientHttpStatus(DWORD status) {
    return status == 408 || status == 425 || status == 429 ||
           status == 500 || status == 502 || status == 503 || status == 504;
}

static std::string winHttpError(const char* stage, DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD chars = FormatMessageW(flags, nullptr, code, 0,
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string detail;
    if (chars && buffer) detail = trim(toUtf8(std::wstring(buffer, chars)));
    if (buffer) LocalFree(buffer);
    return std::string(stage) + " failed (win32=" + std::to_string(code) +
        (detail.empty() ? ")" : ", " + detail + ")");
}

static bool retryableWinHttpError(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT:
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
        case ERROR_WINHTTP_RESEND_REQUEST:
        case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR:
        case ERROR_WINHTTP_AUTODETECTION_FAILED:
        case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
        case ERROR_WINHTTP_INCORRECT_HANDLE_STATE:
            return true;
        default:
            return false;
    }
}

[[noreturn]] static void throwWinHttp(const char* stage, DWORD code = GetLastError()) {
    throw HttpTransportError(winHttpError(stage, code), code, retryableWinHttpError(code));
}

struct ReusableHttpSession {
    InternetHandle handle;
    uint64_t generation = 0;
};

// Each worker process performs its requests serially. Keeping one session per
// worker lets WinHTTP pool the HTTPS connection while retaining the original
// Steam hostname/SNI required by the local transparent accelerator.
static thread_local ReusableHttpSession g_reusableHttpSession;

static HINTERNET acquireReusableHttpSession() {
    if (!g_reusableHttpSession.handle.value) {
        g_reusableHttpSession.handle.value = WinHttpOpen(
            L"QQ/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!g_reusableHttpSession.handle.value) throwWinHttp("WinHttpOpen");
        ++g_reusableHttpSession.generation;
    }
    return g_reusableHttpSession.handle.value;
}

static void resetReusableHttpSession() {
    g_reusableHttpSession.handle.reset();
}

static std::wstring queryHttpHeader(HINTERNET request, DWORD query) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
        nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
        value.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) return {};
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

static DWORD parseRetryAfterMs(const std::wstring& raw) {
    const auto value = trim(toUtf8(raw));
    if (value.empty()) return 0;
    if (std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        try {
            const auto seconds = std::stoull(value);
            const auto capSeconds = static_cast<unsigned long long>(HTTP_MAX_RETRY_AFTER_MS) / 1000ull;
            if (seconds >= capSeconds) return HTTP_MAX_RETRY_AFTER_MS;
            return static_cast<DWORD>(seconds * 1000ull);
        } catch (...) {
            return HTTP_MAX_RETRY_AFTER_MS;
        }
    }
    SYSTEMTIME target{};
    if (!WinHttpTimeToSystemTime(raw.c_str(), &target)) return 0;
    FILETIME targetFile{}, nowFile{};
    if (!SystemTimeToFileTime(&target, &targetFile)) return 0;
    GetSystemTimeAsFileTime(&nowFile);
    ULARGE_INTEGER targetValue{}, nowValue{};
    targetValue.LowPart = targetFile.dwLowDateTime;
    targetValue.HighPart = targetFile.dwHighDateTime;
    nowValue.LowPart = nowFile.dwLowDateTime;
    nowValue.HighPart = nowFile.dwHighDateTime;
    if (targetValue.QuadPart <= nowValue.QuadPart) return 0;
    const auto milliseconds = (targetValue.QuadPart - nowValue.QuadPart) / 10000ull;
    return static_cast<DWORD>((std::min)(milliseconds,
        static_cast<unsigned long long>(HTTP_MAX_RETRY_AFTER_MS)));
}

static bool trustedSteamMetadataFinalUrl(const std::string& requested, const std::string& finalUrl) {
    if (requested.find("/api/appdetails") == std::string::npos) return true;
    const auto wide = toWide(finalUrl);
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength =
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return false;
    const auto host = asciiLower(toUtf8(std::wstring(parts.lpszHostName, parts.dwHostNameLength)));
    const auto path = toUtf8(std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength));
    if (path != "/api/appdetails") return false;
    return host == "store.steampowered.com" || host == "steamstore-a.akamaihd.net";
}

static HttpResponse httpRequest(
    const std::string& method,
    const std::string& url,
    const std::wstring& headers,
    const std::vector<unsigned char>& requestBody,
    size_t maxBytes,
    bool imageRequest,
    const std::string& imageReferer,
    ULONGLONG deadline,
    const std::string& forcedSteamHost = {}) {
#ifdef CUSTOM_STEAM_LIBRARY_TEST_RUNTIME
    throw std::runtime_error("Live HTTP is forbidden by the isolated test runtime");
#endif
    if (GetTickCount64() >= deadline) {
        throw HttpTransportError("HTTP operation budget expired", ERROR_WINHTTP_TIMEOUT, true);
    }
    const auto wideUrl = toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        throw HttpTransportError("Invalid URL", ERROR_INVALID_PARAMETER, false);
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTPS) {
        throw HttpTransportError("Only HTTPS is allowed", ERROR_INVALID_PARAMETER, false);
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring objectPath(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) objectPath.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool directIpFallback = !forcedSteamHost.empty();
    const bool steamHost = directIpFallback || isSteamHost(host);
    const std::string networkRoute = directIpFallback ? "direct-ip" :
        (steamHost ? steamRouteForUrl(url) : "direct");
    const bool acceleratorRoute = isLocalSteamRoute(networkRoute);

    // S302/Steamcommunity 302 is a transparent host-forwarder, not an HTTP
    // CONNECT proxy. Keep direct WinHTTP, preserve the Steam host/SNI, and let
    // the OS hosts mapping route store/CDN requests through 127.0.0.1:80/443.
    const HINTERNET session = acquireReusableHttpSession();
    setHttpTimeouts(session, deadline, acceleratorRoute);
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy))) {
        throwWinHttp("WinHttpSetOption(redirect)");
    }

    InternetHandle connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
    if (!connection.value) throwWinHttp("WinHttpConnect");
    InternetHandle request(WinHttpOpenRequest(
        connection,
        toWide(method).c_str(),
        objectPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request.value) throwWinHttp("WinHttpOpenRequest");
    setHttpTimeouts(request, deadline, acceleratorRoute);

    if (acceleratorRoute || directIpFallback) {
        // The transparent local accelerator has a private TLS endpoint.
        // Normal public Steam traffic must retain system certificate checks;
        // an IP bypass needs only the name exception for its original Host.
        DWORD securityFlags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        if (acceleratorRoute) securityFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags))) {
            throwWinHttp("WinHttpSetOption(security)");
        }
    }

    std::wstring finalHeaders = headers;
    finalHeaders += L"Accept-Encoding: identity\r\nConnection: keep-alive\r\n";
    finalHeaders +=
        L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/131.0 Safari/537.36\r\n";
    if (directIpFallback) {
        // WinHTTP connects to the public A record to bypass a broken local
        // Hosts forwarder. Steam still receives its original virtual host;
        // certificate-name checks are already deliberately relaxed for the
        // transparent Steam accelerator path below.
        finalHeaders += L"Host: " + toWide(forcedSteamHost) + L"\r\n";
    }
    if (!imageReferer.empty()) finalHeaders += L"Referer: " + toWide(imageReferer) + L"\r\n";
    if (!finalHeaders.empty() && !WinHttpAddRequestHeaders(request, finalHeaders.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD)) {
        throwWinHttp("WinHttpAddRequestHeaders");
    }
    if (requestBody.size() > (std::numeric_limits<DWORD>::max)()) {
        throw HttpTransportError("HTTP request body is too large", ERROR_INVALID_DATA, false);
    }
    LPVOID bodyPointer = requestBody.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<unsigned char*>(requestBody.data());
    const DWORD bodyBytes = static_cast<DWORD>(requestBody.size());
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyPointer, bodyBytes, bodyBytes, 0)) {
        throwWinHttp("WinHttpSendRequest");
    }
    if (!WinHttpReceiveResponse(request, nullptr)) throwWinHttp("WinHttpReceiveResponse");

    HttpResponse response;
    response.networkRoute = networkRoute;
    DWORD urlBytes = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &urlBytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || urlBytes < sizeof(wchar_t))
        throw HttpTransportError("Cannot validate final HTTP URL", ERROR_INVALID_DATA, false);
    std::wstring finalUrl(urlBytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, finalUrl.data(), &urlBytes))
        throwWinHttp("WinHttpQueryOption(final URL)");
    while (!finalUrl.empty() && finalUrl.back() == L'\0') finalUrl.pop_back();
    response.finalUrl = toUtf8(finalUrl);
    // A direct-IP route is trusted only while it still uses the original
    // request address. A redirect receives the same host/path check as HTTPS.
    const auto checkedUrl = directIpFallback && response.finalUrl == url
        ? "https://" + forcedSteamHost + toUtf8(objectPath)
        : response.finalUrl;
    if (!imageRequest && !trustedSteamMetadataFinalUrl(url, checkedUrl))
        throw HttpTransportError("Steam AppDetails redirected outside the trusted store endpoint", ERROR_INVALID_DATA, false);
    response.finalUrl = checkedUrl;
    DWORD statusSize = sizeof(response.status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        throwWinHttp("WinHttpQueryHeaders(status)");
    }
    markSteamRouteResult(networkRoute, true);
    response.retryAfterMs = parseRetryAfterMs(queryHttpHeader(request, WINHTTP_QUERY_RETRY_AFTER));
    DWORD expected = 0;
    DWORD expectedSize = sizeof(expected);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &expected, &expectedSize, WINHTTP_NO_HEADER_INDEX)) {
        response.expectedBytes = expected;
        if (response.expectedBytes > maxBytes) {
            throw HttpTransportError("HTTP response declared a size above the limit", ERROR_INVALID_DATA, false);
        }
    }

    DWORD headerBytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &headerBytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && headerBytes >= sizeof(wchar_t)) {
        std::wstring raw(headerBytes / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
            WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &headerBytes, WINHTTP_NO_HEADER_INDEX)) {
            response.headers = toUtf8(raw);
        }
    }

    // Error bodies are not used by the resolver. Closing immediately avoids
    // spending the weak-network budget on a large proxy/CDN error page.
    if (response.status < 200 || response.status >= 300) return response;

    for (;;) {
        if (GetTickCount64() >= deadline) {
            throw HttpTransportError("HTTP response operation budget expired", ERROR_WINHTTP_TIMEOUT, true);
        }
        if (response.expectedBytes > 0 && response.body.size() >= response.expectedBytes) break;
        setHttpTimeouts(request, deadline, acceleratorRoute);
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) throwWinHttp("WinHttpQueryDataAvailable");
        if (available == 0) {
            if (response.expectedBytes > response.body.size()) {
                throw HttpTransportError(
                    "HTTP response ended early (received " + std::to_string(response.body.size()) +
                    ", expected " + std::to_string(response.expectedBytes) + ")",
                    ERROR_WINHTTP_CONNECTION_ERROR,
                    true);
            }
            break;
        }
        size_t readSize = (std::min)(static_cast<size_t>(available), static_cast<size_t>(1u << 20));
        if (response.expectedBytes > response.body.size()) {
            readSize = (std::min)(readSize, static_cast<size_t>(response.expectedBytes - response.body.size()));
        }
        if (response.body.size() + readSize > maxBytes) {
            throw HttpTransportError("HTTP response exceeded size limit", ERROR_INVALID_DATA, false);
        }
        const size_t oldSize = response.body.size();
        response.body.resize(oldSize + readSize);
        DWORD read = 0;
        setHttpTimeouts(request, deadline, acceleratorRoute);
        if (!WinHttpReadData(request, response.body.data() + oldSize,
            static_cast<DWORD>(readSize), &read)) throwWinHttp("WinHttpReadData");
        response.body.resize(oldSize + read);
        if (read == 0 && response.expectedBytes > response.body.size()) {
            throw HttpTransportError("WinHttpReadData returned no data before completion",
                ERROR_WINHTTP_CONNECTION_ERROR, true);
        }
        if (response.expectedBytes > 0 && response.body.size() > response.expectedBytes) {
            throw HttpTransportError("HTTP response exceeded declared Content-Length", ERROR_INVALID_DATA, true);
        }
    }
    if (response.expectedBytes > 0 && response.body.size() != response.expectedBytes) {
        throw HttpTransportError("HTTP response size mismatch", ERROR_WINHTTP_CONNECTION_ERROR, true);
    }
    return response;
}

static HttpResponse httpRequestWithRetry(
    const std::string& provider,
    const std::string& method,
    const std::string& url,
    const std::wstring& headers,
    const std::vector<unsigned char>& requestBody,
    size_t maxBytes,
    bool imageRequest,
    const std::string& imageReferer,
    json* attemptLog = nullptr) {
    std::string lastError;
    const ULONGLONG startedAll = GetTickCount64();
    const bool fastMetadata = g_fastMetadataMode && !imageRequest;
    const std::string acceleratedUrl = steamAcceleratedRequestUrl(url);
    std::string requestUrl = acceleratedUrl;
    std::string forcedSteamHost;
    const bool acceleratorCandidate = acceleratedUrl != url;
    const bool acceleratorRoute = isLocalSteamRoute(steamRouteForUrl(acceleratedUrl));
    bool acceleratorFallbackUsed = false;
    bool directIpFallbackUsed = false;
    std::optional<SteamDirectIpFallback> directIpFallback;
    const int maxAttempts = fastMetadata || acceleratorRoute ? 2 : HTTP_MAX_ATTEMPTS;
    const ULONGLONG deadline = startedAll + (imageRequest
        ? (acceleratorRoute ? HTTP_ACCELERATOR_IMAGE_BUDGET_MS : HTTP_IMAGE_BUDGET_MS)
        : (fastMetadata ? 20000ull : (acceleratorRoute
            ? HTTP_ACCELERATOR_METADATA_BUDGET_MS : HTTP_METADATA_BUDGET_MS)));
    auto scheduleSteamFallback = [&](json& trace, const std::string& reason) {
        const auto canonicalRoute = steamRouteForUrl(url);
        if (directIpFallbackUsed && !forcedSteamHost.empty()) return false;
        if (!directIpFallbackUsed && isLocalSteamRoute(canonicalRoute)) {
            directIpFallbackUsed = true;
            directIpFallback = directSteamIpFallbackForUrl(url);
            if (directIpFallback) {
                requestUrl = directIpFallback->requestUrl;
                forcedSteamHost = directIpFallback->originalHost;
                trace["acceleratorFallback"] = true;
                trace["fallbackMode"] = "direct-ip-bypass-hosts";
                trace["fallbackUrl"] = requestUrl;
                trace["fallbackHost"] = forcedSteamHost;
                trace["fallbackAddress"] = directIpFallback->address;
                trace["fallbackDnsServer"] = directIpFallback->resolver;
                trace["reconnectScheduled"] = true;
                trace["retryDelayMs"] = 0;
                trace["retryReason"] = reason;
                return true;
            }
            trace["directIpFallbackUnavailable"] = true;
        }
        if (requestUrl == url) return false;
        requestUrl = url;
        forcedSteamHost.clear();
        acceleratorFallbackUsed = true;
        trace["acceleratorFallback"] = true;
        trace["fallbackMode"] = "canonical-url";
        trace["fallbackUrl"] = url;
        trace["reconnectScheduled"] = true;
        trace["retryDelayMs"] = 0;
        trace["retryReason"] = reason;
        return true;
    };
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        const bool sessionWasAvailable = g_reusableHttpSession.handle.value != nullptr;
        json trace = {
            {"attempt", attempt},
            {"provider", provider},
            {"method", method},
            {"url", url},
            {"requestUrl", requestUrl},
            {"acceleratorCandidate", acceleratorCandidate},
            {"freshSession", !sessionWasAvailable},
            {"sessionReused", sessionWasAvailable},
            {"connectionGeneration", g_reusableHttpSession.generation},
            {"networkRoute", forcedSteamHost.empty() ? steamRouteForUrl(requestUrl) : "direct-ip"},
            {"acceleratorHealth", steamAcceleratorDiagnostics()},
            {"weakNetworkProfile", true}
        };
        const ULONGLONG started = GetTickCount64();
        if (started >= deadline) {
            trace["ok"] = false;
            trace["budgetExhausted"] = true;
            trace["elapsedMs"] = started - startedAll;
            if (attemptLog) attemptLog->push_back(trace);
            throw std::runtime_error(provider + " request exhausted its weak-network budget");
        }
        try {
            auto consumeInjectedFailure = [&](std::map<std::string, int>& failures) {
                auto fault = failures.find(provider);
                if (fault == failures.end() && provider.rfind("steam-", 0) == 0) {
                    fault = failures.find("steam");
                }
                if (fault == failures.end() || fault->second <= 0) return false;
                --fault->second;
                return true;
            };
            if ((imageRequest && consumeInjectedFailure(g_injectedArtworkNetworkFailures)) ||
                consumeInjectedFailure(g_injectedNetworkFailures)) {
                trace["injectedDisconnect"] = true;
                trace["injectedStage"] = imageRequest ? "artwork" : "metadata";
                throw HttpTransportError("Injected weak-network connection drop",
                    ERROR_WINHTTP_CONNECTION_ERROR, true);
            }
#ifdef CUSTOM_STEAM_LIBRARY_TEST_RUNTIME
            if (!artworkTestTransport) throw std::runtime_error("Fixture HTTP transport is not configured");
            auto response = artworkTestTransport(provider, method, requestUrl, requestBody);
            trace["fixture"] = true;
            if (response.body.size() > maxBytes || response.expectedBytes > maxBytes)
                throw HttpTransportError("Fixture HTTP response exceeded size limit", ERROR_INVALID_DATA, false);
#else
            auto response = httpRequest(
                method, requestUrl, headers, requestBody, maxBytes, imageRequest, imageReferer, deadline,
                forcedSteamHost);
#endif
            if (provider == "steam" && !imageRequest &&
                !trustedSteamMetadataFinalUrl(url, response.finalUrl.empty() ? url : response.finalUrl))
                throw HttpTransportError("Steam AppDetails redirected outside the trusted store endpoint", ERROR_INVALID_DATA, false);
            trace["status"] = response.status;
            trace["bytes"] = response.body.size();
            trace["expectedBytes"] = response.expectedBytes;
            trace["elapsedMs"] = GetTickCount64() - started;
            trace["ok"] = response.status >= 200 && response.status < 300;
            // A transparent route can return a false CDN 404 or keep a
            // broken TLS forwarder alive. In either case, leaving the mapped
            // hostname is required; otherwise the apparent fallback repeats
            // the same localhost request on machines with a full Hosts map.
            if (response.status >= 300 && attempt < maxAttempts &&
                scheduleSteamFallback(trace, "Steam accelerator route returned HTTP " + std::to_string(response.status))) {
                if (attemptLog) attemptLog->push_back(trace);
                resetReusableHttpSession();
                continue;
            }
            if (acceleratorFallbackUsed && requestUrl == url &&
                response.status >= 200 && response.status < 300) {
                rememberDirectSteamStoreAssetRoute(url);
            }
            if (!transientHttpStatus(response.status) || attempt == maxAttempts) {
                if (attemptLog) attemptLog->push_back(trace);
                return response;
            }
            lastError = "HTTP " + std::to_string(response.status);
            const DWORD exponential = (std::min)(600u << (attempt - 1), 8000u);
            const DWORD jitter = static_cast<DWORD>((GetTickCount64() + attempt * 97u) % 251u);
            const DWORD retryDelay = (std::max)(exponential + jitter, response.retryAfterMs);
            trace["retryAfterMs"] = response.retryAfterMs;
            trace["reconnectScheduled"] = true;
            trace["retryDelayMs"] = retryDelay;
            trace["retryReason"] = lastError;
            if (GetTickCount64() + retryDelay >= deadline) {
                trace["reconnectScheduled"] = false;
                trace["retryAborted"] = "weak-network-budget";
                if (attemptLog) attemptLog->push_back(trace);
                return response;
            }
            if (attemptLog) attemptLog->push_back(trace);
            Sleep(retryDelay);
            continue;
        } catch (const HttpTransportError& error) {
            lastError = error.what();
            trace["ok"] = false;
            trace["error"] = lastError;
            trace["win32Error"] = error.code();
            trace["retryable"] = error.retryable();
            trace["elapsedMs"] = GetTickCount64() - started;
            markSteamRouteResult(trace.value("networkRoute", "direct"), false);
            if (error.retryable() && attempt < maxAttempts &&
                scheduleSteamFallback(trace, "Steam accelerator route failed; selecting a separate route")) {
                if (attemptLog) attemptLog->push_back(trace);
                resetReusableHttpSession();
                continue;
            }
            if (!error.retryable() || attempt == maxAttempts) {
                if (attemptLog) attemptLog->push_back(trace);
                throw;
            }
            // A failed WinHTTP connection can retain unusable state. Rebuild
            // only for transport retries; normal CDN candidates keep the
            // pooled connection for speed.
            resetReusableHttpSession();
        } catch (const std::exception& error) {
            lastError = error.what();
            trace["ok"] = false;
            trace["error"] = lastError;
            trace["retryable"] = false;
            trace["elapsedMs"] = GetTickCount64() - started;
            if (attemptLog) attemptLog->push_back(trace);
            throw;
        }

        const DWORD exponential = (std::min)(600u << (attempt - 1), 8000u);
        const DWORD jitter = static_cast<DWORD>((GetTickCount64() + attempt * 97u) % 251u);
        const DWORD retryDelay = exponential + jitter;
        trace["reconnectScheduled"] = true;
        trace["retryDelayMs"] = retryDelay;
        trace["retryReason"] = lastError;
        if (attemptLog) attemptLog->push_back(trace);
        if (GetTickCount64() + retryDelay >= deadline) {
            throw std::runtime_error(provider + " reconnect would exceed its weak-network budget");
        }
        Sleep(retryDelay);
    }
    throw std::runtime_error(provider + " request failed: " + lastError);
}

static HttpResponse httpGet(
    const std::string& url,
    const std::wstring& headers,
    size_t maxBytes,
    bool imageRequest,
    const std::string& provider = "steam",
    const std::string& imageReferer = "https://store.steampowered.com/",
    json* attemptLog = nullptr) {
    return httpRequestWithRetry(
        provider, "GET", url, headers, {}, maxBytes, imageRequest, imageReferer, attemptLog);
}

// A transparent forwarder can return HTTP 200 with an HTML error page or a
// stale non-image payload. Those are semantic failures, so transport retry
// alone cannot detect them. Reissue the original Steam request through the
// public DNS answer while preserving its Host header.
static std::optional<HttpResponse> retrySteamResponseAfterSemanticFailure(
    const std::string& url,
    const std::wstring& headers,
    size_t maxBytes,
    bool imageRequest,
    const std::string& imageReferer,
    const std::string& reason,
    json* attemptLog = nullptr) {
    const auto fallback = directSteamIpFallbackForUrl(url);
    if (!fallback) return std::nullopt;
    json trace = {
        {"semanticFallback", true},
        {"fallbackMode", "direct-ip-bypass-hosts"},
        {"fallbackHost", fallback->originalHost},
        {"fallbackAddress", fallback->address},
        {"fallbackDnsServer", fallback->resolver},
        {"requestUrl", fallback->requestUrl},
        {"networkRoute", "direct-ip"},
        {"reason", reason}
    };
    try {
        const auto budget = imageRequest ? HTTP_ACCELERATOR_IMAGE_BUDGET_MS :
            HTTP_ACCELERATOR_METADATA_BUDGET_MS;
        auto response = httpRequest("GET", fallback->requestUrl, headers, {}, maxBytes,
            imageRequest, imageReferer, GetTickCount64() + budget, fallback->originalHost);
        trace["status"] = response.status;
        trace["bytes"] = response.body.size();
        trace["ok"] = response.status >= 200 && response.status < 300;
        if (attemptLog) attemptLog->push_back(trace);
        return response;
    } catch (const std::exception& error) {
        trace["ok"] = false;
        trace["error"] = error.what();
        if (attemptLog) attemptLog->push_back(trace);
        return std::nullopt;
    }
}

static std::string percentEncode(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out << static_cast<char>(ch);
        else out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
    return out.str();
}

static json getJson(const std::string& url, const std::string& provider = "steam", json* attemptLog = nullptr) {
    std::wstring headers = L"Accept: application/json\r\n";
    auto response = httpGet(url, headers, MAX_METADATA_BYTES, false, provider, {}, attemptLog);
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error(provider + " HTTP " + std::to_string(response.status));
    }
    try {
        return json::parse(response.body.begin(), response.body.end());
    } catch (const std::exception& firstError) {
        const auto fallback = retrySteamResponseAfterSemanticFailure(
            url, headers, MAX_METADATA_BYTES, false, {}, "HTTP success response was not valid JSON", attemptLog);
        if (fallback && fallback->status >= 200 && fallback->status < 300) {
            return json::parse(fallback->body.begin(), fallback->body.end());
        }
        throw std::runtime_error(provider + " returned invalid JSON: " + firstError.what());
    }
}

static json postJson(
    const std::string& url,
    const json& payload,
    const std::string& provider,
    json* attemptLog = nullptr) {
    const auto text = payload.dump();
    const std::vector<unsigned char> body(text.begin(), text.end());
    std::wstring headers =
        L"Accept: application/json\r\n"
        L"Content-Type: application/json; charset=utf-8\r\n";
    const auto response = httpRequestWithRetry(
        provider, "POST", url, headers, body, MAX_METADATA_BYTES, false, {}, attemptLog);
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error(provider + " HTTP " + std::to_string(response.status));
    }
    return json::parse(response.body.begin(), response.body.end());
}

static std::string sanitizedVersionTableValue(const wchar_t* value, UINT valueChars) {
    if (!value || valueChars == 0) return {};
    // VerQueryValueW normally reports a NUL-terminated character count, but a
    // handful of legacy installers have malformed translation tables and make
    // the count span adjacent binary data.  Stop at the first NUL/control
    // character and cap the walk so corrupt metadata cannot leak into JSON.
    const size_t limit = (std::min)(static_cast<size_t>(valueChars), static_cast<size_t>(8192));
    size_t length = 0;
    while (length < limit && value[length] != L'\0') {
        const wchar_t ch = value[length];
        if (ch < 0x20 && ch != L'\t') break;
        ++length;
    }
    if (length == 0) return {};
    return collapseSpaces(toUtf8(std::wstring(value, length)));
}

static std::map<std::string, std::string> versionStrings(const fs::path& exePath) {
    std::map<std::string, std::string> result;
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &ignored);
    if (!size) return result;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, size, data.data())) return result;

    struct Translation { WORD language; WORD codePage; };
    Translation* translations = nullptr;
    UINT translationBytes = 0;
    // Some PE files omit VarFileInfo\Translation even though localized
    // StringFileInfo tables are present. Probe the Windows languages used by
    // the library UI, with Unicode first and the common legacy code page as a
    // fallback. Missing tables are harmless: VerQueryValue simply returns 0.
    std::vector<Translation> fallback = {
        {0x0409, 0x04B0}, {0x0409, 0x04E4}, // English: Unicode / Windows-1252
        {0x0804, 0x04B0}, {0x0804, 0x03A8}, // Simplified Chinese: Unicode / 936
        {0x0404, 0x04B0}, {0x0404, 0x03B6}, // Traditional Chinese: Unicode / 950
        {0x0411, 0x04B0}, {0x0411, 0x03A4}, // Japanese: Unicode / 932
        {0x0419, 0x04B0}, {0x0419, 0x04E3}, // Russian: Unicode / Windows-1251
        {0x0412, 0x04B0}, {0x0412, 0x03B5}  // Korean: Unicode / 949
    };
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translationBytes) ||
        translationBytes < sizeof(Translation)) {
        translations = fallback.data();
        translationBytes = static_cast<UINT>(fallback.size() * sizeof(Translation));
    }

    const std::array<std::wstring, 6> keys = {
        L"ProductName", L"FileDescription", L"InternalName", L"OriginalFilename",
        L"CompanyName", L"LegalCopyright"
    };
    const size_t count = translationBytes / sizeof(Translation);
    for (const auto& key : keys) {
        for (size_t i = 0; i < count; ++i) {
            wchar_t query[128]{};
            swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", translations[i].language, translations[i].codePage, key.c_str());
            wchar_t* value = nullptr;
            UINT valueChars = 0;
            if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &valueChars) && value && valueChars > 0) {
                const auto cleaned = sanitizedVersionTableValue(value, valueChars);
                if (!cleaned.empty()) {
                    result[toUtf8(key)] = cleaned;
                    break;
                }
            }
        }
    }
    return result;
}

struct NameRound {
    int round = 0;
    std::string source;
    std::string value;
    std::string skipReason;
};

static bool noisyDirectory(const std::string& value) {
    const auto key = normalizeTitle(value);
    static const std::unordered_set<std::string> noise = {
        "bin", "bin64", "x64", "win64", "win32", "binaries", "binary", "data", "pc", "game", "games"
    };
    if (noise.count(key)) return true;
    return !key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char ch) { return std::isdigit(ch); });
}

// Strip only recognized separated distribution suffixes, never editions or
// sequel numbers. A P2P suffix was scoring as part of the installed title.
static std::string installedDirectoryTitle(const std::string& raw) {
    try { return trim(std::regex_replace(raw,
        std::regex(R"((?:[-_. ]+)(?:P2P|REPACK|RUNE|TENOKE|SKIDROW|CODEX|FLT)$)", std::regex::icase), "")); }
    catch (...) { return raw; }
}

static std::vector<NameRound> buildNameRounds(
    const fs::path& exePath,
    const std::map<std::string, std::string>& version,
    bool simulateNoVersion,
    const std::string& manualName,
    const std::string& manualSource,
    const std::vector<std::pair<std::string, std::string>>& preferredEvidence = {}) {
    std::vector<NameRound> rounds;
    std::set<std::string> seen;
    int nextRound = 1;
    auto add = [&](const std::string& source, const std::string& raw) {
        const auto value = collapseSpaces(raw);
        const auto key = normalizeTitle(value);
        if (value.empty() || key.size() < 2) {
            rounds.push_back({nextRound, source, value, "missing"});
        } else if (!seen.insert(key).second) {
            rounds.push_back({nextRound, source, value, "duplicate"});
        } else {
            rounds.push_back({nextRound, source, value, {}});
        }
        ++nextRound;
    };

    auto findVersion = [&](const std::string& key) -> std::string {
        const auto it = version.find(key);
        return it == version.end() ? std::string{} : it->second;
    };
    if (!manualName.empty()) add(manualSource.empty() ? "manual-name" : manualSource, manualName);
    add("pe-product-name", simulateNoVersion ? std::string{} : findVersion("ProductName"));
    add("pe-file-description", simulateNoVersion ? std::string{} : findVersion("FileDescription"));
    for (const auto& [source, value] : preferredEvidence) add(source, value);

    // A directory normally describes the installed product more precisely
    // than a short launcher filename (WARNO NORTHAG vs WARNO.exe, or
    // Total War - SHOGUN 2 vs Shogun2.exe).  The resolver still evaluates all
    // rounds globally, but ordering the evidence this way also makes the audit
    // trail and provider query order reflect that specificity.
    auto current = exePath.parent_path();
    int depth = 0;
    while (!current.empty() && depth < 6) {
        const auto name = toUtf8(current.filename().wstring());
        if (!name.empty() && !noisyDirectory(name)) {
            const auto cleaned = installedDirectoryTitle(name);
            if (cleaned != name && !cleaned.empty()) add("parent-directory-clean-" + std::to_string(depth + 1), cleaned);
            add("parent-directory-" + std::to_string(depth + 1), name);
            add("parent-humanized-" + std::to_string(depth + 1), humanizeTitle(name));
        }
        current = current.parent_path();
        ++depth;
    }
    const auto stem = toUtf8(exePath.stem().wstring());
    add("exe-stem", stem);
    add("exe-humanized", humanizeTitle(stem));
    return rounds;
}

struct LocalSteamAppIdEvidence {
    int appId = 0;
    fs::path path;
    std::string key;
    double reliability = 0.0;
    std::string titleHint;
    std::string target;
    bool targetMatchesExecutable = false;
};

static std::vector<int> parseLocalSteamAppIds(
    const fs::path& path,
    std::string content,
    std::string& matchedKey) {
    content.erase(std::remove(content.begin(), content.end(), '\0'), content.end());
    const auto filename = asciiLower(toUtf8(path.filename().wstring()));
    std::vector<int> result;
    std::set<int> seen;
    auto collect = [&](const std::regex& expression, const std::string& key) {
        for (std::sregex_iterator it(content.begin(), content.end(), expression), end; it != end; ++it) {
            int value = 0;
            try { value = std::stoi((*it)[1].str()); } catch (...) { continue; }
            if (value > 0 && seen.insert(value).second) {
                result.push_back(value);
                if (matchedKey.empty()) matchedKey = key;
            }
        }
    };
    if (filename == "steam_appid.txt") {
        collect(std::regex(R"(^\s*([0-9]{3,10})\s*$)", std::regex_constants::icase), "plain-steam-appid");
    }
    collect(std::regex(
        R"((?:^|[\r\n])\s*(?:app_?id|steam_?app_?id)\s*[:=]\s*[\"']?([0-9]{3,10}))",
        std::regex_constants::icase), "named-appid-key");
    if (filename == "tenoke.ini" || filename == "rune.ini" || filename == "flt.ini") {
        collect(std::regex(
            R"((?:^|[\r\n])\s*id\s*=\s*([0-9]{3,10}))",
            std::regex_constants::icase), "provider-section-id");
    }
    return result;
}

static fs::path localIdentityRoot(const fs::path& executable) {
    auto root = executable.parent_path();
    for (int depth = 0; depth < 3 && !root.empty() && noisyDirectory(toUtf8(root.filename().wstring())); ++depth) {
        root = root.parent_path();
    }
    return root.empty() ? executable.parent_path() : root;
}

static std::vector<LocalSteamAppIdEvidence> discoverLocalSteamAppIds(
    const fs::path& executable, size_t traversalBudget = 4000) {
    const auto root = localIdentityRoot(executable);
    const auto executableDirectory = executable.parent_path();
    static const std::unordered_set<std::string> acceptedNames = {
        "steam_appid.txt", "steam_emu.ini", "smartsteamemu.ini", "steam_api.ini",
        "steam_api64.ini", "steamclient_loader.ini", "coldclientloader.ini",
        "tenoke.ini", "rune.ini", "flt.ini", "onlinefix.ini", "launcher-settings.json"
    };
    std::vector<fs::path> files;
    std::set<std::string> seenPaths;
    auto addFile = [&](const fs::path& path) {
        if (files.size() >= 32) return;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec) return;
        if (!pathWithin(path, root)) return;
        const auto key = unicodeLower(path.lexically_normal().wstring());
        if (seenPaths.insert(toUtf8(key)).second) files.push_back(path);
    };
    // Probe the small set of deterministic locations before the bounded tree
    // walk.  A very large install directory must not spend the traversal
    // budget before we inspect its authoritative launcher/AppID files.
    addFile(executableDirectory / L"steam_appid.txt");
    addFile(executableDirectory / L"steam_settings" / L"steam_appid.txt");
    addFile(root / L"steam_appid.txt");
    addFile(root / L"steam_settings" / L"steam_appid.txt");
    addFile(root / L"launcher-settings.json");
    addFile(root / L"launcher" / L"launcher-settings.json");

    std::error_code ec;
    fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, ec), end;
    size_t inspected = 0;
    for (; !ec && iterator != end && inspected < traversalBudget; iterator.increment(ec), ++inspected) {
        if (iterator.depth() > 2) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (!iterator->is_regular_file(ec)) continue;
        const auto filename = asciiLower(toUtf8(iterator->path().filename().wstring()));
        if (acceptedNames.count(filename)) addFile(iterator->path());
    }
    std::vector<LocalSteamAppIdEvidence> evidence;
    uintmax_t inspectedIdentityBytes = 0;
    for (const auto& path : files) {
        std::error_code sizeError;
        const auto size = fs::file_size(path, sizeError);
        if (sizeError || size == 0 || size > (2u << 20) ||
            inspectedIdentityBytes + size > (16u << 20)) continue;
        inspectedIdentityBytes += size;
        std::ifstream input(path, std::ios::binary);
        if (!input) continue;
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        content.erase(std::remove(content.begin(), content.end(), '\0'), content.end());
        const auto filename = asciiLower(toUtf8(path.filename().wstring()));
        if (filename == "launcher-settings.json") {
            const auto settings = json::parse(content, nullptr, false);
            if (!settings.is_object()) continue;
            const auto displayName = trim(settings.value("displayName", std::string{}));
            const auto configuredTarget = trim(settings.value("exePath", std::string{}));
            bool targetMatches = false;
            if (!configuredTarget.empty()) {
                const auto resolvedTarget = (path.parent_path() / fs::path(toWide(configuredTarget))).lexically_normal();
                targetMatches = unicodeLower(resolvedTarget.wstring()) ==
                    unicodeLower(executable.lexically_normal().wstring());
            }
            std::set<int> ids;
            try {
                const std::regex steamUrl(
                    R"((?:store\.steampowered\.com/(?:app|dlc)/|steamcommunity\.com/app/)([0-9]{3,10}))",
                    std::regex_constants::icase);
                for (std::sregex_iterator it(content.begin(), content.end(), steamUrl), end; it != end; ++it) {
                    try {
                        const auto appId = std::stoi((*it)[1].str());
                        if (appId > 0) ids.insert(appId);
                    } catch (...) {}
                }
            } catch (...) {}
            for (const auto appId : ids) {
                evidence.push_back({appId, path, "launcher-settings-steam-url", 0.995,
                    displayName, configuredTarget, targetMatches});
            }
            continue;
        }
        std::string matchedKey;
        const auto ids = parseLocalSteamAppIds(path, content, matchedKey);
        double reliability = filename == "steam_appid.txt" ? 1.0 :
            (filename == "steam_emu.ini" || filename == "smartsteamemu.ini" ? 0.99 : 0.97);
        if (path.parent_path() == executableDirectory) reliability = (std::max)(reliability, 0.99);
        std::string configuredTarget;
        try {
            const std::regex targetExpression(
                R"((?:^|[\r\n])\s*(?:target|executable|exe)\s*[:=]\s*[\"']?([^\r\n\"';#]+))",
                std::regex_constants::icase);
            std::smatch match;
            if (std::regex_search(content, match, targetExpression)) configuredTarget = trim(match[1].str());
        } catch (...) {}
        const auto configuredTargetName = asciiLower(toUtf8(fs::path(toWide(configuredTarget)).filename().wstring()));
        const auto executableName = asciiLower(toUtf8(executable.filename().wstring()));
        const bool targetMatches = !configuredTargetName.empty() && configuredTargetName == executableName;
        for (const int appId : ids) {
            std::string titleHint;
            try {
                const std::regex titleExpression(
                    "(?:^|[\\r\\n])\\s*(?:app_?id|steam_?app_?id|id)\\s*[:=]\\s*" +
                    std::to_string(appId) + R"([^\r\n#;]*[#;]\s*([^\r\n]+))",
                    std::regex_constants::icase);
                std::smatch match;
                if (std::regex_search(content, match, titleExpression)) titleHint = trim(match[1].str());
            } catch (...) {}
            evidence.push_back({appId, path, matchedKey, reliability, titleHint, configuredTarget, targetMatches});
        }
    }
    std::stable_sort(evidence.begin(), evidence.end(), [](const auto& left, const auto& right) {
        if (std::abs(left.reliability - right.reliability) > 0.0001) return left.reliability > right.reliability;
        return left.path.wstring().size() < right.path.wstring().size();
    });
    return evidence;
}

static json localSteamAppIdEvidenceJson(const std::vector<LocalSteamAppIdEvidence>& evidence) {
    json result = json::array();
    for (const auto& item : evidence) {
        result.push_back({
            {"appId", item.appId}, {"path", toUtf8(item.path.wstring())},
            {"key", item.key}, {"reliability", item.reliability},
            {"titleHint", item.titleHint.empty() ? json(nullptr) : json(item.titleHint)},
            {"target", item.target.empty() ? json(nullptr) : json(item.target)},
            {"targetMatchesExecutable", item.targetMatchesExecutable}
        });
    }
    return result;
}

static std::optional<int> unambiguousLocalSteamAppId(
    const std::vector<LocalSteamAppIdEvidence>& evidence) {
    std::set<int> ids;
    double highestReliability = 0.0;
    for (const auto& item : evidence) {
        if (item.appId <= 0) continue;
        ids.insert(item.appId);
        highestReliability = (std::max)(highestReliability, item.reliability);
    }
    if (ids.size() != 1 || highestReliability < 0.97) return std::nullopt;
    return *ids.begin();
}

static json assessLocalSteamAppIdIdentity(
    int appId,
    const std::vector<LocalSteamAppIdEvidence>& evidence,
    const fs::path& executable,
    const std::vector<NameRound>& rounds,
    const std::string& formalName) {
    std::set<int> ids;
    std::set<std::string> sourceFiles;
    double bestTitleHintScore = 0.0;
    double bestLocalNameScore = 0.0;
    double bestInstalledAliasScore = 0.0;
    bool targetMatchesExecutable = false;
    for (const auto& item : evidence) {
        ids.insert(item.appId);
        if (item.appId != appId) continue;
        sourceFiles.insert(toUtf8(item.path.lexically_normal().wstring()));
        if (!item.titleHint.empty()) {
            bestTitleHintScore = (std::max)(bestTitleHintScore, scoreTitle(item.titleHint, formalName));
        }
        targetMatchesExecutable = targetMatchesExecutable || item.targetMatchesExecutable;
    }
    for (const auto& round : rounds) {
        if (round.skipReason.empty()) {
            bestLocalNameScore = (std::max)(bestLocalNameScore, scoreTitle(round.value, formalName));
            bestInstalledAliasScore = (std::max)(bestInstalledAliasScore,
                scoreInstalledTitle(round.value, formalName));
        }
    }
    const bool conflict = ids.size() > 1;
    const bool repeatedIndependentSource = sourceFiles.size() >= 2 && bestLocalNameScore >= 0.78;
    const bool exactConfiguredTitle = bestTitleHintScore >= 0.94;
    const bool configTargetAndNameAgreement = targetMatchesExecutable && bestLocalNameScore >= 0.78;
    const bool directNameAgreement = bestLocalNameScore >= 0.94;
    const bool accepted = !conflict &&
        (repeatedIndependentSource || exactConfiguredTitle || configTargetAndNameAgreement || directNameAgreement);
    return {
        {"appId", appId}, {"sourceCount", sourceFiles.size()}, {"distinctAppIdCount", ids.size()},
        {"conflict", conflict}, {"bestTitleHintScore", bestTitleHintScore},
        {"bestLocalNameScore", bestLocalNameScore},
        {"bestInstalledAliasScore", bestInstalledAliasScore},
        {"targetMatchesExecutable", targetMatchesExecutable},
        {"repeatedIndependentSource", repeatedIndependentSource},
        {"accepted", accepted},
        {"reason", conflict ? "multiple-local-appids-conflict" :
            (repeatedIndependentSource ? "same-appid-repeated-across-independent-files" :
             (exactConfiguredTitle ? "configured-title-agrees-with-official-details" :
              (configTargetAndNameAgreement ? "configured-target-and-local-name-agree-with-official-details" :
               (directNameAgreement ? "local-name-agrees-with-official-details" :
                "single-local-appid-has-no-independent-name-or-source-corroboration"))))},
        {"executable", toUtf8(executable.wstring())}, {"formalName", formalName}
    };
}

struct ResolvedGame {
    int appId = 0;
    std::string name;
    std::string query;
    std::string source;
    double score = 0;
    json details;
    json baseGameDetails;
    bool confident = false;
    uint64_t igdbId = 0;
    std::string primaryProvider = "steam";
    json igdbDetails;
    std::string identityStatus = "unverified";
    // Identity evidence is deliberately split.  A verified IGDB record can
    // supply a title/artwork, but it is not a Steam AppID assertion and must
    // never make an item import-ready by itself.
    std::string steamVerificationStatus = "not-available";
    std::string metadataVerificationStatus = "not-available";
    int steamIdCandidate = 0;
    std::string identityKind = "unknown";
    std::string displayName;
    std::string contentType;
    int storefrontAppId = 0;
    int matchedContentAppId = 0;
    std::string matchedContentName;
    std::string matchedContentType;
    std::string contentRelation;
    int baseGameAppId = 0;
    std::string resolverVersion = STEAM_RESOLVER_VERSION;
};

// Keep network evidence close to the resolver so a successful metadata
// fallback can still describe an unavailable Steam verification path.  This
// distinction is important: metadata can be shown immediately, but it must
// remain retryable and must not silently become an import-ready Steam ID.
struct NetworkEvidence {
    size_t successfulResponses = 0;
    size_t retryableFailures = 0;
    size_t nonRetryableResponses = 0;
};

static void collectNetworkEvidence(const json& value, NetworkEvidence& evidence) {
    if (value.is_array()) {
        for (const auto& item : value) collectNetworkEvidence(item, evidence);
        return;
    }
    if (!value.is_object()) return;
    if (value.contains("attempt") && value.contains("freshSession")) {
        if (value.contains("status") && value["status"].is_number_integer()) {
            const auto status = value["status"].get<DWORD>();
            if (status >= 200 && status < 300) ++evidence.successfulResponses;
            else if (transientHttpStatus(status)) ++evidence.retryableFailures;
            else ++evidence.nonRetryableResponses;
        } else if (value.value("retryable", false) || value.contains("win32Error")) {
            ++evidence.retryableFailures;
        }
    }
    for (const auto& [_, item] : value.items()) collectNetworkEvidence(item, evidence);
}

static bool networkVerificationPending(const json& rounds) {
    NetworkEvidence evidence;
    collectNetworkEvidence(rounds, evidence);
    return evidence.successfulResponses == 0 && evidence.retryableFailures > 0;
}

static int positiveJsonInt(const json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) return 0;
    const auto& value = object[key];
    if (value.is_number_integer()) {
        const auto number = value.get<int64_t>();
        return number > 0 && number <= (std::numeric_limits<int>::max)()
            ? static_cast<int>(number) : 0;
    }
    if (value.is_string()) {
        try {
            const auto number = std::stoll(value.get<std::string>());
            return number > 0 && number <= (std::numeric_limits<int>::max)()
                ? static_cast<int>(number) : 0;
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

// Steam appdetails exposes the release date as release_date.date (for
// example, "19 Sep, 2017").  Keep this parser deliberately provider-neutral
// because the same normalized value is also used by IGDB/legacy manifests.
// A missing or non-date value remains 0: callers must not invent a year.
static int releaseYearFromValue(const json& value) {
    auto yearFromText = [](const std::string& raw) {
        for (size_t index = 0; index + 4 <= raw.size(); ++index) {
            if (!std::isdigit(static_cast<unsigned char>(raw[index])) ||
                !std::isdigit(static_cast<unsigned char>(raw[index + 1])) ||
                !std::isdigit(static_cast<unsigned char>(raw[index + 2])) ||
                !std::isdigit(static_cast<unsigned char>(raw[index + 3]))) continue;
            const int year = (raw[index] - '0') * 1000 + (raw[index + 1] - '0') * 100 +
                (raw[index + 2] - '0') * 10 + (raw[index + 3] - '0');
            if (year >= 1900 && year <= 2100) return year;
        }
        return 0;
    };
    if (value.is_number_integer() || value.is_number_unsigned()) {
        try {
            const auto number = value.get<int64_t>();
            if (number >= 1900 && number <= 2100) return static_cast<int>(number);
            // IGDB exposes first_release_date as Unix seconds. Convert only
            // values in a sane date range so AppIDs/scores are never treated
            // as years and so future/invalid timestamps stay empty.
            if (number >= 946684800 && number <= 4102444800) {
                const std::time_t timestamp = static_cast<std::time_t>(number);
                const auto* utc = std::gmtime(&timestamp);
                if (utc && utc->tm_year >= 0) return utc->tm_year + 1900;
            }
            return 0;
        } catch (...) { return 0; }
    }
    if (value.is_string()) return yearFromText(value.get<std::string>());
    if (value.is_array()) {
        for (const auto& item : value) {
            const int year = releaseYearFromValue(item);
            if (year > 0) return year;
        }
        return 0;
    }
    if (!value.is_object()) return 0;
    // Prefer explicit/canonical date fields before walking the object. This
    // prevents unrelated text in a metadata description from winning.
    for (const char* key : {"year", "releaseYear", "date", "display", "release_date", "releaseDate"}) {
        if (!value.contains(key)) continue;
        const int year = releaseYearFromValue(value[key]);
        if (year > 0) return year;
    }
    for (const auto& [_, item] : value.items()) {
        const int year = releaseYearFromValue(item);
        if (year > 0) return year;
    }
    return 0;
}

static std::string releaseDisplayFromValue(const json& value) {
    if (value.is_string()) return trim(value.get<std::string>());
    if (!value.is_object()) return {};
    for (const char* key : {"display", "date"}) {
        if (value.contains(key) && value[key].is_string()) return trim(value[key].get<std::string>());
    }
    for (const char* key : {"release_date", "releaseDate"}) {
        if (value.contains(key)) {
            const auto display = releaseDisplayFromValue(value[key]);
            if (!display.empty()) return display;
        }
    }
    return {};
}

struct SteamDetailsRecord {
    int requestedAppId = 0;
    int canonicalAppId = 0;
    std::string name;
    std::string type;
    json data;
};

static SteamDetailsRecord steamDetailsRecord(const json& response, int requestedAppId) {
    custom_steam_library::rejectBuiltinExcludedSteamTool({{"appId", requestedAppId}});
    if (requestedAppId <= 0 || !response.is_object())
        throw std::runtime_error("Steam AppDetails response is not an object for the requested AppID");
    const auto envelope = response.find(std::to_string(requestedAppId));
    if (envelope == response.end() || !envelope->is_object() ||
        !envelope->contains("success") || !(*envelope)["success"].is_boolean() ||
        !(*envelope)["success"].get<bool>() ||
        !envelope->contains("data") || !(*envelope)["data"].is_object()) {
        throw std::runtime_error("Steam AppID did not return a valid store record for the requested AppID");
    }
    SteamDetailsRecord result;
    result.requestedAppId = requestedAppId;
    result.data = (*envelope)["data"];
    // Missing/wrong-typed IDs cannot be repaired by echoing our request: that
    // would convert a malformed proxy/cache response into verified identity.
    if (!result.data.contains("steam_appid") || !result.data["steam_appid"].is_number_integer() ||
        (result.canonicalAppId = positiveJsonInt(result.data, "steam_appid")) <= 0)
        throw std::runtime_error("Steam AppID record has no valid canonical steam_appid");
    if (!result.data.contains("name") || !result.data["name"].is_string() ||
        !result.data.contains("type") || !result.data["type"].is_string())
        throw std::runtime_error("Steam AppID record has invalid title or content type");
    custom_steam_library::rejectBuiltinExcludedSteamTool(result.data);
    result.name = trim(result.data["name"].get<std::string>());
    result.type = asciiLower(trim(result.data["type"].get<std::string>()));
    if (result.name.empty() || result.name.size() > 4096 || result.type.empty() || result.type.size() > 64)
        throw std::runtime_error("Steam AppID has no valid formal title/content type");
    return result;
}

static std::optional<SteamDetailsRecord> loadSteamDetailsCache(int appId, uint64_t& fetchedAt);
static void saveSteamDetailsCache(const SteamDetailsRecord& details);

static SteamDetailsRecord fetchSteamDetails(int appId, json& attempts) {
    custom_steam_library::rejectBuiltinExcludedSteamTool({{"appId", appId}});
    uint64_t cachedAt = 0;
    if (const auto cached = loadSteamDetailsCache(appId, cachedAt)) {
        const auto now = unixTimeMs();
        if (cachedAt > 0 && now >= cachedAt && now - cachedAt <= STEAM_APPDETAILS_CACHE_TTL_MS) {
            attempts.push_back({
                {"stage", "appdetails-cache"}, {"provider", "steam"},
                {"appId", appId}, {"cacheHit", true}, {"ok", true},
                {"fetchedAt", cachedAt}
            });
            return *cached;
        }
    }
    const auto response = getJson(
        "https://store.steampowered.com/api/appdetails?appids=" + std::to_string(appId) + "&l=english&cc=us",
        "steam", &attempts);
    auto details = steamDetailsRecord(response, appId);
    if (details.canonicalAppId != appId) {
        // Steam storefront aliases are real, but a mismatched ID is not proof
        // by itself. Independently query the canonical ID and refuse chains,
        // cycles or disagreement before granting identity / saving the cache.
        const auto canonicalResponse = getJson(
            "https://store.steampowered.com/api/appdetails?appids=" + std::to_string(details.canonicalAppId) + "&l=english&cc=us",
            "steam", &attempts);
        const auto canonical = steamDetailsRecord(canonicalResponse, details.canonicalAppId);
        if (canonical.canonicalAppId != details.canonicalAppId || canonical.type != details.type ||
            asciiLower(canonical.name) != asciiLower(details.name))
            throw std::runtime_error("Steam storefront alias disagrees with its independently verified canonical record");
        attempts.push_back({{"stage", "appdetails-canonical-verification"}, {"requestedAppId", appId},
            {"canonicalAppId", canonical.canonicalAppId}, {"ok", true}});
        details = canonical;
        details.requestedAppId = appId;
    }
    saveSteamDetailsCache(details);
    return details;
}

static std::string decodeSteamHtml(std::string value) {
    const std::array<std::pair<const char*, const char*>, 7> entities = {{
        {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"},
        {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}
    }};
    for (const auto& [encoded, decoded] : entities) {
        size_t position = 0;
        while ((position = value.find(encoded, position)) != std::string::npos) {
            value.replace(position, std::strlen(encoded), decoded);
            position += std::strlen(decoded);
        }
    }
    try { value = std::regex_replace(value, std::regex("<[^>]*>"), " "); } catch (...) {}
    return collapseSpaces(value);
}

struct SteamOfficialSearchItem {
    int storefrontAppId = 0;
    std::string name;
    int officialRank = 0;
    std::string searchEndpoint;
    int canonicalHintAppId = 0;
    std::string language = "english";
    std::string country = "US";
};

struct SteamSearchLocale {
    std::string language;
    std::string country;
};

static std::vector<SteamSearchLocale> steamSearchLocales(const std::string& query) {
    bool han = false;
    bool kana = false;
    bool cyrillic = false;
    bool japanesePunctuation = false;
    bool hangul = false;
    for (const wchar_t ch : toWide(query)) {
        han = han || (ch >= 0x3400 && ch <= 0x9FFF) || (ch >= 0xF900 && ch <= 0xFAFF);
        kana = kana || (ch >= 0x3040 && ch <= 0x30FF) || (ch >= 0x31F0 && ch <= 0x31FF);
        cyrillic = cyrillic || (ch >= 0x0400 && ch <= 0x052F);
        japanesePunctuation = japanesePunctuation || ch == 0xFF65;
        hangul = hangul || (ch >= 0x1100 && ch <= 0x11FF) ||
            (ch >= 0x3130 && ch <= 0x318F) || (ch >= 0xAC00 && ch <= 0xD7AF);
    }
    std::vector<SteamSearchLocale> locales;
    auto add = [&](const char* language, const char* country) {
        if (std::none_of(locales.begin(), locales.end(), [&](const auto& item) {
                return item.language == language && item.country == country;
            })) locales.push_back({language, country});
    };
    if (kana || japanesePunctuation) add("japanese", "JP");
    if (han) {
        add("schinese", "CN");
        add("tchinese", "TW");
        add("japanese", "JP");
    }
    if (cyrillic) add("russian", "RU");
    if (hangul) add("koreana", "KR");
    add("english", "US");
    return locales;
}

static std::vector<SteamOfficialSearchItem> parseSteamResultsHtml(
    const json& response,
    const std::string& language = "english",
    const std::string& country = "US") {
    std::vector<SteamOfficialSearchItem> items;
    if (!response.is_object() || !response.contains("results_html") ||
        !response["results_html"].is_string()) return items;
    const auto html = response["results_html"].get<std::string>();
    try {
        const std::regex anchor(R"(<a\b([^>]*)>([\s\S]*?)</a>)", std::regex_constants::icase);
        const std::regex href(R"(href\s*=\s*[\"']([^\"']+)[\"'])", std::regex_constants::icase);
        const std::regex canonicalHint(R"(data-ds-appid\s*=\s*[\"']([0-9]+)[\"'])",
            std::regex_constants::icase);
        const std::regex appPath(R"((?:https?://store\.steampowered\.com)?/app/([0-9]+)(?:/|\?|$))",
            std::regex_constants::icase);
        const std::regex title(
            R"(<span\b[^>]*class=[\"'][^\"']*title[^\"']*[\"'][^>]*>([\s\S]*?)</span>)",
            std::regex_constants::icase);
        std::set<int> seen;
        int officialRank = 0;
        for (std::sregex_iterator iterator(html.begin(), html.end(), anchor), end;
             iterator != end; ++iterator) {
            const auto attributes = (*iterator)[1].str();
            std::smatch hrefMatch;
            if (!std::regex_search(attributes, hrefMatch, href)) continue;
            const auto target = decodeSteamHtml(hrefMatch[1].str());
            std::smatch appMatch;
            if (!std::regex_search(target, appMatch, appPath)) continue;
            int appId = 0;
            try { appId = std::stoi(appMatch[1].str()); } catch (...) { continue; }
            if (appId <= 0 || !seen.insert(appId).second) continue;
            const auto body = (*iterator)[2].str();
            std::smatch titleMatch;
            if (!std::regex_search(body, titleMatch, title)) continue;
            const auto name = decodeSteamHtml(titleMatch[1].str());
            int canonicalHintAppId = 0;
            std::smatch canonicalHintMatch;
            if (std::regex_search(attributes, canonicalHintMatch, canonicalHint)) {
                try { canonicalHintAppId = std::stoi(canonicalHintMatch[1].str()); } catch (...) {}
            }
            if (!name.empty()) items.push_back({appId, name, ++officialRank,
                "official-search-results-json", canonicalHintAppId, language, country});
        }
    } catch (...) {
        return {};
    }
    return items;
}

static std::string steamSearchFriendlyTitle(const std::string& raw) {
    std::wstring result;
    bool pendingSpace = false;
    for (wchar_t ch : toWide(raw)) {
        if (unicodeAlphaNumeric(ch)) {
            if (pendingSpace && !result.empty()) result.push_back(L' ');
            result.push_back(ch);
            pendingSpace = false;
        } else {
            pendingSpace = !result.empty();
        }
    }
    return collapseSpaces(toUtf8(result));
}

static double steamEvidenceReliability(const std::string& source) {
    if (source.rfind("manual", 0) == 0) return 1.0;
    if (source == "pe-product-name") return 0.98;
    if (source == "pe-file-description") return 0.95;
    if (source.rfind("shortcut", 0) == 0 || source.rfind("preferred", 0) == 0) return 0.94;
    if (source.rfind("local-launcher-settings", 0) == 0) return 0.99;
    if (source.rfind("parent-directory-", 0) == 0) return 0.96;
    if (source.rfind("parent-humanized-", 0) == 0) return 0.93;
    if (source.rfind("derived", 0) == 0) return 0.82;
    if (source == "exe-stem") return 0.68;
    if (source == "exe-humanized") return 0.64;
    return 0.80;
}

static int steamTrustedExactTier(const std::string& source, bool exact) {
    if (!exact) return 0;
    if (source.rfind("manual", 0) == 0) return 600;
    if (source.rfind("local-launcher-settings", 0) == 0) return 550;
    if (source == "pe-product-name") return 500;
    if (source == "pe-file-description") return 480;
    if (source.rfind("shortcut", 0) == 0 || source.rfind("preferred", 0) == 0) return 450;
    if (source.rfind("parent-directory-", 0) == 0) return 350;
    if (source.rfind("parent-humanized-", 0) == 0) return 330;
    if (source == "exe-stem" || source == "exe-humanized") return 250;
    return 300;
}

static bool preferSteamCandidateEvidence(
    int candidateTier, double candidateScore, bool candidateExact,
    int currentTier, double currentScore, bool currentExact) {
    if (candidateTier != currentTier) return candidateTier > currentTier;
    if (candidateScore > currentScore + 0.0001) return true;
    return std::abs(candidateScore - currentScore) < 0.0001 && candidateExact && !currentExact;
}

static bool disallowedRelatedSteamContent(const std::string& name, const std::string& type) {
    if (type != "dlc") return true;
    return containsSteamRelatedContentMarker(name);
}

struct BaseNameConsensusAssessment {
    bool safe = false;
    std::string reason;
    std::vector<int> leftOrdinals;
    std::vector<int> rightOrdinals;
    std::vector<std::string> unapprovedTokens;
};

static int romanOrdinal(const std::string& token) {
    if (token.empty() || token.size() > 6) return 0;
    std::map<char, int> values = {
        {'i', 1}, {'v', 5}, {'x', 10}, {'l', 50}, {'c', 100}, {'d', 500}, {'m', 1000}
    };
    int total = 0;
    int previous = 0;
    for (auto iterator = token.rbegin(); iterator != token.rend(); ++iterator) {
        const auto value = values.find(*iterator);
        if (value == values.end()) return 0;
        if (value->second < previous) total -= value->second;
        else { total += value->second; previous = value->second; }
    }
    if (total <= 0 || total > 50) return 0;
    static const std::array<const char*, 14> canonical = {
        "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x",
        "xi", "xii", "xiii", "xiv"
    };
    if (total > static_cast<int>(std::size(canonical))) return 0;
    std::string canonicalRoman;
    int remaining = total;
    const std::array<std::pair<int, const char*>, 13> symbols = {{
        {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"},
        {0, ""}, {0, ""}, {0, ""}, {0, ""}, {0, ""}, {0, ""}, {0, ""}, {0, ""}
    }};
    for (const auto& [number, symbol] : symbols) {
        while (number > 0 && remaining >= number) {
            canonicalRoman += symbol;
            remaining -= number;
        }
    }
    return remaining == 0 && canonicalRoman == token ? total : 0;
}

static int titleOrdinal(const std::string& token) {
    if (!token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    })) {
        try {
            const auto value = std::stoi(token);
            return value > 0 && value <= 50 ? value : 0;
        } catch (...) { return 0; }
    }
    return romanOrdinal(token);
}

static std::string canonicalConsensusToken(const std::string& token, int& ordinal) {
    ordinal = titleOrdinal(token);
    return ordinal > 0 ? "#ordinal-" + std::to_string(ordinal) : token;
}

static bool allowedEditionAliasToken(const std::string& token) {
    static const std::unordered_set<std::string> aliases = {
        "emperor", "definitive", "complete", "ultimate", "deluxe", "enhanced",
        "remastered", "anniversary", "gold", "premium", "goty",
        "directors", "director", "cut", "redux", "hd", "edition"
    };
    return aliases.count(token) != 0;
}

static BaseNameConsensusAssessment assessBaseNameConsensus(
    const std::string& steamBaseName, const std::string& igdbBaseName) {
    BaseNameConsensusAssessment assessment;
    const auto leftTokens = titleTokens(steamBaseName);
    const auto rightTokens = titleTokens(igdbBaseName);
    std::map<std::string, int> left;
    std::map<std::string, int> right;
    for (const auto& token : leftTokens) {
        int ordinal = 0;
        left[canonicalConsensusToken(token, ordinal)]++;
        if (ordinal > 0) assessment.leftOrdinals.push_back(ordinal);
    }
    for (const auto& token : rightTokens) {
        int ordinal = 0;
        right[canonicalConsensusToken(token, ordinal)]++;
        if (ordinal > 0) assessment.rightOrdinals.push_back(ordinal);
    }
    std::sort(assessment.leftOrdinals.begin(), assessment.leftOrdinals.end());
    std::sort(assessment.rightOrdinals.begin(), assessment.rightOrdinals.end());
    if (assessment.leftOrdinals != assessment.rightOrdinals) {
        assessment.reason = "sequel-ordinal-mismatch";
        return assessment;
    }
    std::set<std::string> keys;
    for (const auto& [token, _] : left) keys.insert(token);
    for (const auto& [token, _] : right) keys.insert(token);
    for (const auto& token : keys) {
        const int difference = std::abs(left[token] - right[token]);
        if (difference == 0 || token.rfind("#ordinal-", 0) == 0) continue;
        if (!allowedEditionAliasToken(token)) {
            assessment.unapprovedTokens.push_back(token);
        }
    }
    if (!assessment.unapprovedTokens.empty()) {
        assessment.reason = "non-edition-title-token-mismatch";
        return assessment;
    }
    assessment.safe = true;
    assessment.reason = "base-title-equivalent-with-edition-aliases-only";
    return assessment;
}

static json runSteamBaseConsensusSelfTest() {
    struct Case { const char* steam; const char* igdb; bool expected; };
    const std::array<Case, 5> cases = {{
        {"WARNO", "Warno", true},
        {"Total War: ROME II - Emperor Edition", "Total War: Rome II", true},
        {"WARNO", "WARNO 2", false},
        {"War Thunder", "War Thunder 2", false},
        {"WARNO", "WARNO NORTHAG", false}
    }};
    json results = json::array();
    bool allPassed = true;
    for (const auto& item : cases) {
        const auto assessment = assessBaseNameConsensus(item.steam, item.igdb);
        const bool passed = assessment.safe == item.expected;
        allPassed = allPassed && passed;
        results.push_back({
            {"steamBaseName", item.steam}, {"igdbBaseName", item.igdb},
            {"expectedSafe", item.expected}, {"safe", assessment.safe},
            {"reason", assessment.reason}, {"leftOrdinals", assessment.leftOrdinals},
            {"rightOrdinals", assessment.rightOrdinals},
            {"unapprovedTokens", assessment.unapprovedTokens}, {"passed", passed}
        });
    }
    return {
        {"resolverVersion", STEAM_RESOLVER_VERSION},
        {"allPassed", allPassed},
        {"cases", results}
    };
}

static json runReleaseYearSelfTest() {
    struct Case { const char* label; json value; int expected; };
    const std::array<Case, 7> cases = {{
        {"steam-release-date", json{{"date", "19 Sep, 2017"}, {"coming_soon", false}}, 2017},
        {"steam-release-date-legacy-key", json{{"release_date", json{{"date", "25 Oct, 2019"}}}}, 2019},
        {"normalized-release-date", json{{"releaseDate", json{{"display", "2021"}}}}, 2021},
        {"explicit-year", json{{"year", 2004}}, 2004},
        {"igdb-unix-release-date", json{{"first_release_date", 1609459200}}, 2021},
        {"missing-year-is-not-invented", json{{"date", "Coming Soon"}}, 0},
        {"unrelated-number-is-not-a-year", json{{"appId", 570}, {"score", 0.99}}, 0}
    }};
    json results = json::array();
    bool allPassed = true;
    for (const auto& item : cases) {
        const int actual = releaseYearFromValue(item.value);
        const bool passed = actual == item.expected;
        allPassed = allPassed && passed;
        results.push_back({
            {"case", item.label}, {"expected", item.expected}, {"actual", actual}, {"passed", passed}
        });
    }
    return {
        {"resolverVersion", STEAM_RESOLVER_VERSION},
        {"allPassed", allPassed},
        {"cases", results}
    };
}

static ResolvedGame verifySteamAppId(
    int appId,
    json& roundLog,
    const std::string& mode = "manual-direct-id",
    const std::string& source = "manual-steam-id") {
    json record = {
        {"provider", "steam"},
        {"mode", mode},
        {"appId", appId},
        {"networkAttempts", json::array()},
        {"status", "verifying"}
    };
    try {
        const auto details = fetchSteamDetails(appId, record["networkAttempts"]);
        if (details.type != "game") {
            // A manually entered ID is an identity assertion, not a fuzzy title
            // query.  Keep it strict: related content is only promoted when the
            // automatic resolver can prove an exact path/title/fullgame chain.
            throw std::runtime_error("Steam AppID is not a base game (type=" + details.type + ")");
        }
        record["status"] = "verified";
        record["requestedAppId"] = appId;
        record["canonicalAppId"] = details.canonicalAppId;
        record["formalName"] = details.name;
        record["contentType"] = details.type;
        const int releaseYear = releaseYearFromValue(details.data.value("release_date", json(nullptr)));
        if (releaseYear > 0) {
            record["year"] = releaseYear;
            record["releaseYear"] = releaseYear;
            record["releaseDate"] = releaseDisplayFromValue(details.data.value("release_date", json(nullptr)));
        }
        record["resolverVersion"] = STEAM_RESOLVER_VERSION;
        roundLog.push_back(record);

        ResolvedGame resolved;
        resolved.appId = details.canonicalAppId;
        resolved.name = details.name;
        resolved.displayName = details.name;
        resolved.query = std::to_string(appId);
        resolved.source = source;
        resolved.score = 1.0;
        resolved.details = details.data;
        resolved.baseGameDetails = details.data;
        resolved.confident = true;
        resolved.primaryProvider = "steam";
        resolved.identityStatus = "verified";
        resolved.steamVerificationStatus = "steam-verified";
        resolved.metadataVerificationStatus = "not-available";
        resolved.identityKind = "steam-base-game";
        resolved.contentType = details.type.empty() ? "game" : details.type;
        resolved.storefrontAppId = appId;
        resolved.baseGameAppId = details.canonicalAppId;
        resolved.contentRelation = "canonical-base-game";
        return resolved;
    } catch (const std::exception& error) {
        record["status"] = "rejected";
        record["error"] = error.what();
        roundLog.push_back(record);
        throw;
    }
}

static std::optional<ResolvedGame> resolveSteamGame(
    const std::vector<NameRound>& rounds,
    const std::vector<std::string>& pathEvidence,
    json& roundLog) {
    struct SearchCandidate {
        int storefrontAppId = 0;
        int canonicalHintAppId = 0;
        std::string name;
        double score = 0;
        double queryScore = 0;
        double pathScore = 0;
        double evidenceReliability = 0;
        double officialRankScore = 0;
        int officialRank = 0;
        int trustedExactTier = 0;
        int round = 0;
        std::string query;
        std::string source;
        std::string searchEndpoint;
        std::string searchLanguage;
        std::string searchCountry;
        bool exact = false;
        json evidence = json::array();
    };
    struct VerifiedCandidate {
        ResolvedGame game;
        double queryScore = 0;
        double pathScore = 0;
        double evidenceReliability = 0;
        int officialRank = 0;
        int trustedExactTier = 0;
        bool exact = false;
        size_t queryTokenCount = 0;
        json audit;
    };

    std::map<int, SearchCandidate> found;
    std::set<std::string> searchedQueries;
    int consecutiveNetworkFailures = 0;
    bool providerCircuitOpen = false;

    auto mergeCandidate = [&](const NameRound& round, const std::string& query,
                              const SteamOfficialSearchItem& item,
                              const std::string& endpoint, json& itemLog) {
        const double queryScore = scoreTitle(query, item.name);
        const double pathScore = scorePathEvidence(pathEvidence, item.name);
        const double reliability = steamEvidenceReliability(round.source);
        const double rankScore = std::clamp(1.0 - (std::max)(0, item.officialRank - 1) * 0.06, 0.30, 1.0);
        const bool exact = normalizeTitle(query) == normalizeTitle(item.name);
        const int trustedExactTier = steamTrustedExactTier(round.source, exact);
        double score = std::clamp(queryScore * 0.60 + pathScore * 0.25 +
            reliability * 0.10 + rankScore * 0.05, 0.0, 0.995);
        if (exact && round.source.rfind("manual", 0) == 0) score = (std::max)(score, 0.995);
        if (exact && reliability >= 0.95 &&
            (round.source == "pe-product-name" || round.source == "pe-file-description" ||
             round.source.rfind("local-launcher-settings", 0) == 0)) {
            score = (std::max)(score, 0.985);
        }
        if (exact && pathScore >= 0.97 && reliability >= 0.90) score = (std::max)(score, 0.985);
        const json evidence = {
            {"round", round.round}, {"source", round.source}, {"query", query},
            {"endpoint", endpoint}, {"officialRank", item.officialRank},
            {"language", item.language}, {"country", item.country},
            {"canonicalHintAppId", item.canonicalHintAppId > 0 ? json(item.canonicalHintAppId) : json(nullptr)},
            {"queryScore", queryScore}, {"pathScore", pathScore},
            {"evidenceReliability", reliability}, {"score", score}, {"exact", exact}
            , {"trustedExactTier", trustedExactTier}
        };
        itemLog = {
            {"storefrontAppId", item.storefrontAppId}, {"appId", item.storefrontAppId},
            {"canonicalHintAppId", item.canonicalHintAppId > 0 ? json(item.canonicalHintAppId) : json(nullptr)},
            {"name", item.name}, {"score", score}, {"queryScore", queryScore},
            {"pathScore", pathScore}, {"officialRank", item.officialRank}, {"exact", exact}
        };
        auto existing = found.find(item.storefrontAppId);
        if (existing == found.end()) {
            SearchCandidate candidate;
            candidate.storefrontAppId = item.storefrontAppId;
            candidate.canonicalHintAppId = item.canonicalHintAppId;
            candidate.name = item.name;
            candidate.score = score;
            candidate.queryScore = queryScore;
            candidate.pathScore = pathScore;
            candidate.evidenceReliability = reliability;
            candidate.officialRankScore = rankScore;
            candidate.officialRank = item.officialRank;
            candidate.trustedExactTier = trustedExactTier;
            candidate.round = round.round;
            candidate.query = query;
            candidate.source = round.source;
            candidate.searchEndpoint = endpoint;
            candidate.searchLanguage = item.language;
            candidate.searchCountry = item.country;
            candidate.exact = exact;
            candidate.evidence.push_back(evidence);
            found.emplace(item.storefrontAppId, std::move(candidate));
        } else {
            existing->second.evidence.push_back(evidence);
            if (preferSteamCandidateEvidence(
                    trustedExactTier, score, exact,
                    existing->second.trustedExactTier, existing->second.score, existing->second.exact)) {
                auto evidenceList = std::move(existing->second.evidence);
                existing->second.name = item.name;
                existing->second.canonicalHintAppId = item.canonicalHintAppId;
                existing->second.score = score;
                existing->second.queryScore = queryScore;
                existing->second.pathScore = pathScore;
                existing->second.evidenceReliability = reliability;
                existing->second.officialRankScore = rankScore;
                existing->second.officialRank = item.officialRank;
                existing->second.trustedExactTier = trustedExactTier;
                existing->second.round = round.round;
                existing->second.query = query;
                existing->second.source = round.source;
                existing->second.searchEndpoint = endpoint;
                existing->second.searchLanguage = item.language;
                existing->second.searchCountry = item.country;
                existing->second.exact = exact;
                existing->second.evidence = std::move(evidenceList);
            }
        }
    };

    for (const auto& round : rounds) {
        json record = {
            {"provider", "steam"},
            {"round", round.round},
            {"source", round.source},
            {"input", round.value},
            {"queries", json::array()},
            {"status", "searching"}
        };
        if (!round.skipReason.empty()) {
            record["status"] = "skipped";
            record["reason"] = round.skipReason;
            roundLog.push_back(std::move(record));
            std::cout << "[ROUND " << round.round << "] " << round.source << ": SKIP " << round.skipReason << "\n";
            continue;
        }
        std::cout << "[ROUND " << round.round << "] " << round.source << ": " << round.value << "\n";

        std::vector<std::string> queries;
        std::set<std::string> queryKeys;
        auto addQuery = [&](const std::string& query) {
            const auto value = collapseSpaces(query);
            const auto key = searchQueryKey(value);
            if (!value.empty() && queryKeys.insert(key).second) queries.push_back(value);
        };
        addQuery(round.value);
        addQuery(humanizeTitle(round.value));
        addQuery(steamSearchFriendlyTitle(round.value));
        addQuery(stripAddonSuffix(round.value));
        addQuery(stripSearchPackagingSuffix(round.value));
        std::set<int> roundCandidates;
        for (const auto& query : queries) {
            json queryRecord = {{"query", query}, {"endpoints", json::array()}, {"items", json::array()}};
            const auto queryKey = searchQueryKey(query);
            if (!searchedQueries.insert(queryKey).second) {
                queryRecord["status"] = "duplicate-global-query";
                record["queries"].push_back(std::move(queryRecord));
                continue;
            }
            std::vector<SteamOfficialSearchItem> queryItems;
            auto runResultsSearch = [&](bool gameCategory, const SteamSearchLocale& locale) {
                json endpointRecord = {
                    {"endpoint", "official-search-results-json"},
                    {"category1", gameCategory ? json(998) : json(nullptr)},
                    {"language", locale.language}, {"country", locale.country},
                    {"networkAttempts", json::array()}
                };
                try {
                    auto url = "https://store.steampowered.com/search/results/?query=&start=0&count=10"
                        "&dynamic_data=&sort_by=_ASC&term=" + percentEncode(query) +
                        "&infinite=1&json=1&cc=" + percentEncode(locale.country) +
                        "&l=" + percentEncode(locale.language);
                    if (gameCategory) url += "&category1=998";
                    const auto response = getJson(url, "steam", &endpointRecord["networkAttempts"]);
                    const auto parsed = parseSteamResultsHtml(response, locale.language, locale.country);
                    endpointRecord["responseSuccess"] = response.value("success", 0);
                    endpointRecord["totalCount"] = response.value("total_count", 0);
                    if (response.contains("results_html") && response["results_html"].is_string()) {
                        const auto html = response["results_html"].get<std::string>();
                        endpointRecord["resultsHtmlBytes"] = html.size();
                        endpointRecord["resultsHtmlPrefix"] = html.substr(0, (std::min<size_t>)(html.size(), 512));
                    }
                    endpointRecord["transportOk"] = true;
                    endpointRecord["ok"] = !parsed.empty();
                    endpointRecord["resultStatus"] = parsed.empty() ? "no-parsed-official-candidate" : "parsed";
                    endpointRecord["parsedCount"] = parsed.size();
                    queryItems.insert(queryItems.end(), parsed.begin(), parsed.end());
                    consecutiveNetworkFailures = 0;
                } catch (const std::exception& error) {
                    endpointRecord["ok"] = false;
                    endpointRecord["error"] = error.what();
                    if (++consecutiveNetworkFailures >= 2) {
                        endpointRecord["circuitOpened"] = true;
                        providerCircuitOpen = true;
                    }
                }
                queryRecord["endpoints"].push_back(std::move(endpointRecord));
            };

            const auto locales = steamSearchLocales(query);
            for (const auto& locale : locales) {
                runResultsSearch(true, locale);
                const bool exactOfficialCandidate = std::any_of(queryItems.begin(), queryItems.end(), [&](const auto& item) {
                    return scoreTitle(query, item.name) >= 0.96;
                });
                if (exactOfficialCandidate || providerCircuitOpen) break;
            }
            const auto hasStrongOfficialCandidate = [&]() {
                for (const auto& item : queryItems) {
                    if (scoreTitle(query, item.name) >= 0.86) return true;
                }
                return false;
            };
            if (!hasStrongOfficialCandidate() && !providerCircuitOpen) {
                for (const auto& locale : locales) {
                    runResultsSearch(false, locale);
                    const bool exactOfficialCandidate = std::any_of(queryItems.begin(), queryItems.end(), [&](const auto& item) {
                        return scoreTitle(query, item.name) >= 0.96;
                    });
                    if (exactOfficialCandidate || providerCircuitOpen) break;
                }
            }
            if (queryItems.empty() && !providerCircuitOpen) {
                json endpointRecord = {
                    {"endpoint", "api-storesearch-fallback"}, {"networkAttempts", json::array()}
                };
                try {
                    const auto response = getJson(
                        "https://store.steampowered.com/api/storesearch/?term=" + percentEncode(query) + "&l=english&cc=us",
                        "steam", &endpointRecord["networkAttempts"]);
                    if (response.contains("items") && response["items"].is_array()) {
                        int rank = 0;
                        for (const auto& item : response["items"]) {
                            if (!item.is_object()) continue;
                            if (item.contains("type") && item["type"].is_string() &&
                                item["type"].get<std::string>() != "app") continue;
                            const int resultAppId = item.value("id", 0);
                            const auto resultName = trim(item.value("name", std::string{}));
                            if (resultAppId > 0 && !resultName.empty()) {
                                queryItems.push_back({resultAppId, resultName, ++rank,
                                    "api-storesearch-fallback", 0, "english", "US"});
                            }
                        }
                    }
                    endpointRecord["ok"] = true;
                    endpointRecord["parsedCount"] = queryItems.size();
                    consecutiveNetworkFailures = 0;
                } catch (const std::exception& error) {
                    endpointRecord["ok"] = false;
                    endpointRecord["error"] = error.what();
                    if (++consecutiveNetworkFailures >= 2) {
                        endpointRecord["circuitOpened"] = true;
                        providerCircuitOpen = true;
                    }
                }
                queryRecord["endpoints"].push_back(std::move(endpointRecord));
            }

            std::set<int> querySeen;
            std::stable_sort(queryItems.begin(), queryItems.end(), [&](const auto& left, const auto& right) {
                const double leftScore = scoreTitle(query, left.name);
                const double rightScore = scoreTitle(query, right.name);
                if (std::abs(leftScore - rightScore) > 0.0001) return leftScore > rightScore;
                return left.officialRank < right.officialRank;
            });
            size_t retainedForQuery = 0;
            for (const auto& item : queryItems) {
                if (item.storefrontAppId <= 0 || !querySeen.insert(item.storefrontAppId).second) continue;
                if (retainedForQuery++ >= 5) break;
                json itemLog;
                mergeCandidate(round, query, item,
                    item.searchEndpoint.empty() ? "unknown" : item.searchEndpoint, itemLog);
                queryRecord["items"].push_back(itemLog);
                roundCandidates.insert(item.storefrontAppId);
            }
            queryRecord["status"] = queryItems.empty() ? "no-candidate" : "completed";
            record["queries"].push_back(std::move(queryRecord));
            if (providerCircuitOpen) break;
        }

        std::vector<SearchCandidate> roundRanked;
        for (const int appId : roundCandidates) {
            const auto candidate = found.find(appId);
            if (candidate != found.end() && candidate->second.score >= 0.45) roundRanked.push_back(candidate->second);
        }
        std::sort(roundRanked.begin(), roundRanked.end(), [](const auto& left, const auto& right) {
            return left.score > right.score;
        });
        if (roundRanked.size() > 10) roundRanked.resize(10);
        record["ranked"] = json::array();
        for (const auto& candidate : roundRanked) {
            record["ranked"].push_back({
                {"appId", candidate.storefrontAppId}, {"storefrontAppId", candidate.storefrontAppId},
                {"canonicalHintAppId", candidate.canonicalHintAppId > 0 ? json(candidate.canonicalHintAppId) : json(nullptr)},
                {"name", candidate.name}, {"score", candidate.score}, {"queryScore", candidate.queryScore},
                {"pathScore", candidate.pathScore}, {"query", candidate.query}, {"source", candidate.source},
                {"officialRank", candidate.officialRank}, {"exact", candidate.exact},
                {"trustedExactTier", candidate.trustedExactTier},
                {"language", candidate.searchLanguage}, {"country", candidate.searchCountry}
            });
        }
        record["status"] = providerCircuitOpen ? "provider-circuit-open" :
            (roundRanked.empty() ? "no-candidate" : "search-complete-global-decision-pending");
        roundLog.push_back(std::move(record));
        if (providerCircuitOpen) break;
    }

    std::vector<SearchCandidate> preliminary;
    for (const auto& [_, candidate] : found) if (candidate.score >= 0.45) preliminary.push_back(candidate);
    std::sort(preliminary.begin(), preliminary.end(), [](const auto& left, const auto& right) {
        if (left.trustedExactTier != right.trustedExactTier) return left.trustedExactTier > right.trustedExactTier;
        if (std::abs(left.score - right.score) > 0.0001) return left.score > right.score;
        if (left.exact != right.exact) return left.exact;
        return titleTokens(left.query).size() > titleTokens(right.query).size();
    });
    if (!preliminary.empty()) {
        const double verificationFloor = (std::max)(0.55, preliminary.front().score - 0.30);
        preliminary.erase(std::remove_if(preliminary.begin(), preliminary.end(), [&](const auto& candidate) {
            return candidate.score < verificationFloor;
        }), preliminary.end());
    }
    if (preliminary.size() > 3) preliminary.resize(3);

    json globalRecord = {
        {"provider", "steam"}, {"mode", "global-official-fuzzy-arbitration"},
        {"resolverVersion", STEAM_RESOLVER_VERSION}, {"status", "verifying"},
        {"ranked", json::array()}, {"rejectedContent", json::array()}
    };
    std::vector<VerifiedCandidate> verified;
    std::set<int> attemptedCandidates;
    double highestUnverifiedScore = 0.0;
    json unverifiedCandidates = json::array();
    for (const auto& candidate : preliminary) {
        attemptedCandidates.insert(candidate.storefrontAppId);
        json candidateAudit = {
            {"appId", candidate.storefrontAppId}, {"storefrontAppId", candidate.storefrontAppId},
            {"canonicalHintAppId", candidate.canonicalHintAppId > 0 ? json(candidate.canonicalHintAppId) : json(nullptr)},
            {"storeSearchName", candidate.name}, {"query", candidate.query}, {"source", candidate.source},
            {"preliminaryScore", candidate.score}, {"officialRank", candidate.officialRank},
            {"trustedExactTier", candidate.trustedExactTier},
            {"searchLanguage", candidate.searchLanguage}, {"searchCountry", candidate.searchCountry},
            {"evidence", candidate.evidence}, {"networkAttempts", json::array()}
        };
        try {
            const auto content = fetchSteamDetails(candidate.storefrontAppId, candidateAudit["networkAttempts"]);
            consecutiveNetworkFailures = 0;
            const double formalQueryScore = scoreTitle(candidate.query, content.name);
            const double formalPathScore = scorePathEvidence(pathEvidence, content.name);
            // A localized official search result and the English appdetails
            // name are two names for the same verified AppID.  Preserve the
            // exact localized title/PE agreement instead of discarding it when
            // appdetails canonicalizes the display title to English.
            const double queryScore = (std::max)(formalQueryScore, candidate.queryScore);
            const double pathScore = (std::max)(formalPathScore, candidate.pathScore);
            const bool formalExact = normalizeTitle(candidate.query) == normalizeTitle(content.name);
            const bool exact = formalExact || candidate.exact;
            double verifiedScore = std::clamp(queryScore * 0.60 + pathScore * 0.25 +
                candidate.evidenceReliability * 0.10 + candidate.officialRankScore * 0.05, 0.0, 0.995);
            if (exact && candidate.source.rfind("manual", 0) == 0) verifiedScore = (std::max)(verifiedScore, 0.995);
            if (candidate.exact && candidate.evidenceReliability >= 0.95 &&
                (candidate.source == "pe-product-name" || candidate.source == "pe-file-description" ||
                 candidate.source.rfind("local-launcher-settings", 0) == 0)) {
                verifiedScore = (std::max)(verifiedScore, 0.985);
            }
            if (exact && pathScore >= 0.97 && candidate.evidenceReliability >= 0.90) {
                verifiedScore = (std::max)(verifiedScore, 0.985);
            }

            ResolvedGame resolved;
            resolved.query = candidate.query;
            resolved.source = candidate.source;
            resolved.score = verifiedScore;
            resolved.primaryProvider = "steam";
            resolved.storefrontAppId = candidate.storefrontAppId;
            resolved.steamIdCandidate = candidate.storefrontAppId;
            resolved.contentType = content.type.empty() ? "game" : content.type;
            resolved.identityStatus = "candidate";
            resolved.steamVerificationStatus = "candidate";
            resolved.resolverVersion = STEAM_RESOLVER_VERSION;

            if (content.type == "game") {
                resolved.appId = content.canonicalAppId;
                resolved.baseGameAppId = content.canonicalAppId;
                resolved.name = content.name;
                resolved.displayName = content.name;
                resolved.details = content.data;
                resolved.baseGameDetails = content.data;
                resolved.identityKind = "steam-base-game";
                resolved.contentRelation = candidate.storefrontAppId == content.canonicalAppId
                    ? "canonical-base-game" : "storefront-alias-to-canonical-base-game";
            } else if (content.type == "dlc") {
                const int fullgameAppId = content.data.contains("fullgame") && content.data["fullgame"].is_object()
                    ? positiveJsonInt(content.data["fullgame"], "appid") : 0;
                const bool strongContentMatch = !disallowedRelatedSteamContent(content.name, content.type) &&
                    fullgameAppId > 0 && (exact || queryScore >= 0.96) && pathScore >= 0.92 &&
                    candidate.evidenceReliability >= 0.90 && candidate.score >= 0.90;
                if (!strongContentMatch) {
                    candidateAudit["status"] = "rejected";
                    candidateAudit["reason"] = "related-content-not-an-explicit-strong-folder-title-match";
                    candidateAudit["formalName"] = content.name;
                    candidateAudit["contentType"] = content.type;
                    globalRecord["rejectedContent"].push_back(candidateAudit);
                    continue;
                }
                json baseAttempts = json::array();
                const auto base = fetchSteamDetails(fullgameAppId, baseAttempts);
                candidateAudit["baseNetworkAttempts"] = baseAttempts;
                if (base.type != "game") {
                    candidateAudit["status"] = "rejected";
                    candidateAudit["reason"] = "fullgame-did-not-canonicalize-to-base-game";
                    candidateAudit["baseContentType"] = base.type;
                    globalRecord["rejectedContent"].push_back(candidateAudit);
                    continue;
                }
                double baseExecutableEvidenceScore = 0.0;
                json baseExecutableEvidence = json::array();
                for (const auto& identityRound : rounds) {
                    if (!identityRound.skipReason.empty()) continue;
                    const bool independentExecutableEvidence = identityRound.source == "pe-product-name" ||
                        identityRound.source == "pe-file-description" ||
                        identityRound.source == "exe-stem" || identityRound.source == "exe-humanized";
                    if (!independentExecutableEvidence) continue;
                    const double evidenceScore = scoreTitle(identityRound.value, base.name);
                    baseExecutableEvidenceScore = (std::max)(baseExecutableEvidenceScore, evidenceScore);
                    baseExecutableEvidence.push_back({
                        {"source", identityRound.source}, {"value", identityRound.value}, {"score", evidenceScore}
                    });
                }
                candidateAudit["baseExecutableEvidence"] = baseExecutableEvidence;
                candidateAudit["baseExecutableEvidenceScore"] = baseExecutableEvidenceScore;
                if (baseExecutableEvidenceScore < 0.80) {
                    candidateAudit["status"] = "rejected";
                    candidateAudit["reason"] = "fullgame-has-no-independent-pe-or-executable-name-agreement";
                    globalRecord["rejectedContent"].push_back(candidateAudit);
                    continue;
                }
                resolved.appId = base.canonicalAppId;
                resolved.baseGameAppId = base.canonicalAppId;
                resolved.name = content.name;
                resolved.displayName = content.name;
                resolved.details = content.data;
                resolved.baseGameDetails = base.data;
                resolved.identityKind = "steam-base-game-with-explicit-content";
                resolved.matchedContentAppId = content.canonicalAppId;
                resolved.matchedContentName = content.name;
                resolved.matchedContentType = content.type;
                resolved.contentRelation = "explicit-folder-title-match-with-verified-fullgame";
            } else {
                candidateAudit["status"] = "rejected";
                candidateAudit["reason"] = "unsupported-steam-content-type";
                candidateAudit["formalName"] = content.name;
                candidateAudit["contentType"] = content.type;
                globalRecord["rejectedContent"].push_back(candidateAudit);
                continue;
            }

            // Appdetails has already been fetched for verification. Preserve
            // its release date in the ranked candidate/audit records so the
            // editor can show the year without making another network call.
            const json* releaseSource = nullptr;
            if (resolved.details.is_object() && resolved.details.contains("release_date")) {
                releaseSource = &resolved.details["release_date"];
            }
            if ((!releaseSource || releaseYearFromValue(*releaseSource) <= 0) &&
                resolved.baseGameDetails.is_object() && resolved.baseGameDetails.contains("release_date")) {
                releaseSource = &resolved.baseGameDetails["release_date"];
            }
            if (releaseSource) {
                const int releaseYear = releaseYearFromValue(*releaseSource);
                if (releaseYear > 0) {
                    candidateAudit["year"] = releaseYear;
                    candidateAudit["releaseYear"] = releaseYear;
                    candidateAudit["releaseDate"] = releaseDisplayFromValue(*releaseSource);
                }
            }

            candidateAudit["status"] = "verified-candidate";
            candidateAudit["canonicalAppId"] = resolved.appId;
            candidateAudit["formalName"] = resolved.name;
            const auto artworkPreview = resolved.details.is_object()
                ? resolved.details.value("header_image", std::string{})
                : std::string{};
            if (!artworkPreview.empty()) candidateAudit["artworkPreview"] = artworkPreview;
            candidateAudit["displayName"] = resolved.displayName;
            candidateAudit["contentType"] = resolved.contentType;
            candidateAudit["matchedContentAppId"] = resolved.matchedContentAppId > 0
                ? json(resolved.matchedContentAppId) : json(nullptr);
            candidateAudit["baseGameAppId"] = resolved.baseGameAppId;
            candidateAudit["score"] = verifiedScore;
            candidateAudit["queryScore"] = queryScore;
            candidateAudit["pathScore"] = pathScore;
            candidateAudit["exact"] = exact;
            candidateAudit["localizedSearchExact"] = candidate.exact;
            candidateAudit["formalNameExact"] = formalExact;
            verified.push_back({resolved, queryScore, pathScore, candidate.evidenceReliability,
                candidate.officialRank, candidate.trustedExactTier, exact,
                titleTokens(candidate.query).size(), candidateAudit});
        } catch (const std::exception& error) {
            candidateAudit["status"] = "verification-error";
            candidateAudit["error"] = error.what();
            globalRecord["rejectedContent"].push_back(candidateAudit);
            highestUnverifiedScore = (std::max)(highestUnverifiedScore, candidate.score);
            unverifiedCandidates.push_back({
                {"storefrontAppId", candidate.storefrontAppId},
                {"canonicalHintAppId", candidate.canonicalHintAppId > 0
                    ? json(candidate.canonicalHintAppId) : json(nullptr)},
                {"name", candidate.name},
                {"preliminaryScore", candidate.score},
                {"reason", "appdetails-verification-failed"},
                {"error", error.what()}
            });
            if (++consecutiveNetworkFailures >= 2) {
                globalRecord["verificationCircuitOpen"] = true;
                globalRecord["verificationCircuitReason"] = "two-consecutive-appdetails-failures";
                break;
            }
        }
    }
    // If the verification circuit opened, candidates after the break were not
    // disproven; retain their preliminary scores as conservative runner-up
    // evidence rather than silently accepting the only verified candidate.
    for (const auto& candidate : preliminary) {
        if (attemptedCandidates.count(candidate.storefrontAppId)) continue;
        highestUnverifiedScore = (std::max)(highestUnverifiedScore, candidate.score);
        unverifiedCandidates.push_back({
            {"storefrontAppId", candidate.storefrontAppId},
            {"canonicalHintAppId", candidate.canonicalHintAppId > 0
                ? json(candidate.canonicalHintAppId) : json(nullptr)},
            {"name", candidate.name},
            {"preliminaryScore", candidate.score},
            {"reason", "verification-circuit-open-before-candidate"}
        });
    }

    std::sort(verified.begin(), verified.end(), [](const auto& left, const auto& right) {
        if (left.trustedExactTier != right.trustedExactTier) {
            return left.trustedExactTier > right.trustedExactTier;
        }
        if (std::abs(left.game.score - right.game.score) > 0.0001) return left.game.score > right.game.score;
        if (left.exact != right.exact) return left.exact;
        if (left.queryTokenCount != right.queryTokenCount) return left.queryTokenCount > right.queryTokenCount;
        return left.officialRank < right.officialRank;
    });
    for (const auto& candidate : verified) globalRecord["ranked"].push_back(candidate.audit);
    if (verified.empty()) {
        globalRecord["status"] = providerCircuitOpen ? "provider-circuit-open" : "no-verified-candidate";
        roundLog.push_back(std::move(globalRecord));
        return std::nullopt;
    }

    const double verifiedRunnerUpScore = verified.size() > 1 ? verified[1].game.score : 0.0;
    const double runnerUpScore = (std::max)(verifiedRunnerUpScore, highestUnverifiedScore);
    const double margin = verified.front().game.score - runnerUpScore;
    auto selected = verified.front();
    const bool manualExact = selected.exact && selected.game.source.rfind("manual", 0) == 0;
    const bool peExact = selected.exact && selected.evidenceReliability >= 0.95 &&
        normalizeTitle(selected.game.query).size() >= 5 && selected.officialRank <= 3 &&
        (selected.game.source == "pe-product-name" || selected.game.source == "pe-file-description");
    const bool trustedLocalExact = selected.exact && selected.evidenceReliability >= 0.95 &&
        normalizeTitle(selected.game.query).size() >= 5 && selected.officialRank <= 3 &&
        selected.game.source.rfind("local-launcher-settings", 0) == 0;
    const bool strongExact = selected.exact && selected.pathScore >= 0.93 &&
        selected.evidenceReliability >= 0.90;
    const bool accepted = manualExact || peExact || trustedLocalExact ||
        (selected.game.score >= 0.94 && strongExact && margin >= 0.025) ||
        (selected.game.score >= 0.88 && margin >= 0.06);
    globalRecord["topScore"] = selected.game.score;
    globalRecord["runnerUpScore"] = runnerUpScore;
    globalRecord["verifiedRunnerUpScore"] = verifiedRunnerUpScore;
    globalRecord["unverifiedRunnerUpScore"] = highestUnverifiedScore;
    globalRecord["unverifiedCandidates"] = unverifiedCandidates;
    globalRecord["verificationIncomplete"] = !unverifiedCandidates.empty();
    globalRecord["margin"] = margin;
    globalRecord["selectedStorefrontAppId"] = selected.game.storefrontAppId;
    globalRecord["selectedCanonicalAppId"] = selected.game.appId;
    globalRecord["selectedDisplayName"] = selected.game.displayName;
    globalRecord["status"] = accepted ? "accepted" : "needs-manual-review";
    if (accepted) {
        selected.game.confident = true;
        selected.game.identityStatus = "verified";
        selected.game.steamVerificationStatus = "steam-verified";
        roundLog.push_back(std::move(globalRecord));
        std::cout << "  ACCEPT Steam canonical AppID " << selected.game.appId << ": "
                  << selected.game.displayName << " (storefront=" << selected.game.storefrontAppId
                  << ", score=" << std::fixed << std::setprecision(3) << selected.game.score
                  << ", margin=" << margin << ")\n";
        return selected.game;
    }
    selected.game.identityStatus = "needs-confirmation";
    roundLog.push_back(std::move(globalRecord));
    return selected.game;
}

static const json* responseData(const json& response) {
    if (response.contains("Data")) return &response["Data"];
    if (response.contains("data")) return &response["data"];
    return nullptr;
}

static std::string responseError(const json& response) {
    for (const auto& key : {"Error", "error"}) {
        if (response.contains(key) && response[key].is_string()) return response[key].get<std::string>();
    }
    return {};
}

static bool rejectedIgdbGameType(int type) {
    // IGDB: 1 DLC/add-on, 2 expansion, 3 bundle, 5 mod, 7 season,
    // 13 pack and 14 update are not a base game match.
    return type == 1 || type == 2 || type == 3 || type == 5 ||
           type == 7 || type == 13 || type == 14;
}

static std::vector<std::string> igdbPlatforms(const json& game) {
    if (!game.is_object() || !game.contains("platforms_expanded")) return {};
    return jsonStringArray(game["platforms_expanded"], "name");
}

static ResolvedGame verifyIgdbId(uint64_t igdbId, const std::string& platformHint, json& roundLog) {
    json record = {
        {"provider", "playnite-igdb"},
        {"mode", "manual-direct-id"},
        {"igdbId", igdbId},
        {"networkAttempts", json::array()},
        {"status", "verifying"}
    };
    try {
        const auto response = getJson(
            "https://api2.playnite.link/api/igdb/game/" + std::to_string(igdbId),
            "playnite-igdb",
            &record["networkAttempts"]);
        const auto apiError = responseError(response);
        if (!apiError.empty()) throw std::runtime_error("Playnite IGDB: " + apiError);
        const auto data = responseData(response);
        if (!data || !data->is_object()) throw std::runtime_error("IGDB ID did not return a valid game record");
        const auto name = trim(data->value("name", std::string{}));
        if (name.empty()) throw std::runtime_error("IGDB ID has no formal title");
        int gameType = 0;
        if (data->contains("game_type") && (*data)["game_type"].is_number_integer()) {
            gameType = (*data)["game_type"].get<int>();
        }
        if (rejectedIgdbGameType(gameType)) {
            throw std::runtime_error("IGDB ID is not a base game");
        }
        const auto platforms = igdbPlatforms(*data);
        if (!platforms.empty() && !platformMatches(platforms, platformHint)) {
            throw std::runtime_error("IGDB ID does not match the selected platform");
        }
        record["status"] = "verified";
        record["formalName"] = name;
        record["platforms"] = platforms;
        record["gameType"] = gameType;
        roundLog.push_back(record);

        ResolvedGame resolved;
        resolved.name = name;
        resolved.displayName = name;
        resolved.query = std::to_string(igdbId);
        resolved.source = "manual-igdb-id";
        resolved.score = 1.0;
        resolved.confident = true;
        resolved.igdbId = igdbId;
        resolved.primaryProvider = "playnite-igdb";
        resolved.igdbDetails = *data;
        resolved.identityStatus = "metadata-verified";
        resolved.metadataVerificationStatus = "metadata-verified";
        resolved.identityKind = "igdb-base-game";
        resolved.contentType = "game";
        return resolved;
    } catch (const std::exception& error) {
        record["status"] = "rejected";
        record["error"] = error.what();
        roundLog.push_back(record);
        throw;
    }
}

static std::optional<ResolvedGame> resolveIgdbGame(
    const std::vector<NameRound>& rounds,
    const std::vector<std::string>& pathEvidence,
    const std::string& platformHint,
    json& roundLog) {
    std::optional<ResolvedGame> bestReview;
    int consecutiveNetworkFailures = 0;
    for (const auto& round : rounds) {
        json record = {
            {"round", round.round},
            {"source", round.source},
            {"input", round.value},
            {"provider", "playnite-igdb"},
            {"queries", json::array()},
            {"status", "searching"}
        };
        if (!round.skipReason.empty()) {
            record["status"] = "skipped";
            record["reason"] = round.skipReason;
            roundLog.push_back(std::move(record));
            continue;
        }

        std::vector<std::string> queries;
        std::set<std::string> queryKeys;
        auto addQuery = [&](const std::string& raw) {
            const auto value = collapseSpaces(raw);
            const auto key = searchQueryKey(value);
            if (!value.empty() && queryKeys.insert(key).second) queries.push_back(value);
        };
        addQuery(round.value);
        addQuery(humanizeTitle(round.value));
        addQuery(steamSearchFriendlyTitle(round.value));
        addQuery(stripAddonSuffix(round.value));
        addQuery(stripSearchPackagingSuffix(round.value));

        struct Candidate {
            uint64_t id = 0;
            std::string name;
            int gameType = 0;
            double score = 0;
            double queryScore = 0;
            double pathScore = 0;
            double platformScore = 0;
            std::string query;
            std::vector<std::string> platforms;
        };
        std::map<uint64_t, Candidate> found;
        bool circuitOpen = false;
        for (const auto& query : queries) {
            json queryRecord = {
                {"query", query},
                {"items", json::array()},
                {"networkAttempts", json::array()}
            };
            try {
                const auto response = postJson(
                    "https://api2.playnite.link/api/igdb/search",
                    json{{"SearchTerm", query}},
                    "playnite-igdb",
                    &queryRecord["networkAttempts"]);
                const auto apiError = responseError(response);
                if (!apiError.empty()) throw std::runtime_error("Playnite IGDB: " + apiError);
                const auto data = responseData(response);
                if (data && data->is_array()) {
                    for (const auto& item : *data) {
                        if (!item.is_object()) continue;
                        const uint64_t id = item.value("id", uint64_t{0});
                        const auto name = item.value("name", std::string{});
                        if (!id || name.empty()) continue;
                        int gameType = 0;
                        if (item.contains("game_type") && item["game_type"].is_number_integer()) {
                            gameType = item["game_type"].get<int>();
                        }
                        const auto platforms = igdbPlatforms(item);
                        const bool platformKnown = !platforms.empty();
                        const bool platformOk = !platformKnown || platformMatches(platforms, platformHint);
                        const double platformScore = platformKnown ? (platformOk ? 1.0 : 0.0) : 0.5;
                        const double queryScore = scoreTitle(query, name);
                        const double pathScore = scorePathEvidence(pathEvidence, name);
                        double score = queryScore >= 0.999 && platformOk
                            ? 1.0
                            : std::clamp(queryScore * 0.76 + pathScore * 0.14 + platformScore * 0.10, 0.0, 0.99);
                        if (rejectedIgdbGameType(gameType)) score = (std::max)(0.0, score - 0.34);
                        queryRecord["items"].push_back({
                            {"igdbId", id}, {"name", name}, {"gameType", gameType},
                            {"score", score}, {"queryScore", queryScore}, {"pathScore", pathScore},
                            {"platformScore", platformScore}, {"platforms", platforms}
                        });
                        const auto existing = found.find(id);
                        if (existing == found.end() || score > existing->second.score) {
                            found[id] = {id, name, gameType, score, queryScore, pathScore,
                                         platformScore, query, platforms};
                        }
                    }
                }
                consecutiveNetworkFailures = 0;
                queryRecord["ok"] = true;
            } catch (const std::exception& error) {
                queryRecord["ok"] = false;
                queryRecord["error"] = error.what();
                if (++consecutiveNetworkFailures >= 2) {
                    queryRecord["circuitOpened"] = true;
                    circuitOpen = true;
                }
            }
            record["queries"].push_back(std::move(queryRecord));
            if (circuitOpen) break;
        }
        if (circuitOpen && found.empty()) {
            record["status"] = "provider-circuit-open";
            roundLog.push_back(std::move(record));
            return bestReview;
        }

        std::vector<Candidate> ranked;
        for (const auto& [_, candidate] : found) {
            if (candidate.score >= 0.50 && !rejectedIgdbGameType(candidate.gameType)) ranked.push_back(candidate);
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
        if (ranked.size() > 10) ranked.resize(10);
        record["ranked"] = json::array();
        for (const auto& candidate : ranked) {
            record["ranked"].push_back({
                {"igdbId", candidate.id}, {"name", candidate.name}, {"gameType", candidate.gameType},
                {"score", candidate.score}, {"queryScore", candidate.queryScore},
                {"pathScore", candidate.pathScore}, {"platformScore", candidate.platformScore},
                {"query", candidate.query}, {"platforms", candidate.platforms}
            });
        }
        if (ranked.empty()) {
            record["status"] = "no-candidate";
            roundLog.push_back(std::move(record));
            continue;
        }

        for (size_t index = 0; index < std::min<size_t>(ranked.size(), 3); ++index) {
            const auto& candidate = ranked[index];
            try {
                json detailAttempts = json::array();
                const auto response = getJson(
                    "https://api2.playnite.link/api/igdb/game/" + std::to_string(candidate.id),
                    "playnite-igdb",
                    &detailAttempts);
                const auto apiError = responseError(response);
                if (!apiError.empty()) throw std::runtime_error("Playnite IGDB: " + apiError);
                const auto data = responseData(response);
                if (!data || !data->is_object()) continue;
                const auto formalName = data->value("name", candidate.name);
                const auto platforms = igdbPlatforms(*data);
                const bool platformKnown = !platforms.empty();
                const bool platformOk = !platformKnown || platformMatches(platforms, platformHint);
                const double verifiedQueryScore = scoreTitle(candidate.query, formalName);
                const double verifiedPathScore = scorePathEvidence(pathEvidence, formalName);
                const double platformScore = platformKnown ? (platformOk ? 1.0 : 0.0) : 0.5;
                const double verifiedScore = verifiedQueryScore >= 0.999 && platformOk
                    ? 1.0
                    : std::clamp(verifiedQueryScore * 0.76 + verifiedPathScore * 0.14 + platformScore * 0.10, 0.0, 0.99);
                double bestOtherScore = 0.0;
                for (size_t other = 0; other < ranked.size(); ++other) {
                    if (other != index) bestOtherScore = (std::max)(bestOtherScore, ranked[other].score);
                }
                const double margin = candidate.score - bestOtherScore;
                record["verified"] = {
                    {"igdbId", candidate.id}, {"formalName", formalName}, {"score", verifiedScore},
                    {"queryScore", verifiedQueryScore}, {"pathScore", verifiedPathScore},
                    {"platformScore", platformScore}, {"platforms", platforms}, {"margin", margin},
                    {"networkAttempts", detailAttempts}
                };

                ResolvedGame resolved;
                resolved.name = formalName;
                resolved.displayName = formalName;
                resolved.query = candidate.query;
                resolved.source = round.source;
                resolved.score = verifiedScore;
                resolved.igdbId = candidate.id;
                resolved.primaryProvider = "playnite-igdb";
                resolved.igdbDetails = *data;
                resolved.identityKind = "igdb-base-game";
                resolved.contentType = "game";
                const bool exact = normalizeTitle(candidate.query) == normalizeTitle(formalName);
                if ((exact && platformOk) ||
                    (verifiedScore >= 0.86 && platformOk && (ranked.size() == 1 || margin >= 0.08))) {
                    resolved.confident = true;
                    resolved.identityStatus = "metadata-verified";
                    resolved.metadataVerificationStatus = "metadata-verified";
                    record["status"] = "accepted";
                    roundLog.push_back(std::move(record));
                    std::cout << "  ACCEPT Playnite IGDB " << candidate.id << ": " << formalName
                              << " (score=" << std::fixed << std::setprecision(3) << verifiedScore << ")\n";
                    return resolved;
                }
                resolved.identityStatus = "needs-confirmation";
                resolved.metadataVerificationStatus = "needs-confirmation";
                if (verifiedScore >= 0.72 && (!bestReview || verifiedScore > bestReview->score)) bestReview = resolved;
            } catch (const std::exception& error) {
                record["verifyError"] = error.what();
            }
        }
        record["status"] = "not-confident";
        roundLog.push_back(std::move(record));
    }
    return bestReview;
}

static std::optional<ResolvedGame> resolveMultiSource(
    const std::vector<NameRound>& rounds,
    const std::vector<std::string>& pathEvidence,
    const std::string& platformHint,
    const std::set<std::string>& faultProviders,
    json& steamRounds,
    json& igdbRounds,
    json& arbitration,
    bool fastSteamAuthority = false) {
    std::optional<ResolvedGame> steam;
    std::optional<ResolvedGame> igdb;
    const auto normalizedPlatform = asciiLower(trim(platformHint));
    const bool steamPlatformEligible = normalizedPlatform.empty() || normalizedPlatform == "any" ||
        normalizedPlatform == "all" || normalizedPlatform == "pc" || normalizedPlatform == "windows" ||
        normalizedPlatform == "pc-windows";
    if (!steamPlatformEligible) {
        steamRounds.push_back({
            {"provider", "steam"}, {"status", "platform-not-supported"},
            {"platformHint", platformHint}, {"reason", "Steam provider is PC-scoped"}
        });
    } else if (faultProviders.count("steam")) {
        steamRounds.push_back({
            {"provider", "steam"}, {"status", "injected-failure"},
            {"reason", "--fault-provider steam"}
        });
    } else {
        steam = resolveSteamGame(rounds, pathEvidence, steamRounds);
    }

    // Keep the comprehensive Steam feature evidence on the critical path,
    // but do not block a verified Steam AppID on secondary IGDB enrichment.
    // IGDB remains available to the editor and the fallback artwork round.
    if (fastSteamAuthority && steam && steam->confident && steam->appId > 0 &&
        steam->steamVerificationStatus == "steam-verified") {
        arbitration = {
            {"resolverVersion", STEAM_RESOLVER_VERSION},
            {"decision", "steam-comprehensive-fast-path"},
            {"steamAuthoritative", true},
            {"igdbSecondary", "deferred-until-editor-or-artwork-fallback"},
            {"steam", {
                {"name", steam->name}, {"appId", steam->appId},
                {"storefrontAppId", steam->storefrontAppId},
                {"baseGameAppId", steam->baseGameAppId},
                {"identityStatus", steam->identityStatus},
                {"steamVerificationStatus", steam->steamVerificationStatus},
                {"score", steam->score}, {"confident", steam->confident}
            }},
            {"playniteIgdb", nullptr}
        };
        return steam;
    }

    if (faultProviders.count("playnite-igdb")) {
        igdbRounds.push_back({
            {"provider", "playnite-igdb"}, {"status", "injected-failure"},
            {"reason", "--fault-provider playnite-igdb"}
        });
    } else {
        if (steam && steam->confident) {
            const auto steamBaseName = trim(steam->baseGameDetails.value("name", std::string{}));
            const bool explicitContentIdentity = steam->identityKind ==
                "steam-base-game-with-explicit-content" && !steamBaseName.empty();
            const std::vector<NameRound> exactRound = {{1,
                explicitContentIdentity ? "steam-base-game-formal-name" : "steam-formal-name",
                explicitContentIdentity ? steamBaseName : steam->name, {}}};
            igdb = resolveIgdbGame(exactRound, pathEvidence, platformHint, igdbRounds);
        } else {
            igdb = resolveIgdbGame(rounds, pathEvidence, platformHint, igdbRounds);
        }
    }

    // If Steam was inconclusive, let a confident IGDB formal title act only
    // as a hint for one more Steam search. An IGDB record alone never becomes
    // a SteamID; only the subsequent official Steam verification can do that.
    if (fastSteamAuthority && igdb && igdb->confident &&
        (!steam || !steam->confident)) {
        const std::string igdbFormalName = trim(igdb->name);
        if (!igdbFormalName.empty() && !faultProviders.count("steam")) {
            const std::vector<NameRound> exactRound = {{1, "igdb-secondary-formal-name", igdbFormalName, {}}};
            auto steamFromIgdb = resolveSteamGame(exactRound, pathEvidence, steamRounds);
            if (steamFromIgdb && steamFromIgdb->confident && steamFromIgdb->appId > 0 &&
                steamFromIgdb->steamVerificationStatus == "steam-verified") {
                auto selected = *steamFromIgdb;
                selected.igdbId = igdb->igdbId;
                selected.igdbDetails = igdb->igdbDetails;
                selected.metadataVerificationStatus = igdb->metadataVerificationStatus;
                selected.primaryProvider = "steam";
                arbitration = {
                    {"resolverVersion", STEAM_RESOLVER_VERSION},
                    {"decision", "steam-comprehensive-igdb-hint"},
                    {"steamAuthoritative", true},
                    {"igdbSecondary", "title-hint-only"},
                    {"steamAppId", selected.appId},
                    {"igdbId", igdb->igdbId},
                    {"nameAgreement", scoreTitle(selected.name, igdb->name)}
                };
                return selected;
            }
        }
    }

    // Steam remains the authority for AppID import.  If its official rounds
    // have only retryable failures but Playnite/IGDB produced a confident
    // metadata result, keep that result usable for the editor while marking
    // the Steam side as pending.  The caller will enqueue a single
    // event-driven retry job; this avoids both false negatives and false
    // import-ready positives during weak network conditions.
    const bool steamVerificationUnavailable =
        (!steam || !steam->confident) && networkVerificationPending(steamRounds);
    if (igdb && igdb->confident && steamVerificationUnavailable) {
        igdb->steamVerificationStatus = "network-pending";
        igdb->identityStatus = "metadata-verified";
    }

    arbitration = {
        {"resolverVersion", STEAM_RESOLVER_VERSION},
        {"steam", steam ? json{{"name", steam->name}, {"displayName", steam->displayName},
                                {"appId", steam->appId}, {"storefrontAppId", steam->storefrontAppId},
                                {"matchedContentAppId", steam->matchedContentAppId > 0 ? json(steam->matchedContentAppId) : json(nullptr)},
                                {"baseGameAppId", steam->baseGameAppId > 0 ? json(steam->baseGameAppId) : json(nullptr)},
                                {"contentType", steam->contentType}, {"identityStatus", steam->identityStatus},
                                {"steamVerificationStatus", steam->steamVerificationStatus},
                                {"metadataVerificationStatus", steam->metadataVerificationStatus},
                                {"contentRelation", steam->contentRelation},
                                {"identityKind", steam->identityKind},
                                {"score", steam->score}, {"confident", steam->confident}} : json(nullptr)},
        {"playniteIgdb", igdb ? json{{"name", igdb->name}, {"igdbId", igdb->igdbId},
                                      {"score", igdb->score}, {"confident", igdb->confident},
                                      {"identityStatus", igdb->identityStatus},
                                      {"steamVerificationStatus", igdb->steamVerificationStatus},
                                      {"metadataVerificationStatus", igdb->metadataVerificationStatus},
                                      {"platforms", igdbPlatforms(igdb->igdbDetails)}} : json(nullptr)}
    };

    if (steam && igdb) {
        const double agreement = scoreTitle(steam->name, igdb->name);
        const auto steamBaseName = trim(steam->baseGameDetails.value("name", std::string{}));
        const bool explicitContentIdentity = steam->identityKind ==
            "steam-base-game-with-explicit-content" && !steamBaseName.empty();
        const double baseNameAgreement = explicitContentIdentity
            ? scoreTitle(steamBaseName, igdb->name) : agreement;
        const auto baseConsensus = explicitContentIdentity
            ? assessBaseNameConsensus(steamBaseName, igdb->name)
            : BaseNameConsensusAssessment{true, "not-required-for-base-game", {}, {}, {}};
        arbitration["nameAgreement"] = agreement;
        arbitration["steamBaseName"] = steamBaseName.empty() ? json(nullptr) : json(steamBaseName);
        arbitration["baseNameAgreement"] = baseNameAgreement;
        arbitration["baseConsensusSafe"] = baseConsensus.safe;
        arbitration["baseConsensusReason"] = baseConsensus.reason;
        arbitration["baseConsensusLeftOrdinals"] = baseConsensus.leftOrdinals;
        arbitration["baseConsensusRightOrdinals"] = baseConsensus.rightOrdinals;
        arbitration["baseConsensusUnapprovedTokens"] = baseConsensus.unapprovedTokens;
        if (explicitContentIdentity && baseNameAgreement >= 0.90 && baseConsensus.safe) {
            // The install/display title may name a specific campaign or DLC,
            // while IGDB intentionally resolves only the base game.  Consensus
            // must therefore compare IGDB with Steam's verified fullgame, then
            // retain every Steam content relationship and use IGDB solely as
            // metadata/artwork enrichment.
            ResolvedGame selected = *steam;
            selected.igdbId = igdb->igdbId;
            selected.igdbDetails = igdb->igdbDetails;
            selected.confident = steam->confident || igdb->confident;
            selected.identityStatus = selected.confident ? "verified" : "needs-confirmation";
            selected.steamVerificationStatus = steam->steamVerificationStatus;
            selected.metadataVerificationStatus = igdb->metadataVerificationStatus;
            selected.primaryProvider = "steam";
            selected.score = (std::max)({selected.score, igdb->score, baseNameAgreement});
            arbitration["agreementTarget"] = "steam-base-game-name";
            arbitration["decision"] = "cross-source-base-game-consensus-with-explicit-content";
            return selected.confident ? std::optional<ResolvedGame>(selected) : std::nullopt;
        }
        if (explicitContentIdentity && steam->confident &&
            (baseNameAgreement < 0.90 || !baseConsensus.safe)) {
            // An IGDB result that looks similar but fails sequel-token or
            // edition-alias checks must never replace Steam's explicit,
            // fullgame-verified content identity.
            arbitration["decision"] = "provider-conflict-steam-explicit-content-wins-without-enrichment";
            arbitration["igdbEnrichmentRejected"] = true;
            return steam;
        }
        if (agreement >= 0.99) {
            // IGDB agreement can confirm that two providers describe the same
            // title, but it cannot promote a non-confident Steam candidate
            // into a Steam AppID.  The old appId>0 check did exactly that:
            // Steam fuzzy search could return a review candidate with an ID,
            // IGDB could agree on the name, and the combined result was then
            // incorrectly marked Steam-verified.  Steam is authoritative only
            // when its own resolver has accepted the candidate.
            const bool steamAuthoritative = steam->confident && steam->appId > 0 &&
                steam->steamVerificationStatus == "steam-verified";
            ResolvedGame selected = steamAuthoritative ? *steam : *igdb;
            selected.igdbId = igdb->igdbId;
            selected.igdbDetails = igdb->igdbDetails;
            selected.confident = steamAuthoritative || igdb->confident;
            selected.identityStatus = steamAuthoritative ? "verified" : "metadata-verified";
            selected.primaryProvider = steamAuthoritative ? "steam" : "playnite-igdb";
            selected.steamVerificationStatus = steamAuthoritative
                ? "steam-verified"
                : (steamVerificationUnavailable ? "network-pending" : "needs-confirmation");
            selected.metadataVerificationStatus = igdb->metadataVerificationStatus;
            selected.score = (std::max)({selected.score, igdb->score, agreement});
            arbitration["decision"] = steamAuthoritative
                ? "cross-source-consensus-steam-authoritative"
                : "cross-source-name-agreement-igdb-secondary-steam-unverified";
            arbitration["steamAuthoritative"] = steamAuthoritative;
            arbitration["steamCandidateAppId"] = steam->appId > 0 ? json(steam->appId) : json(nullptr);
            return selected.confident ? std::optional<ResolvedGame>(selected) : std::nullopt;
        }
        if (steam->confident != igdb->confident) {
            arbitration["decision"] = steam->confident ? "steam-only-confident" : "playnite-igdb-only-confident";
            return steam->confident ? steam : igdb;
        }
        if (steam->confident && igdb->confident) {
            arbitration["decision"] = steam->score >= igdb->score ? "provider-conflict-steam-wins" : "provider-conflict-igdb-wins";
            return steam->score >= igdb->score ? steam : igdb;
        }
        arbitration["decision"] = "provider-conflict-unresolved";
        return std::nullopt;
    }
    if (steam && steam->confident) {
        arbitration["decision"] = "steam-fallback";
        return steam;
    }
    if (igdb && igdb->confident) {
        arbitration["decision"] = steamVerificationUnavailable
            ? "playnite-igdb-fallback-steam-verification-pending-network"
            : "playnite-igdb-fallback";
        return igdb;
    }
    arbitration["decision"] = "no-confident-provider";
    return std::nullopt;
}

static ResolvedGame resolveDirectId(
    const std::optional<int>& steamAppId,
    const std::optional<uint64_t>& igdbId,
    const std::vector<std::string>& pathEvidence,
    const std::string& platformHint,
    const std::set<std::string>& faultProviders,
    json& steamRounds,
    json& igdbRounds,
    json& arbitration,
    const std::string& steamIdMode = "manual") {
    if (steamAppId) {
        if (faultProviders.count("steam")) throw std::runtime_error("Steam provider failure was injected");
        const bool localIdentity = steamIdMode == "local-install-metadata";
        auto selected = verifySteamAppId(
            *steamAppId, steamRounds,
            localIdentity ? "local-install-metadata-appid" : "manual-direct-id",
            localIdentity ? "local-steam-appid" : "manual-steam-id");
        if (localIdentity) selected.identityKind = "steam-base-game-local-appid";
        arbitration = {
            {"decision", localIdentity ? "local-steam-appid-verified" : "manual-steam-id-verified"},
            {localIdentity ? "localAppId" : "manualId", *steamAppId},
            {"canonicalAppId", selected.appId},
            {"identityStatus", selected.identityStatus},
            {"identityKind", selected.identityKind},
            {"resolverVersion", STEAM_RESOLVER_VERSION},
            {"formalName", selected.name},
            {"secondaryEnrichment", nullptr}
        };
        if (!faultProviders.count("playnite-igdb")) {
            const std::vector<NameRound> exactRound = {{1, "verified-steam-formal-name", selected.name, {}}};
            const auto secondary = resolveIgdbGame(exactRound, pathEvidence, platformHint, igdbRounds);
            if (secondary && secondary->confident) {
                const auto agreement = scoreTitle(selected.name, secondary->name);
                arbitration["secondaryEnrichment"] = {
                    {"provider", "playnite-igdb"}, {"igdbId", secondary->igdbId},
                    {"formalName", secondary->name}, {"nameAgreement", agreement}
                };
                if (agreement >= 0.99) {
                    selected.igdbId = secondary->igdbId;
                    selected.igdbDetails = secondary->igdbDetails;
                    selected.metadataVerificationStatus = "metadata-verified";
                }
            }
        }
        return selected;
    }

    if (!igdbId) throw std::runtime_error("Direct ID mode requires a Steam AppID or IGDB ID");
    if (faultProviders.count("playnite-igdb")) throw std::runtime_error("Playnite IGDB provider failure was injected");
    auto selected = verifyIgdbId(*igdbId, platformHint, igdbRounds);
    arbitration = {
        {"decision", "manual-igdb-id-verified"},
        {"manualId", *igdbId},
        {"identityStatus", selected.identityStatus},
        {"identityKind", selected.identityKind},
        {"resolverVersion", STEAM_RESOLVER_VERSION},
        {"formalName", selected.name},
        {"secondaryEnrichment", nullptr}
    };
    const auto normalizedPlatform = asciiLower(trim(platformHint));
    const bool steamPlatformEligible = normalizedPlatform.empty() || normalizedPlatform == "any" ||
        normalizedPlatform == "all" || normalizedPlatform == "pc" || normalizedPlatform == "windows" ||
        normalizedPlatform == "pc-windows";
    if (steamPlatformEligible && !faultProviders.count("steam")) {
        const std::vector<NameRound> exactRound = {{1, "verified-igdb-formal-name", selected.name, {}}};
        const auto secondary = resolveSteamGame(exactRound, pathEvidence, steamRounds);
        if (secondary && secondary->confident) {
            const auto agreement = scoreTitle(selected.name, secondary->name);
            arbitration["secondaryEnrichment"] = {
                {"provider", "steam"}, {"appId", secondary->appId},
                {"formalName", secondary->name}, {"nameAgreement", agreement}
            };
            if (agreement >= 0.99) {
                selected.appId = secondary->appId;
                selected.details = secondary->details;
                selected.baseGameDetails = secondary->baseGameDetails;
                selected.storefrontAppId = secondary->storefrontAppId;
                selected.baseGameAppId = secondary->baseGameAppId;
                selected.primaryProvider = "steam";
                selected.identityStatus = "verified";
                selected.steamVerificationStatus = "steam-verified";
            }
        }
    }
    return selected;
}

struct ImageInfo {
    std::string extension;
    int width = 0;
    int height = 0;
    bool alphaAnalyzed = false;
    int visibleWidth = 0;
    int visibleHeight = 0;
    double visiblePixelRatio = 1.0;
};

static ULONG_PTR artworkInspectionGdiPlusToken();
static bool artworkBytesDecode(const std::vector<unsigned char>& bytes, int width, int height) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        static_cast<uint64_t>(width) * height > 32000000ull) return false;
    (void)artworkInspectionGdiPlusToken();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return false;
    auto* data = GlobalLock(memory);
    if (!data) { GlobalFree(memory); return false; }
    std::memcpy(data, bytes.data(), bytes.size());
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK || !stream) { GlobalFree(memory); return false; }
    auto release = [](IStream* value) { value->Release(); };
    std::unique_ptr<IStream, decltype(release)> owned(stream, release);
    Gdiplus::Bitmap bitmap(stream, FALSE);
    if (bitmap.GetLastStatus() != Gdiplus::Ok || bitmap.GetWidth() != width || bitmap.GetHeight() != height) return false;
    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData pixels{};
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &pixels) != Gdiplus::Ok) return false;
    const bool decoded = pixels.Scan0 != nullptr;
    bitmap.UnlockBits(&pixels);
    return decoded;
}

static std::optional<ImageInfo> inspectImage(const std::vector<unsigned char>& data) {
    if (data.size() >= 24 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G' &&
        data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A) {
        const int width = (static_cast<int>(data[16]) << 24) | (static_cast<int>(data[17]) << 16) |
                          (static_cast<int>(data[18]) << 8) | static_cast<int>(data[19]);
        const int height = (static_cast<int>(data[20]) << 24) | (static_cast<int>(data[21]) << 16) |
                           (static_cast<int>(data[22]) << 8) | static_cast<int>(data[23]);
        if (artworkBytesDecode(data, width, height)) return ImageInfo{"png", width, height, false, width, height, 1.0};
    }
    if (data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8) {
        size_t pos = 2;
        while (pos + 4 < data.size()) {
            while (pos < data.size() && data[pos] != 0xFF) ++pos;
            while (pos < data.size() && data[pos] == 0xFF) ++pos;
            if (pos >= data.size()) break;
            const unsigned char marker = data[pos++];
            if (marker == 0xD8 || marker == 0xD9 || marker == 0x01) continue;
            if (pos + 2 > data.size()) break;
            const size_t length = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
            if (length < 2 || pos + length > data.size()) break;
            const bool sof = (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
                             (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
            if (sof && length >= 7) {
                const int height = (static_cast<int>(data[pos + 3]) << 8) | data[pos + 4];
                const int width = (static_cast<int>(data[pos + 5]) << 8) | data[pos + 6];
                if (artworkBytesDecode(data, width, height)) return ImageInfo{"jpg", width, height, false, width, height, 1.0};
            }
            pos += length;
        }
    }
    return std::nullopt;
}

static ULONG_PTR artworkInspectionGdiPlusToken() {
    static const ULONG_PTR token = []() {
        ULONG_PTR value = 0;
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&value, &input, nullptr) != Gdiplus::Ok) {
            throw std::runtime_error("GDI+ startup failed for artwork inspection");
        }
        return value;
    }();
    return token;
}

static void inspectLogoVisiblePixels(const std::vector<unsigned char>& data, ImageInfo& image) {
    if (image.extension != "png" || data.empty()) return;
    const uint64_t pixelCount = static_cast<uint64_t>(image.width) * static_cast<uint64_t>(image.height);
    if (!pixelCount || pixelCount > 20000000ull) return;
    (void)artworkInspectionGdiPlusToken();

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!memory) return;
    void* bytes = GlobalLock(memory);
    if (!bytes) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(bytes, data.data(), data.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK || !stream) {
        GlobalFree(memory);
        return;
    }
    auto releaseStream = [](IStream* value) { if (value) value->Release(); };
    std::unique_ptr<IStream, decltype(releaseStream)> ownedStream(stream, releaseStream);

    Gdiplus::Bitmap bitmap(stream, FALSE);
    if (bitmap.GetLastStatus() != Gdiplus::Ok ||
        bitmap.GetWidth() != static_cast<UINT>(image.width) ||
        bitmap.GetHeight() != static_cast<UINT>(image.height)) {
        return;
    }

    Gdiplus::Rect bounds(0, 0, image.width, image.height);
    Gdiplus::BitmapData locked{};
    if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &locked) != Gdiplus::Ok) {
        return;
    }

    int left = image.width;
    int top = image.height;
    int right = -1;
    int bottom = -1;
    uint64_t visiblePixels = 0;
    auto* scan0 = static_cast<unsigned char*>(locked.Scan0);
    for (int y = 0; y < image.height; ++y) {
        auto* row = scan0 + static_cast<ptrdiff_t>(y) * locked.Stride;
        for (int x = 0; x < image.width; ++x) {
            if (row[x * 4 + 3] <= 16) continue;
            ++visiblePixels;
            left = (std::min)(left, x);
            top = (std::min)(top, y);
            right = (std::max)(right, x);
            bottom = (std::max)(bottom, y);
        }
    }
    bitmap.UnlockBits(&locked);

    image.alphaAnalyzed = true;
    image.visibleWidth = right >= left ? right - left + 1 : 0;
    image.visibleHeight = bottom >= top ? bottom - top + 1 : 0;
    image.visiblePixelRatio = static_cast<double>(visiblePixels) / static_cast<double>(pixelCount);
}

enum class ArtworkQualityBand {
    Rejected = 0,
    UniqueFallback = 1,
    Usable = 2,
    Preferred = 3,
};

struct ArtworkQualityAssessment {
    ArtworkQualityBand band = ArtworkQualityBand::Rejected;
    double score = 0.0;
    int effectiveWidth = 0;
    int effectiveHeight = 0;
    double aspectRatio = 0.0;
    std::string reason;
};

static const char* artworkQualityBandName(ArtworkQualityBand band) {
    switch (band) {
    case ArtworkQualityBand::Preferred: return "preferred";
    case ArtworkQualityBand::Usable: return "usable";
    case ArtworkQualityBand::UniqueFallback: return "unique-low-quality-fallback";
    default: return "rejected";
    }
}

static ArtworkQualityAssessment assessArtworkQuality(
    const std::string& type,
    const ImageInfo& image,
    bool genericBackground = false) {
    ArtworkQualityAssessment result;
    result.effectiveWidth = type == "logo" && image.alphaAnalyzed ? image.visibleWidth : image.width;
    result.effectiveHeight = type == "logo" && image.alphaAnalyzed ? image.visibleHeight : image.height;
    if (image.width <= 0 || image.height <= 0 || result.effectiveWidth <= 0 || result.effectiveHeight <= 0) {
        result.reason = "empty-image-or-visible-bounds";
        return result;
    }

    result.aspectRatio = static_cast<double>(image.width) / static_cast<double>(image.height);
    int preferredWidth = 0;
    int preferredHeight = 0;
    int usableWidth = 0;
    int usableHeight = 0;
    int fallbackWidth = 0;
    int fallbackHeight = 0;
    double targetRatio = result.aspectRatio;
    bool aspectOk = true;

    if (type == "tall") {
        preferredWidth = 600; preferredHeight = 900;
        usableWidth = 460; usableHeight = 690;
        fallbackWidth = 240; fallbackHeight = 360;
        targetRatio = 2.0 / 3.0;
        aspectOk = result.aspectRatio >= 0.55 && result.aspectRatio <= 0.82;
    } else if (type == "long") {
        preferredWidth = 920; preferredHeight = 430;
        usableWidth = 460; usableHeight = 215;
        fallbackWidth = 300; fallbackHeight = 140;
        targetRatio = 1196.0 / 559.0;
        const double minimumRatio = genericBackground ? 1.55 : 1.8;
        aspectOk = result.aspectRatio >= minimumRatio && result.aspectRatio <= 2.4;
    } else if (type == "hero") {
        preferredWidth = 1920; preferredHeight = 620;
        usableWidth = 1280; usableHeight = 400;
        fallbackWidth = 800; fallbackHeight = 250;
        targetRatio = 1920.0 / 620.0;
        const double minimumRatio = genericBackground ? 1.55 : 2.5;
        aspectOk = result.aspectRatio >= minimumRatio && result.aspectRatio <= 4.0;
    } else if (type == "logo") {
        preferredWidth = 512; preferredHeight = 128;
        usableWidth = 320; usableHeight = 80;
        fallbackWidth = 160; fallbackHeight = 40;
        const double visibleRatio = static_cast<double>(result.effectiveWidth) /
            static_cast<double>(result.effectiveHeight);
        targetRatio = visibleRatio;
        if (image.alphaAnalyzed && image.visiblePixelRatio < 0.015) {
            result.reason = "visible-logo-pixels-below-1.5-percent";
            return result;
        }
        if (image.width < 256 || image.height < 64) {
            result.reason = "logo-canvas-below-absolute-minimum";
            return result;
        }
    } else if (type == "icon") {
        preferredWidth = 512; preferredHeight = 512;
        usableWidth = 256; usableHeight = 256;
        fallbackWidth = 128; fallbackHeight = 128;
        aspectOk = result.aspectRatio >= 0.85 && result.aspectRatio <= 1.18;
        targetRatio = 1.0;
    } else {
        result.reason = "unknown-artwork-type";
        return result;
    }

    if (!aspectOk) {
        result.reason = "aspect-ratio-outside-supported-range";
        return result;
    }
    if (result.effectiveWidth < fallbackWidth || result.effectiveHeight < fallbackHeight) {
        result.reason = "resolution-below-unique-fallback-minimum";
        return result;
    }

    if (result.effectiveWidth >= preferredWidth && result.effectiveHeight >= preferredHeight) {
        result.band = ArtworkQualityBand::Preferred;
        result.reason = "meets-preferred-resolution";
    } else if (result.effectiveWidth >= usableWidth && result.effectiveHeight >= usableHeight &&
               (type != "logo" || (image.width >= 512 && image.height >= 128))) {
        result.band = ArtworkQualityBand::Usable;
        result.reason = "meets-normal-resolution";
    } else {
        result.band = ArtworkQualityBand::UniqueFallback;
        result.reason = "below-normal-resolution-retain-only-if-no-better-candidate";
    }

    const double widthFactor = (std::min)(2.0,
        static_cast<double>(result.effectiveWidth) / static_cast<double>(preferredWidth));
    const double heightFactor = (std::min)(2.0,
        static_cast<double>(result.effectiveHeight) / static_cast<double>(preferredHeight));
    const double ratio = type == "logo"
        ? targetRatio
        : static_cast<double>(image.width) / static_cast<double>(image.height);
    const double ratioCloseness = type == "logo" ? 1.0 :
        (std::max)(0.0, 1.0 - std::abs(std::log(ratio / targetRatio)));
    const double bandBase = result.band == ArtworkQualityBand::Preferred ? 300.0 :
        (result.band == ArtworkQualityBand::Usable ? 200.0 : 100.0);
    result.score = bandBase + widthFactor * 30.0 + heightFactor * 20.0 + ratioCloseness * 20.0;
    if (type == "logo" && image.extension == "png") result.score += 8.0;
    return result;
}

static bool dimensionsMatch(const std::string& type, const ImageInfo& image, bool genericBackground = false) {
    return assessArtworkQuality(type, image, genericBackground).band >= ArtworkQualityBand::Usable;
}

static json artworkQualityJson(const ArtworkQualityAssessment& quality, const ImageInfo& image) {
    return {
        {"band", artworkQualityBandName(quality.band)},
        {"score", quality.score},
        {"reason", quality.reason},
        {"effectiveWidth", quality.effectiveWidth},
        {"effectiveHeight", quality.effectiveHeight},
        {"aspectRatio", quality.aspectRatio},
        {"alphaAnalyzed", image.alphaAnalyzed},
        {"visibleWidth", image.visibleWidth},
        {"visibleHeight", image.visibleHeight},
        {"visiblePixelRatio", image.visiblePixelRatio}
    };
}

static json artworkQualityPolicyJson() {
    return {
        {"selection", "expected-source-order-then-measured-quality; stop after preferred or when remaining known routes cannot improve the selected tier; retain low quality only when no usable candidate exists"},
        {"overwrite", "low-quality-only candidates never replace existing Steam artwork when only-missing mode is used"},
        {"tall", {{"preferred", "600x900"}, {"usable", "460x690"}, {"uniqueFallbackMinimum", "240x360"}, {"aspect", "0.55-0.82"}}},
        {"long", {{"preferred", "920x430"}, {"usable", "460x215"}, {"uniqueFallbackMinimum", "300x140"}, {"aspect", "1.8-2.4; generic backgrounds 1.55-2.4"}}},
        {"hero", {{"preferred", "1920x620"}, {"usable", "1280x400"}, {"uniqueFallbackMinimum", "800x250"}, {"aspect", "2.5-4.0; generic backgrounds 1.55-4.0"}}},
        {"logo", {
            {"preferredVisibleBounds", "512x128"},
            {"usableVisibleBounds", "320x80"},
            {"uniqueFallbackVisibleMinimum", "160x40"},
            {"minimumCanvas", "256x64"},
            {"usableCanvas", "512x128"},
            {"minimumVisiblePixelRatio", 0.015},
            {"transparentPngPreferred", true}
        }}
    };
}

static std::string sha256(const std::vector<unsigned char>& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0, hashBytes = 0, resultBytes = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &resultBytes, 0);
    BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashBytes), sizeof(hashBytes), &resultBytes, 0);
    std::vector<unsigned char> object(objectBytes), digest(hashBytes);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), hashBytes, 0) < 0) {
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : digest) out << std::setw(2) << static_cast<int>(byte);
    return out.str();
}

class DataTransactionMutex {
public:
    explicit DataTransactionMutex(DWORD timeoutMs = 30000, const wchar_t* name = L"Local\\YeManCustomSteamLibraryDataTransaction") {
        handle_ = CreateMutexW(nullptr, FALSE, name);
        if (!handle_) throw std::runtime_error("Cannot create Custom Steam Library data transaction mutex");
        const auto wait = WaitForSingleObject(handle_, timeoutMs);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("Timed out waiting for Custom Steam Library data transaction");
        }
        locked_ = true;
    }
    DataTransactionMutex(const DataTransactionMutex&) = delete;
    DataTransactionMutex& operator=(const DataTransactionMutex&) = delete;
    ~DataTransactionMutex() {
        if (locked_) ReleaseMutex(handle_);
        if (handle_) CloseHandle(handle_);
    }
private:
    HANDLE handle_ = nullptr;
    bool locked_ = false;
};

class SteamShortcutWriterMutex : public DataTransactionMutex {
public:
    explicit SteamShortcutWriterMutex(DWORD timeoutMs = 10000)
        : DataTransactionMutex(timeoutMs, L"Local\\YeManSteamArtworkLabShortcutWriter") {}
};

struct ScopedStagedFiles {
    std::vector<fs::path> paths;
    void add(const fs::path& path) { if (!path.empty()) paths.push_back(path); }
    ~ScopedStagedFiles() { for (const auto& path : paths) { std::error_code error; fs::remove(custom_steam_library::ioPath(path), error); } }
};

static void writeBytesWin32(const fs::path& target, const std::vector<unsigned char>& data) {
    const auto ioTarget = custom_steam_library::ioPath(target);
    HANDLE handle = CreateFileW(ioTarget.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        throw std::runtime_error("无法创建输出文件：" + toUtf8(target.wstring()) +
            "（系统错误码：" + std::to_string(error) + "）");
    }
    size_t offset = 0;
    while (offset < data.size()) {
        const DWORD wanted = static_cast<DWORD>((std::min)(data.size() - offset, static_cast<size_t>(1u << 20)));
        DWORD written = 0;
        if (!WriteFile(handle, data.data() + offset, wanted, &written, nullptr) || written == 0) {
            const auto error = GetLastError(); CloseHandle(handle);
            throw std::runtime_error("输出文件写入失败：" + toUtf8(target.wstring()) +
                "（系统错误码：" + std::to_string(error) + "）");
        }
        offset += written;
    }
    if (!FlushFileBuffers(handle)) {
        const auto error = GetLastError(); CloseHandle(handle);
        throw std::runtime_error("输出文件刷新失败：" + toUtf8(target.wstring()) +
            "（系统错误码：" + std::to_string(error) + "）");
    }
    CloseHandle(handle);
}

static void writeAtomic(const fs::path& target, const std::vector<unsigned char>& data) {
    // A named cross-process mutex prevents two detached workers from opening
    // the same staging file at the same time.  Windows may keep a just-read
    // JSON/image file open for a short period (Steam, WebView, antivirus), so
    // activation must tolerate a transient sharing violation.
    DataTransactionMutex transaction;
    fs::create_directories(custom_steam_library::ioPath(target.parent_path()));
    static std::atomic<uint64_t> temporarySequence{0};
    const auto temp = target.wstring() + L".yeman-tmp-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++temporarySequence);
    ScopedStagedFiles temporaryCleanup;
    temporaryCleanup.add(fs::path(temp));
    writeBytesWin32(temp, data);
    DWORD lastError = ERROR_SUCCESS;
    bool activated = false;
    constexpr int activationAttempts = 20;
    for (int attempt = 1; attempt <= activationAttempts; ++attempt) {
        if (MoveFileExW(custom_steam_library::ioPath(temp).c_str(), custom_steam_library::ioPath(target).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            activated = true;
            break;
        }
        lastError = GetLastError();
        if (lastError != ERROR_SHARING_VIOLATION &&
            lastError != ERROR_ACCESS_DENIED &&
            lastError != ERROR_LOCK_VIOLATION) {
            break;
        }
        // Read-only files/directories are not transient reader contention.
        // Never remove the destination or force permissions to bypass a lock.
        const DWORD attributes = GetFileAttributesW(custom_steam_library::ioPath(target).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY))) break;
        if (attempt < activationAttempts) Sleep(static_cast<DWORD>((std::min)(attempt * 60, 720)));
    }
    if (!activated) {
        const auto cleanupError = DeleteFileW(custom_steam_library::ioPath(temp).c_str()) ? ERROR_SUCCESS : GetLastError();
        std::ostringstream detail;
        const DWORD attributes = GetFileAttributesW(custom_steam_library::ioPath(target).c_str());
        detail << "原子输出文件激活失败：" << toUtf8(target.wstring())
               << "（系统错误码：" << lastError << "）";
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            detail << "；目标路径是目录，无法替换为文件";
        } else if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
            detail << "；目标文件为只读，请检查文件属性";
        } else {
            detail << "；目标文件可能被其他程序持续占用，或没有文件/目录替换权限，请稍后重试";
        }
        detail << "；未主动删除或截断原文件";
        if (target.filename() == L"library-config.json") {
            detail << "；这是自定义库配置文件，并非 Steam 快捷方式文件；关闭 Steam 不一定能解除此占用";
        }
        if (cleanupError != ERROR_SUCCESS && cleanupError != ERROR_FILE_NOT_FOUND) {
            detail << "；临时文件清理失败（系统错误码：" << cleanupError << "）";
        }
        throw std::runtime_error(detail.str());
    }
}

static void writeJsonAtomic(const fs::path& target, const json& value) {
    const auto text = value.dump(2);
    writeAtomic(target, std::vector<unsigned char>(text.begin(), text.end()));
}

static void activateStagedFile(const fs::path& staged, const fs::path& target, const std::string& description) {
    DWORD lastError = ERROR_SUCCESS;
    for (int attempt = 1; attempt <= 12; ++attempt) {
        if (MoveFileExW(custom_steam_library::ioPath(staged).c_str(), custom_steam_library::ioPath(target).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        lastError = GetLastError();
        if (lastError != ERROR_SHARING_VIOLATION &&
            lastError != ERROR_ACCESS_DENIED &&
            lastError != ERROR_LOCK_VIOLATION) break;
        Sleep(static_cast<DWORD>((std::min)(attempt * 60, 720)));
    }
    const auto cleanupError = DeleteFileW(custom_steam_library::ioPath(staged).c_str()) ? ERROR_SUCCESS : GetLastError();
    std::ostringstream detail;
    detail << description << "失败，目标文件可能正被其他程序占用（系统错误码：" << lastError << "）";
    if (cleanupError != ERROR_SUCCESS && cleanupError != ERROR_FILE_NOT_FOUND) {
        detail << "；暂存文件清理失败（系统错误码：" << cleanupError << "）";
    }
    throw std::runtime_error(detail.str());
}

class SharedBinaryReadFile {
public:
    explicit SharedBinaryReadFile(const fs::path& path) {
        // Atomic replacement changes the directory entry, not the already-open
        // file object. Our readers must not block rename/delete while a worker
        // or Host publishes a new JSON/image snapshot.
        handle_ = CreateFileW(custom_steam_library::ioPath(path).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            throw std::runtime_error("Cannot open file: " + toUtf8(path.wstring()) +
                " (system error: " + std::to_string(error) + ")");
        }
    }
    SharedBinaryReadFile(const SharedBinaryReadFile&) = delete;
    SharedBinaryReadFile& operator=(const SharedBinaryReadFile&) = delete;
    ~SharedBinaryReadFile() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    HANDLE get() const { return handle_; }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

static std::vector<unsigned char> readBinaryFile(const fs::path& path, size_t maxBytes = 64u << 20) {
    SharedBinaryReadFile input(path);
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(input.get(), &length) || length.QuadPart < 0 ||
        static_cast<uint64_t>(length.QuadPart) > maxBytes) throw std::runtime_error("File is too large or its size cannot be read");
    std::vector<unsigned char> data(static_cast<size_t>(length.QuadPart));
    size_t offset = 0;
    while (offset < data.size()) {
        DWORD received = 0;
        const DWORD wanted = static_cast<DWORD>((std::min)(data.size() - offset, static_cast<size_t>(1u << 20)));
        if (!ReadFile(input.get(), data.data() + offset, wanted, &received, nullptr) || received == 0) {
            throw std::runtime_error("File changed or could not be read completely: " + toUtf8(path.wstring()));
        }
        offset += received;
    }
    LARGE_INTEGER after{};
    if (!GetFileSizeEx(input.get(), &after) || after.QuadPart != length.QuadPart) {
        throw std::runtime_error("File size changed while reading: " + toUtf8(path.wstring()));
    }
    return data;
}

static bool isHashedSteamStoreAssetUrl(const std::string& url) {
    const auto lower = asciiLower(url);
    const std::string marker = "/store_item_assets/steam/apps/";
    const auto markerPos = lower.find(marker);
    if (markerPos == std::string::npos) return false;
    const auto appIdEnd = lower.find('/', markerPos + marker.size());
    if (appIdEnd == std::string::npos) return false;
    const auto remainder = lower.substr(appIdEnd + 1);
    const auto nextSeparator = remainder.find('/');
    // The legacy fixed route is .../apps/<appid>/<filename>. A hashed route
    // adds a non-filename path segment before the filename.
    if (nextSeparator == std::string::npos) return false;
    const auto firstSegment = remainder.substr(0, nextSeparator);
    static const std::set<std::string> fixedNames = {
        "library_600x900_2x.jpg", "library_600x900.jpg", "header.jpg",
        "library_hero_2x.jpg", "library_hero.jpg", "logo_2x.png", "logo.png"
    };
    return fixedNames.count(firstSegment) == 0;
}

static fs::path steamRouteMemoryPath() {
    return g_workerDataRoot / L"cache" / L"network" / L"steam-route-preferences.json";
}

static void loadSteamRouteMemory() {
    if (g_steamRouteMemoryLoaded) return;
    g_steamRouteMemoryLoaded = true;
    if (g_workerDataRoot.empty()) return;
    try {
        const auto path = steamRouteMemoryPath();
        if (!fs::is_regular_file(path)) return;
        const auto bytes = readBinaryFile(path, 1u << 20);
        const auto value = json::parse(bytes.begin(), bytes.end());
        const auto updatedAt = value.value("updatedAt", uint64_t{0});
        const auto now = unixTimeMs();
        if (!value.is_object() || value.value("schemaVersion", 0) != 1 ||
            !value.value("hashedStoreAssetPreferDirect", false) || updatedAt == 0 ||
            now < updatedAt || now - updatedAt > STEAM_ROUTE_MEMORY_TTL_MS) return;
        g_steamHashedStoreAssetPreferDirect = true;
        g_steamRouteMemoryUpdatedAt = updatedAt;
    } catch (...) {
        // Route memory is best-effort. A damaged preference must never block
        // the canonical Steam request path.
    }
}

static bool preferDirectSteamStoreAssetRoute(const std::string& url) {
    if (!isHashedSteamStoreAssetUrl(url)) return false;
    loadSteamRouteMemory();
    return g_steamHashedStoreAssetPreferDirect;
}

static void rememberDirectSteamStoreAssetRoute(const std::string& url) {
    if (!isHashedSteamStoreAssetUrl(url)) return;
    loadSteamRouteMemory();
    g_steamHashedStoreAssetPreferDirect = true;
    g_steamRouteMemoryUpdatedAt = unixTimeMs();
    if (g_workerDataRoot.empty()) return;
    try {
        writeJsonAtomic(steamRouteMemoryPath(), {
            {"schemaVersion", 1},
            {"hashedStoreAssetPreferDirect", true},
            {"updatedAt", g_steamRouteMemoryUpdatedAt},
            {"reason", "canonical-url-succeeded-after-accelerator-miss"}
        });
    } catch (...) {
        // The in-process preference remains useful even when persistence is
        // unavailable for this run.
    }
}

static uint64_t steamRouteMemoryUpdatedAt() {
    loadSteamRouteMemory();
    return g_steamRouteMemoryUpdatedAt;
}

static bool isLegacyFixedSteamAssetUrl(const std::string& url) {
    const auto lower = asciiLower(url);
    const std::string marker = "/store_item_assets/steam/apps/";
    const auto markerPos = lower.find(marker);
    if (markerPos == std::string::npos) return false;
    const auto appIdEnd = lower.find('/', markerPos + marker.size());
    if (appIdEnd == std::string::npos) return false;
    const auto filename = lower.substr(appIdEnd + 1);
    if (filename.find('/') != std::string::npos) return false;
    static const std::set<std::string> fixedNames = {
        "library_600x900_2x.jpg", "library_600x900.jpg", "header.jpg",
        "library_hero_2x.jpg", "library_hero.jpg", "logo_2x.png", "logo.png"
    };
    return fixedNames.count(filename) != 0;
}

static fs::path artworkNegativeCachePath(const std::string& url) {
    const auto key = sha256(std::vector<unsigned char>(url.begin(), url.end()));
    return g_workerDataRoot / L"cache" / L"artwork-negative" / toWide(key + ".json");
}

static bool loadArtworkNegativeCache(const std::string& url, DWORD& status) {
    status = 0;
    if (g_workerDataRoot.empty() || !isLegacyFixedSteamAssetUrl(url)) return false;
    try {
        const auto path = artworkNegativeCachePath(url);
        if (!fs::is_regular_file(path)) return false;
        const auto bytes = readBinaryFile(path, 1u << 20);
        const auto value = json::parse(bytes.begin(), bytes.end());
        const auto cachedAt = value.value("cachedAt", uint64_t{0});
        const auto now = unixTimeMs();
        if (!value.is_object() || value.value("schemaVersion", 0) != 1 ||
            value.value("url", std::string{}) != url || value.value("status", 0) != 404 ||
            cachedAt == 0 || now < cachedAt || now - cachedAt > ARTWORK_NEGATIVE_CACHE_TTL_MS) {
            return false;
        }
        status = 404;
        return true;
    } catch (...) {
        return false;
    }
}

static void saveArtworkNegativeCache(const std::string& url, DWORD status) {
    if (g_workerDataRoot.empty() || status != 404 || !isLegacyFixedSteamAssetUrl(url)) return;
    try {
        writeJsonAtomic(artworkNegativeCachePath(url), {
            {"schemaVersion", 1}, {"url", url}, {"status", status},
            {"cachedAt", unixTimeMs()}, {"ttlMs", ARTWORK_NEGATIVE_CACHE_TTL_MS},
            {"kind", "legacy-fixed-steam-asset"}
        });
    } catch (...) {
        // Negative caching is an optimization only.
    }
}

static fs::path steamDetailsCachePath(int appId) {
    return g_workerDataRoot / L"cache" / L"steam-appdetails" /
        (std::to_wstring(appId) + L"-english-us.json");
}

static std::optional<SteamDetailsRecord> loadSteamDetailsCache(int appId, uint64_t& fetchedAt) {
    fetchedAt = 0;
    if (g_workerDataRoot.empty() || appId <= 0) return std::nullopt;
    try {
        const auto path = steamDetailsCachePath(appId);
        if (!fs::is_regular_file(path)) return std::nullopt;
        const auto bytes = readBinaryFile(path, MAX_METADATA_BYTES);
        const auto cached = json::parse(bytes.begin(), bytes.end());
        if (!cached.is_object() || cached.value("schemaVersion", 0) != 1 ||
            cached.value("steamAppId", 0) != appId || !cached.contains("data") ||
            !cached["data"].is_object()) return std::nullopt;
        fetchedAt = cached.value("fetchedAt", uint64_t{0});
        const auto envelope = json{
            {std::to_string(appId), {{"success", true}, {"data", cached["data"]}}}
        };
        const auto details = steamDetailsRecord(envelope, appId);
        if (details.canonicalAppId != appId) return std::nullopt;
        return details;
    } catch (...) {
        return std::nullopt;
    }
}

static void saveSteamDetailsCache(const SteamDetailsRecord& details) {
    if (g_workerDataRoot.empty() || details.canonicalAppId <= 0 || !details.data.is_object()) return;
    try {
        writeJsonAtomic(steamDetailsCachePath(details.canonicalAppId), {
            {"schemaVersion", 1},
            {"steamAppId", details.canonicalAppId},
            {"fetchedAt", unixTimeMs()},
            {"name", details.name},
            {"type", details.type},
            {"data", details.data}
        });
    } catch (...) {
        // Cache persistence is an optimization only. A read-only or full
        // data directory must never turn a successful network lookup into a
        // scraping failure.
    }
}

static std::string canonicalPathKey(const fs::path& path) {
    std::error_code ec;
    auto full = fs::weakly_canonical(path, ec);
    if (ec) full = fs::absolute(path, ec);
    auto value = toUtf8(full.wstring());
    std::replace(value.begin(), value.end(), '/', '\\');
    return asciiLower(value);
}

static void sortRecoveryCandidates(std::vector<fs::path>& candidates) {
    std::stable_sort(candidates.begin(), candidates.end(), [](const fs::path& left, const fs::path& right) {
        const auto leftTime = fs::last_write_time(custom_steam_library::ioPath(left));
        const auto rightTime = fs::last_write_time(custom_steam_library::ioPath(right));
        if (leftTime != rightTime) return leftTime > rightTime;
        if (left.extension() == L".bak" && right.extension() != L".bak") return true;
        if (right.extension() == L".bak" && left.extension() != L".bak") return false;
        return canonicalPathKey(left) > canonicalPathKey(right);
    });
}

static bool configRegularFile(const fs::path& path) {
    std::error_code error;
    const auto status = fs::status(custom_steam_library::ioPath(path), error);
    if (error == std::errc::no_such_file_or_directory) return false;
    if (error) throw std::runtime_error("Cannot inspect user configuration: " + toUtf8(path.wstring()));
    if (status.type() == fs::file_type::not_found) return false;
    if (!fs::is_regular_file(status)) throw std::runtime_error("User configuration path is not a regular file");
    return true;
}

static std::vector<fs::path> configRecoveryCandidates(const fs::path& path, const std::string& prefix) {
    std::vector<fs::path> candidates;
    const auto adjacent = path.parent_path() / (path.filename().wstring() + L".bak");
    if (configRegularFile(adjacent)) candidates.push_back(adjacent);
    const auto directory = path.parent_path().parent_path() / L"backups" / L"config";
    std::error_code error;
    const auto status = fs::status(custom_steam_library::ioPath(directory), error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect user configuration recovery directory");
    if (!error && status.type() != fs::file_type::not_found) {
        if (!fs::is_directory(status)) throw std::runtime_error("Configuration recovery path is not a directory");
        for (const auto& entry : fs::directory_iterator(custom_steam_library::ioPath(directory))) {
            if (entry.path().extension() == L".json" &&
                toUtf8(entry.path().filename().wstring()).starts_with(prefix + "-") &&
                entry.is_regular_file()) candidates.push_back(entry.path());
        }
    }
    sortRecoveryCandidates(candidates);
    return candidates;
}

static fs::path nextConfigBackupPath(const fs::path& directory, const std::string& prefix) {
    fs::create_directories(custom_steam_library::ioPath(directory));
    static std::atomic<uint64_t> sequence{0};
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const auto candidate = directory / toWide(prefix + "-" + std::to_string(unixTimeMs()) +
            "-" + std::to_string(++sequence) + ".json");
        std::error_code error;
        const auto exists = fs::exists(custom_steam_library::ioPath(candidate), error);
        if (error) throw std::runtime_error("Cannot inspect configuration backup filename");
        if (!exists) return candidate;
    }
    throw std::runtime_error("Cannot allocate configuration backup filename");
}

static json loadUserConfigDocument(
    const fs::path& path, size_t maximum, const std::string& prefix,
    const json& defaults, const std::function<json(json)>& normalize) {
    bool damaged = false;
    const auto parse = [&](const std::vector<unsigned char>& bytes) -> std::optional<json> {
        // The only runtime_error caught here is schema validation from normalize;
        // all filesystem reads, archives and writes occur outside this catch.
        try { return normalize(json::parse(bytes.begin(), bytes.end())); }
        catch (const json::exception&) { damaged = true; return std::nullopt; }
        catch (const std::runtime_error&) { damaged = true; return std::nullopt; }
    };
    const auto dataRoot = path.parent_path().parent_path();
    std::optional<fs::path> corrupt;
    if (configRegularFile(path)) {
        const auto bytes = readBinaryFile(path, maximum);
        if (auto value = parse(bytes)) return std::move(*value);
        corrupt = nextConfigBackupPath(dataRoot / L"backups" / L"corrupt", prefix);
        writeAtomic(*corrupt, bytes);
    }
    for (const auto& candidate : configRecoveryCandidates(path, prefix)) {
        const auto bytes = readBinaryFile(candidate, maximum);
        auto value = parse(bytes);
        if (!value) continue;
        (*value)["recoveredAt"] = unixTimeMs();
        (*value)["recoveredFromBackup"] = toUtf8(candidate.wstring());
        writeJsonAtomic(path, *value);
        const auto receipt = prefix == "library-config" ? L"last-config-recovery.json" : L"last-manual-overrides-recovery.json";
        writeJsonAtomic(dataRoot / L"state" / receipt, {
            {"recoveredAt", unixTimeMs()}, {"config", toUtf8(path.wstring())},
            {"corruptBackup", corrupt ? json(toUtf8(corrupt->wstring())) : json(nullptr)},
            {"recoveredFrom", toUtf8(candidate.wstring())}, {"usedDefaults", false}, {"usedEmptyDefaults", false}
        });
        return std::move(*value);
    }
    if (damaged) throw std::runtime_error("User configuration and available backups are corrupt; defaults were not written");
    // A genuine first-run absence is the only reason to manufacture defaults.
    return normalize(defaults);
}

static json applyConfigDelta(const json& current, const json& baseline, const json& submitted) {
    if (baseline == submitted) return current;
    if (baseline.is_object() && submitted.is_object() && current.is_object()) {
        auto result = current;
        for (const auto& [key, value] : baseline.items()) if (!submitted.contains(key)) result.erase(key);
        for (const auto& [key, value] : submitted.items()) {
            if (!baseline.contains(key)) result[key] = value;
            else if (baseline[key] != value) {
                result[key] = applyConfigDelta(result.contains(key) ? result[key] : json(nullptr), baseline[key], value);
            }
        }
        return result;
    }
    // Collection edits cannot safely infer item ownership. Fail/retry rather
    // than discard another process's concurrently submitted root/game list.
    if (baseline.is_array() && submitted.is_array() && current != baseline && current != submitted)
        throw std::runtime_error("Configuration collection changed during submission; retry without overwriting newer entries");
    return submitted;
}

static json loadManualOverrides(const fs::path& path) {
    DataTransactionMutex transaction;
    auto normalize = [](json value) {
        if (!value.is_object()) throw std::runtime_error("Manual override root is not an object");
        if (!value.contains("items")) value["items"] = json::object();
        else if (!value["items"].is_object()) throw std::runtime_error("Manual override items is not an object");
        value["schemaVersion"] = 1;
        return value;
    };
    return loadUserConfigDocument(path, 1u << 20, "manual-overrides", json{{"schemaVersion", 1}, {"items", json::object()}}, normalize);
}

static uint64_t unixTimeMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

// Steam stores LastPlayTime as a signed 32-bit Unix timestamp.  The normal
// path uses the import time.  Keep a deterministic date-only fallback for a
// damaged system clock or an unexpected time conversion failure; this is only
// used for the local simulated-recent signal and never starts the game.
static constexpr int32_t kSimulatedRecentFallbackUnixTime = 1787659200; // 2026-08-25 12:00:00 UTC

static int32_t simulatedImportUnixTime() noexcept {
    try {
        const auto seconds = unixTimeMs() / 1000;
        if (seconds > 0 && seconds <= static_cast<uint64_t>((std::numeric_limits<int32_t>::max)())) {
            return static_cast<int32_t>(seconds);
        }
    } catch (...) {
    }
    return kSimulatedRecentFallbackUnixTime;
}

static bool configDocumentValid(const std::string& prefix, const json& value) {
    if (prefix != "library-config" && prefix != "manual-overrides") return true;
    if (!value.is_object()) return false;
    if (prefix == "manual-overrides") return !value.contains("items") || value["items"].is_object();
    for (const auto* key : {"roots", "trainerRoots", "manualGameDirectories", "excludedGameDirectories"})
        if (value.contains(key) && !value[key].is_array()) return false;
    for (const auto* key : {"manualPrimary", "manualBuckets", "firstRunDefaultScan"})
        if (value.contains(key) && !value[key].is_object()) return false;
    if (value.contains("scanRootsExplicitlyCleared") && !value["scanRootsExplicitlyCleared"].is_boolean()) return false;
    return true;
}

static json writeJsonWithBackup(
    const fs::path& target,
    const fs::path& backupDirectory,
    const std::string& backupPrefix,
    const json& value, const json* baseline = nullptr) {
    // Serialization errors must occur before any backup or main file changes.
    const auto submittedText = value.dump(2);
    const size_t maximum = backupPrefix == "library-config" ? 4u << 20 :
        backupPrefix == "manual-overrides" ? 1u << 20 : 64u << 20;
    if (submittedText.size() > maximum) throw std::runtime_error("Configuration exceeds its read size limit");
    DataTransactionMutex transaction;
    const bool exists = configRegularFile(target);
    std::vector<unsigned char> bytes;
    std::optional<json> before;
    if (exists) {
        bytes = readBinaryFile(target, maximum);
        try { before = json::parse(bytes.begin(), bytes.end()); }
        catch (const json::parse_error&) { }
    }
    json committed = value;
    if (baseline) {
        // Rebase only the caller's changes, under the shared host/worker lock.
        // Unknown fields and unrelated games from a newer commit stay intact.
        if (!before || !configDocumentValid(backupPrefix, *before))
            throw std::runtime_error("Configuration changed to missing/corrupt data during submission");
        committed = applyConfigDelta(*before, *baseline, value);
    }
    const auto text = committed.dump(2);
    if (text.size() > maximum) throw std::runtime_error("Merged configuration exceeds its read size limit");
    if (before && backupPrefix == "library-config" && before->is_object() && committed.is_object()) {
        auto previous = *before, next = committed;
        previous.erase("updatedAt"); next.erase("updatedAt");
        if (previous == next) return *before;
    }
    if (exists) {
        const auto isUserConfig = backupPrefix == "library-config" || backupPrefix == "manual-overrides";
        if (before && (!isUserConfig || configDocumentValid(backupPrefix, *before))) {
            const auto adjacent = target.parent_path() / (target.filename().wstring() + L".bak");
            writeAtomic(adjacent, bytes);
            writeAtomic(nextConfigBackupPath(backupDirectory, backupPrefix), bytes);
        } else {
            // Diagnostic bytes must never replace the last valid recovery copy.
            writeAtomic(nextConfigBackupPath(target.parent_path().parent_path() / L"backups" / L"corrupt", backupPrefix), bytes);
        }
    }
    writeAtomic(target, std::vector<unsigned char>(text.begin(), text.end()));
    return committed;
}

static json updateManualOverridesAtomically(
    const fs::path& path,
    const fs::path& backupDirectory,
    const std::function<void(json&)>& update) {
    // Serialize the complete read/modify/write cycle. A mutex around only
    // writeJsonWithBackup still lets two workers read the same old snapshot
    // and lose one manual edit.
    DataTransactionMutex transaction;
    auto latest = loadManualOverrides(path);
    update(latest);
    writeJsonWithBackup(path, backupDirectory, "manual-overrides", latest);
    return latest;
}

static std::vector<fs::path> stateBackupCandidates(
    const fs::path& dataRoot,
    const fs::path& target,
    const std::string& prefix) {
    std::vector<fs::path> candidates;
    const auto adjacent = target.parent_path() / (target.filename().wstring() + L".bak");
    if (fs::is_regular_file(adjacent)) candidates.push_back(adjacent);
    const auto backupDirectory = dataRoot / L"backups" / L"state";
    std::error_code ec;
    if (fs::is_directory(backupDirectory, ec)) {
        for (const auto& entry : fs::directory_iterator(backupDirectory, ec)) {
            if (ec) { ec.clear(); break; }
            const auto name = toUtf8(entry.path().filename().wstring());
            if (entry.is_regular_file(ec) && name.starts_with(prefix + "-") &&
                entry.path().extension() == L".json") {
                candidates.push_back(entry.path());
            }
            ec.clear();
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const fs::path& left, const fs::path& right) {
        std::error_code leftError, rightError;
        const auto leftTime = fs::last_write_time(left, leftError);
        const auto rightTime = fs::last_write_time(right, rightError);
        if (!leftError && !rightError && leftTime != rightTime) return leftTime > rightTime;
        return canonicalPathKey(left) > canonicalPathKey(right);
    });
    return candidates;
}

static std::optional<fs::path> preserveCorruptStateFile(
    const fs::path& dataRoot,
    const fs::path& target,
    const std::string& prefix) {
    if (!fs::is_regular_file(target)) return std::nullopt;
    const auto directory = dataRoot / L"backups" / L"corrupt";
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) return std::nullopt;
    const auto corrupt = directory /
        (toWide(prefix) + L"-" + std::to_wstring(unixTimeMs()) + L".json");
    fs::copy_file(target, corrupt, fs::copy_options::overwrite_existing, ec);
    if (ec) return std::nullopt;
    return corrupt;
}

static std::optional<std::string> persistedManualName(const json& overrides, const std::string& exeKey) {
    if (!overrides.contains("items") || !overrides["items"].is_object()) return std::nullopt;
    const auto it = overrides["items"].find(exeKey);
    if (it == overrides["items"].end() || !it->is_object()) return std::nullopt;
    const auto value = trim(it->value("name", std::string{}));
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

class BinaryVdfReader {
public:
    explicit BinaryVdfReader(std::vector<unsigned char> bytes) : data_(std::move(bytes)) {}

    json parse() {
        json root = json::object();
        while (pos_ < data_.size()) {
            const auto type = readByte();
            if (type == 8) break;
            const auto key = readCString();
            root[key] = readValue(type);
        }
        return root;
    }

private:
    std::vector<unsigned char> data_;
    size_t pos_ = 0;

    unsigned char readByte() {
        if (pos_ >= data_.size()) throw std::runtime_error("Unexpected end of binary VDF");
        return data_[pos_++];
    }

    std::string readCString() {
        const size_t start = pos_;
        while (pos_ < data_.size() && data_[pos_] != 0) ++pos_;
        if (pos_ >= data_.size()) throw std::runtime_error("Unterminated VDF string");
        std::string value(reinterpret_cast<const char*>(data_.data() + start), pos_ - start);
        ++pos_;
        return value;
    }

    uint32_t readU32() {
        if (pos_ + 4 > data_.size()) throw std::runtime_error("Truncated VDF int32");
        uint32_t value = static_cast<uint32_t>(data_[pos_]) |
            (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
            (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
            (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return value;
    }

    uint64_t readU64() {
        if (pos_ + 8 > data_.size()) throw std::runtime_error("Truncated VDF uint64");
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
        pos_ += 8;
        return value;
    }

    std::string readWideCString() {
        std::wstring value;
        for (;;) {
            if (pos_ + 2 > data_.size()) throw std::runtime_error("Truncated VDF wide string");
            const wchar_t ch = static_cast<wchar_t>(data_[pos_]) | static_cast<wchar_t>(data_[pos_ + 1] << 8);
            pos_ += 2;
            if (ch == 0) break;
            value.push_back(ch);
        }
        return toUtf8(value);
    }

    json readObject() {
        json value = json::object();
        for (;;) {
            const auto type = readByte();
            if (type == 8) break;
            const auto key = readCString();
            value[key] = readValue(type);
        }
        return value;
    }

    json readValue(unsigned char type) {
        switch (type) {
            case 0: return readObject();
            case 1: return readCString();
            case 2: {
                const uint32_t raw = readU32();
                int32_t signedValue = 0;
                std::memcpy(&signedValue, &raw, sizeof(raw));
                return signedValue;
            }
            case 3: {
                const uint32_t raw = readU32();
                float value = 0;
                std::memcpy(&value, &raw, sizeof(raw));
                return value;
            }
            case 5: return readWideCString();
            case 6: return readU32();
            case 7: return readU64();
            default: throw std::runtime_error("Unsupported binary VDF type: " + std::to_string(type));
        }
    }
};

// A second VDF representation is intentionally kept alongside the audit
// reader.  The audit reader maps into JSON for convenient inspection, while
// this document preserves entry order and every supported scalar type so a
// shortcut append cannot silently discard fields owned by Steam or another
// manager.
struct BinaryVdfNode {
    unsigned char type = 0;
    std::vector<std::pair<std::string, BinaryVdfNode>> objectValue;
    std::string stringValue;
    uint32_t u32Value = 0;
    uint64_t u64Value = 0;
    float floatValue = 0.0f;
};

class BinaryVdfDocument {
public:
    static BinaryVdfDocument emptySteamShortcuts() {
        BinaryVdfDocument document;
        document.root_.type = 0;
        BinaryVdfNode shortcuts;
        shortcuts.type = 0;
        document.root_.objectValue.push_back({"shortcuts", std::move(shortcuts)});
        return document;
    }

    static BinaryVdfDocument parse(std::vector<unsigned char> bytes) {
        BinaryVdfDocument document;
        document.data_ = std::move(bytes);
        document.root_.type = 0;
        document.root_.objectValue = document.readObjectBody();
        if (document.pos_ != document.data_.size()) {
            throw std::runtime_error("Binary VDF contains trailing bytes");
        }
        document.data_.clear();
        document.pos_ = 0;
        return document;
    }

    BinaryVdfNode& root() { return root_; }
    const BinaryVdfNode& root() const { return root_; }

    std::vector<unsigned char> serialize() const {
        std::vector<unsigned char> output;
        writeObjectBody(output, root_.objectValue);
        return output;
    }

private:
    BinaryVdfNode root_;
    std::vector<unsigned char> data_;
    size_t pos_ = 0;

    unsigned char readByte() {
        if (pos_ >= data_.size()) throw std::runtime_error("Unexpected end of binary VDF");
        return data_[pos_++];
    }

    std::string readCString() {
        const size_t start = pos_;
        while (pos_ < data_.size() && data_[pos_] != 0) ++pos_;
        if (pos_ >= data_.size()) throw std::runtime_error("Unterminated VDF string");
        std::string value(reinterpret_cast<const char*>(data_.data() + start), pos_ - start);
        ++pos_;
        return value;
    }

    uint32_t readU32() {
        if (pos_ + 4 > data_.size()) throw std::runtime_error("Truncated VDF uint32");
        uint32_t value = static_cast<uint32_t>(data_[pos_]) |
            (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
            (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
            (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return value;
    }

    uint64_t readU64() {
        if (pos_ + 8 > data_.size()) throw std::runtime_error("Truncated VDF uint64");
        uint64_t value = 0;
        for (int index = 0; index < 8; ++index) {
            value |= static_cast<uint64_t>(data_[pos_ + index]) << (index * 8);
        }
        pos_ += 8;
        return value;
    }

    std::string readWideCString() {
        std::wstring value;
        for (;;) {
            if (pos_ + 2 > data_.size()) throw std::runtime_error("Truncated VDF wide string");
            const wchar_t ch = static_cast<wchar_t>(data_[pos_]) |
                static_cast<wchar_t>(static_cast<uint16_t>(data_[pos_ + 1]) << 8);
            pos_ += 2;
            if (ch == 0) break;
            value.push_back(ch);
        }
        return toUtf8(value);
    }

    BinaryVdfNode readValue(unsigned char type) {
        BinaryVdfNode node;
        node.type = type;
        switch (type) {
            case 0:
                node.objectValue = readObjectBody();
                break;
            case 1:
                node.stringValue = readCString();
                break;
            case 2:
            case 6:
                node.u32Value = readU32();
                break;
            case 3: {
                const uint32_t raw = readU32();
                std::memcpy(&node.floatValue, &raw, sizeof(raw));
                break;
            }
            case 5:
                node.stringValue = readWideCString();
                break;
            case 7:
                node.u64Value = readU64();
                break;
            default:
                throw std::runtime_error("Unsupported binary VDF type: " + std::to_string(type));
        }
        return node;
    }

    std::vector<std::pair<std::string, BinaryVdfNode>> readObjectBody() {
        std::vector<std::pair<std::string, BinaryVdfNode>> entries;
        for (;;) {
            const auto type = readByte();
            if (type == 8) break;
            auto key = readCString();
            entries.push_back({std::move(key), readValue(type)});
        }
        return entries;
    }

    static void appendByte(std::vector<unsigned char>& output, unsigned char value) {
        output.push_back(value);
    }

    static void appendCString(std::vector<unsigned char>& output, const std::string& value) {
        if (value.find('\0') != std::string::npos) throw std::runtime_error("VDF string contains NUL");
        output.insert(output.end(), value.begin(), value.end());
        output.push_back(0);
    }

    static void appendU32(std::vector<unsigned char>& output, uint32_t value) {
        for (int index = 0; index < 4; ++index) {
            output.push_back(static_cast<unsigned char>((value >> (index * 8)) & 0xFF));
        }
    }

    static void appendU64(std::vector<unsigned char>& output, uint64_t value) {
        for (int index = 0; index < 8; ++index) {
            output.push_back(static_cast<unsigned char>((value >> (index * 8)) & 0xFF));
        }
    }

    static void writeValue(std::vector<unsigned char>& output, const BinaryVdfNode& node) {
        switch (node.type) {
            case 0:
                writeObjectBody(output, node.objectValue);
                break;
            case 1:
                appendCString(output, node.stringValue);
                break;
            case 2:
            case 6:
                appendU32(output, node.u32Value);
                break;
            case 3: {
                uint32_t raw = 0;
                std::memcpy(&raw, &node.floatValue, sizeof(raw));
                appendU32(output, raw);
                break;
            }
            case 5: {
                const auto wide = toWide(node.stringValue);
                for (wchar_t ch : wide) {
                    const auto value = static_cast<uint16_t>(ch);
                    output.push_back(static_cast<unsigned char>(value & 0xFF));
                    output.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
                }
                output.push_back(0);
                output.push_back(0);
                break;
            }
            case 7:
                appendU64(output, node.u64Value);
                break;
            default:
                throw std::runtime_error("Unsupported binary VDF type during write: " + std::to_string(node.type));
        }
    }

    static void writeObjectBody(
        std::vector<unsigned char>& output,
        const std::vector<std::pair<std::string, BinaryVdfNode>>& entries) {
        for (const auto& [key, node] : entries) {
            appendByte(output, node.type);
            appendCString(output, key);
            writeValue(output, node);
        }
        appendByte(output, 8);
    }
};

static BinaryVdfNode* vdfFindObject(BinaryVdfNode& parent, const std::string& key) {
    if (parent.type != 0) return nullptr;
    const auto wanted = asciiLower(key);
    for (auto& [name, node] : parent.objectValue) {
        if (asciiLower(name) == wanted && node.type == 0) return &node;
    }
    return nullptr;
}

static const BinaryVdfNode* vdfFindObject(const BinaryVdfNode& parent, const std::string& key) {
    if (parent.type != 0) return nullptr;
    const auto wanted = asciiLower(key);
    for (const auto& [name, node] : parent.objectValue) {
        if (asciiLower(name) == wanted && node.type == 0) return &node;
    }
    return nullptr;
}

static BinaryVdfNode vdfString(std::string value) {
    BinaryVdfNode node;
    node.type = 1;
    node.stringValue = std::move(value);
    return node;
}

static BinaryVdfNode vdfInt32(int32_t value) {
    BinaryVdfNode node;
    node.type = 2;
    std::memcpy(&node.u32Value, &value, sizeof(value));
    return node;
}

static BinaryVdfNode vdfObject(std::vector<std::pair<std::string, BinaryVdfNode>> values = {}) {
    BinaryVdfNode node;
    node.type = 0;
    node.objectValue = std::move(values);
    return node;
}

static std::vector<unsigned char> hexBytes(const std::string& hex) {
    if (hex.size() % 2 != 0) throw std::runtime_error("Steam recent-game record has an odd hex length");
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t index = 0; index < hex.size(); index += 2) {
        const auto high = std::isdigit(static_cast<unsigned char>(hex[index])) ? hex[index] - '0' :
            std::tolower(static_cast<unsigned char>(hex[index])) - 'a' + 10;
        const auto low = std::isdigit(static_cast<unsigned char>(hex[index + 1])) ? hex[index + 1] - '0' :
            std::tolower(static_cast<unsigned char>(hex[index + 1])) - 'a' + 10;
        if (high < 0 || high > 15 || low < 0 || low > 15) throw std::runtime_error("Steam recent-game record has invalid hex");
        bytes.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return bytes;
}

static void appendLittleEndian32(std::vector<unsigned char>& bytes, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<unsigned char>((value >> shift) & 0xFF));
}

static uint32_t readLittleEndian32(const std::vector<unsigned char>& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static std::string bytesHex(const std::vector<unsigned char>& bytes) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) output << std::setw(2) << static_cast<int>(byte);
    return output.str();
}

static bool injectRecentLocalPlayedGameIds(
    std::string& localConfig,
    const std::vector<uint32_t>& shortAppIds,
    int32_t timestamp) {
    static const std::regex recentExpression(R"VDF("RecentLocalPlayedGameIDs"\s*"([0-9A-Fa-f]*)")VDF");
    std::smatch match;
    const bool hasRecentField = std::regex_search(localConfig, match, recentExpression);
    const auto bytes = hasRecentField ? hexBytes(match[1].str()) : std::vector<unsigned char>{};
    if (bytes.size() % 16 != 0) throw std::runtime_error("Steam recent-game record size is invalid");
    struct RecentRecord { uint32_t kind; uint32_t shortAppId; uint32_t timestamp; uint32_t flag; };
    std::vector<RecentRecord> records;
    for (size_t offset = 0; offset < bytes.size(); offset += 16) {
        records.push_back({readLittleEndian32(bytes, offset), readLittleEndian32(bytes, offset + 4),
            readLittleEndian32(bytes, offset + 8), readLittleEndian32(bytes, offset + 12)});
    }
    std::set<uint32_t> requested;
    for (const auto shortAppId : shortAppIds) requested.insert(shortAppId);
    records.erase(std::remove_if(records.begin(), records.end(), [&](const RecentRecord& record) {
        return requested.count(record.shortAppId) != 0;
    }), records.end());
    std::vector<RecentRecord> updated;
    updated.reserve(shortAppIds.size() + records.size());
    for (auto iterator = shortAppIds.rbegin(); iterator != shortAppIds.rend(); ++iterator) {
        updated.push_back({0x02000000u, *iterator, static_cast<uint32_t>(timestamp), 1u});
    }
    updated.insert(updated.end(), records.begin(), records.end());
    if (updated.size() > 50) updated.resize(50);
    std::vector<unsigned char> encoded;
    encoded.reserve(updated.size() * 16);
    for (const auto& record : updated) {
        appendLittleEndian32(encoded, record.kind);
        appendLittleEndian32(encoded, record.shortAppId);
        appendLittleEndian32(encoded, record.timestamp);
        appendLittleEndian32(encoded, record.flag);
    }
    const auto encodedHex = bytesHex(encoded);
    if (hasRecentField) {
        const auto replacementStart = static_cast<size_t>(match.position(1));
        localConfig.replace(replacementStart, match.length(1), encodedHex);
    } else {
        const auto closingBrace = localConfig.find_last_of('}');
        if (closingBrace == std::string::npos) return false;
        const auto newline = localConfig.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        localConfig.insert(closingBrace,
            std::string("\t\"RecentLocalPlayedGameIDs\"\t\t\"") + encodedHex + "\"" + newline);
    }
    return true;
}

static std::string injectSteamSimulatedRecentState(
    std::string localConfig,
    const json& plan,
    int32_t timestamp) {
    std::vector<uint32_t> shortAppIds;
    for (const auto& item : plan.value("items", json::array())) {
        if (item.value("status", std::string{}) != "ready-to-add") continue;
        const auto shortId = item.value("shortAppId", std::string{});
        if (shortId.empty()) throw std::runtime_error("Steam shortcut short AppID is missing");
        shortAppIds.push_back(static_cast<uint32_t>(std::stoul(shortId)));
    }
    if (!shortAppIds.empty() && !injectRecentLocalPlayedGameIds(localConfig, shortAppIds, timestamp)) {
        throw std::runtime_error("Steam localconfig.vdf recent-game section is unavailable");
    }
    return localConfig;
}

// Recent-game metadata is optional. Never synthesize a Steam account config or
// abort an otherwise valid shortcut import when this account has no readable,
// usable localconfig.vdf (first import, stale account, or transient file lock).
struct SteamRecentSignalUpdate {
    bool enabled = false;
    std::string status = "not-requested";
    std::string detail;
    std::vector<unsigned char> original;
    std::string updated;
};

static SteamRecentSignalUpdate prepareSteamRecentSignalUpdate(
    const fs::path& path, const json& plan, int32_t timestamp, bool requested) {
    SteamRecentSignalUpdate result;
    if (!requested) return result;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        result.status = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
            ? "localconfig-missing" : "localconfig-unreadable";
        result.detail = "Windows error " + std::to_string(error);
        return result;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        result.status = "localconfig-not-a-file";
        return result;
    }
    try {
        result.original = readBinaryFile(path, 64u << 20);
    } catch (const std::exception& error) {
        result.status = "localconfig-unreadable";
        result.detail = error.what();
        return result;
    }
    try {
        const std::string originalText(result.original.begin(), result.original.end());
        result.updated = injectSteamSimulatedRecentState(originalText, plan, timestamp);
    } catch (const std::exception& error) {
        result.status = "localconfig-invalid";
        result.detail = error.what();
        return result;
    }
    result.enabled = true;
    result.status = "prepared";
    return result;
}

static json runSteamRecentSignalSelfTest() {
    constexpr uint32_t testShortAppId = 3221151844u;
    constexpr int32_t testTimestamp = 1787659200;
    std::string localConfig =
        "\"localconfig\"\n"
        "{\n"
        "\t\"RecentLocalPlayedGameIDs\"\t\t\"\"\n"
        "}\n";
    json plan = json::object();
    plan["items"] = json::array({{
        {"status", "ready-to-add"}, {"shortAppId", std::to_string(testShortAppId)}
    }, {
        {"status", "already-in-steam"}, {"shortAppId", "2388972698"}
    }});
    const auto updated = injectSteamSimulatedRecentState(localConfig, plan, testTimestamp);
    static const std::regex recentExpression(R"VDF("RecentLocalPlayedGameIDs"\s*"([0-9A-Fa-f]*)")VDF");
    std::smatch match;
    const bool found = std::regex_search(updated, match, recentExpression);
    const auto bytes = found ? hexBytes(match[1].str()) : std::vector<unsigned char>{};
    const bool recordValid = bytes.size() == 16 &&
        readLittleEndian32(bytes, 0) == 0x02000000u &&
        readLittleEndian32(bytes, 4) == testShortAppId &&
        readLittleEndian32(bytes, 8) == static_cast<uint32_t>(testTimestamp) &&
        readLittleEndian32(bytes, 12) == 1u;
    return {
        {"mode", "steam-recent-signal-self-test"},
        {"allPassed", found && recordValid},
        {"recordCount", bytes.size() / 16},
        {"shortAppId", testShortAppId},
        {"timestamp", testTimestamp},
        {"fallbackTimestamp", kSimulatedRecentFallbackUnixTime},
        {"doesNotLaunchGame", true}
    };
}

static std::string jsonStringCaseInsensitive(const json& value, const std::string& key) {
    if (!value.is_object()) return {};
    const auto wanted = asciiLower(key);
    for (const auto& [name, item] : value.items()) {
        if (asciiLower(name) == wanted && item.is_string()) return item.get<std::string>();
    }
    return {};
}

static std::optional<int32_t> jsonIntCaseInsensitive(const json& value, const std::string& key) {
    if (!value.is_object()) return std::nullopt;
    const auto wanted = asciiLower(key);
    for (const auto& [name, item] : value.items()) {
        if (asciiLower(name) == wanted && item.is_number_integer()) return item.get<int32_t>();
    }
    return std::nullopt;
}

struct SteamShortcut {
    std::string accountId;
    fs::path shortcutsFile;
    int32_t storedAppId = 0;
    std::string appName;
    std::string exe;
    std::string startDir;
    std::string launchOptions;
};

static std::string unquote(std::string value) {
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') return value.substr(1, value.size() - 2);
    return value;
}

struct EmulatorProfile {
    std::string id;
    std::string name;
    std::string playnitePlatform;
    std::string platformHint;
    std::string platformLabel;
    int igdbPlatformId = 0;
    int steamArtworkAppId = 0;
    uint32_t colorA = 0;
    uint32_t colorB = 0;
    std::vector<std::string> executableNames;
    std::vector<std::string> imageExtensions;
    std::string playniteStartupArguments;
    bool executablePrefix = false;
    bool directoryTarget = false;
    bool scriptGameImport = false;
};

static const std::vector<EmulatorProfile>& emulatorProfiles() {
    static const std::vector<EmulatorProfile> profiles = {
        {"yuzu", "yuzu", "nintendo_switch", "switch", "Nintendo Switch", 130, 0, 0x2B8BD6, 0x151B2B,
         {"yuzu.exe"}, {"nso", "nro", "nca", "xci", "nsp"}, "\"{ImagePath}\""},
        {"eden", "Eden", "nintendo_switch", "switch", "Nintendo Switch", 130, 0, 0x75C66B, 0x17251E,
         {"eden.exe"}, {"nso", "nro", "nca", "xci", "nsp"}, "-f -g \"{ImagePath}\""},
        {"ryujinx", "Ryujinx", "nintendo_switch", "switch", "Nintendo Switch", 130, 0, 0xE64A4A, 0x27171B,
         {"ryujinx.exe", "ryujinx.ava.exe"}, {"xci", "nsp"}, "--fullscreen \"{ImagePath}\""},
        {"pcsx2", "PCSX2", "sony_playstation2", "ps2", "PlayStation 2", 8, 0, 0x376EB5, 0x151C31,
         {"pcsx2.exe", "pcsx2x64.exe", "pcsx2-qt.exe", "pcsx2x64-avx2.exe", "pcsx2-qtx64-avx2.exe", "pcsx2-qtx64.exe"},
         {"iso", "bin", "mdf", "nrg", "img", "gz", "cso", "chd", "m3u"},
         "\"{ImagePath}\" --nogui --fullboot --fullscreen"},
        {"ppsspp", "PPSSPP", "sony_psp", "psp", "PlayStation Portable", 38, 0, 0x28A8E0, 0x172331,
         {"ppssppwindows64.exe", "ppssppwindows.exe"}, {"iso", "cso", "pbp", "chd"},
         "\"{ImagePath}\" --pause-menu-exit --fullscreen"},
        {"rpcs3", "RPCS3", "sony_playstation3", "ps3", "PlayStation 3", 9, 0, 0xEFC24C, 0x231C16,
         {"rpcs3"}, {"iso"}, "\"{ImagePath}\"", true, true, true},
        {"retroarch", "RetroArch", "", "any", "Multi-system emulator", 0, 1118310, 0x4A4A4A, 0x111111,
         {"retroarch.exe"}, {"zip", "7z", "iso", "bin", "cue", "chd", "nes", "sfc", "smc", "gba", "gbc", "gb", "n64", "z64"},
         "-L \"{EmulatorDir}\\cores\\{Core}.dll\" \"{ImagePath}\""}
    };
    return profiles;
}

static const EmulatorProfile* findEmulatorProfile(const fs::path& executable) {
    const auto fileName = asciiLower(toUtf8(executable.filename().wstring()));
    for (const auto& profile : emulatorProfiles()) {
        if (profile.executablePrefix) {
            for (const auto& prefix : profile.executableNames) {
                if (fileName.starts_with(prefix) && fileName.ends_with(".exe")) return &profile;
            }
        } else {
            for (const auto& name : profile.executableNames) {
                if (fileName == name) return &profile;
            }
        }
    }
    return nullptr;
}

static std::vector<std::string> windowsCommandLineArguments(const std::string& commandLine) {
    if (trim(commandLine).empty()) return {};
    const std::wstring wrapped = L"SteamArtworkLab.exe " + toWide(commandLine);
    int count = 0;
    LPWSTR* values = CommandLineToArgvW(wrapped.c_str(), &count);
    if (!values) throw std::runtime_error("CommandLineToArgvW failed");
    std::vector<std::string> result;
    for (int i = 1; i < count; ++i) result.push_back(toUtf8(values[i]));
    LocalFree(values);
    return result;
}

static fs::path expandEnvironmentPath(const std::string& value) {
    const auto wide = toWide(value);
    const DWORD needed = ExpandEnvironmentStringsW(wide.c_str(), nullptr, 0);
    if (!needed) return fs::path(wide);
    std::wstring expanded(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(wide.c_str(), expanded.data(), needed);
    if (!written || written > needed) return fs::path(wide);
    while (!expanded.empty() && expanded.back() == L'\0') expanded.pop_back();
    return fs::path(expanded);
}

static std::string removeUtf8Text(std::string value, const std::string& needle) {
    for (size_t pos = value.find(needle); pos != std::string::npos; pos = value.find(needle, pos)) {
        value.erase(pos, needle.size());
    }
    return value;
}

static std::string cleanRomTitle(const std::string& raw) {
    std::string value = raw;
    std::string withoutTags;
    int roundDepth = 0;
    int squareDepth = 0;
    for (char ch : value) {
        if (ch == '(') { ++roundDepth; continue; }
        if (ch == ')' && roundDepth > 0) { --roundDepth; continue; }
        if (ch == '[') { ++squareDepth; continue; }
        if (ch == ']' && squareDepth > 0) { --squareDepth; continue; }
        if (roundDepth == 0 && squareDepth == 0) withoutTags.push_back(ch);
    }
    value = removeUtf8Text(std::move(withoutTags), "™");
    value = removeUtf8Text(std::move(value), "®");
    value = removeUtf8Text(std::move(value), "©");
    std::replace(value.begin(), value.end(), '_', ' ');
    try {
        value = std::regex_replace(value,
            std::regex(R"(\b(disc|disk|cd|dvd|side)\s*[-_ ]*[0-9a-z]+\b)", std::regex::icase), " ");
        value = std::regex_replace(value,
            std::regex(R"(\b(v|ver|version)\s*[0-9]+([._-][0-9]+)*\b)", std::regex::icase), " ");
    } catch (...) {
        // Keep the untagged title if the platform regex implementation fails.
    }
    return collapseSpaces(humanizeTitle(value));
}

static std::string ps3SerialFromText(const std::string& text) {
    try {
        std::smatch match;
        if (std::regex_search(text, match,
            std::regex(R"((BLUS|BLES|NPUB|NPEB|BCUS|BCES|NPUA|NPEA)[-_ ]?([0-9]{5}))", std::regex::icase))) {
            auto serial = asciiLower(match[1].str() + match[2].str());
            std::transform(serial.begin(), serial.end(), serial.begin(), [](unsigned char ch) {
                return static_cast<char>(std::toupper(ch));
            });
            return serial;
        }
    } catch (...) {}
    return {};
}

static uint16_t readLe16(const std::vector<unsigned char>& bytes, size_t offset) {
    if (offset + 2 > bytes.size()) throw std::runtime_error("PARAM.SFO uint16 is truncated");
    return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1] << 8);
}

static uint32_t readLe32(const std::vector<unsigned char>& bytes, size_t offset) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("PARAM.SFO uint32 is truncated");
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static std::string boundedCString(const std::vector<unsigned char>& bytes, size_t offset, size_t maximum) {
    if (offset >= bytes.size()) throw std::runtime_error("PARAM.SFO string offset is invalid");
    const size_t end = (std::min)(bytes.size(), offset + maximum);
    size_t cursor = offset;
    while (cursor < end && bytes[cursor] != 0) ++cursor;
    return std::string(reinterpret_cast<const char*>(bytes.data() + offset), cursor - offset);
}

static json readParamSfo(const fs::path& path) {
    const auto bytes = readBinaryFile(path, 4u << 20);
    if (bytes.size() < 20 || bytes[0] != 0 || bytes[1] != 'P' || bytes[2] != 'S' || bytes[3] != 'F') {
        throw std::runtime_error("Invalid PARAM.SFO signature");
    }
    const uint32_t keyTable = readLe32(bytes, 8);
    const uint32_t dataTable = readLe32(bytes, 12);
    const uint32_t count = readLe32(bytes, 16);
    if (keyTable >= bytes.size() || dataTable >= bytes.size() || count > 4096) {
        throw std::runtime_error("Invalid PARAM.SFO table bounds");
    }
    json result = json::object();
    for (uint32_t index = 0; index < count; ++index) {
        const size_t row = 20ull + static_cast<size_t>(index) * 16ull;
        if (row + 16 > bytes.size()) throw std::runtime_error("PARAM.SFO index is truncated");
        const uint16_t keyOffset = readLe16(bytes, row);
        const uint16_t format = readLe16(bytes, row + 2);
        const uint32_t length = readLe32(bytes, row + 4);
        const uint32_t dataOffset = readLe32(bytes, row + 12);
        const auto key = boundedCString(bytes, static_cast<size_t>(keyTable) + keyOffset, 256);
        const size_t absoluteData = static_cast<size_t>(dataTable) + dataOffset;
        if (absoluteData > bytes.size() || length > bytes.size() - absoluteData) {
            throw std::runtime_error("PARAM.SFO data is outside the file");
        }
        if (key != "TITLE" && key != "TITLE_ID" && key != "CATEGORY") continue;
        if (format == 1028 && length >= 4) result[key] = readLe32(bytes, absoluteData);
        else result[key] = trim(boundedCString(bytes, absoluteData, length));
    }
    return result;
}

static std::vector<fs::path> paramSfoCandidates(const fs::path& target) {
    std::vector<fs::path> candidates;
    auto add = [&](const fs::path& value) {
        const auto key = asciiLower(toUtf8(value.lexically_normal().wstring()));
        for (const auto& existing : candidates) {
            if (asciiLower(toUtf8(existing.lexically_normal().wstring())) == key) return;
        }
        candidates.push_back(value);
    };
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        add(target / L"PARAM.SFO");
        add(target / L"PS3_GAME" / L"PARAM.SFO");
    } else {
        auto current = target.parent_path();
        for (int depth = 0; !current.empty() && depth < 5; ++depth) {
            add(current / L"PARAM.SFO");
            add(current / L"PS3_GAME" / L"PARAM.SFO");
            current = current.parent_path();
        }
    }
    return candidates;
}

static std::string fallbackRomTitle(const fs::path& target, const std::string& emulatorId) {
    auto titleFrom = [](const fs::path& value) {
        const auto extension = asciiLower(toUtf8(value.extension().wstring()));
        return cleanRomTitle(toUtf8((extension.empty() ? value.filename() : value.stem()).wstring()));
    };
    auto isGeneric = [](const std::string& value) {
        const auto key = normalizeTitle(value);
        return key.empty() || key == "eboot" || key == "isobinedat" || key == "iso" ||
               key == "usrdir" || key == "ps3game" || key == "devhdd0" || key == "game" ||
               key == "ps3" || key == "rom" || key == "roms" ||
               !ps3SerialFromText(value).empty();
    };
    auto title = titleFrom(target);
    if (emulatorId != "rpcs3" || !isGeneric(title)) return title;
    auto current = target.parent_path();
    for (int depth = 0; !current.empty() && depth < 7; ++depth) {
        title = cleanRomTitle(toUtf8(current.filename().wstring()));
        if (!isGeneric(title) && !noisyDirectory(title)) return title;
        current = current.parent_path();
    }
    return {};
}

static json analyzeShortcutTarget(const SteamShortcut& shortcut) {
    const auto executable = expandEnvironmentPath(unquote(shortcut.exe)).lexically_normal();
    const auto* profile = findEmulatorProfile(executable);
    json result = {
        {"kind", profile ? "emulator-launcher" : "native-game"},
        {"executable", toUtf8(executable.wstring())},
        {"launchOptions", shortcut.launchOptions},
        {"arguments", json::array()},
        {"supported", true},
        {"status", "ready"}
    };
    if (!profile) {
        result["targetPath"] = toUtf8(executable.wstring());
        result["targetExists"] = fs::is_regular_file(executable);
        result["platformHint"] = "pc";
        result["displayNameEvidence"] = shortcut.appName;
        return result;
    }

    result["emulator"] = {
        {"id", profile->id}, {"name", profile->name},
        {"playnitePlatform", profile->playnitePlatform},
        {"platformLabel", profile->platformLabel},
        {"igdbPlatformId", profile->igdbPlatformId},
        {"steamArtworkAppId", profile->steamArtworkAppId > 0 ? json(profile->steamArtworkAppId) : json(nullptr)},
        {"playniteStartupArguments", profile->playniteStartupArguments},
        {"scriptGameImport", profile->scriptGameImport}
    };
    result["platformHint"] = profile->platformHint;

    const auto arguments = windowsCommandLineArguments(shortcut.launchOptions);
    for (const auto& argument : arguments) result["arguments"].push_back(argument);
    fs::path startDirectory = expandEnvironmentPath(unquote(shortcut.startDir));
    if (startDirectory.empty()) startDirectory = executable.parent_path();
    struct TargetCandidate { fs::path path; int score = 0; size_t index = 0; };
    std::optional<TargetCandidate> best;
    bool afterSeparator = false;
    for (size_t index = 0; index < arguments.size(); ++index) {
        std::string value = trim(arguments[index]);
        if (value == "--") { afterSeparator = true; continue; }
        const auto equal = value.find('=');
        if (value.starts_with("-") && equal != std::string::npos) value = value.substr(equal + 1);
        else if (value.starts_with("-") && !afterSeparator) continue;
        if (value.empty()) continue;
        auto path = expandEnvironmentPath(value);
        if (path.is_relative()) path = startDirectory / path;
        path = path.lexically_normal();
        const auto extension = asciiLower(toUtf8(path.extension().wstring()));
        const auto fileName = asciiLower(toUtf8(path.filename().wstring()));
        const bool extensionMatch = std::find(profile->imageExtensions.begin(), profile->imageExtensions.end(),
            extension.starts_with('.') ? extension.substr(1) : extension) != profile->imageExtensions.end();
        const bool rpcs3Special = profile->id == "rpcs3" &&
            (fileName == "eboot.bin" || fileName == "iso.bin.edat");
        std::error_code ec;
        const bool existingFile = fs::is_regular_file(path, ec);
        ec.clear();
        const bool existingDirectory = fs::is_directory(path, ec);
        const bool directoryShape = profile->directoryTarget && extension.empty() &&
            (value.find('\\') != std::string::npos || value.find('/') != std::string::npos);
        if (!extensionMatch && !rpcs3Special && !existingDirectory && !directoryShape) continue;
        int score = (extensionMatch || rpcs3Special ? 40 : 0) +
            (existingFile || existingDirectory ? 30 : 0) + (afterSeparator ? 5 : 0);
        if (!best || score > best->score) best = TargetCandidate{path, score, index};
    }

    if (!best) {
        result["kind"] = "emulator-launcher";
        result["targetPath"] = toUtf8(executable.wstring());
        result["targetExists"] = fs::is_regular_file(executable);
        result["status"] = result["targetExists"].get<bool>() ? "ready" : "executable-missing";
        result["displayNameEvidence"] = shortcut.appName.empty() ? profile->name : shortcut.appName;
        result["launchMode"] = "emulator-only-no-rom";
        return result;
    }

    result["kind"] = "emulator-game";
    const auto target = best->path;
    std::error_code ec;
    const bool targetExists = fs::is_regular_file(target, ec) || fs::is_directory(target, ec);
    result["targetPath"] = toUtf8(target.wstring());
    result["targetArgumentIndex"] = best->index;
    result["targetExists"] = targetExists;
    result["status"] = targetExists ? "ready" : "target-missing";
    result["romTitle"] = fallbackRomTitle(target, profile->id);
    result["displayNameEvidence"] = shortcut.appName;

    if (profile->id == "rpcs3") {
        std::string serial = ps3SerialFromText(toUtf8(target.wstring()));
        for (const auto& sfo : paramSfoCandidates(target)) {
            if (!fs::is_regular_file(sfo)) continue;
            try {
                const auto metadata = readParamSfo(sfo);
                result["paramSfo"] = toUtf8(sfo.wstring());
                result["paramSfoMetadata"] = metadata;
                const auto title = metadata.value("TITLE", std::string{});
                const auto titleId = metadata.value("TITLE_ID", std::string{});
                const auto category = metadata.value("CATEGORY", std::string{});
                if (!title.empty()) result["romTitle"] = title;
                if (!titleId.empty()) serial = titleId;
                if (category == "GD" || category == "2D" || category == "PP") {
                    result["status"] = "ignored-rpcs3-category";
                    result["error"] = "Playnite RPCS3 import ignores this PARAM.SFO category";
                }
                break;
            } catch (const std::exception& error) {
                result["paramSfoError"] = error.what();
            }
        }
        result["serial"] = serial.empty() ? json(nullptr) : json(serial);
    }
    return result;
}

static std::vector<SteamShortcut> readShortcuts(const fs::path& shortcutsFile, const std::string& accountId) {
    const auto root = BinaryVdfReader(readBinaryFile(shortcutsFile, 16u << 20)).parse();
    const json* shortcuts = nullptr;
    for (const auto& [name, value] : root.items()) {
        if (asciiLower(name) == "shortcuts" && value.is_object()) {
            shortcuts = &value;
            break;
        }
    }
    if (!shortcuts) throw std::runtime_error("Binary VDF has no shortcuts object");
    std::vector<SteamShortcut> result;
    for (const auto& [_, value] : shortcuts->items()) {
        if (!value.is_object()) continue;
        const auto appId = jsonIntCaseInsensitive(value, "appid");
        const auto appName = jsonStringCaseInsensitive(value, "appname");
        const auto exe = jsonStringCaseInsensitive(value, "exe");
        if (!appId || appName.empty() || exe.empty()) continue;
        result.push_back({
            accountId,
            shortcutsFile,
            *appId,
            appName,
            exe,
            jsonStringCaseInsensitive(value, "StartDir"),
            jsonStringCaseInsensitive(value, "LaunchOptions")
        });
    }
    return result;
}

static std::optional<fs::path> steamInstallPath() {
    auto registryPath = [](HKEY root, const wchar_t* key, const wchar_t* valueName) -> std::optional<fs::path> {
        DWORD bytes = 0;
        if (RegGetValueW(root, key, valueName, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
            return std::nullopt;
        }
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        if (RegGetValueW(root, key, valueName, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        while (!value.empty() && value.back() == L'\0') value.pop_back();
        std::replace(value.begin(), value.end(), L'/', L'\\');
        return value.empty() ? std::nullopt : std::optional<fs::path>(fs::path(value));
    };
    auto environmentPath = [](const wchar_t* name, const fs::path& fallback) {
        const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
        if (needed <= 1) return fallback;
        std::wstring value(needed, L'\0');
        const DWORD length = GetEnvironmentVariableW(name, value.data(), needed);
        if (length == 0 || length >= needed) return fallback;
        value.resize(length);
        return fs::path(value);
    };
    std::vector<fs::path> candidates;
    for (const auto& item : {
        registryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
        registryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"InstallPath"),
        registryPath(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Valve\\Steam", L"InstallPath"),
        registryPath(HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", L"InstallPath")
    }) {
        if (item) candidates.push_back(*item);
    }
    candidates.push_back(environmentPath(L"ProgramFiles(x86)", fs::path(L"C:\\Program Files (x86)")) / L"Steam");
    candidates.push_back(environmentPath(L"ProgramFiles", fs::path(L"C:\\Program Files")) / L"Steam");
    candidates.push_back(fs::path(L"C:\\Steam"));
    std::set<std::string> seen;
    for (const auto& candidate : candidates) {
        const auto absolute = fs::absolute(candidate);
        if (!seen.insert(canonicalPathKey(absolute)).second) continue;
        std::error_code ec;
        if (fs::is_regular_file(absolute / L"steam.exe", ec)) return absolute;
    }
    return std::nullopt;
}

static fs::path normalizeExplicitSteamShortcutsPath(const fs::path& candidate) {
    if (fs::is_regular_file(candidate)) return candidate;
    if (_wcsicmp(candidate.filename().c_str(), L"shortcuts.vd") == 0) {
        auto corrected = candidate;
        corrected.replace_filename(L"shortcuts.vdf");
        if (fs::is_regular_file(corrected)) return corrected;
    }
    return candidate;
}

static std::vector<std::pair<fs::path, std::string>> shortcutSources(
    const std::optional<fs::path>& explicitFile,
    const std::vector<fs::path>& requestedSteamRoots = {}) {
    std::vector<std::pair<fs::path, std::string>> sources;
    if (explicitFile) {
        const auto resolvedFile = normalizeExplicitSteamShortcutsPath(*explicitFile);
        if (!fs::is_regular_file(resolvedFile)) {
            throw std::runtime_error("Steam shortcuts.vdf does not exist: " + toUtf8(resolvedFile.wstring()));
        }
        const auto accountDirectory = resolvedFile.parent_path().parent_path();
        sources.push_back({resolvedFile, toUtf8(accountDirectory.filename().wstring())});
        return sources;
    }

    std::vector<fs::path> steamRoots;
    if (requestedSteamRoots.empty()) {
        const auto steam = steamInstallPath();
        if (!steam) throw std::runtime_error("SteamPath is unavailable");
        steamRoots.push_back(*steam);
    } else {
        steamRoots = requestedSteamRoots;
    }
    std::set<std::string> seenFiles;
    for (auto root : steamRoots) {
        root = fs::absolute(root);
        const auto userdata = _wcsicmp(root.filename().c_str(), L"userdata") == 0
            ? root : root / L"userdata";
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(userdata, ec)) {
            if (ec) break;
            if (!entry.is_directory()) continue;
            const auto file = entry.path() / L"config" / L"shortcuts.vdf";
            if (fs::is_regular_file(file) && seenFiles.insert(canonicalPathKey(file)).second) {
                sources.push_back({fs::absolute(file), toUtf8(entry.path().filename().wstring())});
            }
        }
    }
    std::sort(sources.begin(), sources.end(), [](const auto& left, const auto& right) {
        if (left.second != right.second) return left.second < right.second;
        return canonicalPathKey(left.first) < canonicalPathKey(right.first);
    });
    return sources;
}

// Steam does not create shortcuts.vdf until the first non-Steam shortcut is
// added. Keep signed-in userdata accounts visible so a first add can create
// the file instead of treating an otherwise valid Steam installation as
// having no selectable account.
static std::vector<std::pair<fs::path, std::string>> steamAccountSources(
    const std::vector<fs::path>& requestedSteamRoots = {}) {
    std::vector<fs::path> steamRoots;
    if (requestedSteamRoots.empty()) {
        const auto steam = steamInstallPath();
        if (!steam) throw std::runtime_error("SteamPath is unavailable");
        steamRoots.push_back(*steam);
    } else {
        steamRoots = requestedSteamRoots;
    }
    std::vector<std::pair<fs::path, std::string>> sources;
    std::set<std::string> seenAccountSources;
    for (auto root : steamRoots) {
        root = fs::absolute(root);
        const auto userdata = _wcsicmp(root.filename().c_str(), L"userdata") == 0
            ? root : root / L"userdata";
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(userdata, ec)) {
            if (ec) break;
            if (!entry.is_directory()) continue;
            const auto accountId = toUtf8(entry.path().filename().wstring());
            if (accountId.empty() || !std::all_of(accountId.begin(), accountId.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) continue;
            const auto shortcutPath = fs::absolute(entry.path() / L"config" / L"shortcuts.vdf");
            const auto sourceKey = canonicalPathKey(shortcutPath);
            if (!seenAccountSources.insert(sourceKey).second) continue;
            sources.push_back({shortcutPath, accountId});
        }
    }
    std::sort(sources.begin(), sources.end(), [](const auto& left, const auto& right) {
        if (left.second != right.second) return left.second < right.second;
        return canonicalPathKey(left.first) < canonicalPathKey(right.first);
    });
    return sources;
}

static std::vector<SteamShortcut> readAllShortcuts(const std::optional<fs::path>& explicitFile) {
    std::vector<SteamShortcut> shortcuts;
    for (const auto& [file, account] : shortcutSources(explicitFile)) {
        auto items = readShortcuts(file, account);
        shortcuts.insert(shortcuts.end(), items.begin(), items.end());
    }
    return shortcuts;
}

static SteamShortcut findMatchingShortcut(const fs::path& exePath, const std::optional<fs::path>& explicitFile) {
    const auto wanted = canonicalPathKey(exePath);
    for (const auto& shortcut : readAllShortcuts(explicitFile)) {
        if (canonicalPathKey(fs::path(toWide(unquote(shortcut.exe)))) == wanted) return shortcut;
    }
    throw std::runtime_error("No Steam non-Steam shortcut matches this EXE");
}

static uint32_t crc32(const std::string& value) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : value) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

struct SteamShortcutIds {
    uint32_t shortId = 0;
    uint64_t longId = 0;
    uint32_t legacyCrcShortId = 0;
    int32_t legacyCrcSignedId = 0;
    bool legacyCrcMatches = false;
};

static SteamShortcutIds shortcutIds(const SteamShortcut& shortcut) {
    SteamShortcutIds ids;
    std::memcpy(&ids.shortId, &shortcut.storedAppId, sizeof(ids.shortId));
    ids.longId = (static_cast<uint64_t>(ids.shortId) << 32) | 0x02000000ull;
    ids.legacyCrcShortId = crc32(shortcut.exe + shortcut.appName) | 0x80000000u;
    std::memcpy(&ids.legacyCrcSignedId, &ids.legacyCrcShortId, sizeof(ids.legacyCrcShortId));
    ids.legacyCrcMatches = ids.legacyCrcSignedId == shortcut.storedAppId;
    return ids;
}

static bool processRunningByName(const wchar_t* wantedName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, wantedName) == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return running;
}

static bool realSteamShortcutsPath(const fs::path& path) {
    const auto steam = steamInstallPath();
    if (steam && pathWithin(path, *steam / L"userdata")) return true;
    const auto absolute = fs::absolute(path);
    const auto config = absolute.parent_path();
    const auto account = config.parent_path();
    const auto userdata = account.parent_path();
    const auto root = userdata.parent_path();
    return _wcsicmp(absolute.filename().c_str(), L"shortcuts.vdf") == 0 &&
        _wcsicmp(config.filename().c_str(), L"config") == 0 &&
        _wcsicmp(userdata.filename().c_str(), L"userdata") == 0 &&
        fs::is_regular_file(root / L"steam.exe");
}

static std::string quotedSteamPath(const fs::path& path) {
    return "\"" + toUtf8(fs::absolute(path).wstring()) + "\"";
}

static int nextSteamShortcutIndex(const BinaryVdfNode& shortcuts) {
    int maximum = -1;
    for (const auto& [key, _] : shortcuts.objectValue) {
        try {
            size_t used = 0;
            const int value = std::stoi(key, &used);
            if (used == key.size() && value >= 0) maximum = (std::max)(maximum, value);
        } catch (...) {}
    }
    if (maximum == (std::numeric_limits<int>::max)()) {
        throw std::runtime_error("Steam shortcuts.vdf has no free numeric index");
    }
    return maximum + 1;
}

static int32_t steamShortcutAppId(const std::string& quotedExecutable, const std::string& appName) {
    const uint32_t raw = crc32(quotedExecutable + appName) | 0x80000000u;
    int32_t value = 0;
    std::memcpy(&value, &raw, sizeof(raw));
    return value;
}

static BinaryVdfNode steamShortcutNode(
    int32_t appId,
    const std::string& appName,
    const std::string& executable,
    const std::string& startDirectory,
    int32_t lastPlayTime,
    std::vector<std::string> tags = {}) {
    if (tags.empty()) tags = {"YeManCC", "Non-Steam Game"};
    std::vector<std::pair<std::string, BinaryVdfNode>> tagNodes;
    tagNodes.reserve(tags.size());
    for (size_t i = 0; i < tags.size(); ++i) {
        const auto tag = trim(tags[i]);
        if (!tag.empty()) tagNodes.push_back({std::to_string(i), vdfString(tag)});
    }
    return vdfObject({
        {"appid", vdfInt32(appId)},
        {"AppName", vdfString(appName)},
        {"Exe", vdfString(executable)},
        {"StartDir", vdfString(startDirectory)},
        {"icon", vdfString("")},
        {"ShortcutPath", vdfString("")},
        {"LaunchOptions", vdfString("")},
        {"IsHidden", vdfInt32(0)},
        {"AllowDesktopConfig", vdfInt32(1)},
        {"AllowOverlay", vdfInt32(1)},
        {"OpenVR", vdfInt32(0)},
        {"Devkit", vdfInt32(0)},
        {"DevkitGameID", vdfString("")},
        {"DevkitOverrideAppID", vdfInt32(0)},
        {"LastPlayTime", vdfInt32(lastPlayTime)},
        {"FlatpakAppID", vdfString("")},
        {"tags", vdfObject(std::move(tagNodes))}
    });
}

static SteamShortcut findShortcut(
    const fs::path& executable,
    const std::optional<fs::path>& explicitFile,
    const std::optional<uint32_t>& requestedShortId) {
    if (!requestedShortId) return findMatchingShortcut(executable, explicitFile);
    std::vector<SteamShortcut> matches;
    for (const auto& shortcut : readAllShortcuts(explicitFile)) {
        if (shortcutIds(shortcut).shortId == *requestedShortId) matches.push_back(shortcut);
    }
    if (matches.empty()) throw std::runtime_error("No Steam shortcut matches --shortcut-id");
    if (matches.size() == 1) return matches.front();
    const auto wanted = canonicalPathKey(executable);
    for (const auto& shortcut : matches) {
        if (canonicalPathKey(expandEnvironmentPath(unquote(shortcut.exe))) == wanted) return shortcut;
    }
    throw std::runtime_error("--shortcut-id is ambiguous across Steam accounts");
}

static bool supportedSteamArtworkExtension(const fs::path& path) {
    const auto extension = asciiLower(toUtf8(path.extension().wstring()));
    // Matches Steam Rom Manager's grid scanner.
    return extension == ".png" || extension == ".ico" || extension == ".tga" ||
           extension == ".jpg" || extension == ".jpeg";
}

static std::vector<fs::path> existingArtworkForStem(const fs::path& grid, const std::string& stem) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(grid, ec)) return files;
    const auto wanted = asciiLower(stem);
    for (const auto& entry : fs::directory_iterator(grid, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() || !supportedSteamArtworkExtension(entry.path())) continue;
        if (asciiLower(toUtf8(entry.path().stem().wstring())) == wanted) files.push_back(entry.path());
    }
    return files;
}

static json artworkPresence(const fs::path& grid, const SteamShortcutIds& ids) {
    auto describe = [&](std::initializer_list<std::string> stems, bool required) {
        json files = json::array();
        for (const auto& stem : stems) {
            for (const auto& file : existingArtworkForStem(grid, stem)) {
                files.push_back(toUtf8(file.wstring()));
            }
        }
        return json{{"present", !files.empty()}, {"required", required}, {"files", files}};
    };
    return {
        {"tall", describe({std::to_string(ids.shortId) + "p"}, true)},
        {"long", describe({std::to_string(ids.shortId), std::to_string(ids.longId)}, false)},
        {"hero", describe({std::to_string(ids.shortId) + "_hero"}, true)},
        {"logo", describe({std::to_string(ids.shortId) + "_logo"}, false)},
        {"icon", describe({std::to_string(ids.shortId) + "_icon"}, false)}
    };
}

static json auditSteamShortcuts(
    const fs::path& outputRoot,
    const std::optional<fs::path>& explicitShortcuts) {
    json items = json::array();
    size_t complete = 0;
    size_t needsArtwork = 0;
    size_t missingExecutable = 0;
    size_t emulatorLaunchers = 0;
    size_t emulatorGames = 0;
    size_t emulatorTargetsMissing = 0;
    for (const auto& shortcut : readAllShortcuts(explicitShortcuts)) {
        const auto ids = shortcutIds(shortcut);
        const auto grid = shortcut.shortcutsFile.parent_path() / L"grid";
        const auto presence = artworkPresence(grid, ids);
        const bool tall = presence["tall"].value("present", false);
        const bool hero = presence["hero"].value("present", false);
        const bool minimumComplete = tall && hero;
        const auto exePath = fs::path(toWide(unquote(shortcut.exe)));
        const bool exeExists = fs::is_regular_file(exePath);
        const auto target = analyzeShortcutTarget(shortcut);
        const bool emulatorLauncher = target.value("kind", std::string{}) == "emulator-launcher";
        const bool emulatorGame = target.value("kind", std::string{}) == "emulator-game";
        const bool targetExists = target.value("targetExists", false);
        const bool targetReady = target.value("status", std::string{}) == "ready";
        json missingRequired = json::array();
        json missingOptional = json::array();
        if (!tall) missingRequired.push_back("tall");
        if (!hero) missingRequired.push_back("hero");
        for (const auto& type : {"long", "logo", "icon"}) {
            if (!presence[type].value("present", false)) missingOptional.push_back(type);
        }
        if (minimumComplete) ++complete;
        else ++needsArtwork;
        if (!exeExists) ++missingExecutable;
        if (emulatorLauncher) ++emulatorLaunchers;
        if (emulatorGame) ++emulatorGames;
        if (emulatorGame && !targetExists) ++emulatorTargetsMissing;
        // Compromise mode: automatic filling handles native PC games and one
        // shortcut for the emulator itself. Per-ROM shortcuts remain visible
        // but are excluded to avoid applying one game's art to another game
        // that shares the same emulator EXE.
        const bool eligible = !minimumComplete && exeExists && !emulatorGame &&
            (!emulatorLauncher || targetReady);
        std::string recommendedAction = "none";
        if (!minimumComplete) {
            if (!exeExists) recommendedAction = "manual-review-missing-executable";
            else if (emulatorGame) recommendedAction = "use-emulator-launcher-shortcut";
            else if (emulatorLauncher) recommendedAction = "generate-emulator-launcher-artwork";
            else recommendedAction = "identify-and-fill-missing";
        }
        items.push_back({
            {"accountId", shortcut.accountId},
            {"shortcutsVdf", toUtf8(shortcut.shortcutsFile.wstring())},
            {"appName", shortcut.appName},
            {"exe", toUtf8(exePath.wstring())},
            {"exeExists", exeExists},
            {"startDir", shortcut.startDir},
            {"launchOptions", shortcut.launchOptions},
            {"storedAppId", shortcut.storedAppId},
            {"shortAppId", std::to_string(ids.shortId)},
            {"longAppId", std::to_string(ids.longId)},
            {"gridDirectory", toUtf8(grid.wstring())},
            {"artwork", presence},
            {"minimumComplete", minimumComplete},
            {"missingRequired", missingRequired},
            {"missingOptional", missingOptional},
            {"identificationTarget", target},
            {"platformHint", target.value("platformHint", std::string{"pc"})},
            {"eligibleForAutomaticFill", eligible},
            {"recommendedAction", recommendedAction}
        });
    }
    json report = {
        {"schemaVersion", 1},
        {"isolated", true},
        {"readOnly", true},
        {"shortcutsVdfModified", false},
        {"minimumCompleteRule", {{"required", {"tall", "hero"}}}},
        {"supportedExtensions", {"png", "ico", "tga", "jpg", "jpeg"}},
        {"summary", {
            {"total", items.size()},
            {"complete", complete},
            {"needsArtwork", needsArtwork},
            {"missingExecutable", missingExecutable},
            {"emulatorLaunchers", emulatorLaunchers},
            {"emulatorGames", emulatorGames},
            {"emulatorTargetsMissing", emulatorTargetsMissing}
        }},
        {"items", items}
    };
    fs::create_directories(outputRoot);
    writeJsonAtomic(outputRoot / L"shortcut-audit.json", report);
    return report;
}

static json applySteamArtwork(
    const fs::path& exePath,
    const fs::path& gameOutput,
    const fs::path& dataRoot,
    const json& manifest,
    const std::optional<fs::path>& explicitShortcuts,
    const std::optional<uint32_t>& requestedShortId,
    bool commitRealSteam,
    bool onlyMissing) {
    const auto shortcut = findShortcut(exePath, explicitShortcuts, requestedShortId);
    const auto ids = shortcutIds(shortcut);

    const auto realGrid = shortcut.shortcutsFile.parent_path() / L"grid";
    const auto simulatedGrid = dataRoot / L"cache" / L"simulated-steam" / L"userdata" / toWide(shortcut.accountId) / L"config" / L"grid";
    const auto targetGrid = commitRealSteam ? realGrid : simulatedGrid;
    const auto backupGrid = dataRoot / L"backups" / L"real-steam-grid" / std::to_wstring(unixTimeMs()) /
        L"userdata" / toWide(shortcut.accountId) / L"config" / L"grid";
    fs::create_directories(targetGrid);

    json mappings = json::array();
    auto writeMapping = [&](const std::string& type, const fs::path& source, const std::string& filename,
                            const std::vector<std::string>& equivalentStems) {
        const auto target = targetGrid / toWide(filename);
        const auto targetStem = toUtf8(fs::path(toWide(filename)).stem().wstring());
        auto existing = existingArtworkForStem(targetGrid, targetStem);
        std::set<std::string> existingKeys;
        for (const auto& file : existing) existingKeys.insert(canonicalPathKey(file));
        for (const auto& stem : equivalentStems) {
            for (const auto& file : existingArtworkForStem(targetGrid, stem)) {
                if (existingKeys.insert(canonicalPathKey(file)).second) existing.push_back(file);
            }
        }
        if (onlyMissing && !existing.empty()) {
            json existingFiles = json::array();
            for (const auto& file : existing) existingFiles.push_back(toUtf8(file.wstring()));
            mappings.push_back({
                {"type", type},
                {"source", toUtf8(source.wstring())},
                {"filename", filename},
                {"target", toUtf8(target.wstring())},
                {"status", "skipped-existing"},
                {"existing", existingFiles},
                {"backup", nullptr},
                {"simulatedTarget", commitRealSteam ? json(nullptr) : json(toUtf8(target.wstring()))},
                {"realTarget", toUtf8((realGrid / toWide(filename)).wstring())}
            });
            return;
        }
        json backup = nullptr;
        if (commitRealSteam && fs::is_regular_file(target)) {
            const auto backupTarget = backupGrid / toWide(filename);
            writeAtomic(backupTarget, readBinaryFile(target, MAX_IMAGE_BYTES));
            backup = toUtf8(backupTarget.wstring());
        }
        writeAtomic(target, readBinaryFile(source, MAX_IMAGE_BYTES));
        mappings.push_back({
            {"type", type},
            {"source", toUtf8(source.wstring())},
            {"filename", filename},
            {"target", toUtf8(target.wstring())},
            {"status", "written"},
            {"backup", backup},
            {"simulatedTarget", commitRealSteam ? json(nullptr) : json(toUtf8(target.wstring()))},
            {"realTarget", toUtf8((realGrid / toWide(filename)).wstring())}
        });
    };
    for (const auto& artwork : manifest["artwork"]) {
        if (!artwork.value("ok", false)) continue;
        const auto source = fs::path(toWide(artwork.value("file", std::string{})));
        const auto extension = asciiLower(toUtf8(source.extension().wstring()));
        const auto suffix = artwork.value("steamSuffix", std::string{});
        const auto filename = std::to_string(ids.shortId) + suffix + extension;
        std::vector<std::string> equivalentStems;
        if (artwork.value("type", std::string{}) == "long") {
            equivalentStems = {std::to_string(ids.shortId), std::to_string(ids.longId)};
        }
        writeMapping(artwork.value("type", std::string{}), source, filename, equivalentStems);

        // Steam Rom Manager creates a long-AppID compatibility link for the
        // numeric landscape grid. The Lab uses a duplicate file because a
        // symlink would require extra Windows privileges.
        if (artwork.value("type", std::string{}) == "long") {
            const auto compatibilityName = std::to_string(ids.longId) + extension;
            writeMapping("long-big-picture-compat", source, compatibilityName, equivalentStems);
        }
    }

    json plan = {
        {"schemaVersion", 1},
        {"mode", commitRealSteam ? "real-steam-grid-write" : "simulation-only"},
        {"committedToRealSteam", commitRealSteam},
        {"onlyMissing", onlyMissing},
        {"shortcutsVdfModified", false},
        {"shortcutsVdf", toUtf8(shortcut.shortcutsFile.wstring())},
        {"accountId", shortcut.accountId},
        {"shortcut", {
            {"storedAppId", shortcut.storedAppId},
            {"appName", shortcut.appName},
            {"exe", shortcut.exe},
            {"startDir", shortcut.startDir},
            {"launchOptions", shortcut.launchOptions},
            {"idSource", "shortcuts.vdf-appid"},
            {"shortAppId", std::to_string(ids.shortId)},
            {"longAppId", std::to_string(ids.longId)},
            {"legacyCrcShortAppId", std::to_string(ids.legacyCrcShortId)},
            {"legacyCrcSignedAppId", ids.legacyCrcSignedId},
            {"legacyCrcMatchesStored", ids.legacyCrcMatches},
            {"diagnostic", ids.legacyCrcMatches
                ? "Stored AppID matches the legacy Steam Rom Manager CRC formula"
                : "Modern Steam-created shortcut AppID differs from the legacy CRC formula; stored VDF AppID is authoritative"}
        }},
        {"realGridDirectory", toUtf8(realGrid.wstring())},
        {"simulatedGridDirectory", commitRealSteam ? json(nullptr) : json(toUtf8(simulatedGrid.wstring()))},
        {"backupGridDirectory", commitRealSteam ? json(toUtf8(backupGrid.wstring())) : json(nullptr)},
        {"mappings", mappings}
    };
    writeJsonAtomic(gameOutput / (commitRealSteam ? L"real-apply-plan.json" : L"apply-plan.json"), plan);
    return plan;
}

struct ArtworkSpec {
    std::string type;
    std::string steamSuffix;
    int targetWidth;
    int targetHeight;
    std::vector<std::string> fixedFiles;
};

struct ArtworkCandidate {
    std::string url;
    std::string provider;
    std::string sourceRole;
    std::string referer;
    bool genericBackground = false;
    std::string assetKey;
    int expectedRank = 0;
    // 2 = explicitly matched installed content, 1 = canonical base game,
    // 0 = provider fallback.  Identity tier is intentionally evaluated before
    // visual quality so a usable edition-specific asset is not replaced by a
    // prettier but semantically wrong base-game asset.
    int identityPriority = 0;
    bool fitCanvas = false; // last-resort related landscape, never an original slot asset
};

struct DownloadedArtworkCandidate {
    ArtworkCandidate candidate;
    std::vector<unsigned char> bytes;
    ImageInfo image;
    ArtworkQualityAssessment quality;
};

static int automaticArtworkProviderPriority(const std::string& provider) {
    if (provider.starts_with("steam")) return 3;
    if (provider == "playnite-igdb" || provider == "igdb") return 2;
    if (provider == "baidu-image") return 1;
    return 0;
}

static bool betterArtworkCandidate(
    const DownloadedArtworkCandidate& left,
    const DownloadedArtworkCandidate& right) {
    if (left.candidate.identityPriority != right.candidate.identityPriority) {
        return left.candidate.identityPriority > right.candidate.identityPriority;
    }
    if (left.quality.band >= ArtworkQualityBand::Usable && right.quality.band >= ArtworkQualityBand::Usable &&
        automaticArtworkProviderPriority(left.candidate.provider) != automaticArtworkProviderPriority(right.candidate.provider)) {
        return automaticArtworkProviderPriority(left.candidate.provider) > automaticArtworkProviderPriority(right.candidate.provider);
    }
    if (left.quality.band != right.quality.band) {
        return static_cast<int>(left.quality.band) > static_cast<int>(right.quality.band);
    }
    if (std::abs(left.quality.score - right.quality.score) > 0.0001) {
        return left.quality.score > right.quality.score;
    }
    return left.candidate.expectedRank > right.candidate.expectedRank;
}

static fs::path artworkUrlCacheDirectory(
    const fs::path& dataRoot, const ArtworkCandidate& candidate, const ArtworkSpec& spec) {
    const auto material = spec.type + (candidate.fitCanvas ? "\nfit-canvas-v2\n" : "\n") + candidate.url;
    const auto key = sha256(std::vector<unsigned char>(material.begin(), material.end()));
    return dataRoot / L"cache" / L"artwork-url" / toWide(key);
}

static std::optional<DownloadedArtworkCandidate> loadArtworkUrlCache(
    const fs::path& dataRoot, const ArtworkCandidate& candidate, const ArtworkSpec& spec) {
    if (dataRoot.empty() || candidate.url.empty()) return std::nullopt;
    try {
        const auto directory = artworkUrlCacheDirectory(dataRoot, candidate, spec);
        const auto manifestPath = directory / L"manifest.json";
        if (!fs::is_regular_file(manifestPath)) return std::nullopt;
        const auto manifestBytes = readBinaryFile(manifestPath, 1u << 20);
        const auto manifest = json::parse(manifestBytes.begin(), manifestBytes.end());
        const auto cachedAt = manifest.value("cachedAt", uint64_t{0});
        const auto now = unixTimeMs();
        if (!manifest.is_object() || manifest.value("schemaVersion", 0) != 1 ||
            manifest.value("url", std::string{}) != candidate.url ||
            manifest.value("type", std::string{}) != spec.type || cachedAt == 0 ||
            now < cachedAt || now - cachedAt > ARTWORK_URL_CACHE_TTL_MS) return std::nullopt;
        const auto extension = manifest.value("extension", std::string{});
        if (extension.empty()) return std::nullopt;
        const auto file = directory / toWide("image." + extension);
        if (!fs::is_regular_file(file)) return std::nullopt;
        const auto bytes = readBinaryFile(file, MAX_IMAGE_BYTES);
        auto info = inspectImage(bytes);
        if (!info) return std::nullopt;
        if (spec.type == "logo") inspectLogoVisiblePixels(bytes, *info);
        auto quality = assessArtworkQuality(spec.type, *info, candidate.genericBackground);
        if (quality.band == ArtworkQualityBand::Rejected) return std::nullopt;
        if (candidate.fitCanvas) {
            quality.band = ArtworkQualityBand::UniqueFallback;
            quality.reason = "related-landscape-fitted-to-slot-not-original-artwork";
        }
        return DownloadedArtworkCandidate{candidate, bytes, *info, quality};
    } catch (...) {
        return std::nullopt;
    }
}

static void saveArtworkUrlCache(
    const fs::path& dataRoot, const ArtworkCandidate& candidate, const ArtworkSpec& spec,
    const std::vector<unsigned char>& bytes, const ImageInfo& info,
    const ArtworkQualityAssessment& quality) {
    if (dataRoot.empty() || candidate.url.empty() || quality.band == ArtworkQualityBand::Rejected) return;
    try {
        const auto directory = artworkUrlCacheDirectory(dataRoot, candidate, spec);
        writeAtomic(directory / toWide("image." + info.extension), bytes);
        writeJsonAtomic(directory / L"manifest.json", {
            {"schemaVersion", 1}, {"url", candidate.url}, {"type", spec.type},
            {"provider", candidate.provider}, {"sourceRole", candidate.sourceRole},
            {"cachedAt", unixTimeMs()}, {"extension", info.extension},
            {"width", info.width}, {"height", info.height},
            {"quality", artworkQualityJson(quality, info)}, {"sha256", sha256(bytes)}
        });
    } catch (...) {
        // A cache write is best-effort. The freshly downloaded image remains
        // valid even when the cache directory is unavailable.
    }
}

static std::vector<ArtworkSpec> artworkSpecs() {
    // Mirrors Steam Rom Manager's tall/long/hero/logo model and ID suffixes.
    // SteamGridDB icons are optional: they are generated when found, but never
    // make the required tall/hero scrape fail when unavailable.
    return {
        {"tall", "p", 600, 900, {"library_600x900_2x.jpg", "library_600x900.jpg"}},
        {"long", "", 1196, 559, {"header.jpg"}},
        {"hero", "_hero", 1920, 620, {"library_hero_2x.jpg", "library_hero.jpg"}},
        {"logo", "_logo", 960, 540, {"logo_2x.png", "logo.png"}},
        {"icon", "_icon", 512, 512, {}}
    };
}

static json steamGridDbIconSearch(const std::string& query, json* attemptLog = nullptr) {
    auto text = [](const json& object, const char* key, const std::string& fallback = std::string{}) {
        const auto it = object.find(key);
        return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
    };
    auto array = [](const json& object, const char* key) {
        const auto it = object.find(key);
        return it != object.end() && it->is_array() ? *it : json::array();
    };
    auto integer = [](const json& object, const char* key, int fallback = 0) {
        const auto it = object.find(key);
        return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
    };
    json result = {{"candidates", json::array()}, {"networkAttempts", json::array()}};
    if (query.empty()) return result;
    auto& attempts = result["networkAttempts"];
    const auto autocomplete = getJson(
        "https://www.steamgriddb.com/api/public/search/autocomplete?term=" + percentEncode(query),
        "steamgriddb", &attempts);
    if (!autocomplete.value("success", false) || !autocomplete.contains("data") || !autocomplete["data"].is_array()) return result;
    const json* selected = nullptr;
    double selectedScore = -1.0;
    const auto queryKey = normalizeTitle(query);
    for (const auto& game : autocomplete["data"]) {
        if (!game.is_object() || !game.contains("id") || !game["id"].is_number_integer()) continue;
        const auto name = text(game, "name");
        if (name.empty()) continue;
        const auto nameKey = normalizeTitle(name);
        double score = nameKey == queryKey ? 100.0 : 0.0;
        if (score == 0.0 && (!queryKey.empty() && (nameKey.find(queryKey) != std::string::npos || queryKey.find(nameKey) != std::string::npos))) score = 70.0;
        bool steamType = false;
        for (const auto& type : array(game, "types")) if (type.is_string() && type.get<std::string>() == "steam") steamType = true;
        if (steamType) score += 10.0;
        if (!selected || score > selectedScore) { selected = &game; selectedScore = score; }
    }
    // Do not download an arbitrary similarly named game's icon.
    if (!selected || selectedScore < 70.0) return result;
    const auto gameId = selected->value("id", 0);
    const json payload = {
        {"game_id", json::array({gameId})}, {"asset_type", "icon"},
        {"page", 0}, {"limit", 12}, {"styles", {"all"}},
        {"languages", {"all"}}, {"dimensions", {"all"}}, {"formats", {"all"}},
        {"order", "score_desc"}, {"static", true}, {"animated", false},
        {"nsfw", false}, {"epilepsy", false}, {"humor", true}, {"untagged", true}
    };
    const auto assets = postJson("https://www.steamgriddb.com/api/public/search/assets", payload,
        "steamgriddb", &attempts);
    if (!assets.value("success", false) || !assets.contains("data") || !assets["data"].is_object()) return result;
    for (const auto& asset : array(assets["data"], "assets")) {
        if (!asset.is_object()) continue;
        const auto url = text(asset, "url");
        const auto width = integer(asset, "width");
        const auto height = integer(asset, "height");
        if (url.rfind("https://cdn2.steamgriddb.com/icon/", 0) != 0 ||
            width < 256 || height < 256 || width > 4096 || height > 4096) continue;
        result["candidates"].push_back({
            {"url", url}, {"previewUrl", text(asset, "thumb", url)},
            {"width", width}, {"height", height}, {"title", text(*selected, "name")},
            {"provider", "steamgriddb"}, {"sourceRole", "steamgriddb-icon"},
            {"steamGridDbGameId", gameId}, {"assetId", asset.value("id", 0)},
            {"preferred", width >= 512 && height >= 512}
        });
        if (result["candidates"].size() >= 8) break;
    }
    if (attemptLog) *attemptLog = attempts;
    return result;
}

class GdiPlusRuntime {
public:
    GdiPlusRuntime() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token_, &input, nullptr) != Gdiplus::Ok) {
            throw std::runtime_error("GDI+ startup failed");
        }
    }
    ~GdiPlusRuntime() {
        if (token_) Gdiplus::GdiplusShutdown(token_);
    }
    GdiPlusRuntime(const GdiPlusRuntime&) = delete;
    GdiPlusRuntime& operator=(const GdiPlusRuntime&) = delete;
private:
    ULONG_PTR token_ = 0;
};

static const CLSID& pngEncoderClsid() {
    static CLSID encoder{};
    static bool initialized = false;
    if (!initialized) {
        UINT count = 0;
        UINT bytes = 0;
        if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || !bytes) {
            throw std::runtime_error("Cannot enumerate GDI+ image encoders");
        }
        std::vector<unsigned char> storage(bytes);
        auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
        if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) {
            throw std::runtime_error("Cannot read GDI+ image encoders");
        }
        bool found = false;
        for (UINT index = 0; index < count; ++index) {
            if (codecs[index].MimeType && _wcsicmp(codecs[index].MimeType, L"image/png") == 0) {
                encoder = codecs[index].Clsid;
                found = true;
                break;
            }
        }
        if (!found) throw std::runtime_error("PNG encoder is unavailable");
        initialized = true;
    }
    return encoder;
}

// Preserve the full related image without stretching or cropping. Fit to
// the requested slot only after no usable matching slot artwork was found.
static bool usableArtworkFittingSource(const ImageInfo& info) {
    return info.width >= 480 && info.height >= 300 &&
        static_cast<double>(info.width) / info.height >= 1.15 && static_cast<double>(info.width) / info.height <= 4.0;
}

static std::vector<unsigned char> fitLandscapeArtworkToSlot(const std::vector<unsigned char>& bytes, const ArtworkSpec& spec) {
    (void)artworkInspectionGdiPlusToken();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) throw std::runtime_error("Cannot allocate fitted fallback image");
    auto* pixels = GlobalLock(memory);
    if (!pixels) { GlobalFree(memory); throw std::runtime_error("Cannot lock fitted fallback image"); }
    std::memcpy(pixels, bytes.data(), bytes.size()); GlobalUnlock(memory);
    IStream* input = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &input) != S_OK || !input) {
        GlobalFree(memory); throw std::runtime_error("Cannot open fitted fallback image");
    }
    auto release = [](IStream* stream) { if (stream) stream->Release(); };
    std::unique_ptr<IStream, decltype(release)> ownedInput(input, release);
    Gdiplus::Bitmap source(input, FALSE);
    if (source.GetLastStatus() != Gdiplus::Ok || !source.GetWidth() || !source.GetHeight())
        throw std::runtime_error("Cannot decode fitted fallback image");
    const int targetWidth = spec.targetWidth, targetHeight = spec.targetHeight;
    Gdiplus::Bitmap canvas(targetWidth, targetHeight, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&canvas);
        graphics.Clear(Gdiplus::Color(255, 20, 24, 29));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        const double scale = (std::min)(static_cast<double>(targetWidth) / source.GetWidth(), static_cast<double>(targetHeight) / source.GetHeight());
        const float width = static_cast<float>(source.GetWidth() * scale);
        const float height = static_cast<float>(source.GetHeight() * scale);
        if (graphics.DrawImage(&source, Gdiplus::RectF((targetWidth-width)/2, (targetHeight-height)/2, width, height),
            0, 0, static_cast<INT>(source.GetWidth()), static_cast<INT>(source.GetHeight()), Gdiplus::UnitPixel) != Gdiplus::Ok)
            throw std::runtime_error("Cannot fit fitted fallback image");
    }
    IStream* output = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &output) != S_OK || !output)
        throw std::runtime_error("Cannot encode fitted fallback image");
    std::unique_ptr<IStream, decltype(release)> ownedOutput(output, release);
    if (canvas.Save(output, &pngEncoderClsid(), nullptr) != Gdiplus::Ok)
        throw std::runtime_error("Cannot encode fitted fallback PNG");
    STATSTG stat{};
    if (output->Stat(&stat, STATFLAG_NONAME) != S_OK || stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > MAX_IMAGE_BYTES)
        throw std::runtime_error("Invalid fitted fallback PNG size");
    std::vector<unsigned char> encoded(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER start{}; ULONG read = 0;
    if (output->Seek(start, STREAM_SEEK_SET, nullptr) != S_OK ||
        output->Read(encoded.data(), static_cast<ULONG>(encoded.size()), &read) != S_OK || read != encoded.size())
        throw std::runtime_error("Cannot read fitted fallback PNG");
    return encoded;
}

static Gdiplus::Color profileColor(uint32_t rgb, BYTE alpha = 255) {
    return Gdiplus::Color(alpha,
        static_cast<BYTE>((rgb >> 16) & 0xFF),
        static_cast<BYTE>((rgb >> 8) & 0xFF),
        static_cast<BYTE>(rgb & 0xFF));
}

static std::wstring emulatorInitials(const std::string& name) {
    const auto wide = toWide(name);
    std::wstring initials;
    bool wordStart = true;
    for (wchar_t ch : wide) {
        if (iswalnum(ch)) {
            if (wordStart && initials.size() < 3) initials.push_back(static_cast<wchar_t>(towupper(ch)));
            wordStart = false;
        } else {
            wordStart = true;
        }
    }
    if (initials.size() == 1) {
        for (size_t index = 1; index < wide.size(); ++index) {
            if (iswalnum(wide[index])) {
                initials.push_back(static_cast<wchar_t>(towupper(wide[index])));
                break;
            }
        }
    }
    return initials.empty() ? L"EMU" : initials;
}

static bool generateEmulatorArtworkPng(
    const fs::path& executable,
    const EmulatorProfile& profile,
    const ArtworkSpec& spec,
    const fs::path& target) {
    static GdiPlusRuntime runtime;
    (void)runtime;

    Gdiplus::Bitmap canvas(spec.targetWidth, spec.targetHeight, PixelFormat32bppARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok) throw std::runtime_error("Cannot allocate emulator artwork canvas");
    Gdiplus::Graphics graphics(&canvas);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    const Gdiplus::Rect bounds(0, 0, spec.targetWidth, spec.targetHeight);
    Gdiplus::LinearGradientBrush background(
        bounds, profileColor(profile.colorA), profileColor(profile.colorB),
        spec.type == "tall" ? 115.0f : 20.0f);
    graphics.FillRectangle(&background, bounds);

    Gdiplus::SolidBrush glow(profileColor(profile.colorA, 65));
    const int glowSize = (std::max)(spec.targetWidth, spec.targetHeight);
    graphics.FillEllipse(&glow,
        spec.targetWidth - glowSize / 2, -glowSize / 3, glowSize, glowSize);
    Gdiplus::SolidBrush shade(Gdiplus::Color(100, 0, 0, 0));
    graphics.FillRectangle(&shade, 0, spec.targetHeight * 2 / 3, spec.targetWidth, spec.targetHeight / 3 + 1);

    int iconSize = 0;
    int iconX = 0;
    int iconY = 0;
    Gdiplus::RectF titleRect;
    Gdiplus::RectF platformRect;
    float titleSize = 0;
    float platformSize = 0;
    if (spec.type == "tall") {
        iconSize = 250;
        iconX = (spec.targetWidth - iconSize) / 2;
        iconY = 120;
        titleRect = Gdiplus::RectF(55.0f, 505.0f, static_cast<float>(spec.targetWidth - 110), 205.0f);
        platformRect = Gdiplus::RectF(55.0f, 760.0f, static_cast<float>(spec.targetWidth - 110), 60.0f);
        titleSize = profile.name.size() > 10 ? 52.0f : 66.0f;
        platformSize = 25.0f;
    } else {
        iconSize = spec.type == "logo" ? 180 : 250;
        iconX = spec.type == "logo" ? 90 : 100;
        iconY = (spec.targetHeight - iconSize) / 2;
        const float left = static_cast<float>(iconX + iconSize + (spec.type == "logo" ? 75 : 110));
        titleRect = Gdiplus::RectF(left, spec.targetHeight * 0.25f,
            spec.targetWidth - left - 70.0f, spec.targetHeight * 0.34f);
        platformRect = Gdiplus::RectF(left, spec.targetHeight * 0.64f,
            spec.targetWidth - left - 70.0f, spec.targetHeight * 0.14f);
        titleSize = spec.type == "hero" ? 92.0f : 64.0f;
        if (profile.name.size() > 10) titleSize *= 0.82f;
        platformSize = spec.type == "hero" ? 34.0f : 27.0f;
    }

    Gdiplus::SolidBrush badge(Gdiplus::Color(72, 255, 255, 255));
    graphics.FillEllipse(&badge, iconX - 22, iconY - 22, iconSize + 44, iconSize + 44);

    HICON largeIcon = nullptr;
    HICON smallIcon = nullptr;
    ExtractIconExW(executable.c_str(), 0, &largeIcon, &smallIcon, 1);
    HICON selectedIcon = largeIcon ? largeIcon : smallIcon;
    bool usedIcon = false;
    if (selectedIcon) {
        Gdiplus::Bitmap icon(selectedIcon);
        if (icon.GetLastStatus() == Gdiplus::Ok && icon.GetWidth() && icon.GetHeight()) {
            const auto status = graphics.DrawImage(&icon,
                Gdiplus::Rect(iconX, iconY, iconSize, iconSize),
                0, 0, static_cast<INT>(icon.GetWidth()), static_cast<INT>(icon.GetHeight()), Gdiplus::UnitPixel);
            usedIcon = status == Gdiplus::Ok;
        }
    }
    if (largeIcon) DestroyIcon(largeIcon);
    if (smallIcon && smallIcon != largeIcon) DestroyIcon(smallIcon);

    Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
    if (!usedIcon) {
        const auto initials = emulatorInitials(profile.name);
        Gdiplus::Font initialsFont(L"Segoe UI", iconSize * 0.32f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::StringFormat centered;
        centered.SetAlignment(Gdiplus::StringAlignmentCenter);
        centered.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        const Gdiplus::RectF iconRect(static_cast<float>(iconX), static_cast<float>(iconY),
            static_cast<float>(iconSize), static_cast<float>(iconSize));
        graphics.DrawString(initials.c_str(), -1, &initialsFont, iconRect, &centered, &white);
    }

    const auto title = toWide(profile.name);
    const auto platform = toWide(profile.platformLabel);
    Gdiplus::Font titleFont(L"Segoe UI", titleSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font platformFont(L"Segoe UI", platformSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat titleFormat;
    titleFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
    titleFormat.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
    Gdiplus::SolidBrush secondary(Gdiplus::Color(205, 255, 255, 255));
    graphics.DrawString(title.c_str(), -1, &titleFont, titleRect, &titleFormat, &white);
    graphics.DrawString(platform.c_str(), -1, &platformFont, platformRect, &titleFormat, &secondary);

    fs::create_directories(target.parent_path());
    const auto temp = target.wstring() + L".tmp.png";
    DeleteFileW(temp.c_str());
    if (canvas.Save(temp.c_str(), &pngEncoderClsid(), nullptr) != Gdiplus::Ok) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Cannot encode generated emulator artwork as PNG");
    }
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Atomic emulator artwork activation failed");
    }
    return usedIcon;
}

static std::string igdbImageUrl(const std::string& rawUrl, const std::string& size) {
    if (rawUrl.empty()) return {};
    std::string url = rawUrl.rfind("//", 0) == 0 ? "https:" + rawUrl : rawUrl;
    const auto marker = url.find("/t_");
    if (marker == std::string::npos) return url;
    const auto end = url.find('/', marker + 1);
    if (end == std::string::npos) return url;
    return url.substr(0, marker) + "/t_" + size + url.substr(end);
}

static std::string steamArtworkAssetText(const json& object, const char* field) {
    if (!object.is_object()) return {};
    const auto it=object.find(field);
    return it!=object.end()&&it->is_string()?it->get<std::string>():std::string{};
}

static std::string steamLibraryAssetUrl(const json& assets, int appId, const char* field) {
    const auto filename=steamArtworkAssetText(assets,field);
    auto format=steamArtworkAssetText(assets,"asset_url_format");
    const auto prefix="steam/apps/"+std::to_string(appId)+"/";
    const std::string placeholder="${FILENAME}";
    if(appId<=0||filename.empty()||filename.size()>512||format.size()>2048||
       !format.starts_with(prefix+placeholder)||format.find(placeholder,prefix.size()+placeholder.size())!=std::string::npos)return {};
    const auto suffix=format.substr(prefix.size()+placeholder.size());
    if(!suffix.empty()&&suffix.front()!='?')return {};
    if(format.find_first_of("\\\r\n\"<>#")!=std::string::npos||filename.find_first_of("\\?#\r\n\"<>")!=std::string::npos||filename.front()=='/'||
       filename.find("..")!=std::string::npos||filename.find(':')!=std::string::npos)return {};
    const auto extension=asciiLower(toUtf8(fs::path(toWide(filename)).extension().wstring()));
    if(extension!=".jpg"&&extension!=".jpeg"&&extension!=".png")return {};
    format.replace(prefix.size(),placeholder.size(),filename);
    return "https://shared.steamstatic.com/store_item_assets/"+format;
}

static std::map<std::string,json> g_steamLibraryAssetsSession;
static json fetchSteamLibraryAssets(int appId, json& attempts) {
    if(appId<=0)return json::object();
    const auto sessionKey=canonicalPathKey(g_workerDataRoot)+":"+std::to_string(appId);
    if(const auto found=g_steamLibraryAssetsSession.find(sessionKey);found!=g_steamLibraryAssetsSession.end())return found->second;
    if(g_steamLibraryAssetsSession.size()>=128)g_steamLibraryAssetsSession.clear();
    const auto path=g_workerDataRoot/L"cache"/L"steam-library-assets"/(std::to_wstring(appId)+L"-english-us.json");
    json cached=json::object();uint64_t fetchedAt=0;
    try {
        if(!g_workerDataRoot.empty()&&fs::is_regular_file(path)) {
            const auto bytes=readBinaryFile(path,2u<<20);const auto record=json::parse(bytes.begin(),bytes.end());
            if(positiveJsonInt(record,"steamAppId")==appId&&record.contains("assets")&&record["assets"].is_object()&&
               !steamLibraryAssetUrl(record["assets"],appId,"library_capsule").empty()) {
                cached=record["assets"];
                if(record.contains("fetchedAt")&&record["fetchedAt"].is_number_unsigned())fetchedAt=record["fetchedAt"].get<uint64_t>();
            }
        }
    } catch(...) { /* Optional cache cannot break artwork discovery. */ }
    const auto now=unixTimeMs();
    if(!cached.empty()&&fetchedAt>0&&fetchedAt<=now&&now-fetchedAt<24ull*60*60*1000) {
        attempts.push_back({{"stage","steam-library-assets-cache"},{"appId",appId},{"cacheHit",true},{"ok",true}});
        g_steamLibraryAssetsSession[sessionKey]=cached;return cached;
    }
    json assets=json::object();
    try {
        const json request={{"ids",json::array({{{"appid",appId}}})},{"context",{{"country_code","US"},{"language","english"}}},
            {"data_request",{{"include_assets",true},{"include_assets_without_overrides",true}}}};
        const auto response=getJson("https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json="+percentEncode(request.dump()),"steam-library-assets",&attempts);
        const auto envelope=response.find("response");
        if(envelope==response.end()||!envelope->is_object())throw std::runtime_error("Steam library assets response is invalid");
        const auto items=envelope->find("store_items");
        if(items==envelope->end()||!items->is_array())throw std::runtime_error("Steam library assets inventory is invalid");
        for(const auto& item:*items) {
            if(!item.is_object()||positiveJsonInt(item,"appid")!=appId||positiveJsonInt(item,"success")!=1)continue;
            const auto value=item.find("assets");
            if(value==item.end()||!value->is_object())continue;
            assets=*value;break;
        }
        if(!assets.empty()&&!g_workerDataRoot.empty())try {
            writeJsonAtomic(path,{{"schemaVersion",1},{"steamAppId",appId},{"fetchedAt",now},{"source","IStoreBrowseService"},{"assets",assets}});
        }catch(...) { /* Discovery still succeeds when the cache is locked. */ }
    }catch(const std::exception& error) {
        attempts.push_back({{"stage","steam-library-assets"},{"appId",appId},{"ok",false},{"error",error.what()}});
        if(!cached.empty()&&fetchedAt>0&&fetchedAt<=now&&now-fetchedAt<30ull*24*60*60*1000)assets=cached;
    }
    // Do not repeatedly query a failing asset endpoint for each slot in one worker.
    g_steamLibraryAssetsSession[sessionKey]=assets;
    return assets;
}

static std::vector<ArtworkCandidate> artworkUrls(const ResolvedGame& game, const ArtworkSpec& spec, json* discoveryAttempts = nullptr) {
    std::vector<ArtworkCandidate> urls;
    std::set<std::string> seen;
    auto add = [&](const std::string& value, const std::string& provider,
                   const std::string& role, const std::string& referer, bool genericBackground = false,
                   const std::string& assetKey = {}, int expectedRank = 0, int identityPriority = 0) {
        if (!value.empty() && value.rfind("https://", 0) == 0 && seen.insert(value).second) {
            urls.push_back({value, provider, role, referer, genericBackground,
                assetKey.empty() ? value : assetKey, expectedRank, identityPriority});
        }
    };
    // When a verified Steam identity falls back to IGDB artwork, keep the
    // SteamID in the manifest but do not probe the known-empty Steam CDN a
    // second time. The artwork provider is then explicitly IGDB while the
    // identity authority remains Steam.
    if ((game.appId > 0 || game.matchedContentAppId > 0) &&
        game.source != "igdb-artwork-fallback") {
        std::vector<std::pair<int, int>> artworkAppIds;
        std::set<int> seenArtworkAppIds;
        auto addArtworkAppId = [&](int appId, int rankBase) {
            if (appId > 0 && seenArtworkAppIds.insert(appId).second) artworkAppIds.push_back({appId, rankBase});
        };
        // An explicitly matched install/content title should look like that
        // title in Big Picture.  Its artwork therefore precedes the canonical
        // base identity, while the base remains a complete fallback.
        addArtworkAppId(game.matchedContentAppId, 1200);
        addArtworkAppId(game.appId, 1000);
        addArtworkAppId(game.baseGameAppId, 950);

        // Steam has been migrating store assets from deterministic filenames
        // (header.jpg/library_hero.jpg) to hashed paths returned by AppDetails.
        // Those URLs are authoritative: a fixed filename can be a permanent
        // 404 even though the same AppID has perfectly valid artwork.
        auto addStoreField = [&](const json& details, const char* field,
                                 const std::string& providerRole, int rank,
                                 int identityPriority, bool genericBackground) {
            if (!details.is_object()) return false;
            const auto value = details.value(field, std::string{});
            if (value.empty()) return false;
            add(value, "steam-store", providerRole, "https://store.steampowered.com/",
                genericBackground, {}, rank, identityPriority);
            return true;
        };
        auto addStoreScreenshots = [&](const json& details, int rank, int identityPriority) {
            if (!details.is_object() || !details.contains("screenshots") ||
                !details["screenshots"].is_array()) return;
            size_t added = 0;
            for (const auto& screenshot : details["screenshots"]) {
                if (!screenshot.is_object() || added >= 8) continue;
                const auto url = screenshot.value("path_full", std::string{});
                if (url.empty()) continue;
                add(url, "steam-store", "store-screenshot", "https://store.steampowered.com/",
                    true, {}, rank - static_cast<int>(added) * 10, identityPriority);
                ++added;
            }
        };
        const int matchedContentPriority = game.matchedContentAppId > 0 ? 2 : 1;
        if (spec.type == "long") {
            addStoreField(game.details, "header_image", "header-image", 1450, matchedContentPriority, false);
            addStoreField(game.baseGameDetails, "header_image", "base-header-image", 1400, 1, false);
        }
        if (spec.type == "hero") {
            addStoreField(game.details, "background_raw", "background-raw", 1500, matchedContentPriority, true);
            addStoreField(game.details, "background", "background", 1480, matchedContentPriority, true);
            addStoreField(game.baseGameDetails, "background_raw", "base-background-raw", 1440, 1, true);
            addStoreField(game.baseGameDetails, "background", "base-background", 1420, 1, true);
            // Some new Steam store records expose screenshots but no usable
            // page background. Keep them as a fast, official wallpaper route.
            addStoreScreenshots(game.details, 1360, matchedContentPriority);
            addStoreScreenshots(game.baseGameDetails, 1320, 1);
        }
        for (const auto& [artworkAppId, rankBase] : artworkAppIds) {
            const int identityPriority = game.matchedContentAppId > 0 && artworkAppId == game.matchedContentAppId
                ? 2 : (game.baseGameAppId > 0 && artworkAppId == game.baseGameAppId ? 1 : 0);
            json assetAttempts=json::array();
            const auto assets=fetchSteamLibraryAssets(artworkAppId,assetAttempts);
            if(discoveryAttempts)for(const auto& attempt:assetAttempts)discoveryAttempts->push_back(attempt);
            const auto fields=spec.type=="tall"?std::vector<const char*>{"library_capsule_2x","library_capsule"}:
                spec.type=="hero"?std::vector<const char*>{"library_hero_2x","library_hero"}:
                spec.type=="long"?std::vector<const char*>{"header_2x","header"}:std::vector<const char*>{"library_logo_2x","library_logo"};
            bool discoveredSlot=false;
            for(const auto* field:fields) {
                const auto url=steamLibraryAssetUrl(assets,artworkAppId,field);
                if(url.empty())continue;
                add(url,"steam-store","library-assets-"+std::string(field),"https://store.steampowered.com/",false,
                    "steam:"+std::to_string(artworkAppId)+":assets:"+field,rankBase+800,identityPriority);
                discoveredSlot=true;
            }
            // The official library capsule uses its own hash and filename.
            // Never replace a discovered portrait with guessed fixed 404s.
            if(discoveredSlot&&spec.type=="tall")continue;
            for (size_t fileIndex = 0; fileIndex < spec.fixedFiles.size(); ++fileIndex) {
                const auto& file = spec.fixedFiles[fileIndex];
                const auto assetKey = "steam:" + std::to_string(artworkAppId) + ":" + file;
                const int expectedRank = rankBase - static_cast<int>(fileIndex) * 100;
                add("https://shared.steamstatic.com/store_item_assets/steam/apps/" + std::to_string(artworkAppId) + "/" + file,
                    "steam-cdn", spec.type, "https://store.steampowered.com/", false, assetKey, expectedRank, identityPriority);
                add("https://shared.cloudflare.steamstatic.com/store_item_assets/steam/apps/" + std::to_string(artworkAppId) + "/" + file,
                    "steam-cdn", spec.type, "https://store.steampowered.com/", false, assetKey, expectedRank, identityPriority);
                add("https://cdn.cloudflare.steamstatic.com/steam/apps/" + std::to_string(artworkAppId) + "/" + file,
                    "steam-cdn", spec.type, "https://store.steampowered.com/", false, assetKey, expectedRank, identityPriority);
            }
        }
    }

    if (game.igdbDetails.is_object()) {
        if (spec.type == "tall" && game.igdbDetails.contains("cover_expanded") &&
            game.igdbDetails["cover_expanded"].is_object()) {
            const auto& cover = game.igdbDetails["cover_expanded"];
            const auto height = cover.value("height", 0);
            const auto rawUrl = cover.value("url", std::string{});
            const auto preferredSize = height > 1080 ? "1080p" : "original";
            const auto alternateSize = height > 1080 ? "original" : "1080p";
            add(igdbImageUrl(rawUrl, preferredSize),
                "playnite-igdb", "cover", "https://www.igdb.com/", false, {}, 850);
            // IGDB image transforms are separate CDN objects. Trying the alternate
            // transform gives a weak connection another fresh route/cache object
            // after the preferred URL has exhausted its own reconnect budget.
            add(igdbImageUrl(rawUrl, alternateSize),
                "playnite-igdb", "cover-alternate-transform", "https://www.igdb.com/", false, {}, 800);
        }
        if (spec.type == "hero") {
            auto addBackgrounds = [&](const std::string& field, const std::string& role) {
                size_t added = 0;
                if (!game.igdbDetails.contains(field) || !game.igdbDetails[field].is_array()) return added;
                size_t count = 0;
                for (const auto& image : game.igdbDetails[field]) {
                    if (!image.is_object() || count >= 8) continue;
                    const auto height = image.value("height", 0);
                    const auto before = urls.size();
                    add(igdbImageUrl(image.value("url", std::string{}), height > 1080 ? "1080p" : "original"),
                        "playnite-igdb", role, "https://www.igdb.com/", true, {},
                        role == "background-artwork" ? 620 : 580);
                    if (urls.size() > before) ++added;
                    ++count;
                }
                return added;
            };
            // Playnite artwork remains preferred, but an artwork can be
            // present and still be unusable for the wide hero slot (for
            // example a 4:3 image). Always enqueue screenshots at a lower
            // rank so a valid 16:9 screenshot can complete the wallpaper
            // fallback instead of being skipped by the presence of a bad
            // artwork candidate.
            addBackgrounds("artworks_expanded", "background-artwork");
            addBackgrounds("screenshots_expanded", "background-screenshot");
        }
        if (spec.type == "long") {
            // The horizontal library capsule has no dedicated IGDB field.
            // Wide artwork and screenshots are its best source, and the
            // normal quality filter rejects anything that is not landscape.
            auto addLongBackgrounds = [&](const std::string& field, const std::string& role, int rankBase) {
                if (!game.igdbDetails.contains(field) || !game.igdbDetails[field].is_array()) return;
                size_t count = 0;
                for (const auto& image : game.igdbDetails[field]) {
                    if (!image.is_object() || count >= 8) continue;
                    const auto height = image.value("height", 0);
                    add(igdbImageUrl(image.value("url", std::string{}), height > 1080 ? "1080p" : "original"),
                        "playnite-igdb", role, "https://www.igdb.com/", true, {},
                        rankBase - static_cast<int>(count) * 10);
                    ++count;
                }
            };
            addLongBackgrounds("artworks_expanded", "long-artwork", 720);
            addLongBackgrounds("screenshots_expanded", "long-screenshot", 680);
        }
    }
    std::stable_sort(urls.begin(), urls.end(), [](const ArtworkCandidate& left, const ArtworkCandidate& right) {
        return left.expectedRank > right.expectedRank;
    });
    return urls;
}

static void replaceAll(std::string& value, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

static std::string truncateUtf8Characters(const std::string& value, size_t maximumCharacters) {
    if (value.empty()) return {};
    auto wide = toWide(value);
    if (!wide.empty()) {
        if (wide.size() > maximumCharacters) wide.resize(maximumCharacters);
        return toUtf8(wide);
    }
    return value.size() <= maximumCharacters ? value : value.substr(0, maximumCharacters);
}

static std::string cleanStoreDescription(const std::string& raw, size_t maximumCharacters) {
    if (raw.empty()) return {};
    std::string text = raw;
    try {
        text = std::regex_replace(text,
            std::regex("<\\s*br\\s*/?\\s*>", std::regex_constants::icase), "\n");
        text = std::regex_replace(text,
            std::regex("<\\s*/\\s*(p|div|li|h[1-6])\\s*>", std::regex_constants::icase), "\n");
        text = std::regex_replace(text, std::regex("<[^>]*>"), " ");
    } catch (...) {
        // If malformed provider HTML defeats the conservative regexes, keep the
        // text and still apply entity decoding, whitespace cleanup and limits.
    }
    replaceAll(text, "&nbsp;", " ");
    replaceAll(text, "&#160;", " ");
    replaceAll(text, "&quot;", "\"");
    replaceAll(text, "&#39;", "'");
    replaceAll(text, "&apos;", "'");
    replaceAll(text, "&lt;", "<");
    replaceAll(text, "&gt;", ">");
    replaceAll(text, "&amp;", "&");

    std::string cleaned;
    cleaned.reserve(text.size());
    bool pendingSpace = false;
    int pendingNewlines = 0;
    for (unsigned char ch : text) {
        if (ch == '\r' || ch == '\n') {
            pendingNewlines = (std::min)(2, pendingNewlines + 1);
            pendingSpace = false;
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v') {
            if (!cleaned.empty()) pendingSpace = true;
            continue;
        }
        while (pendingNewlines > 0 && !cleaned.empty() && cleaned.back() != '\n') {
            cleaned.push_back('\n');
            --pendingNewlines;
        }
        pendingNewlines = 0;
        if (pendingSpace && !cleaned.empty() && cleaned.back() != '\n') cleaned.push_back(' ');
        pendingSpace = false;
        cleaned.push_back(static_cast<char>(ch));
    }
    return truncateUtf8Characters(trim(cleaned), maximumCharacters);
}

static json stringArray(const json& value, const std::string& objectNameField = {}) {
    json result = json::array();
    std::set<std::string> seen;
    if (!value.is_array()) return result;
    for (const auto& item : value) {
        std::string text;
        if (item.is_string()) text = trim(item.get<std::string>());
        else if (!objectNameField.empty() && item.is_object()) text = trim(item.value(objectNameField, std::string{}));
        if (!text.empty() && seen.insert(asciiLower(text)).second) result.push_back(text);
    }
    return result;
}

static json buildGameMetadata(const ResolvedGame& game) {
    json metadata = {
        {"schemaVersion", 1},
        {"formalName", game.name},
        {"shortDescription", ""},
        {"description", ""},
        {"developers", json::array()},
        {"publishers", json::array()},
        {"releaseDate", nullptr},
        {"releaseYear", nullptr},
        {"genres", json::array()},
        {"features", json::array()},
        {"languages", ""},
        {"sources", json::array()},
        {"lastVerifiedAt", unixTimeMs()},
        {"automaticRefreshMayOverwriteManualFields", false}
    };

    if (game.details.is_object() && !game.details.empty()) {
        const auto shortDescription = cleanStoreDescription(
            game.details.value("short_description", std::string{}), 1200);
        auto description = cleanStoreDescription(
            game.details.value("about_the_game", std::string{}), 12000);
        if (description.empty()) description = cleanStoreDescription(
            game.details.value("detailed_description", std::string{}), 12000);
        if (!shortDescription.empty() && description.size() < shortDescription.size()) {
            description = shortDescription;
        }
        metadata["shortDescription"] = shortDescription;
        metadata["description"] = description;
        metadata["developers"] = stringArray(game.details.value("developers", json::array()));
        metadata["publishers"] = stringArray(game.details.value("publishers", json::array()));
        metadata["genres"] = stringArray(game.details.value("genres", json::array()), "description");
        metadata["features"] = stringArray(game.details.value("categories", json::array()), "description");
        metadata["languages"] = cleanStoreDescription(
            game.details.value("supported_languages", std::string{}), 2000);
        if (game.details.contains("release_date") && game.details["release_date"].is_object()) {
            const auto& release = game.details["release_date"];
            metadata["releaseDate"] = {
                {"display", release.value("date", std::string{})},
                {"comingSoon", release.value("coming_soon", false)},
                {"provider", "steam"}
            };
            const int year = releaseYearFromValue(release);
            if (year > 0) {
                metadata["releaseDate"]["year"] = year;
                metadata["releaseYear"] = year;
            }
        }
        metadata["sources"].push_back({
            {"provider", "steam"},
            {"role", game.matchedContentAppId > 0 ? "matched-content" : "base-game"},
            {"language", "english"},
            {"hasShortDescription", !shortDescription.empty()},
            {"hasDescription", !description.empty()}
        });
    }

    if (game.baseGameDetails.is_object() && !game.baseGameDetails.empty() && game.matchedContentAppId > 0) {
        const auto fallbackShort = cleanStoreDescription(
            game.baseGameDetails.value("short_description", std::string{}), 1200);
        auto fallbackDescription = cleanStoreDescription(
            game.baseGameDetails.value("about_the_game", std::string{}), 12000);
        if (fallbackDescription.empty()) fallbackDescription = cleanStoreDescription(
            game.baseGameDetails.value("detailed_description", std::string{}), 12000);
        if (metadata.value("shortDescription", std::string{}).empty()) metadata["shortDescription"] = fallbackShort;
        if (metadata.value("description", std::string{}).empty()) metadata["description"] = fallbackDescription;
        if (metadata["developers"].empty()) metadata["developers"] = stringArray(
            game.baseGameDetails.value("developers", json::array()));
        if (metadata["publishers"].empty()) metadata["publishers"] = stringArray(
            game.baseGameDetails.value("publishers", json::array()));
        if (metadata["genres"].empty()) metadata["genres"] = stringArray(
            game.baseGameDetails.value("genres", json::array()), "description");
        if (metadata["features"].empty()) metadata["features"] = stringArray(
            game.baseGameDetails.value("categories", json::array()), "description");
        if (metadata.value("languages", std::string{}).empty()) metadata["languages"] = cleanStoreDescription(
            game.baseGameDetails.value("supported_languages", std::string{}), 2000);
        if ((metadata["releaseDate"].is_null() || releaseYearFromValue(metadata["releaseDate"]) <= 0) &&
            game.baseGameDetails.contains("release_date") &&
            game.baseGameDetails["release_date"].is_object()) {
            const auto& release = game.baseGameDetails["release_date"];
            metadata["releaseDate"] = {
                {"display", release.value("date", std::string{})},
                {"comingSoon", release.value("coming_soon", false)},
                {"provider", "steam-base-game"}
            };
            const int year = releaseYearFromValue(release);
            if (year > 0) {
                metadata["releaseDate"]["year"] = year;
                metadata["releaseYear"] = year;
            }
        }
        metadata["sources"].push_back({
            {"provider", "steam"}, {"role", "base-game-fallback"}, {"language", "english"},
            {"hasShortDescription", !fallbackShort.empty()},
            {"hasDescription", !fallbackDescription.empty()}
        });
    }

    if (game.igdbDetails.is_object() && !game.igdbDetails.empty()) {
        const auto summary = cleanStoreDescription(
            game.igdbDetails.value("summary", std::string{}), 1200);
        auto description = cleanStoreDescription(
            game.igdbDetails.value("storyline", std::string{}), 12000);
        if (description.empty()) description = summary;
        if (metadata.value("shortDescription", std::string{}).empty()) metadata["shortDescription"] = summary;
        if (metadata.value("description", std::string{}).empty()) metadata["description"] = description;
        if (metadata["genres"].empty()) {
            metadata["genres"] = stringArray(
                game.igdbDetails.value("genres_expanded", json::array()), "name");
        }
        if (metadata["releaseDate"].is_null() && game.igdbDetails.contains("first_release_date")) {
            metadata["releaseDate"] = {
                {"unixSeconds", game.igdbDetails["first_release_date"]},
                {"provider", "playnite-igdb"}
            };
            const int year = releaseYearFromValue(game.igdbDetails["first_release_date"]);
            if (year > 0) {
                metadata["releaseDate"]["year"] = year;
                metadata["releaseYear"] = year;
            }
        }
        metadata["sources"].push_back({
            {"provider", "playnite-igdb"},
            {"hasShortDescription", !summary.empty()},
            {"hasDescription", !description.empty()}
        });
    }

    metadata["hasIntroduction"] =
        !metadata.value("shortDescription", std::string{}).empty() ||
        !metadata.value("description", std::string{}).empty();
    return metadata;
}

static uint64_t parsePositiveId(const std::wstring& raw, const std::string& option) {
    try {
        size_t used = 0;
        const auto value = std::stoull(raw, &used, 10);
        if (used != raw.size() || value == 0) throw std::runtime_error("invalid");
        return value;
    } catch (...) {
        throw std::runtime_error(option + " requires a positive numeric ID");
    }
}

static json describeResolutionFailure(
    const json& steamRounds,
    const json& igdbRounds,
    bool directIdMode) {
    NetworkEvidence evidence;
    collectNetworkEvidence(steamRounds, evidence);
    collectNetworkEvidence(igdbRounds, evidence);
    std::string kind;
    if (evidence.successfulResponses == 0 && evidence.retryableFailures > 0) {
        kind = "network-unavailable";
    } else if (directIdMode) {
        kind = "manual-id-rejected";
    } else {
        kind = "unrecognized";
    }
    return {
        {"kind", kind},
        {"retryWhenNetworkReturns", kind == "network-unavailable"},
        {"successfulResponses", evidence.successfulResponses},
        {"retryableFailures", evidence.retryableFailures},
        {"nonRetryableResponses", evidence.nonRetryableResponses}
    };
}

static void collectManualCandidates(
    const json& value,
    const std::string& provider,
    std::map<std::string, json>& candidates) {
    if (value.is_array()) {
        for (const auto& item : value) collectManualCandidates(item, provider, candidates);
        return;
    }
    if (!value.is_object()) return;
    if (value.contains("ranked") && value["ranked"].is_array()) {
        for (const auto& ranked : value["ranked"]) {
            if (!ranked.is_object()) continue;
            std::string key;
            json candidate = {
                {"provider", provider},
                {"name", ranked.value("name", std::string{})},
                {"score", ranked.value("score", 0.0)},
                {"platforms", ranked.contains("platforms") ? ranked["platforms"] : json::array()}
            };
            if (ranked.contains("year")) candidate["year"] = ranked["year"];
            else if (ranked.contains("releaseYear")) candidate["year"] = ranked["releaseYear"];
            else if (ranked.contains("releaseDate")) candidate["releaseDate"] = ranked["releaseDate"];
            if (ranked.contains("appId") && ranked["appId"].is_number_integer()) {
                const int appId = ranked["appId"].get<int>();
                key = "steam:" + std::to_string(appId);
                candidate["steamAppId"] = appId;
                candidate["igdbId"] = nullptr;
                if (ranked.contains("preview") && ranked["preview"].is_string()) {
                    candidate["preview"] = ranked["preview"];
                } else if (ranked.contains("artworkPreview") && ranked["artworkPreview"].is_string()) {
                    candidate["preview"] = ranked["artworkPreview"];
                } else {
                    // Do not manufacture a deterministic Steam CDN URL here:
                    // many modern AppIDs no longer publish that filename.
                    candidate["preview"] = nullptr;
                }
            } else if (ranked.contains("igdbId") && ranked["igdbId"].is_number_unsigned()) {
                const auto igdbId = ranked["igdbId"].get<uint64_t>();
                key = "igdb:" + std::to_string(igdbId);
                candidate["steamAppId"] = nullptr;
                candidate["igdbId"] = igdbId;
                candidate["preview"] = nullptr;
            } else if (ranked.contains("igdbId") && ranked["igdbId"].is_number_integer()) {
                const auto igdbId = ranked["igdbId"].get<int64_t>();
                if (igdbId <= 0) continue;
                key = "igdb:" + std::to_string(igdbId);
                candidate["steamAppId"] = nullptr;
                candidate["igdbId"] = igdbId;
                candidate["preview"] = nullptr;
            }
            if (key.empty() || candidate.value("name", std::string{}).empty()) continue;
            const auto existing = candidates.find(key);
            if (existing == candidates.end() ||
                candidate.value("score", 0.0) > existing->second.value("score", 0.0)) {
                candidates[key] = std::move(candidate);
            }
        }
    }
    for (const auto& [_, item] : value.items()) collectManualCandidates(item, provider, candidates);
}

static json manualReviewCandidates(const json& steamRounds, const json& igdbRounds) {
    std::map<std::string, json> unique;
    collectManualCandidates(steamRounds, "steam", unique);
    collectManualCandidates(igdbRounds, "playnite-igdb", unique);
    std::vector<json> ranked;
    for (auto& [_, value] : unique) ranked.push_back(std::move(value));
    std::sort(ranked.begin(), ranked.end(), [](const json& left, const json& right) {
        return left.value("score", 0.0) > right.value("score", 0.0);
    });
    if (ranked.size() > 5) ranked.resize(5);
    return ranked;
}

static std::wstring lowerWide(std::wstring value) {
    for (auto& character : value) character = static_cast<wchar_t>(std::towlower(character));
    return value;
}

// Defined with the rest of the scanner classification helpers below.  The
// first-run root discovery must be able to reject known utility/modifier
// folders before it treats a name containing "game" as a library root.
static std::optional<std::string> knownNonGameDirectoryKind(const fs::path& directory);
static bool excludedLibrarySubdirectory(const fs::path& directory);

// First-run discovery is intentionally conservative.  It only considers
// fixed local disks, looks at two directory levels, and never enters Windows'
// protected/system folders.  A candidate is a library root (for example
// D:\\Game), not a single game directory; the normal scanner then enumerates
// the immediate game directories below that root.
static bool firstRunProtectedDirectory(const fs::path& directory) {
    const auto name = lowerWide(directory.filename().wstring());
    static const std::set<std::wstring> blocked = {
        L"$recycle.bin", L"system volume information", L"windows", L"programdata",
        L"recovery", L"config.msi", L"msocache", L"perflogs", L"windowsapps"
    };
    return blocked.contains(name);
}

static bool firstRunSteamDirectory(const fs::path& directory) {
    const auto name = lowerWide(directory.filename().wstring());
    return name == L"steam" || name == L"steamapps" || name == L"steam library" || name == L"steamlibrary";
}

static bool firstRunExcludedDefaultDirectory(const fs::path& directory) {
    return firstRunProtectedDirectory(directory) || firstRunSteamDirectory(directory) ||
        knownNonGameDirectoryKind(directory).has_value();
}

static bool firstRunGameLikeDirectory(const fs::path& directory) {
    const auto name = lowerWide(directory.filename().wstring());
    // Do not use a substring test here.  "Game Cheats Manager" and similar
    // utilities are not game-library roots.  First-run discovery is limited
    // to conventional container names; explicitly configured roots remain
    // available through the normal manual path picker.
    static const std::set<std::wstring> libraryNames = {
        L"game", L"games", L"game library", L"game libraries",
        L"游戏", L"游戏库", L"游戏目录"
    };
    return libraryNames.contains(name);
}

// A directory name such as "Games" is only a hint.  It is not a library
// root until at least one child game directory contains a real PE executable.
// This deliberately ignores .lnk/.url shortcut files, which are not valid
// scrape targets for the native library scanner.
static bool libraryRootHasExecutable(const fs::path& root, int maxDepth = 5) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return false;
    for (const auto& child : fs::directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!child.is_directory(ec)) { ec.clear(); continue; }
        for (fs::recursive_directory_iterator it(
                 child.path(), fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it->is_directory(ec)) {
                if (it.depth() >= maxDepth || excludedLibrarySubdirectory(it->path())) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            if (asciiLower(toUtf8(it->path().extension().wstring())) == ".exe") return true;
        }
    }
    return false;
}

static bool firstRunFixedDrivePath(const fs::path& path) {
    const auto root = path.root_path();
    return !root.empty() && GetDriveTypeW(root.wstring().c_str()) == DRIVE_FIXED;
}

static std::vector<fs::path> firstRunDefaultLibraryRoots() {
    std::vector<fs::path> result;
    std::set<std::string> seen;
    auto add = [&](const fs::path& candidate) {
        std::error_code error;
        if (!firstRunFixedDrivePath(candidate) || !fs::is_directory(candidate, error) ||
            firstRunExcludedDefaultDirectory(candidate) || !libraryRootHasExecutable(candidate)) return;
        const auto canonical = fs::weakly_canonical(candidate, error);
        const auto resolved = error ? candidate : canonical;
        const auto key = canonicalPathKey(resolved);
        if (!key.empty() && seen.insert(key).second) result.push_back(resolved);
    };
    auto environmentPath = [](const wchar_t* name, const fs::path& fallback) {
        const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
        if (needed <= 1) return fallback;
        std::wstring value(needed, L'\0');
        const DWORD length = GetEnvironmentVariableW(name, value.data(), needed);
        if (length == 0 || length >= needed) return fallback;
        value.resize(length);
        return fs::path(value);
    };

    const auto programFiles = environmentPath(L"ProgramFiles", fs::path(L"C:\\Program Files"));
    const auto programFilesX86 = environmentPath(L"ProgramFiles(x86)", fs::path(L"C:\\Program Files (x86)"));
    const auto knownRoots = std::vector<fs::path>{
        fs::path(L"D:\\Game"), fs::path(L"D:\\Games"), fs::path(L"C:\\Games"),
        programFiles / L"Epic Games", fs::path(L"C:\\WeGameApps\\Games"),
        programFiles / L"EA Games", programFilesX86 / L"Origin Games",
        programFilesX86 / L"Ubisoft\\Ubisoft Game Launcher\\games",
        fs::path(L"C:\\GOG Games"), programFilesX86 / L"GOG Galaxy\\Games"
    };
    for (const auto& candidate : knownRoots) add(candidate);

    // Search only fixed drives and only two levels from each drive root.  We
    // inspect directory names, never file contents, and skip blocked parents
    // before opening their children.  Removable USB and optical media never
    // enter this loop, so an inserted disc cannot stall first launch.
    const DWORD driveMask = GetLogicalDrives();
    for (int index = 0; index < 26; ++index) {
        if ((driveMask & (1u << index)) == 0) continue;
        const std::wstring driveRoot = std::wstring(1, static_cast<wchar_t>(L'A' + index)) + L":\\";
        if (GetDriveTypeW(driveRoot.c_str()) != DRIVE_FIXED) continue;
        const fs::path drive(driveRoot);
        std::error_code error;
        for (const auto& first : fs::directory_iterator(
                 drive, fs::directory_options::skip_permission_denied, error)) {
            if (error) break;
            if (!first.is_directory(error)) { error.clear(); continue; }
            error.clear();
            if (firstRunExcludedDefaultDirectory(first.path())) continue;
            if (firstRunGameLikeDirectory(first.path())) {
                add(first.path());
                // A directory named Game/Games is itself the library
                // container.  Do not descend into its child game folders and
                // accidentally register those as additional library roots.
                continue;
            }
            for (const auto& second : fs::directory_iterator(
                     first.path(), fs::directory_options::skip_permission_denied, error)) {
                if (error) break;
                if (!second.is_directory(error)) { error.clear(); continue; }
                error.clear();
                if (firstRunExcludedDefaultDirectory(second.path())) continue;
                if (firstRunGameLikeDirectory(second.path())) add(second.path());
            }
            error.clear();
        }
    }
    return result;
}

static json defaultLibraryConfig() {
    return {
        {"schemaVersion", 1},
        {"enabled", false},
        // Maintenance discovers directory changes.  Scraping is a separate
        // consent because it can use the network and download artwork.
        {"automaticScrapingEnabled", true},
        {"scanMode", "event-driven-no-polling"},
        {"automaticTriggers", {"app-start", "steam-launch", "library-page-open", "directory-change-while-app-running"}},
        {"roots", json::array()},
        {"trainerRoots", json::array()},
        {"manualGameDirectories", json::array()},
        {"excludedGameDirectories", json::array()},
        {"scanRootsExplicitlyCleared", false},
        {"manualPrimary", json::object()},
        // Presentation bucket is intentionally separate from identity and
        // Steam eligibility.  Moving an item between pages must never make a
        // tool eligible for import by itself.
        {"manualBuckets", json::object()},
        {"firstRunDefaultScan", {
            {"status", "pending"},
            {"candidateSearchDepth", 2},
            {"drivePolicy", "fixed-only"},
            {"autoAddToSteam", false}
        }}
    };
}

static json loadLibraryConfig(const fs::path& path) {
    DataTransactionMutex transaction;
    auto normalize = [](json value) {
        if (!value.is_object()) throw std::runtime_error("root is not an object");
        const auto defaults = defaultLibraryConfig();
        for (const auto& key : {"roots", "trainerRoots", "manualGameDirectories", "excludedGameDirectories"}) {
            if (!value.contains(key)) value[key] = defaults[key];
            else if (!value[key].is_array()) throw std::runtime_error(std::string("library config field is not an array: ") + key);
        }
        for (const auto& key : {"manualPrimary", "manualBuckets"}) {
            if (!value.contains(key)) value[key] = defaults[key];
            else if (!value[key].is_object()) throw std::runtime_error(std::string("library config field is not an object: ") + key);
        }
        if (!value.contains("scanRootsExplicitlyCleared")) value["scanRootsExplicitlyCleared"] = false;
        else if (!value["scanRootsExplicitlyCleared"].is_boolean()) throw std::runtime_error("library config scanRootsExplicitlyCleared is not a boolean");
        if (!value.contains("firstRunDefaultScan")) value["firstRunDefaultScan"] = defaults["firstRunDefaultScan"];
        else if (!value["firstRunDefaultScan"].is_object()) throw std::runtime_error("library config firstRunDefaultScan is not an object");
        const auto statusIt = value["firstRunDefaultScan"].find("status");
        const auto firstRunStatus = statusIt != value["firstRunDefaultScan"].end() && statusIt->is_string()
            ? statusIt->get<std::string>() : std::string{};
        if (firstRunStatus != "pending" && firstRunStatus != "running" &&
            firstRunStatus != "completed" && firstRunStatus != "skipped-existing-roots") {
            value["firstRunDefaultScan"] = defaults["firstRunDefaultScan"];
        }
        // Secondary launch entries belonged to an abandoned Steam shortcut
        // experiment.  Drop them during migration instead of retaining dead
        // configuration that could be revived by an old build.
        value.erase("secondaryItems");
        if (!value.contains("enabled") || !value["enabled"].is_boolean()) value["enabled"] = false;
        if (!value.contains("automaticScrapingEnabled") || !value["automaticScrapingEnabled"].is_boolean()) {
            value["automaticScrapingEnabled"] = true;
        }
        value["schemaVersion"] = 1;
        value["scanMode"] = "event-driven-no-polling";
        value["automaticTriggers"] = defaults["automaticTriggers"];
        return value;
    };
    return loadUserConfigDocument(path, 4u << 20, "library-config", defaultLibraryConfig(), normalize);
}

static bool pathWithin(const fs::path& child, const fs::path& parent);

struct LegacyDataCopyStats {
    uint64_t filesCopied = 0;
    uint64_t filesSkippedBecauseTargetWins = 0;
    uint64_t bytesCopied = 0;
    uint64_t symlinksSkipped = 0;
};

static bool replaceAllString(std::string& value, const std::string& from, const std::string& to) {
    if (from.empty()) return false;
    bool changed = false;
    size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
        changed = true;
    }
    return changed;
}

static void rewriteLegacyPathStrings(
    json& value,
    const std::vector<std::pair<std::string, std::string>>& pathMappings) {
    if (value.is_string()) {
        auto text = value.get<std::string>();
        bool changed = false;
        for (const auto& [from, to] : pathMappings) changed = replaceAllString(text, from, to) || changed;
        if (changed) value = std::move(text);
        return;
    }
    if (value.is_array()) {
        for (auto& child : value) rewriteLegacyPathStrings(child, pathMappings);
        return;
    }
    if (value.is_object()) {
        for (auto& [_, child] : value.items()) rewriteLegacyPathStrings(child, pathMappings);
    }
}

static void normalizeMigratedJsonPaths(
    const fs::path& dataRoot,
    const std::vector<fs::path>& legacyRoots) {
    std::vector<std::pair<std::string, std::string>> mappings;
    for (const auto& legacyRoot : legacyRoots) {
        const auto oldPath = toUtf8(legacyRoot.lexically_normal().wstring());
        const auto newPath = toUtf8(dataRoot.lexically_normal().wstring());
        const auto oldSlashPath = [&]() {
            auto value = oldPath;
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }();
        const auto newSlashPath = [&]() {
            auto value = newPath;
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }();
        mappings.push_back({oldPath + "\\artwork-overrides", newPath + "\\artwork\\overrides"});
        mappings.push_back({oldSlashPath + "/artwork-overrides", newSlashPath + "/artwork/overrides"});
        mappings.push_back({oldPath, newPath});
        mappings.push_back({oldSlashPath, newSlashPath});
    }
    mappings.push_back({
        "https://steam-library-data.local/artwork-overrides/",
        "https://steam-library-data.local/artwork/overrides/"
    });

    for (const auto& section : {L"config", L"state", L"games", L"artwork"}) {
        const auto root = dataRoot / section;
        std::error_code error;
        if (!fs::is_directory(root, error)) continue;
        for (fs::recursive_directory_iterator iterator(
                 root, fs::directory_options::skip_permission_denied, error), end;
             iterator != end; iterator.increment(error)) {
            if (error) { error.clear(); continue; }
            if (!iterator->is_regular_file(error) || iterator->path().extension() != L".json") {
                error.clear();
                continue;
            }
            if (iterator->path().filename() == L"data-migration.json") {
                error.clear();
                continue;
            }
            try {
                const auto bytes = readBinaryFile(iterator->path(), 128u << 20);
                auto document = json::parse(bytes.begin(), bytes.end());
                const auto original = document;
                rewriteLegacyPathStrings(document, mappings);
                if (document != original) writeJsonAtomic(iterator->path(), document);
            } catch (...) {
                // Historical logs and optional manifests are not allowed to
                // prevent the application from opening after migration.
            }
            error.clear();
        }
    }
}

static void copyMissingLegacyTree(
    const fs::path& source,
    const fs::path& target,
    LegacyDataCopyStats& stats) {
    std::error_code error;
    if (!fs::exists(source, error)) return;
    if (fs::is_symlink(source, error)) {
        ++stats.symlinksSkipped;
        return;
    }
    if (fs::is_regular_file(source, error)) {
        if (fs::exists(target, error)) {
            ++stats.filesSkippedBecauseTargetWins;
            return;
        }
        fs::create_directories(target.parent_path(), error);
        if (error) throw std::runtime_error("Cannot create migrated data directory: " + toUtf8(target.parent_path().wstring()));
        fs::copy_file(source, target, fs::copy_options::none, error);
        if (error) throw std::runtime_error("Cannot migrate data file: " + toUtf8(source.wstring()));
        ++stats.filesCopied;
        stats.bytesCopied += fs::file_size(source, error);
        return;
    }
    if (!fs::is_directory(source, error)) return;

    fs::create_directories(target, error);
    if (error) throw std::runtime_error("Cannot create migrated data directory: " + toUtf8(target.wstring()));
    for (const auto& entry : fs::directory_iterator(source, error)) {
        if (error) throw std::runtime_error("Cannot enumerate legacy data directory: " + toUtf8(source.wstring()));
        copyMissingLegacyTree(entry.path(), target / entry.path().filename(), stats);
    }
}

static void backupMigrationSettings(
    const fs::path& dataRoot,
    const std::vector<fs::path>& legacyRoots,
    const fs::path& backupRoot) {
    std::error_code error;
    bool copied = false;
    for (const auto& name : {L"library-config.json", L"manual-overrides.json", L"data-root.json"}) {
        const auto target = dataRoot / L"config" / name;
        if (fs::is_regular_file(target, error)) {
            fs::create_directories(backupRoot / L"target-config", error);
            fs::copy_file(target, backupRoot / L"target-config" / name,
                fs::copy_options::overwrite_existing, error);
            if (error) throw std::runtime_error("Cannot back up migration settings: " + toUtf8(target.wstring()));
            copied = true;
        }
    }
    for (const auto& legacyRoot : legacyRoots) {
        const auto sourceConfig = legacyRoot / L"config";
        if (!fs::is_directory(sourceConfig, error)) continue;
        for (const auto& name : {L"library-config.json", L"manual-overrides.json", L"data-root.json"}) {
            const auto source = sourceConfig / name;
            if (!fs::is_regular_file(source, error)) continue;
            fs::create_directories(backupRoot / L"legacy-config", error);
            fs::copy_file(source, backupRoot / L"legacy-config" / name,
                fs::copy_options::overwrite_existing, error);
            if (error) throw std::runtime_error("Cannot back up legacy settings: " + toUtf8(source.wstring()));
            copied = true;
        }
    }
    if (!copied) {
        std::error_code ignored;
        fs::remove(backupRoot, ignored);
    }
}

static fs::path migrateLegacyData(
    const fs::path& dataRoot,
    const fs::path& labRoot) {
    const auto markerPath = dataRoot / L"state" / L"data-migration.json";
    json previousMarker = json::object();
    if (fs::is_regular_file(markerPath)) {
        try {
            const auto bytes = readBinaryFile(markerPath, 4u << 20);
            previousMarker = json::parse(bytes.begin(), bytes.end());
        } catch (...) {
            previousMarker = json::object();
        }
        if (previousMarker.is_object() && previousMarker.value("schemaVersion", 0) >= 2) return {};
    }

    std::vector<fs::path> legacyRoots;
    const auto configuredSources = previousMarker.value("sources", json::array());
    if (configuredSources.is_array()) {
        for (const auto& source : configuredSources) {
            if (source.is_string()) legacyRoots.emplace_back(toWide(source.get<std::string>()));
        }
    }
    if (legacyRoots.empty()) for (const auto& candidate : custom_steam_library::legacyDataRoots(labRoot)) {
        if (canonicalPathKey(candidate) == canonicalPathKey(dataRoot)) continue;
        std::error_code error;
        if (fs::is_directory(candidate, error)) legacyRoots.push_back(candidate);
    }

    if (fs::is_regular_file(markerPath) && previousMarker.is_object() &&
        previousMarker.value("schemaVersion", 0) < 2) {
        std::vector<fs::path> existingRoots;
        for (const auto& root : legacyRoots) {
            if (fs::is_directory(root)) existingRoots.push_back(root);
        }
        normalizeMigratedJsonPaths(dataRoot, existingRoots);
        previousMarker["schemaVersion"] = 2;
        previousMarker["pathNormalization"] = "legacy-data-root-and-artwork-overrides-rewritten";
        previousMarker["normalizedAt"] = unixTimeMs();
        writeJsonAtomic(markerPath, previousMarker);
        return markerPath;
    }

    const auto looseConfig = labRoot / L"library-config.json";
    const auto looseOverrides = labRoot / L"manual-overrides.json";
    bool hasLooseLegacy = fs::is_regular_file(looseConfig) || fs::is_regular_file(looseOverrides);
    if (legacyRoots.empty() && !hasLooseLegacy) return {};

    const auto token = std::to_wstring(unixTimeMs());
    const auto backupRoot = dataRoot / L"backups" / L"migration" / token;
    backupMigrationSettings(dataRoot, legacyRoots, backupRoot);

    LegacyDataCopyStats stats;
    json sourceList = json::array();
    for (const auto& legacyRoot : legacyRoots) {
        sourceList.push_back(toUtf8(legacyRoot.wstring()));
        // The old manual-image directory was outside the artwork tree. Keep
        // the new layout strict while preserving every file.
        copyMissingLegacyTree(
            legacyRoot / L"artwork-overrides",
            dataRoot / L"artwork" / L"overrides",
            stats);
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(legacyRoot, error)) {
            if (error) throw std::runtime_error("Cannot enumerate legacy data directory: " + toUtf8(legacyRoot.wstring()));
            if (entry.path().filename() == L"artwork-overrides") continue;
            copyMissingLegacyTree(entry.path(), dataRoot / entry.path().filename(), stats);
        }
    }

    for (const auto& [source, target] : std::array<std::pair<fs::path, fs::path>, 2>{{
        {looseConfig, dataRoot / L"config" / L"library-config.json"},
        {looseOverrides, dataRoot / L"config" / L"manual-overrides.json"}
    }}) {
        if (fs::is_regular_file(source) && !fs::is_regular_file(target)) {
            copyMissingLegacyTree(source, target, stats);
            sourceList.push_back(toUtf8(source.wstring()));
        }
    }

    writeJsonAtomic(markerPath, {
        {"schemaVersion", 2},
        {"strategy", "copy-missing-files-target-wins"},
        {"targetDataRoot", toUtf8(dataRoot.wstring())},
        {"sources", sourceList},
        {"backupDirectory", fs::is_directory(backupRoot) ? json(toUtf8(backupRoot.wstring())) : json(nullptr)},
        {"filesCopied", stats.filesCopied},
        {"filesSkippedBecauseTargetWins", stats.filesSkippedBecauseTargetWins},
        {"bytesCopied", stats.bytesCopied},
        {"symlinksSkipped", stats.symlinksSkipped},
        {"pathNormalization", "legacy-data-root-and-artwork-overrides-rewritten"},
        {"completedAt", unixTimeMs()}
    });
    normalizeMigratedJsonPaths(dataRoot, legacyRoots);
    return markerPath;
}

static fs::path steamBigPictureDataRoot(const fs::path& labRoot) {
    return custom_steam_library::defaultDataRoot(labRoot);
}

static void initializeSteamBigPictureDataRoot(const fs::path& dataRoot, const fs::path& labRoot) {
    DataTransactionMutex transaction;
    for (const auto& directory : {
        dataRoot / L"config", dataRoot / L"backups", dataRoot / L"state",
        dataRoot / L"games", dataRoot / L"artwork", dataRoot / L"artwork" / L"overrides",
        dataRoot / L"cache", dataRoot / L"cache" / L"jobs", dataRoot / L"cache" / L"webview"}) {
        fs::create_directories(directory);
    }
    // An explicitly selected data root is an isolation boundary.  Do not
    // silently import the development tree or an old installation into a
    // caller-provided/test directory; the default install path still runs
    // the normal copy-only migration below.
    if (custom_steam_library::configuredDataRoot().empty()) {
        migrateLegacyData(dataRoot, labRoot);
    }
    const auto manifestPath = dataRoot / L"data-root.json";
    json manifest = json::object();
    if (fs::is_regular_file(manifestPath)) {
        try {
            const auto bytes = readBinaryFile(manifestPath, 1u << 20);
            manifest = json::parse(bytes.begin(), bytes.end());
        } catch (...) {
            manifest = json::object();
        }
    }
    if (!manifest.is_object()) manifest = json::object();
    manifest["schemaVersion"] = 2;
    manifest["featureName"] = "Custom Steam Library";
    manifest["productName"] = "Steam自定义游戏库";
    manifest["encoding"] = "UTF-8";
    manifest["portableReinstallData"] = true;
    manifest["dataLayoutVersion"] = 2;
    manifest["dataRootRole"] = "portable-config-cache-artwork-and-game-metadata";
    manifest["dataRoot"] = toUtf8(dataRoot.wstring());
    if (!manifest.contains("createdAt")) manifest["createdAt"] = unixTimeMs();
    writeJsonAtomic(manifestPath, manifest);
    const auto libraryConfig = dataRoot / L"config" / L"library-config.json";
    if (!fs::is_regular_file(libraryConfig)) writeJsonAtomic(libraryConfig, loadLibraryConfig(libraryConfig));
}

static std::string gameDataId(const fs::path& gameDirectory) {
    const auto key = canonicalPathKey(gameDirectory);
    return sha256(std::vector<unsigned char>(key.begin(), key.end()));
}

static fs::path persistentGameDirectoryForExecutable(
    const fs::path& dataRoot,
    const fs::path& executable) {
    const auto wanted = canonicalPathKey(executable);
    const auto statePath = dataRoot / L"state" / L"library-scan.json";
    if (fs::is_regular_file(statePath)) {
        try {
            const auto bytes = readBinaryFile(statePath, 64u << 20);
            const auto state = json::parse(bytes.begin(), bytes.end());
            for (const auto& game : state.value("games", json::array())) {
                const auto directory = fs::path(toWide(game.value("gameDirectory", std::string{})));
                bool matches = canonicalPathKey(fs::path(toWide(
                    game.value("primaryExecutable", std::string{})))) == wanted;
                if (!matches) {
                    for (const auto& candidate : game.value("candidates", json::array())) {
                        if (canonicalPathKey(fs::path(toWide(candidate.value("path", std::string{})))) == wanted) {
                            matches = true;
                            break;
                        }
                    }
                }
                // A verified game EXE can live below the game root (for
                // example WarThunder\\win64\\aces.exe).  Preserve artwork
                // against the scanned game root even if a stale scan record
                // no longer repeats that exact primary path.
                if (!matches && !directory.empty()) {
                    auto directoryKey = canonicalPathKey(directory);
                    if (!directoryKey.empty() && directoryKey.back() != '\\') directoryKey.push_back('\\');
                    matches = wanted.starts_with(directoryKey);
                }
                if (matches) {
                    if (!directory.empty()) return directory;
                }
            }
        } catch (...) {
            // Artwork still remains usable in the diagnostic output if the
            // portable state snapshot is missing or temporarily corrupt.
        }
    }
    // A recovery fallback for a just-created or partially recoverable scan
    // state: roots define immediate game directories, so retain that game root
    // rather than using a nested binary directory such as win64 or binaries.
    try {
        const auto config = loadLibraryConfig(dataRoot / L"config" / L"library-config.json");
        for (const auto& rootValue : config.value("roots", json::array())) {
            if (!rootValue.is_string()) continue;
            const auto root = fs::path(toWide(rootValue.get<std::string>()));
            auto rootKey = canonicalPathKey(root);
            if (rootKey.empty()) continue;
            if (rootKey.back() != '\\') rootKey.push_back('\\');
            if (!wanted.starts_with(rootKey)) continue;
            const auto remainder = wanted.substr(rootKey.size());
            const auto separator = remainder.find('\\');
            if (separator != std::string::npos && separator != 0) {
                return root / fs::path(toWide(remainder.substr(0, separator)));
            }
        }
    } catch (...) {
        // The regular output directory remains available even if recovery
        // metadata itself is damaged.
    }
    return executable.parent_path();
}

static std::string jsonStringOr(const json& value, const char* key, std::string fallback);
static bool jsonBoolSafe(const json& value, const char* key, bool fallback);

static void summarizeArtworkManifest(json& manifest) {
    std::set<std::string> available;
    if (manifest.contains("artwork") && manifest["artwork"].is_array()) {
        for (const auto& image : manifest["artwork"]) {
            if (!image.is_object() || !jsonBoolSafe(image, "ok", false)) continue;
            for (const auto* field : {"portableFile", "file"}) {
                std::error_code error;
                const auto source = fs::path(toWide(jsonStringOr(image, field, {})));
                if (source.empty() || !fs::is_regular_file(source, error)) continue;
                try {
                    if (inspectImage(readBinaryFile(source, MAX_IMAGE_BYTES))) {
                        available.insert(jsonStringOr(image, "type", {}));
                        break;
                    }
                } catch (...) { }
            }
        }
    }
    available.erase("");
    manifest["successCount"] = available.size();
    manifest["required"] = {{"tall", available.count("tall") != 0}, {"hero", available.count("hero") != 0}};
    manifest["artworkAvailable"] = !available.empty();
    manifest["artworkComplete"] = available.count("tall") && available.count("long") && available.count("hero");
}

static void persistArtworkToSteamBigPictureData(
    const fs::path& dataRoot,
    const fs::path& executable,
    json& manifest) {
    const auto gameDirectory = persistentGameDirectoryForExecutable(dataRoot, executable);
    const auto id = gameDataId(gameDirectory);
    if (id.empty()) return;
    const auto targetDirectory = dataRoot / L"artwork" / toWide(id);
    if (!pathWithin(targetDirectory, dataRoot)) throw std::runtime_error("Invalid portable artwork target");
    fs::create_directories(targetDirectory);
    size_t copied = 0;
    for (auto& item : manifest["artwork"]) {
        if (!item.value("ok", false)) continue;
        const auto source = fs::path(toWide(item.value("file", std::string{})));
        if (!fs::is_regular_file(source)) continue;
        const auto extension = source.extension();
        auto targetName = toWide(item.value("type", std::string{"artwork"}));
        targetName += extension.wstring();
        const auto target = targetDirectory / targetName;
        if (canonicalPathKey(source) == canonicalPathKey(target)) {
            // A second-round fallback preserves valid slots from the durable
            // manifest. Their source already is the target file; copying a
            // Windows file onto itself fails and used to prevent later slots
            // such as the newly fetched horizontal cover from being saved.
            item["portableFile"] = toUtf8(target.wstring());
            ++copied;
            continue;
        }
        // Keep the last good image intact if replacement is interrupted or locked.
        writeAtomic(target, readBinaryFile(source, MAX_IMAGE_BYTES));
        item["portableFile"] = toUtf8(target.wstring());
        ++copied;
    }
    summarizeArtworkManifest(manifest);
    manifest["portableManifestPath"] = toUtf8((targetDirectory / L"manifest.json").wstring());
    manifest["portableData"] = {
        {"featureName", CUSTOM_STEAM_LIBRARY_FEATURE_NAME},
        {"gameDirectory", toUtf8(gameDirectory.wstring())},
        {"gameDataId", id},
        {"artworkDirectory", toUtf8(targetDirectory.wstring())},
        {"artworkCopied", copied}
    };
    writeJsonAtomic(targetDirectory / L"manifest.json", manifest);
}

static bool configuredDirectory(const json& values, const fs::path& directory) {
    const auto wanted = canonicalPathKey(directory);
    for (const auto& item : values) {
        const auto raw = item.is_object() ? item.value("path", std::string{}) :
            (item.is_string() ? item.get<std::string>() : std::string{});
        if (!raw.empty() && canonicalPathKey(fs::path(toWide(raw))) == wanted) return true;
    }
    return false;
}

static json withoutConfiguredDirectory(const json& values, const fs::path& directory) {
    json updated = json::array();
    const auto wanted = canonicalPathKey(directory);
    for (const auto& item : values) {
        const auto raw = item.is_object() ? item.value("path", std::string{}) :
            (item.is_string() ? item.get<std::string>() : std::string{});
        if (raw.empty() || canonicalPathKey(fs::path(toWide(raw))) != wanted) updated.push_back(item);
    }
    return updated;
}

static bool pathWithin(const fs::path& child, const fs::path& parent) {
    const auto childKey = canonicalPathKey(child);
    auto parentKey = canonicalPathKey(parent);
    if (childKey == parentKey) return true;
    if (!parentKey.empty() && parentKey.back() != '\\') parentKey.push_back('\\');
    return childKey.starts_with(parentKey);
}

static const std::array<std::wstring, 4>& portableSnapshotSections() {
    static const std::array<std::wstring, 4> sections = {L"config", L"state", L"games", L"artwork"};
    return sections;
}

static bool safeSnapshotRelativePath(const fs::path& value) {
    if (value.empty() || value.is_absolute() || value.has_root_name() || value.has_root_directory()) return false;
    for (const auto& component : value) {
        if (component.empty() || component == L"." || component == L".." || component.native().find(L':') != std::wstring::npos) return false;
    }
    return true;
}

static bool isReparsePoint(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static void assertNoReparsePoints(const fs::path& root) {
    if (isReparsePoint(root)) throw std::runtime_error("Portable data contains a reparse point: " + toUtf8(root.wstring()));
    std::error_code ec;
    fs::recursive_directory_iterator iterator(root, fs::directory_options::none, ec), end;
    if (ec) throw std::runtime_error("Cannot enumerate portable backup source");
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) throw std::runtime_error("Cannot enumerate portable backup source");
        if (isReparsePoint(iterator->path())) {
            throw std::runtime_error("Portable data contains a reparse point: " + toUtf8(iterator->path().wstring()));
        }
    }
    if (ec) throw std::runtime_error("Cannot enumerate portable backup source");
}

static json snapshotInventory(const fs::path& snapshot) {
    json files = json::array();
    uint64_t totalBytes = 0;
    for (const auto& section : portableSnapshotSections()) {
        const auto root = snapshot / section;
        if (!fs::is_directory(root)) continue;
        assertNoReparsePoints(root);
        std::error_code ec;
        fs::recursive_directory_iterator iterator(root, fs::directory_options::none, ec), end;
        if (ec) throw std::runtime_error("Cannot inventory portable backup directory");
        for (; iterator != end; iterator.increment(ec)) {
            if (ec) throw std::runtime_error("Cannot inventory portable backup directory");
            if (isReparsePoint(iterator->path())) {
                throw std::runtime_error("Portable backup contains a reparse point");
            }
            const bool regular = iterator->is_regular_file(ec);
            if (ec) throw std::runtime_error("Cannot inventory portable backup entry");
            if (!regular) continue;
            const auto relative = fs::relative(iterator->path(), snapshot, ec);
            if (ec || !safeSnapshotRelativePath(relative)) {
                throw std::runtime_error("Cannot inventory portable backup path");
            }
            const auto bytes = readBinaryFile(iterator->path(), 128u << 20);
            totalBytes += bytes.size();
            files.push_back({
                {"path", toUtf8(relative.generic_wstring())},
                {"bytes", bytes.size()},
                {"sha256", sha256(bytes)}
            });
        }
        if (ec) throw std::runtime_error("Cannot inventory portable backup directory");
    }
    std::sort(files.begin(), files.end(), [](const json& left, const json& right) {
        return left.value("path", std::string{}) < right.value("path", std::string{});
    });
    return {{"files", files}, {"fileCount", files.size()}, {"totalBytes", totalBytes}};
}

static fs::path createPortableBackupSnapshot(
    const fs::path& dataRoot,
    const std::wstring& category,
    const std::string& reason) {
    DataTransactionMutex transaction;
    auto stamp = unixTimeMs();
    fs::path snapshot;
    do {
        snapshot = dataRoot / L"backups" / category / std::to_wstring(stamp++);
    } while (fs::exists(snapshot));
    fs::create_directories(snapshot);
    for (const auto& name : portableSnapshotSections()) {
        const auto source = dataRoot / name;
        if (!fs::is_directory(source)) continue;
        assertNoReparsePoints(source);
        std::error_code ec;
        fs::copy(source, snapshot / name,
            fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error("Cannot create Custom Steam Library portable backup snapshot");
    }
    const auto inventory = snapshotInventory(snapshot);
    writeJsonAtomic(snapshot / L"snapshot-manifest.json", {
        {"schemaVersion", 2},
        {"featureName", CUSTOM_STEAM_LIBRARY_FEATURE_NAME},
        {"sourceDataRoot", toUtf8(dataRoot.wstring())},
        {"createdAt", unixTimeMs()},
        {"reason", reason},
        {"included", {"config", "state", "games", "artwork"}},
        {"cacheExcluded", true},
        {"gameInstallFilesIncluded", false},
        {"fileCount", inventory["fileCount"]},
        {"totalBytes", inventory["totalBytes"]},
        {"files", inventory["files"]}
    });
    return snapshot;
}

static json verifyPortableBackupSnapshot(const fs::path& snapshot) {
    const auto manifestPath = snapshot / L"snapshot-manifest.json";
    if (!fs::is_regular_file(manifestPath)) throw std::runtime_error("Snapshot manifest is missing");
    const auto bytes = readBinaryFile(manifestPath, 16u << 20);
    const auto manifest = json::parse(bytes.begin(), bytes.end());
    if (!manifest.is_object() || !isCustomSteamLibraryFeatureName(manifest.value("featureName", std::string{})) ||
        !manifest.contains("files") || !manifest["files"].is_array()) {
        throw std::runtime_error("Snapshot manifest is invalid or predates integrity hashes");
    }
    std::set<std::string> manifestPaths;
    size_t verified = 0;
    uint64_t totalBytes = 0;
    for (const auto& item : manifest["files"]) {
        const auto relative = fs::path(toWide(item.value("path", std::string{})));
        if (!safeSnapshotRelativePath(relative)) throw std::runtime_error("Snapshot contains an unsafe path");
        const auto normalized = toUtf8(relative.generic_wstring());
        if (!manifestPaths.insert(normalized).second) throw std::runtime_error("Snapshot manifest contains duplicate paths");
        const auto file = snapshot / relative;
        if (!pathWithin(file, snapshot) || isReparsePoint(file) || !fs::is_regular_file(file)) {
            throw std::runtime_error("Snapshot file is missing: " + item.value("path", std::string{}));
        }
        const auto fileBytes = readBinaryFile(file, 128u << 20);
        if (fileBytes.size() != item.value("bytes", static_cast<size_t>(0)) ||
            sha256(fileBytes) != item.value("sha256", std::string{})) {
            throw std::runtime_error("Snapshot integrity mismatch: " + item.value("path", std::string{}));
        }
        ++verified;
        totalBytes += fileBytes.size();
    }
    const auto actual = snapshotInventory(snapshot);
    if (actual.value("fileCount", 0) != manifest.value("fileCount", 0) ||
        actual.value("totalBytes", uint64_t{0}) != manifest.value("totalBytes", uint64_t{0}) ||
        actual.value("files", json::array()) != manifest["files"]) {
        throw std::runtime_error("Snapshot inventory does not match its manifest");
    }
    return {
        {"ok", true}, {"snapshot", toUtf8(snapshot.wstring())},
        {"verifiedFiles", verified}, {"verifiedBytes", totalBytes},
        {"createdAt", manifest.value("createdAt", static_cast<uint64_t>(0))},
        {"reason", manifest.value("reason", std::string{})}
    };
}

static fs::path latestPortableSnapshot(const fs::path& dataRoot) {
    const auto root = dataRoot / L"backups" / L"snapshots";
    if (!fs::is_directory(root)) throw std::runtime_error("No portable backup snapshot exists");
    std::vector<fs::path> candidates;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && fs::is_regular_file(entry.path() / L"snapshot-manifest.json")) {
            candidates.push_back(entry.path());
        }
        ec.clear();
    }
    if (candidates.empty()) throw std::runtime_error("No portable backup snapshot exists");
    std::sort(candidates.begin(), candidates.end(), [](const fs::path& left, const fs::path& right) {
        return left.filename().wstring() > right.filename().wstring();
    });
    return candidates.front();
}

static json restorePortableBackupSnapshot(const fs::path& dataRoot, const fs::path& snapshot) {
    SteamShortcutWriterMutex writer;
    DataTransactionMutex transaction;
    const auto verified = verifyPortableBackupSnapshot(snapshot);
    const auto preRestore = createPortableBackupSnapshot(dataRoot, L"pre-restore", "automatic-pre-restore-safety-copy");
    const auto token = std::to_wstring(unixTimeMs());
    const auto staging = dataRoot / L"backups" / (L"restore-staging-" + token);
    const auto rollback = dataRoot / L"backups" / (L"restore-rollback-" + token);
    fs::create_directories(staging);
    fs::create_directories(rollback);
    try {
        for (const auto& name : portableSnapshotSections()) {
            const auto source = snapshot / name;
            if (!fs::is_directory(source)) continue;
            std::error_code ec;
            fs::copy(source, staging / name,
                fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) throw std::runtime_error("Cannot stage portable backup restore");
        }
        const auto stagedInventory = snapshotInventory(staging);
        const auto sourceManifestBytes = readBinaryFile(snapshot / L"snapshot-manifest.json", 16u << 20);
        const auto sourceManifest = json::parse(sourceManifestBytes.begin(), sourceManifestBytes.end());
        if (stagedInventory["fileCount"] != sourceManifest["fileCount"] ||
            stagedInventory["totalBytes"] != sourceManifest["totalBytes"] ||
            stagedInventory["files"] != sourceManifest["files"]) {
            throw std::runtime_error("Staged restore failed snapshot integrity verification");
        }
    } catch (...) {
        std::error_code cleanup;
        fs::remove_all(staging, cleanup);
        cleanup.clear();
        fs::remove_all(rollback, cleanup);
        throw;
    }

    std::vector<std::wstring> activated;
    try {
        for (const auto& name : portableSnapshotSections()) {
            const auto target = dataRoot / name;
            const auto staged = staging / name;
            const auto old = rollback / name;
            if (!pathWithin(target, dataRoot) || !pathWithin(staged, dataRoot) || !pathWithin(old, dataRoot)) {
                throw std::runtime_error("Restore target escaped Custom Steam Library data root");
            }
            std::error_code ec;
            if (fs::exists(target, ec)) fs::rename(target, old, ec);
            if (ec) throw std::runtime_error("Cannot move current data into restore rollback area");
            activated.push_back(name);
            ec.clear();
            if (fs::exists(staged, ec)) fs::rename(staged, target, ec);
            else fs::create_directories(target, ec);
            if (ec) throw std::runtime_error("Cannot activate staged restore data");
        }
    } catch (...) {
        bool rollbackComplete = true;
        for (auto iterator = activated.rbegin(); iterator != activated.rend(); ++iterator) {
            const auto target = dataRoot / *iterator;
            const auto old = rollback / *iterator;
            std::error_code ec;
            if (fs::exists(target, ec)) fs::remove_all(target, ec);
            if (ec) { rollbackComplete = false; continue; }
            if (fs::exists(old, ec)) fs::rename(old, target, ec);
            if (ec) rollbackComplete = false;
        }
        if (rollbackComplete) {
            std::error_code cleanup;
            fs::remove_all(staging, cleanup);
            cleanup.clear();
            fs::remove_all(rollback, cleanup);
        }
        // If rollback failed, preserve staging, rollback and preRestore for
        // explicit repair; never destroy the remaining original directories.
        throw;
    }
    std::error_code cleanupError;
    fs::remove_all(staging, cleanupError);
    cleanupError.clear();
    fs::remove_all(rollback, cleanupError);
    writeJsonAtomic(dataRoot / L"state" / L"last-restore.json", {
        {"schemaVersion", 1}, {"restoredAt", unixTimeMs()},
        {"snapshot", toUtf8(snapshot.wstring())},
        {"preRestoreSnapshot", toUtf8(preRestore.wstring())},
        {"verifiedFiles", verified.value("verifiedFiles", 0)},
        {"transactionalDirectorySwap", true}
    });
    return {
        {"ok", true}, {"snapshot", toUtf8(snapshot.wstring())},
        {"preRestoreSnapshot", toUtf8(preRestore.wstring())},
        {"verifiedFiles", verified.value("verifiedFiles", 0)},
        {"transactionalDirectorySwap", true}
    };
}

static json defaultNetworkRetryQueue() {
    return {
        {"schemaVersion", 1},
        {"mode", "event-wakeup-no-polling"},
        {"jobs", json::object()},
        {"history", json::array()}
    };
}

static fs::path networkRetryQueuePath(const fs::path& dataRoot) {
    return dataRoot / L"state" / L"network-retry-queue.json";
}

static json loadNetworkRetryQueue(const fs::path& dataRoot) {
    const auto path = networkRetryQueuePath(dataRoot);
    auto normalize = [](json queue) {
        if (!queue.is_object()) throw std::runtime_error("root is not an object");
        if (!queue.contains("jobs")) queue["jobs"] = json::object();
        else if (!queue["jobs"].is_object()) throw std::runtime_error("jobs is not an object");
        if (!queue.contains("history")) queue["history"] = json::array();
        else if (!queue["history"].is_array()) throw std::runtime_error("history is not an array");
        json quarantined = json::object();
        for (auto it = queue["jobs"].begin(); it != queue["jobs"].end();) {
            const auto& job = it.value();
            bool valid = job.is_object();
            if (valid) {
                for (const auto* field : {"status", "executable", "id"}) if (job.contains(field) && !job[field].is_string()) valid = false;
                for (const auto* field : {"nextAttemptAt", "createdAt", "updatedAt", "consecutiveFailures"}) {
                    if (!job.contains(field)) continue;
                    if (!job[field].is_number_integer() || (job[field].is_number_integer() && !job[field].is_number_unsigned() && job[field].get<int64_t>() < 0)) valid = false;
                }
                if (job.contains("consecutiveFailures") && job["consecutiveFailures"].is_number_integer() && job["consecutiveFailures"] > 1000000) valid = false;
                if (job.contains("executableIdentity") && !job["executableIdentity"].is_object()) valid = false;
                if (job.contains("commandArgs")) {
                    if (!job["commandArgs"].is_array()) valid = false;
                    else for (const auto& argument : job["commandArgs"]) if (!argument.is_string()) valid = false;
                }
            }
            if (!valid) { quarantined[it.key()] = {{"reason", "invalid-job-shape-or-fields"}, {"value", job}}; it = queue["jobs"].erase(it); }
            else ++it;
        }
        if (!quarantined.empty()) queue["quarantinedJobs"] = std::move(quarantined);
        queue["schemaVersion"] = 1;
        queue["mode"] = "event-wakeup-no-polling";
        return queue;
    };
    std::vector<fs::path> candidates;
    if (fs::is_regular_file(path)) candidates.push_back(path);
    const auto backups = stateBackupCandidates(dataRoot, path, "network-retry-queue");
    candidates.insert(candidates.end(), backups.begin(), backups.end());
    std::optional<fs::path> corruptTarget;
    for (const auto& candidate : candidates) {
        try {
            const auto bytes = readBinaryFile(candidate, 8u << 20);
            auto queue = normalize(json::parse(bytes.begin(), bytes.end()));
            if (candidate != path) {
                try {
                    writeJsonWithBackup(
                        path, dataRoot / L"backups" / L"state", "network-retry-queue", queue);
                } catch (...) {
                    // Keep this event usable even if repair cannot be written.
                }
            }
            return queue;
        } catch (...) {
            if (candidate == path && !corruptTarget) {
                corruptTarget = preserveCorruptStateFile(dataRoot, path, "network-retry-queue");
            }
            continue;
        }
    }
    if (fs::is_regular_file(path)) {
        auto queue = defaultNetworkRetryQueue();
        if (corruptTarget) queue["recoveredFromCorruptQueue"] = toUtf8(corruptTarget->wstring());
        writeJsonWithBackup(path, dataRoot / L"backups" / L"state", "network-retry-queue", queue);
        return queue;
    }
    return defaultNetworkRetryQueue();
}

static json executableIdentity(const fs::path& executable) {
    std::error_code ec;
    const auto size = fs::is_regular_file(executable, ec) ? fs::file_size(executable, ec) : 0;
    ec.clear();
    const auto modified = fs::is_regular_file(executable, ec)
        ? fs::last_write_time(executable, ec).time_since_epoch().count() : 0;
    return {
        {"canonicalPath", canonicalPathKey(executable)},
        {"size", size},
        {"lastWriteTicks", modified}
    };
}

static std::string networkRetryJobId(
    const fs::path& executable,
    const std::optional<uint32_t>& shortcutId,
    const std::string& platformHint) {
    const auto material = canonicalPathKey(executable) + "|" +
        (shortcutId ? std::to_string(*shortcutId) : std::string{"none"}) + "|" + platformHint;
    return sha256(std::vector<unsigned char>(material.begin(), material.end()));
}

static json enqueueNetworkRetry(
    const fs::path& dataRoot,
    const std::string& jobId,
    const fs::path& executable,
    const json& commandArgs,
    const json& failure) {
    // This is a cross-process read/modify/write transaction. Multiple
    // detached workers can finish different games at the same time; an
    // atomic file replace alone would still let the later writer erase the
    // earlier job.
    DataTransactionMutex transaction;
    auto queue = loadNetworkRetryQueue(dataRoot);
    auto& job = queue["jobs"][jobId];
    const auto now = unixTimeMs();
    const int failures = job.is_object() ? (std::min)(1000000, job.value("consecutiveFailures", 0) + 1) : 1;
    const uint64_t delayMs = static_cast<uint64_t>((std::min)(15 * 60, 15 * (1 << (std::min)(failures - 1, 6)))) * 1000ull;
    const auto createdAt = job.is_object() ? job.value("createdAt", now) : now;
    job = {
        {"id", jobId}, {"status", "waiting-network"},
        {"wakeMode", "event-driven-no-polling"},
        {"executable", toUtf8(executable.wstring())},
        {"executableIdentity", executableIdentity(executable)},
        {"commandArgs", commandArgs},
        {"createdAt", createdAt}, {"updatedAt", now},
        {"lastFailureAt", now}, {"nextAttemptAt", now + delayMs},
        {"consecutiveFailures", failures},
        {"lastFailure", failure},
        {"automaticWakeReasons", {"app-start", "steam-launch", "library-page-open", "network-restored"}}
    };
    writeJsonWithBackup(
        networkRetryQueuePath(dataRoot), dataRoot / L"backups" / L"state", "network-retry-queue", queue);
    return job;
}

static void appendLifecycleEvent(const fs::path& dataRoot, json event) {
    // Lifecycle events are an append-only audit trail shared by detached
    // workers. Serialize the read/append/write cycle so simultaneous scans,
    // retries, and manual actions cannot erase each other's evidence.
    DataTransactionMutex transaction;
    const auto path = dataRoot / L"state" / L"lifecycle-events.json";
    json value = {{"schemaVersion", 1}, {"events", json::array()}};
    if (fs::is_regular_file(path)) {
        try {
            const auto bytes = readBinaryFile(path, 16u << 20);
            value = json::parse(bytes.begin(), bytes.end());
            if (!value.is_object() || !value.contains("events") || !value["events"].is_array()) {
                value = {{"schemaVersion", 1}, {"events", json::array()}};
            }
        } catch (...) {
            const auto corrupt = dataRoot / L"backups" / L"corrupt" /
                (L"lifecycle-events-" + std::to_wstring(unixTimeMs()) + L".json");
            fs::create_directories(corrupt.parent_path());
            std::error_code ec;
            fs::copy_file(path, corrupt, fs::copy_options::overwrite_existing, ec);
            value = {{"schemaVersion", 1}, {"events", json::array()},
                     {"recoveredFromCorruptLog", toUtf8(corrupt.wstring())}};
        }
    }
    event["at"] = unixTimeMs();
    value["events"].push_back(std::move(event));
    while (value["events"].size() > 500) value["events"].erase(value["events"].begin());
    writeJsonAtomic(path, value);
}

static std::string jsonStringOr(const json& value, const char* key, std::string fallback = {}) {
    const auto iterator = value.find(key);
    return iterator != value.end() && iterator->is_string() ? iterator->get<std::string>() : fallback;
}

// nlohmann::json::value<T> throws when a key is present with JSON null. Scan
// records intentionally use null for fields that do not apply to a non-game
// or unresolved item, so plan generation must treat null numeric evidence as
// the neutral value instead of aborting the whole UI command.
static bool jsonBoolSafe(const json& value, const char* key, bool fallback = false) {
    const auto iterator = value.find(key);
    return iterator != value.end() && iterator->is_boolean() ? iterator->get<bool>() : fallback;
}

static double jsonDoubleOr(const json& value, const char* key, double fallback = 0.0) {
    const auto iterator = value.find(key);
    if (iterator == value.end() || iterator->is_null()) return fallback;
    if (iterator->is_number()) return iterator->get<double>();
    if (iterator->is_string()) {
        try { return std::stod(iterator->get<std::string>()); } catch (...) {}
    }
    return fallback;
}

static json inventoryRecoveryFiles(const fs::path& recoveryRoot) {
    json files = json::array();
    uint64_t totalBytes = 0;
    for (const auto& section : {L"games", L"artwork", L"cache"}) {
        const auto root = recoveryRoot / section;
        if (!fs::is_directory(root)) continue;
        std::error_code ec;
        for (fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, ec), end;
             iterator != end; iterator.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!iterator->is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            const auto relative = fs::relative(iterator->path(), recoveryRoot, ec);
            if (ec || !safeSnapshotRelativePath(relative)) {
                throw std::runtime_error("Cannot inventory deleted-game recovery data");
            }
            const auto bytes = readBinaryFile(iterator->path(), 128u << 20);
            totalBytes += bytes.size();
            files.push_back({
                {"path", toUtf8(relative.generic_wstring())},
                {"bytes", bytes.size()},
                {"sha256", sha256(bytes)}
            });
        }
    }
    std::sort(files.begin(), files.end(), [](const json& left, const json& right) {
        return left.value("path", std::string{}) < right.value("path", std::string{});
    });
    return {{"files", files}, {"fileCount", files.size()}, {"totalBytes", totalBytes}};
}

static void verifyRecoveryFiles(const fs::path& recoveryRoot, const json& manifest) {
    if (!manifest.contains("files") || !manifest["files"].is_array()) {
        throw std::runtime_error("Deleted-game recovery manifest has no integrity inventory");
    }
    for (const auto& item : manifest["files"]) {
        const auto relative = fs::path(toWide(item.value("path", std::string{})));
        if (!safeSnapshotRelativePath(relative)) throw std::runtime_error("Unsafe deleted-game recovery path");
        const auto file = recoveryRoot / relative;
        if (!pathWithin(file, recoveryRoot) || !fs::is_regular_file(file)) {
            throw std::runtime_error("Deleted-game recovery file is missing");
        }
        const auto bytes = readBinaryFile(file, 128u << 20);
        if (bytes.size() != item.value("bytes", static_cast<size_t>(0)) ||
            sha256(bytes) != item.value("sha256", std::string{})) {
            throw std::runtime_error("Deleted-game recovery integrity mismatch");
        }
    }
}

static fs::path createDeletedGameRecoveryPackage(
    const fs::path& dataRoot,
    const fs::path& gameDirectory,
    const json& config,
    const json& overrides) {
    const auto id = gameDataId(gameDirectory);
    auto stamp = unixTimeMs();
    fs::path recovery;
    do {
        recovery = dataRoot / L"backups" / L"deleted-games" /
            (std::to_wstring(stamp++) + L"-" + toWide(id.substr(0, 12)));
    } while (fs::exists(recovery));
    fs::create_directories(recovery);

    for (const auto& section : {L"games", L"artwork", L"cache"}) {
        const auto source = dataRoot / section / toWide(id);
        if (!fs::is_directory(source)) continue;
        fs::create_directories(recovery);
        std::error_code ec;
        fs::copy(source, recovery / section,
            fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            const auto detail = ec.message() + " while copying " + toUtf8(source.wstring()) +
                " to " + toUtf8((recovery / section).wstring());
            std::error_code cleanup;
            fs::remove_all(recovery, cleanup);
            throw std::runtime_error("Cannot create deleted-game recovery package: " + detail);
        }
    }

    const auto key = canonicalPathKey(gameDirectory);
    json manualDirectory = nullptr;
    for (const auto& item : config.value("manualGameDirectories", json::array())) {
        if (item.is_object() && configuredDirectory(json::array({item}), gameDirectory)) {
            manualDirectory = item;
            break;
        }
    }
    json manualPrimary = nullptr;
    if (config.contains("manualPrimary") && config["manualPrimary"].is_object()) {
        const auto item = config["manualPrimary"].find(key);
        if (item != config["manualPrimary"].end()) manualPrimary = *item;
    }
    json matchingOverrides = json::object();
    if (overrides.contains("items") && overrides["items"].is_object()) {
        for (const auto& [overrideKey, value] : overrides["items"].items()) {
            const auto overridePath = fs::path(toWide(overrideKey));
            if (pathWithin(overridePath, gameDirectory)) matchingOverrides[overrideKey] = value;
        }
    }
    const auto inventory = inventoryRecoveryFiles(recovery);
    writeJsonAtomic(recovery / L"recovery-manifest.json", {
        {"schemaVersion", 1}, {"featureName", CUSTOM_STEAM_LIBRARY_FEATURE_NAME},
        {"gameDirectory", toUtf8(gameDirectory.wstring())}, {"gameDataId", id},
        {"createdAt", unixTimeMs()}, {"manualGameDirectory", manualDirectory},
        {"manualPrimary", manualPrimary},
        {"manualOverrides", matchingOverrides},
        {"fileCount", inventory["fileCount"]}, {"totalBytes", inventory["totalBytes"]},
        {"files", inventory["files"]}, {"gameInstallFilesIncluded", false}
    });
    return recovery;
}

static fs::path latestDeletedGameRecoveryPackage(const fs::path& dataRoot, const fs::path& gameDirectory) {
    const auto root = dataRoot / L"backups" / L"deleted-games";
    std::vector<fs::path> candidates;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) throw std::runtime_error("No deleted-game recovery package exists");
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec) || fs::is_regular_file(entry.path() / L"recovery-state.json")) continue;
        const auto manifestPath = entry.path() / L"recovery-manifest.json";
        if (!fs::is_regular_file(manifestPath)) continue;
        try {
            const auto bytes = readBinaryFile(manifestPath, 16u << 20);
            const auto manifest = json::parse(bytes.begin(), bytes.end());
            if (canonicalPathKey(fs::path(toWide(manifest.value("gameDirectory", std::string{})))) ==
                canonicalPathKey(gameDirectory)) candidates.push_back(entry.path());
        } catch (...) {
            continue;
        }
    }
    if (candidates.empty()) throw std::runtime_error("No unused deleted-game recovery package matches this directory");
    std::sort(candidates.begin(), candidates.end(), [](const fs::path& left, const fs::path& right) {
        return left.filename().wstring() > right.filename().wstring();
    });
    return candidates.front();
}

static uintmax_t mergeRecoveryDirectory(const fs::path& source, const fs::path& target, const fs::path& dataRoot) {
    if (!fs::is_directory(source)) return 0;
    if (!pathWithin(target, dataRoot)) throw std::runtime_error("Recovery target escaped Custom Steam Library data root");
    fs::create_directories(target);
    uintmax_t copied = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator iterator(source, fs::directory_options::skip_permission_denied, ec), end;
         iterator != end; iterator.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto relative = fs::relative(iterator->path(), source, ec);
        if (ec || !safeSnapshotRelativePath(relative)) throw std::runtime_error("Unsafe recovery merge path");
        const auto destination = target / relative;
        if (iterator->is_directory(ec)) {
            fs::create_directories(destination, ec);
        } else if (iterator->is_regular_file(ec) && !fs::exists(destination)) {
            fs::create_directories(destination.parent_path());
            fs::copy_file(iterator->path(), destination, fs::copy_options::none, ec);
            if (!ec) ++copied;
        }
        if (ec) throw std::runtime_error("Cannot merge deleted-game recovery data");
    }
    return copied;
}

static std::wstring quoteWindowsCommandArgument(const std::wstring& value) {
    if (value.empty()) return L"\"\"";
    if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        if (ch == L'\"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
        } else {
            quoted.append(slashes, L'\\');
            quoted.push_back(ch);
        }
        slashes = 0;
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

static DWORD runLabChildProcess(
    const fs::path& labExecutable,
    const std::vector<std::wstring>& arguments,
    DWORD timeoutMs = 7u * 60u * 1000u + 30u * 1000u) {
    std::wstring command = quoteWindowsCommandArgument(labExecutable.wstring());
    for (const auto& argument : arguments) {
        command.push_back(L' ');
        command += quoteWindowsCommandArgument(argument);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(labExecutable.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
        nullptr, nullptr, &startup, &process)) {
        throw std::runtime_error("Cannot launch isolated retry worker: " + winHttpError("CreateProcess", GetLastError()));
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        // Do not leave the retry worker holding the queue or its inherited
        // handles.  The bounded wait also prevents the wake command itself
        // from becoming an unbounded blocker when the child is wedged.
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hProcess);
        return ERROR_TIMEOUT;
    }
    if (wait != WAIT_OBJECT_0) {
        const auto error = GetLastError();
        TerminateProcess(process.hProcess, ERROR_CANCELLED);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hProcess);
        throw std::runtime_error("Cannot wait for isolated retry worker: " +
            winHttpError("WaitForSingleObject", error));
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    return exitCode;
}

static std::string scanLabelKey(const std::string& value) {
    return normalizeTitle(value);
}

static constexpr const char* PID_COORDINATION_CONTRACT = "yeman-game-target-arbiter-v1";

static std::optional<fs::path> environmentPath(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed <= 1) return std::nullopt;
    std::wstring value(needed, L'\0');
    if (GetEnvironmentVariableW(name, value.data(), needed) == 0) return std::nullopt;
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    if (value.empty()) return std::nullopt;
    return fs::absolute(fs::path(value));
}

static fs::path gameWhitelistPath() {
    if (const auto configured = environmentPath(L"YEMAN_GAME_WHITELIST_PATH")) return *configured;
    return fs::path(L"C:\\SOFT\\YeMan\\PowerControl\\Sleep\\game-whitelist.txt");
}

static fs::path playerBlacklistPath() {
    if (const auto configured = environmentPath(L"YEMAN_PLAYER_BLACKLIST_PATH")) return *configured;
    return fs::path(L"C:\\SOFT\\YeMan\\PowerControl\\Sleep\\player-blacklist.txt");
}

static std::wstring trimWide(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t ch) { return std::iswspace(ch) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) { return std::iswspace(ch) != 0; }).base();
    if (first >= last) return {};
    return std::wstring(first, last);
}

static std::string normalizeGameRule(const std::string& raw) {
    auto value = trimWide(toWide(raw));
    if (value.empty()) return {};
    const auto slash = value.find_last_of(L"\\/");
    if (slash != std::wstring::npos) value = value.substr(slash + 1);
    value = unicodeLower(trimWide(std::move(value)));
    if (value.size() > 4 && value.compare(value.size() - 4, 4, L".exe") == 0) value.resize(value.size() - 4);
    return toUtf8(value);
}

static std::string normalizedExecutableRule(const fs::path& executable) {
    return normalizeGameRule(toUtf8(executable.filename().wstring()));
}

static bool definitePidBlacklistRole(const std::string& role) {
    return role == "modifier" || role == "mod-manager" || role == "crash-handler" || role == "installer" ||
        role == "helper" || role == "updater";
}

static std::vector<std::string> readGameRuleFile(const fs::path& path) {
    std::vector<std::string> rules;
    if (!fs::is_regular_file(path)) return rules;
    const auto bytes = readBinaryFile(path, 1u << 20);
    std::istringstream input(std::string(bytes.begin(), bytes.end()));
    std::string line;
    while (std::getline(input, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        const auto rule = normalizeGameRule(line);
        if (!rule.empty() && std::find(rules.begin(), rules.end(), rule) == rules.end()) rules.push_back(rule);
    }
    return rules;
}

static std::vector<unsigned char> gameRuleFileBytes(const std::vector<std::string>& rules) {
    std::string content;
    for (const auto& rule : rules) content += rule + "\n";
    return std::vector<unsigned char>(content.begin(), content.end());
}

static void writeGameRuleFileAtomic(const fs::path& target, const std::vector<unsigned char>& data) {
    static std::atomic<uint64_t> sequence{1};
    fs::create_directories(target.parent_path());
    const auto temp = target.wstring() + L".sal." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(sequence.fetch_add(1)) + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create PID rule staging file");
    bool writeOk = true;
    size_t offset = 0;
    while (offset < data.size()) {
        const DWORD chunk = static_cast<DWORD>((std::min)(data.size() - offset, static_cast<size_t>(1u << 20)));
        DWORD written = 0;
        if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) || written != chunk) {
            writeOk = false;
            break;
        }
        offset += written;
    }
    if (writeOk && !FlushFileBuffers(file)) writeOk = false;
    if (!CloseHandle(file)) writeOk = false;
    if (!writeOk) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("PID rule staging write failed");
    }
    DWORD lastError = ERROR_SUCCESS;
    for (int attempt = 1; attempt <= 6; ++attempt) {
        if (MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        lastError = GetLastError();
        if (lastError != ERROR_SHARING_VIOLATION && lastError != ERROR_ACCESS_DENIED && lastError != ERROR_LOCK_VIOLATION) break;
        Sleep(static_cast<DWORD>(40 * attempt));
    }
    DeleteFileW(temp.c_str());
    throw std::runtime_error("PID rule atomic activation failed: " + std::to_string(lastError));
}

static json stringVectorJson(const std::vector<std::string>& values) {
    json out = json::array();
    for (const auto& value : values) out.push_back(value);
    return out;
}

static std::string classifyLibraryIdentityText(const std::string& identity) {
    const auto lowered = asciiLower(identity);
    const auto unicodeIdentity = normalizeTitle(identity);
    auto has = [&](std::initializer_list<const char*> needles) {
        for (const auto* needle : needles) {
            const auto normalizedNeedle = normalizeTitle(needle);
            if (lowered.find(asciiLower(needle)) != std::string::npos ||
                (!normalizedNeedle.empty() && unicodeIdentity.find(normalizedNeedle) != std::string::npos)) return true;
        }
        return false;
    };
    if (has({"trainer", "fling", "cheatengine", "cheat engine", "修改器",
             "トレーナー", "チート", "трейнер"})) return "modifier";
    // These are management frontends, not the game itself.  Keep them in the
    // unresolved/tool lane so a directory containing only a mod manager never
    // becomes a false Steam game or PID target.
    if (has({"mentalomegaclient", "mental omega", "modmanager", "mod manager",
             "modorganizer", "mod organizer", "nexusmodmanager", "nexus mod manager",
             "nmm.exe", "vortex", "r2modman", "thunderstore", "overwolf",
             "modlauncher", "mod launcher", "cncnet", "syringe", "cncmaps", "finalalert",
             "map renderer", "map editor", "exeproxy"})) return "mod-manager";
    // Support clients shipped next to a real game client. They must never
    // become the primary PID/Steam target.
    if (has({"battleye", "beservice", "aces_be", "bpreport",
             "助手客户端", "游戏助手", "assistant client", "assistantclient",
             "companion app", "companion client"})) return "helper";
    // Runtime and crash-report binaries often carry a plausible-looking
    // ProductName, but they are never the game target.
    if (has({"crashrpt", "crashsender", "crash report", "crash reporter", "crash report delivery"})) return "crash-handler";
    if (has({"java(tm)", "java platform", "java control panel", "openjdk", "azul zulu", "zulu platform",
             "platform x64 architecture", "java web start", "web start launcher", "jre", "jvm"})) return "helper";
    if (has({"python", "python core", "python launcher", "tcl/tk", "perl", "ruby", "node.js", "nodejs", "mono", "dotnet",
             "7-zip", "7zip", "elevate application", "elevate"})) return "helper";
    if (has({"reshade"})) return "installer";
    if (has({"cncnet", "syringe"})) return "mod-manager";
    if (has({"gaijin_downloader", "gjagent", "gaijin.net updater"})) return "updater";
    if (has({"crashreport", "crash_report", "crashpad", "unitycrash", "bugreport", "werfault"})) return "crash-handler";
    if (has({"unins", "uninstall", "installer", "setup", "installshield", "vcredist", "dxsetup", "directx"})) return "installer";
    if (has({"dedicatedserver", "dedicated_server", "server.exe", "server_x64", "server64"})) return "server";
    if (has({"benchmark", "editor.exe", "map editor", "campaign editor", "font tool", "modtools", "mod_tools", "sdk.exe"})) return "tool";
    if (has({"updater", "update.exe", "patcher", "repair"})) return "updater";
    if (has({"launcher", "dowser", "bootstrap", "startgame", "gamecenter", "smartsteamloader", "steamloader"})) return "launcher";
    if (has({"helper", "cefprocess", "webview", "diagnostic", "config.exe", "service.exe",
             "foregroundwindow", "elevate application", "elevate", "7-zip", "7zip"})) return "helper";
    return "main-game";
}

static std::string classifyLibraryExecutable(const fs::path& executable) {
    return classifyLibraryIdentityText(
        toUtf8(executable.filename().wstring()) + " " + toUtf8(executable.wstring()));
}

// PE string tables are useful identity evidence, but they are not uniformly
// authored.  Some vendors leave placeholders such as "Application" or put a
// launcher/tool name in ProductName.  Keep the raw version table for audit,
// while using a small, Unicode-safe cleanup/validation layer for selection.
static std::string cleanPeIdentityValue(const std::string& raw) {
    auto value = collapseSpaces(raw);
    if (value.empty()) return {};

    // A few build systems accidentally include the executable suffix in the
    // version string.  It is not part of the title evidence.
    const auto lowered = asciiLower(value);
    for (const auto* suffix : {".exe", ".ex"}) {
        const std::string marker = suffix;
        if (lowered.size() > marker.size() && lowered.ends_with(marker)) {
            value.resize(value.size() - marker.size());
            value = collapseSpaces(value);
            break;
        }
    }
    return value;
}

static bool genericPeIdentityValue(const std::string& value) {
    const auto key = normalizeTitle(value);
    if (key.empty() || key.size() < 3) return true;
    static const std::unordered_set<std::string> generic = {
        "app", "application", "client", "game", "launcher", "main", "program",
        "product", "productname", "filedescription", "unknown", "untitled", "test",
        "sample", "demo", "setup", "installer", "update", "updater", "service",
        "helper", "server", "runtime", "engine", "window", "windows"
    };
    if (generic.count(key)) return true;

    // Runtime/vendor labels should not turn a system component into a game.
    static const std::array<const char*, 18> vendorMarkers = {
        "microsoft windows", "windows operating system", "visual c++", "directx",
        "unreal engine", "unity", "electron", "chromium", "cef",
        "steam client", "steamworks", "epic games launcher", "gog galaxy",
        "ea app", "ubisoft connect", "nvidia", "amd", "vulkan"
    };
    const auto lowered = asciiLower(value);
    for (const auto* marker : vendorMarkers) {
        if (lowered.find(marker) != std::string::npos) return true;
    }
    static const std::array<const char*, 41> runtimeMarkers = {
        "java(tm)", "java platform", "java control panel", "openjdk", "azul zulu", "zulu platform",
        "platform x64 architecture", "jre", "jvm", "web start",
        "crashrpt", "crash report", "crashsender", "reshade", "cncnet", "syringe",
        "cncmaps", "finalalert", "map renderer", "map editor", "exeproxy",
        "python", "tcl/tk", "perl", "ruby", "node.js", "nodejs", "mono", "dotnet",
        "foregroundwindow", "elevate application", "elevate", "7-zip", "7zip", "campaign editor", "font tool",
        "redistributable", "paradox launcher", "todo:",
        "product name", "file description"
    };
    for (const auto* marker : runtimeMarkers) {
        if (lowered.find(marker) != std::string::npos) return true;
    }
    return false;
}

static bool usablePeGameIdentity(const std::string& value) {
    return !value.empty() && !genericPeIdentityValue(value) && normalizeTitle(value).size() >= 3;
}

struct PeIdentityAssessment {
    std::string productName;
    std::string fileDescription;
    std::string primaryEvidenceSource;
    std::string identityConfidenceBand = "low";
    double identityConfidence = 0.0;
    std::string role = "main-game";
    bool productNameTool = false;
    bool fileDescriptionTool = false;
};

static bool strongPeToolPathRole(const std::string& role) {
    return role == "modifier" || role == "mod-manager" || role == "crash-handler" ||
        role == "installer" || role == "helper" || role == "updater" || role == "launcher" ||
        role == "tool" || role == "server";
}

// A launcher may legitimately expose the game's ProductName. The other roles
// are unambiguously auxiliary binaries and must never be promoted by matching
// metadata (for example Cyberpunk's CrashReporter.exe).
static bool productNameMayOverridePathRole(const std::string& role) {
    return role == "main-game" || role == "launcher";
}

static PeIdentityAssessment assessPeIdentity(
    const std::map<std::string, std::string>& version,
    const std::string& pathRole = "main-game",
    const std::string& directoryName = {}) {
    PeIdentityAssessment assessment;
    auto findClean = [&](const char* key) {
        const auto found = version.find(key);
        return found == version.end() ? std::string{} : cleanPeIdentityValue(found->second);
    };
    assessment.productName = findClean("ProductName");
    assessment.fileDescription = findClean("FileDescription");

    const auto productRole = assessment.productName.empty()
        ? std::string{"main-game"} : classifyLibraryIdentityText(assessment.productName);
    const auto descriptionRole = assessment.fileDescription.empty()
        ? std::string{"main-game"} : classifyLibraryIdentityText(assessment.fileDescription);
    assessment.productNameTool = usablePeGameIdentity(assessment.productName) && productRole != "main-game";
    assessment.fileDescriptionTool = usablePeGameIdentity(assessment.fileDescription) && descriptionRole != "main-game";
    const bool productAgreesWithDirectory = !directoryName.empty() &&
        usablePeGameIdentity(assessment.productName) && scoreTitle(directoryName, assessment.productName) >= 0.78;
    const bool pathIsStrongTool = strongPeToolPathRole(pathRole);

    // An explicit tool/manager ProductName is a hard exclusion.  This keeps a
    // directory such as "Mental Omega" or "NMM" from becoming a game merely
    // because its parent directory looks game-like.
    if (assessment.productNameTool) {
        assessment.role = productRole;
    } else if (usablePeGameIdentity(assessment.productName) &&
               productNameMayOverridePathRole(pathRole) &&
               (!pathIsStrongTool || productAgreesWithDirectory) && !assessment.fileDescriptionTool) {
        // ProductName is the strongest local identity signal.  It also wins
        // over a misleading filename role such as client.exe/launcher.exe.
        assessment.role = "main-game";
        assessment.primaryEvidenceSource = "pe-product-name";
        assessment.identityConfidence = 0.98;
    } else if (assessment.fileDescriptionTool) {
        assessment.role = descriptionRole;
    } else if (usablePeGameIdentity(assessment.fileDescription)) {
        assessment.role = "main-game";
        assessment.primaryEvidenceSource = "pe-file-description";
        assessment.identityConfidence = 0.94;
    } else {
        // Fall back to all other PE fields for tool detection.  Do not treat a
        // generic ProductName as a game identity, but still honour e.g. a
        // CompanyName containing "Fling Trainer".
        std::string metadata;
        for (const auto& [key, value] : version) metadata += " " + cleanPeIdentityValue(value);
        const auto metadataRole = classifyLibraryIdentityText(metadata);
        assessment.role = metadataRole != "main-game" ? metadataRole : pathRole;
    }

    if (assessment.primaryEvidenceSource.empty()) {
        assessment.primaryEvidenceSource = assessment.role == "main-game" ? "none" : "pe-tool-classification";
    }
    if (assessment.identityConfidence >= 0.90) assessment.identityConfidenceBand = "high";
    else if (assessment.identityConfidence >= 0.80) assessment.identityConfidenceBand = "medium";
    else if (assessment.identityConfidence > 0.0) assessment.identityConfidenceBand = "low";
    return assessment;
}

static std::optional<std::string> knownNonGameDirectoryKind(const fs::path& directory);

static json runIdentityEvidenceSelfTest() {
    json cases = json::array();
    bool allPassed = true;
    auto addCase = [&](const std::string& name, bool passed, const json& detail = json::object()) {
        allPassed = allPassed && passed;
        cases.push_back({{"name", name}, {"passed", passed}, {"detail", detail}});
    };

    const double ck3Score = scoreInstalledTitle("CK3", "Crusader Kings III");
    const double hoi4Score = scoreInstalledTitle("HOI4", "Hearts of Iron IV");
    addCase("acronym-with-roman-number-ck3", ck3Score >= 0.94, {{"score", ck3Score}});
    addCase("acronym-with-roman-number-hoi4", hoi4Score >= 0.94, {{"score", hoi4Score}});
    const double genericAcronymScore = scoreTitle("ABC", "Alpha Beta Charlie");
    addCase("generic-acronym-cannot-drive-network-match", genericAcronymScore < 0.90,
        {{"score", genericAcronymScore}});
    const double threeKingdomsExact = scoreTitle("Total War: THREE KINGDOMS", "Total War: THREE KINGDOMS");
    const double threeKingdomsWrongSequel = scoreTitle("Total War: THREE KINGDOMS", "Total War: THREE KINGDOMS 21");
    addCase("exact-pe-title-outranks-numbered-lookalike",
        threeKingdomsExact >= 0.99 && threeKingdomsWrongSequel <= 0.72,
        {{"exact", threeKingdomsExact}, {"numberedLookalike", threeKingdomsWrongSequel}});
    addCase("trusted-exact-tier-outranks-lower-tier-score",
        !preferSteamCandidateEvidence(350, 0.995, true, 500, 0.985, true) &&
         preferSteamCandidateEvidence(500, 0.985, true, 350, 0.995, true) &&
         preferSteamCandidateEvidence(500, 0.990, true, 500, 0.985, true),
        {{"peTier", 500}, {"directoryTier", 350},
         {"peScore", 0.985}, {"directoryScore", 0.995}});
    const double ishinWrongOriginal = scoreTitle("維新の嵐 幕末志士伝", "維新の嵐");
    addCase("sequel-subtitle-does-not-collapse-to-original",
        ishinWrongOriginal < 0.90, {{"score", ishinWrongOriginal}, {"rejectedAppIdFixture", 545010}});

    std::string key;
    const auto plain = parseLocalSteamAppIds(L"steam_appid.txt", "1158310\r\n", key);
    addCase("plain-steam-appid", plain.size() == 1 && plain.front() == 1158310, {{"key", key}});
    key.clear();
    const auto smart = parseLocalSteamAppIds(
        L"SmartSteamEmu.ini", "[Launcher]\r\nTarget=wuxia.exe\r\nAppId = 377530\r\n", key);
    addCase("named-appid-key", smart.size() == 1 && smart.front() == 377530, {{"key", key}});
    key.clear();
    const auto provider = parseLocalSteamAppIds(
        L"tenoke.ini", "[TENOKE]\r\n# appid\r\nid = 1336980 # title\r\n", key);
    addCase("provider-section-id", provider.size() == 1 && provider.front() == 1336980, {{"key", key}});

    const std::vector<NameRound> ck3Rounds = {{1, "parent-directory-1", "CK3", {}}};
    const std::vector<LocalSteamAppIdEvidence> ck3Evidence = {
        {1158310, L"C:\\Game\\CK3\\binaries\\steam_appid.txt", "plain-steam-appid", 1.0, {}, {}, false},
        {1158310, L"C:\\Game\\CK3\\binaries\\steam_emu.ini", "named-appid-key", 0.99, {}, {}, false},
        {1158310, L"C:\\Game\\CK3\\launcher\\launcher-settings.json", "launcher-settings-steam-url", 0.995,
            "Crusader Kings III", "../binaries/ck3.exe", true}
    };
    const auto ck3Assessment = assessLocalSteamAppIdIdentity(
        1158310, ck3Evidence, L"C:\\Game\\CK3\\binaries\\ck3.exe", ck3Rounds, "Crusader Kings III");
    addCase("launcher-title-and-repeated-appid-are-corroborated", ck3Assessment.value("accepted", false), ck3Assessment);

    const std::vector<NameRound> wuxiaRounds = {{1, "exe-stem", "wuxia", {}}};
    const std::vector<LocalSteamAppIdEvidence> wuxiaEvidence = {
        {377530, L"C:\\Game\\xkfyz\\SmartSteamEmu.ini", "named-appid-key", 0.99,
            {}, "wuxia.exe", true}
    };
    const auto wuxiaAssessment = assessLocalSteamAppIdIdentity(
        377530, wuxiaEvidence, L"C:\\Game\\xkfyz\\wuxia.exe", wuxiaRounds, "Tale of Wuxia");
    addCase("single-appid-needs-target-and-name-corroboration", wuxiaAssessment.value("accepted", false), wuxiaAssessment);

    const std::vector<NameRound> nobuRounds = {{1, "exe-stem", "NOBU16", {}}};
    const std::vector<LocalSteamAppIdEvidence> nobuEvidence = {
        {1336980, L"C:\\Game\\NOBU16\\tenoke.ini", "provider-section-id", 0.97,
            "NOBUNAGA'S AMBITION: Awakening", {}, false}
    };
    const auto nobuAssessment = assessLocalSteamAppIdIdentity(
        1336980, nobuEvidence, L"C:\\Game\\NOBU16\\NOBU16.exe", nobuRounds,
        "NOBUNAGA'S AMBITION: Awakening");
    addCase("single-provider-id-needs-inline-title-corroboration",
        nobuAssessment.value("accepted", false), nobuAssessment);

    const fs::path steinsExe = L"C:/Game/STEINS.GATE.RE.BOOT-P2P/sgre_steam.exe";
    const auto steinsRounds = buildNameRounds(steinsExe, {}, true, {}, {});
    const std::vector<LocalSteamAppIdEvidence> steinsEvidence = {
        {4012810, steinsExe.parent_path()/L"steam_settings"/L"steam_appid.txt", "plain-steam-appid", 1.0, {}, {}, false}
    };
    const auto steinsAssessment = assessLocalSteamAppIdIdentity(4012810, steinsEvidence, steinsExe, steinsRounds, "STEINS;GATE RE:BOOT");
    addCase("installed-p2p-suffix-keeps-official-local-appid", steinsAssessment.value("accepted", false), steinsAssessment);
    addCase("distribution-cleaning-keeps-editions-and-sequels", installedDirectoryTitle("Game 2 Deluxe-P2P") == "Game 2 Deluxe" && installedDirectoryTitle("GameP2P") == "GameP2P");
    addCase("cleaned-title-does-not-accept-wrong-game", !assessLocalSteamAppIdIdentity(4012810, steinsEvidence, steinsExe, steinsRounds, "Unrelated Adventure").value("accepted", true));
    auto steinsConflict = steinsEvidence;
    steinsConflict.push_back({77, steinsExe.parent_path()/L"steam_emu.ini", "named-appid-key", 0.99, {}, {}, false});
    addCase("cleaned-title-cannot-bypass-conflicting-local-appids", !assessLocalSteamAppIdIdentity(4012810, steinsConflict, steinsExe, steinsRounds, "STEINS;GATE RE:BOOT").value("accepted", true));

    const std::vector<LocalSteamAppIdEvidence> weakEvidence = {
        {999999, L"C:\\Game\\Unknown\\steam_appid.txt", "plain-steam-appid", 1.0, {}, {}, false}
    };
    const std::vector<NameRound> weakRounds = {{1, "exe-stem", "unknown", {}}};
    const auto weakAssessment = assessLocalSteamAppIdIdentity(
        999999, weakEvidence, L"C:\\Game\\Unknown\\unknown.exe", weakRounds, "Different Formal Game");
    addCase("single-appid-without-corroboration-is-rejected", !weakAssessment.value("accepted", true), weakAssessment);
    auto repeatedWeakEvidence = weakEvidence;
    repeatedWeakEvidence.push_back({999999, L"C:\\Game\\Unknown\\steam_emu.ini",
        "named-appid-key", 0.99, {}, {}, false});
    const auto repeatedWeakAssessment = assessLocalSteamAppIdIdentity(
        999999, repeatedWeakEvidence, L"C:\\Game\\Unknown\\unknown.exe",
        weakRounds, "Different Formal Game");
    addCase("repeated-stale-configs-without-name-corroboration-are-rejected",
        !repeatedWeakAssessment.value("accepted", true), repeatedWeakAssessment);

    auto conflictEvidence = ck3Evidence;
    conflictEvidence.push_back({1091500, L"C:\\Game\\CK3\\other.ini", "named-appid-key", 0.99, {}, {}, false});
    addCase("multiple-local-appids-conflict", !unambiguousLocalSteamAppId(conflictEvidence).has_value());
    addCase("assistant-client-is-non-game-helper",
        classifyLibraryExecutable(L"C:\\Game\\tool\\枫织梦境助手客户端.exe") == "helper");
    addCase("pe-only-helper-identity-is-not-main-game",
        classifyLibraryIdentityText("client.exe ProductName=枫织梦境助手客户端 FileDescription=游戏助手") == "helper");
    addCase("pe-only-mod-manager-identity-is-not-main-game",
        classifyLibraryIdentityText("client.exe ProductName=Mental Omega Client FileDescription=Mod Manager") == "mod-manager");

    const auto shogunPe = assessPeIdentity({
        {"ProductName", " Total War: SHOGUN 2.exe "},
        {"FileDescription", "Total War: SHOGUN 2"}
    }, "launcher", "Total War - SHOGUN 2");
    addCase("product-name-formal-title-overrides-misleading-path-role",
        shogunPe.role == "main-game" && shogunPe.productName == "Total War: SHOGUN 2" &&
            shogunPe.primaryEvidenceSource == "pe-product-name" && shogunPe.identityConfidenceBand == "high",
        {{"role", shogunPe.role}, {"productName", shogunPe.productName},
         {"source", shogunPe.primaryEvidenceSource}, {"band", shogunPe.identityConfidenceBand}});

    const auto warThunderPe = assessPeIdentity({
        {"ProductName", "War Thunder"}, {"FileDescription", "aces.exe"}
    }, "main-game");
    addCase("product-name-recognizes-war-thunder-aces-binary",
        warThunderPe.role == "main-game" && warThunderPe.primaryEvidenceSource == "pe-product-name" &&
            warThunderPe.identityConfidence >= 0.95,
        {{"source", warThunderPe.primaryEvidenceSource}, {"confidence", warThunderPe.identityConfidence}});

    const auto mentalOmegaPe = assessPeIdentity({
        {"ProductName", "Mental Omega Client"}, {"FileDescription", "Mod Manager"}
    }, "main-game");
    addCase("product-name-tool-remains-non-game",
        mentalOmegaPe.role == "mod-manager" && mentalOmegaPe.primaryEvidenceSource == "pe-tool-classification",
        {{"role", mentalOmegaPe.role}, {"source", mentalOmegaPe.primaryEvidenceSource}});

    const auto descriptionOnlyPe = assessPeIdentity({
        {"ProductName", "Application"}, {"FileDescription", "Hearts of Iron IV"}
    }, "helper");
    addCase("file-description-fallback-is-game-identity",
        descriptionOnlyPe.role == "main-game" && descriptionOnlyPe.primaryEvidenceSource == "pe-file-description" &&
            descriptionOnlyPe.identityConfidenceBand == "high",
        {{"role", descriptionOnlyPe.role}, {"source", descriptionOnlyPe.primaryEvidenceSource}});

    const auto multilingualPe = assessPeIdentity({
        {"ProductName", "战争雷霆"}, {"FileDescription", "ウォーサンダー"}
    });
    addCase("multilingual-pe-identity-is-preserved",
        multilingualPe.productName == "战争雷霆" && multilingualPe.fileDescription == "ウォーサンダー" &&
            multilingualPe.primaryEvidenceSource == "pe-product-name",
        {{"productName", multilingualPe.productName}, {"fileDescription", multilingualPe.fileDescription}});

    const auto reshadePe = assessPeIdentity({
        {"ProductName", "ReShade"}, {"FileDescription", "ReShade Setup"}
    }, "installer", "WARNO NORTHAG");
    addCase("reshade-setup-is-not-a-game",
        reshadePe.role == "installer" && reshadePe.primaryEvidenceSource == "pe-tool-classification",
        {{"role", reshadePe.role}, {"source", reshadePe.primaryEvidenceSource}});

    const auto javaPe = assessPeIdentity({
        {"ProductName", "Java(TM) Platform SE 7 U79"}, {"FileDescription", "Java(TM) Web Start Launcher"}
    }, "main-game", "Starsector");
    addCase("java-runtime-is-not-a-game",
        javaPe.role == "helper" && javaPe.primaryEvidenceSource == "pe-tool-classification",
        {{"role", javaPe.role}, {"source", javaPe.primaryEvidenceSource}});

    const auto crashRptPe = assessPeIdentity({
        {"ProductName", "CrashRpt"}, {"FileDescription", "Crash Report Delivery Module"}
    }, "main-game", "WARNO NORTHAG");
    addCase("crash-report-module-is-not-a-game",
        crashRptPe.role == "crash-handler" && crashRptPe.primaryEvidenceSource == "pe-tool-classification",
        {{"role", crashRptPe.role}, {"source", crashRptPe.primaryEvidenceSource}});

    const auto cyberpunkCrashReporterPe = assessPeIdentity({
        {"ProductName", "Cyberpunk 2077"},
        {"FileDescription", "Cyberpunk 2077 Crash Reporter"}
    }, "crash-handler", "Cyberpunk 2077");
    addCase("matching-product-name-cannot-promote-crash-reporter",
        cyberpunkCrashReporterPe.role == "crash-handler" &&
            cyberpunkCrashReporterPe.primaryEvidenceSource == "pe-tool-classification",
        {{"role", cyberpunkCrashReporterPe.role}, {"source", cyberpunkCrashReporterPe.primaryEvidenceSource}});

    addCase("mental-omega-directory-is-explicit-non-game",
        knownNonGameDirectoryKind(L"D:\\Game\\Mental Omega") == std::optional<std::string>("mod-manager"));
    addCase("mod-manager-directories-are-explicit-non-game",
        knownNonGameDirectoryKind(L"D:\\Tools\\NMM") == std::optional<std::string>("mod-manager") &&
            knownNonGameDirectoryKind(L"D:\\Tools\\r2modman") == std::optional<std::string>("mod-manager"));

    wchar_t tempPath[MAX_PATH]{};
    const DWORD tempChars = GetTempPathW(MAX_PATH, tempPath);
    if (tempChars > 0 && tempChars < MAX_PATH) {
        const auto fixtureRoot = fs::path(tempPath) /
            (L"SteamArtworkLab-identity-fixture-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code cleanupError;
        fs::remove_all(fixtureRoot, cleanupError);
        try {
            const auto executable = fixtureRoot / L"CK3" / L"binaries" / L"ck3.exe";
            const auto launcher = fixtureRoot / L"CK3" / L"launcher" / L"launcher-settings.json";
            fs::create_directories(executable.parent_path());
            fs::create_directories(launcher.parent_path());
            std::ofstream(executable, std::ios::binary).put('\0');
            std::ofstream(executable.parent_path() / L"steam_appid.txt", std::ios::binary) << "1158310\n";
            std::ofstream(executable.parent_path() / L"steam_emu.ini", std::ios::binary) << "AppId=1158310\n";
            std::ofstream(launcher, std::ios::binary) <<
                R"({"displayName":"Crusader Kings III","exePath":"../binaries/ck3.exe","browserDlcUrl":"https://store.steampowered.com/dlc/1158310/","browserModUrl":"https://steamcommunity.com/app/1158310/workshop/"})";
            const auto discovered = discoverLocalSteamAppIds(executable);
            const auto priorityOnly = discoverLocalSteamAppIds(executable, 0);
            const auto discoveredId = unambiguousLocalSteamAppId(discovered);
            const auto priorityOnlyId = unambiguousLocalSteamAppId(priorityOnly);
            const bool launcherFound = std::any_of(discovered.begin(), discovered.end(), [](const auto& item) {
                return item.key == "launcher-settings-steam-url" && item.titleHint == "Crusader Kings III" &&
                    item.targetMatchesExecutable;
            });
            addCase("end-to-end-launcher-settings-discovery",
                discoveredId && *discoveredId == 1158310 && discovered.size() == 3 && launcherFound,
                {{"sourceCount", discovered.size()},
                 {"selectedAppId", discoveredId ? json(*discoveredId) : json(nullptr)},
                 {"launcherFound", launcherFound}});
            addCase("priority-identity-files-bypass-traversal-budget",
                priorityOnlyId && *priorityOnlyId == 1158310 && priorityOnly.size() == 2,
                {{"traversalBudget", 0}, {"sourceCount", priorityOnly.size()},
                 {"selectedAppId", priorityOnlyId ? json(*priorityOnlyId) : json(nullptr)}});
        } catch (const std::exception& error) {
            addCase("end-to-end-launcher-settings-discovery", false, {{"error", error.what()}});
            addCase("priority-identity-files-bypass-traversal-budget", false, {{"error", error.what()}});
        }
        fs::remove_all(fixtureRoot, cleanupError);
    } else {
        addCase("end-to-end-launcher-settings-discovery", false, {{"error", "GetTempPathW failed"}});
        addCase("priority-identity-files-bypass-traversal-budget", false, {{"error", "GetTempPathW failed"}});
    }

    return {{"resolverVersion", STEAM_RESOLVER_VERSION}, {"allPassed", allPassed}, {"cases", cases}};
}

static bool excludedLibrarySubdirectory(const fs::path& directory) {
    if (custom_steam_library::builtinExcludedSteamToolPath(toUtf8(directory.wstring()))) return true;
    const auto key = scanLabelKey(toUtf8(directory.filename().wstring()));
    static const std::unordered_set<std::string> excluded = {
        "commonredist", "redist", "redistributables", "directx", "vcredist", "support",
        "prerequisites", "prereqs", "dotnet", "easyanticheat", "battleye", "crashdumps",
        "logs", "cache", "shadercache"
    };
    return excluded.count(key) != 0;
}

// A Steam library's `steamapps\common` folder can be configured as a scan
// root accidentally.  Directory-name matching alone is not sufficient here:
// a manually copied game may also live below a folder named `common`. Require
// a matching Steam appmanifest installdir before classifying a directory as a
// native Steam installation, so non-Steam games remain eligible.
static bool steamAppManifestMatchesInstallDirectory(
    const fs::path& manifestPath,
    const fs::path& gameDirectory) {
    try {
        const auto bytes = readBinaryFile(manifestPath, 2u << 20);
        const std::string text(bytes.begin(), bytes.end());
        const auto wanted = asciiLower(trim(toUtf8(gameDirectory.filename().wstring())));
        if (wanted.empty()) return false;
        const std::string marker = "\"installdir\"";
        size_t cursor = 0;
        while ((cursor = text.find(marker, cursor)) != std::string::npos) {
            const auto valueStart = text.find('"', cursor + marker.size());
            if (valueStart == std::string::npos) break;
            const auto valueEnd = text.find('"', valueStart + 1);
            if (valueEnd == std::string::npos) break;
            if (asciiLower(trim(text.substr(valueStart + 1, valueEnd - valueStart - 1))) == wanted) {
                return true;
            }
            cursor = valueEnd + 1;
        }
    } catch (...) {
        // A damaged appmanifest must not block the rest of the library scan.
    }
    return false;
}

static bool steamAppManifestExists(const fs::path& steamAppsDirectory) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(
             steamAppsDirectory, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        const auto name = asciiLower(toUtf8(entry.path().filename().wstring()));
        if (name.starts_with("appmanifest_") &&
            asciiLower(toUtf8(entry.path().extension().wstring())) == ".acf") return true;
    }
    return false;
}

struct SteamNativeGameInfo {
    int appId = 0;
    fs::path manifestPath;
    std::string manifestName;
};

static int steamAppIdFromManifestPath(const fs::path& manifestPath) {
    const auto name = asciiLower(toUtf8(manifestPath.stem().wstring()));
    constexpr std::string_view prefix = "appmanifest_";
    if (!name.starts_with(prefix)) return 0;
    const auto value = name.substr(prefix.size());
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    })) return 0;
    try {
        const auto parsed = std::stoll(value);
        return parsed > 0 && parsed <= (std::numeric_limits<int>::max)()
            ? static_cast<int>(parsed) : 0;
    } catch (...) {
        return 0;
    }
}

static std::string steamVdfScalar(const std::string& text, const std::string& key) {
    const auto marker = "\"" + key + "\"";
    size_t cursor = 0;
    while ((cursor = text.find(marker, cursor)) != std::string::npos) {
        const auto valueStart = text.find('"', cursor + marker.size());
        if (valueStart == std::string::npos) break;
        const auto valueEnd = text.find('"', valueStart + 1);
        if (valueEnd == std::string::npos) break;
        std::string value = text.substr(valueStart + 1, valueEnd - valueStart - 1);
        std::string unescaped;
        unescaped.reserve(value.size());
        bool escaped = false;
        for (const char ch : value) {
            if (escaped) {
                unescaped.push_back(ch);
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else {
                unescaped.push_back(ch);
            }
        }
        if (escaped) unescaped.push_back('\\');
        return unescaped;
    }
    return {};
}

static std::optional<SteamNativeGameInfo> steamNativeGameInfoForDirectory(const fs::path& directory) {
    std::error_code ec;
    if (directory.empty()) return std::nullopt;
    const auto absolute = fs::weakly_canonical(directory, ec);
    const auto normalized = ec ? fs::absolute(directory) : absolute;
    const auto common = normalized.parent_path();
    const auto steamApps = common.parent_path();
    if (asciiLower(toUtf8(common.filename().wstring())) != "common" ||
        asciiLower(toUtf8(steamApps.filename().wstring())) != "steamapps") return std::nullopt;
    if (!steamAppManifestExists(steamApps)) return std::nullopt;
    for (const auto& entry : fs::directory_iterator(
             steamApps, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        const auto name = asciiLower(toUtf8(entry.path().filename().wstring()));
        if (!name.starts_with("appmanifest_") ||
            asciiLower(toUtf8(entry.path().extension().wstring())) != ".acf") continue;
        if (!steamAppManifestMatchesInstallDirectory(entry.path(), normalized)) continue;
        const auto bytes = readBinaryFile(entry.path(), 2u << 20);
        const std::string text(bytes.begin(), bytes.end());
        int appId = steamAppIdFromManifestPath(entry.path());
        if (appId <= 0) {
            try {
                const auto raw = steamVdfScalar(text, "appid");
                if (!raw.empty()) appId = std::stoi(raw);
            } catch (...) { appId = 0; }
        }
        return SteamNativeGameInfo{appId, entry.path(), steamVdfScalar(text, "name")};
    }
    return std::nullopt;
}

static bool steamNativeGameDirectory(const fs::path& directory) {
    return steamNativeGameInfoForDirectory(directory).has_value();
}

static std::optional<std::string> platformLibraryKind(const fs::path& directory) {
    const auto key = scanLabelKey(toUtf8(directory.filename().wstring()));
    static const std::map<std::string, std::string> names = {
        {"steam", "steam"}, {"steamlibrary", "steam"}, {"steamapps", "steam"},
        {"epicgames", "epic"}, {"epicgameslauncher", "epic"},
        {"wegame", "wegame"}, {"tencent", "wegame"}, {"qqgame", "wegame"},
        {"goggames", "gog"}, {"goggalaxy", "gog"},
        {"ea", "ea"}, {"eaapp", "ea"}, {"origin", "ea"},
        {"ubisoft", "ubisoft"}, {"ubisoftconnect", "ubisoft"}, {"ubisoftgamelauncher", "ubisoft"},
        {"battlenet", "battle-net"}, {"riotgames", "riot"},
        {"xboxgames", "xbox"}, {"windowsapps", "xbox"},
        {"rockstargames", "rockstar"}, {"rockstargameslauncher", "rockstar"},
        {"bsglauncher", "battlestate-launcher"}
    };
    if (const auto found = names.find(key); found != names.end()) return found->second;
    std::error_code ec;
    if (fs::is_directory(directory / L"steamapps", ec) && fs::is_directory(directory / L"userdata", ec)) return "steam";
    ec.clear();
    if (fs::is_directory(directory / L"QQGameTempest", ec)) return "wegame";
    ec.clear();
    if (fs::is_regular_file(directory / L"GalaxyClient.exe", ec)) return "gog";
    return std::nullopt;
}

static std::vector<fs::path> steamLibraryRootsForNativeScan(const std::vector<fs::path>& configuredRoots) {
    std::vector<fs::path> candidates;
    auto relatedToConfiguredRoot = [&](const fs::path& value) {
        return std::any_of(configuredRoots.begin(), configuredRoots.end(), [&](const auto& configured) {
            return pathWithin(value, configured) || pathWithin(configured, value);
        });
    };
    auto addCandidate = [&](const fs::path& value, bool discoveredFromRelevantSteamRoot = false) {
        if (value.empty()) return;
        std::error_code error;
        const auto normalized = fs::weakly_canonical(value, error);
        const auto resolved = error ? fs::absolute(value) : normalized;
        if (!fs::is_directory(resolved, error)) return;
        // A scan configured for an unrelated game collection must not pull
        // every installed Steam title into that collection.  Alternate
        // libraries are allowed only after their registered Steam root was
        // itself related to a configured scan root.
        if (!discoveredFromRelevantSteamRoot && !relatedToConfiguredRoot(resolved)) return;
        if (std::none_of(candidates.begin(), candidates.end(), [&](const auto& item) {
            return canonicalPathKey(item) == canonicalPathKey(resolved);
        })) candidates.push_back(resolved);
    };
    if (const auto detected = steamInstallPath()) addCandidate(*detected);
    for (const auto& configured : configuredRoots) {
        const auto key = asciiLower(toUtf8(configured.filename().wstring()));
        if (key == "common" &&
            asciiLower(toUtf8(configured.parent_path().filename().wstring())) == "steamapps") {
            addCandidate(configured.parent_path().parent_path());
        } else if (key == "steamapps") {
            addCandidate(configured.parent_path());
        } else if (platformLibraryKind(configured) == std::optional<std::string>("steam")) {
            addCandidate(configured);
        }
    }

    for (size_t index = 0; index < candidates.size(); ++index) {
        const auto libraryRoot = candidates[index];
        const auto foldersFile = libraryRoot / L"steamapps" / L"libraryfolders.vdf";
        if (!fs::is_regular_file(foldersFile)) continue;
        try {
            const auto bytes = readBinaryFile(foldersFile, 8u << 20);
            const std::string text(bytes.begin(), bytes.end());
            static const std::regex pathExpression(R"VDF("path"\s*"([^"]+)")VDF",
                std::regex_constants::icase);
            for (std::sregex_iterator it(text.begin(), text.end(), pathExpression), end; it != end; ++it) {
                std::string raw = (*it)[1].str();
                std::string unescaped;
                unescaped.reserve(raw.size());
                bool escaped = false;
                for (const char ch : raw) {
                    if (escaped) {
                        unescaped.push_back(ch);
                        escaped = false;
                    } else if (ch == '\\') {
                        escaped = true;
                    } else {
                        unescaped.push_back(ch);
                    }
                }
                if (escaped) unescaped.push_back('\\');
                addCandidate(fs::path(toWide(unescaped)), true);
            }
        } catch (...) {
            // A damaged libraryfolders.vdf must not hide the detected Steam root.
        }
    }
    return candidates;
}

static bool explicitCollectionDirectory(const fs::path& directory) {
    const auto key = scanLabelKey(toUtf8(directory.filename().wstring()));
    static const std::unordered_set<std::string> names = {
        "emu", "emulator", "emulators", "romgames", "gamecollection", "gamescollection", "模拟器"
    };
    return names.count(key) != 0;
}

// These folders are known mod/front-end/runtime homes rather than a single
// game installation.  Keep them visible in the scan as explicit non-game
// records (so the user can still inspect or manually override them), but never
// let a PE ProductName inside promote one of their helper binaries to the
// automatic Steam target.
static std::optional<std::string> knownNonGameDirectoryKind(const fs::path& directory) {
    if (custom_steam_library::builtinExcludedSteamToolPath(toUtf8(directory.wstring())))
        return "steam-common-redistributables";
    const auto key = scanLabelKey(toUtf8(directory.filename().wstring()));
    static const std::map<std::string, std::string> names = {
        // Common utility/download roots are not game installations.  A
        // random EXE below one of these folders must never promote the whole
        // folder into the automatic game library.
        {"gamecheatsmanager", "tool"},
        {"virtualgamepad", "tool"},
        {"游戏下载", "tool"},
        {"风灵月影游戏修改器", "modifier"},
        {"mentalomega", "mod-manager"},
        {"mentalomegaclient", "mod-manager"},
        {"reshade", "tool"},
        {"reshadesetup", "tool"},
        {"nmm", "mod-manager"},
        {"nexusmodmanager", "mod-manager"},
        {"modorganizer", "mod-manager"},
        {"vortex", "mod-manager"},
        {"r2modman", "mod-manager"}
    };
    if (const auto found = names.find(key); found != names.end()) return found->second;

    // Keep this list targeted: these are directory-level utilities which
    // routinely contain helper EXEs with game-like product names.  Matching
    // the normalized basename also covers localized suffixes/versions such
    // as "Game Cheats Manager 2" without blacklisting a real game called
    // "Game" or "Games".
    if (key.find("gamecheatsmanager") != std::string::npos ||
        key.find("virtualgamepad") != std::string::npos ||
        key.find("游戏下载") != std::string::npos ||
        key.find("风灵月影") != std::string::npos ||
        key.find("修改器") != std::string::npos ||
        key.find("trainer") != std::string::npos ||
        key.find("cheatengine") != std::string::npos) {
        return key.find("修改器") != std::string::npos ||
                key.find("风灵月影") != std::string::npos ||
                key.find("trainer") != std::string::npos
            ? std::optional<std::string>("modifier")
            : std::optional<std::string>("tool");
    }
    return std::nullopt;
}

struct LibraryExeCandidate {
    fs::path path;
    std::string role;
    uintmax_t size = 0;
    int depth = 0;
    int score = 0;
    std::vector<std::string> reasons;
    std::map<std::string, std::string> version;
    std::string productName;
    std::string fileDescription;
    std::string primaryEvidenceSource;
    std::string identityConfidenceBand = "low";
    double identityConfidence = 0.0;
};

static json libraryPathEvidence(const fs::path& gameDirectory, const fs::path& executable, int depth) {
    const auto gameName = toUtf8(gameDirectory.filename().wstring());
    const auto stem = toUtf8(executable.stem().wstring());
    const auto gameKey = scanLabelKey(gameName);
    const auto stemKey = scanLabelKey(stem);
    const auto acronym = scanTitleAcronym(gameName);
    const bool exact = !gameKey.empty() && gameKey == stemKey;
    const bool acronymMatch = !acronym.empty() && acronym == stemKey;
    const bool overlap = !exact && !acronymMatch && gameKey.size() >= 4 && stemKey.size() >= 4 &&
        (gameKey.find(stemKey) != std::string::npos || stemKey.find(gameKey) != std::string::npos);
    const double confidence = exact ? 0.99 : (acronymMatch ? 0.96 : (overlap ? 0.78 : (depth == 0 ? 0.56 : 0.42)));
    return {
        {"source", "directory-and-executable-path"},
        {"gameDirectoryName", gameName},
        {"executableStem", stem},
        {"normalizedDirectoryName", gameKey},
        {"normalizedExecutableStem", stemKey},
        {"directoryStemExact", exact},
        {"directoryAcronym", acronym},
        {"directoryAcronymMatch", acronymMatch},
        {"directoryStemOverlap", overlap},
        {"depth", depth},
        {"confidence", confidence},
        {"confidenceBand", confidence >= 0.90 ? "high" : (confidence >= 0.70 ? "medium" : "low")},
        {"isIdentityHintOnly", true},
        {"requiresProviderVerification", true}
    };
}

static LibraryExeCandidate scoreLibraryExecutable(const fs::path& gameDirectory, const fs::path& executable, int depth) {
    LibraryExeCandidate item;
    item.path = executable;
    item.depth = depth;
    item.role = classifyLibraryExecutable(executable);
    std::error_code ec;
    item.size = fs::file_size(executable, ec);
    item.score = 20;
    item.reasons.push_back("executable-file");

    const auto gameName = toUtf8(gameDirectory.filename().wstring());
    const auto stem = toUtf8(executable.stem().wstring());
    const auto gameKey = scanLabelKey(gameName);
    const auto stemKey = scanLabelKey(stem);
    const auto gameAcronym = scanTitleAcronym(gameName);
    if (!gameKey.empty() && gameKey == stemKey) {
        item.score += 70;
        item.reasons.push_back("exe-name-equals-game-directory");
    } else if (!gameAcronym.empty() && gameAcronym == stemKey) {
        item.score += 60;
        item.reasons.push_back("exe-name-equals-directory-acronym");
    } else if (gameKey.size() >= 4 && stemKey.size() >= 4 &&
        (gameKey.find(stemKey) != std::string::npos || stemKey.find(gameKey) != std::string::npos)) {
        item.score += 28;
        item.reasons.push_back("exe-name-overlaps-game-directory");
    }

    if (depth == 0) {
        item.score += 18;
        item.reasons.push_back("game-directory-root");
    } else {
        const auto relative = asciiLower(toUtf8(executable.lexically_relative(gameDirectory).wstring()));
        if (relative.find("\\binaries\\") != std::string::npos || relative.starts_with("binaries\\") ||
            relative.find("\\bin\\") != std::string::npos || relative.starts_with("bin\\") ||
            relative.find("\\win64\\") != std::string::npos || relative.find("\\x64\\") != std::string::npos) {
            item.score += 12;
            item.reasons.push_back("common-game-binary-directory");
        }
    }

    if (item.size >= 100u * 1024u * 1024u) {
        item.score += 15;
        item.reasons.push_back("large-executable-100mb");
    } else if (item.size >= 20u * 1024u * 1024u) {
        item.score += 12;
        item.reasons.push_back("large-executable-20mb");
    } else if (item.size >= 5u * 1024u * 1024u) {
        item.score += 5;
        item.reasons.push_back("medium-executable");
    } else if (item.size < 512u * 1024u) {
        item.score -= 12;
        item.reasons.push_back("small-executable-penalty");
    }

    item.version = versionStrings(executable);
    const auto peAssessment = assessPeIdentity(item.version, item.role, gameName);
    item.productName = peAssessment.productName;
    item.fileDescription = peAssessment.fileDescription;
    item.primaryEvidenceSource = peAssessment.primaryEvidenceSource;
    item.identityConfidenceBand = peAssessment.identityConfidenceBand;
    item.identityConfidence = peAssessment.identityConfidence;
    // A valid ProductName/FileDescription is allowed to repair a misleading
    // path role (for example client.exe or launcher.exe).  Explicit tool
    // metadata still wins and remains ineligible for the game target.
    if (peAssessment.role != "main-game" ||
        peAssessment.primaryEvidenceSource == "pe-product-name" ||
        peAssessment.primaryEvidenceSource == "pe-file-description") {
        if (item.role != peAssessment.role) item.reasons.push_back("pe-identity-role-overrides-path:" + peAssessment.role);
        item.role = peAssessment.role;
    }
    if (peAssessment.primaryEvidenceSource == "pe-product-name") {
        // ProductName is the strongest local identity evidence.  The fixed
        // boost intentionally outranks a coincidental directory/EXE match in
        // a sibling helper or launcher.
        item.score += 105;
        item.reasons.push_back("pe-product-name-game-identity");
    } else if (peAssessment.primaryEvidenceSource == "pe-file-description") {
        item.score += 88;
        item.reasons.push_back("pe-file-description-game-identity");
    }
    if (peAssessment.productNameTool) item.reasons.push_back("pe-product-name-tool-exclusion");
    if (peAssessment.fileDescriptionTool) item.reasons.push_back("pe-file-description-tool-exclusion");

    static const std::map<std::string, int> roleAdjustment = {
        {"main-game", 25}, {"launcher", -35}, {"mod-manager", -110}, {"helper", -70}, {"updater", -80},
        {"tool", -75}, {"server", -85}, {"modifier", -95},
        {"installer", -135}, {"crash-handler", -150}
    };
    if (const auto found = roleAdjustment.find(item.role); found != roleAdjustment.end()) {
        item.score += found->second;
        item.reasons.push_back("role:" + item.role);
    }
    if (stemKey == "game" || stemKey == "client" || stemKey == "application") {
        item.score -= 8;
        item.reasons.push_back("generic-executable-name");
    }

    for (const auto& key : {"ProductName", "FileDescription", "InternalName", "OriginalFilename"}) {
        const auto found = item.version.find(key);
        if (found == item.version.end() || found->second.empty()) continue;
        const auto evidenceValue = cleanPeIdentityValue(found->second);
        const auto evidenceKey = scanLabelKey(evidenceValue);
        if (!gameKey.empty() && evidenceKey == gameKey) {
            item.score += 50;
            item.reasons.push_back(std::string("pe-") + asciiLower(key) + "-equals-directory");
            break;
        }
        if (!gameAcronym.empty() && evidenceKey == gameAcronym) {
            item.score += 40;
            item.reasons.push_back(std::string("pe-") + asciiLower(key) + "-equals-directory-acronym");
            break;
        }
    }
    // Edition/DLC folders commonly retain the canonical base-game name in PE
    // metadata. Treat that as strong local evidence while leaving the final
    // identity decision to Steam/IGDB verification.
    const auto baseDirectoryName = stripAddonSuffix(gameName);
    if (!baseDirectoryName.empty()) {
        for (const auto& key : {"ProductName", "FileDescription", "InternalName", "OriginalFilename"}) {
            const auto found = item.version.find(key);
            if (found == item.version.end() || found->second.empty()) continue;
            if (scoreTitle(baseDirectoryName, cleanPeIdentityValue(found->second)) >= 0.78) {
                item.score += 38;
                item.reasons.push_back(std::string("pe-") + asciiLower(key) + "-matches-base-edition");
                break;
            }
        }
    }

    // When PE metadata is absent or generic, expose the path evidence as the
    // fallback identity source.  It remains a provider-verification hint, but
    // the confidence band is now explicit for the UI and add-plan logic.
    if (item.primaryEvidenceSource == "none" || item.primaryEvidenceSource.empty()) {
        const auto pathEvidence = libraryPathEvidence(gameDirectory, executable, depth);
        item.primaryEvidenceSource = pathEvidence.value("source", std::string{"directory-name"});
        item.identityConfidence = pathEvidence.value("confidence", 0.0);
        item.identityConfidenceBand = pathEvidence.value("confidenceBand", std::string{"low"});
    }

    // War Thunder has several equivalent binaries. Prefer the normal win64
    // client deterministically, then keep min-cpu/win32 as lower-ranked
    // alternates instead of forcing manual confirmation.
    const auto relativeLower = asciiLower(toUtf8(executable.lexically_relative(gameDirectory).wstring()));
    const auto stemLower = asciiLower(toUtf8(executable.stem().wstring()));
    if (stemLower == "aces" && relativeLower == "win64\\aces.exe") {
        item.score += 8;
        item.reasons.push_back("preferred-win64-client");
    } else if (stemLower == "aces-min-cpu") {
        item.score -= 2;
        item.reasons.push_back("alternate-min-cpu-client");
    } else if (stemLower == "aces" && relativeLower == "win32\\aces.exe") {
        item.score -= 2;
        item.reasons.push_back("legacy-win32-client");
    }
    return item;
}

static json libraryCandidateJson(const LibraryExeCandidate& item, const fs::path& gameDirectory) {
    json version = json::object();
    for (const auto& [key, value] : item.version) version[key] = value;
    json tags = json::array({"consume-native-valve-only"});
    if (item.role == "main-game") tags.push_back("pid-primary-candidate");
    else tags.push_back("pid-target-ineligible");
    if (definitePidBlacklistRole(item.role)) tags.push_back("pid-blacklist-candidate");
    return {
        {"path", toUtf8(item.path.wstring())},
        {"relativePath", toUtf8(item.path.lexically_relative(gameDirectory).wstring())},
        {"role", item.role},
        {"score", item.score},
        {"size", item.size},
        {"depth", item.depth},
        {"reasons", item.reasons},
        {"productName", item.productName.empty() ? json(nullptr) : json(item.productName)},
        {"fileDescription", item.fileDescription.empty() ? json(nullptr) : json(item.fileDescription)},
        {"primaryEvidenceSource", item.primaryEvidenceSource},
        {"identityConfidence", item.identityConfidence},
        {"identityConfidenceBand", item.identityConfidenceBand},
        {"versionEvidence", version},
        {"pathEvidence", libraryPathEvidence(gameDirectory, item.path, item.depth)},
        {"pidCoordination", {
            {"contract", PID_COORDINATION_CONTRACT},
            {"normalizedExeRule", normalizedExecutableRule(item.path)},
            {"role", item.role},
            {"eligibleForGameTarget", false},
            {"canBecomeGameTargetIfSelected", item.role == "main-game"},
            {"whitelistSuggested", false},
            {"blacklistSuggested", definitePidBlacklistRole(item.role)},
            {"tags", std::move(tags)}
        }}
    };
}

struct TrainerRecord {
    std::string gameName;
    std::string origin;
    std::string version;
    fs::path path;
    fs::path folder;
};

static std::vector<TrainerRecord> scanTrainerRoots(const std::vector<fs::path>& roots) {
    std::vector<TrainerRecord> records;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& directory : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!directory.is_directory(ec)) continue;
            const auto infoPath = directory.path() / L"gcm_info.json";
            if (!fs::is_regular_file(infoPath, ec)) continue;
            try {
                const auto bytes = readBinaryFile(infoPath, 1u << 20);
                const auto info = json::parse(bytes.begin(), bytes.end());
                const auto gameName = trim(info.value("game_name", std::string{}));
                if (gameName.empty()) continue;
                std::vector<fs::path> files;
                for (fs::recursive_directory_iterator it(directory.path(), fs::directory_options::skip_permission_denied, ec), end;
                     it != end && files.size() < 100; it.increment(ec)) {
                    if (ec) { ec.clear(); continue; }
                    if (it.depth() >= 4 && it->is_directory(ec)) it.disable_recursion_pending();
                    if (!it->is_regular_file(ec)) continue;
                    const auto extension = asciiLower(toUtf8(it->path().extension().wstring()));
                    if (extension == ".exe" || extension == ".ct" || extension == ".cetrainer") files.push_back(it->path());
                }
                std::sort(files.begin(), files.end(), [](const fs::path& left, const fs::path& right) {
                    const bool leftExe = asciiLower(toUtf8(left.extension().wstring())) == ".exe";
                    const bool rightExe = asciiLower(toUtf8(right.extension().wstring())) == ".exe";
                    if (leftExe != rightExe) return leftExe;
                    return canonicalPathKey(left) < canonicalPathKey(right);
                });
                if (!files.empty()) records.push_back({
                    gameName, info.value("origin", std::string{}), info.value("version", std::string{}),
                    files.front(), directory.path()
                });
            } catch (...) {
                // A corrupt trainer folder must not stop the library scan.
            }
        }
    }
    return records;
}

static double trainerGameAgreement(
    const std::string& gameDirectoryName,
    const std::optional<LibraryExeCandidate>& selected,
    const std::string& trainerName) {
    double score = scoreTitle(gameDirectoryName, trainerName);
    const auto gameKey = scanLabelKey(gameDirectoryName);
    const auto trainerKey = scanLabelKey(trainerName);
    if (!gameKey.empty() && gameKey == trainerKey) score = 1.0;
    if (!gameKey.empty() && gameKey == scanTitleAcronym(trainerName)) score = (std::max)(score, 0.98);
    if (!trainerKey.empty() && trainerKey == scanTitleAcronym(gameDirectoryName)) score = (std::max)(score, 0.98);
    if (selected) {
        const auto stemKey = scanLabelKey(toUtf8(selected->path.stem().wstring()));
        if (!stemKey.empty() && stemKey == scanTitleAcronym(trainerName)) score = (std::max)(score, 0.98);
        for (const auto& [_, value] : selected->version) {
            if (scanLabelKey(value) == trainerKey) score = 1.0;
        }
    }
    return score;
}

static json scanOneGameDirectory(
    const fs::path& gameDirectory,
    int maxDepth,
    const json& config,
    const std::vector<TrainerRecord>& trainers) {
    std::vector<LibraryExeCandidate> candidates;
    size_t executableCount = 0;
    bool candidateLimitReached = false;
    std::error_code ec;
    bool scanComplete = true;
    fs::recursive_directory_iterator it(gameDirectory, fs::directory_options::none, ec), end;
    if (ec) { scanComplete = false; ec.clear(); }
    for (; it != end; it.increment(ec)) {
        if (ec) { scanComplete = false; ec.clear(); continue; }
        const bool isDirectory = it->is_directory(ec);
        if (ec) { scanComplete = false; ec.clear(); continue; }
        if (isDirectory) {
            if (it.depth() >= maxDepth || excludedLibrarySubdirectory(it->path())) it.disable_recursion_pending();
            continue;
        }
        const bool isRegularFile = it->is_regular_file(ec);
        if (ec) { scanComplete = false; ec.clear(); continue; }
        if (!isRegularFile) continue;
        if (asciiLower(toUtf8(it->path().extension().wstring())) != ".exe") continue;
        ++executableCount;
        if (candidates.size() >= 250) {
            candidateLimitReached = true;
            continue;
        }
        candidates.push_back(scoreLibraryExecutable(gameDirectory, it->path(), it.depth()));
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.size != right.size) return left.size > right.size;
        return canonicalPathKey(left.path) < canonicalPathKey(right.path);
    });

    const auto gameKey = canonicalPathKey(gameDirectory);
    std::optional<LibraryExeCandidate> selected;
    std::string status = "needs-primary-exe-confirmation";
    std::string selectionSource = "none";
    bool selectionLocked = false;
    const auto directoryKind = knownNonGameDirectoryKind(gameDirectory);
    const auto manualIt = config["manualPrimary"].find(gameKey);
    if (manualIt != config["manualPrimary"].end() && manualIt->is_object()) {
        const auto manualPath = fs::path(toWide(manualIt->value("exe", std::string{})));
        selectionSource = "manual";
        selectionLocked = true;
        if (fs::is_regular_file(manualPath) && pathWithin(manualPath, gameDirectory)) {
            auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto& item) {
                return canonicalPathKey(item.path) == canonicalPathKey(manualPath);
            });
            if (found != candidates.end()) selected = *found;
            else selected = scoreLibraryExecutable(gameDirectory, manualPath, 0);
            status = "ready";
        } else {
            status = "manual-primary-missing";
        }
    } else if (!candidates.empty()) {
        const auto& top = candidates.front();
        const int secondScore = candidates.size() > 1 ? candidates[1].score : -999;
        const int margin = top.score - secondScore;
        const auto mainCandidateCount = std::count_if(candidates.begin(), candidates.end(), [](const auto& item) {
            return item.role == "main-game";
        });
        const bool strongRootNonGameClient = std::any_of(candidates.begin(), candidates.end(), [](const auto& item) {
            return item.depth == 0 && definitePidBlacklistRole(item.role) && item.size >= 5u * 1024u * 1024u;
        });
        const bool plausibleGameBinary = std::any_of(candidates.begin(), candidates.end(), [](const auto& item) {
            return item.role == "main-game" &&
                (item.size >= 5u * 1024u * 1024u || item.score >= 58);
        });
        const bool uniquePlausibleMain = mainCandidateCount == 1 && top.role == "main-game" && top.score >= 58;
        if (top.role == "main-game" &&
            ((top.score >= 70 && (margin >= 18 || top.score >= 115)) || uniquePlausibleMain)) {
            selected = top;
            selectionSource = "automatic-high-confidence";
            status = "ready";
        }
        if (!selected && mainCandidateCount == 0 && definitePidBlacklistRole(top.role)) {
            status = "unrecognized-tool";
        }
        if (!selected && strongRootNonGameClient && !plausibleGameBinary) {
            status = "unrecognized-tool";
        }
    } else {
        status = "no-executable";
    }

    // Automatic directory classification is deliberately applied after PE
    // ranking.  It keeps all evidence/candidates visible, while preventing a
    // known mod/runtime directory from becoming a Steam game.  A manual
    // primary selection remains an explicit user override.
    if (directoryKind && !selectionLocked && status != "no-executable") {
        selected.reset();
        status = "unrecognized-tool";
        selectionSource = "directory-classification";
    }

    const bool primaryEligibleForGameTarget = selected.has_value() && status == "ready" && selected->role == "main-game";
    const auto primaryRule = selected ? normalizedExecutableRule(selected->path) : std::string{};
    json candidateJson = json::array();
    const size_t exposed = (std::min)(candidates.size(), static_cast<size_t>(12));
    for (size_t index = 0; index < exposed; ++index) {
        auto item = libraryCandidateJson(candidates[index], gameDirectory);
        item["rank"] = index + 1;
        const bool isSelected = selected && canonicalPathKey(selected->path) == canonicalPathKey(candidates[index].path);
        item["selected"] = isSelected;
        item["pidCoordination"]["eligibleForGameTarget"] = isSelected && primaryEligibleForGameTarget;
        item["pidCoordination"]["whitelistSuggested"] = isSelected && primaryEligibleForGameTarget;
        if (isSelected && primaryEligibleForGameTarget) {
            item["pidCoordination"]["tags"].push_back("game");
            item["pidCoordination"]["tags"].push_back("pid-whitelist-candidate");
        }
        candidateJson.push_back(std::move(item));
    }

    const auto gameName = toUtf8(gameDirectory.filename().wstring());

    json suggestedBlacklist = json::array();
    std::set<std::string> suggestedBlacklistRules;
    auto appendBlacklistSuggestion = [&](const fs::path& path, const std::string& role, const std::string& source) {
        if (asciiLower(toUtf8(path.extension().wstring())) != ".exe") return;
        const auto rule = normalizedExecutableRule(path);
        if (rule.empty() || rule == primaryRule || !suggestedBlacklistRules.insert(rule).second) return;
        suggestedBlacklist.push_back({
            {"rule", rule}, {"path", toUtf8(path.wstring())}, {"role", role},
            {"source", source}, {"automaticSync", false}
        });
    };
    for (const auto& item : candidates) {
        if (definitePidBlacklistRole(item.role)) appendBlacklistSuggestion(item.path, item.role, "executable-role-classification");
    }
    for (const auto& trainer : trainers) {
        if (trainerGameAgreement(gameName, selected, trainer.gameName) >= 0.90) {
            appendBlacklistSuggestion(trainer.path, "modifier", "gcm-trainer-match");
        }
    }

    json pidTags = json::array({"consume-native-valve-only"});
    if (primaryEligibleForGameTarget) {
        pidTags.push_back("game");
        pidTags.push_back("pid-whitelist-candidate");
    } else {
        pidTags.push_back(selected ? "pid-target-ineligible" : "pid-target-unresolved");
    }
    json suggestedWhitelist = json::array();
    if (primaryEligibleForGameTarget && !primaryRule.empty()) suggestedWhitelist.push_back(primaryRule);

    std::string fingerprintMaterial;
    for (const auto& item : candidates) {
        fingerprintMaterial += canonicalPathKey(item.path) + "|" + std::to_string(item.size) + "|" + std::to_string(item.score) + "\n";
    }
    if (ec) scanComplete = false;
    if (!scanComplete && selectionSource != "manual" && status == "ready") status = "scan-incomplete-needs-review";
    const std::vector<unsigned char> fingerprintBytes(fingerprintMaterial.begin(), fingerprintMaterial.end());
    return {
        {"gameDirectory", toUtf8(gameDirectory.wstring())},
        {"scanComplete", scanComplete},
        {"directoryName", gameName},
        {"status", status},
        // Keep tool-only folders explicit in the persisted scan model.  The
        // UI can then place them in a dedicated non-game bucket without
        // treating them as failed game recognition.
        {"contentType", status == "unrecognized-tool" ? "non-game" : "game"},
        {"classification", status == "unrecognized-tool" ? "明确非游戏" : "游戏候选"},
        {"executableCount", executableCount},
        {"candidateLimitReached", candidateLimitReached},
        {"primaryExecutable", selected ? json(toUtf8(selected->path.wstring())) : json(nullptr)},
        {"primaryRole", selected ? json(selected->role) : json(nullptr)},
        {"primaryScore", selected ? json(selected->score) : json(nullptr)},
        {"primaryProductName", selected && !selected->productName.empty() ? json(selected->productName) : json(nullptr)},
        {"primaryFileDescription", selected && !selected->fileDescription.empty() ? json(selected->fileDescription) : json(nullptr)},
        {"primaryEvidenceSource", selected ? json(selected->primaryEvidenceSource) : json(nullptr)},
        {"primaryIdentityConfidence", selected ? json(selected->identityConfidence) : json(nullptr)},
        {"primaryIdentityConfidenceBand", selected ? json(selected->identityConfidenceBand) : json(nullptr)},
        {"primaryPathEvidence", selected ? libraryPathEvidence(gameDirectory, selected->path, selected->depth) : json(nullptr)},
        {"unrecognizedTool", status == "unrecognized-tool"},
        {"directoryClassification", directoryKind ? json(*directoryKind) : json(nullptr)},
        {"directoryClassificationReason", directoryKind ? json("known-non-game-directory") : json(nullptr)},
        {"selectionSource", selectionSource},
        {"selectionLocked", selectionLocked},
        {"uniquePrimaryInvariant", true},
        {"pidCoordination", {
            {"contract", PID_COORDINATION_CONTRACT},
            {"ownsPidSelection", false},
            {"pidSource", "native-game-recognition-valve"},
            {"writesGameTargetSnapshot", false},
            {"normalizedExeRule", primaryRule.empty() ? json(nullptr) : json(primaryRule)},
            {"role", selected ? json(selected->role) : json(nullptr)},
            {"eligibleForGameTarget", primaryEligibleForGameTarget},
            {"whitelistSuggested", primaryEligibleForGameTarget},
            {"customConfiguredSuggested", false},
            {"minimumWorkingSetMbWhenWhitelisted", 50},
            {"minimumWorkingSetMbWithoutRule", 500},
            {"suggestedWhitelist", std::move(suggestedWhitelist)},
            {"suggestedBlacklist", std::move(suggestedBlacklist)},
            {"tags", std::move(pidTags)}
        }},
        {"candidates", candidateJson},
        {"scanFingerprint", sha256(fingerprintBytes)}
    };
}

static void annotateNativeSteamGame(json& game, const fs::path& gameDirectory,
                                    const SteamNativeGameInfo& nativeInfo) {
    const auto executable = jsonStringOr(game, "primaryExecutable");
    game["steamNative"] = true;
    game["contentType"] = "steam-native";
    game["steamOwnership"] = "native-steam";
    game["nativeSteamAppId"] = nativeInfo.appId;
    game["nativeSteamExecutable"] = executable.empty() ? json(nullptr) : json(executable);
    game["nativeSteamGameDirectory"] = toUtf8(gameDirectory.wstring());
    game["nativeSteamManifestPath"] = toUtf8(nativeInfo.manifestPath.wstring());
    game["nativeSteam"] = {
        {"appId", nativeInfo.appId},
        {"executablePath", executable.empty() ? json(nullptr) : json(executable)},
        {"gameDirectory", toUtf8(gameDirectory.wstring())},
        {"manifestPath", toUtf8(nativeInfo.manifestPath.wstring())},
        {"manifestName", nativeInfo.manifestName}
    };
    if (!nativeInfo.manifestName.empty() && jsonStringOr(game, "primaryProductName").empty()) {
        game["primaryProductName"] = nativeInfo.manifestName;
    }
    game["discoverySource"] = "steam-native";
}

static json scanLibraryRoots(
    const std::vector<fs::path>& roots,
    const std::vector<fs::path>& trainerRoots,
    int maxDepth,
    const json& config) {
    const auto trainers = scanTrainerRoots(trainerRoots);
    json rootItems = json::array();
    json games = json::array();
    json skippedDirectories = json::array();
    json collectionContainers = json::array();
    size_t ready = 0;
    size_t needsConfirmation = 0;
    size_t noExecutableSkipped = 0;
    size_t emptyDirectoriesSkipped = 0;
    size_t platformLibrariesExcluded = 0;
    size_t systemProtectedExcluded = 0;
    size_t userRemovedDirectoriesSkipped = 0;
    size_t manualMissing = 0;
    size_t manualGamesIncluded = 0;
    size_t steamNativeGames = 0;
    size_t nonGames = 0;
    std::set<std::string> discoveredGameDirectories;
    auto recordBuiltinExclusion = [&](const fs::path& directory) {
        const auto key = canonicalPathKey(directory);
        if (!discoveredGameDirectories.insert(key).second) return;
        ++nonGames;
        skippedDirectories.push_back({{"path", toUtf8(directory.wstring())},
            {"reason", "builtin-steam-tool-excluded"}, {"steamAppId", 228980}});
    };
    auto appendGame = [&](json game) {
        if (custom_steam_library::builtinExcludedSteamTool(game)) {
            recordBuiltinExclusion(fs::path(toWide(game.value("gameDirectory", std::string{}))));
            return;
        }
        if (!game.contains("discoverySource")) game["discoverySource"] = "automatic";
        discoveredGameDirectories.insert(canonicalPathKey(
            fs::path(toWide(game.value("gameDirectory", std::string{})))));
        const auto status = game.value("status", std::string{});
        if (status == "ready") ++ready;
        else if (status == "manual-primary-missing") ++manualMissing;
        else if (status == "unrecognized-tool") ++nonGames;
        else ++needsConfirmation;
        games.push_back(std::move(game));
    };
    auto recordAutomaticRejection = [&](const json& game) {
        const auto status = game.value("status", std::string{});
        const auto directory = game.value("gameDirectory", std::string{});
        const bool nonGame = status == "unrecognized-tool" ||
            game.value("contentType", std::string{}) == "non-game" ||
            game.value("unrecognizedTool", false);
        if (nonGame) ++nonGames;
        else ++needsConfirmation;
        skippedDirectories.push_back({
            {"path", directory},
            {"reason", nonGame ? "non-game-directory-excluded" : "no-confident-game-executable"},
            {"status", status},
            {"executableCount", game.value("executableCount", 0)},
            {"primaryExecutable", game.contains("primaryExecutable") ? game["primaryExecutable"] : json(nullptr)},
            {"steamIdRequiredForImport", true}
        });
    };
    auto appendNativeGame = [&](const fs::path& directory, const SteamNativeGameInfo& nativeInfo) {
        const auto directoryKey = canonicalPathKey(directory);
        if (directoryKey.empty() || discoveredGameDirectories.count(directoryKey)) return;
        if (custom_steam_library::builtinExcludedSteamTool({{"appId", nativeInfo.appId},
                {"manifestName", nativeInfo.manifestName}, {"gameDirectory", toUtf8(directory.wstring())}})) {
            recordBuiltinExclusion(directory);
            return;
        }
        auto game = scanOneGameDirectory(directory, maxDepth, config, trainers);
        annotateNativeSteamGame(game, directory, nativeInfo);
        game["steamNativeRecord"] = game["nativeSteam"];
        ++steamNativeGames;
        appendGame(std::move(game));
    };
    for (const auto& rawRoot : roots) {
        std::error_code ec;
        const auto canonicalRoot = fs::weakly_canonical(rawRoot, ec);
        const auto root = ec ? rawRoot : canonicalRoot;
        std::error_code rootStatusError;
        const bool rootExists = fs::is_directory(root, rootStatusError);
        json rootItem = {
            {"path", toUtf8(root.wstring())},
            {"exists", rootExists && !rootStatusError},
            {"mode", "immediate-child-directories-are-games"},
            {"gameDirectoryCount", 0},
            {"scanComplete", false}
        };
        if (ec || rootStatusError || !rootExists) {
            rootItem["status"] = "root-unavailable";
            rootItem["scanError"] = (ec ? ec : rootStatusError).message();
            rootItems.push_back(std::move(rootItem));
            continue;
        }
        if (firstRunProtectedDirectory(root)) {
            rootItem["scanComplete"] = true;
            rootItem["status"] = "system-protected-root-excluded";
            ++systemProtectedExcluded;
            rootItems.push_back(std::move(rootItem));
            continue;
        }
        if (const auto directoryKind = knownNonGameDirectoryKind(root)) {
            // Older configurations may already contain a utility directory
            // as a library root from the previous substring-based discovery.
            // Exclude it at the root boundary as well; otherwise its helper
            // subdirectories could be reinterpreted as games on every scan.
            rootItem["scanComplete"] = true;
            rootItem["status"] = "non-game-root-excluded";
            rootItem["directoryClassification"] = *directoryKind;
            ++nonGames;
            rootItems.push_back(std::move(rootItem));
            continue;
        }
        if (const auto platform = platformLibraryKind(root)) {
            rootItem["scanComplete"] = true;
            rootItem["status"] = "platform-library-root-excluded";
            rootItem["platform"] = *platform;
            ++platformLibrariesExcluded;
            rootItems.push_back(std::move(rootItem));
            continue;
        }
        std::vector<fs::path> directories;
        bool rootScanComplete = true;
        fs::directory_iterator rootIterator(root, fs::directory_options::none, ec), rootEnd;
        if (ec) { rootScanComplete = false; rootItem["scanError"] = ec.message(); ec.clear(); }
        for (; rootIterator != rootEnd; rootIterator.increment(ec)) {
            if (ec) { rootScanComplete = false; rootItem["scanError"] = ec.message(); ec.clear(); continue; }
            const bool isDirectory = rootIterator->is_directory(ec);
            if (ec) { rootScanComplete = false; rootItem["scanError"] = ec.message(); ec.clear(); continue; }
            if (isDirectory) directories.push_back(rootIterator->path());
        }
        if (ec) { rootScanComplete = false; rootItem["scanError"] = ec.message(); }
        std::sort(directories.begin(), directories.end(), [](const auto& left, const auto& right) {
            return canonicalPathKey(left) < canonicalPathKey(right);
        });
        const size_t gamesBeforeRoot = games.size();
        rootItem["status"] = "scanning";
        for (const auto& directory : directories) {
            ec.clear();
            if (configuredDirectory(config.value("excludedGameDirectories", json::array()), directory)) {
                ++userRemovedDirectoriesSkipped;
                skippedDirectories.push_back({
                    {"path", toUtf8(directory.wstring())}, {"reason", "user-removed-from-library"}
                });
                continue;
            }
            if (firstRunProtectedDirectory(directory)) {
                ++systemProtectedExcluded;
                skippedDirectories.push_back({
                    {"path", toUtf8(directory.wstring())}, {"reason", "system-protected-directory-excluded"}
                });
                continue;
            }
            if (fs::is_empty(directory, ec) && !ec) {
                ++emptyDirectoriesSkipped;
                skippedDirectories.push_back({
                    {"path", toUtf8(directory.wstring())}, {"reason", "empty-directory"}
                });
                continue;
            }
            if (const auto nativeInfo = steamNativeGameInfoForDirectory(directory)) {
                appendNativeGame(directory, *nativeInfo);
                continue;
            }
            if (const auto platform = platformLibraryKind(directory)) {
                ++platformLibrariesExcluded;
                skippedDirectories.push_back({
                    {"path", toUtf8(directory.wstring())},
                    {"reason", "platform-library-excluded"},
                    {"platform", *platform}
                });
                continue;
            }

            const bool knownCollection = explicitCollectionDirectory(directory);
            if (knownCollection) {
                std::vector<json> childGames;
                std::vector<fs::path> childDirectories;
                ec.clear();
                fs::directory_iterator childIterator(directory, fs::directory_options::none, ec), childEnd;
                if (ec) { rootScanComplete = false; rootItem["scanError"] = ec.message(); ec.clear(); }
                for (; childIterator != childEnd; childIterator.increment(ec)) {
                    if (ec) { rootScanComplete = false; rootItem["scanError"] = ec.message(); ec.clear(); continue; }
                    const bool isDirectory = childIterator->is_directory(ec);
                    if (ec) { rootScanComplete = false; rootItem["scanError"] = ec.message(); ec.clear(); continue; }
                    if (isDirectory) childDirectories.push_back(childIterator->path());
                }
                if (ec) { rootScanComplete = false; rootItem["scanError"] = ec.message(); }
                std::sort(childDirectories.begin(), childDirectories.end(), [](const auto& left, const auto& right) {
                    return canonicalPathKey(left) < canonicalPathKey(right);
                });
                for (const auto& child : childDirectories) {
                    ec.clear();
                    if (fs::is_empty(child, ec) && !ec) continue;
                    if (platformLibraryKind(child)) continue;
                    if (const auto nativeInfo = steamNativeGameInfoForDirectory(child)) {
                        appendNativeGame(child, *nativeInfo);
                        continue;
                    }
                    if (configuredDirectory(config.value("excludedGameDirectories", json::array()), child)) {
                        ++userRemovedDirectoriesSkipped;
                        skippedDirectories.push_back({
                            {"path", toUtf8(child.wstring())}, {"reason", "user-removed-from-library"}
                        });
                        continue;
                    }
                    if (firstRunProtectedDirectory(child)) {
                        ++systemProtectedExcluded;
                        skippedDirectories.push_back({
                            {"path", toUtf8(child.wstring())}, {"reason", "system-protected-directory-excluded"}
                        });
                        continue;
                    }
                    auto childGame = scanOneGameDirectory(child, (std::max)(1, maxDepth - 1), config, trainers);
                    if (!childGame.value("scanComplete", true)) rootScanComplete = false;
                    const auto childStatus = childGame.value("status", std::string{});
                    if (childStatus == "ready" || childStatus == "manual-primary-missing" || childStatus == "scan-incomplete-needs-review") {
                        childGames.push_back(std::move(childGame));
                    } else if (childGame.value("executableCount", 0) > 0) {
                        recordAutomaticRejection(childGame);
                    }
                }
                collectionContainers.push_back({
                    {"path", toUtf8(directory.wstring())},
                    {"reason", "known-collection-directory"},
                    {"discoveredGames", childGames.size()}
                });
                for (auto& childGame : childGames) appendGame(std::move(childGame));
                // A directory explicitly named EMU/Emulators/etc. is always a container.
                // Never let an EXE in one child make the entire container look like one game.
                continue;
            }

            auto parentGame = scanOneGameDirectory(directory, maxDepth, config, trainers);
            if (!parentGame.value("scanComplete", true)) rootScanComplete = false;
            if (parentGame.value("status", std::string{}) == "ready" ||
                parentGame.value("status", std::string{}) == "manual-primary-missing" ||
                parentGame.value("status", std::string{}) == "scan-incomplete-needs-review") {
                appendGame(std::move(parentGame));
                continue;
            }

            if (parentGame.value("executableCount", 0) == 0) {
                ++noExecutableSkipped;
                skippedDirectories.push_back({
                    {"path", toUtf8(directory.wstring())}, {"reason", "no-executable"}
                });
                continue;
            }
            // A directory with arbitrary EXEs is not a game.  Keep the
            // rejection in the scan audit, but never expose it as a library
            // game or let it enter the automatic Steam-ID queue.
            recordAutomaticRejection(parentGame);
        }
        rootItem["gameDirectoryCount"] = games.size() - gamesBeforeRoot;
        rootItem["scanComplete"] = rootScanComplete;
        rootItem["status"] = rootScanComplete ? "scanned" : "scan-incomplete";
        rootItems.push_back(std::move(rootItem));
    }
    for (const auto& item : config.value("manualGameDirectories", json::array())) {
        if (!item.is_object()) continue;
        const auto directory = fs::path(toWide(item.value("path", std::string{})));
        const auto directoryKey = canonicalPathKey(directory);
        if (directoryKey.empty() || discoveredGameDirectories.count(directoryKey)) continue;
        if (configuredDirectory(config.value("excludedGameDirectories", json::array()), directory)) continue;
        if (custom_steam_library::builtinExcludedSteamToolPath(toUtf8(directory.wstring()))) {
            recordBuiltinExclusion(directory);
            continue;
        }
        std::error_code ec;
        const bool manualDirectoryExists = fs::is_directory(directory, ec);
        if (ec || !manualDirectoryExists) {
            const bool unavailable = ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory;
            skippedDirectories.push_back({
                {"path", toUtf8(directory.wstring())},
                {"reason", unavailable ? "manual-game-scan-unavailable" : "manual-game-directory-missing"}
            });
            continue;
        }
        if (const auto nativeInfo = steamNativeGameInfoForDirectory(directory)) {
            appendNativeGame(directory, *nativeInfo);
            continue;
        }
        auto game = scanOneGameDirectory(directory, maxDepth, config, trainers);
        if (!game.value("scanComplete", true)) skippedDirectories.push_back({
            {"path", toUtf8(directory.wstring())}, {"reason", "manual-game-scan-unavailable"}});
        game["discoverySource"] = "manual";
        game["manualGameDirectoryLocked"] = true;
        game["bypassedAutomaticPlatformExclusion"] = true;
        if (game.value("executableCount", 0) == 0) {
            skippedDirectories.push_back({
                {"path", toUtf8(directory.wstring())}, {"reason", "manual-game-no-executable"}
            });
            continue;
        }
        ++manualGamesIncluded;
        appendGame(std::move(game));
    }
    for (const auto& steamRoot : steamLibraryRootsForNativeScan(roots)) {
        const auto common = steamRoot / L"steamapps" / L"common";
        std::error_code ec;
        if (!fs::is_directory(common, ec)) continue;
        for (const auto& entry : fs::directory_iterator(
                 common, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) {
                ec.clear();
                continue;
            }
            if (const auto nativeInfo = steamNativeGameInfoForDirectory(entry.path())) {
                appendNativeGame(entry.path(), *nativeInfo);
            }
            ec.clear();
        }
    }
    json suggestedWhitelist = json::array();
    json suggestedBlacklist = json::array();
    json pidConflicts = json::array();
    std::set<std::string> whitelistRules;
    std::set<std::string> blacklistRules;
    for (const auto& game : games) {
        if (!game.contains("pidCoordination") || !game["pidCoordination"].is_object()) continue;
        for (const auto& value : game["pidCoordination"].value("suggestedWhitelist", json::array())) {
            if (!value.is_string()) continue;
            const auto rule = normalizeGameRule(value.get<std::string>());
            if (!rule.empty() && whitelistRules.insert(rule).second) suggestedWhitelist.push_back(rule);
        }
        for (auto item : game["pidCoordination"].value("suggestedBlacklist", json::array())) {
            if (!item.is_object()) continue;
            const auto rule = normalizeGameRule(item.value("rule", std::string{}));
            if (rule.empty() || !blacklistRules.insert(rule).second) continue;
            item["rule"] = rule;
            item["gameDirectory"] = game.value("gameDirectory", std::string{});
            suggestedBlacklist.push_back(std::move(item));
        }
    }
    for (const auto& rule : whitelistRules) {
        if (blacklistRules.count(rule)) pidConflicts.push_back({
            {"type", "suggested-whitelist-vs-suggested-blacklist"}, {"rule", rule},
            {"resolution", "primary-game-whitelist-wins; blacklist suggestion is never auto-applied"}
        });
    }
    const auto previousGames = games;
    const auto previousGameCount = games.size();
    games = custom_steam_library::uniqueExecutableInventory(games, [](const std::string& path) {
        return canonicalPathKey(fs::path(toWide(path)));
    });
    const size_t duplicateExecutablesRemoved = previousGameCount - games.size();
    for (const auto& game : previousGames) {
        const auto status = jsonStringOr(game, "status");
        if (status == "unrecognized-tool") --nonGames;
        else if (status != "ready" && status != "manual-primary-missing") --needsConfirmation;
    }
    ready = manualMissing = steamNativeGames = manualGamesIncluded = 0;
    for (const auto& game : games) {
        if (jsonStringOr(game, "status") == "ready") ++ready;
        if (jsonStringOr(game, "status") == "manual-primary-missing") ++manualMissing;
        const auto status = jsonStringOr(game, "status");
        if (status == "unrecognized-tool") ++nonGames;
        else if (status != "ready" && status != "manual-primary-missing") ++needsConfirmation;
        if (jsonBoolSafe(game, "steamNative", false)) ++steamNativeGames;
        if (jsonStringOr(game, "discoverySource") == "manual") ++manualGamesIncluded;
    }
    const auto whitelistPath = gameWhitelistPath();
    const auto blacklistPath = playerBlacklistPath();
    std::vector<std::string> existingWhitelist;
    std::vector<std::string> existingBlacklist;
    json ruleReadErrors = json::array();
    try { existingWhitelist = readGameRuleFile(whitelistPath); }
    catch (const std::exception& error) { ruleReadErrors.push_back({{"path", toUtf8(whitelistPath.wstring())}, {"error", error.what()}}); }
    try { existingBlacklist = readGameRuleFile(blacklistPath); }
    catch (const std::exception& error) { ruleReadErrors.push_back({{"path", toUtf8(blacklistPath.wstring())}, {"error", error.what()}}); }
    const std::set<std::string> existingWhitelistSet(existingWhitelist.begin(), existingWhitelist.end());
    const std::set<std::string> existingBlacklistSet(existingBlacklist.begin(), existingBlacklist.end());
    for (const auto& rule : whitelistRules) {
        if (existingBlacklistSet.count(rule)) pidConflicts.push_back({
            {"type", "suggested-whitelist-vs-existing-player-blacklist"}, {"rule", rule},
            {"resolution", "explicit whitelist sync removes the exact player-blacklist rule"}
        });
    }
    for (const auto& rule : existingWhitelistSet) {
        if (existingBlacklistSet.count(rule)) pidConflicts.push_back({
            {"type", "existing-user-rule-conflict"}, {"rule", rule},
            {"resolution", "explicit whitelist sync keeps whitelist and removes the exact player-blacklist rule"}
        });
    }
    return {
        {"schemaVersion", 1},
        {"isolated", true},
        {"readOnlyScan", true},
        {"gamesOrToolsStarted", false},
        {"shortcutsVdfModified", false},
        {"scanMode", "event-driven-no-polling"},
        {"continuousScanDefault", config.value("enabled", true)},
        {"automaticTriggers", config.value("automaticTriggers", json::array())},
        {"maxDepth", maxDepth},
        {"rootPolicy", "Only immediate child directories with one confident main-game EXE enter automatic games; tool/unresolved directories are skipped, and Steam import still requires verified SteamID"},
        {"trainerRoots", [&]() {
            json value = json::array();
            for (const auto& root : trainerRoots) value.push_back(toUtf8(root.wstring()));
            return value;
        }()},
        {"trainerRecords", trainers.size()},
        {"pidCoordination", {
            {"contract", PID_COORDINATION_CONTRACT},
            {"ownsPidSelection", false},
            {"pidSource", "native-game-recognition-valve"},
            {"writesGameTargetSnapshot", false},
            {"processEnumeration", false},
            {"syncPolicy", "explicit-whitelist-only"},
            {"blacklistAutoAdd", false},
            {"normalizedExeRule", "lowercase executable basename without .exe"},
            {"minimumWorkingSetMbWhenWhitelisted", 50},
            {"minimumWorkingSetMbWithoutRule", 500},
            {"whitelistPath", toUtf8(whitelistPath.wstring())},
            {"playerBlacklistPath", toUtf8(blacklistPath.wstring())},
            {"suggestedWhitelist", suggestedWhitelist},
            {"suggestedBlacklist", suggestedBlacklist},
            {"existingWhitelist", stringVectorJson(existingWhitelist)},
            {"existingPlayerBlacklist", stringVectorJson(existingBlacklist)},
            {"conflicts", pidConflicts},
            {"ruleReadErrors", ruleReadErrors}
        }},
        {"roots", rootItems},
        {"collectionContainers", collectionContainers},
        {"skippedDirectories", skippedDirectories},
        {"summary", {
            {"games", games.size()}, {"duplicateExecutablesRemoved", duplicateExecutablesRemoved},
            {"ready", ready},
            {"needsPrimaryExeConfirmation", needsConfirmation},
            {"noExecutable", noExecutableSkipped},
            {"noExecutableSkipped", noExecutableSkipped},
            {"emptyDirectoriesSkipped", emptyDirectoriesSkipped},
            {"platformLibrariesExcluded", platformLibrariesExcluded},
            {"systemProtectedExcluded", systemProtectedExcluded},
            {"userRemovedDirectoriesSkipped", userRemovedDirectoriesSkipped},
            {"collectionContainers", collectionContainers.size()},
            {"manualPrimaryMissing", manualMissing},
            {"manualGamesIncluded", manualGamesIncluded},
            {"steamNativeGames", steamNativeGames},
            {"nonGames", nonGames}
        }},
        {"games", games}
    };
}

struct PidRuleSyncPlan {
    std::vector<std::string> currentWhitelist;
    std::vector<std::string> currentBlacklist;
    std::vector<std::string> nextWhitelist;
    std::vector<std::string> nextBlacklist;
    std::vector<std::string> addedWhitelist;
    std::vector<std::string> removedBlacklistConflicts;
    json skipped = json::array();
};

static json validateLibraryScanState(json state) {
    if (!state.is_object() || !state.contains("games") || !state["games"].is_array())
        throw std::runtime_error("Library scan state has an invalid format");
    std::set<std::string> inventoryExecutables;
    for (const auto* section : {"games", "unscannedGames", "missingGames"}) {
        if (!state.contains(section)) { state[section] = json::array(); continue; }
        if (!state[section].is_array()) throw std::runtime_error("Library scan inventory must be an array");
        json valid = json::array();
        std::set<std::string> seen;
        for (const auto& row : state[section]) {
            if (!row.is_object() || custom_steam_library::builtinExcludedSteamTool(row) || jsonStringOr(row, "gameDirectory").empty()) continue;
            const auto key = canonicalPathKey(fs::path(toWide(jsonStringOr(row, "gameDirectory"))));
            if (key.empty() || !seen.insert(key).second) continue;
            auto game = row;
            bool malformed = false;
            for (const auto* field : {"primaryExecutable", "status", "directoryName", "contentType", "classification", "selectionSource",
                    "primaryRole", "primaryProductName", "primaryFileDescription", "primaryEvidenceSource", "primaryIdentityConfidenceBand"}) {
                if (game.contains(field) && !game[field].is_string() && !game[field].is_null()) malformed = true;
                if (game.contains(field) && !game[field].is_string()) game.erase(field);
            }
            for (const auto* field : {"steamNative", "unrecognizedTool", "selectionLocked", "scanComplete"}) {
                if (game.contains(field) && !game[field].is_boolean()) { malformed = true; game.erase(field); }
            }
            const fs::path gamePath(toWide(jsonStringOr(row, "gameDirectory")));
            if (!gamePath.is_absolute()) continue;
            if (!game.contains("primaryExecutable")) game["primaryExecutable"] = "";
            if (!game.contains("status")) game["status"] = "unknown";
            if (!game["primaryExecutable"].get<std::string>().empty()) {
                const fs::path primary(toWide(game["primaryExecutable"].get<std::string>()));
                const auto extension = asciiLower(toUtf8(primary.extension().wstring()));
                if (!primary.is_absolute() || !pathWithin(primary, gamePath) || extension != ".exe") {
                    game["primaryExecutable"] = "";
                    malformed = true;
                }
            }
            if (!game.value("scanComplete", true) && game.value("status", std::string{}) == "ready" &&
                jsonStringOr(game, "selectionSource") != "manual") game["status"] = "scan-incomplete-needs-review";
            if (malformed) { game["status"] = "data-invalid-review-required"; game["dataValidationWarning"] = true; }
            const auto exe = jsonStringOr(game, "primaryExecutable");
            if (!exe.empty() && inventoryExecutables.contains(canonicalPathKey(fs::path(toWide(exe))))) continue;
            valid.push_back(std::move(game));
        }
        state[section] = custom_steam_library::uniqueExecutableInventory(valid, [](const std::string& path) {
            return canonicalPathKey(fs::path(toWide(path)));
        });
        for (const auto& game : state[section]) {
            const auto exe = jsonStringOr(game, "primaryExecutable");
            if (!exe.empty()) inventoryExecutables.insert(canonicalPathKey(fs::path(toWide(exe))));
        }
    }
    return state;
}

static bool libraryScanCoversGame(const json& report, const json& config, const fs::path& directory) {
    for (const auto& skipped : report.value("skippedDirectories", json::array())) {
        const auto path = fs::path(toWide(jsonStringOr(skipped, "path")));
        if (!path.empty() && pathWithin(directory, path) && jsonStringOr(skipped, "reason") == "manual-game-scan-unavailable") return false;
    }
    if (configuredDirectory(config.value("manualGameDirectories", json::array()), directory)) return true;
    bool covered = false;
    for (const auto& rootItem : report.value("roots", json::array())) {
        const auto root = fs::path(toWide(jsonStringOr(rootItem, "path")));
        if (root.empty() || !pathWithin(directory, root)) continue;
        if (!rootItem.value("scanComplete", true)) return false;
        if (jsonStringOr(rootItem, "status") == "scanned") covered = true;
    }
    return covered;
}

static json loadLibraryScanState(const fs::path& path) {
    const auto dataRoot = path.parent_path().parent_path();
    std::vector<fs::path> candidates{path};
    const auto backups = stateBackupCandidates(dataRoot, path, "library-scan");
    candidates.insert(candidates.end(), backups.begin(), backups.end());
    std::optional<fs::path> corrupt;
    for (const auto& candidate : candidates) {
        try {
            const auto bytes = readBinaryFile(candidate, 64u << 20);
            auto state = json::parse(bytes.begin(), bytes.end());
            state = validateLibraryScanState(std::move(state));
            if (candidate != path) {
                try { writeJsonWithBackup(path, dataRoot / L"backups" / L"state", "library-scan", state); } catch (...) {}
            }
            return state;
        } catch (...) {
            if (candidate == path && !corrupt) corrupt = preserveCorruptStateFile(dataRoot, path, "library-scan");
        }
    }
    throw std::runtime_error(corrupt
        ? "Library scan state is corrupt and no valid backup exists: " + toUtf8(corrupt->wstring())
        : "Library scan state is invalid; run --scan-library first");
}

struct SteamShortcutTarget {
    fs::path path;
    std::string accountId;
};

static SteamShortcutTarget selectSteamShortcutTarget(
    const std::optional<fs::path>& explicitFile,
    const std::optional<std::string>& requestedAccount,
    const std::vector<fs::path>& steamRoots = {}) {
    auto sources = explicitFile
        ? shortcutSources(explicitFile, steamRoots)
        : steamAccountSources(steamRoots);
    if (requestedAccount) {
        sources.erase(std::remove_if(sources.begin(), sources.end(), [&](const auto& item) {
            return item.second != *requestedAccount;
        }), sources.end());
    }
    if (sources.empty()) {
        throw std::runtime_error(requestedAccount
            ? "No shortcuts.vdf exists for the requested Steam account"
            : "No Steam shortcuts.vdf is available; create one non-Steam shortcut once or select an account file");
    }
    if (sources.size() != 1) {
        throw std::runtime_error("Multiple Steam accounts have shortcuts.vdf; select one with --account or --shortcuts");
    }
    return {fs::absolute(sources.front().first), sources.front().second};
}

static json listSteamAccountTargets(const std::vector<fs::path>& steamRoots) {
    const auto detectedSteam = steamInstallPath();
    if (steamRoots.empty() && !detectedSteam) {
        return {
            {"schemaVersion", 1},
            {"mode", "steam-account-target-list"},
            {"steamInstalled", false},
            {"steamStatus", "not-installed"},
            {"steamDownloadUrl", "https://store.steampowered.com/about/"},
            {"multipleAccounts", false},
            {"requiresExplicitSelection", false},
            {"accounts", json::array()}
        };
    }
    json accounts = json::array();
    for (const auto& [file, accountId] : steamAccountSources(steamRoots)) {
        const auto grid = file.parent_path() / L"grid";
        size_t gridFiles = 0;
        uintmax_t gridBytes = 0;
        std::error_code ec;
        if (fs::is_directory(grid, ec)) {
            for (const auto& entry : fs::directory_iterator(grid, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec)) {
                    ec.clear();
                    continue;
                }
                ++gridFiles;
                gridBytes += entry.file_size(ec);
                ec.clear();
            }
        }
        size_t shortcutCount = 0;
        std::string readError;
        if (fs::is_regular_file(file)) {
            try {
                shortcutCount = readShortcuts(file, accountId).size();
            } catch (const std::exception& error) {
                readError = error.what();
            }
        }
        accounts.push_back({
            {"accountId", accountId},
            {"shortcutsVdf", toUtf8(file.wstring())},
            {"shortcutsVdfExists", fs::is_regular_file(file)},
            {"steamRoot", toUtf8(file.parent_path().parent_path().parent_path().parent_path().wstring())},
            {"shortcutCount", shortcutCount},
            {"gridFiles", gridFiles},
            {"gridBytes", gridBytes},
            {"realSteamTarget", realSteamShortcutsPath(file)},
            {"readError", readError.empty() ? json(nullptr) : json(readError)}
        });
    }
    return {
        {"schemaVersion", 1},
        {"mode", "steam-account-target-list"},
        {"steamInstalled", true},
        {"steamStatus", accounts.empty() ? "installed-no-account" : "ready"},
        {"steamPath", detectedSteam ? toUtf8(detectedSteam->wstring()) : std::string{}},
        {"steamDownloadUrl", "https://store.steampowered.com/about/"},
        {"multipleAccounts", accounts.size() > 1},
        {"requiresExplicitSelection", accounts.size() > 1},
        {"accounts", accounts}
    };
}

static json loadJsonDocument(const fs::path& path, size_t maximumBytes = 64u << 20) {
    const auto bytes = readBinaryFile(path, maximumBytes);
    return json::parse(bytes.begin(), bytes.end());
}

static std::optional<json> portableArtworkManifestAtDirectory(
    const fs::path& dataRoot,
    const json& game,
    const fs::path& executable) {
    const auto directory = fs::path(toWide(game.value("gameDirectory", std::string{})));
    if (directory.empty()) return std::nullopt;
    const auto path = dataRoot / L"artwork" / toWide(gameDataId(directory)) / L"manifest.json";
    if (!fs::is_regular_file(path)) return std::nullopt;
    try {
        auto manifest = loadJsonDocument(path);
        if (!manifest.is_object()) return std::nullopt;
        if (canonicalPathKey(fs::path(toWide(jsonStringOr(manifest, "exe")))) !=
            canonicalPathKey(executable)) return std::nullopt;
        // An artwork request may have produced a valid identity and one
        // usable image before a required slot failed (for example a Steam
        // CDN 404). Do not discard that identity from the plan. Keeping the
        // incomplete manifest visible lets the queue retry artwork and use
        // the IGDB fallback without starting identity resolution again.
        const auto match = manifest.value("match", json::object());
        if (!match.is_object()) return std::nullopt;
        for (const auto* field : {"identityStatus", "steamVerificationStatus", "metadataVerificationStatus", "resolverVersion", "primaryProvider", "formalName", "displayName", "contentRelation", "identityKind"}) {
            if (match.contains(field) && !match[field].is_null() && !match[field].is_string()) return std::nullopt;
        }
        if (manifest.contains("metadata") && !manifest["metadata"].is_null() && !manifest["metadata"].is_object()) return std::nullopt;
        if (manifest.contains("artwork") && !manifest["artwork"].is_array()) return std::nullopt;
        const auto provider = jsonStringOr(match, "primaryProvider");
        const bool steamIdentityReady = provider == "steam" &&
            jsonStringOr(match, "identityStatus") == "verified" &&
            jsonStringOr(match, "steamVerificationStatus") == "steam-verified" &&
            positiveJsonInt(match, "appId") > 0;
        const bool igdbIdentityReady = provider == "playnite-igdb" &&
            jsonStringOr(match, "identityStatus") == "metadata-verified" &&
            positiveJsonInt(match, "igdbId") > 0;
        // A partial visual-only result has no Steam/IGDB identity authority.
        // Its valid local images must still be visible and independently usable.
        bool readableArtwork = false;
        for (const auto& image : manifest.value("artwork", json::array())) {
            if (!image.is_object() || !jsonBoolSafe(image, "ok", false)) continue;
            for (const auto* field : {"portableFile", "file"}) {
                const auto file = jsonStringOr(image, field);
                std::error_code error;
                if (!file.empty() && fs::is_regular_file(fs::path(toWide(file)), error)) readableArtwork = true;
            }
        }
        if (!steamIdentityReady && !igdbIdentityReady && !readableArtwork) return std::nullopt;
        summarizeArtworkManifest(manifest);
        manifest["portableManifestPath"] = toUtf8(path.wstring());
        return manifest;
    } catch (...) {
        return std::nullopt;
    }
}

// Deduplication may retain a manually selected bin/x64 directory. Reuse the
// same EXE's former root artwork instead of stranding it under the old key.
static std::optional<json> portableArtworkManifest(
    const fs::path& dataRoot, const json& game, const fs::path& executable) {
    if (const auto direct = portableArtworkManifestAtDirectory(dataRoot, game, executable)) return direct;
    const auto aliases = game.find("alternateGameDirectories");
    if (aliases == game.end() || !aliases->is_array()) return std::nullopt;
    for (const auto& alias : *aliases) {
        if (!alias.is_string()) continue;
        const auto directory = fs::path(toWide(alias.get<std::string>()));
        if (!directory.is_absolute() || !pathWithin(executable, directory)) continue;
        auto alternative = game; alternative["gameDirectory"] = alias;
        if (const auto found = portableArtworkManifestAtDirectory(dataRoot, alternative, executable)) return found;
    }
    return std::nullopt;
}

static fs::path manifestArtworkSource(const json& artwork) {
    const auto portable = fs::path(toWide(jsonStringOr(artwork, "portableFile")));
    if (!portable.empty() && fs::is_regular_file(portable)) return portable;
    const auto original = fs::path(toWide(jsonStringOr(artwork, "file")));
    return fs::is_regular_file(original) ? original : fs::path{};
}

static json manualArtworkForExecutable(const json& overrides, const fs::path& executable) {
    if (!overrides.is_object() || !overrides.contains("items") || !overrides["items"].is_object()) return json::object();
    const auto wanted = canonicalPathKey(executable);
    for (const auto& [key, value] : overrides["items"].items()) {
        if (canonicalPathKey(fs::path(toWide(key))) == wanted && value.is_object()) return value;
    }
    return json::object();
}
static fs::path manualArtworkSource(const json& overrideItem, const char* type) {
    if (!overrideItem.is_object()) return {};
    const auto it = overrideItem.find(type);
    if (it == overrideItem.end() || !it->is_object()) return {};
    const auto portable = fs::path(toWide(jsonStringOr(*it, "portableFile")));
    if (!portable.empty() && fs::is_regular_file(portable)) return portable;
    const auto original = fs::path(toWide(jsonStringOr(*it, "file")));
    return !original.empty() && fs::is_regular_file(original) ? original : fs::path{};
}

static bool validSteamArtworkSuffix(const std::string& type, const std::string& suffix) {
    return (type == "tall" && suffix == "p") || (type == "long" && suffix.empty()) ||
        (type == "hero" && suffix == "_hero") || (type == "logo" && suffix == "_logo") || (type == "icon" && suffix == "_icon");
}

static json artworkReadiness(const json& manifest, const json& manualOverride = json::object()) {
    json available = json::array();
    bool tall = false;
    bool longArtwork = false;
    bool hero = false;
    const auto manualCover = manualArtworkSource(manualOverride, "cover");
    const auto manualLong = manualArtworkSource(manualOverride, "long");
    const auto manualWallpaper = manualArtworkSource(manualOverride, "wallpaper");
    if (!manualCover.empty()) { tall = true; available.push_back("tall-manual"); }
    if (!manualLong.empty()) { longArtwork = true; available.push_back("long-manual"); }
    if (!manualWallpaper.empty()) { hero = true; available.push_back("hero-manual"); }
    const auto artworkList = manifest.contains("artwork") && manifest["artwork"].is_array() ? manifest["artwork"] : json::array();
    for (const auto& artwork : artworkList) {
        if (!jsonBoolSafe(artwork, "ok", false)) continue;
        const auto source = manifestArtworkSource(artwork);
        if (source.empty()) continue;
        const auto type = jsonStringOr(artwork, "type");
        available.push_back(type);
        if (type == "tall") tall = true;
        if (type == "long") longArtwork = true;
        if (type == "hero") hero = true;
    }
    const auto protection = manualOverride.is_object() && manualOverride.contains("artworkProtection") && manualOverride["artworkProtection"].is_object()
        ? manualOverride["artworkProtection"] : json::object();
    const bool tallDeleted = jsonStringOr(protection, "cover") == "deleted";
    const bool longDeleted = jsonStringOr(protection, "long") == "deleted";
    const bool heroDeleted = jsonStringOr(protection, "wallpaper") == "deleted";
    if (tallDeleted) tall = false;
    if (longDeleted) longArtwork = false;
    if (heroDeleted) hero = false;
    return {
        {"automaticSlotsComplete", (tall || tallDeleted) && (longArtwork || longDeleted) && (hero || heroDeleted)},
        {"minimumComplete", tall && hero},
        {"tall", tall},
        {"long", longArtwork},
        {"hero", hero},
        {"available", available}
    };
}

static std::optional<json> cachedVerifiedSteamIdentity(const fs::path& dataRoot, const fs::path& executable) {
    const auto root = dataRoot / L"cache" / L"identity";
    std::error_code error;
    if (!fs::is_directory(root, error)) return std::nullopt;
    std::optional<json> best;
    int64_t bestUpdated = -1;
    for (const auto& entry : fs::directory_iterator(root, error)) {
        if (error) break;
        if (entry.path().extension() != L".json" || !entry.is_regular_file(error)) { error.clear(); continue; }
        try {
            auto record = loadJsonDocument(entry.path());
            const auto exe = jsonStringOr(record, "executable", jsonStringOr(record, "exe"));
            if (exe.empty() || canonicalPathKey(fs::path(toWide(exe))) != canonicalPathKey(executable)) continue;
            json match = record.contains("match") && record["match"].is_object() ? record["match"] : json::object();
            if (match.empty()) {
                const auto path = jsonStringOr(record, "manifestPath");
                if (path.empty()) continue;
                const auto manifest = loadJsonDocument(fs::path(toWide(path)));
                const auto sourceExe = jsonStringOr(manifest, "exe");
                if (sourceExe.empty() || canonicalPathKey(fs::path(toWide(sourceExe))) != canonicalPathKey(executable)) continue;
                match = manifest.value("match", json::object());
                record["metadata"] = manifest.value("metadata", json::object());
            }
            if (!custom_steam_library::verifiedSteamIdentityMatch(match)) continue;
            const auto updated = record.contains("updatedAt") && record["updatedAt"].is_number_integer() ? record["updatedAt"].get<int64_t>() : 0;
            if (best && updated <= bestUpdated) continue;
            record["match"] = match; bestUpdated = updated; best = std::move(record);
        } catch (...) { /* Optional identity cache is not authority if malformed. */ }
    }
    return best;
}

static json buildSteamLibraryAddPlan(
    const fs::path& dataRoot,
    const fs::path& statePath,
    const SteamShortcutTarget& target,
    const fs::path& outputPath,
    const std::set<std::string>& selectedGameDirectories = {},
    bool explicitSelection = false) {
    const auto state = loadLibraryScanState(statePath);
    const auto manualOverrides = loadManualOverrides(dataRoot / L"config" / L"manual-overrides.json");
    const bool shortcutsVdfExists = fs::is_regular_file(target.path);
    const auto originalVdf = shortcutsVdfExists
        ? readBinaryFile(target.path, 16u << 20)
        : std::vector<unsigned char>{};
    const auto existing = shortcutsVdfExists
        ? readShortcuts(target.path, target.accountId)
        : std::vector<SteamShortcut>{};
    std::map<std::string, SteamShortcut> existingByExecutable;
    std::map<uint32_t, SteamShortcut> existingById;
    for (const auto& shortcut : existing) {
        const auto executable = expandEnvironmentPath(unquote(shortcut.exe));
        if (!executable.empty()) existingByExecutable.emplace(canonicalPathKey(executable), shortcut);
        existingById.emplace(shortcutIds(shortcut).shortId, shortcut);
    }

    json items = json::array();
    size_t readyToAdd = 0;
    size_t alreadyInSteam = 0;
    size_t needsPrimary = 0;
    size_t needsIdentity = 0;
    size_t needsSteamVerification = 0;
    size_t needsArtwork = 0;
    size_t steamNativeGames = 0;
    size_t conflicts = 0;
    size_t readyNotSelected = 0;
    size_t nonGames = 0;
    size_t resolverRefreshRecommended = 0;
    std::set<std::string> matchedSelections;

    for (const auto& game : state.value("games", json::array())) {
        const auto gameDirectory = fs::path(toWide(game.value("gameDirectory", std::string{})));
        const auto gameDirectoryKey = gameDirectory.empty() ? std::string{} : canonicalPathKey(gameDirectory);
        bool explicitlySelected = !explicitSelection || selectedGameDirectories.count(gameDirectoryKey) != 0;
        if (explicitSelection && selectedGameDirectories.count(gameDirectoryKey)) matchedSelections.insert(gameDirectoryKey);
        for (const auto& alias : game.value("alternateGameDirectories", json::array())) {
            if (!alias.is_string()) continue;
            const auto key = canonicalPathKey(fs::path(toWide(alias.get<std::string>())));
            if (selectedGameDirectories.count(key)) { explicitlySelected = true; matchedSelections.insert(key); }
        }
        const auto executable = fs::path(toWide(jsonStringOr(game, "primaryExecutable")));
        const double primarySelectionConfidence = game.contains("primaryPathEvidence") &&
            game["primaryPathEvidence"].is_object()
                ? jsonDoubleOr(game["primaryPathEvidence"], "confidence", 0.0) : 0.0;
        const double primaryIdentityConfidence = jsonDoubleOr(game, "primaryIdentityConfidence", 0.0);
        json item = {
            {"gameDirectory", game.value("gameDirectory", std::string{})},
            {"directoryName", game.value("directoryName", std::string{})},
            {"primaryExecutable", executable.empty() ? json(nullptr) : json(toUtf8(executable.wstring()))},
            {"primarySelectionStatus", game.value("status", std::string{"unknown"})},
            {"selectionSource", game.value("selectionSource", std::string{})},
            {"primarySelectionConfidence", primarySelectionConfidence},
            {"primarySelectionConfidenceBand", game.contains("primaryPathEvidence") &&
                game["primaryPathEvidence"].is_object()
                    ? jsonStringOr(game["primaryPathEvidence"], "confidenceBand", "unknown")
                    : std::string{"unknown"}},
            {"primaryEvidenceSource", jsonStringOr(game, "primaryEvidenceSource")},
            {"primaryIdentityConfidence", primaryIdentityConfidence},
            {"primaryIdentityConfidenceBand", jsonStringOr(game, "primaryIdentityConfidenceBand", "unknown")},
            {"primaryProductName", jsonStringOr(game, "primaryProductName")},
            {"primaryFileDescription", jsonStringOr(game, "primaryFileDescription")},
            {"selectedForAdd", explicitlySelected},
            {"status", "needs-primary-confirmation"}
        };

        if (game.value("contentType", std::string{}) == "non-game" ||
            game.value("unrecognizedTool", false) ||
            game.value("status", std::string{}) == "unrecognized-tool") {
            item["status"] = "non-game";
            item["reason"] = "scanner-classified-directory-as-non-game-tool";
            item["classification"] = game.value("classification", std::string{"明确非游戏"});
            ++nonGames;
            items.push_back(std::move(item));
            continue;
        }

        const bool nativeSteamGame = game.value("steamNative", false) ||
            game.value("contentType", std::string{}) == "steam-native" ||
            steamNativeGameDirectory(gameDirectory);
        if (nativeSteamGame) {
            item["status"] = "already-in-steam";
            item["reason"] = "steam-native-game-excluded";
            item["steamNative"] = true;
            item["steamOwnership"] = "native-steam";
            const int nativeAppId = game.value("nativeSteamAppId", 0);
            item["nativeSteamAppId"] = nativeAppId;
            item["steamStoreAppId"] = nativeAppId;
            item["storefrontAppId"] = nativeAppId;
            item["nativeSteamExecutable"] = executable.empty() ? json(nullptr) : json(toUtf8(executable.wstring()));
            item["nativeSteamGameDirectory"] = toUtf8(gameDirectory.wstring());
            item["nativeSteamManifestPath"] = jsonStringOr(game, "nativeSteamManifestPath");
            item["nativeSteam"] = game.contains("nativeSteam") && game["nativeSteam"].is_object()
                ? game["nativeSteam"] : json{
                    {"appId", nativeAppId},
                    {"executablePath", executable.empty() ? json(nullptr) : json(toUtf8(executable.wstring()))},
                    {"gameDirectory", toUtf8(gameDirectory.wstring())},
                    {"manifestPath", jsonStringOr(game, "nativeSteamManifestPath")},
                    {"manifestName", jsonStringOr(game, "directoryName")}
                };
            ++steamNativeGames;
            items.push_back(std::move(item));
            continue;
        }

        if (game.value("status", std::string{}) != "ready" || executable.empty() || !fs::is_regular_file(executable)) {
            ++needsPrimary;
            items.push_back(std::move(item));
            continue;
        }

        const auto existingIterator = existingByExecutable.find(canonicalPathKey(executable));
        if (existingIterator != existingByExecutable.end()) {
            const auto ids = shortcutIds(existingIterator->second);
            item["status"] = "already-in-steam";
            item["formalName"] = existingIterator->second.appName;
            item["shortAppId"] = std::to_string(ids.shortId);
            item["longAppId"] = std::to_string(ids.longId);
            item["storedAppId"] = existingIterator->second.storedAppId;
            const auto existingGrid = existingIterator->second.shortcutsFile.parent_path() / L"grid";
            item["gridDirectory"] = toUtf8(existingGrid.wstring());
            item["artwork"] = artworkPresence(existingGrid, ids);
            ++alreadyInSteam;
            items.push_back(std::move(item));
            continue;
        }

        auto manifest = portableArtworkManifest(dataRoot, game, executable);
        const auto manualArtwork = manualArtworkForExecutable(manualOverrides, executable);
        const bool idCleared = jsonBoolSafe(manualArtwork, "idCleared", false);
        if (idCleared && manifest) (*manifest)["match"] = json::object();
        if (!idCleared && (!manifest || !custom_steam_library::verifiedSteamIdentityMatch(manifest->value("match", json::object())))) {
            const auto cached = cachedVerifiedSteamIdentity(dataRoot, executable);
            if (cached) {
                if (!manifest) manifest = json{{"exe", toUtf8(executable.wstring())}, {"artwork", json::array()}};
                (*manifest)["match"] = (*cached)["match"];
                if (cached->contains("metadata") && (*cached)["metadata"].is_object()) (*manifest)["metadata"] = (*cached)["metadata"];
                item["identitySource"] = "verified-identity-cache";
            }
        }
        for (const auto& [type, field] : {std::pair{"cover", "manualCoverPath"}, std::pair{"long", "manualLongPath"}, std::pair{"wallpaper", "manualWallpaperPath"}}) {
            const auto source = manualArtworkSource(manualArtwork, type);
            if (!source.empty()) item[field] = toUtf8(source.wstring());
        }
        if (!manifest) {
            if (!explicitSelection || !explicitlySelected) {
                item["status"] = "needs-identity-and-artwork";
                item["suggestedName"] = game.value("directoryName", std::string{});
                ++needsIdentity;
                items.push_back(std::move(item));
                continue;
            }
            // Import is intentionally unrestricted once the scanner has
            // explicitly selected it from the waiting queue. Artwork and
            // SteamID are optional metadata for this manual import path.
            const auto fallbackName = game.value("directoryName", std::string{"未命名游戏"});
            const auto quotedExecutable = quotedSteamPath(executable);
            const auto quotedStartDirectory = quotedSteamPath(executable.parent_path());
            const int32_t signedAppId = steamShortcutAppId(quotedExecutable, fallbackName);
            uint32_t shortAppId = 0;
            std::memcpy(&shortAppId, &signedAppId, sizeof(shortAppId));
            const uint64_t longAppId = (static_cast<uint64_t>(shortAppId) << 32) | 0x02000000ull;
            const auto collision = existingById.find(shortAppId);
            if (collision != existingById.end()) {
                item["status"] = "shortcut-id-collision";
                item["conflictingAppName"] = collision->second.appName;
                item["conflictingExecutable"] = collision->second.exe;
                item["shortAppId"] = std::to_string(shortAppId);
                ++conflicts;
                items.push_back(std::move(item));
                continue;
            }
            item["status"] = explicitlySelected ? "ready-to-add" : "ready-not-selected";
            item["formalName"] = fallbackName;
            item["displayName"] = fallbackName;
            item["quotedExecutable"] = quotedExecutable;
            item["quotedStartDirectory"] = quotedStartDirectory;
            item["storedAppId"] = signedAppId;
            item["shortAppId"] = std::to_string(shortAppId);
            item["longAppId"] = std::to_string(longAppId);
            if (explicitlySelected) ++readyToAdd; else ++readyNotSelected;
            items.push_back(std::move(item));
            continue;
        }

        // Keep verified release metadata in the plan item as well as in the
        // portable manifest. The workspace UI consumes plan items as its
        // current Steam snapshot, so dropping this field here made every
        // correctly scraped year look missing in the editor.
        const auto manifestMetadata = manifest->value("metadata", json::object());
        const int manifestReleaseYear = releaseYearFromValue(
            manifestMetadata.is_object() ? manifestMetadata.value("releaseDate", json(nullptr)) : json(nullptr));
        if (manifestReleaseYear > 0) {
            item["year"] = manifestReleaseYear;
            item["releaseYear"] = manifestReleaseYear;
            const auto display = releaseDisplayFromValue(
                manifestMetadata.is_object() ? manifestMetadata.value("releaseDate", json(nullptr)) : json(nullptr));
            if (!display.empty()) item["releaseDate"] = display;
        }

        const auto match = manifest->value("match", json::object());
        const auto identityStatus = jsonStringOr(match, "identityStatus");
        const auto steamVerificationStatus = jsonStringOr(match, "steamVerificationStatus");
        const auto metadataVerificationStatus = jsonStringOr(match, "metadataVerificationStatus");
        const auto resolverVersion = jsonStringOr(match, "resolverVersion");
        const auto identityProvider = jsonStringOr(match, "primaryProvider");
        const int canonicalAppId = positiveJsonInt(match, "appId");
        const uint64_t igdbId = static_cast<uint64_t>(positiveJsonInt(match, "igdbId"));
        const int matchedContentAppId = positiveJsonInt(match, "matchedContentAppId");
        const int baseGameAppId = positiveJsonInt(match, "baseGameAppId");
        const bool contentRelationValid = matchedContentAppId <= 0 ||
            (baseGameAppId > 0 && !jsonStringOr(match, "contentRelation").empty() &&
             jsonStringOr(match, "identityKind").find("with-explicit-content") != std::string::npos);
        const bool currentResolver = resolverVersion == STEAM_RESOLVER_VERSION;
        const bool compatibleVerifiedV2 = resolverVersion == "steam-official-fuzzy-global-v2" &&
            identityStatus == "verified" && !identityProvider.empty() &&
            identityProvider == "steam" && canonicalAppId > 0 && contentRelationValid;
        const bool currentSteamVerified = currentResolver &&
            steamVerificationStatus == "steam-verified" && identityProvider == "steam" &&
            canonicalAppId > 0 && contentRelationValid;
        const bool steamIdentityReady = currentSteamVerified || compatibleVerifiedV2;
        // A non-Steam metadata provider may still have positively identified
        // the executable as a game while Steam's official AppID verification
        // is unavailable.  This is a safe artwork-only signal: it is not
        // sufficient for Steam import, and it must never be inferred from a
        // title, candidate AppID, or a network-pending response alone.
        const bool metadataGameSignal =
            identityProvider == "playnite-igdb" &&
            metadataVerificationStatus == "metadata-verified" &&
            identityStatus == "metadata-verified" &&
            igdbId > 0;
        item["identityStatus"] = identityStatus.empty() ? "legacy-unverified" : identityStatus;
        item["steamVerificationStatus"] = steamVerificationStatus.empty()
            ? "legacy-unknown" : steamVerificationStatus;
        item["metadataVerificationStatus"] = metadataVerificationStatus.empty()
            ? "legacy-unknown" : metadataVerificationStatus;
        item["metadataGameSignal"] = metadataGameSignal;
        item["artworkFallbackEligible"] = metadataGameSignal;
        item["identityKind"] = jsonStringOr(match, "identityKind");
        item["contentRelation"] = match.contains("contentRelation") ? match["contentRelation"] : json(nullptr);
        item["identityConfidence"] = jsonDoubleOr(match, "score", 0.0);
        item["resolverVersion"] = resolverVersion.empty() ? json(nullptr) : json(resolverVersion);
        item["currentResolverVersion"] = STEAM_RESOLVER_VERSION;
        item["resolverRefreshRecommended"] = compatibleVerifiedV2;
        item["identityTrust"] = compatibleVerifiedV2 ? "legacy-compatible-v2" :
            (currentSteamVerified ? "current-v4-steam-verified" :
             (currentResolver ? "current-v4-metadata-or-pending" : "untrusted-or-unknown"));
        if (compatibleVerifiedV2) ++resolverRefreshRecommended;
        const bool unrestrictedImport = explicitSelection && explicitlySelected;
        if (!unrestrictedImport && (!steamIdentityReady || (!currentResolver && !compatibleVerifiedV2) ||
            identityProvider.empty() || (canonicalAppId <= 0 && igdbId == 0) || !contentRelationValid)) {
            const bool metadataOnly = metadataVerificationStatus == "metadata-verified" && !steamIdentityReady;
            const bool pendingNetwork = steamVerificationStatus == "network-pending";
            const auto artwork = artworkReadiness(*manifest, manualArtwork);
            item["artwork"] = artwork;
            item["artworkFallbackComplete"] = artwork.value("minimumComplete", false);
            item["status"] = artwork.value("minimumComplete", false)
                ? "waiting-steam-verification"
                : (metadataOnly || pendingNetwork
                    ? "needs-steam-verification" : "needs-identity-confirmation");
            item["reason"] = identityStatus.empty() ?
                "legacy-artwork-manifest-must-be-reidentified-with-current-resolver" :
                ((!currentResolver && !compatibleVerifiedV2) ? "resolver-version-requires-reidentification" :
                 (!contentRelationValid ? "matched-content-relation-is-incomplete" :
                  (pendingNetwork ? "steam-verification-pending-network" :
                   (metadataOnly ? "metadata-verified-without-steam-identity" :
                    "steam-identity-is-not-verified"))));
            item["artworkManifest"] = manifest->value("portableManifestPath", std::string{});
            // Keep artwork readiness visible even before SteamID verification.
            // The second automatic round may use IGDB for these missing slots,
            // but this metadata never grants Steam import eligibility.
            if (metadataGameSignal) {
                // Keep the positively identified title and current artwork
                // readiness visible even though this item remains in the
                // processing bucket until a SteamID is officially verified.
                const auto formalName = trim(jsonStringOr(match, "displayName", jsonStringOr(match, "formalName")));
                item["formalName"] = formalName;
                item["displayName"] = formalName;
                item["igdbId"] = igdbId;
                item["artworkFallbackProvider"] = identityProvider;
                const auto manualCover = manualArtworkSource(manualArtwork, "cover");
                const auto manualWallpaper = manualArtworkSource(manualArtwork, "wallpaper");
                if (!manualCover.empty()) item["manualCoverPath"] = toUtf8(manualCover.wstring());
                if (!manualWallpaper.empty()) item["manualWallpaperPath"] = toUtf8(manualWallpaper.wstring());
            }
            ++needsIdentity;
            if (!steamIdentityReady) ++needsSteamVerification;
            items.push_back(std::move(item));
            continue;
        }
        const auto scrapedFormalName = trim(jsonStringOr(match, "displayName", jsonStringOr(match, "formalName")));
        const auto formalName = scrapedFormalName.empty()
            ? game.value("directoryName", std::string{"未命名游戏"}) : scrapedFormalName;
        if (formalName.empty() && !unrestrictedImport) {
            item["status"] = "needs-identity-confirmation";
            item["reason"] = "verified-identity-has-no-display-name";
            item["artworkManifest"] = manifest->value("portableManifestPath", std::string{});
            ++needsIdentity;
            items.push_back(std::move(item));
            continue;
        }

        const auto artwork = artworkReadiness(*manifest, manualArtwork);
        item["formalName"] = formalName;
        item["displayName"] = formalName;
        item["steamStoreAppId"] = match.contains("appId") && match["appId"].is_number_integer()
            ? match["appId"] : json(nullptr);
        item["storefrontAppId"] = match.contains("storefrontAppId") && match["storefrontAppId"].is_number_integer()
            ? match["storefrontAppId"] : json(nullptr);
        item["matchedContentAppId"] = match.contains("matchedContentAppId") &&
            match["matchedContentAppId"].is_number_integer() ? match["matchedContentAppId"] : json(nullptr);
        item["baseGameAppId"] = match.contains("baseGameAppId") && match["baseGameAppId"].is_number_integer()
            ? match["baseGameAppId"] : json(nullptr);
        item["igdbId"] = igdbId > 0 ? json(igdbId) : json(nullptr);
        item["artwork"] = artwork;
        item["artworkManifest"] = manifest->value("portableManifestPath", std::string{});
        const auto manualCover = manualArtworkSource(manualArtwork, "cover");
        const auto manualWallpaper = manualArtworkSource(manualArtwork, "wallpaper");
        if (!manualCover.empty()) item["manualCoverPath"] = toUtf8(manualCover.wstring());
        if (!manualWallpaper.empty()) item["manualWallpaperPath"] = toUtf8(manualWallpaper.wstring());
        if (!unrestrictedImport && !artwork.value("minimumComplete", false)) {
            item["status"] = "needs-minimum-artwork";
            ++needsArtwork;
            items.push_back(std::move(item));
            continue;
        }

        const auto quotedExecutable = quotedSteamPath(executable);
        const auto quotedStartDirectory = quotedSteamPath(executable.parent_path());
        const int32_t signedAppId = steamShortcutAppId(quotedExecutable, formalName);
        uint32_t shortAppId = 0;
        std::memcpy(&shortAppId, &signedAppId, sizeof(shortAppId));
        const uint64_t longAppId = (static_cast<uint64_t>(shortAppId) << 32) | 0x02000000ull;
        const auto collision = existingById.find(shortAppId);
        if (collision != existingById.end()) {
            item["status"] = "shortcut-id-collision";
            item["conflictingAppName"] = collision->second.appName;
            item["conflictingExecutable"] = collision->second.exe;
            item["shortAppId"] = std::to_string(shortAppId);
            ++conflicts;
            items.push_back(std::move(item));
            continue;
        }

        item["status"] = "ready-to-add";
        item["quotedExecutable"] = quotedExecutable;
        item["quotedStartDirectory"] = quotedStartDirectory;
        item["storedAppId"] = signedAppId;
        item["shortAppId"] = std::to_string(shortAppId);
        item["longAppId"] = std::to_string(longAppId);
        if (!explicitlySelected) {
            item["status"] = "ready-not-selected";
            ++readyNotSelected;
            items.push_back(std::move(item));
            continue;
        }
        ++readyToAdd;
        items.push_back(std::move(item));
    }

    json requestedSelections = json::array();
    json unmatchedSelections = json::array();
    for (const auto& key : selectedGameDirectories) {
        requestedSelections.push_back(key);
        if (!matchedSelections.count(key)) unmatchedSelections.push_back(key);
    }

    json plan = {
        {"schemaVersion", 1},
        {"isolated", true},
        {"mode", "steam-library-add-plan"},
        {"createdAt", unixTimeMs()},
        {"dataRoot", toUtf8(dataRoot.wstring())},
        {"libraryState", toUtf8(statePath.wstring())},
        {"shortcutsVdf", toUtf8(target.path.wstring())},
        {"shortcutsVdfExists", shortcutsVdfExists},
        {"shortcutsBeforeSha256", sha256(originalVdf)},
        {"accountId", target.accountId},
        {"realSteamTarget", realSteamShortcutsPath(target.path)},
        {"steamRunning", processRunningByName(L"steam.exe")},
        {"requiresSteamClosedForCommit", realSteamShortcutsPath(target.path)},
        {"selectionMode", explicitSelection ? "explicit-game-directories" : "all-ready"},
        {"requestedSelections", requestedSelections},
        {"unmatchedSelections", unmatchedSelections},
        {"summary", {
            {"scannedGames", state.value("games", json::array()).size()},
            {"readyToAdd", readyToAdd},
            {"readyNotSelected", readyNotSelected},
            {"alreadyInSteam", alreadyInSteam},
            {"needsPrimaryConfirmation", needsPrimary},
            {"needsIdentity", needsIdentity},
            {"needsSteamVerification", needsSteamVerification},
            {"needsMinimumArtwork", needsArtwork},
            {"steamNativeGames", steamNativeGames},
            {"nonGames", nonGames},
            {"resolverRefreshRecommended", resolverRefreshRecommended},
            {"shortcutIdConflicts", conflicts}
        }},
        {"items", items}
    };
    writeJsonAtomic(outputPath, plan);
    return plan;
}

struct StagedSteamArtwork {
    fs::path temporary;
    fs::path target;
};

static std::vector<StagedSteamArtwork> stageSteamArtworkForAddPlan(
    const json& plan,
    const fs::path& targetGrid) {
    fs::create_directories(targetGrid);
    std::vector<StagedSteamArtwork> staged;
    ScopedStagedFiles cleanupOnFailure;
    std::set<std::string> targets;
    const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(unixTimeMs());
    auto stageOne = [&](const fs::path& source, const fs::path& target) {
        if (source.empty() || !fs::is_regular_file(source)) {
            throw std::runtime_error("Portable artwork source is missing before Steam commit");
        }
        if (canonicalPathKey(target.parent_path()) != canonicalPathKey(targetGrid) || !supportedSteamArtworkExtension(target)) throw std::runtime_error("Steam artwork target escaped grid directory or uses unsupported extension");
        const auto key = canonicalPathKey(target);
        if (!targets.insert(key).second || fs::is_regular_file(target)) return;
        const auto temporary = fs::path(target.wstring() + L".yeman-stage-" + token);
        writeAtomic(temporary, readBinaryFile(source, MAX_IMAGE_BYTES));
        staged.push_back({temporary, target});
        cleanupOnFailure.add(temporary);
    };

    for (const auto& item : plan.value("items", json::array())) {
        if (item.value("status", std::string{}) != "ready-to-add") continue;
        const auto shortId = item.value("shortAppId", std::string{});
        const auto longId = item.value("longAppId", std::string{});
        const auto manualCover = fs::path(toWide(jsonStringOr(item, "manualCoverPath")));
        const auto manualLong = fs::path(toWide(jsonStringOr(item, "manualLongPath")));
        const auto manualWallpaper = fs::path(toWide(jsonStringOr(item, "manualWallpaperPath")));
        if (!manualCover.empty() && fs::is_regular_file(manualCover)) {
            stageOne(manualCover, targetGrid / toWide(shortId + "p" + asciiLower(toUtf8(manualCover.extension().wstring()))));
        }
        if (!manualLong.empty() && fs::is_regular_file(manualLong)) {
            const auto extension = asciiLower(toUtf8(manualLong.extension().wstring()));
            stageOne(manualLong, targetGrid / toWide(shortId + extension));
            stageOne(manualLong, targetGrid / toWide(longId + extension));
        }
        if (!manualWallpaper.empty() && fs::is_regular_file(manualWallpaper)) {
            const auto extension = asciiLower(toUtf8(manualWallpaper.extension().wstring()));
            stageOne(manualWallpaper, targetGrid / toWide(shortId + "_hero" + extension));
        }
        const auto manifestPath = fs::path(toWide(item.value("artworkManifest", std::string{})));
        // A shortcut does not require artwork. No manifest is expected for
        // games that were imported before scraping completed.
        if (!fs::is_regular_file(manifestPath)) continue;
        const auto manifest = loadJsonDocument(manifestPath);
        for (const auto& artwork : manifest.value("artwork", json::array())) {
            if (!jsonBoolSafe(artwork, "ok", false)) continue;
            const auto source = manifestArtworkSource(artwork);
            if (source.empty()) continue;
            const auto type = jsonStringOr(artwork, "type");
            const auto suffix = jsonStringOr(artwork, "steamSuffix");
            if (!validSteamArtworkSuffix(type, suffix)) throw std::runtime_error("Invalid Steam artwork suffix");
            const auto extension = asciiLower(toUtf8(source.extension().wstring()));
            stageOne(source, targetGrid / toWide(shortId + suffix + extension));
            if (type == "long") stageOne(source, targetGrid / toWide(longId + extension));
        }
    }
    cleanupOnFailure.paths.clear();
    return staged;
}

static json steamTransactionPathList(const std::vector<fs::path>& paths) {
    json result = json::array();
    for (const auto& path : paths) {
        if (!path.empty()) result.push_back(toUtf8(path.wstring()));
    }
    return result;
}

static size_t cleanupInterruptedTemporaryFiles(const fs::path& dataRoot) noexcept {
    size_t removed = 0;
    try {
        SteamShortcutWriterMutex writer(0);
        DataTransactionMutex transaction(0);
        std::vector<fs::path> candidates;
        std::error_code iteratorError;
        if (!fs::is_directory(dataRoot, iteratorError)) return 0;
        for (fs::recursive_directory_iterator iterator(
                 dataRoot, fs::directory_options::skip_permission_denied, iteratorError), end;
             iterator != end; iterator.increment(iteratorError)) {
            if (iteratorError) { iteratorError.clear(); continue; }
            if (isReparsePoint(iterator->path()) || (iterator->is_directory(iteratorError) && iterator->path().filename() == L"backups")) { iterator.disable_recursion_pending(); continue; }
            if (!iterator->is_regular_file(iteratorError) || iteratorError) {
                iteratorError.clear();
                continue;
            }
            const auto name = iterator->path().filename().wstring();
            const bool temporary = name.find(L".yeman-tmp-") != std::wstring::npos;
            const bool staged = name.find(L".yeman-stage-") != std::wstring::npos ||
                name.find(L".yeman-delete-stage-") != std::wstring::npos;
            if (temporary || staged) candidates.push_back(iterator->path());
        }
        std::error_code cleanup;
        for (const auto& path : candidates) {
            if (fs::remove(path, cleanup)) ++removed;
            cleanup.clear();
        }
    } catch (...) {
        // A locked or inaccessible stale file is retained for a later pass.
    }
    return removed;
}

static void cleanupInterruptedSteamTransactions(const fs::path& dataRoot) noexcept {
    try {
        SteamShortcutWriterMutex writer(0);
        DataTransactionMutex transaction(0);
        const auto root = dataRoot / L"backups" / L"steam-shortcuts";
        std::error_code iteratorError;
        if (!fs::is_directory(root, iteratorError)) return;
        for (fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, iteratorError), end;
             iterator != end; iterator.increment(iteratorError)) {
            if (iteratorError) { iteratorError.clear(); continue; }
            if (isReparsePoint(iterator->path())) { iterator.disable_recursion_pending(); continue; }
            if (iterator->path().filename() != L"transaction.pending.json") continue;
            const auto pendingPath = iterator->path();
            try {
                const auto pending = loadJsonDocument(pendingPath, 8u << 20);
                const auto target = fs::path(toWide(jsonStringOr(pending, "targetPath")));
                const auto targetBackup = fs::path(toWide(jsonStringOr(pending, "targetBackup")));
                const auto localConfig = fs::path(toWide(jsonStringOr(pending, "localConfigPath")));
                const auto localConfigBackup = fs::path(toWide(jsonStringOr(pending, "localConfigBackup")));
                if (target.empty() || target.filename() != L"shortcuts.vdf" || target.parent_path().filename() != L"config" ||
                    (!realSteamShortcutsPath(target) && !pathWithin(target, dataRoot)) ||
                    canonicalPathKey(targetBackup) != canonicalPathKey(pendingPath.parent_path() / L"shortcuts.before.vdf")) {
                    throw std::runtime_error("Invalid Steam recovery target or backup binding");
                }
                if (!localConfig.empty() && (canonicalPathKey(localConfig) != canonicalPathKey(target.parent_path() / L"localconfig.vdf") ||
                    canonicalPathKey(localConfigBackup) != canonicalPathKey(pendingPath.parent_path() / L"localconfig.before.vdf"))) {
                    throw std::runtime_error("Invalid Steam recovery config binding");
                }
                const auto grid = target.parent_path() / L"grid";
                const auto artworkTargets = pending.value("artworkTargets", json::array());
                const auto stagedFiles = pending.value("stagedFiles", json::array());
                const auto artworkBackups = pending.value("artworkBackups", json::array());
                if (!artworkTargets.is_array() || !stagedFiles.is_array() || !artworkBackups.is_array()) throw std::runtime_error("Invalid Steam recovery inventory");
                for (const auto& entry : artworkBackups) {
                    if (!entry.is_object()) throw std::runtime_error("Invalid artwork backup entry");
                    const auto original = fs::path(toWide(jsonStringOr(entry, "target")));
                    const auto backup = fs::path(toWide(jsonStringOr(entry, "backup")));
                    if (original.empty() || canonicalPathKey(original.parent_path()) != canonicalPathKey(grid) ||
                        !supportedSteamArtworkExtension(original) ||
                        canonicalPathKey(backup) != canonicalPathKey(pendingPath.parent_path() / L"grid-before" / original.filename())) {
                        throw std::runtime_error("Invalid artwork recovery backup binding");
                    }
                }
                for (const auto& value : artworkTargets) {
                    if (!value.is_string()) throw std::runtime_error("Invalid recovery artwork path");
                    const auto path = fs::path(toWide(value.get<std::string>()));
                    if (canonicalPathKey(path.parent_path()) != canonicalPathKey(grid)) throw std::runtime_error("Recovery artwork escaped grid");
                }
                for (const auto& value : stagedFiles) {
                    if (!value.is_string()) throw std::runtime_error("Invalid recovery stage path");
                    const auto path = fs::path(toWide(value.get<std::string>()));
                    const auto name = path.filename().wstring();
                    const bool shortcutStage = canonicalPathKey(path.parent_path()) == canonicalPathKey(target.parent_path()) &&
                        (name.starts_with(L"shortcuts.vdf.yeman-stage-") || name.starts_with(L"shortcuts.vdf.yeman-delete-stage-") || name.starts_with(L"localconfig.vdf.yeman-stage-"));
                    const bool artworkStage = canonicalPathKey(path.parent_path()) == canonicalPathKey(grid) && name.find(L".yeman-stage-") != std::wstring::npos;
                    if (!shortcutStage && !artworkStage) throw std::runtime_error("Recovery stage escaped Steam account");
                }
                const auto completedPath = pendingPath.parent_path() / L"transaction.complete.json";
                auto removeChecked = [](const fs::path& path) { std::error_code error; fs::remove(path, error); if (error) throw std::runtime_error("Recovery file could not be removed"); };
                if (fs::is_regular_file(completedPath)) {
                    const auto completed = loadJsonDocument(completedPath, 1u << 20);
                    if (jsonStringOr(completed, "targetPath") != jsonStringOr(pending, "targetPath")) throw std::runtime_error("Recovery completion marker mismatch");
                    for (const auto& value : stagedFiles) removeChecked(fs::path(toWide(value.get<std::string>())));
                    removeChecked(pendingPath);
                    continue;
                }
                if (processRunningByName(L"steam.exe")) continue;
                const bool targetExisted = pending.value("targetExisted", false);
                const bool localConfigExisted = pending.value("localConfigExisted", false);
                // Validate every preimage before modifying any recovery target.
                const auto shortcutBytes = targetExisted ? readBinaryFile(targetBackup, 64u << 20) : std::vector<unsigned char>{};
                const auto localBytes = !localConfig.empty() && localConfigExisted ? readBinaryFile(localConfigBackup, 64u << 20) : std::vector<unsigned char>{};
                std::vector<std::pair<fs::path, std::vector<unsigned char>>> artworkPreimages;
                for (const auto& entry : artworkBackups) {
                    const auto bytes = readBinaryFile(fs::path(toWide(jsonStringOr(entry, "backup"))), MAX_IMAGE_BYTES);
                    if (sha256(bytes) != jsonStringOr(entry, "sha256")) throw std::runtime_error("Artwork recovery backup hash mismatch");
                    artworkPreimages.push_back({fs::path(toWide(jsonStringOr(entry, "target"))), bytes});
                }
                if (pending.value("shortcutsModified", true)) {
                    if (targetExisted) writeAtomic(target, shortcutBytes);
                    else removeChecked(target);
                }
                if (!localConfig.empty()) {
                    if (localConfigExisted) writeAtomic(localConfig, localBytes);
                    else removeChecked(localConfig);
                }
                for (const auto& value : artworkTargets) removeChecked(fs::path(toWide(value.get<std::string>())));
                for (const auto& [original, bytes] : artworkPreimages) writeAtomic(original, bytes);
                for (const auto& value : stagedFiles) removeChecked(fs::path(toWide(value.get<std::string>())));
                writeJsonAtomic(pendingPath.parent_path() / L"transaction.recovered.json", {
                    {"schemaVersion", 1}, {"recoveredAt", unixTimeMs()}, {"targetPath", toUtf8(target.wstring())}, {"reason", "workspace-startup-recovery"}
                });
                removeChecked(pendingPath);
            } catch (...) { /* Keep unresolved journals and backups for another open or explicit repair. */ }
        }
    } catch (...) { /* Startup recovery is best-effort, never destructive guesswork. */ }
}

// Separate same-account transactions even when they occur in one millisecond.
static fs::path steamTransactionDirectory(const fs::path& dataRoot, std::string account) {
    for (auto& ch : account) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
    if (account.empty()) account = "account";
    static std::atomic<uint64_t> sequence{0};
    return dataRoot / L"backups" / L"steam-shortcuts" / toWide(account) /
        (std::to_wstring(unixTimeMs()) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence));
}

static void requireResolvedSteamTransactions(const fs::path& dataRoot, const fs::path& target) {
    const auto root = dataRoot / L"backups" / L"steam-shortcuts";
    if (!fs::is_directory(root)) return;
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (isReparsePoint(it->path())) { it.disable_recursion_pending(); continue; }
        if (it->path().filename() != L"transaction.pending.json") continue;
        const auto pending = loadJsonDocument(it->path(), 8u << 20);
        const auto recordedTarget = fs::path(toWide(jsonStringOr(pending, "targetPath")));
        if (recordedTarget.empty()) throw std::runtime_error("Steam 恢复日志损坏，请先执行恢复，已拒绝新的写入");
        if (canonicalPathKey(recordedTarget) != canonicalPathKey(target)) continue;
        const auto complete = it->path().parent_path() / L"transaction.complete.json";
        if (fs::is_regular_file(complete) && jsonStringOr(loadJsonDocument(complete), "targetPath") == jsonStringOr(pending, "targetPath")) continue;
        throw std::runtime_error("此 Steam 账户存在未完成的恢复事务，请先关闭 Steam 并执行恢复，已拒绝覆盖");
    }
}

static void publishCommittedSteamResult(json& result, std::initializer_list<fs::path> paths) {
    // Once the durable completion marker exists, a UI publication failure
    // must not be reported as a failed/rolled-back Steam operation.
    for (const auto& path : paths) {
        try { writeJsonAtomic(path, result); }
        catch (const std::exception& error) {
            if (!result.contains("resultPublicationWarnings")) result["resultPublicationWarnings"] = json::array();
            result["resultPublicationWarnings"].push_back({{"path", toUtf8(path.wstring())}, {"error", error.what()}});
            std::cerr << "WARNING_RESULT_PUBLICATION: " << error.what() << "\n";
        }
    }
}

// Non-Steam icons require both a grid file and the shortcuts.vdf icon field.
// Never replace an explicit icon choice. Bind only an empty field, in the same
// Steam-closed transaction as the artwork so rollback covers both resources.
static bool bindAutomaticSteamShortcutIcon(
    BinaryVdfDocument& document, const fs::path& executable, const fs::path& iconPath) {
    if (executable.empty() || iconPath.empty()) return false;
    auto* shortcuts = vdfFindObject(document.root(), "shortcuts");
    if (!shortcuts) throw std::runtime_error("Binary VDF has no shortcuts object");
    const auto executableKey = canonicalPathKey(executable);
    BinaryVdfNode* selected = nullptr;
    for (auto& [index, node] : shortcuts->objectValue) {
        if (node.type != 0) continue;
        for (const auto& [name, value] : node.objectValue) {
            if (asciiLower(name) != "exe" || value.type != 1) continue;
            const auto path = expandEnvironmentPath(unquote(value.stringValue));
            if (path.empty() || canonicalPathKey(path) != executableKey) continue;
            if (selected && selected != &node) throw std::runtime_error("Ambiguous Steam shortcut icon target");
            selected = &node;
        }
    }
    if (!selected) return false;
    // Duplicated/malformed icon keys are not safe to overwrite automatically.
    BinaryVdfNode* icon = nullptr;
    for (auto& [name, value] : selected->objectValue) {
        if (asciiLower(name) != "icon") continue;
        if (icon || value.type != 1 || !trim(value.stringValue).empty()) return false;
        icon = &value;
    }
    if (icon) icon->stringValue = toUtf8(iconPath.wstring());
    else selected->objectValue.push_back({"icon", vdfString(toUtf8(iconPath.wstring()))});
    return true;
}

static fs::path automaticSteamIconForAddItem(const json& item, const fs::path& grid) {
    const auto manifestPath = fs::path(toWide(jsonStringOr(item, "artworkManifest")));
    if (!fs::is_regular_file(manifestPath)) return {};
    const auto manifest = loadJsonDocument(manifestPath);
    for (const auto& artwork : manifest.value("artwork", json::array())) {
        if (!jsonBoolSafe(artwork, "ok", false) || jsonStringOr(artwork, "type") != "icon" ||
            jsonStringOr(artwork, "steamSuffix") != "_icon") continue;
        const auto source = manifestArtworkSource(artwork);
        if (source.empty() || !supportedSteamArtworkExtension(source)) continue;
        return grid / toWide(jsonStringOr(item, "shortAppId") + "_icon" + asciiLower(toUtf8(source.extension().wstring())));
    }
    return {};
}

static json commitPreparedSteamAddPlan(
    const fs::path& dataRoot,
    const SteamShortcutTarget& target,
    const fs::path& planPath,
    bool steamClosedConfirmed,
    json plan) {
    const ULONGLONG mutexStarted = GetTickCount64();
    SteamShortcutWriterMutex writer;
    DataTransactionMutex transaction;
    requireResolvedSteamTransactions(dataRoot, target.path);

    plan["transactionSerialization"] = "global-all-steam-accounts";
    plan["transactionMutexWaitMs"] = GetTickCount64() - mutexStarted;
    std::string accountFileToken = target.accountId;
    for (auto& ch : accountFileToken) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
    }
    if (accountFileToken.empty()) accountFileToken = "account";
    const bool realTarget = realSteamShortcutsPath(target.path);
    plan["realSteamTarget"] = realTarget;
    if (realTarget && !steamClosedConfirmed) {
        throw std::runtime_error("真实 Steam 写入必须先确认 Steam 已关闭");
    }
    if (realTarget && processRunningByName(L"steam.exe")) {
        throw std::runtime_error("Steam 仍在运行，请先关闭 Steam 再写入 shortcuts.vdf");
    }
    if (plan["summary"].value("shortcutIdConflicts", 0) != 0) {
        throw std::runtime_error("Steam 快捷方式 AppID 冲突，需要手动确认");
    }
    if (!plan.value("unmatchedSelections", json::array()).empty()) {
        throw std::runtime_error("One or more --select-game directories are not present in the library state");
    }
    const size_t addCount = plan["summary"].value("readyToAdd", static_cast<size_t>(0));
    if (addCount == 0) {
        plan["mode"] = "steam-library-add-commit";
        plan["committed"] = false;
        plan["reason"] = "nothing-ready-to-add";
        writeJsonAtomic(dataRoot / L"state" / L"last-steam-add.json", plan);
        writeJsonAtomic(dataRoot / L"state" / toWide("last-steam-add-" + accountFileToken + ".json"), plan);
        return plan;
    }

    const bool shortcutsVdfExists = fs::is_regular_file(target.path);
    const auto original = shortcutsVdfExists
        ? readBinaryFile(target.path, 16u << 20)
        : std::vector<unsigned char>{};
    if (sha256(original) != plan.value("shortcutsBeforeSha256", std::string{})) {
        throw std::runtime_error("shortcuts.vdf changed after planning; regenerate the plan");
    }
    auto document = shortcutsVdfExists
        ? BinaryVdfDocument::parse(original)
        : BinaryVdfDocument::emptySteamShortcuts();
    auto* shortcuts = vdfFindObject(document.root(), "shortcuts");
    if (!shortcuts) throw std::runtime_error("Binary VDF has no shortcuts object");
    const auto simulatedRecentTime = simulatedImportUnixTime();
    int nextIndex = nextSteamShortcutIndex(*shortcuts);
    for (auto& item : plan["items"]) {
        if (item.value("status", std::string{}) != "ready-to-add") continue;
        shortcuts->objectValue.push_back({
            std::to_string(nextIndex++),
            steamShortcutNode(
                item.value("storedAppId", 0),
                item.value("formalName", std::string{}),
                item.value("quotedExecutable", std::string{}),
                item.value("quotedStartDirectory", std::string{}),
                simulatedRecentTime,
                item.value("steamTags", std::vector<std::string>{"YeManCC", "Non-Steam Game"}))
        });
    }

    ScopedStagedFiles stagedCleanup;
    const auto targetGrid = target.path.parent_path() / L"grid";
    const auto localConfigPath = target.path.parent_path() / L"localconfig.vdf";
    const auto recentUpdate = prepareSteamRecentSignalUpdate(
        localConfigPath, plan, simulatedRecentTime, realTarget);
    const bool injectSimulatedRecent = recentUpdate.enabled;
    const auto& originalLocalConfig = recentUpdate.original;
    const auto stagedLocalConfig = injectSimulatedRecent
        ? fs::path(localConfigPath.wstring() + L".yeman-stage-" + std::to_wstring(GetCurrentProcessId()))
        : fs::path{};
    stagedCleanup.add(stagedLocalConfig);
    if (injectSimulatedRecent) {
        writeAtomic(stagedLocalConfig, std::vector<unsigned char>(recentUpdate.updated.begin(), recentUpdate.updated.end()));
    }
    auto stagedArtwork = stageSteamArtworkForAddPlan(plan, targetGrid);
    for (const auto& artwork : stagedArtwork) stagedCleanup.add(artwork.temporary);
    for (auto& item : plan["items"]) {
        if (item.value("status", std::string{}) != "ready-to-add") continue;
        const auto icon = automaticSteamIconForAddItem(item, targetGrid);
        const auto executable = expandEnvironmentPath(unquote(jsonStringOr(item, "quotedExecutable")));
        if (!icon.empty() && bindAutomaticSteamShortcutIcon(document, executable, icon))
            item["automaticIconPath"] = toUtf8(icon.wstring());
    }
    const auto backupDirectory = steamTransactionDirectory(dataRoot, accountFileToken);
    fs::create_directories(backupDirectory);
    const auto backupFile = backupDirectory / L"shortcuts.before.vdf";
    writeAtomic(backupFile, original);
    const auto localConfigBackupFile = injectSimulatedRecent
        ? backupDirectory / L"localconfig.before.vdf" : fs::path{};
    if (injectSimulatedRecent) writeAtomic(localConfigBackupFile, originalLocalConfig);

    const auto stagedVdf = fs::path(target.path.wstring() + L".yeman-stage-" + std::to_wstring(GetCurrentProcessId()));
    stagedCleanup.add(stagedVdf);
    const auto stagedBytes = document.serialize();
    const auto shortcutsAfterHash = sha256(stagedBytes);
    writeAtomic(stagedVdf, stagedBytes);
    // A signed-in Steam account may not have a shortcuts.vdf yet. Steam only
    // creates it after the first non-Steam shortcut is added, so the first
    // import must treat the missing file as an empty shortcut collection.
    const auto beforeCount = shortcutsVdfExists
        ? readShortcuts(target.path, target.accountId).size() : 0u;
    const auto stagedShortcuts = readShortcuts(stagedVdf, target.accountId);
    if (stagedShortcuts.size() != beforeCount + addCount) {
        std::error_code cleanup;
        fs::remove(stagedVdf, cleanup);
        fs::remove(stagedLocalConfig, cleanup);
        for (const auto& artwork : stagedArtwork) fs::remove(artwork.temporary, cleanup);
        throw std::runtime_error("Staged shortcuts.vdf verification failed");
    }

    const auto pendingTransaction = backupDirectory / L"transaction.pending.json";
    std::vector<fs::path> stagedFiles{stagedVdf};
    if (!stagedLocalConfig.empty()) stagedFiles.push_back(stagedLocalConfig);
    for (const auto& artwork : stagedArtwork) stagedFiles.push_back(artwork.temporary);
    writeJsonAtomic(pendingTransaction, {
        {"schemaVersion", 1}, {"createdAt", unixTimeMs()},
        {"targetPath", toUtf8(target.path.wstring())},
        {"targetExisted", shortcutsVdfExists},
        {"targetBackup", toUtf8(backupFile.wstring())},
        {"localConfigPath", injectSimulatedRecent ? json(toUtf8(localConfigPath.wstring())) : json(nullptr)},
        {"localConfigExisted", injectSimulatedRecent},
        {"localConfigBackup", injectSimulatedRecent ? json(toUtf8(localConfigBackupFile.wstring())) : json(nullptr)},
        {"artworkTargets", steamTransactionPathList([&] {
            std::vector<fs::path> values;
            for (const auto& artwork : stagedArtwork) values.push_back(artwork.target);
            return values;
        }())},
        {"stagedFiles", steamTransactionPathList(stagedFiles)}
    });

    std::vector<fs::path> activatedArtwork;
    bool vdfActivated = false;
    bool localConfigActivated = false;
    try {
        activateStagedFile(stagedVdf, target.path, "激活暂存的 shortcuts.vdf");
        vdfActivated = true;
        if (injectSimulatedRecent) activateStagedFile(stagedLocalConfig, localConfigPath, "激活 Steam 导入记录");
        localConfigActivated = injectSimulatedRecent;
        for (const auto& artwork : stagedArtwork) {
            activateStagedFile(artwork.temporary, artwork.target, "激活暂存的 Steam 图片");
            activatedArtwork.push_back(artwork.target);
        }
        if (readShortcuts(target.path, target.accountId).size() != stagedShortcuts.size()) {
            throw std::runtime_error("Committed shortcuts.vdf verification failed");
        }
        // Mark the transaction complete before publishing the UI result.  If
        // output JSON publication is interrupted afterward, the Steam files
        // are already a valid committed set and must not be rolled back.
        writeJsonAtomic(backupDirectory / L"transaction.complete.json", {
            {"schemaVersion", 1}, {"completedAt", unixTimeMs()},
            {"targetPath", toUtf8(target.path.wstring())}
        });
    } catch (...) {
        bool restored = true;
        for (const auto& file : activatedArtwork) {
            std::error_code cleanup; fs::remove(file, cleanup); if (cleanup) restored = false;
        }
        if (vdfActivated) {
            try {
                if (shortcutsVdfExists) writeAtomic(target.path, original);
                else { std::error_code error; fs::remove(target.path, error); if (error) restored = false; }
            } catch (...) { restored = false; }
        }
        if (localConfigActivated) {
            try { writeAtomic(localConfigPath, originalLocalConfig); } catch (...) { restored = false; }
        }
        if (restored) { std::error_code cleanup; fs::remove(pendingTransaction, cleanup); }
        throw;
    }
    {
        std::error_code cleanup;
        fs::remove(pendingTransaction, cleanup);
    }

    plan["mode"] = "steam-library-add-commit";
    plan["committed"] = true;
    plan["committedAt"] = unixTimeMs();
    plan["shortcutsVdfModified"] = true;
    plan["shortcutsAfterSha256"] = shortcutsAfterHash;
    plan["backup"] = toUtf8(backupFile.wstring());
    plan["transactionReceipt"] = toUtf8((backupDirectory / L"transaction.complete.json").wstring());
    plan["recentSignalStatus"] = injectSimulatedRecent ? "updated" : recentUpdate.status;
    plan["recentSignalSkipped"] = realTarget && !injectSimulatedRecent;
    plan["recentSignalSkipDetail"] = realTarget && !injectSimulatedRecent && !recentUpdate.detail.empty()
        ? json(recentUpdate.detail) : json(nullptr);
    plan["simulatedRecentSignal"] = injectSimulatedRecent;
    plan["simulatedRecentSignalTime"] = injectSimulatedRecent ? json(simulatedRecentTime) : json(nullptr);
    plan["simulatedRecentSignalFallbackUsed"] = injectSimulatedRecent && simulatedRecentTime == kSimulatedRecentFallbackUnixTime;
    plan["localConfigVdf"] = injectSimulatedRecent ? json(toUtf8(localConfigPath.wstring())) : json(nullptr);
    plan["localConfigBeforeSha256"] = injectSimulatedRecent ? json(sha256(originalLocalConfig)) : json(nullptr);
    plan["localConfigAfterSha256"] = injectSimulatedRecent ? json(sha256(std::vector<unsigned char>(recentUpdate.updated.begin(), recentUpdate.updated.end()))) : json(nullptr);
    plan["localConfigBackup"] = injectSimulatedRecent ? json(toUtf8(localConfigBackupFile.wstring())) : json(nullptr);
    plan["artworkFilesActivated"] = activatedArtwork.size();
    plan["steamReloadRequired"] = realTarget;
    for (auto& item : plan["items"]) {
        if (item.value("status", std::string{}) != "ready-to-add") continue;
        item["status"] = "added-to-steam";
        item["simulatedRecentSignal"] = injectSimulatedRecent;
        item["simulatedRecentSignalTime"] = injectSimulatedRecent ? json(simulatedRecentTime) : json(nullptr);
    }
    publishCommittedSteamResult(plan, {dataRoot / L"state" / L"last-steam-add.json",
        dataRoot / L"state" / toWide("last-steam-add-" + accountFileToken + ".json"), planPath});
    return plan;
}

static json commitSteamLibraryAddPlan(
    const fs::path& dataRoot,
    const fs::path& statePath,
    const SteamShortcutTarget& target,
    const fs::path& planPath,
    bool steamClosedConfirmed,
    const std::set<std::string>& selectedGameDirectories = {},
    bool explicitSelection = false) {
    return commitPreparedSteamAddPlan(
        dataRoot, target, planPath, steamClosedConfirmed,
        buildSteamLibraryAddPlan(
            dataRoot, statePath, target, planPath, selectedGameDirectories, explicitSelection));
}

static json refreshExistingSteamArtwork(
    const fs::path& dataRoot,
    const fs::path& statePath,
    const SteamShortcutTarget& target,
    const fs::path& outputPath,
    bool steamClosedConfirmed,
    const std::set<std::string>& selectedGameDirectories) {
    SteamShortcutWriterMutex writer;
    DataTransactionMutex transaction;
    requireResolvedSteamTransactions(dataRoot, target.path);
    const bool realTarget = realSteamShortcutsPath(target.path);
    if (realTarget && !steamClosedConfirmed) throw std::runtime_error("真实 Steam 图片更新必须先确认 Steam 已关闭");
    if (realTarget && processRunningByName(L"steam.exe")) throw std::runtime_error("Steam 仍在运行，请先关闭 Steam 再更新图片");
    if (selectedGameDirectories.empty()) throw std::runtime_error("更新 Steam 图标必须选择已加入 Steam 的游戏");

    const auto state = loadLibraryScanState(statePath);
    const auto manualOverrides = loadManualOverrides(dataRoot / L"config" / L"manual-overrides.json");
    const auto originalShortcuts = readBinaryFile(target.path, 16u << 20);
    auto shortcutDocument = BinaryVdfDocument::parse(originalShortcuts);
    size_t iconUpdates = 0;
    const auto shortcuts = readShortcuts(target.path, target.accountId);
    std::map<std::string, SteamShortcut> shortcutsByExecutable;
    for (const auto& shortcut : shortcuts) {
        const auto executable = expandEnvironmentPath(unquote(shortcut.exe));
        if (!executable.empty() && !shortcutsByExecutable.emplace(canonicalPathKey(executable), shortcut).second)
            throw std::runtime_error("同一可执行文件匹配多个 Steam 快捷方式，拒绝覆盖不明确的图标");
    }

    struct PendingArtwork { fs::path source; fs::path target; };
    std::vector<PendingArtwork> pending;
    json items = json::array();
    std::set<std::string> matched;
    for (const auto& game : state.value("games", json::array())) {
        const auto directory = fs::path(toWide(jsonStringOr(game, "gameDirectory")));
        const auto directoryKey = canonicalPathKey(directory);
        if (!selectedGameDirectories.count(directoryKey)) continue;
        matched.insert(directoryKey);
        json item{{"gameDirectory", toUtf8(directory.wstring())}};
        const auto executable = fs::path(toWide(jsonStringOr(game, "primaryExecutable")));
        const auto shortcutIt = shortcutsByExecutable.find(canonicalPathKey(executable));
        if (executable.empty() || shortcutIt == shortcutsByExecutable.end()) {
            item["status"] = "not-in-steam";
            items.push_back(std::move(item));
            continue;
        }
        const auto ids = shortcutIds(shortcutIt->second);
        const auto grid = target.path.parent_path() / L"grid";
        const auto overrideItem = manualArtworkForExecutable(manualOverrides, executable);
        std::map<std::string, fs::path> sources;
        const auto manualCover = manualArtworkSource(overrideItem, "cover");
        const auto manualLong = manualArtworkSource(overrideItem, "long");
        const auto manualWallpaper = manualArtworkSource(overrideItem, "wallpaper");
        if (!manualCover.empty()) sources[std::to_string(ids.shortId) + "p"] = manualCover;
        if (!manualWallpaper.empty()) sources[std::to_string(ids.shortId) + "_hero"] = manualWallpaper;
        if (!manualLong.empty()) {
            sources[std::to_string(ids.shortId)] = manualLong;
            sources[std::to_string(ids.longId)] = manualLong;
        }
        if (const auto manifest = portableArtworkManifest(dataRoot, game, executable)) {
            for (const auto& artwork : manifest->value("artwork", json::array())) {
                if (!jsonBoolSafe(artwork, "ok", false)) continue;
                const auto source = manifestArtworkSource(artwork);
                const auto suffix = jsonStringOr(artwork, "steamSuffix");
                const auto type = jsonStringOr(artwork, "type");
                if (source.empty()) continue;
                if (!validSteamArtworkSuffix(type, suffix)) throw std::runtime_error("Invalid Steam artwork suffix");
                const auto stem = std::to_string(ids.shortId) + suffix;
                if (!sources.count(stem)) sources[stem] = source;
                if (type == "long") sources.emplace(std::to_string(ids.longId), source);
            }
        }
        const auto iconStem = std::to_string(ids.shortId) + "_icon";
        if (const auto icon = sources.find(iconStem); icon != sources.end()) {
            const auto destination = grid / toWide(iconStem + asciiLower(toUtf8(icon->second.extension().wstring())));
            if (supportedSteamArtworkExtension(icon->second) &&
                bindAutomaticSteamShortcutIcon(shortcutDocument, executable, destination)) {
                ++iconUpdates;
                item["automaticIconPath"] = toUtf8(destination.wstring());
            } else {
                // An existing explicit icon wins, including a hand-picked grid icon.
                // Do not overwrite its file even when it uses our usual filename.
                sources.erase(icon);
            }
        }
        size_t changed = 0;
        for (const auto& [stem, source] : sources) {
            if (source.empty() || !fs::is_regular_file(source)) continue;
            pending.push_back({source, grid / toWide(stem + asciiLower(toUtf8(source.extension().wstring())))});
            ++changed;
        }
        item["shortAppId"] = std::to_string(ids.shortId);
        item["status"] = changed ? "ready-to-refresh" : "missing-artwork";
        item["pendingFiles"] = changed;
        items.push_back(std::move(item));
    }
    json unmatched = json::array();
    for (const auto& selected : selectedGameDirectories) if (!matched.count(selected)) unmatched.push_back(selected);
    if (!unmatched.empty()) throw std::runtime_error("所选项目不在当前游戏库，拒绝更新 Steam 图标");
    if (pending.empty()) throw std::runtime_error("所选游戏没有可写入 Steam 的封面或壁纸");

    const auto backupRoot = steamTransactionDirectory(dataRoot, target.accountId);
    fs::create_directories(backupRoot / L"grid-before");
    const auto backupFile = backupRoot / L"shortcuts.before.vdf";
    writeAtomic(backupFile, originalShortcuts);
    ScopedStagedFiles stagedCleanup;
    std::vector<StagedSteamArtwork> staged;
    std::vector<std::pair<fs::path, fs::path>> backups;
    std::vector<fs::path> artworkTargets, stagedFiles;
    json backupInventory = json::array();
    std::set<std::string> handledStems, handledTargets;
    const auto token = backupRoot.filename().wstring();
    const auto grid = target.path.parent_path() / L"grid";
    // Prepare every source and every old image before changing any Steam file.
    for (const auto& item : pending) {
        if (canonicalPathKey(item.target.parent_path()) != canonicalPathKey(grid) || !supportedSteamArtworkExtension(item.target))
            throw std::runtime_error("Invalid Steam artwork destination");
        if (!handledTargets.insert(canonicalPathKey(item.target)).second) continue;
        fs::create_directories(grid);
        const auto temporary = fs::path(item.target.wstring() + L".yeman-stage-" + token);
        stagedCleanup.add(temporary);
        writeAtomic(temporary, readBinaryFile(item.source, MAX_IMAGE_BYTES));
        staged.push_back({temporary, item.target}); stagedFiles.push_back(temporary); artworkTargets.push_back(item.target);
        const auto stem = toUtf8(item.target.stem().wstring());
        if (handledStems.insert(asciiLower(stem)).second) {
            for (const auto& previous : existingArtworkForStem(grid, stem)) {
                const auto backup = backupRoot / L"grid-before" / previous.filename();
                const auto bytes = readBinaryFile(previous, MAX_IMAGE_BYTES);
                writeAtomic(backup, bytes); backups.push_back({previous, backup});
                backupInventory.push_back({{"target", toUtf8(previous.wstring())}, {"backup", toUtf8(backup.wstring())}, {"sha256", sha256(bytes)}});
            }
        }
    }
    const auto stagedVdf = iconUpdates ? fs::path(target.path.wstring() + L".yeman-stage-" + token) : fs::path{};
    if (iconUpdates) {
        stagedCleanup.add(stagedVdf);
        writeAtomic(stagedVdf, shortcutDocument.serialize());
        if (readShortcuts(stagedVdf, target.accountId).size() != shortcuts.size())
            throw std::runtime_error("Staged Steam icon binding verification failed");
        stagedFiles.push_back(stagedVdf);
    }
    const auto pendingTransaction = backupRoot / L"transaction.pending.json";
    writeJsonAtomic(pendingTransaction, {{"schemaVersion", 1}, {"operation", "artwork-refresh"}, {"createdAt", unixTimeMs()},
        {"targetPath", toUtf8(target.path.wstring())}, {"targetExisted", true}, {"shortcutsModified", iconUpdates > 0},
        {"targetBackup", toUtf8(backupFile.wstring())}, {"artworkTargets", steamTransactionPathList(artworkTargets)},
        {"artworkBackups", backupInventory}, {"stagedFiles", steamTransactionPathList(stagedFiles)}});
    std::vector<fs::path> activated;
    bool originalsTouched = false;
    bool vdfActivated = false;
    try {
        if (sha256(readBinaryFile(target.path, 16u << 20)) != sha256(originalShortcuts))
            throw std::runtime_error("shortcuts.vdf changed while preparing artwork; retry the refresh");
        for (const auto& [original, backup] : backups) {
            std::error_code error;
            originalsTouched = true;
            fs::remove(original, error);
            if (error) throw std::runtime_error("无法替换已有 Steam 图标文件");
        }
        for (const auto& item : staged) {
            activateStagedFile(item.temporary, item.target, "激活暂存的 Steam 图片");
            activated.push_back(item.target);
            if (!fs::is_regular_file(item.target)) throw std::runtime_error("Steam 图标写入校验失败");
        }
        if (iconUpdates) {
            activateStagedFile(stagedVdf, target.path, "Activate automatic Steam shortcut icons");
            vdfActivated = true;
            if (readBinaryFile(target.path, 16u << 20) != shortcutDocument.serialize())
                throw std::runtime_error("Committed Steam icon binding verification failed");
        }
        writeJsonAtomic(backupRoot / L"transaction.complete.json", {
            {"schemaVersion", 1}, {"completedAt", unixTimeMs()}, {"targetPath", toUtf8(target.path.wstring())}});
    } catch (...) {
        bool restored = true;
        if (vdfActivated) {
            try { writeAtomic(target.path, originalShortcuts); } catch (...) { restored = false; }
        }
        for (const auto& file : activated) { std::error_code error; fs::remove(file, error); if (error) restored = false; }
        if (originalsTouched) for (const auto& [original, backup] : backups) {
            try { writeAtomic(original, readBinaryFile(backup, MAX_IMAGE_BYTES)); } catch (...) { restored = false; }
        }
        if (restored) { std::error_code cleanup; fs::remove(pendingTransaction, cleanup); }
        throw;
    }
    { std::error_code cleanup; fs::remove(pendingTransaction, cleanup); }
    json result{{"schemaVersion", 1}, {"mode", "steam-artwork-refresh"}, {"committed", true},
        {"committedAt", unixTimeMs()}, {"accountId", target.accountId}, {"shortcutsVdf", toUtf8(target.path.wstring())},
        {"steamReloadRequired", realTarget}, {"artworkFilesActivated", activated.size()}, {"backup", toUtf8(backupRoot.wstring())},
        {"items", items}, {"unmatchedSelections", unmatched},
        {"shortcutsVdfModified", iconUpdates > 0}, {"automaticIconsApplied", iconUpdates}};
    publishCommittedSteamResult(result, {dataRoot / L"state" / L"last-steam-artwork-refresh.json", outputPath});
    return result;
}

static PidRuleSyncPlan planGameWhitelistSync(
    const json& state,
    const fs::path& whitelistPath,
    const fs::path& blacklistPath) {
    PidRuleSyncPlan plan;
    plan.currentWhitelist = readGameRuleFile(whitelistPath);
    plan.currentBlacklist = readGameRuleFile(blacklistPath);
    plan.nextWhitelist = plan.currentWhitelist;
    plan.nextBlacklist = plan.currentBlacklist;
    std::set<std::string> wanted;
    for (const auto& game : state.value("games", json::array())) {
        const auto directory = game.value("gameDirectory", std::string{});
        const auto executableText = game.value("primaryExecutable", std::string{});
        if (game.value("status", std::string{}) != "ready" ||
            game.value("primaryRole", std::string{}) != "main-game" || executableText.empty()) {
            continue;
        }
        const fs::path executable(toWide(executableText));
        const auto rule = normalizedExecutableRule(executable);
        if (rule.empty()) {
            plan.skipped.push_back({{"gameDirectory", directory}, {"reason", "empty-normalized-exe-rule"}});
            continue;
        }
        std::error_code ec;
        if (!fs::is_regular_file(executable, ec) || ec) {
            plan.skipped.push_back({
                {"gameDirectory", directory}, {"primaryExecutable", executableText},
                {"rule", rule}, {"reason", "primary-executable-no-longer-exists"}
            });
            continue;
        }
        wanted.insert(rule);
    }
    for (const auto& rule : wanted) {
        if (std::find(plan.nextWhitelist.begin(), plan.nextWhitelist.end(), rule) == plan.nextWhitelist.end()) {
            plan.nextWhitelist.push_back(rule);
            plan.addedWhitelist.push_back(rule);
        }
    }
    plan.nextBlacklist.erase(std::remove_if(plan.nextBlacklist.begin(), plan.nextBlacklist.end(), [&](const auto& rule) {
        if (!wanted.count(rule)) return false;
        plan.removedBlacklistConflicts.push_back(rule);
        return true;
    }), plan.nextBlacklist.end());
    return plan;
}

static json pidRuleSyncPlanJson(
    const PidRuleSyncPlan& plan,
    const fs::path& statePath,
    const fs::path& whitelistPath,
    const fs::path& blacklistPath,
    bool dryRun) {
    return {
        {"contract", PID_COORDINATION_CONTRACT},
        {"dryRun", dryRun},
        {"ownsPidSelection", false},
        {"writesGameTargetSnapshot", false},
        {"statePath", toUtf8(statePath.wstring())},
        {"whitelistPath", toUtf8(whitelistPath.wstring())},
        {"playerBlacklistPath", toUtf8(blacklistPath.wstring())},
        {"blacklistAutoAdd", false},
        {"currentWhitelist", stringVectorJson(plan.currentWhitelist)},
        {"currentPlayerBlacklist", stringVectorJson(plan.currentBlacklist)},
        {"nextWhitelist", stringVectorJson(plan.nextWhitelist)},
        {"nextPlayerBlacklist", stringVectorJson(plan.nextBlacklist)},
        {"addedWhitelist", stringVectorJson(plan.addedWhitelist)},
        {"removedExactBlacklistConflicts", stringVectorJson(plan.removedBlacklistConflicts)},
        {"skipped", plan.skipped},
        {"changed", !plan.addedWhitelist.empty() || !plan.removedBlacklistConflicts.empty()}
    };
}

struct RuleFileSnapshot {
    bool existed = false;
    std::vector<unsigned char> bytes;
};

static RuleFileSnapshot snapshotRuleFile(const fs::path& path) {
    RuleFileSnapshot snapshot;
    std::error_code ec;
    snapshot.existed = fs::is_regular_file(path, ec) && !ec;
    if (snapshot.existed) snapshot.bytes = readBinaryFile(path, 1u << 20);
    return snapshot;
}

static void restoreRuleFile(const fs::path& path, const RuleFileSnapshot& snapshot) {
    if (snapshot.existed) {
        writeGameRuleFileAtomic(path, snapshot.bytes);
    } else {
        DeleteFileW(path.c_str());
    }
}

static fs::path commitGameWhitelistSync(
    const PidRuleSyncPlan& plan,
    const fs::path& dataRoot,
    const fs::path& whitelistPath,
    const fs::path& blacklistPath) {
    if (readGameRuleFile(whitelistPath) != plan.currentWhitelist ||
        readGameRuleFile(blacklistPath) != plan.currentBlacklist) {
        throw std::runtime_error("PID rule files changed after planning; retry synchronization to preserve the newer rules");
    }
    const auto oldWhitelist = snapshotRuleFile(whitelistPath);
    const auto oldBlacklist = snapshotRuleFile(blacklistPath);
    uint64_t stamp = unixTimeMs();
    fs::path backupDirectory;
    std::error_code ec;
    do {
        backupDirectory = dataRoot / L"backups" / L"pid-rules" / std::to_wstring(stamp++);
        ec.clear();
    } while (fs::exists(backupDirectory, ec) && !ec);
    fs::create_directories(backupDirectory);
    if (oldWhitelist.existed) writeAtomic(backupDirectory / L"game-whitelist.before.txt", oldWhitelist.bytes);
    if (oldBlacklist.existed) writeAtomic(backupDirectory / L"player-blacklist.before.txt", oldBlacklist.bytes);
    writeJsonAtomic(backupDirectory / L"manifest.json", {
        {"schemaVersion", 1}, {"contract", PID_COORDINATION_CONTRACT},
        {"createdAt", unixTimeMs()},
        {"whitelistPath", toUtf8(whitelistPath.wstring())},
        {"playerBlacklistPath", toUtf8(blacklistPath.wstring())},
        {"whitelistExisted", oldWhitelist.existed}, {"playerBlacklistExisted", oldBlacklist.existed},
        {"addedWhitelist", stringVectorJson(plan.addedWhitelist)},
        {"removedExactBlacklistConflicts", stringVectorJson(plan.removedBlacklistConflicts)}
    });
    try {
        writeGameRuleFileAtomic(blacklistPath, gameRuleFileBytes(plan.nextBlacklist));
        writeGameRuleFileAtomic(whitelistPath, gameRuleFileBytes(plan.nextWhitelist));
    } catch (...) {
        try { restoreRuleFile(blacklistPath, oldBlacklist); } catch (...) {}
        try { restoreRuleFile(whitelistPath, oldWhitelist); } catch (...) {}
        throw;
    }
    return backupDirectory;
}

static json runArtworkQualitySelfTest() {
    const auto tallPreferred = assessArtworkQuality("tall", ImageInfo{"jpg", 600, 900, false, 600, 900, 1.0});
    const auto tallFallback = assessArtworkQuality("tall", ImageInfo{"jpg", 260, 390, false, 260, 390, 1.0});
    const auto tallRejected = assessArtworkQuality("tall", ImageInfo{"jpg", 180, 270, false, 180, 270, 1.0});
    const auto longUsable = assessArtworkQuality("long", ImageInfo{"jpg", 460, 215, false, 460, 215, 1.0});
    const auto heroPreferred = assessArtworkQuality("hero", ImageInfo{"jpg", 3840, 1240, false, 3840, 1240, 1.0});
    const auto heroFallback = assessArtworkQuality("hero", ImageInfo{"jpg", 800, 258, false, 800, 258, 1.0});
    const auto logoPreferred = assessArtworkQuality("logo", ImageInfo{"png", 1280, 720, true, 700, 160, 0.08});
    const auto logoTinyVisible = assessArtworkQuality("logo", ImageInfo{"png", 1280, 720, true, 120, 30, 0.004});
    const auto logoFallback = assessArtworkQuality("logo", ImageInfo{"png", 512, 256, true, 180, 50, 0.03});

    auto requireBand = [](const ArtworkQualityAssessment& value, ArtworkQualityBand expected, const char* label) {
        if (value.band != expected) {
            throw std::runtime_error(std::string("Artwork quality self-test failed: ") + label);
        }
    };
    requireBand(tallPreferred, ArtworkQualityBand::Preferred, "tall preferred");
    requireBand(tallFallback, ArtworkQualityBand::UniqueFallback, "tall unique fallback");
    requireBand(tallRejected, ArtworkQualityBand::Rejected, "tall rejected");
    requireBand(longUsable, ArtworkQualityBand::Usable, "long usable");
    requireBand(heroPreferred, ArtworkQualityBand::Preferred, "hero preferred");
    requireBand(heroFallback, ArtworkQualityBand::UniqueFallback, "hero unique fallback");
    requireBand(logoPreferred, ArtworkQualityBand::Preferred, "logo preferred visible bounds");
    requireBand(logoTinyVisible, ArtworkQualityBand::Rejected, "large canvas tiny visible logo rejected");
    requireBand(logoFallback, ArtworkQualityBand::UniqueFallback, "logo unique fallback");
    if (tallPreferred.score <= tallFallback.score || heroPreferred.score <= heroFallback.score ||
        logoPreferred.score <= logoFallback.score) {
        throw std::runtime_error("Artwork quality self-test failed: high-resolution sort order");
    }

    ResolvedGame igdbArtworkPolicy;
    igdbArtworkPolicy.igdbDetails = {
        {"artworks_expanded", {{{"url", "//images.igdb.com/igdb/image/upload/t_thumb/artwork.jpg"}, {"height", 1440}}}},
        {"screenshots_expanded", {{{"url", "//images.igdb.com/igdb/image/upload/t_thumb/screenshot.jpg"}, {"height", 2160}}}}
    };
    const auto heroSpec = artworkSpecs()[2];
    const auto artworkPreferredUrls = artworkUrls(igdbArtworkPolicy, heroSpec);
    const bool screenshotFallbackAvailableWithArtwork = std::any_of(
        artworkPreferredUrls.begin(), artworkPreferredUrls.end(), [](const auto& item) {
            return item.sourceRole == "background-screenshot";
        });
    igdbArtworkPolicy.igdbDetails["artworks_expanded"] = json::array();
    const auto screenshotFallbackUrls = artworkUrls(igdbArtworkPolicy, heroSpec);
    const bool screenshotFallbackAvailable = std::any_of(
        screenshotFallbackUrls.begin(), screenshotFallbackUrls.end(), [](const auto& item) {
            return item.sourceRole == "background-screenshot";
        });
    if (!screenshotFallbackAvailableWithArtwork || !screenshotFallbackAvailable) {
        throw std::runtime_error("Artwork quality self-test failed: Playnite IGDB artwork/screenshot fallback policy");
    }

    return {
        {"ok", true},
        {"highResolutionSortsFirst", true},
        {"largeCanvasTinyVisibleLogoRejected", true},
        {"uniqueLowQualityFallbackRetained", true},
        {"playniteArtworkPreferredOverScreenshot", true},
        {"playniteScreenshotFallbackRetained", true},
        {"policy", artworkQualityPolicyJson()}
    };
}


static bool isHorizontalArtworkSearchType(const std::string& type) {
    return type == "wallpaper" || type == "hero" || type == "long";
}

static int artworkSearchMinimumWidth(const std::string& type) {
    return isHorizontalArtworkSearchType(type) ? 800 : 240;
}

static json buildSteamLibraryDeletePlan(
    const fs::path& dataRoot,
    const fs::path& statePath,
    const SteamShortcutTarget& target,
    const fs::path& outputPath,
    const std::set<std::string>& selectedGameDirectories) {
    if (selectedGameDirectories.empty()) throw std::runtime_error("删除 Steam 快捷方式必须明确选择游戏");
    const auto state = loadLibraryScanState(statePath);
    const auto existing = readShortcuts(target.path, target.accountId);
    json items = json::array();
    json unmatched = json::array();
    size_t ready = 0, notFound = 0, ambiguous = 0;
    std::set<std::string> matched;
    for (const auto& game : state.value("games", json::array())) {
        const auto directory = fs::path(toWide(game.value("gameDirectory", std::string{})));
        const auto directoryKey = directory.empty() ? std::string{} : canonicalPathKey(directory);
        if (!selectedGameDirectories.count(directoryKey)) continue;
        matched.insert(directoryKey);
        const auto executable = fs::path(toWide(game.value("primaryExecutable", std::string{})));
        json item = {{"gameDirectory", game.value("gameDirectory", std::string{})},
                     {"directoryName", game.value("directoryName", std::string{})},
                     {"primaryExecutable", executable.empty() ? json(nullptr) : json(toUtf8(executable.wstring()))},
                     {"status", "not-found"}};
        std::vector<SteamShortcut> matches;
        if (!executable.empty()) {
            const auto wanted = canonicalPathKey(executable);
            for (const auto& shortcut : existing) {
                if (canonicalPathKey(expandEnvironmentPath(unquote(shortcut.exe))) == wanted) matches.push_back(shortcut);
            }
        }
        if (matches.size() == 1) {
            const auto ids = shortcutIds(matches.front());
            item["status"] = "ready-to-delete";
            item["appName"] = matches.front().appName;
            item["shortAppId"] = std::to_string(ids.shortId);
            item["longAppId"] = std::to_string(ids.longId);
            item["storedAppId"] = matches.front().storedAppId;
            item["gridDirectory"] = toUtf8((target.path.parent_path() / L"grid").wstring());
            ++ready;
        } else if (matches.empty()) {
            ++notFound;
            item["reason"] = "no-unique-shortcut-matches-primary-executable";
        } else {
            ++ambiguous;
            item["status"] = "ambiguous";
            item["reason"] = "multiple-shortcuts-match-primary-executable";
        }
        items.push_back(std::move(item));
    }
    for (const auto& key : selectedGameDirectories) if (!matched.count(key)) unmatched.push_back(key);
    json plan = {
        {"schemaVersion", 1}, {"isolated", true}, {"mode", "steam-library-delete-plan"},
        {"createdAt", unixTimeMs()}, {"dataRoot", toUtf8(dataRoot.wstring())},
        {"libraryState", toUtf8(statePath.wstring())}, {"shortcutsVdf", toUtf8(target.path.wstring())},
        {"shortcutsBeforeSha256", sha256(readBinaryFile(target.path, 16u << 20))},
        {"accountId", target.accountId}, {"realSteamTarget", realSteamShortcutsPath(target.path)},
        {"requiresSteamClosedForCommit", realSteamShortcutsPath(target.path)},
        {"selectionMode", "explicit-game-directories"}, {"requestedSelections", json::array()},
        {"unmatchedSelections", unmatched},
        {"summary", {{"selected", selectedGameDirectories.size()}, {"readyToDelete", ready},
                      {"notFound", notFound}, {"ambiguous", ambiguous}}}, {"items", items}
    };
    for (const auto& key : selectedGameDirectories) plan["requestedSelections"].push_back(key);
    writeJsonAtomic(outputPath, plan);
    return plan;
}

static json commitSteamLibraryDeletePlan(
    const fs::path& dataRoot,
    const fs::path& statePath,
    const SteamShortcutTarget& target,
    const fs::path& planPath,
    bool steamClosedConfirmed,
    const std::set<std::string>& selectedGameDirectories) {
    SteamShortcutWriterMutex writer;
    DataTransactionMutex transaction;
    requireResolvedSteamTransactions(dataRoot, target.path);
    auto plan = buildSteamLibraryDeletePlan(dataRoot, statePath, target, planPath, selectedGameDirectories);
    if (plan.value("realSteamTarget", false)) {
        if (!steamClosedConfirmed) throw std::runtime_error("真实 Steam 删除必须先确认 Steam 已关闭");
        if (processRunningByName(L"steam.exe")) throw std::runtime_error("Steam 仍在运行，请先关闭 Steam 再删除 shortcuts.vdf");
    }
    if (!plan.value("unmatchedSelections", json::array()).empty() || plan["summary"].value("notFound", 0) != 0 || plan["summary"].value("ambiguous", 0) != 0) {
        throw std::runtime_error("所选项目未能唯一匹配 Steam 快捷方式，已拒绝删除");
    }
    const size_t deleteCount = plan["summary"].value("readyToDelete", static_cast<size_t>(0));
    if (!deleteCount) throw std::runtime_error("没有可删除的 Steam 快捷方式");
    const auto original = readBinaryFile(target.path, 16u << 20);
    if (sha256(original) != plan.value("shortcutsBeforeSha256", std::string{})) throw std::runtime_error("shortcuts.vdf changed after planning; regenerate the plan");
    auto document = BinaryVdfDocument::parse(original);
    auto* shortcuts = vdfFindObject(document.root(), "shortcuts");
    if (!shortcuts) throw std::runtime_error("Binary VDF has no shortcuts object");
    std::set<uint32_t> ids;
    for (const auto& item : plan["items"]) if (item.value("status", std::string{}) == "ready-to-delete") ids.insert(static_cast<uint32_t>(std::stoul(item.value("shortAppId", std::string{"0"}))));
    const auto beforeCount = readShortcuts(target.path, target.accountId).size();
    auto binaryShortcutAppId = [](const BinaryVdfNode& node) -> std::optional<int64_t> {
        if (node.type != 0) return std::nullopt;
        for (const auto& [name, value] : node.objectValue) {
            if (asciiLower(name) != "appid") continue;
            if (value.type == 2 || value.type == 6) return static_cast<int64_t>(static_cast<int32_t>(value.u32Value));
            if (value.type == 1) { try { return std::stoll(value.stringValue); } catch (...) { return std::nullopt; } }
        }
        return std::nullopt;
    };
    shortcuts->objectValue.erase(std::remove_if(shortcuts->objectValue.begin(), shortcuts->objectValue.end(), [&](const auto& entry) {
        if (entry.second.type != 0) return false;
        const auto appId = binaryShortcutAppId(entry.second);
        return appId && ids.count(static_cast<uint32_t>(*appId)) != 0;
    }), shortcuts->objectValue.end());
    const auto stagedVdf = fs::path(target.path.wstring() + L".yeman-delete-stage-" + std::to_wstring(GetCurrentProcessId()));
    auto accountToken = target.accountId;
    for (auto& ch : accountToken) if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
    if (accountToken.empty()) accountToken = "account";
    const auto backupDirectory = steamTransactionDirectory(dataRoot, accountToken);
    fs::create_directories(backupDirectory);
    const auto backupFile = backupDirectory / L"shortcuts.before.vdf";
    writeAtomic(backupFile, original);
    ScopedStagedFiles stagedCleanup;
    stagedCleanup.add(stagedVdf);
    const auto stagedBytes = document.serialize();
    const auto shortcutsAfterHash = sha256(stagedBytes);
    writeAtomic(stagedVdf, stagedBytes);
    const auto stagedCount = readShortcuts(stagedVdf, target.accountId).size();
    if (stagedCount + deleteCount != beforeCount) { std::error_code ec; fs::remove(stagedVdf, ec); throw std::runtime_error("Staged shortcuts.vdf deletion verification failed"); }
    const auto pendingTransaction = backupDirectory / L"transaction.pending.json";
    writeJsonAtomic(pendingTransaction, {{"schemaVersion", 1}, {"operation", "delete"}, {"createdAt", unixTimeMs()},
        {"targetPath", toUtf8(target.path.wstring())}, {"targetExisted", true}, {"targetBackup", toUtf8(backupFile.wstring())},
        {"artworkTargets", json::array()}, {"stagedFiles", steamTransactionPathList({stagedVdf})}});
    bool activated = false;
    try {
        activateStagedFile(stagedVdf, target.path, "激活删除用的暂存 shortcuts.vdf");
        activated = true;
        if (readShortcuts(target.path, target.accountId).size() != stagedCount) throw std::runtime_error("Committed shortcuts.vdf deletion verification failed");
        writeJsonAtomic(backupDirectory / L"transaction.complete.json", {
            {"schemaVersion", 1}, {"completedAt", unixTimeMs()}, {"targetPath", toUtf8(target.path.wstring())}});
    } catch (...) {
        bool restored = !activated;
        if (activated) { try { writeAtomic(target.path, original); restored = true; } catch (...) {} }
        if (restored) { std::error_code cleanup; fs::remove(pendingTransaction, cleanup); }
        throw;
    }
    { std::error_code cleanup; fs::remove(pendingTransaction, cleanup); }
    plan["mode"] = "steam-library-delete-commit"; plan["committed"] = true; plan["committedAt"] = unixTimeMs();
    plan["shortcutsVdfModified"] = true; plan["shortcutsAfterSha256"] = shortcutsAfterHash;
    plan["backup"] = toUtf8(backupFile.wstring()); plan["deletedCount"] = deleteCount;
    publishCommittedSteamResult(plan, {dataRoot / L"state" / L"last-steam-delete.json", planPath});
    return plan;
}

static int artworkSearchMinimumHeight(const std::string& type) {
    return isHorizontalArtworkSearchType(type) ? 250 : 360;
}

static json baiduArtworkSearch(const std::string& query, const std::string& type, bool automaticFallback = false) {
    if (query.empty()) throw std::runtime_error("Artwork search query is empty");
    std::string networkQuery;
    networkQuery.reserve(query.size());
    for (size_t i = 0; i < query.size();) {
        const unsigned char ch = static_cast<unsigned char>(query[i]);
        if (ch == 0xC2 && i + 1 < query.size() && static_cast<unsigned char>(query[i + 1]) == 0xAE) { i += 2; continue; } // ®
        if (ch == 0xE2 && i + 2 < query.size() && static_cast<unsigned char>(query[i + 1]) == 0x84 && static_cast<unsigned char>(query[i + 2]) == 0xA2) { i += 3; continue; } // ™
        if (ch == 0xC2 && i + 1 < query.size() && static_cast<unsigned char>(query[i + 1]) == 0xA9) { i += 2; continue; } // ©
        networkQuery.push_back(query[i++]);
    }
    networkQuery = trim(networkQuery);
    if (networkQuery.empty()) networkQuery = query;
    const auto encoded = percentEncode(networkQuery);
    const auto url = std::string("https://image.baidu.com/search/acjson?tn=resultjson_com&ipn=rj&ct=201326592&fp=result&fr=ala&word=") + encoded +
        "&queryWord=" + encoded + "&cl=2&lm=-1&ie=utf-8&oe=utf-8&st=-1&z=0&ic=0&hd=0&latest=0&copyright=0&face=0&istype=2&nc=1&pn=0&rn=30";
    json attempts = json::array();
    const auto response = httpRequestWithRetry(
        "baidu-image", "GET", url,
        L"Accept: application/json, text/plain, */*\r\nAccept-Language: zh-CN,zh;q=0.9\r\n",
        {}, 8u << 20, false, "https://image.baidu.com/", &attempts);
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("百度图片搜索 HTTP " + std::to_string(response.status));
    }
    std::string text(response.body.begin(), response.body.end());
    const auto first = text.find('{');
    const auto last = text.rfind('}');
    if (first == std::string::npos || last <= first) throw std::runtime_error("百度图片返回不是有效 JSON");
    const auto document = json::parse(text.substr(first, last - first + 1));
    json candidates = json::array();
    const auto dataIt = document.find("data");
    if (dataIt != document.end() && dataIt->is_array()) {
        std::set<std::string> seen;
        for (const auto& item : *dataIt) {
            if (!item.is_object()) continue;
            const auto preview = item.value("thumbURL", std::string{});
            const auto middle = item.value("middleURL", std::string{});
            const auto original = item.value("objURL", std::string{});
            const auto imageUrl = original.rfind("https://", 0) == 0 ? original : (middle.rfind("https://", 0) == 0 ? middle : preview);
            if (imageUrl.rfind("https://", 0) != 0 || seen.contains(imageUrl)) continue;
            const int width = item.value("width", 0);
            const int height = item.value("height", 0);
            const auto minWidth = automaticFallback && isHorizontalArtworkSearchType(type) ? 480 : artworkSearchMinimumWidth(type);
            const auto minHeight = automaticFallback && isHorizontalArtworkSearchType(type) ? 300 : artworkSearchMinimumHeight(type);
            if (width < minWidth || height < minHeight) continue;
            seen.insert(imageUrl);
            candidates.push_back({
                {"url", imageUrl},
                {"previewUrl", preview.empty() ? imageUrl : preview},
                {"width", width}, {"height", height},
                {"sourcePage", item.value("fromURL", std::string{})},
                {"title", item.value("fromPageTitleEnc", item.value("fromPageTitle", std::string{}))},
                {"provider", "baidu-image"},
                {"preferred", width >= (type == "wallpaper" ? 1920 : 600) && height >= (type == "wallpaper" ? 620 : 900)}
            });
            if (candidates.size() >= 24) break;
        }
    }
    return {
        {"provider", "baidu-image"}, {"query", query}, {"networkQuery", networkQuery}, {"type", type},
        {"candidates", candidates}, {"networkAttempts", attempts},
        {"sizeRule", {"minWidth", artworkSearchMinimumWidth(type), "minHeight", artworkSearchMinimumHeight(type)}}
    };
}

static json playniteIgdbArtworkSearch(const std::string& query, const std::string& type) {
    if (query.empty()) throw std::runtime_error("Artwork search query is empty");

    struct Match {
        uint64_t id = 0;
        std::string name;
        std::string query;
        double score = 0;
        double textScore = 0;
        json item;
    };

    std::vector<std::string> queries;
    std::set<std::string> queryKeys;
    auto addQuery = [&](const std::string& raw) {
        const auto value = collapseSpaces(raw);
        const auto key = searchQueryKey(value);
        if (!value.empty() && queryKeys.insert(key).second) queries.push_back(value);
    };
    addQuery(query);
    addQuery(humanizeTitle(query));
    addQuery(steamSearchFriendlyTitle(query));

    std::map<uint64_t, Match> found;
    json attempts = json::array();
    bool successfulSearch = false;
    std::string lastError;
    for (const auto& searchTerm : queries) {
        json searchAttempts = json::array();
        json record = {
            {"stage", "search"}, {"query", searchTerm},
            {"networkAttempts", searchAttempts}, {"ok", false}
        };
        try {
            const auto response = postJson(
                "https://api2.playnite.link/api/igdb/search",
                json{{"SearchTerm", searchTerm}},
                "playnite-igdb",
                &searchAttempts);
            const auto apiError = responseError(response);
            if (!apiError.empty()) throw std::runtime_error("Playnite IGDB: " + apiError);
            const auto data = responseData(response);
            if (!data || !data->is_array()) throw std::runtime_error("Playnite IGDB search returned no list");
            successfulSearch = true;
            record["ok"] = true;
            size_t loggedItems = 0;
            for (const auto& item : *data) {
                if (!item.is_object()) continue;
                const uint64_t id = item.value("id", uint64_t{0});
                const auto name = trim(item.value("name", std::string{}));
                if (!id || name.empty()) continue;
                const double textScore = item.value("textScore", 0.0);
                const double titleScore = scoreTitle(searchTerm, name);
                const double rankedScore = std::clamp(
                    titleScore * 0.86 + std::clamp(textScore / 2.5, 0.0, 1.0) * 0.14,
                    0.0, 1.0);
                if (loggedItems < 12) {
                    record["items"].push_back({
                        {"igdbId", id}, {"name", name},
                        {"score", rankedScore}, {"textScore", textScore},
                        {"hasCover", item.contains("cover_expanded") && item["cover_expanded"].is_object()}
                    });
                    ++loggedItems;
                }
                const auto existing = found.find(id);
                if (existing == found.end() || rankedScore > existing->second.score) {
                    found[id] = {id, name, searchTerm, rankedScore, textScore, item};
                }
            }
        } catch (const std::exception& error) {
            lastError = error.what();
            record["error"] = lastError;
        }
        record["networkAttempts"] = searchAttempts;
        attempts.push_back(std::move(record));
        // One successful search already returns the proxy's ranked candidate
        // set. Additional variants are only needed when the first query found
        // nothing, which keeps manual artwork search responsive on weak links.
        if (successfulSearch && !found.empty()) break;
    }
    if (!successfulSearch && found.empty()) {
        throw std::runtime_error(lastError.empty() ? "Playnite IGDB artwork search failed" : lastError);
    }

    std::vector<Match> ranked;
    for (const auto& [_, match] : found) {
        if (match.score >= 0.45) ranked.push_back(match);
    }
    std::sort(ranked.begin(), ranked.end(), [](const Match& left, const Match& right) {
        return left.score > right.score ||
            (left.score == right.score && left.textScore > right.textScore);
    });
    if (ranked.size() > 8) ranked.resize(8);

    json candidates = json::array();
    json matches = json::array();
    std::set<std::string> seen;
    for (const auto& match : ranked) {
        matches.push_back({
            {"igdbId", match.id}, {"name", match.name},
            {"score", match.score}, {"query", match.query}
        });
        if (type == "cover") {
            const auto coverIt = match.item.find("cover_expanded");
            if (coverIt == match.item.end() || !coverIt->is_object()) continue;
            const int width = coverIt->value("width", 0);
            const int height = coverIt->value("height", 0);
            const auto rawUrl = coverIt->value("url", std::string{});
            const auto imageUrl = igdbImageUrl(rawUrl, height > 1080 ? "1080p" : "original");
            if (imageUrl.empty() || width < artworkSearchMinimumWidth(type) ||
                height < artworkSearchMinimumHeight(type) || !seen.insert(imageUrl).second) continue;
            candidates.push_back({
                {"url", imageUrl}, {"previewUrl", imageUrl},
                {"width", width}, {"height", height},
                {"sourcePage", match.item.value("url", std::string{})},
                {"title", match.name}, {"provider", "playnite-igdb"},
                {"preferred", width >= 600 && height >= 900},
                {"priority", "playnite-igdb"}, {"igdbId", match.id},
                {"sourceRole", "cover"}
            });
            continue;
        }

        // Search results do not carry hero artwork. Fetch details only for the
        // strongest matches and keep screenshots as a per-game fallback when
        // that game has no IGDB artwork, matching the automatic artwork policy.
        json detailAttempts = json::array();
        json detailRecord = {
            {"stage", "detail"}, {"igdbId", match.id},
            {"name", match.name}, {"networkAttempts", detailAttempts}, {"ok", false}
        };
        try {
            const auto response = getJson(
                "https://api2.playnite.link/api/igdb/game/" + std::to_string(match.id),
                "playnite-igdb",
                &detailAttempts);
            const auto apiError = responseError(response);
            if (!apiError.empty()) throw std::runtime_error("Playnite IGDB: " + apiError);
            const auto data = responseData(response);
            if (!data || !data->is_object()) throw std::runtime_error("Playnite IGDB detail returned no game");
            detailRecord["ok"] = true;
            auto addImages = [&](const char* field, const char* role, bool preferredRole) {
                size_t added = 0;
                const auto imageIt = data->find(field);
                if (imageIt == data->end() || !imageIt->is_array()) return added;
                for (const auto& image : *imageIt) {
                    if (added >= 4 || !image.is_object()) continue;
                    const int width = image.value("width", 0);
                    const int height = image.value("height", 0);
                    if (width < artworkSearchMinimumWidth(type) || height < artworkSearchMinimumHeight(type)) continue;
                    const auto imageUrl = igdbImageUrl(
                        image.value("url", std::string{}), height > 1080 ? "1080p" : "original");
                    if (imageUrl.empty() || !seen.insert(imageUrl).second) continue;
                    candidates.push_back({
                        {"url", imageUrl}, {"previewUrl", imageUrl},
                        {"width", width}, {"height", height},
                        {"sourcePage", match.item.value("url", std::string{})},
                        {"title", match.name}, {"provider", "playnite-igdb"},
                        {"preferred", preferredRole && width >= 1920 && height >= 620},
                        {"priority", "playnite-igdb"}, {"igdbId", match.id},
                        {"sourceRole", role}
                    });
                    ++added;
                }
                return added;
            };
            const auto artworkCount = addImages("artworks_expanded", "background-artwork", true);
            if (artworkCount == 0) addImages("screenshots_expanded", "background-screenshot", false);
        } catch (const std::exception& error) {
            detailRecord["error"] = error.what();
        }
        detailRecord["networkAttempts"] = detailAttempts;
        attempts.push_back(std::move(detailRecord));
        if (candidates.size() >= 16) break;
    }

    return {
        {"provider", "playnite-igdb"}, {"query", query}, {"type", type},
        {"candidates", candidates}, {"matches", matches},
        {"networkAttempts", attempts}, {"networkQuery", queries.empty() ? query : queries.front()},
        {"sizeRule", {"minWidth", artworkSearchMinimumWidth(type), "minHeight", artworkSearchMinimumHeight(type)}}
    };
}

static std::string artworkFallbackTitle(const std::string& manualName,
    const std::vector<NameRound>& rounds, const std::vector<std::string>& pathEvidence, const fs::path& exePath) {
    if (!manualName.empty()) return manualName;
    for (const auto& round : rounds) {
        if (round.skipReason.empty() && !round.value.empty()) return round.value;
    }
    return !pathEvidence.empty() ? pathEvidence.front() : toUtf8(exePath.stem().wstring());
}

// Image captions may use Chinese spellings for a Japanese title. This is
// deliberately NOT part of normalizeTitle()/scoreTitle() or identity matching.
static std::wstring automaticArtworkCaptionKey(const std::string& caption) {
    auto wideCaption = toWide(decodeSteamHtml(caption));
    std::wstring expanded;
    for (const auto ch : wideCaption) {
        if (ch >= 0x2160 && ch <= 0x216b) expanded += std::to_wstring(ch - 0x2160 + 1);
        else if (ch >= 0x2170 && ch <= 0x217b) expanded += std::to_wstring(ch - 0x2170 + 1);
        else expanded.push_back(ch);
    }
    const auto normalized = toWide(normalizeTitle(toUtf8(expanded)));
    if (normalized.empty()) return {};
    const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_SIMPLIFIED_CHINESE,
        normalized.data(), static_cast<int>(normalized.size()), nullptr, 0, nullptr, nullptr, 0);
    std::wstring key = normalized;
    if (size > 0) {
        std::wstring simplified(size, L'\0');
        if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_SIMPLIFIED_CHINESE,
            normalized.data(), static_cast<int>(normalized.size()), simplified.data(), size,
            nullptr, nullptr, 0) > 0) key = std::move(simplified);
    }
    // Common caption orthography, not translation of arbitrary title words.
    for (auto& ch : key) {
        if (ch == L'伝') ch = L'传';
        if (ch == L'の' || ch == L'的') ch = L'之';
    }
    // Common localized edition labels remain mandatory evidence. Do not
    // strip Power-Up Kit and accidentally match the base game/another sequel.
    for (const auto* spelling : {L"パワーアップキット", L"威力加强版", L"威力增强版"}) {
        const std::wstring from = spelling;
        for (auto at = key.find(from); at != std::wstring::npos; at = key.find(from, at + 1))
            key.replace(at, from.size(), L"powerupkit");
    }
    return key;
}

static bool automaticArtworkSearchTitleRelevant(const std::string& query, const std::string& caption) {
    const auto cleanQuery = decodeSteamHtml(query);
    const auto cleanCaption = decodeSteamHtml(caption);
    const auto rawQuery = toWide(normalizeTitle(cleanQuery));
    const bool cjk = std::any_of(rawQuery.begin(), rawQuery.end(), [](wchar_t c) {
        return (c >= 0x3040 && c <= 0x30ff) || (c >= 0x3400 && c <= 0x9fff);
    });
    if (!cjk && scoreTitle(cleanQuery, cleanCaption) >= 0.55) return true;
    const auto needle = automaticArtworkCaptionKey(cleanQuery);
    const auto haystack = automaticArtworkCaptionKey(cleanCaption);
    if (needle.empty() || haystack.empty()) return false;
    if (needle == haystack) return true;
    // Require the COMPLETE subtitle too; "維新の嵐" alone is another game.
    if (needle.size() < 6) return false;
    auto asciiWord = [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'); };
    for (auto at = haystack.find(needle); at != std::wstring::npos; at = haystack.find(needle, at + 1)) {
        const auto end = at + needle.size();
        if (at > 0 && asciiWord(needle.front()) && asciiWord(haystack[at - 1])) continue;
        if (end < haystack.size() && asciiWord(needle.back()) && asciiWord(haystack[end])) continue;
        return true;
    }
    return false;
}

static size_t appendAutomaticArtworkSearchCandidates(
    const json& search, const std::string& query, const ArtworkSpec& spec, std::vector<ArtworkCandidate>& output) {
    if (!search.is_object() || !search.contains("candidates") || !search["candidates"].is_array()) return 0;
    std::set<std::string> seen;
    for (const auto& item : output) seen.insert(item.url);
    size_t added = 0;
    for (const auto& item : search["candidates"]) {
        if (!item.is_object() || added >= 8) continue;
        const auto url = jsonStringOr(item, "url");
        const auto title = jsonStringOr(item, "title");
        const auto provider = jsonStringOr(item, "provider");
        if (url.rfind("https://", 0) != 0 || !seen.insert(url).second ||
            (provider != "playnite-igdb" && provider != "baidu-image")) continue;
        // Search hits are not identity evidence. Require title relevance before
        // automatic downloading; arbitrary top image hits are not a safe fallback.
        if (title.empty() || !automaticArtworkSearchTitleRelevant(query, title)) continue;
        if (provider == "playnite-igdb" && titleOrdinalSet(title) != titleOrdinalSet(query)) continue;
        const ImageInfo shape{"jpg", positiveJsonInt(item, "width"), positiveJsonInt(item, "height")};
        const bool background = spec.type == "hero" || spec.type == "long";
        if (assessArtworkQuality(spec.type, shape, background).band == ArtworkQualityBand::Rejected) continue;
        output.push_back({url, provider, "automatic-search-fallback",
            provider == "playnite-igdb" ? "https://www.igdb.com/" : "https://image.baidu.com/",
            background, url, provider == "playnite-igdb" ? 400 : 200, 0});
        ++added;
    }
    if (added == 0 && (spec.type == "tall" || spec.type == "long" || spec.type == "hero")) {
        // Some old PC games have only screenshots, not scanned portrait
        // packaging. A full-title-matched landscape is better than an empty
        // card, but must remain explicitly tagged as an adapted last resort.
        for (const auto& item : search["candidates"]) {
            if (!item.is_object() || added >= 4 || jsonStringOr(item, "provider") != "baidu-image") continue;
            const auto url = jsonStringOr(item, "url");
            const ImageInfo shape{"jpg", positiveJsonInt(item, "width"), positiveJsonInt(item, "height")};
            if (url.rfind("https://", 0) != 0 || !usableArtworkFittingSource(shape) ||
                !automaticArtworkSearchTitleRelevant(query, jsonStringOr(item, "title"))) continue;
            const bool exists = std::any_of(output.begin(), output.end(), [&](const auto& candidate) { return candidate.url == url; });
            if (exists) continue;
            ArtworkCandidate candidate{url, "baidu-image", spec.type == "tall" ? "automatic-landscape-cover-fallback" : "automatic-fitted-background-fallback",
                "https://image.baidu.com/", false, url, 100, 0};
            candidate.fitCanvas = true;
            output.push_back(std::move(candidate)); ++added;
        }
    }
    return added;
}

static json steamIdArtworkSearch(const std::string& appId, const std::string& type) {
    json result = {
        {"candidates", json::array()},
        {"networkAttempts", json::array()},
        {"sizeRule", {{"minWidth", artworkSearchMinimumWidth(type),
                        "minHeight", artworkSearchMinimumHeight(type)}}}
    };
    if (appId.empty() || !std::all_of(appId.begin(), appId.end(),
        [](unsigned char ch) { return std::isdigit(ch) != 0; })) return result;

    const std::string appPath = "/store_item_assets/steam/apps/" + appId + "/";
    const bool horizontal = isHorizontalArtworkSearchType(type);
    std::set<std::string> seen;
    auto addCandidate = [&](const std::string& url, const std::string& title,
                            int width, int height, const std::string& role,
                            int rank = 0) {
        if (url.rfind("https://", 0) != 0 || !seen.insert(url).second) return;
        if (width < artworkSearchMinimumWidth(type) ||
            height < artworkSearchMinimumHeight(type)) return;
        result["candidates"].push_back({
            {"url", url}, {"previewUrl", url}, {"width", width}, {"height", height},
            {"title", title}, {"provider", "steam-store"}, {"preferred", true},
            {"priority", "steam-id-appdetails"}, {"sourceRole", role}, {"rank", rank}
        });
    };

    json detailAttempts = json::array();
    bool detailsLoaded = false;
    bool hashedStoreAssets = false;
    try {
        int numericAppId = 0;
        try {
            const auto parsed = std::stoll(appId);
            if (parsed <= 0 || parsed > INT_MAX) throw std::runtime_error("Steam AppID out of range");
            numericAppId = static_cast<int>(parsed);
        } catch (const std::exception&) {
            throw std::runtime_error("Steam AppID is out of range");
        }
        const auto details = fetchSteamDetails(numericAppId, detailAttempts);
        detailsLoaded = true;
        const auto title = details.name.empty() ? "Steam AppID " + appId : details.name;
        auto field = [&](const char* name, const char* role, int width, int height, int rank) {
            const auto url = details.data.value(name, std::string{});
            if (url.empty()) return;
            if (url.find(appPath) != std::string::npos) hashedStoreAssets = true;
            addCandidate(url, title + " · Steam " + name, width, height, role, rank);
        };

        if (horizontal) {
            field("background_raw", "background-raw", 1920, 1080, 1000);
            field("background", "background", 1920, 1080, 980);
            if (details.data.contains("screenshots") && details.data["screenshots"].is_array()) {
                int rank = 900;
                for (const auto& screenshot : details.data["screenshots"]) {
                    if (!screenshot.is_object()) continue;
                    const auto url = screenshot.value("path_full", std::string{});
                    if (url.empty()) continue;
                    if (url.find(appPath) != std::string::npos) hashedStoreAssets = true;
                    addCandidate(url, title + " · Steam screenshot", 1920, 1080,
                        "store-screenshot", rank--);
                    if (rank < 820) break;
                }
            }
        } else {
            // AppDetails normally exposes a landscape header/capsule, not the
            // 600x900 library cover. Accept a portrait field when a future
            // Steam response provides one, but do not put a landscape header
            // into the cover slot and corrupt the library layout.
            for (const char* name : {"library_capsule", "library_capsulev5",
                                     "library_image", "vertical_capsule"}) {
                const auto url = details.data.value(name, std::string{});
                if (url.empty()) continue;
                if (url.find(appPath) != std::string::npos) hashedStoreAssets = true;
                addCandidate(url, title + " · Steam " + name, 600, 900,
                    "library-cover", 1000);
            }
            const auto header = details.data.value("header_image", std::string{});
            if (header.find(appPath) != std::string::npos) hashedStoreAssets = true;
        }
    } catch (const std::exception& error) {
        result["detailsError"] = error.what();
    }
    try {
        const auto parsed=std::stoll(appId);
        if(parsed<=0||parsed>INT_MAX)throw std::runtime_error("Steam AppID is out of range");
        const auto assets=fetchSteamLibraryAssets(static_cast<int>(parsed),detailAttempts);
        const auto fields=horizontal?std::vector<const char*>{"library_hero_2x","library_hero"}:
            std::vector<const char*>{"library_capsule_2x","library_capsule"};
        for(const auto* field:fields) {
            const auto url=steamLibraryAssetUrl(assets,static_cast<int>(parsed),field);
            if(url.empty())continue;
            const bool doubled=std::string(field).ends_with("_2x");
            addCandidate(url,"Steam AppID "+appId+" · "+field,horizontal?(doubled?3840:1920):(doubled?1200:600),
                horizontal?(doubled?1240:620):(doubled?1800:900),"library-assets-"+std::string(field),doubled?1800:1700);
            hashedStoreAssets=hashedStoreAssets||isHashedSteamStoreAssetUrl(url);
        }
    }catch(const std::exception& error) {result["libraryAssetsError"]=error.what();}
    result["networkAttempts"] = detailAttempts;

    // Keep the old deterministic list only for offline/legacy records. When
    // AppDetails has already confirmed hashed assets, adding these known-404
    // candidates creates broken previews and needlessly delays the fallback.
    if (!detailsLoaded || result["candidates"].empty() || !hashedStoreAssets) {
        const std::string base = "https://shared.steamstatic.com/store_item_assets/steam/apps/" + appId + "/";
        const std::string fallback = "https://cdn.cloudflare.steamstatic.com/steam/apps/" + appId + "/";
        const std::vector<std::string> files = horizontal
            ? std::vector<std::string>{"library_hero.jpg", "library_hero_2x.jpg", "header.jpg"}
            : std::vector<std::string>{"library_600x900_2x.jpg", "library_600x900.jpg", "capsule_616x353.jpg"};
        for (const auto& file : files) {
            const bool portrait = file.find("600x900") != std::string::npos;
            const int width = horizontal ? 1920 : (portrait ? 600 : 616);
            const int height = horizontal ? 620 : (portrait ? 900 : 353);
            addCandidate(base + file, "Steam AppID " + appId + " · legacy · " + file,
                width, height, "legacy-cdn", 100);
            addCandidate(fallback + file, "Steam AppID " + appId + " · legacy CDN fallback · " + file,
                width, height, "legacy-cdn-fallback", 90);
        }
    }
    result["detailsLoaded"] = detailsLoaded;
    result["hashedStoreAssets"] = hashedStoreAssets;
    return result;
}

static constexpr wchar_t kSupportedArtworkAccept[] = L"Accept: image/png,image/jpeg;q=0.9,*/*;q=0.1\r\n";

static std::string supportedArtworkRequestUrl(std::string url) {
    // Manual downloads already forced JPEG for auto-format image proxies.
    // Automatic downloads must use the same decoder-compatible request.
    // Baidu's /it/u=...&fmt=auto path uses parameter syntax before '?'.
    const std::string parameter = "fmt=auto";
    for (auto at = url.find(parameter); at != std::string::npos; at = url.find(parameter, at + 1)) {
        const auto end = at + parameter.size();
        if (at > 0 && (url[at - 1] == '?' || url[at - 1] == '&') &&
            (end == url.size() || url[end] == '&' || url[end] == '?')) {
            url.replace(at, parameter.size(), "fmt=jpeg");
        }
    }
    return url;
}

static json downloadBaiduArtwork(const std::string& url, const std::string& type, const fs::path& output) {
    if (url.rfind("https://", 0) != 0) throw std::runtime_error("只允许下载 HTTPS 图片");
    json attempts = json::array();
    const auto requestUrl = supportedArtworkRequestUrl(url);
    const bool steamCdn = requestUrl.find("steamstatic.com/") != std::string::npos;
    const bool playniteIgdb = requestUrl.find("images.igdb.com/") != std::string::npos;
    const auto provider = steamCdn ? "steam-cdn" : (playniteIgdb ? "playnite-igdb" : "baidu-image");
    const auto referer = steamCdn ? "https://store.steampowered.com/" :
        (playniteIgdb ? "https://www.igdb.com/" : "https://image.baidu.com/");
    const auto response = httpGet(
        requestUrl,
        kSupportedArtworkAccept,
        MAX_IMAGE_BYTES, true, provider, referer, &attempts);
    if (response.status < 200 || response.status >= 300) throw std::runtime_error("图片下载 HTTP " + std::to_string(response.status));
    const auto info = inspectImage(response.body);
    if (!info || info->width < artworkSearchMinimumWidth(type) || info->height < artworkSearchMinimumHeight(type)) {
        throw std::runtime_error("图片尺寸低于素材最低规则");
    }
    writeAtomic(output, response.body);
    return {{"path", toUtf8(output.wstring())}, {"width", info->width}, {"height", info->height}, {"bytes", response.body.size()}, {"networkAttempts", attempts}, {"provider", provider}};
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    try {
        const fs::path labRoot = fs::absolute(fs::path(argv[0]).parent_path().parent_path());
        if (argc < 2) {
            std::cerr << "Usage:\n"
                         "  SteamArtworkLab.exe --audit-shortcuts [output-root] [--shortcuts <shortcuts.vdf>]\n"
                         "  SteamArtworkLab.exe --inspect-emulator --exe <emulator.exe> --start-dir <dir> --launch-options <args>\n"
                         "  SteamArtworkLab.exe --plan-add-emulator --exe <emulator.exe> [output-root]\n"
                         "  SteamArtworkLab.exe --scan-library [output-root] [--root <library-root>]... [--save-roots] [--clear-roots] [--first-run-defaults]\n"
                         "  SteamArtworkLab.exe --list-steam-accounts [--steam-root <path>]... [--output <json>]\n"
                         "  SteamArtworkLab.exe --plan-add-library-to-steam [--state <library-scan.json>] [--account <id>|--shortcuts <file>]\n"
                         "  SteamArtworkLab.exe --commit-add-library-to-steam [--state <library-scan.json>] [--account <id>|--shortcuts <file>] --confirm-steam-closed\n"
                         "  SteamArtworkLab.exe --plan-delete-library-from-steam [--state <library-scan.json>] [--account <id>|--shortcuts <file>] --select-game <directory>...\n"
                         "  SteamArtworkLab.exe --commit-delete-library-from-steam [--state <library-scan.json>] [--account <id>|--shortcuts <file>] --select-game <directory>... --confirm-steam-closed\n"
                         "  --selection-mode <all-ready|explicit-game-directories> [--select-game <directory>]...\n"
                         "  SteamArtworkLab.exe --search-artwork <query> --type <cover|long|wallpaper>\n"
                         "  SteamArtworkLab.exe --download-artwork --url <https-image> --type <cover|long|wallpaper> --output <file>\n"
                         "  SteamArtworkLab.exe --show-library-data-root\n"
                         "  SteamArtworkLab.exe --steam-network-self-test\n"
                         "  SteamArtworkLab.exe --show-pid-rule-suggestions [--state <library-scan.json>]\n"
                         "  SteamArtworkLab.exe --sync-game-whitelist [--dry-run] [--state <library-scan.json>]\n"
                         "  SteamArtworkLab.exe --backup-library-data\n"
                         "  SteamArtworkLab.exe --verify-library-backup --snapshot <path|latest>\n"
                         "  SteamArtworkLab.exe --restore-library-data --snapshot <path|latest>\n"
                         "  SteamArtworkLab.exe --show-network-retry-queue\n"
                         "  SteamArtworkLab.exe --wake-network-retries --reason <event> [--force] [--max <n>]\n"
                         "  SteamArtworkLab.exe --set-library-scan-enabled <on|off>\n"
                         "  SteamArtworkLab.exe --set-library-scraping-enabled <on|off>\n"
                         "  SteamArtworkLab.exe --set-library-bucket --game-dir <dir> --bucket <auto|not-in-steam|unclassified|non-game>\n"
                         "  SteamArtworkLab.exe --add-library-game --game-dir <dir>\n"
                         "  SteamArtworkLab.exe --remove-library-game --game-dir <dir>\n"
                         "  SteamArtworkLab.exe --undo-remove-library-game --game-dir <dir>\n"
                         "  SteamArtworkLab.exe --restore-library-game --game-dir <dir>\n"
                         "  SteamArtworkLab.exe --set-library-primary --game-dir <dir> --exe <game.exe>\n"
                         "  SteamArtworkLab.exe --artwork-quality-self-test\n"
                         "  SteamArtworkLab.exe --steam-search-query-self-test\n"
                         "  SteamArtworkLab.exe --steam-base-consensus-self-test\n"
                         "  SteamArtworkLab.exe --release-year-self-test\n"
                         "  SteamArtworkLab.exe --identity-evidence-self-test\n"
                         "  SteamArtworkLab.exe --steam-recent-signal-self-test\n"
                         "  SteamArtworkLab.exe --inspect-local-identity --exe <game.exe>\n"
                         "  SteamArtworkLab.exe <game.exe> [output-root] [options]\n"
                         "  --simulate-no-version\n"
                         "  --name <manual title>\n"
                          "  --save-name <manual title>\n"
                         "  --metadata-only       resolve name/IDs without downloading artwork\n"
                         "  --steam-identity-only diagnostic Steam-only AppID recognition\n"
                         "  --steam-comprehensive-identity-only background comprehensive Steam-first recognition\n"
                         "  --steam-artwork-only  download Steam CDN artwork directly from --steam-id\n"
                         "  --igdb-artwork-only   use IGDB only for fallback artwork\n"
                         "  --clear-name\n"
                         "  --ignore-saved-name\n"
                         "  --steam-id <verified Steam AppID>\n"
                         "  --igdb-id <verified IGDB ID>\n"
                         "  --save-id\n"
                         "  --clear-id\n"
                         "  --ignore-saved-id\n"
                         "  --simulate-apply\n"
                         "  --apply-real\n"
                         "  --only-missing\n"
                         "  --shortcuts <shortcuts.vdf>\n"
                         "  --shortcut-id <Steam unsigned shortcut ID>\n"
                         "  --platform <pc|linux|mac|playstation|ps2|psp|ps3|xbox|switch|any>\n"
                         "  --fault-provider <steam|playnite-igdb>\n"
                         "  --fault-network <steam|playnite-igdb>:<1-5>\n"
                         "  --fault-artwork-network <steam|playnite-igdb>:<1-64>\n";
            return 2;
        }
        if (_wcsicmp(argv[1], L"--artwork-quality-self-test") == 0) {
            std::cout << runArtworkQualitySelfTest().dump(2) << "\n";
            return 0;
        }
        if (_wcsicmp(argv[1], L"--steam-search-query-self-test") == 0) {
            const auto report = runSteamSearchQuerySelfTest();
            std::cout << report.dump(2) << "\n";
            return report.value("allPassed", false) ? 0 : 4;
        }
        if (_wcsicmp(argv[1], L"--steam-base-consensus-self-test") == 0) {
            const auto report = runSteamBaseConsensusSelfTest();
            std::cout << report.dump(2) << "\n";
            return report.value("allPassed", false) ? 0 : 4;
        }
        if (_wcsicmp(argv[1], L"--release-year-self-test") == 0) {
            const auto report = runReleaseYearSelfTest();
            std::cout << report.dump(2) << "\n";
            return report.value("allPassed", false) ? 0 : 4;
        }
        if (_wcsicmp(argv[1], L"--identity-evidence-self-test") == 0) {
            const auto report = runIdentityEvidenceSelfTest();
            std::cout << report.dump(2) << "\n";
            return report.value("allPassed", false) ? 0 : 4;
        }
        if (_wcsicmp(argv[1], L"--steam-recent-signal-self-test") == 0) {
            const auto report = runSteamRecentSignalSelfTest();
            std::cout << report.dump(2) << "\n";
            return report.value("allPassed", false) ? 0 : 4;
        }
        if (_wcsicmp(argv[1], L"--inspect-local-identity") == 0) {
            std::optional<fs::path> executable;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--exe") == 0) {
                    if (++i >= argc) throw std::runtime_error("--exe requires a path");
                    executable = fs::weakly_canonical(fs::path(argv[i]));
                } else {
                    throw std::runtime_error("Unknown local identity inspection option: " + toUtf8(argv[i]));
                }
            }
            if (!executable || !fs::is_regular_file(*executable)) {
                throw std::runtime_error("--inspect-local-identity requires an existing --exe");
            }
            const auto evidence = discoverLocalSteamAppIds(*executable);
            const auto selected = unambiguousLocalSteamAppId(evidence);
            std::set<int> distinctIds;
            json preferredNames = json::array();
            for (const auto& item : evidence) {
                if (item.appId > 0) distinctIds.insert(item.appId);
                if (!item.titleHint.empty()) preferredNames.push_back({
                    {"source", item.key}, {"name", item.titleHint},
                    {"targetMatchesExecutable", item.targetMatchesExecutable}
                });
            }
            json pe = json::object();
            const auto peVersion = versionStrings(*executable);
            for (const auto& [key, value] : peVersion) pe[key] = value;
            const auto peIdentity = assessPeIdentity(peVersion, classifyLibraryExecutable(*executable));
            json report = {
                {"schemaVersion", 1}, {"readOnly", true}, {"networkUsed", false},
                {"exe", toUtf8(executable->wstring())},
                {"identityRoot", toUtf8(localIdentityRoot(*executable).wstring())},
                {"resolverVersion", STEAM_RESOLVER_VERSION},
                {"evidence", localSteamAppIdEvidenceJson(evidence)},
                {"sourceCount", evidence.size()}, {"distinctAppIdCount", distinctIds.size()},
                {"conflict", distinctIds.size() > 1},
                {"selectedAppId", selected ? json(*selected) : json(nullptr)},
                {"preferredNames", preferredNames}, {"peVersionEvidence", pe},
                {"productName", peIdentity.productName.empty() ? json(nullptr) : json(peIdentity.productName)},
                {"fileDescription", peIdentity.fileDescription.empty() ? json(nullptr) : json(peIdentity.fileDescription)},
                {"primaryEvidenceSource", peIdentity.primaryEvidenceSource},
                {"identityConfidence", peIdentity.identityConfidence},
                {"identityConfidenceBand", peIdentity.identityConfidenceBand},
                {"classifiedRole", peIdentity.role}
            };
            std::cout << report.dump(2) << "\n";
            return 0;
        }

        const fs::path dataRoot = steamBigPictureDataRoot(labRoot);
        initializeSteamBigPictureDataRoot(dataRoot, labRoot);
        g_workerDataRoot = dataRoot;
        const fs::path libraryConfigPath = dataRoot / L"config" / L"library-config.json";
        const fs::path configurationBackups = dataRoot / L"backups" / L"config";

        if (_wcsicmp(argv[1], L"--show-library-data-root") == 0) {
            if (argc != 2) throw std::runtime_error("--show-library-data-root does not accept options");
            std::cout << "FEATURE_NAME: Custom Steam Library\n";
            std::cout << "DATA_ROOT: " << toUtf8(dataRoot.wstring()) << "\n";
            std::cout << "PORTABLE_REINSTALL_DATA: true\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--steam-network-self-test") == 0) {
            if (argc != 2) throw std::runtime_error("--steam-network-self-test does not accept options");
            constexpr const char* testHost = "store.steampowered.com";
            constexpr const char* testUrl = "https://store.steampowered.com/api/appdetails?appids=570";
            json report = {
                {"schemaVersion", 1},
                {"mode", "direct-ip-bypass-hosts"},
                {"host", testHost},
                {"accelerator", steamAcceleratorDiagnostics()},
                {"passed", false}
            };
            const auto answer = queryConfiguredDnsA(testHost);
            if (!answer) {
                report["error"] = "configured DNS did not return a public A record";
                std::cout << report.dump(2) << "\n";
                return 4;
            }
            const auto directUrl = replaceHttpsHost(testUrl, testHost, answer->address);
            if (directUrl.empty()) throw std::runtime_error("cannot construct direct Steam IP test URL");
            report["address"] = answer->address;
            report["dnsServer"] = answer->resolver;
            report["requestUrl"] = directUrl;
            const auto response = httpRequest(
                "GET", directUrl, L"Accept: application/json\r\n", {}, MAX_METADATA_BYTES,
                false, {}, GetTickCount64() + HTTP_ACCELERATOR_METADATA_BUDGET_MS, testHost);
            report["status"] = response.status;
            report["bytes"] = response.body.size();
            try {
                const auto body = json::parse(response.body.begin(), response.body.end());
                report["appDetailsSuccess"] = body.contains("570") && body["570"].value("success", false);
            } catch (const std::exception& error) {
                report["parseError"] = error.what();
            }
            report["passed"] = response.status >= 200 && response.status < 300 &&
                report.value("appDetailsSuccess", false);
            report["accelerator"] = steamAcceleratorDiagnostics();
            std::cout << report.dump(2) << "\n";
            return report.value("passed", false) ? 0 : 4;
        }

        if (_wcsicmp(argv[1], L"--cleanup-interrupted-transactions") == 0) {
            if (argc != 2) throw std::runtime_error("清理中断事务不接受其他参数");
            cleanupInterruptedSteamTransactions(dataRoot);
            const auto removed = cleanupInterruptedTemporaryFiles(dataRoot);
            std::cout << json{
                {"schemaVersion", 1}, {"mode", "cleanup-interrupted-transactions"},
                {"removedTemporaryFiles", removed}, {"completed", true}
            }.dump(2) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--search-artwork") == 0) {
            if (argc < 3) throw std::runtime_error("--search-artwork requires a query");
            std::string query = trim(toUtf8(argv[2]));
            std::string type = "cover";
            std::string steamAppId;
            for (int i = 3; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--type") == 0) {
                    if (++i >= argc) throw std::runtime_error("--type requires cover, long, or wallpaper");
                    type = asciiLower(trim(toUtf8(argv[i])));
                } else if (_wcsicmp(argv[i], L"--steam-app-id") == 0) {
                    if (++i >= argc) throw std::runtime_error("--steam-app-id requires a positive integer");
                    steamAppId = trim(toUtf8(argv[i]));
                } else throw std::runtime_error("Unknown artwork search option: " + toUtf8(argv[i]));
            }
            if (type != "cover" && type != "long" && type != "wallpaper") throw std::runtime_error("--type requires cover, long, or wallpaper");
            custom_steam_library::rejectBuiltinExcludedSteamTool({{"query", query}, {"steamAppId", steamAppId}});
            const auto steamSearch = steamIdArtworkSearch(steamAppId, type);
            const auto steamCandidates = steamSearch.value("candidates", json::array());
            json result = {
                {"provider", "artwork-search"}, {"query", query}, {"type", type},
                {"candidates", json::array()},
                {"providerOrder", {"steam-cdn", "playnite-igdb", "baidu-image"}},
                {"networkAttempts", json::array()}
            };
            for (const auto& item : steamCandidates) result["candidates"].push_back(item);
            for (const auto& attempt : steamSearch.value("networkAttempts", json::array())) {
                result["networkAttempts"].push_back(attempt);
            }
            if (steamSearch.contains("detailsError")) result["steamDetailsError"] = steamSearch["detailsError"];
            result["steamDetailsLoaded"] = steamSearch.value("detailsLoaded", false);
            result["steamHashedStoreAssets"] = steamSearch.value("hashedStoreAssets", false);
            if (!steamCandidates.empty()) result["steamAppId"] = steamAppId;
            try {
                const auto playnite = playniteIgdbArtworkSearch(query, type);
                for (const auto& item : playnite.value("candidates", json::array())) result["candidates"].push_back(item);
                for (const auto& attempt : playnite.value("networkAttempts", json::array())) result["networkAttempts"].push_back(attempt);
                result["playniteMatches"] = playnite.value("matches", json::array());
                result["playniteCandidateCount"] = playnite.value("candidates", json::array()).size();
                result["playniteNetworkQuery"] = playnite.value("networkQuery", query);
            } catch (const std::exception& error) {
                result["playniteError"] = error.what();
            }
            try {
                const auto baidu = baiduArtworkSearch(query, type);
                for (const auto& item : baidu.value("candidates", json::array())) result["candidates"].push_back(item);
                for (const auto& attempt : baidu.value("networkAttempts", json::array())) result["networkAttempts"].push_back(attempt);
                result["networkQuery"] = baidu.value("networkQuery", query);
                result["baiduCandidateCount"] = baidu.value("candidates", json::array()).size();
            } catch (const std::exception& error) {
                result["baiduError"] = error.what();
            }
            if (result["candidates"].empty()) throw std::runtime_error("Steam、Playnite-IGDB 和百度图片均未返回合格素材");
            result["filterInstruction"] = "Steam 官方优先，Playnite-IGDB 次之，百度图片兜底；已过滤过小图片";
            std::cout << "ARTWORK_SEARCH_JSON: " << result.dump() << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--download-artwork") == 0) {
            std::string url;
            std::string type = "cover";
            fs::path output;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--url") == 0) {
                    if (++i >= argc) throw std::runtime_error("--url requires HTTPS image URL");
                    url = trim(toUtf8(argv[i]));
                } else if (_wcsicmp(argv[i], L"--type") == 0) {
                    if (++i >= argc) throw std::runtime_error("--type requires cover, long, or wallpaper");
                    type = asciiLower(trim(toUtf8(argv[i])));
                } else if (_wcsicmp(argv[i], L"--output") == 0) {
                    if (++i >= argc) throw std::runtime_error("--output requires a path");
                    output = fs::absolute(fs::path(argv[i]));
                } else throw std::runtime_error("Unknown artwork download option: " + toUtf8(argv[i]));
            }
            if (url.empty() || output.empty()) throw std::runtime_error("--url and --output are required");
            if (type != "cover" && type != "long" && type != "wallpaper") throw std::runtime_error("--type requires cover, long, or wallpaper");
            const auto result = downloadBaiduArtwork(url, type, output);
            std::cout << "ARTWORK_DOWNLOAD_JSON: " << result.dump() << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--list-steam-accounts") == 0) {
            std::vector<fs::path> steamRoots;
            std::optional<fs::path> outputPath;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--steam-root") == 0) {
                    if (++i >= argc) throw std::runtime_error("--steam-root requires a Steam installation path");
                    steamRoots.push_back(fs::absolute(fs::path(argv[i])));
                } else if (_wcsicmp(argv[i], L"--output") == 0) {
                    if (++i >= argc) throw std::runtime_error("--output requires a JSON file");
                    outputPath = fs::absolute(fs::path(argv[i]));
                } else {
                    throw std::runtime_error("Unknown Steam account list option: " + toUtf8(argv[i]));
                }
            }
            const auto report = listSteamAccountTargets(steamRoots);
            if (outputPath) writeJsonAtomic(*outputPath, report);
            std::cout << "STEAM_ACCOUNTS: " << report.value("accounts", json::array()).size() << "\n";
            std::cout << "MULTIPLE_STEAM_ACCOUNTS: " << (report.value("multipleAccounts", false) ? "true" : "false") << "\n";
            std::cout << "EXPLICIT_ACCOUNT_SELECTION_REQUIRED: " << (report.value("requiresExplicitSelection", false) ? "true" : "false") << "\n";
            if (outputPath) std::cout << "OUTPUT: " << toUtf8(outputPath->wstring()) << "\n";
            else std::cout << report.dump(2) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--commit-refresh-steam-artwork") == 0) {
            fs::path statePath = dataRoot / L"state" / L"library-scan.json";
            fs::path outputPath = dataRoot / L"state" / L"steam-artwork-refresh.json";
            std::optional<fs::path> shortcutsFile;
            std::optional<std::string> accountId;
            std::vector<fs::path> steamRoots;
            std::set<std::string> selectedGameDirectories;
            bool steamClosedConfirmed = false;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--state") == 0) { if (++i >= argc) throw std::runtime_error("--state requires library-scan.json"); statePath = fs::absolute(fs::path(argv[i])); }
                else if (_wcsicmp(argv[i], L"--shortcuts") == 0) { if (++i >= argc) throw std::runtime_error("--shortcuts requires shortcuts.vdf"); shortcutsFile = fs::absolute(fs::path(argv[i])); }
                else if (_wcsicmp(argv[i], L"--account") == 0) { if (++i >= argc) throw std::runtime_error("--account requires a Steam account ID"); accountId = trim(toUtf8(argv[i])); }
                else if (_wcsicmp(argv[i], L"--steam-root") == 0) { if (++i >= argc) throw std::runtime_error("--steam-root requires a Steam installation path"); steamRoots.push_back(fs::absolute(fs::path(argv[i]))); }
                else if (_wcsicmp(argv[i], L"--select-game") == 0) { if (++i >= argc) throw std::runtime_error("--select-game requires a game directory"); selectedGameDirectories.insert(canonicalPathKey(fs::absolute(fs::path(argv[i])))); }
                else if (_wcsicmp(argv[i], L"--output") == 0) { if (++i >= argc) throw std::runtime_error("--output requires a JSON file"); outputPath = fs::absolute(fs::path(argv[i])); }
                else if (_wcsicmp(argv[i], L"--confirm-steam-closed") == 0) steamClosedConfirmed = true;
                else throw std::runtime_error("Unknown Steam artwork refresh option: " + toUtf8(argv[i]));
            }
            if (shortcutsFile && accountId) throw std::runtime_error("Use either --account or --shortcuts, not both");
            if (shortcutsFile && !steamRoots.empty()) throw std::runtime_error("Use either --shortcuts or --steam-root, not both");
            const auto target = selectSteamShortcutTarget(shortcutsFile, accountId, steamRoots);
            const auto result = refreshExistingSteamArtwork(dataRoot, statePath, target, outputPath, steamClosedConfirmed, selectedGameDirectories);
            std::cout << "STEAM_ARTWORK_REFRESHED: " << result.value("artworkFilesActivated", 0) << "\n";
            std::cout << "STEAM_RELOAD_REQUIRED: " << (result.value("steamReloadRequired", false) ? "true" : "false") << "\n";
            std::cout << "OUTPUT: " << toUtf8(outputPath.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--plan-add-library-to-steam") == 0 ||
            _wcsicmp(argv[1], L"--commit-add-library-to-steam") == 0) {
            const bool commit = _wcsicmp(argv[1], L"--commit-add-library-to-steam") == 0;
            fs::path statePath = dataRoot / L"state" / L"library-scan.json";
            fs::path planPath = dataRoot / L"state" / L"steam-add-plan.json";
            std::optional<fs::path> shortcutsFile;
            std::optional<std::string> accountId;
            std::vector<fs::path> steamRoots;
            std::set<std::string> selectedGameDirectories;
            bool explicitSelection = false;
            bool steamClosedConfirmed = false;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--state") == 0) {
                    if (++i >= argc) throw std::runtime_error("--state requires library-scan.json");
                    statePath = fs::absolute(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--shortcuts") == 0) {
                    if (++i >= argc) throw std::runtime_error("--shortcuts requires shortcuts.vdf");
                    shortcutsFile = fs::absolute(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--account") == 0) {
                    if (++i >= argc) throw std::runtime_error("--account requires a Steam account ID");
                    accountId = trim(toUtf8(argv[i]));
                    if (accountId->empty()) throw std::runtime_error("--account cannot be empty");
                } else if (_wcsicmp(argv[i], L"--steam-root") == 0) {
                    if (++i >= argc) throw std::runtime_error("--steam-root requires a Steam installation path");
                    steamRoots.push_back(fs::absolute(fs::path(argv[i])));
                } else if (_wcsicmp(argv[i], L"--selection-mode") == 0) {
                    if (++i >= argc) throw std::runtime_error("--selection-mode requires all-ready or explicit-game-directories");
                    const auto mode = asciiLower(trim(toUtf8(argv[i])));
                    if (mode == "all-ready") explicitSelection = false;
                    else if (mode == "explicit-game-directories") explicitSelection = true;
                    else throw std::runtime_error("--selection-mode must be all-ready or explicit-game-directories");
                } else if (_wcsicmp(argv[i], L"--select-game") == 0) {
                    if (++i >= argc) throw std::runtime_error("--select-game requires a game directory");
                    explicitSelection = true;
                    selectedGameDirectories.insert(canonicalPathKey(fs::absolute(fs::path(argv[i]))));
                } else if (_wcsicmp(argv[i], L"--output") == 0) {
                    if (++i >= argc) throw std::runtime_error("--output requires a JSON file");
                    planPath = fs::absolute(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--confirm-steam-closed") == 0) {
                    steamClosedConfirmed = true;
                } else {
                    throw std::runtime_error("Unknown Steam library add option: " + toUtf8(argv[i]));
                }
            }
            if (shortcutsFile && accountId) {
                throw std::runtime_error("Use either --account or --shortcuts, not both");
            }
            if (shortcutsFile && !steamRoots.empty()) {
                throw std::runtime_error("Use either --shortcuts or --steam-root, not both");
            }
            if (!commit && steamClosedConfirmed) {
                throw std::runtime_error("--confirm-steam-closed is only valid for commit");
            }
            const auto target = selectSteamShortcutTarget(shortcutsFile, accountId, steamRoots);
            const auto result = commit
                ? commitSteamLibraryAddPlan(
                    dataRoot, statePath, target, planPath, steamClosedConfirmed,
                    selectedGameDirectories, explicitSelection)
                : buildSteamLibraryAddPlan(
                    dataRoot, statePath, target, planPath,
                    selectedGameDirectories, explicitSelection);
            const auto summary = result.value("summary", json::object());
            std::cout << "STEAM_ADD_MODE: " << (commit ? "commit" : "plan-only") << "\n";
            std::cout << "STEAM_ACCOUNT: " << target.accountId << "\n";
            std::cout << "STEAM_SHORTCUTS: " << toUtf8(target.path.wstring()) << "\n";
            std::cout << "STEAM_ADD_READY: " << summary.value("readyToAdd", 0) << "\n";
            std::cout << "STEAM_ADD_READY_NOT_SELECTED: " << summary.value("readyNotSelected", 0) << "\n";
            std::cout << "STEAM_ADD_EXISTING: " << summary.value("alreadyInSteam", 0) << "\n";
            std::cout << "STEAM_ADD_NEEDS_PRIMARY: " << summary.value("needsPrimaryConfirmation", 0) << "\n";
            std::cout << "STEAM_ADD_NEEDS_IDENTITY: " << summary.value("needsIdentity", 0) << "\n";
            std::cout << "STEAM_ADD_NEEDS_ARTWORK: " << summary.value("needsMinimumArtwork", 0) << "\n";
            std::cout << "STEAM_ADD_COMMITTED: " << (result.value("committed", false) ? "true" : "false") << "\n";
            std::cout << "STEAM_RELOAD_REQUIRED: " << (result.value("steamReloadRequired", false) ? "true" : "false") << "\n";
            std::cout << "OUTPUT: " << toUtf8(planPath.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--plan-delete-library-from-steam") == 0 ||
            _wcsicmp(argv[1], L"--commit-delete-library-from-steam") == 0) {
            const bool commit = _wcsicmp(argv[1], L"--commit-delete-library-from-steam") == 0;
            fs::path statePath = dataRoot / L"state" / L"library-scan.json";
            fs::path planPath = dataRoot / L"state" / L"steam-delete-plan.json";
            std::optional<fs::path> shortcutsFile;
            std::optional<std::string> accountId;
            std::vector<fs::path> steamRoots;
            std::set<std::string> selectedGameDirectories;
            bool steamClosedConfirmed = false;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--state") == 0) {
                    if (++i >= argc) throw std::runtime_error("--state requires library-scan.json");
                    statePath = fs::absolute(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--shortcuts") == 0) {
                    if (++i >= argc) throw std::runtime_error("--shortcuts requires shortcuts.vdf");
                    shortcutsFile = fs::absolute(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--account") == 0) {
                    if (++i >= argc) throw std::runtime_error("--account requires a Steam account ID");
                    accountId = trim(toUtf8(argv[i]));
                    if (accountId->empty()) throw std::runtime_error("--account cannot be empty");
                } else if (_wcsicmp(argv[i], L"--steam-root") == 0) {
                    if (++i >= argc) throw std::runtime_error("--steam-root requires a Steam installation path");
                    steamRoots.push_back(fs::absolute(fs::path(argv[i])));
                } else if (_wcsicmp(argv[i], L"--select-game") == 0) {
                    if (++i >= argc) throw std::runtime_error("--select-game requires a game directory");
                    selectedGameDirectories.insert(canonicalPathKey(fs::absolute(fs::path(argv[i]))));
                } else if (_wcsicmp(argv[i], L"--output") == 0) {
                    if (++i >= argc) throw std::runtime_error("--output requires a JSON file");
                    planPath = fs::absolute(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--confirm-steam-closed") == 0) {
                    steamClosedConfirmed = true;
                } else {
                    throw std::runtime_error("Unknown Steam library delete option: " + toUtf8(argv[i]));
                }
            }
            if (selectedGameDirectories.empty()) throw std::runtime_error("--select-game is required for delete");
            if (shortcutsFile && accountId) throw std::runtime_error("Use either --account or --shortcuts, not both");
            if (shortcutsFile && !steamRoots.empty()) throw std::runtime_error("Use either --shortcuts or --steam-root, not both");
            if (!commit && steamClosedConfirmed) throw std::runtime_error("--confirm-steam-closed is only valid for commit");
            const auto target = selectSteamShortcutTarget(shortcutsFile, accountId, steamRoots);
            const auto result = commit
                ? commitSteamLibraryDeletePlan(dataRoot, statePath, target, planPath, steamClosedConfirmed, selectedGameDirectories)
                : buildSteamLibraryDeletePlan(dataRoot, statePath, target, planPath, selectedGameDirectories);
            std::cout << "STEAM_DELETE_MODE: " << (commit ? "commit" : "plan-only") << "\n";
            std::cout << "STEAM_ACCOUNT: " << target.accountId << "\n";
            std::cout << "STEAM_DELETE_READY: " << result.value("summary", json::object()).value("readyToDelete", 0) << "\n";
            std::cout << "STEAM_DELETE_COMMITTED: " << (result.value("committed", false) ? "true" : "false") << "\n";
            std::cout << "OUTPUT: " << toUtf8(planPath.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--show-pid-rule-suggestions") == 0 ||
            _wcsicmp(argv[1], L"--sync-game-whitelist") == 0 ||
            _wcsicmp(argv[1], L"--sync-pid-rules") == 0) {
            const bool showOnly = _wcsicmp(argv[1], L"--show-pid-rule-suggestions") == 0;
            bool dryRun = showOnly;
            fs::path statePath = dataRoot / L"state" / L"library-scan.json";
            fs::path whitelistPath = gameWhitelistPath();
            fs::path blacklistPath = playerBlacklistPath();
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--dry-run") == 0) {
                    if (showOnly) throw std::runtime_error("--show-pid-rule-suggestions is already read-only");
                    dryRun = true;
                } else if (_wcsicmp(argv[i], L"--state") == 0) {
                    if (++i >= argc) throw std::runtime_error("--state requires library-scan.json");
                    statePath = fs::absolute(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--whitelist-path") == 0) {
                    if (++i >= argc) throw std::runtime_error("--whitelist-path requires a file");
                    whitelistPath = fs::absolute(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--blacklist-path") == 0) {
                    if (++i >= argc) throw std::runtime_error("--blacklist-path requires a file");
                    blacklistPath = fs::absolute(fs::path(argv[i]));
                } else {
                    throw std::runtime_error("Unknown PID rule synchronization option: " + toUtf8(argv[i]));
                }
            }
            const auto state = loadLibraryScanState(statePath);
            const auto plan = planGameWhitelistSync(state, whitelistPath, blacklistPath);
            auto result = pidRuleSyncPlanJson(plan, statePath, whitelistPath, blacklistPath, dryRun);
            result["mode"] = showOnly ? "suggestions" : "explicit-whitelist-sync";
            result["applied"] = false;
            result["backupDirectory"] = nullptr;
            if (!dryRun && result.value("changed", false)) {
                const auto backupDirectory = commitGameWhitelistSync(plan, dataRoot, whitelistPath, blacklistPath);
                result["applied"] = true;
                result["backupDirectory"] = toUtf8(backupDirectory.wstring());
                result["appliedAt"] = unixTimeMs();
                writeJsonAtomic(dataRoot / L"state" / L"last-pid-rule-sync.json", result);
                appendLifecycleEvent(dataRoot, {
                    {"type", "game-whitelist-synchronized"},
                    {"contract", PID_COORDINATION_CONTRACT},
                    {"addedWhitelist", result["addedWhitelist"]},
                    {"removedExactBlacklistConflicts", result["removedExactBlacklistConflicts"]},
                    {"backupDirectory", result["backupDirectory"]},
                    {"ownsPidSelection", false}
                });
            }
            std::cout << result.dump(2) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--backup-library-data") == 0) {
            if (argc != 2) throw std::runtime_error("--backup-library-data does not accept options");
            const auto snapshot = createPortableBackupSnapshot(dataRoot, L"snapshots", "manual-full-backup");
            const auto verified = verifyPortableBackupSnapshot(snapshot);
            appendLifecycleEvent(dataRoot, {
                {"type", "portable-backup-created"},
                {"snapshot", toUtf8(snapshot.wstring())},
                {"verifiedFiles", verified.value("verifiedFiles", 0)}
            });
            std::cout << "LIBRARY_DATA_BACKUP_CREATED: " << toUtf8(snapshot.wstring()) << "\n";
            std::cout << "BACKUP_VERIFIED_FILES: " << verified.value("verifiedFiles", 0) << "\n";
            std::cout << "GAME_INSTALL_FILES_INCLUDED: false\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--verify-library-backup") == 0 ||
            _wcsicmp(argv[1], L"--restore-library-data") == 0) {
            const bool restore = _wcsicmp(argv[1], L"--restore-library-data") == 0;
            std::optional<fs::path> requestedSnapshot;
            bool latest = false;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--snapshot") == 0) {
                    if (++i >= argc) throw std::runtime_error("--snapshot requires a path or latest");
                    if (_wcsicmp(argv[i], L"latest") == 0) latest = true;
                    else requestedSnapshot = fs::absolute(fs::path(argv[i]));
                } else {
                    throw std::runtime_error("Unknown portable backup option: " + toUtf8(argv[i]));
                }
            }
            if (!latest && !requestedSnapshot) throw std::runtime_error("--snapshot is required");
            const auto snapshot = latest ? latestPortableSnapshot(dataRoot) : *requestedSnapshot;
            if (restore) {
                const auto result = restorePortableBackupSnapshot(dataRoot, snapshot);
                appendLifecycleEvent(dataRoot, {
                    {"type", "portable-backup-restored"},
                    {"snapshot", toUtf8(snapshot.wstring())},
                    {"preRestoreSnapshot", result.value("preRestoreSnapshot", std::string{})},
                    {"verifiedFiles", result.value("verifiedFiles", 0)}
                });
                std::cout << "LIBRARY_DATA_RESTORED: " << toUtf8(snapshot.wstring()) << "\n";
                std::cout << "PRE_RESTORE_SAFETY_SNAPSHOT: " << result.value("preRestoreSnapshot", std::string{}) << "\n";
                std::cout << "RESTORE_VERIFIED_FILES: " << result.value("verifiedFiles", 0) << "\n";
                std::cout << "TRANSACTIONAL_DIRECTORY_SWAP: true\n";
            } else {
                const auto result = verifyPortableBackupSnapshot(snapshot);
                std::cout << "LIBRARY_BACKUP_VALID: true\n";
                std::cout << "SNAPSHOT: " << toUtf8(snapshot.wstring()) << "\n";
                std::cout << "VERIFIED_FILES: " << result.value("verifiedFiles", 0) << "\n";
                std::cout << "VERIFIED_BYTES: " << result.value("verifiedBytes", 0) << "\n";
            }
            return 0;
        }

        if (_wcsicmp(argv[1], L"--show-network-retry-queue") == 0) {
            if (argc != 2) throw std::runtime_error("--show-network-retry-queue does not accept options");
            std::cout << loadNetworkRetryQueue(dataRoot).dump(2) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--wake-network-retries") == 0) {
            std::string reason;
            bool force = false;
            int maximum = 8;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--reason") == 0) {
                    if (++i >= argc) throw std::runtime_error("--reason requires an event name");
                    reason = trim(toUtf8(argv[i]));
                } else if (_wcsicmp(argv[i], L"--force") == 0) {
                    force = true;
                } else if (_wcsicmp(argv[i], L"--max") == 0) {
                    if (++i >= argc) throw std::runtime_error("--max requires 1-64");
                    try { maximum = std::stoi(argv[i]); } catch (...) { throw std::runtime_error("--max requires 1-64"); }
                    if (maximum < 1 || maximum > 64) throw std::runtime_error("--max requires 1-64");
                } else {
                    throw std::runtime_error("Unknown network wake option: " + toUtf8(argv[i]));
                }
            }
            if (reason.empty()) throw std::runtime_error("--reason is required; wakeups must be event-triggered");

            auto queue = loadNetworkRetryQueue(dataRoot);
            const auto now = unixTimeMs();
            std::vector<std::string> selected;
            for (const auto& [id, job] : queue["jobs"].items()) {
                if (static_cast<int>(selected.size()) >= maximum) break;
                if (job.is_object() && (force || job.value("nextAttemptAt", static_cast<uint64_t>(0)) <= now)) {
                    selected.push_back(id);
                }
            }

            int succeeded = 0;
            int failed = 0;
            int manualReview = 0;
            for (const auto& id : selected) {
                std::vector<std::wstring> arguments;
                json job;
                fs::path executable;
                bool needsManualReview = false;
                {
                    // Only the queue read/modify/write transaction is locked;
                    // the replayed network request runs after this scope.
                    DataTransactionMutex transaction;
                    queue = loadNetworkRetryQueue(dataRoot);
                    auto iterator = queue["jobs"].find(id);
                    if (iterator == queue["jobs"].end() || !iterator->is_object()) continue;
                    job = *iterator;
                    executable = fs::path(toWide(job.value("executable", std::string{})));
                    const auto currentIdentity = executableIdentity(executable);
                    if (!fs::is_regular_file(executable) || currentIdentity != job.value("executableIdentity", json::object())) {
                        job["status"] = "needs-manual-review-executable-changed";
                        job["lastWakeReason"] = reason;
                        job["lastWakeAt"] = unixTimeMs();
                        job["currentExecutableIdentity"] = currentIdentity;
                        queue["jobs"][id] = job;
                        writeJsonWithBackup(
                            networkRetryQueuePath(dataRoot), dataRoot / L"backups" / L"state", "network-retry-queue", queue);
                        needsManualReview = true;
                    } else {
                        const auto commandArgs = job.value("commandArgs", json::array());
                        if (!commandArgs.is_array()) throw std::runtime_error("Network retry job arguments are invalid");
                        for (const auto& argument : commandArgs) {
                            if (!argument.is_string()) throw std::runtime_error("Network retry job contains a non-string argument");
                            arguments.push_back(toWide(argument.get<std::string>()));
                        }
                        if (arguments.empty()) throw std::runtime_error("Network retry job has no replay arguments");
                        job["status"] = "running-event-wakeup";
                        job["lastWakeReason"] = reason;
                        job["lastWakeAt"] = unixTimeMs();
                        queue["jobs"][id] = job;
                        writeJsonWithBackup(
                            networkRetryQueuePath(dataRoot), dataRoot / L"backups" / L"state", "network-retry-queue", queue);
                    }
                }
                if (needsManualReview) {
                    appendLifecycleEvent(dataRoot, {
                        {"type", "network-retry-needs-manual-review"}, {"jobId", id},
                        {"gameExecutable", toUtf8(executable.wstring())}, {"reason", "executable-changed-or-missing"}
                    });
                    ++manualReview;
                    continue;
                }

                const DWORD exitCode = runLabChildProcess(fs::absolute(fs::path(argv[0])), arguments);
                DataTransactionMutex transaction;
                queue = loadNetworkRetryQueue(dataRoot);
                auto iterator = queue["jobs"].find(id);
                if (exitCode == 0) {
                    auto completed = iterator != queue["jobs"].end() && iterator->is_object() ? *iterator : job;
                    completed["status"] = "completed";
                    completed["completedAt"] = unixTimeMs();
                    completed["completedByWakeReason"] = reason;
                    completed["exitCode"] = exitCode;
                    queue["history"].push_back(std::move(completed));
                    if (iterator != queue["jobs"].end()) queue["jobs"].erase(iterator);
                    while (queue["history"].size() > 200) queue["history"].erase(queue["history"].begin());
                    writeJsonWithBackup(
                        networkRetryQueuePath(dataRoot), dataRoot / L"backups" / L"state", "network-retry-queue", queue);
                    appendLifecycleEvent(dataRoot, {{"type", "network-retry-completed"}, {"jobId", id}, {"wakeReason", reason}});
                    ++succeeded;
                } else {
                    // A child normally re-enqueues itself with a fresh
                    // network backoff.  If it timed out, was cancelled, or
                    // failed before reaching that transaction, repair the
                    // wake marker here so the job cannot remain permanently
                    // stuck as running-event-wakeup.
                    bool queueChanged = false;
                    const auto now = unixTimeMs();
                    if (iterator == queue["jobs"].end() || !iterator->is_object()) {
                        job["status"] = "waiting-network";
                        job["updatedAt"] = now;
                        job["lastExitCode"] = exitCode;
                        job["lastFailure"] = {
                            {"kind", "retry-worker-failed"},
                            {"stage", "network-wake"},
                            {"retryWhenNetworkReturns", true},
                            {"exitCode", exitCode}
                        };
                        job["nextAttemptAt"] = now + 15ull * 60ull * 1000ull;
                        job["consecutiveFailures"] = job.value("consecutiveFailures", 0) + 1;
                        queue["jobs"][id] = job;
                        queueChanged = true;
                    } else {
                        auto failedJob = *iterator;
                        const auto status = failedJob.value("status", std::string{});
                        if (status == "running-event-wakeup" || status == "running") {
                            const int failures = failedJob.value("consecutiveFailures", 0) + 1;
                            const auto delaySeconds = static_cast<uint64_t>(
                                (std::min)(15 * 60, 15 * (1 << (std::min)(failures - 1, 6))));
                            failedJob["status"] = "waiting-network";
                            failedJob["updatedAt"] = now;
                            failedJob["lastExitCode"] = exitCode;
                            failedJob["lastFailure"] = {
                                {"kind", "retry-worker-failed"},
                                {"stage", "network-wake"},
                                {"retryWhenNetworkReturns", true},
                                {"exitCode", exitCode}
                            };
                            failedJob["nextAttemptAt"] = now + delaySeconds * 1000ull;
                            failedJob["consecutiveFailures"] = failures;
                            queue["jobs"][id] = std::move(failedJob);
                            queueChanged = true;
                        }
                    }
                    if (queueChanged) writeJsonWithBackup(
                        networkRetryQueuePath(dataRoot), dataRoot / L"backups" / L"state", "network-retry-queue", queue);
                    ++failed;
                }
            }
            std::cout << "NETWORK_WAKE_REASON: " << reason << "\n";
            std::cout << "NETWORK_RETRIES_SELECTED: " << selected.size() << "\n";
            std::cout << "NETWORK_RETRIES_SUCCEEDED: " << succeeded << "\n";
            std::cout << "NETWORK_RETRIES_FAILED: " << failed << "\n";
            std::cout << "NETWORK_RETRIES_MANUAL_REVIEW: " << manualReview << "\n";
            std::cout << "POLLING_USED: false\n";
            return failed == 0 ? 0 : 1;
        }

        if (_wcsicmp(argv[1], L"--undo-remove-library-game") == 0) {
            std::optional<fs::path> gameDirectory;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--game-dir") == 0) {
                    if (++i >= argc) throw std::runtime_error("--game-dir requires a path");
                    std::error_code ec;
                    gameDirectory = fs::weakly_canonical(fs::path(argv[i]), ec);
                    if (ec) gameDirectory = fs::absolute(fs::path(argv[i]));
                } else {
                    throw std::runtime_error("Unknown undo removal option: " + toUtf8(argv[i]));
                }
            }
            if (!gameDirectory) throw std::runtime_error("--game-dir is required");
            const auto recovery = latestDeletedGameRecoveryPackage(dataRoot, *gameDirectory);
            const auto manifestBytes = readBinaryFile(recovery / L"recovery-manifest.json", 16u << 20);
            const auto manifest = json::parse(manifestBytes.begin(), manifestBytes.end());
            verifyRecoveryFiles(recovery, manifest);
            const auto key = canonicalPathKey(*gameDirectory);

            auto config = loadLibraryConfig(libraryConfigPath);


            auto configBaseline = config;
            config["excludedGameDirectories"] = withoutConfiguredDirectory(
                config.value("excludedGameDirectories", json::array()), *gameDirectory);
            if (manifest.contains("manualGameDirectory") && manifest["manualGameDirectory"].is_object() &&
                !configuredDirectory(config.value("manualGameDirectories", json::array()), *gameDirectory)) {
                config["manualGameDirectories"].push_back(manifest["manualGameDirectory"]);
            }
            if (manifest.contains("manualPrimary") && manifest["manualPrimary"].is_object() &&
                !config["manualPrimary"].contains(key)) {
                config["manualPrimary"][key] = manifest["manualPrimary"];
            }
            config["updatedAt"] = unixTimeMs();
            config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;

            const auto manualOverridesPath = dataRoot / L"config" / L"manual-overrides.json";
            auto overrides = loadManualOverrides(manualOverridesPath);

            auto overridesBaseline = overrides;
            if (manifest.contains("manualOverrides") && manifest["manualOverrides"].is_object()) {
                for (const auto& [overrideKey, value] : manifest["manualOverrides"].items()) {
                    if (!overrides["items"].contains(overrideKey)) overrides["items"][overrideKey] = value;
                }
                overrides = writeJsonWithBackup(manualOverridesPath, configurationBackups, "manual-overrides", overrides, &overridesBaseline);
                overridesBaseline = overrides;
            }

            const auto id = manifest.value("gameDataId", gameDataId(*gameDirectory));
            uintmax_t copied = 0;
            for (const auto& section : {L"games", L"artwork", L"cache"}) {
                copied += mergeRecoveryDirectory(
                    recovery / section, dataRoot / section / toWide(id), dataRoot);
            }
            writeJsonAtomic(recovery / L"recovery-state.json", {
                {"restoredAt", unixTimeMs()}, {"mergeDidNotOverwriteNewerFiles", true},
                {"restoredFiles", copied}
            });
            appendLifecycleEvent(dataRoot, {
                {"type", "user-removal-undone"}, {"gameDirectory", toUtf8(gameDirectory->wstring())},
                {"recoveryPackage", toUtf8(recovery.wstring())}, {"restoredFiles", copied}
            });
            std::cout << "LIBRARY_GAME_REMOVAL_UNDONE: " << toUtf8(gameDirectory->wstring()) << "\n";
            std::cout << "RECOVERY_PACKAGE: " << toUtf8(recovery.wstring()) << "\n";
            std::cout << "RECOVERY_FILES_RESTORED: " << copied << "\n";
            std::cout << "NEWER_DATA_OVERWRITTEN: false\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--remove-library-game") == 0 ||
            _wcsicmp(argv[1], L"--restore-library-game") == 0) {
            const bool restore = _wcsicmp(argv[1], L"--restore-library-game") == 0;
            std::optional<fs::path> gameDirectory;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--game-dir") == 0) {
                    if (++i >= argc) throw std::runtime_error("--game-dir requires a path");
                    std::error_code ec;
                    gameDirectory = fs::weakly_canonical(fs::path(argv[i]), ec);
                    if (ec) gameDirectory = fs::absolute(fs::path(argv[i]));
                } else {
                    throw std::runtime_error("Unknown library removal option: " + toUtf8(argv[i]));
                }
            }
            if (!gameDirectory) throw std::runtime_error("--game-dir is required");
            auto config = loadLibraryConfig(libraryConfigPath);

            auto configBaseline = config;
            config["excludedGameDirectories"] = withoutConfiguredDirectory(
                config.value("excludedGameDirectories", json::array()), *gameDirectory);
            if (restore) {
                config["updatedAt"] = unixTimeMs();
                config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;
                appendLifecycleEvent(dataRoot, {
                    {"type", "user-removal-tombstone-cleared"},
                    {"gameDirectory", toUtf8(gameDirectory->wstring())},
                    {"manualDataRestored", false}
                });
                std::cout << "LIBRARY_GAME_RESTORED_FOR_DISCOVERY: " << toUtf8(gameDirectory->wstring()) << "\n";
                std::cout << "MANUAL_DATA_RESTORED: false\n";
                std::cout << "DATA_ROOT: " << toUtf8(dataRoot.wstring()) << "\n";
                return 0;
            }

            const auto manualOverridesPath = dataRoot / L"config" / L"manual-overrides.json";
            auto overrides = loadManualOverrides(manualOverridesPath);

            auto overridesBaseline = overrides;
            const auto recovery = createDeletedGameRecoveryPackage(dataRoot, *gameDirectory, config, overrides);

            config["manualGameDirectories"] = withoutConfiguredDirectory(
                config.value("manualGameDirectories", json::array()), *gameDirectory);
            const auto key = canonicalPathKey(*gameDirectory);
            config["manualPrimary"].erase(key);
            config["excludedGameDirectories"].push_back({
                {"path", toUtf8(gameDirectory->wstring())},
                {"source", "user"},
                {"locked", true},
                {"reason", "removed-from-library"},
                {"tags", {"removed", "locked"}},
                {"updatedAt", unixTimeMs()}
            });
            config["updatedAt"] = unixTimeMs();
            config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;

            bool overridesChanged = false;
            if (overrides.contains("items") && overrides["items"].is_object()) {
                for (auto it = overrides["items"].begin(); it != overrides["items"].end();) {
                    if (pathWithin(fs::path(toWide(it.key())), *gameDirectory)) {
                        it = overrides["items"].erase(it);
                        overridesChanged = true;
                    } else {
                        ++it;
                    }
                }
            }
            if (overridesChanged) {
                overrides = writeJsonWithBackup(manualOverridesPath, configurationBackups, "manual-overrides", overrides, &overridesBaseline);
                overridesBaseline = overrides;
            }

            uintmax_t removedEntries = 0;
            const auto dataId = gameDataId(*gameDirectory);
            for (const auto& base : {dataRoot / L"games", dataRoot / L"artwork", dataRoot / L"cache"}) {
                const auto target = base / toWide(dataId);
                if (!pathWithin(target, dataRoot)) throw std::runtime_error("Refusing to remove data outside Custom Steam Library root");
                std::error_code ec;
                removedEntries += fs::remove_all(target, ec);
                if (ec) throw std::runtime_error("Cannot remove persisted Custom Steam Library game data");
            }
            const auto statePath = dataRoot / L"state" / L"library-scan.json";
            if (fs::is_regular_file(statePath)) {
                try {
                    const auto bytes = readBinaryFile(statePath, 64u << 20);
                    auto state = json::parse(bytes.begin(), bytes.end());
                    for (const auto* arrayName : {"games", "missingGames", "unscannedGames"}) {
                        json filtered = json::array();
                        for (const auto& game : state.value(arrayName, json::array())) {
                            if (canonicalPathKey(fs::path(toWide(game.value("gameDirectory", std::string{})))) != key) {
                                filtered.push_back(game);
                            }
                        }
                        state[arrayName] = std::move(filtered);
                    }
                    state["updatedAfterUserRemovalAt"] = unixTimeMs();
                    writeJsonAtomic(statePath, state);
                } catch (...) {
                    const auto corrupt = dataRoot / L"backups" / L"corrupt" /
                        (L"library-scan-during-remove-" + std::to_wstring(unixTimeMs()) + L".json");
                    fs::create_directories(corrupt.parent_path());
                    std::error_code ec;
                    fs::copy_file(statePath, corrupt, fs::copy_options::overwrite_existing, ec);
                    fs::remove(statePath, ec);
                }
            }
            appendLifecycleEvent(dataRoot, {
                {"type", "user-removed-game"}, {"gameDirectory", toUtf8(gameDirectory->wstring())},
                {"recoveryPackage", toUtf8(recovery.wstring())}, {"persistedEntriesRemoved", removedEntries},
                {"gameInstallFilesDeleted", false}
            });
            std::cout << "LIBRARY_GAME_REMOVED: " << toUtf8(gameDirectory->wstring()) << "\n";
            std::cout << "RECOVERY_PACKAGE: " << toUtf8(recovery.wstring()) << "\n";
            std::cout << "PERSISTED_DATA_ENTRIES_REMOVED: " << removedEntries << "\n";
            std::cout << "GAME_FILES_DELETED: false\n";
            std::cout << "AUTOMATIC_REDISCOVERY_BLOCKED: true\n";
            std::cout << "DATA_ROOT: " << toUtf8(dataRoot.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--set-library-scan-enabled") == 0) {
            if (argc != 3) throw std::runtime_error("--set-library-scan-enabled requires on or off");
            const auto raw = asciiLower(trim(toUtf8(argv[2])));
            if (raw != "on" && raw != "off" && raw != "true" && raw != "false") {
                throw std::runtime_error("Library scan state must be on or off");
            }
            auto config = loadLibraryConfig(libraryConfigPath);

            auto configBaseline = config;
            config["enabled"] = raw == "on" || raw == "true";
            config["updatedAt"] = unixTimeMs();
            config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;
            std::cout << "LIBRARY_SCAN_ENABLED: " << (config["enabled"].get<bool>() ? "true" : "false") << "\n";
            std::cout << "SCAN_MODE: event-driven-no-polling\n";
            std::cout << "OUTPUT: " << toUtf8(libraryConfigPath.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--set-library-scraping-enabled") == 0) {
            if (argc != 3) throw std::runtime_error("--set-library-scraping-enabled requires on or off");
            const auto raw = asciiLower(trim(toUtf8(argv[2])));
            if (raw != "on" && raw != "off" && raw != "true" && raw != "false") {
                throw std::runtime_error("Library scraping state must be on or off");
            }
            auto config = loadLibraryConfig(libraryConfigPath);

            auto configBaseline = config;
            config["automaticScrapingEnabled"] = raw == "on" || raw == "true";
            config["updatedAt"] = unixTimeMs();
            config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;
            std::cout << "LIBRARY_SCRAPING_ENABLED: "
                      << (config["automaticScrapingEnabled"].get<bool>() ? "true" : "false") << "\n";
            std::cout << "NETWORK_TRIGGER_MODE: explicit-library-open-or-event-maintenance\n";
            std::cout << "OUTPUT: " << toUtf8(libraryConfigPath.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--set-library-bucket") == 0) {
            std::optional<fs::path> gameDirectory;
            std::string bucket;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--game-dir") == 0) {
                    if (++i >= argc) throw std::runtime_error("--game-dir requires a path");
                    gameDirectory = fs::path(argv[i]);
                } else if (_wcsicmp(argv[i], L"--bucket") == 0) {
                    if (++i >= argc) throw std::runtime_error("--bucket requires a value");
                    bucket = asciiLower(trim(toUtf8(argv[i])));
                } else {
                    throw std::runtime_error("Unknown library bucket option: " + toUtf8(argv[i]));
                }
            }
            if (!gameDirectory || gameDirectory->empty()) throw std::runtime_error("--game-dir is required");
            if (bucket != "auto" && bucket != "not-in-steam" && bucket != "unclassified" && bucket != "non-game") {
                throw std::runtime_error("Bucket must be auto, not-in-steam, unclassified, or non-game");
            }
            auto config = loadLibraryConfig(libraryConfigPath);

            auto configBaseline = config;
            const auto key = canonicalPathKey(*gameDirectory);
            if (key.empty()) throw std::runtime_error("Invalid game directory");
            if (bucket == "auto") {
                config["manualBuckets"].erase(key);
            } else {
                config["manualBuckets"][key] = {
                    {"gameDirectory", toUtf8(fs::absolute(*gameDirectory).wstring())},
                    {"bucket", bucket}, {"source", "user"}, {"updatedAt", unixTimeMs()}
                };
            }
            config["updatedAt"] = unixTimeMs();
            config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;
            std::cout << "LIBRARY_BUCKET: " << bucket << "\n";
            std::cout << "OUTPUT: " << toUtf8(libraryConfigPath.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--add-library-game") == 0) {
            std::optional<fs::path> gameDirectory;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--game-dir") == 0) {
                    if (++i >= argc) throw std::runtime_error("--game-dir requires a path");
                    gameDirectory = fs::weakly_canonical(fs::path(argv[i]));
                } else {
                    throw std::runtime_error("Unknown manual game option: " + toUtf8(argv[i]));
                }
            }
            if (!gameDirectory || !fs::is_directory(*gameDirectory)) throw std::runtime_error("The game directory does not exist");
            auto config = loadLibraryConfig(libraryConfigPath);

            auto configBaseline = config;
            config["excludedGameDirectories"] = withoutConfiguredDirectory(
                config.value("excludedGameDirectories", json::array()), *gameDirectory);
            json updated = json::array();
            const auto wanted = canonicalPathKey(*gameDirectory);
            for (const auto& item : config["manualGameDirectories"]) {
                if (!item.is_object() || canonicalPathKey(fs::path(toWide(item.value("path", std::string{})))) != wanted) {
                    updated.push_back(item);
                }
            }
            updated.push_back({
                {"path", toUtf8(gameDirectory->wstring())},
                {"source", "user"},
                {"locked", true},
                {"bypassAutomaticPlatformExclusion", true},
                {"tags", {"manual-game-directory", "locked"}},
                {"updatedAt", unixTimeMs()}
            });
            config["manualGameDirectories"] = std::move(updated);
            config["updatedAt"] = unixTimeMs();
            config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;
            appendLifecycleEvent(dataRoot, {
                {"type", "manual-game-directory-added"},
                {"gameDirectory", toUtf8(gameDirectory->wstring())}, {"locked", true}
            });
            std::cout << "LIBRARY_GAME_DIRECTORY_SAVED: " << toUtf8(gameDirectory->wstring()) << "\n";
            std::cout << "BYPASS_AUTOMATIC_PLATFORM_EXCLUSION: true\n";
            std::cout << "MANUAL_LOCKED: true\n";
            std::cout << "OUTPUT: " << toUtf8(libraryConfigPath.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--set-library-primary") == 0) {
            std::optional<fs::path> gameDirectory;
            std::optional<fs::path> executable;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--game-dir") == 0) {
                    if (++i >= argc) throw std::runtime_error("--game-dir requires a path");
                    gameDirectory = fs::weakly_canonical(fs::path(argv[i]));
                } else if (_wcsicmp(argv[i], L"--exe") == 0) {
                    if (++i >= argc) throw std::runtime_error("--exe requires a path");
                    executable = fs::weakly_canonical(fs::path(argv[i]));
                } else {
                    throw std::runtime_error("Unknown primary selection option: " + toUtf8(argv[i]));
                }
            }
            if (!gameDirectory || !fs::is_directory(*gameDirectory)) throw std::runtime_error("The game directory does not exist");
            if (!executable || !fs::is_regular_file(*executable) ||
                asciiLower(toUtf8(executable->extension().wstring())) != ".exe") {
                throw std::runtime_error("The primary executable must be an existing EXE");
            }
            if (!pathWithin(*executable, *gameDirectory)) {
                throw std::runtime_error("The primary EXE must be inside its game directory");
            }
            auto config = loadLibraryConfig(libraryConfigPath);

            auto configBaseline = config;
            config["manualPrimary"][canonicalPathKey(*gameDirectory)] = {
                {"gameDirectory", toUtf8(gameDirectory->wstring())},
                {"exe", toUtf8(executable->wstring())},
                {"source", "user"},
                {"locked", true},
                {"protectedFromAutomaticOverwrite", true},
                {"tags", {"manual-primary", "locked"}},
                {"updatedAt", unixTimeMs()}
            };
            config["updatedAt"] = unixTimeMs();
            config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;
            appendLifecycleEvent(dataRoot, {
                {"type", "manual-primary-selected"},
                {"gameDirectory", toUtf8(gameDirectory->wstring())},
                {"executable", toUtf8(executable->wstring())}, {"locked", true}
            });
            std::cout << "LIBRARY_PRIMARY_SAVED: " << toUtf8(executable->wstring()) << "\n";
            std::cout << "MANUAL_LOCKED: true\n";
            std::cout << "OUTPUT: " << toUtf8(libraryConfigPath.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--scan-library") == 0) {
            std::vector<fs::path> roots;
            std::vector<fs::path> trainerRoots;
            std::optional<fs::path> requestedOutput;
            bool saveRoots = false;
            bool clearRoots = false;
            bool firstRunDefaults = false;
            bool firstRunDiscovery = false;
            int maxDepth = 5;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--root") == 0) {
                    if (++i >= argc) throw std::runtime_error("--root requires a directory");
                    roots.emplace_back(argv[i]);
                } else if (_wcsicmp(argv[i], L"--trainer-root") == 0) {
                    if (++i >= argc) throw std::runtime_error("--trainer-root requires a directory");
                    trainerRoots.emplace_back(argv[i]);
                } else if (_wcsicmp(argv[i], L"--save-roots") == 0) {
                    saveRoots = true;
                } else if (_wcsicmp(argv[i], L"--clear-roots") == 0) {
                    clearRoots = true;
                } else if (_wcsicmp(argv[i], L"--first-run-defaults") == 0) {
                    firstRunDefaults = true;
                } else if (_wcsicmp(argv[i], L"--max-depth") == 0) {
                    if (++i >= argc) throw std::runtime_error("--max-depth requires 1-8");
                    try { maxDepth = std::stoi(argv[i]); } catch (...) { throw std::runtime_error("--max-depth requires 1-8"); }
                    if (maxDepth < 1 || maxDepth > 8) throw std::runtime_error("--max-depth requires 1-8");
                } else if (wcsncmp(argv[i], L"--", 2) == 0) {
                    throw std::runtime_error("Unknown library scan option: " + toUtf8(argv[i]));
                } else if (!requestedOutput) {
                    requestedOutput = fs::path(argv[i]);
                } else {
                    throw std::runtime_error("Unexpected library scan argument");
                }
            }
            auto config = loadLibraryConfig(libraryConfigPath);

            auto configBaseline = config;
            if (clearRoots) {
                if (!roots.empty()) throw std::runtime_error("--clear-roots cannot be combined with --root");
                config["roots"] = json::array();
                config["scanRootsExplicitlyCleared"] = true;
                saveRoots = true;
            }
            const bool rootsExplicitlyCleared = config.value("scanRootsExplicitlyCleared", false);
            // Empty roots are retryable even if an older run saved "completed"
            // or "skipped-existing-roots" without retaining any usable root.
            // Apply this at the worker boundary as well as in the host so old
            // hosts/direct callers cannot fall back to the internal --root error.
            if (roots.empty() && config["roots"].empty() && !rootsExplicitlyCleared) firstRunDefaults = true;
            if (roots.empty()) {
                for (const auto& value : config["roots"]) {
                    if (value.is_string()) roots.emplace_back(toWide(value.get<std::string>()));
                }
            }
            // A user-cleared empty list must stay empty across reopen/refresh,
            // even when an older host supplies --first-run-defaults.
            if (roots.empty() && rootsExplicitlyCleared) firstRunDefaults = false;
            if (!roots.empty()) config["scanRootsExplicitlyCleared"] = false;
            if (firstRunDefaults) {
                auto& firstRun = config["firstRunDefaultScan"];
                const auto status = firstRun.value("status", std::string("pending"));
                if (roots.empty()) {
                    roots = firstRunDefaultLibraryRoots();
                    firstRunDiscovery = true;
                    // The first automatic pass is deliberately shallower
                    // than ordinary user-selected scans.
                    maxDepth = 2;
                    firstRun = {
                        {"status", "running"},
                        {"candidateSearchDepth", 2},
                        {"drivePolicy", "fixed-only"},
                        {"autoAddToSteam", false},
                        {"startedAt", unixTimeMs()},
                        {"rootCount", roots.size()}
                    };
                    saveRoots = true;
                } else if (status == "running") {
                    firstRunDiscovery = true;
                    saveRoots = true;
                } else if (!roots.empty() && status == "pending") {
                    // A pre-existing configuration is authoritative.  Do not
                    // surprise an existing user by adding a new discovery set.
                    firstRun = {
                        {"status", "skipped-existing-roots"},
                        {"candidateSearchDepth", 2},
                        {"drivePolicy", "fixed-only"},
                        {"autoAddToSteam", false},
                        {"skippedAt", unixTimeMs()},
                        {"rootCount", roots.size()}
                    };
                    saveRoots = true;
                }
            }
            if (roots.empty() && !firstRunDiscovery && !rootsExplicitlyCleared) {
                throw std::runtime_error("No library roots are configured; provide --root");
            }
            if (trainerRoots.empty()) {
                for (const auto& value : config["trainerRoots"]) {
                    if (value.is_string()) trainerRoots.emplace_back(expandEnvironmentPath(value.get<std::string>()));
                }
            }
            const auto defaultTrainer = expandEnvironmentPath("%APPDATA%\\GCM Trainers");
            if (trainerRoots.empty() && !defaultTrainer.empty()) trainerRoots.push_back(defaultTrainer);
            if (saveRoots) {
                config["roots"] = json::array();
                std::set<std::string> seen;
                for (const auto& root : roots) {
                    const auto key = canonicalPathKey(root);
                    if (seen.insert(key).second) config["roots"].push_back(toUtf8(fs::absolute(root).wstring()));
                }
                config["trainerRoots"] = json::array();
                seen.clear();
                for (const auto& root : trainerRoots) {
                    const auto key = canonicalPathKey(root);
                    if (seen.insert(key).second) config["trainerRoots"].push_back(toUtf8(fs::absolute(root).wstring()));
                }
                // Keep the standard GCM location portable.  The actual user
                // name is resolved at runtime from %APPDATA%, while custom
                // trainer roots remain absolute paths chosen by the player.
                if (!defaultTrainer.empty()) {
                    for (auto& value : config["trainerRoots"]) {
                        if (value.is_string() && canonicalPathKey(fs::path(toWide(value.get<std::string>()))) == canonicalPathKey(defaultTrainer)) {
                            value = "%APPDATA%\\GCM Trainers";
                        }
                    }
                }
                config["updatedAt"] = unixTimeMs();
                config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;
            }
            const auto persistentStatePath = dataRoot / L"state" / L"library-scan.json";
            json previousState = json::object();
            bool previousStateLoaded = false;
            std::vector<fs::path> stateCandidates;
            if (fs::is_regular_file(persistentStatePath)) stateCandidates.push_back(persistentStatePath);
            const auto stateBackups = stateBackupCandidates(dataRoot, persistentStatePath, "library-scan");
            stateCandidates.insert(stateCandidates.end(), stateBackups.begin(), stateBackups.end());
            std::optional<fs::path> corruptState;
            for (const auto& candidate : stateCandidates) {
                try {
                    const auto bytes = readBinaryFile(candidate, 64u << 20);
                    previousState = validateLibraryScanState(json::parse(bytes.begin(), bytes.end()));
                    previousStateLoaded = true;
                    if (candidate != persistentStatePath) {
                        try {
                            writeJsonWithBackup(
                                persistentStatePath, dataRoot / L"backups" / L"state", "library-scan", previousState);
                        } catch (...) {
                            // Continue this scan with the recovered state;
                            // the next successful scan retries durable repair.
                        }
                    }
                    break;
                } catch (...) {
                    if (candidate == persistentStatePath && !corruptState) {
                        corruptState = preserveCorruptStateFile(dataRoot, persistentStatePath, "library-scan");
                    }
                    continue;
                }
            }
            if (!previousStateLoaded && fs::is_regular_file(persistentStatePath)) {
                    previousState = json::object();
                    appendLifecycleEvent(dataRoot, {
                        {"type", "library-state-corrupt-detected"},
                        {"corruptBackup", corruptState ? json(toUtf8(corruptState->wstring())) : json(nullptr)},
                        {"recovery", "fresh-scan-rebuild"}
                    });
            } else if (corruptState) {
                appendLifecycleEvent(dataRoot, {
                    {"type", "library-state-recovered-from-backup"},
                    {"corruptBackup", toUtf8(corruptState->wstring())},
                    {"recovery", "state-backup"}
                });
            }

            auto report = scanLibraryRoots(roots, trainerRoots, maxDepth, config);
            // Root coverage remains in the report, including empty/unavailable
            // roots. Otherwise an empty result would erase user configuration
            // and hide the evidence needed to distinguish missing vs unscanned.
            report["summary"]["incompleteRootsRetained"] = std::count_if(
                report["roots"].begin(), report["roots"].end(), [](const json& rootItem) {
                    return rootItem.is_object() && !rootItem.value("scanComplete", true);
                });
            report["featureName"] = CUSTOM_STEAM_LIBRARY_FEATURE_NAME;
            report["persistentDataRoot"] = toUtf8(dataRoot.wstring());
            std::map<std::string, json> previousActive;
            std::map<std::string, json> previousMissing;
            std::map<std::string, json> currentActive;
            for (const auto& game : previousState.value("games", json::array())) {
                const auto key = canonicalPathKey(fs::path(toWide(jsonStringOr(game, "gameDirectory"))));
                if (!key.empty()) previousActive[key] = game;
            }
            for (const auto& game : previousState.value("missingGames", json::array())) {
                const auto key = canonicalPathKey(fs::path(toWide(jsonStringOr(game, "gameDirectory"))));
                if (!key.empty()) previousMissing[key] = game;
            }
            for (const auto& game : previousState.value("unscannedGames", json::array())) {
                const auto key = canonicalPathKey(fs::path(toWide(jsonStringOr(game, "gameDirectory"))));
                if (!key.empty() && !previousActive.contains(key)) previousActive[key] = game;
            }
            json transitions = json::array();
            for (const auto& game : report["games"]) {
                const auto directory = fs::path(toWide(jsonStringOr(game, "gameDirectory")));
                const auto key = canonicalPathKey(directory);
                if (key.empty()) continue;
                currentActive[key] = game;
                if (previousMissing.contains(key)) {
                    json event = {
                        {"type", "restored-or-reinstalled"}, {"gameDirectory", toUtf8(directory.wstring())},
                        {"primaryExecutable", jsonStringOr(game, "primaryExecutable")}
                    };
                    appendLifecycleEvent(dataRoot, event);
                    transitions.push_back(std::move(event));
                } else if (!previousActive.contains(key)) {
                    json event = {
                        {"type", "installed-or-added"}, {"gameDirectory", toUtf8(directory.wstring())},
                        {"discoverySource", jsonStringOr(game, "discoverySource", "automatic")},
                        {"primaryExecutable", jsonStringOr(game, "primaryExecutable")}
                    };
                    appendLifecycleEvent(dataRoot, event);
                    transitions.push_back(std::move(event));
                }
            }
            json missingGames = json::array();
            json unscannedGames = json::array();
            for (const auto& [key, oldGame] : previousActive) {
                if (currentActive.contains(key)) continue;
                const auto oldExe = jsonStringOr(oldGame, "primaryExecutable");
                if (!oldExe.empty() && std::any_of(report["games"].begin(), report["games"].end(), [&](const json& game) {
                    const auto exe = jsonStringOr(game, "primaryExecutable");
                    return !exe.empty() && canonicalPathKey(fs::path(toWide(exe))) == canonicalPathKey(fs::path(toWide(oldExe)));
                })) continue; // duplicate scan-directory alias is not a missing game

                auto missing = oldGame;
                const auto directory = fs::path(toWide(jsonStringOr(oldGame, "gameDirectory")));
                const bool coveredByThisScan = libraryScanCoversGame(report, config, directory);
                if (!coveredByThisScan) {
                    missing["retainedOutsideCurrentScanRoots"] = true;
                    missing["lastSeenBeforePartialScanAt"] = unixTimeMs();
                    unscannedGames.push_back(std::move(missing));
                    continue;
                }
                std::error_code ec;
                const bool directoryExists = fs::is_directory(directory, ec);
                if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) {
                    missing["retainedOutsideCurrentScanRoots"] = true;
                    missing["scanUnavailable"] = true;
                    unscannedGames.push_back(std::move(missing));
                    continue;
                }
                const auto type = directoryExists ? "primary-executable-missing-or-filtered" : "game-directory-missing";
                missing["lifecycleStatus"] = type;
                missing["missingDetectedAt"] = unixTimeMs();
                missing["dataRetained"] = true;
                missingGames.push_back(missing);
                json event = {
                    {"type", type}, {"gameDirectory", toUtf8(directory.wstring())},
                    {"previousPrimaryExecutable", jsonStringOr(oldGame, "primaryExecutable")},
                    {"dataRetained", true}
                };
                appendLifecycleEvent(dataRoot, event);
                transitions.push_back(std::move(event));
            }
            for (const auto& [key, oldMissing] : previousMissing) {
                if (currentActive.contains(key)) continue;
                const auto oldExe = jsonStringOr(oldMissing, "primaryExecutable");
                if (!oldExe.empty() && std::any_of(report["games"].begin(), report["games"].end(), [&](const json& game) {
                    const auto exe = jsonStringOr(game, "primaryExecutable");
                    return !exe.empty() && canonicalPathKey(fs::path(toWide(exe))) == canonicalPathKey(fs::path(toWide(oldExe)));
                })) continue;
                auto missing = oldMissing;
                missing["lastCheckedAt"] = unixTimeMs();
                missing["dataRetained"] = true;
                missingGames.push_back(std::move(missing));
            }
            report["missingGames"] = std::move(missingGames);
            report["unscannedGames"] = std::move(unscannedGames);
            report["lifecycleTransitions"] = std::move(transitions);
            report["summary"]["missingRetained"] = report["missingGames"].size();
            report["summary"]["unscannedRetained"] = report["unscannedGames"].size();
            report["summary"]["lifecycleTransitions"] = report["lifecycleTransitions"].size();
            const auto outputRoot = fs::absolute(requestedOutput.value_or(custom_steam_library::jobsRoot(dataRoot)));
            const auto output = outputRoot / L"library-scan.json";
            writeJsonAtomic(output, report);
            writeJsonWithBackup(
                persistentStatePath, dataRoot / L"backups" / L"state", "library-scan", report);
            for (const auto& game : report["games"]) {
                const auto directory = fs::path(toWide(game.value("gameDirectory", std::string{})));
                const auto id = gameDataId(directory);
                if (!id.empty()) writeJsonAtomic(dataRoot / L"games" / toWide(id) / L"game.json", game);
            }
            const auto& summary = report["summary"];
            if (firstRunDiscovery) {
                config["roots"] = json::array();
                for (const auto& rootItem : report["roots"]) {
                    if (rootItem.is_object() && (rootItem.value("gameDirectoryCount", 0) > 0 || !rootItem.value("scanComplete", true)))
                        config["roots"].push_back(jsonStringOr(rootItem, "path"));
                }
                const bool initialScanComplete = std::all_of(report["roots"].begin(), report["roots"].end(), [](const json& rootItem) {
                    return !rootItem.is_object() || rootItem.value("scanComplete", true);
                });
                config["firstRunDefaultScan"] = {
                    {"status", initialScanComplete ? "completed" : "running"},
                    {"candidateSearchDepth", 2},
                    {"drivePolicy", "fixed-only"},
                    {"autoAddToSteam", false},
                    {"startedAt", config["firstRunDefaultScan"].value("startedAt", unixTimeMs())},
                    {"completedAt", initialScanComplete ? json(unixTimeMs()) : json(nullptr)},
                    {"rootCount", config["roots"].size()},
                    {"gamesFound", summary.value("games", 0)},
                    {"autoSteamImport", false}
                };
                std::cout << "FIRST_RUN_DEFAULT_SCAN: " << (initialScanComplete ? "completed" : "incomplete-retry-pending") << "\n";
            } else if (report.contains("roots") && report["roots"].is_array()) {
                config["roots"] = json::array();
                for (const auto& rootItem : report["roots"]) {
                    if (rootItem.is_object() && !jsonStringOr(rootItem, "path").empty())
                        config["roots"].push_back(jsonStringOr(rootItem, "path"));
                }
            }
            config["updatedAt"] = unixTimeMs();
            config = writeJsonWithBackup(libraryConfigPath, configurationBackups, "library-config", config, &configBaseline);
            configBaseline = config;
            std::cout << "LIBRARY_ROOT_COUNT: " << config.value("roots", json::array()).size() << "\n";
            std::cout << "LIBRARY_GAMES: " << summary.value("games", 0) << "\n";
            std::cout << "LIBRARY_READY: " << summary.value("ready", 0) << "\n";
            std::cout << "LIBRARY_NEEDS_PRIMARY_CONFIRMATION: " << summary.value("needsPrimaryExeConfirmation", 0) << "\n";
            std::cout << "LIBRARY_NON_GAMES: " << summary.value("nonGames", 0) << "\n";
            std::cout << "LIBRARY_NO_EXE_SKIPPED: " << summary.value("noExecutableSkipped", 0) << "\n";
            std::cout << "LIBRARY_EMPTY_SKIPPED: " << summary.value("emptyDirectoriesSkipped", 0) << "\n";
            std::cout << "LIBRARY_PLATFORM_LIBRARIES_EXCLUDED: " << summary.value("platformLibrariesExcluded", 0) << "\n";
            std::cout << "LIBRARY_USER_REMOVED_SKIPPED: " << summary.value("userRemovedDirectoriesSkipped", 0) << "\n";
            std::cout << "LIBRARY_COLLECTION_CONTAINERS: " << summary.value("collectionContainers", 0) << "\n";
            std::cout << "LIBRARY_MANUAL_GAMES_INCLUDED: " << summary.value("manualGamesIncluded", 0) << "\n";
            std::cout << "LIBRARY_MISSING_DATA_RETAINED: " << summary.value("missingRetained", 0) << "\n";
            std::cout << "LIBRARY_UNSCANNED_DATA_RETAINED: " << summary.value("unscannedRetained", 0) << "\n";
            std::cout << "LIBRARY_LIFECYCLE_TRANSITIONS: " << summary.value("lifecycleTransitions", 0) << "\n";
            std::cout << "CONTINUOUS_SCAN_DEFAULT: " << (report.value("continuousScanDefault", true) ? "true" : "false") << "\n";
            std::cout << "SCAN_MODE: event-driven-no-polling\n";
            std::cout << "DATA_ROOT: " << toUtf8(dataRoot.wstring()) << "\n";
            std::cout << "OUTPUT: " << toUtf8(output.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--inspect-emulator") == 0) {
            SteamShortcut shortcut;
            shortcut.accountId = "diagnostic";
            shortcut.shortcutsFile = labRoot / L"diagnostic-shortcuts.vdf";
            shortcut.appName = "Diagnostic game";
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--exe") == 0) {
                    if (++i >= argc) throw std::runtime_error("--exe requires a value");
                    shortcut.exe = toUtf8(argv[i]);
                } else if (_wcsicmp(argv[i], L"--start-dir") == 0) {
                    if (++i >= argc) throw std::runtime_error("--start-dir requires a value");
                    shortcut.startDir = toUtf8(argv[i]);
                } else if (_wcsicmp(argv[i], L"--launch-options") == 0) {
                    if (++i >= argc) throw std::runtime_error("--launch-options requires a value");
                    shortcut.launchOptions = toUtf8(argv[i]);
                } else if (_wcsicmp(argv[i], L"--launch-options-env") == 0) {
                    if (++i >= argc) throw std::runtime_error("--launch-options-env requires a variable name");
                    const DWORD needed = GetEnvironmentVariableW(argv[i], nullptr, 0);
                    if (!needed) throw std::runtime_error("The launch-options environment variable is missing");
                    std::wstring value(needed, L'\0');
                    const DWORD written = GetEnvironmentVariableW(argv[i], value.data(), needed);
                    if (!written || written >= needed) throw std::runtime_error("Failed to read launch-options environment variable");
                    value.resize(written);
                    shortcut.launchOptions = toUtf8(value);
                } else if (_wcsicmp(argv[i], L"--app-name") == 0) {
                    if (++i >= argc) throw std::runtime_error("--app-name requires a value");
                    shortcut.appName = toUtf8(argv[i]);
                } else {
                    throw std::runtime_error("Unknown emulator inspection option: " + toUtf8(argv[i]));
                }
            }
            if (shortcut.exe.empty()) throw std::runtime_error("--inspect-emulator requires --exe");
            std::cout << analyzeShortcutTarget(shortcut).dump(2) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--plan-add-emulator") == 0) {
            std::optional<fs::path> emulatorExe;
            std::optional<fs::path> requestedPlanOutput;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--exe") == 0) {
                    if (++i >= argc) throw std::runtime_error("--exe requires a value");
                    emulatorExe = fs::weakly_canonical(fs::path(argv[i]));
                } else if (wcsncmp(argv[i], L"--", 2) == 0) {
                    throw std::runtime_error("Unknown emulator plan option: " + toUtf8(argv[i]));
                } else if (!requestedPlanOutput) {
                    requestedPlanOutput = fs::path(argv[i]);
                } else {
                    throw std::runtime_error("Unexpected emulator plan argument");
                }
            }
            if (!emulatorExe || !fs::is_regular_file(*emulatorExe)) {
                throw std::runtime_error("--plan-add-emulator requires an existing --exe");
            }
            const auto* profile = findEmulatorProfile(*emulatorExe);
            if (!profile) throw std::runtime_error("The EXE does not match a supported emulator profile");
            const auto outputRoot = fs::absolute(requestedPlanOutput.value_or(custom_steam_library::jobsRoot(dataRoot)));
            const auto output = outputRoot / L"emulator-add-plan.json";
            const auto quotedExe = "\"" + toUtf8(emulatorExe->wstring()) + "\"";
            const auto quotedStart = "\"" + toUtf8(emulatorExe->parent_path().wstring()) + "\"";
            writeJsonAtomic(output, {
                {"schemaVersion", 1},
                {"isolated", true},
                {"mode", "plan-only"},
                {"shortcutsVdfModified", false},
                {"gameOrEmulatorStarted", false},
                {"shortcut", {
                    {"appName", profile->name},
                    {"exe", quotedExe},
                    {"startDir", quotedStart},
                    {"launchOptions", ""},
                    {"kind", "emulator-launcher"},
                    {"romLaunchArgumentsGenerated", false}
                }},
                {"profile", {
                    {"id", profile->id},
                    {"name", profile->name},
                    {"platformLabel", profile->platformLabel},
                    {"steamArtworkAppId", profile->steamArtworkAppId > 0
                        ? json(profile->steamArtworkAppId) : json(nullptr)}
                }},
                {"artwork", {
                    {"defaultProvider", "local-emulator-brand"},
                    {"offline", true},
                    {"generatedTypes", {"tall", "long", "hero", "logo"}},
                    {"minimumRequired", {"tall", "hero"}}
                }},
                {"nextStep", "Create this one emulator shortcut through Steam, then audit its stored shortcut ID and apply only missing artwork"}
            });
            std::cout << "EMULATOR_ADD_MODE: plan-only\n";
            std::cout << "ROM_LAUNCH_ARGUMENTS_GENERATED: false\n";
            std::cout << "SHORTCUTS_VDF_MODIFIED: false\n";
            std::cout << "OUTPUT: " << toUtf8(output.wstring()) << "\n";
            return 0;
        }

        if (_wcsicmp(argv[1], L"--audit-shortcuts") == 0) {
            std::optional<fs::path> requestedAuditOutput;
            std::optional<fs::path> auditShortcuts;
            for (int i = 2; i < argc; ++i) {
                if (_wcsicmp(argv[i], L"--shortcuts") == 0) {
                    if (++i >= argc) throw std::runtime_error("--shortcuts requires a path");
                    auditShortcuts = fs::path(argv[i]);
                } else if (wcsncmp(argv[i], L"--", 2) == 0) {
                    throw std::runtime_error("Unknown audit option: " + toUtf8(argv[i]));
                } else if (!requestedAuditOutput) {
                    requestedAuditOutput = fs::path(argv[i]);
                } else {
                    throw std::runtime_error("Unexpected audit argument");
                }
            }
            auto outputRoot = fs::absolute(requestedAuditOutput.value_or(custom_steam_library::jobsRoot(dataRoot)));
            const auto report = auditSteamShortcuts(outputRoot, auditShortcuts);
            const auto& summary = report["summary"];
            std::cout << "AUDIT_TOTAL: " << summary.value("total", 0) << "\n";
            std::cout << "AUDIT_COMPLETE: " << summary.value("complete", 0) << "\n";
            std::cout << "AUDIT_NEEDS_ARTWORK: " << summary.value("needsArtwork", 0) << "\n";
            std::cout << "AUDIT_OUTPUT: " << toUtf8((outputRoot / L"shortcut-audit.json").wstring()) << "\n";
            std::cout << "SHORTCUTS_VDF_MODIFIED: false\n";
            return 0;
        }

        const fs::path exePath = fs::weakly_canonical(fs::path(argv[1]));
        if (!fs::is_regular_file(exePath) || asciiLower(toUtf8(exePath.extension().wstring())) != ".exe") {
            throw std::runtime_error("Input must be an existing EXE file");
        }
        bool simulateNoVersion = false;
        bool simulateApply = false;
        bool realApply = false;
        bool onlyMissing = false;
        bool metadataOnly = false;
        bool steamIdentityOnly = false;
        bool steamComprehensiveIdentityOnly = false;
        bool steamArtworkOnly = false;
        bool igdbArtworkOnly = false;
        bool clearManualName = false;
        bool ignoreSavedName = false;
        bool saveDirectId = false;
        bool clearDirectId = false;
        bool ignoreSavedId = false;
        std::optional<fs::path> requestedOutput;
        std::optional<fs::path> explicitShortcuts;
        std::optional<std::string> explicitManualName;
        std::optional<std::string> savedManualName;
        std::optional<int> explicitSteamAppId;
        std::optional<uint64_t> explicitIgdbId;
        std::optional<int> artworkSteamAppId;
        std::string platformHint = "pc";
        bool platformExplicit = false;
        std::optional<uint32_t> requestedShortcutId;
        std::set<std::string> faultProviders;
        for (int i = 2; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--simulate-no-version") == 0) simulateNoVersion = true;
            else if (_wcsicmp(argv[i], L"--simulate-apply") == 0) simulateApply = true;
            else if (_wcsicmp(argv[i], L"--apply-real") == 0) realApply = true;
            else if (_wcsicmp(argv[i], L"--only-missing") == 0) onlyMissing = true;
            else if (_wcsicmp(argv[i], L"--metadata-only") == 0) metadataOnly = true;
            else if (_wcsicmp(argv[i], L"--steam-identity-only") == 0) steamIdentityOnly = true;
            else if (_wcsicmp(argv[i], L"--steam-comprehensive-identity-only") == 0) steamComprehensiveIdentityOnly = true;
            else if (_wcsicmp(argv[i], L"--steam-artwork-only") == 0) steamArtworkOnly = true;
            else if (_wcsicmp(argv[i], L"--igdb-artwork-only") == 0) igdbArtworkOnly = true;
            else if (_wcsicmp(argv[i], L"--clear-name") == 0) clearManualName = true;
            else if (_wcsicmp(argv[i], L"--ignore-saved-name") == 0) ignoreSavedName = true;
            else if (_wcsicmp(argv[i], L"--save-id") == 0) saveDirectId = true;
            else if (_wcsicmp(argv[i], L"--clear-id") == 0) clearDirectId = true;
            else if (_wcsicmp(argv[i], L"--ignore-saved-id") == 0) ignoreSavedId = true;
            else if (_wcsicmp(argv[i], L"--name") == 0) {
                if (++i >= argc) throw std::runtime_error("--name requires a value");
                explicitManualName = trim(toUtf8(argv[i]));
            } else if (_wcsicmp(argv[i], L"--save-name") == 0) {
                if (++i >= argc) throw std::runtime_error("--save-name requires a value");
                savedManualName = trim(toUtf8(argv[i]));
            } else if (_wcsicmp(argv[i], L"--steam-id") == 0) {
                if (++i >= argc) throw std::runtime_error("--steam-id requires a value");
                const auto value = parsePositiveId(argv[i], "--steam-id");
                if (value > static_cast<uint64_t>((std::numeric_limits<int>::max)())) {
                    throw std::runtime_error("--steam-id is outside the supported range");
                }
                explicitSteamAppId = static_cast<int>(value);
            } else if (_wcsicmp(argv[i], L"--igdb-id") == 0) {
                if (++i >= argc) throw std::runtime_error("--igdb-id requires a value");
                explicitIgdbId = parsePositiveId(argv[i], "--igdb-id");
            } else if (_wcsicmp(argv[i], L"--artwork-steam-id") == 0) {
                if (++i >= argc) throw std::runtime_error("--artwork-steam-id requires a value");
                const auto value = parsePositiveId(argv[i], "--artwork-steam-id");
                if (value > static_cast<uint64_t>((std::numeric_limits<int>::max)())) {
                    throw std::runtime_error("--artwork-steam-id is outside the supported range");
                }
                artworkSteamAppId = static_cast<int>(value);
            } else if (_wcsicmp(argv[i], L"--shortcuts") == 0) {
                if (++i >= argc) throw std::runtime_error("--shortcuts requires a path");
                explicitShortcuts = fs::path(argv[i]);
            } else if (_wcsicmp(argv[i], L"--shortcut-id") == 0) {
                if (++i >= argc) throw std::runtime_error("--shortcut-id requires a value");
                const auto value = parsePositiveId(argv[i], "--shortcut-id");
                if (value > (std::numeric_limits<uint32_t>::max)()) {
                    throw std::runtime_error("--shortcut-id is outside the uint32 range");
                }
                requestedShortcutId = static_cast<uint32_t>(value);
            } else if (_wcsicmp(argv[i], L"--platform") == 0) {
                if (++i >= argc) throw std::runtime_error("--platform requires a value");
                platformHint = asciiLower(trim(toUtf8(argv[i])));
                if (platformHint.empty()) throw std::runtime_error("--platform cannot be empty");
                platformExplicit = true;
            } else if (_wcsicmp(argv[i], L"--fault-provider") == 0) {
                if (++i >= argc) throw std::runtime_error("--fault-provider requires a value");
                const auto provider = asciiLower(trim(toUtf8(argv[i])));
                if (provider != "steam" && provider != "playnite-igdb") {
                    throw std::runtime_error("--fault-provider must be steam or playnite-igdb");
                }
                faultProviders.insert(provider);
            } else if (_wcsicmp(argv[i], L"--fault-network") == 0) {
                if (++i >= argc) throw std::runtime_error("--fault-network requires provider:count");
                const auto value = asciiLower(trim(toUtf8(argv[i])));
                const auto separator = value.rfind(':');
                if (separator == std::string::npos) {
                    throw std::runtime_error("--fault-network requires provider:count");
                }
                const auto provider = value.substr(0, separator);
                if (provider != "steam" && provider != "playnite-igdb") {
                    throw std::runtime_error("--fault-network provider must be steam or playnite-igdb");
                }
                int count = 0;
                try {
                    size_t used = 0;
                    count = std::stoi(value.substr(separator + 1), &used, 10);
                    if (used != value.size() - separator - 1 || count < 1 || count > HTTP_MAX_ATTEMPTS) {
                        throw std::runtime_error("invalid");
                    }
                } catch (...) {
                    throw std::runtime_error("--fault-network count must be between 1 and " +
                        std::to_string(HTTP_MAX_ATTEMPTS));
                }
                g_injectedNetworkFailures[provider] = count;
            } else if (_wcsicmp(argv[i], L"--fault-artwork-network") == 0) {
                if (++i >= argc) throw std::runtime_error("--fault-artwork-network requires provider:count");
                const auto value = asciiLower(trim(toUtf8(argv[i])));
                const auto separator = value.rfind(':');
                if (separator == std::string::npos) {
                    throw std::runtime_error("--fault-artwork-network requires provider:count");
                }
                const auto provider = value.substr(0, separator);
                if (provider != "steam" && provider != "playnite-igdb") {
                    throw std::runtime_error("--fault-artwork-network provider must be steam or playnite-igdb");
                }
                int count = 0;
                try {
                    size_t used = 0;
                    count = std::stoi(value.substr(separator + 1), &used, 10);
                    if (used != value.size() - separator - 1 || count < 1 || count > 64) {
                        throw std::runtime_error("invalid");
                    }
                } catch (...) {
                    throw std::runtime_error("--fault-artwork-network count must be between 1 and 64");
                }
                g_injectedArtworkNetworkFailures[provider] = count;
                g_configuredArtworkNetworkFailures[provider] = count;
            } else if (wcsncmp(argv[i], L"--", 2) == 0) {
                throw std::runtime_error("Unknown command-line option: " + toUtf8(argv[i]));
            } else if (!requestedOutput) requestedOutput = fs::path(argv[i]);
            else throw std::runtime_error("Unexpected command-line argument");
        }
        // Refuse tool requests before persisting overrides or starting network work.
        custom_steam_library::rejectBuiltinExcludedSteamTool({{"executable", toUtf8(exePath.wstring())},
            {"manualName", explicitManualName.value_or(savedManualName.value_or(std::string{}))},
            {"steamId", explicitSteamAppId.value_or(0)}, {"steamAppId", artworkSteamAppId.value_or(0)}});
        if (simulateApply && realApply) throw std::runtime_error("--simulate-apply and --apply-real cannot be combined");
        if (explicitSteamAppId && explicitIgdbId) throw std::runtime_error("--steam-id and --igdb-id cannot be combined");
        if ((steamIdentityOnly || steamComprehensiveIdentityOnly) && igdbArtworkOnly) throw std::runtime_error("Steam identity-only and IGDB artwork-only modes cannot be combined");
        if (steamArtworkOnly && !explicitSteamAppId) throw std::runtime_error("Steam artwork-only mode requires --steam-id");
        if (steamArtworkOnly && (steamIdentityOnly || steamComprehensiveIdentityOnly || igdbArtworkOnly || metadataOnly)) {
            throw std::runtime_error("--steam-artwork-only cannot be combined with identity-only, IGDB artwork-only, or metadata-only mode");
        }
        if ((steamIdentityOnly || steamComprehensiveIdentityOnly) && explicitIgdbId) throw std::runtime_error("Steam identity-only modes cannot use an IGDB ID");
        if (igdbArtworkOnly && explicitSteamAppId) throw std::runtime_error("IGDB artwork-only mode cannot use a Steam AppID");
        if (artworkSteamAppId && !igdbArtworkOnly) throw std::runtime_error("--artwork-steam-id requires --igdb-artwork-only");
        if (steamIdentityOnly || steamComprehensiveIdentityOnly) metadataOnly = true;
        g_fastMetadataMode = steamComprehensiveIdentityOnly;
        if (saveDirectId && !explicitSteamAppId && !explicitIgdbId) {
            throw std::runtime_error("--save-id requires --steam-id or --igdb-id");
        }
        fs::path outputRoot = requestedOutput.value_or(custom_steam_library::jobsRoot(dataRoot));
        outputRoot = fs::absolute(outputRoot);

        std::optional<SteamShortcut> selectedShortcut;
        json shortcutTarget = nullptr;
        fs::path identificationPath = exePath;
        const EmulatorProfile* emulatorProfile = findEmulatorProfile(exePath);
        bool emulatorLauncher = false;
        bool emulatorGame = false;
        std::vector<std::pair<std::string, std::string>> preferredEvidence;
        if (requestedShortcutId) {
            selectedShortcut = findShortcut(exePath, explicitShortcuts, requestedShortcutId);
            shortcutTarget = analyzeShortcutTarget(*selectedShortcut);
            emulatorLauncher = shortcutTarget.value("kind", std::string{}) == "emulator-launcher";
            emulatorGame = shortcutTarget.value("kind", std::string{}) == "emulator-game";
            if (emulatorGame) {
                const auto status = shortcutTarget.value("status", std::string{});
                if (status != "ready") {
                    throw std::runtime_error("Emulator shortcut target is not ready: " + status);
                }
                const auto targetPath = shortcutTarget.value("targetPath", std::string{});
                if (targetPath.empty()) throw std::runtime_error("Emulator shortcut has no ROM/game target");
                identificationPath = fs::path(toWide(targetPath));
                if (!platformExplicit) platformHint = shortcutTarget.value("platformHint", std::string{"any"});
                const auto romTitle = trim(shortcutTarget.value("romTitle", std::string{}));
                if (!romTitle.empty()) preferredEvidence.push_back({"emulator-rom-title", romTitle});
                if (shortcutTarget.contains("serial") && shortcutTarget["serial"].is_string()) {
                    preferredEvidence.push_back({"emulator-game-serial", shortcutTarget["serial"].get<std::string>()});
                }
                auto shortcutName = cleanRomTitle(selectedShortcut->appName);
                const auto emulatorName = shortcutTarget.contains("emulator")
                    ? shortcutTarget["emulator"].value("name", std::string{}) : std::string{};
                if (!shortcutName.empty() && normalizeTitle(shortcutName) != normalizeTitle(emulatorName) &&
                    normalizeTitle(shortcutName) != normalizeTitle(toUtf8(exePath.stem().wstring()))) {
                    preferredEvidence.insert(preferredEvidence.begin(), {"steam-shortcut-name", shortcutName});
                }
            } else if (emulatorLauncher) {
                if (!platformExplicit) platformHint = shortcutTarget.value("platformHint", std::string{"any"});
                const auto emulatorName = shortcutTarget["emulator"].value("name", std::string{});
                if (!emulatorName.empty()) preferredEvidence.push_back({"emulator-profile", emulatorName});
            } else {
                const auto shortcutName = cleanRomTitle(selectedShortcut->appName);
                if (!shortcutName.empty()) preferredEvidence.push_back({"steam-shortcut-name", shortcutName});
            }
        } else if (emulatorProfile) {
            SteamShortcut launcher;
            launcher.accountId = "direct-input";
            launcher.appName = emulatorProfile->name;
            launcher.exe = toUtf8(exePath.wstring());
            launcher.startDir = toUtf8(exePath.parent_path().wstring());
            shortcutTarget = analyzeShortcutTarget(launcher);
            emulatorLauncher = true;
            if (!platformExplicit) platformHint = emulatorProfile->platformHint;
            preferredEvidence.push_back({"emulator-profile", emulatorProfile->name});
        }

        if (emulatorLauncher && (explicitSteamAppId || explicitIgdbId || saveDirectId)) {
            throw std::runtime_error("Emulator-launcher mode uses its built-in profile and does not accept game database IDs");
        }
        if (emulatorLauncher && (simulateApply || realApply) && !requestedShortcutId) {
            throw std::runtime_error("Applying emulator-launcher artwork requires --shortcut-id so a per-ROM shortcut cannot be selected by EXE alone");
        }

        const fs::path manualOverridesPath = dataRoot / L"config" / L"manual-overrides.json";
        auto manualOverrides = loadManualOverrides(manualOverridesPath);
        const auto exeKey = emulatorGame && selectedShortcut
            ? "steam-shortcut:" + selectedShortcut->accountId + ":" + std::to_string(*requestedShortcutId)
            : canonicalPathKey(exePath);
        if (clearManualName) {
            manualOverrides = updateManualOverridesAtomically(
                manualOverridesPath, configurationBackups, [&](json& latest) {
                    auto item = latest["items"].find(exeKey);
                    if (item != latest["items"].end() && item->is_object()) {
                        item->erase("name");
                        (*item)["updatedAt"] = unixTimeMs();
                        if (item->size() == 1 && item->contains("updatedAt")) latest["items"].erase(exeKey);
                    }
                });
        }
        if (clearDirectId) {
            manualOverrides = updateManualOverridesAtomically(
                manualOverridesPath, configurationBackups, [&](json& latest) {
                    auto item = latest["items"].find(exeKey);
                    if (item != latest["items"].end() && item->is_object()) {
                        // Keep the clear operation durable across automatic re-scrapes.
                        (*item)["idCleared"] = true;
                        item->erase("steamId");
                        item->erase("igdbId");
                        item->erase("formalName");
                        (*item)["updatedAt"] = unixTimeMs();
                        const bool hasName = item->contains("name") && (*item)["name"].is_string() &&
                            !trim((*item)["name"].get<std::string>()).empty();
                        if (!hasName) latest["items"].erase(exeKey);
                    }
                });
        }
        if (savedManualName) {
            if (savedManualName->empty()) throw std::runtime_error("--save-name cannot be empty");
            manualOverrides = updateManualOverridesAtomically(
                manualOverridesPath, configurationBackups, [&](json& latest) {
                    auto& item = latest["items"][exeKey];
                    if (!item.is_object()) item = json::object();
                    item["name"] = *savedManualName;
                    item["updatedAt"] = unixTimeMs();
                });
        }
        std::string manualName;
        std::string manualSource;
        std::string manualIdSource;
        if (explicitManualName && !explicitManualName->empty()) {
            manualName = *explicitManualName;
            manualSource = "manual-one-shot";
        } else if (savedManualName && !savedManualName->empty()) {
            manualName = *savedManualName;
            manualSource = "manual-saved";
        } else if (!clearManualName && !ignoreSavedName) {
            const auto persisted = persistedManualName(manualOverrides, exeKey);
            if (persisted) {
                manualName = *persisted;
                manualSource = "manual-persisted";
            }
        }
        bool manualIdCleared = false;
        const auto clearedItem = manualOverrides["items"].find(exeKey);
        if (clearedItem != manualOverrides["items"].end() && clearedItem->is_object()) {
            manualIdCleared = clearedItem->value("idCleared", false);
        }
        if (!emulatorLauncher && !explicitSteamAppId && !explicitIgdbId && !clearDirectId && !ignoreSavedId && !manualIdCleared) {
            const auto item = manualOverrides["items"].find(exeKey);
            if (item != manualOverrides["items"].end() && item->is_object()) {
                if (item->contains("steamId") && (*item)["steamId"].is_number_integer()) {
                    const auto value = (*item)["steamId"].get<int64_t>();
                    if (value > 0 && value <= (std::numeric_limits<int>::max)()) {
                        explicitSteamAppId = static_cast<int>(value);
                        manualIdSource = "manual-id-persisted";
                    }
                }
            }
        }
        if ((explicitSteamAppId || explicitIgdbId) && manualIdSource.empty()) {
            manualIdSource = saveDirectId ? "manual-id-save-request" : "manual-id-one-shot";
        }

        std::cout << "EXE: " << toUtf8(exePath.wstring()) << "\n";
        std::cout << "The EXE will not be started.\n";
        if (selectedShortcut) {
            std::cout << "SHORTCUT_ID: " << *requestedShortcutId << "\n";
            std::cout << "IDENTIFICATION_TARGET: " << toUtf8(identificationPath.wstring()) << "\n";
            if (emulatorGame || emulatorLauncher) {
                std::cout << "EMULATOR: " << shortcutTarget["emulator"].value("name", std::string{}) << "\n";
                std::cout << "EMULATOR_MODE: " << (emulatorLauncher ? "launcher-only-no-rom" : "per-rom-experimental") << "\n";
            }
        } else if (emulatorLauncher) {
            std::cout << "EMULATOR: " << emulatorProfile->name << "\n";
            std::cout << "EMULATOR_MODE: launcher-only-no-rom\n";
        }
        if (simulateNoVersion) std::cout << "SIMULATION: PE ProductName/FileDescription are intentionally ignored.\n";
        custom_steam_library::rejectBuiltinExcludedSteamTool({{"manualName", manualName},
            {"steamId", explicitSteamAppId.value_or(0)}});
        if (!manualName.empty()) std::cout << "MANUAL_NAME: " << manualName << " (" << manualSource << ")\n";
        if (explicitSteamAppId) std::cout << "MANUAL_STEAM_ID: " << *explicitSteamAppId << " (" << manualIdSource << ")\n";
        if (explicitIgdbId) std::cout << "MANUAL_IGDB_ID: " << *explicitIgdbId << " (" << manualIdSource << ")\n";
        std::cout << "PLATFORM_HINT: " << platformHint << "\n";
        for (const auto& provider : faultProviders) std::cout << "FAULT_PROVIDER: " << provider << "\n";
        for (const auto& [provider, count] : g_injectedNetworkFailures) {
            std::cout << "FAULT_NETWORK: " << provider << ":" << count << "\n";
        }
        for (const auto& [provider, count] : g_configuredArtworkNetworkFailures) {
            std::cout << "FAULT_ARTWORK_NETWORK: " << provider << ":" << count << "\n";
        }
        const auto version = (emulatorGame || emulatorLauncher)
            ? std::map<std::string, std::string>{} : versionStrings(exePath);
        const auto pathEvidence = buildPathEvidence(identificationPath);
        const bool directIdMode = explicitSteamAppId.has_value() || explicitIgdbId.has_value();
        const auto localSteamEvidence = (emulatorGame || emulatorLauncher)
            ? std::vector<LocalSteamAppIdEvidence>{}
            : discoverLocalSteamAppIds(identificationPath);
        for (const auto& item : localSteamEvidence) {
            if (!item.titleHint.empty() && item.targetMatchesExecutable) {
                preferredEvidence.push_back({"local-" + item.key + "-display-name", item.titleHint});
            }
        }
        const auto rounds = buildNameRounds(
            identificationPath, version, simulateNoVersion || emulatorGame || emulatorLauncher,
            manualName, manualSource, preferredEvidence);
        // A user-cleared ID is an explicit opt-out from local AppID
        // resurrection as well as from persisted manual IDs.
        const auto localSteamAppId = manualIdCleared
            ? std::optional<int>{}
            : unambiguousLocalSteamAppId(localSteamEvidence);
        std::set<int> localSteamDistinctIds;
        for (const auto& item : localSteamEvidence) if (item.appId > 0) localSteamDistinctIds.insert(item.appId);
        json localSteamIdentity = {
            {"evidence", localSteamAppIdEvidenceJson(localSteamEvidence)},
            {"sourceCount", localSteamEvidence.size()},
            {"distinctAppIdCount", localSteamDistinctIds.size()},
            {"conflict", localSteamDistinctIds.size() > 1},
            {"selectedAppId", localSteamAppId ? json(*localSteamAppId) : json(nullptr)},
            {"candidateAppId", localSteamAppId ? json(*localSteamAppId) : json(nullptr)},
            {"steamVerificationStatus", localSteamDistinctIds.size() > 1 ? "needs-confirmation" :
                (localSteamAppId ? "candidate" : "not-available")},
            {"status", localSteamDistinctIds.size() > 1 ? "conflict-needs-confirmation" :
                (localSteamAppId ? "candidate-needs-official-verification" : "no-unambiguous-local-appid")}
        };
        if (localSteamAppId && !directIdMode) {
            std::cout << "LOCAL_STEAM_APPID_CANDIDATE: " << *localSteamAppId
                      << " sources=" << localSteamEvidence.size() << "\n";
        } else if (localSteamDistinctIds.size() > 1 && !directIdMode) {
            std::cout << "LOCAL_STEAM_APPID_CONFLICT: " << localSteamDistinctIds.size() << "\n";
        }
        json steamRoundLog = json::array();
        json igdbRoundLog = json::array();
        json arbitration;
        std::optional<ResolvedGame> resolved;
        const auto failureMaterial = canonicalPathKey(identificationPath);
        const std::vector<unsigned char> failureBytes(failureMaterial.begin(), failureMaterial.end());
        auto failureStem = normalizeTitle(toUtf8(identificationPath.stem().wstring()));
        if (failureStem.empty()) failureStem = "game";
        fs::path failureOutput = outputRoot / L"failures" /
            toWide(failureStem + "-" + sha256(failureBytes).substr(0, 16) + ".json");
        if (selectedShortcut && requestedShortcutId) {
            failureOutput = outputRoot / L"failures" /
                toWide(selectedShortcut->accountId + "-" + std::to_string(*requestedShortcutId) + ".json");
        }
        auto replayArgumentsForNetwork = [&]() {
            json replayArguments = json::array();
            replayArguments.push_back(toUtf8(exePath.wstring()));
            for (int i = 2; i < argc; ++i) {
                const bool hasValue = _wcsicmp(argv[i], L"--fault-network") == 0 ||
                    _wcsicmp(argv[i], L"--fault-artwork-network") == 0 ||
                    _wcsicmp(argv[i], L"--fault-provider") == 0;
                if (hasValue) {
                    if (i + 1 < argc) ++i;
                    continue;
                }
                if (_wcsicmp(argv[i], L"--apply-real") == 0 ||
                    _wcsicmp(argv[i], L"--simulate-apply") == 0 ||
                    _wcsicmp(argv[i], L"--only-missing") == 0) {
                    continue;
                }
                replayArguments.push_back(toUtf8(argv[i]));
            }
            return replayArguments;
        };
        auto enqueueNetworkFailure = [&](json& failure) {
            const auto jobId = networkRetryJobId(exePath, requestedShortcutId, platformHint);
            auto retryJob = enqueueNetworkRetry(
                dataRoot, jobId, exePath, replayArgumentsForNetwork(), failure);
            failure["networkRetryJobId"] = jobId;
            failure["networkRetryNextAttemptAt"] = retryJob.value("nextAttemptAt", static_cast<uint64_t>(0));
            failure["networkRetryWakeMode"] = "event-driven-no-polling";
            failure["realSteamApplyDeferred"] = false;
            appendLifecycleEvent(dataRoot, {
                {"type", "network-retry-enqueued"}, {"jobId", jobId},
                {"gameExecutable", toUtf8(exePath.wstring())},
                {"stage", failure.value("stage", std::string{"metadata"})},
                {"nextAttemptAt", retryJob.value("nextAttemptAt", static_cast<uint64_t>(0))}
            });
            std::cout << "NETWORK_RETRY_JOB: " << jobId << "\n";
            std::cout << "NETWORK_RETRY_WAKE_MODE: event-driven-no-polling\n";
            return retryJob;
        };
        auto persistResolutionFailure = [&]() {
            fs::create_directories(failureOutput.parent_path());
            auto failure = describeResolutionFailure(steamRoundLog, igdbRoundLog, directIdMode);
            json retryJob = nullptr;
            if (failure.value("kind", std::string{}) == "network-unavailable") {
                failure["stage"] = "metadata";
                retryJob = enqueueNetworkFailure(failure);
            }
            json failureDocument = {
                {"steam", steamRoundLog}, {"playniteIgdb", igdbRoundLog},
                {"arbitration", arbitration}, {"failure", failure},
                {"networkRetry", retryJob},
                {"localSteamIdentity", localSteamIdentity},
                {"manualReviewCandidates", manualReviewCandidates(steamRoundLog, igdbRoundLog)},
                {"executable", toUtf8(exePath.wstring())},
                {"gameDirectory", toUtf8(exePath.parent_path().wstring())},
                {"shortcutId", requestedShortcutId ? json(std::to_string(*requestedShortcutId)) : json(nullptr)},
                {"identificationTarget", shortcutTarget}
            };
            writeJsonAtomic(failureOutput, failureDocument);
            // Preserve the historic single-file location as a latest-failure
            // compatibility view.  Every failure remains immutable and
            // independently auditable under failures/<stable-exe-hash>.json.
            if (!selectedShortcut) writeJsonAtomic(outputRoot / L"rounds-failed.json", failureDocument);
            std::cout << "FAILURE_OUTPUT: " << toUtf8(failureOutput.wstring()) << "\n";
            return failure;
        };
        if (emulatorLauncher) {
            if (!emulatorProfile) throw std::runtime_error("Emulator profile disappeared during launcher resolution");
            ResolvedGame launcher;
            launcher.name = emulatorProfile->name;
            launcher.displayName = emulatorProfile->name;
            launcher.query = emulatorProfile->id;
            launcher.source = "built-in-emulator-profile";
            launcher.score = 1.0;
            launcher.confident = true;
            launcher.primaryProvider = "local-emulator-brand";
            launcher.identityStatus = "verified";
            launcher.identityKind = "local-emulator-launcher";
            launcher.contentType = "emulator-launcher";
            launcher.contentRelation = "local-emulator-profile";
            resolved = launcher;
            arbitration = {
                {"decision", "built-in-emulator-profile"},
                {"networkUsed", false},
                {"romLaunchArgumentsGenerated", false},
                {"steamArtworkAppId", emulatorProfile->steamArtworkAppId > 0
                    ? json(emulatorProfile->steamArtworkAppId) : json(nullptr)}
            };
        } else {
            try {
                if (steamIdentityOnly || steamComprehensiveIdentityOnly) {
                    // Keep the legacy diagnostic Steam-only mode, but let the
                    // background mode restore the comprehensive Steam-first
                    // resolver. IGDB may assist or feed artwork fallback, but
                    // it can never become the automatic Steam AppID.
                    if (faultProviders.count("steam")) {
                        steamRoundLog.push_back({
                            {"provider", "steam"}, {"status", "injected-failure"},
                            {"reason", "--fault-provider steam"}
                        });
                        arbitration["decision"] = "automatic-steam-identity-provider-failure";
                    } else if (explicitSteamAppId) {
                        resolved = verifySteamAppId(
                            *explicitSteamAppId, steamRoundLog,
                            "automatic-steam-identity-only", "automatic-steam-id");
                    } else {
                        if (localSteamAppId) {
                            if (steamComprehensiveIdentityOnly) {
                                std::string localTitleHint;
                                for (const auto& evidence : localSteamEvidence) {
                                    if (evidence.appId == *localSteamAppId && !evidence.titleHint.empty()) {
                                        localTitleHint = evidence.titleHint;
                                        break;
                                    }
                                }
                                if (!localTitleHint.empty()) {
                                    ResolvedGame localEvidenceResolved;
                                    localEvidenceResolved.appId = *localSteamAppId;
                                    localEvidenceResolved.name = localTitleHint;
                                    localEvidenceResolved.displayName = localTitleHint;
                                    localEvidenceResolved.query = localTitleHint;
                                    localEvidenceResolved.source = "local-install-metadata-offline";
                                    localEvidenceResolved.score = 1.0;
                                    localEvidenceResolved.confident = true;
                                    localEvidenceResolved.primaryProvider = "steam";
                                    localEvidenceResolved.identityStatus = "verified";
                                    localEvidenceResolved.steamVerificationStatus = "steam-verified";
                                    localEvidenceResolved.metadataVerificationStatus = "not-available";
                                    localEvidenceResolved.identityKind = "steam-base-game-local-appid";
                                    localEvidenceResolved.contentType = "game";
                                    localEvidenceResolved.storefrontAppId = *localSteamAppId;
                                    localEvidenceResolved.baseGameAppId = *localSteamAppId;
                                    localEvidenceResolved.contentRelation = "canonical-base-game";
                                    localSteamIdentity["status"] = "verified-local-evidence";
                                    localSteamIdentity["verificationSource"] = "local-config-title-fast-path";
                                    localSteamIdentity["offlineTitleHint"] = localTitleHint;
                                    localSteamIdentity["offlineTitleScore"] = 1.0;
                                    arbitration["localSteamIdentity"] = localSteamIdentity;
                                    arbitration["decision"] = "local-steam-appid-offline-evidence";
                                    resolved = std::move(localEvidenceResolved);
                                }
                            }
                            if (!resolved) try {
                                auto localResolved = verifySteamAppId(
                                    *localSteamAppId, steamRoundLog,
                                    "local-install-metadata-appid", "local-steam-appid");
                                const auto assessment = assessLocalSteamAppIdIdentity(
                                    *localSteamAppId, localSteamEvidence, identificationPath, rounds, localResolved.name);
                                localSteamIdentity["officialVerification"] = assessment;
                                if (assessment.value("accepted", false)) {
                                    localSteamIdentity["status"] = "verified";
                                    arbitration["localSteamIdentity"] = localSteamIdentity;
                                    resolved = std::move(localResolved);
                                } else {
                                    localSteamIdentity["status"] = "rejected-needs-fuzzy-resolution";
                                }
                            } catch (const std::exception& error) {
                                NetworkEvidence localNetworkEvidence;
                                collectNetworkEvidence(steamRoundLog, localNetworkEvidence);
                                const bool networkPending = localNetworkEvidence.successfulResponses == 0 &&
                                    localNetworkEvidence.retryableFailures > 0;
                                localSteamIdentity["status"] = networkPending
                                    ? "network-pending" : "verification-failed-falling-back-to-fuzzy";
                                localSteamIdentity["steamVerificationStatus"] = networkPending
                                    ? "network-pending" : "needs-confirmation";
                                localSteamIdentity["verificationError"] = error.what();
                                if (networkPending && steamComprehensiveIdentityOnly) {
                                    std::string localTitleHint;
                                    double localTitleScore = 0.0;
                                    for (const auto& evidence : localSteamEvidence) {
                                        if (evidence.appId != *localSteamAppId || evidence.titleHint.empty()) continue;
                                        // A provider config's inline title is an
                                        // independent identity signal. It does
                                        // not need to match the executable's
                                        // often localized PE name byte-for-byte.
                                        if (localTitleHint.empty()) {
                                            localTitleHint = evidence.titleHint;
                                            localTitleScore = 1.0;
                                        }
                                        for (const auto& round : rounds) {
                                            if (!round.skipReason.empty()) continue;
                                            const double score = scoreTitle(evidence.titleHint, round.value);
                                            if (score > localTitleScore) {
                                                localTitleScore = score;
                                                localTitleHint = evidence.titleHint;
                                            }
                                        }
                                    }
                                    if (!localTitleHint.empty() && localTitleScore >= 0.94) {
                                        ResolvedGame localEvidenceResolved;
                                        localEvidenceResolved.appId = *localSteamAppId;
                                        localEvidenceResolved.name = localTitleHint;
                                        localEvidenceResolved.displayName = localTitleHint;
                                        localEvidenceResolved.query = localTitleHint;
                                        localEvidenceResolved.source = "local-install-metadata-offline";
                                        localEvidenceResolved.score = localTitleScore;
                                        localEvidenceResolved.confident = true;
                                        localEvidenceResolved.primaryProvider = "steam";
                                        localEvidenceResolved.identityStatus = "verified";
                                        localEvidenceResolved.steamVerificationStatus = "steam-verified";
                                        localEvidenceResolved.metadataVerificationStatus = "not-available";
                                        localEvidenceResolved.identityKind = "steam-base-game-local-appid";
                                        localEvidenceResolved.contentType = "game";
                                        localEvidenceResolved.storefrontAppId = *localSteamAppId;
                                        localEvidenceResolved.baseGameAppId = *localSteamAppId;
                                        localEvidenceResolved.contentRelation = "canonical-base-game";
                                        localSteamIdentity["status"] = "verified-local-evidence";
                                        localSteamIdentity["verificationSource"] = "local-config-title-network-pending";
                                        localSteamIdentity["offlineTitleHint"] = localTitleHint;
                                        localSteamIdentity["offlineTitleScore"] = localTitleScore;
                                        arbitration["localSteamIdentity"] = localSteamIdentity;
                                        arbitration["decision"] = "local-steam-appid-offline-evidence";
                                        resolved = std::move(localEvidenceResolved);
                                    }
                                }
                            }
                        }
                        if (!resolved) {
                            resolved = steamComprehensiveIdentityOnly
                                ? resolveMultiSource(
                                    rounds, pathEvidence, platformHint, faultProviders,
                                    steamRoundLog, igdbRoundLog, arbitration, true)
                                : resolveSteamGame(rounds, pathEvidence, steamRoundLog);
                            arbitration["localSteamIdentity"] = localSteamIdentity;
                            arbitration["decision"] = resolved && resolved->confident
                                ? (steamComprehensiveIdentityOnly
                                    ? "automatic-steam-comprehensive-identity"
                                    : "automatic-steam-identity-only")
                                : (steamComprehensiveIdentityOnly
                                    ? "automatic-steam-comprehensive-identity-unresolved"
                                    : "automatic-steam-identity-unresolved");
                        }
                    }
                    if (resolved && steamIdentityOnly) {
                        resolved->igdbId = 0;
                        resolved->igdbDetails = json::object();
                        resolved->metadataVerificationStatus = "not-available";
                    }
                } else if (steamArtworkOnly) {
                    // The identity stage has already verified and persisted
                    // this Steam AppID.  Fetch AppDetails once for the
                    // artwork stage as well: current Steam records commonly
                    // expose only hashed asset paths, so deterministic CDN
                    // filenames alone cannot find their images.
                    const auto fallbackName = !pathEvidence.empty()
                        ? pathEvidence.front() : toUtf8(exePath.stem().wstring());
                    ResolvedGame artwork;
                    artwork.appId = *explicitSteamAppId;
                    artwork.name = fallbackName;
                    artwork.displayName = fallbackName;
                    artwork.query = std::to_string(*explicitSteamAppId);
                    artwork.source = "steam-artwork-only";
                    artwork.score = 1.0;
                    artwork.details = json::object();
                    artwork.baseGameDetails = json::object();
                    artwork.confident = true;
                    artwork.primaryProvider = "steam";
                    artwork.identityStatus = "verified";
                    artwork.steamVerificationStatus = "steam-verified";
                    artwork.metadataVerificationStatus = "not-available";
                    artwork.identityKind = "steam-base-game";
                    artwork.contentType = "game";
                    artwork.storefrontAppId = *explicitSteamAppId;
                    artwork.baseGameAppId = *explicitSteamAppId;
                    artwork.contentRelation = "canonical-base-game";
                    artwork.resolverVersion = STEAM_RESOLVER_VERSION;
                    json artworkDetailsAttempts = json::array();
                    try {
                        const auto steamDetails = fetchSteamDetails(*explicitSteamAppId, artworkDetailsAttempts);
                        artwork.details = steamDetails.data;
                        artwork.baseGameDetails = steamDetails.data;
                        if (!steamDetails.name.empty()) {
                            artwork.name = steamDetails.name;
                            artwork.displayName = steamDetails.name;
                        }
                        artwork.metadataVerificationStatus = "metadata-verified";
                    } catch (const std::exception& error) {
                        // A verified SteamID must remain usable when AppDetails
                        // is temporarily unavailable. The legacy CDN list is
                        // still retained as a last-resort compatibility path.
                        artworkDetailsAttempts.push_back({
                            {"stage", "steam-artwork-details"},
                            {"ok", false},
                            {"error", error.what()}
                        });
                    }
                    resolved = std::move(artwork);
                    arbitration = {
                        {"decision", "automatic-steam-artwork-only"},
                        {"steamAppId", *explicitSteamAppId},
                        {"metadataLookup", artworkDetailsAttempts.empty() ? "not-attempted" :
                            (resolved->metadataVerificationStatus == "metadata-verified" ? "appdetails" : "failed")},
                        {"metadataNetworkAttempts", artworkDetailsAttempts},
                        {"artworkAuthority", "steam-appdetails-then-cdn"}
                    };
                } else if (igdbArtworkOnly) {
                    // Second-round artwork may use IGDB, but this mode must not
                    // search Steam or promote an IGDB match into a SteamID.
                    try {
                    if (faultProviders.count("playnite-igdb")) {
                        igdbRoundLog.push_back({
                            {"provider", "playnite-igdb"}, {"status", "injected-failure"},
                            {"reason", "--fault-provider playnite-igdb"}
                        });
                        throw std::runtime_error("Playnite IGDB provider failure was injected");
                    } else if (explicitIgdbId) {
                        resolved = verifyIgdbId(*explicitIgdbId, platformHint, igdbRoundLog);
                    } else {
                        resolved = resolveIgdbGame(rounds, pathEvidence, platformHint, igdbRoundLog);
                    }
                    // In artwork-only mode IGDB is a visual fallback, not a
                    // new identity authority. A strong verified review
                    // candidate is therefore sufficient even when the
                    // resolver's identity margin is too narrow to promote it
                    // during full identity arbitration.
                    if (resolved && !resolved->confident && resolved->score >= 0.80) {
                        resolved->confident = true;
                        resolved->identityStatus = "metadata-verified";
                        resolved->metadataVerificationStatus = "metadata-verified";
                    }
                    if (!resolved || !resolved->confident) resolved.reset();
                    } catch (const std::exception& error) {
                        igdbRoundLog.push_back({{"stage", "visual-fallback-resolution"}, {"ok", false}, {"error", error.what()}});
                        resolved.reset();
                    }
                    if (!resolved) {
                        ResolvedGame visual;
                        // Prefer the same human-readable evidence as manual search,
                        // notably PE ProductName; directory/EXE aliases such as
                        // "Ishin2" are only a last resort and are not an identity.
                        visual.name = artworkFallbackTitle(manualName, rounds, pathEvidence, exePath);
                        visual.displayName = visual.name;
                        visual.query = visual.name;
                        visual.primaryProvider = "artwork-fallback";
                        visual.identityStatus = "unverified";
                        visual.steamVerificationStatus = "not-available";
                        visual.metadataVerificationStatus = "not-available";
                        visual.identityKind = "visual-only-no-identity-authority";
                        visual.source = "visual-only-fallback";
                        visual.resolverVersion = STEAM_RESOLVER_VERSION;
                        resolved = std::move(visual);
                    }
                    if (artworkSteamAppId) {
                        // Preserve the already verified Steam identity while
                        // explicitly recording that the selected artwork came
                        // from IGDB. This keeps the item eligible for the
                        // Steam add plan after the fallback succeeds.
                        resolved->appId = *artworkSteamAppId;
                        resolved->storefrontAppId = *artworkSteamAppId;
                        resolved->baseGameAppId = *artworkSteamAppId;
                        resolved->matchedContentAppId = 0;
                        resolved->primaryProvider = "steam";
                        resolved->source = "igdb-artwork-fallback";
                        resolved->identityStatus = "verified";
                        resolved->steamVerificationStatus = "steam-verified";
                        resolved->metadataVerificationStatus = "metadata-verified";
                        resolved->identityKind = "steam-base-game-with-igdb-artwork-fallback";
                        resolved->contentType = "game";
                        resolved->contentRelation = "canonical-base-game";
                        resolved->resolverVersion = STEAM_RESOLVER_VERSION;
                    }
                    if (!artworkSteamAppId) {
                        resolved->appId = 0;
                        resolved->storefrontAppId = 0;
                        resolved->baseGameAppId = 0;
                        resolved->matchedContentAppId = 0;
                        resolved->primaryProvider = resolved->igdbId > 0 ? "playnite-igdb" : "artwork-fallback";
                        resolved->steamVerificationStatus = "not-available";
                        resolved->metadataVerificationStatus = resolved->igdbId > 0 ? "metadata-verified" : "not-available";
                    }
                    arbitration = {
                        {"decision", "automatic-igdb-artwork-only"},
                        {"steamIdentityAuthority", "disabled-for-artwork-fallback"},
                        {"igdbId", resolved->igdbId},
                        {"preservedSteamAppId", artworkSteamAppId
                            ? json(*artworkSteamAppId) : json(nullptr)}
                    };
                } else if (directIdMode) {
                    resolved = resolveDirectId(
                        explicitSteamAppId, explicitIgdbId, pathEvidence, platformHint, faultProviders,
                        steamRoundLog, igdbRoundLog, arbitration);
                } else {
                    if (localSteamAppId) {
                        try {
                            auto localResolved = resolveDirectId(
                                localSteamAppId, std::nullopt, pathEvidence, platformHint, faultProviders,
                                steamRoundLog, igdbRoundLog, arbitration, "local-install-metadata");
                            const auto assessment = assessLocalSteamAppIdIdentity(
                                *localSteamAppId, localSteamEvidence, identificationPath, rounds, localResolved.name);
                            localSteamIdentity["officialVerification"] = assessment;
                            if (assessment.value("accepted", false)) {
                                localSteamIdentity["status"] = "verified";
                                arbitration["localSteamIdentity"] = localSteamIdentity;
                                resolved = std::move(localResolved);
                            } else {
                                localSteamIdentity["status"] = "rejected-needs-fuzzy-resolution";
                            }
                        } catch (const std::exception& error) {
                            NetworkEvidence localNetworkEvidence;
                            collectNetworkEvidence(steamRoundLog, localNetworkEvidence);
                            const bool networkPending = localNetworkEvidence.successfulResponses == 0 &&
                                localNetworkEvidence.retryableFailures > 0;
                            localSteamIdentity["status"] = networkPending
                                ? "network-pending" : "verification-failed-falling-back-to-fuzzy";
                            localSteamIdentity["steamVerificationStatus"] = networkPending
                                ? "network-pending" : "needs-confirmation";
                            localSteamIdentity["verificationError"] = error.what();
                        }
                    }
                    if (!resolved) {
                        resolved = resolveMultiSource(
                            rounds, pathEvidence, platformHint, faultProviders,
                            steamRoundLog, igdbRoundLog, arbitration);
                        if (resolved && localSteamAppId) {
                            const bool steamVerified = resolved->primaryProvider == "steam" &&
                                resolved->appId > 0 &&
                                resolved->steamVerificationStatus == "steam-verified";
                            if (!steamVerified) {
                                // Keep the local AppID visible as a candidate
                                // when metadata succeeds but official Steam
                                // verification is temporarily unavailable.
                                resolved->steamIdCandidate = *localSteamAppId;
                                resolved->steamVerificationStatus =
                                    localSteamIdentity.value("status", std::string{}) == "network-pending"
                                    ? "network-pending" : "needs-confirmation";
                                if (resolved->primaryProvider == "playnite-igdb") {
                                    resolved->identityStatus = resolved->confident
                                        ? "metadata-verified" : "needs-confirmation";
                                    arbitration["decision"] = localSteamIdentity.value("status", std::string{}) == "network-pending"
                                        ? "metadata-fallback-steam-verification-pending-network"
                                        : "metadata-fallback-steam-verification-required";
                                }
                            }
                        }
                        arbitration["localSteamIdentity"] = localSteamIdentity;
                    }
                }
            } catch (const std::exception&) {
                const auto failure = persistResolutionFailure();
                std::cout << "FAILURE_KIND: " << failure.value("kind", std::string{"unrecognized"}) << "\n";
                throw;
            }
        }
        if (!resolved || resolved->name.empty()) {
            const auto failure = persistResolutionFailure();
            std::cout << "FAILURE_KIND: " << failure.value("kind", std::string{"unrecognized"}) << "\n";
            throw std::runtime_error("No provider title passed multi-source formal-name verification");
        }
        if (saveDirectId) {
            manualOverrides = updateManualOverridesAtomically(
                manualOverridesPath, configurationBackups, [&](json& latest) {
                    auto& item = latest["items"][exeKey];
                    if (!item.is_object()) item = json::object();
                    if (explicitSteamAppId) {
                        item.erase("idCleared");
                        item["steamId"] = resolved->appId;
                        item["storefrontSteamId"] = resolved->storefrontAppId > 0
                            ? resolved->storefrontAppId : *explicitSteamAppId;
                        item.erase("igdbId");
                    } else {
                        item["igdbId"] = *explicitIgdbId;
                        item.erase("steamId");
                        item.erase("storefrontSteamId");
                    }
                    item["formalName"] = resolved->name;
                    item["updatedAt"] = unixTimeMs();
                });
            manualIdSource = "manual-id-saved";
        }

        if (resolved->displayName.empty()) resolved->displayName = resolved->name;
        const std::string stem = normalizeTitle((emulatorGame || emulatorLauncher)
            ? resolved->displayName : toUtf8(identificationPath.stem().wstring()));
        const std::string providerId = emulatorLauncher ? "emulator-local" :
            (resolved->appId > 0 ? std::to_string(resolved->appId)
                : "igdb-" + std::to_string(resolved->igdbId));
        const fs::path gameOutput = outputRoot / toWide((stem.empty() ? "game" : stem) + "-" + providerId);
        fs::create_directories(gameOutput);
        writeJsonAtomic(gameOutput / L"rounds.json", {
            {"steam", steamRoundLog},
            {"playniteIgdb", igdbRoundLog},
            {"arbitration", arbitration}
        });

        json identityNetworkRetry = nullptr;
        if (!emulatorLauncher && resolved->steamVerificationStatus == "network-pending") {
            json identityFailure = {
                {"kind", "network-unavailable"},
                {"stage", "steam-identity"},
                {"retryWhenNetworkReturns", true},
                {"candidateAppId", resolved->steamIdCandidate > 0
                    ? json(resolved->steamIdCandidate) : json(nullptr)},
                {"primaryProvider", resolved->primaryProvider},
                {"metadataVerificationStatus", resolved->metadataVerificationStatus},
                {"arbitrationDecision", arbitration.value("decision", std::string{})}
            };
            identityNetworkRetry = enqueueNetworkFailure(identityFailure);
        }

        const json metadata = buildGameMetadata(*resolved);
        const int resolvedReleaseYear = releaseYearFromValue(metadata.value("releaseDate", json(nullptr)));
        const auto resolvedReleaseDisplay = releaseDisplayFromValue(metadata.value("releaseDate", json(nullptr)));
        json manifest = {
            {"schemaVersion", 2},
            {"isolated", true},
            {"exe", toUtf8(exePath.wstring())},
            {"exeStarted", false},
            {"contentKind", emulatorLauncher ? "emulator-launcher" : (emulatorGame ? "emulator-game" : "native-game")},
            {"romLaunchArgumentsGenerated", false},
            {"shortcutId", requestedShortcutId ? json(std::to_string(*requestedShortcutId)) : json(nullptr)},
            {"identificationPath", toUtf8(identificationPath.wstring())},
            {"identificationTarget", shortcutTarget},
            {"simulateNoVersion", simulateNoVersion},
            {"manualName", manualName.empty() ? json(nullptr) : json(manualName)},
            {"manualNameSource", manualSource.empty() ? json(nullptr) : json(manualSource)},
            {"manualSteamAppId", explicitSteamAppId ? json(*explicitSteamAppId) : json(nullptr)},
            {"manualIgdbId", explicitIgdbId ? json(*explicitIgdbId) : json(nullptr)},
            {"manualIdSource", manualIdSource.empty() ? json(nullptr) : json(manualIdSource)},
            {"localSteamIdentity", localSteamIdentity},
            {"steamIdentityRetry", identityNetworkRetry},
            {"networkRetry", identityNetworkRetry},
            {"pathEvidence", pathEvidence},
            {"platformHint", platformHint},
            {"faultProviders", faultProviders},
            {"faultNetworkAttempts", g_injectedNetworkFailures},
            {"faultArtworkNetworkAttempts", g_configuredArtworkNetworkFailures},
            {"networkPolicy", {
                {"engine", "WinHTTP"},
                {"profile", "weak-network-default"},
                {"userAgent", "QQ/1.0"},
                {"steamNoProxy", true},
                {"nonSteamProxy", "none"},
                {"steamAccelerator", {
                    {"mode", "transparent-host-forwarding"},
                    {"listenAddress", "127.0.0.1"},
                    {"listenPorts", {80, 443}},
                    {"requiresHttpProxy", false},
                    {"preserveOriginalHostAndSni", true},
                    {"route", "enabled steamstore/steamcdn aliases send requests to the local 80/443 forwarder"},
                    {"aliasFallback", "retry the original canonical URL after a local timeout or HTTP failure"}
                }},
                {"acceleratorDiagnostics", steamAcceleratorDiagnostics()},
                {"sessionReuse", "one WinHTTP session per worker; reset after transport failure"},
                {"resolveTimeoutMs", HTTP_RESOLVE_TIMEOUT_MS},
                {"connectTimeoutMs", HTTP_CONNECT_TIMEOUT_MS},
                {"sendReceiveIdleTimeoutMs", HTTP_TIMEOUT_MS},
                {"metadataLogicalBudgetMs", HTTP_METADATA_BUDGET_MS},
                {"imageLogicalBudgetMs", HTTP_IMAGE_BUDGET_MS},
                {"acceleratorResolveTimeoutMs", HTTP_ACCELERATOR_RESOLVE_TIMEOUT_MS},
                {"acceleratorConnectTimeoutMs", HTTP_ACCELERATOR_CONNECT_TIMEOUT_MS},
                {"acceleratorSendReceiveIdleTimeoutMs", HTTP_ACCELERATOR_TIMEOUT_MS},
                {"acceleratorMetadataBudgetMs", HTTP_ACCELERATOR_METADATA_BUDGET_MS},
                {"acceleratorImageBudgetMs", HTTP_ACCELERATOR_IMAGE_BUDGET_MS},
                {"maxAttempts", HTTP_MAX_ATTEMPTS},
                {"retryStatuses", {408, 425, 429, 500, 502, 503, 504}},
                {"retryAfterCapMs", HTTP_MAX_RETRY_AFTER_MS},
                {"reconnectPolicy", "reuse pooled connection for normal requests; fresh WinHTTP session after transport failure"},
                {"contentLengthValidation", true},
                {"partialResponseRetry", true},
                {"igdbCoverAlternateTransform", true},
                {"metadataLimitBytes", MAX_METADATA_BYTES},
                {"imageLimitBytes", MAX_IMAGE_BYTES},
                {"redirectPolicy", "always"},
                {"providers", {
                    {"steam", {
                        {"search", "https://store.steampowered.com/search/results/?json=1"},
                        {"officialFuzzySearch", "https://store.steampowered.com/search/results/?json=1"},
                        {"officialFuzzyPolicy", "category1=998 first, unfiltered only when empty, api/storesearch fallback"},
                        {"candidatePolicy", "merge all query rounds globally, then appdetails canonicalization and margin arbitration"},
                        {"details", "https://store.steampowered.com/api/appdetails"},
                        {"fallbackSearch", "https://store.steampowered.com/api/storesearch/"},
                        {"artwork", "Steam CDN"},
                        {"authentication", "none"}
                    }},
                    {"steamGridDb", {
                        {"search", "https://www.steamgriddb.com/api/public/search/autocomplete"},
                        {"assets", "https://www.steamgriddb.com/api/public/search/assets"},
                        {"artwork", "https://cdn2.steamgriddb.com/icon/"},
                        {"authentication", "public-web-endpoints"},
                        {"scope", "optional automatic icon only"}
                    }},
                    {"playniteIgdb", {
                        {"base", "https://api2.playnite.link/api/"},
                        {"search", "POST igdb/search"},
                        {"details", "GET igdb/game/{id}"},
                        {"artwork", "https://images.igdb.com/"},
                        {"authentication", "none-via-Playnite-backend"},
                        {"clientTimeoutMs", HTTP_TIMEOUT_MS}
                    }},
                    {"localEmulatorBrand", {
                        {"mode", "generated-from-exe-icon-or-initials"},
                        {"offline", true},
                        {"requiredFallback", {"tall", "hero"}}
                    }}
                }}
            }},
            {"match", {
                {"primaryProvider", resolved->primaryProvider},
                {"appId", resolved->appId > 0 ? json(resolved->appId) : json(nullptr)},
                {"identityStatus", resolved->identityStatus},
                {"steamVerificationStatus", resolved->steamVerificationStatus},
                {"metadataVerificationStatus", resolved->metadataVerificationStatus},
                {"steamIdCandidate", resolved->steamIdCandidate > 0 ? json(resolved->steamIdCandidate) : json(nullptr)},
                {"identityKind", resolved->identityKind},
                {"displayName", resolved->displayName},
                {"contentType", resolved->contentType},
                {"storefrontAppId", resolved->storefrontAppId > 0 ? json(resolved->storefrontAppId) : json(nullptr)},
                {"matchedContentAppId", resolved->matchedContentAppId > 0 ? json(resolved->matchedContentAppId) : json(nullptr)},
                {"matchedContentName", resolved->matchedContentName.empty() ? json(nullptr) : json(resolved->matchedContentName)},
                {"matchedContentType", resolved->matchedContentType.empty() ? json(nullptr) : json(resolved->matchedContentType)},
                {"contentRelation", resolved->contentRelation.empty() ? json(nullptr) : json(resolved->contentRelation)},
                {"baseGameAppId", resolved->baseGameAppId > 0 ? json(resolved->baseGameAppId) : json(nullptr)},
                {"resolverVersion", resolved->resolverVersion},
                {"igdbId", resolved->igdbId > 0 ? json(resolved->igdbId) : json(nullptr)},
                {"formalName", resolved->name},
                {"year", resolvedReleaseYear > 0 ? json(resolvedReleaseYear) : json(nullptr)},
                {"releaseYear", resolvedReleaseYear > 0 ? json(resolvedReleaseYear) : json(nullptr)},
                {"releaseDate", resolvedReleaseDisplay.empty() ? json(nullptr) : json(resolvedReleaseDisplay)},
                {"query", resolved->query},
                {"source", resolved->source},
                {"score", resolved->score},
                {"platforms", igdbPlatforms(resolved->igdbDetails)},
                {"arbitration", arbitration}
            }},
            {"metadata", metadata},
            {"artworkQualityPolicy", artworkQualityPolicyJson()},
            {"artwork", json::array()}
        };

        bool tallOk = metadataOnly;
        bool heroOk = false;
        int successCount = 0;
        json artworkNetworkFailures = json::array();
        json previousPortableManifest = json::object();
        if (!metadataOnly) {
            // Every automatic pass preserves usable durable slots; a partial
            // retry must never turn a known image into an empty slot.
            // IGDB is a second-round supplement. Read the last durable
            // manifest so this pass can request only missing slots instead of
            // downloading and rewriting artwork that Steam already has.
            try {
                const auto stableGameDirectory = persistentGameDirectoryForExecutable(dataRoot, exePath);
                const auto stableId = gameDataId(stableGameDirectory);
                const auto previousPath = dataRoot / L"artwork" / toWide(stableId) / L"manifest.json";
                if (!stableId.empty() && fs::is_regular_file(previousPath)) {
                    previousPortableManifest = loadJsonDocument(previousPath);
                    if (!previousPortableManifest.is_object() ||
                        canonicalPathKey(fs::path(toWide(jsonStringOr(previousPortableManifest, "exe")))) !=
                            canonicalPathKey(exePath)) {
                        previousPortableManifest = json::object();
                    }
                }
            } catch (...) {
                previousPortableManifest = json::object();
            }
        }
        if (metadataOnly) {
            manifest["metadataOnly"] = true;
            manifest["artworkSkipped"] = true;
            manifest["ok"] = true;
            manifest["required"] = {{"tall", false}, {"hero", false}};
            manifest["successCount"] = 0;
            // Identity is intentionally a separate first stage. Copy the
            // existing artwork into this worker result for diagnostics, but
            // never write it back to the durable artwork manifest. A parallel
            // IGDB fallback may have just completed a missing long slot; a
            // metadata-only retry must not overwrite that newer result.
            try {
                const auto stableGameDirectory = persistentGameDirectoryForExecutable(dataRoot, exePath);
                const auto stableId = gameDataId(stableGameDirectory);
                json previous = json::object();
                const auto previousPath = dataRoot / L"artwork" / toWide(stableId) / L"manifest.json";
                if (!stableId.empty() && fs::is_regular_file(previousPath)) {
                    previous = loadJsonDocument(previousPath);
                }
                if (previous.is_object() && previous.contains("artwork") && previous["artwork"].is_array()) {
                    manifest["artwork"] = previous["artwork"];
                    manifest["artworkPreservedFromPrevious"] = true;
                    summarizeArtworkManifest(manifest);
                    manifest["required"] = previous.value("required", json{{"tall", false}, {"hero", false}});
                }
            } catch (...) {
                // Metadata-only mode remains valid even if a legacy artwork
                // manifest is damaged; the next artwork stage can rebuild it.
            }
            writeJsonAtomic(gameOutput / L"manifest.json", manifest);
            std::cout << "METADATA_ONLY: true\n";
        } else for (const auto& spec : artworkSpecs()) {
            json item = {
                {"type", spec.type},
                {"steamSuffix", spec.steamSuffix},
                {"targetWidth", spec.targetWidth},
                {"targetHeight", spec.targetHeight},
                {"attempts", json::array()},
                {"ok", false}
            };
            std::cout << "[ARTWORK] " << spec.type << "\n";
            // Manual portrait/landscape decisions are authoritative. Preserve a
            // manually supplied file, and preserve a manual deletion as a
            // protected empty slot; neither may be replaced by auto scraping.
            const char* manualType = spec.type == "tall" ? "cover" : (spec.type == "long" ? "long" : (spec.type == "hero" ? "wallpaper" : nullptr));
            if (manualType) {
                const auto manualItem = manualArtworkForExecutable(manualOverrides, exePath);
                const auto policy = manualItem.is_object() && manualItem.contains("artworkProtection") && manualItem["artworkProtection"].is_object()
                    ? manualItem["artworkProtection"] : json::object();
                const auto policyValue = jsonStringOr(policy, manualType);
                const auto manualIt = manualItem.find(manualType);
                const bool hasManualFile = manualIt != manualItem.end() && manualIt->is_object();
                const auto protectedSource = hasManualFile ? manualArtworkSource(manualItem, manualType) : fs::path{};
                const bool protectedSlot = policyValue == "deleted" || !protectedSource.empty();
                if (protectedSlot) {
                    item["manualProtected"] = true;
                    item["manualProtection"] = policyValue.empty() ? "manual" : policyValue;
                    if (policyValue != "deleted" && hasManualFile) {
                        const auto source = manualArtworkSource(manualItem, manualType);
                        try {
                            if (source.empty()) throw std::runtime_error("手动素材文件不存在");
                            const auto bytes = readBinaryFile(source, MAX_IMAGE_BYTES);
                            const auto info = inspectImage(bytes);
                            if (!info) throw std::runtime_error("手动素材格式无法读取");
                            const auto quality = assessArtworkQuality(spec.type, *info,
                            spec.type == "hero" || spec.type == "long");
                            item["ok"] = true;
                            item["url"] = jsonStringOr(*manualIt, "url");
                            item["provider"] = "manual-override";
                            item["source"] = "manual";
                            item["priority"] = "manual";
                            item["sourceRole"] = "manual-override";
                            item["file"] = toUtf8(source.wstring());
                            item["portableFile"] = toUtf8(source.wstring());
                            item["bytes"] = bytes.size();
                            item["width"] = info->width;
                            item["height"] = info->height;
                            item["quality"] = artworkQualityJson(quality, *info);
                            item["sha256"] = sha256(bytes);
                            item["attempts"] = json::array({{{"provider", "manual-override"}, {"ok", true}}});
                            ++successCount;
                            if (spec.type == "tall") tallOk = true;
                            if (spec.type == "hero") heroOk = true;
                        } catch (const std::exception& error) {
                            item["manualProtectionError"] = error.what();
                        }
                    } else {
                        item["manualSkipReason"] = "manual-deleted";
                    }
                    manifest["artwork"].push_back(std::move(item));
                    continue;
                }
            }
            if (emulatorLauncher) {
                json attempt = {
                    {"provider", "local-emulator-brand"},
                    {"sourceRole", "generated-from-exe-icon"},
                    {"offlineFallback", true},
                    {"networkAttempts", json::array()}
                };
                try {
                    const fs::path target = gameOutput / toWide(spec.type + ".png");
                    const bool usedIcon = generateEmulatorArtworkPng(exePath, *emulatorProfile, spec, target);
                    const auto bytes = readBinaryFile(target, MAX_IMAGE_BYTES);
                    const auto info = inspectImage(bytes);
                    if (!info || !dimensionsMatch(spec.type, *info)) {
                        throw std::runtime_error("Generated emulator artwork failed dimension validation");
                    }
                    const auto quality = assessArtworkQuality(spec.type, *info);
                    const auto sourceRole = usedIcon ? "generated-from-exe-icon" : "generated-from-name-initials";
                    attempt["sourceRole"] = sourceRole;
                    attempt["ok"] = true;
                    attempt["bytes"] = bytes.size();
                    attempt["quality"] = artworkQualityJson(quality, *info);
                    item["ok"] = true;
                    item["url"] = nullptr;
                    item["provider"] = "local-emulator-brand";
                    item["sourceRole"] = sourceRole;
                    item["offlineFallback"] = true;
                    item["file"] = toUtf8(target.wstring());
                    item["bytes"] = bytes.size();
                    item["width"] = info->width;
                    item["height"] = info->height;
                    item["quality"] = artworkQualityJson(quality, *info);
                    item["lowQualityOnlyCandidate"] = false;
                    item["sha256"] = sha256(bytes);
                    item["attempts"].push_back(std::move(attempt));
                    ++successCount;
                    if (spec.type == "tall") tallOk = true;
                    if (spec.type == "hero") heroOk = true;
                    std::cout << "  OK local " << info->width << "x" << info->height << " " << bytes.size() << " bytes\n";
                } catch (const std::exception& error) {
                    attempt["ok"] = false;
                    attempt["error"] = error.what();
                    item["attempts"].push_back(std::move(attempt));
                    std::cout << "  FAIL " << error.what() << "\n";
                }
                manifest["artwork"].push_back(std::move(item));
                continue;
            }
            std::optional<DownloadedArtworkCandidate> preservedAdaptedFallback;
            if (previousPortableManifest.contains("artwork") &&
                previousPortableManifest["artwork"].is_array()) {
                const json* previousItem = nullptr;
                for (const auto& candidate : previousPortableManifest["artwork"]) {
                    if (candidate.is_object() && jsonStringOr(candidate, "type") == spec.type &&
                        jsonBoolSafe(candidate, "ok", false)) {
                        previousItem = &candidate;
                        break;
                    }
                }
                if (previousItem) {
                    const auto source = manifestArtworkSource(*previousItem);
                    try {
                        if (source.empty()) throw std::runtime_error("existing artwork file is missing");
                        const auto bytes = readBinaryFile(source, MAX_IMAGE_BYTES);
                        const auto info = inspectImage(bytes);
                        if (!info) throw std::runtime_error("existing artwork is not readable");
                        const auto quality = assessArtworkQuality(spec.type, *info, spec.type == "hero" || spec.type == "long");
                        if (quality.band == ArtworkQualityBand::Rejected) {
                            throw std::runtime_error("existing artwork quality is rejected");
                        }
                        if (jsonStringOr(*previousItem, "sourceRole") == "automatic-landscape-cover-fallback" ||
                            jsonStringOr(*previousItem, "sourceRole") == "automatic-fitted-background-fallback") {
                            auto fallbackQuality = quality;
                            fallbackQuality.band = ArtworkQualityBand::UniqueFallback;
                            fallbackQuality.reason = "related-landscape-fitted-to-slot-not-original-artwork";
                            ArtworkCandidate fallback{jsonStringOr(*previousItem, "url"), jsonStringOr(*previousItem, "provider"),
                                jsonStringOr(*previousItem, "sourceRole"), "", false, jsonStringOr(*previousItem, "url"), 100, 0, true};
                            preservedAdaptedFallback = DownloadedArtworkCandidate{fallback, bytes, *info, fallbackQuality};
                            // Keep it offline, but allow a real portrait to replace it.
                        } else {
                            item = *previousItem;
                            item["ok"] = true;
                            item["file"] = toUtf8(source.wstring());
                            item["portableFile"] = toUtf8(source.wstring());
                            item["preservedExisting"] = true;
                            item["source"] = jsonStringOr(item, "provider") == "manual-override" ? "manual" : "automatic";
                            item["priority"] = item["source"];
                            item["attempts"] = json::array({{{
                                {"provider", "existing-portable-artwork"},
                                {"sourceRole", "igdb-second-round-preserved"},
                                {"ok", true},
                                {"cacheHit", true}
                            }}});
                            manifest["artwork"].push_back(std::move(item));
                            ++successCount;
                            if (spec.type == "tall") tallOk = true;
                            if (spec.type == "hero") heroOk = true;
                            std::cout << "  PRESERVE existing " << spec.type << " "
                                      << info->width << "x" << info->height << "\n";
                            continue;
                        }
                    } catch (const std::exception& error) {
                        item["existingArtworkWarning"] = error.what();
                    }
                }
            }
            json discoveryAttempts=json::array();
            auto candidates = artworkUrls(*resolved, spec, &discoveryAttempts);
            if (spec.type == "icon" && !steamArtworkOnly && resolved->appId > 0 &&
                resolved->identityStatus == "verified") {
                try {
                    json gridAttempts = json::array();
                    const auto grid = steamGridDbIconSearch(resolved->name, &gridAttempts);
                    for (const auto& attempt : gridAttempts) discoveryAttempts.push_back(attempt);
                    for (const auto& value : grid.value("candidates", json::array())) {
                        const auto url = jsonStringOr(value, "url");
                        if (url.empty()) continue;
                        candidates.push_back({url, "steamgriddb", "steamgriddb-icon",
                            "https://www.steamgriddb.com/", false, url, 500, 1});
                    }
                    item["steamGridDbCandidateCount"] = grid.value("candidates", json::array()).size();
                } catch (const std::exception& error) {
                    item["steamGridDbError"] = error.what();
                }
            }
            if(!discoveryAttempts.empty())item["discoveryNetworkAttempts"]=std::move(discoveryAttempts);
            std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
                return automaticArtworkProviderPriority(a.provider) > automaticArtworkProviderPriority(b.provider);
            });
            item["candidateCount"] = candidates.size();
            std::set<std::string> successfullyFetchedAssets;
            std::optional<DownloadedArtworkCandidate> bestUsable;
            auto bestUniqueFallback = std::move(preservedAdaptedFallback);
            size_t candidateIndex = 0;
            int fallbackStage = 0;
            for (;;) {
                if (candidateIndex >= candidates.size()) {
                    if (bestUsable || steamArtworkOnly || spec.type == "logo" || spec.type == "icon" || fallbackStage >= 2) break;
                    const auto type = spec.type == "tall" ? "cover" : spec.type == "hero" ? "wallpaper" : "long";
                    const auto provider = fallbackStage++ == 0 ? "playnite-igdb" : "baidu-image";
                    json searchAttempt = {{"stage", "automatic-slot-fallback"}, {"provider", provider}, {"type", spec.type}, {"ok", false}};
                    try {
                        const auto search = std::string(provider) == "playnite-igdb"
                            ? playniteIgdbArtworkSearch(resolved->name, type) : baiduArtworkSearch(resolved->name, type, true);
                        searchAttempt["networkAttempts"] = search.value("networkAttempts", json::array());
                        const auto appended = appendAutomaticArtworkSearchCandidates(search, resolved->name, spec, candidates);
                        searchAttempt["candidateCount"] = appended;
                        searchAttempt["ok"] = appended > 0;
                    } catch (const std::exception& error) { searchAttempt["error"] = error.what(); }
                    item["attempts"].push_back(std::move(searchAttempt));
                    item["candidateCount"] = candidates.size();
                    continue;
                }
                const auto candidate = candidates[candidateIndex++];
                // A usable higher-tier image wins before inspecting lower-tier hits.
                if (bestUsable && automaticArtworkProviderPriority(bestUsable->candidate.provider) > automaticArtworkProviderPriority(candidate.provider)) break;
                if (!candidate.assetKey.empty() && successfullyFetchedAssets.count(candidate.assetKey)) continue;
                json attempt = {
                    {"url", candidate.url},
                    {"provider", candidate.provider},
                    {"sourceRole", candidate.sourceRole},
                    {"assetKey", candidate.assetKey},
                    {"expectedRank", candidate.expectedRank},
                    {"identityPriority", candidate.identityPriority},
                    {"networkAttempts", json::array()}
                };
                try {
                    DWORD negativeStatus = 0;
                    if (loadArtworkNegativeCache(candidate.url, negativeStatus)) {
                        attempt["negativeCacheHit"] = true;
                        attempt["status"] = negativeStatus;
                        attempt["ok"] = false;
                        attempt["error"] = "HTTP 404 (short negative cache)";
                        item["attempts"].push_back(std::move(attempt));
                        std::cout << "  NEGATIVE-CACHE HTTP " << negativeStatus << "\n";
                        continue;
                    }
                    if (const auto cached = loadArtworkUrlCache(dataRoot, candidate, spec)) {
                        const auto& info = cached->image;
                        const auto& quality = cached->quality;
                        if (!candidate.assetKey.empty()) successfullyFetchedAssets.insert(candidate.assetKey);
                        attempt["cacheHit"] = true;
                        attempt["bytes"] = cached->bytes.size();
                        attempt["width"] = info.width;
                        attempt["height"] = info.height;
                        attempt["extension"] = info.extension;
                        attempt["quality"] = artworkQualityJson(quality, info);
                        attempt["ok"] = true;
                        attempt["selected"] = false;
                        if (quality.band >= ArtworkQualityBand::Usable) {
                            if (!bestUsable || betterArtworkCandidate(*cached, *bestUsable)) {
                                bestUsable = *cached;
                            }
                        } else if (!bestUniqueFallback || betterArtworkCandidate(*cached, *bestUniqueFallback)) {
                            bestUniqueFallback = *cached;
                        }
                        item["attempts"].push_back(std::move(attempt));
                        std::cout << "  CACHE " << info.width << "x" << info.height
                                  << " " << artworkQualityBandName(quality.band) << "\n";
                        if (quality.band == ArtworkQualityBand::Preferred ||
                            (spec.type == "long" && quality.band == ArtworkQualityBand::Usable)) break;
                        continue;
                    }
                    const auto artworkRequestUrl = supportedArtworkRequestUrl(candidate.url);
                    auto response = httpGet(
                        artworkRequestUrl,
                        kSupportedArtworkAccept,
                        MAX_IMAGE_BYTES,
                        true,
                        candidate.provider,
                        candidate.referer,
                        &attempt["networkAttempts"]);
                    attempt["status"] = response.status;
                    attempt["bytes"] = response.body.size();
                    saveArtworkNegativeCache(candidate.url, response.status);
                    if (response.status < 200 || response.status >= 300) throw std::runtime_error("HTTP " + std::to_string(response.status));
                    auto info = inspectImage(response.body);
                    if (!info) {
                        const auto fallback = retrySteamResponseAfterSemanticFailure(
                            artworkRequestUrl,
                            kSupportedArtworkAccept,
                            MAX_IMAGE_BYTES,
                            true,
                            candidate.referer,
                            "HTTP success response was not a supported image",
                            &attempt["networkAttempts"]);
                        if (fallback) {
                            response = *fallback;
                            attempt["status"] = response.status;
                            attempt["bytes"] = response.body.size();
                            if (response.status >= 200 && response.status < 300) {
                                info = inspectImage(response.body);
                            }
                        }
                    }
                    if (!info) throw std::runtime_error("Response is not a supported PNG/JPEG image");
                    if (spec.type == "logo") inspectLogoVisiblePixels(response.body, *info);
                    if (!candidate.assetKey.empty()) successfullyFetchedAssets.insert(candidate.assetKey);
                    if (candidate.fitCanvas) {
                        // Validate downloaded bytes too, not only search dimensions.
                        if (!usableArtworkFittingSource(*info))
                            throw std::runtime_error("Downloaded landscape cannot be a cover fallback");
                        attempt["originalWidth"] = info->width;
                        attempt["originalHeight"] = info->height;
                        response.body = fitLandscapeArtworkToSlot(response.body, spec);
                        info = inspectImage(response.body);
                        if (!info) throw std::runtime_error("Adapted portrait fallback cannot be decoded");
                        attempt["adaptation"] = "fit-full-image-without-cropping";
                    }
                    auto quality = assessArtworkQuality(spec.type, *info, candidate.genericBackground);
                    if (candidate.fitCanvas) {
                        quality.band = ArtworkQualityBand::UniqueFallback;
                        quality.reason = "related-landscape-fitted-to-slot-not-original-artwork";
                    }
                    attempt["width"] = info->width;
                    attempt["height"] = info->height;
                    attempt["extension"] = info->extension;
                    attempt["quality"] = artworkQualityJson(quality, *info);
                    if (quality.band == ArtworkQualityBand::Rejected) {
                        throw std::runtime_error("Artwork quality rejected: " + quality.reason);
                    }
                    saveArtworkUrlCache(dataRoot, candidate, spec, response.body, *info, quality);
                    attempt["ok"] = true;
                    attempt["selected"] = false;
                    DownloadedArtworkCandidate downloaded{
                        candidate, response.body, *info, quality
                    };
                    if (quality.band >= ArtworkQualityBand::Usable) {
                        if (!bestUsable || betterArtworkCandidate(downloaded, *bestUsable)) {
                            bestUsable = std::move(downloaded);
                        }
                    } else if (!bestUniqueFallback || betterArtworkCandidate(downloaded, *bestUniqueFallback)) {
                        bestUniqueFallback = std::move(downloaded);
                    }
                    item["attempts"].push_back(std::move(attempt));
                    std::cout << "  CANDIDATE " << info->width << "x" << info->height
                              << " " << artworkQualityBandName(quality.band)
                              << " score=" << std::fixed << std::setprecision(1) << quality.score << "\n";
                    if (quality.band == ArtworkQualityBand::Preferred ||
                        (spec.type == "long" && quality.band == ArtworkQualityBand::Usable)) break;
                } catch (const std::exception& error) {
                    attempt["ok"] = false;
                    attempt["error"] = error.what();
                    NetworkEvidence networkEvidence;
                    collectNetworkEvidence(attempt.value("networkAttempts", json::array()), networkEvidence);
                    if (networkEvidence.retryableFailures > 0) {
                        artworkNetworkFailures.push_back({
                            {"type", spec.type},
                            {"provider", candidate.provider},
                            {"url", candidate.url},
                            {"error", error.what()},
                            {"successfulResponses", networkEvidence.successfulResponses},
                            {"retryableFailures", networkEvidence.retryableFailures},
                            {"networkAttempts", attempt.value("networkAttempts", json::array())}
                        });
                    }
                    item["attempts"].push_back(std::move(attempt));
                    std::cout << "  FAIL " << error.what() << "\n";
                }
            }
            const DownloadedArtworkCandidate* selected = bestUsable
                ? &*bestUsable
                : (bestUniqueFallback ? &*bestUniqueFallback : nullptr);
            if (selected) {
                const fs::path target = gameOutput /
                    toWide(spec.type + "." + selected->image.extension);
                writeAtomic(target, selected->bytes);
                const bool lowQualityOnlyCandidate =
                    selected->quality.band == ArtworkQualityBand::UniqueFallback;
                item["ok"] = true;
                item["url"] = selected->candidate.url;
                item["provider"] = selected->candidate.provider;
                item["source"] = "automatic";
                item["priority"] = "automatic";
                item["sourceRole"] = selected->candidate.sourceRole;
                item["file"] = toUtf8(target.wstring());
                item["bytes"] = selected->bytes.size();
                item["width"] = selected->image.width;
                item["height"] = selected->image.height;
                item["quality"] = artworkQualityJson(selected->quality, selected->image);
                item["lowQualityOnlyCandidate"] = lowQualityOnlyCandidate;
                item["sha256"] = sha256(selected->bytes);
                ++successCount;
                if (spec.type == "tall") tallOk = true;
                if (spec.type == "hero") heroOk = true;
                std::cout << "  SELECT " << selected->image.width << "x" << selected->image.height
                          << " " << artworkQualityBandName(selected->quality.band)
                          << (lowQualityOnlyCandidate ? " only-candidate-exception" : "") << "\n";
            }
            manifest["artwork"].push_back(std::move(item));
        }

        manifest["artworkPriorityPolicy"] = "manual > automatic > none; explicit manual deletion is an opt-out";
        summarizeArtworkManifest(manifest);
        successCount = positiveJsonInt(manifest, "successCount");
        tallOk = jsonBoolSafe(manifest["required"], "tall", false);
        heroOk = jsonBoolSafe(manifest["required"], "hero", false);
        const auto finalManual = manualArtworkForExecutable(manualOverrides, exePath);
        const auto finalProtection = finalManual.contains("artworkProtection") && finalManual["artworkProtection"].is_object()
            ? finalManual["artworkProtection"] : json::object();
        // An explicit deletion is a settled empty slot, not a download failure.
        // Readiness still reports that no image exists; only retry/command success
        // treats the user's opt-out as satisfied.
        manifest["ok"] = metadataOnly ||
            ((tallOk || jsonStringOr(finalProtection, "cover") == "deleted") &&
             (heroOk || jsonStringOr(finalProtection, "wallpaper") == "deleted"));
        json terminalFailure = nullptr;
        if (!manifest["ok"].get<bool>()) {
            if (!artworkNetworkFailures.empty()) {
                terminalFailure = {
                    {"kind", "network-unavailable"},
                    {"stage", "artwork"},
                    {"retryWhenNetworkReturns", true},
                    {"partialArtworkCount", successCount},
                    {"required", manifest["required"]},
                    {"artworkNetworkFailures", artworkNetworkFailures}
                };
                auto retryJob = enqueueNetworkFailure(terminalFailure);
                manifest["networkRetry"] = retryJob;
            } else {
                terminalFailure = {
                    {"kind", "artwork-incomplete"},
                    {"stage", "artwork"},
                    {"retryWhenNetworkReturns", false},
                    {"partialArtworkCount", successCount},
                    {"required", manifest["required"]}
                };
            }
            manifest["failure"] = terminalFailure;
        }
        // The initial manifest is assembled before artwork starts, so refresh
        // the route snapshot after all requests. This makes a local timeout
        // followed by canonical-URL fallback visible in the final manifest.
        manifest["networkPolicy"]["acceleratorDiagnostics"] = steamAcceleratorDiagnostics();
        try {
            if (!metadataOnly) persistArtworkToSteamBigPictureData(dataRoot, exePath, manifest);
        } catch (const std::exception& error) {
            manifest["portableDataError"] = error.what();
            std::cerr << "PORTABLE_DATA_WARNING: " << error.what() << "\n";
        }
        writeJsonAtomic(gameOutput / L"manifest.json", manifest);

        if (manifest["ok"].get<bool>() && (simulateApply || realApply)) {
            const auto plan = applySteamArtwork(
                exePath, gameOutput, dataRoot, manifest, explicitShortcuts, requestedShortcutId, realApply, onlyMissing);
            if (realApply) {
                std::cout << "REAL_APPLY: " << plan.value("realGridDirectory", std::string{}) << "\n";
                std::cout << "REAL_STEAM_WRITE: true\n";
            } else {
                std::cout << "SIMULATED_APPLY: " << plan.value("simulatedGridDirectory", std::string{}) << "\n";
                std::cout << "REAL_STEAM_WRITE: false\n";
            }
        }

        std::cout << "FORMAL_NAME: " << resolved->name << "\n";
        std::cout << "DISPLAY_NAME: " << resolved->displayName << "\n";
        std::cout << "IDENTITY_STATUS: " << resolved->identityStatus << "\n";
        std::cout << "IDENTITY_KIND: " << resolved->identityKind << "\n";
        std::cout << "PRIMARY_PROVIDER: " << resolved->primaryProvider << "\n";
        if (resolved->appId > 0) std::cout << "APP_ID: " << resolved->appId << "\n";
        if (resolved->storefrontAppId > 0) std::cout << "STOREFRONT_APP_ID: " << resolved->storefrontAppId << "\n";
        if (resolved->matchedContentAppId > 0) std::cout << "MATCHED_CONTENT_APP_ID: " << resolved->matchedContentAppId << "\n";
        if (resolved->igdbId > 0) std::cout << "IGDB_ID: " << resolved->igdbId << "\n";
        std::cout << "ARTWORK_COUNT: " << successCount << "\n";
        std::cout << "OUTPUT: " << toUtf8(gameOutput.wstring()) << "\n";
        if ((steamIdentityOnly || steamComprehensiveIdentityOnly) && !(resolved->primaryProvider == "steam" &&
            resolved->appId > 0 && resolved->steamVerificationStatus == "steam-verified")) {
            std::cout << "STEAM_ID_RESOLVED: false\n";
            std::cerr << "Automatic Steam identity was not officially verified.\n";
            return 4;
        }
        if (!manifest["ok"].get<bool>()) {
            std::cout << "FAILURE_KIND: " << terminalFailure.value("kind", std::string{"artwork-incomplete"}) << "\n";
            std::cerr << "Required tall + hero artwork was not completed.\n";
            return 3;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << "\n";
        return 1;
    }
}
